#include "mister.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "card.h"
#include "log.h"

namespace mister {

namespace {

const char *const CmdFifo = "/dev/MiSTer_cmd";
const char *const CoreNameFile = "/tmp/CORENAME";
const char *const FbModeParam = "/sys/module/MiSTer_fb/parameters/mode";
const char *const FakeCore = "/media/fat/_Console/retrogc.rbf";
const char *const GameMgl = "/tmp/game.mgl";
const char *const UinputName = "retrogc-keyboard";

// Main_MiSTer opens input devices again each time it restarts (on every core load): keys sent
// before it opened our virtual keyboard are lost. It grabs the device as soon as it opens it while
// its menu has the screen, which is how we know it is ready. If that is never seen (the Linux
// framebuffer was already shown), trust the probe after this long.
const double MainSettleSeconds = 3.0;
int64_t main_seen_stamp = -1; // core stamp for which Main_MiSTer was seen holding our device
double main_seen_time = 0;    // ... and when: it drops keys sent right after opening a device
const double MainKeyDelay = 0.4;

// The framebuffer the FPGA scaler reads (Main_MiSTer: FB_ADDR + 4096). Big enough for 1920x1080.
const size_t FbMapBytes = 1920 * 1080 * 4;

int uinput_fd = -1;
int uinput_event_fd = -1; // our own device, read side: used to probe Main_MiSTer's input grab
int tty_fd = -1;
double uinput_created = 0; // Main_MiSTer must open (and grab) our device before the probe means anything
uint8_t *fb_map = nullptr;

void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, nullptr);
}

double now_s()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(uinput_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) logf("uinput write: %s", strerror(errno));
}

void press_key(int code)
{
    if (uinput_fd < 0) return;
    emit(EV_KEY, code, 1);
    emit(EV_SYN, SYN_REPORT, 0);
    msleep(60);
    emit(EV_KEY, code, 0);
    emit(EV_SYN, SYN_REPORT, 0);
    msleep(60);
}

// Opens the read side of our uinput device (/dev/input/eventN), found through sysfs.
void open_uinput_event()
{
    char sysname[64] = {};
    if (ioctl(uinput_fd, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0) {
        logf("UI_GET_SYSNAME: %s", strerror(errno));
        return;
    }

    std::string dir = std::string("/sys/devices/virtual/input/") + sysname;
    for (int tries = 0; tries < 20 && uinput_event_fd < 0; tries++) {
        if (DIR *d = opendir(dir.c_str())) {
            while (struct dirent *e = readdir(d)) {
                if (strncmp(e->d_name, "event", 5)) continue;
                std::string node = std::string("/dev/input/") + e->d_name;
                uinput_event_fd = open(node.c_str(), O_RDONLY | O_NONBLOCK);
                if (uinput_event_fd >= 0) logf("Virtual keyboard: %s", node.c_str());
                break;
            }
            closedir(d);
        }
        if (uinput_event_fd < 0) msleep(50);
    }
}

void tty_mode(bool graphics)
{
    // The tty is hung up when the console is reset (a core load restarts Main_MiSTer and its
    // getty): an old descriptor then fails with EIO. Open it fresh each time.
    for (int attempt = 0; attempt < 2; attempt++) {
        if (tty_fd < 0) tty_fd = open("/dev/tty1", O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (tty_fd < 0) {
            logf("open /dev/tty1: %s", strerror(errno));
            return;
        }
        if (ioctl(tty_fd, KDSETMODE, graphics ? KD_GRAPHICS : KD_TEXT) == 0) return;
        int err = errno;
        close(tty_fd);
        tty_fd = -1;
        if (attempt) logf("KDSETMODE %s: %s", graphics ? "graphics" : "text", strerror(err));
    }
}

bool send_command(const std::string &cmd)
{
    int fd = open(CmdFifo, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        logf("Cannot open %s: %s", CmdFifo, strerror(errno));
        return false;
    }
    std::string line = cmd + "\n";
    bool ok = write(fd, line.data(), line.size()) == (ssize_t)line.size();
    close(fd);
    logf("MiSTer_cmd: %s%s", cmd.c_str(), ok ? "" : " (write failed)");
    return ok;
}

// "fmt rb width height stride" of the Linux framebuffer.
bool read_fb_mode(int &w, int &h, int &stride)
{
    FILE *f = fopen(FbModeParam, "r");
    if (!f) return false;
    int fmt, rb;
    bool ok = fscanf(f, "%d %d %d %d %d", &fmt, &rb, &w, &h, &stride) == 5;
    fclose(f);
    return ok;
}

// fb_cmd1 and wait until Main_MiSTer applied it (it writes the module parameter last).
bool set_fb_mode(int want_w, int want_h, int &w, int &h, int &stride)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "fb_cmd1 8888 1 %d %d", want_w, want_h);
    if (!send_command(cmd)) return false;

    for (int i = 0; i < 40; i++) {
        msleep(25);
        if (read_fb_mode(w, h, stride) && (w == want_w || want_w > 2000) && (h == want_h || want_h > 2000) && i >= 4)
            return true;
    }
    return read_fb_mode(w, h, stride);
}

bool map_framebuffer()
{
    if (fb_map) return true;

    int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        logf("open /dev/fb0: %s", strerror(errno));
        return false;
    }
    struct fb_fix_screeninfo fix;
    bool ok = ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0;
    close(fd);
    if (!ok || !fix.smem_start) {
        logf("FBIOGET_FSCREENINFO failed");
        return false;
    }

    int mem = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (mem < 0) {
        logf("open /dev/mem: %s", strerror(errno));
        return false;
    }
    off_t base = fix.smem_start & ~(off_t)4095;
    size_t delta = fix.smem_start - base;
    void *p = mmap(nullptr, FbMapBytes + delta, PROT_READ | PROT_WRITE, MAP_SHARED, mem, base);
    close(mem);
    if (p == MAP_FAILED) {
        logf("mmap /dev/mem 0x%lx: %s", (unsigned long)fix.smem_start, strerror(errno));
        return false;
    }
    fb_map = (uint8_t *)p + delta;
    logf("Framebuffer mapped at 0x%lx", (unsigned long)fix.smem_start);
    return true;
}

} // namespace

bool init()
{
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (uinput_fd < 0) {
        logf("open /dev/uinput: %s", strerror(errno));
        return false;
    }

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_SYN);
    for (int k = 1; k < 256; k++) ioctl(uinput_fd, UI_SET_KEYBIT, k);

    struct uinput_user_dev dev;
    memset(&dev, 0, sizeof(dev));
    snprintf(dev.name, sizeof(dev.name), "%s", UinputName);
    dev.id.bustype = BUS_VIRTUAL;
    dev.id.vendor = 0x1209;
    dev.id.product = 0x5243;
    dev.id.version = 1;

    if (write(uinput_fd, &dev, sizeof(dev)) != (ssize_t)sizeof(dev) || ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        logf("uinput create: %s", strerror(errno));
        close(uinput_fd);
        uinput_fd = -1;
        return false;
    }

    open_uinput_event();
    uinput_created = now_s();
    return true;
}

void shutdown()
{
    if (uinput_event_fd >= 0) close(uinput_event_fd);
    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }
    if (tty_fd >= 0) close(tty_fd);
    uinput_fd = uinput_event_fd = tty_fd = -1;
}

std::string core_name()
{
    char buf[64] = {};
    FILE *f = fopen(CoreNameFile, "r");
    if (!f) return {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

int64_t core_stamp()
{
    struct stat st;
    if (stat(CoreNameFile, &st) != 0) return 0;
    return (int64_t)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
}

bool menu_ready()
{
    if (!file_exists(CmdFifo) || core_name() != "MENU") return false;
    return core_stamp() != 0;
}

int active_console()
{
    int fd = open("/dev/tty0", O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct vt_stat st;
    int vt = ioctl(fd, VT_GETSTATE, &st) == 0 ? st.v_active : 0;
    close(fd);
    return vt;
}

int framebuffer_shown()
{
    if (uinput_event_fd < 0) return -1;

    // Main_MiSTer holds an exclusive grab on every input device while its menu has the screen,
    // and releases it while the Linux framebuffer is shown. Our own device is probed: grabbing
    // it for a few microseconds steals nothing from the user.
    int64_t stamp = core_stamp();
    if (ioctl(uinput_event_fd, EVIOCGRAB, 1) == 0) {
        ioctl(uinput_event_fd, EVIOCGRAB, 0);
        // Free: the framebuffer is shown, or Main_MiSTer has not opened our device yet
        double now = now_s();
        bool settled = now - uinput_created >= MainSettleSeconds && now - stamp / 1000.0 >= MainSettleSeconds;
        return main_seen_stamp == stamp || settled ? 1 : -1;
    }
    if (errno != EBUSY) return -1;
    if (main_seen_stamp != stamp) main_seen_time = now_s();
    main_seen_stamp = stamp;
    return 0;
}

bool show(Framebuffer &fb)
{
    // F9 toggles: only press it while the menu has the screen, and once more if the first
    // press was lost (Main_MiSTer had just (re)opened its input devices).
    int shown = framebuffer_shown();
    double since = now_s() - main_seen_time;
    if (shown == 0 && since < MainKeyDelay) msleep((int)((MainKeyDelay - since) * 1000));
    for (int attempt = 0; attempt < 3 && shown == 0; attempt++) {
        logf("Pressing F9 (show the Linux framebuffer)");
        press_key(KEY_F9);
        for (int i = 0; i < 12 && shown != 1; i++) {
            msleep(50);
            shown = framebuffer_shown();
        }
    }
    {
        if (shown != 1) {
            logf("F9 did not show the Linux framebuffer (fb_terminal=0 in MiSTer.ini?)");
            return false;
        }
    }

    // fb_cmd1 clamps to the video output: ask for "everything" to learn its size.
    int out_w, out_h, stride;
    if (!set_fb_mode(4096, 4096, out_w, out_h, stride)) {
        logf("Cannot read the framebuffer mode");
        return false;
    }

    // About 540 lines, integer-scaled by the FPGA: 1080p -> 960x540, 720p -> 1280x720,
    // 2160p -> 960x540, 15 kHz 240p -> native.
    int k = std::max(1, (int)lround(out_h / 540.0));
    int w, h;
    if (!set_fb_mode(out_w / k, out_h / k, w, h, stride)) return false;
    logf("Video output %dx%d, framebuffer %dx%d (stride %d)", out_w, out_h, w, h, stride);

    if (!map_framebuffer()) return false;
    if ((size_t)stride * h > FbMapBytes) {
        logf("Framebuffer too large: %dx%d", w, h);
        return false;
    }

    tty_mode(true);
    fb.mem = fb_map;
    fb.width = w;
    fb.height = h;
    fb.stride = stride;
    return true;
}

void hide()
{
    tty_mode(false);
    if (framebuffer_shown() == 1) press_key(KEY_F12);
}

void release_console()
{
    tty_mode(false);
}

std::string launch(const Card &card)
{
    if (!card.has_mister) return "This card has no MiSTer FPGA section (\"misterfpga\").";
    if (card.mister.roms.empty()) return "Missing 'misterfpga.roms' in the card.";

    std::string core, rom, error;
    if (!card.resolve(card.mister.core, core, error) || !card.resolve(card.mister.roms[0], rom, error)) return error;
    if (!file_exists(core.c_str())) return "MiSTer core not found on the card: " + card.mister.core;
    if (!file_exists(rom.c_str())) return "MiSTer ROM not found on the card: " + card.mister.roms[0];
    if (!file_exists(CmdFifo)) return std::string("MiSTer command device not found: ") + CmdFifo;

    release_console();

    std::string rel_rom = card.mister.roms[0];
    while (!rel_rom.empty() && rel_rom[0] == '/') rel_rom.erase(0, 1);

    const size_t n = rom.size();
    if (n > 4 && strcasecmp(rom.c_str() + n - 4, ".mra") == 0) {
        logf("Loading arcade MRA %s", rom.c_str());
        return send_command("load_core " + rom) ? "" : "Cannot send the command to Main_MiSTer.";
    }

    // Same MGL as retrogc.sh: the core is linked into _Console (TODO there: a core path on the
    // card itself is not accepted), the rom path is relative to the core's home folder.
    mkdir("/media/fat/_Console", 0755);
    unlink(FakeCore);
    if (symlink(core.c_str(), FakeCore) != 0) return std::string("Cannot link the core: ") + strerror(errno);

    FILE *f = fopen(GameMgl, "w");
    if (!f) return std::string("Cannot write ") + GameMgl;
    fprintf(f, "<mistergamedescription>\n");
    fprintf(f, "  <rbf>_Console/retrogc</rbf>\n");
    fprintf(f, "  <file delay=\"%s\" index=\"%s\" type=\"%s\" path=\"./../../%s\"/>\n", card.mister.delay.c_str(),
            card.mister.index.c_str(), card.mister.type.c_str(), rel_rom.c_str());
    fprintf(f, "</mistergamedescription>\n");
    fclose(f);

    logf("Loading core %s with %s", core.c_str(), rom.c_str());
    return send_command(std::string("load_core ") + GameMgl) ? "" : "Cannot send the command to Main_MiSTer.";
}

void unload()
{
    unlink(FakeCore);
    send_command("load_core menu.rbf");
}

} // namespace mister

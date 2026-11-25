// RetroGC MiSTer framebuffer spike.
//
// Checks, on a real MiSTer running the stock menu.rbf:
//   1. a uinput virtual keyboard can press F9 to switch Main_MiSTer to the Linux framebuffer
//   2. we can draw on /dev/fb0 without the tty1 console drawing over us (KD_GRAPHICS)
//   3. what input we receive (and whether Main_MiSTer reacts to it) while the framebuffer is on
//   4. pressing F12 hands the screen back to the stock menu
//
// Usage (over SSH): ./fbspike [seconds]   (default 20 s; ESC on a real keyboard ends early)
//
// "MiSTer virtual input" is Main_MiSTer's own uinput device: in framebuffer mode it turns pad
// buttons into keys (A=Enter, B=Esc, X=Space, Y=Tab, L/R=PgUp/PgDn, d-pad=arrows).

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAX_INPUTS 32

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

// ------------------------------------------------------------------ uinput keyboard

static int uinput_open(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int k = 1; k < 256; k++) ioctl(fd, UI_SET_KEYBIT, k);

    struct uinput_user_dev dev;
    memset(&dev, 0, sizeof(dev));
    snprintf(dev.name, sizeof(dev.name), "retrogc-keyboard");
    dev.id.bustype = BUS_VIRTUAL;
    dev.id.vendor = 0x1209;
    dev.id.product = 0x5243; // "RC"
    dev.id.version = 1;

    if (write(fd, &dev, sizeof(dev)) != sizeof(dev) || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("uinput create");
        close(fd);
        return -1;
    }

    // Main_MiSTer picks new input devices up through inotify: give it time to open ours.
    msleep(1500);
    return fd;
}

static void uinput_emit(int fd, int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) perror("uinput write");
}

static void press_key(int fd, int code)
{
    uinput_emit(fd, EV_KEY, code, 1);
    uinput_emit(fd, EV_SYN, SYN_REPORT, 0);
    msleep(60);
    uinput_emit(fd, EV_KEY, code, 0);
    uinput_emit(fd, EV_SYN, SYN_REPORT, 0);
    msleep(60);
}

// ------------------------------------------------------------------ tty

static int tty_fd = -1;

static void tty_graphics(int on)
{
    if (tty_fd < 0) tty_fd = open("/dev/tty1", O_RDWR);
    if (tty_fd < 0) {
        perror("open /dev/tty1");
        return;
    }
    if (ioctl(tty_fd, KDSETMODE, on ? KD_GRAPHICS : KD_TEXT) < 0) perror("KDSETMODE");
    else printf("tty1 -> %s\n", on ? "KD_GRAPHICS" : "KD_TEXT");
}

// ------------------------------------------------------------------ framebuffer

typedef struct {
    int fd;
    int method;      // 0 = fb0 mmap, 1 = /dev/mem mmap, 2 = shadow buffer + write
    uint8_t *mem;
    uint8_t *map;    // raw mapping (for /dev/mem the page-aligned base)
    size_t map_len;
    size_t size;
    int w, h, stride, bpp;
    struct fb_var_screeninfo var;
} fb_t;

static int fb_open(fb_t *fb)
{
    struct fb_fix_screeninfo fix;

    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) {
        perror("open /dev/fb0");
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) < 0 || ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("fb ioctl");
        return -1;
    }

    fb->w = fb->var.xres;
    fb->h = fb->var.yres;
    fb->bpp = fb->var.bits_per_pixel;
    fb->stride = fix.line_length;
    fb->size = (size_t)fix.line_length * fb->var.yres_virtual;

    printf("fb0: id='%s' %dx%d (virtual %dx%d) bpp=%d stride=%d smem_len=%u\n", fix.id, fb->w, fb->h,
           fb->var.xres_virtual, fb->var.yres_virtual, fb->bpp, fb->stride, fix.smem_len);
    printf("fb0: red %d/%d green %d/%d blue %d/%d alpha %d/%d\n", fb->var.red.offset, fb->var.red.length,
           fb->var.green.offset, fb->var.green.length, fb->var.blue.offset, fb->var.blue.length,
           fb->var.transp.offset, fb->var.transp.length);

    if (fb->bpp != 32) {
        fprintf(stderr, "only 32 bpp is handled by this spike\n");
        return -1;
    }

    const char *force = getenv("FB_METHOD"); // 0/1/2 to force a method
    int want = force ? atoi(force) : -1;

    if (want < 0 || want == 0) {
        fb->mem = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
        if (fb->mem != MAP_FAILED) {
            fb->method = 0;
            printf("fb: using fb0 mmap\n");
            return 0;
        }
        printf("fb: fb0 mmap failed: %s\n", strerror(errno));
    }

    if (want < 0 || want == 1) {
        int mfd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mfd >= 0) {
            off_t base = fix.smem_start & ~(off_t)4095;
            size_t delta = fix.smem_start - base;
            fb->map_len = fb->size + delta;
            fb->map = mmap(NULL, fb->map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, base);
            close(mfd);
            if (fb->map != MAP_FAILED) {
                fb->mem = fb->map + delta;
                fb->method = 1;
                printf("fb: using /dev/mem mmap at 0x%lx\n", (unsigned long)fix.smem_start);
                return 0;
            }
            printf("fb: /dev/mem mmap failed: %s\n", strerror(errno));
        } else {
            printf("fb: open /dev/mem failed: %s\n", strerror(errno));
        }
    }

    fb->mem = calloc(1, fb->size);
    if (!fb->mem) return -1;
    fb->method = 2;
    printf("fb: using shadow buffer + write\n");
    return 0;
}

// Push rows [y0, y0+h) to the screen (only needed for the write method).
static void fb_flush(fb_t *fb, int y0, int h)
{
    if (fb->method != 2) return;
    if (y0 < 0) { h += y0; y0 = 0; }
    if (y0 + h > fb->h) h = fb->h - y0;
    if (h <= 0) return;
    size_t off = (size_t)y0 * fb->stride, len = (size_t)h * fb->stride;
    // fbdev has no pwrite (EINVAL): seek, then write
    static int warned;
    if ((lseek(fb->fd, off, SEEK_SET) < 0 || write(fb->fd, fb->mem + off, len) != (ssize_t)len) && !warned++)
        perror("write fb0");
}

static uint32_t rgb(const fb_t *fb, int r, int g, int b)
{
    return ((uint32_t)r << fb->var.red.offset) | ((uint32_t)g << fb->var.green.offset) |
           ((uint32_t)b << fb->var.blue.offset) | (fb->var.transp.length ? (0xFFu << fb->var.transp.offset) : 0);
}

static void fill_rect(fb_t *fb, int x0, int y0, int w, int h, uint32_t c)
{
    for (int y = y0; y < y0 + h && y < fb->h; y++) {
        if (y < 0) continue;
        uint32_t *row = (uint32_t *)(fb->mem + (size_t)y * fb->stride);
        for (int x = x0; x < x0 + w && x < fb->w; x++)
            if (x >= 0) row[x] = c;
    }
}

// Background gradient, launcher colour bars, a frame and a square that moves (shows redraw speed).
static void draw_scene(fb_t *fb, int frame)
{
    static const int bars[6][3] = {
        { 0xFF, 0x3E, 0x7F }, { 0xFF, 0x9A, 0x3C }, { 0xFF, 0xD2, 0x3F },
        { 0x6B, 0xE0, 0x6B }, { 0x3C, 0xE0, 0xD8 }, { 0x9B, 0x5C, 0xFF },
    };

    if (frame == 0) {
        for (int y = 0; y < fb->h; y++) {
            int v = 10 + (y * 30) / fb->h;
            fill_rect(fb, 0, y, fb->w, 1, rgb(fb, v, v / 2 + 6, v + 14));
        }
        int bw = fb->w * 9 / 10 / 6, bx = fb->w / 20, by = fb->h / 8;
        for (int i = 0; i < 6; i++)
            fill_rect(fb, bx + i * bw, by, bw, fb->h / 180 + 2, rgb(fb, bars[i][0], bars[i][1], bars[i][2]));

        uint32_t orange = rgb(fb, 0xFF, 0x9A, 0x3C);
        int fx = fb->w / 20, fy = fb->h / 6, fw = fb->w * 45 / 100, fh = fb->h * 3 / 4, t = 3;
        fill_rect(fb, fx, fy, fw, t, orange);
        fill_rect(fb, fx, fy + fh - t, fw, t, orange);
        fill_rect(fb, fx, fy, t, fh, orange);
        fill_rect(fb, fx + fw - t, fy, t, fh, orange);
    }

    int sz = fb->h / 12, track = fb->w * 4 / 10, x0 = fb->w * 55 / 100, y0 = fb->h / 2;
    int pos = (frame * 8) % (track - sz);
    fill_rect(fb, x0, y0, track, sz, rgb(fb, 16, 12, 30));
    fill_rect(fb, x0 + pos, y0, sz, sz, rgb(fb, 0x3C, 0xE0, 0xD8));

    if (frame == 0) fb_flush(fb, 0, fb->h);
    else fb_flush(fb, y0, sz);
}

// ------------------------------------------------------------------ evdev input (read only, no grab)

static int is_virtual(const char *name) { return !strcmp(name, "MiSTer virtual input"); }

// 1 = Main_MiSTer holds an exclusive grab (OSD open / input back to Main), 0 = free, -1 = error
static int grabbed_by_main(int fd)
{
    if (ioctl(fd, EVIOCGRAB, 1) == 0) {
        ioctl(fd, EVIOCGRAB, 0);
        return 0;
    }
    return errno == EBUSY ? 1 : -1;
}

static int open_inputs(struct pollfd *pfd, char names[][128])
{
    int n = 0;
    DIR *d = opendir("/dev/input");
    struct dirent *e;

    if (!d) return 0;
    while ((e = readdir(d)) && n < MAX_INPUTS) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(128), names[n]);
        if (!strcmp(names[n], "retrogc-keyboard")) {
            close(fd);
            continue;
        }
        printf("input: %s = %s\n", path, names[n]);
        pfd[n].fd = fd;
        pfd[n].events = POLLIN;
        n++;
    }
    closedir(d);
    return n;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    fb_t fb;
    struct pollfd pfd[MAX_INPUTS];
    char names[MAX_INPUTS][128] = { { 0 } };
    int quit = 0, last_grab = -2;
    double last_probe = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    int ui = uinput_open();
    if (ui < 0) return 1;

    printf("pressing F9 (switch to Linux framebuffer)\n");
    press_key(ui, KEY_F9);
    msleep(500);

    tty_graphics(1);

    if (fb_open(&fb) < 0) {
        tty_graphics(0);
        press_key(ui, KEY_F12);
        return 1;
    }

    int nin = open_inputs(pfd, names);
    printf("drawing for %d s; press keys/buttons to log them (ESC or Select+Start quits)\n", seconds);

    double start = now_s(), last_fps = start;
    int frame = 0, frames = 0;

    while (!quit && now_s() - start < seconds) {
        draw_scene(&fb, frame++);
        frames++;

        if (now_s() - last_fps >= 2.0) {
            printf("redraw rate: %.1f/s\n", frames / (now_s() - last_fps));
            frames = 0;
            last_fps = now_s();
        }

        if (now_s() - last_probe >= 0.5) {
            last_probe = now_s();
            for (int i = 0; i < nin; i++) {
                if (is_virtual(names[i])) continue;
                int g = grabbed_by_main(pfd[i].fd);
                if (g != last_grab) {
                    printf("probe [%s]: %s\n", names[i],
                           g == 1 ? "GRABBED by Main (OSD open?)" : g == 0 ? "free (our screen is active)" : "error");
                    last_grab = g;
                }
                break; // one physical device is enough
            }
        }

        if (nin && poll(pfd, nin, 16) > 0) {
            for (int i = 0; i < nin; i++) {
                struct input_event ev;
                if (!(pfd[i].revents & POLLIN)) continue;
                while (read(pfd[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type == EV_KEY) {
                        printf("[%s] key %d %s\n", names[i], ev.code,
                               ev.value == 1 ? "down" : ev.value == 0 ? "up" : "repeat");
                        if (ev.code == KEY_ESC && ev.value == 1 && !is_virtual(names[i])) quit = 1;
                    } else if (ev.type == EV_ABS && (ev.value > 200 || ev.value < -200 || ev.code >= ABS_HAT0X)) {
                        printf("[%s] abs %d = %d\n", names[i], ev.code, ev.value);
                    }
                }
            }
        } else if (!nin) {
            msleep(16);
        }
    }

    // Leave the console as we found it, then give the screen back to the stock menu.
    fill_rect(&fb, 0, 0, fb.w, fb.h, rgb(&fb, 0, 0, 0));
    fb_flush(&fb, 0, fb.h);
    tty_graphics(0);
    // F12 is Main_MiSTer's documented way out of framebuffer mode: it switches the framebuffer
    // off, redraws the menu wallpaper and opens the menu (menu.cpp, KEY_F12 | UPSTROKE).
    printf("pressing F12 (back to the stock menu)\n");
    press_key(ui, KEY_F12);
    msleep(300);

    ioctl(ui, UI_DEV_DESTROY);
    close(ui);
    printf("done\n");
    return 0;
}

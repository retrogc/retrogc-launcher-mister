#include "app.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <memory>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "card.h"
#include "card_monitor.h"
#include "image.h"
#include "log.h"
#include "lvgl.h"
#include "mister.h"
#include "settings.h"
#include "ui.h"

#ifndef RETROGC_VERSION
#define RETROGC_VERSION "0.0.0"
#endif

namespace {

volatile sig_atomic_t g_quit = 0;
volatile sig_atomic_t g_show = 0;

void on_signal(int sig)
{
    if (sig == SIGUSR1) g_show = 1;
    else g_quit = 1;
}

double now_s()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

uint32_t tick_ms()
{
    return (uint32_t)(now_s() * 1000.0);
}

enum class Action { None, Play, Cancel, Up, Down, Left, Right, Settings, ToggleCard, Quit };

// ------------------------------------------------------------------ MiSTer input (evdev)

// Reads Main_MiSTer's "MiSTer virtual input" (pads mapped to keys while the Linux framebuffer
// is shown: A=Enter, B=Esc, d-pad=arrows, L/R=PgUp/PgDn) and real keyboards. Nothing is grabbed:
// Main_MiSTer keeps seeing everything (its OSD button / F12 still open the menu).
class EvdevInput {
public:
    ~EvdevInput()
    {
        for (auto &d : devs_) close(d.fd);
    }

    void poll(std::vector<Action> &out)
    {
        double now = now_s();
        if (now - last_scan_ > 2.0) {
            last_scan_ = now;
            rescan();
        }

        for (size_t i = 0; i < devs_.size(); i++) {
            struct input_event ev;
            ssize_t n;
            while ((n = read(devs_[i].fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
                if (ev.type != EV_KEY || ev.value == 0) continue;
                bool repeat = ev.value == 2;
                switch (ev.code) {
                case KEY_ENTER:
                case KEY_KPENTER:
                    if (!repeat) out.push_back(Action::Play);
                    break;
                case KEY_SPACE: // pad X
                case KEY_TAB:   // pad Y
                case KEY_S:
                    if (!repeat) out.push_back(Action::Settings);
                    break;
                case KEY_LEFT:
                    out.push_back(Action::Left);
                    break;
                case KEY_RIGHT:
                    out.push_back(Action::Right);
                    break;
                case KEY_ESC:
                case KEY_BACKSPACE:
                    if (!repeat) out.push_back(Action::Cancel);
                    break;
                case KEY_UP:
                case KEY_PAGEUP:
                    out.push_back(Action::Up);
                    break;
                case KEY_DOWN:
                case KEY_PAGEDOWN:
                    out.push_back(Action::Down);
                    break;
                default: break;
                }
            }
            if (n < 0 && errno == ENODEV) { // unplugged, or Main_MiSTer restarted
                close(devs_[i].fd);
                devs_.erase(devs_.begin() + i--);
            }
        }
    }

    // Drops pending events (the button that showed our screen must not also press "Play").
    void flush()
    {
        std::vector<Action> ignored;
        poll(ignored);
    }

private:
    struct Dev {
        int fd;
        std::string node;
    };

    void rescan()
    {
        DIR *d = opendir("/dev/input");
        if (!d) return;
        while (struct dirent *e = readdir(d)) {
            if (strncmp(e->d_name, "event", 5)) continue;
            std::string node = std::string("/dev/input/") + e->d_name;
            if (std::any_of(devs_.begin(), devs_.end(), [&](const Dev &x) { return x.node == node; })) continue;

            int fd = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            char name[128] = {};
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);

            unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
            auto has = [&](int k) { return (keys[k / (8 * sizeof(long))] >> (k % (8 * sizeof(long)))) & 1; };

            bool main_virtual = !strcmp(name, "MiSTer virtual input");
            bool keyboard = has(KEY_ENTER) && has(KEY_A) && has(KEY_Z) && strcmp(name, "retrogc-keyboard") != 0;
            if (main_virtual || keyboard) {
                logf("Input: %s (%s)", node.c_str(), name);
                devs_.push_back({ fd, node });
            } else {
                close(fd);
            }
        }
        closedir(d);
    }

    std::vector<Dev> devs_;
    double last_scan_ = -10;
};

// ------------------------------------------------------------------ the launcher

class App {
public:
    explicit App(const Options &o) : opt_(o), monitor_("RETROGC", o.mode == Options::Mode::Mister ? "" : o.card_dir) {}

    int run();

private:
    enum class Phase { Waiting, Countdown, Ready, Playing, Error };

    // display
    bool create_display(int w, int h);
    static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px);

    // flow
    void on_inserted();
    void on_removed();
    void start_game();
    void handle(Action a);
    void update_visibility();
    void tick();
    void refresh();
    void set_waiting();

    int screenshot();

    // settings panel
    void open_settings();
    void close_settings();
    void settings_rows();
    void change_setting(int delta);
    bool boot_hook_installed() const;
    int boot_mode() const; // 0: show the launcher, 1: in the background, 2: disabled
    void set_boot_mode(int mode);
    static constexpr int SettingsCount = 4;
    bool preview_boot_disabled_ = false; // desktop preview: no boot hook to look at
    int autostart() const { return opt_.autostart >= -1 ? opt_.autostart : settings_.autostart; }

    Options opt_;
    Settings settings_;
    std::string config_path_;
    bool settings_open_ = false;
    int settings_sel_ = 0;
    CardMonitor monitor_;
    Ui ui_;
    EvdevInput evdev_;

    lv_display_t *disp_ = nullptr;
    std::vector<uint8_t> shadow_;
    mister::Framebuffer fb_;
    bool visible_ = false; // our screen is what the TV shows (MiSTer)
    bool want_show_ = true;
    double next_vis_check_ = 0;
    std::vector<Action> sdl_actions_;

    Phase phase_ = Phase::Waiting;
    bool has_card_ = false;
    Card card_;
    std::string error_;
    bool launched_ = false; // a core was loaded for the inserted card
    double countdown_start_ = 0;
};

App *g_app = nullptr;

bool App::create_display(int w, int h)
{
    ui_.destroy();
    if (disp_) lv_display_delete(disp_);
    disp_ = nullptr;

    if (opt_.mode == Options::Mode::Desktop) {
#if LV_USE_SDL
        disp_ = lv_sdl_window_create(w, h);
        lv_sdl_window_set_title(disp_, "RetroGC MiSTer launcher (preview)");
        lv_indev_t *kb = lv_sdl_keyboard_create();
        lv_group_t *g = lv_group_create();
        lv_indev_set_group(kb, g);

        // Keys go to a hidden object on the top layer (survives screen changes)
        lv_obj_t *catcher = lv_obj_create(lv_layer_top());
        lv_obj_add_flag(catcher, LV_OBJ_FLAG_HIDDEN);
        lv_group_add_obj(g, catcher);
        lv_obj_add_event_cb(
            catcher,
            [](lv_event_t *e) {
                App *app = (App *)lv_event_get_user_data(e);
                uint32_t key = lv_event_get_key(e);
                Action a = Action::None;
                if (key == LV_KEY_ENTER) a = Action::Play;
                else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) a = Action::Cancel;
                else if (key == LV_KEY_UP) a = Action::Up;
                else if (key == LV_KEY_DOWN) a = Action::Down;
                else if (key == LV_KEY_LEFT) a = Action::Left;
                else if (key == LV_KEY_RIGHT) a = Action::Right;
                else if (key == ' ' || key == 's' || key == 'S') a = Action::Settings;
                else if (key == 'i' || key == 'I') a = Action::ToggleCard;
                else if (key == 'q' || key == 'Q') a = Action::Quit;
                if (a != Action::None) app->sdl_actions_.push_back(a);
            },
            LV_EVENT_KEY, this);
#else
        logf("This build has no SDL support (configure with -DRETROGC_SDL=ON)");
        return false;
#endif
    } else {
        disp_ = lv_display_create(w, h);
        lv_display_set_color_format(disp_, LV_COLOR_FORMAT_XRGB8888);
        shadow_.assign((size_t)w * h * 4, 0);
        lv_display_set_buffers(disp_, shadow_.data(), nullptr, (uint32_t)shadow_.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(disp_, flush_cb);
        lv_display_set_user_data(disp_, this);
    }

    ui_.create(w, h, RETROGC_VERSION, settings_.scanlines);
    if (settings_open_) settings_rows();
    refresh();
    return true;
}

// Copies what LVGL redrew from the RAM buffer to the framebuffer (uncached memory: only write
// the changed rows, never read it back).
void App::flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *)
{
    App *app = (App *)lv_display_get_user_data(disp);
    if (app->opt_.mode == Options::Mode::Mister && app->visible_ && app->fb_.mem) {
        int w = lv_display_get_horizontal_resolution(disp);
        int x1 = std::max<int>(0, area->x1), x2 = std::min<int>(std::min(w, app->fb_.width) - 1, area->x2);
        int y2 = std::min<int>(app->fb_.height - 1, area->y2);
        if (x2 >= x1) {
            size_t bytes = (size_t)(x2 - x1 + 1) * 4;
            for (int y = std::max<int>(0, area->y1); y <= y2; y++)
                memcpy(app->fb_.mem + (size_t)y * app->fb_.stride + x1 * 4,
                       app->shadow_.data() + ((size_t)y * w + x1) * 4, bytes);
        }
    }
    lv_display_flush_ready(disp);
}

void App::set_waiting()
{
    phase_ = Phase::Waiting;
    has_card_ = false;
    error_.clear();
    ui_.set_cover(nullptr);
}

void App::on_inserted()
{
    error_.clear();
    has_card_ = false;

    std::string def = Card::find_definition(monitor_.mount_point());
    if (def.empty()) {
        error_ = "No .rgc card file found on the card.";
    } else if (!Card::load(def, card_, error_)) {
        // error_ set
    } else {
        has_card_ = true;
        logf("Card: %s (%s)", card_.name.c_str(), def.c_str());
        if (!card_.has_mister) error_ = "This card has no MiSTer FPGA section (\"misterfpga\" in the .rgc file).";
    }

    Image cover;
    if (has_card_ && !card_.image_path().empty()) {
        std::string err;
        if (!image_load(card_.image_path(), cover, err)) logf("Cover %s: %s", card_.image_path().c_str(), err.c_str());
    }
    ui_.set_cover(cover.empty() ? nullptr : &cover);

    if (!error_.empty()) {
        phase_ = Phase::Error;
        want_show_ = true;
        refresh();
        return;
    }

    // Another core is running (started from the stock menu): load the game straight away,
    // like the script-only launcher does.
    if (opt_.mode == Options::Mode::Mister && mister::core_name() != "MENU") {
        logf("Core '%s' is running: starting the game without the launcher screen", mister::core_name().c_str());
        start_game();
        return;
    }

    want_show_ = true;
    if (autostart() == 0) {
        start_game();
    } else if (autostart() > 0) {
        phase_ = Phase::Countdown;
        countdown_start_ = now_s();
    } else {
        phase_ = Phase::Ready;
    }
    refresh();
}

void App::on_removed()
{
    if (launched_ && opt_.mode == Options::Mode::Mister) {
        logf("Card removed: back to the menu core");
        mister::unload();
        visible_ = false;
    }
    launched_ = false;
    want_show_ = true;
    set_waiting();
    refresh();
}

void App::start_game()
{
    if (!has_card_) return;

    if (opt_.mode == Options::Mode::Mister) {
        std::string err = mister::launch(card_);
        if (!err.empty()) {
            logf("Launch failed: %s", err.c_str());
            error_ = err;
            phase_ = Phase::Error;
            refresh();
            return;
        }
        visible_ = false; // the game core takes the screen
    } else {
        logf("Would load MiSTer core %s with %s", card_.mister.core.c_str(),
             card_.mister.roms.empty() ? "(no rom)" : card_.mister.roms[0].c_str());
    }

    launched_ = true;
    error_.clear();
    phase_ = Phase::Playing;
    refresh();
}

void App::handle(Action a)
{
    if (a == Action::ToggleCard) {
        monitor_.toggle();
        return;
    }
    if (a == Action::Quit) {
        g_quit = 1;
        return;
    }

    if (settings_open_) {
        switch (a) {
        case Action::Up: settings_sel_ = (settings_sel_ + SettingsCount - 1) % SettingsCount, settings_rows(); break;
        case Action::Down: settings_sel_ = (settings_sel_ + 1) % SettingsCount, settings_rows(); break;
        case Action::Left: change_setting(-1); break;
        case Action::Right:
        case Action::Play: change_setting(1); break;
        case Action::Cancel:
        case Action::Settings: close_settings(); break;
        default: break;
        }
        return;
    }

    switch (a) {
    case Action::Play:
        if (has_card_ && (phase_ == Phase::Countdown || phase_ == Phase::Ready || phase_ == Phase::Error)) start_game();
        break;
    case Action::Cancel:
        if (phase_ == Phase::Countdown) {
            phase_ = Phase::Ready;
            refresh();
        } else if (phase_ == Phase::Playing && opt_.mode != Options::Mode::Mister) {
            phase_ = Phase::Ready; // preview: "the game ended"
            refresh();
        }
        break;
    case Action::Up: ui_.scroll(-1); break;
    case Action::Down: ui_.scroll(1); break;
    case Action::Settings: open_settings(); break;
    default: break;
    }
}

// ------------------------------------------------------------------ settings panel

const char *const BootScript = "/media/fat/Scripts/retrogc_ui.sh";
const char *const StartupFile = "/media/fat/linux/user-startup.sh";

bool App::boot_hook_installed() const
{
    FILE *f = fopen(StartupFile, "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) found = strstr(line, "RetroGC Launcher UI") != nullptr;
    fclose(f);
    return found;
}

void App::open_settings()
{
    settings_open_ = true;
    settings_sel_ = 0;
    settings_rows();
}

void App::close_settings()
{
    settings_open_ = false;
    ui_.hide_settings();
}

int App::boot_mode() const
{
    bool enabled = opt_.mode == Options::Mode::Mister ? boot_hook_installed() : !preview_boot_disabled_;
    return !enabled ? 2 : settings_.show_at_boot ? 0 : 1;
}

void App::set_boot_mode(int mode)
{
    // "Show" vs "background" is the launcher's own setting; "disabled" removes the boot hook
    // that retrogc_ui.sh installed in user-startup.sh.
    if (mode != 2) settings_.show_at_boot = mode == 0;

    bool want_hook = mode != 2;
    if (opt_.mode != Options::Mode::Mister) {
        preview_boot_disabled_ = !want_hook;
    } else if (want_hook != boot_hook_installed()) {
        std::string cmd = std::string(BootScript) + (want_hook ? " --boot on" : " --boot off");
        int rc = system((cmd + " >/dev/null 2>&1").c_str());
        logf("%s: exit %d", cmd.c_str(), rc);
    }
}

void App::settings_rows()
{
    auto on_off = [](bool v) { return std::string(v ? "ON" : "OFF"); };
    std::string start = autostart() < 0 ? "OFF" : autostart() == 0 ? "AT ONCE" : std::to_string(autostart()) + " SEC";
    if (opt_.autostart >= -1) start += " (CMD LINE)";
    static const char *const boot[] = { "SHOW LAUNCHER", "IN BACKGROUND", "DISABLED" };

    std::vector<SettingsRow> rows = {
        { "GAME AUTO START", start },
        { "AT MISTER BOOT", boot[boot_mode()] },
        { "SCANLINES", on_off(settings_.scanlines) },
        { "GO TO MISTER MENU", "" },
    };
    ui_.show_settings(rows, settings_sel_, "A / LEFT / RIGHT: CHANGE  ·  B: CLOSE");
}

void App::change_setting(int delta)
{
    switch (settings_sel_) {
    case 0: {
        static const int steps[] = { -1, 0, 5, 10, 30 };
        int i = 0;
        while (i < 4 && steps[i] != settings_.autostart) i++;
        settings_.autostart = steps[(i + 5 + delta) % 5];
        if (phase_ == Phase::Countdown && settings_.autostart < 0) phase_ = Phase::Ready;
        break;
    }
    case 1: set_boot_mode((boot_mode() + 3 + delta) % 3); break;
    case 2:
        settings_.scanlines = !settings_.scanlines;
        ui_.set_scanlines(settings_.scanlines);
        break;
    case 3:
        if (delta > 0) {
            close_settings();
            if (opt_.mode == Options::Mode::Mister) {
                logf("Going to the MiSTer menu");
                mister::hide();
                visible_ = false;
            }
        }
        return;
    }

    settings_.save(config_path_);
    settings_rows();
    refresh();
}

// Keeps |visible_| in line with what the TV shows, and takes the screen when asked to.
void App::update_visibility()
{
    if (opt_.mode != Options::Mode::Mister) {
        visible_ = true;
        return;
    }

    if (g_show) {
        g_show = 0;
        want_show_ = true;
    }

    double now = now_s();
    if (now < next_vis_check_) return;
    // Quick checks while waiting to take the screen (back from a game), slow ones otherwise
    next_vis_check_ = now + (want_show_ && !visible_ ? 0.05 : 0.5);

    if (!mister::menu_ready()) {
        // A game core (or Main_MiSTer restarting): nothing to show on
        if (visible_) logf("Screen taken by core '%s'", mister::core_name().c_str());
        visible_ = false;
        return;
    }

    int shown = mister::framebuffer_shown();
    if (shown < 0) return; // Main_MiSTer has not opened our virtual keyboard yet

    // The framebuffer shows another console (a script from the Scripts menu runs on tty2): not
    // our screen. Wait until it closes, then take the screen if we were asked to.
    if (shown == 1 && mister::active_console() > 1) {
        if (visible_) logf("Another console (tty%d) is on the screen", mister::active_console());
        visible_ = false;
        return;
    }

    if (visible_ && shown == 0) {
        // The user opened the MiSTer menu (OSD button / F12). F9 brings us back.
        logf("MiSTer menu opened: launcher screen hidden (F9 or a card brings it back)");
        visible_ = false;
        mister::release_console();
        return;
    }

    bool user_f9 = !visible_ && shown == 1;
    if (!visible_ && (want_show_ || user_f9) && phase_ != Phase::Playing) {
        mister::Framebuffer fb;
        if (mister::show(fb)) {
            bool resized = fb.width != fb_.width || fb.height != fb_.height || !disp_;
            fb_ = fb;
            if (resized) create_display(fb.width, fb.height);
            visible_ = true;
            evdev_.flush();
            lv_obj_invalidate(lv_screen_active());
            logf("Launcher screen shown (%dx%d)", fb.width, fb.height);
        } else {
            logf("Cannot show the launcher screen: running without it (cards still start)");
        }
        want_show_ = false;
    }
}

void App::tick()
{
    if (phase_ == Phase::Countdown) {
        // The countdown only runs while the screen is shown
        // ... and stands still while the settings are open
        if (!visible_ || settings_open_) countdown_start_ = now_s();
        else if (now_s() - countdown_start_ >= autostart()) start_game();
        else refresh();
    }
}

void App::refresh()
{
    UiModel m;
    switch (phase_) {
    case Phase::Waiting: m.state = UiState::Waiting; break;
    case Phase::Countdown: m.state = UiState::Loading; break;
    case Phase::Ready: m.state = UiState::Ready; break;
    case Phase::Playing: m.state = UiState::Playing; break;
    case Phase::Error: m.state = UiState::Error; break;
    }

    if (!has_card_) {
        if (phase_ == Phase::Error) {
            m.title = "Unreadable card";
            m.description = "The card was detected but could not be read.";
        } else {
            m.title = "Insert a game card";
            m.description = "Plug in a Retro Game Card (USB stick or SD card labelled RETROGC). "
                            "The game starts automatically as soon as the card is detected.";
        }
    } else {
        m.title = card_.name;
        m.description = card_.description;
        m.platform = card_.platform;
        std::vector<std::string> by;
        if (!card_.developer.empty()) by.push_back(card_.developer);
        if (!card_.year.empty()) by.push_back(card_.year);
        for (size_t i = 0; i < by.size(); i++) m.byline += (i ? "  ·  " : "") + by[i];

        auto up = [](std::string s) {
            for (auto &c : s) c = (char)std::toupper((unsigned char)c);
            return s;
        };
        if (!card_.genre.empty()) m.tags.push_back(up(card_.genre));
        if (!card_.players.empty()) {
            std::string p = up(card_.players);
            m.tags.push_back(p.find("PLAYER") != std::string::npos ? p : p == "1" ? "1 PLAYER" : p + " PLAYERS");
        }
        if (card_.has_mister) m.tags.push_back(card_.mister_core_name());
    }

    m.error = error_;
    m.progress = -1;
    const std::string menu_hint = "X: SETTINGS  ·  OSD: MISTER MENU";
    switch (phase_) {
    case Phase::Waiting:
        m.detail = "Waiting for a card…";
        m.hint = menu_hint;
        break;
    case Phase::Countdown: {
        double left = std::max(0.0, autostart() - (now_s() - countdown_start_));
        char buf[64];
        snprintf(buf, sizeof(buf), "Starting in %d…", (int)left + 1);
        m.detail = buf;
        m.progress = std::clamp((int)(100 * (1.0 - left / std::max(1, autostart()))), 0, 100);
        m.hint = "A: PLAY NOW  ·  B: CANCEL  ·  X: SETTINGS";
        break;
    }
    case Phase::Ready:
        m.detail = "Press A to play.";
        m.progress = 100;
        m.hint = "A: PLAY  ·  " + menu_hint;
        break;
    case Phase::Playing:
        m.detail = "Running on MiSTer";
        m.progress = 100;
        m.hint = menu_hint;
        break;
    case Phase::Error:
        m.detail = "Can't start the game";
        m.progress = 0;
        m.hint = has_card_ ? "A: RETRY  ·  " + menu_hint : menu_hint;
        break;
    }

    ui_.update(m);
}

int App::screenshot()
{
    if (!create_display(opt_.width, opt_.height)) return 1;

    if (!opt_.card_dir.empty() && opt_.shot_state != "waiting") {
        monitor_.toggle();
        if (monitor_.poll() == CardMonitor::Event::Inserted) on_inserted();
        if (opt_.shot_state == "ready") phase_ = Phase::Ready;
        else if (opt_.shot_state == "playing") phase_ = Phase::Playing;
        else if (opt_.shot_state == "error") {
            error_ = "MiSTer ROM not found on the card: games/nes/missing.nes";
            phase_ = Phase::Error;
        } else if (opt_.shot_state == "loading") {
            phase_ = Phase::Countdown;
            countdown_start_ = now_s() - 2;
        }
        refresh();
    }
    if (opt_.shot_state == "settings") open_settings();

    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp_);
    lv_refr_now(disp_);
    if (!image_save_png(opt_.screenshot, shadow_.data(), opt_.width, opt_.height, opt_.width * 4)) {
        logf("Cannot write %s", opt_.screenshot.c_str());
        return 1;
    }
    logf("Saved %s (%dx%d)", opt_.screenshot.c_str(), opt_.width, opt_.height);
    return 0;
}

int App::run()
{
    config_path_ = !opt_.config.empty()                      ? opt_.config
                   : opt_.mode == Options::Mode::Mister ? "/media/fat/retrogc/retrogc-mister.ini"
                                                        : "retrogc-mister.ini";
    if (settings_.load(config_path_)) logf("Settings: %s", config_path_.c_str());
    if (opt_.mode == Options::Mode::Mister) want_show_ = settings_.show_at_boot;

    lv_init();
    lv_tick_set_cb(tick_ms);

    if (opt_.mode == Options::Mode::Screenshot) return screenshot();

    if (opt_.mode == Options::Mode::Mister) {
        if (!mister::init()) logf("No virtual keyboard: the launcher screen cannot be shown (cards still start)");
        // The display is created when the screen is first shown (its size depends on the video mode).
    } else {
        if (!create_display(opt_.width, opt_.height)) return 1;
        if (!opt_.card_dir.empty()) logf("Preview: press I to insert / remove the card, Enter = A, Esc = B, Q quits");
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGUSR1, on_signal);

    while (!g_quit) {
        update_visibility();

        uint32_t idle = disp_ ? lv_timer_handler() : 50;

        std::vector<Action> actions;
        if (opt_.mode == Options::Mode::Mister) {
            evdev_.poll(actions);
            if (!visible_) actions.clear();
        } else {
            actions.swap(sdl_actions_);
        }
        for (Action a : actions) handle(a);

        switch (monitor_.poll()) {
        case CardMonitor::Event::Inserted: on_inserted(); break;
        case CardMonitor::Event::Removed: on_removed(); break;
        case CardMonitor::Event::None: break;
        }

        tick();
        usleep(std::min<uint32_t>(std::max<uint32_t>(idle, 1), 10) * 1000);
    }

    logf("Exiting");
    if (opt_.mode == Options::Mode::Mister) {
        if (visible_) mister::hide();
        else mister::release_console();
        if (launched_) mister::unload();
        mister::shutdown();
    }
    ui_.destroy();
    return 0;
}

} // namespace

int run_app(const Options &options)
{
    App app(options);
    g_app = &app;
    return app.run();
}

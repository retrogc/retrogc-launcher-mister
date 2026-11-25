// RetroGC MiSTer launcher: the PC launcher's "insert a card" screen on MiSTer FPGA.
//
//   retrogc-mister [--autostart N] [--config FILE]  run on the MiSTer (started by retrogc_ui.sh)
//   retrogc-mister --preview [--card DIR] [--size WxH] [--autostart N]
//                                                   desktop preview in a window (SDL build)
//   retrogc-mister --screenshot OUT.png [--card DIR] [--size WxH] [--state STATE]
//                                                   renders one frame to a PNG (any build)

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "app.h"
#include "log.h"

#ifndef RETROGC_VERSION
#define RETROGC_VERSION "0.0.0"
#endif

namespace {

void usage()
{
    printf("RetroGC MiSTer launcher %s\n\n"
           "  retrogc-mister [--autostart N] [--config FILE]\n"
           "  retrogc-mister --preview [--card DIR] [--size WxH] [--autostart N]\n"
           "  retrogc-mister --screenshot OUT.png [--card DIR] [--size WxH] [--state waiting|loading|ready|playing|error]\n\n"
           "  --autostart N  seconds the game info is shown before the game starts\n"
           "                 (0: at once, -1: wait for A); overrides the settings\n"
           "  --config FILE  settings file (default /media/fat/retrogc/retrogc-mister.ini\n"
           "                 on the MiSTer, ./retrogc-mister.ini otherwise)\n",
           RETROGC_VERSION);
}

// The script-only launcher (retrogc.sh) watches the same card: never run both.
bool script_launcher_running()
{
    FILE *f = fopen("/tmp/retrogc-launcher.pid", "r");
    if (!f) return false;
    int pid = 0;
    bool ok = fscanf(f, "%d", &pid) == 1;
    fclose(f);
    return ok && pid > 0 && kill(pid, 0) == 0;
}

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);

    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value after %s\n", a.c_str());
                exit(2);
            }
            return argv[++i];
        };

        if (a == "--preview") o.mode = Options::Mode::Desktop;
        else if (a == "--screenshot") {
            o.mode = Options::Mode::Screenshot;
            o.screenshot = next();
        } else if (a == "--card") o.card_dir = next();
        else if (a == "--state") o.shot_state = next();
        else if (a == "--autostart") o.autostart = std::max(-1, atoi(next().c_str()));
        else if (a == "--config") o.config = next();
        else if (a == "--size") {
            if (sscanf(next().c_str(), "%dx%d", &o.width, &o.height) != 2 || o.width < 120 || o.height < 120) {
                fprintf(stderr, "Bad --size (expected WxH)\n");
                return 2;
            }
        } else if (a == "--version") {
            printf("%s\n", RETROGC_VERSION);
            return 0;
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }

    if (o.mode == Options::Mode::Mister) {
        logf("RetroGC MiSTer launcher %s started (PID %d)", RETROGC_VERSION, (int)getpid());
        if (script_launcher_running()) {
            logf("ERROR: the script-only launcher (retrogc.sh) is running. Turn it OFF first "
                 "(Scripts -> retrogc.sh).");
            return 1;
        }
    }

    return run_app(o);
}

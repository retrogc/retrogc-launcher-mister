// Launcher settings, kept in a small key=value file on the SD card
// (/media/fat/retrogc/retrogc-mister.ini).
#pragma once

#include <string>

struct Settings {
    int autostart = -1;        // seconds before the game starts: -1 = wait for A, 0 = at once
    bool show_at_boot = true;  // take the screen when the MiSTer starts (else: on card insert / F9)
    bool scanlines = true;     // CRT scanline overlay (HDMI only, never at 240p)

    bool load(const std::string &path);
    bool save(const std::string &path) const;
};

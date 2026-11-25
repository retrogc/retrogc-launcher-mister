#pragma once

#include <string>

struct Options {
    enum class Mode { Mister, Desktop, Screenshot } mode = Mode::Mister;
    int width = 960;            // desktop / screenshot size
    int height = 540;
    std::string card_dir;       // desktop / screenshot: directory used as the inserted card
    int autostart = -2;         // overrides the setting when >= -1 (0: at once, -1: wait for A)
    std::string config;         // settings file ("" = default for the mode)
    std::string screenshot;     // screenshot: output PNG
    std::string shot_state;     // screenshot: waiting | ready | loading | playing | error
};

int run_app(const Options &options);

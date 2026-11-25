#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

namespace {

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool to_bool(const std::string &v)
{
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

} // namespace

bool Settings::load(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(l.substr(0, eq)), value = trim(l.substr(eq + 1));

        if (key == "autostart") autostart = atoi(value.c_str()) < 0 ? -1 : atoi(value.c_str());
        else if (key == "show_at_boot") show_at_boot = to_bool(value);
        else if (key == "scanlines") scanlines = to_bool(value);
    }
    fclose(f);
    return true;
}

bool Settings::save(const std::string &path) const
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) mkdir(path.substr(0, slash).c_str(), 0755);

    // Write then rename: never a half-written file if the power goes
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) {
        logf("Cannot write %s", tmp.c_str());
        return false;
    }
    fprintf(f, "# RetroGC MiSTer launcher settings (also editable from the launcher: X button)\n");
    fprintf(f, "# autostart: seconds before the game starts, -1 = wait for A, 0 = at once\n");
    fprintf(f, "autostart=%d\n", autostart);
    fprintf(f, "# show_at_boot: 1 = take the screen at boot, 0 = stay in the background until a card\n");
    fprintf(f, "show_at_boot=%d\n", show_at_boot ? 1 : 0);
    fprintf(f, "scanlines=%d\n", scanlines ? 1 : 0);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(tmp.c_str(), path.c_str()) != 0) {
        logf("Cannot write %s", path.c_str());
        return false;
    }
    sync();
    return true;
}

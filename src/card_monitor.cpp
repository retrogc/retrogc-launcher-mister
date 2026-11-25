#include "card_monitor.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "log.h"

namespace {

double now_s()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

struct Mount {
    std::string source;
    std::string target;
    std::string options;
};

// /proc/mounts escapes spaces and friends as \040 (octal).
std::string unescape(const char *s)
{
    std::string out;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] >= '0' && s[1] <= '7' && s[2] && s[3]) {
            out += (char)strtol(std::string(s + 1, 3).c_str(), nullptr, 8);
            s += 3;
        } else {
            out += *s;
        }
    }
    return out;
}

std::vector<Mount> mounts_of(const std::string &device)
{
    std::vector<Mount> out;
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return out;

    char src[512], dst[512], type[64], opts[512];
    while (fscanf(f, "%511s %511s %63s %511s %*d %*d", src, dst, type, opts) == 4) {
        std::string source = unescape(src);
        char real[PATH_MAX];
        if (source == device || (realpath(source.c_str(), real) && device == real))
            out.push_back({ source, unescape(dst), opts });
    }
    fclose(f);
    return out;
}

bool is_read_only(const std::string &options)
{
    return ("," + options + ",").find(",ro,") != std::string::npos;
}

// mount -o remount,ro <target>, without a shell (the target may contain anything).
bool remount_ro(const std::string &target)
{
    pid_t pid = fork();
    if (pid == 0) {
        execlp("mount", "mount", "-o", "remount,ro", target.c_str(), (char *)nullptr);
        _exit(127);
    }
    if (pid < 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace

CardMonitor::CardMonitor(std::string label, std::string simulated_dir)
    : device_link_("/dev/disk/by-label/" + label), sim_dir_(std::move(simulated_dir))
{
}

void CardMonitor::remount_read_only(const char *reason)
{
    // The automount is read-write: FAT then marks the volume dirty, and pulling the card makes
    // Windows offer to "scan and fix" it. The card is never written, so keep it read-only.
    static std::set<std::string> warned;
    for (const Mount &m : mounts_of(device_)) {
        if (is_read_only(m.options)) continue;
        if (remount_ro(m.target)) {
            logf("Remounted read-only (%s): %s", reason, m.target.c_str());
            warned.erase(m.target);
        } else if (warned.insert(m.target).second) {
            logf("WARNING: could not remount %s read-only (%s)", m.target.c_str(), reason);
        }
    }
}

CardMonitor::Event CardMonitor::poll()
{
    if (!sim_dir_.empty()) {
        if (sim_inserted_ == sim_reported_) return Event::None;
        sim_reported_ = sim_inserted_;
        mount_point_ = sim_inserted_ ? sim_dir_ : std::string();
        return sim_inserted_ ? Event::Inserted : Event::Removed;
    }

    const double now = now_s();
    const double period = state_ == State::WaitMount ? 0.25 : 0.5;
    if (now - last_check_ < period) return Event::None;
    last_check_ = now;

    struct stat st;
    const bool present = stat(device_link_.c_str(), &st) == 0;

    switch (state_) {
    case State::NoDevice: {
        if (!present) return Event::None;
        char real[PATH_MAX];
        device_ = realpath(device_link_.c_str(), real) ? real : device_link_;
        logf("Card device detected: %s, waiting for the automount", device_.c_str());
        state_ = State::WaitMount;
        return Event::None;
    }

    case State::WaitMount: {
        if (!present) {
            logf("Card removed before the automount completed");
            state_ = State::NoDevice;
            return Event::None;
        }
        std::vector<Mount> m = mounts_of(device_);
        if (m.empty()) return Event::None;
        // The card may be mounted more than once: use the first mount point.
        mount_point_ = m[0].target;
        logf("Card mounted at %s", mount_point_.c_str());
        remount_read_only("after automount");
        state_ = State::Mounted;
        return Event::Inserted;
    }

    case State::Mounted:
        if (present) {
            // Something (the automounter, Main_MiSTer loading a core) may mount it read-write again.
            remount_read_only("mounted read-write again");
            return Event::None;
        }
        logf("Card removed (was %s at %s)", device_.c_str(), mount_point_.c_str());
        mount_point_.clear();
        state_ = State::NoDevice;
        return Event::Removed;
    }
    return Event::None;
}

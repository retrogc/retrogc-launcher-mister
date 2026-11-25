// Watches for the RETROGC card, like retrogc.sh: the device /dev/disk/by-label/RETROGC appears,
// the MiSTer automounter mounts it, we keep it mounted read-only while it is inserted, then it
// disappears. poll() is called from the main loop and never blocks.
#pragma once

#include <string>

class CardMonitor {
public:
    enum class Event { None, Inserted, Removed };

    // |label|: volume label to watch. |simulated_dir|: desktop preview, the "card" is this
    // directory and toggle() inserts / removes it.
    explicit CardMonitor(std::string label = "RETROGC", std::string simulated_dir = {});

    Event poll();

    // Mount point of the inserted card ("" when none).
    const std::string &mount_point() const { return mount_point_; }

    // Desktop preview only.
    void toggle() { sim_inserted_ = !sim_inserted_; }

private:
    enum class State { NoDevice, WaitMount, Mounted };

    void remount_read_only(const char *reason);

    std::string device_link_;
    std::string device_;
    std::string mount_point_;
    State state_ = State::NoDevice;
    double last_check_ = 0;

    std::string sim_dir_;
    bool sim_inserted_ = false;
    bool sim_reported_ = false;
};

// Everything that talks to Main_MiSTer (the ARM-side MiSTer binary):
//  - /dev/MiSTer_cmd: load_core, fb_cmd1 (framebuffer resolution)
//  - a uinput virtual keyboard: F9 shows the Linux framebuffer over menu.rbf, F12 goes back to the menu
//  - /tmp/CORENAME: which core is loaded
//  - the framebuffer itself, mapped through /dev/mem (the MiSTer_fb driver has no mmap)
#pragma once

#include <cstdint>
#include <string>

struct Card;

namespace mister {

// Creates the virtual keyboard. Main_MiSTer needs a moment to open it (call well before keys).
bool init();
void shutdown();

// Core currently loaded ("MENU" for menu.rbf), from /tmp/CORENAME.
std::string core_name();

// Modification time of /tmp/CORENAME (changes each time Main_MiSTer starts a core).
int64_t core_stamp();

// True once Main_MiSTer runs the menu core and had time to open our virtual keyboard.
bool menu_ready();

// Active virtual console (Main_MiSTer's F9 terminal is tty1; scripts from the Scripts menu run on
// tty2). 0 when unknown.
int active_console();

// Is the Linux framebuffer shown? (Main_MiSTer releases its input grab while it is.)
// Returns -1 when it cannot tell.
int framebuffer_shown();

struct Framebuffer {
    uint8_t *mem = nullptr; // first pixel
    int width = 0;
    int height = 0;
    int stride = 0; // bytes
};

// Shows the Linux framebuffer at a resolution picked for the current video output
// (about 960x540, integer-scaled by the FPGA) and puts tty1 in graphics mode.
bool show(Framebuffer &fb);

// Gives the screen back: tty1 in text mode, F12 (framebuffer off, menu shown).
void hide();

// Console back in text mode only (before loading a core, the screen goes to the core anyway).
void release_console();

// Loads the card's game (MGL or MRA, same as retrogc.sh). Returns "" or an error message.
std::string launch(const Card &card);

// Back to menu.rbf after a game (removes the temporary core link).
void unload();

} // namespace mister

# RetroGC Launcher for MiSTer FPGA (UI version)

The Retro Game Card launcher screen on a MiSTer: insert a card, see its cover and details,
and the game starts. It runs next to the stock `menu.rbf` and doesn't replace it. The stock
MiSTer menu is always one button away.

The script-only launcher (`retrogc.sh`, in `retrogc-launcher/Platforms/MisterFPGA`) is still
available for anyone who prefers no screen. Only one of the two can be ON at a time.

## How it works

`menu.rbf` shows a Linux framebuffer when Main_MiSTer is asked to (that's the F9 terminal).
`retrogc-mister` is a small native ARM program that:

- presses **F9** through a virtual keyboard to show the framebuffer, sets its resolution
  (`fb_cmd1`, about 960×540, scaled up by the FPGA), and draws the launcher with
  [LVGL](https://lvgl.io) into the framebuffer memory (`/dev/mem`, since the MiSTer_fb driver has
  no mmap);
- watches the `RETROGC` card like `retrogc.sh` does (keeps it mounted read-only) and loads the
  game through `/dev/MiSTer_cmd` (an MGL file, or the MRA for arcade games);
- reads Main_MiSTer's "MiSTer virtual input" (your pad, using your MiSTer button mapping) and
  real keyboards.

| On the launcher screen | |
|---|---|
| **A** / Enter | Play |
| **B** / Esc | Cancel the auto start |
| **X** / Space | Settings |
| D-pad up / down | Scroll the description |
| **OSD button** / F12 | Go to the stock MiSTer menu |

From the MiSTer menu, **F9** on a keyboard, *Scripts → retrogc_ui_show.sh*, or simply inserting
a card brings the launcher back. When the card is removed during a game, the MiSTer returns to the
menu core and the launcher shows again.

## Settings (X)

| Setting | |
|---|---|
| **Game auto start** | OFF (wait for A, the default), AT ONCE, or after 5 / 10 / 30 seconds |
| **At MiSTer boot** | SHOW LAUNCHER: starts and takes the screen. IN BACKGROUND: starts hidden, appears when a card is inserted. DISABLED: does not start (removes the boot hook) |
| **Scanlines** | CRT scanline overlay (HDMI only) |
| **Go to MiSTer menu** | Hands the screen back to the stock menu |

Saved in `/media/fat/retrogc/retrogc-mister.ini`.

## Video modes

The layout follows the 960×540 design of the PC launcher on HDMI. At 15 kHz (240p and less) it
switches to a compact layout with the 8 px pixel font only, and corrects for the CRT's non-square
pixels (512×224 shown 4:3) so the cover stays square.

## Install

Copy to the SD card:

| File | To |
|---|---|
| `retrogc-mister` (built binary) | `/media/fat/retrogc/retrogc-mister` |
| `scripts/retrogc_ui.sh` | `/media/fat/Scripts/retrogc_ui.sh` |
| `scripts/retrogc_ui_show.sh` | `/media/fat/Scripts/retrogc_ui_show.sh` |

Make them executable (`chmod a+x`), then run *Scripts → retrogc_ui.sh* to turn it ON (it starts on
every boot) or OFF. If `retrogc.sh` is ON, turn it OFF first.

Log: `/tmp/retrogc-mister.log`.

Requirements: `fb_terminal=1` in `MiSTer.ini` (the default), so F9 works.

## Build

Needs the ARM GNU toolchain 10.2-2020.11 `arm-none-linux-gnueabihf` (the one Main_MiSTer uses), by
default in `/opt/mister/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf`, plus CMake and Ninja.
LVGL v9.2.2 is fetched by CMake; stb and nlohmann/json are in `third_party/`.

```bash
# MiSTer binary (static, stripped)
cmake -B build-mister -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mister-armhf.cmake
cmake --build build-mister

# Desktop preview: the same UI in a window (needs libsdl2-dev).
# I = insert / remove the card, Enter = A, Esc = B, Q = quit
cmake -B build-host -G Ninja -DRETROGC_SDL=ON
cmake --build build-host
./build-host/retrogc-mister --preview --card /path/to/a/card/folder

# One frame to a PNG, for any size / state
./build-host/retrogc-mister --screenshot shot.png --size 1280x720 --card /path/to/card --state ready
```

## Fonts

Press Start 2P, VT323 and Inter (SIL Open Font License 1.1, see `assets/fonts/`), built into the
binary.

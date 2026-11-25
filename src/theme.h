// Palette of the "16-bit console menu" theme, kept in sync with the PC launcher
// (retrogc-launcher/Src/Styles/Theme.axaml and Src/ViewModels/Converters.cs).
#pragma once

#include <cctype>
#include <cstdint>
#include <string>

#include "lvgl.h"

namespace theme {

inline lv_color_t rgb(uint32_t hex) { return lv_color_hex(hex); }

constexpr uint32_t Bg = 0x120E24;
constexpr uint32_t Surface = 0x1B1633;
constexpr uint32_t SurfaceAlt = 0x241D42;
constexpr uint32_t SurfaceSunken = 0x0D0A1A;
constexpr uint32_t BorderSubtle = 0x2C2550;
constexpr uint32_t Border = 0x3A3163;
constexpr uint32_t BorderStrong = 0x544A8A;
constexpr uint32_t Shadow = 0x000000;

constexpr uint32_t Text = 0xF1EEFF;
constexpr uint32_t TextStrong = 0xFFFFFF;
constexpr uint32_t TextMuted = 0xC2BBE3;
constexpr uint32_t TextDim = 0x9790BD;
constexpr uint32_t TextFaint = 0x6B6494;

constexpr uint32_t Magenta = 0xFF3E8A;
constexpr uint32_t MagentaSoft = 0x3A1534;
constexpr uint32_t Cyan = 0x2EE6D6;
constexpr uint32_t CyanSoft = 0x113A45;
constexpr uint32_t Yellow = 0xFFD23F;
constexpr uint32_t YellowSoft = 0x3A3022;
constexpr uint32_t Orange = 0xFF8C42;
constexpr uint32_t OrangeSoft = 0x3B2228;
constexpr uint32_t Violet = 0x9B6CFF;
constexpr uint32_t VioletSoft = 0x2A1F55;
constexpr uint32_t Green = 0x6BE675;
constexpr uint32_t GreenSoft = 0x18352E;
constexpr uint32_t Red = 0xFF5A5F;
constexpr uint32_t RedSoft = 0x3D1726;

struct Accent {
    uint32_t accent;
    uint32_t soft;
};

// Same order and hash as Converters.Accents / AccentIndex, so a platform or tag gets the same
// colour on the PC and on the MiSTer.
inline Accent accent_for(const std::string &text)
{
    static const Accent accents[] = {
        { Magenta, MagentaSoft }, { Cyan, CyanSoft },     { Yellow, YellowSoft },
        { Orange, OrangeSoft },   { Violet, VioletSoft }, { Green, GreenSoft },
    };

    size_t b = text.find_first_not_of(" \t\r\n");
    size_t e = text.find_last_not_of(" \t\r\n");
    uint32_t hash = 2166136261u;
    if (b != std::string::npos) {
        for (size_t i = b; i <= e; i++) {
            // The PC hashes UTF-16 code units; identical for the ASCII names cards use.
            unsigned char c = (unsigned char)text[i];
            hash = (hash ^ (uint32_t)std::toupper(c)) * 16777619u;
        }
    }
    return accents[hash % 6];
}

} // namespace theme

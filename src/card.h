// A Retro Game Card definition: the .rgc JSON file at the root of a card.
// Same format as the PC launcher (retrogc-launcher/Src/Models/Card.cs); this side only needs
// the display fields and the "misterfpga" section.
#pragma once

#include <string>
#include <vector>

struct MisterConfig {
    std::string core;              // relative to the card root, e.g. "_Console/NES_20260603.rbf"
    std::vector<std::string> roms; // relative to the card root; roms[0] is loaded
    std::string delay = "1";       // MGL <file> attributes (ignored for .mra)
    std::string index = "1";
    std::string type = "f";
};

struct Card {
    std::string name;
    std::string description;
    std::string platform;
    std::string year;
    std::string image;
    std::string developer;
    std::string genre;
    std::string players;

    bool has_mister = false;
    MisterConfig mister;

    std::string definition_path; // absolute path of the .rgc file
    std::string base_dir;        // card root (directory of the .rgc file)

    // First *.rgc file at the root of |dir| (case-insensitive order, like the PC launcher), or "".
    static std::string find_definition(const std::string &dir);

    // Parses |path|. On failure returns false and sets |error| to a message for the screen.
    static bool load(const std::string &path, Card &out, std::string &error);

    // Absolute path of a card-relative path. A leading '/' is ignored (the MiSTer script strips
    // it too); a path that escapes the card ("..") is rejected.
    bool resolve(const std::string &relative, std::string &absolute, std::string &error) const;

    // Cover image path, or "" when the card has none.
    std::string image_path() const;

    // Short name of the MiSTer core for a badge: "_Console/NES_20260603.rbf" -> "NES".
    std::string mister_core_name() const;
};

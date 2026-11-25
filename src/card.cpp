#include "card.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <strings.h>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

// A string field, or "" when missing. Numbers are accepted too ("year": 1985, "delay": 2).
std::string get_str(const json &obj, const char *key)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    if (it->is_number()) {
        std::ostringstream s;
        s << it->get<double>();
        return s.str();
    }
    return {};
}

std::string file_name(const std::string &path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ends_with_ci(const std::string &s, const char *suffix)
{
    size_t n = strlen(suffix);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suffix) == 0;
}

} // namespace

std::string Card::find_definition(const std::string &dir)
{
    std::vector<std::string> found;
    if (DIR *d = opendir(dir.c_str())) {
        while (struct dirent *e = readdir(d)) {
            std::string name = e->d_name;
            if (name[0] != '.' && ends_with_ci(name, ".rgc")) found.push_back(name);
        }
        closedir(d);
    }
    if (found.empty()) return {};

    std::sort(found.begin(), found.end(),
              [](const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    return dir + (dir.back() == '/' ? "" : "/") + found[0];
}

bool Card::load(const std::string &path, Card &out, std::string &error)
{
    const std::string file = file_name(path);

    std::ifstream in(path);
    if (!in) {
        error = "Cannot read " + file + ".";
        return false;
    }

    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = file + " is not a valid card file (JSON).";
        return false;
    }

    Card c;
    c.definition_path = path;
    size_t slash = path.find_last_of('/');
    c.base_dir = slash == std::string::npos ? "." : path.substr(0, slash);

    c.name = get_str(j, "name");
    c.description = get_str(j, "description");
    c.platform = get_str(j, "platform");
    c.year = get_str(j, "year");
    c.image = get_str(j, "image");
    c.developer = get_str(j, "developer");
    c.genre = get_str(j, "genre");
    c.players = get_str(j, "players");

    if (c.name.empty()) {
        error = "Missing 'name' in " + file + ".";
        return false;
    }

    auto m = j.find("misterfpga");
    if (m != j.end() && m->is_object()) {
        c.mister.core = get_str(*m, "core");
        auto roms = m->find("roms");
        if (roms != m->end() && roms->is_array()) {
            for (const auto &r : *roms)
                if (r.is_string()) c.mister.roms.push_back(r.get<std::string>());
        }
        std::string v;
        if (!(v = get_str(*m, "delay")).empty()) c.mister.delay = v;
        if (!(v = get_str(*m, "index")).empty()) c.mister.index = v;
        if (!(v = get_str(*m, "type")).empty()) c.mister.type = v;
        c.has_mister = !c.mister.core.empty();
    }

    out = std::move(c);
    return true;
}

bool Card::resolve(const std::string &relative, std::string &absolute, std::string &error) const
{
    std::string rel = relative;
    while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);

    if (rel.empty()) {
        error = "An empty path is defined in '" + name + "'.";
        return false;
    }

    // Reject any ".." component: everything must stay on the card.
    std::stringstream parts(rel);
    std::string part;
    while (std::getline(parts, part, '/')) {
        if (part == "..") {
            error = "Path '" + relative + "' escapes the card directory.";
            return false;
        }
    }

    absolute = base_dir + "/" + rel;
    return true;
}

std::string Card::image_path() const
{
    std::string abs, err;
    if (image.empty() || !resolve(image, abs, err)) return {};
    return abs;
}

std::string Card::mister_core_name() const
{
    std::string n = file_name(mister.core);
    size_t dot = n.find_last_of('.');
    if (dot != std::string::npos) n.erase(dot);

    // Drop the "_YYYYMMDD" build date MiSTer cores carry.
    size_t us = n.find_last_of('_');
    if (us != std::string::npos && n.size() - us - 1 == 8 &&
        std::all_of(n.begin() + us + 1, n.end(), [](unsigned char ch) { return std::isdigit(ch); }))
        n.erase(us);

    for (auto &ch : n) ch = (char)std::toupper((unsigned char)ch);
    return n;
}

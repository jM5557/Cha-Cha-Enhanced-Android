// <data folder>/mods/mods.ini, the file of the DLC screen's Mods list (dlc_content.cpp: "key=1|0" for game/mods.h),
// also holds Cha-Cha-Enhanced's switches, "key=value" lines:
//   cheats=on|off                                     the cheats' master switch (the list itself is the cheat file)
//   texpack.<folder in texture_replacements>=on|off   texture packs (a pack not listed is on)
// Every writer keeps the keys it does not own. Before Cha-Cha 0.2 these were in <data folder>/mods.ini, which
// migrate_mods_ini() carries over once.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mh3u::boot {

using ModsIni = std::vector<std::pair<std::string, std::string>>;

inline std::filesystem::path mods_ini_path(const std::filesystem::path& data) { return data / "mods" / "mods.ini"; }

inline ModsIni read_mods_ini(const std::filesystem::path& file) {
    ModsIni out;
    std::ifstream f(file);
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.rfind('=');  // (a pack's folder name may hold one)
        if (eq != std::string::npos) out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    return out;
}

// The comment lines at the top of a file (kept by writers that don't own them)
inline std::vector<std::string> mods_ini_comments(const std::filesystem::path& file) {
    std::vector<std::string> out;
    std::ifstream f(file);
    for (std::string line; std::getline(f, line) && !line.empty() && line[0] == '#';) {
        if (line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// Replaces every key starting with `prefix` (an exact key when it has no trailing '.') by `entries`.
inline bool write_mods_ini(const std::filesystem::path& file, const std::string& prefix, const ModsIni& entries) {
    ModsIni all = read_mods_ini(file);
    std::vector<std::string> head = mods_ini_comments(file);
    const bool group = !prefix.empty() && prefix.back() == '.';
    std::erase_if(all, [&](const auto& kv) { return group ? kv.first.rfind(prefix, 0) == 0 : kv.first == prefix; });
    all.insert(all.end(), entries.begin(), entries.end());
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::trunc);
    if (head.empty()) head.push_back("# Cha-Cha mods (main menu > DLC > Mods): 1 on, 0 off");
    for (const std::string& h : head) f << h << '\n';
    for (const auto& [k, v] : all) f << k << '=' << v << '\n';
    return static_cast<bool>(f);
}

inline std::string mods_value(const ModsIni& ini, const std::string& key, const std::string& fallback) {
    for (const auto& [k, v] : ini)
        if (k == key) return v;
    return fallback;
}

// Cha-Cha-Enhanced 0.1's <data>/mods.ini: its cheats= and texpack.* lines move to <data>/mods/mods.ini (once: only
// while the new file has none of them). The old file stays.
inline void migrate_mods_ini(const std::filesystem::path& data) {
    const ModsIni old = read_mods_ini(data / "mods.ini");
    const std::filesystem::path now = mods_ini_path(data);
    for (const auto& [k, v] : read_mods_ini(now))
        if (k == "cheats" || k.rfind("texpack.", 0) == 0) return;
    ModsIni moved;
    for (const auto& [k, v] : old)
        if (k == "cheats" || k.rfind("texpack.", 0) == 0) moved.emplace_back(k, v);
    if (!moved.empty()) write_mods_ini(now, "\x01", moved);  // (a prefix no key has: only adds)
}

}  // namespace mh3u::boot

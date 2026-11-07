// <data folder>/mods.ini: what the MODS panel chose, "key=value" lines, in order.
//   pack.<folder in quests>=all|choose|off   quest pack modes (QUESTS)
//   cheats=on|off                            the master switch for cheats (CHEATS; the list itself is the cheat file)
//   texpack.<folder in texture_replacements>=on|off   texture packs (TEXTURES; a pack not listed is on)
// Every writer keeps the keys it does not own.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace mh3u::boot {

using ModsIni = std::vector<std::pair<std::string, std::string>>;

inline ModsIni read_mods_ini(const std::filesystem::path& file) {
    ModsIni out;
    std::ifstream f(file);
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.rfind('=');
        if (eq != std::string::npos) out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    return out;
}

// Replaces every key starting with `prefix` (an exact key when it has no trailing '.') by `entries`.
inline bool write_mods_ini(const std::filesystem::path& file, const std::string& prefix, const ModsIni& entries) {
    ModsIni all = read_mods_ini(file);
    const bool group = !prefix.empty() && prefix.back() == '.';
    std::erase_if(all, [&](const auto& kv) { return group ? kv.first.rfind(prefix, 0) == 0 : kv.first == prefix; });
    all.insert(all.end(), entries.begin(), entries.end());
    std::ofstream f(file, std::ios::trunc);
    f << "# Cha-Cha mods. pack.<folder in quests> = all | choose | off; cheats = on | off;\n"
         "# texpack.<folder in texture_replacements> = on | off\n";
    for (const auto& [k, v] : all) f << k << '=' << v << '\n';
    return static_cast<bool>(f);
}

inline std::string mods_value(const ModsIni& ini, const std::string& key, const std::string& fallback) {
    for (const auto& [k, v] : ini)
        if (k == key) return v;
    return fallback;
}

}  // namespace mh3u::boot

// Text for Cha-Cha-Enhanced's rows in the game's menus (enhanced_menu.cpp; menu_text_test): a cheat's label from its
// name, labels cut to a width, help text wrapped to the help box. Header-only, no game code, so it is tested alone.
#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mh3u::boot::menu_text {

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return s;
}

// "QOL - Name - MH3U USA - by jM5557" -> its parts
inline std::vector<std::string> split_name(const std::string& name) {
    std::vector<std::string> parts;
    for (size_t at = 0;;) {
        const size_t dash = name.find(" - ", at);
        parts.push_back(name.substr(at, dash == std::string::npos ? std::string::npos : dash - at));
        if (dash == std::string::npos) break;
        at = dash + 3;
    }
    return parts;
}
inline bool is_game_tag(const std::string& part) {  // "MH3U USA", "MH3U US", "MH3U": the game, said in every name
    const std::string l = lower(part);
    return l.rfind("mh3u", 0) == 0 && l.size() <= 9;
}
inline bool is_author(const std::string& part) { return lower(part).rfind("by ", 0) == 0; }
// a leading group such as QOL, CHEAT or EXPERIMENTAL: short, no lower-case letters, and not the whole name
inline bool is_group(const std::vector<std::string>& parts) {
    if (parts.size() < 2 || parts[0].empty() || parts[0].size() > 16) return false;
    return std::none_of(parts[0].begin(), parts[0].end(), [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; });
}

// The row label: the name without its group, game and author ("Skip Quest End Timer (Hold X+Y)")
inline std::string cheat_label(const std::string& name) {
    const std::vector<std::string> parts = split_name(name);
    const bool group = is_group(parts);
    std::string out;
    for (size_t i = group ? 1 : 0; i < parts.size(); ++i) {
        if (is_game_tag(parts[i]) || is_author(parts[i]) || parts[i].empty()) continue;
        out += (out.empty() ? "" : " - ") + parts[i];
    }
    return out.empty() ? name : out;
}

// The group ("QOL") and author ("jM5557") of a name, when it has them
inline std::pair<std::string, std::string> group_author(const std::string& name) {
    const std::vector<std::string> parts = split_name(name);
    std::string group = is_group(parts) ? parts[0] : std::string(), author;
    for (const std::string& p : parts)
        if (is_author(p)) author = p.substr(3);
    return {group, author};
}

// s in at most max bytes: cut (not inside a UTF-8 character) and "..." when longer
inline std::string shorten(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t cut = max > 3 ? max - 3 : 0;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    std::string out = s.substr(0, cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "...";
}

// text in lines of at most width (words kept whole unless one alone is longer), at most `lines` of them ("..." when
// some was left out); each line of text starts a new line, empty ones are dropped; '\n' between the lines
inline std::string wrap(const std::string& text, size_t width = 44, size_t lines = 3) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string para; std::getline(in, para);) {
        std::istringstream words(para);
        std::string line;
        for (std::string w; words >> w;) {
            if (w.size() > width) w = shorten(w, width);
            if (line.empty()) line = w;
            else if (line.size() + 1 + w.size() <= width) line += " " + w;
            else out.push_back(line), line = w;
        }
        if (!line.empty()) out.push_back(line);
    }
    if (out.size() > lines) {  // the last line shown ends in " ...", words dropped to make room
        out.resize(lines);
        std::string& last = out.back();
        while (last.size() + 4 > width && last.find(' ') != std::string::npos) last.erase(last.rfind(' '));
        last = last.size() + 4 <= width ? last + " ..." : shorten(last, width);
    }
    std::string joined;
    for (size_t i = 0; i < out.size(); ++i) joined += (i ? "\n" : "") + out[i];
    return joined;
}

}  // namespace mh3u::boot::menu_text

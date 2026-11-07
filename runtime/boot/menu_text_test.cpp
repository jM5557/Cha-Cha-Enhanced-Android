// menu_text.h (the Cheats / Texture Packs rows' text) and mods_ini.h (Cha-Cha-Enhanced's switches in mods/mods.ini).
#include "menu_text.h"
#include "mods_ini.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
int g_failed = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failed;
    }
}
void eq(const std::string& got, const std::string& want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s\n  got  \"%s\"\n  want \"%s\"\n", what, got.c_str(), want.c_str());
        ++g_failed;
    }
}
std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
}  // namespace

int main() {
    using namespace mh3u::boot;
    using namespace mh3u::boot::menu_text;

    // labels from the bundled file's names
    eq(cheat_label("QOL - Skip Quest End Timer (Hold X+Y) - MH3U USA - by jM5557"), "Skip Quest End Timer (Hold X+Y)", "label: group, game, author");
    eq(cheat_label("QOL - Hold A to Carve ONLY - DISABLE - MH3U USA - by jM5557"), "Hold A to Carve ONLY - DISABLE", "label: DISABLE kept");
    eq(cheat_label("EXPERIMENTAL - Frame cap 44 FPS - by jM5557"), "Frame cap 44 FPS", "label: no game tag");
    eq(cheat_label("Infinite Health - by someone"), "Infinite Health", "label: no group (lower case is a name)");
    eq(cheat_label("Moon Jump"), "Moon Jump", "label: plain name");
    eq(cheat_label("CHEAT - by x"), "CHEAT - by x", "label: nothing left -> the name");
    const auto [g, a] = group_author("CHEAT - Many Zenny (999,999z) - MH3U USA - by jM5557");
    eq(g, "CHEAT", "group");
    eq(a, "jM5557", "author");

    eq(shorten("Texture Packs", 18), "Texture Packs", "shorten: fits");
    eq(shorten("Hold A to Carve/Gather/Mine/Catch Bugs", 18), "Hold A to Carve...", "shorten: cut");
    check(shorten("Hold A to Carve/Gather/Mine/Catch Bugs", 18).size() <= 18, "shorten: width");
    eq(shorten("abcd \xC3\xA9\xC3\xA9\xC3\xA9", 9), "abcd...", "shorten: not inside UTF-8 (and no trailing space)");

    eq(wrap("one two three four", 9), "one two\nthree\nfour", "wrap: words");
    eq(wrap("line one\n\nline two", 44), "line one\nline two", "wrap: lines kept, empty dropped");
    const std::string w = wrap("aaaa bbbb cccc dddd eeee ffff gggg", 9, 2);
    eq(w, "aaaa bbbb\ncccc ...", "wrap: cut to two lines with ...");
    check(wrap(std::string(60, 'x'), 20, 3).size() == 20, "wrap: a long word cut to the width");

    // mods.ini: our keys next to the DLC screen's, each writer keeping the other's
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "cha-cha-menu-text-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "mods");
    {
        std::ofstream(mods_ini_path(dir)) << "# Cha-Cha mods (main menu > DLC > Mods): 1 on, 0 off\nquest_scaling=1\nauto_carve=0\n";
    }
    write_mods_ini(mods_ini_path(dir), "texpack.", {{"texpack.A=B", "off"}, {"texpack.HD", "off"}});
    write_mods_ini(mods_ini_path(dir), "cheats", {{"cheats", "off"}});
    write_mods_ini(mods_ini_path(dir), "texpack.", {{"texpack.HD", "off"}});
    const ModsIni ini = read_mods_ini(mods_ini_path(dir));
    eq(mods_value(ini, "quest_scaling", "?"), "1", "mods.ini: the DLC screen's keys kept");
    eq(mods_value(ini, "auto_carve", "?"), "0", "mods.ini: the DLC screen's keys kept (2)");
    eq(mods_value(ini, "cheats", "?"), "off", "mods.ini: cheats");
    eq(mods_value(ini, "texpack.HD", "?"), "off", "mods.ini: pack");
    eq(mods_value(ini, "texpack.A=B", "gone"), "gone", "mods.ini: a group replaced as a whole");
    check(slurp(mods_ini_path(dir)).rfind("# Cha-Cha mods (main menu > DLC > Mods)", 0) == 0, "mods.ini: its comment kept");

    // the 0.1 file: carried over once, and only while the new one has none of our keys
    const std::filesystem::path dir2 = dir / "old";
    std::filesystem::create_directories(dir2);
    {
        std::ofstream(dir2 / "mods.ini") << "# old\npack.DLC=all\ncheats=off\ntexpack.Old Pack=off\n";
    }
    migrate_mods_ini(dir2);
    const ModsIni moved = read_mods_ini(mods_ini_path(dir2));
    eq(mods_value(moved, "cheats", "?"), "off", "migrate: cheats");
    eq(mods_value(moved, "texpack.Old Pack", "?"), "off", "migrate: pack");
    eq(mods_value(moved, "pack.DLC", "not moved"), "not moved", "migrate: quest packs stay behind");
    write_mods_ini(mods_ini_path(dir2), "cheats", {{"cheats", "on"}});
    migrate_mods_ini(dir2);
    eq(mods_value(read_mods_ini(mods_ini_path(dir2)), "cheats", "?"), "on", "migrate: only once");
    std::filesystem::remove_all(dir);

    if (g_failed) return 1;
    std::printf("menu_text_test: ok\n");
    return 0;
}

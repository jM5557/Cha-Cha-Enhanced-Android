#include "enhanced_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <system_error>
#include <utility>

#include "gx2.h"
#include "mods.h"
#include "menu_text.h"
#include "mods_ini.h"
#include "texture_pack.h"

#if !defined(__ANDROID__)
#include <SDL3/SDL_misc.h>
#endif

namespace mh3u::boot {
namespace stdfs = std::filesystem;
namespace {
using menu_text::cheat_label;
using menu_text::group_author;
using menu_text::lower;
using menu_text::shorten;
using menu_text::wrap;

const std::vector<std::string> kOffOn = {"Off", "On"};
// rows that are not a cheat or a pack
constexpr int kMaster = -1, kDump = -2, kEmpty = -3, kOpenFolder = -4;
// label widths: the Game Options pages' label column, and the field menu's narrower one
constexpr size_t kLabelMax = 30, kShortMax = 18;

std::string utf8(const stdfs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool is_disable(const cheats::Cheat& c) { return lower(c.name).find("disable") != std::string::npos; }

#if !defined(__ANDROID__)
void open_folder(const stdfs::path& dir) {
    std::error_code ec;
    stdfs::create_directories(dir, ec);
    std::string url = "file:///" + utf8(dir);
    std::replace(url.begin(), url.end(), '\\', '/');
    if (!SDL_OpenURL(url.c_str())) std::fprintf(stderr, "menu: cannot open %s (%s)\n", url.c_str(), SDL_GetError());
}
#endif

}  // namespace

EnhancedMenu::EnhancedMenu(stdfs::path data, std::function<void()> on_mods_saved)
    : data_(std::move(data)), on_mods_saved_(std::move(on_mods_saved)) {
    for (const auto& [k, v] : read_mods_ini(mods_ini_path(data_)))
        if (k.rfind("texpack.", 0) == 0 && v == "off") packs_off_.insert(k.substr(8));
    packs_ = gfx::texpack::list_packs(texture_dir());  // what the renderer finds at start
}

// ---- Cheats ----------------------------------------------------------------------------------------------------

void EnhancedMenu::load_cheats_if_changed() {
    const stdfs::path file = cheat_file();
    std::error_code ec;
    if (!cheats_read_ && cheats::install_bundled(file)) std::printf("cheats: wrote the bundled cheat file to %s\n", utf8(file).c_str());
    const auto t = stdfs::last_write_time(file, ec);
    if (cheats_read_ && !ec && t == cheats_time_) return;
    cheats_ = cheats::load(file);
    cheats_time_ = ec ? stdfs::file_time_type{} : t;
    if (cheats_read_) cheats::engine().set_cheats(cheats_);  // edited outside: what the file says now runs
    cheats_read_ = true;
}

game::SettingsPage EnhancedMenu::cheats_page() {
    game::SettingsPage p;
    p.title = "Cheats";
    p.page_titles = {"Cheats"};
    p.rows_per_page = 7;
    p.rows = [this] { return cheat_rows(); };
    p.change = [this](int row, int value) { cheat_change(row, value); };
    p.activate = [this](int row) { cheat_activate(row); };
    return p;
}

std::vector<game::SettingRow> EnhancedMenu::cheat_rows() {
    std::lock_guard g(m_);
    load_cheats_if_changed();
    cheats::Engine& eng = cheats::engine();
    const auto problems = eng.problems();
    std::vector<game::SettingRow> rows;
    cheat_of_row_.clear();
    rows.push_back({"All Cheats",
                    "Every cheat on or off at once; which ones are\nticked stays. Off puts the game's own code back,\nso no DISABLE cheat is needed.",
                    kOffOn, eng.enabled() ? 1 : 0});
    cheat_of_row_.push_back(kMaster);
    for (size_t i = 0; i < cheats_.size(); ++i) {
        const cheats::Cheat& c = cheats_[i];
        if (is_disable(c) && !c.enabled && !shown_.count(c.name)) continue;  // not needed here (see All Cheats)
        shown_.insert(c.name);
        const std::string label = cheat_label(c.name);
        std::string help;
        if (auto it = problems.find(c.name); it != problems.end()) help = "Can't run: " + it->second + "\n";
        if (!c.notes.empty()) {
            help += c.notes;
        } else {
            const auto [group, author] = group_author(c.name);
            help += label + "." + (group.empty() ? "" : " " + group + " cheat") + (author.empty() ? "" : " by " + author) + ".";
        }
        game::SettingRow r{shorten(label, kLabelMax), wrap(help), kOffOn, c.enabled ? 1 : 0};
        if (label.size() > kShortMax) r.short_label = shorten(label, kShortMax);
        rows.push_back(std::move(r));
        cheat_of_row_.push_back(static_cast<int>(i));
    }
    if (rows.size() == 1) {
        rows.push_back({"No cheats in the file", wrap("Cheats in Cemu-Enhanced's format go in\n" + utf8(cheat_file())), {""}, 0, true});
        cheat_of_row_.push_back(kEmpty);
    }
#if !defined(__ANDROID__)
    rows.push_back({"Open Cheat Folder", wrap("The cheat file: " + utf8(cheat_file()), 44, 2) + "\nEdit it with any text editor; changes show here.",
                    {"Open"}, 0, true});
    cheat_of_row_.push_back(kOpenFolder);
#endif
    return rows;
}

void EnhancedMenu::cheat_change(int row, int value) {
    std::lock_guard g(m_);
    if (row < 0 || row >= static_cast<int>(cheat_of_row_.size())) return;
    const int which = cheat_of_row_[static_cast<size_t>(row)];
    cheats::Engine& eng = cheats::engine();
    if (which == kMaster) {
        eng.set_enabled(value != 0);
        write_mods_ini(mods_ini_path(data_), "cheats", {{"cheats", value ? "on" : "off"}});
        std::printf("cheats: %s\n", value ? "on" : "off");
        return;
    }
    if (which < 0 || which >= static_cast<int>(cheats_.size())) return;
    cheats::Cheat& c = cheats_[static_cast<size_t>(which)];
    c.enabled = value != 0;
    std::string err;
    if (!cheats::save(cheat_file(), cheats_, err)) std::fprintf(stderr, "cheats: cannot save the cheat file: %s\n", err.c_str());
    std::error_code ec;
    cheats_time_ = stdfs::last_write_time(cheat_file(), ec);
    eng.set_cheats(cheats_);
    std::printf("cheats: %s %s\n", c.name.c_str(), c.enabled ? "on" : "off");
}

void EnhancedMenu::cheat_activate(int row) {
    std::lock_guard g(m_);
    if (row < 0 || row >= static_cast<int>(cheat_of_row_.size())) return;
#if !defined(__ANDROID__)
    if (cheat_of_row_[static_cast<size_t>(row)] == kOpenFolder) open_folder(cheat_file().parent_path());
#endif
}

// ---- Texture Packs ---------------------------------------------------------------------------------------------

game::SettingsPage EnhancedMenu::textures_page() {
    game::SettingsPage p;
    p.title = "Texture Packs";
    p.page_titles = {"Texture Packs"};
    p.rows_per_page = 7;
    p.rows = [this] { return texture_rows(); };
    p.change = [this](int row, int value) { texture_change(row, value); };
    p.activate = [this](int row) { texture_activate(row); };
    return p;
}

std::vector<game::SettingRow> EnhancedMenu::texture_rows() {
    std::lock_guard g(m_);
    std::error_code ec;
    stdfs::create_directories(texture_dir(), ec);
    const std::vector<std::string> before = std::exchange(packs_, gfx::texpack::list_packs(texture_dir()));
    if (before != packs_)  // packs came or went since: the renderer looks through the folder again
        gfx::set_texture_pack_selection(std::vector<std::string>(packs_off_.begin(), packs_off_.end()));
    std::vector<game::SettingRow> rows;
    pack_of_row_.clear();
    rows.push_back({"Texture Packs",
                    "Replacement textures as a whole (the Texture\nPacks mod). The packs below choose which\nones are used.",
                    kOffOn, game::mod_enabled(game::Mod::TexturePacks) ? 1 : 0});
    pack_of_row_.push_back(kMaster);
    const uint64_t dumped = gfx::texture_dumps();
    rows.push_back({"Dump What You See",
                    "Each texture drawn from now on goes to\ntexture_dump, named as Cemu-Enhanced names\nthem. This session only" +
                        (dumped ? " (" + std::to_string(dumped) + " so far)." : std::string(".")),
                    kOffOn, dumping_ ? 1 : 0});
    rows.back().short_label = "Dump Textures";
    pack_of_row_.push_back(kDump);
    for (size_t i = 0; i < packs_.size(); ++i) {
        const std::string& name = packs_[i];
        auto it = pack_help_.find(name);
        if (it == pack_help_.end()) {  // its DDS files, counted once
            size_t files = 0;
            const stdfs::path dir = texture_dir() / stdfs::path(std::u8string(name.begin(), name.end()));
            for (auto f = stdfs::recursive_directory_iterator(dir, stdfs::directory_options::skip_permission_denied, ec);
                 !ec && f != stdfs::recursive_directory_iterator(); f.increment(ec))
                files += lower(f->path().extension().string()) == ".dds" ? 1 : 0;
            const bool ini = stdfs::exists(dir / "textures.ini", ec);
            it = pack_help_
                     .emplace(name, wrap(name + ": " + std::to_string(files) + " DDS file" + (files == 1 ? "" : "s") +
                                         (ini ? ", a textures.ini" : "") + ".\nWhere two packs replace the same texture, the one listed first wins."))
                     .first;
        }
        game::SettingRow r{shorten(name, kLabelMax), it->second, kOffOn, packs_off_.count(name) ? 0 : 1};
        if (name.size() > kShortMax) r.short_label = shorten(name, kShortMax);
        rows.push_back(std::move(r));
        pack_of_row_.push_back(static_cast<int>(i));
    }
    if (packs_.empty()) {
        rows.push_back({"No packs yet",
                        wrap("Put pack folders in " + utf8(texture_dir()) + ". Cemu-Enhanced packs work as they are."), {""}, 0, true});
        pack_of_row_.push_back(kEmpty);
    }
#if !defined(__ANDROID__)
    rows.push_back({"Open Texture Folder", wrap("Each folder in " + utf8(texture_dir()) + " is a pack; new ones show here."), {"Open"}, 0, true});
    pack_of_row_.push_back(kOpenFolder);
#endif
    return rows;
}

void EnhancedMenu::texture_change(int row, int value) {
    std::lock_guard g(m_);
    if (row < 0 || row >= static_cast<int>(pack_of_row_.size())) return;
    const int which = pack_of_row_[static_cast<size_t>(row)];
    if (which == kMaster) {
        game::set_mod_enabled(game::Mod::TexturePacks, value != 0);  // its listener tells the renderer
        if (on_mods_saved_) on_mods_saved_();
        return;
    }
    if (which == kDump) {
        dumping_ = value != 0;
        gfx::set_texture_dump(dumping_);
        std::printf("texture dump (what is drawn): %s\n", dumping_ ? "on" : "off");
        return;
    }
    if (which < 0 || which >= static_cast<int>(packs_.size())) return;
    const std::string& name = packs_[static_cast<size_t>(which)];
    if (value) packs_off_.erase(name);
    else packs_off_.insert(name);
    ModsIni lines;
    for (const std::string& n : packs_off_) lines.emplace_back("texpack." + n, "off");
    write_mods_ini(mods_ini_path(data_), "texpack.", lines);
    gfx::set_texture_pack_selection(std::vector<std::string>(packs_off_.begin(), packs_off_.end()));
    std::printf("texture packs: %s %s\n", name.c_str(), value ? "on" : "off");
}

void EnhancedMenu::texture_activate(int row) {
    std::lock_guard g(m_);
    if (row < 0 || row >= static_cast<int>(pack_of_row_.size())) return;
#if !defined(__ANDROID__)
    if (pack_of_row_[static_cast<size_t>(row)] == kOpenFolder) open_folder(texture_dir());
#endif
}

}  // namespace mh3u::boot

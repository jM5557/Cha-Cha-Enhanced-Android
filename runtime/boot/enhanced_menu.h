// Cha-Cha-Enhanced's pages in the game's own menus (enhanced_menu.cpp): Cheats and Texture Packs, shown in the
// title's Game Options (right column, game_options.cpp) and in the field menu's CFG tab (field_gfx.cpp).
//   Cheats         a master switch, then every cheat of <data>/cheats/<title id>.txt (Cemu-Enhanced's format; the
//                  bundled file on the first start). A switch saves the file; the cheat runs from the next frame.
//   Texture Packs  the Texture Packs mod (game/mods.h), dumping what is drawn (Cemu-Enhanced's names), then every
//                  pack in <data>/texture_replacements, each on or off (mods.ini texpack.<name>=off).
// The rows are read when a page opens: a cheat file edited or a pack added while the game runs shows the next time.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "cheats.h"
#include "settings_page.h"

namespace mh3u::boot {

class EnhancedMenu {
public:
    // on_mods_saved: the Texture Packs mod was switched here (save mods.ini as the DLC screen does).
    EnhancedMenu(std::filesystem::path data, std::function<void()> on_mods_saved);
    game::SettingsPage cheats_page();
    game::SettingsPage textures_page();


private:
    std::vector<game::SettingRow> cheat_rows();
    void cheat_change(int row, int value);
    void cheat_activate(int row);
    std::vector<game::SettingRow> texture_rows();
    void texture_change(int row, int value);
    void texture_activate(int row);
    void load_cheats_if_changed();
    std::filesystem::path cheat_file() const { return cheats::file_for(data_ / "cheats"); }
    std::filesystem::path texture_dir() const { return data_ / "texture_replacements"; }

    std::filesystem::path data_;
    std::function<void()> on_mods_saved_;
    std::mutex m_;
    // cheats: the file's list, when it was read, the rows' cheats (index into cheats_; -1 for the other rows), and
    // the cheats shown so far (a DISABLE cheat is listed while it is ticked, and stays listed once shown)
    std::vector<cheats::Cheat> cheats_;
    std::filesystem::file_time_type cheats_time_{};
    bool cheats_read_ = false;
    std::vector<int> cheat_of_row_;
    std::set<std::string> shown_;
    // texture packs: the packs listed, those off, DDS counts (once per pack), the live dump
    std::vector<std::string> packs_;
    std::vector<int> pack_of_row_;  // index into packs_; -1 for the other rows
    std::set<std::string> packs_off_;
    std::map<std::string, std::string> pack_help_;
    bool dumping_ = false;
};

}  // namespace mh3u::boot

// The field menu's CFG tab (field_gfx.cpp): Cha-Cha's settings (Display, Graphics, Camera, Mods) after the game's last tab.
#pragma once

#include "settings_page.h"

#include <string>
#include <vector>

namespace mh3u::game {

// A row of the CFG tab after Cha-Cha's four, with a settings page of its own (Cha-Cha-Enhanced: Cheats, Texture Packs).
struct FieldPage {
    std::string label, help;  // the row; help: two short lines (\n between them)
    SettingsPage page;        // its rows (read when it opens and after each change), change(), activate()
};

// The settings shown: the Graphics Settings page's rows (label, values, help), changed through its change(); then up
// to two extra rows.
void register_field_gfx(SettingsPage page, std::vector<FieldPage> extra = {});

}  // namespace mh3u::game

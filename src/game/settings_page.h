// A settings page of Cha-Cha's in the game's own Options UI (settings_page.cpp): rows of label + value, drawn with
// the Game Settings page's layout. Opened from Game Options (game_options.cpp).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ppc_context.h"

namespace mh3u::game {

struct SettingRow {
    std::string label, help;          // help: up to three short lines (\n between them)
    std::vector<std::string> values;  // left / right cycle through them; an action row shows values[0]
    int value = 0;
    bool action = false;              // A (or a click) on it calls SettingsPage::activate instead of leaving the page
    std::string short_label;          // for the field menu's narrower rows (field_gfx.cpp), when label is too long
};
struct SettingsPage {
    std::string title;
    std::vector<int> page_starts;                      // first row of each page (rows go on to the next page)
    std::vector<std::string> page_titles;              // per page, after the title ("Controller Settings: Keyboard")
    std::function<std::vector<SettingRow>()> rows;     // read when the page opens and after each change
    std::function<void(int row, int value)> change;    // a row's value changed (guest thread; applied at once)
    std::function<void(int row)> activate;             // A on an action row (guest thread)
    bool live = false;                                 // rows() read again every frame (values change by themselves)
    // > 0: page_starts are ignored and the rows go over pages of this many, however many rows() gives (lists such as
    // the cheats); every page is titled page_titles[0] if there is one
    int rows_per_page = 0;
};

namespace settings_page {

// Hooks the Game Settings page (once).
void install();
// Shows page instead of the game's Game Settings page from the next time that page opens (start menu state 20);
// until it closes.
void open(ppc::Context& ctx, uint8_t* base, const SettingsPage* page);
bool active();
// The list step (start menu state 8) runs again: the page has closed.
void closed();

}  // namespace settings_page
}  // namespace mh3u::game

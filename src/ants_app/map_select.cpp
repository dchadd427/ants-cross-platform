#include "ants_app/map_select.hpp"
#include <iostream>
#include <algorithm>
#include <fstream>

namespace ants::app {

namespace {

constexpr assets::ColorRGBA TEAM_COLORS[4] = {
    {79, 87, 111, 255},   // 0: Black
    {119, 175, 239, 255}, // 1: Blue
    {251, 51, 91, 255},   // 2: Red
    {83, 147, 43, 255}    // 3: Green
};

} // anonymous namespace

MapSelectScreen::MapSelectScreen() = default;

void MapSelectScreen::init(const std::string& maps_dir) {
    maps_.clear();

    // Standard authentic Ants maps in canonical order
    struct DefaultMap {
        const char* file;
        const char* title;
        const char* desc;
        uint32_t w;
        uint32_t h;
    };

    static const DefaultMap defaults[] = {
        { "TREASURE.LVL", "Treasure Island",       "One person's trash...",           60, 60 },
        { "SMALL.LVL",    "Small Arena",          "Small map for fast game",         40, 40 },
        { "MEDIUM.LVL",   "Medium Battleground",  "Intermediate map",                60, 60 },
        { "TINY.LVL",     "Tiny Duel",            "Tiny map with no PowerUps",       31, 31 },
        { "ISLANDS.LVL",  "Archipelago",          "Island hopping, expert map",      60, 60 },
        { "GAUNTLET.LVL", "The Gauntlet",         "Race for your life!",             60, 60 }
    };

    for (const auto& d : defaults) {
        MapSelectEntry entry;
        entry.filename = d.file;
        entry.full_path = maps_dir + "/" + d.file;
        entry.display_name = d.title;
        entry.description = d.desc;
        entry.width = d.w;
        entry.height = d.h;
        entry.anthills_count = (entry.width <= 31) ? 2 : 4;
        maps_.push_back(std::move(entry));
    }

    selected_index_ = 0;
    connection_ticks_ = 0;
    btn_start_hovered_ = false;
    btn_quit_hovered_ = false;
    btn_up_hovered_ = false;
    btn_down_hovered_ = false;
    btn_start_pressed_ = false;
    btn_quit_pressed_ = false;
    btn_up_pressed_ = false;
    btn_down_pressed_ = false;
}

void MapSelectScreen::set_selected_index(int32_t idx) noexcept {
    if (maps_.empty()) return;
    int32_t count = static_cast<int32_t>(maps_.size());
    selected_index_ = ((idx % count) + count) % count;
}

const std::string& MapSelectScreen::get_selected_map_path() const {
    static const std::string empty_path = "";
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        return maps_[static_cast<size_t>(selected_index_)].full_path;
    }
    return empty_path;
}

void MapSelectScreen::trigger_start() {
    if (on_start_ && !maps_.empty() && selected_index_ >= 0 &&
        selected_index_ < static_cast<int32_t>(maps_.size())) {
        on_start_(maps_[static_cast<size_t>(selected_index_)].full_path);
    }
}

void MapSelectScreen::trigger_quit() {
    if (on_quit_) {
        on_quit_();
    }
}

void MapSelectScreen::handle_mouse_motion(int32_t screen_x, int32_t screen_y) {
    mouse_x_ = screen_x;
    mouse_y_ = screen_y;

    btn_up_hovered_ = (screen_x >= BTN_UP_X && screen_x < BTN_UP_X + BTN_UP_W &&
                       screen_y >= BTN_UP_Y && screen_y < BTN_UP_Y + BTN_UP_H);

    btn_down_hovered_ = (screen_x >= BTN_DOWN_X && screen_x < BTN_DOWN_X + BTN_DOWN_W &&
                         screen_y >= BTN_DOWN_Y && screen_y < BTN_DOWN_Y + BTN_DOWN_H);

    btn_start_hovered_ = (screen_x >= BTN_START_X && screen_x < BTN_START_X + BTN_START_W &&
                          screen_y >= BTN_START_Y && screen_y < BTN_START_Y + BTN_START_H);

    btn_quit_hovered_ = (screen_x >= BTN_QUIT_X && screen_x < BTN_QUIT_X + BTN_QUIT_W &&
                         screen_y >= BTN_QUIT_Y && screen_y < BTN_QUIT_Y + BTN_QUIT_H);
}

void MapSelectScreen::handle_mouse_down(int32_t screen_x, int32_t screen_y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return;

    // Up Arrow Button
    if (screen_x >= BTN_UP_X && screen_x < BTN_UP_X + BTN_UP_W &&
        screen_y >= BTN_UP_Y && screen_y < BTN_UP_Y + BTN_UP_H) {
        btn_up_pressed_ = true;
        set_selected_index(selected_index_ - 1);
        return;
    }

    // Down Arrow Button
    if (screen_x >= BTN_DOWN_X && screen_x < BTN_DOWN_X + BTN_DOWN_W &&
        screen_y >= BTN_DOWN_Y && screen_y < BTN_DOWN_Y + BTN_DOWN_H) {
        btn_down_pressed_ = true;
        set_selected_index(selected_index_ + 1);
        return;
    }

    // Check Map Card / Slot clicks
    for (size_t i = 0; i < maps_.size(); ++i) {
        int32_t cy = CARD_Y + static_cast<int32_t>(i) * (CARD_H + CARD_SPACING);
        if (screen_x >= CARD_X && screen_x <= CARD_X + CARD_W &&
            screen_y >= cy && screen_y <= cy + CARD_H) {
            set_selected_index(static_cast<int32_t>(i));
            return;
        }
    }

    // Clicking on w_map box directly advances map
    if (screen_x >= 45 && screen_x < 240 && screen_y >= 86 && screen_y < 125) {
        set_selected_index(selected_index_ + 1);
        return;
    }

    // Start Button
    if (screen_x >= BTN_START_X && screen_x < BTN_START_X + BTN_START_W &&
        screen_y >= BTN_START_Y && screen_y < BTN_START_Y + BTN_START_H) {
        btn_start_pressed_ = true;
        trigger_start();
        return;
    }

    // Leave Game Button
    if (screen_x >= BTN_QUIT_X && screen_x < BTN_QUIT_X + BTN_QUIT_W &&
        screen_y >= BTN_QUIT_Y && screen_y < BTN_QUIT_Y + BTN_QUIT_H) {
        btn_quit_pressed_ = true;
        trigger_quit();
        return;
    }
}

void MapSelectScreen::handle_mouse_up(int32_t, int32_t, uint8_t button) {
    if (button == SDL_BUTTON_LEFT) {
        btn_up_pressed_ = false;
        btn_down_pressed_ = false;
        btn_start_pressed_ = false;
        btn_quit_pressed_ = false;
    }
}

void MapSelectScreen::handle_key_down(SDL_Keycode key) {
    if (maps_.empty()) return;

    if (key == SDLK_UP || key == SDLK_LEFT) {
        set_selected_index(selected_index_ - 1);
    } else if (key == SDLK_DOWN || key == SDLK_RIGHT) {
        set_selected_index(selected_index_ + 1);
    } else if (key >= SDLK_1 && key <= SDLK_6) {
        set_selected_index(key - SDLK_1);
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
        trigger_start();
    } else if (key == SDLK_ESCAPE) {
        trigger_quit();
    }
}

void MapSelectScreen::render(IRenderer& renderer, const ants::assets::AssetArchive&) {
    using ants::assets::ColorRGBA;

    connection_ticks_++;

    // 1. Classic Windows 95 Setup Dialog Background (Authentic CHD palette index 173)
    renderer.fill_rect(0, 0, 640, 480, ColorRGBA{219, 75, 19, 255});

    // Outer subtle 3D border
    renderer.draw_rect(0, 0, 640, 480, ColorRGBA{160, 50, 10, 255});
    renderer.draw_rect(2, 2, 636, 476, ColorRGBA{240, 125, 60, 255});
    renderer.draw_rect(4, 4, 632, 472, ColorRGBA{180, 55, 12, 255});

    // 2. Authentic Header Banner: hostbanr.bmp at (149, 12)
    renderer.draw_named_sprite("hostbanr.bmp", BANNER_X, BANNER_Y);

    // 3. Left Column: Map Selection
    // Header: pickmap.bmp at (45, 60)
    renderer.draw_named_sprite("pickmap.bmp", 45, 60);

    // Map Name Box: w_map.bmp at (45, 86)
    renderer.draw_named_sprite("w_map.bmp", 45, 86);

    // Current Map Name inside w_map.bmp
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        renderer.draw_text(cur.display_name, 56, 99, ColorRGBA{255, 255, 255, 255});
    }

    // Up/Down Arrow Buttons next to w_map.bmp
    const char* up_spr = btn_up_pressed_ ? "up3.bmp" : (btn_up_hovered_ ? "up2.bmp" : "up1.bmp");
    const char* dn_spr = btn_down_pressed_ ? "down3.bmp" : (btn_down_hovered_ ? "down2.bmp" : "down1.bmp");
    renderer.draw_named_sprite(up_spr, BTN_UP_X, BTN_UP_Y);
    renderer.draw_named_sprite(dn_spr, BTN_DOWN_X, BTN_DOWN_Y);

    // Map Info: mapinfo.bmp at (45, 140)
    renderer.draw_named_sprite("mapinfo.bmp", 45, 140);

    // Map Details Box (Sunken black bevel box matching w_map style)
    renderer.fill_rect(45, 168, 246, 95, ColorRGBA{0, 0, 0, 255});
    renderer.draw_rect(45, 168, 246, 95, ColorRGBA{35, 71, 47, 255});
    renderer.draw_rect(46, 169, 244, 93, ColorRGBA{11, 27, 19, 255});

    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        renderer.draw_text(cur.filename, 56, 178, ColorRGBA{255, 220, 90, 255});
        std::string dim_str = "Size: " + std::to_string(cur.width) + " x " + std::to_string(cur.height) + " Grid";
        renderer.draw_text(dim_str, 56, 198, ColorRGBA{200, 225, 245, 255});
        std::string spawn_str = "Spawns: " + std::to_string(cur.anthills_count) + " Anthills";
        renderer.draw_text(spawn_str, 56, 218, ColorRGBA{200, 225, 245, 255});
        renderer.draw_text(cur.description, 56, 238, ColorRGBA{140, 215, 160, 255});
    }

    // Fog of War: fowar.bmp at (45, 280) and fowno.bmp at (195, 276)
    renderer.draw_named_sprite("fowar.bmp", 45, 280);
    renderer.draw_named_sprite("fowno.bmp", 195, 276);

    // 4. Right Column: Player Roster & Status
    // Header: playstat.bmp at (360, 60)
    renderer.draw_named_sprite("playstat.bmp", 360, 60);

    // Roster panel box (Sunken black bevel box matching w_map style)
    renderer.fill_rect(360, 90, 235, 173, ColorRGBA{0, 0, 0, 255});
    renderer.draw_rect(360, 90, 235, 173, ColorRGBA{35, 71, 47, 255});
    renderer.draw_rect(361, 91, 233, 171, ColorRGBA{11, 27, 19, 255});

    // 4 Player Slots
    struct PlayerSlot {
        const char* name;
        const char* role;
        uint8_t team;
    };
    static const PlayerSlot slots[4] = {
        { "Black Ant", "Host (Player 1)", 0 },
        { "Blue Ant",  "Computer (AI)",   1 },
        { "Red Ant",   "Computer (AI)",   2 },
        { "Green Ant", "Computer (AI)",   3 }
    };

    for (size_t i = 0; i < 4; ++i) {
        int32_t sy = 98 + static_cast<int32_t>(i) * 40;
        // Team color marker
        renderer.fill_rect(372, sy + 2, 10, 10, TEAM_COLORS[slots[i].team]);
        renderer.draw_rect(372, sy + 2, 10, 10, ColorRGBA{255, 255, 255, 200});

        // Line 1: Name & READY
        renderer.draw_text(slots[i].name, 390, sy, ColorRGBA{240, 240, 250, 255});
        renderer.draw_text("READY", 535, sy, ColorRGBA{90, 230, 130, 255});

        // Line 2: Role (indented below name)
        renderer.draw_text(slots[i].role, 390, sy + 15, ColorRGBA{160, 185, 215, 255});
    }

    // 5. Center Status Bar: statline.bmp at (164, 335)
    renderer.draw_named_sprite("statline.bmp", 164, 335);

    // Connection progression text
    std::string stat_msg;
    if (connection_ticks_ < 30) {
        stat_msg = "Finding game...";
    } else if (connection_ticks_ < 70) {
        stat_msg = "Network communication initialized.";
    } else {
        stat_msg = "All players ready. Press START to launch!";
    }
    renderer.draw_text(stat_msg, 182, 348, ColorRGBA{255, 240, 150, 255});

    // 6. Action Buttons: START! & Leave Game
    const char* start_spr = btn_start_pressed_ ? "bstart3.bmp" : (btn_start_hovered_ ? "bstart2.bmp" : "bstart1.bmp");
    const char* leave_spr = btn_quit_pressed_ ? "bleave3.bmp" : (btn_quit_hovered_ ? "bleave2.bmp" : "bleave1.bmp");
    renderer.draw_named_sprite(start_spr, BTN_START_X, BTN_START_Y);
    renderer.draw_named_sprite(leave_spr, BTN_QUIT_X, BTN_QUIT_Y);

    // 7. Footer Instructions
    renderer.draw_text("[UP / DOWN / CLICK] Select Map      [ENTER / START] Launch Match      [ESC / LEAVE] Quit",
                       50, 452, ColorRGBA{255, 255, 255, 230});
}

} // namespace ants::app

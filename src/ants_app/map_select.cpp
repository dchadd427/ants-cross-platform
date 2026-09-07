#include "ants_app/map_select.hpp"
#include <iostream>
#include <algorithm>

namespace ants::app {

namespace {

void draw_sunken_box(IRenderer& renderer, int32_t x, int32_t y, int32_t w, int32_t h) {
    using ants::assets::ColorRGBA;
    // Black interior fill
    renderer.fill_rect(x, y, w, h, ColorRGBA{0, 0, 0, 255});
    // Outer frame outline
    renderer.draw_rect(x, y, w, h, ColorRGBA{23, 71, 47, 255});
    // Top & left inner bevel shadow
    renderer.fill_rect(x + 1, y + 1, w - 2, 1, ColorRGBA{11, 27, 19, 255});
    renderer.fill_rect(x + 1, y + 1, 1, h - 2, ColorRGBA{11, 27, 19, 255});
    // Bottom & right inner bevel highlight
    renderer.fill_rect(x + 1, y + h - 2, w - 2, 1, ColorRGBA{59, 151, 111, 255});
    renderer.fill_rect(x + w - 2, y + 1, 1, h - 2, ColorRGBA{59, 151, 111, 255});
}

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
        uint32_t minutes;
    };

    static const DefaultMap defaults[] = {
        { "TREASURE.LVL", "TREASURE", "One person's trash...",           60, 60, 12 },
        { "SMALL.LVL",    "SMALL",    "Small map for fast game",         40, 40,  5 },
        { "MEDIUM.LVL",   "MEDIUM",   "Intermediate map",                60, 60, 10 },
        { "TINY.LVL",     "TINY",     "Tiny map with no PowerUps",       31, 31,  3 },
        { "ISLANDS.LVL",  "ISLANDS",  "Island hopping, expert map",      60, 60, 20 },
        { "GAUNTLET.LVL", "GAUNTLET", "Race for your life!",             60, 60, 15 }
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
        entry.minutes = d.minutes;
        maps_.push_back(std::move(entry));
    }

    selected_index_ = 0;
    connection_ticks_ = 0;
    fog_of_war_ = true;
    player_ready_mask_ = 0b0011; // Player 0 & 1 ready, Player 2 unready matching reference screenshot

    btn_start_hovered_ = false;
    btn_quit_hovered_ = false;
    btn_up_hovered_ = false;
    btn_down_hovered_ = false;
    btn_drop_hovered_ = false;
    btn_fow_on_hovered_ = false;
    btn_fow_off_hovered_ = false;

    btn_start_pressed_ = false;
    btn_quit_pressed_ = false;
    btn_up_pressed_ = false;
    btn_down_pressed_ = false;
    btn_drop_pressed_ = false;
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

    btn_drop_hovered_ = (screen_x >= BTN_DROP_X && screen_x < BTN_DROP_X + BTN_DROP_W &&
                         screen_y >= BTN_DROP_Y && screen_y < BTN_DROP_Y + BTN_DROP_H);

    btn_fow_on_hovered_ = (screen_x >= BTN_FOW_ON_X && screen_x < BTN_FOW_ON_X + BTN_FOW_ON_W &&
                           screen_y >= BTN_FOW_ON_Y && screen_y < BTN_FOW_ON_Y + BTN_FOW_ON_H);

    btn_fow_off_hovered_ = (screen_x >= BTN_FOW_OFF_X && screen_x < BTN_FOW_OFF_X + BTN_FOW_OFF_W &&
                            screen_y >= BTN_FOW_OFF_Y && screen_y < BTN_FOW_OFF_Y + BTN_FOW_OFF_H);
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

    // Clicking w_map box advances map
    if (screen_x >= W_MAP_X && screen_x < W_MAP_X + W_MAP_W &&
        screen_y >= W_MAP_Y && screen_y < W_MAP_Y + W_MAP_H) {
        set_selected_index(selected_index_ + 1);
        return;
    }

    // Clicking map info box also advances map
    if (screen_x >= INFO_BOX_X && screen_x < INFO_BOX_X + INFO_BOX_W &&
        screen_y >= INFO_BOX_Y && screen_y < INFO_BOX_Y + INFO_BOX_H) {
        set_selected_index(selected_index_ + 1);
        return;
    }

    // Fog of War "On" button
    if (screen_x >= BTN_FOW_ON_X && screen_x < BTN_FOW_ON_X + BTN_FOW_ON_W &&
        screen_y >= BTN_FOW_ON_Y && screen_y < BTN_FOW_ON_Y + BTN_FOW_ON_H) {
        set_fog_of_war_enabled(true);
        return;
    }

    // Fog of War "Off" button
    if (screen_x >= BTN_FOW_OFF_X && screen_x < BTN_FOW_OFF_X + BTN_FOW_OFF_W &&
        screen_y >= BTN_FOW_OFF_Y && screen_y < BTN_FOW_OFF_Y + BTN_FOW_OFF_H) {
        set_fog_of_war_enabled(false);
        return;
    }

    // Drop Button: toggles Player 2 ready state
    if (screen_x >= BTN_DROP_X && screen_x < BTN_DROP_X + BTN_DROP_W &&
        screen_y >= BTN_DROP_Y && screen_y < BTN_DROP_Y + BTN_DROP_H) {
        btn_drop_pressed_ = true;
        toggle_player_ready(2);
        return;
    }

    // Click on player thumbs inside players box (x in [535, 565])
    if (screen_x >= 535 && screen_x < 565) {
        for (uint8_t i = 0; i < 3; ++i) {
            int32_t ty = 97 + static_cast<int32_t>(i) * 50;
            if (screen_y >= ty && screen_y < ty + 24) {
                toggle_player_ready(i);
                return;
            }
        }
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
        btn_drop_pressed_ = false;
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
    } else if (key == SDLK_f) {
        set_fog_of_war_enabled(!fog_of_war_);
    } else if (key == SDLK_d) {
        toggle_player_ready(2);
    }
}

void MapSelectScreen::render(IRenderer& renderer, const ants::assets::AssetArchive&) {
    using ants::assets::ColorRGBA;

    connection_ticks_++;

    // 1. Classic Setup Screen Background (Authentic CHD palette index 173)
    renderer.fill_rect(0, 0, 640, 480, ColorRGBA{219, 75, 19, 255});

    // Outer 3px dark teal frame with bevel
    renderer.draw_rect(0, 0, 640, 480, ColorRGBA{23, 71, 47, 255});
    renderer.draw_rect(1, 1, 638, 478, ColorRGBA{59, 151, 111, 255});
    renderer.draw_rect(2, 2, 636, 476, ColorRGBA{23, 71, 47, 255});

    // 2. Top Header Banner: hostbanr.bmp at (140, 1)
    renderer.draw_named_sprite("hostbanr.bmp", BANNER_X, BANNER_Y);

    // 3. Leave Game Button at (525, 12)
    const char* leave_spr = btn_quit_pressed_ ? "bleave3.bmp" : (btn_quit_hovered_ ? "bleave2.bmp" : "bleave1.bmp");
    renderer.draw_named_sprite(leave_spr, BTN_QUIT_X, BTN_QUIT_Y);

    // 4. Left Column:
    // Setup Logo: gamesetup.bmp at (42, 87)
    renderer.draw_named_sprite("gamesetup.bmp", LOGO_X, LOGO_Y);

    // Pick a Map Header: pickmap.bmp at (30, 281)
    renderer.draw_named_sprite("pickmap.bmp", PICKMAP_X, PICKMAP_Y);

    // Map Name Box: w_map.bmp at (27, 306)
    renderer.draw_named_sprite("w_map.bmp", W_MAP_X, W_MAP_Y);

    // Current Map Name inside w_map.bmp
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        renderer.draw_text(cur.display_name, 35, 320, ColorRGBA{255, 255, 255, 255});
    }

    // Up/Down Stepper Buttons at (226, 303) and (226, 327)
    const char* up_spr = btn_up_pressed_ ? "up3.bmp" : (btn_up_hovered_ ? "up2.bmp" : "up1.bmp");
    const char* dn_spr = btn_down_pressed_ ? "down3.bmp" : (btn_down_hovered_ ? "down2.bmp" : "down1.bmp");
    renderer.draw_named_sprite(up_spr, BTN_UP_X, BTN_UP_Y);
    renderer.draw_named_sprite(dn_spr, BTN_DOWN_X, BTN_DOWN_Y);

    // Map Info Header: mapinfo.bmp at (29, 352)
    renderer.draw_named_sprite("mapinfo.bmp", MAPINFO_X, MAPINFO_Y);

    // Map Info Box at (27, 376)
    draw_sunken_box(renderer, INFO_BOX_X, INFO_BOX_Y, INFO_BOX_W, INFO_BOX_H);
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        std::string info_text = cur.description + " (" + std::to_string(cur.minutes) + " min)";
        renderer.draw_text(info_text, 35, 389, ColorRGBA{255, 255, 255, 255});
    }

    // Status Line: statline.bmp at (25, 434)
    renderer.draw_named_sprite("statline.bmp", STATLINE_X, STATLINE_Y);
    renderer.draw_text("Game started, initializing...", 35, 456, ColorRGBA{255, 255, 255, 255});

    // 5. Right Column:
    // Players' Status Header: playstat.bmp at (368, 56)
    renderer.draw_named_sprite("playstat.bmp", PLAYSTAT_X, PLAYSTAT_Y);

    // Players' Status Box at (366, 82)
    draw_sunken_box(renderer, PLAYERS_BOX_X, PLAYERS_BOX_Y, PLAYERS_BOX_W, PLAYERS_BOX_H);

    // Slot 0: Green Ant, Dethcon, thumbs up (if ready)
    renderer.set_hud_team(0); // Team 0 = Green
    renderer.draw_named_sprite("agst201.bmp", 385, 88);
    renderer.set_hud_team(0);
    renderer.draw_text("Dethcon", 415, 103, ColorRGBA{255, 255, 255, 255});
    renderer.draw_named_sprite(is_player_ready(0) ? "thumb1.bmp" : "thumb3.bmp", 540, 97);

    // Slot 1: Blue Ant, OneStarNoTip, thumbs up (if ready)
    renderer.set_hud_team(2); // Team 2 = Blue
    renderer.draw_named_sprite("agst201.bmp", 385, 138);
    renderer.set_hud_team(0); // Reset before drawing thumb
    renderer.draw_text("OneStarNoTip", 415, 153, ColorRGBA{255, 255, 255, 255});
    renderer.draw_named_sprite(is_player_ready(1) ? "thumb1.bmp" : "thumb3.bmp", 540, 147);

    // Slot 2: Red Ant, Sgeo, thumbs down (if unready)
    renderer.set_hud_team(1); // Team 1 = Red
    renderer.draw_named_sprite("agst201.bmp", 385, 188);
    renderer.set_hud_team(0); // Reset before drawing thumb
    renderer.draw_text("Sgeo", 415, 203, ColorRGBA{255, 255, 255, 255});
    renderer.draw_named_sprite(is_player_ready(2) ? "thumb1.bmp" : "thumb3.bmp", 540, 197);

    // Reset hud team back to 0
    renderer.set_hud_team(0);

    // Drop Button at (576, 192)
    const char* drop_spr = btn_drop_pressed_ ? "drop3.bmp" : (btn_drop_hovered_ ? "drop2.bmp" : "drop1.bmp");
    renderer.draw_named_sprite(drop_spr, BTN_DROP_X, BTN_DROP_Y);

    // Fog of War Header at (367, 376)
    renderer.draw_named_sprite("fowar.bmp", FOW_HEADER_X, FOW_HEADER_Y);

    // Fog of War "On" & "Off" pill toggle buttons
    if (fog_of_war_) {
        // "On" button (Raised, active)
        renderer.fill_rect(BTN_FOW_ON_X, BTN_FOW_ON_Y, BTN_FOW_ON_W, BTN_FOW_ON_H, ColorRGBA{50, 110, 97, 255});
        renderer.draw_rect(BTN_FOW_ON_X, BTN_FOW_ON_Y, BTN_FOW_ON_W, BTN_FOW_ON_H, ColorRGBA{23, 71, 47, 255});
        renderer.fill_rect(BTN_FOW_ON_X + 1, BTN_FOW_ON_Y + 1, BTN_FOW_ON_W - 2, 1, ColorRGBA{105, 155, 123, 255});
        renderer.fill_rect(BTN_FOW_ON_X + 1, BTN_FOW_ON_Y + 1, 1, BTN_FOW_ON_H - 2, ColorRGBA{105, 155, 123, 255});
        renderer.fill_rect(BTN_FOW_ON_X + 1, BTN_FOW_ON_Y + BTN_FOW_ON_H - 1, BTN_FOW_ON_W - 1, 1, ColorRGBA{11, 27, 19, 255});
        renderer.fill_rect(BTN_FOW_ON_X + BTN_FOW_ON_W - 1, BTN_FOW_ON_Y + 1, 1, BTN_FOW_ON_H - 1, ColorRGBA{11, 27, 19, 255});
        renderer.draw_text("On", BTN_FOW_ON_X + 11, BTN_FOW_ON_Y + 6, ColorRGBA{220, 240, 230, 255});

        // "Off" button (Sunken, inactive)
        renderer.fill_rect(BTN_FOW_OFF_X, BTN_FOW_OFF_Y, BTN_FOW_OFF_W, BTN_FOW_OFF_H, ColorRGBA{7, 17, 20, 255});
        renderer.draw_rect(BTN_FOW_OFF_X, BTN_FOW_OFF_Y, BTN_FOW_OFF_W, BTN_FOW_OFF_H, ColorRGBA{23, 71, 47, 255});
        renderer.draw_text("Off", BTN_FOW_OFF_X + 14, BTN_FOW_OFF_Y + 6, ColorRGBA{50, 110, 97, 255});
    } else {
        // "On" button (Sunken, inactive)
        renderer.fill_rect(BTN_FOW_ON_X, BTN_FOW_ON_Y, BTN_FOW_ON_W, BTN_FOW_ON_H, ColorRGBA{7, 17, 20, 255});
        renderer.draw_rect(BTN_FOW_ON_X, BTN_FOW_ON_Y, BTN_FOW_ON_W, BTN_FOW_ON_H, ColorRGBA{23, 71, 47, 255});
        renderer.draw_text("On", BTN_FOW_ON_X + 11, BTN_FOW_ON_Y + 6, ColorRGBA{50, 110, 97, 255});

        // "Off" button (Raised, active)
        renderer.fill_rect(BTN_FOW_OFF_X, BTN_FOW_OFF_Y, BTN_FOW_OFF_W, BTN_FOW_OFF_H, ColorRGBA{50, 110, 97, 255});
        renderer.draw_rect(BTN_FOW_OFF_X, BTN_FOW_OFF_Y, BTN_FOW_OFF_W, BTN_FOW_OFF_H, ColorRGBA{23, 71, 47, 255});
        renderer.fill_rect(BTN_FOW_OFF_X + 1, BTN_FOW_OFF_Y + 1, BTN_FOW_OFF_W - 2, 1, ColorRGBA{105, 155, 123, 255});
        renderer.fill_rect(BTN_FOW_OFF_X + 1, BTN_FOW_OFF_Y + 1, 1, BTN_FOW_OFF_H - 2, ColorRGBA{105, 155, 123, 255});
        renderer.fill_rect(BTN_FOW_OFF_X + 1, BTN_FOW_OFF_Y + BTN_FOW_OFF_H - 1, BTN_FOW_OFF_W - 1, 1, ColorRGBA{11, 27, 19, 255});
        renderer.fill_rect(BTN_FOW_OFF_X + BTN_FOW_OFF_W - 1, BTN_FOW_OFF_Y + 1, 1, BTN_FOW_OFF_H - 1, ColorRGBA{11, 27, 19, 255});
        renderer.draw_text("Off", BTN_FOW_OFF_X + 14, BTN_FOW_OFF_Y + 6, ColorRGBA{220, 240, 230, 255});
    }

    // Fog of War Subtitle text
    renderer.draw_named_sprite("fowtext1.bmp", FOW_TEXT1_X, FOW_TEXT1_Y);
    renderer.draw_named_sprite("fowtext2.bmp", FOW_TEXT2_X, FOW_TEXT2_Y);

    // 6. Action Button: START! at (526, 442)
    const char* start_spr = btn_start_pressed_ ? "bstart3.bmp" : (btn_start_hovered_ ? "bstart2.bmp" : "bstart1.bmp");
    renderer.draw_named_sprite(start_spr, BTN_START_X, BTN_START_Y);
}

} // namespace ants::app

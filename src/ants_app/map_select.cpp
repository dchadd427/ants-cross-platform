#include "ants_app/map_select.hpp"
#include <iostream>
#include <algorithm>

namespace ants::app {

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
    fog_of_war_ = false;
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

void MapSelectScreen::render(IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    using ants::assets::ColorRGBA;

    connection_ticks_++;

    // 1. Authentic st_screen composite setup dialog (129 frame elements from Table 4 Animation 106)
    // Rendered in reverse order to produce authentic 640x480 terracotta layout with frames,
    // banners, headers, boxes, and Fog of War texts.
    const auto* anim = archive.find_animation("st_screen");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, fr.dx, fr.dy);
        }
    } else {
        renderer.fill_rect(0, 0, 640, 480, ColorRGBA{219, 75, 19, 255});
    }

    // 2. Top Header Leave Game Button at (525, 12)
    const char* leave_spr = btn_quit_pressed_ ? "bleave3.bmp" : (btn_quit_hovered_ ? "bleave2.bmp" : "bleave1.bmp");
    renderer.draw_named_sprite(leave_spr, BTN_QUIT_X, BTN_QUIT_Y);

    // 3. Current Map Name inside Pick a Map box
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        renderer.draw_text(cur.display_name, 38, 320, ColorRGBA{255, 255, 255, 255});
    }

    // Up/Down Stepper Buttons at (226, 303) and (226, 327)
    const char* up_spr = btn_up_pressed_ ? "up3.bmp" : (btn_up_hovered_ ? "up2.bmp" : "up1.bmp");
    const char* dn_spr = btn_down_pressed_ ? "down3.bmp" : (btn_down_hovered_ ? "down2.bmp" : "down1.bmp");
    renderer.draw_named_sprite(up_spr, BTN_UP_X, BTN_UP_Y);
    renderer.draw_named_sprite(dn_spr, BTN_DOWN_X, BTN_DOWN_Y);

    // 4. Map Info Description inside Map Info box
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        std::string info_text = cur.description + " (" + std::to_string(cur.minutes) + " min)";
        renderer.draw_text(info_text, 38, 387, ColorRGBA{255, 255, 255, 255});
    }

    // 5. Status line: authentic prompt text
    renderer.draw_text("Press START when all players' thumbs have appeared.", 38, 445, ColorRGBA{255, 255, 255, 255});

    // 6. Players' Status (Only connected players shown)
    std::string display_user = player_name_.empty() ? "Player" : player_name_;
    renderer.set_hud_team(0); // Team 0 = Green
    renderer.draw_named_sprite("agst301.bmp", 385, 95);
    renderer.set_hud_team(0);
    renderer.draw_text(display_user, 415, 107, ColorRGBA{255, 255, 255, 255});
    renderer.draw_named_sprite("thumb1.bmp", 540, 101);

    // 7. Fog of War On/Off Buttons using authentic sprites
    if (fog_of_war_) {
        renderer.draw_named_sprite("optond.bmp", 520, 374);
        renderer.draw_named_sprite("dbutoffu.bmp", 572, 376);
    } else {
        renderer.draw_named_sprite("dbutonu.bmp", 522, 376);
        renderer.draw_named_sprite("optoffd.bmp", 570, 374);
    }

    // 8. Action Button: START! at (526, 442)
    const char* start_spr = btn_start_pressed_ ? "bstart3.bmp" : (btn_start_hovered_ ? "bstart2.bmp" : "bstart1.bmp");
    renderer.draw_named_sprite(start_spr, BTN_START_X, BTN_START_Y);
}

} // namespace ants::app

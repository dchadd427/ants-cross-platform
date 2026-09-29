#include "ants_app/map_select.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_app/ui_anim.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace ants::app {

MapSelectScreen::MapSelectScreen() = default;

void MapSelectScreen::init(const std::string& maps_dir) {
    maps_.clear();
    std::unordered_set<std::string> seen_files;

    // Standard authentic Ants maps in canonical order (Ants.exe VA 0x10139e2)
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
        { "SMALL.LVL",    "SMALL",    "Small map for fast game",         40, 40,  8 },
        { "MEDIUM.LVL",   "MEDIUM",   "Intermediate map",                60, 60, 10 },
        { "TINY.LVL",     "TINY",     "Tiny map with no PowerUps",       31, 31,  6 },
        { "ISLANDS.LVL",  "ISLANDS",  "Island hopping, expert map",      60, 60, 12 },
        { "GAUNTLET.LVL", "GAUNTLET", "Race for your life!",             60, 60, 10 }
    };

    for (const auto& d : defaults) {
        MapSelectEntry entry;
        entry.filename = d.file;
        entry.full_path = maps_dir + "/" + d.file;
        if (!fs::exists(entry.full_path)) {
            if (fs::exists(std::string("Maps/") + d.file)) {
                entry.full_path = std::string("Maps/") + d.file;
            } else if (fs::exists(std::string("Original-Ants/Maps/") + d.file)) {
                entry.full_path = std::string("Original-Ants/Maps/") + d.file;
            }
        }
        entry.display_name = d.title;
        entry.description = d.desc;
        entry.width = d.w;
        entry.height = d.h;
        entry.anthills_count = (entry.width <= 31) ? 2 : 4;
        entry.minutes = d.minutes;

        // Dynamically parse authentic .LVL header from file if present
        ants::assets::LevelData lvl;
        if (lvl.load_from_file(entry.full_path)) {
            if (lvl.default_minutes > 0) {
                entry.minutes = lvl.default_minutes;
            }
            if (lvl.width > 0 && lvl.height > 0) {
                entry.width = lvl.width;
                entry.height = lvl.height;
            }
            if (!lvl.description.empty()) {
                entry.description = lvl.description;
            }
            if (!lvl.anthill_spawns.empty()) {
                uint32_t count = 0;
                for (const auto& sp : lvl.anthill_spawns) {
                    if (sp.team_id < 4) ++count;
                }
                if (count > 0) entry.anthills_count = count;
            }
        }

        seen_files.insert(d.file);
        std::string upper_f = d.file;
        std::transform(upper_f.begin(), upper_f.end(), upper_f.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        seen_files.insert(upper_f);
        maps_.push_back(std::move(entry));
    }

    // Dynamic scanning of maps_dir for any additional .lvl / .LVL files
    std::error_code ec;
    if (!maps_dir.empty() && fs::exists(maps_dir, ec) && fs::is_directory(maps_dir, ec)) {
        for (const auto& dir_entry : fs::directory_iterator(maps_dir, ec)) {
            if (dir_entry.is_regular_file(ec)) {
                std::string fname = dir_entry.path().filename().string();
                std::string ext = dir_entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".lvl") {
                    std::string upper_f = fname;
                    std::transform(upper_f.begin(), upper_f.end(), upper_f.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    if (seen_files.find(upper_f) == seen_files.end()) {
                        seen_files.insert(upper_f);
                        MapSelectEntry extra;
                        extra.filename = fname;
                        extra.full_path = dir_entry.path().string();
                        extra.display_name = dir_entry.path().stem().string();
                        extra.description = "Custom map";
                        extra.width = 60;
                        extra.height = 60;
                        extra.anthills_count = 4;
                        extra.minutes = 10;

                        ants::assets::LevelData lvl;
                        if (lvl.load_from_file(extra.full_path)) {
                            if (lvl.default_minutes > 0) extra.minutes = lvl.default_minutes;
                            if (lvl.width > 0 && lvl.height > 0) {
                                extra.width = lvl.width;
                                extra.height = lvl.height;
                            }
                            if (!lvl.description.empty()) extra.description = lvl.description;
                            if (!lvl.anthill_spawns.empty()) {
                                uint32_t count = 0;
                                for (const auto& sp : lvl.anthill_spawns) {
                                    if (sp.team_id < 4) ++count;
                                }
                                if (count > 0) extra.anthills_count = count;
                            }
                        }
                        maps_.push_back(std::move(extra));
                    }
                }
            }
        }
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
        play_sfx(sim::SoundID::ButtonClick);   // up3 carries sound 0 (buttonclick.wav)
        set_selected_index(selected_index_ - 1);
        return;
    }

    // Down Arrow Button
    if (screen_x >= BTN_DOWN_X && screen_x < BTN_DOWN_X + BTN_DOWN_W &&
        screen_y >= BTN_DOWN_Y && screen_y < BTN_DOWN_Y + BTN_DOWN_H) {
        btn_down_pressed_ = true;
        play_sfx(sim::SoundID::ButtonClick);
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
        set_fog_of_war_enabled(true);   // the Fog of War toggles are silent
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
        play_sfx(sim::SoundID::ButtonClick);
        toggle_player_ready(2);
        return;
    }

    // Click on player thumbs inside players box (x in [535, 565])
    if (screen_x >= PLAYER_THUMB_X - 5 && screen_x < PLAYER_THUMB_X + 25) {
        for (uint8_t i = 0; i < 3; ++i) {
            int32_t ty = PLAYER_THUMB_Y + static_cast<int32_t>(i) * PLAYER_ROW_PITCH;
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
        play_sfx(sim::SoundID::ButtonClick);
        trigger_start();
        return;
    }

    // Leave Game Button
    if (screen_x >= BTN_QUIT_X && screen_x < BTN_QUIT_X + BTN_QUIT_W &&
        screen_y >= BTN_QUIT_Y && screen_y < BTN_QUIT_Y + BTN_QUIT_H) {
        btn_quit_pressed_ = true;
        play_sfx(sim::SoundID::ButtonClick);
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

    // 2. Leave Game button: animations leave1 / leave2 (hover) / leave3 (pressed), absolute coordinates
    draw_animation_frame0(renderer, archive, btn_quit_pressed_ ? "leave3" : (btn_quit_hovered_ ? "leave2" : "leave1"));

    int32_t th = renderer.get_text_height(FontSize::Small);

    // 3. Current Map Name inside Pick a Map box (vertically centered in inner cavity y=307..335, h=29)
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        int32_t th_map = renderer.get_text_height(FontSize::Large);
        int32_t name_y = 307 + (29 - th_map) / 2;
        renderer.draw_text(cur.display_name, 38, name_y, ColorRGBA{255, 255, 255, 255}, FontSize::Large);
    }

    // Up/Down stepper buttons: animations up1..3 and down1..3 (up / hover / pressed), absolute coordinates
    draw_animation_frame0(renderer, archive, btn_up_pressed_ ? "up3" : (btn_up_hovered_ ? "up2" : "up1"));
    draw_animation_frame0(renderer, archive, btn_down_pressed_ ? "down3" : (btn_down_hovered_ ? "down2" : "down1"));

    // 4. Map Info Description inside Map Info box (vertically centered in inner cavity y=377..405, h=29)
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        std::string info_text = cur.description + " (" + std::to_string(cur.minutes) + " min)";
        int32_t info_y = 377 + (29 - th) / 2;
        renderer.draw_text(info_text, 38, info_y, ColorRGBA{255, 255, 255, 255}, FontSize::Small);
    }

    // 5. Status line: authentic prompt text (vertically centered in statline box at y=445..464)
    int32_t stat_y = 445 + (19 - th) / 2;
    renderer.draw_text("Press START when all players' thumbs have appeared.", 38, stat_y, ColorRGBA{255, 255, 255, 255}, FontSize::Small);

    // 6. Players' Status, slot 0: the portrait animation agst301 (12 frames, 1650 ms loop) has its origin at (395,115) and
    // the thumbs-up sprite sits at (540,95); the sprite's own part offsets place the ant relative to that origin.
    std::string display_user = player_name_.empty() ? "Player" : player_name_;
    if (const auto* anim_stand = archive.find_animation("agst301")) {
        if (!anim_stand->subitems.empty()) {
            const size_t frame = Renderer::get_anim_subitem_by_time(*anim_stand, SDL_GetTicks());
            const auto& parts = anim_stand->subitems[frame].frames;
            renderer.set_hud_team(player_team_);
            for (size_t k = parts.size(); k-- > 0;) {
                renderer.draw_sprite(parts[k].sprite_index, PLAYER_PORTRAIT_X + parts[k].dx, PLAYER_PORTRAIT_Y + parts[k].dy);
            }
            renderer.set_hud_team(0);
        }
    }

    int32_t player_y = PLAYER_THUMB_Y + (24 - th) / 2;
    renderer.draw_text(display_user, 415, player_y, ColorRGBA{255, 255, 255, 255}, FontSize::Small);
    renderer.draw_named_sprite("thumb1.bmp", PLAYER_THUMB_X, PLAYER_THUMB_Y);

    // 7. Fog of War On / Off (animations d_on1..3, d_off1..3): the chosen one is shown down, the other one up or hover
    draw_animation_frame0(renderer, archive, fog_of_war_ ? "d_on3" : (btn_fow_on_hovered_ ? "d_on2" : "d_on1"));
    draw_animation_frame0(renderer, archive, !fog_of_war_ ? "d_off3" : (btn_fow_off_hovered_ ? "d_off2" : "d_off1"));

    // 8. START! button: animations start1 / start2 (hover) / start3 (pressed), absolute coordinates
    draw_animation_frame0(renderer, archive, btn_start_pressed_ ? "start3" : (btn_start_hovered_ ? "start2" : "start1"));
}

} // namespace ants::app

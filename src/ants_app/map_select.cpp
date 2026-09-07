#include "ants_app/map_select.hpp"
#include <iostream>
#include <algorithm>
#include <fstream>

namespace ants::app {

MapSelectScreen::MapSelectScreen() = default;

void MapSelectScreen::init(const std::string& maps_dir) {
    maps_.clear();

    // Standard authentic Microsoft Ants maps in canonical order
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
    hovered_index_ = -1;
}

void MapSelectScreen::set_selected_index(int32_t idx) noexcept {
    if (maps_.empty()) return;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int32_t>(maps_.size())) {
        idx = static_cast<int32_t>(maps_.size()) - 1;
    }
    selected_index_ = idx;
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

    hovered_index_ = -1;
    for (size_t i = 0; i < maps_.size(); ++i) {
        int32_t cy = CARD_Y + static_cast<int32_t>(i) * (CARD_H + CARD_SPACING);
        if (screen_x >= CARD_X && screen_x <= CARD_X + CARD_W &&
            screen_y >= cy && screen_y <= cy + CARD_H) {
            hovered_index_ = static_cast<int32_t>(i);
            break;
        }
    }

    btn_start_hovered_ = (screen_x >= BTN_START_X && screen_x <= BTN_START_X + BTN_START_W &&
                          screen_y >= BTN_START_Y && screen_y <= BTN_START_Y + BTN_START_H);

    btn_quit_hovered_ = (screen_x >= BTN_QUIT_X && screen_x <= BTN_QUIT_X + BTN_QUIT_W &&
                         screen_y >= BTN_QUIT_Y && screen_y <= BTN_QUIT_Y + BTN_QUIT_H);
}

void MapSelectScreen::handle_mouse_down(int32_t screen_x, int32_t screen_y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return;

    uint32_t now = SDL_GetTicks();

    // Check Map Cards
    for (size_t i = 0; i < maps_.size(); ++i) {
        int32_t cy = CARD_Y + static_cast<int32_t>(i) * (CARD_H + CARD_SPACING);
        if (screen_x >= CARD_X && screen_x <= CARD_X + CARD_W &&
            screen_y >= cy && screen_y <= cy + CARD_H) {
            
            // Detect double-click within 400ms on the same item
            if (last_click_card_ == static_cast<int32_t>(i) && (now - last_click_timestamp_ < 400)) {
                selected_index_ = static_cast<int32_t>(i);
                trigger_start();
                return;
            }

            selected_index_ = static_cast<int32_t>(i);
            last_click_card_ = static_cast<int32_t>(i);
            last_click_timestamp_ = now;
            return;
        }
    }

    // Check Start Button
    if (screen_x >= BTN_START_X && screen_x <= BTN_START_X + BTN_START_W &&
        screen_y >= BTN_START_Y && screen_y <= BTN_START_Y + BTN_START_H) {
        btn_start_pressed_ = true;
        trigger_start();
        return;
    }

    // Check Quit Button
    if (screen_x >= BTN_QUIT_X && screen_x <= BTN_QUIT_X + BTN_QUIT_W &&
        screen_y >= BTN_QUIT_Y && screen_y <= BTN_QUIT_Y + BTN_QUIT_H) {
        btn_quit_pressed_ = true;
        trigger_quit();
        return;
    }
}

void MapSelectScreen::handle_mouse_up(int32_t, int32_t, uint8_t button) {
    if (button == SDL_BUTTON_LEFT) {
        btn_start_pressed_ = false;
        btn_quit_pressed_ = false;
    }
}

void MapSelectScreen::handle_key_down(SDL_Keycode key) {
    if (maps_.empty()) return;

    if (key == SDLK_UP) {
        selected_index_ = (selected_index_ + static_cast<int32_t>(maps_.size()) - 1) % static_cast<int32_t>(maps_.size());
    } else if (key == SDLK_DOWN) {
        selected_index_ = (selected_index_ + 1) % static_cast<int32_t>(maps_.size());
    } else if (key >= SDLK_1 && key <= SDLK_6) {
        int idx = key - SDLK_1;
        if (idx < static_cast<int32_t>(maps_.size())) {
            selected_index_ = idx;
        }
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
        trigger_start();
    } else if (key == SDLK_ESCAPE) {
        trigger_quit();
    }
}

void MapSelectScreen::render(IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    using ants::assets::ColorRGBA;

    // 1. Clear Virtual Canvas Background (Dark Navy)
    renderer.fill_rect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, ColorRGBA{16, 20, 30, 255});

    // 2. Top Header Banner
    renderer.fill_rect(0, 0, CANVAS_WIDTH, 56, ColorRGBA{24, 30, 44, 255});
    renderer.fill_rect(0, 56, CANVAS_WIDTH, 2, ColorRGBA{210, 175, 55, 255}); // Gold divider

    // Banner Text
    renderer.draw_text("MICROSOFT ANTS", CARD_X, 14, ColorRGBA{245, 215, 80, 255});
    renderer.draw_text("SELECT BATTLEFIELD & MAP CONFIGURATION", CARD_X, 32, ColorRGBA{170, 185, 210, 255});

    // Audio status badge
    renderer.fill_rect(445, 14, 170, 28, ColorRGBA{20, 40, 28, 255});
    renderer.draw_rect(445, 14, 170, 28, ColorRGBA{60, 160, 90, 255});
    renderer.draw_text("INTRO MUSIC ACTIVE", 456, 23, ColorRGBA{90, 230, 130, 255});

    // 3. Render Map Cards
    for (size_t i = 0; i < maps_.size(); ++i) {
        const auto& map = maps_[i];
        int32_t cy = CARD_Y + static_cast<int32_t>(i) * (CARD_H + CARD_SPACING);

        bool is_sel = (static_cast<int32_t>(i) == selected_index_);
        bool is_hov = (static_cast<int32_t>(i) == hovered_index_);

        ColorRGBA bg_col = is_sel ? ColorRGBA{36, 52, 78, 255}
                                  : (is_hov ? ColorRGBA{28, 38, 56, 255}
                                            : ColorRGBA{22, 28, 40, 255});

        ColorRGBA border_col = is_sel ? ColorRGBA{240, 210, 50, 255}
                                      : (is_hov ? ColorRGBA{100, 140, 200, 255}
                                                : ColorRGBA{45, 55, 75, 255});

        renderer.fill_rect(CARD_X, cy, CARD_W, CARD_H, bg_col);
        renderer.draw_rect(CARD_X, cy, CARD_W, CARD_H, border_col);

        // Indicator tag
        if (is_sel) {
            renderer.fill_rect(CARD_X + 2, cy + 2, 4, CARD_H - 4, ColorRGBA{240, 210, 50, 255});
        }

        // Line 1: [1] MAPNAME.LVL - Title
        std::string title_line = "[" + std::to_string(i + 1) + "] " + map.filename + "  -  " + map.display_name;
        ColorRGBA title_col = is_sel ? ColorRGBA{255, 240, 130, 255} : ColorRGBA{230, 235, 245, 255};
        renderer.draw_text(title_line, CARD_X + 16, cy + 8, title_col);

        // Line 2: Grid & Description
        std::string desc_line = "Grid: " + std::to_string(map.width) + "x" + std::to_string(map.height) +
                                " | " + std::to_string(map.anthills_count) + " Bases | " + map.description;
        renderer.draw_text(desc_line, CARD_X + 24, cy + 26, ColorRGBA{130, 175, 215, 255});
    }

    // 4. Right Preview Panel
    renderer.fill_rect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ColorRGBA{22, 28, 40, 255});
    renderer.draw_rect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ColorRGBA{55, 70, 95, 255});

    renderer.draw_text("MAP PREVIEW", PREVIEW_X + 38, PREVIEW_Y + 10, ColorRGBA{210, 220, 240, 255});

    // Draw authentic pickmap.bmp artwork if available
    int32_t pick_sid = archive.find_sprite_id("pickmap.bmp");
    if (pick_sid >= 0) {
        renderer.draw_sprite(static_cast<uint32_t>(pick_sid), PREVIEW_X + 7, PREVIEW_Y + 28);
    } else {
        renderer.fill_rect(PREVIEW_X + 12, PREVIEW_Y + 28, 140, 120, ColorRGBA{14, 18, 26, 255});
        renderer.draw_text("NO PREVIEW", PREVIEW_X + 38, PREVIEW_Y + 80, ColorRGBA{100, 120, 150, 255});
    }

    // Map summary below preview image
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        renderer.draw_text(cur.filename, PREVIEW_X + 14, PREVIEW_Y + 185, ColorRGBA{255, 220, 90, 255});
        
        std::string dim_str = "Size: " + std::to_string(cur.width) + " x " + std::to_string(cur.height);
        renderer.draw_text(dim_str, PREVIEW_X + 14, PREVIEW_Y + 205, ColorRGBA{180, 200, 225, 255});

        std::string base_str = "Spawns: " + std::to_string(cur.anthills_count) + " Anthills";
        renderer.draw_text(base_str, PREVIEW_X + 14, PREVIEW_Y + 225, ColorRGBA{180, 200, 225, 255});

        renderer.draw_text("Rules: Standard", PREVIEW_X + 14, PREVIEW_Y + 245, ColorRGBA{120, 210, 160, 255});
    }

    // 5. Action Buttons
    // Start Button
    ColorRGBA start_bg = btn_start_pressed_ ? ColorRGBA{25, 80, 35, 255}
                                            : (btn_start_hovered_ ? ColorRGBA{50, 155, 65, 255}
                                                                  : ColorRGBA{38, 120, 50, 255});
    renderer.fill_rect(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H, start_bg);
    renderer.draw_rect(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H, ColorRGBA{100, 230, 120, 255});
    renderer.draw_text("START GAME  [ENTER]", BTN_START_X + 12, BTN_START_Y + 14, ColorRGBA{255, 255, 255, 255});

    // Quit Button
    ColorRGBA quit_bg = btn_quit_pressed_ ? ColorRGBA{75, 25, 25, 255}
                                          : (btn_quit_hovered_ ? ColorRGBA{130, 42, 42, 255}
                                                               : ColorRGBA{95, 32, 32, 255});
    renderer.fill_rect(BTN_QUIT_X, BTN_QUIT_Y, BTN_QUIT_W, BTN_QUIT_H, quit_bg);
    renderer.draw_rect(BTN_QUIT_X, BTN_QUIT_Y, BTN_QUIT_W, BTN_QUIT_H, ColorRGBA{200, 75, 75, 255});
    renderer.draw_text("QUIT GAME    [ESC]", BTN_QUIT_X + 16, BTN_QUIT_Y + 12, ColorRGBA{235, 220, 220, 255});

    // 6. Footer Navigation Bar
    renderer.fill_rect(0, 442, CANVAS_WIDTH, 1, ColorRGBA{50, 60, 80, 255});
    renderer.draw_text("[1-6 / UP / DOWN] Select Map      [ENTER] Launch Match      [ESC] Quit Game",
                       CARD_X, 455, ColorRGBA{130, 165, 195, 255});
}

} // namespace ants::app

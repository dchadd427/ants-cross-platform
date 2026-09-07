#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace ants::app {

namespace {

// Authentic Team color RGB palettes
constexpr assets::ColorRGBA TEAM_COLORS[4] = {
    {79, 87, 111, 255},   // 0: Black
    {119, 175, 239, 255}, // 1: Blue
    {251, 51, 91, 255},   // 2: Red
    {83, 147, 43, 255}    // 3: Green
};

const char* ANT_TYPE_NAMES[] = {
    "Worker Ant",
    "Bomber Ant",
    "Fire Ant",
    "Thief Ant",
    "Combat Ant",
    "Swimmer Ant"
};

} // anonymous namespace

HUD::HUD() {
    init(0);
}

void HUD::init(uint8_t local_player_id) {
    local_player_id_ = local_player_id;
    selected_ant_id_ = 0;
    active_order_mode_ = sim::OrderType::None;
    is_dragging_ = false;
    is_radar_dragging_ = false;
    incubation_timer_ticks_ = 0;
    is_incubating_ = false;
    news_queue_.clear();

    // Configure Top Header Buttons (x0y0.bmp)
    help_button_ = {475, 0, 45, 22, 0, 0, 0, false, true, false};
    options_button_ = {525, 0, 50, 22, 0, 0, 0, false, true, false};
    quit_button_ = {580, 0, 48, 22, 0, 0, 0, false, true, false};

    // Configure Quit Confirmation Dialog Buttons
    yes_button_ = {205, 255, 49, 24, 0, 0, 0, false, true, false};
    no_button_ = {345, 255, 49, 24, 0, 0, 0, false, true, false};

    show_quit_dialog_ = false;
    show_quick_help_ = false;
    show_options_ = false;

    // Configure Hatch Button at (492, 262)
    hatch_button_.x = 492;
    hatch_button_.y = 262;
    hatch_button_.w = 36;
    hatch_button_.h = 26;
    hatch_button_.sprite_up = 2683;    // buthatup.bmp
    hatch_button_.sprite_down = 2684;  // buthatd.bmp
    hatch_button_.sprite_label = 2682; // labhatch.bmp

    // Configure 7 Action Buttons at (484..636, 362..458)
    // 1. Move
    action_buttons_[0] = {490, 365, 45, 30, 2573, 2585, 2572, false, true, false}; // butmovu, butmovd, labmov
    // 2. Attack
    action_buttons_[1] = {540, 365, 45, 30, 2580, 2589, 2579, false, true, false}; // butattu, butattd, labatt
    // 3. Bomb
    action_buttons_[2] = {590, 365, 45, 30, 2581, 2590, 2582, false, false, false}; // butbomu, butbomd, labbom
    // 4. Fire
    action_buttons_[3] = {490, 400, 45, 30, 2584, 2584, 2583, false, false, false}; // butfireu, labfire
    // 5. Bridge
    action_buttons_[4] = {540, 400, 45, 30, 2576, 2587, 2575, false, false, false}; // butdipu, butdipd, labdib
    // 6. Thief
    action_buttons_[5] = {590, 400, 45, 30, 2577, 2588, 2578, false, false, false}; // butthfu, butthfd, labthf
    // 7. Cancel
    action_buttons_[6] = {540, 435, 45, 25, 2706, 2707, 2705, false, true, false}; // butcanu, butcand, labcan

    queue_news_message("Microsoft Ants Remake", 200, false);
}

void HUD::reset() {
    init(local_player_id_);
}

void HUD::set_active_order_mode(sim::OrderType mode) noexcept {
    active_order_mode_ = mode;
    for (auto& btn : action_buttons_) {
        btn.is_active = false;
    }
    switch (mode) {
        case sim::OrderType::Move:              action_buttons_[0].is_active = true; break;
        case sim::OrderType::Attack:            action_buttons_[1].is_active = true; break;
        case sim::OrderType::PlantBomb:
        case sim::OrderType::DefuseBomb:        action_buttons_[2].is_active = true; break;
        case sim::OrderType::IgniteFire:
        case sim::OrderType::ExtinguishFire:    action_buttons_[3].is_active = true; break;
        case sim::OrderType::BuildBridge:       action_buttons_[4].is_active = true; break;
        case sim::OrderType::InfiltrateAnthill: action_buttons_[5].is_active = true; break;
        default: break;
    }
}

void HUD::update(const sim::WorldState& world, uint32_t delta_ticks) {
    // 1. Update news banner FIFO queue
    if (!news_queue_.empty()) {
        if (news_queue_.front().remaining_ticks <= delta_ticks) {
            news_queue_.pop_front();
        } else {
            news_queue_.front().remaining_ticks -= delta_ticks;
        }
    }

    // 2. Alarm siren blinking
    alarm_blink_ticks_ += delta_ticks;

    // 3. Incubation progress
    if (is_incubating_) {
        if (incubation_timer_ticks_ <= delta_ticks) {
            incubation_timer_ticks_ = 0;
            is_incubating_ = false;
        } else {
            incubation_timer_ticks_ -= delta_ticks;
        }
    }

    // 4. Update button contextual enabled status
    update_action_buttons_state(world);
}

void HUD::poll_sim_events(sim::SimulationEngine& sim) {
    auto news = sim.poll_news_events();
    for (const auto& ev : news) {
        if (ev.target_player == 255 || ev.target_player == local_player_id_) {
            bool is_thief_alarm = (ev.string_id == sim::StringID::ThiefAlarmWarning);
            queue_news_message(ev.message_text, 100, is_thief_alarm);
        }
    }
}

void HUD::queue_news_message(const std::string& msg, uint32_t duration_ticks, bool is_alarm) {
    news_queue_.push_back({msg, duration_ticks, is_alarm, 255});
    if (news_queue_.size() > 32) {
        news_queue_.pop_front();
    }
}

void HUD::update_action_buttons_state(const sim::WorldState& world) {
    bool has_friendly = false;
    bool has_bomber = false;
    bool has_fire = false;
    bool has_swimmer = false;
    bool has_thief = false;

    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        if (ant.player_id == local_player_id_) {
            if (is_ant_selected(ant.id) || ant.id == selected_ant_id_) {
                has_friendly = true;
                if (ant.type == sim::AntType::Bomber) has_bomber = true;
                if (ant.type == sim::AntType::Fire) has_fire = true;
                if (ant.type == sim::AntType::Swimmer) has_swimmer = true;
                if (ant.type == sim::AntType::Thief) has_thief = true;
            }
        }
    }

    if (!has_friendly) {
        // No friendly unit selected: disable all action buttons except Cancel
        for (size_t i = 0; i < 6; ++i) action_buttons_[i].is_enabled = false;
        action_buttons_[6].is_enabled = true; // Cancel
    } else {
        // Move & Attack are universally enabled for all friendly units
        action_buttons_[0].is_enabled = true; // Move
        action_buttons_[1].is_enabled = true; // Attack

        // Class-specific abilities
        action_buttons_[2].is_enabled = has_bomber;
        action_buttons_[3].is_enabled = has_fire;
        action_buttons_[4].is_enabled = has_swimmer;
        action_buttons_[5].is_enabled = has_thief;
        action_buttons_[6].is_enabled = true; // Cancel
    }

    // Hatch button check: cost 200 pts and > 0 eggs
    int32_t score = (local_player_id_ < world.player_scores.size()) ? world.player_scores[local_player_id_] : 0;
    uint32_t eggs = (local_player_id_ < world.player_eggs.size()) ? world.player_eggs[local_player_id_] : 0;
    hatch_button_.is_enabled = (score >= 200 && eggs > 0);
}

// =========================================================================
// Rendering Subsystem
// =========================================================================

void HUD::render(IRenderer& renderer, const assets::AssetArchive& assets,
                 const sim::WorldState& world, const ViewportCamera& camera) {
    // 1. Playfield Frame Borders
    renderer.draw_named_sprite("x0y22.bmp", 0, 22);
    renderer.draw_named_sprite("x458y35.bmp", 458, 35);
    renderer.draw_named_sprite("x458y22.bmp", 458, 22);

    // 2. Right Panel Modules
    render_radar(renderer, assets, world, camera);
    render_selection_card(renderer, assets, world);
    render_hatch_panel(renderer, assets, world);
    render_action_buttons(renderer, assets, world);

    // 3. Top & Bottom Frames
    render_top_bar(renderer, assets, world);
    render_news_banner(renderer, assets, world);

    // 4. Marquee Selection Box
    if (is_dragging_) {
        render_marquee_box(renderer);
    }

    // 5. Overlays and Dialogs
    if (show_quick_help_) {
        render_quick_help(renderer, assets);
    } else if (show_options_) {
        render_options_dialog(renderer, assets);
    } else if (show_quit_dialog_) {
        render_quit_dialog(renderer, assets);
    }
}

void HUD::render_top_bar(IRenderer& renderer, const assets::AssetArchive&, const sim::WorldState& world) {
    // Top border backdrop: x0y0.bmp (640x22)
    renderer.draw_named_sprite("x0y0.bmp", 0, 0);

    // Box 1 (Top-Left): Match Clock countdown in pre-cut black box at (61..129, 4..17)
    uint32_t ms = world.match_time_remaining_ms;
    uint32_t mm = (ms / 1000) / 60;
    uint32_t ss = (ms / 1000) % 60;

    int32_t cx = 75, cy = 5;
    std::string dig_m0 = "dig" + std::to_string(mm / 10) + ".bmp";
    std::string dig_m1 = "dig" + std::to_string(mm % 10) + ".bmp";
    std::string dig_s0 = "dig" + std::to_string(ss / 10) + ".bmp";
    std::string dig_s1 = "dig" + std::to_string(ss % 10) + ".bmp";

    renderer.draw_named_sprite(dig_m0, cx + 0, cy);
    renderer.draw_named_sprite(dig_m1, cx + 8, cy);
    renderer.draw_named_sprite("digc.bmp", cx + 16, cy); // colon
    renderer.draw_named_sprite(dig_s0, cx + 24, cy);
    renderer.draw_named_sprite(dig_s1, cx + 32, cy);

    // Box 2 (Top-Right above Playfield): Local player's own score in pre-cut black box at (402..456, 4..17)
    int32_t my_score = (local_player_id_ < world.player_scores.size()) ? world.player_scores[local_player_id_] : 0;
    renderer.draw_text(std::to_string(my_score), 415, 5, {255, 255, 255, 255});

    // Top Header Buttons feedback (Help, Options, Quit)
    if (help_button_.is_pressed || show_quick_help_) {
        renderer.draw_named_sprite("buthelpd.bmp", 475, 0);
    }
    if (options_button_.is_pressed || show_options_) {
        renderer.draw_named_sprite("butoptd.bmp", 525, 0);
    }
    if (quit_button_.is_pressed || show_quit_dialog_) {
        renderer.draw_named_sprite("butquitd.bmp", 580, 0);
    }
}

void HUD::render_radar(IRenderer& renderer, const assets::AssetArchive&,
                       const sim::WorldState& world, const ViewportCamera& camera) {
    // Minimap panel background at (599, 35)
    renderer.draw_named_sprite("x599y35.bmp", 599, 35);

    // Exact inner viewport bounds matching frame bezels
    const int32_t rx = 480, ry = 35;
    const int32_t rw = 119, rh = 91;

    // Fill radar dark base
    renderer.fill_rect(rx, ry, rw, rh, {30, 25, 20, 255});

    if (world.width == 0 || world.height == 0) return;

    float scale_x = static_cast<float>(rw) / static_cast<float>(world.width);
    float scale_y = static_cast<float>(rh) / static_cast<float>(world.height);

    // Rasterize ground terrain tiles
    if (world.cells.size() == world.width * world.height) {
        for (uint32_t ty = 0; ty < world.height; ++ty) {
            int32_t py = ry + static_cast<int32_t>(ty * scale_y);
            int32_t ph = std::max(1, static_cast<int32_t>((ty + 1) * scale_y) - static_cast<int32_t>(ty * scale_y));
            for (uint32_t tx = 0; tx < world.width; ++tx) {
                const auto& cell = world.cells[ty * world.width + tx];
                int32_t px = rx + static_cast<int32_t>(tx * scale_x);
                int32_t pw = std::max(1, static_cast<int32_t>((tx + 1) * scale_x) - static_cast<int32_t>(tx * scale_x));
                assets::ColorRGBA col{60, 110, 42, 255}; // Walkable grass
                if (cell.terrain_type == sim::TERRAIN_WATER) {
                    col = {25, 75, 150, 255}; // Water
                } else if (cell.terrain_type == sim::TERRAIN_OBSTACLE) {
                    col = {45, 42, 38, 255}; // Obstacle / rock
                } else if (cell.has_completed_bridge()) {
                    col = {140, 100, 60, 255}; // Bridge
                } else if (cell.has_fire()) {
                    col = {240, 80, 20, 255}; // Fire
                } else if (cell.is_food) {
                    col = {230, 210, 50, 255}; // Food morsel
                }
                renderer.fill_rect(px, py, pw, ph, col);
            }
        }
    }

    // Anthill base markers (4x4 footprint)
    for (const auto& base : world.anthills) {
        int32_t bx = rx + static_cast<int32_t>(base.x * scale_x);
        int32_t by = ry + static_cast<int32_t>(base.y * scale_y);
        int32_t bw = std::max(4, static_cast<int32_t>(4.0f * scale_x));
        int32_t bh = std::max(4, static_cast<int32_t>(4.0f * scale_y));
        assets::ColorRGBA c = (base.team_id < 4) ? TEAM_COLORS[base.team_id] : assets::ColorRGBA{200, 200, 200, 255};
        renderer.fill_rect(bx, by, bw, bh, c);
        renderer.draw_rect(bx, by, bw, bh, {255, 255, 255, 200});
    }

    // Active live ants
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning || ant.is_underground) continue;
        int32_t ax = rx + static_cast<int32_t>(static_cast<float>(ant.tile_x) * scale_x);
        int32_t ay = ry + static_cast<int32_t>(static_cast<float>(ant.tile_y) * scale_y);
        assets::ColorRGBA c = (ant.player_id < 4) ? TEAM_COLORS[ant.player_id] : assets::ColorRGBA{255, 255, 255, 255};
        renderer.fill_rect(ax, ay, 2, 2, c);

        if (ant.id == selected_ant_id_) {
            // Outline selected unit
            renderer.draw_rect(ax - 1, ay - 1, 4, 4, {255, 255, 255, 255});
        }
    }

    // Camera frustum wireframe box
    float map_w_px = static_cast<float>(world.width * 32);
    float map_h_px = static_cast<float>(world.height * 32);
    if (map_w_px > 0.0f && map_h_px > 0.0f) {
        float cam_tile_x = camera.x / 32.0f;
        float cam_tile_y = camera.y / 32.0f;
        float cam_tile_w = static_cast<float>(camera.viewport_w) / 32.0f;
        float cam_tile_h = static_cast<float>(camera.viewport_h) / 32.0f;

        int32_t fx = rx + static_cast<int32_t>(cam_tile_x * scale_x);
        int32_t fy = ry + static_cast<int32_t>(cam_tile_y * scale_y);
        int32_t fw = static_cast<int32_t>(cam_tile_w * scale_x);
        int32_t fh = static_cast<int32_t>(cam_tile_h * scale_y);

        renderer.draw_rect(fx, fy, std::max(4, fw), std::max(4, fh), {255, 255, 255, 255});
    }
}

void HUD::render_selection_card(IRenderer& renderer, const assets::AssetArchive&, const sim::WorldState& world) {
    // Card panel background: x480y126.bmp (160x128)
    renderer.draw_named_sprite("x480y126.bmp", CARD_X, CARD_Y);

    const sim::AntSnapshot* sel = nullptr;
    if (selected_ant_id_ != 0) {
        for (const auto& a : world.ants) {
            if (a.id == selected_ant_id_) { sel = &a; break; }
        }
    }

    if (!sel) {
        renderer.draw_text("No Selection", CARD_X + 32, CARD_Y + 50, {160, 160, 160, 255});
        return;
    }

    // Title header: wtype.bmp at (488, 130)
    renderer.draw_named_sprite("wtype.bmp", 488, 130);
    if (selected_ant_ids_.size() > 1) {
        std::string grp_text = "Group (" + std::to_string(selected_ant_ids_.size()) + ")";
        renderer.draw_text(grp_text, 505, 133, {255, 255, 255, 255});
    } else {
        uint8_t type_idx = static_cast<uint8_t>(sel->type);
        if (type_idx < 6) {
            renderer.draw_text(ANT_TYPE_NAMES[type_idx], 505, 133, {255, 255, 255, 255});
        }
    }

    // Portrait frame at (525, 145)
    std::string portrait_name = "agst301.bmp";
    switch (sel->type) {
        case sim::AntType::Worker:  portrait_name = "agst301.bmp"; break;
        case sim::AntType::Bomber:  portrait_name = "abst301.bmp"; break;
        case sim::AntType::Fire:    portrait_name = "afst301.bmp"; break;
        case sim::AntType::Combat:  portrait_name = "acst301.bmp"; break;
        case sim::AntType::Swimmer: portrait_name = "asst301.bmp"; break;
        case sim::AntType::Thief:   portrait_name = "atst301.bmp"; break;
    }
    renderer.draw_named_sprite(portrait_name, 525, 145);

    // Lunchbox / Carrying icon
    if (sel->is_holding || sel->carried_points > 0) {
        renderer.draw_named_sprite("lunchicon.bmp", 592, 145);
        if (sel->carried_points > 0) {
            renderer.draw_text("+" + std::to_string(sel->carried_points), 592, 185, {255, 215, 0, 255});
        }
    }

    // Health Bar at (496, 192)
    renderer.fill_rect(496, 192, 128, 8, {40, 40, 40, 255});
    float frac = std::clamp(static_cast<float>(sel->hp) / static_cast<float>(sel->max_hp > 0 ? sel->max_hp : 10), 0.0f, 1.0f);
    int32_t fill_w = static_cast<int32_t>(frac * 128.0f);

    assets::ColorRGBA hp_color = {0, 200, 0, 255}; // Green
    if (sel->hp <= 3) hp_color = {220, 30, 30, 255}; // Red
    else if (sel->hp <= 7) hp_color = {220, 200, 0, 255}; // Yellow

    if (fill_w > 0) renderer.fill_rect(496, 192, fill_w, 8, hp_color);
    renderer.draw_rect(496, 192, 128, 8, {100, 100, 100, 255});

    // Action status: wstatus.bmp at (488, 206)
    renderer.draw_named_sprite("wstatus.bmp", 488, 206);
    std::string status_str = "Idle";
    if (sel->is_drowning) status_str = "Drowning";
    else if (sel->is_airborne) status_str = "Airborne";
    else if (sel->is_stunned) status_str = "Stunned";
    else if (sel->is_underground) status_str = "In Base";
    else if (sel->is_swimming) status_str = "Swimming";
    else if (sel->anim_state == 1 || sel->anim_state == 2) status_str = "Moving";
    else if (sel->anim_state == 3) status_str = "Attacking";
    else if (sel->is_holding) status_str = "Carrying Food";

    renderer.draw_text(status_str, 510, 209, {255, 255, 255, 255});
}

void HUD::render_hatch_panel(IRenderer& renderer, const assets::AssetArchive&, const sim::WorldState& world) {
    // Fill right panel backing with authentic HUD frame green
    renderer.fill_rect(480, 254, 160, 212, assets::ColorRGBA{43, 107, 95, 255});

    // Decorative relief column
    renderer.draw_named_sprite("x521y254.bmp", 521, 254);

    // Hatch button
    std::string btn_name = hatch_button_.is_pressed ? "buthatd.bmp" : "buthatup.bmp";
    renderer.draw_named_sprite(btn_name, hatch_button_.x, hatch_button_.y);
    renderer.draw_named_sprite("labhatch.bmp", hatch_button_.x + 2, hatch_button_.y + 6);

    // Cost text: 200 pts
    assets::ColorRGBA cost_color = hatch_button_.is_enabled ? assets::ColorRGBA{255, 215, 0, 255} : assets::ColorRGBA{130, 130, 130, 255};
    renderer.draw_text("200 pts", 540, 270, cost_color);

    // Egg Pile visualization: compact eggsc.bmp (83x35) at (490, 305)
    uint32_t eggs = (local_player_id_ < world.player_eggs.size()) ? world.player_eggs[local_player_id_] : 0;
    if (eggs > 0) {
        renderer.draw_named_sprite("eggsc.bmp", 490, 305);
    }

    // Numerical egg count
    renderer.draw_text("x " + std::to_string(eggs), 580, 316, {255, 255, 255, 255});
}

void HUD::render_action_buttons(IRenderer& renderer, const assets::AssetArchive&, const sim::WorldState&) {
    static const char* up_names[7] = {
        "butmovu.bmp", "butattu.bmp", "butbomu.bmp", "butfireu.bmp", "butdipu.bmp", "butthfu.bmp", "butcanu.bmp"
    };
    static const char* dn_names[7] = {
        "butmovd.bmp", "butattd.bmp", "butbomd.bmp", "butfireu.bmp", "butdipd.bmp", "butthfd.bmp", "butcand.bmp"
    };
    static const char* lab_names[7] = {
        "labmov.bmp", "labatt.bmp", "labbom.bmp", "labfire.bmp", "labdib.bmp", "labthf.bmp", "labcan.bmp"
    };

    for (size_t i = 0; i < action_buttons_.size(); ++i) {
        const auto& btn = action_buttons_[i];
        const char* name = (btn.is_pressed || btn.is_active) ? dn_names[i] : up_names[i];
        renderer.draw_named_sprite(name, btn.x, btn.y);
        renderer.draw_named_sprite(lab_names[i], btn.x + 4, btn.y + 6);

        // Active selection highlight border
        if (btn.is_active) {
            renderer.draw_rect(btn.x - 1, btn.y - 1, btn.w + 2, btn.h + 2, {255, 215, 0, 255});
        }
    }
}

void HUD::render_news_banner(IRenderer& renderer, const assets::AssetArchive&, const sim::WorldState& world) {
    // Bottom banner background: x17y461.bmp (623x19)
    renderer.draw_named_sprite("x17y461.bmp", BANNER_X, BANNER_Y);

    // Render other 3 players' scores in the 3 pre-cut black boxes:
    // Box 3: x = 105..159, y = 466
    // Box 4: x = 254..308, y = 466
    // Box 5: x = 402..456, y = 466
    std::vector<uint8_t> other_players;
    for (uint8_t p = 0; p < 4; ++p) {
        if (p != local_player_id_) other_players.push_back(p);
    }

    static const char* TEAM_NAMES[4] = {"Black:", "Blue:", "Red:", "Green:"};
    const int32_t box_xs[3] = {105, 254, 402};

    for (size_t i = 0; i < 3 && i < other_players.size(); ++i) {
        uint8_t p = other_players[i];
        int32_t bx = box_xs[i];
        // Team color label to the left of the box
        renderer.draw_text(TEAM_NAMES[p], bx - 52, 466, TEAM_COLORS[p]);
        // Player score inside black box
        int32_t s = (p < world.player_scores.size()) ? world.player_scores[p] : 0;
        renderer.draw_text(std::to_string(s), bx + 6, 466, {255, 255, 255, 255});
    }

    // If there is an active news message in queue
    if (!news_queue_.empty()) {
        const auto& item = news_queue_.front();
        assets::ColorRGBA text_color = {255, 255, 255, 255};
        if (item.is_alarm) {
            bool blink = (alarm_blink_ticks_ / 4) % 2 == 0;
            text_color = blink ? assets::ColorRGBA{255, 50, 50, 255} : assets::ColorRGBA{255, 255, 50, 255};
        }
        renderer.draw_text(item.text, 470, 466, text_color);
    }
}

void HUD::render_quit_dialog(IRenderer& renderer, const assets::AssetArchive&) {
    using assets::ColorRGBA;

    // Dim background overlay
    renderer.fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ColorRGBA{0, 0, 0, 160});

    // Dialog window at (170, 160, 300, 150)
    const int32_t dx = 170, dy = 160, dw = 300, dh = 150;
    renderer.fill_rect(dx, dy, dw, dh, ColorRGBA{192, 192, 192, 255});

    // 3D beveled borders
    renderer.fill_rect(dx, dy, dw, 2, ColorRGBA{255, 255, 255, 255});
    renderer.fill_rect(dx, dy, 2, dh, ColorRGBA{255, 255, 255, 255});
    renderer.fill_rect(dx, dy + dh - 2, dw, 2, ColorRGBA{64, 64, 64, 255});
    renderer.fill_rect(dx + dw - 2, dy, 2, dh, ColorRGBA{64, 64, 64, 255});

    // Title bar
    renderer.fill_rect(dx + 3, dy + 3, dw - 6, 20, ColorRGBA{0, 0, 128, 255});
    renderer.draw_text("Quit Microsoft Ants", dx + 8, dy + 6, ColorRGBA{255, 255, 255, 255});

    // Prompt text
    renderer.draw_text("Are you sure you want to", dx + 38, dy + 42, ColorRGBA{0, 0, 0, 255});
    renderer.draw_text("quit the game?", dx + 80, dy + 60, ColorRGBA{0, 0, 0, 255});

    // Yes button at (205, 255, 49, 24)
    const char* yes_spr = yes_button_.is_pressed ? "yes3.bmp" : (yes_button_.is_active ? "yes2.bmp" : "yes1.bmp");
    renderer.draw_named_sprite(yes_spr, yes_button_.x, yes_button_.y);

    // No button at (345, 255, 49, 24)
    const char* no_spr = no_button_.is_pressed ? "no3.bmp" : (no_button_.is_active ? "no2.bmp" : "no1.bmp");
    renderer.draw_named_sprite(no_spr, no_button_.x, no_button_.y);
}

void HUD::render_quick_help(IRenderer& renderer, const assets::AssetArchive&) {
    using assets::ColorRGBA;

    // Full screen Quick Help overlay
    renderer.fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ColorRGBA{0, 0, 0, 190});

    // Authentic qh1.bmp (257x461) and qh2.bmp (362x463)
    renderer.draw_named_sprite("qh1.bmp", 10, 9);
    renderer.draw_named_sprite("qh2.bmp", 267, 9);
}

void HUD::render_options_dialog(IRenderer& renderer, const assets::AssetArchive&) {
    using assets::ColorRGBA;

    renderer.fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ColorRGBA{0, 0, 0, 160});
    renderer.draw_named_sprite("optcap1.bmp", 124, 150);
}

void HUD::render_marquee_box(IRenderer& renderer) {
    int32_t dx = std::abs(drag_curr_x_ - drag_start_x_);
    int32_t dy = std::abs(drag_curr_y_ - drag_start_y_);
    if (dx <= 4 && dy <= 4) return; // Authentic threshold: only render when dragged > 4px

    int32_t x1 = std::min(drag_start_x_, drag_curr_x_);
    int32_t y1 = std::min(drag_start_y_, drag_curr_y_);
    int32_t x2 = std::max(drag_start_x_, drag_curr_x_);
    int32_t y2 = std::max(drag_start_y_, drag_curr_y_);

    // Clamp box to playfield boundary
    x1 = std::max(PLAYFIELD_X, x1);
    y1 = std::max(PLAYFIELD_Y, y1);
    x2 = std::min(PLAYFIELD_X + PLAYFIELD_WIDTH, x2);
    y2 = std::min(PLAYFIELD_Y + PLAYFIELD_HEIGHT, y2);

    int32_t w = x2 - x1;
    int32_t h = y2 - y1;

    renderer.draw_rect(x1, y1, w, h, {255, 255, 255, 255});
}

// =========================================================================
// Selection Controls
// =========================================================================

void HUD::select_ant(uint32_t ant_id) {
    selected_ant_id_ = ant_id;
    selected_ant_ids_.clear();
    if (ant_id != 0) {
        selected_ant_ids_.push_back(ant_id);
    }
}

void HUD::clear_selection() noexcept {
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
}

bool HUD::is_ant_selected(uint32_t id) const noexcept {
    if (id == 0) return false;
    return std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), id) != selected_ant_ids_.end();
}

void HUD::select_all_friendly(const sim::WorldState& world) {
    selected_ant_ids_.clear();
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        if (ant.player_id == local_player_id_) {
            selected_ant_ids_.push_back(ant.id);
        }
    }
    if (!selected_ant_ids_.empty()) {
        selected_ant_id_ = selected_ant_ids_.front();
    } else {
        selected_ant_id_ = 0;
    }
}

void HUD::select_ants_in_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const sim::WorldState& world) {
    int32_t rx1 = std::min(x1, x2);
    int32_t rx2 = std::max(x1, x2);
    int32_t ry1 = std::min(y1, y2);
    int32_t ry2 = std::max(y1, y2);

    selected_ant_ids_.clear();
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        if (ant.player_id == local_player_id_) {
            int32_t ax = ant.px;
            int32_t ay = ant.py;
            if (ax >= rx1 && ax <= rx2 && ay >= ry1 && ay <= ry2) {
                selected_ant_ids_.push_back(ant.id);
            }
        }
    }
    if (!selected_ant_ids_.empty()) {
        selected_ant_id_ = selected_ant_ids_.front();
    } else {
        selected_ant_id_ = 0;
    }
}

// =========================================================================
// Input Dispatcher
// =========================================================================

bool HUD::handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                            sim::SimulationEngine& sim, ViewportCamera& camera) {
    if (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT) return false;

    // 0. Overlays and Modals intercept clicks first
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT) close_quick_help();
        return true;
    }
    if (show_options_) {
        if (button == SDL_BUTTON_LEFT) close_options();
        return true;
    }
    if (show_quit_dialog_) {
        if (button == SDL_BUTTON_LEFT) {
            if (yes_button_.contains(x, y)) {
                yes_button_.is_pressed = true;
                return true;
            }
            if (no_button_.contains(x, y)) {
                no_button_.is_pressed = true;
                return true;
            }
        }
        return true; // Consume all clicks while dialog is open
    }

    if (button != SDL_BUTTON_LEFT) {
        // Right clicks continue to playfield handling below
    } else {
        // Top Header Buttons
        if (help_button_.contains(x, y)) {
            help_button_.is_pressed = true;
            show_quick_help_ = !show_quick_help_;
            return true;
        }
        if (options_button_.contains(x, y)) {
            options_button_.is_pressed = true;
            show_options_ = !show_options_;
            return true;
        }
        if (quit_button_.contains(x, y)) {
            quit_button_.is_pressed = true;
            open_quit_dialog();
            return true;
        }

        // 1. Check Hatch Button click
        if (hatch_button_.contains(x, y)) {
            if (hatch_button_.is_enabled) {
                hatch_button_.is_pressed = true;
                sim.hatch_ant(local_player_id_, sim::AntType::Worker);
                is_incubating_ = true;
                incubation_timer_ticks_ = 60; // 3 seconds @ 20 Hz
            }
            return true;
        }

        // 2. Check Action Buttons
        for (size_t i = 0; i < action_buttons_.size(); ++i) {
            if (action_buttons_[i].contains(x, y)) {
                if (action_buttons_[i].is_enabled) {
                    action_buttons_[i].is_pressed = true;
                    switch (static_cast<ActionButtonId>(i)) {
                        case ActionButtonId::Move:   set_active_order_mode(sim::OrderType::Move); break;
                        case ActionButtonId::Attack: set_active_order_mode(sim::OrderType::Attack); break;
                        case ActionButtonId::Bomb:   set_active_order_mode(sim::OrderType::PlantBomb); break;
                        case ActionButtonId::Fire:   set_active_order_mode(sim::OrderType::IgniteFire); break;
                        case ActionButtonId::Bridge: set_active_order_mode(sim::OrderType::BuildBridge); break;
                        case ActionButtonId::Thief:  set_active_order_mode(sim::OrderType::InfiltrateAnthill); break;
                        case ActionButtonId::Cancel: cancel_order_mode(); clear_selection(); break;
                        default: break;
                    }
                }
                return true;
            }
        }

        // 3. Check Minimap Radar click
        const int32_t rx = 480, ry = 35;
        const int32_t rw = 119, rh = 91;
        if (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh)) {
            is_radar_dragging_ = true;
            const auto& world = sim.get_world_state();
            if (world.width > 0 && world.height > 0) {
                int32_t tile_x = static_cast<int32_t>((static_cast<float>(x - rx) / rw) * static_cast<float>(world.width));
                int32_t tile_y = static_cast<int32_t>((static_cast<float>(y - ry) / rh) * static_cast<float>(world.height));
                tile_x = std::clamp(tile_x, 0, static_cast<int32_t>(world.width - 1));
                tile_y = std::clamp(tile_y, 0, static_cast<int32_t>(world.height - 1));
                camera.center_on(tile_x * 32, tile_y * 32, world.width, world.height);
            }
            return true;
        }
    }

    // 4. Playfield Interactions
    if (x >= PLAYFIELD_X && x < (PLAYFIELD_X + PLAYFIELD_WIDTH) &&
        y >= PLAYFIELD_Y && y < (PLAYFIELD_Y + PLAYFIELD_HEIGHT)) {

        int32_t world_x = camera.world_x + (x - PLAYFIELD_X);
        int32_t world_y = camera.world_y + (y - PLAYFIELD_Y);

        if (button == 1) { // Left-click
            if (active_order_mode_ != sim::OrderType::None) {
                dispatch_targeted_order(world_x, world_y, sim);
                cancel_order_mode();
                return true;
            }

            // Start possible drag / click selection
            is_dragging_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            drag_curr_x_ = x;
            drag_curr_y_ = y;
            return true;
        } else if (button == 3) { // Right-click: trigger special abilities or cancel armed order
            if (active_order_mode_ != sim::OrderType::None) {
                cancel_order_mode();
            } else {
                dispatch_smart_special_ability(world_x, world_y, sim);
            }
            return true;
        }
    }

    return false;
}

bool HUD::handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                          sim::SimulationEngine& sim, ViewportCamera& camera) {
    help_button_.is_pressed = false;
    options_button_.is_pressed = false;
    quit_button_.is_pressed = false;
    hatch_button_.is_pressed = false;
    for (auto& btn : action_buttons_) btn.is_pressed = false;
    is_radar_dragging_ = false;

    if (show_quit_dialog_) {
        if (yes_button_.is_pressed) {
            yes_button_.is_pressed = false;
            if (yes_button_.contains(x, y)) {
                close_quit_dialog();
                if (on_quit_) on_quit_();
            }
            return true;
        }
        if (no_button_.is_pressed) {
            no_button_.is_pressed = false;
            if (no_button_.contains(x, y)) {
                close_quit_dialog();
            }
            return true;
        }
        return true;
    }

    if (is_dragging_ && button == 1) {
        is_dragging_ = false;
        int32_t dx = std::abs(drag_curr_x_ - drag_start_x_);
        int32_t dy = std::abs(drag_curr_y_ - drag_start_y_);

        const auto& world = sim.get_world_state();

        if (dx > 4 || dy > 4) {
            // Authentic Marquee Box Selection (> 4px drag threshold)
            int32_t x1 = camera.world_x + (std::min(drag_start_x_, drag_curr_x_) - PLAYFIELD_X);
            int32_t y1 = camera.world_y + (std::min(drag_start_y_, drag_curr_y_) - PLAYFIELD_Y);
            int32_t x2 = camera.world_x + (std::max(drag_start_x_, drag_curr_x_) - PLAYFIELD_X);
            int32_t y2 = camera.world_y + (std::max(drag_start_y_, drag_curr_y_) - PLAYFIELD_Y);
            select_ants_in_rect(x1, y1, x2, y2, world);
        } else {
            // Single Click (dx <= 4 && dy <= 4)
            int32_t world_x = camera.world_x + (drag_start_x_ - PLAYFIELD_X);
            int32_t world_y = camera.world_y + (drag_start_y_ - PLAYFIELD_Y);
            int32_t target_tile_x = world_x / 32;
            int32_t target_tile_y = world_y / 32;

            // 1. Check if clicked directly on an ant
            const sim::AntSnapshot* hit_ant = nullptr;
            for (const auto& ant : world.ants) {
                if (ant.hp == 0 || ant.is_drowning) continue;
                if (std::abs(ant.px - world_x) <= 20 && std::abs(ant.py - world_y) <= 20) {
                    hit_ant = &ant;
                    break;
                }
            }

            if (hit_ant) {
                if (hit_ant->player_id == local_player_id_) {
                    // Friendly ant clicked: select single ant
                    select_ant(hit_ant->id);
                } else {
                    // Enemy ant clicked
                    if (!selected_ant_ids_.empty()) {
                        // Issue Attack order against target enemy
                        dispatch_attack_order(hit_ant->id, sim);
                    } else {
                        // Inspect enemy unit
                        select_ant(hit_ant->id);
                    }
                }
                return true;
            }

            // 2. Check if clicked on an anthill base
            const assets::AnthillSpawn* hit_base = nullptr;
            for (const auto& base : world.anthills) {
                if (base.x == target_tile_x && base.y == target_tile_y) {
                    hit_base = &base;
                    break;
                }
            }

            if (hit_base) {
                if (hit_base->team_id == local_player_id_) {
                    // Friendly anthill: return selected friendly ants to base
                    for (uint32_t aid : selected_ant_ids_) {
                        sim::AntOrder order;
                        order.ant_id = aid;
                        order.type = sim::OrderType::ReturnToBase;
                        sim.issue_order(order);
                    }
                } else {
                    // Enemy anthill clicked
                    bool has_thief = false;
                    for (uint32_t aid : selected_ant_ids_) {
                        for (const auto& a : world.ants) {
                            if (a.id == aid && a.type == sim::AntType::Thief) {
                                has_thief = true;
                                sim::AntOrder order;
                                order.ant_id = aid;
                                order.type = sim::OrderType::InfiltrateAnthill;
                                order.target_x = target_tile_x;
                                order.target_y = target_tile_y;
                                sim.issue_order(order);
                            }
                        }
                    }
                    if (!has_thief) {
                        // Propose alliance to target player
                        sim.propose_alliance(local_player_id_, hit_base->team_id);
                    }
                }
                return true;
            }

            // 3. Ground Click: if friendly units selected, issue Move order!
            bool has_friendly = false;
            for (uint32_t aid : selected_ant_ids_) {
                for (const auto& a : world.ants) {
                    if (a.id == aid && a.player_id == local_player_id_) {
                        has_friendly = true;
                        break;
                    }
                }
                if (has_friendly) break;
            }

            if (has_friendly) {
                dispatch_move_order(target_tile_x, target_tile_y, sim);
            } else {
                clear_selection();
            }
        }
        return true;
    }

    return false;
}

bool HUD::handle_mouse_motion(int32_t x, int32_t y,
                              sim::SimulationEngine& sim, ViewportCamera& camera) {
    if (show_quit_dialog_) {
        yes_button_.is_active = yes_button_.contains(x, y);
        no_button_.is_active = no_button_.contains(x, y);
        return true;
    }

    if (is_dragging_) {
        drag_curr_x_ = x;
        drag_curr_y_ = y;
        return true;
    }

    if (is_radar_dragging_) {
        const int32_t rx = 480, ry = 35;
        const int32_t rw = 119, rh = 91;
        const auto& world = sim.get_world_state();
        if (world.width > 0 && world.height > 0) {
            int32_t tile_x = static_cast<int32_t>((static_cast<float>(x - rx) / rw) * static_cast<float>(world.width));
            int32_t tile_y = static_cast<int32_t>((static_cast<float>(y - ry) / rh) * static_cast<float>(world.height));
            tile_x = std::clamp(tile_x, 0, static_cast<int32_t>(world.width - 1));
            tile_y = std::clamp(tile_y, 0, static_cast<int32_t>(world.height - 1));
            camera.center_on(tile_x * 32, tile_y * 32, world.width, world.height);
        }
        return true;
    }

    return false;
}

bool HUD::handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera) {
    // 1. Modals capture keyboard events
    if (show_quit_dialog_) {
        if (key == 'y' || key == 'Y' || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            close_quit_dialog();
            if (on_quit_) on_quit_();
            return true;
        }
        if (key == 'n' || key == 'N' || key == SDLK_ESCAPE) {
            close_quit_dialog();
            return true;
        }
        return true; // Modal blocks all other gameplay keys
    }

    if (show_quick_help_) {
        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_SPACE || key == 'h' || key == 'H') {
            close_quick_help();
            return true;
        }
        return true;
    }

    if (show_options_) {
        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == 'o' || key == 'O') {
            close_options();
            return true;
        }
        return true;
    }

    const auto& world = sim.get_world_state();
    switch (key) {
        case 'm': case 'M': set_active_order_mode(sim::OrderType::Move); return true;
        case 'b': case 'B': set_active_order_mode(sim::OrderType::PlantBomb); return true;
        case 'f': case 'F': set_active_order_mode(sim::OrderType::IgniteFire); return true;
        case 's': case 'S': set_active_order_mode(sim::OrderType::BuildBridge); return true;
        case 't': case 'T': set_active_order_mode(sim::OrderType::InfiltrateAnthill); return true;
        case 'a': case 'A':
            select_all_friendly(world);
            queue_news_message("All Friendly Ants Selected", 40, false);
            return true;
        case 'n': case 'N': { // Next friendly ant
            std::vector<uint32_t> friendly;
            for (const auto& a : world.ants) {
                if (a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                    friendly.push_back(a.id);
                }
            }
            if (!friendly.empty()) {
                auto it = std::find(friendly.begin(), friendly.end(), selected_ant_id_);
                if (it == friendly.end() || ++it == friendly.end()) {
                    select_ant(friendly.front());
                } else {
                    select_ant(*it);
                }
                for (const auto& a : world.ants) {
                    if (a.id == selected_ant_id_) {
                        camera.center_on(a.px, a.py, world.width, world.height);
                        break;
                    }
                }
            }
            return true;
        }
        case 'p': case 'P': { // Previous friendly ant
            std::vector<uint32_t> friendly;
            for (const auto& a : world.ants) {
                if (a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                    friendly.push_back(a.id);
                }
            }
            if (!friendly.empty()) {
                auto it = std::find(friendly.begin(), friendly.end(), selected_ant_id_);
                if (it == friendly.end() || it == friendly.begin()) {
                    select_ant(friendly.back());
                } else {
                    select_ant(*(--it));
                }
                for (const auto& a : world.ants) {
                    if (a.id == selected_ant_id_) {
                        camera.center_on(a.px, a.py, world.width, world.height);
                        break;
                    }
                }
            }
            return true;
        }
        case 'h': case 'H': {
            const auto* base = sim.grid().find_anthill(local_player_id_);
            if (base) {
                camera.center_on(base->x * 32, base->y * 32, world.width, world.height);
                queue_news_message("Camera Centered on Home Anthill", 40, false);
            }
            return true;
        }
        case 27: // Escape: open quit dialog, or cancel armed order if active
            if (active_order_mode_ != sim::OrderType::None || !selected_ant_ids_.empty()) {
                cancel_order_mode();
                clear_selection();
            } else {
                open_quit_dialog();
            }
            return true;
        case 'c': case 'C':
            cancel_order_mode();
            clear_selection();
            return true;
        case ' ': // Space: Center on selected ant or home base
            if (selected_ant_id_ != 0) {
                for (const auto& a : world.ants) {
                    if (a.id == selected_ant_id_) {
                        camera.center_on(a.px, a.py, world.width, world.height);
                        return true;
                    }
                }
            }
            {
                const auto* base = sim.grid().find_anthill(local_player_id_);
                if (base) {
                    camera.center_on(base->x * 32, base->y * 32, world.width, world.height);
                }
            }
            return true;
        default: break;
    }
    return false;
}

void HUD::dispatch_targeted_order(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim) {
    std::vector<uint32_t> targets = selected_ant_ids_;
    if (targets.empty() && selected_ant_id_ != 0) {
        targets.push_back(selected_ant_id_);
    }
    if (targets.empty()) return;

    int32_t target_tile_x = world_x / 32;
    int32_t target_tile_y = world_y / 32;

    for (uint32_t aid : targets) {
        sim::AntOrder order;
        order.ant_id = aid;
        order.type = active_order_mode_;
        order.target_x = target_tile_x;
        order.target_y = target_tile_y;
        sim.issue_order(order);
    }
}

void HUD::dispatch_move_order(int32_t target_tile_x, int32_t target_tile_y, sim::SimulationEngine& sim) {
    std::vector<uint32_t> targets = selected_ant_ids_;
    if (targets.empty() && selected_ant_id_ != 0) {
        targets.push_back(selected_ant_id_);
    }

    for (uint32_t aid : targets) {
        sim::AntOrder order;
        order.ant_id = aid;
        order.type = sim::OrderType::Move;
        order.target_x = target_tile_x;
        order.target_y = target_tile_y;
        sim.issue_order(order);
    }
}

void HUD::dispatch_attack_order(uint32_t target_enemy_id, sim::SimulationEngine& sim) {
    std::vector<uint32_t> targets = selected_ant_ids_;
    if (targets.empty() && selected_ant_id_ != 0) {
        targets.push_back(selected_ant_id_);
    }

    const auto& world = sim.get_world_state();
    int32_t target_x = 0;
    int32_t target_y = 0;
    for (const auto& a : world.ants) {
        if (a.id == target_enemy_id) {
            target_x = a.tile_x;
            target_y = a.tile_y;
            break;
        }
    }

    for (uint32_t aid : targets) {
        sim::AntOrder order;
        order.ant_id = aid;
        order.type = sim::OrderType::Attack;
        order.target_x = target_x;
        order.target_y = target_y;
        order.target_entity_id = static_cast<int32_t>(target_enemy_id);
        sim.issue_order(order);
    }
}

void HUD::dispatch_smart_special_ability(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim) {
    std::vector<uint32_t> targets = selected_ant_ids_;
    if (targets.empty() && selected_ant_id_ != 0) {
        targets.push_back(selected_ant_id_);
    }
    if (targets.empty()) return;

    int32_t target_tile_x = world_x / 32;
    int32_t target_tile_y = world_y / 32;

    const auto& world = sim.get_world_state();

    for (uint32_t aid : targets) {
        const sim::AntSnapshot* sel = nullptr;
        for (const auto& a : world.ants) {
            if (a.id == aid) { sel = &a; break; }
        }
        if (!sel || sel->player_id != local_player_id_ || sel->hp == 0 || sel->is_drowning) continue;

        sim::AntOrder order;
        order.ant_id = aid;
        order.target_x = target_tile_x;
        order.target_y = target_tile_y;

        switch (sel->type) {
            case sim::AntType::Bomber:
                if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                    order.type = sim::OrderType::DefuseBomb;
                } else {
                    order.type = sim::OrderType::PlantBomb;
                }
                break;
            case sim::AntType::Fire:
                if (sim.has_fire_at({target_tile_x, target_tile_y})) {
                    order.type = sim::OrderType::ExtinguishFire;
                } else {
                    order.type = sim::OrderType::IgniteFire;
                }
                break;
            case sim::AntType::Swimmer:
                order.type = sim::OrderType::BuildBridge;
                break;
            case sim::AntType::Thief:
                order.type = sim::OrderType::InfiltrateAnthill;
                break;
            case sim::AntType::Combat:
                order.type = sim::OrderType::Attack;
                break;
            case sim::AntType::Worker:
            default:
                order.type = sim::OrderType::Move;
                break;
        }

        sim.issue_order(order);
    }
}

} // namespace ants::app

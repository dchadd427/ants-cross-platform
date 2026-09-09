#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>

namespace ants::app {

namespace {

// Authentic Team color RGB palettes
constexpr assets::ColorRGBA TEAM_COLORS[4] = {
    {83, 147, 43, 255},   // 0: Green
    {251, 51, 91, 255},   // 1: Red
    {119, 175, 239, 255}, // 2: Blue
    {79, 87, 111, 255}    // 3: Black
};

// Authentic Team Score Box Background Colors from Ants.exe VA 0x100DA90..0x100DAD0:
// Team 0 (Green in remake, Team 3 in Ants.exe): COLORREF 0x002F4307 -> RGB { 7, 67, 47 }
// Team 1 (Red in remake, Team 2 in Ants.exe):   COLORREF 0x00000077 -> RGB { 119, 0, 0 }
// Team 2 (Blue in remake, Team 1 in Ants.exe):  COLORREF 0x006B272B -> RGB { 43, 39, 107 }
// Team 3 (Black in remake, Team 0 in Ants.exe): COLORREF 0x003B2727 -> RGB { 39, 39, 59 }
constexpr assets::ColorRGBA SCORE_BG_COLORS[4] = {
    {  7,  67,  47, 255}, // 0: Green
    {119,   0,   0, 255}, // 1: Red
    { 43,  39, 107, 255}, // 2: Blue
    { 39,  39,  59, 255}  // 3: Black
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
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    active_order_mode_ = sim::OrderType::None;
    is_dragging_ = false;
    is_radar_dragging_ = false;
    incubation_timer_ticks_ = 0;
    is_incubating_ = false;
    news_queue_.clear();
    chat_log_.clear();
    chat_scroll_offset_ = 0;
    {
        constexpr size_t MAX_CHARS = 27; // Expanded from 21 to utilize full 133px width of wchat.bmp
        std::string start_msg = "[0:00] News Flash: Game started! Go get that food!";
        size_t s_idx = 0;
        while (s_idx < start_msg.length()) {
            if (start_msg.length() - s_idx <= MAX_CHARS) {
                chat_log_.push_back(start_msg.substr(s_idx));
                break;
            }
            size_t split = start_msg.rfind(' ', s_idx + MAX_CHARS);
            if (split == std::string::npos || split <= s_idx) {
                split = s_idx + MAX_CHARS;
            }
            chat_log_.push_back(start_msg.substr(s_idx, split - s_idx));
            s_idx = split;
            while (s_idx < start_msg.length() && start_msg[s_idx] == ' ') {
                ++s_idx;
            }
        }
    }

    // Configure Top Header Buttons (x0y0.bmp)
    help_button_ = {476, 7, 46, 23, 0, 0, 0, false, true, false};
    options_button_ = {525, 7, 52, 23, 0, 0, 0, false, true, false};
    quit_button_ = {579, 7, 46, 23, 0, 0, 0, false, true, false};

    // Configure Authentic Primary Action Pedestal (Move) at (488, 140, 53, 86)
    move_pedestal_button_ = {488, 140, 53, 86, 0, 0, 0, false, true, false};

    // Configure Authentic Secondary Ability Pedestal at (544, 140, 53, 86)
    ability_pedestal_button_ = {544, 140, 53, 86, 0, 0, 0, false, true, false};

    // Configure Authentic Stop Button at (602, 176, 34, 50)
    stop_button_ = {602, 176, 34, 50, 0, 0, 0, false, true, false};

    // Configure Authentic Send-to Toggle Button at (532, 443, 44, 24)
    send_to_button_ = {532, 443, 44, 24, 0, 0, 0, false, true, false};
    // Configure Authentic Team Toggle Button at (579, 443, 46, 24)
    team_button_    = {579, 443, 46, 24, 0, 0, 0, false, true, false};
    send_to_all_ = true;
    is_on_team_ = false;
    chat_input_.clear();
    chat_input_focused_ = false;
    cursor_blink_ticks_ = 0;

    // Configure Quit Confirmation Dialog Buttons
    yes_button_ = {184, 264, 49, 24, 0, 0, 0, false, true, false};
    no_button_  = {296, 264, 49, 24, 0, 0, 0, false, true, false};

    show_quit_dialog_ = false;
    show_quick_help_ = false;
    show_options_ = false;

    // Configure Hatch Button matching Primary Pedestal at (488, 140, 53, 86)
    hatch_button_.x = 488;
    hatch_button_.y = 140;
    hatch_button_.w = 53;
    hatch_button_.h = 86;
    hatch_button_.sprite_up = 2683;    // buthatup.bmp
    hatch_button_.sprite_down = 2684;  // buthatd.bmp
    hatch_button_.sprite_label = 2682; // labhatch.bmp

    // Configure Authentic Team Up Button matching Primary Pedestal on Enemy Base Card
    team_up_button_.x = 488;
    team_up_button_.y = 140;
    team_up_button_.w = 53;
    team_up_button_.h = 86;
    team_up_button_.sprite_up = 2576;    // butdipu.bmp
    team_up_button_.sprite_down = 2587;  // butdipd.bmp
    team_up_button_.sprite_label = 2575; // labdib.bmp
    team_up_button_.is_enabled = true;
    team_up_button_.is_pressed = false;
    team_up_button_.is_active = false;

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

    queue_news_message("Ants Remake", 200, false);
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

    // 5. Cursor blink ticks and alliance team status
    cursor_blink_ticks_ += delta_ticks;
    is_on_team_ = (local_player_id_ < world.player_alliances.size() &&
                   world.player_alliances[local_player_id_] < sim::MAX_PLAYERS &&
                   world.player_alliances[local_player_id_] != local_player_id_);
    if (!is_on_team_) {
        send_to_all_ = true;
    }
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
    constexpr size_t MAX_CHARS_PER_LINE = 27;
    size_t start = 0;
    while (start < msg.length()) {
        if (msg.length() - start <= MAX_CHARS_PER_LINE) {
            chat_log_.push_back(msg.substr(start));
            break;
        }
        size_t split = msg.rfind(' ', start + MAX_CHARS_PER_LINE);
        if (split == std::string::npos || split <= start) {
            split = start + MAX_CHARS_PER_LINE;
        }
        chat_log_.push_back(msg.substr(start, split - start));
        start = split;
        while (start < msg.length() && msg[start] == ' ') {
            ++start;
        }
    }
    while (chat_log_.size() > 50) {
        chat_log_.pop_front();
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
    renderer.set_hud_team(local_player_id_);

    // Base backing fill to ensure zero gaps between modular HUD tiles (Authentic index 10)
    static const assets::ColorRGBA hud_bg_colors[4] = {
        { 43, 104,  95, 255},  // Green (Player 0) - Authentic index 10
        {143,  35,  99, 255},  // Red   (Player 1) - Authentic index 10
        { 51,  87, 163, 255},  // Blue  (Player 2) - Authentic index 10
        { 87,  87,  91, 255}   // Black (Player 3) - Authentic index 10
    };
    renderer.fill_rect(480, 22, 160, 458, hud_bg_colors[local_player_id_ % 4]);

    // 1. Playfield Frame Borders
    renderer.draw_named_sprite("x0y22.bmp", 0, 22);
    renderer.draw_named_sprite("x458y35.bmp", 458, 35);
    renderer.draw_named_sprite("x458y22.bmp", 458, 22);

    // 2. Right Panel Modules (Authentic Ants reconstruction)
    // 2.1 Minimap Radar at (480, 35..126) and decorative bezel x599y35.bmp at (599, 35)
    render_radar(renderer, assets, world, camera);

    // 2.2 Card background x480y126.bmp at (480, 126..254) with embossed "Status"
    renderer.draw_named_sprite("x480y126.bmp", 480, 126);

    // Resolve selected ant
    const sim::AntSnapshot* sel_ant = nullptr;
    if (selected_ant_id_ != 0) {
        for (const auto& a : world.ants) {
            if (a.id == selected_ant_id_) {
                sel_ant = &a;
                break;
            }
        }
    }

    if (selected_base_team_id_ >= 0) {
        if (selected_base_team_id_ == local_player_id_) {
            // Authentic Home Anthill Hatch Interface (Replaces unit action buttons)
            bool hatch_down = hatch_button_.is_pressed;
            renderer.draw_named_sprite(hatch_down ? "butdown.bmp" : "butup.bmp", 488, 155);
            renderer.draw_named_sprite(hatch_down ? "buthatd.bmp" : "buthatup.bmp", 503, hatch_down ? 166 : 164);
            renderer.draw_named_sprite("labhatch.bmp", 497, 140);

            // 3x3 Egg Grid in middle slot (authentic egg tray)
            uint32_t eggs = (local_player_id_ < world.player_eggs.size()) ? world.player_eggs[local_player_id_] : 0;
            for (uint32_t i = 0; i < std::min(eggs, 9u); ++i) {
                int32_t ex = 553 + static_cast<int32_t>(i % 3) * 14;
                int32_t ey = 164 + static_cast<int32_t>(i / 3) * 18;
                renderer.draw_named_sprite("egg.bmp", ex, ey);
            }

            // Authentic Stop Button on right
            renderer.draw_named_sprite("labcan.bmp", 603, 176);
            renderer.draw_named_sprite(stop_button_.is_pressed ? "butcand.bmp" : "butcanu.bmp", 602, 192);

            // Recessed status box wstatus.bmp (143x14) at (480, 253)
            renderer.draw_named_sprite("wstatus.bmp", 480, 253);
            // Empty status box matching authentic appearance
        } else {
            // Authentic Enemy Base Selection Interface
            bool is_allied = (local_player_id_ < world.player_alliances.size()) &&
                             (world.player_alliances[local_player_id_] == selected_base_team_id_);

            // Pedestal 1 (488, 155): TeamUp option
            bool team_down = team_up_button_.is_pressed;
            renderer.draw_named_sprite(team_down ? "butdown.bmp" : "butup.bmp", 488, 155);
            renderer.draw_named_sprite(team_down ? "butdipd.bmp" : "butdipu.bmp", 503, team_down ? 166 : 164);
            renderer.draw_named_sprite("labdib.bmp", 491, 140);

            // Recessed status box wstatus.bmp (143x14) at (480, 253)
            renderer.draw_named_sprite("wstatus.bmp", 480, 253);
            if (is_allied) {
                renderer.draw_text("Allied Colony", 486, 253, {100, 255, 100, 255});
            }
        }
    } else {
        // Pedestal 1: Move (Always present)
        bool ped1_down = move_pedestal_button_.is_pressed || (active_order_mode_ == sim::OrderType::Move);
        renderer.draw_named_sprite(ped1_down ? "butdown.bmp" : "butup.bmp", 488, 155);
        renderer.draw_named_sprite(ped1_down ? "butmovd.bmp" : "butmovu.bmp", 503, ped1_down ? 166 : 164);
        renderer.draw_named_sprite("labmov.bmp", 497, 140);

        // Pedestal 2: Class-Specific Ability Pedestal
        if (sel_ant) {
            if (sel_ant->type == sim::AntType::Swimmer) {
                bool ped2_down = ability_pedestal_button_.is_pressed || (active_order_mode_ == sim::OrderType::BuildBridge);
                renderer.draw_named_sprite(ped2_down ? "butdown.bmp" : "butup.bmp", 544, 155);
                renderer.draw_named_sprite(ped2_down ? "swimd.bmp" : "swimup.bmp", 556, ped2_down ? 167 : 165);
                renderer.draw_named_sprite("labswim.bmp", 551, 140);
            } else if (sel_ant->type == sim::AntType::Fire) {
                bool ped2_down = ability_pedestal_button_.is_pressed || (active_order_mode_ == sim::OrderType::IgniteFire);
                renderer.draw_named_sprite(ped2_down ? "butdown.bmp" : "butup.bmp", 544, 155);
                renderer.draw_named_sprite("butfireu.bmp", 555, ped2_down ? 166 : 164);
                renderer.draw_named_sprite("labfire.bmp", 546, 140);
            } else if (sel_ant->type == sim::AntType::Combat) {
                bool ped2_down = ability_pedestal_button_.is_pressed || (active_order_mode_ == sim::OrderType::Attack);
                renderer.draw_named_sprite(ped2_down ? "butdown.bmp" : "butup.bmp", 544, 155);
                renderer.draw_named_sprite(ped2_down ? "butattd.bmp" : "butattu.bmp", 553, ped2_down ? 167 : 165);
                renderer.draw_named_sprite("labatt.bmp", 550, 140);
            } else if (sel_ant->type == sim::AntType::Bomber) {
                bool ped2_down = ability_pedestal_button_.is_pressed || (active_order_mode_ == sim::OrderType::PlantBomb);
                renderer.draw_named_sprite(ped2_down ? "butdown.bmp" : "butup.bmp", 544, 155);
                renderer.draw_named_sprite(ped2_down ? "butbomd.bmp" : "butbomu.bmp", 554, ped2_down ? 165 : 163);
                renderer.draw_named_sprite("labbom.bmp", 553, 140);
            } else if (sel_ant->type == sim::AntType::Thief) {
                bool ped2_down = ability_pedestal_button_.is_pressed || (active_order_mode_ == sim::OrderType::InfiltrateAnthill);
                renderer.draw_named_sprite(ped2_down ? "butdown.bmp" : "butup.bmp", 544, 155);
                renderer.draw_named_sprite(ped2_down ? "butthfd.bmp" : "butthfu.bmp", 556, ped2_down ? 166 : 164);
                renderer.draw_named_sprite("labthf.bmp", 554, 140);
            }
        }

        // Stop circular button at (602, 192) with "Stop" label at (603, 176)
        renderer.draw_named_sprite("labcan.bmp", 603, 176);
        renderer.draw_named_sprite(stop_button_.is_pressed ? "butcand.bmp" : "butcanu.bmp", 602, 192);

        // Golden Lunchbox Indicator: Displayed above Stop button at (598, 133) ONLY when carrying food
        if (sel_ant && sel_ant->is_holding) {
            renderer.draw_named_sprite("lunchicon.bmp", 598, 133);
        }

        // Recessed status box wstatus.bmp (143x14) at (480, 253)
        renderer.draw_named_sprite("wstatus.bmp", 480, 253);
        std::string status_text = "Ready.";
        if (sel_ant) {
            if (sel_ant->is_drowning) status_text = "Drowning!";
            else if (sel_ant->is_underground) status_text = "In base.";
            else if (sel_ant->is_holding) status_text = "Holds pick up...";
            else if (sel_ant->anim_state == 1 || sel_ant->anim_state == 2) status_text = "On my way.";
            else if (sel_ant->anim_state == 3) status_text = "In combat!";
            else status_text = "Waiting for orders.";
        }
        renderer.draw_text(status_text, 486, 253, {175, 110, 215, 255});
    }

    // 2.3 Lower Panel: Always render Chat Section
    // Cursive embossed Chat header at (480, 266)
    renderer.draw_named_sprite("x480y266.bmp", 480, 266);

    // White chat history log wchat.bmp (143x103) at (479, 298)
    renderer.draw_named_sprite("wchat.bmp", 479, 298);
    int32_t cty = 301;
    constexpr int32_t VISIBLE_LINES = 7;
    int32_t total_lines = static_cast<int32_t>(chat_log_.size());
    int32_t max_scroll = std::max(0, total_lines - VISIBLE_LINES);
    chat_scroll_offset_ = std::clamp(chat_scroll_offset_, 0, max_scroll);
    int32_t start_cidx = (total_lines > VISIBLE_LINES) ? (total_lines - VISIBLE_LINES - chat_scroll_offset_) : 0;
    int32_t end_cidx = std::min(total_lines, start_cidx + VISIBLE_LINES);
    for (int32_t i = start_cidx; i < end_cidx; ++i) {
        renderer.draw_text(chat_log_[static_cast<size_t>(i)], 484, cty, {20, 50, 40, 255});
        cty += 14;
    }

    // Ant relief horizontal divider bar x480y400.bmp (141x24) at (480, 400)
    renderer.draw_named_sprite("x480y400.bmp", 480, 400);

    // Chat text input box wtype.bmp (143x14) at (479, 423)
    renderer.draw_named_sprite("wtype.bmp", 479, 423);
    std::string input_display = chat_input_;
    if (input_display.length() > 25) {
        input_display = input_display.substr(input_display.length() - 25);
    }
    if (chat_input_focused_) {
        if ((cursor_blink_ticks_ / 15) % 2 == 0) {
            input_display += "_";
        }
    } else {
        if (input_display.empty()) {
            input_display = "_";
        }
    }
    renderer.draw_text(input_display, 484, 423, {20, 50, 40, 255});

    // Bottom bar x480y466.bmp (160x25) at (480, 436) containing "Send to:" and [All] / [Team] buttons
    renderer.draw_named_sprite("x480y466.bmp", 480, 436);
    if (!is_on_team_) {
        // In FFA or non-team mode, only [All] is active/shown
        renderer.draw_named_sprite("butalld.bmp", 532, 443);
    } else {
        // When on a team, display both [All] and [Team] with active state
        if (send_to_all_) {
            renderer.draw_named_sprite("butalld.bmp", 532, 443);
            renderer.draw_named_sprite("butteamu.bmp", 579, 443);
        } else {
            renderer.draw_named_sprite("butallu.bmp", 532, 443);
            renderer.draw_named_sprite("butteamd.bmp", 579, 443);
        }
    }

    // Vertical right border strip x521y254.bmp (19x182) placed at x=621, y=254 (seals right screen edge)
    renderer.draw_named_sprite("x521y254.bmp", 621, 254);

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

    std::string time_str = std::to_string(mm) + ":" + (ss < 10 ? "0" : "") + std::to_string(ss);
    int32_t time_w = renderer.get_text_width(time_str, FontSize::Medium);
    int32_t time_h = renderer.get_text_height(FontSize::Medium);
    int32_t time_x = 61 + (68 - time_w) / 2;
    int32_t time_y = 4 + (14 - time_h) / 2;
    renderer.draw_text(time_str, time_x, time_y, {255, 255, 255, 255}, FontSize::Medium);

    // Box 2 (Top-Right above Playfield): Local player's own score in box at (402..455, 4..17)
    // Fill the box with authentic score background color (Ants.exe VA 0x100DA90)
    renderer.fill_rect(402, 4, 54, 14, SCORE_BG_COLORS[local_player_id_ % 4]);
    int32_t my_score = (local_player_id_ < world.player_scores.size()) ? world.player_scores[local_player_id_] : 0;
    std::string my_score_str = std::to_string(my_score);
    int32_t score_text_w = renderer.get_text_width(my_score_str, FontSize::Small);
    int32_t score_text_h = renderer.get_text_height(FontSize::Small);
    int32_t score_text_x = 453 - score_text_w;
    int32_t score_text_y = 4 + (14 - score_text_h) / 2;
    renderer.draw_text(my_score_str, score_text_x, score_text_y, {255, 255, 255, 255}, FontSize::Small);

    // Player label to the left of the top score box in [312..399, 4..17] (Ants.exe VA 0x100E218)
    static const char* TEAM_NAMES[4] = {"Green", "Red", "Blue", "Black"};
    std::string p_name = player_name_.empty() ? TEAM_NAMES[local_player_id_ % 4] : player_name_;
    if (p_name.size() > 15) p_name = p_name.substr(0, 15);
    std::string my_label = p_name + ":";
    int32_t label_w = renderer.get_text_width(my_label, FontSize::Small);
    int32_t label_h = renderer.get_text_height(FontSize::Small);
    int32_t label_x = std::max(312, 399 - label_w);
    int32_t label_y = 4 + (14 - label_h) / 2;
    renderer.draw_text(my_label, label_x, label_y, {255, 255, 255, 255}, FontSize::Small);

    // Top Header Buttons feedback (Help at 476, 7; Options at 525, 7; Quit at 579, 7)
    // Note: In unpressed state, Help/Options/Quit are already pre-rendered inside x0y0.bmp.
    if (help_button_.is_pressed || show_quick_help_) {
        renderer.draw_named_sprite("buthelpd.bmp", 476, 7);
    }
    if (options_button_.is_pressed || show_options_) {
        renderer.draw_named_sprite("butoptd.bmp", 525, 7);
    }
    if (quit_button_.is_pressed || show_quit_dialog_) {
        renderer.draw_named_sprite("butquitd.bmp", 579, 7);
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
            float fty = static_cast<float>(ty);
            int32_t py = ry + static_cast<int32_t>(fty * scale_y);
            int32_t ph = std::max(1, static_cast<int32_t>((fty + 1.0f) * scale_y) - static_cast<int32_t>(fty * scale_y));
            for (uint32_t tx = 0; tx < world.width; ++tx) {
                const auto& cell = world.cells[ty * world.width + tx];
                float ftx = static_cast<float>(tx);
                int32_t px = rx + static_cast<int32_t>(ftx * scale_x);
                int32_t pw = std::max(1, static_cast<int32_t>((ftx + 1.0f) * scale_x) - static_cast<int32_t>(ftx * scale_x));
                assets::ColorRGBA col{155, 115, 108, 255}; // Walkable ground (authentic dirt tan)
                if (cell.is_food) {
                    col = {226, 147, 27, 255}; // Food morsel: Golden orange matching reference
                } else if (cell.terrain_type == sim::TERRAIN_WATER) {
                    col = {58, 67, 192, 255}; // Water: Vibrant blue
                } else if (cell.terrain_type == sim::TERRAIN_OBSTACLE || cell.is_obstacle_overlay) {
                    col = {47, 81, 48, 255}; // Obstacle / Grass (green)
                } else if (cell.is_mud) {
                    col = {95, 90, 85, 255}; // Mud path: Slate gray matching original
                } else if (cell.has_completed_bridge() || cell.has_partial_bridge()) {
                    col = {160, 110, 60, 255}; // Bridge
                } else if (cell.has_fire()) {
                    col = {240, 80, 20, 255}; // Fire
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
        if (selected_base_team_id_ >= 0) {
            // Anthill Base Selection Card
            static const char* hill_sprites[4] = { "GHILL_s.bmp", "rhill_s.bmp", "blhill_s.bmp", "bkhill_s.bmp" };
            static const char* hill_names[4] = { "Green Anthill", "Red Anthill", "Blue Anthill", "Black Anthill" };
            uint8_t tid = static_cast<uint8_t>(selected_base_team_id_ % 4);

            renderer.draw_named_sprite("wtype.bmp", 488, 130);
            renderer.draw_text(hill_names[tid], 505, 133, TEAM_COLORS[tid]);

            // Anthill Portrait
            renderer.draw_named_sprite(hill_sprites[tid], 530, 145);

            // Eggs & Score status
            uint32_t eggs = (tid < world.player_eggs.size()) ? world.player_eggs[tid] : 0;
            int32_t score = (tid < world.player_scores.size()) ? world.player_scores[tid] : 0;

            renderer.draw_named_sprite("wstatus.bmp", 488, 206);
            std::string base_status = (tid == local_player_id_) ? "Home Colony" : "Colony Base";
            renderer.draw_text(base_status, 510, 206, {255, 255, 255, 255});
            renderer.draw_text("Eggs: " + std::to_string(eggs) + "  Food: " + std::to_string(score), 496, 192, {255, 215, 0, 255});
            return;
        }

        // No selection: Render authentic chat log / news box
        renderer.draw_named_sprite("wchat.bmp", 488, 135);
        int32_t ty = 140;
        constexpr int32_t TOP_VISIBLE_LINES = 7;
        int32_t total_l = static_cast<int32_t>(chat_log_.size());
        int32_t max_top_scroll = std::max(0, total_l - TOP_VISIBLE_LINES);
        int32_t top_scroll = std::clamp(chat_scroll_offset_, 0, max_top_scroll);
        int32_t start_idx = (total_l > TOP_VISIBLE_LINES) ? (total_l - TOP_VISIBLE_LINES - top_scroll) : 0;
        int32_t end_idx = std::min(total_l, start_idx + TOP_VISIBLE_LINES);
        for (int32_t i = start_idx; i < end_idx; ++i) {
            renderer.draw_text(chat_log_[static_cast<size_t>(i)], 494, ty, {20, 50, 40, 255});
            ty += 13;
        }
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

    renderer.draw_text(status_str, 510, 206, {255, 255, 255, 255});
}

void HUD::render_hatch_panel(IRenderer& renderer, const assets::AssetArchive&, const sim::WorldState& world) {
    // Fill right panel backing with authentic HUD frame color
    static const assets::ColorRGBA hud_bg_colors[4] = {
        {43, 107, 95, 255},  // Green (Player 0)
        {115, 35, 35, 255},  // Red (Player 1)
        {35, 65, 115, 255},  // Blue (Player 2)
        {48, 48, 52, 255}    // Black (Player 3)
    };
    renderer.fill_rect(480, 254, 160, 212, hud_bg_colors[local_player_id_ % 4]);

    // Status label at (480, 253)
    renderer.draw_named_sprite("wstatus.bmp", 480, 253);
    renderer.draw_text("Home Colony.", 486, 253, {175, 110, 215, 255});

    // Decorative relief column
    renderer.draw_named_sprite("x521y254.bmp", 521, 275);

    // Hatch label and button
    std::string btn_name = hatch_button_.is_pressed ? "buthatd.bmp" : "buthatup.bmp";
    renderer.draw_named_sprite("labhatch.bmp", 492, 282);
    renderer.draw_named_sprite(btn_name, 532, 275);

    // Cost text: 200 pts
    assets::ColorRGBA cost_color = hatch_button_.is_enabled ? assets::ColorRGBA{255, 215, 0, 255} : assets::ColorRGBA{130, 130, 130, 255};
    renderer.draw_text("200 pts", 562, 282, cost_color);

    // Egg display: authentic egg.bmp (12x16)
    uint32_t eggs = (local_player_id_ < world.player_eggs.size()) ? world.player_eggs[local_player_id_] : 0;
    renderer.draw_named_sprite("egg.bmp", 495, 320);
    renderer.draw_text("Eggs: " + std::to_string(eggs), 518, 322, {255, 255, 255, 255});

    // Divider and bottom border
    renderer.draw_named_sprite("x480y400.bmp", 480, 400);
    renderer.draw_named_sprite("x480y466.bmp", 480, 436);
    renderer.draw_named_sprite("x521y254.bmp", 621, 254);
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

    // Render other 3 players' scores and labels in the 3 pre-cut slots (Ants.exe VA 0x10021B8):
    // Slot 0: label [5..101], score [105..158], y = 464..477
    // Slot 1: label [163..251], score [254..307], y = 464..477
    // Slot 2: label [312..399], score [402..455], y = 464..477
    std::vector<uint8_t> other_players;
    for (uint8_t p = 0; p < 4; ++p) {
        if (p != local_player_id_) other_players.push_back(p);
    }

    struct ScoreSlot {
        int32_t label_left;
        int32_t label_right;
        int32_t box_x;
    };
    const ScoreSlot slots[3] = {
        {5, 101, 105},
        {163, 251, 254},
        {312, 399, 402}
    };

    static const char* TEAM_NAMES[4] = {"Green", "Red", "Blue", "Black"};

    for (size_t i = 0; i < 3 && i < other_players.size(); ++i) {
        uint8_t p = other_players[i];
        const auto& slot = slots[i];

        // Player/team label right-aligned before score box (Ants.exe VA 0x100E218)
        std::string name = (p < 4) ? TEAM_NAMES[p] : "AI";
        if (name.size() > 15) name = name.substr(0, 15);
        std::string p_label = name + ":";
        int32_t label_w = renderer.get_text_width(p_label, FontSize::Small);
        int32_t label_h = renderer.get_text_height(FontSize::Small);
        int32_t label_x = std::max(slot.label_left, slot.label_right - label_w);
        int32_t label_y = 464 + (14 - label_h) / 2;
        renderer.draw_text(p_label, label_x, label_y, {255, 255, 255, 255}, FontSize::Small);

        // Score box fill with authentic background color
        renderer.fill_rect(slot.box_x, 464, 54, 14, SCORE_BG_COLORS[p % 4]);

        // Player score inside box (right-justified)
        int32_t s = (p < world.player_scores.size()) ? world.player_scores[p] : 0;
        std::string s_str = std::to_string(s);
        int32_t s_text_w = renderer.get_text_width(s_str, FontSize::Small);
        int32_t s_text_h = renderer.get_text_height(FontSize::Small);
        int32_t s_text_x = slot.box_x + 51 - s_text_w;
        int32_t s_text_y = 464 + (14 - s_text_h) / 2;
        renderer.draw_text(s_str, s_text_x, s_text_y, {255, 255, 255, 255}, FontSize::Small);
    }
}

void HUD::render_quit_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // Dim background overlay
    renderer.fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ColorRGBA{0, 0, 0, 160});

    const int32_t dx = 104;
    const int32_t dy = 104;

    // Authentic std_dialg composite dialog (20 frame elements from Table 4)
    const auto* anim = assets.find_animation("std_dialg");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, dx + fr.dx, dy + fr.dy);
        }
    } else {
        renderer.fill_rect(dx, dy, 320, 224, ColorRGBA{219, 75, 19, 255});
    }

    // Centered prompt text: "Do you really want to quit?"
    std::string prompt = "Do you really want to quit?";
    int32_t text_w = renderer.get_text_width(prompt, FontSize::Small);
    int32_t text_h = renderer.get_text_height(FontSize::Small);
    int32_t text_x = dx + (320 - text_w) / 2;
    int32_t text_y = dy + 88 + (10 - text_h) / 2;
    renderer.draw_text(prompt, text_x, text_y, ColorRGBA{27, 41, 30, 255}, FontSize::Small);

    // Yes button at (184, 264)
    const char* yes_spr = yes_button_.is_pressed ? "yes3.bmp" : (yes_button_.is_active ? "yes2.bmp" : "yes1.bmp");
    renderer.draw_named_sprite(yes_spr, yes_button_.x, yes_button_.y);

    // No button at (296, 264)
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

void HUD::render_options_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // 1. Authentic crimson stipple dither background outside card
    for (int32_t dy = 0; dy < SCREEN_HEIGHT; dy += 200) {
        for (int32_t dx = 0; dx < SCREEN_WIDTH; dx += 200) {
            renderer.draw_named_sprite("dith200.bmp", dx, dy);
        }
    }

    // 2. Terracotta orange solid backing for card
    renderer.fill_rect(18, 20, 442, 440, ColorRGBA{219, 75, 19, 255});

    // 3. Authentic op_screen composite dialog (210 frame elements from Table 4)
    const auto* anim = assets.find_animation("op_screen");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, fr.dx, fr.dy);
        }
    } else {
        renderer.draw_named_sprite("optcap1.bmp", 44, 39);
    }

    // 4. Sliders (Sound Volume, Music Volume, Map Scroll Rate)
    // Track span: x = 188..373 (usable travel range 185px)
    int32_t sfx_thumb_x = 188 + static_cast<int32_t>(std::clamp(sfx_volume_, 0.0f, 1.0f) * 185.0f);
    int32_t music_thumb_x = 188 + static_cast<int32_t>(std::clamp(music_volume_, 0.0f, 1.0f) * 185.0f);
    int32_t scroll_thumb_x = 188 + static_cast<int32_t>(std::clamp(scroll_rate_, 0.0f, 1.0f) * 185.0f);

    renderer.draw_named_sprite("slidd.bmp", sfx_thumb_x, 179);
    renderer.draw_named_sprite("slidd.bmp", music_thumb_x, 216);
    renderer.draw_named_sprite("slidd.bmp", scroll_thumb_x, 253);

    // 5. Chat Toggle Buttons (ON at 96, 287 / OFF at 146, 288)
    if (chat_enabled_) {
        renderer.draw_named_sprite("optond.bmp", 96, 287);
        renderer.draw_named_sprite("dbutoffu.bmp", 146, 288);
    } else {
        renderer.draw_named_sprite("dbutonu.bmp", 96, 287);
        renderer.draw_named_sprite("optoffd.bmp", 146, 288);
    }

    // 6. Quick Help Toggle Buttons (ON at 358, 287 / OFF at 408, 288)
    if (quick_help_enabled_) {
        renderer.draw_named_sprite("optond.bmp", 358, 287);
        renderer.draw_named_sprite("dbutoffu.bmp", 408, 288);
    } else {
        renderer.draw_named_sprite("dbutonu.bmp", 358, 287);
        renderer.draw_named_sprite("optoffd.bmp", 408, 288);
    }

    // 7. Quick Chat Key Edit Fields Text
    for (size_t i = 0; i < 4; ++i) {
        int32_t qx = (i == 0 || i == 1) ? 93 : 302;
        int32_t qy = (i == 0 || i == 2) ? 371 : 402;
        std::string txt = quick_chat_keys_[i];
        if (active_quick_chat_edit_ == static_cast<int>(i)) {
            if ((cursor_blink_ticks_ / 15) % 2 == 0) {
                txt += "_";
            }
        }
        renderer.draw_text(txt, qx, qy, ColorRGBA{255, 255, 255, 255});
    }

    // 8. Return to Game Button (breturn1/2.bmp at 355, 427)
    if (opt_return_button_pressed_) {
        renderer.draw_named_sprite("breturn2.bmp", 355, 427);
    } else {
        renderer.draw_named_sprite("breturn1.bmp", 355, 427);
    }
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

void HUD::select_ant(uint32_t ant_id, bool is_multi) {
    selected_ant_id_ = ant_id;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    is_multi_select_mode_ = is_multi;
    if (ant_id != 0) {
        selected_ant_ids_.push_back(ant_id);
    }
}

void HUD::select_base(int32_t team_id) noexcept {
    selected_base_team_id_ = team_id;
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    is_multi_select_mode_ = false;
}

void HUD::clear_selection() noexcept {
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    is_multi_select_mode_ = false;
}

bool HUD::is_ant_selected(uint32_t id) const noexcept {
    if (id == 0) return false;
    return std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), id) != selected_ant_ids_.end();
}

bool HUD::has_friendly_selected(const sim::WorldState& world) const noexcept {
    for (uint32_t aid : selected_ant_ids_) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                return true;
            }
        }
    }
    if (selected_ant_id_ != 0) {
        for (const auto& a : world.ants) {
            if (a.id == selected_ant_id_ && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                return true;
            }
        }
    }
    return false;
}

void HUD::select_all_friendly(const sim::WorldState& world) {
    selected_base_team_id_ = -1;
    selected_ant_ids_.clear();
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        if (ant.player_id == local_player_id_) {
            selected_ant_ids_.push_back(ant.id);
        }
    }
    is_multi_select_mode_ = (selected_ant_ids_.size() > 1);
    if (!selected_ant_ids_.empty()) {
        selected_ant_id_ = selected_ant_ids_.front();
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_ && ant.player_id == local_player_id_) {
                play_sfx(sim::get_ready_voice_sound(ant.type, voice_variant_++));
                break;
            }
        }
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
    is_multi_select_mode_ = true;
    if (!selected_ant_ids_.empty()) {
        selected_ant_id_ = selected_ant_ids_.front();
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_ && ant.player_id == local_player_id_) {
                play_sfx(sim::get_ready_voice_sound(ant.type, voice_variant_++));
                break;
            }
        }
    } else {
        selected_ant_id_ = 0;
    }
}

// =========================================================================
// Input Dispatcher
// =========================================================================

bool HUD::handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                            sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod) {
    if (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT) return false;

    // 0. Overlays and Modals intercept clicks first
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT) close_quick_help();
        return true;
    }
    if (show_options_) {
        if (button == SDL_BUTTON_LEFT) {
            // Return to Game button (breturn1/2.bmp at 355, 427, size 98x26)
            if ((x >= 345 && x <= 455 && y >= 423 && y <= 455) ||
                (x >= 402 && x <= 455 && y >= 425 && y <= 455)) {
                opt_return_button_pressed_ = true;
                opt_ok_button_pressed_ = true;
                return true;
            }
            // Chat ON (96..142, 287..311)
            if (x >= 96 && x <= 142 && y >= 287 && y <= 311) {
                chat_enabled_ = true;
                return true;
            }
            // Chat OFF (146..192, 287..311)
            if (x >= 146 && x <= 192 && y >= 287 && y <= 311) {
                chat_enabled_ = false;
                return true;
            }
            // Quick Help ON (358..404, 287..311)
            if (x >= 358 && x <= 404 && y >= 287 && y <= 311) {
                quick_help_enabled_ = true;
                return true;
            }
            // Quick Help OFF (408..454, 287..311)
            if (x >= 408 && x <= 454 && y >= 287 && y <= 311) {
                quick_help_enabled_ = false;
                return true;
            }
            // Sound FX slider (x 188..422, y 174..198)
            if (x >= 188 && x <= 422 && y >= 174 && y <= 198) {
                active_slider_dragging_ = 0;
                sfx_volume_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
                if (on_sfx_volume_) on_sfx_volume_(sfx_volume_);
                return true;
            }
            // Music slider (x 188..422, y 211..235)
            if (x >= 188 && x <= 422 && y >= 211 && y <= 235) {
                active_slider_dragging_ = 1;
                music_volume_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
                if (on_music_volume_) on_music_volume_(music_volume_);
                return true;
            }
            // Scroll rate slider (x 188..422, y 248..272)
            if (x >= 188 && x <= 422 && y >= 248 && y <= 272) {
                active_slider_dragging_ = 2;
                scroll_rate_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
                if (on_scroll_rate_) on_scroll_rate_(scroll_rate_);
                return true;
            }
            // Quick Chat Key Edit Fields
            // F9 (89..235, 368..387)
            if (x >= 89 && x <= 235 && y >= 368 && y <= 387) {
                active_quick_chat_edit_ = 0;
                return true;
            }
            // F10 (89..235, 399..418)
            if (x >= 89 && x <= 235 && y >= 399 && y <= 418) {
                active_quick_chat_edit_ = 1;
                return true;
            }
            // F11 (298..444, 368..387)
            if (x >= 298 && x <= 444 && y >= 368 && y <= 387) {
                active_quick_chat_edit_ = 2;
                return true;
            }
            // F12 (298..444, 399..418)
            if (x >= 298 && x <= 444 && y >= 399 && y <= 418) {
                active_quick_chat_edit_ = 3;
                return true;
            }
            // Clicking elsewhere inside dialog clears active quick chat edit field
            active_quick_chat_edit_ = -1;

            // Clicking outside dialog closes options
            if (x < 18 || x > 465 || y < 20 || y > 465) {
                close_options();
                return true;
            }
        }
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

    if (button == SDL_BUTTON_LEFT) {
        // Unfocus chat input if clicking outside wtype.bmp (479..622, 423..437)
        if (chat_input_focused_ && !(x >= 479 && x < (479 + 143) && y >= 423 && y < (423 + 14))) {
            unfocus_chat();
        }
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

        // 0. Check Authentic Base Selection Buttons
        if (selected_base_team_id_ >= 0) {
            if (selected_base_team_id_ == local_player_id_) {
                if (hatch_button_.contains(x, y) || move_pedestal_button_.contains(x, y)) {
                    hatch_button_.is_pressed = true;
                    int32_t score = (local_player_id_ < sim.get_world_state().player_scores.size())
                                        ? sim.get_world_state().player_scores[local_player_id_] : 0;
                    uint32_t eggs = (local_player_id_ < sim.get_world_state().player_eggs.size())
                                        ? sim.get_world_state().player_eggs[local_player_id_] : 0;
                    if (score >= 200 && eggs > 0) {
                        sim.hatch_ant(local_player_id_, sim::AntType::Worker);
                        is_incubating_ = true;
                        incubation_timer_ticks_ = 60;
                    }
                    return true;
                }
                if (stop_button_.contains(x, y)) {
                    stop_button_.is_pressed = true;
                    clear_selection();
                    return true;
                }
            } else {
                if (team_up_button_.contains(x, y) || move_pedestal_button_.contains(x, y)) {
                    team_up_button_.is_pressed = true;
                    uint8_t target_team = static_cast<uint8_t>(selected_base_team_id_);
                    const auto& ws = sim.get_world_state();
                    bool is_allied = (local_player_id_ < ws.player_alliances.size()) &&
                                     (ws.player_alliances[local_player_id_] == target_team);
                    if (is_allied) {
                        sim.break_alliance(local_player_id_, target_team);
                    } else {
                        sim.propose_alliance(local_player_id_, target_team);
                    }
                    return true;
                }
            }
        }

        // 1. Check Authentic Primary Action Pedestal (Move) click
        if (move_pedestal_button_.contains(x, y)) {
            move_pedestal_button_.is_pressed = true;
            if (active_order_mode_ == sim::OrderType::Move) {
                cancel_order_mode();
            } else {
                set_active_order_mode(sim::OrderType::Move);
            }
            return true;
        }

        // 1b. Check Authentic Secondary Action Pedestal (Class-Specific Ability) click
        if (ability_pedestal_button_.contains(x, y) && !selected_ant_ids_.empty()) {
            ability_pedestal_button_.is_pressed = true;
            const auto& sel_u = sim.get_unit(selected_ant_ids_[0]);
            sim::OrderType ability_order = sim::OrderType::None;
            switch (sel_u.type) {
                case sim::AntType::Swimmer: ability_order = sim::OrderType::BuildBridge; break;
                case sim::AntType::Fire:    ability_order = sim::OrderType::IgniteFire; break;
                case sim::AntType::Combat:  ability_order = sim::OrderType::Attack; break;
                case sim::AntType::Bomber:  ability_order = sim::OrderType::PlantBomb; break;
                case sim::AntType::Thief:   ability_order = sim::OrderType::InfiltrateAnthill; break;
                default: break;
            }
            if (ability_order != sim::OrderType::None) {
                if (active_order_mode_ == ability_order) {
                    cancel_order_mode();
                } else {
                    set_active_order_mode(ability_order);
                }
            }
            return true;
        }

        // 2. Check Authentic Stop Button click
        if (stop_button_.contains(x, y)) {
            stop_button_.is_pressed = true;
            cancel_order_mode();
            play_sfx(sim::SoundID::AntStop);
            for (uint32_t aid : selected_ant_ids_) {
                const auto& u = sim.get_unit(aid);
                if (u.player_id != local_player_id_) continue;
                if (u.state == sim::UnitState::BuildingBridge || u.state == sim::UnitState::DemolishingBridge) {
                    continue; // Swimmer cannot be interrupted while building/demolishing a bridge!
                }
                if (u.is_transforming() || u.on_powerup || (sim.grid().in_bounds(u.pos) && sim.grid().has_powerup_at(u.pos))) {
                    sim.interrupt_transformation(aid);
                } else {
                    sim::AntOrder order;
                    order.ant_id = aid;
                    order.type = sim::OrderType::Move;
                    order.target_x = u.pos.x;
                    order.target_y = u.pos.y;
                    sim.issue_order(order);
                }
            }
            return true;
        }

        // 3. Check Chat Text Input box click (wtype.bmp at 479, 423, 143x14)
        if (x >= 479 && x < (479 + 143) && y >= 423 && y < (423 + 14)) {
            focus_chat();
            return true;
        }

        // 4. Check Authentic [All] Button click (532, 443, 44x24)
        if (send_to_button_.contains(x, y)) {
            send_to_button_.is_pressed = true;
            send_to_all_ = true;
            return true;
        }

        // 5. Check Authentic [Team] Button click (579, 443, 46x24)
        if (is_on_team_ && team_button_.contains(x, y)) {
            team_button_.is_pressed = true;
            send_to_all_ = false;
            return true;
        }

        // 4. Check Hatch Button click
        if (hatch_button_.contains(x, y)) {
            if (hatch_button_.is_enabled) {
                hatch_button_.is_pressed = true;
                sim.hatch_ant(local_player_id_, sim::AntType::Worker);
                is_incubating_ = true;
                incubation_timer_ticks_ = 60; // 3 seconds @ 20 Hz
            }
            return true;
        }

        // 5. Check Action Buttons (for compatibility)
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
        } else if (button == 3) { // Right-click: standard RTS context command
            if (active_order_mode_ != sim::OrderType::None) {
                cancel_order_mode();
                return true;
            }

            int32_t target_tile_x = world_x / 32;
            int32_t target_tile_y = world_y / 32;

            // 1. Check if clicked on an enemy ant
            const sim::AntSnapshot* enemy_target = nullptr;
            int32_t best_enemy_dist_sq = INT32_MAX;
            const auto& world = sim.get_world_state();
            for (const auto& ant : world.ants) {
                if (ant.hp == 0 || ant.is_drowning) continue;
                if (ant.player_id != local_player_id_ && !sim.stats_manager().are_allies(local_player_id_, ant.player_id)) {
                    bool in_bbox = (std::abs(ant.px - world_x) <= 18 &&
                                    world_y >= ant.py - 24 && world_y <= ant.py + 18);
                    bool on_tile = (ant.tile_x == target_tile_x && ant.tile_y == target_tile_y);
                    if (in_bbox || on_tile) {
                        int32_t d_sq = (ant.px - world_x) * (ant.px - world_x) + (ant.py - world_y) * (ant.py - world_y);
                        if (d_sq < best_enemy_dist_sq) {
                            best_enemy_dist_sq = d_sq;
                            enemy_target = &ant;
                        }
                    }
                }
            }
            if (enemy_target) {
                if (!has_friendly_selected(world)) {
                    play_sfx(sim::SoundID::AntStop);
                    return true;
                }
                if (enemy_target->on_powerup || (enemy_target->type == sim::AntType::Swimmer && enemy_target->is_swimming)) {
                    play_sfx(sim::SoundID::AntStop);
                    return true;
                }
                dispatch_attack_order(enemy_target->id, sim);
                return true;
            }

            // 2. Check if clicked on an anthill base (4x4 footprint)
            const assets::AnthillSpawn* target_base = nullptr;
            for (const auto& base : world.anthills) {
                if (target_tile_x >= base.x && target_tile_x < base.x + 4 &&
                    target_tile_y >= base.y && target_tile_y < base.y + 4) {
                    target_base = &base;
                    break;
                }
            }
            bool shift_held = ((mod & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0) ||
                              ((static_cast<uint16_t>(SDL_GetModState()) & KMOD_SHIFT) != 0);

            if (target_base) {
                if (target_base->team_id == local_player_id_) {
                    play_sfx(sim::SoundID::GeneralCommand);
                    for (uint32_t aid : selected_ant_ids_) {
                        for (const auto& a : world.ants) {
                            if (a.id == aid && a.player_id == local_player_id_) {
                                sim::AntOrder order;
                                order.ant_id = aid;
                                order.type = sim::OrderType::ReturnToBase;
                                sim.issue_order(order);
                                break;
                            }
                        }
                    }
                } else {
                    dispatch_smart_special_ability(world_x, world_y, sim, shift_held);
                }
                return true;
            }

            // 3. Dispatch unit smart ability (Move for Worker/Queen, PlantBomb for Bomber, BuildBridge for Swimmer, IgniteFire for Fire, etc.)
            dispatch_smart_special_ability(world_x, world_y, sim, shift_held);
            return true;
        }
    }

    return false;
}

bool HUD::handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                          sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod) {
    help_button_.is_pressed = false;
    options_button_.is_pressed = false;
    quit_button_.is_pressed = false;
    move_pedestal_button_.is_pressed = false;
    ability_pedestal_button_.is_pressed = false;
    stop_button_.is_pressed = false;
    send_to_button_.is_pressed = false;
    team_button_.is_pressed = false;
    hatch_button_.is_pressed = false;
    team_up_button_.is_pressed = false;
    for (auto& btn : action_buttons_) btn.is_pressed = false;
    is_radar_dragging_ = false;
    if (show_options_) {
        if (opt_ok_button_pressed_ || opt_return_button_pressed_) {
            opt_ok_button_pressed_ = false;
            opt_return_button_pressed_ = false;
            close_options();
        }
        active_slider_dragging_ = -1;
        return true;
    }

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
        bool shift_held = ((mod & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0) ||
                          ((static_cast<uint16_t>(SDL_GetModState()) & KMOD_SHIFT) != 0);

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

            // 1. Check if clicked directly on an ant (sprite bounding box OR tile match)
            const sim::AntSnapshot* hit_ant = nullptr;
            int32_t best_ant_dist_sq = INT32_MAX;
            for (const auto& ant : world.ants) {
                if (ant.hp == 0 || ant.is_drowning) continue;
                bool in_bbox = (std::abs(ant.px - world_x) <= 18 &&
                                world_y >= ant.py - 24 && world_y <= ant.py + 18);
                bool on_tile = (ant.tile_x == target_tile_x && ant.tile_y == target_tile_y);
                if (in_bbox || on_tile) {
                    int32_t d_sq = (ant.px - world_x) * (ant.px - world_x) + (ant.py - world_y) * (ant.py - world_y);
                    if (d_sq < best_ant_dist_sq) {
                        best_ant_dist_sq = d_sq;
                        hit_ant = &ant;
                    }
                }
            }

            if (hit_ant) {
                selected_base_team_id_ = -1;
                if (hit_ant->player_id == local_player_id_) {
                    // Friendly ant clicked: select single ant (shift_held enables multi-select mode)
                    select_ant(hit_ant->id, shift_held);
                    play_sfx(sim::get_ready_voice_sound(hit_ant->type, voice_variant_++));
                } else {
                    // Enemy or allied ant clicked
                    bool is_ally = sim.stats_manager().are_allies(local_player_id_, hit_ant->player_id);
                    if (has_friendly_selected(world) && !is_ally && !hit_ant->on_powerup && !(hit_ant->type == sim::AntType::Swimmer && hit_ant->is_swimming)) {
                        // Issue Attack order against target enemy for selected friendly ants
                        dispatch_attack_order(hit_ant->id, sim);
                    } else {
                        // Cannot select or command enemy/allied ants!
                        if (has_friendly_selected(world)) {
                            play_sfx(sim::SoundID::AntStop);
                        } else {
                            clear_selection();
                        }
                    }
                }
                return true;
            }

            // 2. Check if clicked on an anthill base (4x4 footprint)
            const assets::AnthillSpawn* hit_base = nullptr;
            for (const auto& base : world.anthills) {
                if (target_tile_x >= base.x && target_tile_x < base.x + 4 &&
                    target_tile_y >= base.y && target_tile_y < base.y + 4) {
                    hit_base = &base;
                    break;
                }
            }

            if (hit_base) {
                if (has_friendly_selected(world)) {
                    if (hit_base->team_id == local_player_id_) {
                        // Friendly anthill: return selected friendly ants to base
                        for (uint32_t aid : selected_ant_ids_) {
                            for (const auto& a : world.ants) {
                                if (a.id == aid && a.player_id == local_player_id_) {
                                    sim::AntOrder order;
                                    order.ant_id = aid;
                                    order.type = sim::OrderType::ReturnToBase;
                                    sim.issue_order(order);
                                    break;
                                }
                            }
                        }
                    } else {
                        // Enemy anthill clicked
                        bool has_thief = false;
                        for (uint32_t aid : selected_ant_ids_) {
                            for (const auto& a : world.ants) {
                                if (a.id == aid && a.player_id == local_player_id_ && a.type == sim::AntType::Thief) {
                                    has_thief = true;
                                    sim::AntOrder order;
                                    order.ant_id = aid;
                                    order.type = sim::OrderType::InfiltrateAnthill;
                                    order.target_x = target_tile_x;
                                    order.target_y = target_tile_y;
                                    sim.issue_order(order);
                                    break;
                                }
                            }
                        }
                        if (!has_thief) {
                            clear_selection();
                            select_base(static_cast<int32_t>(hit_base->team_id));
                        }
                    }
                } else {
                    // No units selected: Select the base!
                    select_base(static_cast<int32_t>(hit_base->team_id));
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
                if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                    bool is_single_bomber = (!is_multi_select() && !shift_held && selected_ant_ids_.size() == 1);
                    if (is_single_bomber) {
                        const auto& sel_u = sim.get_unit(selected_ant_ids_[0]);
                        if (sel_u.type == sim::AntType::Bomber) {
                            sim::AntOrder order;
                            order.ant_id = selected_ant_ids_[0];
                            order.type = sim::OrderType::DefuseBomb;
                            order.target_x = target_tile_x;
                            order.target_y = target_tile_y;
                            sim.issue_order(order);
                            play_sfx(sim::get_ability_voice_sound(sim::AntType::Bomber));
                        } else {
                            dispatch_move_order(target_tile_x, target_tile_y, sim, true /* allow_friendly_bomb */);
                        }
                    } else {
                        // Multi-select or Shift-held or non-bomber: hit bomb!
                        dispatch_move_order(target_tile_x, target_tile_y, sim, true /* allow_friendly_bomb */);
                    }
                } else {
                    dispatch_move_order(target_tile_x, target_tile_y, sim, false);
                }
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
    if (show_options_) {
        if (active_slider_dragging_ == 0) {
            sfx_volume_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
            if (on_sfx_volume_) on_sfx_volume_(sfx_volume_);
        } else if (active_slider_dragging_ == 1) {
            music_volume_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
            if (on_music_volume_) on_music_volume_(music_volume_);
        } else if (active_slider_dragging_ == 2) {
            scroll_rate_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
            if (on_scroll_rate_) on_scroll_rate_(scroll_rate_);
        }
        return true;
    }

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

bool HUD::handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod) {
    // 1. Modals capture keyboard events
    if (show_quit_dialog_) {
        if (key == 'y' || key == 'Y' || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            close_quit_dialog();
            if (on_quit_) on_quit_();
            return true;
        }
        if (key == 'n' || key == 'N' || key == SDLK_ESCAPE || key == 27) {
            close_quit_dialog();
            return true;
        }
        return true; // Modal blocks all other gameplay keys
    }

    if (show_quick_help_) {
        if (key == SDLK_ESCAPE || key == 27 || key == SDLK_RETURN || key == SDLK_SPACE || key == 'h' || key == 'H') {
            close_quick_help();
            return true;
        }
        return true;
    }

    if (show_options_) {
        if (active_quick_chat_edit_ >= 0 && active_quick_chat_edit_ < 4) {
            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                active_quick_chat_edit_ = -1;
                return true;
            }
            if (key == SDLK_BACKSPACE) {
                if (!quick_chat_keys_[active_quick_chat_edit_].empty()) {
                    quick_chat_keys_[active_quick_chat_edit_].pop_back();
                }
                return true;
            }
        }
        if (key == SDLK_ESCAPE || key == 27 || key == SDLK_RETURN || key == 'o' || key == 'O') {
            close_options();
            return true;
        }
        return true;
    }

    bool ctrl = (mod & KMOD_CTRL) || (mod & KMOD_GUI);

    // 2. Chat Input Active (Ctrl/Cmd modifier bypasses chat input to allow commands)
    if (chat_input_focused_ && !ctrl) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            send_chat_message();
            chat_input_focused_ = false;
            return true;
        }
        if (key == SDLK_ESCAPE) {
            chat_input_.clear();
            chat_input_focused_ = false;
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            if (!chat_input_.empty()) {
                chat_input_.pop_back();
            }
            return true;
        }
        if (key == SDLK_PAGEUP) {
            scroll_chat_up(4);
            return true;
        }
        if (key == SDLK_PAGEDOWN) {
            scroll_chat_down(4);
            return true;
        }
        if (key == SDLK_UP) {
            scroll_chat_up(1);
            return true;
        }
        if (key == SDLK_DOWN) {
            scroll_chat_down(1);
            return true;
        }
        // Printable ASCII typing fallback (for direct key events / unit tests)
        if (key >= 32 && key <= 126) {
            if (chat_input_.size() < 120) {
                chat_input_.push_back(static_cast<char>(key));
            }
            return true;
        }
        return true; // While chat is focused, swallow all other keys
    }

    // Quick Chat Broadcast keys F9..F12 (only in gameplay)
    if (!show_options_) {
        if (key == SDLK_F9) {
            trigger_quick_chat(0);
            return true;
        }
        if (key == SDLK_F10) {
            trigger_quick_chat(1);
            return true;
        }
        if (key == SDLK_F11) {
            trigger_quick_chat(2);
            return true;
        }
        if (key == SDLK_F12) {
            trigger_quick_chat(3);
            return true;
        }
    }

    if (key == SDLK_PAGEUP) {
        scroll_chat_up(4);
        return true;
    }
    if (key == SDLK_PAGEDOWN) {
        scroll_chat_down(4);
        return true;
    }

    // 3. Not focused: Enter focuses chat
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        focus_chat();
        return true;
    }

    // 4. Hotkeys requiring Control or Command modifier
    const auto& world = sim.get_world_state();

    if (ctrl) {
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
                if (selected_base_team_id_ == local_player_id_) {
                    int32_t score = (local_player_id_ < world.player_scores.size()) ? world.player_scores[local_player_id_] : 0;
                    uint32_t eggs = (local_player_id_ < world.player_eggs.size()) ? world.player_eggs[local_player_id_] : 0;
                    if (score >= 200 && eggs > 0) {
                        sim.hatch_ant(local_player_id_, sim::AntType::Worker);
                        is_incubating_ = true;
                        incubation_timer_ticks_ = 60;
                    }
                } else {
                    select_base(local_player_id_);
                    queue_news_message("Home Anthill Selected", 40, false);
                }
                return true;
            }
            case 'c': case 'C':
                cancel_order_mode();
                clear_selection();
                return true;
            default: break;
        }
    }

    if (key == 27 || key == SDLK_ESCAPE) {
        if (show_quit_dialog_) {
            close_quit_dialog();
        } else if (show_options_) {
            close_options();
        } else if (show_quick_help_) {
            close_quick_help();
        } else {
            open_quit_dialog();
        }
        return true;
    }

    if (key == ' ' || key == SDLK_SPACE) {
        if (selected_ant_id_ != 0) {
            for (const auto& a : world.ants) {
                if (a.id == selected_ant_id_) {
                    camera.center_on(a.px, a.py, world.width, world.height);
                    return true;
                }
            }
        }
        const auto* base = sim.grid().find_anthill(local_player_id_);
        if (base) {
            camera.center_on(base->x * 32 + 64, base->y * 32 + 64, world.width, world.height);
        }
        return true;
    }

    return false;
}

void HUD::handle_text_input(const std::string& text) {
    if (text.empty()) return;
    if (show_options_) {
        if (active_quick_chat_edit_ >= 0 && active_quick_chat_edit_ < 4) {
            for (char c : text) {
                if (c >= 32 && c <= 126) {
                    if (quick_chat_keys_[active_quick_chat_edit_].size() < 40) {
                        quick_chat_keys_[active_quick_chat_edit_].push_back(c);
                    }
                }
            }
        }
        return;
    }
    if (!chat_input_focused_) {
        chat_input_focused_ = true;
    }
    for (char c : text) {
        if (c >= 32 && c <= 126) {
            if (chat_input_.size() < 120) {
                chat_input_.push_back(c);
            }
        }
    }
}

void HUD::send_chat_message() {
    if (chat_input_.empty()) return;

    std::string sender = player_name_.empty() ? "Player" : player_name_;
    add_chat_entry(sender, chat_input_, is_on_team_ && !send_to_all_);
    chat_input_.clear();
    chat_scroll_offset_ = 0;
}

void HUD::trigger_quick_chat(size_t index) {
    if (index >= 4) return;
    if (quick_chat_keys_[index].empty()) return;
    std::string sender = player_name_.empty() ? "Player" : player_name_;
    add_chat_entry(sender, quick_chat_keys_[index], is_on_team_ && !send_to_all_);
}

void HUD::add_chat_entry(const std::string& sender, const std::string& message, bool team_only) {
    std::string prefix = sender + (team_only ? " (Team): " : ": ");
    std::string full_msg = prefix + message;

    // Word-wrap into lines of at most 27 characters to fit inside wchat.bmp (143px)
    constexpr size_t MAX_CHARS_PER_LINE = 27;
    size_t start = 0;
    while (start < full_msg.length()) {
        if (full_msg.length() - start <= MAX_CHARS_PER_LINE) {
            chat_log_.push_back(full_msg.substr(start));
            break;
        }
        size_t split = full_msg.rfind(' ', start + MAX_CHARS_PER_LINE);
        if (split == std::string::npos || split <= start) {
            split = start + MAX_CHARS_PER_LINE;
        }
        chat_log_.push_back(full_msg.substr(start, split - start));
        start = split;
        while (start < full_msg.length() && full_msg[start] == ' ') {
            ++start;
        }
    }

    while (chat_log_.size() > 50) {
        chat_log_.pop_front();
    }
}

void HUD::scroll_chat_up(int32_t lines) noexcept {
    constexpr int32_t VISIBLE_LINES = 7;
    int32_t total_lines = static_cast<int32_t>(chat_log_.size());
    int32_t max_scroll = std::max(0, total_lines - VISIBLE_LINES);
    chat_scroll_offset_ = std::clamp(chat_scroll_offset_ + lines, 0, max_scroll);
}

void HUD::scroll_chat_down(int32_t lines) noexcept {
    constexpr int32_t VISIBLE_LINES = 7;
    int32_t total_lines = static_cast<int32_t>(chat_log_.size());
    int32_t max_scroll = std::max(0, total_lines - VISIBLE_LINES);
    chat_scroll_offset_ = std::clamp(chat_scroll_offset_ - lines, 0, max_scroll);
}

void HUD::handle_mouse_wheel(int32_t screen_x, int32_t screen_y, int32_t wheel_y) {
    if (wheel_y == 0) return;
    bool in_lower_chat = (screen_x >= 475 && screen_x <= 635 && screen_y >= 265 && screen_y <= 445);
    bool in_upper_chat = (selected_ant_ids_.empty() && selected_ant_id_ == 0 && selected_base_team_id_ < 0 &&
                          screen_x >= 475 && screen_x <= 635 && screen_y >= 130 && screen_y <= 245);

    if (in_lower_chat || in_upper_chat) {
        if (wheel_y > 0) {
            scroll_chat_up(wheel_y);
        } else {
            scroll_chat_down(-wheel_y);
        }
    }
}

void HUD::dispatch_targeted_order(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim) {
    int32_t target_tile_x = world_x / 32;
    int32_t target_tile_y = world_y / 32;

    const auto& world = sim.get_world_state();
    std::vector<uint32_t> raw_targets = selected_ant_ids_;
    if (raw_targets.empty() && selected_ant_id_ != 0) {
        raw_targets.push_back(selected_ant_id_);
    }
    std::vector<uint32_t> targets;
    for (uint32_t aid : raw_targets) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                targets.push_back(aid);
                break;
            }
        }
    }
    if (targets.empty()) return;

    if (active_order_mode_ == sim::OrderType::Move) {
        bool has_bomb = sim.has_bomb_at({target_tile_x, target_tile_y});
        bool allow_bomb = false;
        if (has_bomb) {
            bool is_single_bomber = (targets.size() == 1 && !is_multi_select());
            if (is_single_bomber) {
                const auto& sel_u = sim.get_unit(targets[0]);
                if (sel_u.type == sim::AntType::Bomber) {
                    sim::AntOrder order;
                    order.ant_id = targets[0];
                    order.type = sim::OrderType::DefuseBomb;
                    order.target_x = target_tile_x;
                    order.target_y = target_tile_y;
                    sim.issue_order(order);
                    play_sfx(sim::get_ability_voice_sound(sim::AntType::Bomber));
                    return;
                }
            }
            allow_bomb = true;
        }
        dispatch_move_order(target_tile_x, target_tile_y, sim, allow_bomb);
        return;
    }

    if (active_order_mode_ == sim::OrderType::BuildBridge) {
        const auto& grid = sim.grid();
        if (grid.in_bounds({target_tile_x, target_tile_y})) {
            const auto& cell = grid.get_cell({target_tile_x, target_tile_y});
            if (cell.has_completed_bridge() || cell.has_partial_bridge()) {
                for (uint32_t aid : targets) {
                    sim::AntOrder order;
                    order.ant_id = aid;
                    order.type = sim::OrderType::DemolishBridge;
                    order.target_x = target_tile_x;
                    order.target_y = target_tile_y;
                    sim.issue_order(order);
                }
                play_sfx(sim::get_ability_voice_sound(sim::AntType::Swimmer));
                return;
            }
        }
    }

    for (uint32_t aid : targets) {
        sim::AntOrder order;
        order.ant_id = aid;
        order.type = active_order_mode_;
        order.target_x = target_tile_x;
        order.target_y = target_tile_y;
        sim.issue_order(order);
    }
}

void HUD::dispatch_move_order(int32_t target_tile_x, int32_t target_tile_y, sim::SimulationEngine& sim, bool allow_friendly_bomb) {
    const auto& world = sim.get_world_state();
    std::vector<uint32_t> raw_targets = selected_ant_ids_;
    if (raw_targets.empty() && selected_ant_id_ != 0) {
        raw_targets.push_back(selected_ant_id_);
    }
    std::vector<uint32_t> targets;
    for (uint32_t aid : raw_targets) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                targets.push_back(aid);
                break;
            }
        }
    }
    if (targets.empty()) return;

    // Play authentic Move / Go voice clip for the primary selected friendly unit (if destination is passable)
    bool dest_is_passable = sim.grid().in_bounds(target_tile_x, target_tile_y) &&
                            sim.grid().get_cell(static_cast<uint32_t>(target_tile_x), static_cast<uint32_t>(target_tile_y)).is_passable();
    if (dest_is_passable) {
        for (uint32_t aid : targets) {
            for (const auto& a : world.ants) {
                if (a.id == aid && a.player_id == local_player_id_) {
                    play_sfx(sim::get_move_voice_sound(a.type, voice_variant_++));
                    goto move_voice_done;
                }
            }
        }
    }
move_voice_done:

    if (targets.size() == 1) {
        sim::AntOrder order;
        order.ant_id = targets[0];
        order.type = sim::OrderType::Move;
        order.target_x = target_tile_x;
        order.target_y = target_tile_y;
        order.allow_friendly_bomb = allow_friendly_bomb;
        sim.issue_order(order);
        return;
    }

    if (allow_friendly_bomb && sim.grid().has_bomb_at({target_tile_x, target_tile_y})) {
        for (uint32_t aid : targets) {
            sim::AntOrder order;
            order.ant_id = aid;
            order.type = sim::OrderType::Move;
            order.target_x = target_tile_x;
            order.target_y = target_tile_y;
            order.allow_friendly_bomb = true;
            sim.issue_order(order);
        }
        return;
    }

    // Concentric Chebyshev ring distribution for authentic tidy group formations
    const auto& grid = sim.grid();
    std::vector<std::pair<int32_t, int32_t>> slots;
    slots.reserve(targets.size());
    std::unordered_set<uint64_t> visited;
    auto add_slot = [&](int32_t x, int32_t y) {
        uint64_t key = (static_cast<uint64_t>(x) << 32) | static_cast<uint32_t>(y);
        if (visited.insert(key).second) {
            if (grid.in_bounds(x, y) && grid.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).is_passable()) {
                slots.push_back({x, y});
            }
        }
    };

    if (grid.has_food_at({target_tile_x, target_tile_y}) || grid.has_lunchbox_at({target_tile_x, target_tile_y})) {
        const sim::ActiveFoodSchedule* matched_fs = nullptr;
        for (const auto& afs : grid.food_schedules()) {
            if (!afs.active) continue;
            for (const auto& c : afs.footprint) {
                if (c.x == target_tile_x && c.y == target_tile_y) {
                    matched_fs = &afs;
                    break;
                }
            }
            if (matched_fs) break;
        }
        if (matched_fs) {
            for (const auto& c : matched_fs->footprint) add_slot(c.x, c.y);
            for (const auto& c : matched_fs->footprint) {
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        add_slot(c.x + dx, c.y + dy);
                    }
                }
            }
        } else {
            add_slot(target_tile_x, target_tile_y);
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    add_slot(target_tile_x + dx, target_tile_y + dy);
                }
            }
        }
    } else if (grid.in_bounds(target_tile_x, target_tile_y) &&
               grid.get_cell(static_cast<uint32_t>(target_tile_x), static_cast<uint32_t>(target_tile_y)).is_passable()) {
        add_slot(target_tile_x, target_tile_y);
    }

    for (int32_t r = 1; slots.size() < targets.size() && r < 20; ++r) {
        for (int32_t dy = -r; dy <= r && slots.size() < targets.size(); ++dy) {
            for (int32_t dx = -r; dx <= r && slots.size() < targets.size(); ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                add_slot(target_tile_x + dx, target_tile_y + dy);
            }
        }
    }

    for (size_t i = 0; i < targets.size(); ++i) {
        sim::AntOrder order;
        order.ant_id = targets[i];
        order.type = sim::OrderType::Move;
        order.allow_friendly_bomb = allow_friendly_bomb;
        if (i < slots.size()) {
            order.target_x = slots[i].first;
            order.target_y = slots[i].second;
        } else {
            order.target_x = target_tile_x;
            order.target_y = target_tile_y;
        }
        sim.issue_order(order);
    }
}

void HUD::dispatch_attack_order(uint32_t target_enemy_id, sim::SimulationEngine& sim) {
    const auto& world = sim.get_world_state();
    for (const auto& a : world.ants) {
        if (a.id == target_enemy_id) {
            // Cannot attack friendly teammates or allies
            if (a.player_id == local_player_id_ || sim.stats_manager().are_allies(local_player_id_, a.player_id)) {
                play_sfx(sim::SoundID::AntStop);
                return;
            }
            if (a.on_powerup || (a.type == sim::AntType::Swimmer && a.is_swimming)) {
                play_sfx(sim::SoundID::AntStop);
                return;
            }
            break;
        }
    }

    std::vector<uint32_t> raw_targets = selected_ant_ids_;
    if (raw_targets.empty() && selected_ant_id_ != 0) {
        raw_targets.push_back(selected_ant_id_);
    }

    // STRICT INVARIANT: Only friendly units owned by local_player_id_ may ever receive attack orders!
    std::vector<uint32_t> targets;
    for (uint32_t aid : raw_targets) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                targets.push_back(aid);
                break;
            }
        }
    }
    if (targets.empty()) return;

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
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_) {
                if (a.state == sim::UnitState::BuildingBridge || a.state == sim::UnitState::DemolishingBridge) {
                    continue;
                }
                play_sfx(sim::get_attack_voice_sound(a.type, voice_variant_++));
                goto attack_voice_done;
            }
        }
    }
attack_voice_done:

    for (uint32_t aid : targets) {
        bool skip = false;
        for (const auto& a : world.ants) {
            if (a.id == aid && (a.state == sim::UnitState::BuildingBridge || a.state == sim::UnitState::DemolishingBridge)) {
                skip = true;
                break;
            }
        }
        if (skip) continue;

        sim::AntOrder order;
        order.ant_id = aid;
        order.type = sim::OrderType::Attack;
        order.target_x = target_x;
        order.target_y = target_y;
        order.target_entity_id = static_cast<int32_t>(target_enemy_id);
        sim.issue_order(order);
    }
}

void HUD::dispatch_smart_special_ability(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim, bool shift_held) {
    const auto& world = sim.get_world_state();
    std::vector<uint32_t> raw_targets = selected_ant_ids_;
    if (raw_targets.empty() && selected_ant_id_ != 0) {
        raw_targets.push_back(selected_ant_id_);
    }
    std::vector<uint32_t> targets;
    for (uint32_t aid : raw_targets) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                targets.push_back(aid);
                break;
            }
        }
    }
    if (targets.empty()) return;

    int32_t target_tile_x = world_x / 32;
    int32_t target_tile_y = world_y / 32;

    const auto& grid = sim.grid();
    bool is_food_or_pu = grid.has_food_at({target_tile_x, target_tile_y}) ||
                         grid.has_powerup_at({target_tile_x, target_tile_y}) ||
                         grid.has_lunchbox_at({target_tile_x, target_tile_y});

    std::vector<std::pair<int32_t, int32_t>> slots;
    if (targets.size() > 1 && is_food_or_pu) {
        slots.reserve(targets.size());
        std::unordered_set<uint64_t> visited;
        auto add_slot = [&](int32_t x, int32_t y) {
            uint64_t key = (static_cast<uint64_t>(x) << 32) | static_cast<uint32_t>(y);
            if (visited.insert(key).second) {
                if (grid.in_bounds(x, y) && grid.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).is_passable()) {
                    slots.push_back({x, y});
                }
            }
        };

        const sim::ActiveFoodSchedule* matched_fs = nullptr;
        for (const auto& afs : grid.food_schedules()) {
            if (!afs.active) continue;
            for (const auto& c : afs.footprint) {
                if (c.x == target_tile_x && c.y == target_tile_y) {
                    matched_fs = &afs;
                    break;
                }
            }
            if (matched_fs) break;
        }

        if (matched_fs) {
            for (const auto& c : matched_fs->footprint) add_slot(c.x, c.y);
            for (const auto& c : matched_fs->footprint) {
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        add_slot(c.x + dx, c.y + dy);
                    }
                }
            }
        } else {
            add_slot(target_tile_x, target_tile_y);
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    add_slot(target_tile_x + dx, target_tile_y + dy);
                }
            }
        }

        for (int32_t r = 2; slots.size() < targets.size() && r < 15; ++r) {
            for (int32_t dy = -r; dy <= r && slots.size() < targets.size(); ++dy) {
                for (int32_t dx = -r; dx <= r && slots.size() < targets.size(); ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    add_slot(target_tile_x + dx, target_tile_y + dy);
                }
            }
        }
    }

    bool played_voice = false;

    for (size_t ti = 0; ti < targets.size(); ++ti) {
        uint32_t aid = targets[ti];
        const sim::AntSnapshot* sel = nullptr;
        for (const auto& a : world.ants) {
            if (a.id == aid) { sel = &a; break; }
        }
        if (!sel || sel->player_id != local_player_id_ || sel->hp == 0 || sel->is_drowning) continue;
        if (sel->state == sim::UnitState::BuildingBridge || sel->state == sim::UnitState::DemolishingBridge) continue;

        sim::AntOrder order;
        order.ant_id = aid;
        if (!slots.empty() && ti < slots.size()) {
            order.target_x = slots[ti].first;
            order.target_y = slots[ti].second;
        } else {
            order.target_x = target_tile_x;
            order.target_y = target_tile_y;
        }

        if (is_food_or_pu) {
            order.type = sim::OrderType::Move;
        } else {
            switch (sel->type) {
                case sim::AntType::Bomber:
                    if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                        if (is_multi_select() || shift_held) {
                            order.type = sim::OrderType::Move;
                            order.allow_friendly_bomb = true;
                        } else {
                            order.type = sim::OrderType::DefuseBomb;
                        }
                    } else if (active_order_mode_ == sim::OrderType::PlantBomb) {
                        order.type = sim::OrderType::PlantBomb;
                    } else if (grid.in_bounds({target_tile_x, target_tile_y}) &&
                               grid.get_cell({target_tile_x, target_tile_y}).can_place_bomb()) {
                        order.type = sim::OrderType::PlantBomb;
                    } else {
                        order.type = sim::OrderType::Move;
                    }
                    break;
                case sim::AntType::Fire:
                    if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                        order.type = sim::OrderType::Move;
                        order.allow_friendly_bomb = true;
                    } else if (sim.has_fire_at({target_tile_x, target_tile_y})) {
                        order.type = sim::OrderType::ExtinguishFire;
                    } else {
                        order.type = sim::OrderType::IgniteFire;
                    }
                    break;
                case sim::AntType::Swimmer:
                    if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                        order.type = sim::OrderType::Move;
                        order.allow_friendly_bomb = true;
                    } else if (grid.in_bounds({target_tile_x, target_tile_y})) {
                        const auto& cell = grid.get_cell({target_tile_x, target_tile_y});
                        if (cell.has_completed_bridge() || cell.has_partial_bridge()) {
                            order.type = sim::OrderType::DemolishBridge;
                        } else if (cell.terrain_type == sim::TERRAIN_WATER ||
                                   cell.surface_type == sim::SurfaceType::Water) {
                            order.type = sim::OrderType::BuildBridge;
                        } else {
                            order.type = sim::OrderType::Move;
                        }
                    } else {
                        order.type = sim::OrderType::Move;
                    }
                    break;
                case sim::AntType::Thief: {
                    if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                        order.type = sim::OrderType::Move;
                        order.allow_friendly_bomb = true;
                    } else {
                        bool hit_enemy_base = false;
                        for (const auto& base : world.anthills) {
                            if (base.team_id != sel->player_id &&
                                target_tile_x >= base.x && target_tile_x < base.x + 4 &&
                                target_tile_y >= base.y && target_tile_y < base.y + 4) {
                                hit_enemy_base = true;
                                break;
                            }
                        }
                        if (hit_enemy_base) {
                            order.type = sim::OrderType::InfiltrateAnthill;
                        } else {
                            order.type = sim::OrderType::Move;
                        }
                    }
                    break;
                }
                case sim::AntType::Combat:
                    order.type = sim::OrderType::Move;
                    if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                        order.allow_friendly_bomb = true;
                    }
                    break;
                case sim::AntType::Worker:
                default:
                    order.type = sim::OrderType::Move;
                    if (sim.has_bomb_at({target_tile_x, target_tile_y})) {
                        order.allow_friendly_bomb = true;
                    }
                    break;
            }
        }

        bool is_swimmer = (sel->type == sim::AntType::Swimmer);
        bool is_fire = (sel->type == sim::AntType::Fire);
        bool dest_passable = grid.in_bounds(order.target_x, order.target_y) &&
                             grid.get_cell(static_cast<uint32_t>(order.target_x), static_cast<uint32_t>(order.target_y)).is_passable(is_swimmer, is_fire);
        if (order.type == sim::OrderType::Move && !dest_passable &&
            (sel->is_transforming || sel->on_powerup || grid.has_powerup_at({sel->tile_x, sel->tile_y}))) {
            sim.interrupt_transformation(aid);
            play_sfx(sim::SoundID::AntStop);
            continue;
        }

        if (!played_voice) {
            played_voice = true;
            if (order.type == sim::OrderType::Move || order.type == sim::OrderType::PlantBomb) {
                play_sfx(sim::get_move_voice_sound(sel->type, voice_variant_++));
            } else {
                play_sfx(sim::get_ability_voice_sound(sel->type));
            }
        }

        sim.issue_order(order);
    }
}

} // namespace ants::app

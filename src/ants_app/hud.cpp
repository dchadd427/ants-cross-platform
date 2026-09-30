#include "ants_app/hud.hpp"
#include "ants_sim/prng.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/ui_anim.hpp"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>

namespace ants::app {

namespace {

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

// Unsigned number as the original draws scores (Ants.exe FUN_01010452 without the sign flag): a 6-slot field of 9 px
// digit slots starting at x, leading zeros skipped but still advancing (so the number is right-aligned in the field).
void draw_score_digits(IRenderer& renderer, const assets::AssetArchive& archive, int32_t x, int32_t y, int32_t value) {
    int32_t v = std::clamp(value, 0, 999999);
    int32_t divisor = 100000;
    bool leading = true;
    while (divisor > 0) {
        const int32_t digit = v / divisor;
        v %= divisor;
        if (digit != 0 || divisor == 1) leading = false;
        if (!leading) {
            const std::string name = "dig" + std::to_string(digit);
            draw_animation_frame0(renderer, archive, name.c_str(), x, y);
        }
        divisor /= 10;
        x += 9;
    }
}

// Minimap ground truth (Ants.exe FUN_01009596): 119x91 image at (480,35), palette-index colours
struct MinimapObject { uint16_t id; uint8_t colour; uint8_t size_flag; };
#include "minimap_tables.inc"

// Speckle table of the terrain classes (0x1001c28): 5 palette indices per class 0..5, picked with rand() % 5
constexpr uint8_t kMinimapClassColours[6][5] = {
    {251, 201, 249, 251, 251},   // 0 gravel
    {235, 235, 235, 235, 235},   // 1 slate
    { 37,  37,  37,  37,  37},   // 2 water
    { 77,  77,  77,  77,  77},   // 3 mud
    {231, 232, 233, 231, 231},   // 4 dirt
    {  0,   0,   0,   0,   0}    // 5 (unused class)
};
// Colours of unexplored cells by class (0x1001c48)
constexpr uint8_t kMinimapFogColours[8] = { 244, 237, 225, 245, 236, 0, 0, 0 };
// Ant dot colours by remake player id (green, red, blue, black) = original colour {3,2,1,0} (FUN_0101aa65)
constexpr uint8_t kMinimapAntColours[4] = { 47, 158, 211, 239 };

const MinimapObject* find_minimap_object(uint16_t id) {
    for (const auto& o : kMinimapObjects) if (o.id == id) return &o;
    return nullptr;
}

// Terrain class of a snapshot cell (0 gravel, 1 slate, 2 water, 3 mud, 4 dirt)
uint8_t minimap_class(const sim::TileCell& cell) {
    if (cell.terrain_type == sim::TERRAIN_WATER) return 2;
    switch (cell.surface_type) {
        case sim::SurfaceType::Slate:  return 1;
        case sim::SurfaceType::Water:  return 2;
        case sim::SurfaceType::Mud:    return 3;
        case sim::SurfaceType::Gravel: return 4;
        default:                       return 0;
    }
}

// Option-screen controls (absolute screen coordinates): each rectangle is the union of the resting and the pressed
// art of the control's animations (breturn1/3, op_conu/op_cond, op_coffu/op_coffd, op_honu/op_hond, op_hoffu/op_hoffd)
constexpr UIRect kOptReturn{351, 425, 98, 26};
constexpr UIRect kOptChatOn{102, 289, 49, 24};
constexpr UIRect kOptChatOff{151, 289, 49, 24};
constexpr UIRect kOptHelpOn{355, 289, 49, 24};
constexpr UIRect kOptHelpOff{404, 289, 49, 24};

// Slider thumb (slidd.bmp): x = 188 + min(184, 185 * v / 99) for v = 0..100, on the track rows 178 / 215 / 252
int32_t slider_thumb_x(float value) {
    const int32_t v = static_cast<int32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    return 188 + std::min<int32_t>(184, 185 * v / 99);
}

} // anonymous namespace

HUD::HUD() {
    init(0);
}

void HUD::init(uint8_t local_player_id) {
    local_player_id_ = local_player_id;
    left_pedestal_.reset();
    right_pedestal_.reset();
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    active_order_mode_ = sim::OrderType::None;
    is_dragging_ = false;
    is_radar_dragging_ = false;
    news_queue_.clear();
    chat_log_.clear();
    chat_scroll_offset_ = 0;
    queue_news_message("Game started! Go get that food!", 120, false);
    add_chat_entry("System", "Game started! Go get that food!");

    // Configure Top Header Buttons (x0y0.bmp)
    help_button_ = {476, 7, 46, 23, 0, 0, 0, false, true, false};
    options_button_ = {525, 7, 52, 23, 0, 0, 0, false, true, false};
    quit_button_ = {579, 7, 46, 23, 0, 0, 0, false, true, false};

    // Configure Authentic Primary Action Pedestal (Move) at (476, 156, 55, 75)
    move_pedestal_button_ = {476, 156, 55, 75, 0, 0, 0, false, true, false};

    // Configure Authentic Secondary Ability Pedestal at (537, 156, 55, 75)
    ability_pedestal_button_ = {537, 156, 55, 75, 0, 0, 0, false, true, false};

    // Configure Authentic Stop Button at (602, 176, 34, 50)
    stop_button_ = {595, 180, 32, 50, 0, 0, 0, false, true, false};

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
    yes_button_ = {180, 260, 49, 24, 0, 0, 0, false, true, false}; // dyn_byes1 part (80,160) + origin (100,100)
    no_button_  = {292, 260, 49, 24, 0, 0, 0, false, true, false}; // dyn_bno1 part (192,160) + origin (100,100)

    show_match_start_modal_ = false;
    match_start_modal_ticks_ = 0;
    show_quit_dialog_ = false;
    show_quick_help_ = false;
    show_options_ = false;

    // Configure Hatch Button matching Primary Pedestal at (488, 140, 53, 86)
    hatch_button_.x = 477;   // buteggu: pedestal (477,157) 53x71, label (483,140), icon (490,165)
    hatch_button_.y = 140;
    hatch_button_.w = 53;
    hatch_button_.h = 88;
    hatch_button_.sprite_up = 2683;    // buthatup.bmp
    hatch_button_.sprite_down = 2684;  // buthatd.bmp
    hatch_button_.sprite_label = 2682; // labhatch.bmp

    // Configure Authentic Team Up Button matching Primary Pedestal on Enemy Base Card
    team_up_button_.x = 477;
    team_up_button_.y = 142;
    team_up_button_.w = 55;
    team_up_button_.h = 86;
    team_up_button_.sprite_up = 2576;    // butdipu.bmp
    team_up_button_.sprite_down = 2587;  // butdipd.bmp
    team_up_button_.sprite_label = 2575; // labdib.bmp
    team_up_button_.is_enabled = true;
    team_up_button_.is_pressed = false;
    team_up_button_.is_active = false;


    queue_news_message("Ants Remake", 200, false);
}

void HUD::reset() {
    init(local_player_id_);
}

void HUD::set_active_order_mode(sim::OrderType mode) noexcept {
    active_order_mode_ = mode;
}

void HUD::update(const sim::WorldState& world, uint32_t delta_ticks) {
    // 0. Match Start Modal Countdown (5.0 s / 100 ticks at 20 Hz: first timer tick of Ants.exe 0x1017127)
    if (show_match_start_modal_) {
        match_start_modal_ticks_ += delta_ticks;
        if (match_start_modal_ticks_ >= MATCH_START_MODAL_DURATION_TICKS) {
            show_match_start_modal_ = false;
        }
    }

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

    // 3. Cursor blink ticks and alliance team status
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

    // 1. The static HUD shell: animation uishell = 14 parts (frame borders, top bar, banner, card, chat boxes, status
    //    box at (479,253)) drawn last part first, exactly as the original composes it. Nothing static is drawn twice.
    draw_animation_frame0(renderer, assets, "uishell", 0, 0);

    // 2. Right Panel Modules (Authentic Ants reconstruction)
    // 2.1 Minimap Radar at (480, 35..126) and decorative bezel x599y35.bmp at (599, 35)
    render_radar(renderer, assets, world, camera);

    // 2.2 Card background x480y126.bmp at (480, 126..254) with embossed "Status"

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

    // Selection state that drives the two pedestal slots
    bool has_friendly_ants = false;
    if (selected_base_team_id_ < 0) {
        for (uint32_t aid : selected_ant_ids_) {
            for (const auto& a : world.ants) {
                if (a.id == aid && a.player_id == local_player_id_) {
                    has_friendly_ants = true;
                    break;
                }
            }
            if (has_friendly_ants) break;
        }
        if (selected_ant_id_ != 0 && sel_ant && sel_ant->player_id == local_player_id_) {
            has_friendly_ants = true;
        }
    }

    const uint32_t eggs_now = (local_player_id_ < world.player_eggs.size()) ? world.player_eggs[local_player_id_] : 0;
    PedestalKind left_kind = PedestalKind::Hidden;
    int left_mode = 1;
    PedestalKind right_kind = PedestalKind::Hidden;
    int right_mode = 1;
    if (selected_base_team_id_ >= 0) {
        if (selected_base_team_id_ == local_player_id_) {
            // Home anthill (Ants.exe FUN_01027f07 mode 2): hatch pedestal ("egg" kind) only while eggs remain
            if (eggs_now > 0) { left_kind = PedestalKind::Egg; left_mode = hatch_button_.is_pressed ? 2 : 1; }
        } else {
            left_kind = PedestalKind::Ally;
            left_mode = team_up_button_.is_pressed ? 2 : 1;
        }
    } else if (has_friendly_ants) {
        left_kind = PedestalKind::Move;
        left_mode = (move_pedestal_button_.is_pressed || active_order_mode_ == sim::OrderType::Move) ? 2 : 1;
        // Ability pedestal of the single selected ant (hidden with Shift held or a multi-selection)
        const bool hide_ability = is_shift_held() || is_multi_select() || selected_ant_ids_.size() > 1;
        if (sel_ant && sel_ant->player_id == local_player_id_ && !hide_ability) {
            switch (sel_ant->type) {
                case sim::AntType::Swimmer:
                    right_kind = PedestalKind::Swim;
                    right_mode = (ability_pedestal_button_.is_pressed || active_order_mode_ == sim::OrderType::BuildBridge) ? 2 : 1;
                    break;
                case sim::AntType::Fire:
                    right_kind = PedestalKind::Fire;
                    right_mode = (ability_pedestal_button_.is_pressed || active_order_mode_ == sim::OrderType::IgniteFire) ? 2 : 1;
                    break;
                case sim::AntType::Combat:
                    right_kind = PedestalKind::Attack;
                    right_mode = (ability_pedestal_button_.is_pressed || active_order_mode_ == sim::OrderType::Attack) ? 2 : 1;
                    break;
                case sim::AntType::Bomber:
                    right_kind = PedestalKind::Bomb;
                    right_mode = (ability_pedestal_button_.is_pressed || active_order_mode_ == sim::OrderType::PlantBomb) ? 2 : 1;
                    break;
                case sim::AntType::Thief:
                    right_kind = PedestalKind::Thief;
                    right_mode = (ability_pedestal_button_.is_pressed || active_order_mode_ == sim::OrderType::InfiltrateAnthill) ? 2 : 1;
                    break;
                default: break;
            }
        }
    }
    // Both slots play the original rise / sink / icon-swap / press chains in real time (FUN_01028360)
    const uint32_t now_ms = ticks_fn_ ? ticks_fn_() : SDL_GetTicks();
    left_pedestal_.request(assets, left_kind, left_mode, now_ms);
    right_pedestal_.request(assets, right_kind, right_mode, now_ms);
    left_pedestal_.draw(renderer, assets, now_ms);
    right_pedestal_.draw(renderer, assets, now_ms);
    if (left_pedestal_.is_settled_and_visible() && left_pedestal_.resting_kind() == PedestalKind::Move &&
        (current_cursor_ == CursorType::Move || current_cursor_ == CursorType::Food)) {
        render_pedestal_glow(renderer, assets, 1);
    }
    if (right_pedestal_.is_settled_and_visible() &&
        (right_mouse_held_ || active_order_mode_ != sim::OrderType::None || current_cursor_ == CursorType::Target)) {
        render_pedestal_glow(renderer, assets, 2);
    }

    if (selected_base_team_id_ >= 0) {
        if (selected_base_team_id_ == local_player_id_) {
            // Egg tray egg<N> (N = min(eggs, 9)) and the Stop button; both are animations with absolute part coordinates
            if (eggs_now > 0) {
                const std::string tray = "egg" + std::to_string(std::min<uint32_t>(eggs_now, 9u));
                draw_animation_frame0(renderer, assets, tray.c_str(), 0, 0);
            }
            draw_animation_frame0(renderer, assets, stop_button_.is_pressed ? "butcand" : "butcanu", 0, 0);

            // (the recessed status box wstatus.bmp is part of the uishell composite)
            if (!news_queue_.empty()) {
                ants::assets::ColorRGBA col = news_queue_.front().is_alarm ? ants::assets::ColorRGBA{180, 30, 30, 255} : ants::assets::ColorRGBA{16, 40, 24, 255};
                renderer.draw_text(news_queue_.front().text, 486, 253, col);
            }
        } else {
            // Enemy base: status box only (the ally pedestal is the left slot)
            bool is_allied = (local_player_id_ < world.player_alliances.size()) &&
                             (world.player_alliances[local_player_id_] == selected_base_team_id_);
            if (!news_queue_.empty()) {
                ants::assets::ColorRGBA col = news_queue_.front().is_alarm ? ants::assets::ColorRGBA{180, 30, 30, 255} : ants::assets::ColorRGBA{16, 40, 24, 255};
                renderer.draw_text(news_queue_.front().text, 486, 253, col);
            } else if (is_allied) {
                renderer.draw_text("Allied Colony", 486, 253, {100, 255, 100, 255});
            }
        }
    } else {
        if (has_friendly_ants) {
            // Stop button (animation butcanu / butcand: label at (595,180), button at (595,198))
            draw_animation_frame0(renderer, assets, stop_button_.is_pressed ? "butcand" : "butcanu", 0, 0);

            // Lunchbox indicator (animation UI_LBOX, sprite at (597,131)) only when every selected ant carries food
            bool all_carry = false;
            if (!selected_ant_ids_.empty()) {
                all_carry = true;
                for (uint32_t aid : selected_ant_ids_) {
                    bool carries = false;
                    for (const auto& a : world.ants) if (a.id == aid && a.is_holding) { carries = true; break; }
                    if (!carries) { all_carry = false; break; }
                }
            } else if (sel_ant && sel_ant->is_holding) {
                all_carry = true;
            }
            if (all_carry) {
                draw_animation_frame0(renderer, assets, "UI_LBOX", 0, 0);
            }
        }

        // (the recessed status box wstatus.bmp is part of the uishell composite)
        std::string status_text = "Ready.";
        ants::assets::ColorRGBA status_color = {16, 40, 24, 255};
        if (!news_queue_.empty()) {
            status_text = news_queue_.front().text;
            if (news_queue_.front().is_alarm) {
                status_color = {180, 30, 30, 255};
            }
        } else if (sel_ant) {
            if (sel_ant->player_id != local_player_id_) status_text = "Enemy ant.";
            else if (sel_ant->is_drowning) status_text = "Drowning!";
            else if (sel_ant->is_holding) status_text = "Holds pick up...";
            else if (sel_ant->anim_state == 1 || sel_ant->anim_state == 2) status_text = "On my way.";
            else if (sel_ant->anim_state == 3) status_text = "In combat!";
            else status_text = "Waiting for orders.";
        }
        renderer.draw_text(status_text, 486, 253, status_color);
    }

    // 2.3 Lower Panel: Always render Chat Section
    // Cursive embossed Chat header at (480, 266)

    // White chat history log wchat.bmp (143x103) at (479, 298)
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

    // Chat text input box wtype.bmp (143x14) at (479, 423)
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
    // Chat switched off in the options: chatcovr (three chcovr2 tiles at (478,421/436/445)) covers the input box
    if (!chat_enabled_) {
        draw_animation_frame0(renderer, assets, "chatcovr", 0, 0);
    }

    // "Send to:" buttons (animations butall*, butals*): up / hover ("r" label over the up art) / pressed
    auto send_button = [&](bool down, bool hovered, const char* up, const char* hover, const char* pressed) {
        draw_animation_frame0(renderer, assets, down ? pressed : (hovered ? hover : up));
    };
    if (!is_on_team_) {
        // In FFA or non-team mode, only [All] is active/shown
        send_button(send_to_button_.is_pressed, send_to_button_.is_hovered, "butallu", "butallr", "butalld");
    } else {
        // When on a team, the chosen destination is shown down
        const bool all_down = send_to_button_.is_pressed || (send_to_all_ && !team_button_.is_pressed);
        const bool team_down = team_button_.is_pressed || (!send_to_all_ && !send_to_button_.is_pressed);
        send_button(all_down, send_to_button_.is_hovered, "butallu", "butallr", "butalld");
        send_button(team_down, team_button_.is_hovered, "butalsu", "butalsr", "butalsd");
    }

    // Vertical right border strip x521y254.bmp (19x182) placed at x=621, y=254 (seals right screen edge)

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
    } else if (show_match_start_modal_) {
        render_match_start_modal(renderer, assets);
    }
}

void HUD::render_top_bar(IRenderer& renderer, const assets::AssetArchive& archive, const sim::WorldState& world) {
    // Top border backdrop: x0y0.bmp (640x22)

    // Match clock (Ants.exe FUN_01021e36): digit sprites at y = 6, tens of minutes at x = 70 (skipped when 0), minutes 80,
    // colon 90, tens of seconds 97, seconds 107, inside the pre-cut black box of x0y0.bmp.
    const uint32_t ms = world.match_time_remaining_ms;
    const uint32_t mm = (ms / 1000) / 60;
    const uint32_t ss = (ms / 1000) % 60;
    auto digit = [&](uint32_t d, int32_t x) {
        const std::string name = "dig" + std::to_string(d % 10);
        draw_animation_frame0(renderer, archive, name.c_str(), x, 6);
    };
    if (mm >= 10) digit(mm / 10, 70);
    digit(mm % 10, 80);
    draw_animation_frame0(renderer, archive, "digc", 90, 6);
    digit(ss / 10, 97);
    digit(ss % 10, 107);

    // Box 2 (Top-Right above Playfield): Local player's own score in box at (402..455, 4..17), team background fill
    // (Ants.exe VA 0x100DA90) and the score drawn with digit sprites at (box.left - 1, box.top + 2)
    renderer.fill_rect(402, 4, 54, 14, SCORE_BG_COLORS[local_player_id_ % 4]);
    int32_t my_score = (local_player_id_ < world.player_scores.size()) ? world.player_scores[local_player_id_] : 0;
    draw_score_digits(renderer, archive, 401, 6, my_score);

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

    // Top-bar buttons (animations buthlp*, butopt*, butqit*; absolute coordinates): the resting art is already part of
    // the top bar image, hovering draws the small "r" label over it and the pressed art replaces it. A button stays down
    // while the screen it opened is showing.
    if (help_button_.is_pressed || show_quick_help_) {
        draw_animation_frame0(renderer, archive, "buthlpd");
    } else if (help_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "buthlpr");
    }
    if (options_button_.is_pressed || show_options_) {
        draw_animation_frame0(renderer, archive, "butoptd");
    } else if (options_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "butoptr");
    }
    if (quit_button_.is_pressed || show_quit_dialog_) {
        draw_animation_frame0(renderer, archive, "butqitd");
    } else if (quit_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "butqitr");
    }
}

void HUD::render_radar(IRenderer& renderer, const assets::AssetArchive& archive,
                       const sim::WorldState& world, const ViewportCamera& camera) {
    // The minimap frame (x599y35) is part of the uishell composite. The map image itself is 119x91 at (480,35).
    const int32_t rx = 480, ry = 35;
    const int32_t rw = 119, rh = 91;

    if (world.width == 0 || world.height == 0 || world.cells.size() != static_cast<size_t>(world.width) * world.height) {
        renderer.fill_rect(rx, ry, rw, rh, {0, 0, 0, 255});
        return;
    }

    // Terrain speckle: one random pick per pixel from the class colours, made once per map (the original paints it when
    // cells are first drawn and keeps it until they are repainted)
    if (radar_map_w_ != world.width || radar_map_h_ != world.height || radar_terrain_.size() != static_cast<size_t>(rw * rh)) {
        radar_map_w_ = world.width;
        radar_map_h_ = world.height;
        radar_terrain_.assign(static_cast<size_t>(rw * rh), 0);
        sim::PRNG rng(world.width * 1000u + world.height);
        for (int32_t y = 0; y < rh; ++y) {
            for (int32_t x = 0; x < rw; ++x) {
                const int32_t cx = std::min<int32_t>(static_cast<int32_t>(world.width) - 1, x * static_cast<int32_t>(world.width) / rw);
                const int32_t cy = std::min<int32_t>(static_cast<int32_t>(world.height) - 1, y * static_cast<int32_t>(world.height) / rh);
                const uint8_t cls = minimap_class(world.cells[static_cast<size_t>(cy) * world.width + static_cast<size_t>(cx)]);
                radar_terrain_[static_cast<size_t>(y * rw + x)] = kMinimapClassColours[cls < 6 ? cls : 5][rng.rand() % 5u];
            }
        }
    }

    const auto& palette = archive.get_palette();
    std::vector<uint8_t> pixels(static_cast<size_t>(rw * rh), 0);   // palette indices
    const float px_per_cell_x = static_cast<float>(rw) / static_cast<float>(world.width);
    const float px_per_cell_y = static_cast<float>(rh) / static_cast<float>(world.height);

    auto revealed = [&](int32_t tx, int32_t ty) { return world.is_tile_revealed(tx, ty); };

    for (int32_t y = 0; y < rh; ++y) {
        for (int32_t x = 0; x < rw; ++x) {
            const int32_t cx = std::min<int32_t>(static_cast<int32_t>(world.width) - 1, x * static_cast<int32_t>(world.width) / rw);
            const int32_t cy = std::min<int32_t>(static_cast<int32_t>(world.height) - 1, y * static_cast<int32_t>(world.height) / rh);
            const auto& cell = world.cells[static_cast<size_t>(cy) * world.width + static_cast<size_t>(cx)];
            uint8_t colour = radar_terrain_[static_cast<size_t>(y * rw + x)];
            if (world.fog_of_war_enabled && !revealed(cx, cy)) {
                const uint8_t cls = minimap_class(cell);
                colour = kMinimapFogColours[cls < 8 ? cls : 0];
            } else if (cell.interactive_id != sim::TILE_EMPTY && cell.interactive_id != 0xFFFF) {
                // Bombs (129..132) and fire walls (134) show the terrain colour
                const bool hidden = (cell.interactive_id >= 129 && cell.interactive_id <= 132) || cell.interactive_id == 134;
                if (!hidden) {
                    if (const auto* obj = find_minimap_object(cell.interactive_id)) colour = obj->colour;
                }
            }
            pixels[static_cast<size_t>(y * rw + x)] = colour;
        }
    }

    // Square dots (object size flag 1 or 2 cells, ants 1 cell) centred on the cell centre
    auto dot = [&](int32_t tile_x, int32_t tile_y, uint8_t colour, int32_t flag) {
        const int32_t size_x = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(flag) * px_per_cell_x)));
        const int32_t size_y = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(flag) * px_per_cell_y)));
        const int32_t mx = static_cast<int32_t>((static_cast<float>(tile_x) + 0.5f) * px_per_cell_x);
        const int32_t my = static_cast<int32_t>((static_cast<float>(tile_y) + 0.5f) * px_per_cell_y);
        for (int32_t yy = my - size_y / 2; yy < my - size_y / 2 + size_y; ++yy) {
            for (int32_t xx = mx - size_x / 2; xx < mx - size_x / 2 + size_x; ++xx) {
                if (xx >= 0 && xx < rw && yy >= 0 && yy < rh) pixels[static_cast<size_t>(yy * rw + xx)] = colour;
            }
        }
    };
    for (int32_t ty = 0; ty < static_cast<int32_t>(world.height); ++ty) {
        for (int32_t tx = 0; tx < static_cast<int32_t>(world.width); ++tx) {
            const auto& cell = world.cells[static_cast<size_t>(ty) * world.width + static_cast<size_t>(tx)];
            if (cell.interactive_id == sim::TILE_EMPTY || cell.interactive_id == 0xFFFF) continue;
            if ((cell.interactive_id >= 129 && cell.interactive_id <= 132) || cell.interactive_id == 134) continue;
            if (world.fog_of_war_enabled && !revealed(tx, ty)) continue;
            if (const auto* obj = find_minimap_object(cell.interactive_id)) {
                if (obj->size_flag > 0) dot(tx, ty, obj->colour, obj->size_flag);
            }
        }
    }
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        if (world.fog_of_war_enabled && ant.player_id != local_player_id_ && !revealed(ant.tile_x, ant.tile_y)) continue;
        dot(ant.tile_x, ant.tile_y, kMinimapAntColours[ant.player_id % 4], 1);
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(rw * rh) * 4u, 255);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const auto& c = palette[pixels[i]];
        rgba[i * 4 + 0] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
    }
    renderer.draw_rgba_image(rx, ry, rw, rh, rgba.data());

    // Scale used by the camera frame below
    const float scale_x = px_per_cell_x;
    const float scale_y = px_per_cell_y;

    // Camera frustum wireframe box
    float map_w_px = static_cast<float>(world.width * 32);
    float map_h_px = static_cast<float>(world.height * 32);
    if (map_w_px > 0.0f && map_h_px > 0.0f) {
        float max_cam_x = std::max(0.0f, map_w_px - static_cast<float>(camera.viewport_w));
        float max_cam_y = std::max(0.0f, map_h_px - static_cast<float>(camera.viewport_h));

        float cam_tile_x = camera.x / 32.0f;
        float cam_tile_y = camera.y / 32.0f;
        float cam_tile_w = static_cast<float>(camera.viewport_w) / 32.0f;
        float cam_tile_h = static_cast<float>(camera.viewport_h) / 32.0f;

        int32_t fx1 = rx + static_cast<int32_t>(cam_tile_x * scale_x);
        int32_t fy1 = ry + static_cast<int32_t>(cam_tile_y * scale_y);
        int32_t fx2 = (max_cam_x > 0.0f && camera.x >= max_cam_x - 0.5f)
                          ? (rx + rw)
                          : (rx + static_cast<int32_t>((cam_tile_x + cam_tile_w) * scale_x));
        int32_t fy2 = (max_cam_y > 0.0f && camera.y >= max_cam_y - 0.5f)
                          ? (ry + rh)
                          : (ry + static_cast<int32_t>((cam_tile_y + cam_tile_h) * scale_y));

        int32_t fw = std::max(4, fx2 - fx1);
        int32_t fh = std::max(4, fy2 - fy1);

        renderer.draw_rect(fx1, fy1, fw, fh, {255, 255, 255, 255});
    }
}

void HUD::render_news_banner(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world) {
    // Bottom banner background: x17y461.bmp (623x19)

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

        // Player score inside box: digit sprites at (box.left - 1, box.top + 2), right-aligned in a 6-slot field
        int32_t s = (p < world.player_scores.size()) ? world.player_scores[p] : 0;
        draw_score_digits(renderer, assets, slot.box_x - 1, 466, s);
    }
}

void HUD::render_quit_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // Quit dialog (Ants.exe 0x10142cb): no dim layer, origin (100,100) (OffsetRect(100,100) on the dialog's children)
    const int32_t dx = 100;
    const int32_t dy = 100;

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

    // Prompt (string 99) centred in the rect (130,180) 260x160 at the top, colour (31,23,51)
    std::string prompt = "Do you really want to quit?";
    int32_t text_w = renderer.get_text_width(prompt, FontSize::Large);
    int32_t text_x = 130 + (260 - text_w) / 2;
    renderer.draw_text(prompt, text_x, 180, ColorRGBA{31, 23, 51, 255}, FontSize::Large);

    // Yes button at (180, 260)
    // yes1/2/3 and no1/2/3 are placed with SetPos, so their part offsets are relative to the button origin
    draw_animation_frame0(renderer, assets, yes_button_.is_pressed ? "yes3" : (yes_button_.is_active ? "yes2" : "yes1"),
                          yes_button_.x, yes_button_.y);

    // No button at (292, 260)
    draw_animation_frame0(renderer, assets, no_button_.is_pressed ? "no3" : (no_button_.is_active ? "no2" : "no1"),
                          no_button_.x, no_button_.y);
}

void HUD::render_match_start_modal(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // Start modal (Ants.exe 0x1017127): std_dialg at origin (100,100), one wrapped centred label (string 105) in the
    // rect (130,110) 240x160, footer (string 104) in (130,290) 240x20, animated worker portrait at (245,250).
    const int32_t mx = 100;
    const int32_t my = 100;

    const auto* seq = assets.find_animation("std_dialg");
    if (seq && !seq->subitems.empty()) {
        const auto& frames = seq->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            renderer.draw_sprite(frames[i].sprite_index, mx + frames[i].dx, my + frames[i].dy);
        }
    } else {
        renderer.fill_rect(mx, my, 320, 224, ColorRGBA{219, 75, 19, 255});
        renderer.draw_rect(mx, my, 320, 224, ColorRGBA{36, 82, 77, 255});
    }

    // Dark slate text colour #1F1733 (COLORREF 0x33171F)
    const ColorRGBA text_color{31, 23, 51, 255};

    // Label: "Get ready to play!  You are the <Colour> Ants." word-wrapped and centred in 240 px
    static const char* TEAM_NAMES[4] = {"Green", "Red", "Blue", "Black"};
    const std::string label = std::string("Get ready to play!  You are the ") + TEAM_NAMES[local_player_id_ % 4] + " Ants.";
    std::vector<std::string> lines;
    {
        std::string line, word;
        auto flush_word = [&]() {
            if (word.empty()) return;
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && renderer.get_text_width(trial, FontSize::Large) > 240) {
                lines.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            word.clear();
        };
        for (char ch : label) {
            if (ch == ' ') flush_word(); else word.push_back(ch);
        }
        flush_word();
        if (!line.empty()) lines.push_back(line);
    }
    int32_t ty = 110;
    const int32_t line_h = renderer.get_text_height(FontSize::Large) + 2;
    for (const auto& l : lines) {
        const int32_t w = renderer.get_text_width(l, FontSize::Large);
        renderer.draw_text(l, 130 + (240 - w) / 2, ty, text_color, FontSize::Large);
        ty += line_h;
    }

    // Animated portrait of the local colour's worker ant (agst301: 12 frames, 1650 ms loop), anchor (245,250)
    renderer.set_hud_team(local_player_id_);
    if (const auto* ant = assets.find_animation("agst301")) {
        if (!ant->subitems.empty()) {
            const uint32_t ms = match_start_modal_ticks_ * 50u;
            const size_t frame = Renderer::get_anim_subitem_by_time(*ant, ms);
            const auto& parts = ant->subitems[frame].frames;
            for (size_t k = parts.size(); k-- > 0; ) {
                renderer.draw_sprite(parts[k].sprite_index, 245 + parts[k].dx, 250 + parts[k].dy);
            }
        }
    }
    renderer.set_hud_team(0);

    // Footer: "Waiting for others..."
    const std::string footer = "Waiting for others...";
    int32_t wf = renderer.get_text_width(footer, FontSize::Medium);
    renderer.draw_text(footer, 130 + (240 - wf) / 2, 290, text_color, FontSize::Medium);
}

void HUD::render_quick_help(IRenderer& renderer, const assets::AssetArchive& assets) {
    // In-game quick help (Ants.exe 0x10145d2, qh_screen): a 32-part full-screen composite (frame pieces plus qh2 at
    // (267,10) and qh1 at (10,9)) drawn over the live game, and the Return button qh_return1 at (529,437).
    draw_animation_frame0(renderer, assets, "qh_screen", 0, 0);
    const bool over_return = UIRect{529, 437, 98, 26}.contains(mouse_x_, mouse_y_);
    draw_animation_frame0(renderer, assets, over_return ? "qh_return2" : "qh_return1", 0, 0);
}

void HUD::render_options_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // Authentic op_screen composite (210 parts, Ants.exe 0x101487c): it already contains the 25 dither tiles
    // (a 50 % checker) around the opaque card, so nothing else is drawn behind it.
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

    // Sliders (Sound Volume, Music Volume, Map Scroll Rate): the thumb slidd.bmp sits on the track rows 178 / 215 / 252
    renderer.draw_named_sprite("slidd.bmp", slider_thumb_x(sfx_volume_), 178);
    renderer.draw_named_sprite("slidd.bmp", slider_thumb_x(music_volume_), 215);
    renderer.draw_named_sprite("slidd.bmp", slider_thumb_x(scroll_rate_), 252);

    // Toggles: the chosen one is shown down (with its hover variant), the other one up (op_con*, op_coff*, op_hon*, op_hoff*)
    const bool over_chat_on = kOptChatOn.contains(mouse_x_, mouse_y_);
    const bool over_chat_off = kOptChatOff.contains(mouse_x_, mouse_y_);
    const bool over_help_on = kOptHelpOn.contains(mouse_x_, mouse_y_);
    const bool over_help_off = kOptHelpOff.contains(mouse_x_, mouse_y_);
    draw_animation_frame0(renderer, assets, chat_enabled_ ? (over_chat_on ? "op_condr" : "op_cond") : (over_chat_on ? "op_conr" : "op_conu"));
    draw_animation_frame0(renderer, assets, !chat_enabled_ ? (over_chat_off ? "op_coffdr" : "op_coffd") : (over_chat_off ? "op_coffr" : "op_coffu"));
    draw_animation_frame0(renderer, assets, quick_help_enabled_ ? (over_help_on ? "optondr" : "op_hond") : (over_help_on ? "op_honr" : "op_honu"));
    draw_animation_frame0(renderer, assets, !quick_help_enabled_ ? (over_help_off ? "op_hoffdr" : "op_hoffd") : (over_help_off ? "op_hoffr" : "op_hoffu"));

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

    // Return to Game button: breturn1 (up), breturn2 (hover), breturn3 (pressed), absolute coordinates
    draw_animation_frame0(renderer, assets, opt_return_button_pressed_ ? "breturn3"
                          : (kOptReturn.contains(mouse_x_, mouse_y_) ? "breturn2" : "breturn1"));
}

void HUD::render_marquee_box(IRenderer& renderer) {
    int32_t dx = std::abs(drag_curr_x_ - drag_start_x_);
    int32_t dy = std::abs(drag_curr_y_ - drag_start_y_);
    int32_t threshold = (!selected_ant_ids_.empty() || selected_ant_id_ != 0) ? 10 : 4;
    if (dx <= threshold && dy <= threshold) return;

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

    renderer.draw_rect(x1, y1, w, h, {220, 0, 0, 255});
}

void HUD::render_pedestal_glow(IRenderer& renderer, const assets::AssetArchive& assets, int pedestal_idx) {
    const char* anim_name = (pedestal_idx == 1) ? "butdefl" : "butdefr";
    const auto* seq = assets.find_animation(anim_name);
    if (!seq || seq->subitems.empty()) return;

    // 4-step cycle: 150ms + 120ms + 120ms + 120ms = 510ms total (Ants.exe.c FUN_010285f0)
    uint32_t now_ms = SDL_GetTicks();
    uint32_t cycle_ms = now_ms % 510;
    size_t sub_idx = 0;
    if (cycle_ms < 150) sub_idx = 0;
    else if (cycle_ms < 270) sub_idx = 1;
    else if (cycle_ms < 390) sub_idx = 2;
    else sub_idx = 3;

    if (sub_idx >= seq->subitems.size()) sub_idx = 0;
    const auto& sub = seq->subitems[sub_idx];
    for (const auto& fr : sub.frames) {
        renderer.draw_sprite(fr.sprite_index, fr.dx, fr.dy);
    }
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

bool HUD::is_shift_held() const noexcept {
    return shift_held_ || ((static_cast<uint16_t>(SDL_GetModState()) & KMOD_SHIFT) != 0);
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

void HUD::select_ants_in_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const sim::WorldState& world, bool additive) {
    int32_t rx1 = std::min(x1, x2);
    int32_t rx2 = std::max(x1, x2);
    int32_t ry1 = std::min(y1, y2);
    int32_t ry2 = std::max(y1, y2);

    if (!additive) {
        selected_ant_ids_.clear();
    }
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        if (ant.player_id == local_player_id_) {
            // Authentic ant sprite extents around anchor (ant.px, ant.py):
            // Width spans 40-60px (dx ~ -20..+20, up to -30..+30 for Combat), height spans 44-46px (dy ~ -30..+16)
            int32_t half_w = (ant.type == sim::AntType::Combat) ? 30 : 20;
            int32_t top_h = 30;
            int32_t bot_h = 16;
            int32_t ant_x1 = ant.px - half_w;
            int32_t ant_x2 = ant.px + half_w;
            int32_t ant_y1 = ant.py - top_h;
            int32_t ant_y2 = ant.py + bot_h;

            // Also encompass the discrete tile bounds
            int32_t tile_x1 = ant.tile_x * 32;
            int32_t tile_x2 = tile_x1 + 32;
            int32_t tile_y1 = ant.tile_y * 32;
            int32_t tile_y2 = tile_y1 + 32;

            // Select if the marquee box intersects any part of the ant's sprite bounding box or tile
            bool hit_sprite = (rx1 <= ant_x2 && rx2 >= ant_x1 && ry1 <= ant_y2 && ry2 >= ant_y1);
            bool hit_tile   = (rx1 <= tile_x2 && rx2 >= tile_x1 && ry1 <= tile_y2 && ry2 >= tile_y1);

            if (hit_sprite || hit_tile) {
                if (std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), ant.id) == selected_ant_ids_.end()) {
                    selected_ant_ids_.push_back(ant.id);
                }
            }
        }
    }
    is_multi_select_mode_ = additive || (selected_ant_ids_.size() > 1);
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
    if (button == SDL_BUTTON_RIGHT) {
        right_mouse_held_ = true;
    }

    // 0. Overlays and Modals intercept clicks first
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT) {
            close_quick_help();   // qh_return3 carries no sound
        }
        return true;
    }
    if (show_options_) {
        if (button == SDL_BUTTON_LEFT) {
            // Return to Game button: breturn1/2/3 at (351,425) 98x26; breturn3 (pressed) is silent
            if (kOptReturn.contains(x, y)) {
                opt_return_button_pressed_ = true;
                opt_ok_button_pressed_ = true;
                return true;
            }
            // Chat ON / OFF and Quick Help ON / OFF toggles (op_con*, op_coff*, op_hon*, op_hoff*): all silent
            if (kOptChatOn.contains(x, y))  { chat_enabled_ = true;        return true; }
            if (kOptChatOff.contains(x, y)) { chat_enabled_ = false;       return true; }
            if (kOptHelpOn.contains(x, y))  { quick_help_enabled_ = true;  return true; }
            if (kOptHelpOff.contains(x, y)) { quick_help_enabled_ = false; return true; }
            // Sound FX slider (x 180..430, y 170..205)
            if (x >= 180 && x <= 430 && y >= 170 && y <= 205) {
                active_slider_dragging_ = 0;
                sfx_volume_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
                if (on_sfx_volume_) on_sfx_volume_(sfx_volume_);
                return true;
            }
            // Music slider (x 180..430, y 207..242)
            if (x >= 180 && x <= 430 && y >= 207 && y <= 242) {
                active_slider_dragging_ = 1;
                music_volume_ = std::clamp(static_cast<float>(x - 188) / 185.0f, 0.0f, 1.0f);
                if (on_music_volume_) on_music_volume_(music_volume_);
                return true;
            }
            // Scroll rate slider (x 180..430, y 244..285)
            if (x >= 180 && x <= 430 && y >= 244 && y <= 285) {
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
    if (show_match_start_modal_) {
        return true; // Consume all clicks while match start modal is open
    }
    if (show_quit_dialog_) {
        if (button == SDL_BUTTON_LEFT) {
            if (yes_button_.contains(x, y)) {
                yes_button_.is_pressed = true;
                play_sfx(sim::SoundID::ButtonClick);   // yes3 / no3 carry sound 0
                return true;
            }
            if (no_button_.contains(x, y)) {
                no_button_.is_pressed = true;
                play_sfx(sim::SoundID::ButtonClick);
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
            play_sfx(sim::SoundID::ButtonClick);   // the pressed top-bar animations carry sound 0
            show_quick_help_ = !show_quick_help_;
            return true;
        }
        if (options_button_.contains(x, y)) {
            options_button_.is_pressed = true;
            play_sfx(sim::SoundID::ButtonClick);
            show_options_ = !show_options_;
            return true;
        }
        if (quit_button_.contains(x, y)) {
            quit_button_.is_pressed = true;
            play_sfx(sim::SoundID::ButtonClick);
            open_quit_dialog();
            return true;
        }

        // 0. Check Authentic Base Selection Buttons
        if (selected_base_team_id_ >= 0) {
            if (selected_base_team_id_ == local_player_id_) {
                if (hatch_button_.contains(x, y) || move_pedestal_button_.contains(x, y)) {
                    hatch_button_.is_pressed = true;
                    play_sfx(sim::SoundID::NavButtonClick);
                    // FUN_01010aca: every click is handled by the simulation, which answers a refusal with its text
                    sim.try_hatch(local_player_id_, sim::AntType::Worker);
                    return true;
                }
                if (stop_button_.contains(x, y)) {
                    stop_button_.is_pressed = true;
                    play_sfx(sim::SoundID::NavButtonClick);
                    clear_selection();
                    return true;
                }
            } else {
                if (team_up_button_.contains(x, y) || move_pedestal_button_.contains(x, y)) {
                    team_up_button_.is_pressed = true;
                    play_sfx(sim::SoundID::NavButtonClick);
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
            play_sfx(sim::SoundID::NavButtonClick);
            if (active_order_mode_ == sim::OrderType::Move) {
                cancel_order_mode();
            } else {
                set_active_order_mode(sim::OrderType::Move);
            }
            return true;
        }

        // 1b. Check Authentic Secondary Action Pedestal (Class-Specific Ability) click
        bool hide_pedestal_2 = is_shift_held() || is_multi_select() || selected_ant_ids_.size() > 1;
        if (!hide_pedestal_2 && ability_pedestal_button_.contains(x, y) && !selected_ant_ids_.empty()) {
            ability_pedestal_button_.is_pressed = true;
            play_sfx(sim::SoundID::NavButtonClick);
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
            play_sfx(sim::SoundID::AntStop);   // butcand carries sound 61 (antstop.wav) and nothing else
            for (uint32_t aid : selected_ant_ids_) {
                const auto& u = sim.get_unit(aid);
                if (u.player_id != local_player_id_) continue;
                sim.stop_ant(aid);   // FUN_01028a60: accepted ants with a target go to their own tile
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
            play_sfx(sim::SoundID::ButtonClick);
            send_to_all_ = true;
            return true;
        }

        // 5. Check Authentic [Team] Button click (579, 443, 46x24)
        if (is_on_team_ && team_button_.contains(x, y)) {
            team_button_.is_pressed = true;
            play_sfx(sim::SoundID::ButtonClick);
            send_to_all_ = false;
            return true;
        }

        // 4. Check Hatch Button click
        if (hatch_button_.contains(x, y)) {
            hatch_button_.is_pressed = true;
            play_sfx(sim::SoundID::NavButtonClick);
            sim.try_hatch(local_player_id_, sim::AntType::Worker);
            return true;
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
                if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
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
            const sim::AntSnapshot* ally_target = nullptr;
            const auto& world = sim.get_world_state();
            // The ant under the cursor is picked with the original's rectangles (FUN_01026904 / FUN_01026a39)
            if (const sim::AntSnapshot* picked = pick_ant_at(world, world_x, world_y)) {
                if (picked->player_id != local_player_id_) {
                    if (sim.stats_manager().are_allies(local_player_id_, picked->player_id)) ally_target = picked;
                    else enemy_target = picked;
                }
            }
            if (enemy_target) {
                if (!has_friendly_selected(world)) {
                    play_sfx(sim::SoundID::AntStop);
                    return true;
                }
                if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                dispatch_attack_order(enemy_target->id, sim);
                return true;
            }
            if (ally_target && has_friendly_selected(world)) {
                if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                dispatch_move_to_unit_neighbor(ally_target->tile_x, ally_target->tile_y, sim);
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
                    if (has_friendly_selected(world)) {
                        if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
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
                    }
                } else {
                    if (has_friendly_selected(world)) {
                        if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                    }
                    bool has_thief = false;
                    for (uint32_t aid : selected_ant_ids_) {
                        for (const auto& a : world.ants) {
                            if (a.id == aid && a.player_id == local_player_id_ && a.type == sim::AntType::Thief) {
                                has_thief = true;
                                break;
                            }
                        }
                        if (has_thief) break;
                    }
                    if (has_thief) {
                        dispatch_smart_special_ability(world_x, world_y, sim, shift_held);
                    } else {
                        dispatch_move_order(target_tile_x, target_tile_y, sim, false);
                    }
                }
                return true;
            }

            // 3. Dispatch unit smart ability (Move for Worker/Queen, PlantBomb for Bomber, BuildBridge for Swimmer, IgniteFire for Fire, etc.)
            if (has_friendly_selected(world)) {
                if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
            }
            dispatch_smart_special_ability(world_x, world_y, sim, shift_held);
            return true;
        }
    }

    return false;
}

bool HUD::handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                          sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod) {
    if (button == SDL_BUTTON_RIGHT) {
        right_mouse_held_ = false;
    }
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

    if (show_match_start_modal_) {
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

        int32_t drag_threshold = has_friendly_selected(world) ? 10 : 4;
        bool is_small_drag = (dx <= 20 && dy <= 20);

        if (dx > drag_threshold || dy > drag_threshold) {
            // Authentic Marquee Box Selection
            int32_t x1 = camera.world_x + (std::min(drag_start_x_, drag_curr_x_) - PLAYFIELD_X);
            int32_t y1 = camera.world_y + (std::min(drag_start_y_, drag_curr_y_) - PLAYFIELD_Y);
            int32_t x2 = camera.world_x + (std::max(drag_start_x_, drag_curr_x_) - PLAYFIELD_X);
            int32_t y2 = camera.world_y + (std::max(drag_start_y_, drag_curr_y_) - PLAYFIELD_Y);

            if (is_small_drag && has_friendly_selected(world)) {
                bool hit_any_friendly = false;
                for (const auto& ant : world.ants) {
                    if (ant.player_id == local_player_id_ && ant.hp > 0 && !ant.is_drowning) {
                        if (ant.px >= x1 && ant.px <= x2 && ant.py >= y1 && ant.py <= y2) {
                            hit_any_friendly = true;
                            break;
                        }
                    }
                }
                if (!hit_any_friendly) {
                    // Accidental micro drag on empty ground during rapid clicking!
                    // Preserve selection and execute single-click order at (drag_start_x_, drag_start_y_)
                    goto execute_single_click;
                }
            }
            select_ants_in_rect(x1, y1, x2, y2, world, shift_held);
        } else {
        execute_single_click:
            // Single Click
            int32_t world_x = camera.world_x + (drag_start_x_ - PLAYFIELD_X);
            int32_t world_y = camera.world_y + (drag_start_y_ - PLAYFIELD_Y);
            int32_t target_tile_x = world_x / 32;
            int32_t target_tile_y = world_y / 32;

            // 1. Check if clicked directly on an ant (sprite bounding box OR tile match)
            const sim::AntSnapshot* hit_ant = pick_ant_at(world, world_x, world_y);

            if (hit_ant) {
                selected_base_team_id_ = -1;
                if (hit_ant->player_id == local_player_id_) {
                    // Friendly ant clicked: select single ant (shift_held enables multi-select mode)
                    select_ant(hit_ant->id, shift_held);
                    play_sfx(sim::get_ready_voice_sound(hit_ant->type, voice_variant_++));
                } else {
                    // Enemy or allied ant clicked
                    bool is_ally = sim.stats_manager().are_allies(local_player_id_, hit_ant->player_id);
                    if (has_friendly_selected(world) && !is_ally) {
                        // Issue Attack order against target enemy for selected friendly ants
                        if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                        dispatch_attack_order(hit_ant->id, sim);
                    } else if (has_friendly_selected(world) && is_ally) {
                        // Authentic ally click: spawn marker, walk to adjacent tile without attacking
                        if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                        dispatch_move_to_unit_neighbor(hit_ant->tile_x, hit_ant->tile_y, sim);
                    } else {
                        if (has_friendly_selected(world)) {
                            play_sfx(sim::SoundID::AntStop);
                        } else {
                            // If no friendly ant is selected, select the enemy ant for inspection
                            select_ant(hit_ant->id, false);
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
                    if (world.fog_of_war_enabled && base.team_id != local_player_id_) {
                        bool is_ally = sim.stats_manager().are_allies(local_player_id_, base.team_id);
                        if (!is_ally && !world.is_tile_revealed(target_tile_x, target_tile_y)) {
                            continue; // Cannot click enemy base hidden in fog
                        }
                    }
                    hit_base = &base;
                    break;
                }
            }

            if (hit_base) {
                if (has_friendly_selected(world)) {
                    if (hit_base->team_id == local_player_id_) {
                        // Friendly anthill: return selected friendly ants to base
                        if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                        const sim::AntSnapshot* first_ant = nullptr;
                        for (uint32_t aid : selected_ant_ids_) {
                            for (const auto& a : world.ants) {
                                if (a.id == aid && a.player_id == local_player_id_) {
                                    if (!first_ant) first_ant = &a;
                                    sim::AntOrder order;
                                    order.ant_id = aid;
                                    order.type = sim::OrderType::ReturnToBase;
                                    sim.issue_order(order);
                                    break;
                                }
                            }
                        }
                        if (first_ant) {
                            play_sfx(sim::get_move_voice_sound(first_ant->type, voice_variant_++));
                        }
                    } else {
                        // Enemy anthill clicked
                        bool has_thief = false;
                        const sim::AntSnapshot* first_friendly = nullptr;
                        for (uint32_t aid : selected_ant_ids_) {
                            for (const auto& a : world.ants) {
                                if (a.id == aid && a.player_id == local_player_id_) {
                                    if (!first_friendly) first_friendly = &a;
                                    if (a.type == sim::AntType::Thief) {
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
                        }
                        if (has_thief) {
                            if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                            play_sfx(sim::SoundID::ThiefGo);
                        } else if (first_friendly) {
                            if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
                            dispatch_move_order(target_tile_x, target_tile_y, sim, false);
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
                if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
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
    mouse_x_ = x;
    mouse_y_ = y;
    send_to_button_.is_hovered = send_to_button_.contains(x, y);
    team_button_.is_hovered = is_on_team_ && team_button_.contains(x, y);
    help_button_.is_hovered = help_button_.contains(x, y);
    options_button_.is_hovered = options_button_.contains(x, y);
    quit_button_.is_hovered = quit_button_.contains(x, y);

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
    // 0. Match Start Modal captures/blocks all keyboard events
    if (show_match_start_modal_) {
        return true;
    }

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
                // The original has no hatch key (Ctrl+H only selects the home hill).
                if (selected_base_team_id_ != local_player_id_) {
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
            } else if (cell.terrain_type == sim::TERRAIN_WATER || cell.surface_type == sim::SurfaceType::Water) {
                for (uint32_t aid : targets) {
                    sim::AntOrder order;
                    order.ant_id = aid;
                    order.type = sim::OrderType::BuildBridge;
                    order.target_x = target_tile_x;
                    order.target_y = target_tile_y;
                    sim.issue_order(order);
                }
                play_sfx(sim::get_ability_voice_sound(sim::AntType::Swimmer));
                return;
            }
        }
        play_sfx(sim::SoundID::CantGo);
        return;
    }

    if (active_order_mode_ == sim::OrderType::PlantBomb) {
        const auto& grid = sim.grid();
        if (grid.in_bounds({target_tile_x, target_tile_y})) {
            const auto& cell = grid.get_cell({target_tile_x, target_tile_y});
            if (cell.can_place_bomb() && cell.terrain_type != sim::TERRAIN_OBSTACLE && !cell.is_obstacle_overlay && cell.terrain_type != sim::TERRAIN_WATER) {
                for (uint32_t aid : targets) {
                    sim::AntOrder order;
                    order.ant_id = aid;
                    order.type = sim::OrderType::PlantBomb;
                    order.target_x = target_tile_x;
                    order.target_y = target_tile_y;
                    sim.issue_order(order);
                }
                play_sfx(sim::get_move_voice_sound(sim::AntType::Bomber, voice_variant_++));
                return;
            }
        }
        play_sfx(sim::SoundID::CantGo);
        return;
    }

    if (active_order_mode_ == sim::OrderType::IgniteFire) {
        const auto& grid = sim.grid();
        if (grid.in_bounds({target_tile_x, target_tile_y})) {
            const auto& cell = grid.get_cell({target_tile_x, target_tile_y});
            if (cell.can_place_fire() && cell.terrain_type != sim::TERRAIN_OBSTACLE && !cell.is_obstacle_overlay && cell.terrain_type != sim::TERRAIN_WATER) {
                for (uint32_t aid : targets) {
                    sim::AntOrder order;
                    order.ant_id = aid;
                    order.type = sim::OrderType::IgniteFire;
                    order.target_x = target_tile_x;
                    order.target_y = target_tile_y;
                    sim.issue_order(order);
                }
                play_sfx(sim::get_ability_voice_sound(sim::AntType::Fire));
                return;
            }
        }
        play_sfx(sim::SoundID::CantGo);
        return;
    }

    if (active_order_mode_ == sim::OrderType::InfiltrateAnthill) {
        const assets::AnthillSpawn* target_base = nullptr;
        for (const auto& base : world.anthills) {
            if (target_tile_x >= base.x && target_tile_x < base.x + 4 &&
                target_tile_y >= base.y && target_tile_y < base.y + 4) {
                target_base = &base;
                break;
            }
        }
        if (target_base && target_base->team_id != local_player_id_) {
            for (uint32_t aid : targets) {
                sim::AntOrder order;
                order.ant_id = aid;
                order.type = sim::OrderType::InfiltrateAnthill;
                order.target_x = target_tile_x;
                order.target_y = target_tile_y;
                sim.issue_order(order);
            }
            play_sfx(sim::SoundID::ThiefGo);
            return;
        }
        play_sfx(sim::SoundID::CantGo);
        return;
    }

    if (active_order_mode_ == sim::OrderType::Attack) {
        // The armed attack mode clicks an ant (FUN_010287b5, attack flag, at the picked ant's tile); with no enemy under the
        // cursor the click is an ordinary move.
        const sim::AntSnapshot* enemy = pick_ant_at(world, world_x, world_y);
        if (enemy && enemy->player_id != local_player_id_ && !sim.stats_manager().are_allies(local_player_id_, enemy->player_id)) {
            dispatch_attack_order(enemy->id, sim);
        } else {
            dispatch_move_order(target_tile_x, target_tile_y, sim);
        }
        return;
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

    // Original group order (Ants.exe FUN_010287b5): every selected ant is sent to the clicked tile, closest
    // first; team-mate claims and the goal ring scan spread the group over free tiles. Only the closest ant
    // answers ("On my way." voice), and only when its order queued a path.
    const uint32_t ack = sim.issue_group_move_order(targets, sim::TileCoord{target_tile_x, target_tile_y}, allow_friendly_bomb);
    if (ack != 0) {
        play_sfx(sim::get_move_voice_sound(sim.get_unit(ack).type, voice_variant_++));
    }
}

void HUD::dispatch_attack_order(uint32_t target_enemy_id, sim::SimulationEngine& sim) {
    const auto& world = sim.get_world_state();
    const sim::AntSnapshot* enemy = nullptr;
    for (const auto& a : world.ants) {
        if (a.id == target_enemy_id) {
            enemy = &a;
            break;
        }
    }
    // Cannot attack friendly teammates or allies
    if (enemy && (enemy->player_id == local_player_id_ || sim.stats_manager().are_allies(local_player_id_, enemy->player_id))) {
        play_sfx(sim::SoundID::AntStop);
        return;
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
    if (targets.empty() || !enemy) return;

    // Original group order (Ants.exe FUN_010287b5 with the attack flag): ants that already attack that tile are skipped, so a
    // repeated click changes nothing; the others are sent closest first; only the closest one answers with its voice, and only
    // when its order queued a path.
    const uint32_t ack = sim.issue_group_attack_order(targets, sim::TileCoord{enemy->tile_x, enemy->tile_y});
    if (ack != 0) {
        play_sfx(sim::get_attack_voice_sound(sim.get_unit(ack).type, voice_variant_++));
    }
}

const sim::AntSnapshot* HUD::pick_ant_at(const sim::WorldState& world, int32_t world_x, int32_t world_y) const {
    const int32_t tx = world_x / 32;
    const int32_t ty = world_y / 32;
    const sim::AntSnapshot* hit = nullptr;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (const auto& ant : world.ants) {
                if (ant.hp == 0 || ant.is_drowning) continue;
                if (ant.tile_x != tx + dx || ant.tile_y != ty + dy) continue;     // the occupant registered on the scanned tile
                if (world.fog_of_war_enabled && ant.player_id != local_player_id_) {
                    const bool is_ally = (local_player_id_ < world.player_alliances.size() &&
                                          ant.player_id < world.player_alliances.size() &&
                                          world.player_alliances[local_player_id_] == ant.player_id &&
                                          world.player_alliances[ant.player_id] == local_player_id_);
                    if (!is_ally && !world.is_tile_revealed(ant.tile_x, ant.tile_y)) continue;
                }
                const bool combat = (ant.type == sim::AntType::Combat);
                const int32_t left = ant.px - (combat ? 32 : 20);
                const int32_t right = ant.px + (combat ? 26 : 20);
                const int32_t top = ant.py - (combat ? 46 : 32);
                const int32_t bottom = ant.py + 16;
                if (world_x >= left && world_x < right && world_y >= top && world_y < bottom) hit = &ant;
            }
        }
    }
    return hit;
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
    const sim::TileCoord clicked{target_tile_x, target_tile_y};

    // A click on a food pile, a lunchbox or a power-up is the ordinary group order (FUN_010287b5): every ant is sent to the
    // clicked tile, closest first, and the classification of the original (FUN_01020655) turns the order of each ant into a
    // harvest or a pick-up. There are no per-ant slots around the object.
    if (grid.food_object_at_cell(clicked) >= 0 || grid.has_powerup_at(clicked)) {
        dispatch_move_order(target_tile_x, target_tile_y, sim, false);
        return;
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
        order.target_x = target_tile_x;
        order.target_y = target_tile_y;

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
                    play_sfx(sim::SoundID::CantGo);
                    order.type = sim::OrderType::None;
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

        if (order.type == sim::OrderType::None) {
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

void HUD::dispatch_move_to_unit_neighbor(int32_t target_tile_x, int32_t target_tile_y, sim::SimulationEngine& sim) {
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

    // Find reference position of primary moving unit
    int32_t ref_x = target_tile_x;
    int32_t ref_y = target_tile_y;
    for (const auto& a : world.ants) {
        if (a.id == targets[0]) {
            ref_x = a.tile_x;
            ref_y = a.tile_y;
            break;
        }
    }

    // Find the closest passable neighbor tile around the target unit
    const auto& grid = sim.grid();
    static const int32_t dx[8] = { 0,  1, 0, -1,  1, -1,  1, -1 };
    static const int32_t dy[8] = { -1, 0, 1,  0, -1, -1,  1,  1 };

    int32_t best_dist = INT32_MAX;
    sim::TileCoord best_neighbor{-1, -1};

    for (int i = 0; i < 8; ++i) {
        int32_t nx = target_tile_x + dx[i];
        int32_t ny = target_tile_y + dy[i];
        if (grid.in_bounds(nx, ny) && grid.get_cell(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)).is_passable()) {
            int32_t d = std::max(std::abs(nx - ref_x), std::abs(ny - ref_y));
            if (d < best_dist) {
                best_dist = d;
                best_neighbor = {nx, ny};
            }
        }
    }

    if (best_neighbor.x >= 0) {
        dispatch_move_order(best_neighbor.x, best_neighbor.y, sim, false);
    } else {
        dispatch_move_order(target_tile_x, target_tile_y, sim, false);
    }
}

CursorType HUD::evaluate_cursor(int32_t screen_x, int32_t screen_y,
                               const sim::WorldState& world,
                               const sim::Grid& grid,
                               const ViewportCamera& camera) const {
    // 0. Overlays and Modals: Normal cursor
    if (show_match_start_modal_ || show_options_ || show_quit_dialog_ || show_quick_help_) {
        current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // 1. Edge Panning Bounds Check (Matching Ants.exe 0x1026b09..0x1026d11)
    // Left <= 12, Right >= 628, Top <= 12, Bottom >= 468
    int32_t map_pw = static_cast<int32_t>(grid.width()) * 32;
    int32_t map_ph = static_cast<int32_t>(grid.height()) * 32;
    int32_t max_cam_x = std::max(0, map_pw - PLAYFIELD_WIDTH);
    int32_t max_cam_y = std::max(0, map_ph - PLAYFIELD_HEIGHT);

    bool can_n = (camera.world_y > 0);
    bool can_s = (camera.world_y < max_cam_y);
    bool can_w = (camera.world_x > 0);
    bool can_e = (camera.world_x < max_cam_x);

    bool at_left = (screen_x <= 12);
    bool at_right = (screen_x >= 628);
    bool at_top = (screen_y <= 12);
    bool at_bottom = (screen_y >= 468);

    if (at_left && at_top && (can_w || can_n)) {
        current_cursor_ = CursorType::ScrollNW;
        return current_cursor_;
    }
    if (at_right && at_top && (can_e || can_n)) {
        current_cursor_ = CursorType::ScrollNE;
        return current_cursor_;
    }
    if (at_right && at_bottom && (can_e || can_s)) {
        current_cursor_ = CursorType::ScrollSE;
        return current_cursor_;
    }
    if (at_left && at_bottom && (can_w || can_s)) {
        current_cursor_ = CursorType::ScrollSW;
        return current_cursor_;
    }
    if (at_top && can_n) {
        current_cursor_ = CursorType::ScrollN;
        return current_cursor_;
    }
    if (at_bottom && can_s) {
        current_cursor_ = CursorType::ScrollS;
        return current_cursor_;
    }
    if (at_left && can_w) {
        current_cursor_ = CursorType::ScrollW;
        return current_cursor_;
    }
    if (at_right && can_e) {
        current_cursor_ = CursorType::ScrollE;
        return current_cursor_;
    }

    // 2. Outside the map view rectangle (16,21)-(458,461) (Ants.exe 0x1026d6a): plain pointer
    if (screen_x < 16 || screen_x >= 458 || screen_y < 21 || screen_y >= 461) {
        current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // 2.5 While a selection box wider or taller than 4 px is being dragged the pointer stays normal (0x1026d98)
    if (is_dragging_ && (std::abs(drag_curr_x_ - drag_start_x_) > 4 || std::abs(drag_curr_y_ - drag_start_y_) > 4)) {
        current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // 3. Over Playfield: Convert to world coordinates
    int32_t world_x = camera.world_x + (screen_x - PLAYFIELD_X);
    int32_t world_y = camera.world_y + (screen_y - PLAYFIELD_Y);
    int32_t tx = world_x / 32;
    int32_t ty = world_y / 32;

    // While a selected bomber is actively placing a bomb, cursor remains locked to Target reticle (c_targ1)
    for (uint32_t aid : selected_ant_ids_) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.type == sim::AntType::Bomber) {
                if (a.state == sim::UnitState::PlantingBomb || a.anim_state == static_cast<uint16_t>(sim::UnitState::PlantingBomb)) {
                    current_cursor_ = CursorType::Target;
                    return current_cursor_;
                }
                break;
            }
        }
    }
    if (selected_ant_id_ != 0) {
        for (const auto& a : world.ants) {
            if (a.id == selected_ant_id_ && a.player_id == local_player_id_ && a.type == sim::AntType::Bomber) {
                if (a.state == sim::UnitState::PlantingBomb || a.anim_state == static_cast<uint16_t>(sim::UnitState::PlantingBomb)) {
                    current_cursor_ = CursorType::Target;
                    return current_cursor_;
                }
                break;
            }
        }
    }

    bool tile_revealed = world.is_tile_revealed(tx, ty);
    bool has_friendly = has_friendly_selected(world);

    // Fog of War concealment matching Ants.exe (FUN_01026aa3 lines 28356/28373 via FUN_01009825):
    // If the hovered tile is unrevealed in Fog of War, cursor NEVER reveals enemy units, food, or enemy bases.
    if (world.fog_of_war_enabled && !tile_revealed) {
        if (has_friendly) {
            current_cursor_ = CursorType::Move;
        } else {
            current_cursor_ = CursorType::Normal;
        }
        return current_cursor_;
    }

    // Check if hovering over any alive ant
    const sim::AntSnapshot* hover_ant = pick_ant_at(world, world_x, world_y);

    // Check if hovering over an Anthill base
    const assets::AnthillSpawn* hover_base = nullptr;
    for (const auto& base : world.anthills) {
        if (tx >= base.x && tx < base.x + 4 &&
            ty >= base.y && ty < base.y + 4) {
            if (world.fog_of_war_enabled && base.team_id != local_player_id_) {
                bool is_ally = (local_player_id_ < world.player_alliances.size() &&
                                base.team_id < world.player_alliances.size() &&
                                world.player_alliances[local_player_id_] == base.team_id &&
                                world.player_alliances[base.team_id] == local_player_id_);
                if (!is_ally && !tile_revealed) {
                    continue;
                }
            }
            hover_base = &base;
            break;
        }
    }

    // Case A: NO friendly ants selected
    if (!has_friendly) {
        if (hover_ant || hover_base) {
            current_cursor_ = CursorType::Select;
            return current_cursor_;
        }
        current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // Case B: Friendly ants ARE selected (Command / Target mode)
    // 0. If an explicit order mode is active (from HUD button or hotkey)
    if (active_order_mode_ != sim::OrderType::None) {
        if (active_order_mode_ == sim::OrderType::Attack) {
            bool is_ally = hover_ant && (local_player_id_ < world.player_alliances.size() &&
                                         hover_ant->player_id < world.player_alliances.size() &&
                                         world.player_alliances[local_player_id_] == hover_ant->player_id &&
                                         world.player_alliances[hover_ant->player_id] == local_player_id_);
            if (hover_ant && hover_ant->player_id != local_player_id_ && !is_ally) {
                current_cursor_ = CursorType::Attack;
            } else {
                current_cursor_ = CursorType::Normal;
            }
            return current_cursor_;
        }
        if (active_order_mode_ == sim::OrderType::InfiltrateAnthill) {
            if (hover_base && hover_base->team_id != local_player_id_) {
                current_cursor_ = CursorType::ThiefTarget;
            } else {
                current_cursor_ = CursorType::Normal;
            }
            return current_cursor_;
        }
        if (active_order_mode_ == sim::OrderType::BuildBridge) {
            if (grid.in_bounds(tx, ty)) {
                const auto& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                if ((cell.terrain_type == sim::TERRAIN_WATER || cell.surface_type == sim::SurfaceType::Water) && !cell.has_completed_bridge()) {
                    current_cursor_ = CursorType::Move;
                    return current_cursor_;
                } else if (cell.has_completed_bridge() || cell.has_partial_bridge()) {
                    current_cursor_ = CursorType::Move;
                    return current_cursor_;
                }
            }
            current_cursor_ = CursorType::Normal;
            return current_cursor_;
        }
        if (active_order_mode_ == sim::OrderType::PlantBomb) {
            if (grid.in_bounds(tx, ty)) {
                const auto& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                if (cell.can_place_bomb() && cell.terrain_type != sim::TERRAIN_OBSTACLE && !cell.is_obstacle_overlay && cell.terrain_type != sim::TERRAIN_WATER) {
                    current_cursor_ = CursorType::Target;
                    return current_cursor_;
                }
            }
            current_cursor_ = CursorType::Normal;
            return current_cursor_;
        }
        if (active_order_mode_ == sim::OrderType::IgniteFire) {
            if (grid.in_bounds(tx, ty)) {
                const auto& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                if (cell.can_place_fire() && cell.terrain_type != sim::TERRAIN_OBSTACLE && !cell.is_obstacle_overlay && cell.terrain_type != sim::TERRAIN_WATER) {
                    current_cursor_ = CursorType::Move;
                    return current_cursor_;
                }
            }
            current_cursor_ = CursorType::Normal;
            return current_cursor_;
        }
    }

    // Check if selected ant is a Bomber or is actively planting a bomb
    bool has_bomber = false;
    bool is_planting_bomb = false;
    for (uint32_t aid : selected_ant_ids_) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.type == sim::AntType::Bomber) {
                has_bomber = true;
                if (a.anim_state == static_cast<uint16_t>(sim::UnitState::PlantingBomb)) {
                    is_planting_bomb = true;
                }
                break;
            }
        }
        if (has_bomber) break;
    }

    // While a bomber is placing a bomb, cursor remains locked to Target reticle
    if (is_planting_bomb) {
        current_cursor_ = CursorType::Target;
        return current_cursor_;
    }

    // If hovering over a bomb on the grid:
    // Authentic 1998 parity: Only a single selected unshifted Bomber capable of defusing
    // displays the Target reticle. Non-bombers, multi-select, or shift-held display the regular Move cursor.
    if (grid.in_bounds(tx, ty) && grid.has_bomb_at({tx, ty})) {
        bool can_defuse_bomb = has_bomber && !is_shift_held() && !is_multi_select() && selected_ant_ids_.size() == 1;
        if (can_defuse_bomb) {
            current_cursor_ = CursorType::Target;
            return current_cursor_;
        }
    }

    // If right mouse is held and a bomber is selected, show Target reticle over valid bomb placement tiles
    if (right_mouse_held_ && has_bomber && grid.in_bounds(tx, ty)) {
        const auto& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
        if (cell.can_place_bomb() && cell.terrain_type != sim::TERRAIN_OBSTACLE && !cell.is_obstacle_overlay && cell.terrain_type != sim::TERRAIN_WATER) {
            current_cursor_ = CursorType::Target;
            return current_cursor_;
        }
    }

    // Check if selected ant is a Thief
    bool has_thief = false;
    for (uint32_t aid : selected_ant_ids_) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.type == sim::AntType::Thief) {
                has_thief = true;
                break;
            }
        }
        if (has_thief) break;
    }

    // 1. Food Check: a cell of a food object (pile or lunchbox)
    if (grid.food_object_at_cell(sim::TileCoord{tx, ty}) >= 0) {
        current_cursor_ = CursorType::Food;
        return current_cursor_;
    }

    // 2. Base Check
    if (hover_base) {
        if (hover_base->team_id != local_player_id_) {
            // Enemy base: if Thief selected -> ThiefTarget (c_targ1), else Select
            if (has_thief) {
                current_cursor_ = CursorType::ThiefTarget;
                return current_cursor_;
            } else {
                current_cursor_ = CursorType::Select;
                return current_cursor_;
            }
        } else {
            // Friendly base -> Move into base
            current_cursor_ = CursorType::Move;
            return current_cursor_;
        }
    }

    // 3. Unit Hover Check
    if (hover_ant) {
        if (hover_ant->player_id == local_player_id_) {
            // Friendly ant -> Select
            current_cursor_ = CursorType::Select;
            return current_cursor_;
        } else {
            bool is_ally = (local_player_id_ < world.player_alliances.size() &&
                            hover_ant->player_id < world.player_alliances.size() &&
                            world.player_alliances[local_player_id_] == hover_ant->player_id &&
                            world.player_alliances[hover_ant->player_id] == local_player_id_);
            if (is_ally) {
                // Teammate / Ally ant -> Move towards without attacking
                current_cursor_ = CursorType::Move;
                return current_cursor_;
            }
            // Enemy ant -> Attack!
            current_cursor_ = CursorType::Attack;
            return current_cursor_;
        }
    }

    // 4. Map Bounds Check: within playfield map -> Move; outside -> Normal
    if (grid.in_bounds(tx, ty)) {
        current_cursor_ = CursorType::Move;
        return current_cursor_;
    }

    current_cursor_ = CursorType::Normal;
    return current_cursor_;
}

} // namespace ants::app

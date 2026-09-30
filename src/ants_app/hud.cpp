#include "ants_app/hud.hpp"
#include <cstdio>
#include "ants_sim/game_strings.hpp"
#include "ants_sim/prng.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"
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
    slot_latched_[0] = slot_latched_[1] = false;
    btnpush_until_ms_[0] = btnpush_until_ms_[1] = btnpush_until_ms_[2] = 0;
    input_lock_ticks_ = 0;
    pending_deselect_ = false;
    right_capture_ = 0;
    is_dragging_ = false;
    is_radar_dragging_ = false;
    status_line_.clear();
    selection_status_pending_ = false;
    selection_status_quiet_ = false;
    chat_log_.clear();
    chat_line_colour_.clear();
    chat_line_indent_.clear();
    chat_scroll_offset_ = 0;
    post_status_id(sim::strings::kWelcome, "Ants");    // FUN_0100dbe2 0x100e173, once when the match screen is built
    add_news_flash(0, "Game started! Go get that food!");     // FUN_01022432: "[0:00] News Flash:", the start message

    // Configure Top Header Buttons (x0y0.bmp)
    help_button_ = {476, 7, 46, 23, 0, 0, 0, false, true, false};
    options_button_ = {525, 7, 52, 23, 0, 0, 0, false, true, false};
    quit_button_ = {579, 7, 46, 23, 0, 0, 0, false, true, false};

    // The three pedestal slots of the original (FUN_01028d30, half-open): slot 1 (Move / hatch / ally) (482, 152) - (525, 225),
    // slot 2 (ability) (539, 152) - (582, 225), slot 3 (Stop) (597, 189) - (628, 227)
    move_pedestal_button_ = {482, 152, 43, 73, 0, 0, 0, false, true, false};
    ability_pedestal_button_ = {539, 152, 43, 73, 0, 0, 0, false, true, false};
    stop_button_ = {597, 189, 31, 38, 0, 0, 0, false, true, false};

    // Configure Authentic Send-to Toggle Button at (532, 443, 44, 24)
    send_to_button_ = {532, 443, 44, 24, 0, 0, 0, false, true, false};
    // Configure Authentic Team Toggle Button at (579, 443, 46, 24)
    team_button_    = {579, 443, 46, 24, 0, 0, 0, false, true, false};
    is_on_team_ = false;
    chat_input_.clear();
    cursor_blink_ticks_ = 0;

    // Configure Quit Confirmation Dialog Buttons
    yes_button_ = {180, 260, 49, 24, 0, 0, 0, false, true, false}; // dyn_byes1 part (80,160) + origin (100,100)
    no_button_  = {292, 260, 49, 24, 0, 0, 0, false, true, false}; // dyn_bno1 part (192,160) + origin (100,100)

    show_match_start_modal_ = false;
    match_start_modal_ticks_ = 0;
    show_quit_dialog_ = false;
    show_quick_help_ = false;
    show_options_ = false;

    // The hatch and ally pedestals are slot 1 as well
    hatch_button_.x = 482;
    hatch_button_.y = 152;
    hatch_button_.w = 43;
    hatch_button_.h = 73;
    hatch_button_.sprite_up = 2683;    // buthatup.bmp
    hatch_button_.sprite_down = 2684;  // buthatd.bmp
    hatch_button_.sprite_label = 2682; // labhatch.bmp

    team_up_button_.x = 482;
    team_up_button_.y = 152;
    team_up_button_.w = 43;
    team_up_button_.h = 73;
    team_up_button_.sprite_up = 2576;    // butdipu.bmp
    team_up_button_.sprite_down = 2587;  // butdipd.bmp
    team_up_button_.sprite_label = 2575; // labdib.bmp
    team_up_button_.is_enabled = true;
    team_up_button_.is_pressed = false;
    team_up_button_.is_active = false;
}

void HUD::reset() {
    init(local_player_id_);
}

void HUD::update(const sim::WorldState& world, uint32_t delta_ticks) {
    // 0. Match Start Modal Countdown (5.0 s / 100 ticks at 20 Hz: first timer tick of Ants.exe 0x1017127)
    if (show_match_start_modal_) {
        match_start_modal_ticks_ += delta_ticks;
        if (match_start_modal_ticks_ >= MATCH_START_MODAL_DURATION_TICKS) {
            show_match_start_modal_ = false;
        }
    }

    // The input lock after a Stop (FUN_01028bdd): 250 ms without mouse input, then everything is deselected (FUN_01028c44(0))
    if (input_lock_ticks_ > 0) {
        input_lock_ticks_ -= std::min(input_lock_ticks_, delta_ticks);
        if (input_lock_ticks_ == 0 && pending_deselect_) {
            pending_deselect_ = false;
            clear_selection();
            unlatch_pedestals();
        }
    }

    // 1. The status line (CLEARSTAT and TXTFLASH tasks) and the text that a selection change decides
    status_line_.update(delta_ticks);
    apply_selection_status(world);
    check_selected_type_change(world);

    // 3. Cursor blink ticks and alliance team status
    cursor_blink_ticks_ += delta_ticks;
    is_on_team_ = (local_player_id_ < world.player_alliances.size() &&
                   world.player_alliances[local_player_id_] < sim::MAX_PLAYERS &&
                   world.player_alliances[local_player_id_] != local_player_id_);
}

void HUD::poll_sim_events(sim::SimulationEngine& sim) {
    auto news = sim.poll_news_events();
    for (const auto& ev : news) {
        if (ev.target_player != 255 && ev.target_player != local_player_id_) continue;
        if (ev.channel == sim::NewsChannel::ChatLog) {
            add_news_flash(ev.timestamp_ms, ev.message_text);
        } else if (ev.channel == sim::NewsChannel::Dialog) {
            // the invitation dialog belongs to the network stage: the match screen has no window for it yet
        } else {
            status_line_.post(ev.message_text, ev.blink);
        }
    }
}

uint32_t HUD::voice_rand() noexcept {
    voice_seed_ = voice_seed_ * 1103515245u + 12345u;
    return (voice_seed_ >> 16) & 0x7FFFu;
}

void HUD::voice_ready(sim::AntType type) {
    play_sfx(sim::get_ready_voice_sound(type, voice_rand()));
}

void HUD::voice_go(sim::AntType type) {
    play_sfx(sim::get_move_voice_sound(type, voice_rand()));
    post_status_id(type == sim::AntType::Combat ? sim::strings::kMovinOut
                   : type == sim::AntType::Thief ? sim::strings::kHereIGo : sim::strings::kOnMyWay);
}

void HUD::voice_attack(sim::AntType type) {
    play_sfx(sim::get_attack_voice_sound(type, voice_rand()));
    post_status_id(sim::strings::kAttack);
}

// FUN_0101b78a: bomber and swimmer answer with their voice only, a thief (69) and a fire ant (71) also post a text, worker and
// combat ant say nothing; a special order that goes to more than one ant is silent altogether.
void HUD::voice_special(sim::AntType type, size_t ants_ordered) {
    if (ants_ordered != 1) return;
    const uint32_t sound = sim::get_ability_voice_sound(type);
    if (sound == sim::NoVoice) return;
    play_sfx(sound);
    if (type == sim::AntType::Thief) post_status_id(sim::strings::kMyPleasure);
    else if (type == sim::AntType::Fire) post_status_id(sim::strings::kBurn);
}

void HUD::post_status_id(uint16_t string_id, const std::string& arg) {
    status_line_.post(sim::strings::format(string_id, arg), sim::strings::blinks(string_id));
}

// SetPanelMode (Ants.exe FUN_01027f07) posts its text when the selection is rebuilt: exactly one own ant gives the text of its type
// (6 worker, 7 bomber, 8 fire ant, 9 thief, 10 combat ant, 11 swimmer), more than one gives 12, anything else (an enemy ant, the
// own hill, empty ground) clears the line. Quiet changes keep the text.
void HUD::apply_selection_status(const sim::WorldState& world) {
    if (!selection_status_pending_) return;
    selection_status_pending_ = false;
    if (selection_status_quiet_) {
        selection_status_quiet_ = false;
        return;
    }
    size_t own = 0;
    const sim::AntSnapshot* only = nullptr;
    if (selected_base_team_id_ < 0) {
        for (uint32_t id : selected_ant_ids_) {
            for (const auto& a : world.ants) {
                if (a.id == id && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                    ++own;
                    only = &a;
                    break;
                }
            }
        }
    }
    if (own == 0) {
        status_line_.clear();
    } else if (own > 1 || !only) {
        post_status_id(sim::strings::kSelMany);
    } else {
        static const uint16_t kByType[6] = {sim::strings::kSelWorker, sim::strings::kSelBomber, sim::strings::kSelFire,
                                            sim::strings::kSelThief, sim::strings::kSelCombat, sim::strings::kSelSwimmer};
        const size_t t = static_cast<size_t>(only->type);
        if (t < 6) post_status_id(kByType[t]); else status_line_.clear();
    }
}

void HUD::check_selected_type_change(const sim::WorldState& world) {
    std::vector<std::pair<uint32_t, sim::AntType>> now;
    bool changed = false;
    if (selected_base_team_id_ < 0) {
        for (uint32_t id : selected_ant_ids_) {
            for (const auto& a : world.ants) {
                if (a.id != id || a.player_id != local_player_id_ || a.hp == 0 || a.is_drowning) continue;
                now.emplace_back(id, a.type);
                for (const auto& before : selected_types_) {
                    if (before.first == id && before.second != a.type) changed = true;
                }
                break;
            }
        }
    }
    selected_types_ = std::move(now);
    if (!changed) return;
    if (selected_types_.size() == 1) {
        static const uint16_t kByType[6] = {sim::strings::kSelWorker, sim::strings::kSelBomber, sim::strings::kSelFire,
                                            sim::strings::kSelThief, sim::strings::kSelCombat, sim::strings::kSelSwimmer};
        const size_t t = static_cast<size_t>(selected_types_.front().second);
        if (t < 6) post_status_id(kByType[t]);
    } else {
        post_status_id(sim::strings::kSelMany);
    }
}

// The status box (label rect (481, 254) - (620, 266)): steady colour (79, 0, 143), left aligned, clipped at 139 px, hidden on the
// odd 50 ms steps while a flash runs.
void HUD::render_status_line(IRenderer& renderer) const {
    if (!status_line_.visible()) return;
    std::string text = status_line_.text();
    while (text.size() > 1 && renderer.get_text_width(text, FontSize::Px12) > 139) text.pop_back();
    renderer.draw_text(text, 481, 254, ants::assets::ColorRGBA{79, 0, 143, 255}, FontSize::Px12);
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
    const uint32_t now_ms = ticks_fn_ ? ticks_fn_() : SDL_GetTicks();
    auto flashing = [&](int slot) { return is_pedestal_flashing(slot); };           // BTNPUSH: pressed for 125 ms after an order
    PedestalKind left_kind = PedestalKind::Hidden;
    int left_mode = 1;
    PedestalKind right_kind = PedestalKind::Hidden;
    int right_mode = 1;
    if (selected_base_team_id_ >= 0) {
        if (selected_base_team_id_ == local_player_id_) {
            // Home anthill (Ants.exe FUN_01027f07 mode 2): hatch pedestal ("egg" kind) only while eggs remain
            if (eggs_now > 0) { left_kind = PedestalKind::Egg; left_mode = (hatch_button_.is_pressed || flashing(0)) ? 2 : 1; }
        } else {
            // Another player's hill: the ally pedestal, only with more than two players and while not allied with its owner
            const bool allied = local_player_id_ < world.player_alliances.size() && world.player_alliances[local_player_id_] == selected_base_team_id_;
            if (world.anthills.size() > 2 && !allied) {
                left_kind = PedestalKind::Ally;
                left_mode = (team_up_button_.is_pressed || flashing(0)) ? 2 : 1;
            }
        }
    } else if (has_friendly_ants) {
        left_kind = PedestalKind::Move;
        left_mode = (slot_latched_[0] || move_pedestal_button_.is_pressed || flashing(0)) ? 2 : 1;
        // The ability pedestal of the selected ants' common type (worker and mixed selections have none)
        sim::AntType common = sim::AntType::Worker;
        if (homogeneous_type(world, common)) {
            switch (common) {
                case sim::AntType::Swimmer: right_kind = PedestalKind::Swim; break;
                case sim::AntType::Fire:    right_kind = PedestalKind::Fire; break;
                case sim::AntType::Combat:  right_kind = PedestalKind::Attack; break;
                case sim::AntType::Bomber:  right_kind = PedestalKind::Bomb; break;
                case sim::AntType::Thief:   right_kind = PedestalKind::Thief; break;
                default: break;
            }
            if (right_kind != PedestalKind::Hidden) {
                right_mode = (slot_latched_[1] || ability_pedestal_button_.is_pressed || flashing(1)) ? 2 : 1;
            }
        }
    }
    // Both slots play the original rise / sink / icon-swap / press chains in real time (FUN_01028360)
    left_pedestal_.request(assets, left_kind, left_mode, now_ms);
    right_pedestal_.request(assets, right_kind, right_mode, now_ms);
    left_pedestal_.draw(renderer, assets, now_ms);
    right_pedestal_.draw(renderer, assets, now_ms);
    // The glow follows the cursor mode alone (FUN_010285f0): move / food over slot 1, target over slot 2
    if (left_pedestal_.is_settled_and_visible() && left_pedestal_.resting_kind() == PedestalKind::Move &&
        (current_cursor_ == CursorType::Move || current_cursor_ == CursorType::Food)) {
        render_pedestal_glow(renderer, assets, 1);
    }
    if (right_pedestal_.is_settled_and_visible() && current_cursor_ == CursorType::Target) {
        render_pedestal_glow(renderer, assets, 2);
    }

    if (selected_base_team_id_ >= 0) {
        if (selected_base_team_id_ == local_player_id_) {
            // Egg tray egg<N> (N = min(eggs, 9)) and the Stop button; both are animations with absolute part coordinates
            if (eggs_now > 0) {
                const std::string tray = "egg" + std::to_string(std::min<uint32_t>(eggs_now, 9u));
                draw_animation_frame0(renderer, assets, tray.c_str(), 0, 0);
            }
            draw_animation_frame0(renderer, assets, (stop_button_.is_pressed || flashing(2)) ? "butcand" : "butcanu", 0, 0);
        }
        // (an enemy base shows the status box only: the ally pedestal is the left slot)
    } else {
        if (has_friendly_ants) {
            // Stop button (animation butcanu / butcand: label at (595,180), button at (595,198))
            draw_animation_frame0(renderer, assets, (stop_button_.is_pressed || flashing(2)) ? "butcand" : "butcanu", 0, 0);

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
    }

    // The one-line status box (the recessed box wstatus.bmp is part of the uishell composite)
    render_status_line(renderer);

    // 2.3 Lower Panel: Always render Chat Section
    // Cursive embossed Chat header at (480, 266)

    // White chat history log wchat.bmp (143x103) at (479, 298): the view (482, 299) - (620, 400) shows 12 px lines, headers in
    // the colour of their team (news flashes (79, 0, 143)), bodies in (7, 11, 15) indented by 12 px
    {
        static const assets::ColorRGBA kChatColours[6] = {
            {39, 39, 59, 255}, {43, 39, 107, 255}, {119, 0, 0, 255}, {7, 67, 47, 255}, {79, 0, 143, 255}, {7, 11, 15, 255}};
        int32_t cty = 299;
        const int32_t total_lines = static_cast<int32_t>(chat_log_.size());
        const int32_t max_scroll = std::max(0, total_lines - kChatVisibleLines);
        chat_scroll_offset_ = std::clamp(chat_scroll_offset_, 0, max_scroll);
        const int32_t start_cidx = (total_lines > kChatVisibleLines) ? (total_lines - kChatVisibleLines - chat_scroll_offset_) : 0;
        const int32_t end_cidx = std::min(total_lines, start_cidx + kChatVisibleLines);
        for (int32_t i = start_cidx; i < end_cidx; ++i) {
            const size_t li = static_cast<size_t>(i);
            const int32_t indent = chat_line_indent_[li];
            std::string text = chat_log_[li];
            while (text.size() > 1 && renderer.get_text_width(text, FontSize::Px12) > 138 - indent) text.pop_back();   // clipped by its box
            renderer.draw_text(text, 482 + indent, cty, kChatColours[std::min<size_t>(chat_line_colour_[li], 5)], FontSize::Px12);
            cty += 12;
        }
    }

    // Ant relief horizontal divider bar x480y400.bmp (141x24) at (480, 400)

    // Chat text input box wtype.bmp (143x14) at (479, 423): the edit control is always active (caret blinks) while chat is on
    std::string input_display = chat_input_;
    if (input_display.length() > 25) {
        input_display = input_display.substr(input_display.length() - 25);
    }
    if ((cursor_blink_ticks_ / 15) % 2 == 0) {
        input_display += "_";
    }
    renderer.draw_text(input_display, 484, 423, {20, 50, 40, 255});
    // Chat switched off in the options: chatcovr (three chcovr2 tiles at (478,421/436/445)) covers the input box
    if (!chat_enabled_) {
        draw_animation_frame0(renderer, assets, "chatcovr", 0, 0);
    }

    // "Send to:" buttons (animations butall*, butals*): up / hover ("r" label over the up art) / pressed. [All] is hidden while chat is off,
    // [Team] exists only while the local player has an ally.
    auto send_button = [&](bool down, bool hovered, const char* up, const char* hover, const char* pressed) {
        draw_animation_frame0(renderer, assets, down ? pressed : (hovered ? hover : up));
    };
    if (chat_enabled_) {
        send_button(send_to_button_.is_pressed, send_to_button_.is_hovered, "butallu", "butallr", "butalld");
        if (is_on_team_) send_button(team_button_.is_pressed, team_button_.is_hovered, "butalsu", "butalsr", "butalsd");
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
    // a right aligned label of 14 px letters in its box [312, 399): what does not fit is cut off on the left (DT_RIGHT in a clipped box)
    while (my_label.size() > 1 && renderer.get_text_width(my_label, FontSize::Px14) > 399 - 312) my_label.erase(0, 1);
    int32_t label_w = renderer.get_text_width(my_label, FontSize::Px14);
    int32_t label_h = renderer.get_text_height(FontSize::Px14);
    int32_t label_x = 399 - label_w;
    int32_t label_y = 4 + (14 - label_h) / 2;
    renderer.draw_text(my_label, label_x, label_y, {255, 255, 255, 255}, FontSize::Px14);

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
        if (p != local_player_id_ && ((roster_mask_ >> p) & 1u) != 0) other_players.push_back(p);       // a team without a player has no label
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
        std::string name = (p < 4) ? (team_names_[p].empty() ? std::string(TEAM_NAMES[p]) : team_names_[p]) : std::string("AI");
        if (name.size() > 15) name = name.substr(0, 15);
        std::string p_label = name + ":";
        while (p_label.size() > 1 && renderer.get_text_width(p_label, FontSize::Px14) > slot.label_right - slot.label_left) p_label.erase(0, 1);
        int32_t label_w = renderer.get_text_width(p_label, FontSize::Px14);
        int32_t label_h = renderer.get_text_height(FontSize::Px14);
        int32_t label_x = slot.label_right - label_w;
        int32_t label_y = 464 + (14 - label_h) / 2;
        renderer.draw_text(p_label, label_x, label_y, {255, 255, 255, 255}, FontSize::Px14);

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

    // Prompt (string 99): a wrapped, centred label of 24 px letters in the rect (130,180) 260x160, colour (31,23,51) (FUN_010142cb: label (30,80),
    // 260x160, height 24, centred, moved by (100,100) with the dialog)
    draw_label(renderer, "Do you really want to quit?", 130, 180, 260, ColorRGBA{31, 23, 51, 255}, FontSize::Px24, true);

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

    // Label: "Get ready to play!  You are the <Colour> Ants." (string 105): the original's wrapped, centred label of 35 px letters in the box
    // (30, 10) 240 x 160 of the dialog (FUN_01017127: FUN_0102b0b5 wraps it, DrawTextA centres every line, the lines are one cell height apart)
    static const char* TEAM_NAMES[4] = {"Green", "Red", "Blue", "Black"};
    const std::string label = std::string("Get ready to play!  You are the ") + TEAM_NAMES[local_player_id_ % 4] + " Ants.";
    draw_label(renderer, label, mx + 30, my + 10, 240, text_color, FontSize::Px35, true);

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

    // Footer: "Waiting for others..." (string 104): a centred label of 24 px letters in the box (30, 190) 240 x 20
    draw_label(renderer, "Waiting for others...", mx + 30, my + 190, 240, text_color, FontSize::Px24, true);
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
    // FUN_0102653f (0x1026699..0x1026883): the band exists while the left button is held on the map and no pedestal is latched; it is drawn as a
    // 1 px frame in (255, 0, 0) (GDI FrameRect with the brush 0x0000FF)
    if (slot_latched_[0] || slot_latched_[1]) return;
    const BandRect b = band_rect();
    renderer.draw_rect(b.left, b.top, b.right - b.left, b.bottom - b.top, {255, 0, 0, 255});
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
    selection_status_pending_ = true;
    selection_status_quiet_ = false;
}

void HUD::select_base(int32_t team_id) noexcept {
    selected_base_team_id_ = team_id;
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    is_multi_select_mode_ = false;
    selection_status_pending_ = true;
    selection_status_quiet_ = false;
}

void HUD::clear_selection() noexcept {
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    is_multi_select_mode_ = false;
    selection_status_pending_ = true;      // every deselect clears the status text (FUN_01028c44)
    selection_status_quiet_ = false;
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
    // Ctrl+A (0x1026237): FUN_01028c44(0), then every ant of the player's own table is added (no state filter); panel 3 for one, 4 for several,
    // and the ready voice of the first one
    selected_base_team_id_ = -1;
    selected_ant_ids_.clear();
    for (const auto& ant : world.ants) {
        if (ant.player_id == local_player_id_) selected_ant_ids_.push_back(ant.id);
    }
    unlatch_pedestals();
    is_multi_select_mode_ = (selected_ant_ids_.size() > 1);
    if (!selected_ant_ids_.empty()) {
        selected_ant_id_ = selected_ant_ids_.front();
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_) {
                voice_ready(ant.type);
                break;
            }
        }
    } else {
        selected_ant_id_ = 0;
    }
    selection_status_pending_ = true;
    selection_status_quiet_ = false;
}

void HUD::select_ants_in_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const sim::WorldState& world, bool additive) {
    // FUN_01027530 (drag select): the local team's ants, no state filter, whose hit box (FUN_01026a39) overlaps the rectangle with a positive
    // area (IntersectRect + non-empty test). The rectangle is [left, right) x [top, bottom) in map pixels.
    const int32_t rl = std::min(x1, x2);
    const int32_t rr = std::max(x1, x2);
    const int32_t rt = std::min(y1, y2);
    const int32_t rb = std::max(y1, y2);
    std::vector<uint32_t> picked;
    for (const auto& ant : world.ants) {
        if (ant.player_id != local_player_id_) continue;
        const bool combat = (ant.type == sim::AntType::Combat);
        const int32_t al = ant.px - (combat ? 32 : 20);
        const int32_t ar = ant.px + (combat ? 26 : 20);
        const int32_t at = ant.py - (combat ? 46 : 32);
        const int32_t ab = ant.py + 16;
        if (std::max(rl, al) < std::min(rr, ar) && std::max(rt, at) < std::min(rb, ab)) picked.push_back(ant.id);
    }

    // Shift adds only to a selection of own ants (panels 3 and 4): nothing is cleared, nothing is said, an empty pick changes nothing
    const PanelMode panel = panel_mode(world);
    if (additive && (panel == PanelMode::OneAnt || panel == PanelMode::Ants)) {
        if (picked.empty()) return;
        for (uint32_t id : picked) {
            if (std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), id) == selected_ant_ids_.end()) selected_ant_ids_.push_back(id);
        }
        selected_base_team_id_ = -1;
        selected_ant_id_ = selected_ant_ids_.front();
        is_multi_select_mode_ = selected_ant_ids_.size() > 1;
        selection_status_pending_ = true;
        selection_status_quiet_ = true;                    // shift-add (0x1027950) keeps the text
        unlatch_pedestals();
        return;
    }

    // Otherwise the old selection is cleared first, even when nothing is picked (dragging over empty ground deselects)
    selected_ant_ids_ = std::move(picked);
    selected_base_team_id_ = -1;
    selected_ant_id_ = selected_ant_ids_.empty() ? 0 : selected_ant_ids_.front();
    is_multi_select_mode_ = selected_ant_ids_.size() > 1;
    selection_status_pending_ = true;
    selection_status_quiet_ = false;
    unlatch_pedestals();
    if (selected_ant_id_ != 0) {
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_) {
                voice_ready(ant.type);                     // the first ant answers (FUN_0101b5f9)
                break;
            }
        }
    }
}

// =========================================================================
// Input Dispatcher
// =========================================================================

bool HUD::handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                            sim::SimulationEngine& sim, ViewportCamera& camera, [[maybe_unused]] uint16_t mod) {
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

    // ProcessInput (0x102737e) ignores mouse events while the input is locked (after Stop, FUN_01028bdd) and while the cursor is a scroll
    // arrow (mode 6); the strip test does not run while a button is captured
    if (input_lock_ticks_ > 0) return true;
    if (!is_input_captured() &&
        edge_scroll_step(x, y, 0, camera.world_x, camera.world_y, static_cast<int32_t>(sim.grid().width()),
                         static_cast<int32_t>(sim.grid().height())).dir >= 0) {
        return true;
    }

    if (button == SDL_BUTTON_RIGHT) {
        // The press saves its point and captures the view under it (FUN_01028751); the order is given at the release (FUN_01027b51)
        right_press_x_ = x;
        right_press_y_ = y;
        right_capture_ = in_minimap_rect(x, y) ? 2 : (in_map_rect(x, y) ? 1 : 0);
        return right_capture_ != 0;
    }

    // The buttons of the top bar and the chat (the button class FUN_01011206): a press inside captures the button (pressed art, the click sound of its
    // animation); the callback runs at the release while the pointer is still on it (handle_mouse_up)
    if (help_button_.contains(x, y)) {
        help_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);   // the pressed top-bar animations carry sound 0
        return true;
    }
    if (options_button_.contains(x, y)) {
        options_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }
    if (quit_button_.contains(x, y)) {
        quit_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // The command pedestals fire on the press outside the map rectangle (FUN_010274be)
    if (pedestal_press(sim, x, y)) return true;

    // [All] button (532, 443, 44x24): hidden while chat is off
    if (chat_enabled_ && send_to_button_.contains(x, y)) {
        send_to_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // [Team] button (579, 443, 46x24): exists while the local player has an ally
    if (chat_enabled_ && is_on_team_ && team_button_.contains(x, y)) {
        team_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // The press captures the view under it (FUN_01028751): the minimap (the view follows in the input ticks while the button is held),
    // then the map (a rubber band that is decided at the release)
    if (in_minimap_rect(x, y)) {
        is_radar_dragging_ = true;
        return true;
    }
    if (in_map_rect(x, y)) {
        is_dragging_ = true;
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_curr_x_ = x;
        drag_curr_y_ = y;
        return true;
    }
    return false;
}

bool HUD::handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                          sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod) {
    const bool captured_before = is_input_captured();      // [5534] != 0 while the event is processed
    // The button class (FUN_01011206 / FUN_01011281): the callback runs at the release when the button is still captured, that is when the
    // pointer has not left it (leaving cancels the capture for good)
    const bool left_release = (button == SDL_BUTTON_LEFT);
    const bool fire_help = left_release && help_button_.is_pressed && help_button_.contains(x, y);
    const bool fire_options = left_release && options_button_.is_pressed && options_button_.contains(x, y);
    const bool fire_quit = left_release && quit_button_.is_pressed && quit_button_.contains(x, y);
    const bool fire_all = left_release && chat_enabled_ && send_to_button_.is_pressed && send_to_button_.contains(x, y);
    const bool fire_team = left_release && chat_enabled_ && is_on_team_ && team_button_.is_pressed && team_button_.contains(x, y);
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

    // ProcessInput ignores the button events of an input lock and of a scroll arrow (the strip test is off while a button is captured)
    const bool right = (button == SDL_BUTTON_RIGHT);
    if (input_lock_ticks_ > 0) {
        is_dragging_ = false;
        right_capture_ = 0;
        return true;
    }
    const bool no_edge = captured_before || (right && right_capture_ != 0);
    const bool in_strip = !no_edge &&
        edge_scroll_step(x, y, 0, camera.world_x, camera.world_y, static_cast<int32_t>(sim.grid().width()),
                         static_cast<int32_t>(sim.grid().height())).dir >= 0;

    if (button == SDL_BUTTON_LEFT) {
        if (!in_strip) {
            if (fire_help) open_quick_help();
            else if (fire_options) open_options();
            else if (fire_quit) open_quit_dialog();
            else if (fire_all) send_chat(false);
            else if (fire_team) send_chat(true);
        }
        const bool was_dragging = is_dragging_;
        if (was_dragging) {                       // the band's last update is the release position (the original polls it every 50 ms)
            drag_curr_x_ = x;
            drag_curr_y_ = y;
        }
        if (!in_strip) pointer_release(sim, camera, x, y, is_shift_held() || (mod & KMOD_SHIFT) != 0);
        is_dragging_ = false;
        return was_dragging || in_map_rect(x, y);
    }
    if (right) {
        const int capture = right_capture_;
        if (!in_strip) pointer_right_click(sim, camera, capture, right_press_x_, right_press_y_, x, y);
        right_capture_ = 0;
        return capture != 0;
    }
    return false;
}

bool HUD::handle_mouse_motion(int32_t x, int32_t y,
                              [[maybe_unused]] sim::SimulationEngine& sim, [[maybe_unused]] ViewportCamera& camera) {
    mouse_x_ = x;
    mouse_y_ = y;
    for (UIButton* b : {&help_button_, &options_button_, &quit_button_, &send_to_button_, &team_button_}) {
        if (b->is_pressed && !b->contains(x, y)) b->is_pressed = false;      // FUN_01011281: leaving cancels the capture for good
    }
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

    if (is_radar_dragging_) return true;

    return false;
}

bool HUD::is_input_captured() const noexcept {
    // [5534] != 0: the map, the minimap (left or right button) or a button holds the mouse; the pedestals do not capture
    return is_dragging_ || is_radar_dragging_ || right_capture_ != 0 || help_button_.is_pressed || options_button_.is_pressed ||
           quit_button_.is_pressed || send_to_button_.is_pressed || team_button_.is_pressed;
}

// The INPUT task (0x100ae26, every 50 ms) as far as the view is concerned: FUN_01026aa3 for the edge strips, or, while the left button
// is held on the minimap, FUN_01009850.
bool HUD::input_tick(ViewportCamera& camera, uint32_t map_w, uint32_t map_h, int32_t mouse_x, int32_t mouse_y) {
    if (is_modal_open() || map_w == 0 || map_h == 0) return false;
    const int32_t rate = std::min(99, static_cast<int32_t>(scroll_rate_ * 100.0f));
    EdgeScroll step;
    if (is_radar_dragging_) {
        step = minimap_scroll_step(mouse_x, mouse_y, camera.world_x, camera.world_y, static_cast<int32_t>(map_w), static_cast<int32_t>(map_h));
    } else if (!is_input_captured()) {
        step = edge_scroll_step(mouse_x, mouse_y, rate, camera.world_x, camera.world_y, static_cast<int32_t>(map_w), static_cast<int32_t>(map_h));
    }
    if (step.dx == 0 && step.dy == 0) return false;
    camera.scroll_pixels(step.dx, step.dy, map_w, map_h);
    return true;
}

bool HUD::handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod, bool repeat) {
    // FUN_0102609a. A dialog takes every key first.
    if (show_match_start_modal_) return true;

    if (show_quit_dialog_) {                                   // Y = Yes, N and Esc = No, Enter does nothing
        if (key == 'y' || key == 'Y') {
            close_quit_dialog();
            if (on_quit_) on_quit_();
        } else if (key == 'n' || key == 'N' || key == SDLK_ESCAPE) {
            close_quit_dialog();
        }
        return true;
    }

    if (show_quick_help_) {                                    // C, X, Enter and Esc close it
        if (key == 'c' || key == 'C' || key == 'x' || key == 'X' || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            close_quick_help();
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
            return true;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) close_options();      // Enter closes it, Esc does not
        return true;
    }

    const bool ctrl = (mod & KMOD_CTRL) != 0 || (mod & KMOD_GUI) != 0;
    const auto& world = sim.get_world_state();

    // The chat edit control is always active while chat is on: it takes 0x20 - 0x7E and Backspace unless Ctrl is held
    if (chat_enabled_ && !ctrl) {
        if (key >= 32 && key <= 126) {
            if (chat_input_.size() < kChatInputMax) chat_input_.push_back(static_cast<char>(key));
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            if (!chat_input_.empty()) chat_input_.pop_back();
            return true;
        }
    }

    // Keys of any modifier state
    switch (key) {
        case SDLK_F1:                                          // quick help (page 0)
            open_quick_help();
            return true;
        case SDLK_F9: case SDLK_F10: case SDLK_F11: case SDLK_F12:
            if (!repeat && chat_enabled_) trigger_quick_chat(static_cast<size_t>(key - SDLK_F9));
            return true;
        case SDLK_RETURN: case SDLK_KP_ENTER:                  // FUN_010103eb: the team when the player has an ally, else everybody
            send_chat_message();
            return true;
        case SDLK_ESCAPE:                                      // FUN_01028c44(0): deselect everything, no quit dialog
            clear_selection();
            unlatch_pedestals();
            return true;
        case SDLK_PAGEUP:                                      // (not in the original: the chat log scrolls with a bar)
            scroll_chat_up(4);
            return true;
        case SDLK_PAGEDOWN:
            scroll_chat_down(4);
            return true;
        default:
            break;
    }

    if (!ctrl) return false;
    switch (key) {
        case 'a': case 'A':                                    // select all own ants, panel 3 or 4, the voice of the first
            select_all_friendly(world);
            return true;
        case 'h': case 'H':                                    // the home hill (panel 2); no hatch, no scrolling
            clear_selection();
            unlatch_pedestals();
            select_base(local_player_id_);
            return true;
        case 'l': case 'L':                                    // hit point digits on every ant
            show_hp_ = !show_hp_;
            return true;
        case 'o': case 'O':
            open_options();
            return true;
        case 'q': case 'Q':
            open_quit_dialog();
            return true;
        case 's': case 'S': {                                  // the stop order, without flash, lock or deselect (panels 3 and 4)
            const PanelMode panel = panel_mode(world);
            if (panel == PanelMode::OneAnt || panel == PanelMode::Ants) stop_selected(sim);
            return true;
        }
        case 'n': case 'N': case 'p': case 'P': {
            // Next / previous own ant (FUN_0102609a 0x1026330): the search starts at the lowest selected slot (nothing selected: the last one), steps
            // by one in the direction, the selection is replaced (panel 3, no voice) and the view scrolls just far enough to show the +-128 px square
            std::vector<const sim::AntSnapshot*> mine;
            for (const auto& a : world.ants) {
                if (a.player_id == local_player_id_) mine.push_back(&a);
            }
            if (mine.empty()) return true;
            const int64_t count = static_cast<int64_t>(mine.size());
            int64_t start = count - 1;
            for (int64_t i = 0; i < count; ++i) {
                if (is_ant_selected(mine[static_cast<size_t>(i)]->id)) { start = i; break; }
            }
            const int64_t dir = (key == 'n' || key == 'N') ? 1 : -1;
            const sim::AntSnapshot* ant = mine[static_cast<size_t>((start + count + dir) % count)];
            select_ant(ant->id, false);
            unlatch_pedestals();
            const int32_t map_w = static_cast<int32_t>(world.width) * 32;
            const int32_t map_h = static_cast<int32_t>(world.height) * 32;
            int32_t dx = 0;
            int32_t dy = 0;
            detail::scroll_to_show(std::max(ant->px - 128, 0), std::max(ant->py - 128, 0), std::min(ant->px + 128, map_w), std::min(ant->py + 128, map_h),
                                   camera.world_x, camera.world_y, dx, dy);
            camera.x = static_cast<float>(camera.world_x);          // (both fields describe the same origin)
            camera.y = static_cast<float>(camera.world_y);
            camera.scroll_pixels(dx, dy, world.width, world.height);
            return true;
        }
        default:
            return false;                                      // Ctrl+B / C / F / M / T ... do nothing in the original
    }
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
    if (show_quit_dialog_ || show_quick_help_ || show_match_start_modal_ || !chat_enabled_) return;     // a dialog or the chat cover takes the keys
    for (char c : text) {
        if (c >= 32 && c <= 126) {
            if (chat_input_.size() < kChatInputMax) {
                chat_input_.push_back(c);
            }
        }
    }
}

void HUD::send_chat(bool to_team) {
    if (chat_input_.empty()) return;
    if (!chat_enabled_) {                     // "Participate In Chat" off: the box is covered and nothing is sent
        chat_input_.clear();
        return;
    }
    add_chat_entry(player_name_.empty() ? "Player" : player_name_, chat_input_, to_team && is_on_team_);
    if (on_chat_send_) on_chat_send_(chat_input_, to_team && is_on_team_);
    chat_input_.clear();
    chat_scroll_offset_ = 0;
}

void HUD::trigger_quick_chat(size_t index) {
    if (index >= 4 || !chat_enabled_) return;
    if (quick_chat_keys_[index].empty()) return;
    add_chat_entry(player_name_.empty() ? "Player" : player_name_, quick_chat_keys_[index], false);   // F9 - F12 always go to all
    if (on_chat_send_) on_chat_send_(quick_chat_keys_[index], false);
}

namespace {

// Wraps a text into lines of at most `max_chars` characters at spaces (a word longer than a line is cut).
void wrap_chat_text(const std::string& text, size_t max_chars, std::vector<std::string>& out) {
    size_t start = 0;
    while (start < text.length()) {
        if (text.length() - start <= max_chars) {
            out.push_back(text.substr(start));
            break;
        }
        size_t split = text.rfind(' ', start + max_chars);
        if (split == std::string::npos || split <= start) split = start + max_chars;
        out.push_back(text.substr(start, split - start));
        start = split;
        while (start < text.length() && text[start] == ' ') ++start;
    }
}

constexpr size_t kChatBodyCharsPerLine = 21;    // the body's 126 px at 6 px per character
constexpr size_t kChatBodyMaxChars = 100;
constexpr size_t kChatLogMaxLines = 5000;       // the original never trims; only a safety bound against endless matches

}  // namespace

void HUD::add_chat_entry(const std::string& sender, const std::string& message, bool team_only, int colour_index) {
    const uint8_t colour = static_cast<uint8_t>(colour_index >= 0 && colour_index <= 3 ? colour_index : (3 - (local_player_id_ & 3)));
    push_chat_entry(sender + (team_only ? " (To Teammate):" : ":"), message, colour);
}

void HUD::push_chat_entry(std::string header, const std::string& message, uint8_t colour) {
    if (header.size() > 50) header.resize(50);                       // the header text object holds 50 characters
    chat_log_.push_back(header);
    chat_line_colour_.push_back(colour);
    chat_line_indent_.push_back(0);
    std::vector<std::string> lines;
    wrap_chat_text(message.substr(0, kChatBodyMaxChars), kChatBodyCharsPerLine, lines);
    for (auto& line : lines) {
        chat_log_.push_back(std::move(line));
        chat_line_colour_.push_back(5);
        chat_line_indent_.push_back(12);
    }
    while (chat_log_.size() > kChatLogMaxLines) {
        chat_log_.pop_front();
        chat_line_colour_.pop_front();
        chat_line_indent_.pop_front();
    }
}

void HUD::add_news_flash(uint32_t elapsed_ms, const std::string& text) {
    const uint32_t secs = elapsed_ms / 1000;
    char header[48];
    std::snprintf(header, sizeof(header), "[%u:%02u] News Flash:", secs / 60, secs % 60);
    push_chat_entry(header, text, 4);
}

void HUD::receive_chat_message(uint8_t sender, const std::string& name, const std::string& text, bool to_team, const sim::WorldState& world) {
    if (!chat_enabled_) return;                                       // the receive handler drops it (0x102411a)
    if (to_team && sender != local_player_id_) {
        const bool sender_ally_is_me = sender < world.player_alliances.size() && world.player_alliances[sender] == local_player_id_;
        if (!sender_ally_is_me) return;                               // team text: only the sender and the players whose ally the sender is
    }
    add_chat_entry(name, text, to_team, 3 - (sender & 3));
}

void HUD::scroll_chat_up(int32_t lines) noexcept {
    int32_t total_lines = static_cast<int32_t>(chat_log_.size());
    int32_t max_scroll = std::max(0, total_lines - kChatVisibleLines);
    chat_scroll_offset_ = std::clamp(chat_scroll_offset_ + lines, 0, max_scroll);
}

void HUD::scroll_chat_down(int32_t lines) noexcept {
    int32_t total_lines = static_cast<int32_t>(chat_log_.size());
    int32_t max_scroll = std::max(0, total_lines - kChatVisibleLines);
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

} // namespace ants::app

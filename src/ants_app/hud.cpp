#include "ants_app/hud.hpp"
#include "hud_internal.hpp"
#include "ants_app/minimap_tables.hpp"
#include <cstdio>
#include "ants_sim/game_strings.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/prng.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"

#include <array>
#include <cmath>
#include <algorithm>
#include <iomanip>

namespace ants::app {

using hud_detail::draw_std_dialog;

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

} // anonymous namespace

HUD::HUD() {
    options_.set_on_change([this](OptionSetting setting) { apply_option(setting); });
    init(0);
}

void HUD::init(uint8_t local_player_id, bool announce) {
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
    selected_actions_.clear();
    chat_entries_.clear();
    chat_log_.clear();
    chat_line_color_.clear();
    chat_content_end_ = 0;
    chat_follow_pos_ = 0;
    chat_follow_target_ = 0;
    chat_follow_task_ = false;
    chat_dragging_ = false;
    chat_drag_offset_ = 0;
    chat_scroll_task_ = false;
    radar_map_w_ = 0;
    radar_map_h_ = 0;
    if (announce) {
        post_status_id(sim::strings::kWelcome, "Ants");    // FUN_0100dbe2 0x100e173, once when the match screen is built
        // FUN_01022432: "[0:00] News Flash:", the start message (187 has no food: its players are told what the match is about)
        add_news_flash(0, game_mode_ == sim::GameMode::Kills187 ? "Game started! Most kills wins." : "Game started! Go get that food!");
    }

    // The buttons start with no state; where they are comes from the layout (apply_layout, at the end)
    help_button_ = UIButton{};
    options_button_ = UIButton{};
    quit_button_ = UIButton{};
    move_pedestal_button_ = UIButton{};
    ability_pedestal_button_ = UIButton{};
    stop_button_ = UIButton{};
    send_to_button_ = UIButton{};
    team_button_ = UIButton{};
    is_on_team_ = false;
    chat_input_.clear();
    chat_focus_ms_ = clock_ms();                         // the chat edit control is active from the moment the screen is built (FUN_0100dbe2)

    yes_button_ = UIButton{};
    no_button_ = UIButton{};

    show_match_start_modal_ = false;
    match_start_modal_ticks_ = 0;
    match_start_modal_until_dismissed_ = false;
    match_countdown_ = false; match_countdown_left_ms_ = 0; countdown_seats_ = {}; countdown_button_pressed_ = false;          // (protocol 21)
    show_quit_dialog_ = false;
    show_quick_help_ = false;
    options_.close();
    close_alliance_dialog();
    pending_break_ = PendingBreak{};
    suppressed_invite_from_ = 255;
    suppressed_wait_for_ = 255;

    team_up_button_.is_pressed = false;
    team_up_button_.is_active = false;

    apply_layout();
}

void HUD::set_layout(const ScreenLayout& layout) {
    const int32_t old_view_h = layout_.chat_view().h;
    layout_ = layout;
    apply_layout();
    relayout_chat(old_view_h);                           // the chat view may have another height: the log's positions stay inside it
}

// The rectangles that the layout places, the one place that knows them: init() runs it at every new match and set_layout() when the layout changes, so a layout set earlier is
// never lost. What is anchored to the right edge of the original's screen is `right(x)`, what is anchored to the bottom edge `bottom(y)` (screen_layout.hpp); a button that is
// anchored to neither (the quit dialog's) is a part of a picture of the original's screen and stays.
void HUD::apply_layout() {
    const ScreenLayout& lay = layout_;
    const auto place = [](UIButton& b, int32_t x, int32_t y, int32_t w, int32_t h) {
        b.x = x;
        b.y = y;
        b.w = w;
        b.h = h;
    };
    // Top header buttons (x0y0.bmp): the right part of the top bar
    place(help_button_, lay.right(476), 7, 46, 23);
    place(options_button_, lay.right(525), 7, 52, 23);
    place(quit_button_, lay.right(579), 7, 46, 23);

    // The three pedestal slots of the original (FUN_01028d30, half-open): slot 1 (Move / hatch / ally) (482, 152) - (525, 225),
    // slot 2 (ability) (539, 152) - (582, 225), slot 3 (Stop) (597, 189) - (628, 227); the hatch and ally pedestals are slot 1 as well
    place(move_pedestal_button_, lay.right(482), 152, 43, 73);
    place(ability_pedestal_button_, lay.right(539), 152, 43, 73);
    place(stop_button_, lay.right(597), 189, 31, 38);
    place(hatch_button_, lay.right(482), 152, 43, 73);
    place(team_up_button_, lay.right(482), 152, 43, 73);

    // The authentic Send-to toggle button (532, 443, 44, 24) and the Team toggle button (579, 443, 46, 24): the bottom of the panel
    place(send_to_button_, lay.right(532), lay.bottom(443), 44, 24);
    place(team_button_, lay.right(579), lay.bottom(443), 46, 24);

    // The quit confirmation dialog's buttons: dyn_byes1 part (80, 160) + origin (100, 100), dyn_bno1 part (192, 160) + origin (100, 100). A picture of the original's screen
    place(yes_button_, 180, 260, 49, 24);
    place(no_button_, 292, 260, 49, 24);
}

bool HUD::in_slot(int slot, int32_t x, int32_t y) const noexcept {
    switch (slot) {
        case 0: return move_pedestal_button_.contains(x, y);
        case 1: return ability_pedestal_button_.contains(x, y);
        case 2: return stop_button_.contains(x, y);
        default: return false;
    }
}

void HUD::reset(bool announce) {
    init(local_player_id_, announce);
}

void HUD::update(const sim::WorldState& world, uint32_t delta_ticks) {
    // 0. The "Get ready" dialog's clock: steps of 50 ms (the original's task KWFO closes it 5000 ms after it was made, Ants.exe 0x10254b0). The application steps the HUD in real time while the
    // dialog is up, since the simulation waits for it (see start_match_modal); a match of the network keeps the dialog until its first turn has executed
    if (show_match_start_modal_) {
        match_start_modal_ticks_ = delta_ticks > UINT32_MAX - match_start_modal_ticks_ ? UINT32_MAX : match_start_modal_ticks_ + delta_ticks;
        if (!match_start_modal_until_dismissed_ && match_start_modal_ticks_ >= kMatchStartModalSteps) show_match_start_modal_ = false;
    }
    update_alliance_dialog(world);

    // The input lock after a Stop (FUN_01028bdd): 250 ms without mouse input, then everything is deselected (FUN_01028c44(0))
    if (input_lock_ticks_ > 0) {
        input_lock_ticks_ -= std::min(input_lock_ticks_, delta_ticks);
        if (input_lock_ticks_ == 0 && pending_deselect_) {
            pending_deselect_ = false;
            clear_selection();
            unlatch_pedestals();
        }
    }

    // The chat log's two tasks (CHATAPPD, CHATSCRL)
    update_chat_tasks();

    // 1. The status line (CLEARSTAT and TXTFLASH tasks) and the text that a selection change decides
    status_line_.update(delta_ticks);
    apply_selection_status(world);
    check_selected_pickups(world);

    // The INPUT task polls the pointer every 50 ms (FUN_0102653f dispatches the move to the top window): the pictures of the options' controls follow it
    // (the window's numbers are the original's own: the pointer is taken back to them, as the event handlers do)
    const LayoutPoint window = open_window_offset();
    if (options_.is_open()) options_.on_move(mouse_x_ - window.x, mouse_y_ - window.y);
    if (show_quick_help_) quick_help_return_.on_move(mouse_x_ - window.x, mouse_y_ - window.y);

    // 3. Alliance team status
    is_on_team_ = (local_player_id_ < world.player_alliances.size() &&
                   world.player_alliances[local_player_id_] < sim::MAX_PLAYERS &&
                   world.player_alliances[local_player_id_] != local_player_id_);
}

void HUD::poll_sim_events(sim::SimulationEngine& sim, bool replayed) {
    auto news = sim.poll_news_events();
    for (const auto& ev : news) {
        if (ev.target_player != 255 && ev.target_player != local_player_id_) continue;
        if (ev.channel == sim::NewsChannel::ChatLog) {
            add_news_flash(ev.timestamp_ms, ev.message_text);
        } else if (ev.channel == sim::NewsChannel::Dialog) {
            // the invitation dialog belongs to the network stage: the match screen has no window for it yet
        } else if (!replayed) {
            status_line_.post(ev.message_text, ev.blink);
        }
        if (news_note_) {
            const std::string note = news_note_(ev);
            if (!note.empty()) add_news_flash(ev.timestamp_ms, note);
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
    } else if (own > 1 || !only || is_multi_select_mode_) {        // panel 4 says string 12 even for the one ant it holds
        post_status_id(sim::strings::kSelMany);
    } else {
        static const uint16_t kByType[6] = {sim::strings::kSelWorker, sim::strings::kSelBomber, sim::strings::kSelFire,
                                            sim::strings::kSelThief, sim::strings::kSelCombat, sim::strings::kSelSwimmer};
        const size_t t = static_cast<size_t>(only->type);
        if (t < 6) post_status_id(kByType[t]); else status_line_.clear();
    }
}

void HUD::check_selected_pickups(const sim::WorldState& world) {
    // FUN_0100cd40 is the last thing a pick-up does (its only call, 0x1020dd2, in FUN_01020cdb, which pushes SetAction(4) at 0x1020d2a in the same call): for an ant of the local player that is selected
    // ([ant + 0x50], FUN_0100cd7d tests the observer flag and the team) it rebuilds the panel, whatever the pick-up changed: an ant that takes the power-up of its own type again, or a worker of a
    // level whose default type is that of the power-up, changes nothing that can be seen and the panel is rebuilt all the same. So the pick-up is the rising edge of the action 4 of an own ant that
    // was selected at the last update (AntSnapshot::action); the text that the rebuilt panel posts is that of the type the lone ant is. (The ant that is selected while the clip already runs was not
    // selected when the pick-up happened: no edge.)
    std::vector<std::pair<uint32_t, uint8_t>> now;
    const sim::AntSnapshot* lone = nullptr;
    bool picked_up = false;
    if (selected_base_team_id_ < 0) {
        for (uint32_t id : selected_ant_ids_) {
            for (const auto& a : world.ants) {
                if (a.id != id || a.player_id != local_player_id_ || a.hp == 0 || a.is_drowning) continue;
                now.emplace_back(id, a.action);
                lone = &a;
                if (a.action == sim::AntUnit::kActionGetPow) {
                    for (const auto& before : selected_actions_) {
                        if (before.first == id && before.second != sim::AntUnit::kActionGetPow) picked_up = true;
                    }
                }
                break;
            }
        }
    }
    selected_actions_ = std::move(now);
    if (!picked_up) return;
    unlatch_pedestals();                                              // SetPanelMode 3 / 4: FUN_01028360(1, 1, kind, 1, 0, 1) raises both pedestals (0x1027fde - 0x1028001, 0x1027f99 - 0x1027fa7)
    if (selected_actions_.size() == 1 && !is_multi_select_mode_) {    // panel 3 names the type, panel 4 (several ants, or a shift-selection of one) says string 12
        static const uint16_t kByType[6] = {sim::strings::kSelWorker, sim::strings::kSelBomber, sim::strings::kSelFire,
                                            sim::strings::kSelThief, sim::strings::kSelCombat, sim::strings::kSelSwimmer};
        const size_t t = static_cast<size_t>(lone->type);
        if (t < 6) post_status_id(kByType[t]); else status_line_.clear();
    } else {
        post_status_id(sim::strings::kSelMany);
    }
}

// The status box (label rect (481, 254) - (620, 266)): steady color (79, 0, 143), left aligned, clipped at 139 px, hidden on the
// odd 50 ms steps while a flash runs.
void HUD::render_status_line(IRenderer& renderer) const {
    if (!status_line_.visible()) return;
    std::string text = status_line_.text();
    while (text.size() > 1 && renderer.get_text_width(text, FontSize::Px12) > 139) text.pop_back();
    renderer.draw_text(text, layout_.right(481), 254, ants::assets::ColorRGBA{79, 0, 143, 255}, FontSize::Px12);
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
    const LayoutRect panel = layout_.panel_fill();
    renderer.fill_rect(panel.x, panel.y, panel.w, panel.h, hud_bg_colors[local_player_id_ % 4]);

    // 1. The static HUD shell: animation uishell = 14 parts (frame borders, top bar, banner, card, chat boxes, status
    //    box at (479,253)) drawn last part first, exactly as the original composes it. Nothing static is drawn twice. In a picture bigger than the original's
    //    screen the pieces are placed by their anchors and the stretched ones repeat one line of themselves (shell_layout.hpp).
    render_shell(renderer, assets);

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
            if (ally_pedestal_possible(world) && !allied) {
                left_kind = PedestalKind::Ally;
                left_mode = (team_up_button_.is_pressed || flashing(0)) ? 2 : 1;
            }
        }
    } else if (has_friendly_ants) {
        left_kind = PedestalKind::Move;
        left_mode = (slot_latched_[0] || move_pedestal_button_.is_pressed || flashing(0)) ? 2 : 1;
        // The ability pedestal of the selected ants' common type (worker and mixed selections have none)
        // (panel 3 only: for several ants SetPanelMode 4 gives slot 2 the kind 9, hidden)
        sim::AntType common = sim::AntType::Worker;
        if (panel_mode(world) == PanelMode::OneAnt && homogeneous_type(world, common)) {
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
    left_pedestal_.draw(renderer, assets, now_ms, layout_.dx());                       // (the pedestals are in the panel's top part: right anchored)
    right_pedestal_.draw(renderer, assets, now_ms, layout_.dx());
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
                draw_animation_frame0(renderer, assets, tray.c_str(), layout_.dx(), 0);
            }
            draw_animation_frame0(renderer, assets, (stop_button_.is_pressed || flashing(2)) ? "butcand" : "butcanu", layout_.dx(), 0);
        }
        // (an enemy base shows the status box only: the ally pedestal is the left slot)
    } else {
        if (has_friendly_ants) {
            // Stop button (animation butcanu / butcand: label at (595,180), button at (595,198))
            draw_animation_frame0(renderer, assets, (stop_button_.is_pressed || flashing(2)) ? "butcand" : "butcanu", layout_.dx(), 0);

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
                draw_animation_frame0(renderer, assets, "UI_LBOX", layout_.dx(), 0);
            }
        }
    }

    // The one-line status box (the recessed box wstatus.bmp is part of the uishell composite)
    render_status_line(renderer);

    // 2.3 Lower Panel: Always render Chat Section
    // Cursive embossed Chat header at (480, 266)

    // White chat history log wchat.bmp (143x103) at (479, 298): the view (482, 299) - (620, 400), drawn by render_chat_log
    render_chat_log(renderer);

    // Ant relief horizontal divider bar x480y400.bmp (141x24) at (480, 400)

    // Chat text input box wtype.bmp (143x14) at (479, 423): the edit control of FUN_0100dbe2 (docs 5.51) in the rectangle (481, 424) - (620, 436): 12 px letters in
    // (7, 11, 15), active from the moment the screen is built (its caret toggles every 150 ms from then on) and hidden while the chat option is off. A text that does
    // not fit shows its end (the control always has the focus).
    if (options_.state().chat) {
        const bool caret = ((clock_ms() - chat_focus_ms_) / ScreenEdit::CARET_HALF_PERIOD_MS) % 2 == 0;
        draw_edit_line(renderer, chat_input_, layout_.right(481), layout_.bottom(424), 139, true, caret, assets::ColorRGBA{7, 11, 15, 255}, FontSize::Px12, layout_.width);
    }
    // Chat switched off in the options: chatcovr (three chcovr2 tiles at (478,421/436/445)) covers the input box
    if (!options_.state().chat) {
        draw_animation_frame0(renderer, assets, "chatcovr", layout_.dx(), layout_.dy());
    }

    // "Send to:" buttons (animations butall*, butals*): up / hover ("r" label over the up art) / pressed. [All] is hidden while chat is off,
    // [Team] exists only while the local player has an ally.
    auto send_button = [&](bool down, bool hovered, const char* up, const char* hover, const char* pressed) {
        draw_animation_frame0(renderer, assets, down ? pressed : (hovered ? hover : up), layout_.dx(), layout_.dy());       // (bottom of the panel: right and down)
    };
    if (options_.state().chat) {
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

    // 5. Overlays and Dialogs: the options window and the quick help are the original's own pictures (their own numbers) and sit over the map view, centred in it, with the HUD visible
    //    around them (as the options window does in the original's picture); the dialogs are its dialog frame, moved so that it is centred over the map view too. They are drawn with
    //    their own numbers, moved by the renderer's origin (IRenderer::set_origin: nothing moves in the original's own picture)
    const LayoutPoint modal = layout_.modal_offset();
    if (show_quick_help_ || options_.is_open()) {
        const LayoutPoint window = open_window_offset();
        const bool inset = window != LayoutPoint{};                     // (in the original's own picture the window is where it always was: nothing to cut)
        if (inset && !show_quick_help_) render_options_dim(renderer);   // (the original's window dims the whole picture around its card: in its own picture the window's pieces do that themselves)
        renderer.set_origin(window.x, window.y);
        if (inset) renderer.set_clip_rect(layout_.view().x - window.x, layout_.view().y - window.y, layout_.view().w, layout_.view().h);       // the pieces of a window reach beyond the map view: they stop at it
        if (show_quick_help_) render_quick_help(renderer, assets);
        else options_.render(renderer, assets, clock_ms());
        if (inset) renderer.clear_clip_rect();
        renderer.set_origin(0, 0);
    } else if (show_quit_dialog_ || alliance_dialog_ != AllianceDialog::None || show_match_start_modal_) {
        renderer.set_origin(modal.x, modal.y);
        if (show_quit_dialog_) render_quit_dialog(renderer, assets);
        else if (alliance_dialog_ != AllianceDialog::None) render_alliance_dialog(renderer, assets);
        else render_match_start_modal(renderer, assets);
        renderer.set_origin(0, 0);
    }
}

// The options window of the original dims everything of its picture that the card does not cover: op_screen carries 25 pieces of the checker dither (dith100x, dith100y and dith200: every
// second pixel black, in the pixels where x + y is odd of the window's own numbers), a ring of 20 pixels around the 442 x 440 card and the whole panel to the right of it. In a bigger picture
// those pieces keep the window's place (they are clipped to the map view with the rest of it, so they dim the view's surroundings of the card only on its left and right, from the ring's width
// on), and the rest of the picture is dimmed here in the same checker: four bands around the card (above, below, left and right of it, over the whole picture: the map around the card, the
// frame and the panel), tiled from the corner of the first strip of the original (its own phase: the dithered pixels are the same ones whatever the offset of the window is)
void HUD::render_options_dim(IRenderer& renderer) {
    const LayoutRect card = layout_.options_card();
    const LayoutRect picture{0, 0, layout_.width, layout_.height};
    const LayoutPoint o = layout_.options_offset();
    constexpr int32_t kTile = 200;                                       // dith200.bmp
    const int32_t anchor_x = o.x - 1;                                    // (the top strip's first piece: dith100x at (-1, 0) of the window)
    const int32_t anchor_y = o.y;
    const LayoutRect bands[4] = {LayoutRect{picture.x, picture.y, picture.w, card.y - picture.y},
                                 LayoutRect{picture.x, card.y + card.h, picture.w, picture.y + picture.h - (card.y + card.h)},
                                 LayoutRect{picture.x, card.y, card.x - picture.x, card.h},
                                 LayoutRect{card.x + card.w, card.y, picture.x + picture.w - (card.x + card.w), card.h}};
    for (const LayoutRect& band : bands) {
        if (band.w <= 0 || band.h <= 0) continue;
        const auto first = [](int32_t from, int32_t anchor) { const int32_t d = from - anchor; return anchor + (d >= 0 ? d / kTile : -((-d + kTile - 1) / kTile)) * kTile; };
        renderer.set_clip_rect(band.x, band.y, band.w, band.h);
        for (int32_t y = first(band.y, anchor_y); y < band.y + band.h; y += kTile) {
            for (int32_t x = first(band.x, anchor_x); x < band.x + band.w; x += kTile) renderer.draw_named_sprite("dith200.bmp", x, y);
        }
        renderer.clear_clip_rect();
    }
}

// The frame of the match screen: the 14 pieces of the animation uishell, last stored piece first (the order the original draws them), each where its anchor puts it in this picture and,
// when it is one of the stretched pieces, in parts around one repeated line (shell_layout.hpp). In the original's own picture every piece is one plain copy at its own place.
void HUD::render_shell(IRenderer& renderer, const assets::AssetArchive& assets) {
    const auto* seq = assets.find_animation("uishell");
    if (!seq || seq->subitems.empty()) return;
    const auto& frames = seq->subitems[0].frames;
    for (size_t k = frames.size(); k-- > 0;) {
        const auto& part = frames[k];
        const assets::Sprite& sprite = assets.get_sprite(part.sprite_index);
        const ShellSpans spans = shell_spans(shell_rule(sprite.name), part.dx, part.dy, static_cast<int32_t>(sprite.width), static_cast<int32_t>(sprite.height), layout_);
        for (size_t i = 0; i < spans.count; ++i) {
            const ShellSpan& span = spans.span[i];
            if (span.whole) renderer.draw_sprite(part.sprite_index, span.dst.x, span.dst.y);
            else renderer.draw_sprite_region(part.sprite_index, span.dst.x, span.dst.y, span.dst.w, span.dst.h, span.src.x, span.src.y, span.src.w, span.src.h);
        }
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

    // The local team's label and score box (402..455, 4..17) and label (312..399): FUN_0100dbe2 / FUN_01021e36, see render_score_team
    render_score_team(renderer, archive, world, local_player_id_);

    // Top-bar buttons (animations buthlp*, butopt*, butqit*; absolute coordinates): the resting art is already part of
    // the top bar image, hovering draws the small "r" label over it and the pressed art replaces it. A button stays down
    // while the screen it opened is showing.
    if (help_button_.is_pressed || show_quick_help_) {
        draw_animation_frame0(renderer, archive, "buthlpd", layout_.dx(), 0);
    } else if (help_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "buthlpr", layout_.dx(), 0);
    }
    if (options_button_.is_pressed || options_.is_open()) {
        draw_animation_frame0(renderer, archive, "butoptd", layout_.dx(), 0);
    } else if (options_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "butoptr", layout_.dx(), 0);
    }
    if (quit_button_.is_pressed || show_quit_dialog_) {
        draw_animation_frame0(renderer, archive, "butqitd", layout_.dx(), 0);
    } else if (quit_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "butqitr", layout_.dx(), 0);
    }
}

void HUD::render_news_banner(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world) {
    // Bottom banner background: x17y461.bmp (623x19)

    // The other teams' labels and score boxes in the three pre-cut bottom slots (Ants.exe VA 0x10021B8): slot 0 label [5..101] score [105..158], slot 1 label [163..251] score
    // [254..307], slot 2 label [312..399] score [402..455], y = 464..477. The slot of a team follows its index among the others.
    for (uint8_t p = 0; p < 4; ++p) {
        if (p != local_player_id_) render_score_team(renderer, assets, world, p);
    }
}

// The slots (FUN_0100dbe2 0x100e1f0 - 0x100e222): the local team gets the top bar's rectangles (0x1002218), every other team k the next of the three bottom rectangles
// (0x10021b8 + 32 slot, slot = the number of other teams with a lower index), whether the team exists or not
void HUD::render_score_team(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world, uint8_t team) {
    ScoreSlot slot = layout_.score_slot(0);                                                   // the local team's slot: the top bar's
    if (team != local_player_id_) {
        size_t index = 0;
        for (uint8_t k = 0; k < team; ++k) {
            if (k != local_player_id_) ++index;
        }
        slot = layout_.score_slot(1 + index);                                                 // the others': the bottom strip's slots (a slot past the third repeats it)
    }
    const bool exists = ((roster_mask_ >> team) & 1u) != 0;

    // The label was made with the screen for a team that exists (and stays when the team drops out): "name:" (15 characters at most), right aligned in its box in 14 px white
    if (exists) {
        static const char* TEAM_NAMES[4] = {"Green", "Red", "Blue", "Black"};
        std::string name = team == local_player_id_ ? (player_name_.empty() ? std::string(TEAM_NAMES[team % 4]) : player_name_)
                                                    : (team_names_[team].empty() ? std::string(TEAM_NAMES[team % 4]) : team_names_[team]);
        if (name.size() > 15) name = name.substr(0, 15);
        std::string label = name + ":";
        while (label.size() > 1 && renderer.get_text_width(label, FontSize::Px14) > slot.label_right - slot.label_left) label.erase(0, 1);
        const int32_t label_w = renderer.get_text_width(label, FontSize::Px14);
        const int32_t label_h = renderer.get_text_height(FontSize::Px14);
        renderer.draw_text(label, slot.label_right - label_w, slot.top + (14 - label_h) / 2, {255, 255, 255, 255}, FontSize::Px14);
    }

    // The box (FUN_01021e36): a team that exists and has not dropped out is filled with its color; with an ally the right half (from left + (right - left) / 2) takes the
    // ally's color and the score shown is the sum of the two; any other team's box is covered (scorcovr, 58 x 17 at (left - 2, top - 1))
    const bool dropped = ((world.dropped_mask >> team) & 1u) != 0;
    if (!exists || dropped) {
        renderer.draw_named_sprite("scorcovr.bmp", slot.box_left - 2, slot.top - 1);
        return;
    }
    renderer.fill_rect(slot.box_left, slot.top, 54, 14, SCORE_BG_COLORS[team % 4]);
    // The number is the team's score plus its ally's (FUN_01021e36): the engine's player_scores ARE those sums already (get_display_score), so an allied team's
    // box draws its own entry and nothing is added here (adding the ally's entry too drew every allied box with twice the sum)
    const int32_t score = team < world.player_scores.size() ? world.player_scores[team] : 0;
    const uint8_t ally = team < world.player_alliances.size() ? world.player_alliances[team] : uint8_t{255};
    if (ally < 4 && ally != team) {
        const int32_t half = (455 - 402) / 2;                                                     // (right - left) / 2 of the 53-wide rectangle
        renderer.fill_rect(slot.box_left + half, slot.top, 54 - half, 14, SCORE_BG_COLORS[ally]);
    }
    draw_score_digits(renderer, assets, slot.box_left - 1, slot.top + 2, score);
}

void HUD::render_quit_dialog_alone(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (!show_quit_dialog_) return;
    const LayoutPoint modal = layout_.modal_offset();                    // (where render() puts the dialogs: centred over the map view)
    renderer.set_origin(modal.x, modal.y);
    render_quit_dialog(renderer, assets);
    renderer.set_origin(0, 0);
}

void HUD::render_quit_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // Quit dialog (Ants.exe 0x10142cb): no dim layer, origin (100,100) (OffsetRect(100,100) on the dialog's children)
    draw_std_dialog(renderer, assets);

    // Prompt (string 99): a wrapped, centred label of 24 px letters in the rect (130,180) 260x160, color (31,23,51) (FUN_010142cb: label (30,80),
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

void HUD::render_quick_help(IRenderer& renderer, const assets::AssetArchive& assets) {
    // In-game quick help (Ants.exe 0x10145d2, qh_screen): a 32-part full-screen composite (frame pieces plus qh2 at
    // (267,10) and qh1 at (10,9)) drawn over the live game, and the Return button qh_return1 at (529,437).
    draw_animation_frame0(renderer, assets, "qh_screen", 0, 0);
    draw_animation_frame0(renderer, assets, quick_help_return_.pressed() ? "qh_return3" : (quick_help_return_.hovered() ? "qh_return2" : "qh_return1"), 0, 0);
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
        renderer.draw_sprite(fr.sprite_index, layout_.dx() + fr.dx, fr.dy);
    }
}

} // namespace ants::app

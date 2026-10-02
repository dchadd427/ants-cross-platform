#include "ants_app/hud.hpp"
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

// The colour table of the original (0x1001c50 copied by the constructor FUN_01009056 into a table indexed by the tile id): entry = colour | size << 8. The table is
// cleared first, so a tile id without a record has colour 0 and size 0
constexpr size_t kMinimapTileIds = 1344;
uint16_t minimap_object_entry(uint16_t id) {
    static const std::array<uint16_t, kMinimapTileIds> table = [] {
        std::array<uint16_t, kMinimapTileIds> t{};
        for (const auto& o : kMinimapObjects) {
            if (o.id < kMinimapTileIds) t[o.id] = static_cast<uint16_t>(o.colour | (o.size_flag << 8));
        }
        return t;
    }();
    return id < kMinimapTileIds ? table[id] : uint16_t{0};
}

// The layer-2 id of a snapshot cell as the original's painter reads it: the remake keeps a bomb it planted as 100 .. 103 and a dropped power-up as 0x8000 | type, the original
// as 129 .. 132 and the power-up's own tile id
constexpr uint16_t kMinimapNoObject = 0x7ffe;
constexpr uint16_t kMinimapBomb = 129;
uint16_t minimap_object_id(const sim::TileCell& cell) {
    const uint16_t id = cell.interactive_id;
    if (id == sim::TILE_EMPTY || id == 0xFFFFu) return kMinimapNoObject;
    if (id >= sim::BOMB_BLACK && id <= sim::BOMB_GREEN) return kMinimapBomb;
    if ((id & 0x8000u) != 0) {
        switch (cell.powerup_type) {
            case 1:  return sim::PU_BOMBER;
            case 2:  return sim::PU_FIRE;
            case 3:  return sim::PU_THIEF;
            case 4:  return sim::PU_COMBAT;
            case 5:  return sim::PU_SWIMMER;
            default: return kMinimapNoObject;
        }
    }
    return id;
}

// FUN_01008bc6: the four bomb ids 0x81 .. 0x84
bool minimap_is_bomb(uint16_t id) { return id >= 129 && id <= 132; }

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

} // anonymous namespace

HUD::HUD() {
    options_.set_on_change([this](OptionSetting setting) { apply_option(setting); });
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
    selected_actions_.clear();
    chat_entries_.clear();
    chat_log_.clear();
    chat_line_colour_.clear();
    chat_content_end_ = 0;
    chat_follow_pos_ = 0;
    chat_follow_target_ = 0;
    chat_follow_task_ = false;
    chat_dragging_ = false;
    chat_drag_offset_ = 0;
    chat_scroll_task_ = false;
    post_status_id(sim::strings::kWelcome, "Ants");    // FUN_0100dbe2 0x100e173, once when the match screen is built
    add_news_flash(0, "Game started! Go get that food!");     // FUN_01022432: "[0:00] News Flash:", the start message

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
    layout_ = layout;
    apply_layout();
    relayout_chat();                                     // the chat view may have another height: the log's positions stay inside it
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
    if (options_.is_open()) options_.on_move(mouse_x_, mouse_y_);
    if (show_quick_help_) quick_help_return_.on_move(mouse_x_, mouse_y_);

    // 3. Alliance team status
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

// The status box (label rect (481, 254) - (620, 266)): steady colour (79, 0, 143), left aligned, clipped at 139 px, hidden on the
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
        draw_animation_frame0(renderer, assets, "chatcovr", 0, 0);
    }

    // "Send to:" buttons (animations butall*, butals*): up / hover ("r" label over the up art) / pressed. [All] is hidden while chat is off,
    // [Team] exists only while the local player has an ally.
    auto send_button = [&](bool down, bool hovered, const char* up, const char* hover, const char* pressed) {
        draw_animation_frame0(renderer, assets, down ? pressed : (hovered ? hover : up));
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

    // 5. Overlays and Dialogs
    if (show_quick_help_) {
        render_quick_help(renderer, assets);
    } else if (options_.is_open()) {
        options_.render(renderer, assets, clock_ms());
    } else if (show_quit_dialog_) {
        render_quit_dialog(renderer, assets);
    } else if (alliance_dialog_ != AllianceDialog::None) {
        render_alliance_dialog(renderer, assets);
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

    // The local team's label and score box (402..455, 4..17) and label (312..399): FUN_0100dbe2 / FUN_01021e36, see render_score_team
    render_score_team(renderer, archive, world, local_player_id_);

    // Top-bar buttons (animations buthlp*, butopt*, butqit*; absolute coordinates): the resting art is already part of
    // the top bar image, hovering draws the small "r" label over it and the pressed art replaces it. A button stays down
    // while the screen it opened is showing.
    if (help_button_.is_pressed || show_quick_help_) {
        draw_animation_frame0(renderer, archive, "buthlpd");
    } else if (help_button_.is_hovered) {
        draw_animation_frame0(renderer, archive, "buthlpr");
    }
    if (options_button_.is_pressed || options_.is_open()) {
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
    const LayoutRect mini = layout_.minimap();
    const int32_t rx = mini.x, ry = mini.y;
    const int32_t rw = mini.w, rh = mini.h;

    if (world.width == 0 || world.height == 0 || world.cells.size() != static_cast<size_t>(world.width) * world.height) {
        renderer.fill_rect(rx, ry, rw, rh, {0, 0, 0, 255});
        return;
    }
    const int32_t map_w = static_cast<int32_t>(world.width);
    const int32_t map_h = static_cast<int32_t>(world.height);

    // Terrain speckle: one random pick per pixel from the class colours, made once per map (the original paints it when
    // cells are first drawn and keeps it until they are repainted)
    if (radar_map_w_ != world.width || radar_map_h_ != world.height || radar_terrain_.size() != static_cast<size_t>(rw * rh)) {
        radar_map_w_ = world.width;
        radar_map_h_ = world.height;
        radar_terrain_.assign(static_cast<size_t>(rw * rh), 0);
        sim::PRNG rng(world.width * 1000u + world.height);
        for (int32_t y = 0; y < rh; ++y) {
            for (int32_t x = 0; x < rw; ++x) {
                const int32_t cx = std::min<int32_t>(map_w - 1, x * map_w / rw);
                const int32_t cy = std::min<int32_t>(map_h - 1, y * map_h / rh);
                const uint8_t cls = minimap_class(world.cells[static_cast<size_t>(cy) * world.width + static_cast<size_t>(cx)]);
                radar_terrain_[static_cast<size_t>(y * rw + x)] = kMinimapClassColours[cls < 6 ? cls : 5][rng.rand() % 5u];
            }
        }
    }

    const auto& palette = archive.get_palette();
    const bool fog = world.fog_of_war_enabled;

    // FUN_01009596: every pixel takes the cell under it. An object shows its colour from the table (a tile id without a record: colour 0) unless the cell is empty or holds a bomb;
    // then the terrain shows (speckle, or the fog colour of an unexplored cell). In fog the objects that are removed or given out (power-ups, food, fire walls) fall back to the
    // terrain while their cell is unexplored; every other object, rocks, toys, bridges and the hills of every colony, keeps its colour in the fog
    std::vector<uint8_t> pixels(static_cast<size_t>(rw * rh), 0);   // palette indices
    for (int32_t y = 0; y < rh; ++y) {
        const int32_t cy = std::min<int32_t>(map_h - 1, y * map_h / rh);
        for (int32_t x = 0; x < rw; ++x) {
            const int32_t cx = std::min<int32_t>(map_w - 1, x * map_w / rw);
            const auto& cell = world.cells[static_cast<size_t>(cy) * world.width + static_cast<size_t>(cx)];
            const bool explored = !fog || world.is_tile_revealed(cx, cy);
            const uint16_t id = minimap_object_id(cell);
            bool object = id != kMinimapNoObject && !minimap_is_bomb(id);
            if (object && !explored) {
                const uint8_t flags = sim::movement::tile_flags_of(id);
                object = (flags & (sim::movement::kTileFlagFood | sim::movement::kTileFlagPowerUp)) == 0 && id != sim::TILE_FIREWALL;
            }
            uint8_t colour;
            if (object) {
                colour = static_cast<uint8_t>(minimap_object_entry(id) & 0xffu);
            } else if (explored) {
                colour = radar_terrain_[static_cast<size_t>(y * rw + x)];
            } else {
                const uint8_t cls = minimap_class(cell);
                colour = kMinimapFogColours[cls < 8 ? cls : 0];
            }
            pixels[static_cast<size_t>(y * rw + x)] = colour;
        }
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(rw * rh) * 4u, 255);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const auto& c = palette[pixels[i]];
        rgba[i * 4 + 0] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
    }
    renderer.draw_rgba_image(rx, ry, rw, rh, rgba.data());

    // The scale of the image: world pixels per image pixel are (map * 32) / 119 across and / 91 down (FUN_0100925b). A world point or length divided by it is truncated
    const int64_t world_w_px = static_cast<int64_t>(map_w) * 32;
    const int64_t world_h_px = static_cast<int64_t>(map_h) * 32;
    auto to_image_x = [&](int64_t world_px) { return static_cast<int32_t>(world_px * rw / world_w_px); };
    auto to_image_y = [&](int64_t world_px) { return static_cast<int32_t>(world_px * rh / world_h_px); };

    // FUN_01009988 paints the dots of the world's object list over the image, in list order and in screen coordinates (a dot is not clipped to the image): plants first (made with
    // the match screen), then the ants in the order they were made. A dot is a filled rectangle centred on the object's position (FUN_01009899): the size flag of its record
    // (one or two cells, 32 world pixels each) scaled down and truncated, at least `min_size`; the top edge is taken from half the WIDTH, as the original does
    auto dot = [&](int32_t world_x, int32_t world_y, uint8_t colour, int32_t size_flag, int32_t min_size) {
        const int32_t w = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(32) * size_flag * rw / world_w_px), min_size);
        const int32_t h = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(32) * size_flag * rh / world_h_px), min_size);
        const int32_t cx = to_image_x(world_x);
        const int32_t cy = to_image_y(world_y);
        int32_t left, top, right, bottom;
        if (w > 1 && h > 1) {
            left = cx - (w >> 1);
            top = cy - (w >> 1);
            right = cx + (w - (w >> 1));
            bottom = cy + (h - (h >> 1));
        } else {
            left = std::min(cx, rw - 1);
            top = std::min(cy, rh - 1);
            right = left + w;
            bottom = top + h;
        }
        if (right > left && bottom > top) {
            const auto& c = palette[colour];
            renderer.fill_rect(rx + left, ry + top, right - left, bottom - top, {c.r, c.g, c.b, 255});
        }
    };
    for (const auto& plant : world.plants) {
        const uint16_t entry = minimap_object_entry(plant.tile_id);
        const int32_t size_flag = entry >> 8;
        if (size_flag > 0) dot(plant.x * 32 + 16, plant.y * 32 + 16, static_cast<uint8_t>(entry & 0xffu), size_flag, 0);
    }
    // In fog an ant shows when it is the viewer's own, an ally's, or stands on an explored cell (FUN_0101aa0d); all ants have size flag 1 and a minimum size of 2
    const uint8_t ally = local_player_id_ < world.player_alliances.size() ? world.player_alliances[local_player_id_] : uint8_t{255};
    for (const auto& ant : world.ants) {
        if (fog && ant.player_id != local_player_id_ && ant.player_id != ally && !world.is_tile_revealed(ant.px / 32, ant.py / 32)) continue;
        dot(ant.px, ant.py, kMinimapAntColours[ant.player_id % 4], 1, 2);
    }

    // The view frame (0x1009ae2 - 0x1009b6f, drawn last with GDI FrameRect in (251, 251, 255)): the size of the view scaled down plus one, at the view's origin scaled down, moved
    // inside the image when it would end beyond its right or bottom edge
    const int32_t frame_w = to_image_x(layout_.view().w) + 1;
    const int32_t frame_h = to_image_y(layout_.view().h) + 1;
    int32_t fl = rx + to_image_x(static_cast<int32_t>(camera.x));
    int32_t ft = ry + to_image_y(static_cast<int32_t>(camera.y));
    int32_t fr = fl + frame_w;
    int32_t fb = ft + frame_h;
    if (fr >= rx + rw) { fr = rx + rw; fl = fr - frame_w; }
    if (fb >= ry + rh) { fb = ry + rh; ft = fb - frame_h; }
    renderer.draw_rect(fl, ft, fr - fl, fb - ft, {251, 251, 255, 255});
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

    // The box (FUN_01021e36): a team that exists and has not dropped out is filled with its colour; with an ally the right half (from left + (right - left) / 2) takes the
    // ally's colour and the score shown is the sum of the two; any other team's box is covered (scorcovr, 58 x 17 at (left - 2, top - 1))
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

// The frame of the quit dialog and of the alliance dialogs: the 20 parts of Table-4 animation std_dialg, origin (100, 100), no dim layer
static void draw_std_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    const int32_t dx = 100;
    const int32_t dy = 100;
    const auto* anim = assets.find_animation("std_dialg");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, dx + fr.dx, dy + fr.dy);
        }
    } else {
        renderer.fill_rect(dx, dy, 320, 224, assets::ColorRGBA{219, 75, 19, 255});
    }
}

void HUD::render_quit_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;

    // Quit dialog (Ants.exe 0x10142cb): no dim layer, origin (100,100) (OffsetRect(100,100) on the dialog's children)
    draw_std_dialog(renderer, assets);

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

// ------------------------------------------------------------------------------------------------
// Alliance dialogs (Ants.exe FUN_01015b65 invitation, FUN_010160e2 waiting, FUN_01016438 confirmation; docs 5.42)
// ------------------------------------------------------------------------------------------------

std::string HUD::alliance_colour_word(uint8_t team) const {
    return sim::strings::colour_name(static_cast<uint8_t>(3u - (team & 3u)));      // strings 100 - 103: black, blue, red, green by colour index
}

bool HUD::ally_pedestal_possible(const sim::WorldState& world) const {
    if (local_player_id_ < sim::MAX_PLAYERS && (world.dropped_mask & (1u << local_player_id_)) != 0) return false;
    size_t live = 0;
    for (const auto& hill : world.anthills) {
        if (hill.team_id >= sim::MAX_PLAYERS || (world.dropped_mask & (1u << hill.team_id)) == 0) ++live;
    }
    return live > 2;
}

std::string HUD::alliance_name(uint8_t team) const {
    return team < team_names_.size() && !team_names_[team].empty() ? team_names_[team] : alliance_colour_word(team);
}

void HUD::open_alliance_dialog(AllianceDialog kind, uint8_t other, std::string text) {
    release_capture();
    alliance_dialog_ = kind;
    alliance_other_ = other;
    alliance_text_ = std::move(text);
    // the buttons are the original's animations (dad_bacc / dad_bdec, dw_bwith, dyn_byes / dyn_bno) whose single part sits at these offsets from the
    // dialog's origin (100, 100)
    alliance_button_a_ = {};
    alliance_button_b_ = {};
    switch (kind) {
        case AllianceDialog::Invitation:
            alliance_button_a_ = {152, 260, 80, 24, false, false};      // Accept  (accpt1.bmp at part offset (52, 160))
            alliance_button_b_ = {284, 260, 80, 24, false, false};      // Decline (decl1.bmp at (184, 160))
            break;
        case AllianceDialog::Waiting:
            alliance_button_a_ = {220, 260, 80, 23, false, false};      // Withdraw (withd1.bmp at (120, 160))
            break;
        case AllianceDialog::BreakConfirm:
            alliance_button_a_ = {180, 260, 49, 24, false, false};      // Yes (yes1.bmp at (80, 160))
            alliance_button_b_ = {292, 260, 49, 24, false, false};      // No  (no1.bmp at (192, 160))
            break;
        case AllianceDialog::None:
            break;
    }
}

void HUD::close_alliance_dialog() noexcept {
    alliance_dialog_ = AllianceDialog::None;
    alliance_other_ = 255;
    alliance_text_.clear();
    alliance_replaces_team_ = false;
    alliance_button_a_ = {};
    alliance_button_b_ = {};
}

// The dialogs follow the simulation's state, not an event, so that they are the same on every machine of a network match and a dialog cannot be missed.
// An answer takes a few turns to reach the simulation in a network match: the question or the waiting dialog of an offer that was just answered does not
// come back meanwhile (`suppressed_*`). The original closes the dialogs of a team that dropped out (FUN_0100c4ed); the simulation clears its offers then.
void HUD::update_alliance_dialog(const sim::WorldState& world) {
    const uint8_t me = local_player_id_;
    if (me >= sim::MAX_PLAYERS) return;
    if (suppressed_invite_from_ != 255 && world.pending_invite_from[me] != suppressed_invite_from_) suppressed_invite_from_ = 255;
    if (suppressed_wait_for_ != 255 && world.pending_invite_from[suppressed_wait_for_] != me) suppressed_wait_for_ = 255;
    switch (alliance_dialog_) {
        case AllianceDialog::Invitation:
            if (world.pending_invite_from[me] != alliance_other_) close_alliance_dialog();
            break;
        case AllianceDialog::Waiting:
            if (alliance_other_ >= sim::MAX_PLAYERS || world.pending_invite_from[alliance_other_] != me) close_alliance_dialog();
            break;
        case AllianceDialog::BreakConfirm:
            if (world.player_alliances[me] != alliance_other_) {                      // nothing left to break
                close_alliance_dialog();
                pending_break_ = PendingBreak{};
            }
            break;
        case AllianceDialog::None:
            break;
    }
    if (alliance_dialog_ != AllianceDialog::None || is_modal_open()) return;          // one dialog at a time, none over another
    const uint8_t from = world.pending_invite_from[me];
    if (from < sim::MAX_PLAYERS && from != suppressed_invite_from_) {
        const uint8_t old_ally = world.player_alliances[me];
        const bool replaces = old_ally < sim::MAX_PLAYERS && old_ally != from;       // string 2 when accepting ends the present team
        open_alliance_dialog(AllianceDialog::Invitation, from,
                             replaces ? sim::strings::format(sim::strings::kInviteBreakDialog, alliance_name(from), alliance_colour_word(from),
                                                             alliance_name(old_ally), alliance_colour_word(old_ally))
                                      : sim::strings::format(sim::strings::kInviteDialog, alliance_name(from), alliance_colour_word(from)));
        alliance_replaces_team_ = replaces;
        return;
    }
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        if (t != me && world.pending_invite_from[t] == me && t != suppressed_wait_for_) {
            open_alliance_dialog(AllianceDialog::Waiting, t, sim::strings::format(sim::strings::kWaitingForAnswer, alliance_name(t), alliance_colour_word(t)));
            return;
        }
    }
}

void HUD::render_alliance_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;
    draw_std_dialog(renderer, assets);
    // the label (30, 10) of the dialog, 24 px, centred, colour (31, 23, 51); 270 px wide in the invitation, 240 px in the other two
    draw_label(renderer, alliance_text_, 130, 110, alliance_dialog_ == AllianceDialog::Invitation ? 270 : 240, ColorRGBA{31, 23, 51, 255}, FontSize::Px24, true);
    auto button = [&](const UIButton& b, const char* up, const char* hover, const char* down) {
        if (b.w > 0) draw_animation_frame0(renderer, assets, b.is_pressed ? down : (b.is_active ? hover : up), 100, 100);
    };
    switch (alliance_dialog_) {
        case AllianceDialog::Invitation:
            button(alliance_button_a_, "dad_bacc1", "dad_bacc2", "dad_bacc3");
            button(alliance_button_b_, "dad_bdec1", "dad_bdec2", "dad_bdec3");
            break;
        case AllianceDialog::Waiting:
            button(alliance_button_a_, "dw_bwith1", "dw_bwith2", "dw_bwith3");
            break;
        case AllianceDialog::BreakConfirm:
            button(alliance_button_a_, "dyn_byes1", "dyn_byes2", "dyn_byes3");
            button(alliance_button_b_, "dyn_bno1", "dyn_bno2", "dyn_bno3");
            break;
        case AllianceDialog::None:
            break;
    }
}

void HUD::answer_alliance_dialog(sim::SimulationEngine& sim, bool yes) {
    const uint8_t me = local_player_id_;
    const uint8_t other = alliance_other_;
    auto command = [&](sim::CommandType type, uint8_t target = 255) {
        sim::Command c;
        c.type = type;
        c.issuer = me;
        c.other_player = target;
        submit_command(sim, c);
    };
    const AllianceDialog kind = alliance_dialog_;
    const bool replaces = alliance_replaces_team_;
    PendingBreak pending = std::move(pending_break_);
    pending_break_ = PendingBreak{};
    close_alliance_dialog();
    switch (kind) {
        case AllianceDialog::Invitation:                                              // FUN_01016081
            suppressed_invite_from_ = other;
            if (yes) {
                if (replaces) command(sim::CommandType::AllianceBreak);              // the present team is broken first (FUN_01010d26)
                command(sim::CommandType::AllianceAccept, other);
            } else {
                command(sim::CommandType::AllianceDeny, other);
            }
            break;
        case AllianceDialog::Waiting:                                                 // callback 0x10163f1: the offer is taken back
            suppressed_wait_for_ = other;
            command(sim::CommandType::AllianceWithdraw, other);
            break;
        case AllianceDialog::BreakConfirm:                                            // callbacks FUN_0100c838 and FUN_01020076
            if (!yes) break;
            command(sim::CommandType::AllianceBreak);
            if (pending.action == PendingBreak::Action::Invite) {
                command(sim::CommandType::AllianceInvite, pending.target);
            } else if (pending.action == PendingBreak::Action::Attack) {
                issue_group_order(sim, pending.tile, false, true, pending.ants);
            }
            break;
        case AllianceDialog::None:
            break;
    }
}

void HUD::request_team_up(sim::SimulationEngine& sim, uint8_t target) {
    const uint8_t me = local_player_id_;
    if (me >= sim::MAX_PLAYERS || target >= sim::MAX_PLAYERS || target == me) return;
    const uint8_t ally = sim.get_world_state().player_alliances[me];
    if (ally < sim::MAX_PLAYERS) {                                                    // FUN_0100c7ac: with an ally the confirmation comes first
        if (is_modal_open()) return;
        pending_break_ = PendingBreak{PendingBreak::Action::Invite, target, {}, {}};
        open_alliance_dialog(AllianceDialog::BreakConfirm, ally,
                             sim::strings::format(sim::strings::kBreakTeamConfirm, alliance_name(ally), alliance_colour_word(ally)));
        return;
    }
    sim::Command invite;                                                              // FUN_0100c838(1): the offer goes out
    invite.type = sim::CommandType::AllianceInvite;
    invite.issuer = me;
    invite.other_player = target;
    submit_command(sim, invite);
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
}

void HUD::select_base(int32_t team_id) noexcept {
    selected_base_team_id_ = team_id;
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    is_multi_select_mode_ = false;
    selection_status_pending_ = true;
}

void HUD::clear_selection() noexcept {
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    is_multi_select_mode_ = false;
    selection_status_pending_ = true;      // every deselect clears the status text (FUN_01028c44)
}

bool HUD::is_ant_selected(uint32_t id) const noexcept {
    if (id == 0) return false;
    return std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), id) != selected_ant_ids_.end();
}

bool HUD::is_shift_held() const noexcept {
    return shift_held_ || ((static_cast<uint16_t>(SDL_GetModState()) & KMOD_SHIFT) != 0);
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
        const bool combat = (ant.raw_type == sim::AntType::Combat);       // the own type field, as the hit box reads it (0x1026a3d)
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
        is_multi_select_mode_ = true;                      // panel 4 whatever the count: 0x1027731 calls FUN_01027f07 with mode 4 for any non-empty pick, and the
                                                           // group is then a group even when it holds the one bomber that was selected already
        selection_status_pending_ = true;                  // a shift-add rebuilds the panel and posts its text (FUN_01027f07 with its last argument 0, 0x1027950)
        unlatch_pedestals();
        return;
    }

    // Otherwise the old selection is cleared first, even when nothing is picked (dragging over empty ground deselects)
    selected_ant_ids_ = std::move(picked);
    selected_base_team_id_ = -1;
    selected_ant_id_ = selected_ant_ids_.empty() ? 0 : selected_ant_ids_.front();
    is_multi_select_mode_ = selected_ant_ids_.size() > 1;
    selection_status_pending_ = true;
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
    mouse_x_ = x;                                       // the event carries the pointer: the polled position of the original
    mouse_y_ = y;

    // 0. Overlays and Modals intercept clicks first
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT) quick_help_return_.on_press(x, y);     // the Return button captures (qh_return3 carries no sound); nothing else reacts
        return true;
    }
    if (options_.is_open()) {
        // The window takes every press; only the left button acts (the controls' mouse handlers test the left button message): the start of a slider
        // drag, a captured button, the focus of an edit field. Nothing acts before the release, and a press outside the controls does nothing.
        if (button == SDL_BUTTON_LEFT) options_.on_press(x, y, clock_ms());
        return true;
    }
    if (show_match_start_modal_) {
        return true; // Consume all clicks while match start modal is open
    }
    if (alliance_dialog_ != AllianceDialog::None) {
        if (button == SDL_BUTTON_LEFT) {
            for (UIButton* b : {&alliance_button_a_, &alliance_button_b_}) {
                if (b->w > 0 && b->contains(x, y)) {
                    b->is_pressed = true;
                    play_sfx(sim::SoundID::ButtonClick);
                    return true;
                }
            }
        }
        return true;                                           // a dialog gets every click
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
                         static_cast<int32_t>(sim.grid().height()), layout_).dir >= 0) {
        return true;
    }

    // The chat log window's own mouse handler (FUN_01012015, the left button): a press inside its view starts the drag
    if (button == SDL_BUTTON_LEFT && in_chat_view(x, y)) {
        start_chat_drag(x, y);
        return true;
    }

    if (button == SDL_BUTTON_RIGHT) {
        // The press saves its point and captures the view under it (FUN_01028751); the order is given at the release (FUN_01027b51)
        right_press_x_ = x;
        right_press_y_ = y;
        right_capture_ = over_minimap(x, y) ? 2 : (over_map(x, y) ? 1 : 0);
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
    if (options_.state().chat && send_to_button_.contains(x, y)) {
        send_to_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // [Team] button (579, 443, 46x24): exists while the local player has an ally
    if (options_.state().chat && is_on_team_ && team_button_.contains(x, y)) {
        team_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // The press captures the view under it (FUN_01028751): the minimap (the view follows in the input ticks while the button is held),
    // then the map (a rubber band that is decided at the release)
    if (over_minimap(x, y)) {
        is_radar_dragging_ = true;
        return true;
    }
    if (over_map(x, y)) {
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
    mouse_x_ = x;
    mouse_y_ = y;
    const bool captured_before = is_input_captured();      // [5534] != 0 while the event is processed
    if (button == SDL_BUTTON_LEFT) end_chat_drag();        // FUN_01012015: the release of the left button ends a drag of the chat log
    // The button class (FUN_01011206 / FUN_01011281): the callback runs at the release when the button is still captured, that is when the
    // pointer has not left it (leaving cancels the capture for good)
    const bool left_release = (button == SDL_BUTTON_LEFT);
    const bool fire_help = left_release && help_button_.is_pressed && help_button_.contains(x, y);
    const bool fire_options = left_release && options_button_.is_pressed && options_button_.contains(x, y);
    const bool fire_quit = left_release && quit_button_.is_pressed && quit_button_.contains(x, y);
    const bool fire_all = left_release && options_.state().chat && send_to_button_.is_pressed && send_to_button_.contains(x, y);
    const bool fire_team = left_release && options_.state().chat && is_on_team_ && team_button_.is_pressed && team_button_.contains(x, y);
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
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT && quick_help_return_.on_release(x, y)) close_quick_help();        // the callback runs at the release, on the button
        return true;
    }
    if (options_.is_open()) {
        // The callbacks run at the release: Return, the pairs of switches and the sliders' drags (FUN_010115ca, FUN_01011206)
        if (button == SDL_BUTTON_LEFT) options_.on_release(x, y);
        return true;
    }

    if (show_match_start_modal_) {
        return true;
    }

    if (alliance_dialog_ != AllianceDialog::None) {
        for (UIButton* b : {&alliance_button_a_, &alliance_button_b_}) {
            if (!b->is_pressed) continue;
            b->is_pressed = false;
            if (b->contains(x, y)) answer_alliance_dialog(sim, b == &alliance_button_a_);      // the action runs at the release, on the button
            return true;
        }
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
                         static_cast<int32_t>(sim.grid().height()), layout_).dir >= 0;

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
        return was_dragging || over_map(x, y);
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

    if (show_quick_help_) {
        quick_help_return_.on_move(x, y);
        return true;
    }
    if (options_.is_open()) {
        options_.on_move(x, y);                   // hover pictures; a dragged thumb follows the pointer (only the thumb: the value is applied at the release)
        return true;
    }

    // The dialogs' buttons are the button class too (FUN_01011281): leaving a pressed button cancels its capture for good, coming back only hovers
    if (alliance_dialog_ != AllianceDialog::None) {
        for (UIButton* b : {&alliance_button_a_, &alliance_button_b_}) {
            if (b->is_pressed && !b->contains(x, y)) b->is_pressed = false;
        }
        alliance_button_a_.is_active = alliance_button_a_.w > 0 && alliance_button_a_.contains(x, y);
        alliance_button_b_.is_active = alliance_button_b_.w > 0 && alliance_button_b_.contains(x, y);
        return true;
    }

    if (show_quit_dialog_) {
        for (UIButton* b : {&yes_button_, &no_button_}) {
            if (b->is_pressed && !b->contains(x, y)) b->is_pressed = false;
        }
        yes_button_.is_active = yes_button_.contains(x, y);
        no_button_.is_active = no_button_.contains(x, y);
        return true;
    }

    if (chat_dragging_) {                          // FUN_01012096
        move_chat_drag(x, y);
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
    const int32_t rate = options_.state().scroll_speed;       // the profile's Scroll Speed 0 .. 99 (FUN_01027329: the half extent is rate + 10)
    EdgeScroll step;
    if (is_radar_dragging_) {
        step = minimap_scroll_step(mouse_x, mouse_y, camera.world_x, camera.world_y, static_cast<int32_t>(map_w), static_cast<int32_t>(map_h), layout_);
    } else if (!is_input_captured()) {
        step = edge_scroll_step(mouse_x, mouse_y, rate, camera.world_x, camera.world_y, static_cast<int32_t>(map_w), static_cast<int32_t>(map_h), layout_);
    }
    if (step.dx == 0 && step.dy == 0) return false;
    camera.scroll_pixels(step.dx, step.dy, map_w, map_h);
    return true;
}

bool HUD::handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod, bool repeat) {
    // FUN_0102609a. A dialog takes every key first.
    if (show_match_start_modal_) return true;

    if (alliance_dialog_ != AllianceDialog::None) {            // the dialogs' own key handlers (0x1016015, 0x10163d5, 0x1016761): a dialog takes every key
        const bool esc = key == SDLK_ESCAPE;
        switch (alliance_dialog_) {
            case AllianceDialog::Invitation:                   // A = Accept, D and Esc = Decline
                if (key == 'a' || key == 'A') answer_alliance_dialog(sim, true);
                else if (key == 'd' || key == 'D' || esc) answer_alliance_dialog(sim, false);
                break;
            case AllianceDialog::Waiting:                      // W and Esc = Withdraw
                if (key == 'w' || key == 'W' || esc) answer_alliance_dialog(sim, true);
                break;
            case AllianceDialog::BreakConfirm:                 // Y = Yes, N and Esc = No
                if (key == 'y' || key == 'Y') answer_alliance_dialog(sim, true);
                else if (key == 'n' || key == 'N' || esc) answer_alliance_dialog(sim, false);
                break;
            case AllianceDialog::None:
                break;
        }
        return true;
    }

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

    if (options_.is_open()) {
        // FUN_01014f12: Enter closes the window whatever has the focus; Backspace goes to the edit field that has it; Esc does nothing. Printable keys arrive
        // as text input (SDL_TEXTINPUT), not as key events.
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) options_.on_key(ScreenEdit::KEY_ENTER);
        else if (key == SDLK_BACKSPACE) options_.on_key(ScreenEdit::KEY_BACKSPACE);
        else if (key == SDLK_ESCAPE) options_.on_key(ScreenEdit::KEY_ESCAPE);
        return true;
    }

    const bool ctrl = (mod & KMOD_CTRL) != 0 || (mod & KMOD_GUI) != 0;
    const auto& world = sim.get_world_state();

    // The chat edit control is always active while chat is on: it takes 0x20 - 0x7E and Backspace unless Ctrl is held
    if (options_.state().chat && !ctrl) {
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
            if (!repeat && options_.state().chat) trigger_quick_chat(static_cast<size_t>(key - SDLK_F9));
            return true;
        case SDLK_RETURN: case SDLK_KP_ENTER:                  // FUN_010103eb: the team when the player has an ally, else everybody
            send_chat_message();
            return true;
        case SDLK_ESCAPE:                                      // FUN_01028c44(0): deselect everything, no quit dialog
            clear_selection();
            unlatch_pedestals();
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
                                   camera.world_x, camera.world_y, dx, dy, layout_.view().w, layout_.view().h);
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
    if (options_.is_open()) {
        options_.on_text(text);                   // the focused edit field takes the printable characters (at most 100)
        return;
    }
    if (show_quit_dialog_ || show_quick_help_ || show_match_start_modal_ || !options_.state().chat) return;     // a dialog or the chat cover takes the keys
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
    if (!options_.state().chat) {                     // "Participate In Chat" off: the box is covered and nothing is sent
        chat_input_.clear();
        return;
    }
    add_chat_entry(player_name_.empty() ? "Player" : player_name_, chat_input_, to_team && is_on_team_);
    if (on_chat_send_) on_chat_send_(chat_input_, to_team && is_on_team_);
    chat_input_.clear();
}

// The callbacks of the options screen (FUN_01015058 / FUN_01015092 / FUN_010150bc, FUN_01014f5a .. FUN_0101501f, FUN_0100bbf2): the profile is written first, then the
// setting is applied. The Sound Volume sets the volume and plays the test voice (gantrdy, 0x102bd7e), the Music Volume sets the music volume and restarts the
// track (the owner's callback). The Scroll Speed is read by the input tick, the two switches and the quick chats by the chat box and the start of the program.
void HUD::apply_option(OptionSetting setting) {
    const OptionsState& state = options_.state();
    if (config_store_ != nullptr) state.write(*config_store_, setting);
    switch (setting) {
        case OptionSetting::SoundVolume:
            if (on_sfx_volume_) on_sfx_volume_(state.sound_volume);
            play_sfx(sim::SoundID::GeneralReady);
            break;
        case OptionSetting::MusicVolume:
            if (on_music_volume_) on_music_volume_(state.music_volume);
            break;
        default:
            break;
    }
}

void HUD::trigger_quick_chat(size_t index) {
    if (index >= 4 || !options_.state().chat) return;
    if (options_.state().quick_chat[index].empty()) return;
    add_chat_entry(player_name_.empty() ? "Player" : player_name_, options_.state().quick_chat[index], false);   // F9 - F12 always go to all
    if (on_chat_send_) on_chat_send_(options_.state().quick_chat[index], false);
}

namespace {

constexpr size_t kChatBodyMaxChars = 100;
constexpr size_t kChatHeaderMaxChars = 50;      // the header text object holds 50 characters

// What measures the text when no renderer was given (every character 6 px wide: the IRenderer defaults)
class DefaultTextMetrics final : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string&, int32_t, int32_t, assets::ColorRGBA) override {}
    void set_hud_team(uint8_t) override {}
};

const IRenderer& default_text_metrics() {
    static const DefaultTextMetrics metrics;
    return metrics;
}

}  // namespace

void HUD::add_chat_entry(const std::string& sender, const std::string& message, bool team_only, int colour_index) {
    const uint8_t colour = static_cast<uint8_t>(colour_index >= 0 && colour_index <= 3 ? colour_index : (3 - (local_player_id_ & 3)));
    push_chat_entry(sender + (team_only ? " (To Teammate):" : ":"), message, colour);
}

void HUD::set_text_metrics(const IRenderer* metrics) {
    if (text_metrics_ == metrics) return;
    text_metrics_ = metrics;
    relayout_chat();
}

// The labels of an entry (FUN_010123e2): the header is one line (no wrap), the body wraps at 126 px; both are 12 px labels. The entry starts at `top`, the body under the header.
void HUD::layout_chat_entry(ChatEntry& entry, int32_t top) const {
    const IRenderer& metrics = text_metrics_ != nullptr ? *text_metrics_ : default_text_metrics();
    const int32_t line = font_cell_height(FontSize::Px12);
    entry.top = top;
    entry.header_h = line;
    entry.body_lines = wrap_label_text(metrics, entry.body, kChatBodyW, FontSize::Px12);
    entry.body_h = static_cast<int32_t>(entry.body_lines.size()) * line;
}

void HUD::append_chat_display_lines(const ChatEntry& entry) {
    chat_log_.push_back(entry.header);
    chat_line_colour_.push_back(entry.colour);
    for (const std::string& line : entry.body_lines) {
        chat_log_.push_back(line);
        chat_line_colour_.push_back(5);
    }
}

// The measure changed: every label is measured again and the entries are restacked; the window's positions stay inside the new log
void HUD::relayout_chat() {
    chat_log_.clear();
    chat_line_colour_.clear();
    int32_t top = 0;
    for (ChatEntry& entry : chat_entries_) {
        layout_chat_entry(entry, top);
        append_chat_display_lines(entry);
        top = entry.bottom() + 1;
    }
    chat_content_end_ = top;
    const int32_t limit = std::max(0, chat_content_end_ - layout_.chat_view().h);
    chat_follow_pos_ = std::min(chat_follow_pos_, limit);
    chat_follow_target_ = std::min(chat_follow_target_, limit);
    chat_drag_offset_ = std::min(chat_drag_offset_, limit);
}

// AddLine (0x10120e9): the entry is made at the end of the log (`+0x2c`), the end moves to its bottom + 1, and when the end is now below the window the window's target is the
// position that puts the new bottom on the window's last row; the follow task (CHATAPPD) is started unless it is running (the window is not at its target)
void HUD::push_chat_entry(std::string header, const std::string& message, uint8_t colour) {
    if (header.size() > kChatHeaderMaxChars) header.resize(kChatHeaderMaxChars);
    ChatEntry entry;
    entry.header = std::move(header);
    entry.body = message.substr(0, kChatBodyMaxChars);
    entry.colour = colour;
    layout_chat_entry(entry, chat_content_end_);
    append_chat_display_lines(entry);
    const int32_t bottom = entry.bottom();
    chat_content_end_ = bottom + 1;
    chat_entries_.push_back(std::move(entry));
    const int32_t view_h = layout_.chat_view().h;
    if (view_h < chat_content_end_ - chat_follow_pos_) {
        if (chat_follow_pos_ == chat_follow_target_) {
            chat_follow_task_ = true;
            chat_follow_due_ms_ = clock_ms();                   // AddTask(task, 0, 50 ms, 0): the first pass is the next one
        }
        chat_follow_target_ = bottom - view_h;
    }
}

void HUD::add_news_flash(uint32_t elapsed_ms, const std::string& text) {
    const uint32_t secs = elapsed_ms / 1000;
    char header[48];
    std::snprintf(header, sizeof(header), "[%u:%02u] News Flash:", secs / 60, secs % 60);
    push_chat_entry(header, text, 4);
}

void HUD::receive_chat_message(uint8_t sender, const std::string& name, const std::string& text, bool to_team, const sim::WorldState& world) {
    if (!options_.state().chat) return;                                       // the receive handler drops it (0x102411a)
    if (to_team && sender != local_player_id_) {
        const bool sender_ally_is_me = sender < world.player_alliances.size() && world.player_alliances[sender] == local_player_id_;
        if (!sender_ally_is_me) return;                               // team text: only the sender and the players whose ally the sender is
    }
    add_chat_entry(name, text, to_team, 3 - (sender & 3));
}

// CHATAPPD (0x1025282, every 50 ms) and CHATSCRL (0x1025234, every 100 ms) run on the list scheduler, which runs a task at the next pass after it is due and
// then a period later
void HUD::update_chat_tasks() {
    const uint32_t now = clock_ms();
    if (chat_follow_task_ && static_cast<int32_t>(now - chat_follow_due_ms_) >= 0) {
        if (chat_follow_pos_ == chat_follow_target_) {
            chat_follow_task_ = false;                                  // the task returns 0 and is removed
        } else {
            // the position moves toward the target by at most 5 px per pass
            const int32_t step = std::min(kChatFollowStep, std::abs(chat_follow_target_ - chat_follow_pos_));
            chat_follow_pos_ += chat_follow_target_ > chat_follow_pos_ ? step : -step;
            chat_follow_due_ms_ = now + kChatFollowPeriodMs;
        }
    }
    if (chat_scroll_task_ && static_cast<int32_t>(now - chat_scroll_due_ms_) >= 0) {
        // the pointer of the last press or drag event: outside the view the log scrolls 15 px per pass (up while it is above the view, down from below)
        if (!in_chat_view(chat_drag_x_, chat_drag_y_)) chat_scroll_by(chat_drag_y_ >= layout_.chat_view().y ? -kChatScrollStep : kChatScrollStep);
        chat_scroll_due_ms_ = now + kChatScrollPeriodMs;
    }
}

// 0x101228a: the dragged position moves by `delta` inside the log (only when the log is at least as high as the view)
void HUD::chat_scroll_by(int32_t delta) noexcept {
    const int32_t view_h = layout_.chat_view().h;
    if (view_h <= chat_content_end_) {
        const int32_t room_below = (chat_content_end_ - view_h) - chat_drag_offset_;
        const int32_t room_above = -chat_drag_offset_;
        const int32_t d = std::min(std::max(delta, room_above), room_below);
        chat_drag_offset_ += d;
    }
}

// 0x1012015, a left press: inside the view the drag starts from the window's current position and CHATSCRL is scheduled
void HUD::start_chat_drag(int32_t x, int32_t y) {
    chat_drag_x_ = x;
    chat_drag_y_ = y;
    chat_drag_offset_ = chat_follow_pos_;
    chat_dragging_ = true;
    if (!chat_scroll_task_) {
        chat_scroll_task_ = true;
        chat_scroll_due_ms_ = clock_ms();
    }
}

// 0x1012096: while dragging, the pointer is remembered and a move inside the view scrolls the log by the distance the pointer went up
void HUD::move_chat_drag(int32_t x, int32_t y) {
    if (!chat_dragging_) return;
    const int32_t delta = chat_drag_y_ - y;
    chat_drag_x_ = x;
    chat_drag_y_ = y;
    if (in_chat_view(x, y)) chat_scroll_by(delta);
}

// 0x1012015, the release: the log shows the window that follows the newest entry again
void HUD::end_chat_drag() noexcept {
    chat_dragging_ = false;
    chat_scroll_task_ = false;
}

// FUN_01012190: the entries that reach into the view are drawn at their place, the header at the left edge and the body 10 px right of it, clipped to the view
void HUD::render_chat_log(IRenderer& renderer) {
    static const assets::ColorRGBA kChatColours[6] = {
        {39, 39, 59, 255}, {43, 39, 107, 255}, {119, 0, 0, 255}, {7, 67, 47, 255}, {79, 0, 143, 255}, {7, 11, 15, 255}};
    const int32_t offset = chat_view_offset();
    const int32_t line = font_cell_height(FontSize::Px12);
    const LayoutRect view = layout_.chat_view();
    renderer.set_clip_rect(view.x, view.y, view.w, view.h);
    for (const ChatEntry& entry : chat_entries_) {
        if (entry.bottom() <= offset) continue;
        if (entry.top >= offset + view.h) break;
        const int32_t y = view.y + entry.top - offset;
        draw_single_line_label(renderer, entry.header, view.x, y, view.w, true, kChatColours[std::min<size_t>(entry.colour, 5)], FontSize::Px12);
        int32_t body_y = y + entry.header_h;
        for (const std::string& body_line : entry.body_lines) {
            renderer.draw_text(body_line, view.x + kChatBodyX, body_y, kChatColours[5], FontSize::Px12);
            body_y += line;
        }
    }
    renderer.clear_clip_rect();
}

// What the original writes into chat.txt when the program ends (0x10122d4): "%s @ %s\n\n" with the date and the time, then "%s %s\n" for every entry
std::string HUD::chat_transcript(const std::string& date_time) const {
    std::string out = date_time + "\n\n";
    for (const ChatEntry& entry : chat_entries_) out += entry.header + " " + entry.body + "\n";
    return out;
}

} // namespace ants::app

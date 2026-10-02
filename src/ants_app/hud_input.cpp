// The pointer model of the match screen (Ants.exe INPUT task): the cursor decision table (FUN_01026aa3), the ant under the pointer
// (FUN_01026904 / FUN_01026a39), the release of the left button with its rubber band (FUN_01027530), the click by cursor mode
// (FUN_010277f4), the right button (FUN_01027b51), the group order dispatch (FUN_010287b5) and the command pedestals (FUN_010274be /
// FUN_01028d30 / FUN_01028ee0). docs/GAME_REVERSE_ENGINEERING.md 5.44.
#include "ants_app/hud.hpp"

#include <algorithm>

#include "ants_sim/game_strings.hpp"

namespace ants::app {

namespace {

// Pedestal slot rectangles (FUN_01028d30; half-open): slot 1 (Move / hatch / ally), slot 2 (ability), slot 3 (Stop)
struct SlotRect { int32_t x0, y0, x1, y1; };
constexpr SlotRect kSlot[3] = {{482, 152, 525, 225}, {539, 152, 582, 225}, {597, 189, 628, 227}};

bool in_slot(int slot, int32_t x, int32_t y) {
    const SlotRect& r = kSlot[slot];
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

const assets::AnthillSpawn* hill_at(const sim::WorldState& world, int32_t tx, int32_t ty) {
    for (const auto& base : world.anthills) {
        if (tx >= base.x && tx < base.x + 4 && ty >= base.y && ty < base.y + 4) return &base;
    }
    return nullptr;
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// Panel mode and the selection's type
// ------------------------------------------------------------------------------------------------

HUD::PanelMode HUD::panel_mode(const sim::WorldState& world) const {
    if (selected_base_team_id_ >= 0) return PanelMode::Base;
    size_t own = 0;
    for (uint32_t id : selected_ant_ids_) {
        for (const auto& a : world.ants) {
            if (a.id == id && a.player_id == local_player_id_) { ++own; break; }
        }
    }
    // [54ec] is stored, not counted: a shift add or a shift drag sets panel 4 even when the ant it adds is the one that was already selected, and only
    // the next selection operation changes it (FUN_01027f07 is called with the mode, never with a count). Panel 4 beats the ant count of 1.
    if (own == 1) return is_multi_select_mode_ ? PanelMode::Ants : PanelMode::OneAnt;
    if (own > 1) return PanelMode::Ants;
    if (selected_ant_id_ != 0) {                       // an ant of another player under inspection
        for (const auto& a : world.ants) {
            if (a.id == selected_ant_id_) return a.player_id == local_player_id_ ? PanelMode::OneAnt : PanelMode::Other;
        }
    }
    return PanelMode::None;
}

bool HUD::homogeneous_type(const sim::WorldState& world, sim::AntType& type) const {
    bool any = false;
    for (uint32_t id : selected_ant_ids_) {
        for (const auto& a : world.ants) {
            if (a.id != id || a.player_id != local_player_id_) continue;
            if (!any) { type = a.type; any = true; }
            else if (a.type != type) return false;
            break;
        }
    }
    return any;
}

// ------------------------------------------------------------------------------------------------
// The ant under the pointer
// ------------------------------------------------------------------------------------------------

const sim::AntSnapshot* HUD::pick_ant_at(const sim::WorldState& world, int32_t world_x, int32_t world_y) const {
    const int32_t tx = world_x / 32;
    const int32_t ty = world_y / 32;
    const sim::AntSnapshot* hit = nullptr;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (const auto& ant : world.ants) {
                if (ant.tile_x != tx + dx || ant.tile_y != ty + dy) continue;     // the occupant registered on the scanned tile
                const bool combat = (ant.raw_type == sim::AntType::Combat);          // the ant's own type field (cmp word ptr [ecx + 0x54], 4 at 0x1026a3d), not the getter
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

// ------------------------------------------------------------------------------------------------
// The rubber band and the cursor (FUN_0102653f / FUN_01026aa3)
// ------------------------------------------------------------------------------------------------

HUD::BandRect HUD::band_rect() const noexcept {
    // p0 is the press point, p1 the pointer kept 1 px inside the view (16, 21) - (458, 461): x in [17, 457], y in [22, 460]
    const int32_t px = std::clamp(drag_curr_x_, MAP_LEFT + 1, MAP_RIGHT - 1);
    const int32_t py = std::clamp(drag_curr_y_, MAP_TOP + 1, MAP_BOTTOM - 1);
    BandRect b{std::min(drag_start_x_, px), std::min(drag_start_y_, py), std::max(drag_start_x_, px), std::max(drag_start_y_, py)};
    if (b.right == b.left) { --b.left; ++b.right; }           // InflateRect(1, 0) / (0, 1): a stationary press is a 2 x 2 dot
    if (b.bottom == b.top) { --b.top; ++b.bottom; }
    return b;
}

bool HUD::special_target(const sim::SimulationEngine* query, const sim::WorldState& world, sim::TileCoord tile, PanelMode panel) const {
    // ([54fc] == 2 && FUN_01026f91(tile, 0)) || (panel == 3 && FUN_01026f91(tile, 1)); the type is the selection's common type (FUN_010282e0)
    sim::AntType type = sim::AntType::Worker;
    if (query == nullptr || !homogeneous_type(world, type)) return false;
    if (slot_latched_[1] && query->is_special_target_valid(type, tile, false, local_player_id_)) return true;
    return panel == PanelMode::OneAnt && query->is_special_target_valid(type, tile, true, local_player_id_);
}

CursorType HUD::evaluate_cursor(int32_t screen_x, int32_t screen_y, const sim::WorldState& world, const sim::Grid& grid,
                                const ViewportCamera& camera) const {
    // 1. A dialog is open: the cursor code does not run (FUN_0102653f hands the pointer to the dialog), and every dialog window set cursor mode 1, the normal arrow, when it
    // was attached (the thunk 0x101279a = FUN_01027e65(1, 0) in slot +0x20 of all 15 window vtables, called by AddChild 0x102f977), and nothing that runs while it is open
    // writes another mode: the pointer over a dialog is always the arrow, whatever it was before (a scroll arrow, the attack crosshair); the next input run after the
    // dialog closed decides again
    if (is_modal_open()) {
        current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // 2. The eight edge strips (mode 6): the scroll arrows (edge_scroll.hpp); not while a button is captured
    if (!is_input_captured()) {
        const EdgeScroll strip = edge_scroll_step(screen_x, screen_y, 0, camera.world_x, camera.world_y,
                                                  static_cast<int32_t>(grid.width()), static_cast<int32_t>(grid.height()));
        if (strip.dir >= 0) {
            static const CursorType kArrows[8] = {CursorType::ScrollN, CursorType::ScrollNE, CursorType::ScrollE, CursorType::ScrollSE,
                                                  CursorType::ScrollS, CursorType::ScrollSW, CursorType::ScrollW, CursorType::ScrollNW};
            current_cursor_ = kArrows[strip.dir];
            return current_cursor_;
        }
    }

    // 3. Outside the map rectangle (16, 21) - (458, 461): the plain pointer
    if (!in_map_rect(screen_x, screen_y)) {
        current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // 4. A rubber band of more than 4 px (it only exists while no pedestal is latched): the plain pointer (0x1026d98)
    if (is_dragging_ && !slot_latched_[0] && !slot_latched_[1]) {
        const BandRect b = band_rect();
        if (b.right - b.left > 4 || b.bottom - b.top > 4) {
            current_cursor_ = CursorType::Normal;
            return current_cursor_;
        }
    }

    // 5. By panel mode
    const int32_t world_x = camera.world_x + (screen_x - PLAYFIELD_X);
    const int32_t world_y = camera.world_y + (screen_y - PLAYFIELD_Y);
    const int32_t tx = world_x / 32;
    const int32_t ty = world_y / 32;
    const PanelMode panel = panel_mode(world);
    const bool fogged = world.fog_of_war_enabled && !world.is_tile_revealed(tx, ty);       // FUN_01009825: only the pointer's tile
    const sim::AntSnapshot* ant = fogged ? nullptr : pick_ant_at(world, world_x, world_y);
    const assets::AnthillSpawn* hill = hill_at(world, tx, ty);

    if (panel != PanelMode::OneAnt && panel != PanelMode::Ants) {                            // panels 1, 2, 5
        if (fogged) current_cursor_ = CursorType::Normal;
        else if (ant != nullptr || hill != nullptr) current_cursor_ = CursorType::Select;
        else current_cursor_ = CursorType::Normal;
        return current_cursor_;
    }

    // panels 3 and 4 (own ants selected)
    if (fogged) {
        current_cursor_ = CursorType::Move;
        return current_cursor_;
    }
    if (grid.food_object_at_cell(sim::TileCoord{tx, ty}) >= 0) {                              // the object flag: food, lunchbox included
        current_cursor_ = CursorType::Food;
        return current_cursor_;
    }
    if (hill != nullptr && hill->team_id == local_player_id_) {                               // an own hill beats an ant on the tile
        current_cursor_ = CursorType::Move;
        return current_cursor_;
    }
    if (ant != nullptr) {
        if (ant->player_id == local_player_id_) current_cursor_ = CursorType::Select;
        else if (hill != nullptr) current_cursor_ = CursorType::Move;                         // an enemy ant on a hill tile: move
        else current_cursor_ = CursorType::Attack;                                            // every other player's ant, allies included
        return current_cursor_;
    }
    // no ant: a valid special target shows the target cursor
    if (special_target(sim_query_, world, sim::TileCoord{tx, ty}, panel)) {
        current_cursor_ = CursorType::Target;
        return current_cursor_;
    }
    current_cursor_ = (hill != nullptr) ? CursorType::Select : CursorType::Move;              // another player's hill: select, else move
    return current_cursor_;
}

// ------------------------------------------------------------------------------------------------
// Orders (FUN_010287b5)
// ------------------------------------------------------------------------------------------------

void HUD::flash_pedestal(int slot) noexcept {
    if (slot < 0 || slot > 2) return;
    const uint32_t now = ticks_fn_ ? ticks_fn_() : SDL_GetTicks();
    btnpush_until_ms_[slot] = now + 125;                      // BTNPUSH (FUN_01028ffe): pressed for 125 ms
}

// After an accepted order (0x1027883 / 0x1027a31 / 0x1027a82): a latched pedestal of the order's kind pops up, otherwise the pedestal of the
// kind flashes. Move: slot 1 (kind 1). Special: slot 2 (its kind). Attack: slot 1 when latched; slot 2 when latched and it is the attack
// pedestal (a combat ant), any other latched slot 2 stays and the move pedestal flashes; unlatched: the attack pedestal of a combat ant, else move.
void HUD::order_feedback(bool special, bool attack) {
    // A flash is BTNPUSH: the pedestal shows its pressed clip (butXXX2d), whose first frame carries the click (89); a latched pedestal that pops up
    // is replaced by the up picture and stays silent (LI NEW-3). The click follows the voice of the order (the group order speaks first).
    const auto flash_with_click = [this](int slot) {
        play_sfx(sim::SoundID::NavButtonClick);
        flash_pedestal(slot);
    };
    if (!attack && special) {
        if (slot_latched_[1]) slot_latched_[1] = false; else flash_with_click(1);
        return;
    }
    if (!attack) {
        if (slot_latched_[0]) slot_latched_[0] = false; else flash_with_click(0);
        return;
    }
    const bool slot2_is_attack = slot2_attack_kind_;
    if (slot_latched_[0]) slot_latched_[0] = false;
    else if (slot_latched_[1]) {
        if (slot2_is_attack) slot_latched_[1] = false; else flash_with_click(0);
    } else {
        flash_with_click(slot2_is_attack ? 1 : 0);
    }
}

uint32_t HUD::order_selected(sim::SimulationEngine& sim, sim::TileCoord tile, bool special, bool attack) {
    const auto& world = sim.get_world_state();
    std::vector<uint32_t> raw = selected_ant_ids_;
    if (raw.empty() && selected_ant_id_ != 0) raw.push_back(selected_ant_id_);
    std::vector<uint32_t> targets;
    for (uint32_t aid : raw) {
        for (const auto& a : world.ants) {
            if (a.id == aid && a.player_id == local_player_id_ && a.hp > 0 && !a.is_drowning) {
                targets.push_back(aid);
                break;
            }
        }
    }
    if (targets.empty()) return 0;

    // The kind of the second pedestal decides part of the feedback: a combat ant's ability pedestal is the attack pedestal (kind 3)
    sim::AntType common = sim::AntType::Worker;
    slot2_attack_kind_ = panel_mode(world) == PanelMode::OneAnt && homogeneous_type(world, common) && common == sim::AntType::Combat;     // panel 4 has no slot 2

    // An attack on the ally's ant or hill is not carried out at once: the ant asks first (FUN_0101ffab, called from the attack order FUN_0101fc50):
    // "Doing this will break your team with ...". Yes ends the team and gives the order (FUN_01020076), No drops it. (The original keeps the order of
    // the first ant only; here the whole group's order waits for the answer.)
    const uint8_t ally = local_player_id_ < world.player_alliances.size() ? world.player_alliances[local_player_id_] : uint8_t{255};
    if (attack && ally < sim::MAX_PLAYERS) {
        bool at_ally = false;
        for (const auto& a : world.ants) at_ally = at_ally || (a.player_id == ally && a.tile_x == tile.x && a.tile_y == tile.y);
        for (const auto& hill : world.anthills) {
            at_ally = at_ally || (hill.team_id == ally && tile.x >= static_cast<int32_t>(hill.x) && tile.x <= static_cast<int32_t>(hill.x) + 3 &&
                                  tile.y >= static_cast<int32_t>(hill.y) && tile.y <= static_cast<int32_t>(hill.y) + 3);
        }
        if (at_ally) {
            if (!is_modal_open()) {
                pending_break_ = PendingBreak{PendingBreak::Action::Attack, ally, tile, targets};
                open_alliance_dialog(AllianceDialog::BreakConfirm, ally,
                                     sim::strings::format(sim::strings::kBreakTeamConfirm, alliance_name(ally), alliance_colour_word(ally)));
            }
            return 0;
        }
    }
    return issue_group_order(sim, tile, special, attack, targets);
}

// The group order of the original (FUN_010287b5) as the player's command: the engine keeps only the issuer's own ants, checks the tile and answers with
// the ant that acknowledges
uint32_t HUD::issue_group_order(sim::SimulationEngine& sim, sim::TileCoord tile, bool special, bool attack, const std::vector<uint32_t>& targets) {
    sim::Command cmd;
    cmd.type = attack ? sim::CommandType::GroupAttack : special ? sim::CommandType::GroupSpecial : sim::CommandType::GroupMove;
    cmd.issuer = local_player_id_;
    cmd.tile_x = static_cast<int16_t>(tile.x);
    cmd.tile_y = static_cast<int16_t>(tile.y);
    cmd.ants.assign(targets.begin(), targets.begin() + static_cast<std::ptrdiff_t>(std::min(targets.size(), sim::kMaxCommandAnts)));
    const sim::CommandResult result = submit_command(sim, cmd);
    const uint32_t ack = result.ack_ant;
    // FUN_010287b5 (0x1028929 .. 0x1028a07): nothing happens when every ant already carries out this click; otherwise the closest ant's GoTo decides the voice (a refusal is
    // silent, the others' answers do not count), a special order speaks only when exactly one ant needed it (none for more, not even the go voice), and the function returns 1
    // whatever the GoTos answered, which is what the pedestal feedback follows
    if (result.needing_order == 0) return 0;
    if (ack != 0) {
        const sim::AntType voice_type = sim.ant_type(sim.get_unit(ack));          // the voices ask the getter (0x101b680, 0x101b715, 0x101b78e)
        if (attack) voice_attack(voice_type);
        else if (special) voice_special(voice_type, result.needing_order);
        else voice_go(voice_type);
    }
    order_feedback(special, attack);
    return ack;
}

void HUD::dispatch_move_order(int32_t target_tile_x, int32_t target_tile_y, sim::SimulationEngine& sim) {
    order_selected(sim, sim::TileCoord{target_tile_x, target_tile_y}, false, false);
}

void HUD::dispatch_attack_order(uint32_t target_enemy_id, sim::SimulationEngine& sim) {
    const auto& world = sim.get_world_state();
    for (const auto& a : world.ants) {
        if (a.id == target_enemy_id) {
            order_selected(sim, sim::TileCoord{a.tile_x, a.tile_y}, false, true);
            return;
        }
    }
}

void HUD::stop_selected(sim::SimulationEngine& sim) {
    sim::Command cmd;                                                            // FUN_01028a60 for the selected ants
    cmd.type = sim::CommandType::Stop;
    cmd.issuer = local_player_id_;
    std::vector<uint32_t> ids = selected_ant_ids_;
    if (ids.empty() && selected_ant_id_ != 0) ids.push_back(selected_ant_id_);
    for (uint32_t aid : ids) {
        if (cmd.ants.size() >= sim::kMaxCommandAnts) break;
        if (sim.get_unit(aid).player_id == local_player_id_) cmd.ants.push_back(aid);
    }
    if (!cmd.ants.empty()) submit_command(sim, cmd);
    post_status_id(sim::strings::kStopping);                                     // always posted (0x1028b43)
}

// ------------------------------------------------------------------------------------------------
// Clicks
// ------------------------------------------------------------------------------------------------

// FUN_010277f4: the click at the release point, by the cursor mode found there
void HUD::pointer_click(sim::SimulationEngine& sim, ViewportCamera& camera, int32_t x, int32_t y, bool shift) {
    const auto& world = sim.get_world_state();
    const CursorType mode = evaluate_cursor(x, y, world, sim.grid(), camera);
    const int32_t world_x = camera.world_x + (x - PLAYFIELD_X);
    const int32_t world_y = camera.world_y + (y - PLAYFIELD_Y);
    const sim::TileCoord tile{world_x / 32, world_y / 32};
    switch (mode) {
        case CursorType::Normal:                               // mode 1: deselect all, no marker
            clear_selection();
            unlatch_pedestals();
            return;
        case CursorType::Select: {                             // mode 2
            const sim::AntSnapshot* ant = pick_ant_at(world, world_x, world_y);
            if (ant != nullptr) {
                if (ant->player_id == local_player_id_) {
                    const PanelMode panel = panel_mode(world);
                    if (shift && (panel == PanelMode::OneAnt || panel == PanelMode::Ants)) {
                        // shift toggles an own ant in a selection of own ants: it leaves it (FUN_01027aae) or joins it (0x1027940), and the panel is rebuilt with its
                        // text: several ants give string 12, one remaining ant the text of its type, none clears the line (the last argument of FUN_01027f07 is 0)
                        // leaving recounts (several left: panel 4, one left: panel 3, from panel 3 nothing is left: panel 1); joining always sets panel 4
                        auto it = std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), ant->id);
                        if (it != selected_ant_ids_.end()) {
                            selected_ant_ids_.erase(it);
                            is_multi_select_mode_ = selected_ant_ids_.size() > 1;
                        } else {
                            selected_ant_ids_.push_back(ant->id);
                            is_multi_select_mode_ = true;
                        }
                        selected_ant_id_ = selected_ant_ids_.empty() ? 0 : selected_ant_ids_.front();
                        selection_status_pending_ = true;
                    } else {
                        select_ant(ant->id, false);
                        voice_ready(ant->type);
                    }
                } else {
                    select_ant(ant->id, false);                // another player's ant: the inspect panel
                }
                unlatch_pedestals();
                return;
            }
            if (const assets::AnthillSpawn* hill = hill_at(world, tile.x, tile.y)) {
                select_base(static_cast<int32_t>(hill->team_id));
                unlatch_pedestals();
                return;
            }
            clear_selection();
            unlatch_pedestals();
            return;
        }
        case CursorType::Move:                                 // modes 3 and 7: the group order, special 0
        case CursorType::Food:
            spawn_click_marker(world_x, world_y);
            order_selected(sim, tile, false, false);
            return;
        case CursorType::Target:                               // mode 4: special 1
            spawn_click_marker(world_x, world_y);
            order_selected(sim, tile, true, false);
            return;
        case CursorType::Attack: {                             // mode 5: the order targets the tile of the ant under the pointer (FUN_01026904)
            spawn_click_marker(world_x, world_y);
            if (const sim::AntSnapshot* ant = pick_ant_at(world, world_x, world_y)) {
                order_selected(sim, sim::TileCoord{ant->tile_x, ant->tile_y}, false, true);
            }
            return;
        }
        default:                                               // scroll arrows: mouse buttons are ignored
            return;
    }
}

// FUN_01027530: the release of the left button. The band that was displayed (the button was held on the map and no pedestal is latched) is the
// rectangle; without a band a release with the pointer on the map is a 1 x 1 rectangle at the pointer, anywhere else nothing happens. A rectangle
// of at most 4 px in both directions is a click at the release point, a bigger one selects the local team's ants whose hit box overlaps it.
void HUD::pointer_release(sim::SimulationEngine& sim, ViewportCamera& camera, int32_t x, int32_t y, bool shift) {
    const bool latched = slot_latched_[0] || slot_latched_[1];
    BandRect rect{};
    if (is_dragging_ && !latched) rect = band_rect();
    else if (in_map_rect(x, y)) rect = BandRect{x, y, x + 1, y + 1};
    else return;
    if (rect.right - rect.left <= 4 && rect.bottom - rect.top <= 4) {
        pointer_click(sim, camera, x, y, shift);
        return;
    }
    const int32_t ox = camera.world_x - PLAYFIELD_X;
    const int32_t oy = camera.world_y - PLAYFIELD_Y;
    select_ants_in_rect(rect.left + ox, rect.top + oy, rect.right + ox, rect.bottom + oy, sim.get_world_state(), shift);
}

// FUN_01027b51: the right button executes at its release with the tile of the press point
void HUD::pointer_right_click(sim::SimulationEngine& sim, ViewportCamera& camera, int capture, int32_t press_x, int32_t press_y,
                              int32_t x, int32_t y) {
    const auto& world = sim.get_world_state();
    const PanelMode panel = panel_mode(world);
    if (capture == 2) {                                        // pressed on the minimap: own ants only; the press point is a point of the map
        if (panel != PanelMode::OneAnt && panel != PanelMode::Ants) return;
        int32_t wx = 0;
        int32_t wy = 0;
        minimap_point(press_x, press_y, static_cast<int32_t>(sim.grid().width()), static_cast<int32_t>(sim.grid().height()), wx, wy);
        const sim::TileCoord tile{wx / 32, wy / 32};
        const bool special = panel == PanelMode::OneAnt && special_target(&sim, world, tile, panel);
        spawn_click_marker(wx, wy);
        order_selected(sim, tile, special, false);
        return;
    }
    if (capture != 1) return;
    const CursorType mode = evaluate_cursor(x, y, world, sim.grid(), camera);
    if (mode == CursorType::Attack) {                           // mode 5: the ant under the pointer at the release (FUN_01026904)
        spawn_click_marker(camera.world_x + (press_x - PLAYFIELD_X), camera.world_y + (press_y - PLAYFIELD_Y));
        if (const sim::AntSnapshot* ant = pick_ant_at(world, camera.world_x + (x - PLAYFIELD_X), camera.world_y + (y - PLAYFIELD_Y))) {
            order_selected(sim, sim::TileCoord{ant->tile_x, ant->tile_y}, false, true);
        }
        return;
    }
    if (mode != CursorType::Move && mode != CursorType::Food && mode != CursorType::Target) return;    // modes 1, 2 and 6 do nothing
    const int32_t world_x = camera.world_x + (press_x - PLAYFIELD_X);
    const int32_t world_y = camera.world_y + (press_y - PLAYFIELD_Y);
    const sim::TileCoord tile{world_x / 32, world_y / 32};
    spawn_click_marker(world_x, world_y);                       // the marker is always spawned
    // modes 3, 4 and 7: a move for several ants and for a worker / combat ant (or a mixed group), otherwise the ability of the ant's type
    sim::AntType type = sim::AntType::Worker;
    const bool homogeneous = homogeneous_type(world, type);
    const bool move = panel == PanelMode::Ants || !homogeneous || type == sim::AntType::Worker || type == sim::AntType::Combat;
    order_selected(sim, tile, !move, false);
}

// ------------------------------------------------------------------------------------------------
// Pedestals (FUN_010274be -> FUN_01028d30 -> FUN_01028ee0), fired on the press outside the map rectangle
// ------------------------------------------------------------------------------------------------

bool HUD::pedestal_press(sim::SimulationEngine& sim, int32_t x, int32_t y) {
    if (in_map_rect(x, y)) return false;
    const auto& world = sim.get_world_state();
    const PanelMode panel = panel_mode(world);

    if (panel == PanelMode::Base) {
        const uint32_t eggs = local_player_id_ < world.player_eggs.size() ? world.player_eggs[local_player_id_] : 0;
        if (selected_base_team_id_ == local_player_id_) {
            if (in_slot(0, x, y) && eggs > 0) {                                  // hatch: exists only while eggs remain
                hatch_button_.is_pressed = true;
                play_sfx(sim::SoundID::NavButtonClick);
                flash_pedestal(0);
                sim::Command hatch;                                              // FUN_01010aca answers a refusal with its text
                hatch.type = sim::CommandType::Hatch;
                hatch.issuer = local_player_id_;
                submit_command(sim, hatch);
                return true;
            }
            if (in_slot(2, x, y)) {                                              // Stop on a hill: flash, lock 250 ms, deselect
                stop_button_.is_pressed = true;
                play_sfx(sim::SoundID::AntStop);
                flash_pedestal(2);
                input_lock_ticks_ = 5;
                pending_deselect_ = true;
                return true;
            }
            return false;
        }
        const bool allied = local_player_id_ < world.player_alliances.size() &&
                            world.player_alliances[local_player_id_] == selected_base_team_id_;
        if (in_slot(0, x, y) && ally_pedestal_possible(world) && !allied) {       // the ally pedestal: more than two live players, not allied
            team_up_button_.is_pressed = true;
            play_sfx(sim::SoundID::NavButtonClick);
            flash_pedestal(0);
            request_team_up(sim, static_cast<uint8_t>(selected_base_team_id_));      // FUN_0100c7ac
            return true;
        }
        return false;
    }

    if (panel != PanelMode::OneAnt && panel != PanelMode::Ants) return false;

    if (in_slot(0, x, y)) {                                                      // Move: acts only when raised; latches and raises slot 2
        if (!slot_latched_[0]) {
            move_pedestal_button_.is_pressed = true;
            play_sfx(sim::SoundID::NavButtonClick);
            slot_latched_[0] = true;
            slot_latched_[1] = false;
        }
        return true;
    }
    sim::AntType type = sim::AntType::Worker;
    // the ability pedestal exists in panel 3 only: SetPanelMode gives slot 2 the kind of the ant's type there (table 0x1004ef0) and kind 9 (hidden) in panel 4
    const bool has_ability = panel == PanelMode::OneAnt && homogeneous_type(world, type) && type != sim::AntType::Worker;
    if (in_slot(1, x, y) && has_ability) {                                       // ability: acts only when raised
        if (!slot_latched_[1]) {
            ability_pedestal_button_.is_pressed = true;
            play_sfx(sim::SoundID::NavButtonClick);
            slot_latched_[1] = true;
            slot_latched_[0] = false;
        }
        return true;
    }
    if (in_slot(2, x, y)) {                                                      // Stop: both pedestals up, the stop order, a 250 ms lock, then deselect
        stop_button_.is_pressed = true;
        unlatch_pedestals();
        play_sfx(sim::SoundID::AntStop);
        flash_pedestal(2);
        stop_selected(sim);
        input_lock_ticks_ = 5;
        pending_deselect_ = true;
        return true;
    }
    return false;
}

}  // namespace ants::app

#include "ants_ai/gate_goal.hpp"

#include <algorithm>

namespace ants::ai {

namespace {

constexpr int32_t kExtinguishBase = 100;
constexpr int32_t kExtinguishPerCell = 10;
constexpr int32_t kPowerUpHopBase = 200;
constexpr int32_t kPileHopBase = 400;
constexpr int32_t kHopPerBomb = 40;
constexpr int32_t kDefuseBase = 500;
constexpr int32_t kDefusePerBomb = 20;

bool kind_wanted(const GoalRules& rules, sim::AntType kind) noexcept { return ((rules.kinds >> static_cast<unsigned>(kind)) & 1u) != 0; }

void offer(GateGoal& best, const GateGoal& candidate) {
    if (!best.active() || candidate.cost < best.cost) best = candidate;
}

// Whether the bot has an ant of the kind on its own side of the gates, or a power-up of that kind lies there (a tile of the hill's component): a goal that needs a kind with no source would only wait out its time
// (a typed ant that takes a power-up leaves its old one behind: on the far side of a wall it is no source)
bool on_comp_side(const GateInfo& gates, sim::TileCoord tile, int32_t comp) {  // the tile, or a tile next to it (a power-up is solid in a world made by hand), is in `comp`
    if (comp < 0) return false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (gates.component(sim::TileCoord{tile.x + dx, tile.y + dy}) == comp) return true;
        }
    }
    return false;
}

bool ant_in_comp(const GateInfo& gates, const BotView& view, const AntView& ant, int32_t comp) {
    if (comp < 0) return false;
    if (gates.component(ant.tile) == comp) return true;
    if (comp != gates.home()) return false;
    const MapInfo* map = view.map();
    if (map == nullptr) return true;
    const int32_t where = map->component(view.seat(), ant.tile);
    const int32_t side = map->hill_component(view.seat());
    return where < 0 || side < 0 || where == side;
}

bool has_source_in_comp(const GateInfo& gates, const BotView& view, sim::AntType kind, int32_t comp) {
    if (comp < 0) return false;
    if (!view.mine().empty()) {
        bool any_ant = false;
        for (const AntView& a : view.mine()) {
            if (ant_in_comp(gates, view, a, comp)) {
                any_ant = true;
                break;
            }
        }
        if (!any_ant) return false;
    }
    for (const AntView& a : view.mine()) {
        if (a.type == kind && ant_in_comp(gates, view, a, comp)) return true;
    }
    for (const PowerUpView& p : view.powerups()) {
        if (p.kind == kind && on_comp_side(gates, p.tile, comp)) return true;
    }
    return false;
}

std::vector<int32_t> active_bomber_comps(const GateInfo& gates, const BotView& view) {
    std::vector<int32_t> out;
    if (gates.home() >= 0 && has_source_in_comp(gates, view, sim::AntType::Bomber, gates.home())) {
        out.push_back(gates.home());
    }
    for (const AntView& a : view.mine()) {
        const int32_t c = gates.component(a.tile);
        if (c < 0 || std::find(out.begin(), out.end(), c) != out.end()) continue;
        if (has_source_in_comp(gates, view, sim::AntType::Bomber, c)) out.push_back(c);
    }
    return out;
}

}  // namespace

GateGoal plan_gate_goal(const GateInfo& gates, const BotView& view, const GoalRules& rules) {
    GateGoal best;
    if (gates.empty()) return best;
    const std::vector<GateTarget>& piles = gates.piles();
    bool has_swimmer = false;
    if (rules.shore_breach) {
        for (const AntView& a : view.mine()) {
            if (a.type == sim::AntType::Swimmer) has_swimmer = true;
        }
    }
    bool any_pile = false;
    bool any_fire_barrier = false;
    bool any_shore_pile = false;
    bool any_shore_fire = false;
    bool any_shore_bomb = false;
    for (const PileView& p : view.piles()) {
        if (p.lunchbox || p.index >= piles.size()) continue;
        any_pile = true;
        const GateTarget& t = piles[p.index];
        if (t.free()) return GateGoal{};                                            // food the hill reaches: normal play
        if (t.fire_cells > 0 || t.shore_fire_cells > 0) any_fire_barrier = true;
        if (rules.shore_breach && t.gate == GateKind::Sealed && (t.shore_fire_cells >= 0 || t.shore_bombs >= 0)) {
            if (has_swimmer && t.shore_fire_cells == 0 && t.shore_bombs == 0) return GateGoal{};
            any_shore_pile = true;
            if (t.shore_fire_cells > 0) any_shore_fire = true;
            if (t.shore_bombs > 0) any_shore_bomb = true;
        }
    }
    if (!any_pile) return best;
    const bool fire_source = has_source_in_comp(gates, view, sim::AntType::Fire, gates.home());
    const bool bomber_home_source = has_source_in_comp(gates, view, sim::AntType::Bomber, gates.home());
    const std::vector<int32_t> bomber_comps = active_bomber_comps(gates, view);
    for (const PileView& p : view.piles()) {
        if (p.lunchbox || p.index >= piles.size()) continue;
        const GateTarget& t = piles[p.index];
        if (fire_source && t.fire_cells > 0 && t.fire_cell.x >= 0 && (t.gate == GateKind::Fire || t.gate == GateKind::Either)) {
            GateGoal g;
            g.kind = GoalKind::Extinguish;
            g.need = sim::AntType::Fire;
            g.target = t.index;
            g.cell = t.fire_cell;
            g.gate = t.gate;
            g.cost = kExtinguishBase + kExtinguishPerCell * t.fire_cells;
            offer(best, g);
        }
        if (bomber_home_source && t.defuse_bombs > 0 && t.bomb_cell.x >= 0 && (t.gate == GateKind::Bombs || t.gate == GateKind::Either)) {
            GateGoal g;
            g.kind = GoalKind::Defuse;
            g.need = sim::AntType::Bomber;
            g.target = t.index;
            g.cell = t.bomb_cell;
            g.gate = t.gate;
            g.cost = kDefuseBase + kDefusePerBomb * t.defuse_bombs;
            offer(best, g);
        }
        if (rules.shore_breach && t.gate == GateKind::Sealed) {
            if (fire_source && t.shore_fire_cells > 0 && t.shore_fire_cell.x >= 0) {
                GateGoal g;
                g.kind = GoalKind::Extinguish;
                g.need = sim::AntType::Fire;
                g.target = t.index;
                g.cell = t.shore_fire_cell;
                g.gate = t.gate;
                g.cost = kExtinguishBase + kExtinguishPerCell * t.shore_fire_cells;
                offer(best, g);
            }
            if (bomber_home_source && t.shore_fire_cells == 0 && t.shore_bombs > 0 && t.shore_bomb_cell.x >= 0) {
                GateGoal g;
                g.kind = GoalKind::Defuse;
                g.need = sim::AntType::Bomber;
                g.target = t.index;
                g.cell = t.shore_bomb_cell;
                g.gate = t.gate;
                g.cost = kExtinguishBase + kDefusePerBomb * t.shore_bombs;
                offer(best, g);
            }
        }
        if (bomber_home_source && t.hop >= 0) {
            const BombHop& h = gates.hops()[static_cast<size_t>(t.hop)];
            if (h.bombs <= static_cast<int32_t>(rules.hop_bombs)) {
                GateGoal g;
                g.kind = GoalKind::Hop;
                g.need = sim::AntType::Bomber;
                g.target = t.index;
                g.cell = h.bomb;
                g.hop = h;
                g.gate = t.gate;
                g.cost = kPileHopBase + kHopPerBomb * h.bombs;
                offer(best, g);
            }
        } else if (!bomber_comps.empty() && view.has_grid()) {
            for (const int32_t src_comp : bomber_comps) {
                if (src_comp == gates.home()) continue;
                for (const int32_t dst_comp : t.comps) {
                    const BombHop h = GateInfo::best_hop_between(gates, view.grid(), src_comp, dst_comp);
                    if (h.bombs > 0 && h.bombs <= static_cast<int32_t>(rules.hop_bombs)) {
                        GateGoal g;
                        g.kind = GoalKind::Hop;
                        g.need = sim::AntType::Bomber;
                        g.target = t.index;
                        g.cell = h.bomb;
                        g.hop = h;
                        g.gate = t.gate;
                        g.cost = kPileHopBase + kHopPerBomb * h.bombs;
                        offer(best, g);
                    }
                }
            }
        }
    }
    if (view.map() == nullptr) return best;
    const std::vector<PowerUpInfo>& start = view.map()->powerups();
    for (size_t i = 0; i < gates.powerups().size() && i < start.size(); ++i) {
        const GateTarget& t = gates.powerups()[i];
        if (!bomber_home_source || t.free() || t.hop < 0) continue;
        const PowerUpView* now = view.powerup_at(start[i].tile);
        if (now == nullptr || !kind_wanted(rules, now->kind) || now->standing_team != 255) continue;       // gone, a kind the bot does not want, or somebody stands on it
        if (rules.shore_breach) {
            if (now->kind == sim::AntType::Fire && (!any_fire_barrier || fire_source)) continue;
            if (any_shore_pile && now->kind == sim::AntType::Swimmer && (any_shore_fire || any_shore_bomb)) continue;
        }
        const BombHop* chosen = &gates.hops()[static_cast<size_t>(t.hop)];
        for (const BombHop* exact : gates.hops_onto(start[i].tile)) {              // a hop that lands ON the power-up beats one that lands in reach of it
            if (exact->bombs < chosen->bombs || chosen->land() != start[i].tile) chosen = exact;
        }
        if (chosen->bombs > static_cast<int32_t>(rules.hop_bombs)) continue;
        int32_t bonus = 0;
        if (rules.shore_breach && any_shore_pile) {
            bool on_pile_island = false;
            for (const GateTarget& pt : piles) {
                if (std::find(pt.comps.begin(), pt.comps.end(), chosen->to_comp) != pt.comps.end()) on_pile_island = true;
            }
            if (any_shore_fire && !fire_source && now->kind == sim::AntType::Fire && view.has_grid()) {
                if (GateInfo::best_map_bomb_bounce(gates, view.grid(), start[i].tile, gates.home()).bombs > 0) {
                    bonus = on_pile_island ? -35 : -25;
                }
            } else if (now->kind == sim::AntType::Swimmer && on_pile_island) {
                bonus = -10;
            }
        }
        GateGoal g;
        g.kind = GoalKind::Hop;
        g.need = sim::AntType::Bomber;
        g.powerup = true;
        g.target = static_cast<uint32_t>(i);
        g.cell = chosen->bomb;
        g.hop = *chosen;
        g.gate = t.gate;
        g.cost = kPowerUpHopBase + kHopPerBomb * chosen->bombs + (now->kind == sim::AntType::Swimmer ? 0 : 10) + bonus;
        offer(best, g);
    }
    return best;
}

}  // namespace ants::ai

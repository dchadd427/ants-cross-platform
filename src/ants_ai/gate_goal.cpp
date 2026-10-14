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
bool on_home_side(const GateInfo& gates, sim::TileCoord tile) {            // the tile, or a tile next to it (a power-up is solid in a world made by hand), is in the hill's component
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (gates.component(sim::TileCoord{tile.x + dx, tile.y + dy}) == gates.home()) return true;
        }
    }
    return false;
}

bool ant_on_home_side(const BotView& view, const AntView& ant) {                // the walkers' components of the map analysis; a tile of no component (the mound) is the hill's own
    const MapInfo* map = view.map();
    if (map == nullptr) return true;
    const int32_t where = map->component(view.seat(), ant.tile);
    const int32_t side = map->hill_component(view.seat());
    return where < 0 || side < 0 || where == side;
}

bool has_source(const GateInfo& gates, const BotView& view, sim::AntType kind) {
    for (const AntView& a : view.mine()) {
        if (a.type == kind && ant_on_home_side(view, a)) return true;
    }
    for (const PowerUpView& p : view.powerups()) {
        if (p.kind == kind && gates.home() >= 0 && on_home_side(gates, p.tile)) return true;
    }
    return false;
}

}  // namespace

GateGoal plan_gate_goal(const GateInfo& gates, const BotView& view, const GoalRules& rules) {
    GateGoal best;
    if (gates.empty()) return best;
    const std::vector<GateTarget>& piles = gates.piles();
    bool any_pile = false;
    for (const PileView& p : view.piles()) {
        if (p.lunchbox || p.index >= piles.size()) continue;
        any_pile = true;
        if (piles[p.index].free()) return GateGoal{};                               // food the hill reaches: normal play
    }
    if (!any_pile) return best;
    const bool fire_source = has_source(gates, view, sim::AntType::Fire);
    const bool bomber_source = has_source(gates, view, sim::AntType::Bomber);
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
        if (bomber_source && t.defuse_bombs > 0 && t.bomb_cell.x >= 0 && (t.gate == GateKind::Bombs || t.gate == GateKind::Either)) {
            GateGoal g;
            g.kind = GoalKind::Defuse;
            g.need = sim::AntType::Bomber;
            g.target = t.index;
            g.cell = t.bomb_cell;
            g.gate = t.gate;
            g.cost = kDefuseBase + kDefusePerBomb * t.defuse_bombs;
            offer(best, g);
        }
        if (bomber_source && t.hop >= 0) {
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
        }
    }
    if (view.map() == nullptr) return best;
    const std::vector<PowerUpInfo>& start = view.map()->powerups();
    for (size_t i = 0; i < gates.powerups().size() && i < start.size(); ++i) {
        const GateTarget& t = gates.powerups()[i];
        if (!bomber_source || t.free() || t.hop < 0) continue;
        const PowerUpView* now = view.powerup_at(start[i].tile);
        if (now == nullptr || !kind_wanted(rules, now->kind) || now->standing_team != 255) continue;       // gone, a kind the bot does not want, or somebody stands on it
        const BombHop* chosen = &gates.hops()[static_cast<size_t>(t.hop)];
        for (const BombHop* exact : gates.hops_onto(start[i].tile)) {              // a hop that lands ON the power-up beats one that lands in reach of it
            if (exact->bombs < chosen->bombs || chosen->land() != start[i].tile) chosen = exact;
        }
        if (chosen->bombs > static_cast<int32_t>(rules.hop_bombs)) continue;
        GateGoal g;
        g.kind = GoalKind::Hop;
        g.need = sim::AntType::Bomber;
        g.powerup = true;
        g.target = static_cast<uint32_t>(i);
        g.cell = chosen->bomb;
        g.hop = *chosen;
        g.gate = t.gate;
        g.cost = kPowerUpHopBase + kHopPerBomb * chosen->bombs + (now->kind == sim::AntType::Swimmer ? 0 : 10);
        offer(best, g);
    }
    return best;
}

}  // namespace ants::ai

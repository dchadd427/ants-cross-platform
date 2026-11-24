#include "ants_ai/plug_task.hpp"

#include <algorithm>
#include <queue>
#include <set>

#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::tile_index;

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// The walkable parts of the map: label 0, 1, ... in reading order of the first tile of each (8 neighbours, as the walks of the ants and every cost field of the bots); -1 for a tile that is not walkable
std::vector<int32_t> label_parts(const sim::Grid& grid, const std::vector<uint8_t>& mask) {
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    std::vector<int32_t> label(mask.size(), -1);
    int32_t next = 0;
    std::vector<int> stack;
    for (int start = 0; start < w * h; ++start) {
        if (mask[static_cast<size_t>(start)] == 0 || label[static_cast<size_t>(start)] >= 0) continue;
        label[static_cast<size_t>(start)] = next;
        stack.push_back(start);
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            const int cx = cur % w;
            const int cy = cur / w;
            for (int k = 0; k < 8; ++k) {
                const int nx = cx + kDx[k];
                const int ny = cy + kDy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t n = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);
                if (mask[n] == 0 || label[n] >= 0) continue;
                label[n] = next;
                stack.push_back(static_cast<int>(n));
            }
        }
        ++next;
    }
    return label;
}

// The labels of the walkable parts that touch `tile` (the tiles round it)
std::set<int32_t> parts_at(const sim::Grid& grid, const std::vector<int32_t>& label, sim::TileCoord tile, int radius = 1) {
    std::set<int32_t> out;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const sim::TileCoord n{tile.x + dx, tile.y + dy};
            if (!grid.in_bounds(n)) continue;
            const int32_t l = label[tile_index(grid, n)];
            if (l >= 0) out.insert(l);
        }
    }
    return out;
}

// A worker that may be sent: empty hands, healthy, takes orders, stands on no power-up. Only an ant of the level's default type: a typed ant would lose its type
bool usable(const BotView& view, const AntView& a) {
    if (a.type != view.default_ant_type() || a.holding || a.carried_points > 0 || a.hp < 5) return false;
    if (!a.takes_orders() || a.state == sim::UnitState::Stunned) return false;
    return view.powerup_at(a.tile) == nullptr;
}

}  // namespace

bool PlugTask::black(uint32_t ant, sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = black_.find({ant, key_of(tile)});
    return it != black_.end() && it->second > tick;
}

bool PlugTask::tile_black(sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = tile_black_.find(key_of(tile));
    return it != tile_black_.end() && it->second > tick;
}

void PlugTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove || command.ants.size() != 1) return;
    const auto it = takes_.find(command.ants[0]);
    if (it == takes_.end() || it->second.sent != kPending || command.tile_x != it->second.tile.x || command.tile_y != it->second.tile.y) return;
    if (fate == Bot::Fate::Sent) it->second.sent = tick;                  // the clock of the walk starts when the order LEFT
    else takes_.erase(it);                                                // it never left: the ant was told nothing (its claim is given back at the next look)
}

// The takes under way: one that is done (the power-up is gone, or the ant) or given up gives its ant back
void PlugTask::follow(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    for (auto it = takes_.begin(); it != takes_.end();) {
        const AntView* ant = find_ant(v.mine(), it->first);
        const Take& t = it->second;
        const bool gone = v.powerup_at(t.tile) == nullptr;
        const bool mine = c.ledger.owner(it->first) == id();
        bool done = ant == nullptr || gone || !mine;
        bool failed = false;
        if (!done && ant != nullptr) {
            const PowerUpView* p = v.powerup_at(t.tile);
            if (p != nullptr && p->standing_ant != 0 && p->standing_ant != it->first) failed = true;               // somebody stands on it: nobody else can take it
            if (t.sent != kPending && ant->state == sim::UnitState::CantGo && now >= t.sent + 8u) failed = true;     // the engine refused the walk
        }
        if (!done && t.sent != kPending && now >= t.sent + t.walk + params_.slack_ticks) failed = true;
        if (!done && t.sent == kPending && now >= t.decided + params_.slack_ticks) failed = true;       // (the order never left and never said so)
        if (done || failed) {
            if (gone && ant != nullptr) ++opened_;
            if (failed) {
                black_[{it->first, key_of(t.tile)}] = now + params_.black_ticks;
                tile_black_[key_of(t.tile)] = now + params_.black_ticks / 3u;
                ++given_up_;
            }
            c.ledger.release(it->first, id());
            it = takes_.erase(it);
        } else {
            ++it;
        }
    }
}

void PlugTask::step(TaskContext& c) {
    const uint64_t now = c.view.tick();
    follow(c);
    if (takes_.empty() && now >= params_.first_look && (last_look_ == 0 || now >= last_look_ + params_.look_ticks)) {
        last_look_ = now;
        look(c);
    }
    tactics_.plug_tiles.clear();
    for (const auto& t : takes_) tactics_.plug_tiles.push_back(t.second.tile);
}

void PlugTask::look(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (v.powerups().empty()) return;
    const sim::Grid& grid = v.grid();
    const HillInfo& hill = c.map.hill(c.seat);
    if (!hill.present) return;
    const std::vector<uint8_t>& mask = v.walkable_mask(c.seat);
    const std::vector<int32_t> label = label_parts(grid, mask);
    int32_t home = -1;                                                                                   // the part of the hill: its queue row
    for (int32_t dx = 0; dx <= 2 && home < 0; ++dx) {
        const sim::TileCoord q{hill.origin.x + dx, hill.origin.y - 1};
        if (grid.in_bounds(q)) home = label[tile_index(grid, q)];
    }
    if (home < 0) return;
    // what lies in each part: the own ants (all of them, whatever they carry) and the piles with food
    std::map<int32_t, int> ants_in;
    for (const AntView& a : v.mine()) {
        if (!grid.in_bounds(a.tile)) continue;
        const int32_t l = label[tile_index(grid, a.tile)];
        if (l >= 0) ++ants_in[l];
    }
    std::map<int32_t, int> piles_in;
    for (const PileView& p : v.piles()) {
        for (const int32_t l : parts_at(grid, label, p.anchor, 2)) ++piles_in[l];
    }
    // the plugs: a power-up that touches the part of the hill and another part with something of the seat's in it
    struct Plug {
        const PowerUpView* p;
        int32_t beyond;
        int worth;
    };
    std::vector<Plug> plugs;
    for (const PowerUpView& p : v.powerups()) {
        if (p.standing_ant != 0 || tile_black(p.tile, now) || std::find(tactics_.trip_tiles.begin(), tactics_.trip_tiles.end(), p.tile) != tactics_.trip_tiles.end()) continue;
        const std::set<int32_t> parts = parts_at(grid, label, p.tile);
        if (parts.size() < 2 || parts.count(home) == 0) continue;
        for (const int32_t beyond : parts) {
            if (beyond == home) continue;
            const int worth = 2 * ants_in[beyond] + (piles_in[beyond] > 0 ? 3 : 0);
            if (worth > 0) plugs.push_back(Plug{&p, beyond, worth});
        }
    }
    if (plugs.empty()) return;
    ++found_;
    std::stable_sort(plugs.begin(), plugs.end(), [](const Plug& x, const Plug& y) { return x.worth > y.worth; });
    for (const Plug& plug : plugs) {
        const std::vector<int32_t> field = MapInfo::cost_field_onto(grid, mask, c.seat, plug.p->tile, v.walk_context());
        if (field.empty()) continue;
        const AntView* best = nullptr;
        int32_t best_cost = 0;
        bool best_far = false;
        for (const AntView& a : v.mine()) {
            if (!usable(v, a) || !grid.in_bounds(a.tile) || black(a.id, plug.p->tile, now)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;       // the ants of a fight, the walls and the raids are not taken
            const int32_t l = label[tile_index(grid, a.tile)];
            if (l != plug.beyond && l != home) continue;
            const int32_t cost = field[tile_index(grid, a.tile)];
            if (cost < 0) continue;
            const bool beyond = l == plug.beyond;
            const uint32_t walk = static_cast<uint32_t>(MapInfo::walking_ticks(cost)) + 5u;
            if (walk > (beyond ? params_.stranded_trip_ticks : params_.home_trip_ticks)) continue;
            if (best != nullptr && (best_far != beyond ? best_far : cost >= best_cost)) continue;                  // the beyond part's ants first, then the nearest
            best = &a;
            best_cost = cost;
            best_far = beyond;
        }
        if (best == nullptr) continue;
        if (static_cast<uint64_t>(v.ticks_left()) < static_cast<uint64_t>(MapInfo::walking_ticks(best_cost)) + 17u + 2u * c.profile.reaction_delay + 100u) continue;
        if (!c.ledger.take(best->id, id())) continue;
        c.orders.pick_up(best->id, plug.p->tile, Priority::Normal);
        Take t;
        t.tile = plug.p->tile;
        t.decided = now;
        t.walk = static_cast<uint32_t>(MapInfo::walking_ticks(best_cost)) + 17u;
        takes_[best->id] = t;
        ++sent_;
        return;
    }
}

}  // namespace ants::ai

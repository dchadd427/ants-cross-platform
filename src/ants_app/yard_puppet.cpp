// The Yard's world on the client: see yard_puppet.hpp.
#include "ants_app/yard_puppet.hpp"

#include <algorithm>
#include <unordered_set>

#include "ants_sim/movement_tables.hpp"

namespace ants::app {

namespace {

/// Is the clip one of the idle clips of the tables (every type, direction and carry state, and the swimmer's on water)? The renderer shows an idle clip over and over between two ticks of a
/// world and holds the last frame of every other, which the unit's state tells it (Idle or not).
bool is_idle_clip(uint16_t chd_index) {
    static const std::vector<uint16_t> idle = [] {
        std::vector<uint16_t> found;
        for (uint8_t type = 0; type < sim::movement::kAntTypeCount; ++type) {
            for (uint8_t dir = 0; dir < sim::movement::kDirectionCount; ++dir) {
                for (const bool carrying : {false, true}) {
                    const sim::movement::MotionClip clip = sim::movement::idle_clip(type, dir, carrying);
                    if (clip.valid()) found.push_back(clip.chd_index);
                }
            }
        }
        const sim::movement::MotionClip water = sim::movement::idle_water_clip();
        if (water.valid()) found.push_back(water.chd_index);
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }();
    return std::binary_search(idle.begin(), idle.end(), chd_index);
}

}  // namespace

YardPuppet::YardPuppet(sim::SimulationEngine& engine, size_t slots) : engine_(engine) {
    slots_.reserve(slots);
    // The units stand on land, on one tile each from the top left (an ant that is put on water drowns; a world that is never ticked needs no better place for a unit that is removed)
    const sim::Grid& grid = engine_.grid();
    const int32_t width = static_cast<int32_t>(grid.width());
    const int32_t height = static_cast<int32_t>(grid.height());
    sim::TileCoord at{0, 0};
    for (int32_t y = 0; y < height && slots_.size() < slots; ++y) {
        for (int32_t x = 0; x < width && slots_.size() < slots; ++x) {
            const sim::TileCell& cell = grid.get_cell(sim::TileCoord{x, y});
            if (cell.terrain_type == sim::TERRAIN_WATER && !cell.has_any_bridge()) continue;
            at = sim::TileCoord{x, y};
            Slot slot;
            slot.unit = engine_.spawn_unit(0, sim::AntType::Worker, at);
            slots_.push_back(slot);
        }
    }
    while (slots_.size() < slots) {                                   // (a map with fewer land tiles than slots, or none: the rest stand on the last one)
        Slot slot;
        slot.unit = engine_.spawn_unit(0, sim::AntType::Worker, at);
        slots_.push_back(slot);
    }
    for (size_t i = 0; i < slots_.size(); ++i) {
        engine_.get_unit(slots_[i].unit).removed = true;             // free until a batch uses it (get_world_state leaves a removed ant out)
        by_unit_.emplace(slots_[i].unit, i);
    }
}

size_t YardPuppet::apply(const std::vector<yard::AntRecord>& records, const std::vector<uint32_t>& held) {
    left_out_ = 0;
    if (slots_.empty()) return 0;
    const sim::Grid& grid = engine_.grid();
    const int32_t max_x = std::max(0, static_cast<int32_t>(grid.width()) * 32 - 1);
    const int32_t max_y = std::max(0, static_cast<int32_t>(grid.height()) * 32 - 1);

    // 1. The records that are shown: the visitor's own first (a batch of more ants than the pool holds loses others', never these), each id once, each with a clip of the tables and a color
    std::vector<const yard::AntRecord*> shown;
    shown.reserve(std::min(records.size(), slots_.size()));
    std::unordered_set<uint16_t> seen;
    for (int pass = 0; pass < 2; ++pass) {
        for (const yard::AntRecord& r : records) {
            if ((r.color == yard::kColorMine) != (pass == 0)) continue;
            if (r.id == 0 || r.color >= yard::kColorCount || !sim::movement::clip_by_chd(r.clip, r.mirrored).valid() || !seen.insert(r.id).second || shown.size() >= slots_.size()) {
                ++left_out_;
                continue;
            }
            shown.push_back(&r);
        }
    }

    // 2. An ant that is shown keeps its slot
    std::vector<size_t> place(shown.size(), slots_.size());            // (slots_.size(): none yet)
    std::vector<bool> taken(slots_.size(), false);
    for (size_t i = 0; i < shown.size(); ++i) {
        const auto it = by_server_.find(shown[i]->id);
        if (it == by_server_.end()) continue;
        place[i] = it->second;
        taken[it->second] = true;
    }

    // 3. A new one takes a free slot: not one that an ant that is shown has, and not one that the caller still points at. The search goes round the pool, so that a slot that was freed is the
    // last to be used again.
    const std::unordered_set<uint32_t> pointed_at(held.begin(), held.end());
    std::vector<size_t> free_slots;
    for (size_t n = 0; n < slots_.size(); ++n) {
        const size_t j = (cursor_ + n) % slots_.size();
        if (!taken[j] && pointed_at.count(slots_[j].unit) == 0) free_slots.push_back(j);
    }
    size_t next_free = 0;
    for (size_t i = 0; i < shown.size(); ++i) {
        if (place[i] != slots_.size()) continue;
        if (next_free >= free_slots.size()) {
            ++left_out_;
            continue;
        }
        place[i] = free_slots[next_free++];
        taken[place[i]] = true;
        cursor_ = (place[i] + 1) % slots_.size();
    }

    // 4. The slots of the ants that are gone are free (and removed)
    for (size_t j = 0; j < slots_.size(); ++j) {
        if (taken[j] || slots_[j].server == 0) continue;
        by_server_.erase(slots_[j].server);
        slots_[j].server = 0;
        engine_.get_unit(slots_[j].unit).removed = true;
    }

    // 5. The ants that are shown are written into their units
    size_t count = 0;
    for (size_t i = 0; i < shown.size(); ++i) {
        if (place[i] == slots_.size()) continue;
        const yard::AntRecord& r = *shown[i];
        Slot& slot = slots_[place[i]];
        const sim::movement::MotionClip clip = sim::movement::clip_by_chd(r.clip, r.mirrored);
        const int32_t px = std::clamp<int32_t>(r.px, 0, max_x);
        const int32_t py = std::clamp<int32_t>(r.py, 0, max_y);
        slot.server = r.id;
        slot.color = r.color;
        slot.px = px;
        slot.py = py;
        by_server_[r.id] = place[i];
        sim::AntUnit& u = engine_.get_unit(slot.unit);
        u.removed = false;
        u.player_id = r.color;
        u.team = static_cast<sim::TeamId>(r.color % 4u);
        u.type = sim::AntType::Worker;
        u.hp = u.max_hp = sim::AntUnit::MAX_HP;                       // (the client does not know the health: every ant is shown whole)
        u.state = is_idle_clip(r.clip) ? sim::UnitState::Idle : sim::UnitState::Walking;
        u.set_pixel_pos(px, py);
        u.loco.clip = clip;
        u.loco.cursor = static_cast<uint16_t>(std::min<uint32_t>(r.frame, static_cast<uint32_t>(clip.count) - 1u) + 1u);
        u.loco.next_ms = r.left_ms;                                  // (the engine's animation clock stands at 0 for ever: "ms left" is "next_ms")
        u.loco_action = sim::AntUnit::kActionWalk;                    // (any value but "none": get_world_state() then reads the clip)
        u.frozen = false;
        u.burn_end_ms = 0;
        u.in_water = u.was_in_water = false;
        ++count;
    }
    engine_.set_fog_of_war_enabled(false);                            // (the public call that marks the cached world state stale; the fog is off already, so nothing else happens)
    return count;
}

size_t YardPuppet::set_hills(const std::vector<yard::Hill>& hills) {
    sim::Grid& grid = engine_.grid_mut();
    std::vector<assets::AnthillSpawn>& list = grid.anthills_mut();
    list.clear();
    const int32_t width = static_cast<int32_t>(grid.width());
    const int32_t height = static_cast<int32_t>(grid.height());
    uint32_t used = 0;                                                // (bit n: the hill with the id n is in the list)
    for (const yard::Hill& hill : hills) {
        if (list.size() >= yard::kMaxHills) break;
        if (hill.id == 0 || static_cast<size_t>(hill.id) > yard::kMaxHills || ((used >> hill.id) & 1u) != 0) continue;
        if (static_cast<int32_t>(hill.x) + yard::kHillTiles > width || static_cast<int32_t>(hill.y) + yard::kHillTiles > height) continue;
        used |= 1u << hill.id;
        assets::AnthillSpawn spawn;
        spawn.tile_id = 0;
        spawn.x = hill.x;
        spawn.y = hill.y;
        spawn.team_id = hill.id;                                      // (1 .. 8: no place is the game's own hill, team 0; the renderer cycles the four colors)
        list.push_back(spawn);
    }
    engine_.set_fog_of_war_enabled(false);
    return list.size();
}

uint32_t YardPuppet::server_id(uint32_t slot) const noexcept {
    const auto it = by_unit_.find(slot);
    return it == by_unit_.end() ? 0u : static_cast<uint32_t>(slots_[it->second].server);
}

uint32_t YardPuppet::slot_id(uint32_t server) const noexcept {
    if (server == 0 || server > 0xFFFFu) return 0u;
    const auto it = by_server_.find(static_cast<uint16_t>(server));
    return it == by_server_.end() ? 0u : slots_[it->second].unit;
}

bool YardPuppet::is_own(uint32_t slot) const noexcept {
    const auto it = by_unit_.find(slot);
    return it != by_unit_.end() && slots_[it->second].server != 0 && slots_[it->second].color == yard::kColorMine;
}

bool YardPuppet::on_map(int32_t tile_x, int32_t tile_y) const noexcept {
    return engine_.grid().in_bounds(sim::TileCoord{tile_x, tile_y});
}

uint32_t YardPuppet::foreign_ant_at(int32_t tile_x, int32_t tile_y) const noexcept {
    uint32_t found = 0;
    for (const Slot& slot : slots_) {
        if (slot.server != 0 && slot.color != yard::kColorMine && slot.px / 32 == tile_x && slot.py / 32 == tile_y) found = slot.server;
    }
    return found;
}

size_t YardPuppet::own_shown() const noexcept {
    size_t count = 0;
    for (const Slot& slot : slots_) count += slot.server != 0 && slot.color == yard::kColorMine ? 1u : 0u;
    return count;
}

bool YardPuppet::own_center(int32_t& world_x, int32_t& world_y) const noexcept {
    int64_t sum_x = 0;
    int64_t sum_y = 0;
    int64_t count = 0;
    for (const Slot& slot : slots_) {
        if (slot.server == 0 || slot.color != yard::kColorMine) continue;
        sum_x += slot.px;
        sum_y += slot.py;
        ++count;
    }
    if (count == 0) return false;
    world_x = static_cast<int32_t>(sum_x / count);
    world_y = static_cast<int32_t>(sum_y / count);
    return true;
}

}  // namespace ants::app

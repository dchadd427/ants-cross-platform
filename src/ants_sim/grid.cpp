#include "ants_sim/grid.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace ants::sim {

static_assert(TILE_PIXELS == 32, "Tile pixels must be 32");

uint8_t Grid::determine_terrain_type(uint16_t tile_index, uint16_t flags) const noexcept {
    if (tile_index == 2 || (flags & 0x0100u) != 0) {
        return TERRAIN_WATER;
    }
    if (tile_index == 1) {
        return TERRAIN_OBSTACLE;
    }
    return TERRAIN_WALKABLE;
}

bool Grid::init_from_level(const ants::assets::LevelData& level) {
    width_ = level.width;
    height_ = level.height;
    if (width_ == 0 || height_ == 0) return false;

    cells_.resize(static_cast<size_t>(width_ * height_));

    // Populate Layer 1
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const auto& c1 = level.get_cell_layer1(x, y);
            auto& cell = get_cell_mut(x, y);
            cell.terrain_id = c1.tile_index;
            cell.flags = c1.flags;
            cell.terrain_type = determine_terrain_type(c1.tile_index, c1.flags);
            cell.occupant_ant_id = -1;
            cell.lunchbox_points = 0;
            cell.is_mud = false;
            cell.surface_type = SurfaceType::Grass;

            const std::string& l1_name = level.get_tile_name(c1.tile_index);
            if (!l1_name.empty() && l1_name != ".") {
                char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(l1_name[0])));
                if (ch == 'w') {
                    cell.terrain_type = TERRAIN_WATER;
                    cell.surface_type = SurfaceType::Water;
                } else if (ch == 's') {
                    cell.terrain_type = TERRAIN_WALKABLE;
                    cell.surface_type = SurfaceType::Slate;
                } else if (ch == 'd') {
                    cell.terrain_type = TERRAIN_WALKABLE;
                    cell.surface_type = SurfaceType::Gravel;
                } else if (ch == 'm') {
                    cell.terrain_type = TERRAIN_WALKABLE;
                    cell.surface_type = SurfaceType::Mud;
                    cell.is_mud = true;
                } else {
                    cell.terrain_type = TERRAIN_WALKABLE;
                    cell.surface_type = SurfaceType::Grass;
                }
            }

            if (cell.terrain_type == TERRAIN_WALKABLE) {
                if (!cell.is_mud && cell.surface_type != SurfaceType::Mud) {
                    cell.flags |= FLAG_CAN_PLACE_BOMB;
                }
                cell.flags |= FLAG_CAN_PLACE_FIRE;
            }
        }
    }

    // Populate Layer 2
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            auto& cell = get_cell_mut(x, y);
            cell.interactive_id = c2.tile_index;
            cell.interactive_owner = 255;
            cell.timer_ticks = 0;
            cell.is_food = false;
            cell.is_powerup = false;
            cell.powerup_type = 0;
            cell.is_obstacle_overlay = false;

            if (c2.tile_index != TILE_EMPTY && c2.tile_index != 0xFFFF && c2.tile_index != 0x7FFE) {
                const std::string& tname = level.get_tile_name(c2.tile_index);
                if (!tname.empty() && tname != ".") {
                    std::string lower_name = tname;
                    for (char& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (lower_name.rfind("fd", 0) == 0 || lower_name.rfind("food", 0) == 0) {
                        cell.is_food = true;
                    } else if (lower_name.rfind("pu_", 0) == 0) {
                        cell.is_powerup = true;
                        if (lower_name == "pu_comb") cell.powerup_type = 4; // Combat
                        else if (lower_name == "pu_thief") cell.powerup_type = 3; // Thief
                        else if (lower_name == "pu_bomb") cell.powerup_type = 1; // Bomber
                        else if (lower_name == "pu_swim") cell.powerup_type = 5; // Swimmer
                        else if (lower_name == "pu_mason" || lower_name == "pu_fire") cell.powerup_type = 2; // Fire
                    } else if (lower_name.find("hill") != std::string::npos) {
                        if ((c2.flags & 1) != 0) {
                            cell.terrain_type = TERRAIN_WALKABLE;
                            cell.surface_type = SurfaceType::Gravel;
                            cell.is_obstacle_overlay = false;
                        } else {
                            cell.is_obstacle_overlay = true;
                        }
                    } else if (lower_name.find("start") != std::string::npos) {
                        cell.terrain_type = TERRAIN_WALKABLE;
                        cell.is_obstacle_overlay = false;
                    } else if (lower_name.find("bridge") != std::string::npos ||
                               (c2.tile_index >= TILE_BRIDGE1 && c2.tile_index <= TILE_BRIDGE4) ||
                               c2.tile_index == TILE_BRIDGE4B) {
                        cell.is_obstacle_overlay = false;
                    } else if (lower_name.rfind("broken", 0) == 0) {
                        // Flat floor dishware/debris (broken1, broken2, broken3) is walkable
                        cell.is_obstacle_overlay = false;
                    } else {
                        // Solid obstacle overlay: rocks, cans, pencils, grass clusters (grass1..4, grassbig*, grassmed*), toys, flowers, etc.
                        cell.is_obstacle_overlay = true;
                    }
                } else {
                    cell.is_obstacle_overlay = true;
                }
            }
        }
    }

    // Mark flower plants from Block 1 (team_id == 255) as solid obstacles at their root stem
    for (const auto& sp : level.anthill_spawns) {
        if (sp.team_id == 255 && sp.tile_id < level.tile_dictionary.size()) {
            const std::string& tname = level.tile_dictionary[sp.tile_id];
            std::string lower_name = tname;
            for (char& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (lower_name.find("flower") != std::string::npos) {
                if (in_bounds(static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y))) {
                    get_cell_mut(static_cast<uint32_t>(sp.x), static_cast<uint32_t>(sp.y)).is_obstacle_overlay = true;
                }
            }
        }
    }

    // Identify true 4x4 anthill base origins from Layer 2
    std::array<TileCoord, 4> hill_origins{{{ -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 }}};
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const auto& c2 = level.get_cell_layer2(x, y);
            if (c2.tile_index < level.tile_dictionary.size()) {
                const std::string& tname = level.tile_dictionary[c2.tile_index];
                int team = -1;
                if (tname == "GREENHILL" || tname == "greenhill") team = 0;
                else if (tname == "REDHILL" || tname == "redhill") team = 1;
                else if (tname == "BLUEHILL" || tname == "bluehill") team = 2;
                else if (tname == "BLACKHILL" || tname == "blackhill") team = 3;
                if (team >= 0) {
                    size_t st = static_cast<size_t>(team);
                    if (hill_origins[st].x < 0 || static_cast<int32_t>(x) < hill_origins[st].x) {
                        hill_origins[st].x = static_cast<int32_t>(x);
                    }
                    if (hill_origins[st].y < 0 || static_cast<int32_t>(y) < hill_origins[st].y) {
                        hill_origins[st].y = static_cast<int32_t>(y);
                    }
                }
            }
        }
    }

    anthills_.clear();
    for (uint8_t t = 0; t < 4; ++t) {
        if (hill_origins[t].x >= 0 && hill_origins[t].y >= 0) {
            ants::assets::AnthillSpawn sp{};
            sp.team_id = t;
            sp.x = static_cast<uint16_t>(hill_origins[t].x);
            sp.y = static_cast<uint16_t>(hill_origins[t].y);
            anthills_.push_back(sp);
            configure_anthill_cells(TileCoord{static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)});
        }
    }
    if (anthills_.empty()) {
        anthills_ = level.anthill_spawns;
        for (const auto& sp : anthills_) {
            configure_anthill_cells(TileCoord{static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)});
        }
    }

    food_schedules_.clear();
    for (const auto& fs : level.food_schedules) {
        ActiveFoodSchedule afs{};
        afs.x = fs.x;
        afs.y = fs.y;
        afs.respawn_interval_ticks = static_cast<uint32_t>(fs.respawn_interval) * 20u; // 20 Hz
        afs.countdown_ticks = static_cast<uint32_t>(fs.initial_delay) * 20u;
        afs.variants = fs.variants;
        afs.active = true;
        if (!afs.variants.empty()) {
            afs.remaining_bites = static_cast<int32_t>(afs.variants[0].weight);
            afs.current_tile_id = afs.variants[0].tile_id;
        }
        // Populate footprint of all connected cells matching this food item's layer2 tile
        uint16_t anchor_tile = afs.current_tile_id;
        if (anchor_tile != ants::assets::LVL_EMPTY_TILE && anchor_tile != 32766) {
            for (int32_t dy = -4; dy <= 4; ++dy) {
                for (int32_t dx = -4; dx <= 4; ++dx) {
                    int32_t fx = static_cast<int32_t>(fs.x) + dx;
                    int32_t fy = static_cast<int32_t>(fs.y) + dy;
                    if (in_bounds(fx, fy)) {
                        const auto& c2 = level.get_cell_layer2(static_cast<uint32_t>(fx), static_cast<uint32_t>(fy));
                        if (c2.tile_index == anchor_tile) {
                            afs.footprint.push_back(TileCoord{fx, fy});
                        }
                    }
                }
            }
        }
        if (afs.footprint.empty()) {
            afs.footprint.push_back(TileCoord{static_cast<int32_t>(fs.x), static_cast<int32_t>(fs.y)});
        }
        food_schedules_.push_back(afs);
    }

    return true;
}

void Grid::configure_anthill_cells(TileCoord pos) {
    // Ensure 4x4 mound cells are obstacles EXCEPT entrance mouth (bx + 1, by), entrance hole (bx + 1, by + 1),
    // and right flank approach tiles (bx + 3, by + 0..3) where thieves enter
    for (int dy = 0; dy < 4; ++dy) {
        for (int dx = 0; dx < 4; ++dx) {
            int32_t mx = static_cast<int32_t>(pos.x) + dx;
            int32_t my = static_cast<int32_t>(pos.y) + dy;
            if (in_bounds(mx, my)) {
                auto& mcell = get_cell_mut(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
                if (dx == 1 && (dy == 0 || dy == 1)) {
                    mcell.terrain_type = TERRAIN_WALKABLE;
                    mcell.is_obstacle_overlay = false;
                    mcell.flags &= ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);
                } else if (dx == 3) {
                    // Right flank 4 tiles (dx == 3, dy = 0..3): walkable for thief infiltration
                    mcell.terrain_type = TERRAIN_WALKABLE;
                    mcell.is_obstacle_overlay = false;
                    mcell.flags &= ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);
                } else {
                    mcell.is_obstacle_overlay = true;
                    if (dx == 2 && (dy >= 0 && dy <= 2)) {
                        mcell.flags |= FLAG_CAN_PLACE_FIRE;
                    }
                }
            }
        }
    }
    // Ensure queuing and entry staging cells along dx = -1 are passable (dy = -1..3)
    for (int dy = -1; dy <= 3; ++dy) {
        int32_t qx = static_cast<int32_t>(pos.x) - 1;
        int32_t qy = static_cast<int32_t>(pos.y) + dy;
        if (in_bounds(qx, qy)) {
            auto& qcell = get_cell_mut(static_cast<uint32_t>(qx), static_cast<uint32_t>(qy));
            qcell.terrain_type = TERRAIN_WALKABLE;
            qcell.is_obstacle_overlay = false;
        }
    }
    // Ensure top approach corridor (bx + dx, by - 1) for dx = 0..2 is passable,
    // but strictly blocked from placing fire or bombs on
    for (int dx = 0; dx <= 2; ++dx) {
        int32_t rx = static_cast<int32_t>(pos.x) + dx;
        int32_t ry = static_cast<int32_t>(pos.y) - 1;
        if (in_bounds(rx, ry)) {
            auto& rcell = get_cell_mut(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
            rcell.terrain_type = TERRAIN_WALKABLE;
            rcell.is_obstacle_overlay = false;
            rcell.flags &= ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);
        }
    }
    // Ensure right approach corridor (bx + 4, by + dy for dy = 0..3) is passable,
    // allowing firewalls (up to 3) and bombs to trap thieves or defend the base
    for (int dy = 0; dy < 4; ++dy) {
        int32_t cx = static_cast<int32_t>(pos.x) + 4;
        int32_t cy = static_cast<int32_t>(pos.y) + dy;
        if (in_bounds(cx, cy)) {
            auto& ccell = get_cell_mut(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
            ccell.terrain_type = TERRAIN_WALKABLE;
            ccell.is_obstacle_overlay = false;
            ccell.flags |= (FLAG_CAN_PLACE_FIRE | FLAG_CAN_PLACE_BOMB);
        }
    }
    // Ensure idle spot (pos.x + 3, pos.y + 3) is passable
    for (int delta : {3, 4}) {
        int32_t ix = static_cast<int32_t>(pos.x) + delta;
        int32_t iy = static_cast<int32_t>(pos.y) + delta;
        if (in_bounds(ix, iy)) {
            auto& icell = get_cell_mut(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy));
            icell.terrain_type = TERRAIN_WALKABLE;
            icell.is_obstacle_overlay = false;
        }
    }
}

bool Grid::is_anthill_reserved_spot(TileCoord pos) const noexcept {
    for (const auto& ah : anthills_) {
        int32_t bx = static_cast<int32_t>(ah.x);
        int32_t by = static_cast<int32_t>(ah.y);
        // 1. The authentic 3 blocked mound tiles (FUN_0101d8a4 in Ants.exe)
        if (pos.x == bx - 2 && (pos.y >= by - 1 && pos.y <= by + 1)) return true;
        // 2. Base origin and entrance mouth/hole (FUN_0101d858 in Ants.exe)
        if (pos.x == bx && pos.y == by) return true;
        if (pos.x == bx + 1 && (pos.y >= by && pos.y <= by + 2)) return true;
        // 3. Top approach corridor (row by - 1 across bx..bx+2)
        if (pos.y == by - 1 && pos.x >= bx && pos.x <= bx + 2) return true;
    }
    return false;
}

void Grid::set_anthill(uint8_t team_id, TileCoord pos) {
    for (auto& a : anthills_) {
        if (a.team_id == team_id) {
            a.x = static_cast<uint16_t>(pos.x);
            a.y = static_cast<uint16_t>(pos.y);
            configure_anthill_cells(pos);
            return;
        }
    }
    ants::assets::AnthillSpawn s{};
    s.team_id = team_id;
    s.x = static_cast<uint16_t>(pos.x);
    s.y = static_cast<uint16_t>(pos.y);
    anthills_.push_back(s);
    configure_anthill_cells(pos);
}

void Grid::place_firewall(uint32_t x, uint32_t y, uint8_t owner_player) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    for (const auto& ah : anthills_) {
        int32_t bx = static_cast<int32_t>(ah.x);
        int32_t by = static_cast<int32_t>(ah.y);
        if (static_cast<int32_t>(y) == by - 1 && static_cast<int32_t>(x) >= bx && static_cast<int32_t>(x) <= bx + 2) return;
        if (static_cast<int32_t>(x) == bx + 1 && (static_cast<int32_t>(y) == by || static_cast<int32_t>(y) == by + 1)) return;
    }
    auto& cell = get_cell_mut(x, y);
    cell.interactive_id = TILE_FIREWALL;
    cell.interactive_owner = owner_player;
    cell.timer_ticks = LIFETIME_180S_TICKS;
}

void Grid::clear_firewall(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.interactive_id == TILE_FIREWALL) {
        cell.interactive_id = TILE_EMPTY;
        cell.timer_ticks = 0;
    }
}

void Grid::place_bomb(uint32_t x, uint32_t y, uint8_t team_id) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    for (const auto& ah : anthills_) {
        int32_t bx = static_cast<int32_t>(ah.x);
        int32_t by = static_cast<int32_t>(ah.y);
        if (static_cast<int32_t>(y) == by - 1 && static_cast<int32_t>(x) >= bx && static_cast<int32_t>(x) <= bx + 2) return;
        if (static_cast<int32_t>(x) == bx + 1 && (static_cast<int32_t>(y) == by || static_cast<int32_t>(y) == by + 1)) return;
    }
    auto& cell = get_cell_mut(x, y);
    cell.interactive_id = static_cast<uint16_t>(BOMB_BLACK + (team_id % 4u));
    cell.interactive_owner = team_id;
    cell.timer_ticks = 0;
}

void Grid::clear_bomb(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.has_bomb()) {
        cell.interactive_id = TILE_EMPTY;
        cell.interactive_owner = 255;
    }
}

void Grid::advance_bridge(uint32_t x, uint32_t y, uint8_t owner_player) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.interactive_id < TILE_BRIDGE1 || cell.interactive_id > TILE_BRIDGE4) {
        cell.interactive_id = TILE_BRIDGE1;
    } else if (cell.interactive_id < TILE_BRIDGE4) {
        cell.interactive_id++;
    }
    cell.interactive_owner = owner_player;
    if (cell.interactive_id == TILE_BRIDGE4) {
        cell.timer_ticks = LIFETIME_180S_TICKS;
    }
}

void Grid::regress_bridge(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.interactive_id == TILE_BRIDGE4 || cell.interactive_id == TILE_BRIDGE4B) {
        cell.interactive_id = TILE_BRIDGE3;
        cell.timer_ticks = 0;
    } else if (cell.interactive_id == TILE_BRIDGE3) {
        cell.interactive_id = TILE_BRIDGE2;
    } else if (cell.interactive_id == TILE_BRIDGE2) {
        cell.interactive_id = TILE_BRIDGE1;
    } else if (cell.interactive_id == TILE_BRIDGE1) {
        cell.interactive_id = TILE_EMPTY;
        cell.timer_ticks = 0;
        cell.interactive_owner = 255;
    }
}

void Grid::collapse_bridge(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    cell.interactive_id = TILE_EMPTY;
    cell.timer_ticks = 0;
}

void Grid::drop_lunchbox(uint32_t x, uint32_t y, uint32_t points) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    cell.interactive_id = TILE_LUNCHBOX;
    cell.lunchbox_points = points;
}

void Grid::clear_lunchbox(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.has_lunchbox()) {
        cell.interactive_id = TILE_EMPTY;
        cell.lunchbox_points = 0;
    }
}

void Grid::clear_powerup(int32_t x, int32_t y) noexcept {
    if (!in_bounds(x, y)) return;
    auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    cell.is_powerup = false;
    cell.powerup_type = 0;
    cell.interactive_id = TILE_EMPTY;
}

void Grid::place_powerup(int32_t x, int32_t y, uint8_t powerup_type) noexcept {
    if (!in_bounds(x, y)) return;
    auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    cell.is_powerup = true;
    cell.powerup_type = powerup_type;
    cell.interactive_id = 0x8000u | static_cast<uint16_t>(powerup_type);
}

} // namespace ants::sim

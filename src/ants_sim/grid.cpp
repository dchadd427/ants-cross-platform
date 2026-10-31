#include "ants_sim/grid.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace ants::sim {

static_assert(TILE_PIXELS == 32, "Tile pixels must be 32");

bool Grid::init_from_level(const ants::assets::LevelData& level) {
    width_ = level.width;
    height_ = level.height;
    if (width_ == 0 || height_ == 0) return false;
    rules_ = rules_word_played(level.rules);   // (a word of 2 plays as 1 unless the test hook was set: grid.hpp)

    // A level load starts from empty cells: nothing of a previously loaded map may survive (an engine that plays a second map must equal a
    // fresh engine, which the lock-step state hash relies on)
    cells_.assign(static_cast<size_t>(width_ * height_), TileCell{});
    exact_solid_bits_ = false;

    // Which templates the original has loaded (FUN_0100674e mode 0: the keep flag of an id is dictionary entry != ".", an entry past the dictionary is not kept; FUN_010068be: the 62 forced ids)
    graphic_kept_.assign(movement::kTileIdCount, 0);
    for (size_t i = 0; i < level.tile_dictionary.size() && i < graphic_kept_.size(); ++i) graphic_kept_[i] = level.tile_dictionary[i] != "." ? 1 : 0;
    for (const uint16_t id : assets::LVL_ALWAYS_LOADED_TILES) graphic_kept_[id] = 1;      // (the 62 forced ids, lvl_parser.hpp)

    // LVL block 3 (FUN_01007025, 0x1007025): the level's default ant type. The word is a tile index and the index is the tile id (the dictionary remap is the
    // identity, 0x10067a5 - 0x10067c8); it is kept only when that tile has the power-up flag (0x1007068), so a level has a default type exactly when block 3 names
    // one of the ids 62 .. 66. What the dictionary calls the entry is not looked at. An index outside the dictionary reads the original's remap table out of
    // bounds (a tile of heap garbage): the remake takes it for "no default".
    default_ant_tile_ = TILE_EMPTY;
    if (level.ambient_tile_or_sound < level.tile_dictionary.size()) set_default_ant_tile(level.ambient_tile_or_sound);

    // The plants of Block 1 (0x100e3b0 - 0x100e436): each record whose tile id carries property bit 0x10 or 0x20 becomes a world object at its cell
    plants_.clear();
    for (const auto& sp : level.anthill_spawns) {
        if (movement::is_plant_object_tile(sp.tile_id)) plants_.push_back(MapPlant{sp.tile_id, sp.x, sp.y});
    }

    // Populate Layer 1
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const auto& c1 = level.get_cell_layer1(x, y);
            auto& cell = get_cell_mut(x, y);
            cell.terrain_id = c1.tile_index;
            cell.flags = c1.flags;
            cell.terrain_type = TERRAIN_WALKABLE;
            // Original engine: the layer-1 solid bit is stored verbatim from the file's cell flags by the
            // level reader (Ants.exe FUN_010069d8: word0 = (tile << 1) | (flags & 1)).
            cell.static_solid = (c1.flags & 1u) != 0;
            cell.occupant_ant_id = -1;
            cell.is_mud = false;
            cell.surface_type = SurfaceType::Grass;

            // Terrain class of the layer-1 tile from the original tile-info table (Ants.exe 0x1001360 pairs, FUN_0100724c, FUN_01008af7; LVL tile indices are CHD Table-4 indices), by the
            // ID and nothing else: the dictionary's name of the entry ("." in the community maps that leave a lake or a mud path unnamed) and the flag bits of the cell other than the solid
            // bit play no part in the original, whose class table is indexed by the tile id alone.
            switch (movement::terrain_class_of_tile(c1.tile_index)) {
                case movement::kTerrainWater:
                    cell.terrain_type = TERRAIN_WATER;
                    cell.surface_type = SurfaceType::Water;
                    break;
                case movement::kTerrainSand:
                    cell.surface_type = SurfaceType::Slate;
                    break;
                case movement::kTerrainDirt:
                    cell.surface_type = SurfaceType::Gravel;
                    break;
                case movement::kTerrainMud:
                    cell.surface_type = SurfaceType::Mud;
                    cell.is_mud = true;
                    break;
                default:
                    break;
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
            // Layer-2 cell bytes 2 and 3: the anchor row and column of the object the cell belongs to (FUN_010076b6). LevelData::object_anchor reads them for both versions: the
            // absolute bytes of version 8, or the sum of the cell and the relative offset of version 9 (so the same map as a version 9 file gives the same cells).
            // (an empty cell, 0xFFFF and an original bomb tile have none: the cell keeps -1)
            int32_t anchor_row = 0;
            int32_t anchor_column = 0;
            const bool has_anchor = level.object_anchor(y, x, anchor_row, anchor_column);
            cell.anchor_y = has_anchor ? static_cast<int16_t>(anchor_row) : int16_t{-1};
            cell.anchor_x = has_anchor ? static_cast<int16_t>(anchor_column) : int16_t{-1};
            cell.interactive_owner = 255;
            cell.timer_ticks = 0;
            cell.is_food = false;
            cell.is_powerup = false;
            cell.powerup_type = 0;
            cell.is_obstacle_overlay = false;

            if (is_original_bomb_tile(c2.tile_index)) {
                // A bomb is a tile id (FUN_01008bc6, 0x81 .. 0x84), never a dictionary name: community editors leave these entries "." and the original arms them all the same. Its COLOR is the
                // tile's (the planted bomb's id, 100 + the color's team index); its OWNER is the cell's second word (the file's third, `properties`: the team index that credits the hits and whose
                // ants and allies route around it), kept verbatim: 0 .. 4 as they are (4 is also "no ally", as in the original), anything above is an editor leftover that matches nobody
                // (255). The word is not an anchor here: the cell has none, as a planted bomb has none. It is walkable (a bomb is not solid) and set off by the first ant that ends a step on
                // it (docs/reverse_engineering/map_file_bombs.md).
                cell.interactive_id = static_cast<uint16_t>(BOMB_BLACK + original_bomb_team(c2.tile_index));
                cell.interactive_owner = c2.properties <= 4u ? static_cast<uint8_t>(c2.properties) : uint8_t{255};
                cell.anchor_x = -1;
                cell.anchor_y = -1;
            } else if (c2.tile_index != TILE_EMPTY && c2.tile_index != 0xFFFF && c2.tile_index != 0x7FFE && movement::is_powerup_tile(c2.tile_index)) {
                // A power-up is a tile id (FUN_01007202, 0x1007202: the ids 62 .. 66 carry the flag, the five kinds of FUN_01021087), never a dictionary name: community
                // dictionaries often name these entries "." (the editor did not list them) and the original plays and draws them all the same. The cell is a removable
                // object (no obstacle overlay): it is solid to every order but the power-up order, and the arrival of that order takes it.
                cell.is_powerup = true;
                cell.powerup_type = movement::ant_type_of_powerup_tile(c2.tile_index);
            } else if (assets::hill_team_of_id(c2.tile_index) >= 0) {
                // A hill is a tile id (245 .. 248, FUN_0100ecac), never a dictionary name: the table of forced graphics has the four ids, so a hill is drawn and played whatever the entry says
                if ((c2.flags & 1) != 0) {
                    // The terrain class stays that of the layer-1 tile (Ants.exe FUN_01008af7).
                    cell.terrain_type = TERRAIN_WALKABLE;
                    cell.is_obstacle_overlay = false;
                } else {
                    cell.is_obstacle_overlay = true;
                }
            } else if (assets::start_marker_team_of_id(c2.tile_index) >= 0) {
                // (a start marker is a block-1 record; one in layer 2 is walkable ground)
                cell.terrain_type = TERRAIN_WALKABLE;
                cell.is_obstacle_overlay = false;
            } else if (c2.tile_index != TILE_EMPTY && c2.tile_index != 0xFFFF && c2.tile_index != 0x7FFE && (movement::tile_flags_of(c2.tile_index) & movement::kTileFlagFood) != 0) {
                // Food is the tile flag 0x02 of the id (FUN_010071dd: 87 ids), never the dictionary's name ("fd..." or "food..."): an id with the flag is food whatever the entry says ("." included),
                // and an entry named fd... of an id without the flag (fdpez2b1 at 344, fdfrl7 at 287) is an ordinary object. Food is a removable object: no obstacle overlay.
                cell.is_food = true;
            } else if (c2.tile_index != TILE_EMPTY && c2.tile_index != 0xFFFF && c2.tile_index != 0x7FFE) {
                const std::string& tname = level.get_tile_name(c2.tile_index);
                if (!tname.empty() && tname != ".") {
                    std::string lower_name = tname;
                    for (char& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (lower_name.find("bridge") != std::string::npos ||
                               (c2.tile_index >= TILE_BRIDGE1 && c2.tile_index <= TILE_BRIDGE4)) {
                        cell.is_obstacle_overlay = false;
                    } else if (lower_name.rfind("broken", 0) == 0) {
                        // Flat floor dishware/debris (broken1, broken2, broken3) is walkable
                        cell.is_obstacle_overlay = false;
                    } else {
                        // Solid obstacle overlay: rocks, cans, pencils, grass clusters (grass1..4, grassbig*, grassmed*), toys, flowers, etc.
                        cell.is_obstacle_overlay = true;
                    }
                }
                // An entry called "." or left empty is no reason for anything: the name never changes behaviour in the original (only whether the graphic is loaded), and what blocks is the
                // file's solid bit. Such a cell is an object like any other without the remake's name-based obstacle overlay.
            }
        }
    }

    // Mark flower plants from Block 1 as solid obstacles at their root stem. Block 1 holds start markers, plants (tile flag 0x10 or 0x20) and nothing else that the original reads
    // (FUN_0100dbb0: every other record is ignored), so only a plant can be one
    for (const auto& sp : level.anthill_spawns) {
        if (sp.team_id == 255 && movement::is_plant_object_tile(sp.tile_id) && sp.tile_id < level.tile_dictionary.size()) {
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
            const int team = assets::hill_team_of_id(c2.tile_index);                // the hill of a color is a tile id (245 .. 248), not a dictionary name (FUN_0100ecac)
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

    anthills_.clear();
    for (uint8_t t = 0; t < 4; ++t) {
        if (hill_origins[t].x >= 0 && hill_origins[t].y >= 0) {
            ants::assets::AnthillSpawn sp{};
            sp.team_id = t;
            sp.x = static_cast<uint16_t>(hill_origins[t].x);
            sp.y = static_cast<uint16_t>(hill_origins[t].y);
            anthills_.push_back(sp);
            configure_anthill_cells(TileCoord{static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)}, sp.team_id);
        }
    }
    if (anthills_.empty()) {
        // a map without hill cells: the start markers stand in (the remake's own fallback); the other records of block 1 are plants and ignored records, never a hill
        for (const auto& sp : level.anthill_spawns) {
            if (sp.team_id < 4) anthills_.push_back(sp);
        }
        for (const auto& sp : anthills_) {
            configure_anthill_cells(TileCoord{static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)}, sp.team_id);
        }
    }

    // Food objects of LVL Block 2 (Ants.exe 0x1006d19): anchor (row, col), units ("delay"), points per unit ("interval") and
    // the stage list. The first stage without a tile ends the list: its threshold and every later stage become (0, gone).
    // Every object then puts its stage tile on its anchor (SetTile), in file order: an anchor that already shows that tile
    // keeps the cells of the file, a later object at the same anchor replaces the earlier one's cells.
    food_objects_.clear();
    for (const auto& fs : level.food_schedules) {
        FoodObject o;
        o.row = fs.y;
        o.col = fs.x;
        o.units = fs.initial_delay;
        o.value = fs.respawn_interval;
        o.remaining = o.units;
        for (const auto& v : fs.variants) {
            o.thresholds.push_back(v.weight);
            o.stage_tiles.push_back(v.tile_id == ants::assets::LVL_EMPTY_TILE ? uint16_t{0x7FFEu} : v.tile_id);
        }
        bool ended = false;
        for (size_t k = 0; k < o.stage_tiles.size(); ++k) {
            if (ended) {
                o.thresholds[k] = 0;
                o.stage_tiles[k] = 0x7FFEu;
            } else if (o.stage_tiles[k] == 0x7FFEu) {
                o.thresholds[k] = 0;
                ended = true;
            }
        }
        food_objects_.push_back(o);
        set_food_tile(TileCoord{static_cast<int32_t>(o.col), static_cast<int32_t>(o.row)}, o.stage_tile());
    }

    // Original-engine solid bits (layer-1 cell bit0) beyond the per-cell file flag. The file's bit is the ONLY source of an object's solidity, power-ups, food, fire walls and
    // lunchboxes included (FUN_0100cf0f reads the bit; FUN_010069d8 stored it verbatim): a power-up that the file leaves on a cell without the bit is walkable ground, and the run-time
    // placement and removal of an object set and clear the bit as SetTile does (Grid::place_powerup, clear_powerup, place_firewall, set_food_tile, set_layer2 ...).
    //  * every Block-4 entry marks its tile solid (Ants.exe FUN_01006f0e -> FUN_0100660c(row, col, 1));
    //    this is what makes daisy-flower stems block ants, including entries without a plant;
    //  * row 0 is always solid (level loader loop at 0x1006592; FUN_0100660c never clears row 0).
    for (const auto& wp : level.waypoints) {
        if (in_bounds(static_cast<int32_t>(wp.x), static_cast<int32_t>(wp.y))) {
            get_cell_mut(static_cast<uint32_t>(wp.x), static_cast<uint32_t>(wp.y)).static_solid = true;
        }
    }
    for (uint32_t x = 0; x < width_; ++x) {
        get_cell_mut(x, 0).static_solid = true;
    }
    // The anthill set-up clears the entrance and the tile above it (FUN_0100ecdf -> FUN_0100660c(.., 0)).
    for (const auto& ah : anthills_) {
        clear_anthill_entrance_solid(TileCoord{static_cast<int32_t>(ah.x), static_cast<int32_t>(ah.y)});
    }
    exact_solid_bits_ = true;

    return true;
}

void Grid::clear_anthill_entrance_solid(TileCoord base) noexcept {
    // Remake anthill origin (bx, by) is the top-left of the 4x4 mound; the original's hill entrance is
    // (bx + 1, by + 1) and the tile above it (bx + 1, by). Row 0 can never be cleared.
    for (int32_t dy = 0; dy <= 1; ++dy) {
        int32_t ex = base.x + 1;
        int32_t ey = base.y + dy;
        if (ey > 0 && in_bounds(ex, ey)) {
            get_cell_mut(static_cast<uint32_t>(ex), static_cast<uint32_t>(ey)).static_solid = false;
        }
    }
}

void Grid::configure_anthill_cells(TileCoord pos, uint8_t team_id) {
    // Ensure 4x4 mound cells are obstacles EXCEPT entrance mouth (bx + 1, by), entrance hole (bx + 1, by + 1),
    // and bottlecap (bx + 3, by + 2) exclusively occupiable by enemy thief ants.
    // All 13 other mound cells are solid impassable obstacles for all ants.
    for (int dy = 0; dy < 4; ++dy) {
        for (int dx = 0; dx < 4; ++dx) {
            int32_t mx = static_cast<int32_t>(pos.x) + dx;
            int32_t my = static_cast<int32_t>(pos.y) + dy;
            if (in_bounds(mx, my)) {
                auto& mcell = get_cell_mut(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
                mcell.flags = static_cast<uint16_t>(mcell.flags & ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE));
                mcell.base_owner_team = team_id;
                if (dx == 1 && (dy == 0 || dy == 1)) {
                    mcell.is_base_hole = true;
                    mcell.is_thief_only = false;
                    mcell.terrain_type = TERRAIN_OBSTACLE;
                    mcell.is_obstacle_overlay = false;
                    mcell.static_solid = (my == 0); // entrance + tile above are cleared (row 0 stays solid)
                } else if (dx == 3 && dy == 2) {
                    mcell.is_thief_only = true;
                    mcell.is_base_hole = false;
                    mcell.terrain_type = TERRAIN_OBSTACLE;
                    mcell.is_obstacle_overlay = false;
                    mcell.static_solid = true; // hill footprint (the raid rule bypasses it for raiding thieves)
                } else {
                    mcell.is_thief_only = false;
                    mcell.is_base_hole = false;
                    mcell.terrain_type = TERRAIN_OBSTACLE;
                    mcell.is_obstacle_overlay = true;
                    mcell.static_solid = true; // hill footprint
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
    // Ensure top approach corridor (bx + dx, by - 1) for dx = 0..2 is team-locked
    // to the base owner's team and strictly blocked from placing fire or bombs on
    for (int dx = 0; dx <= 2; ++dx) {
        int32_t rx = static_cast<int32_t>(pos.x) + dx;
        int32_t ry = static_cast<int32_t>(pos.y) - 1;
        if (in_bounds(rx, ry)) {
            auto& rcell = get_cell_mut(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
            rcell.terrain_type = TERRAIN_WALKABLE;
            rcell.is_obstacle_overlay = false;
            rcell.base_owner_team = team_id;
            rcell.flags = static_cast<uint16_t>(rcell.flags & ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE));
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
    // Ensure idle spot outside base (pos.x + 4, pos.y + 4) is passable
    int32_t ix = static_cast<int32_t>(pos.x) + 4;
    int32_t iy = static_cast<int32_t>(pos.y) + 4;
    if (in_bounds(ix, iy)) {
        auto& icell = get_cell_mut(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy));
        icell.terrain_type = TERRAIN_WALKABLE;
        icell.is_obstacle_overlay = false;
    }
}

void Grid::set_anthill(uint8_t team_id, TileCoord pos) {
    for (auto& a : anthills_) {
        if (a.team_id == team_id) {
            a.x = static_cast<uint16_t>(pos.x);
            a.y = static_cast<uint16_t>(pos.y);
            configure_anthill_cells(pos, team_id);
            return;
        }
    }
    ants::assets::AnthillSpawn s{};
    s.team_id = team_id;
    s.x = static_cast<uint16_t>(pos.x);
    s.y = static_cast<uint16_t>(pos.y);
    anthills_.push_back(s);
    configure_anthill_cells(pos, team_id);
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
    cell.static_solid = true;                               // SetTile copies the tile flag 0x01 of the fire wall (id 134) to the layer-1 solid bit (FUN_01007a22)
}

void Grid::clear_firewall(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.interactive_id == TILE_FIREWALL) {
        cell.interactive_id = TILE_EMPTY;
        cell.timer_ticks = 0;
        if (y > 0) cell.static_solid = false;               // SetTile(empty) clears the footprint's solid bit (FUN_0100744f -> FUN_0100660c(.., 0); row 0 never)
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
    if (y > 0) cell.static_solid = false;                   // SetTile clears what the cell held before (the placeholder 160 of a plant in progress is solid); a bomb has no solid flag itself
}

void Grid::clear_bomb(uint32_t x, uint32_t y) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.has_bomb()) {
        cell.interactive_id = TILE_EMPTY;
        cell.interactive_owner = 255;
        // SetTile(empty) clears the cell's layer-1 solid bit too (FUN_0100744f -> FUN_0100660c(row, col, 0); row 0 never): a bomb of a map file on a solid cell leaves it open. A planted bomb
        // lies on a cell that was not solid, so nothing changes for it.
        if (y > 0) cell.static_solid = false;
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
    if (cell.interactive_id == TILE_BRIDGE4) {
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

void Grid::set_layer2(uint32_t x, uint32_t y, uint16_t id, uint8_t owner) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    auto& cell = get_cell_mut(x, y);
    if (cell.interactive_id != id && id != TILE_EMPTY && !tile_graphic_loaded(id)) {
        // FUN_01007352 clears the old object, then refuses a tile whose template is not loaded: the cell stays empty
        if (!cell.is_empty_overlay() && y > 0) cell.static_solid = false;
        cell.interactive_id = TILE_EMPTY;
        cell.interactive_owner = 255;
        cell.timer_ticks = 0;
        return;
    }
    if (cell.interactive_id != id) {
        // FUN_01007352: the old object's footprint is cleared first (solid bit off, row 0 never), then the new tile's flag 0x01 sets it
        if (!cell.is_empty_overlay() && y > 0) cell.static_solid = false;
        if (id != TILE_EMPTY && (movement::tile_flags_of(id) & movement::kTileFlagSolid) != 0) cell.static_solid = true;
    }
    cell.interactive_id = id;
    cell.interactive_owner = (id == TILE_EMPTY) ? uint8_t{255} : owner;
    cell.timer_ticks = 0;
}

int32_t Grid::food_object_first_at(TileCoord anchor) const noexcept {
    for (size_t i = 0; i < food_objects_.size(); ++i) {
        if (food_objects_[i].row == anchor.y && food_objects_[i].col == anchor.x) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t Grid::food_object_at_cell(TileCoord cell) const noexcept {
    if (!in_bounds(cell)) return -1;
    const TileCell& c = get_cell(cell);
    if (c.interactive_id == TILE_EMPTY || c.interactive_id == 0xFFFFu || c.interactive_id == 0x7FFEu) return -1;
    if ((movement::tile_flags_of(c.interactive_id) & movement::kTileFlagFood) == 0) return -1;
    const int32_t ax = (c.anchor_x >= 0) ? c.anchor_x : cell.x;       // a cell without anchor bytes is its own anchor
    const int32_t ay = (c.anchor_y >= 0) ? c.anchor_y : cell.y;
    int32_t found = -1;                                                // no break: the last object of the table wins
    for (size_t i = 0; i < food_objects_.size(); ++i) {
        if (food_objects_[i].row == ay && food_objects_[i].col == ax) found = static_cast<int32_t>(i);
    }
    return found;
}

uint32_t Grid::take_food(int32_t object, uint16_t units, bool& stage_changed) noexcept {
    stage_changed = false;
    if (object < 0 || static_cast<size_t>(object) >= food_objects_.size()) return 0;
    FoodObject& o = food_objects_[static_cast<size_t>(object)];
    const uint16_t before = o.stage_tile();
    const uint16_t taken = std::min(units, o.remaining);
    o.remaining = static_cast<uint16_t>(o.remaining - taken);
    stage_changed = (before != o.stage_tile());
    return static_cast<uint32_t>(o.value) * taken;
}

int32_t Grid::add_food_object(FoodObject object) {
    food_objects_.push_back(std::move(object));
    const FoodObject& o = food_objects_.back();
    set_food_tile(TileCoord{static_cast<int32_t>(o.col), static_cast<int32_t>(o.row)}, o.stage_tile());
    return static_cast<int32_t>(food_objects_.size() - 1);
}

void Grid::set_food_tile(TileCoord anchor, uint16_t tile) noexcept {
    if (!in_bounds(anchor)) return;
    TileCell& a = get_cell_mut(static_cast<uint32_t>(anchor.x), static_cast<uint32_t>(anchor.y));
    if (a.interactive_id == tile) return;                              // 0x1007397: nothing to do
    auto clear_cell = [this](TileCoord t) {                            // FUN_0100744f: layer-2 word, anchor bytes, object bit
        if (!in_bounds(t)) return;
        TileCell& c = get_cell_mut(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y));
        c.interactive_id = TILE_EMPTY;
        c.interactive_owner = 255;
        c.timer_ticks = 0;
        c.is_food = false;
        c.is_obstacle_overlay = false;
        c.anchor_x = -1;
        c.anchor_y = -1;
        if (t.y != 0) c.static_solid = false;                          // FUN_0100660c never clears row 0
    };
    auto set_cell = [this, tile, anchor](TileCoord t) {                // FUN_01007a22: tile, anchor bytes, object bit
        if (!in_bounds(t)) return;
        TileCell& c = get_cell_mut(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y));
        c.interactive_id = tile;
        c.interactive_owner = 255;
        c.timer_ticks = 0;
        c.is_food = (tile != 0x7FFEu) && (movement::tile_flags_of(tile) & movement::kTileFlagFood) != 0;       // (FUN_010071dd: the tile flag 0x02)
        c.is_powerup = false;                                          // the cell now holds this tile: a power-up that the file had under the pile's footprint is gone with its tile
        c.powerup_type = 0;
        c.anchor_x = static_cast<int16_t>(anchor.x);
        c.anchor_y = static_cast<int16_t>(anchor.y);
        if ((movement::tile_flags_of(tile) & movement::kTileFlagSolid) != 0) c.static_solid = true;     // the tile flag 0x01 is copied to the layer-1 solid bit
    };
    if (a.interactive_id != TILE_EMPTY && a.interactive_id != 0xFFFFu && a.interactive_id != 0x7FFEu) {
        const movement::FootprintSpan old = movement::food_footprint(a.interactive_id);
        for (size_t k = 0; k < old.count; ++k) {
            clear_cell(TileCoord{anchor.x + old.cells[k].dcol, anchor.y + old.cells[k].drow});
        }
        clear_cell(anchor);
    }
    if (tile == 0x7FFEu || tile == TILE_EMPTY) return;
    if (!tile_graphic_loaded(tile)) return;                            // FUN_01007352 returns 0 here: the old object is gone (cleared above) and the new tile is not placed
    const movement::FootprintSpan span = movement::food_footprint(tile);
    for (size_t k = 0; k < span.count; ++k) {
        set_cell(TileCoord{anchor.x + span.cells[k].dcol, anchor.y + span.cells[k].drow});
    }
    set_cell(anchor);                                                  // the anchor cell always gets the tile
}

void Grid::drop_lunchbox(uint32_t x, uint32_t y, uint32_t points) noexcept {
    if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
    FoodObject o;
    o.row = static_cast<uint16_t>(y);
    o.col = static_cast<uint16_t>(x);
    o.units = 1;
    o.value = static_cast<uint16_t>(std::min<uint32_t>(points, 0xFFFFu));
    o.remaining = 1;
    o.thresholds = {1, 0};
    o.stage_tiles = {TILE_LUNCHBOX, 0x7FFEu};
    add_food_object(std::move(o));
}

void Grid::clear_powerup(int32_t x, int32_t y) noexcept {
    if (!in_bounds(x, y)) return;
    auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    cell.is_powerup = false;
    cell.powerup_type = 0;
    cell.interactive_id = TILE_EMPTY;
    if (y > 0) cell.static_solid = false;                   // SetTile(2, tile, 0x7ffe) clears the footprint's solid bit (row 0 never)
}

void Grid::strip_pickups() noexcept {
    for (const FoodObject& o : food_objects_) set_food_tile(TileCoord{static_cast<int32_t>(o.col), static_cast<int32_t>(o.row)}, 0x7FFEu);
    food_objects_.clear();
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            if (get_cell(x, y).is_powerup) clear_powerup(static_cast<int32_t>(x), static_cast<int32_t>(y));
            TileCell& cell = get_cell_mut(x, y);
            if (cell.is_food) {                                            // a food tile that no Block 2 object owns (community maps have them): it goes as the owned ones do
                cell.interactive_id = TILE_EMPTY;
                cell.interactive_owner = 255;
                cell.timer_ticks = 0;
                cell.is_food = false;
                cell.is_obstacle_overlay = false;
                cell.anchor_x = -1;
                cell.anchor_y = -1;
                if (y != 0) cell.static_solid = false;
            }
        }
    }
}

void Grid::place_powerup(int32_t x, int32_t y, uint8_t powerup_type) noexcept {
    if (!in_bounds(x, y)) return;
    auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    cell.is_powerup = true;
    cell.powerup_type = powerup_type;
    cell.interactive_id = 0x8000u | static_cast<uint16_t>(powerup_type);
    cell.static_solid = true;                               // SetTile copies the power-up tile's flag 0x01 (ids 62 .. 66) to the layer-1 solid bit
}

} // namespace ants::sim

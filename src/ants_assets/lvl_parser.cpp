#include "ants_assets/lvl_parser.hpp"
#include <fstream>
#include <cstring>

namespace ants::assets {

namespace {

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    size_t pos() const noexcept { return pos_; }
    size_t remaining() const noexcept { return (pos_ < size_) ? (size_ - pos_) : 0; }

    bool read_u8(uint8_t& val) noexcept {
        if (pos_ + 1 > size_) return false;
        val = data_[pos_++];
        return true;
    }

    bool read_u16(uint16_t& val) noexcept {
        if (pos_ + 2 > size_) return false;
        val = static_cast<uint16_t>(
            static_cast<uint32_t>(data_[pos_]) |
            (static_cast<uint32_t>(data_[pos_ + 1]) << 8)
        );
        pos_ += 2;
        return true;
    }

    bool read_u32(uint32_t& val) noexcept {
        if (pos_ + 4 > size_) return false;
        val = static_cast<uint32_t>(data_[pos_]) |
              (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
              (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
              (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    bool read_double(double& val) noexcept {
        if (pos_ + 8 > size_) return false;
        std::memcpy(&val, data_ + pos_, 8);
        pos_ += 8;
        return true;
    }

    bool read_bytes(void* dst, size_t len) noexcept {
        if (pos_ + len > size_) return false;
        std::memcpy(dst, data_ + pos_, len);
        pos_ += len;
        return true;
    }

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t pos_{0};
};

static const MapCell EMPTY_CELL{LVL_EMPTY_TILE, 0, 0};
static const std::string EMPTY_STRING;

} // anonymous namespace

const MapCell& LevelData::get_cell_layer1(uint32_t x, uint32_t y) const {
    if (x >= width || y >= height) return EMPTY_CELL;
    size_t idx = static_cast<size_t>(y) * width + x;
    if (idx >= layer1_terrain.size()) return EMPTY_CELL;
    return layer1_terrain[idx];
}

const MapCell& LevelData::get_cell_layer2(uint32_t x, uint32_t y) const {
    if (x >= width || y >= height) return EMPTY_CELL;
    size_t idx = static_cast<size_t>(y) * width + x;
    if (idx >= layer2_interactive.size()) return EMPTY_CELL;
    return layer2_interactive[idx];
}

const std::string& LevelData::get_tile_name(uint16_t tile_index) const {
    if (tile_index >= tile_dictionary.size()) return EMPTY_STRING;
    return tile_dictionary[tile_index];
}

int32_t LevelData::find_tile_index(const std::string& name) const noexcept {
    for (size_t i = 0; i < tile_dictionary.size(); ++i) {
        if (tile_dictionary[i] == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool LevelData::load_from_file(const std::string& filepath) {
    return LVLParser::load_from_file(filepath, *this);
}

bool LevelData::load_from_memory(const uint8_t* data, size_t size) {
    return LVLParser::load_from_memory(data, size, *this);
}

bool LVLParser::load_from_file(const std::string& filepath, LevelData& out_level) {
    try {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize size = file.tellg();
        if (size < 42) return false;
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return false;
        }

        return load_from_memory(buffer.data(), buffer.size(), out_level);
    } catch (...) {
        out_level = LevelData{};
        return false;
    }
}

bool LVLParser::load_from_memory(const uint8_t* data, size_t size, LevelData& out_level) {
    try {
        if (!data || size < 42) return false;
        BinaryReader r(data, size);

        // 1. Fixed Header (42 bytes)
        if (!r.read_u32(out_level.version) ||
            !r.read_u32(out_level.game_mode) ||
            !r.read_u16(out_level.default_minutes)) {
            return false;
        }

        if (out_level.version != LVL_EXPECTED_VERSION || out_level.game_mode != LVL_GAME_MODE_STANDARD) {
            return false;
        }

        char desc_buf[30];
        if (!r.read_bytes(desc_buf, 30)) return false;
        size_t desc_len = 0;
        while (desc_len < 30 && desc_buf[desc_len] != '\0') {
            ++desc_len;
        }
        out_level.description.assign(desc_buf, desc_len);

        if (!r.read_u16(out_level.tile_type_count)) return false;

        // 2. Tile Dictionary: (tile_type_count + 1) entries of 11 bytes
        size_t dict_count = static_cast<size_t>(out_level.tile_type_count) + 1;
        if (r.remaining() < dict_count * 11) return false;
        out_level.tile_dictionary.resize(dict_count);
        for (size_t i = 0; i < dict_count; ++i) {
            char name_buf[11];
            if (!r.read_bytes(name_buf, 11)) return false;
            size_t nlen = 0;
            while (nlen < 11 && name_buf[nlen] != '\0') {
                ++nlen;
            }
            out_level.tile_dictionary[i].assign(name_buf, nlen);
        }

        // 3. Grid Dimensions (8 bytes)
        if (!r.read_u32(out_level.width) || !r.read_u32(out_level.height)) return false;
        // Enforce upper sanity limit on map dimensions (Microsoft Ants max is 60x60, allow up to 256x256)
        if (out_level.width == 0 || out_level.height == 0 ||
            out_level.width > 256 || out_level.height > 256) {
            return false;
        }

        size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;

        // Stream capacity check BEFORE allocating terrain layers (Layer 1 + Layer 2 = 12 bytes/cell)
        if (r.remaining() < cell_count * 12) {
            return false;
        }

        // 4. Layer 1: Base Terrain Grid (cell_count * 6 bytes)
        out_level.layer1_terrain.resize(cell_count);
        for (size_t i = 0; i < cell_count; ++i) {
            MapCell& c = out_level.layer1_terrain[i];
            if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
                return false;
            }
        }

        // 5. Layer 2: Interactive Overlay Grid (cell_count * 6 bytes)
        out_level.layer2_interactive.resize(cell_count);
        for (size_t i = 0; i < cell_count; ++i) {
            MapCell& c = out_level.layer2_interactive[i];
            if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
                return false;
            }
        }

        // 6. Trailing Block 1: Anthill Spawns (b1_count * 6 bytes)
        uint16_t b1_count = 0;
        if (!r.read_u16(b1_count)) return false;
        if (r.remaining() < static_cast<size_t>(b1_count) * 6) return false;
        out_level.anthill_spawns.resize(b1_count);
        for (size_t i = 0; i < b1_count; ++i) {
            AnthillSpawn& sp = out_level.anthill_spawns[i];
            if (!r.read_u16(sp.tile_id) || !r.read_u16(sp.y) || !r.read_u16(sp.x)) {
                return false;
            }

            const std::string& tname = out_level.get_tile_name(sp.tile_id);
            if (tname.find("BSTART") != std::string::npos) {
                sp.team_id = 0; // Black
            } else if (tname.find("USTART") != std::string::npos) {
                sp.team_id = 1; // Blue
            } else if (tname.find("RSTART") != std::string::npos) {
                sp.team_id = 2; // Red
            } else if (tname.find("GSTART") != std::string::npos) {
                sp.team_id = 3; // Green
            } else {
                sp.team_id = 255;
            }
        }

        // 7. Trailing Block 2: Food Respawn Pools (at least b2_count * 10 bytes)
        uint16_t b2_count = 0;
        if (!r.read_u16(b2_count)) return false;
        if (r.remaining() < static_cast<size_t>(b2_count) * 10) return false;
        out_level.food_schedules.resize(b2_count);
        for (size_t i = 0; i < b2_count; ++i) {
            FoodSchedule& fs = out_level.food_schedules[i];
            uint16_t item_count = 0;
            if (!r.read_u16(fs.y) || !r.read_u16(fs.x) ||
                !r.read_u16(fs.initial_delay) || !r.read_u16(fs.respawn_interval) ||
                !r.read_u16(item_count)) {
                return false;
            }

            if (r.remaining() < static_cast<size_t>(item_count) * 4) return false;
            fs.variants.resize(item_count);
            for (size_t v = 0; v < item_count; ++v) {
                FoodItemVariant& var = fs.variants[v];
                if (!r.read_u16(var.weight) || !r.read_u16(var.tile_id)) {
                    return false;
                }
            }
        }

        // 8. Trailing Block 3: Ambient Parameters
        if (!r.read_u16(out_level.ambient_flag) ||
            !r.read_u16(out_level.ambient_tile_or_sound)) {
            return false;
        }

        // 9. Trailing Block 4: Waypoints (at least b4_count * 8 bytes)
        uint16_t b4_count = 0;
        if (!r.read_u16(b4_count)) return false;
        if (r.remaining() < static_cast<size_t>(b4_count) * 8) return false;
        out_level.waypoints.resize(b4_count);
        for (size_t i = 0; i < b4_count; ++i) {
            Waypoint& wp = out_level.waypoints[i];
            if (!r.read_u16(wp.y) || !r.read_u16(wp.x) || !r.read_u32(wp.flag)) {
                return false;
            }

            if (wp.flag != 0) {
                if (!r.read_u32(wp.param)) return false;
                for (size_t p = 0; p < 5; ++p) {
                    if (!r.read_double(wp.probabilities[p])) {
                        return false;
                    }
                }
            } else {
                wp.param = 0;
                wp.probabilities.fill(0.0);
            }
        }

        // 10. Final Boundary Parameter
        if (!r.read_u16(out_level.boundary_param)) return false;

        // Strict 0 remaining bytes check
        return (r.pos() == size);
    } catch (...) {
        out_level = LevelData{};
        return false;
    }
}

} // namespace ants::assets

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <fstream>
#include <cstring>
#include <memory>
#include <algorithm>
#include <map>
#include <cmath>
#include <iostream>

namespace e2e {

// --- ASSET FORMATS & STRUCTURES ---

struct ColorRGBA {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

struct CHDHeader {
    uint32_t version{0};
    uint32_t timestamp{0};
    uint32_t table1_offset{0};
    uint32_t table2_offset{0};
    uint32_t table3_offset{0};
    uint32_t table4_offset{0};
    uint32_t palette_bytes{0};
};

struct SpriteInfo {
    uint32_t index{0};
    uint32_t pitch{0};
    uint32_t width{0};
    uint32_t height{0};
    std::string filename;
    std::vector<uint8_t> pixels;
};

struct SoundClip {
    uint32_t sound_id{0};
    uint16_t format_tag{1}; // 1 = PCM
    uint16_t channels{1};
    uint32_t samples_per_sec{11025};
    uint32_t avg_bytes_per_sec{11025};
    uint16_t block_align{1};
    uint16_t bits_per_sample{8};
    std::string filename;
    std::vector<uint8_t> pcm_data;
};

struct AnimFrame {
    int32_t dx{0};
    int32_t dy{0};
    uint32_t sprite_index{0};
};

struct AnimSubItem {
    uint32_t default_sp{0xFFFFFFFF};
    uint32_t box_left{0};
    uint32_t box_top{0};
    uint32_t box_right{0};
    uint32_t box_bottom{0};
    std::vector<AnimFrame> frames;
};

struct AnimationInfo {
    uint32_t anim_id{0};
    std::string name;
    std::vector<AnimSubItem> subitems;
};

// CHD Reader directly reading ants.chd
class ChdReader {
public:
    static bool load_file(const std::string& path, CHDHeader& out_header, std::array<ColorRGBA, 256>& out_palette) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&out_header), sizeof(CHDHeader));
        if (file.gcount() != sizeof(CHDHeader)) return false;

        // Palette starts at 0x1C (28 bytes)
        for (int i = 0; i < 256; ++i) {
            uint8_t entry[4];
            file.read(reinterpret_cast<char*>(entry), 4);
            out_palette[i].r = entry[0];
            out_palette[i].g = entry[1];
            out_palette[i].b = entry[2];
            // Index 254 is transparent DirectDraw color key
            out_palette[i].a = (i == 254) ? 0 : 255;
        }

        return true;
    }

    static bool load_table1_summary(const std::string& path, uint32_t& out_count, std::vector<uint32_t>& out_offsets) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.seekg(1052);
        file.read(reinterpret_cast<char*>(&out_count), 4);
        if (out_count > 100000) return false;

        out_offsets.resize(out_count);
        file.read(reinterpret_cast<char*>(out_offsets.data()), out_count * 4);
        return true;
    }

    static bool read_sprite(const std::string& path, uint32_t offset, SpriteInfo& out_sprite) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.seekg(0, std::ios::end);
        size_t fsize = file.tellg();
        if (offset >= fsize || offset + 16 > fsize) return false;

        file.seekg(offset);
        file.read(reinterpret_cast<char*>(&out_sprite.pitch), 4);
        file.read(reinterpret_cast<char*>(&out_sprite.width), 4);
        file.read(reinterpret_cast<char*>(&out_sprite.height), 4);

        uint32_t fn_len = 0;
        file.read(reinterpret_cast<char*>(&fn_len), 4);
        if (fn_len > 256) return false;

        std::vector<char> name_buf(fn_len + 1, 0);
        file.read(name_buf.data(), fn_len);
        out_sprite.filename = name_buf.data();

        uint32_t pix_size = out_sprite.pitch * out_sprite.height;
        out_sprite.pixels.resize(pix_size);
        file.read(reinterpret_cast<char*>(out_sprite.pixels.data()), pix_size);
        return true;
    }

    static bool load_table2_summary(const std::string& path, uint32_t& out_count, std::vector<uint32_t>& out_offsets) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.seekg(6835937);
        file.read(reinterpret_cast<char*>(&out_count), 4);
        if (out_count > 10000) return false;

        out_offsets.resize(out_count);
        file.read(reinterpret_cast<char*>(out_offsets.data()), out_count * 4);
        return true;
    }

    static bool read_sound(const std::string& path, uint32_t offset, SoundClip& out_sound) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.seekg(0, std::ios::end);
        size_t fsize = file.tellg();
        if (offset >= fsize || offset + 16 > fsize) return false;

        file.seekg(offset);
        uint32_t format_len = 0;
        file.read(reinterpret_cast<char*>(&format_len), 4);
        if (format_len < 16 || format_len > 100) return false;

        file.read(reinterpret_cast<char*>(&out_sound.format_tag), 2);
        file.read(reinterpret_cast<char*>(&out_sound.channels), 2);
        file.read(reinterpret_cast<char*>(&out_sound.samples_per_sec), 4);
        file.read(reinterpret_cast<char*>(&out_sound.avg_bytes_per_sec), 4);
        file.read(reinterpret_cast<char*>(&out_sound.block_align), 2);
        file.read(reinterpret_cast<char*>(&out_sound.bits_per_sample), 2);

        if (format_len > 16) {
            file.seekg(offset + 4 + format_len);
        }

        uint32_t pcm_len = 0;
        file.read(reinterpret_cast<char*>(&pcm_len), 4);
        out_sound.pcm_data.resize(pcm_len);
        file.read(reinterpret_cast<char*>(out_sound.pcm_data.data()), pcm_len);

        uint32_t fn_len = 0;
        file.read(reinterpret_cast<char*>(&fn_len), 4);
        if (fn_len < 256) {
            std::vector<char> fn_buf(fn_len + 1, 0);
            file.read(fn_buf.data(), fn_len);
            out_sound.filename = fn_buf.data();
        }
        return true;
    }

    static bool load_table4_summary(const std::string& path, uint32_t& out_count, std::vector<uint32_t>& out_offsets) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.seekg(7903835);
        file.read(reinterpret_cast<char*>(&out_count), 4);
        if (out_count > 10000) return false;

        out_offsets.resize(out_count);
        file.read(reinterpret_cast<char*>(out_offsets.data()), out_count * 4);
        return true;
    }
};

// LVL Map Reader
struct LVLHeader {
    uint32_t version{0};
    uint32_t game_mode{0};
    uint16_t default_minutes{0};
    char description[31]{0};
    uint16_t tile_type_count{0};
    std::vector<std::string> tile_names;
    uint32_t width{0};
    uint32_t height{0};
};

struct SpawnRecord {
    uint16_t tile_id;
    uint16_t y;
    uint16_t x;
};

struct FoodSpawnPoint {
    uint16_t x;
    uint16_t y;
    uint16_t initial_delay;
    uint16_t respawn_interval;
    struct ItemVariant {
        uint16_t weight;
        uint16_t tile_id;
    };
    std::vector<ItemVariant> variants;
};

class LvlReader {
public:
    static bool parse_lvl(const std::string& path, LVLHeader& out_header,
                          std::vector<uint8_t>& out_layer1,
                          std::vector<uint8_t>& out_layer2,
                          std::vector<SpawnRecord>& out_spawns,
                          std::vector<FoodSpawnPoint>& out_food,
                          uint32_t& out_remaining_bytes) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&out_header.version), 4);
        file.read(reinterpret_cast<char*>(&out_header.game_mode), 4);
        file.read(reinterpret_cast<char*>(&out_header.default_minutes), 2);
        file.read(out_header.description, 30);
        out_header.description[30] = '\0';
        file.read(reinterpret_cast<char*>(&out_header.tile_type_count), 2);

        out_header.tile_names.resize(out_header.tile_type_count + 1);
        for (uint16_t i = 0; i <= out_header.tile_type_count; ++i) {
            char name_buf[12] = {0};
            file.read(name_buf, 11);
            out_header.tile_names[i] = name_buf;
        }

        file.read(reinterpret_cast<char*>(&out_header.width), 4);
        file.read(reinterpret_cast<char*>(&out_header.height), 4);

        size_t grid_bytes = out_header.width * out_header.height * 6;
        out_layer1.resize(grid_bytes);
        file.read(reinterpret_cast<char*>(out_layer1.data()), grid_bytes);

        out_layer2.resize(grid_bytes);
        file.read(reinterpret_cast<char*>(out_layer2.data()), grid_bytes);

        // Block 1: Spawns
        uint16_t spawn_count = 0;
        file.read(reinterpret_cast<char*>(&spawn_count), 2);
        out_spawns.resize(spawn_count);
        for (uint16_t i = 0; i < spawn_count; ++i) {
            file.read(reinterpret_cast<char*>(&out_spawns[i].tile_id), 2);
            file.read(reinterpret_cast<char*>(&out_spawns[i].y), 2);
            file.read(reinterpret_cast<char*>(&out_spawns[i].x), 2);
        }

        // Block 2: Food schedules
        uint16_t food_count = 0;
        file.read(reinterpret_cast<char*>(&food_count), 2);
        out_food.resize(food_count);
        for (uint16_t i = 0; i < food_count; ++i) {
            file.read(reinterpret_cast<char*>(&out_food[i].x), 2);
            file.read(reinterpret_cast<char*>(&out_food[i].y), 2);
            file.read(reinterpret_cast<char*>(&out_food[i].initial_delay), 2);
            file.read(reinterpret_cast<char*>(&out_food[i].respawn_interval), 2);
            uint16_t item_count = 0;
            file.read(reinterpret_cast<char*>(&item_count), 2);
            out_food[i].variants.resize(item_count);
            for (uint16_t j = 0; j < item_count; ++j) {
                file.read(reinterpret_cast<char*>(&out_food[i].variants[j].weight), 2);
                file.read(reinterpret_cast<char*>(&out_food[i].variants[j].tile_id), 2);
            }
        }

        // Block 3: Ambient
        uint16_t amb_flag = 0, amb_id = 0;
        file.read(reinterpret_cast<char*>(&amb_flag), 2);
        file.read(reinterpret_cast<char*>(&amb_id), 2);

        // Block 4: Waypoints
        uint16_t wp_count = 0;
        file.read(reinterpret_cast<char*>(&wp_count), 2);
        for (uint16_t i = 0; i < wp_count; ++i) {
            uint16_t wx = 0, wy = 0;
            uint32_t wflag = 0;
            file.read(reinterpret_cast<char*>(&wx), 2);
            file.read(reinterpret_cast<char*>(&wy), 2);
            file.read(reinterpret_cast<char*>(&wflag), 4);
            if (wflag != 0) {
                uint32_t param = 0;
                file.read(reinterpret_cast<char*>(&param), 4);
                char pts[40];
                file.read(pts, 40);
            }
        }

        // f_last: 2 bytes
        uint16_t f_last = 0;
        file.read(reinterpret_cast<char*>(&f_last), 2);

        // Check if at EOF
        auto cur_pos = file.tellg();
        file.seekg(0, std::ios::end);
        auto end_pos = file.tellg();
        out_remaining_bytes = static_cast<uint32_t>(end_pos - cur_pos);

        return true;
    }
};

// 5-to-8 Directional Mirroring
enum class Direction {
    North = 7,
    NorthEast = 8,
    East = 9,
    SouthEast = 2,
    South = 3,
    SouthWest = 225,
    West = 270,
    NorthWest = 315
};

struct MirrorResult {
    bool is_mirrored{false};
    uint32_t source_direction{0};
    int32_t dx_prime{0};
    std::vector<uint8_t> mirrored_pixels;
};

inline MirrorResult compute_mirror(Direction dir, int32_t original_dx, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels) {
    MirrorResult res;
    if (dir == Direction::SouthWest) {
        res.is_mirrored = true;
        res.source_direction = 2; // SouthEast
    } else if (dir == Direction::West) {
        res.is_mirrored = true;
        res.source_direction = 9; // East
    } else if (dir == Direction::NorthWest) {
        res.is_mirrored = true;
        res.source_direction = 8; // NorthEast
    } else {
        res.is_mirrored = false;
        res.source_direction = static_cast<uint32_t>(dir);
        res.dx_prime = original_dx;
        res.mirrored_pixels = pixels;
        return res;
    }

    // Formula: dx' = -(dx + W)
    res.dx_prime = -(original_dx + static_cast<int32_t>(width));

    // Horizontal reflection: row by row, reverse pixels
    res.mirrored_pixels.resize(pixels.size());
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            res.mirrored_pixels[y * width + (width - 1 - x)] = pixels[y * width + x];
        }
    }
    return res;
}

// --- DETERMINISTIC SIMULATION ORACLE & CONTRACT MODELS ---

enum class AntType : uint8_t {
    Worker = 0,
    Bomber = 1,
    Fire = 2,
    Thief = 3,
    Combat = 4,
    Swimmer = 5
};

enum class AntState : uint8_t {
    Idle = 0,
    Moving = 1,
    Attacking = 2,
    Flinching = 10,
    Stunned = 12,
    AirborneKnockback = 14,
    Bouncing = 19,
    Drowning = 20,
    Infiltrating = 25,
    BaseEntry = 30,
    Dead = 0x0F
};

struct Vec2i {
    int32_t x{0};
    int32_t y{0};
    bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2i& o) const { return !(*this == o); }
};

inline int32_t chebyshev_distance(const Vec2i& a, const Vec2i& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

inline int32_t manhattan_distance(const Vec2i& a, const Vec2i& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

// MSVC LCG PRNG Engine
class MsvcPrng {
public:
    explicit MsvcPrng(uint32_t seed = 0) : state_(seed) {}
    void seed(uint32_t s) { state_ = s; }
    uint32_t next() {
        state_ = state_ * 214013u + 2531011u;
        return (state_ >> 16) & 0x7FFFu;
    }
    uint32_t raw_state() const { return state_; }
private:
    uint32_t state_{0};
};

struct AudioEvent {
    uint32_t sound_id{0};
    int32_t world_x{0};
    int32_t world_y{0};
    uint32_t priority{0};
    uint8_t target_player{255}; // 255 = all
};

struct NewsEvent {
    uint8_t target_player{0};
    std::string text;
};

struct AntUnit {
    uint32_t id{0};
    uint8_t team{0};
    AntType type{AntType::Worker};
    Vec2i pos{0, 0};
    Vec2i guard_tile{0, 0};
    int32_t hp{10};
    AntState state{AntState::Idle};
    uint32_t state_ticks{0};
    bool holding_lunchbox{false};
    uint32_t carried_points{0};
    Vec2i knockback_velocity{0, 0};
    uint32_t drown_subitem{0};
};

struct GroundItem {
    uint16_t tile_id{0}; // e.g. 356 = lunchbox, 134 = firewall, 60 = bomb
    uint8_t team_owner{0};
    uint32_t created_tick{0};
    uint32_t lifetime_ticks{0}; // e.g. 3600 ticks (180s)
};

struct PlayerStats {
    int32_t score{0};
    uint32_t friendly_lost{0};
    uint32_t enemy_killed{0};
    uint32_t new_hatched{0};
    uint32_t egg_count{10};
    uint8_t ally_id{4}; // 4 = FFA independent
};

class SimulationModel {
public:
    SimulationModel(uint32_t width = 60, uint32_t height = 60, uint32_t seed = 42)
        : width_(width), height_(height), prng_(seed) {
        terrain_.assign(width * height, 0); // 0 = Walkable ground
        for (int p = 0; p < 4; ++p) {
            players_[p].score = 0;
            players_[p].egg_count = 10;
            players_[p].ally_id = 4;
        }
        match_time_ms_ = 12 * 60 * 1000; // 12 minutes default
    }

    void set_terrain(int32_t x, int32_t y, uint8_t t) {
        if (in_bounds(x, y)) terrain_[y * width_ + x] = t;
    }

    uint8_t get_terrain(int32_t x, int32_t y) const {
        if (!in_bounds(x, y)) return 1; // Wall out of bounds
        return terrain_[y * width_ + x];
    }

    void set_anthill(uint8_t team, Vec2i pos) {
        anthills_[team] = pos;
    }

    Vec2i get_anthill(uint8_t team) const {
        auto it = anthills_.find(team);
        if (it != anthills_.end()) return it->second;
        return {0, 0};
    }

    bool in_bounds(int32_t x, int32_t y) const {
        return x >= 0 && x < static_cast<int32_t>(width_) && y >= 0 && y < static_cast<int32_t>(height_);
    }

    uint32_t spawn_ant(uint8_t team, AntType type, Vec2i pos) {
        AntUnit u;
        u.id = ++next_unit_id_;
        u.team = team;
        u.type = type;
        u.pos = pos;
        u.guard_tile = pos;
        u.hp = 10;
        u.state = AntState::Idle;
        units_.push_back(u);
        return u.id;
    }

    AntUnit* find_unit(uint32_t id) {
        for (auto& u : units_) {
            if (u.id == id) return &u;
        }
        return nullptr;
    }

    const std::vector<AntUnit>& units() const { return units_; }
    std::vector<AntUnit>& units() { return units_; }

    const PlayerStats& get_player_stats(uint8_t p) const { return players_[p]; }
    PlayerStats& get_player_stats(uint8_t p) { return players_[p]; }

    uint32_t match_time_ms() const { return match_time_ms_; }
    void set_match_time_ms(uint32_t ms) { match_time_ms_ = ms; }
    bool is_frozen() const { return match_time_ms_ == 0; }

    const std::vector<AudioEvent>& audio_events() const { return audio_events_; }
    void clear_audio_events() { audio_events_.clear(); }

    const std::vector<NewsEvent>& news_events() const { return news_events_; }
    void clear_news_events() { news_events_.clear(); }

    MsvcPrng& prng() { return prng_; }

    // --- RULES ENFORCEMENT ---

    // Feature 12: Cardinal placement check (|dx| + |dy| == 1)
    bool validate_cardinal_placement(Vec2i unit_pos, Vec2i target_pos) const {
        int32_t dx = std::abs(unit_pos.x - target_pos.x);
        int32_t dy = std::abs(unit_pos.y - target_pos.y);
        return (dx + dy) == 1;
    }

    // Feature 13: Plant Bomb
    bool plant_bomb(uint32_t unit_id, Vec2i target_pos) {
        auto* u = find_unit(unit_id);
        if (!u || u->type != AntType::Bomber) return false;
        if (!validate_cardinal_placement(u->pos, target_pos)) return false;
        if (!in_bounds(target_pos.x, target_pos.y)) return false;

        GroundItem b;
        b.tile_id = 60; // Bomb tile
        b.team_owner = u->team;
        b.created_tick = tick_count_;
        b.lifetime_ticks = 0xFFFFFFFF; // Permanent until defused or exploded
        ground_items_[target_pos] = b;

        audio_events_.push_back({90, target_pos.x, target_pos.y, 1, 255}); // Sound 90: bombpick.wav
        return true;
    }

    // Feature 14: Bomber Squash Defuse
    bool defuse_bomb(uint32_t unit_id, Vec2i bomb_pos) {
        auto* u = find_unit(unit_id);
        if (!u || u->type != AntType::Bomber) return false;
        if (manhattan_distance(u->pos, bomb_pos) > 1) return false;

        auto it = ground_items_.find(bomb_pos);
        if (it == ground_items_.end() || it->second.tile_id != 60) return false;

        // Sounds 73 (bombdrop.wav) + 74 (bombmuffle.wav)
        audio_events_.push_back({73, bomb_pos.x, bomb_pos.y, 1, 255});
        audio_events_.push_back({74, bomb_pos.x, bomb_pos.y, 2, 255});
        ground_items_.erase(it);
        // Bomber takes 0 damage
        return true;
    }

    // Feature 15: Fire Ignition (180s countdown = 3600 ticks)
    bool place_firewall(uint32_t unit_id, Vec2i target_pos) {
        auto* u = find_unit(unit_id);
        if (!u || u->type != AntType::Fire) return false;
        if (!validate_cardinal_placement(u->pos, target_pos)) return false;
        if (!in_bounds(target_pos.x, target_pos.y)) return false;

        GroundItem f;
        f.tile_id = 134; // wallup04
        f.team_owner = u->team;
        f.created_tick = tick_count_;
        f.lifetime_ticks = 3600; // 180s @ 20Hz
        ground_items_[target_pos] = f;

        audio_events_.push_back({67, target_pos.x, target_pos.y, 1, 255}); // Sound 67: firestarta
        audio_events_.push_back({68, target_pos.x, target_pos.y, 2, 255}); // Sound 68: firestartb
        return true;
    }

    // Feature 17: Fire Extinguish
    bool extinguish_fire(uint32_t unit_id, Vec2i fire_pos) {
        auto* u = find_unit(unit_id);
        if (!u || u->type != AntType::Fire) return false;
        if (manhattan_distance(u->pos, fire_pos) > 1) return false;

        auto it = ground_items_.find(fire_pos);
        if (it == ground_items_.end() || it->second.tile_id != 134) return false;

        audio_events_.push_back({69, fire_pos.x, fire_pos.y, 1, 255}); // Sound 69: fireextinguish.wav
        ground_items_.erase(it);
        return true;
    }

    // Feature 19: Build Bridge (4 stages, 180s lifetime)
    bool build_bridge(uint32_t unit_id, Vec2i target_pos) {
        auto* u = find_unit(unit_id);
        if (!u || u->type != AntType::Swimmer) return false;
        if (!validate_cardinal_placement(u->pos, target_pos)) return false;
        if (!in_bounds(target_pos.x, target_pos.y)) return false;

        GroundItem br;
        br.tile_id = 37; // bridge4 fully built
        br.team_owner = u->team;
        br.created_tick = tick_count_;
        br.lifetime_ticks = 3600; // 180s
        ground_items_[target_pos] = br;

        audio_events_.push_back({82, target_pos.x, target_pos.y, 1, 255}); // Sound 82: shovelwater.wav
        return true;
    }

    // Check if tile has bridge
    bool has_bridge(Vec2i pos) const {
        auto it = ground_items_.find(pos);
        return it != ground_items_.end() && (it->second.tile_id >= 34 && it->second.tile_id <= 37);
    }

    // Check if tile has fire
    bool has_fire(Vec2i pos) const {
        auto it = ground_items_.find(pos);
        return it != ground_items_.end() && it->second.tile_id == 134;
    }

    // Check if tile has bomb
    bool has_bomb(Vec2i pos) const {
        auto it = ground_items_.find(pos);
        return it != ground_items_.end() && it->second.tile_id == 60;
    }

    // Check if tile has dropped lunchbox (Anim 356, Sprite 513)
    bool has_lunchbox(Vec2i pos) const {
        auto it = ground_items_.find(pos);
        return it != ground_items_.end() && it->second.tile_id == 356;
    }

    // Melee attack
    bool execute_melee_attack(uint32_t attacker_id, uint32_t target_id) {
        auto* att = find_unit(attacker_id);
        auto* tgt = find_unit(target_id);
        if (!att || !tgt || att->team == tgt->team) return false;
        if (att->state == AntState::Dead || att->hp <= 0) return false;
        if (tgt->state == AntState::Dead || tgt->hp <= 0) return false;
        if (chebyshev_distance(att->pos, tgt->pos) > 1) return false;

        // Friendly fire disabled for allies
        if (are_allied(att->team, tgt->team)) return false;

        if (att->type == AntType::Combat) {
            // Feature 10: 2 HP damage + 4-5 tile knockback
            tgt->hp -= 2;
            audio_events_.push_back({78, tgt->pos.x, tgt->pos.y, 1, 255}); // Sound 78: attack2.wav
            tgt->state = AntState::AirborneKnockback;
            tgt->state_ticks = 12; // 12-tick stun

            // Vector from attacker to target
            int32_t dx = tgt->pos.x - att->pos.x;
            int32_t dy = tgt->pos.y - att->pos.y;
            if (dx == 0 && dy == 0) dx = 1;
            int32_t dist = 5; // 5 tiles knockback
            apply_knockback(*tgt, dx, dy, dist);
        } else {
            // Feature 9: Standard 1 HP melee strike
            tgt->hp -= 1;
            audio_events_.push_back({57, tgt->pos.x, tgt->pos.y, 1, 255}); // Sound 57: attack.wav
            tgt->state = AntState::Flinching;
            tgt->state_ticks = 4;
        }

        if (tgt->hp <= 0) {
            on_unit_killed(*tgt, att->team);
        }
        return true;
    }

    // Feature 10 & 16: Apply knockback with fire ricochet and obstacle bounce
    void apply_knockback(AntUnit& tgt, int32_t dir_x, int32_t dir_y, int32_t distance) {
        for (int step = 0; step < distance; ++step) {
            Vec2i next_pos = {tgt.pos.x + dir_x, tgt.pos.y + dir_y};

            // Bounds check
            if (!in_bounds(next_pos.x, next_pos.y)) {
                // Bounce off map edge
                dir_x = -dir_x;
                dir_y = -dir_y;
                continue;
            }

            // Fire contact check
            if (has_fire(next_pos)) {
                if (tgt.type != AntType::Fire) {
                    // Feature 16: Takes +1 fire damage and bounces off
                    tgt.hp -= 1;
                    audio_events_.push_back({64, next_pos.x, next_pos.y, 1, 255}); // flythump
                    // Random deflection
                    int32_t rnd = (prng_.next() % 3) - 1;
                    dir_x = -dir_x + rnd;
                    dir_y = -dir_y;
                    if (dir_x == 0 && dir_y == 0) dir_x = 1;
                    if (tgt.hp <= 0) {
                        on_unit_killed(tgt, 255);
                        return;
                    }
                    continue;
                }
            }

            // Water contact check (without bridge)
            if (get_terrain(next_pos.x, next_pos.y) == 2 && !has_bridge(next_pos)) {
                tgt.pos = next_pos;
                if (tgt.type != AntType::Swimmer) {
                    // Drowns!
                    drown_unit(tgt);
                    return;
                }
            }

            tgt.pos = next_pos;
        }
    }

    // Drown unit in deep water
    void drown_unit(AntUnit& u) {
        u.state = AntState::Drowning;
        u.drown_subitem = 0;
        audio_events_.push_back({71, u.pos.x, u.pos.y, 1, 255}); // Sound 71: splash.wav
        audio_events_.push_back({72, u.pos.x, u.pos.y, 2, 255}); // Sound 72: antdrown.wav (at subitem 1)
        on_unit_killed(u, 255);
    }

    // Unit killed handler: lunchbox drop and stats
    void on_unit_killed(AntUnit& u, uint8_t killer_team) {
        u.state = AntState::Dead;
        players_[u.team].friendly_lost += 1;
        if (killer_team < 4 && killer_team != u.team) {
            players_[killer_team].enemy_killed += 1;
        }

        // Feature 28: Lunchbox physical drop on death
        if (u.holding_lunchbox || u.carried_points > 0) {
            GroundItem lb;
            lb.tile_id = 356; // Anim 356 / Sprite 513
            lb.team_owner = 255;
            lb.created_tick = tick_count_;
            lb.lifetime_ticks = 0xFFFFFFFF;
            ground_items_[u.pos] = lb;
            u.holding_lunchbox = false;
            u.carried_points = 0;
        }
    }

    // Feature 24: Egg Hatching (costs 200 pts)
    bool hatch_ant(uint8_t team, AntType type) {
        if (players_[team].score < 200 || players_[team].egg_count == 0) return false;

        players_[team].score -= 200;
        players_[team].egg_count -= 1;
        players_[team].new_hatched += 1;

        Vec2i base = get_anthill(team);
        spawn_ant(team, type, base);
        return true;
    }

    // Feature 25-27: Thief Infiltration Dive
    bool thief_infiltrate(uint32_t thief_id, uint8_t victim_team) {
        auto* u = find_unit(thief_id);
        if (!u || u->type != AntType::Thief || u->team == victim_team) return false;
        if (are_allied(u->team, victim_team)) return false;

        Vec2i victim_base = get_anthill(victim_team);
        if (chebyshev_distance(u->pos, victim_base) > 1) return false;

        // 33-frame sequence
        u->state = AntState::Infiltrating;

        // Sounds 84 (steala), 85 (stealb), 86 (stealc)
        audio_events_.push_back({84, victim_base.x, victim_base.y, 1, 255});
        audio_events_.push_back({85, victim_base.x, victim_base.y, 2, 255});
        audio_events_.push_back({86, victim_base.x, victim_base.y, 3, 255});

        // Feature 26: Alarm Siren Sound 58 + News Flash String 53 to victim
        audio_events_.push_back({58, victim_base.x, victim_base.y, 5, victim_team});
        news_events_.push_back({victim_team, "A ThiefAnt is at your anthill!"});

        // Feature 27: Steal min(50, score) + Sound 88 scoredn
        int32_t stolen = std::min(50, players_[victim_team].score);
        players_[victim_team].score -= stolen;
        u->carried_points = stolen;
        u->holding_lunchbox = true;
        audio_events_.push_back({88, victim_base.x, victim_base.y, 4, victim_team});

        return true;
    }

    // Feature 22: Anthill Food Deposit
    bool deposit_food(uint32_t unit_id) {
        auto* u = find_unit(unit_id);
        if (!u) return false;
        Vec2i base = get_anthill(u->team);
        if (u->pos != base) return false;

        if (u->holding_lunchbox || u->carried_points > 0) {
            int32_t pts = u->carried_points > 0 ? u->carried_points : 50;
            players_[u->team].score += pts;
            u->holding_lunchbox = false;
            u->carried_points = 0;
            audio_events_.push_back({87, base.x, base.y, 1, 255}); // Sound 87: scoreup.wav
        }

        // Feature 23: Underground 100% Heal to 10 HP
        u->hp = 10;
        return true;
    }

    // Feature 29 & 30: Alliances
    bool propose_alliance(uint8_t from_player, uint8_t to_player) {
        if (from_player == to_player || from_player >= 4 || to_player >= 4) return false;
        audio_events_.push_back({51, 0, 0, 1, to_player}); // Sound 51: allypro.wav
        return true;
    }

    bool accept_alliance(uint8_t p1, uint8_t p2) {
        if (p1 == p2 || p1 >= 4 || p2 >= 4) return false;
        uint8_t slot = std::min(p1, p2);
        players_[p1].ally_id = slot;
        players_[p2].ally_id = slot;
        audio_events_.push_back({53, 0, 0, 1, 255}); // Sound 53: allyyes.wav
        audio_events_.push_back({50, 0, 0, 2, 255}); // Sound 50: allyon.wav
        return true;
    }

    bool deny_alliance(uint8_t to_player) {
        audio_events_.push_back({52, 0, 0, 1, to_player}); // Sound 52: allynot.wav
        return true;
    }

    bool break_alliance(uint8_t p1, uint8_t p2) {
        if (players_[p1].ally_id == 4) return false;
        players_[p1].ally_id = 4;
        players_[p2].ally_id = 4;
        audio_events_.push_back({49, 0, 0, 1, 255}); // Sound 49: allyoff.wav
        return true;
    }

    bool are_allied(uint8_t p1, uint8_t p2) const {
        if (p1 == p2) return true;
        if (players_[p1].ally_id == 4 || players_[p2].ally_id == 4) return false;
        return players_[p1].ally_id == players_[p2].ally_id;
    }

    int32_t get_team_score(uint8_t p) const {
        if (players_[p].ally_id == 4) {
            return players_[p].score;
        }
        int32_t combined = 0;
        for (int i = 0; i < 4; ++i) {
            if (players_[i].ally_id == players_[p].ally_id) {
                combined += players_[i].score;
            }
        }
        return combined;
    }

    // Determine winner team
    uint8_t get_winner_player() const {
        uint8_t best_p = 0;
        int32_t best_score = -999999;
        for (uint8_t p = 0; p < 4; ++p) {
            int32_t s = get_team_score(p);
            if (s > best_score) {
                best_score = s;
                best_p = p;
            }
        }
        return best_p;
    }

    // Tick simulation advance by 1 tick (50 ms)
    void tick() {
        if (match_time_ms_ == 0) return; // Simulation frozen

        if (match_time_ms_ <= 50) {
            match_time_ms_ = 0;
            // Feature 31 & 32: Simulation freeze & Split Game Over Audio
            uint8_t winner = get_winner_player();
            for (uint8_t p = 0; p < 4; ++p) {
                if (are_allied(p, winner)) {
                    audio_events_.push_back({56, 0, 0, 10, p}); // Sound 56: winner.wav
                } else {
                    audio_events_.push_back({41, 0, 0, 10, p}); // Sound 41: playerout.wav
                }
            }
            return;
        }
        match_time_ms_ -= 50;
        tick_count_++;

        // Feature 18 & 20: 180s Timer Expirations (Firewall burnout & Bridge collapse)
        std::vector<Vec2i> expired_items;
        for (auto& pair : ground_items_) {
            if (pair.second.lifetime_ticks != 0xFFFFFFFF) {
                if (tick_count_ >= pair.second.created_tick + pair.second.lifetime_ticks) {
                    expired_items.push_back(pair.first);
                }
            }
        }

        for (const auto& pos : expired_items) {
            auto it = ground_items_.find(pos);
            if (it != ground_items_.end()) {
                if (it->second.tile_id == 134) {
                    // Firewall burned out
                    audio_events_.push_back({68, pos.x, pos.y, 1, 255});
                } else if (it->second.tile_id >= 34 && it->second.tile_id <= 37) {
                    // Bridge collapse!
                    // Feature 20: Drown all non-swimmers on this tile
                    for (auto& u : units_) {
                        if (u.pos == pos && u.state != AntState::Dead) {
                            if (u.type != AntType::Swimmer) {
                                drown_unit(u);
                            }
                        }
                    }
                }
                ground_items_.erase(it);
            }
        }

        // Feature 11: Combat Ant Autonomous Guard AI
        for (auto& u : units_) {
            if (u.type == AntType::Combat && u.state == AntState::Idle) {
                // Scan 3-tile Chebyshev radius
                for (auto& tgt : units_) {
                    if (tgt.team != u.team && !are_allied(u.team, tgt.team) && tgt.state != AntState::Dead) {
                        if (chebyshev_distance(u.guard_tile, tgt.pos) <= 3) {
                            // Autonomous intercept
                            if (chebyshev_distance(u.pos, tgt.pos) <= 1) {
                                execute_melee_attack(u.id, tgt.id);
                                u.pos = u.guard_tile; // Returns to post
                            } else {
                                // Step towards target
                                int32_t step_x = (tgt.pos.x > u.pos.x) ? 1 : (tgt.pos.x < u.pos.x) ? -1 : 0;
                                int32_t step_y = (tgt.pos.y > u.pos.y) ? 1 : (tgt.pos.y < u.pos.y) ? -1 : 0;
                                u.pos.x += step_x;
                                u.pos.y += step_y;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

private:
    uint32_t width_{60};
    uint32_t height_{60};
    uint32_t next_unit_id_{0};
    uint32_t tick_count_{0};
    uint32_t match_time_ms_{720000};
    MsvcPrng prng_;
    std::vector<uint8_t> terrain_;
    std::vector<AntUnit> units_;
    std::map<Vec2i, GroundItem, bool(*)(const Vec2i&, const Vec2i&)> ground_items_{
        [](const Vec2i& a, const Vec2i& b) {
            if (a.y != b.y) return a.y < b.y;
            return a.x < b.x;
        }
    };
    std::map<uint8_t, Vec2i> anthills_;
    std::array<PlayerStats, 4> players_;
    std::vector<AudioEvent> audio_events_;
    std::vector<NewsEvent> news_events_;
};

// --- APP & HUD MODELS ---

struct ViewportScaler {
    uint32_t window_width{640};
    uint32_t window_height{480};
    uint32_t render_width{640};
    uint32_t render_height{480};
    int32_t offset_x{0};
    int32_t offset_y{0};
    uint32_t scale_factor{1};

    static ViewportScaler compute(uint32_t win_w, uint32_t win_h) {
        ViewportScaler s;
        s.window_width = win_w;
        s.window_height = win_h;

        uint32_t scale_x = win_w / 640;
        uint32_t scale_y = win_h / 480;
        s.scale_factor = std::max(1u, std::min(scale_x, scale_y));

        s.render_width = 640 * s.scale_factor;
        s.render_height = 480 * s.scale_factor;

        s.offset_x = static_cast<int32_t>(win_w - s.render_width) / 2;
        s.offset_y = static_cast<int32_t>(win_h - s.render_height) / 2;
        return s;
    }

    Vec2i screen_to_logical(int32_t sx, int32_t sy) const {
        int32_t lx = (sx - offset_x) / static_cast<int32_t>(scale_factor);
        int32_t ly = (sy - offset_y) / static_cast<int32_t>(scale_factor);
        lx = std::max(0, std::min(639, lx));
        ly = std::max(0, std::min(479, ly));
        return {lx, ly};
    }
};

} // namespace e2e

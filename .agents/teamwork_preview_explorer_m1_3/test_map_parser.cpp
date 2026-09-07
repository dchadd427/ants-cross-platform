#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>

#pragma pack(push, 1)

struct LVLHeaderFixed {
    uint32_t version;          // Expected: 8
    uint32_t game_mode;        // Expected: 1
    uint16_t default_minutes;  // 6, 8, 10, 12
    char     description[30];  // Null-terminated ASCII
    uint16_t tile_type_count;  // N
};

struct LVLCell {
    uint16_t word1;            // Tile dictionary index or 0x7FFE
    uint16_t word2;            // Flags / variation
    uint16_t word3;            // Properties
};

struct LVLSpawnRecord {
    uint16_t tile_id;
    uint16_t y;
    uint16_t x;
};

struct LVLFoodVariant {
    uint16_t weight;
    uint16_t tile_id;
};

struct LVLFoodEntry {
    uint16_t y;
    uint16_t x;
    uint16_t initial_delay;
    uint16_t respawn_interval;
    uint16_t item_count;
    std::vector<LVLFoodVariant> variants;
};

struct LVLAmbient {
    uint16_t flag1;
    uint16_t tile_or_sound_id;
};

struct LVLWaypoint {
    uint16_t y;
    uint16_t x;
    uint32_t flag;
    uint32_t param;
    double   probabilities[5];
};

struct LVLMap {
    uint32_t version;
    uint32_t game_mode;
    uint16_t default_minutes;
    std::string description;
    uint16_t tile_type_count;
    std::vector<std::string> tile_dictionary;
    uint32_t width;
    uint32_t height;
    
    std::vector<LVLCell> layer1;
    std::vector<LVLCell> layer2;
    
    std::vector<LVLSpawnRecord> spawns;
    std::vector<LVLFoodEntry> food_schedules;
    LVLAmbient ambient;
    std::vector<LVLWaypoint> waypoints;
    uint16_t f_last;
};

#pragma pack(pop)

bool parse_lvl_file(const std::string& path, LVLMap& out_map) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << path << std::endl;
        return false;
    }
    
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        std::cerr << "Failed to read file: " << path << std::endl;
        return false;
    }
    
    size_t pos = 0;
    
    if (pos + sizeof(LVLHeaderFixed) > buffer.size()) return false;
    const auto* hdr = reinterpret_cast<const LVLHeaderFixed*>(buffer.data() + pos);
    pos += sizeof(LVLHeaderFixed);
    
    out_map.version = hdr->version;
    out_map.game_mode = hdr->game_mode;
    out_map.default_minutes = hdr->default_minutes;
    out_map.description = std::string(hdr->description, strnlen(hdr->description, 30));
    out_map.tile_type_count = hdr->tile_type_count;
    
    // Read tile dictionary: (tile_type_count + 1) entries of 11 bytes
    size_t dict_entries = out_map.tile_type_count + 1;
    out_map.tile_dictionary.resize(dict_entries);
    for (size_t i = 0; i < dict_entries; ++i) {
        if (pos + 11 > buffer.size()) return false;
        char name_buf[12] = {0};
        std::memcpy(name_buf, buffer.data() + pos, 11);
        pos += 11;
        out_map.tile_dictionary[i] = std::string(name_buf);
    }
    
    // Read width and height
    if (pos + 8 > buffer.size()) return false;
    std::memcpy(&out_map.width, buffer.data() + pos, 4);
    pos += 4;
    std::memcpy(&out_map.height, buffer.data() + pos, 4);
    pos += 4;
    
    size_t total_cells = out_map.width * out_map.height;
    
    // Layer 1
    size_t l1_bytes = total_cells * sizeof(LVLCell);
    if (pos + l1_bytes > buffer.size()) return false;
    out_map.layer1.resize(total_cells);
    std::memcpy(out_map.layer1.data(), buffer.data() + pos, l1_bytes);
    pos += l1_bytes;
    
    // Layer 2
    size_t l2_bytes = total_cells * sizeof(LVLCell);
    if (pos + l2_bytes > buffer.size()) return false;
    out_map.layer2.resize(total_cells);
    std::memcpy(out_map.layer2.data(), buffer.data() + pos, l2_bytes);
    pos += l2_bytes;
    
    // Block 1: Spawns
    if (pos + 2 > buffer.size()) return false;
    uint16_t b1_count = 0;
    std::memcpy(&b1_count, buffer.data() + pos, 2);
    pos += 2;
    
    size_t b1_bytes = b1_count * sizeof(LVLSpawnRecord);
    if (pos + b1_bytes > buffer.size()) return false;
    out_map.spawns.resize(b1_count);
    std::memcpy(out_map.spawns.data(), buffer.data() + pos, b1_bytes);
    pos += b1_bytes;
    
    // Block 2: Food schedules
    if (pos + 2 > buffer.size()) return false;
    uint16_t b2_count = 0;
    std::memcpy(&b2_count, buffer.data() + pos, 2);
    pos += 2;
    
    out_map.food_schedules.resize(b2_count);
    for (size_t i = 0; i < b2_count; ++i) {
        if (pos + 10 > buffer.size()) return false;
        auto& entry = out_map.food_schedules[i];
        std::memcpy(&entry.y, buffer.data() + pos, 2); pos += 2;
        std::memcpy(&entry.x, buffer.data() + pos, 2); pos += 2;
        std::memcpy(&entry.initial_delay, buffer.data() + pos, 2); pos += 2;
        std::memcpy(&entry.respawn_interval, buffer.data() + pos, 2); pos += 2;
        std::memcpy(&entry.item_count, buffer.data() + pos, 2); pos += 2;
        
        entry.variants.resize(entry.item_count);
        size_t v_bytes = entry.item_count * sizeof(LVLFoodVariant);
        if (pos + v_bytes > buffer.size()) return false;
        std::memcpy(entry.variants.data(), buffer.data() + pos, v_bytes);
        pos += v_bytes;
    }
    
    // Block 3: Ambient
    if (pos + sizeof(LVLAmbient) > buffer.size()) return false;
    std::memcpy(&out_map.ambient, buffer.data() + pos, sizeof(LVLAmbient));
    pos += sizeof(LVLAmbient);
    
    // Block 4: Waypoints
    if (pos + 2 > buffer.size()) return false;
    uint16_t b4_count = 0;
    std::memcpy(&b4_count, buffer.data() + pos, 2);
    pos += 2;
    
    out_map.waypoints.resize(b4_count);
    for (size_t i = 0; i < b4_count; ++i) {
        if (pos + 8 > buffer.size()) return false;
        auto& wp = out_map.waypoints[i];
        std::memcpy(&wp.y, buffer.data() + pos, 2); pos += 2;
        std::memcpy(&wp.x, buffer.data() + pos, 2); pos += 2;
        std::memcpy(&wp.flag, buffer.data() + pos, 4); pos += 4;
        
        if (wp.flag != 0) {
            if (pos + 4 + 40 > buffer.size()) return false;
            std::memcpy(&wp.param, buffer.data() + pos, 4); pos += 4;
            std::memcpy(wp.probabilities, buffer.data() + pos, 40); pos += 40;
        } else {
            wp.param = 0;
            std::memset(wp.probabilities, 0, sizeof(wp.probabilities));
        }
    }
    
    // Final field
    if (pos + 2 > buffer.size()) return false;
    std::memcpy(&out_map.f_last, buffer.data() + pos, 2);
    pos += 2;
    
    size_t rem = buffer.size() - pos;
    std::cout << "[" << path << "] Size=" << file_size << " Parsed=" << pos 
              << " Rem=" << rem << (rem == 0 ? " [OK]" : " [MISMATCH]") << std::endl;
              
    return (rem == 0);
}

int main() {
    std::vector<std::string> maps = {
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/TINY.LVL",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/SMALL.LVL",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/MEDIUM.LVL",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/GAUNTLET.LVL",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/ISLANDS.LVL",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/TREASURE.LVL"
    };
    
    bool all_ok = true;
    for (const auto& m : maps) {
        LVLMap lvl;
        if (!parse_lvl_file(m, lvl)) {
            std::cerr << "FAILED to parse " << m << std::endl;
            all_ok = false;
        } else {
            std::cout << "  Title: '" << lvl.description << "', Grid: " 
                      << lvl.width << "x" << lvl.height << ", Dict: " << lvl.tile_dictionary.size()
                      << ", Spawns: " << lvl.spawns.size() << ", Food: " << lvl.food_schedules.size()
                      << ", Waypoints: " << lvl.waypoints.size() << ", f_last: " << lvl.f_last << std::endl;
        }
    }
    
    if (all_ok) {
        std::cout << "\nALL 6 MAPS PARSED SUCCESSFULLY WITH 0 REMAINING BYTES!\n";
        return 0;
    } else {
        std::cerr << "\nERROR: Some maps failed parsing!\n";
        return 1;
    }
}

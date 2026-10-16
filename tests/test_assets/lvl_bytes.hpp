// Synthetic .lvl files for the tests of the map format (tests/test_assets/test_assets.cpp suite 6c, tests/test_assets/test_lvl_writer.cpp): maps are made by editing bytes of
// the shipped TINY.LVL and SMALL.LVL, or from nothing, and the helpers find the parts of a file the way the loader walks it (an independent walker: it is not the
// reader's code and not the writer's).
#pragma once

#include "ants_assets/lvl_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace lvl_edit {

using ants::assets::LevelData;
using ants::assets::LevelProblem;
using ants::assets::LevelProblemKind;
using ants::assets::LevelProblemSeverity;
using ants::assets::LevelValidation;

struct Spots {
    size_t dims{0};                    // the rows dword; the columns dword follows
    uint32_t rows{0};
    uint32_t columns{0};
    size_t layer1{0};
    size_t layer2{0};
    size_t b1_count{0};                // block 1: a word count, then (tile, row, column) words
    size_t b1_records{0};
    uint16_t b1{0};
    size_t b2_count{0};                // block 2: a word count, then per object row, column, units, points, stages (words) and `stages` pairs of words
    uint16_t b2{0};
    std::vector<size_t> food;          // the offset of each object
    size_t b3{0};                      // block 3: two words
    size_t b4_count{0};                // block 4: a word count, then per waypoint row, column (words), a flag dword and, when it is not 0, 44 more bytes
    uint16_t b4{0};
    std::vector<size_t> waypoints;     // the offset of each record
    size_t final_word{0};              // the egg stock
};

inline uint16_t rd16(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); }
inline uint32_t rd32(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint32_t>(rd16(d, p)) | (static_cast<uint32_t>(rd16(d, p + 2)) << 16); }
inline void wr16(std::vector<uint8_t>& d, size_t p, uint32_t v) { d[p] = static_cast<uint8_t>(v & 0xFF); d[p + 1] = static_cast<uint8_t>((v >> 8) & 0xFF); }
inline void wr32(std::vector<uint8_t>& d, size_t p, uint32_t v) { wr16(d, p, v & 0xFFFF); wr16(d, p + 2, v >> 16); }
inline void put16(std::vector<uint8_t>& d, uint32_t v) { d.push_back(static_cast<uint8_t>(v & 0xFF)); d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); }
inline void put32(std::vector<uint8_t>& d, uint32_t v) { put16(d, v & 0xFFFF); put16(d, v >> 16); }

inline std::vector<uint8_t> read_map(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const std::streamsize size = f.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

// The parts of the file `d`, walked as the loader walks them (the grid size by the lower word of each dword, as the original keeps it): true when the file holds
// every part up to and with its egg word. A file that ends before then gives false, with the spots of the parts it holds; nothing past the end of `d` is read.
inline bool locate(const std::vector<uint8_t>& d, Spots& s) {
    s = Spots{};
    auto holds = [&d](size_t at, uint64_t bytes) { return at <= d.size() && bytes <= d.size() - at; };
    if (!holds(40, 2)) return false;
    size_t p = 42 + (static_cast<size_t>(rd16(d, 40)) + 1) * 11;
    s.dims = p;
    if (!holds(p, 8)) return false;
    s.rows = rd32(d, p);
    s.columns = rd32(d, p + 4);
    p += 8;
    const uint64_t layer = static_cast<uint64_t>(s.rows & 0xFFFFu) * (s.columns & 0xFFFFu) * 6u;
    s.layer1 = p;
    if (!holds(p, layer)) return false;
    p += static_cast<size_t>(layer);
    s.layer2 = p;
    if (!holds(p, layer)) return false;
    p += static_cast<size_t>(layer);
    s.b1_count = p;
    if (!holds(p, 2)) return false;
    s.b1 = rd16(d, p);
    s.b1_records = p + 2;
    p += 2;
    if (!holds(p, static_cast<uint64_t>(s.b1) * 6)) return false;
    p += static_cast<size_t>(s.b1) * 6;
    s.b2_count = p;
    if (!holds(p, 2)) return false;
    s.b2 = rd16(d, p);
    p += 2;
    for (size_t i = 0; i < s.b2; ++i) {
        s.food.push_back(p);
        if (!holds(p, 10)) return false;
        const size_t stages = rd16(d, p + 8);
        p += 10;
        if (!holds(p, static_cast<uint64_t>(stages) * 4)) return false;
        p += stages * 4;
    }
    s.b3 = p;
    if (!holds(p, 4)) return false;
    p += 4;
    s.b4_count = p;
    if (!holds(p, 2)) return false;
    s.b4 = rd16(d, p);
    p += 2;
    for (size_t i = 0; i < s.b4; ++i) {
        s.waypoints.push_back(p);
        if (!holds(p, 8)) return false;
        const size_t record = 8u + (rd32(d, p + 4) != 0 ? 44u : 0u);
        if (!holds(p, record)) return false;
        p += record;
    }
    s.final_word = p;
    return holds(p, 2);
}

// The first Block 1 record of the tile id (152 BSTART, 153 USTART, 154 GSTART, 155 RSTART, 421 flower1): its offset, or 0
inline size_t record_of_tile(const std::vector<uint8_t>& d, const Spots& s, uint16_t tile) {
    for (size_t i = 0; i < s.b1; ++i) {
        const size_t at = s.b1_records + i * 6;
        if (rd16(d, at) == tile) return at;
    }
    return 0;
}

inline std::vector<uint8_t> prefix(const std::vector<uint8_t>& d, size_t n) { return std::vector<uint8_t>(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(n)); }

// A flat map from nothing: the header with a dictionary of `names` names, the two dimension dwords as given, `data_rows` x `data_columns` cells of data in each layer
// (empty overlay), and the blocks behind the layers (no start marker, no food, no waypoint, an egg stock of 3)
inline std::vector<uint8_t> flat_map(uint32_t names, uint32_t rows_dword, uint32_t columns_dword, uint32_t data_rows, uint32_t data_columns) {
    std::vector<uint8_t> b;
    put32(b, 8);
    put32(b, 1);
    put16(b, 6);
    for (int i = 0; i < 30; ++i) b.push_back(0);
    put16(b, names - 1);
    for (uint32_t i = 0; i < names * 11; ++i) b.push_back(0);
    put32(b, rows_dword);
    put32(b, columns_dword);
    for (uint32_t i = 0; i < data_rows * data_columns; ++i) { put16(b, 0); put16(b, 0); put16(b, 0); }              // layer 1: tile 0
    for (uint32_t i = 0; i < data_rows * data_columns; ++i) { put16(b, 0x7FFE); put16(b, 0); put16(b, 0); }        // layer 2: empty
    put16(b, 0);                                                                                                      // block 1
    put16(b, 0);                                                                                                      // block 2
    put16(b, 0);
    put16(b, 0x7FFE);                                                                                                 // block 3
    put16(b, 0);                                                                                                      // block 4
    put16(b, 3);                                                                                                      // the final word
    return b;
}

inline bool has_fatal(const LevelValidation& v, LevelProblemKind kind) {
    for (const LevelProblem& p : v.problems) {
        if (p.kind == kind && p.severity == LevelProblemSeverity::Fatal) return true;
    }
    return false;
}

inline bool mentions(const LevelValidation& v, const char* text) { return v.reason().find(text) != std::string::npos; }

inline bool is_empty_level(const LevelData& m) {
    return m.width() == 0u && m.height() == 0u && m.layer1_terrain.empty() && m.layer2_interactive.empty() && m.anthill_spawns.empty() && m.food_schedules.empty() &&
           m.waypoints.empty() && m.load_notes.empty();
}

}  // namespace lvl_edit

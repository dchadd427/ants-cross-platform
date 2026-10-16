// The writer of the .lvl format (include/ants_assets/lvl_writer.hpp, docs/MAP_FORMAT.md), and the reader and the writer on hostile input:
//   W1 the canonical file: the bytes of the original maps where the original reads them, every field of a level in its place, the parts where the loader walks
//   W2 what the writer refuses (a level that no reader can have made), each with its sentence and no byte
//   W3 random levels: written, read back as the same level, written again as the same bytes
//   W4 hostile files: cut at every byte, bytes changed at random (fixed seeds), giant counts, objects outside the grid, food without stages, odd waypoints, empty
//      grids: no crash, a finding that says what is wrong, and every file that loads is written and read back the same; the heap that the reader and the writer
//      take at once is bounded by the size of their input (a counting operator new below)
//   W5 what a rewrite changes in a file (describe_rewrite), kind by kind
//   W6 the dictionary of a new map (the rule of the six original maps, checked on them) and the blank map
// The 540 maps of the library are the round-trip proof's (tests/test_sim/test_map_roundtrip.cpp).
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"
#include "lvl_bytes.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// A counting heap: every operator new of this program goes through here, so a test can ask how much the reader or the writer held at once
// ------------------------------------------------------------------------------------------------------------------------------------------------------

namespace heap {
size_t live = 0;     // bytes asked for and not given back
size_t peak = 0;     // the most `live` has been since the last reset
}  // namespace heap

namespace {
constexpr size_t kHeader = 16;   // keeps the alignment of malloc's blocks
}

// (never inlined: inlined into a caller, GCC's warnings follow the pointer from `new` into the free() of its block and take that for a mismatch)
#if defined(_MSC_VER)
#define HEAP_NOINLINE __declspec(noinline)
#else
#define HEAP_NOINLINE __attribute__((noinline))
#endif

HEAP_NOINLINE void* operator new(std::size_t size) {
    if (size > std::numeric_limits<std::size_t>::max() - kHeader) throw std::bad_alloc();
    void* block = std::malloc(size + kHeader);
    if (!block) throw std::bad_alloc();
    std::memcpy(block, &size, sizeof size);
    heap::live += size;
    if (heap::live > heap::peak) heap::peak = heap::live;
    return static_cast<char*>(block) + kHeader;
}
HEAP_NOINLINE void operator delete(void* p) noexcept {
    if (!p) return;
    char* block = static_cast<char*>(p) - kHeader;
    std::size_t size = 0;
    std::memcpy(&size, block, sizeof size);
    heap::live -= size;
    std::free(block);
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { operator delete(p); }

namespace {

using namespace ants::assets;
using namespace lvl_edit;

int g_checks = 0;
int g_failures = 0;
int g_cases = 0;

void check(bool ok, const std::string& what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "    FAILED: " << what << " (line " << line << ")\n";
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_MSG(cond, text) check((cond), std::string(#cond) + ": " + (text), __LINE__)

void run_case(const std::string& name, const std::function<void()>& fn) {
    ++g_cases;
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    const int before = g_failures;
    fn();
    if (g_failures == before) std::cout << "PASS\n";
}

// The heap that `fn` holds at once above what was held before it
size_t peak_of(const std::function<void()>& fn) {
    const size_t base = heap::live;
    heap::peak = heap::live;
    fn();
    return heap::peak - base;
}

// The bounds: what the reader may hold at once for a file of `bytes` bytes (a level in memory is at most 7 times its file: a waypoint without trigger is 8 bytes
// of file and 56 of memory), and what the writer may hold for a file of `bytes` bytes that it writes (the file, and the level it reads back to check it)
constexpr size_t kSlack = 64 * 1024;
size_t reader_bound(size_t bytes) { return kSlack + 8 * bytes; }
size_t writer_bound(size_t bytes) { return kSlack + 10 * bytes; }

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
    uint16_t word() { return static_cast<uint16_t>(next() & 0xFFFFu); }
};

const std::string& maps_dir() {
    static const std::string dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
    return dir;
}

const char* const kOriginals[] = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};

double from_bits(uint64_t bits) {
    double d = 0;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

void put_text(std::vector<uint8_t>& d, const std::string& s, size_t field) {
    d.insert(d.end(), s.begin(), s.end());
    d.insert(d.end(), field - s.size(), uint8_t{0});
}

void put_double(std::vector<uint8_t>& d, double v) {
    uint8_t raw[8];
    std::memcpy(raw, &v, sizeof raw);
    d.insert(d.end(), raw, raw + 8);
}

// A level that holds a distinct value in every field the file has (W1.2)
LevelData every_field() {
    LevelData l;
    l.version = 8;
    l.game_mode = 0xA1B2C3D4u;
    l.default_minutes = 0x1234;
    l.description = "abcdefghijklmnopqrstuvwxyz0123";                               // 30 bytes: no room for a zero
    l.tile_dictionary = {"a", "bb", "abcdefghijk", "", "."};                       // an 11-byte name has no zero either; an empty one is kept
    l.tile_type_count = 4;
    l.height.val = 2;
    l.width.val = 3;
    for (uint16_t i = 0; i < 6; ++i) {
        l.layer1_terrain.push_back(MapCell{static_cast<uint16_t>(100 + i), static_cast<uint16_t>(200 + i), static_cast<uint16_t>(300 + i)});
        l.layer2_interactive.push_back(MapCell{static_cast<uint16_t>(400 + i), static_cast<uint16_t>(500 + i), static_cast<uint16_t>(600 + i)});
    }
    l.anthill_spawns.push_back(AnthillSpawn{152, 1, 2, 3});                         // the black team's start marker
    l.anthill_spawns.push_back(AnthillSpawn{421, 0, 0, 255});                       // a flower
    l.anthill_spawns.push_back(AnthillSpawn{7, 65535, 65535, 255});                 // a record the original ignores, far outside the grid
    FoodSchedule a;
    a.y = 1;
    a.x = 2;
    a.initial_delay = 5;
    a.respawn_interval = 10;
    a.variants = {FoodItemVariant{5, 253}, FoodItemVariant{2, 254}, FoodItemVariant{0, LVL_EMPTY_TILE}};
    FoodSchedule b;
    b.y = 0;
    b.x = 0;
    b.initial_delay = 1;
    b.respawn_interval = 1;
    b.variants = {FoodItemVariant{1, 300}};
    l.food_schedules.push_back(a);
    l.food_schedules.push_back(b);
    l.ambient_flag = 0xBEEF;
    l.ambient_tile_or_sound = 62;
    Waypoint w0;
    w0.y = 1;
    w0.x = 1;
    Waypoint w1;
    w1.y = 0;
    w1.x = 2;
    w1.flag = 0x80000001u;
    w1.param = 0xFFFFFFFFu;
    w1.probabilities = {0.45, -0.0, from_bits(0x7FF8DEADBEEF0001ull), std::numeric_limits<double>::infinity(), 1e-300};
    l.waypoints = {w0, w1};
    l.boundary_param = 0xFFFF;
    return l;
}

// The bytes of every_field(), written out here by hand in the order of docs/MAP_FORMAT.md (not with the writer's code)
std::vector<uint8_t> every_field_bytes() {
    const LevelData l = every_field();
    std::vector<uint8_t> d;
    put32(d, 8);
    put32(d, 0xA1B2C3D4u);
    put16(d, 0x1234);
    put_text(d, l.description, 30);
    put16(d, 4);
    for (const std::string& n : l.tile_dictionary) put_text(d, n, 11);
    put32(d, 2);                                                                    // rows first
    put32(d, 3);
    for (const MapCell& c : l.layer1_terrain) { put16(d, c.tile_index); put16(d, c.flags); put16(d, c.properties); }
    for (const MapCell& c : l.layer2_interactive) { put16(d, c.tile_index); put16(d, c.flags); put16(d, c.properties); }
    put16(d, 3);
    put16(d, 152); put16(d, 1); put16(d, 2);
    put16(d, 421); put16(d, 0); put16(d, 0);
    put16(d, 7); put16(d, 65535); put16(d, 65535);
    put16(d, 2);
    put16(d, 1); put16(d, 2); put16(d, 5); put16(d, 10); put16(d, 3);
    put16(d, 5); put16(d, 253); put16(d, 2); put16(d, 254); put16(d, 0); put16(d, 0x7FFE);
    put16(d, 0); put16(d, 0); put16(d, 1); put16(d, 1); put16(d, 1);
    put16(d, 1); put16(d, 300);
    put16(d, 0xBEEF); put16(d, 62);
    put16(d, 2);
    put16(d, 1); put16(d, 1); put32(d, 0);
    put16(d, 0); put16(d, 2); put32(d, 0x80000001u); put32(d, 0xFFFFFFFFu);
    for (double p : l.waypoints[1].probabilities) put_double(d, p);
    put16(d, 0xFFFF);
    return d;
}

// The original's bytes as the canonical form has them: every byte after the end of the description and of each name zero, nothing after the egg word
// (an independent oracle: lvl_bytes.hpp's walker finds the parts, not the writer)
std::vector<uint8_t> zero_the_leftovers(const std::vector<uint8_t>& d) {
    Spots s;
    if (!locate(d, s)) return {};
    std::vector<uint8_t> out(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(s.final_word + 2));
    auto clear_after_end = [&out](size_t from, size_t field) {
        size_t i = from;
        while (i < from + field && out[i] != 0) ++i;
        for (; i < from + field; ++i) out[i] = 0;
    };
    clear_after_end(10, 30);
    const size_t names = static_cast<size_t>(rd16(d, 40)) + 1;
    for (size_t i = 0; i < names; ++i) clear_after_end(42 + 11 * i, 11);
    return out;
}

bool loads_same_as(const std::vector<uint8_t>& bytes, const LevelData& level, std::string* why = nullptr) {
    LevelData back;
    if (!back.load_from_memory(bytes.data(), bytes.size())) {
        if (why) *why = "does not load";
        return false;
    }
    return same_level(level, back, why);
}

bool has_kind(const std::vector<RewriteChange>& changes, RewriteChangeKind kind) {
    for (const RewriteChange& c : changes) {
        if (c.kind == kind) return true;
    }
    return false;
}

const RewriteChange* change_of(const std::vector<RewriteChange>& changes, RewriteChangeKind kind) {
    for (const RewriteChange& c : changes) {
        if (c.kind == kind) return &c;
    }
    return nullptr;
}

// Everything a hostile file must give: the reader ends with a level or a Fatal finding that says why (never a crash, never an empty reason), within the heap bound;
// a file that loads is written (the reader's levels are all the writer's), reads back the same, and every byte of the rewrite that differs is accounted for
void hostile(const uint8_t* data, size_t size, const std::string& what) {
    LevelData level;
    LevelValidation report;
    bool loaded = false;
    const size_t read_peak = peak_of([&] { loaded = level.load_from_memory(data, size, &report); });
    CHECK_MSG(read_peak <= reader_bound(size), what + ": the reader held " + std::to_string(read_peak) + " bytes for " + std::to_string(size));
    if (!loaded) {
        CHECK_MSG(report.first_fatal() != nullptr && !report.reason().empty(), what + ": a file that does not load says why");
        CHECK_MSG(is_empty_level(level), what + ": nothing of it is left");
        return;
    }
    CHECK_MSG(report.parsed, what);
    LevelWriteResult w;
    const size_t write_peak = peak_of([&] { w = write_level(level); });
    CHECK_MSG(w.ok, what + ": a level the reader made is written: " + w.error);
    if (!w.ok) return;
    CHECK_MSG(write_peak <= writer_bound(w.bytes.size()), what + ": the writer held " + std::to_string(write_peak) + " bytes for " + std::to_string(w.bytes.size()));
    std::string why;
    CHECK_MSG(loads_same_as(w.bytes, level, &why), what + ": " + why);
    for (const RewriteChange& c : describe_rewrite(data, size, level, w.bytes)) CHECK_MSG(c.kind != RewriteChangeKind::Unexplained, what + ": " + c.message);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W1 The canonical file
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void w1_canonical_file() {
    run_case("W1.1 An Original Map Is Written As Its Own Bytes Where The Original Reads Them: The Leftovers After The End Of Each Name Are Zero, Nothing Else Moves", [] {
        for (const char* name : kOriginals) {
            const std::vector<uint8_t> source = read_map(maps_dir() + "/" + name);
            LevelData level;
            CHECK_MSG(level.load_from_memory(source.data(), source.size()), name);
            const LevelWriteResult w = write_level(level);
            CHECK_MSG(w.ok, std::string(name) + ": " + w.error);
            const std::vector<uint8_t> expected = zero_the_leftovers(source);
            CHECK_MSG(!expected.empty() && w.bytes == expected, name);
            CHECK_MSG(w.bytes.size() == source.size(), std::string(name) + ": nothing after the egg word in an original");
            CHECK_MSG(w.bytes != source, std::string(name) + ": every original has leftovers after its names (\".\\0\\0\\0.\\\\level\" in TINY)");
        }
    });

    run_case("W1.2 Every Field Of A Level Is In Its Place: The Bytes Written Out By Hand In The Order Of docs/MAP_FORMAT.md (Rows First, A NaN's Bits, An 11-Byte Name)", [] {
        const LevelData l = every_field();
        const LevelWriteResult w = write_level(l);
        CHECK_MSG(w.ok, w.error);
        const std::vector<uint8_t> expected = every_field_bytes();
        CHECK(w.bytes == expected);
        CHECK(w.bytes.size() == 303u);
        CHECK(rd32(w.bytes, 97) == 2u && rd32(w.bytes, 101) == 3u);                // the number of rows, then the number of columns
        CHECK(rd16(w.bytes, 105 + 6 * 5) == 105u);                                  // cell (row 1, column 2) is entry 5 of layer 1
        std::string why;
        CHECK_MSG(loads_same_as(w.bytes, l, &why), why);
        LevelData back;
        CHECK(back.load_from_memory(w.bytes.data(), w.bytes.size()));
        uint64_t nan_bits = 0;
        std::memcpy(&nan_bits, &back.waypoints[1].probabilities[2], sizeof nan_bits);
        CHECK(nan_bits == 0x7FF8DEADBEEF0001ull);                                  // the payload of a NaN survives
        CHECK(std::signbit(back.waypoints[1].probabilities[1]));                   // and the sign of a zero
    });

    run_case("W1.3 The Parts Of A Written File Are Where The Loader Walks (An Independent Walker): Blocks 1, 2 And 4 And The Egg Word Of Each Original", [] {
        for (const char* name : kOriginals) {
            LevelData level;
            CHECK(level.load_from_file(maps_dir() + "/" + name));
            const LevelWriteResult w = write_level(level);
            Spots s;
            CHECK_MSG(w.ok && locate(w.bytes, s), name);
            CHECK(s.rows == level.height && s.columns == level.width);
            CHECK(s.b1 == level.anthill_spawns.size() && s.b2 == level.food_schedules.size() && s.b4 == level.waypoints.size());
            CHECK(s.final_word + 2 == w.bytes.size());
            CHECK(rd16(w.bytes, s.final_word) == level.boundary_param);
        }
    });

    run_case("W1.4 The Mode Is Written As The Level Holds It (The Original Never Reads It), The Version Is 8, And A Level From Any Mode Reads Back The Same", [] {
        for (uint32_t mode : {1u, 0u, 4631u, 0xFFFFFFFFu}) {
            LevelData l = every_field();
            l.game_mode = mode;
            const LevelWriteResult w = write_level(l);
            CHECK(w.ok && rd32(w.bytes, 0) == 8u && rd32(w.bytes, 4) == mode);
            CHECK(loads_same_as(w.bytes, l));
        }
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W2 What the writer refuses
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void w2_refusals() {
    run_case("W2.1 A Level That No Reader Can Have Made Is Refused With A Sentence That Says What, And No Byte", [] {
        struct Bad {
            const char* words;
            std::function<void(LevelData&)> make;
        };
        const std::vector<Bad> bad = {
            {"version 9", [](LevelData& l) { l.version = 9; }},
            {"version 0", [](LevelData& l) { l.version = 0; }},
            {"0 rows x 3 columns: a map needs at least one cell", [](LevelData& l) { l.height.val = 0; l.layer1_terrain.clear(); l.layer2_interactive.clear(); }},
            {"2 rows x 0 columns", [](LevelData& l) { l.width.val = 0; l.layer1_terrain.clear(); l.layer2_interactive.clear(); }},
            {"257 rows x 3 columns, more than the 256 x 256 allowed here", [](LevelData& l) {
                 l.height.val = 257;
                 l.layer1_terrain.resize(257 * 3);
                 l.layer2_interactive.resize(257 * 3);
             }},
            {"the layers hold 5 and 6 cells", [](LevelData& l) { l.layer1_terrain.pop_back(); }},
            {"the layers hold 6 and 7 cells", [](LevelData& l) { l.layer2_interactive.push_back(MapCell{}); }},
            {"the description has 31 bytes", [](LevelData& l) { l.description += "!"; }},
            {"the description holds a zero byte (at 3)", [](LevelData& l) { l.description = std::string("abc\0def", 7); }},
            {"the dictionary is empty", [](LevelData& l) { l.tile_dictionary.clear(); l.tile_type_count = 0; }},
            {"the dictionary has 1345 names, more than the 1344 allowed here", [](LevelData& l) { l.tile_dictionary.assign(1345, "."); l.tile_type_count = 1344; }},
            {"tile_type_count is 9 but the dictionary has 5 names", [](LevelData& l) { l.tile_type_count = 9; }},
            {"dictionary name #1 (\"abcdefghijkl\") has 12 bytes, a name holds 11", [](LevelData& l) { l.tile_dictionary[1] = "abcdefghijkl"; }},
            {"dictionary name #2 holds a zero byte", [](LevelData& l) { l.tile_dictionary[2] = std::string("ab\0c", 4); }},
            {"block 1 record #1 (tile 421, row 0, column 0) says team 0, the team of its tile is 255", [](LevelData& l) { l.anthill_spawns[1].team_id = 0; }},
            {"block 1 record #0 (tile 152, row 1, column 2) says team 0, the team of its tile is 3", [](LevelData& l) { l.anthill_spawns[0].team_id = 0; }},
            {"block 1 has 65536 records, a file holds at most 65,535", [](LevelData& l) { l.anthill_spawns.assign(65536, AnthillSpawn{0, 0, 0, 255}); }},
            {"food pile #1 (row 0, column 0) has no stages: the original stops reading block 2 at such a pile", [](LevelData& l) { l.food_schedules[1].variants.clear(); }},
            {"food pile #0 has 65536 stages", [](LevelData& l) { l.food_schedules[0].variants.assign(65536, FoodItemVariant{1, 253}); }},
            {"block 2 has 65536 food piles", [](LevelData& l) { l.food_schedules.assign(65536, l.food_schedules[1]); }},
            {"waypoint #0 (row 1, column 1) has no trigger (flag 0) but holds an interval or chances", [](LevelData& l) { l.waypoints[0].param = 5; }},
            {"waypoint #0 (row 1, column 1) has no trigger (flag 0) but holds an interval or chances", [](LevelData& l) { l.waypoints[0].probabilities[4] = -0.0; }},
            {"block 4 has 65536 waypoints", [](LevelData& l) { l.waypoints.assign(65536, Waypoint{}); }},
        };
        for (const Bad& b : bad) {
            LevelData l = every_field();
            b.make(l);
            const LevelWriteResult w = write_level(l);
            CHECK_MSG(!w.ok && w.bytes.empty(), b.words);
            CHECK_MSG(w.error.find(b.words) != std::string::npos, std::string(b.words) + " / " + w.error);
        }
        CHECK(write_level(every_field()).ok);                                       // (the level they all start from is written)
    });

    run_case("W2.2 The Limits Of A New Map, 10,000 Cells, 130 A Side, 144 KB: 100 x 100 With The Largest Dictionary And 12,584 Bytes Of Blocks Fits, So Does 40 x 129; 131 A Side, 10,100 Cells Or One More Waypoint Do Not", [] {
        const std::vector<std::string> names(1344, "n");
        const LevelLimits new_map = LevelLimits::for_new_maps();
        // a blank 100 x 100 map: 42 + 486 x 11 + 8 + 10,000 x 12 + 12 bytes
        const LevelData blank = blank_level(100, 100, names);
        const LevelWriteResult written = write_level(blank, new_map);
        CHECK_MSG(written.ok && written.bytes.size() == 125408u, written.error);
        // with the largest dictionary, 1,344 names: 134,846 bytes; 242 waypoints with a trigger (52 bytes each) fill the 147,456 bytes to 147,430, one more is over
        LevelData full = blank;
        full.tile_dictionary.assign(1344, "n");
        full.tile_type_count = 1343;
        const LevelWriteResult largest = write_level(full, new_map);
        CHECK_MSG(largest.ok && largest.bytes.size() == 134846u, largest.error);
        Waypoint trigger;
        trigger.flag = 1;
        trigger.param = 5;
        trigger.probabilities = {1.0, 0.0, 0.0, 0.0, 0.0};
        full.waypoints.assign(242, trigger);
        const LevelWriteResult filled = write_level(full, new_map);
        CHECK_MSG(filled.ok && filled.bytes.size() == 147430u, filled.error);
        full.waypoints.push_back(trigger);
        const LevelWriteResult over = write_level(full, new_map);
        CHECK(!over.ok && over.error.find("the file would have 147482 bytes, more than the 147456 allowed here") != std::string::npos);
        CHECK(write_level(full).ok);
        // the grid: Bomb Races's 40 x 129 fits; 131 a side or 10,100 cells do not
        CHECK(write_level(blank_level(40, 129, names), new_map).ok);
        CHECK(write_level(blank_level(130, 76, names), new_map).ok);
        const LevelWriteResult side = write_level(blank_level(131, 1, names), new_map);
        CHECK(!side.ok && side.error.find("131 rows x 1 columns, more than the 130 x 130 allowed here") != std::string::npos);
        const LevelWriteResult cells = write_level(blank_level(101, 100, names), new_map);
        CHECK(!cells.ok && cells.error.find("101 rows x 100 columns, 10100 cells, more than the 10000 allowed here") != std::string::npos);
        CHECK(write_level(blank_level(131, 1, names)).ok && write_level(blank_level(101, 100, names)).ok);   // the game's limits take them
        LevelLimits few_names = LevelLimits{};
        few_names.max_dictionary_names = 100;
        const LevelWriteResult dictionary = write_level(blank_level(5, 5, names), few_names);  // (the blank map's dictionary has 486 names)
        CHECK(!dictionary.ok && dictionary.error.find("the dictionary has 486 names, more than the 100 allowed here") != std::string::npos);
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W3 Random levels
// ------------------------------------------------------------------------------------------------------------------------------------------------------

LevelData random_level(Lcg& r) {
    LevelData l;
    l.version = 8;
    l.game_mode = r.next() ^ (r.next() << 16);
    l.default_minutes = r.word();
    const size_t desc = r.below(31);
    for (size_t i = 0; i < desc; ++i) l.description += static_cast<char>(1 + r.below(255));
    const size_t names = 1 + r.below(40);
    for (size_t i = 0; i < names; ++i) {
        std::string n;
        const size_t len = r.below(12);
        for (size_t k = 0; k < len; ++k) n += static_cast<char>(1 + r.below(255));
        l.tile_dictionary.push_back(n);
    }
    l.tile_type_count = static_cast<uint16_t>(names - 1);
    l.height.val = 1 + r.below(12);
    l.width.val = 1 + r.below(12);
    const size_t cells = static_cast<size_t>(l.height.val) * l.width.val;
    for (size_t i = 0; i < cells; ++i) {
        l.layer1_terrain.push_back(MapCell{r.word(), r.word(), r.word()});
        l.layer2_interactive.push_back(MapCell{r.below(4) == 0 ? LVL_EMPTY_TILE : r.word(), r.word(), r.word()});
    }
    const size_t records = r.below(8);
    for (size_t i = 0; i < records; ++i) {
        AnthillSpawn sp;
        sp.tile_id = r.below(2) == 0 ? static_cast<uint16_t>(152 + r.below(4)) : r.word();
        sp.y = r.word();
        sp.x = r.word();
        const int team = start_marker_team_of_id(sp.tile_id);
        sp.team_id = team >= 0 ? static_cast<uint8_t>(team) : uint8_t{255};
        l.anthill_spawns.push_back(sp);
    }
    const size_t piles = r.below(5);
    for (size_t i = 0; i < piles; ++i) {
        FoodSchedule f;
        f.y = r.word();
        f.x = r.word();
        f.initial_delay = r.word();
        f.respawn_interval = r.word();
        const size_t stages = 1 + r.below(4);
        for (size_t k = 0; k < stages; ++k) f.variants.push_back(FoodItemVariant{r.word(), r.word()});
        l.food_schedules.push_back(f);
    }
    l.ambient_flag = r.word();
    l.ambient_tile_or_sound = r.word();
    const size_t points = r.below(5);
    for (size_t i = 0; i < points; ++i) {
        Waypoint w;
        w.y = r.word();
        w.x = r.word();
        if (r.below(3) != 0) {
            w.flag = r.next() | 1u;
            w.param = r.next();
            for (double& p : w.probabilities) p = from_bits((static_cast<uint64_t>(r.next()) << 40) ^ (static_cast<uint64_t>(r.next()) << 16) ^ r.next());
        }
        l.waypoints.push_back(w);
    }
    l.boundary_param = r.word();
    return l;
}

void w3_random_levels() {
    run_case("W3.1 2,000 Random Levels (Any Words, Odd Bytes In The Texts, NaN Chances, Records Outside The Grid): Written, Read Back The Same, Written Again The Same Bytes", [] {
        Lcg r(0xC0FFEEu);
        int written = 0;
        for (int i = 0; i < 2000; ++i) {
            const LevelData l = random_level(r);
            const LevelWriteResult w = write_level(l);
            CHECK_MSG(w.ok, "level " + std::to_string(i) + ": " + w.error);
            if (!w.ok) continue;
            ++written;
            LevelData back;
            CHECK(back.load_from_memory(w.bytes.data(), w.bytes.size()));
            std::string why;
            CHECK_MSG(same_level(l, back, &why), "level " + std::to_string(i) + ": " + why);
            const LevelWriteResult again = write_level(back);
            CHECK(again.ok && again.bytes == w.bytes);
            CHECK(describe_rewrite(w.bytes.data(), w.bytes.size(), back, w.bytes).empty());
        }
        CHECK(written == 2000);
    });

    run_case("W3.2 same_level Sees Every Field: A Copy Is The Same Level, And A Change Of Any One Field (The Bits Of A Chance Included) Is A Difference That It Names", [] {
        const LevelData base = every_field();
        std::string why;
        CHECK(same_level(base, LevelData(base), &why) && why.empty());
        struct Change {
            const char* named;
            std::function<void(LevelData&)> make;
        };
        const std::vector<Change> changes = {
            {"version", [](LevelData& l) { l.version = 7; }},
            {"mode", [](LevelData& l) { l.game_mode = 1; }},
            {"minutes", [](LevelData& l) { l.default_minutes = 1; }},
            {"description", [](LevelData& l) { l.description = "x"; }},
            {"tile_type_count", [](LevelData& l) { l.tile_type_count = 1; }},
            {"dictionary name #4", [](LevelData& l) { l.tile_dictionary[4] = ".."; }},
            {"dictionary: 5 / 6 names", [](LevelData& l) { l.tile_dictionary.push_back("."); }},
            {"grid", [](LevelData& l) { l.width.val = 6; l.height.val = 1; }},
            {"layer 1, row 1, column 2: (105, 205, 305) / (105, 205, 306)", [](LevelData& l) { l.layer1_terrain[5].properties = 306; }},
            {"layer 2, row 0, column 1", [](LevelData& l) { l.layer2_interactive[1].flags = 0; }},
            {"block 1 record #2", [](LevelData& l) { l.anthill_spawns[2].y = 4; }},
            {"block 1: 3 / 2 records", [](LevelData& l) { l.anthill_spawns.pop_back(); }},
            {"food pile #0, stage #2", [](LevelData& l) { l.food_schedules[0].variants[2].tile_id = 255; }},
            {"food pile #1", [](LevelData& l) { l.food_schedules[1].respawn_interval = 2; }},
            {"block 3", [](LevelData& l) { l.ambient_flag = 0; }},
            {"waypoint #1", [](LevelData& l) { l.waypoints[1].param = 1; }},
            {"(the chances differ)", [](LevelData& l) { l.waypoints[1].probabilities[1] = 0.0; }},       // -0.0 to +0.0: equal as numbers, not as bits
            {"(the chances differ)", [](LevelData& l) { l.waypoints[1].probabilities[2] = from_bits(0x7FF8DEADBEEF0002ull); }},
            {"eggs", [](LevelData& l) { l.boundary_param = 0; }},
        };
        for (const Change& c : changes) {
            LevelData other = base;
            c.make(other);
            CHECK_MSG(!same_level(base, other, &why), c.named);
            CHECK_MSG(why.find(c.named) != std::string::npos, std::string(c.named) + " / " + why);
        }
        LevelData noted = base;                                                     // the reader's findings are no part of the map
        noted.load_notes.push_back(LevelProblem{});
        CHECK(same_level(base, noted));
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W4 Hostile files
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void w4_hostile_files() {
    const std::vector<uint8_t> tiny = read_map(maps_dir() + "/TINY.LVL");
    const std::vector<uint8_t> small = read_map(maps_dir() + "/SMALL.LVL");
    Spots t;
    Spots s;
    const bool located = locate(tiny, t) && locate(small, s);

    run_case("W4.1 Every Cut Of TINY And SMALL (Every Length From 0 To The Whole File): A Finding That Says Where Before Block 4, The Level Read So Far From Block 4 On", [&] {
        CHECK(located);
        for (const std::vector<uint8_t>* file : {&tiny, &small}) {
            const std::vector<uint8_t>& d = *file;
            Spots spots;
            locate(d, spots);
            int loaded = 0;
            for (size_t n = 0; n <= d.size(); ++n) {
                LevelData level;
                LevelValidation v;
                const bool ok = level.load_from_memory(d.data(), n, &v);
                CHECK_MSG(ok == (n >= spots.b4_count), "a cut at " + std::to_string(n));
                if (!ok) {
                    CHECK_MSG(has_fatal(v, n == 0 ? LevelProblemKind::Unreadable : LevelProblemKind::Truncated), "a cut at " + std::to_string(n));
                    continue;
                }
                ++loaded;
                hostile(d.data(), n, "a cut at " + std::to_string(n));
            }
            CHECK(loaded == static_cast<int>(d.size() - spots.b4_count + 1));
        }
    });

    run_case("W4.2 Bytes Changed At Random In Each Original (500 Files Each, Fixed Seeds, 1 To 8 Bytes): No Crash, A Reason For Each Refusal, Each Level Written And Read Back", [] {
        for (const char* name : kOriginals) {
            const std::vector<uint8_t> d = read_map(maps_dir() + "/" + name);
            Lcg r(0xF00Du ^ static_cast<uint32_t>(d.size()));
            for (int i = 0; i < 500; ++i) {
                std::vector<uint8_t> m = d;
                const uint32_t flips = 1 + r.below(8);
                for (uint32_t k = 0; k < flips; ++k) m[r.below(static_cast<uint32_t>(m.size()))] = static_cast<uint8_t>(r.next());
                hostile(m.data(), m.size(), std::string(name) + " flip " + std::to_string(i));
            }
        }
    });

    run_case("W4.3 The Counts Of A File Set To 0, 1, 0x7FFF, 0x8000 And 0xFFFF (The Dictionary, The Grid, Blocks 1, 2 And 4, A Pile's Stages): Refused Or Read, Never Allocated Ahead", [&] {
        CHECK(located);
        if (!located) return;
        struct Word {
            const char* what;
            size_t at;
        };
        const std::vector<Word> words = {
            {"the dictionary's count", 40}, {"the rows", t.dims}, {"the rows' high word", t.dims + 2}, {"the columns", t.dims + 4}, {"block 1's count", t.b1_count},
            {"block 2's count", t.b2_count}, {"a pile's stages", t.food[3] + 8}, {"block 4's count", t.b4_count},
        };
        for (const Word& w : words) {
            for (uint32_t value : {0u, 1u, 0x7FFFu, 0x8000u, 0xFFFFu}) {
                std::vector<uint8_t> m = tiny;
                wr16(m, w.at, value);
                hostile(m.data(), m.size(), std::string(w.what) + " = " + std::to_string(value));
            }
        }
        // the same claims in a file of 85 bytes (one name, one cell): nothing the size of the claim is held, a few kilobytes at the most
        const std::vector<uint8_t> mini = flat_map(1, 1, 1, 1, 1);
        Spots ms;
        CHECK(locate(mini, ms) && mini.size() == 85u);
        for (const size_t at : {size_t{40}, ms.dims, ms.dims + 4, ms.b1_count, ms.b2_count, ms.b4_count}) {
            std::vector<uint8_t> m = mini;
            wr16(m, at, 0xFFFF);
            LevelData level;
            LevelValidation v;
            const size_t held = peak_of([&] { level.load_from_memory(m.data(), m.size(), &v); });
            CHECK_MSG(held < 16u * 1024u, "a count of 65,535 at byte " + std::to_string(at) + ": " + std::to_string(held) + " bytes");
            CHECK(!v.reason().empty() || v.playable);
            hostile(m.data(), m.size(), "a count of 65,535 at byte " + std::to_string(at));
        }
        // a grid of 65,535 x 65,535 claimed by a file of the size of TINY: a short file, no allocation of four billion cells (what is held is its 670 names)
        std::vector<uint8_t> m = tiny;
        wr32(m, t.dims, 0xFFFF);
        wr32(m, t.dims + 4, 0xFFFF);
        LevelValidation v;
        LevelData level;
        const size_t held = peak_of([&] { level.load_from_memory(m.data(), m.size(), &v); });
        CHECK(has_fatal(v, LevelProblemKind::Truncated) && mentions(v, "layers"));
        CHECK_MSG(held <= reader_bound(m.size()) && held < 64u * 1024u, std::to_string(held));
    });

    run_case("W4.4 A Start Marker And A Food Pile Outside The Grid, Plants And Waypoints Outside It: Read As The Original Reads Them, Written And Read Back, The Findings Kept", [&] {
        CHECK(located);
        if (!located) return;
        std::vector<uint8_t> m = small;
        const size_t blue = record_of_tile(m, s, 153);
        const size_t flower = record_of_tile(m, s, 421);
        CHECK(blue != 0 && flower != 0);
        wr16(m, blue + 4, 77);                                                       // the blue team's marker at column 77 of 40
        wr16(m, flower + 2, 100);
        wr16(m, s.food[0], 100);                                                     // a food pile at row 100
        wr16(m, s.waypoints[0], 999);                                                // a waypoint at row 999
        hostile(m.data(), m.size(), "outside the grid");
        LevelData level;
        CHECK(level.load_from_memory(m.data(), m.size()));
        const LevelWriteResult w = write_level(level);
        CHECK(w.ok);
        const LevelValidation all = LVLParser::check_memory(w.bytes.data(), w.bytes.size(), 0x0F);
        CHECK(!all.playable && has_fatal(all, LevelProblemKind::StartMarkerOutsideGrid));   // the rewrite is the same map: refused for a roster with blue
        CHECK(all.has(LevelProblemKind::FoodOutsideGrid) && all.has(LevelProblemKind::ObjectOutsideGrid));
        CHECK(LVLParser::check_memory(w.bytes.data(), w.bytes.size(), 0x0B).playable);     // and played without blue
    });

    run_case("W4.5 A Food Pile Without Stages Ends Block 2 (FoodBlockEndedEarly): The Rewrite Holds The Piles Read Before It And Is Whole, The Difference Is Named", [&] {
        CHECK(located);
        if (!located) return;
        std::vector<uint8_t> m = tiny;
        wr16(m, t.food[2] + 8, 0);                                                   // the third pile has no stages
        hostile(m.data(), m.size(), "a pile without stages");
        LevelData level;
        LevelValidation v;
        CHECK(level.load_from_memory(m.data(), m.size(), &v) && v.has(LevelProblemKind::FoodBlockEndedEarly));
        CHECK(level.food_schedules.size() == 2u);
        const LevelWriteResult w = write_level(level);
        CHECK(w.ok);
        LevelValidation again;
        LevelData back;
        CHECK(back.load_from_memory(w.bytes.data(), w.bytes.size(), &again) && !again.has(LevelProblemKind::FoodBlockEndedEarly) && back.food_schedules.size() == 2u);
        const std::vector<RewriteChange> changes = describe_rewrite(m.data(), m.size(), level, w.bytes);
        const RewriteChange* r = change_of(changes, RewriteChangeKind::Restructured);
        CHECK(r != nullptr && r->offset == t.b2_count && r->message.find("block 2 of the file ends early") != std::string::npos);
    });

    run_case("W4.6 Odd Waypoints (A Trigger With NaN, Infinite And Negative Chances, The Largest Interval, A Flag Of Any Bits): Kept To The Bit", [&] {
        CHECK(located);
        if (!located) return;
        std::vector<uint8_t> m = small;
        const size_t w0 = s.waypoints[0];
        wr32(m, w0 + 4, 0xDEADBEEFu);
        wr32(m, w0 + 8, 0xFFFFFFFFu);
        for (size_t k = 0; k < 5; ++k) wr32(m, w0 + 12 + 8 * k + 4, k == 0 ? 0x7FF00000u : k == 1 ? 0xFFF80000u : k == 2 ? 0x7FF80000u : 0x80000000u);
        hostile(m.data(), m.size(), "odd waypoints");
        LevelData level;
        CHECK(level.load_from_memory(m.data(), m.size()));
        const LevelWriteResult w = write_level(level);
        CHECK(w.ok && std::memcmp(w.bytes.data() + w0, m.data() + w0, 52) == 0);   // the record's 52 bytes as they were
    });

    run_case("W4.7 Other Versions (0, 7, 9, 0xFFFFFFFF) And Grids Without A Cell (0 x 0, 31 x 0, 0 x 31) Are Refused By The Reader And Have No Level For The Writer", [&] {
        for (uint32_t version : {0u, 7u, 9u, 0xFFFFFFFFu}) {
            std::vector<uint8_t> m = tiny;
            wr32(m, 0, version);
            hostile(m.data(), m.size(), "version " + std::to_string(version));
            LevelValidation v;
            LevelData level;
            CHECK(!level.load_from_memory(m.data(), m.size(), &v) && has_fatal(v, LevelProblemKind::WrongVersion) && mentions(v, "wrong version"));
        }
        for (const auto& dims : {std::pair<uint32_t, uint32_t>{0, 0}, std::pair<uint32_t, uint32_t>{31, 0}, std::pair<uint32_t, uint32_t>{0, 31}}) {
            const std::vector<uint8_t> d = flat_map(4, dims.first, dims.second, 0, 0);
            hostile(d.data(), d.size(), "an empty grid");
            LevelValidation v;
            LevelData level;
            CHECK(!level.load_from_memory(d.data(), d.size(), &v) && has_fatal(v, LevelProblemKind::GridEmpty));
        }
    });

    run_case("W4.8 What The Reader And The Writer Hold Is Bounded By Their Input: A File Of 8,000 Waypoints Without Trigger (The Dearest Bytes), The Largest Original", [] {
        // a 1 x 1 map whose block 4 holds 8,000 records of 8 bytes: the level holds 56 bytes for each
        std::vector<uint8_t> d = flat_map(2, 1, 1, 1, 1);
        d.resize(d.size() - 4);                                                      // up to block 4's count
        put16(d, 8000);
        for (int i = 0; i < 8000; ++i) { put16(d, 0); put16(d, 0); put32(d, 0); }
        put16(d, 3);
        LevelData level;
        size_t held = peak_of([&] { level.load_from_memory(d.data(), d.size()); });
        CHECK(level.waypoints.size() == 8000u);
        CHECK_MSG(held <= reader_bound(d.size()) && held >= 7 * d.size() / 2, std::to_string(held) + " bytes for " + std::to_string(d.size()));
        LevelWriteResult w;
        held = peak_of([&] { w = write_level(level); });
        CHECK_MSG(w.ok && held <= writer_bound(w.bytes.size()), std::to_string(held));
        const std::vector<uint8_t> treasure = read_map(maps_dir() + "/TREASURE.LVL");
        LevelData big;
        held = peak_of([&] { big.load_from_memory(treasure.data(), treasure.size()); });
        CHECK_MSG(held <= reader_bound(treasure.size()), std::to_string(held));
    });

    run_case("W4.9 With The Limits Of A New Map A 10 MB File Is Refused Before It Is Read (FileTooLarge), And Holds Nothing Of Its Size", [] {
        const std::vector<uint8_t> huge(10u * 1024u * 1024u, 0x08);
        LevelData level;
        LevelValidation v;
        bool ok = true;
        const size_t held = peak_of([&] { ok = level.load_from_memory(huge.data(), huge.size(), &v, LevelLimits::for_new_maps()); });
        CHECK(!ok && has_fatal(v, LevelProblemKind::FileTooLarge) && !v.parsed);
        CHECK_MSG(held < 4u * 1024u, std::to_string(held));
        // from a file: refused before it is read into memory
        const std::string path = (std::filesystem::temp_directory_path() / ("test_lvl_writer_huge_" + std::to_string(huge.size()) + ".lvl")).string();
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(huge.data()), static_cast<std::streamsize>(huge.size()));
        }
        LevelValidation from_file;
        const size_t held_file = peak_of([&] { from_file = LVLParser::check_file(path, 0x0F, LevelLimits::for_new_maps()); });
        CHECK(has_fatal(from_file, LevelProblemKind::FileTooLarge) && !from_file.playable);
        CHECK_MSG(held_file < kSlack, std::to_string(held_file));                  // (the stream's own buffer, nothing of the file's size)
        std::error_code ec;
        std::filesystem::remove(path, ec);
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W5 What a rewrite changes
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void w5_describe_rewrite() {
    const LevelData base = every_field();
    const std::vector<uint8_t> canonical = every_field_bytes();

    run_case("W5.1 A Canonical File Has Nothing To Change; Each Kind Of Leftover Is Named Where It Is, With Its Count", [&] {
        CHECK(describe_rewrite(canonical.data(), canonical.size(), base, canonical).empty());
        // leftovers after two names and after the description (the description is shortened to have a place for them)
        LevelData l = base;
        l.description = "short";
        const std::vector<uint8_t> clean = write_level(l).bytes;
        std::vector<uint8_t> d = clean;
        d[10 + 5 + 1] = 'x';                                                         // after the zero that ends "short"
        d[42 + 11 * 0 + 2] = '\\';                                                   // after "a"
        d[42 + 11 * 4 + 10] = 0xCD;                                                  // after "."
        LevelData read;
        CHECK(read.load_from_memory(d.data(), d.size()) && same_level(read, l));
        std::vector<RewriteChange> c = describe_rewrite(d.data(), d.size(), read, clean);
        CHECK(c.size() == 2u);
        CHECK(c.size() == 2u && c[0].kind == RewriteChangeKind::DescriptionPadding && c[0].offset == 16u);
        CHECK(c.size() == 2u && c[1].kind == RewriteChangeKind::NamePadding && c[1].count == 2u && c[1].offset == 44u);
        CHECK(c.size() == 2u && c[1].message.find("2 of the 5 dictionary names have leftover bytes") != std::string::npos);
        // the grid size's high words and bytes after the egg word
        d = clean;
        wr16(d, 97 + 2, 0x0001);
        wr16(d, 101 + 2, 0xABCD);
        d.insert(d.end(), 206, 0x55);
        CHECK(read.load_from_memory(d.data(), d.size()) && same_level(read, l));
        c = describe_rewrite(d.data(), d.size(), read, clean);
        CHECK(c.size() == 2u && c[0].kind == RewriteChangeKind::GridSizeHighWords && c[0].offset == 99u);
        CHECK(c.size() == 2u && c[1].kind == RewriteChangeKind::TrailingBytes && c[1].count == 206u && c[1].offset == clean.size());
        CHECK(c.size() == 2u && c[1].message == "206 bytes after the egg word are dropped (the original never reads them)");
        // the high word of the columns alone, and of the rows alone
        for (const size_t at : {size_t{101 + 2}, size_t{97 + 2}}) {
            d = clean;
            wr16(d, at, 0x8000);
            CHECK(read.load_from_memory(d.data(), d.size()) && same_level(read, l));
            c = describe_rewrite(d.data(), d.size(), read, clean);
            CHECK_MSG(c.size() == 1u && c[0].kind == RewriteChangeKind::GridSizeHighWords, std::to_string(at));
        }
    });

    run_case("W5.2 A File Cut Inside Block 4 Or Before Its Egg Word Is Restructured From There; Nothing Else Of It Is Named", [&] {
        LevelData l = base;
        const std::vector<uint8_t> clean = write_level(l).bytes;
        struct Cut {
            size_t at;
            size_t from;
            const char* words;
        };
        const Cut cuts[] = {
            {239 + 2 + 4, 239, "the file ends inside block 4"},                      // inside the first waypoint
            {239, 239, "the file ends inside block 4"},                              // before block 4's count
            {301, 301, "the file ends before its egg word"},
            {302, 301, "the file ends before its egg word"},
        };
        for (const Cut& cut : cuts) {
            LevelData read;
            CHECK(read.load_from_memory(clean.data(), cut.at));
            const LevelWriteResult w = write_level(read);
            CHECK(w.ok);
            const std::vector<RewriteChange> c = describe_rewrite(clean.data(), cut.at, read, w.bytes);
            CHECK_MSG(c.size() == 1u && c[0].kind == RewriteChangeKind::Restructured && c[0].offset == cut.from, std::to_string(cut.at));
            CHECK(c.size() == 1u && c[0].message.find(cut.words) != std::string::npos);
        }
    });

    run_case("W5.3 A Byte That The Original Reads And The Rewrite Changed Is Unexplained (A Fault Of The Writer), And So Is A Rewrite Of Another Level", [&] {
        for (size_t at : {size_t{2}, size_t{6}, size_t{12}, size_t{40}, size_t{42}, size_t{97}, size_t{150}, size_t{200}, size_t{300}}) {
            std::vector<uint8_t> forged = canonical;
            forged[at] ^= 0x40;
            const std::vector<RewriteChange> c = describe_rewrite(canonical.data(), canonical.size(), base, forged);
            CHECK_MSG(has_kind(c, RewriteChangeKind::Unexplained), std::to_string(at));
            const RewriteChange* u = change_of(c, RewriteChangeKind::Unexplained);
            CHECK_MSG(u != nullptr && u->offset == at, std::to_string(at));
        }
        LevelData other = base;
        other.boundary_param = 3;
        const std::vector<uint8_t> theirs = write_level(other).bytes;
        CHECK(has_kind(describe_rewrite(canonical.data(), canonical.size(), base, theirs), RewriteChangeKind::Unexplained));
        std::vector<uint8_t> longer = canonical;
        longer.push_back(0);
        CHECK(has_kind(describe_rewrite(canonical.data(), canonical.size(), base, longer), RewriteChangeKind::Unexplained));
        CHECK(has_kind(describe_rewrite(nullptr, 0, base, canonical), RewriteChangeKind::Unexplained));
        for (RewriteChangeKind k : {RewriteChangeKind::NamePadding, RewriteChangeKind::DescriptionPadding, RewriteChangeKind::GridSizeHighWords, RewriteChangeKind::TrailingBytes,
                                    RewriteChangeKind::Restructured, RewriteChangeKind::Unexplained}) {
            CHECK(std::string(to_string(k)) != "?");
        }
    });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W6 The dictionary of a new map and the blank map
// ------------------------------------------------------------------------------------------------------------------------------------------------------

void w6_new_maps() {
    const std::vector<uint8_t> chd = read_map(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
    const std::vector<std::string> names = tile_names_from_chd(chd.data(), chd.size());

    run_case("W6.1 The Tile Names Are Table 4 Of ants.chd: 1,344 Names Of At Most 10 Bytes (485 Is g01a, 245 BLACKHILL); Bytes That Are No Archive Give None", [&] {
        CHECK(names.size() == 1344u);
        if (names.size() != 1344u) return;
        size_t longest = 0;
        for (const std::string& n : names) longest = std::max(longest, n.size());
        CHECK(longest <= 10u);
        CHECK(names[485] == "g01a" && names[245] == "BLACKHILL" && names[152] == "BSTART" && names[62] == "pu_comb");
        CHECK(tile_names_from_chd(nullptr, 0).empty());
        const std::vector<uint8_t> tiny = read_map(maps_dir() + "/TINY.LVL");
        CHECK(tile_names_from_chd(tiny.data(), tiny.size()).empty());
    });

    run_case("W6.2 The Rule Of The Six Original Maps: Their Dictionary Names Exactly The Tiles They Use, With Their Table-4 Names, Up To The Highest; Every Other Entry Is \".\"", [&] {
        for (const char* name : kOriginals) {
            LevelData level;
            CHECK(level.load_from_file(maps_dir() + "/" + name));
            CHECK_MSG(dictionary_of_used_tiles(level, names) == level.tile_dictionary, name);
        }
        LevelData tiny;
        CHECK(tiny.load_from_file(maps_dir() + "/TINY.LVL"));
        const std::vector<uint16_t> used = used_tile_ids(tiny);
        CHECK(used.size() == 93u && used.back() == 669u);
        // a level that uses no tile has one entry; a used id that has no name stays "."; an id past the table names nothing
        LevelData empty = blank_level(2, 2, names);
        for (MapCell& c : empty.layer1_terrain) c.tile_index = LVL_EMPTY_TILE;
        CHECK(dictionary_of_used_tiles(empty, names) == std::vector<std::string>{"."});
        LevelData far = blank_level(1, 1, names);
        far.layer2_interactive[0].tile_index = 1400;
        const std::vector<std::string> d = dictionary_of_used_tiles(far, names);
        CHECK(d.size() == 1401u && d[1400] == "." && d[485] == "g01a");
    });

    run_case("W6.3 A Blank Map: Plain Ground (485) With The Originals' Third Word, Nothing On It, Version 8, Mode 1, 10 Minutes, 6 Eggs; Written, Read Back, Checked", [&] {
        for (const auto& size : {std::pair<uint16_t, uint16_t>{31, 31}, std::pair<uint16_t, uint16_t>{1, 1}, std::pair<uint16_t, uint16_t>{60, 40}}) {
            const LevelData l = blank_level(size.first, size.second, names);
            CHECK(l.version == 8u && l.game_mode == 1u && l.default_minutes == 10u && l.boundary_param == 6u && l.description.empty());
            CHECK(l.height == static_cast<uint32_t>(size.first) && l.width == static_cast<uint32_t>(size.second));
            CHECK(l.tile_dictionary.size() == 486u && l.tile_dictionary[485] == "g01a" && l.tile_type_count == 485u);
            CHECK(std::count(l.tile_dictionary.begin(), l.tile_dictionary.end(), ".") == 485);
            CHECK(l.layer1_terrain.size() == static_cast<size_t>(size.first) * size.second);
            CHECK(l.layer1_terrain[0].tile_index == 485u && l.layer1_terrain[0].flags == 0u && l.layer1_terrain[0].properties == 0xCDCDu);
            CHECK(l.layer2_interactive.back().tile_index == LVL_EMPTY_TILE && l.anthill_spawns.empty() && l.food_schedules.empty() && l.waypoints.empty());
            CHECK(l.ambient_tile_or_sound == LVL_EMPTY_TILE);
            const LevelWriteResult w = write_level(l, LevelLimits::for_new_maps());
            CHECK_MSG(w.ok, w.error);
            CHECK(loads_same_as(w.bytes, l));
            const LevelValidation v = LVLParser::check_memory(w.bytes.data(), w.bytes.size(), 0x0F);
            CHECK(v.playable && v.clean());
        }
        CHECK(!write_level(blank_level(0, 31, names)).ok);                          // no cell: nothing to write
    });
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " The map writer, and the reader and the writer on hostile input\n"
              << "=======================================================\n";
    w1_canonical_file();
    w2_refusals();
    w3_random_levels();
    w4_hostile_files();
    w5_describe_rewrite();
    w6_new_maps();
    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

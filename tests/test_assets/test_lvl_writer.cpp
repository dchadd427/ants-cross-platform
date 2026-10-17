// The writer of the .lvl format (include/ants_assets/lvl_writer.hpp, docs/MAP_FORMAT.md), and the reader and the writer on hostile input:
//   W1 the canonical file: the bytes of the original maps where the original reads them, every field of a level in its place, the parts where the loader walks
//   W2 what the writer refuses (a level that no reader can have made), each with its sentence and no byte
//   W3 random levels: written, read back as the same level, written again as the same bytes; and same_level against every member of a level and of its
//      records, changed one at a time (a walk that names every member, so that the compiler stops it when a member is added)
//   W4 hostile files: cut at every byte, bytes changed at random (fixed seeds), giant counts, objects outside the grid, food without stages, odd waypoints, empty
//      grids, the dearest files: no crash, a finding that says what is wrong, and every file that loads is written and read back the same; the heap that the
//      reader and the writer take at once is at most 7 and 8 times their input and 64 KiB (a counting operator new below)
//   W5 what a rewrite changes in a file (describe_rewrite), kind by kind
//   W6 the dictionary of a new map (the rule of the six original maps, checked on them) and the blank map, which makes no cell of a grid beyond its limits
//   W7 Extended maps (version 9): the layout by hand, read-write-read, the relative anchors, the conversions and their reasons, the limits and the ceiling, a place that
//      reads Classic maps only, hostile headers and fields, every cut, random changes, the heap
// The 540 maps of the library are the round-trip proof's (tests/test_sim/test_map_roundtrip.cpp).
#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"
#include "lvl_bytes.hpp"

#include <algorithm>
#include <chrono>
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
#include <random>
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

// The bounds of docs/MAP_FORMAT.md ("Limits"): what the reader may hold at once for a file of `bytes` bytes, 7 times the file (a waypoint without trigger is 8
// bytes of file and 56 of memory, and nothing of a file costs more), and what the writer may hold for a file of `bytes` bytes that it writes, 8 times (the bytes,
// and the level that it reads back from them); 64 KiB on top of each for what does not grow with the file (the findings and their sentences)
constexpr size_t kSlack = 64 * 1024;
size_t reader_bound(size_t bytes) { return kSlack + 7 * bytes; }
size_t writer_bound(size_t bytes) { return kSlack + 8 * bytes; }

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

// Refused by a precondition of the writer, with its own sentence (`words`), and no byte: not by the read-back check, whose refusals say "a fault of the writer"
bool refused_with(const LevelWriteResult& w, const std::string& words) {
    return !w.ok && w.bytes.empty() && w.error.find(words) != std::string::npos && w.error.find("a fault of the writer") == std::string::npos;
}

// A file in the system's temporary folder that no other run of this program uses at the same time: the name holds the clock and a random number, and a name
// that is there already is not taken
std::string scratch_path(const std::string& stem) {
    std::random_device random;
    const auto clock = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::path path;
    for (int attempt = 0; attempt < 100; ++attempt) {
        path = std::filesystem::temp_directory_path(ec) / (stem + "_" + std::to_string(clock) + "_" + std::to_string(random()) + ".lvl");
        if (!std::filesystem::exists(path, ec)) break;
    }
    return path.string();
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
            {"block 1 record #0 (tile 152, row 1, column 2) says team 255, the team of its tile is 3", [](LevelData& l) { l.anthill_spawns[0].team_id = 255; }},
            {"block 1 record #1 (tile 421, row 0, column 0) says team 7, the team of its tile is 255", [](LevelData& l) { l.anthill_spawns[1].team_id = 7; }},
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
            CHECK_MSG(refused_with(w, b.words), std::string(b.words) + " / " + w.error);
        }
        CHECK(write_level(every_field()).ok);                                       // (the level they all start from is written)
    });

    run_case("W2.2 The Limits Of A New Map, 10,000 Cells, 130 A Side, 144 KB Of Canonical File: Each Is Written At Exactly The Limit (100 x 100 With The Largest Dictionary And 12,610 Bytes Of Blocks, 130 Rows, 130 Columns) And Refused Past It", [] {
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
        // exactly the limit: a food pile of four stages (10 + 4 x 4 bytes) more makes 147,456 bytes, which are written; so are they under a limit of their size,
        // and one byte less refuses them
        LevelData exact = full;
        FoodSchedule pile;
        pile.y = 50;
        pile.x = 50;
        pile.initial_delay = 10;
        pile.respawn_interval = 1;
        pile.variants.assign(4, FoodItemVariant{10, 253});
        exact.food_schedules.push_back(pile);
        const LevelWriteResult at_limit = write_level(exact, new_map);
        CHECK_MSG(at_limit.ok && at_limit.bytes.size() == 147456u, at_limit.error);
        LevelLimits by_size;
        by_size.max_write_bytes = 147456;
        CHECK(write_level(exact, by_size).ok);
        by_size.max_write_bytes = 147455;
        const LevelWriteResult one_less = write_level(exact, by_size);
        CHECK_MSG(refused_with(one_less, "the file would have 147456 bytes, more than the 147455 allowed here"), one_less.error);
        // the read limit holds the writer too: what it writes reads back with the same limits
        LevelLimits by_read;
        by_read.max_read_bytes = 147456;
        CHECK(write_level(exact, by_read).ok);
        by_read.max_read_bytes = 147455;
        const LevelWriteResult unreadable = write_level(exact, by_read);
        CHECK_MSG(refused_with(unreadable, "the file would have 147456 bytes, more than the 147455 allowed here"), unreadable.error);
        full.waypoints.push_back(trigger);
        const LevelWriteResult over = write_level(full, new_map);
        CHECK_MSG(refused_with(over, "the file would have 147482 bytes, more than the 147456 allowed here"), over.error);
        CHECK(write_level(full).ok);
        // the grid: Bomb Races's 40 x 129 fits, and so do 130 rows, 130 columns (9,880 cells each) and 100 x 100 (10,000 cells); 131 a side or 10,010 cells do not
        CHECK(write_level(blank_level(40, 129, names), new_map).ok);
        CHECK(write_level(blank_level(130, 76, names), new_map).ok);
        CHECK(write_level(blank_level(76, 130, names), new_map).ok);
        const LevelWriteResult side = write_level(blank_level(131, 1, names), new_map);
        CHECK_MSG(refused_with(side, "131 rows x 1 columns, more than the 130 x 130 allowed here"), side.error);
        const LevelWriteResult side_columns = write_level(blank_level(1, 131, names), new_map);
        CHECK_MSG(refused_with(side_columns, "1 rows x 131 columns, more than the 130 x 130 allowed here"), side_columns.error);
        const LevelWriteResult cells = write_level(blank_level(101, 100, names), new_map);
        CHECK_MSG(refused_with(cells, "101 rows x 100 columns, 10100 cells, more than the 10000 allowed here"), cells.error);
        const LevelWriteResult cells_wide = write_level(blank_level(77, 130, names), new_map);
        CHECK_MSG(refused_with(cells_wide, "77 rows x 130 columns, 10010 cells, more than the 10000 allowed here"), cells_wide.error);
        CHECK(write_level(blank_level(131, 1, names)).ok && write_level(blank_level(101, 100, names)).ok);   // the game's limits take them
        LevelLimits few_names = LevelLimits{};
        few_names.max_dictionary_names = 100;
        const LevelWriteResult dictionary = write_level(blank_level(5, 5, names), few_names);  // (the blank map's dictionary has 486 names)
        CHECK_MSG(refused_with(dictionary, "the dictionary has 486 names, more than the 100 allowed here"), dictionary.error);
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

// W3.2's walk over every member of a level and of its records. Each structured binding below must name every member of its class, so a member added to LevelData
// or to one of its records stops this file from building until it is named here, and each name is walked: a number gets its lowest bit flipped, a text one byte
// more, a chance the lowest bit of its double, a list one element more. OneChange makes the change number `target` of the walk and none other, so a copy of a level
// differs from it in that one place; the reader's findings (load_notes) are the one member that must make no difference.
class OneChange {
public:
    explicit OneChange(size_t target) : target_(target) {}
    bool made() const { return made_; }
    bool part_of_the_map() const { return part_of_the_map_; }
    const std::string& member() const { return member_; }
    const std::vector<std::string>& words() const { return words_; }

    template <class T> void number(T& value, const std::string& member, const std::vector<std::string>& words) {
        if (mine(member, words)) value = static_cast<T>(value ^ 1u);
    }
    void chance(double& value, const std::string& member, const std::vector<std::string>& words) {
        if (!mine(member, words)) return;
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        bits ^= 1u;
        std::memcpy(&value, &bits, sizeof bits);
    }
    void text(std::string& value, const std::string& member, const std::vector<std::string>& words) {
        if (mine(member, words)) value += "x";
    }
    template <class List> void one_more(List& list, const std::string& member, const std::vector<std::string>& words) {
        if (mine(member, words)) list.push_back(list.empty() ? typename List::value_type{} : list.back());
    }
    template <class List> void no_part_of_the_map(List& list, const std::string& member) {
        if (!mine(member, {})) return;
        list.push_back(typename List::value_type{});
        part_of_the_map_ = false;
    }

private:
    bool mine(const std::string& member, const std::vector<std::string>& words) {
        if (index_++ != target_) return false;
        made_ = true;
        member_ = member;
        words_ = words;
        return true;
    }
    size_t target_;
    size_t index_{0};
    bool made_{false};
    bool part_of_the_map_{true};
    std::string member_;
    std::vector<std::string> words_;
};

std::string nth(const std::string& name, size_t i) { return name + "[" + std::to_string(i) + "]"; }

void walk(OneChange& c, MapCell& cell, const std::string& member, const std::string& place) {
    auto& [tile_index, flags, properties] = cell;
    c.number(tile_index, member + ".tile_index", {place});
    c.number(flags, member + ".flags", {place});
    c.number(properties, member + ".properties", {place});
}

void walk(OneChange& c, AnthillSpawn& record, const std::string& member, const std::string& place) {
    auto& [tile_id, row, column, team_id] = record;
    c.number(tile_id, member + ".tile_id", {place});
    c.number(row, member + ".y", {place});
    c.number(column, member + ".x", {place});
    c.number(team_id, member + ".team_id", {place});
}

void walk(OneChange& c, FoodItemVariant& stage, const std::string& member, const std::string& place) {
    auto& [weight, tile_id] = stage;
    c.number(weight, member + ".weight", {place});
    c.number(tile_id, member + ".tile_id", {place});
}

void walk(OneChange& c, FoodSchedule& pile, const std::string& member, const std::string& place) {
    auto& [row, column, initial_delay, respawn_interval, variants] = pile;
    c.number(row, member + ".y", {place});
    c.number(column, member + ".x", {place});
    c.number(initial_delay, member + ".initial_delay", {place});
    c.number(respawn_interval, member + ".respawn_interval", {place});
    c.one_more(variants, member + ".variants", {place, std::to_string(variants.size()) + " stages / "});
    for (size_t k = 0; k < variants.size(); ++k) walk(c, variants[k], nth(member + ".variants", k), place + ", stage #" + std::to_string(k));
}

void walk(OneChange& c, Waypoint& point, const std::string& member, const std::string& place) {
    auto& [row, column, flag, param, probabilities] = point;
    c.number(row, member + ".y", {place});
    c.number(column, member + ".x", {place});
    c.number(flag, member + ".flag", {place});
    c.number(param, member + ".param", {place});
    for (size_t k = 0; k < probabilities.size(); ++k) c.chance(probabilities[k], nth(member + ".probabilities", k), {place, "(the chances differ)"});
}

void walk(OneChange& c, LevelData& level) {
    auto& [version, format_flags, rules, game_mode, default_minutes, description, tile_type_count, tile_dictionary, width, height, layer1_terrain, layer2_interactive, anthill_spawns,
           food_schedules, ambient_flag, ambient_tile_or_sound, waypoints, boundary_param, load_notes] = level;
    const std::string grid = "grid: " + std::to_string(height.val) + " rows x " + std::to_string(width.val) + " columns / ";
    c.number(version, "version", {"version: "});
    c.number(format_flags, "format_flags", {"format flags: "});
    c.number(rules, "rules", {"rules: "});
    c.number(game_mode, "game_mode", {"mode: "});
    c.number(default_minutes, "default_minutes", {"minutes: "});
    c.text(description, "description", {"description: "});
    c.number(tile_type_count, "tile_type_count", {"tile_type_count: "});
    c.one_more(tile_dictionary, "tile_dictionary", {"dictionary: " + std::to_string(tile_dictionary.size()) + " / "});
    for (size_t i = 0; i < tile_dictionary.size(); ++i) c.text(tile_dictionary[i], nth("tile_dictionary", i), {"dictionary name #" + std::to_string(i) + ": "});
    const uint32_t columns = width.val;                                              // (a lambda of C++17 cannot capture a structured binding)
    c.number(width.val, "width", {grid});
    c.number(height.val, "height", {grid});
    const auto cells = [&c, columns](std::vector<MapCell>& layer, const std::string& member, const std::string& name) {
        c.one_more(layer, member, {name + ": " + std::to_string(layer.size()) + " / "});
        for (size_t i = 0; i < layer.size(); ++i) {
            const std::string place = name + ", row " + std::to_string(i / columns) + ", column " + std::to_string(i % columns) + ": ";
            walk(c, layer[i], nth(member, i), place);
        }
    };
    cells(layer1_terrain, "layer1_terrain", "layer 1");
    cells(layer2_interactive, "layer2_interactive", "layer 2");
    c.one_more(anthill_spawns, "anthill_spawns", {"block 1: " + std::to_string(anthill_spawns.size()) + " / "});
    for (size_t i = 0; i < anthill_spawns.size(); ++i) walk(c, anthill_spawns[i], nth("anthill_spawns", i), "block 1 record #" + std::to_string(i) + ": ");
    c.one_more(food_schedules, "food_schedules", {"block 2: " + std::to_string(food_schedules.size()) + " / "});
    for (size_t i = 0; i < food_schedules.size(); ++i) walk(c, food_schedules[i], nth("food_schedules", i), "food pile #" + std::to_string(i));
    c.number(ambient_flag, "ambient_flag", {"block 3: "});
    c.number(ambient_tile_or_sound, "ambient_tile_or_sound", {"block 3: "});
    c.one_more(waypoints, "waypoints", {"block 4: " + std::to_string(waypoints.size()) + " / "});
    for (size_t i = 0; i < waypoints.size(); ++i) walk(c, waypoints[i], nth("waypoints", i), "waypoint #" + std::to_string(i) + ": ");
    c.number(boundary_param, "boundary_param", {"eggs: "});
    c.no_part_of_the_map(load_notes, "load_notes");
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

    run_case("W3.2 same_level Sees Every Member Of A Level And Of Its Records: A Copy Is The Same Level, Each Of The 106 Changes Of The Walk (One Member, Its Bits) Is A Difference It Names", [] {
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
        // the walk (above): every member of the level and of each of its records, changed alone, in a copy
        size_t walked = 0;
        for (size_t target = 0; target < 1000; ++target) {
            LevelData other = base;
            OneChange change(target);
            walk(change, other);
            if (!change.made()) break;
            ++walked;
            const bool same = same_level(base, other, &why);
            if (!change.part_of_the_map()) {
                CHECK_MSG(same && why.empty(), change.member() + " is no part of the map: " + why);
                continue;
            }
            CHECK_MSG(!same, change.member() + ": the change is not seen");
            for (const std::string& words : change.words()) CHECK_MSG(why.find(words) != std::string::npos, change.member() + ": \"" + why + "\" does not say \"" + words + "\"");
        }
        CHECK_MSG(walked == 108u, std::to_string(walked) + " changes walked");
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

    run_case("W4.7 Other Versions (0, 7, 10, 0xFFFFFFFF) And Grids Without A Cell (0 x 0, 31 x 0, 0 x 31) Are Refused By The Reader And Have No Level For The Writer", [&] {
        for (uint32_t version : {0u, 7u, 10u, 0xFFFFFFFFu}) {
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

    run_case("W4.8 The Reader Holds At Most 7 Times Its File (8 From A File) And The Writer 8 Times What It Writes (And 64 KiB): Waypoints Without Trigger, A Half Record, A Block 2 That Ends At Once, 512 KB", [] {
        // a 1 x 1 map whose block 4 announces `announced` records, holds `records` of 8 bytes (no trigger: 56 bytes of level each, the dearest bytes of a file) and
        // then `tail` (the egg word, or the first bytes of a record where the file ends); with `piles` block 2 announces that many food piles and ends at once, at a
        // first pile without stages
        auto file_of = [](uint16_t announced, size_t records, const std::vector<uint8_t>& tail, uint16_t piles) {
            std::vector<uint8_t> d = flat_map(2, 1, 1, 1, 1);
            d.resize(d.size() - 10);                                                  // up to block 2's count (then block 3, block 4's count and the egg word)
            put16(d, piles);
            if (piles != 0) {
                for (int k = 0; k < 5; ++k) put16(d, 0);                              // row, column, units, points and 0 stages
            }
            put16(d, 0);
            put16(d, LVL_EMPTY_TILE);                                                 // block 3
            put16(d, announced);
            for (size_t i = 0; i < records; ++i) {
                put16(d, 0);
                put16(d, 0);
                put32(d, 0);
            }
            d.insert(d.end(), tail.begin(), tail.end());
            return d;
        };
        struct Shape {
            const char* what;
            std::vector<uint8_t> file;
            size_t waypoints;
        };
        const std::vector<Shape> shapes = {
            {"8,000 waypoints without trigger", file_of(8000, 8000, {3, 0}, 0), 8000},
            {"8,000 and the row and column of one more, where the file ends", file_of(8001, 8000, {0, 0, 0, 0}, 0), 8001},
            {"8,000 and the first 6 bytes of one more", file_of(8001, 8000, {0, 0, 0, 0, 1, 0}, 0), 8001},
            {"block 2 announces 65,535 piles and ends at its first, then 8,000 waypoints and a half record", file_of(8001, 8000, {0, 0, 0, 0}, 65535), 8001},
        };
        for (const Shape& shape : shapes) {
            LevelData level;
            LevelValidation v;
            const size_t held = peak_of([&] { level.load_from_memory(shape.file.data(), shape.file.size(), &v); });
            CHECK_MSG(level.waypoints.size() == shape.waypoints && level.food_schedules.empty(), shape.what);
            CHECK_MSG(held <= reader_bound(shape.file.size()) && held >= 7 * shape.file.size() / 2,
                      std::string(shape.what) + ": " + std::to_string(held) + " bytes for " + std::to_string(shape.file.size()));
            LevelWriteResult w;
            const size_t wrote = peak_of([&] { w = write_level(level); });
            CHECK_MSG(w.ok && wrote <= writer_bound(w.bytes.size()) && wrote >= 7 * w.bytes.size() / 2, std::string(shape.what) + ": the writer held " + std::to_string(wrote));
        }
        CHECK(LVLParser::check_memory(shapes[3].file.data(), shapes[3].file.size()).has(LevelProblemKind::FoodBlockEndedEarly));
        // block 2's room for 65,535 piles, where the bytes after the egg word are many: the room the file can claim is what the reader holds most
        std::vector<uint8_t> trailing = file_of(0, 0, {3, 0}, 65535);
        trailing.insert(trailing.end(), 300000, 0x55);
        LevelData after_egg_word;
        size_t held = peak_of([&] { after_egg_word.load_from_memory(trailing.data(), trailing.size()); });
        CHECK_MSG(after_egg_word.food_schedules.empty() && held <= reader_bound(trailing.size()), std::to_string(held));
        // the largest original
        const std::vector<uint8_t> treasure = read_map(maps_dir() + "/TREASURE.LVL");
        LevelData big;
        held = peak_of([&] { big.load_from_memory(treasure.data(), treasure.size()); });
        CHECK_MSG(held <= reader_bound(treasure.size()), std::to_string(held));
        // the largest file that the limits of a new map read, 512 KB of waypoints without trigger: at most 64 KiB + 7 x 512 KiB (3.6 MiB) are held; one byte more is
        // not read; its canonical file is more than a new map may have, and the writer refuses it without holding anything of its size
        const LevelLimits new_map = LevelLimits::for_new_maps();
        std::vector<uint8_t> largest = file_of(65524, 65524, {3, 0}, 0);
        CHECK(largest.size() == 524288u);
        LevelData level;
        bool loaded = false;
        held = peak_of([&] { loaded = level.load_from_memory(largest.data(), largest.size(), nullptr, new_map); });
        CHECK_MSG(loaded && held <= reader_bound(largest.size()) && held <= 3735552u, std::to_string(held));
        LevelWriteResult w;
        held = peak_of([&] { w = write_level(level, new_map); });
        CHECK_MSG(refused_with(w, "the file would have 524288 bytes, more than the 147456 allowed here") && held < 4096u, w.error + " / " + std::to_string(held));
        held = peak_of([&] { w = write_level(level); });
        CHECK_MSG(w.ok && w.bytes == largest && held <= writer_bound(w.bytes.size()), std::to_string(held));
        // from a file, the reader also holds the file's bytes: 8 times the file and 64 KiB (4.1 MiB under the limits of a new map)
        const std::string path = scratch_path("test_lvl_writer_largest");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(largest.data()), static_cast<std::streamsize>(largest.size()));
        }
        LevelValidation from_file;
        held = peak_of([&] { from_file = LVLParser::check_file(path, 0x0F, new_map); });
        CHECK_MSG(from_file.parsed && held <= kSlack + 8 * largest.size() && held <= 4259840u && held >= 7 * largest.size(), std::to_string(held));
        std::error_code ec;
        std::filesystem::remove(path, ec);
        largest.push_back(0);
        LevelData refused;
        LevelValidation v;
        held = peak_of([&] { loaded = refused.load_from_memory(largest.data(), largest.size(), &v, new_map); });
        CHECK(!loaded && has_fatal(v, LevelProblemKind::FileTooLarge) && mentions(v, "the file has 524289 bytes, more than the 524288 allowed here") && held < 4096u);
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
        const std::string path = scratch_path("test_lvl_writer_huge");
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

    run_case("W5.3 A Byte That The Original Reads And The Rewrite Changed Is Unexplained (A Fault Of The Writer), So Is One That The Rewrite Must Hold As Zero, And So Is A Rewrite Of Another Level", [&] {
        // the header, the description, the count word, a name, the zeros after a name, every byte of the grid size, the layers, the blocks, the egg word
        for (size_t at : {size_t{2}, size_t{6}, size_t{12}, size_t{40}, size_t{42}, size_t{45}, size_t{97}, size_t{98}, size_t{99}, size_t{100}, size_t{101}, size_t{102},
                          size_t{103}, size_t{104}, size_t{150}, size_t{200}, size_t{300}}) {
            std::vector<uint8_t> forged = canonical;
            forged[at] ^= 0x40;
            const std::vector<RewriteChange> c = describe_rewrite(canonical.data(), canonical.size(), base, forged);
            CHECK_MSG(has_kind(c, RewriteChangeKind::Unexplained), std::to_string(at));
            const RewriteChange* u = change_of(c, RewriteChangeKind::Unexplained);
            CHECK_MSG(u != nullptr && u->offset == at, std::to_string(at));
        }
        // the zeros after the end of a shorter description
        LevelData shorter = base;
        shorter.description = "short";
        const std::vector<uint8_t> clean = write_level(shorter).bytes;
        std::vector<uint8_t> padded = clean;
        padded[17] = 'x';
        const std::vector<RewriteChange> after_text = describe_rewrite(clean.data(), clean.size(), shorter, padded);
        const RewriteChange* u = change_of(after_text, RewriteChangeKind::Unexplained);
        CHECK(after_text.size() == 1u && u != nullptr && u->offset == 17u);
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

    run_case("W6.4 A Blank Map Beyond The Limits Gets No Cell: 65,535 x 65,535 (51 GB Of Cells) And 9,000 x 9,000 Take Nothing Of Their Size, And The Writer Refuses Them By Their Grid", [&] {
        struct Grid {
            uint16_t rows;
            uint16_t columns;
            const char* words;
        };
        // (what is held is the dictionary and used_tile_ids' table of 65,536 ids: 64 KiB and a little, nothing of the grid's size)
        const size_t little = kSlack + 4096;
        for (const Grid& g : {Grid{65535, 65535, "the grid has 65535 rows x 65535 columns, more than the 256 x 256 allowed here"},
                              Grid{9000, 9000, "the grid has 9000 rows x 9000 columns, more than the 256 x 256 allowed here"},
                              Grid{257, 1, "the grid has 257 rows x 1 columns, more than the 256 x 256 allowed here"}}) {
            LevelData l;
            const size_t held = peak_of([&] { l = blank_level(g.rows, g.columns, names); });
            CHECK_MSG(held < little && l.layer1_terrain.empty() && l.layer2_interactive.empty(), g.words + std::string(": ") + std::to_string(held));
            CHECK(l.height == static_cast<uint32_t>(g.rows) && l.width == static_cast<uint32_t>(g.columns));
            LevelWriteResult w;
            const size_t wrote = peak_of([&] { w = write_level(l); });
            CHECK_MSG(refused_with(w, g.words) && wrote < 4096u, w.error + " / " + std::to_string(wrote));
        }
        // with the limits of a new map: 131 a side or 10,010 cells get no cell either; the largest grids of each limit get theirs
        const LevelLimits new_map = LevelLimits::for_new_maps();
        for (const Grid& g : {Grid{200, 200, "the grid has 200 rows x 200 columns, more than the 130 x 130 allowed here"},
                              Grid{77, 130, "the grid has 77 rows x 130 columns, 10010 cells, more than the 10000 allowed here"}}) {
            LevelData l;
            const size_t held = peak_of([&] { l = blank_level(g.rows, g.columns, names, new_map); });
            CHECK_MSG(held < little && l.layer1_terrain.empty(), g.words + std::string(": ") + std::to_string(held));
            const LevelWriteResult w = write_level(l, new_map);
            CHECK_MSG(refused_with(w, g.words), w.error);
            // written with looser limits, the level that has no cell is refused by its grid too
            const LevelWriteResult loose = write_level(l);
            const std::string grid = "the grid has " + std::to_string(g.rows) + " rows x " + std::to_string(g.columns) + " columns but the level holds no cell";
            CHECK_MSG(refused_with(loose, grid), loose.error);
        }
        CHECK(blank_level(100, 100, names, new_map).layer1_terrain.size() == 10000u && blank_level(76, 130, names, new_map).layer2_interactive.size() == 9880u);
        const LevelData largest = blank_level(256, 256, names);
        CHECK(largest.layer1_terrain.size() == 65536u && write_level(largest).ok);
    });
}

}  // namespace

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// W7 Extended maps (version 9): the layout, the laws of conversion, the limits, the refusal by a place that reads Classic maps only, hostile input
// ------------------------------------------------------------------------------------------------------------------------------------------------------

// A level of every field a version 9 file has, with the anchors in the relative form of the format (docs/MAP_FORMAT.md, "Version 9")
LevelData extended_level() {
    LevelData l;
    l.version = 9;
    l.format_flags = 0;
    l.rules = 1;
    l.game_mode = 1;
    l.default_minutes = 8;
    l.description = std::string(100, 'd') + " and thirty more bytes of text, 130 in all";   // 100 + 30 bytes: more than a Classic map holds
    l.description.resize(130, '.');
    l.tile_dictionary = {".", "g01a", "obj", "abcdefghijk"};
    l.tile_type_count = 3;
    l.height.val = 3;
    l.width.val = 4;
    for (uint16_t i = 0; i < 12; ++i) {
        l.layer1_terrain.push_back(MapCell{1, i, 0xCDCD});
        l.layer2_interactive.push_back(MapCell{LVL_EMPTY_TILE, 0, 0});
    }
    l.layer2_interactive[0] = MapCell{3, 0, 0x0000};        // row 0, column 0: anchored at itself
    l.layer2_interactive[6] = MapCell{2, 1, 0x01FF};        // row 1, column 2: the row offset is -1 (0xFF), the column offset +1 (0x01): the anchor is row 0, column 3
    l.layer2_interactive[11] = MapCell{129, 0, 3};          // a bomb: the third word is its owner, whatever it looks like
    l.anthill_spawns.push_back(AnthillSpawn{152, 1, 1, 3});
    l.anthill_spawns.push_back(AnthillSpawn{421, 0, 3, 255});
    FoodSchedule f;
    f.y = 2;
    f.x = 1;
    f.initial_delay = 4;
    f.respawn_interval = 2;
    f.variants = {FoodItemVariant{4, 2}, FoodItemVariant{0, LVL_EMPTY_TILE}};
    l.food_schedules.push_back(f);
    l.ambient_flag = 5;
    l.ambient_tile_or_sound = 62;
    Waypoint w;
    w.y = 1;
    w.x = 1;
    w.flag = 1;
    w.param = 40;
    w.probabilities = {0.5, 0.25, 0.125, 0.0625, 0.0625};
    l.waypoints.push_back(w);
    l.boundary_param = 9;
    return l;
}

// The bytes of extended_level(), written out here by hand in the order of the document (not with the writer's code)
std::vector<uint8_t> extended_bytes() {
    const LevelData l = extended_level();
    std::vector<uint8_t> d;
    put32(d, 9);
    put32(d, 1);                                                                    // mode
    put16(d, 8);                                                                    // minutes
    put32(d, 0);                                                                    // format flags
    put16(d, 1);                                                                    // rules
    put16(d, 130);                                                                  // the description's length, then its bytes
    d.insert(d.end(), l.description.begin(), l.description.end());
    put16(d, 3);                                                                    // the number of names less one
    for (const std::string& n : l.tile_dictionary) put_text(d, n, 11);
    put32(d, 3);                                                                    // rows first
    put32(d, 4);
    for (const MapCell& c : l.layer1_terrain) { put16(d, c.tile_index); put16(d, c.flags); put16(d, c.properties); }
    for (const MapCell& c : l.layer2_interactive) { put16(d, c.tile_index); put16(d, c.flags); put16(d, c.properties); }
    put16(d, 2);
    put16(d, 152); put16(d, 1); put16(d, 1);
    put16(d, 421); put16(d, 0); put16(d, 3);
    put16(d, 1);
    put16(d, 2); put16(d, 1); put16(d, 4); put16(d, 2); put16(d, 2);
    put16(d, 4); put16(d, 2); put16(d, 0); put16(d, 0x7FFE);
    put16(d, 5); put16(d, 62);
    put16(d, 1);
    put16(d, 1); put16(d, 1); put32(d, 1); put32(d, 40);
    for (double p : l.waypoints[0].probabilities) put_double(d, p);
    put16(d, 9);
    return d;
}

// A version 9 file made by hand: the header, a dictionary of three names, the grid size and `cells` cells of each layer (zero words), the four blocks empty and the egg word
std::vector<uint8_t> v9_bytes(uint32_t flags, uint32_t rules_word, const std::string& description, uint32_t rows, uint32_t columns, uint64_t cells, uint32_t description_length_word = 0xFFFFFFFFu) {
    std::vector<uint8_t> d;
    put32(d, 9);
    put32(d, 1);
    put16(d, 6);
    put32(d, flags);
    put16(d, rules_word);
    put16(d, description_length_word == 0xFFFFFFFFu ? static_cast<uint32_t>(description.size()) : description_length_word);
    d.insert(d.end(), description.begin(), description.end());
    put16(d, 2);
    put_text(d, ".", 11);
    put_text(d, "g", 11);
    put_text(d, "x", 11);
    put32(d, rows);
    put32(d, columns);
    d.insert(d.end(), static_cast<size_t>(cells) * 12, uint8_t{0});
    put16(d, 0);                                                                    // block 1
    put16(d, 0);                                                                    // block 2
    put16(d, 0); put16(d, LVL_EMPTY_TILE);                                          // block 3
    put16(d, 0);                                                                    // block 4
    put16(d, 6);
    return d;
}

const LevelLimits& extended_limits() {
    static const LevelLimits limits = LevelLimits::for_extended_maps();
    return limits;
}

bool load_extended(const std::vector<uint8_t>& d, LevelData& level, LevelValidation* report = nullptr, const LevelLimits& limits = LevelLimits::format_ceiling()) {
    return level.load_from_memory(d.data(), d.size(), report, limits);
}

// Everything a hostile file must give the version 9 reader: a level or a Fatal finding that says why (never a crash), within the heap bound; a level is written, reads
// back the same, and writes the same bytes again
void hostile_extended(const uint8_t* data, size_t size, const std::string& what) {
    const LevelLimits limits = LevelLimits::format_ceiling();
    LevelData level;
    LevelValidation report;
    bool loaded = false;
    const size_t read_peak = peak_of([&] { loaded = level.load_from_memory(data, size, &report, limits); });
    CHECK_MSG(read_peak <= reader_bound(size), what + ": the reader held " + std::to_string(read_peak) + " bytes for " + std::to_string(size));
    if (!loaded) {
        CHECK_MSG(report.first_fatal() != nullptr && !report.reason().empty(), what + ": a file that does not load says why");
        CHECK_MSG(is_empty_level(level), what + ": nothing of it is left");
        return;
    }
    LevelWriteResult w;
    const size_t write_peak = peak_of([&] { w = write_level(level, limits); });
    CHECK_MSG(w.ok, what + ": a level the reader made is written: " + w.error);
    if (!w.ok) return;
    CHECK_MSG(write_peak <= writer_bound(w.bytes.size()), what + ": the writer held " + std::to_string(write_peak) + " bytes for " + std::to_string(w.bytes.size()));
    LevelData back;
    CHECK_MSG(back.load_from_memory(w.bytes.data(), w.bytes.size(), nullptr, limits), what + ": the rewrite loads");
    std::string why;
    CHECK_MSG(same_level(level, back, &why), what + ": " + why);
    const LevelWriteResult again = write_level(back, limits);
    CHECK_MSG(again.ok && again.bytes == w.bytes, what + ": written twice, the same bytes");
}

void w7_extended_maps() {
    const std::vector<uint8_t> tiny = read_map(maps_dir() + "/TINY.LVL");
    const std::vector<uint8_t> small = read_map(maps_dir() + "/SMALL.LVL");

    run_case("W7.1 The Layout Of A Version 9 File: The Bytes Of A Level Written By Hand, Header, Flags, Rules, Description, Dictionary, Grid, Blocks; Read Back, Written Again The Same Bytes", [] {
        const LevelData level = extended_level();
        const std::vector<uint8_t> expected = extended_bytes();
        const LevelWriteResult w = write_level(level, extended_limits());
        CHECK_MSG(w.ok, w.error);
        CHECK(w.bytes == expected);
        LevelData back;
        LevelValidation report;
        CHECK(back.load_from_memory(expected.data(), expected.size(), &report, extended_limits()));
        std::string why;
        CHECK_MSG(same_level(level, back, &why), why);
        CHECK(back.version == 9u && back.rules == 1u && back.format_flags == 0u && back.description == level.description && back.is_extended());
        CHECK(report.parsed && report.playable);
        const LevelWriteResult again = write_level(back, extended_limits());
        CHECK(again.ok && again.bytes == expected);
        // the description's length is a word, then the bytes; the dictionary's count follows at once
        CHECK(rd32(expected, 0) == 9u && rd32(expected, 10) == 0u && rd16(expected, 14) == 1u && rd16(expected, 16) == 130u && rd16(expected, 18 + 130) == 3u);
        // the same level without a description: the dictionary follows the length word
        LevelData none = level;
        none.description.clear();
        const LevelWriteResult n = write_level(none, extended_limits());
        CHECK(n.ok && n.bytes.size() + 130 == expected.size() && rd16(n.bytes, 16) == 0u && rd16(n.bytes, 18) == 3u);
        // every version 9 byte after the description is the version 8 layout: the dictionary and what follows are the same encoding
        LevelData classic = level;
        classic.version = 8;
        classic.format_flags = 0;
        classic.rules = 0;
        classic.description = "short";
        classic.layer2_interactive[6].properties = 0x0300;       // (an absolute anchor: row 0, column 3)
        const LevelWriteResult c = write_level(classic);
        CHECK(c.ok && c.bytes.size() == 42 + 4 * 11 + (expected.size() - (18 + 130 + 2 + 4 * 11)));
        CHECK(std::equal(c.bytes.begin() + 42 + 44, c.bytes.end() - 0, expected.begin() + 18 + 130 + 2 + 44, expected.end() - 0) == false);   // (the anchor word differs)
        CHECK(std::equal(c.bytes.begin() + 42, c.bytes.begin() + 42 + 44, expected.begin() + 18 + 130 + 2));
    });

    run_case("W7.2 Read, Write, Read Of 1,500 Random Extended Levels (Rules 0 And 1, Descriptions Of 0 To 255 Bytes): The Same Level, Written Twice The Same Bytes", [] {
        Lcg r(0xE17E4DEDu);
        for (int i = 0; i < 1500; ++i) {
            LevelData l = random_level(r);
            l.version = 9;
            l.rules = static_cast<uint16_t>(r.below(2));
            l.description.clear();
            const size_t len = r.below(256);
            for (size_t k = 0; k < len; ++k) l.description += static_cast<char>(1 + r.below(255));
            const LevelWriteResult w = write_level(l, extended_limits());
            CHECK_MSG(w.ok, "level " + std::to_string(i) + ": " + w.error);
            if (!w.ok) continue;
            LevelData back;
            CHECK(back.load_from_memory(w.bytes.data(), w.bytes.size(), nullptr, extended_limits()));
            std::string why;
            CHECK_MSG(same_level(l, back, &why), "level " + std::to_string(i) + ": " + why);
            const LevelWriteResult again = write_level(back, extended_limits());
            CHECK(again.ok && again.bytes == w.bytes);
        }
    });

    run_case("W7.3 The Anchor Of A Layer-2 Object Is Relative To Its Cell In Version 9 (Signed Bytes, Row In The Low Byte): What Has One, The Sum, The Wrap In A Map Of 256 Or Less, The Plain Sum In A Larger One", [] {
        const LevelData l = extended_level();
        int32_t ar = -1, ac = -1;
        CHECK(l.object_anchor(1, 2, ar, ac) && ar == 0 && ac == 3);                // row 1 - 1, column 2 + 1
        CHECK(l.object_anchor(0, 0, ar, ac) && ar == 0 && ac == 0);
        CHECK(!l.object_anchor(0, 1, ar, ac) && !l.has_object_anchor(0, 1));       // the empty tile has none
        CHECK(!l.object_anchor(2, 3, ar, ac) && !l.has_object_anchor(2, 3));       // a bomb's word is its owner
        CHECK(!l.object_anchor(3, 0, ar, ac) && !l.object_anchor(0, 4, ar, ac));   // outside the grid
        for (const uint16_t tile : {uint16_t{0xFFFF}, uint16_t{128}, uint16_t{133}, uint16_t{129}, uint16_t{132}}) {
            LevelData m = l;
            m.layer2_interactive[1] = MapCell{tile, 0, 0x1234};
            CHECK_MSG(m.has_object_anchor(0, 1) == (tile == 128 || tile == 133), "tile " + std::to_string(tile));
        }
        // the sign of each byte: -128 and 127 reach as far as a byte goes; the two bytes are the row (low) and the column (high)
        LevelData m = l;
        m.layer2_interactive[5] = MapCell{2, 0, 0x7F80};                           // row 1, column 1: row offset -128, column offset +127
        CHECK(m.object_anchor(1, 1, ar, ac) && ar == ((1 - 128) & 255) && ac == ((1 + 127) & 255));   // (a map of 3 x 4: the sum modulo 256, as the Classic anchor byte is)
        // a version 8 level keeps the absolute bytes, row in the low byte
        LevelData v8 = l;
        v8.version = 8;
        v8.layer2_interactive[6].properties = 0x0300;
        CHECK(v8.object_anchor(1, 2, ar, ac) && ar == 0 && ac == 3);
        v8.layer2_interactive[6].properties = 0x0007;
        CHECK(v8.object_anchor(1, 2, ar, ac) && ar == 7 && ac == 0);
        // a map of more than 256 on a side: the plain sum, which may be negative or beyond a byte
        LevelData wide;
        wide.version = 9;
        wide.height.val = 2;
        wide.width.val = 300;
        wide.layer1_terrain.assign(600, MapCell{0, 0, 0});
        wide.layer2_interactive.assign(600, MapCell{LVL_EMPTY_TILE, 0, 0});
        wide.layer2_interactive[299] = MapCell{2, 0, static_cast<uint16_t>(0x0100 | 0x00FF)};      // row 0, column 299: row -1, column + 1
        wide.layer2_interactive[2] = MapCell{2, 0, 0x80FD};                                        // row 0, column 2: row -3, column -128
        CHECK(wide.object_anchor(0, 299, ar, ac) && ar == -1 && ac == 300);
        CHECK(wide.object_anchor(0, 2, ar, ac) && ar == -3 && ac == -126);
    });

    run_case("W7.4 Classic To Extended: Every Field Carried, The Anchors Made Relative, Rules 0, The Description Kept; A Copy, Another File; And Back To The Same Bytes", [&] {
        for (const std::vector<uint8_t>* file : {&tiny, &small}) {
            LevelData source;
            CHECK(source.load_from_memory(file->data(), file->size()));
            const LevelConversion up = convert_to_extended(source);
            CHECK_MSG(up.ok && up.reasons.empty(), up.reasons.empty() ? "" : up.reasons[0]);
            CHECK(up.level.version == 9u && up.level.rules == 0u && up.level.format_flags == 0u && up.level.description == source.description && up.level.is_extended());
            CHECK(source.version == 8u);                                           // the source is not touched
            // every anchor: the same absolute anchor through the relative word
            for (uint32_t r = 0; r < source.height; ++r) {
                for (uint32_t c = 0; c < source.width; ++c) {
                    int32_t a_row = 0, a_column = 0, b_row = 0, b_column = 0;
                    const bool has = source.object_anchor(r, c, a_row, a_column);
                    CHECK(has == up.level.object_anchor(r, c, b_row, b_column));
                    if (has) CHECK_MSG(a_row == b_row && a_column == b_column, "row " + std::to_string(r) + ", column " + std::to_string(c));
                    if (!has) CHECK(source.get_cell_layer2(c, r).properties == up.level.get_cell_layer2(c, r).properties);   // the empty cells and the bombs keep their word
                }
            }
            const LevelWriteResult v9 = write_level(up.level, extended_limits());
            const LevelWriteResult v8 = write_level(source);
            CHECK(v9.ok && v8.ok && v9.bytes != v8.bytes);                         // another file, so another hash
            LevelData read9;
            CHECK(read9.load_from_memory(v9.bytes.data(), v9.bytes.size(), nullptr, extended_limits()));
            const LevelConversion down = convert_to_classic(read9);
            CHECK_MSG(down.ok && down.reasons.empty(), down.reasons.empty() ? "" : down.reasons[0]);
            const LevelWriteResult again = write_level(down.level);
            CHECK(again.ok && again.bytes == v8.bytes);                            // the same bytes as the canonical file of the source
            std::string why;
            CHECK_MSG(same_level(source, down.level, &why), why);
            CHECK(down.level.version == 8u && down.level.rules == 0u);
            CHECK(down.notes.empty());
        }
        // a junk anchor byte of a Classic map (some library maps hold 0xCDCD where an object stands) survives both ways
        LevelData junk;
        CHECK(junk.load_from_memory(tiny.data(), tiny.size()));
        junk.layer2_interactive[7 * junk.width + 15] = MapCell{476, 0, 0xCDCD};
        junk.layer2_interactive[0] = MapCell{476, 0, 0xFE58};
        const LevelConversion up = convert_to_extended(junk);
        CHECK(up.ok);
        const LevelConversion down = convert_to_classic(up.level);
        CHECK_MSG(down.ok && same_level(junk, down.level), down.ok ? "" : down.reasons[0]);
        int32_t ar = 0, ac = 0;
        CHECK(up.level.object_anchor(7, 15, ar, ac) && ar == 0xCD && ac == 0xCD);
        // a level that is Extended already is copied; a level that is no map has reasons
        const LevelConversion same = convert_to_extended(up.level);
        CHECK(same.ok && same_level(same.level, up.level));
        LevelData odd = junk;
        odd.version = 7;
        const LevelConversion no = convert_to_extended(odd);
        CHECK(!no.ok && no.reasons.size() == 1u && no.reasons[0].find("version 7") != std::string::npos);
        LevelData cut = junk;
        cut.layer2_interactive.pop_back();
        CHECK(!convert_to_extended(cut).ok && !convert_to_classic(cut).ok);
        // a Classic level is copied by convert_to_classic as it is
        const LevelConversion classic_again = convert_to_classic(junk);
        CHECK(classic_again.ok && same_level(classic_again.level, junk));
    });

    run_case("W7.5 Extended To Classic When It Fits, And The Reasons When It Does Not (Each With Its Number And Its Limit); needs_extended Says The Same", [] {
        LevelData base = extended_level();
        // 130 bytes of description are more than the 30 of a Classic map's field
        LevelConversion c = convert_to_classic(base);
        CHECK(!c.ok && c.reasons.size() == 1u && c.reasons[0].find("the description is 130 bytes; a Classic map holds 30") == 0 && c.notes.empty());
        base.description = "fits";
        // it fits: rules 1 is carried with a note
        c = convert_to_classic(base);
        CHECK_MSG(c.ok, c.reasons.empty() ? "" : c.reasons[0]);
        CHECK(c.notes.size() == 1u && c.notes[0].find("rules 1") != std::string::npos);
        CHECK(c.level.version == 8u && c.level.rules == 0u);
        LevelData d = base;
        d.description = std::string(31, 'x');
        c = convert_to_classic(d);
        CHECK(!c.ok && c.reasons.size() == 1u && c.reasons[0].find("the description is 31 bytes; a Classic map holds 30") == 0);
        CHECK(needs_extended(d) == c.reasons);
        d.description = std::string(30, 'x');
        CHECK(convert_to_classic(d).ok && needs_extended(d).empty());
        // both sides, the dictionary, the flags, the rules word
        LevelData big = base;
        big.height.val = 300;
        big.width.val = 400;
        big.layer1_terrain.assign(120000, MapCell{1, 0, 0});
        big.layer2_interactive.assign(120000, MapCell{LVL_EMPTY_TILE, 0, 0});
        c = convert_to_classic(big);
        CHECK(!c.ok && c.reasons.size() == 2u);
        CHECK(c.reasons[0] == "this map is 400 columns wide; a Classic map has at most 256 columns" && c.reasons[1] == "this map is 300 rows tall; a Classic map has at most 256 rows");
        CHECK(needs_extended(big) == c.reasons);
        LevelData names = base;
        names.tile_dictionary.assign(1345, ".");
        names.tile_type_count = 1344;
        c = convert_to_classic(names);
        CHECK(!c.ok && c.reasons.size() == 1u && c.reasons[0] == "the dictionary has 1345 names; a Classic map holds at most 1344");
        LevelData flags = base;
        flags.format_flags = 0x10;
        c = convert_to_classic(flags);
        CHECK(!c.ok && c.reasons.size() == 1u && c.reasons[0].find("format flags (0x10)") != std::string::npos);
        LevelData rules = base;
        rules.rules = 2;
        c = convert_to_classic(rules);
        CHECK(!c.ok && c.reasons.size() == 1u && c.reasons[0] == "the rules word is 2: only 0 and 1 are known");
        // an anchor that no byte holds: a map of 300 columns, an object at column 290 anchored at column 289; the map is too wide as well
        LevelData wide = base;
        wide.height.val = 1;
        wide.width.val = 300;
        wide.layer1_terrain.assign(300, MapCell{1, 0, 0});
        wide.layer2_interactive.assign(300, MapCell{LVL_EMPTY_TILE, 0, 0});
        wide.layer2_interactive[290] = MapCell{2, 0, 0xFF00};                       // row offset 0, column offset -1: column 289
        wide.layer2_interactive[10] = MapCell{2, 0, 0x0000};                        // column 10: fine
        c = convert_to_classic(wide);
        CHECK(!c.ok && c.reasons.size() == 2u);
        CHECK(c.reasons.size() == 2u && c.reasons[1] == "1 object cells are anchored outside the 0 to 255 that a Classic anchor byte holds (the first at row 0, column 290)");
        // the edges of a byte: column 255 and row 255 are the last anchors a Classic map holds, 256 and -1 are not (the map is wide as well: its width is a reason of its own)
        auto anchor_reasons = [](uint32_t rows, uint32_t columns, size_t at, uint16_t word) {
            LevelData m = extended_level();
            m.description.clear();
            m.height.val = rows;
            m.width.val = columns;
            m.layer1_terrain.assign(static_cast<size_t>(rows) * columns, MapCell{1, 0, 0});
            m.layer2_interactive.assign(static_cast<size_t>(rows) * columns, MapCell{LVL_EMPTY_TILE, 0, 0});
            m.layer2_interactive[at] = MapCell{2, 0, word};
            size_t n = 0;
            for (const std::string& r : needs_extended(m)) n += r.find("object cells are anchored outside") != std::string::npos ? 1u : 0u;
            return n;
        };
        CHECK(anchor_reasons(1, 300, 280, static_cast<uint16_t>(0xFB00)) == 1u);            // column 280 - 5 = 275: beyond a byte
        CHECK(anchor_reasons(1, 300, 280, static_cast<uint16_t>(0xE700)) == 0u);            // column 280 - 25 = 255: the last anchor a byte holds
        CHECK(anchor_reasons(1, 300, 280, static_cast<uint16_t>(0xE800)) == 1u);            // column 280 - 24 = 256: no byte
        CHECK(anchor_reasons(1, 300, 280, static_cast<uint16_t>(0x0000)) == 1u);            // column 280: no byte
        CHECK(anchor_reasons(1, 300, 0, static_cast<uint16_t>(0xFF00)) == 1u);              // column 0 - 1 = -1: no byte
        CHECK(anchor_reasons(1, 300, 0, static_cast<uint16_t>(0x0000)) == 0u);              // column 0: fine
        CHECK(anchor_reasons(300, 1, 280, static_cast<uint16_t>(0x00E7)) == 0u);            // row 280 - 25 = 255
        CHECK(anchor_reasons(300, 1, 280, static_cast<uint16_t>(0x00E8)) == 1u);            // row 256
        CHECK(anchor_reasons(300, 1, 0, static_cast<uint16_t>(0x00FF)) == 1u);              // row -1
        // an anchor that points outside the grid of a map of 256 or less is a byte modulo 256 (so no reason): a bomb's word never is an anchor
        LevelData small_map = base;
        small_map.layer2_interactive[6].properties = 0x7F80;                         // -128 rows, +127 columns from row 1, column 2
        c = convert_to_classic(small_map);
        CHECK(c.ok);
        int32_t ar = -1, ac = -1;
        CHECK(c.ok && c.level.object_anchor(1, 2, ar, ac) && ar == ((1 - 128) & 255) && ac == ((2 + 127) & 255));
        CHECK(c.ok && c.level.get_cell_layer2(3, 2).properties == 3u);               // the bomb keeps its owner
        // a level that is no map
        LevelData seven = base;
        seven.version = 7;
        CHECK(!convert_to_classic(seven).ok && needs_extended(seven).size() == 1u);
        // the conversion writes a copy: the source is unchanged, and nothing is shared
        LevelData before = base;
        const LevelConversion once = convert_to_classic(base);
        CHECK(same_level(before, base) && once.level.version != base.version);
    });

    run_case("W7.6 Limits: A Place Reads Classic Maps Only Until It Sets allow_extended; The Numbers Of Both Kinds; No Value Passes The Ceiling Of The Format", [] {
        CHECK(!LevelLimits{}.allow_extended && !LevelLimits::for_new_maps().allow_extended && LevelLimits::for_extended_maps().allow_extended);
        const LevelLimits e = LevelLimits::for_extended_maps();
        CHECK(e.rows() == 1024u && e.columns() == 1024u && e.cells() == 262144u && e.read_bytes() == 4u * 1024u * 1024u && e.write_bytes() == 3584u * 1024u &&
              e.dictionary_names() == 1344u && e.description_bytes() == 255u);
        CHECK(e.rows_for(8) == 256u && e.columns_for(8) == 256u && e.cells_for(8) == 65536u && e.rows_for(9) == 1024u && e.cells_for(9) == 262144u);   // (a Classic map keeps its 256)
        CHECK(LVL_FORMAT_CEILING_SIDE == 2048u && LVL_FORMAT_CEILING_CELLS == 4194304u && LVL_FORMAT_CEILING_FILE_BYTES == 64u * 1024u * 1024u);
        LevelLimits huge = LevelLimits::format_ceiling();
        huge.max_rows = huge.max_columns = 100000;
        huge.max_cells = 0xFFFFFFFFu;
        huge.max_read_bytes = huge.max_write_bytes = std::numeric_limits<size_t>::max();
        huge.max_dictionary_names = 100000;
        huge.max_description_bytes = 100000;
        CHECK(huge.rows() == 2048u && huge.columns() == 2048u && huge.cells() == 4194304u && huge.read_bytes() == 64u * 1024u * 1024u && huge.write_bytes() == 64u * 1024u * 1024u &&
              huge.dictionary_names() == 1344u && huge.description_bytes() == 255u);
        LevelLimits classic = huge;
        classic.allow_extended = false;                                              // without it the values count as the game's
        CHECK(classic.rows() == 256u && classic.columns() == 256u && classic.cells() == 65536u && classic.read_bytes() == std::numeric_limits<size_t>::max());
        CHECK(LevelLimits{}.rows() == 256u && LevelLimits::for_new_maps().rows() == 130u && LevelLimits::for_new_maps().cells() == 10000u);   // the Classic kinds are as they were
        // a canonical file of a 512 x 512 map is within the 3.5 MiB the limits write, and 513 x 512 is not
        std::vector<std::string> names(1344, ".");
        const LevelWriteResult w = write_level(blank_extended_level(512, 512, names), e);
        CHECK_MSG(w.ok && w.bytes.size() < 3584u * 1024u, w.error + " / " + std::to_string(w.bytes.size()));
        CHECK_MSG(refused_with(write_level(blank_extended_level(513, 512, names), e), "513 rows x 512 columns, 262656 cells, more than the 262144 allowed here"), "513 x 512");
        CHECK(write_level(blank_extended_level(1024, 256, names), e).ok && write_level(blank_extended_level(256, 1024, names), e).ok);
        CHECK_MSG(refused_with(write_level(blank_extended_level(1025, 1, names), e), "1025 rows x 1 columns, more than the 1024 x 1024 allowed here"), "1025 x 1");
        const LevelWriteResult tall = write_level(blank_extended_level(2048, 2048, names, LevelLimits::format_ceiling()), LevelLimits::format_ceiling());
        CHECK_MSG(tall.ok && tall.bytes.size() > 48u * 1024u * 1024u && tall.bytes.size() <= 64u * 1024u * 1024u, tall.error + " / " + std::to_string(tall.bytes.size()));
        // the ceiling is the format's: the writer refuses 2049 whatever the limits
        CHECK_MSG(refused_with(write_level(blank_extended_level(2049, 1, names), huge), "2049 rows x 1 columns, more than the 2048 x 2048 allowed here"), "2049");
        // the blank Extended map: rules 1 from 91 rows or 257 columns on
        CHECK(blank_extended_level(90, 256, names).rules == 0u && blank_extended_level(91, 5, names).rules == 1u && blank_extended_level(5, 257, names).rules == 1u);
        CHECK(blank_extended_level(5, 5, names).version == 9u && blank_extended_level(5, 5, names).format_flags == 0u);
        // a grid beyond the limits gets no cell and nothing of its size is allocated
        LevelData none;
        const size_t held = peak_of([&] { none = blank_extended_level(0xFFFFFFFFu, 0xFFFFFFFFu, names); });
        CHECK_MSG(none.layer1_terrain.empty() && held <= kSlack, std::to_string(held));         // (the 64 KiB are the table of the tile ids that dictionary_of_used_tiles keeps)
        CHECK(refused_with(write_level(none, e), "more than the 1024 x 1024 allowed here"));
    });

    run_case("W7.7 A Place That Reads Classic Maps Only Refuses Version 9 Cleanly (version_not_supported), Reads Nothing, Writes Nothing; A Version 8 File Is The Same In Either Reader", [&] {
        const std::vector<uint8_t> v9 = extended_bytes();
        for (const LevelLimits& limits : {LevelLimits{}, LevelLimits::for_new_maps()}) {
            LevelData level;
            LevelValidation v;
            bool loaded = true;
            const size_t held = peak_of([&] { loaded = level.load_from_memory(v9.data(), v9.size(), &v, limits); });
            CHECK(!loaded && !v.parsed && !v.playable && has_fatal(v, LevelProblemKind::VersionNotSupported) && is_empty_level(level) && held < kSlack);
            CHECK_MSG(mentions(v, "version 9 is not supported here"), v.reason());
            CHECK(std::string(to_string(LevelProblemKind::VersionNotSupported)) == "version_not_supported" && !LVLParser::check_memory(v9.data(), v9.size(), 0x0F, limits).playable);
            CHECK(refused_with(write_level(extended_level(), limits), "this place writes Classic maps (version 8) only"));
        }
        // the same by file, and by a level in memory
        const std::string path = scratch_path("test_lvl_writer_v9");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(v9.data()), static_cast<std::streamsize>(v9.size()));
        }
        LevelValidation from_file = LVLParser::check_file(path);
        CHECK(!from_file.playable && has_fatal(from_file, LevelProblemKind::VersionNotSupported));
        std::error_code ec;
        std::filesystem::remove(path, ec);
        const LevelValidation in_memory = extended_level().validate();
        CHECK(!in_memory.playable && in_memory.has(LevelProblemKind::VersionNotSupported));
        CHECK(extended_level().validate(0x0F, extended_limits()).playable);
        // version 10 and the others stay what they were (and a place with Extended maps says it reads both)
        std::vector<uint8_t> ten = v9;
        wr32(ten, 0, 10);
        LevelData level;
        LevelValidation v;
        CHECK(!level.load_from_memory(ten.data(), ten.size(), &v) && has_fatal(v, LevelProblemKind::WrongVersion) && !mentions(v, "versions 8 and 9"));
        CHECK(!level.load_from_memory(ten.data(), ten.size(), &v, extended_limits()) && has_fatal(v, LevelProblemKind::WrongVersion) && mentions(v, "this place reads versions 8 and 9"));
        // a version 8 file in the Extended reader: the level, the findings and the canonical bytes of the Classic reader
        for (const std::vector<uint8_t>* file : {&tiny, &small}) {
            LevelData a;
            LevelData b;
            LevelValidation va;
            LevelValidation vb;
            CHECK(a.load_from_memory(file->data(), file->size(), &va) && b.load_from_memory(file->data(), file->size(), &vb, extended_limits()));
            CHECK(same_level(a, b) && a.version == 8u && va.describe() == vb.describe());
            CHECK(write_level(b, extended_limits()).bytes == write_level(a).bytes);
        }
        // a version 8 file of 300 columns is still too large for a Classic map in the Extended reader (the anchors of its format are bytes)
        const std::vector<uint8_t> wide = flat_map(4, 4, 300, 4, 300);
        LevelData w;
        CHECK(!w.load_from_memory(wide.data(), wide.size(), &v, LevelLimits::format_ceiling()) && has_fatal(v, LevelProblemKind::GridTooLarge) && mentions(v, "more than the 256 the remake hosts"));
        // the original-safe profile: an Extended map is not for the original, no fault, no warning, no reading of the file
        const OriginalSafeReport by_bytes = check_original_safe(v9.data(), v9.size());
        CHECK(by_bytes.issues.size() == 1u && by_bytes.has(OriginalSafeCode::NotForOriginal) && by_bytes.issues[0].severity == LevelProblemSeverity::Info && by_bytes.not_for_original());
        CHECK(!by_bytes.works_in_original_game() && by_bytes.judged && by_bytes.fits_the_place());
        CHECK(by_bytes.issues[0].message == "not for the original game (needs a version 8 file): this is an Extended map, version 9, which only this remake loads");
        const OriginalSafeReport by_level = check_original_safe(extended_level());
        CHECK(by_level.issues.size() == 1u && by_level.not_for_original() && !by_level.works_in_original_game());
        const std::vector<uint8_t> four = {9, 0, 0, 0};
        CHECK(check_original_safe(four.data(), four.size(), extended_limits()).not_for_original());     // (by the version word alone, whatever the limits)
        CHECK(std::string(to_string(OriginalSafeCode::NotForOriginal)) == "not_for_original");
        CHECK(!check_original_safe(tiny.data(), tiny.size()).not_for_original() && check_original_safe(tiny.data(), tiny.size()).works_in_original_game());
    });

    run_case("W7.8 Hostile Headers: 65,535 x 65,535, 2^32-1, 65,536 x 65,536 (A Cell Count That Is 0 In 32 Bits), More Than The Ceiling Or The Limits Of The Place: Refused Before Any Allocation", [] {
        struct Dims {
            uint32_t rows, columns;
            const char* what;
        };
        const Dims dims[] = {{65535, 65535, "65535 x 65535"}, {0xFFFFFFFFu, 0xFFFFFFFFu, "2^32-1 x 2^32-1"}, {0xFFFFFFFFu, 1, "2^32-1 x 1"}, {1, 0xFFFFFFFFu, "1 x 2^32-1"},
                             {65536, 65536, "65536 x 65536 (2^32 cells: 0 in 32 bits)"}, {65536, 1, "65536 x 1"}, {2049, 1, "2049 x 1"}, {1, 2049, "1 x 2049"}, {2048, 2049, "2048 x 2049"},
                             {0x10001, 0x10000, "65537 x 65536"}, {0x80000000u, 2, "2^31 x 2"}};
        for (const Dims& d : dims) {
            for (const LevelLimits& limits : {LevelLimits::format_ceiling(), LevelLimits::for_extended_maps()}) {
                const std::vector<uint8_t> file = v9_bytes(0, 0, "", d.rows, d.columns, 0);
                LevelData level;
                LevelValidation v;
                bool loaded = true;
                const size_t held = peak_of([&] { loaded = level.load_from_memory(file.data(), file.size(), &v, limits); });
                CHECK_MSG(!loaded && has_fatal(v, LevelProblemKind::GridTooLarge) && is_empty_level(level), d.what);
                CHECK_MSG(held < kSlack, std::string(d.what) + ": " + std::to_string(held));
            }
        }
        // 2,048 x 2,048 is within the ceiling: with the bytes missing it is a truncated file, still without an allocation
        {
            const std::vector<uint8_t> file = v9_bytes(0, 0, "", 2048, 2048, 0);
            LevelData level;
            LevelValidation v;
            bool loaded = true;
            const size_t held = peak_of([&] { loaded = level.load_from_memory(file.data(), file.size(), &v, LevelLimits::format_ceiling()); });
            CHECK(!loaded && has_fatal(v, LevelProblemKind::Truncated) && mentions(v, "the layers") && held < kSlack);
            // and the places' limits refuse it by its grid: 4,194,304 cells are more than the 262,144 of an Extended map
            CHECK(!level.load_from_memory(file.data(), file.size(), &v, LevelLimits::for_extended_maps()) && has_fatal(v, LevelProblemKind::GridTooLarge));
        }
        // 1,024 x 256 and 512 x 512 are the 262,144 cells that an Extended map may have; one cell more is too many
        for (const auto& shape : {std::pair<uint32_t, uint32_t>{1024, 256}, std::pair<uint32_t, uint32_t>{512, 512}, std::pair<uint32_t, uint32_t>{256, 1024}, std::pair<uint32_t, uint32_t>{1, 1024}}) {
            const std::vector<uint8_t> file = v9_bytes(0, 0, "", shape.first, shape.second, static_cast<uint64_t>(shape.first) * shape.second);
            LevelData level;
            LevelValidation v;
            bool loaded = false;
            const size_t held = peak_of([&] { loaded = level.load_from_memory(file.data(), file.size(), &v, LevelLimits::for_extended_maps()); });
            CHECK_MSG(loaded && level.height == shape.first && level.width == shape.second, std::to_string(shape.first) + " x " + std::to_string(shape.second) + ": " + v.describe());
            CHECK_MSG(held <= reader_bound(file.size()), std::to_string(held) + " for " + std::to_string(file.size()));
        }
        for (const auto& shape : {std::pair<uint32_t, uint32_t>{1025, 1}, std::pair<uint32_t, uint32_t>{1, 1025}, std::pair<uint32_t, uint32_t>{513, 512}, std::pair<uint32_t, uint32_t>{1024, 257}}) {
            const std::vector<uint8_t> file = v9_bytes(0, 0, "", shape.first, shape.second, static_cast<uint64_t>(shape.first) * shape.second);
            LevelData level;
            LevelValidation v;
            CHECK_MSG(!level.load_from_memory(file.data(), file.size(), &v, LevelLimits::for_extended_maps()) && has_fatal(v, LevelProblemKind::GridTooLarge),
                      std::to_string(shape.first) + " x " + std::to_string(shape.second));
        }
        // a file of more than the limits' bytes is not read at all
        std::vector<uint8_t> five(5u * 1024u * 1024u, 0x09);
        LevelData level;
        LevelValidation v;
        const size_t held = peak_of([&] { level.load_from_memory(five.data(), five.size(), &v, LevelLimits::for_extended_maps()); });
        CHECK(has_fatal(v, LevelProblemKind::FileTooLarge) && mentions(v, "5242880 bytes, more than the 4194304 allowed here") && held < kSlack);
        std::vector<uint8_t> above(64u * 1024u * 1024u + 1u, 0x09);
        LevelLimits unlimited = LevelLimits::format_ceiling();
        unlimited.max_read_bytes = std::numeric_limits<size_t>::max();
        CHECK(!level.load_from_memory(above.data(), above.size(), &v, unlimited) && has_fatal(v, LevelProblemKind::FileTooLarge) && mentions(v, "more than the 67108864 allowed here"));
        // an empty grid is still a finding of its own
        const std::vector<uint8_t> empty = v9_bytes(0, 0, "", 0, 7, 0);
        CHECK(!level.load_from_memory(empty.data(), empty.size(), &v, extended_limits()) && has_fatal(v, LevelProblemKind::GridEmpty));
    });

    run_case("W7.9 Hostile Fields: Every Unknown Flag Bit, A Rules Word Above 1, A Description Of 256 Bytes Or More Than The File Holds, A Zero Byte In It; The Dictionary And The Blocks", [] {
        // the flags: each of the 32 bits alone, and all of them
        for (int bit = 0; bit < 33; ++bit) {
            const uint32_t flags = bit == 32 ? 0xFFFFFFFFu : (1u << bit);
            const std::vector<uint8_t> file = v9_bytes(flags, 0, "x", 1, 1, 1);
            LevelData level;
            LevelValidation v;
            CHECK_MSG(!level.load_from_memory(file.data(), file.size(), &v, extended_limits()) && has_fatal(v, LevelProblemKind::FormatFlagsUnknown) && is_empty_level(level), "flags bit " + std::to_string(bit));
            CHECK_MSG(mentions(v, "bits that this reader does not know") && mentions(v, "0x"), v.reason());
        }
        // the rules: 0 and 1 are the two; every other word is refused
        for (const uint32_t word : {0u, 1u, 2u, 3u, 255u, 256u, 0x8000u, 0xFFFFu}) {
            const std::vector<uint8_t> file = v9_bytes(0, word, "x", 1, 1, 1);
            LevelData level;
            LevelValidation v;
            const bool loaded = level.load_from_memory(file.data(), file.size(), &v, extended_limits());
            CHECK_MSG(loaded == (word <= 1), "rules " + std::to_string(word));
            if (word > 1) CHECK_MSG(has_fatal(v, LevelProblemKind::RulesOutOfRange) && mentions(v, "only 0 (the original's rules) and 1 (extended rules)"), v.reason());
            if (loaded) CHECK(level.rules == word);
        }
        // the description: 255 bytes are the most; 256 and the words above are refused before the bytes are looked for; a length that the file cannot hold is a cut file
        for (const size_t len : {size_t{0}, size_t{1}, size_t{255}}) {
            const std::vector<uint8_t> file = v9_bytes(0, 0, std::string(len, 'a'), 1, 1, 1);
            LevelData level;
            CHECK_MSG(level.load_from_memory(file.data(), file.size(), nullptr, extended_limits()) && level.description.size() == len, std::to_string(len));
        }
        for (const uint32_t len : {256u, 257u, 4096u, 0xFFFFu}) {
            const std::vector<uint8_t> file = v9_bytes(0, 0, std::string(len, 'a'), 1, 1, 1);
            LevelData level;
            LevelValidation v;
            CHECK_MSG(!level.load_from_memory(file.data(), file.size(), &v, extended_limits()) && has_fatal(v, LevelProblemKind::DescriptionTooLong) && mentions(v, "more than the 255 allowed here"),
                      std::to_string(len));
        }
        {
            const std::vector<uint8_t> file = v9_bytes(0, 0, "abc", 1, 1, 1, 200);       // the word says 200 bytes, the file has 3 and the rest of the layout
            LevelData level;
            LevelValidation v;
            const bool loaded = level.load_from_memory(file.data(), file.size(), &v, extended_limits());
            CHECK(!loaded);
            const std::vector<uint8_t> cut = v9_bytes(0, 0, "", 1, 1, 1, 255);
            CHECK(!level.load_from_memory(cut.data(), 30, &v, extended_limits()) && has_fatal(v, LevelProblemKind::Truncated) && mentions(v, "the file ends inside the description"));
        }
        {
            LevelLimits shorter = extended_limits();
            shorter.max_description_bytes = 10;
            const std::vector<uint8_t> file = v9_bytes(0, 0, "01234567890", 1, 1, 1);
            LevelData level;
            LevelValidation v;
            CHECK(!level.load_from_memory(file.data(), file.size(), &v, shorter) && has_fatal(v, LevelProblemKind::DescriptionTooLong) && mentions(v, "more than the 10 allowed here"));
        }
        {
            std::string with_zero = "ab";
            with_zero += '\0';
            with_zero += "cd";
            const std::vector<uint8_t> file = v9_bytes(0, 0, with_zero, 1, 1, 1);
            LevelData level;
            CHECK(level.load_from_memory(file.data(), file.size(), nullptr, extended_limits()) && level.description == "ab");          // read to its first zero byte, as in version 8
        }
        // the dictionary: a count that the file cannot hold, and one beyond the 1,344 names
        {
            std::vector<uint8_t> file = v9_bytes(0, 0, "", 1, 1, 1);
            wr16(file, 18, 0xFFFF);
            LevelData level;
            LevelValidation v;
            const size_t held = peak_of([&] { level.load_from_memory(file.data(), file.size(), &v, extended_limits()); });
            CHECK(has_fatal(v, LevelProblemKind::Truncated) && mentions(v, "tile dictionary") && held < kSlack);
            std::vector<uint8_t> many;
            put32(many, 9); put32(many, 1); put16(many, 6); put32(many, 0); put16(many, 0); put16(many, 0);
            put16(many, 1344);                                                                // 1,345 names: one too many
            for (int i = 0; i < 1345; ++i) put_text(many, "n", 11);
            put32(many, 1); put32(many, 1);
            many.insert(many.end(), 12, uint8_t{0});
            put16(many, 0); put16(many, 0); put16(many, 0); put16(many, LVL_EMPTY_TILE); put16(many, 0); put16(many, 6);
            CHECK(!level.load_from_memory(many.data(), many.size(), &v, extended_limits()) && has_fatal(v, LevelProblemKind::DictionaryTooLarge));
            wr16(many, 18, 1343);                                                             // 1,344 names: the most (the file holds one name more than it announces: the rest is block 1)
            CHECK(!level.load_from_memory(many.data(), many.size(), &v, extended_limits()) && !has_fatal(v, LevelProblemKind::DictionaryTooLarge));
        }
        // a version 9 file copies no quirk of the original's loader: a pile without stages, a cut inside block 4 and a missing egg word are files that are not whole
        {
            std::vector<uint8_t> file = v9_bytes(0, 0, "", 1, 1, 1);
            const size_t b2 = file.size() - 10;                                               // block 2's count word (then block 3, block 4's count and the egg word: 10 bytes)
            std::vector<uint8_t> pile(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(b2));
            put16(pile, 1);
            for (int k = 0; k < 5; ++k) put16(pile, 0);                                       // row, column, units, points and 0 stages
            put16(pile, 0); put16(pile, LVL_EMPTY_TILE); put16(pile, 0); put16(pile, 6);
            LevelData level;
            LevelValidation v;
            CHECK(!level.load_from_memory(pile.data(), pile.size(), &v, extended_limits()) && has_fatal(v, LevelProblemKind::FoodBlockEndedEarly) && mentions(v, "every food object with at least one stage"));
            CHECK(level.load_from_memory(file.data(), file.size(), &v, extended_limits()));
            CHECK(!level.load_from_memory(file.data(), file.size() - 1, &v, extended_limits()) && has_fatal(v, LevelProblemKind::Truncated) && mentions(v, "egg word"));
            std::vector<uint8_t> trailing = file;
            trailing.insert(trailing.end(), 100, 0x55);
            CHECK(level.load_from_memory(trailing.data(), trailing.size(), &v, extended_limits()));           // (bytes after the egg word are never read)
            const LevelWriteResult w = write_level(level, extended_limits());
            const std::vector<RewriteChange> changes = describe_rewrite(trailing.data(), trailing.size(), level, w.bytes);
            CHECK(w.ok && w.bytes == file && changes.size() == 1u && changes[0].kind == RewriteChangeKind::TrailingBytes && changes[0].count == 100u);
            CHECK(describe_rewrite(file.data(), file.size(), level, w.bytes).empty());
            std::vector<uint8_t> altered = file;
            altered[20] ^= 1;
            CHECK(describe_rewrite(altered.data(), altered.size(), level, w.bytes)[0].kind == RewriteChangeKind::Unexplained);
        }
    });

    run_case("W7.10 Every Cut Of A Version 9 File (Every Length From 0 To The Whole File): A Finding That Says Why, Never A Level, Nothing Held That Is Not Bounded", [&] {
        LevelData source;
        CHECK(source.load_from_memory(tiny.data(), tiny.size()));
        const std::vector<uint8_t> converted = write_level(convert_to_extended(source).level, extended_limits()).bytes;
        for (const std::vector<uint8_t>* file : {&converted}) {
            LevelData level;
            LevelValidation v;
            CHECK(level.load_from_memory(file->data(), file->size(), &v, extended_limits()));
            for (size_t cut = 0; cut < file->size(); ++cut) {
                bool loaded = true;
                const size_t held = peak_of([&] { loaded = level.load_from_memory(file->data(), cut, &v, extended_limits()); });
                if (loaded || v.first_fatal() == nullptr || held > reader_bound(cut)) {
                    CHECK_MSG(false, "cut at " + std::to_string(cut) + " of " + std::to_string(file->size()) + ": loaded " + std::to_string(loaded) + ", held " + std::to_string(held));
                    break;
                }
            }
        }
        const std::vector<uint8_t> hand = extended_bytes();
        for (size_t cut = 0; cut < hand.size(); ++cut) {
            LevelData level;
            LevelValidation v;
            const bool loaded = level.load_from_memory(hand.data(), cut, &v, extended_limits());
            CHECK_MSG(!loaded && v.first_fatal() != nullptr && !v.reason().empty() && is_empty_level(level), "the hand-made file cut at " + std::to_string(cut));
        }
        // the sentence of a cut says where
        LevelData level;
        LevelValidation v;
        CHECK(!level.load_from_memory(hand.data(), 12, &v, extended_limits()) && mentions(v, "the file ends inside the header"));
        CHECK(!level.load_from_memory(hand.data(), 18 + 100, &v, extended_limits()) && mentions(v, "the file ends inside the description"));
        CHECK(!level.load_from_memory(hand.data(), 18 + 130 + 2 + 20, &v, extended_limits()) && mentions(v, "the file ends inside the tile dictionary"));
        CHECK(!level.load_from_memory(hand.data(), 18 + 130 + 2 + 44 + 4, &v, extended_limits()) && mentions(v, "the file ends inside the grid size"));
        CHECK(!level.load_from_memory(hand.data(), 18 + 130 + 2 + 44 + 8 + 50, &v, extended_limits()) && mentions(v, "the file ends inside the layers"));
    });

    run_case("W7.11 Bytes Changed At Random In Version 9 Files (1,000 Files Each, Fixed Seeds, 1 To 8 Bytes): No Crash, A Reason For Each Refusal, Each Level Written And Read Back The Same, Twice The Same Bytes", [&] {
        LevelData source;
        CHECK(source.load_from_memory(small.data(), small.size()));
        const std::vector<uint8_t> converted = write_level(convert_to_extended(source).level, extended_limits()).bytes;
        const std::vector<uint8_t> hand = extended_bytes();
        uint32_t seed = 0x9A9A0001u;
        int loaded = 0;
        for (const std::vector<uint8_t>* base : {&hand, &converted}) {
            Lcg r(seed++);
            for (int i = 0; i < 1000; ++i) {
                std::vector<uint8_t> m = *base;
                const size_t flips = 1 + r.below(8);
                for (size_t k = 0; k < flips; ++k) {
                    // half of the changes land in the header and the first dictionary names, where the fields that the new version adds are
                    const size_t at = r.below(2) == 0 ? r.below(static_cast<uint32_t>(std::min<size_t>(m.size(), 80))) : r.below(static_cast<uint32_t>(m.size()));
                    m[at] = static_cast<uint8_t>(r.below(256));
                }
                LevelData level;
                loaded += level.load_from_memory(m.data(), m.size(), nullptr, LevelLimits::format_ceiling()) ? 1 : 0;
                hostile_extended(m.data(), m.size(), "file " + std::to_string(i));
            }
        }
        CHECK_MSG(loaded > 200, std::to_string(loaded) + " of the changed files load");
    });

    run_case("W7.12 The Heap: The Reader Holds At Most 7 Times Its File And 64 KiB For A Version 9 File (From A File 8), The Writer 8 Times; 512 x 512 (3.1 MB), 8,000 Waypoints Without Trigger", [&] {
        std::vector<std::string> names(1344, ".");
        const LevelData big = blank_extended_level(512, 512, names);
        LevelWriteResult w;
        size_t held = peak_of([&] { w = write_level(big, extended_limits()); });
        CHECK_MSG(w.ok && held <= writer_bound(w.bytes.size()), w.error + " / " + std::to_string(held));
        CHECK(w.ok && w.bytes.size() > 3u * 1000u * 1000u);
        LevelData back;
        LevelValidation v;
        held = peak_of([&] { back.load_from_memory(w.bytes.data(), w.bytes.size(), &v, extended_limits()); });
        CHECK_MSG(back.height == 512u && held <= reader_bound(w.bytes.size()) && v.parsed, std::to_string(held));
        const std::string path = scratch_path("test_lvl_writer_v9_big");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(w.bytes.data()), static_cast<std::streamsize>(w.bytes.size()));
        }
        LevelValidation from_file;
        held = peak_of([&] { from_file = LVLParser::check_file(path, 0x0F, extended_limits()); });
        CHECK_MSG(from_file.parsed && held <= kSlack + 8 * w.bytes.size(), std::to_string(held));
        std::error_code ec;
        std::filesystem::remove(path, ec);
        // 8,000 waypoints without a trigger: 8 bytes of file, 56 of memory each
        std::vector<uint8_t> file = v9_bytes(0, 0, "", 1, 1, 1);
        file.resize(file.size() - 4);                                                       // block 4's count and the egg word go
        put16(file, 8000);
        for (int i = 0; i < 8000; ++i) { put16(file, 0); put16(file, 0); put32(file, 0); }
        put16(file, 6);
        LevelData level;
        held = peak_of([&] { level.load_from_memory(file.data(), file.size(), &v, extended_limits()); });
        CHECK_MSG(level.waypoints.size() == 8000u && held <= reader_bound(file.size()) && held >= 7 * file.size() / 2, std::to_string(held));
        // a block 2 that announces 65,535 piles and holds one byte of them
        std::vector<uint8_t> piles = v9_bytes(0, 0, "", 1, 1, 1);
        piles.resize(piles.size() - 10);                                                    // (block 2's count word and what follows it go)
        put16(piles, 65535);
        piles.insert(piles.end(), 4, uint8_t{0});
        held = peak_of([&] { level.load_from_memory(piles.data(), piles.size(), &v, extended_limits()); });
        CHECK_MSG(!v.parsed && held < kSlack, std::to_string(held));
    });

    run_case("W7.13 Round Trip Of A Bigger Extended Map With Objects Everywhere: 300 x 700, Anchors Of -128 To 127, Read Back The Same, Written Twice The Same Bytes", [] {
        LevelData l;
        l.version = 9;
        l.rules = 1;
        l.game_mode = 1;
        l.default_minutes = 12;
        l.description = "300 x 700";
        l.tile_dictionary = {".", "ground", "tree"};
        l.tile_type_count = 2;
        l.height.val = 300;
        l.width.val = 700;
        Lcg r(0x300700u);
        const size_t cells = 300u * 700u;
        for (size_t i = 0; i < cells; ++i) {
            l.layer1_terrain.push_back(MapCell{1, 0, 0xCDCD});
            l.layer2_interactive.push_back(r.below(5) == 0 ? MapCell{2, 0, r.word()} : MapCell{LVL_EMPTY_TILE, 0, 0});
        }
        const LevelWriteResult w = write_level(l, extended_limits());
        CHECK_MSG(w.ok, w.error);
        LevelData back;
        CHECK(back.load_from_memory(w.bytes.data(), w.bytes.size(), nullptr, extended_limits()));
        std::string why;
        CHECK_MSG(same_level(l, back, &why), why);
        CHECK(write_level(back, extended_limits()).bytes == w.bytes);
        const LevelConversion c = convert_to_classic(l);
        CHECK(!c.ok);
        CHECK(c.reasons.size() == 3u && c.reasons[0].find("700 columns wide") != std::string::npos && c.reasons[1].find("300 rows tall") != std::string::npos &&
              c.reasons[2].find("object cells are anchored outside the 0 to 255") != std::string::npos);
    });
}

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
    w7_extended_maps();
    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

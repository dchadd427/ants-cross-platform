// The map core behind plain data (include/ants_assets/lvl_core.hpp, src/ants_assets/lvl_core_c.cpp): the one code that the browser module lvl_core.wasm is built from.
//   C1 the tile names of the core are the names of ants.chd's Table 4
//   C2 the C interface: the result block, memory, a profile that does not exist, a null buffer
//   C3 the library: for each file of the set, the reports, the original-safe findings and the rewrite are those that tests/data/lvl_core/ holds (the expectation that the
//      Node test tests/scripts/lvl_core_check.js holds the browser module to, byte for byte), the findings are the lines lvl_tool prints, the rewrite reads back as the
//      same level and is what write_level makes
//   C4 hostile files (empty, one byte, cut, 513 KB, huge counts, random bytes): an error report, never a throw
//   C5 the blank map: sizes, refusals, the findings of a map with no hill
// `test_lvl_core --write-expected` writes the expectation again (after a change of the core that is meant), `--print-tile-names` prints src/ants_assets/lvl_tile_names.inc.
#include "ants_assets/chd_parser.hpp"
#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
uint8_t* lvl_alloc(uint32_t size);
void lvl_free(void* p);
uint8_t* lvl_read_report(const uint8_t* data, uint32_t size, uint32_t profile);
uint8_t* lvl_check_original(const uint8_t* data, uint32_t size, uint32_t profile);
uint8_t* lvl_rewrite(const uint8_t* data, uint32_t size, uint32_t profile);
uint8_t* lvl_blank(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, uint32_t profile);
uint8_t* lvl_same_level(const uint8_t* a, uint32_t size_a, const uint8_t* b, uint32_t size_b, uint32_t profile);
uint32_t lvl_result_status(const uint8_t* result);
uint32_t lvl_result_size(const uint8_t* result);
const uint8_t* lvl_result_data(const uint8_t* result);
uint32_t lvl_interface_version();
}

namespace {

using namespace ants::assets;
using Bytes = std::vector<uint8_t>;

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

bool g_write = false;   // --write-expected

std::string expected_dir() { return std::string(TEST_DATA_DIR) + "/lvl_core"; }

bool read_file(const std::string& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool read_text(const std::string& path, std::string& out) {
    Bytes b;
    if (!read_file(path, b)) return false;
    out.assign(b.begin(), b.end());
    return true;
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// FNV-1a, 64 bits, as 16 hex digits (the Node test computes the same)
std::string fnv(const std::string& bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : bytes) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001b3ull;
    }
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h));
    return text;
}

// What a call of the C interface answered: status and payload, the block given back
struct Answer {
    uint32_t status{99};
    std::string payload;
    bool null{true};
};

Answer take(uint8_t* block) {
    Answer a;
    if (block == nullptr) return a;
    a.null = false;
    a.status = lvl_result_status(block);
    a.payload.assign(reinterpret_cast<const char*>(lvl_result_data(block)), lvl_result_size(block));
    lvl_free(block);
    return a;
}

Answer c_read(const Bytes& b, uint32_t profile = 0) { return take(lvl_read_report(b.data(), static_cast<uint32_t>(b.size()), profile)); }
Answer c_check(const Bytes& b, uint32_t profile = 0) { return take(lvl_check_original(b.data(), static_cast<uint32_t>(b.size()), profile)); }
Answer c_rewrite(const Bytes& b, uint32_t profile = 0) { return take(lvl_rewrite(b.data(), static_cast<uint32_t>(b.size()), profile)); }
Answer c_same(const Bytes& a, const Bytes& b, uint32_t profile = 0) {
    return take(lvl_same_level(a.data(), static_cast<uint32_t>(a.size()), b.data(), static_cast<uint32_t>(b.size()), profile));
}

Bytes as_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

// ---- the set of files ----

struct FileCase {
    std::string id;
    std::string path;     // relative to the checkout
    bool hostile{false};
    Bytes bytes;
};

Bytes read_checked(const std::string& relative) {
    Bytes b;
    if (!read_file(std::string(ANTS_SOURCE_DIR) + "/" + relative, b)) {
        std::cout << "cannot read " << relative << "\n";
        std::exit(2);
    }
    return b;
}

void put32(Bytes& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

Bytes random_bytes(uint32_t seed, size_t n) {   // xorshift32, the Node test has the same
    Bytes b(n);
    uint32_t x = seed;
    for (size_t i = 0; i < n; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b[i] = static_cast<uint8_t>(x & 0xFF);
    }
    return b;
}

// The files of the library that the tests read: a 31 x 31 and a 40 x 40 and 60 x 60 original, a 60 x 60 that does not show a food pile, a 31 x 31 whose description has 30
// bytes, a file cut short in block 4 with a 30-byte description, the largest file (235,594 bytes: mostly bytes after the egg word), the largest grid (81 x 100) and a hill without
// an entrance
std::vector<FileCase> library_cases() {
    const std::vector<std::pair<std::string, std::string>> files = {
        {"tiny", "Original-Ants/Maps/TINY.LVL"},
        {"small", "Original-Ants/Maps/SMALL.LVL"},
        {"medium", "Original-Ants/Maps/MEDIUM.LVL"},
        {"turtleworld", "Community-Maps/!TurtleWorld.lvl"},
        {"party", "Community-Maps/ANTS_PARTY-V.3.0.5.lvl"},
        {"conquer", "Community-Maps/Conquer.lvl"},
        {"bomberworld", "Community-Maps/BOMBERWORLD.lvl"},
        {"easyjungle", "Community-Maps/Easy jungle.lvl"},
        {"icon", "Community-Maps/IcoN.LVL"},
    };
    std::vector<FileCase> cases;
    for (const auto& f : files) cases.push_back(FileCase{f.first, f.second, false, read_checked(f.second)});
    return cases;
}

std::vector<FileCase> hostile_cases() {
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    std::vector<FileCase> cases;
    auto add = [&](const std::string& id, Bytes b) { cases.push_back(FileCase{"hostile_" + id, "-", true, std::move(b)}); };
    add("empty", Bytes());
    add("one_byte", Bytes{8});
    add("cut_in_header", Bytes(tiny.begin(), tiny.begin() + 20));
    add("cut_in_dictionary", Bytes(tiny.begin(), tiny.begin() + 3000));
    add("cut_in_layers", Bytes(tiny.begin(), tiny.begin() + 12000));
    add("cut_in_half", Bytes(tiny.begin(), tiny.begin() + static_cast<std::ptrdiff_t>(tiny.size() / 2)));
    add("zeros_513_kb", Bytes(513u * 1024u, 0));
    Bytes huge_dictionary = tiny;                       // the dictionary announces 65,536 names
    huge_dictionary[40] = 0xFF;
    huge_dictionary[41] = 0xFF;
    add("huge_dictionary", huge_dictionary);
    Bytes huge_grid = tiny;                             // 4,294,967,295 rows and columns (TINY's grid size is at 7412)
    put32(huge_grid, 7412, 0xFFFFFFFFu);
    put32(huge_grid, 7416, 0xFFFFFFFFu);
    add("huge_grid", huge_grid);
    Bytes huge_block = tiny;                            // block 1 announces 65,535 records (TINY's block 1 starts at 18952)
    huge_block[18952] = 0xFF;
    huge_block[18953] = 0xFF;
    add("huge_block_1", huge_block);
    add("random_1", random_bytes(1, 4096));
    add("random_2", random_bytes(2, 4096));
    add("random_3", random_bytes(3, 100000));
    add("version_8_then_random", [&] { Bytes b = random_bytes(4, 30000); b[0] = 8; b[1] = b[2] = b[3] = 0; return b; }());
    return cases;
}

// The lines that lvl_tool check --original --new-map prints for the findings of a file (tools/lvl_tool.cpp, check_original)
std::vector<std::string> lvl_tool_finding_lines(const Bytes& b) {
    const OriginalSafeReport r = check_original_safe(b.data(), b.size(), LevelLimits::for_new_maps());
    std::vector<std::string> lines;
    for (const OriginalSafeIssue& i : r.issues) lines.push_back(std::string("  ") + (i.place_limit ? "limit" : to_string(i.severity)) + ' ' + to_string(i.code) + ": " + i.message);
    return lines;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

// Compares `text` with the expectation file `name` (or writes it with --write-expected)
void expect_text(const std::string& name, const std::string& text) {
    const std::string path = expected_dir() + "/" + name;
    if (g_write) {
        write_text(path, text);
        return;
    }
    std::string stored;
    CHECK_MSG(read_text(path, stored), "the expectation " + name + " is there");
    CHECK_MSG(stored == text, "the answer of the core is the stored expectation " + name);
}

std::string rewrite_line(const FileCase& c, const Answer& rw) {
    return c.id + "\t" + c.path + "\t" + std::to_string(rw.status) + "\t" + (rw.status == 0 ? std::to_string(rw.payload.size()) + "\t" + fnv(rw.payload) : "-\t" + rw.payload);
}

// ---- C1 ----
void c1_names() {
    const std::vector<std::string> names = lvl_core::tile_names();
    CHECK(names.size() == 1344);
    Bytes chd;
    CHECK(read_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd", chd));
    CHECK_MSG(tile_names_from_chd(chd.data(), chd.size()) == names, "lvl_tile_names.inc is the list of Table 4's names (test_lvl_core --print-tile-names makes it again)");
    CHECK(names[485] == "g01a");
}

// ---- C2 ----
void c2_interface() {
    CHECK(lvl_interface_version() == 1);
    lvl_free(nullptr);
    uint8_t* zero = lvl_alloc(0);
    CHECK(zero != nullptr);
    lvl_free(zero);
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    uint8_t* block = lvl_read_report(tiny.data(), static_cast<uint32_t>(tiny.size()), 0);
    CHECK(block != nullptr);
    if (block != nullptr) {
        CHECK(lvl_result_status(block) == 0);
        CHECK(lvl_result_size(block) > 100);
        CHECK(lvl_result_data(block) == block + 8);
        lvl_free(block);
    }
    const Answer bad_profile = c_read(tiny, 7);                 // a profile that does not exist is a refusal, not a guess
    CHECK(bad_profile.status == 1 && bad_profile.payload == "unknown profile");
    const Answer null_data = take(lvl_read_report(nullptr, 0, 0));
    CHECK(null_data.status == 0 && null_data.payload.find("\"loaded\":false") != std::string::npos);
    const Answer same_null = take(lvl_same_level(nullptr, 0, nullptr, 0, 0));
    CHECK(same_null.status == 1);
    // the same call twice gives the same bytes: no state is kept between calls
    CHECK(c_read(tiny).payload == c_read(tiny).payload);
    const Answer game = c_read(tiny, 1);
    CHECK(game.status == 0 && game.payload.find("\"profile\":\"game\"") != std::string::npos);
}

// ---- C3 ----
void c3_library() {
    std::ostringstream index;
    for (const FileCase& c : library_cases()) {
        const Answer read = c_read(c.bytes);
        const Answer chk = c_check(c.bytes);
        const Answer rw = c_rewrite(c.bytes);
        CHECK_MSG(read.status == 0 && chk.status == 0, c.id);
        // the same text the C++ functions make (the C interface adds the block and nothing else)
        CHECK(read.payload == lvl_core::read_report(c.bytes.data(), c.bytes.size(), lvl_core::Profile::NewMap).payload);
        CHECK(chk.payload == lvl_core::check_original(c.bytes.data(), c.bytes.size(), lvl_core::Profile::NewMap).payload);
        expect_text(c.id + ".read.json", read.payload);
        expect_text(c.id + ".check.json", chk.payload);
        index << rewrite_line(c, rw) << "\n";
        // the rewrite is what write_level makes of the level the reader makes, and reads back as the same level
        LevelData level;
        LevelValidation report;
        CHECK_MSG(level.load_from_memory(c.bytes.data(), c.bytes.size(), &report, LevelLimits::for_new_maps()), c.id);
        const LevelWriteResult direct = write_level(level, LevelLimits::for_new_maps());
        CHECK_MSG(rw.status == 0 && direct.ok && as_bytes(rw.payload) == direct.bytes, c.id + ": the rewrite is write_level's bytes");
        const Answer same = c_same(c.bytes, as_bytes(rw.payload));
        CHECK_MSG(same.status == 0 && same.payload == "{\"same\":true}", c.id + ": the rewrite reads back as the same level " + same.payload);
        CHECK_MSG(c_rewrite(as_bytes(rw.payload)).payload == rw.payload, c.id + ": rewriting the rewrite changes nothing");
        // the findings are lvl_tool's lines, in lvl_tool's words: the stored output of `lvl_tool check FILE --original --new-map` holds exactly them
        std::string tool;
        CHECK(read_text(expected_dir() + "/" + c.id + ".lvl_tool.txt", tool));
        std::vector<std::string> findings;
        for (const std::string& l : split_lines(tool)) {
            if (l.rfind("  ", 0) == 0) findings.push_back(l);
        }
        CHECK_MSG(findings == lvl_tool_finding_lines(c.bytes), c.id + ": the findings are the lines lvl_tool prints");
    }
    if (g_write) write_text(expected_dir() + "/index.txt", index.str());
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/index.txt", stored));
        std::string made;
        for (const FileCase& c : library_cases()) made += rewrite_line(c, c_rewrite(c.bytes)) + "\n";
        CHECK_MSG(stored.substr(0, made.size()) == made, "the rewrites are the stored ones (index.txt)");
    }
    // a file that is not the set's: the two reads of one map differ in "same"
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    const Bytes small = read_checked("Original-Ants/Maps/SMALL.LVL");
    const Answer different = c_same(tiny, small);
    CHECK(different.status == 0 && different.payload.rfind("{\"same\":false,\"difference\":\"", 0) == 0);
}

// ---- C4 ----
void c4_hostile() {
    std::ostringstream index;
    for (const FileCase& c : hostile_cases()) {
        const Answer read = c_read(c.bytes);
        const Answer chk = c_check(c.bytes);
        const Answer rw = c_rewrite(c.bytes);
        const Answer same = c_same(c.bytes, c.bytes);
        CHECK_MSG(!read.null && !chk.null && !rw.null && !same.null, c.id + ": an answer");
        CHECK_MSG(read.status == 0 && read.payload.find("\"loaded\":false") != std::string::npos, c.id + ": the report says it does not load");
        CHECK_MSG(chk.status == 0 && chk.payload.find("\"works_in_original_game\":false") != std::string::npos, c.id + ": it does not work in the original game");
        CHECK_MSG(rw.status == 1, c.id + ": no rewrite (" + rw.payload + ")");
        CHECK_MSG(same.status == 1, c.id + ": no comparison");
        expect_text(c.id + ".read.json", read.payload);
        expect_text(c.id + ".check.json", chk.payload);
        index << rewrite_line(c, rw) << "\n";
    }
    if (g_write) write_text(expected_dir() + "/hostile_index.txt", index.str());
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/hostile_index.txt", stored));
        std::string made;
        for (const FileCase& c : hostile_cases()) made += rewrite_line(c, c_rewrite(c.bytes)) + "\n";
        CHECK_MSG(stored == made, "the refusals are the stored ones (hostile_index.txt)");
    }
}

// ---- C5 ----
struct BlankCase {
    uint32_t rows, columns, ground, eggs, minutes;
};

const std::vector<BlankCase>& blank_cases() {
    static const std::vector<BlankCase> cases = {
        {31, 31, 485, 6, 10}, {40, 40, 485, 6, 10}, {60, 60, 485, 6, 10}, {100, 100, 485, 6, 10},   // a new map's sizes, up to the 10,000 cells
        {31, 31, 486, 9, 12},                                                                          // another ground, eggs and minutes
        {1, 1, 485, 6, 10},                                                                            // the smallest
        {101, 100, 485, 6, 10}, {131, 10, 485, 6, 10}, {10, 131, 485, 6, 10},                          // beyond the limits of a new map
        {0, 10, 485, 6, 10}, {10, 0, 485, 6, 10}, {70000, 10, 485, 6, 10},                             // no cell, more than a word
        {31, 31, 1344, 6, 10}, {31, 31, 32766, 6, 10},                                                 // a tile without a name, the empty tile
        {31, 31, 485, 70000, 10}, {31, 31, 485, 6, 70000},                                             // more than a word
    };
    return cases;
}

std::string blank_id(const BlankCase& c) { return "blank_" + std::to_string(c.rows) + "x" + std::to_string(c.columns) + "_g" + std::to_string(c.ground) + "_e" + std::to_string(c.eggs) + "_m" + std::to_string(c.minutes); }

void c5_blank() {
    std::string made;
    for (const BlankCase& c : blank_cases()) {
        const Answer a = take(lvl_blank(c.rows, c.columns, c.ground, c.eggs, c.minutes, 0));
        made += blank_id(c) + "\t" + std::to_string(a.status) + "\t" + (a.status == 0 ? std::to_string(a.payload.size()) + "\t" + fnv(a.payload) : "-\t" + a.payload) + "\n";
        if (a.status != 0) continue;
        const Bytes bytes = as_bytes(a.payload);
        // the bytes read back as the map that was asked for
        LevelData level;
        LevelValidation report;
        CHECK_MSG(level.load_from_memory(bytes.data(), bytes.size(), &report, LevelLimits::for_new_maps()), blank_id(c));
        CHECK(level.height() == c.rows && level.width() == c.columns && level.boundary_param == c.eggs && level.default_minutes == c.minutes);
        CHECK(!level.layer1_terrain.empty() && level.layer1_terrain.front().tile_index == c.ground);
        CHECK(level.tile_dictionary.size() == c.ground + 1u && level.tile_dictionary[c.ground] == lvl_core::tile_names()[c.ground]);
        // ... and holds the findings of a map with no hill and no start marker, and nothing else
        const OriginalSafeReport r = check_original_safe(bytes.data(), bytes.size(), LevelLimits::for_new_maps());
        CHECK_MSG(r.fits_the_place(), blank_id(c) + ": fits the place");
        CHECK_MSG(!r.works_in_original_game() && r.has(OriginalSafeCode::HillMissing) && r.has(OriginalSafeCode::StartingAntsMissing), blank_id(c) + ": no hill, no starting ants");
        for (const OriginalSafeIssue& i : r.issues) {
            CHECK_MSG(i.code == OriginalSafeCode::HillMissing || i.code == OriginalSafeCode::StartingAntsMissing, blank_id(c) + ": only missing things: " + to_string(i.code));
        }
        if (c.ground == 485 && c.eggs == 6 && c.minutes == 10) {                 // the same bytes as blank_level + write_level (the library's own blank map)
            const LevelWriteResult w = write_level(blank_level(static_cast<uint16_t>(c.rows), static_cast<uint16_t>(c.columns), lvl_core::tile_names(), LevelLimits::for_new_maps()), LevelLimits::for_new_maps());
            CHECK_MSG(w.ok && w.bytes == bytes, blank_id(c) + ": blank_level's bytes");
        }
        if (c.rows == c.columns && c.ground == 485 && c.eggs == 6 && c.rows >= 31) {
            expect_text(blank_id(c) + ".read.json", c_read(bytes).payload);
            expect_text(blank_id(c) + ".check.json", c_check(bytes).payload);
        }
    }
    if (g_write) write_text(expected_dir() + "/blank_index.txt", made);
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/blank_index.txt", stored));
        CHECK_MSG(stored == made, "the blank maps are the stored ones (blank_index.txt)");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--write-expected") == 0) g_write = true;
        else if (std::strcmp(argv[i], "--print-tile-names") == 0) {
            Bytes chd;
            if (!read_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd", chd)) return 2;
            for (const std::string& n : tile_names_from_chd(chd.data(), chd.size())) std::cout << '"' << n << "\",\n";
            return 0;
        } else {
            std::cout << "usage: test_lvl_core [--write-expected | --print-tile-names]\n";
            return 2;
        }
    }
    std::cout << "=======================================================\n"
              << " The map core behind plain data (lvl_core)\n"
              << "=======================================================\n";
    run_case("C1 the tile names", c1_names);
    run_case("C2 the C interface", c2_interface);
    run_case("C3 the library files", c3_library);
    run_case("C4 hostile files", c4_hostile);
    run_case("C5 the blank map", c5_blank);
    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

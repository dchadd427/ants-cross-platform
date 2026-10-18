// The map core behind plain data (include/ants_assets/lvl_core.hpp, src/ants_assets/lvl_core_c.cpp): the one code that the browser module lvl_core.wasm is built from.
//   C1 the tile names of the core are the names of ants.chd's Table 4
//   C2 the C interface: the result block, memory, a profile that does not exist, a null buffer
//   C3 the library: for each file of the set, the reports, the original-safe findings and the rewrite are those that tests/data/lvl_core/ holds (the expectation that the
//      Node test tests/scripts/lvl_core_check.js holds the browser module to, byte for byte), the findings are the lines lvl_tool prints, the rewrite reads back as the
//      same level and is what write_level makes
//   C4 hostile files (empty, one byte, cut, 513 KB, huge counts, random bytes): an error report, never a throw
//   C5 the blank map: sizes, refusals, the findings of a map with no hill
//   C6 no memory left: every allocation made to fail in turn, in every call: a status 2, never an exception out of a call
//   C7 the Extended profile and the limits of each profile (interface version 2)
//   C8 the check by kind (check_map): the library as Classic maps and as Extended maps, an Extended map in a place that takes Classic maps only
//   C9 the conversions: a Classic map goes up and comes back as its canonical bytes (the library), the reasons and the notes with their codes, a copy that is another file
//   C10 the blank Extended map, the reasons a map needs to be Extended, hostile Extended files
//   C11 the properties (interface version 3): the values a file has set again give the very same bytes (the whole library, as Classic and as Extended maps), setting and restoring, the
//       edits, the limits of the description (the checker's own sentence), every refusal, the check of the map after it
// `test_lvl_core --write-expected` writes the expectation again (after a change of the core that is meant), `--print-tile-names` prints src/ants_assets/lvl_tile_names.inc.
#include "ants_assets/chd_parser.hpp"
#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <new>
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
uint8_t* lvl_check_map(const uint8_t* data, uint32_t size, uint32_t profile);
uint8_t* lvl_convert_to_extended(const uint8_t* data, uint32_t size);
uint8_t* lvl_convert_to_classic(const uint8_t* data, uint32_t size, uint32_t profile);
uint8_t* lvl_needs_extended(const uint8_t* data, uint32_t size, uint32_t profile);
uint8_t* lvl_blank_extended(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes);
uint8_t* lvl_set_properties(const uint8_t* data, uint32_t size, const uint8_t* description, uint32_t description_size, uint32_t minutes, uint32_t eggs, uint32_t default_ant_type, uint32_t profile);
uint8_t* lvl_limits(uint32_t profile);
uint32_t lvl_result_status(const uint8_t* result);
uint32_t lvl_result_size(const uint8_t* result);
const uint8_t* lvl_result_data(const uint8_t* result);
uint32_t lvl_interface_version();
}

// Fault injection (C6): every allocation after the first `g_allow` ones fails with std::bad_alloc (the memory has run out and stays out) while g_allow is not negative
static long g_allow = -1;
static void (*volatile g_release)(void*) = std::free;     // (called through a pointer: a compiler that sees free() beside new warns of a mismatch)

static unsigned long long g_bytes = 0;      // every byte asked of operator new so far (a test reads the difference)

void* operator new(std::size_t n) {
    if (g_allow == 0) throw std::bad_alloc();
    if (g_allow > 0) --g_allow;
    g_bytes += n;
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
// the nothrow forms (std::stable_partition asks for its buffer with one) are replaced too, so that new and delete of the program always pair up (AddressSanitizer: alloc-dealloc-mismatch)
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (g_allow == 0) return nullptr;
    if (g_allow > 0) --g_allow;
    g_bytes += n;
    return std::malloc(n == 0 ? 1 : n);
}
void operator delete(void* p) noexcept { g_release(p); }
void operator delete(void* p, std::size_t) noexcept { g_release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { g_release(p); }

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

Answer c_check_map(const Bytes& b, uint32_t profile = 0) { return take(lvl_check_map(b.data(), static_cast<uint32_t>(b.size()), profile)); }
Answer c_needs(const Bytes& b, uint32_t profile = 0) { return take(lvl_needs_extended(b.data(), static_cast<uint32_t>(b.size()), profile)); }
Answer c_limits(uint32_t profile) { return take(lvl_limits(profile)); }

// What a conversion answered: the status, the JSON of the answer (the sentence of a refusal) and the new file
struct Conv {
    uint32_t status{99};
    std::string json;
    Bytes bytes;
};

Conv unpack(const Answer& a) {
    Conv c;
    c.status = a.status;
    if (a.status != 0) {
        c.json = a.payload;
        return c;
    }
    const std::string& p = a.payload;
    uint32_t n = 0;
    if (p.size() >= 4) n = static_cast<uint8_t>(p[0]) | static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8 | static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24;
    if (p.size() < 4u + n) {
        c.status = 98;
        return c;
    }
    c.json = p.substr(4, n);
    c.bytes = Bytes(p.begin() + 4 + n, p.end());
    return c;
}

Conv c_to_extended(const Bytes& b) { return unpack(take(lvl_convert_to_extended(b.data(), static_cast<uint32_t>(b.size())))); }
Conv c_to_classic(const Bytes& b, uint32_t profile = 0) { return unpack(take(lvl_convert_to_classic(b.data(), static_cast<uint32_t>(b.size()), profile))); }

Bytes up_of(const Bytes& classic) { return c_to_extended(classic).bytes; }

// The properties call: the description as bytes
Conv c_set(const Bytes& b, const std::string& description, uint32_t minutes, uint32_t eggs, uint32_t type, uint32_t profile = 2) {
    return unpack(take(lvl_set_properties(b.data(), static_cast<uint32_t>(b.size()), reinterpret_cast<const uint8_t*>(description.data()), static_cast<uint32_t>(description.size()), minutes, eggs, type, profile)));
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

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
    CHECK(lvl_interface_version() == 3);
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

// ---- C6 ----
// No memory left: whichever allocation fails, in whichever call, the answer is a status (2 when the memory ran out, else the answer), never an exception that escapes
// (guarded() is noexcept: a throw out of its handler would end the program, which this test would show as a crash)
void c6_no_memory() {
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    const Bytes tiny9 = up_of(tiny);
    const std::string new_text = "A new text, 20 bytes";
    const std::string too_long(256, 'x');
    int faults = 0;
    for (long allow = 0; allow < 400; allow += (allow < 60 ? 1 : 7)) {
        for (int call = 0; call < 14; ++call) {
            g_allow = allow;
            lvl_core::Result r;
            switch (call) {
                case 0: r = lvl_core::read_report(tiny.data(), tiny.size(), lvl_core::Profile::NewMap); break;
                case 1: r = lvl_core::check_original(tiny.data(), tiny.size(), lvl_core::Profile::NewMap); break;
                case 2: r = lvl_core::rewrite(tiny.data(), tiny.size(), lvl_core::Profile::NewMap); break;
                case 3: r = lvl_core::blank(31, 31, 485, 6, 10, lvl_core::Profile::NewMap); break;
                case 4: r = lvl_core::same_level(tiny.data(), tiny.size(), tiny.data(), tiny.size(), lvl_core::Profile::NewMap); break;
                case 5: r = lvl_core::check_map(tiny9.data(), tiny9.size(), lvl_core::Profile::Extended); break;
                case 6: r = lvl_core::convert_to_extended(tiny.data(), tiny.size()); break;
                case 7: r = lvl_core::convert_to_classic(tiny9.data(), tiny9.size(), lvl_core::Profile::NewMap); break;
                case 8: r = lvl_core::needs_extended(tiny9.data(), tiny9.size(), lvl_core::Profile::NewMap); break;
                case 9: r = lvl_core::blank_extended(31, 31, 485, 6, 10); break;
                case 10: r = lvl_core::profile_limits(lvl_core::Profile::Extended); break;
                case 11: r = lvl_core::set_properties(tiny.data(), tiny.size(), reinterpret_cast<const uint8_t*>(new_text.data()), new_text.size(), 7, 3, 63, lvl_core::Profile::NewMap); break;
                case 12: r = lvl_core::set_properties(tiny9.data(), tiny9.size(), reinterpret_cast<const uint8_t*>(too_long.data()), too_long.size(), 7, 3, 63, lvl_core::Profile::Extended); break;     // (refused: too long)
                default: r = lvl_core::check_map(tiny.data(), tiny.size(), lvl_core::Profile::NewMap); break;
            }
            g_allow = -1;
            CHECK(r.status == 0 || r.status == 1 || r.status == 2);
            if (r.status == 2) {
                ++faults;
                CHECK_MSG(r.payload.empty() || r.payload == "out of memory", r.payload);
            }
        }
    }
    g_allow = -1;
    // a grid beyond the Extended limits is refused (status 1) before its cells are made: nothing of its size is asked of the heap (the limits are the Extended ones, not the format's ceiling)
    const unsigned long long before = g_bytes;
    const lvl_core::Result huge = lvl_core::blank_extended(2048, 2048, 485, 6, 10);
    const lvl_core::Result tall = lvl_core::blank_extended(1025, 10, 485, 6, 10);
    CHECK_MSG(huge.status == 1 && tall.status == 1, "beyond the Extended limits: " + huge.payload.substr(0, 60));
    CHECK_MSG(g_bytes - before < 4000000ull, "no cell of a refused grid is made: " + std::to_string(g_bytes - before) + " bytes");
    for (const long allow : {0L, 1L, 5L, 50L, 400L}) {
        g_allow = allow;
        const lvl_core::Result r = lvl_core::blank_extended(2048, 2048, 485, 6, 10);
        g_allow = -1;
        CHECK_MSG(r.status == 1 || r.status == 2, "refused or out of memory, never an answer: " + std::to_string(r.status));
    }
    CHECK_MSG(faults > 200, "the injected failures were met: " + std::to_string(faults));
    // a text beyond what a file can hold is refused before it is copied: its size is all the sentence needs (a megabyte of it asks the heap for no megabyte)
    const std::string long_text(1u << 20, 'x');
    const unsigned long long asked = g_bytes;
    const lvl_core::Result many = lvl_core::set_properties(tiny9.data(), tiny9.size(), reinterpret_cast<const uint8_t*>(long_text.data()), long_text.size(), 7, 3, 63, lvl_core::Profile::Extended);
    CHECK_MSG(many.status == 0 && contains(many.payload, "\"ok\":false") && contains(many.payload, "description_too_long"), many.payload.substr(0, 80));
    CHECK_MSG(g_bytes - asked < 2u * (1u << 20) , "a refused text is not copied: " + std::to_string(g_bytes - asked) + " bytes");
}

// ---- C5 ----
struct BlankCase {
    uint32_t rows, columns, ground, eggs, minutes;
};

const std::vector<BlankCase>& blank_cases() {
    static const std::vector<BlankCase> cases = {
        {31, 31, 485, 6, 10}, {40, 40, 485, 6, 10}, {60, 60, 485, 6, 10}, {90, 90, 485, 6, 10},     // a new Classic map's sizes, up to its 90 rows
        {31, 31, 486, 9, 12},                                                                          // another ground, eggs and minutes
        {1, 1, 485, 6, 10},                                                                            // the smallest
        {91, 10, 485, 6, 10}, {100, 100, 485, 6, 10}, {131, 10, 485, 6, 10}, {10, 131, 485, 6, 10},   // beyond the limits of a new map (91 rows: the wall at row 90)
        {90, 112, 485, 6, 10},                                                                         // 10,080 cells
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


// ---- C7 ----
// The Extended profile: a place that takes Extended maps. The limits of each profile are the numbers of the code (the page shows them), the same text native and in the module.
void c7_profiles() {
    const char* const names[3] = {"new_map", "game", "extended"};
    for (uint32_t p = 0; p < 3; ++p) {
        const Answer a = c_limits(p);
        CHECK_MSG(a.status == 0 && contains(a.payload, std::string("\"profile\":\"") + names[p] + "\""), a.payload);
        CHECK(a.payload == lvl_core::profile_limits(static_cast<lvl_core::Profile>(p)).payload);
        expect_text(std::string("limits_") + names[p] + ".json", a.payload);
    }
    const Answer none = c_limits(3);
    CHECK(none.status == 1 && none.payload == "unknown profile");
    // the numbers are those of LevelLimits (not a second copy of them)
    const std::string e = c_limits(2).payload;
    const LevelLimits x = LevelLimits::for_extended_maps();
    CHECK_MSG(contains(e, "\"extended\":{\"rows\":" + std::to_string(x.rows()) + ",\"columns\":" + std::to_string(x.columns()) + ",\"cells\":" + std::to_string(x.cells()) + ",\"description_bytes\":255}"), e);
    CHECK_MSG(contains(e, "\"classic\":{\"rows\":256,\"columns\":256,\"cells\":65536}") && contains(e, "\"read_bytes\":4194304,\"write_bytes\":3670016"), e);
    const std::string n = c_limits(0).payload;
    CHECK_MSG(contains(n, "\"allow_extended\":false") && contains(n, "\"extended\":null"), n);
    CHECK_MSG(contains(n, "\"classic\":{\"rows\":" + std::to_string(LevelLimits::for_new_maps().rows_for(8)) + ",\"columns\":130,\"cells\":10000}"), n);
    CHECK_MSG(contains(c_limits(1).payload, "\"read_bytes\":null,\"write_bytes\":null"), "the game's profile has no limit on a file (null on every platform)");

    // the profile 2 reads both kinds; the profiles 0 and 1 refuse a version 9 file as they always did
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    const Conv up = c_to_extended(tiny);
    CHECK_MSG(up.status == 0 && !up.bytes.empty(), up.json);
    const Answer read9 = c_read(up.bytes, 2);
    CHECK_MSG(read9.status == 0 && contains(read9.payload, "\"loaded\":true") && contains(read9.payload, "\"version\":9") && contains(read9.payload, "\"profile\":\"extended\""), read9.payload);
    CHECK(contains(c_read(up.bytes, 0).payload, "\"loaded\":false") && contains(c_read(up.bytes, 0).payload, "version_not_supported"));
    CHECK(contains(c_read(up.bytes, 1).payload, "\"loaded\":false"));
    CHECK(contains(c_read(tiny, 2).payload, "\"version\":8"));
    // rewrite keeps the kind: a version 9 file comes back as itself (it is canonical), a version 8 file as its canonical bytes
    CHECK_MSG(c_rewrite(up.bytes, 2).status == 0 && as_bytes(c_rewrite(up.bytes, 2).payload) == up.bytes, "rewrite of a version 9 file under the Extended profile");
    CHECK(c_rewrite(up.bytes, 0).status == 1);
    CHECK(c_rewrite(tiny, 2).payload == c_rewrite(tiny, 0).payload);
    CHECK(c_same(up.bytes, up.bytes, 2).payload == "{\"same\":true}");
    const Answer kinds = c_same(tiny, up.bytes, 2);
    CHECK_MSG(kinds.status == 0 && contains(kinds.payload, "version"), kinds.payload);
    // the original-safe check of a version 9 file: not for the original, whatever the profile
    CHECK(contains(c_check(up.bytes, 2).payload, "\"works_in_original_game\":false") && contains(c_check(up.bytes, 2).payload, "not_for_original"));
    // a Classic blank map under the Extended profile is a Classic map (256 a side)
    const Answer classic_blank = take(lvl_blank(200, 200, 485, 6, 10, 2));
    CHECK_MSG(classic_blank.status == 0 && contains(c_read(as_bytes(classic_blank.payload), 2).payload, "\"version\":8"), classic_blank.payload.substr(0, 80));
    CHECK(take(lvl_blank(257, 10, 485, 6, 10, 2)).status == 1);
}

// ---- C8 ----
std::string library_check_name(const FileCase& c, const char* what) { return c.id + "." + what + ".json"; }

void c8_check_map() {
    for (const FileCase& c : library_cases()) {
        const Answer m = c_check_map(c.bytes);
        CHECK_MSG(m.status == 0 && contains(m.payload, "\"kind\":\"classic\""), c.id + ": " + m.payload.substr(0, 100));
        CHECK(m.payload == lvl_core::check_map(c.bytes.data(), c.bytes.size(), lvl_core::Profile::NewMap).payload);
        expect_text(library_check_name(c, "map"), m.payload);
        // the headline follows the original-safe verdict, and the findings are the same lines
        const Answer o = c_check(c.bytes);
        const bool works = contains(o.payload, "\"works_in_original_game\":true");
        CHECK_MSG(contains(m.payload, works ? "\"works\":true" : "\"works\":false"), c.id + ": the verdict of the original-safe profile");
        CHECK_MSG(contains(m.payload, works ? "\"headline\":\"Works in the original game\"" : "\"headline\":\"Does not work in the original game\""), c.id + ": " + m.payload.substr(0, 150));
        CHECK_MSG(contains(m.payload, "\"badge\":\"\""), c.id + ": a Classic map has no badge");
        // the same map as an Extended map, in a place that takes them
        const Conv up = c_to_extended(c.bytes);
        CHECK_MSG(up.status == 0 && !up.bytes.empty(), c.id + ": " + up.json);
        const Answer e = c_check_map(up.bytes, 2);
        CHECK_MSG(e.status == 0 && contains(e.payload, "\"kind\":\"extended\"") && contains(e.payload, "\"badge\":\"New version only\"") && contains(e.payload, "not_for_original"), c.id + ": " + e.payload.substr(0, 200));
        CHECK_MSG(contains(e.payload, "\"headline\":\"Works in the new version\"") || contains(e.payload, "\"headline\":\"Does not work in the new version\""), c.id + ": a headline of the new version");
        expect_text(library_check_name(c, "extended.map"), e.payload);
        // ... and in a place that reads Classic maps only: not judged
        const Answer refused_here = c_check_map(up.bytes, 0);
        CHECK_MSG(refused_here.status == 0 && contains(refused_here.payload, "\"judged\":false") && contains(refused_here.payload, "extended_not_accepted_here") && contains(refused_here.payload, "\"badge\":\"New version only\""), refused_here.payload);
        CHECK(contains(c_check_map(up.bytes, 1).payload, "extended_not_accepted_here"));
    }
    // the profile 2 judges a Classic map as it does the profile 0, except for the limits of the place
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    CHECK(c_check_map(tiny, 2).payload.find("\"kind\":\"classic\"") != std::string::npos && contains(c_check_map(tiny, 1).payload, "\"works\":true"));
    CHECK(c_check_map(tiny, 7).status == 1 && c_check_map(tiny, 7).payload == "unknown profile");
    // a file that is no map is a report that says so, never a refusal
    for (const FileCase& h : hostile_cases()) {
        const Answer m = c_check_map(h.bytes, 2);
        CHECK_MSG(m.status == 0 && contains(m.payload, "\"works\":false"), h.id + ": " + m.payload.substr(0, 100));
    }
}

// ---- C9 ----
// The ids of a conversion's reasons and notes, in the order of the JSON
std::string codes_of(const std::string& json, const char* list) {
    const std::string key = std::string("\"") + list + "\":[";
    const size_t at = json.find(key);
    if (at == std::string::npos) return "?";
    if (json[at + key.size()] == ']') return "";                                // (an empty list)
    const size_t stop = json.find("\n]", at);
    std::string codes;
    for (size_t i = json.find("\"code\":\"", at); i != std::string::npos && i < stop; i = json.find("\"code\":\"", i + 1)) {
        const size_t from = i + 8;
        codes += (codes.empty() ? "" : ",") + json.substr(from, json.find('"', from) - from);
    }
    return codes;
}

std::string hex_fnv_line(const Bytes& b) { return std::to_string(b.size()) + "\t" + fnv(std::string(b.begin(), b.end())); }

void c9_conversions() {
    std::ostringstream index;
    for (const FileCase& c : library_cases()) {
        const Conv up = c_to_extended(c.bytes);
        CHECK_MSG(up.status == 0 && contains(up.json, "\"ok\":true") && contains(up.json, "\"from\":\"classic\"") && contains(up.json, "\"to\":\"extended\""), c.id + ": " + up.json);
        CHECK_MSG(contains(up.json, "\"bytes\":" + std::to_string(up.bytes.size())), c.id + ": the size in the JSON is the size of the file");
        expect_text(library_check_name(c, "to_extended"), up.json);
        // a copy that is another file, a map of version 9 that reads back as the same map (the anchors made relative), with the original's rules
        CHECK_MSG(up.bytes != c.bytes && up.bytes.size() >= 4 && up.bytes[0] == 9, c.id + ": another file, version 9");
        LevelData source;
        LevelData copy;
        LevelValidation report;
        CHECK(source.load_from_memory(c.bytes.data(), c.bytes.size(), &report, LevelLimits::for_new_maps()));
        CHECK_MSG(copy.load_from_memory(up.bytes.data(), up.bytes.size(), &report, LevelLimits::for_extended_maps()) && copy.version == 9 && copy.rules == 0 && copy.format_flags == 0, c.id);
        CHECK(copy.height() == source.height() && copy.width() == source.width() && copy.layer2_interactive.size() == source.layer2_interactive.size());
        // ... converting it again changes nothing, and it comes back as the canonical bytes of the Classic file (law 1)
        const Conv again = c_to_extended(up.bytes);
        CHECK_MSG(again.status == 0 && contains(again.json, "\"from\":\"extended\"") && again.bytes == up.bytes, c.id + ": an Extended map converts to itself");
        const Conv back = c_to_classic(up.bytes);
        CHECK_MSG(back.status == 0 && contains(back.json, "\"ok\":true") && contains(back.json, "\"from\":\"extended\"") && contains(back.json, "\"to\":\"classic\""), c.id + ": " + back.json);
        expect_text(library_check_name(c, "to_classic"), back.json);
        const Answer rw = c_rewrite(c.bytes);
        CHECK_MSG(rw.status == 0 && as_bytes(rw.payload) == back.bytes, c.id + ": up and back is the canonical file");
        // the Classic map converts to itself under the Classic profile
        const Conv same = c_to_classic(c.bytes);
        CHECK_MSG(same.status == 0 && contains(same.json, "\"from\":\"classic\"") && same.bytes == back.bytes, c.id + ": a Classic map converts to itself (canonical)");
        index << c.id << "\t" << hex_fnv_line(up.bytes) << "\t" << hex_fnv_line(back.bytes) << "\n";
    }
    if (g_write) write_text(expected_dir() + "/kinds_index.txt", index.str());
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/kinds_index.txt", stored));
        CHECK_MSG(stored == index.str(), "the conversions are the stored ones (kinds_index.txt)");
    }

    // a file that does not load, a profile that does not exist
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    const Conv bad = c_to_extended(Bytes{8});
    CHECK_MSG(bad.status == 1 && contains(bad.json, "does not load"), bad.json);
    CHECK(c_to_classic(Bytes{}).status == 1 && c_to_classic(tiny, 9).status == 1 && c_to_classic(tiny, 9).json == "unknown profile");
    CHECK(unpack(take(lvl_convert_to_extended(nullptr, 0))).status == 1);

    // the reasons, with their codes: an Extended map of 95 rows (the wall at row 90), of 300 x 400 (past 256), with a description of 100 bytes
    LevelData tall = blank_extended_level(95, 20, lvl_core::tile_names());
    const LevelWriteResult tall_file = write_level(tall, LevelLimits::for_extended_maps());
    CHECK(tall_file.ok);
    const Conv down = c_to_classic(tall_file.bytes);
    CHECK_MSG(down.status == 0 && contains(down.json, "\"ok\":false") && contains(down.json, "\"bytes\":0") && down.bytes.empty() && codes_of(down.json, "reasons") == "rows_beyond_wall", down.json);
    const LevelConversion direct = convert_to_classic(tall);
    CHECK_MSG(direct.reason_codes == std::vector<std::string>{"rows_beyond_wall"} && contains(down.json, direct.reasons[0].substr(0, 40)), "the reasons are the writer's");
    LevelData wide = blank_extended_level(300, 400, lvl_core::tile_names());
    wide.description = std::string(100, 'x');
    const LevelWriteResult wide_file = write_level(wide, LevelLimits::for_extended_maps());
    CHECK(wide_file.ok);
    const Conv wide_down = c_to_classic(wide_file.bytes);
    CHECK_MSG(wide_down.status == 0 && contains(wide_down.json, "\"ok\":false"), wide_down.json);
    CHECK_MSG(codes_of(wide_down.json, "reasons") == "columns_beyond_classic,rows_beyond_classic,rows_beyond_wall,description_too_long", codes_of(wide_down.json, "reasons"));
    // the notes of a conversion that works: rules 1 dropped, a description of 30 bytes
    LevelData noted = blank_extended_level(40, 40, lvl_core::tile_names());
    noted.rules = 1;
    noted.description = std::string(30, 'd');
    const LevelWriteResult noted_file = write_level(noted, LevelLimits::for_extended_maps());
    CHECK(noted_file.ok);
    const Conv noted_down = c_to_classic(noted_file.bytes);
    CHECK_MSG(noted_down.status == 0 && contains(noted_down.json, "\"ok\":true") && codes_of(noted_down.json, "notes") == "description_30_bytes,rules_extended_dropped" && codes_of(noted_down.json, "reasons") == "", noted_down.json);
    CHECK_MSG(!noted_down.bytes.empty() && noted_down.bytes[0] == 8, "version 8");
    // the place: a new Classic map has at most 10,000 cells; the copy of a bigger Extended map has a reason of its own (the game's profile has no such limit)
    LevelData big = blank_extended_level(90, 120, lvl_core::tile_names());          // 10,800 cells: a Classic map, not a new one
    const LevelWriteResult big_file = write_level(big, LevelLimits::for_extended_maps());
    CHECK(big_file.ok);
    const Conv big_new = c_to_classic(big_file.bytes, 0);
    CHECK_MSG(big_new.status == 0 && contains(big_new.json, "\"ok\":false") && codes_of(big_new.json, "reasons") == "beyond_place_limits" && big_new.bytes.empty(), big_new.json);
    const Conv big_game = c_to_classic(big_file.bytes, 1);
    CHECK_MSG(big_game.status == 0 && contains(big_game.json, "\"ok\":true") && !big_game.bytes.empty() && big_game.bytes[0] == 8, big_game.json);
    // ... and the notes of a copy that is refused for the place are not told (there is no copy): a description of 30 bytes would have one
    big.description = std::string(30, 'd');
    const LevelWriteResult big_noted_file = write_level(big, LevelLimits::for_extended_maps());
    CHECK(big_noted_file.ok);
    const Conv big_noted_new = c_to_classic(big_noted_file.bytes, 0);
    CHECK_MSG(big_noted_new.status == 0 && contains(big_noted_new.json, "\"ok\":false") && codes_of(big_noted_new.json, "notes") == "", big_noted_new.json);
    CHECK_MSG(codes_of(c_to_classic(big_noted_file.bytes, 1).json, "notes") == "description_30_bytes", "the game's profile takes the copy, and tells the note");
    // needs_extended: the reasons of the conversion (one code path), true when there are some
    const Answer needs_tall = c_needs(tall_file.bytes, 2);
    CHECK_MSG(needs_tall.status == 0 && contains(needs_tall.payload, "\"needs_extended\":true") && codes_of(needs_tall.payload, "reasons") == "rows_beyond_wall", needs_tall.payload);
    CHECK_MSG(contains(c_needs(tiny).payload, "\"needs_extended\":false") && contains(c_needs(up_of(tiny)).payload, "\"needs_extended\":false"), "a map that fits needs nothing");
    CHECK_MSG(contains(c_needs(big_file.bytes, 0).payload, "\"needs_extended\":true") && contains(c_needs(big_file.bytes, 1).payload, "\"needs_extended\":false"), "the place decides");
    CHECK(c_needs(Bytes{1, 2, 3}).status == 1 && c_needs(tiny, 5).status == 1);
    expect_text("needs_extended_95x20.json", needs_tall.payload);
    expect_text("to_classic_95x20.json", down.json);
    expect_text("to_classic_300x400.json", wide_down.json);
    expect_text("to_classic_40x40_noted.json", noted_down.json);
    // every library map, not only nine: up, back, the canonical bytes (the maps that play for four teams; test_map_roundtrip has all 540 through the library functions)
    int tried = 0;
    for (const std::string& dir : {std::string("Original-Ants/Maps"), std::string("Community-Maps")}) {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(std::string(ANTS_SOURCE_DIR) + "/" + dir)) {
            const std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".lvl" || ext == ".LVL")) files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
        for (size_t i = 0; i < files.size(); ++i) {
            Bytes b;
            if (!read_file(files[i], b)) continue;
            const Answer rw = c_rewrite(b);
            if (rw.status != 0) continue;                                           // (a map that does not play for four teams has no canonical file to compare)
            const Conv up = c_to_extended(b);
            const Conv back = c_to_classic(up.bytes);
            CHECK_MSG(up.status == 0 && back.status == 0 && as_bytes(rw.payload) == back.bytes, files[i] + ": up and back is the canonical file");
            ++tried;
        }
    }
    CHECK_MSG(tried > 100, "maps went up and back (all of them): " + std::to_string(tried));
}

// ---- C10 ----
struct ExtBlank {
    uint32_t rows, columns, ground, eggs, minutes;
};

void c10_blank_extended() {
    static const std::vector<ExtBlank> cases = {
        {31, 31, 485, 6, 10}, {90, 90, 485, 6, 10}, {91, 5, 485, 6, 10}, {100, 100, 485, 6, 10}, {300, 400, 485, 6, 10}, {256, 1024, 485, 6, 10}, {1024, 256, 486, 9, 12}, {512, 512, 485, 6, 10},
        {1, 1, 485, 6, 10},                                                           // the smallest
        {513, 512, 485, 6, 10}, {1025, 1, 485, 6, 10}, {1, 1025, 485, 6, 10},         // beyond the Extended limits
        {0, 10, 485, 6, 10}, {10, 70000, 485, 6, 10}, {31, 31, 1344, 6, 10}, {31, 31, 485, 70000, 10},
    };
    std::string made;
    for (const ExtBlank& c : cases) {
        const std::string id = "blank_extended_" + std::to_string(c.rows) + "x" + std::to_string(c.columns) + "_g" + std::to_string(c.ground) + "_e" + std::to_string(c.eggs) + "_m" + std::to_string(c.minutes);
        const Answer a = take(lvl_blank_extended(c.rows, c.columns, c.ground, c.eggs, c.minutes));
        made += id + "\t" + std::to_string(a.status) + "\t" + (a.status == 0 ? std::to_string(a.payload.size()) + "\t" + fnv(a.payload) : "-\t" + a.payload) + "\n";
        if (a.status != 0) continue;
        const Bytes bytes = as_bytes(a.payload);
        LevelData level;
        LevelValidation report;
        CHECK_MSG(level.load_from_memory(bytes.data(), bytes.size(), &report, LevelLimits::for_extended_maps()), id);
        CHECK(level.version == 9 && level.height() == c.rows && level.width() == c.columns && level.boundary_param == c.eggs && level.default_minutes == c.minutes && level.format_flags == 0);
        CHECK_MSG(level.rules == (c.rows > 90 || c.columns > 256 ? 1u : 0u), id + ": the extended rules from 91 rows or 257 columns on");
        CHECK(!level.layer1_terrain.empty() && level.layer1_terrain.front().tile_index == c.ground);
        const LevelWriteResult w = write_level(blank_extended_level(c.rows, c.columns, lvl_core::tile_names(), LevelLimits::for_extended_maps()), LevelLimits::for_extended_maps());
        if (c.ground == 485 && c.eggs == 6 && c.minutes == 10) CHECK_MSG(w.ok && w.bytes == bytes, id + ": blank_extended_level's bytes");
        // the check by kind: an Extended map with no hill and no start marker, and nothing else
        const Answer m = c_check_map(bytes, 2);
        CHECK_MSG(m.status == 0 && contains(m.payload, "\"kind\":\"extended\"") && contains(m.payload, "\"fits_the_place\":true") && contains(m.payload, "\"works\":false"), id + ": " + m.payload.substr(0, 200));
        CHECK_MSG(contains(m.payload, "hill_missing") && contains(m.payload, "starting_ants_missing") && contains(m.payload, "not_for_original"), id);
        CHECK_MSG(!contains(m.payload, "rows_beyond_wall"), id + ": an Extended map has no wall");
        CHECK_MSG(contains(m.payload, "\"works\":false"), id);
        // the canonical bytes are the file itself, under the Extended profile only
        CHECK_MSG(c_rewrite(bytes, 2).status == 0 && c_rewrite(bytes, 2).payload == a.payload && c_rewrite(bytes, 0).status == 1, id + ": the rewrite");
        if (c.rows * c.columns <= 120000u) {                                       // (the reports of the large maps are the same text over and over: their bytes are in the index)
            expect_text(id + ".read.json", c_read(bytes, 2).payload);
            expect_text(id + ".map.json", m.payload);
        }
    }
    if (g_write) write_text(expected_dir() + "/blank_extended_index.txt", made);
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/blank_extended_index.txt", stored));
        CHECK_MSG(stored == made, "the blank Extended maps are the stored ones (blank_extended_index.txt)");
    }
}

// Hostile Extended files: headers and cuts of a version 9 file made from TINY. The offsets of the version 9 layout: the version dword at 0, the mode at 4, the minutes at 8, the flags at 10,
// the rules at 14, the description's length at 16 (docs/MAP_FORMAT.md)
std::vector<FileCase> hostile_extended_cases() {
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    const Bytes up = c_to_extended(tiny).bytes;
    const size_t description = static_cast<size_t>(up[16]) | static_cast<size_t>(up[17]) << 8;
    const size_t shift = 18 + description - 40;                                 // where the version 8 file's offsets (the dictionary's count at 40) are in the version 9 file
    std::vector<FileCase> cases;
    auto add = [&](const std::string& id, Bytes b) { cases.push_back(FileCase{"hostile_ext_" + id, "-", true, std::move(b)}); };
    Bytes flags = up;
    flags[10] = 1;
    add("format_flag", flags);
    Bytes rules = up;
    rules[14] = 2;
    add("rules_2", rules);
    Bytes grid = up;
    put32(grid, 7412 + shift, 0xFFFFFFFFu);
    put32(grid, 7416 + shift, 0xFFFFFFFFu);
    add("huge_grid", grid);
    Bytes grid2 = up;
    put32(grid2, 7412 + shift, 65535u);
    put32(grid2, 7416 + shift, 65535u);
    add("grid_65535", grid2);
    add("cut_in_header", Bytes(up.begin(), up.begin() + 12));
    add("cut_in_layers", Bytes(up.begin(), up.begin() + 12000));
    add("cut_in_half", Bytes(up.begin(), up.begin() + static_cast<std::ptrdiff_t>(up.size() / 2)));
    add("version_9_then_random", [&] { Bytes b = random_bytes(5, 30000); b[0] = 9; b[1] = b[2] = b[3] = 0; return b; }());
    return cases;
}

void c10_hostile_extended() {
    std::ostringstream index;
    for (const FileCase& c : hostile_extended_cases()) {
        const Answer map = c_check_map(c.bytes, 2);
        const Conv down = c_to_classic(c.bytes);
        const Conv up = c_to_extended(c.bytes);
        const Answer needs = c_needs(c.bytes, 2);
        const Answer read = c_read(c.bytes, 2);
        CHECK_MSG(map.status == 0 && read.status == 0, c.id + ": an answer");
        CHECK_MSG(contains(map.payload, "\"works\":false"), c.id + ": it does not work: " + map.payload.substr(0, 150));
        CHECK_MSG(down.status == 1 && up.status == 1 && needs.status == 1, c.id + ": a file that does not load is not converted (" + down.json + ")");
        CHECK_MSG(contains(read.payload, "\"loaded\":false"), c.id + ": does not load");
        expect_text(c.id + ".map.json", map.payload);
        expect_text(c.id + ".read.json", read.payload);
        index << c.id << "\t" << down.status << "\t" << down.json << "\n";
    }
    if (g_write) write_text(expected_dir() + "/hostile_ext_index.txt", index.str());
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/hostile_ext_index.txt", stored));
        CHECK_MSG(stored == index.str(), "the refusals are the stored ones (hostile_ext_index.txt)");
    }
}


// ---- C11 ----
std::string hex_of(const std::string& bytes) {
    static const char* const digits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        out += digits[(static_cast<uint8_t>(c) >> 4) & 15];
        out += digits[static_cast<uint8_t>(c) & 15];
    }
    return out;
}

// Every file of the library (Original-Ants/Maps and Community-Maps), as bytes, in the order of the names
std::vector<std::pair<std::string, Bytes>> whole_library() {
    std::vector<std::pair<std::string, Bytes>> files;
    for (const std::string& dir : {std::string("Original-Ants/Maps"), std::string("Community-Maps")}) {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(std::string(ANTS_SOURCE_DIR) + "/" + dir)) {
            const std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".lvl" || ext == ".LVL")) names.push_back(entry.path().string());
        }
        std::sort(names.begin(), names.end());
        for (const std::string& n : names) {
            Bytes b;
            if (read_file(n, b)) files.emplace_back(n, std::move(b));
        }
    }
    return files;
}

// The properties a file holds, by the reader (the Extended profile reads both kinds)
struct Props {
    bool loaded{false};
    std::string description;
    uint32_t minutes{0}, eggs{0}, type{0};
    HeaderLayout layout;
    uint32_t version{0};
};

Props props_of(const Bytes& b) {
    Props p;
    LevelData level;
    LevelValidation report;
    p.loaded = LVLParser::load_from_memory(b.data(), b.size(), level, &report, LevelLimits::for_extended_maps(), &p.layout);
    if (p.loaded) {
        p.description = level.description;
        p.minutes = level.default_minutes;
        p.eggs = level.boundary_param;
        p.type = level.ambient_tile_or_sound;
        p.version = level.version;
    }
    return p;
}

// Does the file say these four things, and is everything else of it as it was? The two levels are compared as levels after the four values are put into the first one
bool says(const Bytes& b, const Bytes& before, const std::string& description, uint32_t minutes, uint32_t eggs, uint32_t type, std::string* why) {
    LevelData now, was;
    LevelValidation report;
    if (!now.load_from_memory(b.data(), b.size(), &report, LevelLimits::for_extended_maps()) || !was.load_from_memory(before.data(), before.size(), &report, LevelLimits::for_extended_maps())) { *why = "does not load"; return false; }
    if (now.description != description || now.default_minutes != minutes || now.ambient_tile_or_sound != type) { *why = "the values are not the asked ones"; return false; }
    if (was.boundary_param != now.boundary_param && now.boundary_param != eggs) { *why = "the eggs are not the asked ones"; return false; }
    was.description = description;
    was.default_minutes = static_cast<uint16_t>(minutes);
    was.ambient_tile_or_sound = static_cast<uint16_t>(type);
    was.boundary_param = now.boundary_param;
    return same_level(was, now, why);
}

void c11_properties() {
    const std::vector<std::pair<std::string, Bytes>> library = whole_library();
    CHECK_MSG(library.size() == 540, "the library has 540 maps: " + std::to_string(library.size()));
    int same_count = 0, restored = 0, dirty_field = 0, no_egg_word = 0, noop_checked = 0, ext_checked = 0, shifted = 0;
    for (const auto& file : library) {
        const Bytes& b = file.second;
        const Props p = props_of(b);
        if (!p.loaded) continue;                                        // (a map that the Extended place does not read: none of the 540)
        // asking for the values the file has gives the very same bytes: no edit, nothing changed, whatever the file holds (a quirk, bytes after the egg word)
        const Conv same = c_set(b, p.description, p.minutes, p.eggs, p.type);
        CHECK_MSG(same.status == 0 && contains(same.json, "\"ok\":true") && contains(same.json, "\"changed\":false") && contains(same.json, "\"edits\":[]") && same.bytes == b, file.first + ": " + same.json.substr(0, 120));
        ++noop_checked;
        // the same on the Extended copy of it
        const Conv up = c_to_extended(b);
        const Props q = props_of(up.bytes);
        const Conv same9 = c_set(up.bytes, q.description, q.minutes, q.eggs, q.type);
        CHECK_MSG(same9.status == 0 && contains(same9.json, "\"changed\":false") && same9.bytes == up.bytes, file.first + ": the Extended copy: " + same9.json.substr(0, 120));
        ++ext_checked;
        if (p.layout.eggs_at == HeaderLayout::kNone) ++no_egg_word;
        // setting other values: the file says them, everything else is as it was, and setting the old values again gives the file back (a field that holds more than the text after its
        // zero byte is zeroed by the first change, which is the one thing the second change cannot give back)
        const std::string text = "Caf\xC3\xA9 of the ants";
        const uint32_t minutes = p.minutes == 7 ? 8 : 7, type = p.type == 63 ? 64 : 63, eggs = p.layout.eggs_at == HeaderLayout::kNone ? p.eggs : (p.eggs == 3 ? 4 : 3);
        for (int kind = 0; kind < 2; ++kind) {
            const Bytes& from = kind == 0 ? b : up.bytes;
            const Props now = kind == 0 ? p : q;
            const Conv set = c_set(from, text, minutes, eggs, type);
            std::string why;
            CHECK_MSG(set.status == 0 && contains(set.json, "\"ok\":true") && contains(set.json, "\"changed\":true") && contains(set.json, "\"bytes\":" + std::to_string(set.bytes.size())), file.first + ": " + set.json.substr(0, 160));
            CHECK_MSG(says(set.bytes, from, text, minutes, eggs, type, &why), file.first + ": " + why);
            if (kind == 1) { CHECK_MSG(set.bytes.size() == from.size() - now.description.size() + text.size(), file.first + ": a version 9 description is as long as its text"); if (set.bytes.size() != from.size()) ++shifted; }
            else CHECK_MSG(set.bytes.size() == from.size(), file.first + ": a version 8 file keeps its size");
            const Conv back = c_set(set.bytes, now.description, now.minutes, now.eggs, now.type);
            CHECK_MSG(back.status == 0 && contains(back.json, "\"ok\":true"), file.first + ": " + back.json.substr(0, 160));
            bool clean = true;
            if (kind == 0) for (size_t i = p.layout.description_at + p.description.size() + (p.description.size() < 30 ? 1 : 0); i < p.layout.description_at + 30; ++i) clean = clean && b[i] == 0;
            if (clean) { CHECK_MSG(back.bytes == from, file.first + ": setting back gives the file back"); ++restored; }
            else { ++dirty_field; CHECK_MSG(back.bytes.size() == from.size(), file.first + ": a dirty field"); }
        }
        ++same_count;
    }
    CHECK_MSG(same_count == 540 && noop_checked == 540 && ext_checked == 540, "all 540 maps: " + std::to_string(same_count));
    CHECK_MSG(restored > 1000, "setting back was checked: " + std::to_string(restored) + " (field with leftover bytes: " + std::to_string(dirty_field) + ")");
    CHECK_MSG(shifted > 500, "an Extended file grows and shrinks with its text: " + std::to_string(shifted));
    std::cout << "(" << dirty_field << " description fields have leftover bytes, " << no_egg_word << " maps have no egg word) " << std::flush;

    // ---- the nine maps of the set, and the limits, with the stored answers ----
    const Bytes tiny = read_checked("Original-Ants/Maps/TINY.LVL");
    const Bytes tiny9 = up_of(tiny);
    std::ostringstream index;
    struct Case { std::string id, from, text; uint32_t minutes, eggs, type, profile; };
    std::vector<Case> cases;
    const std::string cafe = "Caf\xC3\xA9, tiny ants";
    for (const FileCase& c : library_cases()) {
        cases.push_back({c.id, c.path, cafe, 7, 3, 63, 2});
        cases.push_back({c.id + "_ext", "ext:" + c.path, cafe, 7, 3, 63, 2});
    }
    cases.push_back({"tiny_29", "Original-Ants/Maps/TINY.LVL", std::string(29, 'a'), 12, 9, 0x7FFE, 0});
    cases.push_back({"tiny_30", "Original-Ants/Maps/TINY.LVL", std::string(30, 'b'), 12, 9, 0x7FFE, 0});
    cases.push_back({"tiny_31", "Original-Ants/Maps/TINY.LVL", std::string(31, 'c'), 12, 9, 0x7FFE, 0});
    cases.push_back({"tiny_ext_255", "ext:Original-Ants/Maps/TINY.LVL", std::string(255, 'd'), 120, 255, 66, 2});
    cases.push_back({"tiny_ext_256", "ext:Original-Ants/Maps/TINY.LVL", std::string(256, 'e'), 120, 255, 66, 2});
    cases.push_back({"tiny_zero_byte", "Original-Ants/Maps/TINY.LVL", std::string("ab\0cd", 5), 12, 9, 0x7FFE, 2});
    cases.push_back({"tiny_numbers", "Original-Ants/Maps/TINY.LVL", "x", 65536, 65536, 65536, 2});
    cases.push_back({"tiny_largest", "Original-Ants/Maps/TINY.LVL", "", 65535, 65535, 65535, 2});
    cases.push_back({"tiny_ext_under_new_map", "ext:Original-Ants/Maps/TINY.LVL", "x", 12, 9, 0x7FFE, 0});
    for (const Case& c : cases) {
        Bytes from;
        const bool ext = c.from.rfind("ext:", 0) == 0;
        from = read_checked(ext ? c.from.substr(4) : c.from);
        if (ext) from = up_of(from);
        const Answer a = take(lvl_set_properties(from.data(), static_cast<uint32_t>(from.size()), reinterpret_cast<const uint8_t*>(c.text.data()), static_cast<uint32_t>(c.text.size()), c.minutes, c.eggs, c.type, c.profile));
        const Conv r = unpack(a);
        const std::string name = "properties." + c.id + ".json";
        expect_text(name, r.status == 0 ? r.json : a.payload);
        index << c.id << "\t" << c.from << "\t" << hex_of(c.text) << "\t" << c.minutes << "\t" << c.eggs << "\t" << c.type << "\t" << c.profile << "\t" << r.status << "\t"
              << (r.status == 0 ? std::to_string(r.bytes.size()) + "\t" + fnv(std::string(r.bytes.begin(), r.bytes.end())) : "-\t" + a.payload) << "\n";
    }
    if (g_write) write_text(expected_dir() + "/properties_index.txt", index.str());
    else {
        std::string stored;
        CHECK(read_text(expected_dir() + "/properties_index.txt", stored));
        CHECK_MSG(stored == index.str(), "the answers of lvl_set_properties are the stored ones (properties_index.txt)");
    }

    // the description's limit, the checker's own sentence, and the check of the map after it
    auto set_text = [&](const Bytes& from, size_t n, uint32_t profile) { return c_set(from, std::string(n, 'z'), 12, 9, 0x7FFE, profile); };
    const Conv ok29 = set_text(tiny, 29, 0);
    CHECK_MSG(ok29.status == 0 && contains(ok29.json, "\"ok\":true") && codes_of(ok29.json, "warnings") == "" && contains(ok29.json, "\"description_limit\":29"), ok29.json);
    CHECK_MSG(!contains(take(lvl_check_map(ok29.bytes.data(), static_cast<uint32_t>(ok29.bytes.size()), 0)).payload, "description_too_long"), "29 bytes: the check says nothing of the description");
    const Conv ok30 = set_text(tiny, 30, 0);
    CHECK_MSG(ok30.status == 0 && contains(ok30.json, "\"ok\":true") && codes_of(ok30.json, "warnings") == "description_too_long" && contains(ok30.json, description_too_long_text(MapKind::Classic, 30)), ok30.json);
    const std::string verdict30 = take(lvl_check_map(ok30.bytes.data(), static_cast<uint32_t>(ok30.bytes.size()), 0)).payload;
    CHECK_MSG(contains(verdict30, "\"severity\":\"warning\",\"code\":\"description_too_long\"") && contains(verdict30, description_too_long_text(MapKind::Classic, 30)), "30 bytes: the check warns, in the same words: " + verdict30.substr(0, 300));
    const Conv no31 = set_text(tiny, 31, 0);
    CHECK_MSG(no31.status == 0 && contains(no31.json, "\"ok\":false") && contains(no31.json, "\"bytes\":0") && no31.bytes.empty() && codes_of(no31.json, "reasons") == "description_too_long" && contains(no31.json, description_too_long_text(MapKind::Classic, 31)), no31.json);
    const Conv ok255 = set_text(tiny9, 255, 2);
    CHECK_MSG(ok255.status == 0 && contains(ok255.json, "\"ok\":true") && codes_of(ok255.json, "warnings") == "" && contains(ok255.json, "\"description_limit\":255"), ok255.json.substr(0, 200));
    CHECK_MSG(!contains(take(lvl_check_map(ok255.bytes.data(), static_cast<uint32_t>(ok255.bytes.size()), 2)).payload, "description_too_long"), "255 bytes: the check says nothing of the description");
    const Conv no256 = set_text(tiny9, 256, 2);
    CHECK_MSG(no256.status == 0 && contains(no256.json, "\"ok\":false") && no256.bytes.empty() && codes_of(no256.json, "reasons") == "description_too_long" && contains(no256.json, description_too_long_text(MapKind::Extended, 256)), no256.json);
    CHECK_MSG(description_too_long_text(MapKind::Extended, 256) == "the description is 256 bytes long: an Extended map holds at most 255", "the checker's sentence for an Extended map");
    // a letter outside a to z takes 2 bytes: 15 of them are 30 bytes
    const Conv accents = c_set(tiny, std::string("\xC3\xA9", 2) + std::string(28, 'a'), 12, 9, 0x7FFE, 0);
    CHECK_MSG(accents.status == 0 && contains(accents.json, "\"description_bytes\":30") && codes_of(accents.json, "warnings") == "description_too_long", accents.json);
    // the other refusals
    const Conv bad = c_set(tiny, std::string("a\0b", 3), 65536, 65536, 65536, 2);
    CHECK_MSG(bad.status == 0 && codes_of(bad.json, "reasons") == "description_has_zero_byte,minutes_out_of_range,eggs_out_of_range,default_ant_type_out_of_range", bad.json);
    const Conv top = c_set(tiny, "", 65535, 65535, 65535, 2);
    CHECK_MSG(top.status == 0 && contains(top.json, "\"ok\":true") && props_of(top.bytes).minutes == 65535 && props_of(top.bytes).eggs == 65535 && props_of(top.bytes).type == 65535 && props_of(top.bytes).description.empty(), top.json);
    CHECK(c_set(Bytes{8}, "x", 1, 1, 1).status == 1 && c_set(Bytes{}, "x", 1, 1, 1).status == 1);
    CHECK_MSG(c_set(tiny, "x", 1, 1, 1, 9).status == 1 && c_set(tiny, "x", 1, 1, 1, 9).json == "unknown profile", "a profile that does not exist");
    CHECK_MSG(c_set(tiny9, "x", 1, 1, 1, 0).status == 1 && contains(c_set(tiny9, "x", 1, 1, 1, 0).json, "does not load") && c_set(tiny9, "x", 1, 1, 1, 1).status == 1, "an Extended map needs the Extended profile");
    CHECK(take(lvl_set_properties(nullptr, 0, nullptr, 0, 1, 1, 1, 2)).status == 1);
    CHECK(take(lvl_set_properties(tiny.data(), static_cast<uint32_t>(tiny.size()), nullptr, 5, 1, 1, 1, 2)).status == 1);
    CHECK_MSG(take(lvl_set_properties(tiny.data(), static_cast<uint32_t>(tiny.size()), nullptr, 0, 12, 9, 0x7FFE, 2)).status == 0, "an empty description needs no bytes");
    // the same call twice gives the same answer (no state)
    CHECK(c_set(tiny, cafe, 7, 3, 63).json == c_set(tiny, cafe, 7, 3, 63).json && c_set(tiny, cafe, 7, 3, 63).bytes == c_set(tiny, cafe, 7, 3, 63).bytes);
    // the edits: the old file's pieces, in order, and nothing else differs
    const Conv three = c_set(tiny, cafe, 7, 3, 63);
    CHECK_MSG(codes_of(three.json, "reasons") == "" && contains(three.json, "\"at\":8,\"removed\":2,\"inserted\":2") && contains(three.json, "\"at\":10,\"removed\":30,\"inserted\":30"), three.json);
    size_t different = 0;
    for (size_t i = 0; i < tiny.size(); ++i) different += tiny[i] != three.bytes[i];
    CHECK_MSG(different < 30 + 6, "only the four places differ: " + std::to_string(different) + " bytes");
    const Conv grown = c_set(tiny9, cafe, 12, 9, 0x7FFE);
    CHECK_MSG(contains(grown.json, "\"at\":16,\"removed\":" + std::to_string(2 + props_of(tiny9).description.size()) + ",\"inserted\":" + std::to_string(2 + cafe.size())), grown.json);
    // a file that ends before its egg word has no place for the eggs: refused, unless the eggs are what it reads (0)
    int refused_eggs = 0;
    for (const auto& file : library) {
        const Props p = props_of(file.second);
        if (!p.loaded || p.layout.eggs_at != HeaderLayout::kNone) continue;
        const Conv more = c_set(file.second, p.description, p.minutes, p.eggs + 1, p.type);
        CHECK_MSG(more.status == 0 && codes_of(more.json, "reasons") == "eggs_not_in_file" && more.bytes.empty(), file.first + ": " + more.json.substr(0, 200));
        ++refused_eggs;
    }
    CHECK_MSG(refused_eggs == no_egg_word, "the maps without an egg word were asked for eggs: " + std::to_string(refused_eggs));
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
    run_case("C6 no memory left", c6_no_memory);
    run_case("C7 the Extended profile and the limits", c7_profiles);
    run_case("C8 the check by kind", c8_check_map);
    run_case("C9 the conversions", c9_conversions);
    run_case("C10 the blank Extended map", c10_blank_extended);
    run_case("C10 hostile Extended files", c10_hostile_extended);
    run_case("C11 the properties", c11_properties);
    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

// lvl_tool: checks, describes and rewrites map files (.lvl), and makes a blank map; the format, the canonical form and the limits are docs/MAP_FORMAT.md.
//
//   lvl_tool check FILE [--roster MASK] [--new-map]          whether the file loads and plays for the teams of MASK (default 15, all four), and every finding
//   lvl_tool info FILE                                       what the file holds: grid, dictionary, start markers, hills, food, waypoints, eggs, and what a rewrite changes
//   lvl_tool rewrite IN OUT [--roster MASK] [--new-map]      reads IN, checks it, writes its canonical bytes to OUT and says what differs from IN (OUT may be IN)
//   lvl_tool blank OUT --rows R --columns C [--chd FILE] [--new-map]
//                                                            a new map of plain ground, its dictionary by the rule of the original maps (names from ants.chd)
//       --roster MASK  the teams that play: bit 0 green, 1 red, 2 blue, 3 black (1 .. 15)
//       --new-map      hold the file to the limits of a new map: at most 100 x 100 cells and 120 KB (LevelLimits::for_new_maps)
//       --chd FILE     where the tile names are read (default: the archive of this checkout, Original-Ants/ants.chd)
//   lvl_tool --selftest                                      checks the tool itself (the command line, the four commands and their exit codes) in a scratch folder
//
// Exit status: 0 the file plays (check), is described (info), is written (rewrite, blank); 1 the file does not load, does not play for the roster or cannot be written within
// the limits (nothing is written then); 2 the command line is wrong, or a file cannot be read or written.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"

using namespace ants::assets;

namespace {

namespace fs = std::filesystem;

constexpr const char* kTeams[4] = {"green", "red", "blue", "black"};

void usage(std::ostream& out) {
    out << "usage: lvl_tool check FILE [--roster MASK] [--new-map]\n"
           "       lvl_tool info FILE\n"
           "       lvl_tool rewrite IN OUT [--roster MASK] [--new-map]\n"
           "       lvl_tool blank OUT --rows R --columns C [--chd FILE] [--new-map]\n"
           "       lvl_tool --selftest\n"
           "  check    whether the file loads and plays for the teams of the roster, and every finding\n"
           "  info     what the file holds, and what a rewrite would change\n"
           "  rewrite  reads IN, checks it and writes its canonical bytes to OUT (OUT may be IN); says what differs\n"
           "  blank    writes a new map of plain ground, R rows x C columns, its dictionary by the rule of the original maps\n"
           "  --roster MASK  the teams that play: bit 0 green, 1 red, 2 blue, 3 black (1 .. 15; default 15)\n"
           "  --new-map      the limits of a new map: at most 100 x 100 cells and 120 KB\n"
           "  --chd FILE     the archive with the tile names (default: Original-Ants/ants.chd of this checkout)\n"
           "  --selftest     check the tool itself\n";
}

bool read_file(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}

std::string roster_words(uint8_t roster) {
    if ((roster & 0x0Fu) == 0x0Fu) return "all four teams";
    std::vector<std::string> teams;
    for (int t = 0; t < 4; ++t) {
        if ((roster & (1u << t)) != 0) teams.push_back(kTeams[t]);
    }
    std::string text;
    for (size_t i = 0; i < teams.size(); ++i) text += (i == 0 ? "" : i + 1 == teams.size() ? " and " : ", ") + teams[i];
    return text;
}

void print_findings(const LevelValidation& v, std::ostream& out) {
    for (const LevelProblem& p : v.problems) out << "  " << to_string(p.severity) << ' ' << to_string(p.kind) << ": " << p.message << "\n";
}

std::string file_name(const std::string& path) { return fs::path(path).filename().string(); }

struct Options {
    std::vector<std::string> files;
    uint8_t roster{0x0F};
    bool new_map{false};
    uint32_t rows{0};
    uint32_t columns{0};
    std::string chd;
};

bool number(const std::string& text, uint32_t low, uint32_t high, uint32_t& value) {
    if (text.empty() || text.size() > 6 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    const unsigned long v = std::strtoul(text.c_str(), nullptr, 10);
    if (v < low || v > high) return false;
    value = static_cast<uint32_t>(v);
    return true;
}

// The options after the command; false (and the reason) for a word that is not one of them or a value that is wrong
bool parse(const std::vector<std::string>& args, Options& o, std::string& why) {
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool has_value = i + 1 < args.size();
        uint32_t v = 0;
        if (a == "--roster") {
            if (!has_value || !number(args[++i], 1, 15, v)) { why = "--roster takes a number from 1 to 15"; return false; }
            o.roster = static_cast<uint8_t>(v);
        } else if (a == "--rows" || a == "--columns") {
            if (!has_value || !number(args[++i], 1, 65535, v)) { why = a + " takes a number from 1 to 65535"; return false; }
            (a == "--rows" ? o.rows : o.columns) = v;
        } else if (a == "--chd") {
            if (!has_value) { why = "--chd takes a file"; return false; }
            o.chd = args[++i];
        } else if (a == "--new-map") {
            o.new_map = true;
        } else if (a.size() > 1 && a[0] == '-') {
            why = "unknown option " + a;
            return false;
        } else {
            o.files.push_back(a);
        }
    }
    return true;
}

int check(const Options& o, std::ostream& out, std::ostream& err) {
    const std::string& path = o.files[0];
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes)) {
        err << "lvl_tool: cannot read " << path << "\n";
        return 2;
    }
    const LevelLimits limits = o.new_map ? LevelLimits::for_new_maps() : LevelLimits{};
    const std::string name = file_name(path);
    LevelData level;
    LevelValidation report;
    if (!level.load_from_memory(bytes.data(), bytes.size(), &report, limits)) {
        out << name << ": does not load: " << report.reason() << "\n";
        print_findings(report, out);
        return 1;
    }
    const LevelValidation v = level.validate(o.roster, limits);           // (what LVLParser::check_memory answers)
    if (!v.playable) {
        out << name << ": does not play with " << roster_words(o.roster) << ": " << v.reason() << "\n";
        print_findings(v, out);
        return 1;
    }
    if (v.problems.empty()) {
        out << name << ": plays with " << roster_words(o.roster) << "; no findings\n";
    } else {
        out << name << ": plays with " << roster_words(o.roster) << (v.clean() ? "; notes:" : "; the original reads it only in part or from odd data:") << "\n";
        print_findings(v, out);
    }
    return 0;
}

std::string default_type(uint16_t tile) {
    switch (tile) {
        case 62: return "combat (block 3: tile 62)";
        case 63: return "thief (block 3: tile 63)";
        case 64: return "bomber (block 3: tile 64)";
        case 65: return "swimmer (block 3: tile 65)";
        case 66: return "fire (block 3: tile 66)";
        default: return "none (block 3: tile " + std::to_string(tile) + ")";
    }
}

int info(const Options& o, std::ostream& out, std::ostream& err) {
    const std::string& path = o.files[0];
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes)) {
        err << "lvl_tool: cannot read " << path << "\n";
        return 2;
    }
    LevelData level;
    LevelValidation report;
    if (!level.load_from_memory(bytes.data(), bytes.size(), &report)) {
        out << file_name(path) << ": does not load: " << report.reason() << "\n";
        return 1;
    }
    std::array<uint32_t, 4> markers{};
    std::array<uint32_t, 4> hills{};
    uint32_t other_records = 0;
    for (const AnthillSpawn& sp : level.anthill_spawns) {
        if (sp.team_id < 4) ++markers[sp.team_id]; else ++other_records;
    }
    for (const MapCell& c : level.layer2_interactive) {
        const int team = hill_team_of_id(c.tile_index);
        if (team >= 0) ++hills[static_cast<size_t>(team)];
    }
    size_t named = 0;
    for (const std::string& n : level.tile_dictionary) named += n != "." ? 1u : 0u;
    const std::vector<uint16_t> used = used_tile_ids(level);
    size_t outside = 0;
    for (const uint16_t id : used) outside += id >= level.tile_dictionary.size() ? 1u : 0u;
    size_t triggers = 0;
    for (const Waypoint& w : level.waypoints) triggers += w.flag != 0 ? 1u : 0u;
    auto per_team = [](const std::array<uint32_t, 4>& n, const char* unit) {
        std::string text;
        for (size_t t = 0; t < 4; ++t) text += std::string(t == 0 ? "" : ", ") + kTeams[t] + " " + std::to_string(n[t]) + (t == 0 ? unit : "");
        return text;
    };
    out << "File:          " << file_name(path) << " (" << bytes.size() << " bytes)\n"
        << "Version:       " << level.version << ", mode " << level.game_mode << "\n"
        << "Description:   \"" << level.description << "\" (" << level.description.size() << " bytes)\n"
        << "Minutes:       " << level.default_minutes << "\n"
        << "Grid:          " << level.height() << " rows x " << level.width() << " columns\n"
        << "Dictionary:    " << level.tile_dictionary.size() << " names, " << named << " of them other than \".\"; the map uses " << used.size() << " tile ids, "
        << outside << " of them outside the dictionary\n"
        << "Start markers: " << per_team(markers, "") << "; other block 1 records " << other_records << "\n"
        << "Hills:         " << per_team(hills, " cells") << "\n"
        << "Food piles:    " << level.food_schedules.size() << "\n"
        << "Waypoints:     " << level.waypoints.size() << " (" << triggers << " with a trigger)\n"
        << "Default type:  " << default_type(level.ambient_tile_or_sound) << "\n"
        << "Eggs:          " << level.boundary_param << " a team\n";
    const LevelWriteResult w = write_level(level);
    if (!w.ok) {
        out << "Rewrite:       not possible: " << w.error << "\n";
        return 0;
    }
    const std::vector<RewriteChange> changes = describe_rewrite(bytes.data(), bytes.size(), level, w.bytes);
    out << "Rewrite:       " << w.bytes.size() << " bytes" << (changes.empty() ? ", the same bytes" : "") << "\n";
    for (const RewriteChange& c : changes) out << "  " << to_string(c.kind) << ": " << c.message << "\n";
    return 0;
}

int rewrite(const Options& o, std::ostream& out, std::ostream& err) {
    const std::string& in_path = o.files[0];
    const std::string& out_path = o.files[1];
    std::vector<uint8_t> bytes;
    if (!read_file(in_path, bytes)) {
        err << "lvl_tool: cannot read " << in_path << "\n";
        return 2;
    }
    const LevelLimits limits = o.new_map ? LevelLimits::for_new_maps() : LevelLimits{};
    const std::string name = file_name(in_path);
    LevelData level;
    LevelValidation report;
    if (!level.load_from_memory(bytes.data(), bytes.size(), &report, limits)) {
        out << name << ": does not load: " << report.reason() << "; nothing written\n";
        print_findings(report, out);
        return 1;
    }
    const LevelValidation v = level.validate(o.roster, limits);
    if (!v.playable) {
        out << name << ": does not play with " << roster_words(o.roster) << ": " << v.reason() << "; nothing written\n";
        print_findings(v, out);
        return 1;
    }
    const LevelWriteResult w = write_level(level, limits);
    if (!w.ok) {
        out << name << ": cannot be written: " << w.error << "; nothing written\n";
        return 1;
    }
    if (!write_file(out_path, w.bytes)) {
        err << "lvl_tool: cannot write " << out_path << "\n";
        return 2;
    }
    const std::vector<RewriteChange> changes = describe_rewrite(bytes.data(), bytes.size(), level, w.bytes);
    out << name << " -> " << file_name(out_path) << ": " << w.bytes.size() << " bytes written, the same map"
        << (changes.empty() ? "; the file was canonical already: the same bytes" : "; what differs from the file:") << "\n";
    for (const RewriteChange& c : changes) out << "  " << to_string(c.kind) << ": " << c.message << "\n";
    for (const LevelProblem& p : v.problems) out << "  (" << to_string(p.severity) << " " << to_string(p.kind) << ": " << p.message << ")\n";
    return 0;
}

int blank(const Options& o, std::ostream& out, std::ostream& err) {
    if (o.rows == 0 || o.columns == 0) {
        err << "lvl_tool: blank needs --rows and --columns\n";
        usage(err);
        return 2;
    }
    const std::string chd = o.chd.empty() ? std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd" : o.chd;
    std::vector<uint8_t> archive;
    if (!read_file(chd, archive)) {
        err << "lvl_tool: cannot read " << chd << "\n";
        return 2;
    }
    const std::vector<std::string> names = tile_names_from_chd(archive.data(), archive.size());
    if (names.empty()) {
        err << "lvl_tool: " << chd << " is no archive of the game (no tile names)\n";
        return 2;
    }
    const LevelLimits limits = o.new_map ? LevelLimits::for_new_maps() : LevelLimits{};
    const LevelData level = blank_level(static_cast<uint16_t>(o.rows), static_cast<uint16_t>(o.columns), names);
    const LevelWriteResult w = write_level(level, limits);
    if (!w.ok) {
        out << "a blank map of " << o.rows << " rows x " << o.columns << " columns cannot be written: " << w.error << "; nothing written\n";
        return 1;
    }
    if (!write_file(o.files[0], w.bytes)) {
        err << "lvl_tool: cannot write " << o.files[0] << "\n";
        return 2;
    }
    out << file_name(o.files[0]) << ": a blank map of " << o.rows << " rows x " << o.columns << " columns, " << w.bytes.size() << " bytes (plain ground, a dictionary of "
        << level.tile_dictionary.size() << " names; no hill and no start marker yet)\n";
    return 0;
}

int run_tool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        usage(out);
        return 0;
    }
    if (args.empty()) {
        usage(err);
        return 2;
    }
    Options o;
    std::string why;
    const std::string& command = args[0];
    const size_t files = command == "rewrite" ? 2u : 1u;
    if ((command != "check" && command != "info" && command != "rewrite" && command != "blank") || !parse(args, o, why) || o.files.size() != files) {
        if (!why.empty()) err << "lvl_tool: " << why << "\n";
        usage(err);
        return 2;
    }
    if (command == "check") return check(o, out, err);
    if (command == "info") return info(o, out, err);
    if (command == "rewrite") return rewrite(o, out, err);
    return blank(o, out, err);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// --selftest: the tool on the shipped map TINY and on files that it makes in a scratch folder, through the command line
// ---------------------------------------------------------------------------------------------------------------------------------

struct SelfTest {
    int checks{0};
    int failures{0};
    void check(bool ok, const std::string& what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "    FAILED: " << what << "\n";
        }
    }
};

struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("lvl_tool_selftest_" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    bool ok() const {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

struct Run {
    int status{-1};
    std::string out;
    std::string err;
};

Run run_words(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    Run r;
    r.status = run_tool(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void put16(std::vector<uint8_t>& d, size_t at, uint16_t v) {
    d[at] = static_cast<uint8_t>(v & 0xFFu);
    d[at + 1] = static_cast<uint8_t>(v >> 8);
}

int selftest() {
    SelfTest t;
    TempDir dir;
    const std::string tiny_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";
    std::vector<uint8_t> tiny;
    LevelData tiny_level;
    if (!dir.ok() || !read_file(tiny_path, tiny) || !tiny_level.load_from_memory(tiny.data(), tiny.size())) {
        std::cerr << "lvl_tool --selftest: the shipped map TINY or a scratch folder is missing\n";
        return 1;
    }
    auto file_of = [&](const std::string& name, const std::vector<uint8_t>& bytes) {
        const std::string path = dir.file(name);
        t.check(write_file(path, bytes), "the scratch file " + name + " is written");
        return path;
    };
    auto level_of = [](const std::string& path, LevelData& level) {
        std::vector<uint8_t> bytes;
        return read_file(path, bytes) && level.load_from_memory(bytes.data(), bytes.size());
    };

    std::cout << "  the command line\n";
    {
        const Run help = run_words({"--help"});
        t.check(help.status == 0 && has(help.out, "usage: lvl_tool") && help.err.empty(), "--help prints the usage and succeeds");
        for (const std::vector<std::string>& bad : std::vector<std::vector<std::string>>{
                 {}, {"nonsense"}, {"check"}, {"check", "a", "b"}, {"rewrite", "a"}, {"info"}, {"check", tiny_path, "--roster", "0"}, {"check", tiny_path, "--roster", "16"},
                 {"check", tiny_path, "--roster"}, {"check", tiny_path, "--what"}, {"blank", "x.lvl", "--rows", "0", "--columns", "5"}, {"blank", "x.lvl", "--rows", "-3"}}) {
            const Run r = run_words(bad);
            t.check(r.status == 2 && r.out.empty() && has(r.err, "usage: lvl_tool"), "a wrong command line (" + std::to_string(bad.size()) + " words) is exit 2 with the usage");
        }
        const Run missing = run_words({"check", dir.file("no such map.lvl")});
        t.check(missing.status == 2 && has(missing.err, "cannot read"), "a file that is not there is exit 2 and says so");
        t.check(run_words({"blank", dir.file("b.lvl"), "--rows", "5"}).status == 2, "blank without --columns is exit 2");
    }

    std::cout << "  check\n";
    {
        const Run r = run_words({"check", tiny_path});
        t.check(r.status == 0 && r.out == "TINY.LVL: plays with all four teams; no findings\n" && r.err.empty(), "check: TINY plays with all four teams, no findings");
        std::vector<uint8_t> cut(tiny.begin(), tiny.begin() + 10000);
        const Run c = run_words({"check", file_of("cut.lvl", cut)});
        t.check(c.status == 1 && has(c.out, "cut.lvl: does not load: the file ends inside the layers") && has(c.out, "  fatal truncated: "), "check: a cut file does not load, and says where");
        // the green team's start marker (block 1 record of tile 154) outside the grid
        std::vector<uint8_t> marker = tiny;
        const size_t b1 = 42 + 670 * 11 + 8 + 31 * 31 * 12 + 2;
        bool moved = false;
        for (size_t i = 0; i < 12 && !moved; ++i) {
            const size_t at = b1 + 6 * i;
            if ((marker[at] | (marker[at + 1] << 8)) == 154) {
                put16(marker, at + 2, 31);
                moved = true;
            }
        }
        t.check(moved, "TINY has a green start marker");
        const std::string marker_path = file_of("marker.lvl", marker);
        const Run all = run_words({"check", marker_path});
        t.check(all.status == 1 && has(all.out, "marker.lvl: does not play with all four teams: team green") && has(all.out, "  fatal start_marker_outside_grid: "),
                "check: a green start marker outside the grid refuses the four teams");
        const Run three = run_words({"check", marker_path, "--roster", "14"});
        t.check(three.status == 0 && has(three.out, "marker.lvl: plays with red, blue and black; notes:") && has(three.out, "  note start_marker_outside_grid: "),
                "check --roster 14: red, blue and black play, the marker is a note");
        // a grid of 101 columns: a map of the game, not a new map
        LevelData wide = blank_level(4, 101, std::vector<std::string>(1344, "n"));
        const std::string wide_path = file_of("wide.lvl", write_level(wide).bytes);
        t.check(run_words({"check", wide_path}).status == 0, "check: 4 x 101 plays");
        const Run limited = run_words({"check", wide_path, "--new-map"});
        t.check(limited.status == 1 && has(limited.out, "more than the 100 x 100 allowed here"), "check --new-map: 101 columns are more than a new map has");
    }

    std::cout << "  info\n";
    {
        const Run r = run_words({"info", tiny_path});
        t.check(r.status == 0 && r.err.empty(), "info succeeds");
        t.check(has(r.out, "File:          TINY.LVL (19312 bytes)\n") && has(r.out, "Version:       8, mode 1\n") && has(r.out, "Minutes:       6\n"), "info: the file, the version, the minutes");
        t.check(has(r.out, "Grid:          31 rows x 31 columns\n") && has(r.out, "Dictionary:    670 names, 93 of them other than \".\"; the map uses 93 tile ids, 0 of them outside"),
                "info: the grid and the dictionary");
        t.check(has(r.out, "Start markers: green 3, red 3, blue 3, black 3; other block 1 records 0\n") && has(r.out, "Hills:         green 16 cells, red 16, blue 16, black 16\n"),
                "info: the start markers and the hills of each team");
        t.check(has(r.out, "Food piles:    14\n") && has(r.out, "Waypoints:     0 (0 with a trigger)\n") && has(r.out, "Default type:  none") && has(r.out, "Eggs:          3 a team\n"),
                "info: food, waypoints, the default type, the eggs");
        t.check(has(r.out, "Rewrite:       19312 bytes\n  name_padding: 670 of the 670 dictionary names have leftover bytes"), "info: what a rewrite changes");
    }

    std::cout << "  rewrite\n";
    {
        const std::string out1 = dir.file("tiny_out.lvl");
        const Run r = run_words({"rewrite", tiny_path, out1});
        t.check(r.status == 0 && r.err.empty() && has(r.out, "TINY.LVL -> tiny_out.lvl: 19312 bytes written, the same map; what differs from the file:\n"), "rewrite: TINY is written");
        t.check(has(r.out, "  name_padding: 670 of the 670 dictionary names have leftover bytes after their end"), "rewrite: says that the names' leftovers are zero now");
        LevelData back;
        t.check(level_of(out1, back) && same_level(back, tiny_level), "rewrite: the file it wrote reads back as TINY");
        std::vector<uint8_t> written;
        t.check(read_file(out1, written) && written == write_level(tiny_level).bytes, "rewrite: the file is the canonical bytes");
        const std::string out2 = dir.file("tiny_out2.lvl");
        const Run again = run_words({"rewrite", out1, out2});
        std::vector<uint8_t> second;
        t.check(again.status == 0 && has(again.out, "the file was canonical already: the same bytes") && read_file(out2, second) && second == written,
                "rewrite of a canonical file: the same bytes");
        t.check(run_words({"rewrite", out2, out2}).status == 0 && read_file(out2, second) && second == written, "rewrite: OUT may be IN");
        std::vector<uint8_t> filler = tiny;
        filler.insert(filler.end(), 206, 0x55);
        const Run f = run_words({"rewrite", file_of("filler.lvl", filler), dir.file("filler_out.lvl")});
        t.check(f.status == 0 && has(f.out, "  trailing_bytes: 206 bytes after the egg word are dropped (the original never reads them)"), "rewrite: bytes after the egg word are dropped");
        const std::string refused = dir.file("refused.lvl");
        const Run m = run_words({"rewrite", dir.file("marker.lvl"), refused});
        t.check(m.status == 1 && has(m.out, "does not play with all four teams") && has(m.out, "nothing written") && !fs::exists(refused), "rewrite: a map that does not play is not written");
        t.check(run_words({"rewrite", dir.file("marker.lvl"), refused, "--roster", "14"}).status == 0 && fs::exists(refused), "rewrite --roster 14: it plays without green, it is written");
        const Run cut = run_words({"rewrite", dir.file("cut.lvl"), dir.file("cut_out.lvl")});
        t.check(cut.status == 1 && has(cut.out, "does not load") && !fs::exists(dir.file("cut_out.lvl")), "rewrite: a file that does not load is not written");
        const Run limited = run_words({"rewrite", dir.file("wide.lvl"), dir.file("wide_out.lvl"), "--new-map"});
        t.check(limited.status == 1 && has(limited.out, "more than the 100 x 100 allowed here") && !fs::exists(dir.file("wide_out.lvl")), "rewrite --new-map: 101 columns are not written");
        t.check(run_words({"rewrite", tiny_path, dir.path.string()}).status == 2, "rewrite: a folder as OUT is exit 2");
    }

    std::cout << "  blank\n";
    {
        const std::string out = dir.file("blank.lvl");
        const Run r = run_words({"blank", out, "--rows", "31", "--columns", "40"});
        t.check(r.status == 0 && r.out == "blank.lvl: a blank map of 31 rows x 40 columns, 20288 bytes (plain ground, a dictionary of 486 names; no hill and no start marker yet)\n",
                "blank: a blank map of 31 x 40 is written: " + r.out);
        LevelData back;
        t.check(level_of(out, back) && back.height() == 31u && back.width() == 40u && back.tile_dictionary.size() == 486u && back.tile_dictionary[485] == "g01a",
                "blank: the file reads back with its grid and its dictionary");
        t.check(run_words({"check", out}).status == 0, "blank: the file checks");
        const Run big = run_words({"blank", dir.file("big.lvl"), "--rows", "100", "--columns", "100", "--new-map"});
        t.check(big.status == 1 && has(big.out, "the file would have 125408 bytes, more than the 122880 allowed here") && !fs::exists(dir.file("big.lvl")),
                "blank --new-map: 100 x 100 is more than 120 KB");
        t.check(run_words({"blank", dir.file("huge.lvl"), "--rows", "300", "--columns", "3"}).status == 1, "blank: 300 rows are more than the game hosts");
        t.check(run_words({"blank", dir.file("x.lvl"), "--rows", "3", "--columns", "3", "--chd", tiny_path}).status == 2, "blank: a --chd that is no archive is exit 2");
    }

    std::cout << "lvl_tool selftest: " << t.checks << " checks, " << t.failures << " failures\n";
    return t.failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    if (args.size() == 1 && args[0] == "--selftest") return selftest();
    return run_tool(args, std::cout, std::cerr);
}

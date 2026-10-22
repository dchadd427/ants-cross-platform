// lvl_tool: checks, describes and rewrites map files (.lvl), and makes a blank map; the format, the canonical form and the limits are docs/MAP_FORMAT.md.
//
//   lvl_tool check FILE [--roster MASK] [--new-map]          whether the file loads and plays for the teams of MASK (default 15, all four), and every finding
//   lvl_tool check FILE --original [--new-map]               whether the map works in the original game (the original-safe profile, lvl_checker.hpp): every finding
//                                                            with its code, and exit 0 only when there is no fatal finding and no warning; with --new-map a map that
//                                                            is too big for a new map is said so on a line of its own (a limit of the map maker, not of the original)
//   lvl_tool check FILE --extended                           the checker per kind (lvl_checker.hpp, check_map): the file's kind by its version (Classic map, version 8; Extended map, version 9),
//                                                            the headline of the kind ("Works in the original game", "Works in the new version" with the badge "New version
//                                                            only") and every finding with its code; the place that takes Extended maps (LevelLimits::for_extended_maps). Without
//                                                            --extended a version 9 file is refused as it always was
//   lvl_tool info FILE                                       what the file holds: grid, dictionary, start markers, hills, food, waypoints, eggs, whether it works in the
//                                                            original game, and what a rewrite changes; of an Extended map (version 9) also its kind, format flags, rules and
//                                                            whether it fits a Classic map (and why not)
//   lvl_tool rewrite IN OUT [--roster MASK] [--new-map]      reads IN, checks it, writes its canonical bytes to OUT and says what differs from IN (OUT must be another file than IN;
//                                                            the bytes go to a new file beside OUT, which takes OUT's place once it is whole)
//   lvl_tool blank OUT --rows R --columns C [--chd FILE] [--new-map]
//                                                            a new map of plain ground, R x C (1 to 256 each), its dictionary by the rule of the original maps (names from ants.chd)
//   lvl_tool blank OUT --extended --rows R --columns C [--chd FILE]
//                                                            a new Extended map (version 9) of plain ground: at most 1,024 a side and 262,144 cells (LevelLimits::for_extended_maps)
//   lvl_tool convert FILE --to extended|classic -o OUT [--rows R --columns C]
//                                                            a COPY of the map as an Extended map (always) or a Classic map (when it fits; else the reasons are listed and nothing is
//                                                            written); FILE is kept, OUT must be another file. With --rows and --columns (Extended only) the copy is grown to
//                                                            that size by plain ground at the right and below: a big map that works, for the tests of the server and the site
//       --roster MASK  the teams that play: bit 0 green, 1 red, 2 blue, 3 black (1 .. 15)
//       --new-map      hold the file to the limits of a new Classic map (LevelLimits::for_new_maps): at most 90 rows, 130 columns and 10,000 cells, a file of at most 512 KB is read and
//                      a canonical file of at most 144 KB written
//       --chd FILE     where the tile names are read (default: the archive of this checkout, Original-Ants/ants.chd)
//   lvl_tool --selftest                                      checks the tool itself (the command line, the four commands and their exit codes) in a scratch folder
//
// A file is read up to 64 MiB (512 KB with --new-map): one that holds more is not read to its end, and it does not load. Exit status: 0 the file plays (check), is
// described (info), is written (rewrite, blank); 1 the file does not load, does not play for the roster or cannot be written within the limits (nothing is written
// then); 2 the command line is wrong, a file cannot be read or written, an OUT is refused (below), or there is no memory left.
//
// The owner's rule (library_guard.hpp, docs/WORKFLOW.md "Tools never touch the map library"): rewrite, convert and blank never write over the file they read and never write
// into or over a file of Original-Ants/ or Community-Maps/ (by any spelling: another case, `..`, a relative path, a link); they say so in words and exit 2, before they read
// anything. Reading the library is fine.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_test_paths.hpp"
#include "library_guard.hpp"

using namespace ants::assets;

namespace {

namespace fs = std::filesystem;

constexpr const char* kTeams[4] = {"green", "red", "blue", "black"};

void usage(std::ostream& out) {
    out << "usage: lvl_tool check FILE [--roster MASK | --original | --extended] [--new-map]\n"
           "       lvl_tool info FILE\n"
           "       lvl_tool rewrite IN OUT [--roster MASK] [--new-map]\n"
           "       lvl_tool blank OUT --rows R --columns C [--chd FILE] [--new-map]\n"
           "       lvl_tool blank OUT --extended --rows R --columns C [--chd FILE]\n"
           "       lvl_tool convert FILE --to extended|classic -o OUT [--rows R --columns C]\n"
           "       lvl_tool --selftest\n"
           "  check    whether the file loads and plays for the teams of the roster, and every finding\n"
           "           (with --original: whether the map works in the original game, every finding with its code;\n"
           "           with --extended: the checker per kind, which also takes an Extended map and answers for its kind)\n"
           "  info     what the file holds, whether it works in the original game, and what a rewrite would change\n"
           "  rewrite  reads IN, checks it and writes its canonical bytes to OUT (another file than IN); says what differs\n"
           "  blank    writes a new map of plain ground, R rows x C columns (1 to 256 each), its dictionary by the rule of the original maps\n"
           "  convert  writes a copy of the map as an Extended map (version 9, always) or a Classic map (version 8, when it fits: else the reasons are listed); FILE is kept\n"
           "           (with --rows and --columns the Extended copy is grown to that size by plain ground at the right and below, to make a big map that works)\n"
           "  --extended     blank: a new Extended map, at most 1,024 a side and 262,144 cells (rows and columns 1 to 2,048 are read, the limits refuse more)\n"
           "                 check: the checker per kind, for a Classic or an Extended map (the limits of an Extended map)\n"
           "  --roster MASK  the teams that play: bit 0 green, 1 red, 2 blue, 3 black (1 .. 15; default 15)\n"
           "  --new-map      the limits of a new Classic map: at most 90 rows, 130 columns and 10,000 cells; a file of at most 512 KB is read, 144 KB written\n"
           "  --chd FILE     the archive with the tile names (default: Original-Ants/ants.chd of this checkout)\n"
           "  A command never writes over the file it reads and never writes into or over a file of Original-Ants/ or Community-Maps/ (exit 2, nothing written).\n"
           "  --selftest     check the tool itself\n";
}

constexpr size_t kMostBytesRead = 64u * 1024u * 1024u;   // the most of a file that the tool reads (a map of the game has a few MB at the most, ants.chd 8 MB)

enum class Reading { Read, Unreadable, TooLarge };

// The bytes of the file at `path`, at most `limit` of them: Read; Unreadable when it cannot be opened or read; TooLarge when it holds more, which are not read (so
// that `lvl_tool check /dev/zero` ends), with its size in `size` when the system knows it (a file on a disk) and 0 when it does not (a device, a pipe)
Reading read_file(const std::string& path, size_t limit, std::vector<uint8_t>& bytes, uint64_t& size) {
    bytes.clear();
    size = 0;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        const uintmax_t on_disk = fs::file_size(path, ec);
        if (!ec && on_disk > limit) {
            size = static_cast<uint64_t>(on_disk);
            return Reading::TooLarge;
        }
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return Reading::Unreadable;
    std::vector<char> chunk(64 * 1024);
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const size_t got = static_cast<size_t>(in.gcount());
        if (got > limit - bytes.size()) {
            bytes.clear();
            return Reading::TooLarge;
        }
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }
    return in.bad() ? Reading::Unreadable : Reading::Read;
}

// What the tool says of a file larger than it reads: the reader's own sentence (FileTooLarge) when its size is known
std::string too_large(uint64_t size, size_t limit) {
    if (size != 0) return "the file has " + std::to_string(size) + " bytes, more than the " + std::to_string(limit) + " allowed here: nothing of it is read";
    return "the file has more than the " + std::to_string(limit) + " bytes allowed here: nothing more of it is read";
}

// Writes `bytes` to `path` through a new file beside it, which takes the place of `path` once it is complete: a file that cannot be written whole leaves `path` as
// it was (a file that cannot be written whole never cuts anything short), and the new file is removed
bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    const fs::path target(path);
    std::random_device random;
    std::error_code ec;
    fs::path temp;
    for (int attempt = 0; attempt < 100; ++attempt) {
        temp = target;
        temp += ".lvl_tool-" + std::to_string(random()) + ".tmp";
        if (!fs::exists(temp, ec)) break;
    }
    bool whole = false;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        whole = static_cast<bool>(out);
    }
    if (whole) fs::rename(temp, target, ec);
    if (!whole || ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return false;
    }
    return true;
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

// The owner's rule: exit 2 with the reason when `out` may not be written (it is the file that is read, or it is in the map library); 0 when it may
int refuse_output(const std::string& out, const std::vector<std::string>& inputs, std::ostream& err) {
    const std::string why = library_guard::write_refusal(out, inputs);
    if (why.empty()) return 0;
    err << "lvl_tool: " << why << "; nothing written\n";
    return 2;
}

struct Options {
    std::vector<std::string> files;
    uint8_t roster{0x0F};
    bool roster_given{false};
    bool original{false};
    bool new_map{false};
    bool extended{false};
    std::string to;
    std::string output;
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
    o.extended = std::find(args.begin() + 1, args.end(), "--extended") != args.end();      // (it decides how many rows and columns a number may be)
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool has_value = i + 1 < args.size();
        uint32_t v = 0;
        if (a == "--roster") {
            if (!has_value || !number(args[++i], 1, 15, v)) { why = "--roster takes a number from 1 to 15"; return false; }
            o.roster = static_cast<uint8_t>(v);
            o.roster_given = true;
        } else if (a == "--original") {
            o.original = true;
        } else if (a == "--rows" || a == "--columns") {
            const uint32_t most = o.extended || args[0] == "convert" ? LVL_FORMAT_CEILING_SIDE : LVL_MAX_GRID_SIDE;
            if (!has_value || !number(args[++i], 1, most, v)) { why = a + " takes a number from 1 to " + std::to_string(most); return false; }
            (a == "--rows" ? o.rows : o.columns) = v;
        } else if (a == "--chd") {
            if (!has_value) { why = "--chd takes a file"; return false; }
            o.chd = args[++i];
        } else if (a == "--new-map") {
            o.new_map = true;
        } else if (a == "--extended") {
            // (read above)
        } else if (a == "--to") {
            if (!has_value || (args[i + 1] != "extended" && args[i + 1] != "classic")) { why = "--to takes extended or classic"; return false; }
            o.to = args[++i];
        } else if (a == "-o" || a == "--output") {
            if (!has_value) { why = a + " takes a file"; return false; }
            o.output = args[++i];
        } else if (a.size() > 1 && a[0] == '-') {
            why = "unknown option " + a;
            return false;
        } else {
            o.files.push_back(a);
        }
    }
    return true;
}

// Reads the map file of a command, as far as `limits` let the reader take it: 0 when its bytes are in `bytes`, else the exit status, with what was said (2: it
// cannot be read; 1: it holds more bytes than are read, so it does not load; `tail` ends that sentence)
int read_map_file(const std::string& path, const LevelLimits& limits, std::vector<uint8_t>& bytes, std::ostream& out, std::ostream& err, const char* tail) {
    const size_t most = std::min(limits.read_bytes(), kMostBytesRead);
    uint64_t size = 0;
    const Reading reading = read_file(path, most, bytes, size);
    if (reading == Reading::Unreadable) {
        err << "lvl_tool: cannot read " << path << "\n";
        return 2;
    }
    if (reading == Reading::TooLarge) {
        const std::string why = too_large(size, most);
        out << file_name(path) << ": does not load: " << why << tail << "\n  " << to_string(LevelProblemSeverity::Fatal) << ' ' << to_string(LevelProblemKind::FileTooLarge)
            << ": " << why << "\n";
        return 1;
    }
    return 0;
}

// The codes of a report's findings, each once, in order: those that stop "works in the original game" (`notes` false) or the notes
std::string codes_of(const OriginalSafeReport& r, bool notes) {
    std::vector<std::string> codes;
    for (const OriginalSafeIssue& i : r.issues) {
        const std::string code = to_string(i.code);
        if ((i.severity == LevelProblemSeverity::Info) == notes && std::find(codes.begin(), codes.end(), code) == codes.end()) codes.push_back(code);
    }
    std::string text;
    for (const std::string& c : codes) text += (text.empty() ? "" : ", ") + c;
    return text;
}

int check_original(const std::string& name, const std::vector<uint8_t>& bytes, const LevelLimits& limits, std::ostream& out) {
    const OriginalSafeReport r = check_original_safe(bytes.data(), bytes.size(), limits);
    // the verdict on the original game first: the findings that stop it come first (a limit of --new-map is none of them: it has its own line below)
    if (r.works_in_original_game()) {
        bool notes = false;
        for (const OriginalSafeIssue& i : r.issues) notes = notes || i.severity == LevelProblemSeverity::Info;
        out << name << ": works in the original game" << (r.issues.empty() ? "; no findings" : notes ? "; notes:" : "") << "\n";
    } else if (r.judged) {
        for (const OriginalSafeIssue& i : r.issues) {
            if (i.severity == LevelProblemSeverity::Info || i.place_limit) continue;
            out << name << ": does not work in the original game: " << i.message << "\n";
            break;
        }
    }
    for (const OriginalSafeIssue& i : r.issues) {
        if (i.place_limit) out << name << ": too big for a new map (a limit of the map maker, the original has none): " << i.message << "\n";
    }
    for (const OriginalSafeIssue& i : r.issues) out << "  " << (i.place_limit ? "limit" : to_string(i.severity)) << ' ' << to_string(i.code) << ": " << i.message << "\n";
    return r.works_in_original_game() && r.fits_the_place() ? 0 : 1;
}

// The checker per kind (check --extended): the headline of the kind, then every finding with its code
int check_kind(const std::string& name, const std::vector<uint8_t>& bytes, const LevelLimits& limits, std::ostream& out) {
    const MapCheckReport m = check_map(bytes.data(), bytes.size(), limits);
    const std::string kind = m.kind == MapKind::Extended ? "Extended map" : "Classic map";
    const std::string badge = m.badge.empty() ? "" : ", " + m.badge;
    const OriginalSafeIssue* blocking = nullptr;
    bool notes = false;
    for (const OriginalSafeIssue& i : m.report.issues) {
        if (i.severity == LevelProblemSeverity::Info) notes = true;
        else if (!i.place_limit && blocking == nullptr) blocking = &i;
    }
    out << name << ": " << m.headline << " (" << kind << badge << ")";
    if (!m.works && blocking != nullptr) out << ": " << blocking->message;
    else if (m.works && m.report.issues.empty()) out << "; no findings";
    else if (m.works && notes) out << "; notes:";
    out << "\n";
    for (const OriginalSafeIssue& i : m.report.issues) {
        if (i.place_limit) out << name << ": beyond the limits of this place (a limit of the place that reads the map, not a rule of the " << (m.kind == MapKind::Extended ? "new version" : "original") << "): " << i.message << "\n";
    }
    for (const OriginalSafeIssue& i : m.report.issues) out << "  " << (i.place_limit ? "limit" : to_string(i.severity)) << ' ' << to_string(i.code) << ": " << i.message << "\n";
    return m.works && m.fits_the_place ? 0 : 1;
}

int check(const Options& o, std::ostream& out, std::ostream& err) {
    const std::string& path = o.files[0];
    if (o.extended) {
        const LevelLimits extended_limits = LevelLimits::for_extended_maps();
        std::vector<uint8_t> bytes;
        if (const int status = read_map_file(path, extended_limits, bytes, out, err, "")) return status;
        return check_kind(file_name(path), bytes, extended_limits, out);
    }
    const LevelLimits limits = o.new_map ? LevelLimits::for_new_maps() : LevelLimits{};
    std::vector<uint8_t> bytes;
    if (const int status = read_map_file(path, limits, bytes, out, err, "")) return status;
    const std::string name = file_name(path);
    if (o.original) return check_original(name, bytes, limits, out);
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

// The limits for a file that may be an Extended map: the ceiling of the format, with no cap of a place (the tool reads what the format holds)
LevelLimits ceiling_limits() { return LevelLimits::format_ceiling(); }

// Is the version word of these bytes 9 (an Extended map)?
bool says_extended(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 4 && bytes[0] == LVL_EXTENDED_VERSION && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
}

// Text from a file (a map's description is a player's) printed so that it is only data: a quote and a backslash get a backslash, a line end is \n, \r or \t, every other control character and every
// byte from 0x80 up is \xHH. Nothing in it can move the cursor, set a color, or look like a line of this tool's own.
std::string escaped(const std::string& text) {
    std::string out;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += ch;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c >= 0x7F) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "\\x%02x", c);
            out += hex;
        } else {
            out += ch;
        }
    }
    return out;
}

int info(const Options& o, std::ostream& out, std::ostream& err) {
    const std::string& path = o.files[0];
    std::vector<uint8_t> bytes;
    if (const int status = read_map_file(path, ceiling_limits(), bytes, out, err, "")) return status;
    const bool extended = says_extended(bytes);
    const LevelLimits limits = extended ? ceiling_limits() : LevelLimits{};         // (a Classic map is read as it always was: its findings are the same)
    LevelData level;
    LevelValidation report;
    if (!level.load_from_memory(bytes.data(), bytes.size(), &report, limits)) {
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
        << "Version:       " << level.version << ", mode " << level.game_mode << "\n";
    if (!extended) out << "Kind:          Classic map (version " << level.version << ")\n";
    if (extended) {
        char flags[16];
        std::snprintf(flags, sizeof flags, "0x%X", level.format_flags);
        out << "Kind:          Extended map (version 9; new version only)\n"
            << "Format flags:  " << flags << (level.format_flags == 0 ? " (none)" : "") << "\n"
            << "Rules:         " << level.rules << (level.rules == LVL_RULES_ORIGINAL ? " (the original's rules)" : " (extended rules)") << "\n";
    }
    out << "Description:   \"" << escaped(level.description) << "\" (" << level.description.size() << " bytes)\n"
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
    const OriginalSafeReport original = check_original_safe(level);
    const std::string notes = codes_of(original, true);
    if (original.not_for_original()) {
        out << "Original game: not for the original game (needs a version 8 file)\n";
    } else {
        out << "Original game: " << (original.works_in_original_game() ? "works" : "does not work: " + codes_of(original, false))
            << (notes.empty() ? (original.issues.empty() ? ", no findings" : "") : "; notes: " + notes) << "\n";
    }
    if (extended) {
        const MapCheckReport kind = check_map(level, ceiling_limits());
        const std::string kind_notes = codes_of(kind.report, true);
        out << "New version:   " << (kind.works ? "works" : "does not work: " + codes_of(kind.report, false))
            << (kind_notes.empty() ? "" : "; notes: " + kind_notes) << "\n";
        const std::vector<std::string> reasons = needs_extended(level);
        if (reasons.empty()) {
            out << "Classic map:   fits (lvl_tool convert FILE --to classic -o OUT)\n";
        } else {
            out << "Classic map:   does not fit:\n";
            for (const std::string& r : reasons) out << "  " << r << "\n";
        }
    }
    const LevelWriteResult w = write_level(level, limits);
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
    const LevelLimits limits = o.new_map ? LevelLimits::for_new_maps() : LevelLimits{};
    if (const int status = refuse_output(out_path, {in_path}, err)) return status;
    std::vector<uint8_t> bytes;
    if (const int status = read_map_file(in_path, limits, bytes, out, err, "; nothing written")) return status;
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

int convert(const Options& o, std::ostream& out, std::ostream& err) {
    if (o.to.empty() || o.output.empty()) {
        err << "lvl_tool: convert needs --to extended|classic and -o OUT\n";
        usage(err);
        return 2;
    }
    const std::string& in_path = o.files[0];
    if (const int status = refuse_output(o.output, {in_path}, err)) return status;     // (a conversion makes a copy and keeps the map it was made from)
    std::vector<uint8_t> bytes;
    if (const int status = read_map_file(in_path, ceiling_limits(), bytes, out, err, "; nothing written")) return status;
    const std::string name = file_name(in_path);
    LevelData level;
    LevelValidation report;
    if (!level.load_from_memory(bytes.data(), bytes.size(), &report, ceiling_limits())) {
        out << name << ": does not load: " << report.reason() << "; nothing written\n";
        print_findings(report, out);
        return 1;
    }
    const bool to_extended = o.to == "extended";
    const LevelConversion c = to_extended ? convert_to_extended(level) : convert_to_classic(level);
    if (!c.ok) {
        out << name << ": cannot be " << (to_extended ? "an Extended" : "a Classic") << " map:\n";
        for (const std::string& r : c.reasons) out << "  " << r << "\n";
        out << "nothing written\n";
        return 1;
    }
    LevelData result = c.level;
    if (o.rows != 0 || o.columns != 0) {                 // --rows and --columns: the Extended copy is grown to that size with plain ground at the right and below (for a test that needs a big map that works)
        if (!to_extended || o.rows == 0 || o.columns == 0) {
            err << "lvl_tool: --rows and --columns go together and with --to extended only (the copy is grown to that size)\n";
            return 2;
        }
        if (o.rows < c.level.height() || o.columns < c.level.width()) {
            out << name << ": is " << c.level.height() << " rows x " << c.level.width() << " columns, more than " << o.rows << " x " << o.columns << "; nothing written\n";
            return 1;
        }
        LevelData grown = blank_extended_level(o.rows, o.columns, lvl_core::tile_names(), ceiling_limits());
        grown.tile_dictionary = c.level.tile_dictionary;
        grown.tile_type_count = c.level.tile_type_count;
        grown.description = c.level.description;
        grown.default_minutes = c.level.default_minutes;
        grown.game_mode = c.level.game_mode;
        for (uint32_t y = 0; y < c.level.height(); ++y) {
            for (uint32_t x = 0; x < c.level.width(); ++x) {
                grown.layer1_terrain[static_cast<size_t>(y) * o.columns + x] = c.level.layer1_terrain[static_cast<size_t>(y) * c.level.width() + x];
                grown.layer2_interactive[static_cast<size_t>(y) * o.columns + x] = c.level.layer2_interactive[static_cast<size_t>(y) * c.level.width() + x];
            }
        }
        grown.anthill_spawns = c.level.anthill_spawns;
        grown.food_schedules = c.level.food_schedules;
        grown.waypoints = c.level.waypoints;
        grown.ambient_flag = c.level.ambient_flag;
        grown.ambient_tile_or_sound = c.level.ambient_tile_or_sound;
        grown.boundary_param = c.level.boundary_param;
        result = std::move(grown);
    }
    const LevelWriteResult w = write_level(result, to_extended ? ceiling_limits() : LevelLimits{});
    if (!w.ok) {
        out << name << ": cannot be written: " << w.error << "; nothing written\n";
        return 1;
    }
    if (!write_file(o.output, w.bytes)) {
        err << "lvl_tool: cannot write " << o.output << "\n";
        return 2;
    }
    out << name << " -> " << file_name(o.output) << ": " << (level.version == c.level.version ? "already " : "converted to ") << (to_extended ? "an Extended map (version 9)" : "a Classic map (version 8)")
        << ", " << w.bytes.size() << " bytes written to a new file; " << name << " is kept\n";
    for (const std::string& n : c.notes) out << "  note: " << n << "\n";
    return 0;
}

int blank(const Options& o, std::ostream& out, std::ostream& err) {
    if (o.rows == 0 || o.columns == 0) {
        err << "lvl_tool: blank needs --rows and --columns\n";
        usage(err);
        return 2;
    }
    const std::string chd = o.chd.empty() ? std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd" : o.chd;
    if (const int status = refuse_output(o.files[0], {chd}, err)) return status;
    std::vector<uint8_t> archive;
    uint64_t size = 0;
    if (read_file(chd, kMostBytesRead, archive, size) != Reading::Read) {
        err << "lvl_tool: cannot read " << chd << "\n";
        return 2;
    }
    const std::vector<std::string> names = tile_names_from_chd(archive.data(), archive.size());
    if (names.empty()) {
        err << "lvl_tool: " << chd << " is no archive of the game (no tile names)\n";
        return 2;
    }
    if (o.extended && o.new_map) {
        err << "lvl_tool: --extended has its own limits (at most 1,024 a side and 262,144 cells): it takes no --new-map\n";
        return 2;
    }
    const LevelLimits limits = o.extended ? LevelLimits::for_extended_maps() : o.new_map ? LevelLimits::for_new_maps() : LevelLimits{};
    const LevelData level = o.extended ? blank_extended_level(o.rows, o.columns, names, limits)
                                       : blank_level(static_cast<uint16_t>(o.rows), static_cast<uint16_t>(o.columns), names, limits);   // (a grid beyond the limits gets no cell)
    const LevelWriteResult w = write_level(level, limits);
    if (!w.ok) {
        out << "a blank map of " << o.rows << " rows x " << o.columns << " columns cannot be written: " << w.error << "; nothing written\n";
        return 1;
    }
    if (!write_file(o.files[0], w.bytes)) {
        err << "lvl_tool: cannot write " << o.files[0] << "\n";
        return 2;
    }
    out << file_name(o.files[0]) << ": a blank " << (o.extended ? "Extended " : "") << "map of " << o.rows << " rows x " << o.columns << " columns, " << w.bytes.size() << " bytes (plain ground, a dictionary of "
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
    bool ok = (command == "check" || command == "info" || command == "rewrite" || command == "blank" || command == "convert") && parse(args, o, why) && o.files.size() == files;
    if (ok && o.extended && command != "blank" && command != "check") {
        why = "--extended goes with blank and check only";
        ok = false;
    } else if (ok && o.extended && command == "check" && (o.original || o.roster_given || o.new_map)) {
        why = "--extended takes no --original, --roster or --new-map (the checker per kind holds the map to all four teams, with the limits of an Extended map)";
        ok = false;
    } else if (ok && (!o.to.empty() || !o.output.empty()) && command != "convert") {
        why = "--to and -o go with convert only";
        ok = false;
    } else if (ok && o.original && command != "check") {
        why = "--original goes with check only";
        ok = false;
    } else if (ok && o.original && o.roster_given) {
        why = "--original holds the map to all four teams: it takes no --roster";
        ok = false;
    }
    if (!ok) {
        if (!why.empty()) err << "lvl_tool: " << why << "\n";
        usage(err);
        return 2;
    }
    if (command == "check") return check(o, out, err);
    if (command == "info") return info(o, out, err);
    if (command == "rewrite") return rewrite(o, out, err);
    if (command == "convert") return convert(o, out, err);
    return blank(o, out, err);
}

// The tool as main runs it: a failure that it does not foresee (no memory left, an exception of the library) ends it with one line and status 2, not an abort
int guarded(const std::function<int()>& run, std::ostream& err) {
    try {
        return run();
    } catch (const std::bad_alloc&) {
        err << "lvl_tool: out of memory\n";
    } catch (const std::exception& e) {
        err << "lvl_tool: " << e.what() << "\n";
    }
    return 2;
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

bool read_whole(const std::string& path, std::vector<uint8_t>& bytes) {
    uint64_t size = 0;
    return read_file(path, kMostBytesRead, bytes, size) == Reading::Read;
}

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
    if (!dir.ok() || !read_whole(tiny_path, tiny) || !tiny_level.load_from_memory(tiny.data(), tiny.size())) {
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
        return read_whole(path, bytes) && level.load_from_memory(bytes.data(), bytes.size());
    };

    std::cout << "  the command line\n";
    {
        const Run help = run_words({"--help"});
        t.check(help.status == 0 && has(help.out, "usage: lvl_tool") && help.err.empty(), "--help prints the usage and succeeds");
        for (const std::vector<std::string>& bad : std::vector<std::vector<std::string>>{
                 {}, {"nonsense"}, {"check"}, {"check", "a", "b"}, {"rewrite", "a"}, {"info"}, {"check", tiny_path, "--roster", "0"}, {"check", tiny_path, "--roster", "16"},
                 {"check", tiny_path, "--roster"}, {"check", tiny_path, "--what"}, {"blank", "x.lvl", "--rows", "0", "--columns", "5"}, {"blank", "x.lvl", "--rows", "-3"},
                 {"blank", "x.lvl", "--rows", "5", "--columns", "257"}, {"blank", "x.lvl", "--rows", "65535", "--columns", "65535"},
                 {"check", tiny_path, "--original", "--roster", "3"}, {"info", tiny_path, "--original"}, {"rewrite", tiny_path, "x.lvl", "--original"}}) {
            const Run r = run_words(bad);
            t.check(r.status == 2 && r.out.empty() && has(r.err, "usage: lvl_tool"), "a wrong command line (" + std::to_string(bad.size()) + " words) is exit 2 with the usage");
        }
        const Run missing = run_words({"check", dir.file("no such map.lvl")});
        t.check(missing.status == 2 && has(missing.err, "cannot read"), "a file that is not there is exit 2 and says so");
        t.check(run_words({"blank", dir.file("b.lvl"), "--rows", "5"}).status == 2, "blank without --columns is exit 2");
        const Run side = run_words({"blank", dir.file("b.lvl"), "--rows", "65535", "--columns", "65535"});
        t.check(side.status == 2 && has(side.err, "lvl_tool: --rows takes a number from 1 to 256\n") && !fs::exists(dir.file("b.lvl")),
                "blank --rows 65535: a grid the game cannot host is a wrong command line, before anything is made");
        t.check(has(run_words({"check", tiny_path, "--original", "--roster", "3"}).err, "lvl_tool: --original holds the map to all four teams: it takes no --roster\n") &&
                    has(run_words({"info", tiny_path, "--original"}).err, "lvl_tool: --original goes with check only\n"),
                "--original: with a roster or another command it says why");
    }

    std::cout << "  what the tool does not foresee\n";
    {
        std::ostringstream no_memory;
        t.check(guarded([]() -> int { throw std::bad_alloc(); }, no_memory) == 2 && no_memory.str() == "lvl_tool: out of memory\n",
                "no memory left is exit 2 with one line, not an abort");
        std::ostringstream failure;
        t.check(guarded([]() -> int { throw std::runtime_error("a failure"); }, failure) == 2 && failure.str() == "lvl_tool: a failure\n",
                "an exception of the library is exit 2 with its words");
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
        // a grid of 131 columns: a map of the game, not a new map (130 columns at the most)
        LevelData wide = blank_level(4, 131, std::vector<std::string>(1344, "n"));
        const std::string wide_path = file_of("wide.lvl", write_level(wide).bytes);
        t.check(run_words({"check", wide_path}).status == 0, "check: 4 x 131 plays");
        const Run limited = run_words({"check", wide_path, "--new-map"});
        t.check(limited.status == 1 && has(limited.out, "more than the 90 x 130 allowed here"), "check --new-map: 131 columns are more than a new map has");
        // the size of a file: with --new-map one of more than 512 KB is not read (TINY and 600,000 bytes after its egg word), one of 512 KB is
        std::vector<uint8_t> large = tiny;
        large.insert(large.end(), 600000, 0x55);
        const std::string large_path = file_of("large.lvl", large);
        t.check(run_words({"check", large_path}).status == 0, "check: TINY with 600,000 bytes after its egg word plays (they are never read)");
        const Run too_large = run_words({"check", large_path, "--new-map"});
        t.check(too_large.status == 1 && too_large.out == "large.lvl: does not load: the file has 619312 bytes, more than the 524288 allowed here: nothing of it is read\n"
                                                          "  fatal file_too_large: the file has 619312 bytes, more than the 524288 allowed here: nothing of it is read\n",
                "check --new-map: a file of more than 512 KB does not load: " + too_large.out);
        large.resize(524288);
        t.check(run_words({"check", file_of("limit.lvl", large), "--new-map"}).status == 0, "check --new-map: a file of exactly 512 KB is read");
        // a file without end (a device): read up to the limit, not to its end
        if (fs::exists("/dev/zero")) {
            const Run zero = run_words({"check", "/dev/zero", "--new-map"});
            t.check(zero.status == 1 && has(zero.out, "zero: does not load: the file has more than the 524288 bytes allowed here: nothing more of it is read\n"),
                    "check /dev/zero --new-map ends: " + zero.out);
            const Run game = run_words({"check", "/dev/zero"});
            t.check(game.status == 1 && has(game.out, "the file has more than the 67108864 bytes allowed here"), "check /dev/zero ends at 64 MiB: " + game.out);
        }
    }

    std::cout << "  check --original\n";
    {
        const Run r = run_words({"check", tiny_path, "--original"});
        t.check(r.status == 0 && r.out == "TINY.LVL: works in the original game; no findings\n" && r.err.empty(), "check --original: TINY works in the original game: " + r.out);
        // Blue's hill (rows 4 .. 7, columns 23 .. 26 of TINY's layer 2) taken away
        std::vector<uint8_t> nohill = tiny;
        const size_t layer2 = 42 + 670 * 11 + 8 + 31 * 31 * 6;
        for (size_t row = 4; row <= 7; ++row) {
            for (size_t column = 23; column <= 26; ++column) {
                const size_t at = layer2 + (row * 31 + column) * 6;
                put16(nohill, at, 0x7FFE);
                put16(nohill, at + 2, 0);
                put16(nohill, at + 4, 0);
            }
        }
        const Run n = run_words({"check", file_of("nohill.lvl", nohill), "--original"});
        t.check(n.status == 1 && n.out == "nohill.lvl: does not work in the original game: Blue has no hill\n  warning hill_missing: Blue has no hill\n",
                "check --original: a map without Blue's hill does not work in the original game: " + n.out);
        t.check(run_words({"check", dir.file("nohill.lvl")}).status == 0, "check: the remake plays it all the same");
        std::vector<uint8_t> mode = tiny;
        put16(mode, 4, 4631);
        const Run m = run_words({"check", file_of("mode.lvl", mode), "--original"});
        t.check(m.status == 0 && m.out == "mode.lvl: works in the original game; notes:\n  note mode_not_1: the mode word is 4631, not 1: the original never reads it (the map maker writes 1)\n",
                "check --original: a note does not stop it: " + m.out);
        const Run c = run_words({"check", dir.file("cut.lvl"), "--original"});
        t.check(c.status == 1 && has(c.out, "cut.lvl: does not work in the original game: the file ends inside the layers") && has(c.out, "  fatal file_cut_short: "),
                "check --original: a file cut short does not work, and says where");
        // TINY made 131 columns wide (the new columns are plain ground): the original plays it, a new map may not be so wide
        LevelData broad = tiny_level;
        broad.width.val = 131;
        broad.layer1_terrain.assign(31u * 131u, tiny_level.layer1_terrain[0]);
        broad.layer2_interactive.assign(31u * 131u, MapCell{LVL_EMPTY_TILE, 0, 0});
        for (size_t row = 0; row < 31; ++row) {
            for (size_t column = 0; column < 31; ++column) {
                broad.layer1_terrain[row * 131 + column] = tiny_level.layer1_terrain[row * 31 + column];
                broad.layer2_interactive[row * 131 + column] = tiny_level.layer2_interactive[row * 31 + column];
            }
        }
        const LevelWriteResult broad_file = write_level(broad);
        t.check(broad_file.ok, "a 31 x 131 map of TINY is written");
        const std::string broad_path = file_of("broad.lvl", broad_file.bytes);
        const Run b_game = run_words({"check", broad_path, "--original"});
        t.check(b_game.status == 0 && has(b_game.out, "broad.lvl: works in the original game") && !has(b_game.out, "too big"),
                "check --original: 131 columns work in the original game: " + b_game.out);
        const Run b_new = run_words({"check", broad_path, "--original", "--new-map"});
        t.check(b_new.status == 1 && has(b_new.out, "broad.lvl: works in the original game") && !has(b_new.out, "does not work") &&
                    has(b_new.out, "broad.lvl: too big for a new map (a limit of the map maker, the original has none): the grid has 31 rows x 131 columns, more than the 90 x 130 allowed here") &&
                    has(b_new.out, "  limit grid_too_large: "),
                "check --original --new-map: 131 columns are a limit of the map maker, not under the heading that the original does not work: " + b_new.out);
        t.check(has(run_words({"info", dir.file("nohill.lvl")}).out, "Original game: does not work: hill_missing\n") &&
                    has(run_words({"info", dir.file("mode.lvl")}).out, "Original game: works; notes: mode_not_1\n"),
                "info: whether the map works in the original game, by code");
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
        t.check(has(r.out, "Original game: works, no findings\n"), "info: TINY works in the original game");
        t.check(has(r.out, "Rewrite:       19312 bytes\n  name_padding: 670 of the 670 dictionary names have leftover bytes"), "info: what a rewrite changes");
        // the description is a player's text: printed escaped, never raw (no line end, no escape byte, no angle bracket's meaning, and the quote cannot end the field early)
        LevelData described = tiny_level;
        described.description = "A\n\x1B[1m\"q\"\\<\xC3\xA9";
        const std::string described_path = file_of("described.lvl", write_level(described).bytes);
        const Run d = run_words({"info", described_path});
        t.check(d.status == 0 && has(d.out, "Description:   \"A\\n\\x1b[1m\\\"q\\\"\\\\<\\xc3\\xa9\" (13 bytes)\n"), "info: a description with a line end, an escape byte, quotes and a UTF-8 letter is printed escaped");
        t.check(d.out.find('\x1B') == std::string::npos && d.out.find("A\n") == std::string::npos, "info: no raw escape byte and no raw line end of the description is in the output");
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
        t.check(read_whole(out1, written) && written == write_level(tiny_level).bytes, "rewrite: the file is the canonical bytes");
        const std::string out2 = dir.file("tiny_out2.lvl");
        const Run again = run_words({"rewrite", out1, out2});
        std::vector<uint8_t> second;
        t.check(again.status == 0 && has(again.out, "the file was canonical already: the same bytes") && read_whole(out2, second) && second == written,
                "rewrite of a canonical file: the same bytes");
        const Run self = run_words({"rewrite", out2, out2});
        t.check(self.status == 2 && has(self.err, "never writes over its own input") && has(self.err, "nothing written") && read_whole(out2, second) && second == written,
                "rewrite: OUT = IN is refused in words, exit 2, and the file stays as it was: " + self.err);
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
        t.check(limited.status == 1 && has(limited.out, "more than the 90 x 130 allowed here") && !fs::exists(dir.file("wide_out.lvl")), "rewrite --new-map: 131 columns are not written");
        const std::string folder = dir.file("folder");
        std::error_code made;
        fs::create_directories(folder, made);
        t.check(run_words({"rewrite", tiny_path, folder}).status == 2 && fs::is_directory(folder), "rewrite: a folder as OUT is exit 2, and the folder stays");
        const Run nowhere = run_words({"rewrite", tiny_path, dir.file("no such folder/out.lvl")});
        t.check(nowhere.status == 2 && has(nowhere.err, "cannot write"), "rewrite: OUT in a folder that is not there is exit 2");
        const Run large = run_words({"rewrite", dir.file("large.lvl"), dir.file("large_out.lvl"), "--new-map"});
        t.check(large.status == 1 && has(large.out, "more than the 524288 allowed here: nothing of it is read; nothing written") && !fs::exists(dir.file("large_out.lvl")),
                "rewrite --new-map: a file of more than 512 KB is not read, nothing is written");
        const Run whole = run_words({"rewrite", dir.file("large.lvl"), dir.file("large_out.lvl")});
        t.check(whole.status == 0 && has(whole.out, "19312 bytes written"), "rewrite: without --new-map it is read, and its rewrite drops the 600,000 bytes");
        // every file is written through a new file beside it: none of those is left
        size_t left = 0;
        std::error_code ec;
        for (const fs::directory_entry& e : fs::directory_iterator(dir.path, ec)) left += has(e.path().filename().string(), ".lvl_tool-") ? 1u : 0u;
        t.check(left == 0 && !ec, "rewrite: no file of its own is left beside OUT");
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
        const Run big = run_words({"blank", dir.file("big.lvl"), "--rows", "90", "--columns", "100", "--new-map"});
        t.check(big.status == 0 && has(big.out, "a blank map of 90 rows x 100 columns, 113408 bytes") && fs::exists(dir.file("big.lvl")), "blank --new-map: 90 x 100 is a new map");
        const Run tall = run_words({"blank", dir.file("tall91.lvl"), "--rows", "91", "--columns", "10", "--new-map"});
        t.check(tall.status == 1 && has(tall.out, "a Classic map made now has at most 90 rows") && has(tall.out, "make it an Extended map?") && !fs::exists(dir.file("tall91.lvl")),
                "blank --new-map: 91 rows are the wall at row 90, and it says so");
        t.check(run_words({"blank", dir.file("tall91_game.lvl"), "--rows", "91", "--columns", "10"}).status == 0, "blank: without --new-map 91 rows are the game's own map");
        const Run more = run_words({"blank", dir.file("more.lvl"), "--rows", "90", "--columns", "112", "--new-map"});
        t.check(more.status == 1 && has(more.out, "10080 cells, more than the 10000 allowed here") && !fs::exists(dir.file("more.lvl")), "blank --new-map: 90 x 112 has too many cells");
        const Run huge = run_words({"blank", dir.file("huge.lvl"), "--rows", "300", "--columns", "3"});
        t.check(huge.status == 2 && has(huge.err, "--rows takes a number from 1 to 256") && !fs::exists(dir.file("huge.lvl")), "blank: 300 rows are more than the game hosts");
        t.check(run_words({"blank", dir.file("tall.lvl"), "--rows", "256", "--columns", "1"}).status == 0, "blank: 256 rows are the most the game hosts");
        const Run wide = run_words({"blank", dir.file("wide_new.lvl"), "--rows", "200", "--columns", "200", "--new-map"});
        t.check(wide.status == 1 && has(wide.out, "the grid has 200 rows x 200 columns: a Classic map made now has at most 90 rows") && !fs::exists(dir.file("wide_new.lvl")),
                "blank --new-map: 200 x 200 is not a new map (and gets no cell)");
        t.check(run_words({"blank", dir.file("x.lvl"), "--rows", "3", "--columns", "3", "--chd", tiny_path}).status == 2, "blank: a --chd that is no archive is exit 2");
    }


    std::cout << "  convert, blank --extended and info of an Extended map\n";
    {
        const std::string ext = dir.file("tiny_ext.lvl");
        const Run up = run_words({"convert", tiny_path, "--to", "extended", "-o", ext});
        t.check(up.status == 0 && up.err.empty() && has(up.out, "TINY.LVL -> tiny_ext.lvl: converted to an Extended map (version 9), ") && has(up.out, "TINY.LVL is kept\n"),
                "convert --to extended: a copy is written: " + up.out);
        LevelData ext_level;
        std::vector<uint8_t> ext_bytes;
        t.check(read_whole(ext, ext_bytes) && ext_level.load_from_memory(ext_bytes.data(), ext_bytes.size(), nullptr, LevelLimits::for_extended_maps()) && ext_level.version == 9u &&
                    ext_level.rules == 0u,
                "convert --to extended: the file is a version 9 map with the original's rules");
        const Run refused = run_words({"check", ext});
        t.check(refused.status == 1 && has(refused.out, "version 9 is not supported here") && has(refused.out, "version_not_supported"), "check of an Extended map: refused cleanly: " + refused.out);
        const Run safe = run_words({"check", ext, "--original"});
        t.check(safe.status == 1 && has(safe.out, "not_for_original") && !has(safe.out, "does not work"), "check --original of an Extended map: not for the original, no failure: " + safe.out);
        const Run info9 = run_words({"info", ext});
        t.check(info9.status == 0 && has(info9.out, "Version:       9, mode 1\n") && has(info9.out, "Kind:          Extended map (version 9; new version only)\n") &&
                    has(info9.out, "Format flags:  0x0 (none)\n") && has(info9.out, "Rules:         0 (the original's rules)\n") && has(info9.out, "Grid:          31 rows x 31 columns\n") &&
                    has(info9.out, "Original game: not for the original game (needs a version 8 file)\n") && has(info9.out, "Classic map:   fits"),
                "info of an Extended map: kind, flags, rules, and that it fits a Classic map: " + info9.out);
        const std::string back = dir.file("tiny_back.lvl");
        const Run down = run_words({"convert", ext, "--to", "classic", "-o", back});
        std::vector<uint8_t> back_bytes;
        t.check(down.status == 0 && has(down.out, "converted to a Classic map (version 8)") && read_whole(back, back_bytes) && back_bytes == write_level(tiny_level).bytes,
                "convert --to classic: the bytes of TINY's rewrite again");
        t.check(run_words({"convert", ext, "--to", "classic", "-o", ext}).status == 2 && run_words({"convert", ext, "-o", back}).status == 2 &&
                    run_words({"convert", ext, "--to", "classic"}).status == 2 && run_words({"convert", ext, "--to", "huge", "-o", back}).status == 2 &&
                    run_words({"check", ext, "--to", "classic"}).status == 2 && run_words({"info", tiny_path, "--extended"}).status == 2,
                "convert: no --to, no -o, OUT = FILE, another kind, or the words on another command are exit 2");
        const std::string grown_path = dir.file("tiny_grown.lvl");
        const Run grown = run_words({"convert", tiny_path, "--to", "extended", "-o", grown_path, "--rows", "300", "--columns", "310"});
        LevelData grown_level;
        std::vector<uint8_t> grown_bytes;
        t.check(grown.status == 0 && read_whole(grown_path, grown_bytes) && grown_level.load_from_memory(grown_bytes.data(), grown_bytes.size(), nullptr, LevelLimits::for_extended_maps()) &&
                    grown_level.height() == 300u && grown_level.width() == 310u && grown_level.anthill_spawns.size() == ext_level.anthill_spawns.size() &&
                    grown_level.validate(0x0F, LevelLimits::for_extended_maps()).playable,
                "convert --rows --columns: the copy is grown to 300 x 310, keeps its start markers and still plays: " + grown.out);
        t.check(run_words({"convert", tiny_path, "--to", "extended", "-o", dir.file("small.lvl"), "--rows", "10", "--columns", "10"}).status == 1 &&
                    run_words({"convert", tiny_path, "--to", "extended", "-o", dir.file("half.lvl"), "--rows", "300"}).status == 2 &&
                    run_words({"convert", tiny_path, "--to", "classic", "-o", dir.file("cl.lvl"), "--rows", "300", "--columns", "300"}).status == 2,
                "convert --rows --columns: a size smaller than the map is exit 1, one side alone or a Classic copy is exit 2");
        const std::string wide = dir.file("wide_ext.lvl");
        const Run big = run_words({"blank", wide, "--extended", "--rows", "300", "--columns", "400"});
        t.check(big.status == 0 && has(big.out, "wide_ext.lvl: a blank Extended map of 300 rows x 400 columns, ") && fs::exists(wide), "blank --extended: 300 x 400 is an Extended map: " + big.out);
        const Run wide_info = run_words({"info", wide});
        t.check(wide_info.status == 0 && has(wide_info.out, "Rules:         1 (extended rules)\n") && has(wide_info.out, "Classic map:   does not fit:\n") &&
                    has(wide_info.out, "this map is 400 columns wide; a Classic map has at most 256 columns\n") && has(wide_info.out, "this map is 300 rows tall; a Classic map has at most 256 rows\n"),
                "info of a wide Extended map: rules 1 and why it is not a Classic map: " + wide_info.out);
        const Run no_fit = run_words({"convert", wide, "--to", "classic", "-o", dir.file("no_fit.lvl")});
        t.check(no_fit.status == 1 && has(no_fit.out, "wide_ext.lvl: cannot be a Classic map:\n  this map is 400 columns wide") && has(no_fit.out, "nothing written\n") && !fs::exists(dir.file("no_fit.lvl")),
                "convert --to classic: a map that does not fit lists why and writes nothing: " + no_fit.out);
        // the checker per kind: check --extended answers for the kind of the file
        const Run kind_ext = run_words({"check", ext, "--extended"});
        t.check(kind_ext.status == 0 && has(kind_ext.out, "tiny_ext.lvl: Works in the new version (Extended map, New version only)") && has(kind_ext.out, "  note not_for_original: ") &&
                    !has(kind_ext.out, "warning") && !has(kind_ext.out, "version_not_supported"),
                "check --extended: a complete Extended map works in the new version, with the badge and the note: " + kind_ext.out);
        t.check(has(info9.out, "New version:   works"), "info of an Extended map: the new version's verdict: " + info9.out);
        const Run kind_classic = run_words({"check", tiny_path, "--extended"});
        t.check(kind_classic.status == 0 && has(kind_classic.out, "TINY.LVL: Works in the original game (Classic map)"), "check --extended: a Classic map is answered for its kind: " + kind_classic.out);
        const Run kind_wide = run_words({"check", wide, "--extended"});
        t.check(kind_wide.status == 1 && has(kind_wide.out, "wide_ext.lvl: Does not work in the new version (Extended map, New version only): Green has no hill\n") &&
                    has(kind_wide.out, "  warning hill_missing: Blue has no hill\n") && !has(kind_wide.out, "rows_beyond_wall"),
                "check --extended: a tall Extended map without hills fails in the new version for the hills, with no wall: " + kind_wide.out);
        t.check(run_words({"check", ext, "--extended", "--original"}).status == 2 && run_words({"check", ext, "--extended", "--roster", "3"}).status == 2 &&
                    run_words({"check", ext, "--extended", "--new-map"}).status == 2,
                "check --extended: with --original, --roster or --new-map it is exit 2");
        const Run wall = run_words({"check", dir.file("tall91_game.lvl"), "--original"});
        t.check(wall.status == 1 && has(wall.out, "warning rows_beyond_wall: the map has 91 rows: ants cannot enter or cross the 91st row of a taller map"),
                "check --original: a Classic map of 91 rows (the game's own limits) has the wall as a warning: " + wall.out);
        const Run too_big = run_words({"blank", dir.file("too_big.lvl"), "--extended", "--rows", "600", "--columns", "600"});
        t.check(too_big.status == 1 && has(too_big.out, "360000 cells, more than the 262144 allowed here") && !fs::exists(dir.file("too_big.lvl")), "blank --extended: 600 x 600 has too many cells: " + too_big.out);
        t.check(run_words({"blank", dir.file("a.lvl"), "--extended", "--rows", "2049", "--columns", "1"}).status == 2 &&
                    run_words({"blank", dir.file("a.lvl"), "--extended", "--new-map", "--rows", "5", "--columns", "5"}).status == 2,
                "blank --extended: more than 2,048 a side, or with --new-map, is exit 2");
    }

    std::cout << "  the map library is never written (library_guard.hpp)\n";
    {
        // a copy of the layout of the checkout in the scratch folder: the real library is never given to a writing mode
        const fs::path layout = dir.path / "layout";
        std::error_code ec;
        fs::create_directories(layout / "Original-Ants" / "Maps", ec);
        fs::create_directories(layout / "Community-Maps", ec);
        fs::create_directories(layout / "work", ec);
        const std::string original = (layout / "Original-Ants" / "Maps" / "TINY.LVL").string();
        const std::string community = (layout / "Community-Maps" / "a map.lvl").string();
        const std::string list = (layout / "Community-Maps" / "maps.json").string();
        t.check(write_file(original, tiny) && write_file(community, tiny) && write_file(list, std::vector<uint8_t>{'{', '}'}), "the copy of the layout is made");
        const std::vector<std::string> library_files{original, community, list};
        std::vector<std::vector<uint8_t>> before(library_files.size());
        for (size_t i = 0; i < library_files.size(); ++i) read_whole(library_files[i], before[i]);
        auto untouched = [&]() {
            for (size_t i = 0; i < library_files.size(); ++i) {
                std::vector<uint8_t> now;
                if (!read_whole(library_files[i], now) || now != before[i]) return false;
            }
            size_t files = 0;
            for (const fs::directory_entry& e : fs::recursive_directory_iterator(layout, ec)) files += e.is_regular_file(ec) ? 1u : 0u;
            return files == library_files.size();      // (nothing new beside them either: no temporary file is left)
        };
        // every writing mode, every way to name the library: refused in words, exit 2, nothing written, nothing changed
        const std::string folder_other_case = (layout / "COMMUNITY-MAPS" / "a map.lvl").string();
        const std::string original_other_case = (layout / "original-ants" / "Maps" / "TINY.LVL").string();
        const std::string with_dots = (layout / "work" / ".." / "Community-Maps" / "a map.lvl").string();
        const std::string new_in_library = (layout / "Community-Maps" / "new map.lvl").string();
        const std::string new_in_originals = (layout / "Original-Ants" / "Maps" / "NEW.LVL").string();
        const std::string deep_in_library = (layout / "Community-Maps" / "no such folder" / "x.lvl").string();
        for (const std::string& target : {community, original, list, folder_other_case, original_other_case, with_dots, new_in_library, new_in_originals, deep_in_library}) {
            const std::vector<std::vector<std::string>> modes{{"rewrite", tiny_path, target},
                                                              {"rewrite", tiny_path, target, "--new-map"},
                                                              {"convert", tiny_path, "--to", "extended", "-o", target},
                                                              {"convert", tiny_path, "--to", "extended", "-o", target, "--rows", "40", "--columns", "40"},
                                                              {"blank", target, "--rows", "5", "--columns", "5"},
                                                              {"blank", target, "--extended", "--rows", "5", "--columns", "5"}};
            for (const std::vector<std::string>& words : modes) {
                const Run r = run_words(words);
                t.check(r.status == 2 && r.out.empty() && has(r.err, "map library") && has(r.err, "nothing written") && untouched(),
                        "the library is refused (" + words[0] + " into " + file_name(target) + "): " + r.err);
            }
        }
        // the library as the INPUT of a rewrite whose OUT is the same file, and as the input of a conversion whose OUT is the same file
        t.check(run_words({"rewrite", community, community}).status == 2 && run_words({"convert", community, "--to", "extended", "-o", community}).status == 2 && untouched(),
                "the library as input and output of one command is refused and stays as it was");
        // reading it is fine
        t.check(run_words({"check", community}).status == 0 && run_words({"info", original}).status == 0 && untouched(), "check and info read the library and change nothing");
        t.check(run_words({"rewrite", community, dir.file("from_library.lvl")}).status == 0 && fs::exists(dir.file("from_library.lvl")) && untouched(),
                "rewrite reads a library map and writes a copy outside the library");
        // a relative path, from a folder beside the library, and the ways up out of it
        const fs::path was = fs::current_path(ec);
        fs::current_path(layout / "work", ec);
        const bool moved = !ec;
        t.check(moved, "the scratch folder can be the current folder");
        if (moved) {
            const Run up = run_words({"rewrite", tiny_path, "../Community-Maps/a map.lvl"});
            const Run up_new = run_words({"convert", tiny_path, "--to", "extended", "-o", "./../Community-Maps/./up.lvl"});
            const Run near = run_words({"blank", "../Original-Ants/Maps/TINY.LVL", "--rows", "5", "--columns", "5"});
            const Run self_relative = run_words({"rewrite", "../../tiny_out.lvl", "../../tiny_out.lvl"});
            fs::current_path(was, ec);
            t.check(up.status == 2 && up_new.status == 2 && near.status == 2 && has(up.err, "map library") && untouched(), "the library by a relative path with .. is refused: " + up.err + up_new.err + near.err);
            t.check(self_relative.status == 2 && has(self_relative.err, "never writes over its own input"), "the same file by a relative path is its own input: " + self_relative.err);
        }
        // symbolic links to the folder and to a file of it, and a hard link to a file of it (the other name of a library map)
        std::error_code link_error;
        fs::create_directory_symlink(layout / "Community-Maps", dir.path / "link_to_folder", link_error);
        const bool folder_linked = !link_error;
        fs::create_symlink(community, dir.path / "link_to_file.lvl", link_error);
        const bool file_linked = !link_error;
        if (folder_linked) {
            const Run r = run_words({"convert", tiny_path, "--to", "extended", "-o", (dir.path / "link_to_folder" / "a map.lvl").string()});
            const Run n = run_words({"blank", (dir.path / "link_to_folder" / "fresh.lvl").string(), "--rows", "5", "--columns", "5"});
            t.check(r.status == 2 && n.status == 2 && has(r.err, "map library") && untouched(), "a link to the library folder is followed: " + r.err + n.err);
        } else {
            std::cout << "    (symbolic links to folders are not allowed here: that part is skipped)\n";
        }
        if (file_linked) {
            const Run r = run_words({"rewrite", tiny_path, (dir.path / "link_to_file.lvl").string()});
            t.check(r.status == 2 && has(r.err, "map library") && untouched(), "a link to a library map is followed: " + r.err);
        } else {
            std::cout << "    (symbolic links to files are not allowed here: that part is skipped)\n";
        }
        fs::create_symlink(layout / "Community-Maps" / "not there yet.lvl", dir.path / "dangling.lvl", link_error);      // (a link to a library file that does not exist yet: a plain write would create it through the link)
        if (!link_error) {
            const Run r = run_words({"blank", (dir.path / "dangling.lvl").string(), "--rows", "5", "--columns", "5"});
            const Run c = run_words({"convert", tiny_path, "--to", "extended", "-o", (dir.path / "dangling.lvl").string()});
            t.check(r.status == 2 && c.status == 2 && has(r.err, "map library") && !fs::exists(layout / "Community-Maps" / "not there yet.lvl") && untouched(), "a link to a library file that is not there yet is followed: " + r.err + c.err);
            fs::remove(dir.path / "dangling.lvl", link_error);
        }
        fs::create_hard_link(community, dir.path / "hard_name.lvl", link_error);
        if (!link_error) {
            const Run r = run_words({"rewrite", tiny_path, (dir.path / "hard_name.lvl").string()});
            const Run c = run_words({"convert", tiny_path, "--to", "extended", "-o", (dir.path / "hard_name.lvl").string()});
            t.check(r.status == 2 && c.status == 2 && has(r.err, "more than one name") && untouched(), "a second name of a library map is refused: " + r.err);
            fs::remove(dir.path / "hard_name.lvl", link_error);
        } else {
            std::cout << "    (hard links are not allowed here: that part is skipped)\n";
        }
        // the folder is found by what it is, not by its name: a library under another name, an input under another spelling
        const fs::path moved_library = dir.path / "stash";
        fs::create_directories(moved_library / "deeper", ec);
        const std::string stash_error = library_guard::write_refusal((moved_library / "deeper" / "x.lvl").string(), {}, {moved_library});
        t.check(!stash_error.empty() && has(stash_error, "map library"), "a folder that is the library by identity (whatever its name) is refused: " + stash_error);
        t.check(library_guard::write_refusal((dir.path / "elsewhere.lvl").string(), {}, {moved_library}).empty(), "a path outside it is not");
        t.check(library_guard::write_refusal((dir.path / "Community-Maps-old" / "x.lvl").string()).empty() && library_guard::write_refusal((dir.path / "My Original-Ants copy" / "x.lvl").string()).empty(),
                "a folder whose name only contains the words is not the library");
        // the real checkout is only ASKED, never written to: its two library folders are known, and a path in them is refused
        const std::vector<fs::path> real = library_guard::checkout_library();
        t.check(real.size() == 2u && fs::is_directory(real[0], ec) && fs::is_directory(real[1], ec) && fs::exists(real[0] / "Maps" / "TINY.LVL", ec) && fs::exists(real[1] / "maps.json", ec),
                "the checkout's library folders are found (Original-Ants, Community-Maps)");
        t.check(!library_guard::write_refusal((real[0] / "Maps" / "NEVER.LVL").string()).empty() && !library_guard::write_refusal((real[1] / "never.lvl").string()).empty() &&
                    !library_guard::write_refusal((real[1] / "previews" / "never.webp").string()).empty(),
                "a new file in the checkout's own library is refused (asked, not tried)");
        // what stays allowed: a file in an ordinary folder, even over another file that is not the input
        const std::string plain = dir.file("plain.lvl");
        t.check(run_words({"blank", plain, "--rows", "5", "--columns", "5"}).status == 0 && run_words({"convert", tiny_path, "--to", "extended", "-o", plain}).status == 0,
                "an ordinary file is written, and over another file that is not the input");
        t.check(untouched(), "the copy of the library is as it was at the end");
    }

    std::cout << "lvl_tool selftest: " << t.checks << " checks, " << t.failures << " failures\n";
    return t.failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    return guarded(
        [argc, argv] {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
            if (args.size() == 1 && args[0] == "--selftest") return selftest();
            return run_tool(args, std::cout, std::cerr);
        },
        std::cerr);
}

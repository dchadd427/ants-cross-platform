// replay_tool: reads a replay file (docs/REPLAYS.md), checks that it plays out the same here, and lists every order of the match with its time, seat, place and ant type.
//
//   replay_tool info FILE                              what the file says about the match (no map needed)
//   replay_tool verify FILE [--maps-dir DIR]           plays it on the engine and compares every hash it holds and the final state
//   replay_tool orders FILE [--maps-dir DIR] [--seat N] [--type ANT] [--csv]
//                                                      plays it and lists every order: time, turn, seat, order, place, the ants by type, where the first stood, what the engine said
//       --seat N     only the orders of seat N (0 green, 1 red, 2 blue, 3 black)
//       --type ANT   only the orders that name an ant of that type (worker, bomber, fire, thief, combat, swimmer)
//       --csv        one comma separated line for each order (every text quoted) instead of the table
//   --maps-dir DIR   where the maps are looked for by the name that the file holds (default: the shipped maps of this checkout)
//   replay_tool --selftest                             checks the tool itself (the command line, the three commands and their exit codes, damaged files) on a match that it makes
//
// Exit status: 0 the file was read (and, for verify and orders, played to the end with every hash right); 1 the file is refused, its map is not found, or it does not play out the same;
// 2 the command line is wrong or the file cannot be read.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "ants_net/netgame.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::replay;

namespace {

namespace fs = std::filesystem;

void usage(std::ostream& out) {
    out << "usage: replay_tool info FILE\n"
           "       replay_tool verify FILE [--maps-dir DIR]\n"
           "       replay_tool orders FILE [--maps-dir DIR] [--seat N] [--type ANT] [--csv]\n"
           "       replay_tool --selftest\n"
           "  info    what the file says about the match (no map needed)\n"
           "  verify  plays the match on the engine and compares every hash that the file holds and the final state\n"
           "  orders  plays it and lists every order: time, turn, seat, order, place, the ants by type, where the first stood, what the engine said\n"
           "  --seat N      only the orders of seat N (0 green, 1 red, 2 blue, 3 black)\n"
           "  --type ANT    only the orders that name an ant of that type (worker, bomber, fire, thief, combat, swimmer)\n"
           "  --csv         one comma separated line for each order (every text quoted)\n"
           "  --maps-dir D  where the maps are looked for (default: the shipped maps of this checkout)\n"
           "  --selftest    check the tool itself\n";
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string hex64(uint64_t v) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << v;
    return out.str();
}

/// The bytes of a file; false (and a reason) when it cannot be read or is longer than a replay can be
bool read_file(const std::string& path, std::vector<uint8_t>& bytes, std::string& why) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        why = "cannot open " + path;
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > kMaxFileBytes) {
        why = path + " is larger than a replay file can be (" + std::to_string(kMaxFileBytes / 1024) + " KiB)";
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    in.seekg(0);
    if (size > 0) in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) {
        why = "cannot read " + path;
        return false;
    }
    return true;
}

std::string seats_text(const Header& h) {
    std::string text;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (((h.roster >> seat) & 1u) == 0) continue;
        if (!text.empty()) text += ", ";
        text += seat_label(h, seat);
    }
    return text;
}

std::string teams_text(const Header& h) {
    return h.teams.set ? seat_label(h, h.teams.a) + " + " + seat_label(h, h.teams.b) : "free for all";
}

std::string with_commas(uint64_t n) {
    std::string digits = std::to_string(n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

void print_info(std::ostream& out, const Replay& r, size_t file_bytes, const std::string& path) {
    const Header& h = r.head;
    out << "File:      " << path << " (" << with_commas(file_bytes) << " bytes)\n";
    out << "Format:    " << h.format_version << ", rules: network protocol " << h.engine_rules << (h.engine_rules == net::kProtocolVersion ? " (the one this build plays)" : " (this build plays protocol " + std::to_string(net::kProtocolVersion) + ")") << "\n";
    out << "Made by:   " << (h.game_version.empty() ? "unknown" : h.game_version) << (h.build_id.empty() ? "" : " (build " + h.build_id + ")") << "\n";
    if (!h.venue.empty()) out << "Played as: " << h.venue << (h.recorder_seat < sim::MAX_PLAYERS ? ", recorded at " + seat_label(h, h.recorder_seat) : std::string()) << "\n";
    out << "Map:       " << h.map_name << " (hash " << hex64(h.map_hash) << "), seed " << h.seed << ", Fog of War " << (h.fog ? "on" : "off") << "\n";
    out << "Seats:     " << seats_text(h) << "\n";
    out << "Teams:     " << teams_text(h) << "\n";
    out << "Length:    " << r.total_turns << " turns (" << format_time(r.total_turns) << ")" << (r.complete ? "" : ", INCOMPLETE: the recording stops here") << "\n";
    out << "Commands:  " << with_commas(r.commands.size()) << "\n";
    out << "Hashes:    " << r.hashes.size() << " (one every " << h.hash_period << " turns)\n";
    if (r.complete) out << "End:       " << (r.match_over ? "the rules ended the match" : "the match was not over") << ", final state hash " << hex64(r.final_hash) << "\n";
}

struct Options {
    std::string command;
    std::string file;
    std::string maps_dir;
    int seat{-1};
    bool has_type{false};
    sim::AntType type{sim::AntType::Worker};
    bool csv{false};
};

bool parse_type(const std::string& text, sim::AntType& out) {
    const std::string t = lower(text);
    for (const sim::AntType candidate : {sim::AntType::Worker, sim::AntType::Bomber, sim::AntType::Fire, sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer}) {
        if (lower(ant_type_name(candidate)) == t) {
            out = candidate;
            return true;
        }
    }
    return false;
}

/// The words after the program's name
bool parse_args(const std::vector<std::string>& args, Options& o) {
    if (args.size() < 2) return false;
    o.command = args[0];
    if (o.command != "info" && o.command != "verify" && o.command != "orders") return false;
    o.file = args[1];
    for (size_t i = 2; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i];
            return true;
        };
        std::string text;
        if (arg == "--maps-dir" && o.command != "info") {
            if (!value(o.maps_dir)) return false;
        } else if (arg == "--seat" && o.command == "orders") {
            if (!value(text) || text.size() != 1 || text[0] < '0' || text[0] > '3') return false;
            o.seat = text[0] - '0';
        } else if (arg == "--type" && o.command == "orders") {
            if (!value(text) || !parse_type(text, o.type)) return false;
            o.has_type = true;
        } else if (arg == "--csv" && o.command == "orders") {
            o.csv = true;
        } else {
            return false;
        }
    }
    return true;
}

/// A text cell of the table, in quotes. A spreadsheet takes a cell that begins with = + - or @ (or a tab or a carriage return) for a formula, and a name comes from a file that may be someone else's:
/// a ' in front of it makes it the text that it is.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    if (!text.empty() && std::string("=+-@\t\r").find(text[0]) != std::string::npos) out += '\'';
    for (const char ch : text) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + "\"";
}

bool wanted(const Options& o, const OrderRecord& r) {
    if (o.seat >= 0 && r.command.issuer != o.seat) return false;
    if (o.has_type && std::none_of(r.ants.begin(), r.ants.end(), [&](const OrderRecord::AntCount& a) { return a.type == o.type; })) return false;
    return true;
}

void print_orders(std::ostream& out, const Options& o, const Replay& replay, const std::vector<OrderRecord>& orders) {
    const Header& h = replay.head;
    std::vector<const OrderRecord*> shown;
    for (const OrderRecord& r : orders) {
        if (wanted(o, r)) shown.push_back(&r);
    }
    if (o.csv) {
        out << "time,turn,seat,name,order,x,y,ants,from_x,from_y,result\n";
        for (const OrderRecord* r : shown) {
            const sim::Command& c = r->command;
            const bool group = sim::is_group_order(c.type);
            out << format_time(r->turn) << "," << r->turn << "," << quoted(seat_label(h, c.issuer)) << "," << quoted(c.issuer < sim::MAX_PLAYERS ? h.names[c.issuer] : std::string()) << ","
                << quoted(order_name(c.type)) << "," << (group ? std::to_string(c.tile_x) : std::string()) << "," << (group ? std::to_string(c.tile_y) : std::string()) << ","
                << quoted(ants_text(*r)) << "," << (r->has_from ? std::to_string(r->from_x) : std::string()) << "," << (r->has_from ? std::to_string(r->from_y) : std::string()) << ","
                << quoted(status_name(r->status)) << "\n";
        }
        return;
    }
    out << h.map_name << ", seed " << h.seed << ": " << seats_text(h) << "; " << replay.total_turns << " turns (" << format_time(replay.total_turns) << ")\n";
    const std::vector<std::string> titles = {"time", "turn", "seat", "order", "place", "ants", "from", "result"};
    std::vector<std::vector<std::string>> rows;
    for (const OrderRecord* r : shown) {
        rows.push_back({format_time(r->turn), std::to_string(r->turn), seat_label(h, r->command.issuer), order_name(r->command.type), place_text(*r, h), ants_text(*r),
                        r->has_from ? "(" + std::to_string(r->from_x) + "," + std::to_string(r->from_y) + ")" : std::string(), status_name(r->status)});
    }
    std::vector<size_t> widths(titles.size());
    for (size_t i = 0; i < titles.size(); ++i) widths[i] = titles[i].size();
    for (const std::vector<std::string>& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) widths[i] = std::max(widths[i], row[i].size());
    }
    const auto print_row = [&](const std::vector<std::string>& row) {
        std::string line;
        for (size_t i = 0; i < row.size(); ++i) {
            const bool right = i == 0 || i == 1;                                     // (the time and the turn read better against their right edge)
            const std::string pad(widths[i] - row[i].size(), ' ');
            line += (right ? pad + row[i] : row[i] + pad) + (i + 1 < row.size() ? "  " : "");
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out << line << "\n";
    };
    print_row(titles);
    for (const std::vector<std::string>& row : rows) print_row(row);

    // the totals: who gave how many orders, and which ants the special orders went to (a bomber's bomb, a swimmer's bridge ...)
    std::map<uint8_t, size_t> by_seat;
    std::map<std::string, size_t> special_by_type;
    for (const OrderRecord* r : shown) {
        ++by_seat[r->command.issuer];
        if (r->command.type == sim::CommandType::GroupSpecial) {
            for (const OrderRecord::AntCount& a : r->ants) ++special_by_type[ant_type_name(a.type)];
        }
    }
    out << "\n" << shown.size() << " orders" << (shown.size() == orders.size() ? "" : " of " + std::to_string(orders.size()));
    const char* separator = ": ";
    for (const auto& entry : by_seat) {
        out << separator << seat_label(h, entry.first) << " " << entry.second;
        separator = ", ";
    }
    out << "\n";
    if (!special_by_type.empty()) {
        separator = "special orders by ant type: ";
        for (const auto& entry : special_by_type) {
            out << separator << entry.first << " " << entry.second;
            separator = ", ";
        }
        out << "\n";
    }
}

/// The tool: `args` are the words after its name; the output goes to `out`, the complaints to `err`; the result is the exit status
int run_tool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        usage(out);
        return 0;
    }
    Options o;
    if (!parse_args(args, o)) {
        usage(err);
        return 2;
    }
    std::vector<uint8_t> bytes;
    std::string why;
    if (!read_file(o.file, bytes, why)) {
        err << "replay_tool: " << why << "\n";
        return 2;
    }
    Replay replay;
    if (!decode(bytes.data(), bytes.size(), replay, why)) {
        err << "replay_tool: " << o.file << " is refused: " << why << "\n";
        return 1;
    }
    if (o.command == "info") {
        print_info(out, replay, bytes.size(), o.file);
        return 0;
    }
    const std::string maps_dir = !o.maps_dir.empty() ? o.maps_dir : std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
    assets::LevelData level;
    if (!load_map(replay.head, maps_dir, level, why)) {
        err << "replay_tool: " << why << "\n";
        return 1;
    }
    Outcome outcome;
    if (o.command == "orders") {
        const std::vector<OrderRecord> orders = list_orders(replay, level, outcome);
        print_orders(out, o, replay, orders);
    } else {
        outcome = play(replay, level);
    }
    if (!outcome.ok) {
        err << "replay_tool: " << (outcome.error.empty() ? "the replay did not play out" : outcome.error) << "\n";
        return 1;
    }
    if (o.command == "verify") {
        if (!replay.complete && outcome.hashes_checked == 0) {                 // (nothing was compared with anything: that is not "OK")
            err << "replay_tool: UNVERIFIED: " << outcome.turns << " turns (" << format_time(outcome.turns) << ") were played, but the file is INCOMPLETE and holds no hash to compare them with\n";
            return 1;
        }
        out << "OK: " << outcome.turns << " turns (" << format_time(outcome.turns) << "), " << outcome.hashes_checked << " hashes" << (replay.complete ? " and the final state" : "") << " match"
            << (replay.complete ? "" : "; the file is INCOMPLETE: the recording stops here") << "\n";
        out << (outcome.match_over ? "The match was over" : "The match was not over") << "; scores:";
        const char* separator = " ";
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            if (((replay.head.roster >> seat) & 1u) == 0) continue;
            out << separator << seat_label(replay.head, seat) << " " << outcome.scores[seat];
            separator = ", ";
        }
        out << "\n";
    } else if (!replay.complete && !o.csv) {
        err << "replay_tool: the file is INCOMPLETE: the recording stops here (" << format_time(outcome.turns) << ")\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// --selftest: the tool on a match that it makes itself (the shipped map TINY, four seats, a few hundred orders), written to a scratch folder and read back through the command line
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
        path = fs::temp_directory_path(ec) / ("replay_tool_selftest_" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
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

size_t lines_of(const std::string& text) { return static_cast<size_t>(std::count(text.begin(), text.end(), '\n')); }

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}

/// The fields of one comma separated line, the quoted ones with their quotes taken off ("" is a quote)
std::vector<std::string> csv_fields(const std::string& line) {
    std::vector<std::string> fields(1);
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (in_quotes) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                fields.back() += '"';
                ++i;
            } else if (ch == '"') {
                in_quotes = false;
            } else {
                fields.back() += ch;
            }
        } else if (ch == '"') {
            in_quotes = true;
        } else if (ch == ',') {
            fields.emplace_back();
        } else {
            fields.back() += ch;
        }
    }
    return fields;
}

/// A match on TINY, recorded as a game on one machine records it: every 40 turns each seat sends two of its ants to a tile, the first seat asks the second for a team, and the third gives a
/// special order to a worker (a worker has none to carry out: the list says what the engine said)
bool make_sample(const assets::LevelData& level, uint64_t map_hash, uint32_t turns, std::vector<uint8_t>& bytes, std::string& why) {
    sim::SimulationEngine engine;
    engine.init(level, 7, 0x0F);
    Header head;
    head.game_version = "v0.0.0";
    head.build_id = "selftest";
    head.venue = "local game";
    head.map_name = "TINY.LVL";
    head.map_hash = map_hash;
    head.seed = 7;
    head.roster = 0x0F;
    head.names[1] = "Bot (Hard)";
    Recorder recorder(head);
    for (uint32_t turn = 0; turn < turns; ++turn) {
        std::vector<sim::Command> commands;
        const auto ants_of = [&](uint8_t seat, size_t n) {
            std::vector<uint32_t> ids;
            for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
                if (a.player_id == seat && a.hp > 0 && ids.size() < n) ids.push_back(a.id);
            }
            return ids;
        };
        const auto give = [&](sim::CommandType type, uint8_t issuer, int16_t x, int16_t y, std::vector<uint32_t> ants, uint8_t other = 255) {
            sim::Command c;
            c.type = type;
            c.issuer = issuer;
            c.other_player = other;
            c.tile_x = x;
            c.tile_y = y;
            c.ants = std::move(ants);
            commands.push_back(std::move(c));
        };
        if (turn % 40 == 5) {
            for (uint8_t seat = 0; seat < 4; ++seat) {
                const std::vector<uint32_t> ids = ants_of(seat, 2);
                if (!ids.empty()) give(sim::CommandType::GroupMove, seat, static_cast<int16_t>(10 + turn / 8 % 20 + seat), static_cast<int16_t>(10 + turn / 16 % 20), ids);
            }
        }
        if (turn == 50) give(sim::CommandType::AllianceInvite, 0, 0, 0, {}, 1);
        if (turn == 60) {
            const std::vector<uint32_t> ids = ants_of(2, 1);
            if (!ids.empty()) give(sim::CommandType::GroupSpecial, 2, 14, 14, ids);
        }
        sim::canonical_order(commands);
        for (const sim::Command& c : commands) {
            engine.apply_command(c);
            recorder.on_command(c);
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        recorder.on_tick(engine);
    }
    bytes = recorder.finish(engine, why);
    return !bytes.empty();
}

/// The start of the last chunk of a file that the writer made
size_t last_chunk_start(const std::vector<uint8_t>& bytes) {
    size_t pos = 8;
    size_t last = 8;
    while (pos + 12 <= bytes.size()) {
        last = pos;
        const size_t length = static_cast<size_t>(bytes[pos + 4]) | (static_cast<size_t>(bytes[pos + 5]) << 8) | (static_cast<size_t>(bytes[pos + 6]) << 16) | (static_cast<size_t>(bytes[pos + 7]) << 24);
        pos += 12 + length;
    }
    return last;
}

int selftest() {
    SelfTest t;
    TempDir dir;
    t.check(dir.ok(), "a scratch folder");
    const std::string maps = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
    assets::LevelData level;
    uint64_t map_hash = 0;
    const std::string tiny = maps + "/TINY.LVL";
    if (!dir.ok() || !level.load_from_file(tiny) || !net::hash_file(tiny, map_hash)) {
        std::cerr << "replay_tool --selftest: the shipped map TINY or a scratch folder is missing\n";
        return 1;
    }
    std::vector<uint8_t> bytes;
    std::string why;
    constexpr uint32_t kTurns = 450;
    t.check(make_sample(level, map_hash, kTurns, bytes, why), "a match is recorded: " + why);
    Replay sample;
    t.check(!bytes.empty() && decode(bytes.data(), bytes.size(), sample, why) && sample.complete && sample.total_turns == kTurns && sample.commands.size() > 20, "and the file reads back whole with its orders");
    const std::string file = dir.file("sample.antsrep");
    t.check(write_bytes(file, bytes), "the file is written");
    const size_t commands = sample.commands.size();
    const std::string count = std::to_string(commands);
    size_t red = 0;
    for (const TimedCommand& c : sample.commands) red += c.command.issuer == 1 ? size_t{1} : size_t{0};

    // the command line
    {
        const Run help = run_words({"--help"});
        t.check(help.status == 0 && has(help.out, "usage: replay_tool") && help.err.empty(), "--help prints the usage and succeeds");
        for (const std::vector<std::string>& bad : std::vector<std::vector<std::string>>{{},
                                                                                          {"info"},
                                                                                          {"nonsense", file},
                                                                                          {"orders", file, "--seat", "4"},
                                                                                          {"orders", file, "--seat", "12"},
                                                                                          {"orders", file, "--seat"},
                                                                                          {"orders", file, "--type", "frog"},
                                                                                          {"orders", file, "--maps-dir"},
                                                                                          {"orders", file, "--sideways"},
                                                                                          {"info", file, "--csv"},
                                                                                          {"info", file, "--maps-dir", maps},
                                                                                          {"verify", file, "--seat", "1"},
                                                                                          {"verify", file, "--csv"}}) {
            const Run r = run_words(bad);
            t.check(r.status == 2 && r.out.empty() && has(r.err, "usage: replay_tool"), "a wrong command line (" + (bad.size() > 1 ? bad[0] + " ... " + bad.back() : std::string("short")) + ") is exit 2 with the usage");
        }
        const Run missing = run_words({"info", dir.file("nowhere.antsrep")});
        t.check(missing.status == 2 && has(missing.err, "cannot open"), "a file that is not there is exit 2 and says so");
    }

    // info
    {
        const Run r = run_words({"info", file});
        t.check(r.status == 0 && r.err.empty(), "info succeeds");
        t.check(has(r.out, "Map:       TINY.LVL (hash " + hex64(map_hash) + "), seed 7, Fog of War off"), "info names the map, its hash and the seed");
        t.check(has(r.out, "Seats:     Green, Red (Bot (Hard)), Blue, Black"), "info lists the seats, with the name that was typed");
        t.check(has(r.out, "Length:    450 turns (0:22.5)") && has(r.out, "Commands:  " + count) && has(r.out, "Hashes:    4 (one every 100 turns)"), "info gives the length, the commands and the hashes");
        t.check(has(r.out, "End:       the match was not over, final state hash " + hex64(sample.final_hash)), "info gives the end");
        t.check(has(r.out, "network protocol " + std::to_string(net::kProtocolVersion) + " (the one this build plays)") && has(r.out, "Made by:   v0.0.0 (build selftest)"), "info says which rules and which game");
    }

    // verify
    {
        const Run r = run_words({"verify", file});
        t.check(r.status == 0 && r.err.empty() && has(r.out, "OK: 450 turns (0:22.5), 4 hashes and the final state match"), "verify plays the match and every hash is right");
        t.check(has(r.out, "scores: Green ") && has(r.out, "Red (Bot (Hard)) ") && has(r.out, "Black "), "verify prints the scores");
        t.check(run_words({"verify", file, "--maps-dir", maps}).status == 0, "--maps-dir with the folder of the shipped maps works");
        fs::create_directories(dir.path / "nomaps");
        const Run none = run_words({"verify", file, "--maps-dir", dir.file("nomaps")});
        t.check(none.status == 1 && has(none.err, "TINY.LVL") && has(none.err, "not in") && none.out.empty(), "without the map it is exit 1 and names the map");
        fs::copy_file(tiny, dir.path / "nomaps" / "tiny.lvl");
        t.check(run_words({"verify", file, "--maps-dir", dir.file("nomaps")}).status == 0, "a map file in another case is found");
    }

    // orders
    {
        const Run all = run_words({"orders", file});
        t.check(all.status == 0 && all.err.empty(), "orders succeeds");
        t.check(has(all.out, "TINY.LVL, seed 7: Green, Red (Bot (Hard)), Blue, Black; 450 turns (0:22.5)\n"), "orders opens with the match");
        t.check(has(all.out, "time  turn  seat") && has(all.out, "order") && has(all.out, "place") && has(all.out, "ants") && has(all.out, "from") && has(all.out, "result"), "orders has the columns");
        t.check(has(all.out, "\n" + count + " orders: Green ") && has(all.out, ", Red (Bot (Hard)) ") && has(all.out, ", Blue ") && has(all.out, ", Black "), "orders counts the orders of each seat");
        t.check(has(all.out, "0:00.2") && has(all.out, "move") && has(all.out, "2 Worker") && has(all.out, "with Red") && has(all.out, "team offer"), "a move of 2 workers and the team offer are in the list with their words");
        t.check(has(all.out, "special") && has(all.out, "1 Worker") && has(all.out, "special orders by ant type: Worker 1"), "the special order is listed, with the type of the ant that got it");
        // every order is one line between the columns' line and the totals
        t.check(lines_of(all.out) == 1 + 1 + commands + 1 + 1 + 1, "one line for each order, the totals' lines and no more");
        const Run seat = run_words({"orders", file, "--seat", "1"});
        t.check(seat.status == 0 && has(seat.out, "\n" + std::to_string(red) + " orders of " + count + ": Red (Bot (Hard)) " + std::to_string(red) + "\n"), "--seat 1 keeps the orders of Red and counts them against all");
        t.check(red > 0 && red < commands && lines_of(seat.out) == 1 + 1 + red + 1 + 1, "and the lines are theirs only");
        const Run workers = run_words({"orders", file, "--type", "worker"});
        t.check(workers.status == 0 && has(workers.out, "\n" + std::to_string(commands - 1) + " orders of " + count + ": ") && has(workers.out, "special orders by ant type: Worker 1"), "--type worker keeps the orders that name a worker (not the team offer)");
        t.check(has(run_words({"orders", file, "--type", "WORKER"}).out, "\n" + std::to_string(commands - 1) + " orders of "), "the ant type is read in any case");
        const Run bombers = run_words({"orders", file, "--type", "bomber"});
        t.check(bombers.status == 0 && has(bombers.out, "\n0 orders of " + count + "\n"), "--type bomber finds none: nobody ordered a bomber");
        const Run both = run_words({"orders", file, "--seat", "1", "--type", "worker"});
        t.check(both.status == 0 && has(both.out, "\n" + std::to_string(red) + " orders of " + count + ": "), "--seat and --type together keep what both ask for");
    }

    // orders as a table for a spreadsheet
    {
        const Run r = run_words({"orders", file, "--csv"});
        t.check(r.status == 0 && r.err.empty() && lines_of(r.out) == 1 + commands, "--csv: a line for each order and the names of the columns");
        std::istringstream lines(r.out);
        std::string line;
        std::getline(lines, line);
        t.check(line == "time,turn,seat,name,order,x,y,ants,from_x,from_y,result", "the columns are named");
        bool fields_ok = true;
        bool saw_move = false;
        bool saw_name = false;
        size_t rows = 0;
        while (std::getline(lines, line)) {
            ++rows;
            const std::vector<std::string> f = csv_fields(line);
            fields_ok = fields_ok && f.size() == 11 && !f[0].empty() && !f[1].empty();
            if (f.size() == 11 && f[4] == "move" && f[7] == "2 Worker" && !f[5].empty() && !f[8].empty()) saw_move = true;
            if (f.size() == 11 && f[2] == "Red (Bot (Hard))" && f[3] == "Bot (Hard)") saw_name = true;
        }
        t.check(rows == commands && fields_ok && saw_move && saw_name, "every line has its 11 fields, a move has its tile, its ants and where they stood, a name is given with its seat");
        const Run seat = run_words({"orders", file, "--seat", "1", "--csv"});
        t.check(seat.status == 0 && lines_of(seat.out) == 1 + red, "--csv with --seat keeps one seat's lines");

        // names that a spreadsheet would take for formulas stay text: every cell of text that begins with = + - or @ gets a ' in front, whoever's file it is
        Replay formulas = sample;
        const std::array<std::string, 4> names = {"=HYPERLINK(\"http://example.com/\",\"x\")", "+1", "-2+3", "@SUM(1+1)"};
        formulas.head.names = names;
        std::string error;
        t.check(write_bytes(dir.file("formulas.antsrep"), encode(formulas, error)), "a file whose names are formulas is written: " + error);
        const Run formula_run = run_words({"orders", dir.file("formulas.antsrep"), "--csv"});
        t.check(formula_run.status == 0 && lines_of(formula_run.out) == 1 + commands, "--csv of it has a line for each order");
        std::istringstream formula_lines(formula_run.out);
        std::getline(formula_lines, line);
        std::array<bool, 4> seat_seen = {false, false, false, false};
        bool text_cells_safe = true;
        bool names_kept = true;
        while (std::getline(formula_lines, line)) {
            const std::vector<std::string> f = csv_fields(line);
            if (f.size() != 11) {
                text_cells_safe = false;
                continue;
            }
            for (const size_t column : {size_t{2}, size_t{3}, size_t{4}, size_t{7}, size_t{10}}) {
                text_cells_safe = text_cells_safe && (f[column].empty() || std::string("=+-@\t\r").find(f[column][0]) == std::string::npos);
            }
            for (size_t seat_index = 0; seat_index < 4; ++seat_index) {
                if (f[2].rfind(std::string(seat_index == 0 ? "Green" : seat_index == 1 ? "Red" : seat_index == 2 ? "Blue" : "Black") + " (", 0) != 0) continue;
                seat_seen[seat_index] = true;
                names_kept = names_kept && f[3] == "'" + names[seat_index];
            }
        }
        t.check(text_cells_safe && seat_seen[0] && seat_seen[1] && seat_seen[2] && seat_seen[3] && names_kept, "no text cell begins with = + - or @, and each name is its own text with the ' in front");
    }

    // files that are not what they seem
    {
        std::vector<uint8_t> flipped = bytes;
        flipped[flipped.size() / 2] ^= 0x01u;
        t.check(write_bytes(dir.file("flipped.antsrep"), flipped), "a damaged copy is written");
        for (const char* command : {"info", "verify", "orders"}) {
            const Run r = run_words({command, dir.file("flipped.antsrep")});
            t.check(r.status == 1 && r.out.empty() && has(r.err, "flipped.antsrep is refused:"), std::string(command) + " refuses a file with one flipped bit, with the reason, exit 1");
        }
        t.check(write_bytes(dir.file("empty.antsrep"), {}) && run_words({"info", dir.file("empty.antsrep")}).status == 1, "an empty file is refused");
        std::vector<uint8_t> words;
        for (const char ch : std::string("this is not a replay, it is a sentence")) words.push_back(static_cast<uint8_t>(ch));
        t.check(write_bytes(dir.file("words.antsrep"), words) && run_words({"verify", dir.file("words.antsrep")}).status == 1, "a text file is refused");

        std::string error;

        // a recording that stopped (a copy that was cut short): the file has no end
        const std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(last_chunk_start(bytes)));
        t.check(write_bytes(dir.file("cut.antsrep"), cut), "a file without its last chunk is written");
        const Run info = run_words({"info", dir.file("cut.antsrep")});
        t.check(info.status == 0 && has(info.out, "INCOMPLETE: the recording stops here") && !has(info.out, "End:"), "info of a file that stops says so, and has no end to give");
        const Run verify = run_words({"verify", dir.file("cut.antsrep")});
        t.check(verify.status == 0 && has(verify.out, "hashes match; the file is INCOMPLETE") && !has(verify.out, "final state"), "verify plays it as far as it goes and says that it stopped");
        const Run orders = run_words({"orders", dir.file("cut.antsrep")});
        t.check(orders.status == 0 && has(orders.err, "the file is INCOMPLETE") && has(orders.out, " orders: "), "orders lists what there is and says on the other stream that it stopped");
        t.check(run_words({"orders", dir.file("cut.antsrep"), "--csv"}).err.empty(), "and keeps its table clean for a spreadsheet");

        // a recording that stopped before its first hash: nothing in it can be compared with anything, so verify does not say OK
        Replay unhashed = sample;
        unhashed.complete = false;
        unhashed.hashes.clear();
        t.check(write_bytes(dir.file("unhashed.antsrep"), encode(unhashed, error)), "a file with no end and no hash is written: " + error);
        const Run unverified = run_words({"verify", dir.file("unhashed.antsrep")});
        t.check(unverified.status == 1 && unverified.out.empty() && has(unverified.err, "UNVERIFIED") && has(unverified.err, "no hash"), "verify of it is exit 1 and says UNVERIFIED, not OK");
        t.check(run_words({"orders", dir.file("unhashed.antsrep")}).status == 0, "orders still lists it");

        // a short match that was finished: no hash in it either, but the final state is compared, so it verifies
        std::vector<uint8_t> brief;
        t.check(make_sample(level, map_hash, 60, brief, why) && write_bytes(dir.file("brief.antsrep"), brief), "a match of 60 turns is recorded and written: " + why);
        const Run brief_run = run_words({"verify", dir.file("brief.antsrep")});
        t.check(brief_run.status == 0 && has(brief_run.out, "OK: 60 turns (0:03.0), 0 hashes and the final state match"), "verify of a finished match with no hash in it compares the final state");

        // a recording of other rules: it reads, it is not played
        Replay other = sample;
        other.head.engine_rules = static_cast<uint16_t>(net::kProtocolVersion + 1);
        t.check(write_bytes(dir.file("other.antsrep"), encode(other, error)), "a file of other rules is written");
        const Run rules_info = run_words({"info", dir.file("other.antsrep")});
        t.check(rules_info.status == 0 && has(rules_info.out, "this build plays protocol " + std::to_string(net::kProtocolVersion)), "info reads it and says which rules it needs");
        const Run rules_verify = run_words({"verify", dir.file("other.antsrep")});
        t.check(rules_verify.status == 1 && has(rules_verify.err, std::to_string(net::kProtocolVersion + 1)) && has(rules_verify.err, std::to_string(net::kProtocolVersion)), "verify does not play it, exit 1, and names both numbers");

        // a recording that is not what was played: the hashes say so
        Replay changed = sample;
        changed.head.seed += 1;
        t.check(write_bytes(dir.file("changed.antsrep"), encode(changed, error)), "a file with another seed is written");
        const Run bad = run_words({"verify", dir.file("changed.antsrep")});
        t.check(bad.status == 1 && bad.out.empty() && !bad.err.empty(), "verify of a match that does not play out the same is exit 1 and says what differs");
        t.check(run_words({"orders", dir.file("changed.antsrep")}).status == 1, "so is orders");
    }

    std::cout << "replay_tool --selftest: " << t.checks << " checks, " << t.failures << " failures\n";
    return t.failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    if (args.size() == 1 && args[0] == "--selftest") return selftest();
    return run_tool(args, std::cout, std::cerr);
}

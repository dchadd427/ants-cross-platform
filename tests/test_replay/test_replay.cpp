// Tests of the replay file (ants_replay/replay.hpp), the recorder (recorder.hpp), the lock-step runner's tap that feeds it (LockstepRunner::set_on_executed) and the player that checks a file
// and lists its orders (player.hpp).
//
// What is checked:
//   - the file: what is written is read back exactly, the same bytes every time, in several chunks when the commands are many; every refusal of the reader (a flipped byte, a cut, a chunk that
//     lies about its length, a count past its limit, an unknown critical chunk, a command that the network would not take, a map name that names a path ...) is a refusal with a reason and never a crash;
//   - the recorder: a game on one machine (commands and ticks) and a lock-step runner (live turns, the catch-up's turns) write the SAME file for the same match; a turn that went missing spoils
//     the recording and nothing is written; commands that the wire form cannot hold are left out and counted;
//   - the player: a recorded match plays out to the same state hash on a fresh engine, a changed command or seed or rule number does not, and the first hash that differs is named; a match of four
//     computer players (the arena) is a real one with hundreds of commands;
//   - the list of orders: every command of the match, with its time, its seat, its target and the type of every ant that it names (read off the engine as the replay plays).
#include "ants_ai/arena.hpp"
#include "ants_net/lockstep.hpp"
#include "ants_net/netgame.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ANTS_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_GETPID getpid
#endif

using namespace ants;
using namespace ants::replay;
using ants::sim::Command;
using ants::sim::CommandType;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace fs = std::filesystem;

namespace {

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    uint32_t below(uint32_t n) {
        s = s * 1664525u + 1013904223u;
        return n == 0 ? 0u : (s >> 8) % n;
    }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("ants_test_replay_" + std::to_string(ANTS_GETPID()) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

struct Map {
    assets::LevelData level;
    std::string path;
    uint64_t hash{0};
};

bool load_shipped(const std::string& name, Map& m) {
    m.path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL";
    return m.level.load_from_file(m.path) && net::hash_file(m.path, m.hash);
}

Header head_for(const Map& m, const std::string& name, uint32_t seed, uint8_t roster) {
    Header h;
    h.game_version = "v0.0.0";
    h.build_id = "test";
    h.venue = "local game";
    h.map_name = name + ".LVL";
    h.map_hash = m.hash;
    h.seed = seed;
    h.roster = roster;
    return h;
}

Command make_command(CommandType type, uint8_t issuer, int16_t x, int16_t y, std::vector<uint32_t> ants, uint8_t other = 255) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.other_player = other;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

bool same_head(const Header& a, const Header& b) {
    return a.format_version == b.format_version && a.engine_rules == b.engine_rules && a.sim_rules == b.sim_rules && a.game_version == b.game_version && a.build_id == b.build_id && a.venue == b.venue &&
           a.map_name == b.map_name && a.map_hash == b.map_hash && a.seed == b.seed && a.roster == b.roster && a.fog == b.fog && a.mode == b.mode && a.names == b.names && a.teams == b.teams &&
           a.recorder_seat == b.recorder_seat && a.hash_period == b.hash_period;
}

bool same(const Replay& a, const Replay& b) {
    if (!same_head(a.head, b.head) || a.commands.size() != b.commands.size() || a.hashes != b.hashes || a.complete != b.complete || a.total_turns != b.total_turns ||
        a.match_over != b.match_over || a.final_hash != b.final_hash) {
        return false;
    }
    for (size_t i = 0; i < a.commands.size(); ++i) {
        if (a.commands[i].turn != b.commands[i].turn || a.commands[i].command != b.commands[i].command) return false;
    }
    return true;
}

/// A replay that holds one of every kind of thing: every command type, same-turn commands, gaps that need one, two and three bytes, names, teams, hashes, the match's end
Replay synthetic() {
    Replay r;
    r.head.game_version = "v1.2.3";
    r.head.build_id = "abc1234";
    r.head.venue = "network game";
    r.head.map_name = "TINY.LVL";
    r.head.map_hash = 0x0123456789ABCDEFull;
    r.head.seed = 4242;
    r.head.roster = 0x0F;
    r.head.fog = true;
    r.head.names = {"Dave", "Hard Bot", "", "Sam O'Neil ~ [2]"};
    r.head.teams = sim::StartTeams{true, 0, 2};
    r.head.recorder_seat = 1;
    const auto add = [&](uint32_t turn, Command c) { r.commands.push_back(TimedCommand{turn, std::move(c)}); };
    add(0, make_command(CommandType::AllianceInvite, 0, 0, 0, {}, 1));
    add(0, make_command(CommandType::AllianceAccept, 1, 0, 0, {}, 0));
    add(5, make_command(CommandType::GroupMove, 0, 3, 4, {10, 11, 12}));
    add(5, make_command(CommandType::GroupSpecial, 1, -1, 7, {70000}));
    add(130, make_command(CommandType::GroupAttack, 2, 20, 21, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}));
    add(130, make_command(CommandType::Stop, 2, 0, 0, {9}));
    add(131, make_command(CommandType::Hatch, 3, 0, 0, {}));
    add(20000, make_command(CommandType::AllianceDeny, 3, 0, 0, {}, 2));
    add(20001, make_command(CommandType::AllianceWithdraw, 0, 0, 0, {}, 1));
    add(20001, make_command(CommandType::AllianceBreak, 1, 0, 0, {}));
    add(2000000, make_command(CommandType::Drop, 2, 0, 0, {}));
    add(2000001, make_command(CommandType::Quit, 0, 0, 0, {}));
    r.hashes = {0xDEADBEEFu, 1u, 0xFFFFFFFFu};
    r.complete = true;
    r.total_turns = 2000001;
    r.match_over = true;
    r.final_hash = 0xFEDCBA9876543210ull;
    return r;
}

/// The same without the long gaps: every command is within 30 s, so the commands are ONE chunk (the tests that edit a chunk's bytes need to know which one it is)
Replay synthetic_small() {
    Replay r = synthetic();
    const uint32_t turns[] = {0, 0, 5, 5, 130, 130, 131, 200, 201, 201, 300, 301};
    for (size_t i = 0; i < r.commands.size(); ++i) r.commands[i].turn = turns[i];
    r.total_turns = 301;
    return r;
}

std::vector<uint8_t> encoded(const Replay& r) {
    std::string error;
    std::vector<uint8_t> bytes = encode(r, error);
    if (bytes.empty()) std::cout << "\n    encode refused: " << error << "\n";
    return bytes;
}

struct Chunk {
    std::string tag;
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> magic_bytes() { return {0x89, 'A', 'R', 'P', 'L', 0x0D, 0x0A, 0x1A}; }

/// The chunks of a file that was written by encode()
std::vector<Chunk> chunks_of(const std::vector<uint8_t>& bytes) {
    std::vector<Chunk> out;
    size_t pos = 8;
    while (pos + 12 <= bytes.size()) {
        const uint32_t length = static_cast<uint32_t>(bytes[pos + 4]) | (static_cast<uint32_t>(bytes[pos + 5]) << 8) | (static_cast<uint32_t>(bytes[pos + 6]) << 16) | (static_cast<uint32_t>(bytes[pos + 7]) << 24);
        Chunk c;
        c.tag.assign(reinterpret_cast<const char*>(&bytes[pos]), 4);
        c.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos + 8), bytes.begin() + static_cast<std::ptrdiff_t>(pos + 8 + length));
        out.push_back(std::move(c));
        pos += 12 + length;
    }
    return out;
}

/// A file made of chunks with their lengths and checksums right, whatever the chunks hold
std::vector<uint8_t> file_of(const std::vector<Chunk>& chunks) {
    std::vector<uint8_t> out = magic_bytes();
    for (const Chunk& c : chunks) {
        const uint8_t* tag = reinterpret_cast<const uint8_t*>(c.tag.data());
        out.insert(out.end(), tag, tag + 4);
        const uint32_t length = static_cast<uint32_t>(c.payload.size());
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>((length >> shift) & 0xFFu));
        out.insert(out.end(), c.payload.begin(), c.payload.end());
        const uint32_t crc = crc32(c.payload.data(), c.payload.size(), crc32(tag, 4));
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>((crc >> shift) & 0xFFu));
    }
    return out;
}

struct Decoded {
    bool ok{false};
    Replay replay;
    std::string error;
};

Decoded decoded(const std::vector<uint8_t>& bytes) {
    Decoded d;
    d.ok = decode(bytes.data(), bytes.size(), d.replay, d.error);
    return d;
}

size_t count_chunks(const std::vector<uint8_t>& bytes, const std::string& tag) {
    size_t n = 0;
    for (const Chunk& c : chunks_of(bytes)) n += c.tag == tag ? size_t{1} : size_t{0};
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// A match played on the engine, with a script of orders, recorded as a game on one machine records
// ---------------------------------------------------------------------------------------------------------------------------------

struct Played {
    std::vector<uint8_t> file;
    Replay replay;
    uint64_t hash{0};
    bool over{false};
    std::vector<std::vector<Command>> turns;      // the commands of every turn, as they were applied (for the runner's side)
};

/// The orders of the script, by turn: they are made from what the world holds, so they are the same for the same seed
std::vector<Command> orders_at(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<Command> out;
    const sim::WorldState& world = engine.get_world_state();
    const auto ants_of = [&](uint8_t seat, size_t n) {
        std::vector<uint32_t> ids;
        for (const sim::AntSnapshot& a : world.ants) {
            if (a.player_id == seat && a.hp > 0 && ids.size() < n) ids.push_back(a.id);
        }
        return ids;
    };
    if (turn % 40 == 5) {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            const std::vector<uint32_t> ids = ants_of(seat, 3);
            if (ids.empty()) continue;
            const int16_t x = static_cast<int16_t>(10 + turn / 8 % 20 + seat);
            const int16_t y = static_cast<int16_t>(10 + turn / 16 % 20);
            out.push_back(make_command(turn % 80 == 5 ? CommandType::GroupMove : CommandType::GroupAttack, seat, x, y, ids));
        }
    }
    if (turn == 10) {
        const std::vector<uint32_t> green = ants_of(0, 2);
        if (!green.empty()) out.push_back(make_command(CommandType::GroupMove, 1, 12, 12, {green[0]}));      // a seat that orders an ant that is not its own: the engine leaves it out
        out.push_back(make_command(CommandType::GroupMove, 0, 12, 12, {4000000}));                           // an ant that never was
    }
    if (turn == 60) out.push_back(make_command(CommandType::AllianceInvite, 0, 0, 0, {}, 1));
    if (turn == 61) out.push_back(make_command(CommandType::AllianceAccept, 1, 0, 0, {}, 0));
    if (turn == 100) out.push_back(make_command(CommandType::Hatch, 2, 0, 0, {}));
    if (turn == 150) out.push_back(make_command(CommandType::Stop, 0, 0, 0, ants_of(0, 2)));
    if (turn == 200) out.push_back(make_command(CommandType::AllianceBreak, 1, 0, 0, {}));
    return out;
}

/// Asks the recorder for a snapshot after every `every` turns (and notes the engine's state hash then): what the server does for a match that still runs
struct SnapshotTap {
    uint32_t every{0};
    std::vector<std::vector<uint8_t>> files;
    std::vector<uint32_t> turns;
    std::vector<uint64_t> hashes;
};

Played play_scripted(const Map& m, uint32_t seed, uint32_t turns, uint8_t roster = 0x0F, const sim::StartTeams& teams = sim::StartTeams{}, bool fog = false, SnapshotTap* tap = nullptr, uint8_t mode = 0) {
    Played p;
    sim::SimulationEngine engine;
    engine.set_fog_of_war_enabled(fog);
    engine.set_game_mode(static_cast<sim::GameMode>(mode));                  // (before init, as every machine of a match does)
    engine.init(m.level, seed, roster);
    if (teams.set) sim::apply_start_teams(engine, teams);                    // (as a match begins: after init, before the first tick)
    Header head = head_for(m, "TINY", seed, roster);
    head.teams = teams;
    head.fog = fog;
    head.set_mode(mode);
    Recorder rec(head);
    for (uint32_t turn = 0; turn < turns; ++turn) {
        std::vector<Command> commands = orders_at(engine, turn);
        sim::canonical_order(commands);                                      // (a turn of a lock-step match is in canonical order: the same file whichever way it is fed)
        for (const Command& c : commands) {
            engine.apply_command(c);
            rec.on_command(c);
        }
        p.turns.push_back(commands);
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
        if (tap != nullptr && tap->every != 0 && (turn + 1) % tap->every == 0) {
            std::string why;
            tap->files.push_back(rec.snapshot(why));
            tap->turns.push_back(turn + 1);
            tap->hashes.push_back(engine.state_hash().total);
        }
    }
    p.hash = engine.state_hash().total;
    p.over = engine.is_match_over();
    std::string error;
    p.file = rec.finish(engine, error);
    if (p.file.empty()) std::cout << "\n    finish: " << error << "\n";
    Decoded d = decoded(p.file);
    if (d.ok) p.replay = std::move(d.replay);
    return p;
}

/// A match of four standard computer players on TINY (the arena's own), recorded as a game on one machine records: the commands of step k are given after k ticks
struct ArenaRecording {
    ai::ArenaResult match;
    std::vector<uint8_t> file;
    uint64_t hash{0};                                                        // the recording engine's state hash at the end (the arena's own is match.hash)
    std::string error;
};

ArenaRecording record_arena_match(const Map& m, uint32_t seed, uint64_t max_ticks) {
    ArenaRecording out;
    ai::ArenaSpec spec;
    spec.level = &m.level;
    spec.seed = seed;
    spec.max_ticks = max_ticks;
    spec.record = true;
    for (uint8_t seat = 0; seat < 4; ++seat) spec.bots.push_back(ai::BotSpec{seat, "standard", ai::Level::Hard, ai::Style::Random});
    out.match = ai::play_match(spec);
    if (!out.match.error.empty()) {
        out.error = out.match.error;
        return out;
    }
    sim::SimulationEngine engine;
    engine.init(m.level, seed, 0x0F);
    Recorder rec(head_for(m, "TINY", seed, 0x0F));
    size_t next = 0;
    for (uint64_t step = 0; step < out.match.steps; ++step) {
        for (; next < out.match.log.size() && out.match.log[next].step == step; ++next) {
            engine.apply_command(out.match.log[next].command);
            rec.on_command(out.match.log[next].command);
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
    }
    if (next != out.match.log.size()) out.error = "the arena's log was not used up";
    out.hash = engine.state_hash().total;
    if (out.error.empty()) out.file = rec.finish(engine, out.error);
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "\n=======================================================\n [SUITE] Replays: the file, the recorder, the lock-step tap, the player and the list of orders\n"
                 "=======================================================\n";

    Map tiny;
    if (!load_shipped("TINY", tiny)) {
        std::cout << "  The shipped map TINY is missing: nothing can be tested\n";
        return 1;
    }

    // ---- the file ----------------------------------------------------------------------------------------------------------------

    TEST_CASE("RP1.1 CRC-32 Is The One Of zlib And PNG (\"123456789\" Gives 0xCBF43926) And Can Be Made In Pieces") {
        const std::string text = "123456789";
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text.data());
        ASSERT_EQ(crc32(bytes, text.size()), 0xCBF43926u);
        ASSERT_EQ(crc32(bytes + 4, text.size() - 4, crc32(bytes, 4)), 0xCBF43926u);
        ASSERT_EQ(crc32(nullptr, 0), 0u);
    } TEST_END();

    TEST_CASE("RP1.2 A Replay With Every Kind Of Command, Names, Teams, Hashes And An End Is Read Back Exactly And Written The Same Every Time") {
        const Replay r = synthetic();
        const std::vector<uint8_t> bytes = encoded(r);
        ASSERT_TRUE(bytes.size() > 100);
        const std::vector<uint8_t> magic = magic_bytes();
        ASSERT_TRUE(std::equal(magic.begin(), magic.end(), bytes.begin()));
        const Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok);
        ASSERT_TRUE(same(d.replay, r));
        ASSERT_EQ(encoded(d.replay), bytes);                                       // (read and written again: the same bytes)
        ASSERT_EQ(encoded(r), bytes);
        const std::vector<Chunk> chunks = chunks_of(bytes);
        ASSERT_TRUE(chunks.size() == 6 && chunks[0].tag == "HEAD" && chunks[1].tag == "CMDS" && chunks[chunks.size() - 2].tag == "hash" && chunks.back().tag == "ENDS");
        ASSERT_EQ(count_chunks(bytes, "CMDS"), 3u);                                // (turns 0 - 131, 20000 - 20001 and 2000000 - 2000001: three stretches of 30 s)
    } TEST_END();

    TEST_CASE("RP1.3 A Replay Without Commands, Names Or Teams Is A Replay; A Cut One (No ENDS) Reads As Incomplete With The Turns That It Has") {
        Replay r;
        r.head = head_for(tiny, "TINY", 1, 0x03);
        r.complete = true;
        r.total_turns = 0;
        const Decoded d = decoded(encoded(r));
        ASSERT_TRUE(d.ok && d.replay.complete && d.replay.commands.empty() && d.replay.total_turns == 0);
        ASSERT_TRUE(!d.replay.head.teams.set && d.replay.head.names[0].empty() && d.replay.head.recorder_seat == kNoSeat);
        Replay s = synthetic();
        s.complete = false;                                                        // (a recording that was not finished: no ENDS, and its turn count is what its last command and hash say)
        s.total_turns = 0;
        s.match_over = false;
        s.final_hash = 0;
        const Decoded cut = decoded(encoded(s));
        ASSERT_TRUE(cut.ok && !cut.replay.complete);
        ASSERT_EQ(cut.replay.total_turns, 2000001u);
        ASSERT_EQ(cut.replay.commands.size(), s.commands.size());
    } TEST_END();

    TEST_CASE("RP1.4 A Long Match Is Several CMDS Chunks (At Most 30 s Of Game Time And 4,096 Commands Each) And Reads Back Whole") {
        Replay r;
        r.head = head_for(tiny, "TINY", 9, 0x0F);
        for (uint32_t t = 0; t < 3000; ++t) r.commands.push_back(TimedCommand{t, make_command(CommandType::GroupMove, static_cast<uint8_t>(t % 4), static_cast<int16_t>(t % 50), 3, {t + 1})});
        r.complete = true;
        r.total_turns = 3000;
        std::vector<uint8_t> bytes = encoded(r);
        ASSERT_EQ(count_chunks(bytes, "CMDS"), 5u);                                // 600 turns each
        Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok && same(d.replay, r));
        Replay crowd;                                                              // (nine thousand commands in two turns: the count splits it)
        crowd.head = head_for(tiny, "TINY", 9, 0x0F);
        for (uint32_t i = 0; i < 9000; ++i) crowd.commands.push_back(TimedCommand{i / 4500, make_command(CommandType::Hatch, static_cast<uint8_t>(i % 4), 0, 0, {})});
        crowd.complete = true;
        crowd.total_turns = 2;
        bytes = encoded(crowd);
        ASSERT_EQ(count_chunks(bytes, "CMDS"), 3u);                                // 4096 + 4096 + 808
        d = decoded(bytes);
        ASSERT_TRUE(d.ok && same(d.replay, crowd));
    } TEST_END();

    TEST_CASE("RP1.5 The Writer Refuses What The Reader Would: A Command That The Wire Form Cannot Hold, A Turn Out Of Order Or Past The End, An Unprintable Text, A File Over 1 MiB") {
        std::string error;
        const auto refused = [&](const Replay& r) { return encode(r, error).empty() && !error.empty(); };
        Replay r = synthetic();
        ASSERT_FALSE(refused(r));
        Replay bad = r;
        bad.commands[2].command.ants.assign(33, 1u);
        ASSERT_TRUE(refused(bad));                                                 // 33 ants
        bad = r;
        bad.commands[2].command.ants.clear();
        ASSERT_TRUE(refused(bad));                                                 // a group order without ants
        bad = r;
        bad.commands[6].command.ants = {1};
        ASSERT_TRUE(refused(bad));                                                 // a Hatch with ants
        bad = r;
        bad.commands[0].command.type = CommandType::None;
        ASSERT_TRUE(refused(bad));
        bad = r;
        std::swap(bad.commands[2], bad.commands[8]);
        ASSERT_TRUE(refused(bad));                                                 // turns out of order
        bad = r;
        bad.total_turns = 100;
        ASSERT_TRUE(refused(bad));                                                 // commands after the end
        bad = r;
        bad.head.names[0] = "line\nbreak";
        ASSERT_TRUE(refused(bad));
        bad = r;
        bad.head.names[0] = std::string(65, 'x');
        ASSERT_TRUE(refused(bad));
        for (const std::string& text : {std::string("Zo\xC3\xA9"), std::string("\xFF"), std::string("bidi \xE2\x80\xAE text"), std::string("delete\x7F"), std::string("tab\there"), std::string("esc\x1B[31m"), std::string("nul\0x", 5)}) {
            bad = r;
            bad.head.names[0] = text;
            ASSERT_TRUE(refused(bad));                                             // a letter outside ASCII, a lone byte of 0x80 and up, a bidirectional mark, a control character, a NUL
            bad = r;
            bad.head.venue = text;
            ASSERT_TRUE(refused(bad));                                             // (every text of the head is held to it)
        }
        bad = r;
        bad.head.names[3] = " ~";
        ASSERT_FALSE(refused(bad));                                                // (the first and the last printable characters are fine)
        bad = r;
        bad.head.map_name = "../TINY.LVL";
        ASSERT_TRUE(refused(bad));
        bad = r;
        bad.head.roster = 0;
        ASSERT_TRUE(refused(bad));
        bad = r;
        bad.head.teams = sim::StartTeams{true, 0, 0};
        ASSERT_TRUE(refused(bad));
        bad = r;
        bad.head.hash_period = 0;
        ASSERT_TRUE(refused(bad));
        bad = r;
        bad.hashes.assign(20000, 7u);
        bad.total_turns = 2000001;
        ASSERT_FALSE(refused(bad));                                                // (a hash for every 100 turns of a match of 2,000,001 turns: 20,000 of them fit)
        bad.hashes.assign(20001, 7u);
        ASSERT_TRUE(refused(bad));                                                 // (more hashes than turns)
        Replay huge;
        huge.head = head_for(tiny, "TINY", 1, 0x0F);
        huge.complete = true;
        huge.total_turns = 100;
        for (uint32_t i = 0; i < 40000; ++i) huge.commands.push_back(TimedCommand{i / 400, make_command(CommandType::GroupMove, 0, 1, 1, std::vector<uint32_t>(32, i))});
        ASSERT_TRUE(refused(huge));                                                // 40,000 commands of 137 bytes: over 1 MiB
        ASSERT_TRUE(contains(error, "KiB"));
    } TEST_END();

    TEST_CASE("RP1.6 Every Single Flipped Byte Of A File Is Found (A Checksum, A Length, The Magic) And Never Reads As The Same Replay; The Reason Is Always Given") {
        const std::vector<uint8_t> bytes = encoded(synthetic());
        const Replay original = synthetic();
        for (size_t i = 0; i < bytes.size(); ++i) {
            std::vector<uint8_t> broken = bytes;
            broken[i] = static_cast<uint8_t>(broken[i] ^ 0x5Au);
            const Decoded d = decoded(broken);
            ASSERT_TRUE(d.ok ? !same(d.replay, original) : !d.error.empty());
            if (d.ok) ASSERT_FALSE(d.replay.complete);                             // (a length that grew reads as a cut file: it is not the whole match)
        }
    } TEST_END();

    TEST_CASE("RP1.7 A File Cut At Any Length Is Either Refused With A Reason Or An Incomplete Replay That Holds Whole Chunks Only") {
        const std::vector<uint8_t> bytes = encoded(synthetic());
        const size_t head_end = 8 + 12 + chunks_of(bytes)[0].payload.size();
        for (size_t length = 0; length < bytes.size(); ++length) {
            const Decoded d = decoded(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)));
            if (length < head_end) {
                ASSERT_FALSE(d.ok);
                ASSERT_FALSE(d.error.empty());
            } else {
                ASSERT_TRUE(d.ok && !d.replay.complete);
                const Replay whole = synthetic();                                  // (what is read is the start of the match: the commands of the chunks that are whole)
                ASSERT_TRUE(d.replay.commands.size() <= whole.commands.size());
                for (size_t i = 0; i < d.replay.commands.size(); ++i) ASSERT_TRUE(d.replay.commands[i].turn == whole.commands[i].turn && d.replay.commands[i].command == whole.commands[i].command);
            }
        }
        ASSERT_EQ(decoded(bytes).replay.commands.size(), synthetic().commands.size());
    } TEST_END();

    TEST_CASE("RP1.8 Hostile Files: Lies About Lengths, Counts, Orders, Numbers, Chunks And Commands Are Refused By Name; Unknown Fields And Lower Case Chunks Are Skipped") {
        const std::vector<Chunk> base = chunks_of(encoded(synthetic_small()));         // HEAD, CMDS, hash, ENDS
        ASSERT_TRUE(base.size() == 4 && base[1].tag == "CMDS" && base[2].tag == "hash" && base[3].tag == "ENDS");
        const auto refused_with = [&](std::vector<Chunk> chunks, const std::string& part) {
            const Decoded d = decoded(file_of(chunks));
            return !d.ok && contains(d.error, part);
        };
        // chunks
        {
            std::vector<Chunk> c = base;
            c.insert(c.begin() + 1, Chunk{"ZZZZ", {1, 2, 3}});
            ASSERT_TRUE(refused_with(c, "cannot skip"));                           // an unknown critical chunk
            c = base;
            c.insert(c.begin() + 1, Chunk{"zzzz", {1, 2, 3}});
            Decoded d = decoded(file_of(c));
            ASSERT_TRUE(d.ok && same(d.replay, synthetic_small()));                // (a lower case chunk is skipped)
            c = base;
            c.insert(c.begin() + 1, c[0]);
            ASSERT_TRUE(refused_with(c, "HEAD"));                                  // two heads
            c = base;
            std::swap(c[0], c[1]);
            ASSERT_TRUE(refused_with(c, "HEAD"));                                  // the head is not first
            c = base;
            c.push_back(Chunk{"zzzz", {}});
            ASSERT_TRUE(refused_with(c, "after the ENDS"));
            c = base;
            c.insert(c.begin() + 2, Chunk{"CMDS", {0, 0, 0, 0, 0, 0, 0, 0}});      // first turn 0 after commands up to turn 301
            ASSERT_TRUE(refused_with(c, "CMDS"));
            c = {base[0]};
            ASSERT_TRUE(decoded(file_of(c)).ok);                                   // (a head alone is an incomplete replay)
            c = {Chunk{"HEA7", {1}}};
            ASSERT_TRUE(refused_with(c, "no valid tag"));
            c = {Chunk{"CMDS", {}}};
            ASSERT_TRUE(refused_with(c, "not HEAD"));
            c = {};
            ASSERT_TRUE(refused_with(c, "no HEAD"));
        }
        // the head
        {
            const auto with_head = [&](const std::function<void(std::vector<uint8_t>&)>& edit) {
                std::vector<Chunk> c = base;
                edit(c[0].payload);
                return c;
            };
            ASSERT_TRUE(refused_with(with_head([](std::vector<uint8_t>& p) { p[0] = 2; p[1] = 0; }), "newer game"));         // format 2
            ASSERT_TRUE(refused_with(with_head([](std::vector<uint8_t>& p) { p[0] = 0; p[1] = 0; }), "format 0"));
            ASSERT_TRUE(refused_with(with_head([](std::vector<uint8_t>& p) { p.resize(3); }), "too short"));
            ASSERT_TRUE(refused_with(with_head([](std::vector<uint8_t>& p) { p.push_back(3); p.push_back(200); }), "cut"));      // field 3 of 200 bytes, with none behind it
            ASSERT_TRUE(refused_with(with_head([](std::vector<uint8_t>& p) { p.push_back(0x80); p.push_back(0x00); p.push_back(0); }), "cut"));   // a field id that is not in its shortest form
            Decoded d = decoded(file_of(with_head([](std::vector<uint8_t>& p) { p.push_back(99); p.push_back(2); p.push_back('o'); p.push_back('k'); })));
            ASSERT_TRUE(d.ok && same(d.replay, synthetic_small()));                // (a field that this reader does not know is skipped)
            // fields made from nothing: the head without the map name, with a map name that names a path, a roster that no seat plays, teams of seats that do not play
            const auto head_of = [&](const std::vector<uint8_t>& fields) {
                std::vector<uint8_t> p = {1, 0, static_cast<uint8_t>(net::kProtocolVersion & 0xFF), static_cast<uint8_t>(net::kProtocolVersion >> 8)};
                p.insert(p.end(), fields.begin(), fields.end());
                return std::vector<Chunk>{Chunk{"HEAD", p}};
            };
            const std::vector<uint8_t> tail = {4, 8, 1, 2, 3, 4, 5, 6, 7, 8, 5, 4, 1, 0, 0, 0, 6, 1, 0x0F, 7, 1, 0};
            std::vector<uint8_t> good = {3, 8, 'T', 'I', 'N', 'Y', '.', 'L', 'V', 'L'};
            good.insert(good.end(), tail.begin(), tail.end());
            ASSERT_TRUE(decoded(file_of(head_of(good))).ok);
            ASSERT_TRUE(refused_with(head_of(tail), "map name is missing"));
            std::vector<uint8_t> path_name = {3, 3, '.', '.', '/'};
            path_name.insert(path_name.end(), tail.begin(), tail.end());
            ASSERT_TRUE(refused_with(head_of(path_name), "map name"));
            std::vector<uint8_t> no_roster = good;
            no_roster[no_roster.size() - 4] = 0;                                   // the roster byte
            ASSERT_TRUE(refused_with(head_of(no_roster), "seat"));
            std::vector<uint8_t> teams = good;
            teams.insert(teams.end(), {12, 2, 0, 5});                              // seats 0 and 5
            ASSERT_TRUE(refused_with(head_of(teams), "teams"));
            std::vector<uint8_t> control = {3, 4, 'T', 'I', 0x01, 'Y'};
            control.insert(control.end(), tail.begin(), tail.end());
            ASSERT_TRUE(refused_with(head_of(control), "printable"));
            // a name that is not ASCII (UTF-8 and bytes that are no text at all), the venue, the game's version: a reader that showed them could be made to print a line that is not there
            for (const std::vector<uint8_t>& field : {std::vector<uint8_t>{8, 3, 'Z', 'o', 0xE9}, std::vector<uint8_t>{9, 2, 0xC3, 0xA9}, std::vector<uint8_t>{10, 3, 0xE2, 0x80, 0xAE}, std::vector<uint8_t>{14, 2, 'o', 0xFF},
                                                      std::vector<uint8_t>{1, 2, 'v', 0x80}, std::vector<uint8_t>{2, 2, 0x7F, 'x'}}) {
                std::vector<uint8_t> hostile = field;
                hostile.insert(hostile.end(), good.begin(), good.end());
                ASSERT_TRUE(refused_with(head_of(hostile), "printable"));
            }
            std::vector<uint8_t> plain_name = {8, 4, 'Z', 'o', 'e', '~'};            // (the same field in ASCII reads)
            plain_name.insert(plain_name.end(), good.begin(), good.end());
            ASSERT_TRUE(decoded(file_of(head_of(plain_name))).ok);
            std::vector<uint8_t> short_hash = {3, 1, 'T', 4, 3, 1, 2, 3};
            ASSERT_TRUE(refused_with(head_of(short_hash), "wrong length"));
            std::vector<uint8_t> fog2 = good;
            fog2[fog2.size() - 1] = 2;                                             // the fog byte
            ASSERT_TRUE(refused_with(head_of(fog2), "0 or 1"));
        }
        // CMDS
        {
            const auto with_cmds = [&](const std::function<void(std::vector<uint8_t>&)>& edit) {
                std::vector<Chunk> c = base;
                edit(c[1].payload);
                return c;
            };
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p.resize(5); }), "too short"));
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p[4] = 0xFF; p[5] = 0xFF; }), "out of range"));       // 65,535 commands
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p[4] = 100; }), "out of range"));                       // 100 commands in a few bytes
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p.push_back(0); }), "bytes after"));
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p[8 + 1] = 99; }), "not a valid command"));            // the type of the first command (after its gap)
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p[8] = 0x80; }), "CMDS"));                              // a gap that goes on into the type byte: what follows is no command
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p[8] = 0x80; p[9] = 0x00; }), "gap"));                  // not in its shortest form
            ASSERT_TRUE(refused_with(with_cmds([](std::vector<uint8_t>& p) { p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF; p[3] = 0xFF; }), "out of range"));     // first turn 4 billion
        }
        // hash and ENDS
        {
            std::vector<Chunk> c = base;
            c[2].payload[0] = 50;                                                  // another period than the head's
            ASSERT_TRUE(refused_with(c, "hash"));
            c = base;
            c[2].payload.push_back(1);
            ASSERT_TRUE(refused_with(c, "hash"));
            c = base;
            c.insert(c.begin() + 3, c[2]);
            ASSERT_TRUE(refused_with(c, "two hash"));
            c = base;
            c[3].payload.push_back(0);
            ASSERT_TRUE(refused_with(c, "ENDS"));
            c = base;
            c[3].payload[4] = 2;                                                   // a flag that does not exist
            ASSERT_TRUE(refused_with(c, "ENDS"));
            c = base;
            c[3].payload[0] = 5;                                                   // 5 turns in all, and a command at turn 301
            c[3].payload[1] = c[3].payload[2] = c[3].payload[3] = 0;
            ASSERT_TRUE(refused_with(c, "turn count"));
        }
    } TEST_END();

    TEST_CASE("RP1.9 A File Over 1 MiB Is Refused At Once, A Chunk That Claims Four Billion Bytes Does Not Allocate Them, And Random Bytes Are Never A Replay") {
        std::vector<uint8_t> big = encoded(synthetic());
        big.resize(kMaxFileBytes + 1, 0);
        Decoded d = decoded(big);
        ASSERT_TRUE(!d.ok && contains(d.error, "KiB"));
        std::vector<uint8_t> liar = magic_bytes();
        liar.insert(liar.end(), {'H', 'E', 'A', 'D', 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4, 5, 6, 7, 8});
        d = decoded(liar);
        ASSERT_TRUE(!d.ok && contains(d.error, "length"));
        Lcg rng(77);
        const std::vector<uint8_t> magic = magic_bytes();                          // (one vector: the two ends of a range must belong to the same one)
        for (int round = 0; round < 300; ++round) {
            std::vector<uint8_t> junk(rng.below(200));
            for (uint8_t& b : junk) b = static_cast<uint8_t>(rng.below(256));
            if (round % 2 == 0 && junk.size() >= 8) std::copy(magic.begin(), magic.end(), junk.begin());       // (with the right magic: the rest is noise)
            d = decoded(junk);
            ASSERT_FALSE(d.ok);
            ASSERT_FALSE(d.error.empty());
        }
        ASSERT_FALSE(decode(nullptr, 0, d.replay, d.error));
    } TEST_END();

    // ---- the recorder and the player ---------------------------------------------------------------------------------------------

    TEST_CASE("RP2.1 A Game On One Machine Records Its Commands At The Turn They Were Given And A Hash Every 100 Turns; The File Reads Back As What Was Played") {
        const Played p = play_scripted(tiny, 31, 450);
        ASSERT_FALSE(p.file.empty());
        ASSERT_TRUE(p.replay.complete);
        ASSERT_EQ(p.replay.total_turns, 450u);
        ASSERT_EQ(p.replay.hashes.size(), 4u);                                     // after 100, 200, 300 and 400 turns
        ASSERT_EQ(p.replay.final_hash, p.hash);
        ASSERT_TRUE(p.replay.commands.size() > 20);
        ASSERT_EQ(p.replay.commands.front().turn, 5u);
        ASSERT_TRUE(std::is_sorted(p.replay.commands.begin(), p.replay.commands.end(), [](const TimedCommand& a, const TimedCommand& b) { return a.turn < b.turn; }));
        const auto at = [&](uint32_t turn) {
            std::vector<Command> out;
            for (const TimedCommand& tc : p.replay.commands) {
                if (tc.turn == turn) out.push_back(tc.command);
            }
            return out;
        };
        ASSERT_TRUE(at(60).size() == 1 && at(60)[0].type == CommandType::AllianceInvite && at(60)[0].other_player == 1);
        ASSERT_TRUE(at(100).size() == 1 && at(100)[0].type == CommandType::Hatch && at(100)[0].issuer == 2);
        ASSERT_TRUE(at(10).size() == 2 && at(10)[0].issuer == 0 && at(10)[1].issuer == 1);                  // (the canonical order of a turn: by seat)
        ASSERT_EQ(p.replay.head.engine_rules, net::kProtocolVersion);
        ASSERT_EQ(p.replay.head.map_hash, tiny.hash);
    } TEST_END();

    TEST_CASE("RP2.2 A Recorded Match Plays Out On A Fresh Engine To The Same State: Every Hash And The Final One Match, And The Match's End Is The Same") {
        const Played p = play_scripted(tiny, 31, 450);
        Outcome o = play(p.replay, tiny.level);
        ASSERT_TRUE(o.ran && o.ok && o.complete);
        ASSERT_EQ(o.turns, 450u);
        ASSERT_EQ(o.hashes_checked, 4u);
        ASSERT_EQ(o.hash, p.hash);
        ASSERT_EQ(o.match_over, p.over);
        // a seat that does not play, a different roster: the roster is part of the start
        const Played three = play_scripted(tiny, 31, 450, 0x07);
        ASSERT_TRUE(three.hash != p.hash);
        o = play(three.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.hash == three.hash);
        // the match to its natural end: the replay ends the same way
        const Played whole = play_scripted(tiny, 3, 14400);
        o = play(whole.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.hash == whole.hash && o.match_over == whole.over);
    } TEST_END();

    TEST_CASE("RP2.3 A Replay That Is Not What Was Played Is Caught: A Changed Command, A Missing One, A Moved One, Another Seed, Another Roster; The First Bad Hash Is Named") {
        const Played p = play_scripted(tiny, 31, 450);
        const auto outcome_of = [&](const std::function<void(Replay&)>& edit) {
            Replay r = p.replay;
            edit(r);
            return play(r, tiny.level);
        };
        const uint32_t moved_turn = p.replay.commands.front().turn;
        Outcome o = outcome_of([](Replay& r) { r.commands[0].command.tile_x = static_cast<int16_t>(r.commands[0].command.tile_x + 7); });
        ASSERT_TRUE(o.ran ? !o.ok : !o.error.empty());
        ASSERT_TRUE(!o.ok && o.first_bad_turn > moved_turn && o.first_bad_turn % 100 == 0 && contains(o.error, "does not reproduce"));
        o = outcome_of([](Replay& r) { r.commands.erase(r.commands.begin()); });
        ASSERT_FALSE(o.ok);
        o = outcome_of([](Replay& r) { r.commands[0].turn += 3; });
        ASSERT_FALSE(o.ok);
        o = outcome_of([](Replay& r) { r.head.seed += 1; });
        ASSERT_TRUE(!o.ok && o.first_bad_turn == 100);                              // the first hash is taken after 100 turns
        o = outcome_of([](Replay& r) { r.head.roster = 0x07; });
        ASSERT_FALSE(o.ok);
        o = outcome_of([](Replay& r) { r.hashes[3] ^= 1u; });
        ASSERT_TRUE(!o.ok && o.first_bad_turn == 400 && contains(o.error, "0:20.0"));
        o = outcome_of([](Replay& r) { r.final_hash ^= 1u; });
        ASSERT_TRUE(!o.ok && o.ran && contains(o.error, "final state"));
        o = outcome_of([](Replay& r) { r.match_over = !r.match_over; });
        ASSERT_TRUE(!o.ok && contains(o.error, "ended differently"));
    } TEST_END();

    TEST_CASE("RP2.4 A Replay Of Other Simulation Rules Is Not Played: The Error Names Both Numbers And The Game That Made It; The Head Of Any Rules Still Reads; Another Protocol Number Alone Is No Reason") {
        Played p = play_scripted(tiny, 31, 150);
        const uint16_t foreign = static_cast<uint16_t>(kSimRulesMode187 + 1);        // (a number that no build plays: 2 is the rules of game mode 187)
        p.replay.head.sim_rules = foreign;
        p.replay.head.game_version = "v0.1.2";
        const std::vector<uint8_t> bytes = encoded(p.replay);
        const Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok && d.replay.head.sim_rules == foreign);
        const Outcome o = play(d.replay, tiny.level);
        ASSERT_TRUE(!o.ran && !o.ok);
        ASSERT_TRUE(contains(o.error, "simulation rules " + std::to_string(foreign)) && contains(o.error, "simulation rules " + std::to_string(kSimRules)) && contains(o.error, "v0.1.2"));
        ASSERT_EQ(o.turns, 0u);
        // a protocol number that moved while the simulation did not: the file still plays (RP7.1)
        Played q = play_scripted(tiny, 31, 150);
        q.replay.head.engine_rules = static_cast<uint16_t>(net::kProtocolVersion + 5);
        const Decoded e = decoded(encoded(q.replay));
        ASSERT_TRUE(e.ok);
        const Outcome ok = play(e.replay, tiny.level);
        ASSERT_TRUE(ok.ok && ok.hash == q.hash);
    } TEST_END();

    TEST_CASE("RP2.5 An Incomplete File Plays As Far As It Goes (And Says So); It Is Not A Failure That It Has No End") {
        const Played p = play_scripted(tiny, 31, 450);
        std::vector<Chunk> chunks = chunks_of(p.file);
        chunks.pop_back();                                                          // no ENDS
        const Decoded d = decoded(file_of(chunks));
        ASSERT_TRUE(d.ok && !d.replay.complete);
        const Outcome o = play(d.replay, tiny.level);
        ASSERT_TRUE(o.ran && o.ok && !o.complete);
        ASSERT_TRUE(o.turns >= 400 && o.turns <= 450);                              // (what the last command and the last hash say)
        ASSERT_EQ(o.hashes_checked, 4u);
    } TEST_END();

    TEST_CASE("RP2.6 Recorder: A Command That The Engine Turns Down Is Left Out And Counted, One That It Would Apply But The Wire Form Cannot Hold Spoils The Recording, A Missing Turn Spoils It, A Finished Recorder Takes Nothing More") {
        using Status = sim::CommandResult::Status;
        sim::SimulationEngine engine;
        engine.init(tiny.level, 1, 0x0F);
        Recorder rec(head_for(tiny, "TINY", 1, 0x0F));
        // what the engine turns down without changing anything: the recorder keeps no trace of it (the engine's own answer is the check, not the recorder's idea of it)
        const std::vector<Command> turned_down = {make_command(CommandType::GroupMove, 0, 3, 3, std::vector<uint32_t>(33, 1u)), make_command(CommandType::GroupMove, 0, 3, 3, {}), make_command(CommandType::Stop, 0, 0, 0, {}),
                                                  make_command(CommandType::None, 0, 0, 0, {}), make_command(static_cast<CommandType>(static_cast<uint8_t>(CommandType::Last) + 1), 0, 0, 0, {})};
        for (const Command& c : turned_down) {
            ASSERT_TRUE(engine.apply_command(c).status == Status::RejectedMalformed);
            rec.on_command(c);
        }
        ASSERT_TRUE(rec.skipped_commands() == turned_down.size() && rec.commands() == 0 && rec.failure().empty());
        for (int i = 0; i < 205; ++i) {
            engine.tick();
            rec.on_tick(engine);
        }
        ASSERT_EQ(rec.turns(), 205u);
        std::string error;
        const std::vector<uint8_t> bytes = rec.finish(engine, error);
        ASSERT_FALSE(bytes.empty());
        ASSERT_TRUE(rec.finished());
        const Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok && d.replay.commands.empty() && d.replay.hashes.size() == 2 && d.replay.total_turns == 205);
        rec.on_command(make_command(CommandType::Hatch, 0, 0, 0, {}));            // (nothing is taken after the end)
        rec.on_tick(engine);
        ASSERT_TRUE(rec.commands() == 0 && rec.turns() == 205);
        ASSERT_TRUE(rec.finish(engine, error).empty() && contains(error, "finished"));

        // a command that has no ant list and names ants: the engine takes no notice of the ants and applies the command, the wire form cannot hold them, so a file without it would not play the
        // match that was played. It is not skipped like the ones above: the recording is spoiled and says why
        {
            sim::SimulationEngine fresh;
            fresh.init(tiny.level, 1, 0x0F);
            ASSERT_TRUE(fresh.apply_command(make_command(CommandType::Hatch, 0, 0, 0, {5})).status == Status::Applied);        // (the engine takes it)
            ASSERT_TRUE(fresh.apply_command(make_command(CommandType::Quit, 1, 0, 0, {5, 6})).status == Status::Applied);
        }
        for (const CommandType type : {CommandType::Hatch, CommandType::AllianceInvite, CommandType::AllianceAccept, CommandType::AllianceDeny, CommandType::AllianceWithdraw, CommandType::AllianceBreak,
                                       CommandType::Quit, CommandType::Drop}) {
            Header spoiled_head = head_for(tiny, "TINY", 1, 0x0F);
            spoiled_head.recorder_seat = 0;
            Recorder spoiled(spoiled_head);
            spoiled.on_command(make_command(CommandType::GroupMove, 0, 3, 3, {1}));
            ASSERT_TRUE(spoiled.commands() == 1 && spoiled.own_commands() == 1 && spoiled.failure().empty());
            spoiled.on_command(make_command(type, 0, 0, 0, {5}, 1));
            ASSERT_TRUE(contains(spoiled.failure(), "no ant list") && spoiled.skipped_commands() == 0);
            ASSERT_TRUE(spoiled.commands() == 0 && spoiled.own_commands() == 0);                                              // (nothing is kept of a recording that is lost)
            spoiled.on_tick(engine);
            ASSERT_EQ(spoiled.turns(), 0u);
            ASSERT_TRUE(spoiled.finish(engine, error).empty() && contains(error, "no ant list"));
            // the same command without the ants is an ordinary command
            Recorder fine(head_for(tiny, "TINY", 1, 0x0F));
            fine.on_command(make_command(type, 0, 0, 0, {}, 1));
            ASSERT_TRUE(fine.commands() == 1 && fine.failure().empty() && fine.skipped_commands() == 0);
        }

        Recorder empty(head_for(tiny, "TINY", 1, 0x0F));
        ASSERT_TRUE(empty.finish(engine, error).empty() && contains(error, "no turn"));    // a match that never ticked has nothing to show
        Recorder gap(head_for(tiny, "TINY", 1, 0x0F));
        net::TurnMsg late;
        late.turn = 5;                                                              // the recording began at turn 5: it missed five
        gap.on_turn(late, engine);
        ASSERT_TRUE(!gap.failure().empty() && contains(gap.failure(), "turn 0"));
        ASSERT_TRUE(gap.finish(engine, error).empty() && contains(error, "turn 0"));
    } TEST_END();

    TEST_CASE("RP2.10 Recorder: The Commands Of The Seat That Records Are Counted Apart (The Computer Players' And The Other Machines' Are Held, Not Counted); A Head That Names No Seat Counts None") {
        sim::SimulationEngine engine;
        engine.init(tiny.level, 1, 0x0F);
        Header head = head_for(tiny, "TINY", 1, 0x0F);
        head.recorder_seat = 2;
        Recorder rec(head);
        rec.on_command(make_command(CommandType::GroupMove, 0, 3, 3, {1}));
        rec.on_command(make_command(CommandType::GroupMove, 1, 3, 3, {2}));
        rec.on_command(make_command(CommandType::Hatch, 3, 0, 0, {}));
        ASSERT_TRUE(rec.commands() == 3 && rec.own_commands() == 0);               // (three orders, none of them the recording seat's)
        rec.on_command(make_command(CommandType::GroupMove, 2, 3, 3, {3}));
        rec.on_command(make_command(CommandType::Quit, 2, 0, 0, {}));
        ASSERT_TRUE(rec.commands() == 5 && rec.own_commands() == 2);               // (its orders, the quit among them)
        rec.on_command(make_command(CommandType::GroupMove, 2, 3, 3, {}));          // (an order that the engine turns down is not held, so not counted)
        ASSERT_TRUE(rec.commands() == 5 && rec.own_commands() == 2 && rec.skipped_commands() == 1);
        net::TurnMsg turn;                                                          // the lock-step runner's way in: a turn with the orders of several seats
        turn.turn = 0;
        turn.commands = {make_command(CommandType::GroupMove, 1, 4, 4, {2}), make_command(CommandType::GroupMove, 2, 4, 4, {3}), make_command(CommandType::Stop, 2, 0, 0, {3})};
        rec.on_turn(turn, engine);
        ASSERT_TRUE(rec.commands() == 8 && rec.own_commands() == 4 && rec.turns() == 1);

        Recorder nobody(head_for(tiny, "TINY", 1, 0x0F));                           // (no recorder seat: a game that nobody plays at this machine)
        ASSERT_EQ(nobody.replay().head.recorder_seat, kNoSeat);
        for (uint8_t seat = 0; seat < 4; ++seat) nobody.on_command(make_command(CommandType::GroupMove, seat, 3, 3, {1u + seat}));
        nobody.on_command(make_command(CommandType::GroupMove, kNoSeat, 3, 3, {9}));
        ASSERT_TRUE(nobody.commands() == 5 && nobody.own_commands() == 0);
    } TEST_END();

    TEST_CASE("RP2.7 Recorder: A Match Too Long To Keep Is Dropped (No File, A Reason) And The Recorder Stops Growing") {
        sim::SimulationEngine engine;
        engine.init(tiny.level, 1, 0x0F);
        Recorder rec(head_for(tiny, "TINY", 1, 0x0F));
        const Command big = make_command(CommandType::GroupMove, 0, 3, 3, std::vector<uint32_t>(32, 1u));
        for (int i = 0; i < 9000 && rec.failure().empty(); ++i) rec.on_command(big);
        ASSERT_FALSE(rec.failure().empty());
        ASSERT_TRUE(rec.commands() == 0 && contains(rec.failure(), "too long"));
        engine.tick();
        rec.on_tick(engine);
        std::string error;
        ASSERT_TRUE(rec.finish(engine, error).empty() && contains(error, "too long"));
    } TEST_END();

    TEST_CASE("RP2.8 What the match began with is part of it: the teams made at the start and the Fog of War setting are set up as they were, and an order given after the last tick (a Quit that ends the match) is played too") {
        // the teams: a file that lost them is another match
        const sim::StartTeams teams{true, 0, 1};
        const Played with_teams = play_scripted(tiny, 31, 450, 0x0F, teams);
        ASSERT_TRUE(with_teams.replay.head.teams == teams);
        ASSERT_TRUE(with_teams.hash != play_scripted(tiny, 31, 450).hash);          // (an alliance changes the match: else nothing here would prove anything)
        Outcome o = play(with_teams.replay, tiny.level);
        ASSERT_TRUE(o.ran && o.ok && o.hash == with_teams.hash);
        Replay lost = with_teams.replay;
        lost.head.teams = sim::StartTeams{};
        o = play(lost, tiny.level);
        ASSERT_TRUE(!o.ok && contains(o.error, "does not reproduce"));
        // the Fog of War setting: the engine is set up as the match was (what a hook sees is what the engine says)
        for (const bool fog : {false, true}) {
            const Played p = play_scripted(tiny, 31, 120, 0x0F, sim::StartTeams{}, fog);
            ASSERT_EQ(p.replay.head.fog, fog);
            int seen = 0;
            bool right = true;
            Hooks hooks;
            hooks.before_command = [&](uint32_t, const Command&, sim::SimulationEngine& engine) {
                ++seen;
                right = right && engine.is_fog_of_war_enabled() == fog;
            };
            o = play(p.replay, tiny.level, hooks);
            ASSERT_TRUE(o.ok && o.hash == p.hash && seen > 0 && right);
        }
        // an order after the last tick: the match of two seats ends with a Quit
        sim::SimulationEngine engine;
        engine.init(tiny.level, 31, 0x03);
        Recorder rec(head_for(tiny, "TINY", 31, 0x03));
        for (uint32_t turn = 0; turn < 200; ++turn) {
            engine.tick();
            engine.clear_news_events();
            engine.clear_audio_events();
            rec.on_tick(engine);
        }
        const Command quit = make_command(CommandType::Quit, 1, 0, 0, {});
        engine.apply_command(quit);
        rec.on_command(quit);
        ASSERT_TRUE(engine.is_match_over());
        std::string error;
        const std::vector<uint8_t> bytes = rec.finish(engine, error);
        ASSERT_FALSE(bytes.empty());
        const Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok && d.replay.match_over && d.replay.total_turns == 200u && d.replay.commands.size() == 1 && d.replay.commands[0].turn == 200u);
        o = play(d.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.match_over && o.turns == 200u && o.hash == engine.state_hash().total);
    } TEST_END();

    TEST_CASE("RP2.9 A Replay Made By Hand Is Held To The Limits Of A File: A Hash Period Of 0 Or More Turns Than A Match Can Have Is Refused By play() With A Reason (Not Divided By Zero, Not Played For Hours)") {
        const Played p = play_scripted(tiny, 31, 150);
        Replay no_period = p.replay;
        no_period.head.hash_period = 0;
        Outcome o = play(no_period, tiny.level);
        ASSERT_TRUE(!o.ran && !o.ok && contains(o.error, "out of range"));
        ASSERT_EQ(o.turns, 0u);
        Replay too_long = p.replay;
        too_long.total_turns = kMaxTurns + 1;
        o = play(too_long, tiny.level);
        ASSERT_TRUE(!o.ran && !o.ok && contains(o.error, "out of range"));
        ASSERT_EQ(o.turns, 0u);
        o = play(p.replay, tiny.level);                                             // (the same replay, as it is, plays)
        ASSERT_TRUE(o.ran && o.ok);
        // the recorder is held to the same limit: it divides by the period at every tick
        sim::SimulationEngine engine;
        engine.init(tiny.level, 31, 0x0F);
        for (const uint32_t period : {0u, kMaxTurns + 1}) {
            Header head = head_for(tiny, "TINY", 31, 0x0F);
            head.hash_period = period;
            Recorder rec(head);
            ASSERT_TRUE(contains(rec.failure(), "hash period"));
            rec.on_command(make_command(CommandType::GroupMove, 0, 3, 3, {1}));
            engine.tick();
            rec.on_tick(engine);                                                   // (no division by zero)
            ASSERT_TRUE(rec.turns() == 0 && rec.commands() == 0);
            std::string error;
            ASSERT_TRUE(rec.finish(engine, error).empty() && contains(error, "hash period"));
        }
    } TEST_END();

    TEST_CASE("RP3.1 Four Computer Players Make A Real Match (Hundreds Of Commands Of Every Kind): Their Log, Recorded, Plays Out To The Arena's Own State Hash") {
        const ArenaRecording recording = record_arena_match(tiny, 5, 3000);
        const ai::ArenaResult& match = recording.match;
        ASSERT_TRUE(match.error.empty() && recording.error.empty());
        ASSERT_TRUE(match.log.size() > 100);
        ASSERT_EQ(recording.hash, match.hash);                                      // (the arena's log, given to a fresh engine as a game on one machine gives it, ends in the arena's own state)
        const std::vector<uint8_t>& bytes = recording.file;
        ASSERT_FALSE(bytes.empty());
        ASSERT_TRUE(bytes.size() < 100000);
        const Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok && d.replay.commands.size() == match.log.size());
        const Outcome o = play(d.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.complete);
        ASSERT_EQ(o.hash, match.hash);
        ASSERT_EQ(o.turns, static_cast<uint32_t>(match.steps));
        // the orders of this match, each with the types of the ants that it names
        Outcome listed;
        const std::vector<OrderRecord> orders = list_orders(d.replay, tiny.level, listed);
        ASSERT_TRUE(listed.ok && orders.size() == d.replay.commands.size());
        uint32_t named = 0;
        uint32_t found = 0;
        for (size_t i = 0; i < orders.size(); ++i) {
            ASSERT_EQ(orders[i].turn, d.replay.commands[i].turn);
            ASSERT_TRUE(orders[i].command == d.replay.commands[i].command);
            named += static_cast<uint32_t>(orders[i].command.ants.size());
            for (const OrderRecord::AntCount& c : orders[i].ants) found += c.count;
            found += orders[i].absent;
            if (!orders[i].command.ants.empty() && orders[i].absent < orders[i].command.ants.size()) ASSERT_TRUE(orders[i].has_from);
        }
        ASSERT_EQ(found, named);                                                    // (every named ant is counted: by type, or as absent)
    } TEST_END();

    // ---- the lock-step runner's tap ----------------------------------------------------------------------------------------------

    TEST_CASE("RP4.1 The Runner's Tap Writes The SAME File As A Game On One Machine: Live Turns And The Catch-Up's Turns Alike, In Slices Of Any Size") {
        const Played p = play_scripted(tiny, 31, 450);
        for (const uint32_t catch_up_turns : {0u, 1u, 120u, 300u, 448u, 450u}) {                // (a runner that is given one live turn after a catch-up waits for a second: the buffer of a link that goes on)
            sim::SimulationEngine engine;
            engine.init(tiny.level, 31, 0x0F);
            net::LockstepRunner runner(engine);
            Recorder rec(head_for(tiny, "TINY", 31, 0x0F));
            runner.set_on_executed([&](const net::TurnMsg& turn) { rec.on_turn(turn, engine); });
            uint32_t ticks_seen = 0;
            runner.set_on_tick([&]() { ++ticks_seen; });
            for (uint32_t t = 0; t < 450; ++t) {
                net::TurnMsg msg;
                msg.turn = t;
                msg.commands = p.turns[t];
                ASSERT_TRUE(t < catch_up_turns ? runner.on_catch_up_turn(msg) : runner.on_turn(msg));
            }
            uint32_t slice = 1;
            while (runner.next_turn_to_execute() < catch_up_turns) {                                  // the catch-up: in slices of growing size, as a window slices it into frames
                ASSERT_TRUE(runner.fast_forward(std::min(slice, catch_up_turns - runner.next_turn_to_execute())) > 0);
                slice = slice * 2 + 1;
            }
            for (int frames = 0; frames < 200000 && runner.next_turn_to_execute() < 450; ++frames) runner.update(50);
            ASSERT_EQ(runner.next_turn_to_execute(), 450u);
            ASSERT_EQ(rec.turns(), 450u);
            ASSERT_EQ(ticks_seen, 450u - catch_up_turns);                           // (the catch-up's turns call no tick hook: only the tap sees them)
            std::string error;
            const std::vector<uint8_t> bytes = rec.finish(engine, error);
            ASSERT_EQ(bytes, p.file);
        }
    } TEST_END();

    TEST_CASE("RP4.2 The Tap Is Called After The Turn's Tick And Before The Tick Hook (The Application Ends The Match From The Tick Hook), With The Turn As It Ran") {
        sim::SimulationEngine engine;
        engine.init(tiny.level, 7, 0x0F);
        net::LockstepRunner runner(engine);
        std::vector<std::string> order;
        uint64_t tick_at_tap = 0;
        runner.set_on_executed([&](const net::TurnMsg& turn) {
            order.push_back("executed " + std::to_string(turn.turn) + " with " + std::to_string(turn.commands.size()));
            tick_at_tap = engine.current_tick();
        });
        runner.set_on_tick([&]() { order.push_back("tick"); });
        for (uint32_t t = 0; t < 3; ++t) {
            net::TurnMsg msg;
            msg.turn = t;
            if (t == 1) msg.commands.push_back(make_command(CommandType::AllianceInvite, 0, 0, 0, {}, 1));
            ASSERT_TRUE(runner.on_turn(msg));
        }
        for (int frames = 0; frames < 20 && runner.next_turn_to_execute() < 3; ++frames) runner.update(50);
        ASSERT_TRUE(order.size() == 6 && order[0] == "executed 0 with 0" && order[1] == "tick" && order[2] == "executed 1 with 1" && order[3] == "tick" && order[4] == "executed 2 with 0");
        ASSERT_EQ(tick_at_tap, 3u);                                                 // (the tick of the turn had run when the tap was called)
    } TEST_END();

    // ---- the list of orders ------------------------------------------------------------------------------------------------------

    TEST_CASE("RP5.1 Every Order Is Listed With Its Time, Seat, Target And The Types Of Its Ants: An Ant That Is Not The Issuer's, Or That Never Was, Is Counted As Not There") {
        const Played p = play_scripted(tiny, 31, 450);
        Outcome o;
        const std::vector<OrderRecord> orders = list_orders(p.replay, tiny.level, o);
        ASSERT_TRUE(o.ok);
        ASSERT_EQ(orders.size(), p.replay.commands.size());
        const auto find = [&](uint32_t turn, uint8_t issuer, CommandType type) -> const OrderRecord* {
            for (const OrderRecord& r : orders) {
                if (r.turn == turn && r.command.issuer == issuer && r.command.type == type) return &r;
            }
            return nullptr;
        };
        const OrderRecord* move = find(5, 0, CommandType::GroupMove);
        ASSERT_TRUE(move != nullptr && move->has_from && move->absent == 0 && move->status == sim::CommandResult::Status::Applied);
        uint32_t ants = 0;
        for (const OrderRecord::AntCount& c : move->ants) ants += c.count;
        ASSERT_EQ(ants, static_cast<uint32_t>(move->command.ants.size()));
        ASSERT_TRUE(!move->ants.empty() && move->ants_ordered == ants);
        const OrderRecord* foreign = find(10, 1, CommandType::GroupMove);          // seat 1 orders a Green ant
        ASSERT_TRUE(foreign != nullptr && foreign->absent == 1 && foreign->ants.empty() && !foreign->has_from && foreign->status == sim::CommandResult::Status::Ignored);
        const OrderRecord* never = find(10, 0, CommandType::GroupMove);            // an ant number that no ant has
        ASSERT_TRUE(never != nullptr && never->absent == 1 && never->ants.empty());
        const OrderRecord* invite = find(60, 0, CommandType::AllianceInvite);
        ASSERT_TRUE(invite != nullptr && invite->ants.empty() && invite->status == sim::CommandResult::Status::Applied);
        const OrderRecord* hatch = find(100, 2, CommandType::Hatch);
        ASSERT_TRUE(hatch != nullptr && hatch->status == sim::CommandResult::Status::Applied);
    } TEST_END();

    TEST_CASE("RP5.2 The Words Of The List: Game Time As M:SS.T, Seats As Colours (With The Name), Ants As Counts By Type, Places As Tiles Or Allies") {
        ASSERT_EQ(format_time(0), std::string("0:00.0"));
        ASSERT_EQ(format_time(1), std::string("0:00.0"));
        ASSERT_EQ(format_time(2), std::string("0:00.1"));
        ASSERT_EQ(format_time(20), std::string("0:01.0"));
        ASSERT_EQ(format_time(599), std::string("0:29.9"));
        ASSERT_EQ(format_time(1200), std::string("1:00.0"));
        ASSERT_EQ(format_time(14405), std::string("12:00.2"));
        ASSERT_EQ(format_time(72000), std::string("60:00.0"));
        Header h;
        h.names = {"Dave", "", "", ""};
        ASSERT_EQ(seat_label(h, 0), std::string("Green (Dave)"));
        ASSERT_EQ(seat_label(h, 1), std::string("Red"));
        ASSERT_EQ(seat_label(h, 2), std::string("Blue"));
        ASSERT_EQ(seat_label(h, 3), std::string("Black"));
        ASSERT_EQ(seat_label(h, 7), std::string("seat 7"));
        OrderRecord r;
        r.ants = {{sim::AntType::Bomber, 3}, {sim::AntType::Worker, 1}, {sim::AntType::Swimmer, 2}};
        ASSERT_EQ(ants_text(r), std::string("3 Bomber, 1 Worker, 2 Swimmer"));
        r.absent = 2;
        ASSERT_EQ(ants_text(r), std::string("3 Bomber, 1 Worker, 2 Swimmer + 2 not there"));
        r.ants.clear();
        ASSERT_EQ(ants_text(r), std::string("2 not there"));
        r.absent = 0;
        ASSERT_EQ(ants_text(r), std::string(""));
        const sim::AntType all[] = {sim::AntType::Worker, sim::AntType::Bomber, sim::AntType::Fire, sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer};
        const char* names[] = {"Worker", "Bomber", "Fire", "Thief", "Combat", "Swimmer"};
        for (size_t i = 0; i < 6; ++i) ASSERT_EQ(std::string(ant_type_name(all[i])), std::string(names[i]));
        r.command = make_command(CommandType::GroupSpecial, 0, 45, 32, {1});
        ASSERT_EQ(place_text(r, h), std::string("(45,32)"));
        r.command = make_command(CommandType::AllianceInvite, 0, 0, 0, {}, 0 + 1);
        ASSERT_EQ(place_text(r, h), std::string("with Red"));
        r.command = make_command(CommandType::Hatch, 0, 0, 0, {});
        ASSERT_EQ(place_text(r, h), std::string(""));
        ASSERT_EQ(std::string(order_name(CommandType::GroupMove)), std::string("move"));
        ASSERT_EQ(std::string(order_name(CommandType::GroupSpecial)), std::string("special"));
        ASSERT_EQ(std::string(order_name(CommandType::GroupAttack)), std::string("attack"));
        ASSERT_EQ(std::string(order_name(CommandType::Quit)), std::string("quit"));
        ASSERT_EQ(std::string(status_name(sim::CommandResult::Status::Applied)), std::string("ok"));
        ASSERT_EQ(std::string(status_name(sim::CommandResult::Status::Ignored)), std::string("ignored"));
        ASSERT_EQ(std::string(status_name(sim::CommandResult::Status::RejectedIssuer)), std::string("refused"));
        for (uint8_t t = 1; t <= static_cast<uint8_t>(CommandType::Last); ++t) ASSERT_TRUE(std::string(order_name(static_cast<CommandType>(t))) != "?");
    } TEST_END();

    // ---- the map ------------------------------------------------------------------------------------------------------------------

    TEST_CASE("RP6.1 The Replay's Map Is Found By Its Name (In Any Case), Checked By Its Hash And Loaded; A Missing Map And Another File Of The Same Name Are Refused With The Facts") {
        TempDir dir;
        fs::copy_file(tiny.path, dir.path / "tiny.lvl");
        Header h = head_for(tiny, "TINY", 1, 0x0F);
        assets::LevelData level;
        std::string error;
        ASSERT_TRUE(load_map(h, dir.path.string(), level, error));
        ASSERT_TRUE(error.empty() && level.width() == tiny.level.width());
        ASSERT_FALSE(load_map(h, (dir.path / "nowhere").string(), level, error));
        ASSERT_TRUE(contains(error, "TINY.LVL") && contains(error, "not in"));
        h.map_hash ^= 1u;
        ASSERT_FALSE(load_map(h, dir.path.string(), level, error));
        ASSERT_TRUE(contains(error, "not the file") && contains(error, "hash"));
        h.map_hash ^= 1u;
        h.map_name = "../TINY.LVL";
        ASSERT_FALSE(load_map(h, dir.path.string(), level, error));
        ASSERT_TRUE(contains(error, "cannot name"));
        {
            std::ofstream broken(dir.path / "BROKEN.LVL", std::ios::binary);
            broken << "this is not a map";
        }
        uint64_t broken_hash = 0;
        ASSERT_TRUE(net::hash_file((dir.path / "BROKEN.LVL").string(), broken_hash));
        h.map_name = "BROKEN.LVL";
        h.map_hash = broken_hash;
        ASSERT_FALSE(load_map(h, dir.path.string(), level, error));
        ASSERT_TRUE(contains(error, "cannot be loaded"));
    } TEST_END();

    // ---- the rules number of the simulation (kSimRules) ---------------------------------------------------------------------------

    TEST_CASE("RP7.1 The Head Says The Rules Number Of The Simulation, As An Optional Field: Written Always, Read Back; A File Without It Is Judged By The Table Of Protocol Numbers (15 And 16 Are The Same Simulation, Any Other Is Unknown)") {
        Played p = play_scripted(tiny, 33, 120);
        ASSERT_EQ(p.replay.head.sim_rules, kSimRules);
        ASSERT_TRUE(plays_here(p.replay.head));
        // a file that was made before the field (the head of PR 31's writer: no field 16): sim_rules reads 0 and the table of protocol numbers decides
        std::vector<Chunk> chunks = chunks_of(p.file);
        std::vector<uint8_t>& head = chunks[0].payload;
        ASSERT_TRUE(head.size() > 4 && head[head.size() - 4] == 16 && head[head.size() - 3] == 2);                  // (the field is the last: id 16, length 2, the number)
        head.resize(head.size() - 4);
        head[2] = 16;                                                                                              // (and it says the protocol of the release that wrote such files, the last one before the number moved on: u16 after the format)
        head[3] = 0;
        const Decoded old = decoded(file_of(chunks));
        ASSERT_TRUE(old.ok && old.replay.head.sim_rules == 0 && old.replay.head.engine_rules == 16);
        for (const uint16_t protocol : {uint16_t{15}, uint16_t{16}}) {
            Header h = old.replay.head;
            h.engine_rules = protocol;
            ASSERT_EQ(sim_rules_of(h), 1u);
            ASSERT_TRUE(plays_here(h) == (kSimRules == 1));
        }
        for (const uint16_t protocol : {uint16_t{0}, uint16_t{1}, uint16_t{14}, uint16_t{17}, uint16_t{18}, uint16_t{200}}) {      // (17 wrote the field itself: no file of it lacks it)
            Header h = old.replay.head;
            h.engine_rules = protocol;
            ASSERT_EQ(sim_rules_of(h), 0u);
            ASSERT_FALSE(plays_here(h));
        }
        // the field wins over the table
        Header h = old.replay.head;
        h.engine_rules = 15;
        h.sim_rules = 7;
        ASSERT_EQ(sim_rules_of(h), 7u);
        // a field of the wrong size or 0 is refused; the played file of the old kind plays (its hashes are the simulation's)
        const Outcome o = play(old.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.hash == p.hash);
    } TEST_END();

    TEST_CASE("RP7.2 A Head Field Of The Rules Number That Is 0 Or Of The Wrong Size Is Refused With The Field Named") {
        Played p = play_scripted(tiny, 33, 120);
        const std::vector<Chunk> chunks = chunks_of(p.file);
        const auto refused = [&](const std::vector<uint8_t>& head) {
            std::vector<Chunk> edited = chunks;
            edited[0].payload = head;
            const Decoded d = decoded(file_of(edited));
            return !d.ok && contains(d.error, "field 16");
        };
        std::vector<uint8_t> zero = chunks[0].payload;
        zero[zero.size() - 2] = 0;
        zero[zero.size() - 1] = 0;
        ASSERT_TRUE(refused(zero));
        std::vector<uint8_t> three = chunks[0].payload;
        three[three.size() - 3] = 3;
        three.push_back(0);
        ASSERT_TRUE(refused(three));
    } TEST_END();

    // The reference match: a scripted match on TINY (orders from the engine's own state, no bot) and the state hash that it ends in, for each rules number that has existed. A change that moves the
    // hash without moving kSimRules strands nothing visible today and everything tomorrow: this test says so. When it fails because the match plays out differently on purpose: raise kSimRules, add the
    // new line here (keep the old ones), and say in docs/REPLAYS.md which protocol numbers share which rules.
    struct ReferenceHash {
        uint16_t sim_rules;
        uint64_t hash;
    };
    constexpr ReferenceHash kReferenceHashes[] = {{1, 0x7a96df7831c965e0ull}};

    TEST_CASE("RP7.3 The Reference Match Ends In The Hash That Its Rules Number Says: The Simulation Cannot Change Without kSimRules") {
        const Played p = play_scripted(tiny, 31, 1500);
        ASSERT_TRUE(p.replay.commands.size() > 20);
        const ReferenceHash* line = nullptr;
        for (const ReferenceHash& r : kReferenceHashes) {
            if (r.sim_rules == kSimRules) line = &r;
        }
        if (line == nullptr || line->hash != p.hash) std::cout << "\n    the reference match ends in " << std::hex << p.hash << std::dec << " (kSimRules " << kSimRules << ")\n";
        ASSERT_TRUE(line != nullptr);
        ASSERT_EQ(line->hash, p.hash);
        // and the replay of that match, played by the player, ends there too
        const Outcome o = play(p.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.hash == line->hash);
    } TEST_END();

    // ---- the game mode in the head (field 17): a match of mode 187 carries its mode and the rules number 2; every match of mode 0 is what it was ----------------------------------------------------

    constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);

    // The fields of a head payload (after the two u16 of the format and the protocol), in order, and the payload made of fields again: to find, drop, change and add a field. Every id and length of these heads is below 128: one byte.
    struct HeadField {
        uint32_t id{0};
        std::vector<uint8_t> bytes;
    };
    const auto fields_of = [](const std::vector<uint8_t>& payload) {
        std::vector<HeadField> out;
        size_t pos = 4;
        while (pos + 2 <= payload.size()) {
            HeadField f;
            f.id = payload[pos];
            const size_t length = payload[pos + 1];
            f.bytes.assign(payload.begin() + static_cast<std::ptrdiff_t>(pos + 2), payload.begin() + static_cast<std::ptrdiff_t>(pos + 2 + length));
            out.push_back(std::move(f));
            pos += 2 + length;
        }
        return out;
    };
    const auto payload_of = [](const std::vector<uint8_t>& old_payload, const std::vector<HeadField>& fields) {
        std::vector<uint8_t> out(old_payload.begin(), old_payload.begin() + 4);
        for (const HeadField& f : fields) {
            out.push_back(static_cast<uint8_t>(f.id));
            out.push_back(static_cast<uint8_t>(f.bytes.size()));
            out.insert(out.end(), f.bytes.begin(), f.bytes.end());
        }
        return out;
    };
    const auto find_field = [](const std::vector<HeadField>& fields, uint32_t id) -> const HeadField* {
        for (const HeadField& f : fields) {
            if (f.id == id) return &f;
        }
        return nullptr;
    };
    // the file of `played` with its head changed by `edit` (fields to add, drop, change)
    const auto file_with_head = [&](const Played& played, const std::function<void(std::vector<HeadField>&)>& edit) {
        std::vector<Chunk> chunks = chunks_of(played.file);
        std::vector<HeadField> fields = fields_of(chunks[0].payload);
        edit(fields);
        chunks[0].payload = payload_of(chunks[0].payload, fields);
        return file_of(chunks);
    };
    const auto drop_field = [](std::vector<HeadField>& fields, uint32_t id) {
        fields.erase(std::remove_if(fields.begin(), fields.end(), [&](const HeadField& f) { return f.id == id; }), fields.end());
    };
    const auto set_field = [](std::vector<HeadField>& fields, uint32_t id, std::vector<uint8_t> bytes) {
        for (HeadField& f : fields) {
            if (f.id == id) {
                f.bytes = std::move(bytes);
                return;
            }
        }
        fields.push_back(HeadField{id, std::move(bytes)});
    };

    TEST_CASE("RP7.4 A Match Of Game Mode 187 Carries Its Mode In Head Field 17 (Written Only For A Mode That Is Not 0) And The Rules Number 2 In Field 16: Read Back, Written The Same, Played To The Same End; A Match Of Mode 0 Has No Field 17 And Keeps Rules 1") {
        const Played p = play_scripted(tiny, 33, 300, 0x0F, sim::StartTeams{}, false, nullptr, k187);
        ASSERT_TRUE(!p.file.empty() && p.replay.complete);
        ASSERT_TRUE(p.replay.head.mode == k187 && p.replay.head.sim_rules == kSimRulesMode187 && kSimRulesMode187 == 2 && kSimRules == 1);
        ASSERT_TRUE(plays_here(p.replay.head) && sim_rules_of(p.replay.head) == 2u);
        // the head: field 17 is one byte, 1, written right before field 16 (the last), which says 2
        const std::vector<HeadField> fields = fields_of(chunks_of(p.file)[0].payload);
        ASSERT_TRUE(fields.size() >= 3 && fields[fields.size() - 2].id == 17 && fields.back().id == 16);
        ASSERT_TRUE(fields[fields.size() - 2].bytes == std::vector<uint8_t>({1}) && fields.back().bytes == std::vector<uint8_t>({2, 0}));
        // read back exactly, written again the same, and it plays out to the hash of the match that was played
        const Decoded d = decoded(p.file);
        ASSERT_TRUE(d.ok && same(d.replay, p.replay) && d.replay.head.mode == k187);
        ASSERT_EQ(encoded(d.replay), p.file);
        const Outcome o = play(d.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.complete && o.hash == p.hash);
        // the player set the engine up with the mode before the first init: the map's own start (every seat of the roster has its ants) and no food on the map, as the match began
        sim::SimulationEngine engine;
        begin_match(engine, d.replay, tiny.level, false);
        ASSERT_TRUE(engine.game_mode() == sim::GameMode::Kills187);
        std::array<uint32_t, sim::MAX_PLAYERS> ants{};
        for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
            if (a.player_id < sim::MAX_PLAYERS) ++ants[a.player_id];
        }
        for (const uint32_t n : ants) ASSERT_TRUE(n > 0);
        for (const auto& cell : engine.get_world_state().cells) ASSERT_FALSE(cell.is_food);
        // the same file taken for the original's game starts with the food on the map, and in another state
        Replay as_original = d.replay;
        as_original.head.set_mode(0);
        sim::SimulationEngine hs_engine;
        begin_match(hs_engine, as_original, tiny.level, false);
        ASSERT_TRUE(hs_engine.game_mode() == sim::GameMode::HighestScore);
        bool food = false;
        for (const auto& cell : hs_engine.get_world_state().cells) food = food || cell.is_food;
        ASSERT_TRUE(food);
        ASSERT_FALSE(hs_engine.state_hash() == engine.state_hash());
        // another game than the same seed's highest-score match
        const Played original = play_scripted(tiny, 33, 300);
        ASSERT_TRUE(original.hash != p.hash);
        // a match of mode 0: no field 17, rules 1, whatever else; and its engine is the original's
        const std::vector<HeadField> plain = fields_of(chunks_of(original.file)[0].payload);
        ASSERT_TRUE(find_field(plain, 17) == nullptr && plain.back().id == 16 && plain.back().bytes == std::vector<uint8_t>({1, 0}));
        ASSERT_TRUE(original.replay.head.mode == 0 && original.replay.head.sim_rules == kSimRules && plays_here(original.replay.head));
        // the recorder stamps the rules number that goes with the mode on a head that only named the mode (a head made by hand, a caller that forgot)
        Header unstamped = head_for(tiny, "TINY", 1, 0x03);
        unstamped.mode = k187;
        ASSERT_EQ(unstamped.sim_rules, kSimRules);
        Recorder rec(unstamped);
        ASSERT_TRUE(rec.replay().head.mode == k187 && rec.replay().head.sim_rules == kSimRulesMode187);
        Header stamped = head_for(tiny, "TINY", 1, 0x03);
        stamped.set_mode(k187);
        ASSERT_TRUE(stamped.mode == k187 && stamped.sim_rules == kSimRulesMode187);
        stamped.set_mode(0);
        ASSERT_TRUE(stamped.mode == 0 && stamped.sim_rules == kSimRules);
        // a snapshot of a 187 match that still runs says the same
        SnapshotTap tap;
        tap.every = 100;
        const Played live = play_scripted(tiny, 33, 250, 0x0F, sim::StartTeams{}, false, &tap, k187);
        ASSERT_TRUE(!live.file.empty() && tap.files.size() == 2 && !tap.files[0].empty());
        const Decoded snap = decoded(tap.files[1]);
        ASSERT_TRUE(snap.ok && !snap.replay.complete && snap.replay.head.mode == k187 && snap.replay.head.sim_rules == kSimRulesMode187 && plays_here(snap.replay.head));
        const Outcome so = play(snap.replay, tiny.level);
        ASSERT_TRUE(so.ok && so.turns == 200u);
    } TEST_END();

    TEST_CASE("RP7.5 A Head Whose Mode And Rules Number Do Not Belong Together Is Refused By The Writer And By The Reader: A Mode With Rules 1 Or With No Rules Number, Rules 2 Without The Mode, A Mode Above The Last (And A Field 17 Of 0 Or Of The Wrong Size), Each With The Field Named; No Such Replay Is Played") {
        const Played p = play_scripted(tiny, 33, 120, 0x0F, sim::StartTeams{}, false, nullptr, k187);
        const Played original = play_scripted(tiny, 33, 120);
        ASSERT_TRUE(!p.file.empty() && !original.file.empty());
        // the writer: encode() applies the same checks as the reader
        {
            const auto refused = [&](Replay r, const std::string& part) {
                std::string error;
                return encode(r, error).empty() && contains(error, part);
            };
            Replay mode_with_rules_1 = p.replay;
            mode_with_rules_1.head.sim_rules = kSimRules;
            ASSERT_TRUE(refused(mode_with_rules_1, "game mode"));
            Replay mode_without_rules = p.replay;
            mode_without_rules.head.sim_rules = 0;
            ASSERT_TRUE(refused(mode_without_rules, "game mode"));
            Replay rules_2_without_mode = original.replay;
            rules_2_without_mode.head.sim_rules = kSimRulesMode187;
            ASSERT_TRUE(refused(rules_2_without_mode, "game mode"));
            Replay mode_2 = p.replay;
            mode_2.head.mode = static_cast<uint8_t>(sim::kLastGameMode + 1);
            ASSERT_TRUE(refused(mode_2, "game mode"));
            Replay mode_2_set = p.replay;
            mode_2_set.head.set_mode(static_cast<uint8_t>(sim::kLastGameMode + 1));
            ASSERT_TRUE(mode_2_set.head.sim_rules == 0 && refused(mode_2_set, "game mode"));
            Replay mode_255 = p.replay;
            mode_255.head.mode = 255;
            ASSERT_TRUE(refused(mode_255, "game mode"));
            for (const uint8_t unknown : {uint8_t{2}, uint8_t{255}}) {          // a mode that nobody has with the rules number of a later build (3, 255): no rule about the pair catches it, only the mode itself does
                Replay later_build = p.replay;
                later_build.head.mode = unknown;
                later_build.head.sim_rules = unknown == 2 ? uint16_t{3} : uint16_t{255};
                ASSERT_TRUE(refused(later_build, "game mode"));
            }
            std::string error;
            ASSERT_TRUE(!encode(p.replay, error).empty() && !encode(original.replay, error).empty());     // (and the two that belong together are written)
        }
        // the reader: the same, from the bytes
        const auto reads = [&](const Played& base, const std::function<void(std::vector<HeadField>&)>& edit) { return decoded(file_with_head(base, edit)); };
        ASSERT_TRUE(reads(p, [](std::vector<HeadField>&) {}).ok);
        {   // a mode with rules 1 (a writer that kept the old number): refused for the head, not played as the original's game
            const Decoded d = reads(p, [&](std::vector<HeadField>& f) { set_field(f, 16, {1, 0}); });
            ASSERT_TRUE(!d.ok && contains(d.error, "HEAD") && contains(d.error, "game mode"));
        }
        {   // a mode with no rules number at all (the table of protocol numbers never gives a mode)
            const Decoded d = reads(p, [&](std::vector<HeadField>& f) { drop_field(f, 16); });
            ASSERT_TRUE(!d.ok && contains(d.error, "game mode"));
        }
        {   // a file of mode 0 that got a field 17 and kept rules 1
            const Decoded d = reads(original, [&](std::vector<HeadField>& f) { set_field(f, 17, {1}); });
            ASSERT_TRUE(!d.ok && contains(d.error, "game mode"));
        }
        {   // rules 2 without the mode (a reader that dropped field 17 would see this)
            const Decoded d = reads(p, [&](std::vector<HeadField>& f) { drop_field(f, 17); });
            ASSERT_TRUE(!d.ok && contains(d.error, "game mode"));
            const Decoded e = reads(original, [&](std::vector<HeadField>& f) { set_field(f, 16, {2, 0}); });
            ASSERT_TRUE(!e.ok && contains(e.error, "game mode"));
        }
        for (const uint8_t bad : {uint8_t{0}, uint8_t{2}, uint8_t{3}, uint8_t{255}}) {          // field 17: 0 is the original's game (no field), 2 and up are modes that this build does not know
            const Decoded d = reads(p, [&](std::vector<HeadField>& f) { set_field(f, 17, {bad}); });
            ASSERT_TRUE(!d.ok && contains(d.error, "field 17"));
        }
        for (const size_t length : {size_t{0}, size_t{2}, size_t{4}}) {                          // ... and of the wrong size
            const Decoded d = reads(p, [&](std::vector<HeadField>& f) { set_field(f, 17, std::vector<uint8_t>(length, 1)); });
            ASSERT_TRUE(!d.ok && contains(d.error, "field 17"));
        }
        // a replay that was made by hand (it never went through the writer): not played, whatever its head says
        {
            Replay r = p.replay;
            r.head.sim_rules = kSimRules;                                                       // a mode with the original's rules number
            ASSERT_FALSE(plays_here(r.head));
            Outcome o = play(r, tiny.level);
            ASSERT_TRUE(!o.ran && !o.ok && contains(o.error, "simulation rules"));
            r = p.replay;
            r.head.mode = 7;                                                                     // a mode that nobody has
            ASSERT_FALSE(plays_here(r.head));
            o = play(r, tiny.level);
            ASSERT_TRUE(!o.ran && !o.ok);
            r = original.replay;
            r.head.sim_rules = kSimRulesMode187;                                                 // the rules of 187 with no mode
            ASSERT_FALSE(plays_here(r.head));
            r = original.replay;
            r.head.mode = k187;                                                                  // the mode with the original's rules
            ASSERT_FALSE(plays_here(r.head));
            r = p.replay;
            r.head.engine_rules = 99;                                                            // (the protocol number is no reason either way)
            ASSERT_TRUE(plays_here(r.head));
            o = play(r, tiny.level);
            ASSERT_TRUE(o.ok && o.hash == p.hash);
        }
        // the table of the numbers: what plays here, mode by mode
        ASSERT_TRUE(sim_rules_for_mode(0) == 1 && sim_rules_for_mode(1) == 2 && sim_rules_for_mode(2) == 0 && sim_rules_for_mode(255) == 0);
        {
            struct Case { uint8_t mode; uint16_t rules; bool plays; };
            for (const Case& c : {Case{0, 1, true}, Case{1, 2, true}, Case{0, 2, false}, Case{1, 1, false}, Case{0, 3, false}, Case{1, 3, false}, Case{2, 2, false}, Case{2, 1, false}, Case{2, 3, false}, Case{0, 0, false}, Case{1, 0, false}, Case{2, 0, false}, Case{3, 0, false}, Case{255, 0, false}}) {
                Header h = original.replay.head;
                h.mode = c.mode;
                h.sim_rules = c.rules;
                h.engine_rules = 17;                                                              // (a head with its own rules number: the protocol table is not asked)
                ASSERT_TRUE(plays_here(h) == c.plays);
            }
        }
    } TEST_END();

    TEST_CASE("RP7.6 A Reader That Does Not Know The Mode Refuses A Match Of Mode 187 By Its Rules Number: It Skips Field 17, Reads Field 16 As 2 And Plays Rules 1 Only; An Id That Nobody Knows Yet (18) Is Skipped By This Reader Too, In A File Of Either Mode") {
        const Played p = play_scripted(tiny, 33, 120, 0x0F, sim::StartTeams{}, false, nullptr, k187);
        const Played original = play_scripted(tiny, 33, 120);
        // the reader of every release before the mode: it walks the fields, takes the ones it knows (here: the rules number, field 16) and skips the others, then plays only its own rules number
        const auto legacy_rules = [&](const std::vector<uint8_t>& file) -> uint16_t {
            const std::vector<HeadField> fields = fields_of(chunks_of(file)[0].payload);
            uint16_t rules = 0;
            for (const HeadField& f : fields) {
                if (f.id == 16) rules = static_cast<uint16_t>(f.bytes[0] | (f.bytes[1] << 8));
            }
            return rules;
        };
        const auto legacy_plays = [&](const std::vector<uint8_t>& file) { return legacy_rules(file) == 1; };       // (its kSimRules, and no mode to ask)
        ASSERT_TRUE(legacy_rules(p.file) == 2 && !legacy_plays(p.file));
        ASSERT_TRUE(legacy_rules(original.file) == 1 && legacy_plays(original.file));
        // so the file of a mode must not be made with rules 1 by any writer of this build: nothing in this build writes a mode with rules 1 (RP7.5), and a file that did not get its mode (field 17 lost) is refused
        // by this reader too, because rules 2 means "a mode" and the head does not say which
        ASSERT_TRUE(!decoded(file_with_head(p, [&](std::vector<HeadField>& f) { drop_field(f, 17); })).ok);
        // an id that no build knows yet is skipped: the head still reads and the match plays, whichever mode it is
        for (const Played* base : {&p, &original}) {
            const Decoded d = decoded(file_with_head(*base, [&](std::vector<HeadField>& f) { f.insert(f.begin() + 3, HeadField{18, {9, 9, 9}}); }));
            ASSERT_TRUE(d.ok && same(d.replay, base->replay));
            const Decoded last = decoded(file_with_head(*base, [&](std::vector<HeadField>& f) { f.push_back(HeadField{18, {}}); f.push_back(HeadField{99, {1}}); }));
            ASSERT_TRUE(last.ok && same(last.replay, base->replay) && plays_here(last.replay.head));
            const Outcome o = play(last.replay, tiny.level);
            ASSERT_TRUE(o.ok && o.hash == base->hash);
        }
    } TEST_END();

    // The reference match of game mode 187, the same way as the one above: its rules number is kSimRulesMode187, and the hash it ends in changes only with a new number for the mode
    constexpr ReferenceHash kReferenceHashes187[] = {{2, 0x309916fdf73aa5daull}};

    TEST_CASE("RP7.7 The Reference Match Of Game Mode 187 Ends In The Hash That Its Rules Number Says: The Rules Of 187 Cannot Change Without kSimRulesMode187; The Original's Reference Hash Is Untouched") {
        const Played p = play_scripted(tiny, 31, 1500, 0x0F, sim::StartTeams{}, false, nullptr, k187);
        ASSERT_TRUE(p.replay.commands.size() > 20);
        const ReferenceHash* line = nullptr;
        for (const ReferenceHash& r : kReferenceHashes187) {
            if (r.sim_rules == kSimRulesMode187) line = &r;
        }
        if (line == nullptr || line->hash != p.hash) std::cout << "\n    the 187 reference match ends in " << std::hex << p.hash << std::dec << " (kSimRulesMode187 " << kSimRulesMode187 << ")\n";
        ASSERT_TRUE(line != nullptr);
        ASSERT_EQ(line->hash, p.hash);
        const Outcome o = play(p.replay, tiny.level);
        ASSERT_TRUE(o.ok && o.hash == line->hash);
        // the original's line stays what it was, and its match ends there still (the 187 engine is not in its way)
        ASSERT_TRUE(kReferenceHashes[0].sim_rules == kSimRules && kReferenceHashes[0].hash == 0x7a96df7831c965e0ull);
        const Played original = play_scripted(tiny, 31, 1500);
        ASSERT_EQ(original.hash, 0x7a96df7831c965e0ull);
    } TEST_END();

    // ---- the snapshot of a match that still runs: the optional chunk `live` ----------------------------------------------------------

    TEST_CASE("RP8.1 A Snapshot Is An Incomplete Replay With A Lower Case Chunk `live` (The Turns Run When It Was Made): Read Back With Its Commands And Hashes, Its Turn Count The Largest Of The Last Command, The Last Hash And The Chunk; An Older Reader Skips The Chunk") {
        Replay s = synthetic_small();                                              // commands up to turn 301, three hashes (300 turns)
        s.complete = false;
        s.total_turns = 0;
        s.match_over = false;
        s.final_hash = 0;
        std::string error;
        const std::vector<uint8_t> bytes = encode_snapshot(s, 450, error);
        ASSERT_FALSE(bytes.empty());
        const std::vector<Chunk> chunks = chunks_of(bytes);
        ASSERT_TRUE(chunks.size() == 4 && chunks[0].tag == "HEAD" && chunks[1].tag == "CMDS" && chunks[2].tag == "hash" && chunks[3].tag == "live");
        ASSERT_EQ(chunks[3].payload, (std::vector<uint8_t>{0xC2, 0x01, 0x00, 0x00}));        // 450, little endian
        ASSERT_EQ(count_chunks(bytes, "ENDS"), 0u);
        Decoded d = decoded(bytes);
        ASSERT_TRUE(d.ok && !d.replay.complete && !d.replay.match_over && d.replay.final_hash == 0);
        ASSERT_EQ(d.replay.total_turns, 450u);
        ASSERT_TRUE(same_head(d.replay.head, s.head) && d.replay.hashes == s.hashes && d.replay.commands.size() == s.commands.size());
        for (size_t i = 0; i < s.commands.size(); ++i) ASSERT_TRUE(d.replay.commands[i].turn == s.commands[i].turn && d.replay.commands[i].command == s.commands[i].command);
        ASSERT_EQ(encode_snapshot(d.replay, 450, error), bytes);                   // (read and written again: the same bytes, and the same every time)
        ASSERT_EQ(encode_snapshot(s, 450, error), bytes);
        // the turn count is the largest of the three, and the writer refuses a turn count below the others
        ASSERT_EQ(decoded(encode_snapshot(s, 301, error)).replay.total_turns, 301u);     // = the last command (the hashes say 300)
        ASSERT_TRUE(encode_snapshot(s, 300, error).empty() && contains(error, "snapshot"));       // below the last command
        Replay hashed;                                                             // three hashes, no commands
        hashed.head = head_for(tiny, "TINY", 1, 0x03);
        hashed.hashes = {1, 2, 3};
        ASSERT_EQ(decoded(encode_snapshot(hashed, 300, error)).replay.total_turns, 300u);
        ASSERT_EQ(decoded(encode_snapshot(hashed, 380, error)).replay.total_turns, 380u);
        ASSERT_TRUE(encode_snapshot(hashed, 299, error).empty() && !error.empty());      // below the last hash
        Replay bare;
        bare.head = head_for(tiny, "TINY", 1, 0x03);
        ASSERT_EQ(decoded(encode_snapshot(bare, 0, error)).replay.total_turns, 0u);
        ASSERT_EQ(decoded(encode_snapshot(bare, kMaxTurns, error)).replay.total_turns, kMaxTurns);
        ASSERT_TRUE(encode_snapshot(bare, kMaxTurns + 1, error).empty() && !error.empty());
        // a replay with its end is not a snapshot
        Replay whole = synthetic_small();
        ASSERT_TRUE(whole.complete);
        ASSERT_TRUE(encode_snapshot(whole, 400, error).empty() && contains(error, "end"));
        // an older reader skips a lower case chunk it does not know: the same file with the chunk under another name is the file as it was before the chunk, which reads up to its last command and hash
        std::vector<Chunk> older = chunks;
        older[3].tag = "lixe";
        d = decoded(file_of(older));
        ASSERT_TRUE(d.ok && !d.replay.complete && d.replay.total_turns == 301 && d.replay.commands.size() == s.commands.size());
        ASSERT_TRUE(chunks[3].tag[0] >= 'a' && chunks[3].tag[0] <= 'z');                    // (lower case: not critical)
    } TEST_END();

    TEST_CASE("RP8.2 The Reader Is Strict About The Chunk `live`: Two Of Them, A Wrong Length, A Turn Count Below The Last Command Or The Last Hash Or Above A Match's Limit, And One In A File That Has Its ENDS Chunk Are Refused With The Chunk Named; Every Flipped Byte And Every Cut Of A Snapshot Is Found Or Is A Shorter Replay") {
        Replay s = synthetic_small();
        s.complete = false;
        std::string error;
        const std::vector<Chunk> base = chunks_of(encode_snapshot(s, 450, error));
        ASSERT_TRUE(base.size() == 4 && base[3].tag == "live");
        const auto turns_payload = [](uint32_t v) { return std::vector<uint8_t>{static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)}; };
        const auto refused_with = [&](const std::vector<Chunk>& chunks, const std::string& part) {
            const Decoded d = decoded(file_of(chunks));
            if (d.ok || !contains(d.error, part)) std::cout << "\n    reads: " << (d.ok ? "ok" : d.error) << "\n";
            return !d.ok && contains(d.error, part);
        };
        std::vector<Chunk> c = base;
        c.push_back(base[3]);
        ASSERT_TRUE(refused_with(c, "live: two"));
        for (const size_t length : {size_t{0}, size_t{1}, size_t{3}, size_t{5}, size_t{8}}) {
            c = base;
            c[3].payload.assign(length, 7);
            ASSERT_TRUE(refused_with(c, "live: the wrong length"));
        }
        c = base;
        c[3].payload = turns_payload(300);                                          // the last command is at turn 301
        ASSERT_TRUE(refused_with(c, "live"));
        c[3].payload = turns_payload(301);
        ASSERT_TRUE(decoded(file_of(c)).ok);                                        // (exactly the last command: fine)
        c = {base[0], base[2], base[3]};                                            // no commands, three hashes
        c[2].payload = turns_payload(299);
        ASSERT_TRUE(refused_with(c, "live"));
        c[2].payload = turns_payload(300);
        ASSERT_TRUE(decoded(file_of(c)).ok);
        c = base;
        c[3].payload = turns_payload(kMaxTurns + 1);
        ASSERT_TRUE(refused_with(c, "live: more turns"));
        c[3].payload = turns_payload(0xFFFFFFFFu);
        ASSERT_TRUE(refused_with(c, "live"));
        // a live chunk in a file that has its end: before the ENDS chunk and after it
        const std::vector<Chunk> ends = chunks_of(encoded(synthetic_small()));      // HEAD, CMDS, hash, ENDS
        c = ends;
        c.insert(c.end() - 1, base[3]);
        ASSERT_TRUE(refused_with(c, "live") && refused_with(c, "ENDS"));
        c = ends;
        c.push_back(base[3]);
        ASSERT_TRUE(refused_with(c, "live") && refused_with(c, "ENDS"));
        // any other chunk after the end is "bytes after the ENDS chunk": the chunk's name is said for ours only
        c = ends;
        c.push_back(Chunk{"zzzz", {}});
        const Decoded other = decoded(file_of(c));
        ASSERT_TRUE(!other.ok && contains(other.error, "after the ENDS") && !contains(other.error, "live"));
        // every flipped byte of a snapshot is found, and a cut one is refused or is a shorter replay of the same match
        const std::vector<uint8_t> bytes = encode_snapshot(s, 450, error);
        const Decoded whole = decoded(bytes);
        ASSERT_TRUE(whole.ok);
        for (size_t i = 0; i < bytes.size(); ++i) {
            std::vector<uint8_t> broken = bytes;
            broken[i] = static_cast<uint8_t>(broken[i] ^ 0x5Au);
            const Decoded d = decoded(broken);
            ASSERT_TRUE(d.ok ? !same(d.replay, whole.replay) : !d.error.empty());
        }
        const size_t head_end = 8 + 12 + base[0].payload.size();
        for (size_t length = 0; length < bytes.size(); ++length) {
            const Decoded d = decoded(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)));
            if (length < head_end) {
                ASSERT_FALSE(d.ok);
            } else {
                ASSERT_TRUE(d.ok && !d.replay.complete && d.replay.total_turns < 450u);      // (cut before the whole live chunk: it has not got the turns that the chunk says)
            }
        }
    } TEST_END();

    TEST_CASE("RP8.3 Recorder::snapshot: A Match That Runs Is Snapshotted Again And Again Without Changing The Recording; Each Snapshot Plays On The Real Map Up To Its Turns With Every Hash Right And Ends In The State The Match Was In, And Its Commands And Hashes Are The Start Of The Final File's; Nothing For A Recording That Failed, Is Finished Or Has Run No Turn") {
        SnapshotTap tap;
        tap.every = 90;
        const Played p = play_scripted(tiny, 31, 450, 0x0F, sim::StartTeams{}, false, &tap);
        const Played plain = play_scripted(tiny, 31, 450);
        ASSERT_FALSE(p.file.empty());
        ASSERT_EQ(p.file, plain.file);                                              // (a recorder that was asked for snapshots writes the same file: it is not changed)
        ASSERT_EQ(tap.files.size(), 5u);                                            // after 90, 180, 270, 360 and 450 turns
        for (size_t k = 0; k < tap.files.size(); ++k) {
            const uint32_t turns = tap.turns[k];
            ASSERT_EQ(turns, 90u * static_cast<uint32_t>(k + 1));
            ASSERT_FALSE(tap.files[k].empty());
            const Decoded d = decoded(tap.files[k]);
            ASSERT_TRUE(d.ok && !d.replay.complete);
            ASSERT_EQ(d.replay.total_turns, turns);                                 // (the snapshot's own count, whatever the last command or hash say)
            ASSERT_EQ(count_chunks(tap.files[k], "live"), 1u);
            ASSERT_EQ(count_chunks(tap.files[k], "ENDS"), 0u);
            // the start of the final file: the same head, the commands given before the snapshot's turn, the hashes taken by then
            ASSERT_TRUE(same_head(d.replay.head, p.replay.head));
            size_t expected = 0;
            while (expected < p.replay.commands.size() && p.replay.commands[expected].turn < turns) ++expected;
            ASSERT_EQ(d.replay.commands.size(), expected);
            for (size_t i = 0; i < expected; ++i) ASSERT_TRUE(d.replay.commands[i].turn == p.replay.commands[i].turn && d.replay.commands[i].command == p.replay.commands[i].command);
            ASSERT_EQ(d.replay.hashes.size(), static_cast<size_t>(turns / 100));
            ASSERT_TRUE(std::equal(d.replay.hashes.begin(), d.replay.hashes.end(), p.replay.hashes.begin()));
            // it plays on the real map up to its turns: every hash it holds is right, and the state is the one the match was in
            const Outcome o = play(d.replay, tiny.level);
            ASSERT_TRUE(o.ran && o.ok && !o.complete);
            ASSERT_EQ(o.turns, turns);
            ASSERT_EQ(o.hashes_checked, turns / 100);
            ASSERT_EQ(o.hash, tap.hashes[k]);
        }
        ASSERT_TRUE(decoded(tap.files.back()).replay.commands.size() == p.replay.commands.size());         // (the last snapshot is the whole match so far: all its commands)
        // a recording that cannot be a file, one that is finished and one with no turn give nothing, and say why
        sim::SimulationEngine engine;
        engine.init(tiny.level, 1, 0x0F);
        std::string error;
        Recorder fresh(head_for(tiny, "TINY", 1, 0x0F));
        ASSERT_TRUE(fresh.snapshot(error).empty() && contains(error, "no turn"));
        fresh.on_command(make_command(CommandType::Hatch, 0, 0, 0, {}));
        ASSERT_TRUE(fresh.snapshot(error).empty() && contains(error, "no turn"));    // (a command before the first tick is not a turn)
        engine.tick();
        fresh.on_tick(engine);
        const std::vector<uint8_t> one = fresh.snapshot(error);
        ASSERT_FALSE(one.empty());
        const Decoded first = decoded(one);
        ASSERT_TRUE(first.ok && !first.replay.complete && first.replay.total_turns == 1 && first.replay.commands.size() == 1);
        ASSERT_TRUE(fresh.turns() == 1 && fresh.commands() == 1 && fresh.failure().empty() && !fresh.finished());        // (it changed nothing)
        ASSERT_FALSE(fresh.finish(engine, error).empty());
        ASSERT_TRUE(fresh.snapshot(error).empty() && contains(error, "finished"));
        Recorder spoiled(head_for(tiny, "TINY", 1, 0x0F));
        engine.tick();
        spoiled.on_tick(engine);
        ASSERT_FALSE(spoiled.snapshot(error).empty());
        net::TurnMsg late;
        late.turn = 9;                                                              // a turn that was not seen: the recording is lost
        spoiled.on_turn(late, engine);
        ASSERT_FALSE(spoiled.failure().empty());
        ASSERT_TRUE(spoiled.snapshot(error).empty() && error == spoiled.failure());
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

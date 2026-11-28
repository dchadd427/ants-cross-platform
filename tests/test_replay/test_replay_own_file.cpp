// Tests of a replay file that a visitor opens on the website (docs/REPLAYS.md, "A file of your own"): the limit on the starting ants of a map that a file carries, whichever copy of the map is read (RP9.11), and the
// recording kept in tests/data, which the browser checks open and which plays here to its last hash (RP9.12). The mark of a heavy order rate in the head (RP9.13), and the recordings of tests/data
// given to the host's sequencer once more, which seals every order of them as it was (RP9.14). The cases of a map that a file carries that come before these (RP9.1 to RP9.10) are in test_replay.cpp.
#include "ants_net/netgame.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/session.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_test_paths.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"
#include "big_map_fixtures.hpp"

#if defined(_WIN32)
#include <process.h>
#define ANTS_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_GETPID getpid
#endif

using namespace ants;
using namespace ants::replay;

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

namespace fs = std::filesystem;

namespace {

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("ants_test_replay_own_file_" + std::to_string(ANTS_GETPID()) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    TEST_CASE("RP9.11 A Map That A File Carries Starts At Most kMaxEmbeddedStartAnts (8,000) Ants: One More Is Refused With Its Count Before Any Match Is Made, The Plants Are No Ants, Tens Of Thousands Cost No More Than Loading, A Map Of The Folder Is Never Counted") {
        Replay r;                                                                      // (the loaders read the map's name, the head's hash and the bytes that the file carries, nothing else)
        r.head.map_name = "TINY.LVL";
        const auto carrying = [&](const std::string& map) {
            Replay c = r;
            c.embedded_map.assign(map.begin(), map.end());
            c.head.map_hash = net::hash_bytes(c.embedded_map.data(), c.embedded_map.size());
            return c;
        };
        TempDir nowhere;
        const std::vector<std::string> dirs{nowhere.path.string()};
        std::string error;
        assets::LevelData level;
        ASSERT_EQ(kMaxEmbeddedStartAnts, 8000u);                                       // (the number that web/map_files.js MAX_EMBEDDED_ANTS keeps: tests/scripts/test_web_replay_open.py)
        const std::string at = tiny_with_ants({2000, 2000, 2000, 2000}), over = tiny_with_ants({2001, 2000, 2000, 2000});
        ASSERT_TRUE(!at.empty() && !over.empty());
        ASSERT_TRUE(load_map_or_embedded(carrying(at), dirs, level, error) && error.empty());
        ASSERT_FALSE(load_map_or_embedded(carrying(over), dirs, level, error));
        ASSERT_TRUE(contains(error, "8,001 ants") && contains(error, "8,000"));
        assets::LevelData planted;                                                     // (the plants of block 1, records that are no start marker, start no ant: 8,000 markers and 2,000 plants are not too many)
        ASSERT_TRUE(assets::LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(at.data()), at.size(), planted, nullptr, assets::LevelLimits::for_extended_maps()));
        for (int i = 0; i < 2000; ++i) planted.anthill_spawns.push_back(assets::AnthillSpawn{404, 1, 1, 255, 0});
        const assets::LevelWriteResult with_plants = assets::write_level(planted, assets::LevelLimits::for_extended_maps());
        ASSERT_TRUE(with_plants.ok && load_map_or_embedded(carrying(std::string(with_plants.bytes.begin(), with_plants.bytes.end())), dirs, level, error) && error.empty());
        const auto started = std::chrono::steady_clock::now();
        const std::string crowd = tiny_with_ants({16384, 16384, 16384, 16383});        // (a file of 65,535 ants, the most that block 1 holds: seconds to set up natively, minutes in a browser)
        ASSERT_FALSE(crowd.empty());
        ASSERT_FALSE(load_embedded_map(carrying(crowd), level, error));
        ASSERT_TRUE(contains(error, "65,535 ants"));
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
        {   // the folder's own file is the server's: a map of it is never counted
            std::ofstream big(nowhere.path / "BIG.LVL", std::ios::binary);
            big << crowd;
        }
        Replay folder = carrying(crowd);
        folder.head.map_name = "BIG.LVL";
        folder.embedded_map.clear();
        ASSERT_TRUE(load_map_or_embedded(folder, dirs, level, error) && error.empty());
        folder.embedded_map = {1, 2, 3}; ASSERT_TRUE(load_map_or_embedded(folder, dirs, level, error) && error.empty());      // (bytes that are not the head's map, in a Replay made by hand: it carries no map, so the folder's copy is not counted either)
    } TEST_END();

    TEST_CASE("RP9.12 The Recording Kept In tests/data (replay_bots_tiny.antsrep: 7,205 Turns Of Four Computer Players On TINY, The Browser Checks' File) Plays Here To Its Last Hash: A Change That Strands It Fails Here And Not Only In The Opt-In Browser Check") {
        const std::vector<uint8_t> bytes = read_bytes(std::string(TEST_DATA_DIR) + "/replay_bots_tiny.antsrep");
        ASSERT_FALSE(bytes.empty());
        Replay replay;
        std::string error;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), replay, error));
        ASSERT_TRUE(plays_here(replay.head) && replay.complete && replay.total_turns == 7205);
        assets::LevelData level;
        ASSERT_TRUE(load_map(replay.head, std::string(ORIGINAL_ASSETS_DIR) + "/Maps", level, error));
        const Outcome o = play(replay, level);
        ASSERT_TRUE(o.ok && o.ran && o.complete && o.hash == replay.final_hash && o.hashes_checked == 72);
    } TEST_END();

    TEST_CASE("RP9.13 The Head's Mark Of A Heavy Order Rate (Field 18, The Seats Whose Orders Went Over Their Budget): Written Only When A Seat Has It, Read Back, Refused For A Seat That Does Not Play Or A Field That Is Empty, Skipped By A Reader That Does Not Know It; A File Without It Is Byte For Byte What It Was") {
        const std::vector<uint8_t> bytes = read_bytes(std::string(TEST_DATA_DIR) + "/replay_bots_tiny.antsrep");
        Replay replay;
        std::string error;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), replay, error));
        ASSERT_EQ(replay.head.heavy_orders, 0);
        ASSERT_TRUE(encode(replay, error) == bytes);                                   // (no mark: the file is what it was, so every recording of honest play is)
        Replay marked = replay;
        marked.head.heavy_orders = 0x06;                                               // seats 1 and 2
        const std::vector<uint8_t> with = encode(marked, error);
        ASSERT_FALSE(with.empty());
        ASSERT_TRUE(with.size() > bytes.size() && with.size() <= bytes.size() + 8);    // (the field: an id, a length, a byte)
        Replay back;
        ASSERT_TRUE(decode(with.data(), with.size(), back, error));
        ASSERT_EQ(back.head.heavy_orders, 0x06);
        ASSERT_TRUE(back.commands.size() == replay.commands.size() && back.final_hash == replay.final_hash);        // (the match is the same match)
        Replay stranger = replay;
        stranger.head.roster = 0x03;                                                   // a seat that does not play cannot have the mark
        stranger.head.heavy_orders = 0x04;
        ASSERT_TRUE(encode(stranger, error).empty() && contains(error, "does not play"));
        stranger.head.heavy_orders = 0x00;
        // a field with nothing in it, or a seat that does not exist, is refused with the field named; the head is the first chunk, so its length and its checksum are rewritten for the test
        const auto refuse_with = [&](const std::vector<uint8_t>& field_bytes) {
            const auto u32_at = [&](size_t at) { return static_cast<uint32_t>(with[at]) | (static_cast<uint32_t>(with[at + 1]) << 8) | (static_cast<uint32_t>(with[at + 2]) << 16) | (static_cast<uint32_t>(with[at + 3]) << 24); };
            const auto put_u32 = [](std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF)); };
            const uint32_t head_length = u32_at(12);                                   // (the magic is 8 bytes, then the tag HEAD, then the length)
            std::vector<uint8_t> payload(with.begin() + 16, with.begin() + 16 + head_length);
            payload.resize(payload.size() - 3);                                        // (field 18 is the last three bytes of the head)
            payload.insert(payload.end(), field_bytes.begin(), field_bytes.end());
            std::vector<uint8_t> checked = {'H', 'E', 'A', 'D'};
            checked.insert(checked.end(), payload.begin(), payload.end());
            std::vector<uint8_t> whole(with.begin(), with.begin() + 8);
            whole.insert(whole.end(), {'H', 'E', 'A', 'D'});
            put_u32(whole, static_cast<uint32_t>(payload.size()));
            whole.insert(whole.end(), payload.begin(), payload.end());
            put_u32(whole, crc32(checked.data(), checked.size()));
            whole.insert(whole.end(), with.begin() + 16 + head_length + 4, with.end());        // (the chunks after the head)
            Replay out;
            std::string why;
            const bool ok = decode(whole.data(), whole.size(), out, why);
            return std::make_pair(ok, why);
        };
        ASSERT_TRUE(refuse_with({18, 1, 0x06}).first);                                 // (the helper rebuilds the head right: the same field read back)
        const auto empty_field = refuse_with({18, 1, 0x00});
        ASSERT_TRUE(!empty_field.first && contains(empty_field.second, "field 18"));
        const auto beyond = refuse_with({18, 1, 0x30});
        ASSERT_TRUE(!beyond.first && contains(beyond.second, "field 18"));
        const auto longer = refuse_with({18, 2, 0x06, 0x00});
        ASSERT_TRUE(!longer.first && contains(longer.second, "field 18"));
        // the recorder carries the mark into the head it writes
        Header head = replay.head;
        head.heavy_orders = 0;
        Recorder recorder(head);
        recorder.set_heavy_orders(0x02);
        ASSERT_EQ(recorder.replay().head.heavy_orders, 0x02);
    } TEST_END();

    // The longest match part that a file may have is 16 MiB (replay::kMaxFileBytes: no one seat can fill it under the server's budget of orders): a recording that large is written, read back, and played to the hash it had.
    TEST_CASE("RP9.16 A Recording Of Almost 16 MiB (The Kept Recording Of TINY And 122,000 More Commands, Each Of 32 Ants That Never Were, All In Its Last Turn) Is Written, Read Back, Re-Written The Same, Played To The Same Last Hash, And Is Taken By A Recorder With The Default Limit; One That Passes The Limit Is Not Taken")
    {
        const std::vector<uint8_t> bytes = read_bytes(std::string(TEST_DATA_DIR) + "/replay_bots_tiny.antsrep");
        Replay replay;
        std::string error;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), replay, error));
        ASSERT_TRUE(kMaxFileBytes == (size_t{16} << 20) && kMaxFileBytesWithMap == (size_t{17} << 20) + kChunkFrameBytes);
        Replay big = replay;
        const uint32_t last_turn = big.commands.empty() ? 0u : big.commands.back().turn;
        sim::Command filler;
        filler.type = sim::CommandType::GroupMove;
        filler.issuer = 1;
        filler.tile_x = 5;
        filler.tile_y = 5;
        filler.ants.assign(32, 4000000u);                                              // (ants that never were: the engine passes over the order, so the match is the match)
        for (int i = 0; i < 122000; ++i) big.commands.push_back(TimedCommand{last_turn, filler});
        const std::vector<uint8_t> file = encode(big, error);
        ASSERT_TRUE(!file.empty());
        ASSERT_TRUE(file.size() > (size_t{15} << 20) && file.size() <= kMaxFileBytes);
        Replay back;
        ASSERT_TRUE(decode(file.data(), file.size(), back, error));
        ASSERT_TRUE(back.commands.size() == big.commands.size() && back.final_hash == replay.final_hash);
        ASSERT_TRUE(encode(back, error) == file);                                      // (the same bytes each time, whatever the length)
        assets::LevelData level;
        ASSERT_TRUE(load_map(back.head, std::string(ORIGINAL_ASSETS_DIR) + "/Maps", level, error));
        const Outcome o = play(back, level);
        ASSERT_TRUE(o.ok && o.ran && o.complete && o.hash == replay.final_hash);       // (a viewer plays it to the last hash of the match as it was)
        // one more filling of the same kind is no file: the writer refuses what the reader would
        for (int i = 0; i < 2000; ++i) big.commands.push_back(TimedCommand{last_turn, filler});
        const std::vector<uint8_t> past = encode(big, error);
        ASSERT_TRUE(past.empty() && contains(error, "16384 KiB"));
        // a recorder with the default limit takes the commands up to it and not past it
        Header head = replay.head;
        head.heavy_orders = 0;
        Recorder recorder(head);
        int taken = 0;
        while (recorder.failure().empty() && taken < 130000) {
            recorder.on_command(filler);
            ++taken;
        }
        ASSERT_TRUE(recorder.over_size_limit() && contains(recorder.failure(), "size limit (16 MiB)"));
        ASSERT_TRUE(taken > 122000 && taken < 123500);                                 // (16 MiB of 137 bytes)
    } TEST_END();

    // What a recording that passed its size limit can still say: whose commands filled it (the history row of the match names that seat).
    TEST_CASE("RP9.15 The Recorder Counts The Bytes Of Commands That Each Seat Sent; A Recording That Passes Its Size Limit Names The Seat That Sent The Most (The Lowest Of A Tie, None When No Seat Sent Any); The System's Commands Are Nobody's; Another Failure Does Not Say It Was The Size; The Counts Stop Where The Recording Fails") {
        const std::vector<uint8_t> bytes = read_bytes(std::string(TEST_DATA_DIR) + "/replay_bots_tiny.antsrep");
        Replay replay;
        std::string error;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), replay, error));
        Header head = replay.head;
        head.heavy_orders = 0;
        const auto group = [](uint8_t seat, size_t ants) {
            sim::Command c;
            c.type = sim::CommandType::GroupMove;
            c.issuer = seat;
            c.tile_x = 3;
            c.tile_y = 4;
            for (size_t i = 0; i < ants; ++i) c.ants.push_back(static_cast<uint32_t>(10 + i));
            return c;
        };
        {   // every seat's bytes are counted as the limit counts them (a byte for the gap, the header, four bytes an ant), the drop of a peer is nobody's, and nothing is named before a seat has sent something
            Recorder recorder(head, 3000);
            ASSERT_EQ(recorder.largest_sender(), -1);
            recorder.on_command(group(1, 2));
            recorder.on_command(group(2, 10));
            recorder.on_command(group(1, 1));
            sim::Command drop;
            drop.type = sim::CommandType::Drop;
            drop.issuer = 3;
            recorder.on_command(drop);
            ASSERT_TRUE(recorder.sent_bytes(0) == 0 && recorder.sent_bytes(1) == 30 && recorder.sent_bytes(2) == 49 && recorder.sent_bytes(3) == 0 && recorder.sent_bytes(9) == 0);
            ASSERT_EQ(recorder.largest_sender(), 2);
            ASSERT_TRUE(recorder.failure().empty() && !recorder.over_size_limit());
        }
        {   // the same bytes from two seats: the lower seat is named
            Recorder recorder(head, 3000);
            recorder.on_command(group(2, 4));
            recorder.on_command(group(0, 4));
            ASSERT_TRUE(recorder.sent_bytes(0) == recorder.sent_bytes(2) && recorder.sent_bytes(0) > 0);
            ASSERT_EQ(recorder.largest_sender(), 0);
        }
        {   // a flood passes the limit: the recording is lost, it says that it was the size, and which seat sent the most (the one that did the flooding, not the seats that sent a few orders before it)
            Recorder recorder(head, 1000);
            for (int i = 0; i < 6; ++i) recorder.on_command(group(static_cast<uint8_t>(i % 2), 3));          // (seats 0 and 1, 21 bytes each time: 63 bytes each)
            for (int i = 0; i < 20 && recorder.failure().empty(); ++i) recorder.on_command(group(3, 32));   // (137 bytes each: the seventh of them passes 1,000)
            ASSERT_TRUE(!recorder.failure().empty() && recorder.over_size_limit());
            ASSERT_EQ(recorder.largest_sender(), 3);
            const size_t flooder = recorder.sent_bytes(3);
            ASSERT_TRUE(flooder >= 137 * 6 && flooder <= 137 * 7);                                         // (the command that passed the limit is counted too)
            recorder.on_command(group(0, 32));                                                              // (what comes after the failure changes nothing)
            recorder.on_command(group(3, 32));
            ASSERT_TRUE(recorder.sent_bytes(3) == flooder && recorder.sent_bytes(0) == 63 && recorder.largest_sender() == 3);
        }
        {   // a recording that failed for another reason is not a recording that was too big
            Recorder recorder(head, 3000);
            recorder.on_command(group(1, 2));
            sim::Command odd;
            odd.type = sim::CommandType::Hatch;
            odd.issuer = 1;
            odd.ants.push_back(5);                                                                          // (a command with no ant list that names ants: the file could not say what was played)
            recorder.on_command(odd);
            ASSERT_TRUE(!recorder.failure().empty() && !recorder.over_size_limit());
        }
    } TEST_END();

    // The recordings of tests/data are honest play (computer players of the hardest level among them). Given to the host's sequencer again, turn by turn, the engine of the match deciding what the host would ask
    // of it, every order is taken, none is changed and none is over the budget: the turns that are sealed are the recorded ones.
    TEST_CASE("RP9.14 The Orders Of The Recordings In tests/data, Given Once More To The Host's Sequencer Turn By Turn (Its Vetter Asking The Engine Of The Match), Are All Sealed As They Were: None Is Changed, None Is Dropped For The Budget, And The Turns Are The Recorded Ones") {
        for (const char* name : {"replay_bots_tiny.antsrep", "replay_187_rules6.antsrep"}) {
            const std::vector<uint8_t> bytes = read_bytes(std::string(TEST_DATA_DIR) + "/" + name);
            ASSERT_FALSE(bytes.empty());
            Replay replay;
            std::string error;
            ASSERT_TRUE(decode(bytes.data(), bytes.size(), replay, error));
            assets::LevelData level;
            ASSERT_TRUE(load_map(replay.head, std::string(ORIGINAL_ASSETS_DIR) + "/Maps", level, error));
            net::Sequencer seq;
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) seq.set_active(seat, (replay.head.roster >> seat) & 1u);
            std::vector<sim::Command> given;
            std::vector<sim::Command> sealed;
            uint32_t changed = 0;
            uint32_t refused = 0;
            Hooks hooks;
            hooks.before_command = [&](uint32_t, const sim::Command& c, sim::SimulationEngine& engine) {
                sim::Command copy = c;
                if (!net::HostSession::vet_ants(engine, c.issuer, copy) || copy != c) ++changed;
                if (seq.submit_checked(c.issuer, copy) != net::Submit::Queued) ++refused;
                given.push_back(c);
            };
            hooks.after_tick = [&](uint32_t, sim::SimulationEngine&) {
                for (const sim::Command& c : seq.seal().commands) sealed.push_back(c);
            };
            const Outcome o = play(replay, level, hooks);
            ASSERT_TRUE(o.ok);
            for (const sim::Command& c : seq.seal().commands) sealed.push_back(c);       // (a Quit after the last tick)
            ASSERT_TRUE(given.size() > 40);
            ASSERT_EQ(changed, 0u);
            ASSERT_EQ(refused, 0u);
            ASSERT_EQ(seq.heavy_seats(), 0);
            ASSERT_TRUE(sealed == given);
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

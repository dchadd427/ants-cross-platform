// Tests of a replay file that a visitor opens on the website (docs/REPLAYS.md, "A file of your own"): the limit on the starting ants of a map that a file carries, whichever copy of the map is read (RP9.11), and the
// recording kept in tests/data, which the browser checks open and which plays here to its last hash (RP9.12). The cases of a map that a file carries that come before these (RP9.1 to RP9.10) are in test_replay.cpp.
#include "ants_net/netgame.hpp"
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
#include "../test_server/big_map_fixtures.hpp"

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

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

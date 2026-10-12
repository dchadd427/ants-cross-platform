// Tests of the players' maps in a match of the network (src/ants_app/application_maps.cpp, include/ants_app/map_fetch.hpp): a headless Application joins a room whose map its maps folder does not hold (the
// web build's case), a fake MapFetcher plays the page's part (web/map_files.js is checked by tests/scripts/web_map_files_check.js), and the other machine is a bare NetGame over loopback sockets. The
// match must not load before the file is here, must not start for a machine that cannot have it or has another file, and must be the same match on both machines when it does.
#include "ants_app/application.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::app;
namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name.substr(0, 100) << " ... " << std::flush;
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

namespace {

const char* const kCommunity = "Test Players Map (1).lvl";               // a name with spaces and brackets, as the community's have

// Two folders in a place of this process alone: the host's (the originals and the players' map) and the application's (the originals only, what the web build's file system holds)
class Folders {
public:
    Folders() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000 && root_.empty(); ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_map_fetch_" + std::to_string(static_cast<long>(getpid())) + "_" + suffix);
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec) root_ = candidate;
        }
        if (root_.empty()) return;
        std::error_code ec;
        fs::create_directories(host(), ec);
        fs::create_directories(guest(), ec);
        const fs::path originals = fs::path(ORIGINAL_ASSETS_DIR) / "Maps";
        for (fs::directory_iterator it(originals, ec), end; !ec && it != end; it.increment(ec)) {
            fs::copy_file(it->path(), host() / it->path().filename(), ec);
            fs::copy_file(it->path(), guest() / it->path().filename(), ec);
        }
        fs::copy_file(originals / "SMALL.LVL", host() / kCommunity, ec);
        fs::copy_file(originals / "GAUNTLET.LVL", elsewhere(), ec);       // another file, for the test that serves the wrong one
    }
    ~Folders() {
        std::error_code ec;
        if (!root_.empty()) fs::remove_all(root_, ec);
    }
    Folders(const Folders&) = delete;
    Folders& operator=(const Folders&) = delete;
    bool ok() const { return !root_.empty() && fs::exists(host() / kCommunity); }
    fs::path host() const { return root_ / "host"; }
    fs::path guest() const { return root_ / "guest"; }
    fs::path elsewhere() const { return root_ / "other.lvl"; }

private:
    fs::path root_;
};

// The page's part: asked for a map, it says Pending for `delay` polls, then copies the host's file (or another) into the folder, or fails
class FakeFetcher final : public MapFetcher {
public:
    FakeFetcher(fs::path source, int delay) : source_(std::move(source)), delay_(delay) {}
    struct Ask {
        std::string folder;
        std::string name;
        uint64_t hash;
    };
    std::vector<Ask> asks;
    int polls = 0;
    std::string fail;                                   // not empty: every map fails with this text
    bool wrong_file = false;                            // Ready, but the file in the folder is another one

    void request(const std::string& folder, const std::string& name, uint64_t hash) override {
        asks.push_back(Ask{folder, name, hash});
        if (hash != 0) left_ = delay_;
    }
    Status status(const std::string& name) override {
        ++polls;
        Status s;
        if (left_ > 0) {
            --left_;
            return s;
        }
        if (!fail.empty()) {
            s.state = State::Failed;
            s.message = fail;
            return s;
        }
        std::error_code ec;
        fs::copy_file(source_, fs::path(asks.back().folder) / name, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            s.state = State::Failed;
            s.message = "no copy";
            return s;
        }
        s.state = State::Ready;
        return s;
    }
    void set_source(fs::path source) { source_ = std::move(source); }

private:
    fs::path source_;
    int delay_;
    int left_ = 0;
};

// The other machine: a simulation and a NetGame, loading from its own folder
struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    uint32_t now{1000};
    fs::path folder;
    int starts = 0;
    int cancels = 0;
    Peer() { net.set_discovery(0); }
    void update() {
        net.update(now);
        for (const auto& ev : net.take_events()) {
            if (ev.type == net::NetGame::Event::Type::Cancelled) ++cancels;
            if (ev.type != net::NetGame::Event::Type::StartRequested) continue;
            ++starts;
            const net::StartMsg& s = net.start_info();
            assets::LevelData level;
            uint64_t hash = 0;
            const std::string path = (folder / s.map_name).string();
            const bool ok = level.load_lvl(path) && net::hash_file(path, hash) && hash == s.map_hash;
            if (ok) {
                sim.set_fog_of_war_enabled(s.fog);
                sim.set_game_mode(s.game_mode());
                sim.init(level, s.seed, s.roster);
                sim::apply_start_teams(sim, s.teams());
            }
            net.report_loaded(ok);
        }
    }
};

struct Duo {
    Application& app;
    Peer& peer;
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            app.pump_network(0.010f);
            app.update_simulation(0.010f);
            peer.now += 10;
            peer.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {       // the game clock is virtual but the sockets are real: a late kernel gets real time, with the game clock standing still
            app.pump_network(0.0f);
            peer.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

// The host (a bare machine, Alice, seat 0) holds a room on `map`; the application joins as Bob with `fetcher` and the guest folder as its maps folder. True once both are in the room.
bool join_room(Application& app, Peer& host, Duo*& duo, std::unique_ptr<Duo>& owner, const Folders& dirs, std::unique_ptr<FakeFetcher> fetcher, const std::string& map) {
    host.folder = dirs.host();
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map(map);
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;
    cfg.maps_dir = dirs.guest().string();
    cfg.net_role = ApplicationConfig::NetRole::Join;
    cfg.net_address = "127.0.0.1";
    cfg.net_port = host.net.listen_port();
    cfg.player_name = "Bob";
    app.set_map_fetcher(std::move(fetcher));
    if (!app.init(cfg)) return false;
    owner = std::make_unique<Duo>(Duo{app, host});
    duo = owner.get();
    return duo->until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start() && app.net()->room().map_name == map; }, 8000);
}

bool start(Peer& host, const std::string& map, uint32_t seed = 77) {
    uint64_t hash = 0;
    return net::hash_file((host.folder / map).string(), hash) && host.net.start_match(seed, hash);
}

uint64_t hash_of(const fs::path& file) {
    uint64_t h = 0;
    net::hash_file(file.string(), h);
    return h;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=== The players' maps in a match of the network ===\n";

    TEST_CASE("MF1.1 A map that the folder does not hold is asked for as soon as the room names it, then again with the Start's hash; the match loads when the file is here and is the host's match") {
        Folders dirs;
        ASSERT_TRUE(dirs.ok());
        Application app;
        Peer host;
        std::unique_ptr<Duo> owner;
        Duo* duo = nullptr;
        auto fake = std::make_unique<FakeFetcher>(dirs.host() / kCommunity, 40);
        FakeFetcher* fetcher = fake.get();
        ASSERT_TRUE(join_room(app, host, duo, owner, dirs, std::move(fake), kCommunity));
        ASSERT_FALSE(fetcher->asks.empty());                                          // (ahead of the Start: the room's map, no hash yet)
        ASSERT_EQ(fetcher->asks.front().name, std::string(kCommunity));
        ASSERT_EQ(fetcher->asks.front().hash, 0u);
        ASSERT_EQ(fetcher->asks.front().folder, dirs.guest().string());
        const size_t before = fetcher->asks.size();
        duo->step(300);
        ASSERT_EQ(fetcher->asks.size(), before);                                       // (once, not once a frame)
        ASSERT_FALSE(fs::exists(dirs.guest() / kCommunity));
        ASSERT_TRUE(start(host, kCommunity));
        ASSERT_TRUE(duo->until([&]() { return fetcher->asks.size() > before; }, 4000));
        ASSERT_EQ(fetcher->asks.back().hash, hash_of(dirs.host() / kCommunity));         // (the Start's hash, for the page to check)
        duo->step(100);
        ASSERT_TRUE(app.state() != AppState::Playing && app.net()->phase() == net::NetGame::Phase::Loading);       // (the file is not here yet: the machine waits, the host waits for it)
        ASSERT_TRUE(duo->until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(host.cancels, 0);
        ASSERT_TRUE(fs::exists(dirs.guest() / kCommunity));
        for (int i = 0; i < 400 && app.sim().current_tick() != host.sim.current_tick(); ++i) duo->step(10);
        ASSERT_EQ(app.sim().current_tick(), host.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());                   // the same match
        app.shutdown();
    } TEST_END();

    TEST_CASE("MF1.2 A map that cannot be had does not start for that machine: the start is cancelled for everybody, the reason is on the status line, and the next START tries again") {
        Folders dirs;
        ASSERT_TRUE(dirs.ok());
        Application app;
        Peer host;
        std::unique_ptr<Duo> owner;
        Duo* duo = nullptr;
        auto fake = std::make_unique<FakeFetcher>(dirs.host() / kCommunity, 5);
        FakeFetcher* fetcher = fake.get();
        fetcher->fail = "The map is not on the server (it may have been taken off).";
        ASSERT_TRUE(join_room(app, host, duo, owner, dirs, std::move(fake), kCommunity));
        ASSERT_TRUE(start(host, kCommunity));
        ASSERT_TRUE(duo->until([&]() { return host.cancels > 0 && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(app.state() != AppState::Playing);
        ASSERT_TRUE(host.net.phase() == net::NetGame::Phase::Room);
        ASSERT_EQ(app.net()->status_text(), fetcher->fail);                              // (the page's words, not "not found on your computer")
        ASSERT_FALSE(fs::exists(dirs.guest() / kCommunity));
        // the network comes back: the leader's next START is the retry (the fetcher is asked again, with the hash)
        fetcher->fail.clear();
        const size_t asked = fetcher->asks.size();
        duo->step(2500);                                                                  // (the room backs off a moment after a cancel)
        ASSERT_TRUE(start(host, kCommunity));
        ASSERT_TRUE(duo->until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 10000));
        ASSERT_GT(fetcher->asks.size(), asked);
        app.shutdown();
    } TEST_END();

    TEST_CASE("MF1.3 A file that is not the host's is never played: the hash of the Start decides, whatever the page said") {
        Folders dirs;
        ASSERT_TRUE(dirs.ok());
        Application app;
        Peer host;
        std::unique_ptr<Duo> owner;
        Duo* duo = nullptr;
        auto fake = std::make_unique<FakeFetcher>(dirs.elsewhere(), 3);                  // (says Ready, but wrote another map under the name)
        ASSERT_TRUE(join_room(app, host, duo, owner, dirs, std::move(fake), kCommunity));
        ASSERT_TRUE(start(host, kCommunity));
        ASSERT_TRUE(duo->until([&]() { return host.cancels > 0 && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(app.state() != AppState::Playing);
        ASSERT_TRUE(host.net.phase() == net::NetGame::Phase::Room);
        app.shutdown();
    } TEST_END();

    TEST_CASE("MF1.4 A map of the folder (an original) is loaded at once and the fetcher is never asked") {
        Folders dirs;
        ASSERT_TRUE(dirs.ok());
        Application app;
        Peer host;
        std::unique_ptr<Duo> owner;
        Duo* duo = nullptr;
        auto fake = std::make_unique<FakeFetcher>(dirs.host() / kCommunity, 40);
        FakeFetcher* fetcher = fake.get();
        ASSERT_TRUE(join_room(app, host, duo, owner, dirs, std::move(fake), "TINY.LVL"));
        ASSERT_TRUE(start(host, "TINY.LVL"));
        ASSERT_TRUE(duo->until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(fetcher->asks.empty());
        ASSERT_EQ(fetcher->polls, 0);
        app.shutdown();
    } TEST_END();

    TEST_CASE("MF1.5 A start that the host cancels while the file is on its way is dropped: the machine goes back to the room and the late file loads nothing") {
        Folders dirs;
        ASSERT_TRUE(dirs.ok());
        Application app;
        Peer host;
        std::unique_ptr<Duo> owner;
        Duo* duo = nullptr;
        auto fake = std::make_unique<FakeFetcher>(dirs.host() / kCommunity, 100000);      // (never arrives)
        ASSERT_TRUE(join_room(app, host, duo, owner, dirs, std::move(fake), kCommunity));
        ASSERT_TRUE(start(host, kCommunity));
        ASSERT_TRUE(duo->until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading; }, 4000));
        host.net.leave();                                                                 // the host goes: the start is over for this machine
        ASSERT_TRUE(duo->until([&]() { return app.net()->phase() != net::NetGame::Phase::Loading; }, 8000));
        ASSERT_TRUE(app.state() != AppState::Playing);
        app.shutdown();
    } TEST_END();


    TEST_CASE("MF2.1 The head of a recording names the map as tests/data/replay_head.hex holds it byte for byte (the page reads the map and its hash from there before the game starts: tests/scripts/web_map_files_check.js reads the same file)") {
        replay::Replay r;
        r.head.game_version = "v0.0.0";
        r.head.build_id = "test";
        r.head.venue = "network game";
        r.head.map_name = kCommunity;
        r.head.map_hash = 0x0123456789ABCDEFull;
        r.head.seed = 5;
        r.head.roster = 0x03;
        r.head.names[0] = "Ann";
        r.head.names[1] = "Bob";
        std::string error;
        const std::vector<uint8_t> bytes = replay::encode(r, error);
        ASSERT_FALSE(bytes.empty());
        std::string hex;
        for (const uint8_t b : bytes) {
            char two[3];
            std::snprintf(two, sizeof two, "%02x", static_cast<unsigned>(b));
            hex += two;
        }
        if (std::getenv("ANTS_WRITE_REPLAY_HEAD") != nullptr) std::cout << hex << "\n";       // the file again after the layout changed: redirect this output to tests/data/replay_head.hex (the first line is the header)
        std::ifstream in(std::string(TEST_DATA_DIR) + "/replay_head.hex");
        std::string line, last;
        while (std::getline(in, line)) {
            if (!line.empty() && line[0] != '#') last = line;
        }
        ASSERT_EQ(last, hex);
    } TEST_END();

    std::cout << "\nplayers' maps application tests: " << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

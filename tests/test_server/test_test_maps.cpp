// Tests of the test maps of the Map Maker's "With other players" (TestMapStore and TestMapApi, docs/MAP_SUBMISSIONS.md "Test maps"): the one route of the test-map door and what it refuses, the canonical
// file, the limits by address and for all, the checking window, the store's caps (count, per address, bytes, six hours), the names, the 65,536-cell limit, that the same bytes get the same verdict as at
// the real submissions door, and that nothing here needs a database, an account or the submissions door. Requests are made by hand: no sockets. (The real program, with no database and the submissions door
// off, is tests/scripts/test_maps_server_check.py; the nginx block is tests/scripts/test_nginx_test_maps.py.)
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "../common/test_assert.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/online_limits.hpp"
#include "ants_server/submissions.hpp"
#include "ants_server/test_maps.hpp"
#include "ants_server/test_maps_http.hpp"
#include "ants_server/test_maps_rooms.hpp"
#include "big_map_fixtures.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
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

// An assertion that says which input failed
#define ASSERT_MSG(cond, msg) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n    " << (msg) << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

namespace core = ants::assets::lvl_core;
using ctl::HttpRequest;
using ctl::HttpResponse;

std::string tiny() {
    std::ifstream in(fs::path(maps_dir()) / "TINY.LVL", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// TINY with the description `text` written as it is (a Classic map holds 30 bytes)
std::string tiny_described(const std::string& text) {
    const std::string base = tiny();
    ants::assets::LevelData t;
    if (!ants::assets::LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(base.data()), base.size(), t, nullptr, ants::assets::LevelLimits::for_new_maps())) return std::string();
    t.description = text;
    const ants::assets::LevelWriteResult w = ants::assets::write_level(t, ants::assets::LevelLimits::for_new_maps());
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

size_t files_in(const fs::path& dir) {
    size_t n = 0;
    for (const auto& entry : fs::directory_iterator(dir)) n += entry.is_regular_file() ? 1u : 0u;
    return n;
}

// The test-map door with its store, as the server makes them: a folder and nothing else (no database, no accounts, no submissions store)
struct Door {
    TempDir dir;
    TestMapConfig config;
    std::unique_ptr<TestMapStore> store;
    std::unique_ptr<TestMapApi> api;
    int64_t now{kT0};
    uint32_t now_ms{1000};
    int refreshed{0};

    explicit Door(const std::function<void(TestMapApiConfig&)>& tune = nullptr, const std::function<void(TestMapConfig&)>& tune_store = nullptr) {
        config.dir = (dir.path / "test-maps").string();
        config.min_free_bytes = 1;
        if (tune_store) tune_store(config);
        store = std::make_unique<TestMapStore>(config);
        std::string why;
        store->open(why);
        TestMapApiConfig ac;
        if (tune) tune(ac);
        api = std::make_unique<TestMapApi>(*store, ac);
        api->set_on_changed([this] { ++refreshed; });
    }

    HttpResponse call(const HttpRequest& r) { return api->handle(r, now, now_ms); }

    HttpRequest req(const std::string& method, const std::string& path, const std::string& query = "", const std::map<std::string, std::string>& headers = {}, const std::string& body = "") {
        HttpRequest r;
        r.method = method;
        r.path = path;
        r.query = query;
        r.headers.insert(headers.begin(), headers.end());
        r.body = body;
        r.peer = "172.18.0.4";
        return r;
    }

    // A test upload (POST /test-maps) of `bytes` as a map of `kind`
    HttpResponse upload(const std::string& bytes, const std::string& name = "Popcorn", const std::string& address = "203.0.113.20", const std::string& kind = "classic") {
        return call(req("POST", "/test-maps", "kind=" + kind + "&name=" + name, {{"content-type", "application/octet-stream"}, {"x-client-address", address}}, bytes));
    }

    // The same, to the store without the door: what the store keeps (the door's own limits are not asked). Moves the clock `later` seconds first.
    TestMapOutcome keep(const std::string& address, int64_t later = 0, const std::string& bytes = std::string(), const std::string& name = "Popcorn") {
        now += later;
        TestMapInput in;
        in.kind = "classic";
        in.name = name;
        in.bytes = bytes.empty() ? tiny() : bytes;
        in.address = address;
        return store->submit(in, now);
    }
};

}  // namespace

int main() {
    std::cout << "=======================================================\n TEST MAPS: THE DOOR, THE STORE AND THEIR LIMITS\n=======================================================\n";

    TEST_CASE("TM1.1 POST /test-maps saves an unlisted test map for a room, resolves in MapStore::find and test_map while staying out of list_json, tells the lobby once, and expires after kTestMapTtlS") {
        Door t;
        HttpRequest q = t.req("POST", "/test-maps", "kind=classic&name=Popcorn%20Remix", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}}, tiny());
        HttpResponse r = t.call(q);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(t.refreshed, 1);
        const ctl::JsonValue j = parse_text(r.body).get("test_map");
        const std::string file = j.get("file").str();
        ASSERT_TRUE(MapStore::is_test_map_name(file));
        ASSERT_EQ(j.get("name").str(), "Popcorn Remix");
        ASSERT_EQ(j.get("kind").str(), "classic");
        ASSERT_EQ(j.get("usable").size(), size_t{4});
        ASSERT_EQ(j.get("players").as_int_or(0), 4);

        MapStore ms("", "");
        ms.set_test_maps(t.store->maps(t.now));
        MapEntry entry;
        ASSERT_TRUE(ms.find(file, entry));
        ASSERT_TRUE(ms.test_map(file) != nullptr);
        ASSERT_EQ(ms.listed_name(file), file);
        ASSERT_TRUE(ms.list_json(nullptr, false).find(file) == std::string::npos && ms.list_json(nullptr, true).find(file) == std::string::npos);
        ASSERT_TRUE(fs::path(entry.path).parent_path() == fs::path(t.config.dir));      // (the store's own folder, nothing else)

        // expires after kTestMapTtlS
        ASSERT_EQ(t.store->maps(t.now + TestMapStore::kTestMapTtlS).size(), size_t{0});
        ASSERT_EQ(files_in(t.config.dir), size_t{0});
    } TEST_END();

    TEST_CASE("TM1.2 A test map keeps the CANONICAL file, never the bytes that came: trailing junk is gone, the folder is for the server's user only (0700, the files 0600), and the file is the one that the hash and the size in the answer describe") {
        Door t;
        const std::string junk = tiny() + std::string(60000, 'Z');
        const HttpResponse r = t.upload(junk);
        ASSERT_EQ(r.status, 201);
        const ctl::JsonValue j = parse_text(r.body).get("test_map");
        MapStore ms("", "");
        ms.set_test_maps(t.store->maps(t.now));
        const ApprovedMap* kept = ms.test_map(j.get("file").str());
        ASSERT_TRUE(kept != nullptr);
        const std::string stored = read_file(kept->path);
        ASSERT_TRUE(stored.size() < junk.size() - 50000 && stored.find("ZZZZZZZZ") == std::string::npos);        // (the junk did not come through)
        ASSERT_EQ(static_cast<uint64_t>(stored.size()), kept->size);
        ASSERT_EQ(j.get("size").as_int_or(0), static_cast<int64_t>(stored.size()));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(kept->path, hash));
        ASSERT_EQ(map_hash_text(hash), j.get("hash").str());
        ASSERT_EQ(t.store->maps(t.now)[0].hash, hash);
#ifndef _WIN32
        ASSERT_EQ(static_cast<int>(fs::status(fs::path(t.config.dir)).permissions() & fs::perms::all), static_cast<int>(fs::perms::owner_all));
        ASSERT_EQ(static_cast<int>(fs::status(kept->path).permissions() & fs::perms::all), static_cast<int>(fs::perms::owner_read | fs::perms::owner_write));
        {   // the folder is made 0700 when the store opens (a folder that exists already too), and an upload makes it 0700 again if it was opened up meanwhile
            fs::permissions(fs::path(t.config.dir), fs::perms::all, fs::perm_options::replace);
            std::string why;
            ASSERT_TRUE(t.store->open(why));
            ASSERT_EQ(static_cast<int>(fs::status(fs::path(t.config.dir)).permissions() & fs::perms::all), static_cast<int>(fs::perms::owner_all));
            fs::permissions(fs::path(t.config.dir), fs::perms::all, fs::perm_options::replace);
            t.now_ms += 20000;
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.21").status, 201);
            ASSERT_EQ(static_cast<int>(fs::status(fs::path(t.config.dir)).permissions() & fs::perms::all), static_cast<int>(fs::perms::owner_all));
        }
#endif
        // a Classic map stays a Classic map (version 8) in the test folder
        ASSERT_EQ(static_cast<unsigned>(static_cast<unsigned char>(stored[0])), 8u);
    } TEST_END();

    TEST_CASE("TM1.3a The numbers that the server runs with are the owner's: 30 an hour for an address and 600 for all, 2 checks in a window of 10 s with 1 for an address and 600 ms of checking time, 64 maps, 4 for an address, 64 MiB, 6 hours, 2 minutes, 2 MiB of body and 512 KB for a Classic file") {
        const TestMapApiConfig c;
        ASSERT_TRUE(c.trust_client_header && c.attempts_per_hour == 30 && c.total_per_hour == 600 && c.checks_per_window == 2 && c.checks_per_address_window == 1 && c.check_window_ms == 10000 && c.check_budget_ms == 600);
        const TestMapConfig s;
        ASSERT_TRUE(s.max_held_bytes == (uint64_t{64} << 20) && s.min_free_bytes == (uint64_t{256} << 20));
        ASSERT_TRUE(TestMapStore::kMaxTestMaps == 64 && TestMapStore::kMaxTestPerAddress == 4 && TestMapStore::kTestMapTtlS == 6 * 3600 && TestMapStore::kTestMinKeepS == 120);
        ASSERT_TRUE(TestMapStore::kMaxBodyBytes == 2u * 1024u * 1024u && TestMapStore::kClassicBytes == 512u * 1024u);
    } TEST_END();

    TEST_CASE("TM1.3 The door's limits: an address may send so many an hour and all addresses together so many (whatever came of the tries), and the checks have a window of their own") {
        {   // the hour of one address: 6 test maps (spaced so that the store keeps them), the 7th is refused; a try that fails counts too
            Door t([](TestMapApiConfig& c) { c.attempts_per_hour = 6; });
            for (int i = 0; i < 5; ++i) {
                t.now += 130;
                t.now_ms += 20000;
                const HttpResponse r = t.upload(tiny());
                ASSERT_MSG(r.status == 201, "test upload " + std::to_string(i) + " answered " + std::to_string(r.status));
            }
            t.now += 130;
            t.now_ms += 20000;
            ASSERT_EQ(t.upload("not a map at all").status, 400);                  // (a failed try is a try)
            t.now += 130;
            t.now_ms += 20000;
            HttpResponse r = t.upload(tiny());
            ASSERT_EQ(r.status, 429);
            ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("too_many_tests"));
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.21").status, 201);       // (another address has its own hour)
            t.now += 3601;                                                         // (an hour later the address may send again)
            t.now_ms += 20000;
            ASSERT_EQ(t.upload(tiny()).status, 201);
        }
        {   // all addresses together
            Door t([](TestMapApiConfig& c) { c.total_per_hour = 3; });
            for (int i = 0; i < 3; ++i) {
                t.now_ms += 20000;
                ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113." + std::to_string(30 + i)).status, 201);
            }
            t.now_ms += 20000;
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.99").status, 429);
            t.now += 3601;                                                          // (an hour later the allowance is back)
            t.now_ms += 20000;
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.99").status, 201);
        }
        {   // the window: one check for an address in ten seconds, two in all
            Door t;
            ASSERT_EQ(t.upload(tiny()).status, 201);
            HttpResponse busy = t.upload(tiny());
            ASSERT_EQ(busy.status, 503);
            ASSERT_EQ(parse_text(busy.body).get("error").str(), std::string("busy"));
            t.now_ms += 10001;
            ASSERT_EQ(t.upload(tiny()).status, 201);                                // (the next window)
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.31").status, 201);       // (another address has the second check of the window)
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.32").status, 503);      // (two in all: checks_per_window)
        }
        {   // a refusal that costs no check (not a map by its version word, the wrong kind) uses none of the window
            Door t;
            for (int i = 0; i < 20; ++i) ASSERT_EQ(t.upload(std::string("\x01\x00\x00\x00junk", 8), "Junk", "198.51.100." + std::to_string(i + 1)).status, 400);
            ASSERT_EQ(t.upload(tiny(), "Popcorn", "203.0.113.50").status, 201);
        }
    } TEST_END();

    TEST_CASE("TM1.4 The checking time of test maps has a budget: a map that takes longer than the budget closes the window for the next one (503 busy), and the next window is clean") {
        Door t([](TestMapApiConfig& c) {
            c.check_budget_ms = 1;
            c.checks_per_window = 100;
            c.checks_per_address_window = 100;
        });
        const std::string slow = grown_tiny(256, 256);                                 // (a check of a big map takes tens of milliseconds)
        ASSERT_TRUE(!slow.empty());
        ASSERT_EQ(t.upload(slow, "Slow", "203.0.113.60", "extended").status, 201);
        const HttpResponse busy = t.upload(tiny(), "Quick", "203.0.113.61");
        ASSERT_MSG(busy.status == 503 && parse_text(busy.body).get("error").str() == "busy", std::to_string(busy.status) + " " + busy.body.substr(0, 100));
        t.now_ms += 10001;
        ASSERT_EQ(t.upload(tiny(), "Quick", "203.0.113.61").status, 201);
    } TEST_END();

    TEST_CASE("TM1.5 Refusals of a test map: a file that is no map, a kind that the file is not, an unknown kind, an empty body, a cross-site request, the wrong content type, a map of one team, a map that is bigger than the kind allows; nothing is kept for any of them") {
        Door t;
        const auto expect = [&](const HttpResponse& r, int status, const char* code, const std::string& what) {
            ASSERT_MSG(r.status == status && parse_text(r.body).get("error").str() == code, what + ": " + std::to_string(r.status) + " " + r.body.substr(0, 120));
        };
        const auto step = [&]() { t.now_ms += 20000; };                              // (a new checking window for each)
        expect(t.upload("this is not a map file at all, not even close"), 400, "not_a_map", "text");
        step();
        expect(t.upload(std::string(4, '\x08')), 400, "not_a_map", "a version word and nothing else");
        step();
        expect(t.upload(tiny(), "Popcorn", "203.0.113.20", "extended"), 400, "kind_mismatch", "a Classic file that says it is Extended");
        step();
        expect(t.upload(tiny(), "Popcorn", "203.0.113.20", "weird"), 400, "bad_kind", "an unknown kind");
        step();
        expect(t.upload(std::string()), 400, "not_a_map", "no body");
        ASSERT_TRUE(parse_text(t.upload(std::string()).body).get("message").str().find("No map file") != std::string::npos);          // (the empty body has its own sentence)
        step();
        expect(t.upload(tiny_with_teams(0x01)), 422, "map_fails_checks", "a map of one team");
        step();
        expect(t.upload(tiny_with_teams(0x00)), 422, "map_fails_checks", "a map of no team");
        step();
        const std::string cut = tiny().substr(0, tiny().size() / 2);
        expect(t.upload(cut), 400, "not_a_map", "a map that ends in the middle");
        step();
        expect(t.upload(std::string(600000, 'x')), 413, "too_large", "more bytes than a Classic map has");
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic", {{"content-type", "text/plain"}, {"x-client-address", "203.0.113.20"}}, tiny())).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic", {{"x-client-address", "203.0.113.20"}}, tiny())).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic", {{"content-type", "multipart/form-data; boundary=x"}, {"x-client-address", "203.0.113.20"}}, tiny())).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}, {"sec-fetch-site", "cross-site"}}, tiny())).status, 403);
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}, {"sec-fetch-site", "same-site"}}, tiny())).status, 403);
        step();
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic" + std::string(5000, 'x'), {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}}, tiny())).status, 414);
        HttpRequest nobody = t.req("POST", "/test-maps", "kind=classic", {{"content-type", "application/octet-stream"}}, tiny());
        nobody.peer.clear();
        expect(t.call(nobody), 400, "no_address", "no address at all");
        step();
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic", {{"content-type", "application/octet-stream"}}, tiny())).status, 201);        // (no address header: the connection's counts, 172.18.0.4)
        ASSERT_EQ(t.store->maps(t.now).size(), size_t{1});                       // (only the last one, the file without an address header, was a good map)
        ASSERT_EQ(files_in(t.config.dir), size_t{1});
        {   // a disk with no room left: the upload is refused whole, nothing is kept
            TestMapConfig full = t.config;
            full.dir = (t.dir.path / "starved").string();
            full.min_free_bytes = ~uint64_t{0} >> 1;
            TestMapStore starved(full);
            std::string why;
            ASSERT_TRUE(starved.open(why));
            TestMapInput in;
            in.kind = "classic";
            in.name = "Popcorn";
            in.bytes = tiny();
            in.address = "203.0.113.77";
            const TestMapOutcome o = starved.submit(in, t.now);
            ASSERT_TRUE(!o.ok && o.fail.status == 503 && o.fail.code == "storage_full");
            ASSERT_EQ(starved.maps(t.now).size(), size_t{0});
        }
        {   // a store that never opened keeps nothing and says so
            TestMapConfig none;
            TestMapStore closed(none);
            std::string why;
            ASSERT_FALSE(closed.open(why));
            TestMapInput in;
            in.kind = "classic";
            in.bytes = tiny();
            in.address = "203.0.113.78";
            const TestMapOutcome o = closed.submit(in, t.now);
            ASSERT_TRUE(!o.ok && o.fail.status == 503 && o.fail.code == "test_maps_off");
        }
        // two teams are enough
        Door two;
        ASSERT_EQ(two.upload(tiny_with_teams(0x03)).status, 201);
        ASSERT_EQ(parse_text(two.upload(tiny_with_teams(0x05), "Popcorn", "203.0.113.21").body).get("test_map").get("players").as_int_or(0), 2);
    } TEST_END();

    TEST_CASE("TM1.6 The name of a test map: a tilde, an angle bracket, a line end, a right-to-left mark or a path cannot get into it, it is cut at 45 characters, and the suffix ' ~test-<8 hex>.lvl' is the server's own") {
        Door t;
        const auto stem_of = [&](const std::string& encoded_name, const std::string& address) {
            t.now_ms += 20000;
            const HttpResponse r = t.upload(tiny(), encoded_name, address);
            return r.status == 201 ? parse_text(r.body).get("test_map") : ctl::JsonValue::make_null();
        };
        int n = 0;
        const auto address = [&]() { return "203.0.113." + std::to_string(40 + n++); };
        ctl::JsonValue j = stem_of("Evil%20~test-deadbeef%20Map", address());
        ASSERT_TRUE(j.get("file").str().size() > 0);
        const std::string file = j.get("file").str();
        ASSERT_TRUE(MapStore::is_test_map_name(file));
        ASSERT_EQ(file.find('~'), file.size() - 19 + 1);                              // (the one tilde is the suffix's)
        ASSERT_EQ(file.find("~test-"), file.size() - 18);                              // (the only "~test-" is the server's own, at the end)
        ASSERT_EQ(j.get("name").str().find('~'), std::string::npos);
        ASSERT_EQ(j.get("name").str(), std::string("Evil test-deadbeef Map"));                       // (the tilde became a blank: the words stay)
        j = stem_of("A%3Cb%3Eb%0D%0Ax%E2%80%AEy..%2F..%2Fz", address());               // < > CR LF, U+202E, ../..
        ASSERT_TRUE(j.get("name").str().size() > 0);
        for (const char bad : std::string("<>/\\\r\n~")) ASSERT_TRUE(j.get("name").str().find(bad) == std::string::npos);
        for (const char c : j.get("name").str()) ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
        j = stem_of(std::string(60, 'A'), address());                                 // 60 letters: 45 are kept
        ASSERT_EQ(j.get("name").str(), std::string(45, 'A'));
        ASSERT_EQ(j.get("file").str().size(), size_t{45 + 19});
        j = stem_of(std::string(45, 'B'), address());                                 // exactly 45 stay
        ASSERT_EQ(j.get("name").str(), std::string(45, 'B'));
        j = stem_of("%E2%80%AE%00", address());                                      // nothing that can be a name: the plain one
        ASSERT_EQ(j.get("name").str(), std::string("Test Map"));
        j = stem_of("Tiny", address());                                              // a name of the library is fine: the suffix makes it another
        ASSERT_EQ(j.get("name").str(), std::string("Tiny"));
        ASSERT_TRUE(j.get("file").str() != "Tiny.lvl" && MapStore::is_test_map_name(j.get("file").str()));
    } TEST_END();

    TEST_CASE("TM1.7 A map over the size limit of a room (65,536 cells) is refused as a test map with one plain sentence, whatever its kind allows; 256 x 256 is the most; the numbers are OnlineLimits'") {
        ASSERT_TRUE(OnlineLimits::kMaxCells == 65536u && OnlineLimits::allows(65536u) && !OnlineLimits::allows(65537u));          // the limit of a room
        Door t([](TestMapApiConfig& c) { c.checks_per_address_window = 100; c.checks_per_window = 100; });
        const auto step = [&]() { t.now_ms += 20000; };
        const std::string fits = grown_tiny(256, 256);
        ASSERT_TRUE(!fits.empty() && fits.size() > 700000 && fits.size() < TestMapStore::kMaxBodyBytes);
        HttpResponse r = t.upload(fits, "Fits", "203.0.113.20", "extended");
        ASSERT_MSG(r.status == 201, r.body.substr(0, 200));
        ASSERT_EQ(parse_text(r.body).get("test_map").get("width").as_int_or(0), 256);
        step();
        const std::string wide = grown_tiny(160, 410);                                // 65,600 cells: 64 over
        ASSERT_TRUE(!wide.empty());
        r = t.upload(wide, "Wide", "203.0.113.21", "extended");
        ASSERT_EQ(r.status, 422);
        ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("map_too_big_online"));
        ASSERT_TRUE(parse_text(r.body).get("message").str().find("65,536 cells") != std::string::npos && parse_text(r.body).get("message").str().find("against bots") != std::string::npos);
        step();
        r = t.upload(grown_tiny(512, 512), "Huge", "203.0.113.22", "extended");        // (3.1 MB: over the 2 MiB of this door, it never gets here from the wire; the store refuses it too)
        ASSERT_EQ(r.status, 413);
        ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("too_large"));
        step();
        r = t.upload(grown_tiny(300, 300), "Big", "203.0.113.25", "extended");         // 90,000 cells, under 2 MiB: a good map that no room plays
        ASSERT_EQ(r.status, 422);
        ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("map_too_big_online"));
        step();
        r = t.upload(grown_tiny(257, 1024), "Beyond", "203.0.113.23", "extended");    // 263,168 cells: more than the format allows at all, so the core reads no such map
        ASSERT_MSG(r.status == 400 || r.status == 413, std::to_string(r.status) + " " + r.body.substr(0, 200));
        step();
        {   // a Classic map of 100 rows (a Classic map has 90 at the most) is a map that loads and is too big for its kind
            ants::assets::LevelData tall = ants::assets::blank_level(100, 50, core::tile_names());
            const ants::assets::LevelWriteResult w = ants::assets::write_level(tall, ants::assets::LevelLimits{});
            ASSERT_TRUE(w.ok);
            r = t.upload(std::string(w.bytes.begin(), w.bytes.end()), "Tall", "203.0.113.24", "classic");
            ASSERT_MSG(r.status == 422 && parse_text(r.body).get("error").str() == "map_too_big", std::to_string(r.status) + " " + r.body.substr(0, 200));
        }
        ASSERT_EQ(t.store->maps(t.now).size(), size_t{1});                         // (only the 256 x 256 map is kept)
        ASSERT_TRUE(std::string(ants::net::kNoticeMapTooBig).find("65,536 cells") != std::string::npos);        // (the room's sentence says the same number)
        ASSERT_TRUE(std::string(ants::net::kNoticeMapTooBig).size() <= ants::net::kMaxChatChars);
    } TEST_END();

    TEST_CASE("TM1.8 The store of test maps: at most kMaxTestMaps (the oldest goes), at most kMaxTestPerAddress for one address (its own oldest goes), a map that a room has or that is under two minutes old never goes - the upload is refused instead - so a burst cannot push out the map of a room that has not started") {
        {   // the 64: 70 uploads from 70 addresses, each 3 minutes after the last
            Door t;
            std::vector<std::string> files;
            for (int i = 0; i < 70; ++i) {
                const TestMapOutcome o = t.keep("198.51.100." + std::to_string(i), 180);
                ASSERT_MSG(o.ok, "upload " + std::to_string(i) + ": " + o.fail.code);
                files.push_back(o.map.name);
            }
            MapStore ms("", "");
            ms.set_test_maps(t.store->maps(t.now));
            ASSERT_EQ(t.store->maps(t.now).size(), TestMapStore::kMaxTestMaps);
            ASSERT_TRUE(ms.test_map(files[0]) == nullptr && ms.test_map(files[5]) == nullptr && ms.test_map(files[6]) != nullptr && ms.test_map(files[69]) != nullptr);        // (the six oldest went: 70 - 64)
            ASSERT_EQ(files_in(t.config.dir), TestMapStore::kMaxTestMaps);                  // (and their files are gone)
        }
        {   // one address: the 5th takes the place of its own oldest
            Door t;
            std::vector<std::string> files;
            for (int i = 0; i < 6; ++i) files.push_back(t.keep("198.51.100.1", 180).map.name);
            MapStore ms("", "");
            ms.set_test_maps(t.store->maps(t.now));
            ASSERT_EQ(t.store->maps(t.now).size(), TestMapStore::kMaxTestPerAddress);
            ASSERT_TRUE(ms.test_map(files[0]) == nullptr && ms.test_map(files[1]) == nullptr && ms.test_map(files[2]) != nullptr && ms.test_map(files[5]) != nullptr);
            const TestMapOutcome other = t.keep("198.51.100.2", 1);              // (another address is not in the way of the first)
            ASSERT_TRUE(other.ok);
            ASSERT_EQ(t.store->maps(t.now).size(), TestMapStore::kMaxTestPerAddress + 1);
        }
        {   // an address that holds its share drops ITS OWN oldest map, never an older map of another address
            Door t;
            const TestMapOutcome older = t.keep("198.51.100.9", 0);              // (the oldest map of all is another address's)
            ASSERT_TRUE(older.ok);
            std::vector<std::string> mine;
            for (int i = 0; i < 5; ++i) mine.push_back(t.keep("198.51.100.1", 180).map.name);
            MapStore ms("", "");
            ms.set_test_maps(t.store->maps(t.now));
            ASSERT_TRUE(ms.test_map(older.map.name) != nullptr);
            ASSERT_TRUE(ms.test_map(mine[0]) == nullptr && ms.test_map(mine[1]) != nullptr && ms.test_map(mine[4]) != nullptr);
        }
        {   // five in a minute from one address: the fifth is refused (the other four are new), and the four stay
            Door t;
            for (int i = 0; i < 4; ++i) ASSERT_TRUE(t.keep("198.51.100.1", 5).ok);
            const TestMapOutcome fifth = t.keep("198.51.100.1", 5);
            ASSERT_TRUE(!fifth.ok && fifth.fail.status == 429 && fifth.fail.code == "test_limit");
            ASSERT_EQ(t.store->maps(t.now).size(), size_t{4});
            ASSERT_TRUE(t.keep("198.51.100.1", 200).ok);                            // (two minutes on, the oldest may go)
        }
        {   // a map that a room has is never dropped; when the store is full of such maps, the upload is refused
            Door t;
            std::set<std::string> in_use;
            t.store->set_in_use([&](const std::string& file) { return in_use.count(file) != 0; });
            std::vector<std::string> files;
            for (int i = 0; i < static_cast<int>(TestMapStore::kMaxTestMaps); ++i) {
                const TestMapOutcome o = t.keep("198.51.100." + std::to_string(i), 150);
                ASSERT_TRUE(o.ok);
                files.push_back(o.map.name);
                if (i < 10) in_use.insert(o.map.name);                                   // (the first ten have a room)
            }
            for (int i = 0; i < 20; ++i) ASSERT_TRUE(t.keep("198.51.101." + std::to_string(i), 150).ok);
            MapStore ms("", "");
            ms.set_test_maps(t.store->maps(t.now));
            for (int i = 0; i < 10; ++i) ASSERT_MSG(ms.test_map(files[static_cast<size_t>(i)]) != nullptr, "a map with a room is kept: " + std::to_string(i));
            ASSERT_TRUE(ms.test_map(files[10]) == nullptr && ms.test_map(files[29]) == nullptr && ms.test_map(files[30]) != nullptr);        // (the next twenty oldest went)
            in_use = std::set<std::string>(files.begin(), files.end());                    // every map has a room now
            for (const ApprovedMap& m : t.store->maps(t.now)) in_use.insert(m.name);
            const TestMapOutcome full = t.keep("198.51.102.1", 150);
            ASSERT_TRUE(!full.ok && full.fail.status == 503 && full.fail.code == "test_full");
            ASSERT_EQ(t.store->maps(t.now).size(), TestMapStore::kMaxTestMaps);
        }
        {   // a map of six hours is gone whatever; a restart (the store opens) wipes the folder
            Door t;
            ASSERT_TRUE(t.keep("198.51.100.1").ok);
            ASSERT_EQ(t.store->maps(t.now + TestMapStore::kTestMapTtlS - 1).size(), size_t{1});
            ASSERT_EQ(t.store->maps(t.now + TestMapStore::kTestMapTtlS).size(), size_t{0});
            ASSERT_TRUE(t.keep("198.51.100.1", 10).ok);
            std::string why;
            ASSERT_TRUE(t.store->open(why));
            ASSERT_EQ(files_in(t.config.dir), size_t{0});
            ASSERT_EQ(t.store->maps(t.now).size(), size_t{0});
        }
        {   // the hourly upkeep drops the old ones and says so once
            Door t;
            ASSERT_TRUE(t.keep("198.51.100.1").ok);
            t.store->take_changed();
            t.store->maintenance(t.now + TestMapStore::kTestMapTtlS - 1);
            ASSERT_FALSE(t.store->take_changed());
            t.store->maintenance(t.now + TestMapStore::kTestMapTtlS);
            ASSERT_TRUE(t.store->take_changed());
            ASSERT_FALSE(t.store->take_changed());
            ASSERT_EQ(files_in(t.config.dir), size_t{0});
        }
    } TEST_END();

    TEST_CASE("TM1.9 The held bytes have a cap: a full store drops the oldest map that may go, a map that cannot make room is refused (503 test_full), and a map that is bigger than the cap alone never gets in") {
        const std::string small = tiny();
        const TestMapOutcome probe = Door().keep("198.51.100.200");
        ASSERT_TRUE(probe.ok);
        const uint64_t one = probe.map.size;
        ASSERT_TRUE(one > 0 && one < small.size() + 1000);
        {   // room for three of them: the fourth (a new address each time, so that the count of an address is no cause) takes the place of the oldest
            Door t(nullptr, [&](TestMapConfig& c) { c.max_held_bytes = one * 3 + one / 2; });
            std::vector<std::string> files;
            for (int i = 0; i < 5; ++i) {
                const TestMapOutcome o = t.keep("198.51.100." + std::to_string(i), 180);
                ASSERT_MSG(o.ok, "upload " + std::to_string(i) + ": " + o.fail.code);
                files.push_back(o.map.name);
                ASSERT_TRUE(t.store->held_bytes() <= one * 3 + one / 2);
            }
            ASSERT_EQ(t.store->maps(t.now).size(), size_t{3});
            MapStore ms("", "");
            ms.set_test_maps(t.store->maps(t.now));
            ASSERT_TRUE(ms.test_map(files[0]) == nullptr && ms.test_map(files[1]) == nullptr && ms.test_map(files[2]) != nullptr && ms.test_map(files[4]) != nullptr);
            ASSERT_EQ(files_in(t.config.dir), size_t{3});
        }
        {   // the maps are new (under two minutes) or have a room: nothing can go, so the upload is refused and the held maps stay
            Door t(nullptr, [&](TestMapConfig& c) { c.max_held_bytes = one * 2 + one / 2; });
            ASSERT_TRUE(t.keep("198.51.100.1", 1).ok);
            ASSERT_TRUE(t.keep("198.51.100.2", 1).ok);
            const TestMapOutcome third = t.keep("198.51.100.3", 1);
            ASSERT_TRUE(!third.ok && third.fail.status == 503 && third.fail.code == "test_full");
            ASSERT_EQ(t.store->maps(t.now).size(), size_t{2});
            std::set<std::string> in_use;
            for (const ApprovedMap& m : t.store->maps(t.now)) in_use.insert(m.name);
            t.store->set_in_use([&](const std::string& file) { return in_use.count(file) != 0; });
            const TestMapOutcome fourth = t.keep("198.51.100.4", 300);                  // (old enough, but both have a room)
            ASSERT_TRUE(!fourth.ok && fourth.fail.code == "test_full");
            ASSERT_EQ(files_in(t.config.dir), size_t{2});
        }
        {   // a cap of exactly three maps holds three (the cap is "more than", not "as much as"), and the fourth takes the place of the oldest
            Door t(nullptr, [&](TestMapConfig& c) { c.max_held_bytes = one * 3; });
            std::vector<std::string> files;
            for (int i = 0; i < 4; ++i) {
                const TestMapOutcome o = t.keep("198.51.100." + std::to_string(i), 180);
                ASSERT_MSG(o.ok, "upload " + std::to_string(i) + ": " + o.fail.code);
                files.push_back(o.map.name);
                if (i == 2) ASSERT_EQ(t.store->maps(t.now).size(), size_t{3});                    // (three fit)
            }
            ASSERT_EQ(t.store->held_bytes(), one * 3);
            MapStore ms("", "");
            ms.set_test_maps(t.store->maps(t.now));
            ASSERT_TRUE(ms.test_map(files[0]) == nullptr && ms.test_map(files[1]) != nullptr && ms.test_map(files[3]) != nullptr);
        }
        {   // a refused upload drops nothing: the address holds its four, the other three have a room, and what would be left of the room after its oldest goes is still not enough
            Door t(nullptr, [&](TestMapConfig& c) { c.max_held_bytes = one * 4 + 1; });
            std::vector<std::string> files;
            for (int i = 0; i < 4; ++i) {
                const TestMapOutcome o = t.keep("198.51.100.1", 180);
                ASSERT_TRUE(o.ok);
                files.push_back(o.map.name);
            }
            std::set<std::string> in_use(files.begin() + 1, files.end());
            t.store->set_in_use([&](const std::string& file) { return in_use.count(file) != 0; });
            t.store->take_changed();
            t.now_ms += 20000;
            const HttpResponse r = t.upload(grown_tiny(64, 64), "Big", "198.51.100.1", "extended");
            ASSERT_MSG(r.status == 503 && parse_text(r.body).get("error").str() == "test_full", std::to_string(r.status) + " " + r.body.substr(0, 120));
            ASSERT_EQ(t.refreshed, 0);                                                              // (nothing changed: the lobby is not told)
            ASSERT_EQ(t.store->maps(t.now).size(), size_t{4});                                      // (the address's own oldest was NOT dropped for an upload that failed)
            ASSERT_EQ(files_in(t.config.dir), size_t{4});
            in_use.clear();                                                                         // (nobody has a room: the same upload now takes the room it needs)
            t.now_ms += 20000;
            ASSERT_EQ(t.upload(grown_tiny(64, 64), "Big", "198.51.100.1", "extended").status, 201);
            ASSERT_EQ(t.refreshed, 1);
        }
        {   // a map that is six hours old is gone before an upload is judged, so the lobby is told even when that upload is refused
            Door t;
            ASSERT_EQ(t.upload(tiny(), "Old", "198.51.100.1").status, 201);
            ASSERT_EQ(t.refreshed, 1);
            t.now += TestMapStore::kTestMapTtlS;
            t.now_ms += 20000;
            ASSERT_EQ(t.upload("not a map at all", "Bad", "198.51.100.2").status, 400);
            ASSERT_EQ(t.refreshed, 2);
            ASSERT_EQ(files_in(t.config.dir), size_t{0});
        }
        {   // a cap smaller than one map: nothing is ever kept
            Door t(nullptr, [&](TestMapConfig& c) { c.max_held_bytes = one - 1; });
            const TestMapOutcome o = t.keep("198.51.100.1", 180);
            ASSERT_TRUE(!o.ok && o.fail.code == "test_full");
            ASSERT_EQ(files_in(t.config.dir), size_t{0});
        }
        ASSERT_EQ(TestMapConfig().max_held_bytes, uint64_t{64} << 20);                      // (the real cap: 64 MiB)
    } TEST_END();

    TEST_CASE("TM1.10 A map that is six hours old is gone within a minute whether anybody uploads or not: upkeep() looks once a kUpkeepEveryMs and tells whether the set changed") {
        Door t;
        ASSERT_TRUE(t.keep("198.51.100.1").ok);
        t.store->take_changed();
        ASSERT_FALSE(t.store->upkeep(t.now, 1000));                                                  // (the first look: nothing is old)
        ASSERT_FALSE(t.store->upkeep(t.now + TestMapStore::kTestMapTtlS, 1000 + TestMapStore::kUpkeepEveryMs - 1));          // (a minute has not passed: not looked at)
        ASSERT_EQ(files_in(t.config.dir), size_t{1});
        ASSERT_TRUE(t.store->upkeep(t.now + TestMapStore::kTestMapTtlS, 1000 + TestMapStore::kUpkeepEveryMs));
        ASSERT_EQ(files_in(t.config.dir), size_t{0});
        ASSERT_FALSE(t.store->upkeep(t.now + TestMapStore::kTestMapTtlS, 1000 + 2 * TestMapStore::kUpkeepEveryMs));          // (and it is told once)
        ASSERT_EQ(TestMapStore::kUpkeepEveryMs, uint32_t{60000});
        ASSERT_TRUE(t.keep("198.51.100.1").ok);
        ASSERT_TRUE(t.store->upkeep(t.now + TestMapStore::kTestMapTtlS, 4294967000u));                // (the steady clock wraps at 2^32: a minute later is still a minute)
        ASSERT_EQ(files_in(t.config.dir), size_t{0});
    } TEST_END();

    TEST_CASE("TM2.1 The door has ONE route: POST /test-maps. The submissions routes, the owner's API, a path below or beside it, the old test form and every other method on it get a 404 or a 405, and nothing is made") {
        Door t;
        const std::map<std::string, std::string> good = {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}};
        const auto code_of = [](const HttpResponse& r) { return parse_text(r.body).get("error").str(); };
        for (const char* path : {"/submissions", "/submissions/", "/submissions/00000000000000000000000000000000", "/submissions/00000000000000000000000000000000/withdraw", "/admin", "/admin/api/me", "/admin/api/login",
                                 "/admin/api/maps", "/", "/test-maps/", "/test-maps/x", "/test-maps/../submissions", "/test-maps%2f", "/TEST-MAPS", "/test-map", "/test-mapsx", "/maps", "/maps/all", "/maps/files/x.lvl",
                                 "/healthz", "/replays", "/live"}) {
            for (const char* method : {"POST", "GET"}) {
                const HttpResponse r = t.call(t.req(method, path, "kind=classic&test=1&name=Popcorn", good, tiny()));
                ASSERT_MSG(r.status == 404 && code_of(r) == "not_found", std::string(method) + " " + path + " answered " + std::to_string(r.status));
            }
        }
        for (const char* method : {"GET", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS", "get", ""}) {
            const HttpResponse r = t.call(t.req(method, "/test-maps", "kind=classic&name=Popcorn", good, tiny()));
            ASSERT_MSG(r.status == 405 && code_of(r) == "method_not_allowed", std::string("method '") + method + "' answered " + std::to_string(r.status));
        }
        ASSERT_EQ(t.store->maps(t.now).size(), size_t{0});
        ASSERT_EQ(files_in(t.config.dir), size_t{0});
        ASSERT_EQ(t.refreshed, 0);
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic&test=1&name=Popcorn", good, tiny())).status, 201);        // (a "test" in the query is no switch: this IS the test door, whatever it says)
        ASSERT_EQ(std::string(TestMapApi::route()), std::string("/test-maps"));
    } TEST_END();

    TEST_CASE("TM2.2 The body limit is asked on the head: 2 MiB for POST /test-maps of an Extended map, 512 KB for any other kind, a few KB for every other call; the store refuses a Classic body over 512 KB 413 too_large too") {
        Door t;
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "kind=classic&name=x")), size_t{512 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "kind=extended&name=x")), size_t{2 * 1024 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "name=x&kind=extended")), size_t{2 * 1024 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "")), size_t{512 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "kind=extended%20&name=x")), size_t{512 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "kind=Extended&name=x")), size_t{512 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps", "x=kind=extended")), size_t{512 * 1024});
        ASSERT_EQ(TestMapApi::body_limit(t.req("GET", "/test-maps", "kind=extended")), size_t{4096});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/submissions", "kind=extended&name=x")), size_t{4096});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/test-maps/x", "kind=extended")), size_t{4096});
        ASSERT_EQ(TestMapApi::body_limit(t.req("POST", "/admin/api/maps/x/approve")), size_t{4096});
        ASSERT_EQ(TestMapApi::max_body(), size_t{2 * 1024 * 1024});
        ASSERT_EQ(TestMapStore::max_upload_bytes("classic"), size_t{512 * 1024});
        ASSERT_EQ(TestMapStore::max_upload_bytes("extended"), size_t{2 * 1024 * 1024});
        const HttpResponse over = t.upload(std::string(512 * 1024 + 1, 'x'));
        ASSERT_TRUE(over.status == 413 && parse_text(over.body).get("error").str() == "too_large");
        t.now_ms += 20000;
        const std::string exactly = std::string("\x08\x00\x00\x00", 4) + std::string(512 * 1024 - 4, 'x');          // (the biggest Classic body that is read as a map: it is junk, so it is "not a map", not "too large")
        ASSERT_EQ(parse_text(t.upload(exactly).body).get("error").str(), std::string("not_a_map"));
        t.now_ms += 20000;
        ASSERT_EQ(parse_text(t.upload(std::string(2 * 1024 * 1024 + 1, 'x'), "X", "203.0.113.20", "extended").body).get("error").str(), std::string("too_large"));
    } TEST_END();

    TEST_CASE("TM2.3 A name from a player never makes a path: a path, %2f, a backslash, a dot and ~test- cannot move the file out of the store's folder, and the file is always test-<8 hex>.lvl in it") {
        Door t([](TestMapApiConfig& c) { c.checks_per_window = 100; c.checks_per_address_window = 100; c.attempts_per_hour = 1000; c.total_per_hour = 1000; });
        const std::vector<std::string> names = {"..%2F..%2Fetc%2Fpasswd", "%2e%2e%2f%2e%2e%2fescape", "..\\..\\escape", "/etc/passwd", "C%3A%5CWindows%5Cx", "....//....//x", "%00..%00", "a/../../b", "~test-00000000", ".lvl", "x.lvl.lvl", "CON", "%2F"};
        int n = 0;
        for (const std::string& name : names) {
            t.now += 200;
            t.now_ms += 20000;
            const HttpResponse r = t.upload(tiny(), name, "198.51.100." + std::to_string(++n));
            ASSERT_MSG(r.status == 201, name + " answered " + std::to_string(r.status) + " " + r.body.substr(0, 100));
            const ctl::JsonValue j = parse_text(r.body).get("test_map");
            const std::string file = j.get("file").str();
            ASSERT_MSG(file.find('/') == std::string::npos && file.find('\\') == std::string::npos && file.find("..") == std::string::npos && file.find('~') == file.size() - 19 + 1, file);
        }
        const fs::path parent = fs::path(t.config.dir).parent_path();
        const size_t entries_beside = static_cast<size_t>(std::distance(fs::directory_iterator(parent), fs::directory_iterator()));
        ASSERT_EQ(entries_beside, size_t{1});                                                         // (the store's folder is the only thing in its parent: nothing escaped)
        size_t on_disk = 0;
        for (const auto& entry : fs::directory_iterator(t.config.dir)) {
            const std::string name = entry.path().filename().string();
            ASSERT_MSG(entry.is_regular_file() && name.size() == 17 && name.compare(0, 5, "test-") == 0 && name.compare(13, 4, ".lvl") == 0, name);
            ++on_disk;
        }
        ASSERT_EQ(on_disk, t.store->maps(t.now).size());
        ASSERT_EQ(on_disk, names.size());                                                             // (every map is on disk once, each from its own address)
    } TEST_END();

    TEST_CASE("TM2.4 Opening the store wipes only its own files: test-<8 hex>.lvl and .tmp-* that are regular files; another file, a folder, a link and a name in another case stay, and nothing outside the folder is touched") {
        TempDir dir;
        const fs::path folder = dir.path / "maps";
        fs::create_directories(folder);
        const fs::path outside = dir.path / "outside.lvl";
        { std::ofstream(outside, std::ios::binary) << "keep me"; }
        for (const char* name : {"test-0a1b2c3d.lvl", "test-ffffffff.lvl", ".tmp-1234"}) { std::ofstream(folder / name, std::ios::binary) << "x"; }
        // (names that differ from a store file's by letter case only are made with other digits: on Windows and on a default macOS volume "test-0A1B2C3D.lvl" is the very file "test-0a1b2c3d.lvl", so the first test would wipe it)
        for (const char* name : {"keep.txt", "test-0A1B2C3E.lvl", "test-0a1b2c3.lvl", "test-0a1b2c3dd.lvl", "test-0a1b2c3e.LVL", "other-0a1b2c3d.lvl"}) { std::ofstream(folder / name, std::ios::binary) << "x"; }
        fs::create_directories(folder / "test-aaaaaaaa.lvl.d");
        fs::create_directories(folder / "test-bbbbbbbb.lvl");                                          // (a folder with a store file's name)
        { std::ofstream(folder / "test-bbbbbbbb.lvl" / "inner.txt", std::ios::binary) << "x"; }
#ifndef _WIN32
        std::error_code ec;
        fs::create_symlink(outside, folder / "test-deadbeef.lvl", ec);                                  // (a link with a store file's name: not a regular file, so it is left alone)
        ASSERT_TRUE(!ec);
#endif
        TestMapConfig c;
        c.dir = folder.string();
        TestMapStore store(c);
        std::string why;
        ASSERT_TRUE(store.open(why));
        ASSERT_TRUE(!fs::exists(folder / "test-0a1b2c3d.lvl") && !fs::exists(folder / "test-ffffffff.lvl") && !fs::exists(folder / ".tmp-1234"));
        for (const char* name : {"keep.txt", "test-0A1B2C3E.lvl", "test-0a1b2c3.lvl", "test-0a1b2c3dd.lvl", "test-0a1b2c3e.LVL", "other-0a1b2c3d.lvl", "test-aaaaaaaa.lvl.d"}) ASSERT_MSG(fs::exists(folder / name), name);
        ASSERT_TRUE(fs::is_directory(folder / "test-bbbbbbbb.lvl") && fs::exists(folder / "test-bbbbbbbb.lvl" / "inner.txt"));
        ASSERT_EQ(read_file(outside.string()), std::string("keep me"));
#ifndef _WIN32
        ASSERT_TRUE(fs::is_symlink(folder / "test-deadbeef.lvl"));
#endif
        // no folder: not open
        TestMapConfig none;
        TestMapStore closed(none);
        ASSERT_FALSE(closed.open(why));
        ASSERT_FALSE(why.empty());
        // a folder that cannot be made (a file is where it should go)
        TestMapConfig blocked;
        blocked.dir = (outside / "inside").string();
        TestMapStore refused(blocked);
        ASSERT_FALSE(refused.open(why));
        ASSERT_EQ(read_file(outside.string()), std::string("keep me"));
    } TEST_END();

    TEST_CASE("TM2.5 The test door needs no database, no account and no submissions store, and a test map is never a library map: the folder is its own, the MapStore lists nothing of it, and a library folder is never written") {
        Door t;                                                                         // (nothing but a folder and the checker: there is no db::Database in this program's Door)
        ASSERT_EQ(t.upload(tiny()).status, 201);
        MapStore library(maps_dir(), std::string(ANTS_SOURCE_DIR) + "/Community-Maps");
        library.scan();
        const size_t listed = library.list_json(nullptr, true).size();
        library.set_test_maps(t.store->maps(t.now));
        ASSERT_EQ(library.list_json(nullptr, true).size(), listed);                    // (a test map is on no list)
        ASSERT_EQ(library.list_json(nullptr, false).find("~test-"), std::string::npos);
        ASSERT_EQ(library.list_json(nullptr, true).find("~test-"), std::string::npos);
        const fs::path folder(t.config.dir);
        const std::string lower = folder.generic_string();
        ASSERT_TRUE(lower.find("Original-Ants") == std::string::npos && lower.find("Community-Maps") == std::string::npos);
        for (const auto& m : t.store->maps(t.now)) ASSERT_TRUE(fs::path(m.path).parent_path() == folder);
        const std::string name = t.store->maps(t.now)[0].name;
        ASSERT_TRUE(MapStore::is_test_map_name(name) && library.test_map(name) != nullptr);
    } TEST_END();

    TEST_CASE("TM2.6 The same bytes get the same verdict from the test store as from the real submissions store (one core checker), except the three rules that are a room's (a map of two or three colors may be played, a warning of the checker does not stop a map, a map over 65,536 cells may not)") {
        TempDir dir;
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        SubmissionConfig sc;
        sc.dir = (dir.path / "s").string();
        sc.min_free_bytes = 1;
        sc.per_address_per_day = 100000;
        sc.per_day_total = 100000;
        sc.max_waiting = 100000;
        SubmissionStore real(database, sc, [](const std::string&) { return false; });
        std::string why;
        ASSERT_TRUE(real.open(kT0, why));
        TestMapConfig tc;
        tc.dir = (dir.path / "t").string();
        tc.min_free_bytes = 1;
        TestMapStore test(tc);
        ASSERT_TRUE(test.open(why));

        struct Case {
            std::string what, kind, bytes;
            int mode;                        // 0: the same answer and the same code; 1: both refuse, the code may differ (the test store rewrites a map before it judges it, the real one judges the bytes first); 2: only the test store refuses (the online limit); 3: only the real store refuses (a map that works for two or three colors, a map with a warning of the checker: a room of two can play them, a library map must work for all four colors and without a warning)
        };
        const core::Result blank = core::blank(31, 31, 485, 10, 10, core::Profile::NewMap);       // (a map that does not pass)
        ASSERT_EQ(blank.status, 0);
        ants::assets::LevelData tall = ants::assets::blank_level(100, 50, core::tile_names());
        const ants::assets::LevelWriteResult tall_bytes = ants::assets::write_level(tall, ants::assets::LevelLimits{});
        ASSERT_TRUE(tall_bytes.ok);
        const std::string cut = tiny().substr(0, tiny().size() / 2);
        const std::vector<Case> cases = {
            {"a good Classic map", "classic", tiny(), 0},
            {"a good Classic map with trailing bytes", "classic", tiny() + std::string(3000, 'Z'), 0},
            {"text", "classic", "this is not a map file at all, not even close", 0},
            {"a version word and nothing else", "classic", std::string(4, '\x08'), 0},
            {"a Classic file that says it is Extended", "extended", tiny(), 0},
            {"an unknown kind", "weird", tiny(), 0},
            {"no body", "classic", std::string(), 0},
            {"a map that ends in the middle", "classic", cut, 1},
            {"more bytes than a Classic map has", "classic", std::string(600000, 'x'), 0},
            {"a Classic map with a description that fills all 30 bytes (a warning of the checker)", "classic", tiny_described(std::string(30, 'x')), 3},
            {"a map of two teams", "classic", tiny_with_teams(0x03), 3},
            {"a map of three teams", "classic", tiny_with_teams(0x07), 3},
            {"a map of one team", "classic", tiny_with_teams(0x01), 0},
            {"a map of no team", "classic", tiny_with_teams(0x00), 0},
            {"a map that does not pass the checks", "classic", std::string(blank.payload.begin(), blank.payload.end()), 0},
            {"a Classic map that is too tall for its kind", "classic", std::string(tall_bytes.bytes.begin(), tall_bytes.bytes.end()), 0},
            {"a 256 x 256 Extended map", "extended", grown_tiny(256, 256), 0},
            {"a 300 x 300 Extended map (too big for a room, fine for a library)", "extended", grown_tiny(300, 300), 2},
            {"a 160 x 410 Extended map (64 cells over a room's limit)", "extended", grown_tiny(160, 410), 2},
        };
        int n = 0;
        for (const Case& c : cases) {
            SubmitInput rin;
            rin.kind = c.kind;
            rin.name = "Popcorn " + std::to_string(++n);
            rin.maker = "Mia";
            rin.bytes = c.bytes;
            rin.address = "203.0.113." + std::to_string(n);
            TestMapInput tin;
            tin.kind = c.kind;
            tin.name = rin.name;
            tin.bytes = c.bytes;
            tin.address = rin.address;
            const SubmitOutcome a = real.submit(rin, kT0 + n);
            const TestMapOutcome b = test.submit(tin, kT0 + n * 200);
            const std::string saw = c.what + ": real " + (a.ok ? "accepted" : a.fail.code) + ", test " + (b.ok ? "accepted" : b.fail.code);
            if (c.mode == 3) {
                ASSERT_MSG(!a.ok && a.fail.code == "map_fails_checks" && b.ok && b.map.players >= 2, saw);
            } else if (c.mode != 2) {
                ASSERT_MSG(a.ok == b.ok, saw);
                if (!a.ok && c.mode == 0) ASSERT_MSG(a.fail.status == b.fail.status && a.fail.code == b.fail.code, saw);
                if (a.ok) {
                    ASSERT_MSG(read_file(sc.dir + "/files/" + a.submission.file) == read_file(b.map.path), saw + " (the canonical files differ)");      // (the same canonical file, byte for byte)
                    ASSERT_MSG(static_cast<int64_t>(b.map.width) == a.submission.width && static_cast<int64_t>(b.map.height) == a.submission.height, saw);
                    ASSERT_MSG(map_hash_text(b.map.hash) == a.submission.hash, saw);
                }
            } else {
                ASSERT_MSG(a.ok && !b.ok && b.fail.status == 422 && b.fail.code == "map_too_big_online", saw);
            }
        }
    } TEST_END();

    TEST_CASE("TM3.1 The wiring to the rooms (connect_test_maps, upkeep_test_maps, what main() calls): the rooms' store knows a new map and a dropped one, a map that a room has is not dropped to make room, and the upkeep reaches the rooms") {
        const TestMapOutcome probe = Door().keep("198.51.100.200");
        ASSERT_TRUE(probe.ok);
        Door u(nullptr, [&](TestMapConfig& c) { c.max_held_bytes = probe.map.size + probe.map.size / 2; });          // (room for one map)
        MapStore library{maps_dir()};
        RoomManager rooms(std::move(library), ServerLimits());
        connect_test_maps(rooms, *u.store, *u.api, [&u]() { return u.now; });
        u.now_ms += 20000;
        HttpResponse r = u.upload(tiny(), "Room", "198.51.100.1");
        ASSERT_EQ(r.status, 201);
        const std::string file = parse_text(r.body).get("test_map").get("file").str();
        ASSERT_TRUE(rooms.store().test_map(file) != nullptr);                                        // (the door told the rooms' store)
        RoomSpec spec;
        spec.code = "TM-ROOM-1";
        spec.map = file;
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        ASSERT_TRUE(rooms.create_room(spec, 1000).ok);
        ASSERT_TRUE(rooms.map_in_use(file));
        u.now += 300;
        u.now_ms += 20000;
        r = u.upload(tiny(), "Next", "198.51.100.2");                                                // (the only map that could go has a room: refused, and the map stays)
        ASSERT_MSG(r.status == 503 && parse_text(r.body).get("error").str() == "test_full", std::to_string(r.status) + " " + r.body.substr(0, 100));
        ASSERT_TRUE(rooms.store().test_map(file) != nullptr);
        ASSERT_TRUE(rooms.close_room("TM-ROOM-1", 2000));
        for (const uint32_t ms : {2000u, 600000u, 3600000u, 7200000u}) {                              // (a closed room is gone when its keep time is over and the manager has seen it)
            rooms.update(ms);
            rooms.take_ended(ms);
            if (!rooms.map_in_use(file)) break;
        }
        ASSERT_FALSE(rooms.map_in_use(file));
        u.now_ms += 20000;
        r = u.upload(tiny(), "Next", "198.51.100.2");
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(rooms.store().test_map(file) == nullptr);                                        // (the old one was dropped, and the rooms' store knows)
        const std::string next = parse_text(r.body).get("test_map").get("file").str();
        ASSERT_TRUE(rooms.store().test_map(next) != nullptr);
        upkeep_test_maps(rooms, *u.store, u.now + TestMapStore::kTestMapTtlS, 5000);                  // (six hours on: the upkeep drops it and the rooms follow)
        ASSERT_TRUE(rooms.store().test_map(next) == nullptr);
        ASSERT_EQ(files_in(u.config.dir), size_t{0});
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

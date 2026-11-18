// Tests of the routes of the map submissions (SubmissionApi, docs/MAP_SUBMISSIONS.md): what a player's browser may do and how it is limited by address, what an administrator's page is given (the lists and their counts,
// the decisions through the API, what is cleaned) and the page's text (nothing is HTML). The admin page's pass, the one answer without it and the token of a change are test_admin_pass. Requests are made by hand: no sockets.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "../common/test_assert.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_server/admin_audit.hpp"
#include "ants_server/admin_pass.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/map_pictures.hpp"
#include "ants_net/netgame.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/submissions.hpp"
#include "ants_server/submissions_http.hpp"
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

const char* const kSecret = "testing-gate-key-0b8e2d71a4c6f395-z";           // (a made-up key: the test plays the hub's gate and signs the passes)
const char* const kOwnerRef = "0123456789abcdef";

// The checking budget of every Api below, unless a test sets its own: SubmissionApiConfig::check_budget_ms is real time (1,500 ms in a window by default, spent by the sends themselves), so a slow or busy
// machine used it up in the middle of a test, and the next send in the window was answered 503 (the first send of a process also pays for the one-time scan of the map library). An hour is more than
// any runner needs; the budget itself is held by API1.6 (none at all) and API1.6c (a real send that takes longer than the budget).
constexpr uint32_t kBudgetNoRunnerUsesUp = 3600u * 1000u;

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

// A pass as the hub's gate makes it (docs/HUB.md): `v1.<payload>.<mac>`, the mac keyed with the raw digest of HMAC(secret, label)
std::string make_pass(const std::string& secret, const std::string& ref, const std::string& name, const std::string& role, int64_t exp, const std::string& nonce) {
    const std::string payload = "{\"exp\":" + std::to_string(exp) + ",\"name\":\"" + name + "\",\"nonce\":\"" + nonce + "\",\"ref\":\"" + ref + "\",\"role\":\"" + role + "\"}";
    const std::string signed_part = "v1." + base64url_encode(payload);
    return signed_part + "." + hex_encode(hmac_sha256(hmac_sha256(secret, "ants-admin-pass-v1"), signed_part));
}

MapStore& library() {
    static MapStore store = [] {
        MapStore s(maps_dir(), std::string(ANTS_SOURCE_DIR) + "/Community-Maps");
        s.scan();
        return s;
    }();
    return store;
}

struct Api {
    TempDir dir;
    db::Database database;
    SubmissionConfig config;
    std::unique_ptr<SubmissionStore> store;
    std::unique_ptr<AdminAudit> audit;
    std::unique_ptr<SubmissionApi> api;
    int64_t now{kT0};
    uint32_t now_ms{1000};
    int refreshed{0};
    uint64_t passes{0};

    explicit Api(const std::function<void(SubmissionApiConfig&)>& tune = nullptr) {
        open_server_db(database, "");
        config.dir = (dir.path / "s").string();
        config.min_free_bytes = 1;
        config.per_address_per_day = 1000;
        config.per_day_total = 100000;
        config.max_waiting = 1000;
        store = std::make_unique<SubmissionStore>(database, config, [](const std::string& key) { return library().library_has_name(key); });
        std::string why;
        store->open(now, why);
        audit = std::make_unique<AdminAudit>(database);
        audit->open(why);
        SubmissionApiConfig ac;
        ac.gate_secret = kSecret;
        ac.attempts_per_hour = 1000;
        ac.check_budget_ms = kBudgetNoRunnerUsesUp;
        if (tune) tune(ac);
        api = std::make_unique<SubmissionApi>(*store, *audit, ac);
        api->set_on_approved_changed([this] { ++refreshed; });
    }

    HttpResponse call(const HttpRequest& r) { return api->handle(r, now, now_ms); }

    // The pictures that the picture maker made (map_pictures.hpp): a folder that holds the picture of each of `ids` (the frame of a PNG; the maker itself is test_map_pictures.cpp) and is not asked to make any
    std::unique_ptr<MapPictures> pictures;
    void make_pictures(const std::vector<std::string>& ids) {
        MapPicturesConfig pc;
        pc.dir = (dir.path / "pictures").string();
        fs::create_directories(pc.dir);
        { std::ofstream(fs::path(pc.dir) / "version") << "1\n"; }
        const std::string png = std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\x0a\0\0\0\x0a\x08\x02\0\0\0", 29) + std::string(4, '\0') + std::string("\0\0\0\0IEND\xAE\x42\x60\x82", 12);
        for (const std::string& id : ids) std::ofstream(fs::path(pc.dir) / (id + ".png"), std::ios::binary) << png;
        pictures = std::make_unique<MapPictures>(pc);
        std::string why;
        pictures->open(why);
        store->set_pictures(pictures.get());
    }

    std::string text_of_state(const std::string& id) {
        Submission s;
        return store->get(id, s) ? s.state : std::string("<none>");
    }

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

    HttpResponse submit(const std::string& name, const std::string& address = "203.0.113.5", const std::string& token = "", const std::string& body = std::string(), const std::string& extra_query = "") {
        std::map<std::string, std::string> h = {{"content-type", "application/octet-stream"}, {"x-client-address", address}};
        if (!token.empty()) h["authorization"] = "Bearer " + token;
        HttpRequest r = req("POST", "/submissions", "kind=classic&name=" + name + "&maker=Mia&note=hello" + extra_query, h, body.empty() ? tiny() : body);
        return call(r);
    }

    // A new pass for the admin's account (each request needs its own: a pass is used once)
    std::string pass() { char nonce[32]; std::snprintf(nonce, sizeof nonce, "%016llx", static_cast<unsigned long long>(++passes)); return make_pass(kSecret, kOwnerRef, "dave", "admin", now + 60, nonce); }

    ctl::JsonValue me() { return parse_text(call(req("GET", "/admin/api/me", "", {{"x-ants-admin-pass", pass()}})).body); }

    // an admin's call: a pass of its own and, for a change, the page's token of the account
    HttpResponse owner(const std::string& method, const std::string& path, const std::string& query = "", const std::string& body = "") {
        std::map<std::string, std::string> h = {{"x-ants-admin-pass", pass()}};
        if (method == "POST") {
            h["content-type"] = "application/json";
            h["x-ants-csrf"] = me().get("csrf").str();
        }
        return call(req(method, path, query, h, body));
    }
};

}  // namespace

int main() {
    std::cout << "=======================================================\n MAP SUBMISSIONS: THE ROUTES AND THE ADMIN PAGE'S PASS\n=======================================================\n";

    TEST_CASE("API1.1 A player sends a map, finds it again by the token, and withdraws it: the shapes of the answers, and nothing else is a route") {
        Api t;
        HttpResponse r = t.submit("River%20Run");
        ASSERT_EQ(r.status, 201);
        const ctl::JsonValue sent = parse_text(r.body);
        ASSERT_TRUE(sent.get("token_is_new").as_bool_or(false) && sent.get("token").str().size() == 43 && !sent.get("name_taken").as_bool_or(true));
        const ctl::JsonValue& s = sent.get("submission");
        ASSERT_TRUE(s.get("state").str() == "waiting" && s.get("kind").str() == "classic" && s.get("name").str() == "River Run" && s.get("id").str().size() == 32);
        ASSERT_TRUE(!s.has("maker") && !s.has("note") && !s.has("findings"));                  // (a maker's view only)
        const std::string token = sent.get("token").str();
        const std::string id = s.get("id").str();
        r = t.call(t.req("GET", "/submissions", "", {{"authorization", "Bearer " + token}}));
        ASSERT_TRUE(r.status == 200 && parse_text(r.body).get("submissions").size() == 1);
        r = t.call(t.req("GET", "/submissions/" + id, "", {{"authorization", "Bearer " + token}}));
        ASSERT_TRUE(r.status == 200 && parse_text(r.body).get("submission").get("id").str() == id);
        r = t.call(t.req("GET", "/submissions/" + id, "", {{"authorization", "Bearer " + std::string(43, 'A')}}));
        ASSERT_EQ(r.status, 404);
        r = t.call(t.req("GET", "/submissions/" + std::string(32, '0'), "", {{"authorization", "Bearer " + token}}));
        ASSERT_EQ(r.status, 404);
        r = t.call(t.req("GET", "/submissions"));
        ASSERT_TRUE(r.status == 401 && parse_text(r.body).get("error").str() == "no_token");
        r = t.call(t.req("GET", "/submissions/" + id + "/withdraw", "", {{"authorization", "Bearer " + token}}));
        ASSERT_EQ(r.status, 405);
        r = t.call(t.req("POST", "/submissions/" + id + "/withdraw", "", {{"authorization", "Bearer " + token}}));
        ASSERT_TRUE(r.status == 200 && parse_text(r.body).get("submission").get("state").str() == "withdrawn");
        ASSERT_EQ(t.call(t.req("POST", "/submissions/" + id + "/withdraw", "", {{"authorization", "Bearer " + token}})).status, 409);
        ASSERT_EQ(t.call(t.req("POST", "/submissions/" + id + "/other", "", {{"authorization", "Bearer " + token}})).status, 404);
        ASSERT_EQ(t.call(t.req("DELETE", "/submissions")).status, 405);
        ASSERT_EQ(t.call(t.req("GET", "/other")).status, 404);
        ASSERT_EQ(t.call(t.req("GET", "/submissionsx")).status, 404);
        ASSERT_EQ(t.call(t.req("GET", "/")).status, 404);
        r = t.submit("Second%20Map", "203.0.113.5", token);                                       // (the token goes back with the next one: no new token)
        ASSERT_TRUE(r.status == 201 && !parse_text(r.body).get("token_is_new").as_bool_or(true) && !parse_text(r.body).has("token"));
    } TEST_END();

    TEST_CASE("API1.2 What a send must be: the content type, the same site, a name that is kept, the kind; a refusal is JSON with a stable code and a sentence") {
        Api t;
        HttpRequest r = t.req("POST", "/submissions", "kind=classic&name=Abc&maker=Mia", {{"x-client-address", "203.0.113.5"}}, tiny());
        ASSERT_EQ(t.call(r).status, 415);                                                       // (no content type)
        r.headers["content-type"] = "text/plain";
        ASSERT_EQ(t.call(r).status, 415);                                                       // (the type of a form that another site can send)
        r.headers["content-type"] = "multipart/form-data; boundary=x";
        ASSERT_EQ(t.call(r).status, 415);
        r.headers["content-type"] = "Application/Octet-Stream; charset=binary";
        ASSERT_EQ(t.call(r).status, 201);
        r.query = "kind=classic&name=Abd&maker=Mia";
        r.headers["sec-fetch-site"] = "cross-site";
        ASSERT_EQ(t.call(r).status, 403);
        r.headers["sec-fetch-site"] = "same-site";
        ASSERT_EQ(t.call(r).status, 403);
        r.headers["sec-fetch-site"] = "same-origin";
        ASSERT_EQ(t.call(r).status, 201);
        r.headers.erase("sec-fetch-site");
        r.query = "kind=classic&name=a%2F..%2Fb&maker=Mia";
        HttpResponse bad = t.call(r);
        ASSERT_TRUE(bad.status == 400 && parse_text(bad.body).get("error").str() == "bad_name" && parse_text(bad.body).get("message").str().size() > 10 && parse_text(bad.body).get("reason").str() == "characters");
        r.query = "kind=classic&name=Abe";
        ASSERT_EQ(parse_text(t.call(r).body).get("error").str(), std::string("bad_maker"));       // (no maker)
        r.query = "kind=classic&name=%zz&maker=Mia";
        ASSERT_EQ(parse_text(t.call(r).body).get("error").str(), std::string("bad_name"));        // (a bad escape reads as no name)
        r.query = "name=Abf&maker=Mia";
        ASSERT_EQ(parse_text(t.call(r).body).get("error").str(), std::string("bad_kind"));
        r.query = "kind=classic&name=Abg&maker=Mia";
        r.body = "";
        ASSERT_EQ(parse_text(t.call(r).body).get("error").str(), std::string("not_a_map"));
        r.query = "kind=classic&name=Abg&maker=Mia" + std::string(5000, 'x');
        r.body = tiny();
        ASSERT_EQ(t.call(r).status, 414);
        ASSERT_EQ(t.call(t.req("POST", "/submissions", "kind=classic&name=Abh&maker=Mia", {{"content-type", "application/octet-stream"}}, tiny())).status, 201);      // (no address header: the connection's own address)
        HttpRequest nobody = t.req("POST", "/submissions", "kind=classic&name=Abi&maker=Mia", {{"content-type", "application/octet-stream"}}, tiny());
        nobody.peer.clear();
        ASSERT_EQ(parse_text(t.call(nobody).body).get("error").str(), std::string("no_address"));
        ASSERT_EQ(t.call(t.req("GET", "/submissions/ZZZ")).status, 404);
        ASSERT_EQ(t.call(t.req("GET", "/submissions/..%2f..%2fetc", "", {})).status, 404);
    } TEST_END();

    TEST_CASE("API1.3 Tries are limited for each address and hour whatever came of them (a check costs time); another address, another hour, and the /64 of an IPv6 address are as the rule says") {
        Api t([](SubmissionApiConfig& c) { c.attempts_per_hour = 4; });
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.submit("Map%20" + std::to_string(i + 10), "203.0.113.5", "", "not a map").status, 400);    // (tries that fail count too)
        HttpResponse r = t.submit("Map%2020", "203.0.113.5");
        ASSERT_TRUE(r.status == 429 && parse_text(r.body).get("error").str() == "too_many_requests");
        ASSERT_EQ(t.submit("Map%2021", "203.0.113.6").status, 201);
        t.now += 3601;
        ASSERT_EQ(t.submit("Map%2022", "203.0.113.5").status, 201);
        for (int i = 0; i < 4; ++i) ASSERT_NE(t.submit("V6%20" + std::to_string(i), "2001:db8:5:6:" + std::to_string(i + 1) + "::1", "", "x").status, 429);        // (the same /64)
        ASSERT_EQ(t.submit("V6%20Five", "2001:db8:5:6:9::1").status, 429);
        ASSERT_NE(t.submit("V6%20Six", "2001:db8:5:7::1").status, 429);
        for (int i = 0; i < 4; ++i) ASSERT_NE(t.submit("Mapped" + std::to_string(i), "::ffff:198.51.100.9", "", "x").status, 429);
        ASSERT_EQ(t.submit("Mapped5", "198.51.100.9").status, 429);                              // (a mapped address is the IPv4 address)
        // reading one's own list is not a try
        for (int i = 0; i < 20; ++i) ASSERT_EQ(t.call(t.req("GET", "/submissions", "", {{"x-client-address", "203.0.113.5"}})).status, 401);
    } TEST_END();

    TEST_CASE("API1.4 A header that the visitor sends cannot change the address that counts when the door does not trust it; one flood of spoofed addresses cannot make an endless table") {
        Api trusting;
        Api direct([](SubmissionApiConfig& c) { c.trust_client_header = false; c.attempts_per_hour = 3; });
        for (int i = 0; i < 3; ++i) ASSERT_EQ(direct.submit("Map%20" + std::to_string(i), "203.0.113." + std::to_string(i + 1), "", "x").status, 400);
        ASSERT_EQ(direct.submit("Map%20Four", "203.0.113.99").status, 429);                      // (the visitor's header is not believed: the connection's address is the one)
        Api table([](SubmissionApiConfig& c) { c.attempts_per_hour = 1; });
        int refused = 0;
        for (int i = 0; i < 10200; ++i) {
            const HttpResponse r = table.submit("Mx", "10." + std::to_string(i / 65536) + "." + std::to_string((i / 256) % 256) + "." + std::to_string(i % 256), "", "x");
            refused += r.status == 429 ? 1 : 0;
        }
        ASSERT_GT(refused, 0);                                                                   // (the table has a bound: past it nobody new is let in until the hour has passed)
        table.now += 3601;
        ASSERT_NE(table.submit("Mx", "192.0.2.1", "", "x").status, 429);
    } TEST_END();

    TEST_CASE("API1.5 The body limit is asked on the head: 512 KB for a Classic map, 4 MiB for an Extended one, a few KB for every other call") {
        Api t;
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/submissions", "kind=classic&name=x")), size_t{512 * 1024});
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/submissions", "name=x")), size_t{512 * 1024});
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/submissions", "kind=extended&name=x")), size_t{4 * 1024 * 1024});
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/submissions", "kind=extended%20&name=x")), size_t{512 * 1024});
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/submissions", "x=kind=extended")), size_t{512 * 1024});
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/admin/api/maps/x/approve")), size_t{4096});
        ASSERT_EQ(SubmissionApi::body_limit(t.req("POST", "/submissions/x/withdraw", "kind=extended")), size_t{4096});
        ASSERT_EQ(SubmissionApi::max_body(), size_t{4 * 1024 * 1024});
    } TEST_END();

    TEST_CASE("API1.6 Checking maps has a budget for the server's one thread: past the count of a window, or past its time, a send is answered 503 busy, and the next window starts clean") {
        Api t([](SubmissionApiConfig& c) { c.checks_per_window = 2; c.check_window_ms = 10000; });
        ASSERT_EQ(t.submit("Map%20One", "203.0.113.1").status, 201);
        ASSERT_EQ(t.submit("Map%20Two", "203.0.113.2").status, 201);
        const HttpResponse busy = t.submit("Map%20Three", "203.0.113.3");
        ASSERT_TRUE(busy.status == 503 && parse_text(busy.body).get("error").str() == "busy");
        t.now_ms += 10000;
        ASSERT_EQ(t.submit("Map%20Three", "203.0.113.3").status, 201);
        Api timed([](SubmissionApiConfig& c) { c.check_budget_ms = 0; });                       // (a budget of no time at all: nothing is checked)
        ASSERT_EQ(timed.submit("Map%20One").status, 503);
    } TEST_END();

    TEST_CASE("API1.6b One address cannot use the checking window of the others: after its share (two) it is told busy while another address is still checked; junk that is no map by its version word uses none of the window") {
        Api t;
        const core::Result blank = core::blank(31, 31, 485, 10, 10, core::Profile::NewMap);       // (a map that does not pass: it takes a real check)
        ASSERT_EQ(blank.status, 0);
        ASSERT_EQ(t.submit("Bad%20One", "203.0.113.1", "", blank.payload).status, 422);
        ASSERT_EQ(t.submit("Bad%20Two", "203.0.113.1", "", blank.payload).status, 422);
        const HttpResponse third = t.submit("Bad%20Three", "203.0.113.1", "", blank.payload);
        ASSERT_TRUE(third.status == 503 && parse_text(third.body).get("error").str() == "busy");
        ASSERT_EQ(t.submit("Good%20Map", "203.0.113.2").status, 201);                           // (the window of the others is whole)
        Api flood;
        for (int i = 0; i < 30; ++i) {
            const HttpResponse junk = flood.submit("Junk%20" + std::to_string(i), "198.51.100." + std::to_string(i + 1), "", std::string("\x01\x00\x00\x00junk", 8));
            ASSERT_TRUE(junk.status == 400 && parse_text(junk.body).get("error").str() == "not_a_map");
        }
        ASSERT_EQ(flood.submit("Good%20Map", "203.0.113.2").status, 201);                       // (thirty junk files from thirty addresses did not make the server busy)
    } TEST_END();

    TEST_CASE("API1.6c The time of the checks is the budget: a send that takes longer than it closes the window (the next send is answered 503 busy, with the code and a sentence), and the next window starts clean") {
        Api t([](SubmissionApiConfig& c) { c.check_budget_ms = 1; c.checks_per_window = 100; c.checks_per_address_window = 100; });
        const std::string slow = grown_tiny(256, 256);                                               // (checking this big map takes about ten milliseconds: well over the 1 ms of the budget, and a slower machine only takes longer)
        ASSERT_TRUE(!slow.empty());
        HttpRequest big = t.req("POST", "/submissions", "kind=extended&name=Slow%20Map&maker=Mia", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.1"}}, slow);
        const HttpResponse first = t.call(big);
        ASSERT_MSG(first.status == 201, std::to_string(first.status) + " " + first.body.substr(0, 160));      // (the window was open: a send is checked whatever it costs)
        const HttpResponse busy = t.submit("Quick%20Map", "203.0.113.2");
        ASSERT_MSG(busy.status == 503, std::to_string(busy.status) + " " + busy.body.substr(0, 160));
        const ctl::JsonValue body = parse_text(busy.body);
        ASSERT_TRUE(body.get("error").str() == "busy" && body.get("message").str().size() > 10);
        ASSERT_EQ(t.submit("Quick%20Map", "203.0.113.2").status, 503);                               // (it stays closed for the rest of the window)
        t.now_ms += 10000;
        ASSERT_EQ(t.submit("Quick%20Map", "203.0.113.2").status, 201);                               // (the next window: the time is counted again from nothing)
    } TEST_END();

    TEST_CASE("API2.4 The admin page's whole job through the API: the four lists and their counts, the next name, approve with and without a new name, decline, take down, put back, the picture, and the lobby is told when it changes") {
        Api t;
        std::vector<SubmitOutcome> sent;
        for (const char* name : {"First%20Map", "Second%20Map", "Third%20Map", "Tiny"}) {
            const HttpResponse r = t.submit(name, "203.0.113." + std::to_string(sent.size() + 1));
            ASSERT_EQ(r.status, 201);
            SubmitOutcome o;
            o.submission.id = parse_text(r.body).get("submission").get("id").str();
            sent.push_back(o);
        }
        ctl::JsonValue list = parse_text(t.owner("GET", "/admin/api/maps").body);
        ASSERT_TRUE(list.get("state").str() == "waiting" && list.get("maps").size() == 4 && list.get("counts").get("waiting").as_int_or(0) == 4);
        ASSERT_TRUE(list.get("untrusted").as_bool_or(false) && list.get("untrusted_fields").is_array() && list.get("untrusted_fields").size() == 6);          // (the list says which fields are player text)
        ASSERT_TRUE(list.get("untrusted_fields").at(0).str() == "name" && list.get("untrusted_fields").at(1).str() == "maker" && list.get("untrusted_fields").at(2).str() == "note" && list.get("untrusted_fields").at(3).str() == "description" && list.get("untrusted_fields").at(4).str() == "approved_name" && list.get("untrusted_fields").at(5).str() == "decided_by");
        ASSERT_TRUE(list.get("counts").get("approved").as_int_or(-1) == 0 && list.get("counts").get("declined").as_int_or(-1) == 0 && list.get("counts").get("taken_down").as_int_or(-1) == 0);
        const ctl::JsonValue* first_map = nullptr;
        for (size_t i = 0; i < list.get("maps").size(); ++i) {
            if (list.get("maps").at(i).get("name").str() == "First Map") first_map = &list.get("maps").at(i);
        }
        ASSERT_TRUE(first_map != nullptr);
        const ctl::JsonValue& first = *first_map;
        ASSERT_TRUE(first.get("maker").str() == "Mia" && first.get("note").str() == "hello" && first.get("width").as_int_or(0) == 31 && first.get("kind").str() == "classic");
        ASSERT_TRUE(!first.get("name_taken").as_bool_or(true) && first.get("has_picture").as_bool_or(false) && first.get("findings").is_array());
        bool tiny_flagged = false;
        for (size_t i = 0; i < list.get("maps").size(); ++i) tiny_flagged = tiny_flagged || (list.get("maps").at(i).get("name").str() == "Tiny" && list.get("maps").at(i).get("name_taken").as_bool_or(false));
        ASSERT_TRUE(tiny_flagged);                                                                // (a name that is a library map's is flagged for the owner)
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps", "state=bogus").body).get("error").str(), std::string("bad_state"));
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/next-name", "name=Tiny").body).get("name").str(), std::string("Tiny 2"));
        ASSERT_EQ(t.owner("GET", "/admin/api/next-name", "name=a%2Fb").status, 400);
        ASSERT_EQ(t.owner("GET", "/admin/api/next-name").status, 400);
        ASSERT_EQ(t.owner("GET", "/admin/api/maps/" + sent[0].submission.id + "/picture").status, 404);          // (no picture is made yet: the page shows its empty square)
        t.make_pictures({sent[0].submission.id});
        const HttpResponse picture = t.owner("GET", "/admin/api/maps/" + sent[0].submission.id + "/picture");
        ASSERT_TRUE(picture.status == 200 && picture.content_type == "image/png" && picture.body.compare(0, 4, "\x89PNG") == 0);
        ASSERT_EQ(t.owner("GET", "/admin/api/maps/" + std::string(32, 'e') + "/picture").status, 404);
        ASSERT_EQ(t.owner("GET", "/admin/api/maps/../../etc/passwd/picture").status, 404);
        const std::string p0 = "/admin/api/maps/" + sent[0].submission.id;
        HttpResponse r = t.owner("POST", p0 + "/approve", "", "{\"name\":\"First Map Plus\",\"message\":\"Longer name\"}");
        ASSERT_TRUE(r.status == 200 && parse_text(r.body).get("map").get("approved_name").str() == "First Map Plus" && parse_text(r.body).get("map").get("renamed_by_owner").as_bool_or(false));
        ASSERT_EQ(t.refreshed, 1);
        r = t.owner("POST", "/admin/api/maps/" + sent[3].submission.id + "/approve", "", "{}");           // (Tiny is the library's)
        ASSERT_TRUE(r.status == 409 && parse_text(r.body).get("error").str() == "name_taken" && parse_text(r.body).get("suggestion").str() == "Tiny 2");
        ASSERT_EQ(t.refreshed, 1);
        r = t.owner("POST", "/admin/api/maps/" + sent[3].submission.id + "/approve", "", "{\"name\":\"Tiny 2\"}");
        ASSERT_EQ(r.status, 200);
        r = t.owner("POST", "/admin/api/maps/" + sent[1].submission.id + "/decline", "", "{\"reason\":\"Too plain\"}");
        ASSERT_TRUE(r.status == 200 && parse_text(r.body).get("map").get("state").str() == "declined");
        ASSERT_EQ(t.owner("POST", "/admin/api/maps/" + sent[2].submission.id + "/decline", "", "{}").status, 400);
        const int before = t.refreshed;
        ASSERT_EQ(t.owner("POST", "/admin/api/maps/" + sent[2].submission.id + "/decline", "", "{\"reason\":\"<b>no</b>\"}").status, 400);
        ASSERT_EQ(t.refreshed, before);                                                           // (a decline changes nothing of the lobby's list)
        list = parse_text(t.owner("GET", "/admin/api/maps", "state=approved").body);
        ASSERT_TRUE(list.get("maps").size() == 2 && list.get("counts").get("approved").as_int_or(0) == 2 && list.get("counts").get("declined").as_int_or(0) == 1 && list.get("counts").get("waiting").as_int_or(0) == 1);
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps", "state=declined").body).get("maps").at(0).get("message").str(), std::string("Too plain"));
        r = t.owner("POST", p0 + "/take-down", "", "{}");
        ASSERT_TRUE(r.status == 200 && parse_text(r.body).get("map").get("state").str() == "taken_down");
        ASSERT_EQ(t.refreshed, 3);
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps", "state=taken_down").body).get("maps").size(), size_t{1});
        ASSERT_EQ(t.owner("POST", p0 + "/take-down", "", "{}").status, 409);
        ASSERT_EQ(t.owner("POST", p0 + "/put-back", "", "{}").status, 200);
        ASSERT_EQ(t.refreshed, 4);
        ASSERT_EQ(t.owner("POST", p0 + "/put-back", "", "{}").status, 409);
        ASSERT_EQ(t.owner("POST", p0 + "/dance", "", "{}").status, 404);
        ASSERT_EQ(t.owner("POST", "/admin/api/maps/" + std::string(32, 'f') + "/approve", "", "{}").status, 404);
        ASSERT_EQ(t.owner("POST", "/admin/api/maps/nonsense/approve", "", "{}").status, 404);
        ASSERT_EQ(t.owner("POST", "/admin/api/maps", "", "{}").status, 404);
        ASSERT_EQ(t.owner("POST", "/admin/api/elsewhere", "", "{}").status, 404);
    } TEST_END();

    TEST_CASE("API2.5 What the admin page is given holds only what was cleaned (no angle bracket, no control character), so that even a page that forgot to escape would show text; a note with quotes and an ampersand is JSON-escaped") {
        Api t;
        HttpResponse r = t.submit("Quote%20Map", "203.0.113.5", "", "", "");
        ASSERT_EQ(r.status, 201);
        HttpRequest q = t.req("POST", "/submissions", "kind=classic&name=Quote%20Map%202&maker=%22Mia%22%20%26%20co&note=say%20%22hi%22%20%26%20%27bye%27%5C", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.6"}}, tiny());
        r = t.call(q);
        ASSERT_EQ(r.status, 201);
        const std::string body = t.owner("GET", "/admin/api/maps").body;
        ASSERT_TRUE(body.find('<') == std::string::npos && body.find('>') == std::string::npos);
        const ctl::JsonValue list = parse_text(body);
        bool found = false;
        for (size_t i = 0; i < list.get("maps").size(); ++i) {
            if (list.get("maps").at(i).get("name").str() == "Quote Map 2") {
                found = true;
                ASSERT_TRUE(list.get("maps").at(i).get("maker").str() == "\"Mia\" & co" && list.get("maps").at(i).get("note").str() == "say \"hi\" & 'bye'\\");
            }
        }
        ASSERT_TRUE(found);
        for (const char* attack : {"%3Cscript%3Ealert(1)%3C%2Fscript%3E", "%3Cimg%20src%3Dx%20onerror%3Dalert(1)%3E", "javascript%3Aalert(1)%3C", "%0D%0ASet-Cookie%3A%20x%3D1"}) {
            q.query = std::string("kind=classic&name=Attack%20Map&maker=Mia&note=") + attack;
            ASSERT_EQ(t.call(q).status, std::string(attack).find("Set-Cookie") != std::string::npos ? 201 : 400);          // (a line end is allowed in a note: the text "Set-Cookie: x=1" is only text, in a JSON string)
            q.query = std::string("kind=classic&maker=Mia&name=") + attack;
            const HttpResponse as_name = t.call(q);
            ASSERT_MSG(as_name.status == 400, std::string(attack) + " -> " + std::to_string(as_name.status) + " " + as_name.body);
            q.query = std::string("kind=classic&name=Attack%20Map&maker=") + attack;
            const HttpResponse as_maker = t.call(q);
            ASSERT_MSG(as_maker.status == 400, std::string(attack) + " -> " + std::to_string(as_maker.status) + " " + as_maker.body);
        }
    } TEST_END();

    TEST_CASE("API2.6 Player text that looks like an order or like the owner is refused with a reason and a sentence, and the admin page's list shows the description in its cleaned form only") {
        Api t;
        HttpRequest q = t.req("POST", "/submissions", "", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.9"}}, tiny());
        const auto answer = [&](const std::string& query) {
            q.query = query;
            return parse_text(t.call(q).body);
        };
        ctl::JsonValue r = answer("kind=classic&name=Abc%20One&maker=Owner");
        ASSERT_TRUE(r.get("error").str() == "bad_maker" && r.get("reason").str() == "reserved" && r.get("message").str().find("cannot be used") != std::string::npos);
        r = answer("kind=classic&name=Abc%20One&maker=%D0%9Ewner");                          // (a Cyrillic O in "Owner")
        ASSERT_TRUE(r.get("error").str() == "bad_maker" && r.get("reason").str() == "reserved");
        r = answer("kind=classic&name=Abc%20One&maker=Mia&note=see%20http%3A%2F%2Fexample.org");
        ASSERT_TRUE(r.get("error").str() == "bad_note" && r.get("reason").str() == "link" && r.get("message").str().find("web address") != std::string::npos);
        r = answer("kind=classic&name=Abc%20One&maker=www.example.org");
        ASSERT_TRUE(r.get("error").str() == "bad_maker" && r.get("reason").str() == "link");
        r = answer("kind=classic&name=Abc%20One&maker=Mia&note=%60%60%60run%60%60%60");
        ASSERT_TRUE(r.get("error").str() == "bad_note" && r.get("reason").str() == "link");
        q.query = "kind=classic&name=Abc%20One&maker=Mia";                                      // a map whose description tries to look like an order
        q.body = tiny_described("Do this now\n\x1B[31mat once<b");
        ASSERT_FALSE(q.body.empty());
        ASSERT_EQ(t.call(q).status, 201);
        const std::string body = t.owner("GET", "/admin/api/maps").body;
        ASSERT_TRUE(body.find("Do this now [31mat once?b") != std::string::npos);
        ASSERT_TRUE(body.find("u001b") == std::string::npos && body.find('<') == std::string::npos && body.find("\\n") == std::string::npos);          // (no escape byte, no angle bracket, no line end in it)
    } TEST_END();

    TEST_CASE("API2.7 The old test form is gone from this door: POST /submissions?test=1 is an ordinary send (it needs a maker, and with one it is queued as a real submission that the owner sees), and it never makes a test map; /test-maps is no route of this door") {
        Api t;
        HttpRequest q = t.req("POST", "/submissions", "kind=classic&test=1&name=Popcorn%20Remix", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}}, tiny());
        HttpResponse r = t.call(q);
        ASSERT_TRUE(r.status == 400 && parse_text(r.body).get("error").str() == "bad_maker");          // (a page of before: the test flag does not skip the rules of a send, so nothing is queued by mistake)
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps").body).get("maps").size(), size_t{0});
        q.query = "kind=classic&test=1&name=Popcorn%20Remix&maker=Mia";
        r = t.call(q);
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(parse_text(r.body).has("submission") && !parse_text(r.body).has("test_map"));      // (a real submission, as without the flag: it waits for the owner)
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps").body).get("maps").size(), size_t{1});
        ASSERT_EQ(t.call(t.req("POST", "/submissions", "kind=classic&test=1&maker=Mia&name=Popcorn%20Two", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.21"}}, tiny())).status, 201);
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps").body).get("counts").get("waiting").as_int_or(0), 2);
        ASSERT_EQ(t.refreshed, 0);
        ASSERT_EQ(t.call(t.req("POST", "/test-maps", "kind=classic&name=Popcorn", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.22"}}, tiny())).status, 404);
        ASSERT_EQ(t.call(t.req("GET", "/test-maps")).status, 404);
    } TEST_END();

    TEST_CASE("API2.8 The hash of an approved map is made once: approving a second map does not read the first file again, and a file that changed is hashed again") {
        Api t;
        const HttpResponse sent = t.submit("First%20Run", "203.0.113.50");
        ASSERT_EQ(sent.status, 201);
        const std::string id = parse_text(sent.body).get("submission").get("id").str();
        ASSERT_EQ(t.owner("POST", "/admin/api/maps/" + id + "/approve", "", "{}").status, 200);
        std::vector<ApprovedMap> a = t.store->approved_maps();
        ASSERT_EQ(a.size(), size_t{1});
        const std::string path = a[0].path;
        const uint64_t hash = a[0].hash;
        const std::string original = [&]() { std::ifstream in(path, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }();
        // the file is replaced by another of the same size and the same time: the cache cannot tell, so this proves the hash is not made again (and is the reason a changed size or time matters)
        const auto when = fs::last_write_time(path);
        { std::string other = original; other[other.size() - 1] = static_cast<char>(other[other.size() - 1] ^ 0x55); std::ofstream out(path, std::ios::binary | std::ios::trunc); out << other; }
        fs::last_write_time(path, when);
        a = t.store->approved_maps();
        ASSERT_EQ(a.size(), size_t{1});
        ASSERT_EQ(a[0].hash, hash);                                                    // (the same size and time: the hash that was made is used)
        { std::ofstream out(path, std::ios::binary | std::ios::trunc); out << original << "more"; }
        fs::last_write_time(path, when);                                              // (the old time again: only the size tells)
        a = t.store->approved_maps();
        uint64_t again = 0;
        ASSERT_TRUE(net::hash_file(path, again));
        ASSERT_TRUE(a.size() == 1 && a[0].hash == again && again != hash);              // (a file with another size is hashed again)
        { std::ofstream out(path, std::ios::binary | std::ios::trunc); out << original << "MORE"; }       // (the size of the file that was hashed last, other bytes ...)
        fs::last_write_time(path, when + std::chrono::hours(1));                      // (... and another time)
        a = t.store->approved_maps();
        uint64_t third = 0;
        ASSERT_TRUE(net::hash_file(path, third));
        ASSERT_TRUE(a.size() == 1 && a[0].hash == third && third != hash && third != again);
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

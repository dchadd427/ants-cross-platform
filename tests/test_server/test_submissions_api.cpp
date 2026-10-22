// Tests of the routes of the map submissions (SubmissionApi, docs/MAP_SUBMISSIONS.md): what a player's browser may do and how it is limited by address, and what only a signed-in account may do: the session cookie that
// the server signs at the sign-in (every way to forge, change, age or misuse one), the CSRF token, the answer that a visitor gets for everything of the owner's (the answer of a path that does not exist, byte for byte),
// the decisions through the API, and the page's text (nothing is HTML). The accounts themselves, the roles and the guessing limits are test_admin_accounts. Requests are made by hand: no sockets.
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
#include "ants_server/admin_accounts.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/online_limits.hpp"
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

const char* const kSecret = "0123456789abcdef0123456789abcdef0123";
const char* const kOwnerPassword = "correct horse battery staple";

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

// A cookie as this server makes one, for a user id and generation (the tests forge with it: a wrong secret, a wrong generation, a wrong time)
std::string cookie_value(const std::string& secret, int64_t uid, int64_t gen, int64_t exp, const std::string& nonce = "n0nce", const std::string& prefix = "v2.") {
    const std::string payload = "{\"u\":" + std::to_string(uid) + ",\"g\":" + std::to_string(gen) + ",\"exp\":" + std::to_string(exp) + (nonce.empty() ? "" : ",\"n\":\"" + nonce + "\"") + "}";
    const std::string signed_part = prefix + base64url_encode(payload);
    return signed_part + "." + hex_encode(hmac_sha256(secret, signed_part));
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
    std::unique_ptr<AdminAccounts> accounts;
    std::unique_ptr<SubmissionApi> api;
    int64_t owner_id{0};
    std::string owner_cookie;
    int64_t now{kT0};
    uint32_t now_ms{1000};
    int refreshed{0};
    int test_refreshed{0};

    explicit Api(const std::function<void(SubmissionApiConfig&)>& tune = nullptr, bool owner_on = true) {
        open_server_db(database, "");
        config.dir = (dir.path / "s").string();
        config.min_free_bytes = 1;
        config.per_address_per_day = 1000;
        config.per_day_total = 100000;
        config.max_waiting = 1000;
        store = std::make_unique<SubmissionStore>(database, config, [](const std::string& key) { return library().library_has_name(key); });
        std::string why;
        store->open(now, why);
        AccountConfig account_config;
        account_config.rounds = 1000;                                        // (the real cost is 600,000 rounds: test_admin_accounts holds that, and one check of it)
        accounts = std::make_unique<AdminAccounts>(database, account_config);
        accounts->open(why);
        User dave;
        std::string starting;
        Failure refusal;
        int64_t gen = 0;
        accounts->create(Actor(), "dave", "admin", now, dave, starting, refusal);
        accounts->change_password(Actor(), dave.id, kOwnerPassword, now, gen, refusal);            // (the starting password is changed: the account is as the owner uses it)
        owner_id = dave.id;
        SubmissionApiConfig ac;
        if (owner_on) ac.session_secret = kSecret;
        ac.attempts_per_hour = 1000;
        if (tune) tune(ac);
        api = std::make_unique<SubmissionApi>(*store, *accounts, ac);
        api->set_on_approved_changed([this] { ++refreshed; });
        api->set_on_test_changed([this] { ++test_refreshed; });
    }

    HttpResponse call(const HttpRequest& r) { return api->handle(r, now, now_ms); }

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

    // A test upload (POST /submissions?test=1) of `bytes` as a map of `kind`
    HttpResponse test_upload(const std::string& bytes, const std::string& name = "Popcorn", const std::string& address = "203.0.113.20", const std::string& kind = "classic") {
        return call(req("POST", "/submissions", "kind=" + kind + "&test=1&name=" + name, {{"content-type", "application/octet-stream"}, {"x-client-address", address}}, bytes));
    }

    // The same, to the store without the door: what the store keeps (the door's own limits are not asked). Moves the clock `later` seconds first.
    TestSubmitOutcome keep_test(const std::string& address, int64_t later = 0, const std::string& bytes = std::string(), const std::string& name = "Popcorn") {
        now += later;
        SubmitInput in;
        in.kind = "classic";
        in.name = name;
        in.bytes = bytes.empty() ? tiny() : bytes;
        in.address = address;
        return store->submit_test(in, now);
    }

    // Signs in through the API and returns the cookie pair ("ants_admin=v2...."), "" when it fails. Each sign-in moves the steady clock on, so that the budget of password checks is a new window.
    std::string login_cookie(const std::string& name, const std::string& password, const std::string& address = "198.51.100.7", HttpResponse* answer = nullptr) {
        now_ms += 20000;
        const HttpResponse r = call(req("POST", "/admin/api/login", "", {{"content-type", "application/json"}, {"x-client-address", address}, {"sec-fetch-site", "same-origin"}}, "{\"username\":\"" + name + "\",\"password\":\"" + password + "\"}"));
        if (answer != nullptr) *answer = r;
        return r.status == 200 && !r.set_cookies.empty() ? r.set_cookies[0].substr(0, r.set_cookies[0].find(';')) : std::string();
    }

    std::string cookie() {
        if (owner_cookie.empty()) owner_cookie = login_cookie("dave", kOwnerPassword);
        return owner_cookie;
    }

    std::string forged(int64_t uid, int64_t gen, int64_t exp, const std::string& nonce = "n0nce", const std::string& secret = kSecret, const std::string& prefix = "v2.") { return "ants_admin=" + cookie_value(secret, uid, gen, exp, nonce, prefix); }

    ctl::JsonValue me(const std::string& cookie_header) {
        std::map<std::string, std::string> h;
        if (!cookie_header.empty()) h["cookie"] = cookie_header;
        return parse_text(call(req("GET", "/admin/api/me", "", h)).body);
    }

    // an owner's call: the cookie and the CSRF token of that cookie
    HttpResponse owner(const std::string& method, const std::string& path, const std::string& query = "", const std::string& body = "") {
        const std::string c = cookie();
        std::map<std::string, std::string> h = {{"cookie", c}};
        if (method == "POST") {
            h["content-type"] = "application/json";
            h["x-ants-csrf"] = me(c).get("csrf").str();
        }
        return call(req(method, path, query, h, body));
    }
};

}  // namespace

int main() {
    std::cout << "=======================================================\n MAP SUBMISSIONS: THE ROUTES AND THE OWNER'S SESSION\n=======================================================\n";

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

    TEST_CASE("API2.1 The session cookie: a sign-in makes it (HttpOnly, Secure, SameSite=Strict, 12 hours); a changed, forged, old, over-long or out-of-date one is nobody; the generation of the user's sessions decides") {
        Api t;
        ctl::JsonValue m = t.me("");
        ASSERT_TRUE(!m.get("signed_in").as_bool_or(true) && !m.has("csrf") && !m.has("name") && !m.has("role"));
        HttpResponse answer;
        const std::string pair = t.login_cookie("dave", kOwnerPassword, "198.51.100.7", &answer);
        ASSERT_FALSE(pair.empty());
        const std::string& header = answer.set_cookies[0];
        for (const char* flag : {"; Path=/admin", "; Max-Age=43200", "; HttpOnly", "; Secure", "; SameSite=Strict"}) ASSERT_MSG(header.find(flag) != std::string::npos, std::string("missing ") + flag);
        ASSERT_TRUE(header.compare(0, 14, "ants_admin=v2.") == 0 && answer.set_cookies.size() == 1);
        ASSERT_TRUE(answer.body.find(kOwnerPassword) == std::string::npos);                       // (the answer never holds the password)
        const ctl::JsonValue login = parse_text(answer.body);
        ASSERT_TRUE(login.get("signed_in").as_bool_or(false) && login.get("name").str() == "dave" && login.get("role").str() == "admin" && !login.get("must_change").as_bool_or(true) && login.get("csrf").str().size() == 32);
        const uint64_t compares = ctl::constant_time_equal_calls();
        m = t.me(pair);
        ASSERT_EQ(ctl::constant_time_equal_calls(), compares + 1);                                  // (the signature of the cookie is compared in constant time)
        ASSERT_TRUE(m.get("signed_in").as_bool_or(false) && m.get("role").str() == "admin" && m.get("name").str() == "dave" && m.get("csrf").str() == login.get("csrf").str() && !m.has("owner"));
        m = t.me("other=1; " + pair + "; more=2");
        ASSERT_TRUE(m.get("signed_in").as_bool_or(false));                                       // (among other cookies)
        const std::string good = pair.substr(11);
        const auto signed_in = [&](const std::string& header_text) { return t.me(header_text).get("signed_in").as_bool_or(false); };
        ASSERT_FALSE(signed_in("ants_admin="));
        ASSERT_FALSE(signed_in("ants_admin=" + good + "x"));                                      // (a changed signature)
        ASSERT_FALSE(signed_in("ants_admin=" + good.substr(0, good.size() - 1)));
        std::string flipped = good;
        flipped[flipped.size() - 1] = flipped[flipped.size() - 1] == '0' ? '1' : '0';
        ASSERT_FALSE(signed_in("ants_admin=" + flipped));
        std::string changed_payload = good;
        changed_payload[5] = changed_payload[5] == 'A' ? 'B' : 'A';                               // (a changed payload with the old signature)
        ASSERT_FALSE(signed_in("ants_admin=" + changed_payload));
        ASSERT_FALSE(signed_in("ants_admin=v3" + good.substr(2)));
        ASSERT_FALSE(signed_in("ants_admin=" + good.substr(3)));
        ASSERT_FALSE(signed_in("Ants_Admin=" + good));                                            // (the name is exact)
        ASSERT_FALSE(signed_in("xants_admin=" + good));
        ASSERT_FALSE(signed_in("ants_admin2=" + good));
        ASSERT_FALSE(signed_in("ants_admin=\"" + good + "\""));
        const int64_t uid = t.owner_id;
        ASSERT_TRUE(signed_in(t.forged(uid, 1 + 1, t.now + 3600)));                                // (the user's generation is 2 after the password change: a cookie of that generation is good)
        ASSERT_FALSE(signed_in(t.forged(uid, 1, t.now + 3600)));                                   // (the old generation: every session from before the password change is over)
        ASSERT_FALSE(signed_in(t.forged(uid, 3, t.now + 3600)));                                   // (a generation that does not exist yet)
        ASSERT_FALSE(signed_in(t.forged(uid, 0, t.now + 3600)));
        ASSERT_FALSE(signed_in(t.forged(uid + 1, 2, t.now + 3600)));                               // (no such user)
        ASSERT_FALSE(signed_in(t.forged(0, 2, t.now + 3600)));
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now + 3600, "n0nce", "another-secret-another-secret-another")));        // (signed with another secret)
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now - 1)));                                      // (over)
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now)));
        ASSERT_TRUE(signed_in(t.forged(uid, 2, t.now + 43200)));                                   // (12 hours: what this server makes)
        ASSERT_TRUE(signed_in(t.forged(uid, 2, t.now + 43200 + 60)));
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now + 43200 + 61)));                             // (claims to last longer than a session of this server)
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now + 3 * 86400)));
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now + 3600, "")));                               // (no nonce)
        ASSERT_FALSE(signed_in(t.forged(uid, 2, t.now + 3600, "n0nce", kSecret, "v1.")));          // (the format of the cookie of the sign-in that this one replaced)
        ASSERT_FALSE(signed_in("ants_admin=" + std::string(3000, 'a')));                         // (a huge cookie header is not even read)
        ASSERT_FALSE(signed_in(std::string(3000, ';') + pair));
        // the cookie ends when its time does: 12 hours after the sign-in, to the second
        const std::string made = t.login_cookie("dave", kOwnerPassword);
        t.now += 43199;
        ASSERT_TRUE(signed_in(made));
        t.now += 1;
        ASSERT_FALSE(signed_in(made));
        t.now -= 43200;
        // a session ends when the user is disabled, enabled again does not bring it back, and a removed user has none
        Api u;
        const std::string before = u.cookie();
        ASSERT_TRUE(u.me(before).get("signed_in").as_bool_or(false));
        User second;
        std::string pw;
        Failure why;
        ASSERT_MSG(u.accounts->create(Actor(), "second", "admin", u.now, second, pw, why), why.code + why.message);
        ASSERT_TRUE(u.accounts->set_disabled(Actor(), u.owner_id, true, u.now, why));
        ASSERT_FALSE(u.me(before).get("signed_in").as_bool_or(false));
        ASSERT_TRUE(u.accounts->set_disabled(Actor(), u.owner_id, false, u.now, why));
        ASSERT_FALSE(u.me(before).get("signed_in").as_bool_or(false));
        ASSERT_TRUE(u.me(u.login_cookie("dave", kOwnerPassword)).get("signed_in").as_bool_or(false));
        Api same_secret;
        ASSERT_TRUE(same_secret.me(same_secret.cookie()).get("signed_in").as_bool_or(false));
        // a user that is disabled in the database is nobody, whatever the generation of the cookie says (a second lock behind the generation)
        Api d;
        const std::string alive = d.cookie();
        ASSERT_TRUE(d.me(alive).get("signed_in").as_bool_or(false));
        ASSERT_TRUE(d.database.exec("UPDATE admin_users SET disabled = 1").ok());
        ASSERT_FALSE(d.me(alive).get("signed_in").as_bool_or(true));
    } TEST_END();

    TEST_CASE("API2.2 A visitor and a stranger get, for every call of the owner's, the answer of a path that does not exist, byte for byte; so does everything when the owner's page is not set up") {
        Api t;
        const std::string some_id(32, 'a');
        const std::vector<std::pair<std::string, std::string>> owner_calls = {
            {"GET", "/admin/api/maps"}, {"GET", "/admin/api/maps?state=approved"}, {"GET", "/admin/api/next-name"}, {"GET", "/admin/api/maps/" + some_id + "/picture"},
            {"POST", "/admin/api/maps/" + some_id + "/approve"}, {"POST", "/admin/api/maps/" + some_id + "/decline"}, {"POST", "/admin/api/maps/" + some_id + "/take-down"},
            {"POST", "/admin/api/maps/" + some_id + "/put-back"}, {"POST", "/admin/api/maps/x/y"}, {"GET", "/admin/api/users"}, {"POST", "/admin/api/users"}, {"POST", "/admin/api/users/1/reset"},
            {"POST", "/admin/api/users/1/disable"}, {"POST", "/admin/api/users/1/enable"}, {"POST", "/admin/api/users/1/remove"}, {"GET", "/admin/api/audit"}, {"POST", "/admin/api/password"}};
        const HttpResponse nothing = t.call(t.req("GET", "/admin/api/does-not-exist"));
        ASSERT_EQ(nothing.status, 404);
        const std::vector<std::string> cookies = {"", t.forged(t.owner_id + 5, 2, t.now + 3600), t.forged(t.owner_id, 2, t.now - 5), t.forged(t.owner_id, 1, t.now + 3600), "ants_admin=garbage"};
        for (const auto& call : owner_calls) {
            for (const std::string& c : cookies) {
                std::map<std::string, std::string> h;
                if (!c.empty()) h["cookie"] = c;
                if (call.first == "POST") {
                    h["content-type"] = "application/json";
                    h["x-ants-csrf"] = "whatever";
                }
                const std::string path = call.second.substr(0, call.second.find('?'));
                const std::string query = call.second.find('?') == std::string::npos ? "" : call.second.substr(call.second.find('?') + 1);
                const HttpResponse r = t.call(t.req(call.first, path, query, h));
                ASSERT_MSG(r.status == nothing.status && r.body == nothing.body && r.content_type == nothing.content_type && r.set_cookies.empty(), call.first + " " + call.second + " with cookie '" + c.substr(0, 20) + "'");
            }
        }
        for (const char* method : {"PUT", "DELETE", "PATCH"}) ASSERT_EQ(t.call(t.req(method, "/admin/api/me")).body, nothing.body);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api")).body, nothing.body);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/login")).body, nothing.body);                  // (the sign-in is a POST; any other method is a missing path)
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/logout")).body, nothing.body);
        Api off(nullptr, false);
        for (const auto& call : owner_calls) {
            const HttpResponse r = off.call(off.req(call.first, call.second.substr(0, call.second.find('?')), "", {{"cookie", t.cookie()}}));
            ASSERT_MSG(r.status == 404 && r.body == nothing.body, call.second);
        }
        ASSERT_EQ(off.call(off.req("GET", "/admin/api/me", "", {{"cookie", t.cookie()}})).body, nothing.body);        // (not even /me: there is nothing to say)
        const HttpResponse off_login = off.call(off.req("POST", "/admin/api/login", "", {{"content-type", "application/json"}, {"x-client-address", "198.51.100.7"}}, std::string("{\"username\":\"dave\",\"password\":\"") + kOwnerPassword + "\"}"));
        ASSERT_TRUE(off_login.status == 404 && off_login.body == nothing.body && off_login.set_cookies.empty());           // (no secret, no sign-in)
        Api half([](SubmissionApiConfig& c) { c.session_secret = "short"; });                                          // (a secret that is too short is no secret)
        ASSERT_EQ(half.call(half.req("GET", "/admin/api/me")).status, 404);
        // A secret of 31 characters is too few, 32 are enough (the check is on the length, not only on "a request without a cookie")
        const std::string s31(31, 'k'), s32(32, 'k');
        Api short31([&](SubmissionApiConfig& c) { c.session_secret = s31; });
        ASSERT_EQ(short31.call(short31.req("GET", "/admin/api/me", "", {{"cookie", short31.forged(short31.owner_id, 2, short31.now + 3600, "n0nce", s31)}})).body, nothing.body);
        ASSERT_EQ(short31.call(short31.req("GET", "/admin/api/maps", "", {{"cookie", short31.forged(short31.owner_id, 2, short31.now + 3600, "n0nce", s31)}})).body, nothing.body);
        Api short32([&](SubmissionApiConfig& c) { c.session_secret = s32; });
        ASSERT_TRUE(short32.me(short32.forged(short32.owner_id, 2, short32.now + 3600, "n0nce", s32)).get("signed_in").as_bool_or(false));
        // The picture of a map that exists is for a signed-in account alone: a visitor and a stranger get the answer of a missing path
        SubmitInput in;
        in.kind = "classic";
        in.name = "River Run";
        in.maker = "Mia";
        in.bytes = tiny();
        in.address = "203.0.113.5";
        const SubmitOutcome sent = t.store->submit(in, t.now);
        ASSERT_TRUE(sent.ok);
        const std::string picture = "/admin/api/maps/" + sent.submission.id + "/picture";
        ASSERT_EQ(t.call(t.req("GET", picture)).body, nothing.body);
        ASSERT_EQ(t.call(t.req("GET", picture, "", {{"cookie", t.forged(999, 1, t.now + 3600)}})).body, nothing.body);
        ASSERT_EQ(t.call(t.req("GET", picture)).status, 404);
        ASSERT_EQ(t.call(t.req("GET", picture, "", {{"cookie", t.cookie()}})).status, 200);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api", "", {{"cookie", t.cookie()}})).body, nothing.body);        // ("/admin/api" itself, 10 characters: no route, and no exception for a signed-in user)
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/", "", {{"cookie", t.cookie()}})).status, 404);
    } TEST_END();

    TEST_CASE("API2.3 A change by the owner needs the CSRF token of that very session, a same-origin request, the JSON type and a JSON object; a read does not come from another site") {
        Api t;
        const SubmitOutcome sent = [&] {
            SubmitInput in;
            in.kind = "classic";
            in.name = "River Run";
            in.maker = "Mia";
            in.bytes = tiny();
            in.address = "203.0.113.5";
            return t.store->submit(in, t.now);
        }();
        ASSERT_TRUE(sent.ok);
        const std::string path = "/admin/api/maps/" + sent.submission.id + "/approve";
        const std::string c = t.cookie();
        const std::string csrf = t.me(c).get("csrf").str();
        const auto post = [&](const std::map<std::string, std::string>& extra, const std::string& body = "{}") {
            std::map<std::string, std::string> h = {{"cookie", c}, {"content-type", "application/json"}};
            for (const auto& e : extra) {
                if (e.second == "<erase>") h.erase(e.first);
                else h[e.first] = e.second;
            }
            return t.call(t.req("POST", path, "", h, body));
        };
        ASSERT_EQ(post({}).status, 403);                                                          // (no token)
        ASSERT_EQ(post({{"x-ants-csrf", ""}}).status, 403);
        ASSERT_EQ(post({{"x-ants-csrf", csrf.substr(1)}}).status, 403);
        ASSERT_EQ(post({{"x-ants-csrf", csrf + "0"}}).status, 403);
        const std::string other_session = t.me(t.login_cookie("dave", kOwnerPassword)).get("csrf").str();
        ASSERT_NE(other_session, csrf);
        ASSERT_EQ(post({{"x-ants-csrf", other_session}}).status, 403);                            // (the token of another session)
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"sec-fetch-site", "cross-site"}}).status, 404);       // (from another site: the answer of a path that is not there)
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"sec-fetch-site", "same-site"}}).status, 404);
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"sec-fetch-site", "none"}}).status, 403);                 // (a typed address may read, but a change is made by the page itself: same-origin)
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"content-type", "text/plain"}}).status, 415);
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"content-type", "<erase>"}}).status, 415);
        ASSERT_EQ(post({{"x-ants-csrf", csrf}}, "[1]").status, 400);
        ASSERT_EQ(post({{"x-ants-csrf", csrf}}, "{not json").status, 400);
        ASSERT_EQ(t.text_of_state(sent.submission.id), std::string("waiting"));                  // (nothing of that changed anything)
        ASSERT_EQ(t.refreshed, 0);
        const uint64_t compares = ctl::constant_time_equal_calls();
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"sec-fetch-site", "same-origin"}}).status, 200);
        ASSERT_EQ(ctl::constant_time_equal_calls(), compares + 2);                                // (the cookie's signature and the page's token: each compared in constant time)
        ASSERT_EQ(t.refreshed, 1);
        const HttpResponse cross_read = t.call(t.req("GET", "/admin/api/maps", "", {{"cookie", c}, {"sec-fetch-site", "cross-site"}}));
        ASSERT_EQ(cross_read.status, 404);                                                        // (a read that another site sets off learns nothing)
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/maps", "", {{"cookie", c}, {"sec-fetch-site", "same-origin"}})).status, 200);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/maps", "", {{"cookie", c}, {"sec-fetch-site", "none"}})).status, 200);
        ASSERT_EQ(t.call(t.req("PUT", "/admin/api/maps", "", {{"cookie", c}})).status, 404);
    } TEST_END();

    TEST_CASE("API2.4 The owner's page's whole job through the API: the four lists and their counts, the next name, approve with and without a new name, decline, take down, put back, the picture, and the lobby is told when it changes") {
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

    TEST_CASE("API2.5 What the owner's page is given holds only what was cleaned (no angle bracket, no control character), so that even a page that forgot to escape would show text; a note with quotes and an ampersand is JSON-escaped") {
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

    TEST_CASE("API2.6 Player text that looks like an order or like the owner is refused with a reason and a sentence, and the owner's list shows the description in its cleaned form only") {
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

    TEST_CASE("API2.7 POST /submissions?test=1 saves an unlisted test map for a room without touching the submission queue, resolves in MapStore::find and test_map while staying out of list_json, and expires after kTestMapTtlS") {
        Api t;
        HttpRequest q = t.req("POST", "/submissions", "kind=classic&test=1&name=Popcorn%20Remix", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}}, tiny());
        HttpResponse r = t.call(q);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(t.test_refreshed, 1);
        ASSERT_EQ(t.refreshed, 0);                                           // (a test upload does not make the server read every approved map again)
        const ctl::JsonValue j = parse_text(r.body).get("test_map");
        const std::string file = j.get("file").str();
        ASSERT_TRUE(MapStore::is_test_map_name(file));
        ASSERT_EQ(j.get("name").str(), "Popcorn Remix");
        ASSERT_EQ(j.get("kind").str(), "classic");
        ASSERT_EQ(j.get("usable").size(), size_t{4});
        ASSERT_EQ(parse_text(t.owner("GET", "/admin/api/maps").body).get("maps").size(), size_t{0});        // touches no submission table

        MapStore ms("", "");
        ms.set_test_maps(t.store->test_maps(t.now));
        MapEntry entry;
        ASSERT_TRUE(ms.find(file, entry));
        ASSERT_TRUE(ms.test_map(file) != nullptr);
        ASSERT_EQ(ms.listed_name(file), file);
        ASSERT_TRUE(ms.list_json(nullptr, false).find(file) == std::string::npos && ms.list_json(nullptr, true).find(file) == std::string::npos);

        // expires after kTestMapTtlS
        ASSERT_EQ(t.store->test_maps(t.now + SubmissionStore::kTestMapTtlS).size(), size_t{0});
    } TEST_END();

    TEST_CASE("API2.8 A test map keeps the CANONICAL file, never the bytes that came: trailing junk is gone, the folder is for the server's user only (0700, the files 0600), and the file is the one that the hash and the size in the answer describe") {
        Api t;
        const std::string junk = tiny() + std::string(60000, 'Z');
        const HttpResponse r = t.test_upload(junk);
        ASSERT_EQ(r.status, 201);
        const ctl::JsonValue j = parse_text(r.body).get("test_map");
        MapStore ms("", "");
        ms.set_test_maps(t.store->test_maps(t.now));
        const ApprovedMap* kept = ms.test_map(j.get("file").str());
        ASSERT_TRUE(kept != nullptr);
        std::ifstream in(kept->path, std::ios::binary);
        const std::string stored((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ASSERT_TRUE(stored.size() < junk.size() - 50000 && stored.find("ZZZZZZZZ") == std::string::npos);        // (the junk did not come through)
        ASSERT_EQ(static_cast<uint64_t>(stored.size()), kept->size);
        ASSERT_EQ(j.get("size").as_int_or(0), static_cast<int64_t>(stored.size()));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(kept->path, hash));
        ASSERT_EQ(map_hash_text(hash), j.get("hash").str());
        ASSERT_EQ(t.store->test_maps(t.now)[0].hash, hash);
#ifndef _WIN32
        ASSERT_EQ(static_cast<int>(fs::status(fs::path(t.config.dir) / "test").permissions() & fs::perms::all), static_cast<int>(fs::perms::owner_all));
        ASSERT_EQ(static_cast<int>(fs::status(kept->path).permissions() & fs::perms::all), static_cast<int>(fs::perms::owner_read | fs::perms::owner_write));
#endif
        // a Classic map stays a Classic map (version 8) in the test folder
        ASSERT_EQ(static_cast<unsigned>(static_cast<unsigned char>(stored[0])), 8u);
    } TEST_END();

    TEST_CASE("API2.9 A test map has limits of its own: it never uses the allowance of real submissions, an address may send so many an hour and all addresses together so many, and its checks have a window and a budget that the checks of real maps do not share") {
        {   // the hour of one address: 6 test maps (spaced so that the store keeps them), the 7th is refused; real submissions from the same address are not touched
            Api t([](SubmissionApiConfig& c) {
                c.attempts_per_hour = 2;
                c.test_attempts_per_hour = 6;
            });
            for (int i = 0; i < 6; ++i) {
                t.now += 130;
                t.now_ms += 20000;
                const HttpResponse r = t.test_upload(tiny());
                ASSERT_MSG(r.status == 201, "test upload " + std::to_string(i) + " answered " + std::to_string(r.status));
            }
            t.now += 130;
            t.now_ms += 20000;
            HttpResponse r = t.test_upload(tiny());
            ASSERT_EQ(r.status, 429);
            ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("too_many_tests"));
            ASSERT_EQ(t.submit("Real%20Run%20One").status, 201);                  // (six test maps used none of the two real tries an hour)
            ASSERT_EQ(t.submit("Real%20Run%20Two").status, 201);
            ASSERT_EQ(t.submit("Real%20Run%20Three").status, 429);                 // (and the real limit is still the real one)
        }
        {   // all addresses together
            Api t([](SubmissionApiConfig& c) { c.test_total_per_hour = 3; });
            for (int i = 0; i < 3; ++i) {
                t.now_ms += 20000;
                ASSERT_EQ(t.test_upload(tiny(), "Popcorn", "203.0.113." + std::to_string(30 + i)).status, 201);
            }
            t.now_ms += 20000;
            ASSERT_EQ(t.test_upload(tiny(), "Popcorn", "203.0.113.99").status, 429);
            t.now += 3601;                                                          // (an hour later the allowance is back)
            t.now_ms += 20000;
            ASSERT_EQ(t.test_upload(tiny(), "Popcorn", "203.0.113.99").status, 201);
        }
        {   // the window: one check of test maps for an address in ten seconds, whatever the real checks have used, and the other way round
            Api t;
            ASSERT_EQ(t.test_upload(tiny()).status, 201);
            HttpResponse busy = t.test_upload(tiny());
            ASSERT_EQ(busy.status, 503);
            ASSERT_EQ(parse_text(busy.body).get("error").str(), std::string("busy"));
            ASSERT_EQ(t.submit("Real%20Run%20One").status, 201);                   // (the real window is a window of its own: the same address, the same second)
            t.now_ms += 10001;
            ASSERT_EQ(t.test_upload(tiny()).status, 201);                           // (the next window)
            ASSERT_EQ(t.test_upload(tiny(), "Popcorn", "203.0.113.31").status, 201);       // (another address has the second check of the window)
            ASSERT_EQ(t.test_upload(tiny(), "Popcorn", "203.0.113.32").status, 503);      // (two in all: test_checks_per_window)
        }
    } TEST_END();

    TEST_CASE("API2.10 Refusals of a test map: a file that is no map, a kind that the file is not, an unknown kind, an empty body, a cross-site request, the wrong content type, a map of one team, a map that is bigger than the kind allows; nothing is kept for any of them") {
        Api t;
        const auto expect = [&](const HttpResponse& r, int status, const char* code, const std::string& what) {
            ASSERT_MSG(r.status == status && parse_text(r.body).get("error").str() == code, what + ": " + std::to_string(r.status) + " " + r.body.substr(0, 120));
        };
        const auto step = [&]() { t.now_ms += 20000; };                              // (a new checking window for each)
        expect(t.test_upload("this is not a map file at all, not even close"), 400, "not_a_map", "text");
        step();
        expect(t.test_upload(std::string(4, '\x08')), 400, "not_a_map", "a version word and nothing else");
        step();
        expect(t.test_upload(tiny(), "Popcorn", "203.0.113.20", "extended"), 400, "kind_mismatch", "a Classic file that says it is Extended");
        step();
        expect(t.test_upload(tiny(), "Popcorn", "203.0.113.20", "weird"), 400, "bad_kind", "an unknown kind");
        step();
        expect(t.test_upload(std::string()), 400, "not_a_map", "no body");
        step();
        expect(t.test_upload(tiny_with_teams(0x01)), 422, "map_fails_checks", "a map of one team");
        step();
        expect(t.test_upload(tiny_with_teams(0x00)), 422, "map_fails_checks", "a map of no team");
        step();
        const std::string cut = tiny().substr(0, tiny().size() / 2);
        expect(t.test_upload(cut), 400, "not_a_map", "a map that ends in the middle");
        step();
        expect(t.test_upload(std::string(600000, 'x')), 413, "too_large", "more bytes than a Classic map has");
        ASSERT_EQ(t.call(t.req("POST", "/submissions", "kind=classic&test=1", {{"content-type", "text/plain"}, {"x-client-address", "203.0.113.20"}}, tiny())).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/submissions", "kind=classic&test=1", {{"content-type", "application/octet-stream"}, {"x-client-address", "203.0.113.20"}, {"sec-fetch-site", "cross-site"}}, tiny())).status, 403);
        step();
        ASSERT_EQ(t.call(t.req("POST", "/submissions", "kind=classic&test=1", {{"content-type", "application/octet-stream"}}, tiny())).status, 201);        // (no address header: the connection's counts, 172.18.0.4)
        ASSERT_EQ(t.store->test_maps(t.now).size(), size_t{1});                       // (only the last one, the file without an address header, was a good map)
        {   // a disk with no room left: the upload is refused whole, nothing is kept (the same test as for a real submission)
            SubmissionConfig full = t.config;
            full.min_free_bytes = ~uint64_t{0} >> 1;
            SubmissionStore starved(t.database, full, [](const std::string&) { return false; });
            std::string why;
            ASSERT_TRUE(starved.open(t.now, why));
            SubmitInput in;
            in.kind = "classic";
            in.name = "Popcorn";
            in.bytes = tiny();
            in.address = "203.0.113.77";
            const TestSubmitOutcome o = starved.submit_test(in, t.now);
            ASSERT_TRUE(!o.ok && o.fail.status == 503 && o.fail.code == "storage_full");
            ASSERT_EQ(starved.test_maps(t.now).size(), size_t{0});
        }
        // two teams are enough
        Api two;
        ASSERT_EQ(two.test_upload(tiny_with_teams(0x03)).status, 201);
        ASSERT_EQ(parse_text(two.test_upload(tiny_with_teams(0x05), "Popcorn", "203.0.113.21").body).get("test_map").get("players").as_int_or(0), 2);
    } TEST_END();

    TEST_CASE("API2.11 The name of a test map: a tilde, an angle bracket, a line end, a right-to-left mark or a path cannot get into it, it is cut at 45 characters, and the suffix ' ~test-<8 hex>.lvl' is the server's own") {
        Api t;
        const auto stem_of = [&](const std::string& encoded_name, const std::string& address) {
            t.now_ms += 20000;
            const HttpResponse r = t.test_upload(tiny(), encoded_name, address);
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

    TEST_CASE("API2.12 A map over the size limit of a room (65,536 cells) is refused as a test map with one plain sentence, whatever its kind allows; 256 x 256 is the most; the numbers are OnlineLimits'") {
        ASSERT_TRUE(OnlineLimits::kMaxCells == 65536u && OnlineLimits::allows(65536u) && !OnlineLimits::allows(65537u));          // the limit of a room
        Api t([](SubmissionApiConfig& c) { c.test_checks_per_address_window = 100; c.test_checks_per_window = 100; });
        const auto step = [&]() { t.now_ms += 20000; };
        const std::string fits = grown_tiny(256, 256);
        ASSERT_TRUE(!fits.empty() && fits.size() > 700000);
        HttpResponse r = t.test_upload(fits, "Fits", "203.0.113.20", "extended");
        ASSERT_MSG(r.status == 201, r.body.substr(0, 200));
        ASSERT_EQ(parse_text(r.body).get("test_map").get("width").as_int_or(0), 256);
        step();
        const std::string wide = grown_tiny(160, 410);                                // 65,600 cells: 64 over
        ASSERT_TRUE(!wide.empty());
        r = t.test_upload(wide, "Wide", "203.0.113.21", "extended");
        ASSERT_EQ(r.status, 422);
        ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("map_too_big_online"));
        ASSERT_TRUE(parse_text(r.body).get("message").str().find("65,536 cells") != std::string::npos && parse_text(r.body).get("message").str().find("against bots") != std::string::npos);
        step();
        r = t.test_upload(grown_tiny(512, 512), "Huge", "203.0.113.22", "extended");
        ASSERT_EQ(r.status, 422);
        ASSERT_EQ(parse_text(r.body).get("error").str(), std::string("map_too_big_online"));
        step();
        r = t.test_upload(grown_tiny(257, 1024), "Beyond", "203.0.113.23", "extended");    // 263,168 cells: more than the format allows at all, so the core reads no such map
        ASSERT_MSG(r.status == 400 && parse_text(r.body).get("error").str() == "not_a_map", std::to_string(r.status) + " " + r.body.substr(0, 200));
        step();
        {   // a Classic map of 100 rows (a Classic map has 90 at the most) is a map that loads and is too big for its kind
            ants::assets::LevelData tall = ants::assets::blank_level(100, 50, core::tile_names());
            const ants::assets::LevelWriteResult w = ants::assets::write_level(tall, ants::assets::LevelLimits{});
            ASSERT_TRUE(w.ok);
            r = t.test_upload(std::string(w.bytes.begin(), w.bytes.end()), "Tall", "203.0.113.24", "classic");
            ASSERT_MSG(r.status == 422 && parse_text(r.body).get("error").str() == "map_too_big", std::to_string(r.status) + " " + r.body.substr(0, 200));
        }
        ASSERT_EQ(t.store->test_maps(t.now).size(), size_t{1});                         // (only the 256 x 256 map is kept)
        ASSERT_TRUE(std::string(ants::net::kNoticeMapTooBig).find("65,536 cells") != std::string::npos);        // (the room's sentence says the same number)
        ASSERT_TRUE(std::string(ants::net::kNoticeMapTooBig).size() <= ants::net::kMaxChatChars);
    } TEST_END();

    TEST_CASE("API2.13 The store of test maps: at most kMaxTestMaps (the oldest goes), at most kMaxTestPerAddress for one address (its own oldest goes), a map that a room has or that is under two minutes old never goes - the upload is refused instead - so a burst cannot push out the map of a room that has not started") {
        {   // the 64: 70 uploads from 70 addresses, each 3 minutes after the last
            Api t;
            std::vector<std::string> files;
            for (int i = 0; i < 70; ++i) {
                const TestSubmitOutcome o = t.keep_test("198.51.100." + std::to_string(i), 180);
                ASSERT_MSG(o.ok, "upload " + std::to_string(i) + ": " + o.fail.code);
                files.push_back(o.map.name);
            }
            MapStore ms("", "");
            ms.set_test_maps(t.store->test_maps(t.now));
            ASSERT_EQ(t.store->test_maps(t.now).size(), SubmissionStore::kMaxTestMaps);
            ASSERT_TRUE(ms.test_map(files[0]) == nullptr && ms.test_map(files[5]) == nullptr && ms.test_map(files[6]) != nullptr && ms.test_map(files[69]) != nullptr);        // (the six oldest went: 70 - 64)
            size_t on_disk = 0;
            for (const auto& entry : fs::directory_iterator(fs::path(t.config.dir) / "test")) on_disk += entry.is_regular_file() ? 1u : 0u;
            ASSERT_EQ(on_disk, SubmissionStore::kMaxTestMaps);                           // (and their files are gone)
        }
        {   // one address: the 5th takes the place of its own oldest
            Api t;
            std::vector<std::string> files;
            for (int i = 0; i < 6; ++i) files.push_back(t.keep_test("198.51.100.1", 180).map.name);
            MapStore ms("", "");
            ms.set_test_maps(t.store->test_maps(t.now));
            ASSERT_EQ(t.store->test_maps(t.now).size(), SubmissionStore::kMaxTestPerAddress);
            ASSERT_TRUE(ms.test_map(files[0]) == nullptr && ms.test_map(files[1]) == nullptr && ms.test_map(files[2]) != nullptr && ms.test_map(files[5]) != nullptr);
            const TestSubmitOutcome other = t.keep_test("198.51.100.2", 1);              // (another address is not in the way of the first)
            ASSERT_TRUE(other.ok);
            ASSERT_EQ(t.store->test_maps(t.now).size(), SubmissionStore::kMaxTestPerAddress + 1);
        }
        {   // an address that holds its share drops ITS OWN oldest map, never an older map of another address
            Api t;
            const TestSubmitOutcome older = t.keep_test("198.51.100.9", 0);              // (the oldest map of all is another address's)
            ASSERT_TRUE(older.ok);
            std::vector<std::string> mine;
            for (int i = 0; i < 5; ++i) mine.push_back(t.keep_test("198.51.100.1", 180).map.name);
            MapStore ms("", "");
            ms.set_test_maps(t.store->test_maps(t.now));
            ASSERT_TRUE(ms.test_map(older.map.name) != nullptr);
            ASSERT_TRUE(ms.test_map(mine[0]) == nullptr && ms.test_map(mine[1]) != nullptr && ms.test_map(mine[4]) != nullptr);
        }
        {   // five in a minute from one address: the fifth is refused (the other four are new), and the four stay
            Api t;
            for (int i = 0; i < 4; ++i) ASSERT_TRUE(t.keep_test("198.51.100.1", 5).ok);
            const TestSubmitOutcome fifth = t.keep_test("198.51.100.1", 5);
            ASSERT_TRUE(!fifth.ok && fifth.fail.status == 429 && fifth.fail.code == "test_limit");
            ASSERT_EQ(t.store->test_maps(t.now).size(), size_t{4});
            ASSERT_TRUE(t.keep_test("198.51.100.1", 200).ok);                            // (two minutes on, the oldest may go)
        }
        {   // a map that a room has is never dropped; when the store is full of such maps, the upload is refused
            Api t;
            std::set<std::string> in_use;
            t.store->set_test_map_in_use([&](const std::string& file) { return in_use.count(file) != 0; });
            std::vector<std::string> files;
            for (int i = 0; i < static_cast<int>(SubmissionStore::kMaxTestMaps); ++i) {
                const TestSubmitOutcome o = t.keep_test("198.51.100." + std::to_string(i), 150);
                ASSERT_TRUE(o.ok);
                files.push_back(o.map.name);
                if (i < 10) in_use.insert(o.map.name);                                   // (the first ten have a room)
            }
            for (int i = 0; i < 20; ++i) ASSERT_TRUE(t.keep_test("198.51.101." + std::to_string(i), 150).ok);
            MapStore ms("", "");
            ms.set_test_maps(t.store->test_maps(t.now));
            for (int i = 0; i < 10; ++i) ASSERT_MSG(ms.test_map(files[static_cast<size_t>(i)]) != nullptr, "a map with a room is kept: " + std::to_string(i));
            ASSERT_TRUE(ms.test_map(files[10]) == nullptr && ms.test_map(files[29]) == nullptr && ms.test_map(files[30]) != nullptr);        // (the next twenty oldest went)
            in_use = std::set<std::string>(files.begin(), files.end());                    // every map has a room now
            for (const ApprovedMap& m : t.store->test_maps(t.now)) in_use.insert(m.name);
            const TestSubmitOutcome full = t.keep_test("198.51.102.1", 150);
            ASSERT_TRUE(!full.ok && full.fail.status == 503 && full.fail.code == "test_full");
            ASSERT_EQ(t.store->test_maps(t.now).size(), SubmissionStore::kMaxTestMaps);
        }
        {   // a map of six hours is gone whatever; a restart (the store opens) wipes the folder
            Api t;
            ASSERT_TRUE(t.keep_test("198.51.100.1").ok);
            ASSERT_EQ(t.store->test_maps(t.now + SubmissionStore::kTestMapTtlS - 1).size(), size_t{1});
            ASSERT_EQ(t.store->test_maps(t.now + SubmissionStore::kTestMapTtlS).size(), size_t{0});
            ASSERT_TRUE(t.keep_test("198.51.100.1", 10).ok);
            std::string why;
            ASSERT_TRUE(t.store->open(t.now, why));
            size_t files = 0;
            for (const auto& entry : fs::directory_iterator(fs::path(t.config.dir) / "test")) files += entry.is_regular_file() ? 1u : 0u;
            ASSERT_EQ(files, size_t{0});
            ASSERT_EQ(t.store->test_maps(t.now).size(), size_t{0});
        }
    } TEST_END();

    TEST_CASE("API2.14 The hash of an approved map is made once: approving a second map does not read the first file again, and a file that changed is hashed again") {
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

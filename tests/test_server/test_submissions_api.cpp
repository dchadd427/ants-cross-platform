// Tests of the routes of the map submissions (SubmissionApi, docs/MAP_SUBMISSIONS.md): what a player's browser may do and how it is limited by address, and what only the owner may do: the session cookie that the login
// helper signs (every way to forge, change, age or misuse one), the CSRF token, the answer that a visitor gets for everything of the owner's (the answer of a path that does not exist, byte for byte), the owner's decisions
// through the API, and the page's text (nothing is HTML). Requests are made by hand: no sockets.
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>

#include "../common/test_assert.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/submissions.hpp"
#include "ants_server/submissions_http.hpp"
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
const char* const kOwner = "111222333444";

std::string tiny() {
    std::ifstream in(fs::path(maps_dir()) / "TINY.LVL", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The cookie that tools/admin_login.py makes: v1.<base64url of the JSON>.<hex HMAC of "v1.<payload>">
std::string cookie_value(const std::string& secret, const std::string& id, const std::string& name, int64_t exp, const std::string& nonce = "n0nce") {
    const std::string payload = "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"exp\":" + std::to_string(exp) + (nonce.empty() ? "" : ",\"n\":\"" + nonce + "\"") + "}";
    const std::string signed_part = "v1." + base64url_encode(payload);
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
    std::unique_ptr<SubmissionApi> api;
    int64_t now{kT0};
    uint32_t now_ms{1000};
    int refreshed{0};

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
        SubmissionApiConfig ac;
        if (owner_on) {
            ac.owner_id = kOwner;
            ac.session_secret = kSecret;
        }
        ac.attempts_per_hour = 1000;
        if (tune) tune(ac);
        api = std::make_unique<SubmissionApi>(*store, ac);
        api->set_on_approved_changed([this] { ++refreshed; });
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

    std::string cookie(const std::string& id = kOwner, const std::string& name = "Dave", int64_t exp = 0, const std::string& nonce = "n0nce", const std::string& secret = kSecret) {
        return "ants_admin=" + cookie_value(secret, id, name, exp != 0 ? exp : now + 3600, nonce);
    }

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

    TEST_CASE("API2.1 The owner's cookie: the one the helper makes is the owner; a signed cookie of another person is a visitor who signed in, and shown as such; everything else is nobody") {
        Api t;
        ctl::JsonValue m = t.me("");
        ASSERT_TRUE(!m.get("signed_in").as_bool_or(true) && !m.has("csrf") && !m.has("name"));
        m = t.me(t.cookie());
        ASSERT_TRUE(m.get("signed_in").as_bool_or(false) && m.get("owner").as_bool_or(false) && m.get("name").str() == "Dave" && m.get("csrf").str().size() == 32);
        m = t.me(t.cookie("999888777"));
        ASSERT_TRUE(m.get("signed_in").as_bool_or(false) && !m.get("owner").as_bool_or(true) && m.get("name").str() == "Dave" && !m.has("csrf"));
        m = t.me("other=1; " + t.cookie() + "; more=2");
        ASSERT_TRUE(m.get("owner").as_bool_or(false));                                           // (among other cookies)
        const std::string good = t.cookie().substr(11);
        const auto signed_in = [&](const std::string& header) { return t.me(header).get("signed_in").as_bool_or(false); };
        ASSERT_FALSE(signed_in("ants_admin="));
        ASSERT_FALSE(signed_in("ants_admin=" + good + "x"));                                      // (a changed signature)
        ASSERT_FALSE(signed_in("ants_admin=" + good.substr(0, good.size() - 1)));
        std::string flipped = good;
        flipped[flipped.size() - 1] = flipped[flipped.size() - 1] == '0' ? '1' : '0';
        ASSERT_FALSE(signed_in("ants_admin=" + flipped));
        std::string changed_payload = good;
        changed_payload[5] = changed_payload[5] == 'A' ? 'B' : 'A';                               // (a changed payload with the old signature)
        ASSERT_FALSE(signed_in("ants_admin=" + changed_payload));
        ASSERT_FALSE(signed_in("ants_admin=v2" + good.substr(2)));
        ASSERT_FALSE(signed_in("ants_admin=" + good.substr(3)));
        ASSERT_FALSE(signed_in("Ants_Admin=" + good));                                            // (the name is exact)
        ASSERT_FALSE(signed_in("xants_admin=" + good));
        ASSERT_FALSE(signed_in("ants_admin2=" + good));
        ASSERT_FALSE(signed_in("ants_admin=\"" + good + "\""));
        ASSERT_FALSE(signed_in(t.cookie(kOwner, "Dave", 0, "n0nce", "another-secret-another-secret-another")));        // (signed with another secret)
        ASSERT_FALSE(signed_in(t.cookie(kOwner, "Dave", t.now - 1)));                             // (over)
        ASSERT_FALSE(signed_in(t.cookie(kOwner, "Dave", t.now)));
        ASSERT_FALSE(signed_in(t.cookie(kOwner, "Dave", t.now + 3 * 86400)));                     // (claims to last longer than the helper's cookies)
        ASSERT_TRUE(signed_in(t.cookie(kOwner, "Dave", t.now + 2 * 86400)));
        ASSERT_FALSE(signed_in(t.cookie(kOwner, "Dave", 0, "")));                                 // (no nonce)
        ASSERT_FALSE(signed_in(t.cookie("12a4", "Dave")));                                        // (an id that is not digits)
        ASSERT_FALSE(signed_in(t.cookie("", "Dave")));
        ASSERT_FALSE(signed_in(t.cookie(std::string(40, '1'), "Dave")));
        ASSERT_FALSE(signed_in("ants_admin=" + std::string(3000, 'a')));                         // (a huge cookie header is not even read)
        ASSERT_FALSE(signed_in(std::string(3000, ';') + t.cookie()));
        m = t.me(t.cookie(kOwner, "<img src=x onerror=alert(1)>"));
        ASSERT_TRUE(m.get("signed_in").as_bool_or(false) && m.get("name").str() == "someone");    // (a name that cannot be shown is replaced, not passed on)
        Api other(nullptr, true);
        ASSERT_TRUE(other.me(t.cookie()).get("owner").as_bool_or(false));                         // (the same secret, the same owner: the cookie works for any server that holds the secret)
    } TEST_END();

    TEST_CASE("API2.2 A visitor and a stranger get, for every call of the owner's, the answer of a path that does not exist, byte for byte; so does everything when the owner's page is not set up") {
        Api t;
        const std::vector<std::pair<std::string, std::string>> owner_calls = {
            {"GET", "/admin/api/maps"}, {"GET", "/admin/api/maps?state=approved"}, {"GET", "/admin/api/next-name"}, {"GET", "/admin/api/maps/" + std::string(32, 'a') + "/picture"},
            {"POST", "/admin/api/maps/" + std::string(32, 'a') + "/approve"}, {"POST", "/admin/api/maps/" + std::string(32, 'a') + "/decline"}, {"POST", "/admin/api/maps/" + std::string(32, 'a') + "/take-down"},
            {"POST", "/admin/api/maps/" + std::string(32, 'a') + "/put-back"}, {"POST", "/admin/api/maps/x/y"}};
        const HttpResponse nothing = t.call(t.req("GET", "/admin/api/does-not-exist"));
        ASSERT_EQ(nothing.status, 404);
        const std::vector<std::string> cookies = {"", t.cookie("999888777"), t.cookie(kOwner, "Dave", t.now - 5), "ants_admin=garbage"};
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
                ASSERT_MSG(r.status == nothing.status && r.body == nothing.body && r.content_type == nothing.content_type, call.first + " " + call.second + " with cookie '" + c.substr(0, 20) + "'");
            }
        }
        for (const char* method : {"PUT", "DELETE", "PATCH"}) ASSERT_EQ(t.call(t.req(method, "/admin/api/me")).body, nothing.body);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api")).body, nothing.body);
        Api off(nullptr, false);
        for (const auto& call : owner_calls) {
            const HttpResponse r = off.call(off.req(call.first, call.second.substr(0, call.second.find('?')), "", {{"cookie", t.cookie()}}));
            ASSERT_MSG(r.status == 404 && r.body == nothing.body, call.second);
        }
        ASSERT_EQ(off.call(off.req("GET", "/admin/api/me", "", {{"cookie", t.cookie()}})).body, nothing.body);        // (not even /me: there is nothing to say)
        Api half([](SubmissionApiConfig& c) { c.session_secret = "short"; });                                          // (a secret that is too short is no secret)
        ASSERT_EQ(half.call(half.req("GET", "/admin/api/me")).status, 404);
        Api no_id([](SubmissionApiConfig& c) { c.owner_id.clear(); });
        ASSERT_EQ(no_id.call(no_id.req("GET", "/admin/api/me")).status, 404);
        Api bad_id([](SubmissionApiConfig& c) { c.owner_id = "not digits"; });
        ASSERT_EQ(bad_id.call(bad_id.req("GET", "/admin/api/me")).status, 404);
        // A cookie that is signed with the short secret is no cookie either: 31 characters are too few, 32 are enough (the check is on the length, not only on "a request without a cookie")
        const std::string s31(31, 'k'), s32(32, 'k');
        Api short31([&](SubmissionApiConfig& c) { c.session_secret = s31; });
        ASSERT_EQ(short31.call(short31.req("GET", "/admin/api/me", "", {{"cookie", short31.cookie(kOwner, "Dave", 0, "n0nce", s31)}})).body, nothing.body);
        ASSERT_EQ(short31.call(short31.req("GET", "/admin/api/maps", "", {{"cookie", short31.cookie(kOwner, "Dave", 0, "n0nce", s31)}})).body, nothing.body);
        Api short32([&](SubmissionApiConfig& c) { c.session_secret = s32; });
        ASSERT_TRUE(short32.me(short32.cookie(kOwner, "Dave", 0, "n0nce", s32)).get("owner").as_bool_or(false));
        // The owner is the id itself, not an id that begins with it (nor one it begins)
        ASSERT_FALSE(t.me(t.cookie(std::string(kOwner) + "7")).get("owner").as_bool_or(false));
        ASSERT_FALSE(t.me(t.cookie(std::string(kOwner).substr(0, 5))).get("owner").as_bool_or(false));
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/maps", "", {{"cookie", t.cookie(std::string(kOwner) + "7")}})).body, nothing.body);
        // The picture of a map that exists is for the owner alone: a visitor and a stranger get the answer of a missing path
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
        ASSERT_EQ(t.call(t.req("GET", picture, "", {{"cookie", t.cookie("999")}})).body, nothing.body);
        ASSERT_EQ(t.call(t.req("GET", picture)).status, 404);
        ASSERT_EQ(t.call(t.req("GET", picture, "", {{"cookie", t.cookie()}})).status, 200);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api", "", {{"cookie", t.cookie()}})).body, nothing.body);        // ("/admin/api" itself, 10 characters: no route, and no exception for the owner)
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
        const std::string other_session = t.me(t.cookie(kOwner, "Dave", 0, "another-nonce")).get("csrf").str();
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
        ASSERT_EQ(post({{"x-ants-csrf", csrf}, {"sec-fetch-site", "same-origin"}}).status, 200);
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
        ASSERT_TRUE(list.get("untrusted").as_bool_or(false) && list.get("untrusted_fields").is_array() && list.get("untrusted_fields").size() == 4);          // (the list says which fields are player text)
        ASSERT_TRUE(list.get("untrusted_fields").at(0).str() == "name" && list.get("untrusted_fields").at(1).str() == "maker" && list.get("untrusted_fields").at(2).str() == "note" && list.get("untrusted_fields").at(3).str() == "description");
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

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

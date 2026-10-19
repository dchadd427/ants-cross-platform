// Tests of the owner's page API for accounts (SubmissionApi, docs/MAP_SUBMISSIONS.md "Accounts"): the sign-in and what it answers, the forced choice of a new password (held by the server, not the page), the
// change of a password, the two roles (a Moderator cannot reach what an Admin can: the answer is the bytes of a missing path), the user pages, the log, and that no password or hash is ever in an answer.
// Requests are made by hand: no sockets.
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>

#include "../common/test_assert.hpp"
#include "ants_server/admin_accounts.hpp"
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

using ctl::HttpRequest;
using ctl::HttpResponse;

const char* const kSecret = "0123456789abcdef0123456789abcdef0123";
const char* const kPassword = "correct horse battery staple";
const char* const kLocked = "Too many tries. Wait a few minutes.";

std::string tiny() {
    std::ifstream in(fs::path(maps_dir()) / "TINY.LVL", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

MapStore& library() {
    static MapStore store = [] {
        MapStore s(maps_dir(), std::string(ANTS_SOURCE_DIR) + "/Community-Maps");
        s.scan();
        return s;
    }();
    return store;
}

// A person at a browser: the cookie pair and the CSRF token of a signed-in session
struct Session {
    std::string cookie, csrf;
    explicit operator bool() const { return !cookie.empty(); }
};

struct Site {
    TempDir dir;
    db::Database database;
    SubmissionConfig config;
    std::unique_ptr<SubmissionStore> store;
    std::unique_ptr<AdminAccounts> accounts;
    std::unique_ptr<SubmissionApi> api;
    int64_t now{kT0};
    uint32_t now_ms{1000};

    explicit Site(const std::function<void(AccountConfig&)>& tune = nullptr) {
        open_server_db(database, "");
        config.dir = (dir.path / "s").string();
        config.min_free_bytes = 1;
        config.per_address_per_day = 1000;
        config.per_day_total = 100000;
        config.max_waiting = 1000;
        store = std::make_unique<SubmissionStore>(database, config, [](const std::string& key) { return library().library_has_name(key); });
        std::string why;
        store->open(now, why);
        AccountConfig ac;
        ac.rounds = 300;
        ac.checks_per_window = 100000;
        if (tune) tune(ac);
        accounts = std::make_unique<AdminAccounts>(database, ac);
        accounts->open(why);
        SubmissionApiConfig sc;
        sc.session_secret = kSecret;
        sc.attempts_per_hour = 1000;
        api = std::make_unique<SubmissionApi>(*store, *accounts, sc);
    }

    HttpResponse call(const HttpRequest& r) { return api->handle(r, now, now_ms); }

    HttpRequest req(const std::string& method, const std::string& path, const std::map<std::string, std::string>& headers = {}, const std::string& body = "", const std::string& query = "") {
        HttpRequest r;
        r.method = method;
        r.path = path;
        r.query = query;
        r.headers.insert(headers.begin(), headers.end());
        r.body = body;
        r.peer = "172.18.0.4";
        return r;
    }

    static std::string json_pair(const std::string& a, const std::string& av, const std::string& b, const std::string& bv) { return "{\"" + a + "\":\"" + av + "\",\"" + b + "\":\"" + bv + "\"}"; }

    HttpResponse login(const std::string& name, const std::string& password, const std::string& address = "198.51.100.7") {
        now_ms += 20000;
        return call(req("POST", "/admin/api/login", {{"content-type", "application/json"}, {"x-client-address", address}, {"sec-fetch-site", "same-origin"}}, json_pair("username", name, "password", password)));
    }

    Session sign_in(const std::string& name, const std::string& password, const std::string& address = "198.51.100.7") {
        const HttpResponse r = login(name, password, address);
        Session s;
        if (r.status == 200 && !r.set_cookies.empty()) {
            s.cookie = r.set_cookies[0].substr(0, r.set_cookies[0].find(';'));
            s.csrf = parse_text(r.body).get("csrf").str();
        }
        return s;
    }

    HttpResponse get(const Session& s, const std::string& path, const std::string& query = "") { return call(req("GET", path, {{"cookie", s.cookie}}, "", query)); }

    HttpResponse post(const Session& s, const std::string& path, const std::string& body = "{}", const std::string& address = "198.51.100.7") {
        return call(req("POST", path, {{"cookie", s.cookie}, {"x-ants-csrf", s.csrf}, {"content-type", "application/json"}, {"sec-fetch-site", "same-origin"}, {"x-client-address", address}}, body));
    }

    // an account whose password is chosen already
    int64_t add(const std::string& name, const std::string& role, const std::string& password = kPassword) {
        User u;
        std::string starting;
        Failure why;
        if (!accounts->create(Actor(), name, role, now, u, starting, why)) return 0;
        int64_t gen = 0;
        accounts->change_password(Actor(), u.id, password, now, gen, why);
        return u.id;
    }

    std::string submit_map(const std::string& name, const std::string& address) {
        SubmitInput in;
        in.kind = "classic";
        in.name = name;
        in.maker = "Mia";
        in.bytes = tiny();
        in.address = address;
        const SubmitOutcome out = store->submit(in, now);
        return out.ok ? out.submission.id : std::string();
    }
};

bool is_missing(const Site& s, const HttpResponse& r, const HttpResponse& nothing) {
    (void)s;
    return r.body == nothing.body && r.content_type == nothing.content_type && r.set_cookies.empty();
}

}  // namespace

int main() {
    std::cout << "=======================================================\n THE OWNER'S PAGE: ACCOUNTS THROUGH THE API\n=======================================================\n";

    TEST_CASE("ADM1.1 A sign-in answers with the account and a cookie; a wrong name, a wrong password, a disabled account and a name of any shape get the very same bytes; no failure sets a cookie") {
        Site t;
        t.add("dave", "admin");
        const HttpResponse ok = t.login("dave", kPassword);
        ASSERT_TRUE(ok.status == 200 && ok.set_cookies.size() == 1);
        const HttpResponse wrong_password = t.login("dave", "not the password");
        const HttpResponse wrong_name = t.login("nobody", kPassword);
        ASSERT_TRUE(wrong_password.status == 401 && wrong_password.body == "{\"error\":\"bad_login\",\"message\":\"Wrong username or password.\"}");
        ASSERT_TRUE(wrong_name.status == wrong_password.status && wrong_name.body == wrong_password.body && wrong_name.content_type == wrong_password.content_type);
        ASSERT_TRUE(wrong_password.set_cookies.empty() && wrong_name.set_cookies.empty());
        t.add("mia", "moderator");
        std::string pw;
        Failure why;
        const User mia = t.accounts->list()[1];
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), mia.id, true, t.now, why));
        const HttpResponse disabled = t.login("mia", kPassword);
        ASSERT_TRUE(disabled.status == 401 && disabled.body == wrong_password.body && disabled.set_cookies.empty());
        for (const std::string& odd : {std::string(), std::string(65, 'x'), std::string("<b>"), std::string("da\\u0000ve"), std::string("dave ")}) {
            const HttpResponse r = t.login(odd, kPassword);
            ASSERT_MSG(r.status == 401 && r.body == wrong_password.body, odd);
        }
        ASSERT_TRUE(t.login("dave", std::string(257, 'p')).body == wrong_password.body);              // (a password of such a size is refused without a check)
        ASSERT_TRUE(t.login("dave", "").body == wrong_password.body);
        for (const HttpResponse* r : {&ok, &wrong_password, &wrong_name, &disabled}) ASSERT_TRUE(r->body.find(kPassword) == std::string::npos && r->body.find("pbkdf2") == std::string::npos);
        // the shape of the request
        const std::map<std::string, std::string> good = {{"content-type", "application/json"}, {"x-client-address", "198.51.100.7"}};
        const std::string body = Site::json_pair("username", "dave", "password", kPassword);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", {{"x-client-address", "198.51.100.7"}}, body)).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", {{"content-type", "text/plain"}, {"x-client-address", "198.51.100.7"}}, body)).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", good, "not json")).status, 400);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", good, "[1]")).status, 400);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", good, "")).status, 400);
        std::map<std::string, std::string> cross = good;
        cross["sec-fetch-site"] = "cross-site";
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", cross, body)).status, 403);              // (a page of another site cannot sign the visitor in)
        cross["sec-fetch-site"] = "same-site";
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", cross, body)).status, 403);
        cross["sec-fetch-site"] = "none";
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", cross, body)).status, 403);
        cross["sec-fetch-site"] = "same-origin";
        t.now_ms += 20000;
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/login", cross, body)).status, 200);
        HttpRequest no_address = t.req("POST", "/admin/api/login", {{"content-type", "application/json"}}, body);
        no_address.peer.clear();
        ASSERT_EQ(t.call(no_address).status, 400);
        // the body of a failed sign-in does not hold the password that was typed, and nothing is in the url
        ASSERT_TRUE(t.login("dave", "the wrong one 123").body.find("the wrong one") == std::string::npos);
    } TEST_END();

    TEST_CASE("ADM1.2 The fifth wrong try of a name from an address answers 429 with the words of the picture, and so does everything from that pair until the time is up; the page is told nothing about which names exist") {
        Site t;
        t.add("dave", "admin");
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.login("dave", "wrong", "203.0.113.9").status, 401);
        HttpResponse r = t.login("dave", "wrong", "203.0.113.9");
        ASSERT_TRUE(r.status == 429 && parse_text(r.body).get("error").str() == "locked" && parse_text(r.body).get("message").str() == kLocked && r.set_cookies.empty());
        ASSERT_EQ(t.login("dave", kPassword, "203.0.113.9").status, 429);
        ASSERT_EQ(t.login("dave", kPassword, "203.0.113.10").status, 200);
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.login("ghost", "wrong", "203.0.113.9").status, 401);
        r = t.login("ghost", "wrong", "203.0.113.9");
        ASSERT_TRUE(r.status == 429 && parse_text(r.body).get("message").str() == kLocked);        // (the same for a name that does not exist)
        t.now += 600;
        ASSERT_EQ(t.login("dave", kPassword, "203.0.113.9").status, 200);
    } TEST_END();

    TEST_CASE("ADM1.3 The budget of password checks answers 503 busy, and a refused check is not a wrong try") {
        Site t([](AccountConfig& c) { c.checks_per_window = 2; });
        t.add("dave", "admin");
        const auto login_now = [&](const std::string& name, const std::string& password, const std::string& address) {
            return t.call(t.req("POST", "/admin/api/login", {{"content-type", "application/json"}, {"x-client-address", address}}, Site::json_pair("username", name, "password", password)));
        };
        ASSERT_EQ(login_now("dave", "wrong", "203.0.113.1").status, 401);
        ASSERT_EQ(login_now("dave", "wrong", "203.0.113.2").status, 401);
        const HttpResponse busy = login_now("dave", kPassword, "203.0.113.3");
        ASSERT_TRUE(busy.status == 503 && parse_text(busy.body).get("error").str() == "busy" && busy.set_cookies.empty());
        t.now_ms += 10000;
        ASSERT_EQ(login_now("dave", kPassword, "203.0.113.3").status, 200);
    } TEST_END();

    TEST_CASE("ADM2.1 The first sign-in with a starting password can do nothing but choose a password: every other call is refused by the server, not by the page") {
        Site t;
        User u;
        std::string starting;
        Failure why;
        ASSERT_TRUE(t.accounts->create_first_admin("owner-account-1", t.now, u, starting, why));
        const Session s = t.sign_in("owner-account-1", starting);
        ASSERT_TRUE(s);
        const ctl::JsonValue me = parse_text(t.get(s, "/admin/api/me").body);
        ASSERT_TRUE(me.get("signed_in").as_bool_or(false) && me.get("must_change").as_bool_or(false) && me.get("role").str() == "admin" && me.get("csrf").str() == s.csrf);
        const std::string some_id(32, 'a');
        for (const char* path : {"/admin/api/maps", "/admin/api/next-name", "/admin/api/users", "/admin/api/audit"}) {
            const HttpResponse r = t.get(s, path);
            ASSERT_MSG(r.status == 403 && parse_text(r.body).get("error").str() == "password_change_required", path);
        }
        ASSERT_EQ(t.get(s, "/admin/api/maps/" + some_id + "/picture").status, 403);
        for (const char* path : {"/admin/api/users", "/admin/api/users/1/reset", "/admin/api/users/1/remove", "/admin/api/users/1/disable"}) {
            const HttpResponse r = t.post(s, path, "{\"username\":\"newone\",\"role\":\"admin\"}");
            ASSERT_MSG(r.status == 403 && parse_text(r.body).get("error").str() == "password_change_required", path);
        }
        ASSERT_EQ(t.post(s, "/admin/api/maps/" + some_id + "/approve").status, 403);
        ASSERT_EQ(t.accounts->list().size(), size_t{1});                                              // (nothing was made)
        // the weak and the wrong new passwords
        const auto change = [&](const Session& who, const std::string& current, const std::string& fresh) { return t.post(who, "/admin/api/password", Site::json_pair("current", current, "new", fresh)); };
        HttpResponse r = change(s, "", "short one");
        ASSERT_TRUE(r.status == 400 && parse_text(r.body).get("error").str() == "bad_new_password" && parse_text(r.body).get("reason").str() == "too_short");
        r = change(s, "", "OWNER");
        ASSERT_EQ(parse_text(r.body).get("reason").str(), std::string("too_short"));
        r = change(s, "", "Owner-Account-1");
        ASSERT_TRUE(r.status == 400 && parse_text(r.body).get("reason").str() == "same_as_name");        // (not the username, in any capitals)
        r = change(s, "", starting);
        ASSERT_TRUE(r.status == 400 && parse_text(r.body).get("reason").str() == "same_as_old");     // (not the starting password)
        ASSERT_EQ(parse_text(change(s, "", std::string(257, 'a')).body).get("reason").str(), std::string("too_long"));
        ASSERT_EQ(parse_text(change(s, "", "bad\\u0001control chars").body).get("reason").str(), std::string("characters"));
        ASSERT_TRUE(t.get(s, "/admin/api/maps").status == 403);                                        // (still not allowed)
        // without the page's token, and from another site
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/password", {{"cookie", s.cookie}, {"content-type", "application/json"}}, Site::json_pair("current", "", "new", "a fresh long password"))).status, 403);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/password", {{"cookie", s.cookie}, {"x-ants-csrf", s.csrf}, {"content-type", "application/json"}, {"sec-fetch-site", "cross-site"}}, Site::json_pair("current", "", "new", "a fresh long password"))).status, 404);
        // the real change
        r = change(s, "", "my own fresh password");
        ASSERT_TRUE(r.status == 200 && r.set_cookies.size() == 1 && r.set_cookies[0].find("ants_admin=v2.") == 0 && r.set_cookies[0].find("; Secure") != std::string::npos);
        ASSERT_TRUE(r.body.find("my own fresh password") == std::string::npos && !parse_text(r.body).get("must_change").as_bool_or(true));
        Session renewed;
        renewed.cookie = r.set_cookies[0].substr(0, r.set_cookies[0].find(';'));
        renewed.csrf = parse_text(r.body).get("csrf").str();
        ASSERT_EQ(t.get(renewed, "/admin/api/maps").status, 200);                                     // (the new cookie works at once)
        ASSERT_EQ(t.get(s, "/admin/api/maps").status, 404);                                           // (the cookie of the starting password is dead: it is nobody)
        ASSERT_FALSE(parse_text(t.get(s, "/admin/api/me").body).get("signed_in").as_bool_or(true));
        ASSERT_EQ(t.login("owner-account-1", starting).status, 401);
        ASSERT_EQ(t.login("owner-account-1", "my own fresh password").status, 200);
    } TEST_END();

    TEST_CASE("ADM2.1b The checks of a password that the page asks for are counted like a sign-in: past the budget of the server the answer is 503 busy, so a session cannot make the one thread hash without end") {
        Site t([](AccountConfig& c) { c.checks_per_window = 3; });
        User u;
        std::string starting;
        Failure why;
        ASSERT_TRUE(t.accounts->create_first_admin("owner-account-1", t.now, u, starting, why));
        const Session s = t.sign_in("owner-account-1", starting);                                    // (a check: 1)
        ASSERT_TRUE(s);
        const auto change = [&](const std::string& fresh) { return t.post(s, "/admin/api/password", Site::json_pair("current", "", "new", fresh)); };
        ASSERT_EQ(parse_text(change(starting).body).get("reason").str(), std::string("same_as_old"));    // (2)
        ASSERT_EQ(parse_text(change(starting).body).get("reason").str(), std::string("same_as_old"));    // (3)
        HttpResponse r = change(starting);
        ASSERT_TRUE(r.status == 503 && parse_text(r.body).get("error").str() == "busy");              // (nothing is hashed past the budget)
        const uint64_t runs = pbkdf2_runs();
        ASSERT_EQ(change(starting).status, 503);
        ASSERT_EQ(pbkdf2_runs(), runs);
        ASSERT_EQ(change("short").status, 400);                                                        // (the rules that need no hash are still told)
        t.now_ms += 10000;
        ASSERT_EQ(change("my own fresh password").status, 200);
    } TEST_END();

    TEST_CASE("ADM2.2 Changing a password later needs the current one (wrong tries count toward the lock), a new one that is not the old one, ends every other session and keeps this one") {
        Site t;
        t.add("dave", "admin");
        const Session a = t.sign_in("dave", kPassword, "198.51.100.1");
        const Session other = t.sign_in("dave", kPassword, "198.51.100.2");
        const auto change = [&](const Session& who, const std::string& current, const std::string& fresh, const std::string& address = "198.51.100.1") {
            return t.post(who, "/admin/api/password", Site::json_pair("current", current, "new", fresh), address);
        };
        HttpResponse r = change(a, "not the current one", "a brand new password");
        ASSERT_TRUE(r.status == 403 && parse_text(r.body).get("error").str() == "bad_password" && r.set_cookies.empty());
        ASSERT_EQ(change(a, "", "a brand new password").status, 403);
        ASSERT_EQ(change(a, std::string(300, 'x'), "a brand new password").status, 403);
        r = change(a, kPassword, kPassword);
        ASSERT_TRUE(r.status == 400 && parse_text(r.body).get("reason").str() == "same_as_old");
        ASSERT_EQ(change(a, kPassword, "too short").status, 400);
        r = change(a, kPassword, "a brand new password");
        ASSERT_EQ(r.status, 200);
        Session renewed = a;
        renewed.cookie = r.set_cookies[0].substr(0, r.set_cookies[0].find(';'));
        renewed.csrf = parse_text(r.body).get("csrf").str();
        ASSERT_EQ(t.get(renewed, "/admin/api/maps").status, 200);
        ASSERT_EQ(t.get(a, "/admin/api/maps").status, 404);
        ASSERT_EQ(t.get(other, "/admin/api/maps").status, 404);                                       // (a session from another place is over too)
        ASSERT_EQ(t.login("dave", kPassword).status, 401);
        ASSERT_EQ(t.login("dave", "a brand new password").status, 200);
        // five wrong "current" passwords lock the pair, like five wrong sign-ins
        for (int i = 0; i < 4; ++i) ASSERT_EQ(change(renewed, "wrong", "yet another password", "203.0.113.50").status, 403);
        r = change(renewed, "wrong", "yet another password", "203.0.113.50");
        ASSERT_TRUE(r.status == 429 && parse_text(r.body).get("message").str() == kLocked);
        ASSERT_EQ(change(renewed, "a brand new password", "yet another password", "203.0.113.50").status, 429);
        ASSERT_EQ(t.login("dave", "a brand new password", "203.0.113.50").status, 429);               // (the lock is of the pair: the sign-in from there is locked too)
        ASSERT_EQ(change(renewed, "a brand new password", "yet another password", "203.0.113.51").status, 200);
    } TEST_END();

    TEST_CASE("ADM3.1 A Moderator decides on maps, sees who decided, and gets, for the users and the log, the status 403 with the very bytes of a missing path; an Admin has all of it") {
        Site t;
        t.add("dave", "admin");
        t.add("mia", "moderator");
        const Session admin = t.sign_in("dave", kPassword, "198.51.100.1");
        const Session mod = t.sign_in("mia", kPassword, "198.51.100.2");
        ASSERT_TRUE(admin && mod);
        const HttpResponse nothing = t.call(t.req("GET", "/admin/api/does-not-exist"));
        ASSERT_EQ(nothing.status, 404);
        const std::vector<std::pair<std::string, std::string>> admin_only = {{"GET", "/admin/api/users"}, {"GET", "/admin/api/audit"}, {"POST", "/admin/api/users"}, {"POST", "/admin/api/users/1/reset"},
                                                                              {"POST", "/admin/api/users/1/disable"}, {"POST", "/admin/api/users/1/enable"}, {"POST", "/admin/api/users/1/remove"}, {"POST", "/admin/api/users/x/y"},
                                                                              {"GET", "/admin/api/users/1"}, {"POST", "/admin/api/audit"}};
        for (const auto& call : admin_only) {
            const HttpResponse r = call.first == "GET" ? t.get(mod, call.second) : t.post(mod, call.second, "{\"username\":\"newone\",\"role\":\"admin\"}");
            ASSERT_MSG(r.status == 403 && is_missing(t, r, nothing), call.first + " " + call.second);
        }
        ASSERT_EQ(t.accounts->list().size(), size_t{2});                                              // (a Moderator made no Admin)
        std::string id1 = t.submit_map("First Map", "203.0.113.1"), id2 = t.submit_map("Second Map", "203.0.113.2"), id3 = t.submit_map("Third Map", "203.0.113.3");
        ASSERT_TRUE(!id1.empty() && !id2.empty() && !id3.empty());
        ASSERT_EQ(parse_text(t.get(mod, "/admin/api/maps").body).get("maps").size(), size_t{3});
        ASSERT_EQ(parse_text(t.post(mod, "/admin/api/maps/" + id1 + "/approve").body).get("map").get("decided_by").str(), std::string("mia"));
        ASSERT_EQ(parse_text(t.post(mod, "/admin/api/maps/" + id2 + "/decline", "{\"reason\":\"Too plain\"}").body).get("map").get("decided_by").str(), std::string("mia"));
        ASSERT_EQ(parse_text(t.post(admin, "/admin/api/maps/" + id1 + "/take-down").body).get("map").get("decided_by").str(), std::string("dave"));
        ASSERT_EQ(parse_text(t.post(mod, "/admin/api/maps/" + id1 + "/put-back").body).get("map").get("decided_by").str(), std::string("mia"));
        const ctl::JsonValue approved = parse_text(t.get(admin, "/admin/api/maps", "state=approved").body);
        ASSERT_TRUE(approved.get("maps").size() == 1 && approved.get("maps").at(0).get("decided_by").str() == "mia");
        const ctl::JsonValue declined = parse_text(t.get(mod, "/admin/api/maps", "state=declined").body);
        ASSERT_TRUE(declined.get("maps").at(0).get("decided_by").str() == "mia" && !parse_text(t.get(mod, "/admin/api/maps").body).get("maps").at(0).has("decided_by"));         // (a waiting map has no decider)
        ASSERT_TRUE(parse_text(t.get(mod, "/admin/api/maps").body).get("untrusted_fields").size() == 6);
        // an Admin sees the log of it all, with the fields marked as typed by people
        const ctl::JsonValue log = parse_text(t.get(admin, "/admin/api/audit").body);
        ASSERT_TRUE(log.get("untrusted").as_bool_or(false) && log.get("untrusted_fields").size() == 3);
        std::vector<std::string> lines;
        for (size_t i = 0; i < log.get("entries").size(); ++i) {
            const ctl::JsonValue& e = log.get("entries").at(i);
            lines.push_back(e.get("who").str() + ":" + e.get("action").str() + ":" + e.get("target").str() + ":" + e.get("detail").str());
            ASSERT_TRUE(e.get("at").as_int_or(0) == t.now);
        }
        ASSERT_MSG(lines.size() >= 6, std::to_string(lines.size()));
        ASSERT_EQ(lines[0], std::string("mia:map_put_back:First Map:"));
        ASSERT_EQ(lines[1], std::string("dave:map_taken_down:First Map:"));
        ASSERT_EQ(lines[2], std::string("mia:map_declined:Second Map:Too plain"));
        ASSERT_EQ(lines[3], std::string("mia:map_approved:First Map:"));
        ASSERT_EQ(lines[4], std::string("mia:sign_in::"));
        ASSERT_EQ(lines[5], std::string("dave:sign_in::"));
        // a decision that fails writes no line
        const size_t before = log.get("entries").size();
        ASSERT_EQ(t.post(mod, "/admin/api/maps/" + id1 + "/put-back").status, 409);
        ASSERT_EQ(parse_text(t.get(admin, "/admin/api/audit").body).get("entries").size(), before);
    } TEST_END();

    TEST_CASE("ADM4.1 The user pages: list, add with a password shown once, reset, disable, enable, remove, and the guard of the last Admin") {
        Site t;
        t.add("dave", "admin");
        const Session admin = t.sign_in("dave", kPassword, "198.51.100.1");
        ctl::JsonValue list = parse_text(t.get(admin, "/admin/api/users").body);
        ASSERT_TRUE(list.get("users").size() == 1 && list.get("users").at(0).get("name").str() == "dave" && list.get("users").at(0).get("role").str() == "admin" && list.get("users").at(0).get("you").as_bool_or(false));
        ASSERT_TRUE(list.get("untrusted").as_bool_or(false) && list.get("untrusted_fields").size() == 2);
        // add
        HttpResponse r = t.post(admin, "/admin/api/users", "{\"username\":\"Mia.B\",\"role\":\"moderator\"}");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue made = parse_text(r.body);
        const std::string starting = made.get("password").str();
        ASSERT_TRUE(starting.size() == 20 && made.get("user").get("name").str() == "Mia.B" && made.get("user").get("role").str() == "moderator" && made.get("user").get("has_not_chosen_password").as_bool_or(false));
        ASSERT_TRUE(!made.get("user").has("password") && !made.get("user").has("pw_hash"));
        const int64_t mia = made.get("user").get("id").as_int_or(0);
        // the password is shown once: the list never has it, nor a hash
        const std::string listed = t.get(admin, "/admin/api/users").body;
        ASSERT_TRUE(listed.find(starting) == std::string::npos && listed.find("pbkdf2") == std::string::npos && listed.find("\"password\"") == std::string::npos);
        list = parse_text(listed);
        ASSERT_TRUE(list.get("users").size() == 2 && list.get("users").at(1).get("has_not_chosen_password").as_bool_or(false) && list.get("users").at(1).get("last_login_at").as_int_or(-1) == 0 && !list.get("users").at(1).get("you").as_bool_or(true));
        // refusals
        ASSERT_EQ(parse_text(t.post(admin, "/admin/api/users", "{\"username\":\"mia.b\",\"role\":\"admin\"}").body).get("error").str(), std::string("username_taken"));
        ASSERT_EQ(t.post(admin, "/admin/api/users", "{\"username\":\"mia.b\",\"role\":\"admin\"}").status, 409);
        for (const char* body : {"{\"username\":\"ab\",\"role\":\"admin\"}", "{\"username\":\"has space\",\"role\":\"admin\"}", "{\"username\":\"<b>\",\"role\":\"admin\"}", "{\"role\":\"admin\"}",
                                 "{\"username\":\"newone\",\"role\":\"owner\"}", "{\"username\":\"newone\"}", "{\"username\":\"newone\",\"role\":\"Admin\"}", "{\"username\":123,\"role\":\"admin\"}"}) {
            ASSERT_MSG(t.post(admin, "/admin/api/users", body).status == 400, body);
        }
        ASSERT_EQ(t.accounts->list().size(), size_t{2});
        // the new user signs in with the starting password and must choose another
        const Session mia_session = t.sign_in("mia.B", starting, "198.51.100.2");
        ASSERT_TRUE(mia_session && parse_text(t.get(mia_session, "/admin/api/me").body).get("must_change").as_bool_or(false));
        // reset: a new password, and the user's sessions are over
        r = t.post(admin, "/admin/api/users/" + std::to_string(mia) + "/reset");
        ASSERT_EQ(r.status, 200);
        const std::string fresh = parse_text(r.body).get("password").str();
        ASSERT_TRUE(fresh.size() == 20 && fresh != starting);
        ASSERT_EQ(t.get(mia_session, "/admin/api/me").body, std::string("{\"signed_in\":false}"));
        ASSERT_EQ(t.login("mia.b", starting).status, 401);
        ASSERT_EQ(t.login("mia.b", fresh).status, 200);
        // disable ends the session and forbids the sign-in; enable allows it again, with a new session
        const Session again = t.sign_in("mia.b", fresh, "198.51.100.2");
        ASSERT_EQ(t.post(admin, "/admin/api/users/" + std::to_string(mia) + "/disable").status, 200);
        ASSERT_FALSE(parse_text(t.get(again, "/admin/api/me").body).get("signed_in").as_bool_or(true));
        ASSERT_EQ(t.login("mia.b", fresh).status, 401);
        list = parse_text(t.get(admin, "/admin/api/users").body);
        ASSERT_TRUE(list.get("users").at(1).get("disabled").as_bool_or(false));
        ASSERT_EQ(t.post(admin, "/admin/api/users/" + std::to_string(mia) + "/enable").status, 200);
        ASSERT_EQ(t.login("mia.b", fresh).status, 200);
        ASSERT_FALSE(parse_text(t.get(again, "/admin/api/me").body).get("signed_in").as_bool_or(true));
        // the last Admin
        const std::string dave = std::to_string(t.accounts->list()[0].id);
        r = t.post(admin, "/admin/api/users/" + dave + "/remove");
        ASSERT_TRUE(r.status == 409 && parse_text(r.body).get("error").str() == "last_admin");
        r = t.post(admin, "/admin/api/users/" + dave + "/disable");
        ASSERT_TRUE(r.status == 409 && parse_text(r.body).get("error").str() == "last_admin");
        ASSERT_EQ(t.get(admin, "/admin/api/me").status, 200);
        ASSERT_TRUE(parse_text(t.get(admin, "/admin/api/me").body).get("signed_in").as_bool_or(false));
        // a second Admin: the first can be removed (and is signed out), the second is the last
        r = t.post(admin, "/admin/api/users", "{\"username\":\"second\",\"role\":\"admin\"}");
        ASSERT_EQ(r.status, 201);
        const int64_t second = parse_text(r.body).get("user").get("id").as_int_or(0);
        ASSERT_EQ(t.post(admin, "/admin/api/users/" + dave + "/remove").status, 200);
        ASSERT_FALSE(parse_text(t.get(admin, "/admin/api/me").body).get("signed_in").as_bool_or(true));
        ASSERT_EQ(t.login("dave", kPassword).status, 401);
        ASSERT_EQ(t.accounts->list().size(), size_t{2});
        ASSERT_EQ(t.post(admin, "/admin/api/users/" + std::to_string(second) + "/remove").status, 404);          // (the first Admin's session is over: a missing path)
        // unknown and odd ids
        const Session s2 = t.sign_in("second", parse_text(r.body).get("password").str(), "198.51.100.3");
        ASSERT_TRUE(s2);
        ASSERT_EQ(t.post(s2, "/admin/api/users/99999/reset").status, 403);                                          // (a starting password: nothing else yet)
    } TEST_END();

    TEST_CASE("ADM4.2 User calls with odd ids, methods and tokens: a missing path, a refused token, never a change") {
        Site t;
        t.add("dave", "admin");
        t.add("mia", "moderator");
        const Session admin = t.sign_in("dave", kPassword, "198.51.100.1");
        ASSERT_TRUE(admin);
        for (const char* path : {"/admin/api/users/abc/reset", "/admin/api/users//reset", "/admin/api/users/1", "/admin/api/users/1/dance", "/admin/api/users/1/reset/more", "/admin/api/users/-1/reset", "/admin/api/users/1e3/reset",
                                 "/admin/api/users/99999999999999999999/reset"}) {
            const HttpResponse r = t.post(admin, path);
            ASSERT_MSG(r.status == 404, std::string(path) + " " + std::to_string(r.status));
        }
        ASSERT_EQ(t.post(admin, "/admin/api/users/99999/reset").status, 404);
        ASSERT_EQ(parse_text(t.post(admin, "/admin/api/users/99999/reset").body).get("error").str(), std::string("not_found"));
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/users/2/remove", {{"cookie", admin.cookie}, {"content-type", "application/json"}})).status, 403);          // (no token)
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/users/2/remove", {{"cookie", admin.cookie}, {"x-ants-csrf", "0123456789abcdef0123456789abcdef"}, {"content-type", "application/json"}})).status, 403);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/users/2/remove", {{"cookie", admin.cookie}, {"x-ants-csrf", admin.csrf}, {"content-type", "text/plain"}})).status, 415);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/users/2/remove", {{"cookie", admin.cookie}, {"x-ants-csrf", admin.csrf}, {"content-type", "application/json"}, {"sec-fetch-site", "cross-site"}})).status, 404);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/users", {{"cookie", admin.cookie}, {"sec-fetch-site", "cross-site"}})).status, 404);
        ASSERT_EQ(t.call(t.req("DELETE", "/admin/api/users/2/remove", {{"cookie", admin.cookie}})).status, 404);
        ASSERT_EQ(t.accounts->list().size(), size_t{2});
        // the token of another session is not this one's
        const Session other = t.sign_in("dave", kPassword, "198.51.100.9");
        ASSERT_TRUE(other.csrf != admin.csrf);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/users/2/remove", {{"cookie", admin.cookie}, {"x-ants-csrf", other.csrf}, {"content-type", "application/json"}})).status, 403);
        ASSERT_EQ(t.accounts->list().size(), size_t{2});
        ASSERT_EQ(t.post(admin, "/admin/api/users/2/remove").status, 200);
    } TEST_END();

    TEST_CASE("ADM5.1 Sign out clears the cookie (Max-Age 0, the same flags); it needs the page's token when a session exists, and works without one") {
        Site t;
        t.add("dave", "admin");
        const Session s = t.sign_in("dave", kPassword);
        HttpResponse r = t.post(s, "/admin/api/logout");
        ASSERT_TRUE(r.status == 200 && r.set_cookies.size() == 1);
        ASSERT_TRUE(r.set_cookies[0].compare(0, 11, "ants_admin=") == 0 && r.set_cookies[0].find("; Max-Age=0") != std::string::npos && r.set_cookies[0].find("; HttpOnly") != std::string::npos &&
                    r.set_cookies[0].find("; Secure") != std::string::npos && r.set_cookies[0].find("; SameSite=Strict") != std::string::npos && r.set_cookies[0].find("; Path=/admin") != std::string::npos);
        ASSERT_EQ(r.set_cookies[0].compare(11, 1, ";"), 0);                                         // (the value is empty)
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/logout", {{"cookie", s.cookie}, {"content-type", "application/json"}})).status, 403);          // (a session needs its token: another site cannot sign the person out)
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/logout", {{"cookie", s.cookie}, {"x-ants-csrf", s.csrf}, {"sec-fetch-site", "cross-site"}})).status, 403);
        r = t.call(t.req("POST", "/admin/api/logout", {{"content-type", "application/json"}}));
        ASSERT_TRUE(r.status == 200 && r.set_cookies.size() == 1 && r.set_cookies[0].find("; Max-Age=0") != std::string::npos);
        ASSERT_EQ(t.call(t.req("GET", "/admin/api/logout")).status, 404);
    } TEST_END();

    TEST_CASE("ADM5.2 The cookie carries Secure unless the trial setting drops it, and names the user and generation only: nothing of the password or the role") {
        Site t;
        t.add("dave", "admin");
        const HttpResponse r = t.login("dave", kPassword);
        const std::string value = r.set_cookies[0].substr(11, r.set_cookies[0].find(';') - 11);
        ASSERT_TRUE(value.compare(0, 3, "v2.") == 0);
        std::string payload;
        ASSERT_TRUE(base64url_decode(value.substr(3, value.rfind('.') - 3), payload));
        const ctl::JsonValue p = parse_text(payload);
        ASSERT_TRUE(p.get("u").as_int_or(0) > 0 && p.get("g").as_int_or(0) == 2 && p.get("exp").as_int_or(0) == t.now + 43200 && !p.has("role") && !p.has("name") && !p.has("password"));
        SubmissionApiConfig sc;
        sc.session_secret = kSecret;
        sc.cookie_secure = false;
        SubmissionApi plain(*t.store, *t.accounts, sc);
        t.now_ms += 20000;
        const HttpResponse r2 = plain.handle(t.req("POST", "/admin/api/login", {{"content-type", "application/json"}, {"x-client-address", "198.51.100.7"}}, Site::json_pair("username", "dave", "password", kPassword)), t.now, t.now_ms);
        ASSERT_TRUE(r2.status == 200 && r2.set_cookies[0].find("Secure") == std::string::npos && r2.set_cookies[0].find("HttpOnly") != std::string::npos && r2.set_cookies[0].find("SameSite=Strict") != std::string::npos);
    } TEST_END();

    TEST_CASE("ADM6.1 Names that people typed are only text: the log holds a failed sign-in name only when it is a username, and the users page cannot hold markup") {
        Site t;
        t.add("dave", "admin");
        const Session admin = t.sign_in("dave", kPassword, "198.51.100.1");
        for (int i = 0; i < 5; ++i) t.login("<script>alert(1)</script>", "x", "203.0.113.9");
        for (int i = 0; i < 5; ++i) t.login("fine.name", "x", "203.0.113.8");
        const std::string log = t.get(admin, "/admin/api/audit").body;
        ASSERT_TRUE(log.find('<') == std::string::npos && log.find('>') == std::string::npos);
        ASSERT_TRUE(log.find("(not a username)") != std::string::npos && log.find("fine.name") != std::string::npos);
        ASSERT_EQ(t.post(admin, "/admin/api/users", "{\"username\":\"<img src=x>\",\"role\":\"admin\"}").status, 400);
        const std::string users = t.get(admin, "/admin/api/users").body;
        ASSERT_TRUE(users.find('<') == std::string::npos);
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

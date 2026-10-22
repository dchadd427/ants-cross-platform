#pragma once

// The routes of the map submissions (docs/MAP_SUBMISSIONS.md): what a player's browser may ask (send a map, list one's own, withdraw) and what only the owner may ask (the review page's API). One
// handler for one door (HttpServer::listen_door), behind the site's nginx, which decides which paths reach it and sets the real client address.
//
// Trust: the door has no secret. The player's calls are open to everybody and limited by address. The owner's calls need the cookie that this server makes at POST /admin/api/login (a username and a
// password: admin_accounts.hpp), signed with the session secret, naming the account and the generation of its sessions; a change also needs the CSRF token of that session (GET /admin/api/me). Everybody
// else gets, for every route under /admin/api except /me, /login and /logout, the very answer of a path that does not exist, so nothing is learned about what is there.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_server/admin_accounts.hpp"
#include "ants_server/submissions.hpp"

namespace ants::server {

struct SubmissionApiConfig {
    std::string session_secret;              // the secret that signs the session cookie (at least 32 characters). Empty: the owner's API is off, every /admin/api route is a 404
    bool cookie_secure{true};                // the cookie carries Secure (the page is served over https); false only for a trial over plain http on this machine
    int64_t session_seconds{12 * 3600};      // how long a sign-in lasts
    bool trust_client_header{true};          // take the client's address from X-Client-Address (the proxy sets it, and removes one the visitor sent); false: the address of the connection
    size_t attempts_per_hour{20};            // POST /submissions calls for one address and hour, whatever came of them (checking a map costs time)
    size_t checks_per_window{6};             // maps that are checked at the same time window for all addresses together
    size_t checks_per_address_window{2};     // ... of which one address may use this many (an address that has used them is told busy and costs the others nothing)
    uint32_t check_window_ms{10000};
    uint32_t check_budget_ms{1500};          // the time that checking maps may take in one window: the server's one thread also runs the matches, so a window that has used it up answers 503 busy
    // The test maps of the Map Maker (POST /submissions?test=1) have limits of their own, so that nobody can use up the real submissions' allowance with them, and they cannot flush the store of test maps or fill the
    // server's thread: an address may send this many in an hour, all addresses together this many, and their checks have a window (the same length) and a time budget of their own, smaller than the real one.
    size_t test_attempts_per_hour{30};
    size_t test_total_per_hour{120};
    size_t test_checks_per_window{2};
    size_t test_checks_per_address_window{1};
    uint32_t test_check_budget_ms{600};
};

class SubmissionApi {
public:
    SubmissionApi(SubmissionStore& store, AdminAccounts& accounts, SubmissionApiConfig config);

    /// Answers one request. `now_s` is the wall clock (seconds since 1970), `now_ms` a steady millisecond clock.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);

    /// Called after the set of approved maps changed (an approval, a take-down, a put-back), so that the lobby's list follows
    void set_on_approved_changed(std::function<void()> fn) { on_approved_changed_ = std::move(fn); }
    /// Called after the set of test maps changed (a test map came in, or one was dropped to make room): the lobby's test maps follow, and the approved maps are not read again
    void set_on_test_changed(std::function<void()> fn) { on_test_changed_ = std::move(fn); }

    /// The limit on the body of a request, asked on its head: 512 KB for a Classic map, 4 MiB for an Extended one, a few KB for anything else (DoorOptions::body_limit)
    static size_t body_limit(const ctl::HttpRequest& head);
    /// The largest body of any request: the door's ceiling
    static size_t max_body();

    bool owner_api_on() const { return config_.session_secret.size() >= 32; }

private:
    struct Session {
        bool valid{false};
        User user;
        std::string nonce;
    };
    ctl::HttpResponse submit(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse maker_routes(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse admin_routes(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse admin_post(const ctl::HttpRequest& request, const Session& who, const std::string& tail, int64_t now_s);
    ctl::HttpResponse user_routes(const ctl::HttpRequest& request, const Session& who, const std::string& tail, int64_t now_s);
    ctl::HttpResponse login(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse logout(const ctl::HttpRequest& request, const Session& who, int64_t now_s);
    ctl::HttpResponse change_password(const ctl::HttpRequest& request, const Session& who, int64_t now_s, uint32_t now_ms);
    bool post_guard(const ctl::HttpRequest& request, const Session& who, ctl::JsonValue& body, ctl::HttpResponse& refusal) const;
    Session session_of(const ctl::HttpRequest& request, int64_t now_s) const;
    std::string make_cookie(const User& user, int64_t now_s, std::string& csrf) const;
    std::string clear_cookie() const;
    std::string csrf_for(const std::string& nonce, const User& user) const;
    Actor actor_of(const Session& who, const ctl::HttpRequest& request) const;
    /// The checking window of one kind of upload: how many maps and how much time the checks of the window may take, for all addresses and for one
    struct CheckWindow {
        bool started{false};
        uint32_t start_ms{0};
        size_t count{0};
        uint32_t spent_ms{0};
        std::map<std::string, size_t> by_address;               // address identity -> the checks it used in this window (at most `per_window` entries)
    };
    bool address_allowed(const std::string& identity, int64_t now_s);
    bool test_allowed(const std::string& identity, int64_t now_s);
    bool check_window_open(CheckWindow& window, uint32_t now_ms, const std::string& identity, size_t per_window, size_t per_address, uint32_t budget_ms);
    ctl::HttpResponse submit_test(const ctl::HttpRequest& request, const std::string& identity, int64_t now_s, uint32_t now_ms);

    SubmissionStore& store_;
    AdminAccounts& accounts_;
    SubmissionApiConfig config_;
    std::function<void()> on_approved_changed_;
    std::function<void()> on_test_changed_;
    std::map<std::string, std::vector<int64_t>> attempts_;      // address identity -> the seconds of its recent POSTs
    CheckWindow window_;                                        // the checks of real submissions
    CheckWindow test_window_;                                   // the checks of test maps
    std::map<std::string, std::vector<int64_t>> test_attempts_;      // address identity -> the seconds of its recent test uploads
    std::vector<int64_t> test_all_;                             // ... and of all addresses together
};

/// The address that a request counts under: the proxy's header when trusted and well formed, else the connection's. An IPv4-mapped IPv6 address is its IPv4 address; any other IPv6 address
/// is its /64 (one person's machines share a /64, so a /64 is one address). "" when nothing usable is known.
std::string client_identity(const ctl::HttpRequest& request, bool trust_header);

/// The text with its %XX escapes decoded; false for a bad escape. (A '+' stays a plus sign.)
bool percent_decode_text(const std::string& in, std::string& out);

/// Percent-decoded value of `name` in a query string, false when it is absent or has a bad escape. A '+' is a plus sign (the pages use encodeURIComponent).
bool query_value(const std::string& query, const std::string& name, std::string& out);

}  // namespace ants::server

#pragma once

// The routes of the map submissions (docs/MAP_SUBMISSIONS.md): what a player's browser may ask (send a map, list one's own, withdraw) and what only the owner may ask (the review page's API). One
// handler for one door (HttpServer::listen_door), behind the site's nginx, which decides which paths reach it and sets the real client address.
//
// Trust: the door has no secret. The player's calls are open to everybody and limited by address. The owner's calls need the cookie that the login helper (tools/admin_login.py) signs with the
// shared session secret, whose Discord id is the owner's, and for a change also the CSRF token that the page got from GET /admin/api/me. Everybody else gets, for every route under
// /admin/api except /me, the very answer of a path that does not exist, so nothing is learned about what is there.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_server/submissions.hpp"

namespace ants::server {

struct SubmissionApiConfig {
    std::string owner_id;                    // the owner's Discord user id (digits). Empty: the owner's API is off, every /admin/api route is a 404
    std::string session_secret;              // the secret that the login helper signs the owner's cookie with (at least 32 characters). Empty: the owner's API is off
    bool trust_client_header{true};          // take the client's address from X-Client-Address (the proxy sets it, and removes one the visitor sent); false: the address of the connection
    size_t attempts_per_hour{20};            // POST /submissions calls for one address and hour, whatever came of them (checking a map costs time)
    size_t checks_per_window{6};             // maps that are checked at the same time window for all addresses together
    uint32_t check_window_ms{10000};
    uint32_t check_budget_ms{1500};          // the time that checking maps may take in one window: the server's one thread also runs the matches, so a window that has used it up answers 503 busy
};

class SubmissionApi {
public:
    SubmissionApi(SubmissionStore& store, SubmissionApiConfig config);

    /// Answers one request. `now_s` is the wall clock (seconds since 1970), `now_ms` a steady millisecond clock.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);

    /// Called after the set of approved maps changed (an approval, a take-down, a put-back), so that the lobby's list follows
    void set_on_approved_changed(std::function<void()> fn) { on_approved_changed_ = std::move(fn); }

    /// The limit on the body of a request, asked on its head: 512 KB for a Classic map, 4 MiB for an Extended one, a few KB for anything else (DoorOptions::body_limit)
    static size_t body_limit(const ctl::HttpRequest& head);
    /// The largest body of any request: the door's ceiling
    static size_t max_body();

    bool owner_api_on() const { return !config_.owner_id.empty() && config_.session_secret.size() >= 32; }

private:
    struct Session {
        bool valid{false};
        bool owner{false};
        std::string id, name, nonce;
    };
    ctl::HttpResponse submit(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse maker_routes(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse admin_routes(const ctl::HttpRequest& request, int64_t now_s);
    ctl::HttpResponse admin_post(const ctl::HttpRequest& request, const Session& who, const std::string& tail, int64_t now_s);
    Session session_of(const ctl::HttpRequest& request, int64_t now_s) const;
    std::string csrf_of(const Session& s) const;
    bool address_allowed(const std::string& identity, int64_t now_s);
    bool check_window_open(uint32_t now_ms);

    SubmissionStore& store_;
    SubmissionApiConfig config_;
    std::function<void()> on_approved_changed_;
    std::map<std::string, std::vector<int64_t>> attempts_;      // address identity -> the seconds of its recent POSTs
    uint32_t window_start_ms_{0};
    size_t window_count_{0};
    uint32_t window_spent_ms_{0};
    bool window_started_{false};
};

/// The address that a request counts under: the proxy's header when trusted and well formed, else the connection's. An IPv4-mapped IPv6 address is its IPv4 address; any other IPv6 address
/// is its /64 (one person's machines share a /64, so a /64 is one address). "" when nothing usable is known.
std::string client_identity(const ctl::HttpRequest& request, bool trust_header);

/// The text with its %XX escapes decoded; false for a bad escape. (A '+' stays a plus sign.)
bool percent_decode_text(const std::string& in, std::string& out);

/// Percent-decoded value of `name` in a query string, false when it is absent or has a bad escape. A '+' is a plus sign (the pages use encodeURIComponent).
bool query_value(const std::string& query, const std::string& name, std::string& out);

}  // namespace ants::server

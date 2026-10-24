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
#include "ants_server/upload_common.hpp"

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
};

class SubmissionApi {
public:
    SubmissionApi(SubmissionStore& store, AdminAccounts& accounts, SubmissionApiConfig config);

    /// Answers one request. `now_s` is the wall clock (seconds since 1970), `now_ms` a steady millisecond clock.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);

    /// Called after the set of approved maps changed (an approval, a take-down, a put-back), so that the lobby's list follows
    void set_on_approved_changed(std::function<void()> fn) { on_approved_changed_ = std::move(fn); }
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
    bool address_allowed(const std::string& identity, int64_t now_s);

    SubmissionStore& store_;
    AdminAccounts& accounts_;
    SubmissionApiConfig config_;
    std::function<void()> on_approved_changed_;
    std::map<std::string, std::vector<int64_t>> attempts_;      // address identity -> the seconds of its recent POSTs
    CheckWindow window_;                                        // the checks of real submissions
};

}  // namespace ants::server

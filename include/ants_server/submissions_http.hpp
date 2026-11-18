#pragma once

// The routes of the map submissions (docs/MAP_SUBMISSIONS.md): what a player's browser may ask (send a map, list one's own, withdraw) and what only an administrator may ask (the admin page's API). One handler for one
// door (HttpServer::listen_door), behind the site's nginx, which decides which paths reach it and sets the real client address.
//
// Trust: the door has no secret. The player's calls are open to everybody and limited by address. The admin page's calls (/admin/api) need the pass that the site's nginx got from the chat hub's gate and puts in the header
// X-Ants-Admin-Pass (admin_pass.hpp): a signed pass of a minute, used once, that names an administrator or the operator by reference; a change also needs the CSRF token of that account (GET /admin/api/me). Everybody else
// (no pass, a bad one, an old one, one used before, or a server with no key) gets, for every route under /admin/api, the one answer 403 `closed`, so nothing is learned about what is there. There is no password, no cookie
// and no account in this program.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_server/admin_audit.hpp"
#include "ants_server/admin_pass.hpp"
#include "ants_server/submissions.hpp"
#include "ants_server/upload_common.hpp"

namespace ants::server {

struct SubmissionApiConfig {
    std::string gate_secret;                 // the key of the admin page's pass (ANTS_ADMIN_GATE_SECRET, the one setting that the chat hub reads too): 32 to 256 printable characters. Absent or not of that shape: the admin page is closed (every /admin/api call is 403 closed)
    bool trust_client_header{true};          // take the client's address from X-Client-Address (the proxy sets it, and removes one the visitor sent); false: the address of the connection
    size_t attempts_per_hour{20};            // POST /submissions calls for one address and hour, whatever came of them (checking a map costs time)
    size_t checks_per_window{6};             // maps that are checked at the same time window for all addresses together
    size_t checks_per_address_window{2};     // ... of which one address may use this many (an address that has used them is told busy and costs the others nothing)
    uint32_t check_window_ms{10000};
    uint32_t check_budget_ms{1500};          // the time that checking maps may take in one window: the server's one thread also runs the matches, so a window that has used it up answers 503 busy
};

class SubmissionApi {
public:
    SubmissionApi(SubmissionStore& store, AdminAudit& audit, SubmissionApiConfig config);

    /// Answers one request. `now_s` is the wall clock (seconds since 1970), `now_ms` a steady millisecond clock.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);

    /// Called after the set of approved maps changed (an approval, a take-down, a put-back), so that the lobby's list follows
    void set_on_approved_changed(std::function<void()> fn) { on_approved_changed_ = std::move(fn); }
    /// The limit on the body of a request, asked on its head: 512 KB for a Classic map, 4 MiB for an Extended one, a few KB for anything else (DoorOptions::body_limit)
    static size_t body_limit(const ctl::HttpRequest& head);
    /// The largest body of any request: the door's ceiling
    static size_t max_body();

    /// Whether the admin page can open at all: the key is there and good (admin_pass.hpp)
    bool admin_api_on() const { return gate_.open(); }
    /// Whether the setting of the key was there at all (the start line tells "not set" from "not good")
    bool gate_key_given() const { return gate_.given(); }
    bool gate_key_is_sample() const { return gate_.sample(); }
    /// Lines for the operator's log (`admin_gate kind=<word>`: a pass that was refused, at most one line a minute for each kind; never the pass, a name or an address)
    std::vector<std::string> take_notices();

private:
    ctl::HttpResponse submit(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse maker_routes(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse admin_routes(const ctl::HttpRequest& request, int64_t now_s);
    ctl::HttpResponse admin_post(const ctl::HttpRequest& request, const AdminIdentity& who, const std::string& tail, int64_t now_s);
    ctl::HttpResponse audit_list();
    bool post_guard(const ctl::HttpRequest& request, const AdminIdentity& who, ctl::JsonValue& body, ctl::HttpResponse& refusal) const;
    bool admitted(const ctl::HttpRequest& request, int64_t now_s, AdminIdentity& who);
    bool address_allowed(const std::string& identity, int64_t now_s);

    SubmissionStore& store_;
    AdminAudit& audit_;
    SubmissionApiConfig config_;
    AdminGate gate_;
    std::function<void()> on_approved_changed_;
    std::map<std::string, std::vector<int64_t>> attempts_;      // address identity -> the seconds of its recent POSTs
    CheckWindow window_;                                        // the checks of real submissions
    std::map<int, int64_t> pass_logged_;                        // PassVerdict -> the second of the last line about it
    std::vector<std::string> notices_;
};

}  // namespace ants::server

#pragma once

// What the two doors that take a player's map share (docs/MAP_SUBMISSIONS.md): the real submissions door (submissions_http.hpp) and the test-map door (test_maps_http.hpp). One copy of the cheap
// refusals, of the way a finding is told, of the JSON answers, of the address that a request counts under and of the checking window, so that the same bytes get the same verdict at both doors and
// a rule is never written twice. Nothing here knows a database, an account or a folder.

#include <cstdint>
#include <map>
#include <string>

#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"

namespace ants::server {

/// A refusal: the HTTP status, a stable code (docs/MAP_SUBMISSIONS.md lists them) and one sentence in plain words for the person
struct Failure {
    int status{0};
    std::string code;
    std::string message;
    ctl::JsonValue extra;                             // an object whose members are added to the answer (the findings of a map that fails, a suggested name), or null
};

Failure make_failure(int status, const char* code, const char* message);

/// The cheap refusals that a real submission and a test map share: the file is a map of version 8 (Classic) or 9 (Extended), and it is the kind that the request says. On success `real_kind` is the file's own kind.
bool kind_of_file(const std::string& claimed, const std::string& bytes, std::string& real_kind, Failure& why);

/// The warnings of a check, as the owner's page and the Map Maker show them (the notes are not kept; at most 20, each cut to 240 bytes)
ctl::JsonValue kept_findings(const ctl::JsonValue& report);

/// Parses what the map core answers (a few MB of JSON at most)
bool parse_big_json(const std::string& text, ctl::JsonValue& out);

// ---- the answers of a door

ctl::HttpResponse json_reply(int status, const ctl::JsonValue& v);
ctl::HttpResponse error_reply(int status, const char* code, const char* message);
/// The answer to a path that does not exist
ctl::HttpResponse not_found();
ctl::HttpResponse failure_reply(const Failure& f);

/// A request that another site sent (Sec-Fetch-Site says anything but the same origin or no site at all)
bool cross_site(const ctl::HttpRequest& r);
/// Whether the request's content type is `type` (lower case; parameters such as the charset are allowed)
bool content_type_is(const ctl::HttpRequest& r, const char* type);

/// The address that a request counts under: the proxy's header when trusted and well formed, else the connection's. An IPv4-mapped IPv6 address is its IPv4 address; any other IPv6 address
/// is its /64 (one person's machines share a /64, so a /64 is one address). "" when nothing usable is known.
std::string client_identity(const ctl::HttpRequest& request, bool trust_header);

/// The text with its %XX escapes decoded; false for a bad escape. (A '+' stays a plus sign.)
bool percent_decode_text(const std::string& in, std::string& out);

/// Percent-decoded value of `name` in a query string, false when it is absent or has a bad escape. A '+' is a plus sign (the pages use encodeURIComponent).
bool query_value(const std::string& query, const std::string& name, std::string& out);

/// The checking window of one kind of upload: how many maps and how much time the checks of the window may take, for all addresses and for one
struct CheckWindow {
    bool started{false};
    uint32_t start_ms{0};
    size_t count{0};
    uint32_t spent_ms{0};
    std::map<std::string, size_t> by_address;               // address identity -> the checks it used in this window (at most `per_window` entries)
};

/// Asks for one check in `window` (a new window starts when `window_ms` have passed): false when the count, the time budget or the address's share of the window is used up. The server's one thread also
/// runs the matches, so a window that has used its time answers 503 busy.
bool check_window_open(CheckWindow& window, uint32_t window_ms, uint32_t now_ms, const std::string& identity, size_t per_window, size_t per_address, uint32_t budget_ms);

}  // namespace ants::server

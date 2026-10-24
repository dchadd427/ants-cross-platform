#include "ants_server/test_maps_http.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>

namespace ants::server {

namespace {

using ctl::HttpRequest;
using ctl::HttpResponse;
using ctl::JsonValue;

constexpr size_t kSmallBody = 4 * 1024;                  // no other request has a body that is read
constexpr size_t kMaxAddressesKept = 10000;
constexpr int64_t kAttemptWindowS = 3600;
constexpr const char* kRoute = "/test-maps";

}  // namespace

TestMapApi::TestMapApi(TestMapStore& store, TestMapApiConfig config) : store_(store), config_(std::move(config)) {}

const char* TestMapApi::route() { return kRoute; }

size_t TestMapApi::max_body() { return TestMapStore::kMaxBodyBytes; }

size_t TestMapApi::body_limit(const HttpRequest& head) {
    if (head.method == "POST" && head.path == kRoute) return TestMapStore::kMaxBodyBytes;
    return kSmallBody;
}

// An address may send config_.attempts_per_hour an hour and all addresses together config_.total_per_hour, whatever came of the tries
bool TestMapApi::allowed(const std::string& identity, int64_t now_s) {
    const auto old = [&](int64_t t) { return t <= now_s - kAttemptWindowS; };
    all_.erase(std::remove_if(all_.begin(), all_.end(), old), all_.end());
    if (all_.size() >= config_.total_per_hour) return false;
    if (attempts_.size() >= kMaxAddressesKept) {
        for (auto it = attempts_.begin(); it != attempts_.end();) {
            auto& times = it->second;
            times.erase(std::remove_if(times.begin(), times.end(), old), times.end());
            it = times.empty() ? attempts_.erase(it) : std::next(it);
        }
        if (attempts_.size() >= kMaxAddressesKept) return false;           // (that many addresses at once is a flood: nobody new is let in until some time has passed)
    }
    std::vector<int64_t>& times = attempts_[identity];
    times.erase(std::remove_if(times.begin(), times.end(), old), times.end());
    if (times.size() >= config_.attempts_per_hour) return false;
    times.push_back(now_s);
    all_.push_back(now_s);
    return true;
}

HttpResponse TestMapApi::handle(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    if (request.path != kRoute) return not_found();                                // (the only path that exists: nothing else is served, whatever its method or query)
    if (request.method != "POST") return error_reply(405, "method_not_allowed", "This address takes POST.");
    return submit(request, now_s, now_ms);
}

HttpResponse TestMapApi::submit(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    if (cross_site(request)) return error_reply(403, "cross_site", "This request must come from the site's own page.");
    if (!content_type_is(request, "application/octet-stream")) return error_reply(415, "bad_content_type", "Send the map file with the content type application/octet-stream.");
    const std::string identity = client_identity(request, config_.trust_client_header);
    if (identity.empty()) return error_reply(400, "no_address", "The server cannot tell where this request came from.");
    if (request.query.size() > 4096) return error_reply(414, "query_too_long", "The request is too long.");          // (before anything is decoded)
    if (!allowed(identity, now_s)) return error_reply(429, "too_many_tests", "Too many test maps came in from this address or from everybody in the last hour. Please try again later.");
    TestMapInput in;
    if (!query_value(request.query, "kind", in.kind)) in.kind.clear();
    if (!query_value(request.query, "name", in.name)) in.name.clear();
    in.bytes = request.body;
    in.address = identity;
    in.allow_check = [this, now_ms, identity]() { return check_window_open(window_, config_.check_window_ms, now_ms, identity, config_.checks_per_window, config_.checks_per_address_window, config_.check_budget_ms); };
    const auto began = std::chrono::steady_clock::now();
    const TestMapOutcome out = store_.submit(in, now_s);
    window_.spent_ms += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
    if (!out.ok) return failure_reply(out.fail);
    if (store_.take_changed() && on_changed_) on_changed_();          // (only the test maps changed: the approved ones are not read again)
    JsonValue tm = JsonValue::make_object();
    tm.set("file", JsonValue::make_string(out.map.name));
    tm.set("name", JsonValue::make_string(out.clean_name));
    tm.set("kind", JsonValue::make_string(out.map.kind));
    tm.set("width", JsonValue::make_int(out.map.width));
    tm.set("height", JsonValue::make_int(out.map.height));
    tm.set("minutes", JsonValue::make_int(out.map.minutes));
    tm.set("players", JsonValue::make_int(out.map.players));
    tm.set("size", JsonValue::make_int(static_cast<int64_t>(out.map.size)));
    tm.set("hash", JsonValue::make_string(out.hash_text));
    JsonValue usable = JsonValue::make_array();
    for (size_t s = 0; s < 4; ++s) usable.push_back(JsonValue::make_bool(out.usable[s]));
    tm.set("usable", std::move(usable));
    JsonValue o = JsonValue::make_object();
    o.set("test_map", std::move(tm));
    return json_reply(201, o);
}

}  // namespace ants::server

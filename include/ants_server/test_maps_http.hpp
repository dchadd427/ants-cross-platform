#pragma once

// The test-map door (docs/MAP_SUBMISSIONS.md, "Test maps"): ONE route, POST /test-maps?kind=classic|extended&name=<name>, for the Map Maker's "With other players". A handler for a door of its own
// (HttpServer::listen_door, --test-map-port) behind the site's nginx, which sets the real client address. It has no secret, no account, no owner's page and no database: it answers that one route and
// a 404 (or a 405 for another method on that path) for everything else, so a mistake in front of it can at worst send a test upload to a handler that only knows test uploads.
//
// The limits are its own: an address may send test_attempts_per_hour an hour, all addresses together test_total_per_hour (429 too_many_tests, whatever came of the tries), and the checks have a window of
// ten seconds with a count and a time budget (503 busy), so a flood of test maps cannot fill the server's one thread. The store (test_maps.hpp) holds the rest: how many maps, how many bytes, how long.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_server/test_maps.hpp"
#include "ants_server/upload_common.hpp"

namespace ants::server {

struct TestMapApiConfig {
    bool trust_client_header{true};          // take the client's address from X-Client-Address (the proxy sets it, and removes one the visitor sent); false: the address of the connection
    size_t attempts_per_hour{30};            // POST /test-maps calls for one address and hour, whatever came of them (checking a map costs time)
    size_t total_per_hour{120};              // ... and for all addresses together
    size_t checks_per_window{2};             // maps that are checked in one window for all addresses together
    size_t checks_per_address_window{1};     // ... of which one address may use this many (an address that has used them is told busy and costs the others nothing)
    uint32_t check_window_ms{10000};
    uint32_t check_budget_ms{600};           // the time that checking maps may take in one window: the server's one thread also runs the matches
};

class TestMapApi {
public:
    TestMapApi(TestMapStore& store, TestMapApiConfig config);

    /// Answers one request. `now_s` is the wall clock (seconds since 1970), `now_ms` a steady millisecond clock.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);

    /// Called after the set of test maps changed (a test map came in, or one was dropped to make room): the lobby's test maps follow
    void set_on_changed(std::function<void()> fn) { on_changed_ = std::move(fn); }

    /// The limit on the body of a request, asked on its head: 2 MiB for POST /test-maps (the store holds a Classic map to 512 KB), a few KB for anything else (DoorOptions::body_limit)
    static size_t body_limit(const ctl::HttpRequest& head);
    /// The largest body of any request: the door's ceiling
    static size_t max_body();
    /// The only path that this door serves
    static const char* route();

private:
    bool allowed(const std::string& identity, int64_t now_s);
    ctl::HttpResponse submit(const ctl::HttpRequest& request, int64_t now_s, uint32_t now_ms);

    TestMapStore& store_;
    TestMapApiConfig config_;
    std::function<void()> on_changed_;
    CheckWindow window_;
    std::map<std::string, std::vector<int64_t>> attempts_;      // address identity -> the seconds of its recent uploads
    std::vector<int64_t> all_;                                  // ... and of all addresses together
};

}  // namespace ants::server

#pragma once

// An in-memory network for tests and development: pairs of connected endpoints with a configurable one-way latency and jitter. Delivery is
// reliable and ordered like a data channel (a message never overtakes an earlier one on the same direction), and driven by a clock that the
// caller advances, so a whole match over a bad network runs deterministically in milliseconds.

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ants_net/transport.hpp"

namespace ants::net {

class LoopbackNetwork {
public:
    struct Link {
        uint32_t latency_ms{20};      // one way
        uint32_t jitter_ms{0};        // each message takes latency + a pseudo-random 0 .. jitter (ordering is preserved)
    };

    explicit LoopbackNetwork(uint32_t seed = 1);
    ~LoopbackNetwork();
    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    /// A connected pair (a, b): what a sends, b receives after the link's delay and vice versa. The endpoints stay valid while the network lives.
    std::pair<Connection*, Connection*> connect(Link link);
    /// The current time; messages become receivable when it reaches their delivery time. The caller's clock is 32 bits and may wrap (tests of a server that has run
    /// for weeks): the network keeps its own 64-bit time, advanced by the wrap-safe difference between two calls.
    void set_time(uint32_t now_ms) {
        now_ += static_cast<uint32_t>(now_ms - last_set_);
        last_set_ = now_ms;
    }
    /// Cuts the link (both ends see Closed); with `fail` they see Failed instead
    void cut(Connection* endpoint, bool fail = false);

private:
    class Endpoint;
    struct Pending {
        uint64_t deliver_at;
        std::vector<uint8_t> data;
    };
    uint32_t next_random() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return rng_ >> 8;
    }

    std::vector<std::unique_ptr<Endpoint>> endpoints_;
    uint64_t now_{0};
    uint32_t last_set_{0};
    uint32_t rng_;
};

}  // namespace ants::net

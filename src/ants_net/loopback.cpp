#include "ants_net/loopback.hpp"

#include <algorithm>
#include <deque>

namespace ants::net {

class LoopbackNetwork::Endpoint : public Connection {
public:
    Endpoint(LoopbackNetwork& net, Link link) : net_(net), link_(link) {}

    bool send(const std::vector<uint8_t>& message) override {
        if (state_ != State::Open || peer_ == nullptr || peer_->state_ != State::Open || peer_closed_) return false;
        uint32_t delay = link_.latency_ms;
        if (link_.jitter_ms > 0) delay += net_.next_random() % (link_.jitter_ms + 1);
        if (link_.spike_per_mille > 0 && net_.next_random() % 1000 < link_.spike_per_mille) delay += link_.spike_ms;
        // reliable and ordered: never delivered before an earlier message of this direction
        const uint64_t at = std::max(net_.now_ + delay, last_deliver_);
        last_deliver_ = at;
        peer_->inbox_.push_back(Pending{at, message});
        return true;
    }
    bool poll(std::vector<uint8_t>& message) override {
        if (state_ != State::Open && state_ != State::Closed) return false;            // a failed connection delivers nothing more
        if (inbox_.empty() || inbox_.front().deliver_at > net_.now_) return false;
        message = std::move(inbox_.front().data);
        const uint64_t waited = net_.now_ - inbox_.front().deliver_at;       // the network's clock is the real time of a test: how long the message lay here
        last_age_ms_ = waited > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(waited);
        inbox_.pop_front();
        return true;
    }
    uint32_t last_message_age_ms() const override { return last_age_ms_; }
    // Like a TCP connection: what the peer sent before it closed can still be read; only then does this end see the close
    State state() const override {
        if (state_ != State::Open) return state_;
        if (peer_closed_ && inbox_.empty()) return State::Closed;
        return State::Open;
    }
    void close() override {
        if (state_ != State::Open) return;
        state_ = State::Closed;
        if (peer_ != nullptr) peer_->peer_closed_ = true;
    }

    LoopbackNetwork& net_;
    Link link_;
    Endpoint* peer_{nullptr};
    State state_{State::Open};          // this end's own decision (Open, or closed by us, or cut)
    bool peer_closed_{false};           // the other end closed in an orderly way
    uint64_t last_deliver_{0};
    uint32_t last_age_ms_{0};
    std::deque<Pending> inbox_;
};

LoopbackNetwork::LoopbackNetwork(uint32_t seed) : rng_(seed) {}

LoopbackNetwork::~LoopbackNetwork() = default;

std::pair<Connection*, Connection*> LoopbackNetwork::connect(Link link) {
    endpoints_.push_back(std::make_unique<Endpoint>(*this, link));
    Endpoint* a = endpoints_.back().get();
    endpoints_.push_back(std::make_unique<Endpoint>(*this, link));
    Endpoint* b = endpoints_.back().get();
    a->peer_ = b;
    b->peer_ = a;
    return {a, b};
}

void LoopbackNetwork::cut(Connection* endpoint, bool fail) {
    for (auto& e : endpoints_) {
        if (e.get() != endpoint) continue;
        const Connection::State st = fail ? Connection::State::Failed : Connection::State::Closed;
        e->state_ = st;
        e->inbox_.clear();                              // an abrupt cut loses what was in flight
        if (e->peer_ != nullptr) {
            e->peer_->state_ = st;
            e->peer_->inbox_.clear();
        }
        return;
    }
}

}  // namespace ants::net

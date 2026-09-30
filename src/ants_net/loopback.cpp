#include "ants_net/loopback.hpp"

#include <algorithm>

namespace ants::net {

class LoopbackNetwork::Endpoint : public Connection {
public:
    Endpoint(LoopbackNetwork& net, Link link) : net_(net), link_(link) {}

    bool send(const std::vector<uint8_t>& message) override {
        if (state_ != State::Open || peer_ == nullptr || peer_->state_ != State::Open) return false;
        uint32_t delay = link_.latency_ms;
        if (link_.jitter_ms > 0) delay += net_.next_random() % (link_.jitter_ms + 1);
        // reliable and ordered: never delivered before an earlier message of this direction
        const uint32_t at = std::max(net_.now_ + delay, last_deliver_);
        last_deliver_ = at;
        peer_->inbox_.push_back(Pending{at, message});
        ++net_.messages_;
        return true;
    }
    bool poll(std::vector<uint8_t>& message) override {
        if (inbox_.empty() || inbox_.front().deliver_at > net_.now_) return false;
        message = std::move(inbox_.front().data);
        inbox_.pop_front();
        return true;
    }
    State state() const override { return state_; }
    void close() override {
        state_ = State::Closed;
        if (peer_ != nullptr && peer_->state_ == State::Open) peer_->state_ = State::Closed;
    }

    LoopbackNetwork& net_;
    Link link_;
    Endpoint* peer_{nullptr};
    State state_{State::Open};
    uint32_t last_deliver_{0};
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
        e->state_ = fail ? Connection::State::Failed : Connection::State::Closed;
        if (e->peer_ != nullptr) e->peer_->state_ = fail ? Connection::State::Failed : Connection::State::Closed;
        return;
    }
}

}  // namespace ants::net

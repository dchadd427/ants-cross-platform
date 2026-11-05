#pragma once

// The links of machines whose reports are not their state (the tests of a dedicated server's verdict on the state hashes, test_lockstep S2.40 - S2.59 and test_server S3.250 and on): the machine itself is
// sound and plays as any other. HashSkew: the Hash messages that it sends say another state, nothing else is changed. HashHold: its acknowledgements run behind, it reports nothing on its own, and
// report_odd() says another state, once, for the newest turn it holds.
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"

namespace ants_test {

class HashSkew final : public ants::net::Connection {
public:
    explicit HashSkew(ants::net::Connection* inner) : inner_(inner) {}
    bool send(const std::vector<uint8_t>& message) override {
        ants::net::HashMsg hash;
        if (skew && ants::net::peek_type(message) == ants::net::MsgType::Hash && ants::net::decode(message, hash) && hash.turn >= from_turn) {
            hash.hash.total ^= 0x5AA55AA5ull;
            hash.hash.ants ^= 1u;
            ++skewed;
            return inner_->send(ants::net::encode(hash));
        }
        return inner_->send(message);
    }
    bool poll(std::vector<uint8_t>& message) override { return inner_->poll(message); }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t last_message_age_ms() const override { return inner_->last_message_age_ms(); }

    bool skew{true};          // false: the messages go through as they are
    uint32_t from_turn{0};    // only the reports of this turn and the turns after it are changed
    uint32_t skewed{0};       // how many were

private:
    ants::net::Connection* inner_;
};

class HashHold final : public ants::net::Connection {
public:
    /// `clock` is the clock of the test (the one that the machine is updated with); the acknowledgements reach the host `ack_delay_ms` late
    HashHold(ants::net::Connection* inner, const uint32_t* clock, uint32_t ack_delay_ms) : inner_(inner), clock_(clock), delay_(ack_delay_ms) {}
    bool send(const std::vector<uint8_t>& message) override {
        ants::net::HashMsg hash;
        switch (ants::net::peek_type(message)) {
            case ants::net::MsgType::TurnAck:
                late_.emplace_back(*clock_, message);
                return true;
            case ants::net::MsgType::Hash:
                if (!ants::net::decode(message, hash)) return inner_->send(message);
                held_[hash.turn] = hash.hash;
                return true;
            default:
                return inner_->send(message);
        }
    }
    bool poll(std::vector<uint8_t>& message) override {
        while (!hold_acks && !late_.empty() && *clock_ - late_.front().first >= delay_) {      // (the session polls at every update: the acknowledgements that are due leave then)
            inner_->send(late_.front().second);
            late_.pop_front();
        }
        return inner_->poll(message);
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t last_message_age_ms() const override { return inner_->last_message_age_ms(); }

    /// Reports the newest turn that the machine has hashed, with another state than its own; the turn (0: it has hashed none). A repeat for the same turn is a repeat to the host.
    uint32_t report_odd() {
        if (held_.empty()) return 0;
        ants::net::HashMsg hash;
        hash.turn = held_.rbegin()->first;
        hash.hash = held_.rbegin()->second;
        hash.hash.total ^= 0x5AA55AA5ull;
        inner_->send(ants::net::encode(hash));
        return hash.turn;
    }

    bool hold_acks{false};    // true: no acknowledgement leaves, however old

private:
    ants::net::Connection* inner_;
    const uint32_t* clock_;
    uint32_t delay_;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> late_;
    std::map<uint32_t, ants::sim::StateHash> held_;
};

}  // namespace ants_test

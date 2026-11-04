#pragma once

// A client's link as a machine would use it whose reports are not its state (the tests of a dedicated server's verdict on the state hashes, test_lockstep S2.40 - S2.59 and test_server S3.250 and
// on): the machine itself is sound, it plays and acknowledges as any other, but the Hash messages that it sends say another state. Nothing else is changed.
#include <cstdint>
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

}  // namespace ants_test

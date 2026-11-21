// The relay's credential in a server's room (docs/P2P_HINTS.md, docs/SERVER.md "The relay for the players' channels"): which of a room's players are given one and for how long it lasts. The session's own rules
// (the number of credentials, the empty answer) are tested in the lock-step suite (P25.12 - P25.14); what only the room decides is here: that its match counts the people who began it, and that the credential
// lives as long as the match can (the room's limit of play and, in a room that holds seats, the pauses that the seats may hold) and no longer.
#include "server_fixtures.hpp"

#include "ants_net/p2p.hpp"
#include "ants_net/turn.hpp"

namespace {

constexpr char kSecret[] = "room-secret";

// What the persons of a room were answered when they asked for the relay once the match was running
struct RelayAnswers {
    bool running{false};
    std::vector<std::vector<net::IceConfigMsg>> by_person;
};

// A room that starts with `people` persons (and the computer player that its specification names, if it does), on a server with a relay (its own limit: three hours). Every person says P2pConfig when the
// match is running.
RelayAnswers relay_room(const RoomSpec& spec, size_t people) {
    ServerLimits limits;
    auto turn = std::make_shared<net::TurnSettings>();
    turn->urls = {"stun:relay.example:3478", "turn:relay.example:3478?transport=udp"};
    turn->credential = net::make_turn_signer(kSecret);
    limits.turn = turn;
    World w(limits);
    RelayAnswers out;
    if (!w.mgr.create_room(spec, w.now).ok) return out;
    std::vector<Client*> persons;
    for (size_t i = 0; i < people; ++i) persons.push_back(&w.connect("Person" + std::to_string(i), spec.code));
    for (Client* c : persons) c->next_order_ms = 0xFFFFFFFFu;
    for (int i = 0; i < 400 && w.status(spec.code).state != RoomState::Running; ++i) w.run(10);
    out.running = w.status(spec.code).state == RoomState::Running;
    w.run(300);
    out.by_person.resize(people);
    for (size_t i = 0; i < people; ++i) {
        if (persons[i]->session == nullptr) return out;
        persons[i]->session->set_on_p2p([&out, i](const std::vector<uint8_t>& m) {
            net::IceConfigMsg ice;
            if (net::decode(m, ice)) out.by_person[i].push_back(ice);
        });
        persons[i]->session->send_p2p(net::encode(net::P2pConfigMsg{false}));
    }
    w.run(500);
    return out;
}

uint32_t unix_seconds() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }

}  // namespace

void run_relay_tests() {
    TEST_CASE("S3.305 A Room Gives The Relay's Credential To The People Of A Match Of Two Or More People And Not To One Person Playing Computer Players; It Lasts As Long As The Match Can And No Longer") {
        const uint32_t run_s = 20u * 60u;
        for (const bool holds_seats : {false, true}) {
            RoomSpec spec = spec_of(holds_seats ? "RL-1" : "RL-2", 2);
            spec.run_ms = run_s * 1000u;
            spec.reconnect = holds_seats;
            const RelayAnswers a = relay_room(spec, 2);
            ASSERT_TRUE(a.running && a.by_person.size() == 2);
            const uint32_t longest_s = run_s + (holds_seats ? spec.max_pause_ms / 1000u : 0u);       // the match's limit of play, and the pauses that a room which holds seats may add
            for (const auto& person : a.by_person) {
                ASSERT_EQ(person.size(), 1u);
                ASSERT_TRUE(!person[0].urls.empty() && !person[0].credential.empty());
                const uint32_t now_s = unix_seconds();
                ASSERT_TRUE(net::turn_credential_valid(kSecret, person[0].username, person[0].credential, now_s));
                const uint32_t life_s = person[0].expires - now_s;
                ASSERT_TRUE(life_s >= longest_s);                                                    // the credential outlasts the longest match
                ASSERT_TRUE(life_s <= longest_s + 1800u);                                           // ... by a margin and no more (the relay's own limit is three hours)
            }
        }
        // one person and a computer player: nobody to open a channel to, and no credential is made
        RoomSpec solo = spec_of("RL-3", 2);
        solo.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
        const RelayAnswers s = relay_room(solo, 1);
        ASSERT_TRUE(s.running && s.by_person.size() == 1 && s.by_person[0].size() == 1);
        ASSERT_TRUE(s.by_person[0][0].urls.empty() && s.by_person[0][0].username.empty() && s.by_person[0][0].credential.empty());
    } TEST_END();
}

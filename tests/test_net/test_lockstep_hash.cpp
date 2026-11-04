// Tests of the lock-step network core: a dedicated server's referee and the seats whose state differs from its own (docs/NETWORK_PORT.md, "Desync"; session.hpp, "A seat whose state differs from the referee's")
#include "lockstep_fixtures.hpp"

#include "../common/hash_skew.hpp"

void run_hash_tests() {
    TEST_CASE("S2.40 Dedicated Server: A Seat Whose State Differs From The Referee's Is Dropped And The Others Go On: It Alone Is Told, The Match Is Not Stopped, Its Ants Die On Every Machine, And The Rest Stay Equal To The Referee") {
        ServerMatch m(1, 4, {40, 10});
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());
        m.sims[3]->get_unit(m.ids.ants[3][1]).hp = 3;                                // a hit point that no command explains
        const uint32_t before = m.now;
        m.run(2000, false);
        ASSERT_TRUE(m.now - before <= 2000);                                         // named within a second of play, with the subsystem
        ASSERT_FALSE(m.host->frozen());                                              // the match goes on
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_FALSE(m.host->client_present(3));                                     // the seat is out ...
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(m.host->client_present(p) && !m.clients[p]->desynced());     // ... the others are not told and stay
        ASSERT_TRUE(m.clients[3]->desynced());                                       // it alone is told
        const DesyncMsg d = m.clients[3]->desync();
        ASSERT_EQ(d.player, 3);
        ASSERT_TRUE(d.peer.ants != d.host.ants && d.peer.grid == d.host.grid && d.peer.players == d.host.players);       // only the ants differ
        const uint32_t sealed = m.host->turns_sealed();
        m.run(8000);                                                                 // the three who are left play on for eight more seconds
        ASSERT_TRUE(m.host->turns_sealed() >= sealed + 150);
        ASSERT_FALSE(m.host->frozen());
        m.settle();
        ASSERT_TRUE(m.referee.is_player_dropped(3));
        for (uint8_t p = 0; p < 3; ++p) {
            ASSERT_FALSE(m.referee.is_player_dropped(p));
            ASSERT_TRUE(m.sims[p]->is_player_dropped(3) && !m.sims[p]->is_player_dropped(p));      // the drop travelled in the turn stream: every machine dropped the team at the same tick
            ASSERT_TRUE(m.sims[p]->state_hash() == m.referee.state_hash());
            size_t ants_of_3 = 0;
            for (const auto& a : m.sims[p]->get_world_state().ants) ants_of_3 += a.player_id == 3;
            ASSERT_EQ(ants_of_3, 0u);                                                // the team's ants died
        }
    } TEST_END();
}

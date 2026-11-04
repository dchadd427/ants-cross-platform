// The tests of a dedicated server's room and the seats whose state differs from the referee's (docs/NETWORK_PORT.md, "Desync"; the verdict itself is Sequencer::judge_hashes, tested in test_lockstep S2.40 - S2.59). A
// machine that reports a state which is not the one it holds is a Client whose link changes its Hash messages (tests/common/hash_skew.hpp): it plays as any other. Included by test_server_files.cpp, which holds
// (through server_fixtures.hpp) the harness; test_server.cpp calls run_state_hash_tests().
#pragma once

#include "../common/hash_skew.hpp"

namespace {

// Runs the world until `cond` holds (at most `max_ms` of its time)
template <typename Cond>
bool world_until(World& w, Cond cond, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms; t += 10) {
        if (cond()) return true;
        w.run(10);
    }
    return cond();
}

}  // namespace

void run_state_hash_tests() {
    TEST_CASE("S3.250 A Seat Whose State Hash Differs From The Referee's Is Dropped, The Room Is Not Failed: Two Honest Seats And One That Reports Another State, The Match Goes On For The Two") {
        std::unique_ptr<ants_test::HashSkew> skew;                                       // (the link of Cat's machine: declared before the world that holds the machine)
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("HASH-1", 3), w.now).ok);
        Client& ann = w.connect("Ann", "HASH-1");
        Client& bob = w.connect("Bob", "HASH-1");
        Client& cat = w.connect("Cat", "HASH-1");
        ASSERT_TRUE(world_until(w, [&] { return ann.session && bob.session && cat.session; }, 5000));
        skew = std::make_unique<ants_test::HashSkew>(cat.end);
        cat.session->set_connection(skew.get());                                         // from now on Cat reports a state that it does not hold
        w.run(kPre + 3000);                                                              // the first hash of the match is the one of turn 19, a second after the first turn
        RoomStatus s = w.status("HASH-1");
        ASSERT_TRUE(skew->skewed >= 1);
        ASSERT_TRUE(s.state == RoomState::Running);                                      // (the room used to fail here: "the simulation of seat 2 diverged at turn 19", and all three were sent away)
        ASSERT_EQ(s.joined, 3);
        ASSERT_TRUE(cat.session->desynced());                                            // Cat alone is told ...
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());                // ... and is out
        ASSERT_FALSE(cat.end->is_open());
        ASSERT_TRUE(ann.end->is_open() && bob.end->is_open());
        const uint32_t ticks = s.ticks;
        w.run(3000);
        s = w.status("HASH-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks >= ticks + 50);              // the match goes on for the two
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
    } TEST_END();
}

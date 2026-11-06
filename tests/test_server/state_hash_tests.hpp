// The tests of a dedicated server's room and the seats whose state differs from the referee's (docs/NETWORK_PORT.md, "Sessions" and "Room"; the verdict itself is Sequencer::judge_hashes, tested in test_lockstep
// S2.40 - S2.57). A machine that reports a state which is not the one it holds is a Client whose link changes its Hash messages (tests/common/hash_skew.hpp): it plays as any other. A machine whose state really
// differs is one whose engine was changed after the map was loaded (RClient::tamper).
//   S3.250 - S3.252  a seat that differs is dropped and the room goes on (the cases that fail without the change); a match of two ends with the seat as the quitter: the results, the replay (kept from 30 s, and not
//                    before), the history record that is made of it; the line in the server's log at the moment of the drop (S3.250)
//   S3.253 - S3.254  the room fails, as it always did, when no seat at the table agrees with the referee; a computer player's seat is not touched
//   S3.255 - S3.257  a room that holds seats (the key of the dropped seat is told "dropped", the status counts the drop), a machine that really differs, a link that is no seat's
//   S3.258           a room that holds seats, one odd report from a machine whose acknowledgements run late, while the other seat's link is lost or just after it is back: the room goes on
// The codes that the control interface draws, from the bytes of the operating system's generator, are tested in test_server_core.cpp (S3.10c the alphabet, S3.10d the source of the bytes).
// Included by test_server_files.cpp, which holds (through server_fixtures.hpp) the harness and, from replay_tests.hpp, the helpers of the replays; test_server.cpp calls run_state_hash_tests(). The mutants:
// tests/data/state_hash_mutants.json (tools/mutate.py), every one caught.
#pragma once

#include "../common/hash_skew.hpp"
#include "ants_server/control.hpp"
#include "ants_server/history_record.hpp"

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

// A hash message as a machine that reports another state would send it
std::vector<uint8_t> odd_hash_message(uint32_t turn) {
    net::HashMsg h;
    h.turn = turn;
    h.hash.total = 0xBADull;
    return net::encode(h);
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
        ASSERT_TRUE(s.state == RoomState::Running);                                      // the room goes on
        ASSERT_EQ(s.joined, 3);
        ASSERT_EQ(s.drops_by_state, 1u);
        {   // the log has a line for it at the moment of the drop: the room, the turn, how many differed and how many agreed (never a name)
            const std::vector<std::string> notes = w.mgr.take_notices();
            std::vector<std::string> mine;
            for (const std::string& line : notes) {
                if (line.find("differed from the referee's") != std::string::npos) mine.push_back(line);
            }
            ASSERT_EQ(mine.size(), size_t{1});
            ASSERT_TRUE(mine[0].find("room HASH-1: 1 seat dropped at turn 19 for a state that differed from the referee's; ") == 0);
            ASSERT_TRUE(mine[0].find("; 1 seat agreed with it") != std::string::npos || mine[0].find("; 2 seats agreed with it") != std::string::npos);
            ASSERT_TRUE(mine[0].find("Cat") == std::string::npos);
            ASSERT_TRUE(w.mgr.take_notices().empty());                                   // (said once)
        }
        ASSERT_TRUE(cat.session->desynced());                                            // Cat alone is told ...
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());                // ... and is out
        ASSERT_FALSE(cat.end->is_open());
        ASSERT_TRUE(ann.end->is_open() && bob.end->is_open());
        const uint32_t ticks = s.ticks;
        w.run(3000);
        s = w.status("HASH-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks >= ticks + 50);              // the match goes on for the two
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        sim::Command quit;                                                                // and ends as any match does: Bob quits, one other side is left, the quit ends the match and ranks Bob last
        quit.type = sim::CommandType::Quit;
        quit.issuer = bob.lobby->my_seat();
        ASSERT_TRUE(bob.session->submit(quit));
        w.run(2000);
        s = w.status("HASH-1");
        ASSERT_TRUE(s.state == RoomState::Finished && s.quitter == 1);
        ASSERT_EQ(s.rows.size(), size_t{2});                                              // Cat has no row: it left the match before the end, as any seat that is dropped
        ASSERT_TRUE(s.rows[0].first == 0 && s.rows[0].winner && s.rows[1].first == 1 && !s.rows[1].winner);
        ASSERT_TRUE(s.referee_hash != 0 && s.drops_by_state == 1);
    } TEST_END();

    TEST_CASE("S3.251 A Match Of Two That Ran For More Than 30 s: The Seat Whose State Hash Differs Is Dropped As A Quitter Is: Its Quit Ends The Match, Its Row Is The Last Whatever The Scores Are, The Other Seat Wins; The Replay Is Kept, Plays Out On A Fresh Engine, And The History Record Of It Names The Seat As The Quitter, As Having Left, And The Other As The Winner") {
        ReplayClock clock;
        std::unique_ptr<ants_test::HashSkew> skew;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("hash-late", &clock), false, why));
        ASSERT_TRUE(w.mgr.create_room(spec_of("HASH-2", 2), w.now).ok);
        Client& ann = w.connect("Ann", "HASH-2");
        Client& cat = w.connect("Cat", "HASH-2");
        ASSERT_TRUE(world_until(w, [&] { return ann.session && cat.session; }, 5000));
        skew = std::make_unique<ants_test::HashSkew>(cat.end);
        skew->skew = false;                                                              // Cat's reports are right for the first 36 s of play
        cat.session->set_connection(skew.get());
        w.run(kPre + 36000);
        RoomStatus s = w.status("HASH-2");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns >= 650 && s.drops_by_state == 0);
        ASSERT_TRUE(skew->skewed == 0 && !cat.session->desynced());
        skew->skew = true;
        w.run(4000);
        s = w.status("HASH-2");
        ASSERT_TRUE(skew->skewed >= 1);
        ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "the match ended");     // the quit ended the match by its rules
        ASSERT_EQ(s.quitter, 1);                                                          // Cat's seat
        ASSERT_EQ(s.drops_by_state, 1u);
        ASSERT_EQ(s.rows.size(), size_t{2});
        ASSERT_TRUE(s.rows[0].first == 0 && s.rows[0].name == "Ann" && s.rows[0].winner);
        ASSERT_TRUE(s.rows[1].first == 1 && s.rows[1].name == "Cat" && !s.rows[1].winner);       // the last row, whatever the scores
        ASSERT_TRUE(cat.session->desynced() && !ann.session->desynced());
        ASSERT_TRUE(ann.sim.is_match_over() && ann.sim.quitter() == 1);                  // Ann's own game ended the same way
        ASSERT_TRUE(s.replay_kept && s.replay_note.empty() && ReplayStore::valid_file_name(s.replay_file));
        const StoredReplay stored = load_stored(*w.mgr.replay_store(), s.replay_file);
        ASSERT_TRUE(stored.ok && stored.rep.complete);
        replay::Outcome out;
        ASSERT_TRUE(plays_out(stored.rep, out));                                          // the file plays out on a fresh engine to every hash
        ASSERT_TRUE(out.match_over && out.result.quitter == 1);
        bool quit_in_file = false;
        for (const replay::TimedCommand& tc : stored.rep.commands) quit_in_file = quit_in_file || (tc.command.type == sim::CommandType::Quit && tc.command.issuer == 1);
        ASSERT_TRUE(quit_in_file);                                                        // the Quit of the seat is in the file, as the quit of a player is
        const HistoryRecord rec = make_history_record("ants-TINY-20261008-143209Z", kReplayT0, stored.rep, out);
        ASSERT_TRUE(rec.quitter == 1 && rec.finished && std::string(rec.result()) == "cut" && !rec.draw && rec.turns >= 600);
        ASSERT_TRUE(rec.seats.size() == 2 && rec.seats[1].left && !rec.seats[0].left);
        ASSERT_TRUE(rec.seats[0].winner && !rec.seats[1].winner);
        ASSERT_TRUE(rec.rows.size() == 2 && rec.rows[0].seats[0] == 0 && rec.rows[0].winner && rec.rows[1].seats[0] == 1 && !rec.rows[1].winner);
    } TEST_END();

    TEST_CASE("S3.252 The Same In The First Seconds Of A Match: The Seat Is Dropped As A Quitter And The Match Is Over; It Ran Less Than 30 s, So It Is Not Kept (The Status Says Why) - A Match Of Two That Is Cut Short Leaves Nothing In The Store") {
        ReplayClock clock;
        std::unique_ptr<ants_test::HashSkew> skew;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("hash-early", &clock), false, why));
        ASSERT_TRUE(w.mgr.create_room(spec_of("HASH-3", 2), w.now).ok);
        Client& ann = w.connect("Ann", "HASH-3");
        Client& cat = w.connect("Cat", "HASH-3");
        ASSERT_TRUE(world_until(w, [&] { return ann.session && cat.session; }, 5000));
        skew = std::make_unique<ants_test::HashSkew>(cat.end);
        cat.session->set_connection(skew.get());
        w.run(kPre + 4000);
        const RoomStatus s = w.status("HASH-3");
        ASSERT_TRUE(s.state == RoomState::Finished && s.quitter == 1 && s.drops_by_state == 1);
        ASSERT_TRUE(s.rows.size() == 2 && s.rows[0].first == 0 && s.rows[0].winner && s.rows[1].first == 1 && !s.rows[1].winner);
        ASSERT_TRUE(s.ticks > 20 && s.ticks < 600);
        ASSERT_TRUE(!s.replay_kept && s.replay_file.empty() && s.replay_note.find("ran less than 30 seconds") != std::string::npos);
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{0});
    } TEST_END();

    TEST_CASE("S3.253 When No Seat At The Table Agrees With The Referee The Room Fails, As It Always Did: Both Seats Of A Match Of Two That Report Another State (After 36 s: The Replay Is Kept), And A Person Alone With A Computer Player; Nobody Is Dropped For It, Every Seat Is Told, The Reason Names The Seats") {
        {
            ReplayClock clock;
            std::unique_ptr<ants_test::HashSkew> skews[2];
            World w;
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("hash-fail", &clock), false, why));
            ASSERT_TRUE(w.mgr.create_room(spec_of("HASH-4", 2), w.now).ok);
            Client& ann = w.connect("Ann", "HASH-4");
            Client& cat = w.connect("Cat", "HASH-4");
            ASSERT_TRUE(world_until(w, [&] { return ann.session && cat.session; }, 5000));
            skews[0] = std::make_unique<ants_test::HashSkew>(ann.end);
            skews[1] = std::make_unique<ants_test::HashSkew>(cat.end);
            for (auto& skew : skews) skew->skew = false;
            ann.session->set_connection(skews[0].get());
            cat.session->set_connection(skews[1].get());
            w.run(kPre + 36000);
            ASSERT_TRUE(w.status("HASH-4").state == RoomState::Running);
            for (auto& skew : skews) skew->skew = true;
            w.run(4000);
            const RoomStatus s = w.status("HASH-4");
            ASSERT_TRUE(s.state == RoomState::Failed);
            ASSERT_TRUE(s.reason.find("seats 0 and 1") != std::string::npos && s.reason.find("diverged at turn") != std::string::npos && s.reason.find("no seat agreed with the referee") != std::string::npos);
            ASSERT_EQ(s.drops_by_state, 0u);
            ASSERT_TRUE(s.quitter == 0xFFFF && s.rows.empty());
            ASSERT_TRUE(ann.session->desynced() && cat.session->desynced());
            ASSERT_FALSE(ann.end->is_open() || cat.end->is_open());                       // the room closed every link
            ASSERT_TRUE(s.replay_kept);                                                   // a match of more than 30 s is kept however it ended
        }
        {
            std::unique_ptr<ants_test::HashSkew> skew;
            World w;
            RoomSpec spec = spec_of("HASH-4B", 2);
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "HASH-4B");
            ASSERT_TRUE(world_until(w, [&] { return ann.session != nullptr; }, 5000));
            skew = std::make_unique<ants_test::HashSkew>(ann.end);
            ann.session->set_connection(skew.get());
            w.run(kPre + 3000);
            const RoomStatus s = w.status("HASH-4B");
            ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("seat 0 diverged at turn 19") != std::string::npos && s.drops_by_state == 0);
            ASSERT_TRUE(ann.session->desynced());
        }
    } TEST_END();

    TEST_CASE("S3.254 A Computer Player's Seat Is Not Judged And Not Touched: Two People And A Bot, One Person Reports Another State: The Person Is Dropped, The Other Person And The Bot Go On (The Bot Goes On Deciding), And The Match Ends With The Bot's Row Among The Results") {
        std::unique_ptr<ants_test::HashSkew> skew;
        World w;
        RoomSpec spec = spec_of("HASH-5", 3);
        spec.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}};
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& ann = w.connect("Ann", "HASH-5");
        Client& bob = w.connect("Bob", "HASH-5");
        ASSERT_TRUE(world_until(w, [&] { return ann.session && bob.session; }, 5000));
        skew = std::make_unique<ants_test::HashSkew>(bob.end);
        bob.session->set_connection(skew.get());
        w.run(kPre + 3000);
        RoomStatus s = w.status("HASH-5");
        ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_state == 1);
        ASSERT_TRUE(bob.session->desynced() && !ann.session->desynced() && !bob.end->is_open() && ann.end->is_open());
        ASSERT_TRUE(s.bots.size() == 1 && s.bots[0].seat == 2);
        const uint32_t decisions = s.bot_decisions;
        w.run(3000);
        s = w.status("HASH-5");
        ASSERT_TRUE(s.state == RoomState::Running && s.bot_decisions > decisions);        // the bot goes on
        ASSERT_FALSE(ann.sim.is_player_dropped(2) || ann.sim.is_player_dropped(0));
        ASSERT_TRUE(ann.sim.is_player_dropped(1));
        sim::Command quit;                                                                // Ann ends the match: one other side (the bot) is left
        quit.type = sim::CommandType::Quit;
        quit.issuer = ann.lobby->my_seat();
        ASSERT_TRUE(ann.session->submit(quit));
        w.run(2000);
        s = w.status("HASH-5");
        ASSERT_TRUE(s.state == RoomState::Finished && s.quitter == 0 && row_of(s, 2) != nullptr && row_of(s, 1) == nullptr);
    } TEST_END();

    TEST_CASE("S3.255 A Room That Holds Seats: The Key Of A Seat That Was Dropped For Its State Is Told \"Dropped\" By The Door And Brings Nothing Back, No Pause Is Made For The Seat, The Status Counts The Drop (In The Status And In The Control Interface's JSON) And The Others Go On") {
        std::unique_ptr<ants_test::HashSkew> skew;
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("HASH-6", 3), w.now).ok);
        RClient& ann = w.connect("Ann", "HASH-6");
        RClient& bob = w.connect("Bob", "HASH-6");
        RClient& cat = w.connect("Cat", "HASH-6");
        ASSERT_TRUE(w.until([&] { return ann.session && bob.session && cat.session; }, 5000));
        const net::SeatKey key = cat.session->key();
        ASSERT_FALSE(net::key_is_zero(key));
        skew = std::make_unique<ants_test::HashSkew>(cat.end);
        cat.session->set_connection(skew.get());
        w.run(kPre + 3000);
        RoomStatus s = w.status("HASH-6");
        ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && s.drops_by_state == 1 && s.drops_by_vote == 0 && s.drops_by_cap == 0);
        ASSERT_TRUE(!s.paused && s.absent.empty() && s.rejoins == 0);                       // the seat is out, not held
        ASSERT_TRUE(cat.lost && cat.session->desynced() && !cat.session->reconnecting() && !cat.was_rejected);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        ASSERT_EQ(status_to_json(s).get("drops_by_state").as_int_or(9), 1);
        RClient& again = w.connect("Cat", "HASH-6", 255, key);                              // somebody shows its key
        w.run(1500);
        ASSERT_TRUE(again.was_rejected && again.rejected == net::RejectReason::Dropped && !again.session);
        s = w.status("HASH-6");
        ASSERT_TRUE(s.state == RoomState::Running && !s.paused && s.rejoins == 0 && s.drops_by_state == 1);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        w.run(2000);
        ASSERT_TRUE(w.status("HASH-6").ticks > s.ticks + 20);
    } TEST_END();

    TEST_CASE("S3.256 A Machine Whose State Really Differs (Its Engine Was Changed After The Map Was Loaded) Is Dropped At The First Hash, Told That It Is Out Of Sync, And The Match Goes On For The Others") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("HASH-7", 3), w.now).ok);
        RClient& ann = w.connect("Ann", "HASH-7");
        RClient& bob = w.connect("Bob", "HASH-7");
        RClient& cat = w.connect("Cat", "HASH-7");
        cat.tamper = [](sim::SimulationEngine& engine) {                                    // a hit point that no turn explains
            for (const auto& a : engine.get_world_state().ants) {
                if (a.player_id == 2) {
                    engine.get_unit(a.id).hp = 3;
                    return;
                }
            }
        };
        w.run(kPre + 4000);
        RoomStatus s = w.status("HASH-7");
        ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_state == 1);
        ASSERT_TRUE(cat.session != nullptr && cat.session->desynced() && cat.session->desync().player == 2 && cat.session->desync().host != cat.session->desync().peer);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        ASSERT_TRUE(ann.session->mode() == net::ClientSession::Mode::Normal && bob.session->mode() == net::ClientSession::Mode::Normal);
        w.run(2000);
        ASSERT_TRUE(w.status("HASH-7").ticks > s.ticks + 20);
        ASSERT_TRUE(ann.sim.is_player_dropped(2) && bob.sim.is_player_dropped(2));
    } TEST_END();

    TEST_CASE("S3.257 A Link That Is No Seat's Cannot Report A State: A Stranger Whose First Message Is A Hash Is Refused And Closed By The Door, One That Says Hello For A Match That Runs Is Told So And Has No Seat; Their Hashes, Of Any Turn, Change Nothing In The Room - No Verdict, No Drop, No Stop") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("HASH-8", 2), w.now).ok);
        Client& ann = w.connect("Ann", "HASH-8");
        Client& bob = w.connect("Bob", "HASH-8");
        w.run(kPre + 2000);
        ASSERT_TRUE(ann.session && bob.session && w.status("HASH-8").state == RoomState::Running);
        {
            auto ends = w.net.connect({20, 10});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            for (uint32_t turn : {19u, 39u, 20u, 100000u}) ends.second->send(odd_hash_message(turn));
            w.run(500);
            std::vector<uint8_t> msg;
            net::RejectMsg rejected;
            int reason = -1;
            while (ends.second->poll(msg)) {
                if (net::peek_type(msg) == net::MsgType::Reject && net::decode(msg, rejected)) reason = static_cast<int>(rejected.reason);
            }
            ASSERT_EQ(reason, static_cast<int>(net::RejectReason::BadRequest));              // refused and closed by the door: the first message of a connection is a Hello or nothing
            ASSERT_FALSE(ends.second->is_open());
        }
        {
            auto ends = w.net.connect({20, 10});                                             // a Hello for a match that runs (no key: nobody's seat), and hashes behind it
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            net::HelloMsg hello;
            hello.name = "Dee";
            hello.room = "HASH-8";
            ends.second->send(net::encode(hello));
            for (uint32_t turn : {39u, 59u}) ends.second->send(odd_hash_message(turn));
            w.run(500);
            std::vector<uint8_t> msg;
            net::RejectMsg rejected;
            int reason = -1;
            while (ends.second->poll(msg)) {
                if (net::peek_type(msg) == net::MsgType::Reject && net::decode(msg, rejected)) reason = static_cast<int>(rejected.reason);
            }
            ASSERT_EQ(reason, static_cast<int>(net::RejectReason::MatchRunning));            // told that the match has started: it has no seat
            ASSERT_FALSE(ends.second->is_open());
        }
        const RoomStatus s = w.status("HASH-8");
        ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_state == 0 && s.joined == 2);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        ASSERT_TRUE(ann.end->is_open() && bob.end->is_open());
    } TEST_END();

    TEST_CASE("S3.258 A Room That Holds Seats: One Odd Report From A Machine Whose Acknowledgements Run Late, Sent While The Other Seat's Link Is Lost Or Just After The Seat Is Back, Does Not Fail The Room (Nobody Who Was At The Table For That Turn Can Say Which State Was Right: The Turn Is Let Go); The Next One, Once The Table Has Settled, Drops The Seat As Any Other Report Of Another State Does") {
        for (const bool away : {true, false}) {
            RWorld w;
            const std::string code = away ? "HASH-9A" : "HASH-9B";
            ASSERT_TRUE(w.mgr.create_room(held_spec(code, 2), w.now).ok);
            RClient& ann = w.connect("Ann", code);
            RClient& bob = w.connect("Bob", code);
            ASSERT_TRUE(w.until([&] { return ann.session && bob.session; }, 5000));
            ants_test::HashHold odd(ann.end, &w.now, 1200);                                  // Ann's machine: its acknowledgements reach the room 1.2 s late, and it reports no state of its own
            ann.session->set_connection(&odd);
            w.run(kPre + 6000);
            ASSERT_TRUE(w.status(code).state == RoomState::Running);
            odd.hold_acks = true;                                                            // (while a seat is missing, the acknowledgements stand still, so that the newest turns are kept)
            bob.reconnects = false;                                                          // Bob's machine does not come back by itself (until the test says)
            w.cut(bob);
            ASSERT_TRUE(w.until([&] { return w.status(code).paused; }, 3000));
            uint32_t first = 0;
            if (away) first = odd.report_odd();
            w.run(1000);
            RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_state == 0);
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && !s.absent[0].catching_up);      // Bob's seat is still held
            bob.reconnects = true;
            ASSERT_TRUE(w.until([&] {
                const RoomStatus t = w.status(code);
                return !t.paused && t.absent.empty() && t.rejoins == 1;
            }, 20000));
            if (!away) first = odd.report_odd();
            odd.hold_acks = false;
            w.run(1500);
            s = w.status(code);
            ASSERT_TRUE(first != 0 && (first + 1) % net::kHashEveryTurns == 0);
            ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_state == 0);             // the room goes on, nobody is dropped
            ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
            ASSERT_TRUE(ann.end->is_open() && bob.end->is_open());
            w.run(3000);                                                                     // the table settles
            const uint32_t second = odd.report_odd();
            ASSERT_TRUE(second > first);
            w.run(2000);
            s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Finished && s.quitter == 0 && s.drops_by_state == 1);       // Ann quit a match of two: Bob wins
            ASSERT_TRUE(ann.session->desynced() && !bob.session->desynced() && !ann.end->is_open());
            ASSERT_TRUE(s.rows.size() == 2 && s.rows[0].first == 1 && s.rows[0].winner && s.rows[1].first == 0 && !s.rows[1].winner);
        }
    } TEST_END();
}

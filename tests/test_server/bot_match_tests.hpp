// The tests of the server's own matches of computer players (RoomSpec::bots_only, RoomManager::enable_bot_matches / start_bot_match; docs/SERVER.md "Matches of computer players"): a room whose seats are
// all bots starts with nobody in it, is not closed for lack of people and is kept like any other match; the specification that cannot be such a room is refused; the timer makes one match at a time, only
// when the server keeps replays, and chooses the same things from the same seed. Included by test_server.cpp, which holds the harness (TEST_CASE, ASSERT_*, World) and calls run_bot_match_tests().
#pragma once

namespace {

// A room of four computer players on TINY: the specification of the server's own match, made by hand
RoomSpec bots_only_spec(const std::string& code, uint8_t players = 2) {
    RoomSpec spec = spec_of(code, players);
    for (uint8_t seat = 0; seat < players; ++seat) {
        ai::BotSpec bot;
        bot.seat = seat;
        bot.level = seat % 2 == 0 ? ai::Level::Medium : ai::Level::Hard;
        spec.bots.push_back(bot);
    }
    spec.bots_only = true;
    spec.early_start = false;
    spec.reconnect = false;
    spec.wait_ms = 5u * 1000u;
    spec.run_ms = 10u * 60u * 1000u;
    return spec;
}

// What the timer chose, as the status shows it: the map, the seed and the seats with their level
std::string bot_match_choice(const RoomStatus& s) {
    std::string text = s.map + " players " + std::to_string(s.bots.size());
    for (const RoomStatus::Bot& b : s.bots) text += " " + std::to_string(static_cast<unsigned>(b.seat)) + ":" + b.level;
    return text;
}

// Runs the world in steps of ten milliseconds until the condition holds (false: it did not within `max_ms`)
template <typename Condition>
bool run_until(World& w, Condition cond, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms; t += 10) {
        if (cond()) return true;
        w.run(10);
    }
    return cond();
}

const RoomStatus* only_bot_room(const std::vector<RoomStatus>& rooms) {
    return rooms.size() == 1 ? &rooms[0] : nullptr;
}

}  // namespace

void run_bot_match_tests() {
    TEST_CASE("S3.179 A Room Of Computer Players Alone Is Refused Unless Every Seat Is A Bot, And Is No Lobby, No Public Room And Holds No Seats") {
        World w;
        RoomSpec fewer = bots_only_spec("BM-FEWER", 3);
        fewer.bots.pop_back();
        const CreateResult a = w.mgr.create_room(fewer, w.now);
        ASSERT_TRUE(!a.ok && a.http_status == 400 && a.error.find("bots_only") != std::string::npos);
        RoomSpec none = spec_of("BM-NONE", 2);
        none.bots_only = true;
        ASSERT_TRUE(!w.mgr.create_room(none, w.now).ok);
        RoomSpec holds = bots_only_spec("BM-HOLDS", 2);
        holds.reconnect = true;
        ASSERT_TRUE(!w.mgr.create_room(holds, w.now).ok);
        RoomSpec open = bots_only_spec("BM-OPEN", 2);
        open.public_room = true;
        ASSERT_TRUE(!w.mgr.create_room(open, w.now).ok);
        RoomSpec fog = bots_only_spec("BM-FOG", 2);
        fog.fog = true;
        ASSERT_TRUE(!w.mgr.create_room(fog, w.now).ok);
        RoomSpec twice = bots_only_spec("BM-TWICE", 2);
        twice.bots[1].seat = 0;
        ASSERT_TRUE(!w.mgr.create_room(twice, w.now).ok);
        ASSERT_TRUE(w.mgr.list(w.now).empty());
        ASSERT_TRUE(w.mgr.create_room(bots_only_spec("BM-OK", 2), w.now).ok);
        // a room with a person still needs one: the rule of the other rooms is unchanged
        RoomSpec all = spec_of("BM-ALL", 2);
        all.bots = {ai::BotSpec{0, "standard", ai::Level::Medium}, ai::BotSpec{1, "standard", ai::Level::Medium}};
        const CreateResult b = w.mgr.create_room(all, w.now);
        ASSERT_TRUE(!b.ok && b.error.find("at least one") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.180 A Room Of Computer Players Alone Starts With Nobody In It, Is Not Closed For Lack Of People, Plays At The Normal Speed Until The Time Limit And Is Kept Like Any Other Match") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-room", &clock), false, why));
        RoomSpec spec = bots_only_spec("BM-ROOM", 3);
        spec.run_ms = 45u * 1000u;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        ASSERT_TRUE(w.status("BM-ROOM").state == RoomState::Waiting);
        ASSERT_TRUE(run_until(w, [&] { return w.status("BM-ROOM").state == RoomState::Running; }, 12000));
        RoomStatus s = w.status("BM-ROOM");
        ASSERT_TRUE(s.joined == 3 && s.bot_controller && s.bots.size() == 3 && s.names[0] == "Bot (Medium)" && s.names[1] == "Bot (Hard)");
        w.run(20000);                                                        // (nobody has ever been in the room: "everybody left" does not end it)
        s = w.status("BM-ROOM");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_MSG(s.turns > 300 && s.turns < 500, "turns " + std::to_string(s.turns));      // (about 20 turns a second: the normal speed, not the headless arena's)
        ASSERT_TRUE(s.bot_decisions > 0);
        ASSERT_TRUE(run_until(w, [&] { return w.status("BM-ROOM").state != RoomState::Running; }, 40000));
        s = w.status("BM-ROOM");
        ASSERT_TRUE(s.state == RoomState::Finished || s.state == RoomState::Failed);
        ASSERT_TRUE(s.reason != "everybody left");
        ASSERT_TRUE(s.replay_kept && s.replay_note.empty() && ReplayStore::valid_file_name(s.replay_file));
        const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.complete && f.rep.total_turns >= 600 && f.rep.head.roster == 0x07);
        ASSERT_EQ(f.rep.head.names, (std::array<std::string, sim::MAX_PLAYERS>{{"Bot (Medium)", "Bot (Hard)", "Bot (Medium)", ""}}));
        ASSERT_FALSE(bytes_contain(f.bytes, "BM-ROOM"));
        replay::Outcome played;
        ASSERT_TRUE(plays_out(f.rep, played));
        ASSERT_TRUE(played.complete && played.turns == f.rep.total_turns && played.hashes_checked >= 5);
    } TEST_END();

    TEST_CASE("S3.181 The Timer Makes A Match Of Two To Four Computer Players After The First Wait And Then Every N Minutes, One At A Time, From Its Seed The Same Match; With No Replays It Makes None And Says Why") {
        ReplayClock clock;
        std::string why;
        std::string first;
        for (int round = 0; round < 2; ++round) {                            // (the same seed twice: the same choices)
            World w;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-timer", &clock), false, why));
            ASSERT_TRUE(w.mgr.take_notices().size() == 1);                   // (what the store found when it opened)
            w.mgr.enable_bot_matches(5, w.now, 60u * 1000u, 20261009u);
            ASSERT_TRUE(w.mgr.bot_matches_enabled());
            w.run(59000);
            ASSERT_TRUE(w.mgr.list(w.now).empty());                          // (nothing before the first wait)
            w.run(2000);
            const std::vector<RoomStatus> rooms = w.mgr.list(w.now);
            const RoomStatus* made = only_bot_room(rooms);
            ASSERT_TRUE(made != nullptr);
            ASSERT_TRUE(made->bots.size() >= 2 && made->bots.size() <= 4 && made->joined == made->bots.size() && !made->public_room && !made->lobby && !made->reconnect);
            for (const RoomStatus::Bot& b : made->bots) ASSERT_TRUE(b.seat < 4 && (b.level == "medium" || b.level == "hard") && b.kind == "standard" && !b.fill);
            ASSERT_TRUE(made->map == "TINY.LVL" || made->map == "SMALL.LVL" || made->map == "MEDIUM.LVL" || made->map == "GAUNTLET.LVL" || made->map == "TREASURE.LVL" || made->map == "ISLANDS.LVL");
            const std::string choice = bot_match_choice(*made);
            if (round == 0) first = choice;
            else ASSERT_EQ(choice, first);
            const std::vector<std::string> notes = w.mgr.take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot match: room " + made->code + " started");
            w.run(5u * 60u * 1000u);                                         // (the next time comes while the first match runs: nothing is made, the reason is told)
            ASSERT_TRUE(w.mgr.list(w.now).size() <= 1);
            const std::vector<std::string> later = w.mgr.take_notices();
            ASSERT_TRUE(later.size() == 1 && later[0] == "bot match: not started (the last one is still going)");
        }
        World bare;                                                          // no replays: a match that nobody could watch or look at again is not played
        bare.mgr.enable_bot_matches(5, bare.now, 1000u, 7u);
        bare.run(2000);
        ASSERT_TRUE(bare.mgr.list(bare.now).empty());
        const std::vector<std::string> notes = bare.mgr.take_notices();
        ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot match: not started (the server keeps no replays)");
        World off;                                                           // 0 minutes: off
        ASSERT_TRUE(off.mgr.enable_replays(replay_config("botmatch-off", &clock), false, why));
        (void)off.mgr.take_notices();
        off.mgr.enable_bot_matches(0, off.now, 1000u, 7u);
        ASSERT_FALSE(off.mgr.bot_matches_enabled());
        off.run(5000);
        ASSERT_TRUE(off.mgr.list(off.now).empty() && off.mgr.take_notices().empty());
    } TEST_END();
}

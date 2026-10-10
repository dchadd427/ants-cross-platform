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
        ASSERT_TRUE(s.bots_only && s.joined == 3 && s.bot_controller && s.bots.size() == 3 && s.names[0] == "Bot (Medium)" && s.names[1] == "Bot (Hard)");
        w.run(20000);                                                        // (nobody has ever been in the room: "everybody left" does not end it)
        s = w.status("BM-ROOM");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_MSG(s.turns > 250 && s.turns < 500, "turns " + std::to_string(s.turns));      // (about 20 turns a second: the normal speed, not the headless arena's)
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

    TEST_CASE("S3.182 A Match Of Computer Players Alone Is Not A Game That The Site's Statistics Count, Whatever Its Length; The Same Status Without bots_only Is") {
        StatsClock clock;
        SiteStats stats(clock.fn());
        RoomStatus bots = ended_room("BM-STATS", RoomState::Finished, 12000);
        bots.bots_only = true;
        stats.count_ended(bots);
        ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 0);
        bots.bots_only = false;
        stats.count_ended(bots);
        ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 1);
        World w;                                                             // (and the status of a real room of that kind says so)
        ASSERT_TRUE(w.mgr.create_room(bots_only_spec("BM-FLAG", 2), w.now).ok);
        ASSERT_TRUE(w.status("BM-FLAG").bots_only);
        ASSERT_TRUE(w.mgr.create_room(spec_of("BM-PLAIN", 2), w.now).ok && !w.status("BM-PLAIN").bots_only);
    } TEST_END();

    TEST_CASE("S3.183 The Timer's Match Is Free For All Or A Match Of Teams, Every Way To Seat Them Comes Up And Never Teams For Two; The Teams Are Drawn Last, So The First Match Of A Seed Chooses The Rest As Before") {
        ReplayClock clock;
        std::string why;
        std::set<std::string> seen;                                          // "<bots> <teams>" of every match that the seeds made
        std::string pinned;
        for (uint32_t seed = 1; seed <= 150; ++seed) {
            World w;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-teams", &clock), false, why));
            w.mgr.enable_bot_matches(5, w.now, 1000u, seed);
            w.run(2000);
            const std::vector<RoomStatus> rooms = w.mgr.list(w.now);
            const RoomStatus* made = only_bot_room(rooms);
            ASSERT_TRUE(made != nullptr);
            uint8_t roster = 0;
            for (const RoomStatus::Bot& b : made->bots) roster = static_cast<uint8_t>(roster | (1u << b.seat));
            std::vector<std::string> choices;                                // what a match of these seats may be: free for all ("") or one of the ways to make teams
            for (const sim::StartTeams& c : sim::roster_team_choices(roster)) choices.push_back(c.set ? sim::start_teams_text(c) : std::string());
            ASSERT_TRUE(std::find(choices.begin(), choices.end(), made->room_teams) != choices.end());
            if (made->bots.size() == 2) ASSERT_EQ(made->room_teams, std::string());
            seen.insert(std::to_string(made->bots.size()) + " " + (made->room_teams.empty() ? std::string("ffa") : made->room_teams));
            if (seed == 7u) pinned = bot_match_choice(*made) + " teams " + (made->room_teams.empty() ? std::string("ffa") : made->room_teams);
        }
        std::string all_seen;
        for (const std::string& one : seen) all_seen += one + "; ";
        // two bots: free for all only; three: free for all and every pair of seats that the draw gave (the third plays alone); four: free for all and each of the three splits
        ASSERT_EQ(all_seen, std::string("2 ffa; 3 0+1; 3 0+2; 3 0+3; 3 1+2; 3 1+3; 3 2+3; 3 ffa; 4 0+1; 4 0+2; 4 0+3; 4 ffa; "));
        ASSERT_EQ(pinned, std::string("GAUNTLET.LVL players 3 0:medium 1:hard 3:medium teams 0+3"));      // (seed 7: the map, seats and levels that the timer chose before it drew teams, and the teams drawn after them)
    } TEST_END();

    TEST_CASE("S3.184 A Room Of Computer Players With Teams Starts As A Match Of Those Teams (Every Engine Of The Match Has Them Before The First Turn), The Third Of Three Plays Alone, And The Replay Has Them In Its Head And Plays Out") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-teams-room", &clock), false, why));
        RoomSpec four = bots_only_spec("BM-FOUR", 4);
        four.teams = sim::StartTeams{true, 0, 2};
        four.run_ms = 45u * 1000u;
        ASSERT_TRUE(w.mgr.create_room(four, w.now).ok);
        ASSERT_TRUE(run_until(w, [&] { return w.status("BM-FOUR").state == RoomState::Running; }, 12000));
        RoomStatus s = w.status("BM-FOUR");
        ASSERT_EQ(s.teams, std::string("0+2"));
        ASSERT_EQ(s.room_teams, std::string("0+2"));
        ASSERT_TRUE(s.allies[0] == 2 && s.allies[2] == 0 && s.allies[1] == 3 && s.allies[3] == 1);      // (Green and Blue against Red and Black: the referee's engine is allied from the start)
        ASSERT_TRUE(run_until(w, [&] { return w.status("BM-FOUR").state != RoomState::Running; }, 60000));
        s = w.status("BM-FOUR");
        ASSERT_TRUE(s.replay_kept && ReplayStore::valid_file_name(s.replay_file));
        const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.head.roster == 0x0F);
        ASSERT_TRUE(f.rep.head.teams.set && f.rep.head.teams.a == 0 && f.rep.head.teams.b == 2);
        replay::Outcome played;
        ASSERT_TRUE(plays_out(f.rep, played));                               // (the file's own start: the same teams on a fresh engine, to every hash it holds)
        ASSERT_TRUE(played.complete && played.hashes_checked >= 5);

        RoomSpec three = bots_only_spec("BM-THREE", 3);
        three.teams = sim::StartTeams{true, 1, 2};
        World v;
        ASSERT_TRUE(v.mgr.enable_replays(replay_config("botmatch-teams-three", &clock), false, why));
        ASSERT_TRUE(v.mgr.create_room(three, v.now).ok);
        ASSERT_TRUE(run_until(v, [&] { return v.status("BM-THREE").state == RoomState::Running; }, 12000));
        s = v.status("BM-THREE");
        ASSERT_EQ(s.teams, std::string("1+2"));
        ASSERT_TRUE(s.allies[1] == 2 && s.allies[2] == 1 && s.allies[0] == 4);                          // (Green plays alone)

        for (const sim::StartTeams& teams : {sim::StartTeams{true, 2, 3}, sim::StartTeams{true, 0, 3}, sim::StartTeams{true, 0, 2}}) {     // seats that are not the first ones: Green, Blue and Black play, Red does not
            World gap;
            ASSERT_TRUE(gap.mgr.enable_replays(replay_config("botmatch-teams-gap", &clock), false, why));
            RoomSpec spec = bots_only_spec("BM-GAP", 3);
            spec.bots[0].seat = 0;
            spec.bots[1].seat = 2;
            spec.bots[2].seat = 3;
            spec.teams = teams;
            ASSERT_TRUE(gap.mgr.create_room(spec, gap.now).ok);
            ASSERT_TRUE(run_until(gap, [&] { return gap.status("BM-GAP").state == RoomState::Running; }, 12000));
            s = gap.status("BM-GAP");
            ASSERT_EQ(s.teams, sim::start_teams_text(teams));
            ASSERT_TRUE(s.allies[teams.a] == teams.b && s.allies[teams.b] == teams.a && s.allies[1] == 4);
            for (uint8_t seat : {uint8_t{0}, uint8_t{2}, uint8_t{3}}) {
                if (seat != teams.a && seat != teams.b) ASSERT_EQ(s.allies[seat], uint8_t{4});          // (the seat that is left plays alone)
            }
        }
    } TEST_END();

    TEST_CASE("S3.185 The Control Interface Starts A Match Of Computer Players Alone (\"bots_only\": a bot for every seat that plays, the players are those bots, the teams are \"ffa\" or a pair of seats that play) And Says So In The Room's Status; Every Wrong Body Is A 400 That Names The Key And Makes No Room; A Room With People Is As It Was") {
        World w;
        const auto call = [&](const std::string& body) {
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = body;
            return handle_control(w.mgr, rq, w.now);
        };
        const auto parse = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        const std::string four = R"("bots":[{"seat":0,"bot":"hard"},{"seat":1,"bot":"hard"},{"seat":2,"bot":"hard"},{"seat":3,"bot":"hard"}])";
        // Two against two, top against bottom on TINY (Green and Blue against Red and Black), all Hard: the room is made at once and says what it is
        ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"CTL-FOUR","bots_only":true,"teams":"0+2","seed":7,)" + four + "}");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = parse(r);
        ASSERT_TRUE(v.get("bots_only").as_bool_or(false) && v.get("teams").str() == "0+2" && v.get("expected").as_int_or(0) == 4);
        ASSERT_TRUE(!v.get("early_start").as_bool_or(true) && !v.get("reconnect").as_bool_or(true));
        ASSERT_EQ(v.get("players").items().size(), size_t{4});
        for (const ctl::JsonValue& p : v.get("players").items()) ASSERT_TRUE(p.get("bot").as_bool_or(false) && p.get("name").str() == "Bot (Hard)");
        RoomStatus s = w.status("CTL-FOUR");
        ASSERT_TRUE(s.bots_only && s.room_teams == "0+2");
        ASSERT_TRUE(run_until(w, [&] { return w.status("CTL-FOUR").state == RoomState::Running; }, 12000));      // (nobody is waited for)
        s = w.status("CTL-FOUR");
        ASSERT_EQ(s.teams, std::string("0+2"));
        ASSERT_TRUE(s.allies[0] == 2 && s.allies[2] == 0 && s.allies[1] == 3 && s.allies[3] == 1);
        ASSERT_EQ(s.bots.size(), size_t{4});
        for (const RoomStatus::Bot& b : s.bots) ASSERT_TRUE(b.level == "hard" && !b.fill);
        // The members of the list tell the same
        ctl::HttpRequest list;
        list.method = "GET";
        list.path = "/rooms";
        size_t listed_bots_only = 0;
        const ctl::JsonValue listed = parse(handle_control(w.mgr, list, w.now));
        for (const ctl::JsonValue& room : listed.get("rooms").items()) listed_bots_only += room.get("bots_only").as_bool_or(false) ? 1u : 0u;
        ASSERT_EQ(listed_bots_only, size_t{1});
        // No teams: free for all; three bots on seats that are not the first ones are three players, and the body need not count them
        r = call(R"({"map":"TINY.LVL","code":"CTL-FFA","bots_only":true,"bots":[{"seat":0,"bot":"hard"},{"seat":2,"bot":"medium"},{"seat":3,"bot":"hard"}]})");
        ASSERT_EQ(r.status, 201);
        v = parse(r);
        ASSERT_TRUE(v.get("teams").str() == "ffa" && v.get("expected").as_int_or(0) == 3);
        ASSERT_TRUE(run_until(w, [&] { return w.status("CTL-FFA").state == RoomState::Running; }, 12000));
        ASSERT_EQ(w.status("CTL-FFA").teams, std::string("ffa"));
        r = call(R"({"map":"TINY.LVL","code":"CTL-THREE","bots_only":true,"teams":"2+3","bots":[{"seat":0,"bot":"hard"},{"seat":2,"bot":"medium"},{"seat":3,"bot":"hard"}]})");      // (two of three: the third plays alone)
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("teams").str(), std::string("2+3"));
        r = call(R"({"map":"TINY.LVL","code":"CTL-FALSE","bots_only":false,"players":3,"bots":[{"seat":0,"bot":"hard"}]})");        // "false" is a room with people, as without the key: its status has neither key
        ASSERT_EQ(r.status, 201);
        v = parse(r);
        ASSERT_TRUE(!v.has("bots_only") && !v.has("teams"));
        r = call(R"({"map":"TINY.LVL","code":"CTL-NUMBERS","bots_only":true,"wait_seconds":90,"max_run_seconds":120,"seed":0,)" + four + "}");     // what the body names wins over the numbers of the server's own match
        ASSERT_EQ(r.status, 201);
        const size_t rooms_before = w.mgr.room_count();

        // Every wrong body: a 400 that names the key, and no room
        const std::pair<std::string, std::string> wrong[] = {
            {R"("bots_only":"yes",)" + four, "\"bots_only\" must be true or false"},
            {R"("bots_only":1,)" + four, "\"bots_only\" must be true or false"},
            {R"("bots_only":true)", "\"bots_only\" needs \"bots\""},
            {R"("bots_only":true,"bots":[])", "\"bots_only\" needs \"bots\""},
            {R"("bots_only":true,"bots":[{"seat":1,"bot":"hard"}])", "\"bots_only\" needs \"bots\""},
            {R"("bots_only":true,"bots":[{"seat":0,"bot":"hard"},{"seat":1,"bot":"hard"},{"seat":2,"bot":"hard"},{"seat":3,"bot":"hard"},{"seat":3,"bot":"hard"}])", "\"bots_only\" needs \"bots\""},
            {R"("bots_only":true,"early_start":true,)" + four, "\"early_start\""},
            {R"("bots_only":true,"reconnect":true,)" + four, "bots_only"},
            {R"("bots_only":true,"players":3,)" + four, "bots_only"},
            {R"("bots_only":true,"fog":true,)" + four, "Fog of War"},
            {R"("bots_only":true,"bots":[{"seat":0,"bot":"hard"},{"seat":0,"bot":"hard"}])", "seat 0 has a bot already"},
            {R"("bots_only":true,"teams":"0+0",)" + four, "\"teams\""},
            {R"("bots_only":true,"teams":"0+4",)" + four, "\"teams\""},
            {R"("bots_only":true,"teams":"01",)" + four, "\"teams\""},
            {R"("bots_only":true,"teams":"",)" + four, "\"teams\""},
            {R"("bots_only":true,"teams":3,)" + four, "\"teams\""},
            {R"("bots_only":true,"teams":null,)" + four, "\"teams\""},
            {R"("bots_only":true,"teams":"1+3","bots":[{"seat":0,"bot":"hard"},{"seat":1,"bot":"hard"},{"seat":2,"bot":"hard"}])", "\"teams\""},      // (Black does not play)
            {R"("bots_only":true,"teams":"0+1","bots":[{"seat":0,"bot":"hard"},{"seat":1,"bot":"hard"}])", "\"teams\""},                          // (two players as a team would be the whole match)
            {R"("teams":"0+1",)" + four, "\"teams\" is for a room of computer players alone"},
            {R"("teams":"0+1","players":3,"bots":[{"seat":2,"bot":"hard"}])", "\"teams\" is for a room of computer players alone"},
            {R"("bots_only":false,"teams":"ffa")", "\"teams\" is for a room of computer players alone"},
        };
        for (const auto& bad : wrong) {
            r = call(R"({"map":"TINY.LVL",)" + bad.first + "}");
            ASSERT_MSG(r.status == 400, bad.first);
            const std::string why = parse(r).get("error").str();
            ASSERT_MSG(why.find(bad.second) != std::string::npos, bad.first + " -> " + why);
        }
        ASSERT_EQ(w.mgr.room_count(), rooms_before);
        // A room with people is as it was: every seat a bot is still refused without the key
        r = call(R"({"map":"TINY.LVL","players":2,"bots":[{"seat":0,"bot":"hard"},{"seat":1,"bot":"hard"}]})");
        ASSERT_TRUE(r.status == 400 && parse(r).get("error").str().find("at least one") != std::string::npos);
    } TEST_END();
}

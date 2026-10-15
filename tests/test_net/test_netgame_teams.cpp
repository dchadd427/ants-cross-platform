// Tests of the game's NetGame: the start delay (protocol 12), teams and the bots' levels (protocol 13), moving between seats and the prediction of one's own orders
#include "netgame_fixtures.hpp"


// Protocol 12: the first turn of a match is sealed kMatchStartDelayMs after the match began (the "Get ready to play!" dialog of every machine), on a game of the local network too
void run_start_delay_tests() {
    TEST_CASE("N3.22 Protocol 12, The Start Of A Match On The Local Network: No Machine Runs A Tick For 5 s After The Match Began, Nobody Waits Or Is Told Anything Meanwhile (No Stall, No Lag, No Election, No Notice), Then Every Machine's First Tick Comes And The Match Is Identical; A Guest Of Protocol 11 Is Refused By The Host's Door") {
        ASSERT_EQ(kProtocolVersion, 21);                                  // (12 added the start delay: no message; 13 changed the StartRequest and the Start: N2.100, N3.34; 14 added the SeatMove; 15 the platform and the create block; 16 the lobby room; 17 the game mode)
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(21, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        const uint32_t begun = t.now;                                                // (within one step of the host's own Begin)
        std::array<uint32_t, 3> first_tick{0, 0, 0};
        for (uint32_t elapsed = 0; elapsed < 20000 && (first_tick[0] == 0 || first_tick[1] == 0 || first_tick[2] == 0); elapsed += 10) {
            t.run(10);
            for (size_t i = 0; i < 3; ++i) {
                if (first_tick[i] == 0 && t.machines[i]->ticks >= 1) first_tick[i] = t.now - begun;
            }
            for (size_t i = 0; i < 3; ++i) {
                Machine& m = *t.machines[i];
                if (first_tick[i] != 0) continue;
                ASSERT_TRUE(m.sim.current_tick() == 0 && m.net.turns_executed() == 0);   // nothing runs ...
                ASSERT_TRUE(m.net.stalled_ms() == 0 && !m.net.stalled() && m.net.laggard() == 255 && !m.net.lag_notice() && !m.net.catching_up() && !m.net.self_lag_behind_ms());   // ... and nobody waits, lags or is told
                ASSERT_TRUE(!m.net.electing() && m.net.match_notice().empty() && !m.net.desynced() && m.net.phase() == NetGame::Phase::Playing);
            }
        }
        for (size_t i = 0; i < 3; ++i) ASSERT_TRUE(first_tick[i] >= kMatchStartDelayMs && first_tick[i] <= kMatchStartDelayMs + 250);       // the host's runner starts with two turns in hand, a guest's adds the link
        t.run(5000);
        for (auto& m : t.machines) ASSERT_TRUE(m->net.stalled_ms() == 0 && !m->net.lag_notice() && m->ticks > 60);
        host.net.freeze();
        t.run(2000);
        ASSERT_TRUE(all_equal(t));
        // a guest of protocol 11 (a raw connection that says its Hello) is refused with the existing refusal, in a room that is open for a match of the same protocol
        Table room;
        ASSERT_TRUE(make_room(room, 1));
        Machine& open_host = *room.machines[0];
        auto raw = TcpConnection::connect("127.0.0.1", open_host.net.listen_port());
        ASSERT_TRUE(raw != nullptr);
        HelloMsg old;
        old.version = static_cast<uint16_t>(kProtocolVersion - 1);
        old.name = "Old";
        bool sent = false;
        bool refused = false;
        ASSERT_TRUE(room.run_until([&]() {
            std::vector<uint8_t> msg;
            RejectMsg rj;
            while (raw->poll(msg)) refused = refused || (decode(msg, rj) && rj.reason == RejectReason::VersionMismatch);
            if (raw->is_open() && !sent) sent = raw->send(encode(old));
            return refused;
        }, 5000));
        ASSERT_EQ(open_host.net.room().slots[2].state, SlotState::Empty);            // nobody sat down for it
    } TEST_END();

    TEST_CASE("N3.23 Protocol 12, The Host Leaves While The Dialogs Are Up (Before The First Turn Was Sealed): The Lowest Guest Takes Over, Seals The First Turn At Once (A Host That Was Never Seen Seal Anything Has No Schedule To Keep), And The Two Guests Play On Identical; The Match Is Not Lost") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        Machine& carol = *t.machines[2];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(22, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(1500);                                                                // still inside the 5 s: nothing was sealed
        ASSERT_TRUE(host.sim.current_tick() == 0 && bob.sim.current_tick() == 0 && carol.sim.current_tick() == 0);
        host.net.leave();
        ASSERT_TRUE(t.run_until([&]() { return bob.net.is_host() && carol.net.host_seat() == 1; }, 10000));
        ASSERT_TRUE(t.run_until([&]() { return bob.ticks >= 40 && carol.ticks >= 40; }, 10000));      // the new host seals from turn 0 on, at once: the match plays on
        ASSERT_FALSE(carol.saw(NetGame::Event::Type::HostLeft));
        ASSERT_TRUE(t.run_until([&]() { return bob.count(NetGame::Event::Type::PlayerLeft) == 1 && carol.count(NetGame::Event::Type::PlayerLeft) == 1; }, 5000));      // the old host's team drops out
        ASSERT_TRUE(bob.sim.is_player_dropped(0) && carol.sim.is_player_dropped(0));
        bob.net.freeze();
        t.run(3000);
        ASSERT_TRUE(bob.sim.state_hash() == carol.sim.state_hash());
        ASSERT_EQ(bob.sim.current_tick(), carol.sim.current_tick());
        ASSERT_FALSE(bob.net.desynced() || carol.net.desynced());
    } TEST_END();

    TEST_CASE("N3.24 Protocol 12, A Command That Reaches A LAN Host Before Its First Turn Is Sealed Is Discarded (The Product Path: NetGame::begin_match Sets The Start Delay): A Guest's Orders At 100 ms (Seventy At Once) And 2,500 ms Of The Dialog Are Applied On No Machine And Cost It Neither Its Seat Nor A Desync; Its Order After Every Machine Has Run Its First Tick Is Applied On All Of Them At The Same Tick") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(24, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        const uint32_t begun = t.now;
        const uint32_t ant = first_ant(bob, 1);
        int16_t goal_x = 0, goal_y = 0;
        ASSERT_TRUE(ant != 0 && open_goal_near_hill(bob.sim, 1, goal_x, goal_y));
        // Bob is a modified client (or a rig: a NetGame has no dialog of its own, the application's dialog is what keeps a person's client quiet): he orders in the dialog's seconds, seventy at once and one more later
        bool sent = true;
        t.run(2600, [&](uint32_t now) {
            const uint32_t at = now - begun;
            if (at != 100 && at != 2500) return;
            for (int i = 0; i < (at == 100 ? 70 : 1); ++i) sent = bob.net.submit(order(1, ant, static_cast<int16_t>(goal_x + i % 3), goal_y)).status == sim::CommandResult::Status::Applied && sent;
        });
        ASSERT_TRUE(sent);
        for (auto& m : t.machines) ASSERT_TRUE(m->sim.current_tick() == 0 && m->ticks == 0);          // still inside the 5 s: nothing runs (the orders are in the host's hands, or gone)
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t); }, kUntilRunning));
        for (auto& m : t.machines) {
            for (const auto& a : m->applied) ASSERT_TRUE(a.second.issuer != 1);                          // none of the seventy-one was applied, on any machine, in any turn
            ASSERT_FALSE(m->net.desynced());
        }
        ASSERT_FALSE(host.sim.is_player_dropped(1) || bob.sim.is_player_dropped(1));                     // a discarded command is no violation: the seat stays
        // the same order once every machine has run its first tick (the point from which a person can give one) is applied everywhere, at the same tick
        ASSERT_EQ(bob.net.submit(order(1, ant, goal_x, goal_y)).status, sim::CommandResult::Status::Applied);
        t.run(1500);
        std::array<std::vector<uint64_t>, 3> ticks;
        for (size_t i = 0; i < 3; ++i) {
            for (const auto& a : t.machines[i]->applied) {
                if (a.second.issuer == 1) ticks[i].push_back(a.first);
            }
        }
        ASSERT_TRUE(ticks[0].size() == 1 && ticks[1] == ticks[0] && ticks[2] == ticks[0]);
        ASSERT_EQ(bob.sim.get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        host.net.freeze();
        t.run(2000);
        ASSERT_TRUE(all_equal(t));
        for (auto& m : t.machines) ASSERT_FALSE(m->net.desynced());
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The prediction of one's own orders (prediction.hpp), in a match of real NetGames
// ---------------------------------------------------------------------------------------------------------------------------------

// The prediction is OFF by default (opt-in): the tests of it ask for it, as the application does when it is told `--prediction on`
void enable_prediction(Table& t) {
    for (auto& m : t.machines) m->net.set_prediction_enabled(true);
}

// Protocol 13: the bots of each seat's own level, and the teams of the host's START, on a game on the local network
void run_team_tests() {
    const auto allies_of = [](const sim::SimulationEngine& e) {
        std::string out;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) out += std::to_string(static_cast<unsigned>(e.alliance_of(seat)));
        return out;                                                                                   // by seat: the ally's seat, 4 for none
    };
    const auto notices_of = [](const std::vector<ChatLine>& lines) {
        std::vector<std::string> out;
        for (const ChatLine& l : lines) {
            if (l.notice()) out.push_back(l.text);
        }
        return out;
    };
    TEST_CASE("N3.34 Protocol 13, A Game On The Local Network: The Host's START Seats A Bot Of Each Seat's Own Level And Puts Its Teams Into The Start (Every Machine Makes Them Before Its First Tick: The Same State To The End); Teams That The Seats Cannot Make Start The Match Without Them And Tell The Host (Its Status Line) And Every Guest (A Notice) Why") {
        {   // the host alone: seats 2 and 3 get bots of their own levels, seat 1 stays empty; Green + Blue (0 + 2) are a team, the Hard bot plays alone
            Table t;
            Machine& host = t.add("Alice");
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            t.run(200);
            const FillPlan plan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard});
            host.net.set_fill_bots(plan);
            host.net.set_start_teams(sim::StartTeams{true, 0, 2});
            ASSERT_TRUE(host.net.fill_bots() == plan && host.net.start_teams() == sim::StartTeams({true, 0, 2}));
            std::vector<uint8_t> seats;
            ASSERT_EQ(host.net.fill_bots(host.net.fill_bots(), &seats), size_t{2});
            ASSERT_EQ(seats, (std::vector<uint8_t>{2, 3}));
            ASSERT_TRUE(host.net.room().slots[1].state == SlotState::Empty);
            ASSERT_TRUE(host.net.room().slots[2].state == SlotState::Bot && host.net.room().slots[2].name == "Bot (Easy)");
            ASSERT_TRUE(host.net.room().slots[3].state == SlotState::Bot && host.net.room().slots[3].name == "Bot (Hard)");
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(5, hash));
            ASSERT_TRUE(host.net.start_info().roster == 0x0D && host.net.start_info().team_a == 0 && host.net.start_info().team_b == 2);
            ASSERT_TRUE(t.run_until([&]() { return host.net.phase() == NetGame::Phase::Playing; }, 3000));
            ASSERT_EQ(allies_of(host.sim), std::string("2404"));                                        // seats 0 and 2 are a team; seat 3 plays alone; seat 1 does not play
            ASSERT_EQ(host.sim.current_tick(), uint64_t{0});                                           // (made before the first tick)
            ASSERT_TRUE(notices_of(host.net.pregame_chat()).empty());                                   // nothing was refused
        }
        {   // two guests and a Hard bot in the last seat: Red + Blue (1 + 2, the two guests) against Green + Black (the host and the bot): every machine stands in the same state to the end of the test
            Table t;
            ASSERT_TRUE(make_room(t, 2));
            Machine& host = *t.machines[0];
            host.net.set_fill_bots(FillLevel::Hard);                                                   // one level: every empty seat
            host.net.set_start_teams(sim::StartTeams{true, 1, 2});
            ASSERT_EQ(host.net.fill_bots(host.net.fill_bots()), size_t{1});
            ASSERT_TRUE(t.run_until([&]() { return t.machines[1]->net.room().slots[3].state == SlotState::Bot; }, 3000));
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(8, hash));
            ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
            for (auto& m : t.machines) {
                ASSERT_TRUE(m->net.start_info().team_a == 1 && m->net.start_info().team_b == 2);      // every machine was told
                ASSERT_EQ(allies_of(m->sim), std::string("3210"));
                ASSERT_EQ(m->sim.current_tick(), uint64_t{0});
                ASSERT_TRUE(notices_of(m->net.pregame_chat()).empty());
            }
            ASSERT_TRUE(t.run_until([&]() { return everybody_running(t); }, kUntilRunning));
            t.run(6000);
            ASSERT_TRUE(all_equal(t));                                                                  // (the lock-step hashes agree: a machine that had not made the teams would have been named)
            for (auto& m : t.machines) ASSERT_FALSE(m->net.desynced());
            ASSERT_EQ(allies_of(t.machines[2]->sim), std::string("3210"));                             // (nobody broke them)
        }
        {   // teams that the seats cannot make: two people play, and a team would be both of them: the match starts without, and everybody is told why
            Table t;
            ASSERT_TRUE(make_room(t, 1));
            Machine& host = *t.machines[0];
            Machine& bob = *t.machines[1];
            host.net.set_start_teams(sim::StartTeams{true, 0, 1});
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(4, hash));
            ASSERT_TRUE(host.net.start_info().team_a == net::kNoTeam && host.net.start_info().team_b == net::kNoTeam);
            const std::string text = std::string(net::kNoticeNoTeams) + "only two seats play: a team of them would end the match at once.";
            ASSERT_EQ(host.net.status_text(), text);                                                    // the host's own status line
            ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t) && !notices_of(bob.net.pregame_chat()).empty(); }, 5000));
            ASSERT_EQ(notices_of(bob.net.pregame_chat()), (std::vector<std::string>{text}));           // the guest was told, once
            ASSERT_TRUE(bob.net.start_info().team_a == net::kNoTeam);
            ASSERT_EQ(allies_of(host.sim), std::string("4444"));
            ASSERT_EQ(allies_of(bob.sim), std::string("4444"));
            ASSERT_TRUE(text.size() <= kMaxChatChars);
        }
        {   // a seat of the pair that does not play: the host and a guest play, the pair is 0 + 3
            Table t;
            ASSERT_TRUE(make_room(t, 2));
            Machine& host = *t.machines[0];
            host.net.set_start_teams(sim::StartTeams{true, 0, 3});
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(4, hash));
            const std::string text = std::string(net::kNoticeNoTeams) + "Black does not play in this match.";
            ASSERT_EQ(host.net.status_text(), text);                                                    // the host's own status line (until the match begins)
            ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t) && !notices_of(t.machines[1]->net.pregame_chat()).empty() && !notices_of(t.machines[2]->net.pregame_chat()).empty(); }, 5000));
            for (size_t g : {size_t{1}, size_t{2}}) ASSERT_EQ(notices_of(t.machines[g]->net.pregame_chat()), (std::vector<std::string>{text}));
            for (auto& m : t.machines) ASSERT_EQ(allies_of(m->sim), std::string("4444"));
        }
        {   // a start that goes wrong takes nothing with it: the host that tries again gets what it chose (a guest that cannot load the map cancels the first start)
            Table t;
            ASSERT_TRUE(make_room(t, 2));
            Machine& host = *t.machines[0];
            t.machines[2]->corrupt_map = true;
            host.net.set_fill_bots(FillLevel::Medium);
            host.net.set_start_teams(sim::StartTeams{true, 0, 1});
            std::vector<uint8_t> seats;
            ASSERT_EQ(host.net.fill_bots(host.net.fill_bots(), &seats), size_t{1});
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(9, hash));
            ASSERT_TRUE(t.run_until([&]() { return host.net.phase() == NetGame::Phase::Room && host.saw(NetGame::Event::Type::Cancelled); }, 5000));
            host.net.remove_bot(3);
            t.machines[2]->corrupt_map = false;
            t.run(300);
            ASSERT_EQ(host.net.fill_bots(host.net.fill_bots()), size_t{1});
            ASSERT_TRUE(host.net.start_match(10, hash));
            ASSERT_TRUE(host.net.start_info().team_a == 0 && host.net.start_info().team_b == 1 && host.net.start_info().roster == 0x0F);
            ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
            for (auto& m : t.machines) ASSERT_EQ(allies_of(m->sim), std::string("1032"));
        }
    } TEST_END();

    TEST_CASE("N3.35 Protocol 13, What The Leader And The Host Are Told: The Prompt Says What START Will Do (The One Level Of The Empty Seats As Before; Each Seat's Own Bot And The Teams In One Line With Shorter Ways To Say It), The Footer Of The 16:9 Screen Says It In Two Lines; Fog Of War Seats No Bots And The Teams Still Count; Nothing To Say Leaves The Original's Prompt") {
        const auto room_with = [](std::initializer_list<uint8_t> people, std::initializer_list<uint8_t> bots = {}) {
            RoomMsg r;
            for (const uint8_t seat : people) r.slots[seat] = {SlotState::Client, "P" + std::to_string(seat), 10};
            for (const uint8_t seat : bots) r.slots[seat] = {SlotState::Bot, "Bot (Easy)", 0};
            return r;
        };
        const FillPlan none;
        const sim::StartTeams ffa;
        const FillPlan every_medium = FillLevel::Medium;
        const FillPlan ehm(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Easy, FillLevel::Hard, FillLevel::Medium});
        const FillPlan eh(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard});
        const FillPlan medium_for_others(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Medium, FillLevel::Medium, FillLevel::Medium});
        // nothing to say: the original's prompt stands
        ASSERT_TRUE(NetGame::start_prompt_texts(none, ffa, room_with({0}), false).empty());
        ASSERT_TRUE(NetGame::start_footer(none, ffa, room_with({0}), false).empty());
        ASSERT_TRUE(NetGame::start_prompt_texts(FillPlan(std::array<FillLevel, 4>{FillLevel::Hard, FillLevel::None, FillLevel::None, FillLevel::None}), ffa, room_with({0, 1, 2, 3}), false).empty());     // (a level for the seat of a person asks for nothing now)
        // one level in every seat, as before: the sentence of protocol 11 and its footer
        ASSERT_EQ(NetGame::start_prompt_texts(every_medium, ffa, room_with({0}), false).front(), std::string("Press START: the empty seats get Medium bots."));
        ASSERT_EQ(NetGame::start_prompt_texts(medium_for_others, ffa, room_with({0}), false).front(), std::string("Press START: the empty seats get Medium bots."));     // (the host's own seat has no level: the same words, the empty seats are all that get one)
        ASSERT_EQ(NetGame::start_prompt(FillLevel::Medium, false), std::string("Press START: the empty seats get Medium bots."));
        {
            const NetGame::FooterTexts f = NetGame::start_footer(every_medium, ffa, room_with({0}), false);
            ASSERT_TRUE(f.line[0] == std::vector<std::string>{"Empty seats at START:"} && f.line[1] == std::vector<std::string>{"Medium bots"});
        }
        // a level for each seat: the seats that are empty, by their colours
        ASSERT_EQ(NetGame::start_prompt_texts(eh, ffa, room_with({0, 1}), false).front(), std::string("Press START: Blue gets an Easy bot, Black a Hard bot."));
        ASSERT_EQ(NetGame::start_prompt_texts(ehm, ffa, room_with({0}), false).front(), std::string("Press START: Red gets an Easy bot, Blue a Hard bot, Black a Medium bot."));
        ASSERT_EQ(NetGame::start_prompt_texts(ehm, ffa, room_with({0, 2}), false).front(), std::string("Press START: Red gets an Easy bot, Black a Medium bot."));          // (a person took Blue: its Hard bot is not seated)
        ASSERT_EQ(NetGame::start_prompt_texts(ehm, ffa, room_with({0, 1, 2}), false).front(), std::string("Press START: Black gets a Medium bot."));
        {
            const NetGame::FooterTexts f = NetGame::start_footer(eh, ffa, room_with({0, 1}), false);
            ASSERT_TRUE(f.line[0] == std::vector<std::string>{"Empty seats at START:"} && f.line[1] == std::vector<std::string>{"Blue Easy, Black Hard"});
        }
        // the teams, with the seats that would play (the bots' too)
        const std::vector<std::string> both = NetGame::start_prompt_texts(eh, sim::StartTeams{true, 0, 2}, room_with({0, 1}), false);
        ASSERT_EQ(both.front(), std::string("Press START: Blue gets an Easy bot, Black a Hard bot; teams Green + Blue against Red + Black."));
        ASSERT_TRUE(both.size() >= 4);
        for (size_t i = 1; i < both.size(); ++i) ASSERT_TRUE(both[i].size() < both[i - 1].size());      // (the longest first, each one shorter than the one before)
        ASSERT_EQ(both.back(), std::string("bots: Blue Easy, Black Hard; teams Green + Blue vs Red + Black."));
        ASSERT_EQ(NetGame::start_prompt_texts(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false).front(), std::string("Press START: teams Red + Blue against Green."));
        ASSERT_EQ(NetGame::start_prompt_texts(every_medium, sim::StartTeams{true, 0, 1}, room_with({0}), false).front(), std::string("Press START: the empty seats get Medium bots; teams Green + Red against Blue + Black."));
        ASSERT_EQ(NetGame::start_prompt_texts(none, sim::StartTeams{true, 0, 3}, room_with({0, 1, 2}), false).front(), std::string("Press START: teams Green + Black (not with these seats)."));      // (no seat 3: they cannot be made as it stands)
        {
            const NetGame::FooterTexts f = NetGame::start_footer(eh, sim::StartTeams{true, 0, 2}, room_with({0, 1}), false);
            ASSERT_EQ(f.line[0], (std::vector<std::string>{"Bots: Blue Easy, Black Hard", "Blue Easy, Black Hard"}));
            ASSERT_EQ(f.line[1].front(), std::string("Teams: Green + Blue against Red + Black"));
            const NetGame::FooterTexts g = NetGame::start_footer(every_medium, sim::StartTeams{true, 0, 1}, room_with({0}), false);
            ASSERT_EQ(g.line[0], (std::vector<std::string>{"Empty seats: Medium bots"}));
            ASSERT_EQ(g.line[1].front(), std::string("Teams: Green + Red against Blue + Black"));
            const NetGame::FooterTexts h = NetGame::start_footer(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false);
            ASSERT_TRUE(h.line[0] == std::vector<std::string>{"Teams at START:"} && h.line[1].front() == "Red + Blue against Green");
        }
        // Fog of War seats no bots and says so; the teams still count
        ASSERT_EQ(NetGame::start_prompt_texts(every_medium, ffa, room_with({0}), true).front(), std::string("Fog of War is on, so START seats no bots."));
        ASSERT_EQ(NetGame::start_prompt(FillLevel::Hard, true), std::string("Fog of War is on, so START seats no bots."));
        ASSERT_EQ(NetGame::start_prompt_texts(every_medium, sim::StartTeams{true, 0, 1}, room_with({0, 1, 2}), true).front(), std::string("Fog of War is on, so START seats no bots; teams Green + Red against Blue."));
        ASSERT_TRUE(NetGame::start_footer(every_medium, ffa, room_with({0}), true).empty());
        ASSERT_TRUE(NetGame::start_prompt_texts(none, ffa, room_with({0}), true).empty());                  // (fog and no bots asked for: nothing to say)
        {
            const NetGame::FooterTexts f = NetGame::start_footer(every_medium, sim::StartTeams{true, 0, 1}, room_with({0, 1, 2}), true);
            ASSERT_TRUE(f.line[0] == std::vector<std::string>{"Teams at START:"} && f.line[1].front() == "Green + Red against Blue");
        }
        // the status line of a machine follows: a leader / host with a plan shows it, a guest never
        {
            Table t;
            ASSERT_TRUE(make_room(t, 1));
            Machine& host = *t.machines[0];
            Machine& bob = *t.machines[1];
            host.net.set_fill_bots(FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard}));
            host.net.set_start_teams(sim::StartTeams{true, 0, 2});
            t.run(100);
            ASSERT_EQ(host.net.status_text(), std::string("Press START: Blue gets an Easy bot, Black a Hard bot; teams Green + Blue against Red + Black."));
            ASSERT_TRUE(!host.net.prompt_texts().empty() && host.net.prompt_texts().front() == host.net.status_text());
            bob.net.set_fill_bots(FillLevel::Hard);                                                      // a guest has no START
            bob.net.set_start_teams(sim::StartTeams{true, 0, 2});
            t.run(100);
            ASSERT_EQ(bob.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
            ASSERT_TRUE(bob.net.prompt_texts().empty());
            host.net.set_fill_bots(FillPlan());
            host.net.set_start_teams(sim::StartTeams{});
            t.run(100);
            ASSERT_EQ(host.net.status_text(), std::string(sim::strings::text(sim::strings::kPressStart)));      // nothing chosen: the original's prompt
            ASSERT_TRUE(host.net.prompt_texts().empty());
        }
    } TEST_END();

    TEST_CASE("N3.36 Protocol 13, The Room's Own Teams (Protocol 15: The Room Message Names Them, A Create Block Chose Them): The Host Of A LAN Room, And A Client That Has Been Told No Room Yet, Have None And The Machine's Own Choice Counts; The Leader's Prompt And Footer Say That The Teams Are The Room's (A Client Of A Room That Has Them: RJ1.26)") {
        // ---- no room teams: the machine's own choice ----
        {
            Table t;
            Machine& lan = t.add("Alice");
            ASSERT_TRUE(lan.net.host(0, "Alice", true));
            lan.net.set_start_teams(sim::StartTeams{true, 0, 3});
            ASSERT_FALSE(lan.net.room_teams().set);                                               // the host of a room on the local network has no code and no block
            ASSERT_TRUE(lan.net.effective_teams() == sim::StartTeams({true, 0, 3}));
            Machine& zed = t.add("Zed");
            ASSERT_TRUE(zed.net.join("127.0.0.1", 1, "Zed", 255, "k7m2xq"));                    // (no server answers: no Room message has come)
            zed.net.set_start_teams(sim::StartTeams{true, 1, 2});
            ASSERT_FALSE(zed.net.room_teams().set);
            ASSERT_TRUE(zed.net.effective_teams() == sim::StartTeams({true, 1, 2}));
        }
        // ---- the words: the teams are the room's ----
        const auto room_with = [](std::initializer_list<uint8_t> people, std::initializer_list<uint8_t> bots = {}) {
            RoomMsg r;
            for (const uint8_t seat : people) r.slots[seat] = {SlotState::Client, "P" + std::to_string(seat), 10};
            for (const uint8_t seat : bots) r.slots[seat] = {SlotState::Bot, "Bot (Easy)", 0};
            return r;
        };
        const FillPlan none;
        const FillPlan eh(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard});
        const FillPlan every_medium = FillLevel::Medium;
        ASSERT_EQ(NetGame::start_prompt_texts(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false, true).front(), std::string("Press START: the room's teams: Red + Blue against Green."));
        ASSERT_EQ(NetGame::start_prompt_texts(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false, false).front(), std::string("Press START: teams Red + Blue against Green."));       // (a START's own choice: the words that it had)
        ASSERT_EQ(NetGame::start_prompt_texts(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false).front(), std::string("Press START: teams Red + Blue against Green."));
        const std::vector<std::string> both = NetGame::start_prompt_texts(eh, sim::StartTeams{true, 0, 2}, room_with({0, 1}), false, true);
        ASSERT_EQ(both.front(), std::string("Press START: Blue gets an Easy bot, Black a Hard bot; the room's teams: Green + Blue against Red + Black."));
        ASSERT_TRUE(both.size() >= 4);
        for (size_t i = 1; i < both.size(); ++i) ASSERT_TRUE(both[i].size() < both[i - 1].size());      // (the longest first, each one shorter than the one before)
        ASSERT_EQ(both.back(), std::string("bots: Blue Easy, Black Hard; teams Green + Blue vs Red + Black."));        // (the shortest way keeps the teams: the label is narrow)
        ASSERT_EQ(NetGame::start_prompt_texts(every_medium, sim::StartTeams{true, 0, 1}, room_with({0}), false, true).front(),
                  std::string("Press START: the empty seats get Medium bots; the room's teams: Green + Red against Blue + Black."));
        ASSERT_EQ(NetGame::start_prompt_texts(none, sim::StartTeams{true, 0, 3}, room_with({0, 1, 2}), false, true).front(), std::string("Press START: the room's teams: Green + Black (not with these seats)."));
        ASSERT_EQ(NetGame::start_prompt_texts(every_medium, sim::StartTeams{true, 0, 1}, room_with({0, 1, 2}), true, true).front(),
                  std::string("Fog of War is on, so START seats no bots; the room's teams: Green + Red against Blue."));
        ASSERT_TRUE(NetGame::start_prompt_texts(none, sim::StartTeams{}, room_with({0}), false, true).empty());          // (no teams: nothing to say, whoever owns them)
        {
            const NetGame::FooterTexts f = NetGame::start_footer(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false, true);
            ASSERT_TRUE(f.line[0] == std::vector<std::string>{"Room teams:"} && f.line[1].front() == "Red + Blue against Green");
            const NetGame::FooterTexts own = NetGame::start_footer(none, sim::StartTeams{true, 1, 2}, room_with({0, 1, 2}), false, false);
            ASSERT_TRUE(own.line[0] == std::vector<std::string>{"Teams at START:"} && own.line[1] == f.line[1]);          // (the same teams, said as a choice of START)
            const NetGame::FooterTexts g = NetGame::start_footer(eh, sim::StartTeams{true, 0, 2}, room_with({0, 1}), false, true);
            ASSERT_EQ(g.line[0], (std::vector<std::string>{"Bots: Blue Easy, Black Hard", "Blue Easy, Black Hard"}));
            ASSERT_EQ(g.line[1], (std::vector<std::string>{"Room teams: Green + Blue against Red + Black", "Room teams: Green + Blue vs Red + Black", "Teams: Green + Blue against Red + Black",
                                                            "Green + Blue against Red + Black", "Teams: Green + Blue vs Red + Black", "Green + Blue vs Red + Black"}));
            const NetGame::FooterTexts h = NetGame::start_footer(every_medium, sim::StartTeams{true, 0, 1}, room_with({0}), false, true);
            ASSERT_EQ(h.line[0], (std::vector<std::string>{"Empty seats: Medium bots"}));
            ASSERT_EQ(h.line[1].front(), std::string("Room teams: Green + Red against Blue + Black"));
            const NetGame::FooterTexts k = NetGame::start_footer(every_medium, sim::StartTeams{}, room_with({0}), false, true);        // (no teams: the footer of protocol 11, whoever would own them)
            ASSERT_TRUE(k.line[0] == std::vector<std::string>{"Empty seats at START:"} && k.line[1] == std::vector<std::string>{"Medium bots"});
        }
    } TEST_END();
}

void run_seat_move_tests() {
    TEST_CASE("N3.38 Protocol 14 And 15, The Colour That A Tap Asks For (NetGame::seat_move_target): The Next Colour That Nobody Holds, Round Again After Black; When Every Colour Is Held, The Next Colour Of Another Guest (The Two Change Places); Never The Seat Of A Bot Or Of The Host, Nothing For A Seat That Holds No Person Or Has No Guest To Go To; Every Room Of Four Seats Of Four Kinds Agrees With The Rule Said Another Way") {
        const auto room_of = [](const std::string& kinds) {                         // "CC.B": a person, a person, nobody, a bot (H: the host's own seat)
            RoomMsg r;
            for (size_t seat = 0; seat < 4; ++seat) {
                const char k = kinds[seat];
                if (k == 'C') r.slots[seat] = {SlotState::Client, "P" + std::to_string(seat), 10};
                else if (k == 'B') r.slots[seat] = {SlotState::Bot, "Bot", 0};
                else if (k == 'H') r.slots[seat] = {SlotState::Host, "Host", 0};
            }
            return r;
        };
        const auto target = [&room_of](const std::string& kinds, uint8_t seat) { return NetGame::seat_move_target(room_of(kinds), seat); };
        // by name: the next free colour, in the order green, red, blue, black and round again
        ASSERT_EQ(target("C...", 0), 1);                                           // a lone leader moves to red
        ASSERT_EQ(target("CC..", 0), 2);                                           // red is taken: blue
        ASSERT_EQ(target("CC..", 1), 2);
        ASSERT_EQ(target("CCC.", 1), 3);                                           // blue is taken too: black
        ASSERT_EQ(target("CCC.", 2), 3);
        ASSERT_EQ(target("CCC.", 0), 3);                                           // from green: red and blue are taken, black is free
        ASSERT_EQ(target("CCC.", 3), 255);                                         // (nobody sits in black)
        ASSERT_EQ(target("..CC", 3), 0);                                           // after black comes green again
        ASSERT_EQ(target("..CC", 2), 0);                                           // from blue: black is taken, round to green
        ASSERT_EQ(target(".C.C", 1), 2);
        ASSERT_EQ(target(".C.C", 3), 0);
        ASSERT_EQ(target("C..C", 3), 1);                                           // (green is taken: the next free is red)
        // every colour held by people (a room that does not start by itself when it is full, protocol 15): the next colour of another guest, round again, so that a person goes round with every press and
        // the leader can have every arrangement of the people
        for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(target("CCCC", seat), (seat + 1) % 4);
        // a bot's seat is not taken from it, and a bot does not move: past it, the next guest
        ASSERT_EQ(target("CBCC", 0), 2);                                           // no free colour: the next guest's, past the bot
        ASSERT_EQ(target("CBCC", 2), 3);
        ASSERT_EQ(target("CBCC", 3), 0);
        ASSERT_EQ(target("CBCC", 1), 255);                                         // (a bot is no person)
        ASSERT_EQ(target("CBBC", 0), 3);
        ASSERT_EQ(target("CBBB", 0), 255);                                         // a lone guest among bots has nobody to change places with
        ASSERT_EQ(target("BBBC", 3), 255);
        ASSERT_EQ(target("CB.C", 0), 2);                                           // a free colour is found past the bot (and goes before a guest's: seat 3 is a guest's)
        ASSERT_EQ(target("CB.C", 3), 2);
        ASSERT_EQ(target("C.BC", 0), 1);
        ASSERT_EQ(target("HCCC", 3), 1);                                           // the host sits in green: its seat is nobody's to take, the next guest is red's
        ASSERT_EQ(target("HC.C", 1), 2);
        ASSERT_EQ(target("HCCC", 1), 2);
        ASSERT_EQ(target("HCCC", 0), 255);                                         // (the host's seat holds no guest)
        ASSERT_EQ(target("HBCB", 2), 255);                                         // nobody else to go to
        ASSERT_EQ(target("C.CB", 0), 1);
        ASSERT_EQ(target("C.CB", 2), 1);                                           // round the bot and the leader to the free red
        ASSERT_EQ(target("....", 0), 255);
        ASSERT_EQ(target("C...", 1), 255);                                         // nobody sits there
        ASSERT_EQ(target("C...", 4), 255);                                         // no such seat
        ASSERT_EQ(target("C...", 255), 255);
        // every room of four seats of four kinds, every seat: the rule said another way (the first free colour after the seat, else the first guest's colour after it, looked up in a list)
        size_t answered = 0;
        size_t swaps = 0;
        for (unsigned code = 0; code < 256; ++code) {
            std::string kinds;
            for (unsigned i = 0; i < 4; ++i) kinds += ".CBH"[(code >> (2 * i)) & 3u];
            const RoomMsg room = room_of(kinds);
            for (uint8_t seat = 0; seat < 4; ++seat) {
                const uint8_t got = NetGame::seat_move_target(room, seat);
                if (kinds[seat] != 'C') {
                    ASSERT_EQ(got, 255);                                           // only a person moves
                    continue;
                }
                uint8_t want = 255;
                for (unsigned step = 1; step < 4 && want == 255; ++step) {         // the first free colour after this one, round the circle
                    if (kinds[(seat + step) % 4u] == '.') want = static_cast<uint8_t>((seat + step) % 4u);
                }
                for (unsigned step = 1; step < 4 && want == 255; ++step) {         // ... else the first colour of another guest
                    if (kinds[(seat + step) % 4u] == 'C') want = static_cast<uint8_t>((seat + step) % 4u);
                }
                ASSERT_EQ(got, want);
                if (got != 255) {
                    ASSERT_TRUE(got < 4 && got != seat && (kinds[got] == '.' || kinds[got] == 'C'));     // (a free colour or a guest's: never a bot's or the host's)
                    if (kinds.find('.') != std::string::npos) ASSERT_EQ(kinds[got], '.');                  // (a free colour goes before a guest's: a move, not a swap, whenever one can be had)
                    else ++swaps;
                    ++answered;
                }
            }
        }
        ASSERT_TRUE(answered > 100 && swaps > 10);                                 // (the rule answers for many of the rooms, and for the rooms with no free colour: the test is no empty loop)
    } TEST_END();

    TEST_CASE("N3.39 Protocol 14, The Plan Of Bots Follows The Room Message By Message (NetGame::follow_moved_player, With A Server That Is A Script): A Message In Which A Person Left One Colour And Took Another Trades The Levels Of The Two Colours; A Person Who Only Comes Or Goes, Two Who Go And One Who Comes, One Who Goes And Two Who Come, A Bot Seated Or Nothing Changed Trade Nothing; Messages Read Together Are Compared One By One (A Person Who Goes And Another Who Comes Are No Move, A Move With A Newcomer After It Is One); A Guest's Plan Never Moves") {
        using L = FillLevel;
        const FillPlan base(std::array<L, 4>{L::None, L::Easy, L::Medium, L::Hard});            // a different level in each colour but the leader's: any trade shows
        const auto traded = [&base](size_t a, size_t b) {
            std::array<L, 4> level = base.level;
            std::swap(level[a], level[b]);
            return FillPlan(level);
        };
        // a room as the script says it: "C.CB" is a person, nobody, a person, a bot; this machine (a person) is seat 0
        const auto room_of = [](const std::string& kinds, uint8_t leader) {
            RoomMsg r;
            r.map_name = "TINY.LVL";
            r.you = 0;
            r.leader = leader;
            for (size_t seat = 0; seat < 4; ++seat) {
                if (kinds[seat] == 'C') r.slots[seat] = {SlotState::Client, "P" + std::to_string(seat), 10};
                else if (kinds[seat] == 'B') r.slots[seat] = {SlotState::Bot, "Bot (Easy)", 0};
            }
            return r;
        };
        for (const bool leads : {true, false}) {
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            sim::SimulationEngine sim;
            NetGame net(sim);
            net.set_discovery(0);
            ASSERT_TRUE(net.join("127.0.0.1", listener->port(), "P0", 255, "ROOM-1"));
            std::unique_ptr<TcpConnection> server;
            uint32_t now = 1000;
            const auto run_until = [&](const std::function<bool()>& cond) {
                for (int i = 0; i < 3000 && !cond(); ++i) {
                    now += 10;
                    net.update(now);
                    if (!server) server = listener->accept();
                    ants_test::short_pause();
                }
                return ants_test::real_time_tail(cond, [&]() {                      // (real time for a late kernel, the game clock standing still: see run_until above)
                    net.update(now);
                    if (!server) server = listener->accept();
                });
            };
            const auto shows = [&](const std::string& kinds) {                         // the room that this machine shows is the one that the script said
                for (size_t seat = 0; seat < 4; ++seat) {
                    const SlotState want = kinds[seat] == 'C' ? SlotState::Client : kinds[seat] == 'B' ? SlotState::Bot : SlotState::Empty;
                    if (net.room().slots[seat].state != want) return false;
                }
                return true;
            };
            // the first person of a room leads it: the seat of the last person in the room when this machine does not lead (a guest is led by somebody else)
            const auto leader_of = [&](const std::string& kinds) -> uint8_t {
                if (leads) return 0;
                for (size_t seat = 3; seat > 0; --seat) {
                    if (kinds[seat] == 'C') return static_cast<uint8_t>(seat);
                }
                return 0;
            };
            const auto say = [&](const std::string& kinds) { return server->send(encode(room_of(kinds, leader_of(kinds)))); };
            // joined: Hello heard, Welcome and the first room sent
            ASSERT_TRUE(run_until([&]() { return server != nullptr; }));
            bool hello_heard = false;
            ASSERT_TRUE(run_until([&]() {                                                                          // (a condition that reads must remember what it read)
                std::vector<uint8_t> m;
                if (server->poll(m)) hello_heard = peek_type(m) == MsgType::Hello;
                return hello_heard;
            }));
            ASSERT_TRUE(server->send(encode(WelcomeMsg{0, 4})));
            ASSERT_TRUE(say("CC.."));
            ASSERT_TRUE(run_until([&]() { return net.phase() == NetGame::Phase::Room && shows("CC.."); }));
            ASSERT_EQ(net.is_leader(), leads);
            // one case: the room that the machine shows, the script's messages (all sent before the machine looks), what the plan is after them
            struct Case {
                const char* from;
                std::vector<const char*> then;       // each is one Room message
                int a;                               // the two colours whose levels trade places (-1: none)
                int b;
            };
            const std::vector<Case> cases = {
                {"CC..", {"C.C."}, 1, 2},            // the person of Red takes Blue
                {"C.C.", {"C..C"}, 2, 3},            // ... and goes on to Black
                {"C..C", {"CC.C"}, -1, -1},          // a person only comes
                {"CC.C", {"C..C"}, -1, -1},          // a person only goes
                {"CCC.", {"C..C"}, -1, -1},          // two go and one comes
                {"CC..", {"C.CC"}, -1, -1},          // one goes and two come
                {"CC..", {"C.B."}, -1, -1},          // one goes, a bot is seated (it is nobody's move)
                {"C.C.", {"CBC."}, -1, -1},          // a bot is seated
                {"CC..", {"CC.."}, -1, -1},          // nothing changed (the same message again)
                {"CC..", {"C...", "C.C."}, -1, -1},  // one goes, and later another comes: two messages, no move
                {"CC..", {"C.C.", "C.CC"}, 1, 2},    // a move, and a newcomer in the next message: the move is not lost
                {"CC..", {"CCC.", "C.C."}, -1, -1},  // one comes, and later one goes: two messages, no move
                {"CC.C", {"C.CC"}, 1, 2},            // a move while another sits in Black
            };
            for (const Case& c : cases) {
                ASSERT_TRUE(say(c.from));
                ASSERT_TRUE(run_until([&]() { return shows(c.from); }));
                net.set_fill_bots(base);
                for (const char* kinds : c.then) ASSERT_TRUE(say(kinds));
                std::this_thread::sleep_for(std::chrono::milliseconds(3));                  // (the messages are in the socket together when the machine looks, as far as the kernel is quick)
                ASSERT_TRUE(run_until([&]() { return shows(c.then.back()); }));
                net.update(now);                                                            // (nothing is left unread)
                const FillPlan want = leads && c.a >= 0 ? traded(static_cast<size_t>(c.a), static_cast<size_t>(c.b)) : base;
                ASSERT_TRUE(net.fill_bots() == want);
                ASSERT_TRUE(net.phase() == NetGame::Phase::Room && net.my_seat() == 0);
            }
            {   // two guests that changed places (protocol 15): the names go with the guests, the colours that are free are the same colours, so nothing trades
                ASSERT_TRUE(say("CCC."));
                ASSERT_TRUE(run_until([&]() { return shows("CCC."); }));
                net.set_fill_bots(base);
                RoomMsg swapped = room_of("CCC.", leader_of("CCC."));
                std::swap(swapped.slots[1], swapped.slots[2]);
                ASSERT_TRUE(swapped.slots[1].name == "P2" && swapped.slots[2].name == "P1");
                ASSERT_TRUE(server->send(encode(swapped)));
                ASSERT_TRUE(run_until([&]() { return net.room().slots[1].name == "P2"; }));
                net.update(now);
                ASSERT_TRUE(net.fill_bots() == base);
                RoomMsg and_a_move = room_of("CC.C", leader_of("CC.C"));                          // ... and a swap is no move that hides one: the person of Blue goes to Black, and the plan trades
                ASSERT_TRUE(server->send(encode(and_a_move)));
                ASSERT_TRUE(run_until([&]() { return shows("CC.C"); }));
                net.update(now);
                ASSERT_TRUE(net.fill_bots() == (leads ? traded(2, 3) : base));
                ASSERT_TRUE(net.phase() == NetGame::Phase::Room && net.my_seat() == 0);
            }
            // a session that follows another one starts afresh: its first Room message is not compared with the last room of the session before it (the machine shows that room until
            // the new one speaks: "C.CC" left, "CC.C" is a room of other people, so nobody has moved)
            net.leave();
            ASSERT_TRUE(net.phase() == NetGame::Phase::Off);
            server.reset();
            ASSERT_TRUE(net.join("127.0.0.1", listener->port(), "P0", 255, "ROOM-2"));
            net.set_fill_bots(base);
            ASSERT_TRUE(run_until([&]() { return server != nullptr; }));
            hello_heard = false;
            ASSERT_TRUE(run_until([&]() {
                std::vector<uint8_t> m;
                if (server->poll(m)) hello_heard = peek_type(m) == MsgType::Hello;
                return hello_heard;
            }));
            ASSERT_TRUE(server->send(encode(WelcomeMsg{0, 4})));
            ASSERT_TRUE(net.room().slots[3].state == SlotState::Client);                    // (the old room is still what the machine shows)
            ASSERT_TRUE(say("CC.C"));
            ASSERT_TRUE(run_until([&]() { return net.phase() == NetGame::Phase::Room && shows("CC.C"); }));
            ASSERT_EQ(net.is_leader(), leads);
            ASSERT_TRUE(net.fill_bots() == base);
        }
    } TEST_END();

    TEST_CASE("N3.40 Protocol 15, The Leader's Press (NetGame::request_move_seat(from, to), With A Server That Is A Script): A Machine That Leads An Open Room Sends A SeatMove With The Guard Of The Seats That It Shows, To A Free Colour And To A Guest's Alike; Whatever The Rule Refuses Is Not Sent; The Next Press Waits Half A Second And For The Room's Answer (A Second At Most); The Tap On A Row Sends What seat_move_target Names; A Machine That Does Not Lead Sends Nothing") {
        // a room as the script says it: "CCB." is a person, a person, a bot, nobody; this machine (a person) is seat 0
        const auto room_of = [](const std::string& kinds, uint8_t leader) {
            RoomMsg r;
            r.map_name = "TINY.LVL";
            r.you = 0;
            r.leader = leader;
            for (size_t seat = 0; seat < 4; ++seat) {
                if (kinds[seat] == 'C') r.slots[seat] = {SlotState::Client, "P" + std::to_string(seat), 10};
                else if (kinds[seat] == 'B') r.slots[seat] = {SlotState::Bot, "Bot (Easy)", 0};
            }
            return r;
        };
        for (const bool leads : {true, false}) {
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            sim::SimulationEngine sim;
            NetGame net(sim);
            net.set_discovery(0);
            ASSERT_TRUE(net.join("127.0.0.1", listener->port(), "P0", 255, "ROOM-1"));
            std::unique_ptr<TcpConnection> server;
            uint32_t now = 1000;
            std::vector<SeatMoveMsg> sent;                                                                         // every SeatMove that the script's server has read, in order
            size_t garbage = 0;
            bool hello_heard = false;
            const auto read = [&]() {
                std::vector<uint8_t> m;
                while (server != nullptr && server->poll(m)) {
                    if (peek_type(m) == MsgType::Hello) hello_heard = true;
                    if (peek_type(m) != MsgType::SeatMove) continue;                                               // (a ping ...)
                    SeatMoveMsg move;
                    if (decode(m, move)) sent.push_back(move);
                    else ++garbage;
                }
            };
            const auto run_for = [&](uint32_t ms) {                                                                 // the machine's clock runs; the script's server listens
                for (const uint32_t end = now + ms; now < end;) {
                    now += 10;
                    net.update(now);
                    if (!server) server = listener->accept();
                    read();
                    ants_test::short_pause();
                }
            };
            const auto run_until = [&](const std::function<bool()>& cond) {
                for (int i = 0; i < 3000 && !cond(); ++i) run_for(10);
                return ants_test::real_time_tail(cond, [&]() {                      // (real time for a late kernel, the game clock standing still: see run_until of N3.39)
                    net.update(now);
                    if (!server) server = listener->accept();
                    read();
                });
            };
            const auto shows = [&](const std::string& kinds) {
                for (size_t seat = 0; seat < 4; ++seat) {
                    const SlotState want = kinds[seat] == 'C' ? SlotState::Client : kinds[seat] == 'B' ? SlotState::Bot : SlotState::Empty;
                    if (net.room().slots[seat].state != want) return false;
                }
                return true;
            };
            const auto leader_of = [&](const std::string& kinds) -> uint8_t {                                       // a guest that is led by somebody else sees the last person as the leader
                if (leads) return 0;
                for (size_t seat = 3; seat > 0; --seat) {
                    if (kinds[seat] == 'C') return static_cast<uint8_t>(seat);
                }
                return 0;
            };
            const auto say = [&](const std::string& kinds) {
                ASSERT_TRUE(server->send(encode(room_of(kinds, leader_of(kinds)))));
                ASSERT_TRUE(run_until([&]() { return shows(kinds); }));
            };
            // The windows of the press rule (half a second between two presses, a second for the room's answer) are counted in the machine's game time, and a step of game time lasts a fraction of a
            // millisecond of real time, so a late kernel could use the window up while a wait for a message goes on (the review's second round). A wait that a window assertion follows therefore lets
            // the real clock run and the game clock stand still: one pass over the machine and the script's server for each millisecond, up to two seconds.
            const auto pass_still = [&]() {
                net.update(now);
                if (!server) server = listener->accept();
                read();
            };
            const auto wait_sent = [&](size_t count) { return ants_test::real_time_tail([&]() { return sent.size() >= count; }, pass_still); };
            const auto say_still = [&](const std::string& kinds) {
                ASSERT_TRUE(server->send(encode(room_of(kinds, leader_of(kinds)))));
                ASSERT_TRUE(ants_test::real_time_tail([&]() { return shows(kinds); }, pass_still));
            };
            ASSERT_TRUE(run_until([&]() { return hello_heard; }));
            ASSERT_TRUE(server->send(encode(WelcomeMsg{0, 4})));
            say("CCC.");
            ASSERT_TRUE(run_until([&]() { return net.phase() == NetGame::Phase::Room; }));
            ASSERT_EQ(net.is_leader(), leads);
            const auto sent_so_far = [&]() { run_for(50); return sent.size(); };
            if (!leads) {                                                                                           // led by somebody else: no press of this machine goes out, whatever it asks
                ASSERT_FALSE(net.request_move_seat(1, 3));
                ASSERT_FALSE(net.request_move_seat(1, 2));
                ASSERT_FALSE(net.request_move_seat(1));
                ASSERT_EQ(sent_so_far(), size_t{0});
                continue;
            }
            // what the rule refuses is not sent: the same seat twice, a seat that no room has, a seat that holds no guest, a colour that a bot holds, a bot that moves
            say("CCB.");
            const uint32_t seated = seating_hash(room_of("CCB.", 0));
            ASSERT_TRUE(net.room().slots[2].state == SlotState::Bot && seating_hash(net.room()) == seated);
            ASSERT_FALSE(net.request_move_seat(1, 1));
            ASSERT_FALSE(net.request_move_seat(1, 4));
            ASSERT_FALSE(net.request_move_seat(4, 1));
            ASSERT_FALSE(net.request_move_seat(255, 255));
            ASSERT_FALSE(net.request_move_seat(3, 1));                                                              // nobody sits in black
            ASSERT_FALSE(net.request_move_seat(1, 2));                                                              // the bot's colour is not taken from it
            ASSERT_FALSE(net.request_move_seat(2, 1));                                                              // ... and a bot does not move
            ASSERT_FALSE(net.request_move_seat(2));                                                                 // (the tap on a bot's row asks nothing)
            ASSERT_FALSE(net.request_move_seat(3));                                                                 // (nor the tap on an empty colour)
            ASSERT_EQ(sent_so_far(), size_t{0});
            // a free colour: one SeatMove, with the guard of the seats that the machine shows
            ASSERT_TRUE(net.request_move_seat(1, 3));
            ASSERT_TRUE(wait_sent(1));
            ASSERT_EQ(sent.size(), size_t{1});
            ASSERT_TRUE(sent[0].from == 1 && sent[0].to == 3 && sent[0].guard == seated && sent[0].guard != 0u);
            // the next press waits: a double click is one press (half a second), and the answer of the room (a second at most: a request that the room cannot do is not answered at all)
            ASSERT_FALSE(net.request_move_seat(1, 3));
            run_for(300);
            ASSERT_FALSE(net.request_move_seat(0, 3));                                                              // (a leader that moves itself is no different)
            run_for(300);
            ASSERT_FALSE(net.request_move_seat(0, 3));                                                              // 600 ms, and the room has not answered
            ASSERT_EQ(sent_so_far(), size_t{1});
            run_for(500);                                                                                           // more than a second: the room is not going to answer, and the leader may press again
            ASSERT_TRUE(net.request_move_seat(1, 0));                                                               // a guest's colour: the leader's own, so the leader goes to red and the guest to green
            ASSERT_TRUE(wait_sent(2));
            ASSERT_TRUE(sent[1].from == 1 && sent[1].to == 0 && sent[1].guard == seated);                          // (the same seats as shown: the same guard)
            // the room answers with the seats as they are: the guard of the next press is the new room's, and it may go out when half a second has passed
            say_still("CC.C");
            const uint32_t after = seating_hash(room_of("CC.C", 0));
            ASSERT_TRUE(after != seated && seating_hash(net.room()) == after);
            ASSERT_FALSE(net.request_move_seat(1, 2));                                                              // (the room's answer came a moment after the press: still one press)
            run_for(600);
            ASSERT_TRUE(net.request_move_seat(1, 2));
            ASSERT_TRUE(run_until([&]() { return sent.size() == 3; }));
            ASSERT_TRUE(sent[2].from == 1 && sent[2].to == 2 && sent[2].guard == after);
            // the tap on a row: the next free colour, else the next guest's colour (a full room: protocol 15)
            say("CCC.");
            run_for(1100);
            const uint32_t three = seating_hash(room_of("CCC.", 0));
            ASSERT_TRUE(net.request_move_seat(2));                                                                  // blue goes to black
            ASSERT_TRUE(run_until([&]() { return sent.size() == 4; }));
            ASSERT_TRUE(sent[3].from == 2 && sent[3].to == 3 && sent[3].guard == three);
            say("CCCC");
            run_for(1100);
            const uint32_t four = seating_hash(room_of("CCCC", 0));
            ASSERT_TRUE(net.request_move_seat(2));                                                                  // no colour is free: blue changes places with black
            ASSERT_TRUE(run_until([&]() { return sent.size() == 5; }));
            ASSERT_TRUE(sent[4].from == 2 && sent[4].to == 3 && sent[4].guard == four);
            run_for(1100);
            ASSERT_TRUE(net.request_move_seat(3));                                                                  // ... and round again: black with green, the leader's own colour
            ASSERT_TRUE(run_until([&]() { return sent.size() == 6; }));
            ASSERT_TRUE(sent[5].from == 3 && sent[5].to == 0 && sent[5].guard == four);
            ASSERT_EQ(garbage, size_t{0});
            ASSERT_TRUE(net.phase() == NetGame::Phase::Room && net.is_leader());
        }
    } TEST_END();

    TEST_CASE("N3.41 Protocol 17, The Game Mode Of A Game On The Local Network: The Host Sets It In The Room (Every Guest Is Shown It; A Mode This Build Does Not Know And A Call After The Start Change Nothing), The Start Carries It To Every Machine, Each Makes Its Engine With It Before The First Init, And Three Machines Play It 20 Seconds Identical (No Food On The Map); A Room Left At The Original's Mode Plays The Original's Game") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        ASSERT_EQ(host.net.room().mode, 0);
        host.net.set_mode(static_cast<uint8_t>(sim::kLastGameMode + 1));            // (a mode that is not known is not taken)
        ASSERT_EQ(host.net.room().mode, 0);
        host.net.set_mode(static_cast<uint8_t>(sim::GameMode::Kills187));
        ASSERT_EQ(host.net.room().mode, 1);
        ASSERT_TRUE(t.run_until([&]() { return t.machines[1]->net.room().mode == 1 && t.machines[2]->net.room().mode == 1; }, 3000));
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(61, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t); }, kUntilRunning));
        host.net.set_mode(0);                                                       // (after the start: the room is closed to it)
        ASSERT_EQ(host.net.start_info().mode, 1);
        for (auto& m : t.machines) {
            ASSERT_EQ(m->net.start_info().mode, 1);
            ASSERT_TRUE(m->sim.game_mode() == sim::GameMode::Kills187);
        }
        std::array<uint32_t, sim::MAX_PLAYERS> ants{};
        for (const auto& a : host.sim.get_world_state().ants) {
            if (a.player_id < sim::MAX_PLAYERS) ++ants[a.player_id];
        }
        for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_TRUE(ants[seat] > 0);                  // (the map's own start: every seat of the roster has its ants)
        ASSERT_EQ(ants[3], 0u);
        for (const auto& cell : host.sim.get_world_state().cells) ASSERT_FALSE(cell.is_food);
        uint32_t next_order_ms = t.now + 500;
        t.run(20000, [&](uint32_t now) {
            if (now < next_order_ms) return;
            next_order_ms = now + 700;
            for (uint8_t seat = 0; seat < 3; ++seat) {
                Machine& m = *t.machines[seat];
                std::vector<uint32_t> mine;
                for (const auto& a : m.sim.get_world_state().ants) {
                    if (a.player_id == seat) mine.push_back(a.id);
                }
                if (mine.empty()) continue;
                const uint32_t pick = mine[(now / 700 + seat) % mine.size()];
                m.net.submit(order(seat, pick, static_cast<int16_t>((now / 10 + seat * 7) % 40), static_cast<int16_t>((now / 30 + seat * 11) % 40)));
            }
        });
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        for (auto& m : t.machines) ASSERT_FALSE(m->net.desynced());
        // the same room left at the original's mode starts with the food on the map
        Table plain;
        ASSERT_TRUE(make_room(plain, 2));
        Machine& plain_host = *plain.machines[0];
        ASSERT_EQ(plain_host.net.room().mode, 0);
        ASSERT_TRUE(plain_host.net.start_match(61, hash));
        ASSERT_TRUE(plain.run_until([&]() { return everybody_running(plain); }, kUntilRunning));
        for (auto& m : plain.machines) {
            ASSERT_EQ(m->net.start_info().mode, 0);
            ASSERT_TRUE(m->sim.game_mode() == sim::GameMode::HighestScore);
        }
        bool food = false;
        for (const auto& cell : plain_host.sim.get_world_state().cells) food = food || cell.is_food;
        ASSERT_TRUE(food);
    } TEST_END();
}

void run_prediction_tests() {
    TEST_CASE("N3.25 Prediction: Off By Default In Every Machine Of A Match (Nobody Asked For It), On In The Machines That Were Asked To (Their View Engine Stands Ahead Of The Confirmed One, The Third Machine's Is The Confirmed One); Orders From Everybody, And All Confirmed Engines End Identical") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        Machine& carol = *t.machines[2];
        host.net.set_map("SMALL.LVL");
        ASSERT_FALSE(host.net.prediction_enabled() || bob.net.prediction_enabled() || carol.net.prediction_enabled());      // off by default: opt-in
        host.net.set_prediction_enabled(true);                                                // the host and Bob are asked to; Carol keeps the default: its screen is its confirmed engine, as it always was
        bob.net.set_prediction_enabled(true);
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(99, hash));
        ASSERT_FALSE(host.net.predicting());                                                  // (nothing runs before the first turn: the "Get ready" dialog)
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t); }, kUntilRunning));
        t.run(2000);
        for (Machine* m : {&host, &bob}) {
            ASSERT_TRUE(m->net.predicting());
            ASSERT_TRUE(&m->net.view_engine() != &m->sim);
            ASSERT_TRUE(m->net.view_engine().current_tick() > m->sim.current_tick());
            ASSERT_EQ(m->net.view_engine().current_tick(), m->sim.current_tick() + m->net.prediction()->lead_ticks());
        }
        ASSERT_FALSE(carol.net.predicting());
        ASSERT_TRUE(&carol.net.view_engine() == &carol.sim);
        int orders[3] = {0, 0, 0};
        uint32_t next_order_ms = t.now + 300;
        t.run(25000, [&](uint32_t now) {
            if (now < next_order_ms) return;
            next_order_ms = now + 400;
            for (uint8_t seat = 0; seat < 3; ++seat) {
                Machine& m = *t.machines[seat];
                std::vector<uint32_t> mine;
                for (const auto& a : m.net.view_engine().get_world_state().ants) {              // (what the screen shows: the HUD picks its ants from the view)
                    if (a.player_id == seat) mine.push_back(a.id);
                }
                if (mine.empty()) continue;
                const uint32_t pick = mine[(now / 400 + seat) % mine.size()];
                const sim::CommandResult r = m.net.submit(order(seat, pick, static_cast<int16_t>((now / 10 + seat * 7) % 40), static_cast<int16_t>((now / 30 + seat * 11) % 40)));
                ASSERT_EQ(r.status == sim::CommandResult::Status::Applied || r.status == sim::CommandResult::Status::Ignored, true);
                ++orders[seat];
            }
        });
        ASSERT_TRUE(orders[0] > 40 && orders[1] > 40 && orders[2] > 40);
        // the machines that predict handled every order, lost none, and corrected what the others' orders changed; nothing took long
        for (Machine* m : {&host, &bob}) {
            const Prediction::Stats& st = m->net.prediction()->stats();
            ASSERT_TRUE(st.commands_predicted >= static_cast<uint64_t>(orders[m->net.my_seat()]) - 3);
            ASSERT_EQ(st.commands_lost, 0u);
            ASSERT_TRUE(st.rebuilds > 0);
            ASSERT_TRUE(st.rebuild_ns_max < 100ull * 1000ull * 1000ull);                      // (a rebuild of a 40 x 40 map: well under a frame; 100 ms is a loose bound for a busy machine)
        }
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));                                                            // the confirmed engines: identical, predicting or not
        for (auto& m : t.machines) ASSERT_FALSE(m->net.desynced());
        ASSERT_EQ(host.sim.current_tick(), carol.sim.current_tick());
    } TEST_END();

    TEST_CASE("N3.26 Prediction: An Order Given Through The HUD's Sink Is In The View Engine In The Same Call, Long Before The Confirmed Engine Has It; A Machine That Does Not Predict Waits For The Turn; The Switch Works At Run Time") {
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        enable_prediction(t);
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(7, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting(); }, kUntilRunning));
        t.run(3000);
        const uint32_t ant = first_ant(bob, 1);
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(bob.sim, 1, gx, gy));
        ASSERT_TRUE(bob.net.predicting());
        const sim::CommandResult r = bob.net.submit(order(1, ant, gx, gy));
        // in the same call: the view engine has the order, with the engine's own verdict; the confirmed engine does not
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(r.ack_ant, ant);
        ASSERT_EQ(r.needing_order, 1u);
        ASSERT_EQ(bob.net.view_engine().get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        ASSERT_TRUE(bob.net.view_engine().has_pending_path(ant) || bob.net.view_engine().get_unit(ant).final_dest.x == gx);
        ASSERT_TRUE(bob.sim.get_unit(ant).orig_order != sim::AntUnit::kOrderMove);
        const uint32_t given = t.now;
        ASSERT_TRUE(t.run_until([&]() { return bob.sim.get_unit(ant).orig_order == sim::AntUnit::kOrderMove; }, 3000));
        ASSERT_TRUE(t.now - given >= 50);                                                      // the confirmed engine had it at least a turn later (the way to the host and back, and the buffer)
        ASSERT_TRUE(t.now - given <= 1500);
        // the same order from a machine that does not predict: nothing in the view (which IS its confirmed engine) until the turn runs it
        bob.net.set_prediction_enabled(false);
        t.run(200);
        ASSERT_FALSE(bob.net.predicting());
        ASSERT_TRUE(&bob.net.view_engine() == &bob.sim);
        const uint32_t ant2 = [&]() {
            for (const auto& a : bob.sim.get_world_state().ants) {
                if (a.player_id == 1 && a.id != ant) return a.id;
            }
            return 0u;
        }();
        ASSERT_TRUE(ant2 != 0);
        const sim::CommandResult r2 = bob.net.submit(order(1, ant2, gx, gy));
        ASSERT_EQ(r2.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(r2.ack_ant, ant2);                                                           // (the click's feedback is the guess of predict_order_ack, as before)
        ASSERT_TRUE(bob.sim.get_unit(ant2).orig_order != sim::AntUnit::kOrderMove);
        ASSERT_TRUE(t.run_until([&]() { return bob.sim.get_unit(ant2).orig_order == sim::AntUnit::kOrderMove; }, 3000));
        // on again: it begins with the next tick
        bob.net.set_prediction_enabled(true);
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting(); }, 1000));
        ASSERT_TRUE(bob.net.view_engine().current_tick() > bob.sim.current_tick());
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        ASSERT_FALSE(bob.net.desynced() || host.net.desynced());
    } TEST_END();

    TEST_CASE("N3.27 Prediction: A Host Change Switches It Off While The Guests Elect (The Confirmed Engine Is Shown), It Begins Again Under The New Host, And Both Machines End Identical") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        enable_prediction(t);
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        Machine& carol = *t.machines[2];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(5, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting() && carol.net.predicting(); }, kUntilRunning));
        t.run(3000);
        host.net.leave();
        bool carol_predicted_while_electing = false;
        bool carol_electing_seen = false;
        ASSERT_TRUE(t.run_until([&]() {
            if (carol.net.electing()) {
                carol_electing_seen = true;
                carol_predicted_while_electing = carol_predicted_while_electing || carol.net.predicting();
            }
            return bob.net.is_host() && carol.net.host_seat() == 1;
        }, 8000));
        ASSERT_FALSE(carol_predicted_while_electing);                                          // not while the host is being chosen
        ASSERT_TRUE(carol_electing_seen || carol.net.host_seat() == 1);
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting() && carol.net.predicting(); }, 8000));
        ASSERT_TRUE(&bob.net.view_engine() != &bob.sim && &carol.net.view_engine() != &carol.sim);
        // orders go through the new host, are predicted, and arrive
        const uint32_t ant = first_ant(carol, 2);
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(carol.sim, 2, gx, gy));
        const sim::CommandResult r = carol.net.submit(order(2, ant, gx, gy));
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(carol.net.view_engine().get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        t.run(2000);
        ASSERT_EQ(bob.sim.get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        ASSERT_EQ(carol.sim.get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        ASSERT_EQ(carol.net.prediction()->stats().commands_lost, 0u);
        bob.net.freeze();
        t.run(3000);
        ASSERT_TRUE(bob.sim.state_hash() == carol.sim.state_hash());
        ASSERT_EQ(bob.sim.current_tick(), carol.sim.current_tick());
        ASSERT_FALSE(bob.net.desynced() || carol.net.desynced());
    } TEST_END();

    TEST_CASE("N3.28 Prediction: The Application's Switch And A Held Match (A Pause: The Sessions Hold The Runner) Turn It Off At Once, The Confirmed Engine Is Shown Meanwhile, And It Begins Again With The Next Tick After They Are Gone") {
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        enable_prediction(t);
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(8, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting(); }, kUntilRunning));
        t.run(1000);
        ASSERT_TRUE(bob.net.predicting() && &bob.net.view_engine() != &bob.sim);
        // the application (a hidden page's background steps, a screen over the match) switches it off
        bob.net.set_prediction_suspended(true);
        t.run(150);
        ASSERT_FALSE(bob.net.predicting());
        ASSERT_TRUE(&bob.net.view_engine() == &bob.sim);
        const uint64_t starts = bob.net.prediction()->stats().starts;
        t.run(500);
        ASSERT_EQ(bob.net.prediction()->stats().starts, starts);                              // (nothing began again meanwhile)
        bob.net.set_prediction_suspended(false);
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting(); }, 1000));
        ASSERT_EQ(bob.net.prediction()->stats().starts, starts + 1);
        t.run(300);
        ASSERT_TRUE(bob.net.view_engine().current_tick() > bob.sim.current_tick());
        // a pause: the session holds the runner (a seat that is away) and releases it when the match runs again
        ASSERT_TRUE(bob.net.runner() != nullptr);
        bob.net.runner()->set_held(true);
        t.run(150);
        ASSERT_FALSE(bob.net.predicting());
        ASSERT_TRUE(&bob.net.view_engine() == &bob.sim);
        const sim::CommandResult r = bob.net.submit(order(1, first_ant(bob, 1), 10, 10));         // an order given in a pause is not predicted (the server discards it)
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);                                // (the click's feedback is the guess of predict_order_ack, as before)
        ASSERT_EQ(bob.net.prediction()->pending_orders(), 0u);
        bob.net.runner()->set_held(false);
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting(); }, 1000));
        ASSERT_EQ(bob.net.prediction()->stats().starts, starts + 2);
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        ASSERT_FALSE(bob.net.desynced() || host.net.desynced());
    } TEST_END();

    TEST_CASE("N3.29 Prediction: A Machine Whose Match Has Gone Out Of Sync Shows The Confirmed Engine (The Hash Exchange Finds A State That Was Changed Behind The Turns' Back)") {
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        enable_prediction(t);
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(9, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting() && host.net.predicting(); }, kUntilRunning));
        t.run(1000);
        ASSERT_FALSE(bob.net.desynced() || host.net.desynced());
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(bob.sim, 1, gx, gy));
        bob.sim.apply_command(order(1, first_ant(bob, 1), gx, gy));                              // bob's confirmed engine alone: the machines now differ
        ASSERT_TRUE(t.run_until([&]() { return bob.net.desynced() || host.net.desynced(); }, 20000));
        t.run(300);
        for (Machine* m : {&host, &bob}) {
            if (!m->net.desynced()) continue;
            ASSERT_FALSE(m->net.predicting());                                                    // a machine that knows it is wrong shows what it has executed
            ASSERT_TRUE(&m->net.view_engine() == &m->sim);
        }
    } TEST_END();

    TEST_CASE("N3.30 Prediction: A Machine Whose Prediction Costs More Than Its Budget Loses The Prediction For A While And Nothing Else (A Cool-Down: The Confirmed Engine Is Shown, Orders Go As They Did Before, The Match Runs On And Ends Identical); The Other Machine Goes On Predicting, And When The Cool-Down Is Over It Begins Again") {
        constexpr uint64_t kMs = 1000ull * 1000ull;
        ManualClock clock;                                                                       // (declared before the table: the machine's clocks and hook hold it) the clocks of Bob's prediction
        uint64_t burn = 30 * kMs;                                                                // what every timed block costs
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        enable_prediction(t);
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        bob.net.set_prediction_budget(50 * kMs, 3, 40);                                          // three strikes of more than 50 ms of CPU, a cool-down of 40 ticks (2 s)
        bob.net.set_prediction_clocks(clock.wall_clock(), clock.cpu_clock());                    // (a spin on the real clocks cannot cost a block what it says on every runner)
        bob.net.set_prediction_work_hook([&burn, &clock]() { clock.work(burn); });
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(11, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting(); }, kUntilRunning));
        t.run(500);
        ASSERT_TRUE(bob.net.predicting());                                                       // 30 ms a block is under the budget of 50: no strike (the default budget, 12 ms, would have had four)
        ASSERT_EQ(bob.net.prediction()->stats().over_budget, 0u);
        burn = 60 * kMs;                                                                         // (a machine that is too slow for it)
        ASSERT_TRUE(t.run_until([&]() { return bob.net.prediction_cooling_down(); }, 2000));
        burn = 0;                                                                                // (the load is gone: from here on its blocks cost nothing)
        ASSERT_EQ(bob.net.prediction()->stats().over_budget, 3u);                                // three strikes, as many as the machine was given
        ASSERT_FALSE(bob.net.predicting());
        ASSERT_TRUE(&bob.net.view_engine() == &bob.sim);
        ASSERT_FALSE(host.net.prediction_cooling_down());
        ASSERT_TRUE(host.net.predicting() && &host.net.view_engine() != &host.sim);
        // an order of the machine that is cooling down goes the old way (the guess of predict_order_ack, the turn decides)
        const uint32_t ant = first_ant(bob, 1);
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(bob.sim, 1, gx, gy));
        const sim::CommandResult r = bob.net.submit(order(1, ant, gx, gy));
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(r.ack_ant, ant);
        ASSERT_TRUE(bob.sim.get_unit(ant).orig_order != sim::AntUnit::kOrderMove);
        ASSERT_TRUE(t.run_until([&]() { return bob.sim.get_unit(ant).orig_order == sim::AntUnit::kOrderMove; }, 3000));
        ASSERT_EQ(bob.net.prediction()->stats().cooldowns, 1u);
        // the cool-down is over after its ticks: it begins again, from the confirmed engine, and orders are predicted again
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting(); }, 6000));
        ASSERT_FALSE(bob.net.prediction_cooling_down());
        ASSERT_EQ(bob.net.prediction()->stats().starts, 2u);
        ASSERT_EQ(bob.net.prediction()->stats().cooldowns, 1u);
        ASSERT_TRUE(bob.net.view_engine().current_tick() > bob.sim.current_tick());
        t.run(500);
        ASSERT_TRUE(bob.net.predicting());                                                       // (and it stays)
        const std::vector<uint32_t> bobs = [&]() {
            std::vector<uint32_t> ids;
            for (const auto& a : bob.sim.get_world_state().ants) {
                if (a.player_id == 1 && a.id != ant) ids.push_back(a.id);
            }
            return ids;
        }();
        ASSERT_TRUE(!bobs.empty());
        const sim::CommandResult again = bob.net.submit(order(1, bobs[0], gx, gy));
        ASSERT_EQ(again.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(bob.net.view_engine().get_unit(bobs[0]).orig_order, sim::AntUnit::kOrderMove);   // (in the shown engine in the same call: it predicts)
        ASSERT_TRUE(bob.sim.get_unit(bobs[0]).orig_order != sim::AntUnit::kOrderMove);
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        ASSERT_FALSE(bob.net.desynced() || host.net.desynced());
    } TEST_END();

    TEST_CASE("N3.31 Prediction: The Lead Begins At The Lag That The Jitter Buffer And The Round Trip Promise (One Tick Less Than The Delay Counts), Then Learns The Lag Of The Orders That Really Came Back: Twelve Orders Of A Guest, The Lead Is The Lag Itself, An Order Is Put Where The Host Runs It (Hardly One Is Corrected), Nothing Is Lost Or Chased, And The Machines End Identical") {
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        enable_prediction(t);
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(30, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting(); }, kUntilRunning));
        t.run(1000);
        Prediction& p = *bob.net.prediction();
        // before the first order: the promise. The jitter buffer holds one turn and the round trip of a loopback is a few ms: the delay that an order is going to have is 25 + 50 + the ping,
        // 50 ms of which are the tick that the delay counts and the lag does not: the lag is one tick, and so is the lead
        ASSERT_TRUE(bob.net.runner() != nullptr && bob.net.runner()->buffer_turns() == 1u);
        ASSERT_TRUE(bob.net.ping_ms().value_or(0u) < 30u);
        ASSERT_EQ(p.learned_lag_ticks(), 0u);
        ASSERT_EQ(p.lead_ticks(), 1u);
        // twelve orders, 400 ms apart, to the ants of Bob's in turn
        std::vector<uint32_t> ants;
        for (const auto& a : bob.sim.get_world_state().ants) {
            if (a.player_id == 1) ants.push_back(a.id);
        }
        ASSERT_TRUE(ants.size() >= 2);
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(bob.sim, 1, gx, gy));
        for (int i = 0; i < 12; ++i) {
            ASSERT_EQ(bob.net.submit(order(1, ants[static_cast<size_t>(i) % ants.size()], static_cast<int16_t>(gx + i % 3), static_cast<int16_t>(gy + i % 2))).status, sim::CommandResult::Status::Applied);
            t.run(400);
            if (i == 0) ASSERT_TRUE(p.learned_lag_ticks() >= 1u);                                // the first order came back and taught its lag
        }
        t.run(2500);                                                                            // (a lead that has to fall does so after 40 ticks)
        const uint32_t lag = p.learned_lag_ticks();
        ASSERT_TRUE(lag >= 1u && lag <= 3u);                                                     // a loopback: a turn or two
        ASSERT_EQ(p.lead_ticks(), lag);                                                          // the lag itself: no bias
        const Prediction::Stats& st = p.stats();
        ASSERT_EQ(st.commands_lost, 0u);
        ASSERT_TRUE(st.commands_predicted >= 12u);
        ASSERT_TRUE(st.rebuilds <= 3u);                                                          // an order lands where the host runs it (a lead of one bias would make one rebuild an order: twelve)
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        ASSERT_FALSE(bob.net.desynced() || host.net.desynced());
    } TEST_END();

    TEST_CASE("N3.32 Prediction: A Match With The Defaults Has No Predicted Engine (Nothing Starts, The View Engine Is The Confirmed One, An Order Is The Old Guess And Waits For Its Turn, The Machines End Identical); Asked For At Run Time It Begins With The Next Tick") {
        int told = 0;                                                                          // (declared before the table: the callbacks hold it)
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        for (Machine* m : {&host, &bob}) m->net.set_on_prediction_dropped([&told]() { ++told; });
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(32, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t); }, kUntilRunning));
        t.run(3000);
        for (Machine* m : {&host, &bob}) {
            ASSERT_FALSE(m->net.prediction_enabled());                                         // the default
            ASSERT_FALSE(m->net.predicting());
            ASSERT_TRUE(&m->net.view_engine() == &m->sim);                                     // the screen reads the confirmed engine
            ASSERT_TRUE(m->net.prediction() == nullptr);                                        // there is none at all (not a suspended one): a match that does not ask for it pays nothing
        }
        ASSERT_EQ(told, 0);                                                                    // (none was ever made, so none was dropped: not one that lived until the next update)
        const uint32_t ant = first_ant(bob, 1);
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(bob.sim, 1, gx, gy));
        const sim::CommandResult r = bob.net.submit(order(1, ant, gx, gy));
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(r.ack_ant, ant);                                                             // (the guess of predict_order_ack: the click's feedback is what it always was)
        ASSERT_TRUE(bob.sim.get_unit(ant).orig_order != sim::AntUnit::kOrderMove);              // the order waits for its turn
        ASSERT_TRUE(t.run_until([&]() { return bob.sim.get_unit(ant).orig_order == sim::AntUnit::kOrderMove; }, 3000));
        ASSERT_FALSE(bob.net.predicting());
        // asked for at run time, it begins with the next tick (on the machine that was asked, not on the other)
        bob.net.set_prediction_enabled(true);
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting(); }, 1000));
        ASSERT_TRUE(bob.net.view_engine().current_tick() > bob.sim.current_tick());
        ASSERT_FALSE(host.net.predicting());
        ASSERT_TRUE(bob.net.prediction() != nullptr && host.net.prediction() == nullptr);      // (made on the machine that was asked, not on the other)
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        ASSERT_FALSE(bob.net.desynced() || host.net.desynced());
    } TEST_END();

    TEST_CASE("N3.33 Prediction: It Is Made Only Where It Is Wanted And Destroyed When It Is Switched Off (A Machine That Was Not Asked Has None; Switched Off At Run Time There Is None Left, Not A Suspended One; Switched On Again A New One Begins), And Whoever Points At Its Engine Is Told At Once Each Time (When It Is Switched Off And When The Machine Leaves The Match)") {
        int dropped = 0;                                                                         // (declared before the table: the callback holds it)
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        host.net.set_map("SMALL.LVL");
        bob.net.set_prediction_enabled(true);                                                    // Bob is asked to, the host is not
        bob.net.set_on_prediction_dropped([&dropped]() { ++dropped; });
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(33, hash));
        ASSERT_TRUE(bob.net.prediction() == nullptr);                                            // (nothing is made before the match plays)
        ASSERT_TRUE(t.run_until([&]() { return everybody_running(t) && bob.net.predicting(); }, kUntilRunning));
        ASSERT_TRUE(bob.net.prediction() != nullptr && host.net.prediction() == nullptr);
        ASSERT_EQ(dropped, 0);
        t.run(500);
        // switched off: it is destroyed with the next update, and the owner is told then
        bob.net.set_prediction_enabled(false);
        ASSERT_EQ(dropped, 0);
        t.run(30);
        ASSERT_TRUE(bob.net.prediction() == nullptr);
        ASSERT_EQ(dropped, 1);
        ASSERT_FALSE(bob.net.predicting());
        ASSERT_TRUE(&bob.net.view_engine() == &bob.sim);
        t.run(500);
        ASSERT_EQ(dropped, 1);                                                                   // (told once)
        // switched on again: a new prediction begins, the old one is not resumed
        bob.net.set_prediction_enabled(true);
        ASSERT_TRUE(t.run_until([&]() { return bob.net.predicting(); }, 2000));
        ASSERT_EQ(bob.net.prediction()->stats().starts, 1u);
        ASSERT_EQ(dropped, 1);
        // leaving the match destroys it too
        host.net.freeze();
        t.run(500);
        bob.net.leave();
        ASSERT_TRUE(bob.net.prediction() == nullptr);
        ASSERT_EQ(dropped, 2);
    } TEST_END();
}

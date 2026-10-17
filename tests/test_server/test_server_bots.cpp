// Tests of the dedicated game server: bots fill the seats, bots in the specification, chat in the waiting room (protocol 11)
#include "server_fixtures.hpp"


// ---------------------------------------------------------------------------------------------------------------------------------
// Bots fill the empty seats at the leader's START, bots in the room's specification, and chat in the waiting room (protocol 11, docs/NETWORK_PORT.md "Protocol 11")
// ---------------------------------------------------------------------------------------------------------------------------------


void run_bot_tests() {
    TEST_CASE("S3.60 The Leader's START With A Fill: One Person In A Room For Four Starts It With Bots In Exactly The Three Empty Seats, Named \"Bot (Medium)\" (The Person's Own Seat Is Left Alone), The Match Runs To Its End With The Bots Playing (Their Scores In The Rows), The Referee And The Client Stand At The Same State At The Same Tick, The Status Lists The Bots") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("FILL-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "FILL-1", 2);                                     // the person sits in seat 2: the bots take 0, 1 and 3
        ann.record_hashes = true;
        w.run(500);
        RoomStatus s = w.status("FILL-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 1 && s.bots.empty() && ann.lobby->is_leader());
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
        w.run(2000);
        s = w.status("FILL-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_EQ(s.joined, 4);                                                           // a bot is a player
        ASSERT_EQ(s.expected, 4);
        ASSERT_TRUE(s.names[0] == bn("FILL-1", 0, "Medium") && s.names[1] == bn("FILL-1", 1, "Medium") && s.names[2] == "Ann" && s.names[3] == bn("FILL-1", 3, "Medium"));
        ASSERT_EQ(s.bots.size(), size_t{3});
        for (size_t i = 0; i < 3; ++i) {
            const uint8_t seat = i < 2 ? static_cast<uint8_t>(i) : uint8_t{3};
            ASSERT_TRUE(s.bots[i].seat == seat && s.bots[i].kind == "standard" && s.bots[i].level == "medium" && s.bots[i].name == bn("FILL-1", seat, "Medium") && s.bots[i].fill);
        }
        ASSERT_TRUE(ann.sim.roster_mask() == 0x0F);                                       // everybody plays: the roster of the Start has the bots' seats
        {
            const net::StartMsg& start = ann.lobby->start_info();                            // what every machine was told: the names of the seats, the bots' among them
            ASSERT_TRUE(start.names[0] == bn("FILL-1", 0, "Medium") && start.names[1] == bn("FILL-1", 1, "Medium") && start.names[2] == "Ann" && start.names[3] == bn("FILL-1", 3, "Medium") && start.roster == 0x0F);
        }
        for (int guard = 0; guard < 4000 && w.status("FILL-1").state == RoomState::Running; ++guard) w.run(250);
        s = w.status("FILL-1");
        ASSERT_TRUE(s.state == RoomState::Finished);                                      // (a client that diverged from the referee would have failed the room)
        ASSERT_FALSE(ann.session->desynced());
        w.run(Room::kGraceMs + 500);
        ASSERT_EQ(s.rows.size(), size_t{4});
        for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{3}}) {
            const RoomRow* row = row_of(s, seat);
            ASSERT_TRUE(row != nullptr && row->name == bn("FILL-1", seat, "Medium") && row->score > 300);       // the bots harvested: they played
        }
        ASSERT_TRUE(s.referee_hash != 0 && s.ticks > 1000);
        ASSERT_TRUE(ann.hash_at.count(s.ticks) == 1);
        ASSERT_TRUE(ann.hash_at[s.ticks] == s.referee_hash);                              // the referee and the person's game, the same state at the same tick
        ASSERT_TRUE(ann.sim.is_match_over());
        const RoomStatus end = w.status("FILL-1");
        ASSERT_EQ(end.bots.size(), size_t{3});                                            // the seats stay listed after the match
    } TEST_END();

    TEST_CASE("S3.61 The Fill Only Happens On The Leader's Request: A Player Who Is Not The Leader Is Ignored, Fill None Is The START Of Protocol 7 (One Person Alone Does Nothing, Two Start Without Bots), A Room That Fills Up Starts By Itself With No Bots, A Room For Three Gets Two Bots (Never More Than The Room's Players), Easy And Hard Name Their Bots, A Room Without A Leader Ignores The Fill") {
        {   // not the leader
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "FR-1");
            Client& bob = w.connect("Bob", "FR-1");
            w.run(500);
            bob.end->send(net::encode(net::StartRequestMsg::all(net::FillLevel::Hard)));
            w.run(1500);
            RoomStatus s = w.status("FR-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 2 && s.ignored_start_requests == 1);
            ASSERT_EQ(bob.lobby->phase(), net::ClientLobby::Phase::InRoom);
            // fill none, two people: today's early start, no bots
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));
            w.run(1500);
            s = w.status("FR-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 2 && ann.sim.roster_mask() == 0x03);
        }
        {   // one person alone, fill none: nothing happens (the room needs two)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-2", 4), w.now).ok);
            Client& ann = w.connect("Ann", "FR-2");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));
            w.run(1500);
            RoomStatus s = w.status("FR-2");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1 && s.ignored_start_requests == 1);
        }
        {   // a room that fills up starts by itself: no bots, whatever the leader once asked for
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-3", 2), w.now).ok);
            Client& ann = w.connect("Ann", "FR-3");
            w.connect("Bob", "FR-3");
            w.run(2500);
            RoomStatus s = w.status("FR-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.bots.empty());
            ASSERT_TRUE(ann.sim.roster_mask() == 0x03);
        }
        {   // a full room that the leader's fill request finds already loading adds nothing: the request crossed the start
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-4", 2), w.now).ok);
            Client& ann = w.connect("Ann", "FR-4");
            w.connect("Bob", "FR-4");
            w.run(500);
            ann.end->send(net::encode(net::StartRequestMsg::all(net::FillLevel::Medium)));
            w.run(2500);
            RoomStatus s = w.status("FR-4");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 2);
        }
        {   // a room for three: one person and two bots, the empty seat beyond the room's players stays empty; the level names the bots
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-5", 3), w.now).ok);
            Client& ann = w.connect("Ann", "FR-5");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Easy));
            w.run(2000);
            RoomStatus s = w.status("FR-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.expected == 3 && s.bots.size() == 2);
            ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == bn("FR-5", 1, "Easy") && s.names[2] == bn("FR-5", 2, "Easy") && s.names[3].empty());
            ASSERT_TRUE(s.bots[0].level == "easy" && s.bots[1].level == "easy" && ann.sim.roster_mask() == 0x07);
        }
        {   // hard
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-6", 2), w.now).ok);
            Client& ann = w.connect("Ann", "FR-6");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Hard));
            w.run(2000);
            RoomStatus s = w.status("FR-6");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.bots.size() == 1 && s.bots[0].level == "hard" && s.names[1] == bn("FR-6", 1, "Hard"));
        }
        {   // a room without a leader (early_start false) ignores every request, fill or not
            World w;
            RoomSpec spec = spec_of("FR-7", 4);
            spec.early_start = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "FR-7");
            w.run(500);
            ann.end->send(net::encode(net::StartRequestMsg::all(net::FillLevel::Medium)));
            w.run(1500);
            RoomStatus s = w.status("FR-7");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1 && s.ignored_start_requests == 1);
        }
    } TEST_END();

    TEST_CASE("S3.62 Bots And Fog Of War Never Mix: A Fill Request In A Room With Fog Is Refused And The Leader Is Told Why (A Notice From The Room, To The Leader Only); One Person Alone Stays In The Waiting Room, Two Start Without Bots; A Room Specification With Bots And Fog Is A 400") {
        {
            World w;
            RoomSpec spec = spec_of("FOG-1", 4);
            spec.fog = true;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "FOG-1");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Hard));
            w.run(1500);
            RoomStatus s = w.status("FOG-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillFog}));
            ASSERT_TRUE(ann.room_chat[0].notice());
            Client& bob = w.connect("Bob", "FOG-1");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));                  // two people: the match goes on without bots, and the leader is told again
            w.run(2000);
            s = w.status("FOG-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 2 && ann.sim.is_fog_of_war_enabled());
            ASSERT_EQ(ann.room_chat.size(), size_t{2});
            ASSERT_TRUE(bob.room_chat.empty());                                              // the notice is the leader's alone
        }
        {
            World w;
            RoomSpec spec = spec_of("FOG-2", 4);
            spec.fog = true;
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            const CreateResult made = w.mgr.create_room(spec, w.now);
            ASSERT_TRUE(!made.ok && made.http_status == 400 && made.error.find("Fog") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.63 A Cancelled Start Takes The Fill Away Again: A Player Who Cannot Load The Map Cancels It, The Bots Go (The Room Is As It Was: The Status Lists None, Their Seats Are Free), The Next Player Who Comes Gets A Seat, And The Leader's New Request Seats Bots Again Round The Three People; The Pause After A Cancel Loses A Request") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("CAN-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "CAN-1");
        Client& bob = w.connect("Bob", "CAN-1");
        bob.fail_load = true;                                                              // Bob's copy of the map is no good
        w.run(500);
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
        w.run(1000);
        RoomStatus s = w.status("CAN-1");
        ASSERT_TRUE(s.state == RoomState::Waiting);                                        // cancelled
        ASSERT_TRUE(s.bots.empty() && s.joined == 2 && s.names[2].empty() && s.names[3].empty());
        ASSERT_TRUE(ann.lobby->room().slots[2].state == net::SlotState::Empty && ann.lobby->room().slots[3].state == net::SlotState::Empty);      // the room that Ann sees has no bots
        Client& cat = w.connect("Cat", "CAN-1");                                           // a seat for the next player
        w.run(300);
        ASSERT_TRUE(cat.lobby->phase() == net::ClientLobby::Phase::InRoom && w.status("CAN-1").joined == 3);
        bob.fail_load = false;
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));                    // inside the pause of two seconds: lost
        w.run(600);
        s = w.status("CAN-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty());
        w.run(2500);
        ASSERT_TRUE(w.status("CAN-1").state == RoomState::Waiting);                       // (nobody asked again: three of four do not start by themselves)
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
        w.run(2000);
        s = w.status("CAN-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 1);
        ASSERT_TRUE(s.bots[0].seat == 3 && s.bots[0].fill && s.names[3] == bn("CAN-1", 3, "Medium") && s.names[2] == "Cat");
        ASSERT_TRUE(ann.sim.roster_mask() == 0x0F);
    } TEST_END();

    TEST_CASE("S3.64 A Map The Filled Roster Cannot Play: The Request Does Nothing But Tell The Leader (A Notice), The Room Does Not Fail And Seats No Bot; The Same Request Where The Bots Take Playable Seats Starts") {
        const std::string dir = temp_dir_for("fill_marker");
        {   // TINY with the green start marker moved outside the grid (as in S3.14 and S3.29)
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
            const assets::AnthillSpawn* green = nullptr;
            for (const auto& sp : tiny.anthill_spawns) {
                if (sp.tile_id == 154) green = &sp;
            }
            ASSERT_TRUE(green != nullptr);
            const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
            size_t at = bytes.size();
            for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
            }
            ASSERT_TRUE(at < bytes.size());
            bytes[at + 2] = 200;
            bytes[at + 3] = 0;
            std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const auto run_in = [&dir](World& w, uint32_t ms) {
            for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
                w.now += 10;
                w.net.set_time(w.now);
                w.mgr.update(w.now);
                for (auto& c : w.clients) c->update(w.now, dir);
            }
        };
        {   // Ann in seat 1 (red), a room for three: the lowest empty seats are 0 (green) and 2: the filled roster has green, which this map cannot play
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MAP-1", 3, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "MAP-1", 1);
            run_in(w, 500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
            run_in(w, 2000);
            RoomStatus s = w.status("MAP-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillMap}));
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            ASSERT_TRUE(ann.lobby->room().slots[0].state == net::SlotState::Empty);        // (no bot appeared and went away: the room never had one)
        }
        {   // Ann in seat 0 would be the green that cannot play: nothing to do with the fill, the room is for 3 with a bot at red and blue: that roster has green: also refused
            // Ann in seat 1 (red) and Bob in seat 2 (blue) start with no bots: the map is playable for them (S3.29's second case); a fill for them changes nothing
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MAP-2", 4, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "MAP-2", 1);
            w.connect("Bob", "MAP-2", 2);
            run_in(w, 500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));                 // the empty seats are 0 and 3: green again
            run_in(w, 1500);
            ASSERT_TRUE(w.status("MAP-2").state == RoomState::Waiting && w.status("MAP-2").bots.empty());
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));                   // without the fill the same two start
            run_in(w, 2000);
            ASSERT_TRUE(w.status("MAP-2").state == RoomState::Running && w.status("MAP-2").bots.empty());
        }
        std::error_code ignore;
        fs::remove_all(dir, ignore);
    } TEST_END();

    TEST_CASE("S3.125 Protocol 13, A Level For Each Seat Of The Leader's Fill: Each Empty Seat Gets The Bot Of Its Own Level (Named For It, The Lowest Seat First, Never More Than The Room's Players), A Seat That A Person Took Meanwhile Is Skipped And Its Level Is Ignored, A Level Only For Taken Seats Seats Nobody, With Fog Of War No Bot Is Seated And The Teams Still Count") {
        using net::FillLevel;
        const auto fill_of = [](FillLevel a, FillLevel b, FillLevel c, FillLevel d) { return std::array<FillLevel, 4>{a, b, c, d}; };
        {   // seat 1 Easy and seat 3 Hard, seat 2 stays empty: each bot has the level, the name and the seat that was asked for
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PS-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PS-1");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::None, FillLevel::Easy, FillLevel::None, FillLevel::Hard)));
            w.run(2000);
            const RoomStatus s = w.status("PS-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.bots.size() == 2);
            ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].level == "easy" && s.bots[0].name == bn("PS-1", 1, "Easy") && s.bots[0].fill);
            ASSERT_TRUE(s.bots[1].seat == 3 && s.bots[1].level == "hard" && s.bots[1].name == bn("PS-1", 3, "Hard") && s.bots[1].fill);
            ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == bn("PS-1", 1, "Easy") && s.names[2].empty() && s.names[3] == bn("PS-1", 3, "Hard"));
            ASSERT_EQ(ann.sim.roster_mask(), 0x0B);                                            // the roster of the Start: seats 0, 1 and 3
            ASSERT_TRUE(ann.lobby->start_info().names[1] == bn("PS-1", 1, "Easy") && ann.lobby->start_info().names[3] == bn("PS-1", 3, "Hard"));
            ASSERT_EQ(s.teams, std::string("ffa"));
        }
        {   // three levels in three seats: Medium, Hard, Easy
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PS-2", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PS-2");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::None, FillLevel::Medium, FillLevel::Hard, FillLevel::Easy)));
            w.run(2000);
            const RoomStatus s = w.status("PS-2");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.size() == 3);
            ASSERT_TRUE(s.bots[0].level == "medium" && s.bots[1].level == "hard" && s.bots[2].level == "easy");
            ASSERT_TRUE(s.names[1] == bn("PS-2", 1, "Medium") && s.names[2] == bn("PS-2", 2, "Hard") && s.names[3] == bn("PS-2", 3, "Easy"));
        }
        {   // a seat that a person has taken: Bob sits in seat 2, so the level asked for seat 2 is ignored and the others are seated; the room is full with them
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PS-3", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PS-3");
            Client& bob = w.connect("Bob", "PS-3", 2);
            w.run(500);
            ASSERT_EQ(bob.lobby->my_seat(), 2);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::None, FillLevel::Easy, FillLevel::Hard, FillLevel::Medium)));
            w.run(2000);
            const RoomStatus s = w.status("PS-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 2);
            ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].level == "easy" && s.bots[1].seat == 3 && s.bots[1].level == "medium");      // (no Hard bot anywhere)
            ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == bn("PS-3", 1, "Easy") && s.names[2] == "Bob" && s.names[3] == bn("PS-3", 3, "Medium"));
        }
        {   // the person's own seat holds a level too (the leader's screen does not show it): it is ignored
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PS-4", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PS-4");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::Hard, FillLevel::None, FillLevel::Easy, FillLevel::None)));
            w.run(2000);
            const RoomStatus s = w.status("PS-4");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.bots.size() == 1);
            ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[0].level == "easy" && s.names[0] == "Ann" && s.names[1].empty());
        }
        {   // the cap is the room's players, as ever: a room for three with one person gets two bots, the lowest seats that were asked for; one with two people gets one
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PS-5", 3), w.now).ok);
            Client& ann = w.connect("Ann", "PS-5");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard)));
            w.run(2000);
            const RoomStatus s = w.status("PS-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.bots.size() == 2);
            ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].level == "easy" && s.bots[1].seat == 2 && s.bots[1].level == "medium" && s.names[3].empty());      // (the Hard bot of seat 3 does not fit)
            World v;
            ASSERT_TRUE(v.mgr.create_room(spec_of("PS-6", 3), v.now).ok);
            Client& cat = v.connect("Cat", "PS-6");
            v.connect("Dan", "PS-6");
            v.run(500);
            ASSERT_TRUE(cat.lobby->request_start(fill_of(FillLevel::None, FillLevel::None, FillLevel::Hard, FillLevel::Easy)));
            v.run(2000);
            const RoomStatus t = v.status("PS-6");
            ASSERT_TRUE(t.state == RoomState::Running && t.joined == 3 && t.bots.size() == 1);
            ASSERT_TRUE(t.bots[0].seat == 2 && t.bots[0].level == "hard" && t.names[3].empty());
        }
        {   // a level only for seats that people hold seats nobody: one person alone cannot start (the request is ignored, no offence), two people start with nobody added
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PS-7", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PS-7");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::Medium, FillLevel::None, FillLevel::None, FillLevel::None)));
            w.run(1500);
            RoomStatus s = w.status("PS-7");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1);
            Client& bob = w.connect("Bob", "PS-7");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::Medium, FillLevel::Hard, FillLevel::None, FillLevel::None)));
            w.run(2000);
            s = w.status("PS-7");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.bots.empty() && s.names[1] == "Bob");
            ASSERT_EQ(bob.sim.roster_mask(), 0x03);
        }
        {   // Fog of War: no bot is seated (the leader is told, as in protocol 11), and the teams of the same request still count for the people who play
            World w;
            RoomSpec spec = spec_of("PS-8", 4);
            spec.fog = true;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "PS-8");
            Client& bob = w.connect("Bob", "PS-8");
            Client& cat = w.connect("Cat", "PS-8");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(fill_of(FillLevel::None, FillLevel::None, FillLevel::None, FillLevel::Hard), sim::StartTeams{true, 0, 1}));
            w.run(2500);
            const RoomStatus s = w.status("PS-8");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 3);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillFog}));      // the leader alone is told about the bots, and nothing about the teams: they were made
            ASSERT_TRUE(said(bob.room_chat).empty() && said(cat.room_chat).empty());
            ASSERT_EQ(s.teams, std::string("0+1"));
            ASSERT_EQ(std::string(1, static_cast<char>('0' + s.allies[0])) + static_cast<char>('0' + s.allies[1]) + static_cast<char>('0' + s.allies[2]), std::string("104"));       // seat 0 and 1 are a team, seat 2 plays alone
            ASSERT_TRUE(ann.sim.alliance_of(0) == 1 && ann.sim.alliance_of(1) == 0 && ann.sim.alliance_of(2) == sim::ALLIANCE_NONE);
        }
    } TEST_END();

    TEST_CASE("S3.126 Protocol 13, The Leader's Teams: A Match Starts With Them On Every Engine Before Its First Tick (The Referee's Own Included: The Status At Tick 0), The Machines' State Hashes Agree With The Referee's To The End; Teams That The Seats Cannot Make Start The Match Without Them And Tell Everybody In The Room Why (A Notice, Once, People Only); Only The Leader's Request Counts; Free For All Is What It Was") {
        using net::FillLevel;
        const auto allies_text = [](const std::array<uint8_t, 4>& a) {
            std::string out;
            for (const uint8_t v : a) out += std::to_string(static_cast<unsigned>(v));
            return out;
        };
        const auto engine_allies = [](const sim::SimulationEngine& e) {
            std::string out;
            for (uint8_t seat = 0; seat < 4; ++seat) out += std::to_string(static_cast<unsigned>(e.alliance_of(seat)));
            return out;
        };
        {   // one person and three bots, Green + Blue against Red + Black: the alliances stand at tick 0 on the referee and on the machine; no notice; the match is played to its end and the machine
            // stands at the referee's state at the same tick (a machine that had not made the teams would have diverged at the first hash check, turn 19, and failed the room)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-1");
            ann.record_hashes = true;
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::StartRequestMsg::all(FillLevel::Medium).fill, sim::StartTeams{true, 0, 2}));
            w.run(1500);
            RoomStatus s = w.status("TM-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.ticks == 0 && s.turns == 0);          // (in the "Get ready" dialog: no turn is sealed, no tick has run)
            ASSERT_EQ(s.teams, std::string("0+2"));
            ASSERT_EQ(allies_text(s.allies), std::string("2301"));                               // the referee: 0 with 2, 1 with 3
            ASSERT_EQ(engine_allies(ann.sim), std::string("2301"));                              // the machine
            ASSERT_EQ(ann.sim.current_tick(), uint64_t{0});
            ASSERT_TRUE(ann.lobby->start_info().teams() == sim::StartTeams({true, 0, 2}));      // what the Start said
            ASSERT_TRUE(said(ann.room_chat).empty());                                            // nothing was refused: no notice
            for (int guard = 0; guard < 4000 && w.status("TM-1").state == RoomState::Running; ++guard) w.run(250);
            s = w.status("TM-1");
            ASSERT_TRUE(s.state == RoomState::Finished);                                         // (a machine that diverged from the referee would have failed the room)
            ASSERT_FALSE(ann.session->desynced());
            w.run(Room::kGraceMs + 500);
            ASSERT_TRUE(s.referee_hash != 0 && s.ticks > 1000 && ann.hash_at.count(s.ticks) == 1 && ann.hash_at[s.ticks] == s.referee_hash);
            ASSERT_EQ(engine_allies(ann.sim), std::string("2301"));                              // the bots keep a team for the whole match
        }
        {   // three people, the pair Red + Blue (1 + 2): the third seat plays alone; both machines and the referee stand the same
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-2", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-2");
            Client& bob = w.connect("Bob", "TM-2");
            Client& cat = w.connect("Cat", "TM-2");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 1, 2}));        // no bots: the three people play
            w.run(2500);
            const RoomStatus s = w.status("TM-2");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.teams == "1+2");
            ASSERT_EQ(allies_text(s.allies), std::string("4214"));
            for (Client* c : {&ann, &bob, &cat}) {
                ASSERT_EQ(engine_allies(c->sim), std::string("4214"));
                ASSERT_TRUE(said(c->room_chat).empty());
            }
            w.run(12000);
            ASSERT_TRUE(w.status("TM-2").state == RoomState::Running && !ann.session->desynced() && !bob.session->desynced() && !cat.session->desynced());
        }
        {   // teams that cannot be made: two people play and a team would be both of them (the original ends such a match at once): the match starts without teams, each person is told once
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-3", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-3");
            Client& bob = w.connect("Bob", "TM-3");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 0, 1}));
            w.run(2500);
            const RoomStatus s = w.status("TM-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.teams == "ffa" && allies_text(s.allies) == "4444");
            const std::string notice = std::string("255||") + net::kNoticeNoTeams + "only two seats play: a team of them would end the match at once.";
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{notice}));
            ASSERT_EQ(said(bob.room_chat), (std::vector<std::string>{notice}));                  // everybody in the room, not the leader alone
            ASSERT_TRUE(ann.room_chat[0].notice() && bob.room_chat[0].notice());
            ASSERT_TRUE(ann.lobby->start_info().team_a == net::kNoTeam && ann.lobby->start_info().team_b == net::kNoTeam);
            ASSERT_EQ(engine_allies(ann.sim), std::string("4444"));
        }
        {   // a seat of the pair that does not play: seats 0, 1 and 2 are here, the pair is 0 + 3; with a bot filled into seat 3 the same pair is made
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-4", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-4");
            Client& bob = w.connect("Bob", "TM-4");
            Client& cat = w.connect("Cat", "TM-4");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 0, 3}));
            w.run(2500);
            RoomStatus s = w.status("TM-4");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.teams == "ffa" && allies_text(s.allies) == "4444");
            const std::string notice = std::string("255||") + net::kNoticeNoTeams + "Black does not play in this match.";
            for (Client* c : {&ann, &bob, &cat}) ASSERT_EQ(said(c->room_chat), (std::vector<std::string>{notice}));
            World v;
            ASSERT_TRUE(v.mgr.create_room(spec_of("TM-5", 4), v.now).ok);
            Client& dan = v.connect("Dan", "TM-5");
            Client& eve = v.connect("Eve", "TM-5");
            Client& fay = v.connect("Fay", "TM-5");
            v.run(500);
            ASSERT_TRUE(dan.lobby->request_start(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::None, FillLevel::Easy}, sim::StartTeams{true, 0, 3}));
            v.run(2500);
            s = v.status("TM-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 1 && s.teams == "0+3" && allies_text(s.allies) == "3210");
            for (Client* c : {&dan, &eve, &fay}) {
                ASSERT_TRUE(said(c->room_chat).empty());
                ASSERT_EQ(engine_allies(c->sim), std::string("3210"));
            }
        }
        {   // only the leader's request counts: a guest's StartRequest with teams is ignored altogether; a room that names no teams of its own and fills up starts by itself with none (S3.129: a room whose create block names them has them)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-6", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-6");
            Client& bob = w.connect("Bob", "TM-6");
            Client& cat = w.connect("Cat", "TM-6");
            w.run(500);
            net::StartRequestMsg guest;
            guest.set_teams(sim::StartTeams{true, 1, 2});
            bob.end->send(net::encode(guest));
            w.run(1500);
            RoomStatus s = w.status("TM-6");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 1 && s.teams.empty());
            Client& dan = w.connect("Dan", "TM-6");                                              // the room is full now and starts by itself
            w.run(2500);
            s = w.status("TM-6");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.teams == "ffa" && allies_text(s.allies) == "4444");
            for (Client* c : {&ann, &bob, &cat, &dan}) ASSERT_TRUE(said(c->room_chat).empty());
        }
        {   // free for all, the request of protocol 11: no teams, no notice, the match is what it always was
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-7", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-7");
            Client& bob = w.connect("Bob", "TM-7");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(FillLevel::None));
            w.run(2500);
            const RoomStatus s = w.status("TM-7");
            ASSERT_TRUE(s.state == RoomState::Running && s.teams == "ffa" && allies_text(s.allies) == "4444");
            ASSERT_TRUE(said(ann.room_chat).empty() && said(bob.room_chat).empty());
        }
        {   // a start that is cancelled (a machine cannot load the map) and tried again: the room tells again, as the second start happens
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TM-8", 4), w.now).ok);
            Client& ann = w.connect("Ann", "TM-8");
            Client& bob = w.connect("Bob", "TM-8");
            bob.fail_load = true;
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 0, 1}));
            w.run(1500);
            ASSERT_TRUE(w.status("TM-8").state == RoomState::Waiting);                           // cancelled
            bob.fail_load = false;
            w.run(2500);                                                                         // (the pause after a cancelled start)
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 0, 1}));
            w.run(2500);
            ASSERT_TRUE(w.status("TM-8").state == RoomState::Running);
            size_t told = 0;
            for (const std::string& line : said(ann.room_chat)) told += line.find("No teams: ") != std::string::npos ? 1u : 0u;
            ASSERT_EQ(told, size_t{2});                                                          // once for each start that went through (the first one was cancelled after it was told)
        }
    } TEST_END();

    TEST_CASE("S3.129 Protocol 13, The Room's Own Teams (Its Create Block Names Them, Protocol 15): A Room That Fills With People Starts By Itself WITH The Teams On Every Engine And The Referee's Own, Before The First Tick; So Does An Early START Whose Request Names No Teams; The Room's Teams Win Over A Leader's Request (Even When The Room's Cannot Be Made: It Starts Without Teams And Says Why, Once, To Everybody); A Pair That The Room's Seats Cannot Make (A Seat That Does Not Play, A Room Of Two) Says So At The Start; What A Block May Name, Edge By Edge") {
        using net::FillLevel;
        const auto allies_text = [](const std::array<uint8_t, 4>& a) {
            std::string out;
            for (const uint8_t v : a) out += std::to_string(static_cast<unsigned>(v));
            return out;
        };
        const auto engine_allies = [](const sim::SimulationEngine& e) {
            std::string out;
            for (uint8_t seat = 0; seat < 4; ++seat) out += std::to_string(static_cast<unsigned>(e.alliance_of(seat)));
            return out;
        };
        ServerLimits limits;
        limits.demo_rooms = 40;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL"};
        {   // four people fill a room whose block names 0 + 1: it starts by itself, nobody pressed START, nobody asked for teams
            World w(limits);
            const std::string code = "aaaaaaaa";
            Client& ann = w.connect_creating("Ann", code, block_of("TINY.LVL", 4, 0, 1));
            w.run(300);
            RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Waiting && s.expected == 4 && s.room_teams == "0+1" && s.teams.empty());     // (the room has them from its block: before anybody asks)
            ASSERT_TRUE(ann.lobby->room().teams() == sim::StartTeams({true, 0, 1}));              // (and says so in the Room message, to its first player ...)
            Client& bob = w.connect("Bob", code);
            Client& cat = w.connect("Cat", code);
            w.run(300);
            ASSERT_TRUE(w.status(code).state == RoomState::Waiting && w.status(code).joined == 3);
            ASSERT_TRUE(bob.lobby->room().teams() == sim::StartTeams({true, 0, 1}) && cat.lobby->room().teams() == sim::StartTeams({true, 0, 1}));      // (... and to those who come after)
            Client& dan = w.connect("Dan", code);                                                // the room is full now
            w.run(2500);
            s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.empty());
            ASSERT_EQ(s.ticks, uint32_t{0});                                                      // (in the "Get ready" dialog: the teams stand before the first tick)
            ASSERT_TRUE(s.teams == "0+1" && s.room_teams == "0+1");
            ASSERT_EQ(allies_text(s.allies), std::string("1032"));                                // the referee: 0 with 1, 2 with 3
            for (Client* c : {&ann, &bob, &cat, &dan}) {
                ASSERT_EQ(engine_allies(c->sim), std::string("1032"));                            // every machine
                ASSERT_EQ(c->sim.current_tick(), uint64_t{0});
                ASSERT_TRUE(c->lobby->start_info().teams() == sim::StartTeams({true, 0, 1}));    // what the Start said
                ASSERT_TRUE(said(c->room_chat).empty());                                          // nothing was refused: no notice
            }
            w.run(14000);                                                                          // the match goes on: the referee compares the machines' hashes at its checkpoints (turn 19 and on)
            s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.turns > 40);
            for (Client* c : {&ann, &bob, &cat, &dan}) ASSERT_TRUE(!c->session->desynced() && !c->lost);
            ASSERT_EQ(allies_text(s.allies), std::string("1032"));
        }
        {   // the other pairs of a four-player room, and a room of three: each makes its own pair (the other seats as a team when both play)
            struct Case { const char* code; uint8_t players; uint8_t a; uint8_t b; const char* teams; const char* allies; };
            const Case cases[] = {
                {"bbbbbbbb", 4, 0, 2, "0+2", "2301"},
                {"cccccccc", 4, 0, 3, "0+3", "3210"},
                {"dddddddd", 3, 1, 2, "1+2", "4214"},                                              // seat 0 plays alone
                {"eeeeeeee", 3, 0, 1, "0+1", "1044"},                                              // seat 2 plays alone
            };
            for (const Case& c : cases) {
                World w(limits);
                std::vector<Client*> people;
                for (uint8_t i = 0; i < c.players; ++i) {
                    const std::string name = std::string("P") + std::to_string(i);
                    people.push_back(i == 0 ? &w.connect_creating(name, c.code, block_of("TINY.LVL", c.players, c.a, c.b)) : &w.connect(name, c.code));
                }
                w.run(3500);
                const RoomStatus s = w.status(c.code);
                ASSERT_MSG(s.state == RoomState::Running && s.joined == c.players && s.teams == c.teams && s.room_teams == c.teams, c.code);
                ASSERT_MSG(allies_text(s.allies) == c.allies, c.code);
                for (Client* p : people) {
                    ASSERT_MSG(engine_allies(p->sim) == c.allies && said(p->room_chat).empty(), c.code);
                }
            }
        }
        {   // an early START: three people and a leader whose request names no teams: the room's teams are made for the seats that play (seat 2 plays alone), no notice
            World w(limits);
            const std::string code = "ffffffff";
            Client& ann = w.connect_creating("Ann", code, block_of("TINY.LVL", 4, 0, 1));
            Client& bob = w.connect("Bob", code);
            Client& cat = w.connect("Cat", code);
            w.run(600);
            ASSERT_TRUE(w.status(code).state == RoomState::Waiting && w.status(code).joined == 3);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{}));
            w.run(2500);
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.teams == "0+1");
            ASSERT_EQ(allies_text(s.allies), std::string("1044"));
            for (Client* c : {&ann, &bob, &cat}) {
                ASSERT_EQ(engine_allies(c->sim), std::string("1044"));
                ASSERT_TRUE(said(c->room_chat).empty());
            }
            w.run(10000);
            ASSERT_TRUE(w.status(code).state == RoomState::Running && !ann.session->desynced() && !bob.session->desynced() && !cat.session->desynced());
        }
        {   // an early START with a bot that the leader seated: the same teams, now for four seats
            World w(limits);
            const std::string code = "gggggggg";
            Client& ann = w.connect_creating("Ann", code, block_of("TINY.LVL", 4, 0, 1));
            Client& bob = w.connect("Bob", code);
            Client& cat = w.connect("Cat", code);
            w.run(600);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::None, FillLevel::Easy}, sim::StartTeams{}));
            w.run(2500);
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 1 && s.teams == "0+1");
            ASSERT_EQ(allies_text(s.allies), std::string("1032"));
            for (Client* c : {&ann, &bob, &cat}) {
                ASSERT_EQ(engine_allies(c->sim), std::string("1032"));
                ASSERT_TRUE(said(c->room_chat).empty());
            }
        }
        {   // precedence: the leader's request names 1 + 2, the room's block 0 + 1: the room's
            World w(limits);
            const std::string code = "hhhhhhhh";
            Client& ann = w.connect_creating("Ann", code, block_of("TINY.LVL", 4, 0, 1));
            Client& bob = w.connect("Bob", code);
            Client& cat = w.connect("Cat", code);
            w.run(600);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 1, 2}));
            w.run(2500);
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.teams == "0+1");
            ASSERT_EQ(allies_text(s.allies), std::string("1044"));                                // (not 4214: the request's pair)
            for (Client* c : {&ann, &bob, &cat}) {
                ASSERT_EQ(engine_allies(c->sim), std::string("1044"));
                ASSERT_TRUE(said(c->room_chat).empty());
            }
            // ... and the same request in a room whose block names none is what it always was (S3.126): the leader's pair
            World v(limits);
            const std::string plain = "iiiiiiii";
            Client& dan = v.connect_creating("Dan", plain, block_of("TINY.LVL", 4));
            Client& eve = v.connect("Eve", plain);
            Client& fay = v.connect("Fay", plain);
            v.run(600);
            ASSERT_TRUE(v.status(plain).room_teams.empty());
            ASSERT_TRUE(dan.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 1, 2}));
            v.run(2500);
            ASSERT_TRUE(v.status(plain).state == RoomState::Running && v.status(plain).teams == "1+2");
            ASSERT_EQ(allies_text(v.status(plain).allies), std::string("4214"));
            for (Client* c : {&dan, &eve, &fay}) ASSERT_EQ(engine_allies(c->sim), std::string("4214"));
        }
        {   // the room's teams that its seats cannot make stay the room's: the match starts WITHOUT teams (never with the request's), and everybody in the room is told why, once
            World w(limits);
            const std::string code = "jjjjjjjj";                                                   // seat 3 would be the other half of the team
            Client& ann = w.connect_creating("Ann", code, block_of("TINY.LVL", 4, 0, 3));
            Client& bob = w.connect("Bob", code);
            Client& cat = w.connect("Cat", code);
            w.run(600);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 1, 2}));    // (a pair that three seats can make)
            w.run(2500);
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.teams == "ffa" && s.room_teams == "0+3" && allies_text(s.allies) == "4444");
            const std::string notice = std::string("255||") + net::kNoticeNoTeams + "Black does not play in this match.";
            for (Client* c : {&ann, &bob, &cat}) {
                ASSERT_EQ(said(c->room_chat), (std::vector<std::string>{notice}));                // everybody, once
                ASSERT_TRUE(c->room_chat[0].notice());
                ASSERT_EQ(engine_allies(c->sim), std::string("4444"));
                ASSERT_TRUE(c->lobby->start_info().team_a == net::kNoTeam && c->lobby->start_info().team_b == net::kNoTeam);
            }
        }
        {   // a pair that the room's seats cannot make, and nobody asked for anything: a room of three that names seat 3, and a room of two that names a team of both
            World w(limits);
            const std::string three = "kkkkkkkk";
            std::vector<Client*> people;
            people.push_back(&w.connect_creating("Ann", three, block_of("TINY.LVL", 3, 0, 3)));
            for (const char* name : {"Bob", "Cat"}) people.push_back(&w.connect(name, three));
            w.run(3500);
            RoomStatus s = w.status(three);
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.teams == "ffa" && s.room_teams == "0+3" && allies_text(s.allies) == "4444");
            for (Client* c : people) ASSERT_EQ(said(c->room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeNoTeams + "Black does not play in this match."}));
            const std::string two = "llllllll";
            Client& dan = w.connect_creating("Dan", two, block_of("TINY.LVL", 2, 0, 1));
            Client& eve = w.connect("Eve", two);
            w.run(3500);
            s = w.status(two);
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.expected == 2 && s.teams == "ffa" && s.room_teams == "0+1" && allies_text(s.allies) == "4444");
            const std::string notice = std::string("255||") + net::kNoticeNoTeams + "only two seats play: a team of them would end the match at once.";
            ASSERT_EQ(said(dan.room_chat), (std::vector<std::string>{notice}));
            ASSERT_EQ(said(eve.room_chat), (std::vector<std::string>{notice}));
        }
        {   // a room without early start has no leader: a StartRequest with teams (raw: no client lobby sends one there) counts for nothing, and the room that fills up in the same pass starts by itself
            // with no teams when it names none, and with its own when it names some (a room's own teams do not depend on a leader, nor on a block: they are its specification)
            for (const bool own : {false, true}) {
                World w;
                RoomSpec spec = spec_of(own ? "TM-10" : "TM-9", 4);
                spec.early_start = false;
                if (own) spec.teams = sim::StartTeams{true, 0, 2};
                ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
                Client& ann = w.connect("Ann", spec.code);
                Client& bob = w.connect("Bob", spec.code);
                Client& cat = w.connect("Cat", spec.code);
                w.run(500);
                ASSERT_FALSE(ann.lobby->is_leader());                                             // (there is none)
                net::StartRequestMsg request;
                request.set_teams(sim::StartTeams{true, 1, 2});
                ASSERT_TRUE(ann.end->send(net::encode(request)));                                  // seat 0's, as a modified client would send it
                Client& dan = w.connect("Dan", spec.code);                                         // ... and the room fills up as it is read
                w.run(3000);
                const RoomStatus s = w.status(spec.code);
                ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.ignored_start_requests == 1);
                ASSERT_EQ(s.teams, std::string(own ? "0+2" : "ffa"));
                ASSERT_EQ(allies_text(s.allies), std::string(own ? "2301" : "4444"));              // (never the request's 1 + 2: that would be "3210")
                for (Client* c : {&ann, &bob, &cat, &dan}) {
                    ASSERT_EQ(engine_allies(c->sim), std::string(own ? "2301" : "4444"));
                    ASSERT_TRUE(said(c->room_chat).empty());
                }
            }
        }
        {   // what a block may name, edge by edge (one Hello makes each room; the room says what it read)
            World w(limits);
            struct Case { const char* code; const char* map; uint8_t seats; uint8_t a; uint8_t b; const char* teams; };
            const Case cases[] = {
                {"mmmmmmmm", "TINY.LVL", 4, net::kNoTeam, net::kNoTeam, ""},                      // no teams: free for all
                {"nnnnnnnn", "TINY.LVL", 2, net::kNoTeam, net::kNoTeam, ""},
                {"oooooooo", "TINY.LVL", 3, 1, 2, "1+2"},
                {"pppppppp", "TINY.LVL", 4, 2, 3, "2+3"},
                {"qqqqqqqq", "TINY.LVL", 4, 1, 3, "1+3"},
                {"rrrrrrrr", "SMALL.LVL", 4, 0, 1, "0+1"},
                {"ssssssss", "SMALL.LVL", 2, 0, 1, "0+1"},                                         // a pair in a room of two is read (the start says it cannot be)
            };
            for (const Case& c : cases) {
                w.connect_creating("P", c.code, block_of(c.map, c.seats, c.a, c.b));
                w.run(300);
                const RoomStatus st = w.status(c.code);
                ASSERT_MSG(st.state == RoomState::Waiting && st.room_teams == c.teams && st.expected == c.seats, c.code);
            }
            // a block that no honest client sends is no block (the lobby leaves it out, the Hello joins: no room is made)
            struct Bad { const char* code; uint8_t a; uint8_t b; };
            const Bad bad[] = {{"tttttttt", 1, 0}, {"uuuuuuuu", 1, 1}, {"vvvvvvvv", 0, 4}, {"wwwwwwww", 0, net::kNoTeam}, {"xxxxxxxx", net::kNoTeam, 2}, {"yyyyyyyy", 4, 5}};
            for (const Bad& c : bad) {
                w.connect_creating("P", c.code, block_of("TINY.LVL", 4, c.a, c.b));
                w.run(300);
                ASSERT_MSG(w.status(c.code).map.empty(), c.code);
            }
            ASSERT_EQ(w.mgr.room_count(), sizeof(cases) / sizeof(cases[0]));
        }
    } TEST_END();

    TEST_CASE("S3.65 The Control Interface Seats Bots In A Room's Specification (\"bots\": [{\"seat\": 2, \"bot\": \"medium\"}]): The Status JSON Lists Them Next To The Players, A Mistake Is A 400 That Names The Key, A Person Who Joins Gets Another Seat, The Room Starts By Itself When The Person Has Come (The Bots Count As Players) And Plays To Its End; The Referee's Final Hash Is In The JSON") {
        World w;
        const auto call = [&w](const char* method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.body = body;
            return handle_control(w.mgr, rq, w.now);
        };
        const auto json_of = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ctl::HttpResponse r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":2,"code":"CB-1","seed":9,"bots":[{"seat":0,"bot":"medium"}]})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = json_of(r);
        ASSERT_TRUE(v.get("bots").is_array() && v.get("bots").size() == 1);
        const ctl::JsonValue& b = v.get("bots").at(0);
        ASSERT_TRUE(b.get("seat").as_int_or(9) == 0 && b.get("bot").str() == "medium" && b.get("kind").str() == "standard" && b.get("level").str() == "medium" &&
                    b.get("name").str() == bn("CB-1", 0, "Medium") && !b.get("fill").as_bool_or(true));
        ASSERT_TRUE(v.get("joined").as_int_or(0) == 1 && v.get("expected").as_int_or(0) == 2);
        ASSERT_TRUE(v.get("players").size() == 1 && v.get("players").at(0).get("name").str() == bn("CB-1", 0, "Medium") && v.get("players").at(0).get("bot").as_bool_or(false));      // (a bot is a player of the room, and the entry says it is a bot)
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","code":"CB-2"})");                  // no bots: an empty list in the JSON
        ASSERT_TRUE(r.status == 201 && json_of(r).get("bots").is_array() && json_of(r).get("bots").size() == 0);
        // the mistakes: each one a 400 that names the key
        const char* bad[] = {R"({"map":"TINY.LVL","bots":"medium"})", R"({"map":"TINY.LVL","bots":[1]})", R"({"map":"TINY.LVL","bots":[{"seat":2}]})", R"({"map":"TINY.LVL","bots":[{"bot":"easy"}]})",
                             R"({"map":"TINY.LVL","bots":[{"seat":4,"bot":"easy"}]})", R"({"map":"TINY.LVL","bots":[{"seat":-1,"bot":"easy"}]})",
                             R"({"map":"TINY.LVL","bots":[{"seat":"1","bot":"easy"}]})", R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":7}]})",
                             R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"expert"}]})", R"({"map":"TINY.LVL","players":4,"bots":[{"seat":1,"bot":"easy"},{"seat":1,"bot":"hard"}]})",           // a seat twice (with room for a person, so that only this is wrong)
                             R"({"map":"TINY.LVL","players":2,"bots":[{"seat":1,"bot":"easy"},{"seat":2,"bot":"easy"}]})",      // no seat left for a person
                             R"({"map":"TINY.LVL","players":3,"bots":[{"seat":0,"bot":"easy"},{"seat":1,"bot":"easy"},{"seat":2,"bot":"easy"}]})",
                             R"({"map":"TINY.LVL","fog":true,"bots":[{"seat":1,"bot":"easy"}]})"};
        for (const char* body : bad) {
            r = call("POST", "/rooms", body);
            ASSERT_EQ(r.status, 400);
            ASSERT_TRUE(json_of(r).get("error").str().find("bot") != std::string::npos || json_of(r).get("error").str().find("Bot") != std::string::npos);
        }
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"CB-3","bots":[{"seat":3,"bot":"hard"},{"seat":1,"bot":"worker:easy"},{"seat":2,"bot":"idle"}]})");
        ASSERT_EQ(r.status, 201);                                                          // three bots and one person; kinds and levels as --bot takes them
        v = json_of(r);
        ASSERT_TRUE(v.get("bots").size() == 3 && v.get("bots").at(0).get("seat").as_int_or(9) == 1 && v.get("bots").at(0).get("bot").str() == "worker:easy" &&
                    v.get("bots").at(1).get("bot").str() == "idle:medium" && v.get("bots").at(2).get("bot").str() == "hard");     // (listed by seat)
        ASSERT_TRUE(v.get("bots").at(2).get("style").str() == "random");                   // (a standard bot without a pinned style draws its own at the start of the match)
        ASSERT_TRUE(!v.get("bots").at(0).has("style") && !v.get("bots").at(1).has("style"));       // (the worker and the idle bot have no style: the key is absent, it does not say "random")
        // a pinned style (docs/BOTS.md, "Styles"): the text is what --bot takes, the style is listed, the name of the seat is still "Bot (Level)"; a style that the level may not play is refused
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"CB-3S","bots":[{"seat":3,"bot":"hard:raider"},{"seat":1,"bot":"standard:medium:defensive"}]})");
        ASSERT_EQ(r.status, 201);
        v = json_of(r);
        ASSERT_TRUE(v.get("bots").size() == 2 && v.get("bots").at(0).get("seat").as_int_or(9) == 1 && v.get("bots").at(0).get("bot").str() == "medium:defensive" && v.get("bots").at(0).get("style").str() == "defensive" &&
                    v.get("bots").at(0).get("level").str() == "medium" && v.get("bots").at(0).get("name").str() == bn("CB-3S", 1, "Medium") && v.get("bots").at(1).get("bot").str() == "hard:raider" &&
                    v.get("bots").at(1).get("style").str() == "raider" && v.get("bots").at(1).get("name").str() == bn("CB-3S", 3, "Hard"));
        for (const char* body : {R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"hard:economic"}]})", R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"worker:easy:raider"}]})", R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"medium:wild"}]})"}) {
            r = call("POST", "/rooms", body);
            ASSERT_EQ(r.status, 400);
            ASSERT_TRUE(json_of(r).get("error").str().find("style") != std::string::npos);
        }
        // a person joins CB-1 (a room for two with a bot at seat 0): it gets seat 1, and the room is full: it starts by itself
        Client& ann = w.connect("Ann", "CB-1", 0);                                         // (it asks for seat 0, which the bot has: the first free seat)
        ann.record_hashes = true;
        w.run(2500);
        RoomStatus s = w.status("CB-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && ann.lobby->my_seat() == 1);
        ASSERT_TRUE(s.names[0] == bn("CB-1", 0, "Medium") && s.names[1] == "Ann" && s.bots.size() == 1 && !s.bots[0].fill);
        ASSERT_TRUE(ann.sim.roster_mask() == 0x03);
        for (int guard = 0; guard < 4000 && w.status("CB-1").state == RoomState::Running; ++guard) w.run(250);
        w.run(Room::kGraceMs + 500);
        r = call("GET", "/rooms/CB-1");
        v = json_of(r);
        ASSERT_EQ(v.get("state").str(), std::string("finished"));
        ASSERT_TRUE(v.get("bots").size() == 1 && v.get("result").get("rows").size() == 2);
        ASSERT_TRUE(v.get("players").size() == 2 && v.get("players").at(0).get("bot").as_bool_or(false) && !v.get("players").at(1).get("bot").as_bool_or(true) && v.get("players").at(1).get("name").str() == "Ann");
        const std::string hex = v.get("state_hash").str();
        ASSERT_EQ(hex.size(), size_t{16});
        s = w.status("CB-1");
        char expected[17];
        std::snprintf(expected, sizeof(expected), "%016llx", static_cast<unsigned long long>(ann.hash_at[s.ticks]));
        ASSERT_EQ(hex, std::string(expected));                                             // the JSON says what the person's game stands at, at the end
        ASSERT_TRUE(row_of(s, 0) != nullptr && row_of(s, 0)->score > 300);
        // the list shows bots too
        r = call("GET", "/rooms");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 4);               // (CB-1, CB-2, CB-3 and the room with the pinned styles)
    } TEST_END();

    TEST_CASE("S3.66 A Room's Own Bots: The Early Start With Them (One Person And The Bot Of A Room For Three Start At The Leader's Request, No Fill Needed), And A Fill On Top Of Them Seats Only The Seats That Are Still Empty") {
        World w;
        RoomSpec spec = spec_of("CB-4", 3);
        spec.bots = {ai::BotSpec{2, "standard", ai::Level::Hard}};
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& ann = w.connect("Ann", "CB-4");
        w.run(500);
        RoomStatus s = w.status("CB-4");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.bots.size() == 1 && ann.lobby->is_leader());
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));                      // one person and the room's bot are two players
        w.run(2000);
        s = w.status("CB-4");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.names[2] == bn("CB-4", 2, "Hard") && s.bots.size() == 1 && !s.bots[0].fill);
        ASSERT_TRUE(ann.sim.roster_mask() == 0x05);
        {   // a room for four with a bot at seat 1: the fill takes seats 2 and 3 (Ann has 0), not seat 1
            World v;
            RoomSpec four = spec_of("CB-5", 4);
            four.bots = {ai::BotSpec{1, "standard", ai::Level::Easy}};
            ASSERT_TRUE(v.mgr.create_room(four, v.now).ok);
            Client& bob = v.connect("Bob", "CB-5", 0);
            v.run(500);
            ASSERT_TRUE(bob.lobby->request_start(net::FillLevel::Hard));
            v.run(2000);
            s = v.status("CB-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 3);
            ASSERT_TRUE(s.names[1] == bn("CB-5", 1, "Easy") && s.names[2] == bn("CB-5", 2, "Hard") && s.names[3] == bn("CB-5", 3, "Hard"));
            ASSERT_TRUE(!s.bots[0].fill && s.bots[1].fill && s.bots[2].fill);
        }
    } TEST_END();

    TEST_CASE("S3.67 Bot Seats In A Room That Holds Seats: A Bot Is Never Absent And Never Votes (Two People And Two Bots: The Person Who Is Cut Is The Only One Missing, The Vote Has ONE Voter, The Survivor's Vote Drops The Absent Person And The Bots Play On To The End, The Referee And The Survivor Agree); A Person Who Comes Back Through The Door Finds The Bots Where They Were; The Bots' Commands Wait While The Match Is Paused") {
        {
            RWorld w;
            RoomSpec spec = held_spec("BH-1", 4);
            spec.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}, ai::BotSpec{3, "standard", ai::Level::Easy}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "BH-1", 0);
            RClient& bob = w.connect("Bob", "BH-1", 1);
            ann.record_hashes = true;
            w.run(6000 + kPre);                                                                     // the room is full (two people and two bots): it started by itself
            ASSERT_TRUE(w.status("BH-1").state == RoomState::Running);
            ASSERT_TRUE(ann.sim.roster_mask() == 0x0F);
            const uint8_t bob_seat = bob.lobby->my_seat();
            bob.reconnects = false;
            w.cut(bob);
            w.run(1500);
            RoomStatus s = w.status("BH-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.absent[0].seat == bob_seat && s.absent[0].name == "Bob");        // only the person is missing: a bot has no connection to lose
            ASSERT_EQ(s.bots.size(), size_t{2});
            const uint32_t ticks_at_pause = s.ticks;
            w.run(10000);
            s = w.status("BH-1");
            ASSERT_TRUE(s.ticks <= ticks_at_pause + 2);                                      // nothing runs, the bots included
            ASSERT_TRUE(ann.session->presence().missing.size() == 1 && ann.session->presence().missing[0].seat == bob_seat);
            w.run(21000);                                                                    // 30 s away in all: the vote opens
            s = w.status("BH-1");
            ASSERT_TRUE(s.paused && s.vote_seat == bob_seat);
            ASSERT_EQ(s.voters, 1);                                                          // Ann alone: the bots do not vote, and are not counted as connected
            ASSERT_EQ(ann.session->presence().voters, 1);
            ASSERT_TRUE(ann.session->vote(bob_seat, true));                                  // more than half of ONE
            w.run(3000);
            s = w.status("BH-1");
            ASSERT_TRUE(s.state == RoomState::Running && !s.paused && s.drops_by_vote == 1);
            const uint32_t after_vote = s.ticks;
            w.run(10000);
            s = w.status("BH-1");
            ASSERT_TRUE(s.ticks > after_vote + 150);                                         // the match runs again, with the bots
            for (int guard = 0; guard < 4000 && w.status("BH-1").state == RoomState::Running; ++guard) w.run(250);
            w.run(Room::kGraceMs + 500);
            s = w.status("BH-1");
            ASSERT_TRUE(s.state == RoomState::Finished);
            ASSERT_TRUE(row_of(s, 2) != nullptr && row_of(s, 2)->score > 100 && row_of(s, 3) != nullptr && row_of(s, 3)->score > 100);       // the bots kept harvesting
            ASSERT_TRUE(ann.hash_at.count(s.ticks) == 1 && ann.hash_at[s.ticks] == s.referee_hash);
            ASSERT_FALSE(ann.session->desynced());
        }
        {   // the person comes back with its key: the bots are where they were, and everybody ends identical
            RWorld w;
            RoomSpec spec = held_spec("BH-2", 3);
            spec.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "BH-2", 0);
            RClient& bob = w.connect("Bob", "BH-2", 1);
            ann.record_hashes = true;
            bob.record_hashes = true;
            w.run(6000);
            ASSERT_TRUE(w.status("BH-2").state == RoomState::Running);
            bob.reconnects = false;
            w.cut(bob);
            w.run(4000);
            ASSERT_TRUE(w.status("BH-2").paused);
            const uint32_t ticks_at_pause = w.status("BH-2").ticks;
            bob.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("BH-2").paused; }, 8000));
            w.run(3000);
            RoomStatus s = w.status("BH-2");
            ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.ticks > ticks_at_pause + 40);
            for (int guard = 0; guard < 4000 && w.status("BH-2").state == RoomState::Running; ++guard) w.run(250);
            w.run(Room::kGraceMs + 500);
            s = w.status("BH-2");
            ASSERT_TRUE(s.state == RoomState::Finished && s.rejoins == 1);
            ASSERT_TRUE(ann.hash_at.count(s.ticks) == 1 && ann.hash_at[s.ticks] == s.referee_hash);
            ASSERT_TRUE(bob.hash_at.count(s.ticks) == 1 && bob.hash_at[s.ticks] == s.referee_hash);       // the one that was away, too
            ASSERT_TRUE(row_of(s, 2) != nullptr && row_of(s, 2)->score > 100);
            ASSERT_FALSE(ann.lost || bob.lost);
        }
    } TEST_END();

    TEST_CASE("S3.68 Chat In A Server's Waiting Room: Everybody Hears A Line With The Sender's Seat And Name (The Sender Too), A Player Who Comes Later Hears Only What Is Said After It Came, Chat Works While The Map Loads (A Slow Link Keeps The Room In Loading), A Flood Of Lines Costs The Sender Its Seat And Nobody Else Notices, And The Lines Are Kept For The Match's Log After The Start") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("CHAT-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "CHAT-1");
        Client& bob = w.connect("Bob", "CHAT-1");
        w.run(500);
        ASSERT_TRUE(ann.lobby->chat("hello Bob"));
        w.run(200);
        ASSERT_TRUE(bob.lobby->chat("hello Ann"));
        w.run(200);
        ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{"0|Ann|hello Bob", "1|Bob|hello Ann"}));
        ASSERT_EQ(said(bob.room_chat), said(ann.room_chat));
        Client& cat = w.connect("Cat", "CHAT-1");
        w.run(500);
        ASSERT_TRUE(cat.room_chat.empty());                                                  // what was said before Cat came is not for Cat
        ASSERT_TRUE(ann.lobby->chat("welcome Cat"));
        w.run(200);
        ASSERT_EQ(said(cat.room_chat), (std::vector<std::string>{"0|Ann|welcome Cat"}));
        ASSERT_EQ(said(bob.room_chat).size(), size_t{3});
        // a flood: 1500 lines at once from Bob; the chat budget (5 lines, then one a second) lets 5 through and then it is a violation each: he is out (BadRequest) at once
        net::ChatMsg spam;
        spam.text = "spam spam spam";
        const std::vector<uint8_t> bytes = net::encode(spam);
        for (int i = 0; i < 1500; ++i) bob.end->send(bytes);
        w.run(1500);
        ASSERT_TRUE(bob.lobby->phase() == net::ClientLobby::Phase::Rejected && bob.lobby->reject_reason() == net::RejectReason::BadRequest);
        RoomStatus s = w.status("CHAT-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.names[1].empty());
        ASSERT_TRUE(ann.lobby->phase() == net::ClientLobby::Phase::InRoom && cat.lobby->phase() == net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(ann.room_chat.size(), size_t{3 + net::kChatBurst - 1});                    // what was said before, and what was left of Bob's burst of 5 (he had said one line); not 1000, not 1500
        // chat while loading: Dan is on a slow link (400 ms each way), so the room waits for his Loaded for a second after the Start
        Client& dan = w.connect("Dan", "CHAT-1", 255, {400, 0});
        w.run(1500);
        ASSERT_TRUE(ann.lobby->is_leader());
        const size_t before = ann.room_chat.size();
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(100);                                                                          // the Start has gone out, Dan has not got it yet
        ASSERT_TRUE(w.status("CHAT-1").state == RoomState::Loading);
        ASSERT_TRUE(cat.lobby->chat("loading, loading"));
        w.run(200);
        ASSERT_EQ(ann.room_chat.size(), before + 1);
        ASSERT_EQ(ann.room_chat.back().text, std::string("loading, loading"));
        ASSERT_EQ(ann.room_chat.back().name, std::string("Cat"));
        w.run(3000);
        ASSERT_TRUE(w.status("CHAT-1").state == RoomState::Running);
        ASSERT_TRUE(!dan.room_chat.empty() && dan.room_chat.back().text == "loading, loading" && dan.room_chat.back().name == "Cat");      // Dan, on his slow link, heard it too (it came behind his Start)
        // the lines are still there when the match has begun (the application starts the match's chat log with them)
        ASSERT_TRUE(ann.lobby->chat_log().back().text == "loading, loading");
        ASSERT_EQ(ann.lobby->chat_log().size(), size_t{3 + net::kChatBurst - 1 + 1});        // (every line of the room is kept: the three, what the flood got through, the one of the loading room)
        // and the chat of the match is the match's: it works as ever
        ASSERT_TRUE(ann.session != nullptr && cat.session != nullptr);
        ASSERT_TRUE(ann.session->chat("in the match", false));
        w.run(500);
        bool heard = false;
        for (const net::ChatMsg& m : cat.chats) heard = heard || (m.text == "in the match" && m.sender == ann.lobby->my_seat());
        ASSERT_TRUE(heard);
    } TEST_END();

    TEST_CASE("S3.69 The Door Of Protocol 11: A Hello Of Protocol 10 Is Answered VersionMismatch (The Layout Is The Same, The Messages Are Not: A One-Byte StartRequest Of Protocol 10 Is Garbage Now); A Leader's Old One-Byte Request Costs A Violation Each Time, Eight Throw It Out") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("V11-1", 4), w.now).ok);
        {
            auto ends = w.net.connect({20, 10});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            net::HelloMsg hello;
            hello.version = 10;
            hello.name = "Old";
            hello.room = "V11-1";
            ends.second->send(net::encode(hello));
            w.run(300);
            ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::VersionMismatch));
        }
        ASSERT_EQ(w.status("V11-1").joined, 0);
        Client& ann = w.connect("Ann", "V11-1");
        w.run(500);
        ASSERT_TRUE(ann.lobby->is_leader());
        for (int i = 0; i < 7; ++i) ann.end->send({static_cast<uint8_t>(net::MsgType::StartRequest)});       // protocol 10's request: one byte
        w.run(500);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);                                       // seven are not enough
        ann.end->send({static_cast<uint8_t>(net::MsgType::StartRequest)});
        w.run(500);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::Rejected);                                     // the eighth
        ASSERT_EQ(w.status("V11-1").ignored_start_requests, 0u);                                              // (garbage is no request)
    } TEST_END();

    TEST_CASE("S3.70 Over Real Sockets: A Person Alone Starts A Room For Four With A Fill (Bots In The Three Other Seats), Two Players Chat In The Waiting Room First; The Match Is Played, Both Clients Stand At The Referee's State, The Status Lists The Bots") {
        RoomManager mgr{MapStore(maps_dir())};
        auto listener = net::TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        uint32_t now = 1000;
        ASSERT_TRUE(mgr.create_room(spec_of("SOCK-5", 4), now).ok);
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<net::TcpConnection>> links;
        const auto pump = [&]() {
            now += 10;
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        };
        for (const char* name : {"Ann", "Bob"}) {
            links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(links.back() != nullptr);
            clients.push_back(std::make_unique<Client>());
            clients.back()->name = name;
            clients.back()->room = "SOCK-5";
            clients.back()->record_hashes = true;
            clients.back()->start(links.back().get(), 77u);
            // Ann is in, and the leader, before Bob connects: two Hellos sent at once over two sockets can be read in either order (a Mac's loopback is handed over by a kernel thread), and the first one in is the leader
            for (int i = 0; i < 3000 && !clients[0]->lobby->is_leader(); ++i) pump();
        }
        for (int i = 0; i < 3000; ++i) {
            pump();
            RoomStatus s;
            mgr.status("SOCK-5", s, now);
            if (s.joined == 2 && clients[0]->lobby->room().slots[1].state == net::SlotState::Client && clients[1]->lobby->room().slots[0].state == net::SlotState::Client) break;
        }
        ASSERT_TRUE(clients[0]->lobby->chat("hello over TCP"));
        for (int i = 0; i < 3000 && clients[1]->room_chat.empty(); ++i) pump();                // (Bob answers after it has heard Ann: two lines sent at the same moment over two sockets arrive in either order)
        ASSERT_TRUE(clients[1]->lobby->chat("and back"));
        for (int i = 0; i < 400; ++i) pump();
        ASSERT_EQ(said(clients[0]->room_chat), (std::vector<std::string>{"0|Ann|hello over TCP", "1|Bob|and back"}));
        ASSERT_EQ(said(clients[1]->room_chat), said(clients[0]->room_chat));
        ASSERT_TRUE(clients[0]->lobby->request_start(net::FillLevel::Hard));
        RoomStatus s;
        for (int i = 0; i < 6000; ++i) {
            pump();
            mgr.status("SOCK-5", s, now);
            if (s.state == RoomState::Running && s.ticks > 400) break;
        }
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 400 && s.joined == 4 && s.bots.size() == 2);
        ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[1].seat == 3 && s.bots[0].level == "hard" && s.names[2] == bn("SOCK-5", 2, "Hard") && s.names[3] == bn("SOCK-5", 3, "Hard"));
        ASSERT_TRUE(clients[0]->sim.roster_mask() == 0x0F && clients[1]->sim.roster_mask() == 0x0F);
        bool compared = false;
        for (int i = 0; i < 400 && !compared; ++i) {
            pump();
            if (clients[0]->sim.current_tick() == clients[1]->sim.current_tick()) {
                ASSERT_TRUE(clients[0]->sim.state_hash() == clients[1]->sim.state_hash());
                compared = true;
            }
        }
        ASSERT_TRUE(compared);
        ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        ASSERT_TRUE(clients[0]->lobby->chat_log().size() == 2);                              // the waiting room's lines are still there
    } TEST_END();

    TEST_CASE("S3.71 Server CPU With Bots (Measured): Twelve Rooms Of One Person And Three Bots Each Cost The Server's Thread A Few Milliseconds A Second (Only mgr.update Is Timed, As The Thread's CPU Time: The Clients' Work Is Not The Server's, A Busy Machine Is Not Either), On TINY And On TREASURE, With Idle, Medium And Hard Bots; The Start Of Twelve Rooms Is Not A Stall (Each Room Analyses Its Map Once)") {
        // the clock of the measurement: it works, it does not run while the thread sleeps (a wall clock would read the whole 60 ms) and it runs while the thread works
        const double cpu_start = thread_cpu_ms();
        ASSERT_TRUE(cpu_start >= 0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        ASSERT_TRUE(thread_cpu_ms() - cpu_start < 30.0);
        volatile uint64_t spin = 0;
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (thread_cpu_ms() - cpu_start < 30.0 && std::chrono::steady_clock::now() < give_up) spin = spin + 1;
        static_cast<void>(spin);
        ASSERT_TRUE(thread_cpu_ms() - cpu_start >= 30.0);
        // ... and it moves in steps of under a millisecond: a clock that counts in ticks of 15.6 ms, or that posts what the thread used in lumps, cannot time a pass of a tenth of a millisecond
        // (the smallest of the next twenty steps that it takes while the thread works is less than a millisecond)
        double finest_step_ms = 1.0e9;
        int steps = 0;
        const auto give_up_steps = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (double before = thread_cpu_ms(); steps < 20 && std::chrono::steady_clock::now() < give_up_steps;) {
            const double now = thread_cpu_ms();
            if (now > before) {
                finest_step_ms = std::min(finest_step_ms, now - before);
                before = now;
                ++steps;
            }
        }
        ASSERT_MSG(steps == 20 && finest_step_ms < 1.0, "the thread clock cannot time a pass: its finest step is " + std::to_string(finest_step_ms) + " ms (" + std::to_string(steps) + " of 20 steps seen)");
        struct Result {
            double ms_per_second{0};
            double worst_pass_ms{0};
            double start_pass_ms{0};
        };
        // twelve rooms, each with one person (a client that asks for its seat) and bots in the other three seats: through the leader's fill (`fill` set) or the room's specification
        const auto measure = [&](const char* map, const char* kind, ai::Level level, bool via_fill, Result& out) {
            World w;
            std::vector<Client*> leaders;
            std::vector<std::string> codes;
            for (int r = 0; r < 12; ++r) {
                codes.push_back("CPU-" + std::to_string(r));
                RoomSpec spec = spec_of(codes.back(), 4, map);
                if (!via_fill) {
                    for (uint8_t seat = 1; seat < 4; ++seat) spec.bots.push_back(ai::BotSpec{seat, kind, level});
                }
                if (!w.mgr.create_room(spec, w.now).ok) return false;
                leaders.push_back(&w.connect("P" + std::to_string(r), codes.back(), 0));
            }
            const auto pass = [&](double& timed_ms) {
                w.now += 10;
                w.net.set_time(w.now);
                const double cpu0 = thread_cpu_ms();                                          // the thread's CPU time, not the wall clock: a loaded machine delays the thread, it does not make the pass cost more
                w.mgr.update(w.now);
                timed_ms = thread_cpu_ms() - cpu0;
                for (auto& c : w.clients) c->update(w.now, maps_dir());
            };
            double ms = 0;
            for (int i = 0; i < 100; ++i) pass(ms);
            if (via_fill) {
                for (Client* c : leaders) c->lobby->request_start(net::FillLevel::Medium);
            }
            out.start_pass_ms = 0;
            for (int i = 0; i < 600; ++i) {                                                  // six seconds: every room starts, loads, begins
                pass(ms);
                out.start_pass_ms = std::max(out.start_pass_ms, ms);
            }
            for (const std::string& code : codes) {
                if (w.status(code).state != RoomState::Running) return false;
            }
            double total = 0;
            const int passes = 3000;                                                         // 30 s of match in all twelve rooms
            for (int i = 0; i < passes; ++i) {
                pass(ms);
                total += ms;
                out.worst_pass_ms = std::max(out.worst_pass_ms, ms);
            }
            out.ms_per_second = total / 30.0;
            return true;
        };
        // A build with AddressSanitizer or UBSan runs the server 10 to 30 times slower than the program: its figures say nothing about the server, so such a build plays the two TINY rows (twelve
        // rooms with bots, memory and undefined behaviour are what it checks) and puts no bound on the time
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
        constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
        constexpr bool kSanitized = true;
#else
        constexpr bool kSanitized = false;
#endif
#else
        constexpr bool kSanitized = false;
#endif
        Result tiny_fill, tiny_idle, tiny_medium, tiny_hard, treasure_idle, treasure_hard;
        ASSERT_TRUE(measure("TINY.LVL", "standard", ai::Level::Medium, true, tiny_fill));
        ASSERT_TRUE(measure("TINY.LVL", "standard", ai::Level::Medium, false, tiny_medium));
        if (!kSanitized) {
            ASSERT_TRUE(measure("TINY.LVL", "idle", ai::Level::Medium, false, tiny_idle));
            ASSERT_TRUE(measure("TINY.LVL", "standard", ai::Level::Hard, false, tiny_hard));
            ASSERT_TRUE(measure("TREASURE.LVL", "idle", ai::Level::Medium, false, treasure_idle));
            ASSERT_TRUE(measure("TREASURE.LVL", "standard", ai::Level::Hard, false, treasure_hard));
        }
        const auto row = [](const char* what, const Result& r) {
            std::cout << "\n    " << std::left << std::setw(34) << what << std::right << std::fixed << std::setprecision(2) << std::setw(7) << r.ms_per_second << " ms per second of play for 12 rooms ("
                      << std::setw(5) << r.ms_per_second / 12.0 << " per room), worst pass " << std::setw(6) << r.worst_pass_ms << " ms, worst pass while starting " << std::setw(6) << r.start_pass_ms << " ms";
        };
        std::cout << "\n    [12 rooms, one person and three bots in each; only the server's own thread is timed" << (kSanitized ? "; a sanitizer build: no figure here is the server's, nothing is bounded" : "") << "]";
        row("TINY, the leader's fill, medium", tiny_fill);
        row("TINY, medium bots", tiny_medium);
        if (!kSanitized) {
            row("TINY, idle bots (the controller)", tiny_idle);
            row("TINY, hard bots", tiny_hard);
            row("TREASURE, idle bots", treasure_idle);
            row("TREASURE, hard bots", treasure_hard);
        }
        std::cout << "\n    ";
        if (kSanitized) return;
        for (const Result* r : {&tiny_fill, &tiny_idle, &tiny_medium, &tiny_hard, &treasure_idle, &treasure_hard}) {
            ASSERT_TRUE(r->ms_per_second < 100.0);                                           // a tenth of the thread's time (1000 ms of it pass every second): the rooms are far from it
            ASSERT_TRUE(r->worst_pass_ms < 250.0);                                           // no pass is a stall (the machine of a test run is shared and busy: the number is for the record, the bound is wide)
            ASSERT_TRUE(r->start_pass_ms < 250.0);                                           // twelve rooms that start in a few passes: a hitch, not a stall
        }
    } TEST_END();

    TEST_CASE("S3.72 Team Chat On A Real Server (The Referee's Relay): In A Match Of Three, A Line For The Team Reaches The Sender And Its Ally Only, Measured On The Link Of A Raw Client That Reads Every Byte Of It (It Gets No Team Line, Not Even One That Its Own Screen Would Have Dropped); A Line For All Reaches It; A Broken Alliance Stops The Delivery At Once; The Waiting Room Before The Match Has No Teams And Tells Everybody") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("TEAM-1", 4), w.now).ok);                      // (a room for four, started by its leader with the three who are there)
        Client& ann = w.connect("Ann", "TEAM-1");
        Client& bob = w.connect("Bob", "TEAM-1");
        Client& cat = w.connect("Cat", "TEAM-1");
        w.run(500);
        ASSERT_TRUE(ann.lobby->chat("before the match"));                                    // the waiting room: everybody, whatever the clients would call their teams
        w.run(300);
        for (Client* c : {&ann, &bob, &cat}) ASSERT_TRUE(!c->room_chat.empty() && c->room_chat.back().text == "before the match");
        ASSERT_TRUE(ann.lobby->is_leader() && ann.lobby->request_start());
        w.run(3000 + kPre);
        ASSERT_TRUE(w.status("TEAM-1").state == RoomState::Running);
        ASSERT_TRUE(ann.session && bob.session && cat.session);
        cat.freeze = true;                                                                   // Cat's session stops: nothing filters, nothing acknowledges; the test reads its link
        std::vector<net::ChatMsg> raw;
        const auto pump = [&](uint32_t ms) {
            for (uint32_t t = 0; t < ms; t += 10) {
                w.run(10);
                std::vector<uint8_t> msg;
                while (cat.end->poll(msg)) {
                    net::ChatMsg c;
                    if (net::peek_type(msg) == net::MsgType::Chat && net::decode(msg, c)) raw.push_back(c);
                }
            }
        };
        const uint8_t a = ann.lobby->my_seat();
        const uint8_t b = bob.lobby->my_seat();
        const auto command = [](sim::CommandType type, uint8_t issuer, uint8_t other) {
            sim::Command c;
            c.type = type;
            c.issuer = issuer;
            c.other_player = other;
            return c;
        };
        const auto has = [](const std::vector<net::ChatMsg>& v, const char* text) {
            for (const net::ChatMsg& c : v) {
                if (c.text == text) return true;
            }
            return false;
        };
        pump(500);
        ASSERT_TRUE(ann.session->submit(command(sim::CommandType::AllianceInvite, a, b)));
        pump(800);
        ASSERT_TRUE(bob.session->submit(command(sim::CommandType::AllianceAccept, b, a)));
        pump(800);
        ASSERT_TRUE(ann.sim.alliance_of(a) == b && bob.sim.alliance_of(b) == a);              // allies, by the clients' own engines (the referee's is the same state)
        ann.next_order_ms = bob.next_order_ms = UINT32_MAX;                                  // (no more random orders: the test's own commands only)
        raw.clear();
        ASSERT_TRUE(ann.session->chat("team talk", true));
        pump(500);
        ASSERT_TRUE(has(ann.chats, "team talk") && has(bob.chats, "team talk"));
        ASSERT_TRUE(raw.empty());                                                            // not one team line on the raw client's link
        ASSERT_TRUE(bob.session->chat("team reply", true));
        pump(500);
        ASSERT_TRUE(has(ann.chats, "team reply") && has(bob.chats, "team reply"));
        ASSERT_TRUE(raw.empty());
        ASSERT_TRUE(bob.session->chat("to everybody", false));                               // a line for all reaches the raw client
        pump(500);
        ASSERT_TRUE(has(ann.chats, "to everybody") && has(bob.chats, "to everybody"));
        ASSERT_TRUE(raw.size() == 1 && raw[0].text == "to everybody" && raw[0].sender == b && !raw[0].team);
        // a broken alliance: the next team line of the former ally does not reach the one who left the team
        ASSERT_TRUE(ann.session->submit(command(sim::CommandType::AllianceBreak, a, 255)));
        for (int i = 0; i < 300 && (ann.sim.alliance_of(a) != sim::ALLIANCE_NONE || bob.sim.alliance_of(b) != sim::ALLIANCE_NONE); ++i) pump(10);
        ASSERT_TRUE(ann.sim.alliance_of(a) == sim::ALLIANCE_NONE && bob.sim.alliance_of(b) == sim::ALLIANCE_NONE);
        const size_t ann_heard = ann.chats.size();
        ASSERT_TRUE(bob.session->chat("late team line", true));
        pump(500);
        ASSERT_TRUE(has(bob.chats, "late team line"));                                       // Bob hears itself
        ASSERT_EQ(ann.chats.size(), ann_heard);                                              // Ann nothing
        ASSERT_EQ(raw.size(), size_t{1});                                                    // and the raw client still only the line for all
    } TEST_END();

    TEST_CASE("S3.73 The Door Tells A Hello Of Another Protocol Before It Looks For The Room (A Hello Of Protocol 10, 11 (The Release Before The Match Clock Waited For The Start Dialog), 12 (The Release Before The Teams), 13 (The Release Before The Color Moves), 14 (The Release Before The Create Block) Or 16 For A Room That Does Not Exist Is VersionMismatch, Not NoSuchRoom; The Right Protocol Is NoSuchRoom); A Room Without Bots Builds No Bot Controller (The \"No Bot Code\" Rule), A Room With A Bot Seat Or A Fill Does; The Map Notice Of A Fill Waits For The Pause After A Cancelled Start To End; A Vote That No Person Can Cast (Everybody Who Is Left Is A Bot) Is No Vote In The Status JSON") {
        {   // the door's own check of the protocol, for a code that no room has
            World w;
            ASSERT_EQ(net::kProtocolVersion, uint16_t{21});                  // (16 was the protocol of v0.12.0: a Start, a Room and a Plan without the mode byte; 15 was the protocol of v0.11.0: a Hello without a client kind; 14 was the protocol of v0.10.0: a Hello without a platform byte and a create block; 13 was the protocol of v0.8.0 to v0.8.2: its leader cannot move a color; 11 was the protocol of v0.1.0 and v0.1.1: a client of it counts its dialog in simulation ticks, which a host that seals its first turn 5 s late would block for 100 ticks of the running match; 12 was the protocol of v0.2.0 to v0.4.0: its leader sends the one-level StartRequest and its Start has no team bytes)
            for (const uint16_t version : {uint16_t{10}, uint16_t{11}, uint16_t{12}, uint16_t{13}, uint16_t{14}, uint16_t{15}, uint16_t{16}, uint16_t{17}, uint16_t{18}, uint16_t{19}, uint16_t{20}, uint16_t{22}, uint16_t{1}, uint16_t{0}}) {      // (16: v0.12.0, whose Hello is the Hello of 17 byte for byte: the number alone refuses it; 18: v0.12.9, the Hello of 19 byte for byte; 20: v0.12.41, the Hello of 21 byte for byte; 22: a client from the future)
                auto ends = w.net.connect({20, 10});
                w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
                net::HelloMsg hello;
                hello.version = version;
                hello.name = "Old";
                hello.room = "NO-SUCH-ROOM";
                ends.second->send(net::encode(hello));
                w.run(300);
                ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::VersionMismatch));
            }
            {   // the very bytes that v0.10.0 sent (protocol 14 has no platform byte and no create block): the door reads the version and the name, answers, and never reads the rest
                auto old_ends = w.net.connect({20, 10});
                w.mgr.add_connection(std::make_unique<Borrowed>(old_ends.first), "127.0.0.1", w.now);
                net::HelloMsg old;
                old.version = 14;
                old.name = "Old";
                old.room = "NO-SUCH-ROOM";
                std::vector<uint8_t> bytes = net::encode(old);
                ASSERT_TRUE(!bytes.empty());
                bytes.pop_back();                                                                // (the client kind ends a Hello that has no block, and the platform byte before it: what is left is the layout of 14)
                bytes.pop_back();
                net::HelloMsg again;
                ASSERT_FALSE(net::decode(bytes, again));                                          // (a protocol 17 reader takes it for no Hello at all)
                old_ends.second->send(bytes);
                w.run(300);
                ASSERT_EQ(reject_on(old_ends.second), static_cast<int>(net::RejectReason::VersionMismatch));
            }
            {   // the very bytes that v0.11.0 sent (protocol 15 has the platform byte, and no client kind): the same answer
                auto old_ends = w.net.connect({20, 10});
                w.mgr.add_connection(std::make_unique<Borrowed>(old_ends.first), "127.0.0.1", w.now);
                net::HelloMsg old;
                old.version = 15;
                old.name = "Old";
                old.room = "NO-SUCH-ROOM";
                std::vector<uint8_t> bytes = net::encode(old);
                bytes.pop_back();                                                                // (the client kind ends a Hello that has no block: what is left is the layout of 15)
                net::HelloMsg again;
                ASSERT_FALSE(net::decode(bytes, again));
                old_ends.second->send(bytes);
                w.run(300);
                ASSERT_EQ(reject_on(old_ends.second), static_cast<int>(net::RejectReason::VersionMismatch));
            }
            auto ends = w.net.connect({20, 10});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            net::HelloMsg hello;
            hello.name = "Right";
            hello.room = "NO-SUCH-ROOM";
            ends.second->send(net::encode(hello));
            w.run(300);
            ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::NoSuchRoom));       // (the protocol is right: the room is what is wrong)
        }
        {   // no bot code in a room without bots
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("NOBOT-1", 2), w.now).ok);
            Client& ann = w.connect("Ann", "NOBOT-1");
            w.connect("Bob", "NOBOT-1");
            w.run(500);
            ASSERT_FALSE(w.status("NOBOT-1").bot_controller);                                           // (waiting: nothing yet)
            w.run(5000);
            ASSERT_TRUE(w.status("NOBOT-1").state == RoomState::Running);
            ASSERT_FALSE(w.status("NOBOT-1").bot_controller);                                           // a room that runs with two people has no controller
            ASSERT_TRUE(ann.sim.roster_mask() == 0x03);
            // a leader's START without a fill: the same
            ASSERT_TRUE(w.mgr.create_room(spec_of("NOBOT-2", 4), w.now).ok);
            Client& cat = w.connect("Cat", "NOBOT-2");
            w.run(300);                                                                                // (Cat first: the Hellos of two links that are made together arrive in either order, whatever the link's jitter says, and the first to arrive leads)
            w.connect("Dan", "NOBOT-2");
            w.run(500);
            ASSERT_TRUE(cat.lobby->request_start(net::FillLevel::None));
            w.run(3000);
            ASSERT_TRUE(w.status("NOBOT-2").state == RoomState::Running && w.status("NOBOT-2").bots.empty());
            ASSERT_FALSE(w.status("NOBOT-2").bot_controller);
            // the fill builds one (and the room's own bot spec does too)
            ASSERT_TRUE(w.mgr.create_room(spec_of("BOT-F", 3), w.now).ok);
            Client& eve = w.connect("Eve", "BOT-F");
            w.run(500);
            ASSERT_TRUE(eve.lobby->request_start(net::FillLevel::Easy));
            w.run(3000);
            ASSERT_TRUE(w.status("BOT-F").state == RoomState::Running && w.status("BOT-F").bots.size() == 2);
            ASSERT_TRUE(w.status("BOT-F").bot_controller);
            RoomSpec own = spec_of("BOT-S", 2);
            own.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(own, w.now).ok);
            w.connect("Fay", "BOT-S");
            w.run(6000);
            ASSERT_TRUE(w.status("BOT-S").state == RoomState::Running && w.status("BOT-S").bot_controller);
        }
        {   // the map notice waits for the pause after a cancelled start (the request that falls into the pause is lost, and the room says nothing about it)
            const std::string dir = temp_dir_for("fill_pause");
            {
                std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                assets::LevelData tiny;
                ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
                const assets::AnthillSpawn* green = nullptr;
                for (const auto& sp : tiny.anthill_spawns) {
                    if (sp.tile_id == 154) green = &sp;
                }
                ASSERT_TRUE(green != nullptr);
                const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
                size_t at = bytes.size();
                for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                    if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
                }
                ASSERT_TRUE(at < bytes.size());
                bytes[at + 2] = 200;
                bytes[at + 3] = 0;
                std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("PAUSE-1", 4, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "PAUSE-1", 1);              // red and blue are playable together (S3.64): the room starts with no fill
            Client& bob = w.connect("Bob", "PAUSE-1", 2);
            bob.fail_load = true;                                       // Bob's copy is no good: the start is cancelled and the room waits two seconds
            const auto run_in = [&](uint32_t ms) {
                for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
                    w.now += 10;
                    w.net.set_time(w.now);
                    w.mgr.update(w.now);
                    for (auto& c : w.clients) c->update(w.now, dir);
                }
            };
            run_in(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));
            run_in(300);                                                // the cancel has come: the pause (2 s) has begun
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);
            bob.fail_load = false;
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));      // the filled roster has green, which this map cannot play: but we are in the pause
            run_in(600);
            ASSERT_TRUE(ann.room_chat.empty());                         // the room says nothing during the pause (the request is lost, START again)
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting && w.status("PAUSE-1").bots.empty());
            run_in(2500);                                               // the pause is over
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
            run_in(500);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillMap}));       // now it is answered
            std::error_code ignore;
            fs::remove_all(dir, ignore);
        }
        {   // a vote that nobody can cast: Ann, the only person, is cut; the two bots are not voters; the status says "vote: null" (and the C++ status has the seat and no voter)
            RWorld w;
            RoomSpec spec = held_spec("BV-9", 3);
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Easy}, ai::BotSpec{2, "standard", ai::Level::Easy}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "BV-9", 0);
            w.run(6000);
            ASSERT_TRUE(w.status("BV-9").state == RoomState::Running);
            const uint8_t ann_seat = ann.lobby->my_seat();
            ann.reconnects = false;
            w.cut(ann);
            w.run(1500);
            ASSERT_TRUE(w.status("BV-9").paused);
            w.run(32000);                                               // 30 s away in all: the vote is open for the people who are connected: there are none
            RoomStatus s = w.status("BV-9");
            ASSERT_TRUE(s.paused && s.vote_seat == ann_seat && s.voters == 0);
            ctl::JsonValue v = status_to_json(s);
            ASSERT_TRUE(v.get("paused").as_bool_or(false) && v.get("absent").size() == 1);
            ASSERT_TRUE(v.get("vote").is_null());                       // nobody can vote: no vote to show (the status of a vote with voters 0 used to be {"seat": 0, "continue": 0, "voters": 0})
        }
    } TEST_END();

    TEST_CASE("S3.76 The Opening Of A Server's Room: The First Turn Is Sealed 5000 ms After The Match Began (Protocol 12: Nobody's Clock Runs Behind The Dialog, Nothing Is Sealed, Run Or Late Before It); A Room That The Leader's START Fills With Hard Bots Has No Command Of A Bot Seat In Turn 0 And Before A Bot's First Look (Tick 1 + Seat) And Its Reaction Time Are Over, And The Bots Do Play After It") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("HOLD-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "HOLD-1", 2);                      // the person sits in seat 2: the bots take 0, 1 and 3
        ann.record_commands = true;
        w.run(500);
        ASSERT_TRUE(ann.lobby->is_leader());
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Hard));
        uint32_t begin = 0;                                                  // the time of the room's begin_match: the first pass after which the room runs (one pass is one 10 ms step here)
        for (int i = 0; i < 400 && begin == 0; ++i) {
            w.run(10);
            if (w.status("HOLD-1").state == RoomState::Running) begin = w.now;
        }
        ASSERT_TRUE(begin != 0);
        RoomStatus s = w.status("HOLD-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.bots.size() == 3 && s.bots[0].level == "hard" && s.bot_controller);
        ASSERT_TRUE(s.turns == 0 && s.ticks == 0);
        // the pre-start: for 5 s nothing is sealed and nothing runs (the referee's engine, the person's engine), nobody is announced as lagging, no pause, no command of anybody
        uint32_t first_turn_ms = 0;
        while (w.now - begin < 20000 && first_turn_ms == 0) {
            w.run(10);
            s = w.status("HOLD-1");
            if (s.turns > 0) {
                first_turn_ms = w.now - begin;
            } else {
                ASSERT_TRUE(s.ticks == 0 && !s.paused && s.state == RoomState::Running);
                ASSERT_TRUE(ann.sim.current_tick() == 0 && ann.saw.empty());
                if (ann.session == nullptr) continue;                         // (the Begin is on its way to the client: its session starts a link delay after the room's)
                ASSERT_TRUE(!ann.session->catching_up() && ann.session->lagging_seat() == 255 && ann.session->self_lag_behind_ms() == 0);
                ASSERT_TRUE(ann.session->runner().stalled_ms() == 0 && ann.session->runner().buffer_turns() == 1 && !ann.session->runner().stalled());   // not a stall of the link, no growth of the jitter buffer
            }
        }
        ASSERT_TRUE(first_turn_ms >= net::kMatchStartDelayMs && first_turn_ms <= net::kMatchStartDelayMs + net::kTurnMs);   // the first turn: 5000 ms after the match began (one turn, 50 ms, at the most more)
        w.run(25000);                                                       // some 500 ticks
        ASSERT_TRUE(ann.sim.current_tick() > 400);
        ASSERT_TRUE(ann.session->runner().buffer_turns() == 1 && !ann.session->catching_up());     // (the pre-start left the buffer as it was)
        size_t per_seat[4] = {0, 0, 0, 0};
        uint64_t first = ~0ull;
        for (const auto& e : ann.saw) {
            if (e.second.issuer == 2) continue;                              // the person's own (this test client orders from the first turn on: it knows no dialog)
            ASSERT_TRUE(e.second.issuer == 0 || e.second.issuer == 1 || e.second.issuer == 3);
            ASSERT_TRUE(e.first >= 1u);                                      // no turn 0 carries a command of a bot (the first tick that a person's engine runs is 1)
            ++per_seat[e.second.issuer];
            first = std::min(first, e.first);
        }
        ASSERT_TRUE(per_seat[0] >= 1 && per_seat[1] >= 1 && per_seat[3] >= 1);        // every bot played (a harvesting bot needs well under a command per second)
        ASSERT_TRUE(first >= 1u + 6u);                                       // a Hard bot looks on tick 1 + seat and its first order leaves 6 to 10 ticks later (then it is sealed into a turn)
        ASSERT_FALSE(ann.session->desynced());
    } TEST_END();
    TEST_CASE("S3.75 What The Room's Limit And Status Say About The Start (Protocol 12): run_ms Counts From The Moment The Match Began, The 5 s Before The First Turn Included (A Room With A Limit Of 8 s Fails 8 s After It Began, Not 13); A Room With A Bot Of Its Specification Opens Its Bots Like The Product Does (The Status Says The Controller's Hold Is kStartHoldTicks), A Room Without Bots Has No Controller") {
        {
            World w;
            RoomSpec spec = spec_of("RUNLIM-1", 2);
            spec.run_ms = 8000;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "RUNLIM-1");
            w.connect("Bob", "RUNLIM-1");
            uint32_t begin = 0;
            for (int i = 0; i < 400 && begin == 0; ++i) {
                w.run(10);
                if (w.status("RUNLIM-1").state == RoomState::Running) begin = w.now;
            }
            ASSERT_TRUE(begin != 0);
            uint32_t failed_after = 0;
            while (w.now - begin < 20000 && failed_after == 0) {
                w.run(10);
                const RoomStatus s = w.status("RUNLIM-1");
                if (s.state == RoomState::Failed) {
                    failed_after = w.now - begin;
                    ASSERT_TRUE(s.reason.find("longer") != std::string::npos);
                    ASSERT_TRUE(s.turns > 0 && s.turns < 100);                                       // (3 s of turns: the first one was sealed 5 s into the 8)
                }
            }
            ASSERT_TRUE(failed_after >= 8000 && failed_after <= 8000 + net::kTurnMs);             // the limit counts from the begin of the match, the 5 s of the dialog in it
        }
        {
            World w;
            RoomSpec own = spec_of("HOLDSPEC-1", 2);
            own.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(own, w.now).ok);
            w.connect("Fay", "HOLDSPEC-1");
            ASSERT_TRUE(w.mgr.create_room(spec_of("PLAIN-1", 2), w.now).ok);
            w.connect("Gus", "PLAIN-1");
            w.connect("Hal", "PLAIN-1");
            w.run(1500);
            const RoomStatus with_bot = w.status("HOLDSPEC-1");
            const RoomStatus plain = w.status("PLAIN-1");
            ASSERT_TRUE(with_bot.state == RoomState::Running && with_bot.bot_controller && with_bot.bot_start_hold == ai::kStartHoldTicks);
            ASSERT_TRUE(plain.state == RoomState::Running && !plain.bot_controller && plain.bot_start_hold == 0u);
        }
    } TEST_END();
    TEST_CASE("S3.77 Protocol 12, A Command That Reaches A Server's Room Before Its First Turn Is Sealed Is Discarded (The Product Path: Room::begin_match Sets The Start Delay): A Raw Connection That Writes Seventy Orders At 100 ms And One At 4,500 ms Of The Dialog Has Them In No Turn On Any Machine And Stays In The Room; Its Order After The First Seal And An Honest Client's Order After Its First Tick Are Applied On Both Machines At The Same Tick") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("EARLY-1", 2), w.now).ok);
        Client& ann = w.connect("Ann", "EARLY-1");                       // honest: it orders after its first tick
        Client& bob = w.connect("Bob", "EARLY-1");                       // a raw connection in the dialog (a modified client): it writes CommandMsg itself
        ann.record_commands = true;
        bob.record_commands = true;
        ann.next_order_ms = bob.next_order_ms = 0xFFFFFFFFu;             // (the rig's own orders, one every 700 ms from its first moment because it knows no dialog, are off: this test sends its own, at chosen times)
        uint32_t begin = 0;
        for (int i = 0; i < 400 && begin == 0; ++i) {
            w.run(10);
            if (w.status("EARLY-1").state == RoomState::Running) begin = w.now;
        }
        ASSERT_TRUE(begin != 0);
        ASSERT_TRUE(ann.sim.get_world_state().ants.size() > 0);
        const auto order_of = [&](Client& who, int16_t x) {
            sim::Command c;
            c.type = sim::CommandType::GroupMove;
            c.issuer = who.lobby->my_seat();
            c.tile_x = x;
            c.tile_y = 12;
            for (const auto& a : who.sim.get_world_state().ants) {
                if (a.player_id == c.issuer && c.ants.size() < 3) c.ants.push_back(a.id);
            }
            return c;
        };
        const auto step_to = [&](uint32_t ms_after_begin) {
            while (w.now - begin < ms_after_begin) w.run(10);
        };
        step_to(100);
        ASSERT_TRUE(bob.session != nullptr && !order_of(bob, 10).ants.empty());
        for (int i = 0; i < 70; ++i) bob.end->send(net::encode(net::CommandMsg{order_of(bob, static_cast<int16_t>(10 + i % 5))}));   // a scripted opening, seventy orders
        step_to(4500);
        bob.end->send(net::encode(net::CommandMsg{order_of(bob, 30)}));                                                              // one more, half a second before the first turn
        step_to(4700);
        RoomStatus s = w.status("EARLY-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns == 0 && s.ticks == 0);      // the dialog is still up: nothing sealed
        uint32_t sealed_at = 0;
        for (int i = 0; i < 100 && sealed_at == 0; ++i) {
            w.run(10);
            if (w.status("EARLY-1").turns > 0) sealed_at = w.now - begin;
        }
        ASSERT_TRUE(sealed_at >= net::kMatchStartDelayMs && sealed_at <= net::kMatchStartDelayMs + net::kTurnMs);   // the first turn is 5000 ms after the match began
        // after the first seal: the raw connection's order is an order like any other, and so is an honest client's, sent after its own first tick
        step_to(sealed_at + 200);
        bob.end->send(net::encode(net::CommandMsg{order_of(bob, 31)}));
        bool ann_sent = false;
        for (int i = 0; i < 600 && !ann_sent; ++i) {
            if (ann.sim.current_tick() >= 1) ann_sent = ann.session->submit(order_of(ann, 20));
            if (!ann_sent) w.run(10);
        }
        ASSERT_TRUE(ann_sent);
        w.run(3000);
        ASSERT_TRUE(ann.saw.size() == 2 && bob.saw.size() == 2);                         // Bob's late order and Ann's, and none of Bob's seventy-one orders of the dialog (their tiles are 10 - 14 and 30)
        for (size_t i = 0; i < 2; ++i) {
            ASSERT_TRUE(ann.saw[i].second.tile_x == 31 || ann.saw[i].second.tile_x == 20);
            ASSERT_TRUE(ann.saw[i].first >= 1u);                                         // not turn 0 (no tick had run)
            ASSERT_TRUE(ann.saw[i].first == bob.saw[i].first && ann.saw[i].second.issuer == bob.saw[i].second.issuer && ann.saw[i].second.tile_x == bob.saw[i].second.tile_x);   // the same tick on both machines
        }
        ASSERT_TRUE(!ann.lost && !bob.lost && !ann.session->lost() && !bob.session->lost());      // a discarded command is no violation: Bob stays in the room
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        s = w.status("EARLY-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 50);                      // (some 3 s of play after the first turn)
    } TEST_END();
}


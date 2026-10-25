// Tests of the lock-step network core: the wire protocol, the in-memory network, the sequencer, the runner, whole matches, a dropped peer and host migration
#include "lockstep_fixtures.hpp"

void run_protocol_tests() {
    TEST_CASE("N2.1 Protocol: Every Message Round-Trips And Trailing Or Missing Bytes Are Rejected") {
        ASSERT_EQ(kProtocolVersion, 21);                                 // 7: the room leader's START; 8: turns of 50 ms, one tick each, the adaptive buffer and the Lag message (type 25); 9: the community-map rules; 10: keys, presence, votes and the catch-up stream (types 26 - 30); 11: the leader's START carries a fill level, chat in the waiting room; 12: the match clock waits for the start dialog (the first turn is sealed kMatchStartDelayMs after the match began, a dialog ends with the first turn that executes: no message changed); 13: a level for each seat of the fill and the teams (StartRequest is seven bytes, Start ends with the two team bytes); 14: the leader moves the colors (SeatMove, type 31, three bytes); 15: a room is made by its first Hello's create block, the platform byte (Hello, Room slots, Start), the room's teams and rules in the Room message; 16: lobby rooms (the Hello's client kind, the create block's kCreateLobby, the Welcome's kWelcomeCreated, the Room message's plan and games, the PlanMsg, type 32; 17: the game mode, one byte at the end of the Start, Room and Plan messages)
        ASSERT_TRUE(kTurnMs == 50 && kTicksPerTurn == 1 && kTurnsPerSecond == 20 && kHashEveryTurns == 20);     // a hash every 20 ticks, one second, as before
        ASSERT_TRUE(turns_for_ms(0) == 0 && turns_for_ms(1) == 1 && turns_for_ms(50) == 1 && turns_for_ms(51) == 2 && turns_for_ms(3000) == 60);
        ASSERT_EQ(static_cast<int>(MsgType::Last), static_cast<int>(MsgType::Remove));
        LagMsg lag;
        lag.seat = 2;
        lag.behind_ms = 12345;
        LagMsg lag2;
        ASSERT_TRUE(decode(encode(lag), lag2) && lag2.seat == 2 && lag2.behind_ms == 12345);
        ASSERT_EQ(peek_type(encode(lag)), MsgType::Lag);
        ASSERT_EQ(encode(lag).size(), size_t{6});                        // type, seat, u32 behind_ms
        lag.behind_ms = 0;                                               // "it is not lagging any more"
        ASSERT_TRUE(decode(encode(lag), lag2) && lag2.behind_ms == 0);
        HelloMsg hello;
        hello.name = "Queen Ant";
        HelloMsg h2;
        ASSERT_TRUE(decode(encode(hello), h2) && h2.name == "Queen Ant" && h2.version == kProtocolVersion);
        WelcomeMsg w;
        w.player = 2;
        w.players = 4;
        WelcomeMsg w2;
        ASSERT_TRUE(decode(encode(w), w2) && w2.player == 2 && w2.players == 4);
        RejectMsg r;
        r.reason = RejectReason::Full;
        RejectMsg r2;
        ASSERT_TRUE(decode(encode(r), r2) && r2.reason == RejectReason::Full);
        CommandMsg cm;
        cm.command = cmd(CommandType::GroupAttack, 3, 255, 12, 34, {5, 6, 7});
        CommandMsg cm2;
        ASSERT_TRUE(decode(encode(cm), cm2) && cm2.command == cm.command);
        TurnMsg t;
        t.turn = 123456;
        t.commands = {cmd(CommandType::GroupMove, 0, 255, 1, 2, {1}), cmd(CommandType::Hatch, 1), cmd(CommandType::AllianceInvite, 2, 3)};
        TurnMsg t2;
        ASSERT_TRUE(decode(encode(t), t2) && t2.turn == 123456 && t2.commands == t.commands);
        TurnMsg empty;
        empty.turn = 7;
        ASSERT_TRUE(decode(encode(empty), t2) && t2.turn == 7 && t2.commands.empty());
        AckMsg a;
        a.turn = 99;
        AckMsg a2;
        ASSERT_TRUE(decode(encode(a), a2) && a2.turn == 99);
        HashMsg hm;
        hm.turn = 19;
        hm.hash = {1, 2, 3, 4, 5, 6, 7, 8};
        HashMsg hm2;
        ASSERT_TRUE(decode(encode(hm), hm2) && hm2.turn == 19 && hm2.hash.total == 1 && hm2.hash.droppers == 8);
        DesyncMsg d;
        d.turn = 29;
        d.player = 2;
        d.host = {1, 1, 1, 1, 1, 1, 1, 1};
        d.peer = {2, 2, 2, 2, 2, 2, 2, 2};
        DesyncMsg d2;
        ASSERT_TRUE(decode(encode(d), d2) && d2.player == 2 && d2.peer.total == 2);
        ChatMsg c;
        c.sender = 1;
        c.team = true;
        c.text = "Attack the north hill!";
        ChatMsg c2;
        ASSERT_TRUE(decode(encode(c), c2) && c2.team && c2.text == c.text && c2.sender == 1);
        PingMsg p;
        p.nonce = 5;
        p.sent_ms = 1000;
        PingMsg p2;
        ASSERT_TRUE(decode_ping(encode_ping(p).data(), 9, p2) && p2.nonce == 5 && p2.sent_ms == 1000);
        ASSERT_EQ(peek_type(encode_pong(p)), MsgType::Pong);
        // the messages of host migration and the mesh
        ProposeMsg pr;
        pr.epoch = 3;
        pr.candidate = 2;
        ProposeMsg pr2;
        ASSERT_TRUE(decode(encode(pr), pr2) && pr2.epoch == 3 && pr2.candidate == 2);
        AcceptMsg ac;
        ac.epoch = 1;
        ac.next_receive = 500;
        ac.next_execute = 498;
        AcceptMsg ac2;
        ASSERT_TRUE(decode(encode(ac), ac2) && ac2.epoch == 1 && ac2.next_receive == 500 && ac2.next_execute == 498);
        RefuseMsg rf;
        rf.epoch = 2;
        rf.lowest = 1;
        RefuseMsg rf2;
        ASSERT_TRUE(decode(encode(rf), rf2) && rf2.epoch == 2 && rf2.lowest == 1);
        ResumeMsg rs;
        rs.epoch = 1;
        rs.host = 2;
        rs.resume_turn = 4242;
        ResumeMsg rs2;
        ASSERT_TRUE(decode(encode(rs), rs2) && rs2.epoch == 1 && rs2.host == 2 && rs2.resume_turn == 4242);
        RequestMsg rq;
        rq.from_turn = 77;
        RequestMsg rq2;
        ASSERT_TRUE(decode(encode(rq), rq2) && rq2.from_turn == 77);
        PeerHelloMsg ph;
        ph.seat = 3;
        PeerHelloMsg ph2;
        ASSERT_TRUE(decode(encode(ph), ph2) && ph2.seat == 3);
        // the port a guest accepts the other guests on travels with its Hello; the host hands every guest the others' endpoints in Start
        HelloMsg hello_port;
        hello_port.name = "Ann";
        hello_port.listen_port = 41234;
        HelloMsg hello_port2;
        ASSERT_TRUE(decode(encode(hello_port), hello_port2) && hello_port2.listen_port == 41234 && hello_port2.name == "Ann");
        StartMsg start;
        start.map_name = "TREASURE.LVL";
        start.roster = 0x0D;
        start.endpoints[2] = Endpoint{"192.0.2.22", 5001};
        start.endpoints[3] = Endpoint{"fe80::1%en0", 5002};
        StartMsg start2;
        ASSERT_TRUE(decode(encode(start), start2) && start2.endpoints[2] == start.endpoints[2] && start2.endpoints[3] == start.endpoints[3] &&
                    start2.endpoints[0].address.empty() && start2.endpoints[1].port == 0);
        // protocol 7: the room names its leader, and the leader's request to start is a message of its own
        RoomMsg led = server_room_of(3);
        RoomMsg led2;
        ASSERT_TRUE(decode(encode(led), led2) && led2.leader == 3 && led2.you == 1 && led2.map_name == "SMALL.LVL" && led2.slots[3].name == "Cat" && led2.slots[2].state == SlotState::Empty);
        StartRequestMsg sr;
        ASSERT_TRUE(decode(encode(sr), sr) && peek_type(encode(sr)) == MsgType::StartRequest);
        // protocol 10: a Hello and a Welcome that carry a key (the five new messages are N2.40's)
        HelloMsg keyed;
        keyed.name = "Ann";
        keyed.room = "ROOM-7";
        keyed.key = key_with(1);
        keyed.have_turns = 1234;
        HelloMsg keyed2;
        ASSERT_TRUE(decode(encode(keyed), keyed2) && keyed2.key == key_with(1) && keyed2.have_turns == 1234 && keyed2.room == "ROOM-7");
        WelcomeMsg welcome_key;
        welcome_key.player = 3;
        welcome_key.players = 4;
        welcome_key.key = key_with(2);
        welcome_key.flags = kWelcomeRejoin;
        WelcomeMsg welcome_key2;
        ASSERT_TRUE(decode(encode(welcome_key), welcome_key2) && welcome_key2.key == key_with(2) && welcome_key2.flags == kWelcomeRejoin && welcome_key2.player == 3);
        // every valid message is rejected with a byte too many and with any byte missing
        const std::vector<std::vector<uint8_t>> all = {encode(hello), encode(w),  encode(r),  encode(cm), encode(t),  encode(a),  encode(hm),
                                                       encode(d),     encode(c),  encode(pr), encode(ac), encode(rf), encode(rs), encode(rq),
                                                       encode(ph),    encode(hello_port), encode(start), encode(lag), encode(led), encode(sr), encode(server_room_of(255)),
                                                       encode(keyed), encode(welcome_key)};
        for (const auto& m : all) {
            std::vector<uint8_t> longer = m;
            longer.push_back(0);
            for (size_t cut = 0; cut < m.size(); ++cut) {
                std::vector<uint8_t> shorter(m.begin(), m.begin() + static_cast<std::ptrdiff_t>(cut));
                ASSERT_FALSE(any_decodes(shorter));
            }
            ASSERT_FALSE(any_decodes(longer));
        }
    } TEST_END();

    TEST_CASE("N2.2 Protocol: Out-Of-Range Fields Are Rejected (players, counts, reasons, names, chat)") {
        WelcomeMsg w;
        auto bytes = encode(WelcomeMsg{2, 4});
        bytes[1] = 4;                                  // player 4 does not exist
        ASSERT_FALSE(decode(bytes, w));
        bytes = encode(WelcomeMsg{2, 4});
        bytes[2] = 1;                                  // a room needs two players at least
        ASSERT_FALSE(decode(bytes, w));
        bytes[2] = 5;
        ASSERT_FALSE(decode(bytes, w));
        RejectMsg r;
        bytes = encode(RejectMsg{RejectReason::Full});
        bytes[1] = 0;
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 10;                                                   // the reasons are 1 .. 9 (6 = NoSuchRoom, protocol 6; 7 - 9 = Dropped, RejoinFailed, Superseded, protocol 10)
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 255;
        ASSERT_FALSE(decode(bytes, r));
        bytes[1] = 6;
        ASSERT_TRUE(decode(bytes, r) && r.reason == RejectReason::NoSuchRoom);
        bytes[1] = 9;
        ASSERT_TRUE(decode(bytes, r) && r.reason == RejectReason::Superseded);
        DesyncMsg d;
        DesyncMsg good;
        good.player = 1;
        bytes = encode(good);
        bytes[5] = 4;                                  // the peer's player number
        ASSERT_FALSE(decode(bytes, d));
        // a turn that claims more commands than fit, or than the limit
        TurnMsg t;
        t.turn = 1;
        bytes = encode(t);
        bytes[5] = 0xFF;                               // count = 65280 + 0 ...
        bytes[6] = 0xFF;
        ASSERT_FALSE(decode(bytes, t));
        bytes = encode(t);
        bytes[5] = 1;                                  // one command promised, none present
        ASSERT_FALSE(decode(bytes, t));
        // chat: non-printable bytes and an overlong text
        ChatMsg c;
        bytes = encode(ChatMsg{1, false, "hi"});
        bytes[4] = 1;                                  // control character in the text
        ASSERT_FALSE(decode(bytes, c));
        bytes = encode(ChatMsg{1, false, "hi"});
        bytes[2] = 2;                                  // team flag must be 0 or 1
        ASSERT_FALSE(decode(bytes, c));
        ChatMsg longtext;
        longtext.text = std::string(300, 'x');
        ChatMsg back;
        ASSERT_TRUE(decode(encode(longtext), back) && back.text.size() == kMaxChatChars);   // clipped by the encoder
        // a name with a control character
        HelloMsg h;
        bytes = encode(hello_of(kProtocolVersion, "Bob"));
        bytes[4] = 7;
        ASSERT_FALSE(decode(bytes, h));
        // host migration: seats out of range, an Accept that executed more than it received, endpoints that are no addresses
        ProposeMsg pr;
        bytes = encode(ProposeMsg{1, 2});
        bytes[2] = 4;
        ASSERT_FALSE(decode(bytes, pr));
        RefuseMsg rf;
        bytes = encode(RefuseMsg{1, 2});
        bytes[2] = 4;
        ASSERT_FALSE(decode(bytes, rf));
        ResumeMsg rs;
        bytes = encode(ResumeMsg{1, 2, 10});
        bytes[2] = 4;
        ASSERT_FALSE(decode(bytes, rs));
        PeerHelloMsg ph;
        bytes = encode(PeerHelloMsg{2});
        bytes[1] = 4;
        ASSERT_FALSE(decode(bytes, ph));
        AcceptMsg ac;
        AcceptMsg bad_accept;
        bad_accept.next_receive = 10;
        bad_accept.next_execute = 11;
        ASSERT_FALSE(decode(encode(bad_accept), ac));
        StartMsg start;
        start.map_name = "TREASURE.LVL";
        start.roster = 0x03;
        StartMsg start_back;
        start.endpoints[1].address = "bad address";
        ASSERT_FALSE(decode(encode(start), start_back));
        start.endpoints[1].address = std::string(65, '1');                    // too long to travel: the encoder sends none
        ASSERT_TRUE(decode(encode(start), start_back) && start_back.endpoints[1].address.empty());
        start.endpoints[1].address = "198.51.100.7";
        ASSERT_TRUE(decode(encode(start), start_back) && start_back.endpoints[1].address == "198.51.100.7");
        // the Lag message names a seat of the match
        LagMsg bad_lag;
        LagMsg lag2;
        bad_lag.seat = 4;
        ASSERT_FALSE(decode(encode(bad_lag), lag2));
        bad_lag.seat = 255;
        ASSERT_FALSE(decode(encode(bad_lag), lag2));
        // unknown types (24 was one until protocol 7 gave it to StartRequest, 25 until protocol 8 gave it to Lag, 26 - 30 until protocol 10 gave them to Presence, Vote, CatchUp, TurnBatch, CaughtUp, 31 until protocol 14 gave it to SeatMove, 32 until protocol 16 gave it to Plan, 33 until protocol 16 gave it to Name, and 34 until protocol 16 gave it to Remove)
        for (uint8_t type : std::vector<uint8_t>{0, 35, 100, 255}) {
            const std::vector<uint8_t> m = {type, 0, 0, 0, 0};
            ASSERT_EQ(peek_type(m), MsgType::None);
        }
        ASSERT_EQ(peek_type(std::vector<uint8_t>{24}), MsgType::StartRequest);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{25}), MsgType::Lag);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{26}), MsgType::Presence);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{30}), MsgType::CaughtUp);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{31}), MsgType::SeatMove);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{32}), MsgType::Plan);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{33}), MsgType::Name);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{34}), MsgType::Remove);
        ASSERT_EQ(static_cast<int>(MsgType::Last), 34);
        ASSERT_EQ(peek_type(std::vector<uint8_t>{}), MsgType::None);
    } TEST_END();

    TEST_CASE("N2.3 Protocol: 400000 Random And Mutated Messages Never Crash And Only Give Messages That Encode Back To The Same Bytes") {
        Lcg rng(99);
        TurnMsg turn;
        turn.turn = 1000;
        turn.commands = {cmd(CommandType::GroupMove, 1, 255, 3, 4, {1, 2}), cmd(CommandType::Stop, 2, 255, 0, 0, {9}), cmd(CommandType::Hatch, 0)};
        HashMsg hash;
        hash.turn = 10;
        hash.hash = {1, 2, 3, 4, 5, 6, 7, 8};
        HelloMsg keyed;                                                // the messages of protocol 10 as seeds (N2.40 has the rules one by one; this is the net under them)
        keyed.name = "Ann";
        keyed.room = "ROOM-7";
        keyed.token = "tok";
        keyed.key = key_with(3);
        keyed.have_turns = 700;
        const std::vector<uint8_t> seed_hello = encode(keyed);
        WelcomeMsg welcome;
        welcome.player = 2;
        welcome.players = 4;
        welcome.key = key_with(4);
        welcome.flags = kWelcomeRejoin;
        const std::vector<uint8_t> seed_welcome = encode(welcome);
        PresenceMsg presence;
        presence.missing = {{1, PresenceMsg::State::Absent, 42, 0}, {3, PresenceMsg::State::CatchingUp, 31, 63}};
        presence.vote_seat = 1;
        presence.votes_continue = 1;
        presence.voters = 2;
        presence.your_vote = 2;
        presence.cap_s = 1700;
        const std::vector<uint8_t> seed_presence = encode(presence);
        StartRequestMsg request13;                                     // protocol 13: a level for each seat and a pair of seats as a team (N2.100 has the rules one by one)
        request13.fill = {FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard};
        request13.set_teams(sim::StartTeams{true, 1, 2});
        const std::vector<uint8_t> seed_request13 = encode(request13);
        StartMsg start13;
        start13.map_name = "TREASURE.LVL";
        start13.roster = 0x0F;
        start13.names = {"A", "B", "C", "D"};
        start13.set_teams(sim::StartTeams{true, 0, 3});
        const std::vector<uint8_t> seed_start13 = encode(start13);
        HelloMsg page_hello;                                           // protocol 16: a lobby page that makes its room (the client kind, the create block with kCreateLobby)
        page_hello.name = "Ann";
        page_hello.room = "k7m2xq";
        page_hello.client_kind = kClientPage;
        page_hello.create = CreateBlock{"TREASURE.LVL", 4, kNoTeam, kNoTeam, static_cast<uint8_t>(kCreateLeaderStarts | kCreateLobby)};
        const std::vector<uint8_t> seed_page_hello = encode(page_hello);
        WelcomeMsg created;
        created.player = 0;
        created.players = 4;
        created.key = key_with(5);
        created.flags = kWelcomeCreated;
        const std::vector<uint8_t> seed_created = encode(created);
        RoomMsg lobby_room = server_room_of(0);                        // a lobby room: a plan for each color and the seats that hold a game
        lobby_room.flags = static_cast<uint8_t>(kRoomLeaderStarts | kRoomLobby);
        lobby_room.plan = {PlanKind::Open, PlanKind::Open, PlanKind::Hard, PlanKind::Nobody};
        lobby_room.in_game = 0x01;
        const std::vector<uint8_t> seed_lobby_room = encode(lobby_room);
        PlanMsg plan_msg;
        plan_msg.map_name = "TREASURE.LVL";
        plan_msg.plan = {PlanKind::Open, PlanKind::Easy, PlanKind::Medium, PlanKind::Nobody};
        plan_msg.set_teams(sim::StartTeams{true, 0, 2});
        const std::vector<uint8_t> seed_plan = encode(plan_msg);
        TurnBatchMsg batch;
        batch.first_turn = 500;
        batch.turns.resize(3);
        batch.turns[0].commands = {cmd(CommandType::GroupMove, 1, 255, 3, 4, {1, 2}), cmd(CommandType::Hatch, 2)};
        batch.turns[2].commands = {cmd(CommandType::Stop, 0, 255, 0, 0, {9})};
        for (uint32_t i = 0; i < 3; ++i) batch.turns[i].turn = 500 + i;
        const std::vector<uint8_t> seed_batch = encode(batch);
        const std::vector<std::vector<uint8_t>> seeds = {encode(turn), encode(hash), encode(CommandMsg{cmd(CommandType::GroupAttack, 2, 255, 5, 5, {1})}),
                                                         encode(ChatMsg{1, true, "hello"}), encode(hello_of(1, "Ann")), encode(WelcomeMsg{1, 4}),
                                                         encode(ProposeMsg{1, 2}), encode(AcceptMsg{1, 100, 98}), encode(RefuseMsg{1, 1}),
                                                         encode(ResumeMsg{1, 2, 500}), encode(RequestMsg{40}), encode(PeerHelloMsg{3}),
                                                         encode(server_room_of(0)), encode(server_room_of(255)), encode(StartRequestMsg{}), encode(LagMsg{2, 7000}), seed_hello,
                                                         seed_welcome, encode(RejectMsg{RejectReason::Superseded}), seed_presence, encode(VoteMsg{2, true}), encode(CatchUpMsg{100, 4000}),
                                                         seed_batch, encode(CaughtUpMsg{4000, {1, 2, 3, 4, 5, 6, 7, 8}}), seed_request13, seed_start13, encode(SeatMoveMsg{1, 3, 5}), seed_page_hello, seed_created, seed_lobby_room, seed_plan, encode(NameMsg{"Ann"}), encode(RemoveMsg{2, 9})};
        size_t accepted = 0;
        for (int i = 0; i < 400000; ++i) {
            std::vector<uint8_t> buf;
            if (i % 3 == 0) {
                buf.resize(rng.below(80));
                for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
                if (!buf.empty()) buf[0] = static_cast<uint8_t>(1 + rng.below(34));    // a plausible type byte
            } else {
                buf = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
                for (uint32_t m = 1 + rng.below(3); m > 0; --m) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
                if (rng.below(5) == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            }
            TurnMsg t;
            HashMsg h;
            CommandMsg c;
            ChatMsg ch;
            HelloMsg he;
            WelcomeMsg we;
            ProposeMsg pr;
            AcceptMsg ac;
            RefuseMsg rf;
            ResumeMsg rs;
            RequestMsg rq;
            PeerHelloMsg ph;
            RoomMsg room;
            StartRequestMsg sreq;
            StartMsg smsg;
            SeatMoveMsg smove;
            PlanMsg plan;
            NameMsg nm;
            RemoveMsg rem;
            LagMsg lg;
            RejectMsg rj;
            PresenceMsg pres;
            VoteMsg vt;
            CatchUpMsg cu;
            TurnBatchMsg tb;
            CaughtUpMsg cg;
            if (decode(buf, lg)) { ++accepted; ASSERT_TRUE(encode(lg) == buf); }
            if (decode(buf, rj)) { ++accepted; ASSERT_TRUE(encode(rj) == buf); }
            if (decode(buf, pres)) {                   // a Presence that gets through is one that an honest server could have said, and encodes back to the same bytes
                ++accepted;
                ASSERT_TRUE(encode(pres) == buf);
                ASSERT_TRUE(pres.missing.size() <= sim::MAX_PLAYERS && pres.votes_continue <= pres.voters && pres.voters <= sim::MAX_PLAYERS && pres.your_vote <= 2);
                ASSERT_TRUE(pres.vote_seat == 255 || (pres.vote_seat < sim::MAX_PLAYERS && !pres.missing.empty()));
            }
            if (decode(buf, vt)) { ++accepted; ASSERT_TRUE(encode(vt) == buf && buf.size() == 3); }
            if (decode(buf, cu)) { ++accepted; ASSERT_TRUE(encode(cu) == buf && cu.first_turn <= cu.total_turns); }
            if (decode(buf, tb)) {
                ++accepted;
                ASSERT_TRUE(encode(tb) == buf);
                ASSERT_TRUE(!tb.turns.empty() && tb.turns.size() <= kMaxBatchTurns && buf.size() <= kMaxMessageBytes);
                for (size_t k = 0; k < tb.turns.size(); ++k) ASSERT_TRUE(tb.turns[k].turn == tb.first_turn + k && tb.turns[k].commands.size() <= kMaxTurnCommands);
            }
            if (decode(buf, cg)) { ++accepted; ASSERT_TRUE(encode(cg) == buf && buf.size() == 69); }
            if (decode(buf, t)) {
                ++accepted;
                ASSERT_TRUE(encode(t) == buf);
                ASSERT_TRUE(t.commands.size() <= kMaxTurnCommands);
            }
            if (decode(buf, h)) { ++accepted; ASSERT_TRUE(encode(h) == buf); }
            if (decode(buf, c)) { ++accepted; ASSERT_TRUE(encode(c) == buf); }
            if (decode(buf, ch)) { ++accepted; ASSERT_TRUE(encode(ch) == buf); }
            if (decode(buf, he)) { ++accepted; ASSERT_TRUE(encode(he) == buf); }
            if (decode(buf, we)) { ++accepted; ASSERT_TRUE(encode(we) == buf); }
            if (decode(buf, pr)) { ++accepted; ASSERT_TRUE(encode(pr) == buf); }
            if (decode(buf, ac)) { ++accepted; ASSERT_TRUE(encode(ac) == buf); }
            if (decode(buf, rf)) { ++accepted; ASSERT_TRUE(encode(rf) == buf); }
            if (decode(buf, rs)) { ++accepted; ASSERT_TRUE(encode(rs) == buf); }
            if (decode(buf, rq)) { ++accepted; ASSERT_TRUE(encode(rq) == buf); }
            if (decode(buf, ph)) { ++accepted; ASSERT_TRUE(encode(ph) == buf); }
            if (decode(buf, room)) {                   // a Room that gets through names a leader that is a guest's seat (or nobody) and encodes back to the same bytes
                ++accepted;
                ASSERT_TRUE(encode(room) == buf);
                ASSERT_TRUE(room.leader == kNoLeader || (room.leader < sim::MAX_PLAYERS && room.slots[room.leader].state == SlotState::Client));
                ASSERT_TRUE(room.lobby() || (!room.starting() && room.in_game == 0 && room.plan == std::array<PlanKind, 4>{}));        // (protocol 16: a plan, a game and "starting" are a lobby room's)
                ASSERT_TRUE(!room.lobby() || room.leader_starts());
                for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) ASSERT_TRUE(!room.seat_in_game(seat) || room.slots[seat].state == SlotState::Client);
            }
            if (decode(buf, sreq)) {                   // (protocol 13: the type, a level for each seat, two team bytes: none, or two different seats)
                ++accepted;
                ASSERT_TRUE(encode(sreq) == buf && buf.size() == 7);
                for (const FillLevel level : sreq.fill) ASSERT_TRUE(static_cast<uint8_t>(level) <= kFillLevelLast);
                ASSERT_TRUE((sreq.team_a == kNoTeam && sreq.team_b == kNoTeam) || (sreq.team_a < sim::MAX_PLAYERS && sreq.team_b < sim::MAX_PLAYERS && sreq.team_a != sreq.team_b));
            }
            if (decode(buf, smove)) {                  // (protocol 14: the type and two different seats of the room; 15: and the guard that is not 0)
                ++accepted;
                ASSERT_TRUE(encode(smove) == buf && buf.size() == 7 && smove.from < sim::MAX_PLAYERS && smove.to < sim::MAX_PLAYERS && smove.from != smove.to && smove.guard != 0);
            }
            if (decode(buf, plan)) {                   // (protocol 16: a map, a kind 0 .. 4 for each color, the teams: none or two seats, the lower first)
                ++accepted;
                ASSERT_TRUE(encode(plan) == buf);
                for (const PlanKind kind : plan.plan) ASSERT_TRUE(static_cast<uint8_t>(kind) <= kPlanKindLast);
                ASSERT_TRUE((plan.team_a == kNoTeam && plan.team_b == kNoTeam) || (plan.team_a < plan.team_b && plan.team_b < sim::MAX_PLAYERS));
            }
            if (decode(buf, nm)) {                     // (protocol 16: a name of 1 - 32 printable characters that neither starts nor ends with a space)
                ++accepted;
                ASSERT_TRUE(encode(nm) == buf && !nm.name.empty() && nm.name.size() <= kMaxNameChars && nm.name.front() != ' ' && nm.name.back() != ' ');
            }
            if (decode(buf, rem)) {                    // (protocol 16: the type, a seat of the room and the guard that is not 0)
                ++accepted;
                ASSERT_TRUE(encode(rem) == buf && buf.size() == 6 && rem.seat < sim::MAX_PLAYERS && rem.guard != 0);
            }
            if (decode(buf, smsg)) {                   // a Start that gets through has teams that its own roster can make, and encodes back to the same bytes
                ++accepted;
                ASSERT_TRUE(encode(smsg) == buf);
                ASSERT_TRUE((smsg.team_a == kNoTeam && smsg.team_b == kNoTeam) || sim::plan_start_teams(smsg.teams(), smsg.roster).why.empty());
            }
        }
        ASSERT_TRUE(accepted > 5000);                  // the mutations of valid messages do get through
    } TEST_END();

    TEST_CASE("N2.3b Protocol 7: The Room Names Its Leader (Every Value; Only The Seat Of A Guest Or Nobody), StartRequest Is The Type, A Fill Level 0 .. 3 For Each Seat And The Two Team Bytes (Seven Bytes Since Protocol 13, Two Before, One Before That) And Nothing Else, The Layout Of Protocol 6 Is Refused") {
        ASSERT_TRUE(kProtocolVersion >= 7);                                // protocol 7 grew the Room message by a byte and added a message type (8 to 11 keep both): a client of protocol 6 cannot play with it
        ASSERT_EQ(kNoLeader, 255);
        // every value of the leader: nobody, and each seat that a guest holds; the byte is the fourth before the room's two team bytes, its rules and (protocol 16) its five lobby bytes and (protocol 17) its mode, the receiver's own seat
        // ("you") the one before it
        const size_t lobby_tail = 5 + 1;
        for (const uint8_t leader : {uint8_t{255}, uint8_t{0}, uint8_t{1}, uint8_t{3}}) {
            RoomMsg r = server_room_of(leader);
            for (const uint8_t you : {uint8_t{255}, uint8_t{0}, uint8_t{1}, uint8_t{3}}) {                     // (the receiver may or may not be the leader)
                r.you = you;
                const std::vector<uint8_t> bytes = encode(r);
                ASSERT_EQ(bytes[bytes.size() - 4 - lobby_tail], leader);
                ASSERT_EQ(bytes[bytes.size() - 5 - lobby_tail], you);
                RoomMsg back;
                ASSERT_TRUE(decode(bytes, back) && back.leader == leader && back.you == you);
                ASSERT_TRUE(encode(back) == bytes);
            }
        }
        {   // a room that names nobody: the default, and what a LAN host's room says
            RoomMsg none;
            RoomMsg back;
            ASSERT_EQ(none.leader, 255);
            ASSERT_TRUE(decode(encode(none), back) && back.leader == kNoLeader);
            RoomMsg lan = none;                                            // a host that holds a seat: no leader
            lan.slots[0] = {SlotState::Host, "Queen", 0};
            lan.slots[1] = {SlotState::Client, "Bob", 20};
            ASSERT_TRUE(decode(encode(lan), back) && back.leader == kNoLeader && back.slots[0].state == SlotState::Host);
        }
        // what a leader may be: the seat of a person who joined as a guest. Anything else is no message.
        const std::vector<uint8_t> base = encode(server_room_of(255));
        RoomMsg out;
        for (unsigned value = 0; value < 256; ++value) {                   // every byte value in the leader's place
            std::vector<uint8_t> bytes = base;
            bytes[bytes.size() - 4 - lobby_tail] = static_cast<uint8_t>(value);
            const bool should = value == 255 || value == 0 || value == 1 || value == 3;      // seats 0, 1 and 3 hold guests, seat 2 is empty
            ASSERT_EQ(decode(bytes, out), should);
        }
        {   // a leader on an empty seat, on a bot, on the host
            RoomMsg r = server_room_of(0);
            r.leader = 2;
            ASSERT_FALSE(decode(encode(r), out));                          // empty
            r.slots[2] = {SlotState::Bot, "Bot (Medium)", 0};
            ASSERT_FALSE(decode(encode(r), out));                          // a computer player never leads
            r.slots[2] = {SlotState::Host, "Queen", 0};
            ASSERT_FALSE(decode(encode(r), out));                          // nor does a host that holds a seat
            r.slots[2] = {SlotState::Client, "Dan", 10};
            ASSERT_TRUE(decode(encode(r), out) && out.leader == 2);        // a guest does
        }
        {   // a message that is one byte short (the layout of protocol 6 had no leader byte at all: N2.103 has the layout of protocol 14) is no Room message of this protocol, and neither is one byte too many
            std::vector<uint8_t> v6 = base;
            v6.pop_back();
            ASSERT_FALSE(decode(v6, out));
            std::vector<uint8_t> longer = base;
            longer.push_back(255);
            ASSERT_FALSE(decode(longer, out));
        }
        // StartRequest (protocol 13): exactly the type, a fill level 0 .. 3 for each of the four seats and the two team bytes (N2.100 has every rule of it)
        const std::vector<uint8_t> request = encode(StartRequestMsg{});
        ASSERT_TRUE(request == (std::vector<uint8_t>{24, 0, 0, 0, 0, 255, 255}));
        StartRequestMsg sr;
        ASSERT_TRUE(decode(request, sr) && sr.fill == StartRequestMsg{}.fill && sr.team_a == kNoTeam && sr.team_b == kNoTeam);
        for (uint8_t level = 0; level <= kFillLevelLast; ++level) {
            const std::vector<uint8_t> m = {24, level, level, level, level, 255, 255};
            ASSERT_TRUE(decode(m, sr) && sr.fill == StartRequestMsg::all(static_cast<FillLevel>(level)).fill && encode(sr) == m);
        }
        ASSERT_FALSE(decode(std::vector<uint8_t>{24}, sr));                // protocol 7's single byte is no StartRequest any more
        ASSERT_FALSE(decode(std::vector<uint8_t>{24, 0}, sr));             // nor is protocol 11's two bytes
        ASSERT_FALSE(decode(std::vector<uint8_t>{}, sr));
        ASSERT_FALSE(decode(nullptr, 1, sr));
        for (unsigned level = kFillLevelLast + 1u; level < 256u; ++level) ASSERT_FALSE(decode(std::vector<uint8_t>{24, static_cast<uint8_t>(level), 0, 0, 0, 255, 255}, sr));      // a level above 3
        for (size_t extra : {size_t{1}, size_t{2}, size_t{9}, size_t{200}, kMaxMessageBytes - 7}) {    // a payload of any size beyond the two team bytes makes it garbage
            std::vector<uint8_t> payload = request;
            payload.resize(7 + extra, 0x01);
            ASSERT_FALSE(decode(payload, sr));
        }
        for (unsigned type = 0; type < 256; ++type) {                      // no other type byte is a StartRequest
            if (type == 24) continue;
            ASSERT_FALSE(decode(std::vector<uint8_t>{static_cast<uint8_t>(type), 0, 0, 0, 0, 255, 255}, sr));
        }
        // and the neighbours: no other decoder takes it, and it takes no other message
        ASSERT_FALSE(any_decodes(std::vector<uint8_t>{24}) || any_decodes(std::vector<uint8_t>{24, 4}) || any_decodes(std::vector<uint8_t>{}));
        ASSERT_TRUE(any_decodes(request) && peek_type(request) == MsgType::StartRequest);                         // (and the one decoder that takes it is the StartRequest's own)
        for (const auto& m : {encode(server_room_of(0)), encode(hello_of(kProtocolVersion, "A")), encode_begin(), encode_leave(), encode(WelcomeMsg{1, 4})}) {
            ASSERT_FALSE(decode(m, sr));
        }
        // 20,000 messages made of the leader's neighbours: bytes of the Room that were changed, or cut, or lengthened never give a leader that is not a guest's seat
        Lcg rng(7);
        size_t with_leader = 0;
        for (int i = 0; i < 20000; ++i) {
            std::vector<uint8_t> bytes = encode(server_room_of(static_cast<uint8_t>(i % 2 == 0 ? 0 : 255)));
            const uint32_t pick = rng.below(12);                                                                 // the leader's byte: mostly a seat number or nobody, sometimes any byte
            bytes[bytes.size() - 4 - lobby_tail] = pick < 8 ? static_cast<uint8_t>(pick) : (pick == 8 ? uint8_t{255} : static_cast<uint8_t>(rng.below(256)));
            if (rng.below(3) == 0) bytes[bytes.size() - 5 - lobby_tail] = static_cast<uint8_t>(rng.below(256));               // and the receiver's own seat
            if (rng.below(7) == 0) bytes.resize(rng.below(static_cast<uint32_t>(bytes.size()) + 2));
            RoomMsg room;
            if (decode(bytes, room)) {
                with_leader += room.leader == kNoLeader ? 0u : 1u;
                ASSERT_TRUE(room.leader == kNoLeader || room.slots[room.leader].state == SlotState::Client);
                ASSERT_TRUE(room.you == 255 || room.you < sim::MAX_PLAYERS);
                ASSERT_TRUE(encode(room) == bytes);
            }
        }
        ASSERT_TRUE(with_leader > 1000);
    } TEST_END();

    TEST_CASE("N2.40 Protocol 10: Keys In Hello And Welcome, Three More Rejections, Presence (With The Resume Countdown), Vote, CatchUp, TurnBatch And CaughtUp: Numbers, Layouts Byte By Byte, Every Truncation, Every Range Rule, The Batch Encoders Agree") {
        // ---- the numbers ----
        ASSERT_EQ(kProtocolVersion, 21);                                 // (the layouts of protocol 10 below are still the layouts of protocols 11 to 17: 11 and 13 changed the StartRequest (13 the Start too), 12 no message, 14 added the SeatMove; 15 changed the Hello, the Room and the Start, and 16 the Hello, the Welcome's flags, the Room and added the PlanMsg, and 17 the mode byte at the end of the Room, the Start and the PlanMsg, which this test reads where it says so)
        ASSERT_TRUE(static_cast<int>(MsgType::Presence) == 26 && static_cast<int>(MsgType::Vote) == 27 && static_cast<int>(MsgType::CatchUp) == 28 &&
                    static_cast<int>(MsgType::TurnBatch) == 29 && static_cast<int>(MsgType::CaughtUp) == 30 && static_cast<int>(MsgType::SeatMove) == 31 && static_cast<int>(MsgType::Plan) == 32 && static_cast<int>(MsgType::Name) == 33 && static_cast<int>(MsgType::Remove) == 34 && static_cast<int>(MsgType::Last) == 34);
        ASSERT_TRUE(static_cast<int>(RejectReason::Dropped) == 7 && static_cast<int>(RejectReason::RejoinFailed) == 8 && static_cast<int>(RejectReason::Superseded) == 9);
        ASSERT_TRUE(kKeyBytes == 16 && kMaxBatchTurns == 4096 && kBatchBytes == 48 * 1024 && kBatchHeaderBytes == 7 && kWelcomeRejoin == 1 && kCapSecondsMore == 0xFFFF);
        for (const MsgType t : {MsgType::Presence, MsgType::Vote, MsgType::CatchUp, MsgType::TurnBatch, MsgType::CaughtUp}) {
            ASSERT_EQ(peek_type(std::vector<uint8_t>{static_cast<uint8_t>(t)}), t);
        }
        ASSERT_EQ(peek_type(std::vector<uint8_t>{35}), MsgType::None);                  // (31 is the SeatMove of protocol 14, N2.101; 32 the PlanMsg, 33 the NameMsg and 34 the RemoveMsg of protocol 16, N2.106, N2.108 and N2.110)

        // ---- the keys: zero is no key, a key matches only itself, and never "no key" ----
        {
            SeatKey zero{};
            ASSERT_TRUE(key_is_zero(zero));
            ASSERT_FALSE(key_matches(zero, zero));                           // "no key" matches nothing, not even "no key"
            ASSERT_FALSE(key_matches(zero, key_with(1)));
            ASSERT_FALSE(key_matches(key_with(1), zero));
            ASSERT_TRUE(key_matches(key_with(1), key_with(1)));
            ASSERT_FALSE(key_matches(key_with(1), key_with(2)));
            for (size_t i = 0; i < kKeyBytes; ++i) {                         // a key that differs in ANY one byte, in any bit, is another key
                for (unsigned bit = 0; bit < 8; ++bit) {
                    SeatKey other = key_with(1);
                    other[i] = static_cast<uint8_t>(other[i] ^ (1u << bit));
                    ASSERT_FALSE(key_matches(key_with(1), other));
                    ASSERT_FALSE(key_is_zero(other));
                }
                SeatKey one_byte{};
                one_byte[i] = 1;
                ASSERT_FALSE(key_is_zero(one_byte));                         // a key with one byte set, wherever it is, is a key
            }
        }

        // ---- Hello: the layout (v6 fields, then the key and the turns, then the platform byte of protocol 15 and the client kind of protocol 16: N2.102 has its create block), the rules ----
        {
            HelloMsg h;
            h.name = "Ann";
            h.listen_port = 0x1234;
            h.want_seat = 2;
            h.room = "ROOM-7";
            h.token = "tok";
            h.key = key_with(5);
            h.have_turns = 0x01020304;
            const std::vector<uint8_t> bytes = encode(h);
            ASSERT_EQ(bytes.size(), size_t{1 + 2 + 4 + 2 + 1 + 7 + 4 + 16 + 4 + 1 + 1}); // type, version, "Ann", port, seat, "ROOM-7", "tok", key, turns, platform, client kind
            ASSERT_TRUE(bytes[0] == 1 && bytes[1] == (kProtocolVersion & 0xFF) && bytes[2] == (kProtocolVersion >> 8));      // (the version is whatever this protocol is: 10 here until protocol 11)
            const SeatKey key5 = key_with(5);
            ASSERT_TRUE(std::equal(key5.begin(), key5.end(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() - 22)));       // the key: 16 bytes
            ASSERT_TRUE(bytes[bytes.size() - 6] == 4 && bytes[bytes.size() - 5] == 3 && bytes[bytes.size() - 4] == 2 && bytes[bytes.size() - 3] == 1 && bytes[bytes.size() - 2] == 0 && bytes[bytes.size() - 1] == 0);   // the turns, little endian, the platform byte (not told) and the client kind (a game)
            HelloMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.version == kProtocolVersion && back.name == "Ann" && back.listen_port == 0x1234 && back.want_seat == 2 && back.room == "ROOM-7" && back.token == "tok" &&
                        back.key == key_with(5) && back.have_turns == 0x01020304);
            ASSERT_TRUE(encode(back) == bytes);
            // a new player: no key, no turns; the largest count with a key; no key with turns is no Hello (a count belongs to a key)
            HelloMsg fresh;
            ASSERT_TRUE(decode(encode(fresh), back) && key_is_zero(back.key) && back.have_turns == 0);
            h.have_turns = 0xFFFFFFFFu;
            ASSERT_TRUE(decode(encode(h), back) && back.have_turns == 0xFFFFFFFFu);
            h.have_turns = 0;
            ASSERT_TRUE(decode(encode(h), back) && back.key == key_with(5) && back.have_turns == 0);        // a key and nothing yet: starts from nothing
            HelloMsg orphan;
            orphan.have_turns = 1;
            ASSERT_FALSE(decode(encode(orphan), back));
            // a key that is cut short: every length between the end of the token and the end of the message
            const std::vector<uint8_t> core = encode(h);
            for (size_t keep = core.size() - 22; keep < core.size(); ++keep) {
                const std::vector<uint8_t> shorter(core.begin(), core.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = core;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            // the prefix of every version's Hello reads the same: a host answers "version mismatch" without reading what follows
            ASSERT_TRUE(decode_hello_prefix(core.data(), core.size(), back) && back.version == kProtocolVersion && back.name == "Ann");
            for (const uint16_t version : {uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}}) {         // the old layout (protocols 6 to 9 had it): no key, no turns
                const std::vector<uint8_t> old = old_layout_hello(version, "Old", "ROOM-7", "tok");
                ASSERT_FALSE(decode(old, back));                                             // it is not a Hello of this protocol ...
                ASSERT_TRUE(decode_hello_prefix(old.data(), old.size(), back) && back.version == version && back.name == "Old");    // ... but its version and name are read
                ASSERT_FALSE(any_decodes(old));
            }
        }

        // ---- Welcome: the layout, the flags, the rules ----
        {
            WelcomeMsg w;
            w.player = 2;
            w.players = 4;
            w.key = key_with(6);
            w.flags = kWelcomeRejoin;
            std::vector<uint8_t> bytes = encode(w);
            ASSERT_EQ(bytes.size(), size_t{20});                                             // type, player, players, key, flags
            ASSERT_TRUE(bytes[0] == 2 && bytes[1] == 2 && bytes[2] == 4 && bytes[19] == 1);
            const SeatKey key6 = key_with(6);
            ASSERT_TRUE(std::equal(key6.begin(), key6.end(), bytes.begin() + 3));
            WelcomeMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.player == 2 && back.players == 4 && back.key == key_with(6) && back.flags == kWelcomeRejoin);
            ASSERT_TRUE(encode(back) == bytes);
            WelcomeMsg lan;                                                                  // a LAN or direct host: no key, no flags
            lan.player = 1;
            lan.players = 4;
            ASSERT_TRUE(decode(encode(lan), back) && key_is_zero(back.key) && back.flags == 0);
            w.flags = 0;                                                                     // a seat in the room: a key and no rejoin
            ASSERT_TRUE(decode(encode(w), back) && back.key == key_with(6) && back.flags == 0);
            for (unsigned flags = 0; flags < 256; ++flags) {                                 // the flags are 0, 1 (a rejoin) or 2 (protocol 16: this Hello made the room), never both and never another bit
                bytes = encode(w);
                bytes[19] = static_cast<uint8_t>(flags);
                ASSERT_EQ(decode(bytes, back), flags <= 2);
            }
            lan.flags = kWelcomeRejoin;                                                      // a rejoin is of a seat that has a key
            ASSERT_FALSE(decode(encode(lan), back));
            lan.flags = kWelcomeCreated;                                                     // and so is a made room
            ASSERT_FALSE(decode(encode(lan), back));
            // the old layout (type, player, players) and a cut key
            ASSERT_FALSE(decode(std::vector<uint8_t>{2, 1, 4}, back));
            bytes = encode(w);
            for (size_t keep = 3; keep < bytes.size(); ++keep) ASSERT_FALSE(any_decodes(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep))));
            bytes.push_back(1);
            ASSERT_FALSE(decode(bytes, back));
        }

        // ---- Reject: the three new reasons ----
        for (unsigned reason = 0; reason < 256; ++reason) {
            RejectMsg rj;
            const std::vector<uint8_t> bytes = {static_cast<uint8_t>(MsgType::Reject), static_cast<uint8_t>(reason)};
            ASSERT_EQ(decode(bytes, rj), reason >= 1 && reason <= 9);
            if (reason >= 1 && reason <= 9) {
                ASSERT_TRUE(static_cast<unsigned>(rj.reason) == reason && encode(rj) == bytes);
            }
        }

        // ---- Presence ----
        {
            PresenceMsg base;
            base.missing = {{1, PresenceMsg::State::Absent, 42, 0}, {3, PresenceMsg::State::CatchingUp, 31, 63}};
            base.vote_seat = 1;
            base.votes_continue = 1;
            base.voters = 2;
            base.your_vote = 2;
            base.cap_s = 1700;
            const std::vector<uint8_t> bytes = encode(base);
            ASSERT_EQ(bytes.size(), size_t{1 + 1 + 2 * 5 + 4 + 2 + 1});
            ASSERT_TRUE(bytes[0] == 26 && bytes[1] == 2);
            ASSERT_TRUE(bytes[2] == 1 && bytes[3] == 1 && bytes[4] == 42 && bytes[5] == 0 && bytes[6] == 0);                    // seat 1, absent, waited 42 s (u16), no progress
            ASSERT_TRUE(bytes[7] == 3 && bytes[8] == 2 && bytes[9] == 31 && bytes[10] == 0 && bytes[11] == 63);                // seat 3, catching up, 31 s, 63 %
            ASSERT_TRUE(bytes[12] == 1 && bytes[13] == 1 && bytes[14] == 2 && bytes[15] == 2 && bytes[16] == (1700 & 0xFF) && bytes[17] == (1700 >> 8));
            ASSERT_TRUE(bytes[18] == 0);                                                                                       // no countdown
            PresenceMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.missing.size() == 2 && back.missing[0].seat == 1 && back.missing[0].waited_s == 42 && back.missing[1].state == PresenceMsg::State::CatchingUp &&
                        back.missing[1].progress == 63 && back.vote_seat == 1 && back.votes_continue == 1 && back.voters == 2 && back.your_vote == 2 && back.cap_s == 1700);
            ASSERT_TRUE(encode(back) == bytes);
            // the u16 fields use both bytes: a wait of 0xFFFF s (it saturates there) and one of 0x1234, a cap of 0xABCD
            PresenceMsg wide;
            wide.missing = {{2, PresenceMsg::State::Absent, 0xFFFF, 0}, {0, PresenceMsg::State::CatchingUp, 0x1234, 100}};
            wide.vote_seat = 2;
            wide.votes_continue = 3;
            wide.voters = 3;
            wide.your_vote = 1;
            wide.cap_s = 0xABCD;
            ASSERT_TRUE(decode(encode(wide), back) && back.missing[0].waited_s == 0xFFFF && back.missing[1].waited_s == 0x1234 && back.cap_s == 0xABCD && encode(back) == encode(wide));
            // the match runs: nobody missing, no vote, any number of voters; the cap is told all the same
            PresenceMsg runs;
            runs.voters = 3;
            runs.cap_s = 0;
            ASSERT_TRUE(decode(encode(runs), back) && back.missing.empty() && back.vote_seat == 255 && back.voters == 3 && back.cap_s == 0);
            runs.cap_s = 0xFFFF;
            ASSERT_TRUE(decode(encode(runs), back) && back.cap_s == 0xFFFF);
            ASSERT_EQ(encode(PresenceMsg{}).size(), size_t{1 + 1 + 4 + 2 + 1});
            // every truncation and a byte too many
            for (size_t keep = 0; keep < bytes.size(); ++keep) {
                const std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            // every range rule, one at a time, on the base message
            auto with = [&](size_t at, uint8_t value) {
                std::vector<uint8_t> b = bytes;
                b[at] = value;
                return decode(b, back);
            };
            ASSERT_FALSE(with(1, 5));                                    // more than four missing
            ASSERT_FALSE(with(1, 4));                                    // four promised, two there
            ASSERT_FALSE(with(1, 3));                                    // three promised: the vote's bytes are read as the third entry, and then a byte is short
            ASSERT_FALSE(with(2, 4));                                    // a seat that does not exist (the entry's seat, and the vote is about it)
            ASSERT_FALSE(with(7, 1));                                    // a seat twice
            ASSERT_FALSE(with(3, 0));                                    // a state that is neither absent nor catching up
            ASSERT_FALSE(with(3, 3));
            ASSERT_FALSE(with(8, 0));
            ASSERT_FALSE(with(8, 3));
            ASSERT_FALSE(with(6, 1));                                    // an absent seat has no progress
            ASSERT_TRUE(with(11, 100) && back.missing[1].progress == 100);
            ASSERT_FALSE(with(11, 101));                                 // a progress above 100
            ASSERT_FALSE(with(11, 255));
            ASSERT_TRUE(with(9, 42) && back.missing[1].waited_s == 42);  // equal waits are in order (a tie)
            ASSERT_FALSE(with(9, 43));                                   // the second seat waited longer than the first: not longest away first
            ASSERT_TRUE(with(4, 255) && back.missing[0].waited_s == 255);// (a longer first wait is in order: the second is 31)
            ASSERT_TRUE(with(12, 3) && back.vote_seat == 3);             // the vote may be about a seat that is catching up, or one that is not missing at all (a seat that FLAPS is put to the vote in any state) ...
            ASSERT_TRUE(with(12, 0) && with(12, 2) && back.vote_seat == 2);
            ASSERT_FALSE(with(12, 4));                                   // ... but about a seat that exists
            ASSERT_FALSE(with(12, 254));
            ASSERT_FALSE(with(13, 3));                                   // more votes (3) than voters (2)
            ASSERT_TRUE(with(13, 2));
            ASSERT_FALSE(with(14, 5));                                   // five voters
            ASSERT_FALSE(with(14, 255));
            ASSERT_TRUE(with(14, 1) && back.voters == 1);                // one vote of one voter is all of them
            ASSERT_FALSE(with(14, 0));                                   // one vote and no voter
            ASSERT_FALSE(with(15, 3));                                   // a choice of the receiver that is not 0, 1 or 2
            ASSERT_TRUE(with(15, 0) && with(15, 1) && with(15, 2));
            ASSERT_TRUE(with(16, 0) && back.cap_s == (1700 & 0xFF00));   // the cap, a u16 that nothing constrains
            ASSERT_TRUE(with(17, 0) && back.cap_s == (1700 & 0x00FF));
            ASSERT_TRUE(with(17, 0xFF) && back.cap_s == (0xFF00 | (1700 & 0xFF)));
            ASSERT_TRUE(with(18, 0) && back.resume_s == 0);              // the countdown of the resume: none while a seat is missing (the match is paused, nothing counts down)
            ASSERT_FALSE(with(18, 1));
            ASSERT_FALSE(with(18, 60));
            ASSERT_FALSE(with(18, 255));
            // no vote: nobody has voted, and the receiver has no choice
            PresenceMsg no_vote = base;
            no_vote.vote_seat = 255;
            no_vote.votes_continue = 0;
            no_vote.your_vote = 0;
            ASSERT_TRUE(decode(encode(no_vote), back) && back.vote_seat == 255);
            no_vote.votes_continue = 1;
            ASSERT_FALSE(decode(encode(no_vote), back));
            no_vote.votes_continue = 0;
            no_vote.your_vote = 1;
            ASSERT_FALSE(decode(encode(no_vote), back));
            // the vote may be about any seat of the match (a seat that flaps is the subject whatever its state), listed or not; only a seat that does not exist is refused
            PresenceMsg catching_first;
            catching_first.missing = {{3, PresenceMsg::State::CatchingUp, 90, 10}, {1, PresenceMsg::State::Absent, 60, 0}, {0, PresenceMsg::State::Absent, 40, 0}};
            catching_first.voters = 1;
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}}) {
                catching_first.vote_seat = seat;
                ASSERT_TRUE(decode(encode(catching_first), back) && back.vote_seat == seat);
            }
            catching_first.vote_seat = 4;
            ASSERT_FALSE(decode(encode(catching_first), back));
            // the resume countdown: 1 .. 60 seconds, only while nobody is missing; the vote may go on during it (a seat that flaps and is back)
            PresenceMsg count;
            count.voters = 3;
            count.cap_s = 1234;
            for (unsigned secs = 0; secs <= 60; ++secs) {
                count.resume_s = static_cast<uint8_t>(secs);
                const std::vector<uint8_t> b = encode(count);
                ASSERT_TRUE(b.size() == 1 + 1 + 4 + 2 + 1 && b.back() == secs);
                ASSERT_TRUE(decode(b, back) && back.resume_s == secs && encode(back) == b);
            }
            for (unsigned secs = 61; secs < 256; ++secs) {
                count.resume_s = static_cast<uint8_t>(secs);
                ASSERT_FALSE(decode(encode(count), back));                          // more than a minute is not a countdown that a server makes
            }
            count.resume_s = 10;
            count.vote_seat = 2;
            count.votes_continue = 1;
            count.voters = 2;
            ASSERT_TRUE(decode(encode(count), back) && back.resume_s == 10 && back.vote_seat == 2);
            count.missing = {{1, PresenceMsg::State::Absent, 5, 0}};                    // somebody is missing again: no countdown runs
            ASSERT_FALSE(decode(encode(count), back));
            count.missing = {{1, PresenceMsg::State::CatchingUp, 5, 50}};
            ASSERT_FALSE(decode(encode(count), back));
            count.resume_s = 0;
            ASSERT_TRUE(decode(encode(count), back) && back.resume_s == 0);
            // four missing (everybody), all catching up
            PresenceMsg all_four;
            for (uint8_t s = 0; s < 4; ++s) all_four.missing.push_back({s, PresenceMsg::State::CatchingUp, static_cast<uint16_t>(100 - s), static_cast<uint8_t>(s * 10)});
            ASSERT_TRUE(decode(encode(all_four), back) && back.missing.size() == 4);
            all_four.vote_seat = 0;                                      // (a seat that flaps may be the subject of a vote in any state: the decoder takes it)
            ASSERT_TRUE(decode(encode(all_four), back) && back.vote_seat == 0);
            // the encoder never writes more than four entries
            PresenceMsg crowd = all_four;
            crowd.vote_seat = 255;
            crowd.missing.push_back({0, PresenceMsg::State::Absent, 1, 0});
            ASSERT_EQ(encode(crowd)[1], 4);
        }

        // ---- Vote ----
        {
            for (uint8_t seat = 0; seat < 4; ++seat) {
                for (const bool cont : {false, true}) {
                    VoteMsg v;
                    v.seat = seat;
                    v.continue_without = cont;
                    const std::vector<uint8_t> bytes = encode(v);
                    ASSERT_TRUE(bytes.size() == 3 && bytes[0] == 27 && bytes[1] == seat && bytes[2] == (cont ? 1 : 0));
                    VoteMsg back;
                    ASSERT_TRUE(decode(bytes, back) && back.seat == seat && back.continue_without == cont);
                }
            }
            VoteMsg back;
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 4, 0}, back));       // a seat that does not exist
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 255, 0}, back));
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 0, 2}, back));       // a choice that is neither
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 0}, back));          // short, long
            ASSERT_FALSE(decode(std::vector<uint8_t>{27}, back));
            ASSERT_FALSE(decode(std::vector<uint8_t>{27, 0, 1, 0}, back));
            ASSERT_FALSE(decode(std::vector<uint8_t>{26, 0, 1}, back));       // another type
            ASSERT_FALSE(any_decodes(std::vector<uint8_t>{27, 0}) || any_decodes(std::vector<uint8_t>{27, 0, 1, 0}) || any_decodes(std::vector<uint8_t>{27, 4, 0}));
        }

        // ---- CatchUp ----
        {
            CatchUpMsg c;
            c.first_turn = 0x01020304;
            c.total_turns = 0x05060708;
            const std::vector<uint8_t> bytes = encode(c);
            ASSERT_TRUE(bytes.size() == 9 && bytes[0] == 28 && bytes[1] == 4 && bytes[4] == 1 && bytes[5] == 8 && bytes[8] == 5);
            CatchUpMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.first_turn == 0x01020304 && back.total_turns == 0x05060708);
            c.first_turn = c.total_turns;                                      // nothing to stream: the client has everything
            ASSERT_TRUE(decode(encode(c), back) && back.first_turn == back.total_turns);
            c.first_turn = 0;
            c.total_turns = 0;
            ASSERT_TRUE(decode(encode(c), back));
            c.first_turn = 0xFFFFFFFFu;
            c.total_turns = 0xFFFFFFFFu;
            ASSERT_TRUE(decode(encode(c), back));
            c.first_turn = 6;                                                  // the stream cannot start after its end
            c.total_turns = 5;
            ASSERT_FALSE(decode(encode(c), back));
            c.first_turn = 0xFFFFFFFFu;
            c.total_turns = 0;
            ASSERT_FALSE(decode(encode(c), back));
            for (size_t keep = 0; keep < bytes.size(); ++keep) ASSERT_FALSE(any_decodes(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep))));
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
        }

        // ---- CaughtUp ----
        {
            CaughtUpMsg c;
            c.turns = 36000;
            c.hash = {0x1111111111111111ull, 2, 3, 4, 5, 6, 7, 0x8888888888888888ull};
            const std::vector<uint8_t> bytes = encode(c);
            ASSERT_TRUE(bytes.size() == 1 + 4 + 64 && bytes[0] == 30 && bytes[1] == (36000 & 0xFF) && bytes[2] == ((36000 >> 8) & 0xFF) && bytes[5] == 0x11 && bytes[bytes.size() - 1] == 0x88);
            CaughtUpMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.turns == 36000 && back.hash == c.hash);
            ASSERT_TRUE(encode(back) == bytes);
            for (size_t keep = 0; keep < bytes.size(); ++keep) {
                const std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
        }

        // ---- TurnBatch ----
        {
            auto pack = [](const std::vector<TurnMsg>& turns) {                // the packing by hand: u16 count, the commands (not TurnLog's: that is N2.41's to prove the same)
                std::vector<uint8_t> out;
                for (const TurnMsg& t : turns) {
                    out.push_back(static_cast<uint8_t>(t.commands.size() & 0xFF));
                    out.push_back(static_cast<uint8_t>(t.commands.size() >> 8));
                    for (const Command& c : t.commands) sim::encode(c, out);
                }
                return out;
            };
            TurnBatchMsg b;
            b.first_turn = 0x00010203;
            b.turns.resize(4);
            b.turns[0].commands = {cmd(CommandType::GroupMove, 0, 255, 1, 2, {1}), cmd(CommandType::Hatch, 1), cmd(CommandType::AllianceInvite, 2, 3)};
            b.turns[1].commands = {};
            std::vector<uint32_t> thirty_two;
            for (uint32_t i = 0; i < 32; ++i) thirty_two.push_back(1000 + i);
            b.turns[2].commands = {cmd(CommandType::GroupAttack, 3, 255, 59, 59, thirty_two)};
            b.turns[3].commands = {cmd(CommandType::Stop, 1, 255, 0, 0, {7, 8}), cmd(CommandType::Drop, 2)};
            for (uint32_t i = 0; i < 4; ++i) b.turns[i].turn = b.first_turn + i;
            const std::vector<uint8_t> bytes = encode(b);
            const std::vector<uint8_t> packed = pack(b.turns);
            ASSERT_EQ(bytes.size(), kBatchHeaderBytes + packed.size());
            ASSERT_TRUE(bytes[0] == 29 && bytes[1] == 3 && bytes[2] == 2 && bytes[3] == 1 && bytes[4] == 0 && bytes[5] == 4 && bytes[6] == 0);
            ASSERT_TRUE(std::equal(packed.begin(), packed.end(), bytes.begin() + static_cast<std::ptrdiff_t>(kBatchHeaderBytes)));
            TurnBatchMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.first_turn == b.first_turn && back.turns.size() == 4);
            for (uint32_t i = 0; i < 4; ++i) ASSERT_TRUE(back.turns[i].turn == b.first_turn + i && back.turns[i].commands == b.turns[i].commands);
            ASSERT_TRUE(encode(back) == bytes);
            // the two encoders agree: from the turns, and from the turns packed already
            ASSERT_TRUE(encode_turn_batch_packed(b.first_turn, 4, packed.data(), packed.size()) == bytes);
            ASSERT_TRUE(encode_turn_batch_packed(0, 0, packed.data(), packed.size()).empty());            // no turn: no message
            ASSERT_TRUE(encode_turn_batch_packed(0, static_cast<uint32_t>(kMaxBatchTurns) + 1, packed.data(), packed.size()).empty());
            ASSERT_TRUE(encode_turn_batch_packed(5, 1, nullptr, 0).size() == kBatchHeaderBytes);          // (a count that its bytes do not match is the caller's: this is no message that decodes)
            ASSERT_FALSE(decode(encode_turn_batch_packed(5, 1, nullptr, 0), back));
            Lcg rng(40);
            for (int round = 0; round < 200; ++round) {                        // random batches: both ways round give the same bytes, and decoding gives the turns back
                TurnBatchMsg r;
                r.first_turn = rng.below(1000000);
                const uint32_t count = 1 + rng.below(40);
                r.turns.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                    r.turns[i].turn = r.first_turn + i;
                    const uint32_t n = rng.below(4) == 0 ? rng.below(6) : 0;
                    for (uint32_t k = 0; k < n; ++k) {
                        const uint8_t issuer = static_cast<uint8_t>(rng.below(4));
                        if (rng.below(3) == 0) {
                            r.turns[i].commands.push_back(cmd(CommandType::Hatch, issuer));
                        } else {
                            std::vector<uint32_t> ants;
                            for (uint32_t a = 1 + rng.below(32); a > 0; --a) ants.push_back(rng.below(100000));
                            r.turns[i].commands.push_back(cmd(CommandType::GroupMove, issuer, 255, static_cast<int16_t>(rng.below(60)), static_cast<int16_t>(rng.below(60)), ants));
                        }
                    }
                }
                const std::vector<uint8_t> a = encode(r);
                const std::vector<uint8_t> p = pack(r.turns);
                ASSERT_TRUE(encode_turn_batch_packed(r.first_turn, count, p.data(), p.size()) == a);
                TurnBatchMsg again;
                ASSERT_TRUE(decode(a, again) && again.first_turn == r.first_turn && again.turns.size() == count);
                for (uint32_t i = 0; i < count; ++i) ASSERT_TRUE(again.turns[i].turn == r.turns[i].turn && again.turns[i].commands == r.turns[i].commands);
            }
            // every truncation (a turn that ends inside the buffer, a count that promises more, a half command) and a byte too many
            for (size_t keep = 0; keep < bytes.size(); ++keep) {
                const std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
                ASSERT_FALSE(decode(shorter, back));
                ASSERT_FALSE(any_decodes(shorter));
            }
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            longer = bytes;
            longer.insert(longer.end(), {0, 0});                               // the bytes of another (empty) turn that the count does not promise
            ASSERT_FALSE(decode(longer, back));
            // the count: 0 and 4097 are refused; 4096 (empty turns: 2 bytes each) is the largest
            {
                std::vector<uint8_t> empty_turns = {29, 0, 0, 0, 0, 0, 0};
                ASSERT_FALSE(decode(empty_turns, back));                       // count 0
                TurnBatchMsg full;
                full.first_turn = 7;
                full.turns.resize(kMaxBatchTurns);
                const std::vector<uint8_t> big = encode(full);
                ASSERT_EQ(big.size(), kBatchHeaderBytes + 2 * kMaxBatchTurns);
                ASSERT_TRUE(decode(big, back) && back.turns.size() == kMaxBatchTurns && back.turns.back().turn == 7 + kMaxBatchTurns - 1);
                std::vector<uint8_t> too_many = big;                           // count 4097 and the 2 bytes of the extra turn that it promises
                too_many[5] = static_cast<uint8_t>((kMaxBatchTurns + 1) & 0xFF);
                too_many[6] = static_cast<uint8_t>((kMaxBatchTurns + 1) >> 8);
                too_many.insert(too_many.end(), {0, 0});
                ASSERT_FALSE(decode(too_many, back));
                full.turns.resize(kMaxBatchTurns + 5);                         // the encoder cuts what the decoder would refuse
                ASSERT_EQ(encode(full).size(), big.size());
            }
            // the commands of a turn: at most kMaxTurnCommands (512), written as a count that is checked before any command is read
            {
                TurnBatchMsg five_twelve;
                five_twelve.turns.resize(1);
                for (size_t i = 0; i < kMaxTurnCommands; ++i) five_twelve.turns[0].commands.push_back(cmd(CommandType::Hatch, static_cast<uint8_t>(i % 4)));
                const std::vector<uint8_t> ok = encode(five_twelve);
                ASSERT_TRUE(decode(ok, back) && back.turns[0].commands.size() == kMaxTurnCommands);
                std::vector<uint8_t> over = ok;                                // 513: the count and the command that it promises
                over[kBatchHeaderBytes] = static_cast<uint8_t>((kMaxTurnCommands + 1) & 0xFF);
                over[kBatchHeaderBytes + 1] = static_cast<uint8_t>((kMaxTurnCommands + 1) >> 8);
                sim::encode(cmd(CommandType::Hatch, 0), over);
                ASSERT_FALSE(decode(over, back));
                five_twelve.turns[0].commands.push_back(cmd(CommandType::Hatch, 0));    // (the encoder cuts at 512)
                ASSERT_EQ(encode(five_twelve).size(), ok.size());
                std::vector<uint8_t> lie = ok;                                 // a count of 65535 and a few bytes
                lie[kBatchHeaderBytes] = 0xFF;
                lie[kBatchHeaderBytes + 1] = 0xFF;
                ASSERT_FALSE(decode(lie, back));
            }
            // a command that is no command (unknown type, 33 ants, an ant list on a Hatch) inside a batch
            {
                std::vector<uint8_t> bad = {29, 1, 0, 0, 0, 1, 0, 1, 0};       // one turn, one command
                const std::vector<uint8_t> good_command = {static_cast<uint8_t>(CommandType::Hatch), 0, 255, 0, 0, 0, 0, 0};
                std::vector<uint8_t> v = bad;
                v.insert(v.end(), good_command.begin(), good_command.end());
                ASSERT_TRUE(decode(v, back));
                v[kBatchHeaderBytes + 2] = 0;                                  // type None
                ASSERT_FALSE(decode(v, back));
                v[kBatchHeaderBytes + 2] = 99;                                 // unknown type
                ASSERT_FALSE(decode(v, back));
                v[kBatchHeaderBytes + 2] = static_cast<uint8_t>(CommandType::Hatch);
                v[kBatchHeaderBytes + 2 + 7] = 1;                              // a Hatch with an ant list
                v.insert(v.end(), {1, 0, 0, 0});
                ASSERT_FALSE(decode(v, back));
                std::vector<uint8_t> w = bad;
                w.insert(w.end(), {static_cast<uint8_t>(CommandType::GroupMove), 0, 255, 0, 0, 0, 0, 33});     // 33 ants
                w.insert(w.end(), 33 * 4, 0);
                ASSERT_FALSE(decode(w, back));
            }
            // the turn numbers must not wrap: first_turn + count - 1 is at most 0xFFFFFFFF
            {
                TurnBatchMsg edge;
                edge.first_turn = 0xFFFFFFFFu - 2;
                edge.turns.resize(3);
                ASSERT_TRUE(decode(encode(edge), back) && back.turns[2].turn == 0xFFFFFFFFu);
                edge.first_turn = 0xFFFFFFFFu - 1;                             // three turns from here would end past the last number
                ASSERT_FALSE(decode(encode(edge), back));
                edge.first_turn = 0xFFFFFFFFu;
                edge.turns.resize(1);
                ASSERT_TRUE(decode(encode(edge), back) && back.turns[0].turn == 0xFFFFFFFFu);
                edge.turns.resize(2);
                ASSERT_FALSE(decode(encode(edge), back));
            }
            // the size of the message: the largest that fits (65,535 bytes: a batch is 7 + an even number) is taken, one more is not
            {
                TurnBatchMsg fill;
                size_t size = kBatchHeaderBytes;
                uint32_t number = 0;
                std::vector<uint32_t> thirty_two_ants;
                for (uint32_t i = 0; i < 32; ++i) thirty_two_ants.push_back(i);
                const size_t big_turn = 2 + sim::kCommandHeaderBytes + 4 * 32;
                while (kMaxMessageBytes - 1 - size >= big_turn + 10) {
                    TurnMsg t;
                    t.turn = number++;
                    t.commands = {cmd(CommandType::GroupMove, 0, 255, 1, 1, thirty_two_ants)};
                    fill.turns.push_back(std::move(t));
                    size += big_turn;
                }
                // the rest, an even number of bytes, in turns of 2 + 8 h bytes: j turns with the last one holding the Hatch commands
                const size_t rest = kMaxMessageBytes - 1 - size;
                ASSERT_EQ(rest % 2, size_t{0});
                size_t j = 0;
                for (size_t k = 1; k <= 4 && j == 0; ++k) {
                    if (rest >= 2 * k && (rest - 2 * k) % 8 == 0) j = k;
                }
                ASSERT_TRUE(j != 0);
                for (size_t k = 0; k < j; ++k) {
                    TurnMsg t;
                    t.turn = number++;
                    if (k + 1 == j) {
                        for (size_t h = 0; h < (rest - 2 * j) / 8; ++h) t.commands.push_back(cmd(CommandType::Hatch, 0));
                    }
                    fill.turns.push_back(std::move(t));
                }
                const std::vector<uint8_t> largest = encode(fill);
                ASSERT_EQ(largest.size(), kMaxMessageBytes - 1);
                ASSERT_TRUE(decode(largest, back) && back.turns.size() == fill.turns.size());
                fill.turns.push_back(TurnMsg{});                               // two more bytes: 65,537
                const std::vector<uint8_t> too_big = encode(fill);
                ASSERT_EQ(too_big.size(), kMaxMessageBytes + 1);
                ASSERT_FALSE(decode(too_big, back));
                ASSERT_FALSE(any_decodes(too_big));
            }
        }
    } TEST_END();
}

void run_network_tests() {
    TEST_CASE("N2.4 Loopback Network: Latency Is Respected, Delivery Is Ordered Even With Jitter, Closing Is Seen On Both Ends") {
        LoopbackNetwork net(5);
        auto ends = net.connect({50, 40});
        std::vector<uint8_t> m;
        std::vector<std::pair<uint8_t, uint32_t>> arrivals;         // (message, arrival time)
        std::vector<uint32_t> sent_at(100, 0);
        uint8_t sent = 0;
        for (uint32_t t = 0; t <= 1500; ++t) {
            net.set_time(t);
            if (t % 3 == 0 && sent < 100) {
                sent_at[sent] = t;
                ASSERT_TRUE(ends.first->send({sent}));
                ++sent;
            }
            while (ends.second->poll(m)) arrivals.emplace_back(m[0], t);
        }
        ASSERT_EQ(arrivals.size(), 100u);
        for (size_t i = 0; i < arrivals.size(); ++i) {
            ASSERT_EQ(arrivals[i].first, i);                                   // in order
            ASSERT_TRUE(arrivals[i].second >= sent_at[i] + 50);                // never before the latency
            ASSERT_TRUE(arrivals[i].second <= sent_at[i] + 50 + 40 + 40 * 33); // (a slow message may hold up later ones, but not for long)
        }
        // the other direction, and a message sent now is not there yet
        net.set_time(2000);
        ASSERT_TRUE(ends.second->send({42}));
        ASSERT_FALSE(ends.first->poll(m));
        net.set_time(2000 + 50 + 40);
        ASSERT_TRUE(ends.first->poll(m) && m[0] == 42);
        // close
        ends.first->close();
        ASSERT_EQ(ends.second->state(), Connection::State::Closed);
        ASSERT_FALSE(ends.second->send({1}));
        LoopbackNetwork net2;
        auto e2 = net2.connect({10, 0});
        net2.cut(e2.first, true);
        ASSERT_EQ(e2.second->state(), Connection::State::Failed);
    } TEST_END();
}

void run_sequencer_tests() {
    TEST_CASE("N2.5 Sequencer: The Connection, Not The Payload, Decides The Issuer; Canonical Order; Inactive Slots And Floods Are Refused") {
        Sequencer seq;
        seq.set_host_player(0);
        seq.set_active(0, true);
        seq.set_active(1, true);
        seq.set_active(2, true);
        // player 1 claims to be player 2
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 2, 255, 1, 1, {5})));
        ASSERT_TRUE(seq.submit(2, cmd(CommandType::Hatch, 2)));
        ASSERT_TRUE(seq.submit(0, cmd(CommandType::Stop, 0, 255, 0, 0, {1})));
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupAttack, 0, 255, 2, 2, {6})));
        ASSERT_FALSE(seq.submit(3, cmd(CommandType::Hatch, 3)));                       // slot 3 is not part of the match
        ASSERT_FALSE(seq.submit(4, cmd(CommandType::Hatch, 3)));
        ASSERT_FALSE(seq.submit(1, cmd(CommandType::None, 1)));
        const TurnMsg t = seq.seal();
        ASSERT_EQ(t.turn, 0u);
        ASSERT_EQ(t.commands.size(), 4u);
        ASSERT_EQ(t.commands[0].issuer, 0);
        ASSERT_EQ(t.commands[1].issuer, 1);
        ASSERT_EQ(t.commands[2].issuer, 1);
        ASSERT_EQ(t.commands[3].issuer, 2);
        ASSERT_EQ(t.commands[1].type, CommandType::GroupMove);       // player 1's commands keep their order
        ASSERT_EQ(t.commands[2].type, CommandType::GroupAttack);
        ASSERT_EQ(seq.seal().turn, 1u);                              // turns are numbered without gaps
        // flooding: max_commands_per_turn per peer and turn; what does not fit waits for a later turn, up to max_carried_commands; a peer with more than that waiting is refused
        Sequencer::Config cfg;
        cfg.max_commands_per_turn = 3;
        cfg.max_carried_commands = 4;
        Sequencer small(cfg);
        small.set_active(1, true);
        int taken = 0;
        for (int i = 0; i < 10; ++i) taken += small.submit(1, cmd(CommandType::Hatch, 1)) ? 1 : 0;
        ASSERT_EQ(taken, 7);                                           // 3 for the turn and 4 that wait
        ASSERT_EQ(small.seal().commands.size(), 3u);
        ASSERT_EQ(small.carried(1), 1u);                               // (three of the four moved into the next turn, one still waits)
        ASSERT_TRUE(small.submit(1, cmd(CommandType::Hatch, 1)));      // and there is room to wait again
        ASSERT_EQ(small.seal().commands.size(), 3u);
    } TEST_END();

    TEST_CASE("N2.5b Sequencer: Commands Beyond The Turn's Quota Are Carried Into The Next Turns In The Order They Came (A Stuck Uplink That Is Let Go Delivers 76 Orders At Once: They Are Not A Flood), Nobody Else's Quota Is Touched, Only More Than Sixteen Turns' Worth Waiting Is Refused") {
        Sequencer seq;                                                 // the defaults: 64 per turn, 1,024 waiting
        seq.set_active(0, true);
        seq.set_active(1, true);
        for (int i = 0; i < 76; ++i) ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, static_cast<int16_t>(i), 0, {7})));   // order i goes to the tile x = i
        ASSERT_EQ(seq.queued(), 64u);
        ASSERT_EQ(seq.carried(1), 12u);
        ASSERT_TRUE(seq.submit(0, cmd(CommandType::Hatch, 0)));        // another player's command is not behind them
        ASSERT_EQ(seq.carried(0), 0u);
        const TurnMsg first = seq.seal();
        ASSERT_EQ(first.commands.size(), 65u);                         // 64 of player 1 and the one of player 0
        ASSERT_EQ(first.commands[0].issuer, 0);                        // (canonical order: by issuer)
        for (int i = 0; i < 64; ++i) {
            ASSERT_EQ(first.commands[static_cast<size_t>(i) + 1].issuer, 1);
            ASSERT_EQ(first.commands[static_cast<size_t>(i) + 1].tile_x, i);               // in the order they came
        }
        ASSERT_EQ(seq.carried(1), 0u);                                 // the other twelve are the first commands of the next turn
        ASSERT_EQ(seq.queued(), 12u);
        // a command that comes now goes behind the twelve that wait, not before them
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, 100, 0, {7})));
        const TurnMsg second = seq.seal();
        ASSERT_EQ(second.commands.size(), 13u);
        for (int i = 0; i < 12; ++i) ASSERT_EQ(second.commands[static_cast<size_t>(i)].tile_x, 64 + i);
        ASSERT_EQ(second.commands[12].tile_x, 100);
        // a burst that fills the turn and the carry: 64 + 1,024 are taken, the rest refused; nobody's quota is shared
        Sequencer flood;
        flood.set_active(1, true);
        flood.set_active(2, true);
        int taken = 0;
        for (int i = 0; i < 1200; ++i) taken += flood.submit(1, cmd(CommandType::Hatch, 1)) ? 1 : 0;
        ASSERT_EQ(taken, 64 + 1024);
        ASSERT_EQ(flood.carried(1), 1024u);
        ASSERT_TRUE(flood.submit(2, cmd(CommandType::Hatch, 2)));      // player 2 is not affected by player 1's flood
        size_t total = 0;
        uint32_t turns = 0;
        while (flood.carried(1) > 0 || flood.queued() > 0) {           // the 1,089 commands take seventeen turns of 64, in order, none lost
            const TurnMsg t = flood.seal();
            total += t.commands.size();
            ++turns;
            ASSERT_TRUE(turns <= 17);
        }
        ASSERT_EQ(total, size_t{64 + 1024 + 1});
        // a player that is gone has no commands waiting, and a host that resumes forgets them
        Sequencer gone;
        gone.set_active(1, true);
        for (int i = 0; i < 70; ++i) gone.submit(1, cmd(CommandType::Hatch, 1));
        ASSERT_EQ(gone.carried(1), 6u);
        gone.set_active(1, false);
        ASSERT_EQ(gone.carried(1), 0u);
        Sequencer moved;
        moved.set_active(1, true);
        for (int i = 0; i < 70; ++i) moved.submit(1, cmd(CommandType::Hatch, 1));
        moved.resume(500);
        ASSERT_TRUE(moved.carried(1) == 0 && moved.queued() == 0);
        ASSERT_FALSE(moved.submit(1, cmd(CommandType::Hatch, 1)));     // (nobody is active after a resume)
        ASSERT_EQ(moved.carried(1), 0u);
    } TEST_END();

    TEST_CASE("N2.5c Sequencer: A Big Selection Is One Click Of Up To 48 Commands Of 32 Ants (The HUD's Budget, 1,536 Ants): They Reach One Turn In The Order They Were Sent, Beside The Player's Other Commands, Nothing Is Carried, And The Turn Goes Over The Wire Whole") {
        Sequencer seq;                                                 // the defaults: 64 per turn
        seq.set_active(0, true);
        seq.set_active(1, true);
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::Hatch, 1)));        // a command of the player's before the click
        ASSERT_TRUE(seq.submit(0, cmd(CommandType::Hatch, 0)));        // and one of another player in the middle of it
        for (uint32_t i = 0; i < 48; ++i) {
            std::vector<uint32_t> ants;
            for (uint32_t k = 0; k < sim::kMaxCommandAnts; ++k) ants.push_back(1 + i * 32 + k);
            ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, 20, 30, ants)));
            if (i == 20) ASSERT_TRUE(seq.submit(0, cmd(CommandType::Stop, 0, 255, 0, 0, {7})));
        }
        for (int i = 0; i < 15; ++i) ASSERT_TRUE(seq.submit(1, cmd(CommandType::Hatch, 1)));      // 1 + 48 + 15 = 64: the turn is full and nothing waits
        ASSERT_EQ(seq.carried(1), 0u);
        const TurnMsg turn = seq.seal();
        ASSERT_EQ(turn.commands.size(), size_t{2 + 1 + 48 + 15});
        ASSERT_EQ(turn.commands[0].issuer, 0);                         // (canonical order: the lower issuer first, each issuer's commands as they came)
        ASSERT_EQ(turn.commands[1].issuer, 0);
        ASSERT_EQ(turn.commands[2].type, CommandType::Hatch);
        for (uint32_t i = 0; i < 48; ++i) {
            const Command& c = turn.commands[3 + i];
            ASSERT_TRUE(c.issuer == 1 && c.type == CommandType::GroupMove && c.ants.size() == sim::kMaxCommandAnts && c.ants.front() == 1 + i * 32);
        }
        TurnMsg decoded;
        ASSERT_TRUE(decode(encode(turn), decoded));
        ASSERT_TRUE(decoded.commands == turn.commands);
        // one more command than the quota carries into the next turn, behind the others (a click that came on top of a full turn is not lost)
        Sequencer full;
        full.set_active(1, true);
        for (int i = 0; i < 16; ++i) ASSERT_TRUE(full.submit(1, cmd(CommandType::Hatch, 1)));
        for (uint32_t i = 0; i < 48; ++i) ASSERT_TRUE(full.submit(1, cmd(CommandType::GroupMove, 1, 255, 20, 30, {i + 1})));
        for (int i = 0; i < 16; ++i) ASSERT_TRUE(full.submit(1, cmd(CommandType::Hatch, 1)));
        ASSERT_EQ(full.carried(1), 16u);
        ASSERT_EQ(full.seal().commands.size(), 64u);
        const TurnMsg next = full.seal();
        ASSERT_TRUE(next.commands.size() == 16 && next.commands[0].type == CommandType::Hatch);
    } TEST_END();

    TEST_CASE("N2.5d Sequencer: Ten Big Clicks Of 48 Commands Each Arrive Before A Single Turn Is Sealed (A Stuck Uplink That Is Let Go): None Is Refused, So The Player Has No Violation And Keeps Its Seat, And They All Run In The Order They Were Sent, 64 To A Turn") {
        Sequencer seq;                                                 // the defaults
        seq.set_active(1, true);
        uint32_t refused = 0;
        for (uint32_t click = 0; click < 10; ++click) {
            for (uint32_t i = 0; i < 48; ++i) refused += seq.submit(1, cmd(CommandType::Stop, 1, 255, 0, 0, {1 + click * 48 + i})) ? 0u : 1u;
        }
        ASSERT_EQ(refused, 0u);                                        // (a refusal is a violation, and eight of them drop the player: Sequencer::submit false, HostSession::handle_message)
        ASSERT_EQ(seq.carried(1), 480u - 64u);
        uint32_t next = 1;
        uint32_t turns = 0;
        while (seq.carried(1) > 0 || seq.queued() > 0) {
            const TurnMsg t = seq.seal();
            ASSERT_TRUE(t.commands.size() <= 64);
            for (const Command& c : t.commands) ASSERT_EQ(c.ants.front(), next++);
            ++turns;
            ASSERT_TRUE(turns <= 8);
        }
        ASSERT_EQ(next, 481u);                                         // all 480, in order, in eight turns
        // the same for ten move clicks of 693 ants each (22 commands: 21 of 32 ants and one of 21), none of them refused either
        Sequencer moves;
        moves.set_active(1, true);
        uint32_t refused_moves = 0;
        for (uint32_t click = 0; click < 10; ++click) {
            for (uint32_t i = 0; i < 22; ++i) {
                std::vector<uint32_t> ants;
                for (uint32_t k = 0; k < (i < 21 ? sim::kMaxCommandAnts : 693u - 21 * 32); ++k) ants.push_back(1 + i * 32 + k);
                refused_moves += moves.submit(1, cmd(CommandType::GroupMove, 1, 255, 20, 30, ants)) ? 0u : 1u;
            }
        }
        ASSERT_EQ(refused_moves, 0u);
        ASSERT_EQ(moves.carried(1), 220u - 64u);
        uint32_t move_turns = 0;
        size_t move_commands = 0;
        while (moves.carried(1) > 0 || moves.queued() > 0) {
            move_commands += moves.seal().commands.size();
            ASSERT_TRUE(++move_turns <= 4);                              // (220 commands: four turns of 64 at the most)
        }
        ASSERT_EQ(move_commands, size_t{220});                         // (the 64 that were queued and the 156 that waited)
    } TEST_END();

    TEST_CASE("N2.6 Sequencer: Flow Control Stalls Sealing While A Peer Lags, Acks Only Move Forward, Reports Are Pruned") {
        Sequencer::Config cfg;
        cfg.max_lag_turns = 5;
        Sequencer seq(cfg);
        seq.set_host_player(0);
        seq.set_active(0, true);
        seq.set_active(1, true);
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(seq.can_seal());
            seq.seal();
        }
        ASSERT_TRUE(seq.can_seal());                                   // lag 5 is still allowed
        seq.seal();                                                    // lag 6
        ASSERT_FALSE(seq.can_seal());
        ASSERT_EQ(seq.laggard(), 0);                                   // nobody acked yet: player 0 is the furthest behind (first in order)
        seq.on_ack(0, 5);
        ASSERT_EQ(seq.laggard(), 1);
        seq.on_ack(1, 2);                                              // turn 2 executed: needs 3 next, lag 3
        ASSERT_TRUE(seq.can_seal());
        seq.on_ack(1, 0);                                              // an old ack changes nothing
        ASSERT_EQ(seq.acked(1), 3u);
        seq.on_ack(1, 1000);                                           // an ack for a turn that was never sealed is clamped
        ASSERT_EQ(seq.acked(1), seq.next_turn());
        // a peer that joins late is level with the sequencer
        seq.set_active(2, true);
        ASSERT_EQ(seq.acked(2), seq.next_turn());
    } TEST_END();

    TEST_CASE("N2.7 Sequencer: Hash Reports Are Compared With The Host's; A Mismatch Is Named, Equal Reports Are Silent, Order Does Not Matter") {
        Sequencer seq;
        seq.set_host_player(0);
        for (uint8_t p = 0; p < 3; ++p) seq.set_active(p, true);
        for (int i = 0; i < 40; ++i) seq.seal();                       // turns 0 .. 39 exist (a report for a turn that was never sealed is dropped)
        const sim::StateHash good{10, 1, 2, 3, 4, 5, 6, 7};
        sim::StateHash bad = good;
        bad.total = 11;
        bad.ants = 55;
        ASSERT_TRUE(seq.on_hash(1, 9, good).empty());                  // the host's report is not in yet
        ASSERT_TRUE(seq.on_hash(2, 9, bad).empty());
        const auto d = seq.on_hash(0, 9, good);                        // now it is: player 2 differs
        ASSERT_EQ(d.size(), 1u);
        ASSERT_EQ(d[0].player, 2);
        ASSERT_EQ(d[0].turn, 9u);
        ASSERT_TRUE(d[0].host == good && d[0].peer == bad);
        ASSERT_TRUE(d[0].peer.ants != d[0].host.ants && d[0].peer.grid == d[0].host.grid);   // which subsystem
        ASSERT_TRUE(seq.on_hash(0, 19, good).empty());                 // host first
        ASSERT_TRUE(seq.on_hash(1, 19, good).empty());
        ASSERT_EQ(seq.on_hash(2, 19, bad).size(), 1u);
        ASSERT_TRUE(seq.on_hash(3, 29, good).empty());                 // an inactive slot is ignored
    } TEST_END();
}

void run_flood_budget_tests() {
    TEST_CASE("N2.37 Flood Control: The Message Budget Is A Token Bucket (1000 A Second, A Burst Of 1000) That Is Exact To The Millisecond, Also Across The Wrap Of The 32-Bit Clock; Honest Rates Never Meet It And Floods Always Do") {
        {   // the burst, then nothing at the same instant; one millisecond gives one message back; a long pause fills it to the burst and no further
            MessageBudget b;
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(5000));
            ASSERT_FALSE(b.take(5000));
            ASSERT_FALSE(b.take(5000));
            ASSERT_TRUE(b.take(5001));
            ASSERT_FALSE(b.take(5001));
            ASSERT_TRUE(b.take(5002));
            ASSERT_FALSE(b.take(5002));
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(60000));
            ASSERT_FALSE(b.take(60000));
            ASSERT_FALSE(b.take(60000));
        }
        {   // the first message finds a full bucket whatever the clock says (a server that has just started, the signed half of the clock, the wrap), and 500 ms give back 500
            for (uint32_t start : {0u, 1u, 999u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFC00u, 0xFFFFFFFFu}) {
                MessageBudget b;
                for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(start));
                ASSERT_FALSE(b.take(start));
                const uint32_t later = start + 500u;                        // (wraps for the last two)
                for (uint32_t i = 0; i < 500; ++i) ASSERT_TRUE(b.take(later));
                ASSERT_FALSE(b.take(later));
            }
        }
        {   // other settings: a burst of 3, 10 a second (100 ms for one message; 99 ms are not enough)
            MessageBudget b;
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(b.take(100, 3, 10));
            ASSERT_FALSE(b.take(100, 3, 10));
            ASSERT_FALSE(b.take(199, 3, 10));
            ASSERT_TRUE(b.take(200, 3, 10));
            ASSERT_FALSE(b.take(200, 3, 10));
        }
        {   // an hour at 60 (a person), 400, 900 and 1000 messages a second, spread evenly: never refused; at 1100 and 2000 a second the surplus is refused, nothing else
            for (uint32_t rate : {60u, 400u, 900u, 1000u, 1100u, 2000u}) {
                MessageBudget b;
                uint64_t refused = 0;
                uint32_t acc = 0;
                uint32_t now = 123456;
                for (uint32_t ms = 0; ms < 3600u * 1000u; ++ms) {
                    ++now;
                    acc += rate;
                    while (acc >= 1000) {
                        acc -= 1000;
                        if (!b.take(now)) ++refused;
                    }
                }
                if (rate <= kMessagesPerSecond) {
                    ASSERT_EQ(refused, 0u);
                } else {
                    const uint64_t offered = uint64_t{rate} * 3600u;
                    const uint64_t allowed = kMessageBurst + uint64_t{kMessagesPerSecond} * 3600u;
                    ASSERT_TRUE(refused + allowed >= offered - 2000u && refused + allowed <= offered + 2000u);
                }
            }
        }
        {   // three hours of silence give back no more than the burst
            MessageBudget b;
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(1000));
            const uint32_t later = 1000u + 3u * 3600u * 1000u;
            for (uint32_t i = 0; i < kMessageBurst; ++i) ASSERT_TRUE(b.take(later));
            ASSERT_FALSE(b.take(later));
        }
    } TEST_END();
}

void run_runner_tests() {
    TEST_CASE("N2.8 Lock-Step Runner: Collects A Buffer Before Starting, Runs One Tick Per Turn Every 50 ms, Applies A Turn's Commands Before Its Tick, Waits Without Debt And Runs A Long Queue Down Faster") {
        sim::SimulationEngine sim;
        const Ids ids = build_world(sim, 1);
        LockstepRunner runner(sim);
        auto turn = [](uint32_t n, std::vector<Command> cmds = {}) {
            TurnMsg t;
            t.turn = n;
            t.commands = std::move(cmds);
            return t;
        };
        ASSERT_FALSE(runner.on_turn(turn(1)));                                       // turns must start at 0 and have no gaps
        ASSERT_TRUE(runner.on_turn(turn(0, {cmd(CommandType::GroupMove, 0, 255, 20, 30, {ids.ants[0][0]})})));
        ASSERT_TRUE(runner.update(1000).empty());                                    // one turn is not enough to start: the buffer is one turn behind the turn that runs
        ASSERT_EQ(sim.current_tick(), 0u);
        ASSERT_EQ(runner.buffer_turns(), 1u);                                        // (a steady link: one turn of buffer, 50 ms)
        ASSERT_FALSE(runner.on_turn(turn(0)));                                       // a turn cannot repeat
        ASSERT_TRUE(runner.on_turn(turn(1)));
        auto done = runner.update(0);                                                // starting: the first tick is due at once, and a turn is one tick
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(done[0].turn, 0u);
        ASSERT_FALSE(done[0].has_hash);
        ASSERT_EQ(sim.current_tick(), 1u);
        ASSERT_EQ(sim.get_unit(ids.ants[0][0]).orig_order, sim::AntUnit::kOrderMove);   // the command was applied before that tick
        done = runner.update(49);
        ASSERT_TRUE(done.empty());
        ASSERT_EQ(sim.current_tick(), 1u);
        done = runner.update(1);                                                     // 50 ms: the next turn
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(done[0].turn, 1u);
        ASSERT_EQ(sim.current_tick(), 2u);
        ASSERT_FALSE(runner.stalled());
        // no turn queued although one is due: the runner waits at the boundary, and the buffer that the wait used up is collected again (the buffer's size is decided when the
        // late turn comes: a wait of up to 100 ms grows it by a turn, a longer one does not)
        for (int i = 0; i < 6; ++i) ASSERT_TRUE(runner.update(16).empty());
        ASSERT_TRUE(runner.stalled());
        ASSERT_TRUE(runner.rebuilding());
        ASSERT_EQ(runner.buffer_turns(), 1u);
        ASSERT_EQ(sim.current_tick(), 2u);
        const uint32_t waited = runner.stalled_ms();
        ASSERT_TRUE(waited >= 16 && waited <= 96);
        for (int i = 0; i < 100; ++i) runner.update(16);                             // a long wait is counted from the tick that was due (the message shows at one second)
        ASSERT_TRUE(runner.stalled_ms() >= 1000);
        for (uint32_t n = 2; n < 40; ++n) ASSERT_TRUE(runner.on_turn(turn(n)));
        // 38 turns queued: far more than the buffer: the first runs at once, then the queue is run down at up to four times normal speed, never faster
        done = runner.update(16);
        ASSERT_EQ(done.size(), 1u);
        ASSERT_EQ(runner.buffer_turns(), 1u);                                        // a bunch of 38 turns after a wait of 1.7 s: the freeze's own lateness, not the link's (it is left out, jitter.hpp): the buffer stays at one turn (the wait itself, over 150 ms, did not grow it either)
        ASSERT_FALSE(runner.stalled());
        ASSERT_EQ(runner.stalled_ms(), 0u);
        ASSERT_EQ(runner.speed_x4(), 16u);                                           // 37 queued, 3 wanted: the limit, 4x
        size_t hashes = 0;
        size_t ticks_in_50 = 0;
        uint32_t frames = 0;
        for (int i = 0; i < 400 && runner.queued() > 0; ++i) {
            const auto ran = runner.update(10);
            ASSERT_TRUE(ran.size() <= 1);                                            // 10 ms at 4x: not even one tick per frame is exceeded by more than one
            for (const auto& e : ran) hashes += e.has_hash ? 1 : 0;
            ticks_in_50 += ran.size();
            if (++frames % 5 == 0) {
                ASSERT_TRUE(ticks_in_50 <= 4 + 1);                                   // 50 ms of frames: at most four ticks (and one for the rounding of the quarter steps)
                ticks_in_50 = 0;
            }
        }
        for (const auto& e : runner.update(200)) hashes += e.has_hash ? 1 : 0;
        ASSERT_EQ(runner.next_turn_to_execute(), 40u);
        ASSERT_EQ(sim.current_tick(), 40u);                                          // a turn is a tick
        ASSERT_EQ(hashes, 2u);                                                       // turns 19 and 39: a hash every 20 turns (one second)
        ASSERT_TRUE(runner.stalled() == false || runner.queued() == 0);
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------

void run_match_tests() {
    TEST_CASE("N2.9 Match: A Host And Three Clients Over 25 - 120 ms Links With Jitter Stay Bit-Identical For 90 Seconds Of Play") {
        for (uint32_t seed : {1u, 2u}) {
            Match m(seed, 4, {60, 60});
            m.run(90000);
            m.settle();
            if (!m.host->desyncs().empty()) std::cout << "\n    desync at turn " << m.host->desyncs()[0].turn << "\n";
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.host->turns_sealed() > 1750);                                // about 1800 turns of 50 ms
            for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.sims[0]->current_tick());
            ASSERT_TRUE(m.all_equal());
            ASSERT_TRUE(m.sims[0]->current_tick() > 1700);
            // the commands of every player took effect: ants of every player are not where they started
            sim::SimulationEngine fresh;
            build_world(fresh, seed);
            for (uint8_t p = 0; p < 4; ++p) {
                int moved = 0;
                for (uint32_t id : m.ids.ants[p]) {
                    if (m.sims[0]->get_unit(id).pos != fresh.get_unit(id).pos) ++moved;
                }
                ASSERT_TRUE(moved >= 2);
            }
        }
    } TEST_END();

    TEST_CASE("N2.10 Match: Different Links (40 / 150 / 300 ms, heavy jitter) Still End In The Same State") {
        LoopbackNetwork net(11);
        std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
        Ids ids;
        for (int i = 0; i < 4; ++i) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            ids = build_world(*sims.back(), 3);
        }
        HostSession host(*sims[0], HostSession::Config{});
        std::vector<std::unique_ptr<ClientSession>> clients;
        const LoopbackNetwork::Link links[3] = {{40, 5}, {150, 100}, {300, 200}};
        for (uint8_t p = 1; p < 4; ++p) {
            auto ends = net.connect(links[p - 1]);
            host.add_client(p, ends.first);
            ClientSession::Config cc;
            cc.player = p;
            clients.push_back(std::make_unique<ClientSession>(*sims[p], cc));
            clients.back()->set_connection(ends.second);
        }
        host.start(0);
        for (auto& c : clients) c->start(0);
        uint32_t now = 0;
        for (; now < 60000; now += 10) {
            net.set_time(now);
            Command c;
            if (script(ids, 3, now, 0, c)) host.submit_local(c);
            for (uint8_t p = 1; p < 4; ++p) {
                if (script(ids, 3, now, p, c)) clients[p - 1]->submit(c);
            }
            host.update(now);
            for (auto& cl : clients) cl->update(now);
        }
        host.freeze();
        for (uint32_t end = now + 5000; now < end; now += 10) {
            net.set_time(now);
            host.update(now);
            for (auto& cl : clients) cl->update(now);
        }
        ASSERT_TRUE(host.desyncs().empty());
        for (int i = 1; i < 4; ++i) ASSERT_TRUE(sims[static_cast<size_t>(i)]->state_hash() == sims[0]->state_hash());
        ASSERT_TRUE(clients[2]->rtt_ms() >= 600 && clients[2]->rtt_ms() <= 1000);        // the 300 ms link measures its round trip
        ASSERT_TRUE(clients[0]->rtt_ms() >= 80 && clients[0]->rtt_ms() <= 120);
    } TEST_END();

    TEST_CASE("N2.11 Match: Executed Ticks Are Steady At 20 Hz On Every Machine (no bursts beyond the bound, no drift against the host's schedule)") {
        Match m(1, 3, {80, 40});
        std::vector<uint64_t> last(3, 0);
        uint64_t max_step = 0;
        m.run(20000, true, [&](uint32_t) {
            for (size_t i = 0; i < m.sims.size(); ++i) {
                const uint64_t t = m.sims[i]->current_tick();
                max_step = std::max<uint64_t>(max_step, t - last[i]);
                last[i] = t;
            }
        });
        ASSERT_TRUE(max_step <= 2);                                    // a 10 ms step never advances more than one tick per machine
        // after the start buffer every machine has run about (20 s - start delay) * 20 ticks
        for (auto& s : m.sims) ASSERT_TRUE(s->current_tick() > 20000 / 50 - 100);
        m.settle();
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.12 Match: A Client Cannot Speak For Another Player (the payload's issuer is ignored)") {
        Match m(1, 2, {30, 0});
        const uint32_t victim_ant = m.ids.ants[0][0];                  // the host's worker
        const uint32_t own_ant = m.ids.ants[1][0];
        // client 1 orders the host's ant and claims to be the host
        m.clients[0]->submit(cmd(CommandType::GroupMove, 0, 255, 20, 30, {victim_ant}));
        m.clients[0]->submit(cmd(CommandType::GroupMove, 1, 255, 21, 31, {own_ant}));
        m.run(1000, false);
        m.settle();
        ASSERT_EQ(m.sims[0]->get_unit(victim_ant).orig_order, sim::AntUnit::kOrderNone);
        ASSERT_EQ(m.sims[1]->get_unit(victim_ant).orig_order, sim::AntUnit::kOrderNone);
        ASSERT_EQ(m.sims[0]->get_unit(own_ant).orig_order_tile, (TileCoord{21, 31}));
        ASSERT_TRUE(m.all_equal());
    } TEST_END();
}

void run_dropout_tests() {
    TEST_CASE("N2.18 Drop-Out Travels In The Turn Stream: Every Machine Drops The Team At The Same Tick, The Match Stays Identical") {
        Match m(1, 4, {30, 5});
        std::vector<uint8_t> left;
        m.host->set_on_player_left([&](uint8_t p) { left.push_back(p); });
        std::vector<uint64_t> host_ticks;
        std::vector<uint64_t> client_ticks;
        m.host->runner().set_on_command([&](const Command& c, const sim::CommandResult&) {
            if (c.type == CommandType::Drop) host_ticks.push_back(m.sims[0]->current_tick());
        });
        m.clients[0]->runner().set_on_command([&](const Command& c, const sim::CommandResult&) {
            if (c.type == CommandType::Drop) client_ticks.push_back(m.sims[1]->current_tick());
        });
        m.run(3000);
        for (uint8_t p = 0; p < 4; ++p) ASSERT_FALSE(m.sims[0]->is_player_dropped(p));
        m.net.cut(m.client_ends[3], true);                                  // player 3's machine dies
        m.run(5000);
        ASSERT_EQ(left.size(), 1u);
        ASSERT_EQ(left[0], 3);
        m.settle();
        ASSERT_EQ(host_ticks.size(), 1u);                                   // one Drop, applied once ...
        ASSERT_EQ(client_ticks.size(), 1u);
        ASSERT_EQ(host_ticks[0], client_ticks[0]);                          // ... at the same tick on the host and on a client
        for (size_t i = 0; i < 3; ++i) {                                    // the survivors all dropped team 3 and agree on everything
            ASSERT_TRUE(m.sims[i]->is_player_dropped(3));
            ASSERT_FALSE(m.sims[i]->is_player_dropped(0));
            ASSERT_FALSE(m.sims[i]->is_player_dropped(1));
            ASSERT_FALSE(m.sims[i]->is_player_dropped(2));
            ASSERT_TRUE(m.sims[i]->state_hash() == m.sims[0]->state_hash());
            size_t ants_of_3 = 0;
            for (const auto& a : m.sims[i]->get_world_state().ants) ants_of_3 += a.player_id == 3;
            ASSERT_EQ(ants_of_3, 0u);                                       // the team's ants died on every machine
        }
        ASSERT_TRUE(m.host->desyncs().empty() || m.host->desyncs()[0].player == 3);
        ASSERT_TRUE(m.host->turns_sealed() > 140);
    } TEST_END();

    TEST_CASE("N2.19 Drop-Out: A Client Cannot Send The System Command (it is refused and counted as a violation, nobody is dropped)") {
        Match m(1, 3, {20, 0});
        m.run(1000);
        ASSERT_EQ(m.host->violations(2), 0u);
        m.client_ends[2]->send(encode(CommandMsg{cmd(CommandType::Drop, 2)}));       // its own drop
        m.client_ends[2]->send(encode(CommandMsg{cmd(CommandType::Drop, 1)}));       // somebody else's
        m.run(500, false);
        ASSERT_EQ(m.host->violations(2), 2u);
        ASSERT_TRUE(m.host->client_present(1));
        ASSERT_TRUE(m.host->client_present(2));
        m.settle();
        for (size_t i = 0; i < m.sims.size(); ++i) {
            for (uint8_t p = 0; p < 4; ++p) ASSERT_FALSE(m.sims[i]->is_player_dropped(p));
        }
        ASSERT_TRUE(m.all_equal());
        // the sequencer refuses it for a client and accepts it as a system command
        Sequencer seq;
        seq.set_active(1, true);
        ASSERT_FALSE(seq.submit(1, cmd(CommandType::Drop, 1)));
        ASSERT_EQ(seq.queued(), 0u);
        seq.submit_system(cmd(CommandType::Drop, 1));
        ASSERT_EQ(seq.queued(), 1u);
        seq.submit_system(cmd(CommandType::Drop, 9));                                // no such player
        seq.submit_system(cmd(CommandType::None, 1));
        ASSERT_EQ(seq.queued(), 1u);
        const TurnMsg turn = seq.seal();
        ASSERT_EQ(turn.commands.size(), 1u);
        ASSERT_EQ(turn.commands[0].type, CommandType::Drop);
        ASSERT_EQ(turn.commands[0].issuer, 1);
    } TEST_END();

    TEST_CASE("N2.20 Drop-Out: A Client That Falls Silent Holds The Match Up (flow control) And Is Dropped After The Silence Timeout") {
        ASSERT_EQ(HostSession::Config{}.silence_timeout_ms, 60000u);                 // the original's 60 s
        HostSession::Config hc;
        hc.silence_timeout_ms = 8000;
        Match m(1, 3, {20, 0}, hc);
        std::vector<uint8_t> left;
        m.host->set_on_player_left([&](uint8_t p) { left.push_back(p); });
        auto run_without_client_2 = [&](uint32_t ms) {                                // client 2's process hangs: its connection stays open
            const uint32_t end = m.now + ms;
            while (m.now < end) {
                m.now += 10;
                m.net.set_time(m.now);
                m.host->update(m.now);
                m.clients[0]->update(m.now);
            }
        };
        m.run(2000);
        const uint32_t hang_at = m.now;
        run_without_client_2(6000);                                                  // 6 s: past the 3 s flow control, before the timeout
        ASSERT_TRUE(left.empty());
        ASSERT_TRUE(m.host->waiting());
        ASSERT_EQ(m.host->laggard(), 2);
        const uint32_t sealed_while_waiting = m.host->turns_sealed();
        run_without_client_2(1000);
        ASSERT_EQ(m.host->turns_sealed(), sealed_while_waiting);                     // sealing is stalled
        run_without_client_2(3000);                                                  // the 8 s of silence are over
        ASSERT_TRUE(m.now - hang_at > 8000);
        ASSERT_EQ(left.size(), 1u);
        ASSERT_EQ(left[0], 2);
        ASSERT_FALSE(m.host->client_present(2));
        ASSERT_FALSE(m.host->waiting());
        run_without_client_2(4000);
        ASSERT_TRUE(m.host->turns_sealed() > sealed_while_waiting + 40);             // the game goes on for the others
        m.host->freeze();
        run_without_client_2(3000);
        ASSERT_TRUE(m.sims[0]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
    } TEST_END();

    TEST_CASE("N2.21 Runner Hooks: One Call Per Tick (one per turn) And One Per Command With The Engine's Verdict, In Order") {
        sim::SimulationEngine sim;
        const Ids ids = build_world(sim, 1);
        LockstepRunner runner(sim);
        std::vector<uint64_t> tick_times;
        std::vector<std::pair<CommandType, sim::CommandResult::Status>> commands;
        runner.set_on_tick([&]() { tick_times.push_back(sim.current_tick()); });
        runner.set_on_command([&](const Command& c, const sim::CommandResult& r) { commands.emplace_back(c.type, r.status); });
        auto turn = [](uint32_t n, std::vector<Command> cmds = {}) {
            TurnMsg t;
            t.turn = n;
            t.commands = std::move(cmds);
            return t;
        };
        runner.on_turn(turn(0, {cmd(CommandType::GroupMove, 0, 255, 20, 30, {ids.ants[0][0]}), cmd(CommandType::Hatch, 1),
                                cmd(CommandType::GroupMove, 4, 255, 1, 1, {1})}));                 // the last has no such player
        runner.on_turn(turn(1));
        runner.on_turn(turn(2, {cmd(CommandType::Stop, 0, 255, 0, 0, {ids.ants[0][0]})}));
        for (int i = 0; i < 40; ++i) runner.update(50);
        ASSERT_EQ(runner.next_turn_to_execute(), 3u);
        ASSERT_EQ(tick_times.size(), 3u);                                                           // one tick per turn
        for (size_t i = 0; i < tick_times.size(); ++i) ASSERT_EQ(tick_times[i], i + 1);             // the hook runs after the tick
        ASSERT_EQ(commands.size(), 4u);
        ASSERT_EQ(commands[0].first, CommandType::GroupMove);
        ASSERT_EQ(commands[0].second, sim::CommandResult::Status::Applied);
        ASSERT_EQ(commands[1].first, CommandType::Hatch);
        ASSERT_EQ(commands[2].second, sim::CommandResult::Status::RejectedIssuer);
        ASSERT_EQ(commands[3].first, CommandType::Stop);
    } TEST_END();
}

namespace {


// ---------------------------------------------------------------------------------------------------------------------------------
// Host migration (docs/NETWORK_PORT.md): the guests are linked to each other as well, so the game outlives its host
// ---------------------------------------------------------------------------------------------------------------------------------

using LinkFn = std::function<LoopbackNetwork::Link(uint8_t, uint8_t)>;

LinkFn same_link(LoopbackNetwork::Link link) {
    return [link](uint8_t, uint8_t) { return link; };
}

// A match of `players` machines: seat 0 hosts, every guest is linked to the host and to every other guest
struct Mesh {
    LoopbackNetwork net;
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
    Ids ids;
    std::vector<std::unique_ptr<HostSession>> hosts;         // by seat: the first host, and every guest that took over
    std::vector<std::unique_ptr<ClientSession>> clients;     // by seat; reset when the seat took over
    std::vector<std::vector<Connection*>> link;              // link[a][b]: the endpoint that seat a holds of its link to seat b
    std::vector<bool> alive;                                 // a machine that is not alive is no longer updated
    std::vector<std::pair<uint8_t, uint8_t>> left;           // (host seat, player) of every drop-out a host announced
    std::vector<std::vector<ChatMsg>> chats;                 // what each machine's chat callback saw
    uint32_t now{0};
    uint32_t seed{1};
    uint8_t players{4};
    int promotions{0};

    Mesh(uint32_t seed_, uint8_t players_, const LinkFn& link_of, ClientSession::Config cc = {}) : net(seed_ * 17u), seed(seed_), players(players_) {
        for (uint8_t p = 0; p < players; ++p) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            ids = build_world(*sims.back(), seed);
        }
        hosts.resize(players);
        clients.resize(players);
        alive.assign(players, true);
        chats.resize(players);
        link.assign(players, std::vector<Connection*>(players, nullptr));
        hosts[0] = std::make_unique<HostSession>(*sims[0], HostSession::Config{});
        hosts[0]->set_on_player_left([this](uint8_t p) { left.emplace_back(0, p); });
        hosts[0]->set_on_chat([this](const ChatMsg& c) { chats[0].push_back(c); });
        for (uint8_t p = 1; p < players; ++p) {
            ClientSession::Config c = cc;
            c.player = p;
            c.host = 0;
            clients[p] = std::make_unique<ClientSession>(*sims[p], c);
            clients[p]->set_on_chat([this, p](const ChatMsg& m) { chats[p].push_back(m); });
            auto ends = net.connect(link_of(0, p));
            link[0][p] = ends.first;
            link[p][0] = ends.second;
            hosts[0]->add_client(p, ends.first);
            clients[p]->set_connection(ends.second);
        }
        for (uint8_t a = 1; a < players; ++a) {
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < players; ++b) {
                auto ends = net.connect(link_of(a, b));
                link[a][b] = ends.first;
                link[b][a] = ends.second;
                clients[a]->set_peer(b, ends.first);
                clients[b]->set_peer(a, ends.second);
            }
        }
        hosts[0]->start(0);
        for (uint8_t p = 1; p < players; ++p) clients[p]->start(0);
    }

    // The machine of `seat` dies: its links are cut at once (abrupt), or it simply stops answering while its links stay open (a frozen process)
    void kill(uint8_t seat, bool abrupt = true) {
        alive[seat] = false;
        if (abrupt) isolate(seat);
    }
    // Every link of the seat is cut, but the machine goes on (a partition)
    void isolate(uint8_t seat) {
        for (uint8_t o = 0; o < players; ++o) {
            if (o != seat && link[seat][o] != nullptr) net.cut(link[seat][o]);
        }
    }
    void cut_link(uint8_t a, uint8_t b) { net.cut(link[a][b]); }

    void step_promotions() {
        for (uint8_t s = 0; s < players; ++s) {
            if (!alive[s] || !clients[s] || !clients[s]->promoted()) continue;
            hosts[s] = promote_to_host(*clients[s], *sims[s], HostSession::Config{}, now, [this, s](HostSession& h) {
                h.set_on_player_left([this, s](uint8_t p) { left.emplace_back(s, p); });
                h.set_on_chat([this, s](const ChatMsg& c) { chats[s].push_back(c); });
            });
            clients[s].reset();
            ++promotions;
        }
    }

    // Advances virtual time by `ms` in 10 ms steps; every machine that plays issues its scripted commands when `scripted`
    void run(uint32_t ms, bool scripted = true, const std::function<void(uint32_t)>& each_step = {}) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            for (uint8_t s = 0; s < players; ++s) {
                Command c;
                if (!alive[s] || !scripted || !script(ids, seed, now, s, c)) continue;
                if (hosts[s]) hosts[s]->submit_local(c);
                else if (clients[s]) clients[s]->submit(c);
            }
            if (each_step) each_step(now);
            for (uint8_t s = 0; s < players; ++s) {
                if (!alive[s]) continue;
                if (hosts[s]) hosts[s]->update(now);
                else if (clients[s]) clients[s]->update(now);
            }
            step_promotions();
        }
    }
    bool playing(uint8_t s) const { return alive[s] && (hosts[s] != nullptr || (clients[s] && !clients[s]->lost())); }
    void settle(uint32_t ms = 3000) {
        for (uint8_t s = 0; s < players; ++s) {
            if (alive[s] && hosts[s]) hosts[s]->freeze();
        }
        run(ms, false);
    }
    // Every machine that still plays holds the same state (the hosts' desync detectors stayed silent as well)
    bool survivors_equal(uint8_t except = 255) const {
        const sim::StateHash* first = nullptr;
        sim::StateHash hash;
        for (uint8_t s = 0; s < players; ++s) {
            if (!playing(s) || s == except) continue;
            if (hosts[s] && !hosts[s]->desyncs().empty()) return false;
            hash = sims[s]->state_hash();
            if (first == nullptr) {
                first = &hash;
                continue;
            }
            if (hash != *first) return false;
        }
        return true;
    }
    uint32_t tick(uint8_t s) const { return static_cast<uint32_t>(sims[s]->current_tick()); }
};

}  // namespace

void run_migration_tests() {
    TEST_CASE("N2.22 Host Migration: The Host Dies, The Lowest Guest Takes Over, The Others Follow, Every Survivor Stays Identical And The Game Goes On") {
        std::vector<sim::StateHash> finals;
        for (int run = 0; run < 2; ++run) {                                  // twice: the whole thing is deterministic
            Mesh m(1, 4, same_link({30, 5}));
            std::vector<uint8_t> after;                                      // issuers of the commands seat 1's machine applies after the death
            bool killed = false;
            m.clients[1]->runner().set_on_command([&](const Command& c, const sim::CommandResult&) {
                if (killed && c.type != CommandType::Drop) after.push_back(c.issuer);
            });
            m.run(6000);
            ASSERT_EQ(m.promotions, 0);
            ASSERT_TRUE(m.hosts[0]->turns_sealed() > 100);
            const uint32_t kill_tick = m.tick(1);
            killed = true;
            m.kill(0);
            m.run(8000);
            ASSERT_EQ(m.promotions, 1);
            ASSERT_TRUE(m.hosts[1] != nullptr);
            ASSERT_TRUE(m.hosts[2] == nullptr && m.hosts[3] == nullptr);
            ASSERT_EQ(m.hosts[1]->epoch(), 1);
            ASSERT_EQ(m.hosts[1]->host_player(), 1);
            for (uint8_t s = 2; s < 4; ++s) {
                ASSERT_EQ(m.clients[s]->epoch(), 1);
                ASSERT_EQ(m.clients[s]->host_seat(), 1);
                ASSERT_EQ(m.clients[s]->mode(), ClientSession::Mode::Normal);
            }
            m.settle();
            ASSERT_TRUE(m.survivors_equal());
            for (uint8_t s = 1; s < 4; ++s) {
                ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
                for (uint8_t p = 1; p < 4; ++p) ASSERT_FALSE(m.sims[s]->is_player_dropped(p));
            }
            ASSERT_TRUE(m.tick(1) > kill_tick + 150);                        // the game went on for several seconds after the host died
            ASSERT_EQ(std::count(after.begin(), after.end(), 0), 0);         // nothing more from the dead host ...
            for (uint8_t p = 1; p < 4; ++p) ASSERT_TRUE(std::count(after.begin(), after.end(), p) > 5);   // ... and everybody else still commands
            ASSERT_EQ(m.left.size(), 1u);                                    // the new host announced the drop-out of the old one, once
            ASSERT_TRUE(m.left[0].first == 1 && m.left[0].second == 0);
            finals.push_back(m.sims[1]->state_hash());
        }
        ASSERT_TRUE(finals[0] == finals[1]);
    } TEST_END();

    TEST_CASE("N2.23 Host Migration: A Host That Only Falls Silent (links stay open) Is Given Up After Ten Seconds") {
        Mesh m(2, 4, same_link({40, 10}));
        m.run(4000);
        m.kill(0, false);
        m.run(9000);
        ASSERT_EQ(m.promotions, 0);                                          // nine seconds are not enough
        for (uint8_t s = 1; s < 4; ++s) ASSERT_EQ(m.clients[s]->mode(), ClientSession::Mode::Normal);
        m.run(3000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr);
        m.run(4000);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 1; s < 4; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
    } TEST_END();

    TEST_CASE("N2.24 Host Migration: Two Players Left (three in the match); Two Players Total: The Drop Of The Host Leaves The Guest Without A Live Enemy And Ends The Match At Once (0x100d172)") {
        {
            Mesh m(3, 3, same_link({25, 0}));
            m.run(3000);
            m.kill(0);
            m.run(6000);
            ASSERT_EQ(m.promotions, 1);
            ASSERT_TRUE(m.hosts[1] != nullptr);
            ASSERT_EQ(m.clients[2]->host_seat(), 1);
            m.settle();
            ASSERT_TRUE(m.survivors_equal());
            ASSERT_TRUE(m.sims[1]->is_player_dropped(0));
            ASSERT_TRUE(m.sims[2]->is_player_dropped(0));
            ASSERT_TRUE(m.sims[1]->is_player_dropped(3));                    // the seat of the roster that nobody plays leaves with the old host
            ASSERT_FALSE(m.sims[1]->is_match_over());                        // two enemies are left: the guests play on
            ASSERT_FALSE(m.sims[2]->is_match_over());
        }
        {
            Mesh m(4, 2, same_link({25, 0}));
            m.run(3000);
            m.kill(0);
            m.run(6000);
            ASSERT_EQ(m.promotions, 1);                                      // nobody to ask: the guest is the host at once
            ASSERT_TRUE(m.hosts[1] != nullptr);
            ASSERT_TRUE(m.sims[1]->is_player_dropped(0));
            ASSERT_FALSE(m.sims[1]->is_player_dropped(1));
            ASSERT_TRUE(m.sims[1]->is_match_over());                         // the win test of the drop-out: no team is left besides the guest
            ASSERT_EQ(m.sims[1]->quitter(), sim::NO_QUITTER);
            ASSERT_TRUE(m.sims[1]->get_world_state().match_result.is_winner(1));
        }
    } TEST_END();

    TEST_CASE("N2.25 Host Migration: The Host And The Next Seat Die Together; The Seats Above Them Elect Among Themselves") {
        Mesh m(5, 4, same_link({30, 5}));
        m.run(4000);
        m.kill(0);
        m.kill(1);
        m.run(8000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[2] != nullptr && m.hosts[1] == nullptr);
        ASSERT_EQ(m.hosts[2]->epoch(), 1);
        ASSERT_EQ(m.clients[3]->host_seat(), 2);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 2; s < 4; ++s) {
            ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
            ASSERT_TRUE(m.sims[s]->is_player_dropped(1));
            ASSERT_FALSE(m.sims[s]->is_player_dropped(2));
            ASSERT_FALSE(m.sims[s]->is_player_dropped(3));
        }
    } TEST_END();

    TEST_CASE("N2.26 Host Migration: The Successor Dies While It Is Being Accepted; The Next One Takes Over Within A Blink") {
        Mesh m(6, 4, same_link({30, 0}));
        m.run(4000);
        m.kill(0);
        bool cut = false;
        m.run(6000, true, [&](uint32_t) {
            if (!cut && m.clients[2] && m.clients[2]->mode() == ClientSession::Mode::Following) {
                cut = true;                                                  // seat 2 has accepted seat 1: seat 1 dies before it can resume
                m.kill(1);
            }
        });
        ASSERT_TRUE(cut);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[2] != nullptr && m.hosts[1] == nullptr);
        ASSERT_EQ(m.clients[3]->host_seat(), 2);
        ASSERT_EQ(m.clients[3]->epoch(), 1);                                 // still the first host change: the election was only repeated
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        ASSERT_TRUE(m.sims[2]->is_player_dropped(0) && m.sims[2]->is_player_dropped(1));
        ASSERT_TRUE(m.sims[3]->is_player_dropped(0) && m.sims[3]->is_player_dropped(1));
    } TEST_END();

    TEST_CASE("N2.27 Host Migration: Guests That Are Behind Or Ahead Of The New Host Get Or Give The Missing Turns; Nobody Diverges") {
        for (int variant = 0; variant < 2; ++variant) {
            // variant 0: seat 1 (the successor) is on a slow link, so it is behind the others when the host dies: it fetches the turns from one of them
            // variant 1: seats 2 and 3 are on slow links: they are behind the successor, which sends them what they miss
            const LinkFn links = [variant](uint8_t a, uint8_t b) {
                if (a == 0) {
                    const bool slow = variant == 0 ? b == 1 : b >= 2;
                    return LoopbackNetwork::Link{slow ? 400u : 30u, 0u};
                }
                return LoopbackNetwork::Link{30u, 0u};
            };
            Mesh m(7 + static_cast<uint32_t>(variant), 4, links);
            m.run(5000);
            const uint32_t behind_1 = m.clients[1]->runner().next_turn_expected();
            const uint32_t at_2 = m.clients[2]->runner().next_turn_expected();
            ASSERT_TRUE(variant == 0 ? behind_1 + 2 < at_2 : at_2 + 2 < behind_1);   // the links really make them differ
            m.kill(0);
            m.run(8000);
            ASSERT_EQ(m.promotions, 1);
            ASSERT_TRUE(m.hosts[1] != nullptr);
            m.settle();
            ASSERT_TRUE(m.survivors_equal());
            for (uint8_t s = 1; s < 4; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(0));
            ASSERT_EQ(m.sims[1]->current_tick(), m.sims[2]->current_tick());
            ASSERT_EQ(m.sims[1]->current_tick(), m.sims[3]->current_tick());
        }
    } TEST_END();

    TEST_CASE("N2.28 Host Migration: The Guest That Holds The Turns Dies While They Are Being Fetched; The Rest Goes On Without Them") {
        const LinkFn links = [](uint8_t a, uint8_t b) {
            if (a == 0) return LoopbackNetwork::Link{b == 2 ? 30u : 400u, 0u};   // only seat 2 hears the host promptly
            return LoopbackNetwork::Link{30u, 0u};
        };
        Mesh m(9, 4, links);
        m.run(5000);
        ASSERT_TRUE(m.clients[1]->runner().next_turn_expected() + 2 < m.clients[2]->runner().next_turn_expected());
        m.kill(0);
        bool cut = false;
        m.run(9000, true, [&](uint32_t) {
            if (!cut && m.clients[1] && m.clients[1]->mode() == ClientSession::Mode::Fetching) {
                cut = true;                                                  // seat 1 asks seat 2 for the turns it lacks: seat 2 dies first
                m.kill(2);
            }
        });
        ASSERT_TRUE(cut);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr);
        ASSERT_EQ(m.clients[3]->host_seat(), 1);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        ASSERT_TRUE(m.sims[1]->is_player_dropped(0) && m.sims[1]->is_player_dropped(2));
        ASSERT_FALSE(m.sims[1]->is_player_dropped(3));
        ASSERT_TRUE(m.sims[3]->is_player_dropped(2));
    } TEST_END();

    TEST_CASE("N2.29 Host Migration: A Guest That Says Nothing Does Not Hold The Election Up; It Is Dropped With The Old Host") {
        Mesh m(10, 4, same_link({30, 5}));
        m.run(4000);
        m.kill(0);
        m.kill(3, false);                                                    // seat 3's machine freezes (its links stay open, it never answers)
        m.run(3000);
        ASSERT_EQ(m.promotions, 0);                                          // seat 1 waits three seconds for the answer of seat 3 ...
        m.run(3000);
        ASSERT_EQ(m.promotions, 1);                                          // ... and goes on without it
        ASSERT_TRUE(m.hosts[1] != nullptr);
        m.run(4000);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        ASSERT_TRUE(m.sims[1]->is_player_dropped(0) && m.sims[1]->is_player_dropped(3));
        ASSERT_TRUE(m.sims[2]->is_player_dropped(3));
        ASSERT_FALSE(m.sims[1]->is_player_dropped(2));
    } TEST_END();

    TEST_CASE("N2.30 Host Migration: A Host That Is Cut Off From Everybody Goes On Alone; The Guests Elect A New One And Ignore Its Past") {
        Mesh m(11, 4, same_link({30, 5}));
        m.run(4000);
        m.isolate(0);                                                        // a partition: the old host's machine is fine but nobody reaches it
        m.run(8000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr);
        ASSERT_EQ(m.hosts[0]->epoch(), 0);
        ASSERT_EQ(m.hosts[1]->epoch(), 1);
        // the old host dropped everybody (its links were closed) and plays on by itself
        for (uint8_t p = 1; p < 4; ++p) ASSERT_TRUE(m.sims[0]->is_player_dropped(p));
        m.settle();
        ASSERT_TRUE(m.survivors_equal(0));                                   // the guests agree with each other
        ASSERT_TRUE(m.sims[1]->is_player_dropped(0));
    } TEST_END();

    TEST_CASE("N2.31 Host Migration: A Guest Whose Own Link To The Host Broke While The Host Lives For The Others Is Lost, Not A New Host") {
        Mesh m(12, 4, same_link({30, 0}));
        m.run(3000);
        m.cut_link(0, 3);                                                    // only seat 3 loses the host
        m.run(12000);
        ASSERT_EQ(m.promotions, 0);                                          // nobody took over: the host is alive for seats 1 and 2
        ASSERT_TRUE(m.clients[3]->lost());
        ASSERT_EQ(m.clients[1]->mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(m.clients[2]->mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(m.clients[1]->host_seat(), 0);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 0; s < 3; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(3));   // the host dropped the one it lost
        ASSERT_FALSE(m.sims[0]->is_player_dropped(1) || m.sims[0]->is_player_dropped(2));
    } TEST_END();

    TEST_CASE("N2.32 Host Migration: Chat, Leaving And Commands All Work Through The New Host") {
        Mesh m(13, 4, same_link({30, 5}));
        m.run(3000);
        m.kill(0);
        m.run(4000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.clients[3]->chat("still here", false));
        m.hosts[1]->chat_local("the host of the rest", false);
        m.hosts[1]->chat_local("team of the new host", true);                     // (no alliance: a line for the team stays with its sender, N2.90)
        m.run(1000);
        for (uint8_t s = 1; s < 4; ++s) {
            bool a = false, b = false, t = false;
            for (const ChatMsg& c : m.chats[s]) {
                if (c.text == "still here") a = c.sender == 3 && !c.team;
                if (c.text == "the host of the rest") b = c.sender == 1 && !c.team;
                if (c.text == "team of the new host") t = true;
            }
            ASSERT_TRUE(a && b);
            ASSERT_EQ(t, s == 1);
        }
        m.clients[2]->leave();                                               // seat 2 quits: the new host drops it
        m.run(2000);
        ASSERT_FALSE(m.hosts[1]->client_present(2));
        ASSERT_TRUE(m.hosts[1]->client_present(3));
        m.settle();
        ASSERT_TRUE(m.sims[1]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[3]->is_player_dropped(2));
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[3]->state_hash());
    } TEST_END();

    TEST_CASE("N2.33 Host Migration: The Host Changes Three Times In A Row; The Last Guest Ends Up Alone And Identical To What It Saw") {
        Mesh m(14, 4, same_link({30, 5}));
        m.run(4000);
        m.kill(0);
        m.run(5000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_EQ(m.hosts[1]->epoch(), 1);
        m.kill(1);
        m.run(6000);
        ASSERT_EQ(m.promotions, 2);
        ASSERT_TRUE(m.hosts[2] != nullptr);
        ASSERT_EQ(m.hosts[2]->epoch(), 2);
        ASSERT_EQ(m.clients[3]->host_seat(), 2);
        ASSERT_EQ(m.clients[3]->epoch(), 2);
        m.run(1000);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());                                    // seats 2 and 3, after two changes of host
        for (uint8_t s = 2; s < 4; ++s) ASSERT_TRUE(m.sims[s]->is_player_dropped(0) && m.sims[s]->is_player_dropped(1));
    } TEST_END();

    TEST_CASE("N2.34 Host Migration: The Session Ignores Proposals That Are Stale, Forged, From The Wrong Election Or Made While Its Host Lives") {
        LoopbackNetwork net(3);
        sim::SimulationEngine sim;
        build_world(sim, 1);
        ClientSession::Config cfg;
        cfg.player = 2;
        cfg.host = 0;
        ClientSession c(sim, cfg);
        auto host_link = net.connect({5, 0});
        auto peer1 = net.connect({5, 0});                                    // the link to seat 1
        auto peer3 = net.connect({5, 0});                                    // the link to seat 3
        c.set_connection(host_link.second);
        c.set_peer(1, peer1.second);
        c.set_peer(3, peer3.second);
        c.start(0);
        uint32_t now = 0;
        bool host_talks = true;
        auto step = [&](uint32_t ms) {
            for (uint32_t i = 0; i < ms; i += 10) {
                now += 10;
                net.set_time(now);
                if (host_talks && now % 500 == 0) host_link.first->send(encode_pong(PingMsg{1, 0}));   // a live host talks every half second
                c.update(now);
            }
        };
        auto drain = [](Connection* end) {
            std::vector<std::vector<uint8_t>> out;
            std::vector<uint8_t> m;
            while (end->poll(m)) out.push_back(m);
            return out;
        };
        auto only_pings = [](const std::vector<std::vector<uint8_t>>& msgs) {
            for (const auto& m : msgs) {
                if (peek_type(m) != MsgType::Ping) return false;
            }
            return true;
        };
        step(1000);
        drain(peer1.first);
        drain(peer3.first);
        // a proposal of the right election while the host lives for us: refused, naming the host
        peer1.first->send(encode(ProposeMsg{1, 1}));
        step(100);
        RefuseMsg refuse;
        bool refused = false;
        for (const auto& m : drain(peer1.first)) refused = refused || (peek_type(m) == MsgType::Refuse && decode(m, refuse));
        ASSERT_TRUE(refused && refuse.epoch == 1 && refuse.lowest == 0);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        // a wrong election, a candidate that is not the seat of the link, a Resume although the host lives: no answer, nothing changes
        peer1.first->send(encode(ProposeMsg{2, 1}));
        peer1.first->send(encode(ProposeMsg{0, 1}));
        peer1.first->send(encode(ProposeMsg{1, 3}));                         // seat 1's link claims to be seat 3 ...
        peer3.first->send(encode(ProposeMsg{1, 1}));                         // ... and seat 3's link claims to be seat 1
        peer1.first->send(encode(ResumeMsg{1, 1, 0}));
        step(100);
        ASSERT_TRUE(only_pings(drain(peer1.first)));
        ASSERT_TRUE(only_pings(drain(peer3.first)));
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(c.host_seat(), 0);
        // the host falls silent (for longer than "alive", shorter than "gone"): the proposal of the lowest seat is accepted
        host_talks = false;
        step(2500);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        drain(peer1.first);
        peer1.first->send(encode(ProposeMsg{1, 1}));
        step(100);
        AcceptMsg accept;
        bool accepted = false;
        for (const auto& m : drain(peer1.first)) accepted = accepted || (peek_type(m) == MsgType::Accept && decode(m, accept));
        ASSERT_TRUE(accepted && accept.epoch == 1 && accept.next_receive == 0 && accept.next_execute == 0);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Following);
        // one candidate per election: seat 3 is refused, naming the seat that has been accepted; its Resume does not count
        drain(peer3.first);
        peer3.first->send(encode(ProposeMsg{1, 3}));
        peer3.first->send(encode(ResumeMsg{1, 3, 0}));
        step(100);
        refused = false;
        for (const auto& m : drain(peer3.first)) refused = refused || (peek_type(m) == MsgType::Refuse && decode(m, refuse));
        ASSERT_TRUE(refused && refuse.lowest == 1);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Following);
        // the accepted candidate's Resume ends the election: its link is the host link from now on
        peer1.first->send(encode(ResumeMsg{1, 1, 0}));
        step(100);
        ASSERT_EQ(c.mode(), ClientSession::Mode::Normal);
        ASSERT_EQ(c.host_seat(), 1);
        ASSERT_EQ(c.epoch(), 1);
        drain(peer1.first);
        ASSERT_TRUE(c.submit(cmd(CommandType::Hatch, 2)));
        step(50);
        bool command_seen = false;
        for (const auto& m : drain(peer1.first)) command_seen = command_seen || peek_type(m) == MsgType::Command;
        ASSERT_TRUE(command_seen);                                           // commands go to the new host now
    } TEST_END();

    TEST_CASE("N2.35 Host Migration: 20000 Garbage Messages On The Links Between Guests Change Nothing While The Host Lives") {
        Mesh m(15, 4, same_link({25, 0}));
        Lcg rng(4242);
        m.run(2000);
        for (int i = 0; i < 20000; ++i) {
            std::vector<uint8_t> buf(rng.below(30));
            for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
            if (!buf.empty() && rng.below(2) == 0) buf[0] = static_cast<uint8_t>(18 + rng.below(6));   // a migration message type
            const uint8_t from = static_cast<uint8_t>(1 + rng.below(3));
            uint8_t to = static_cast<uint8_t>(1 + rng.below(3));
            if (to == from) to = static_cast<uint8_t>(from % 3 + 1);
            m.link[from][to]->send(buf);
            if (i % 200 == 0) m.run(100);
        }
        m.run(4000);
        ASSERT_EQ(m.promotions, 0);
        for (uint8_t s = 1; s < 4; ++s) ASSERT_EQ(m.clients[s]->mode(), ClientSession::Mode::Normal);
        m.settle();
        ASSERT_TRUE(m.survivors_equal());
        for (uint8_t s = 0; s < 4; ++s) {
            for (uint8_t p = 0; p < 4; ++p) ASSERT_FALSE(m.sims[s]->is_player_dropped(p));
        }
    } TEST_END();

    TEST_CASE("N2.36 Turn Log: The Runner Remembers The Last 600 Turns (30 s), In Order; Older And Future Turns Are Not There; The Sequencer Resumes Where Told") {
        sim::SimulationEngine sim;
        build_world(sim, 1);
        LockstepRunner runner(sim);
        ASSERT_TRUE(runner.logged_turn(0) == nullptr);
        ASSERT_EQ(LockstepRunner::kTurnLogTurns, size_t{600});               // 30 s of play at 20 turns a second (it was 300 turns of 100 ms)
        for (uint32_t t = 0; t < 650; ++t) {
            TurnMsg turn;
            turn.turn = t;
            turn.commands = {cmd(CommandType::Hatch, static_cast<uint8_t>(t % 4))};
            ASSERT_TRUE(runner.on_turn(turn));
        }
        ASSERT_TRUE(runner.logged_turn(49) == nullptr);                      // 650 received, the log holds 600: turns 50 ..649
        ASSERT_TRUE(runner.logged_turn(50) != nullptr && runner.logged_turn(50)->turn == 50);
        ASSERT_TRUE(runner.logged_turn(649) != nullptr && runner.logged_turn(649)->turn == 649);
        ASSERT_EQ(runner.logged_turn(649)->commands[0].issuer, 1);
        ASSERT_TRUE(runner.logged_turn(650) == nullptr);
        TurnMsg gap;
        gap.turn = 652;
        ASSERT_FALSE(runner.on_turn(gap));                                   // a gap is refused and not logged
        ASSERT_TRUE(runner.logged_turn(652) == nullptr);
        // the sequencer of a new host: continues at the turn it is told, forgets what was queued, knows how far the others got
        Sequencer seq;
        seq.set_active(0, true);
        seq.set_active(1, true);
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::Hatch, 1)));
        seq.seal();
        seq.seal();
        ASSERT_TRUE(seq.submit(1, cmd(CommandType::Hatch, 1)));
        seq.on_hash(1, 9, sim::StateHash{1, 1, 1, 1, 1, 1, 1, 1});
        seq.resume(500);
        ASSERT_EQ(seq.next_turn(), 500u);
        ASSERT_EQ(seq.queued(), 0u);
        ASSERT_EQ(seq.acked(1), 0u);
        ASSERT_FALSE(seq.is_active(0) || seq.is_active(1));                  // the survivors are activated again by the new host
        seq.set_active(0, true);
        seq.set_active(1, true);
        ASSERT_EQ(seq.acked(1), 500u);                                       // level with the sequencer until told otherwise
        seq.set_acked(1, 480);
        ASSERT_EQ(seq.acked(1), 480u);
        seq.set_acked(1, 900);                                               // never beyond what was sealed
        ASSERT_EQ(seq.acked(1), 500u);
        seq.set_acked(1, 480);
        ASSERT_TRUE(seq.can_seal());                                         // 20 turns behind is within the flow-control bound (60 turns: 3 s) ...
        for (uint32_t i = 0; i < 41; ++i) seq.seal();
        ASSERT_FALSE(seq.can_seal());                                        // ... 61 is beyond it: the peer is the laggard that holds the game up
        ASSERT_EQ(seq.laggard(), 1);
        ASSERT_EQ(seq.seal().turn, 541u);
    } TEST_END();
}

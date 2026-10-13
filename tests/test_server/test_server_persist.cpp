// Tests of the dedicated game server: a match survives a restart of the server (S3.84 and on) and the review of the restart records
#include "server_fixtures.hpp"


void run_persist_server_tests() {
    TEST_CASE("S3.84 A Match Survives A Restart Of The Server (SIGTERM): The Record Holds Every Turn That Was Sealed And Has The Keys Of The Seats; After The Restart The Room Is Back With Its Code, Its Turns Replayed (The State Hash Is That Of An Independent Replay And The Stored Checkpoints Are The Machines' Own Hashes), Every Seat Held And The Match Paused; The Three Machines Come Back By Themselves, The Match Goes On And Ends Identical On Them And On The Referee, And The Record Is Gone") {
        PWorld w("persist-78");
        w.start_server(500);
        const RoomSpec spec = held_spec("P-1", 3);
        std::vector<RClient*> m = play_room(w, spec, 30000);
        RoomStatus s = w.status("P-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && !s.paused && s.record_kept && s.record_bytes > 0 && s.record_note.empty() && !s.restored);
        ASSERT_EQ(w.record_files().size(), size_t{1});
        RestartLoaded rec = w.read_record("P-1");
        ASSERT_TRUE(rec.ok() && !rec.torn && rec.head.code == "P-1" && rec.head.map == "TINY.LVL" && rec.head.players == 3);
        ASSERT_EQ(rec.turns.size(), static_cast<size_t>(s.turns));                           // every turn that the room has sealed is in the record: written before it was sent
        ASSERT_TRUE(rec.turns.size() > 590 && rec.checks.size() >= 29);
        ASSERT_EQ(s.record_bytes, rec.file_bytes);
        for (RClient* p : m) {                                                               // the keys that the players hold are the ones in the record, and nobody else's
            const uint8_t seat = p->lobby->my_seat();
            ASSERT_TRUE(rec.head.keys[seat] == p->lobby->key() && !net::key_is_zero(rec.head.keys[seat]));
            ASSERT_TRUE(rec.head.start.names[seat] == p->name);
        }
        for (const RestartCheck& c : rec.checks) {                                           // the checkpoints are the machines' own states: the referee's hash and the three machines' agree
            for (RClient* p : m) {
                const auto at = p->hash_at.find(uint64_t{c.turn} + 1);
                if (at != p->hash_at.end()) ASSERT_EQ(at->second, c.hash);
            }
        }
        size_t compared = 0;
        for (const RestartCheck& c : rec.checks) compared += m[0]->hash_at.count(uint64_t{c.turn} + 1);
        ASSERT_TRUE(compared >= 10);
        // ---- the server is told to stop (SIGTERM): the record is made durable and stays; the machines lose their links and look for the server ------------------------------------------
        const uint32_t sealed_at_stop = s.turns;
        w.stop_server(true);
        ASSERT_EQ(w.kept_at_stop, size_t{1});
        ASSERT_TRUE(fs::exists(w.record_path("P-1")));
        w.run(3000);
        for (RClient* p : m) ASSERT_TRUE(p->session->reconnecting() && !p->lost);            // (they retry every 2 s: the server is not there)
        rec = w.read_record("P-1");
        ASSERT_TRUE(rec.ok() && rec.turns.size() == sealed_at_stop);                         // nothing was lost, nothing was added
        // ---- the server starts again (its uptime is 0.5 s): the room is back ------------------------------------------------------------------------------------------------------
        w.start_server(500);
        ASSERT_EQ(w.report.items.size(), size_t{1});
        ASSERT_TRUE(w.report.items[0].outcome == RestoreItem::Outcome::Restored && w.report.items[0].code == "P-1" && w.report.items[0].turns == sealed_at_stop);
        ASSERT_TRUE(w.report.items[0].note.find("restored") != std::string::npos && w.report.items[0].note.find("3 seat(s) waiting") != std::string::npos);
        s = w.status("P-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.restored_turns == sealed_at_stop && s.turns == sealed_at_stop && s.ticks == sealed_at_stop);
        ASSERT_TRUE(s.paused && s.absent.size() == 3 && s.joined == 3 && s.reconnect && s.record_kept && s.record_note.empty());
        ASSERT_TRUE(s.names[0] == "P0" && s.names[1] == "P1" && s.names[2] == "P2");
        ASSERT_EQ(s.restored_hash, replay_hash(rec, rec.turns.size()));                      // the state that the room stands at is that of an independent replay of the record
        ASSERT_TRUE(s.restore_ms < 5000);
        ASSERT_EQ(w.record_files().size(), size_t{1});                                       // the same record: the room goes on writing it
        {
            const ctl::JsonValue j = status_json(w, "P-1");                                   // the control interface says so
            ASSERT_TRUE(j.get("state").str() == "running" && j.get("paused").as_bool_or(false) && j.get("absent").size() == 3);
            ASSERT_TRUE(j.get("restored").is_object() && j.get("restored").get("turns").as_int_or(0) == sealed_at_stop && j.get("restored").get("state_hash").str().size() == 16);
            ASSERT_TRUE(j.get("record").get("kept").as_bool_or(false) && j.get("record").get("bytes").as_int_or(0) > 0);
        }
        // ---- the three machines find the server again, with their keys, and the match goes on ----------------------------------------------------------------------------------
        ASSERT_TRUE(w.until([&]() { return !w.status("P-1").paused; }, 90000));
        s = w.status("P-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 3 && s.absent.empty() && s.drops_by_cap == 0 && s.drops_by_vote == 0);
        ASSERT_TRUE(w.until([&]() { return m[0]->session->mode() == net::ClientSession::Mode::Normal && m[1]->session->mode() == net::ClientSession::Mode::Normal && m[2]->session->mode() == net::ClientSession::Mode::Normal; }, 3000));
        for (RClient* p : m) ASSERT_FALSE(p->lost);
        w.run(8000);
        ASSERT_TRUE(w.status("P-1").turns > sealed_at_stop + 100);                           // the match goes on: new turns are sealed (and written: the same record grows)
        ASSERT_TRUE(w.read_record("P-1").turns.size() > sealed_at_stop + 100);
        w.play_to_the_end("P-1");
        const RoomStatus end = w.status("P-1");
        ASSERT_TRUE(end.state == RoomState::Finished && end.restored);
        ASSERT_TRUE(m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[1]->sim.state_hash() == m[2]->sim.state_hash());
        ASSERT_EQ(m[0]->sim.state_hash().total, end.referee_hash);                           // the machines that lived through the restart and the restored referee: one state at the end
        ASSERT_TRUE(m[0]->sim.is_match_over());
        for (RClient* p : m) ASSERT_FALSE(p->session->desynced() || p->lost);
        ASSERT_TRUE(w.record_files().empty());                                               // the match is over: its record is gone
        ASSERT_TRUE(!end.record_kept && end.record_note == "the room is over");
        ASSERT_TRUE(w.mgr->take_ended(w.server_now()).size() == 1);
    } TEST_END();

    TEST_CASE("S3.85 What Cannot Be Restored Is Said, And The Room Is A Failed Room With The Reason (Status, JSON, The Log's Report): Another Game Version, Another Protocol, A Map That Changed Or Is Gone, A Record That Is Older Than An Hour, A Replay That Does Not Agree With The Stored Hash (A Tampered Checkpoint, A Tampered Turn), A Room That This Server Would Not Make, A Replay That Takes Too Long, A Turn Log That Cannot Hold The Match, No Room For It; Each Record Is Deleted, A Match That Is Within Its Hour Is Restored, The Failed Room Keeps Its Code Until Its Keep Time Is Over And Says NoSuchRoom To A Hello")  {
        struct Refusal {
            const char* what;
            std::function<void(PWorld&)> before_restart;           // what changes between the crash and the new server
            const char* in_reason;                                  // a word that the reason must hold
        };
        const auto crash_with_record = [](PWorld& w, const char* code, uint32_t play_ms) {
            RoomSpec spec = held_spec(code, 2);
            spec.keep_ms = 8000;
            std::vector<RClient*> m = play_room(w, spec, play_ms);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);                                               // a crash
        };
        // rewrites a record: the head replaced by a changed one (the frames after it are kept as they are)
        const auto change_head = [](PWorld& w, const char* code, const std::function<void(RestartHead&)>& edit) {
            std::vector<uint8_t> bytes = record_bytes(w, code);
            const auto frames = frames_of(bytes);
            RestartLoaded rec = parse_restart_record(bytes.data(), bytes.size());
            edit(rec.head);
            std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(kRestartMagic)));
            const std::vector<uint8_t> head = encode_restart_head(rec.head);
            out.insert(out.end(), head.begin(), head.end());
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end());
            write_all_bytes(w.record_path(code), out);
        };
        // rewrites one frame's payload (and its checksum): a record that is whole as far as the format goes and wrong in what it says
        const auto change_frame = [](PWorld& w, const char* code, size_t index, const std::function<void(std::vector<uint8_t>&)>& edit) {
            std::vector<uint8_t> bytes = record_bytes(w, code);
            const auto frames = frames_of(bytes);
            std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].first + 5), bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].second - 4));
            edit(payload);
            const std::vector<uint8_t> frame = test_frame(bytes[frames[index].first], payload);
            std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].first));
            out.insert(out.end(), frame.begin(), frame.end());
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].second), bytes.end());
            write_all_bytes(w.record_path(code), out);
        };
        const std::vector<Refusal> cases = {
            {"another protocol", [](PWorld& w) { w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1); }, "protocol"},
            {"the map file changed", [](PWorld& w) { std::ofstream(fs::path(w.maps) / "TINY.LVL", std::ios::binary | std::ios::app).put('x'); }, "not the one"},
            {"the map is gone", [](PWorld& w) { fs::remove(fs::path(w.maps) / "TINY.LVL"); }, "not on this server"},
            {"older than an hour", [](PWorld& w) { fs::last_write_time(w.record_path("R-1"), fs::file_time_type::clock::now() - std::chrono::hours(2)); }, "given up"},
            {"a replay that takes too long", [](PWorld& w) { w.restart.replay_budget_ms = 0; }, "longer than"},
        };
        for (const Refusal& c : cases) {
            // a maps folder of this test's own, so that a map can change
            const std::string maps_copy = (fs::path(temp_dir_for("persist-79-maps")) / "Maps").string();
            fs::create_directories(maps_copy);
            fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(maps_copy) / "TINY.LVL", fs::copy_options::overwrite_existing);
            PWorld w("persist-79", ServerLimits(), maps_copy);
            // (the restore's clock is this world's own, 1 ms a read: the real one stamped the failed room ahead of the world's time on a slow machine, and the keep
            // time read that as long over; a replay still outlasts a budget of 0)
            w.restart.clock_ms = [&w]() { w.now += 1; return w.server_now(); };
            w.start_server(500);
            crash_with_record(w, "R-1", 8000);
            ASSERT_MSG(w.record_files().size() == 1, c.what);
            c.before_restart(w);
            w.start_server(500);
            ASSERT_MSG(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended, c.what);
            ASSERT_MSG(w.report.items[0].note.find(c.in_reason) != std::string::npos, std::string(c.what) + ": " + w.report.items[0].note);
            ASSERT_MSG(w.record_files().empty(), c.what);                              // the record is deleted
            const RoomStatus s = w.status("R-1");
            ASSERT_MSG(s.state == RoomState::Failed && s.reason == w.report.items[0].note && s.ticks > 100 && !s.record_kept && !s.restored && s.joined == 2, c.what);
            ASSERT_MSG(s.names[0] == "P0" && s.names[1] == "P1" && s.expected == 2 && s.map == "TINY.LVL", c.what);
            const ctl::JsonValue j = status_json(w, "R-1");
            ASSERT_MSG(j.get("state").str() == "failed" && j.get("reason").str().find(c.in_reason) != std::string::npos && j.get("restored").is_null(), c.what);
            bool saw_notice = false;
            for (const std::string& n : w.notices) saw_notice = saw_notice || (n.find("R-1") != std::string::npos && n.find("not restored") != std::string::npos);
            ASSERT_MSG(saw_notice, c.what);
            const std::vector<RoomStatus> ended = w.mgr->take_ended(w.server_now());     // the end is reported once, like any room's: the log line and the result file
            ASSERT_MSG(ended.size() == 1 && ended[0].code == "R-1" && ended[0].state == RoomState::Failed, c.what);
            ASSERT_MSG(w.mgr->take_ended(w.server_now()).empty(), c.what);
            // its code is taken until its keep time is over (8 s), a Hello for it says that there is no such room, and then a new room can have the code
            net::Connection* hello_link = w.open_link();
            net::HelloMsg h;
            h.room = "R-1";
            hello_link->send(net::encode(h));
            w.run(500);
            ASSERT_MSG(reject_on(hello_link) == static_cast<int>(net::RejectReason::NoSuchRoom), c.what);
            ASSERT_MSG(!w.mgr->create_room(held_spec("R-1", 2), w.server_now()).ok, c.what);
            w.run(9000);
            RoomStatus gone;
            ASSERT_MSG(!w.mgr->status("R-1", gone, w.server_now()), c.what);
            fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(w.maps) / "TINY.LVL", fs::copy_options::overwrite_existing);          // (the map is as it was: a room needs it)
            ASSERT_MSG(w.mgr->create_room(held_spec("R-1", 2), w.server_now()).ok, c.what);
        }
        {   // M4 of the review: the game version moves with every release, the protocol with every change of the rules: a record of another game version and build and the same protocol IS restored, with the
            // checkpoint-verified replay as the safety net (and the log says who wrote it); another protocol is refused (the cases above)
            PWorld w("persist-79v");
            w.start_server(500);
            crash_with_record(w, "R-6", 8000);
            w.restart.identity.game_version = "v9.9.9";
            w.restart.identity.build_id = "other-build";
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("R-6").restored && w.status("R-6").state == RoomState::Running);
            ASSERT_TRUE(w.report.items[0].note.find("written by " + std::string(kTestVersion)) != std::string::npos);
        }
        {   // a record that is within its hour is restored (59 minutes old)
            PWorld w("persist-79b");
            w.start_server(500);
            crash_with_record(w, "R-2", 8000);
            fs::last_write_time(w.record_path("R-2"), fs::file_time_type::clock::now() - std::chrono::minutes(59));
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("R-2").restored);
        }
        {   // a tampered checkpoint, and a tampered turn: the replay does not agree with what the old server saw
            PWorld w("persist-79c");
            w.start_server(500);
            crash_with_record(w, "R-3", 12000);
            change_frame(w, "R-3", [&]() -> size_t {                                    // (the frame of the first checkpoint)
                const auto frames = frames_of(record_bytes(w, "R-3"));
                for (size_t i = 1; i < frames.size(); ++i) {
                    if (record_bytes(w, "R-3")[frames[i].first] == 3) return i;
                }
                return 0;
            }(), [](std::vector<uint8_t>& p) { p[8] = static_cast<uint8_t>(p[8] ^ 1); });
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended);
            ASSERT_TRUE(w.report.items[0].note.find("does not agree with the state hash") != std::string::npos && w.report.items[0].note.find("turn 19") != std::string::npos);
            ASSERT_TRUE(w.status("R-3").state == RoomState::Failed && w.record_files().empty());
        }
        {
            PWorld w("persist-79d");
            w.start_server(500);
            crash_with_record(w, "R-4", 12000);
            const RestartLoaded rec = w.read_record("R-4");
            // the first turn that holds a group move (a machine gives an order every 0.7 s from the end of the start dialog on, and the room discards what it is sent before its first turn: protocol 12)
            size_t move_turn = 0;
            while (move_turn < rec.turns.size()) {
                bool has_move = false;
                for (const sim::Command& c : rec.turns[move_turn].commands) has_move = has_move || c.type == sim::CommandType::GroupMove;
                if (has_move) break;
                ++move_turn;
            }
            ASSERT_TRUE(move_turn < 60);
            size_t frame_of_turn = 0;                                                   // (the record's frames: the head, one frame a turn, a checkpoint after every 20th turn)
            {
                const std::vector<uint8_t> bytes = record_bytes(w, "R-4");
                const auto frames = frames_of(bytes);
                size_t turns_seen = 0;
                for (size_t i = 1; i < frames.size(); ++i) {
                    if (bytes[frames[i].first] != 2) continue;
                    if (turns_seen++ == move_turn) {
                        frame_of_turn = i;
                        break;
                    }
                }
            }
            ASSERT_TRUE(frame_of_turn != 0);
            change_frame(w, "R-4", frame_of_turn, [](std::vector<uint8_t>& p) { p.resize(8); p[6] = 0; p[7] = 0; });   // that turn without its commands (first turn, count 1, then a command count of 0)
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended);
            ASSERT_TRUE(w.report.items[0].note.find("does not agree") != std::string::npos);
        }
        {   // a head that this server would not make (a wait of 0 s), a log that cannot hold the match, a budget for logs that cannot, and no room for the room
            PWorld w("persist-79e");
            w.start_server(500);
            crash_with_record(w, "R-5", 20000);
            change_head(w, "R-5", [](RestartHead& h) { h.wait_ms = 0; });
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.items[0].note.find("would not make") != std::string::npos);
        }
        {
            PWorld w("persist-79f");
            w.start_server(500);
            crash_with_record(w, "R-6", 20000);
            change_head(w, "R-6", [](RestartHead& h) { h.max_log_bytes = 1024; });
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.items[0].note.find("turn log cannot hold") != std::string::npos);
            ASSERT_EQ(w.mgr->log_bytes(), uint64_t{0});                                 // (the half-built room gave its log back)
        }
        {
            PWorld w("persist-79g");
            w.start_server(500);
            crash_with_record(w, "R-7", 20000);
            w.limits.log_budget_bytes = 1000;                                            // the server's memory for logs cannot hold even the first allocation of one
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.items[0].note.find("turn log cannot hold") != std::string::npos);
        }
        {   // two records and room for one room: one is restored, the other is ended with the reason (and has no room to show it in: it is only in the log)
            PWorld w("persist-79h");
            w.start_server(500);
            RoomSpec one = held_spec("R-8", 2);
            RoomSpec two = held_spec("R-9", 2);
            ASSERT_TRUE(w.mgr->create_room(one, w.server_now()).ok && w.mgr->create_room(two, w.server_now()).ok);
            for (const char* code : {"R-8", "R-9"}) {
                for (int p = 0; p < 2; ++p) w.connect(std::string("Q") + std::to_string(p), code).reconnects = false;
            }
            w.run(8000 + kPre);
            ASSERT_TRUE(w.status("R-8").state == RoomState::Running && w.status("R-9").state == RoomState::Running);
            w.stop_server(false);
            w.limits.max_rooms = 1;
            w.start_server(500);
            ASSERT_EQ(w.report.items.size(), size_t{2});
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.count(RestoreItem::Outcome::Ended) == 1);
            ASSERT_EQ(w.mgr->room_count(), size_t{1});
            ASSERT_TRUE(w.record_files().size() == 1);                                   // the restored room's record stays, the other is deleted
        }
    } TEST_END();
}


void run_persist_server_tests_2() {
    TEST_CASE("S3.86 Files That Are No Records Are Refused Safely At The Start (An Empty File, Garbage, A Head That Is Cut Short, A Record Of Another Format, A Record With A Flipped Bit In The Middle, A Text, A File Bigger Than A Record May Be, A Link; A Folder With A Record's Name Is Not Looked At): Each Is Named In The Log With A Reason And Deleted, Nothing Crashes Or Waits, The Good Record Next To Them Is Restored, The Files That Are Not Records' Names Are Left Alone, And The Server Then Makes Rooms As Usual") {
        PWorld w("persist-80");
        w.start_server(500);
        {
            RoomSpec spec = held_spec("G-1", 2);
            std::vector<RClient*> m = play_room(w, spec, 8000);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);
        }
        const fs::path dir = w.restart.dir;
        const std::vector<uint8_t> good = record_bytes(w, "G-1");
        const auto frames = frames_of(good);
        const auto plant = [&](const std::string& name, const std::vector<uint8_t>& bytes) { write_all_bytes(dir / name, bytes); };
        plant("room-EMPTY-00000001.restart", {});
        {
            std::vector<uint8_t> junk;
            uint32_t rng = 777;
            for (int i = 0; i < 500; ++i) {
                rng = rng * 1664525u + 1013904223u;
                junk.push_back(static_cast<uint8_t>(rng >> 16));
            }
            plant("room-JUNK-00000002.restart", junk);
        }
        plant("room-CUT-00000003.restart", std::vector<uint8_t>(good.begin(), good.begin() + 60));                            // the head is cut short
        {
            std::vector<uint8_t> v2 = good;
            v2[sizeof(kRestartMagic) - 1] = '2';
            plant("room-FORMAT2-00000004.restart", v2);
        }
        {
            std::vector<uint8_t> rot = good;
            rot[(frames[3].first + frames[3].second) / 2] ^= 0x40;                                                              // a bit flipped inside the third turn's frame
            plant("room-BITROT-00000005.restart", rot);
        }
        plant("room-TEXT-00000006.restart", std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'});
        fs::create_directories(dir / "room-DIR-00000007.restart");
        {
            std::ofstream out(dir / "room-HUGE-00000008.restart", std::ios::binary);
            out.put('x');
        }
        fs::resize_file(dir / "room-HUGE-00000008.restart", 60ull * 1024 * 1024);                                               // more than the 48 MiB of a record
#ifndef _WIN32
        fs::create_symlink(w.record_path("G-1"), dir / "room-LINK-00000009.restart");
#endif
        plant("notes.txt", {'n'});
        plant("other.restart", good);                                                                                           // (a name that is not a record's: no "room-" in front)
        w.start_server(500);
#ifndef _WIN32
        const size_t bad = 8;
#else
        const size_t bad = 7;
#endif
        ASSERT_EQ(w.report.items.size(), 1 + bad);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.count(RestoreItem::Outcome::Unreadable) == bad && w.report.count(RestoreItem::Outcome::Ended) == 0);
        for (const RestoreItem& i : w.report.items) {
            if (i.outcome == RestoreItem::Outcome::Restored) continue;
            ASSERT_MSG(!i.note.empty() && i.code.empty() && i.file.rfind("room-", 0) == 0, i.file);
            bool logged = false;
            for (const std::string& n : w.notices) logged = logged || (n.find(i.file) != std::string::npos && n.find(i.note) != std::string::npos);
            ASSERT_MSG(logged, i.file);                                                                                         // named in the log, with the reason
            ASSERT_MSG(!fs::exists(dir / i.file) && !fs::is_symlink(dir / i.file), i.file);                                     // and deleted
        }
        ASSERT_TRUE(fs::exists(dir / "room-DIR-00000007.restart") && fs::is_directory(dir / "room-DIR-00000007.restart"));      // a folder is no record: left alone
        ASSERT_TRUE(fs::exists(dir / "notes.txt") && fs::exists(dir / "other.restart"));
        ASSERT_TRUE(w.status("G-1").restored && w.status("G-1").state == RoomState::Running);
        ASSERT_TRUE(fs::exists(w.record_path("G-1")));                                                                          // (the link pointed at the good record: the link went, the record stayed)
        for (const RestoreItem& i : w.report.items) {                                                                           // the reasons are of the kinds that they are
            if (i.file.find("EMPTY") != std::string::npos || i.file.find("TEXT") != std::string::npos) ASSERT_TRUE(i.note.find("record") != std::string::npos);
            if (i.file.find("FORMAT2") != std::string::npos) ASSERT_TRUE(i.note.find("another format") != std::string::npos);
            if (i.file.find("BITROT") != std::string::npos) ASSERT_TRUE(i.note.find("corrupt") != std::string::npos && i.note.find("checksum") != std::string::npos);
            if (i.file.find("HUGE") != std::string::npos) ASSERT_TRUE(i.note.find("too big") != std::string::npos);
            if (i.file.find("CUT") != std::string::npos) ASSERT_TRUE(i.note.find("corrupt") != std::string::npos);
        }
        // the server goes on as usual
        ASSERT_TRUE(w.mgr->create_room(held_spec("N-1", 2), w.server_now()).ok);
        std::vector<RClient*> fresh;
        fresh.push_back(&w.connect("Ann", "N-1"));
        fresh.push_back(&w.connect("Bob", "N-1"));
        w.run(6000 + kPre);
        ASSERT_TRUE(w.status("N-1").state == RoomState::Running && w.status("N-1").record_kept);
    } TEST_END();

    TEST_CASE("S3.87 A Record Whose Last Frame Was Cut Short (A Write That The Death Of The Machine Interrupted) Is Read As Far As It Is Whole, The Torn Bytes Are Cut Off And The Room Goes On Writing After The Last Good Frame; A Record That Lacks The Last Seconds (The Machine Died And The Disk Never Had Them) Is Restored To That Tick, And The Machines That Were Ahead Of It Are Told BadRequest (They Must Start The Match From Nothing, Which They Can: Their Keys Are Good) And Do, And The Match Goes On To The End Identical") {
        {
            PWorld w("persist-81a");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("T-1", 2), 8000);
            const uint32_t sealed = w.status("T-1").turns;
            w.stop_server(false);
            std::vector<uint8_t> bytes = record_bytes(w, "T-1");
            const size_t whole = bytes.size();
            const std::vector<uint8_t> next = test_frame(2, turns_payload(sealed, {sample_turn(sealed)}));
            bytes.insert(bytes.end(), next.begin(), next.begin() + 9);                                     // the first nine bytes of the next frame
            write_all_bytes(w.record_path("T-1"), bytes);
            ASSERT_TRUE(read_restart_record(w.record_path("T-1"), 1ull << 30).torn);
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("T-1").restored_turns == sealed);
            ASSERT_EQ(fs::file_size(w.record_path("T-1")), whole);                                          // the torn bytes are cut off: the next frame follows the last whole one
            ASSERT_TRUE(w.until([&]() { return !w.status("T-1").paused; }, 60000));
            w.run(6000);
            const RoomStatus s = w.status("T-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.turns > sealed + 80 && s.rejoins == 2);
            const RestartLoaded rec = w.read_record("T-1");
            ASSERT_TRUE(rec.ok() && !rec.torn && rec.turns.size() == s.turns);                              // the record is whole again and has grown
            w.play_to_the_end("T-1");
            ASSERT_TRUE(w.status("T-1").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[0]->sim.state_hash().total == w.status("T-1").referee_hash);
        }
        {
            PWorld w("persist-81b");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("T-2", 2), 8000);
            const uint32_t sealed = w.status("T-2").turns;
            const net::SeatKey key0 = m[0]->lobby->key();
            const net::SeatKey key1 = m[1]->lobby->key();
            const uint8_t seat0 = m[0]->lobby->my_seat();
            const uint8_t seat1 = m[1]->lobby->my_seat();
            ASSERT_TRUE(m[0]->session->runner().next_turn_expected() + 3 >= sealed);                       // the machines have (nearly) every turn of the room
            w.stop_server(false);
            // the disk never had the last ten turns: the record is cut back to a frame boundary that holds `sealed - 10` turns
            const std::vector<uint8_t> bytes = record_bytes(w, "T-2");
            const auto frames = frames_of(bytes);
            size_t turns_seen = 0;
            size_t cut = frames[0].second;
            for (size_t i = 1; i < frames.size(); ++i) {
                if (bytes[frames[i].first] == 2) {
                    if (turns_seen == sealed - 10) break;
                    ++turns_seen;
                }
                cut = frames[i].second;
            }
            write_all_bytes(w.record_path("T-2"), std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)));
            const RestartLoaded rolled_back = w.read_record("T-2");
            ASSERT_TRUE(rolled_back.ok() && rolled_back.turns.size() == sealed - 10);
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("T-2").restored_turns == sealed - 10);
            w.run(8000);                                                                                   // the machines try: they are ahead of the record
            for (RClient* p : m) ASSERT_TRUE(p->lost && p->was_rejected && p->rejected == net::RejectReason::BadRequest);     // told so (more turns than were ever sealed)
            ASSERT_TRUE(w.status("T-2").paused && w.status("T-2").absent.size() == 2 && w.status("T-2").rejoins == 0 && w.status("T-2").rejoins_refused == 0);        // (the seats are held, the refusal cost them nothing)
            // they start the match from nothing, with their keys (what a client that finds itself ahead of a restarted server has to do)
            RClient& a2 = w.connect("P0", "T-2", seat0, key0);
            RClient& b2 = w.connect("P1", "T-2", seat1, key1);
            ASSERT_TRUE(w.until([&]() { return !w.status("T-2").paused && a2.session != nullptr && b2.session != nullptr && a2.session->mode() == net::ClientSession::Mode::Normal && b2.session->mode() == net::ClientSession::Mode::Normal; }, 60000));
            w.run(5000);
            w.play_to_the_end("T-2");
            const RoomStatus end = w.status("T-2");
            ASSERT_TRUE(end.state == RoomState::Finished && end.rejoins == 2);
            ASSERT_TRUE(a2.sim.state_hash() == b2.sim.state_hash() && a2.sim.state_hash().total == end.referee_hash && a2.sim.is_match_over());
            ASSERT_FALSE(a2.session->desynced() || b2.session->desynced() || a2.lost || b2.lost);
        }
    } TEST_END();

    TEST_CASE("S3.88 The Record Always Holds What Any Machine Has Run (Written Before The Turn Is Sent): In A Match Of Three Players, At Every 10 ms Step Of 30 s Of Play The File Holds Exactly The Turns That The Room Has Sealed, And No Machine Has Received A Turn That The File Lacks (So A Process That Is Killed At Any Moment Loses Nothing That A Player Has Seen)") {
        PWorld w("persist-82");
        w.start_server(500);
        ASSERT_TRUE(w.mgr->create_room(held_spec("W-1", 3), w.server_now()).ok);
        std::vector<RClient*> m;
        for (int p = 0; p < 3; ++p) m.push_back(&w.connect(std::string("P") + std::to_string(p), "W-1"));
        uint32_t checks = 0;
        uint32_t most_turns = 0;
        for (int step = 0; step < 3500; ++step) {
            w.run(10);
            const RoomStatus s = w.status("W-1");
            if (s.state != RoomState::Running) continue;
            const RestartLoaded rec = w.read_record("W-1");                                                // (read with no flush and no stop: whatever the process has handed to the operating system)
            ASSERT_TRUE(rec.ok() && !rec.torn);
            ASSERT_EQ(rec.turns.size(), static_cast<size_t>(s.turns));                                    // everything that was sealed is in the file ...
            for (RClient* p : m) {
                if (p->session == nullptr) continue;
                ASSERT_TRUE(p->session->runner().next_turn_expected() <= rec.turns.size());                // ... and no machine has a turn that the file lacks
            }
            most_turns = s.turns;
            ++checks;
        }
        ASSERT_TRUE(checks > 2500 && most_turns > 400);
    } TEST_END();
}


void run_persist_server_tests_3() {
    TEST_CASE("S3.89 The Disk Has Limits And The Match Does Not Suffer From Them: A Record That Passes The Size Of A Record, One That The Server's Budget Cannot Hold (At Its Head, And As It Grows), A Write That The Disk Refuses (EFBIG), A Turn Log That Passed Its Limit: Each Ends The Record (The File Is Deleted: A Record That Stops In The Middle Of A Match Would Bring It Back At The Wrong Tick), The Room's Status Says Why, The Log Gets A Line, The Budget Is Whole Again And The Match Goes On To Its End On The Same State Everywhere")  {
        {   // the size of one record
            PWorld w("persist-83a");
            w.restart.max_record_bytes = 3000;
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("D-1", 2), 14000);                // 280 turns: a record of 4 KB
            const RoomStatus s = w.status("D-1");
            ASSERT_TRUE(s.state == RoomState::Running && !s.record_kept && s.record_bytes == 0 && s.record_note.find("size that a record may have") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("D-1") != std::string::npos && n.find("restart record is gone") != std::string::npos && n.find("size that a record may have") != std::string::npos);
            ASSERT_TRUE(noted);
            ASSERT_EQ(w.mgr->restart_store()->records_failed(), 1u);
            ASSERT_TRUE(w.mgr->restart_store()->used_bytes() == 0 && w.mgr->restart_store()->open_records() == 0);
            ASSERT_TRUE(status_json(w, "D-1").get("record").get("kept").as_bool_or(true) == false && status_json(w, "D-1").get("record").get("note").str() == s.record_note);
            w.play_to_the_end("D-1");                                                          // the match is not touched by it
            ASSERT_TRUE(w.status("D-1").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[0]->sim.state_hash().total == w.status("D-1").referee_hash);
        }
        {   // the server's budget: a second room cannot even make its head, the first one's record ends when it outgrows what is left, and the budget is whole afterwards
            uint64_t head_bytes = 0;
            {
                PWorld probe("persist-83b-probe");
                probe.start_server(500);
                ASSERT_TRUE(probe.mgr->create_room(held_spec("B-0", 2), probe.server_now()).ok);
                probe.connect("A", "B-0");
                probe.connect("B", "B-0");
                ASSERT_TRUE(probe.until([&]() { return probe.status("B-0").state == RoomState::Running; }, 10000));
                head_bytes = probe.status("B-0").record_bytes;                                 // (a record that holds a head and nothing else)
                ASSERT_TRUE(head_bytes > 100 && head_bytes < 1000);
            }
            PWorld w("persist-83b");
            w.restart.budget_bytes = 2 * head_bytes - 20;
            w.start_server(500);
            for (const char* code : {"B-1", "B-2"}) {
                ASSERT_TRUE(w.mgr->create_room(held_spec(code, 2), w.server_now()).ok);
                w.connect("A", code);
                w.connect("B", code);
            }
            ASSERT_TRUE(w.until([&]() { return w.status("B-1").state == RoomState::Running && w.status("B-2").state == RoomState::Running; }, 10000));
            w.run(1500);                                                                       // (before the first turn of either: the 5 s of the dialog)
            const RoomStatus first = w.status("B-1");
            const RoomStatus second = w.status("B-2");
            ASSERT_TRUE(first.record_kept && first.record_bytes == head_bytes);
            ASSERT_TRUE(!second.record_kept && second.record_note.find("no restart record could be made") != std::string::npos && second.record_note.find("budget") != std::string::npos);
            ASSERT_EQ(w.record_files().size(), size_t{1});
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("B-2") != std::string::npos && n.find("budget") != std::string::npos);
            ASSERT_TRUE(noted);
            w.run(8000);                                                                       // the first room's record outgrows the budget: it ends (and its bytes come back)
            const RoomStatus later = w.status("B-1");
            ASSERT_TRUE(later.state == RoomState::Running && !later.record_kept && later.record_note.find("budget") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty() && w.mgr->restart_store()->used_bytes() == 0);
            ASSERT_TRUE(w.mgr->create_room(held_spec("B-3", 2), w.server_now()).ok);          // and a room that is made now has the budget again
            w.connect("A", "B-3");
            w.connect("B", "B-3");
            ASSERT_TRUE(w.until([&]() { return w.status("B-3").state == RoomState::Running; }, 10000));
            ASSERT_TRUE(w.status("B-3").record_kept);
        }
#ifndef _WIN32
        {   // a write that the disk refuses (the process may not grow a file beyond 2500 bytes: EFBIG, as for a full disk)
            PWorld w("persist-83c");
            w.start_server(500);
            std::vector<RClient*> m;
            {
                const FileSizeLimit limit(2500);
                m = play_room(w, held_spec("D-3", 2), 14000);
            }
            const RoomStatus s = w.status("D-3");
            ASSERT_TRUE(s.state == RoomState::Running && !s.record_kept && s.record_note.find("the disk refused a write") != std::string::npos && s.record_note.find("File too large") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());                                             // the file that stopped in the middle of the match is deleted
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("D-3") != std::string::npos && n.find("the disk refused a write") != std::string::npos);
            ASSERT_TRUE(noted && w.mgr->restart_store()->records_failed() == 1 && w.mgr->restart_store()->used_bytes() == 0);
            w.play_to_the_end("D-3");
            ASSERT_TRUE(w.status("D-3").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash());
        }
#endif
        {   // the turn log of the room passed its limit: the room can no longer give the match to a player who comes back, so a restart could not hold its seats: the record ends
            PWorld w("persist-83d");
            w.start_server(500);
            RoomSpec spec = held_spec("D-4", 2);
            spec.max_log_bytes = 1536;
            std::vector<RClient*> m = play_room(w, spec, 40000);
            const RoomStatus s = w.status("D-4");
            ASSERT_TRUE(s.state == RoomState::Running && !s.log_usable && !s.record_kept && s.record_note.find("turn log") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("D-4") != std::string::npos && n.find("turn log") != std::string::npos);
            ASSERT_TRUE(noted);
            w.play_to_the_end("D-4");
            ASSERT_TRUE(w.status("D-4").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash());
        }
    } TEST_END();

    TEST_CASE("S3.90 A Record Lives As Long As Its Room Has A Match To Bring Back: A Waiting Room Has None; The Start Makes It And A Cancelled Start Deletes It (A Machine That Cannot Load The Map); A Running Room Has One Until The Owner Closes It (DELETE /rooms/<code>) Or The Match Ends; A Room That Holds No Seats Has None (The Status Says Why); A Server That Keeps No Records Says So; The Server's Stop Keeps The Records Of The Rooms That Have One And Closes The Others")  {
        {
            PWorld w("persist-84");
            w.start_server(500);
            ASSERT_TRUE(w.mgr->create_room(held_spec("C-1", 2), w.server_now()).ok);
            RoomStatus s = w.status("C-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.record_kept && s.record_note.find("has not started") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            w.connect("Ann", "C-1");
            w.run(500);
            ASSERT_TRUE(w.record_files().empty());                                             // (one player: still waiting)
            RClient& bad = w.connect("Bob", "C-1");
            bad.fail_load = true;                                                              // Bob cannot load the map: the start is cancelled
            bool seen_while_loading = false;
            for (int i = 0; i < 400; ++i) {
                w.run(10);
                const RoomStatus now_status = w.status("C-1");
                if (now_status.state == RoomState::Loading) {
                    seen_while_loading = seen_while_loading || (now_status.record_kept && w.record_files().size() == 1);
                }
                if (now_status.state == RoomState::Waiting && seen_while_loading) break;
            }
            ASSERT_TRUE(seen_while_loading);                                                   // the start made a record (the match is fixed: the start message, the keys) ...
            s = w.status("C-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.record_kept);
            ASSERT_TRUE(w.record_files().empty());                                             // ... and its cancel took it away again
            ASSERT_TRUE(s.record_note.find("has not started") != std::string::npos);
        }
        {   // a running room: the record until the owner closes the room
            PWorld w("persist-84b");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("C-2", 2), 3000);
            ASSERT_TRUE(w.status("C-2").record_kept && w.record_files().size() == 1);
            ctl::HttpRequest rq;
            rq.method = "DELETE";
            rq.path = "/rooms/C-2";
            const ctl::HttpResponse r = handle_control(*w.mgr, rq, w.server_now());
            ASSERT_EQ(r.status, 200);
            ASSERT_TRUE(w.record_files().empty());                                             // closed by its owner: nothing to bring back
            const RoomStatus s = w.status("C-2");
            ASSERT_TRUE(s.state == RoomState::Failed && !s.record_kept && s.record_note == "the room is over");
            ASSERT_EQ(w.mgr->restart_store()->used_bytes(), uint64_t{0});
        }
        {   // a room that holds no seats: no record, and the status says why; the stop closes it
            PWorld w("persist-84c");
            w.start_server(500);
            RoomSpec spec = spec_of("C-3", 2);                                                  // (reconnect off)
            ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
            w.connect("Ann", "C-3");
            w.connect("Bob", "C-3");
            w.run(3000 + kPre);
            RoomStatus s = w.status("C-3");
            ASSERT_TRUE(s.state == RoomState::Running && !s.reconnect && !s.record_kept && s.record_note.find("holds no seats") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            // and the stop: one room with a record and this one without
            ASSERT_TRUE(w.mgr->create_room(held_spec("C-4", 2), w.server_now()).ok);
            w.connect("Cat", "C-4");
            w.connect("Dan", "C-4");
            w.run(3000 + kPre);
            ASSERT_TRUE(w.status("C-4").record_kept);
            w.stop_server(true);
            ASSERT_EQ(w.kept_at_stop, size_t{1});                                              // the record of the room that holds seats is kept ...
            ASSERT_TRUE(w.record_files().size() == 1 && fs::exists(w.record_path("C-4")));
            w.start_server(500);                                                               // ... and only that room is back
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].code == "C-4" && w.report.items[0].outcome == RestoreItem::Outcome::Restored);
            RoomStatus gone;
            ASSERT_FALSE(w.mgr->status("C-3", gone, w.server_now()));
        }
        {   // a server that keeps no records says so in every room's status
            PWorld w("persist-84d");
            w.restart.dir.clear();
            w.start_server(500);
            ASSERT_TRUE(w.mgr->restart_store() == nullptr);
            ASSERT_TRUE(w.mgr->create_room(held_spec("C-5", 2), w.server_now()).ok);
            RoomStatus s = w.status("C-5");
            ASSERT_TRUE(!s.record_kept && s.record_note == "this server keeps no restart records");
            w.connect("Ann", "C-5");
            w.connect("Bob", "C-5");
            w.run(3000 + kPre);
            s = w.status("C-5");
            ASSERT_TRUE(s.state == RoomState::Running && !s.record_kept && s.record_note == "this server keeps no restart records");
            ASSERT_EQ(w.mgr->shutdown(w.server_now()), size_t{0});                              // (and the stop closes its rooms, as it always did)
            ASSERT_TRUE(w.status("C-5").state == RoomState::Failed);
        }
    } TEST_END();

    TEST_CASE("S3.91 No Key Is In Any Text The Server Makes: The Record Holds The Keys (As Bytes: The Test Looks For Them There First), And They Are In No Notice For The Log, No Status (Running, Restored, Refused, Finished), No JSON Of The Control Interface, No List, No Result Of An Ended Room, Through A Restart And To The Match's End") {
        PWorld w("persist-85");
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("K-1", 3), 6000);
        std::vector<net::SeatKey> keys;
        for (RClient* p : m) keys.push_back(p->lobby->key());
        const std::vector<uint8_t> bytes = record_bytes(w, "K-1");
        for (const net::SeatKey& k : keys) {                                                   // the keys ARE in the record: the search is not a search for nothing
            ASSERT_TRUE(!net::key_is_zero(k) && std::search(bytes.begin(), bytes.end(), k.begin(), k.end()) != bytes.end());
        }
        std::vector<std::string> texts;
        const auto collect = [&]() {
            for (const std::string& n : w.notices) texts.push_back(n);
            for (const char* code : {"K-1", "K-2"}) {
                RoomStatus s;
                if (w.mgr != nullptr && w.mgr->status(code, s, w.server_now())) {
                    texts.push_back(status_json_text(w, code));
                    texts.push_back(ctl::to_json(status_to_json(s)));
                }
            }
            if (w.mgr != nullptr) {
                ctl::HttpRequest rq;
                rq.method = "GET";
                rq.path = "/rooms";
                texts.push_back(handle_control(*w.mgr, rq, w.server_now()).body);
                rq.path = "/stats";
                texts.push_back(handle_control(*w.mgr, rq, w.server_now()).body);
            }
        };
        collect();
        w.stop_server(true);
        collect();
        w.start_server(500);                                                                   // (the restart: the room comes back; the log's lines about it are in w.notices)
        collect();
        ASSERT_TRUE(w.until([&]() { return !w.status("K-1").paused; }, 90000));
        w.run(3000);
        collect();
        // a second room whose record cannot be restored (another network protocol): the failed room and its report
        w.stop_server(false);
        w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
        w.start_server(500);
        collect();
        for (const RoomStatus& ended : w.mgr->take_ended(w.server_now())) texts.push_back(ctl::to_json(status_to_json(ended)));
        w.restart.identity.protocol = net::kProtocolVersion;
        size_t looked_at = 0;
        for (const std::string& t : texts) {
            for (const net::SeatKey& k : keys) ASSERT_TRUE(t.find(hex_of(k)) == std::string::npos);
            looked_at += t.size();
        }
        ASSERT_TRUE(texts.size() >= 14 && looked_at > 5000);
        // the record of a room is for its owner only
#ifndef _WIN32
        {
            PWorld p("persist-85b");
            p.start_server(500);
            std::vector<RClient*> q = play_room(p, held_spec("K-3", 2), 3000);
            struct stat st;
            ASSERT_EQ(::stat(p.record_path("K-3").c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);
            ASSERT_EQ(::stat(p.restart.dir.c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);
        }
#endif
    } TEST_END();
}


void run_persist_server_tests_4() {
    TEST_CASE("S3.92 After A Restart The Room Waits For Its Players As For A Lost Link, With The Restart's Longer Wait: Of Three Players Two Come Back And One Never Does: No Vote Is Open At 30 s, None At 85 s, The Vote Opens At 90 s About The Seat That Is Missing (Two Voters), One Vote Of Two Does Not Win It And The Second Does, The Seat Is Dropped And The Match Goes On (A Quit Ends It), Whatever The New Server's Clock Is (0.5 s, A Second Before Its 32-Bit Wrap, Half Way); A Seat That Had Been Dropped Before The Restart Is Told So By Its Key; A Room Whose Players Never Come Back Ends 'Everybody Left' At The Cap")  {
        const uint32_t clocks[] = {500u, 0xFFFFFC18u, 0x7FFFFFF0u};
        for (const uint32_t clock : clocks) {
            PWorld w("persist-86");
            w.start_server(7000);
            const RoomSpec spec = held_spec("V-1", 3);
            std::vector<RClient*> m = play_room(w, spec, 6000);
            const uint8_t seat_a = m[0]->lobby->my_seat();
            const uint8_t seat_c = m[2]->lobby->my_seat();
            w.stop_server(false);
            m[2]->reconnects = false;                                                          // Cat's machine does not come back
            w.start_server(clock);
            const uint32_t restarted = w.now;
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1);
            ASSERT_TRUE(w.until([&]() { return w.status("V-1").rejoins == 2; }, 30000));
            const auto since_restart = [&]() { return w.now - restarted; };
            while (since_restart() < 40000) w.run(500);
            RoomStatus s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.absent[0].seat == seat_c && s.absent[0].away_s >= 38 && s.absent[0].away_s <= 42 && s.vote_seat == 255);       // 40 s: a lost link's 30 s are over, the restart's wait is not
            while (since_restart() < 85000) w.run(500);
            s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.vote_seat == 255);
            while (since_restart() < 92000) w.run(500);
            s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.vote_seat == seat_c && s.voters == 2 && s.votes_continue == 0);
            ASSERT_TRUE(m[0]->session->vote(seat_c, true));                                    // one vote of two: not more than half
            w.run(600);
            s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.votes_continue == 1 && s.vote_seat == seat_c);
            ASSERT_TRUE(m[1]->session->vote(seat_c, true));                                    // two of two
            ASSERT_TRUE(w.until([&]() { return !w.status("V-1").paused; }, 5000));
            s = w.status("V-1");
            ASSERT_TRUE(s.drops_by_vote == 1 && s.drops_by_cap == 0 && s.absent.empty() && s.state == RoomState::Running);
            const uint32_t turns_then = s.turns;
            w.run(3000);
            ASSERT_TRUE(w.status("V-1").turns > turns_then + 40);                              // the match goes on without Cat
            sim::Command quit;
            quit.type = sim::CommandType::Quit;
            quit.issuer = seat_a;
            ASSERT_TRUE(m[0]->session->submit(quit));
            ASSERT_TRUE(w.until([&]() { return w.status("V-1").state == RoomState::Finished; }, 20000));
            w.run(3000);
            const RoomStatus end = w.status("V-1");
            ASSERT_TRUE(end.state == RoomState::Finished && end.quitter == seat_a && end.drops_by_vote == 1);
            ASSERT_TRUE(m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[0]->sim.state_hash().total == end.referee_hash);
            ASSERT_TRUE(m[0]->sim.is_player_dropped(seat_c) && w.record_files().empty());
        }
        {   // a seat that had left before the restart is Dropped after it: its key is told so, and nobody waits for it
            PWorld w("persist-86b");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("V-2", 3), 6000);
            const net::SeatKey key_b = m[1]->lobby->key();
            const uint8_t seat_b = m[1]->lobby->my_seat();
            m[1]->session->leave();                                                            // Bob quits: his Drop is sealed
            w.run(1500);
            ASSERT_TRUE(w.status("V-2").state == RoomState::Running && !w.status("V-2").paused);
            w.stop_server(false);
            w.start_server(500);
            RoomStatus s = w.status("V-2");
            ASSERT_TRUE(s.restored && s.paused && s.absent.size() == 2);                       // (Ann and Cat: Bob is gone for good)
            for (const RoomStatus::Absent& a : s.absent) ASSERT_TRUE(a.seat != seat_b);
            net::Connection* link = w.open_link();
            net::HelloMsg hello;
            hello.room = "V-2";
            hello.key = key_b;
            hello.have_turns = 0;
            link->send(net::encode(hello));
            w.run(600);
            ASSERT_EQ(reject_on(link), static_cast<int>(net::RejectReason::Dropped));          // "Sorry, you have been dropped from the game"
            ASSERT_TRUE(w.until([&]() { return !w.status("V-2").paused; }, 60000));            // the others come back: nobody waits for Bob
            ASSERT_TRUE(w.status("V-2").rejoins == 2 && w.status("V-2").state == RoomState::Running);
        }
        {   // nobody comes back: the room ends by the existing rules (the pause's cap: here 60 s), "everybody left"
            PWorld w("persist-86c");
            w.start_server(500);
            RoomSpec spec = held_spec("V-3", 2);
            spec.max_pause_ms = 60000;
            std::vector<RClient*> m = play_room(w, spec, 6000);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);
            w.start_server(500);
            w.run(58000);
            RoomStatus s = w.status("V-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.absent.size() == 2 && s.drops_by_cap == 0);
            w.run(4000);
            s = w.status("V-3");
            ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "everybody left" && s.drops_by_cap == 2 && s.drops_by_vote == 0);
            ASSERT_TRUE(w.record_files().empty() && w.mgr->take_ended(w.server_now()).size() == 1);
        }
    } TEST_END();

    TEST_CASE("S3.93 The Bots Of A Room Sit Down Again At The Restored Tick And Play On: A Room Of One Person And A Bot Of Its Specification, And A Room Whose Leader's START Filled The Empty Seats; After The Restart The Status Lists The Bots (Fill Marked As Fill), The Controller Is There, The Bot Is Never Absent (Only The Person Is), And When The Person Is Back The Bots Give Orders Again (Commands Of Their Seats In Turns After The Restored One); A Quit Ends The Match With The Same State On The Person's Machine And The Referee") {
        const auto commands_of_seat = [](const RestartLoaded& rec, size_t from, uint8_t seat) {
            size_t n = 0;
            for (size_t i = from; i < rec.turns.size(); ++i) {
                for (const sim::Command& c : rec.turns[i].commands) n += c.issuer == seat ? 1u : 0u;
            }
            return n;
        };
        {   // the room's own bot
            PWorld w("persist-87");
            w.start_server(500);
            RoomSpec spec = held_spec("B-1", 2);
            spec.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Medium});
            ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
            RClient& person = w.connect("Pat", "B-1");
            person.record_hashes = true;
            w.run(14000 + kPre);
            RoomStatus s = w.status("B-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.bot_controller && s.bots.size() == 1 && s.bots[0].seat == 1 && s.bots[0].name == bn("B-1", 1, "Medium") && !s.bots[0].fill && s.joined == 2 && s.record_kept);
            const uint8_t seat_person = person.lobby->my_seat();
            ASSERT_EQ(seat_person, 0);
            RestartLoaded before = w.read_record("B-1");
            ASSERT_TRUE(before.ok() && commands_of_seat(before, 0, 1) > 0 && s.bot_decisions >= 10);          // the bot gives orders (its harvest order, once: the ants go on by themselves)
            ASSERT_TRUE(before.head.bots.size() == 1 && before.head.fill_mask == 0 && before.head.keys[1] == net::SeatKey{});     // (no key for a bot's seat)
            const size_t sealed = before.turns.size();
            w.stop_server(false);
            w.start_server(500);
            s = w.status("B-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && s.bot_controller && s.bots.size() == 1 && s.bots[0].name == bn("B-1", 1, "Medium") && s.joined == 2);
            ASSERT_TRUE(s.absent.size() == 1 && s.absent[0].seat == seat_person);              // only the person is missing: a bot is never absent
            ASSERT_TRUE(w.until([&]() { return !w.status("B-1").paused; }, 60000));
            w.run(14000);
            const RestartLoaded after = w.read_record("B-1");
            ASSERT_TRUE(after.ok() && after.turns.size() > sealed + 200);
            s = w.status("B-1");
            ASSERT_TRUE(s.bot_controller && s.bot_decisions >= 10);                            // the bot looks at the match again, from the restored tick on (a new controller: it counts from 0; a Medium bot looks every 20 ticks)
            sim::Command quit;
            quit.type = sim::CommandType::Quit;
            quit.issuer = seat_person;
            ASSERT_TRUE(person.session->submit(quit));
            ASSERT_TRUE(w.until([&]() { return w.status("B-1").state == RoomState::Finished; }, 20000));
            w.run(3000);
            ASSERT_TRUE(person.sim.state_hash().total == w.status("B-1").referee_hash && !person.session->desynced());
        }
        {   // a restart in the seconds before the first turn: the room comes back at turn 0, the bot acts as in any match that begins (its first look is on tick 1 + its seat, its order follows its reaction time),
            // and nothing of the pre-start is needed to bring the match back (a command that a machine gave in the dialog is in turn 0, or is not: the room restores sealed turns, nothing else)
            PWorld w("persist-87c");
            w.start_server(500);
            RoomSpec spec = held_spec("B-3", 2);
            spec.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Hard});
            ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
            RClient& person = w.connect("Pat", "B-3");
            ASSERT_TRUE(w.until([&]() { return w.status("B-3").state == RoomState::Running; }, 10000));
            w.run(2500);
            ASSERT_EQ(w.status("B-3").turns, 0u);                                              // (in the dialog: nothing is sealed yet)
            ASSERT_TRUE(w.status("B-3").record_kept && w.read_record("B-3").turns.empty());
            w.stop_server(false);
            w.start_server(500);
            RoomStatus s = w.status("B-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.restored_turns == 0 && s.turns == 0 && s.paused && s.bot_controller && s.absent.size() == 1);
            ASSERT_TRUE(w.until([&]() { return !w.status("B-3").paused; }, 60000));
            w.run(12000);
            const RestartLoaded after = w.read_record("B-3");
            ASSERT_TRUE(after.ok() && after.turns.size() > 200);
            ASSERT_TRUE(commands_of_seat(after, 0, 1) > 0);                                    // the bot's first order, as in any match
            ASSERT_TRUE(w.status("B-3").bot_decisions >= 10);
            ASSERT_TRUE(person.session->mode() == net::ClientSession::Mode::Normal && !person.lost);
        }
        {   // the leader's START with a fill: two bots that the room seated
            PWorld w("persist-87b");
            w.start_server(500);
            ASSERT_TRUE(w.mgr->create_room(held_spec("B-2", 3), w.server_now()).ok);
            RClient& leader = w.connect("Lea", "B-2");
            w.run(1500);
            ASSERT_TRUE(leader.end->send(net::encode(net::StartRequestMsg::all(net::FillLevel::Medium))));
            ASSERT_TRUE(w.until([&]() { return w.status("B-2").state == RoomState::Running; }, 20000));
            w.run(12000 + kPre);
            RoomStatus s = w.status("B-2");
            ASSERT_TRUE(s.bots.size() == 2 && s.bots[0].fill && s.bots[1].fill && s.joined == 3 && s.record_kept);
            const RestartLoaded before = w.read_record("B-2");
            ASSERT_TRUE(before.ok() && before.head.fill_mask == 0x06 && before.head.bots.size() == 2);
            const size_t sealed = before.turns.size();
            w.stop_server(true);
            w.start_server(500);
            s = w.status("B-2");
            ASSERT_TRUE(s.restored && s.bot_controller && s.bots.size() == 2 && s.bots[0].fill && s.bots[1].fill && s.bots[0].name == bn("B-2", s.bots[0].seat, "Medium") && s.joined == 3);
            ASSERT_TRUE(s.absent.size() == 1 && s.paused);
            ASSERT_TRUE(w.until([&]() { return !w.status("B-2").paused; }, 60000));
            w.run(12000);
            const RestartLoaded after = w.read_record("B-2");
            ASSERT_TRUE(after.ok() && after.turns.size() > sealed + 200);
            ASSERT_TRUE(w.status("B-2").bot_controller && w.status("B-2").bot_decisions >= 20);          // two bots look again, each every 20 ticks
        }
    } TEST_END();

    TEST_CASE("S3.127 Protocol 13, A Restart Record Keeps What The Leader's START Chose: Its Start Message Has The Pair, Its Bots Their Own Levels; The Restored Referee Makes The Same Teams (A Replay That Did Not Would Be Refused At The First Checkpoint), Its Status Says So, The Machine That Comes Back With Its Turns Keeps Its Engine And A Machine That Comes Back From Nothing Is Given The Start With The Teams And Stands At The Referee's State") {
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
        PWorld w("persist-125");
        w.start_server(500);
        ASSERT_TRUE(w.mgr->create_room(held_spec("T-1", 4), w.server_now()).ok);
        RClient& leader = w.connect("Lea", "T-1");
        w.run(1500);
        net::StartRequestMsg request;                                                          // Medium at seat 1, Hard at seat 2, Easy at seat 3; Red + Blue (1 + 2) are a team, so are Green + Black
        request.fill = {FillLevel::None, FillLevel::Medium, FillLevel::Hard, FillLevel::Easy};
        request.set_teams(sim::StartTeams{true, 1, 2});
        ASSERT_TRUE(leader.end->send(net::encode(request)));
        ASSERT_TRUE(w.until([&]() { return w.status("T-1").state == RoomState::Running; }, 20000));
        w.run(9000 + kPre);
        RoomStatus s = w.status("T-1");
        ASSERT_TRUE(s.teams == "1+2" && allies_text(s.allies) == "3210" && s.bots.size() == 3 && s.record_kept);
        ASSERT_EQ(engine_allies(leader.sim), std::string("3210"));
        const RestartLoaded before = w.read_record("T-1");
        ASSERT_TRUE(before.ok() && before.head.start.team_a == 1 && before.head.start.team_b == 2 && before.head.fill_mask == 0x0E);      // the record's Start has the pair
        ASSERT_TRUE(before.head.bots.size() == 3);
        ASSERT_TRUE(before.head.bots[0].seat == 1 && before.head.bots[0].level == ai::Level::Medium && before.head.bots[1].seat == 2 && before.head.bots[1].level == ai::Level::Hard &&
                    before.head.bots[2].seat == 3 && before.head.bots[2].level == ai::Level::Easy);                                   // ... and each bot its own level
        ASSERT_TRUE(before.checks.size() >= 1);                                                // (the referee's hash at turn 19 is in it: what a replay has to meet)
        const net::SeatKey key = leader.lobby->key();
        const uint8_t seat = leader.lobby->my_seat();
        w.stop_server(true);
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1);                      // (a replay that had not made the teams would have been refused at turn 19: "the replay does not agree")
        s = w.status("T-1");
        ASSERT_TRUE(s.restored && s.teams == "1+2" && allies_text(s.allies) == "3210" && s.bots.size() == 3);
        ASSERT_TRUE(s.bots[0].level == "medium" && s.bots[1].level == "hard" && s.bots[2].level == "easy" && s.bots[0].fill && s.bots[1].fill && s.bots[2].fill);
        ASSERT_TRUE(w.until([&]() { return !w.status("T-1").paused; }, 60000));               // the leader came back with its turns: its engine was kept
        w.run(5000);
        s = w.status("T-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && allies_text(s.allies) == "3210");
        ASSERT_TRUE(leader.session->mode() == net::ClientSession::Mode::Normal && !leader.session->desynced() && !leader.lost);
        ASSERT_EQ(engine_allies(leader.sim), std::string("3210"));
        // a second restart; this time the leader's machine is a page that was reloaded: it starts from nothing, is sent the Start of the match with the teams and catches up to the referee
        w.stop_server(true);
        leader.reconnects = false;
        w.start_server(500);
        RClient& reloaded = w.connect("Lea", "T-1", seat, key);
        ASSERT_TRUE(w.until([&]() { return reloaded.lobby != nullptr && reloaded.lobby->rejoined() && reloaded.session != nullptr; }, 30000));
        ASSERT_TRUE(reloaded.lobby->start_info().team_a == 1 && reloaded.lobby->start_info().team_b == 2);      // the Start that a machine from nothing is sent has the teams
        ASSERT_TRUE(w.until([&]() { return !w.status("T-1").paused && reloaded.session->mode() == net::ClientSession::Mode::Normal; }, 60000));
        w.run(3000);
        s = w.status("T-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.teams == "1+2" && allies_text(s.allies) == "3210");
        ASSERT_EQ(engine_allies(reloaded.sim), std::string("3210"));                           // its own engine, made from the Start: the teams were made before it ran a tick
        ASSERT_TRUE(!reloaded.session->desynced() && !reloaded.lost);
        w.play_to_the_end("T-1");                                                              // the match is played to its end: the machine from nothing stands at the referee's final state
        s = w.status("T-1");
        ASSERT_TRUE(s.state == RoomState::Finished && s.referee_hash != 0);
        ASSERT_TRUE(reloaded.sim.state_hash().total == s.referee_hash && !reloaded.session->desynced() && !reloaded.lost);
        ASSERT_EQ(engine_allies(reloaded.sim), std::string("3210"));                           // (the bots kept their teams to the end)
    } TEST_END();

    TEST_CASE("S3.128 A Record Of Protocol 12, 13, 14, 15 Or 16 (Its Start Message Lacks What Later Protocols Added: The Team Bytes, The Platform Bytes, The Game Mode) Is Read As The Start That Its Release Wrote And Refused For Its Protocol, Like A Record Of Any Other Protocol: At The Restart (Run Through With A Record Of Protocol 12; The Parser Reads The Other Two The Same Way) It Is Not Restored (Nothing Of It Is Replayed), Its Room Is A Failed Room That Names The Protocols, The File Is Kept Whole For A Day (Not Deleted As Corrupt) And The Log Says So; An Old Layout Under A Protocol That Did Not Write It (11, Or 13 Under 12, Or 15 Under 14, Or 17) Stays Unreadable; The Same Record In This Build's Layout Is Restored") {
        PWorld w("persist-126");
        w.start_server(500);
        const auto crash = [](PWorld& world, const char* code) {                               // a match that is played for a while, and a server that dies with its record
            std::vector<RClient*> m = play_room(world, held_spec(code, 2), 8000);
            for (RClient* p : m) p->reconnects = false;
            world.stop_server(false);
        };
        crash(w, "OLD-1");
        const std::vector<uint8_t> bytes = read_all_bytes(w.record_path("OLD-1"));
        const auto frames = frames_of(bytes);
        const RestartLoaded rec = parse_restart_record(bytes.data(), bytes.size());
        ASSERT_TRUE(rec.ok() && rec.head.start.team_a == net::kNoTeam && rec.head.identity.protocol == net::kProtocolVersion);
        const std::vector<uint8_t> start = net::encode(rec.head.start);                        // the Start inside the head, in this build's layout
        // the record as protocol `protocol` with the old layout wrote it: the Start without what later protocols added at its end (`lacking` bytes: protocol 12's has neither the two team bytes nor the four
        // platform bytes of protocol 15 nor the mode byte of protocol 17 (7), protocol 13's and 14's lack the platform bytes and the mode (5), protocol 15's and 16's lack the mode (1)), its length in front of it, and the protocol in the identity
        const auto old_layout = [&](uint16_t protocol, size_t lacking) {
            std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].first + 5), bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second - 4));
            const auto found = std::search(payload.begin(), payload.end(), start.begin(), start.end());
            if (found == payload.end()) throw std::runtime_error("the Start is not in the record's head");
            const size_t at = static_cast<size_t>(found - payload.begin());
            payload.erase(payload.begin() + static_cast<std::ptrdiff_t>(at + start.size() - lacking), payload.begin() + static_cast<std::ptrdiff_t>(at + start.size()));      // the old Start: the same bytes without what came later ...
            const uint16_t old_length = static_cast<uint16_t>(start.size() - lacking);
            payload[at - 2] = static_cast<uint8_t>(old_length & 0xFFu);                        // ... and its length in front of it
            payload[at - 1] = static_cast<uint8_t>(old_length >> 8);
            const size_t protocol_at = 2u + 1u + payload[2];                                   // the identity: format (u16), game version (str8), then the protocol (u16)
            payload[protocol_at] = static_cast<uint8_t>(protocol & 0xFFu);
            payload[protocol_at + 1] = static_cast<uint8_t>(protocol >> 8);
            std::vector<uint8_t> record = with_magic(test_frame(1, payload));
            record.insert(record.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end());
            return record;
        };
        const std::vector<uint8_t> v12 = old_layout(12, 7);
        {   // the parser: protocol 12's record reads, as a Start without teams, without platforms and without a game mode (the rest of it is what this build reads), and that is what the refusal judges
            const RestartLoaded old = parse_restart_record(v12.data(), v12.size());
            ASSERT_TRUE(old.ok() && old.head.identity.protocol == 12);
            ASSERT_TRUE(old.head.start.team_a == net::kNoTeam && old.head.start.team_b == net::kNoTeam && old.head.start.roster == rec.head.start.roster && old.head.start.seed == rec.head.start.seed);
            for (const uint8_t platform : old.head.start.platforms) ASSERT_EQ(platform, net::kPlatformUnknown);
            ASSERT_TRUE(old.turn_count == rec.turn_count && old.turn_count > 100 && !old.checks.empty() && old.head.code == "OLD-1" && old.head.players == 2);
            ASSERT_EQ(old.head.start.mode, 0);                                                 // (a record of a release before the game modes is a match of the original's rules)
            // the layout of protocols 13 and 14 (the team bytes, no platforms) reads for them: the same record of a release that wrote it
            for (const uint16_t protocol : {uint16_t{13}, uint16_t{14}}) {
                const std::vector<uint8_t> v = old_layout(protocol, 5);
                const RestartLoaded r = parse_restart_record(v.data(), v.size());
                ASSERT_TRUE(r.ok() && r.head.identity.protocol == protocol);
                ASSERT_TRUE(r.head.start.team_a == net::kNoTeam && r.head.start.team_b == net::kNoTeam && r.head.start.roster == rec.head.start.roster && r.head.start.seed == rec.head.start.seed);
                for (const uint8_t platform : r.head.start.platforms) ASSERT_EQ(platform, net::kPlatformUnknown);
                ASSERT_EQ(r.head.start.mode, 0);
            }
            // the layout of protocols 15 and 16 (the teams and the platforms, no game mode) reads for them: the platforms are the record's own, the mode is 0
            for (const uint16_t protocol : {uint16_t{15}, uint16_t{16}}) {
                const std::vector<uint8_t> v = old_layout(protocol, 1);
                const RestartLoaded r = parse_restart_record(v.data(), v.size());
                ASSERT_TRUE(r.ok() && r.head.identity.protocol == protocol);
                ASSERT_TRUE(r.head.start.team_a == rec.head.start.team_a && r.head.start.team_b == rec.head.start.team_b && r.head.start.roster == rec.head.start.roster && r.head.start.seed == rec.head.start.seed);
                ASSERT_TRUE(r.head.start.platforms == rec.head.start.platforms);
                ASSERT_EQ(r.head.start.mode, 0);
                ASSERT_TRUE(r.turn_count == rec.turn_count && !r.checks.empty() && r.head.code == "OLD-1");
            }
            // each old layout is no Start of any other protocol: it is read only for the releases that wrote it
            for (const uint16_t other : {uint16_t{1}, uint16_t{11}, uint16_t{13}, uint16_t{14}, uint16_t{15}, uint16_t{16}, static_cast<uint16_t>(net::kProtocolVersion), static_cast<uint16_t>(net::kProtocolVersion + 1)}) {
                const std::vector<uint8_t> not_12 = old_layout(other, 7);                      // (protocol 12's layout under another number)
                const RestartLoaded r = parse_restart_record(not_12.data(), not_12.size());
                ASSERT_TRUE(!r.ok() && r.why.find("start message") != std::string::npos);
            }
            for (const uint16_t other : {uint16_t{1}, uint16_t{11}, uint16_t{12}, uint16_t{15}, uint16_t{16}, static_cast<uint16_t>(net::kProtocolVersion), static_cast<uint16_t>(net::kProtocolVersion + 1)}) {
                const std::vector<uint8_t> not_13 = old_layout(other, 5);                      // (the layout of 13 and 14 under another number)
                const RestartLoaded r = parse_restart_record(not_13.data(), not_13.size());
                ASSERT_TRUE(!r.ok() && r.why.find("start message") != std::string::npos);
            }
            for (const uint16_t other : {uint16_t{1}, uint16_t{11}, uint16_t{12}, uint16_t{13}, uint16_t{14}, static_cast<uint16_t>(net::kProtocolVersion), static_cast<uint16_t>(net::kProtocolVersion + 1)}) {
                const std::vector<uint8_t> not_15 = old_layout(other, 1);                      // (the layout of 15 and 16 under another number: protocol 17's Start has the mode byte)
                const RestartLoaded r = parse_restart_record(not_15.data(), not_15.size());
                ASSERT_TRUE(!r.ok() && r.why.find("start message") != std::string::npos);
            }
        }
        write_all_bytes(w.record_path("OLD-1"), v12);
        w.start_server(500);
        ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.count(RestoreItem::Outcome::Restored) == 0 && w.report.count(RestoreItem::Outcome::Unreadable) == 0);
        const std::string note = w.report.items[0].note;                                       // the protocol rule's sentence: both protocols and the reason
        ASSERT_TRUE(note.find("network protocol 12") != std::string::npos && note.find("this server speaks protocol " + std::to_string(net::kProtocolVersion)) != std::string::npos &&
                    note.find("change of the rules") != std::string::npos);
        ASSERT_TRUE(w.record_files().empty());                                                 // (not in the folder that a start reads any more)
        const fs::path kept = fs::path(w.restart.dir) / "refused" / fs::path(w.record_path("OLD-1")).filename();
        ASSERT_TRUE(fs::exists(kept) && read_all_bytes(kept) == v12);                          // kept whole for a day: the owner may want it back (a corrupt record is deleted at once)
        const RoomStatus failed = w.status("OLD-1");                                           // its room: failed, with the reason, nothing replayed
        ASSERT_TRUE(failed.state == RoomState::Failed && failed.reason == note && !failed.restored && !failed.record_kept && failed.ticks > 100);
        bool logged = false;
        bool kept_logged = false;
        for (const std::string& n : w.notices) {
            logged = logged || (n.find("was not restored") != std::string::npos && n.find("network protocol 12") != std::string::npos);
            kept_logged = kept_logged || (n.find("OLD-1") != std::string::npos && n.find("refused") != std::string::npos && n.find("24 hours") != std::string::npos);
        }
        ASSERT_TRUE(logged && kept_logged);
        {   // a record of protocol 16 (the layout without the game mode) is read and refused for its protocol like the others: its room fails and names the protocols, the record is kept whole
            PWorld v16("persist-126c");
            v16.start_server(500);
            const std::vector<uint8_t> record16 = old_layout(16, 1);
            write_all_bytes(v16.record_path("OLD-1"), record16);
            v16.start_server(500);
            ASSERT_TRUE(v16.report.items.size() == 1 && v16.report.items[0].outcome == RestoreItem::Outcome::Ended && v16.report.count(RestoreItem::Outcome::Restored) == 0 && v16.report.count(RestoreItem::Outcome::Unreadable) == 0);
            ASSERT_TRUE(v16.report.items[0].note.find("network protocol 16") != std::string::npos && v16.report.items[0].note.find("this server speaks protocol " + std::to_string(net::kProtocolVersion)) != std::string::npos);
            ASSERT_TRUE(v16.status("OLD-1").state == RoomState::Failed && !v16.status("OLD-1").restored);
            ASSERT_TRUE(v16.record_files().empty());
            ASSERT_TRUE(read_all_bytes(fs::path(v16.restart.dir) / "refused" / fs::path(v16.record_path("OLD-1")).filename()) == record16);
        }
        PWorld control("persist-126b");                                                        // the same match, as this build writes it: restored
        control.start_server(500);
        crash(control, "NEW-1");
        control.start_server(500);
        ASSERT_TRUE(control.report.count(RestoreItem::Outcome::Restored) == 1 && control.status("NEW-1").restored);
    } TEST_END();

    TEST_CASE("S3.187 A Match Of Game Mode 187 Survives A Restart Of The Server (Protocol 17): The Mode Is In The Start Of The Record's Head (A Room Made With The Mode Keeps It, Every Machine Played It), The Restored Room Plays It Again From The Record (Its State Is That Of An Independent Replay Of The Record Made With The Mode, And Is Not That Of The Original's Game), Says It In Its Status, And The Machines That Come Back With Their Keys Go On In Sync; A Machine That Starts From Nothing Is Sent The Start With The Mode") {
        PWorld w("persist-187");
        w.start_server(500);
        RoomSpec spec = held_spec("G-187", 2);
        spec.mode = 1;
        std::vector<RClient*> m = play_room(w, spec, 12000);
        RoomStatus s = w.status("G-187");
        ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && s.record_kept && static_cast<int>(s.mode) == 1 && !s.restored);
        for (RClient* p : m) ASSERT_TRUE(p->sim.game_mode() == sim::GameMode::Kills187 && p->lobby->start_info().mode == 1);
        RestartLoaded rec = w.read_record("G-187");
        ASSERT_TRUE(rec.ok() && !rec.torn && rec.head.start.mode == 1 && rec.head.start.game_mode() == sim::GameMode::Kills187 && rec.head.identity.protocol == net::kProtocolVersion);
        for (const RestartCheck& c : rec.checks) {                                           // the checkpoints are the machines' own states: the referee's hash (the server's engine of mode 187) and the machines' agree
            for (RClient* p : m) {
                const auto at = p->hash_at.find(uint64_t{c.turn} + 1);
                if (at != p->hash_at.end()) ASSERT_EQ(at->second, c.hash);
            }
        }
        const uint32_t sealed_at_stop = s.turns;
        w.stop_server(true);
        w.run(3000);
        rec = w.read_record("G-187");
        ASSERT_TRUE(rec.ok() && rec.turns.size() == sealed_at_stop && rec.head.start.mode == 1);
        w.start_server(500);
        ASSERT_EQ(w.report.items.size(), size_t{1});
        ASSERT_TRUE(w.report.items[0].outcome == RestoreItem::Outcome::Restored && w.report.items[0].code == "G-187" && w.report.items[0].turns == sealed_at_stop);
        s = w.status("G-187");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.turns == sealed_at_stop && static_cast<int>(s.mode) == 1);
        ASSERT_EQ(status_to_json(s).get("mode").str(), std::string("187"));
        ASSERT_EQ(s.restored_hash, replay_hash(rec, rec.turns.size()));                      // the state that the room stands at is that of an independent replay of the record, made with the mode
        RestartLoaded as_original = rec;                                                     // (the same turns played as the original's game are another state)
        as_original.head.start.mode = 0;
        ASSERT_TRUE(replay_hash(as_original, rec.turns.size()) != s.restored_hash);
        ASSERT_TRUE(w.until([&]() { return !w.status("G-187").paused; }, 90000));
        ASSERT_TRUE(w.until([&]() { return m[0]->session->mode() == net::ClientSession::Mode::Normal && m[1]->session->mode() == net::ClientSession::Mode::Normal; }, 3000));
        w.run(6000);
        s = w.status("G-187");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns > sealed_at_stop + 100 && s.rejoins == 2 && static_cast<int>(s.mode) == 1);
        for (RClient* p : m) ASSERT_TRUE(!p->lost && !p->session->desynced() && p->sim.game_mode() == sim::GameMode::Kills187);
    } TEST_END();

    TEST_CASE("S3.94 A Match That Had Ended When The Server Stopped (Its Last Turn Was In The Record, The Room Had Not Finished Yet) Is Finished At The Restart With The Same Final State As The Match That Was Never Interrupted: The Result Rows, The Referee's Hash And The Report Of Its End Are There, Its Record Is Deleted, Nobody Is Waited For")  {
        PWorld w("persist-88");
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("O-1", 2), 6000);
        sim::Command quit;
        quit.type = sim::CommandType::Quit;
        quit.issuer = m[1]->lobby->my_seat();
        ASSERT_TRUE(m[1]->session->submit(quit));
        std::vector<uint8_t> last_copy;                                                        // the record as it was at the last step before the room finished: it holds the turn of the Quit
        bool finished = false;
        for (int step = 0; step < 3000 && !finished; ++step) {
            w.run(10);
            if (w.status("O-1").state == RoomState::Finished) {
                finished = true;
            } else if (fs::exists(w.record_path("O-1"))) {
                last_copy = record_bytes(w, "O-1");
            }
        }
        ASSERT_TRUE(finished);
        const RoomStatus first_end = w.status("O-1");
        ASSERT_TRUE(first_end.referee_hash != 0 && first_end.quitter == quit.issuer && !first_end.rows.empty());
        ASSERT_TRUE(w.record_files().empty());
        ASSERT_FALSE(last_copy.empty());
        const RestartLoaded copy = parse_restart_record(last_copy.data(), last_copy.size());
        ASSERT_TRUE(copy.ok());
        bool quit_in_record = false;
        for (const net::TurnMsg& t : copy.turns) {
            for (const sim::Command& c : t.commands) quit_in_record = quit_in_record || c.type == sim::CommandType::Quit;
        }
        ASSERT_TRUE(quit_in_record);                                                           // (the copy was taken in the window between the Quit's turn and the end of the room)
        w.stop_server(false);
        write_all_bytes(w.record_path("O-1"), last_copy);                                      // the old server died in that window
        w.start_server(500);
        ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Restored && w.report.items[0].note.find("the match had ended") != std::string::npos);
        const RoomStatus s = w.status("O-1");
        ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "the match had ended when the server stopped" && s.restored);
        ASSERT_EQ(s.referee_hash, first_end.referee_hash);                                     // the final state of the match that was never interrupted, hash for hash
        ASSERT_TRUE(s.quitter == first_end.quitter && s.rows.size() == first_end.rows.size() && s.rows[0].winner == first_end.rows[0].winner && s.rows[0].score == first_end.rows[0].score);
        ASSERT_TRUE(w.record_files().empty());                                                 // its record is deleted
        const std::vector<RoomStatus> ended = w.mgr->take_ended(w.server_now());               // and its end is reported, with the result file's content
        ASSERT_TRUE(ended.size() == 1 && ended[0].code == "O-1" && ended[0].state == RoomState::Finished);
        ASSERT_TRUE(status_to_json(ended[0]).get("state_hash").str().size() == 16);
    } TEST_END();

    TEST_CASE("S3.97 The Switch Is On (Release B): Rooms Hold Seats By Default (ServerLimits, A Room Specification Made By The Manager, A Room Made By The Control Interface Without A \"reconnect\" Key, A Public Room) And A Room's Own Key Wins Either Way; A Server That Was Told Not To (--no-reconnect: ServerLimits::reconnect false) Holds None By Default And The Rooms That Say So; A Restored Room Holds Seats Whatever The Default Is Now (It Held Them When It Was Written)")  {
        ASSERT_TRUE(kReconnectByDefault);                                                      // the switch itself: a test of its value, not only of the plumbing behind it
        ASSERT_TRUE(ServerLimits().reconnect);
        World dflt;
        ASSERT_TRUE(dflt.mgr.default_spec().reconnect);
        {   // the control interface
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-1\"}";
            ctl::HttpResponse r = handle_control(dflt.mgr, rq, dflt.now);
            ASSERT_EQ(r.status, 201);
            ASSERT_TRUE(dflt.status("SW-1").reconnect);
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-2\", \"reconnect\": true}";
            ASSERT_EQ(handle_control(dflt.mgr, rq, dflt.now).status, 201);
            ASSERT_TRUE(dflt.status("SW-2").reconnect);
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-3\", \"reconnect\": false}";
            ASSERT_EQ(handle_control(dflt.mgr, rq, dflt.now).status, 201);
            ASSERT_FALSE(dflt.status("SW-3").reconnect);
        }
        {   // a public room follows the default
            ServerLimits l;
            l.demo_rooms = 2;
            l.demo_map = "TINY.LVL";
            World w(l);
            w.connect_creating("Ann", "abcd1234", block_of());
            w.run(500);
            ASSERT_TRUE(w.status("abcd1234").reconnect);
        }
        {   // a server that does not hold seats by default (--no-reconnect): none of its rooms does unless it says so, the public rooms included
            ServerLimits l;
            l.reconnect = false;
            l.demo_rooms = 2;
            l.demo_map = "TINY.LVL";
            World w(l);
            ASSERT_FALSE(w.mgr.default_spec().reconnect);
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-6\"}";
            ASSERT_EQ(handle_control(w.mgr, rq, w.now).status, 201);
            ASSERT_FALSE(w.status("SW-6").reconnect);
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-7\", \"reconnect\": true}";
            ASSERT_EQ(handle_control(w.mgr, rq, w.now).status, 201);
            ASSERT_TRUE(w.status("SW-7").reconnect);
            w.connect_creating("Ann", "abcd1234", block_of());
            w.run(500);
            ASSERT_FALSE(w.status("abcd1234").reconnect);
        }
        {   // a server that holds seats by default, said out loud: the same default for every room that does not say
            ServerLimits l;
            l.reconnect = true;
            World w(l);
            ASSERT_TRUE(w.mgr.default_spec().reconnect);
            RoomSpec spec = w.mgr.default_spec();
            spec.code = "SW-4";
            spec.map = "TINY.LVL";
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok && w.status("SW-4").reconnect);
        }
        {   // a server that does not (a restart's record is of a room that held seats: it holds them now, whatever the server's default is)
            ServerLimits l;
            l.reconnect = false;
            PWorld w("persist-91", l);
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("SW-5", 2), 4000);
            ASSERT_TRUE(w.status("SW-5").reconnect && w.status("SW-5").record_kept);
            w.stop_server(true);
            w.start_server(500);
            ASSERT_TRUE(w.status("SW-5").reconnect && w.status("SW-5").restored && w.status("SW-5").record_kept);
        }
    } TEST_END();
}


void run_persist_server_tests_5() {
    TEST_CASE("S3.98 The Room's Wall-Clock Limit Counts The Play That Came Before The Restart (Not The Time The Match Waited For Its Players): A Room With A Limit Of 70 s Whose Match Had Been Played For 35 s When The Server Stopped Fails 35 s Of Play After The Players Are Back, Not 70") {
        PWorld w("persist-92");
        w.start_server(500);
        RoomSpec spec = held_spec("L-1", 2);
        spec.run_ms = 70000;
        std::vector<RClient*> m = play_room(w, spec, 30000);                                   // 5 s of dialog and 30 s of play: 35 s of the match
        ASSERT_EQ(w.status("L-1").state, RoomState::Running);
        w.stop_server(true);
        w.run(20000);                                                                          // the server is away for 20 s: that is no play
        w.start_server(500);
        ASSERT_TRUE(w.until([&]() { return !w.status("L-1").paused; }, 60000));
        w.run(28000);
        ASSERT_EQ(w.status("L-1").state, RoomState::Running);                                  // 28 s of play after the return: 63 s of the match, the limit is 70
        w.run(12000);
        const RoomStatus s = w.status("L-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("longer than the room's limit") != std::string::npos);
        ASSERT_TRUE(w.record_files().empty());
    } TEST_END();

    TEST_CASE("S3.99 A Record Keeps No Address Of A Client: The Start Message That The Room Sent To Machines That Announce A Port Names Their Addresses (Host Migration's Business, A Game On The Local Network's); The Record's Start Message Has None, And Neither Has The Start That A Machine From Nothing Is Sent After A Restart")  {
        PWorld w("persist-93");
        w.announce_port = 4321;
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("E-1", 2), 3000);
        const net::StartMsg& live = m[0]->lobby->start_info();
        size_t named = 0;
        for (const net::Endpoint& e : live.endpoints) named += e.port == 4321 && !e.address.empty() ? 1u : 0u;
        ASSERT_EQ(named, size_t{2});                                                           // (the live Start names the two machines: the premise of the test)
        const RestartLoaded rec = w.read_record("E-1");
        ASSERT_TRUE(rec.ok());
        for (const net::Endpoint& e : rec.head.start.endpoints) ASSERT_TRUE(e.address.empty() && e.port == 0);
        const net::SeatKey key = m[1]->lobby->key();
        const uint8_t seat = m[1]->lobby->my_seat();
        w.stop_server(false);
        m[1]->reconnects = false;
        w.start_server(500);
        RClient& reloaded = w.connect("P1", "E-1", seat, key);                                 // a machine from nothing: it is sent the room's Start message
        ASSERT_TRUE(w.until([&]() { return reloaded.lobby != nullptr && reloaded.lobby->phase() != net::ClientLobby::Phase::Connecting && reloaded.lobby->phase() != net::ClientLobby::Phase::Joining; }, 20000));
        w.run(500);
        for (const net::Endpoint& e : reloaded.lobby->start_info().endpoints) ASSERT_TRUE(e.address.empty() && e.port == 0);
        ASSERT_EQ(reloaded.lobby->start_info().seed, live.seed);
    } TEST_END();
}


void run_persist_server_tests_6() {
    TEST_CASE("S3.100 What A Record Costs (Measured): Its Size For A Minute Of Play (An Idle Match, A Busy One Of Three Players And Of Four), The Cost Of Writing A Turn (One write() Of A Few Dozen Bytes) And Of The Flush Once A Second, And The Time It Takes To Bring A Match Back (Three Minutes Of Play Here; ANTS_PERSIST_MEASURE_LONG=1 Measures The Longest Plays Of TINY And Of TREASURE): The Numbers Are Printed, The Bounds Are Generous (A Record Of A Minute Under 120 KB, A Turn Under 1 ms, A Flush Under 250 ms, A Restore Of A Minute Of Play Under 5 s)")  {
        const auto seconds_since = [](const std::chrono::steady_clock::time_point& t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        // the size of a minute of play
        {
            double idle_per_minute = 0;
            double busy3_per_minute = 0;
            double busy4_per_minute = 0;
            for (int variant = 0; variant < 3; ++variant) {
                PWorld w("persist-94a");
                w.start_server(500);
                const uint8_t players = variant == 2 ? 4 : 3;
                std::vector<RClient*> m = play_room(w, held_spec("Z-1", players), 10000);
                if (variant == 0) {
                    for (RClient* p : m) p->orders = false;                                    // nobody gives an order: the idle match
                }
                const uint64_t at_start = w.status("Z-1").record_bytes;
                const uint32_t turns_start = w.status("Z-1").turns;
                w.run(60000);
                const RoomStatus s = w.status("Z-1");
                const double per_minute = static_cast<double>(s.record_bytes - at_start) / (static_cast<double>(s.turns - turns_start) / 1200.0);
                (variant == 0 ? idle_per_minute : variant == 1 ? busy3_per_minute : busy4_per_minute) = per_minute;
            }
            std::cout << "\n      [measured] a minute of play adds " << static_cast<int>(idle_per_minute) << " bytes to a record when nobody gives an order, " << static_cast<int>(busy3_per_minute)
                      << " for three players who each give an order every 0.7 s, " << static_cast<int>(busy4_per_minute) << " for four" << std::flush;
            ASSERT_TRUE(idle_per_minute > 15000 && idle_per_minute < 40000);              // 1,200 turns at 17 bytes and 60 checkpoints: about 21 KB
            ASSERT_TRUE(busy3_per_minute < 120000 && busy4_per_minute < 120000 && busy3_per_minute > idle_per_minute);
        }
        // the cost of writing a turn, and of the flush (the folder is the test's own: on a real disk)
        {
            RestartConfig cfg = test_restart_config("persist-94b");
            RestartStore store(cfg);
            std::string why;
            ASSERT_TRUE(store.prepare(why));
            auto writer = store.create(sample_head("M-1"), why);
            ASSERT_TRUE(writer != nullptr);
            const uint32_t turns = 100000;
            std::vector<net::TurnMsg> prepared;
            for (uint32_t n = 0; n < 1000; ++n) {
                prepared.push_back(sample_turn(n % 11 == 4 ? n : n + 1));
                prepared.back().commands.erase(std::remove_if(prepared.back().commands.begin(), prepared.back().commands.end(), [](const sim::Command& c) { return c.type == sim::CommandType::Drop; }), prepared.back().commands.end());
            }
            const auto t0 = std::chrono::steady_clock::now();
            for (uint32_t n = 0; n < turns; ++n) {
                net::TurnMsg t = prepared[n % prepared.size()];
                t.turn = n;
                if (!writer->append_turn(t)) break;
            }
            const double write_seconds = seconds_since(t0);
            ASSERT_EQ(writer->turns(), turns);
            const double write_us = write_seconds * 1e6 / turns;
            // The 100,000 turns are in the operating system's cache, and the first flush writes all of them out (2.4 MB): it is no flush of a second's turns, which are about 20, so it is made first and told apart
            const auto b0 = std::chrono::steady_clock::now();
            ASSERT_TRUE(writer->sync());
            const double backlog_ms = seconds_since(b0) * 1000.0;
            // A flush of a second's turns: 30 of them are a trial. Noise only adds time, so the cost of the flush is the cheapest trial's, and a trial is cheap when its mean and its worst are: a shared runner's disk has
            // slow moments (the backlog took 3 to 5 s on the Windows ones now and then, and the flushes after it were slow for a few seconds, 340 ms at the worst), and what is slow now is quick again a little later,
            // so the flush is cheap if one of three trials, two seconds apart, is
            const double mean_limit_ms = 100.0;
            const double worst_limit_ms = 2000.0;
            const int syncs = 30;
            double mean_sync_ms = 0;
            double worst_sync_ms = 0;
            std::string slow_trials;                                                      // what the trials that were not cheap measured, so that a failure shows all of them
            int trials = 0;
            while (trials < 3 && (trials == 0 || mean_sync_ms >= mean_limit_ms || worst_sync_ms >= worst_limit_ms)) {
                if (trials > 0) {
                    slow_trials += "; trial " + std::to_string(trials) + " took " + std::to_string(static_cast<int>(mean_sync_ms)) + " ms on average, " + std::to_string(static_cast<int>(worst_sync_ms)) + " ms at the worst";
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
                ++trials;
                double total_sync_ms = 0;
                worst_sync_ms = 0;
                for (int i = 0; i < syncs; ++i) {
                    for (uint32_t n = 0; n < 20; ++n) {
                        net::TurnMsg t = prepared[n];
                        t.turn = writer->turns();
                        ASSERT_TRUE(writer->append_turn(t));
                    }
                    const auto s0 = std::chrono::steady_clock::now();
                    ASSERT_TRUE(writer->sync());
                    const double ms = seconds_since(s0) * 1000.0;
                    worst_sync_ms = std::max(worst_sync_ms, ms);
                    total_sync_ms += ms;
                }
                mean_sync_ms = total_sync_ms / syncs;
            }
            std::cout << "\n      [measured] writing a turn costs " << write_us << " microseconds (100,000 turns in " << write_seconds << " s, " << writer->bytes() / 1024 << " KiB); the first flush, of all of them, " << backlog_ms
                      << " ms; a flush of a second's turns costs " << mean_sync_ms << " ms on average, " << worst_sync_ms << " ms at the worst of " << syncs << " (the limits are " << mean_limit_ms << " and " << worst_limit_ms << ")" << (trials > 1 ? " (trial " + std::to_string(trials) + slow_trials + ")" : std::string()) << std::flush;
            ASSERT_TRUE(write_us < 1000.0);
            ASSERT_TRUE(backlog_ms < 30000.0);                                            // (the backlog of 2.4 MB is printed and not judged, a disk may take seconds for it; it only has to end)
            ASSERT_TRUE(mean_sync_ms < mean_limit_ms);                                    // (the flush is cheap)
            ASSERT_TRUE(worst_sync_ms < worst_limit_ms);                                  // (the worst only says that no flush blocks for seconds)
        }
        // the time to bring a match back
        {
            struct Run {
                const char* map;
                uint8_t players;
                uint32_t minutes;
            };
            std::vector<Run> runs = {{"TINY.LVL", 3, 3}};
            if (std::getenv("ANTS_PERSIST_MEASURE_LONG") != nullptr) {
                runs.push_back(Run{"TINY.LVL", 3, 5});                                         // (a minute short of its 6 minutes)
                runs.push_back(Run{"TREASURE.LVL", 4, 12});                                    // (its whole match is 12 minutes: 14,400 ticks; this is 20 s short of the end)
            }
            for (const Run& r : runs) {
                PWorld w("persist-94c");
                w.start_server(500);
                RoomSpec spec = held_spec("Z-2", r.players, r.map);
                spec.run_ms = 24u * 3600u * 1000u;
                std::vector<RClient*> m = play_room(w, spec, r.minutes * 60000u - 20000u);                  // (the match ends at its minutes of play: a little before)
                for (RClient* p : m) p->reconnects = false;
                const uint32_t sealed = w.status("Z-2").turns;
                const uint64_t bytes = w.status("Z-2").record_bytes;
                w.stop_server(false);
                const auto t0 = std::chrono::steady_clock::now();
                w.start_server(500);
                const double total_seconds = seconds_since(t0);
                ASSERT_EQ(w.report.count(RestoreItem::Outcome::Restored), size_t{1});
                const RoomStatus s = w.status("Z-2");
                std::cout << "\n      [measured] a match of " << r.minutes << " minutes (less 20 s) of play on " << r.map << " with " << static_cast<int>(r.players) << " players: " << sealed << " turns, a record of " << bytes / 1024
                          << " KiB, brought back in " << total_seconds << " s (the replay " << s.restore_ms << " ms: " << static_cast<double>(sealed) / std::max(1.0, static_cast<double>(s.restore_ms)) * 1000.0 << " turns a second)" << std::flush;
                ASSERT_TRUE(s.restored && s.restored_turns == sealed);
                ASSERT_TRUE(total_seconds < 5.0 * r.minutes);
            }
        }
    } TEST_END();

    TEST_CASE("S3.102 A Restored Room Is A Match That Runs, As Far As /busy Counts (The Public Answer That A Deploy Waits On), For Five Minutes (M3 Of The Review): After A Restart Of The Server Two Rooms Whose Players Have Not Come Back Count Two Matches And Their People (The Seats Are Held, A Restart Now Would Interrupt Them Again; A Bot Is No Person) up to 299.999 s After The Restore And Nothing From 300 s On (A Room That Nobody Comes Back To Must Not Hold A Deploy For The Pause Cap); A Player Who Is Back Makes Its Room Count Again, As Itself (The Held Seats Of The Others Count Only Inside The Window); In A Live Room A Seat Whose Link Is Lost Is Held And Is No Person Who Is There; After A Restart That Cannot Bring Them Back (Another Network Protocol) They Count Nothing") {
        PWorld w("persist-102");
        w.restart.clock_ms = []() { return 0u; };                                              // (a clock that stands still: the restore takes no time by it, so the rooms begin at the very moment of the restart and the window's edge is exact)
        w.start_server(500);
        const auto busy_after = [&w](uint32_t ms) { return w.mgr->busy(w.server_now() + ms); };
        const auto busy = [&]() { return busy_after(0); };
        RoomSpec one = held_spec("BZ-1", 2);
        one.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Easy});
        ASSERT_TRUE(w.mgr->create_room(one, w.server_now()).ok);
        RClient& pat = w.connect("Pat", "BZ-1");
        std::vector<RClient*> two = play_room(w, held_spec("BZ-2", 2), 4000);
        ASSERT_TRUE(w.until([&]() { return w.status("BZ-1").state == RoomState::Running; }, 20000));
        ASSERT_TRUE(busy().matches == 2 && busy().players == 3);                              // Pat and two more people: the bot of BZ-1 is no person
        const uint8_t seat_back = two[0]->lobby->my_seat();
        const net::SeatKey key_back = two[0]->lobby->key();
        pat.reconnects = false;
        for (RClient* p : two) p->reconnects = false;
        {   // a live room: a seat whose link is lost is held (the room pauses, the seat may come back) and is no person who is there; the room counts while anybody is
            w.net.cut(two[1]->end);
            w.run(1500);
            ASSERT_TRUE(w.status("BZ-2").paused && w.status("BZ-2").absent.size() == 1);
            ASSERT_TRUE(busy().matches == 2 && busy().players == 2);                          // Pat, and the one person who is left in BZ-2
            w.net.cut(two[0]->end);
            w.run(1500);
            ASSERT_TRUE(w.status("BZ-2").paused && w.status("BZ-2").absent.size() == 2);
            ASSERT_TRUE(busy().matches == 1 && busy().players == 1);                          // BZ-2 has nobody in it now: a restart would interrupt Pat's match only (its record is kept anyway)
        }
        // ---- a restart: both rooms are back, paused, every seat of a person held and absent; they are matches that run, with their people ---------------------------------------------------------------
        w.stop_server(false);
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 2);
        for (const char* code : {"BZ-1", "BZ-2"}) {
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && !s.absent.empty());
        }
        ASSERT_EQ(busy().matches, 2u);
        ASSERT_EQ(busy().players, 3u);                                                         // (nobody is connected: the held seats are the people that a restart now would interrupt again)
        // ---- the window: five minutes from the restore, to the millisecond -------------------------------------------------------------------------------------------------------------------------
        ASSERT_EQ(kRestoredBusyWindowMs, 300000u);
        ASSERT_TRUE(busy_after(kRestoredBusyWindowMs - 1).matches == 2 && busy_after(kRestoredBusyWindowMs - 1).players == 3);
        ASSERT_TRUE(busy_after(kRestoredBusyWindowMs).matches == 0 && busy_after(kRestoredBusyWindowMs).players == 0);       // nobody came back: the rooms hold no deploy (the pause cap would hold it for 30 minutes)
        ASSERT_TRUE(busy_after(10u * 60u * 1000u).matches == 0 && busy_after(24u * 3600u * 1000u).players == 0);
        // ---- a player comes back to BZ-2 (a machine from nothing, with its key): its room counts again, as itself --------------------------------------------------------------------------------------
        RClient& back = w.connect("P0", "BZ-2", seat_back, key_back);
        bool saw_catching_up = false;
        for (int step = 0; step < 6000 && w.status("BZ-2").rejoins < 1; ++step) {
            w.run(10);
            for (const RoomStatus::Absent& a : w.status("BZ-2").absent) {
                if (!a.catching_up) continue;
                saw_catching_up = true;                                                        // a player who is being given the match is there: the room counts, beyond the window too
                ASSERT_TRUE(busy_after(kRestoredBusyWindowMs).matches == 1 && busy_after(kRestoredBusyWindowMs).players == 1);
            }
        }
        ASSERT_TRUE(saw_catching_up && w.status("BZ-2").rejoins == 1);
        ASSERT_TRUE(back.session != nullptr && !back.lost);
        ASSERT_TRUE(busy().matches == 2 && busy().players == 3);                               // still inside the window: BZ-2 has a person and a held seat, BZ-1 two held seats' worth of one person
        ASSERT_TRUE(busy_after(kRestoredBusyWindowMs).matches == 1 && busy_after(kRestoredBusyWindowMs).players == 1);      // beyond it: the person who is there, and no held seat
        ASSERT_TRUE(busy_after(24u * 3600u * 1000u).matches == 1 && busy_after(24u * 3600u * 1000u).players == 1);
        // ---- a restart that cannot bring them back (the network protocol is another one): they are failed rooms, and a failed room counts nothing ---------------------------------------------------
        w.stop_server(false);
        w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Ended) == 2);
        ASSERT_TRUE(w.status("BZ-1").state == RoomState::Failed && w.status("BZ-2").state == RoomState::Failed);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The review of the restart records: S3.103 and on
// ---------------------------------------------------------------------------------------------------------------------------------


void run_persist_review_tests() {
    TEST_CASE("S3.103 A Hostile Record Costs What Its Bytes Cost (M1 Of The Review): A Record May Hold 1,800,000 Turns (25 Hours Of Play) And Not One More, In Both Modes; The Limit Is Judged From The Header Of A Frame, Before Any Of Its Turns Is Decoded; A Record Read As Streaming Keeps No Turns (Its Count, Checkpoints, Head And Good Bytes Are The Whole Record's) And Gives Its Turns Again One At A Time; The Biggest Hostile File (48 MiB Of Valid Empty Turns, One To A Frame) Is Refused As Corrupt At The Start And Deleted; A Head Whose Keep Time Or Run Limit Is Beyond 24 Hours Is No Room That This Server Would Make") {
        {   // the limit: exactly kRestartMaxTurns empty turns are read (4096 to a frame), one more is corrupt; the same in both modes
            const std::vector<uint8_t> at_limit = empty_turns_record(kRestartMaxTurns, 4096);
            const RestartLoaded whole = parse_restart_record(at_limit.data(), at_limit.size());
            ASSERT_TRUE(whole.ok() && whole.turn_count == kRestartMaxTurns && whole.turns.size() == kRestartMaxTurns && !whole.torn && whole.bytes.empty());
            const RestartLoaded streamed = parse_restart_record(at_limit.data(), at_limit.size(), false);
            ASSERT_TRUE(streamed.ok() && streamed.turn_count == kRestartMaxTurns && streamed.turns.empty() && streamed.good_bytes == whole.good_bytes && streamed.file_bytes == whole.file_bytes);
            ASSERT_TRUE(same_head(streamed.head, whole.head));
            for (const bool keep : {true, false}) {
                const std::vector<uint8_t> over = empty_turns_record(kRestartMaxTurns + 1, 4096);
                const RestartLoaded r = parse_restart_record(over.data(), over.size(), keep);
                ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::Corrupt && r.why.find("more turns than a match can") != std::string::npos);
                ASSERT_TRUE(r.turns.empty() && r.turn_count == 0 && r.checks.empty() && r.good_bytes == 0);       // (a refused record hands out nothing)
            }
        }
        {   // the header decides: a frame that would take the record past the limit is refused for its count, whatever its body holds (here: nothing that decodes)
            std::vector<uint8_t> bytes = empty_turns_record(kRestartMaxTurns - 1, 4096);
            std::vector<uint8_t> payload(6, 0);
            put_le32(payload.data(), kRestartMaxTurns - 1);
            payload[4] = 2;                                                                                // two turns: one more than the limit allows; the body that would hold them is not there
            const std::vector<uint8_t> frame = test_frame(2, payload);
            bytes.insert(bytes.end(), frame.begin(), frame.end());
            const RestartLoaded r = parse_restart_record(bytes.data(), bytes.size(), false);
            ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::Corrupt && r.why.find("more turns than a match can") != std::string::npos);
            payload[4] = 1;                                                                                // ... and one turn that is within the limit but has no body is "cut short", the old message
            std::vector<uint8_t> within = empty_turns_record(kRestartMaxTurns - 1, 4096);
            const std::vector<uint8_t> frame_one = test_frame(2, payload);
            within.insert(within.end(), frame_one.begin(), frame_one.end());
            const RestartLoaded cut = parse_restart_record(within.data(), within.size(), false);
            ASSERT_TRUE(!cut.ok() && cut.why.find("cut short") != std::string::npos);
        }
        {   // a record read as Streaming keeps no turns and gives them again, one at a time, the same as the kept ones; an early stop stops
            RestartConfig cfg = test_restart_config("persist-103a");
            RestartStore store(cfg);
            std::string why;
            ASSERT_TRUE(store.prepare(why));
            auto writer = store.create(sample_head("S-1"), why);
            ASSERT_TRUE(writer != nullptr);
            for (uint32_t n = 0; n < 100; ++n) {
                ASSERT_TRUE(writer->append_turn(sample_turn(n)));
                if ((n + 1) % net::kHashEveryTurns == 0) ASSERT_TRUE(writer->append_check(n, 0x1000u + n));
            }
            ASSERT_TRUE(writer->sync());
            const std::string path = writer->path();
            writer.reset();
            {   // (a torn tail after the good frames: the walk stops at the last good byte)
                std::vector<uint8_t> bytes = read_all_bytes(path);
                const std::vector<uint8_t> next = test_frame(2, turns_payload(100, {sample_turn(100)}));
                bytes.insert(bytes.end(), next.begin(), next.begin() + 9);
                write_all_bytes(path, bytes);
            }
            const RestartLoaded kept = read_restart_record(path, cfg.max_record_bytes);
            const RestartLoaded streamed = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(kept.ok() && streamed.ok() && kept.torn && streamed.torn);
            ASSERT_TRUE(kept.turns.size() == 100 && kept.turn_count == 100 && kept.bytes.empty());
            ASSERT_TRUE(streamed.turns.empty() && streamed.turn_count == 100 && streamed.bytes.size() == streamed.file_bytes);
            ASSERT_TRUE(streamed.good_bytes == kept.good_bytes && streamed.checks.size() == 5 && same_checks(streamed.checks, kept.checks, 5) && same_head(streamed.head, kept.head) && streamed.path == path);
            std::vector<net::TurnMsg> a;
            std::vector<net::TurnMsg> b;
            ASSERT_TRUE(for_each_restart_turn(kept, [&](const net::TurnMsg& t) { a.push_back(t); return true; }));
            ASSERT_TRUE(for_each_restart_turn(streamed, [&](const net::TurnMsg& t) { b.push_back(t); return true; }));
            ASSERT_TRUE(a.size() == 100 && b.size() == 100 && same_turns(a, b, 100) && same_turns(b, kept.turns, 100));
            size_t seen = 0;
            ASSERT_FALSE(for_each_restart_turn(streamed, [&](const net::TurnMsg&) { return ++seen < 7; }));        // the callback said stop at the seventh turn
            ASSERT_EQ(seen, size_t{7});
            ASSERT_FALSE(for_each_restart_turn(kept, [&](const net::TurnMsg&) { return false; }));
            // a streamed record that is refused hands out no bytes
            std::vector<uint8_t> broken = read_all_bytes(path);
            broken[sizeof(kRestartMagic) + 7] ^= 0x20;                                                     // inside the head
            write_all_bytes(path, broken);
            const RestartLoaded bad = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(!bad.ok() && bad.bytes.empty() && bad.turns.empty() && bad.turn_count == 0);
        }
        {   // the biggest hostile file that a record may be (48 MiB: valid empty turns, one to a frame) is refused at the start: corrupt, named in the log, deleted, nothing left of it
            PWorld w("persist-103b");
            w.start_server(500);
            w.stop_server(false);
            const uint32_t turns = 2'900'000;                                                              // 17 bytes each: 47 MiB
            const std::vector<uint8_t> hostile = empty_turns_record(turns, 1);
            ASSERT_TRUE(hostile.size() < w.restart.max_record_bytes && hostile.size() > 40ull * 1024 * 1024);
            const fs::path planted = fs::path(w.restart.dir) / "room-HOSTILE-00000001.restart";
            write_all_bytes(planted, hostile);
            const auto began = std::chrono::steady_clock::now();
            w.start_server(500);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            ASSERT_EQ(w.report.items.size(), size_t{1});
            ASSERT_TRUE(w.report.items[0].outcome == RestoreItem::Outcome::Unreadable && w.report.items[0].note.find("corrupt") != std::string::npos && w.report.items[0].note.find("more turns than a match can") != std::string::npos);
            ASSERT_FALSE(fs::exists(planted));
            bool named = false;
            for (const std::string& n : w.notices) named = named || (n.find("room-HOSTILE-00000001.restart") != std::string::npos && n.find("more turns than a match can") != std::string::npos);
            ASSERT_TRUE(named);
            ASSERT_EQ(w.mgr->room_count(), size_t{0});
            ASSERT_TRUE(seconds < 20.0);                                                                   // (it is refused when the 1,800,001st turn is read: a second at the most; the bound is loose, a loaded machine)
            // the same file read as a file in both modes
            write_all_bytes(planted, hostile);
            for (const RestartRead mode : {RestartRead::Whole, RestartRead::Streaming}) {
                const RestartLoaded r = read_restart_record(planted.string(), w.restart.max_record_bytes, mode);
                ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::Corrupt && r.turns.empty() && r.bytes.empty());
            }
        }
        {   // a head whose keep time or run limit is beyond what the control interface allows (24 hours) is no room that this server would make, in create_room and in a restore
            ASSERT_EQ(kRestartMaxTurns, 1'800'000u);
            World w;
            RoomSpec ok_spec = spec_of("RG-1");
            ok_spec.keep_ms = 24u * 3600u * 1000u;
            ok_spec.run_ms = 24u * 3600u * 1000u;
            ASSERT_TRUE(w.mgr.create_room(ok_spec, w.now).ok);                                              // (24 h exactly is fine)
            RoomSpec long_keep = spec_of("RG-2");
            long_keep.keep_ms = 24u * 3600u * 1000u + 1;
            CreateResult r = w.mgr.create_room(long_keep, w.now);
            ASSERT_TRUE(!r.ok && r.http_status == 400 && r.error.find("keep_seconds") != std::string::npos);
            RoomSpec long_run = spec_of("RG-3");
            long_run.run_ms = 24u * 3600u * 1000u + 1;
            r = w.mgr.create_room(long_run, w.now);
            ASSERT_TRUE(!r.ok && r.http_status == 400 && r.error.find("max_run_seconds") != std::string::npos);
            long_run.run_ms = 0xFFFFFFFFu;
            ASSERT_EQ(w.mgr.create_room(long_run, w.now).http_status, 400);
            for (const bool keep_side : {true, false}) {
                PWorld p("persist-103c");
                p.start_server(500);
                crash_with_record_of(p, "RH-1", 6000);
                rewrite_head(p, "RH-1", [keep_side](RestartHead& h) { (keep_side ? h.keep_ms : h.run_ms) = 24u * 3600u * 1000u + 1; });
                p.start_server(500);
                ASSERT_TRUE(p.report.items.size() == 1 && p.report.items[0].outcome == RestoreItem::Outcome::Ended && p.report.items[0].note.find("would not make") != std::string::npos);
                ASSERT_TRUE(p.status("RH-1").state == RoomState::Failed && p.record_files().empty());
            }
        }
    } TEST_END();

    TEST_CASE("S3.104 One Server To A Folder (M5 Of The Review): The Folder Is Locked For The Life Of The Store That Prepared It (The File .lock, flock): A Second Store On The Same Folder, In This Process Or In Another, Cannot Prepare It And Says Another Server Has It; The Temporary File Of The First One's Record Is Not Removed By The Second (The Cleanup Comes After The Lock); The Same Store May Prepare Again; The Lock Goes When The Store Does (Or The Process Dies); A Second Room Manager Keeps No Records And Says Why") {
        RestartConfig cfg = test_restart_config("persist-104");
        std::string why;
        const fs::path tmp = fs::path(cfg.dir) / "room-LIVE-0000abcd.restart.0123456789abcdef.tmp";
        {
            RestartStore first(cfg);
            ASSERT_TRUE(first.prepare(why) && why.empty());
            ASSERT_TRUE(first.prepare(why));                                                    // (the same store again: it holds the lock already)
            ASSERT_TRUE(fs::exists(fs::path(cfg.dir) / ".lock"));
            ASSERT_TRUE(first.records().empty());                                               // (the lock file is no record)
            write_all_bytes(tmp, {1, 2, 3});                                                    // the first server is making a record: its temporary file
            RestartStore second(cfg);
            ASSERT_FALSE(second.prepare(why));
            ASSERT_TRUE(why.find("another server") != std::string::npos && why.find("lock") != std::string::npos);
            ASSERT_TRUE(fs::exists(tmp));                                                       // the refused store did not touch what is the first one's
            ASSERT_FALSE(second.prepare(why));                                                  // (and it stays refused)
            auto w1 = first.create(sample_head("LK-1"), why);                                    // the first one goes on as it did
            ASSERT_TRUE(w1 != nullptr && w1->append_turn(sample_turn(0)));
            ASSERT_TRUE(fs::exists(tmp));
            RoomManager keeper{MapStore(maps_dir())};                                           // a second room manager over the same folder keeps no records, and says why
            ASSERT_FALSE(keeper.enable_restart_records(cfg, why));
            ASSERT_TRUE(keeper.restart_store() == nullptr && why.find("another server") != std::string::npos);
            ASSERT_TRUE(keeper.create_room(held_spec("LK-2", 2), 1000).ok);
            RoomStatus s;
            ASSERT_TRUE(keeper.status("LK-2", s, 1000) && !s.record_kept && s.record_note == "this server keeps no restart records");
        }
        {   // the first store is gone: the folder is free, and the stale temporary file is cleaned up now that the lock is held
            RestartStore later(cfg);
            ASSERT_TRUE(later.prepare(why));
            ASSERT_FALSE(fs::exists(tmp));
            RestartStore blocked(cfg);
            ASSERT_FALSE(blocked.prepare(why));                                                 // ... and is taken again
        }
        {   // another folder is another lock; a folder with a lock file that nobody holds (a server that died) is free
            RestartConfig other = test_restart_config("persist-104b");
            RestartStore x(other);
            ASSERT_TRUE(x.prepare(why));
            RestartStore y(cfg);
            ASSERT_TRUE(y.prepare(why));
            ASSERT_TRUE(fs::exists(fs::path(cfg.dir) / ".lock") && fs::exists(fs::path(other.dir) / ".lock"));
        }
#ifndef _WIN32
        {   // a lock that another PROCESS holds: the same refusal, and the lock goes when that process dies (SIGKILL: no destructor runs)
            RestartConfig shared = test_restart_config("persist-104c");
            int up[2];
            ASSERT_EQ(::pipe(up), 0);
            const pid_t pid = ::fork();
            ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                ::close(up[0]);
                RestartStore holder(shared);
                std::string child_why;
                const char ok = holder.prepare(child_why) ? 'y' : 'n';
                (void)!::write(up[1], &ok, 1);
                ::pause();
                ::_exit(0);
            }
            ::close(up[1]);
            char told = 0;
            ASSERT_TRUE(::read(up[0], &told, 1) == 1 && told == 'y');
            RestartStore rival(shared);
            const bool refused = !rival.prepare(why) && why.find("another server") != std::string::npos;
            ::kill(pid, SIGKILL);
            int wait_status = 0;
            ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
            ::close(up[0]);
            ASSERT_TRUE(refused);
            ASSERT_TRUE(rival.prepare(why));                                                    // the process is dead: its lock is gone, the folder is free
        }
#endif
    } TEST_END();

    TEST_CASE("S3.112 A Record Is Opened Without A Race (L8 Of The Review): The File That Is Looked At Is The File That Is Read (One Open, Then The Descriptor Is Asked What It Is And How Big): A Link, A Folder And A Pipe Named Like A Record Are Refused As Not Regular Files (The Pipe Without Waiting On It: The Read Happens In A Child With An Alarm), A Missing Name Is Unreadable, A File Over The Limit Is Refused From The Size Of The Open File, An Empty File Is No Record, A Good One Reads In Both Modes") {
        RestartConfig cfg = test_restart_config("persist-112");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        auto w = store.create(sample_head("OP-1"), why);
        ASSERT_TRUE(w != nullptr);
        for (uint32_t n = 0; n < 30; ++n) ASSERT_TRUE(w->append_turn(sample_turn(n)));
        ASSERT_TRUE(w->sync());
        const std::string path = w->path();
        w.reset();
        const RestartLoaded good = read_restart_record(path, cfg.max_record_bytes);
        ASSERT_TRUE(good.ok() && good.turn_count == 30);
        const fs::path dir = cfg.dir;
        const auto read_at = [&](const char* name) { return read_restart_record((dir / name).string(), uint64_t{1} << 20); };
        // a folder, a name that is not there
        fs::create_directories(dir / "room-DIR-00000002.restart");
        const RestartLoaded folder = read_at("room-DIR-00000002.restart");
        ASSERT_TRUE(folder.status == RestartLoaded::Status::Unreadable);
        ASSERT_TRUE(read_at("room-NONE-00000003.restart").status == RestartLoaded::Status::Unreadable);
        // the limit comes from the size of the file that was opened, to the byte
        ASSERT_TRUE(read_restart_record(path, good.file_bytes - 1).status == RestartLoaded::Status::TooBig);
        ASSERT_TRUE(read_restart_record(path, good.file_bytes).ok());
        // an empty file is no record; a good one reads in both modes, and the bytes are those of the file
        write_all_bytes(dir / "room-EMPTY-00000005.restart", {});
        ASSERT_TRUE(read_at("room-EMPTY-00000005.restart").status == RestartLoaded::Status::NotARecord);
        const RestartLoaded streamed = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);
        ASSERT_TRUE(streamed.ok() && streamed.turn_count == 30 && streamed.bytes.size() == good.file_bytes && streamed.turns.empty());
#ifndef _WIN32
        ASSERT_TRUE(folder.why.find("not a regular file") != std::string::npos && folder.why.find("a folder") != std::string::npos);
        // a link to a good record is not followed (a followed link would read as that record)
        fs::create_symlink(path, dir / "room-LINK-00000001.restart");
        const RestartLoaded linked = read_at("room-LINK-00000001.restart");
        ASSERT_TRUE(linked.status == RestartLoaded::Status::Unreadable && linked.why.find("not a regular file") != std::string::npos && linked.why.find("a link") != std::string::npos);
        fs::create_symlink((dir / "room-NONE-00000003.restart").string(), dir / "room-DANGLING-00000006.restart");      // (and a link to nothing is the same refusal)
        ASSERT_TRUE(read_at("room-DANGLING-00000006.restart").why.find("a link") != std::string::npos);
        // a pipe that nobody writes to: opening it for reading would wait for ever; the child has five seconds (the alarm ends a hang)
        const std::string fifo = (dir / "room-FIFO-00000004.restart").string();
        ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        const pid_t pid = ::fork();
        ASSERT_TRUE(pid >= 0);
        if (pid == 0) {
            ::alarm(5);
            const RestartLoaded r = read_restart_record(fifo, uint64_t{1} << 20);
            ::_exit(r.status == RestartLoaded::Status::Unreadable && r.why.find("not a regular file") != std::string::npos ? 0 : 3);
        }
        int wait_status = 0;
        ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
        ASSERT_TRUE(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0);
#endif
    } TEST_END();
}



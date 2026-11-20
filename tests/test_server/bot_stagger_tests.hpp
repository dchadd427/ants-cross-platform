// The tests of the staggered start (BotWhat::every_s, BotMatches' waiting starts, the routes bot-matches and bot-matches/<id>/stop; docs/SERVER_REPLAYS.md "Start one every X minutes"): a batch with a spacing starts its first match now
// and keeps the others as starts that wait, each for its time by the server's clock (the loop's pass, never a sleep); a waiting start is listed, can be stopped, and Stop all ends the batch; a start whose time comes while a limit
// holds it back waits, it is not dropped, and the matches of people come first; what a match is dealt is drawn when it starts; a restart drops what waits; the bounds; and the spacing of a schedule (its routes, its runs under the eight and people's share). Included by test_server_files.cpp after
// bot_rotation_tests.hpp, whose fixtures it uses; test_server.cpp calls run_bot_stagger_tests().
#pragma once

namespace {

// The body of a batch with a spacing, as the page sends it
std::string spaced_body(int count, uint32_t every_s, const std::string& map = "TINY.LVL", const std::string& rules = "highest-score", const std::string& bots = "2") {
    std::string body = batch_body(count, map, rules, bots);
    body.insert(body.find(",\"map\""), ",\"every_s\":" + std::to_string(every_s));
    return body;
}

// The clock of the door goes on by `ms`, and the loop makes its pass (the door's wall clock goes on by the whole seconds)
void stagger_step(BotDoor& d, uint32_t ms) {
    d.w.now += ms;
    d.wall += ms / 1000;
    d.bots->update(d.wall, d.w.now);
}

ctl::JsonValue waiting_of(BotDoor& d) { return BotDoor::json(d.get("bot-matches")).get("waiting"); }

// The body of a schedule as the page sends it: every 30 minutes, with the batch given
std::string schedule_body(const std::string& name, const std::string& what) { return "{\"name\":\"" + name + "\",\"on\":true,\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + what + "}"; }

// A schedule that runs every `minutes` and starts `count` matches of TINY with two bots, one every `every_s` seconds
BotSchedulePatch spaced_schedule(const std::string& name, int minutes, uint32_t count, uint32_t every_s) {
    BotSchedulePatch p;
    p.has_name = true;
    p.name = name;
    p.has_when = true;
    p.kind = BotSchedule::Kind::Every;
    p.minutes = minutes;
    p.has_what = true;
    p.what.count = count;
    p.what.every_s = every_s;
    p.what.spec.map = "TINY.LVL";
    p.what.spec.bots = 2;
    return p;
}

std::vector<int64_t> numbers_of(const ctl::JsonValue& list, const char* field) {
    std::vector<int64_t> out;
    for (size_t i = 0; i < list.size(); ++i) out.push_back(list.at(i).get(field).as_int_or(-1));
    return out;
}

}  // namespace

void run_bot_stagger_tests() {
    TEST_CASE("S3.292 A Batch With A Spacing Starts Its First Match Now And Keeps The Others As Starts That Wait: One Every X Seconds By The Server's Clock, Each Listed With Its Number And Its Time, Each Started By The Loop When Its Time Comes And Not Before") {
        BotDoor d("botstagger-basic");
        const ctl::HttpResponse made = d.post("bot-matches", spaced_body(5, 180));
        ASSERT_EQ(made.status, 200);
        const ctl::JsonValue a = BotDoor::json(made);
        ASSERT_TRUE(a.get("started").as_int_or(0) == 1 && a.get("asked").as_int_or(0) == 5 && a.get("queued").as_int_or(0) == 4 && a.get("every_s").as_int_or(0) == 180 && !a.has("stopped_for"));
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 4);
        const ctl::JsonValue list = BotDoor::json(d.get("bot-matches"));
        ASSERT_TRUE(list.get("matches").size() == 1 && list.get("waiting").size() == 4 && list.get("waiting_max").as_int_or(0) == static_cast<int64_t>(kBotWaitingMax));
        ASSERT_EQ(numbers_of(list.get("waiting"), "n"), (std::vector<int64_t>{2, 3, 4, 5}));
        ASSERT_EQ(numbers_of(list.get("waiting"), "of"), (std::vector<int64_t>{5, 5, 5, 5}));
        ASSERT_EQ(numbers_of(list.get("waiting"), "at"), (std::vector<int64_t>{d.wall + 180, d.wall + 360, d.wall + 540, d.wall + 720}));          // (when each starts, in the seconds of the wall clock: the page says "in 3 min")
        for (size_t i = 0; i < 4; ++i) {
            const ctl::JsonValue& one = list.get("waiting").at(i);
            ASSERT_TRUE(one.get("id").str().size() > 1 && one.get("id").str()[0] == 'w' && one.get("name").str() == "TINY" && one.get("by").str() == "dave" && !one.has("schedule") && !one.get("held").as_bool_or(true));
            ASSERT_TRUE(one.get("what").get("map").str() == "TINY.LVL" && one.get("what").get("count").as_int_or(0) == 1 && one.get("what").get("every_s").as_int_or(-1) == 0);          // (one match of the batch: the others are listed on their own)
        }
        std::set<std::string> ids;
        for (size_t i = 0; i < 4; ++i) ids.insert(list.get("waiting").at(i).get("id").str());
        ASSERT_EQ(ids.size(), size_t{4});
        const std::vector<AuditEntry> log = d.log();
        ASSERT_TRUE(log.size() == 1 && log[0].action == "bot_started" && log[0].target == "5 matches" && log[0].detail.find("one every 3 minutes") != std::string::npos);          // (one line for the batch: the whole of it, now and later)
        stagger_step(d, 179000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 4);                  // (a second before its time: nothing)
        stagger_step(d, 1000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 3);
        stagger_step(d, 100);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 3);                  // (and not again at the next pass)
        stagger_step(d, 178900);
        stagger_step(d, 900);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 3);                  // (the third is 100 ms away)
        stagger_step(d, 100);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 3 && d.bots->waiting_count() == 2);                  // (the third at 360 s from the first: the spacing is the batch's, not that of each pass)
        stagger_step(d, 180000);
        stagger_step(d, 180000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 5 && d.bots->waiting_count() == 0);
        const ctl::JsonValue now = BotDoor::json(d.get("bot-matches"));
        ASSERT_TRUE(now.get("matches").size() == 5 && now.get("waiting").size() == 0);
        for (size_t i = 0; i < 5; ++i) ASSERT_TRUE(now.get("matches").at(i).get("by").str() == "dave" && !now.get("matches").at(i).has("schedule"));          // (the maker is the admin who pressed Start now, for the ones that came later too)
        stagger_step(d, 180000);
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{5});                                               // (the batch is over: nothing more comes)
        ASSERT_EQ(d.log().size(), size_t{1});                                                          // (the later starts are not logged one by one)
        std::string note;
        for (const std::string& line : d.bots->take_notices()) note += line + "|";
        ASSERT_TRUE(note.find("dropped") == std::string::npos);
    } TEST_END();

    TEST_CASE("S3.293 The Spacing Is Bounded: 0 (All At Once, As Always) To Two Hours In Steps Of Half A Minute, A Spacing Of A Single Match Is Nothing, At Most 32 Starts Wait, And A Batch That Cannot Start Its First Match Makes No Waiting Start") {
        BotDoor d("botstagger-bounds");
        ASSERT_EQ(d.post("bot-matches", spaced_body(3, 0)).status, 200);                              // 0: the three at once, nothing waits
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 3 && d.bots->waiting_count() == 0 && !BotDoor::json(d.post("bot-matches", spaced_body(1, 600, "TINY.LVL", "highest-score", "2"))).has("queued"));          // (one match: no spacing to keep)
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 4 && d.bots->waiting_count() == 0);
        for (const uint32_t bad : {1u, 15u, 29u, 45u, 7230u, 7260u, 99999u}) {
            const ctl::HttpResponse r = d.post("bot-matches", spaced_body(2, bad));
            ASSERT_TRUE(r.status == 400 && BotDoor::json(r).get("error").str() == "bad_spacing");
        }
        const ctl::HttpResponse words = d.post("bot-matches", "{\"count\":2,\"every_s\":\"180\",\"map\":\"TINY.LVL\",\"rules\":\"highest-score\",\"bots\":\"2\",\"levels\":[\"rmh\",\"rmh\",\"rmh\",\"rmh\"],\"teams\":\"random\",\"split\":\"\"}");
        ASSERT_TRUE(words.status == 400 && BotDoor::json(words).get("error").str() == "bad_spacing");  // (a number in seconds, not words)
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 4 && d.bots->waiting_count() == 0 && d.log().size() == 2);                  // (no refusal started anything or made a line)
        d.post("bot-matches/stop-all");
        const ctl::HttpResponse half = d.post("bot-matches", spaced_body(2, 30));                     // (half a minute, the smallest step, and two hours, the longest)
        ASSERT_TRUE(half.status == 200 && BotDoor::json(half).get("queued").as_int_or(0) == 1);
        ASSERT_EQ(numbers_of(waiting_of(d), "at"), (std::vector<int64_t>{d.wall + 30}));
        const ctl::HttpResponse longest = d.post("bot-matches", spaced_body(3, 7200));
        ASSERT_TRUE(longest.status == 200 && BotDoor::json(longest).get("queued").as_int_or(0) == 2);
        ASSERT_EQ(numbers_of(waiting_of(d), "at"), (std::vector<int64_t>{d.wall + 30, d.wall + 7200, d.wall + 14400}));          // (listed in the order of their time, whatever batch they are of)
        d.post("bot-matches/stop-all");
        // at most 32 starts wait: a batch that would pass it is refused whole, and one that fits is taken
        for (int i = 0; i < 4; ++i) ASSERT_EQ(d.post("bot-matches", spaced_body(8, 600)).status, 200);          // (four batches of eight: four matches and 28 waiting starts)
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 4 && d.bots->waiting_count() == 28);
        const ctl::HttpResponse over = d.post("bot-matches", spaced_body(8, 600));
        const ctl::JsonValue refused = BotDoor::json(over);
        ASSERT_TRUE(over.status == 409 && refused.get("error").str() == "queue_full" && refused.get("waiting").as_int_or(0) == 28 && refused.get("asked").as_int_or(0) == 7 && refused.get("max").as_int_or(0) == 32);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 4 && d.bots->waiting_count() == 28);                  // (nothing was started: not the first match either)
        ASSERT_EQ(d.post("bot-matches", spaced_body(5, 600)).status, 200);                             // (four more fit: 32)
        ASSERT_TRUE(d.bots->waiting_count() == 32 && d.w.mgr.bot_match_count() == 5);
        ASSERT_EQ(d.post("bot-matches", spaced_body(2, 600)).status, 409);
        ASSERT_EQ(d.post("bot-matches", batch_body(3, "TINY.LVL", "highest-score", "2")).status, 200); // (a batch that starts at once needs no waiting place)
        d.post("bot-matches/stop-all");
        // the first match cannot start (the eight are used): no match is made and nothing waits
        ASSERT_EQ(d.post("bot-matches", batch_body(8, "TINY.LVL", "highest-score", "2")).status, 200);
        const ctl::HttpResponse full = d.post("bot-matches", spaced_body(3, 600));
        ASSERT_TRUE(full.status == 409 && BotDoor::json(full).get("error").str() == "cap" && d.bots->waiting_count() == 0 && d.w.mgr.bot_match_count() == 8);
        d.post("bot-matches/stop-all");
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->waiting_count() == 0);
        const ctl::HttpResponse nomap = d.post("bot-matches", spaced_body(3, 60, "Nowhere.lvl"));
        ASSERT_TRUE(nomap.status == 409 && BotDoor::json(nomap).get("error").str() == "no_map");
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->waiting_count() == 0);                  // (a batch whose first match is refused leaves no waiting start behind)
        const ctl::HttpResponse islands = d.post("bot-matches", spaced_body(3, 60, "ISLANDS.LVL", "187"));
        ASSERT_TRUE(islands.status == 409 && BotDoor::json(islands).get("error").str() == "map_187" && d.bots->waiting_count() == 0);
    } TEST_END();

    TEST_CASE("S3.294 A Waiting Start Can Be Stopped On Its Own And Stop All Ends The Batch: The Others Of The Batch Keep Their Times, A Stopped One Is Gone And Is Not Started, Both Are In The Log, And A Name That Is Not A Waiting Start Is Not Found") {
        BotDoor d("botstagger-stop");
        ASSERT_EQ(d.post("bot-matches", spaced_body(4, 120)).status, 200);
        const ctl::JsonValue before = waiting_of(d);
        ASSERT_EQ(before.size(), size_t{3});
        const std::string second = before.at(1).get("id").str();
        ctl::HttpResponse r = d.post("bot-matches/" + second + "/stop");
        ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("stopped").as_int_or(0) == 1);
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});                                              // (it stopped a start, no running match)
        const ctl::JsonValue after = waiting_of(d);
        ASSERT_EQ(numbers_of(after, "n"), (std::vector<int64_t>{2, 4}));                              // (the third is gone, the second and the fourth are as they were)
        ASSERT_EQ(numbers_of(after, "at"), (std::vector<int64_t>{d.wall + 120, d.wall + 360}));
        std::vector<AuditEntry> log = d.log();
        ASSERT_TRUE(log[0].action == "bot_waiting_stopped" && log[0].target == "TINY" && log[0].actor == "dave" && log[0].detail.empty());
        r = d.post("bot-matches/" + second + "/stop");
        ASSERT_TRUE(r.status == 404 && BotDoor::json(r).get("error").str() == "gone");                // (not there any more)
        ASSERT_EQ(d.log().size(), size_t{2});
        stagger_step(d, 120000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 1);
        stagger_step(d, 240000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 3 && d.bots->waiting_count() == 0);                  // (the stopped start never came)
        // a name that is no start (the room's own code, a match that goes, a name that does not exist) is not found
        ASSERT_EQ(d.post("bot-matches/w999/stop").status, 404);
        // Stop all ends the matches and the batch together, and says how many of each
        d.post("bot-matches/stop-all");
        ASSERT_EQ(d.post("bot-matches", spaced_body(5, 60)).status, 200);
        ASSERT_EQ(d.bots->waiting_count(), size_t{4});
        r = d.post("bot-matches/stop-all");
        const ctl::JsonValue ended = BotDoor::json(r);
        ASSERT_TRUE(r.status == 200 && ended.get("stopped").as_int_or(-1) == 1 && ended.get("dropped").as_int_or(-1) == 4);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->waiting_count() == 0);
        log = d.log();
        ASSERT_TRUE(log[0].action == "bot_stopped_all" && log[0].target == "1 match" && log[0].detail == "and 4 waiting starts");
        stagger_step(d, 600000);
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});                                              // (nothing comes after the end of the batch)
        // starts that wait and no match that goes: Stop all still ends them (and a stop of nothing is not logged)
        ASSERT_EQ(d.post("bot-matches", spaced_body(3, 60)).status, 200);
        d.post("bot-matches/" + waiting_of(d).at(0).get("id").str() + "/stop");
        d.post("bot-matches/" + waiting_of(d).at(0).get("id").str() + "/stop");
        const size_t lines = d.log().size();
        d.post("bot-matches/stop-all");
        r = d.post("bot-matches/stop-all");
        ASSERT_TRUE(BotDoor::json(r).get("stopped").as_int_or(-1) == 0 && BotDoor::json(r).get("dropped").as_int_or(-1) == 0 && d.log().size() == lines + 1);
        // the stop of a running match is as before, by its own name
        ASSERT_EQ(d.post("bot-matches", batch_body(1, "TINY.LVL", "highest-score", "2")).status, 200);
        const std::string running = BotDoor::json(d.get("bot-matches")).get("matches").at(0).get("id").str();
        ASSERT_TRUE(running[0] == 'm' && d.post("bot-matches/" + running + "/stop").status == 200 && d.w.mgr.bot_match_count() == 0);
    } TEST_END();

    TEST_CASE("S3.295 A Start Whose Time Comes While A Limit Holds It Back Waits And Is Not Dropped: The Eight Matches, People's Matches (They Come First) And The Places Hold It, It Starts At The First Pass After The Limit Lets Go, And The Starts That Follow Keep Their Spacing From It") {
        {   // the eight places
            BotDoor d("botstagger-cap");
            ASSERT_EQ(d.post("bot-matches", spaced_body(3, 60)).status, 200);                         // (one now, two wait)
            ASSERT_EQ(d.post("bot-matches", batch_body(7, "TINY.LVL", "highest-score", "2")).status, 200);          // (the other seven fill the eight)
            ASSERT_EQ(d.w.mgr.bot_match_count(), kBotMatchesMax);
            stagger_step(d, 60000);                                                                 // (the second one's time: no place)
            ASSERT_TRUE(d.w.mgr.bot_match_count() == kBotMatchesMax && d.bots->waiting_count() == 2);
            ctl::JsonValue list = waiting_of(d);
            ASSERT_TRUE(list.at(0).get("held").as_bool_or(false) && !list.at(1).get("held").as_bool_or(true));          // (the first one waits for a place; the next one's time has not come)
            ASSERT_TRUE(list.at(0).get("at").as_int_or(0) <= d.wall);                                 // (its time is now: the page says that it waits for a place)
            for (int i = 0; i < 5; ++i) stagger_step(d, 1000);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == kBotMatchesMax && d.bots->waiting_count() == 2);          // (still there after five seconds: it is not dropped)
            ASSERT_TRUE(d.bots->take_notices().empty());                                              // (and nothing is said in the log: it is waiting, not failing)
            const std::string first = BotDoor::json(d.get("bot-matches")).get("matches").at(0).get("id").str();
            ASSERT_EQ(d.post("bot-matches/" + first + "/stop").status, 200);                          // (a place is free)
            stagger_step(d, 1000);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == kBotMatchesMax && d.bots->waiting_count() == 1);          // (it took the place at the first pass after that)
            const int64_t late = waiting_of(d).at(0).get("at").as_int_or(0);
            ASSERT_TRUE(late >= d.wall + 58 && late <= d.wall + 60);                                  // (the one after it comes a spacing after the late one started, not at its first time)
            stagger_step(d, 59000);
            ASSERT_EQ(d.bots->waiting_count(), size_t{1});
            ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);
            ASSERT_EQ(d.bots->waiting_count(), size_t{0});
        }
        {   // the matches of people come first
            ServerLimits limits;
            limits.demo_rooms = 8;
            BotDoor d("botstagger-people", limits);
            ASSERT_EQ(d.post("bot-matches", spaced_body(3, 60)).status, 200);
            std::vector<std::string> people;
            for (int i = 0; i < 7; ++i) {
                RoomSpec p = spec_of("stg-" + std::to_string(i), 2);
                p.public_room = true;
                p.keep_ms = 500;                                                                      // (a room that is closed is answered for this long, and holds its place until then)
                const CreateResult made = d.w.mgr.create_room(p, d.w.now);
                ASSERT_TRUE(made.ok);
                people.push_back(made.code);
            }
            ASSERT_TRUE(d.w.mgr.people_busy());
            stagger_step(d, 60000);
            for (int i = 0; i < 3; ++i) stagger_step(d, 1000);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2);              // (people's matches use most of the places: it waits)
            ASSERT_TRUE(waiting_of(d).at(0).get("held").as_bool_or(false));
            ASSERT_TRUE(d.bots->take_notices().empty());
            ASSERT_TRUE(d.w.mgr.close_room(people[0], d.w.now) && d.w.mgr.close_room(people[1], d.w.now));
            d.w.run(1000);                                                                         // (a closed room is answered for its keep time and then leaves the manager)
            ASSERT_FALSE(d.w.mgr.people_busy());
            stagger_step(d, 1000);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 1);              // (people left: it started)
        }
        {   // a batch is refused at once while people use the places: nothing waits
            ServerLimits limits;
            limits.demo_rooms = 8;
            BotDoor d("botstagger-busy", limits);
            for (int i = 0; i < 7; ++i) {
                RoomSpec p = spec_of("stg-busy-" + std::to_string(i), 2);
                p.public_room = true;
                ASSERT_TRUE(d.w.mgr.create_room(p, d.w.now).ok);
            }
            const ctl::HttpResponse busy = d.post("bot-matches", spaced_body(3, 60));
            ASSERT_TRUE(busy.status == 409 && BotDoor::json(busy).get("error").str() == "busy" && d.bots->waiting_count() == 0 && d.w.mgr.bot_match_count() == 0);
        }
        {   // the places of the bot matches: a map of 3 places, so the sixteen are used up by five of them
            BotDoor d("botstagger-places");
            BotMatchSpec fight = fight_island_spec();
            std::string code, why;
            for (int i = 0; i < 4; ++i) ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, fight, code, why));            // (four of the three places: 12 of 16)
            ASSERT_EQ(d.post("bot-matches", spaced_body(3, 60, "Fight Island.lvl", "187", "2")).status, 200);            // (the fifth one fits: 15 of 16; the others wait)
            stagger_step(d, 60000);
            for (int i = 0; i < 3; ++i) stagger_step(d, 1000);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 5 && d.bots->waiting_count() == 2 && waiting_of(d).at(0).get("held").as_bool_or(false));          // (the places of the bot matches hold it)
            d.post("bot-matches/stop-all");
        }
    } TEST_END();

    TEST_CASE("S3.296 What A Waiting Start Is Dealt Is Drawn When It Starts, Not When The Batch Was Made: A List Of The Rotation That Changes Meanwhile Is The List Of The Later Match; A Waiting Start Is Not Kept Across A Restart; Starts Go On While The Schedules Are Switched Off") {
        {   // drawn at its time
            BotDoor d("botstagger-draw");
            ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL"})).status, 200);
            const std::string body = [] { std::string b = batch_body(3, "random:rotation", "highest-score", "2"); b.insert(b.find(",\"map\""), ",\"every_s\":60"); return b; }();
            ASSERT_EQ(d.post("bot-matches", body).status, 200);
            ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"SMALL.LVL"})).status, 200);          // (the admin changes the list while the batch waits)
            stagger_step(d, 60000);
            stagger_step(d, 60000);
            std::vector<std::string> maps;
            for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) maps.push_back(m.map);
            ASSERT_EQ(maps, (std::vector<std::string>{"TINY.LVL", "SMALL.LVL", "SMALL.LVL"}));            // (the first was drawn at once, the two that waited from the new list)
        }
        {   // a restart drops what waits: a new BotMatches (a new process) over the same schedules has no waiting start
            BotDoor d("botstagger-restart");
            ASSERT_EQ(d.post("bot-matches", spaced_body(4, 60)).status, 200);
            ASSERT_EQ(d.bots->waiting_count(), size_t{3});
            BotRotationStore other_rotation(&d.database);
            std::string why;
            ASSERT_TRUE(other_rotation.open(why));
            BotMatches after(d.w.mgr, *d.schedules, other_rotation, d.audit.get());
            ASSERT_EQ(after.waiting_count(), size_t{0});
            after.update(d.wall + 600, d.w.now + 600000);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});                                          // (the one that was going; nothing of the batch comes from a new process)
        }
        {   // the loop starts waiting starts while the schedules are off (the operator's switch is for the schedules)
            BotDoor d("botstagger-off");
            d.bots->set_scheduling(false);
            ASSERT_EQ(d.post("bot-matches", spaced_body(2, 60)).status, 200);
            stagger_step(d, 60000);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 0);
        }
    } TEST_END();

    TEST_CASE("S3.297 A Schedule Can Carry A Spacing: Its Run Starts The First Match And Lets The Others Wait, The Line In The Log Says So, And The Spacing Is Kept Through The Database And The Page's Words") {
        BotDoor d("botstagger-schedule");
        std::string why;
        BotSchedulePatch patch;
        patch.has_name = true;
        patch.name = "Swarm";
        patch.has_when = true;
        patch.kind = BotSchedule::Kind::Every;
        patch.minutes = 5;
        patch.has_what = true;
        patch.what.count = 3;
        patch.what.every_s = 90;
        patch.what.spec.map = "TINY.LVL";
        patch.what.spec.bots = 2;
        BotSchedule s;
        ASSERT_TRUE(d.schedules->create(patch, kBotWall0, s, why));
        ASSERT_EQ(d.schedules->find(s.id)->what.every_s, 90u);
        d.bots->update(kBotWall0 + 300, d.w.now);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2);
        const std::vector<std::string> notes = d.bots->take_notices();
        ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 1 of 3, 2 waiting for their time");
        const ctl::JsonValue list = waiting_of(d);
        ASSERT_TRUE(list.at(0).get("schedule").str() == "Swarm" && !list.at(0).has("by") && list.at(0).get("what").get("every_s").as_int_or(-1) == 0);          // (named by the schedule, as the matches that it starts)
        const ctl::JsonValue shown = BotDoor::json(d.get("bot-schedules"));
        ASSERT_EQ(shown.get("schedules").at(0).get("what").get("every_s").as_int_or(-1), 90);
        d.w.now += 90000;
        d.bots->update(kBotWall0 + 390, d.w.now);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 1);
    } TEST_END();

    TEST_CASE("S3.298 The Spacing Of A Schedule Goes Through The Admin Routes As Start Now's Does: Whole Or Half Minutes From 0 To Two Hours Only (Checked On A New Schedule And On A Change), Kept In The Schedule And Listed, A Change Of The Spacing Alone Is A Change That Is Logged, And The Same Save Again Is Not") {
        BotDoor d("botsched-routes");
        const std::string flat = batch_body(4, "TINY.LVL", "highest-score", "2");
        for (const char* bad : {"1", "15", "29", "45", "7230", "7260", "99999", "-30", "\"180\"", "1.5", "null", "[]"}) {
            std::string what = flat;
            what.insert(what.find(",\"map\""), std::string(",\"every_s\":") + bad);
            const ctl::HttpResponse r = d.post("bot-schedules", schedule_body("Swarm", what));
            ASSERT_TRUE(r.status == 400 && BotDoor::json(r).get("error").str() == "bad_spacing");
        }
        ASSERT_TRUE(d.schedules->list().empty() && d.log().empty());                                  // (nothing was made, nothing was logged)
        for (const uint32_t ok : {0u, 30u, 90u, 7200u}) {
            const ctl::HttpResponse r = d.post("bot-schedules", schedule_body("Swarm " + std::to_string(ok), spaced_body(4, ok)));
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(BotDoor::json(r).get("schedule").get("what").get("every_s").as_int_or(-1), static_cast<int64_t>(ok));
        }
        ASSERT_EQ(d.schedules->list().size(), size_t{4});
        const ctl::JsonValue listed = BotDoor::json(d.get("bot-schedules")).get("schedules");
        ASSERT_EQ(numbers_of([&] { ctl::JsonValue w = ctl::JsonValue::make_array(); for (size_t i = 0; i < listed.size(); ++i) w.push_back(listed.at(i).get("what")); return w; }(), "every_s"), (std::vector<int64_t>{0, 30, 90, 7200}));
        std::vector<AuditEntry> log = d.log();
        ASSERT_TRUE(log[0].action == "bot_schedule_added" && log[0].detail.find("one every 120 minutes") != std::string::npos && log[1].detail.find("one every 1.5 minutes") != std::string::npos);
        ASSERT_TRUE(log[3].detail.find("one every") == std::string::npos);                          // (a spacing of 0 says nothing of one)
        // a change of the spacing alone
        const int64_t id = listed.at(3).get("id").as_int_or(0);
        const size_t lines = d.log().size();
        ASSERT_EQ(d.post("bot-schedules/" + std::to_string(id), "{\"what\":" + spaced_body(4, 240) + "}").status, 200);
        ASSERT_EQ(d.schedules->find(id)->what.every_s, 240u);
        log = d.log();
        ASSERT_TRUE(log.size() == lines + 1 && log[0].action == "bot_schedule_changed" && log[0].target == "Swarm 7200" && log[0].detail.find("one every 4 minutes") != std::string::npos);
        ASSERT_EQ(d.post("bot-schedules/" + std::to_string(id), "{\"what\":" + spaced_body(4, 240) + "}").status, 200);
        ASSERT_EQ(d.log().size(), lines + 1);                                                          // (the same again changes nothing: no line)
        std::string over = flat;
        over.insert(over.find(",\"map\""), ",\"every_s\":7230");
        const ctl::HttpResponse refused = d.post("bot-schedules/" + std::to_string(id), "{\"what\":" + over + "}");
        ASSERT_TRUE(refused.status == 400 && BotDoor::json(refused).get("error").str() == "bad_spacing" && d.schedules->find(id)->what.every_s == 240u);          // (a refused change leaves the spacing as it was)
        ASSERT_EQ(d.log().size(), lines + 1);
    } TEST_END();

    TEST_CASE("S3.299 A Run Of A Schedule With A Spacing Keeps The Limits: Eight Matches Hold The Run Back (It Is Skipped, Nothing Waits), With Seven Going It Starts The First And Lets The Four Others Wait, A Held Start Waits For A Place And For People And Is Not Dropped, People Come First, And A Deleted Schedule's Waiting Starts Go On And Can Be Stopped") {
        {   // the eight matches
            BotDoor d("botsched-cap");
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(spaced_schedule("Swarm", 5, 5, 60), kBotWall0, s, why));
            ASSERT_EQ(d.post("bot-matches", batch_body(8, "TINY.LVL", "highest-score", "2")).status, 200);
            (void)d.bots->take_notices();
            d.bots->update(kBotWall0 + 300, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 8 && d.bots->waiting_count() == 0);              // (the first match has no place: the run is skipped, nothing waits)
            std::vector<std::string> notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 0 of 5 (8 matches of computer players go already)");
            ASSERT_EQ(d.schedules->find(s.id)->next_run, kBotWall0 + 600);
            d.post("bot-matches/stop-all");
            ASSERT_EQ(d.post("bot-matches", batch_body(7, "TINY.LVL", "highest-score", "2")).status, 200);
            d.bots->update(kBotWall0 + 600, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 8 && d.bots->waiting_count() == 4);              // (the first one takes the eighth place; the others wait for their time)
            notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 1 of 5, 4 waiting for their time");
            ctl::JsonValue list = waiting_of(d);
            ASSERT_TRUE(list.size() == 4 && list.at(0).get("schedule").str() == "Swarm" && !list.at(0).has("by") && list.at(0).get("n").as_int_or(0) == 2 && list.at(3).get("of").as_int_or(0) == 5);
            d.w.now += 60000;
            d.bots->update(kBotWall0 + 660, d.w.now);                                                  // (the time of the second: the eight are used)
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 8 && d.bots->waiting_count() == 4 && waiting_of(d).at(0).get("held").as_bool_or(false));
            ASSERT_TRUE(d.bots->take_notices().empty());                                              // (waiting is not failing)
            d.post("bot-matches/" + BotDoor::json(d.get("bot-matches")).get("matches").at(0).get("id").str() + "/stop");
            d.w.now += 1000;
            d.bots->update(kBotWall0 + 661, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 8 && d.bots->waiting_count() == 3);              // (it took the place that came free)
        }
        {   // people first: a run is skipped while they use the places, and a start that waits is held
            ServerLimits limits;
            limits.demo_rooms = 8;
            BotDoor d("botsched-people", limits);
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(spaced_schedule("Swarm", 5, 3, 60), kBotWall0, s, why));
            for (int i = 0; i < 7; ++i) {
                RoomSpec p = spec_of("sch-" + std::to_string(i), 2);
                p.public_room = true;
                ASSERT_TRUE(d.w.mgr.create_room(p, d.w.now).ok);
            }
            ASSERT_TRUE(d.w.mgr.people_busy());
            (void)d.bots->take_notices();
            d.bots->update(kBotWall0 + 300, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->waiting_count() == 0);
            const std::vector<std::string> notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 0 of 3 (people's matches use 7 of the server's 8 places)");
        }
        {   // a start that waits is held by people, and a deleted schedule's waiting starts go on
            ServerLimits limits;
            limits.demo_rooms = 8;
            BotDoor d("botsched-held", limits);
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(spaced_schedule("Swarm", 5, 3, 60), kBotWall0, s, why));
            d.bots->update(kBotWall0 + 300, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2);
            for (int i = 0; i < 7; ++i) {
                RoomSpec p = spec_of("sch-held-" + std::to_string(i), 2);
                p.public_room = true;
                ASSERT_TRUE(d.w.mgr.create_room(p, d.w.now).ok);
            }
            (void)d.bots->take_notices();
            d.w.now += 60000;
            d.bots->update(kBotWall0 + 360, d.w.now);
            d.w.now += 3000;
            d.bots->update(kBotWall0 + 363, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2 && waiting_of(d).at(0).get("held").as_bool_or(false));          // (people use the places: it waits, it is not dropped)
            ASSERT_TRUE(d.bots->take_notices().empty());
            ASSERT_EQ(d.post("bot-schedules/" + std::to_string(s.id) + "/delete").status, 200);
            ASSERT_TRUE(d.schedules->list().empty() && d.bots->waiting_count() == 2);                  // (the schedule is gone, what it let wait is not)
            const ctl::JsonValue after = waiting_of(d);
            ASSERT_TRUE(after.size() == 2 && after.at(0).get("schedule").str() == "Swarm");           // (still named by it)
            ASSERT_EQ(d.post("bot-matches/" + after.at(1).get("id").str() + "/stop").status, 200);
            ASSERT_EQ(d.bots->waiting_count(), size_t{1});
        }
    } TEST_END();
}

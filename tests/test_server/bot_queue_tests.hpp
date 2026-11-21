// The tests of the queue of matches of computer players (docs/SERVER_REPLAYS.md, "Matches of computer players", "The limits" and "Start one every X minutes"): a batch over what the limits allow starts what fits and keeps the rest
// as ONE queue that the loop's passes work off (two starts a pass, never a refusal for a limit); the counters of a queue (done, running, kept as replays); the summary that the chat's /bots status asks; the starts that can never be
// made end their queue; a schedule's batch is still 1 to 8; and the loop's meter (loop_load.hpp). Included by test_server_files.cpp after bot_stagger_tests.hpp, whose helpers it uses; test_server.cpp calls run_bot_queue_tests().
#pragma once

#include "ants_server/loop_load.hpp"

void run_bot_queue_tests() {
    TEST_CASE("S3.300 A Batch Over What The Limits Allow Starts What Fits And Keeps The Rest As ONE Queue: The Loop Starts Two A Pass Up To The Eighty, The Rest Waits Held By The Cap (Not Dropped, Not Refused), A Match That Ends Or Is Stopped Lets The Next One In At The Next Pass, And The Row Counts Done And Running") {
        BotDoor d("botqueue-cap");
        const ctl::HttpResponse r = d.post("bot-matches", batch_body(100, "TINY.LVL", "highest-score", "2"));
        ASSERT_EQ(r.status, 200);
        const ctl::JsonValue a = BotDoor::json(r);
        ASSERT_TRUE(a.get("started").as_int_or(0) == 8 && a.get("asked").as_int_or(0) == 100 && a.get("queued").as_int_or(0) == 92 && a.get("at_once").as_int_or(0) == 80 && !a.has("every_s") && !a.has("stopped_for"));          // (eight now: one request never holds the thread for a hundred rooms)
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 8 && d.bots->waiting_count() == 92 && d.bots->queue_count() == 1);
        d.bots->update(d.wall, d.w.now);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 10 && d.bots->waiting_count() == 90);               // (one pass of the loop makes two: no more)
        d.bots->update(d.wall, d.w.now);
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{12});
        for (int i = 0; i < 60; ++i) d.bots->update(d.wall, d.w.now);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == kBotMatchesMax && d.bots->waiting_count() == 20);   // (the eighty are full: twenty wait)
        for (int i = 0; i < 3; ++i) d.bots->update(d.wall, d.w.now);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == kBotMatchesMax && d.bots->waiting_count() == 20);
        const ctl::JsonValue row = queues_of(d).at(0);
        ASSERT_TRUE(row.get("held").as_bool_or(false) && row.get("why").str() == "cap" && row.get("total").as_int_or(0) == 100 && row.get("remaining").as_int_or(0) == 20 && row.get("started").as_int_or(0) == 80 && row.get("running").as_int_or(0) == 80 && row.get("finished").as_int_or(-1) == 0);
        ASSERT_EQ(row.get("at").as_int_or(-1), 0);                                                    // (a batch without a spacing has no time: it starts when there is room)
        ASSERT_TRUE(d.bots->take_notices().empty());                                                  // (waiting is not failing)
        // a stopped match is done, and its place goes to the next start at the next pass
        const std::string first = BotDoor::json(d.get("bot-matches")).get("matches").at(0).get("id").str();
        ASSERT_EQ(d.post("bot-matches/" + first + "/stop").status, 200);
        stagger_step(d, 1000);
        stagger_step(d, 1000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == kBotMatchesMax && d.bots->waiting_count() == 19);
        const ctl::JsonValue after = queues_of(d).at(0);
        ASSERT_TRUE(after.get("finished").as_int_or(-1) == 1 && after.get("running").as_int_or(0) == 80 && after.get("started").as_int_or(0) == 81);
        // the queue is one object whatever its size, and the summary is small
        const ctl::JsonValue sum = BotDoor::json(d.get("bot-matches/summary"));
        ASSERT_TRUE(sum.get("at_once").as_int_or(0) == 80 && sum.get("running").as_int_or(0) == 80 && sum.get("waiting").as_int_or(0) == 19 && sum.get("queues").size() == 1 && sum.get("gate").as_int_or(0) == 40 && sum.get("untrusted").as_bool_or(false));
        ASSERT_TRUE(sum.get("queues").at(0).get("total").as_int_or(0) == 100 && sum.get("queues").at(0).get("next_in").as_int_or(0) == -1 && !sum.get("queues").at(0).has("what") && sum.get("queues").at(0).get("by").str() == "dave");
        ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);
        ASSERT_TRUE(d.bots->waiting_count() == 0 && d.bots->queue_count() == 0);
    } TEST_END();

    TEST_CASE("S3.301 A Queue Holds Ten Thousand Starts And Stays Small: One Row In The List, A Summary Of A Few Kilobytes With Forty Queues, The Loop Makes At Most Two Starts A Pass, And A Request For A Batch Of Ten Thousand Starts Eight Before It Answers") {
        BotDoor d("botqueue-big");
        const ctl::HttpResponse r = d.post("bot-matches", batch_body(10000, "TINY.LVL", "highest-score", "2"));
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(BotDoor::json(r).get("started").as_int_or(0) == 8 && BotDoor::json(r).get("queued").as_int_or(0) == 9992 && d.bots->waiting_count() == 9992);
        const ctl::HttpResponse list = d.get("bot-matches");
        ASSERT_TRUE(list.status == 200 && list.body.size() < 6000);                                   // (the page's list is the eight matches and one row: not ten thousand rows)
        ASSERT_EQ(BotDoor::json(list).get("queues").size(), size_t{1});
        ASSERT_EQ(d.post("bot-matches", batch_body(1, "TINY.LVL", "highest-score", "2")).status, 200); // (a small batch beside it is taken: the bound is on the starts that wait, 9,992 of 10,000)
        ASSERT_EQ(d.post("bot-matches", batch_body(10, "TINY.LVL", "highest-score", "2")).status, 409);          // (10 would make 10,002)
        d.post("bot-matches/stop-all");
        for (int i = 0; i < 40; ++i) ASSERT_EQ(d.post("bot-matches", batch_body(10, "TINY.LVL", "highest-score", "2", "random", "", "[\"rmh\",\"rmh\",\"rmh\",\"rmh\"]")).status, 200);
        ASSERT_TRUE(d.bots->queue_count() >= 30 && d.bots->queue_count() <= 40 && d.w.mgr.bot_match_count() <= kBotMatchesMax);       // (eight start now: after ten batches the eighty are full, and the next batches wait whole, a queue each)
        const ctl::HttpResponse summary = d.get("bot-matches/summary");
        ASSERT_TRUE(summary.status == 200 && summary.body.size() < 12000 && BotDoor::json(summary).get("queues").size() == d.bots->queue_count());
        d.post("bot-matches/stop-all");
        {   // a schedule's run that would add a queue past the 50 starts its first match and says that the rest does not fit (the page's own check is stricter, so this is the schedule's only guard)
            BotDoor s("botqueue-sched-full");
            std::string why;
            for (int i = 0; i < 50; ++i) ASSERT_EQ(s.post("bot-matches", spaced_body(2, 600)).status, 200);
            ASSERT_EQ(s.bots->queue_count(), size_t{50});
            BotSchedule sched;
            ASSERT_TRUE(s.schedules->create(spaced_schedule("Swarm", 5, 3, 60), kBotWall0, sched, why));
            (void)s.bots->take_notices();
            s.bots->update(kBotWall0 + 300, s.w.now);
            const std::vector<std::string> notes = s.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0].find("more starts than the server holds wait already") != std::string::npos);
            ASSERT_TRUE(s.bots->queue_count() == 50 && s.bots->waiting_count() == 50 && s.w.mgr.bot_match_count() == 51);
        }
    } TEST_END();

    TEST_CASE("S3.302 The Hour's Slots Of Recordings Are Counted In The Queue's Row: A Match That Holds A Slot Is Counted As Kept When It Starts (The Slots Left, Not The Number Played), The Others Are Played And Not Kept") {
        BotDoor d("botqueue-kept", ServerLimits(), true, true, 3);
        ASSERT_EQ(d.post("bot-matches", batch_body(20, "TINY.LVL", "highest-score", "2")).status, 200);
        for (int i = 0; i < 6; ++i) d.bots->update(d.wall, d.w.now);
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{20});
        ASSERT_EQ(d.w.mgr.bot_kept_count(), size_t{3});
        const ctl::JsonValue row = queues_of(d).at(0);
        ASSERT_TRUE(row.get("kept").as_int_or(-1) == 3 && row.get("started").as_int_or(0) == 20 && row.get("remaining").as_int_or(-1) == 0);
        d.post("bot-matches/stop-all");
    } TEST_END();

    TEST_CASE("S3.303 A Start That Can Never Be Made Ends Its Queue With One Line In The Log (Its Map Was Taken Down) And Nothing Else Is Dropped; A Schedule's Batch Is Still 1 To 8 And A Batch Of The Page Is Not") {
        BotDoor d("botqueue-gone");
        ASSERT_EQ(d.post("bot-matches", batch_body(20, "Fight Island.lvl", "187", "2")).status, 200);
        ASSERT_EQ(d.post("bot-matches", batch_body(15, "TINY.LVL", "highest-score", "2")).status, 200);
        ASSERT_EQ(d.bots->queue_count(), size_t{2});
        d.w.mgr.mutable_store().set_approved({});                                                     // (the maintainer takes the map down)
        d.bots->update(d.wall, d.w.now);
        const std::vector<std::string> notes = d.bots->take_notices();
        ASSERT_TRUE(notes.size() == 1 && notes[0].find("the rest was dropped") != std::string::npos && notes[0].find("bot queue q") == 0);
        ASSERT_TRUE(d.bots->queue_count() >= 1 && d.bots->waiting_count() > 0);                       // (the other queue goes on)
        d.post("bot-matches/stop-all");
        const std::string flat = batch_body(9, "TINY.LVL", "highest-score", "2");
        const ctl::HttpResponse nine = d.post("bot-schedules", "{\"name\":\"Nine\",\"on\":true,\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + flat + "}");
        ASSERT_TRUE(nine.status == 400 && BotDoor::json(nine).get("error").str() == "bad_count");
        const std::string eight = batch_body(8, "TINY.LVL", "highest-score", "2");
        ASSERT_EQ(d.post("bot-schedules", "{\"name\":\"Eight\",\"on\":true,\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + eight + "}").status, 200);
        ASSERT_EQ(d.post("bot-matches", batch_body(9, "TINY.LVL", "highest-score", "2")).status, 200);       // (Start now takes any number up to 10,000)
        d.post("bot-matches/stop-all");
    } TEST_END();

    TEST_CASE("S3.304 The Loop's Meter Says What Part Of The Last Ten Seconds The Loop Was Busy: Empty Reads 0, Half Of Every Second Reads Half, A Second That Falls Out Of The Window Is Forgotten, A Silence Of Ten Seconds Empties It, A Clock That Goes Back Changes Nothing, And It Never Reads Over One") {
        LoopLoad load;
        ASSERT_TRUE(load.share(0) == 0.0);
        for (uint32_t s = 0; s < 10; ++s) load.add(s * 1000 + 10, 500000);
        ASSERT_TRUE(load.share(9500) > 0.49 && load.share(9500) < 0.51);
        ASSERT_TRUE(load.share(10500) > 0.44 && load.share(10500) < 0.46);                              // (the first second is out of the window)
        ASSERT_TRUE(load.share(14500) > 0.24 && load.share(14500) < 0.26);
        const double before = load.share(14500);
        ASSERT_TRUE(load.share(14000) == before);                                                       // (a clock that went back: nothing)
        ASSERT_EQ(load.share(60000), 0.0);                                                              // (a minute of silence: empty)
        for (uint32_t s = 0; s < 3; ++s) load.add(60000 + s * 100, 5000000);
        ASSERT_EQ(load.share(60300), 1.0);                                                              // (more busy time than the time that passed reads one, never more)
        LoopLoad fresh;
        fresh.add(1000, 100000);
        ASSERT_TRUE(fresh.share(1000) > 0.0099 && fresh.share(1000) < 0.0101);                         // (a server that has just started reads low: the slots not filled yet count as idle)
    } TEST_END();
}

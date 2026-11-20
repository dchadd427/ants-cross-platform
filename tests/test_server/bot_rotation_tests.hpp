// The tests of the catalog of the server's maps and the two lists of the rotation (RoomManager::bot_map_options, bot_catalog_has, set_bot_rotation; BotMatches' routes bot-options, bot-rotation and bot-rotation/reset;
// BotRotationStore; docs/SERVER_REPLAYS.md "The lists", docs/MAP_SUBMISSIONS.md "Bot matches on the admin page"): any map of the server can be named, by its exact name and never as a path; the catalog is judged a few maps at a
// time and never all at once; an edited list is checked, kept, logged, drawn from and brought back to the defaults; a map that is gone from a list is skipped. Included by test_server_files.cpp after bot_admin_tests.hpp, whose
// fixtures it uses; test_server.cpp calls run_bot_rotation_tests(). The store alone is test_bot_rotation.cpp.
#pragma once

namespace {

// The body of a change of a list, as the page sends it
std::string rotation_body(const std::string& rules, const std::vector<std::string>& files) {
    std::string maps;
    for (const std::string& f : files) maps += std::string(maps.empty() ? "" : ",") + "\"" + f + "\"";
    return "{\"rules\":\"" + rules + "\",\"maps\":[" + maps + "]}";
}

std::vector<std::string> default_files(bool kills_187) {
    std::vector<std::string> out;
    for (const BotRotationEntry& e : bot_rotation_defaults(kills_187)) out.push_back(e.file);
    return out;
}

std::vector<std::string> files_in(const ctl::JsonValue& list) {
    std::vector<std::string> out;
    for (size_t i = 0; i < list.size(); ++i) out.push_back(list.at(i).get("file").str());
    return out;
}

// The state of every map of a list as the page gets it: "ok", "skipped" or "checking"
std::string states_of(const ctl::JsonValue& list) {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) out += list.at(i).get("state").str() + ",";
    return out;
}

// Every map of the catalog is judged (the first calls of the page would do it a few at a time)
void judge_everything(BotDoor& d) { (void)d.w.mgr.bot_map_options(kBotLoadAll); }

// The maps that a draw from the lists of the rotation gives in `rounds` matches (one at a time: each is stopped and the world run on)
std::set<std::string> rotation_draws(BotDoor& d, BotRules rules, int rounds, std::set<uint8_t>* modes = nullptr) {
    std::set<std::string> drawn;
    BotMatchSpec spec;
    spec.rules = rules;
    spec.bots = 2;
    for (int i = 0; i < rounds; ++i) {
        std::string code, reason;
        if (!d.w.mgr.start_bot_match(d.w.now, spec, code, reason)) {
            drawn.insert("<refused: " + reason + ">");
            break;
        }
        for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) {
            drawn.insert(m.map);
            if (modes != nullptr) modes->insert(m.mode);
        }
        d.w.mgr.stop_bot_matches(d.w.now);
        d.w.run(1200);
    }
    return drawn;
}

// A made-up map of the players whose file is not a map (the engine cannot play it), or a good one
ApprovedMap approved_map(const char* tag, const char* name, uint32_t minutes = 10, bool garbage = false) {
    ApprovedMap a = made_up_fight_island(tag, 60, 60, name, minutes);
    if (garbage) {
        { std::ofstream out(a.path, std::ios::binary | std::ios::trunc); out << "this is not a map of the game"; }
        a.size = 29;
        net::hash_file(a.path, a.hash);
    }
    return a;
}

}  // namespace

void run_bot_rotation_tests() {
    TEST_CASE("S3.285 Any Map Of The Server Can Be Named, By Its Exact Name: Every Map Of The Original's Six, The Library And The Approved Maps Of The Players Is In The Catalog And Plays; A Name That The Catalog Does Not Have Is Never Looked For On The Disk (A Path, Another Letter Case, A Test Map)") {
        BotDoor d("botrot-any");
        const std::vector<BotMapOption> catalog = d.w.mgr.bot_map_options(kBotLoadAll);
        ASSERT_EQ(catalog.size(), size_t{541});
        for (const BotMapOption& o : catalog) {
            bool approved = false;
            ASSERT_TRUE(o.checked && o.playable && o.reason.empty() && d.w.mgr.bot_catalog_has(o.file, approved) && approved == (o.group == "players"));          // (the whole library plays: every name of it is a choice)
        }
        std::string code, reason;
        BotRefusal refusal = BotRefusal::None;
        for (const char* file : {"Killing Trez.LVL", "!Pond.lvl", "BoMbZ AwAy PoPcOrN.lvl", "POPcOrN.lvl", "Fight Island.lvl"}) {              // (a library map that no list has, one whose name starts with a mark, a long name, one of the lists, a map of the players)
            BotMatchSpec spec;
            spec.map = file;
            spec.rules = BotRules::HighestScore;
            spec.bots = 2;
            ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, spec, code, reason));
            const std::vector<BotMatchInfo> going = d.w.mgr.bot_matches(d.w.now);
            ASSERT_TRUE(going.size() == 1 && going[0].map == file);
            d.w.mgr.stop_bot_matches(d.w.now);
            d.w.run(1200);
        }
        // a name that the catalog does not list: refused before any file is looked for
        d.w.mgr.mutable_store().set_test_maps({approved_map("botrot-any-test", "Foo ~test-0123abcd.lvl")});
        for (const char* file : {"Nowhere.lvl", "tiny.lvl", "killing trez.lvl", "Foo ~test-0123abcd.lvl"}) {
            BotMatchSpec spec;
            spec.map = file;
            ASSERT_FALSE(d.w.mgr.start_bot_match(d.w.now, spec, code, reason, &refusal));
            ASSERT_TRUE(refusal == BotRefusal::NoMap && reason.find("not one of the maps of this server") != std::string::npos);
        }
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
        bool approved = false;
        ASSERT_FALSE(d.w.mgr.bot_catalog_has("Foo ~test-0123abcd.lvl", approved));                                                             // (a test map is no map of the catalog: it is never listed)
        for (const BotMapOption& o : d.w.mgr.bot_map_options(kBotLoadAll)) ASSERT_TRUE(o.file.find("~test-") == std::string::npos);
        // through the door: the name is data, matched against the catalog, and every shape of a path is refused with the page's own word
        ctl::HttpResponse r = d.post("bot-matches", batch_body(1, "Killing Trez.LVL", "highest-score", "2"));
        ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("started").as_int_or(0) == 1);
        ASSERT_TRUE(d.log().size() == 1 && d.log()[0].detail.find("Killing Trez.LVL") != std::string::npos);
        const ctl::JsonValue list = BotDoor::json(d.get("bot-matches"));
        ASSERT_TRUE(list.get("matches").size() == 1 && list.get("matches").at(0).get("name").str() == "Killing Trez");
        for (const char* file : {"../TINY.LVL", "..\\\\TINY.LVL", "sub/TINY.LVL", "/etc/passwd.lvl", "TINY.txt", ".lvl", "TINY"}) {
            r = d.post("bot-matches", batch_body(1, file, "highest-score", "2"));
            ASSERT_TRUE(r.status == 400 && BotDoor::json(r).get("error").str() == "bad_map");
        }
        r = d.post("bot-matches", batch_body(1, "Nowhere.lvl", "highest-score", "2"));
        ASSERT_TRUE(r.status == 409 && BotDoor::json(r).get("error").str() == "no_map");
        ASSERT_EQ(d.log().size(), size_t{1});                                                                                                    // (a refusal is no line of the log)
    } TEST_END();

    TEST_CASE("S3.286 The Catalog Is Judged A Few Maps At A Time, Never All At Once: A Cold Call Loads At Most 48 Files And Lists The Rest As Not Checked, Each Call Judges Others, A Map Is Loaded Once, And The Loop's Idle Passes Warm The Rest A Few At A Time, But Not In A Pass That Starts A Match") {
        {
            BotDoor d("botrot-bound");
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{0});
            size_t last_unchecked = 0;
            size_t calls = 0;
            for (; calls < 30; ++calls) {
                const size_t before = d.w.mgr.bot_map_loads();
                const ctl::JsonValue o = BotDoor::json(d.get("bot-options"));
                ASSERT_EQ(o.get("maps").size(), size_t{541});
                ASSERT_LE(d.w.mgr.bot_map_loads() - before, kBotCatalogLoads);                                                                    // (the bound: never the whole library in one call of the loop)
                const size_t unchecked = static_cast<size_t>(o.get("checking").as_int_or(-1));
                size_t flagged = 0;
                for (size_t i = 0; i < o.get("maps").size(); ++i) flagged += o.get("maps").at(i).has("checked") && !o.get("maps").at(i).get("checked").as_bool_or(true) ? 1 : 0;
                ASSERT_EQ(flagged, unchecked);
                if (calls == 0) ASSERT_EQ(unchecked, size_t{541} - kBotCatalogLoads);
                else ASSERT_LT(unchecked, last_unchecked);                                                                                        // (each call judges others)
                last_unchecked = unchecked;
                if (unchecked == 0) break;
            }
            ASSERT_TRUE(last_unchecked == 0 && calls == 11);                                                                                      // (541 files, 48 a call: twelve calls)
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{541});                                                                                      // (each file once)
            (void)d.get("bot-options");
            (void)d.get("bot-rotation");
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{541});                                                                                      // (judged: asked again, nothing is loaded)
        }
        {   // the loop's idle passes judge them, four at a time, so that the first call of the page finds them done
            BotDoor d("botrot-warm");
            for (size_t pass = 1; pass <= 20; ++pass) {
                d.bots->update(d.wall, d.w.now);
                ASSERT_EQ(d.w.mgr.bot_map_loads(), pass * kBotWarmLoadsPerPass);
            }
            size_t guard = 0;
            while (d.w.mgr.bot_map_loads() < 541 && guard++ < 200) d.bots->update(d.wall, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{541});
            d.bots->update(d.wall, d.w.now);
            d.bots->update(d.wall + 5, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{541});                                                                                      // (nothing is left: nothing is loaded)
            const ctl::JsonValue o = BotDoor::json(d.get("bot-options"));
            ASSERT_EQ(o.get("checking").as_int_or(-1), 0);
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{541});
            d.w.now += 11000;                                                                                                                       // (a map that is approved later is judged in the next look, ten seconds on)
            d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0], approved_map("botrot-warm-new", "Brand New.lvl")});
            d.bots->update(d.wall, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{542});
        }
        {   // a pass that starts a match judges nothing else: the schedule is due, the match is made, and only what the match drew from was loaded
            BotDoor d("botrot-warm-start");
            BotSchedulePatch p;
            p.has_name = true;
            p.name = "Tiny";
            p.has_when = true;
            p.kind = BotSchedule::Kind::Every;
            p.minutes = 30;
            p.has_what = true;
            p.what.spec.map = "TINY.LVL";
            p.what.spec.rules = BotRules::HighestScore;
            p.what.spec.bots = 2;
            BotSchedule made;
            std::string why;
            ASSERT_TRUE(d.schedules->create(p, d.wall, made, why));
            d.bots->update(made.next_run, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{1});                                                                                         // (the one map that it started on)
            d.bots->update(made.next_run + 1, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_map_loads(), size_t{1} + kBotWarmLoadsPerPass);                                                                  // (the next pass is idle: it warms)
        }
    } TEST_END();

    TEST_CASE("S3.287 The Rotation Routes: The Lists Are Listed With Each Map's State And The Defaults; A List Is Replaced Whole, Checked Against The Catalog (A Name Is Never A Path), Kept, Logged And Drawn From; Every Refusal Has Its Code And Changes Nothing; A Hundred Maps At The Most") {
        BotDoor d("botrot-routes");
        judge_everything(d);
        ctl::JsonValue o = BotDoor::json(d.get("bot-rotation"));
        ASSERT_TRUE(o.get("max").as_int_or(0) == 100 && o.get("checking").as_int_or(-1) == 0 && o.get("untrusted").as_bool_or(false) && o.get("untrusted_fields").size() == 1 && o.get("untrusted_fields").at(0).str() == "name");
        ASSERT_EQ(files_in(o.get("lists").get("highest-score")), default_files(false));
        ASSERT_EQ(files_in(o.get("lists").get("187")), default_files(true));
        ASSERT_EQ(files_in(o.get("defaults").get("highest-score")), default_files(false));
        ASSERT_EQ(states_of(o.get("lists").get("highest-score")), std::string("ok,ok,ok,ok,ok,ok,ok,ok,ok,"));
        const ctl::JsonValue& tiny = o.get("lists").get("highest-score").at(0);
        ASSERT_TRUE(tiny.get("name").str() == "TINY" && tiny.get("group").str() == "original" && tiny.get("width").as_int_or(0) == 31 && tiny.get("players").as_int_or(0) == 4 && tiny.get("minutes").as_int_or(0) == 6 && tiny.get("places").as_int_or(0) == 1 && tiny.get("random").as_bool_or(false));
        ASSERT_TRUE(o.get("lists").get("187").at(5).get("group").str() == "players" && o.get("lists").get("187").at(5).get("places").as_int_or(0) == 3);
        // replace the list of the original's game: the defaults and a map of the library that no list has
        std::vector<std::string> mine = default_files(false);
        mine.push_back("Killing Trez.LVL");
        ctl::HttpResponse r = d.post("bot-rotation", rotation_body("highest-score", mine));
        ASSERT_MSG(r.status == 200, r.body);
        ASSERT_TRUE(BotDoor::json(r).get("count").as_int_or(0) == 10 && BotDoor::json(r).get("changed").as_bool_or(false));
        ASSERT_TRUE(d.rotation->edited(false) && !d.rotation->edited(true) && d.w.mgr.bot_rotation(false).size() == 10);
        std::vector<AuditEntry> log = d.log();
        ASSERT_EQ(log.size(), size_t{1});
        ASSERT_TRUE(log[0].action == "bot_rotation_changed" && log[0].target == "Highest score" && log[0].detail == "+1 -0, 10 maps" && log[0].actor == "dave" && log[0].ref == kBotRefDave && log[0].at == d.wall);
        o = BotDoor::json(d.get("bot-rotation"));
        ASSERT_EQ(files_in(o.get("lists").get("highest-score")), mine);
        ASSERT_EQ(files_in(o.get("lists").get("187")), default_files(true));
        ASSERT_EQ(files_in(o.get("defaults").get("highest-score")), default_files(false));                                                      // (the defaults are what the server came with, not what was saved)
        r = d.post("bot-rotation", rotation_body("highest-score", mine));                                                                         // (the same save: nothing is written or logged)
        ASSERT_TRUE(r.status == 200 && !BotDoor::json(r).get("changed").as_bool_or(true) && d.log().size() == 1);
        // 187 gets two maps and loses four: the line says how many
        r = d.post("bot-rotation", rotation_body("187", {"TINY.LVL", "SMALL.LVL", "Killing Trez.LVL", "Killing Spree.LVL"}));
        ASSERT_EQ(r.status, 200);
        log = d.log();
        ASSERT_TRUE(log[0].action == "bot_rotation_changed" && log[0].target == "187" && log[0].detail == "+2 -4, 4 maps");
        // the refusals, each with its own code and no change: the lists, the log and the table are as they were
        const std::vector<AuditEntry> before = d.log();
        const std::vector<BotRotationEntry> hs_before = d.w.mgr.bot_rotation(false);
        const auto refused = [&](const std::string& body, int status, const char* code) {
            const ctl::HttpResponse a = d.post("bot-rotation", body);
            return a.status == status && BotDoor::json(a).get("error").str() == code && d.log().size() == before.size() && bot_rotation_same(d.w.mgr.bot_rotation(false), hs_before) && bot_rotation_same(d.rotation->list(false), hs_before);
        };
        ASSERT_TRUE(refused("{\"maps\":[\"TINY.LVL\"]}", 400, "bad_rules"));
        ASSERT_TRUE(refused("{\"rules\":\"random\",\"maps\":[\"TINY.LVL\"]}", 400, "bad_rules"));
        ASSERT_TRUE(refused("{\"rules\":\"highest-score\"}", 400, "bad_maps"));
        ASSERT_TRUE(refused("{\"rules\":\"highest-score\",\"maps\":\"TINY.LVL\"}", 400, "bad_maps"));
        ASSERT_TRUE(refused(rotation_body("highest-score", {}), 400, "empty"));                                                                  // (a list with no map would never be drawn)
        ASSERT_TRUE(refused(rotation_body("highest-score", {"TINY.LVL", "TINY.LVL"}), 400, "bad_map"));
        ASSERT_TRUE(refused(rotation_body("highest-score", {"TINY.LVL", "Nowhere.lvl"}), 400, "bad_map"));
        ASSERT_TRUE(refused(rotation_body("highest-score", {"tiny.lvl"}), 400, "bad_map"));                                                      // (the name is exact)
        for (const char* path : {"../TINY.LVL", "..\\\\TINY.LVL", "sub/TINY.LVL", "/etc/passwd.lvl", "TINY.txt", ".lvl"}) ASSERT_TRUE(refused(rotation_body("highest-score", {"TINY.LVL", path}), 400, "bad_map"));
        ASSERT_TRUE(refused("{\"rules\":\"highest-score\",\"maps\":[\"TINY.LVL\",3]}", 400, "bad_map"));
        ASSERT_TRUE(refused("{\"rules\":\"highest-score\",\"maps\":[[\"TINY.LVL\"]]}", 400, "bad_map"));
        d.w.mgr.mutable_store().set_test_maps({approved_map("botrot-routes-test", "Foo ~test-0123abcd.lvl")});
        ASSERT_TRUE(refused(rotation_body("highest-score", {"TINY.LVL", "Foo ~test-0123abcd.lvl"}), 400, "bad_map"));                              // (a test map is no map of the catalog)
        std::vector<std::string> over;
        for (size_t i = 0; i < kBotRotationMax + 1; ++i) over.push_back("Map " + std::to_string(i) + ".lvl");
        ASSERT_TRUE(refused(rotation_body("highest-score", over), 400, "too_many"));
        // a hundred maps of the library are a list, and the table keeps them
        std::vector<std::string> hundred;
        for (const BotMapOption& m : d.w.mgr.bot_map_options(kBotLoadAll)) {
            if (hundred.size() < kBotRotationMax && m.group == "library") hundred.push_back(m.file);
        }
        ASSERT_EQ(hundred.size(), kBotRotationMax);
        r = d.post("bot-rotation", rotation_body("highest-score", hundred));
        ASSERT_MSG(r.status == 200 && BotDoor::json(r).get("count").as_int_or(0) == 100, r.body);
        ASSERT_TRUE(d.w.mgr.bot_rotation(false).size() == 100 && d.rotation->list(false).size() == 100);
        // a map that a room cannot play is not put in a list: the file is judged when the list is saved
        d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0], approved_map("botrot-routes-bad", "Not A Map.lvl", 10, true)});
        r = d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "Not A Map.lvl"}));
        const ctl::JsonValue bad = BotDoor::json(r);
        ASSERT_TRUE(r.status == 409 && bad.get("error").str() == "no_map" && bad.get("file").str() == "Not A Map.lvl" && bad.get("why").str().find("cannot play") != std::string::npos);
        ASSERT_EQ(d.w.mgr.bot_rotation(false).size(), size_t{100});
    } TEST_END();

    TEST_CASE("S3.288 ISLANDS Can Be Put In The List Of 187 (The Page Asks Once): The Match It Is Drawn For Is Played In 187 On ISLANDS; Naming ISLANDS With 187 In Start Now Is Still Refused") {
        BotDoor d("botrot-islands");
        judge_everything(d);
        ctl::HttpResponse r = d.post("bot-rotation", rotation_body("187", {"TINY.LVL", "ISLANDS.LVL"}));
        ASSERT_MSG(r.status == 200, r.body);
        ASSERT_EQ(d.w.mgr.bot_rotation(true).size(), size_t{2});
        r = d.post("bot-rotation", rotation_body("187", {"ISLANDS.LVL"}));
        ASSERT_EQ(r.status, 200);
        std::set<uint8_t> modes;
        const std::set<std::string> drawn = rotation_draws(d, BotRules::Kills187, 6, &modes);
        ASSERT_TRUE(drawn == std::set<std::string>{"ISLANDS.LVL"} && modes == std::set<uint8_t>{static_cast<uint8_t>(sim::GameMode::Kills187)});
        r = d.post("bot-matches", batch_body(1, "ISLANDS.LVL", "187", "2"));
        ASSERT_TRUE(r.status == 409 && BotDoor::json(r).get("error").str() == "map_187");                                                         // (a named ISLANDS in 187 is refused before anything is drawn: the rule is unchanged)
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
    } TEST_END();

    TEST_CASE("S3.289 A List Is What The Draw Uses: One Map Is Every Match, Two Maps Are Both And Nothing Else, A Map Of The Library Is Drawn In 187 Too, A Long Map Is Drawn On Purpose, The Original's And The Players' Random Picks Do Not Use The Lists, And Back To The Defaults Is Today's List") {
        BotDoor d("botrot-draws");
        judge_everything(d);
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL"})).status, 200);
        ASSERT_EQ(d.post("bot-rotation", rotation_body("187", {"Killing Trez.LVL"})).status, 200);
        std::set<uint8_t> modes;
        ASSERT_TRUE(rotation_draws(d, BotRules::HighestScore, 6) == std::set<std::string>{"TINY.LVL"});
        ASSERT_TRUE(rotation_draws(d, BotRules::Kills187, 6, &modes) == std::set<std::string>{"Killing Trez.LVL"} && modes == std::set<uint8_t>{static_cast<uint8_t>(sim::GameMode::Kills187)});
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "SMALL.LVL"})).status, 200);
        ASSERT_TRUE(rotation_draws(d, BotRules::HighestScore, 30) == (std::set<std::string>{"TINY.LVL", "SMALL.LVL"}));
        BotMatchSpec original;                                                                                                                  // (the other random picks keep to their own maps)
        original.map = kBotMapOriginal;
        original.rules = BotRules::HighestScore;
        original.bots = 2;
        std::set<std::string> from_original;
        for (int i = 0; i < 40; ++i) {
            std::string code, reason;
            ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, original, code, reason));
            for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) from_original.insert(m.map);
            d.w.mgr.stop_bot_matches(d.w.now);
            d.w.run(1200);
        }
        ASSERT_GE(from_original.size(), size_t{5});                                                                                              // (not two: the six of the original's, whatever the list says)
        // back to the defaults: today's list, the same draws as before there was a list to edit
        ctl::HttpResponse r = d.post("bot-rotation/reset", "{\"rules\":\"highest-score\"}");
        ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("changed").as_bool_or(false) && BotDoor::json(r).get("count").as_int_or(0) == 9);
        ASSERT_TRUE(!d.rotation->edited(false) && d.rotation->edited(true) && bot_rotation_same(d.w.mgr.bot_rotation(false), bot_rotation_defaults(false)));
        const std::vector<AuditEntry> log = d.log();
        ASSERT_TRUE(log[0].action == "bot_rotation_reset" && log[0].target == "Highest score" && log[0].detail == "9 maps" && log[0].actor == "dave");
        r = d.post("bot-rotation/reset", "{\"rules\":\"highest-score\"}");                                                                       // (twice: nothing to do, nothing logged)
        ASSERT_TRUE(r.status == 200 && !BotDoor::json(r).get("changed").as_bool_or(true) && d.log().size() == log.size());
        ASSERT_TRUE(drawable(d.w.mgr, false) == 9 && rotation_draws(d, BotRules::HighestScore, 60).size() >= 7);
        ASSERT_EQ(d.post("bot-rotation/reset", "{\"rules\":\"both\"}").status, 400);
        ASSERT_EQ(d.post("bot-rotation/reset", "{}").status, 400);
        ASSERT_TRUE(d.rotation->edited(true));                                                                                                   // (the other list is as it was)
    } TEST_END();

    TEST_CASE("S3.290 A Map Of A List That Cannot Be Played Is Skipped And Shown As Skipped, And Is Drawn Again When It Comes Back: A Map Of The Players That Was Taken Down Stays In The List (Remove Is The Admin's), A Save Around It Is Not Refused, And The Log Names It Once") {
        BotDoor d("botrot-skip");
        judge_everything(d);
        const ApprovedMap mossy = approved_map("botrot-skip-mossy", "Mossy Ridge.lvl");
        d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0], mossy});
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "Mossy Ridge.lvl"})).status, 200);
        ASSERT_TRUE(d.w.mgr.bot_rotation(false)[1].approved);
        judge_everything(d);
        ctl::JsonValue o = BotDoor::json(d.get("bot-rotation"));
        ASSERT_EQ(states_of(o.get("lists").get("highest-score")), std::string("ok,ok,"));
        d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0]});                                                               // (the maintainer takes it down)
        o = BotDoor::json(d.get("bot-rotation"));
        ASSERT_EQ(states_of(o.get("lists").get("highest-score")), std::string("ok,skipped,"));
        const ctl::JsonValue& gone = o.get("lists").get("highest-score").at(1);
        ASSERT_TRUE(gone.get("file").str() == "Mossy Ridge.lvl" && gone.get("reason").str() == "taken down" && gone.get("name").str() == "Mossy Ridge");
        d.w.mgr.take_notices();
        ASSERT_TRUE(rotation_draws(d, BotRules::HighestScore, 12) == std::set<std::string>{"TINY.LVL"});                                           // (never drawn: the others are as likely as before)
        size_t named = 0;
        for (const std::string& line : d.w.mgr.take_notices()) named += line.find("the map Mossy Ridge.lvl is skipped") != std::string::npos ? 1 : 0;
        ASSERT_EQ(named, size_t{1});                                                                                                              // (named once, not at every match)
        // a save that leaves the lost map where it is goes through; the map is the admin's to remove
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "Mossy Ridge.lvl", "SMALL.LVL"})).status, 200);
        ASSERT_EQ(d.w.mgr.bot_rotation(false).size(), size_t{3});
        ASSERT_TRUE(rotation_draws(d, BotRules::HighestScore, 30) == (std::set<std::string>{"TINY.LVL", "SMALL.LVL"}));
        // a map of the players that is not approved cannot be put in a list, and a library name that is not there is no name
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "Old Bridge.lvl"})).status, 400);
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "SMALL.LVL"})).status, 200);                                  // (and the lost map is removed)
        ASSERT_EQ(d.w.mgr.bot_rotation(false).size(), size_t{2});
        // it comes back (approved again): the same entry plays again
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "SMALL.LVL"})).status, 200);
        d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0], mossy});
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", {"TINY.LVL", "Mossy Ridge.lvl"})).status, 200);
        d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0]});
        o = BotDoor::json(d.get("bot-rotation"));
        ASSERT_EQ(states_of(o.get("lists").get("highest-score")), std::string("ok,skipped,"));
        d.w.mgr.mutable_store().set_approved({d.w.mgr.store().approved_maps()[0], mossy});
        ASSERT_TRUE(rotation_draws(d, BotRules::HighestScore, 30) == (std::set<std::string>{"TINY.LVL", "Mossy Ridge.lvl"}));
    } TEST_END();

    TEST_CASE("S3.291 A List That An Admin Saved Is Kept Across A Restart: The Table Has It, A New Store And A New BotMatches Over The Same File Give The Rooms The Same Lists, And A List That Was Never Saved Is The Defaults") {
        BotDoor d("botrot-restart");
        judge_everything(d);
        const std::vector<std::string> mine = {"TINY.LVL", "Killing Trez.LVL", "POPcOrN.lvl"};
        ASSERT_EQ(d.post("bot-rotation", rotation_body("highest-score", mine)).status, 200);
        d.w.mgr.set_bot_rotation(false, bot_rotation_defaults(false));                                                                             // (a new process: its rooms start with the defaults)
        d.w.mgr.set_bot_rotation(true, {{"TINY.LVL", false}});
        std::string why;
        BotRotationStore again(&d.database);
        ASSERT_TRUE(again.open(why) && again.edited(false) && !again.edited(true));
        BotMatches restarted(d.w.mgr, *d.schedules, again, d.audit.get());                                                                          // (the lists of the file are given to the rooms at once)
        std::vector<std::string> now_false, now_true;
        for (const BotRotationEntry& e : d.w.mgr.bot_rotation(false)) now_false.push_back(e.file);
        for (const BotRotationEntry& e : d.w.mgr.bot_rotation(true)) now_true.push_back(e.file);
        ASSERT_EQ(now_false, mine);
        ASSERT_EQ(now_true, default_files(true));
    } TEST_END();
}

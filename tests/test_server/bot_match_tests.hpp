// The tests of the server's own matches of computer players (RoomSpec::bots_only, RoomManager::enable_bot_matches / start_bot_match; docs/SERVER.md "Matches of computer players"): a room whose seats are
// all bots starts with nobody in it, is not closed for lack of people and is kept like any other match; the specification that cannot be such a room is refused; the timer makes one match at a time, only
// when the server keeps replays, and chooses the same things from the same seed; each match is dealt the original's game or 187 by a coin of its own and then a map of that rules' list (each map of a list as likely
// as any other; a map that the store lacks is skipped with one line in the log), and a match in 187 plays on every map with every seating of the computer players. Included by test_server_files.cpp, which holds (through server_fixtures.hpp) the harness (TEST_CASE, ASSERT_*, World); test_server.cpp calls run_bot_match_tests().
#pragma once

#include "big_map_fixtures.hpp"

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

// Every seating that the timer's match can come to: two to four bots on seats (the first ones, and the last ones for two and three), free for all and every way to make teams of them (sim::roster_team_choices)
struct BotSeating {
    uint8_t roster;
    sim::StartTeams teams;
};

std::vector<BotSeating> every_bot_seating() {
    std::vector<BotSeating> out;
    for (const uint8_t roster : {uint8_t{0x03}, uint8_t{0x0C}, uint8_t{0x07}, uint8_t{0x0E}, uint8_t{0x0F}}) {
        for (const sim::StartTeams& teams : sim::roster_team_choices(roster)) out.push_back({roster, teams});
    }
    return out;
}

// The room of such a seating: bots on the seats of the roster, in `mode`, on `map`, with the teams
RoomSpec bot_seating_spec(const std::string& code, const char* map, const BotSeating& seating, uint8_t mode) {
    uint8_t players = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) players = static_cast<uint8_t>(players + ((seating.roster >> seat) & 1u));
    RoomSpec spec = bots_only_spec(code, players);
    uint8_t next = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((seating.roster & (1u << seat)) != 0) spec.bots[next++].seat = seat;
    }
    spec.map = map;
    spec.mode = mode;
    spec.teams = seating.teams;
    spec.run_ms = 25u * 60u * 1000u;                                         // (the server's own guard: the longest map's limit is 12 minutes)
    return spec;
}

const char* const kBotMatchMaps[] = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};

// The lists that the timer draws from (docs/SERVER.md "Matches of computer players"): the original's game on the six and on three library maps, 187 on five of the six and on a map of the players
const char* const kScoreListMaps[] = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL", "POPcOrN.lvl", "HELLRAISER.lvl", "Bombz Away.lvl"};
const char* const k187ListMaps[] = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "Fight Island.lvl"};

std::string bot_match_community() { return std::string(ANTS_SOURCE_DIR) + "/Community-Maps"; }

// The map of the players on the 187 list, made up (the real one is not in this repository): TINY grown to 150 x 150 as an Extended map, four hills, kept in a folder of its own as the submissions store keeps an approved file
ApprovedMap made_up_fight_island(const char* tag, uint32_t rows = 150, uint32_t columns = 150) {
    const std::string bytes = grown_tiny(rows, columns);
    const fs::path path = fs::path(temp_dir_for(tag)) / "Fight Island.lvl.bin";
    { std::ofstream out(path, std::ios::binary); out << bytes; }
    ApprovedMap a;
    a.name = "Fight Island.lvl";
    a.path = path.string();
    a.size = bytes.size();
    a.width = columns;
    a.height = rows;
    a.minutes = 20;
    a.players = 4;
    a.maker = "Mia";
    a.kind = "extended";
    net::hash_file(a.path, a.hash);
    return a;
}

// A server that has every map of the two lists: the original's six, the community folder of the repository and the made-up approved map
struct ListedWorld : World {
    explicit ListedWorld(const char* tag) : World(ServerLimits(), maps_dir(), bot_match_community()) { mgr.mutable_store().set_approved({made_up_fight_island(tag)}); }
};

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
        ASSERT_TRUE(s.bots_only && s.joined == 3 && s.bot_controller && s.bots.size() == 3 && s.names[0] == bn("BM-ROOM", 0, "Medium") && s.names[1] == bn("BM-ROOM", 1, "Hard"));
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
        ASSERT_EQ(f.rep.head.names, (std::array<std::string, sim::MAX_PLAYERS>{{bn("BM-ROOM", 0, "Medium"), bn("BM-ROOM", 1, "Hard"), bn("BM-ROOM", 2, "Medium"), ""}}));
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
            ListedWorld w("botmatch-timer-maps");
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
            const char* const* list = made->mode == 1 ? k187ListMaps : kScoreListMaps;      // (the map is one of the rules' list)
            const size_t listed = made->mode == 1 ? std::size(k187ListMaps) : std::size(kScoreListMaps);
            ASSERT_TRUE(std::find_if(list, list + listed, [&](const char* name) { return made->map == name; }) != list + listed);
            const std::string choice = bot_match_choice(*made) + " rules " + std::to_string(static_cast<unsigned>(made->mode));      // (the rules are drawn from the seed too)
            if (round == 0) first = choice;
            else ASSERT_EQ(choice, first);
            const std::vector<std::string> notes = w.mgr.take_notices();
            ASSERT_TRUE(made->mode <= 1);
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot match: room " + made->code + " started (" + sim::game_mode_name(static_cast<sim::GameMode>(made->mode)) + ")");      // (the log says the rules: "187" or "highest-score")
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

    TEST_CASE("S3.184b Each Match Of The Timer Is Dealt The Original's Game Or 187 By A Coin Of Its Own: About Half Are 187, What The Last Match Had Does Not Matter (Runs Of Two And Three Of Each Kind Come Up, It Is No Alternation), The Room Says The Rules It Was Dealt, A Map Of Its Rules' List Follows (Nine For The Original's Game, Six For 187, Each As Likely As Any Other Of Its List) And A 187 Match Is Never On ISLANDS") {
        ReplayClock clock;
        ListedWorld w("botmatch-coin-maps");
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-coin", &clock), false, why));
        (void)w.mgr.take_notices();
        w.mgr.enable_bot_matches(0, w.now, 0u, 20261117u);                   // (0 minutes: no timer, only the seed: the matches are the ones that the test asks for)
        std::string deals;                                                   // '1' for 187, '0' for the original's game, in the order of the matches
        std::map<std::string, size_t> maps_in_187, maps_in_score;               // (how often each map was dealt in each rules)
        for (int match = 0; match < 300; ++match) {
            std::string code, reason;
            uint8_t mode = 9;
            ASSERT_TRUE(w.mgr.start_bot_match(w.now, code, reason, &mode));
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(mode <= 1 && s.mode == mode && s.bots_only);        // (what the room says is what was dealt)
            deals += mode == 1 ? '1' : '0';
            ++(mode == 1 ? maps_in_187 : maps_in_score)[s.map];
            ASSERT_TRUE(w.mgr.close_room(code, w.now));                      // (the last one is no longer going: the next one may be made)
            if (match % 100 == 99) w.run(31000);                             // (a closed room is forgotten after its keep time)
        }
        const size_t n187 = static_cast<size_t>(std::count(deals.begin(), deals.end(), '1'));
        ASSERT_MSG(n187 > 120 && n187 < 180, "187 in " + std::to_string(n187) + " of 300");
        size_t longest[2] = {0, 0};
        size_t run = 0;
        size_t after[2] = {0, 0};                                            // the matches that came after one of each kind, and how many of those had the same kind again
        size_t same_after[2] = {0, 0};
        for (size_t i = 0; i < deals.size(); ++i) {
            const size_t kind = deals[i] == '1' ? 1u : 0u;
            run = i > 0 && deals[i] == deals[i - 1] ? run + 1 : 1;
            longest[kind] = std::max(longest[kind], run);
            if (i == 0) continue;
            const size_t before = deals[i - 1] == '1' ? 1u : 0u;
            ++after[before];
            if (deals[i] == deals[i - 1]) ++same_after[before];
        }
        // the maps: the original's game on the nine of its list, 187 on the six of its own (the water of ISLANDS joins no two hills by a walk and 187 has no Swimmer, so no 187 match is made there; the map of the players is
        // in 187 only), and each map of a list as likely as any other of it: the draw is a map of the list for the rules that the coin dealt, never one list for both
        ASSERT_EQ(maps_in_score.size(), std::size(kScoreListMaps));
        ASSERT_EQ(maps_in_187.size(), std::size(k187ListMaps));
        for (const char* name : kScoreListMaps) ASSERT_TRUE(maps_in_score.count(name) == 1);
        for (const char* name : k187ListMaps) ASSERT_TRUE(maps_in_187.count(name) == 1);
        ASSERT_TRUE(maps_in_187.count("ISLANDS.LVL") == 0 && maps_in_score.count("ISLANDS.LVL") == 1);
        ASSERT_TRUE(maps_in_score.count("Fight Island.lvl") == 0);
        ASSERT_TRUE(maps_in_187.count("POPcOrN.lvl") == 0 && maps_in_187.count("HELLRAISER.lvl") == 0 && maps_in_187.count("Bombz Away.lvl") == 0);
        for (const auto* counted : {&maps_in_score, &maps_in_187}) {
            size_t total = 0;
            for (const auto& kv : *counted) total += kv.second;
            const size_t mean = total / counted->size();
            for (const auto& kv : *counted) ASSERT_MSG(kv.second * 10 > mean * 4 && kv.second * 10 < mean * 20, kv.first + " " + std::to_string(kv.second) + " of " + std::to_string(total) + " in " + std::to_string(counted->size()) + " maps");
        }
        ASSERT_MSG(longest[0] >= 3 && longest[1] >= 3, "the longest runs: " + std::to_string(longest[0]) + " and " + std::to_string(longest[1]));      // (two or three of a kind in a row are expected)
        for (size_t kind = 0; kind < 2; ++kind) {                            // (after a match of either kind the next one is a coin again: about half the same, never none)
            ASSERT_MSG(after[kind] > 100 && same_after[kind] * 100 > after[kind] * 35 && same_after[kind] * 100 < after[kind] * 65, "after " + std::to_string(kind) + ": " + std::to_string(same_after[kind]) + " of " + std::to_string(after[kind]));
        }
    } TEST_END();

    TEST_CASE("S3.184c A Match Of Computer Players In 187 Starts On Each Of The Six Maps With Every Seating The Timer Can Make (Two To Four Bots On Their Seats, Free For All And Every Way To Make Teams): No Refusal, The Rules And The Teams Stand On The Referee, And The Bots Look At The Match For A Minute") {
        ReplayClock clock;
        const std::vector<BotSeating> seatings = every_bot_seating();
        ASSERT_EQ(seatings.size(), size_t{14});                              // (two bots on two pairs of seats, free for all; three bots on two sets of seats and four bots: free for all and three ways each)
        for (const char* map : kBotMatchMaps) {
            World w;
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-187-start", &clock), false, why));
            std::vector<std::string> codes;
            for (size_t i = 0; i < seatings.size(); ++i) {
                const std::string code = "B187-" + std::to_string(i);
                const CreateResult made = w.mgr.create_room(bot_seating_spec(code, map, seatings[i], 1), w.now);
                ASSERT_MSG(made.ok, std::string(map) + " seating " + std::to_string(i) + ": " + made.error);
                codes.push_back(code);
            }
            ASSERT_TRUE(run_until(w, [&] {
                for (const std::string& code : codes) {
                    if (w.status(code).state != RoomState::Running) return false;
                }
                return true;
            }, 15000));
            w.run(60000);
            for (size_t i = 0; i < seatings.size(); ++i) {
                const RoomStatus s = w.status(codes[i]);
                const sim::StartTeams& teams = seatings[i].teams;
                ASSERT_MSG(s.state == RoomState::Running && s.turns > 1000 && s.bot_decisions > 0, std::string(map) + " seating " + std::to_string(i) + ": " + s.reason);
                ASSERT_TRUE(static_cast<int>(s.mode) == 1 && s.bots_only && s.bot_controller);
                ASSERT_EQ(s.teams, sim::start_teams_text(teams));
                std::array<uint8_t, sim::MAX_PLAYERS> allies{4, 4, 4, 4};     // (4: none) the pair that was asked for, and with four seats the two that are left are the other team; the third of three plays alone
                if (teams.set) {
                    allies[teams.a] = teams.b;
                    allies[teams.b] = teams.a;
                    uint8_t others[2];
                    size_t found = 0;
                    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                        if ((seatings[i].roster & (1u << seat)) != 0 && seat != teams.a && seat != teams.b && found < 2) others[found++] = seat;
                    }
                    if (found == 2) {
                        allies[others[0]] = others[1];
                        allies[others[1]] = others[0];
                    }
                }
                ASSERT_TRUE(s.allies == allies);
            }
        }
    } TEST_END();

    TEST_CASE("S3.184d A Match Of Computer Players In 187 Is Played To Its End On Each Of The Six Maps (One Seating Each: Four Bots In Each Way To Make Teams And Free For All, Three In Teams, Two): The Brawl Kills (On ISLANDS Nobody Can Be Struck), The Rules End It Before The Room's Own Limit, And The Recording Has The Mode And Plays Out To The Last Hash") {
        ReplayClock clock;
        const std::vector<BotSeating> seatings = every_bot_seating();
        // the seatings of the matches, by index into every_bot_seating(): four bots in each of the three splits (0+1, 0+2, 0+3), three bots with a pair, four bots free for all, three bots on the last seats with a pair
        const size_t chosen[] = {11, 12, 13, 4, 10, 8};
        size_t map_index = 0;
        for (const char* map : kBotMatchMaps) {
            const BotSeating& seating = seatings[chosen[map_index++]];
            World w;
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-187-play", &clock), false, why));
            RoomSpec spec = bot_seating_spec("B187-END", map, seating, 1);
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            ASSERT_TRUE(run_until(w, [&] { return w.status("B187-END").state == RoomState::Running; }, 15000));
            ASSERT_TRUE(run_until(w, [&] { return w.status("B187-END").state != RoomState::Running; }, 24u * 60u * 1000u));
            const RoomStatus s = w.status("B187-END");
            ASSERT_MSG(s.state == RoomState::Finished && s.reason != "everybody left", std::string(map) + ": " + s.reason);
            ASSERT_TRUE(s.ticks > 1200 && s.ticks < 24u * 60u * 20u);        // (the rules ended it: a minute at the least, and well before the room's own limit of 25 minutes)
            ASSERT_TRUE(s.replay_kept && ReplayStore::valid_file_name(s.replay_file));
            const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
            ASSERT_TRUE(f.ok && f.rep.complete && f.rep.head.mode == 1 && f.rep.head.sim_rules == replay::sim_rules_for_mode(1) && f.rep.head.roster == seating.roster);
            ASSERT_TRUE(f.rep.head.teams == seating.teams);
            replay::Outcome played;
            ASSERT_TRUE(plays_out(f.rep, played));
            ASSERT_TRUE(played.complete && played.match_over && played.hashes_checked >= 5);
            uint32_t killed = 0;
            uint32_t ants_left = 0;
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                killed += played.result.stats[seat].enemy_killed;
                ants_left += played.result.ants_left[seat];                  // (kept in 187 only: the original's game leaves it 0)
            }
            ASSERT_MSG(ants_left > 0, std::string(map) + ": ants left " + std::to_string(ants_left));
            const bool islands = std::string(map) == "ISLANDS.LVL";          // (the water joins no two hills by a walk and 187 has no Swimmer: nobody is struck, the clock ends the match, and that is why the timer makes no 187 match there)
            ASSERT_MSG(islands ? killed == 0 : killed > 0, std::string(map) + ": killed " + std::to_string(killed));
        }
    } TEST_END();

    TEST_CASE("S3.184e A Map Of A List That This Server Cannot Play Is Skipped With One Line In The Log, Never A Failed Match: A Server With Neither The Library Maps Nor The Players' Map, One That Has The Players' Map Under A Name That Is Another File Of The Library, One That Took The Map Down, One Whose Map Is Over A Limit Of A Room, And One With None Of The Maps That Makes No Match And Says Why; The Others Stay As Likely As Before, And A Map That Comes Back Is Played Again") {
        ReplayClock clock;
        std::string why;
        const auto skip_lines = [](const std::vector<std::string>& notes, const std::string& map) {
            size_t n = 0;
            for (const std::string& line : notes) n += line.find("bot match: the map " + map + " is skipped (") == 0 ? 1u : 0u;
            return n;
        };
        const auto deal = [&](World& w, int matches, std::map<std::string, size_t>& maps, std::vector<std::string>& notes) {
            for (int match = 0; match < matches; ++match) {
                std::string code, reason;
                uint8_t mode = 9;
                ASSERT_MSG(w.mgr.start_bot_match(w.now, code, reason, &mode), "match " + std::to_string(match) + ": " + reason);      // (never a failed match)
                ++maps[w.status(code).map];
                ASSERT_TRUE(w.mgr.close_room(code, w.now));
                if (match % 100 == 99) w.run(31000);
            }
            for (std::string& line : w.mgr.take_notices()) {
                if (line.find("bot match: ") == 0) notes.push_back(line);       // (what the timer says: the rooms' own notes are another matter)
            }
        };
        {                                                                    // the original's six alone: no community folder, no map of the players (the door of the submissions is off)
            World w;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-skip-bare", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.enable_bot_matches(0, w.now, 0u, 20261118u);
            std::map<std::string, size_t> maps;
            std::vector<std::string> notes;
            deal(w, 200, maps, notes);
            ASSERT_EQ(maps.size(), size_t{6});                               // (the six of the original: every one of them is on a list, ISLANDS in the original's game only)
            for (const char* name : {"POPcOrN.lvl", "HELLRAISER.lvl", "Bombz Away.lvl", "Fight Island.lvl"}) {
                ASSERT_TRUE(maps.count(name) == 0);
                ASSERT_MSG(skip_lines(notes, name) == 1, std::string(name) + ": " + std::to_string(skip_lines(notes, name)) + " lines in " + std::to_string(notes.size()));       // (one line for the whole run, not one for each match)
            }
            for (const char* name : kBotMatchMaps) ASSERT_TRUE(skip_lines(notes, name) == 0);
            ASSERT_EQ(notes.size(), size_t{4});                              // (nothing else was said: no match was refused)
        }
        {                                                                    // none of the maps: no match is made, and the reason is told
            World none(ServerLimits(), temp_dir_for("botmatch-skip-none"));
            ASSERT_TRUE(none.mgr.enable_replays(replay_config("botmatch-skip-none", &clock), false, why));
            std::string code, reason;
            ASSERT_FALSE(none.mgr.start_bot_match(none.now, code, reason));
            ASSERT_TRUE(code.empty() && reason.find("none of the maps") != std::string::npos && none.mgr.list(none.now).empty());
        }
        {                                                                    // a map of the players of that name that a file of the library shadows is not the approved map: skipped; taken down: skipped; over a limit of a room: skipped; back: played
            const std::string shadow = temp_dir_for("botmatch-skip-names-community");
            fs::copy_file(fs::path(maps_dir()) / "TINY.LVL", fs::path(shadow) / "Fight Island.lvl");
            World w(ServerLimits(), maps_dir(), shadow);
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-skip-names", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.enable_bot_matches(0, w.now, 0u, 20261119u);
            w.mgr.mutable_store().set_approved({made_up_fight_island("botmatch-skip-names-maps")});
            std::map<std::string, size_t> maps;
            std::vector<std::string> notes;
            deal(w, 200, maps, notes);
            ASSERT_TRUE(maps.count("Fight Island.lvl") == 0);                // (the file of the library is on the path of that name: the approved map is not the one the store reads)
            ASSERT_TRUE(skip_lines(notes, "Fight Island.lvl") == 1);
            for (const char* name : k187ListMaps) {
                if (std::string(name) != "Fight Island.lvl") ASSERT_TRUE(maps.count(name) == 1);       // (the others of the 187 list are as likely as ever)
            }
        }
        {
            ListedWorld w("botmatch-skip-down");
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-skip-down", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.enable_bot_matches(0, w.now, 0u, 20261120u);
            std::map<std::string, size_t> maps;
            std::vector<std::string> notes;
            deal(w, 100, maps, notes);
            ASSERT_TRUE(maps.count("Fight Island.lvl") == 1 && notes.empty());          // (every map of both lists is there: nothing is said)
            w.mgr.mutable_store().set_approved({});                          // the maintainer takes the map down
            maps.clear();
            deal(w, 100, maps, notes);
            ASSERT_TRUE(maps.count("Fight Island.lvl") == 0 && skip_lines(notes, "Fight Island.lvl") == 1 && notes.size() == 1);
            w.mgr.mutable_store().set_approved({made_up_fight_island("botmatch-skip-down")});      // and approves it again
            deal(w, 100, maps, notes);
            ASSERT_TRUE(maps.count("Fight Island.lvl") == 1 && notes.size() == 1);
            w.mgr.mutable_store().set_approved({made_up_fight_island("botmatch-skip-down", 300, 300)});      // a bigger map under the name: 90,000 cells are over what a room plays online
            maps.clear();
            deal(w, 100, maps, notes);
            ASSERT_TRUE(maps.count("Fight Island.lvl") == 0 && skip_lines(notes, "Fight Island.lvl") == 2 && notes.size() == 2);       // (named again: it was back, and it is gone again)
            ASSERT_TRUE(notes.back().find("too big") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.184f A Match Of Computer Players Starts On Each New Map Of The Lists (POPcOrN, HELLRAISER And Bombz Away In The Original's Game, The Players' Map In 187) With Seatings Of Two, Three And Four Bots In Teams And Free For All, And Plays: The Bots Look At It And The Match Runs For A Minute") {
        ReplayClock clock;
        const std::vector<BotSeating> seatings = every_bot_seating();
        const size_t chosen[] = {1, 4, 10, 12};                              // two bots on the last seats; three with a pair; four free for all; four across the corners
        struct Case {
            const char* map;
            uint8_t mode;
        };
        for (const Case c : {Case{"POPcOrN.lvl", 0}, Case{"HELLRAISER.lvl", 0}, Case{"Bombz Away.lvl", 0}, Case{"Fight Island.lvl", 1}}) {
            ListedWorld w("botmatch-new-maps");
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botmatch-new-play", &clock), false, why));
            std::vector<std::string> codes;
            for (const size_t index : chosen) {
                const std::string code = "BNEW-" + std::to_string(index);
                const CreateResult made = w.mgr.create_room(bot_seating_spec(code, c.map, seatings[index], c.mode), w.now);
                ASSERT_MSG(made.ok, std::string(c.map) + " seating " + std::to_string(index) + ": " + made.error);
                codes.push_back(code);
            }
            ASSERT_TRUE(run_until(w, [&] {
                for (const std::string& code : codes) {
                    if (w.status(code).state != RoomState::Running) return false;
                }
                return true;
            }, 15000));
            w.run(60000);
            for (const std::string& code : codes) {
                const RoomStatus s = w.status(code);
                ASSERT_MSG(s.state == RoomState::Running && s.turns > 1000 && s.bot_decisions > 0, std::string(c.map) + " " + code + ": " + s.reason);
                ASSERT_TRUE(s.mode == c.mode && s.map == c.map && s.bots_only);
            }
        }
    } TEST_END();
}

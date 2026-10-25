// Tests of the list of maps (MapStore::scan, list_json, GET /maps on the public door, docs/SERVER.md "The list of maps") and of the lobby's choice of map: the six originals first, the community folder with
// its maps.json and previews.json (the list says what the files say), the play counts from the database keyed by file name and hash (0 without one), names with spaces and odd characters, a bad json
// file or a file with another hash, the lobby that may choose every map of the store (and nothing that cannot be a file name), and the query/method rules of the door.
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "ants_net/protocol.hpp"
#include "ants_server/online_limits.hpp"
#include "ants_sim/sim_engine.hpp"
#include "big_map_fixtures.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

namespace {

std::string community_dir() { return std::string(ANTS_SOURCE_DIR) + "/Community-Maps"; }

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void copy_map(const std::string& from, const fs::path& dir, const std::string& as) { fs::copy_file(from, dir / as, fs::copy_options::overwrite_existing); }

ctl::JsonValue list_of(const MapStore& store, const std::vector<MapPlays>* plays = nullptr) { return parse_text(store.list_json(plays)); }

const ctl::JsonValue* entry_of(const ctl::JsonValue& list, const std::string& file) {
    const ctl::JsonValue& maps = list.get("maps");
    for (size_t i = 0; i < maps.size(); ++i) {
        if (maps.at(i).get("file").str() == file) return &maps.at(i);
    }
    return nullptr;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n THE LIST OF MAPS (GET /maps) AND THE LOBBY'S CHOICE OF MAP\n=======================================================\n";

    TEST_CASE("ML1.1 The six originals are listed first, in the order of the page, flagged original, with the front page's own pictures and numbers from the file's header") {
        MapStore store(maps_dir());
        ASSERT_EQ(store.scan(), size_t{6});
        const std::vector<std::string> order = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};
        const ctl::JsonValue list = list_of(store);
        ASSERT_EQ(list.get("format").as_int_or(0), int64_t{1});
        ASSERT_EQ(list.get("maps").size(), order.size());
        for (size_t i = 0; i < order.size(); ++i) {
            const ctl::JsonValue& m = list.get("maps").at(i);
            ASSERT_EQ(m.get("file").str(), order[i]);
            ASSERT_TRUE(m.get("original").as_bool_or(false));
            ASSERT_EQ(m.get("plays").as_int_or(-1), int64_t{0});
            ASSERT_TRUE(m.get("width").as_int_or(0) >= 31 && m.get("height").as_int_or(0) >= 31);
            ASSERT_TRUE(m.get("minutes").as_int_or(0) >= 6);
            ASSERT_TRUE(m.get("players").as_int_or(0) >= 2 && m.get("players").as_int_or(0) <= 4);
            std::string stem = order[i].substr(0, order[i].size() - 4);
            for (char& c : stem) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            ASSERT_EQ(m.get("thumb").str(), "/front/preview_" + stem + ".png");                    // (the pictures of web/front/: no second copy)
            ASSERT_TRUE(fs::exists(fs::path(ANTS_SOURCE_DIR) / "web" / "front" / ("preview_" + stem + ".png")));
        }
        ASSERT_EQ(list.get("maps").at(0).get("width").as_int_or(0), int64_t{31});                  // Tiny: 31 x 31, 6 minutes
        ASSERT_EQ(list.get("maps").at(0).get("minutes").as_int_or(0), int64_t{6});
    } TEST_END();

    TEST_CASE("ML1.2 The community folder is listed as maps.json and previews.json say, after the originals, by file name; every map's hash is the file's own") {
        MapStore store(maps_dir(), community_dir());
        std::vector<std::string> notes;
        const size_t n = store.scan(&notes);
        const ctl::JsonValue json = parse_text(slurp(fs::path(community_dir()) / "maps.json"));
        const ctl::JsonValue pictures = parse_text(slurp(fs::path(community_dir()) / "previews.json"));
        ASSERT_EQ(json.get("maps").size(), size_t{534});
        ASSERT_EQ(n, size_t{6} + json.get("maps").size());
        ASSERT_TRUE(notes.empty());                                                                // (nothing is wrong with the repository's own lists)
        const ctl::JsonValue list = list_of(store);
        ASSERT_EQ(list.get("maps").size(), n);
        for (size_t i = 6; i < n; ++i) ASSERT_TRUE(list.get("maps").at(i).get("file").str() > list.get("maps").at(i - 1).get("file").str() || i == 6);
        for (size_t i = 0; i < 6; ++i) ASSERT_TRUE(list.get("maps").at(i).get("original").as_bool_or(false));
        for (size_t i = 0; i < json.get("maps").size(); ++i) {
            const ctl::JsonValue& want = json.get("maps").at(i);
            const std::string file = want.get("file").str();
            const ctl::JsonValue* got = entry_of(list, file);
            ASSERT_TRUE(got != nullptr);
            if (got == nullptr) continue;
            for (const char* key : {"width", "height", "minutes", "players"}) ASSERT_EQ(got->get(key).as_int_or(-1), want.get(key).as_int_or(-2));
            ASSERT_EQ(got->get("description").str(), want.get("description").str());
            ASSERT_EQ(got->get("plays").as_int_or(-1), int64_t{0});
            ASSERT_FALSE(got->has("original"));
            const std::string picture = pictures.get(file).get("picture").str();
            ASSERT_FALSE(picture.empty());
            ASSERT_TRUE(fs::exists(fs::path(community_dir()) / "previews" / picture));              // (a picture that exists)
            ASSERT_EQ(got->get("thumb").str().compare(0, 15, "/maps/previews/"), 0);
            MapEntry entry;
            ASSERT_TRUE(store.find(file, entry));
            ASSERT_EQ(map_hash_text(entry.hash), want.get("hash").str());                          // (maps.json's hash is the hash of the start barrier)
        }
    } TEST_END();

    TEST_CASE("ML1.3 The address of a picture is the file name written as %XX but for letters, digits and - . _ ~ : spaces and ! ' # $ & survive a round trip") {
        MapStore store(maps_dir(), community_dir());
        store.scan();
        const ctl::JsonValue list = list_of(store);
        const ctl::JsonValue* danger = entry_of(list, "!!!!!!!!!!!!!!!! Danger_World!_II!!!.lvl");
        ASSERT_TRUE(danger != nullptr);
        if (danger != nullptr) ASSERT_EQ(danger->get("thumb").str(), "/maps/previews/%21%21%21%21%21%21%21%21%21%21%21%21%21%21%21%21%20Danger_World%21_II%21%21%21.webp");
        size_t checked = 0;
        const ctl::JsonValue& maps = list.get("maps");
        for (size_t i = 0; i < maps.size(); ++i) {
            const std::string thumb = maps.at(i).get("thumb").str();
            if (thumb.compare(0, 15, "/maps/previews/") != 0) continue;
            std::string decoded;
            for (size_t k = 15; k < thumb.size(); ++k) {
                if (thumb[k] == '%') {
                    decoded.push_back(static_cast<char>(std::stoi(thumb.substr(k + 1, 2), nullptr, 16)));
                    k += 2;
                } else {
                    ASSERT_TRUE(std::string("-._~").find(thumb[k]) != std::string::npos || std::isalnum(static_cast<unsigned char>(thumb[k])));
                    decoded.push_back(thumb[k]);
                }
            }
            ASSERT_TRUE(fs::exists(fs::path(community_dir()) / "previews" / decoded));
            ++checked;
        }
        ASSERT_EQ(checked, size_t{534});
    } TEST_END();

    TEST_CASE("ML1.4 Every map of the community folder has the full setup of four seats, a hill and a start marker (so starting ants) for each, and can be played by each of them: no Fatal finding for its rosters, and no world that kills a lone team's ants within a minute") {
        // The owner's rules (Oct 12): a broken map is removed from the repository completely, and so is every map that lacks a hill or starting ants for any of the four seats (the nine 1 v 1 maps,
        // `Fighter Dream 2`, `junk yard` and the two arenas without hills went for that). A map is broken when the loader refuses it for a roster that plays (any Fatal finding), or when a team
        // would start with no ant at all, as `Home Bound`, `K-P` or `QuItErS` did. This case keeps such a map from being added again.
        namespace as = ants::assets;
        const ctl::JsonValue json = parse_text(slurp(fs::path(community_dir()) / "maps.json"));
        ASSERT_TRUE(json.get("maps").size() > 400);
        for (size_t i = 0; i < json.get("maps").size(); ++i) {
            const ctl::JsonValue& row = json.get("maps").at(i);
            const std::string file = row.get("file").str();
            const std::string path = (fs::path(community_dir()) / file).string();
            as::LevelData level;
            as::LevelValidation report;
            const bool loads = level.load_from_file(path, &report);
            if (!loads) std::cout << "\n    " << file << " does not load: " << report.describe();
            ASSERT_TRUE(loads);
            if (!loads) continue;
            std::array<size_t, 4> markers{};
            std::array<size_t, 4> hills{};
            for (const as::AnthillSpawn& sp : level.anthill_spawns) {
                if (sp.team_id < 4) ++markers[sp.team_id];
            }
            for (const as::MapCell& cell : level.layer2_interactive) {
                const int team = as::hill_team_of_id(cell.tile_index);                                // (a hill cell of the file itself: the start markers that stand in for hills on a map without any do not count)
                if (team >= 0) ++hills[static_cast<size_t>(team)];
            }
            uint8_t with_marker = 0;                                                                // (the roster of the teams that have a starting ant)
            bool four_seats = true;
            for (uint8_t t = 0; t < 4; ++t) {
                if (markers[t] > 0) with_marker = static_cast<uint8_t>(with_marker | (1u << t));
                if (markers[t] == 0 || hills[t] == 0) {
                    four_seats = false;
                    std::cout << "\n    " << file << ": seat " << int{t} << " has " << markers[t] << " start marker(s) and " << hills[t] << " hill cell(s)";
                }
            }
            ASSERT_TRUE(four_seats);
            ASSERT_EQ(row.get("players").as_int_or(0), int64_t{4});
            // Class C, a world that kills its own ants (hell2: a lake, every ant drowns by tick 48): each team that has ants, played alone and idle for 1,200 ticks (a minute),
            // must still have an ant and the match must not be over (a lone team never ends it by itself).
            for (uint8_t t = 0; t < 4; ++t) {
                if ((with_marker & (1u << t)) == 0) continue;
                ants::sim::SimulationEngine sim;
                sim.init(level, 12345, static_cast<uint8_t>(1u << t));
                bool over = false;
                for (int tick = 0; tick < 1200 && !over; ++tick) {
                    sim.tick();
                    over = sim.is_match_over();
                }
                size_t alive = 0;
                for (const auto& a : sim.get_world_state().ants) {
                    if (a.player_id == t && a.hp > 0) ++alive;
                }
                if (over || alive == 0) std::cout << "\n    " << file << " team " << int{t} << " alone: match over " << over << ", ants alive after 1,200 ticks " << alive;
                ASSERT_FALSE(over);
                ASSERT_TRUE(alive > 0);
            }
            for (const uint8_t roster : {uint8_t{0x0F}, with_marker}) {
                const as::LevelValidation v = as::LVLParser::check_file(path, roster);
                if (!v.playable) std::cout << "\n    " << file << " roster " << int{roster} << " is not playable: " << v.reason();
                ASSERT_TRUE(v.playable);
                ASSERT_TRUE(v.first_fatal() == nullptr);
            }
        }
    } TEST_END();

    TEST_CASE("ML2.1 Odd file names: spaces and ! # $ & ' ~ are listed, a name that cannot travel, a file that is empty or too big or does not load, and other files are not") {
        TempDir t;
        const fs::path maps = t.path / "maps";
        const fs::path community = t.path / "community";
        fs::create_directories(maps);
        fs::create_directories(community);
        const std::string tiny = maps_dir() + "/TINY.LVL";
        copy_map(tiny, maps, "TINY.LVL");
        copy_map(tiny, community, "A B!#$&'~.lvl");
        copy_map(tiny, community, "UPPER.LVL");
#ifndef _WIN32
        copy_map(tiny, community, "a:b.lvl");                                                       // (a name that cannot travel; it cannot exist on Windows at all: ':' is forbidden there)
#endif
        copy_map(tiny, community, ".hidden.lvl");
        copy_map(tiny, community, "x.txt");
        copy_map(tiny, community, "TINY.LVL");                                                      // (the maps folder wins)
        std::ofstream(community / "empty.lvl").close();
        std::ofstream(community / "junk.lvl") << "this is not a level";
        { std::ofstream big(community / "huge.lvl", std::ios::binary); big << std::string(MapStore::kMaxMapBytes + 1, 'x'); }
        fs::create_directories(community / "dir.lvl");
        MapStore store(maps.string(), community.string());
        ASSERT_EQ(store.scan(), size_t{3});
        const ctl::JsonValue list = list_of(store);
        ASSERT_EQ(list.get("maps").at(0).get("file").str(), std::string("TINY.LVL"));
        ASSERT_TRUE(list.get("maps").at(0).get("original").as_bool_or(false));
        ASSERT_EQ(list.get("maps").at(1).get("file").str(), std::string("A B!#$&'~.lvl"));       // (not in maps.json: described by its own header)
        ASSERT_EQ(list.get("maps").at(1).get("width").as_int_or(0), int64_t{31});
        ASSERT_FALSE(list.get("maps").at(1).has("thumb"));                                         // (no picture: the key is left out)
        ASSERT_EQ(list.get("maps").at(2).get("file").str(), std::string("UPPER.LVL"));
        ASSERT_EQ(store.listed_name("upper.lvl"), std::string("UPPER.LVL"));
        ASSERT_EQ(store.listed_name("UPPER.LVL"), std::string("UPPER.LVL"));
        ASSERT_EQ(store.listed_name("nothing.lvl"), std::string());
    } TEST_END();

    TEST_CASE("ML2.2 A bad maps.json or previews.json is a note, not a failure: the maps are listed from their files; a map whose hash differs from maps.json's is described by its own header") {
        TempDir t;
        const fs::path community = t.path / "community";
        fs::create_directories(community);
        copy_map(maps_dir() + "/SMALL.LVL", community, "Mine.lvl");
        std::ofstream(community / "maps.json") << "{ not json";
        std::ofstream(community / "previews.json") << "[1,2";
        MapStore store(t.path.string(), community.string());
        std::vector<std::string> notes;
        ASSERT_EQ(store.scan(&notes), size_t{1});
        ASSERT_EQ(notes.size(), size_t{2});
        ASSERT_EQ(list_of(store).get("maps").at(0).get("width").as_int_or(0), int64_t{40});
        std::ofstream(community / "maps.json") << R"({"format":1,"maps":[{"file":"Mine.lvl","hash":"0000000000000001","width":99,"height":99,"minutes":1,"players":1,"description":"stale"}]})";
        std::ofstream(community / "previews.json") << R"({"Mine.lvl":{"hash":"0000000000000001","picture":"Mine.webp"}})";
        notes.clear();
        MapStore other(t.path.string(), community.string());
        ASSERT_EQ(other.scan(&notes), size_t{1});
        ASSERT_EQ(notes.size(), size_t{1});
        const ctl::JsonValue m = list_of(other).get("maps").at(0);
        ASSERT_EQ(m.get("width").as_int_or(0), int64_t{40});                                       // (the file, not the stale list)
        ASSERT_FALSE(m.has("thumb"));                                                              // (a picture of another version is not shown)
        MapEntry entry;
        ASSERT_TRUE(other.find("Mine.lvl", entry));
        std::ofstream(community / "previews.json") << R"({"Mine.lvl":{"hash":")" << map_hash_text(entry.hash) << R"(","picture":"../x.webp"}})";   // (a picture name must be a plain name)
        MapStore evil(t.path.string(), community.string());
        evil.scan();
        ASSERT_FALSE(list_of(evil).get("maps").at(0).has("thumb"));
    } TEST_END();

    TEST_CASE("ML2.3 A community folder that cannot be looked into (here: a plain file where the folder should be, the error a folder without search rights gives) is a note or nothing, never an exception; the maps folder is still listed") {
        TempDir t;
        const fs::path fake = t.path / "community";
        std::ofstream(fake) << "not a folder";
        MapStore store(maps_dir(), fake.string());
        std::vector<std::string> notes;
        size_t n = 0;
        bool threw = false;
        try { n = store.scan(&notes); } catch (...) { threw = true; }
        ASSERT_FALSE(threw);
        ASSERT_EQ(n, size_t{6});
        ASSERT_TRUE(notes.size() <= 2);                                                            // (a path that cannot be looked at is a note, or simply not there: never an exception)
    } TEST_END();

    TEST_CASE("ML4.3 The lobby's choice is answered from the scanned list, without touching the disk: a listed map is accepted after its file is gone, a file that came after the scan is refused") {
        TempDir t;
        const fs::path dir = t.path / "maps";
        fs::create_directories(dir);
        copy_map(maps_dir() + "/TINY.LVL", dir, "TINY.LVL");
        copy_map(maps_dir() + "/SMALL.LVL", dir, "Mine One.lvl");
        RoomManager mgr{MapStore(dir.string())};                                                    // (a manager made by hand lists its folder itself)
        const LobbyServices s = mgr.lobby_services();
        fs::remove(dir / "Mine One.lvl");                                                          // gone from the disk: the answer comes from memory
        ASSERT_EQ(s.choose_map("Mine One.lvl"), std::string("Mine One.lvl"));
        ASSERT_EQ(s.choose_map("mine one.LVL"), std::string("Mine One.lvl"));
        copy_map(maps_dir() + "/MEDIUM.LVL", dir, "Later.lvl");                                    // there on the disk, not in the list
        ASSERT_EQ(s.choose_map("Later.lvl"), std::string());
        ASSERT_EQ(s.choose_map("NOT-HERE.LVL"), std::string());
        MapEntry entry;
        assets::LevelData level;
        ASSERT_FALSE(s.load_map("Mine One.lvl", entry, level));                                    // (loading is where the disk is read)
    } TEST_END();

    TEST_CASE("ML3.1 plays: 0 without a database; with one, the rows of map_plays by file name AND hash (another hash or name is 0)") {
        MapStore store(maps_dir());
        store.scan();
        MapEntry tiny;
        ASSERT_TRUE(store.find("TINY.LVL", tiny));
        std::vector<MapPlays> rows;
        MapPlays a;
        a.map = "TINY.LVL";
        a.hash = map_hash_text(tiny.hash);
        a.plays = 7;
        rows.push_back(a);
        MapPlays stale = a;
        stale.map = "SMALL.LVL";
        stale.hash = "0000000000000001";                                                           // (a version of the map that is not the file's)
        stale.plays = 5;
        rows.push_back(stale);
        MapPlays gone = a;
        gone.map = "NOT-HERE.LVL";
        gone.plays = 9;
        rows.push_back(gone);
        const ctl::JsonValue list = list_of(store, &rows);
        ASSERT_EQ(entry_of(list, "TINY.LVL")->get("plays").as_int_or(-1), int64_t{7});
        ASSERT_EQ(entry_of(list, "SMALL.LVL")->get("plays").as_int_or(-1), int64_t{0});
        ASSERT_EQ(list.get("maps").size(), size_t{6});                                             // (a map that is not here is not listed)
        ASSERT_EQ(entry_of(list_of(store), "TINY.LVL")->get("plays").as_int_or(-1), int64_t{0});
    } TEST_END();

    TEST_CASE("ML3.2 GET /maps on the public door: the same list as maps.json and the DB's counts; 0 when the server keeps no history; a query, a POST or a path below it is a 404") {
        TempDir t;
        db::Database database;
        ASSERT_TRUE(open_server_db(database, (t.path / "ants.db").string()));
        MapStore scanned(maps_dir(), community_dir());
        ASSERT_EQ(scanned.scan(), size_t{540});
        RoomManager mgr{std::move(scanned)};
        const auto get = [&](const char* method, const std::string& path, const std::string& query = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.query = query;
            return handle_public_replays(mgr, rq);
        };
        ctl::HttpResponse r = get("GET", "/maps");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.content_type, std::string("application/json"));
        ASSERT_EQ(entry_of(parse_text(r.body), "TREASURE.LVL")->get("plays").as_int_or(-1), int64_t{0});     // (no history: every count is 0, the list is there)
        ASSERT_EQ(get("GET", "/maps", "x=1").status, 404);
        ASSERT_EQ(get("POST", "/maps").status, 404);
        ASSERT_EQ(get("GET", "/maps/").status, 404);
        ASSERT_EQ(get("GET", "/maps/TINY.LVL").status, 404);
        std::string why;
        ASSERT_TRUE(mgr.enable_history(database, why));
        MapEntry treasure;
        ASSERT_TRUE(mgr.store().find("TREASURE.LVL", treasure));
        HistoryRecord rec;
        rec.id = id_at(kT0, "TREASURE");
        rec.ended_s = kT0;
        rec.map = "TREASURE.LVL";
        rec.map_hash = treasure.hash;
        rec.game = "v0.12.0-test";
        rec.turns = 1200;
        rec.finished = true;
        rec.mode = "highest-score";
        rec.format = "1v1";
        rec.computers_only = false;
        for (uint8_t i = 0; i < 2; ++i) {
            HistorySeat s;
            s.seat = i;
            s.name = i == 0 ? "Ann" : "Bob";
            rec.seats.push_back(s);
            HistoryRow row;
            row.seats.push_back(i);
            row.winner = i == 0;
            rec.rows.push_back(row);
        }
        ASSERT_TRUE(mgr.history_store()->put(rec, why));
        rec.id = id_at(kT0 + 60, "TREASURE");
        ASSERT_TRUE(mgr.history_store()->put(rec, why));
        r = get("GET", "/maps");
        ASSERT_EQ(r.status, 200);
        const ctl::JsonValue list = parse_text(r.body);
        ASSERT_EQ(entry_of(list, "TREASURE.LVL")->get("plays").as_int_or(-1), int64_t{2});
        ASSERT_EQ(entry_of(list, "TINY.LVL")->get("plays").as_int_or(-1), int64_t{0});
        ASSERT_EQ(list.get("maps").size(), size_t{540});
        mgr.disable_history();
    } TEST_END();

    TEST_CASE("ML4.1 The lobby may choose any map of the store (more than the 64 of --demo-maps, the community folder too, any letter case); the six originals stay as they were; a name that cannot be a file is refused") {
        ServerLimits limits;
        limits.demo_map = "TREASURE.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL"};
        RoomManager mgr{MapStore(maps_dir(), community_dir()), limits};
        const LobbyServices s = mgr.lobby_services();
        ASSERT_EQ(s.choose_map("TINY.LVL"), std::string("TINY.LVL"));
        ASSERT_EQ(s.choose_map("tiny.lvl"), std::string("TINY.LVL"));                              // (as before: the name of the list, in any case)
        ASSERT_EQ(s.choose_map("TREASURE.LVL"), std::string("TREASURE.LVL"));
        ASSERT_EQ(s.choose_map("MEDIUM.LVL"), std::string("MEDIUM.LVL"));                          // (in the maps folder, not in --demo-maps)
        const ctl::JsonValue json = parse_text(slurp(fs::path(community_dir()) / "maps.json"));
        size_t chosen = 0;
        for (size_t i = 0; i < json.get("maps").size(); ++i) {
            const std::string file = json.get("maps").at(i).get("file").str();
            ASSERT_EQ(s.choose_map(file), file);
            ++chosen;
        }
        ASSERT_TRUE(chosen > 64);
        ASSERT_EQ(s.choose_map(""), std::string());
        ASSERT_EQ(s.choose_map("NOT-HERE.LVL"), std::string());
        ASSERT_EQ(s.choose_map("../Community-Maps/!!!BLOCKADES!!!.lvl"), std::string());
        ASSERT_EQ(s.choose_map("sub/TINY.LVL"), std::string());
        ASSERT_EQ(s.choose_map("a:b.lvl"), std::string());
        ASSERT_EQ(s.choose_map("TINY.LVL\n"), std::string());
        ASSERT_EQ(s.choose_map("TINY"), std::string());
        // a server without a community folder offers its maps folder only
        RoomManager plain{MapStore(maps_dir()), limits};
        ASSERT_EQ(plain.lobby_services().choose_map("!!!BLOCKADES!!!.lvl"), std::string());
        ASSERT_EQ(plain.lobby_services().choose_map("MEDIUM.LVL"), std::string("MEDIUM.LVL"));
    } TEST_END();

    TEST_CASE("ML4.2 A community map loads for a room through the lobby's services, from the community folder, with the hash of the file") {
        RoomManager mgr{MapStore(maps_dir(), community_dir())};
        const LobbyServices s = mgr.lobby_services();
        MapEntry entry;
        assets::LevelData level;
        ASSERT_TRUE(s.load_map("!!!BLOCKADES!!!.lvl", entry, level));
        ASSERT_TRUE(fs::path(entry.path).lexically_normal() == (fs::path(community_dir()) / "!!!BLOCKADES!!!.lvl").lexically_normal());      // (paths, not strings: the separators differ on Windows)
        ASSERT_TRUE(level.width > 0 && level.height > 0);
        ASSERT_FALSE(s.load_map("../x.lvl", entry, level));
    } TEST_END();

    TEST_CASE("ML4.4 The online size limit (OnlineLimits::kMaxCells = 65,536 cells): the lobby may not choose a bigger approved or test map, a room does not load one, the list still shows it (\"online\": false) and find() still reaches its file; 256 x 256 is the largest map a room plays") {
        TempDir dir;
        const auto keep = [&](const std::string& name, uint32_t rows, uint32_t columns) {
            const std::string bytes = grown_tiny(rows, columns);
            const fs::path path = dir.path / (name + ".bin");
            { std::ofstream out(path, std::ios::binary); out << bytes; }
            ApprovedMap a;
            a.name = name;
            a.path = path.string();
            a.size = bytes.size();
            a.width = columns;
            a.height = rows;
            a.minutes = 10;
            a.players = 4;
            a.maker = "Mia";
            a.kind = "extended";
            net::hash_file(a.path, a.hash);
            return a;
        };
        ServerLimits limits;
        limits.demo_map = "TREASURE.LVL";
        RoomManager mgr{MapStore(maps_dir(), community_dir()), limits};
        mgr.mutable_store().scan();
        std::vector<ApprovedMap> approved;
        approved.push_back(keep("Fits 256.lvl", 256, 256));
        approved.push_back(keep("Wide 410.lvl", 160, 410));            // 65,600 cells
        approved.push_back(keep("Edge 65536.lvl", 128, 512));          // exactly 65,536 cells
        ASSERT_TRUE(approved[0].size > 700000 && approved[1].size > 700000);
        mgr.mutable_store().set_approved(approved);
        std::vector<ApprovedMap> tests;
        tests.push_back(keep("Draft Big ~test-0badf00d.lvl", 300, 300));
        tests.push_back(keep("Draft Fits ~test-0badf00e.lvl", 100, 100));
        mgr.mutable_store().set_test_maps(tests);
        const MapStore& store = mgr.store();
        const LobbyServices s = mgr.lobby_services();
        ASSERT_EQ(s.choose_map("Fits 256.lvl"), std::string("Fits 256.lvl"));
        ASSERT_EQ(s.choose_map("fits 256.LVL"), std::string("Fits 256.lvl"));
        ASSERT_EQ(s.choose_map("Edge 65536.lvl"), std::string("Edge 65536.lvl"));       // (exactly the limit is in)
        ASSERT_EQ(s.choose_map("Wide 410.lvl"), std::string());                        // (64 cells over is out, in any letter case)
        ASSERT_EQ(s.choose_map("WIDE 410.lvl"), std::string());
        ASSERT_EQ(s.choose_map("Draft Big ~test-0badf00d.lvl"), std::string());
        ASSERT_EQ(s.choose_map("Draft Fits ~test-0badf00e.lvl"), std::string("Draft Fits ~test-0badf00e.lvl"));
        ASSERT_EQ(s.map_refusal("Wide 410.lvl"), std::string(net::kNoticeMapTooBig));
        ASSERT_EQ(s.map_refusal("wide 410.lvl"), std::string(net::kNoticeMapTooBig));
        ASSERT_EQ(s.map_refusal("Draft Big ~test-0badf00d.lvl"), std::string(net::kNoticeMapTooBig));
        for (const char* fine : {"Fits 256.lvl", "Edge 65536.lvl", "TINY.LVL", "NOT-HERE.LVL", "", "Draft Fits ~test-0badf00e.lvl"}) ASSERT_EQ(s.map_refusal(fine), std::string());
        ASSERT_TRUE(store.too_big_online("Wide 410.lvl") && store.too_big_online("WIDE 410.LVL") && store.too_big_online("Draft Big ~test-0badf00d.lvl"));
        ASSERT_FALSE(store.too_big_online("Fits 256.lvl") || store.too_big_online("Edge 65536.lvl") || store.too_big_online("TINY.LVL") || store.too_big_online("NOT-HERE.LVL"));
        // a room does not load one (the START finds "map lost" in the lobby; a room of the control interface is a 422), the file is still found
        MapEntry entry;
        assets::LevelData level;
        ASSERT_TRUE(s.load_map("Fits 256.lvl", entry, level));
        ASSERT_TRUE(s.load_map("Edge 65536.lvl", entry, level));
        ASSERT_TRUE(store.find("Wide 410.lvl", entry));
        ASSERT_FALSE(s.load_map("Wide 410.lvl", entry, level));
        ASSERT_FALSE(s.load_map("Draft Big ~test-0badf00d.lvl", entry, level));
        RoomSpec spec;
        spec.code = "BIGROOM-1";
        spec.map = "Wide 410.lvl";
        spec.players = 2;
        CreateResult made = mgr.create_room(spec, 1000);
        ASSERT_TRUE(!made.ok && made.http_status == 422 && made.error.find("too big") != std::string::npos);
        spec.map = "Fits 256.lvl";
        ASSERT_TRUE(mgr.create_room(spec, 1000).ok);
        // the cells that a match on each costs
        ASSERT_EQ(store.cells_of("Fits 256.lvl"), uint64_t{65536});
        ASSERT_EQ(store.cells_of("Wide 410.lvl"), uint64_t{65600});
        ASSERT_EQ(store.cells_of("Draft Big ~test-0badf00d.lvl"), uint64_t{90000});
        ASSERT_EQ(store.cells_of("TINY.LVL"), uint64_t{961});
        ASSERT_EQ(store.cells_of("NOT-HERE.LVL"), uint64_t{0});
        // the list: the old page's list never has an Extended map; the newest one has them all, and says which ones a room does not play
        const ctl::JsonValue newest = parse_text(store.list_json(nullptr, true));
        const ctl::JsonValue* fits = entry_of(newest, "Fits 256.lvl");
        const ctl::JsonValue* wide = entry_of(newest, "Wide 410.lvl");
        ASSERT_TRUE(fits != nullptr && wide != nullptr);
        ASSERT_TRUE(fits->get("online").is_null() && wide->get("online").as_bool_or(true) == false);
        ASSERT_TRUE(entry_of(newest, "TINY.LVL") != nullptr && entry_of(newest, "TINY.LVL")->get("online").is_null());
        ASSERT_TRUE(entry_of(parse_text(store.list_json(nullptr, false)), "Fits 256.lvl") == nullptr && store.list_json(nullptr, false).find("online") == std::string::npos);
        ASSERT_TRUE(store.list_json(nullptr, true).find("0badf00d") == std::string::npos);     // (a test map is on no list)
        // the constant and its sentence
        ASSERT_TRUE(OnlineLimits::weight(1) == 1 && OnlineLimits::weight(10000) == 1 && OnlineLimits::weight(10001) == 2 && OnlineLimits::weight(65536) == 7 && OnlineLimits::weight(262144) == 27);        // the places a match takes
        ASSERT_EQ(OnlineLimits::weight(0), 1u);                                                      // (never less than a place)
        ASSERT_TRUE(OnlineLimits::kMaxCells == 65536u && OnlineLimits::kCellsPerPlace == 10000u && OnlineLimits::allows(65536) && !OnlineLimits::allows(65537));
        // the public door sends a big map's file whole (HttpResponse::file) and a picture or an error as every other answer
        {
            ctl::HttpRequest request;
            request.method = "GET";
            request.path = "/maps/files/Wide%20410.lvl";
            ctl::HttpResponse r = handle_public_replays(mgr, request);
            ASSERT_TRUE(r.status == 200 && r.file && r.body.size() == approved[1].size && r.content_type == "application/octet-stream");
            request.path = "/maps/previews/Wide%20410.png";
            r = handle_public_replays(mgr, request);
            ASSERT_TRUE(r.status == 200 && !r.file && r.body.compare(0, 4, "\x89PNG") == 0);
            request.path = "/maps/files/Draft%20Big%20~test-0badf00d.lvl";                          // (a test map's file too: unlisted, not unreachable)
            r = handle_public_replays(mgr, request);
            ASSERT_TRUE(r.status == 200 && r.file && r.body.size() == tests[0].size);
            request.path = "/maps/files/Nobody.lvl";
            r = handle_public_replays(mgr, request);
            ASSERT_TRUE(r.status == 404 && !r.file);
            {   // a file that grew after the store looked at it (the list says 100 bytes, the disk has one byte over what the store keeps) is not read whole on the game thread: a 404
                const fs::path grown = dir.path / "grown.bin";
                { std::ofstream out(grown, std::ios::binary); out << std::string(static_cast<size_t>(MapStore::kMaxMapBytes) + 1u, 'x'); }
                ApprovedMap g = approved[0];
                g.name = "Grew 256.lvl";
                g.path = grown.string();
                g.size = 100;
                std::vector<ApprovedMap> more = approved;
                more.push_back(g);
                mgr.mutable_store().set_approved(more);
                request.path = "/maps/files/Grew%20256.lvl";
                std::error_code size_error;
                const unsigned long long on_disk = static_cast<unsigned long long>(fs::file_size(grown, size_error));
                r = handle_public_replays(mgr, request);
                if (on_disk != MapStore::kMaxMapBytes + 1u || r.status != 404 || r.file) std::cout << "\n      [grew] on the disk " << on_disk << " bytes, status " << r.status << ", file " << r.file << ", body " << r.body.size() << " bytes: " << r.body.substr(0, 80) << "\n";
                ASSERT_TRUE(on_disk == MapStore::kMaxMapBytes + 1u);
                ASSERT_TRUE(r.status == 404 && !r.file);
                // (and a file of exactly the largest size the store keeps is sent whole)
                { std::ofstream out(grown, std::ios::binary | std::ios::trunc); out << std::string(static_cast<size_t>(MapStore::kMaxMapBytes), 'x'); }
                g.size = MapStore::kMaxMapBytes;
                more.back() = g;
                mgr.mutable_store().set_approved(more);
                request.path = "/maps/files/Grew%20256.lvl";
                r = handle_public_replays(mgr, request);
                ASSERT_TRUE(r.status == 200 && r.file && r.body.size() == MapStore::kMaxMapBytes);
                mgr.mutable_store().set_approved(approved);
            }
        }
        ASSERT_TRUE(std::string(net::kNoticeMapTooBig).find("65,536 cells") != std::string::npos && std::string(net::kNoticeMapTooBig).size() <= net::kMaxChatChars);
    } TEST_END();

    TEST_CASE("ML4.5 The start ants of a map: counted from the file (the start markers of the four colors added up), 300 is not heavy and 301 is; the 540 maps of the library start at most 248, so none is marked; the list says \"ants\" and \"heavy\" of a heavy map only (the newest list); and nothing is refused for them: the lobby may choose a map of 6,000 ants") {
        MapStore store(maps_dir(), community_dir());
        store.scan();
        uint64_t most = 0;
        size_t heavy = 0;
        size_t originals = 0;
        for (const MapListing& m : store.listing()) {
            most = std::max(most, m.start_ants);
            heavy += OnlineLimits::heavy(m.start_ants) ? 1u : 0u;
            if (m.original) {
                ++originals;
                ASSERT_TRUE(m.start_ants >= 12 && m.start_ants <= 32);                         // (TINY starts 12, ISLANDS 32)
            }
            ASSERT_TRUE(m.start_ants > 0);                                            // (every map of the library has start markers, so a count of 0 would mean a file that did not load)
        }
        ASSERT_EQ(store.listing().size(), size_t{540});
        ASSERT_EQ(originals, size_t{6});
        ASSERT_EQ(heavy, size_t{0});                                                           // (no map of the library is heavy today)
        ASSERT_EQ(most, uint64_t{248});
        ASSERT_TRUE(store.list_json(nullptr, true).find("\"heavy\"") == std::string::npos && store.list_json(nullptr, true).find("\"ants\"") == std::string::npos);
        ASSERT_TRUE(OnlineLimits::kHeavyStartAnts == 300u && !OnlineLimits::heavy(300) && OnlineLimits::heavy(301) && !OnlineLimits::heavy(0));
        TempDir dir;
        const auto keep = [&](const std::string& name, const std::array<uint32_t, 4>& per_color, const char* kind = "classic") {
            const std::string bytes = tiny_with_ants(per_color);
            const fs::path path = dir.path / (name + ".bin");
            { std::ofstream out(path, std::ios::binary); out << bytes; }
            ApprovedMap a;
            a.name = name;
            a.path = path.string();
            a.size = bytes.size();
            a.width = 31;
            a.height = 31;
            a.minutes = 10;
            a.players = 4;
            a.maker = "Mia";
            a.kind = kind;
            a.start_ants = 5;                                                                 // (a number that the caller gives is not believed: the store counts the file)
            net::hash_file(a.path, a.hash);
            return a;
        };
        store.set_approved({keep("Ants 300.lvl", {75, 75, 75, 75}), keep("Ants 301.lvl", {75, 75, 75, 76}), keep("Ext 300.lvl", {75, 75, 75, 75}, "extended"), keep("Ext 301.lvl", {75, 75, 75, 76}, "extended"),
                            keep("Uneven 6000.lvl", {1, 1, 1, 5997}), keep("Huge 6000.lvl", {1500, 1500, 1500, 1500}, "extended")});
        const ctl::JsonValue newest = parse_text(store.list_json(nullptr, true));
        for (const auto& [file, ants] : std::vector<std::pair<const char*, int64_t>>{{"Ants 301.lvl", 301}, {"Ext 301.lvl", 301}, {"Uneven 6000.lvl", 6000}, {"Huge 6000.lvl", 6000}}) {
            const ctl::JsonValue* e = entry_of(newest, file);
            ASSERT_TRUE(e != nullptr);
            ASSERT_TRUE(e->get("heavy").as_bool_or(false) && e->get("ants").as_int_or(0) == ants);      // (the four colors are added up, whatever their share)
        }
        for (const char* fine : {"Ants 300.lvl", "Ext 300.lvl", "TINY.LVL", "TREASURE.LVL"}) {
            const ctl::JsonValue* e = entry_of(newest, fine);
            ASSERT_TRUE(e != nullptr && e->get("heavy").is_null() && e->get("ants").is_null());
        }
        ASSERT_TRUE(entry_of(newest, "Ants 301.lvl")->get("online").is_null() && entry_of(newest, "Huge 6000.lvl")->get("online").is_null());       // (no refusal: "online" is about the size of the grid only)
        ASSERT_TRUE(store.list_json(nullptr, false).find("heavy") == std::string::npos);      // (the old list has the same maps and no new keys)
        // nothing is refused: every one of them is chosen as any other, in any letter case
        for (const char* name : {"Ants 300.lvl", "Ants 301.lvl", "Ext 301.lvl", "ext 301.LVL", "Uneven 6000.lvl", "Huge 6000.lvl", "huge 6000.LVL"}) {
            ASSERT_TRUE(!store.listed_name(name).empty());
            ASSERT_FALSE(store.too_big_online(name));
        }
        MapEntry entry;
        ASSERT_TRUE(store.find("Huge 6000.lvl", entry));
        // a take-down: the map that is gone is not listed
        store.set_approved({keep("Ants 300.lvl", {75, 75, 75, 75})});
        ASSERT_TRUE(entry_of(parse_text(store.list_json(nullptr, true)), "Ants 301.lvl") == nullptr);
        {   // the same count for a map of the community folder that maps.json describes as for one that it does not (both are loaded)
            TempDir folder;
            fs::create_directories(folder.path / "maps");
            fs::create_directories(folder.path / "players");
            const std::string heavy_map = tiny_with_ants({100, 100, 100, 100});
            { std::ofstream out(folder.path / "maps" / "TINY.LVL", std::ios::binary); out << heavy_map; }
            { std::ofstream out(folder.path / "players" / "Plain.lvl", std::ios::binary); out << heavy_map; }
            MapStore own((folder.path / "maps").string(), (folder.path / "players").string());
            own.scan();
            const ctl::JsonValue shown = parse_text(own.list_json(nullptr, true));
            ASSERT_TRUE(entry_of(shown, "TINY.LVL")->get("heavy").as_bool_or(false) && entry_of(shown, "TINY.LVL")->get("ants").as_int_or(0) == 400);        // (an original is counted like any map: the list says what the file holds)
            ASSERT_TRUE(entry_of(shown, "Plain.lvl")->get("heavy").as_bool_or(false) && entry_of(shown, "Plain.lvl")->get("ants").as_int_or(0) == 400);
            ASSERT_EQ(own.listed_name("Plain.lvl"), std::string("Plain.lvl"));
            ASSERT_EQ(own.listed_name("TINY.LVL"), std::string("TINY.LVL"));
        }
    } TEST_END();

    TEST_CASE("ML4.6 The count of an approved map's start ants is kept by path, size and hash: the same path with new bytes is counted again (other size, and the same size with other bytes), a file that is unchanged keeps its count (the store does not load it again), a file that the store no longer holds is forgotten") {
        MapStore store(maps_dir(), community_dir());
        store.scan();
        TempDir dir;
        const auto put = [&](const std::string& bytes) {                                   // (the file at the one path, and the approved map that describes it)
            const fs::path path = dir.path / "kept.bin";
            { std::ofstream out(path, std::ios::binary); out << bytes; }
            ApprovedMap a;
            a.name = "Kept.lvl";
            a.path = path.string();
            a.size = bytes.size();
            a.width = 31;
            a.height = 31;
            a.minutes = 10;
            a.players = 4;
            a.maker = "Mia";
            a.kind = "classic";
            net::hash_file(a.path, a.hash);
            return a;
        };
        const auto ants = [&]() { const ctl::JsonValue* e = entry_of(parse_text(store.list_json(nullptr, true)), "Kept.lvl"); return e == nullptr ? int64_t{-1} : e->get("ants").as_int_or(0); };       // (0: not heavy, no count in the list)
        const std::string first = tiny_with_ants({75, 75, 75, 76});                     // 301
        const std::string second = tiny_with_ants({75, 75, 75, 77});                    // 302: another size
        const std::string third = with_one_marker_made_a_plant(second);                 // 301: the size of the second, other bytes
        ASSERT_TRUE(!first.empty() && !second.empty() && first.size() != second.size() && third.size() == second.size() && third != second);
        store.set_approved({put(first)});
        ASSERT_EQ(ants(), int64_t{301});
        store.set_approved({put(second)});
        ASSERT_EQ(ants(), int64_t{302});                                                    // (the same path, another size)
        const ApprovedMap same_size = put(third);
        store.set_approved({same_size});
        ASSERT_EQ(ants(), int64_t{301});                                                    // (the same path and size, another hash)
        fs::remove(dir.path / "kept.bin");                                                  // (an unchanged file is not loaded again: its count is kept, though the file is gone)
        store.set_approved({same_size});
        ASSERT_EQ(ants(), int64_t{301});
        store.set_approved({});                                                             // (the file is no longer held: forgotten, so a file that comes back is counted from the disk, where there is none)
        ASSERT_EQ(ants(), int64_t{-1});
        store.set_approved({same_size});
        ASSERT_EQ(ants(), int64_t{0});
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

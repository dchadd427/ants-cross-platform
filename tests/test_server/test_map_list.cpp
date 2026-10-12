// Tests of the list of maps (MapStore::scan, list_json, GET /maps on the public door, docs/SERVER.md "The list of maps") and of the lobby's choice of map: the six originals first, the community folder with
// its maps.json and previews.json (the list says what the files say), the play counts from the database keyed by file name and hash (0 without one), names with spaces and odd characters, a bad json
// file or a file with another hash, the lobby that may choose every map of the store (and nothing that cannot be a file name), and the query/method rules of the door.
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "ants_sim/sim_engine.hpp"
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

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

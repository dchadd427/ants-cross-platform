// Tests of the game mode "187" (include/ants_sim/game_mode.hpp, docs/GAMEPLAY.md "187"): what a 187 match starts with on every shipped map, what it takes off the map,
// that the kills are the score, that the match ends when one side is left whatever the scores are, and that every other match plays exactly as before.
//   1.x  the mode and the state hash
//   2.x  the start: the map's own ants and eggs, no food and no power-ups, on every shipped map and for every roster
//   3.x  the end rule and the ranking (kills, the last one standing, ties, alliances, the clock)
//   4.x  the eggs are lives and the free hatch costs no kills; the original's own ranking (a mode that is not set changes nothing)
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/start_teams.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include "ants_test_paths.hpp"

using namespace ants::sim;
using ants::assets::LevelData;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_VOID(cond) do { ++g_assert_count; if (!(cond)) { std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; ++g_test_failures; return; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

namespace {

const char* const kMaps[] = {"TINY", "SMALL", "MEDIUM", "TREASURE", "GAUNTLET", "ISLANDS"};

LevelData load_map(const std::string& name) {
    LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL")) std::cout << "  (cannot load " << name << ")\n";
    return level;
}

std::vector<AntSnapshot> ants_of(const SimulationEngine& e, uint8_t team) {
    std::vector<AntSnapshot> out;
    for (const AntSnapshot& a : e.get_world_state().ants) {
        if (a.player_id == team && a.hp > 0) out.push_back(a);
    }
    return out;
}

// A small world for the rules: a 40 x 40 test map with the given rules, `per_team` combat ants for each of the given teams in a row, no eggs for anybody
SimulationEngine small_world(GameMode mode, std::initializer_list<uint8_t> teams, uint32_t per_team = 3, uint32_t ms = 720000) {
    SimulationEngine e;
    e.set_game_mode(mode);
    e.init_test_world(40, 40, 11, ms);
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) e.set_player_eggs(p, 0);
    for (uint8_t t : teams) {
        for (uint32_t i = 0; i < per_team; ++i) e.spawn_unit(t, AntType::Combat, TileCoord{4 + 10 * static_cast<int32_t>(t) + 2 * static_cast<int32_t>(i), 6});
    }
    return e;
}

void run_ticks(SimulationEngine& e, int n) {
    for (int i = 0; i < n && !e.is_match_over(); ++i) e.tick();
}

// team `killer` kills an ant of team `victim` in melee: a pair of fresh ants side by side (the contact needs the two to touch), the victim with one hit
// point left; the flight and the death clip run out. The killer stays as one more ant of its team.
void kill_pair(SimulationEngine& e, uint8_t killer, uint8_t victim) {
    static int row = 12;
    row += 3;
    const uint32_t a = e.spawn_unit(killer, AntType::Combat, TileCoord{30, row});
    const uint32_t b = e.spawn_unit(victim, AntType::Combat, TileCoord{31, row});
    e.get_unit(b).hp = 1;
    e.execute_melee_attack(a, b);
    for (int k = 0; k < 80; ++k) e.tick();
    ASSERT_VOID(e.get_unit(b).removed);
}

}  // namespace

int main() {
    std::cout << "=== Game mode 187 ===\n";

    TEST_CASE("1.1 the mode is HighestScore until it is set, a re-init keeps it, the numbers and names are the wire's") {
        SimulationEngine e;
        ASSERT_TRUE(e.game_mode() == GameMode::HighestScore);
        e.set_game_mode(GameMode::Kills187);
        const LevelData tiny = load_map("TINY");
        e.init(tiny, 5u);
        ASSERT_TRUE(e.game_mode() == GameMode::Kills187);
        e.init(load_map("SMALL"), 6u, 0x05);
        ASSERT_TRUE(e.game_mode() == GameMode::Kills187);
        ASSERT_TRUE(e.get_world_state().game_mode == GameMode::Kills187);
        ASSERT_EQ(static_cast<int>(GameMode::HighestScore), 0);
        ASSERT_EQ(static_cast<int>(GameMode::Kills187), 1);
        ASSERT_EQ(kLastGameMode, 1);
        ASSERT_TRUE(valid_game_mode(0) && valid_game_mode(1) && !valid_game_mode(2) && !valid_game_mode(255));
        ASSERT_EQ(std::string(game_mode_name(GameMode::Kills187)), std::string("187"));
    } TEST_END();

    TEST_CASE("1.2 the state hash: a mode of the original's rules keeps the hash it had, 187 is another state, the same match twice is the same hash") {
        const LevelData tiny = load_map("TINY");
        SimulationEngine plain;
        plain.init(tiny, 9u);
        SimulationEngine explicit_default;
        explicit_default.set_game_mode(GameMode::HighestScore);
        explicit_default.init(tiny, 9u);
        ASSERT_TRUE(plain.state_hash() == explicit_default.state_hash());
        SimulationEngine a;
        a.set_game_mode(GameMode::Kills187);
        a.init(tiny, 9u);
        SimulationEngine b;
        b.set_game_mode(GameMode::Kills187);
        b.init(tiny, 9u);
        ASSERT_TRUE(a.state_hash() == b.state_hash());
        ASSERT_TRUE(!(a.state_hash() == plain.state_hash()));
        // 187's engine part alone (the mode word) is mixed in: the same state with the word taken away would hash like the other engine's
        for (int i = 0; i < 400; ++i) { a.tick(); b.tick(); }
        ASSERT_TRUE(a.state_hash() == b.state_hash());
        SimulationEngine copy(a);
        ASSERT_TRUE(copy.state_hash() == a.state_hash());
        ASSERT_TRUE(copy.game_mode() == GameMode::Kills187);
    } TEST_END();

    TEST_CASE("2.1 a 187 match on every shipped map starts as the map does (the same ants on the same tiles, the same eggs) and has no food, no power-ups, no droppers") {
        for (const char* name : kMaps) {
            const LevelData level = load_map(name);
            SimulationEngine plain;
            plain.init(level, 77u);
            SimulationEngine e;
            e.set_game_mode(GameMode::Kills187);
            e.init(level, 77u);
            const WorldState& ws = e.get_world_state();
            const WorldState& base = plain.get_world_state();
            for (uint8_t t = 0; t < MAX_PLAYERS; ++t) {
                const std::vector<AntSnapshot> ants = ants_of(e, t);
                const std::vector<AntSnapshot> expected = ants_of(plain, t);
                ASSERT_TRUE(!expected.empty());
                ASSERT_EQ(ants.size(), expected.size());
                for (size_t i = 0; i < ants.size(); ++i) {
                    ASSERT_TRUE(ants[i].type == expected[i].type);
                    ASSERT_TRUE(ants[i].tile_x == expected[i].tile_x && ants[i].tile_y == expected[i].tile_y);
                }
                ASSERT_EQ(ws.player_eggs[t], base.player_eggs[t]);
                ASSERT_TRUE(ws.player_eggs[t] > 0u);
            }
            for (const TileCell& c : ws.cells) ASSERT_TRUE(!c.is_food && !c.is_powerup && !c.has_food() && !c.has_powerup() && !c.has_lunchbox());
            ASSERT_EQ(ws.flower_droppers.size(), 0u);
            ASSERT_EQ(ws.player_scores[0], 0);
        }
    } TEST_END();

    TEST_CASE("2.2 the same map without the mode keeps its food, its power-ups, its eggs and its start markers (TINY: 3 ants each)") {
        const LevelData tiny = load_map("TINY");
        SimulationEngine e;
        e.init(tiny, 77u);
        ASSERT_EQ(ants_of(e, 0).size(), 3u);
        ASSERT_EQ(ants_of(e, 1).size(), 3u);
        ASSERT_EQ(ants_of(e, 2).size(), 3u);
        ASSERT_EQ(ants_of(e, 3).size(), 3u);
        ASSERT_TRUE(e.get_world_state().player_eggs[0] > 0u);
        bool food = false;
        for (const TileCell& c : e.get_world_state().cells) food = food || c.is_food;
        ASSERT_TRUE(food);
        bool pu = false;
        for (const char* name : kMaps) {
            SimulationEngine other;
            other.init(load_map(name), 77u);
            for (const TileCell& c : other.get_world_state().cells) pu = pu || c.is_powerup;
        }
        ASSERT_TRUE(pu);                                                   // some shipped map has power-ups, and they are there without the mode
        ASSERT_TRUE(ants_of(e, 0)[0].type == AntType::Worker);
    } TEST_END();

    TEST_CASE("2.3 a roster of two teams: only they get ants and eggs, the teams that are not in the match have none") {
        for (const char* name : kMaps) {
            const LevelData level = load_map(name);
            SimulationEngine plain;
            plain.init(level, 3u, 0x09);                               // teams 0 and 3
            SimulationEngine e;
            e.set_game_mode(GameMode::Kills187);
            e.init(level, 3u, 0x09);
            ASSERT_EQ(ants_of(e, 0).size(), ants_of(plain, 0).size());
            ASSERT_EQ(ants_of(e, 1).size(), 0u);
            ASSERT_EQ(ants_of(e, 2).size(), 0u);
            ASSERT_EQ(ants_of(e, 3).size(), ants_of(plain, 3).size());
            ASSERT_TRUE(e.get_world_state().player_eggs[0] > 0u && e.get_world_state().player_eggs[3] > 0u);
            ASSERT_EQ(e.get_world_state().player_eggs[1], 0u);
        }
    } TEST_END();

    TEST_CASE("2.4 nothing drops in 187: every map runs for three minutes (the flowers of the original drop a power-up every 30 to 60 seconds) and no food or power-up ever appears; the same map without the mode does drop") {
        bool plain_drops = false;
        for (const char* name : kMaps) {
            const LevelData level = load_map(name);
            SimulationEngine e;
            e.set_game_mode(GameMode::Kills187);
            e.init(level, 31u);
            SimulationEngine plain;
            plain.init(level, 31u);
            size_t plain_start = 0;
            for (const TileCell& c : plain.get_world_state().cells) plain_start += (c.is_powerup || c.has_powerup()) ? 1u : 0u;
            for (int t = 0; t < 3600; ++t) {
                e.tick();
                plain.tick();
                if (t % 200 != 199) continue;
                for (const TileCell& c : e.get_world_state().cells) ASSERT_TRUE(!c.is_food && !c.is_powerup && !c.has_food() && !c.has_powerup() && !c.has_lunchbox());
                ASSERT_EQ(e.get_world_state().flower_droppers.size(), 0u);
            }
            size_t plain_end = 0;
            for (const TileCell& c : plain.get_world_state().cells) plain_end += (c.is_powerup || c.has_powerup()) ? 1u : 0u;
            plain_drops = plain_drops || plain_end > plain_start;
        }
        ASSERT_TRUE(plain_drops);                                          // (the check is able to see a drop: some shipped map drops one in three minutes without the mode)
    } TEST_END();

    TEST_CASE("3.1 a kill is a point: the killer's score and its enemy_killed go up together, the victim's team loses an ant, nothing else scores") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1});
        kill_pair(e, 0, 1);
        const WorldState& ws = e.get_world_state();
        ASSERT_EQ(ws.player_stats[0].enemy_killed, 1u);
        ASSERT_EQ(ws.player_scores[0], 1);
        ASSERT_EQ(ws.player_stats[1].friendly_lost, 1u);
        ASSERT_EQ(ws.player_scores[1], 0);
        ASSERT_EQ(ants_of(e, 1).size(), 3u);                               // its own three; the victim was a fourth
        ASSERT_EQ(ws.match_result.ants_left[1], 3u);
        ASSERT_EQ(ws.match_result.ants_left[0], 4u);                        // its three and the killer
        ASSERT_TRUE(ws.score_bubbles.empty());                             // a fight has many kills: no bubble and no cue for them
    } TEST_END();

    TEST_CASE("3.1b an ally's kill is no point (allied teams are not enemies) and the ants of a team that dropped out die with nobody to thank") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1, 2, 3});          // (a fourth team keeps the match going when team 2 drops)
        e.apply_command([] { Command c; c.type = CommandType::AllianceInvite; c.issuer = 0; c.other_player = 1; return c; }());
        e.apply_command([] { Command c; c.type = CommandType::AllianceAccept; c.issuer = 1; c.other_player = 0; return c; }());
        ASSERT_EQ(e.alliance_of(0), 1);
        // an ant of team 1 whose last damage came from its ally, team 0 (a bomb or a fire does that; a melee hit does not reach an ally), dies
        const uint32_t ally = e.spawn_unit(1, AntType::Combat, TileCoord{30, 30});
        e.get_unit(ally).killer_team = 0;
        e.kill_unit(ally);
        for (int k = 0; k < 80; ++k) e.tick();
        ASSERT_TRUE(e.get_unit(ally).removed);
        ASSERT_EQ(e.get_world_state().player_stats[1].friendly_lost, 1u);
        ASSERT_EQ(e.get_world_state().player_scores[0], 0);                 // no point for it
        // the same death with an enemy of the last damage is one
        const uint32_t foe = e.spawn_unit(2, AntType::Combat, TileCoord{30, 34});
        e.get_unit(foe).killer_team = 0;
        e.kill_unit(foe);
        for (int k = 0; k < 80; ++k) e.tick();
        ASSERT_TRUE(e.get_unit(foe).removed);
        ASSERT_EQ(e.get_world_state().player_scores[0], 1);
        // a wounded ant of team 2 (its last damage came from team 0), then team 2 drops out: its ants die and team 0 gets nothing for them
        const uint32_t b = e.spawn_unit(2, AntType::Combat, TileCoord{21, 30});
        e.get_unit(b).killer_team = 0;
        const int32_t before = e.get_world_state().player_scores[0];
        e.drop_player(2);
        for (int k = 0; k < 120; ++k) e.tick();
        ASSERT_TRUE(e.get_unit(b).removed);
        ASSERT_EQ(e.get_world_state().player_scores[0], before);
    } TEST_END();

    TEST_CASE("1.3 the mode alone changes the hash: the same world built twice differs between 187 and the original's game, and equals itself") {
        SimulationEngine a = small_world(GameMode::Kills187, {0, 1});
        SimulationEngine b = small_world(GameMode::Kills187, {0, 1});
        SimulationEngine plain = small_world(GameMode::HighestScore, {0, 1});
        SimulationEngine plain2 = small_world(GameMode::HighestScore, {0, 1});
        ASSERT_TRUE(a.state_hash() == b.state_hash());
        ASSERT_TRUE(plain.state_hash() == plain2.state_hash());
        ASSERT_TRUE(!(a.state_hash() == plain.state_hash()));
    } TEST_END();

    TEST_CASE("2.5 no food tile stays on the map in 187, owned by a Block 2 object or not") {
        const LevelData level = load_map("TINY");
        Grid g;
        g.init_from_level(level);
        TileCell& orphan = g.get_cell_mut(3u, 3u);                         // a food tile with no object (as a community map can have)
        orphan.is_food = true;
        orphan.interactive_id = 100;
        g.strip_pickups();
        for (uint32_t y = 0; y < g.height(); ++y) {
            for (uint32_t x = 0; x < g.width(); ++x) ASSERT_TRUE(!g.get_cell(x, y).is_food && !g.get_cell(x, y).is_powerup);
        }
        ASSERT_EQ(g.get_cell(3u, 3u).interactive_id, TILE_EMPTY);
    } TEST_END();

    TEST_CASE("3.2 the same kill in the original's rules scores nothing") {
        SimulationEngine e = small_world(GameMode::HighestScore, {0, 1});
        kill_pair(e, 0, 1);
        ASSERT_EQ(e.get_world_state().player_stats[0].enemy_killed, 1u);
        ASSERT_EQ(e.get_world_state().player_scores[0], 0);
        ASSERT_EQ(e.get_world_state().match_result.ants_left[0], 0u);       // only 187 counts the ants that are left
    } TEST_END();

    TEST_CASE("3.3 the last side standing ends the match at once and wins whatever the kills are (team 1 killed more, team 0 is the one that is left)") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1});
        kill_pair(e, 1, 0);
        kill_pair(e, 1, 0);                                                // team 1 has 2 kills, team 0 none
        ASSERT_EQ(e.get_world_state().player_scores[1], 2);
        ASSERT_EQ(e.get_world_state().player_scores[0], 0);
        ASSERT_FALSE(e.is_match_over());
        for (const AntSnapshot& a : ants_of(e, 1)) e.kill_unit(a.id);       // the hook credits nobody
        run_ticks(e, 10);
        ASSERT_TRUE(e.is_match_over());
        const MatchResult r = e.get_world_state().match_result;
        ASSERT_EQ(r.standing, 0);
        ASSERT_EQ(r.ants_left[0], 3u);
        ASSERT_EQ(r.ants_left[1], 0u);
        ASSERT_EQ(r.rows(0)[0].first, 0);
        ASSERT_EQ(r.rows(1)[0].first, 0);                                  // on the screen of the one with more kills too
        ASSERT_EQ(r.rows(1)[0].ants_left, 3);
        ASSERT_EQ(r.rows(1)[1].enemy_killed, 2);
        ASSERT_TRUE(r.is_winner(0));
        ASSERT_FALSE(r.is_winner(1));
        ASSERT_EQ(r.winning_players.size(), 1u);
        ASSERT_EQ(r.winning_players[0], 0);
    } TEST_END();

    TEST_CASE("3.4 the original's rule would not end that match: a last side with 0 points never wins; 187 ends it, 4 teams in the match, three of them gone") {
        SimulationEngine plain = small_world(GameMode::HighestScore, {0, 1});
        for (const AntSnapshot& a : ants_of(plain, 1)) plain.kill_unit(a.id);
        run_ticks(plain, 200);
        ASSERT_FALSE(plain.is_match_over());
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1, 2, 3});
        for (int t : {1, 2}) for (const AntSnapshot& a : ants_of(e, static_cast<uint8_t>(t))) e.kill_unit(a.id);
        run_ticks(e, 40);
        ASSERT_FALSE(e.is_match_over());                                   // two teams are left
        for (const AntSnapshot& a : ants_of(e, 3)) e.kill_unit(a.id);
        run_ticks(e, 40);
        ASSERT_TRUE(e.is_match_over());
        ASSERT_EQ(e.get_world_state().match_result.standing, 0);
    } TEST_END();

    TEST_CASE("3.5 a match of one team does not end by itself (nobody to beat), and a match where nobody is left ends and ranks by kills") {
        SimulationEngine solo;
        solo.set_game_mode(GameMode::Kills187);
        solo.init(load_map("TINY"), 4u, 0x01);                             // the roster is one team (init_test_world's is all four)
        run_ticks(solo, 200);
        ASSERT_FALSE(solo.is_match_over());
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1}, 1);
        kill_pair(e, 0, 1);
        for (const AntSnapshot& a : ants_of(e, 1)) e.kill_unit(a.id);       // team 0 has a kill, team 1 has nothing left: team 0 is the side that is left and wins
        run_ticks(e, 10);
        ASSERT_TRUE(e.is_match_over());
        ASSERT_EQ(e.get_world_state().match_result.standing, 0);
        SimulationEngine none = small_world(GameMode::Kills187, {0, 1}, 1);
        for (int t : {0, 1}) for (const AntSnapshot& a : ants_of(none, static_cast<uint8_t>(t))) none.kill_unit(a.id);
        run_ticks(none, 10);
        ASSERT_TRUE(none.is_match_over());
        ASSERT_EQ(none.get_world_state().match_result.standing, PLAYER_NEUTRAL);
    } TEST_END();

    TEST_CASE("3.6 the clock: with ants on both sides the most kills ranks first, a tie goes to the local team, nobody stands") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1});
        kill_pair(e, 1, 0);
        e.set_match_time_remaining_ms(100);
        run_ticks(e, 40);
        ASSERT_TRUE(e.is_match_over());
        const MatchResult r = e.get_world_state().match_result;
        ASSERT_EQ(r.standing, PLAYER_NEUTRAL);
        ASSERT_EQ(r.rows(0)[0].first, 1);
        ASSERT_EQ(r.rows(1)[0].first, 1);
        ASSERT_TRUE(r.is_winner(1));
        ASSERT_FALSE(r.is_winner(0));
        SimulationEngine tie = small_world(GameMode::Kills187, {0, 1});
        tie.set_match_time_remaining_ms(100);
        run_ticks(tie, 40);
        ASSERT_TRUE(tie.is_match_over());
        const MatchResult t = tie.get_world_state().match_result;
        ASSERT_EQ(t.rows(0)[0].first, 0);                                  // the original's tie rule: each screen puts its own team first
        ASSERT_EQ(t.rows(1)[0].first, 1);
    } TEST_END();

    TEST_CASE("3.7 allies: two allied teams that are left are one side (the match ends when an enemy is gone) and share the first row") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1, 2});
        e.apply_command([] { Command c; c.type = CommandType::AllianceInvite; c.issuer = 0; c.other_player = 1; return c; }());
        e.apply_command([] { Command c; c.type = CommandType::AllianceAccept; c.issuer = 1; c.other_player = 0; return c; }());
        ASSERT_EQ(e.alliance_of(0), 1);
        run_ticks(e, 10);
        ASSERT_FALSE(e.is_match_over());                                   // three teams, two sides
        for (const AntSnapshot& a : ants_of(e, 2)) e.kill_unit(a.id);
        run_ticks(e, 40);
        ASSERT_TRUE(e.is_match_over());
        const MatchResult r = e.get_world_state().match_result;
        ASSERT_TRUE(r.standing == 0 || r.standing == 1);
        const std::vector<ResultRow> rows = r.rows(2);
        ASSERT_EQ(rows[0].first, 0);
        ASSERT_EQ(rows[0].second, 1);
        ASSERT_EQ(rows[0].ants_left, 6);
        ASSERT_TRUE(r.is_winner(0) && r.is_winner(1) && !r.is_winner(2));
    } TEST_END();

    TEST_CASE("3.8 a team that drops out is not alive: the last one that is left wins (a match of 3, one drops, one is killed)") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1, 2});
        e.apply_command([] { Command c; c.type = CommandType::Drop; c.issuer = 2; return c; }());
        run_ticks(e, 10);
        ASSERT_FALSE(e.is_match_over());
        for (const AntSnapshot& a : ants_of(e, 1)) e.kill_unit(a.id);
        run_ticks(e, 40);
        ASSERT_TRUE(e.is_match_over());
        ASSERT_EQ(e.get_world_state().match_result.standing, 0);
    } TEST_END();

    TEST_CASE("3.9 teams from the start (the room's Teams choice): 2 against 2 is two sides; an ally that is gone does not end the match, the last side with an ant does, and its two players share the first row") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1, 2, 3});
        apply_start_teams(e, StartTeams{true, 0, 2});                      // green + blue against red + black
        ASSERT_EQ(e.alliance_of(0), 2);
        ASSERT_EQ(e.alliance_of(1), 3);
        kill_pair(e, 0, 1);                                                // a kill of the other side: one point for green, one for blue (the pair's score is the two added)
        kill_pair(e, 2, 3);
        ASSERT_EQ(e.get_world_state().player_scores[0], 2);                // an alliance shows its two players' kills added, on both boxes, as today
        ASSERT_EQ(e.get_world_state().player_scores[2], 2);
        ASSERT_EQ(e.get_world_state().player_scores[1], 0);
        for (const AntSnapshot& a : ants_of(e, 1)) e.kill_unit(a.id);       // red is gone: red + black is still a side (black)
        run_ticks(e, 40);
        ASSERT_FALSE(e.is_match_over());
        for (const AntSnapshot& a : ants_of(e, 0)) e.kill_unit(a.id);       // green is gone: green + blue is still a side (blue)
        run_ticks(e, 40);
        ASSERT_FALSE(e.is_match_over());
        for (const AntSnapshot& a : ants_of(e, 3)) e.kill_unit(a.id);       // black is gone: one side is left, blue's
        run_ticks(e, 40);
        ASSERT_TRUE(e.is_match_over());
        const MatchResult r = e.get_world_state().match_result;
        ASSERT_EQ(r.standing, 2);
        for (int screen = 0; screen < 4; ++screen) {
            const std::vector<ResultRow> rows = r.rows(static_cast<uint8_t>(screen));
            ASSERT_EQ(rows.size(), 2u);                                    // two sides, two rows
            ASSERT_TRUE(rows[0].first == 0 || rows[0].first == 2);
            ASSERT_TRUE(rows[0].second == 0 || rows[0].second == 2);
            ASSERT_EQ(rows[0].enemy_killed, 2);                            // the two players' kills added
            ASSERT_EQ(rows[0].ants_left, 4);                               // blue's three and the ant that killed
        }
        ASSERT_TRUE(r.is_winner(0) && r.is_winner(2) && !r.is_winner(1) && !r.is_winner(3));
    } TEST_END();

    TEST_CASE("3.10 teams from the start with three players (2 against 1), and the clock with teams: the most kills of a side ranks first") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1, 2});
        apply_start_teams(e, StartTeams{true, 0, 2});                      // green + blue against red
        ASSERT_EQ(e.alliance_of(0), 2);
        ASSERT_TRUE(e.alliance_of(1) != 0 && e.alliance_of(1) != 2);        // red has no ally
        for (const AntSnapshot& a : ants_of(e, 1)) e.kill_unit(a.id);       // red has nothing left: the allies stand
        run_ticks(e, 40);
        ASSERT_TRUE(e.is_match_over());
        ASSERT_EQ(e.get_world_state().match_result.standing, 0);
        SimulationEngine f = small_world(GameMode::Kills187, {0, 1, 2});
        apply_start_teams(f, StartTeams{true, 0, 2});
        kill_pair(f, 1, 0);
        kill_pair(f, 1, 2);
        kill_pair(f, 0, 1);                                                // red has 2 kills, green + blue have 1
        f.set_match_time_remaining_ms(100);
        run_ticks(f, 40);
        ASSERT_TRUE(f.is_match_over());
        const MatchResult r = f.get_world_state().match_result;
        ASSERT_EQ(r.standing, PLAYER_NEUTRAL);
        ASSERT_EQ(r.rows(0)[0].first, 1);                                  // red (2 kills) before the pair (1)
        ASSERT_EQ(r.rows(0)[1].enemy_killed, 1);
        ASSERT_TRUE(r.is_winner(1) && !r.is_winner(0) && !r.is_winner(2));
    } TEST_END();

    TEST_CASE("4.1 the eggs are lives: a team with an egg is not out when its last ant dies, the free hatch costs it none of its kills, and the match goes on until the egg is hatched and the ant killed") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1}, 1);
        kill_pair(e, 1, 0);
        kill_pair(e, 1, 0);                                                // team 1 has 2 kills
        ASSERT_EQ(e.get_world_state().player_scores[1], 2);
        for (const AntSnapshot& a : ants_of(e, 0)) e.kill_unit(a.id);       // team 0 has no ant and no egg left: team 1 is the side that is left, whatever its kills
        run_ticks(e, 10);
        ASSERT_TRUE(e.is_match_over());
        ASSERT_EQ(e.get_world_state().match_result.standing, 1);
        // the other way round: team 0 has the egg, loses its last ant to the killers of team 1
        SimulationEngine f = small_world(GameMode::Kills187, {0, 1}, 1);
        f.set_player_eggs(0, 1);
        kill_pair(f, 0, 1);
        kill_pair(f, 0, 1);
        ASSERT_EQ(f.get_world_state().player_scores[0], 2);
        for (const AntSnapshot& a : ants_of(f, 0)) f.kill_unit(a.id);       // the last ant of team 0 dies: the egg is hatched at once, for nothing
        ASSERT_EQ(f.get_pending_hatch_count(0), 1u);
        ASSERT_EQ(f.get_world_state().player_eggs[0], 0u);
        ASSERT_EQ(f.get_world_state().player_scores[0], 2);
        for (const AntSnapshot& a : ants_of(f, 1)) f.kill_unit(a.id);       // team 1 has nothing left, team 0 has a hatching egg: the match ends only for a side that is gone
        run_ticks(f, 10);
        ASSERT_TRUE(f.is_match_over());
        ASSERT_EQ(f.get_world_state().match_result.standing, 0);
        ASSERT_EQ(f.get_world_state().player_scores[0], 2);
    } TEST_END();

    TEST_CASE("4.1b a hatch that a player asks for needs 200 kills in 187 (as it needs 200 points elsewhere) and takes none of them") {
        SimulationEngine e = small_world(GameMode::Kills187, {0, 1});
        e.set_player_eggs(0, 2);
        ASSERT_TRUE(e.try_hatch(0, AntType::Worker, false) == SimulationEngine::HatchResult::NotEnoughPoints);
        ASSERT_TRUE(e.try_hatch(0, AntType::Worker, true) == SimulationEngine::HatchResult::Started);
        ASSERT_EQ(e.get_world_state().player_scores[0], 0);
        ASSERT_EQ(e.get_world_state().score_bubbles.size(), 0u);
    } TEST_END();

    TEST_CASE("4.2 a ranking that sets no `standing` sorts exactly as before (the original's exchange sort, tie to the local team)") {
        MatchResult r;
        r.present_mask = 0x0F;
        r.stats[0].score = 5;
        r.stats[1].score = 9;
        r.stats[2].score = 9;
        r.stats[3].score = 1;
        ASSERT_EQ(r.standing, PLAYER_NEUTRAL);
        ASSERT_EQ(r.rows(0)[0].first, 1);                                  // 9 beats 5; of the two 9s the earlier row stays (team 0 is not one of them)
        ASSERT_EQ(r.rows(2)[0].first, 2);                                  // the local team goes first on a tie
        ASSERT_EQ(r.rows(3)[3].first, 3);
        r.standing = 3;
        ASSERT_EQ(r.rows(0)[0].first, 3);                                  // the last one standing goes first whatever its 1 point is
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

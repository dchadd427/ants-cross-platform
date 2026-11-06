// AI32: the token groups of IslandInfo (docs/BOTS.md, "Islands"): power-ups that touch each other (8-connected) form a group whose members are listed in the order of a walk from the first entrance, and the
// expedition walks a row of them from every one of its entrances (ExpeditionTask::walk_of).
// AI32.1 pins the order of the members against a plain re-statement of the walk (a search of the members already listed, which is what the analysis did before it kept a flag for each power-up) on clusters,
// lines, diagonal chains, blobs and scattered cells; AI32.2 pins the cost: one cluster of three thousand power-ups is analysed in a time that the cube of its size could not keep. AI32.3 pins the order of
// the expedition's walk from every end against a plain re-statement of it (a scan of all the tokens for each one that is reached, which is what walk_of did before it looked the tokens up by their
// cells); AI32.4 pins its cost: the walks from every entrance of a row of three thousand tokens take a time that a scan of all of them for every one could not keep. AI32.5 walks a row that is two rows made by hand
// (what no walk reaches comes last), AI32.6 compares the members and the walks of every group on every map of the library.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "ai_test.hpp"
#include "ants_ai/island_expedition.hpp"
#include "ants_ai/island_info.hpp"

namespace {

using namespace ants;
using namespace ants::ai;

// The first `count` power-ups in reading order, each with water on every cell round it that holds no power-up: no walker can enter at them, so the first entrance of their row leads to a later member
void wall_off_first(sim::SimulationEngine& sim, const std::vector<sim::TileCoord>& cells, size_t count) {
    std::vector<sim::TileCoord> order = cells;
    std::sort(order.begin(), order.end(), [](const sim::TileCoord& a, const sim::TileCoord& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    for (size_t k = 0; k < count && k < order.size(); ++k) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                bool token = false;
                for (const sim::TileCoord& c : cells) token = token || (c.x == order[k].x + dx && c.y == order[k].y + dy);
                if (!token) sim.grid_mut().set_terrain(order[k].x + dx, order[k].y + dy, sim::TERRAIN_WATER);
            }
        }
    }
}

// A test world of 120 x 120 with the hill of team 0 in the corner and a power-up on each of the cells (the five kinds in turn)
void world_with(sim::SimulationEngine& sim, const std::vector<sim::TileCoord>& cells) {
    sim.init_test_world(120, 120, 1, 720000);
    sim.set_anthill(0, sim::TileCoord{4, 4});
    for (size_t i = 0; i < cells.size(); ++i) sim.grid_mut().place_powerup(cells[i].x, cells[i].y, static_cast<uint8_t>(1 + i % 5));
}

// The layouts, placed inside x and y from 20 to 110 so that ground is left all round them
std::vector<sim::TileCoord> layout(const std::string& kind, uint32_t n) {
    std::vector<sim::TileCoord> cells;
    if (kind == "cluster") {
        const uint32_t w = 1 + static_cast<uint32_t>(std::sqrt(static_cast<double>(n)));
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>(30 + k % w), static_cast<int32_t>(30 + k / w)});
    } else if (kind == "line") {
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>(20 + k % 80), static_cast<int32_t>(20 + k / 80)});
    } else if (kind == "diagonal") {
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>(20 + k % 80 + (k / 80) * 2), static_cast<int32_t>(20 + k % 80)});
    } else if (kind == "blob") {
        uint64_t state = 12345;                                                                 // (a walk that grows from one cell, the same every run)
        const auto next = [&state]() { state = state * 6364136223846793005ull + 1442695040888963407ull; return static_cast<uint32_t>(state >> 33); };
        std::vector<sim::TileCoord> have{sim::TileCoord{60, 60}};
        cells.push_back(have[0]);
        for (uint32_t tries = 0; have.size() < n && tries < n * 50; ++tries) {
            const sim::TileCoord from = have[next() % have.size()];
            const sim::TileCoord to{from.x + static_cast<int32_t>(next() % 3) - 1, from.y + static_cast<int32_t>(next() % 3) - 1};
            if (to.x < 20 || to.y < 20 || to.x > 110 || to.y > 110) continue;
            bool known = false;
            for (const sim::TileCoord& c : have) known = known || (c.x == to.x && c.y == to.y);
            if (!known) {
                have.push_back(to);
                cells.push_back(to);
            }
        }
    } else if (kind == "vline") {                                                               // columns of 90 tokens (a single column up to 90): the tokens of a column share an x, so a lookup by cell has to tell them apart
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>(30 + k / 90), static_cast<int32_t>(20 + k % 90)});
    } else if (kind == "pair") {                                                                // two clusters, far apart: two rows with an entrance each
        const uint32_t w = 1 + static_cast<uint32_t>(std::sqrt(static_cast<double>(n / 2 + 1)));
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>((k % 2 == 0 ? 30 : 75) + (k / 2) % w), static_cast<int32_t>(30 + (k / 2) / w)});
    } else {                                                                                    // "scatter": every second cell of every second row, none touches another
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>(20 + 2 * (k % 40)), static_cast<int32_t>(20 + 2 * (k / 40))});
    }
    return cells;
}

// The world of a layout. The kinds "walled" (a cluster) and "walled_line" (a line) have water all round their first power-up (the first six of the line), so that the first entrance of the row leads to a later member:
// the walk does not start at the first member that the analysis finds, and the order of a walk differs from the order of the search
std::vector<sim::TileCoord> make_world(sim::SimulationEngine& sim, const std::string& kind, uint32_t n) {
    const std::vector<sim::TileCoord> cells = layout(kind == "walled" ? "cluster" : kind == "walled_line" ? "line" : kind, n);
    world_with(sim, cells);
    if (kind == "walled") wall_off_first(sim, cells, 1);
    if (kind == "walled_line") wall_off_first(sim, cells, 6);
    return cells;
}

// The members of the group that `start` belongs to, in the order of a walk from `start`: the group is found by a search over all the power-ups from its lowest index (the order of the search is the order of
// `found`), and the walk takes, for each member already listed, the members of `found` that are not listed yet and touch it. Whether a member is listed is a search of the list (`by_search`, as it once
// was in the analysis: the work grows with the cube of the group, so only for small ones) or a flag.
std::vector<uint32_t> reference_members(const std::vector<PowerUpInfo>& powerups, uint32_t start, bool by_search = true) {
    std::vector<int> group(powerups.size(), -1);
    std::vector<uint32_t> any{start};
    group[start] = 0;
    for (size_t head = 0; head < any.size(); ++head) {
        for (size_t j = 0; j < powerups.size(); ++j) {
            if (group[j] >= 0 || powerups[any[head]].tile.chebyshev_dist(powerups[j].tile) != 1) continue;
            group[j] = 0;
            any.push_back(static_cast<uint32_t>(j));
        }
    }
    const uint32_t lowest = *std::min_element(any.begin(), any.end());
    std::vector<int> seen(powerups.size(), -1);
    std::vector<uint32_t> found{lowest};
    seen[lowest] = 0;
    for (size_t head = 0; head < found.size(); ++head) {
        for (size_t j = 0; j < powerups.size(); ++j) {
            if (seen[j] >= 0 || powerups[found[head]].tile.chebyshev_dist(powerups[j].tile) != 1) continue;
            seen[j] = 0;
            found.push_back(static_cast<uint32_t>(j));
        }
    }
    std::vector<uint32_t> members{start};
    std::vector<char> flag(powerups.size(), 0);
    flag[start] = 1;
    for (size_t head = 0; head < members.size(); ++head) {
        for (const uint32_t m : found) {
            if (by_search ? std::find(members.begin(), members.end(), m) != members.end() : flag[m] != 0) continue;
            if (powerups[members[head]].tile.chebyshev_dist(powerups[m].tile) != 1) continue;
            members.push_back(m);
            flag[m] = 1;
        }
    }
    return members;
}

// The tokens of a row in the order of a walk from an entrance, as a Hard bot takes it, restated plainly: the members in the order of the analysis (the other way round for the far end), the ones
// within a step of the entrance first, then from every token that is reached the ones that touch it, found by a scan of all of the tokens; then the tokens by their steps, ties in the order of the row
std::vector<uint32_t> reference_walk(const std::vector<PowerUpInfo>& powerups, const TokenGroup& group, const TokenGroup::Entrance& entrance) {
    std::vector<uint32_t> order = group.members;
    if (entrance.member != 0) std::reverse(order.begin(), order.end());
    const int32_t kFar = 1 << 20;
    std::vector<int32_t> steps(order.size(), kFar);
    std::vector<size_t> queue;
    for (size_t i = 0; i < order.size(); ++i) {
        if (powerups[order[i]].tile.chebyshev_dist(entrance.tile) > 1) continue;
        steps[i] = 1;
        queue.push_back(i);
    }
    for (size_t head = 0; head < queue.size(); ++head) {
        for (size_t j = 0; j < order.size(); ++j) {
            if (steps[j] != kFar || powerups[order[j]].tile.chebyshev_dist(powerups[order[queue[head]]].tile) > 1) continue;
            steps[j] = steps[queue[head]] + 1;
            queue.push_back(j);
        }
    }
    std::vector<size_t> by(order.size());
    for (size_t i = 0; i < by.size(); ++i) by[i] = i;
    std::stable_sort(by.begin(), by.end(), [&](size_t a, size_t b) { return steps[a] < steps[b]; });
    std::vector<uint32_t> out;
    for (const size_t i : by) out.push_back(order[i]);
    return out;
}

// The expedition of a Hard bot (timed_row: the walk by steps) or of the others (the order of the analysis)
ExpeditionTask::Params walk_params(bool timed) {
    ExpeditionTask::Params p;
    p.timed_row = timed;
    return p;
}

}  // namespace

void run_token_group_tests() {
    TEST_CASE("AI32.1 The Members Of A Token Group Are In The Order Of A Walk From The First Entrance: Clusters, Lines, Rows With Water Round Their First Power-Ups, Diagonal Chains, Blobs And Scattered Cells Of 1 To 300 Power-Ups Agree With A Plain Re-Statement Of The Walk, Every Power-Up Is In One Group And Once") {
        for (const char* kind : {"cluster", "walled", "line", "walled_line", "diagonal", "blob", "scatter"}) {
            for (const uint32_t n : {1u, 2u, 3u, 9u, 50u, 150u, 300u}) {
                sim::SimulationEngine sim;
                make_world(sim, kind, n);
                const MapInfo map(sim);
                const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
                const std::vector<PowerUpInfo>& powerups = map.powerups();
                ASSERT_EQ(powerups.size(), static_cast<size_t>(n));
                std::vector<int> times(powerups.size(), 0);
                size_t listed = 0;
                for (const TokenGroup& g : info.token_groups()) {
                    ASSERT_TRUE(!g.members.empty());
                    ASSERT_TRUE(g.members == reference_members(powerups, g.members[0]));                  // (the members are the walk's order)
                    for (const uint32_t m : g.members) {
                        ASSERT_TRUE(m < powerups.size());
                        ++times[m];
                        ++listed;
                    }
                    for (const TokenGroup::Entrance& e : g.entrances) ASSERT_TRUE(e.member < g.members.size());
                    ASSERT_TRUE(g.entrances.empty() || g.entrances[0].member == 0);                    // (the first entrance is the one that the walk starts from)
                }
                ASSERT_EQ(listed, powerups.size());
                for (const int t : times) ASSERT_EQ(t, 1);
                if (std::string(kind) == "scatter") ASSERT_EQ(info.token_groups().size(), static_cast<size_t>(n));        // (none touches another: a group each)
                if (std::string(kind) == "cluster" || std::string(kind) == "line") ASSERT_EQ(info.token_groups().size(), size_t{1});
            }
        }
    } TEST_END();

    TEST_CASE("AI32.2 One Cluster Of 3,000 Power-Ups Is One Token Group Of 3,000 In An Order That Is Still The Walk's, And Is Analysed In A Time That The Cube Of Its Size Could Not Keep (Well Under The 400 Milliseconds That Is Allowed Here)") {
        sim::SimulationEngine sim;
        world_with(sim, layout("cluster", 3000));
        const MapInfo map(sim);
        ASSERT_EQ(map.powerups().size(), size_t{3000});
        const auto before = std::chrono::steady_clock::now();
        const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        ASSERT_EQ(info.token_groups().size(), size_t{1});
        ASSERT_EQ(info.token_groups()[0].members.size(), size_t{3000});
        ASSERT_TRUE(info.token_groups()[0].members == reference_members(map.powerups(), info.token_groups()[0].members[0], false));
        ASSERT_TRUE(ms < 400.0);                                                                  // (the work must not grow with the cube of the size of the group)
    } TEST_END();

    TEST_CASE("AI32.3 The Expedition's Walk Through A Row Is The Same From Every End: Clusters, Pairs Of Clusters, Lines, Rows With Water Round Their First Power-Ups, Columns, Diagonal Chains, Blobs And Scattered Cells Of 1 To 300 Tokens, Every Entrance (Or Sixty Of Them), A Hard Bot's Order Agrees With A Plain Re-Statement Of The Walk, The Others' Is The Analysis's") {
        for (const char* kind : {"cluster", "walled", "pair", "line", "walled_line", "vline", "diagonal", "blob", "scatter"}) {
            for (const uint32_t n : {1u, 2u, 3u, 9u, 50u, 150u, 300u}) {
                sim::SimulationEngine sim;
                make_world(sim, kind, n);
                const MapInfo map(sim);
                const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
                Tactics tactics;
                const ExpeditionTask hard(1, tactics, walk_params(true));
                const ExpeditionTask others(2, tactics, walk_params(false));
                size_t walks = 0;
                size_t reordered = 0;                                                             // (walks whose order is not the analysis's: the steps do something)
                for (const TokenGroup& g : info.token_groups()) {
                    const size_t stride = g.entrances.size() > 60 ? g.entrances.size() / 60 : 1;
                    for (size_t ei = 0; ei < g.entrances.size(); ei += stride) {
                        const TokenGroup::Entrance& e = g.entrances[ei];
                        const std::vector<uint32_t> got = hard.walk_of(map, g, e);
                        ASSERT_TRUE(got == reference_walk(map.powerups(), g, e));
                        ASSERT_TRUE(std::is_permutation(got.begin(), got.end(), g.members.begin(), g.members.end()));
                        std::vector<uint32_t> plain = g.members;
                        if (e.member != 0) std::reverse(plain.begin(), plain.end());
                        ASSERT_TRUE(others.walk_of(map, g, e) == plain);
                        reordered += got != plain ? 1u : 0u;
                        ++walks;
                    }
                }
                ASSERT_TRUE(walks > 0 || std::string(kind).rfind("walled", 0) == 0);                // (a walled row of a few power-ups has no entrance at all)
                if (n >= 50 && (std::string(kind) == "cluster" || std::string(kind) == "pair" || std::string(kind) == "vline" || std::string(kind) == "blob")) ASSERT_TRUE(reordered > 0);
            }
        }
    } TEST_END();

    TEST_CASE("AI32.4 The Walks From Every Entrance Of One Row Of 3,000 Tokens Take A Time That A Scan Of All The Tokens For Every One That Is Reached Could Not Keep (Well Under The 400 Milliseconds That Is Allowed Here), And Three Of Them Are The Plain Walk's") {
        sim::SimulationEngine sim;
        world_with(sim, layout("vline", 3000));                                                   // (34 columns of 90: 3,000 tokens in one row, a column of them shares an x)
        const MapInfo map(sim);
        const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
        ASSERT_EQ(info.token_groups().size(), size_t{1});
        const TokenGroup& g = info.token_groups()[0];
        ASSERT_TRUE(g.entrances.size() >= 200u);                                                  // (the tokens along the rim of the cluster: the plan walks the row from each of them)
        Tactics tactics;
        const ExpeditionTask hard(1, tactics, walk_params(true));
        size_t tokens = 0;
        const auto before = std::chrono::steady_clock::now();
        for (const TokenGroup::Entrance& e : g.entrances) tokens += hard.walk_of(map, g, e).size();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        ASSERT_EQ(tokens, g.entrances.size() * 3000u);
        for (const size_t ei : {size_t{0}, g.entrances.size() / 2, g.entrances.size() - 1}) ASSERT_TRUE(hard.walk_of(map, g, g.entrances[ei]) == reference_walk(map.powerups(), g, g.entrances[ei]));
        ASSERT_TRUE(ms < 400.0);                                                                  // (the work of a walk must not grow with the square of the size of the row)
    } TEST_END();

    TEST_CASE("AI32.5 A Row That Is Not One Group (Made By Hand From Two): The Tokens That The Walk Does Not Reach Come Last, In The Order Of The Row, And The Rest Is The Plain Walk's, From The Near End And From The Far End") {
        sim::SimulationEngine sim;
        world_with(sim, layout("pair", 40));
        const MapInfo map(sim);
        const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
        ASSERT_EQ(info.token_groups().size(), size_t{2});
        const TokenGroup& a = info.token_groups()[0];
        const TokenGroup& b = info.token_groups()[1];
        TokenGroup both;                                                                          // (the analysis never makes one: the members of two rows that touch nothing, the entrances of the first)
        both.members = a.members;
        both.members.insert(both.members.end(), b.members.begin(), b.members.end());
        both.entrances = a.entrances;
        ASSERT_TRUE(!a.members.empty() && !b.members.empty() && !both.entrances.empty());
        Tactics tactics;
        const ExpeditionTask hard(1, tactics, walk_params(true));
        bool near_end = false;
        bool far_end = false;
        for (const TokenGroup::Entrance& e : both.entrances) {
            const std::vector<uint32_t> got = hard.walk_of(map, both, e);
            ASSERT_EQ(got.size(), both.members.size());
            ASSERT_TRUE(got == reference_walk(map.powerups(), both, e));
            ASSERT_TRUE(std::is_permutation(got.end() - static_cast<std::ptrdiff_t>(b.members.size()), got.end(), b.members.begin(), b.members.end()));      // (the second row's tokens are last)
            (e.member == 0 ? near_end : far_end) = true;
        }
        ASSERT_TRUE(near_end && far_end);
    } TEST_END();

    TEST_CASE("AI32.6 Every Map Of The Library: The Members Of Every Token Group Of Every Hill's Analysis, And The Walk From (Up To Twenty Of) Its Entrances, Agree With The Plain Re-Statements") {
        std::vector<std::string> files;
        for (const char* m : {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"}) files.push_back(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + m + ".LVL");
        for (const auto& e : std::filesystem::directory_iterator(std::string(ANTS_SOURCE_DIR) + "/Community-Maps")) {
            if (e.path().extension() == ".lvl") files.push_back(e.path().string());
        }
        std::sort(files.begin(), files.end());
        ASSERT_TRUE(files.size() >= 500u);
        Tactics tactics;
        const ExpeditionTask hard(1, tactics, walk_params(true));
        size_t groups = 0;
        size_t walks = 0;
        size_t reordered = 0;                                                                     // (walks whose order is not the analysis's)
        for (const std::string& file : files) {
            assets::LevelData level;
            ASSERT_TRUE(level.load_from_file(file));
            sim::SimulationEngine sim;
            sim.init(level, 5, 0x0F);
            const MapInfo map(sim);
            for (uint8_t team = 0; team < 4; ++team) {
                if (!map.hill(team).present) continue;
                const IslandInfo info = IslandInfo::analyse(map, sim.grid(), team);
                for (const TokenGroup& g : info.token_groups()) {
                    ASSERT_TRUE(!g.members.empty());
                    ASSERT_TRUE(g.members == reference_members(map.powerups(), g.members[0]));
                    ++groups;
                    const size_t stride = g.entrances.size() > 20 ? g.entrances.size() / 20 : 1;
                    for (size_t ei = 0; ei < g.entrances.size(); ei += stride) {
                        const std::vector<uint32_t> got = hard.walk_of(map, g, g.entrances[ei]);
                        ASSERT_TRUE(got == reference_walk(map.powerups(), g, g.entrances[ei]));
                        std::vector<uint32_t> plain = g.members;
                        if (g.entrances[ei].member != 0) std::reverse(plain.begin(), plain.end());
                        reordered += got != plain ? 1u : 0u;
                        ++walks;
                    }
                }
            }
        }
        ASSERT_TRUE(groups > 20000u && walks > 40000u && reordered > 10000u);                          // (the library has 26,000 groups, 48,000 of the walks are looked at and a third of them are not the order of the analysis)
    } TEST_END();
}

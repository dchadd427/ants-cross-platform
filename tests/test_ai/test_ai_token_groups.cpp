// AI32: the token groups of IslandInfo (docs/BOTS.md, "Islands"): power-ups that touch each other (8-connected) form a group whose members are listed in the order of a walk from the first entrance.
// AI32.1 pins that order against a plain re-statement of the walk (a search of the members already listed, which is what the analysis did before it kept a flag for each power-up) on clusters,
// lines, diagonal chains, blobs and scattered cells; AI32.2 pins the cost: one cluster of three thousand power-ups is analysed in a time that the cube of its size could not keep.
#include <algorithm>
#include <chrono>
#include <cmath>

#include "ai_test.hpp"
#include "ants_ai/island_info.hpp"

namespace {

using namespace ants;
using namespace ants::ai;

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
    } else {                                                                                    // "scatter": every second cell of every second row, none touches another
        for (uint32_t k = 0; k < n; ++k) cells.push_back(sim::TileCoord{static_cast<int32_t>(20 + 2 * (k % 40)), static_cast<int32_t>(20 + 2 * (k / 40))});
    }
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

}  // namespace

void run_token_group_tests() {
    TEST_CASE("AI32.1 The Members Of A Token Group Are In The Order Of A Walk From The First Entrance: Clusters, Lines, Diagonal Chains, Blobs And Scattered Cells Of 1 To 300 Power-Ups Agree With A Plain Re-Statement Of The Walk, Every Power-Up Is In One Group And Once") {
        for (const char* kind : {"cluster", "line", "diagonal", "blob", "scatter"}) {
            for (const uint32_t n : {1u, 2u, 3u, 9u, 50u, 150u, 300u}) {
                sim::SimulationEngine sim;
                world_with(sim, layout(kind, n));
                const MapInfo map(sim);
                const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
                const std::vector<PowerUpInfo>& powerups = map.powerups();
                ASSERT_EQ(powerups.size(), static_cast<size_t>(n));
                std::vector<int> times(powerups.size(), 0);
                size_t listed = 0;
                for (const TokenGroup& g : info.token_groups()) {
                    ASSERT_TRUE(!g.members.empty());
                    ASSERT_MSG(g.members == reference_members(powerups, g.members[0]), std::string(kind) + " " + std::to_string(n) + ": the members are the walk's order");
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
        ASSERT_MSG(ms < 400.0, "the analysis of 3,000 touching power-ups took " + std::to_string(ms) + " ms: the work must not grow with the cube of the group");
    } TEST_END();
}

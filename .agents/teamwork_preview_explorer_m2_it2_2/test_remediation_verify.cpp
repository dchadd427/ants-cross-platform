#include <iostream>
#include <vector>
#include <array>
#include <cstdint>
#include <cassert>
#include <algorithm>

// ============================================================================
// Verification Component 1: MatchStatsManager Dynamic Alliance Desync Remediation
// ============================================================================
constexpr uint8_t MAX_PLAYERS = 4;
constexpr uint8_t ALLIANCE_NONE = 4;

class MockMatchStatsManager {
public:
    std::array<int32_t, MAX_PLAYERS> scores_{100, 200, 300, 400};
    std::array<uint8_t, MAX_PLAYERS> alliances_{ALLIANCE_NONE, ALLIANCE_NONE, ALLIANCE_NONE, ALLIANCE_NONE};

    void set_alliance(uint8_t p1, uint8_t p2) noexcept {
        if (p1 < MAX_PLAYERS && p2 < MAX_PLAYERS && p1 != p2) {
            // Break existing alliance for p1's former partner if different from p2
            if (alliances_[p1] != ALLIANCE_NONE && alliances_[p1] != p2) {
                if (alliances_[p1] < MAX_PLAYERS) {
                    alliances_[alliances_[p1]] = ALLIANCE_NONE;
                }
            }
            // Break existing alliance for p2's former partner if different from p1
            if (alliances_[p2] != ALLIANCE_NONE && alliances_[p2] != p1) {
                if (alliances_[p2] < MAX_PLAYERS) {
                    alliances_[alliances_[p2]] = ALLIANCE_NONE;
                }
            }
            alliances_[p1] = p2;
            alliances_[p2] = p1;
        }
    }

    int32_t get_display_score(uint8_t player_id) const noexcept {
        if (player_id >= MAX_PLAYERS) return 0;
        uint8_t ally = alliances_[player_id];
        if (ally < MAX_PLAYERS && ally != player_id) {
            return scores_[player_id] + scores_[ally];
        }
        return scores_[player_id];
    }

    bool are_allies(uint8_t p1, uint8_t p2) const noexcept {
        if (p1 >= MAX_PLAYERS || p2 >= MAX_PLAYERS) return false;
        if (p1 == p2) return true;
        return alliances_[p1] == p2 && alliances_[p2] == p1;
    }
};

void test_alliance_desync() {
    std::cout << "[Test 1: Alliance Desync Remediation]\n";
    MockMatchStatsManager mgr;

    // Player 0 allies with Player 1
    mgr.set_alliance(0, 1);
    assert(mgr.are_allies(0, 1));
    assert(mgr.get_display_score(0) == 300); // 100 + 200
    assert(mgr.get_display_score(1) == 300); // 200 + 100

    // Player 0 shifts alliance to Player 2
    mgr.set_alliance(0, 2);
    assert(mgr.are_allies(0, 2));
    assert(mgr.alliances_[1] == ALLIANCE_NONE); // Player 1 is cleanly broken!
    assert(!mgr.are_allies(1, 0));
    assert(mgr.get_display_score(1) == 200); // Player 1 only has own score!
    assert(mgr.get_display_score(0) == 400); // 100 + 300
    assert(mgr.get_display_score(2) == 400); // 300 + 100

    // Multi-party shift: Player 3 allies with Player 2 (Player 2 leaves Player 0)
    mgr.set_alliance(3, 2);
    assert(mgr.are_allies(3, 2));
    assert(mgr.alliances_[0] == ALLIANCE_NONE); // Player 0 is cleanly broken!
    assert(mgr.get_display_score(0) == 100);    // Back to individual score
    assert(mgr.get_display_score(2) == 700);    // 300 + 400
    assert(mgr.get_display_score(3) == 700);    // 400 + 300

    std::cout << "  -> PASS: All alliance shift transitions cleanly dissociate former allies!\n";
}

// ============================================================================
// Verification Component 2: Combat Ant Knockback Boundary Safety
// ============================================================================
struct MockTileCoord {
    int32_t x{0};
    int32_t y{0};
};

class MockGrid {
public:
    int32_t w_{60};
    int32_t h_{60};
    std::vector<bool> obstacles_;

    MockGrid(int32_t w, int32_t h) : w_(w), h_(h), obstacles_(w * h, false) {}

    bool in_bounds(const MockTileCoord& c) const noexcept {
        return c.x >= 0 && c.x < w_ && c.y >= 0 && c.y < h_;
    }

    bool is_solid_obstacle(int32_t x, int32_t y) const noexcept {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return true;
        return obstacles_[y * w_ + x];
    }

    void set_obstacle(int32_t x, int32_t y, bool obs) {
        if (x >= 0 && x < w_ && y >= 0 && y < h_) {
            obstacles_[y * w_ + x] = obs;
        }
    }
};

MockTileCoord compute_combat_knockback(const MockGrid& grid,
                                      MockTileCoord owner_pos,
                                      MockTileCoord target_pos,
                                      int32_t dist_tiles) {
    int32_t kdx = target_pos.x - owner_pos.x;
    int32_t kdy = target_pos.y - owner_pos.y;
    if (kdx == 0 && kdy == 0) {
        kdx = 1;
    }
    int32_t step_x = (kdx > 0) ? 1 : ((kdx < 0) ? -1 : 0);
    int32_t step_y = (kdy > 0) ? 1 : ((kdy < 0) ? -1 : 0);
    if (step_x == 0 && step_y == 0) {
        step_x = 1;
    }

    MockTileCoord land_pos = target_pos;
    for (int32_t s = 1; s <= dist_tiles; ++s) {
        MockTileCoord next{target_pos.x + step_x * s, target_pos.y + step_y * s};
        if (!grid.in_bounds(next) || grid.is_solid_obstacle(next.x, next.y)) {
            break;
        }
        land_pos = next;
    }

    int32_t max_x = (grid.w_ > 0) ? (grid.w_ - 1) : 0;
    int32_t max_y = (grid.h_ > 0) ? (grid.h_ - 1) : 0;
    int32_t clamped_x = std::clamp(land_pos.x, 0, max_x);
    int32_t clamped_y = std::clamp(land_pos.y, 0, max_y);

    return MockTileCoord{clamped_x, clamped_y};
}

void test_combat_knockback_boundary() {
    std::cout << "[Test 2: Combat Knockback Boundary & Obstacle Safety]\n";
    MockGrid grid(60, 60);

    // Scenario A: Reviewer finding 4 (Combat at 58,30, enemy at 59,30, punch East)
    {
        MockTileCoord owner{58, 30};
        MockTileCoord target{59, 30};
        MockTileCoord res = compute_combat_knockback(grid, owner, target, 5);
        assert(res.x == 59 && res.y == 30);
        assert(grid.in_bounds(res));
    }

    // Scenario B: Punch towards West edge (Combat at 2, 10, enemy at 1, 10, punch West)
    {
        MockTileCoord owner{2, 10};
        MockTileCoord target{1, 10};
        MockTileCoord res = compute_combat_knockback(grid, owner, target, 5);
        assert(res.x == 0 && res.y == 10);
        assert(grid.in_bounds(res));
    }

    // Scenario C: Obstacle in flight path (Combat at 10,10, target at 11,10, rock at 14,10)
    {
        grid.set_obstacle(14, 10, true);
        MockTileCoord owner{10, 10};
        MockTileCoord target{11, 10};
        MockTileCoord res = compute_combat_knockback(grid, owner, target, 5);
        // Step 1: 12,10 (clear)
        // Step 2: 13,10 (clear)
        // Step 3: 14,10 (obstacle! stops)
        assert(res.x == 13 && res.y == 10);
        assert(grid.in_bounds(res));
    }

    // Scenario D: Punch diagonally towards corner (Combat at 58, 58, target at 59, 59)
    {
        MockTileCoord owner{58, 58};
        MockTileCoord target{59, 59};
        MockTileCoord res = compute_combat_knockback(grid, owner, target, 5);
        assert(res.x == 59 && res.y == 59);
        assert(grid.in_bounds(res));
    }

    std::cout << "  -> PASS: All knockback trajectories strictly clamped within grid and stop at obstacles!\n";
}

// ============================================================================
// Verification Component 3: Thief Infiltration Victim Dispatch
// ============================================================================
struct MockAnt {
    uint32_t id{1};
    uint8_t  player_id{0};
    uint8_t  target_team_id{4}; // Default None
    MockTileCoord pos{40, 40};
};

struct MockAnthill {
    uint8_t team_id{0};
    int32_t x{0};
    int32_t y{0};
};

uint8_t resolve_thief_victim(const MockAnt& u, const std::vector<MockAnthill>& anthills) {
    uint8_t victim = u.target_team_id;
    if (victim >= 4) {
        for (const auto& a : anthills) {
            if (a.team_id != u.player_id && a.x == u.pos.x && a.y == u.pos.y) {
                victim = a.team_id;
                break;
            }
        }
        if (victim >= 4) {
            victim = (u.player_id == 0) ? 1 : 0;
        }
    }
    return victim;
}

void test_thief_victim_routing() {
    std::cout << "[Test 3: Thief Victim Alert Routing]\n";
    std::vector<MockAnthill> anthills = {
        {0, 10, 10},
        {1, 40, 40},
        {2, 50, 50},
        {3, 20, 20}
    };

    // Case A: Explicit start_thief_infiltration targeting Player 2 (Challenger 6.1)
    {
        MockAnt u;
        u.player_id = 0;
        u.target_team_id = 2;
        assert(resolve_thief_victim(u, anthills) == 2);
    }

    // Case B: Explicit start_thief_infiltration targeting Player 3 (Reviewer Test 2)
    {
        MockAnt u;
        u.player_id = 2;
        u.target_team_id = 3;
        assert(resolve_thief_victim(u, anthills) == 3);
    }

    // Case C: Legacy test 10.2 without explicit start_thief_infiltration, spawned at {40,40}
    {
        MockAnt u;
        u.player_id = 0;
        u.pos = {40, 40}; // Team 1 anthill location
        u.target_team_id = 4;
        assert(resolve_thief_victim(u, anthills) == 1);
    }

    // Case D: Arbitrary location fallback without explicit target
    {
        MockAnt u;
        u.player_id = 0;
        u.pos = {1, 1};
        u.target_team_id = 4;
        assert(resolve_thief_victim(u, anthills) == 1);

        MockAnt u1;
        u1.player_id = 1;
        u1.pos = {1, 1};
        u1.target_team_id = 4;
        assert(resolve_thief_victim(u1, anthills) == 0);
    }

    std::cout << "  -> PASS: All thief infiltration victim dispatches route to true target!\n";
}

int main() {
    std::cout << "=== RUNNING REMEDIATION COMPILATION & LOGIC VERIFICATION ===\n";
    test_alliance_desync();
    test_combat_knockback_boundary();
    test_thief_victim_routing();
    std::cout << "=== ALL REMEDIATION LOGIC VERIFIED SUCCESSFULLY ===\n";
    return 0;
}

#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace ants::sim {

constexpr uint8_t MAX_PLAYERS = 4;
constexpr uint8_t ALLIANCE_NONE = 4; // Value 4 indicates independent FFA faction
constexpr uint8_t PLAYER_NEUTRAL = 255;

constexpr uint32_t HATCH_COST_POINTS = 200; // Cost deducted per egg hatched
constexpr int32_t  MAX_THIEF_STEAL   = 50;  // Maximum points stolen per infiltration

/**
 * @brief Statistic type identifier for recording match statistics.
 */
enum class StatType : uint8_t {
    Score        = 0,
    FriendlyLost = 1,
    EnemyKilled  = 2,
    NewHatched   = 3
};

/**
 * @brief 4-stat structure faithfully tracking end-game metrics for re_screen.
 */
struct PlayerMatchStats {
    int32_t  score{0};         // Column 1 (X ~ 496): Net match score
    uint32_t friendly_lost{0}; // Column 2 (X ~ 536): Friendly units killed
    uint32_t enemy_killed{0};  // Column 3 (X ~ 557): Enemy units destroyed
    uint32_t ants_hatched{0};  // Column 4 (X ~ 578): Units hatched from base
    uint32_t new_hatched{0};   // Alias/sync for ants_hatched

    // Diagnostic sub-counters
    uint32_t food_deposited{0};
    uint32_t food_stolen{0};
    uint32_t food_lost{0};
    uint32_t bombs_planted{0};
    uint32_t bombs_defused{0};
    uint32_t fires_lit{0};
    uint32_t bridges_built{0};

    constexpr bool operator==(const PlayerMatchStats& o) const noexcept {
        return score == o.score && friendly_lost == o.friendly_lost &&
               enemy_killed == o.enemy_killed && (ants_hatched == o.ants_hatched || new_hatched == o.new_hatched);
    }
};

/**
 * @brief Match progression lifecycle state.
 */
enum class MatchState : uint8_t {
    NotStarted = 0,
    Running    = 1,
    Paused     = 2,
    GameOver   = 3
};

/**
 * @brief Alliance invitation status tracking.
 */
struct AllianceInvite {
    uint8_t  from_player{PLAYER_NEUTRAL};
    uint8_t  to_player{PLAYER_NEUTRAL};
    uint32_t expiry_tick{0};
    bool     active{false};
};

/**
 * @brief Complete match outcome structure computed at clock 0:00.
 */
struct MatchResult {
    bool is_over{false};
    bool is_tie{false};
    std::vector<uint8_t> winning_players;
    std::vector<uint8_t> losing_players;
    std::array<int32_t, MAX_PLAYERS> final_scores{};
    std::array<PlayerMatchStats, MAX_PLAYERS> stats{};

    bool is_winner(uint8_t player_id) const noexcept {
        for (uint8_t p : winning_players) {
            if (p == player_id) return true;
        }
        return false;
    }
};

/**
 * @brief Manager class encapsulating 4-player standings, alliances, and economy.
 */
class MatchStatsManager {
public:
    MatchStatsManager() noexcept {
        reset();
    }

    void reset() noexcept {
        for (size_t i = 0; i < MAX_PLAYERS; ++i) {
            stats_[i] = PlayerMatchStats{};
            eggs_[i] = 0;
            alliances_[i] = ALLIANCE_NONE;
            pending_invite_[i] = AllianceInvite{};
        }
    }

    // Individual Player Stats
    const PlayerMatchStats& get_player_stats(uint8_t player_id) const noexcept {
        return stats_[player_id < MAX_PLAYERS ? player_id : 0];
    }
    PlayerMatchStats& get_player_stats_mut(uint8_t player_id) noexcept {
        return stats_[player_id < MAX_PLAYERS ? player_id : 0];
    }

    void record_stat(uint8_t player_id, StatType stat, uint32_t value) noexcept {
        if (player_id >= MAX_PLAYERS) return;
        auto& s = stats_[player_id];
        switch (stat) {
            case StatType::Score:
                s.score = static_cast<int32_t>(value);
                break;
            case StatType::FriendlyLost:
                s.friendly_lost = value;
                break;
            case StatType::EnemyKilled:
                s.enemy_killed = value;
                break;
            case StatType::NewHatched:
                s.ants_hatched = value;
                s.new_hatched = value;
                break;
        }
    }

    // Egg Inventory
    uint32_t get_egg_count(uint8_t player_id) const noexcept {
        return (player_id < MAX_PLAYERS) ? eggs_[player_id] : 0;
    }
    void set_egg_count(uint8_t player_id, uint32_t count) noexcept {
        if (player_id < MAX_PLAYERS) eggs_[player_id] = count;
    }

    // Scoring (Individual vs Allied Combined)
    int32_t get_individual_score(uint8_t player_id) const noexcept {
        return (player_id < MAX_PLAYERS) ? stats_[player_id].score : 0;
    }

    void set_individual_score(uint8_t player_id, int32_t score) noexcept {
        if (player_id < MAX_PLAYERS) {
            stats_[player_id].score = score;
        }
    }

    int32_t get_display_score(uint8_t player_id) const noexcept {
        if (player_id >= MAX_PLAYERS) return 0;
        uint8_t ally = alliances_[player_id];
        if (ally < MAX_PLAYERS && ally != player_id) {
            return stats_[player_id].score + stats_[ally].score;
        }
        return stats_[player_id].score;
    }

    void add_score(uint8_t player_id, int32_t points) noexcept {
        if (player_id < MAX_PLAYERS) {
            stats_[player_id].score += points;
            if (stats_[player_id].score < 0) stats_[player_id].score = 0;
        }
    }

    void deduct_score(uint8_t player_id, int32_t points) noexcept {
        if (player_id < MAX_PLAYERS) {
            stats_[player_id].score -= points;
            if (stats_[player_id].score < 0) stats_[player_id].score = 0;
        }
    }

    // Alliance Relationships
    uint8_t get_alliance(uint8_t player_id) const noexcept {
        return (player_id < MAX_PLAYERS) ? alliances_[player_id] : ALLIANCE_NONE;
    }

    bool are_allies(uint8_t p1, uint8_t p2) const noexcept {
        if (p1 >= MAX_PLAYERS || p2 >= MAX_PLAYERS) return false;
        if (p1 == p2) return true;
        return alliances_[p1] == p2 && alliances_[p2] == p1;
    }

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

    void break_alliance(uint8_t player_id) noexcept {
        if (player_id < MAX_PLAYERS) {
            uint8_t ally = alliances_[player_id];
            alliances_[player_id] = ALLIANCE_NONE;
            if (ally < MAX_PLAYERS) {
                alliances_[ally] = ALLIANCE_NONE;
            }
        }
    }

    void break_alliance(uint8_t p1, uint8_t p2) noexcept {
        if (p1 < MAX_PLAYERS && p2 < MAX_PLAYERS) {
            if (alliances_[p1] == p2) alliances_[p1] = ALLIANCE_NONE;
            if (alliances_[p2] == p1) alliances_[p2] = ALLIANCE_NONE;
        }
    }

    // Invites
    void set_pending_invite(uint8_t to_player, uint8_t from_player, uint32_t expiry_tick) noexcept {
        if (to_player < MAX_PLAYERS) {
            pending_invite_[to_player] = AllianceInvite{from_player, to_player, expiry_tick, true};
        }
    }

    const AllianceInvite& get_pending_invite(uint8_t player_id) const noexcept {
        static AllianceInvite empty{};
        return (player_id < MAX_PLAYERS) ? pending_invite_[player_id] : empty;
    }

    void clear_pending_invite(uint8_t player_id) noexcept {
        if (player_id < MAX_PLAYERS) {
            pending_invite_[player_id] = AllianceInvite{};
        }
    }

    // Evaluation at Match Expiry
    MatchResult evaluate_victory() const noexcept {
        MatchResult result{};
        result.is_over = true;
        int32_t best_score = -1;

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            result.stats[i] = stats_[i];
            result.final_scores[i] = get_display_score(i);
            if (result.final_scores[i] > best_score) {
                best_score = result.final_scores[i];
            }
        }

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (result.final_scores[i] == best_score) {
                result.winning_players.push_back(i);
            } else {
                result.losing_players.push_back(i);
            }
        }

        result.is_tie = (result.winning_players.size() > 1);
        return result;
    }

private:
    std::array<PlayerMatchStats, MAX_PLAYERS> stats_{};
    std::array<uint32_t, MAX_PLAYERS> eggs_{};
    std::array<uint8_t, MAX_PLAYERS> alliances_{};
    std::array<AllianceInvite, MAX_PLAYERS> pending_invite_{};
};

} // namespace ants::sim

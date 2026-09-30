#pragma once

#include <cstdint>
#include <array>
#include <utility>
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

/// "No team quit": the game-over message of the original carries the word -1 (FUN_010226c5(-1)) when the clock or the rules ended the match
constexpr uint16_t NO_QUITTER = 0xFFFF;

/**
 * @brief One row of the results screen (Ants.exe FUN_01015136): a team alone, or two allied teams together with the columns added up.
 */
struct ResultRow {
    uint8_t first{PLAYER_NEUTRAL};     // the team that made the row: the lower-numbered member of an alliance (the sort and the tie rule look at this one only)
    uint8_t second{PLAYER_NEUTRAL};    // its ally; PLAYER_NEUTRAL (the original's word 0xFFFF) when the row is one team
    int32_t score{0};                  // the teams' own scores added up (not the display score)
    int32_t friendly_lost{0};
    int32_t enemy_killed{0};
    int32_t new_hatched{0};
    bool has_second() const noexcept { return second != PLAYER_NEUTRAL; }
};

/**
 * @brief Complete match outcome: what the results screen is built from (Ants.exe FUN_010155ac, FUN_01015136).
 * The rows are not stored: every machine builds them for its own team (`rows(local)`), because a tie is broken in favour of the local team.
 */
struct MatchResult {
    bool is_over{false};
    std::vector<uint8_t> winning_players;     // the present teams whose own results screen would play the winner cue (is_winner)
    std::vector<uint8_t> losing_players;      // the other present teams
    std::array<int32_t, MAX_PLAYERS> final_scores{};     // the display score of each team (own score plus its ally's)
    std::array<PlayerMatchStats, MAX_PLAYERS> stats{};
    uint8_t present_mask{0x0F};               // the teams that have a row: in the match and not dropped (the builder skips NULL and dropped teams)
    std::array<uint8_t, MAX_PLAYERS> ally{ALLIANCE_NONE, ALLIANCE_NONE, ALLIANCE_NONE, ALLIANCE_NONE};   // each team's ally field (team +0x68)
    uint16_t quitter{NO_QUITTER};             // the team whose quit ended the match: its row goes last; NO_QUITTER when the clock or the rules ended it

    /// The alliances that both sides confirm: before the rows are built the screen clears every alliance that the other side does not return,
    /// team by team (FUN_010155ac, 0x1015609 - 0x1015645)
    std::array<uint8_t, MAX_PLAYERS> mutual_allies() const noexcept {
        std::array<uint8_t, MAX_PLAYERS> al = ally;
        for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
            if (((present_mask >> k) & 1u) == 0) continue;
            const uint8_t a = al[k];
            if (a == ALLIANCE_NONE) continue;
            if (a >= MAX_PLAYERS || al[a] != k) al[k] = ALLIANCE_NONE;
        }
        return al;
    }

    /// The rows of the results screen of team `local` (FUN_01015136): one row per present team, an alliance (both sides confirmed) in one row that the
    /// lower-numbered team makes; then the exchange sort of 0x1015316: a row moves in front of an earlier row when the earlier one belongs to the quitter,
    /// or has a lower score, or an equal score while the moving row is made by the local team (a row the quitter made never moves forward).
    std::vector<ResultRow> rows(uint8_t local) const {
        const std::array<uint8_t, MAX_PLAYERS> al = mutual_allies();
        std::vector<ResultRow> out;
        for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
            if (((present_mask >> k) & 1u) == 0) continue;
            ResultRow* row = nullptr;
            for (ResultRow& r : out) {
                if (al[k] == r.first) { row = &r; break; }
            }
            if (row == nullptr) {
                out.push_back(ResultRow{});
                row = &out.back();
                row->first = k;
            } else {
                row->second = k;
            }
            row->score += stats[k].score;
            row->friendly_lost += static_cast<int32_t>(stats[k].friendly_lost);
            row->enemy_killed += static_cast<int32_t>(stats[k].enemy_killed);
            row->new_hatched += static_cast<int32_t>(stats[k].new_hatched);
        }
        for (size_t i = 0; i < out.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                bool move_up;
                if (out[i].first == quitter) move_up = false;
                else if (out[j].first == quitter) move_up = true;
                else if (out[i].score != out[j].score) move_up = out[i].score > out[j].score;
                else move_up = out[i].first == local;
                if (move_up) std::swap(out[i], out[j]);
            }
        }
        return out;
    }

    /// The cue of team `player_id`'s results screen (0x1015a4a): the winner cue when its own team or its ally is the first team of the top row
    bool is_winner(uint8_t player_id) const {
        if (player_id >= MAX_PLAYERS) return false;
        const std::vector<ResultRow> r = rows(player_id);
        if (r.empty()) return false;
        return r[0].first == player_id || mutual_allies()[player_id] == r[0].first;
    }

    /// Fills winning_players / losing_players: every present team sorted by the cue that its own screen plays
    void decide_winners() {
        winning_players.clear();
        losing_players.clear();
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            if (((present_mask >> p) & 1u) == 0) continue;
            (is_winner(p) ? winning_players : losing_players).push_back(p);
        }
    }
};

/**
 * @brief A change of a player's score (deposit, theft, hatch cost). The original shows every change as a floating
 * "+N" / "-N" bubble at the player's home tile (Ants.exe FUN_01010cc9 -> FUN_01010560).
 */
struct ScoreChange {
    uint8_t player{0};
    int32_t delta{0};
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
        score_changes_.clear();
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

    // AddScore (Ants.exe 0x1010cc9) just adds: a score can go below zero (two thieves that raid a 60-point hill together take 100);
    // the score box draws 0 for it (FUN_01010452), the results show the real number.
    void add_score(uint8_t player_id, int32_t points) noexcept {
        if (player_id < MAX_PLAYERS) {
            stats_[player_id].score += points;
            if (points != 0) score_changes_.push_back(ScoreChange{player_id, points});
        }
    }


    // Score changes since the last call (consumed by the simulation to spawn score bubbles)
    std::vector<ScoreChange> take_score_changes() noexcept {
        std::vector<ScoreChange> out;
        out.swap(score_changes_);
        return out;
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

    // Evaluation at the end of the match: the result holds what the rows are built from; `present_mask` is the set of teams that are in the match
    // and have not dropped, `quitter` the team whose quit ended it.
    MatchResult evaluate_victory(uint8_t present_mask = 0x0F, uint16_t quitter = NO_QUITTER) const {
        MatchResult result{};
        result.is_over = true;
        result.present_mask = static_cast<uint8_t>(present_mask & 0x0Fu);
        result.quitter = quitter;
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            result.stats[i] = stats_[i];
            result.final_scores[i] = get_display_score(i);
            result.ally[i] = alliances_[i];
        }
        result.decide_winners();
        return result;
    }

private:
    std::vector<ScoreChange> score_changes_{};
    std::array<PlayerMatchStats, MAX_PLAYERS> stats_{};
    std::array<uint32_t, MAX_PLAYERS> eggs_{};
    std::array<uint8_t, MAX_PLAYERS> alliances_{};
    std::array<AllianceInvite, MAX_PLAYERS> pending_invite_{};
};

} // namespace ants::sim

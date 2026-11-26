#include "ants_app/match_report.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

// The numbers and the honors of the results screen (match_report.hpp), made from what the stats pass counted (ants_replay/match_stats.hpp). Pure: a function of its arguments, whole numbers only (a thousandth is an integer
// division, never a float), so that every machine that counted the same match says the same words.
//
// THE DEFINITIONS (the owner's; docs/REPLAYS.md "The match pages" has them too)
//   Winning side   the seats of the first row of result.rows(local_seat): one seat, or two for an alliance. The honors and the play are about every seat of the match, except the MVP (a seat of the winning side) and the Last stand (a
//                  seat that is not on it).
//   MVP            the seat of the winning side with the highest mvp_share (ties: the lower seat); no MVP when that share is 0. `value` is that share (thousandths). The three things are the damage done, the kills and the points that the seat's hill gained (MatchStats::banked, a thief's loot
//                  included, so loot is not a thing of its own).
//   First blood    the first kill, by turn and then by the order they were counted, whose killer is a real team that is not its victim's own. A death by drowning has no killer and is nobody's. None when there is none.
//   Top Thief      the seat with the most raids whose loot got home (stats.raids_home: a raid on a hill with nothing, and a Thief that was killed on the way, are begun and never home; more than 0; ties: the lower seat). The
//                  card says the raids and their points (stats.loot_home). Not in 187, which has the Last stand instead: of the seats that are not on the winning side and ran out of ants for good during the match
//                  (no ant, no egg, no hatch: stats.out_turn), the one that did LAST; none when none did. (A seat that was wiped out and came back from an egg did not run out.)
//   Play of the game  the best moment of one seat. A moment is a seat's own 10 seconds (200 turns): its kills from one kill (the first of the moment) to the last that is less than 200 turns after it, so there is one for every kill that
//                  the seat made, and the best of them is the Play. A kill is one of another seat's ant that the seat killed (the same kills as First blood's: not a drowned ant, not a team's own, not one that no team did). Points:
//                    10         for every kill
//                    +20        the kill that knocked a seat out: the last ant that the seat lost before it was out of the match (stats.out_turn: no ant, no egg, no hatch; a seat that was wiped out and came back from an egg
//                               was not out then), when that ant was killed by a seat
//                    +15        a Thief that was raiding the killer's own hill (it was ordered to raid it, or was at it, and had not taken the loot: Kill::raid_on) killed within 4 tiles of the mound of that hill (the 4 by 4 tiles
//                               of the hill, `hill_x` and `hill_y` of the stats pass; the distance is the larger of the two tile differences, 0 on the mound itself): the hill is saved
//                    +10        a Thief that carried the loot of a raid when it was killed (Kill::carried_loot), wherever
//                    +15        a group wiped out: a group is the ants of one seat within 4 tiles (MatchStats::kMatesTiles) of each other; a kill wipes one out when no other ant of its victim's seat is left within 4 tiles of the victim
//                               after the tick (Kill::mates_near is 0) and, counting that kill, at least 3 ants of that seat that the same seat killed within the moment, up to and including that kill, lay within 4 tiles of it; once for
//                               a place: not again for a kill within 4 tiles of an earlier wipe of the same victim seat in the moment. The bonus belongs to the moment, so a group whose first ants fell before the moment began is
//                               not wiped out in it.
//                  The best moment of the match is the one with the most points; ties go to the earlier moment (the turn of its first kill), then to the lower seat. There is no minimum and no bonus for speed: one kill is a play when it
//                  is the only kill of the match. None when no seat killed an ant of another seat. The play says the seat, its kills, the points, the seconds from the first kill to the last of the moment (rounded up, at least 1; 0 for a moment of
//                  one kill), the bonuses that it has and, for a knockout, the seat that it knocked out (the first of the moment). Its time and place (Watch opens the replay there) are the first kill's.
//   MVP share      mvp_share() below.
namespace ants::app {

namespace {

using replay::MatchStats;
constexpr size_t kSeats = MatchReport::kSeats;

constexpr uint32_t kMomentTurns = 10 * MatchReport::kTurnsPerSecond;                 // a moment is the 10 seconds from a kill on
constexpr uint32_t kKillPoints = 10;
constexpr uint32_t kKnockoutPoints = 20;
constexpr uint32_t kSavedHillPoints = 15;
constexpr uint32_t kStoppedLootPoints = 10;
constexpr uint32_t kWipedGroupPoints = 15;
constexpr size_t kGroupMin = 3;                                                        // a group is 3 ants or more (of one seat, within MatchStats::kMatesTiles tiles of each other)
constexpr int32_t kHillSide = 4;                                                       // a hill is a mound of 4 by 4 tiles (sim/grid.cpp, configure_anthill_cells)
constexpr int32_t kHillNearTiles = 4;                                                  // "near" a hill: within 4 tiles of its mound, as far as a bomb throws an ant

bool plays(uint8_t roster, size_t seat) { return seat < kSeats && ((roster >> seat) & 1u) != 0; }

// A kill that is somebody's: a real team that is not the victim's own did it (the pass leaves the killer of a team's own ant empty), and the ant did not drown
bool is_kill_of_a_seat(const MatchStats::Kill& kill) { return !kill.drowned && kill.killer < kSeats && kill.killer != kill.victim; }

int32_t distance(int32_t ax, int32_t ay, int32_t bx, int32_t by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

// ---- the Play of the game ----------------------------------------------------------------------------------------------------------------------------

// Whether the tile is within kHillNearTiles tiles of the mound of the seat's hill (0 away on the mound itself); a seat without a hill has no tile near it
bool near_hill(const MatchStats& st, uint8_t seat, int32_t x, int32_t y) {
    if (seat >= kSeats || st.hill_x[seat] < 0) return false;
    const int32_t hx = st.hill_x[seat];
    const int32_t hy = st.hill_y[seat];
    const int32_t dx = std::max({hx - x, 0, x - (hx + kHillSide - 1)});
    const int32_t dy = std::max({hy - y, 0, y - (hy + kHillSide - 1)});
    return std::max(dx, dy) <= kHillNearTiles;
}

// A kill of a seat with the points that it is worth by itself (10 and the bonuses that need nothing but the kill): the group's bonus depends on the moment, and is added when a moment is scored
struct Scored {
    const MatchStats::Kill* kill{nullptr};
    uint32_t points{0};
    uint8_t bonuses{0};
};

// The moment that begins with the kill `first` of a seat's kills (in the order they happened) and goes on while the kills are less than 10 seconds after it: `last` is the last of them
MatchReport::Play moment_of(const std::vector<Scored>& kills, size_t first, size_t last) {
    MatchReport::Play play;
    play.present = true;
    play.kills = static_cast<uint32_t>(last - first + 1);
    std::vector<const MatchStats::Kill*> wiped;                             // the kills of the moment that wiped a group out, so far
    bool knocked = false;
    for (size_t k = first; k <= last; ++k) {
        const MatchStats::Kill& kill = *kills[k].kill;
        play.points += kills[k].points;
        play.bonuses = static_cast<uint8_t>(play.bonuses | kills[k].bonuses);
        if ((kills[k].bonuses & MatchReport::kKnockedOut) != 0 && !knocked) {
            knocked = true;
            play.knocked_out = kill.victim;
        }
        if (kill.mates_near != 0) continue;
        const auto close = [&](const MatchStats::Kill& other) { return other.victim == kill.victim && distance(other.tile_x, other.tile_y, kill.tile_x, kill.tile_y) <= MatchStats::kMatesTiles; };
        size_t group = 0;
        for (size_t m = first; m <= k; ++m) group += close(*kills[m].kill) ? 1u : 0u;
        if (group < kGroupMin || std::any_of(wiped.begin(), wiped.end(), [&](const MatchStats::Kill* earlier) { return close(*earlier); })) continue;
        wiped.push_back(&kill);
        play.points += kWipedGroupPoints;
        play.bonuses = static_cast<uint8_t>(play.bonuses | MatchReport::kWipedGroup);
    }
    const MatchStats::Kill& begin = *kills[first].kill;
    const uint32_t span = kills[last].kill->turn - begin.turn;
    play.seconds = play.kills == 1 ? 0u : std::max(1u, (span + MatchReport::kTurnsPerSecond - 1) / MatchReport::kTurnsPerSecond);
    play.turn = begin.turn;
    play.tile_x = begin.tile_x;
    play.tile_y = begin.tile_y;
    return play;
}

// The Play of the game: the best moment of any seat (the most points; the earlier moment on a tie, then the lower seat)
MatchReport::Play make_play(const MatchStats& st) {
    // the kill that knocked a seat out: of the kills of an ant of a seat that ran out of ants for good, the last one at or before the turn that it did
    std::vector<bool> knockout(st.kills.size(), false);
    for (size_t seat = 0; seat < kSeats; ++seat) {
        if (!plays(st.roster, seat) || st.out_turn[seat] < 0) continue;
        for (size_t k = st.kills.size(); k-- > 0;) {
            if (st.kills[k].victim != seat || st.kills[k].turn > static_cast<uint32_t>(st.out_turn[seat])) continue;
            knockout[k] = is_kill_of_a_seat(st.kills[k]);
            break;
        }
    }
    // the kills of each seat, with what each is worth by itself
    std::array<std::vector<Scored>, kSeats> by_seat;
    for (size_t k = 0; k < st.kills.size(); ++k) {
        const MatchStats::Kill& kill = st.kills[k];
        if (!is_kill_of_a_seat(kill)) continue;
        Scored scored;
        scored.kill = &kill;
        scored.points = kKillPoints;
        if (knockout[k]) {
            scored.points += kKnockoutPoints;
            scored.bonuses = static_cast<uint8_t>(scored.bonuses | MatchReport::kKnockedOut);
        }
        if (kill.raid_on == kill.killer && near_hill(st, kill.killer, kill.tile_x, kill.tile_y)) {
            scored.points += kSavedHillPoints;
            scored.bonuses = static_cast<uint8_t>(scored.bonuses | MatchReport::kSavedHill);
        }
        if (kill.carried_loot) {
            scored.points += kStoppedLootPoints;
            scored.bonuses = static_cast<uint8_t>(scored.bonuses | MatchReport::kStoppedLoot);
        }
        by_seat[kill.killer].push_back(scored);
    }
    MatchReport::Play best;
    for (size_t seat = 0; seat < kSeats; ++seat) {
        std::vector<Scored>& kills = by_seat[seat];
        std::stable_sort(kills.begin(), kills.end(), [](const Scored& a, const Scored& b) { return a.kill->turn < b.kill->turn; });
        for (size_t first = 0, last = 0; first < kills.size(); ++first) {
            last = std::max(last, first);
            while (last + 1 < kills.size() && kills[last + 1].kill->turn - kills[first].kill->turn < kMomentTurns) ++last;
            MatchReport::Play moment = moment_of(kills, first, last);
            moment.seat = static_cast<uint8_t>(seat);
            if (!best.present || moment.points > best.points || (moment.points == best.points && moment.turn < best.turn)) best = moment;      // (strictly: of two equal moments the lower seat, then the earlier kill, stays)
        }
    }
    return best;
}

}  // namespace

uint32_t mvp_share(const replay::MatchStats& st, uint8_t seat) {
    if (!plays(st.roster, seat)) return 0;
    // the three things, for every seat: damage done, kills (a drowned ant is nobody's), points banked (the hill's gain, a thief's loot included: loot is not a thing of its own, it would count twice; never below 0)
    std::array<std::array<int64_t, kSeats>, 3> things{};
    for (size_t s = 0; s < kSeats; ++s) {
        things[0][s] = st.damage_dealt[s];
        things[2][s] = std::max<int64_t>(0, st.banked[s]);
    }
    for (const MatchStats::Kill& kill : st.kills) {
        if (is_kill_of_a_seat(kill)) ++things[1][kill.killer];
    }
    int64_t sum = 0;
    int64_t done = 0;                                                        // the things that somebody did
    for (const auto& thing : things) {
        int64_t total = 0;
        for (size_t s = 0; s < kSeats; ++s) total += plays(st.roster, s) ? thing[s] : 0;
        if (total <= 0) continue;
        ++done;
        sum += thing[seat] * 1000 / total;
    }
    return done == 0 ? 0u : static_cast<uint32_t>(sum / done);
}

MatchReport make_match_report(const replay::MatchStats& st, const sim::MatchResult& result, sim::GameMode mode, uint8_t local_seat) {
    MatchReport report;
    report.turns = st.turns;
    if (!st.ok) return report;
    report.ok = true;
    report.roster = st.roster;

    // the totals
    for (size_t s = 0; s < kSeats; ++s) {
        report.damage_done[s] = st.damage_dealt[s];
        report.damage_taken[s] = st.damage_taken[s];
        report.food_trips[s] = st.food_trips[s];
        report.food_points[s] = st.food_points[s];
        report.bombs_set[s] = st.bombs_planted[s];
        report.fires_lit[s] = st.fires_lit[s];
        report.raids_home[s] = st.raids_home[s];
        report.loot_home[s] = st.loot_home[s];
    }
    for (const MatchStats::Raid& raid : st.raids) {
        if (raid.thief >= kSeats) continue;
        ++report.raids[raid.thief];
        report.loot[raid.thief] += raid.points;
    }

    // the winning side: the seats of the first row of the results
    const std::vector<sim::ResultRow> rows = result.rows(local_seat);
    if (!rows.empty()) {
        if (rows[0].first < kSeats) report.winners = static_cast<uint8_t>(report.winners | (1u << rows[0].first));
        if (rows[0].has_second() && rows[0].second < kSeats) report.winners = static_cast<uint8_t>(report.winners | (1u << rows[0].second));
    }
    report.winners = static_cast<uint8_t>(report.winners & st.roster);

    // MVP: the highest mixed share of the winning side
    uint32_t best = 0;
    for (size_t s = 0; s < kSeats; ++s) {
        if (((report.winners >> s) & 1u) == 0) continue;
        const uint32_t share = mvp_share(st, static_cast<uint8_t>(s));
        if (share > best) {                                                 // (strictly: the lower seat keeps a tie)
            best = share;
            report.mvp = MatchReport::Honor{true, static_cast<uint8_t>(s), static_cast<int64_t>(share), 0};
        }
    }

    // First blood: the first kill that is a seat's, by turn, and of kills of the same turn the one counted first
    for (const MatchStats::Kill& kill : st.kills) {
        if (!is_kill_of_a_seat(kill)) continue;
        if (!report.first_blood.present || kill.turn < report.first_blood.turn) report.first_blood = MatchReport::Honor{true, kill.killer, 0, kill.turn};      // (strictly: of two on one turn the earlier stays)
    }

    if (mode == sim::GameMode::Kills187) {
        // Last stand: of the seats that did not win and ran out of ants for good, the one that did last
        int32_t latest = -1;
        for (size_t s = 0; s < kSeats; ++s) {
            if (!plays(st.roster, s) || ((report.winners >> s) & 1u) != 0 || st.out_turn[s] < 0) continue;
            if (st.out_turn[s] > latest) {                                  // (strictly: the lower seat keeps a tie)
                latest = st.out_turn[s];
                report.last_stand = MatchReport::Honor{true, static_cast<uint8_t>(s), 0, static_cast<uint32_t>(st.out_turn[s])};
            }
        }
    } else {
        // Top Thief: the seat with the most raids whose loot got home, when any did
        uint32_t most = 0;
        for (size_t s = 0; s < kSeats; ++s) {
            if (st.raids_home[s] <= most) continue;                         // (strictly: the lower seat keeps a tie)
            most = st.raids_home[s];
            report.top_thief = MatchReport::Honor{true, static_cast<uint8_t>(s), static_cast<int64_t>(most), 0};
        }
    }

    report.play = make_play(st);
    return report;
}

}  // namespace ants::app

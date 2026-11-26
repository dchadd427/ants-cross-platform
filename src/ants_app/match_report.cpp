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
//   Play of the game  the busiest 10 seconds that have a play to tell: two neighboring buckets of 100 turns (b and b + 1, for every b; a bucket that the match did not reach is empty) with the most activity, the activity being
//                  the sum of count times weight (death 14, bomb 10, theft 6, fire 5, hit 2: the activity curve's). The earlier window wins a tie; a window under 28 (two deaths) is no window; when the busiest has no play to tell
//                  (see below), the next busiest is tried, and so on. The events of the window belong to seats: a death to the team of the last damage
//                  (none when it drowned, or the team was its own), a bomb to its planter, a theft to its thief, a fire to the seat that lit it, a hit to the team of the damage (never to the seat that was hurt). The seat with the most weighted
//                  activity in the window gets the play (ties: the lower seat); its biggest KIND of event, by weighted activity (ties: the order death, bomb, theft, fire, hit), says the kind and the count (a kind that counts
//                  nothing, a bomb that took out nobody or a fight without a kill of its own, is no play: the seat's next kind, then the next seat, else no play in this window, so the sentence never says "0"):
//                    bomb -> Bomb   the ants of other seats that died within 20 turns after, and within 4 tiles of, one of its bombs that went off in the window; the busiest of those bombs is the moment (the earlier of two equal ones)
//                    theft -> Raid  the points that its raids of the window took; the raid with the most points is the moment
//                    death, hit -> Fight   the kills that it made in the window; the middle one is the moment: a window of blows alone, or of the kills of others, has no place to show and is no play for the seat
//                    fire -> Fire   the ants of other seats that died in the window within 3 tiles of a wall that it lit, from the turn it lit it to 180 seconds after; the fire of the window with the most of them is the moment
//                  A distance is the larger of the two tile differences.
//                  HONEST LIMIT: the pass does not know what caused a hit or a death, so these counts are "near in time and place", not "caused by"; a bomb does not claim the ant that a fire in the same place killed.
//   MVP share      mvp_share() below.
namespace ants::app {

namespace {

using replay::MatchStats;
constexpr size_t kSeats = MatchReport::kSeats;
constexpr size_t kKinds = MatchStats::kActionKinds;

constexpr uint32_t kMinPlayScore = 28;                    // the activity of two deaths
constexpr uint32_t kBombReachTurns = 20;                  // a victim of a bomb died within a second of it ...
constexpr int32_t kBombReachTiles = 4;                    // ... and within the 4 tiles that a bomb throws an ant
constexpr int32_t kFireReachTiles = 3;
constexpr uint32_t kFireLifeTurns = 180 * MatchReport::kTurnsPerSecond;       // a fire wall burns for 3 minutes (ability_system.cpp end_ignite)
// The weight of one event of each kind, in the order of MatchStats::Action (the activity curve's: docs/HEATMAPS.md "The activity curve")
constexpr std::array<uint32_t, kKinds> kWeight = {14, 10, 6, 5, 2};
constexpr size_t kDeath = static_cast<size_t>(MatchStats::Action::Death);
constexpr size_t kBomb = static_cast<size_t>(MatchStats::Action::Bomb);
constexpr size_t kTheft = static_cast<size_t>(MatchStats::Action::Theft);
constexpr size_t kFire = static_cast<size_t>(MatchStats::Action::Fire);
constexpr size_t kHit = static_cast<size_t>(MatchStats::Action::Hit);

bool plays(uint8_t roster, size_t seat) { return seat < kSeats && ((roster >> seat) & 1u) != 0; }

// A kill that is somebody's: a real team that is not the victim's own did it (the pass leaves the killer of a team's own ant empty), and the ant did not drown
bool is_kill_of_a_seat(const MatchStats::Kill& kill) { return !kill.drowned && kill.killer < kSeats && kill.killer != kill.victim; }

int32_t distance(int32_t ax, int32_t ay, int32_t bx, int32_t by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

bool in_window(uint32_t turn, uint32_t lo, uint32_t hi) { return turn >= lo && turn < hi; }

// ---- the Play of the game ----------------------------------------------------------------------------------------------------------------------------

// The play of one window (the bucket `best_bucket` and the next); present is false when nobody did anything there that counts something
MatchReport::Play play_in_window(const MatchStats& st, size_t best_bucket) {
    MatchReport::Play none;
    const uint32_t lo = static_cast<uint32_t>(best_bucket) * MatchStats::kSampleTurns;
    const uint32_t hi = lo + 2 * MatchStats::kSampleTurns;

    // 2. whose events they are
    std::array<std::array<uint32_t, kKinds>, kSeats> events{};
    for (const MatchStats::Kill& kill : st.kills) {
        if (is_kill_of_a_seat(kill) && in_window(kill.turn, lo, hi)) ++events[kill.killer][kDeath];
    }
    for (const MatchStats::BombOff& bomb : st.bomb_events) {
        if (bomb.owner < kSeats && in_window(bomb.turn, lo, hi)) ++events[bomb.owner][kBomb];
    }
    for (const MatchStats::Raid& raid : st.raids) {
        if (raid.thief < kSeats && in_window(raid.turn, lo, hi)) ++events[raid.thief][kTheft];
    }
    for (const MatchStats::FireLit& fire : st.fire_events) {
        if (fire.seat < kSeats && in_window(fire.turn, lo, hi)) ++events[fire.seat][kFire];
    }
    for (size_t b = best_bucket; b < best_bucket + 2 && b < st.hit_credit.size(); ++b) {
        for (size_t seat = 0; seat < kSeats; ++seat) events[seat][kHit] += st.hit_credit[b][seat];
    }
    const auto weighted = [&](size_t seat, size_t kind) { return events[seat][kind] * kWeight[kind]; };

    // 3. what it was, how many, and when and where: one candidate for a seat and a kind of play, which is only a play when it counts something (a bomb that took out nobody is no play to say "takes out 0 ants" of)
    const auto candidate = [&](size_t seat, MatchReport::PlayKind kind, MatchReport::Play& play) -> bool {
        play = MatchReport::Play{};
        play.seat = static_cast<uint8_t>(seat);
        play.kind = kind;
        if (kind == MatchReport::PlayKind::Bomb) {
            std::vector<const MatchStats::BombOff*> bombs;
            for (const MatchStats::BombOff& bomb : st.bomb_events) {
                if (bomb.owner == seat && in_window(bomb.turn, lo, hi)) bombs.push_back(&bomb);
            }
            if (bombs.empty()) return false;
            const auto victim_of = [&](const MatchStats::Kill& kill, const MatchStats::BombOff& bomb) {
                return !kill.drowned && kill.victim != seat && kill.turn >= bomb.turn && kill.turn - bomb.turn <= kBombReachTurns && distance(kill.tile_x, kill.tile_y, bomb.tile_x, bomb.tile_y) <= kBombReachTiles;
            };
            uint32_t busiest = 0;
            const MatchStats::BombOff* moment = bombs.front();
            for (const MatchStats::BombOff* bomb : bombs) {
                uint32_t taken = 0;
                for (const MatchStats::Kill& kill : st.kills) taken += victim_of(kill, *bomb) ? 1u : 0u;
                if (taken > busiest) {
                    busiest = taken;
                    moment = bomb;
                }
            }
            for (const MatchStats::Kill& kill : st.kills) {                 // (an ant near two bombs is counted once)
                play.count += std::any_of(bombs.begin(), bombs.end(), [&](const MatchStats::BombOff* bomb) { return victim_of(kill, *bomb); }) ? 1u : 0u;
            }
            play.turn = moment->turn;
            play.tile_x = moment->tile_x;
            play.tile_y = moment->tile_y;
        } else if (kind == MatchReport::PlayKind::Raid) {
            const MatchStats::Raid* moment = nullptr;
            for (const MatchStats::Raid& raid : st.raids) {
                if (raid.thief != seat || !in_window(raid.turn, lo, hi)) continue;
                play.count += static_cast<uint32_t>(std::max<int32_t>(0, raid.points));
                if (moment == nullptr || raid.points > moment->points) moment = &raid;
            }
            if (moment == nullptr) return false;
            play.turn = moment->turn;
            play.tile_x = moment->tile_x;
            play.tile_y = moment->tile_y;
        } else if (kind == MatchReport::PlayKind::Fire) {
            std::vector<const MatchStats::FireLit*> fires;                  // the seat's fires, whenever they were lit (one still burns); the moment is one of the window's
            for (const MatchStats::FireLit& fire : st.fire_events) {
                if (fire.seat == seat && fire.has_tile) fires.push_back(&fire);
            }
            const auto victim_of = [&](const MatchStats::Kill& kill, const MatchStats::FireLit& fire) {
                return !kill.drowned && kill.victim != seat && in_window(kill.turn, lo, hi) && kill.turn >= fire.turn && kill.turn - fire.turn <= kFireLifeTurns &&
                       distance(kill.tile_x, kill.tile_y, fire.tile_x, fire.tile_y) <= kFireReachTiles;
            };
            const MatchStats::FireLit* moment = nullptr;
            uint32_t busiest = 0;
            for (const MatchStats::FireLit* fire : fires) {
                if (!in_window(fire->turn, lo, hi)) continue;
                uint32_t taken = 0;
                for (const MatchStats::Kill& kill : st.kills) taken += victim_of(kill, *fire) ? 1u : 0u;
                if (moment == nullptr || taken > busiest) {
                    busiest = taken;
                    moment = fire;
                }
            }
            if (moment == nullptr) return false;                            // (the fires of the window left no wall: no place to show)
            for (const MatchStats::Kill& kill : st.kills) {
                play.count += std::any_of(fires.begin(), fires.end(), [&](const MatchStats::FireLit* fire) { return victim_of(kill, *fire); }) ? 1u : 0u;
            }
            play.turn = moment->turn;
            play.tile_x = moment->tile_x;
            play.tile_y = moment->tile_y;
        } else {                                                            // deaths and hits: a fight, the kills that the seat made in the window
            std::vector<const MatchStats::Kill*> made;
            for (const MatchStats::Kill& kill : st.kills) {
                if (is_kill_of_a_seat(kill) && kill.killer == seat && in_window(kill.turn, lo, hi)) made.push_back(&kill);
            }
            if (made.empty()) return false;                                 // (only blows, or the kills of others: nothing of its own to show)
            play.count = static_cast<uint32_t>(made.size());
            const MatchStats::Kill& middle = *made[(made.size() - 1) / 2];
            play.turn = middle.turn;
            play.tile_x = middle.tile_x;
            play.tile_y = middle.tile_y;
        }
        play.present = play.count > 0;
        return play.present;
    };

    // 4. the seats that did something, the one with the most weighted activity first (the lower seat on a tie); for each, its kinds of event, the biggest first (the earlier kind on a tie); the first that counts something is the play
    std::vector<size_t> seats;
    for (size_t s = 0; s < kSeats; ++s) {
        uint32_t sum = 0;
        for (size_t k = 0; k < kKinds; ++k) sum += weighted(s, k);
        if (sum > 0) seats.push_back(s);                                    // (a window that nobody did: deaths by drowning, hits of no team, has none)
    }
    const auto total_of = [&](size_t s) {
        uint32_t sum = 0;
        for (size_t k = 0; k < kKinds; ++k) sum += weighted(s, k);
        return sum;
    };
    std::stable_sort(seats.begin(), seats.end(), [&](size_t a, size_t b) { return total_of(a) > total_of(b); });
    for (const size_t seat : seats) {
        std::vector<size_t> kinds;
        for (size_t k = 0; k < kKinds; ++k) {
            if (weighted(seat, k) > 0) kinds.push_back(k);
        }
        std::stable_sort(kinds.begin(), kinds.end(), [&](size_t a, size_t b) { return weighted(seat, a) > weighted(seat, b); });
        std::vector<MatchReport::PlayKind> tried;
        for (const size_t k : kinds) {
            const MatchReport::PlayKind kind = k == kBomb ? MatchReport::PlayKind::Bomb : k == kTheft ? MatchReport::PlayKind::Raid : k == kFire ? MatchReport::PlayKind::Fire : MatchReport::PlayKind::Fight;
            if (std::find(tried.begin(), tried.end(), kind) != tried.end()) continue;      // (a death and a hit are both a fight)
            tried.push_back(kind);
            MatchReport::Play play;
            if (candidate(seat, kind, play)) return play;
        }
    }
    return none;
}

// The Play of the game: of the windows that have at least kMinPlayScore of activity, the busiest (the earlier on a tie) that has a play to tell; when its seats did nothing that counts (a bomb that took out
// nobody, hits and no kill), the next busiest, and so on, so that a match in which anything real happened has a card
MatchReport::Play make_play(const MatchStats& st) {
    size_t buckets = st.hit_credit.size();
    for (const std::vector<uint32_t>& series : st.action) buckets = std::max(buckets, series.size());
    const auto count_at = [&](size_t kind, size_t bucket) -> uint32_t { return bucket < st.action[kind].size() ? st.action[kind][bucket] : 0u; };
    std::vector<std::pair<uint32_t, size_t>> windows;                       // (the activity, the bucket b: the window is b and b + 1)
    for (size_t b = 0; b < buckets; ++b) {
        uint32_t score = 0;
        for (size_t k = 0; k < kKinds; ++k) score += kWeight[k] * (count_at(k, b) + count_at(k, b + 1));
        if (score >= kMinPlayScore) windows.emplace_back(score, b);
    }
    std::stable_sort(windows.begin(), windows.end(), [](const std::pair<uint32_t, size_t>& x, const std::pair<uint32_t, size_t>& y) { return x.first > y.first; });       // (stable: of two equal windows the earlier stays first)
    for (const auto& window : windows) {
        const MatchReport::Play play = play_in_window(st, window.second);
        if (play.present) return play;
    }
    return MatchReport::Play{};
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
    if (!st.ok || !st.events_ok) return report;
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

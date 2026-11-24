#pragma once

// The `details` of a match's record (docs/REPLAYS.md "The match pages"): what the replay pass counted (ants_replay/match_stats.hpp) as the small JSON that `history_matches.details` keeps and
// GET /history/<id> gives back under "details". Pure: no clock, no disk, no database. The page reads it defensively (a plain object, every key it does not know skipped), so a later version adds keys.
//
// Layout v1 (the key "v"; seats are indexed 0 Green, 1 Red, 2 Blue, 3 Black, an array for a seat that did not play is []):
//   turns, step            the match's length in turns, and the turns between two points of the series below (15 s, more for a very long match: at most kDetailsMaxPoints points)
//   over_time              {score: [[..] x4], kills: [[..] x4]}  point i is after (i + 1) * step turns; score is the seat's own score (absent in game mode 187, whose score is the kills)
//   highlights             {first_blood: {t, by, of}, last_kill: {..}, lead: [[t, seat, ally or -1] ..] (who had the most on the score box, by sides), comeback: {seat, ally?, behind, t} (the winning side's
//                           biggest deficit), wiped_out: [[seat, t, times] ..], out: [[seat, t] ..], burst: {seat, kills, t0, t1} (3 or more kills in a minute)}; each only when it happened
//   activity               {per_minute: [[..] x4], by_kind: {kind: [x4]}, ants: [{type: orders that name it} x4]}
//   combat                 {kills: [[..] x4] (killer row, victim column), ev: [[t, by or -1, of, type, drowned] ..] (the first kDetailsMaxEvents; ev_n says how many there were when more), dealt, taken (x4),
//                           hp: [[..] x4] (the team of the damage row, the seat that lost the points), raids: [[t, thief, victim, points] ..] (capped likewise; raids_n), stolen, robbed (x4)}
//   economy                {food: [[trips, points] x4], banked, hatch_cost, bombs_off (x4), taken: [[bomber, fire, thief, combat, swimmer] x4]}
//   special                {built, undug, out (x4: bridges built, bridges dug away, fires put out), raids: [[begun, home, loot that got home] x4]}  (the counts of the Special ants section; only in a match counted by a build that
//                           has them; about 150 bytes whatever the match, so it stays when combat and economy go and is left out only by the very last try: the page shows what a record has and hides what it lacks)
//   heavy_orders          [seat ..]  only when a seat's orders went over their budget in the match (the "heavy order rate" mark: net::Sequencer::Config::order_burst); the marker has it too
//   largest_sender        seat       only in the marker of a match whose recording passed its size limit: the seat that had sent the most bytes of commands into it (replay::Recorder::largest_sender)
// A match that the pass could not count has the marker {"v":1,"n":1} (so that it is not tried again at every start); the page shows no stats for it. A match that was played and has no recording (the file could not
// be written) has the marker too: its numbers are the referee's own, kept in the record.
// The text is at most kDetailsMaxBytes, always: when a very long match with many events is larger, the lists of events go first, then the points of the series get fewer, then combat and economy go (special stays unless nothing else would fit);
// a text that is still too long (a match of hours with numbers of ten digits) is the marker.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ants_replay/match_stats.hpp"
#include "ants_server/history_record.hpp"

namespace ants::server {

/// The layout number of `details` (the key "v") that this build writes
inline constexpr int64_t kDetailsVersion = 1;
inline constexpr size_t kDetailsMaxBytes = 5000;
inline constexpr size_t kDetailsMaxPoints = 48;       // points of each series of the chart
inline constexpr size_t kDetailsMaxEvents = 30;       // events in each list (kills, raids)

/// The details of a counted match as JSON text; "" when the pass did not count it (`stats.ok` false). `record` supplies who won (for the comeback).
std::string make_history_details(const replay::MatchStats& stats, const HistoryRecord& record);

/// The text that stands for "the pass tried and could not count this match" (`heavy_orders`: the seats with the mark of a heavy order rate, as in HistoryRecord; 0: none, and the text is the plain marker)
/// `largest_sender`: the seat (0 to 3) that sent the most bytes of commands into a recording that failed on its size, -1 for none or another failure; the marker has the key "largest_sender" then
std::string history_details_marker(uint8_t heavy_orders = 0, int largest_sender = -1);

}  // namespace ants::server

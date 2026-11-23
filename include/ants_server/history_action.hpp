#pragma once

// The action of a match (docs/REPLAYS.md "The activity curve"): how many deaths, bombs, thefts, fires and hits happened in every few seconds of a match, found by the replay pass (ants_replay/match_stats.hpp:
// `MatchStats::action`) and kept in a small field of its own next to the `details` of a match's record, with a cap of its own (kActionMaxBytes), so that the cap of the details (kDetailsMaxBytes) is not touched. The
// replay page draws the curve over its timeline from it, with the weights of the Auto camera (the page's own table: a weight can be tuned without counting a match again). Pure: no clock, no disk, no database. The
// page (web/replay_activity.js) reads it as data from a server: every number is clamped, every character is checked.
//
// Layout v1 (the JSON object that GET /history/<id> gives under "action"):
//   v        1
//   s        the seconds in one bucket: 5, then 10, 20, 40 ... (5 times a power of two: the smallest that makes the match fit kActionMaxBuckets buckets; a match of up to 20 minutes has 5)
//   n        how many buckets (1 to kActionMaxBuckets): bucket i is the seconds s i to s i + s of the match; the last tick of a match that ends on the edge of a bucket is counted in the last bucket
//   death, bomb, theft, fire, hit
//            one text of n letters for each kind: the count of that kind in the bucket as one base64url letter (A-Z a-z 0-9 - _: A is 0, _ is 63), never more than kActionMaxCount (a bucket with more says 63).
//            A death is an ant that was lost, a bomb one that went off, a theft a Thief that began to raid a hill, a fire one that a Fire Ant lit, a hit an ant that lost hit points in a tick.
// A match in which none of the five happened, and one that the pass could not count, has no field at all (the column is empty; the page then draws what the deaths alone say, or nothing). The text is at most
// kActionMaxBytes, always: five texts of at most kActionMaxBuckets letters and a short head.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_replay/match_stats.hpp"

namespace ants::server {

inline constexpr int64_t kActionVersion = 1;
inline constexpr size_t kActionMaxBytes = 1500;           // the field of one match, as JSON text (the longest one is 5 x 240 letters and about 60 characters)
inline constexpr size_t kActionMaxBuckets = 240;          // buckets in a field: 20 minutes of 5 seconds, then the buckets double
inline constexpr uint32_t kActionMaxCount = 63;           // what one letter holds
inline constexpr uint32_t kActionBaseSeconds = 5;         // the smallest bucket (MatchStats::kSampleTurns turns at 20 a second)
inline constexpr uint32_t kActionMaxSeconds = 5u << 20;   // what a field may say a bucket is (the page and the reader refuse more)

/// What a field holds, read back: `counts[kind]` has `buckets` entries (0 to kActionMaxCount), the kinds in the order of MatchStats::Action
struct ActionView {
    uint32_t seconds{0};
    uint32_t buckets{0};
    std::array<std::vector<uint8_t>, replay::MatchStats::kActionKinds> counts;
};

/// The field of a counted match as JSON text; "" when there is nothing to keep (the pass did not count it, or nothing of the five kinds happened)
std::string make_history_action(const replay::MatchStats& stats);

/// Reads a field back; false for text that is not a field of this version (another version, a size or a count that is not a number in range, a text that is not n letters of the 64)
bool parse_history_action(const std::string& text, ActionView& out);

}  // namespace ants::server

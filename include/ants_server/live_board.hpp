#pragma once

// The matches that run now, for the public door (docs/SERVER.md "Replays", the live routes; docs/REPLAYS.md "On the game server"): a live match is a replay that is still growing, read over HTTP. A room that
// records its match registers it here when the recording begins (Room::replay_begin) and takes it off when the recording becomes a file or is dropped (Room::replay_end, or the room's destructor), so the
// board holds a raw pointer to the room's recorder only for as long as the room has it. The board is single threaded like the rest of the server and is owned by the RoomManager next to the replay store. A match
// played with Fog of War is not registered (a snapshot that anybody can read while it runs would show what the fog hides from its players; the match is public when it is over, like any other).
//
// What it gives the door:
//  * an id for every registered match, which is the only thing a visitor ever names a match by: `<MAP>-<YYYYMMDD>-<HHMMSS>Z`, the stem of the map's file name and the UTC second in which the match began, with
//    `-2`, `-3` ... when two matches of one map began in one second (the id is unique among the matches that run and the ones that ended lately). It is made of nothing else: never of the room's code, a
//    name, an address or a key;
//  * the list: the matches that have run at least kReplayMinTurns (the length that the store keeps; a shorter one may never be a replay) and whose recording has not failed, newest start first, at most 50, with
//    the players in the strings of the replay list ("Green (Ann)", "Green", "Red (Bot (Medium))");
//  * the snapshot of one of them: the recorder's incomplete file with its `live` chunk (Recorder::snapshot), cached for as long as the board's clock stays in one second, so a crowd of viewers costs one
//    snapshot a second and the same bytes go to all of them meanwhile;
//  * the fate of a match that ended lately: the name that the store kept it under ("" when it was not kept), for 15 minutes, 256 matches at most, so that a page that watched it finds the replay when it ends.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ants::replay {
class Recorder;
}

namespace ants::server {

/// One match of the list
struct LiveMatch {
    std::string id;
    std::string map;                                 // the map's file name as the match named it ("TREASURE.LVL")
    int64_t started{0};                              // seconds since 1970 UTC when the recording began
    uint32_t turns{0};                               // the turns run so far
    std::vector<std::string> players;                // the seats that play, "Green (Ann)", "Green" (no name) or "Red (Bot (Medium))", in seat order
};

class LiveBoard {
public:
    static constexpr size_t kMaxListed = 50;                 // the list gives the newest 50
    static constexpr int64_t kSnapshotMaxAgeS = 1;           // a snapshot is handed out again for as long as the clock is in the second in which it was made; it is made again when a second or more has passed
    static constexpr int64_t kRememberS = 15 * 60;           // an ended match is remembered this long ...
    static constexpr size_t kMaxRemembered = 256;            // ... and this many are, the oldest forgotten first
    static constexpr unsigned kMaxSameSecond = 9999;         // the "-2" ... "-9999" of the id (the page's pattern holds four digits)

    /// `clock_s`: seconds since 1970 UTC (empty: the system clock); the tests give their own
    explicit LiveBoard(std::function<int64_t()> clock_s = nullptr);
    LiveBoard(const LiveBoard&) = delete;
    LiveBoard& operator=(const LiveBoard&) = delete;

    /// Takes the clock from `clock_s` (empty: the system clock) from now on
    void set_clock(std::function<int64_t()> clock_s);

    /// Registers a match whose recording has just begun (no turn has run). The recorder must stay alive until end() is called with the id. Returns the id; empty when the match cannot be named (every one of the 9999
    /// ids of its second is taken) or is played with Fog of War (what the other sides do is hidden from a player, and a live view would not hide it), and then it is not on the board.
    std::string begin(const replay::Recorder& recorder);
    /// Takes the match off the board: the recording became a file (`kept_file` is the name the store gave it) or was dropped (`kept_file` empty). The match is remembered as ended when it had run kReplayMinTurns, and
    /// forgotten at once otherwise (the list never held it). An id that is not on the board is ignored.
    void end(const std::string& id, const std::string& kept_file);

    /// The matches that run, newest start first (see above)
    std::vector<LiveMatch> list() const;
    /// The bytes of the match's snapshot, null when the id is not that of a listed match (the same rule as the list's) or the recorder cannot make one. Valid until the board is next used.
    const std::vector<uint8_t>* snapshot(const std::string& id) const;
    /// A match that ended lately: true with the name of its file ("" when it was not kept); false for an id that did not end, was never listed or is forgotten
    bool outcome(const std::string& id, std::string& kept_file) const;

    /// True for an id of the shape that begin() makes: `<A-Z a-z 0-9 _, 1 to 24>-<8 digits>-<6 digits>Z[-<1 to 4 digits>]`. The door checks it before any lookup.
    static bool valid_id(const std::string& id);

    /// The matches on the board (listed or not), and the ended ones that are remembered
    size_t running() const noexcept { return running_.size(); }
    size_t remembered() const;

private:
    struct Running {
        std::string id;
        std::string map;
        int64_t started{0};
        std::vector<std::string> players;
        const replay::Recorder* recorder{nullptr};
        mutable std::vector<uint8_t> cache;          // the snapshot made at cache_s (empty: none yet)
        mutable int64_t cache_s{0};
    };
    struct Ended {
        std::string id;
        std::string file;
        int64_t ended_s{0};
    };

    int64_t now_s() const { return clock_s_(); }
    bool listed(const Running& match) const;         // run long enough and the recording has not failed
    void forget_old();                               // drops the ended matches that are past kRememberS or beyond kMaxRemembered

    std::function<int64_t()> clock_s_;
    std::vector<Running> running_;                   // oldest start first
    std::vector<Ended> ended_;                       // oldest end first
};

}  // namespace ants::server

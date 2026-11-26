#pragma once

// The matches that run now, for the public door (docs/SERVER_REPLAYS.md "Replays", the live routes; docs/REPLAYS.md "On the game server"): a live match is a replay that is still growing, read over HTTP. A room that
// records its match registers it here when the recording begins (Room::replay_begin: the moment the room starts the match, before its first turn) and takes it off when the recording becomes a file or is dropped
// (Room::replay_end, or the room's destructor), so the board holds a raw pointer to the room's recorder only for as long as the room has it. The board is single threaded like the rest of the server and is owned by
// the RoomManager next to the replay store. A match played with Fog of War is not registered (a snapshot that anybody can read while it runs would show what the fog hides from its players; the match is public when it
// is over, like any other).
//
// THE DELAY (the one rule of fairness here): a match with two or more people in it (kDelayMinPeople, counted by the room from its seats when the match starts, never from names, and never again: a person who leaves or
// comes back changes nothing) is shown kDefaultDelayS (30) seconds late: what the board hands out is the match as it was that many seconds ago, cut by the recorder (Recorder::snapshot), so no order given and no turn
// run after that moment is in the bytes, whatever a visitor asks. A match with no person, or one person, is shown as it is (the snapshot of the last second). A watcher who is a player of the match, or a friend of one,
// therefore learns nothing about the match that is current.
//
// What it gives the door:
//  * an id for every registered match, which is the only thing a visitor ever names a match by: `<MAP>-<YYYYMMDD>-<HHMMSS>Z`, the stem of the map's file name and the UTC second in which the match began, with
//    `-2`, `-3` ... when two matches of one map began in one second (the id is unique among the matches that run and the ones that ended lately). It is made of nothing else: never of the room's code, a
//    name, an address or a key;
//  * the list: the matches whose recording has not failed, from the moment they begin (a match that has run no turn yet is on it with turns 0, and is shown as starting), newest start first, at most 50 (when there
//    are more, the matches that have run kReplayMinTurns, the length that the store keeps, come before the younger ones, so that a stream of matches that end at once cannot push the others off the list), with the
//    players in the strings of the replay list ("Green (Ann)", "Green", "Red (Bot (Medium))") and the match's game mode;
//  * the score boxes (LiveScores): the number that each seat's score box shows in the game, and which seats are one side, sampled by the room from its referee's engine once a second (Room::publish_live_scores) and given to
//    the list with no delay: only what every player of the match sees on the screen at that moment, nothing of the map, the ants, the orders or the other numbers of the match. This exception to the delay below holds for EVERY listed
//    match, one of an invite-only room included (the board knows nothing of a room's visibility, and the numbers are the same kind of thing for all). A match that is not on the board (Fog of War, an unrecorded match) has none;
//  * the snapshot of one of them: the recorder's incomplete file with its `live` chunk, cut at the turns that the delay allows (Recorder::snapshot), cached for as long as the board's clock stays in one second, so a
//    crowd of viewers costs one snapshot a second and the same bytes go to all of them meanwhile; with it the turns the match had run and the turns that the snapshot shows, so that a page can say how late it is;
//  * the fate of a match that ended lately: the name that the store kept it under ("" when it was not kept) and the turns it ran, for 15 minutes, 256 matches at most, so that a page that watched it finds the
//    replay when it ends; a match that ended before kReplayMinTurns (never kept: a room that was closed in its countdown or soon after) is remembered too, 2 minutes, 64 of them, so that a page that was sent to it says
//    that it ended too soon to be kept.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ants::replay {
class Recorder;
}

namespace ants::server {

/// What the score boxes of a match show at one moment (the game's own: HUD::render_score_box), by seat. Only the seats of the match's roster have a number. `known` is false until the room has taken its first sample (the
/// match has run no turn): the list then says nothing, and the page says that there is no score yet.
struct LiveScores {
    static constexpr size_t kSeats = 4;                    // sim::MAX_PLAYERS (live_board.cpp checks it)
    static constexpr int32_t kBoxMax = 999999;             // the box draws no more than this, and no less than 0 (a score can go below 0: the box shows 0)
    bool known{false};
    std::array<int32_t, kSeats> box{};                     // the number in the seat's box: its score and its ally's together, from 0 to kBoxMax
    std::array<uint8_t, kSeats> side{};                    // the side of the seat: the lowest seat of the seat and its ally, so allies share a number; the seat itself with no ally
    uint8_t covered{0};                                    // bit s: the box of seat s is covered in the game (the seat left the match): it shows no number
};

/// One match of the list
struct LiveMatch {
    std::string id;
    std::string map;                                 // the map's file name as the match named it ("TREASURE.LVL")
    uint8_t mode{0};                                 // the room's game mode as the replay's head holds it (sim::GameMode: 0 highest score, 1 for 187)
    uint8_t roster{0};                               // bit s: seat s plays (`players` has one string for each, in seat order)
    LiveScores scores;                               // the score boxes as the room last sampled them (`known` false: not yet)
    int64_t started{0};                              // seconds since 1970 UTC when the recording began
    uint32_t turns{0};                               // the turns run so far
    std::vector<std::string> players;                // the seats that play, "Green (Ann)", "Green" (no name) or "Red (Bot (Medium))", in seat order
};

/// What the door hands out for one match: the bytes (null: no such match), the turns the match had run when they were made, the turns they show, and the delay that applies to the match (all in turns)
struct LiveSnapshot {
    const std::vector<uint8_t>* bytes{nullptr};
    uint32_t now{0};
    uint32_t shown{0};
    uint32_t delay{0};
};

/// What became of a match that ended lately: the name of the file the store kept it as ("" when it was not kept) and the turns it ran
struct LiveOutcome {
    std::string file;
    uint32_t turns{0};
};

class LiveBoard {
public:
    static constexpr size_t kMaxListed = 50;                 // the list gives the newest 50
    static constexpr int64_t kSnapshotMaxAgeS = 1;           // a snapshot is handed out again for as long as the clock is in the second in which it was made; it is made again when a second or more has passed
    static constexpr int64_t kRememberS = 15 * 60;           // a match that ran kReplayMinTurns and ended is remembered this long ...
    static constexpr size_t kMaxRemembered = 256;            // ... and this many are, the oldest forgotten first
    static constexpr int64_t kRememberBriefS = 2 * 60;       // a match that ended before that (it is never kept) is remembered this long ...
    static constexpr size_t kMaxRememberedBrief = 64;        // ... and this many are (their own count: a stream of them forgets none of the others)
    static constexpr unsigned kDelayMinPeople = 2;           // a match with this many people or more is shown late
    static constexpr uint32_t kDefaultDelayS = 30;           // ... this many seconds (the server's --live-delay-seconds)
    static constexpr uint32_t kMaxDelayS = 300;
    static constexpr unsigned kMaxSameSecond = 9999;         // the "-2" ... "-9999" of the id (the page's pattern holds four digits)

    /// `clock_s`: seconds since 1970 UTC (empty: the system clock); the tests give their own
    explicit LiveBoard(std::function<int64_t()> clock_s = nullptr);
    LiveBoard(const LiveBoard&) = delete;
    LiveBoard& operator=(const LiveBoard&) = delete;

    /// Takes the clock from `clock_s` (empty: the system clock) from now on
    void set_clock(std::function<int64_t()> clock_s);
    /// The seconds that a match with two or more people is shown late (0: it is shown as it is); at most kMaxDelayS. It applies to the matches that begin from now on: a match that runs keeps the delay it began with.
    void set_delay_seconds(uint32_t seconds) noexcept;
    uint32_t delay_seconds() const noexcept { return delay_s_; }

    /// Registers a match whose recording has just begun (no turn has run). `people` is how many of the seats that play are held by people (not computer players), counted by the room from its seats at the moment it
    /// starts the match: two or more make the match one that is shown late, and the board never counts again. The recorder must stay alive until end() is called with the id. Returns the id; empty when the match cannot
    /// be named (every one of the 9999 ids of its second is taken) or is played with Fog of War (what the other sides do is hidden from a player, and a live view would not hide it), and then it is not on the board.
    std::string begin(const replay::Recorder& recorder, unsigned people);
    /// The room's sample of the score boxes of the match `id` (Room::publish_live_scores): the list shows it from now on, in place of the one before. An id that is not on the board (Fog of War, a match that is over, a made-up
    /// one) is ignored: nothing is made for it. The numbers are clamped to the box's range, and a seat that plays no part in the match keeps no number.
    void set_scores(const std::string& id, const LiveScores& scores);
    /// Takes the match off the board: the recording became a file (`kept_file` is the name the store gave it) or was dropped (`kept_file` empty). The match is remembered as ended (see above); an id that is not on the
    /// board is ignored.
    void end(const std::string& id, const std::string& kept_file);

    /// The matches that run, newest start first (see above)
    std::vector<LiveMatch> list() const;
    /// The snapshot of the match, `bytes` null when the id is not that of a listed match (the same rule as the list's) or the recorder cannot make one. The bytes are valid until the board is next used.
    LiveSnapshot snapshot(const std::string& id) const;
    /// A match that ended lately: true with the name of its file ("" when it was not kept) and the turns it ran; false for an id that did not end, was never on the board or is forgotten
    bool outcome(const std::string& id, LiveOutcome& out) const;

    /// True for an id of the shape that begin() makes: `<A-Z a-z 0-9 _, 1 to 24>-<8 digits>-<6 digits>Z[-<1 to 4 digits>]`. The door checks it before any lookup.
    static bool valid_id(const std::string& id);

    /// The matches on the board (listed or not), and the ended ones that are remembered
    size_t running() const noexcept { return running_.size(); }
    size_t remembered() const;

private:
    struct Running {
        std::string id;
        std::string map;
        uint8_t mode{0};
        uint8_t roster{0};
        LiveScores scores;
        int64_t started{0};
        std::vector<std::string> players;
        const replay::Recorder* recorder{nullptr};
        uint32_t delay_turns{0};                     // how late the match is shown, in turns: fixed when it began, from the people at its start
        mutable std::vector<uint8_t> cache;          // the snapshot made at cache_s (empty: none yet)
        mutable int64_t cache_s{0};
        mutable uint32_t cache_now{0}, cache_shown{0};
    };
    struct Ended {
        std::string id;
        std::string file;
        int64_t ended_s{0};
        uint32_t turns{0};
        bool brief{false};                           // it ran less than kReplayMinTurns: never kept
    };

    int64_t now_s() const { return clock_s_(); }
    bool listed(const Running& match) const;         // the recording has not failed and is not over
    static int64_t memory_s(const Ended& e) noexcept { return e.brief ? kRememberBriefS : kRememberS; }
    void forget_old();                               // drops the ended matches that are past their memory or beyond their count

    std::function<int64_t()> clock_s_;
    uint32_t delay_s_{kDefaultDelayS};
    std::vector<Running> running_;                   // oldest start first
    std::vector<Ended> ended_;                       // oldest end first
};

}  // namespace ants::server

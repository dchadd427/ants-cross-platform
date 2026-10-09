#pragma once

// Watching a replay inside the game (docs/REPLAYS.md "Watching"): what the page that holds the game may ask of it and read from it. Plain numbers, because the page reads them through
// exported C functions (ants_replay_get, ants_replay_do in application.cpp).

#include <cstdint>

namespace ants::app {

/// Where the replay on the screen is
enum class ReplayState : int {
    None = 0,        // the game shows no replay
    Playing = 2,
    Paused = 3,
    Jumping = 4,     // playing from the start to the turn that was asked for, without a picture
    Ended = 5,       // the match was over: the results screen is up
    CutShort = 6,    // the recording ends before the match did (it was left or closed): the last picture stays
    Failed = 7       // the file cannot be shown: replay_failure() says why
};

/// Why a file cannot be shown
enum class ReplayFailure : int {
    None = 0,
    Unreadable = 1,  // not a replay, damaged, or the file could not be opened
    Older = 2,       // another version of the game's simulation: an older one
    Newer = 3,       // ... a newer one
    NoMap = 4,       // the map is not in the game's maps folder, or it is another file than the one the match was played on
    Diverged = 5     // played here, the match differs from the recorded one (a hash of the file does not match)
};

/// What the page asks (Application::replay_control)
enum class ReplayControl : int {
    TogglePause = 0,
    SetPaused = 1,   // value 1 pauses, 0 plays
    SetSpeed = 2,    // value: the speed times 100 (50, 100, 200, 400, 800); anything from 25 to 1600 is taken
    Seek = 3,        // value: the turn to be at (a turn is 50 ms); past the end is the end
    Restart = 4,     // from the start, playing (the bar's "Watch again")
    Extend = 5,      // a recording that is still being made (--replay-live): the page has put a newer copy of the same match in the file; the game reads it again (a copy that is not the same match, or shorter, is ignored)
    LiveOver = 6     // no newer copy will come: the game stops waiting for more and plays to the end of what it holds
};

/// What the page reads (Application::replay_value)
enum class ReplayValue : int {
    State = 0,       // a ReplayState
    Turn = 1,        // the turns played (the time of the picture)
    Total = 2,       // the turns of the recording
    Speed = 3,       // times 100
    JumpPercent = 4, // while Jumping: how far
    Failure = 5,     // a ReplayFailure
    JumpTarget = 6,  // while Jumping: the turn that it goes to
    Live = 7,        // 1 while the game follows a recording that is still being made (--replay-live, not over, not complete), else 0
    Limit = 8,       // the last turn that can be played now: Total, and while following Total less the turns held back (kLiveHoldTurns) so that the picture does not stall between two copies
    Complete = 9     // 1 when the file holds its end (the match is over and the file is whole)
};

/// A following game keeps this many turns (2 seconds) of the recording unplayed: the copies of a match that is going on come every few seconds, and a picture that ran up to the newest turn would stand still
/// until the next one
inline constexpr unsigned kLiveHoldTurns = 40;

}  // namespace ants::app

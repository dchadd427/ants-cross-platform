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
    Restart = 4      // from the start, playing (the bar's "Watch again")
};

/// What the page reads (Application::replay_value)
enum class ReplayValue : int {
    State = 0,       // a ReplayState
    Turn = 1,        // the turns played (the time of the picture)
    Total = 2,       // the turns of the recording
    Speed = 3,       // times 100
    JumpPercent = 4, // while Jumping: how far
    Failure = 5,     // a ReplayFailure
    JumpTarget = 6   // while Jumping: the turn that it goes to
};

}  // namespace ants::app

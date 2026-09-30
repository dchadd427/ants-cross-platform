#pragma once

// The texts of Ants.exe's string table (LoadStr, FUN_010292dc) that the status line, the chat log and the dialogs of a
// running match use, by their original ids. `%s` stands for a name / colour / title that the caller inserts (the original
// turns `%%s` into `%s`). The application title (string 79) is "Ants" here. Whether a status is posted with the flash flag
// (PostStatus flag 1: a 500 ms flicker before the steady text) is a property of the call site in the original; the table
// records it per id because every site of an id agrees (17, 49, 50, 53, 59, 75).

#include <cstdint>
#include <string>

namespace ants::sim::strings {

// Status texts (PostStatus)
inline constexpr uint16_t kWelcome = 5;               // "Welcome to %s!"
inline constexpr uint16_t kSelWorker = 6;             // "Ready!"
inline constexpr uint16_t kSelBomber = 7;             // "BomberAnt selected."
inline constexpr uint16_t kSelFire = 8;               // "Where to?"
inline constexpr uint16_t kSelThief = 9;              // "Thief here"
inline constexpr uint16_t kSelCombat = 10;            // "Yessir!"
inline constexpr uint16_t kSelSwimmer = 11;           // "SwimmerAnt selected."
inline constexpr uint16_t kSelMany = 12;              // "Ready!"
inline constexpr uint16_t kNeed200Points = 13;        // "You need 200 points to hatch!"
inline constexpr uint16_t kAlreadyHatching = 14;      // "An Ant is already hatching!"
inline constexpr uint16_t kHatching = 15;             // "Hatching a new Ant!"
inline constexpr uint16_t kNoEggs = 16;               // "No eggs to hatch!"
inline constexpr uint16_t kAlreadyHaveFood = 17;      // "Can't - already have food."
inline constexpr uint16_t kQuickChat1 = 18;           // "Now you are in for it!" (F9 default)
inline constexpr uint16_t kQuickChat2 = 19;           // "Let me be!" (F10)
inline constexpr uint16_t kQuickChat3 = 20;           // "Attack!" (F11)
inline constexpr uint16_t kQuickChat4 = 21;           // "Do you want to ally?" (F12)
inline constexpr uint16_t kTeamNow = 39;              // "%s (%s) and %s (%s) are a team now!" (chat log news flash)
inline constexpr uint16_t kTeamNoMore = 40;           // "%s (%s) and %s (%s) are no longer a team!" (chat log news flash)
inline constexpr uint16_t kDroppedOut = 46;           // "%s dropped out of the game!" (chat log news flash)
inline constexpr uint16_t kCantDoThat = 48;           // "Can't do that..."
inline constexpr uint16_t kOneMinute = 49;            // "1 minute left in the game."
inline constexpr uint16_t kThirtySeconds = 50;        // "30 seconds left in the game."
inline constexpr uint16_t kAntDead = 51;              // "Ant dead."
inline constexpr uint16_t kAntDrowned = 52;           // "Ant drowned."
inline constexpr uint16_t kThiefAtHill = 53;          // "A ThiefAnt is at your anthill!"
inline constexpr uint16_t kStopping = 54;             // "Stopping."
inline constexpr uint16_t kOuch = 55;                 // "Ouch!"
inline constexpr uint16_t kBombDropped = 56;          // "Bomb dropped."
inline constexpr uint16_t kBombDefused = 57;          // "Bomb defused."
inline constexpr uint16_t kCantGoThere = 58;          // "Can't go there."
inline constexpr uint16_t kTenSeconds = 59;           // "10 seconds and counting..."
inline constexpr uint16_t kGotFood = 60;              // "Got Food!"
inline constexpr uint16_t kScoreGoingUp = 61;         // "Score going up..."
inline constexpr uint16_t kFoodStolen = 62;           // "Food stolen..."
inline constexpr uint16_t kHatched = 63;              // "Ready!"
inline constexpr uint16_t kFirePutOut = 64;           // "Fire put out."
inline constexpr uint16_t kStartingFire = 65;         // "Starting a fire..."
inline constexpr uint16_t kOnMyWay = 66;              // "On my way."
inline constexpr uint16_t kAttack = 67;               // "Attack!"
inline constexpr uint16_t kMovinOut = 68;             // "Movin' out."
inline constexpr uint16_t kMyPleasure = 69;           // "My pleasure..."
inline constexpr uint16_t kHereIGo = 70;              // "Here I go..."
inline constexpr uint16_t kBurn = 71;                 // "Burn..."
inline constexpr uint16_t kTeamMade = 75;             // "A team has been made."
inline constexpr uint16_t kTeamRejected = 80;         // "%s rejected teaming up"
inline constexpr uint16_t kTeamAccepted = 81;         // "%s accepted teaming up"
inline constexpr uint16_t kTeamWithdrawn = 82;        // "%s withdrew offer to team up"
inline constexpr uint16_t kColourBlack = 100;         // "Black"
inline constexpr uint16_t kColourBlue = 101;          // "Blue"
inline constexpr uint16_t kColourRed = 102;           // "Red"
inline constexpr uint16_t kColourGreen = 103;         // "Green"
inline constexpr uint16_t kWaitingForOthers = 104;    // "Waiting for others..."
inline constexpr uint16_t kGetReady = 105;            // "Get ready to play!  You are the %s Ants."
inline constexpr uint16_t kWaitingForScores = 111;    // "Waiting for scores..."

/// The text of an original string id (with its `%s` markers), nullptr for an id that the table does not hold.
const char* text(uint16_t id) noexcept;

/// True when the status of this id is posted with the flash flag.
bool blinks(uint16_t id) noexcept;

/// The text with its `%s` markers replaced one after the other by `a`, `b`, `c` and `d`.
std::string format(uint16_t id, const std::string& a = {}, const std::string& b = {},
                   const std::string& c = {}, const std::string& d = {});

/// Colour name of a colour index 0..3 (black, blue, red, green: strings 100..103), "" otherwise.
const char* colour_name(uint8_t colour_index) noexcept;

}  // namespace ants::sim::strings

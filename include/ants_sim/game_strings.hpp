#pragma once

// The texts of Ants.exe's string table (LoadStr, FUN_010292dc) that the status line, the chat log and the dialogs of a
// running match use, by their original ids. `%s` stands for a name / colour / title that the caller inserts (the original
// turns `%%s` into `%s`). The application title (string 79) is "Ants" here. Whether a status is posted with the flash flag
// (PostStatus flag 1: a 500 ms flicker before the steady text) is a property of the call site in the original; the table
// records it per id because every site of an id agrees (17, 49, 50, 53, 59, 75).

#include <cstdint>
#include <string>

namespace ants::sim::strings {

// Dialog texts of the alliance protocol
inline constexpr uint16_t kInviteDialog = 1;          // "%s (%s) invites you to form a team.  Would you like to accept?"
inline constexpr uint16_t kInviteBreakDialog = 2;     // "... This will remove you from the team you have with %s (%s).  Would you like to accept?"
inline constexpr uint16_t kWaitingForAnswer = 3;      // "Waiting for %s (%s) to respond to your offer to team up."
inline constexpr uint16_t kBreakTeamConfirm = 4;      // "Doing this will break your team with %s (%s).  Continue?"

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
// The setup screen / lobby (the original's network states; docs 5.47)
inline constexpr uint16_t kGameStartedInit = 85;      // "Game started, initializing..."
inline constexpr uint16_t kFindingGame = 86;          // "Finding game..."
inline constexpr uint16_t kNetworkInit = 87;          // "Network communication initializing..."
inline constexpr uint16_t kPressStart = 88;           // "Press START when all players' thumbs have appeared."
inline constexpr uint16_t kStartedWithoutYou = 89;    // "The game has been started without you.  Press 'Q' to quit."
inline constexpr uint16_t kWaitingForHost = 90;       // "Waiting for the host to start the game..."
inline constexpr uint16_t kConnectingToHost = 91;     // "Trying to connect to the host..." (first 30 s)
inline constexpr uint16_t kDroppedFromGame = 94;      // "Sorry, you have been dropped from the game.  Hit OK to exit the program."
inline constexpr uint16_t kLoadingGame = 98;          // "Loading game..."
inline constexpr uint16_t kTroubleConnecting = 107;   // "Having trouble connecting to host..." (30 - 60 s)
inline constexpr uint16_t kUnableToConnect = 108;     // "Unable to connect to host, recommend you quit..." (after 60 s)
inline constexpr uint16_t kMapFileMissing = 112;      // "You can't join the game because the map file '%s' was not found on your computer."
inline constexpr uint16_t kPeerMapMissing = 113;      // "%s was missing the map file '%s' and had to leave the game."
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

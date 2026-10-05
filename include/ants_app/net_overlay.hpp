#pragma once

// The lines that a network match shows at the top of the playfield when something is wrong or has just happened (remake text: the original has none). Which lines they are is decided
// here, from what the network layer reports, so that the priorities and the words can be tested without a window. The highest rule that applies wins; a line of a lower rule never shows
// while one of a higher rule does:
//
//   1. this machine's way back   "Connection lost. Reconnecting... 0:12" (the seconds since the loss; from the second link on " (attempt 2)") and "Esc leaves the match": its link to the
//                                server is lost and a new one is being made (NetGame::pause_info().reconnecting; the runner is not held, so the wait for a turn would say "Waiting for the other
//                                players..." after a second: this line wins)
//   2. a vote that is open       the VOTE BLOCK under the lines of rule 3: "1 of 2 voted to continue" and two buttons, "F2 Keep waiting" and "F3 Continue without Bob" (never a modal dialog:
//                                chat goes on and the map stays visible); this player's choice is shown as pressed. A seat that keeps losing its connection and is back is put to the vote
//                                too, then the block says so in a line of its own
//   3. the seats that are missing "Bob (Red) lost the connection, waiting 0:42", one line for each (at most three), only after a second of pause (a blip of a few hundred
//                                milliseconds is not worth a banner); a seat that is back and is being given the match says "Bob is coming back... 45%"
//   4. the resume countdown      "Bob is back: the match goes on in 7" ("The match goes on in 7" when nobody came back: the others voted to go on without a seat, or it was this machine)
//   5. today's lines             nothing of these shows while one of the above does:
//        "Out of sync: the match has stopped."        two machines disagree: the match is frozen (red)
//        "The host left. Choosing a new host..."      no turns arrive until the guests have agreed (a game on the local network)
//        "Waiting for the other players..."           this machine has waited a second for a turn; "Waiting for Bob..." when the host knows who holds the game up (a host with a
//                                                     seat waits for a peer that is more than 3 s behind)
//        "Catching up..."                             this machine is more than 3 s behind a server's room (it was away) and runs the backlog down at up to four times normal speed
//        "You are lagging (12 s behind)"              the server told this player that it is the one who falls behind (its own link is slow: the turns it misses are still on their
//                                                     way, so there is no backlog in its queue and nothing else tells it)
//        "Bob is lagging (12 s behind)"               a server's room does not wait for a player that falls behind: the others are told who it is, once a second, while it is
//                                                     more than 3 s behind and until it is within one second (ants_net/session.hpp)
//        the match notice                             "Bob is the host now." for a few seconds
// Among today's lines a machine that is itself cut off says so (waiting) before it says who else is slow (lagging).
//
// A text that has a player's name in it is cut at the NAME ("A very long na..." in the place of the name) until it fits the width that the caller gives (a name has up to 32 characters, and the
// overlay of the original's picture is 442 pixels wide): every line and every label fits, whatever the name is.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// A seat that is missing from the match, as the server last said it (NetGame::pause_info().missing)
struct NetOverlaySeat {
    uint8_t seat{255};
    std::string name;                // "" when the seat has none: "Player 3"
    uint32_t away_s{0};              // the seat's total absence in this match, in whole seconds
    bool catching_up{false};         // the seat is back and is being given the match (false: its connection is lost)
    uint8_t progress{0};             // catching_up: 0 .. 100
};

struct NetOverlayInput {
    bool desynced{false};
    bool electing{false};
    uint32_t stalled_ms{0};          // how long this machine has waited for a turn (NetGame::stalled_ms)
    std::string waiting_for;         // the player that holds the game up, when a host with a seat knows ("" when none)
    bool catching_up{false};         // NetGame::catching_up
    uint32_t self_lag_behind_ms{0};  // how far behind the match the server says this player is (NetGame::self_lag_behind_ms), 0 when it does not say so
    int lag_seat{-1};                // the player that the server announced as lagging (0 .. 3), -1 when none
    std::string lag_name;            // its name ("" when it has none: "Player 3")
    uint32_t lag_behind_ms{0};       // how far behind the match it was at the last announcement
    std::string notice;              // NetGame::match_notice

    // The way back and the held match (NetGame::pause_info, docs/NETWORK_PORT.md "Reconnect")
    bool reconnecting{false};        // this machine's link is lost and a new one is being made
    uint32_t away_s{0};              // ... for this many whole seconds
    uint32_t attempts{0};            // ... and this many new links were made since
    bool way_back_catching_up{false};   // the server gives this machine the match, which it runs without drawing it (the catch-up screen: no overlay)
    uint32_t held_ms{0};             // how long this screen has seen the match held (a seat missing, or the countdown): the missing seats are shown after NET_HELD_MESSAGE_MS
    std::vector<NetOverlaySeat> missing;      // the seats that are missing, longest away first
    bool vote_open{false};
    uint8_t vote_seat{255};          // the seat that the vote is about (it may be back: a seat that keeps losing its connection)
    std::string vote_name;
    uint8_t votes_continue{0};       // the connected players who chose to go on without it
    uint8_t voters{0};               // the connected players that vote
    enum class Choice : uint8_t { None, KeepWaiting, Continue };
    Choice my_vote{Choice::None};    // this machine's own choice
    uint8_t resume_seconds_left{0};  // the countdown that follows a pause (0: none)
    std::string back_name;           // the seat that came back and begins the countdown ("" when none did)

    // What a line may be (the overlay's own width) and how wide a text is: a name is cut to fit. Unset: nothing is cut.
    int32_t max_width{0};
    std::function<int32_t(const std::string&)> measure;
};

/// The vote block (rule 2): the count and the labels of the two buttons; the keys are F2 and F3
struct NetOverlayVote {
    bool open{false};
    std::string count;               // "1 of 2 voted to continue"
    std::string keep;                // "F2 Keep waiting"
    std::string go_on;               // "F3 Continue without Bob"
    bool keep_pressed{false};        // this player's choice, shown as a pressed button
    bool go_on_pressed{false};
};

struct NetOverlayLine {
    std::string text;                // the first line; empty: nothing to say
    bool alarm{false};               // drawn in red
    std::vector<std::string> lines;  // every line, top down (the first is `text`); empty when there is nothing
    NetOverlayVote vote;             // under the lines, while a vote is open
};

/// A machine waits for a turn this long before it says so
inline constexpr uint32_t NET_WAIT_MESSAGE_MS = 1000;
/// The match is held this long before the seats that are missing are shown (the vote is shown as soon as it is open, and so is the countdown)
inline constexpr uint32_t NET_HELD_MESSAGE_MS = 1000;

NetOverlayLine net_overlay_line(const NetOverlayInput& in);

/// "m:ss" of a number of seconds (0:12, 7:05, 62:00)
std::string net_overlay_clock(uint32_t seconds);

/// Where the line is drawn: the text is centred in the map view `view`, 5 rows below its top, on a dark box with 6 px on the sides and 3 px above and below it. (For the original's
/// view (16, 21, 442, 440) the text starts at (17 + (441 - w) / 2, 26).)
struct NetOverlayBox {
    int32_t text_x{0};
    int32_t text_y{0};
    LayoutRect box;
};
NetOverlayBox net_overlay_box(const LayoutRect& view, int32_t text_w, int32_t text_h);

/// The width that a line of the overlay may have in the map view `view`: the view less the box's sides and a margin
int32_t net_overlay_max_width(const LayoutRect& view);

/// The widths that the drawing measured (the renderer's width of each text) and the height of a line of text
struct NetOverlayMetrics {
    std::vector<int32_t> line_w;     // the width of each line of NetOverlayLine::lines
    int32_t text_h{0};
    int32_t count_w{0};              // the vote's count line, the labels of its buttons
    int32_t keep_w{0};
    int32_t go_on_w{0};
};

/// Where everything of the overlay stands in the map view `view`: the lines one under the other (the first where net_overlay_box puts it), then, with a vote, the count line and a row of the
/// two buttons under it, all centred. The rectangles are the drawing's and the mouse's: a click inside a button is that choice.
struct NetOverlayLayout {
    std::vector<NetOverlayBox> lines;
    bool has_vote{false};
    NetOverlayBox count;             // the count line of the vote block
    LayoutRect keep;                 // the buttons; each label is centred in its button
    LayoutRect go_on;
    int32_t keep_text_x{0};
    int32_t go_on_text_x{0};
    int32_t button_text_y{0};
};
NetOverlayLayout net_overlay_layout(const LayoutRect& view, const NetOverlayMetrics& metrics, bool with_vote);

/// What the browser check reads of the lines on screen (the page cannot read the game's canvas; read-only; Application's ants_probe 16 and 10000 +): 16 is how many lines `overlay` has; 10000 + 1000 * line + index
/// is the code of the character at `index` of that line (0 past its end, -1 where there is no such line or `index` is 1000 or more). Any other `what`: -1.
int net_overlay_probe(const NetOverlayLine& overlay, int what);

}  // namespace ants::app

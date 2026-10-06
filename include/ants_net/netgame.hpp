#pragma once

// The network as the application sees it: one object that owns the listener and the connections, runs the room before the match and the session
// during it, and gives the HUD its CommandSink. It is driven from the main loop with a monotonic millisecond clock (`update`), never blocks, has no
// threads, and does not know about SDL, the renderer or the map files: the application loads the map when the room says so and reports back.
//
//   host:   host(port, name) -> room (guests join, the host picks the map and the fog) -> start_match(seed, map hash) -> every machine loads
//           (StartRequested event, report_loaded) -> Begun -> the match runs -> freeze() at its end
//   client: join(address, port, name) -> room (follows the host's map and fog) -> StartRequested -> load, report_loaded -> Begun -> the match runs
//   leader: the first player in a dedicated server's room (protocol 7) is the room's leader (is_leader()); request_start() asks the server to start with the players who are
//           there (the server may refuse: fewer than two players), the rest is as for any client. Since protocol 11 the request carries set_fill_bots(): with a level the server seats
//           bots of that level in every empty seat first, and one player is enough. A LAN host's own START does the same with fill_bots() (its machine runs the bots).
//   chat:   in the waiting room and while the map loads everybody can talk (chat(), protocol 11): the lines come back from the room (Event::Chat, take_pregame_chat(), pregame_chat()), and
//           the status line shows the latest for a few seconds. Nothing about it is a command: it is the room's, before the match has a simulation.
//
// During the match the guests are also linked to each other (each guest listens on a port that the host passes on with the roster; the links are made
// while the map loads). When the host goes, the guests agree on the lowest living seat as the new host and the match goes on (see session.hpp); the
// events HostChanged and PlayerLeft report it, HostLeft only when no new host could be found.
//
// Raw TCP (LAN and development) is the transport of native builds; a WebAssembly build has none yet (host() and join() return false there) until the
// WebRTC transport of the network port arrives. The class does not care which Connection it talks to.
//
// The way back (protocol 10, docs/NETWORK_PORT.md "Reconnect"). A client of a dedicated server's room that holds seats is given a key with its Welcome; its ClientSession is then built to come
// back by itself when its link is lost (the room is not left: Phase stays Playing), and this class makes the new links that the session asks for (a JoinTarget keeps how the first one was made).
// LAN and direct games are exactly what they were (host migration).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ants_net/lan.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/prediction.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

/// FNV-1a 64 over the bytes of a file (the map identity that the start barrier compares); false when the file cannot be read.
bool hash_file(const std::string& path, uint64_t& out);

/// What the player is told when the match is lost to this machine (NetGame::status_text after the event HostLeft): the reason as the session knows it. A server that closed
/// the link of a machine that held half a minute of the match unplayed dropped it for being away (a hidden tab, a process that was stopped); anything else is a link that is gone.
std::string match_lost_text(ClientSession::LostReason reason);

/// How a client reached its server, kept by join() and join_url() so that the way back can make the same link again: the TCP address and port of a native client, or the WebSocket URL of the
/// browser build, and what the Hello said (the name, the seat that was asked for, the room's code, the token: carried, never interpreted, never written to a log).
struct JoinTarget {
    std::string address;               // native: the host to connect to, as the player gave it (empty in the browser)
    std::string resolved;              // native: the numeric address that `address` stood for when the game was joined: the way back's links go there, the name is not looked up again
    uint16_t port{0};
    std::string url;                   // the browser build: the ws:// or wss:// URL (empty natively)
    std::string name;
    uint8_t want_seat{255};
    std::string room;
    std::string token;
};

/// A seat's key as the player's machine has to keep it (NetGame::set_on_key) and let go of it (set_on_forget_key). A SECRET: whoever has it can take the seat. The place that keeps it (a file that only its
/// owner can read, the browser's local storage) keeps it private, and it is never written to a log, a status line or an address.
struct RejoinKey {
    std::string room;                  // the room's code
    uint8_t seat{255};                 // the seat that the key holds
    SeatKey key{};
    std::string server;                // where the room is: "host:port" of a native client, the URL of the browser build (never the token)
};

/// What the screens of a match that is held, or whose machine is on its way back, draw (NetGame::pause_info). Everything is the last word of the server or of this machine's own session; nothing here is
/// a decision. The fields about the server's pause are those of the last Presence and are only filled while this machine follows the match (not while it is on its way back: they would be old).
struct PauseInfo {
    // this machine's way back: its link to the server was lost and the session makes a new one (docs/NETWORK_PORT.md "Reconnect")
    bool reconnecting{false};          // the link is lost and a new one is being made or says Hello
    bool catching_up{false};           // the server gives this machine the match, which it runs without drawing it (the loading screen's "Catching up N%")
    uint8_t catch_up_percent{0};       // 0 .. 100 while catching_up
    uint32_t away_s{0};                // whole seconds since the link was lost (reconnecting or catching_up)
    uint32_t attempts{0};              // the new links that were made since (reconnecting or catching_up)
    uint32_t give_up_s{0};             // whole seconds left before the way back is given up (reconnecting): the cap that the room named and a minute, from the loss
    // the match as the server holds it: the seats that are missing from it, longest away first
    struct Seat {
        uint8_t seat{255};
        std::string name;              // as the match's roster names it
        uint32_t away_s{0};            // the seat's total absence in this match, in whole seconds
        bool catching_up{false};       // the seat is back and is being given the match (false: its connection is lost)
        uint8_t progress{0};           // catching_up: 0 .. 100
    };
    std::vector<Seat> missing;
    // the vote of the others about the seat that has been away longest (or that flaps); never about this machine's own seat: that seat has no block and no vote to cast
    bool vote_open{false};
    uint8_t vote_seat{255};
    std::string vote_name;
    uint8_t votes_continue{0};         // the connected players who chose to go on without it
    uint8_t voters{0};                 // the connected players that vote (not the seat itself): the vote is won by more than half of them
    enum class Choice : uint8_t { None, KeepWaiting, Continue };
    Choice my_vote{Choice::None};      // this machine's own choice
    // the countdown that follows a pause: the match goes on in this many seconds (0: none)
    uint8_t resume_seconds_left{0};
};

class NetGame final : public sim::CommandSink {
public:
    enum class Role : uint8_t { None, Host, Client };
    enum class Phase : uint8_t {
        Off,           // not networked
        Connecting,    // client: connecting and waiting for the host's welcome
        Room,          // in the room before a match
        Loading,       // the host said Start: the application loads the map and reports
        Playing,       // the match runs
        Over,          // the session ended (the host left, the connection is gone)
        Failed         // could not join (refused, full, wrong version, connection failed)
    };
    /// Why a join ended in Phase::Failed (None until it has): the server was never reached, the server answered with a Reject (reject_reason()), the connection was lost after it
    /// was made but before the room was joined, the server accepted the connection and never answered (no Welcome within ten seconds: NoAnswer), or the room's connection ended once
    /// the player was in it
    enum class FailReason : uint8_t { None, Unreachable, Rejected, Lost, Closed, NoAnswer };
    struct Event {
        enum class Type : uint8_t {
            RoomChanged,      // somebody joined or left, or the host changed the map or the fog
            StartRequested,   // load start_info() now and call report_loaded()
            Begun,            // everybody is loaded: the match runs
            Cancelled,        // the start failed (a player left, a map did not load): back in the room
            PlayerLeft,       // during the match: `seat` dropped out
            HostLeft,         // during the match (client): the host is gone and no new host could be agreed: the match cannot go on here
            Desync,           // two machines disagree: the match is frozen
            Failed,           // joining failed, see status_text()
            HostChanged,      // during the match: the host left and `seat` is the new host (this machine when it is our own seat)
            Chat,             // in the room (protocol 11): a line was said, `seat` is its sender (kRoomSender: the room itself); take_pregame_chat() has it
            Rejoined          // this machine's way back is over: it has caught up, the server gave it its seat again and it follows the live match (the screen leaves the catch-up view)
        };
        Type type{Type::RoomChanged};
        uint8_t seat{255};
        bool rejoin{false};   // StartRequested and Begun: this machine starts from nothing and is given a match that runs (it joined with its key, or took the way back again after a server that had lost
                              // the last turns answered it BadRequest): the screen skips the start dialog and the start sound, the catch-up is its loading
    };

    explicit NetGame(sim::SimulationEngine& sim);
    ~NetGame() override;
    NetGame(const NetGame&) = delete;
    NetGame& operator=(const NetGame&) = delete;

    // ---- set-up (non-blocking) -------------------------------------------------------------------------------------------------------------------
    /// Opens a room on `port` (0 = any free port, see listen_port()); `loopback_only` accepts only this machine (tests). False when the port cannot be
    /// used or there is no transport.
    bool host(uint16_t port, const std::string& name, bool loopback_only = false);
    /// Starts joining the room at address:port. False when the address cannot be used or there is no transport; the outcome arrives as events.
    /// `want_seat` (0 .. 3) asks the host for that seat (the colour: 0 green, 1 red, 2 blue, 3 black); a seat that is taken gives the first free one; 255 = any.
    /// `key` (all zero: a new player, as it always was) is the key of a seat of a dedicated server's room that this machine had (a game that was started again, a page that was reloaded: the place
    /// that keeps it gave it back): the Hello shows it, and a room whose match runs gives the machine its seat and the match from the server's log (the event StartRequested, then Begun, both with
    /// `rejoin`). A room that is over, that does not hold the seat or that has dropped it refuses, with the reasons of the way back (status_text()).
    bool join(const std::string& address, uint16_t port, const std::string& name, uint8_t want_seat = 255, const std::string& room = std::string(), const std::string& token = std::string(),
              const SeatKey& key = SeatKey{});
    /// The same through a WebSocket (ws:// or wss:// URL, the game server's door behind its proxy): the browser build's only way to join. False when the URL cannot
    /// be used or there is no WebSocket (every native build: it joins with TCP). A server's room has no host migration and no links between guests.
    bool join_url(const std::string& url, const std::string& name, uint8_t want_seat = 255, const std::string& room = std::string(), const std::string& token = std::string(),
                  const SeatKey& key = SeatKey{});
    /// Leaves for good: tells the others (a guest says Leave), closes every connection. The others see the host or the guest gone. This works in every state of the way back: a machine whose link is up
    /// (during a pause too, when no Quit command would be sealed) says Leave; one that has no link cannot tell the server (its seat is held until the others vote or the cap drops it). The key is forgotten,
    /// unless the session had ended by itself (Over, Failed): its end decided about the key (a refusal that keeps it, a link that never opened), and the application's way back to its menu only cleans up.
    void leave();

    // ---- the way back (docs/NETWORK_PORT.md "Reconnect") ---------------------------------------------------------------------------------------------
    /// Told when this machine has been handed a key by a dedicated server's room that holds seats and the match it is for has begun: where to keep it so that the match can be taken up again if the game is
    /// closed or the page reloaded (join / join_url with the key). A player of a waiting room is told when the Start arrives, not at the room's Welcome (a visit to a waiting room alone leaves no key to
    /// outlive it, and a reload in the waiting room is a new visit); a start that is cancelled takes the key back (set_on_forget_key) and the next Start gives it again. Told again at the Welcome of every
    /// rejoin, with the same key (the match is running then). Never for a room that gives none (a game on the local network, a room that holds no seats). The function may end the session (leave()).
    void set_on_key(std::function<void(const RejoinKey&)> fn) { on_key_ = std::move(fn); }
    /// Told once per key when it can no longer be used and is to be let go of: the match ended (freeze), the server dropped the seat or has no such match any more or does not hold the seat, the time to
    /// wait ran out, the player left (leave), or the room that took the code does not know the key (a new room). Not told when another window has the seat (Superseded) or when the server would not take
    /// the machine back just now (RejoinFailed): the player may use Rejoin later. The function may end the session (leave()) too.
    void set_on_forget_key(std::function<void(const RejoinKey&)> fn) { on_forget_key_ = std::move(fn); }
    /// The match is held for this machine: a seat is missing from it or the countdown after a pause runs (the server's word), or this machine is not following the live match itself (its link is lost, a new one
    /// is being made, or it catches up). Nothing is sealed meanwhile, so a Quit command would be lost: the application leaves with leave() instead.
    bool paused() const;
    /// What the screens draw: this machine's way back, the missing seats, the vote, the countdown. Empty (every field at its default) outside a match as a client.
    PauseInfo pause_info() const;
    /// This machine's choice in the vote that is open: keep waiting for the seat that is missing, or go on without it (the server counts the last choice of every player). False (nothing is sent) when no vote
    /// is open or this machine does not follow the live match.
    bool vote(bool keep_waiting);
    /// What a player is told when a refusal ends a way back (status_text()): the reasons that the way back has words of its own for (the seat was dropped, the match is over, the server does not hold the seat,
    /// the server would not take the machine back now), and the words of reject_text() for the rest
    static std::string way_back_text(RejectReason reason);

    // ---- the room on the local network -------------------------------------------------------------------------------------------------------------
    /// Before host(): the UDP port on which the open room announces itself to the games of the local network (see lan.hpp; 0 = not at all, the default is
    /// kLanDiscoveryPort). `loopback_only` keeps the announcements on this machine (tests); a room opened with host(..., loopback_only = true) does that anyway.
    void set_discovery(uint16_t udp_port, bool loopback_only = false);
    /// The game's version text that the announcements carry for the list of games (the application sets it; empty: none)
    void set_game_version(const std::string& text) { game_version_ = text; }
    /// True while the room announces itself: the host, in the room, before the start (no late join), with a UDP socket
    bool announcing() const noexcept;

    // ---- every frame -----------------------------------------------------------------------------------------------------------------------------
    void update(uint32_t now_ms);
    /// Real time that the clock of update() did not count (the browser build, a hidden page: the application hands the network at most a second per wake-up): a host that
    /// said nothing in the last update has been silent for `ms` more (ClientSession::note_gap), and the session judges it now. Only a match as a guest is touched.
    void note_gap(uint32_t ms);
    std::vector<Event> take_events();
    /// The browser build only: called from the browser's event loop when the server's connection has news (a message arrived, an error, the link closed), whether or
    /// not the page draws frames. A hidden page runs none and the browser slows its timers, but the WebSocket's events still come: the application makes its
    /// background step from here (Application::background_pump), which calls update() and take_events() like a frame does. The function may end the session (leave()).
    /// A native build never calls it (its transports are polled by the frame loop).
    void set_on_wake(std::function<void()> fn) { on_wake_ = std::move(fn); }
    /// The tests: the way back asks this function for every new link instead of connecting to the JoinTarget; null means that no link could be made (the name does not resolve: the network is down)
    void set_link_maker_for_test(std::function<std::unique_ptr<Connection>()> fn) { link_maker_ = std::move(fn); }
    /// The tests: join() asks this function for the numeric address of its host (the lookup that it makes once) instead of the system's; "" means that the name does not resolve
    void set_resolver_for_test(std::function<std::string(const std::string&)> fn) { resolver_ = std::move(fn); }

    // ---- state -----------------------------------------------------------------------------------------------------------------------------------
    Phase phase() const noexcept { return phase_; }
    /// True for the room's owner, and for a guest that took over during the match
    bool is_host() const noexcept { return role_ == Role::Host || host_session_ != nullptr; }
    bool active() const noexcept { return role_ != Role::None; }
    uint8_t my_seat() const noexcept { return seat_; }
    /// Valid in Phase::Failed (None before): what went wrong, so that the application can say it in its own words
    FailReason fail_reason() const noexcept { return fail_reason_; }
    /// The server's reason when fail_reason() is Rejected
    RejectReason reject_reason() const noexcept { return reject_reason_; }
    /// A test hook: puts the session in `phase` as it would stand when the network layer got there by itself (Over comes only from a match that is lost, so the application's way of
    /// treating a session that is over under the setup screen, the room's panel or a join cannot be reached by any real sequence of messages). Nothing else changes.
    void force_phase_for_test(Phase phase) noexcept { phase_ = phase; }
    uint16_t listen_port() const noexcept { return listen_port_; }
    /// A guest: the port on which the other guests connect to it during the match (0 when it has none)
    uint16_t peer_port() const noexcept { return peer_port_; }
    /// The room as this machine sees it (the host's own copy, or the last one received)
    const RoomMsg& room() const noexcept { return room_; }
    /// One line for the screen, in the original's words: what the machine is waiting for ("Press START when all players' thumbs have appeared.",
    /// "Waiting for the host to start the game...", "Trying to connect to the host..." and its 30 s / 60 s successors), or why it failed
    const std::string& status_text() const noexcept { return status_; }
    /// The thumb beside a player's name: the host's measured round trip to that seat (the host's own seat is always good)
    LinkQuality seat_quality(uint8_t seat) const noexcept;
    /// A client in the room (or loading the match) whose seat the last Room message names as the leader: the first player in a dedicated server's room, then, when
    /// it leaves, the earliest of those who are left (protocol 7). Never true for the host of a LAN room, and in a server's room that does not allow an early start.
    bool is_leader() const noexcept;
    /// The leader asks the server to start the match now with the players who are in the room (StartRequest). False (nothing is sent) when this machine is not the leader, the
    /// room is not open, or fewer than two players are in it: the same answer as the host's START gives when `start_match` refuses (the application plays the can't-go cue).
    /// True means the request was sent, not that the server will start: it starts at once when it can, otherwise nothing happens.
    bool request_start();
    /// The bots that this machine's START asks for when it leads a server's room: a level for each seat (protocol 13; one level for all in protocol 11). None everywhere (the default) is the START
    /// of protocol 7. With a level somewhere, request_start() also works with one player in the room (the server seats a bot of the seat's level in each empty seat that has one, and starts);
    /// Fog of War and bots refuse each other: the server says so in a notice to this machine and starts without them only if two people are there. One level converts to the plan that gives it to
    /// every seat. Set it before the START (the application does from --fill-bots and the screens).
    void set_fill_bots(const FillPlan& plan) noexcept { fill_ = plan; }
    const FillPlan& fill_bots() const noexcept { return fill_; }
    /// The teams that this machine's START asks for (protocol 13): free for all (the default) or a pair of seats, and the other two as a team when both play; the room makes them when the seats
    /// that play can (else the match starts without them and everybody in the room is told why). A LAN host's own START and a server's leader's request carry them.
    void set_start_teams(const sim::StartTeams& teams) noexcept { teams_ = teams; }
    const sim::StartTeams& start_teams() const noexcept { return teams_; }
    /// The teams that the room's own code names (protocol 13: net::room_code_teams of the room that this machine joined; none for the host of a LAN room): the room makes them for EVERY start, when it fills
    /// up and starts by itself too, and ignores the teams of a leader's request.
    sim::StartTeams room_teams() const noexcept { return role_ == Role::Client ? room_code_teams(target_.room) : sim::StartTeams{}; }
    /// The teams that this machine's screens show and its START asks for: the room's own when its code names some, else set_start_teams' (sim::start_teams_for)
    sim::StartTeams effective_teams() const noexcept { return sim::start_teams_for(room_teams(), true, teams_); }
    /// What the status line of the setup screen says to somebody who can START a room that has a fill level or teams (the host of a room on the local network, the leader of a server's room), in
    /// place of the original's "Press START when all players' thumbs have appeared.": "Press START: the empty seats get Medium bots." (one level for every empty seat), "Press START: Red gets
    /// an Easy bot, Black a Hard bot; teams Green + Red against Blue + Black." and, in a room with Fog of War (bots and fog never mix), "Fog of War is on, so START seats no bots."
    static std::string start_prompt(FillLevel level, bool fog);
    /// The same line for a plan and teams over the seats of `room`, and shorter ways to say it for a label that is too narrow (the longest first); empty when there is nothing to say (no bot
    /// would be seated and no teams are chosen: the original's own prompt stands). `room_teams`: the teams are the room's own (its code names them), not a choice of this START: "the room's teams:
    /// Green + Red against Blue + Black" in place of "teams Green + Red against Blue + Black".
    static std::vector<std::string> start_prompt_texts(const FillPlan& plan, const sim::StartTeams& teams, const RoomMsg& room, bool fog, bool room_teams = false);
    /// The foot of the leader's Players' Status box on the 16:9 setup screen (two lines, each in the ways it can be said, the longest first; the screen takes the first that fits): "Empty seats at
    /// START:" / "Medium bots" for one level in every empty seat (the footer of protocol 11), "Empty seats at START:" / "Red Easy, Black Hard" for a level for each seat, "Teams at START:" / "Green + Red
    /// against Blue + Black" for teams alone, and with both the bots in the first line and "Teams: ..." in the second. Empty lines: nothing to say (no bot would be seated, no teams, Fog of War).
    /// `room_teams` (the teams are the room's own): "Room teams:" in place of "Teams at START:", and "Room teams: ..." first in the second line.
    struct FooterTexts {
        std::array<std::vector<std::string>, 2> line;
        bool empty() const noexcept { return line[0].empty() && line[1].empty(); }
    };
    static FooterTexts start_footer(const FillPlan& plan, const sim::StartTeams& teams, const RoomMsg& room, bool fog, bool room_teams = false);
    /// The START prompt that status_text() shows in the room, in its other (shorter) ways of saying it, longest first: the application picks the longest that fits its label. Empty while the
    /// original's own prompt stands (a guest, a START that seats no bot and makes no teams).
    const std::vector<std::string>& prompt_texts() const noexcept { return prompts_; }
    /// The words of a refusal, as the status line shows them when a join fails (the original's text for a dropped machine, the remake's for the rest). `in_browser`: the game runs in a web page,
    /// where reloading the page is how a player gets the current version (a tab that was opened before the server was updated is the old game), so the refusal for another version says so; every
    /// other refusal is the same words everywhere (the desktop start menu has texts of its own, Application::menu_failure_text). `room`: the code that was asked for. NoSuchRoom for a code that
    /// begins "demo-" (the server makes the room of such a code when somebody comes) says that the server cannot make a room now, which is what a full cap of demo rooms is; for any other code, or
    /// none, it says that there is no such room.
    static std::string reject_text(RejectReason reason, bool in_browser, const std::string& room = std::string());

    // ---- the waiting room's chat (protocol 11) ------------------------------------------------------------------------------------------------------
    /// Says a line to everybody in the room: in the waiting room and while the map loads (and, as ever, during the match: then `team` counts; before it nobody has a team and the flag is
    /// ignored). Printable ASCII, at most kMaxChatChars characters. The room relays it to everybody, this machine included, so the line comes back as an Event::Chat. True when the line was
    /// sent (false: not in a room or a match, nothing to say).
    bool chat(const std::string& text, bool team = false);
    /// Shows `text` on the status line for five seconds (the line that this player has just said: the room's own copy of it is not shown to its sender); cut to kStatusNoticeChars (a
    /// bound: the setup screen draws two lines of 14 px and fits what it draws to them by pixels, MapSelectScreen::status_lines)
    void show_notice(std::string text);
    /// The longest text that a notice keeps (the rest is "..."): a line of 100 characters with the sender's name in front is cut here
    static constexpr size_t kStatusNoticeChars = 84;
    /// Whether the lines that arrive in the room are also shown on the status line for five seconds, as the classic setup screen does (true, the default). The 16:9 setup screen has a chat box
    /// that shows them (MapSelectScreen::set_chat_panel), so the application turns the mirror off while that box is on screen: the status line then keeps the prompt.
    void set_chat_status_mirror(bool on) noexcept { chat_status_mirror_ = on; }
    bool chat_status_mirror() const noexcept { return chat_status_mirror_; }
    /// The lines that arrived in the room since the last call (a guest's, the host's, and the room's own notices to this machine: ChatLine::notice())
    std::vector<ChatLine> take_pregame_chat();
    /// Every line of the waiting room (the last 200), kept after the match begins so that the match's chat log can start with them: the application decides
    const std::vector<ChatLine>& pregame_chat() const noexcept;

    // ---- what the network costs this player (latency.hpp), shown next to the frame rate -----------------------------------------------------------
    /// The round trip to the host in ms, as this machine measures it (the mean of its last few Ping / Pong round trips, in the room and in the match). The host has no
    /// link to itself: 0 (a guest that took over as host too). Empty before the first answer came back, when there is nothing to measure, and when the last answer is older
    /// than three seconds (PingMeter::kStaleAfterMs: the host says nothing, or this machine was not run for a while: an old reading is not shown as if it were one).
    std::optional<uint32_t> ping_ms() const;
    /// The delay of this player's own commands in ms: the real time from sending one (a guest) or handing it to the sequencer (the host) to the tick that applies it on
    /// this machine, the median of the last five. Empty until a command has been applied, and when the last one was applied more than ten seconds ago.
    std::optional<uint32_t> command_delay_ms() const;

    // ---- the host's controls in the room ---------------------------------------------------------------------------------------------------------
    /// Host only: the map every machine will load (a plain .LVL file name of the maps folder) and the Fog of War option
    void set_map(const std::string& map_name);
    /// Fog of War on is refused while a bot sits in the room (a bot would see through it): the option stays off and the status line says why
    void set_fog(bool fog);
    /// Host only, in the room: a computer player takes `seat` (docs/BOTS.md). The room shows it as a bot with the good thumb; the machine's own bot (ants_ai) is
    /// built by the application once the match begins and sends its commands with submit_bot(). False when the seat is taken, the room is full or Fog of War is on.
    bool add_bot(uint8_t seat, const std::string& name);
    void remove_bot(uint8_t seat);
    /// Host only, in the room (protocol 11: the host's own START with a fill level; a level for each seat since 13): seats a bot named fill_bot_name(level) in every empty seat that has a level
    /// (a room on the local network has four seats) and appends the seats to `seats` when it is given; returns how many. Fog of War is refused (a notice on the status line, 0 seated). The
    /// application builds the bots of those seats when the match begins, as for --bot, and takes them out again (remove_bot) when the start is cancelled.
    size_t fill_bots(const FillPlan& plan, std::vector<uint8_t>* seats = nullptr);
    /// Host only, during the match: a command of the bot at `seat` (the issuer is stamped with that seat). False unless this machine is the host and the seat is a bot seat.
    /// The simulation's verdict arrives with the turn, like every command's; a bot ignores it.
    bool submit_bot(uint8_t seat, const sim::Command& command);
    /// Host only, at least two players: sends Start (the map named in the room, `seed`, the roster, the teams of set_start_teams when the seats that play can make them) to everybody and
    /// expects report_loaded(). Teams that the seats cannot make: the match starts without them and every person in the room is told why (the notice "No teams: ...", also on this machine's status line).
    bool start_match(uint32_t seed, uint64_t map_hash);
    bool can_start() const;
    /// The map, seed, roster and names of the match (valid from Loading on)
    const StartMsg& start_info() const noexcept { return start_; }
    /// The application loaded the map of start_info() (true) or could not (a missing file, a different file)
    void report_loaded(bool ok);

    // ---- the match ---------------------------------------------------------------------------------------------------------------------------------
    /// The HUD's sink: the command is stamped with this machine's seat and queued for the next turn; the answer carries the predicted acknowledgement
    /// (the ant that will say "Yessir!") so that the click feels immediate although the order reaches the simulation a few turns later. While the prediction is on (below), an order
    /// that it handles (group moves, special orders, attacks, Stop) is also applied to the predicted engine at once and the answer is that engine's own verdict.
    sim::CommandResult submit(const sim::Command& command) override;

    // ---- the prediction of one's own orders (prediction.hpp, docs/NETWORK_PORT.md "Prediction of one's own orders") --------------------------------------
    /// Off by default (opt-in: the application turns it on when asked to); off, every order waits for its turn as before. A local game has no delay to hide. Takes effect with the next update:
    /// a match that is asked for it makes the prediction then (it begins with the next tick), and one that is not asked never makes it; switched off, it is destroyed (not suspended).
    void set_prediction_enabled(bool on) noexcept { prediction_enabled_ = on; }
    bool prediction_enabled() const noexcept { return prediction_enabled_; }
    /// Told whenever the predicted engine is destroyed (switched off, the match left, this object destroyed): whoever points at its engine (the HUD's special-target query) points elsewhere
    /// at once. Called from update(), shutdown and the destructor.
    void set_on_prediction_dropped(std::function<void()> fn) { on_prediction_dropped_ = std::move(fn); }
    /// The application suspends the prediction where only it knows that the match is not simply following the live stream (a hidden page's background steps, a screen over the match)
    void set_prediction_suspended(bool suspended) noexcept { app_suspends_prediction_ = suspended; }
    /// True during a cool-down of the prediction (Prediction::Config::budget_ns): its work cost more than the budget too often, so it is off for a while: the confirmed engine is shown and
    /// orders go as they did before the prediction. It begins again by itself.
    bool prediction_cooling_down() const noexcept { return prediction_ != nullptr && prediction_->cooling_down(); }
    /// The budget of the prediction's work for the NEXT prediction (the tests and the diagnostics): a rebuild or a run of predicted ticks that costs more than `ns` is a strike, `strikes` of
    /// them within 10 s start a cool-down of `cooldown_ticks`. The defaults are Prediction::Config's; default_prediction_budget_ns() is what a NetGame starts with (a test program that
    /// runs under a sanitizer raises it)
    void set_prediction_budget(uint64_t ns, uint32_t strikes, uint32_t cooldown_ticks = Prediction::Config{}.cooldown_ticks) noexcept {
        prediction_budget_ns_ = ns;
        prediction_budget_strikes_ = strikes;
        prediction_cooldown_ticks_ = cooldown_ticks;
    }
    /// The tests: something that runs inside every timed block of the prediction (Prediction::Config::work_hook)
    void set_prediction_work_hook(std::function<void()> hook) { prediction_work_hook_ = std::move(hook); }
    /// The tests: the clocks that the prediction's timed blocks are measured with, in ns (Prediction::Config::wall_clock, cpu_clock; empty: the real ones), for the NEXT prediction
    void set_prediction_clocks(std::function<uint64_t()> wall, std::function<uint64_t()> cpu) {
        prediction_wall_clock_ = std::move(wall);
        prediction_cpu_clock_ = std::move(cpu);
    }
    static uint64_t& default_prediction_budget_ns() noexcept;
    /// True while the predicted engine is the one that the screen shows: a match is running, the prediction is on and not suspended, and at least one tick has run
    bool predicting() const noexcept { return prediction_ != nullptr && prediction_->active(); }
    /// The engine that the screen shows and that the HUD asks (the cursor, the selection, the panel, the minimap): the predicted engine while predicting(), else the confirmed one that
    /// this object was given. Never the engine that orders go to: they go through submit().
    sim::SimulationEngine& view_engine();
    const Prediction* prediction() const noexcept { return prediction_.get(); }
    Prediction* prediction() noexcept { return prediction_.get(); }
    /// The match's lock-step runner (null before a match has begun and after it); for the tests and the diagnostics: the application does not drive it
    LockstepRunner* runner() const;
    /// (the chat() above is the match's too) the message goes through the host and comes back to everybody (own text included); use the callback to show it
    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }
    /// After every simulation tick / for every applied command (see LockstepRunner); may be set before the match begins
    void set_on_tick(std::function<void()> fn);
    void set_on_command(std::function<void(const sim::Command&, const sim::CommandResult&)> fn);
    /// The match is over: the host stops sealing turns
    void freeze();

    /// True while the runner stands at a turn boundary and the next turn has not arrived (see LockstepRunner::stalled)
    bool stalled() const;
    /// How long the runner has stood still for want of a turn: the time since it last ran a tick (0 while it runs; the application shows "Waiting for the other players..." at
    /// one second)
    uint32_t stalled_ms() const;
    /// The host: the seat that holds the game up (255 when none does). A dedicated server never waits for anybody (see lag_notice).
    uint8_t laggard() const;
    /// A dedicated server's room: the player that the server announced as lagging, and how far behind the match it is in ms. The match goes on without waiting for it (it
    /// catches up on its own); this is the one-line notice for the others. Empty when nobody lags, and in games of the local network (their host waits: laggard()).
    struct LagNotice {
        uint8_t seat{255};
        uint32_t behind_ms{0};
    };
    std::optional<LagNotice> lag_notice() const;
    /// This machine is more than 3 s behind the match and runs the backlog down at up to four times normal speed (a dedicated server's room): "Catching up..."
    bool catching_up() const;
    /// The server told this player that it is the one who lags, and how far behind the match it is in ms (a dedicated server's room): the notice that reaches a player whose
    /// own link is slow, so that its backlog is on the way and not in its queue and "Catching up..." has nothing to say. Empty when none, and in games of the local network.
    std::optional<uint32_t> self_lag_behind_ms() const;
    bool desynced() const;
    /// Milliseconds into the current tick, for smooth drawing between ticks
    uint32_t sub_tick_ms() const;
    /// The turn the local machine executes next
    uint32_t turns_executed() const;
    /// The host is gone and the guests are agreeing on a new one: no turns arrive meanwhile (the game shows a message)
    bool electing() const;
    /// How this machine reached its server (valid after a successful join() or join_url(); empty before): the way back makes its new links from it
    const JoinTarget& join_target() const noexcept { return target_; }
    /// The seat that seals the turns now
    uint8_t host_seat() const noexcept { return known_host_; }
    /// What just happened in the match ("Bob is the host now."), for five seconds; empty otherwise
    std::string match_notice() const;

private:
    void update_host();
    void update_client();
    void update_host_session();
    void promote();
    void pump_peers();
    void begin_peer_links();
    /// The part of joining that every transport shares: the uplink is ready, the lobby asks for its room and seat
    void begin_client(std::unique_ptr<Connection> uplink, uint16_t peer_port, const SeatKey& key);
    /// The lobby's Hello for the join target: `key` (all zero for a new player) and the seat that it asks for
    ClientLobby::Config lobby_config(const SeatKey& key, uint8_t want_seat) const;
    void close_peer_links();
    void refresh_status();
    void set_notice(std::string text);
    void begin_match();
    void install_hooks();
    void make_prediction();
    void drop_prediction();
    void refresh_prediction();
    /// The lag of the player's orders, as a delay in ms, until the prediction has learned it from the orders: the measured delay of the last orders, else the round trip, half a seal and the
    /// jitter buffer, less the tick that the delay counts and the lag does not
    uint32_t expected_command_delay_ms() const;
    void shutdown_transport();
    void announce_room();
    /// Takes the new lines of the lobby (the room's chat), shows the latest on the status line and queues them for take_pregame_chat()
    void collect_room_chat();
    // The way back (see the head of this file)
    /// A new link to the server, made the way the first one was (TCP natively, a WebSocket in the browser); null when none can be made (the session tries again in two seconds). `for_lobby`: the link
    /// is the lobby's, not the session's (a machine that starts the match from nothing): the browser's socket then says the lobby's Hello when it opens
    std::unique_ptr<Connection> make_link(bool for_lobby = false);
    /// The session wants a new link: makes one and hands it over (attach), a failed attempt too (null)
    void attach_new_link();
    /// The browser's connections tell the application that they have news (set_on_wake); the same function for the first link and every new one
    std::function<void()> wake_function();
    /// What a new link says as its Hello (the session adds the version, the key and the number of turns): the name, the room, the token and this machine's seat
    HelloMsg way_back_hello() const;
    /// The session is lost: a server that restored the match from a record that lost its last turns (docs/NETWORK_PORT.md, "start from nothing") answered BadRequest to a Hello that carried more
    /// turns than it holds; the machine starts the match from nothing with the same key (begin_reload), once per match. True when it did; false: the session ended some other way (end_lost_match)
    bool reload_after_bad_request();
    void begin_reload();
    void end_lost_match();
    /// The key of the seat: handed out (on_key) and let go of (on_forget_key), once per key
    void announce_key(const SeatKey& key, uint8_t seat);
    void forget_key();
    std::string server_text() const;
    /// The lobby's Welcome came: a Hello that showed a key and was answered as a new player's (the match is gone, a new room took the code) forgets the old one; the key of a rejoin is announced at once, the
    /// key of a new player waits for the Start (announce_start_key)
    void note_lobby_welcome();
    /// The Start arrived: the key that the room's Welcome gave a new player is announced now (the match has begun), and a cancelled start takes it back (unannounce_start_key)
    void announce_start_key();
    void unannounce_start_key();
    /// The session's mode changed: a rejoin's Welcome (the key again), the end of the way back (Rejoined)
    void note_session_mode();

    sim::SimulationEngine& sim_;
    Role role_{Role::None};
    Phase phase_{Phase::Off};
    uint8_t seat_{255};
    uint16_t listen_port_{0};
    uint32_t now_{0};
    std::string status_;
    FailReason fail_reason_{FailReason::None};
    RejectReason reject_reason_{RejectReason::BadRequest};
    std::string notice_;                    // a message that replaces the standing prompt for a few seconds (a cancelled start, ...)
    uint32_t notice_until_ms_{0};
    uint32_t phase_since_ms_{0};
    bool loaded_reported_{false};
    RoomMsg room_;
    StartMsg start_;
    std::vector<Event> events_;
    bool desync_reported_{false};
    uint8_t known_host_{255};               // the seat of the host as far as this machine knows (changes with a host migration)
    uint16_t peer_port_{0};                 // guest: the port on which the other guests connect (announced in Hello)
    uint16_t discovery_port_{kLanDiscoveryPort};   // 0: the room is not announced
    bool discovery_loopback_only_{false};
    [[maybe_unused]] bool room_loopback_only_{false};   // host(): the door accepts this machine only, so the announcements stay here too (native builds)
    [[maybe_unused]] uint32_t room_id_{0};              // names the room in the announcements (native builds)
    std::string game_version_;
    FillPlan fill_;                         // the bots that this machine's START asks for: a level for each seat (protocol 11, 13)
    sim::StartTeams teams_;                 // the teams that this machine's START asks for (protocol 13)
    JoinTarget target_;                     // client: how the first link was made (join, join_url)
    bool way_back_{false};                  // client: the session was built to come back by itself (a dedicated server's room that gave this machine a key)
    bool reload_used_{false};               // client: the BadRequest fallback (reload_after_bad_request) was taken in this match
    SeatKey join_key_{};                    // client: the key that the lobby's Hello showed (all zero: a new player)
    bool have_key_{false};                  // client: a key is out (announced, or given to join): the place that keeps it has it until forget_key()
    RejoinKey rejoin_key_;                  // ... and which
    bool welcomed_{false};                  // client: the lobby's Welcome has been looked at (note_lobby_welcome)
    SeatKey pending_key_{};                 // client: the key of a new player that waits for the Start to be announced (announce_start_key); zero: none
    bool start_key_announced_{false};       // client: the key was announced at a Start (a cancelled start takes it back)
    ClientSession::Mode last_mode_{ClientSession::Mode::Normal};      // client: the session's mode at the last look (note_session_mode)
    std::function<void(const RejoinKey&)> on_key_;
    std::function<void(const RejoinKey&)> on_forget_key_;
    std::vector<std::string> prompts_;      // the START prompt of the leader / host in its ways of saying it, longest first (prompt_texts()); empty when the original's prompt stands
    std::vector<ChatLine> pending_chat_;    // the waiting room's lines that take_pregame_chat() has not handed out
    bool chat_status_mirror_{true};         // the lines of the room are shown on the status line too (set_chat_status_mirror)

    std::function<void(const ChatMsg&)> on_chat_;
    std::function<void()> on_wake_;
    std::function<std::unique_ptr<Connection>()> link_maker_;      // (the tests: make_link)
    std::function<std::string(const std::string&)> resolver_;      // (the tests: join's one lookup)
    std::function<void()> on_tick_;
    std::function<void()> on_prediction_dropped_;
    std::function<void(const sim::Command&, const sim::CommandResult&)> on_command_;

    // transport (owned here, borrowed by the lobbies and sessions)
    struct Transport;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<HostLobby> host_lobby_;
    std::unique_ptr<ClientLobby> client_lobby_;
    std::unique_ptr<HostSession> host_session_;
    std::unique_ptr<ClientSession> client_session_;
    std::unique_ptr<Prediction> prediction_;     // (after the sessions: it holds their runner and goes first; made by refresh_prediction() when it is wanted, never otherwise)
    bool prediction_enabled_{false};
    bool app_suspends_prediction_{false};
    uint64_t prediction_budget_ns_{default_prediction_budget_ns()};
    uint32_t prediction_budget_strikes_{Prediction::Config{}.budget_strikes};
    uint32_t prediction_cooldown_ticks_{Prediction::Config{}.cooldown_ticks};
    std::function<void()> prediction_work_hook_;
    std::function<uint64_t()> prediction_wall_clock_;
    std::function<uint64_t()> prediction_cpu_clock_;
};

}  // namespace ants::net

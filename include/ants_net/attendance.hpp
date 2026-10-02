#pragma once

// Who is at the table of a running match (docs/NETWORK_PORT.md, "Reconnect"): the rules by which a dedicated server holds the seat of a player whose connection was lost, pauses
// the match for everybody, lets the players that are there vote on going on without the one who is away, and gives up on a seat. Pure logic without clock, sockets or simulation, like
// the Sequencer: the host's session tells it what it sees (a connection lost, a player back, a vote, the time) and asks what to do (is the match paused, which seats are dropped).
//
// A seat that a person holds is in one of four states (a bot's seat and an empty seat have none: Empty):
//
//   Present     connected and playing
//   Absent      the connection is LOST: the seat is held, its key stays valid, nothing is sealed (the match is paused). The session decides what "lost" is (the link closed, a send
//               failed, nothing at all arrived for 10 s): a player who is slow but still talks is not lost, only a laggard, and never pauses anybody (docs/NETWORK_PORT.md)
//   CatchingUp  the player is back (a Hello with the key was accepted) and is being given the turns of the match: still paused
//   Dropped     the seat is gone for good (its Drop is sealed): nothing brings it back, the key only tells its owner so
//
//   event                          from                           to            what it does
//   seat_humans(mask)              -                              Present       the match begins
//   lost(seat)                     Present                        Absent        the absence begins (it is counted from now); the seat's own votes are gone, so are the votes about it
//   returning(seat)                Absent, CatchingUp, Present    CatchingUp    a Hello with the key, if the seat's budgets allow it (below): the away time goes on from the loss (a Present seat, whose old link was
//                                                                               not known to be dead, is away from now). A Hello of an attempt that is in progress changes nothing of that attempt (its stall clock, its percent)
//   progress(seat, percent)        CatchingUp                     CatchingUp    only a higher percent than the last one is a sign of life: it starts the stall clock again
//   caught_up(seat)                CatchingUp                     Present       the hashes agreed; the absence is over and counts at least min_absence_ms
//   catch_up_failed(seat)          CatchingUp                     Absent        the link closed, the hash differed, or update() found no progress for catch_up_stall_ms or the absence's catch-up time used up
//   dropped(seat)                  any but Empty, Dropped         Dropped       the seat left (Leave), was thrown out (violations) or its player said so in a vote: final
//   update(now)                    Absent, CatchingUp, Present    Dropped       the vote was won, or the match's pauses used up max_pause_ms (every seat that is not Present); the seats are returned for the session to Drop
//
// Anything else is a no-op that returns false (a seat that is not in the state the event needs, a seat that is no seat of the match, a Dropped seat: Dropped is final).
//
// AWAY TIME is counted per seat and in TOTAL over the match: the absences that are over, added up (each counts at least min_absence_ms: a connection that flaps every second is put
// to the vote after six losses, not after thirty), plus the absence that goes on. The vote opens for the seat that has been away longest when its total reaches vote_after_ms (30 s).
// A seat that is catching up has come back and is never the subject of a vote, unless it FLAPS (below).
//
// ONE VOTE AT A TIME, about the Absent seat with the longest total (the lowest seat of a tie) once it is at least vote_after_ms, or about a seat that flaps: the connected players (the
// Present seats of persons, not bots, not seats that are away, not the subject itself) choose Keep waiting or Continue without it, and the seat is dropped when MORE THAN HALF of the
// connected players choose Continue (2 * votes > connected: 1 of 1, 2 of 2, 2 of 3, 3 of 4; a player who does not vote counts as not wanting to continue). A voter that is lost leaves the
// count at once and its vote is gone for good; votes about a seat are gone when that seat changes state (except for a seat that flaps); a vote only counts while its seat is the subject.
// There is no automatic drop of a seat in time: the players that are there wait as long as they want, within the cap.
//
// FLAPPING. A seat whose connection is lost three times within 60 s (kFlapLosses, kFlapWindowMs) FLAPS: every return of such a seat starts the pause again, and each loss costs the others at
// least a few seconds; a vote about it is put to the others at once, whatever its state (Absent, CatchingUp, or back and Present), and stays open across its returns, with the votes
// already cast, until it is decided or until 60 s have gone by without another loss.
//
// THE CAP. The match's TOTAL paused time (the time in which at least one seat was Absent or CatchingUp, each moment counted once however many seats were away, and each pause counted at
// least min_absence_ms: a connection that flaps reaches the cap too) may not pass max_pause_ms (30 minutes by default). The cap is ABSOLUTE: when it is reached every seat that is not Present
// is dropped at once, Absent or CatchingUp, whatever progress it shows (and a seat that goes Absent after that is dropped by the next update(): the match has no pause left). cap_s() is
// what the match has left, for the screens.
//
// THE BUDGETS of a seat, so that a key holder cannot hold the room or the server's bandwidth (docs/NETWORK_PORT.md):
//   - the CATCH-UP TIME of one absence (max_catch_up_ms, 5 minutes): the time that the seat spent CatchingUp, over all the attempts of the absence. When it is used up update() fails the
//     catch-up (the seat is Absent: the vote and the cap apply) and returning() refuses
//   - at most rejoin_attempts (3) accepted Hellos per rejoin_window_ms (a minute), and at most stream_factor (3) times the log's size streamed per stream_window_ms (10 minutes), at least
//     stream_floor_bytes (1 MiB) of it: returning() refuses beyond either (RejoinFailed). A refused Hello changes nothing and counts nothing against the seat
//
// THE RESUME COUNTDOWN. When a pause ends (a seat is back and verified, or a vote or the cap dropped the last seat that was missing) after at least resume_min_pause_ms, the match is held
// for resume_countdown_ms more before the next turn is sealed (counting_down(), resume_s(): the players see the seconds): the time of the countdown is not part of the pause (it does not
// count toward the cap, nor toward a catch-up), and a seat that is lost during it cancels it (the pause rules apply, a new countdown follows that pause). A pause that is shorter (a blip) resumes
// at once. resume_countdown_ms 0: no countdown.
//
// All times are 32-bit milliseconds of the host's clock (clock.hpp: it wraps after 49.7 days): they are only ever used as DIFFERENCES, and a reading that is older than the event it is
// compared with counts as no time. Every time that is stored is taken from the clock when the event happens, never left at 0.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

/// The defaults of the rules above (a room may choose others: ants_server)
inline constexpr uint32_t kVoteAfterMs = 30000;             // the vote opens for a seat that has been away this long in all
inline constexpr uint32_t kCatchUpStallMs = 20000;          // a seat that is catching up and shows no progress for this long is absent again
inline constexpr uint32_t kMinAbsenceMs = 5000;             // an absence that is over counts at least this much toward the seat's total away time, and toward the match's pauses
inline constexpr uint32_t kMaxPauseMs = 30u * 60u * 1000u;  // the match's pauses may last this long in all
inline constexpr uint32_t kMaxCatchUpMs = 5u * 60u * 1000u; // one absence may spend this long catching up in all, over all its attempts
inline constexpr uint32_t kRejoinAttempts = 3;              // Hellos with a seat's key that are accepted per kRejoinWindowMs ...
inline constexpr uint32_t kRejoinWindowMs = 60000;
inline constexpr uint32_t kStreamFactor = 3;                // ... and bytes of the log that are streamed to one seat per kStreamWindowMs: this many times the log's size ...
inline constexpr uint32_t kStreamWindowMs = 600000;
inline constexpr size_t kStreamFloorBytes = size_t{1} << 20;    // ... and at least this much (a log of a few KB is no reason to refuse the fifth reload of a page)
inline constexpr uint32_t kFlapLosses = 3;                  // a seat that is lost this many times within kFlapWindowMs flaps
inline constexpr uint32_t kFlapWindowMs = 60000;
inline constexpr uint32_t kResumeCountdownMs = 10000;       // the match is held this long after a pause before it goes on (a room's default) ...
inline constexpr uint32_t kResumeMinPauseMs = 3000;         // ... if the pause lasted at least this long

class Attendance {
public:
    struct Config {
        uint32_t vote_after_ms = kVoteAfterMs;
        uint32_t catch_up_stall_ms = kCatchUpStallMs;
        uint32_t min_absence_ms = kMinAbsenceMs;
        uint32_t max_pause_ms = kMaxPauseMs;
        uint32_t max_catch_up_ms = kMaxCatchUpMs;
        uint32_t rejoin_attempts = kRejoinAttempts;             // 1 .. kMaxRejoinAttempts
        uint32_t rejoin_window_ms = kRejoinWindowMs;
        uint32_t stream_factor = kStreamFactor;
        uint32_t stream_window_ms = kStreamWindowMs;
        size_t stream_floor_bytes = kStreamFloorBytes;
        uint32_t flap_losses = kFlapLosses;                     // 2 .. kMaxFlapLosses
        uint32_t flap_window_ms = kFlapWindowMs;
        uint32_t resume_countdown_ms = 0;                       // the library's default is no countdown (a room's is kResumeCountdownMs: ants_server)
        uint32_t resume_min_pause_ms = kResumeMinPauseMs;
    };
    static constexpr uint32_t kMaxRejoinAttempts = 8;
    static constexpr uint32_t kMaxFlapLosses = 8;
    enum class State : uint8_t { Empty, Present, Absent, CatchingUp, Dropped };
    /// What returning() / rejoin_check() say about a Hello with a seat's key
    enum class Rejoin : uint8_t {
        Allowed,
        NotHeld,            // the seat is Dropped or Empty (or no seat of the match): there is nothing to come back to
        CatchUpSpent,       // the catch-up time of this absence is used up
        TooManyAttempts,    // rejoin_attempts Hellos were accepted in the last rejoin_window_ms
        TooMuchStreamed     // the log has been streamed to this seat as often as the budget allows in the last stream_window_ms
    };

    Attendance() : Attendance(Config{}) {}
    explicit Attendance(Config config) : cfg_(clamped(config)) {}

    /// The match begins at `now_ms`: the seats of persons (bit s of `mask`: not a bot, not an empty seat) take their places, all present. Forgets everything that was before.
    void seat_humans(uint8_t mask, uint32_t now_ms);
    State state(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS ? seats_[seat].state : State::Empty; }

    /// A present seat's connection is lost: it is absent from now on, and the match is paused (the pause begins now when no other seat was away). False unless the seat was Present.
    /// The loss is counted for the flapping rule, and the seat's catch-up time starts again (a new absence).
    bool lost(uint8_t seat, uint32_t now_ms);
    /// What a Hello with the seat's key would be told now (nothing is changed): Allowed, or why not. `planned_bytes` is what the stream to the seat would take (the log's packed bytes
    /// from the turn that the player has), `log_bytes` the log's size; both 0: the budget of bytes is not asked.
    Rejoin rejoin_check(uint8_t seat, uint32_t now_ms, size_t planned_bytes = 0, size_t log_bytes = 0) const noexcept;
    /// A Hello with the seat's key was accepted: the seat is catching up from now on. The seat was Absent (its away time goes on from the loss), already CatchingUp (a second try of the
    /// same attempt: its stall clock, its percent and its start are NOT touched, so a key holder that says Hello again and again cannot keep the pause alive) or Present (the old connection
    /// was not known to be dead yet: the seat is away from now, the pause begins). False, and nothing changes, when rejoin_check() refuses (Dropped or Empty seats, the budgets); the
    /// refusals are counted (rejoins_refused). An accepted Hello counts against the attempts of the minute.
    bool returning(uint8_t seat, uint32_t now_ms, size_t planned_bytes = 0, size_t log_bytes = 0);
    /// The session streamed `bytes` of the log to the seat: they count against its budget of bytes
    void charge_stream(uint8_t seat, uint32_t now_ms, size_t bytes);
    /// An acknowledgement of the catch-up: the seat has executed `percent_done` percent of the turns. Only a percent HIGHER than the last one (0 after returning()) is progress: it starts the
    /// stall clock again and returns true. The same acknowledgement again, or an older one, is no sign that the player is getting anywhere: false, the clock keeps running (so a
    /// client cannot hold the pause for ever by repeating itself). Percents above 100 are 100.
    bool progress(uint8_t seat, uint8_t percent_done, uint32_t now_ms);
    /// The hashes agreed: the seat is Present again. Its absence is over and counts at least min_absence_ms toward its total away time; the pause ends when no other seat is away.
    bool caught_up(uint8_t seat, uint32_t now_ms);
    /// The catch-up failed (the link closed, the hash differed): the seat is Absent again, its away time kept running from the loss, and the time that the attempt took is spent
    /// from the absence's catch-up time.
    bool catch_up_failed(uint8_t seat, uint32_t now_ms);
    /// A connected player's choice about the seat that the vote is about. False (nothing is recorded) when the voter is not a Present seat or is the subject itself, or `subject` is not the
    /// seat that the vote is about now (no vote is open, or it is about another seat): such a vote crossed a change of state on the wire, it is no offence. A second choice replaces the first.
    bool vote(uint8_t voter, uint8_t subject, bool continue_without, uint32_t now_ms);
    /// A seat that left by itself (it said Leave), was thrown out (violations), or was dropped by the room in any other way: final at once, whatever its state was.
    bool dropped(uint8_t seat, uint32_t now_ms);

    /// Advances the timers, in this order: a catch-up that has shown no progress for catch_up_stall_ms, or whose absence has used up max_catch_up_ms, ends (the seat is Absent again); a
    /// seat that has stopped flapping closes its vote; a vote that is won drops its seat; when the match's pauses have used up max_pause_ms every seat that is not Present is dropped.
    /// Returns the seats that were dropped by this call (their state is Dropped): the session seals their Drops.
    std::vector<uint8_t> update(uint32_t now_ms);

    /// The match waits for a seat: a seat is Absent or CatchingUp (the resume countdown is not a pause: see counting_down)
    bool paused() const noexcept;
    /// The resume countdown after a pause is running at `now_ms`: the match is held for the seats' sake no longer, and goes on when it ends
    bool counting_down(uint32_t now_ms) const noexcept;
    /// What is left of the resume countdown in whole seconds, rounded up (0: none is running)
    uint8_t resume_s(uint32_t now_ms) const noexcept;
    /// How long the match has been held in all, in ms: its pauses as they really lasted (not the minimum that the cap counts) and its countdowns: the time that a wall-clock limit on the
    /// play must leave out
    uint32_t held_ms(uint32_t now_ms) const noexcept;
    /// How long the seat has been away in this match, in ms: its absences that are over (each at least min_absence_ms) and the one that goes on (Absent or CatchingUp). 0 for a seat that
    /// has not been away; what a seat had been when it was Dropped stays.
    uint32_t away_ms(uint8_t seat, uint32_t now_ms) const noexcept;
    /// The match's paused time so far, in ms, as the cap counts it: the moments at which a seat was Absent or CatchingUp, each moment once, and every pause at least min_absence_ms
    uint32_t pause_ms(uint32_t now_ms) const noexcept;
    /// What the pauses may still last before the cap: max_pause_ms less pause_ms, 0 when it is used up
    uint32_t cap_left_ms(uint32_t now_ms) const noexcept;
    /// The same in whole seconds, rounded up (so it is 0 exactly when the cap is reached), at most 0xFFFF (kCapSecondsMore): what Presence tells
    uint16_t cap_s(uint32_t now_ms) const noexcept;
    bool cap_reached(uint32_t now_ms) const noexcept { return cap_left_ms(now_ms) == 0; }
    /// The time that the seat's absence has spent catching up so far, over all its attempts, in ms
    uint32_t catching_up_ms(uint8_t seat, uint32_t now_ms) const noexcept;
    /// The seat flaps now: it was lost flap_losses times within flap_window_ms, and has not stopped for that long
    bool flapping(uint8_t seat, uint32_t now_ms) const noexcept;

    /// The seat that the vote is about now: the seat that flaps, or the Absent seat that has been away longest once that is at least vote_after_ms (of two such seats the one that has been
    /// away longer; the lowest seat of a tie); 255 when no vote is open
    uint8_t vote_subject(uint32_t now_ms) const noexcept;
    /// The connected players: seats of persons that are Present, except `except` (the subject of the vote does not vote on itself). They are the voters, and more than half of them must choose Continue
    uint8_t connected_humans(uint8_t except = 255) const noexcept;
    /// The rule of the vote: `votes` of `connected` players chose "continue without it" and that is MORE THAN HALF of them (2 * votes > connected: 1 of 1, 2 of 2, 2 of 3, 3 of 4; never
    /// when nobody is connected). A match has four seats, so a missing one leaves at most three to vote; the rule is general.
    static bool vote_won(uint8_t votes, uint8_t connected) noexcept { return connected > 0 && 2u * votes > connected; }
    /// How many of the connected players chose "continue without it" about `subject` (0 for a seat that is no seat)
    uint8_t votes_for_continue(uint8_t subject) const noexcept;
    /// The percent that a seat that is catching up has executed (0 for any other)
    uint8_t percent(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && seats_[seat].state == State::CatchingUp ? seats_[seat].percent : uint8_t{0}; }

    /// What every player is told: the seats that are missing (longest away first), the vote, the cap and the resume countdown. `viewer` is the receiver's seat (its own choice goes in
    /// `your_vote`), 255 for none.
    PresenceMsg presence_for(uint8_t viewer, uint32_t now_ms) const;

    /// Counters for the room's status: seats dropped by a won vote, seats dropped by the cap, absences that ended with a seat that caught up, Hellos with a key that were refused for a
    /// budget (the catch-up time, the attempts, the bytes), absences whose catch-up time ran out, and the bytes of the log that were streamed (all seats)
    uint32_t drops_by_vote() const noexcept { return drops_by_vote_; }
    uint32_t drops_by_cap() const noexcept { return drops_by_cap_; }
    uint32_t rejoins() const noexcept { return rejoins_; }
    uint32_t rejoins_refused() const noexcept { return refused_; }
    uint32_t catch_up_expired() const noexcept { return expired_; }
    uint64_t streamed_bytes() const noexcept { return streamed_total_; }

    const Config& config() const noexcept { return cfg_; }

private:
    struct Charge {
        uint32_t at_ms{0};                              // when the first of these bytes was streamed
        uint64_t bytes{0};
    };
    struct Seat {
        State state{State::Empty};
        uint32_t since_ms{0};                           // Absent, CatchingUp: when the absence that goes on began (the away time is counted from here); otherwise when the seat took its state
        uint32_t away_before_ms{0};                     // the absences that are over, added up (saturating)
        uint32_t progress_ms{0};                        // CatchingUp: the time of the return or of the last progress
        uint32_t catch_start_ms{0};                     // CatchingUp: when the attempt that goes on began (a second Hello of it does not change this)
        uint32_t catching_before_ms{0};                 // the time that the earlier attempts of this absence spent catching up
        uint8_t percent{0};                             // CatchingUp: the highest percent acknowledged
        std::array<uint8_t, sim::MAX_PLAYERS> vote{};   // vote[voter] about THIS seat: 0 none, 1 keep waiting, 2 continue without it
        std::array<uint32_t, kMaxFlapLosses> losses{};  // when the seat was lost last, newest first (losses_n of them are valid)
        uint8_t losses_n{0};
        bool flap{false};                               // the seat flaps (and the vote about it is open until it stops)
        std::vector<uint32_t> attempts;                 // when the Hellos that were accepted in the last rejoin_window_ms were
        std::vector<Charge> charges;                    // the bytes that were streamed to the seat in the last stream_window_ms (in groups of at most a minute)
    };
    static Config clamped(Config c) noexcept;
    void note_loss(Seat& s, uint32_t now_ms) noexcept;
    static bool away_state(State s) noexcept { return s == State::Absent || s == State::CatchingUp; }
    void clear_votes(uint8_t seat, bool keep_about) noexcept;   // the votes the seat cast, and (not with keep_about) the votes about it
    void settle_pause(bool was_paused, uint32_t now_ms) noexcept;
    void drop_seat(uint8_t seat, uint32_t now_ms);
    void fold_countdown(uint32_t now_ms) noexcept;
    uint32_t catching_ms(const Seat& s, uint32_t now_ms) const noexcept;
    bool flap_active(const Seat& s, uint32_t now_ms) const noexcept;

    Config cfg_;
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
    uint32_t pause_before_ms_{0};                       // the pauses that are over, added up as the cap counts them (saturating)
    uint32_t pause_real_before_ms_{0};                  // ... and as they really lasted
    uint32_t pause_since_ms_{0};                        // while paused: when the pause that goes on began
    bool owe_countdown_{false};                         // the pause that goes on cut a countdown short: a countdown follows it, however short it is
    bool resuming_{false};                              // a countdown began at resume_since_ms_ (it is over when resume_countdown_ms have passed)
    uint32_t resume_since_ms_{0};
    uint32_t resume_before_ms_{0};                      // the countdowns that are over, added up
    uint32_t drops_by_vote_{0};
    uint32_t drops_by_cap_{0};
    uint32_t rejoins_{0};
    uint32_t refused_{0};
    uint32_t expired_{0};
    uint64_t streamed_total_{0};
};

}  // namespace ants::net

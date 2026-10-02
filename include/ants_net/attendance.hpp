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
//   returning(seat)                Absent, CatchingUp, Present    CatchingUp    a Hello with the key: the away time goes on from the loss (a Present seat, whose old link was not known to be dead, is away from now)
//   progress(seat, percent)        CatchingUp                     CatchingUp    only a higher percent than the last one is a sign of life: it starts the stall clock again
//   caught_up(seat)                CatchingUp                     Present       the hashes agreed; the absence is over and counts at least min_absence_ms
//   catch_up_failed(seat)          CatchingUp                     Absent        the link closed, the hash differed, or update() found no progress for catch_up_stall_ms; the away time kept running
//   dropped(seat)                  any but Empty, Dropped         Dropped       the seat left (Leave), was thrown out (violations) or its player said so in a vote: final
//   update(now)                    Absent                         Dropped       the vote was won, or the match's pauses used up max_pause_ms; the seats are returned for the session to Drop
//
// Anything else is a no-op that returns false (a seat that is not in the state the event needs, a seat that is no seat of the match, a Dropped seat: Dropped is final).
//
// AWAY TIME is counted per seat and in TOTAL over the match: the absences that are over, added up (each counts at least min_absence_ms: a connection that flaps every second is put
// to the vote after six losses, not after thirty), plus the absence that goes on. The vote opens for the seat that has been away longest when its total reaches vote_after_ms (30 s).
// A seat that is catching up has come back and is never the subject of a vote.
//
// ONE VOTE AT A TIME, about the Absent seat with the longest total (the lowest seat of a tie) once it is at least vote_after_ms: the connected players (the Present seats of persons:
// not bots, not seats that are away) choose Keep waiting or Continue without it, and the seat is dropped when MORE THAN HALF of the connected players choose Continue
// (2 * votes > connected: 1 of 1, 2 of 2, 2 of 3, 3 of 4; a player who does not vote counts as not wanting to continue). A voter that is lost leaves the count at once and its vote
// is gone for good; votes about a seat are gone when that seat changes state; a vote only counts while its seat is the subject. There is no automatic drop of a seat in time: the
// players that are there wait as long as they want, within the cap.
//
// THE CAP. The match's TOTAL paused time (the time in which at least one seat was Absent or CatchingUp, each moment counted once however many seats were away) may not pass
// max_pause_ms (30 minutes by default). When it does, every Absent seat is dropped at once (and a seat that goes Absent after that is dropped by the next update(): the match has no
// pause left). A CatchingUp seat keeps its chance while it is making progress; when its progress stops for catch_up_stall_ms it is Absent again and the cap takes it. The 5 s
// minimum of a loss counts for the vote only: the pause budget is the time that the others really waited. cap_s() is what the match has left, for the screens.
//
// All times are 32-bit milliseconds of the host's clock (clock.hpp: it wraps after 49.7 days): they are only ever used as DIFFERENCES, and a reading that is older than the event it is
// compared with counts as no time. Every time that is stored is taken from the clock when the event happens, never left at 0.

#include <array>
#include <cstdint>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

/// The defaults of the rules above (a room may choose others: ants_server)
inline constexpr uint32_t kVoteAfterMs = 30000;             // the vote opens for a seat that has been away this long in all
inline constexpr uint32_t kCatchUpStallMs = 20000;          // a seat that is catching up and shows no progress for this long is absent again
inline constexpr uint32_t kMinAbsenceMs = 5000;             // an absence that is over counts at least this much toward the seat's total away time
inline constexpr uint32_t kMaxPauseMs = 30u * 60u * 1000u;  // the match's pauses may last this long in all

class Attendance {
public:
    struct Config {
        uint32_t vote_after_ms = kVoteAfterMs;
        uint32_t catch_up_stall_ms = kCatchUpStallMs;
        uint32_t min_absence_ms = kMinAbsenceMs;
        uint32_t max_pause_ms = kMaxPauseMs;
    };
    enum class State : uint8_t { Empty, Present, Absent, CatchingUp, Dropped };

    Attendance() : Attendance(Config{}) {}
    explicit Attendance(Config config) : cfg_(config) {}

    /// The match begins at `now_ms`: the seats of persons (bit s of `mask`: not a bot, not an empty seat) take their places, all present. Forgets everything that was before.
    void seat_humans(uint8_t mask, uint32_t now_ms);
    State state(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS ? seats_[seat].state : State::Empty; }

    /// A present seat's connection is lost: it is absent from now on, and the match is paused (the pause begins now when no other seat was away). False unless the seat was Present.
    bool lost(uint8_t seat, uint32_t now_ms);
    /// A Hello with the seat's key was accepted: the seat is catching up from now on, with the stall clock starting now. The seat was Absent (its away time goes on from the loss),
    /// already CatchingUp (a second try: the percent starts at 0 again) or Present (the old connection was not known to be dead yet: the seat is away from now, the pause begins).
    /// False for a seat that is Dropped or Empty.
    bool returning(uint8_t seat, uint32_t now_ms);
    /// An acknowledgement of the catch-up: the seat has executed `percent_done` percent of the turns. Only a percent HIGHER than the last one (0 after returning()) is progress: it starts the
    /// stall clock again and returns true. The same acknowledgement again, or an older one, is no sign that the player is getting anywhere: false, the clock keeps running (so a
    /// client cannot hold the pause for ever by repeating itself). Percents above 100 are 100.
    bool progress(uint8_t seat, uint8_t percent_done, uint32_t now_ms);
    /// The hashes agreed: the seat is Present again. Its absence is over and counts at least min_absence_ms toward its total away time; the pause ends when no other seat is away.
    bool caught_up(uint8_t seat, uint32_t now_ms);
    /// The catch-up failed (the link closed, the hash differed): the seat is Absent again, its away time kept running from the loss.
    bool catch_up_failed(uint8_t seat, uint32_t now_ms);
    /// A connected player's choice about the seat that the vote is about. False (nothing is recorded) when the voter is not a Present seat, or `subject` is not the seat that the
    /// vote is about now (no vote is open, or it is about another seat): such a vote crossed a change of state on the wire, it is no offence. A second choice replaces the first.
    bool vote(uint8_t voter, uint8_t subject, bool continue_without, uint32_t now_ms);
    /// A seat that left by itself (it said Leave), was thrown out (violations), or was dropped by the room in any other way: final at once, whatever its state was.
    bool dropped(uint8_t seat, uint32_t now_ms);

    /// Advances the timers, in this order: a catch-up that has shown no progress for catch_up_stall_ms ends (the seat is Absent again); a vote that is won drops its seat; when the match's
    /// pauses have used up max_pause_ms every seat that is Absent is dropped. Returns the seats that were dropped by this call (their state is Dropped): the session seals their Drops.
    std::vector<uint8_t> update(uint32_t now_ms);

    /// The match waits: a seat is Absent or CatchingUp
    bool paused() const noexcept;
    /// How long the seat has been away in this match, in ms: its absences that are over (each at least min_absence_ms) and the one that goes on (Absent or CatchingUp). 0 for a seat that
    /// has not been away; what a seat had been when it was Dropped stays.
    uint32_t away_ms(uint8_t seat, uint32_t now_ms) const noexcept;
    /// The match's paused time so far, in ms: the moments at which a seat was Absent or CatchingUp, each moment once (what the cap counts)
    uint32_t pause_ms(uint32_t now_ms) const noexcept;
    /// What the pauses may still last before the cap: max_pause_ms less pause_ms, 0 when it is used up
    uint32_t cap_left_ms(uint32_t now_ms) const noexcept;
    /// The same in whole seconds, rounded up (so it is 0 exactly when the cap is reached), at most 0xFFFF (kCapSecondsMore): what Presence tells
    uint16_t cap_s(uint32_t now_ms) const noexcept;
    bool cap_reached(uint32_t now_ms) const noexcept { return cap_left_ms(now_ms) == 0; }

    /// The seat that the vote is about now: the Absent seat that has been away longest (the lowest seat of a tie) once that is at least vote_after_ms; 255 when no vote is open
    uint8_t vote_subject(uint32_t now_ms) const noexcept;
    /// The connected players: seats of persons that are Present (they are the voters, and more than half of them must choose Continue)
    uint8_t connected_humans() const noexcept;
    /// The rule of the vote: `votes` of `connected` players chose "continue without it" and that is MORE THAN HALF of them (2 * votes > connected: 1 of 1, 2 of 2, 2 of 3, 3 of 4; never
    /// when nobody is connected). A match has four seats, so a missing one leaves at most three to vote; the rule is general.
    static bool vote_won(uint8_t votes, uint8_t connected) noexcept { return connected > 0 && 2u * votes > connected; }
    /// How many of the connected players chose "continue without it" about `subject` (0 for a seat that is no seat)
    uint8_t votes_for_continue(uint8_t subject) const noexcept;
    /// The percent that a seat that is catching up has executed (0 for any other)
    uint8_t percent(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && seats_[seat].state == State::CatchingUp ? seats_[seat].percent : uint8_t{0}; }

    /// What every player is told: the seats that are missing (longest away first), the vote and the cap. `viewer` is the receiver's seat (its own choice goes in `your_vote`), 255 for none.
    PresenceMsg presence_for(uint8_t viewer, uint32_t now_ms) const;

    /// Counters for the room's status: seats dropped by a won vote, seats dropped by the cap, absences that ended with a seat that caught up
    uint32_t drops_by_vote() const noexcept { return drops_by_vote_; }
    uint32_t drops_by_cap() const noexcept { return drops_by_cap_; }
    uint32_t rejoins() const noexcept { return rejoins_; }

    const Config& config() const noexcept { return cfg_; }

private:
    struct Seat {
        State state{State::Empty};
        uint32_t since_ms{0};                           // Absent, CatchingUp: when the absence that goes on began (the away time is counted from here); otherwise when the seat took its state
        uint32_t away_before_ms{0};                     // the absences that are over, added up (saturating)
        uint32_t progress_ms{0};                        // CatchingUp: the time of the return or of the last progress
        uint8_t percent{0};                             // CatchingUp: the highest percent acknowledged
        std::array<uint8_t, sim::MAX_PLAYERS> vote{};   // vote[voter] about THIS seat: 0 none, 1 keep waiting, 2 continue without it
    };
    static bool away_state(State s) noexcept { return s == State::Absent || s == State::CatchingUp; }
    void clear_votes(uint8_t seat) noexcept;            // the votes about the seat and the votes it cast
    void settle_pause(bool was_paused, uint32_t now_ms) noexcept;
    void drop_seat(uint8_t seat, uint32_t now_ms);

    Config cfg_;
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
    uint32_t pause_before_ms_{0};                       // the pauses that are over, added up (saturating)
    uint32_t pause_since_ms_{0};                        // while paused: when the pause that goes on began
    uint32_t drops_by_vote_{0};
    uint32_t drops_by_cap_{0};
    uint32_t rejoins_{0};
};

}  // namespace ants::net

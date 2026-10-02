#include "ants_net/attendance.hpp"

#include <algorithm>

namespace ants::net {

namespace {

// How long ago `since_ms` was, by the wrap-safe difference of the 32-bit clock (clock.hpp); a reading that is older than the event counts as no time
uint32_t elapsed(uint32_t now_ms, uint32_t since_ms) noexcept {
    const int32_t d = static_cast<int32_t>(now_ms - since_ms);
    return d > 0 ? static_cast<uint32_t>(d) : 0u;
}

uint32_t saturating_add(uint32_t a, uint32_t b) noexcept { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }

}  // namespace

void Attendance::seat_humans(uint8_t mask, uint32_t now_ms) {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        seats_[s] = Seat{};
        if ((mask & (1u << s)) != 0) {
            seats_[s].state = State::Present;
            seats_[s].since_ms = now_ms;
            seats_[s].progress_ms = now_ms;
        }
    }
    pause_before_ms_ = 0;
    pause_since_ms_ = now_ms;
    drops_by_vote_ = 0;
    drops_by_cap_ = 0;
    rejoins_ = 0;
}

bool Attendance::paused() const noexcept {
    for (const Seat& s : seats_) {
        if (away_state(s.state)) return true;
    }
    return false;
}

void Attendance::settle_pause(bool was_paused, uint32_t now_ms) noexcept {
    const bool is_paused = paused();
    if (!was_paused && is_paused) pause_since_ms_ = now_ms;                                                           // the pause begins
    else if (was_paused && !is_paused) pause_before_ms_ = saturating_add(pause_before_ms_, elapsed(now_ms, pause_since_ms_));       // it is over: it is added to the match's total
}

void Attendance::clear_votes(uint8_t seat) noexcept {
    seats_[seat].vote.fill(0);                                  // the votes about the seat
    for (Seat& s : seats_) s.vote[seat] = 0;                    // and the votes that it cast
}

uint32_t Attendance::away_ms(uint8_t seat, uint32_t now_ms) const noexcept {
    if (seat >= sim::MAX_PLAYERS) return 0;
    const Seat& s = seats_[seat];
    return saturating_add(s.away_before_ms, away_state(s.state) ? elapsed(now_ms, s.since_ms) : 0u);
}

uint32_t Attendance::pause_ms(uint32_t now_ms) const noexcept {
    return saturating_add(pause_before_ms_, paused() ? elapsed(now_ms, pause_since_ms_) : 0u);
}

uint32_t Attendance::cap_left_ms(uint32_t now_ms) const noexcept {
    const uint32_t used = pause_ms(now_ms);
    return used >= cfg_.max_pause_ms ? 0u : cfg_.max_pause_ms - used;
}

uint16_t Attendance::cap_s(uint32_t now_ms) const noexcept {
    const uint64_t seconds = (uint64_t{cap_left_ms(now_ms)} + 999u) / 1000u;          // rounded up: 0 only when the cap is reached
    return static_cast<uint16_t>(std::min<uint64_t>(seconds, kCapSecondsMore));
}

bool Attendance::lost(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state != State::Present) return false;
    const bool was_paused = paused();
    Seat& s = seats_[seat];
    s.state = State::Absent;
    s.since_ms = now_ms;
    s.percent = 0;
    clear_votes(seat);
    settle_pause(was_paused, now_ms);
    return true;
}

bool Attendance::returning(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS) return false;
    Seat& s = seats_[seat];
    if (s.state != State::Present && s.state != State::Absent && s.state != State::CatchingUp) return false;
    const bool was_paused = paused();
    if (s.state == State::Present) s.since_ms = now_ms;          // the old link was not known to be dead: the seat is away from now; Absent and CatchingUp keep counting from the loss
    s.state = State::CatchingUp;
    s.progress_ms = now_ms;
    s.percent = 0;
    clear_votes(seat);
    settle_pause(was_paused, now_ms);
    return true;
}

bool Attendance::progress(uint8_t seat, uint8_t percent_done, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state != State::CatchingUp) return false;
    percent_done = std::min<uint8_t>(percent_done, 100);
    if (percent_done <= seats_[seat].percent) return false;       // nothing new: no sign that the seat is getting anywhere
    seats_[seat].percent = percent_done;
    seats_[seat].progress_ms = now_ms;
    return true;
}

bool Attendance::caught_up(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state != State::CatchingUp) return false;
    const bool was_paused = paused();
    Seat& s = seats_[seat];
    s.away_before_ms = saturating_add(s.away_before_ms, std::max(elapsed(now_ms, s.since_ms), cfg_.min_absence_ms));        // an absence counts at least min_absence_ms
    s.state = State::Present;
    s.since_ms = now_ms;
    s.percent = 0;
    clear_votes(seat);
    ++rejoins_;
    settle_pause(was_paused, now_ms);
    return true;
}

bool Attendance::catch_up_failed(uint8_t seat, uint32_t now_ms) {
    (void)now_ms;
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state != State::CatchingUp) return false;
    seats_[seat].state = State::Absent;                           // (since_ms is as it was: the away time kept running from the loss, and the match is still paused)
    seats_[seat].percent = 0;
    return true;
}

void Attendance::drop_seat(uint8_t seat, uint32_t now_ms) {
    const bool was_paused = paused();
    Seat& s = seats_[seat];
    if (away_state(s.state)) s.away_before_ms = saturating_add(s.away_before_ms, elapsed(now_ms, s.since_ms));      // (the absence that ends here counts as it was)
    s.state = State::Dropped;
    s.since_ms = now_ms;
    s.percent = 0;
    clear_votes(seat);
    settle_pause(was_paused, now_ms);
}

bool Attendance::dropped(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state == State::Empty || seats_[seat].state == State::Dropped) return false;
    drop_seat(seat, now_ms);
    return true;
}

uint8_t Attendance::connected_humans() const noexcept {
    uint8_t n = 0;
    for (const Seat& s : seats_) n = static_cast<uint8_t>(n + (s.state == State::Present ? 1 : 0));
    return n;
}

uint8_t Attendance::votes_for_continue(uint8_t subject) const noexcept {
    if (subject >= sim::MAX_PLAYERS) return 0;
    uint8_t n = 0;
    for (uint8_t voter = 0; voter < sim::MAX_PLAYERS; ++voter) {
        if (seats_[subject].vote[voter] == 2) ++n;               // (only a connected player has a choice stored: vote() asks for a Present seat, and every way out of Present clears the seat's votes)
    }
    return n;
}

uint8_t Attendance::vote_subject(uint32_t now_ms) const noexcept {
    uint8_t best = 255;
    uint32_t best_away = 0;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (seats_[s].state != State::Absent) continue;           // a seat that is catching up has come back: it is not put to the vote
        const uint32_t away = away_ms(s, now_ms);
        if (away >= cfg_.vote_after_ms && (best == 255 || away > best_away)) {      // (strictly longer: of a tie the lowest seat)
            best = s;
            best_away = away;
        }
    }
    return best;
}

bool Attendance::vote(uint8_t voter, uint8_t subject, bool continue_without, uint32_t now_ms) {
    if (voter >= sim::MAX_PLAYERS || subject >= sim::MAX_PLAYERS || seats_[voter].state != State::Present) return false;
    if (vote_subject(now_ms) != subject) return false;
    seats_[subject].vote[voter] = continue_without ? 2 : 1;
    return true;
}

std::vector<uint8_t> Attendance::update(uint32_t now_ms) {
    std::vector<uint8_t> drops;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {              // a catch-up that shows no progress is let go: the seat is absent again (its time kept running)
        if (seats_[s].state == State::CatchingUp && elapsed(now_ms, seats_[s].progress_ms) >= cfg_.catch_up_stall_ms) catch_up_failed(s, now_ms);
    }
    const uint8_t subject = vote_subject(now_ms);                 // a vote that is won drops its seat (the next seat's vote starts empty: one at a time)
    if (subject != 255) {
        const uint8_t connected = connected_humans();
        if (vote_won(votes_for_continue(subject), connected)) {
            ++drops_by_vote_;
            drop_seat(subject, now_ms);
            drops.push_back(subject);
        }
    }
    if (cap_reached(now_ms)) {                                    // the match has no pause left: every seat that is away and not catching up is dropped
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (seats_[s].state != State::Absent) continue;
            ++drops_by_cap_;
            drop_seat(s, now_ms);
            drops.push_back(s);
        }
    }
    return drops;
}

PresenceMsg Attendance::presence_for(uint8_t viewer, uint32_t now_ms) const {
    PresenceMsg m;
    struct Row {
        uint32_t away;
        uint8_t seat;
    };
    std::array<Row, sim::MAX_PLAYERS> rows{};
    size_t n = 0;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (away_state(seats_[s].state)) rows[n++] = Row{away_ms(s, now_ms), s};
    }
    const auto first = [](const Row& a, const Row& b) { return a.away != b.away ? a.away > b.away : a.seat < b.seat; };       // the longest away first, of a tie the lowest seat
    for (size_t i = 1; i < n; ++i) {                                                                                           // (an insertion sort: of at most four rows, and GCC 12 reads std::sort of a short array as a bounds error)
        const Row row = rows[i];
        size_t j = i;
        for (; j > 0 && first(row, rows[j - 1]); --j) rows[j] = rows[j - 1];
        rows[j] = row;
    }
    for (size_t i = 0; i < n; ++i) {
        PresenceMsg::Entry e;
        e.seat = rows[i].seat;
        e.state = seats_[e.seat].state == State::CatchingUp ? PresenceMsg::State::CatchingUp : PresenceMsg::State::Absent;
        e.waited_s = static_cast<uint16_t>(std::min<uint32_t>(rows[i].away / 1000u, 0xFFFFu));
        e.progress = seats_[e.seat].state == State::CatchingUp ? seats_[e.seat].percent : uint8_t{0};
        m.missing.push_back(e);
    }
    m.vote_seat = vote_subject(now_ms);
    m.voters = connected_humans();
    if (m.vote_seat != 255) {
        m.votes_continue = votes_for_continue(m.vote_seat);
        if (viewer < sim::MAX_PLAYERS) m.your_vote = seats_[m.vote_seat].vote[viewer];
    }
    m.cap_s = cap_s(now_ms);
    return m;
}

}  // namespace ants::net

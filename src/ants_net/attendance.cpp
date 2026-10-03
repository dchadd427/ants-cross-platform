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

Attendance::Config Attendance::clamped(Config c) noexcept {
    c.rejoin_attempts = std::clamp(c.rejoin_attempts, 1u, kMaxRejoinAttempts);
    c.flap_losses = std::clamp(c.flap_losses, 2u, kMaxFlapLosses);
    return c;
}

void Attendance::seat_humans(uint8_t mask, uint32_t now_ms) {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        seats_[s] = Seat{};
        if ((mask & (1u << s)) != 0) {
            seats_[s].state = State::Present;
            seats_[s].since_ms = now_ms;
            seats_[s].progress_ms = now_ms;
            seats_[s].catch_start_ms = now_ms;
        }
    }
    pause_before_ms_ = 0;
    pause_real_before_ms_ = 0;
    pause_since_ms_ = now_ms;
    owe_countdown_ = false;
    resuming_ = false;
    resume_since_ms_ = now_ms;
    resume_before_ms_ = 0;
    drops_by_vote_ = 0;
    drops_by_cap_ = 0;
    rejoins_ = 0;
    refused_ = 0;
    expired_ = 0;
    streamed_total_ = 0;
}

void Attendance::seat_restored(uint8_t humans, uint8_t dropped, uint32_t now_ms) {
    seat_humans(humans, now_ms);
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if ((humans & (1u << s)) == 0) continue;
        Seat& seat = seats_[s];
        seat.since_ms = now_ms;
        if ((dropped & (1u << s)) != 0) {
            seat.state = State::Dropped;                              // (it left or was dropped before the restart: final, and no drop of this run: the counters stay 0)
        } else {
            seat.state = State::Absent;                               // its player has to find the server again: held, the key valid, the match paused
            seat.excused = true;                                      // nobody's fault: a longer wait before the vote, no loss for the flapping rule, nothing added to the away time
        }
    }
    // (the pause begins now when a seat is away: seat_humans set its start, and paused() is what the seats say)
}

bool Attendance::paused() const noexcept {
    for (const Seat& s : seats_) {
        if (away_state(s.state)) return true;
    }
    return false;
}

bool Attendance::counting_down(uint32_t now_ms) const noexcept {
    return resuming_ && cfg_.resume_countdown_ms > 0 && elapsed(now_ms, resume_since_ms_) < cfg_.resume_countdown_ms;
}

uint8_t Attendance::resume_s(uint32_t now_ms) const noexcept {
    if (!counting_down(now_ms)) return 0;
    const uint32_t left = cfg_.resume_countdown_ms - elapsed(now_ms, resume_since_ms_);
    return static_cast<uint8_t>(std::min<uint32_t>((left + 999u) / 1000u, kMaxResumeSeconds));
}

// A countdown that is over, or is cut short, is added to the time that the match was held
void Attendance::fold_countdown(uint32_t now_ms) noexcept {
    if (!resuming_) return;
    resume_before_ms_ = saturating_add(resume_before_ms_, std::min(elapsed(now_ms, resume_since_ms_), cfg_.resume_countdown_ms));
    resuming_ = false;
}

void Attendance::settle_pause(bool was_paused, uint32_t now_ms) noexcept {
    const bool is_paused = paused();
    if (!was_paused && is_paused) {                                                   // the pause begins (a countdown that is running is cut short: one follows this pause, however short)
        owe_countdown_ = counting_down(now_ms);
        fold_countdown(now_ms);
        pause_since_ms_ = now_ms;
    } else if (was_paused && !is_paused) {                                            // it is over: it is added to the match's total (as the cap counts it, and as it was)
        const uint32_t real = elapsed(now_ms, pause_since_ms_);
        pause_before_ms_ = saturating_add(pause_before_ms_, std::max(real, cfg_.min_absence_ms));
        pause_real_before_ms_ = saturating_add(pause_real_before_ms_, real);
        if (cfg_.resume_countdown_ms > 0 && (owe_countdown_ || real >= cfg_.resume_min_pause_ms)) {      // a pause of a few seconds is a blip: the match goes on at once
            fold_countdown(now_ms);
            resuming_ = true;
            resume_since_ms_ = now_ms;
        }
        owe_countdown_ = false;
    }
}

void Attendance::clear_votes(uint8_t seat, bool keep_about) noexcept {
    if (!keep_about) seats_[seat].vote.fill(0);                 // the votes about the seat
    for (Seat& s : seats_) s.vote[seat] = 0;                    // and the votes that it cast
}

uint32_t Attendance::away_ms(uint8_t seat, uint32_t now_ms) const noexcept {
    if (seat >= sim::MAX_PLAYERS) return 0;
    const Seat& s = seats_[seat];
    return saturating_add(s.away_before_ms, away_state(s.state) ? elapsed(now_ms, s.since_ms) : 0u);
}

uint32_t Attendance::pause_ms(uint32_t now_ms) const noexcept {
    return saturating_add(pause_before_ms_, paused() ? std::max(elapsed(now_ms, pause_since_ms_), cfg_.min_absence_ms) : 0u);
}

uint32_t Attendance::held_ms(uint32_t now_ms) const noexcept {
    uint32_t held = saturating_add(pause_real_before_ms_, paused() ? elapsed(now_ms, pause_since_ms_) : 0u);
    held = saturating_add(held, resume_before_ms_);
    if (resuming_) held = saturating_add(held, std::min(elapsed(now_ms, resume_since_ms_), cfg_.resume_countdown_ms));
    return held;
}

uint32_t Attendance::cap_left_ms(uint32_t now_ms) const noexcept {
    const uint32_t used = pause_ms(now_ms);
    return used >= cfg_.max_pause_ms ? 0u : cfg_.max_pause_ms - used;
}

uint16_t Attendance::cap_s(uint32_t now_ms) const noexcept {
    const uint64_t seconds = (uint64_t{cap_left_ms(now_ms)} + 999u) / 1000u;          // rounded up: 0 only when the cap is reached
    return static_cast<uint16_t>(std::min<uint64_t>(seconds, kCapSecondsMore));
}

uint32_t Attendance::catching_ms(const Seat& s, uint32_t now_ms) const noexcept {
    return saturating_add(s.catching_before_ms, s.state == State::CatchingUp ? elapsed(now_ms, s.catch_start_ms) : 0u);
}

uint32_t Attendance::catching_up_ms(uint8_t seat, uint32_t now_ms) const noexcept { return seat < sim::MAX_PLAYERS ? catching_ms(seats_[seat], now_ms) : 0u; }

// The seat was lost flap_losses times within flap_window_ms (that is what `flap` says) and has not stopped for that long
bool Attendance::flap_active(const Seat& s, uint32_t now_ms) const noexcept { return s.flap && s.losses_n > 0 && elapsed(now_ms, s.losses[0]) < cfg_.flap_window_ms; }

bool Attendance::flapping(uint8_t seat, uint32_t now_ms) const noexcept { return seat < sim::MAX_PLAYERS && flap_active(seats_[seat], now_ms); }

// A loss of the seat's connection (or a second window that took it over): counted for the flapping rule
void Attendance::note_loss(Seat& s, uint32_t now_ms) noexcept {
    for (size_t i = kMaxFlapLosses - 1; i > 0; --i) s.losses[i] = s.losses[i - 1];
    s.losses[0] = now_ms;
    s.losses_n = static_cast<uint8_t>(std::min<size_t>(s.losses_n + 1u, kMaxFlapLosses));
    s.flap = s.losses_n >= cfg_.flap_losses && elapsed(s.losses[0], s.losses[cfg_.flap_losses - 1]) <= cfg_.flap_window_ms;
}

bool Attendance::lost(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state != State::Present) return false;
    const bool was_paused = paused();
    Seat& s = seats_[seat];
    s.state = State::Absent;
    s.since_ms = now_ms;
    s.percent = 0;
    s.catching_before_ms = 0;                                    // a new absence has all its catch-up time again
    s.excused = false;                                           // (a lost link is the seat's own: the restart's grace is over for good)
    note_loss(s, now_ms);
    clear_votes(seat, s.flap);                                   // (the votes about a seat that flaps stay: the vote is open across its returns)
    settle_pause(was_paused, now_ms);
    return true;
}

Attendance::Rejoin Attendance::rejoin_check(uint8_t seat, uint32_t now_ms, size_t planned_bytes, size_t log_bytes) const noexcept {
    if (seat >= sim::MAX_PLAYERS) return Rejoin::NotHeld;
    const Seat& s = seats_[seat];
    if (s.state != State::Present && s.state != State::Absent && s.state != State::CatchingUp) return Rejoin::NotHeld;
    if (catching_ms(s, now_ms) >= cfg_.max_catch_up_ms) return Rejoin::CatchUpSpent;
    uint32_t attempts = 0;
    for (const uint32_t t : s.attempts) attempts += elapsed(now_ms, t) < cfg_.rejoin_window_ms ? 1u : 0u;
    if (attempts >= cfg_.rejoin_attempts) return Rejoin::TooManyAttempts;
    if (planned_bytes > 0 && log_bytes > 0) {
        uint64_t streamed = 0;
        for (const Charge& c : s.charges) streamed += elapsed(now_ms, c.at_ms) < cfg_.stream_window_ms ? c.bytes : 0u;
        const uint64_t allowed = std::max<uint64_t>(uint64_t{cfg_.stream_factor} * log_bytes, cfg_.stream_floor_bytes);
        if (streamed + planned_bytes > allowed) return Rejoin::TooMuchStreamed;
    }
    return Rejoin::Allowed;
}

bool Attendance::returning(uint8_t seat, uint32_t now_ms, size_t planned_bytes, size_t log_bytes) {
    const Rejoin verdict = rejoin_check(seat, now_ms, planned_bytes, log_bytes);
    if (verdict != Rejoin::Allowed) {
        if (verdict != Rejoin::NotHeld) ++refused_;
        return false;
    }
    const bool was_paused = paused();
    Seat& s = seats_[seat];
    s.attempts.erase(std::remove_if(s.attempts.begin(), s.attempts.end(), [&](uint32_t t) { return elapsed(now_ms, t) >= cfg_.rejoin_window_ms; }), s.attempts.end());
    s.attempts.push_back(now_ms);
    if (s.state != State::CatchingUp) {                          // a Hello of an attempt that is in progress changes nothing of it: its stall clock, its percent and its start go on
        if (s.state == State::Present) {                         // the old link was not known to be dead: the seat is away from now (Absent and CatchingUp keep counting from the loss), which is a loss for the flapping rule
            s.since_ms = now_ms;
            s.catching_before_ms = 0;
            s.excused = false;
            note_loss(s, now_ms);
        }
        s.state = State::CatchingUp;
        s.progress_ms = now_ms;
        s.catch_start_ms = now_ms;
        s.percent = 0;
    }
    clear_votes(seat, flap_active(s, now_ms));
    settle_pause(was_paused, now_ms);
    return true;
}

void Attendance::charge_stream(uint8_t seat, uint32_t now_ms, size_t bytes) {
    if (seat >= sim::MAX_PLAYERS || bytes == 0) return;
    Seat& s = seats_[seat];
    s.charges.erase(std::remove_if(s.charges.begin(), s.charges.end(), [&](const Charge& c) { return elapsed(now_ms, c.at_ms) >= cfg_.stream_window_ms; }), s.charges.end());
    const uint32_t group_ms = std::max<uint32_t>(1u, cfg_.stream_window_ms / 10u);        // (the bytes are kept in ten groups: a few entries however much is streamed)
    if (!s.charges.empty() && elapsed(now_ms, s.charges.back().at_ms) < group_ms) {
        s.charges.back().bytes += bytes;
    } else {
        s.charges.push_back(Charge{now_ms, bytes});
    }
    streamed_total_ += bytes;
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
    if (!s.excused) s.away_before_ms = saturating_add(s.away_before_ms, std::max(elapsed(now_ms, s.since_ms), cfg_.min_absence_ms));        // an absence counts at least min_absence_ms (a restart's counts nothing)
    s.excused = false;
    s.state = State::Present;
    s.since_ms = now_ms;
    s.percent = 0;
    s.catching_before_ms = 0;                                    // the absence is over
    clear_votes(seat, flap_active(s, now_ms));
    ++rejoins_;
    settle_pause(was_paused, now_ms);
    return true;
}

bool Attendance::catch_up_failed(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state != State::CatchingUp) return false;
    Seat& s = seats_[seat];
    s.catching_before_ms = catching_ms(s, now_ms);                // the attempt's time is spent from the absence's catch-up time
    s.state = State::Absent;                                      // (since_ms is as it was: the away time kept running from the loss, and the match is still paused)
    s.percent = 0;
    return true;
}

void Attendance::drop_seat(uint8_t seat, uint32_t now_ms) {
    const bool was_paused = paused();
    Seat& s = seats_[seat];
    if (away_state(s.state) && !s.excused) s.away_before_ms = saturating_add(s.away_before_ms, elapsed(now_ms, s.since_ms));      // (the absence that ends here counts as it was)
    s.excused = false;
    s.state = State::Dropped;
    s.since_ms = now_ms;
    s.percent = 0;
    s.flap = false;
    clear_votes(seat, false);
    settle_pause(was_paused, now_ms);
}

bool Attendance::dropped(uint8_t seat, uint32_t now_ms) {
    if (seat >= sim::MAX_PLAYERS || seats_[seat].state == State::Empty || seats_[seat].state == State::Dropped) return false;
    drop_seat(seat, now_ms);
    return true;
}

uint8_t Attendance::connected_humans(uint8_t except) const noexcept {
    uint8_t n = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) n = static_cast<uint8_t>(n + (seats_[seat].state == State::Present && seat != except ? 1 : 0));
    return n;
}

uint8_t Attendance::votes_for_continue(uint8_t subject) const noexcept {
    if (subject >= sim::MAX_PLAYERS) return 0;
    uint8_t n = 0;
    for (uint8_t voter = 0; voter < sim::MAX_PLAYERS; ++voter) {
        if (voter != subject && seats_[subject].vote[voter] == 2) ++n;               // (only a connected player has a choice stored: vote() asks for a Present seat, and every way out of Present clears the seat's votes)
    }
    return n;
}

// An Absent seat is put to the vote when it has been away long enough: its total away time reaches vote_after_ms, or, when its absence is a restart's (seat_restored), the absence itself has
// lasted restart_vote_after_ms (the total of the seat's earlier absences is not what a restart is judged by)
bool Attendance::vote_time_reached(const Seat& s, uint8_t seat, uint32_t now_ms) const noexcept {
    if (s.excused) return elapsed(now_ms, s.since_ms) >= cfg_.restart_vote_after_ms;
    return away_ms(seat, now_ms) >= cfg_.vote_after_ms;
}

uint8_t Attendance::vote_subject(uint32_t now_ms) const noexcept {
    uint8_t best = 255;
    uint32_t best_away = 0;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        const Seat& seat = seats_[s];
        const uint32_t away = away_ms(s, now_ms);
        bool candidate = false;
        if (seat.state == State::Absent) candidate = vote_time_reached(seat, s, now_ms) || flap_active(seat, now_ms);     // away long enough, or a seat that flaps
        else if (seat.state == State::CatchingUp || seat.state == State::Present) candidate = flap_active(seat, now_ms);    // a seat that is catching up has come back: it is put to the vote only when it flaps
        if (candidate && (best == 255 || away > best_away)) {      // (strictly longer: of a tie the lowest seat)
            best = s;
            best_away = away;
        }
    }
    return best;
}

bool Attendance::vote(uint8_t voter, uint8_t subject, bool continue_without, uint32_t now_ms) {
    if (voter >= sim::MAX_PLAYERS || subject >= sim::MAX_PLAYERS || voter == subject || seats_[voter].state != State::Present) return false;
    if (vote_subject(now_ms) != subject) return false;
    seats_[subject].vote[voter] = continue_without ? 2 : 1;
    return true;
}

std::vector<uint8_t> Attendance::update(uint32_t now_ms) {
    std::vector<uint8_t> drops;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {              // a catch-up that shows no progress, or has used up the absence's catch-up time, is let go: the seat is absent again (its time kept running)
        Seat& seat = seats_[s];
        if (seat.state != State::CatchingUp) continue;
        const bool spent = catching_ms(seat, now_ms) >= cfg_.max_catch_up_ms;
        if (spent || elapsed(now_ms, seat.progress_ms) >= cfg_.catch_up_stall_ms) {
            if (spent) ++expired_;
            catch_up_failed(s, now_ms);
        }
    }
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {              // a seat that has stopped flapping: the vote about it is closed, whatever was said in it
        Seat& seat = seats_[s];
        if (seat.flap && !flap_active(seat, now_ms)) {
            seat.flap = false;
            if (!(seat.state == State::Absent && vote_time_reached(seat, s, now_ms))) seat.vote.fill(0);      // (a seat that has been away long enough stays the subject of its vote)
        }
    }
    const uint8_t subject = vote_subject(now_ms);                 // a vote that is won drops its seat (the next seat's vote starts empty: one at a time)
    if (subject != 255 && vote_won(votes_for_continue(subject), connected_humans(subject))) {
        ++drops_by_vote_;
        drop_seat(subject, now_ms);
        drops.push_back(subject);
    }
    if (cap_reached(now_ms)) {                                    // the match has no pause left: every seat that is not present is dropped, whatever progress a catch-up shows
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (!away_state(seats_[s].state)) continue;
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
    m.voters = connected_humans(m.vote_seat);
    if (m.vote_seat != 255) {
        m.votes_continue = votes_for_continue(m.vote_seat);
        if (viewer < sim::MAX_PLAYERS) m.your_vote = seats_[m.vote_seat].vote[viewer];
    }
    m.cap_s = cap_s(now_ms);
    m.resume_s = resume_s(now_ms);
    return m;
}

}  // namespace ants::net

#include "ants_server/live_board.hpp"

#include <algorithm>
#include <chrono>

#include "ants_net/protocol.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_server/replay_store.hpp"

namespace ants::server {

static_assert(LiveScores::kSeats == sim::MAX_PLAYERS, "the board's score boxes are one for each seat of the game");

namespace {

int64_t system_seconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

bool id_char(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

bool digits_at(const std::string& text, size_t from, size_t count) {
    if (text.size() < from + count) return false;
    for (size_t i = from; i < from + count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

}  // namespace

LiveBoard::LiveBoard(std::function<int64_t()> clock_s) : clock_s_(clock_s ? std::move(clock_s) : std::function<int64_t()>(system_seconds)) {}

void LiveBoard::set_clock(std::function<int64_t()> clock_s) { clock_s_ = clock_s ? std::move(clock_s) : std::function<int64_t()>(system_seconds); }

void LiveBoard::set_delay_seconds(uint32_t seconds) noexcept { delay_s_ = std::min(seconds, kMaxDelayS); }

bool LiveBoard::valid_id(const std::string& id) {
    // <stem 1 to 24>-<8 digits>-<6 digits>Z[-<1 to 4 digits>]
    size_t at = 0;
    while (at < id.size() && id_char(id[at])) ++at;
    if (at < 1 || at > 24 || id.size() < at + 1 + 8 + 1 + 6 + 1) return false;
    if (id[at] != '-' || !digits_at(id, at + 1, 8) || id[at + 9] != '-' || !digits_at(id, at + 10, 6) || id[at + 16] != 'Z') return false;
    at += 17;
    if (at == id.size()) return true;
    const size_t count = id.size() - at - 1;
    return id[at] == '-' && count >= 1 && count <= 4 && digits_at(id, at + 1, count);
}

void LiveBoard::forget_old() {
    const int64_t now = now_s();
    for (Ended& e : ended_) {
        if (e.ended_s > now) e.ended_s = now;                                // (a clock that was set back must not keep a match for longer than the memory is)
    }
    ended_.erase(std::remove_if(ended_.begin(), ended_.end(), [now](const Ended& e) { return now - e.ended_s >= memory_s(e); }), ended_.end());
    // each kind has its own count, the oldest of it forgotten first: a stream of matches that ended at once forgets none of the matches that were kept
    for (const bool brief : {false, true}) {
        const size_t limit = brief ? kMaxRememberedBrief : kMaxRemembered;
        size_t count = static_cast<size_t>(std::count_if(ended_.begin(), ended_.end(), [brief](const Ended& e) { return e.brief == brief; }));
        for (auto it = ended_.begin(); it != ended_.end() && count > limit;) {
            if (it->brief == brief) {
                it = ended_.erase(it);
                --count;
            } else {
                ++it;
            }
        }
    }
}

std::string LiveBoard::begin(const replay::Recorder& recorder, unsigned people) {
    forget_old();
    const replay::Header& head = recorder.replay().head;
    if (head.fog) return std::string();                          // (Fog of War hides the other sides from a player: a snapshot that anybody can read while the match runs would show them. The match is public when it is over, like any other.)
    const int64_t now = now_s();
    const std::string base = ReplayStore::map_stem(head.map_name) + "-" + ReplayStore::time_stamp(now);
    // The ids of this map and second that the matches on the board and the ones remembered hold: the match takes the first free one ("", then -2, -3 ...)
    std::vector<bool> used(kMaxSameSecond + 1, false);
    const auto note = [&](const std::string& other) {
        if (other.compare(0, base.size(), base) != 0) return;
        if (other.size() == base.size()) {
            used[1] = true;
        } else if (other[base.size()] == '-' && other.size() - base.size() - 1 <= 4) {
            unsigned n = 0;
            for (size_t i = base.size() + 1; i < other.size(); ++i) n = n * 10 + static_cast<unsigned>(other[i] - '0');
            if (n >= 2 && n <= kMaxSameSecond) used[n] = true;
        }
    };
    for (const Running& r : running_) note(r.id);
    for (const Ended& e : ended_) {
        if (now - e.ended_s < memory_s(e)) note(e.id);
    }
    unsigned sequence = 1;
    while (sequence <= kMaxSameSecond && used[sequence]) ++sequence;
    if (sequence > kMaxSameSecond) return std::string();
    const std::string id = sequence == 1 ? base : base + "-" + std::to_string(sequence);
    Running match;
    match.id = id;
    match.map = head.map_name;
    match.mode = head.mode;
    match.roster = head.roster;
    match.started = now;
    match.delay_turns = people >= kDelayMinPeople ? delay_s_ * static_cast<uint32_t>(net::kTurnsPerSecond) : 0u;       // (fixed here, from the seats at the start: nothing that happens in the match changes it)
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((head.roster & (1u << seat)) != 0) match.players.push_back(replay::seat_label(head, seat));
    }
    match.recorder = &recorder;
    running_.push_back(std::move(match));
    return id;
}

void LiveBoard::set_scores(const std::string& id, const LiveScores& scores) {
    const auto it = std::find_if(running_.begin(), running_.end(), [&id](const Running& r) { return r.id == id; });
    if (it == running_.end() || !listed(*it)) return;
    LiveScores kept;
    kept.known = scores.known;
    for (uint8_t seat = 0; seat < LiveScores::kSeats; ++seat) {
        kept.side[seat] = seat;                                              // (a seat that is not in the match, or says nothing sensible of its side, is a side of its own)
        if ((it->roster & (1u << seat)) == 0) continue;                      // (only the seats of the match have a number)
        kept.box[seat] = std::max(0, std::min(scores.box[seat], LiveScores::kBoxMax));
        const uint8_t side = scores.side[seat];
        if (side < LiveScores::kSeats && (it->roster & (1u << side)) != 0) kept.side[seat] = side;
        if ((scores.covered & (1u << seat)) != 0) kept.covered = static_cast<uint8_t>(kept.covered | (1u << seat));
    }
    it->scores = kept;
}

void LiveBoard::end(const std::string& id, const std::string& kept_file) {
    forget_old();
    const auto it = std::find_if(running_.begin(), running_.end(), [&id](const Running& r) { return r.id == id; });
    if (it == running_.end()) return;
    Ended ended;
    ended.id = id;
    ended.file = kept_file;
    ended.ended_s = now_s();
    ended.turns = it->recorder != nullptr ? it->recorder->turns() : 0u;
    ended.brief = ended.turns < kReplayMinTurns;                 // (never kept, whatever became of it: a room that was closed in its countdown, or soon after)
    running_.erase(it);
    ended_.push_back(std::move(ended));
    forget_old();
}

bool LiveBoard::listed(const Running& match) const {
    return match.recorder != nullptr && match.recorder->failure().empty() && !match.recorder->finished();
}

std::vector<LiveMatch> LiveBoard::list() const {
    // the matches that have run kReplayMinTurns take the places first, newest first; the younger ones fill what is left (a stream of matches that end at once cannot push the others off the list); then all newest first
    std::vector<const Running*> chosen;
    for (const bool old : {true, false}) {
        for (auto it = running_.rbegin(); it != running_.rend() && chosen.size() < kMaxListed; ++it) {        // (newest start first)
            if (!listed(*it) || (it->recorder->turns() >= kReplayMinTurns) != old) continue;
            chosen.push_back(&*it);
        }
    }
    std::vector<LiveMatch> out;
    for (auto it = running_.rbegin(); it != running_.rend(); ++it) {
        if (std::find(chosen.begin(), chosen.end(), &*it) == chosen.end()) continue;
        LiveMatch m;
        m.id = it->id;
        m.map = it->map;
        m.mode = it->mode;
        m.roster = it->roster;
        m.scores = it->scores;
        m.started = it->started;
        m.turns = it->recorder->turns();
        m.players = it->players;
        out.push_back(std::move(m));
    }
    return out;
}

LiveSnapshot LiveBoard::snapshot(const std::string& id) const {
    LiveSnapshot out;
    const auto it = std::find_if(running_.begin(), running_.end(), [&id](const Running& r) { return r.id == id; });
    if (it == running_.end() || !listed(*it)) return out;
    const int64_t now = now_s();
    if (it->cache.empty() || now < it->cache_s || now - it->cache_s >= kSnapshotMaxAgeS) {
        // what is handed out is the match as it was `delay_turns` ago: the recorder cuts the file there, and the cut is the only thing that decides what can be read (never the page, never a query)
        const uint32_t run = it->recorder->turns();
        const uint32_t shown = run > it->delay_turns ? run - it->delay_turns : 0u;
        std::string error;
        it->cache = it->recorder->snapshot(shown, error);
        it->cache_s = now;
        it->cache_now = run;
        it->cache_shown = shown;
    }
    if (it->cache.empty()) return out;
    out.bytes = &it->cache;
    out.now = it->cache_now;
    out.shown = it->cache_shown;
    out.delay = it->delay_turns;
    return out;
}

bool LiveBoard::outcome(const std::string& id, LiveOutcome& out) const {
    const int64_t now = now_s();
    for (const Ended& e : ended_) {
        if (e.id == id && now - e.ended_s < memory_s(e)) {
            out.file = e.file;
            out.turns = e.turns;
            return true;
        }
    }
    return false;
}

size_t LiveBoard::remembered() const {
    const int64_t now = now_s();
    return static_cast<size_t>(std::count_if(ended_.begin(), ended_.end(), [now](const Ended& e) { return now - e.ended_s < memory_s(e); }));
}

}  // namespace ants::server

#include "ants_net/sequencer.hpp"

#include <algorithm>
#include <iterator>

namespace ants::net {

namespace {
constexpr uint32_t kRefereeWindowTurns = turns_for_ms(6400);       // the referee's report of a turn is taken while the turn is at most 6.4 s old
constexpr bool hashed_turn(uint32_t turn) noexcept { return (uint64_t{turn} + 1) % kHashEveryTurns == 0; }       // the turns whose state hash is reported (LockstepRunner)
constexpr uint8_t bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }
}  // namespace

namespace {
constexpr uint32_t kMilli = 1000;                                  // the budget counts in thousandths of an order, so that its earnings (a few orders a second, one turn at a time) have no rounding
}  // namespace

Sequencer::Sequencer(Config config) : cfg_(config) {
    budget_milli_.fill(cfg_.order_burst * kMilli);
    control_milli_.fill(cfg_.control_burst * kMilli);
}

void Sequencer::set_active(uint8_t player, bool active) {
    if (player >= sim::MAX_PLAYERS) return;
    active_[player] = active;
    if (!active) {
        carry_[player].clear();                        // a player that is gone has no commands waiting for a later turn
        drop_ahead(player);
        if (host_player_ >= sim::MAX_PLAYERS) {        // (a seat that leaves the table of a dedicated server takes its reports with it: they were made in a stay that is over)
            for (auto& entry : have_report_) entry.second[player] = false;
        }
    }
    // a player that joins is level with the sequencer: it has "executed" everything before the current turn (and was not at the table for any of them)
    if (active) {
        acked_[player] = next_turn_;
        joined_[player] = next_turn_;
    }
}

void Sequencer::drop_ahead(uint8_t player) {
    for (auto it = ahead_.begin(); it != ahead_.end();) {
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(), [player](const sim::Command& c) { return c.issuer == player; }), list.end());
        it = list.empty() ? ahead_.erase(it) : std::next(it);
    }
    ahead_count_[player] = 0;
    last_target_[player] = 0;
}

Submit Sequencer::submit_checked(uint8_t player, sim::Command command, uint32_t at_turn) {
    if (player >= sim::MAX_PLAYERS || !active_[player]) return Submit::Refused;
    if (!sim::is_client_command(command.type)) return Submit::Refused;     // Drop and unknown types are not for clients
    command.issuer = player;                       // the connection decides who speaks, not the payload
    if (vetter_ && !vetter_(player, command)) {    // nothing in it that the seat can order: the engine would do nothing with it, and it would only take room in the turn, the log and the recording
        ++no_ants_[player];
        return Submit::NoAnts;
    }
    // What a command takes its charge from: the commands that carry ants (the ones that can be padded) from the seat's orders, the others from its control commands, and a seat's first Quit from nothing
    // (a seat that is over a budget can always leave); a store that is switched off (0 in either number) charges nothing
    const bool first_quit = command.type == sim::CommandType::Quit && !quit_sent_[player];
    const bool listed = sim::has_ant_list(command.type);
    uint32_t* store = nullptr;
    if (!first_quit) {
        if (listed && cfg_.order_burst != 0 && cfg_.orders_per_second != 0) store = &budget_milli_[player];
        else if (!listed && cfg_.control_burst != 0 && cfg_.controls_per_second != 0) store = &control_milli_[player];
    }
    const auto over = [&]() {
        ++over_budget_[player];
        heavy_ = static_cast<uint8_t>(heavy_ | (1u << player));
        return Submit::OverBudget;
    };
    if (store != nullptr && *store < kMilli) return over();
    // The turn it goes into (protocol 30): the one that the sender named when that is still open, else the first open one; never before a command that the seat sent earlier; at most max_order_ahead_turns beyond the first open
    // turn. Beyond the next turn it waits in ahead_ and joins the queue when that turn is next.
    uint32_t target = std::max(std::max(at_turn, next_turn_), last_target_[player]);
    if (target > next_turn_ + cfg_.max_order_ahead_turns) {
        target = next_turn_ + cfg_.max_order_ahead_turns;
        ++ahead_clamped_[player];
    }
    last_target_[player] = target;
    if (target > next_turn_) {
        if (carry_[player].size() + ahead_count_[player] >= cfg_.max_carried_commands && !first_quit) return store != nullptr ? over() : Submit::Refused;
        ahead_[target].push_back(std::move(command));
        ++ahead_count_[player];
        if (store != nullptr) *store -= kMilli;
        if (first_quit) quit_sent_[player] = true;
        return Submit::Queued;
    }
    // A command that does not fit into the turn waits for the next ones, behind the commands of the same player that wait already (what was sent first is run first: a player
    // that has commands waiting has a full turn, so a new one never finds room in front of them). A burst that a stuck uplink delivers at last is carried into the turns that
    // follow; a peer with more waiting than the bound is a flood (a seat that has a budget is not refused for it: the store of its orders is bigger than what can wait, and the rest is dropped).
    if (queued_by_[player] < cfg_.max_commands_per_turn) {
        ++queued_by_[player];
        queue_.push_back(std::move(command));
    } else {
        if (carry_[player].size() + ahead_count_[player] >= cfg_.max_carried_commands && !first_quit) return store != nullptr ? over() : Submit::Refused;       // (the first Quit waits behind the others, however many they are)
        carry_[player].push_back(std::move(command));
    }
    if (store != nullptr) *store -= kMilli;
    if (first_quit) quit_sent_[player] = true;
    return Submit::Queued;
}

void Sequencer::submit_system(sim::Command command) {
    if (command.type == sim::CommandType::None || command.type > sim::CommandType::Last || command.issuer >= sim::MAX_PLAYERS) return;
    queue_.push_back(std::move(command));
}

void Sequencer::resume(uint32_t next_turn) {
    next_turn_ = next_turn;
    queue_.clear();
    queued_by_.fill(0);
    for (auto& c : carry_) c.clear();
    ahead_.clear();
    ahead_count_.fill(0);
    last_target_.fill(0);
    reports_.clear();
    have_report_.clear();
    referee_.clear();
    passed_ = 0;
    acked_.fill(0);
    joined_.fill(0);
    budget_milli_.fill(cfg_.order_burst * kMilli);
    control_milli_.fill(cfg_.control_burst * kMilli);
    quit_sent_.fill(false);
    active_.fill(false);                             // the new host activates the peers that follow it, one by one
}

void Sequencer::set_acked(uint8_t player, uint32_t next_to_execute) {
    if (player < sim::MAX_PLAYERS) acked_[player] = std::min(next_to_execute, next_turn_);
}

void Sequencer::on_ack(uint8_t player, uint32_t turn) {
    if (player >= sim::MAX_PLAYERS) return;
    // "executed turn t" means that the next turn the peer needs is t + 1; acks only move forward and never past what was sealed
    const uint32_t next_needed = std::min(turn + 1, next_turn_);
    if (next_needed > acked_[player]) acked_[player] = next_needed;
    // reports of turns everybody has passed are no longer needed
    uint32_t low = next_turn_;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (active_[p]) low = std::min(low, acked_[p]);
    }
    passed_ = std::max(passed_, low);
    // (a turn on which a report differs from the referee's is kept until judge_hashes has answered it: the acknowledgements that pass it may come in the same pass as the report)
    for (auto it = reports_.begin(); it != reports_.end();) {
        if (it->first + 1 < low && suspect_.count(it->first) == 0) {
            have_report_.erase(it->first);
            it = reports_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = referee_.begin(); it != referee_.end();) {            // the referee's reports of turns that everybody has passed
        if (it->first + 1 < low && suspect_.count(it->first) == 0) it = referee_.erase(it);
        else ++it;
    }
}

void Sequencer::on_referee_hash(uint32_t turn, const sim::StateHash& hash) {
    if (host_player_ < sim::MAX_PLAYERS) return;                          // a host with a seat reports through on_hash like everybody else
    if (turn >= next_turn_ || !hashed_turn(turn)) return;                 // the referee only executed turns that were sealed, and reports the hashed ones
    if (turn + kRefereeWindowTurns < next_turn_) return;                  // (a turn that is long past: nothing is waiting for it)
    if (uint64_t{turn} + 1 < passed_) return;                             // every seat has passed the turn: what the seats said of it is forgotten, so this word has nothing to be weighed against and must not wait for a report that comes later
    referee_[turn] = hash;
    const auto found = reports_.find(turn);
    const auto have = have_report_.find(turn);
    if (found == reports_.end() || have == have_report_.end()) return;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (have->second[p] && found->second[p] != hash) suspect_.insert(turn);
    }
}

std::vector<DesyncMsg> Sequencer::on_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash) {
    std::vector<DesyncMsg> out;
    if (player >= sim::MAX_PLAYERS || !active_[player]) return out;
    if (turn >= next_turn_) return out;                                   // only a turn that was sealed can be reported: a hostile client cannot make the maps grow with turns of the future
    if (host_player_ >= sim::MAX_PLAYERS) {                               // a host without a seat: the referee's report is the reference, and judge_hashes() weighs the seats' against it
        if (!hashed_turn(turn)) return out;                               // (nobody hashes any other turn: a report of one is not kept)
        if (uint64_t{turn} + 1 < passed_) return out;                     // (every seat has passed the turn: what the others said of it is forgotten, so this report would stand alone)
        bool& have = have_report_[turn][player];
        if (have) return out;                                             // the first report of a seat for a turn is its report: a repeat says nothing new
        reports_[turn][player] = hash;
        have = true;
        const auto ref = referee_.find(turn);
        if (ref != referee_.end() && ref->second != hash) suspect_.insert(turn);
        return out;
    }
    auto& reports = reports_[turn];
    auto& have = have_report_[turn];
    reports[player] = hash;
    have[player] = true;
    if (!have[host_player_]) return out;             // compared as soon as the host's own report of that turn is in
    const sim::StateHash& host = reports[host_player_];
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (p == host_player_ || !have[p]) continue;
        if (reports[p] != host) {
            DesyncMsg d;
            d.turn = turn;
            d.player = p;
            d.host = host;
            d.peer = reports[p];
            out.push_back(d);
        }
    }
    return out;
}

HashVerdict Sequencer::judge_hashes(uint8_t seated, uint8_t held) {
    HashVerdict verdict;
    for (auto it = suspect_.begin(); it != suspect_.end();) {
        const uint32_t turn = *it;
        const auto ref = referee_.find(turn);
        const auto found = reports_.find(turn);
        const auto have = have_report_.find(turn);
        if (ref == referee_.end() || found == reports_.end() || have == have_report_.end()) {       // (cannot be: they are kept as long as the turn is a suspect)
            it = suspect_.erase(it);
            continue;
        }
        uint8_t agree = 0;                           // the seats at the table that report the referee's state, report another, or have not executed the turn yet
        uint8_t differ = 0;
        uint8_t waiting = 0;
        uint8_t later = 0;                           // the seats that came to the table after the turn was sealed: they were not there to run it
        for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
            if ((seated & bit(p)) == 0 || !active_[p]) continue;
            if (have->second[p]) {
                uint8_t& side = found->second[p] == ref->second ? agree : differ;
                side = static_cast<uint8_t>(side | bit(p));
            } else if (acked_[p] <= turn) {          // (a client sends its hash of a turn before its acknowledgement of it: one that has executed the turn and said nothing never will)
                waiting = static_cast<uint8_t>(waiting | bit(p));
            } else if (joined_[p] > turn) {
                later = static_cast<uint8_t>(later | bit(p));
            }
        }
        if (differ == 0) {                           // the seat that differed is gone (dropped, lost): nothing to judge
            it = suspect_.erase(it);
            continue;
        }
        if (agree == 0 && waiting != 0) {            // nobody has vouched for the referee yet and somebody still may: wait for the answer
            ++it;
            continue;
        }
        if (agree == 0 && (later != 0 || held != 0)) {       // nobody who was there can say, but a seat that is away or came later can say of the turns that follow: this one is let go
            it = suspect_.erase(it);
            continue;
        }
        verdict.kind = agree != 0 ? HashVerdict::Kind::Drop : HashVerdict::Kind::Fail;
        verdict.agree = agree;
        for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
            if ((differ & bit(p)) == 0) continue;
            DesyncMsg d;
            d.turn = turn;
            d.player = p;
            d.host = ref->second;
            d.peer = found->second[p];
            verdict.seats.push_back(d);
        }
        suspect_.erase(it);
        return verdict;
    }
    return verdict;
}

bool Sequencer::can_seal() const noexcept { return laggard() == 255; }

uint32_t Sequencer::behind_turns(uint8_t player) const noexcept {
    if (player >= sim::MAX_PLAYERS || !active_[player]) return 0;
    return next_turn_ - std::min(acked_[player], next_turn_);
}

uint8_t Sequencer::laggard() const noexcept {
    uint8_t worst = 255;
    uint32_t worst_lag = 0;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!active_[p]) continue;
        const uint32_t lag = next_turn_ - std::min(acked_[p], next_turn_);   // sealed turns the peer has not executed yet
        if (lag > cfg_.max_lag_turns && lag > worst_lag) {
            worst = p;
            worst_lag = lag;
        }
    }
    return worst;
}

TurnMsg Sequencer::seal() {
    TurnMsg t;
    t.turn = next_turn_++;
    const uint32_t earned = cfg_.orders_per_second * kMilli / kTurnsPerSecond;           // (a seat's budgets earn what one turn is worth, up to their full stores)
    const uint32_t full = cfg_.order_burst * kMilli;
    for (uint32_t& left : budget_milli_) left = std::min(full, left + earned);
    const uint32_t earned_control = cfg_.controls_per_second * kMilli / kTurnsPerSecond;
    const uint32_t full_control = cfg_.control_burst * kMilli;
    for (uint32_t& left : control_milli_) left = std::min(full_control, left + earned_control);
    sim::canonical_order(queue_);
    t.commands = std::move(queue_);
    queue_.clear();
    queued_by_.fill(0);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {   // the commands that did not fit are the first of the next turn, as many as the turn takes from each player
        while (!carry_[p].empty() && queued_by_[p] < cfg_.max_commands_per_turn) {
            queue_.push_back(std::move(carry_[p].front()));
            carry_[p].pop_front();
            ++queued_by_[p];
        }
    }
    while (!ahead_.empty() && ahead_.begin()->first <= next_turn_) {      // the commands that were stamped with the turn that is next now (each seat's keep their order, behind what that seat has carried over)
        for (sim::Command& c : ahead_.begin()->second) {
            const uint8_t p = c.issuer;
            if (ahead_count_[p] > 0) --ahead_count_[p];
            if (queued_by_[p] < cfg_.max_commands_per_turn) {
                ++queued_by_[p];
                queue_.push_back(std::move(c));
            } else {
                carry_[p].push_back(std::move(c));
            }
        }
        ahead_.erase(ahead_.begin());
    }
    return t;
}

}  // namespace ants::net

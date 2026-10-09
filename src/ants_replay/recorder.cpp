#include "ants_replay/recorder.hpp"

namespace ants::replay {

Recorder::Recorder(Header head) {
    head.format_version = kFormatVersion;
    replay_.head = std::move(head);
    if (replay_.head.hash_period == 0 || replay_.head.hash_period > kMaxTurns) fail("the head's hash period is out of range");       // (on_tick divides by it)
}

void Recorder::fail(const std::string& why) {
    if (!failure_.empty()) return;
    failure_ = why;
    replay_.commands.clear();                                   // (nothing more is kept of a recording that is lost)
    replay_.commands.shrink_to_fit();
    replay_.hashes.clear();
    own_ = 0;
}

void Recorder::on_command(const sim::Command& command) {
    if (finished_ || !failure_.empty()) return;
    // What the engine turns down without changing anything (apply_command: RejectedMalformed) leaves no trace in the replay: a command of no known type, an ant order that names no ant or too many
    if (command.type == sim::CommandType::None || command.type > sim::CommandType::Last) {
        ++skipped_;
        return;
    }
    if (sim::has_ant_list(command.type)) {
        if (command.ants.empty() || command.ants.size() > sim::kMaxCommandAnts) {
            ++skipped_;
            return;
        }
    } else if (!command.ants.empty()) {                         // the engine applies it and takes no notice of the ants, but the wire form cannot hold them: the file could not say what was played
        fail("a command that has no ant list names ants");
        return;
    }
    bytes_ += 1 + sim::encoded_size(command);                   // (the gap is a byte or two: the count is about what the commands take in the file; encode() makes the final check)
    if (bytes_ > kMaxFileBytes) {
        fail("the match is too long to keep as a replay");
        return;
    }
    if (replay_.head.recorder_seat != kNoSeat && command.issuer == replay_.head.recorder_seat) ++own_;
    TimedCommand tc;
    tc.turn = turns_;
    tc.command = command;
    replay_.commands.push_back(std::move(tc));
}

void Recorder::on_tick(const sim::SimulationEngine& engine) {
    if (finished_ || !failure_.empty()) return;
    if (turns_ >= kMaxTurns) {
        fail("the match is longer than a match can be");
        return;
    }
    ++turns_;
    if (turns_ % replay_.head.hash_period == 0) replay_.hashes.push_back(static_cast<uint32_t>(engine.state_hash().total & 0xFFFFFFFFu));
}

void Recorder::on_turn(const net::TurnMsg& turn, const sim::SimulationEngine& engine) {
    if (finished_ || !failure_.empty()) return;
    if (turn.turn != turns_) {
        fail("turn " + std::to_string(turns_) + " was not seen (the next one is " + std::to_string(turn.turn) + ")");
        return;
    }
    for (const sim::Command& c : turn.commands) on_command(c);
    on_tick(engine);
}

std::vector<uint8_t> Recorder::finish(const sim::SimulationEngine& engine, std::string& error) {
    error.clear();
    if (finished_) {
        error = "the recording is finished already";
        return {};
    }
    finished_ = true;
    if (!failure_.empty()) {
        error = failure_;
        return {};
    }
    if (turns_ == 0) {
        error = "no turn ran";
        return {};
    }
    replay_.complete = true;
    replay_.total_turns = turns_;
    replay_.match_over = engine.is_match_over();
    replay_.final_hash = engine.state_hash().total;
    std::vector<uint8_t> bytes = encode(replay_, error);
    if (bytes.empty() && failure_.empty()) failure_ = error;
    return bytes;
}

std::vector<uint8_t> Recorder::snapshot(std::string& error) const {
    error.clear();
    if (finished_) {
        error = "the recording is finished already";
        return {};
    }
    if (!failure_.empty()) {
        error = failure_;
        return {};
    }
    if (turns_ == 0) {
        error = "no turn ran";
        return {};
    }
    return encode_snapshot(replay_, turns_, error);
}

}  // namespace ants::replay

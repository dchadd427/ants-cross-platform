// Replays in the application (replay.hpp, docs/REPLAYS.md): the recorder is made when a match begins and watches it: every command that the engine is given and every tick that it runs (a game on this
// computer: LocalSink, update_simulation and confirm_quit; a match of the network: the lock-step runner's tap, attach_net). When the match is over the file is made and the desktop game writes it into the
// `replays` folder beside its settings file. The web page keeps none: the games it plays in rooms are recorded by the game server (docs/REPLAYS.md). Nothing here changes what the engine does.

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "ants_app/application.hpp"
#include "ants_app/version.hpp"
#include "ants_replay/player.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace ants::app {

namespace {

namespace fs = std::filesystem;

/// A match that is left before its end is kept when it ran this long (a minute) or when the player at this machine gave an order in it
constexpr uint32_t kLeftMatchKeptTurns = 60 * net::kTurnsPerSecond;

/// A text for the head of a file: printable ASCII only (a letter outside it, a control character and the byte that a cut leaves of a multi-byte letter each become a '?': the reader refuses any other text), at most `limit` characters
std::string head_text(const std::string& raw, size_t limit) {
    std::string out;
    for (const char c : raw) {
        if (out.size() >= limit) break;
        const unsigned char u = static_cast<unsigned char>(c);
        out.push_back(u >= 0x20 && u <= 0x7E ? c : '?');
    }
    return out;
}

/// What a match's file is called: "ants-TREASURE-20261006-143209.antsrep" (the map, then the computer's date and time when the match ended). The map's name keeps its letters, digits, '_' and '-': whatever
/// a map is called, the name is safe on every system.
std::string replay_file_name(const std::string& map_name, std::time_t when) {
    std::string stem = map_name.size() > 4 ? map_name.substr(0, map_name.size() - 4) : map_name;      // (without ".LVL")
    for (char& c : stem) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';          // (ASCII only, whatever the locale)
        if (!safe) c = '_';
    }
    char stamp[32] = {0};
    if (const std::tm* local = std::localtime(&when)) std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
    return "ants-" + stem + "-" + stamp + ".antsrep";
}

/// The HUD's orders while a replay is watched: nowhere (a null sink would apply them to the engine, hud.hpp submit_command)
class DiscardSink final : public sim::CommandSink {
public:
    sim::CommandResult submit(const sim::Command&) override { return sim::CommandResult{}; }     // (status Ignored: nothing to do, no ant to answer)
};

/// How many turns a jump plays in one frame (a turn takes a few microseconds; the page shows the progress between frames)
constexpr uint32_t kJumpTurnsPerFrame = 1500;
/// The speeds that the page may ask for, times 100 (a quarter of the normal speed to sixteen times it)
constexpr int kMinSpeed100 = 25;
constexpr int kMaxSpeed100 = 1600;

/// The turns that an engine has run
uint32_t turn_of(const sim::SimulationEngine& sim) noexcept { return static_cast<uint32_t>(sim.current_tick()); }

#if defined(__EMSCRIPTEN__)
/// A text for the page's JSON: printable ASCII, with the backslash and the quote escaped (the names of a file are printable ASCII already: the reader refuses any other)
std::string json_text(const std::string& raw) {
    std::string out = "\"";
    for (const char c : raw) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(u >= 0x20 && u <= 0x7E ? c : '?');
    }
    out.push_back('"');
    return out;
}
#endif

}  // namespace

// The recording of a match begins (an earlier one that was not finished is dropped, and so is the last file). The web build records nothing: its page has nowhere to keep a file, and the matches it plays in
// rooms are recorded by the game server (docs/REPLAYS.md).
void Application::begin_recording(replay::Header head) {
    withdraw_replay();
    head.game_version = head_text(std::string(VERSION_STRING), replay::kMaxTextBytes);
    head.build_id = head_text(std::string(BUILD_ID), replay::kMaxTextBytes);
    for (std::string& name : head.names) name = head_text(name, replay::kMaxTextBytes / 2);               // (a typed name is at most 32 characters)
#if defined(__EMSCRIPTEN__)
    recorder_.reset();
#else
    recorder_ = std::make_unique<replay::Recorder>(std::move(head));
#endif
}

// The Start of a network match, as net_load_match has just applied it: the same data on every machine, so every machine's file holds the same match (the seat that wrote it is the only difference)
void Application::begin_net_recording() {
    recorder_.reset();
    const net::StartMsg& start = net_->start_info();
    replay::Header head;
    head.venue = "network game";
    head.map_name = start.map_name;
    head.map_hash = start.map_hash;
    head.seed = start.seed;
    head.roster = start.roster;
    head.fog = start.fog;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) head.names[p] = ((start.roster >> p) & 1u) != 0 ? start.names[p] : std::string();
    head.teams = start.teams();
    head.recorder_seat = net_->my_seat() < sim::MAX_PLAYERS ? net_->my_seat() : replay::kNoSeat;
    begin_recording(std::move(head));
}

// The match is over (check_match_over, once for each match) or is left (the player goes back to a menu, the connection is lost, the program ends): the file is made, kept and offered. A match that is left
// is kept when something happened in it, the player at this machine gave an order (the quit is one; the computer players' orders and the other machines' do not count) or a minute went by
// (docs/REPLAYS.md); one that is left at once leaves nothing. A recording that cannot be a file is dropped with a line on the console and the match goes on as it did.
void Application::finish_recording() {
    if (!recorder_) return;
    if (!sim_.is_match_over() && recorder_->own_commands() == 0 && recorder_->turns() < kLeftMatchKeptTurns) {
        recorder_.reset();
        return;
    }
    const std::string map_name = recorder_->replay().head.map_name;
    const bool ran = recorder_->turns() != 0;
    std::string error;
    std::vector<uint8_t> bytes = recorder_->finish(sim_, error);
    recorder_.reset();
    if (bytes.empty()) {
        if (ran) std::cerr << "[Application] This match is not kept as a replay: " << error << std::endl;       // (a match that ended before its first tick has nothing to keep)
        return;
    }
    last_replay_ = std::move(bytes);
    last_replay_name_ = replay_file_name(map_name, std::time(nullptr));
    offer_replay();
}

// The desktop game writes the file into the folder `replays` beside the settings file: the folder of --settings, else the per-user application folder (the one that has chat.txt); a run that has no settings
// file (a headless run: the tests') keeps nothing on disk. A file that is there is never replaced: a match that ends in the same second as another one gets "-2" (then "-3" ...), and the name that the
// application reports (last_replay_name) is the one that was written. The folder is never cleaned up: the game deletes nothing from it. The console says where the file went.
void Application::offer_replay() {
#if !defined(__EMSCRIPTEN__)
    constexpr int kMaxSameNameFiles = 10000;      // (how many files of one name a second can have: the next free "-2", "-3" ... is taken)
    const std::string settings = config_store_.location();
    if (settings.empty()) return;
    std::error_code ec;
    const fs::path folder = fs::path(settings).parent_path() / "replays";
    fs::create_directories(folder, ec);
    const std::string stem = last_replay_name_.substr(0, last_replay_name_.size() - 8);            // (without ".antsrep")
    fs::path file = folder / last_replay_name_;
    for (int n = 2; n <= kMaxSameNameFiles && fs::exists(file, ec); ++n) file = folder / (stem + "-" + std::to_string(n) + ".antsrep");
    if (fs::exists(file, ec)) {
        std::cerr << "[Application] The replay of this match could not be saved: every name like " << (folder / last_replay_name_).string() << " is taken" << std::endl;
        return;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(last_replay_.data()), static_cast<std::streamsize>(last_replay_.size()));
    out.close();
    if (!out) {
        fs::remove(file, ec);                                                                 // (a file that stops half way would read as a recording that was cut: none is better)
        std::cerr << "[Application] The replay of this match could not be saved to " << file.string() << std::endl;
        return;
    }
    last_replay_name_ = file.filename().string();
    std::cerr << "[Application] Replay saved: " << file.string() << std::endl;
#endif
}

// A match begins: the file of the last one is let go
void Application::withdraw_replay() {
    last_replay_.clear();
    last_replay_name_.clear();
}

// ------------------------------------------------------------------------------------------------
// Watching a replay (ApplicationConfig::replay_path, docs/REPLAYS.md "Watching"): the game shows the match that a file holds. The engine is set up as the match began (replay::begin_match, without the
// Fog of War: a recording has no point of view, and the fog is no part of any hash), the orders of the file go in at their turns, the HUD's own orders are discarded. The page's bar drives it through
// replay_control. A file that cannot be shown leaves the game idle with a failure that the page tells the player.
// ------------------------------------------------------------------------------------------------

// init: the file is read and checked before anything is opened, so that every way it can fail ends in a failure of the replay, never in a game that does not start
void Application::prepare_replay() {
    replay_mode_ = true;
    replay_failure_ = ReplayFailure::None;
    replay_failure_text_.clear();
    replay_file_.reset();
    std::vector<uint8_t> bytes;
    {
        std::ifstream in(config_.replay_path, std::ios::binary);
        if (in) {
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (bytes.size() > replay::kMaxFileBytes) bytes.clear();
        }
    }
    auto replay = std::make_unique<replay::Replay>();
    std::string why;
    if (bytes.empty() || !replay::decode(bytes.data(), bytes.size(), *replay, why)) {
        replay_failure_ = ReplayFailure::Unreadable;
        replay_failure_text_ = bytes.empty() ? std::string("the file is missing, empty or too big") : why;
        return;
    }
    if (!replay::plays_here(replay->head)) {
        const uint16_t needs = replay::sim_rules_of(replay->head);
        const bool older = needs != 0 ? needs < replay::kSimRules : replay->head.engine_rules < net::kProtocolVersion;
        replay_failure_ = older ? ReplayFailure::Older : ReplayFailure::Newer;
        replay_failure_text_ = replay::rules_refusal(replay->head);
        replay_file_ = std::move(replay);                                  // (its head is shown: the page says who recorded it)
        return;
    }
    if (!replay::load_map(replay->head, config_.maps_dir, replay_level_, why)) {
        replay_failure_ = ReplayFailure::NoMap;
        replay_failure_text_ = why;
        replay_file_ = std::move(replay);
        return;
    }
    const assets::LevelValidation verdict = replay_level_.validate(replay->head.roster);
    if (!verdict.playable) {
        replay_failure_ = ReplayFailure::NoMap;
        replay_failure_text_ = "the map cannot be played by the seats of this match: " + verdict.reason();
        replay_file_ = std::move(replay);
        return;
    }
    replay_file_ = std::move(replay);
}

ReplayState Application::replay_state() const noexcept {
    if (!replay_mode_) return ReplayState::None;
    if (replay_failure_ != ReplayFailure::None || !replay_file_) return ReplayState::Failed;
    if (replay_jumping_) return ReplayState::Jumping;
    if (replay_cut_) return ReplayState::CutShort;
    if (sim_.is_match_over()) return ReplayState::Ended;
    return replay_paused_ ? ReplayState::Paused : ReplayState::Playing;
}

int Application::replay_value(ReplayValue what) const noexcept {
    switch (what) {
        case ReplayValue::State: return static_cast<int>(replay_state());
        case ReplayValue::Turn: return replay_mode_ ? static_cast<int>(turn_of(sim_)) : 0;
        case ReplayValue::Total: return replay_file_ ? static_cast<int>(replay_file_->total_turns) : 0;
        case ReplayValue::Speed: return static_cast<int>(replay_speed_ * 100.0f + 0.5f);
        case ReplayValue::JumpPercent: {
            if (!replay_jumping_ || replay_jump_target_ <= replay_jump_from_) return 0;
            const uint32_t done = turn_of(sim_) > replay_jump_from_ ? turn_of(sim_) - replay_jump_from_ : 0;
            return static_cast<int>(std::min<uint64_t>(100, static_cast<uint64_t>(done) * 100 / (replay_jump_target_ - replay_jump_from_)));
        }
        case ReplayValue::Failure: return static_cast<int>(replay_failure_);
        case ReplayValue::JumpTarget: return static_cast<int>(replay_jump_target_);
    }
    return 0;
}

// The match of the file, set up on the engine and the screen; the first turn is next. Also the way back to it (a restart, a jump to an earlier turn): everything of the match that the screen holds is made again.
void Application::start_replay() {
    state_ = AppState::Playing;
    match_started_ = true;
    if (!discard_sink_) discard_sink_ = std::make_unique<DiscardSink>();
    hud_.set_command_sink(discard_sink_.get());
    recorder_.reset();                                                     // (a viewer records nothing)
    withdraw_replay();
    if (!replay_file_ || replay_failure_ != ReplayFailure::None) {
        report_replay_to_page();
        return;
    }
    const replay::Header& head = replay_file_->head;
    const uint8_t roster = head.roster;
    uint8_t seat = 0;
    while (seat < 3 && ((roster >> seat) & 1u) == 0) ++seat;               // (the score line at the top is the first seat's)
    current_level_ = replay_level_;
    if ((roster & 0x0Fu) != 0x0Fu) current_level_ = current_level_.for_roster(roster);
    replay::begin_match(sim_, *replay_file_, current_level_, false);
    local_roster_ = roster;
    player_name_ = head.names[seat];
    local_player_name_ = head.names[seat];
    if (renderer_) {
        renderer_->set_level(current_level_);
        apply_match_zoom();
    }
    set_local_player(seat);                                                // (the viewing player, the HUD's seat, the view at the start)
    hud_.set_roster_mask(roster);
    scorecard_.set_shown_teams(roster);
    apply_team_names(head.names, roster);
    hud_.set_player_name(head.names[seat]);                                // (an empty name is the colour word: apply_team_names leaves the old name when the new one is empty)
    scorecard_.set_local_player_name(head.names[seat]);
    replay_next_ = 0;
    replay_cut_ = false;
    replay_jumping_ = false;
    replay_jump_from_ = 0;
    replay_jump_target_ = 0;
    enter_match(true);                                                     // (the HUD and the scorecard are new, the music starts, no start dialog and no start sound)
    report_replay_to_page();
}

void Application::report_replay_to_page() {
#if defined(__EMSCRIPTEN__)
    std::string json = "{\"failure\":" + std::to_string(static_cast<int>(replay_failure_)) + ",\"text\":" + json_text(replay_failure_text_) + ",\"version\":" + json_text(std::string(VERSION_STRING)) + ",\"rules\":" + std::to_string(replay::kSimRules);
    if (replay_file_) {
        const replay::Header& head = replay_file_->head;
        json += ",\"map\":" + json_text(head.map_name) + ",\"game\":" + json_text(head.game_version) + ",\"made_rules\":" + std::to_string(replay::sim_rules_of(head)) + ",\"roster\":" + std::to_string(head.roster) +
                ",\"turns\":" + std::to_string(replay_file_->total_turns) + ",\"complete\":" + (replay_file_->complete ? "true" : "false") + ",\"over\":" + (replay_file_->match_over ? "true" : "false") + ",\"names\":[";
        for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) json += (p == 0 ? "" : ",") + json_text(head.names[p]);
        json += "]";
    }
    json += "}";
    const std::string script = "try{if(window.antsReplayReport)window.antsReplayReport(" + json + ");}catch(e){}";
    emscripten_run_script(script.c_str());
#endif
}

// The jump or the restart begins again from the first turn: the engine and everything the screen shows of the match are made new
void Application::replay_jump_to(uint32_t turn) {
    if (!replay_file_ || replay_failure_ != ReplayFailure::None) return;
    turn = std::min(turn, replay_file_->total_turns);
    if (turn < turn_of(sim_)) start_replay();                        // (the match is played from its first turn again, the way a jump back has to go)
    if (turn == turn_of(sim_)) return;
    replay_jumping_ = true;
    replay_jump_from_ = turn_of(sim_);
    replay_jump_target_ = turn;
    tick_accumulator_ = 0.0f;
}

void Application::replay_control(ReplayControl what, int value) {
    if (!replay_mode_ || !replay_file_ || replay_failure_ != ReplayFailure::None) return;
    switch (what) {
        case ReplayControl::TogglePause: replay_paused_ = !replay_paused_; break;
        case ReplayControl::SetPaused: replay_paused_ = value != 0; break;
        case ReplayControl::SetSpeed: replay_speed_ = static_cast<float>(std::clamp(value, kMinSpeed100, kMaxSpeed100)) / 100.0f; break;
        case ReplayControl::Seek: replay_jump_to(value < 0 ? 0u : static_cast<uint32_t>(value)); break;
        case ReplayControl::Restart:
            replay_paused_ = false;
            start_replay();
            break;
    }
}

// What a turn of the file does: its orders in the order they stand, then the tick. A turn with a picture is what a frame shows (post_tick); a turn of a jump is only played (the sounds and news are told once at the end).
bool Application::replay_turn(bool picture) {
    const replay::Replay& file = *replay_file_;
    const uint32_t turn = turn_of(sim_);
    if (turn >= file.total_turns) return false;
    for (; replay_next_ < file.commands.size() && file.commands[replay_next_].turn == turn; ++replay_next_) sim_.apply_command(file.commands[replay_next_].command);
    sim_.tick();
    const uint32_t done = turn + 1;
    const uint32_t period = file.head.hash_period;
    if (done % period == 0 && done / period <= file.hashes.size() && static_cast<uint32_t>(sim_.state_hash().total & 0xFFFFFFFFu) != file.hashes[done / period - 1]) {
        replay_diverged(done);
        return false;
    }
    if (picture) {
        post_tick();
    } else {
        (void)sim_.poll_audio_events();                                    // (drained: the queue must not grow; the news stay for the chat log)
    }
    return true;
}

void Application::replay_diverged(uint32_t turn) {
    replay_failure_ = ReplayFailure::Diverged;
    replay_failure_text_ = "this replay does not reproduce here: the match differs after " + replay::format_time(turn) + " (turn " + std::to_string(turn) + ")";
    replay_jumping_ = false;
    report_replay_to_page();
}

// The last turn is played: the orders that stand after it (a Quit that ends the match), then either the match is over (the results screen opens in update_results) or the recording stopped before its match did
void Application::replay_ends() {
    const replay::Replay& file = *replay_file_;
    for (; replay_next_ < file.commands.size(); ++replay_next_) sim_.apply_command(file.commands[replay_next_].command);
    if (file.complete && (sim_.state_hash().total != file.final_hash || sim_.is_match_over() != file.match_over)) {
        replay_diverged(file.total_turns);
        return;
    }
    if (!sim_.is_match_over()) replay_cut_ = true;
}

void Application::run_replay(float dt) {
    if (!replay_file_ || replay_failure_ != ReplayFailure::None || replay_cut_) return;
    const replay::Replay& file = *replay_file_;
    if (replay_jumping_) {
        for (uint32_t n = 0; n < kJumpTurnsPerFrame && turn_of(sim_) < replay_jump_target_; ++n) {
            if (!replay_turn(false)) break;
        }
        if (replay_failure_ != ReplayFailure::None) return;
        if (turn_of(sim_) >= replay_jump_target_) {
            replay_jumping_ = false;
            catch_up_backlog_ = true;                                      // (what the turns of the jump made is not told: the sounds are dropped, the news stay in the chat log)
            post_tick();
            tick_accumulator_ = 0.0f;
            if (turn_of(sim_) >= file.total_turns) replay_ends();
        }
        return;
    }
    if (replay_paused_ || sim_.is_match_over()) return;
    tick_accumulator_ += dt * replay_speed_;
    while (tick_accumulator_ >= 0.050f) {
        tick_accumulator_ -= 0.050f;
        if (!replay_turn(true) || sim_.is_match_over()) break;
    }
    if (replay_failure_ == ReplayFailure::None && turn_of(sim_) >= file.total_turns && !replay_cut_ && !sim_.is_match_over()) replay_ends();
}

// Quit and Leave Game: back to the page's list of matches (a native game ends)
void Application::leave_replay() {
#if defined(__EMSCRIPTEN__)
    emscripten_run_script("try{if(window.antsReplayLeave)window.antsReplayLeave();else window.location.assign('/watch.html');}catch(e){}");
#else
    is_running_ = false;
#endif
}

}  // namespace ants::app

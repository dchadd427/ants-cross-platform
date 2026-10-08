// Replays in the application (replay.hpp, docs/REPLAYS.md): the recorder is made when a match begins and watches it: every command that the engine is given and every tick that it runs (a game on this
// computer: LocalSink, update_simulation and confirm_quit; a match of the network: the lock-step runner's tap, attach_net). When the match is over the file is made and the desktop game writes it into the
// `replays` folder beside its settings file. The web page keeps none: the games it plays in rooms are recorded by the game server (docs/REPLAYS.md). Nothing here changes what the engine does.

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "ants_app/application.hpp"
#include "ants_app/version.hpp"

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

}  // namespace ants::app

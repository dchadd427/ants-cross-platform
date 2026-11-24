#include "ants_replay/player.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

#include "ants_net/netgame.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/start_teams.hpp"

namespace ants::replay {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string hex64(uint64_t v) {
    static const char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) out[static_cast<size_t>(i)] = digits[v & 0xFu];
    return out;
}

/// 65535 as "65,535" (the page says its numbers the same way)
std::string with_commas(uint64_t n) {
    std::string digits = std::to_string(n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

/// Whether a map that a file carries is one that a replay is set up with: not more starting ants than kMaxEmbeddedStartAnts, and not more pairs of a plant and a waypoint than kMaxEmbeddedPlantWaypointPairs (both are
/// the work of setting a match up: the second is SimulationEngine::init's lookup of the waypoint of every plant). False and one sentence in `error` when it is not. A stranger's file: the work of such a map holds the
/// one thread for seconds natively and minutes in a browser, so it is refused before any match is made of it. The counts are those of the loader and of the engine: the start markers are the records of block 1 of a
/// team of the four, the plants those whose tile has the plant flag, the waypoints the records of block 4 that the loader kept.
bool fits_a_replay(const assets::LevelData& level, std::string& error) {
    uint64_t ants = 0, plants = 0;
    for (const assets::AnthillSpawn& spawn : level.anthill_spawns()) {
        if (spawn.team_id < 4) ++ants;
        if (sim::movement::is_dropper_plant_tile(spawn.tile_id)) ++plants;
    }
    if (ants > kMaxEmbeddedStartAnts) {
        error = "the map in the replay starts " + with_commas(ants) + " ants (the start markers of the four colors), more than the " + with_commas(kMaxEmbeddedStartAnts) + " that a replay is set up with";
        return false;
    }
    const uint64_t waypoints = level.waypoints.size();
    if (plants * waypoints > kMaxEmbeddedPlantWaypointPairs) {
        error = "the map in the replay has " + with_commas(plants) + " plants and " + with_commas(waypoints) + " waypoints, so setting it up looks at " + with_commas(plants * waypoints) + " pairs of them, more than the " +
                with_commas(kMaxEmbeddedPlantWaypointPairs) + " that a replay is set up with";
        return false;
    }
    return true;
}

/// The file of `maps_dir` called `name`: the name as written, else a file of the same name in another case ("" when there is none)
std::string find_map_file(const std::string& maps_dir, const std::string& name) {
    std::error_code ec;
    const fs::path exact = fs::path(maps_dir) / name;
    if (fs::is_regular_file(exact, ec)) return exact.string();
    const std::string wanted = lower(name);
    for (fs::directory_iterator it(maps_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (lower(it->path().filename().string()) == wanted && it->is_regular_file(ec)) return it->path().string();
    }
    return std::string();
}

}  // namespace

bool load_map(const Header& head, const std::string& maps_dir, assets::LevelData& level, std::string& error) {
    return load_map(head, std::vector<std::string>{maps_dir}, level, error);
}

bool load_map(const Header& head, const std::vector<std::string>& dirs, assets::LevelData& level, std::string& error, const assets::LevelLimits& limits, assets::LevelValidation* verdict) {
    error.clear();
    if (!net::valid_map_name(head.map_name)) {
        error = "the replay's map name cannot name a map file";
        return false;
    }
    std::string path, where;
    for (const std::string& dir : dirs) {                         // (the first folder that holds the name is the one: a file of that name there that is another file is refused, as the server's map list does)
        if (!where.empty()) where += " or ";
        where += dir;
        path = find_map_file(dir, head.map_name);
        if (!path.empty()) break;
    }
    if (path.empty()) {
        error = "the map " + head.map_name + " is not in " + where + " (the replay needs it; --maps-dir says where the maps are)";
        return false;
    }
    return load_map_file(head, path, level, error, limits, verdict);
}

bool load_map_file(const Header& head, const std::string& path, assets::LevelData& level, std::string& error, const assets::LevelLimits& limits, assets::LevelValidation* verdict_out) {
    error.clear();
    uint64_t hash = 0;
    if (!net::hash_file(path, hash)) {
        error = "the map file " + path + " cannot be read";
        return false;
    }
    if (hash != head.map_hash) {
        error = "the map " + head.map_name + " here is not the file that the match was played on (its hash is " + hex64(hash) + ", the replay's is " + hex64(head.map_hash) + ")";
        return false;
    }
    assets::LevelValidation verdict;
    if (!level.load_from_file(path, &verdict, limits)) {
        error = "the map " + head.map_name + " cannot be loaded" + (verdict.first_fatal() ? ": " + verdict.reason() : std::string());
        if (verdict_out != nullptr) *verdict_out = verdict;
        return false;
    }
    return true;
}

bool load_embedded_map(const Replay& replay, assets::LevelData& level, std::string& error) {
    error.clear();
    const std::vector<uint8_t>& bytes = replay.embedded_map;
    if (bytes.empty()) {
        error = "the replay carries no map";
        return false;
    }
    if (bytes.size() > kMaxEmbeddedMapBytes) {                    // (decode() refuses it; a replay made by hand could hold it)
        error = "the map in the replay is longer than a map can be";
        return false;
    }
    const uint64_t hash = net::hash_bytes(bytes.data(), bytes.size());
    if (hash != replay.head.map_hash) {
        error = "the map in the replay is not the file that the match was played on (its hash is " + hex64(hash) + ", the head's is " + hex64(replay.head.map_hash) + ")";
        return false;
    }
    assets::LevelValidation verdict;
    if (!level.load_from_memory(bytes.data(), bytes.size(), &verdict, assets::LevelLimits::for_play_extended())) {
        error = "the map in the replay cannot be loaded" + (verdict.first_fatal() ? ": " + verdict.reason() : std::string());
        return false;
    }
    return fits_a_replay(level, error);
}

bool load_map_or_embedded(const Replay& replay, const std::vector<std::string>& dirs, assets::LevelData& level, std::string& error) {
    if (load_map(replay.head, dirs, level, error)) {
        // A file that carries its map is held to the same limits whichever copy is read: the page writes the map that a file carries into the maps folder under its name, so this is the way in for such a file on the web
        const std::vector<uint8_t>& carried = replay.embedded_map;
        if (!carried.empty() && carried.size() <= kMaxEmbeddedMapBytes && net::hash_bytes(carried.data(), carried.size()) == replay.head.map_hash) return fits_a_replay(level, error);
        return true;
    }
    if (replay.embedded_map.empty()) return false;                // (the folder's own words)
    return load_embedded_map(replay, level, error);
}

std::string rules_refusal(const Header& head) {
    const uint16_t needs = sim_rules_of(head);
    const std::string made_by = head.game_version.empty() ? std::string("a game of unknown version") : head.game_version;
    if (needs == 0) {
        return "recorded by " + made_by + " with network protocol " + std::to_string(head.engine_rules) + ", whose simulation this build does not know; this build plays simulation rules " + played_rules_text() +
               ": open it with the game that made it";
    }
    return "recorded by " + made_by + " with simulation rules " + std::to_string(needs) + "; this build plays simulation rules " + played_rules_text() + ": open it with the game that made it";
}

void begin_match(sim::SimulationEngine& engine, const Replay& replay, const assets::LevelData& level, bool fog) {
    engine.set_fog_of_war_enabled(fog);
    engine.set_game_mode(static_cast<sim::GameMode>(replay.head.mode));       // (before init, like every machine of the match did; plays_here and the head's checks have made sure that the mode is one this build knows)
    engine.set_hill_open_187(replay.head.mode == static_cast<uint8_t>(sim::GameMode::Kills187) && sim_rules_of(replay.head) == kSimRulesMode187Heal);       // (a 187 match recorded before the closed hill plays with the hill that works)
    engine.set_end_fix_187(!(replay.head.mode == static_cast<uint8_t>(sim::GameMode::Kills187) && sim_rules_of(replay.head) < kSimRulesMode187BeforeNoDrops));       // (a 187 match recorded before the end-rule fixes, 6, 7 or 10, counts the seats of the roster and any egg)
    engine.set_no_drops_187(!(replay.head.mode == static_cast<uint8_t>(sim::GameMode::Kills187) && sim_rules_of(replay.head) < kSimRulesMode187BeforeCantGo));       // (a 187 match recorded before the no-drop rule, 6, 7, 10 or 11, leaves the power-up of a special ant that dies on the ground as the original's game does)
    engine.set_last_side_rule(!(replay.head.mode == 0 && sim_rules_of(replay.head) == kSimRulesBeforeLastSide));       // (a match of the original's game recorded before the last-side rule ends by the original's CHECKGO alone)
    engine.set_fire_wall_as_original(fire_wall_as_original_rules(sim_rules_of(replay.head)));       // (a match recorded before the loader of the original reads a named fire wall with the remake's obstacle overlay, in either mode)
    engine.set_bite_skips_empty_record(bite_skips_empty_record_rules(sim_rules_of(replay.head)));       // (a match recorded before the rule of the empty record takes every bite from the first object of the food tile, as the original does, in either mode)
    engine.set_path_answers_after_commands(path_answers_after_commands_rules(sim_rules_of(replay.head)));       // (a match recorded before the rule of the can't-go gap gives the path manager's answer at once, at the end of the tick, in either mode)
    engine.init(level, replay.head.seed, replay.head.roster);
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        engine.set_player_name(seat, ((replay.head.roster >> seat) & 1u) != 0 ? replay.head.names[seat] : std::string());
    }
    if (replay.head.teams.set) sim::apply_start_teams(engine, replay.head.teams);
}

void take_result_of(uint8_t roster, sim::SimulationEngine& engine, Outcome& out) {
    out.hash = engine.state_hash().total;
    out.match_over = engine.is_match_over();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        const bool plays = ((roster >> seat) & 1u) != 0;
        out.scores[seat] = plays ? engine.get_display_score(seat) : 0;
        if (plays && engine.is_player_dropped(seat)) out.dropped_mask = static_cast<uint8_t>(out.dropped_mask | (1u << seat));
    }
    out.result = engine.get_world_state().match_result;                 // (the engine's own make_match_result: also the side that is left in mode 187, which a bare evaluate_victory does not know)
    out.ran = true;
}

Playback::Playback(const Replay& replay, const assets::LevelData& level, const Hooks& hooks) : replay_(replay), hooks_(hooks) {
    out_.complete = replay.complete;
    done_ = true;                                                       // (until the engine is set up: a refusal leaves it so)
    if (!plays_here(replay.head)) {
        out_.error = rules_refusal(replay.head);
        return;
    }
    if (replay.head.hash_period == 0 || replay.total_turns > kMaxTurns) {      // (what encode and decode refuse; a replay made by hand could hold them)
        out_.error = "the replay's hash period or its length is out of range";
        return;
    }
    engine_ = std::make_unique<sim::SimulationEngine>();
    begin_match(*engine_, replay, level, replay.head.fog);
    done_ = false;
}

Playback::~Playback() = default;

void Playback::give(const TimedCommand& tc) {
    if (hooks_.before_command) hooks_.before_command(tc.turn, tc.command, *engine_);
    const sim::CommandResult result = engine_->apply_command(tc.command);
    if (hooks_.after_command) hooks_.after_command(tc.turn, tc.command, result, *engine_);
}

// After the last tick: the commands that wait for the end (a Quit that ends the match), the result and the checks of the final state
void Playback::end() {
    done_ = true;
    for (; next_ < replay_.commands.size() && replay_.commands[next_].turn == replay_.total_turns; ++next_) give(replay_.commands[next_]);
    take_result_of(replay_.head.roster, *engine_, out_);
    if (replay_.complete && out_.hash != replay_.final_hash) {
        out_.first_bad_turn = replay_.total_turns;
        out_.error = "this replay does not reproduce here: the final state differs";
        return;
    }
    if (replay_.complete && out_.match_over != replay_.match_over) {
        out_.first_bad_turn = replay_.total_turns;
        out_.error = "this replay does not reproduce here: the match ended differently";
        return;
    }
    out_.ok = true;
}

bool Playback::advance(uint32_t turns) {
    if (done_) return true;
    const uint32_t period = replay_.head.hash_period;
    uint32_t played = 0;
    for (; played < turns && turn_ < replay_.total_turns; ++played) {
        for (; next_ < replay_.commands.size() && replay_.commands[next_].turn == turn_; ++next_) give(replay_.commands[next_]);
        engine_->tick();
        if (hooks_.after_tick) hooks_.after_tick(turn_ + 1, *engine_);
        engine_->clear_news_events();                                   // (nobody reads them here, and they would pile up for the whole match)
        engine_->clear_audio_events();
        out_.turns = turn_ + 1;
        ++turn_;
        if (out_.turns % period == 0 && out_.turns / period <= replay_.hashes.size()) {
            ++out_.hashes_checked;
            if (static_cast<uint32_t>(engine_->state_hash().total & 0xFFFFFFFFu) != replay_.hashes[out_.turns / period - 1]) {
                out_.first_bad_turn = out_.turns;
                out_.hash = engine_->state_hash().total;
                out_.match_over = engine_->is_match_over();
                out_.error = "this replay does not reproduce here: the match differs after " + format_time(out_.turns) + " (turn " + std::to_string(out_.turns) + ")";
                done_ = true;
                return true;
            }
        }
    }
    if (turn_ < replay_.total_turns || (played != 0 && played == turns)) return false;       // (the turns are played: the end is the next call's, when this one used up the turns it was given)
    end();
    return true;
}

bool Playback::next_takes_hash() const noexcept {
    if (done_) return false;
    if (turn_ >= replay_.total_turns) return true;                                                    // (the end: the final state's hash)
    const uint32_t next = turn_ + 1;
    return replay_.head.hash_period != 0 && next % replay_.head.hash_period == 0 && next / replay_.head.hash_period <= replay_.hashes.size();
}

Outcome play(const Replay& replay, const assets::LevelData& level, const Hooks& hooks) {
    Playback playback(replay, level, hooks);
    while (!playback.advance(replay.total_turns + 1u)) {
    }
    return playback.outcome();
}

std::vector<OrderRecord> list_orders(const Replay& replay, const assets::LevelData& level, Outcome& outcome) {
    std::vector<OrderRecord> lines;
    OrderRecord pending;
    Hooks hooks;
    hooks.before_command = [&](uint32_t turn, const sim::Command& c, sim::SimulationEngine& engine) {
        pending = OrderRecord{};
        pending.turn = turn;
        pending.command = c;
        for (const uint32_t id : c.ants) {
            const sim::AntUnit* ant = nullptr;
            try {
                ant = &engine.get_unit(id);
            } catch (const std::runtime_error&) {
                ant = nullptr;                                          // (no such ant)
            }
            if (ant == nullptr || ant->removed || ant->player_id != c.issuer) {
                ++pending.absent;
                continue;
            }
            const sim::AntType type = engine.ant_type(*ant);
            auto at = std::find_if(pending.ants.begin(), pending.ants.end(), [type](const OrderRecord::AntCount& a) { return a.type == type; });
            if (at == pending.ants.end()) pending.ants.push_back(OrderRecord::AntCount{type, 1});
            else ++at->count;
            if (!pending.has_from) {
                pending.has_from = true;
                pending.from_x = ant->pos.x;
                pending.from_y = ant->pos.y;
            }
        }
    };
    hooks.after_command = [&](uint32_t, const sim::Command&, const sim::CommandResult& result, sim::SimulationEngine&) {
        pending.status = result.status;
        pending.ants_ordered = result.ants_ordered;
        lines.push_back(pending);
    };
    outcome = play(replay, level, hooks);
    return lines;
}

std::string format_time(uint32_t turn) {
    const uint64_t tenths = static_cast<uint64_t>(turn) * net::kTurnMs / 100;     // (a turn is 50 ms: half a tenth, rounded down)
    return std::to_string(tenths / 600) + ":" + (tenths / 10 % 60 < 10 ? "0" : "") + std::to_string(tenths / 10 % 60) + "." + std::to_string(tenths % 10);
}

std::string seat_label(const Header& head, uint8_t seat) {
    static const char* const colors[sim::MAX_PLAYERS] = {"Green", "Red", "Blue", "Black"};
    if (seat >= sim::MAX_PLAYERS) return "seat " + std::to_string(static_cast<unsigned>(seat));
    return head.names[seat].empty() ? std::string(colors[seat]) : std::string(colors[seat]) + " (" + head.names[seat] + ")";
}

const char* ant_type_name(sim::AntType type) noexcept {
    switch (type) {
        case sim::AntType::Worker: return "Worker";
        case sim::AntType::Bomber: return "Bomber";
        case sim::AntType::Fire: return "Fire";
        case sim::AntType::Thief: return "Thief";
        case sim::AntType::Combat: return "Combat";
        case sim::AntType::Swimmer: return "Swimmer";
    }
    return "?";
}

const char* order_name(sim::CommandType type) noexcept {
    switch (type) {
        case sim::CommandType::GroupMove: return "move";
        case sim::CommandType::GroupSpecial: return "special";
        case sim::CommandType::GroupAttack: return "attack";
        case sim::CommandType::Stop: return "stop";
        case sim::CommandType::Hatch: return "hatch";
        case sim::CommandType::AllianceInvite: return "team offer";
        case sim::CommandType::AllianceAccept: return "team accept";
        case sim::CommandType::AllianceDeny: return "team refuse";
        case sim::CommandType::AllianceWithdraw: return "team withdraw";
        case sim::CommandType::AllianceBreak: return "team leave";
        case sim::CommandType::Quit: return "quit";
        case sim::CommandType::Drop: return "dropped out";
        case sim::CommandType::None: break;
    }
    return "?";
}

const char* status_name(sim::CommandResult::Status status) noexcept {
    switch (status) {
        case sim::CommandResult::Status::Applied: return "ok";
        case sim::CommandResult::Status::Ignored: return "ignored";
        case sim::CommandResult::Status::RejectedIssuer:
        case sim::CommandResult::Status::RejectedMalformed:
        case sim::CommandResult::Status::RejectedNotAllowed: return "refused";
    }
    return "?";
}

std::string ants_text(const OrderRecord& order) {
    std::string text;
    for (const OrderRecord::AntCount& a : order.ants) {
        if (!text.empty()) text += ", ";
        text += std::to_string(a.count) + " " + ant_type_name(a.type);
    }
    if (order.absent != 0) text += (text.empty() ? "" : " + ") + std::to_string(order.absent) + " not there";
    return text;
}

std::string place_text(const OrderRecord& order, const Header& head) {
    const sim::Command& c = order.command;
    if (sim::is_group_order(c.type)) return "(" + std::to_string(c.tile_x) + "," + std::to_string(c.tile_y) + ")";
    switch (c.type) {
        case sim::CommandType::AllianceInvite:
        case sim::CommandType::AllianceAccept:
        case sim::CommandType::AllianceDeny:
        case sim::CommandType::AllianceWithdraw:
            return "with " + seat_label(head, c.other_player);
        default: break;
    }
    return std::string();
}

}  // namespace ants::replay

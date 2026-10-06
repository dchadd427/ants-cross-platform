#include "ants_replay/player.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

#include "ants_net/netgame.hpp"
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
    error.clear();
    if (!net::valid_map_name(head.map_name)) {
        error = "the replay's map name cannot name a map file";
        return false;
    }
    const std::string path = find_map_file(maps_dir, head.map_name);
    if (path.empty()) {
        error = "the map " + head.map_name + " is not in " + maps_dir + " (the replay needs it; --maps-dir says where the maps are)";
        return false;
    }
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
    if (!level.load_from_file(path, &verdict)) {
        error = "the map " + head.map_name + " cannot be loaded" + (verdict.first_fatal() ? ": " + verdict.reason() : std::string());
        return false;
    }
    return true;
}

Outcome play(const Replay& replay, const assets::LevelData& level, const Hooks& hooks) {
    Outcome out;
    out.complete = replay.complete;
    if (replay.head.engine_rules != net::kProtocolVersion) {
        out.error = "recorded with network protocol " + std::to_string(replay.head.engine_rules) + " (" + (replay.head.game_version.empty() ? std::string("a game of unknown version") : replay.head.game_version) +
                    "); this build plays protocol " + std::to_string(net::kProtocolVersion) + ": open it with the game that made it";
        return out;
    }
    if (replay.head.hash_period == 0 || replay.total_turns > kMaxTurns) {      // (what encode and decode refuse; a replay made by hand could hold them)
        out.error = "the replay's hash period or its length is out of range";
        return out;
    }
    sim::SimulationEngine engine;
    engine.set_fog_of_war_enabled(replay.head.fog);
    engine.init(level, replay.head.seed, replay.head.roster);
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        engine.set_player_name(seat, ((replay.head.roster >> seat) & 1u) != 0 ? replay.head.names[seat] : std::string());
    }
    if (replay.head.teams.set) sim::apply_start_teams(engine, replay.head.teams);

    size_t next = 0;
    const auto give = [&](const TimedCommand& tc) {
        if (hooks.before_command) hooks.before_command(tc.turn, tc.command, engine);
        const sim::CommandResult result = engine.apply_command(tc.command);
        if (hooks.after_command) hooks.after_command(tc.turn, tc.command, result, engine);
    };
    const uint32_t period = replay.head.hash_period;
    for (uint32_t turn = 0; turn < replay.total_turns; ++turn) {
        for (; next < replay.commands.size() && replay.commands[next].turn == turn; ++next) give(replay.commands[next]);
        engine.tick();
        engine.clear_news_events();                                     // (nobody reads them here, and they would pile up for the whole match)
        engine.clear_audio_events();
        out.turns = turn + 1;
        if (out.turns % period == 0 && out.turns / period <= replay.hashes.size()) {
            ++out.hashes_checked;
            if (static_cast<uint32_t>(engine.state_hash().total & 0xFFFFFFFFu) != replay.hashes[out.turns / period - 1]) {
                out.first_bad_turn = out.turns;
                out.hash = engine.state_hash().total;
                out.match_over = engine.is_match_over();
                out.error = "this replay does not reproduce here: the match differs after " + format_time(out.turns) + " (turn " + std::to_string(out.turns) + ")";
                return out;
            }
        }
    }
    for (; next < replay.commands.size() && replay.commands[next].turn == replay.total_turns; ++next) give(replay.commands[next]);     // (after the last tick: a Quit that ends the match)
    out.hash = engine.state_hash().total;
    out.match_over = engine.is_match_over();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) out.scores[seat] = ((replay.head.roster >> seat) & 1u) != 0 ? engine.get_display_score(seat) : 0;
    out.ran = true;
    if (replay.complete && out.hash != replay.final_hash) {
        out.first_bad_turn = replay.total_turns;
        out.error = "this replay does not reproduce here: the final state differs";
        return out;
    }
    if (replay.complete && out.match_over != replay.match_over) {
        out.first_bad_turn = replay.total_turns;
        out.error = "this replay does not reproduce here: the match ended differently";
        return out;
    }
    out.ok = true;
    return out;
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
    static const char* const colours[sim::MAX_PLAYERS] = {"Green", "Red", "Blue", "Black"};
    if (seat >= sim::MAX_PLAYERS) return "seat " + std::to_string(static_cast<unsigned>(seat));
    return head.names[seat].empty() ? std::string(colours[seat]) : std::string(colours[seat]) + " (" + head.names[seat] + ")";
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

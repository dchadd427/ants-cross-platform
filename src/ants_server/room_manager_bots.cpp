#include <algorithm>
#include <filesystem>
#include <random>
#include <set>

#include "ants_net/clock.hpp"
#include "ants_server/online_limits.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/start_teams.hpp"

// The server's own matches of computer players, the part of the room manager that makes, lists and stops them (docs/SERVER_REPLAYS.md "Matches of computer players"; what asks for them is bot_matches.hpp).

namespace ants::server {

void RoomManager::seed_bot_matches(uint32_t seed) {
    bot_rng_.seed(seed != 0 ? seed : std::random_device{}());
    bot_rng_seeded_ = true;
}

// The maps of the server's own matches, one list for each rules (docs/SERVER_REPLAYS.md "Matches of computer players": the one home of the lists): the match is dealt its rules first, then a map of that rules' list, each map as likely as any
// other of its list. A map of the players (`approved`) is found in the store of approved maps only, so a server without the submissions door, or one that took the map down, has none; a map that this server cannot play is
// skipped (one line in the log) and the others are as likely as before. ISLANDS is on the first list alone: its water joins no two hills by a walk and 187 has no Swimmer, so nobody could ever be struck there (docs/BOT_187.md).
namespace {

struct BotMatchMap {
    const char* file;
    bool approved;
};

const std::vector<BotMatchMap>& bot_match_maps(bool kills_187) {
    static const std::vector<BotMatchMap> kHighestScore = {{"TINY.LVL", false},   {"SMALL.LVL", false},  {"MEDIUM.LVL", false},   {"GAUNTLET.LVL", false}, {"TREASURE.LVL", false},
                                                           {"ISLANDS.LVL", false}, {"POPcOrN.lvl", false}, {"HELLRAISER.lvl", false}, {"Bombz Away.lvl", false}};
    static const std::vector<BotMatchMap> k187 = {{"TINY.LVL", false}, {"SMALL.LVL", false}, {"MEDIUM.LVL", false}, {"GAUNTLET.LVL", false}, {"TREASURE.LVL", false}, {"Fight Island.lvl", true}};
    return kills_187 ? k187 : kHighestScore;
}

constexpr size_t kOriginalMaps = 6;                      // (the first six of the list of the original's game are the original's six; the three that follow are the library's, which a choice may name)
constexpr const char* kIslands = "ISLANDS.LVL";

bool is_fixed_map(const std::string& file, size_t& at) {
    const std::vector<BotMatchMap>& list = bot_match_maps(false);
    for (at = 0; at < list.size(); ++at) {
        if (file == list[at].file) return true;
    }
    return false;
}

std::string places_words(size_t people, size_t total) { return "people's matches use " + std::to_string(people) + " of the server's " + std::to_string(total) + " places"; }

// The sides of a match by seat, as the admin page has them: "ffa", or the seats of each side in digits, the sides from the lowest seat up ("03-12": seats 0 and 3 against 1 and 2; "01-2": seats 0 and 1 against 2)
std::string bot_sides_of(const std::string& room_teams, uint8_t roster) {
    sim::StartTeams teams;
    std::string why;
    if (room_teams.empty() || !sim::parse_start_teams(room_teams, teams, why) || !teams.set) return "ffa";
    const sim::StartTeamsPlan plan = sim::plan_start_teams(teams, roster);
    if (!plan.why.empty() || plan.pairs.empty()) return "ffa";
    std::vector<uint8_t> sides;                             // (a bit for each seat of a side)
    uint8_t used = 0;
    for (const std::array<uint8_t, 2>& pair : plan.pairs) {
        sides.push_back(static_cast<uint8_t>((1u << pair[0]) | (1u << pair[1])));
        used = static_cast<uint8_t>(used | sides.back());
    }
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((roster & (1u << seat)) != 0 && (used & (1u << seat)) == 0) sides.push_back(static_cast<uint8_t>(1u << seat));      // (a bot that plays alone: the third of three)
    }
    std::sort(sides.begin(), sides.end(), [](uint8_t a, uint8_t b) { return (a & -a) < (b & -b); });
    std::string text;
    for (const uint8_t side : sides) {
        if (!text.empty()) text += '-';
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            if ((side & (1u << seat)) != 0) text += static_cast<char>('0' + seat);
        }
    }
    return text;
}

}  // namespace

size_t RoomManager::bot_match_count() const {
    size_t n = 0;
    for (const auto& kv : rooms_) {
        const RoomState state = kv.second->state();
        if (kv.second->bots_only() && (state == RoomState::Waiting || state == RoomState::Loading || state == RoomState::Running)) ++n;
    }
    return n;
}

uint32_t RoomManager::bot_places() const {
    uint32_t places = 0;
    for (const auto& kv : rooms_) {
        const RoomState state = kv.second->state();
        if (kv.second->bots_only() && (state == RoomState::Waiting || state == RoomState::Loading || state == RoomState::Running)) places += kv.second->map_places();
    }
    return places;
}

// More than three of every four places of the server (--demo-rooms) are taken by matches of people: the matches of computer players wait. A server without places for people (--demo-rooms 0) never holds a start back.
bool RoomManager::people_busy() const {
    return limits_.demo_rooms != 0 && public_places() * kBotPeopleShareDenominator > limits_.demo_rooms * kBotPeopleShareNumerator;
}

std::vector<BotMapOption> RoomManager::bot_map_options() const {
    std::vector<BotMapOption> out;
    std::set<std::string> known;                                // (the maps of the lists now: a verdict for any other name is dropped at the end)
    const std::vector<BotMatchMap>& fixed = bot_match_maps(false);
    for (size_t i = 0; i < fixed.size(); ++i) {
        known.insert(fixed[i].file);
        uint32_t minutes = 0, places = 1;
        if (!bot_match_map_refusal(fixed[i].file, false, minutes, &places).empty()) continue;
        BotMapOption o;
        o.file = fixed[i].file;
        o.group = i < kOriginalMaps ? "original" : "library";
        for (const MapListing& listed : store_.listing()) {
            if (listed.file == o.file) {
                o.width = listed.width;
                o.height = listed.height;
            }
        }
        o.places = places;
        o.minutes = minutes;
        o.random = minutes <= kBotRandomMaxMinutes;
        out.push_back(std::move(o));
    }
    for (const ApprovedMap& map : store_.approved_maps()) {
        known.insert(map.name);
        uint32_t minutes = 0, places = 1;
        size_t at = 0;
        if (is_fixed_map(map.name, at) || !bot_match_map_refusal(map.name, true, minutes, &places).empty()) continue;
        BotMapOption o;
        o.file = map.name;
        o.group = "players";
        o.width = map.width;
        o.height = map.height;
        o.places = places;
        o.minutes = minutes;
        o.random = minutes <= kBotRandomMaxMinutes;
        out.push_back(std::move(o));
    }
    for (auto it = bot_map_verdicts_.begin(); it != bot_map_verdicts_.end();) {      // (a map that was taken down leaves nothing behind)
        if (known.count(it->first) == 0) it = bot_map_verdicts_.erase(it);
        else ++it;
    }
    return out;
}

size_t RoomManager::bot_rotation_maps(bool kills_187) const {
    size_t n = 0;
    for (const BotMatchMap& entry : bot_match_maps(kills_187)) {
        uint32_t minutes = 0;
        if (bot_match_map_refusal(entry.file, entry.approved, minutes).empty() && minutes <= kBotRandomMaxMinutes) ++n;
    }
    return n;
}

std::vector<BotMatchInfo> RoomManager::bot_matches(uint32_t now_ms) const {
    std::vector<BotMatchInfo> out;
    std::vector<uint32_t> ages;
    std::set<std::string> listed;
    for (const LiveMatch& live : live_board_.list()) listed.insert(live.id);
    for (const auto& kv : rooms_) {
        const Room& room = *kv.second;
        const RoomState state = room.state();
        if (!room.bots_only() || !(state == RoomState::Waiting || state == RoomState::Loading || state == RoomState::Running)) continue;
        const RoomStatus s = room.status(now_ms);
        BotMatchInfo info;
        info.code = kv.first;
        info.map = s.map;
        info.mode = s.mode;
        info.state = state;
        uint8_t roster = 0;
        for (const RoomStatus::Bot& b : s.bots) {
            info.seats.emplace_back(b.seat, b.level);
            roster = static_cast<uint8_t>(roster | (1u << b.seat));
        }
        std::sort(info.seats.begin(), info.seats.end());
        info.sides = bot_sides_of(s.room_teams, roster);
        if (listed.count(room.live_id()) != 0) info.live_id = room.live_id();
        info.places = room.map_places();
        out.push_back(std::move(info));
        ages.push_back(s.age_ms);
    }
    std::vector<size_t> order(out.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ages[a] > ages[b]; });          // (the oldest first)
    std::vector<BotMatchInfo> sorted;
    for (const size_t i : order) sorted.push_back(std::move(out[i]));
    return sorted;
}

bool RoomManager::stop_bot_match(const std::string& code, uint32_t now_ms) {
    const auto it = rooms_.find(code);
    if (it == rooms_.end() || !it->second->bots_only()) return false;
    const RoomState state = it->second->state();
    if (!(state == RoomState::Waiting || state == RoomState::Loading || state == RoomState::Running)) return false;
    it->second->close("stopped from the admin page", now_ms);
    return true;
}

size_t RoomManager::stop_bot_matches(uint32_t now_ms) {
    size_t stopped = 0;
    for (const auto& kv : rooms_) {
        if (stop_bot_match(kv.first, now_ms)) ++stopped;
    }
    return stopped;
}

// "" when a room can play the map of a list now, else why not (the store does not have it, the approved map is gone or is another file, this engine cannot play it with four seats, a limit of a room: OnlineLimits). `minutes`
// gets the map's own time limit and `places`, when asked, what a match on it takes. Loading a map and judging it is the costly part (the file is read, parsed and walked); the verdict is therefore kept by the file's name and
// the path, size and time of the file (and the hash of the approved record, which a new approval changes): the same file is not loaded again, one that was replaced is. A map that is not approved (any more) is refused
// before the verdict is looked at, so a take-down is never answered from it.
std::string RoomManager::bot_match_map_refusal(const std::string& file, bool approved, uint32_t& minutes, uint32_t* places) const {
    const ApprovedMap* player_map = approved ? store_.approved(file) : nullptr;
    if (approved && player_map == nullptr) return "it is not an approved map of this server";
    std::string path, why;
    uint64_t size = 0;
    if (!store_.locate(file, path, size, &why)) {
        bot_map_verdicts_.erase(file);
        return why;
    }
    if (player_map != nullptr && path != player_map->path) return "the file of that name is not the approved map";
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(path, ec);
    const int64_t stamp = ec ? 0 : static_cast<int64_t>(written.time_since_epoch().count());
    const uint64_t hash = player_map != nullptr ? player_map->hash : 0;
    const auto kept = bot_map_verdicts_.find(file);
    if (kept != bot_map_verdicts_.end() && kept->second.path == path && kept->second.size == size && kept->second.stamp == stamp && kept->second.hash == hash) {
        minutes = kept->second.minutes;
        if (places != nullptr) *places = kept->second.places;
        return kept->second.refusal;
    }
    ++bot_map_loads_;
    BotMapVerdict verdict;
    verdict.path = path;
    verdict.size = size;
    verdict.stamp = stamp;
    verdict.hash = hash;
    assets::LevelData level;
    if (!level.load_from_file(path, nullptr, assets::LevelLimits::for_play_extended())) {
        verdict.refusal = "this engine cannot play it";
    } else {
        const MapCounts counts = OnlineLimits::counts_of(level);
        const Term over = OnlineLimits::first_over(counts);
        if (over != Term::None) verdict.refusal = std::string("it ") + OnlineLimits::info(over).phrase + " for a room";
        else if (!level.validate(static_cast<uint8_t>(0x0F), assets::LevelLimits::for_play_extended()).playable) verdict.refusal = "it has no place for four seats";
        else {
            verdict.minutes = level.default_minutes;
            verdict.places = OnlineLimits::weight(counts.cells, counts.start_ants);
        }
    }
    if (bot_map_verdicts_.size() > 8192) bot_map_verdicts_.clear();          // (a bound for a store whose names come and go; bot_map_options() drops the ones that are gone)
    bot_map_verdicts_[file] = verdict;
    minutes = verdict.minutes;
    if (places != nullptr) *places = verdict.places;
    return verdict.refusal;
}

namespace {

ai::Level bot_level_of(BotLevelChoice choice, std::mt19937& rng) {
    switch (choice) {
        case BotLevelChoice::Easy: return ai::Level::Easy;
        case BotLevelChoice::Medium: return ai::Level::Medium;
        case BotLevelChoice::Hard: return ai::Level::Hard;
        case BotLevelChoice::RandomAny: {
            const uint32_t draw = static_cast<uint32_t>(rng() % 3);          // (the generator's own type is 64 bits on Linux)
            return draw == 0 ? ai::Level::Easy : draw == 1 ? ai::Level::Medium : ai::Level::Hard;
        }
        case BotLevelChoice::RandomMediumHard: break;
    }
    return rng() % 2 == 0 ? ai::Level::Medium : ai::Level::Hard;
}

}  // namespace

// The match: a map of the choice, the rules, two to four standard bots on random seats of the four (every set of that many seats as likely as any other), each at the level that the choice says (Medium or Hard by a draw, any
// of the three by a draw, or one level), a seed, and the teams: free for all or any way to make teams of the seats that play (roster_team_choices: with four, the three ways to split them in two, which on a map whose hills
// sit at the corners of a square are top against bottom, left against right and across the corners (TINY, SMALL, TREASURE and ISLANDS; MEDIUM's hills sit north, east, south and west and GAUNTLET's along a diagonal);
// with three, any two of them and the third alone; with two, free for all), each choice for the seats that play as likely as any other, or the split that the choice names (the bots are numbered from 1 in the order of their
// seats). What the choice leaves to a draw is drawn in the order that the default choices always drew it: the map's draw first, as it always was (the list is known after the rules), then the number of bots, each seat with
// its level, the seed, the teams, and the rules last; a choice that is fixed draws nothing, so the first match of a seed with the default choices is the same as before there were choices. The room has no person, plays at the
// normal speed (the maps' own time limits make it 6 to 20 minutes or more: its own guard is the longer of 25 minutes and the map's limit and five more), and ends by the rules or at its own time limit; the recording keeps it
// like any other match (the 30 seconds, the store's limits), and the live list shows it. Limits: at most kBotMatchesMax of these rooms at once and kBotPlacesMax of the places together, and none while the matches of people
// use most of the places (docs/SERVER_REPLAYS.md). The coin has no memory of the last match: two or three of a kind in a row come up as often as chance gives them.
bool RoomManager::start_bot_match(uint32_t now_ms, const BotMatchSpec& choice, std::string& code, std::string& why, BotRefusal* refusal, uint8_t* mode) {
    code.clear();
    if (refusal != nullptr) *refusal = BotRefusal::None;
    const auto refuse = [&](BotRefusal kind, std::string text) {
        why = std::move(text);
        if (refusal != nullptr) *refusal = kind;
        return false;
    };
    if (replays_ == nullptr) return refuse(BotRefusal::NoReplays, "the server keeps no replays");
    if (bot_match_count() >= kBotMatchesMax) return refuse(BotRefusal::Cap, std::to_string(kBotMatchesMax) + " matches of computer players go already");
    if (people_busy()) return refuse(BotRefusal::PeopleBusy, places_words(public_places(), limits_.demo_rooms));
    if (replays_->bot_saves_left() <= bot_match_count()) return refuse(BotRefusal::Budget, "the store keeps at most " + std::to_string(replays_->config().max_bot_saves_per_hour) + " recordings of matches of computer players an hour, and that many are kept or about to be: a match that would not be kept is not played");
    size_t fixed_at = 0;
    const bool named = choice.map != kBotMapRotation && choice.map != kBotMapOriginal && choice.map != kBotMapPlayers;
    if (named && choice.rules == BotRules::Kills187 && choice.map == kIslands) return refuse(BotRefusal::Islands187, "ISLANDS is not played in 187: its water joins no two hills, so nobody could be struck there");
    if (!bot_rng_seeded_) seed_bot_matches(0);
    RoomSpec spec = default_spec();
    const uint32_t map_draw = static_cast<uint32_t>(bot_rng_());                                  // (the first draw of the match, as the map's always was: what it picks from is known after the rules)
    spec.players = choice.bots != 0 ? choice.bots : static_cast<uint8_t>(2 + bot_rng_() % 3);
    uint32_t need = spec.players;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {              // (a seat is taken with the chance need / seats left)
        if (bot_rng_() % (sim::MAX_PLAYERS - seat) >= need) continue;
        ai::BotSpec bot;
        bot.seat = seat;
        bot.level = bot_level_of(choice.levels[spec.bots.size()], bot_rng_);
        spec.bots.push_back(bot);
        --need;
    }
    spec.seed = (bot_rng_() & 0x7FFFFFFFu) + 1u;
    spec.has_seed = true;
    uint8_t roster = 0;
    for (const ai::BotSpec& bot : spec.bots) roster = static_cast<uint8_t>(roster | (1u << bot.seat));
    const std::vector<sim::StartTeams> team_choices = sim::roster_team_choices(roster);
    if (choice.teams == BotTeamsChoice::Random) {
        spec.teams = team_choices[bot_rng_() % team_choices.size()];            // (the room starts with them: Room::update, sim::start_teams_for; the replay's head and the live match carry them)
    } else if (choice.teams == BotTeamsChoice::Split) {
        const std::vector<BotSplit>& splits = bot_splits(spec.players);
        if (choice.split >= 0 && static_cast<size_t>(choice.split) < splits.size()) {
            const BotSplit& split = splits[static_cast<size_t>(choice.split)];
            spec.teams.set = true;
            spec.teams.a = spec.bots[split.pair[0]].seat;
            spec.teams.b = spec.bots[split.pair[1]].seat;
        }
    }
    const bool islands_only = named && choice.map == kIslands;                  // (ISLANDS is played in the original's game only: a match that is dealt its rules by a coin is dealt that)
    if (choice.rules == BotRules::Random && !islands_only) spec.mode = static_cast<uint8_t>(bot_rng_() % 2 == 0 ? sim::GameMode::HighestScore : sim::GameMode::Kills187);      // (the coin: the room's mode reaches the lobby, the start data, the engine of the referee and the replay's head)
    else spec.mode = static_cast<uint8_t>(choice.rules == BotRules::Kills187 ? sim::GameMode::Kills187 : sim::GameMode::HighestScore);
    const bool kills_187 = spec.mode == static_cast<uint8_t>(sim::GameMode::Kills187);
    std::vector<BotMatchMap> listed;                                       // (the maps of the choice, in the list's order)
    if (named) {
        const bool fixed = is_fixed_map(choice.map, fixed_at);
        if (!fixed && store_.approved(choice.map) == nullptr) return refuse(BotRefusal::NoMap, "that map is not one that matches of computer players play");
        listed.push_back({choice.map.c_str(), !fixed});
    } else if (choice.map == kBotMapRotation) {
        listed = bot_match_maps(kills_187);
    } else if (choice.map == kBotMapOriginal) {
        for (size_t i = 0; i < kOriginalMaps; ++i) {
            if (!(kills_187 && std::string(bot_match_maps(false)[i].file) == kIslands)) listed.push_back(bot_match_maps(false)[i]);
        }
    } else {
        for (const ApprovedMap& map : store_.approved_maps()) {
            size_t at = 0;
            if (!is_fixed_map(map.name, at)) listed.push_back({map.name.c_str(), true});
        }
    }
    std::vector<std::string> maps;                                         // (the maps of the choice that a room can play now, in the list's order)
    std::vector<uint32_t> minutes;
    std::vector<uint32_t> weights;
    std::string named_refusal;
    for (const BotMatchMap& entry : listed) {
        uint32_t limit = 0, places = 1;
        std::string refusal_text = bot_match_map_refusal(entry.file, entry.approved, limit, &places);
        if (refusal_text.empty() && !named && limit > kBotRandomMaxMinutes) refusal_text = "it plays " + std::to_string(limit) + " minutes, and a random pick keeps to " + std::to_string(kBotRandomMaxMinutes);
        if (refusal_text.empty()) {
            maps.push_back(entry.file);
            minutes.push_back(limit);
            weights.push_back(places);
            bot_maps_skipped_.erase(entry.file);
        } else if (named) {
            named_refusal = refusal_text;
        } else if (bot_maps_skipped_.insert(entry.file).second) {          // (named once, and again if the map comes back and goes: not at every match)
            bot_notes_.push_back(std::string("bot match: the map ") + entry.file + " is skipped (" + refusal_text + ")");
        }
    }
    if (maps.empty()) return refuse(BotRefusal::NoMap, named ? "that map cannot be played now (" + named_refusal + ")" : "the server has none of the maps of this choice");
    const size_t picked = map_draw % maps.size();
    spec.map = maps[picked];
    const uint32_t weight = weights[picked];
    if (bot_places() + weight > kBotPlacesMax) return refuse(BotRefusal::Places, "the matches of computer players would take " + std::to_string(bot_places() + weight) + " of the " + std::to_string(kBotPlacesMax) + " places that they may have");
    spec.bots_only = true;
    spec.early_start = false;
    spec.reconnect = false;                                                // (it holds no seat, so no restart record: a server that stops ends it)
    spec.wait_ms = 30u * 1000u;
    spec.run_ms = std::max<uint32_t>(25u, minutes[picked] + 5u) * 60u * 1000u;      // (this is only the room's own guard: it must not end a match before the map's own limit, 6 to 20 minutes on the maps of these lists)
    spec.keep_ms = 30u * 1000u;
    spec.start_hold_ms = bot_gap_pending_ && !net::time_reached(now_ms, next_bot_start_ms_) ? next_bot_start_ms_ - now_ms : 0u;      // (one after another, kBotStartGapMs apart)
    const uint8_t made_mode = spec.mode;
    const uint32_t hold = spec.start_hold_ms;
    const CreateResult made = create_room(std::move(spec), now_ms);
    if (!made.ok) return refuse(BotRefusal::Failed, made.error);
    bot_gap_pending_ = true;
    next_bot_start_ms_ = now_ms + hold + kBotStartGapMs;
    code = made.code;
    if (mode != nullptr) *mode = made_mode;
    return true;
}

}  // namespace ants::server

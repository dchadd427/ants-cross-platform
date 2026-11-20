#include "ants_server/bot_match_spec.hpp"

#include <cstring>

#include "ants_net/protocol.hpp"

namespace ants::server {

namespace {

struct Word {
    const char* text;
    BotLevelChoice level;
};
const Word kLevelWords[] = {{"rmh", BotLevelChoice::RandomMediumHard}, {"easy", BotLevelChoice::Easy}, {"medium", BotLevelChoice::Medium}, {"hard", BotLevelChoice::Hard}, {"random", BotLevelChoice::RandomAny}};

const char* level_word(BotLevelChoice level) {
    for (const Word& w : kLevelWords) {
        if (w.level == level) return w.text;
    }
    return "rmh";
}

const char* rules_word(BotRules rules) { return rules == BotRules::HighestScore ? "highest-score" : rules == BotRules::Kills187 ? "187" : "random"; }
const char* teams_word(BotTeamsChoice teams) { return teams == BotTeamsChoice::FreeForAll ? "ffa" : teams == BotTeamsChoice::Split ? "split" : "random"; }

// A number from 2 to 4 in the page's words: "2", "3", "4", or the number itself
int bots_from(const ctl::JsonValue& v) {
    if (v.is_int()) return v.as_int_or(0) >= 2 && v.as_int_or(0) <= 4 ? static_cast<int>(v.as_int_or(0)) : -1;
    if (v.is_string() && v.str().size() == 1 && v.str()[0] >= '2' && v.str()[0] <= '4') return v.str()[0] - '0';
    return -1;
}

}  // namespace

// The lists that a server starts with (docs/SERVER_REPLAYS.md "The lists"): the original's game has the six maps of the original game and three of the library, 187 has five of the six (ISLANDS is a map where nobody can be struck) and an approved map of the players
const std::vector<BotRotationEntry>& bot_rotation_defaults(bool kills_187) {
    static const std::vector<BotRotationEntry> kHighestScore = {{"TINY.LVL", false},   {"SMALL.LVL", false},  {"MEDIUM.LVL", false},   {"GAUNTLET.LVL", false}, {"TREASURE.LVL", false},
                                                                {"ISLANDS.LVL", false}, {"POPcOrN.lvl", false}, {"HELLRAISER.lvl", false}, {"Bombz Away.lvl", false}};
    static const std::vector<BotRotationEntry> k187 = {{"TINY.LVL", false}, {"SMALL.LVL", false}, {"MEDIUM.LVL", false}, {"GAUNTLET.LVL", false}, {"TREASURE.LVL", false}, {"Fight Island.lvl", true}};
    return kills_187 ? k187 : kHighestScore;
}

const std::vector<BotSplit>& bot_splits(uint8_t bots) {
    static const std::vector<BotSplit> kFour = {{"12-34", {0, 1}}, {"13-24", {0, 2}}, {"14-23", {0, 3}}};
    static const std::vector<BotSplit> kThree = {{"1-23", {1, 2}}, {"2-13", {0, 2}}, {"3-12", {0, 1}}};
    static const std::vector<BotSplit> kNone;
    return bots == 4 ? kFour : bots == 3 ? kThree : kNone;
}

bool bot_map_name_shape_ok(const std::string& name) {
    if (name.empty() || name.size() > 96 || !net::valid_map_name(name)) return false;
    if (name.size() < 5) return false;
    const std::string tail = name.substr(name.size() - 4);
    return (tail[0] == '.') && (tail[1] == 'l' || tail[1] == 'L') && (tail[2] == 'v' || tail[2] == 'V') && (tail[3] == 'l' || tail[3] == 'L');
}

bool parse_bot_what(const ctl::JsonValue& json, BotWhat& out, std::string& error, uint32_t max_count) {
    BotWhat what;
    if (!json.is_object()) {
        error = "bad_count";
        return false;
    }
    const ctl::JsonValue& count = json.get("count");
    if (!count.is_int() || count.as_int_or(0) < 1 || count.as_int_or(0) > static_cast<int64_t>(max_count)) {
        error = "bad_count";
        return false;
    }
    what.count = static_cast<uint32_t>(count.as_int_or(1));

    if (json.has("every_s")) {                                                              // (left out: every match at once, as a schedule that was kept before there was a spacing)
        const ctl::JsonValue& every = json.get("every_s");
        if (!every.is_int() || every.as_int_or(-1) < 0 || every.as_int_or(-1) > static_cast<int64_t>(kBotSpacingMaxS) || every.as_int_or(-1) % static_cast<int64_t>(kBotSpacingStepS) != 0) {
            error = "bad_spacing";
            return false;
        }
        what.every_s = static_cast<uint32_t>(every.as_int_or(0));
    }

    const ctl::JsonValue& map = json.get("map");
    if (!map.is_string() || !(map.str() == kBotMapRotation || map.str() == kBotMapOriginal || map.str() == kBotMapPlayers || bot_map_name_shape_ok(map.str()))) {
        error = "bad_map";
        return false;
    }
    what.spec.map = map.str();

    const ctl::JsonValue& rules = json.get("rules");
    if (!rules.is_string() || !(rules.str() == "highest-score" || rules.str() == "187" || rules.str() == "random")) {
        error = "bad_rules";
        return false;
    }
    what.spec.rules = rules.str() == "highest-score" ? BotRules::HighestScore : rules.str() == "187" ? BotRules::Kills187 : BotRules::Random;

    const ctl::JsonValue& bots = json.get("bots");
    if (bots.is_string() && bots.str() == "random") {
        what.spec.bots = 0;
    } else {
        const int n = bots_from(bots);
        if (n < 0) {
            error = "bad_bots";
            return false;
        }
        what.spec.bots = static_cast<uint8_t>(n);
    }

    const ctl::JsonValue& levels = json.get("levels");
    if (!levels.is_array() || levels.size() > 4) {
        error = "bad_levels";
        return false;
    }
    for (size_t i = 0; i < levels.size(); ++i) {
        bool known = false;
        if (levels.at(i).is_string()) {
            for (const Word& w : kLevelWords) {
                if (levels.at(i).str() == w.text) {
                    what.spec.levels[i] = w.level;
                    known = true;
                }
            }
        }
        if (!known) {
            error = "bad_levels";
            return false;
        }
    }

    const ctl::JsonValue& teams = json.get("teams");
    if (!teams.is_string() || !(teams.str() == "ffa" || teams.str() == "split" || teams.str() == "random")) {
        error = "bad_teams";
        return false;
    }
    what.spec.teams = teams.str() == "ffa" ? BotTeamsChoice::FreeForAll : teams.str() == "split" ? BotTeamsChoice::Split : BotTeamsChoice::Random;
    if (what.spec.teams == BotTeamsChoice::Split) {
        const std::vector<BotSplit>& splits = bot_splits(what.spec.bots);        // (a split needs a number of bots: with 2, or with a draw of the number, there is none)
        const ctl::JsonValue& split = json.get("split");
        bool found = false;
        for (size_t i = 0; i < splits.size() && split.is_string(); ++i) {
            if (split.str() == splits[i].text) {
                what.spec.split = static_cast<int>(i);
                found = true;
            }
        }
        if (!found) {
            error = "bad_split";
            return false;
        }
    }
    out = what;
    return true;
}

ctl::JsonValue bot_what_json(const BotWhat& what) {
    ctl::JsonValue o = ctl::JsonValue::make_object();
    o.set("count", ctl::JsonValue::make_int(static_cast<int64_t>(what.count)));
    o.set("every_s", ctl::JsonValue::make_int(static_cast<int64_t>(what.every_s)));
    o.set("map", ctl::JsonValue::make_string(what.spec.map));
    o.set("rules", ctl::JsonValue::make_string(rules_word(what.spec.rules)));
    o.set("bots", ctl::JsonValue::make_string(what.spec.bots == 0 ? std::string("random") : std::to_string(what.spec.bots)));
    ctl::JsonValue levels = ctl::JsonValue::make_array();
    for (size_t i = 0; i < what.spec.levels.size(); ++i) levels.push_back(ctl::JsonValue::make_string(level_word(what.spec.levels[i])));          // (four, whatever the number of bots: a page that raises it finds a level for each)
    o.set("levels", std::move(levels));
    o.set("teams", ctl::JsonValue::make_string(teams_word(what.spec.teams)));
    const std::vector<BotSplit>& splits = bot_splits(what.spec.bots);
    o.set("split", ctl::JsonValue::make_string(what.spec.teams == BotTeamsChoice::Split && what.spec.split >= 0 && static_cast<size_t>(what.spec.split) < splits.size() ? splits[static_cast<size_t>(what.spec.split)].text : ""));
    return o;
}

std::string bot_spacing_words(uint32_t every_s) {
    const uint32_t whole = every_s / 60, rest = every_s % 60;
    return "one every " + std::to_string(whole) + (rest != 0 ? ".5" : "") + (whole == 1 && rest == 0 ? " minute" : " minutes");
}

std::string bot_what_words(const BotWhat& what) {
    std::string map = what.spec.map == kBotMapRotation ? "rotation map" : what.spec.map == kBotMapOriginal ? "original map" : what.spec.map == kBotMapPlayers ? "players' map" : what.spec.map;
    std::string text = std::to_string(what.count) + (what.count == 1 ? " match" : " matches") + ", " + map + ", ";
    text += what.spec.rules == BotRules::Random ? "random rules" : what.spec.rules == BotRules::Kills187 ? "rules 187" : "rules highest score";
    text += ", " + (what.spec.bots == 0 ? std::string("2 to 4") : std::to_string(what.spec.bots)) + " bots";
    if (what.count > 1 && what.every_s != 0) text += ", " + bot_spacing_words(what.every_s);
    return text;
}

}  // namespace ants::server

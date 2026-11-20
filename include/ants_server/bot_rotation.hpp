#pragma once

// The two lists of the rotation (docs/SERVER_REPLAYS.md "The lists"): the maps that "random:rotation" draws from, one list for each rules. A server starts with the lists of bot_rotation_defaults() (bot_match_spec.hpp); an admin
// edits them on the admin page (BotMatches, bot_matches.hpp) and the server keeps what was edited in the table bot_rotation (migration 10: src/ants_server/db_schema.cpp) and in memory. A rules with no row has the defaults, so
// nothing is written at the first start and "Back to the defaults" deletes the row. A server without a database keeps the lists in memory only. One thread, like everything else of the server.

#include <cstdint>
#include <string>
#include <vector>

#include "ants_ctl/json.hpp"
#include "ants_server/bot_match_spec.hpp"

namespace ants::db {
class Database;
}

namespace ants::server {

/// The word of a rules on the wire and in the table: "highest-score" or "187"
const char* bot_rotation_word(bool kills_187);
/// The rules of a word, false for any other word
bool bot_rotation_rules_of(const std::string& word, bool& kills_187);

/// A list as the table keeps it: the JSON text of an array of {"file", "approved"}
std::string bot_rotation_json(const std::vector<BotRotationEntry>& list);
/// The list that such a text holds. False for anything else: not JSON, not an array, an entry that is no object with a file name of the shape of bot_map_name_shape_ok and a bool `approved`, a name twice, no map, more than
/// kBotRotationMax maps.
bool bot_rotation_parse(const std::string& text, std::vector<BotRotationEntry>& out);
/// Whether two lists are the same maps in the same order (the file names and the approved flags)
bool bot_rotation_same(const std::vector<BotRotationEntry>& a, const std::vector<BotRotationEntry>& b);

class BotRotationStore {
public:
    /// `database` may be null: the lists then live in memory only
    explicit BotRotationStore(db::Database* database);

    /// Reads the rows (a row that cannot be read is left out, so that rules has its defaults, and is told in the notices). False with a sentence when the table cannot be read.
    bool open(std::string& why);
    /// The list of the rules now: what an admin saved, or the defaults
    std::vector<BotRotationEntry> list(bool kills_187) const;
    /// Whether an admin saved a list for these rules that is not the defaults
    bool edited(bool kills_187) const;
    /// Keeps `maps` as the list of the rules (the caller has checked it: bot_rotation_parse's rules, and every map playable). A list that is the defaults is kept as no row. False with the error code `db` when the database
    /// refuses; nothing is changed then.
    bool set(bool kills_187, const std::vector<BotRotationEntry>& maps, int64_t now_s, std::string& error);
    /// The defaults again: the row is deleted. False with `db` when the database refuses.
    bool reset(bool kills_187, std::string& error);

    std::vector<std::string> take_notices();

private:
    bool database_on() const;

    db::Database* db_;
    bool has_[2]{false, false};
    std::vector<BotRotationEntry> lists_[2];
    std::vector<std::string> notices_;
};

}  // namespace ants::server

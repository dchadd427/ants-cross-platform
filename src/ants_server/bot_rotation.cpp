#include "ants_server/bot_rotation.hpp"

#include <set>

#include "ants_db/database.hpp"

namespace ants::server {

const char* bot_rotation_word(bool kills_187) { return kills_187 ? "187" : "highest-score"; }

bool bot_rotation_rules_of(const std::string& word, bool& kills_187) {
    if (word == "highest-score") {
        kills_187 = false;
        return true;
    }
    if (word == "187") {
        kills_187 = true;
        return true;
    }
    return false;
}

std::string bot_rotation_json(const std::vector<BotRotationEntry>& list) {
    ctl::JsonValue a = ctl::JsonValue::make_array();
    for (const BotRotationEntry& e : list) {
        ctl::JsonValue o = ctl::JsonValue::make_object();
        o.set("file", ctl::JsonValue::make_string(e.file));
        o.set("approved", ctl::JsonValue::make_bool(e.approved));
        a.push_back(std::move(o));
    }
    return ctl::to_json(a);
}

bool bot_rotation_parse(const std::string& text, std::vector<BotRotationEntry>& out) {
    ctl::JsonValue json;
    if (!ctl::parse_json(text, json, nullptr) || !json.is_array() || json.size() == 0 || json.size() > kBotRotationMax) return false;
    std::vector<BotRotationEntry> list;
    std::set<std::string> seen;
    for (size_t i = 0; i < json.size(); ++i) {
        const ctl::JsonValue& o = json.at(i);
        if (!o.is_object() || !o.get("file").is_string() || !o.get("approved").is_bool() || !bot_map_name_shape_ok(o.get("file").str()) || !seen.insert(o.get("file").str()).second) return false;
        list.push_back({o.get("file").str(), o.get("approved").as_bool_or(false)});
    }
    out = std::move(list);
    return true;
}

bool bot_rotation_same(const std::vector<BotRotationEntry>& a, const std::vector<BotRotationEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].file != b[i].file || a[i].approved != b[i].approved) return false;
    }
    return true;
}

BotRotationStore::BotRotationStore(db::Database* database) : db_(database) {}

bool BotRotationStore::database_on() const { return db_ != nullptr && db_->is_open(); }

std::vector<std::string> BotRotationStore::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

bool BotRotationStore::open(std::string& why) {
    has_[0] = has_[1] = false;
    lists_[0].clear();
    lists_[1].clear();
    if (!database_on()) return true;
    db::Statement st;
    if (!db_->prepare(st, "SELECT rules, maps FROM bot_rotation").ok()) {
        why = "the database has no table of the rotation";
        return false;
    }
    while (st.step() == db::Statement::Step::Row) {
        bool kills_187 = false;
        std::vector<BotRotationEntry> list;
        if (!bot_rotation_rules_of(st.column_text(0), kills_187) || !bot_rotation_parse(st.column_text(1), list)) {
            notices_.push_back("bot rotation: a row of the table could not be read: its rules have the default list");
            continue;
        }
        has_[kills_187 ? 1 : 0] = true;
        lists_[kills_187 ? 1 : 0] = std::move(list);
    }
    return true;
}

std::vector<BotRotationEntry> BotRotationStore::list(bool kills_187) const {
    const size_t at = kills_187 ? 1 : 0;
    return has_[at] ? lists_[at] : bot_rotation_defaults(kills_187);
}

bool BotRotationStore::edited(bool kills_187) const { return has_[kills_187 ? 1 : 0]; }

bool BotRotationStore::set(bool kills_187, const std::vector<BotRotationEntry>& maps, int64_t now_s, std::string& error) {
    if (bot_rotation_same(maps, bot_rotation_defaults(kills_187))) return reset(kills_187, error);          // (the defaults are no row)
    const size_t at = kills_187 ? 1 : 0;
    if (database_on()) {
        db::Statement st;
        if (!db_->prepare(st, "INSERT OR REPLACE INTO bot_rotation (rules, maps, updated_at) VALUES (?, ?, ?)").ok() || !st.bind_text(1, bot_rotation_word(kills_187)).ok() || !st.bind_text(2, bot_rotation_json(maps)).ok() ||
            !st.bind_int64(3, now_s).ok() || st.step() != db::Statement::Step::Done) {
            error = "db";
            return false;
        }
    }
    has_[at] = true;
    lists_[at] = maps;
    return true;
}

bool BotRotationStore::reset(bool kills_187, std::string& error) {
    const size_t at = kills_187 ? 1 : 0;
    if (database_on()) {
        db::Statement st;
        if (!db_->prepare(st, "DELETE FROM bot_rotation WHERE rules = ?").ok() || !st.bind_text(1, bot_rotation_word(kills_187)).ok() || st.step() != db::Statement::Step::Done) {
            error = "db";
            return false;
        }
    }
    has_[at] = false;
    lists_[at].clear();
    return true;
}

}  // namespace ants::server

// The one-time copy of the game server's match history into the history service's file (history_service.hpp import_game_history, docs/HISTORY_SERVICE.md "The first start").
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "ants_history/history_service.hpp"
#include "ants_server/history_action.hpp"

namespace ants::history {

namespace {

// A column of a table that is copied: its name, and what it holds (the columns of the history are text or whole numbers, a few of them may be NULL)
struct Column {
    const char* name;
    char kind;                                         // 'i' integer, 't' text
    bool nullable;
    bool newer{false};                                 // a column of a later migration: a game database that does not have it yet (it is at an older schema) is copied with a 0 in its place
};

struct Table {
    const char* name;
    std::vector<Column> columns;
    bool optional;                                     // a game database of an older schema may not have it (the map of deaths came with version 7, the action of a match with version 11, the likes with version 12)
    bool (*accepts)(db::Statement& row) = nullptr;     // null: every row is copied; else a row that it refuses is left out (the same check that put() makes of a text before it keeps it)
};

// A row of history_action is copied only when its text is a field of this version and within its cap (history_store.cpp put())
bool action_row_ok(db::Statement& row) {
    const std::string text = row.column_text(1);
    server::ActionView view;
    return text.size() <= server::kActionMaxBytes && server::parse_history_action(text, view);
}

// The tables of the match history, in the order that foreign keys need (a seat after its match). The columns are those of the migrations in src/ants_server/db_schema.cpp: a column that a later migration adds
// to the game server's table is not copied until it is added here and to history_db_migrations() together.
const std::vector<Table>& tables() {
    static const std::vector<Table> list = {
        {"history_matches",
         {{"id", 't', false}, {"ended", 'i', false}, {"map", 't', false}, {"game", 't', false}, {"turns", 'i', false}, {"finished", 'i', false}, {"quitter", 'i', true}, {"mode", 't', false},
          {"format", 't', false}, {"result", 't', false}, {"computers_only", 'i', false}, {"best_score", 'i', false}, {"best_kills", 'i', false}, {"best_hatched", 'i', false}, {"search", 't', false},
          {"rows_json", 't', false}, {"details", 't', true}, {"counted_at", 'i', false}, {"plays", 'i', false, true}, {"likes", 'i', false, true}},
         false},
        {"history_seats",
         {{"match_id", 't', false}, {"seat", 'i', false}, {"name", 't', false}, {"bot", 't', false}, {"score", 'i', false}, {"killed", 'i', false}, {"lost", 'i', false}, {"hatched", 'i', false},
          {"bombs_planted", 'i', false}, {"bombs_defused", 'i', false}, {"fires_lit", 'i', false}, {"left_early", 'i', false}, {"winner", 'i', false}},
         false},
        {"history_markers", {{"id", 't', false}, {"deleted_at", 'i', false}}, false},
        {"history_deaths", {{"id", 't', false}, {"map", 't', false}, {"hash", 't', false}, {"ended", 'i', false}, {"w", 'i', false}, {"h", 'i', false}, {"n", 'i', false}, {"deaths", 't', false}}, true},
        {"history_action", {{"id", 't', false}, {"action", 't', false}}, true, action_row_ok},
        {"map_plays", {{"map", 't', false}, {"hash", 't', false}, {"plays", 'i', false}, {"last_played", 'i', false}}, false},
        {"replay_likes", {{"id", 't', false}, {"ref", 't', false}}, true},
    };
    return list;
}

bool has_table(db::Database& d, const char* name, std::string& why) {
    db::Statement st;
    db::Error e = d.prepare(st, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = ?1");
    if (e.ok()) e = st.bind_text(1, name);
    if (!e.ok() || st.step() != db::Statement::Step::Row) {
        why = e.ok() ? st.error().message : e.message;
        return false;
    }
    return st.column_int64(0) > 0;
}

int64_t count_rows(db::Database& d, const std::string& table, db::Error& e) {
    db::Statement st;
    e = d.prepare(st, "SELECT count(*) FROM " + table);
    if (!e.ok()) return 0;
    if (st.step() != db::Statement::Step::Row) {
        e = st.error();
        return 0;
    }
    return st.column_int64(0);
}

bool has_column(db::Database& d, const char* table, const char* column) {
    db::Statement st;
    db::Error e = d.prepare(st, "SELECT count(*) FROM pragma_table_info(?1) WHERE name = ?2");
    if (e.ok()) e = st.bind_text(1, table);
    if (e.ok()) e = st.bind_text(2, column);
    return e.ok() && st.step() == db::Statement::Step::Row && st.column_int64(0) > 0;
}

// The list of columns: the names (the source's: `from` says which database the names are read from, and a column that it does not have yet is read as 0) or the numbered marks of an INSERT
std::string joined(const Table& t, bool marks, db::Database* from = nullptr) {
    std::string out;
    for (size_t k = 0; k < t.columns.size(); ++k) {
        if (k > 0) out += ", ";
        if (marks) out += "?" + std::to_string(k + 1);
        else if (from != nullptr && t.columns[k].newer && !has_column(*from, t.name, t.columns[k].name)) out += "0";
        else out += t.columns[k].name;
    }
    return out;
}

// All the rows of one table, from the source to the target (the caller holds a read transaction on the first and a write transaction on the second)
db::Error copy_table(db::Database& from, db::Database& to, const Table& t, size_t& rows, size_t& refused) {
    rows = 0;
    refused = 0;
    db::Statement select;
    db::Error e = from.prepare(select, "SELECT " + joined(t, false, &from) + " FROM " + t.name);
    if (!e.ok()) return e;
    db::Statement insert;
    e = to.prepare(insert, std::string("INSERT INTO ") + t.name + " (" + joined(t, false) + ") VALUES (" + joined(t, true) + ")");
    if (!e.ok()) return e;
    for (;;) {
        const db::Statement::Step step = select.step();
        if (step == db::Statement::Step::Failed) return select.error();
        if (step == db::Statement::Step::Done) return db::Error();
        if (t.accepts && !t.accepts(select)) {
            ++refused;
            continue;
        }
        for (size_t k = 0; k < t.columns.size(); ++k) {
            const int column = static_cast<int>(k);
            const int at = column + 1;
            if (select.column_is_null(column)) {
                if (!t.columns[k].nullable) return db::Error{1, std::string(t.name) + "." + t.columns[k].name + " is NULL in the game server's database"};
                e = insert.bind_null(at);
            } else if (t.columns[k].kind == 'i') {
                e = insert.bind_int64(at, select.column_int64(column));
            } else {
                e = insert.bind_text(at, select.column_text(column));
            }
            if (!e.ok()) return e;
        }
        if (insert.step() == db::Statement::Step::Failed) return insert.error();
        e = insert.reset();
        if (!e.ok()) return e;
        ++rows;
    }
}

// A read transaction on the source, so that its tables are copied as they were at one moment (the game server goes on writing)
struct Snapshot {
    db::Database& d;
    bool open{false};
    explicit Snapshot(db::Database& database) : d(database) { open = d.exec("BEGIN").ok(); }
    ~Snapshot() {
        if (open) d.exec("ROLLBACK");
    }
};

}  // namespace

bool import_game_history(db::Database& target, const std::string& source_path, ImportReport& report, std::string& why) {
    report = ImportReport();
    why.clear();
    // Done before? One row of `meta` says so.
    {
        db::Statement st;
        db::Error e = target.prepare(st, "SELECT value FROM meta WHERE key = 'history_import'");
        if (e.ok() && st.step() == db::Statement::Step::Row) {
            report.already_done = true;
            report.note = "history import: done at an earlier start (" + st.column_text(0) + "); nothing is copied";
            return true;
        }
        if (!e.ok()) {
            why = "the service's database cannot be read: " + e.message;
            return false;
        }
    }
    // The service's own file may have records that it counted itself: the play counts of the maps are sums and cannot be told apart from the game server's, so nothing is copied (the operator starts with a new file)
    {
        db::Error e;
        int64_t own = 0;
        for (const Table& t : tables()) {
            own += count_rows(target, t.name, e);
            if (!e.ok()) {
                why = "the service's database cannot be read: " + e.message;
                return false;
            }
        }
        if (own > 0) {
            report.not_empty = true;
            report.note = "history import: not done: the service's own database already holds " + std::to_string(own) + " row(s) of history and was not made by an import (start with a new file to copy the game server's history)";
            return true;
        }
    }
    std::error_code ec;
    if (std::filesystem::equivalent(std::filesystem::u8path(source_path), std::filesystem::u8path(target.path()), ec)) {
        why = "the game server's database and the service's database are the same file";
        return false;
    }
    db::Database source;
    db::OpenOptions options;
    options.create = false;
    options.read_only = true;
    options.busy_timeout_ms = 2000;
    db::Error e = source.open(source_path, options);
    if (!e.ok()) {
        why = "the game server's database cannot be read: " + e.message;
        return false;
    }
    {
        Snapshot snapshot(source);
        if (!snapshot.open) {
            why = "the game server's database cannot be read: it would not start a read";
            return false;
        }
        std::string table_why;
        for (const Table& t : tables()) {
            const bool there = has_table(source, t.name, table_why);
            if (!table_why.empty()) {
                why = "the game server's database cannot be read: " + table_why;
                return false;
            }
            if (!there && !t.optional) {
                why = std::string("the game server's database has no table ") + t.name + " (it is not a database of this game's server, or an old one)";
                return false;
            }
        }
        db::Transaction tx(target);
        if (!tx.error().ok()) {
            why = "the service's database cannot be written: " + tx.error().message;
            return false;
        }
        ImportReport counted;
        for (const Table& t : tables()) {
            if (!has_table(source, t.name, table_why)) continue;
            size_t rows = 0, refused = 0;
            e = copy_table(source, target, t, rows, refused);
            if (!e.ok()) {
                why = std::string("copying ") + t.name + " failed (nothing was copied): " + e.message;
                return false;
            }
            const std::string name = t.name;
            if (name == "history_matches") counted.matches = rows;
            else if (name == "history_seats") counted.seats = rows;
            else if (name == "history_markers") counted.markers = rows;
            else if (name == "history_deaths") counted.deaths = rows;
            else if (name == "history_action") {
                counted.action = rows;
                counted.action_refused = refused;
            }
            else if (name == "replay_likes") counted.replay_likes = rows;
            else counted.map_plays = rows;
        }
        const std::string summary = std::to_string(counted.matches) + " match(es), " + std::to_string(counted.seats) + " seat(s), " + std::to_string(counted.markers) + " marker(s), " + std::to_string(counted.deaths) +
                                    " map(s) of deaths, " + std::to_string(counted.action) + " action(s)" + (counted.action_refused > 0 ? " (" + std::to_string(counted.action_refused) + " refused: not a field of this version)" : std::string()) + ", " + std::to_string(counted.map_plays) + " play count(s), " + std::to_string(counted.replay_likes) + " like(s)";
        db::Statement mark;
        e = target.prepare(mark, "INSERT INTO meta (key, value) VALUES ('history_import', strftime('%Y-%m-%dT%H:%M:%SZ', 'now') || ' ' || ?1)");
        if (e.ok()) e = mark.bind_text(1, summary);
        if (e.ok() && mark.step() == db::Statement::Step::Failed) e = mark.error();
        if (e.ok()) e = tx.commit();
        if (!e.ok()) {
            why = "the copy could not be committed (nothing was copied): " + e.message;
            return false;
        }
        report = counted;
        report.imported = true;
        report.note = "history import: copied " + summary + " from the game server's database";
    }
    return true;
}

}  // namespace ants::history

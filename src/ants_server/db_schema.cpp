// The migrations of the server's database (docs/SERVER.md "The database"). Version 1 is the foundation (`meta`); version 2 holds the match history (docs/REPLAYS.md "The match history"); version 3 the play counts of the maps.
#include "ants_server/db_schema.hpp"

namespace ants::server {

namespace {

// Version 2. history_matches: one row per match, with a column for everything that GET /history filters or sorts by, and an index for each of them (the list is "newest first" unless it says otherwise, so
// every index ends in the end time and the id: the order of a tie). `result` is "decided", "draw" or "cut" (a player left, or the time ran out), `mode` the room's game mode, `best_score` / `best_kills` /
// `best_hatched` the largest of the seats' numbers (the keys of the sorts "score", "kills" and "hatched"; in mode 187 `best_score` is the kills too), `search` the lower-case words that the list's `q`
// looks in (the map's name and what each seat shows, one per line), `rows_json` the ranked rows, and `details` is reserved for what a later version keeps of a match (JSON text, NULL now). `turns` has two indexes: the sort "long" reads the first one backwards (longest first, the newer first among equal ones), the
// sort "short" the second forwards (shortest first, the newer first among equal ones).
// history_seats: one row per seat that played. history_markers: a match that the owner deleted while its recording is still kept (so that it is not counted again), until the recording goes.
const char* const kHistorySql =
    "CREATE TABLE history_matches ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " ended INTEGER NOT NULL,"
    " map TEXT NOT NULL,"
    " game TEXT NOT NULL,"
    " turns INTEGER NOT NULL CHECK (turns >= 0),"
    " finished INTEGER NOT NULL CHECK (finished IN (0, 1)),"
    " quitter INTEGER CHECK (quitter IS NULL OR (quitter >= 0 AND quitter <= 3)),"
    " mode TEXT NOT NULL CHECK (mode IN ('highest-score', '187')),"
    " format TEXT NOT NULL,"
    " result TEXT NOT NULL CHECK (result IN ('decided', 'draw', 'cut')),"
    " computers_only INTEGER NOT NULL CHECK (computers_only IN (0, 1)),"
    " best_score INTEGER NOT NULL,"
    " best_kills INTEGER NOT NULL,"
    " best_hatched INTEGER NOT NULL,"
    " search TEXT NOT NULL,"
    " rows_json TEXT NOT NULL,"
    " details TEXT,"
    " counted_at INTEGER NOT NULL"
    ") WITHOUT ROWID;"
    "CREATE INDEX history_matches_ended ON history_matches (ended, id);"
    "CREATE INDEX history_matches_mode ON history_matches (mode, ended, id);"
    "CREATE INDEX history_matches_format ON history_matches (format, ended, id);"
    "CREATE INDEX history_matches_result ON history_matches (result, ended, id);"
    "CREATE INDEX history_matches_computers ON history_matches (computers_only, ended, id);"
    "CREATE INDEX history_matches_score ON history_matches (best_score, ended, id);"
    "CREATE INDEX history_matches_kills ON history_matches (best_kills, ended, id);"
    "CREATE INDEX history_matches_hatched ON history_matches (best_hatched, ended, id);"
    "CREATE INDEX history_matches_turns ON history_matches (turns, ended, id);"
    "CREATE INDEX history_matches_turns_short ON history_matches (turns, ended DESC, id DESC);"
    "CREATE TABLE history_seats ("
    " match_id TEXT NOT NULL REFERENCES history_matches (id) ON DELETE CASCADE,"
    " seat INTEGER NOT NULL CHECK (seat >= 0 AND seat <= 3),"
    " name TEXT NOT NULL,"
    " bot TEXT NOT NULL,"
    " score INTEGER NOT NULL,"
    " killed INTEGER NOT NULL,"
    " lost INTEGER NOT NULL,"
    " hatched INTEGER NOT NULL,"
    " bombs_planted INTEGER NOT NULL,"
    " bombs_defused INTEGER NOT NULL,"
    " fires_lit INTEGER NOT NULL,"
    " left_early INTEGER NOT NULL CHECK (left_early IN (0, 1)),"
    " winner INTEGER NOT NULL CHECK (winner IN (0, 1)),"
    " PRIMARY KEY (match_id, seat)"
    ") WITHOUT ROWID;"
    "CREATE TABLE history_markers ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " deleted_at INTEGER NOT NULL"
    ") WITHOUT ROWID;";

// Version 3. map_plays: how many times each map was played, one row per map (the file name and the FNV-1a 64 of the file as 16 lower-case hex digits, the key of Community-Maps/maps.json: another file under
// the same name is another map). Written in the transaction that keeps a match in the history (HistoryStore::put), once for each match that had a person in it and ran at least 30 seconds; `last_played` is
// the end time of the newest such match (seconds since 1970 UTC).
const char* const kMapPlaysSql =
    "CREATE TABLE map_plays ("
    " map TEXT NOT NULL,"
    " hash TEXT NOT NULL CHECK (length(hash) = 16),"
    " plays INTEGER NOT NULL CHECK (plays >= 1),"
    " last_played INTEGER NOT NULL,"
    " PRIMARY KEY (map, hash)"
    ") WITHOUT ROWID;";

}  // namespace

const std::vector<db::Migration>& db_migrations() {
    static const std::vector<db::Migration> list = {
        {1, "meta",
         "CREATE TABLE meta (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID;"
         "INSERT INTO meta (key, value) VALUES ('created_at', strftime('%Y-%m-%dT%H:%M:%SZ', 'now'));"},
        {2, "history", kHistorySql},
        {3, "map_plays", kMapPlaysSql},
    };
    return list;
}

}  // namespace ants::server

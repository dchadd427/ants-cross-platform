// The migrations of the server's database (docs/SERVER.md "The database"). Version 1 is the foundation (`meta`); version 2 holds the match history (docs/REPLAYS.md "The match history"); version 3 the play counts of the maps; version 4 the site statistics.
#include "ants_server/db_schema.hpp"

namespace ants::server {

namespace {

// Version 2. history_matches: one row per match, with a column for everything that GET /history filters or sorts by, and an index for each of them (the list is "newest first" unless it says otherwise, so
// every index ends in the end time and the id: the order of a tie). `result` is "decided", "draw" or "cut" (a player left, or the time ran out), `mode` the room's game mode, `best_score` / `best_kills` /
// `best_hatched` the largest of the seats' numbers (the keys of the sorts "score", "kills" and "hatched"; in mode 187 `best_score` is the kills too), `search` the lower-case words that the list's `q`
// looks in (the map's name and what each seat shows, one per line), `rows_json` the ranked rows, and `details` is the stats of a match that the replay pass keeps (JSON text, ants_server/history_details.hpp; NULL until the pass has counted it). `turns` has two indexes: the sort "long" reads the first one backwards (longest first, the newer first among equal ones), the
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

// Version 4. site_stats: the two counters of the front page (docs/SERVER.md "The site statistics"), one row each: `kind` is "online" (a match that ran 30 seconds) or "local" (a single-player game that a browser
// reported) and `total` the games since the counters began. site_stats_hours: the buckets of the last 24 hours (`hour` is the hour number on the server's clock, seconds since 1970 / 3600; `games` at least 1); the
// server writes both tables whole in one transaction. The UTC day the counters count from (`since`) is the meta row 'site_stats_since'. The file site-stats.json of an older server is read into these
// tables at the first start with them (SiteStats::open_db) and is left as it was.
const char* const kSiteStatsSql =
    "CREATE TABLE site_stats ("
    " kind TEXT PRIMARY KEY NOT NULL CHECK (kind IN ('online', 'local')),"
    " total INTEGER NOT NULL CHECK (total >= 0)"
    ") WITHOUT ROWID;"
    "CREATE TABLE site_stats_hours ("
    " kind TEXT NOT NULL REFERENCES site_stats (kind) ON DELETE CASCADE,"
    " hour INTEGER NOT NULL,"
    " games INTEGER NOT NULL CHECK (games >= 1),"
    " PRIMARY KEY (kind, hour)"
    ") WITHOUT ROWID;";

// Version 5. The map submissions (docs/MAP_SUBMISSIONS.md). map_submissions: one row for each map that a player sent: `state` is waiting, approved, declined, taken_down or withdrawn; `token_hash` is
// the SHA-256 of the maker's token (the token itself is never kept); `name` / `name_key` what the maker proposed, `final_name` / `final_key` the name the map has once it is approved (the owner's, which may
// differ; NULL until then). The partial unique index on `final_key` is what keeps an approved or taken-down name protected, whatever the code does. `file` is the name of the canonical file in the
// submissions folder (made by the server, never from the maker's text), `hash` its FNV-1a 64 as 16 hex digits. `findings` holds the checker's warnings as JSON text. submission_limits counts the
// submissions of each address and UTC day by a keyed hash of the address, never the address; review_log is the owner's audit trail (who did what, when).
const char* const kSubmissionsSql =
    "CREATE TABLE map_submissions ("
    " id TEXT PRIMARY KEY NOT NULL CHECK (length(id) = 32),"
    " token_hash TEXT NOT NULL CHECK (length(token_hash) = 64),"
    " state TEXT NOT NULL CHECK (state IN ('waiting', 'approved', 'declined', 'taken_down', 'withdrawn')),"
    " kind TEXT NOT NULL CHECK (kind IN ('classic', 'extended')),"
    " name TEXT NOT NULL,"
    " name_key TEXT NOT NULL,"
    " final_name TEXT,"
    " final_key TEXT,"
    " renamed INTEGER NOT NULL DEFAULT 0 CHECK (renamed IN (0, 1)),"
    " maker TEXT NOT NULL,"
    " note TEXT NOT NULL,"
    " owner_message TEXT NOT NULL DEFAULT '',"
    " width INTEGER NOT NULL,"
    " height INTEGER NOT NULL,"
    " minutes INTEGER NOT NULL,"
    " players INTEGER NOT NULL,"
    " description TEXT NOT NULL,"
    " size INTEGER NOT NULL CHECK (size > 0),"
    " hash TEXT NOT NULL CHECK (length(hash) = 16),"
    " findings TEXT NOT NULL,"
    " file TEXT NOT NULL,"
    " file_kept INTEGER NOT NULL DEFAULT 1 CHECK (file_kept IN (0, 1)),"
    " created_at INTEGER NOT NULL,"
    " decided_at INTEGER,"
    " CHECK ((state IN ('approved', 'taken_down')) = (final_key IS NOT NULL))"
    ") WITHOUT ROWID;"
    "CREATE UNIQUE INDEX map_submissions_final ON map_submissions (final_key) WHERE final_key IS NOT NULL;"
    "CREATE INDEX map_submissions_state ON map_submissions (state, created_at, id);"
    "CREATE INDEX map_submissions_token ON map_submissions (token_hash, created_at, id);"
    "CREATE TABLE submission_limits ("
    " addr_key TEXT NOT NULL,"
    " day INTEGER NOT NULL,"
    " submitted INTEGER NOT NULL CHECK (submitted >= 1),"
    " PRIMARY KEY (addr_key, day)"
    ") WITHOUT ROWID;"
    "CREATE TABLE review_log ("
    " id INTEGER PRIMARY KEY,"
    " at INTEGER NOT NULL,"
    " actor_id TEXT NOT NULL,"
    " actor_name TEXT NOT NULL,"
    " action TEXT NOT NULL,"
    " submission TEXT NOT NULL,"
    " detail TEXT NOT NULL"
    ");";

// Version 6. The accounts of the owner's page (docs/MAP_SUBMISSIONS.md, "Accounts"). admin_users: one row for each account; `pw_hash` is "pbkdf2-sha256$<rounds>$<salt>$<hash>" (the password itself is never kept),
// `must_change` is 1 while the password is the starting one, `session_gen` goes up whenever the user's sessions must end. admin_locks: the counts of wrong tries (scope pair = one name from one address,
// addr = one address, user = one name from everywhere; `key` is a keyed hash, never a name or an address). admin_known_addr: the addresses (keyed hashes) that signed a user in. admin_audit: who did what and
// when (`addr_key` a keyed hash). map_submissions.decided_by is the name of the account that approved, declined, took down or put back a map; the maps decided before this version get the name that
// review_log recorded.
const char* const kAccountsSql =
    "CREATE TABLE admin_users ("
    " id INTEGER PRIMARY KEY,"
    " name TEXT NOT NULL,"
    " name_key TEXT NOT NULL UNIQUE,"
    " role TEXT NOT NULL CHECK (role IN ('admin', 'moderator')),"
    " pw_hash TEXT NOT NULL,"
    " must_change INTEGER NOT NULL DEFAULT 1 CHECK (must_change IN (0, 1)),"
    " disabled INTEGER NOT NULL DEFAULT 0 CHECK (disabled IN (0, 1)),"
    " session_gen INTEGER NOT NULL DEFAULT 1,"
    " created_at INTEGER NOT NULL,"
    " created_by TEXT NOT NULL DEFAULT '',"
    " last_login_at INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE TABLE admin_locks ("
    " scope TEXT NOT NULL CHECK (scope IN ('pair', 'addr', 'user')),"
    " key TEXT NOT NULL,"
    " user_ref TEXT NOT NULL DEFAULT '',"
    " fails INTEGER NOT NULL,"
    " first_at INTEGER NOT NULL,"
    " locked_until INTEGER NOT NULL DEFAULT 0,"
    " PRIMARY KEY (scope, key)"
    ") WITHOUT ROWID;"
    "CREATE INDEX admin_locks_user ON admin_locks (user_ref);"
    "CREATE TABLE admin_known_addr ("
    " user_id INTEGER NOT NULL,"
    " addr_key TEXT NOT NULL,"
    " last_ok INTEGER NOT NULL,"
    " PRIMARY KEY (user_id, addr_key)"
    ") WITHOUT ROWID;"
    "CREATE TABLE admin_audit ("
    " id INTEGER PRIMARY KEY,"
    " at INTEGER NOT NULL,"
    " actor TEXT NOT NULL,"
    " actor_id INTEGER NOT NULL DEFAULT 0,"
    " action TEXT NOT NULL,"
    " target TEXT NOT NULL DEFAULT '',"
    " detail TEXT NOT NULL DEFAULT '',"
    " addr_key TEXT NOT NULL DEFAULT ''"
    ");"
    "CREATE INDEX admin_audit_at ON admin_audit (at, id);"
    "ALTER TABLE map_submissions ADD COLUMN decided_by TEXT NOT NULL DEFAULT '';"
    "UPDATE map_submissions SET decided_by = COALESCE((SELECT r.actor_name FROM review_log r WHERE r.submission = map_submissions.id AND r.action IN ('approve', 'decline', 'take_down', 'put_back') ORDER BY r.id DESC LIMIT 1), '')"
    " WHERE state IN ('approved', 'declined', 'taken_down');";

}  // namespace

const std::vector<db::Migration>& db_migrations() {
    static const std::vector<db::Migration> list = {
        {1, "meta",
         "CREATE TABLE meta (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID;"
         "INSERT INTO meta (key, value) VALUES ('created_at', strftime('%Y-%m-%dT%H:%M:%SZ', 'now'));"},
        {2, "history", kHistorySql},
        {3, "map_plays", kMapPlaysSql},
        {4, "site_stats", kSiteStatsSql},
        {5, "map_submissions", kSubmissionsSql},
        {6, "admin_accounts", kAccountsSql},
    };
    return list;
}

}  // namespace ants::server

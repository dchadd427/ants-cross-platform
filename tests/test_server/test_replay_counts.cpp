// Tests of the plays and the likes of replays (docs/REPLAY_COUNTS.md): the numbers on a match's row and the table of the likes (HistoryStore add_play, like, unlike, liked_among, forget_likes), the list that shows
// and sorts by them, the pass that names a person (LikeGate) and the door that changes the numbers (ReplayCountsApi) in front of real recordings of the shipped TINY map. Requests are made by hand: no sockets (the
// real program and its door are tests/scripts/counts_server_check.py). Made-up keys and references only: no real key, account or id is anywhere in this file.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>

#include "../common/test_assert.hpp"
#include "ants_server/admin_pass.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/like_pass.hpp"
#include "ants_server/replay_counts_api.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

namespace {

using ctl::HttpRequest;
using ctl::HttpResponse;

const char* const kSecret = "testing-like-key-3c9a17e5d2b84f06-w";
const char* const kRefAnn = "0123456789abcdef";
const char* const kRefBo = "fedcba9876543210";
// What tests/scripts/test_hub_likes.py's reading of the contract makes from the same words, made once by a script of its own (hmac and hashlib only): a pass for kRefAnn that ends at kT0 + 60 with the nonce
// 00112233445566ff, and the tokens of the two references under kSecret
const char* const kPythonPass = "v1.eyJleHAiOjE3OTE0Njk5ODksIm5vbmNlIjoiMDAxMTIyMzM0NDU1NjZmZiIsInJlZiI6IjAxMjM0NTY3ODlhYmNkZWYifQ.e02831305ea98b18cd1069813d34b59f4e875ef51ddaea3dc3a952f4c075045c";
const char* const kPythonCsrfAnn = "08c76cdec23252e332f20248e7d4bfbf";
const char* const kPythonCsrfBo = "2d462263c4baf107cb7bfa2dd99218f8";

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

std::string payload_json(const std::string& ref, int64_t exp, const std::string& nonce) { return "{\"exp\":" + std::to_string(exp) + ",\"nonce\":" + quoted(nonce) + ",\"ref\":" + quoted(ref) + "}"; }

std::string sign_with(const std::string& key, const std::string& payload) {
    const std::string body = "v1." + base64url_encode(payload);
    return body + "." + hex_encode(hmac_sha256(key, body));
}

std::string like_key(const std::string& secret) { return hmac_sha256(secret, "ants-like-pass-v1"); }

std::string nonce_of(uint64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(n));
    return buf;
}

// A pass as the hub's gate makes it: good for `now`, a new nonce each time
struct Maker {
    std::string secret{kSecret};
    uint64_t counter{0x2000};
    std::string make(int64_t now, const std::string& ref = kRefAnn, int64_t life = 60) { return sign_with(like_key(secret), payload_json(ref, now + life, nonce_of(++counter))); }
};

std::string csrf_of(const std::string& ref) { return hex_encode(hmac_sha256(hmac_sha256(kSecret, "ants-like-csrf-v1"), std::string("csrf1\0", 6) + ref)).substr(0, 32); }

// Three recordings that the history counted, the door in front of them and the made-up clocks of both
struct World {
    Rig rig;
    std::vector<std::string> ids;
    std::unique_ptr<ReplayCountsApi> api;
    Maker maker;
    int64_t now_s{kT0};
    uint32_t now_ms{1000};

    explicit World(const std::string& secret = kSecret) {
        maker.secret = secret;
        ReplayCountsConfig config;
        config.gate_secret = secret;
        config.clock_s = [this]() { return now_s; };
        api = std::make_unique<ReplayCountsApi>(rig.history, &rig.replays, config);
    }
    bool open() {
        if (!rig.open()) return false;
        for (int k = 0; k < 3; ++k) ids.push_back(rig.keep(match_file(k)));
        rig.drain(10);
        return rig.history.count() == 3;
    }
    HttpResponse call(const char* method, const std::string& path, const std::string& query = std::string(), const std::vector<std::pair<std::string, std::string>>& headers = {}, const std::string& body = std::string()) {
        HttpRequest rq;
        rq.method = method;
        rq.path = path;
        rq.query = query;
        rq.body = body;
        for (const auto& h : headers) rq.headers[h.first] = h.second;
        return api->handle(rq, now_ms);
    }
    // a person's call: a new pass, the token of the person (what GET /counts/mine gives), and whatever else is asked for
    HttpResponse as(const std::string& ref, const char* method, const std::string& path, const std::string& query = std::string(), bool with_token = true) {
        std::vector<std::pair<std::string, std::string>> headers = {{"x-ants-like-pass", maker.make(now_s, ref)}};
        if (with_token) headers.push_back({"x-ants-csrf", csrf_of(ref)});
        return call(method, path, query, headers);
    }
    int64_t number(const std::string& sql) {
        db::Statement st;
        if (!rig.database.prepare(st, sql).ok() || st.step() != db::Statement::Step::Row) return -1;
        return st.column_int64(0);
    }
};

// The plan of a statement, for the check that a sort is carried by its index
std::string plan_of(db::Database& database, const std::string& select) {
    db::Statement st;
    if (!database.prepare(st, "EXPLAIN QUERY PLAN " + select).ok()) return "(no plan)";
    std::string out;
    while (st.step() == db::Statement::Step::Row) out += st.column_text(3) + "\n";
    return out;
}

ctl::JsonValue json_of(const HttpResponse& r) { return parse_text(r.body); }

std::string error_of(const HttpResponse& r) { return json_of(r).get("error").str(); }

}  // namespace

int main() {
    std::cout << "=======================================================\n REPLAY COUNTS SUITE (the plays and the likes of replays: the store, the list, the pass and the door)\n=======================================================\n";

    TEST_CASE("RC1.1 The schema: version 12 of the server's database and version 6 of the history service's file have the two numbers on a match's row, an index for each sort, and the table of the likes") {
        ASSERT_TRUE(db_migrations().back().version == 12 && history_db_migrations().back().version == 6);
        for (const bool service : {false, true}) {
            db::Database database;
            ASSERT_TRUE(db::open_and_migrate(database, ":memory:", service ? history_db_migrations() : db_migrations(), db::OpenOptions()).ok());
            db::Statement st;
            ASSERT_TRUE(database.prepare(st, "SELECT group_concat(name, ',') FROM (SELECT name FROM pragma_table_info('history_matches') WHERE name IN ('plays', 'likes') ORDER BY name)").ok() && st.step() == db::Statement::Step::Row);
            ASSERT_EQ(st.column_text(0), std::string("likes,plays"));
            ASSERT_TRUE(database.prepare(st, "SELECT group_concat(name, ',') FROM (SELECT name FROM sqlite_master WHERE type = 'index' AND name IN ('history_matches_plays', 'history_matches_likes', 'replay_likes_ref') ORDER BY name)").ok() && st.step() == db::Statement::Step::Row);
            ASSERT_EQ(st.column_text(0), std::string("history_matches_likes,history_matches_plays,replay_likes_ref"));
            // the table of the likes holds the match's id and the person's reference, and nothing else: no address, no name, no time
            ASSERT_TRUE(database.prepare(st, "SELECT group_concat(name, ',') FROM (SELECT name FROM pragma_table_info('replay_likes') ORDER BY name)").ok() && st.step() == db::Statement::Step::Row);
            ASSERT_EQ(st.column_text(0), std::string("id,ref"));
            ASSERT_FALSE(database.exec("INSERT INTO replay_likes (id, ref) VALUES ('x', 'short')").ok());            // (a reference is 16 characters)
            ASSERT_TRUE(database.exec("INSERT INTO history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, counted_at) VALUES ('m1', 1, 'TINY.LVL', 'v1', 10, 1, NULL, 'highest-score', '1v1', 'decided', 0, 1, 1, 1, 'tiny', '[]', 1)").ok());
            ASSERT_TRUE(database.exec("UPDATE history_matches SET plays = 1000000, likes = 1000000").ok());                // (a match starts with none, and the numbers have a cap)
            ASSERT_FALSE(database.exec("UPDATE history_matches SET plays = 1000001").ok());
            ASSERT_FALSE(database.exec("UPDATE history_matches SET likes = -1").ok());
        }
    } TEST_END();

    TEST_CASE("RC1.2 A play adds one to the match's count and nothing else: an id that is no match's changes nothing; at the cap it stops; the answer of the list carries it") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        const std::string& a = w.ids[0];
        ASSERT_TRUE(w.rig.history.add_play(a) == HistoryStore::Counted::Done && w.rig.history.add_play(a) == HistoryStore::Counted::Done && w.rig.history.add_play(w.ids[1]) == HistoryStore::Counted::Done);
        HistoryRecord r;
        ASSERT_TRUE(w.rig.history.find(a, r) && r.plays == 2 && r.likes == 0);
        ASSERT_TRUE(w.rig.history.find(w.ids[1], r) && r.plays == 1 && w.rig.history.find(w.ids[2], r) && r.plays == 0);
        ASSERT_TRUE(w.rig.history.add_play(id_at(kT0 + 5, "TINY")) == HistoryStore::Counted::Unknown && w.rig.history.add_play("not an id") == HistoryStore::Counted::Unknown);
        ASSERT_EQ(w.number("SELECT count(*) FROM history_matches"), int64_t{3});
        ASSERT_TRUE(w.rig.database.exec("UPDATE history_matches SET plays = 1000000 WHERE id = '" + w.ids[2] + "'").ok());
        ASSERT_TRUE(w.rig.history.add_play(w.ids[2]) == HistoryStore::Counted::Done);
        ASSERT_EQ(w.number("SELECT plays FROM history_matches WHERE id = '" + w.ids[2] + "'"), int64_t{1000000});
        ASSERT_TRUE(w.rig.history.find(a, r));
        const ctl::JsonValue item = parse_text(ctl::to_json(history_record_to_json(r, true)));
        ASSERT_TRUE(item.get("plays").as_int_or(-1) == 2 && item.get("likes").as_int_or(-1) == 0);
        const ctl::JsonValue lean = parse_text(ctl::to_json(history_record_to_json(r, false)));
        ASSERT_TRUE(lean.get("plays").is_int() && lean.get("likes").is_int());
    } TEST_END();

    TEST_CASE("RC1.3 A like is one row of a person and a match, and the match's count is the number of rows: a second like changes nothing, an unlike of a like that is not there changes nothing, two people count two") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        uint32_t likes = 99;
        const std::string& a = w.ids[0];
        ASSERT_TRUE(w.rig.history.like(a, kRefAnn, likes) == HistoryStore::Counted::Done && likes == 1);
        ASSERT_TRUE(w.rig.history.like(a, kRefAnn, likes) == HistoryStore::Counted::Done && likes == 1);
        ASSERT_TRUE(w.rig.history.like(a, kRefBo, likes) == HistoryStore::Counted::Done && likes == 2);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes WHERE id = '" + a + "'"), int64_t{2});
        ASSERT_TRUE(w.rig.history.unlike(a, kRefAnn, likes) == HistoryStore::Counted::Done && likes == 1);
        ASSERT_TRUE(w.rig.history.unlike(a, kRefAnn, likes) == HistoryStore::Counted::Done && likes == 1);                 // (nothing to take back)
        ASSERT_TRUE(w.rig.history.unlike(w.ids[1], kRefAnn, likes) == HistoryStore::Counted::Done && likes == 0);
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + a + "'"), int64_t{1});
        ASSERT_TRUE(w.rig.history.like(id_at(kT0 + 5, "TINY"), kRefAnn, likes) == HistoryStore::Counted::Unknown && w.rig.history.unlike("nope", kRefAnn, likes) == HistoryStore::Counted::Unknown);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes"), int64_t{1});
        // a like does not touch the plays, and the numbers of a record that is put again stay (the likes' rows are not the record's)
        ASSERT_TRUE(w.rig.history.add_play(a) == HistoryStore::Counted::Done);
        HistoryRecord r;
        ASSERT_TRUE(w.rig.history.find(a, r) && r.plays == 1 && r.likes == 1);
        std::string why;
        ASSERT_TRUE(w.rig.history.put(r, why));
        HistoryRecord again;
        ASSERT_TRUE(w.rig.history.find(a, again) && again.plays == 1 && again.likes == 1);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes WHERE id = '" + a + "'"), int64_t{1});
    } TEST_END();

    TEST_CASE("RC1.4 The limits of the store: a person likes at most 1,000 matches at a time, a match shows at most a million likes, and a like that is refused changes nothing") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        ASSERT_TRUE(w.rig.database.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000) INSERT INTO replay_likes (id, ref) SELECT 'other-' || i, '" + std::string(kRefAnn) + "' FROM n").ok());
        uint32_t likes = 0;
        ASSERT_TRUE(w.rig.history.like(w.ids[0], kRefAnn, likes) == HistoryStore::Counted::Limit && likes == 0);
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + w.ids[0] + "'"), int64_t{0});
        ASSERT_TRUE(w.rig.history.like(w.ids[0], kRefBo, likes) == HistoryStore::Counted::Done && likes == 1);               // (somebody else is not held by it)
        ASSERT_TRUE(w.rig.database.exec("DELETE FROM replay_likes WHERE id = 'other-1'").ok());
        ASSERT_TRUE(w.rig.history.like(w.ids[0], kRefAnn, likes) == HistoryStore::Counted::Done && likes == 2);                // (one is taken back: one more may be given)
        ASSERT_TRUE(w.rig.database.exec("UPDATE history_matches SET likes = 1000000 WHERE id = '" + w.ids[1] + "'").ok());
        ASSERT_TRUE(w.rig.history.like(w.ids[1], "aaaaaaaaaaaaaaaa", likes) == HistoryStore::Counted::Limit);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes WHERE ref = 'aaaaaaaaaaaaaaaa'"), int64_t{0});
    } TEST_END();

    TEST_CASE("RC1.5 liked_among gives the person's own likes among the ids asked, in the order asked, and nobody else's; Forget me takes back every like of one person, the counts drop, the others stay") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        uint32_t likes = 0;
        for (const std::string& id : w.ids) ASSERT_TRUE(w.rig.history.like(id, kRefAnn, likes) == HistoryStore::Counted::Done);
        ASSERT_TRUE(w.rig.history.like(w.ids[1], kRefBo, likes) == HistoryStore::Counted::Done && likes == 2);
        ASSERT_TRUE(w.rig.history.add_play(w.ids[0]) == HistoryStore::Counted::Done);
        std::vector<std::string> mine;
        ASSERT_TRUE(w.rig.history.liked_among(kRefBo, {w.ids[2], w.ids[1], w.ids[0]}, mine) && mine == std::vector<std::string>{w.ids[1]});
        ASSERT_TRUE(w.rig.history.liked_among(kRefAnn, {w.ids[2], "ants-TINY-20000101-000000Z", w.ids[0]}, mine) && mine == (std::vector<std::string>{w.ids[2], w.ids[0]}));
        ASSERT_TRUE(w.rig.history.liked_among(kRefAnn, {}, mine) && mine.empty());
        ASSERT_TRUE(w.rig.history.liked_among("0000000000000000", w.ids, mine) && mine.empty());
        size_t removed = 99;
        ASSERT_TRUE(w.rig.history.forget_likes(kRefAnn, removed) && removed == 3);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes WHERE ref = '" + std::string(kRefAnn) + "'"), int64_t{0});
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + w.ids[0] + "'"), int64_t{0});
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + w.ids[1] + "'"), int64_t{1});                  // (Bo's like stays)
        ASSERT_EQ(w.number("SELECT plays FROM history_matches WHERE id = '" + w.ids[0] + "'"), int64_t{1});                  // (a person's plays were never theirs)
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes"), int64_t{1});
        ASSERT_TRUE(w.rig.history.forget_likes(kRefAnn, removed) && removed == 0);
        // the likes of a match that the owner takes out go with it, and so do its numbers
        std::string why;
        ASSERT_TRUE(w.rig.history.remove(w.ids[1], false, why) == HistoryStore::Removal::Done);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes"), int64_t{0});
    } TEST_END();

    TEST_CASE("RC1.6 The list gives plays and likes with every row, sorts by them (a tie keeps the newer match first), and the sorts are words of the query only") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        uint32_t likes = 0;
        for (int k = 0; k < 3; ++k) {
            ASSERT_TRUE(w.rig.history.add_play(w.ids[0]) == HistoryStore::Counted::Done);        // 0: three plays
            if (k < 2) ASSERT_TRUE(w.rig.history.add_play(w.ids[2]) == HistoryStore::Counted::Done);     // 2: two plays
        }
        ASSERT_TRUE(w.rig.history.like(w.ids[1], kRefAnn, likes) == HistoryStore::Counted::Done && w.rig.history.like(w.ids[1], kRefBo, likes) == HistoryStore::Counted::Done);        // 1: two likes
        ASSERT_TRUE(w.rig.history.like(w.ids[2], kRefAnn, likes) == HistoryStore::Counted::Done);
        const auto listed = [&](const std::string& query) {
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/history";
            rq.query = query;
            const ctl::HttpResponse r = public_history_response(&w.rig.history, &w.rig.replays, rq);
            std::vector<std::string> out;
            if (r.status != 200) return out;
            const ctl::JsonValue list = parse_text(r.body).get("history");
            for (size_t i = 0; i < list.size(); ++i) out.push_back(list.at(i).get("id").str() + ":" + std::to_string(list.at(i).get("plays").as_int_or(-1)) + ":" + std::to_string(list.at(i).get("likes").as_int_or(-1)));
            return out;
        };
        // (the newer match is the one kept later: ids[2] is the newest, then 1, then 0)
        ASSERT_TRUE(listed("sort=plays") == (std::vector<std::string>{w.ids[0] + ":3:0", w.ids[2] + ":2:1", w.ids[1] + ":0:2"}));
        ASSERT_TRUE(listed("sort=likes") == (std::vector<std::string>{w.ids[1] + ":0:2", w.ids[2] + ":2:1", w.ids[0] + ":3:0"}));
        ASSERT_TRUE(listed("sort=plays&offset=1&limit=1") == (std::vector<std::string>{w.ids[2] + ":2:1"}));
        ASSERT_TRUE(listed("")[0] == w.ids[2] + ":2:1");                                                                     // (newest first, as before)
        ASSERT_TRUE(w.rig.database.exec("UPDATE history_matches SET plays = 5, likes = 5").ok());
        ASSERT_TRUE(listed("sort=plays") == listed("") && listed("sort=likes") == listed(""));                                // (a tie: the newer first)
        ASSERT_TRUE(listed("sort=plays&rec=watch").size() == 3);
        ctl::HttpRequest bad;
        bad.method = "GET";
        bad.path = "/history";
        bad.query = "sort=popular";
        const ctl::HttpResponse refused = public_history_response(&w.rig.history, &w.rig.replays, bad);
        ASSERT_TRUE(refused.status == 400 && contains(refused.body, "plays or likes"));
        // the one match
        ctl::HttpRequest one;
        one.method = "GET";
        one.path = "/history/" + w.ids[0];
        const ctl::JsonValue item = parse_text(public_history_response(&w.rig.history, &w.rig.replays, one).body);
        ASSERT_TRUE(item.get("plays").as_int_or(-1) == 5 && item.get("likes").as_int_or(-1) == 5);
        // the plan of both sorts is the index's: no temporary tree
        const std::string plan_plays = plan_of(w.rig.database, "SELECT m.id FROM history_matches m ORDER BY m.plays DESC, m.ended DESC, m.id DESC LIMIT 25");
        const std::string plan_likes = plan_of(w.rig.database, "SELECT m.id FROM history_matches m ORDER BY m.likes DESC, m.ended DESC, m.id DESC LIMIT 25");
        ASSERT_TRUE(contains(plan_plays, "history_matches_plays") && !contains(plan_plays, "TEMP B-TREE") && contains(plan_likes, "history_matches_likes") && !contains(plan_likes, "TEMP B-TREE"));
    } TEST_END();

    TEST_CASE("RC2.1 The pass: the known answers of an independent maker are accepted, a pass is used once, and every other shape is refused for its own reason; an admin pass is no like pass and the other way round") {
        LikeGate gate(kSecret);
        ASSERT_TRUE(gate.open());
        PersonIdentity who;
        ASSERT_TRUE(gate.check(kPythonPass, kT0, who) == PassVerdict::Ok && who.ref == kRefAnn && who.nonce == "00112233445566ff" && who.exp == kT0 + 60);
        ASSERT_TRUE(gate.check(kPythonPass, kT0, who) == PassVerdict::Replayed);
        ASSERT_EQ(gate.csrf_token(kRefAnn), std::string(kPythonCsrfAnn));
        ASSERT_EQ(gate.csrf_token(kRefBo), std::string(kPythonCsrfBo));
        Maker m;
        ASSERT_TRUE(gate.check(m.make(kT0), kT0, who) == PassVerdict::Ok);
        ASSERT_TRUE(gate.check("", kT0, who) == PassVerdict::NoPass);
        ASSERT_TRUE(gate.check("v2.abc.def", kT0, who) == PassVerdict::Malformed && gate.check("garbage", kT0, who) == PassVerdict::Malformed && gate.check(std::string(600, 'a'), kT0, who) == PassVerdict::Malformed);
        const std::string good = m.make(kT0);
        std::string flipped = good;
        flipped.back() = flipped.back() == '0' ? '1' : '0';
        ASSERT_TRUE(gate.check(flipped, kT0, who) == PassVerdict::BadPass);                                                     // (one character of the mac)
        Maker other;
        other.secret = "another-like-key-99b04c61f7e2d583-v";
        ASSERT_TRUE(gate.check(other.make(kT0), kT0, who) == PassVerdict::BadPass);                                             // (the hub of another key)
        ASSERT_TRUE(gate.check(m.make(kT0, kRefAnn, -1), kT0, who) == PassVerdict::Expired);                                    // (it ended a second ago)
        ASSERT_TRUE(gate.check(m.make(kT0, kRefAnn, 200), kT0, who) == PassVerdict::BadFields);                                 // (a pass that lasts longer than the hub makes one)
        const std::string key = like_key(kSecret);
        ASSERT_TRUE(gate.check(sign_with(key, "{\"exp\":" + std::to_string(kT0 + 60) + ",\"nonce\":\"00112233445566aa\",\"ref\":\"0123456789abcdef\",\"role\":\"admin\"}"), kT0, who) == PassVerdict::BadFields);      // (a fourth member)
        ASSERT_TRUE(gate.check(sign_with(key, "{\"exp\":" + std::to_string(kT0 + 60) + ",\"nonce\":\"00112233445566ab\"}"), kT0, who) == PassVerdict::BadFields);                                                       // (no reference)
        ASSERT_TRUE(gate.check(sign_with(key, payload_json("0123456789ABCDEF", kT0 + 60, "00112233445566ac")), kT0, who) == PassVerdict::BadFields);            // (a reference is lower-case hex)
        ASSERT_TRUE(gate.check(sign_with(key, payload_json("12345", kT0 + 60, "00112233445566ad")), kT0, who) == PassVerdict::BadFields);
        ASSERT_TRUE(gate.check(sign_with(key, payload_json(kRefAnn, kT0 + 60, "short")), kT0, who) == PassVerdict::BadFields);
        ASSERT_TRUE(gate.check(sign_with(key, "not json"), kT0, who) == PassVerdict::Malformed);
        // not one pass of the other kind: the keys have their own labels
        AdminGate admin(kSecret);
        AdminIdentity admin_who;
        ASSERT_TRUE(admin.check(m.make(kT0), kT0, admin_who) != PassVerdict::Ok);
        const std::string admin_pass = sign_with(hmac_sha256(kSecret, "ants-admin-pass-v1"), "{\"exp\":" + std::to_string(kT0 + 60) + ",\"name\":\"ann\",\"nonce\":\"00112233445566ae\",\"ref\":\"0123456789abcdef\",\"role\":\"admin\"}");
        ASSERT_TRUE(gate.check(admin_pass, kT0, who) == PassVerdict::BadPass);
        ASSERT_TRUE(gate.csrf_token(kRefAnn) != admin.csrf_token(AdminIdentity{kRefAnn, "ann", "admin", "00112233445566af", kT0 + 60}));
        // no good key: nothing is accepted, and a key that is the documents' sample is no key
        ASSERT_TRUE(!LikeGate("").open() && !LikeGate("short").open() && !LikeGate(std::string(kSampleGateKey)).open());
        ASSERT_TRUE(LikeGate("").check(m.make(kT0), kT0, who) == PassVerdict::NoKey && LikeGate("").csrf_token(kRefAnn).empty());
        // the table of the used passes is bounded, and a pass that has ended is forgotten
        LikeGate small(kSecret, 3);
        for (int k = 0; k < 3; ++k) ASSERT_TRUE(small.check(m.make(kT0), kT0, who) == PassVerdict::Ok);
        ASSERT_TRUE(small.check(m.make(kT0), kT0, who) == PassVerdict::Full && small.remembered() == 3);
        ASSERT_TRUE(small.check(m.make(kT0 + 200), kT0 + 200, who) == PassVerdict::Ok && small.remembered() == 1);
    } TEST_END();

    TEST_CASE("RC3.1 POST /counts/play/<id>: one play of a recording that can be watched, 204; an id that is unknown, malformed or whose recording is gone is 404 and changes nothing; the other shapes of a request are refused") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        const HttpResponse ok = w.call("POST", "/counts/play/" + w.ids[0]);
        ASSERT_TRUE(ok.status == 204 && ok.body.empty());
        ASSERT_EQ(w.number("SELECT plays FROM history_matches WHERE id = '" + w.ids[0] + "'"), int64_t{1});
        ASSERT_EQ(w.call("POST", "/counts/play/" + id_at(kT0 + 5, "TINY")).status, 404);
        ASSERT_EQ(w.call("POST", "/counts/play/ants-TINY-x").status, 404);
        ASSERT_EQ(w.call("POST", "/counts/play/").status, 404);
        ASSERT_EQ(w.call("POST", "/counts/play").status, 404);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0] + "/more").status, 404);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "x=1").status, 404);
        ASSERT_EQ(w.call("GET", "/counts/play/" + w.ids[0]).status, 405);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "", {}, "{}").status, 400);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "", {{"sec-fetch-site", "cross-site"}}).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "", {{"sec-fetch-site", "same-site"}}).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "", {{"sec-fetch-site", "same-origin"}}).status, 204);
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "", {{"sec-fetch-site", "none"}}).status, 204);
        ASSERT_EQ(w.call("PUT", "/counts/play/" + w.ids[0]).status, 405);
        ASSERT_EQ(w.number("SELECT plays FROM history_matches WHERE id = '" + w.ids[0] + "'"), int64_t{3});
        ASSERT_EQ(w.call("POST", "/elsewhere/play/" + w.ids[0]).status, 404);
        // a match whose recording is gone keeps its numbers and takes no more
        ASSERT_TRUE(w.rig.replays.remove(history_recording_file(w.ids[1])));
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[1]).status, 404);
        ASSERT_EQ(w.number("SELECT plays FROM history_matches WHERE id = '" + w.ids[1] + "'"), int64_t{0});
        // nothing about who played is anywhere in an answer or in the tables
        ASSERT_EQ(w.number("SELECT count(*) FROM pragma_table_info('history_matches') WHERE name LIKE '%addr%' OR name LIKE '%ip%' OR name LIKE '%cookie%'"), int64_t{0});
    } TEST_END();

    TEST_CASE("RC3.2 The door's budget of plays: a burst of 100 and ten a second after it, over all visitors; the answer is 429 and the count is not touched") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        int taken = 0, refused = 0;
        for (int k = 0; k < 130; ++k) {
            const int status = w.call("POST", "/counts/play/" + w.ids[0]).status;
            taken += status == 204 ? 1 : 0;
            refused += status == 429 ? 1 : 0;
        }
        ASSERT_TRUE(taken == 100 && refused == 30);
        ASSERT_EQ(w.number("SELECT plays FROM history_matches WHERE id = '" + w.ids[0] + "'"), int64_t{100});
        ASSERT_EQ(error_of(w.call("POST", "/counts/play/" + w.ids[0])), std::string("rate"));
        w.now_ms += 500;                                                                                                       // (half a second: five plays)
        int again = 0;
        for (int k = 0; k < 8; ++k) again += w.call("POST", "/counts/play/" + w.ids[0]).status == 204 ? 1 : 0;
        ASSERT_EQ(again, 5);
        w.now_ms += 60000;                                                                                                     // (a minute: the bucket is full again, and no more)
        taken = 0;
        for (int k = 0; k < 130; ++k) taken += w.call("POST", "/counts/play/" + w.ids[0]).status == 204 ? 1 : 0;
        ASSERT_EQ(taken, 100);
        // a request that is refused for its shape does not spend the budget
        World fresh;
        ASSERT_TRUE(fresh.open());
        for (int k = 0; k < 200; ++k) fresh.call("POST", "/counts/play/ants-TINY-x");
        ASSERT_EQ(fresh.call("POST", "/counts/play/" + fresh.ids[0]).status, 204);
    } TEST_END();

    TEST_CASE("RC4.1 GET /counts/mine: a person's own likes among the ids asked (at most 50, every one a match id) and the token of their changes; no pass, a bad pass and no key are told apart; nobody else's likes are in it") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        uint32_t likes = 0;
        ASSERT_TRUE(w.rig.history.like(w.ids[0], kRefAnn, likes) == HistoryStore::Counted::Done && w.rig.history.like(w.ids[1], kRefBo, likes) == HistoryStore::Counted::Done);
        const std::string ids = w.ids[2] + "," + w.ids[1] + "," + w.ids[0];
        const HttpResponse ann = w.as(kRefAnn, "GET", "/counts/mine", "ids=" + ids, false);
        ASSERT_EQ(ann.status, 200);
        const ctl::JsonValue a = json_of(ann);
        ASSERT_TRUE(a.get("signed_in").is_bool() && a.get("signed_in").as_bool_or(false) && a.get("liked").size() == 1 && a.get("liked").at(0).str() == w.ids[0] && a.get("csrf").str() == kPythonCsrfAnn);
        const ctl::JsonValue b = json_of(w.as(kRefBo, "GET", "/counts/mine", "ids=" + ids, false));
        ASSERT_TRUE(b.get("liked").size() == 1 && b.get("liked").at(0).str() == w.ids[1] && b.get("csrf").str() == kPythonCsrfBo);
        ASSERT_TRUE(json_of(w.as(kRefAnn, "GET", "/counts/mine", "", false)).get("liked").size() == 0);
        ASSERT_TRUE(json_of(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + w.ids[1], false)).get("liked").size() == 0);
        ASSERT_TRUE(!contains(ann.body, kRefAnn) && !contains(ann.body, kRefBo));                                                // (no reference in an answer)
        // the shape of the query
        std::string many = w.ids[0];
        for (int k = 0; k < 49; ++k) many += "," + id_at(kT0 + 100 + k, "TINY");
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + many, false).status, 200);                                      // (fifty)
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + many + "," + id_at(kT0 + 500, "TINY"), false).status, 400);      // (fifty-one)
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + w.ids[0] + ",nope", false).status, 400);
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + w.ids[0] + ",", false).status, 400);
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "who=" + w.ids[0], false).status, 400);
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + w.ids[0] + "&x=1", false).status, 400);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/mine", "", false).status, 405);
        // who is asking
        ASSERT_EQ(w.call("GET", "/counts/mine", "ids=" + w.ids[0]).status, 401);
        ASSERT_EQ(w.call("GET", "/counts/mine", "ids=" + w.ids[0], {{"x-ants-like-pass", "v1.e30.00"}}).status, 401);
        Maker other;
        other.secret = "another-like-key-99b04c61f7e2d583-v";
        ASSERT_EQ(w.call("GET", "/counts/mine", "", {{"x-ants-like-pass", other.make(kT0)}}).status, 401);
        const std::string pass = w.maker.make(kT0);
        ASSERT_EQ(w.call("GET", "/counts/mine", "", {{"x-ants-like-pass", pass}}).status, 200);
        ASSERT_EQ(w.call("GET", "/counts/mine", "", {{"x-ants-like-pass", pass}}).status, 401);                                  // (a pass is used once)
        w.now_s += 61;
        ASSERT_EQ(w.call("GET", "/counts/mine", "", {{"x-ants-like-pass", pass}}).status, 401);
        // with no good key likes are off, and the answer says that, not that the person is nobody
        World off("");
        ASSERT_TRUE(off.open());
        ASSERT_EQ(off.call("GET", "/counts/mine", "", {{"x-ants-like-pass", Maker().make(kT0)}}).status, 503);
        ASSERT_EQ(error_of(off.call("GET", "/counts/mine", "", {{"x-ants-like-pass", Maker().make(kT0)}})), std::string("likes_off"));
        ASSERT_FALSE(off.api->likes_on());
        ASSERT_EQ(off.call("POST", "/counts/play/" + off.ids[0]).status, 204);                                                    // (plays need no key)
    } TEST_END();

    TEST_CASE("RC4.2 POST /counts/like and /counts/unlike: the person and the token are checked first, then the match; a like is once, an unlike takes it back; the answers carry the count and no reference") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        const std::string& a = w.ids[0];
        HttpResponse r = w.as(kRefAnn, "POST", "/counts/like/" + a);
        ASSERT_TRUE(r.status == 200 && json_of(r).get("liked").as_bool_or(false) && json_of(r).get("likes").as_int_or(-1) == 1 && !contains(r.body, kRefAnn));
        r = w.as(kRefAnn, "POST", "/counts/like/" + a);
        ASSERT_TRUE(r.status == 200 && json_of(r).get("likes").as_int_or(-1) == 1);
        r = w.as(kRefBo, "POST", "/counts/like/" + a);
        ASSERT_TRUE(r.status == 200 && json_of(r).get("likes").as_int_or(-1) == 2);
        r = w.as(kRefAnn, "POST", "/counts/unlike/" + a);
        ASSERT_TRUE(r.status == 200 && !json_of(r).get("liked").as_bool_or(true) && json_of(r).get("likes").as_int_or(-1) == 1);
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + a + "'"), int64_t{1});
        // no token, a wrong one, the token of another person
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/" + a, "", false).status, 403);
        ASSERT_EQ(error_of(w.as(kRefAnn, "POST", "/counts/like/" + a, "", false)), std::string("csrf"));
        ASSERT_EQ(w.call("POST", "/counts/like/" + a, "", {{"x-ants-like-pass", w.maker.make(kT0, kRefAnn)}, {"x-ants-csrf", csrf_of(kRefBo)}}).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/like/" + a, "", {{"x-ants-like-pass", w.maker.make(kT0, kRefAnn)}, {"x-ants-csrf", std::string(32, '0')}}).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/like/" + a, "", {{"x-ants-like-pass", w.maker.make(kT0, kRefAnn)}, {"x-ants-csrf", std::string(kPythonCsrfAnn) + "0"}}).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/like/" + a, "", {{"x-ants-csrf", csrf_of(kRefAnn)}}).status, 401);                    // (no pass)
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + a + "'"), int64_t{1});
        // the shape of the request
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/like/" + a).status, 405);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/" + a, "x=1").status, 404);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/ants-nope").status, 404);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/" + id_at(kT0 + 5, "TINY")).status, 404);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/unlike/" + id_at(kT0 + 5, "TINY")).status, 404);
        ASSERT_EQ(w.call("POST", "/counts/like/" + a, "", {{"x-ants-like-pass", w.maker.make(kT0)}, {"x-ants-csrf", csrf_of(kRefAnn)}, {"sec-fetch-site", "cross-site"}}).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/like/" + a, "", {{"x-ants-like-pass", w.maker.make(kT0)}, {"x-ants-csrf", csrf_of(kRefAnn)}}, "{}").status, 400);
        // a recording that is gone takes no new like, and a like on it can still be taken back
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/" + w.ids[1]).status, 200);
        ASSERT_TRUE(w.rig.replays.remove(history_recording_file(w.ids[1])));
        ASSERT_EQ(w.as(kRefBo, "POST", "/counts/like/" + w.ids[1]).status, 404);
        ASSERT_EQ(w.number("SELECT likes FROM history_matches WHERE id = '" + w.ids[1] + "'"), int64_t{1});
        r = w.as(kRefAnn, "POST", "/counts/unlike/" + w.ids[1]);
        ASSERT_TRUE(r.status == 200 && json_of(r).get("likes").as_int_or(-1) == 0);
        // the limit of a person
        ASSERT_TRUE(w.rig.database.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000) INSERT INTO replay_likes (id, ref) SELECT 'other-' || i, '" + std::string(kRefBo) + "' FROM n").ok());
        ASSERT_EQ(w.as(kRefBo, "POST", "/counts/like/" + w.ids[2]).status, 409);
        ASSERT_EQ(error_of(w.as(kRefBo, "POST", "/counts/like/" + w.ids[2])), std::string("limit"));
        // with no good key a like is off
        World off("");
        ASSERT_TRUE(off.open());
        ASSERT_EQ(off.call("POST", "/counts/like/" + off.ids[0], "", {{"x-ants-like-pass", Maker().make(kT0)}, {"x-ants-csrf", csrf_of(kRefAnn)}}).status, 503);
    } TEST_END();

    TEST_CASE("RC4.3 The changes of one person are limited, a burst of 10 and one every two seconds, and the plays and the other people are not held by it; the table of the people is bounded") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        int done = 0, slowed = 0;
        for (int k = 0; k < 16; ++k) {
            const int status = w.as(kRefAnn, "POST", k % 2 == 0 ? "/counts/like/" + w.ids[0] : "/counts/unlike/" + w.ids[0]).status;
            done += status == 200 ? 1 : 0;
            slowed += status == 429 ? 1 : 0;
        }
        ASSERT_TRUE(done == 10 && slowed == 6);
        ASSERT_EQ(error_of(w.as(kRefAnn, "POST", "/counts/like/" + w.ids[0])), std::string("rate"));
        ASSERT_EQ(w.as(kRefBo, "POST", "/counts/like/" + w.ids[0]).status, 200);                                                // (another person)
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0]).status, 204);
        w.now_ms += 4000;                                                                                                       // (two more)
        ASSERT_TRUE(w.as(kRefAnn, "POST", "/counts/like/" + w.ids[0]).status == 200 && w.as(kRefAnn, "POST", "/counts/unlike/" + w.ids[0]).status == 200 && w.as(kRefAnn, "POST", "/counts/like/" + w.ids[0]).status == 429);
        // a request that is refused before the budget (a wrong token) does not spend it
        World fresh;
        ASSERT_TRUE(fresh.open());
        for (int k = 0; k < 40; ++k) ASSERT_EQ(fresh.as(kRefAnn, "POST", "/counts/like/" + fresh.ids[0], "", false).status, 403);
        ASSERT_EQ(fresh.as(kRefAnn, "POST", "/counts/like/" + fresh.ids[0]).status, 200);
        // five thousand people at once: the table is bounded, a person who is new is turned away until some budgets are full again
        World crowd;
        ASSERT_TRUE(crowd.open());
        int served = 0;
        for (int k = 0; k < 4200; ++k) {
            char ref[17];
            std::snprintf(ref, sizeof ref, "%016x", 0x100000u + static_cast<unsigned>(k));
            served += crowd.as(ref, "POST", "/counts/unlike/" + crowd.ids[0]).status == 200 ? 1 : 0;
        }
        ASSERT_EQ(served, 4096);
        crowd.now_ms += 30000;                                                                                                  // (every budget is full again)
        ASSERT_EQ(crowd.as("00000000000f0000", "POST", "/counts/unlike/" + crowd.ids[0]).status, 200);
    } TEST_END();

    TEST_CASE("RC5.1 POST /counts/forget takes back every like of the person of the pass, with the token: the counts drop, the others stay, and the answer says how many; without the pass or the token nothing happens") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        for (const std::string& id : w.ids) {
            ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/" + id).status, 200);
            ASSERT_EQ(w.as(kRefBo, "POST", "/counts/like/" + id).status, 200);
            w.now_ms += 3000;
        }
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/forget", "", false).status, 403);
        ASSERT_EQ(w.call("POST", "/counts/forget", "", {{"x-ants-csrf", csrf_of(kRefAnn)}}).status, 401);
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/forget").status, 405);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/forget", "x=1").status, 404);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/forget/" + w.ids[0]).status, 404);
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes"), int64_t{6});
        const HttpResponse r = w.as(kRefAnn, "POST", "/counts/forget");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("removed").as_int_or(-1) == 3 && !contains(r.body, kRefAnn));
        ASSERT_EQ(w.number("SELECT count(*) FROM replay_likes WHERE ref = '" + std::string(kRefAnn) + "'"), int64_t{0});
        ASSERT_EQ(w.number("SELECT sum(likes) FROM history_matches"), int64_t{3});
        ASSERT_EQ(json_of(w.as(kRefAnn, "POST", "/counts/forget")).get("removed").as_int_or(-1), int64_t{0});
        ASSERT_EQ(json_of(w.as(kRefBo, "GET", "/counts/mine", "ids=" + w.ids[0], false)).get("liked").size(), size_t{1});         // (Bo still likes it)
        // forgetting does not wait for the budget of changes: a person who used it up can still go
        World busy;
        ASSERT_TRUE(busy.open());
        for (int k = 0; k < 12; ++k) busy.as(kRefAnn, "POST", k % 2 == 0 ? "/counts/like/" + busy.ids[0] : "/counts/unlike/" + busy.ids[0]);
        ASSERT_EQ(busy.as(kRefAnn, "POST", "/counts/forget").status, 200);
    } TEST_END();

    TEST_CASE("RC6.1 A database that does not answer is a 503 on every route, and the door never throws; a pass that is refused is one line of the log a minute with no pass and no reference in it") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        World w;
        ASSERT_TRUE(w.open());
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0], "", {{"x-ants-like-pass", "v1.e30.00"}}).status, 204);
        for (int k = 0; k < 5; ++k) w.call("GET", "/counts/mine", "", {{"x-ants-like-pass", std::string("v1.e30.") + std::string(64, 'a')}});
        const std::vector<std::string> lines = w.api->take_notices();
        ASSERT_TRUE(lines.size() == 1 && contains(lines[0], "refused") && !contains(lines[0], "v1.") && !contains(lines[0], kRefAnn));
        w.now_s += 61;
        w.call("GET", "/counts/mine", "", {{"x-ants-like-pass", std::string("v1.e30.") + std::string(64, 'a')}});
        ASSERT_EQ(w.api->take_notices().size(), size_t{1});                                                                     // (a minute later: once more)
        ASSERT_TRUE(w.rig.database.exec("ALTER TABLE history_matches RENAME TO history_matches_gone").ok());
        uint32_t likes = 0;
        ASSERT_TRUE(w.rig.history.like(w.ids[0], kRefAnn, likes) == HistoryStore::Counted::Failed);                             // (the first error: the store is unavailable from now on)
        ASSERT_FALSE(w.rig.history.available());
        ASSERT_EQ(w.call("POST", "/counts/play/" + w.ids[0]).status, 503);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/like/" + w.ids[0]).status, 503);
        ASSERT_EQ(w.as(kRefAnn, "POST", "/counts/forget").status, 503);
        ASSERT_EQ(w.as(kRefAnn, "GET", "/counts/mine", "ids=" + w.ids[0], false).status, 503);
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

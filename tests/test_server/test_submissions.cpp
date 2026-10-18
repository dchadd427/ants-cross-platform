// Tests of the map submissions' store (SubmissionStore, docs/MAP_SUBMISSIONS.md): what is accepted and what is refused (the server's own check, never the client's claim), the canonical file that is kept under a name the
// server makes, the limits (an address and a day, all addresses, the queue, the size budgets, the disk), the names (protected from approval on, taken down or not, the library's too, case does not matter, never
// replaced), the maker's token (only its hash is kept; a wrong token learns nothing), the owner's decisions, what an old row and an old file become, and a database that fails. No network, no room that runs a match.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/submissions.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {         // (run_tests.sh clears it; a mutation run sets it)
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

namespace core = ants::assets::lvl_core;

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A map that passes the checks: the original's TINY, as it is
const std::string& tiny() {
    static const std::string bytes = slurp(fs::path(maps_dir()) / "TINY.LVL");
    return bytes;
}

// The same map with bytes after its end (the writer drops them): a file that is not canonical
std::string with_trailing_junk() { return tiny() + std::string(3000, 'J') + "MARKER-OF-THE-UPLOAD"; }

// TINY as an Extended map (version 9)
std::string extended_tiny() {
    const core::Result r = core::convert_to_extended(reinterpret_cast<const uint8_t*>(tiny().data()), tiny().size());
    if (r.status != 0 || r.payload.size() < 4) return std::string();
    const auto b = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(r.payload[i])); };
    const uint32_t json = b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
    return r.payload.substr(4 + json);
}


// TINY (an Extended copy) put in the corner of a bigger grid of its own ground: a map that passes, as large as the format allows when 512 x 512
std::string enlarged_extended(uint32_t rows, uint32_t columns, const assets::LevelLimits& limits) {
    const std::string small = extended_tiny();
    assets::LevelData t;
    if (!assets::LVLParser::load_from_memory(reinterpret_cast<const uint8_t*>(small.data()), small.size(), t, nullptr, assets::LevelLimits::for_extended_maps())) return std::string();
    assets::LevelData big = t;
    big.width.val = columns;
    big.height.val = rows;
    big.layer1_terrain.assign(static_cast<size_t>(rows) * columns, t.layer1_terrain[0]);
    big.layer2_interactive.assign(static_cast<size_t>(rows) * columns, assets::MapCell());
    for (uint32_t y = 0; y < t.height; ++y) {
        for (uint32_t x = 0; x < t.width; ++x) {
            big.layer1_terrain[static_cast<size_t>(y) * columns + x] = t.layer1_terrain[static_cast<size_t>(y) * t.width + x];
            big.layer2_interactive[static_cast<size_t>(y) * columns + x] = t.layer2_interactive[static_cast<size_t>(y) * t.width + x];
        }
    }
    const assets::LevelWriteResult w = assets::write_level(big, limits);
    return w.ok ? std::string(w.bytes.begin(), w.bytes.end()) : std::string();
}

// A map that does not pass: plain ground, no hill, no ants
std::string blank_map() {
    const core::Result r = core::blank(31, 31, 485, 10, 10, core::Profile::NewMap);
    return r.status == 0 ? r.payload : std::string();
}

MapStore& library() {
    static MapStore store = [] {
        MapStore s(maps_dir(), std::string(ANTS_SOURCE_DIR) + "/Community-Maps");
        s.scan();
        return s;
    }();
    return store;
}

struct Sub {
    TempDir dir;                                       // (declared before the database: it goes after it)
    db::Database database;
    SubmissionConfig config;
    std::unique_ptr<SubmissionStore> store;
    int64_t now{kT0};

    explicit Sub(const std::function<void(SubmissionConfig&)>& tune = nullptr) {
        open_server_db(database, "");
        config.dir = (dir.path / "submissions").string();
        config.min_free_bytes = 1;                      // (the machine's own free space is not what a test is about)
        if (tune) tune(config);
        store = std::make_unique<SubmissionStore>(database, config, [](const std::string& key) { return library().library_has_name(key); });
        std::string why;
        if (!store->open(now, why)) std::cout << "(open failed: " << why << ") ";
    }

    SubmitInput input(const std::string& bytes, const std::string& name = "River Run", const std::string& address = "203.0.113.5", const std::string& token = "") {
        SubmitInput in;
        in.kind = "classic";
        in.name = name;
        in.maker = "Mia";
        in.note = "my first map";
        in.bytes = bytes;
        in.address = address;
        in.token = token;
        return in;
    }
    SubmitOutcome send(const std::string& name = "River Run", const std::string& address = "203.0.113.5", const std::string& token = "") { return store->submit(input(tiny(), name, address, token), now); }
    std::string text(const std::string& sql) {
        db::Statement st;
        database.prepare(st, sql);
        return st.step() == db::Statement::Step::Row ? st.column_text(0) : std::string("<no row>");
    }
    std::vector<std::string> files() {
        std::vector<std::string> out;
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(config.dir) / "files", ec), end; !ec && it != end; it.increment(ec)) out.push_back(it->path().filename().string());
        return out;
    }
    bool approve(const std::string& id, const std::string& name = "", const std::string& message = "") {
        Submission s;
        Failure why;
        return store->approve(id, name, message, "42", "Dave", now, s, why);
    }
    Failure approve_failure(const std::string& id, const std::string& name = "") {
        Submission s;
        Failure why;
        store->approve(id, name, "", "42", "Dave", now, s, why);
        return why;
    }
};

}  // namespace

int main() {
    std::cout << "=======================================================\n MAP SUBMISSIONS: THE STORE\n=======================================================\n";
    if (tiny().empty()) {
        std::cout << "the map TINY.LVL is not there\n";
        return 1;
    }

    TEST_CASE("SUB1.1 A map that passes is kept as its canonical file under a name the server made; the upload's own bytes are not kept; the answer has the maker's token once") {
        Sub rig;
        const SubmitOutcome o = rig.store->submit(rig.input(with_trailing_junk()), rig.now);
        ASSERT_TRUE(o.ok);
        ASSERT_TRUE(o.token_is_new && o.token.size() == 43);
        ASSERT_TRUE(o.submission.state == "waiting" && o.submission.kind == "classic" && o.submission.name == "River Run" && o.submission.maker == "Mia");
        ASSERT_TRUE(o.submission.width == 31 && o.submission.height == 31 && o.submission.players >= 2);
        const std::vector<std::string> files = rig.files();
        ASSERT_EQ(files.size(), size_t{1});
        ASSERT_EQ(files[0], o.submission.id + ".lvl");                                 // (32 hex digits of the server's own making)
        ASSERT_EQ(o.submission.id.size(), size_t{32});
        const std::string stored = slurp(fs::path(rig.config.dir) / "files" / files[0]);
        ASSERT_TRUE(stored.find("MARKER-OF-THE-UPLOAD") == std::string::npos);        // not the bytes that came
        ASSERT_TRUE(stored.size() < with_trailing_junk().size());
        const core::Result canonical = core::rewrite(reinterpret_cast<const uint8_t*>(tiny().data()), tiny().size(), core::Profile::NewMap);
        ASSERT_TRUE(canonical.status == 0 && stored == canonical.payload);             // (the core's own rewrite)
        ASSERT_EQ(static_cast<int64_t>(stored.size()), o.submission.size);
    } TEST_END();

    TEST_CASE("SUB1.2 An Extended map is accepted as Extended, and its kind is the file's, not the sender's claim") {
        Sub rig;
        const std::string ext = extended_tiny();
        ASSERT_FALSE(ext.empty());
        SubmitInput in = rig.input(ext, "Ext Run");
        in.kind = "extended";
        const SubmitOutcome o = rig.store->submit(in, rig.now);
        ASSERT_TRUE(o.ok && o.submission.kind == "extended");
        in.kind = "classic";                                                            // (an Extended file said to be Classic: refused, not judged as Classic)
        const SubmitOutcome lied = rig.store->submit(in, rig.now);
        ASSERT_TRUE(!lied.ok && lied.fail.code == "kind_mismatch" && lied.fail.status == 400);
        SubmitInput other = rig.input(tiny(), "Other Run");
        other.kind = "extended";                                                        // (and a Classic file said to be Extended)
        ASSERT_EQ(rig.store->submit(other, rig.now).fail.code, std::string("kind_mismatch"));
        other.kind = "huge";
        ASSERT_EQ(rig.store->submit(other, rig.now).fail.code, std::string("bad_kind"));
    } TEST_END();

    TEST_CASE("SUB1.2b The largest Extended map (512 x 512 cells, 3.1 MiB) passes and is kept whole; a grid of more cells than the place allows is refused by the server's own check") {
        Sub rig;
        const std::string big = enlarged_extended(512, 512, assets::LevelLimits::for_extended_maps());
        ASSERT_TRUE(big.size() > 3 * 1024 * 1024 && big.size() <= SubmissionStore::max_upload_bytes("extended"));
        SubmitInput in = rig.input(big, "Whole Plain");
        in.kind = "extended";
        const auto began = std::chrono::steady_clock::now();
        const SubmitOutcome o = rig.store->submit(in, rig.now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
        std::cout << "(" << ms << " ms) " << std::flush;
        ASSERT_TRUE(o.ok && o.submission.kind == "extended" && o.submission.width == 512 && o.submission.height == 512);
        ASSERT_EQ(o.submission.size, static_cast<int64_t>(big.size()));
        ASSERT_TRUE(!rig.store->preview_png(o.submission.id).empty());                        // (a picture of the whole map, sampled to 128 pixels)
        const std::string too_many = enlarged_extended(600, 600, assets::LevelLimits::format_ceiling());       // (360,000 cells: more than the place's 262,144)
        ASSERT_TRUE(!too_many.empty() && too_many.size() > SubmissionStore::max_upload_bytes("classic"));
        in.bytes = too_many;
        in.name = "Too Many Cells";
        in.address = "203.0.113.99";
        if (too_many.size() <= SubmissionStore::max_upload_bytes("extended")) {
            const SubmitOutcome refused = rig.store->submit(in, rig.now);
            ASSERT_TRUE(!refused.ok && (refused.fail.code == "map_too_big" || refused.fail.code == "map_fails_checks" || refused.fail.code == "not_a_map"));
        } else {
            ASSERT_EQ(rig.store->submit(in, rig.now).fail.code, std::string("too_large"));
        }
        ASSERT_EQ(rig.files().size(), size_t{1});
    } TEST_END();

    TEST_CASE("SUB1.3 A map that does not pass the server's check is refused with the findings, and nothing is kept: no file, no row, and no use of the day's three") {
        Sub rig;
        const SubmitOutcome o = rig.store->submit(rig.input(blank_map()), rig.now);
        ASSERT_TRUE(!o.ok && o.fail.status == 422 && o.fail.code == "map_fails_checks");
        ASSERT_TRUE(o.fail.extra.is_object() && o.fail.extra.get("findings").size() > 0);
        ASSERT_TRUE(o.fail.extra.get("findings").at(0).get("message").str().size() > 3);
        ASSERT_TRUE(rig.files().empty());
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions"), std::string("0"));
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM submission_limits"), std::string("0"));
        for (int i = 0; i < 5; ++i) ASSERT_EQ(rig.store->submit(rig.input(blank_map()), rig.now).fail.code, std::string("map_fails_checks"));
        ASSERT_TRUE(rig.send("Still Ok").ok);                                           // (the refusals cost the sender nothing of the three a day)
    } TEST_END();

    TEST_CASE("SUB1.4 Nothing that is not a map passes: empty, truncated, a text, a wrong version word, a file that is too large for its kind, one byte flipped in the middle") {
        Sub rig;
        ASSERT_EQ(rig.store->submit(rig.input(""), rig.now).fail.code, std::string("not_a_map"));
        const SubmitOutcome cut = rig.store->submit(rig.input(tiny().substr(0, 100)), rig.now);
        ASSERT_TRUE(!cut.ok && (cut.fail.code == "not_a_map" || cut.fail.code == "map_fails_checks"));
        ASSERT_EQ(rig.store->submit(rig.input("GET / HTTP/1.1\r\n\r\n"), rig.now).fail.code, std::string("not_a_map"));
        std::string v7 = tiny();
        v7[0] = 7;
        ASSERT_EQ(rig.store->submit(rig.input(v7), rig.now).fail.code, std::string("not_a_map"));
        std::string big = tiny() + std::string(520 * 1024, 'x');
        const SubmitOutcome too_big = rig.store->submit(rig.input(big), rig.now);
        ASSERT_TRUE(too_big.fail.status == 413 && too_big.fail.code == "too_large");
        for (size_t at : {size_t{10}, size_t{40}, tiny().size() / 2, tiny().size() - 3}) {            // (a damaged map either fails the checks or is still a map: it is never stored damaged)
            std::string damaged = tiny();
            damaged[at] = static_cast<char>(damaged[at] ^ 0xFF);
            const SubmitOutcome o = rig.store->submit(rig.input(damaged, "Damaged " + std::to_string(at)), rig.now);
            if (o.ok) {
                const core::Result again = core::check_map(reinterpret_cast<const uint8_t*>(slurp(fs::path(rig.config.dir) / "files" / (o.submission.id + ".lvl")).data()), static_cast<size_t>(o.submission.size), core::Profile::NewMap);
                ASSERT_TRUE(again.status == 0 && again.payload.find("\"works\":true") != std::string::npos);
            } else {
                ASSERT_TRUE(o.fail.status == 400 || o.fail.status == 422);
            }
        }
        for (const std::string& f : rig.files()) ASSERT_TRUE(f.size() == 36 && f.compare(f.size() - 4, 4, ".lvl") == 0);            // (and nothing but made names is on the disk, no temporary file)
    } TEST_END();

    TEST_CASE("SUB1.5 Three a day for one address: the fourth is refused, another address is not, the next UTC day starts again; the address itself is not kept") {
        Sub rig;
        for (int i = 0; i < 3; ++i) ASSERT_TRUE(rig.send("Map " + std::to_string(i + 1)).ok);
        const SubmitOutcome fourth = rig.send("Map 4");
        ASSERT_TRUE(!fourth.ok && fourth.fail.status == 429 && fourth.fail.code == "address_limit");
        ASSERT_TRUE(rig.send("Map 4", "203.0.113.6").ok);
        rig.now += 86400;
        ASSERT_TRUE(rig.send("Map 5").ok);
        db::Statement st;
        rig.database.prepare(st, "SELECT addr_key FROM submission_limits");
        while (st.step() == db::Statement::Step::Row) {
            ASSERT_TRUE(st.column_text(0).size() == 32 && st.column_text(0).find("203.0.113") == std::string::npos);        // (a keyed hash)
        }
    } TEST_END();

    TEST_CASE("SUB1.6 All addresses together are limited too (a flood from many addresses fills the day, not the disk)") {
        Sub rig([](SubmissionConfig& c) { c.per_day_total = 5; c.max_waiting = 100; });
        for (int i = 0; i < 5; ++i) ASSERT_TRUE(rig.send("Map " + std::to_string(i + 1), "198.51.100." + std::to_string(i + 1)).ok);
        const SubmitOutcome sixth = rig.send("Map 6", "198.51.100.77");
        ASSERT_TRUE(!sixth.ok && sixth.fail.status == 429 && sixth.fail.code == "day_limit");
        rig.now += 86400;
        ASSERT_TRUE(rig.send("Map 6", "198.51.100.77").ok);
    } TEST_END();

    TEST_CASE("SUB1.6b The queue holds the configured number at most (200 by default) and says so; a map that was decided leaves it") {
        Sub rig([](SubmissionConfig& c) { c.max_waiting = 4; c.per_day_total = 1000; });
        SubmissionConfig defaults;
        ASSERT_TRUE(defaults.max_waiting == 200 && defaults.per_address_per_day == 3);
        std::vector<std::string> ids;
        for (int i = 0; i < 4; ++i) {
            const SubmitOutcome o = rig.send("Map " + std::to_string(i + 1), "198.51.100." + std::to_string(i + 1));
            ASSERT_TRUE(o.ok);
            ids.push_back(o.submission.id);
        }
        const SubmitOutcome full = rig.send("Map 5", "198.51.100.9");
        ASSERT_TRUE(!full.ok && full.fail.status == 429 && full.fail.code == "queue_full");
        ASSERT_TRUE(rig.approve(ids[0]));                                               // (decided: it is no longer waiting)
        ASSERT_TRUE(rig.send("Map 5", "198.51.100.9").ok);
        ASSERT_EQ(rig.send("Map 6", "198.51.100.10").fail.code, std::string("queue_full"));
    } TEST_END();

    TEST_CASE("SUB1.7 A withdrawal by the maker frees the place in the queue at once, deletes the file, and cannot be done twice or to a map that was decided") {
        Sub rig([](SubmissionConfig& c) { c.max_waiting = 1; c.per_address_per_day = 10; });
        const SubmitOutcome a = rig.send("First Map");
        ASSERT_TRUE(a.ok);
        ASSERT_EQ(rig.send("Second Map", "203.0.113.9").fail.code, std::string("queue_full"));
        Submission s;
        Failure why;
        ASSERT_TRUE(rig.store->withdraw(a.submission.id, a.token, rig.now, s, why));
        ASSERT_TRUE(s.state == "withdrawn" && rig.files().empty());
        ASSERT_FALSE(rig.store->withdraw(a.submission.id, a.token, rig.now, s, why));
        ASSERT_EQ(why.code, std::string("not_waiting"));
        const SubmitOutcome b = rig.send("Second Map", "203.0.113.9");
        ASSERT_TRUE(b.ok);
        ASSERT_TRUE(rig.approve(b.submission.id));
        ASSERT_FALSE(rig.store->withdraw(b.submission.id, b.token, rig.now, s, why));    // (approved: nothing to withdraw)
        ASSERT_EQ(why.code, std::string("not_waiting"));
        ASSERT_EQ(rig.files().size(), size_t{1});
    } TEST_END();

    TEST_CASE("SUB1.8 The size budgets and the disk: a full budget or too little free space is a clean refusal that keeps nothing") {
        {
            Sub rig([](SubmissionConfig& c) { c.max_waiting_bytes = 30 * 1000; c.per_address_per_day = 10; });
            ASSERT_TRUE(rig.send("Map One").ok);
            const SubmitOutcome o = rig.send("Map Two", "203.0.113.8");
            ASSERT_TRUE(!o.ok && o.fail.code == "queue_full");
        }
        {
            Sub rig([](SubmissionConfig& c) { c.max_kept_bytes = 10 * 1000; });
            const SubmitOutcome o = rig.send();
            ASSERT_TRUE(!o.ok && o.fail.status == 503 && o.fail.code == "storage_full");
            ASSERT_TRUE(rig.files().empty());
        }
        {
            Sub rig([](SubmissionConfig& c) { c.min_free_bytes = uint64_t{1} << 60; });             // (more free space than any disk has)
            const SubmitOutcome o = rig.send();
            ASSERT_TRUE(!o.ok && o.fail.status == 503 && o.fail.code == "storage_full");
            ASSERT_TRUE(rig.files().empty());
        }
    } TEST_END();

    TEST_CASE("SUB1.9 The maker's token: only its hash is kept; one browser's submissions are listed together; a wrong or missing token, and a stranger's id, all get the same refusal") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 10; });
        const SubmitOutcome a = rig.send("First Map");
        const SubmitOutcome b = rig.send("Second Map", "203.0.113.5", a.token);
        ASSERT_TRUE(a.ok && b.ok && !b.token_is_new && b.token == a.token);
        const SubmitOutcome other = rig.send("Third Map", "203.0.113.5");               // (no token: another browser)
        ASSERT_TRUE(other.ok && other.token_is_new && other.token != a.token);
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions WHERE token_hash LIKE '%" + a.token.substr(0, 8) + "%'"), std::string("0"));
        ASSERT_EQ(rig.text("SELECT token_hash FROM map_submissions WHERE id = '" + a.submission.id + "'"), hex_encode(sha256(a.token)));
        std::vector<Submission> list;
        Failure why;
        ASSERT_TRUE(rig.store->list_for_token(a.token, list, why) && list.size() == 2);
        ASSERT_TRUE(rig.store->list_for_token(other.token, list, why) && list.size() == 1 && list[0].id == other.submission.id);
        Submission s;
        ASSERT_TRUE(rig.store->get_for_token(a.submission.id, a.token, s, why));
        const std::string forged = base64url_encode(std::string(32, 'z'));
        ASSERT_FALSE(rig.store->get_for_token(a.submission.id, forged, s, why));
        const Failure wrong = why;
        ASSERT_FALSE(rig.store->get_for_token(std::string(32, '0'), a.token, s, why));                // (an id that does not exist)
        ASSERT_TRUE(why.status == wrong.status && why.code == wrong.code && why.message == wrong.message);
        ASSERT_FALSE(rig.store->get_for_token(a.submission.id, other.token, s, why));                // (an id that exists, another's token)
        ASSERT_TRUE(why.status == wrong.status && why.code == wrong.code && why.message == wrong.message);
        ASSERT_FALSE(rig.store->get_for_token("not-an-id", a.token, s, why));
        ASSERT_FALSE(rig.store->list_for_token("", list, why));
        ASSERT_EQ(why.status, 401);
        ASSERT_FALSE(rig.store->list_for_token("short", list, why));
        ASSERT_FALSE(rig.store->withdraw(a.submission.id, other.token, rig.now, s, why));         // (and nobody withdraws another's map)
        ASSERT_EQ(rig.text("SELECT state FROM map_submissions WHERE id = '" + a.submission.id + "'"), std::string("waiting"));
        SubmitInput bad = rig.input(tiny(), "Fourth Map");
        bad.token = "not a token";
        ASSERT_EQ(rig.store->submit(bad, rig.now).fail.code, std::string("bad_token"));
        bad.token = forged;                                                              // (a token this server never made is not taken as the browser's: a new one is made)
        const SubmitOutcome fresh = rig.store->submit(bad, rig.now);
        ASSERT_TRUE(fresh.ok && fresh.token_is_new && fresh.token != forged);
    } TEST_END();

    TEST_CASE("SUB2.1 The maker's name, the map's name and the note are cleaned and judged on the server; a refusal says which and why, and keeps nothing") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 100; c.per_day_total = 1000; });
        const auto code = [&](const std::string& name, const std::string& maker, const std::string& note) {
            SubmitInput in = rig.input(tiny(), name);
            in.maker = maker;
            in.note = note;
            return rig.store->submit(in, rig.now).fail.code;
        };
        ASSERT_EQ(code("../../etc/passwd", "Mia", ""), std::string("bad_name"));
        ASSERT_EQ(code("a/b", "Mia", ""), std::string("bad_name"));
        ASSERT_EQ(code("a\\b", "Mia", ""), std::string("bad_name"));
        ASSERT_EQ(code("CON", "Mia", ""), std::string("bad_name"));
        ASSERT_EQ(code("ab", "Mia", ""), std::string("bad_name"));
        ASSERT_EQ(code(std::string(61, 'a'), "Mia", ""), std::string("bad_name"));
        ASSERT_EQ(code("Caf\xC3\xA9", "Mia", ""), std::string("bad_name"));                  // (a map's name is its file name in the game's protocol: ASCII only)
        ASSERT_EQ(code("Name\n", "Mia", ""), std::string("bad_name"));                      // (a control character is never white space to be trimmed: the name is refused)
        ASSERT_EQ(code("Good Name", "", ""), std::string("bad_maker"));
        ASSERT_EQ(code("Good Name", std::string(25, 'm'), ""), std::string("bad_maker"));
        ASSERT_EQ(code("Good Name", "<b>Mia</b>", ""), std::string("bad_maker"));
        ASSERT_EQ(code("Good Name", "Mia", std::string(281, 'n')), std::string("bad_note"));
        ASSERT_EQ(code("Good Name", "Mia", "<script>alert(1)</script>"), std::string("bad_note"));
        ASSERT_EQ(code("Good Name", "Mia", std::string("bad\x01" "byte")), std::string("bad_note"));
        ASSERT_EQ(code("Good Name", "Mia", "\xC3\x28"), std::string("bad_note"));            // (not UTF-8)
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions WHERE name LIKE '%Name%' AND name != 'Name' AND name != 'Good Name'"), std::string("0"));
        SubmitInput fine = rig.input(tiny(), "  Wide   Name \xE2\x80\x8B ");              // (spaces folded and trimmed, a zero-width space removed)
        fine.maker = "J\xC3\xBCrgen \xE2\x80\x8F";                                           // (a letter of Latin-1, and a right-to-left mark that goes)
        fine.note = "line one\r\n\r\n\r\n\r\nline two";
        const SubmitOutcome ok = rig.store->submit(fine, rig.now);
        ASSERT_TRUE(ok.ok && ok.submission.name == "Wide Name" && ok.submission.maker == "J\xC3\xBCrgen" && ok.submission.note == "line one\n\nline two");
    } TEST_END();

    TEST_CASE("SUB3.1 A name is taken by a library map, by an approved map and by a taken-down one, whatever the letter case; a taken name may be sent (the owner decides) but never approved over") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 100; c.per_day_total = 1000; });
        const SubmitOutcome tiny_named = rig.send("tiny");                               // (a library map: TINY.LVL)
        ASSERT_TRUE(tiny_named.ok && tiny_named.name_taken);
        const SubmitOutcome free_named = rig.send("River Run");
        ASSERT_TRUE(free_named.ok && !free_named.name_taken);
        Failure f = rig.approve_failure(tiny_named.submission.id);
        ASSERT_TRUE(f.status == 409 && f.code == "name_taken" && f.extra.get("suggestion").str() == "tiny 2");
        ASSERT_EQ(rig.approve_failure(tiny_named.submission.id, "TINY").code, std::string("name_taken"));
        ASSERT_EQ(rig.approve_failure(tiny_named.submission.id, "Treasure").code, std::string("name_taken"));
        ASSERT_TRUE(rig.approve(free_named.submission.id));
        const SubmitOutcome again = rig.send("RIVER RUN");
        ASSERT_TRUE(again.ok && again.name_taken);                                       // (approved: protected now)
        ASSERT_EQ(rig.approve_failure(again.submission.id).code, std::string("name_taken"));
        ASSERT_EQ(rig.approve_failure(again.submission.id, "river run").code, std::string("name_taken"));
        Submission s;
        Failure why;
        ASSERT_TRUE(rig.store->take_down(free_named.submission.id, "42", "Dave", rig.now, s, why) && s.state == "taken_down");
        ASSERT_EQ(rig.approve_failure(again.submission.id).code, std::string("name_taken"));        // (taken down: still protected)
        ASSERT_TRUE(rig.store->approve(again.submission.id, "River Run 2", "", "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(s.state == "approved" && s.final_name == "River Run 2" && s.renamed);
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions WHERE final_key = 'river run'"), std::string("1"));       // (the name has one holder, and the database says it too)
        db::Statement st;
        rig.database.prepare(st, "UPDATE map_submissions SET state = 'approved', final_name = 'x', final_key = 'river run' WHERE id = ?");
        st.bind_text(1, tiny_named.submission.id);
        ASSERT_TRUE(st.step() == db::Statement::Step::Failed);                           // (the unique index refuses a second holder of a name, whatever the code does)
    } TEST_END();

    TEST_CASE("SUB3.2 The next free name: a number from 2, the one the name already ends in is replaced, a taken number is skipped, and nothing else about the name is guessed") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 100; c.per_day_total = 1000; });
        std::string name;
        Failure why;
        ASSERT_TRUE(rig.store->next_name("Zzyzx Pond", name, why));
        ASSERT_EQ(name, std::string("Zzyzx Pond 2"));
        const SubmitOutcome two = rig.send("Zzyzx Pond 2");
        ASSERT_TRUE(rig.approve(two.submission.id));
        ASSERT_TRUE(rig.store->next_name("Zzyzx Pond", name, why) && name == "Zzyzx Pond 3");
        ASSERT_TRUE(rig.store->next_name("Zzyzx Pond 2", name, why) && name == "Zzyzx Pond 3");
        ASSERT_TRUE(rig.store->next_name("Fresh Lake", name, why) && name == "Fresh Lake 2");
        ASSERT_FALSE(rig.store->next_name("a/b", name, why));
        ASSERT_EQ(why.code, std::string("bad_name"));
    } TEST_END();

    TEST_CASE("SUB4.1 Approve keeps the maker's name or takes the owner's, with the owner's short message; the maker sees 'renamed by the owner'; the file is the same one") {
        Sub rig;
        const SubmitOutcome o = rig.send("River Run");
        Submission s;
        Failure why;
        ASSERT_TRUE(rig.store->approve(o.submission.id, "Big River", "Nice map, I made the name longer", "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(s.state == "approved" && s.final_name == "Big River" && s.name == "River Run" && s.renamed);
        const ctl::JsonValue maker = maker_view(s);
        ASSERT_TRUE(maker.get("approved_name").str() == "Big River" && maker.get("renamed_by_owner").as_bool_or(false) && maker.get("message").str() == "Nice map, I made the name longer");
        ASSERT_TRUE(maker.get("name").str() == "River Run" && !maker.has("maker") && !maker.has("note"));      // (the maker's view has nothing of the owner's side)
        std::vector<ApprovedMap> lobby = rig.store->approved_maps();
        ASSERT_TRUE(lobby.size() == 1 && lobby[0].name == "Big River.lvl" && lobby[0].kind == "classic" && lobby[0].maker == "Mia");
        ASSERT_TRUE(fs::path(lobby[0].path).filename().string() == o.submission.id + ".lvl");
        ASSERT_EQ(rig.approve_failure(o.submission.id).code, std::string("not_waiting"));              // (nothing is approved twice)
        const SubmitOutcome p = rig.send("Plain Map", "203.0.113.6");
        ASSERT_TRUE(rig.store->approve(p.submission.id, "", "", "42", "Dave", rig.now, s, why) && s.final_name == "Plain Map" && !s.renamed);
        ASSERT_FALSE(maker_view(s).get("renamed_by_owner").as_bool_or(true));
        ASSERT_EQ(rig.approve_failure(std::string(32, 'a')).status, 404);
        ASSERT_EQ(rig.approve_failure("../etc").status, 404);
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM review_log WHERE action = 'approve' AND actor_id = '42'"), std::string("2"));       // (the owner's trail)
        ASSERT_EQ(rig.store->take_approved_changed(), true);
        ASSERT_EQ(rig.store->take_approved_changed(), false);
    } TEST_END();

    TEST_CASE("SUB4.2 Decline needs a short reason that the maker can read; a declined map is not approved afterwards; take down hides without deleting, put back shows again") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 100; });
        const SubmitOutcome a = rig.send("Declined One");
        Submission s;
        Failure why;
        ASSERT_FALSE(rig.store->decline(a.submission.id, "", "42", "Dave", rig.now, s, why));
        ASSERT_EQ(why.code, std::string("bad_reason"));
        ASSERT_FALSE(rig.store->decline(a.submission.id, std::string(201, 'r'), "42", "Dave", rig.now, s, why));
        ASSERT_FALSE(rig.store->decline(a.submission.id, "<img src=x>", "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(rig.store->decline(a.submission.id, "Too small to play", "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(s.state == "declined" && maker_view(s).get("message").str() == "Too small to play");
        ASSERT_EQ(rig.approve_failure(a.submission.id).code, std::string("not_waiting"));
        ASSERT_FALSE(rig.store->take_down(a.submission.id, "42", "Dave", rig.now, s, why));
        const SubmitOutcome b = rig.send("Taken Map", "203.0.113.6");
        ASSERT_TRUE(rig.approve(b.submission.id));
        ASSERT_EQ(rig.store->approved_maps().size(), size_t{1});
        ASSERT_TRUE(rig.store->take_down(b.submission.id, "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(rig.store->approved_maps().empty());                                 // (hidden from the lobby's list ...)
        ASSERT_EQ(rig.files().size(), size_t{2});                                        // (... and nothing was deleted)
        ASSERT_FALSE(rig.store->take_down(b.submission.id, "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(rig.store->put_back(b.submission.id, "42", "Dave", rig.now, s, why) && s.state == "approved");
        ASSERT_EQ(rig.store->approved_maps().size(), size_t{1});
        ASSERT_FALSE(rig.store->put_back(b.submission.id, "42", "Dave", rig.now, s, why));
        ReviewCounts c = rig.store->counts();
        ASSERT_TRUE(c.waiting == 0 && c.approved == 1 && c.declined == 1 && c.taken_down == 0);
        std::vector<Submission> list;
        ASSERT_TRUE(rig.store->list("declined", list, why) && list.size() == 1);
        ASSERT_FALSE(rig.store->list("everything", list, why));
        ASSERT_EQ(why.code, std::string("bad_state"));
    } TEST_END();

    TEST_CASE("SUB5.1 The lobby's list: an approved Classic map joins the list of GET /maps and can be found by name; an Extended one is only in the newest version's list and is never found; a taken-down one is neither") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 100; });
        MapStore store(maps_dir(), std::string(ANTS_SOURCE_DIR) + "/Community-Maps");
        store.scan();
        const size_t before = store.listing().size();
        const SubmitOutcome classic = rig.send("Fresh Classic");
        SubmitInput in = rig.input(extended_tiny(), "Fresh Extended");
        in.kind = "extended";
        const SubmitOutcome extended = rig.store->submit(in, rig.now);
        ASSERT_TRUE(rig.approve(classic.submission.id) && rig.approve(extended.submission.id));
        store.set_approved(rig.store->approved_maps());
        ASSERT_EQ(store.listing().size(), before + 1);
        const ctl::JsonValue plain = parse_text(store.list_json(nullptr));
        const ctl::JsonValue all = parse_text(store.list_json(nullptr, true));
        ASSERT_TRUE(plain.get("format").as_int_or(0) == 1 && all.get("format").as_int_or(0) == 2);
        const auto has = [](const ctl::JsonValue& list, const std::string& file) {
            for (size_t i = 0; i < list.get("maps").size(); ++i) {
                if (list.get("maps").at(i).get("file").str() == file) return true;
            }
            return false;
        };
        ASSERT_TRUE(has(plain, "Fresh Classic.lvl") && !has(plain, "Fresh Extended.lvl"));              // (an old version never sees an Extended map)
        ASSERT_TRUE(has(all, "Fresh Classic.lvl") && has(all, "Fresh Extended.lvl"));
        ASSERT_EQ(plain.get("maps").size(), before + 1);
        for (size_t i = 0; i < plain.get("maps").size(); ++i) ASSERT_FALSE(plain.get("maps").at(i).has("kind"));       // (the list of before has no new member)
        for (size_t i = 0; i < all.get("maps").size(); ++i) {
            const ctl::JsonValue& m = all.get("maps").at(i);
            if (m.get("file").str() == "Fresh Classic.lvl") {
                ASSERT_TRUE(m.get("maker").str() == "Mia" && m.get("kind").str() == "classic" && m.get("thumb").str() == "/maps/previews/Fresh%20Classic.png");
            }
            if (m.get("file").str() == "Fresh Extended.lvl") ASSERT_EQ(m.get("kind").str(), std::string("extended"));
        }
        MapEntry entry;
        ASSERT_TRUE(store.find("Fresh Classic.lvl", entry));
        ASSERT_FALSE(store.find("Fresh Extended.lvl", entry));                           // (no room can load it: the game does not load Extended maps)
        ASSERT_EQ(store.listed_name("fresh classic.lvl"), std::string("Fresh Classic.lvl"));
        ASSERT_TRUE(store.approved("Fresh Classic.lvl") != nullptr && store.approved("Fresh Extended.lvl") != nullptr && store.approved("fresh classic.lvl") == nullptr);
        Submission s;
        Failure why;
        ASSERT_TRUE(rig.store->take_down(classic.submission.id, "42", "Dave", rig.now, s, why));
        store.set_approved(rig.store->approved_maps());
        ASSERT_FALSE(store.find("Fresh Classic.lvl", entry));
        ASSERT_EQ(store.listing().size(), before);
        ASSERT_EQ(store.listed_name("Fresh Classic.lvl"), std::string());
        ASSERT_TRUE(store.library_has_name("tiny") && store.library_has_name("treasure") && !store.library_has_name("fresh classic"));        // (an approved name is not the library's: the store's unique index holds it)
        store.scan();                                                                    // (a scan keeps what was approved)
        ASSERT_EQ(store.listing().size(), before);
    } TEST_END();

    TEST_CASE("SUB5.2 A map of the library keeps its name against an approved one that clashes later, and a file that is gone is not listed") {
        Sub rig;
        const SubmitOutcome o = rig.send("Late Name");
        ASSERT_TRUE(rig.approve(o.submission.id));
        const std::string folder = (rig.dir.path / "library").string();
        fs::create_directories(folder);
        fs::copy_file(fs::path(maps_dir()) / "TINY.LVL", fs::path(folder) / "Late Name.lvl");           // (the library gets a map of that name afterwards)
        MapStore store(folder);
        store.scan();
        store.set_approved(rig.store->approved_maps());
        ASSERT_EQ(store.listing().size(), size_t{1});
        ASSERT_FALSE(store.listing()[0].approved);                                       // (the library's map wins)
        fs::remove(fs::path(rig.config.dir) / "files" / (o.submission.id + ".lvl"));
        ASSERT_TRUE(rig.store->approved_maps().empty());                                 // (a file that is gone: left out, with a line for the log)
        ASSERT_FALSE(rig.store->take_notices().empty());
    } TEST_END();

    TEST_CASE("SUB6.1 The picture is a real PNG (signature, chunk checks, size) and the owner's picture of a map that is gone is empty") {
        Sub rig;
        const SubmitOutcome o = rig.send("Picture Map");
        const std::string png = rig.store->preview_png(o.submission.id);
        ASSERT_TRUE(png.size() > 100 && png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
        ASSERT_TRUE(png.find("IHDR") == 12 && png.find("PLTE") != std::string::npos && png.find("IDAT") != std::string::npos && png.compare(png.size() - 8, 4, "IEND") == 0);
        ASSERT_EQ(static_cast<unsigned char>(png[19]), 124);                             // 31 cells, enlarged four times: 124 pixels
        // every chunk's CRC is right (read the chunks back)
        size_t pos = 8;
        const auto be32 = [&](size_t at) { return (uint32_t{static_cast<unsigned char>(png[at])} << 24) | (uint32_t{static_cast<unsigned char>(png[at + 1])} << 16) | (uint32_t{static_cast<unsigned char>(png[at + 2])} << 8) | uint32_t{static_cast<unsigned char>(png[at + 3])}; };
        int chunks = 0;
        while (pos + 12 <= png.size()) {
            const uint32_t length = be32(pos);
            ASSERT_TRUE(pos + 12 + length <= png.size());
            uint32_t crc = 0xFFFFFFFFu;
            for (size_t i = pos + 4; i < pos + 8 + length; ++i) {
                crc ^= static_cast<unsigned char>(png[i]);
                for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            }
            ASSERT_EQ(~crc, be32(pos + 8 + length));
            pos += 12 + length;
            ++chunks;
        }
        ASSERT_TRUE(chunks == 4 && pos == png.size());
        ASSERT_TRUE(rig.store->preview_png(std::string(32, 'f')).empty());
        ASSERT_TRUE(rig.store->preview_png("../../x").empty());
        fs::remove(fs::path(rig.config.dir) / "files" / (o.submission.id + ".lvl"));
        ASSERT_TRUE(rig.store->preview_png(o.submission.id).empty());
    } TEST_END();

    TEST_CASE("SUB7.1 Old things go: the file of a declined map after 30 days (the row stays), the row of a declined or withdrawn one after 90, the counters of old days; what is approved or waiting never goes") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 100; c.per_day_total = 1000; c.max_waiting = 100; });
        const SubmitOutcome a = rig.send("Old Declined");
        const SubmitOutcome b = rig.send("Old Approved");
        const SubmitOutcome c = rig.send("Old Waiting");
        Submission s;
        Failure why;
        ASSERT_TRUE(rig.store->decline(a.submission.id, "No", "42", "Dave", rig.now, s, why));
        ASSERT_TRUE(rig.approve(b.submission.id));
        rig.store->maintenance(rig.now + 29 * 86400);
        ASSERT_EQ(rig.files().size(), size_t{3});
        rig.store->maintenance(rig.now + 31 * 86400);
        ASSERT_EQ(rig.files().size(), size_t{2});
        ASSERT_EQ(rig.text("SELECT file_kept FROM map_submissions WHERE id = '" + a.submission.id + "'"), std::string("0"));
        ASSERT_TRUE(rig.store->preview_png(a.submission.id).empty());
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions"), std::string("3"));
        rig.store->maintenance(rig.now + 91 * 86400);
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions"), std::string("2"));
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM map_submissions WHERE id = '" + b.submission.id + "' OR id = '" + c.submission.id + "'"), std::string("2"));
        ASSERT_EQ(rig.text("SELECT COUNT(*) FROM submission_limits"), std::string("0"));
    } TEST_END();

    TEST_CASE("SUB8.1 A database that fails is a clean 503 with nothing half done: no file stays, the server does not stop") {
        Sub rig;
        ASSERT_TRUE(rig.send("Before It Breaks").ok);
        rig.database.exec("DROP TABLE submission_limits");
        const SubmitOutcome o = rig.send("After It Broke", "203.0.113.77");
        ASSERT_TRUE(!o.ok && o.fail.status == 503);
        ASSERT_EQ(rig.files().size(), size_t{1});
        rig.database.exec("DROP TABLE map_submissions");
        std::vector<Submission> list;
        Failure why;
        ASSERT_FALSE(rig.store->list("waiting", list, why));
        ASSERT_EQ(why.status, 503);
        ASSERT_TRUE(rig.store->approved_maps().empty());
        ASSERT_FALSE(rig.store->take_notices().empty());
        for (const std::string& line : rig.store->take_notices()) ASSERT_TRUE(line.find("203.0.113") == std::string::npos);
        db::Database closed;
        SubmissionStore without(closed, rig.config, nullptr);
        std::string reason;
        ASSERT_FALSE(without.open(kT0, reason));
        const SubmitOutcome off = without.submit(rig.input(tiny()), kT0);
        ASSERT_TRUE(!off.ok && off.fail.code == "submissions_off");
    } TEST_END();

    TEST_CASE("SUB8.3 A database that is full (no page left to grow into) answers 503 and keeps no file of the refused map; what was kept is still there and the store goes on answering") {
        Sub rig([](SubmissionConfig& c) { c.per_address_per_day = 1000; c.per_day_total = 100000; c.max_waiting = 100000; });
        ASSERT_TRUE(rig.send("Before It Is Full").ok);
        rig.database.exec("CREATE TABLE filler (x TEXT)");
        const auto pages = [&]() { return std::stoll(rig.text("PRAGMA page_count")); };
        const int64_t start = pages();
        for (int i = 0; i < 400 && pages() == start; ++i) rig.database.exec("INSERT INTO filler VALUES (hex(randomblob(500)))");
        rig.database.exec("PRAGMA max_page_count = " + std::to_string(pages()));            // (the file may not grow any more)
        int accepted = 1;
        int refused = 0;
        for (int i = 0; i < 60 && refused == 0; ++i) {
            rig.now += 1;
            const SubmitOutcome o = rig.send("Map " + std::to_string(i + 1), "198.51.100." + std::to_string(i % 200 + 1));
            if (o.ok) ++accepted;
            else {
                ++refused;
                ASSERT_TRUE(o.fail.status == 503 && o.fail.code == "storage_error");
            }
        }
        ASSERT_EQ(refused, 1);                                                               // (a page was needed at last)
        ASSERT_EQ(rig.files().size(), static_cast<size_t>(accepted));                        // (and the refused map left no file)
        std::vector<Submission> list;
        Failure why;
        ASSERT_TRUE(rig.store->list("waiting", list, why) && list.size() == static_cast<size_t>(accepted));
        ASSERT_EQ(rig.store->counts().waiting, static_cast<size_t>(accepted));
        ASSERT_FALSE(rig.store->take_notices().empty());
    } TEST_END();

    TEST_CASE("SUB8.2 The folder: a stale temporary file of a crash is removed at the start; a folder that cannot be made is a clear answer, not a crash") {
        TempDir dir;
        db::Database database;
        open_server_db(database, "");
        SubmissionConfig config;
        config.dir = (dir.path / "s").string();
        fs::create_directories(fs::path(config.dir) / "files");
        std::ofstream((fs::path(config.dir) / "files" / ".tmp-abc").string()) << "half a map";
        std::ofstream((fs::path(config.dir) / "files" / "keep.txt").string()) << "something else";
        SubmissionStore store(database, config, nullptr);
        std::string why;
        ASSERT_TRUE(store.open(kT0, why));
        ASSERT_FALSE(fs::exists(fs::path(config.dir) / "files" / ".tmp-abc"));
        ASSERT_TRUE(fs::exists(fs::path(config.dir) / "files" / "keep.txt"));
        std::ofstream((dir.path / "file").string()) << "a file, not a folder";
        SubmissionConfig blocked;
        blocked.dir = (dir.path / "file" / "inside").string();
        SubmissionStore store2(database, blocked, nullptr);
        ASSERT_FALSE(store2.open(kT0, why));
        ASSERT_FALSE(why.empty());
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

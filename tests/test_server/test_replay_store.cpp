// Tests of the store of the replays that the game server keeps (ants_server/replay_store.hpp): the file names and what they say, a match that is kept (the name, the index, the bytes on the disk),
// the age limit (from the name, never from the file's time), the size limit (the oldest first), the hour's limit on saves, the free space of the disk, a folder that holds other files (they
// are left alone), the half-written files of a crash, a file that cannot be read, and the lookups (only by a name of the index, never a path).
//
// No network and no match: the files are small replays that the test makes with replay::encode, and the clock and the free space of the disk are the test's own.
#include "ants_replay/replay.hpp"
#include "ants_server/replay_store.hpp"
#include "ants_sim/command.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ANTS_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_GETPID getpid
#endif

using namespace ants;
using namespace ants::server;
namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace {

constexpr int64_t kT0 = 1791469929;       // 2026-10-08 14:32:09 UTC
constexpr int64_t kHour = 3600;
constexpr int64_t kDay = 86400;
const std::string kName0 = "ants-TREASURE-20261008-143209Z.antsrep";      // a match of TREASURE.LVL that ended at kT0

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("ants_test_replay_store_" + std::to_string(ANTS_GETPID()) + "_" + std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool any_line_has(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& l : lines) {
        if (contains(l, part)) return true;
    }
    return false;
}

std::vector<uint8_t> slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_text(const fs::path& path, const std::string& text) { write_file(path, std::vector<uint8_t>(text.begin(), text.end())); }

// The files in a folder (names only), sorted
std::vector<std::string> names_in(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir)) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

// A small replay as the game server writes one: a map, the seats that play, `turns` turns and, when `padding` says so, enough orders to make the file at least that many bytes long. `seed` makes
// two files differ.
std::vector<uint8_t> make_file(const std::string& map = "TREASURE.LVL", uint32_t turns = 700, bool over = true, size_t at_least = 0, uint32_t seed = 1, uint8_t roster = 0x05, bool complete = true) {
    replay::Replay r;
    r.head.game_version = "v0.0.0";
    r.head.build_id = "test";
    r.head.venue = "game server";
    r.head.map_name = map;
    r.head.map_hash = 0x1122334455667788ull;
    r.head.seed = seed;
    r.head.roster = roster;
    r.head.names[2] = "Bot (Medium)";
    r.complete = complete;
    r.total_turns = turns;
    r.match_over = over;
    r.final_hash = 77;
    r.hashes.assign(turns / replay::kHashPeriodTurns, 0xABCDu);
    std::string error;
    std::vector<uint8_t> bytes = replay::encode(r, error);
    // an order is 9 bytes in the file: add what is missing, and what is still missing after that (the chunks have their own bytes)
    for (uint32_t count = 0; bytes.size() < at_least && count < 100000;) {
        count += static_cast<uint32_t>((at_least - bytes.size()) / 9 + 1);
        r.commands.clear();
        for (uint32_t i = 0; i < count; ++i) {
            sim::Command c;
            c.type = sim::CommandType::Hatch;
            c.issuer = 0;
            r.commands.push_back(replay::TimedCommand{i * turns / count, c});
        }
        bytes = replay::encode(r, error);
    }
    return bytes;
}

// A store in a folder of the test, on the test's clock and disk
struct Fixture {
    TempDir temp;
    fs::path dir;
    int64_t now{kT0};
    uint64_t free_bytes{1ull << 40};
    bool free_known{true};
    int free_asked{0};

    Fixture() : dir(temp.path / "replays") {}

    ReplayConfig config() {
        ReplayConfig c;
        c.dir = dir.string();
        c.game_version = "v0.0.0";
        c.build_id = "test";
        c.clock_s = [this]() { return now; };
        c.free_bytes = [this](const std::string&, uint64_t& available) {
            ++free_asked;
            available = free_bytes;
            return free_known;
        };
        c.min_free_bytes = 0;
        return c;
    }
};

}  // namespace

int main() {
    std::cout << "=======================================================\n REPLAY STORE SUITE (the files of the matches that the server keeps)\n=======================================================\n";

    // ---- the names ---------------------------------------------------------------------------------------------------------------

    TEST_CASE("RS1.1 A Name Is The Store's Own Only When It Reads ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z[-<n>].antsrep With A Real UTC Time; Anything Else (A Path, A Temporary File, Another Case, A Date That Does Not Exist) Is Not") {
        for (const char* good : {"ants-TREASURE-20261008-143209Z.antsrep", "ants-a_B9-20261008-143209Z-2.antsrep", "ants-X-19700101-000000Z.antsrep", "ants-X-20240229-235959Z.antsrep",
                                 "ants-ABCDEFGHIJKLMNOPQRSTUVWX-20261008-143209Z.antsrep", "ants-X-20261231-235959Z-9999.antsrep", "ants-X-99991231-235959Z.antsrep"}) {
            ASSERT_TRUE(ReplayStore::valid_file_name(good));
        }
        const std::string with_nul = std::string("ants-X-20261008-143209Z.antsrep") + std::string(1, '\0');
        for (const std::string& bad : std::vector<std::string>{
                 "", "ants-.antsrep", "ants--20261008-143209Z.antsrep", "ants-X-20261008-143209.antsrep", "ants-X-20261008-143209Z.antsrep.tmp", "ants-X-20261008-143209Z-1.antsrep",
                 "ants-X-20261008-143209Z-02.antsrep", "ants-X-20261008-143209Z-10000.antsrep", "ants-X-20261008-143209Z-.antsrep", "ants-X-20261308-143209Z.antsrep",
                 "ants-X-20260230-143209Z.antsrep", "ants-X-20250229-143209Z.antsrep", "ants-X-20261008-246060Z.antsrep", "ants-X-20261008-240000Z.antsrep",
                 "ants-X-20261008-146009Z.antsrep", "ants-X-20261008-143260Z.antsrep", "ants-X-19691231-235959Z.antsrep", "ants-X-20260008-143209Z.antsrep", "ants-X-20261000-143209Z.antsrep",
                 "ants-X-20261032-143209Z.antsrep", "ants-X Y-20261008-143209Z.antsrep", "ants-X.Y-20261008-143209Z.antsrep", "ants-X-20261008-143209Z.ANTSREP", "ANTS-X-20261008-143209Z.antsrep",
                 "../ants-X-20261008-143209Z.antsrep", "dir/ants-X-20261008-143209Z.antsrep", "dir\\ants-X-20261008-143209Z.antsrep", "/ants-X-20261008-143209Z.antsrep",
                 "ants-ABCDEFGHIJKLMNOPQRSTUVWXY-20261008-143209Z.antsrep", "ants-\xC3\x89-20261008-143209Z.antsrep", "ants-X-2026100a-143209Z.antsrep", "ants-X-20261008-14320aZ.antsrep",
                 "ants-X-20261008_143209Z.antsrep", "ants-X-20261008-143209z.antsrep", " ants-X-20261008-143209Z.antsrep", "ants-X-20261008-143209Z.antsrep ", with_nul}) {
            ASSERT_FALSE(ReplayStore::valid_file_name(bad));
        }
    } TEST_END();

    TEST_CASE("RS1.2 A Name Carries The UTC Time The Match Ended, The Same Whether Or Not It Has A \"-n\"; The Calendar Is Right (A Leap Day, The Epoch, A Year's End)") {
        int64_t t = 0;
        ASSERT_TRUE(ReplayStore::time_of_name(kName0, t) && t == kT0);
        ASSERT_TRUE(ReplayStore::time_of_name("ants-TREASURE-20261008-143209Z-3.antsrep", t) && t == kT0);
        ASSERT_TRUE(ReplayStore::time_of_name("ants-X-19700101-000000Z.antsrep", t) && t == 0);
        ASSERT_TRUE(ReplayStore::time_of_name("ants-X-20240229-235959Z.antsrep", t) && t == 1709251199);
        ASSERT_TRUE(ReplayStore::time_of_name("ants-X-20261231-235959Z.antsrep", t) && t == 1798761599);
        ASSERT_TRUE(ReplayStore::time_of_name("ants-X-20000101-000000Z.antsrep", t) && t == 946684800);
        t = 12345;
        ASSERT_FALSE(ReplayStore::time_of_name("ants-X-20260230-143209Z.antsrep", t));
        ASSERT_EQ(t, 12345);                                                            // (nothing is written for a name that is not one)
    } TEST_END();

    // ---- a match that is kept ------------------------------------------------------------------------------------------------------------

    TEST_CASE("RS2.1 A Match Is Kept Under A Name With The Map And The End Time: The Folder Is Made, The Bytes On The Disk Are The Bytes Given, The Index Says What The Head And The End Say, Nothing Else Is Left Behind") {
        Fixture f;
        ReplayStore store(f.config());
        ASSERT_FALSE(store.enabled());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_TRUE(store.enabled() && fs::is_directory(f.dir) && store.count() == 0 && store.total_bytes() == 0);
        const std::vector<uint8_t> bytes = make_file();
        const ReplaySave saved = store.save(bytes);
        ASSERT_TRUE(saved.kept && saved.note.empty());
        ASSERT_EQ(saved.file, kName0);
        ASSERT_EQ(saved.bytes, bytes.size());
        ASSERT_TRUE(slurp(f.dir / kName0) == bytes);
        ASSERT_EQ(names_in(f.dir), std::vector<std::string>{kName0});                    // (no ".tmp")
        ASSERT_EQ(store.count(), size_t{1});
        ASSERT_EQ(store.total_bytes(), uint64_t{bytes.size()});
        const std::vector<ReplayEntry> list = store.list();
        ASSERT_EQ(list.size(), size_t{1});
        const ReplayEntry& e = list[0];
        ASSERT_EQ(e.file, kName0);
        ASSERT_TRUE(e.readable && e.finished);
        ASSERT_EQ(e.bytes, uint64_t{bytes.size()});
        ASSERT_EQ(e.ended_s, kT0);
        ASSERT_EQ(e.map, std::string("TREASURE.LVL"));
        ASSERT_EQ(e.rules, net::kProtocolVersion);
        ASSERT_EQ(e.game, std::string("v0.0.0"));
        ASSERT_EQ(e.turns, uint32_t{700});
        ASSERT_EQ(e.players, (std::vector<std::string>{"Green", "Blue (Bot (Medium))"}));      // a person is a colour, a computer player has its name
        const ReplayEntry* found = store.find(kName0);
        ASSERT_TRUE(found != nullptr && found->bytes == bytes.size() && found->ended_s == kT0);
        ASSERT_TRUE(any_line_has(store.take_notes(), "0 file(s)"));                      // (what prepare found, said once)
        ASSERT_TRUE(store.take_notes().empty());
    } TEST_END();

    TEST_CASE("RS2.2 The Map's Name Is Made Into A Safe Stem (Letters, Digits And _): A Space, A Dash Or A Dot Becomes _, The Extension Goes, A Long Name Is Cut To 24 Characters") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_EQ(store.save(make_file("Big Map-2.LVL")).file, std::string("ants-Big_Map_2-20261008-143209Z.antsrep"));
        f.now += 1;
        ASSERT_EQ(store.save(make_file("a.b.lvl")).file, std::string("ants-a_b-20261008-143210Z.antsrep"));
        f.now += 1;
        ASSERT_EQ(store.save(make_file("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.LVL")).file, std::string("ants-ABCDEFGHIJKLMNOPQRSTUVWX-20261008-143211Z.antsrep"));
        for (const ReplayEntry& e : store.list()) ASSERT_TRUE(ReplayStore::valid_file_name(e.file));
    } TEST_END();

    TEST_CASE("RS2.3 Matches Of One Map That End In One Second Get -2, -3 ...: Nothing Is Replaced (Not A File Of The Store, Not A Foreign File With That Name), And The List Shows The One Kept Last First") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> a = make_file("TREASURE.LVL", 700, true, 0, 1);
        const std::vector<uint8_t> b = make_file("TREASURE.LVL", 700, true, 0, 2);
        const std::vector<uint8_t> c = make_file("TREASURE.LVL", 700, true, 0, 3);
        ASSERT_EQ(store.save(a).file, kName0);
        ASSERT_EQ(store.save(b).file, std::string("ants-TREASURE-20261008-143209Z-2.antsrep"));
        ASSERT_EQ(store.save(c).file, std::string("ants-TREASURE-20261008-143209Z-3.antsrep"));
        ASSERT_TRUE(slurp(f.dir / kName0) == a);
        ASSERT_TRUE(slurp(f.dir / "ants-TREASURE-20261008-143209Z-2.antsrep") == b);
        ASSERT_TRUE(slurp(f.dir / "ants-TREASURE-20261008-143209Z-3.antsrep") == c);
        const std::vector<ReplayEntry> list = store.list();
        ASSERT_TRUE(list.size() == 3 && list[0].file == "ants-TREASURE-20261008-143209Z-3.antsrep" && list[1].file == "ants-TREASURE-20261008-143209Z-2.antsrep" && list[2].file == kName0);
        // a file that someone put into the folder after the store was opened, under the name that the next match would take
        write_text(f.dir / "ants-TREASURE-20261008-143210Z.antsrep", "someone else's file");
        f.now += 1;
        ASSERT_EQ(store.save(a).file, std::string("ants-TREASURE-20261008-143210Z-2.antsrep"));
        const std::string theirs = "someone else's file";
        ASSERT_TRUE(slurp(f.dir / "ants-TREASURE-20261008-143210Z.antsrep") == std::vector<uint8_t>(theirs.begin(), theirs.end()));      // (not replaced)
        // the order after a restart is the same as the order before it (the folder holds the store's files only: a file with a name of the store's own is the store's, readable or not)
        fs::remove(f.dir / "ants-TREASURE-20261008-143210Z.antsrep");
        ReplayStore again(f.config());
        ASSERT_TRUE(again.prepare(why));
        std::vector<std::string> before;
        std::vector<std::string> after;
        for (const ReplayEntry& e : store.list()) before.push_back(e.file);
        for (const ReplayEntry& e : again.list()) after.push_back(e.file);
        ASSERT_EQ(before, after);
    } TEST_END();

    TEST_CASE("RS2.4 A Clock That Is Wrong Does Not Make A Name That The Store Would Not Read Again (Before 1970 Is 1970, After 9999 Is The End Of 9999)") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        f.now = -5;
        const ReplaySave early = store.save(make_file());
        ASSERT_TRUE(early.kept && ReplayStore::valid_file_name(early.file));
        ASSERT_EQ(early.file, std::string("ants-TREASURE-19700101-000000Z.antsrep"));
        f.now = 400000000000ll;
        const ReplaySave late = store.save(make_file("TREASURE.LVL", 700, true, 0, 2));
        ASSERT_TRUE(late.kept && ReplayStore::valid_file_name(late.file));
        ASSERT_EQ(late.file, std::string("ants-TREASURE-99991231-235959Z.antsrep"));
    } TEST_END();

    // ---- refusals -----------------------------------------------------------------------------------------------------------------

    TEST_CASE("RS3.1 A Recording That Is No Whole Replay File Is Refused With A Reason And Writes Nothing: Nothing, Garbage, A Cut File, A File Without Its End, A File Past 1 MiB; The Same Refusal Is Told Once And Then Counted") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        store.take_notes();
        const std::vector<uint8_t> good = make_file();
        std::vector<uint8_t> cut(good.begin(), good.end() - 5);
        std::vector<uint8_t> flipped = good;
        flipped[flipped.size() / 2] ^= 0x10;
        const std::vector<uint8_t> unfinished = make_file("TREASURE.LVL", 700, true, 0, 1, 0x05, false);
        const std::vector<uint8_t> huge(replay::kMaxFileBytes + 1, 0x41);
        const std::vector<const std::vector<uint8_t>*> broken = {&cut, &flipped, &unfinished, &huge};
        for (const std::vector<uint8_t>* bytes : broken) {
            const ReplaySave refused = store.save(*bytes);
            ASSERT_FALSE(refused.kept);
            ASSERT_TRUE(!refused.note.empty() && refused.file.empty());
        }
        ASSERT_FALSE(store.save(std::vector<uint8_t>()).kept);
        ASSERT_FALSE(store.save(std::vector<uint8_t>{1, 2, 3, 4, 5}).kept);
        ASSERT_TRUE(contains(store.save(unfinished).note, "no end"));
        ASSERT_TRUE(names_in(f.dir).empty() && store.count() == 0 && store.total_bytes() == 0);
        const std::vector<std::string> notes = store.take_notes();
        ASSERT_TRUE(any_line_has(notes, "a match was not kept") && any_line_has(notes, "repeated"));
        ASSERT_TRUE(notes.size() < 9);                                                   // (nine refusals were not nine lines)
        ASSERT_TRUE(store.save(good).kept);                                              // (and a good file is still kept after them)
    } TEST_END();

    TEST_CASE("RS3.2 A Store That Was Not Prepared (Or Could Not Be) Keeps Nothing: Save Says So And Writes Nothing; A Folder That Cannot Be Made, Or Is A File, Is A Reason") {
        Fixture f;
        ReplayStore unprepared(f.config());
        const ReplaySave refused = unprepared.save(make_file());
        ASSERT_TRUE(!refused.kept && refused.note == "this server keeps no replays");
        ASSERT_FALSE(fs::exists(f.dir));
        unprepared.update();
        ASSERT_TRUE(unprepared.list().empty() && unprepared.purge() == 0 && unprepared.count() == 0 && unprepared.take_notes().empty());
    } TEST_END();

    TEST_CASE("RS3.3 A Folder That Is Not There Is Made (With Its Parents); An Empty Name, And A Name That Is A File, Are Refused With The Reason") {
        Fixture f;
        ReplayConfig deep = f.config();
        deep.dir = (f.temp.path / "a" / "b" / "replays").string();
        ReplayStore made(deep);
        std::string why;
        ASSERT_TRUE(made.prepare(why) && fs::is_directory(f.temp.path / "a" / "b" / "replays"));
        ReplayConfig none = f.config();
        none.dir.clear();
        ReplayStore empty(none);
        ASSERT_FALSE(empty.prepare(why));
        ASSERT_TRUE(!why.empty() && !empty.enabled());
        write_text(f.temp.path / "plain", "a file");
        ReplayConfig file = f.config();
        file.dir = (f.temp.path / "plain").string();
        ReplayStore on_file(file);
        why.clear();
        ASSERT_FALSE(on_file.prepare(why));
        ASSERT_TRUE(contains(why, "cannot be made or is not a folder") && !on_file.enabled());
        ASSERT_FALSE(on_file.save(make_file()).kept);
    } TEST_END();

    // ---- the age limit -------------------------------------------------------------------------------------------------------------

    TEST_CASE("RS4.1 A File Is Deleted keep_days After The Match Ended: Not A Second Before, At That Second On; The Age Is Looked At Once An Hour") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.keep_days = 1;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_TRUE(store.save(make_file("TREASURE.LVL", 700, true, 0, 1)).kept);        // ended at T0
        f.now = kT0 + 100;
        const ReplaySave second = store.save(make_file("TREASURE.LVL", 700, true, 0, 2));
        ASSERT_TRUE(second.kept);                                                        // ended at T0 + 100
        f.now = kT0 + kDay - 1;                                                          // (the first is one second short of a day old)
        store.update();
        ASSERT_EQ(store.count(), size_t{2});
        ASSERT_EQ(store.purge(), size_t{0});
        f.now = kT0 + kDay;                                                              // (exactly a day)
        ASSERT_EQ(store.purge(), size_t{1});
        ASSERT_FALSE(fs::exists(f.dir / kName0));
        ASSERT_TRUE(fs::exists(f.dir / second.file) && store.count() == 1);
        // update() looks once an hour: a file that has become old since the last look stays until the hour is up
        Fixture g;
        ReplayConfig cfg2 = g.config();
        cfg2.keep_days = 1;
        ReplayStore watched(cfg2);
        ASSERT_TRUE(watched.prepare(why));
        ASSERT_TRUE(watched.save(make_file("TREASURE.LVL", 700, true, 0, 1)).kept);      // F: ended T0
        g.now = kT0 + 100;
        ASSERT_TRUE(watched.save(make_file("TREASURE.LVL", 700, true, 0, 2)).kept);     // G: ended T0 + 100
        g.now = kT0 + kDay + 50;                                                         // F is old, G is not (50 s short)
        watched.update();                                                                // (an hour has passed since prepare: it looks)
        ASSERT_EQ(watched.count(), size_t{1});
        g.now = kT0 + kDay + 100;                                                        // G is old now, but the look was 50 s ago
        watched.update();
        ASSERT_EQ(watched.count(), size_t{1});
        g.now = kT0 + kDay + 50 + kHour;
        watched.update();
        ASSERT_EQ(watched.count(), size_t{0});
        ASSERT_TRUE(names_in(g.dir).empty());
    } TEST_END();

    TEST_CASE("RS4.2 The Age Is The One In The Name, Never The File's Time: A Fresh File With An Old Name Is Deleted When The Store Opens, An Old File With A New Name Stays") {
        Fixture f;
        fs::create_directories(f.dir);
        const std::vector<uint8_t> bytes = make_file();
        write_file(f.dir / "ants-OLD-20260101-000000Z.antsrep", bytes);                   // (written just now)
        write_file(f.dir / "ants-NEW-20261008-143000Z.antsrep", bytes);
        fs::last_write_time(f.dir / "ants-NEW-20261008-143000Z.antsrep", fs::file_time_type::clock::now() - std::chrono::hours(24 * 365 * 5));
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_FALSE(fs::exists(f.dir / "ants-OLD-20260101-000000Z.antsrep"));
        ASSERT_TRUE(fs::exists(f.dir / "ants-NEW-20261008-143000Z.antsrep") && store.count() == 1);
        ASSERT_TRUE(any_line_has(store.take_notes(), "1 file(s) deleted"));
    } TEST_END();

    // ---- the size limit ------------------------------------------------------------------------------------------------------------

    TEST_CASE("RS5.1 The Files Together Never Pass The Limit: A New File Pushes The Oldest Out First; A File Bigger Than The Whole Store Is Refused And Nothing Is Deleted For It; The Smallest Limit Is 1 KiB") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.max_bytes = 1;                                                               // (held to 1 KiB)
        ReplayStore store(cfg);
        ASSERT_EQ(store.config().max_bytes, uint64_t{1024});
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> one = make_file("TREASURE.LVL", 700, true, 400, 1);
        const std::vector<uint8_t> two = make_file("TREASURE.LVL", 700, true, 400, 2);
        const std::vector<uint8_t> three = make_file("TREASURE.LVL", 700, true, 400, 3);
        ASSERT_TRUE(one.size() >= 400 && one.size() <= 512 && two.size() <= 512 && three.size() <= 512);
        ASSERT_TRUE(store.save(one).kept);
        f.now += 1;
        ASSERT_TRUE(store.save(two).kept);
        ASSERT_TRUE(store.count() == 2 && store.total_bytes() == one.size() + two.size() && store.total_bytes() <= 1024);
        f.now += 1;
        const ReplaySave third = store.save(three);
        ASSERT_TRUE(third.kept);
        ASSERT_TRUE(store.count() == 2 && store.total_bytes() <= 1024);
        ASSERT_FALSE(fs::exists(f.dir / kName0));                                        // the oldest went
        ASSERT_TRUE(fs::exists(f.dir / "ants-TREASURE-20261008-143210Z.antsrep") && fs::exists(f.dir / third.file));
        const std::vector<uint8_t> bigger = make_file("TREASURE.LVL", 700, true, 1100, 4);
        ASSERT_TRUE(bigger.size() > 1024);
        f.now += 1;
        const ReplaySave refused = store.save(bigger);
        ASSERT_TRUE(!refused.kept && contains(refused.note, "bigger than the whole store"));
        ASSERT_TRUE(store.count() == 2 && names_in(f.dir).size() == 2);                  // (and the others stay)
        // the biggest limit is 4 GiB, whatever is asked
        ReplayConfig huge = f.config();
        huge.max_bytes = 1ull << 50;
        ASSERT_EQ(ReplayStore(huge).config().max_bytes, uint64_t{4096} * 1024 * 1024);
    } TEST_END();

    TEST_CASE("RS5.2 The Size Limit Is Kept When The Store Opens On A Folder That Holds More (The Oldest Go First); keep_days Is Held To 1 .. 3650") {
        Fixture f;
        fs::create_directories(f.dir);
        const std::vector<uint8_t> bytes = make_file("TREASURE.LVL", 700, true, 400, 1);
        for (int i = 0; i < 4; ++i) write_file(f.dir / ("ants-OLDMAP-2026100" + std::to_string(5 + i) + "-120000Z.antsrep"), bytes);
        ReplayConfig cfg = f.config();
        cfg.max_bytes = 1024;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_TRUE(store.count() == 2 && store.total_bytes() <= 1024);
        ASSERT_EQ(names_in(f.dir), (std::vector<std::string>{"ants-OLDMAP-20261007-120000Z.antsrep", "ants-OLDMAP-20261008-120000Z.antsrep"}));
        ReplayConfig days = f.config();
        days.keep_days = 0;
        ASSERT_EQ(ReplayStore(days).config().keep_days, uint32_t{1});
        days.keep_days = 100000;
        ASSERT_EQ(ReplayStore(days).config().keep_days, uint32_t{3650});
    } TEST_END();

    // ---- the hour's limit and the disk ----------------------------------------------------------------------------------------------

    TEST_CASE("RS6.1 No More Than max_saves_per_hour Matches Are Kept In A Rolling Hour (A Flood Of Short Matches Cannot Push The Others Out): The Next Is Refused With The Reason, Writes And Deletes Nothing, And The Hour Rolls On") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.max_saves_per_hour = 3;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        for (uint32_t i = 0; i < 3; ++i) {
            ASSERT_TRUE(store.save(make_file("TREASURE.LVL", 700, true, 0, i + 1)).kept);
            f.now += 1;
        }
        const uint64_t total = store.total_bytes();
        const ReplaySave refused = store.save(make_file("TREASURE.LVL", 700, true, 0, 9));
        ASSERT_TRUE(!refused.kept && contains(refused.note, "3 kept matches an hour"));
        ASSERT_TRUE(store.count() == 3 && store.total_bytes() == total && names_in(f.dir).size() == 3);
        f.now = kT0 + kHour - 1;                                                         // (the first was kept 3599 s ago)
        ASSERT_FALSE(store.save(make_file("TREASURE.LVL", 700, true, 0, 9)).kept);
        f.now = kT0 + kHour;                                                             // (3600 s: it has left the hour)
        ASSERT_TRUE(store.save(make_file("TREASURE.LVL", 700, true, 0, 9)).kept);
        ASSERT_FALSE(store.save(make_file("TREASURE.LVL", 700, true, 0, 10)).kept);      // (three are in the hour again: the second, the third and the one just kept)
    } TEST_END();

    TEST_CASE("RS6.2 The Disk Is Asked Before A Write: A Match Is Kept Only When The Disk Would Keep min_free_bytes Free After It; An Answer That Cannot Be Had Does Not Stop The Store; With min_free_bytes 0 The Disk Is Not Asked") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.min_free_bytes = 1000;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> bytes = make_file();
        f.free_bytes = 1000 + bytes.size() - 1;
        const ReplaySave refused = store.save(bytes);
        ASSERT_TRUE(!refused.kept && contains(refused.note, "less than") && contains(refused.note, "free"));
        ASSERT_TRUE(names_in(f.dir).empty() && store.count() == 0);
        f.free_bytes = 1000 + bytes.size();
        ASSERT_TRUE(store.save(bytes).kept);
        f.now += 1;
        f.free_known = false;
        f.free_bytes = 0;
        ASSERT_TRUE(store.save(bytes).kept);                                             // (the disk could not be asked: the match is kept)
        Fixture g;
        ReplayStore unasked(g.config());
        ASSERT_TRUE(unasked.prepare(why) && unasked.save(bytes).kept);
        ASSERT_EQ(g.free_asked, 0);
    } TEST_END();

    // ---- a folder that holds other things ---------------------------------------------------------------------------------------------

    TEST_CASE("RS7.1 Opening A Folder: The Half-Written Files Of A Crash Are Deleted, A File Of Another Name, A Folder And A Link Are Left Alone And Not Counted, A File That Cannot Be Read Is Counted And Kept Until It Is Old") {
        Fixture f;
        fs::create_directories(f.dir);
        const std::vector<uint8_t> bytes = make_file();
        write_file(f.dir / "ants-GOOD-20261007-120000Z.antsrep", bytes);
        write_file(f.dir / "ants-HALF-20261008-120000Z.antsrep.tmp", std::vector<uint8_t>(bytes.begin(), bytes.begin() + 40));
        write_text(f.dir / "ants-JUNK-20261008-100000Z.antsrep", "this is not a replay");
        write_text(f.dir / "notes.txt", "not ours");
        write_text(f.dir / "ants-bad-name.antsrep", "not ours either");
        write_text(f.dir / "other.tmp", "not ours, though it ends like a temporary file");
        fs::create_directories(f.dir / "ants-DIR-20261008-143209Z.antsrep");
        write_text(f.dir / "ants-DIR-20261008-143209Z.antsrep" / "inside", "x");
#ifndef _WIN32
        fs::create_symlink(f.dir / "ants-GOOD-20261007-120000Z.antsrep", f.dir / "ants-LINK-20261008-110000Z.antsrep");
#endif
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_EQ(store.count(), size_t{2});                                             // the good file and the junk
        ASSERT_EQ(store.total_bytes(), uint64_t{bytes.size() + 20});
        ASSERT_FALSE(fs::exists(f.dir / "ants-HALF-20261008-120000Z.antsrep.tmp"));
        for (const char* kept : {"notes.txt", "ants-bad-name.antsrep", "other.tmp", "ants-DIR-20261008-143209Z.antsrep/inside"}) ASSERT_TRUE(fs::exists(f.dir / kept));
#ifndef _WIN32
        ASSERT_TRUE(fs::is_symlink(f.dir / "ants-LINK-20261008-110000Z.antsrep") && store.find("ants-LINK-20261008-110000Z.antsrep") == nullptr);
#endif
        const ReplayEntry* good = store.find("ants-GOOD-20261007-120000Z.antsrep");
        const ReplayEntry* junk = store.find("ants-JUNK-20261008-100000Z.antsrep");
        ASSERT_TRUE(good != nullptr && good->readable && good->map == "TREASURE.LVL");
        ASSERT_TRUE(junk != nullptr && !junk->readable && junk->bytes == 20 && junk->turns == 0 && junk->players.empty());
        ASSERT_TRUE(store.find("notes.txt") == nullptr && store.find("ants-bad-name.antsrep") == nullptr && store.find("other.tmp") == nullptr);
        const std::vector<std::string> notes = store.take_notes();
        ASSERT_TRUE(any_line_has(notes, "2 file(s)") && any_line_has(notes, "1 half-written") && any_line_has(notes, "1 file(s) that this build cannot read"));
        // the junk is old after a month, like every file
        f.now = kT0 + 31 * kDay;
        ASSERT_EQ(store.purge(), size_t{2});
        ASSERT_TRUE(store.count() == 0 && !fs::exists(f.dir / "ants-JUNK-20261008-100000Z.antsrep") && fs::exists(f.dir / "notes.txt"));
    } TEST_END();

    TEST_CASE("RS7.2 A Store Opened Again Finds The Same Files In The Same Order With The Same Facts; A File Of A Match With Other Rules Is Listed As What It Is") {
        Fixture f;
        std::string why;
        std::vector<ReplayEntry> before;
        {
            ReplayStore store(f.config());
            ASSERT_TRUE(store.prepare(why));
            ASSERT_TRUE(store.save(make_file("TREASURE.LVL", 700, true, 0, 1)).kept);
            f.now += 5;
            ASSERT_TRUE(store.save(make_file("SMALL.LVL", 1500, false, 0, 2)).kept);
            f.now += 5;
            ASSERT_TRUE(store.save(make_file("TINY.LVL", 800, true, 0, 3)).kept);
            before = store.list();
        }
        ReplayStore again(f.config());
        ASSERT_TRUE(again.prepare(why));
        const std::vector<ReplayEntry> after = again.list();
        ASSERT_EQ(after.size(), size_t{3});
        ASSERT_EQ(before.size(), after.size());
        uint64_t sum = 0;
        for (size_t i = 0; i < after.size(); ++i) {
            ASSERT_TRUE(before[i].file == after[i].file && before[i].bytes == after[i].bytes && before[i].ended_s == after[i].ended_s && before[i].map == after[i].map);
            ASSERT_TRUE(before[i].turns == after[i].turns && before[i].finished == after[i].finished && before[i].players == after[i].players && before[i].rules == after[i].rules);
            ASSERT_TRUE(after[i].readable);
            sum += after[i].bytes;
        }
        ASSERT_EQ(again.total_bytes(), sum);
        ASSERT_TRUE(after[0].map == "TINY.LVL" && after[1].map == "SMALL.LVL" && after[2].map == "TREASURE.LVL");      // newest first
        ASSERT_TRUE(!after[1].finished && after[1].turns == 1500 && after[2].finished);
    } TEST_END();

    // ---- looking a file up --------------------------------------------------------------------------------------------------------

    TEST_CASE("RS8.1 A File Is Found, Read And Deleted By A Name Of The Index Only: A Path, A Name That Is Not In The Index And A Foreign File In The Folder Are Not Opened, Read Or Deleted") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> bytes = make_file();
        ASSERT_TRUE(store.save(bytes).kept);
        write_text(f.dir / "notes.txt", "private notes");
        write_text(f.temp.path / "outside.antsrep", "outside the folder");
        std::vector<uint8_t> out{1, 2, 3};
        ASSERT_TRUE(store.read(kName0, out) && out == bytes);
        for (const std::string& bad : std::vector<std::string>{"notes.txt", "../outside.antsrep", "../replays/" + kName0, "./" + kName0, kName0 + "/", "/" + kName0, (f.dir / kName0).string(),
                                                              "ants-TREASURE-20261008-143209Z-2.antsrep", "", "..", "."}) {
            out = {9, 9};
            ASSERT_TRUE(store.find(bad) == nullptr);
            ASSERT_FALSE(store.read(bad, out));
            ASSERT_TRUE(out.empty());                                                    // (nothing of an earlier read is left in it)
            ASSERT_FALSE(store.remove(bad));
        }
        ASSERT_TRUE(fs::exists(f.dir / "notes.txt") && fs::exists(f.temp.path / "outside.antsrep") && fs::exists(f.dir / kName0));
        ASSERT_TRUE(store.remove(kName0));
        ASSERT_TRUE(!fs::exists(f.dir / kName0) && store.count() == 0 && store.total_bytes() == 0);
        ASSERT_FALSE(store.remove(kName0));
    } TEST_END();

    TEST_CASE("RS8.2 A File That Changed On The Disk Since It Was Indexed Is Not Handed Out (Cut Or Longer Than The Index Says); One That Is Gone Is Not Read And Can Be Dropped From The Index") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> bytes = make_file();
        ASSERT_TRUE(store.save(bytes).kept);
        std::vector<uint8_t> out;
        {
            std::ofstream more(f.dir / kName0, std::ios::binary | std::ios::app);
            more << "x";
        }
        ASSERT_FALSE(store.read(kName0, out));
        write_file(f.dir / kName0, std::vector<uint8_t>(bytes.begin(), bytes.end() - 1));
        ASSERT_FALSE(store.read(kName0, out));
        write_file(f.dir / kName0, bytes);
        ASSERT_TRUE(store.read(kName0, out) && out == bytes);
        fs::remove(f.dir / kName0);
        ASSERT_FALSE(store.read(kName0, out));
        ASSERT_TRUE(store.remove(kName0) && store.count() == 0 && store.total_bytes() == 0);
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures
              << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

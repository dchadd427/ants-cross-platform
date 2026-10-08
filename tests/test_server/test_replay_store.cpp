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
#include <set>
#include <string>
#include <system_error>
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
std::vector<uint8_t> make_file(const std::string& map = "TREASURE.LVL", uint32_t turns = 700, bool over = true, size_t at_least = 0, uint32_t seed = 1, uint8_t roster = 0x05, bool complete = true,
                               uint16_t rules = net::kProtocolVersion) {
    replay::Replay r;
    r.head.engine_rules = rules;
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

// A file of exactly `size` bytes (a limit is a limit of bytes, and its edge is one byte): a hash is 8 bytes of the file (one per 100 turns) and an order 9, so some mix of the two makes the size. Empty when none does.
std::vector<uint8_t> make_file_of_size(size_t size, uint32_t seed = 1) {
    for (uint32_t hashes = 1; hashes < 60; ++hashes) {
        for (uint32_t orders = 0; orders < 100; ++orders) {
            replay::Replay r;
            r.head.game_version = "v0.0.0";
            r.head.build_id = "test";
            r.head.venue = "game server";
            r.head.map_name = "TREASURE.LVL";
            r.head.map_hash = 0x1122334455667788ull;
            r.head.seed = seed;
            r.head.roster = 0x05;
            r.head.names[2] = "Bot (Medium)";
            r.complete = true;
            r.total_turns = hashes * replay::kHashPeriodTurns;
            r.match_over = true;
            r.final_hash = 77;
            r.hashes.assign(hashes, 0xABCDu);
            for (uint32_t i = 0; i < orders; ++i) {
                sim::Command c;
                c.type = sim::CommandType::Hatch;
                c.issuer = 0;
                r.commands.push_back(replay::TimedCommand{i, c});
            }
            std::string error;
            std::vector<uint8_t> bytes = replay::encode(r, error);
            if (bytes.size() == size) return bytes;
            if (bytes.size() > size) break;
        }
    }
    return std::vector<uint8_t>();
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
        ASSERT_EQ(e.players, (std::vector<std::string>{"Green", "Blue (Bot (Medium))"}));      // a seat without a name is its colour, a computer player has its name
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
        ASSERT_TRUE(store.find(early.file) != nullptr && store.find(early.file)->ended_s == 0);         // (the index holds the time that the name says, which is what the store reads again, not the clock's)
        {
            ReplayStore again(f.config());
            ASSERT_TRUE(again.prepare(why));
            ASSERT_TRUE(again.find(early.file) != nullptr && again.find(early.file)->ended_s == 0);
        }
        f.now = 400000000000ll;
        const ReplaySave late = store.save(make_file("TREASURE.LVL", 700, true, 0, 2));
        ASSERT_TRUE(late.kept && ReplayStore::valid_file_name(late.file));
        ASSERT_EQ(late.file, std::string("ants-TREASURE-99991231-235959Z.antsrep"));
        ASSERT_TRUE(store.find(late.file) != nullptr && store.find(late.file)->ended_s == 253402300799ll);        // 9999-12-31 23:59:59 UTC
        f.now = 0;
        ReplayStore again2(f.config());
        ASSERT_TRUE(again2.prepare(why));
        ASSERT_TRUE(again2.find(late.file) != nullptr && again2.find(late.file)->ended_s == 253402300799ll);
    } TEST_END();

    TEST_CASE("RS2.5 The Matches Of One Map That End In One Second Can Have The Names Up To -9999; When Every One Of Them Is Taken The Match Is Refused With The Reason, And Nothing Is Replaced") {
        Fixture f;
        fs::create_directories(f.dir);
        const std::string base = "ants-TREASURE-20261008-143209Z";
        write_text(f.dir / (base + ".antsrep"), "x");
        for (int n = 2; n <= 9998; ++n) write_text(f.dir / (base + "-" + std::to_string(n) + ".antsrep"), "x");       // (bytes that are no replay, under names that are the store's own: kept, not readable)
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_EQ(store.count(), size_t{9998});
        const std::vector<uint8_t> last = make_file("TREASURE.LVL", 700, true, 0, 5);
        const ReplaySave kept = store.save(last);                                         // (-9999 is the last name that is free)
        ASSERT_TRUE(kept.kept && kept.file == base + "-9999.antsrep");
        ASSERT_TRUE(slurp(f.dir / kept.file) == last);
        const ReplaySave refused = store.save(make_file("TREASURE.LVL", 700, true, 0, 6));
        ASSERT_TRUE(!refused.kept && contains(refused.note, "every name for a match of this second is taken"));
        ASSERT_TRUE(slurp(f.dir / kept.file) == last);                                    // (the file with the last name is still the one that was kept)
        ASSERT_EQ(store.count(), size_t{9999});
    } TEST_END();

    TEST_CASE("RS2.6 The List Says A Seat's Colour With The Name That The File Holds (\"Green (Ann)\"), The Colour Alone For A Seat Without One, A Computer Player's Name As It Is, And Gives A Name With Quotes And Brackets As Typed; Opening The Store Again Reads The Same") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        replay::Replay r;                                                                 // a match of four, as the server writes it, with the names that its room showed
        r.head.game_version = "v0.0.0";
        r.head.build_id = "test";
        r.head.venue = "game server";
        r.head.map_name = "TREASURE.LVL";
        r.head.map_hash = 0x1122334455667788ull;
        r.head.seed = 5;
        r.head.roster = 0x0F;
        r.head.names = {"Ann", "", "Bot (Medium)", "Q\"x\\y <b>"};
        r.complete = true;
        r.total_turns = 700;
        r.match_over = true;
        r.final_hash = 77;
        r.hashes.assign(7, 0xABCDu);
        std::string error;
        const std::vector<uint8_t> bytes = replay::encode(r, error);
        ASSERT_FALSE(bytes.empty());
        const std::vector<std::string> shown = {"Green (Ann)", "Red", "Blue (Bot (Medium))", "Black (Q\"x\\y <b>)"};
        ASSERT_TRUE(store.save(bytes).kept);
        ASSERT_EQ(store.list().at(0).players, shown);
        ReplayStore again(f.config());                                                    // (the list is read from the file's head again when the store is opened)
        ASSERT_TRUE(again.prepare(why));
        ASSERT_TRUE(again.count() == 1 && again.list().at(0).readable);
        ASSERT_EQ(again.list().at(0).players, shown);
        std::vector<uint8_t> read;
        ASSERT_TRUE(again.read(kName0, read) && read == bytes);
        for (const std::string name : {"Ann", "Bot (Medium)"}) ASSERT_TRUE(std::search(read.begin(), read.end(), name.begin(), name.end()) != read.end());
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
        ASSERT_TRUE(any_line_has(notes, "a match was not kept") && any_line_has(notes, "repeated"));         // (the huge file and the empty one are refused with one text: the count came with the line that followed)
        // The same refusal again and again, as a disk that refuses every match would have it, while the server's loop takes the notes at every pass: told once, and then only counted
        // (the last line told so far is that of the file without an end, which the cut file has too: the damaged file is a line of its own)
        store.report_repeats();
        store.take_notes();
        for (int pass = 0; pass < 3; ++pass) {
            ASSERT_FALSE(store.save(flipped).kept);
            const std::vector<std::string> told = store.take_notes();
            ASSERT_EQ(told.size(), pass == 0 ? size_t{1} : size_t{0});
        }
        f.now += kHour - 1;
        store.update();
        ASSERT_TRUE(store.take_notes().empty());                                         // (the count waits for the hour: it is told an hour after the first repeat)
        f.now += 1;
        store.update();
        std::vector<std::string> counted = store.take_notes();
        ASSERT_TRUE(counted.size() == 1 && contains(counted[0], "repeated 2 more time(s)"));
        ASSERT_FALSE(store.save(flipped).kept);                                          // (and the line is still the last one told: one more repeat is counted, not told again)
        ASSERT_TRUE(store.take_notes().empty());
        ASSERT_FALSE(store.save(huge).kept);                                             // (another line comes: the count first, then the line)
        counted = store.take_notes();
        ASSERT_TRUE(counted.size() == 2 && contains(counted[0], "repeated 1 more time(s)") && contains(counted[1], "a match was not kept"));
        ASSERT_FALSE(store.save(huge).kept);                                             // (a repeat at the stop: told by report_repeats)
        ASSERT_TRUE(store.take_notes().empty());
        store.report_repeats();
        counted = store.take_notes();
        ASSERT_TRUE(counted.size() == 1 && contains(counted[0], "repeated 1 more time(s)"));
        store.report_repeats();
        ASSERT_TRUE(store.take_notes().empty());                                         // (nothing counted: nothing told)
        // a clock that was set back a long way does not keep the count for longer than the hour: it is told at once
        ASSERT_FALSE(store.save(huge).kept);
        f.now -= 5 * kHour;
        store.update();
        counted = store.take_notes();
        ASSERT_TRUE(counted.size() == 1 && contains(counted[0], "repeated 1 more time(s)"));
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

    TEST_CASE("RS3.4 A Disk That Refuses The Write, Or The Name, Costs The Match And Nothing Else: The Match Is Refused With The Reason, No Half File Is Left, The Index And The Count Stay As They Were, And The Next Match Is Kept") {
        Fixture f;
        bool refuse_rename = false;
        ReplayConfig cfg = f.config();
        cfg.rename_file = [&refuse_rename](const std::string& from, const std::string& to, std::error_code& ec) {
            if (refuse_rename) ec = std::make_error_code(std::errc::permission_denied);
            else fs::rename(from, to, ec);
        };
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        store.take_notes();
        const std::vector<uint8_t> good = make_file();
        // the write: a folder is where the temporary file would be (the portable way to make a file that cannot be made), and it holds something so that it cannot be removed
        fs::create_directories(f.dir / (kName0 + ".tmp") / "inside");
        ReplaySave refused = store.save(good);
        ASSERT_TRUE(!refused.kept && refused.file.empty() && contains(refused.note, "refused the write"));
        ASSERT_TRUE(store.count() == 0 && store.total_bytes() == 0 && store.readable_count() == 0);
        ASSERT_TRUE(fs::is_directory(f.dir / (kName0 + ".tmp")) && !fs::exists(f.dir / kName0));
        ASSERT_TRUE(any_line_has(store.take_notes(), "refused the write"));
        fs::remove_all(f.dir / (kName0 + ".tmp"));
        // the name: the file was written whole, the disk will not give it its name; the temporary file is removed again
        refuse_rename = true;
        refused = store.save(good);
        ASSERT_TRUE(!refused.kept && refused.file.empty() && contains(refused.note, "could not be given its name"));
        ASSERT_TRUE(names_in(f.dir).empty() && store.count() == 0 && store.total_bytes() == 0 && store.readable_count() == 0);
        ASSERT_TRUE(any_line_has(store.take_notes(), "could not be given its name"));
        // the disk comes right: the same match is kept under the same name, and the refusals did not use the hour's allowance
        refuse_rename = false;
        const ReplaySave kept = store.save(good);
        ASSERT_TRUE(kept.kept && kept.file == kName0);
        ASSERT_TRUE(names_in(f.dir) == std::vector<std::string>{kName0} && store.count() == 1 && store.readable_count() == 1);
#if defined(__linux__)
        // a write that fails after the file was made (the disk fills up while it is written): /dev/full takes the open and refuses every byte; the half file must not stay behind
        Fixture g;
        ReplayStore full(g.config());
        ASSERT_TRUE(full.prepare(why));
        std::error_code link_ec;
        fs::create_symlink("/dev/full", g.dir / (kName0 + ".tmp"), link_ec);
        if (!link_ec && fs::exists("/dev/full")) {
            const ReplaySave refused_full = full.save(good);
            ASSERT_TRUE(!refused_full.kept && contains(refused_full.note, "refused the write"));
            ASSERT_TRUE(fs::symlink_status(g.dir / (kName0 + ".tmp")).type() == fs::file_type::not_found);        // (what stood for the half file is gone)
            ASSERT_TRUE(full.count() == 0 && full.readable_count() == 0 && full.total_bytes() == 0);
        }
#endif
    } TEST_END();

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

    TEST_CASE("RS4.3 A Clock That Was Set Back A Long Way Does Not Keep The Next Look Away For As Long: The Hour Is Counted From The Clock's New Time") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.keep_days = 1;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));                                                 // (the first look is due at T0 + 1 hour)
        const int64_t back = kT0 - 100 * kDay;
        f.now = back;
        store.update();                                                                  // (the due time is far ahead of this clock: looked at now, the next look is an hour from now)
        ASSERT_TRUE(store.save(make_file("TREASURE.LVL", 700, true, 0, 1)).kept);       // F: ended at `back`
        ASSERT_EQ(store.count(), size_t{1});
        f.now = back + kDay + kHour;                                                     // (F is a day old, and an hour has passed since the look)
        store.update();
        ASSERT_EQ(store.count(), size_t{0});
        ASSERT_TRUE(names_in(f.dir).empty());
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

    TEST_CASE("RS5.3 A File That Cannot Be Deleted Stays In The Index And On The Disk And Is Tried Again At The Next Purge, The Others Behind It Still Go, The Log Names The First And Counts The Rest; A Match Is Refused Only When No Room Can Be Made; A DELETE Of Such A File Says No") {
        Fixture f;
        std::set<std::string> stuck;                                                     // the names that the "disk" refuses to delete
        const auto config_with_stuck = [&]() {
            ReplayConfig c = f.config();
            c.remove_file = [&stuck](const std::string& path, std::error_code& ec) {
                if (stuck.count(fs::path(path).filename().string()) != 0) ec = std::make_error_code(std::errc::permission_denied);
                else fs::remove(path, ec);
            };
            return c;
        };
        // the age limit: four matches ended 10 s apart; at the purge the first three are a day old, the fourth is not; the first two cannot be deleted
        ReplayConfig cfg = config_with_stuck();
        cfg.keep_days = 1;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        std::vector<std::string> names;
        for (uint32_t i = 0; i < 4; ++i) {
            f.now = kT0 + static_cast<int64_t>(i) * 10;
            const ReplaySave saved = store.save(make_file("TREASURE.LVL", 700, true, 0, 10 + i));
            ASSERT_TRUE(saved.kept);
            names.push_back(saved.file);
        }
        store.take_notes();
        stuck = {names[0], names[1]};
        f.now = kT0 + kDay + 25;
        ASSERT_EQ(store.purge(), size_t{1});                                             // (only the third could go: the stuck ones do not stop it)
        ASSERT_TRUE(fs::exists(f.dir / names[0]) && fs::exists(f.dir / names[1]) && !fs::exists(f.dir / names[2]) && fs::exists(f.dir / names[3]));
        ASSERT_TRUE(store.count() == 3 && store.find(names[0]) != nullptr && store.find(names[1]) != nullptr && store.find(names[2]) == nullptr);
        uint64_t on_disk = 0;
        for (const std::string& n : names_in(f.dir)) on_disk += fs::file_size(f.dir / n);
        ASSERT_EQ(store.total_bytes(), on_disk);                                         // (the index still counts what is still there)
        const std::vector<std::string> notes = store.take_notes();
        ASSERT_TRUE(any_line_has(notes, names[0] + " could not be deleted") && !any_line_has(notes, names[1] + " could not be deleted"));
        ASSERT_TRUE(any_line_has(notes, "1 more file(s) could not be deleted either") && any_line_has(notes, "1 file(s) deleted"));
        stuck.clear();                                                                   // the disk gives way: the next purge deletes them
        ASSERT_EQ(store.purge(), size_t{2});
        ASSERT_TRUE(store.count() == 1 && store.find(names[3]) != nullptr && names_in(f.dir).size() == 1);

        // the size limit: the oldest file is stuck, so the next one is deleted instead, and the new match fits behind it
        Fixture g;
        ReplayConfig size_cfg = g.config();
        size_cfg.max_bytes = 1024;
        size_cfg.remove_file = [&stuck](const std::string& path, std::error_code& ec) {
            if (stuck.count(fs::path(path).filename().string()) != 0) ec = std::make_error_code(std::errc::permission_denied);
            else fs::remove(path, ec);
        };
        ReplayStore small(size_cfg);
        ASSERT_TRUE(small.prepare(why));
        const ReplaySave a = small.save(make_file("TREASURE.LVL", 700, true, 400, 1));
        g.now += 1;
        const ReplaySave b = small.save(make_file("TREASURE.LVL", 700, true, 400, 2));
        ASSERT_TRUE(a.kept && b.kept && small.count() == 2);
        stuck = {a.file};
        g.now += 1;
        const ReplaySave c = small.save(make_file("TREASURE.LVL", 700, true, 400, 3));
        ASSERT_TRUE(c.kept);
        ASSERT_TRUE(small.find(a.file) != nullptr && small.find(b.file) == nullptr && small.find(c.file) != nullptr && small.total_bytes() <= 1024);
        // nothing can be deleted: the match is refused, with the reason, and nothing is written
        stuck = {a.file, c.file};
        g.now += 1;
        const std::vector<std::string> before = names_in(g.dir);
        const ReplaySave d = small.save(make_file("TREASURE.LVL", 700, true, 400, 4));
        ASSERT_TRUE(!d.kept && contains(d.note, "older replays could not be deleted to make room"));
        ASSERT_EQ(names_in(g.dir), before);
        // an explicit delete (the control interface's DELETE) of a file that is stuck says no and keeps it; once the disk allows it, it goes
        ASSERT_FALSE(small.remove(a.file));
        ASSERT_TRUE(small.find(a.file) != nullptr && fs::exists(g.dir / a.file));
        stuck.clear();
        ASSERT_TRUE(small.remove(a.file));
        ASSERT_TRUE(small.find(a.file) == nullptr && !fs::exists(g.dir / a.file) && small.count() == 1);
    } TEST_END();

    // ---- the hour's limit and the disk ----------------------------------------------------------------------------------------------

    TEST_CASE("RS5.4 The Limit Is The Limit Itself: A File As Big As The Whole Store Is Kept, Files That Together Are Exactly The Limit Are All Kept (Nothing Goes For Them), A File One Byte Bigger Than The Store Is Refused") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.max_bytes = 1024;                                                            // (the smallest)
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> whole = make_file_of_size(1024, 1);
        ASSERT_EQ(whole.size(), size_t{1024});
        const ReplaySave alone = store.save(whole);
        ASSERT_TRUE(alone.kept && store.count() == 1 && store.total_bytes() == 1024);
        const std::vector<uint8_t> one_more = make_file_of_size(1025, 2);
        ASSERT_EQ(one_more.size(), size_t{1025});
        f.now += 1;
        const ReplaySave refused = store.save(one_more);
        ASSERT_TRUE(!refused.kept && contains(refused.note, "bigger than the whole store"));
        ASSERT_TRUE(store.count() == 1 && fs::exists(f.dir / alone.file));
        // two files that fill the store to the byte
        Fixture g;
        ReplayConfig cfg2 = g.config();
        cfg2.max_bytes = 1024;
        ReplayStore pair(cfg2);
        ASSERT_TRUE(pair.prepare(why));
        const std::vector<uint8_t> first = make_file("TREASURE.LVL", 700, true, 400, 1);
        const std::vector<uint8_t> second = make_file_of_size(1024 - first.size(), 2);
        ASSERT_TRUE(first.size() > 100 && first.size() < 1024 && second.size() == 1024 - first.size());
        const ReplaySave a = pair.save(first);
        g.now += 1;
        const ReplaySave b = pair.save(second);
        ASSERT_TRUE(a.kept && b.kept);
        ASSERT_TRUE(pair.count() == 2 && pair.total_bytes() == 1024);
        ASSERT_TRUE(fs::exists(g.dir / a.file) && fs::exists(g.dir / b.file));           // (the sum is the limit, not over it: nothing was deleted)
        g.now += 1;
        ASSERT_TRUE(pair.save(make_file("TREASURE.LVL", 700, true, 0, 3)).kept);        // (one more does push the oldest out)
        ASSERT_FALSE(fs::exists(g.dir / a.file));
    } TEST_END();

    TEST_CASE("RS5.5 The Oldest Go First By The Time Of Their Match, Not By The Order In Which They Were Kept: A File Kept Later That Ended Earlier (A Clock Set Back) Is The First To Go") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.max_bytes = 1024;
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> a = make_file("TREASURE.LVL", 700, true, 400, 1);
        const std::vector<uint8_t> b = make_file("TREASURE.LVL", 700, true, 400, 2);
        const std::vector<uint8_t> c = make_file("TREASURE.LVL", 700, true, 400, 3);
        ASSERT_TRUE(a.size() <= 512 && b.size() <= 512 && c.size() <= 512 && a.size() >= 400 && b.size() >= 400 && c.size() >= 400);
        const ReplaySave newer = store.save(a);                                          // ended at T0
        f.now = kT0 - 1000;
        const ReplaySave older = store.save(b);                                          // kept second, but it ended 1000 s before the first
        ASSERT_TRUE(newer.kept && older.kept);
        const std::vector<ReplayEntry> list = store.list();
        ASSERT_TRUE(list.size() == 2 && list[0].file == newer.file && list[1].file == older.file);          // (newest first, by the time of the match)
        f.now = kT0 + 5;
        const ReplaySave third = store.save(c);                                          // the limit needs one out
        ASSERT_TRUE(third.kept);
        ASSERT_FALSE(fs::exists(f.dir / older.file));
        ASSERT_TRUE(fs::exists(f.dir / newer.file) && fs::exists(f.dir / third.file) && store.count() == 2);
    } TEST_END();

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
        // a clock that was set back: what was kept "later" than now is forgotten, so that the hour does not stay full until the clock catches up
        f.now = kT0 - 7 * kDay;
        ASSERT_TRUE(store.save(make_file("TREASURE.LVL", 700, true, 0, 11)).kept);
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
        ASSERT_TRUE(store.readable_count() == 1 && store.entries().size() == 2 && store.entries()[0].file == good->file && store.entries()[1].file == junk->file);        // (oldest first)
        ASSERT_TRUE(store.find("notes.txt") == nullptr && store.find("ants-bad-name.antsrep") == nullptr && store.find("other.tmp") == nullptr);
        const std::vector<std::string> notes = store.take_notes();
        ASSERT_TRUE(any_line_has(notes, "2 file(s)") && any_line_has(notes, "1 half-written") && any_line_has(notes, "1 file(s) that this build cannot read"));
        // the junk is old after a month, like every file
        f.now = kT0 + 31 * kDay;
        ASSERT_EQ(store.purge(), size_t{2});
        ASSERT_TRUE(store.count() == 0 && store.readable_count() == 0 && !fs::exists(f.dir / "ants-JUNK-20261008-100000Z.antsrep") && fs::exists(f.dir / "notes.txt"));
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
            f.now += 5;
            ASSERT_TRUE(store.save(make_file("ISLANDS.LVL", 900, true, 0, 4, 0x05, true, static_cast<uint16_t>(net::kProtocolVersion - 1))).kept);      // (a match of the rules of the build before this one)
            before = store.list();
        }
        ReplayStore again(f.config());
        ASSERT_TRUE(again.prepare(why));
        const std::vector<ReplayEntry> after = again.list();
        ASSERT_EQ(after.size(), size_t{4});
        ASSERT_EQ(before.size(), after.size());
        uint64_t sum = 0;
        for (size_t i = 0; i < after.size(); ++i) {
            ASSERT_TRUE(before[i].file == after[i].file && before[i].bytes == after[i].bytes && before[i].ended_s == after[i].ended_s && before[i].map == after[i].map);
            ASSERT_TRUE(before[i].turns == after[i].turns && before[i].finished == after[i].finished && before[i].players == after[i].players && before[i].rules == after[i].rules);
            ASSERT_TRUE(after[i].readable);
            sum += after[i].bytes;
        }
        ASSERT_EQ(again.total_bytes(), sum);
        ASSERT_TRUE(after[0].map == "ISLANDS.LVL" && after[1].map == "TINY.LVL" && after[2].map == "SMALL.LVL" && after[3].map == "TREASURE.LVL");      // newest first
        ASSERT_TRUE(!after[2].finished && after[2].turns == 1500 && after[3].finished);
        ASSERT_TRUE(after[0].readable && after[0].rules == net::kProtocolVersion - 1 && after[1].rules == net::kProtocolVersion);       // (listed as what it is: readable, and the rules it was played by)
        ASSERT_EQ(again.readable_count(), size_t{4});
    } TEST_END();

    // ---- looking a file up --------------------------------------------------------------------------------------------------------

    TEST_CASE("RS8.1 A File Is Found, Read And Deleted By A Name Of The Index Only: A Path, A Name That Is Not In The Index And A Foreign File In The Folder Are Not Opened, Read Or Deleted") {
        Fixture f;
        ReplayStore store(f.config());
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<uint8_t> bytes = make_file();
        ASSERT_TRUE(store.save(bytes).kept);
        ASSERT_EQ(store.readable_count(), size_t{1});
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
        ASSERT_EQ(store.readable_count(), size_t{0});                                    // (the count of the files that can be read goes down with the file)
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

    TEST_CASE("RS8.3 A Name Is Found Among Many By Bisection: Every File Of A Store With Matches That Ended In One Second (Of One Map, Of Several), Old And New, Is Found At Itself; A Name That Is Valid And Not In The Index Is Not, Wherever It Would Stand") {
        Fixture f;
        ReplayConfig cfg = f.config();
        cfg.max_saves_per_hour = 100000;                                                 // (the hour's allowance is not what this is about)
        ReplayStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        std::vector<std::string> names;
        const char* const maps[] = {"TREASURE.LVL", "ISLANDS.LVL", "TINY.LVL"};
        for (uint32_t second = 0; second < 40; ++second) {
            for (uint32_t i = 0; i < 7; ++i) {                                           // seven matches in the second: two maps with more than one, so "-2", "-3" ... and different stems
                const ReplaySave saved = store.save(make_file(maps[i % 4 == 3 ? 0 : (i % 3)], 700, true, 0, second * 10 + i + 1));       // (TREASURE four times, ISLANDS and TINY twice)
                ASSERT_TRUE(saved.kept);
                names.push_back(saved.file);
            }
            f.now += second % 5 == 0 ? 0 : 1;                                            // (some seconds hold the matches of the one before)
            f.now -= second == 20 ? 30 : 0;                                              // (and a clock that was set back: a later file that ended earlier)
        }
        ASSERT_EQ(store.count(), size_t{280});
        const std::vector<ReplayEntry>& index = store.entries();
        for (size_t i = 1; i < index.size(); ++i) ASSERT_TRUE(index[i - 1].ended_s < index[i].ended_s || (index[i - 1].ended_s == index[i].ended_s && (index[i - 1].sequence < index[i].sequence || (index[i - 1].sequence == index[i].sequence && index[i - 1].file < index[i].file))));
        for (const std::string& name : names) {
            const ReplayEntry* e = store.find(name);
            ASSERT_TRUE(e != nullptr && e->file == name && e >= index.data() && e < index.data() + index.size());
        }
        // names that are valid and not there: the second before the first, between two, after the last; another map in a second that holds others; a "-n" that does not exist
        for (const std::string& absent : std::vector<std::string>{"ants-TREASURE-19700101-000000Z.antsrep", "ants-ZZZ-20261008-143200Z.antsrep", "ants-TREASURE-20261008-143209Z-9999.antsrep", "ants-TREASURE-29990101-000000Z.antsrep",
                                                                  "ants-UNKNOWN-20261008-143209Z.antsrep", "ants-TREASURE-20261008-143209Z-9000.antsrep", "ants-AAA-20261008-143209Z-2.antsrep"}) {
            ASSERT_TRUE(ReplayStore::valid_file_name(absent) && store.find(absent) == nullptr);
        }
        for (const std::string& invalid : std::vector<std::string>{"", "x", names.front() + ".tmp", "../" + names.front(), names.front() + "/"}) ASSERT_TRUE(store.find(invalid) == nullptr);
        // after files went (the oldest by age: half of them) the rest is still found, and what went is not
        std::vector<int64_t> ended_at;
        for (const std::string& name : names) {
            int64_t ended = 0;
            ASSERT_TRUE(ReplayStore::time_of_name(name, ended));
            ended_at.push_back(ended);
        }
        std::vector<int64_t> sorted = ended_at;
        std::sort(sorted.begin(), sorted.end());
        f.now = sorted[sorted.size() / 2] + 30 * kDay;
        const size_t gone = store.purge();
        ASSERT_TRUE(gone > 20 && gone < 260 && store.count() == names.size() - gone && store.readable_count() == store.count());
        for (size_t i = 0; i < names.size(); ++i) ASSERT_EQ(store.find(names[i]) != nullptr, f.now - ended_at[i] < 30 * kDay);
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures
              << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

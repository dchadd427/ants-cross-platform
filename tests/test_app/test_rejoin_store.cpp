// Tests of the keys' storage (ants_app/rejoin_store.hpp, docs/NETWORK_PORT.md "What the clients do in release B"): the desktop's file and the browser's local storage behind one interface. No window,
// no sockets, a clock of the test's own: the file's format and what a broken line does, the age of an entry (3 hours), the cap (8), replace and forget, the file's mode (0600, whatever the umask
// says, the temp file's too), the atomic write (a reader sees the old file or the new one, never half of one) and the web backend's names and values over a map.
//
// A KEY IS A SECRET: no test prints one (a failed assertion names its condition, never a value).
#include "ants_app/rejoin_store.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::app;
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
    std::cout << "  RUNNING: " << std::left << std::setw(120) << name.substr(0, 120) << " ... " << std::flush;
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

constexpr int64_t kSecond = 1000;
constexpr int64_t kHour = 3600 * kSecond;
constexpr int64_t kAge = 3 * kHour;                              // how long a key is of use (rejoin_store.hpp: a key that outlives its match is a button for a match that is gone)
constexpr int64_t kT0 = 1'700'000'000'000;                         // a Unix time in ms, a whole second

// A folder of this process alone, made new and removed with everything in it when the program ends
class Scratch {
public:
    Scratch() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_rejoin_store_" + std::to_string(static_cast<long>(getpid())) + "_" + suffix);
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot make a scratch folder");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    fs::path fresh(const char* tag) const {                         // an empty folder of its own
        const fs::path p = path_ / tag;
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p);
        return p;
    }

private:
    fs::path path_;
};

Scratch& scratch() {
    static Scratch s;
    return s;
}

// A key that no two seats share: byte i is `tag` + i (never all zero)
net::SeatKey key_of(uint8_t tag) {
    net::SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(tag + i * 7u + 1u);
    return k;
}

net::RejoinKey rk(const std::string& room, uint8_t seat, const std::string& server, uint8_t tag) {
    net::RejoinKey k;
    k.room = room;
    k.seat = seat;
    k.server = server;
    k.key = key_of(tag);
    return k;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

size_t files_in(const fs::path& dir) {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        (void)e;
        ++n;
    }
    return n;
}

struct Clock {
    int64_t now{kT0};
    std::function<int64_t()> fn() { return [this]() { return now; }; }
};

const std::string kServer = "play.example.org:4001";

// One line of the file as the store writes it
std::string line_of(const std::string& server, const std::string& room, unsigned seat, uint8_t tag, int64_t seconds) {
    return server + "\t" + room + "\t" + std::to_string(seat) + "\t" + rejoin_key_hex(key_of(tag)) + "\t" + std::to_string(seconds) + "\n";
}

#if !defined(_WIN32)
mode_t mode_of(const fs::path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return 0;
    return st.st_mode & 0777;
}
#endif

}  // namespace

void run_text_tests() {
    TEST_CASE("RS1.1 A Key Is 32 Lower-Case Hex Digits And Back (Capitals Are Read, Anything Else Is Refused, And So Is No Key At All)") {
        net::SeatKey k{};
        for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(i * 17u);          // 00 11 22 ... ff
        k[15] = 0xFFu;
        ASSERT_EQ(rejoin_key_hex(k), std::string("00112233445566778899aabbccddeeff"));
        net::SeatKey back{};
        ASSERT_TRUE(rejoin_key_from_hex("00112233445566778899aabbccddeeff", back) && net::key_matches(back, k));
        net::SeatKey upper{};
        ASSERT_TRUE(rejoin_key_from_hex("00112233445566778899AABBCCDDEEFF", upper) && net::key_matches(upper, k));
        net::SeatKey untouched = key_of(9);
        ASSERT_FALSE(rejoin_key_from_hex("", untouched));
        ASSERT_FALSE(rejoin_key_from_hex("00112233445566778899aabbccddeef", untouched));      // 31 digits
        ASSERT_FALSE(rejoin_key_from_hex("00112233445566778899aabbccddeeff0", untouched));    // 33
        ASSERT_FALSE(rejoin_key_from_hex("00112233445566778899aabbccddeefg", untouched));     // not hex
        ASSERT_FALSE(rejoin_key_from_hex("00112233445566778899aabbccddee ff", untouched));
        ASSERT_FALSE(rejoin_key_from_hex("00000000000000000000000000000000", untouched));     // no key
        ASSERT_TRUE(net::key_matches(untouched, key_of(9)));                                  // (a refusal leaves the output alone)
    } TEST_END();

    TEST_CASE("RS1.2 The Text Of A Server Is The One NetGame Keeps In A Key (host:port, [v6]:port, The URL Of The Browser's Way)") {
        ASSERT_EQ(rejoin_server_text("10.0.0.5", 4001, ""), std::string("10.0.0.5:4001"));
        ASSERT_EQ(rejoin_server_text("beta.playants.org", 4444, ""), std::string("beta.playants.org:4444"));
        ASSERT_EQ(rejoin_server_text("::1", 4001, ""), std::string("[::1]:4001"));
        ASSERT_EQ(rejoin_server_text("", 0, "wss://play.example.org/game"), std::string("wss://play.example.org/game"));
        ASSERT_EQ(rejoin_server_text("10.0.0.5", 4001, "ws://x/y"), std::string("ws://x/y"));       // the URL wins, as it does in NetGame::server_text
    } TEST_END();

    TEST_CASE("RS1.3 What Can Be Stored: A Room Of The Server's Own Rules, A Seat 0 - 3, A Key That Is Not Zero, A Server Of 1 - 255 Printable Characters") {
        ASSERT_TRUE(rejoin_storable(rk("RA-1", 0, kServer, 1)));
        ASSERT_TRUE(rejoin_storable(rk("k7m2xq", 3, kServer, 1)));
        ASSERT_TRUE(rejoin_storable(rk(std::string(32, 'x'), 1, kServer, 1)));
        ASSERT_FALSE(rejoin_storable(rk("", 0, kServer, 1)));                                     // a room of the server has a code
        ASSERT_FALSE(rejoin_storable(rk(std::string(33, 'x'), 0, kServer, 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA.1", 0, kServer, 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA 1", 0, kServer, 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 4, kServer, 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 255, kServer, 1)));
        net::RejoinKey zero = rk("RA-1", 0, kServer, 1);
        zero.key = net::SeatKey{};
        ASSERT_FALSE(rejoin_storable(zero));
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 0, "", 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 0, "a\tb", 1)));                                  // a tab or a line end would break the file
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 0, "a\nb", 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 0, "caf\xC3\xA9:4001", 1)));
        ASSERT_TRUE(rejoin_storable(rk("RA-1", 0, std::string(255, 'h'), 1)));
        ASSERT_FALSE(rejoin_storable(rk("RA-1", 0, std::string(256, 'h'), 1)));
    } TEST_END();
}

void run_file_tests() {
    TEST_CASE("RS2.1 The File Is One Line Per Key, Tab-Separated: Server, Room, Seat, Key In Hex, Written In Unix Seconds; What Is Written Is Read Back") {
        std::vector<RejoinEntry> entries;
        RejoinEntry a;
        a.server = "10.0.0.5:4001";
        a.room = "ROOM-1";
        a.seat = 2;
        a.key = key_of(3);
        a.written_ms = kT0 + 5 * kSecond;
        RejoinEntry b = a;
        b.server = "wss://play.example.org/game";
        b.room = "k7m2xq";
        b.seat = 0;
        b.key = key_of(4);
        b.written_ms = kT0;
        entries = {a, b};
        const std::string text = FileRejoinStore::serialise(entries);
        ASSERT_EQ(text, "10.0.0.5:4001\tROOM-1\t2\t" + rejoin_key_hex(key_of(3)) + "\t" + std::to_string(kT0 / kSecond + 5) + "\n" + "wss://play.example.org/game\tk7m2xq9p\t0\t" +
                            rejoin_key_hex(key_of(4)) + "\t" + std::to_string(kT0 / kSecond) + "\n");
        const std::vector<RejoinEntry> back = FileRejoinStore::parse(text, kT0 + 10 * kSecond);
        ASSERT_EQ(back.size(), size_t{2});
        ASSERT_TRUE(back[0].server == a.server && back[0].room == a.room && back[0].seat == 2 && net::key_matches(back[0].key, a.key) && back[0].written_ms == a.written_ms);   // (newest first)
        ASSERT_TRUE(back[1].server == b.server && back[1].room == b.room && back[1].seat == 0 && net::key_matches(back[1].key, b.key) && back[1].written_ms == b.written_ms);
        ASSERT_EQ(FileRejoinStore::serialise(back), text);
    } TEST_END();

    TEST_CASE("RS2.2 A Line That Is Not An Entry Is Ignored, The Good Lines Around It Are Kept (Too Few Or Too Many Fields, An Empty Server, A Room Or A Seat Out Of Range, A Key That Is Short, Not Hex Or Zero, A Time That Is No Number)") {
        const int64_t s = kT0 / kSecond;
        const std::string good1 = line_of("a.example:4001", "GOOD-1", 1, 11, s);
        const std::string good2 = line_of("a.example:4001", "GOOD-2", 3, 12, s - 5);
        const std::string hex = rejoin_key_hex(key_of(20));
        const std::vector<std::string> broken = {
            "",                                                                                     // an empty line
            "just text",
            "a.example:4001\tROOM\t1\t" + hex,                                                      // four fields
            "a.example:4001\tROOM\t1\t" + hex + "\t" + std::to_string(s) + "\textra",               // six
            "a.example:4001\tROOM\t1\t" + hex + "\t" + std::to_string(s) + "\t",                    // six, the last empty
            "\tROOM\t1\t" + hex + "\t" + std::to_string(s),                                         // no server
            "a.example:4001\t\t1\t" + hex + "\t" + std::to_string(s),                               // no room
            "a.example:4001\tRO.OM\t1\t" + hex + "\t" + std::to_string(s),                          // a room with a dot
            "a.example:4001\t" + std::string(33, 'r') + "\t1\t" + hex + "\t" + std::to_string(s),   // a room too long
            "a.example:4001\tROOM\t4\t" + hex + "\t" + std::to_string(s),                           // seat 4
            "a.example:4001\tROOM\t-1\t" + hex + "\t" + std::to_string(s),
            "a.example:4001\tROOM\t01\t" + hex + "\t" + std::to_string(s),                          // two digits
            "a.example:4001\tROOM\t\t" + hex + "\t" + std::to_string(s),
            "a.example:4001\tROOM\t1\t" + hex.substr(1) + "\t" + std::to_string(s),                 // 31 hex digits
            "a.example:4001\tROOM\t1\t" + hex + "0\t" + std::to_string(s),                          // 33
            "a.example:4001\tROOM\t1\t" + std::string(31, 'a') + "g\t" + std::to_string(s),         // not hex
            "a.example:4001\tROOM\t1\t" + std::string(32, '0') + "\t" + std::to_string(s),          // the zero key
            "a.example:4001\tROOM\t1\t" + hex + "\t",                                               // no time
            "a.example:4001\tROOM\t1\t" + hex + "\tsoon",
            "a.example:4001\tROOM\t1\t" + hex + "\t-5",
            "a.example:4001\tROOM\t1\t" + hex + "\t1.5",
            "a.example:4001\tROOM\t1\t" + hex + "\t1234567890123",                                  // 13 digits: not a Unix time in seconds of this century
            "caf\xC3\xA9:4001\tROOM\t1\t" + hex + "\t" + std::to_string(s),                         // a server that is not printable ASCII
        };
        for (const std::string& bad : broken) {
            const std::string text = good1 + bad + "\n" + good2;
            const std::vector<RejoinEntry> got = FileRejoinStore::parse(text, kT0 + kSecond);
            ASSERT_TRUE(got.size() == 2 && got[0].room == "GOOD-1" && got[1].room == "GOOD-2");   // (the bad line is gone, the good ones are not)
        }
        const std::string lines_with_cr = line_of("a.example:4001", "CRLF", 0, 13, s);
        std::string crlf = lines_with_cr;
        crlf.insert(crlf.size() - 1, "\r");                                                         // "...\r\n"
        ASSERT_EQ(FileRejoinStore::parse(crlf, kT0).size(), size_t{1});                             // a file that went through a Windows editor
        std::string no_end = good1;
        no_end.pop_back();
        ASSERT_EQ(FileRejoinStore::parse(no_end, kT0).size(), size_t{1});                           // the last line may lack its line end
        ASSERT_TRUE(FileRejoinStore::parse("", kT0).empty() && FileRejoinStore::parse("\n\n\n", kT0).empty());
        ASSERT_TRUE(FileRejoinStore::parse(std::string("\0\0\0garbage\0", 11), kT0).empty());
    } TEST_END();

    TEST_CASE("RS2.3 A Key Is Kept: The File Has Its Line, The Store Gives It Back By Server And Room (And Seat), Newest First; The Same Seat Of The Same Room Is Replaced, Another Seat, Room Or Server Is Another Entry") {
        const fs::path dir = scratch().fresh("rs23");
        Clock clock;
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        ASSERT_TRUE(store.entries().empty() && !store.newest() && !store.find(kServer, "R1"));
        ASSERT_TRUE(store.put(rk("R1", 2, kServer, 1)));
        ASSERT_EQ(read_file(dir / "rejoin.txt"), line_of(kServer, "R1", 2, 1, kT0 / kSecond));
        clock.now += 10 * kSecond;
        ASSERT_TRUE(store.put(rk("R2", 0, kServer, 2)));
        clock.now += 10 * kSecond;
        ASSERT_TRUE(store.put(rk("R1", 3, kServer, 3)));                                            // the same room, another seat
        clock.now += 10 * kSecond;
        ASSERT_TRUE(store.put(rk("R1", 3, "other.example:4001", 4)));                               // the same room and seat on another server
        std::vector<RejoinEntry> all = store.entries();
        ASSERT_EQ(all.size(), size_t{4});
        ASSERT_TRUE(all[0].server == "other.example:4001" && all[1].room == "R1" && all[1].seat == 3 && all[2].room == "R2" && all[3].room == "R1" && all[3].seat == 2);      // newest first
        ASSERT_TRUE(all[0].written_ms == kT0 + 30 * kSecond && all[3].written_ms == kT0);
        ASSERT_TRUE(store.newest() && store.newest()->server == "other.example:4001");
        ASSERT_TRUE(store.find(kServer, "R1") && store.find(kServer, "R1")->seat == 3 && net::key_matches(store.find(kServer, "R1")->key, key_of(3)));       // the newest of that room
        ASSERT_TRUE(store.find(kServer, "R1", 2) && net::key_matches(store.find(kServer, "R1", 2)->key, key_of(1)));
        ASSERT_TRUE(store.find(kServer, "R1", 3) && net::key_matches(store.find(kServer, "R1", 3)->key, key_of(3)));
        ASSERT_FALSE(store.find(kServer, "R1", 1));                                                 // a seat that holds nothing
        ASSERT_FALSE(store.find(kServer, "R3"));
        ASSERT_FALSE(store.find("nowhere.example:4001", "R1"));
        ASSERT_FALSE(store.find("play.example.org:4002", "R1"));                                    // another port is another server
        clock.now += 10 * kSecond;
        ASSERT_TRUE(store.put(rk("R1", 3, kServer, 5)));                                            // replaced: a new key of the same seat, in front, and one entry fewer than a copy
        all = store.entries();
        ASSERT_EQ(all.size(), size_t{4});
        ASSERT_TRUE(all[0].room == "R1" && all[0].seat == 3 && all[0].server == kServer && net::key_matches(all[0].key, key_of(5)) && all[0].written_ms == kT0 + 40 * kSecond);
        for (const RejoinEntry& e : all) ASSERT_FALSE(e.room == "R1" && e.seat == 3 && e.server == kServer && net::key_matches(e.key, key_of(3)));      // (the old key of that seat is gone)
        const std::string text = read_file(dir / "rejoin.txt");
        ASSERT_EQ(std::count(text.begin(), text.end(), '\n'), 4);
    } TEST_END();

    TEST_CASE("RS2.4 A Key That Cannot Be Stored Is Refused And The File Is Not Touched") {
        const fs::path dir = scratch().fresh("rs24");
        Clock clock;
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        ASSERT_TRUE(store.put(rk("R1", 0, kServer, 1)));
        const std::string before = read_file(dir / "rejoin.txt");
        ASSERT_FALSE(store.put(rk("R.1", 0, kServer, 2)));
        ASSERT_FALSE(store.put(rk("R1", 7, kServer, 2)));
        ASSERT_FALSE(store.put(rk("R1", 0, "bad\tserver", 2)));
        net::RejoinKey zero = rk("R1", 1, kServer, 2);
        zero.key = net::SeatKey{};
        ASSERT_FALSE(store.put(zero));
        ASSERT_EQ(read_file(dir / "rejoin.txt"), before);
        ASSERT_EQ(files_in(dir), size_t{1});
    } TEST_END();

    TEST_CASE("RS3.1 An Entry Is Of Use For 3 Hours: One Second Short Of Them It Is Given Back, A Second Past Them It Is Not; A Read Writes Nothing, The Next Write Leaves It Out, And A Time In The Far Future Is No Entry") {
        ASSERT_EQ(kRejoinMaxAgeMs, kAge);                                                           // (the limit itself: three hours; it was 24)
        const fs::path dir = scratch().fresh("rs31");
        Clock clock;
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        ASSERT_TRUE(store.put(rk("OLD", 1, kServer, 1)));
        clock.now = kT0 + kAge - kSecond;
        ASSERT_EQ(store.entries().size(), size_t{1});
        clock.now = kT0 + kAge;
        ASSERT_EQ(store.entries().size(), size_t{1});                                               // (three hours exactly: not older than that)
        clock.now = kT0 + kAge + kSecond;
        ASSERT_TRUE(store.entries().empty() && !store.find(kServer, "OLD") && !store.newest());
        ASSERT_EQ(read_file(dir / "rejoin.txt"), line_of(kServer, "OLD", 1, 1, kT0 / kSecond));      // reading removed nothing from the file ...
        ASSERT_TRUE(store.put(rk("NEW", 2, kServer, 2)));                                            // ... a write does not write it again
        ASSERT_EQ(read_file(dir / "rejoin.txt"), line_of(kServer, "NEW", 2, 2, clock.now / kSecond));
        // the same through the other direction: a file from the future (the clock was set back) is judged by the same limit
        write_file(dir / "rejoin.txt", line_of(kServer, "SOON", 0, 3, (clock.now + kAge - kHour) / kSecond) + line_of(kServer, "FAR", 0, 4, (clock.now + kAge + kHour) / kSecond));
        const std::vector<RejoinEntry> got = store.entries();
        ASSERT_TRUE(got.size() == 1 && got[0].room == "SOON");
    } TEST_END();

    TEST_CASE("RS3.2 At Most 8 Entries: The Newest Are Kept, The Oldest Go, And A Key Put Again Moves To The Front Without Making A Ninth") {
        const fs::path dir = scratch().fresh("rs32");
        Clock clock;
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(store.put(rk("ROOM-" + std::to_string(i), static_cast<uint8_t>(i % 4), kServer, static_cast<uint8_t>(10 + i))));
            clock.now += kSecond;
        }
        std::vector<RejoinEntry> all = store.entries();
        ASSERT_EQ(all.size(), kRejoinMaxEntries);
        for (size_t i = 0; i < all.size(); ++i) ASSERT_EQ(all[i].room, "ROOM-" + std::to_string(9 - i));         // ROOM-9 ... ROOM-2: the two oldest are gone
        ASSERT_FALSE(store.find(kServer, "ROOM-0") || store.find(kServer, "ROOM-1"));
        const std::string text = read_file(dir / "rejoin.txt");
        ASSERT_EQ(static_cast<size_t>(std::count(text.begin(), text.end(), '\n')), kRejoinMaxEntries);
        ASSERT_TRUE(store.put(rk("ROOM-4", 0, kServer, 99)));                                       // ROOM-4's seat is 0: the same seat of the same room, a new key
        all = store.entries();
        ASSERT_TRUE(all.size() == kRejoinMaxEntries && all[0].room == "ROOM-4" && net::key_matches(all[0].key, key_of(99)));
        ASSERT_TRUE(store.find(kServer, "ROOM-2") && store.find(kServer, "ROOM-9"));                // (nothing else was lost for it)
    } TEST_END();

    TEST_CASE("RS3.3 A Key Is Let Go Of By Its Server, Room, Seat And Key: Another Key Of The Seat (A Newer Match Of The Same Room) Stays, A Key That Is Not There Changes Nothing (The File Is Not Written), The Last One Takes The File Away") {
        const fs::path dir = scratch().fresh("rs33");
        Clock clock;
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        int writes = 0;
        store.set_before_rename_for_test([&writes](const std::string&) { ++writes; });
        ASSERT_TRUE(store.put(rk("R1", 1, kServer, 1)));
        clock.now += kSecond;
        ASSERT_TRUE(store.put(rk("R2", 2, kServer, 2)));
        ASSERT_EQ(writes, 2);
        store.forget(rk("R1", 1, kServer, 77));                                                     // another key of that seat
        store.forget(rk("R1", 2, kServer, 1));                                                      // another seat
        store.forget(rk("R9", 1, kServer, 1));                                                      // another room
        store.forget(rk("R1", 1, "other.example:4001", 1));                                         // another server
        ASSERT_EQ(writes, 2);                                                                       // nothing was written for any of them
        ASSERT_EQ(store.entries().size(), size_t{2});
        store.forget(rk("R1", 1, kServer, 1));
        ASSERT_EQ(writes, 3);
        std::vector<RejoinEntry> all = store.entries();
        ASSERT_TRUE(all.size() == 1 && all[0].room == "R2");
        ASSERT_FALSE(store.find(kServer, "R1"));
        store.forget(rk("R2", 2, kServer, 2));
        ASSERT_TRUE(store.entries().empty());
        ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));                                               // no key left: no file left
        ASSERT_EQ(files_in(dir), size_t{0});
        store.forget(rk("R2", 2, kServer, 2));                                                      // (and a forget of nothing, with no file, is no trouble)
        ASSERT_EQ(writes, 3);
    } TEST_END();

#if !defined(_WIN32)
    TEST_CASE("RS4.1 The File Is Mode 0600 Whatever The Umask Says (A Loose One, A Tight One, One That Takes The Owner's Write Bit), A File That Was Mode 0644 Is Replaced By One That Is 0600, And So Is The Temp File Of Every Write, Which Is Gone Afterwards") {
        const fs::path dir = scratch().fresh("rs41");
        Clock clock;
        const mode_t old_umask = ::umask(0);                                                        // the loosest: nothing is taken away from the mode that was asked for
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        std::vector<mode_t> temp_modes;
        std::vector<std::string> temp_names;
        store.set_before_rename_for_test([&](const std::string& tmp) {
            temp_modes.push_back(mode_of(tmp));
            temp_names.push_back(tmp);
        });
        ASSERT_TRUE(store.put(rk("R1", 0, kServer, 1)));
        ASSERT_EQ(mode_of(dir / "rejoin.txt"), mode_t{0600});
        ASSERT_TRUE(temp_modes.size() == 1 && temp_modes[0] == 0600);
        ASSERT_EQ(files_in(dir), size_t{1});                                                        // (the temp file was renamed over the file: nothing else is left)
        ::chmod((dir / "rejoin.txt").c_str(), 0644);                                                // a file from an older version, or another program, that others can read
        ASSERT_TRUE(store.put(rk("R2", 1, kServer, 2)));
        ASSERT_EQ(mode_of(dir / "rejoin.txt"), mode_t{0600});
        ASSERT_TRUE(temp_modes.size() == 2 && temp_modes[1] == 0600);
        ::umask(077);                                                                               // the tightest ordinary one
        ASSERT_TRUE(store.put(rk("R3", 2, kServer, 3)));
        ASSERT_EQ(mode_of(dir / "rejoin.txt"), mode_t{0600});
        store.forget(rk("R3", 2, kServer, 3));
        ASSERT_EQ(mode_of(dir / "rejoin.txt"), mode_t{0600});                                       // (a rewrite after a forget is private too)
        ASSERT_TRUE(temp_modes.size() == 4 && temp_modes[2] == 0600 && temp_modes[3] == 0600);
        ::umask(0277);                                                                              // one that takes the owner's own write bit away: the mode is still the 0600 that is promised
        ASSERT_TRUE(store.put(rk("R4", 3, kServer, 4)));
        ASSERT_EQ(mode_of(dir / "rejoin.txt"), mode_t{0600});
        ASSERT_TRUE(temp_modes.size() == 5 && temp_modes[4] == 0600);
        ASSERT_EQ(files_in(dir), size_t{1});
        ::umask(old_umask);
        ASSERT_EQ(temp_names.size(), size_t{5});
        for (const std::string& name : temp_names) ASSERT_TRUE(!fs::exists(name) && name.find(".tmp") != std::string::npos);
    } TEST_END();
#endif

    TEST_CASE("RS4.2 A Write Is Atomic: While The Temp File Is Complete The File Is Still The Old One, All Of It; Then It Is The New One, All Of It; A Write That Fails (No Folder, A Name That Cannot Be Replaced) Leaves The Old File And No Temp File") {
        const fs::path dir = scratch().fresh("rs42");
        Clock clock;
        FileRejoinStore store((dir / "rejoin.txt").string(), clock.fn());
        ASSERT_TRUE(store.put(rk("R1", 0, kServer, 1)));
        const std::string old_text = read_file(dir / "rejoin.txt");
        std::string seen_final;
        std::string seen_temp;
        store.set_before_rename_for_test([&](const std::string& tmp) {
            seen_final = read_file(dir / "rejoin.txt");                                             // what a second window that reads at this moment sees
            seen_temp = read_file(tmp);
        });
        clock.now += kSecond;
        ASSERT_TRUE(store.put(rk("R2", 1, kServer, 2)));
        const std::string new_text = read_file(dir / "rejoin.txt");
        ASSERT_EQ(seen_final, old_text);                                                            // not a byte of the new one yet
        ASSERT_EQ(seen_temp, new_text);                                                             // and the new one was complete before it was named
        ASSERT_TRUE(new_text != old_text && FileRejoinStore::parse(new_text, clock.now).size() == 2);
        // a folder that is not there: nothing can be written, nothing is thrown, the store is empty
        FileRejoinStore lost((dir / "missing" / "rejoin.txt").string(), clock.fn());
        ASSERT_FALSE(lost.put(rk("R1", 0, kServer, 1)));
        ASSERT_TRUE(lost.entries().empty());
        ASSERT_FALSE(fs::exists(dir / "missing"));
        // a name that is a folder with something in it: the rename fails, the temp file is taken away
        const fs::path blocked = dir / "blocked";
        fs::create_directories(blocked / "inside");
        FileRejoinStore stuck(blocked.string(), clock.fn());
        ASSERT_FALSE(stuck.put(rk("R1", 0, kServer, 1)));
        ASSERT_TRUE(fs::is_directory(blocked / "inside"));
        ASSERT_EQ(files_in(dir), size_t{2});                                                        // rejoin.txt, blocked, and nothing else (no .tmp)
        ASSERT_EQ(read_file(dir / "rejoin.txt"), new_text);
    } TEST_END();
}

void run_other_store_tests() {
    TEST_CASE("RS5.1 The Memory Store Has The Rules Of The File: Replace, Newest First, 8 At Most, 3 Hours, Forget By Key (What A Headless Run Without A Settings File Keeps, And Nothing Reaches A Disk)") {
        Clock clock;
        MemoryRejoinStore store(clock.fn());
        ASSERT_TRUE(store.entries().empty() && !store.newest());
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(store.put(rk("ROOM-" + std::to_string(i), static_cast<uint8_t>(i % 4), kServer, static_cast<uint8_t>(30 + i))));
            clock.now += kSecond;
        }
        ASSERT_EQ(store.entries().size(), kRejoinMaxEntries);
        ASSERT_TRUE(store.newest() && store.newest()->room == "ROOM-9" && !store.find(kServer, "ROOM-0"));
        ASSERT_TRUE(store.put(rk("ROOM-4", 0, kServer, 98)) && store.newest()->room == "ROOM-4" && store.entries().size() == kRejoinMaxEntries);
        ASSERT_FALSE(store.put(rk("ROOM.4", 0, kServer, 98)));
        store.forget(rk("ROOM-4", 0, kServer, 5));                                                  // not its key
        ASSERT_TRUE(store.find(kServer, "ROOM-4"));
        store.forget(rk("ROOM-4", 0, kServer, 98));
        ASSERT_FALSE(store.find(kServer, "ROOM-4"));
        clock.now = kT0 + 9 * kSecond + kAge;                                                       // ROOM-9 was put at +9 s: exactly as old as a key may be; ROOM-8, a second older, is not given back
        ASSERT_TRUE(store.entries().size() == 1 && store.entries()[0].room == "ROOM-9");
    } TEST_END();

    TEST_CASE("RS6.1 The Web Backend Keeps `ants.rejoin.<room>.<seat>` = {\"k\": hex, \"s\": server, \"t\": epoch ms} In The Browser's Storage; A Seat Of A Room Is One Item (A New Key Replaces The Old), A Foreign Item Is Left Alone") {
        MapStorage storage;
        storage.set("ants.settings", "zoom=1\n");
        Clock clock;
        LocalStorageRejoinStore store(storage, clock.fn());
        ASSERT_TRUE(store.entries().empty());
        ASSERT_TRUE(store.put(rk("k7m2xq", 1, "wss://play.example.org/game", 5)));
        ASSERT_EQ(storage.items().size(), size_t{2});
        ASSERT_TRUE(storage.items().count("ants.rejoin.k7m2xq.1") == 1);
        ASSERT_EQ(storage.items()["ants.rejoin.k7m2xq.1"],
                  "{\"k\":\"" + rejoin_key_hex(key_of(5)) + "\",\"s\":\"wss://play.example.org/game\",\"t\":" + std::to_string(kT0) + "}");
        clock.now += 5 * kSecond;
        ASSERT_TRUE(store.put(rk("k7m2xq", 2, "wss://play.example.org/game", 6)));
        ASSERT_TRUE(store.put(rk("k7m2xq", 1, "wss://play.example.org/game", 7)));      // the same seat: the item is replaced
        ASSERT_EQ(storage.items().size(), size_t{3});
        std::vector<RejoinEntry> all = store.entries();
        ASSERT_TRUE(all.size() == 2 && all[0].seat == 1 && net::key_matches(all[0].key, key_of(7)) && all[1].seat == 2 && all[0].server == "wss://play.example.org/game");
        ASSERT_TRUE(all[0].written_ms == kT0 + 5 * kSecond && all[0].room == "k7m2xq");
        ASSERT_TRUE(store.find("wss://play.example.org/game", "k7m2xq", 2) && !store.find("wss://other.example/game", "k7m2xq"));
        ASSERT_TRUE(store.newest() && store.newest()->seat == 1);
        ASSERT_EQ(storage.items()["ants.settings"], std::string("zoom=1\n"));
        ASSERT_FALSE(store.put(rk("a.b", 0, "wss://x/y", 1)));                                      // (a room that is none)
        ASSERT_EQ(storage.items().size(), size_t{3});
    } TEST_END();

    TEST_CASE("RS6.2 The Web Backend: An Entry Older Than 3 Hours Is Removed When It Is Read, A Key Is Let Go Of By Its Key (A Newer Key Of The Seat Stays), 8 At Most, And What Is Not An Entry Is Ignored") {
        MapStorage storage;
        Clock clock;
        LocalStorageRejoinStore store(storage, clock.fn());
        ASSERT_TRUE(store.put(rk("OLD", 0, "wss://x/y", 1)));
        clock.now = kT0 + kAge / 2;
        ASSERT_TRUE(store.put(rk("MID", 1, "wss://x/y", 2)));
        clock.now = kT0 + kAge;
        ASSERT_EQ(store.entries().size(), size_t{2});
        clock.now = kT0 + kAge + kSecond;
        std::vector<RejoinEntry> all = store.entries();
        ASSERT_TRUE(all.size() == 1 && all[0].room == "MID");
        ASSERT_TRUE(storage.items().count("ants.rejoin.OLD.0") == 0 && storage.items().count("ants.rejoin.MID.1") == 1);       // removed when read
        store.forget(rk("MID", 1, "wss://x/y", 77));                                                // another key of that seat: stays
        ASSERT_EQ(store.entries().size(), size_t{1});
        store.forget(rk("MID", 2, "wss://x/y", 2));                                                 // another seat
        ASSERT_EQ(store.entries().size(), size_t{1});
        store.forget(rk("MID", 1, "wss://x/y", 2));
        ASSERT_TRUE(store.entries().empty() && storage.items().empty());
        // not entries: wrong names, broken values
        const std::string hex = rejoin_key_hex(key_of(3));
        const std::string value = "{\"k\":\"" + hex + "\",\"s\":\"wss://x/y\",\"t\":" + std::to_string(clock.now) + "}";
        storage.items()["ants.rejoin.GOOD.2"] = value;
        storage.items()["ants.rejoin.NOSEAT"] = value;
        storage.items()["ants.rejoin.BAD.9"] = value;                                               // seat 9
        storage.items()["ants.rejoin.B.D.1"] = value;                                               // a room with a dot
        storage.items()["ants.rejoin.TEXT.1"] = "not json";
        storage.items()["ants.rejoin.SHORT.1"] = "{\"k\":\"" + hex.substr(2) + "\",\"s\":\"wss://x/y\",\"t\":" + std::to_string(clock.now) + "}";
        storage.items()["ants.rejoin.NOTIME.1"] = "{\"k\":\"" + hex + "\",\"s\":\"wss://x/y\"}";
        storage.items()["ants.rejoin..1"] = value;                                                  // no room
        all = store.entries();
        ASSERT_TRUE(all.size() == 1 && all[0].room == "GOOD" && all[0].seat == 2 && net::key_matches(all[0].key, key_of(3)));
        // the cap: at most 8, the oldest go
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(store.put(rk("CAP-" + std::to_string(i), static_cast<uint8_t>(i % 4), "wss://x/y", static_cast<uint8_t>(40 + i))));
            clock.now += kSecond;
        }
        all = store.entries();
        ASSERT_EQ(all.size(), kRejoinMaxEntries);
        ASSERT_TRUE(all[0].room == "CAP-9" && all[7].room == "CAP-2");
        ASSERT_TRUE(storage.items().count("ants.rejoin.CAP-0.0") == 0 && storage.items().count("ants.rejoin.CAP-1.1") == 0);   // (not just left out: gone)
    } TEST_END();

    TEST_CASE("RS6.3 The Value Of The Web Backend Is Read Like JSON: Any Order, Blanks, A Member That Is Not Ours, Escapes; Anything Else Is Refused (A Missing Member, A Number That Is No Whole Number, Text After The Object, A Nested Value)") {
        std::string hex;
        std::string server;
        int64_t t = 0;
        ASSERT_TRUE(LocalStorageRejoinStore::parse_value("{\"k\":\"ab\",\"s\":\"x\",\"t\":5}", hex, server, t) && hex == "ab" && server == "x" && t == 5);
        ASSERT_TRUE(LocalStorageRejoinStore::parse_value(" { \"t\" : 1700000000000 , \"s\" : \"wss://a/b\" , \"k\" : \"cd\" } ", hex, server, t) && hex == "cd" && server == "wss://a/b" && t == 1'700'000'000'000);
        ASSERT_TRUE(LocalStorageRejoinStore::parse_value("{\"k\":\"ab\",\"s\":\"a\\\"b\\\\c\\u0041\",\"t\":5}", hex, server, t) && server == "a\"b\\cA");
        ASSERT_TRUE(LocalStorageRejoinStore::parse_value("{\"k\":\"ab\",\"v\":2,\"s\":\"x\",\"note\":\"hi\",\"t\":5}", hex, server, t));      // members that are not ours are ignored
        const std::vector<std::string> refused = {
            "", "{}", "[]", "5", "\"k\"", "{\"k\":\"ab\",\"s\":\"x\"}", "{\"k\":\"ab\",\"t\":5}", "{\"s\":\"x\",\"t\":5}",
            "{\"k\":\"ab\",\"s\":\"x\",\"t\":5} x", "{\"k\":\"ab\",\"s\":\"x\",\"t\":5}}", "{\"k\":\"ab\",\"s\":\"x\",\"t\":5",
            "{\"k\":\"ab\",\"s\":\"x\",\"t\":-5}", "{\"k\":\"ab\",\"s\":\"x\",\"t\":5.5}", "{\"k\":\"ab\",\"s\":\"x\",\"t\":\"5\"}", "{\"k\":5,\"s\":\"x\",\"t\":5}",
            "{\"k\":\"ab\",\"s\":{\"a\":1},\"t\":5}", "{\"k\":\"ab\",\"s\":\"x\",\"t\":5,\"z\":[1]}", "{\"k\":\"ab\",\"s\":\"x\",\"t\":5,}", "{\"k\":\"ab\" \"s\":\"x\",\"t\":5}",
            "{\"k\":\"ab\",\"s\":\"x\\n\",\"t\":5}", "{\"k\":\"ab\",\"s\":\"x\\u0001\",\"t\":5}", "{\"k\":\"ab\",\"s\":\"x\\u00e9\",\"t\":5}", "{\"k\":\"ab\",\"s\":\"x\\q\",\"t\":5}",
            "{\"k\":\"ab\",\"s\":\"x\x01y\",\"t\":5}", "{\"k\":\"ab\",\"s\":\"x\",\"t\":1234567890123456}",
        };
        for (const std::string& text : refused) {
            std::string h = "keep";
            std::string s = "keep";
            int64_t n = -1;
            ASSERT_FALSE(LocalStorageRejoinStore::parse_value(text, h, s, n));
            ASSERT_TRUE(h == "keep" && s == "keep" && n == -1);                                     // (a refusal leaves the outputs alone)
        }
    } TEST_END();
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "======================================================================\n";
    std::cout << "REJOIN STORE SUITE (the keys of the seats: the desktop's file, the browser's storage)\n";
    std::cout << "======================================================================\n";
    run_text_tests();
    run_file_tests();
    run_other_store_tests();
    std::cout << "\n" << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "SOME TESTS FAILED\n";
    return 1;
}

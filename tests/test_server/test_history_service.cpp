// Tests of the history service (include/ants_history/history_service.hpp, docs/HISTORY_SERVICE.md): the same recordings give the same JSON from the game server's public door and from the service, for every
// route and query that the door's tests use; the copy of the game server's history at the first start (once, whole, row for row); a new recording counted within seconds, on the counter's own thread, while the
// HTTP side answers; recordings that are damaged, half written, of a map that is not here; a folder of thousands; the counter and the HTTP side at the same time; the service with no folder, no file or a file that
// stops answering; and the real program as a child process. The store, the feeder and the door's answers themselves are test_history_store.cpp and test_replay_store.cpp.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>

#include "../common/test_assert.hpp"
#include "ants_history/history_service.hpp"
#include "history_fixtures.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef ANTS_HAS_HISTORY_BINARY
namespace ants_test_paths {
const char* history_dir();                                   // (a tiny source that CMake generates: tests/test_server/CMakeLists.txt)
}
#endif

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

namespace {

using history::HistoryService;
using history::ImportReport;
using history::ServiceConfig;

#if defined(__SANITIZE_THREAD__)
constexpr bool kThreadSanitizer = true;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
constexpr bool kThreadSanitizer = true;
#else
constexpr bool kThreadSanitizer = false;
#endif
#else
constexpr bool kThreadSanitizer = false;
#endif
constexpr int kSlow = kThreadSanitizer ? 10 : 1;                 // (a sanitized build is far slower: the waits are longer, the numbers that are limits are not asked)

// ---- helpers ------------------------------------------------------------------------------------------------------------------------

// Lines of the service's log, from either thread
struct Collector {
    std::mutex mutex;
    std::vector<std::string> lines;
    void add(const std::string& line) {
        const std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(line);
    }
    size_t with(const std::string& part) {
        const std::lock_guard<std::mutex> lock(mutex);
        return lines_with(lines, part);
    }
    std::vector<std::string> all() {
        const std::lock_guard<std::mutex> lock(mutex);
        return lines;
    }
};

uint64_t fixed_clock() { return 1000; }                           // (a clock that does not move: the time box of a list never ends and its budget never drains)

// The service's configuration for a test: a clock that does not move for the lists, the counter looking every 25 ms, no waiting after a recording, nothing to settle
ServiceConfig base_config(const fs::path& work, const fs::path& recordings, const std::shared_ptr<Collector>& log) {
    ServiceConfig c;
    c.db_path = (work / "history.db").string();
    c.recordings_dir = recordings.string();
    c.maps_dir = maps_dir();
    c.settle_s = 0;
    c.look_ms = 25;
    c.pace_ms = 0;
    c.pace_factor = 0;
    c.history_clock_us = fixed_clock;
    c.log = [log](const std::string& line) { log->add(line); };
    return c;
}

bool wait_for(const std::function<bool()>& done, int timeout_ms) {
    const auto began = std::chrono::steady_clock::now();
    for (;;) {
        if (done()) return true;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count() > timeout_ms) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int64_t elapsed_ms(std::chrono::steady_clock::time_point since) { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count(); }

ctl::HttpResponse ask(HistoryService& service, const char* method, const std::string& path, const std::string& query = std::string()) {
    static uint32_t ms = 5000;
    ms += 300;                                                   // (more than the HTTP side's look at its folder: every request looks)
    ctl::HttpRequest rq;
    rq.method = method;
    rq.path = path;
    rq.query = query;
    return service.handle(rq, ms);
}

ctl::JsonValue json_of(const ctl::HttpResponse& r) { return parse_text(r.body); }

// A recording that appears the way the game server's store makes one: written whole under a temporary name, then renamed
std::string put_recording(const fs::path& dir, const std::vector<uint8_t>& bytes, int64_t t, const std::string& map = "TINY", int n = 1) {
    const std::string id = id_at(t, map, n);
    const fs::path temp = dir / (id + ".antsrep.tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::error_code ec;
    fs::rename(temp, dir / (id + ".antsrep"), ec);
#if defined(_WIN32)
    static std::atomic<int> touches{0};                          // (NTFS does not always move a folder's time at once: a look must see this change)
    fs::last_write_time(dir, fs::file_time_type::clock::now() + std::chrono::milliseconds(++touches), ec);
#endif
    return id;
}

// What a folder holds: every name with its size and time (the service must leave the recordings as they are)
std::string folder_listing(const fs::path& dir) {
    std::set<std::string> lines;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::error_code entry_ec;
        const auto time = fs::last_write_time(e.path(), entry_ec).time_since_epoch().count();
        lines.insert(e.path().filename().string() + " " + std::to_string(e.is_regular_file() ? fs::file_size(e.path(), entry_ec) : 0) + " " + std::to_string(static_cast<long long>(time)));
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

bool same_answer(const ctl::HttpResponse& a, const ctl::HttpResponse& b) {
    return a.status == b.status && a.content_type == b.content_type && a.body == b.body && a.file == b.file;
}

struct Request {
    const char* method;
    std::string path;
    std::string query;
};

// Every route and query that the door's tests use (test_history_store.cpp, test_history_deaths.cpp, test_replay_store.cpp, test_server_public.cpp), for the ids and files that the rig has
std::vector<Request> door_requests(const std::vector<std::string>& ids, const std::vector<std::string>& files) {
    std::vector<Request> r;
    for (const char* q : {"", "limit=1", "limit=2&offset=1", "limit=100", "limit=0", "limit=101", "limit=abc", "offset=1000001", "offset=2", "sort=new", "sort=old", "sort=score", "sort=kills", "sort=hatched", "sort=long",
                          "sort=short", "sort=bogus", "format=all", "format=1v1", "format=team", "format=ffa", "format=bogus", "result=all", "result=decided", "result=draw", "result=cut", "result=bogus",
                          "who=all", "who=people", "who=computers", "who=bogus", "rec=all", "rec=watch", "rec=removed", "rec=old", "rec=bogus", "color=all", "color=green", "color=red", "color=blue", "color=black",
                          "color=bogus", "colour=blue", "color=blue&colour=blue", "mode=all", "mode=highest-score", "mode=187", "mode=bogus", "q=ann", "q=ANN", "q=bot", "q=treasure", "q=%25", "q=a%2", "q=%zz", "limit=1&limit=2",
                          "unknown=1", "limit", "limit=", "sort=score&format=ffa&who=people&result=all&rec=watch&limit=3&offset=0", "q=treasure&sort=kills", "q=tiny&sort=long&limit=2", "format=team&sort=hatched&offset=1",
                          "q=zzzz", "limit=-1", "offset=1.5", "sort=best", "format=3v3", "result=won", "who=robots", "rec=lost", "color=purple", "color=BLACK", "color=any", "colour=purple", "colour=BLACK", "colour=any",
                          "mode=chess", "mode=1", "mode=187&mode=187", "limit=5&limit=6", "q=%4", "q=%00", "q=%7F", "color", "colour", "colour=", "color=red&color=red", "colour=red&colour=red", "color=red&colour=red",
                          "colour=red&color=blue", "nonsense=1", "Limit=5", "sort=NEW", "&&limit=3&", "colour=blue&limit=5", "q=ann&sort=old&limit=1", "q=a%20b%2Bc+d", "colour=green", "colour=red", "colour=black",
                          "limit=100&offset=1000000&sort=short&format=team&result=cut&who=computers&rec=old&color=black&mode=187&q=a%20b%2Bc+d"}) {
        r.push_back({"GET", "/history", q});
    }
    r.push_back({"GET", "/history", "q=" + std::string(41, 'a')});
    r.push_back({"GET", "/history", "q=" + std::string(40, 'a')});
    for (const std::string& id : ids) {
        r.push_back({"GET", "/history/" + id, ""});
        r.push_back({"GET", "/history/" + id, "x=1"});
        r.push_back({"DELETE", "/history/" + id, ""});
        r.push_back({"POST", "/history/" + id, ""});
    }
    for (const char* path : {"/history/", "/history/nonsense", "/history/..%2f..", "/history/ants-NONE-20261008-143209Z", "/history/ants-TINY-20261008-143209Z.antsrep", "/history/ants-TINY-20261008-143209Z/x",
                             "/history/deaths/x", "/historyx", "/history/deaths"}) {
        r.push_back({"GET", path, ""});
    }
    for (const char* q : {"map=TREASURE.LVL", "map=treasure.lvl", "map=TINY.LVL", "map=", "map=a&b=c", "map=../../etc/passwd", "map=NOT%20A%20MAP.LVL", "map=%zz", "x=1"}) r.push_back({"GET", "/history/deaths", q});
    r.push_back({"GET", "/history/deaths", "map=" + std::string(120, 'a')});
    r.push_back({"GET", "/history/deaths", "map=" + std::string(121, 'a')});
    for (const char* method : {"POST", "PUT", "DELETE"}) {
        r.push_back({method, "/history", ""});
        r.push_back({method, "/history/deaths", "map=TREASURE.LVL"});
        r.push_back({method, "/replays", ""});
    }
    r.push_back({"GET", "/replays", ""});
    r.push_back({"GET", "/replays", "limit=5"});
    r.push_back({"GET", "/replays/", ""});
    r.push_back({"GET", "/replays/nothing", ""});
    r.push_back({"GET", "/replays/../x", ""});
    r.push_back({"GET", "/replays/notes.txt", ""});
    r.push_back({"GET", "/replays/ants-NONE-20261008-143209Z.antsrep", ""});
    for (const std::string& f : files) {
        r.push_back({"GET", "/replays/" + f, ""});
        r.push_back({"GET", "/replays/" + f, "x=1"});
        r.push_back({"DELETE", "/replays/" + f, ""});
    }
    for (const char* path : {"/", "/nothing", "/replay", "/replaysx", "/history.json", "/maps/files/x.lvl"}) r.push_back({"GET", path, ""});
    return r;
}

// How many of the requests that the two doors answer differently (each one is named on the log)
size_t differences(const std::vector<Request>& requests, const std::function<ctl::HttpResponse(const Request&)>& game, const std::function<ctl::HttpResponse(const Request&)>& service) {
    size_t different = 0;
    for (const Request& rq : requests) {
        const ctl::HttpResponse a = game(rq);
        const ctl::HttpResponse b = service(rq);
        if (same_answer(a, b)) continue;
        ++different;
        std::cout << "\n    differs: " << rq.method << " " << rq.path << (rq.query.empty() ? "" : "?" + rq.query) << " (game " << a.status << " " << a.body.substr(0, 80) << " | service " << b.status << " "
                  << b.body.substr(0, 80) << ")";
    }
    return different;
}

// The text of a table of a database, row by row in the order of its key: NULL and the empty text differ
std::string dump_table(db::Database& d, const std::string& table, const std::string& order) {
    db::Statement st;
    if (!d.prepare(st, "SELECT * FROM " + table + " ORDER BY " + order).ok()) return "(no table " + table + ")";
    std::string out;
    for (;;) {
        const db::Statement::Step step = st.step();
        if (step != db::Statement::Step::Row) break;
        for (int c = 0; c < st.column_count(); ++c) out += (st.column_is_null(c) ? std::string("<null>") : st.column_text(c)) + "|";
        out += "\n";
    }
    return out;
}

// The tables of the history, as one text; the plays and the likes' numbers are columns of the matches (the rows of the likes, which hold references, are not copied: HV1.2). The counted_at column is left out of the matches (the time a row was made): a row is copied with its own, and the rest must be equal
std::string dump_history(db::Database& d) {
    return dump_table(d, "history_matches", "id") + "--\n" + dump_table(d, "history_seats", "match_id, seat") + "--\n" + dump_table(d, "history_markers", "id") + "--\n" + dump_table(d, "history_deaths", "id") + "--\n" +
           dump_table(d, "history_action", "id") + "--\n" + dump_table(d, "map_plays", "map, hash");
}

size_t table_rows(db::Database& d, const std::string& table) {
    db::Statement st;
    if (!d.prepare(st, "SELECT count(*) FROM " + table).ok() || st.step() != db::Statement::Step::Row) return 0;
    return static_cast<size_t>(st.column_int64(0));
}

// The game server's side of a test: a room manager that counts the recordings that the test keeps, its database file and recordings folder in the test's temporary folder
struct GameSide {
    Server server;
    fs::path recordings() const { return server.temp.path / "replays"; }
    fs::path database() const { return server.temp.path / "ants.db"; }
    bool open() {
        HistoryStore::Config hc;
        hc.clock_us = fixed_clock;
        return server.open(true, hc);
    }
    // keeps the recordings (the numbers of match_file) and lets the room manager count them
    std::vector<std::string> keep_and_count(const std::vector<int>& which) {
        std::vector<std::string> ids;
        for (const int k : which) ids.push_back(server.keep(match_file(k)));
        for (int round = 0; round < 200 && server.mgr.history_store()->count() < ids.size(); ++round) server.run_updates(10);          // (the feeder's pace follows the real time that a play takes)
        return ids;
    }
    ctl::HttpResponse door(const Request& rq) const { return server.door(rq.method, rq.path, rq.query); }
};

// The files of the recordings folder, by name
std::vector<std::string> recording_files(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() == ReplayStore::kExtension) out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

#if !defined(_WIN32) && defined(ANTS_HAS_HISTORY_BINARY)
std::string history_binary_path() {
    const fs::path dir = ants_test_paths::history_dir();
    for (const char* sub : {"", "Release", "RelWithDebInfo", "MinSizeRel", "Debug"}) {
        const fs::path candidate = dir / sub / "ants_history";
        if (fs::exists(candidate)) return candidate.string();
    }
    return (dir / "ants_history").string();
}

// The real program as a child process of the test (only the process that was started is ever signalled)
struct Program {
    pid_t pid{-1};
    fs::path log;
    bool start(const std::vector<std::string>& args, const fs::path& log_file) {
        log = log_file;
        std::vector<std::string> storage;
        storage.push_back(history_binary_path());
        for (const std::string& a : args) storage.push_back(a);
        std::vector<char*> argv;
        for (std::string& a : storage) argv.push_back(&a[0]);
        argv.push_back(nullptr);
        pid = ::fork();
        if (pid < 0) return false;
        if (pid == 0) {
            const int fd = ::open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (fd >= 0) {
                ::dup2(fd, 1);
                ::dup2(fd, 2);
                ::close(fd);
            }
            ::execv(argv[0], argv.data());
            ::_exit(127);
        }
        return true;
    }
    // the exit status, or -1 when the program is still there after `timeout_ms`
    int wait_exit(int timeout_ms) {
        const auto began = std::chrono::steady_clock::now();
        for (;;) {
            int status = 0;
            const pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                pid = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            if (elapsed_ms(began) > timeout_ms) return -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    void signal_it(int sig) {
        if (pid > 0) ::kill(pid, sig);
    }
    std::string log_text() const {
        std::ifstream in(log);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    ~Program() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
    }
};

struct Answer {
    int status{0};
    std::string body;
};

// A blocking GET (the answer carries Connection: close)
Answer http_get(uint16_t port, const std::string& target) {
    Answer out;
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return out;
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return out;
    }
    const std::string request = "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    ::send(s, request.data(), request.size(), 0);
    std::string response;
    char buf[8192];
    for (;;) {
        const ssize_t n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
    }
    ::close(s);
    out.status = response.compare(0, 9, "HTTP/1.1 ") == 0 ? std::atoi(response.c_str() + 9) : 0;
    const size_t split = response.find("\r\n\r\n");
    if (split != std::string::npos) out.body = response.substr(split + 4);
    return out;
}

// The port that the program says it listens on, from its log
uint16_t port_in_log(const Program& p) {
    const std::string text = p.log_text();
    const std::string key = "public pages on port ";
    const size_t at = text.find(key);
    return at == std::string::npos ? uint16_t{0} : static_cast<uint16_t>(std::atoi(text.c_str() + at + key.size()));
}
#endif

}  // namespace

int main() {
    std::cout << "==================== HISTORY SERVICE SUITE (the same JSON as the game server's door, the first start, the counter, the HTTP side) ====================\n";

    // ---- the same answers ---------------------------------------------------------------------------------------------------------

    TEST_CASE("HV1.1 The same recordings give the same JSON, byte for byte, from the game server's public door and from the service: every route and query of the door's tests, the files, the errors, the other methods") {
        if (match_file(0).empty() || match_file(4).empty() || match_file(5).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        const std::vector<std::string> ids = game.keep_and_count({0, 1, 2, 3, 4, 5});
        ASSERT_EQ(game.server.mgr.history_store()->count(), ids.size());
        TempDir work;
        auto log = std::make_shared<Collector>();
        HistoryService service(base_config(work.path, game.recordings(), log));
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == ids.size(); }, 180000 * kSlow));
        const std::vector<std::string> files = recording_files(game.recordings());
        ASSERT_EQ(files.size(), ids.size());
        const std::vector<Request> requests = door_requests(ids, files);
        const size_t different = differences(requests, [&](const Request& rq) { return game.door(rq); }, [&](const Request& rq) { return ask(service, rq.method, rq.path, rq.query); });
        ASSERT_EQ(different, size_t{0});
        // what was compared was not an empty answer: the list, a record with its stats, the map of deaths and a file
        const ctl::JsonValue list = json_of(ask(service, "GET", "/history"));
        ASSERT_TRUE(list.get("count").as_int_or(0) == 6 && list.get("total").as_int_or(0) == 6 && list.get("history").size() == 6 && list.get("history").at(0).get("recording").str() == "watch");
        ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + ids[4])).get("details").get("v").as_int_or(0) == 1);
        ASSERT_EQ(json_of(ask(service, "GET", "/history/deaths", "map=TREASURE.LVL")).get("matches").as_int_or(0), int64_t{2});
        const ctl::HttpResponse file = ask(service, "GET", "/replays/" + files[0]);
        ASSERT_TRUE(file.status == 200 && file.file && file.body.size() == fs::file_size(game.recordings() / files[0]));
        ASSERT_EQ(json_of(ask(service, "GET", "/replays")).get("replays").size(), size_t{6});
        std::cout << "\n    " << requests.size() << " requests compared";
        ASSERT_TRUE(requests.size() > 100);
    } TEST_END();

    TEST_CASE("HV1.2 The history that the service copies from the game server's database gives the same JSON as the game server, with no recording on either side (every record 'removed'), and the tables are equal row for row") {
        if (match_file(0).empty() || match_file(4).empty() || match_file(5).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        const std::vector<std::string> ids = game.keep_and_count({0, 1, 2, 3, 4, 5});
        ASSERT_EQ(game.server.mgr.history_store()->count(), ids.size());
        // some of the matches have been played and liked (docs/REPLAY_COUNTS.md): the numbers are on the matches' rows and the likes' rows are a table, and both are copied
        uint32_t likes = 0;
        ASSERT_TRUE(game.server.mgr.history_store()->add_play(ids[0]) == HistoryStore::Counted::Done && game.server.mgr.history_store()->add_play(ids[0]) == HistoryStore::Counted::Done);
        ASSERT_TRUE(game.server.mgr.history_store()->like(ids[1], "0123456789abcdef", likes) == HistoryStore::Counted::Done && game.server.mgr.history_store()->like(ids[1], "fedcba9876543210", likes) == HistoryStore::Counted::Done && likes == 2);
        // the recordings go (their 30 days), and the game server's store forgets them
        for (const std::string& f : recording_files(game.recordings())) fs::remove(game.recordings() / f);
        fs::last_write_time(game.recordings(), fs::file_time_type(std::chrono::hours(100000)));
        game.server.mgr.replay_store()->rescan();
        ASSERT_EQ(game.server.mgr.replay_store()->count(), size_t{0});
        TempDir work;
        fs::create_directories(work.path / "none");
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, work.path / "none", log);
        cfg.game_db_path = game.database().string();
        cfg.submissions_dir = (work.path / "submissions").string();
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(service.import_report().imported && service.import_report().matches == ids.size());
        ASSERT_EQ(service.stats().matches, ids.size());
        const std::vector<Request> requests = door_requests(ids, std::vector<std::string>());
        const size_t different = differences(requests, [&](const Request& rq) { return game.door(rq); }, [&](const Request& rq) { return ask(service, rq.method, rq.path, rq.query); });
        ASSERT_EQ(different, size_t{0});
        const ctl::JsonValue list = json_of(ask(service, "GET", "/history"));
        ASSERT_TRUE(list.get("total").as_int_or(0) == 6 && list.get("history").at(0).get("recording").str() == "removed" && list.get("history").at(0).get("file").is_null());
        ASSERT_EQ(json_of(ask(service, "GET", "/history/deaths", "map=TREASURE.LVL")).get("matches").as_int_or(0), int64_t{2});
        ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + ids[0])).get("plays").as_int_or(-1) == 2 && json_of(ask(service, "GET", "/history/" + ids[1])).get("likes").as_int_or(-1) == 2);
        ASSERT_EQ(json_of(ask(service, "GET", "/history", "sort=likes&limit=1")).get("history").at(0).get("id").str(), ids[1]);        // (the sorts are the service's own too)
        service.stop();
        db::Database copy;
        ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
        const std::string theirs = dump_history(copy);
        ASSERT_EQ(dump_history(game.server.database), theirs);                       // (every table of the history, row for row, the play counts, the maps of deaths and the actions too)
        std::cout << "\n    tables: " << theirs.size() << " bytes, " << table_rows(copy, "history_deaths") << " maps of deaths, " << table_rows(copy, "history_action") << " actions, " << table_rows(copy, "map_plays") << " play counts";
        ASSERT_TRUE(theirs.size() > 1000 && table_rows(copy, "history_deaths") >= 1 && table_rows(copy, "history_action") >= 1 && table_rows(copy, "map_plays") > 0);
        ASSERT_EQ(service.import_report().action, table_rows(copy, "history_action"));
        ASSERT_EQ(table_rows(game.server.database, "replay_likes"), size_t{2});
        ASSERT_EQ(table_rows(copy, "replay_likes"), size_t{0});                       // (the numbers are copied with the matches; the rows that say who liked are not: they hold the hub's references, and Forget me does not reach this file)
    } TEST_END();

    TEST_CASE("HV1.2c A game database that is older than the plays and the likes (schema 11, no such column, no table of likes) is copied with 0 in their place, and the service's file has them from then on") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        const std::string id = "ants-TINY-20261008-143209Z";
        {
            db::Database old_game;
            const std::vector<db::Migration>& all = db_migrations();
            const std::vector<db::Migration> eleven(all.begin(), all.begin() + 11);
            ASSERT_TRUE(db::open_and_migrate(old_game, (work.path / "old.db").string(), eleven, db::OpenOptions()).ok());
            ASSERT_TRUE(old_game.exec("INSERT INTO history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, counted_at)"
                                      " VALUES ('" + id + "', 1, 'TINY.LVL', 'v1', 2400, 1, NULL, 'highest-score', '1v1', 'decided', 0, 5, 5, 0, 'tiny ann', '[]', 1)").ok());
            ASSERT_TRUE(old_game.exec("INSERT INTO history_seats VALUES ('" + id + "', 0, 'Ann', '', 5, 5, 0, 0, 0, 0, 0, 0, 1)").ok());
        }
        fs::create_directories(work.path / "none");
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, work.path / "none", log);
        cfg.game_db_path = (work.path / "old.db").string();
        cfg.submissions_dir = (work.path / "submissions").string();
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(service.import_report().imported && service.import_report().matches == 1);
        const ctl::JsonValue one = json_of(ask(service, "GET", "/history/" + id));
        ASSERT_TRUE(one.get("id").str() == id && one.get("plays").as_int_or(-1) == 0 && one.get("likes").as_int_or(-1) == 0);
        service.stop();
    } TEST_END();

    TEST_CASE("HV1.2b A row of history_action in the game server's database that is not a field of this version is not copied (the same guarantee as the store's put()): the others are, the report counts both, and the record of a match that has none gives no action") {
        if (match_file(0).empty() || match_file(4).empty() || match_file(5).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        const std::vector<std::string> ids = game.keep_and_count({0, 4, 5});
        const size_t actions = table_rows(game.server.database, "history_action");
        ASSERT_TRUE(actions >= 2);
        // two rows go bad (the column's own check holds only the length): a text that is no JSON, and one of another version
        std::string first, last;
        {
            db::Statement st;
            ASSERT_TRUE(game.server.database.prepare(st, "SELECT min(id), max(id) FROM history_action").ok() && st.step() == db::Statement::Step::Row);
            first = st.column_text(0);
            last = st.column_text(1);
        }
        ASSERT_TRUE(first != last);
        ASSERT_TRUE(game.server.database.exec("UPDATE history_action SET action = 'not a field' WHERE id = '" + first + "'").ok());
        ASSERT_TRUE(game.server.database.exec("UPDATE history_action SET action = '{\"v\":2}' WHERE id = '" + last + "'").ok());
        for (const std::string& f : recording_files(game.recordings())) fs::remove(game.recordings() / f);
        fs::last_write_time(game.recordings(), fs::file_time_type(std::chrono::hours(100000)));
        game.server.mgr.replay_store()->rescan();
        TempDir work;
        fs::create_directories(work.path / "none");
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, work.path / "none", log);
        cfg.game_db_path = game.database().string();
        cfg.submissions_dir = (work.path / "submissions").string();
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(service.import_report().imported && service.import_report().matches == ids.size());
        ASSERT_EQ(service.import_report().action, actions - 2);
        ASSERT_EQ(service.import_report().action_refused, size_t{2});
        ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + first)).get("id").is_string() && !json_of(ask(service, "GET", "/history/" + first)).has("action"));
        ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + last)).get("id").is_string() && !json_of(ask(service, "GET", "/history/" + last)).has("action"));
        size_t served = 0;
        for (const std::string& id : ids) {
            if (id != first && id != last && json_of(ask(service, "GET", "/history/" + id)).get("action").is_object()) ++served;
        }
        ASSERT_EQ(served, actions - 2);
        service.stop();
        db::Database copy;
        ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
        ASSERT_EQ(table_rows(copy, "history_action"), actions - 2);
    } TEST_END();

    TEST_CASE("HV1.3 The limits are the door's: a clock that runs on makes the same time box and the same budget answer, with the same words, and the plain list is not held back") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        // (a clock of its own for each side that moves 40 ms at each look: what a list took drains the budget of 100 ms, which is then used up, and the answer is 'busy')
        const auto stepping = []() {
            const auto now = std::make_shared<std::atomic<uint64_t>>(1000);
            return std::function<uint64_t()>([now]() { return now->fetch_add(40000) + 40000; });
        };
        GameSide game;
        HistoryStore::Config hc;
        hc.clock_us = stepping();
        ASSERT_TRUE(game.server.open(true, hc));
        game.keep_and_count({1, 2});
        TempDir work;
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, game.recordings(), log);
        cfg.history_clock_us = stepping();
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 2; }, 60000 * kSlow));
        std::vector<std::string> theirs, ours;
        for (int i = 0; i < 6; ++i) {
            const Request rq{"GET", "/history", "limit=5"};
            const ctl::HttpResponse a = game.door(rq);
            const ctl::HttpResponse b = ask(service, rq.method, rq.path, rq.query);
            theirs.push_back(std::to_string(a.status) + a.body);
            ours.push_back(std::to_string(b.status) + b.body);
        }
        ASSERT_EQ(ours.front().substr(0, 3), std::string("200"));
        ASSERT_EQ(ours.back().substr(0, 3), std::string("503"));
        ASSERT_EQ(ours, theirs);                                                                                         // (the same answers in the same order: the same box, the same budget, the same words)
        // the plain list has no query: it is not held back by the budget that the others have used up
        ASSERT_EQ(ask(service, "GET", "/history").status, 200);
        ASSERT_EQ(game.server.door("GET", "/history").status, 200);
    } TEST_END();

    // ---- the first start ----------------------------------------------------------------------------------------------------------

    TEST_CASE("HV2.1 The copy at the first start: every row, the markers (a match that the operator took out is not counted again from its recording that is still there), and a start again changes nothing") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        const std::vector<std::string> ids = game.keep_and_count({0, 1, 2});
        ASSERT_EQ(ids.size(), size_t{3});
        ASSERT_EQ(game.server.control("DELETE", "/history/" + ids[1]).status, 200);                            // (its recording stays: a marker does too)
        ASSERT_EQ(game.server.mgr.history_store()->marker_count(), size_t{1});
        TempDir work;
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, game.recordings(), log);
        cfg.game_db_path = game.database().string();
        cfg.submissions_dir = (work.path / "submissions").string();
        {
            HistoryService service(cfg);
            std::string why;
            ASSERT_TRUE(service.start(why));
            const ImportReport& report = service.import_report();
            ASSERT_TRUE(report.imported && !report.already_done && !report.not_empty && report.matches == 2 && report.markers == 1 && report.seats == 6);
            ASSERT_TRUE(!report.note.empty() && log->with(report.note) == 1);                                          // (the report is the one line that the log has of it)
            ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= 5; }, 20000));                          // (the counter has looked at the folder, in which all three recordings are)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ASSERT_EQ(service.stats().matches, size_t{2});
            ASSERT_EQ(service.stats().counted, size_t{0});                                                         // (nothing counted again: the two are copied, the third is the operator's marker)
            ASSERT_EQ(ask(service, "GET", "/history/" + ids[1]).status, 404);
            ASSERT_EQ(ask(service, "GET", "/history/" + ids[0]).status, 200);
            service.stop();
            db::Database copy;
            ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
            ASSERT_EQ(dump_history(game.server.database), dump_history(copy));
            ASSERT_EQ(table_rows(copy, "history_markers"), size_t{1});
        }
        // a start again: the marker row of `meta` says it was done, nothing is copied, the history is what it was
        const std::string before = dump_history(game.server.database);
        HistoryService again(cfg);
        std::string why;
        ASSERT_TRUE(again.start(why));
        ASSERT_TRUE(again.import_report().already_done && !again.import_report().imported);
        ASSERT_TRUE(!again.import_report().note.empty() && log->with(again.import_report().note) == 1);
        ASSERT_EQ(again.stats().matches, size_t{2});
        again.stop();
        db::Database copy;
        ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
        ASSERT_EQ(dump_history(copy), before);
        ASSERT_EQ(table_rows(copy, "meta"), size_t{2});                                                             // (created_at and history_import: one marker, not two)
    } TEST_END();

    TEST_CASE("HV2.2 A match that the game server counts after the copy is counted by the service from its recording, once: the play counts of the maps equal the game server's, none is doubled or lost") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        game.keep_and_count({0, 1});
        TempDir work;
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, game.recordings(), log);
        cfg.game_db_path = game.database().string();
        cfg.submissions_dir = (work.path / "submissions").string();
        {
            HistoryService service(cfg);
            std::string why;
            ASSERT_TRUE(service.start(why));
            ASSERT_EQ(service.stats().matches, size_t{2});
            service.stop();
        }
        // the game server goes on counting (it is not told of the service), and the service starts again, with the same options
        const std::string id = game.server.keep(match_file(3));
        for (int round = 0; round < 100 && game.server.mgr.history_store()->count() < 3; ++round) game.server.run_updates(10);
        ASSERT_EQ(game.server.mgr.history_store()->count(), size_t{3});
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(service.import_report().already_done);                                                          // (not copied again: the third match comes from its recording)
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 3; }, 60000 * kSlow));
        ASSERT_EQ(service.stats().counted, size_t{1});
        const ctl::HttpResponse third = ask(service, "GET", "/history/" + id);
        ASSERT_EQ(third.status, 200);
        service.stop();
        db::Database copy;
        ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
        ASSERT_EQ(dump_table(copy, "map_plays", "map, hash"), dump_table(game.server.database, "map_plays", "map, hash"));
        ASSERT_EQ(dump_table(copy, "history_seats", "match_id, seat"), dump_table(game.server.database, "history_seats", "match_id, seat"));
        ASSERT_EQ(json_of(third).get("id").str(), id);
    } TEST_END();

    TEST_CASE("HV2.3 A file of the service that holds history of its own, and no marker, is left as it is (nothing is copied, no marker is made, the log says why); the same game database is copied into a new file") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        game.keep_and_count({1, 2});
        TempDir work;
        auto log = std::make_shared<Collector>();
        {
            HistoryService own(base_config(work.path, game.recordings(), log));              // (counts the recordings itself: no game database was given)
            std::string why;
            ASSERT_TRUE(own.start(why));
            ASSERT_TRUE(wait_for([&]() { return own.stats().matches == 2; }, 60000 * kSlow));
            own.stop();
        }
        ServiceConfig cfg = base_config(work.path, game.recordings(), log);
        cfg.game_db_path = game.database().string();
        cfg.submissions_dir = (work.path / "submissions").string();
        {
            HistoryService service(cfg);
            std::string why;
            ASSERT_TRUE(service.start(why));
            ASSERT_TRUE(service.import_report().not_empty && !service.import_report().imported);
            ASSERT_TRUE(!service.import_report().note.empty() && log->with(service.import_report().note) == 1);
            ASSERT_EQ(service.stats().matches, size_t{2});
            service.stop();
            db::Database copy;
            ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
            ASSERT_EQ(table_rows(copy, "meta"), size_t{1});                                                          // (no marker)
        }
        TempDir fresh;
        cfg.db_path = (fresh.path / "history.db").string();
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(service.import_report().imported && service.import_report().matches == 2);
        service.stop();
    } TEST_END();

    TEST_CASE("HV2.4 A game database that cannot be copied stops the start with a reason and leaves the service's file without a row: no such file, a file that is no database, a database of no history, the service's own file, a row that cannot be copied") {
        TempDir work;
        auto log = std::make_shared<Collector>();
        fs::create_directories(work.path / "rec");
        const auto attempt = [&](const std::string& source, std::string& why, const std::string& db_name) {
            ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
            cfg.db_path = (work.path / db_name).string();
            cfg.game_db_path = source;
            cfg.submissions_dir = (work.path / "submissions").string();
            HistoryService service(cfg);
            return service.start(why);
        };
        const auto rows_of = [&](const std::string& db_name) {
            db::Database d;
            db::OpenOptions options;
            options.create = false;
            options.wal = false;
            if (!d.open((work.path / db_name).string(), options).ok()) return size_t{9999};
            return table_rows(d, "history_matches") + table_rows(d, "history_seats") + table_rows(d, "history_markers") + table_rows(d, "map_plays") + table_rows(d, "meta") - 1;
        };
        std::string why;
        ASSERT_FALSE(attempt((work.path / "nothing.db").string(), why, "a.db"));                                    // no such file
        ASSERT_TRUE(contains(why, "nothing.db"));                                                                    // (the reason names the file)
        ASSERT_EQ(rows_of("a.db"), size_t{0});
        ASSERT_FALSE(fs::exists(work.path / "nothing.db"));                                                         // (and it was not made: it is opened read only)
        {
            std::ofstream junk(work.path / "junk.db", std::ios::binary);
            junk << std::string(4096, 'x');
        }
        ASSERT_FALSE(attempt((work.path / "junk.db").string(), why, "b.db"));                                       // not a database
        ASSERT_FALSE(why.empty());
        ASSERT_EQ(rows_of("b.db"), size_t{0});
        {
            db::Database other;
            ASSERT_TRUE(other.open((work.path / "other.db").string()).ok());
            ASSERT_TRUE(other.exec("CREATE TABLE unrelated (a INTEGER)").ok());
        }
        ASSERT_FALSE(attempt((work.path / "other.db").string(), why, "c.db"));                                      // a database of something else
        ASSERT_FALSE(why.empty());
        ASSERT_EQ(rows_of("c.db"), size_t{0});
        ASSERT_FALSE(attempt((work.path / "d.db").string(), why, "d.db"));                                          // the service's own file named as the source
        ASSERT_FALSE(why.empty());
        {
            db::Database loose;                                                                                       // the right names and no constraints, and a row that the service's table cannot take (no id)
            ASSERT_TRUE(loose.open((work.path / "loose.db").string()).ok());
            ASSERT_TRUE(loose.exec("CREATE TABLE history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, details, counted_at);"
                                   "CREATE TABLE history_seats (match_id, seat, name, bot, score, killed, lost, hatched, bombs_planted, bombs_defused, fires_lit, left_early, winner);"
                                   "CREATE TABLE history_markers (id, deleted_at);"
                                   "CREATE TABLE map_plays (map, hash, plays, last_played);"
                                   "INSERT INTO history_matches VALUES ('ants-TINY-20261008-143209Z', 1, 'TINY.LVL', 'v1', 100, 1, NULL, 'highest-score', '1v1', 'decided', 0, 1, 1, 1, 'tiny', '[]', NULL, 1);"
                                   "INSERT INTO history_matches VALUES (NULL, 1, 'TINY.LVL', 'v1', 100, 1, NULL, 'highest-score', '1v1', 'decided', 0, 1, 1, 1, 'tiny', '[]', NULL, 1);").ok());
        }
        ASSERT_FALSE(attempt((work.path / "loose.db").string(), why, "e.db"));
        ASSERT_FALSE(why.empty());
        ASSERT_EQ(rows_of("e.db"), size_t{0});                                                                      // (the first row went in and was taken back with the rest)
        ASSERT_FALSE(attempt((work.path / "e.db").string(), why, "e.db"));
        ASSERT_FALSE(why.empty());
    } TEST_END();

    TEST_CASE("HV2.5 A match that the operator took out stays out: a recordings folder that is empty (a mount that is not the game server's) does not delete its marker, the service says the folder is empty, and a start on the real folder does not count the match again") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        GameSide game;
        ASSERT_TRUE(game.open());
        const std::vector<std::string> ids = game.keep_and_count({0, 1, 2});
        ASSERT_EQ(ids.size(), size_t{3});
        ASSERT_EQ(game.server.control("DELETE", "/history/" + ids[1]).status, 200);                            // (its recording stays: a marker does too)
        TempDir work;
        fs::create_directories(work.path / "empty");
        auto log = std::make_shared<Collector>();
        ServiceConfig real = base_config(work.path, game.recordings(), log);
        real.game_db_path = game.database().string();
        real.submissions_dir = (work.path / "submissions").string();
        ServiceConfig wrong = real;
        wrong.recordings_dir = (work.path / "empty").string();
        {
            HistoryService service(real);
            std::string why;
            ASSERT_TRUE(service.start(why));
            ASSERT_TRUE(service.import_report().imported && service.import_report().markers == 1);
            ASSERT_FALSE(service.recordings_folder_empty_with_history());                                       // (a folder with recordings: nothing to say)
            service.stop();
        }
        size_t quiet_lines = 0;                                                                                  // (the lines that name an empty folder when nothing is to be said: a new file, no history)
        {
            ServiceConfig fresh = wrong;
            fresh.db_path = (work.path / "fresh.db").string();
            fresh.game_db_path.clear();
            fresh.submissions_dir.clear();
            const size_t before = log->with(wrong.recordings_dir);
            HistoryService service(fresh);
            std::string why;
            ASSERT_TRUE(service.start(why));
            ASSERT_FALSE(service.recordings_folder_empty_with_history());
            service.stop();
            quiet_lines = log->with(wrong.recordings_dir) - before;
        }
        {
            const size_t before = log->with(wrong.recordings_dir);
            HistoryService service(wrong);                                                                       // (the same file as the real one, a folder with no recording)
            std::string why;
            ASSERT_TRUE(service.start(why));
            ASSERT_TRUE(service.recordings_folder_empty_with_history());
            ASSERT_EQ(log->with(wrong.recordings_dir) - before, quiet_lines + 1);                                // (one line more than for a new file: it says so, and names the folder)
            ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= 5; }, 20000));                          // (the counter has had its passes: the old code deleted the marker in the first)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            ASSERT_EQ(service.stats().matches, size_t{2});
            service.stop();
            db::Database copy;
            ASSERT_TRUE(copy.open((work.path / "history.db").string()).ok());
            ASSERT_EQ(table_rows(copy, "history_markers"), size_t{1});                                          // (the marker is still there)
            ASSERT_EQ(table_rows(copy, "history_matches"), size_t{2});
        }
        HistoryService again(real);                                                                              // (the real folder again)
        std::string why;
        ASSERT_TRUE(again.start(why));
        ASSERT_FALSE(again.recordings_folder_empty_with_history());
        ASSERT_TRUE(wait_for([&]() { return again.stats().passes >= 5; }, 20000));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ASSERT_EQ(again.stats().matches, size_t{2});
        ASSERT_EQ(again.stats().counted, size_t{0});                                                             // (the match that was taken out is not counted again from its recording)
        ASSERT_EQ(ask(again, "GET", "/history/" + ids[1]).status, 404);
        ASSERT_EQ(json_of(ask(again, "GET", "/history")).get("total").as_int_or(-1), int64_t{2});
    } TEST_END();

    // ---- the counter --------------------------------------------------------------------------------------------------------------

    TEST_CASE("HV3.1 A recording is counted within five seconds of its file appearing, with the program's own settings (a second to settle, a look every 250 ms), and the page shows it with its recording") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        fs::create_directories(work.path / "rec");
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
        cfg.settle_s = 1;
        cfg.look_ms = 250;
        cfg.pace_factor = 1;
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_FALSE(service.recordings_folder_empty_with_history());                                               // (a new service: no folder to doubt, nothing to say)
        ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= 2; }, 10000 * kSlow));
        ASSERT_EQ(json_of(ask(service, "GET", "/history")).get("total").as_int_or(-1), int64_t{0});
        int64_t slowest = 0;
        for (int round = 0; round < 3; ++round) {
            const auto began = std::chrono::steady_clock::now();
            const std::string id = put_recording(work.path / "rec", match_file(round == 1 ? 2 : 1), kT0 + round * 100);
            ASSERT_TRUE(wait_for([&]() { return service.stats().matches == static_cast<size_t>(round) + 1; }, 60000 * kSlow));
            const int64_t took = elapsed_ms(began);
            slowest = std::max(slowest, took);
            const ctl::JsonValue list = json_of(ask(service, "GET", "/history"));
            ASSERT_TRUE(list.get("total").as_int_or(0) == round + 1 && list.get("history").at(0).get("id").str() == id);
            ASSERT_TRUE(list.get("history").at(0).get("recording").str() == "watch" && list.get("history").at(0).get("file").str() == history_recording_file(id));
            std::cout << "\n    recording " << round + 1 << " counted after " << took << " ms";
            if (!kThreadSanitizer) ASSERT_LT(took, int64_t{5000});
        }
        ASSERT_TRUE(slowest > 0);
        ASSERT_EQ(service.stats().counted, size_t{3});
    } TEST_END();

    TEST_CASE("HV3.2 Recordings that arrive together are all counted once; a start again counts nothing twice; a recording that the game server deletes leaves its record ('removed') and goes from /replays") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        fs::create_directories(work.path / "rec");
        auto log = std::make_shared<Collector>();
        std::vector<std::string> ids;
        {
            HistoryService service(base_config(work.path, work.path / "rec", log));
            std::string why;
            ASSERT_TRUE(service.start(why));
            for (int i = 0; i < 5; ++i) ids.push_back(put_recording(work.path / "rec", match_file(1 + i % 3), kT0 + i * 5));
            ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 5; }, 120000 * kSlow));
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ASSERT_TRUE(service.stats().matches == 5 && service.stats().counted == 5 && service.stats().pending == 0);
            const ctl::JsonValue list = json_of(ask(service, "GET", "/history"));
            ASSERT_TRUE(list.get("count").as_int_or(0) == 5 && list.get("total").as_int_or(0) == 5);
            std::set<std::string> seen;
            for (size_t i = 0; i < list.get("history").size(); ++i) seen.insert(list.get("history").at(i).get("id").str());
            ASSERT_EQ(seen, std::set<std::string>(ids.begin(), ids.end()));
            // the game server's own purge takes a file away: the service sees the folder change, keeps the record, and the list of recordings loses it
            fs::remove(work.path / "rec" / history_recording_file(ids[0]));
            ASSERT_TRUE(wait_for([&]() { return json_of(ask(service, "GET", "/replays")).get("count").as_int_or(9) == 4; }, 10000));
            const ctl::JsonValue gone = json_of(ask(service, "GET", "/history/" + ids[0]));
            ASSERT_TRUE(gone.get("id").str() == ids[0] && gone.get("recording").str() == "removed" && gone.get("file").is_null());
            ASSERT_EQ(json_of(ask(service, "GET", "/history", "rec=removed")).get("count").as_int_or(-1), int64_t{1});
            ASSERT_EQ(ask(service, "GET", "/replays/" + history_recording_file(ids[0])).status, 404);
            service.stop();
        }
        HistoryService again(base_config(work.path, work.path / "rec", log));
        std::string why;
        ASSERT_TRUE(again.start(why));
        ASSERT_TRUE(wait_for([&]() { return again.stats().passes >= 6; }, 20000));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ASSERT_TRUE(again.stats().matches == 5 && again.stats().counted == 0);                                       // (none counted again, none lost)
        ASSERT_EQ(json_of(ask(again, "GET", "/history")).get("total").as_int_or(-1), int64_t{5});
    } TEST_END();

    TEST_CASE("HV3.3 A recording that cannot be counted (its map is not here) is left out with one line in the log, however often the service looks, and does not hold up the others") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work, nowhere;
        fs::create_directories(work.path / "rec");
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
        cfg.maps_dir = nowhere.path.string();                                                                       // (no map at all: the recording of TINY has no map to be played on)
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        const std::string id = put_recording(work.path / "rec", match_file(1), kT0);
        ASSERT_TRUE(wait_for([&]() { return service.stats().left_out == 1; }, 30000 * kSlow));
        const uint64_t passes = service.stats().passes;
        ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= passes + 10; }, 10000));
        ASSERT_TRUE(service.stats().matches == 0 && service.stats().counted == 0 && service.stats().left_out == 1);
        ASSERT_EQ(log->with(id), size_t{1});                                                                         // (one line names the recording, however often the counter looked)
        ASSERT_EQ(json_of(ask(service, "GET", "/history")).get("total").as_int_or(-1), int64_t{0});
        ASSERT_EQ(json_of(ask(service, "GET", "/replays")).get("count").as_int_or(-1), int64_t{1});                  // (the recording is still listed and can be fetched: it can be watched in a build that has the map)
    } TEST_END();

    TEST_CASE("HV3.4 A match on a map that the game server's operator approved is counted when the game database and its submissions folder are named, and left out when they are not") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work, mapdir;
        fs::create_directories(work.path / "rec");
        fs::create_directories(work.path / "submissions" / "files");
        fs::copy_file(maps_dir() + "/TINY.LVL", mapdir.path / "Sample Map.lvl");                                    // (a copy of TINY under the approved map's name, kept under a file name the server made)
        fs::copy_file(maps_dir() + "/TINY.LVL", work.path / "submissions" / "files" / "0123456789abcdef0123456789abcdef.lvl");
        MatchSpec spec;
        spec.map = "Sample Map";
        spec.file = "Sample Map.lvl";
        spec.dir = mapdir.path.string();
        spec.seed = 31;
        spec.turns = 700;
        spec.roster = 0x03;
        spec.names = {"Una", "Vic", "", ""};
        const std::vector<uint8_t> bytes = record_match(spec);
        ASSERT_FALSE(bytes.empty());
        const std::string id = put_recording(work.path / "rec", bytes, kT0, "Sample_Map");
        TempDir empty_maps;
        {
            db::Database game;                                                                                      // (the game server's database, with the one row that matters: the approved map)
            ASSERT_TRUE(open_server_db(game, (work.path / "game.db").string()));
            ASSERT_TRUE(game.exec("INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, final_name, final_key, renamed, maker, note, owner_message, width, height, minutes, players, description,"
                                  " size, hash, findings, file, file_kept, created_at, decided_at, decided_by) VALUES ('0123456789abcdef0123456789abcdef', '" + std::string(64, 'a') +
                                  "', 'approved', 'classic', 'Sample Map', 'sample map', 'Sample Map', 'sample map', 0, 'Una', '', '', 10, 10, 5, 2, '', 100, '0123456789abcdef', '[]', "
                                  "'0123456789abcdef0123456789abcdef.lvl', 1, 1, 2, 'operator')").ok());
        }
        auto log = std::make_shared<Collector>();
        {
            ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
            cfg.maps_dir = empty_maps.path.string();
            cfg.db_path = (work.path / "with.db").string();
            cfg.game_db_path = (work.path / "game.db").string();
            cfg.submissions_dir = (work.path / "submissions").string();
            HistoryService service(cfg);
            std::string why;
            ASSERT_TRUE(service.start(why));
            ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 1; }, 30000 * kSlow));
            ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + id)).get("map").str() == "Sample Map.lvl");
            service.stop();
        }
        ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
        cfg.maps_dir = empty_maps.path.string();
        cfg.db_path = (work.path / "without.db").string();
        HistoryService without(cfg);
        std::string why;
        ASSERT_TRUE(without.start(why));
        ASSERT_TRUE(wait_for([&]() { return without.stats().left_out == 1; }, 30000 * kSlow));
        ASSERT_EQ(without.stats().matches, size_t{0});
    } TEST_END();

    TEST_CASE("HV3.6 A match that names an approved map in another letter case than the operator's list holds it is counted (the lobby's rule), and a name that differs in more than the case is left out") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work, mapdir;
        fs::create_directories(work.path / "rec");
        fs::create_directories(work.path / "submissions" / "files");
        fs::copy_file(maps_dir() + "/TINY.LVL", mapdir.path / "SAMPLE MAP.lvl");                                    // (the recording names the map in capitals; the list says "Sample Map")
        fs::copy_file(maps_dir() + "/TINY.LVL", mapdir.path / "Sample_Map.lvl");                                    // (and one that names it with an underscore)
        fs::copy_file(maps_dir() + "/TINY.LVL", work.path / "submissions" / "files" / "0123456789abcdef0123456789abcdef.lvl");
        const auto recording = [&](const char* file, uint32_t seed, int64_t ended) {
            MatchSpec spec;
            spec.map = "Sample Map";
            spec.file = file;
            spec.dir = mapdir.path.string();
            spec.seed = seed;
            spec.turns = 700;
            spec.roster = 0x03;
            spec.names = {"Una", "Vic", "", ""};
            const std::vector<uint8_t> bytes = record_match(spec);
            return bytes.empty() ? std::string() : put_recording(work.path / "rec", bytes, ended, "Sample_Map");
        };
        const std::string folded = recording("SAMPLE MAP.lvl", 31, kT0);
        const std::string other = recording("Sample_Map.lvl", 32, kT0 + 1000);
        ASSERT_TRUE(!folded.empty() && !other.empty());
        {
            db::Database game;
            ASSERT_TRUE(open_server_db(game, (work.path / "game.db").string()));
            ASSERT_TRUE(game.exec("INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, final_name, final_key, renamed, maker, note, owner_message, width, height, minutes, players, description,"
                                  " size, hash, findings, file, file_kept, created_at, decided_at, decided_by) VALUES ('0123456789abcdef0123456789abcdef', '" + std::string(64, 'a') +
                                  "', 'approved', 'classic', 'Sample Map', 'sample map', 'Sample Map', 'sample map', 0, 'Una', '', '', 10, 10, 5, 2, '', 100, '0123456789abcdef', '[]', "
                                  "'0123456789abcdef0123456789abcdef.lvl', 1, 1, 2, 'operator')").ok());
        }
        TempDir empty_maps;
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
        cfg.maps_dir = empty_maps.path.string();
        cfg.db_path = (work.path / "with.db").string();
        cfg.game_db_path = (work.path / "game.db").string();
        cfg.submissions_dir = (work.path / "submissions").string();
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 1 && service.stats().left_out == 1; }, 30000 * kSlow));
        ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + folded)).get("map").str() == "SAMPLE MAP.lvl");
        ASSERT_EQ(json_of(ask(service, "GET", "/history/" + other)).get("error").str(), std::string("no such match"));
        service.stop();
    } TEST_END();

    TEST_CASE("HV3.5 A match on an approved map that was left out because the list of approved maps could not be read, or did not hold the map yet, is counted when the list can be read again or has the map: not only at the next start") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir mapdir, empty_maps;
        fs::copy_file(maps_dir() + "/TINY.LVL", mapdir.path / "Sample Map.lvl");                                    // (a copy of TINY under the approved map's name)
        MatchSpec spec;
        spec.map = "Sample Map";
        spec.file = "Sample Map.lvl";
        spec.dir = mapdir.path.string();
        spec.seed = 31;
        spec.turns = 700;
        spec.roster = 0x03;
        spec.names = {"Una", "Vic", "", ""};
        const std::vector<uint8_t> bytes = record_match(spec);
        ASSERT_FALSE(bytes.empty());
        const auto approve = [](db::Database& d) {                                                                  // (the operator approves the map: the one row that matters)
            return d.exec("INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, final_name, final_key, renamed, maker, note, owner_message, width, height, minutes, players, description,"
                          " size, hash, findings, file, file_kept, created_at, decided_at, decided_by) VALUES ('0123456789abcdef0123456789abcdef', '" + std::string(64, 'a') +
                          "', 'approved', 'classic', 'Sample Map', 'sample map', 'Sample Map', 'sample map', 0, 'Una', '', '', 10, 10, 5, 2, '', 100, '0123456789abcdef', '[]', "
                          "'0123456789abcdef0123456789abcdef.lvl', 1, 1, 2, 'operator')").ok();
        };
        // one rig for both: a service on the player's map, whose recording appears while the list cannot give the map
        const auto rig = [&](bool list_unreadable) {
            TempDir work;
            fs::create_directories(work.path / "rec");
            fs::create_directories(work.path / "submissions" / "files");
            fs::copy_file(maps_dir() + "/TINY.LVL", work.path / "submissions" / "files" / "0123456789abcdef0123456789abcdef.lvl");
            const fs::path game_db = work.path / "game.db";
            {
                db::Database game;
                ASSERT_TRUE(open_server_db(game, game_db.string()));
                if (list_unreadable) ASSERT_TRUE(approve(game));                                                    // (the map is approved; its list cannot be read for a while)
            }
            auto log = std::make_shared<Collector>();
            ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
            cfg.maps_dir = empty_maps.path.string();
            cfg.game_db_path = game_db.string();
            cfg.submissions_dir = (work.path / "submissions").string();
            if (!list_unreadable) cfg.approved_refresh_s = 1;                                                       // (a list that can be read is read again every refresh; one that could not be read, within five seconds, whatever the refresh)
            HistoryService service(cfg);
            std::string why;
            ASSERT_TRUE(service.start(why));
            const fs::path away = work.path / "game.db.away";
            if (list_unreadable) fs::rename(game_db, away);                                                         // (the game server's file is not there for a moment)
            const std::string id = put_recording(work.path / "rec", bytes, kT0, "Sample_Map");
            ASSERT_TRUE(wait_for([&]() { return service.stats().left_out == 1; }, 30000 * kSlow));
            ASSERT_EQ(service.stats().matches, size_t{0});
            ASSERT_EQ(log->with(id), size_t{1});                                                                     // (left out with one line, whatever the counter does next)
            const uint64_t passes = service.stats().passes;
            ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= passes + 10; }, 10000));
            ASSERT_TRUE(service.stats().matches == 0 && service.stats().counted == 0);                               // (while the list cannot give the map, it stays out)
            if (list_unreadable) {
                fs::rename(away, game_db);                                                                           // (the file is back)
            } else {
                db::Database operator_side;
                ASSERT_TRUE(open_server_db(operator_side, game_db.string()));
                ASSERT_TRUE(approve(operator_side));                                                                 // (the operator approves the map now)
            }
            ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 1; }, 20000 * kSlow));
            ASSERT_EQ(service.stats().counted, size_t{1});
            ASSERT_EQ(service.stats().left_out, size_t{0});
            ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + id)).get("map").str() == "Sample Map.lvl");
            ASSERT_EQ(log->with(id), size_t{1});                                                                     // (and nothing more was said of it)
            service.stop();
        };
        rig(true);
        rig(false);
    } TEST_END();

    // ---- files that are not whole ---------------------------------------------------------------------------------------------------

    TEST_CASE("HV4.1 A damaged or half written recording is neither counted nor listed and does not stop the others; a whole file put over it by a rename is counted; a '.tmp' of a crash and foreign files are left alone") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        const std::vector<uint8_t>& whole = match_file(1);
        const auto write = [&](const std::string& name, const std::string& bytes) {
            std::ofstream out(rec / name, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        };
        const std::string half(reinterpret_cast<const char*>(whole.data()), whole.size() / 2);
        write(id_at(kT0) + ".antsrep", half);                                                                       // a copy that stopped half way
        write(id_at(kT0 + 10) + ".antsrep", std::string(2000, '\x7f'));                                              // not a replay at all
        write(id_at(kT0 + 20) + ".antsrep", std::string());                                                          // empty
        write("notes.txt", "not ours");
        write("ants-TINY-20261008-143209Z.antsrep.bak", "not ours either");
        write(id_at(kT0 + 30) + ".antsrep.tmp", half);                                                               // the leftover of a write that a crash cut off
        auto log = std::make_shared<Collector>();
        HistoryService service(base_config(work.path, rec, log));
        std::string why;
        ASSERT_TRUE(service.start(why));
        put_recording(rec, whole, kT0 + 40);                                                                         // a whole one arrives
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 1; }, 60000 * kSlow));
        ASSERT_EQ(json_of(ask(service, "GET", "/replays")).get("count").as_int_or(-1), int64_t{1});                  // (the damaged ones are not listed)
        ASSERT_EQ(ask(service, "GET", "/replays/" + id_at(kT0) + ".antsrep").status, 404);
        ASSERT_EQ(ask(service, "GET", "/replays/" + id_at(kT0 + 10) + ".antsrep").status, 404);
        ASSERT_EQ(json_of(ask(service, "GET", "/history")).get("total").as_int_or(-1), int64_t{1});
        ASSERT_EQ(ask(service, "GET", "/history").status, 200);
        // the half file is finished: the whole file arrives under its name by a rename, as the store writes one
        const std::string finished = put_recording(rec, whole, kT0);
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 2; }, 60000 * kSlow));
        ASSERT_EQ(json_of(ask(service, "GET", "/replays")).get("count").as_int_or(-1), int64_t{2});
        ASSERT_TRUE(json_of(ask(service, "GET", "/history/" + finished)).get("recording").str() == "watch");
        service.stop();
        // nothing was written, deleted or touched in the folder by the service: the leftovers are there as they were
        ASSERT_TRUE(fs::exists(rec / (id_at(kT0 + 30) + ".antsrep.tmp")) && fs::exists(rec / "notes.txt") && fs::exists(rec / (id_at(kT0 + 10) + ".antsrep")));
        ASSERT_EQ(fs::file_size(rec / (id_at(kT0 + 30) + ".antsrep.tmp")), half.size());
    } TEST_END();

    TEST_CASE("HV4.2 The service only reads the recordings folder: every file that was there is byte for byte and time for time as it was, a file older than the 30 days is not deleted, and no file is made in it") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        put_recording(rec, match_file(1), 1500000000);                                                               // 2017: far older than the 30 days of the lists
        put_recording(rec, match_file(2), kT0);
        const std::string before = folder_listing(rec);
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, rec, log);
        cfg.keep_days = 1;
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 2; }, 60000 * kSlow));
        ASSERT_EQ(json_of(ask(service, "GET", "/replays")).get("count").as_int_or(-1), int64_t{2});                  // (listed, though it is older than the keep days: the game server's purge deletes, not this)
        ASSERT_EQ(ask(service, "DELETE", "/replays/" + history_recording_file(id_at(kT0))).status, 404);              // (no delete, whatever the method)
        service.stop();
        ASSERT_EQ(folder_listing(rec), before);
    } TEST_END();

    // ---- a folder of thousands ----------------------------------------------------------------------------------------------------

    TEST_CASE("HV5.1 A folder of thousands of recordings: the service starts, a hundred more are seen by a look, and the lists stay quick") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        constexpr int kFiles = 3000;
        TempDir work, nowhere;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        for (int i = 0; i < kFiles; ++i) {                                                                           // (the same match under thousands of names: what is read of a file is the same work)
            const fs::path to = rec / (id_at(kT0 + i * 7) + ".antsrep");
            std::ofstream out(to, std::ios::binary);
            out.write(reinterpret_cast<const char*>(match_file(1).data()), static_cast<std::streamsize>(match_file(1).size()));
        }
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, rec, log);
        cfg.maps_dir = nowhere.path.string();                                                                       // (nothing is played: every file is left out at once, so that the test measures the folder, not the engine)
        HistoryService service(cfg);
        std::string why;
        const auto began = std::chrono::steady_clock::now();
        ASSERT_TRUE(service.start(why));
        const int64_t start_ms = elapsed_ms(began);
        auto timed = [&](const char* path, const char* query) {
            const auto t = std::chrono::steady_clock::now();
            const ctl::HttpResponse r = ask(service, "GET", path, query);
            return std::make_pair(r, elapsed_ms(t));
        };
        const auto list = timed("/replays", "");
        ASSERT_TRUE(list.first.status == 200 && json_of(list.first).get("count").as_int_or(0) == kFiles && json_of(list.first).get("replays").size() == size_t{200});
        for (int i = 0; i < 100; ++i) put_recording(rec, match_file(1), kT0 + (kFiles + i) * 7);
        const auto more = std::chrono::steady_clock::now();
        ASSERT_TRUE(wait_for([&]() { return json_of(ask(service, "GET", "/replays")).get("count").as_int_or(0) == kFiles + 100; }, 60000 * kSlow));
        const int64_t look_ms = elapsed_ms(more);
        ASSERT_TRUE(wait_for([&]() { return service.stats().left_out >= 50; }, 60000 * kSlow));                         // (the counter has the new files too: it is working through them)
        const auto page = timed("/history", "");
        ASSERT_EQ(page.first.status, 200);
        std::cout << "\n    " << kFiles << " files: start " << start_ms << " ms, " << list.second << " ms for the list, 100 more seen after " << look_ms << " ms, the history page " << page.second << " ms";
        if (!kThreadSanitizer) {
            ASSERT_LT(start_ms, int64_t{30000});
            ASSERT_LT(list.second, int64_t{500});
            ASSERT_LT(page.second, int64_t{500});
            ASSERT_LT(look_ms, int64_t{5000});
        }
    } TEST_END();

    // ---- the counter and the HTTP side at the same time -------------------------------------------------------------------------------

    TEST_CASE("HV6.1 While the counter counts a stream of recordings the HTTP side answers without a pause: every list is whole (the count equals the total of a plain list, an id is never listed twice), the totals only grow, and the end is what was put in") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        constexpr int kStream = 9;
        for (const int k : {1, 2, 3}) ASSERT_FALSE(match_file(k).empty());                                           // (made now, on this thread: the dropper only copies them)
        TempDir work;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, rec, log);
        cfg.settle_s = 1;                                                                                            // (the program's own: the files are counted as the store would have them)
        cfg.look_ms = 100;
        cfg.pace_factor = 1;
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        std::atomic<int> put{0};
        std::thread dropper([&]() {                                                                                  // (the game server's side: a recording every 200 ms)
            for (int i = 0; i < kStream; ++i) {
                put_recording(rec, match_file(1 + i % 3), kT0 + i * 11);
                ++put;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
        int64_t last_total = 0;
        size_t requests = 0;
        bool whole = true;
        const auto began = std::chrono::steady_clock::now();
        while (elapsed_ms(began) < 240000 * kSlow) {                                                                 // (this thread is the HTTP side: it asks all the time)
            const ctl::HttpResponse plain = ask(service, "GET", "/history");
            const ctl::JsonValue list = json_of(plain);
            const int64_t total = list.get("total").as_int_or(-1);
            std::set<std::string> ids;
            for (size_t i = 0; i < list.get("history").size(); ++i) ids.insert(list.get("history").at(i).get("id").str());
            whole = whole && plain.status == 200 && list.get("count").as_int_or(-2) == total && ids.size() == list.get("history").size() && ids.size() == static_cast<size_t>(total) && total >= last_total;
            last_total = std::max(last_total, total);
            const ctl::HttpResponse filtered = ask(service, "GET", "/history", "sort=old&limit=3");
            whole = whole && filtered.status == 200 && json_of(filtered).get("total").as_int_or(-1) >= last_total - 1;
            whole = whole && ask(service, "GET", "/replays").status == 200;
            ++requests;
            if (put == kStream && service.stats().matches == static_cast<size_t>(kStream)) break;
            std::this_thread::yield();
        }
        dropper.join();
        ASSERT_TRUE(whole);
        ASSERT_EQ(service.stats().matches, static_cast<size_t>(kStream));
        ASSERT_EQ(service.stats().counted, static_cast<size_t>(kStream));
        ASSERT_EQ(json_of(ask(service, "GET", "/history")).get("total").as_int_or(-1), int64_t{kStream});
        std::cout << "\n    " << requests << " rounds of three requests while " << kStream << " recordings were counted";
        ASSERT_TRUE(requests > 10);
    } TEST_END();

    // ---- no folder, no file, a file that stops answering --------------------------------------------------------------------------------

    TEST_CASE("HV7.1 A service that cannot start gives a reason that names the file or folder and has made nothing: no recordings folder, no maps folder, no database file named, a file that is no database, a database of a newer program, a game database without its folder; before it starts it answers 503") {
        TempDir work;
        fs::create_directories(work.path / "rec");
        auto log = std::make_shared<Collector>();
        const auto start_with = [&](const std::function<void(ServiceConfig&)>& change, std::string& why) {
            ServiceConfig cfg = base_config(work.path, work.path / "rec", log);
            change(cfg);
            HistoryService service(cfg);
            return service.start(why);
        };
        std::string why;
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.recordings_dir = (work.path / "missing").string(); }, why));
        ASSERT_TRUE(contains(why, (work.path / "missing").string()));                                               // (the reason names the folder)
        ASSERT_FALSE(fs::exists(work.path / "history.db"));                                                         // (the file was not made for a service that cannot start)
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.recordings_dir.clear(); }, why));
        ASSERT_FALSE(why.empty());
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.maps_dir = (work.path / "no maps").string(); }, why));
        ASSERT_TRUE(contains(why, (work.path / "no maps").string()));
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.community_dir = (work.path / "no community").string(); }, why));
        ASSERT_TRUE(contains(why, (work.path / "no community").string()));
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.db_path.clear(); }, why));
        ASSERT_FALSE(why.empty());
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.game_db_path = (work.path / "game.db").string(); c.submissions_dir.clear(); }, why));
        ASSERT_FALSE(why.empty());
        {
            std::ofstream junk(work.path / "junk.db", std::ios::binary);
            junk << std::string(4096, 'x') << "\n";
        }
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.db_path = (work.path / "junk.db").string(); }, why));
        ASSERT_TRUE(contains(why, "junk.db"));
        ASSERT_EQ(fs::file_size(work.path / "junk.db"), uintmax_t{4097});                                           // (the file is as it was)
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.db_path = (work.path / "junk.db" / "inside.db").string(); }, why));      // (below a file)
        ASSERT_FALSE(why.empty());
        {
            db::Database newer;
            ASSERT_TRUE(db::open_and_migrate(newer, (work.path / "newer.db").string(), history_db_migrations(), db::OpenOptions()).ok());
            ASSERT_TRUE(newer.exec("INSERT INTO schema_version (version, name, applied_at) VALUES (99, 'future', 'then')").ok());
        }
        ASSERT_FALSE(start_with([&](ServiceConfig& c) { c.db_path = (work.path / "newer.db").string(); }, why));
        ASSERT_TRUE(contains(why, "newer.db"));
        {
            db::Database check;                                                                                     // (the file of the newer program is as it was: its row is the last)
            ASSERT_TRUE(check.open((work.path / "newer.db").string()).ok());
            ASSERT_EQ(table_rows(check, "schema_version"), history_db_migrations().size() + 1);
        }
        HistoryService never(base_config(work.path, work.path / "rec", log));                                       // (not started)
        const ctl::HttpResponse r = ask(never, "GET", "/history");
        ASSERT_TRUE(r.status == 503 && !json_of(r).get("error").str().empty());
    } TEST_END();

    TEST_CASE("HV7.2 A database that stops answering gives 503 with an error and nothing of SQLite's, the recordings are still given, the counter does not stop the service, and a stop is clean") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        auto log = std::make_shared<Collector>();
        HistoryService service(base_config(work.path, rec, log));
        std::string why;
        ASSERT_TRUE(service.start(why));
        put_recording(rec, match_file(1), kT0);
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 1; }, 60000 * kSlow));
        ASSERT_EQ(ask(service, "GET", "/history").status, 200);
        {
            db::Database wrecker;                                                                                     // (another program breaks the file's schema under the service's feet)
            ASSERT_TRUE(wrecker.open((work.path / "history.db").string()).ok());
            ASSERT_TRUE(wrecker.exec("ALTER TABLE history_matches RENAME TO history_matches_gone").ok());
        }
        const ctl::HttpResponse down = ask(service, "GET", "/history");
        ASSERT_TRUE(down.status == 503 && !json_of(down).get("error").str().empty());
        ASSERT_EQ(ask(service, "GET", "/history/" + id_at(kT0)).status, 503);
        ASSERT_EQ(ask(service, "GET", "/history/nonsense").status, 404);                                              // (an id that cannot exist is a 404 whether the database answers or not)
        ASSERT_EQ(ask(service, "GET", "/replays").status, 200);                                                      // (the recordings do not need the database)
        ASSERT_EQ(ask(service, "GET", "/replays/" + history_recording_file(id_at(kT0))).status, 200);
        put_recording(rec, match_file(2), kT0 + 50);                                                                 // (the counter meets the broken file too)
        ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= 20; }, 20000));
        ASSERT_FALSE(contains(down.body, "SQL") || contains(down.body, "history_matches") || contains(down.body, "no such table"));
        ASSERT_EQ(ask(service, "GET", "/replays").status, 200);
        service.stop();
        service.stop();                                                                                              // (twice is fine)
    } TEST_END();

    TEST_CASE("HV7.3 A database that answers again is used again within seconds, on the counter's side (a recording that waited is counted) and the HTTP side (the lists are back): the service looks again after its own short time, not the game server's minute") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        ASSERT_TRUE(ServiceConfig().store_retry_ms > 0 && ServiceConfig().store_retry_ms <= 5000);                  // (the program's own setting: about five seconds)
        TempDir work;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        auto log = std::make_shared<Collector>();
        ServiceConfig cfg = base_config(work.path, rec, log);
        cfg.store_retry_ms = 200;                                                                                    // (short, so that the test is quick; what is tested is that the number reaches both stores)
        HistoryService service(cfg);
        std::string why;
        ASSERT_TRUE(service.start(why));
        put_recording(rec, match_file(1), kT0);
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 1; }, 60000 * kSlow));
        const auto rename_table = [&](const char* from, const char* to) {
            db::Database other;
            return other.open((work.path / "history.db").string()).ok() && other.exec(std::string("ALTER TABLE ") + from + " RENAME TO " + to).ok();
        };
        ASSERT_TRUE(rename_table("history_matches", "history_matches_gone"));                                        // (the file breaks: both sides find out)
        for (int i = 0; i < 4; ++i) ASSERT_EQ(ask(service, "GET", "/history").status, 503);                          // (the HTTP side looks again and again, and finds it broken each time)
        put_recording(rec, match_file(2), kT0 + 50);
        const uint64_t passes = service.stats().passes;
        ASSERT_TRUE(wait_for([&]() { return service.stats().passes >= passes + 20; }, 20000));                      // (the counter meets the broken file with a recording to count)
        ASSERT_EQ(service.stats().matches, size_t{1});
        ASSERT_TRUE(rename_table("history_matches_gone", "history_matches"));                                        // (the file is whole again)
        int asks = 0;
        ctl::HttpResponse r;
        do {
            r = ask(service, "GET", "/history");                                                                     // (every ask is 300 ms later on the HTTP side's clock)
        } while (r.status != 200 && ++asks < 10);
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(wait_for([&]() { return service.stats().matches == 2; }, 20000 * kSlow));                       // (the counter looked again and counted the recording that waited)
        ASSERT_EQ(json_of(ask(service, "GET", "/history")).get("total").as_int_or(-1), int64_t{2});
        service.stop();
    } TEST_END();

    // ---- the real program --------------------------------------------------------------------------------------------------------

#if !defined(_WIN32) && defined(ANTS_HAS_HISTORY_BINARY)
    TEST_CASE("HV8.1 The real program: --version works, a wrong command line stops it with 2, a missing folder stops it with 1, and a started one counts a recording, serves it over HTTP and stops cleanly on SIGTERM") {
        if (match_file(1).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir work;
        const fs::path rec = work.path / "rec";
        fs::create_directories(rec);
        const auto run = [&](const std::vector<std::string>& args, const std::string& log_name) {
            Program p;
            if (!p.start(args, work.path / log_name)) return std::make_pair(-99, std::string());
            const int code = p.wait_exit(15000);
            return std::make_pair(code, p.log_text());
        };
        const auto version = run({"--version"}, "version.log");
        ASSERT_TRUE(version.first == 0 && version.second.compare(0, 12, "ants_history") == 0);
        const auto nothing = run({}, "none.log");
        ASSERT_EQ(nothing.first, 2);
        const auto wrong = run({"--port", "99999"}, "wrong.log");
        ASSERT_EQ(wrong.first, 2);
        const auto unknown = run({"--nonsense"}, "unknown.log");
        ASSERT_EQ(unknown.first, 2);
        const std::vector<std::string> base = {"--db", (work.path / "data" / "history.db").string(), "--recordings", rec.string(), "--maps", maps_dir(), "--port", "0"};
        std::vector<std::string> missing = base;
        missing[3] = (work.path / "absent").string();
        const auto absent = run(missing, "absent.log");
        ASSERT_TRUE(absent.first == 1 && contains(absent.second, "absent"));                                       // (stops with 1, and its words name the folder)
        ASSERT_FALSE(fs::exists(work.path / "data" / "history.db"));
        Program program;
        ASSERT_TRUE(program.start(base, work.path / "run.log"));
        ASSERT_TRUE(wait_for([&]() { return port_in_log(program) != 0; }, 20000 * kSlow));
        const uint16_t port = port_in_log(program);
        const Answer empty = http_get(port, "/history");
        ASSERT_TRUE(empty.status == 200 && parse_text(empty.body).get("total").as_int_or(-1) == 0);
        ASSERT_EQ(http_get(port, "/healthz").status, 200);
        ASSERT_EQ(http_get(port, "/nothing").status, 404);
        const auto began = std::chrono::steady_clock::now();
        const std::string id = put_recording(rec, match_file(1), kT0);
        Answer counted;
        ASSERT_TRUE(wait_for([&]() {
            counted = http_get(port, "/history");
            return counted.status == 200 && parse_text(counted.body).get("total").as_int_or(0) == 1;
        }, 60000 * kSlow));
        std::cout << "\n    the program counted a recording " << elapsed_ms(began) << " ms after it appeared (its own settings)";
        if (!kThreadSanitizer) ASSERT_LT(elapsed_ms(began), int64_t{5000});
        ctl::JsonValue item;
        ASSERT_TRUE(wait_for([&]() {                                                                               // (the HTTP side looks at the folder by itself, at most every http_look_ms: the match that the counter has
            item = parse_text(http_get(port, "/history/" + id).body);                                              //  just counted shows its recording from that look on, and a request in between still says "removed")
            return item.get("id").str() == id && item.get("recording").str() == "watch";
        }, 20000 * kSlow));
        const Answer file = http_get(port, "/replays/" + history_recording_file(id));
        ASSERT_TRUE(file.status == 200 && file.body.size() == match_file(1).size());
        program.signal_it(SIGTERM);
        ASSERT_EQ(program.wait_exit(15000), 0);
        ASSERT_FALSE(contains(program.log_text(), "Sanitizer") || contains(program.log_text(), "runtime error"));          // (a build with a sanitizer: its report would be in the program's own log)
        db::Database after;                                                                                          // (the file is whole: it opens, with the record in it)
        ASSERT_TRUE(after.open((work.path / "data" / "history.db").string()).ok());
        ASSERT_EQ(table_rows(after, "history_matches"), size_t{1});
    } TEST_END();
#endif

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

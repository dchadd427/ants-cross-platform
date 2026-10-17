// Tests of the dedicated game server: the real program as a child process (not on Windows), and the restore that never blocks the server
#include "server_fixtures.hpp"

#if !defined(_WIN32) && defined(ANTS_SERVER_BINARY)
// ---------------------------------------------------------------------------------------------------------------------------------
// The real program: ants_server as a child process of the test, stopped with SIGTERM or killed with SIGKILL in the middle of a match and started again over the same folder, and two
// machines of the test (RClient: the real lobby and the real session) that find it again by themselves over real TCP sockets
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

uint32_t wall_ms() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()) + 1000u;
}

uint16_t free_tcp_port() {
    auto listener = net::TcpListener::listen(0, true);
    return listener != nullptr ? listener->port() : uint16_t{0};
}

// The real ants_server program. Only the process that this object started is ever signalled (never by name).
struct ServerProcess {
    pid_t pid{-1};
    int started{0};
    bool start(const std::vector<std::string>& args, const std::string& secret, const std::string& log_file) {
        std::vector<std::string> storage;
        storage.push_back(ANTS_SERVER_BINARY);
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
            ::setenv("ANTS_SERVER_SECRET", secret.c_str(), 1);
            ::execv(argv[0], argv.data());
            ::_exit(127);
        }
        ++started;
        return true;
    }
    // Waits for the process to end (and reaps it): how long it took in ms, or -1 when it did not end within `timeout_ms` (then it is still there)
    int64_t wait_exit(uint32_t timeout_ms, int& status) {
        const auto began = std::chrono::steady_clock::now();
        for (;;) {
            const pid_t r = ::waitpid(pid, &status, WNOHANG);
            const int64_t took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
            if (r == pid) {
                pid = -1;
                return took;
            }
            if (took > static_cast<int64_t>(timeout_ms)) return -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void signal_it(int sig) {
        if (pid > 0) ::kill(pid, sig);
    }
    ~ServerProcess() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
    }
};

// A blocking request to the control interface (the answer carries Connection: close): the JSON body, null when there is none
ctl::JsonValue ctl_call(uint16_t port, const std::string& method, const std::string& path, const std::string& secret, const std::string& body = std::string(), int* status = nullptr) {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return ctl::JsonValue::make_null();
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return ctl::JsonValue::make_null();
    }
    std::string request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + secret + "\r\nConnection: close\r\n";
    if (!body.empty()) request += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    request += "\r\n" + body;
    ::send(s, request.data(), request.size(), 0);
    std::string response;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
    }
    ::close(s);
    if (status != nullptr) *status = response.compare(0, 9, "HTTP/1.1 ") == 0 ? std::atoi(response.c_str() + 9) : 0;
    const size_t split = response.find("\r\n\r\n");
    ctl::JsonValue j;
    std::string why;
    if (split == std::string::npos || !ctl::parse_json(response.substr(split + 4), j, &why)) return ctl::JsonValue::make_null();
    return j;
}

// Machines of the test over real TCP links (the clock is the wall clock)
struct RealWorld : LinkSource {
    uint16_t port{0};
    std::string maps;
    std::vector<std::unique_ptr<net::TcpConnection>> links;
    std::vector<std::unique_ptr<RClient>> clients;

    net::Connection* open_link() override {
        links.push_back(net::TcpConnection::connect("127.0.0.1", port));
        return links.back().get();
    }
    RClient& connect(const std::string& name, const std::string& room, const std::optional<net::CreateBlock>& create = std::nullopt, uint8_t kind = net::kClientGame, const net::SeatKey& key = net::SeatKey{}) {
        net::Connection* end = open_link();
        clients.push_back(std::make_unique<RClient>());
        RClient& c = *clients.back();
        c.name = name;
        c.room = room;
        c.create = create;                                                              // (the block of a Hello that makes a public room, protocol 15)
        c.kind = kind;                                                                  // (protocol 16: a lobby page, or a game)
        c.key = key;
        c.record_hashes = true;
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    void step() {
        for (size_t i = 0; i < clients.size(); ++i) clients[i]->update(wall_ms(), maps, *this);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool until(const std::function<bool()>& cond, uint32_t timeout_ms) {
        const uint32_t end = wall_ms() + timeout_ms;
        while (static_cast<int32_t>(wall_ms() - end) < 0) {
            if (cond()) return true;
            step();
        }
        return cond();
    }
    void run_for(uint32_t ms) {
        const uint32_t end = wall_ms() + ms;
        while (static_cast<int32_t>(wall_ms() - end) < 0) step();
    }
};

// The port accepts connections (the kernel completes them before the program reads them: a restore that is still running does not show here)
bool port_accepts(uint16_t port, uint32_t timeout_ms) {
    const uint32_t end = wall_ms() + timeout_ms;
    while (static_cast<int32_t>(wall_ms() - end) < 0) {
        auto c = net::TcpConnection::connect("127.0.0.1", port);
        for (int i = 0; i < 100 && c != nullptr && c->state() == net::Connection::State::Connecting; ++i) {
            std::vector<uint8_t> nothing;
            c->poll(nothing);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (c != nullptr && c->is_open()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::string text_of_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string hex_u64(uint64_t v) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[static_cast<size_t>(15 - i)] = kHex[(v >> (4 * i)) & 0xFu];
    return out;
}

// One scenario: a match of two machines on a real server, the server stopped with `stop_signal` in the middle of it, started again, the machines find it by themselves, the match goes on to its end
void real_process_scenario(const char* tag, int stop_signal, bool control_room) {
    const fs::path root = temp_dir_for(tag);
    const fs::path results = root / "results";
    const uint16_t game_port = free_tcp_port();
    const uint16_t ctl_port = free_tcp_port();
    ASSERT_TRUE(game_port != 0 && ctl_port != 0 && game_port != ctl_port);
    const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
    const std::string log_file = (root / "server.log").string();
    const std::vector<std::string> args = {"--maps", maps_dir(), "--port", std::to_string(game_port), "--ctl-port", std::to_string(ctl_port), "--results-dir", results.string(), "--reconnect",
                                           "--demo-rooms", "4", "--demo-map", "TINY.LVL", "--resume-countdown-seconds", "0", "--max-pause-seconds", "300"};
    const fs::path restart_dir = results / "restart";
    ServerProcess server;
    ASSERT_TRUE(server.start(args, secret, log_file));
    ASSERT_TRUE(port_accepts(game_port, 15000));
    ASSERT_TRUE(port_accepts(ctl_port, 5000));
    const std::string code = control_room ? "RP-1" : "pubroom1";
    if (control_room) {
        int http = 0;
        const ctl::JsonValue made = ctl_call(ctl_port, "POST", "/rooms", secret, "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"RP-1\", \"reconnect\": true, \"resume_countdown_seconds\": 0, \"max_pause_seconds\": 300}", &http);
        ASSERT_TRUE(http == 201 && made.get("state").str() == "waiting");
    }
    RealWorld w;
    w.port = game_port;
    w.maps = maps_dir();
    RClient& a = w.connect("Ann", code, control_room ? std::nullopt : std::optional<net::CreateBlock>(block_of("", 2)));        // (a public room: Ann's Hello carries the block that makes it)
    ASSERT_TRUE(w.until([&]() { return a.lobby->phase() == net::ClientLobby::Phase::InRoom; }, 15000));       // (Ann is in before Bob connects: two Hellos over two sockets can be read in either order, and Bob's finds no room if it comes first)
    RClient& b = w.connect("Bob", code);
    ASSERT_TRUE(w.until([&]() { return a.session != nullptr && b.session != nullptr && a.session->runner().next_turn_expected() >= 130 && b.session->runner().next_turn_expected() >= 130; }, 40000));
    // the record is there, for its owner only, and the control interface says so
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(restart_dir)) {
        if (e.path().extension() == kRestartExtension) files.push_back(e.path());              // (the folder also holds the lock file of the server that runs)
    }
    ASSERT_EQ(files.size(), size_t{1});
    {
        struct stat st;
        ASSERT_EQ(::stat(files[0].c_str(), &st), 0);
        ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);
        ASSERT_EQ(::stat(restart_dir.c_str(), &st), 0);
        ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);
    }
    ctl::JsonValue j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("record").get("kept").as_bool_or(false) && j.get("restored").is_null());
    const net::SeatKey key_a = a.lobby->key();
    const net::SeatKey key_b = b.lobby->key();
    // ---- the server is stopped (SIGTERM: it makes the records durable and exits at once; SIGKILL: it is gone) -------------------------------------------------------------------
    w.run_for(1234);                                                                    // (a moment that is no turn boundary)
    const uint32_t seen = std::max(a.session->runner().next_turn_expected(), b.session->runner().next_turn_expected());       // the most turns that any machine has been sent
    const auto began_stop = std::chrono::steady_clock::now();
    server.signal_it(stop_signal);
    int status = 0;
    const int64_t took = server.wait_exit(10000, status);
    const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began_stop).count();
    ASSERT_TRUE(took >= 0);
    if (stop_signal == SIGTERM) {
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        ASSERT_TRUE(stop_ms < 3000);                                                    // within the grace (the stack files give docker 15 s): the flush is a moment's work
    } else {
        ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    }
    files.clear();
    for (const auto& e : fs::directory_iterator(restart_dir)) {
        if (e.path().extension() == kRestartExtension) files.push_back(e.path());              // (the folder also holds the lock file of the server that runs)
    }
    ASSERT_EQ(files.size(), size_t{1});                                                 // the record is still there
    const RestartLoaded rec = read_restart_record(files[0].string(), 1ull << 30);
    ASSERT_TRUE(rec.ok());
    ASSERT_TRUE(rec.turns.size() >= seen);                                              // it holds every turn that any machine has been sent: a restart loses nothing that a player has run
    ASSERT_TRUE(rec.head.code == code && rec.head.keys[a.lobby->my_seat()] == key_a && rec.head.keys[b.lobby->my_seat()] == key_b);
    const size_t record_turns = rec.turns.size();
    const std::string log_before = text_of_file(log_file);
    if (stop_signal == SIGTERM) ASSERT_TRUE(log_before.find("stopped: 1 restart record(s) made durable and kept") != std::string::npos);
    // ---- the machines notice and look for the server; it starts again over the same folder ----------------------------------------------------------------------------------------
    w.run_for(2500);
    ASSERT_TRUE(a.session->reconnecting() && b.session->reconnecting() && !a.lost && !b.lost);
    ASSERT_TRUE(server.start(args, secret, log_file));
    ASSERT_TRUE(port_accepts(game_port, 20000));
    // the machines find the room by its code and their keys: no help from the test
    ASSERT_TRUE(w.until([&]() { return a.session->mode() == net::ClientSession::Mode::Normal && b.session->mode() == net::ClientSession::Mode::Normal && !a.session->paused(); }, 60000));
    ASSERT_FALSE(a.lost || b.lost || a.was_rejected || b.was_rejected);                 // (none was told that it is ahead of the record)
    j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("restored").is_object() && j.get("restored").get("turns").as_int_or(0) == static_cast<int64_t>(record_turns));
    ASSERT_TRUE(j.get("rejoins").as_int_or(0) == 2 && !j.get("paused").as_bool_or(true) && j.get("record").get("kept").as_bool_or(false));
    w.run_for(4000);
    j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
    ASSERT_TRUE(j.get("turns").as_int_or(0) > static_cast<int64_t>(record_turns) + 60);  // the match goes on
    // ---- Bob quits: the match ends, on the machines and on the referee, in one state -------------------------------------------------------------------------------------------------
    sim::Command quit;
    quit.type = sim::CommandType::Quit;
    quit.issuer = b.lobby->my_seat();
    ASSERT_TRUE(b.session->submit(quit));
    bool finished = false;
    for (int i = 0; i < 100 && !finished; ++i) {
        w.run_for(200);
        j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
        finished = j.get("state").str() == "finished";
    }
    ASSERT_TRUE(finished);
    w.run_for(1500);
    ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && a.sim.state_hash() == b.sim.state_hash());
    ASSERT_EQ(j.get("state_hash").str(), hex_u64(a.sim.state_hash().total));              // the referee that was restored and the two machines that lived through the restart
    ASSERT_FALSE(a.session->desynced() || b.session->desynced());
    ASSERT_EQ(entries_but_lock(restart_dir), size_t{0});                                // the match is over: its record is gone
    // ---- the log: what happened, and no key anywhere in it ---------------------------------------------------------------------------------------------------------------------------
    const std::string log_text = text_of_file(log_file);
    ASSERT_TRUE(log_text.find("room " + code + " restored: " + std::to_string(record_turns) + " turns") != std::string::npos);
    ASSERT_TRUE(log_text.find(hex_of(key_a)) == std::string::npos && log_text.find(hex_of(key_b)) == std::string::npos);
    ASSERT_TRUE(log_text.find("restart records in ") != std::string::npos);
    // the server that has nothing to keep stops at once too
    server.signal_it(SIGTERM);
    ASSERT_TRUE(server.wait_exit(5000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::cout << "\n      [restart] " << (stop_signal == SIGTERM ? "SIGTERM" : "SIGKILL") << " in the middle of a match (" << record_turns << " turns in the record, the machines had been sent " << seen
              << "): the server " << (stop_signal == SIGTERM ? "exited " + std::to_string(stop_ms) + " ms after the signal" : std::string("was gone")) << ", started again, both machines found the room by themselves, the match ended in one state" << std::flush;
}


// The output (stdout and stderr) of a shell command and its exit status
std::string shell_output(const std::string& command, int& exit_status) {
    std::string out;
    FILE* pipe = ::popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        exit_status = -1;
        return out;
    }
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
    const int st = ::pclose(pipe);
    exit_status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return out;
}

// The scenario of S3.101: a server that runs in a docker container that the operator made (a volume at /results, the game and control ports published on this machine, --reconnect), stopped with
// `docker stop` and started again. The test never makes or removes a container: it only starts, stops and asks the one whose name it is given.
void container_scenario(const std::string& name, uint16_t game_port, uint16_t ctl_port, const std::string& secret) {
    int rc = 0;
    const auto docker = [&rc](const std::string& args) { return shell_output("docker " + args, rc); };
    ASSERT_TRUE(port_accepts(game_port, 20000));
    ASSERT_TRUE(port_accepts(ctl_port, 5000));
    int http = 0;
    const ctl::JsonValue made = ctl_call(ctl_port, "POST", "/rooms", secret, "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"CT-1\", \"reconnect\": true, \"resume_countdown_seconds\": 0, \"max_pause_seconds\": 300}", &http);
    ASSERT_TRUE(http == 201 && made.get("state").str() == "waiting");
    RealWorld w;
    w.port = game_port;
    w.maps = maps_dir();
    RClient& a = w.connect("Ann", "CT-1");
    RClient& b = w.connect("Bob", "CT-1");
    ASSERT_TRUE(w.until([&]() { return a.session != nullptr && b.session != nullptr && a.session->runner().next_turn_expected() >= 130 && b.session->runner().next_turn_expected() >= 130; }, 60000));
    // the record is on the volume, for its owner only (the folder 700, the file 600), and the control interface says so
    const std::string modes = docker("exec " + name + " sh -c 'stat -c %a /results/restart /results/restart/*.restart'");
    ASSERT_TRUE(rc == 0 && modes == "700\n600\n");
    ctl::JsonValue j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("record").get("kept").as_bool_or(false) && j.get("restored").is_null());
    const net::SeatKey key_a = a.lobby->key();
    const net::SeatKey key_b = b.lobby->key();
    // ---- docker stop: SIGTERM, then SIGKILL after the grace period that the stack gives (15 s) -------------------------------------------------------------------------------------------
    w.run_for(1234);
    const uint32_t seen = std::max(a.session->runner().next_turn_expected(), b.session->runner().next_turn_expected());
    const auto began_stop = std::chrono::steady_clock::now();
    docker("stop -t 15 " + name);
    const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began_stop).count();
    ASSERT_EQ(rc, 0);
    const std::string exit_code = docker("inspect -f '{{.State.ExitCode}}' " + name);
    ASSERT_TRUE(rc == 0 && exit_code == "0\n");                                         // it stopped by itself (137 would be docker's SIGKILL)
    ASSERT_TRUE(stop_ms < 6000);
    w.run_for(2500);
    ASSERT_TRUE(a.session->reconnecting() && b.session->reconnecting() && !a.lost && !b.lost);
    docker("start " + name);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(port_accepts(game_port, 30000));
    // the machines find the room by its code and their keys, and the match goes on
    ASSERT_TRUE(w.until([&]() { return a.session->mode() == net::ClientSession::Mode::Normal && b.session->mode() == net::ClientSession::Mode::Normal && !a.session->paused(); }, 60000));
    ASSERT_FALSE(a.lost || b.lost || a.was_rejected || b.was_rejected);
    j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("restored").is_object() && j.get("restored").get("turns").as_int_or(0) >= static_cast<int64_t>(seen));
    ASSERT_TRUE(j.get("rejoins").as_int_or(0) == 2 && !j.get("paused").as_bool_or(true));
    const int64_t restored_turns = j.get("restored").get("turns").as_int_or(0);
    w.run_for(4000);
    j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
    ASSERT_TRUE(j.get("turns").as_int_or(0) > restored_turns + 60);
    // ---- Bob quits: the match ends in one state on the machines and on the referee ----------------------------------------------------------------------------------------------------
    sim::Command quit;
    quit.type = sim::CommandType::Quit;
    quit.issuer = b.lobby->my_seat();
    ASSERT_TRUE(b.session->submit(quit));
    bool finished = false;
    for (int i = 0; i < 100 && !finished; ++i) {
        w.run_for(200);
        j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
        finished = j.get("state").str() == "finished";
    }
    ASSERT_TRUE(finished);
    w.run_for(1500);
    ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && a.sim.state_hash() == b.sim.state_hash());
    ASSERT_EQ(j.get("state_hash").str(), hex_u64(a.sim.state_hash().total));
    ASSERT_FALSE(a.session->desynced() || b.session->desynced());
    const std::string left = docker("exec " + name + " sh -c 'ls /results/restart'");    // the match is over: its record is gone
    ASSERT_TRUE(rc == 0 && left.empty());
    // the container's log: what happened, and no key anywhere in it
    const std::string log_text = docker("logs " + name);
    ASSERT_TRUE(log_text.find("stopped: 1 restart record(s) made durable and kept") != std::string::npos);
    ASSERT_TRUE(log_text.find("room CT-1 restored: ") != std::string::npos);
    ASSERT_TRUE(log_text.find(hex_of(key_a)) == std::string::npos && log_text.find(hex_of(key_b)) == std::string::npos);
    std::cout << "\n      [container] docker stop took " << stop_ms << " ms (exit code 0), the room came back with " << restored_turns << " turns (the machines had been sent " << seen
              << "), both machines found it by themselves, the match ended in one state" << std::flush;
}

// The rematch on the real program over real TCP (docs/SERVER.md "Lobby rooms"): two pages make a lobby, START, their games take the seats over with the keys, Bob quits and the rules end the match; the
// lobby is there again under the same code: Bob's results screen holds his seat, a stranger is turned away, Pia's page comes back with her key and leads, START waits for Bob (Pia is told), Pia removes him
// and START is taken. The control interface lists the match beside the lobby.
void rematch_process_scenario() {
    const fs::path root = temp_dir_for("rematch-199");
    const uint16_t game_port = free_tcp_port();
    const uint16_t ctl_port = free_tcp_port();
    ASSERT_TRUE(game_port != 0 && ctl_port != 0 && game_port != ctl_port);
    const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
    const std::string log_file = (root / "server.log").string();
    const std::vector<std::string> args = {"--maps", maps_dir(), "--port", std::to_string(game_port), "--ctl-port", std::to_string(ctl_port), "--results-dir", (root / "results").string(), "--reconnect",
                                           "--demo-rooms", "4", "--demo-lobbies", "2", "--demo-map", "TINY.LVL", "--start-countdown-seconds", "0"};
    ServerProcess server;
    ASSERT_TRUE(server.start(args, secret, log_file));
    ASSERT_TRUE(port_accepts(game_port, 15000));
    ASSERT_TRUE(port_accepts(ctl_port, 5000));
    const std::string code = "rematch1";
    RealWorld w;
    w.port = game_port;
    w.maps = maps_dir();
    RClient& pia_page = w.connect("Pia", code, lobby_block_of(), net::kClientPage);
    ASSERT_TRUE(w.until([&]() { return pia_page.lobby->phase() == net::ClientLobby::Phase::InRoom; }, 15000));
    RClient& bob_page = w.connect("Bob", code, lobby_block_of(), net::kClientPage);
    ASSERT_TRUE(w.until([&]() { return bob_page.lobby->phase() == net::ClientLobby::Phase::InRoom && pia_page.lobby->room().slots[1].state == net::SlotState::Client; }, 15000));
    ASSERT_TRUE(pia_page.lobby->is_leader() && bob_page.lobby->my_seat() == 1);
    ASSERT_TRUE(pia_page.lobby->request_start());
    ASSERT_TRUE(w.until([&]() { return pia_page.lobby->room().starting(); }, 10000));
    const net::SeatKey pia_key = pia_page.lobby->key();
    const net::SeatKey bob_key = bob_page.lobby->key();
    pia_page.end->close();                                                              // (the pages go to the game page)
    bob_page.end->close();
    RClient& pia_game = w.connect("Pia", code, std::nullopt, net::kClientGame, pia_key);
    RClient& bob_game = w.connect("Bob", code, std::nullopt, net::kClientGame, bob_key);
    ASSERT_TRUE(w.until([&]() { return pia_game.session != nullptr && bob_game.session != nullptr && bob_game.session->runner().next_turn_expected() >= 120; }, 40000));
    sim::Command quit;                                                                  // Bob quits: with one other side left the rules end the match
    quit.type = sim::CommandType::Quit;
    quit.issuer = bob_game.lobby->my_seat();
    ASSERT_TRUE(bob_game.session->submit(quit));
    ASSERT_TRUE(w.until([&]() { return pia_game.sim.is_match_over() && bob_game.sim.is_match_over(); }, 20000));
    // the code is a lobby's again at once, with nobody back: no leader, the match's colors; the match is listed beside it
    ctl::JsonValue lobby;
    ASSERT_TRUE(w.until([&]() { lobby = ctl_call(ctl_port, "GET", "/rooms/" + code, secret); return lobby.get("state").str() == "waiting"; }, 10000));
    ASSERT_TRUE(lobby.get("lobby").as_bool_or(false) && lobby.get("leader").is_null() && !lobby.get("starting").as_bool_or(true) && lobby.get("plan").str() == "oooo -");
    ASSERT_EQ(lobby.get("players").items().size(), size_t{2});
    const ctl::JsonValue list = ctl_call(ctl_port, "GET", "/rooms", secret);
    size_t finished = 0;
    for (const ctl::JsonValue& r : list.get("rooms").items()) finished += r.get("code").str() == code && r.get("state").str() == "finished" ? 1u : 0u;
    ASSERT_EQ(finished, size_t{1});
    // Bob's results screen holds his seat; a stranger is turned away
    RClient& bob_results = w.connect("Bob", code, std::nullopt, net::kClientGame, bob_key);
    ASSERT_TRUE(w.until([&]() { return bob_results.lobby->phase() == net::ClientLobby::Phase::InRoom; }, 10000));
    ASSERT_TRUE(bob_results.lobby->my_seat() == 1 && bob_results.lobby->room().seat_in_game(0) && bob_results.lobby->room().seat_in_game(1) && bob_results.lobby->room().leader == net::kNoLeader);
    RClient& stranger = w.connect("Sid", code, lobby_block_of(), net::kClientPage);
    ASSERT_TRUE(w.until([&]() { return stranger.was_rejected; }, 10000));
    ASSERT_EQ(stranger.rejected, net::RejectReason::MatchRunning);
    // Pia's page comes back with her key and leads; her START waits for Bob, and she is told
    RClient& pia_back = w.connect("Pia", code, lobby_block_of(), net::kClientPage, pia_key);
    ASSERT_TRUE(w.until([&]() { return pia_back.lobby->phase() == net::ClientLobby::Phase::InRoom && pia_back.lobby->is_leader(); }, 10000));
    ASSERT_TRUE(pia_back.lobby->my_seat() == 0 && !pia_back.lobby->room().seat_in_game(0) && pia_back.lobby->room().seat_in_game(1));
    net::PlanMsg plan;                                                                  // (a Medium bot in Blue: Pia and the bot can play without Bob)
    plan.plan = {net::PlanKind::Open, net::PlanKind::Open, net::PlanKind::Medium, net::PlanKind::Nobody};
    ASSERT_TRUE(pia_back.lobby->request_plan(plan));
    ASSERT_TRUE(w.until([&]() { return pia_back.lobby->room().plan[2] == net::PlanKind::Medium; }, 10000));
    ASSERT_TRUE(pia_back.lobby->request_start());
    ASSERT_TRUE(w.until([&]() { return !pia_back.room_chat.empty(); }, 10000));
    ASSERT_TRUE(pia_back.room_chat.back().notice() && pia_back.room_chat.back().text == std::string("Bob") + net::kNoticeNotBack);
    ASSERT_FALSE(ctl_call(ctl_port, "GET", "/rooms/" + code, secret).get("starting").as_bool_or(true));
    // Pia removes Bob: his results screen is told, and her START is taken
    ASSERT_TRUE(pia_back.lobby->request_remove(1));
    ASSERT_TRUE(w.until([&]() { return bob_results.was_rejected; }, 10000));
    ASSERT_EQ(bob_results.rejected, net::RejectReason::Kicked);
    ASSERT_TRUE(pia_back.lobby->request_start());
    ASSERT_TRUE(w.until([&]() { return pia_back.lobby->room().starting(); }, 10000));
    server.signal_it(SIGTERM);
    int status = 0;
    ASSERT_TRUE(server.wait_exit(10000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    const std::string log = text_of_file(log_file);
    ASSERT_TRUE(log.find(hex_of(pia_key)) == std::string::npos && log.find(hex_of(bob_key)) == std::string::npos);      // (no key in the log)
}

}  // namespace

void run_persist_process_tests() {
    TEST_CASE("S3.95 The Real Program, Told To Stop (SIGTERM) In The Middle Of A Match Of A Demo Room: It Exits At Once With Status 0 (Well Within The 15 s That The Stack Gives Docker), The Record Holds Every Turn That A Machine Has Been Sent, Is For Its Owner Only And Is Still There; The Program Started Again Over The Same Folder Logs The Restored Room, Both Machines (The Real Lobby And Session Over Real TCP) Find The Room By Its Code And Their Keys By Themselves, Nobody Is Told That It Is Ahead, The Match Goes On And Ends: The Referee's State Hash (From The Control Interface) Is The Machines'; The Record Is Gone, No Key Is In The Log") {
        real_process_scenario("persist-89", SIGTERM, false);
    } TEST_END();

    TEST_CASE("S3.96 The Real Program, Killed (SIGKILL: A Crash, The Container's Death) In The Middle Of A Match Of A Room That The Control Interface Made: The Record Holds Every Turn That A Machine Has Been Sent (A Turn Is Written Before It Is Sent: No Turn Is Lost That A Player Has Seen), The Program Started Again Restores The Room, Both Machines Find It By Themselves And Are Not Told That They Are Ahead, The Match Goes On And Ends In One State On The Machines And On The Referee")  {
        real_process_scenario("persist-90", SIGKILL, true);
    } TEST_END();

    TEST_CASE("S3.199 The Real Program, The Rematch Over Real TCP (--demo-lobbies): Two Pages Make A Lobby, START, Their Games Take The Seats Over With The Keys, One Quits And The Rules End The Match; The Same Code Is A Lobby At Once (Nobody Leads, The Match's Colors; The Control Interface Lists The Match Beside It); A Results Screen Holds Its Seat, A Stranger Is Turned Away (MatchRunning), The Host's Page Comes Back With Its Key And Leads, START Waits And The Host Is Told Who Is Missing, The Host Removes Them (Their Results Screen Is Told) And START Is Taken; No Key Is In The Log") {
        rematch_process_scenario();
    } TEST_END();

    TEST_CASE("S3.101 The Real Container (Opt-In: ANTS_PERSIST_CONTAINER Names A Container That The Operator Made From The Server's Image With A Volume At /results, --reconnect And The Ports ANTS_PERSIST_GAME_PORT / ANTS_PERSIST_CTL_PORT / ANTS_PERSIST_SECRET): A Match Of Two Machines, docker stop (SIGTERM, Exit Code 0 Within The Grace), docker start, The Record On The Volume Is For Its Owner Only, The Machines Find The Room By Themselves, The Match Ends In One State On The Machines And The Referee, The Record Is Gone, No Key Is In The Container's Log") {
        const char* name = std::getenv("ANTS_PERSIST_CONTAINER");
        const char* game = std::getenv("ANTS_PERSIST_GAME_PORT");
        const char* ctl_port = std::getenv("ANTS_PERSIST_CTL_PORT");
        const char* secret = std::getenv("ANTS_PERSIST_SECRET");
        if (name == nullptr || game == nullptr || ctl_port == nullptr || secret == nullptr) {
            std::cout << "\n      [container] skipped: ANTS_PERSIST_CONTAINER, _GAME_PORT, _CTL_PORT and _SECRET are not all set (the opt-in check of a real container: tests/test_server)" << std::flush;
        } else {
            const std::string container = name;
            ASSERT_FALSE(container.empty());
            for (const char c : container) ASSERT_TRUE(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '-');      // (it goes into a shell command)
            container_scenario(container, static_cast<uint16_t>(std::atoi(game)), static_cast<uint16_t>(std::atoi(ctl_port)), secret);
        }
    } TEST_END();
}

namespace {

// Waits until the log of a server process holds `text` (the server logs the state of its restart folder after its listeners are open: a port that accepts says nothing about it)
bool log_has(const std::string& log_file, const std::string& text, uint32_t timeout_ms) {
    const uint32_t end = wall_ms() + timeout_ms;
    while (static_cast<int32_t>(wall_ms() - end) < 0) {
        if (text_of_file(log_file).find(text) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return text_of_file(log_file).find(text) != std::string::npos;
}

}  // namespace

void run_persist_review_process_tests() {
    TEST_CASE("S3.105 The Real Program Over A Folder That Another Server Holds (M5 Of The Review): The Second Server With The Same --restart-dir Exits With Status 1 And Says Why; The Second Server With The Same Results Folder (The Default Restart Folder) Runs, Logs That Restart Records Are Off, And Keeps None; Neither Touches The Temporary File Of The First One's Record; When The First Is Killed (SIGKILL) The Lock Is Gone With It And A Third Server Takes The Folder") {
        const fs::path root = temp_dir_for("persist-105");
        const fs::path results = root / "results";
        const fs::path restart_dir = results / "restart";
        const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
        const auto args = [&](uint16_t port, const std::vector<std::string>& more) {
            std::vector<std::string> a = {"--maps", maps_dir(), "--port", std::to_string(port)};
            a.insert(a.end(), more.begin(), more.end());
            return a;
        };
        // ---- the first server has the folder (the default one of its results folder) ---------------------------------------------------------------------------------------------------------------
        const std::string log_a = (root / "a.log").string();
        ServerProcess a;
        ASSERT_TRUE(a.start(args(free_tcp_port(), {"--results-dir", results.string(), "--reconnect"}), secret, log_a));
        ASSERT_TRUE(log_has(log_a, "restart records in ", 15000));
        ASSERT_TRUE(fs::exists(restart_dir / ".lock"));
        const fs::path temp_of_a = restart_dir / "room-A-0000abcd.restart.0123456789abcdef.tmp";       // a record that the first server is making, as far as anybody else can tell
        write_all_bytes(temp_of_a, {1, 2, 3});
        // ---- a second server that is told to keep its records in that folder: it cannot, and the operator asked for it: status 1 ---------------------------------------------------------------------------
        {
            const std::string log_b = (root / "b.log").string();
            ServerProcess b;
            ASSERT_TRUE(b.start(args(free_tcp_port(), {"--restart-dir", restart_dir.string()}), secret, log_b));
            int status = 0;
            ASSERT_TRUE(b.wait_exit(15000, status) >= 0);
            ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 1);
            ASSERT_TRUE(text_of_file(log_b).find("another server is using the restart folder") != std::string::npos);
        }
        // ---- a second server over the same results folder: its default restart folder is taken: it runs without records and says so ------------------------------------------------------------
        {
            const std::string log_c = (root / "c.log").string();
            ServerProcess c;
            const uint16_t port_c = free_tcp_port();
            ASSERT_TRUE(c.start(args(port_c, {"--results-dir", results.string(), "--reconnect"}), secret, log_c));
            ASSERT_TRUE(log_has(log_c, "restart records are off: another server is using the restart folder", 15000));
            ASSERT_TRUE(port_accepts(port_c, 15000));                                                  // (it runs: the game goes on without records)
            ASSERT_TRUE(text_of_file(log_c).find("restart records in ") == std::string::npos);
            c.signal_it(SIGTERM);
            int status = 0;
            ASSERT_TRUE(c.wait_exit(10000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        ASSERT_TRUE(fs::exists(temp_of_a));                                                           // neither of them removed what is the first server's
        // ---- the first server dies without a word (SIGKILL): the lock goes with the process, and a third server has the folder -----------------------------------------------------------------------
        a.signal_it(SIGKILL);
        int killed = 0;
        ASSERT_TRUE(a.wait_exit(10000, killed) >= 0 && WIFSIGNALED(killed));
        {
            const std::string log_d = (root / "d.log").string();
            ServerProcess d;
            ASSERT_TRUE(d.start(args(free_tcp_port(), {"--restart-dir", restart_dir.string(), "--reconnect"}), secret, log_d));
            ASSERT_TRUE(log_has(log_d, "restart records in ", 15000));
            ASSERT_TRUE(text_of_file(log_d).find("another server") == std::string::npos);
            ASSERT_FALSE(fs::exists(temp_of_a));                                                      // (the lock is held now: the leftover of the dead server is cleaned up)
            d.signal_it(SIGTERM);
            int status = 0;
            ASSERT_TRUE(d.wait_exit(10000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    } TEST_END();
}

void run_persist_review_process_tests_2() {
    TEST_CASE("S3.107 The Real Program Told To Stop While It Restores (M2 Of The Review): Eight Records Of A Busy Match On TREASURE (Four Players, Three Minutes Of Play) Take Seconds To Replay; SIGTERM Comes While The First Are Being Replayed And The Program Leaves At Once (The Replays Run In Slices From The Main Loop, Which Looks At The Stop Flag Every Pass: Not After The Replays Are Over), With Status 0, Every Record Untouched, Nothing Deleted, Nothing Restored") {
        const fs::path root = temp_dir_for("persist-107");
        const fs::path results = root / "results";
        const fs::path restart_dir = results / "restart";
        fs::create_directories(restart_dir);
        const RestartStore names{[&]() {
            RestartConfig c;
            c.dir = restart_dir.string();
            return c;
        }()};
        std::vector<std::string> paths;
        std::vector<std::vector<uint8_t>> contents;
        uint32_t turns = 0;
        {   // a match of four machines on the busiest map, played for three minutes (they give orders: the replay does what the match did), and copies of its record under other codes
            PWorld w("persist-107w");
            w.start_server(500);
            RoomSpec spec = held_spec("ST-1", 4, "TREASURE.LVL");
            spec.run_ms = 24u * 3600u * 1000u;
            std::vector<RClient*> m = play_room(w, spec, 180000);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);
            const std::vector<uint8_t> bytes = read_all_bytes(w.record_path("ST-1"));
            const RestartLoaded base = w.read_record("ST-1");
            ASSERT_TRUE(base.ok() && !base.torn);
            turns = base.turn_count;
            const auto frames = frames_of(bytes);
            for (int i = 1; i <= 8; ++i) {
                RestartHead head = base.head;
                head.code = "ST-" + std::to_string(i);
                std::vector<uint8_t> copy = with_magic(encode_restart_head(head));
                const size_t at = copy.size();
                copy.resize(at + (bytes.size() - frames[0].second));
                std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end(), copy.begin() + static_cast<std::ptrdiff_t>(at));
                paths.push_back(names.path_for(head.code));
                write_all_bytes(paths.back(), copy);
                contents.push_back(copy);
            }
        }
        const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
        const std::vector<std::string> args = {"--maps", maps_dir(), "--port", std::to_string(free_tcp_port()), "--results-dir", results.string(), "--reconnect"};
        const std::string log_a = (root / "a.log").string();
        ServerProcess server;
        ASSERT_TRUE(server.start(args, secret, log_a));
        ASSERT_TRUE(log_has(log_a, "restart records in ", 15000));                                // (the restore starts right after this line: the replays take seconds)
        const auto began_stop = std::chrono::steady_clock::now();
        server.signal_it(SIGTERM);
        int status = 0;
        const int64_t took = server.wait_exit(60000, status);
        const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began_stop).count();
        ASSERT_TRUE(took >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        ASSERT_TRUE(stop_ms < 1500);                                                              // (not the seconds that the replays would still take)
        for (size_t i = 0; i < paths.size(); ++i) ASSERT_TRUE(fs::exists(paths[i]) && read_all_bytes(paths[i]) == contents[i]);           // every record is as it was: nothing deleted, nothing written
        const std::string log_text = text_of_file(log_a);
        ASSERT_TRUE(log_text.find("the restore was stopped by the server's stop: 8 restart record(s) are left on disk as they were") != std::string::npos);
        ASSERT_TRUE(log_text.find(" restored: ") == std::string::npos && log_text.find("was not restored") == std::string::npos);
        std::cout << "\n      [stop in a restore] the program left " << stop_ms << " ms after SIGTERM, in the middle of the replays of eight matches of " << turns << " turns, with every record untouched" << std::flush;
    } TEST_END();

    TEST_CASE("S3.108 The Record Exists Before The Start Is Sent (L3 Of The Review): A Room Of Two Machines Is Started; At The Moment The Server Sends The Start Message To Each Of Them (A Tap On The Server's End Of The Links Looks At The Folder) The Record Of The Room Is On Disk, Whole, With Both Keys And The Start Message That Is Being Sent; A Start That Is Cancelled (A Machine That Cannot Load The Map) Takes Its Record Away And The Next Start Makes It Again Before Its Start Is Sent") {
        PWorld w("persist-108");
        w.start_server(500);
        int starts_sent = 0;
        int with_record = 0;
        int whole_and_the_same = 0;
        w.on_server_send = [&](const std::vector<uint8_t>& m) {
            if (net::peek_type(m) != net::MsgType::Start) return;
            ++starts_sent;
            const std::string path = w.record_path("L3-1");
            if (!fs::exists(path)) return;
            ++with_record;
            const RestartLoaded rec = read_restart_record(path, 1ull << 30);
            net::StartMsg sent;
            if (rec.ok() && rec.head.code == "L3-1" && rec.turns.empty() && !net::key_is_zero(rec.head.keys[0]) && !net::key_is_zero(rec.head.keys[1]) && net::decode(m, sent) && sent.seed == rec.head.start.seed && sent.roster == rec.head.start.roster &&
                sent.map_hash == rec.head.start.map_hash) {
                ++whole_and_the_same;                                                           // (the addresses of the clients are the one thing that the record's copy does not have: it has no business with them)
            }
        };
        ASSERT_TRUE(w.mgr->create_room(held_spec("L3-1", 2), w.server_now()).ok);
        w.connect("Ann", "L3-1");
        RClient& bad = w.connect("Bob", "L3-1");
        bad.fail_load = true;                                                                   // Bob cannot load the map: the first start is cancelled
        ASSERT_TRUE(w.until([&]() { return starts_sent >= 2 && w.status("L3-1").state == RoomState::Waiting; }, 20000));
        ASSERT_TRUE(w.record_files().empty());                                                  // the cancel took the record away
        ASSERT_EQ(starts_sent, 2);
        ASSERT_TRUE(with_record == 2 && whole_and_the_same == 2);                               // both Starts of the first attempt were sent with the record on disk
        bad.fail_load = false;                                                                  // (Bob stays in the room: the retry comes two seconds after the cancel, and now he can load the map)
        ASSERT_TRUE(w.until([&]() { return w.status("L3-1").state == RoomState::Running; }, 30000));
        ASSERT_TRUE(starts_sent >= 4 && with_record == starts_sent && whole_and_the_same == starts_sent);       // every Start of every attempt was sent with its record on disk
        ASSERT_TRUE(w.status("L3-1").record_kept && w.record_files().size() == 1);
    } TEST_END();

    TEST_CASE("S3.109 A Record That Cannot Be Deleted Is Not Forgotten (L1 Of The Review): The Delete That Fails (Here: A Folder With Something In It Takes The File's Name) Leaves The File Of A Room That Is Over; The Room's Status Says So (record.stale, A Note) And Says Kept While The File Is There (Never Kept: False), The Log Gets A Line; The Server Tries Again Every 10 s (Not Sooner) And When It Can The Status Is That Of A Room That Is Over And The Log Says So; The Server's Stop Tries Once More") {
#ifndef _WIN32
        const auto make_undeletable = [](PWorld& w, const std::string& code) {                  // the room's descriptor goes on writing the file that was moved aside; the name is now a folder that is not empty
            const std::string path = w.record_path(code);
            fs::rename(path, path + ".aside");
            fs::create_directory(path);
            write_all_bytes(fs::path(path) / "x", {1});
        };
        PWorld w("persist-109");
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("DL-1", 2), 6000);
        make_undeletable(w, "DL-1");
        RoomStatus s = w.status("DL-1");
        ASSERT_TRUE(s.record_kept && !s.record_stale);
        ctl::HttpRequest rq;
        rq.method = "DELETE";
        rq.path = "/rooms/DL-1";
        ASSERT_EQ(handle_control(*w.mgr, rq, w.server_now()).status, 200);                      // the owner closes the room: its record is to go
        const std::string path = w.record_path("DL-1");
        ASSERT_TRUE(fs::exists(path));                                                           // ... and cannot
        s = w.status("DL-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.record_kept && s.record_stale && s.record_note.find("could not be deleted") != std::string::npos);      // kept: true while the file is there
        {
            const ctl::JsonValue j = status_json(w, "DL-1");
            ASSERT_TRUE(j.get("record").get("kept").as_bool_or(false) && j.get("record").get("stale").as_bool_or(false) && j.get("record").get("note").str() == s.record_note);
        }
        w.collect();                                                                             // (the lines that the server would have logged)
        bool noted = false;
        for (const std::string& n : w.notices) noted = noted || (n.find("could not be deleted") != std::string::npos && n.find("DL-1") != std::string::npos);
        ASSERT_TRUE(noted);
        for (const std::string& n : w.notices) ASSERT_TRUE(n.find(w.restart.dir) == std::string::npos);        // (a line names the file, not the folder of the server)
        ASSERT_EQ(w.mgr->restart_store()->stale_count(), size_t{1});
        // the obstacle goes at once; the server tries again every 10 s: not before 5 s, and by 11 s the file is gone
        fs::remove(fs::path(path) / "x");
        w.run(5000);
        ASSERT_TRUE(fs::exists(path) && w.status("DL-1").record_stale);
        w.run(6000);
        ASSERT_FALSE(fs::exists(path));
        s = w.status("DL-1");
        ASSERT_TRUE(!s.record_kept && !s.record_stale && s.record_note == "the room is over");
        ASSERT_EQ(w.mgr->restart_store()->stale_count(), size_t{0});
        noted = false;
        for (const std::string& n : w.notices) noted = noted || (n.find("was deleted") != std::string::npos && n.find("DL-1") != std::string::npos);
        ASSERT_TRUE(noted);
        // the retries go on while the obstacle stays: a second room, 25 s of tries, the file is still there and still stale
        std::vector<RClient*> m2 = play_room(w, held_spec("DL-2", 2), 6000);
        make_undeletable(w, "DL-2");
        ASSERT_TRUE(w.mgr->close_room("DL-2", w.server_now()));
        w.run(25000);
        ASSERT_TRUE(fs::exists(w.record_path("DL-2")) && w.status("DL-2").record_stale && w.mgr->restart_store()->stale_count() == 1);
        // the stop tries once more, at once: the obstacle goes just before it
        fs::remove(fs::path(w.record_path("DL-2")) / "x");
        w.mgr->shutdown(w.server_now());
        ASSERT_FALSE(fs::exists(w.record_path("DL-2")));
        ASSERT_EQ(w.mgr->restart_store()->stale_count(), size_t{0});
#endif
    } TEST_END();

    TEST_CASE("S3.110 A Record That Was Read And Refused Is Kept For A Day (L2 Of The Review): The Record Of A Match That Another Network Protocol Ended Goes To restart/refused/ Whole (Mode 600 In A Folder Of 700), The Log Says So, And The Owner Who Moves It Back When The Cause Is Gone Has The Match Restored; A Record That Is Corrupt Is Deleted At Once; At Start The Folder Is Purged Of What Is Older Than 24 Hours (Not Of Younger Files); The Folder Is Held Under The Budget (The Oldest Go First) And A Record That Alone Passes It Is Deleted") {
        const auto refused_dir = [](PWorld& w) { return fs::path(w.restart.dir) / "refused"; };
        const auto name_of = [](PWorld& w, const std::string& code) { return fs::path(w.record_path(code)).filename(); };
        {   // refused: kept, whole, private; moved back by the owner it is restored
            PWorld w("persist-110a");
            w.start_server(500);
            crash_with_record_of(w, "RF-1", 6000);
            const std::vector<uint8_t> bytes = read_all_bytes(w.record_path("RF-1"));
            w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.status("RF-1").state == RoomState::Failed);
            ASSERT_TRUE(w.record_files().empty());                                               // (not in the folder that a start reads any more)
            const fs::path kept = refused_dir(w) / name_of(w, "RF-1");
            ASSERT_TRUE(fs::exists(kept) && read_all_bytes(kept) == bytes);
#ifndef _WIN32
            struct stat st;
            ASSERT_EQ(::stat(kept.c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);
            ASSERT_EQ(::stat(refused_dir(w).c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);
#endif
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("RF-1") != std::string::npos && n.find("refused") != std::string::npos && n.find("24 hours") != std::string::npos);
            ASSERT_TRUE(noted);
            w.stop_server(false);
            w.restart.identity.protocol = net::kProtocolVersion;                                 // the cause is gone (the server is what it was) and the owner moves the file back
            fs::rename(kept, w.record_path("RF-1"));
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("RF-1").restored && w.status("RF-1").state == RoomState::Running);
        }
        {   // corrupt: deleted at once, nothing kept (a bit flipped in the middle, and a file that is no record at all)
            PWorld w("persist-110b");
            w.start_server(500);
            crash_with_record_of(w, "RF-2", 6000);
            std::vector<uint8_t> bytes = read_all_bytes(w.record_path("RF-2"));
            bytes[bytes.size() / 2] ^= 0x10;
            write_all_bytes(w.record_path("RF-2"), bytes);
            write_all_bytes(fs::path(w.restart.dir) / "room-JUNK-00000001.restart", {'j', 'u', 'n', 'k'});
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Unreadable) == 2 && w.record_files().empty());
            ASSERT_TRUE(!fs::exists(refused_dir(w)) || fs::is_empty(refused_dir(w)));
        }
        {   // the purge at start: what is older than 24 hours goes, what is younger stays
            PWorld w("persist-110c");
            w.start_server(500);
            w.stop_server(false);
            fs::create_directories(refused_dir(w));
            const auto plant = [&](const char* name, std::chrono::hours age) {
                write_all_bytes(refused_dir(w) / name, {1, 2, 3});
                fs::last_write_time(refused_dir(w) / name, fs::file_time_type::clock::now() - age);
            };
            plant("old-1.restart", std::chrono::hours(25));
            plant("old-2.restart", std::chrono::hours(72));
            plant("young-1.restart", std::chrono::hours(23));
            plant("young-2.restart", std::chrono::hours(1));
            w.start_server(500);
            ASSERT_TRUE(!fs::exists(refused_dir(w) / "old-1.restart") && !fs::exists(refused_dir(w) / "old-2.restart"));
            ASSERT_TRUE(fs::exists(refused_dir(w) / "young-1.restart") && fs::exists(refused_dir(w) / "young-2.restart"));
        }
        {   // the folder is held under the budget: the oldest go first; a record that alone passes the budget is deleted
            PWorld w("persist-110d");
            w.start_server(500);
            crash_with_record_of(w, "RF-3", 6000);
            const uint64_t size = fs::file_size(w.record_path("RF-3"));
            fs::create_directories(refused_dir(w));
            const auto plant = [&](const char* name, std::chrono::hours age) {
                write_all_bytes(refused_dir(w) / name, std::vector<uint8_t>(static_cast<size_t>(size), 7));
                fs::last_write_time(refused_dir(w) / name, fs::file_time_type::clock::now() - age);
            };
            plant("a.restart", std::chrono::hours(5));                                           // the oldest
            plant("b.restart", std::chrono::hours(4));
            plant("c.restart", std::chrono::hours(3));
            w.restart.budget_bytes = 3 * size + 10;                                              // room for three records of this size, not four
            w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Ended) == 1);
            ASSERT_TRUE(!fs::exists(refused_dir(w) / "a.restart") && fs::exists(refused_dir(w) / "b.restart") && fs::exists(refused_dir(w) / "c.restart") && fs::exists(refused_dir(w) / name_of(w, "RF-3")));
            // a record that alone is more than the budget is deleted, not kept
            w.stop_server(false);
            PWorld small("persist-110e");
            small.start_server(500);
            crash_with_record_of(small, "RF-4", 6000);
            const uint64_t big = fs::file_size(small.record_path("RF-4"));
            small.restart.budget_bytes = big - 1;
            small.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            small.start_server(500);
            ASSERT_TRUE(small.report.count(RestoreItem::Outcome::Ended) == 1 && small.record_files().empty());
            ASSERT_TRUE(!fs::exists(refused_dir(small) / name_of(small, "RF-4")));
        }
    } TEST_END();

    TEST_CASE("S3.111 A Folder That Cannot Be Written Is Found Out At The Start (L5 Of The Review): prepare() Makes A Probe File And Deletes It; A Folder Where A Write Fails (The Process May Not Grow A File: EFBIG, As A Full Disk Or A Quota Would Answer) Is Refused With The Reason And Holds No Lock; A Folder That Can Be Written Leaves No Probe Behind; A Room Manager Over Such A Folder Keeps No Records And Says Why") {
#ifndef _WIN32
        {
            RestartConfig cfg = test_restart_config("persist-111a");
            std::string why;
            {
                RestartStore ok(cfg);
                ASSERT_TRUE(ok.prepare(why));
                ASSERT_EQ(entries_but_lock(cfg.dir), size_t{0});                                   // no probe is left behind
            }
            {
                const FileSizeLimit none(0);                                                       // every write fails (the limit is 0 bytes) and no signal comes
                RestartStore refused(cfg);
                ASSERT_FALSE(refused.prepare(why));
                ASSERT_TRUE(why.find("cannot be written") != std::string::npos && why.find("File too large") != std::string::npos);
                ASSERT_EQ(entries_but_lock(cfg.dir), size_t{0});                                   // (and the probe that was made is deleted)
                RoomManager keeper{MapStore(maps_dir())};
                ASSERT_FALSE(keeper.enable_restart_records(cfg, why));
                ASSERT_TRUE(keeper.restart_store() == nullptr && why.find("cannot be written") != std::string::npos);
            }
            RestartStore again(cfg);                                                               // the refused store holds no lock: the folder is free once writes work
            ASSERT_TRUE(again.prepare(why));
        }
#endif
    } TEST_END();
}
#endif
// ---------------------------------------------------------------------------------------------------------------------------------
// The restore that never blocks the server (docs/NETWORK_PORT.md "Restart records", "Restoring"): S3.113 and on
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

struct WalkedTurns {
    std::vector<net::TurnMsg> turns;
    bool failed{false};
    std::string why;
    uint32_t read{0};
    bool stays_ended{true};
};

// Every turn that a RestartTurnReader gives for a record, and how it ended
WalkedTurns walk_turns(const RestartLoaded& rec) {
    WalkedTurns out;
    RestartTurnReader reader(rec);
    net::TurnMsg t;
    while (reader.next(t)) out.turns.push_back(t);
    out.failed = reader.failed();
    out.why = reader.why();
    out.read = reader.turns_read();
    out.stays_ended = !reader.next(t) && !reader.next(t);                                      // (a reader that has ended, or failed, says no again)
    return out;
}



// The record of `code` with a bit of the hash of its nth checkpoint changed (and the frame's checksum made again): whole as far as the format goes and wrong in what it says, so that a replay
// is refused at that checkpoint
void tamper_checkpoint(PWorld& w, const std::string& code, size_t nth) {
    const std::vector<uint8_t> bytes = read_all_bytes(w.record_path(code));
    const auto frames = frames_of(bytes);
    size_t seen = 0;
    size_t at = 0;
    for (size_t i = 1; i < frames.size() && at == 0; ++i) {
        if (bytes[frames[i].first] == 3 && ++seen == nth) at = i;
    }
    if (at == 0) throw std::runtime_error("tamper_checkpoint: the record has no such checkpoint");
    std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(frames[at].first + 5), bytes.begin() + static_cast<std::ptrdiff_t>(frames[at].second - 4));
    payload[8] = static_cast<uint8_t>(payload[8] ^ 1);
    const std::vector<uint8_t> frame = test_frame(3, payload);
    std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[at].first));
    out.insert(out.end(), frame.begin(), frame.end());
    out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[at].second), bytes.end());
    write_all_bytes(w.record_path(code), out);
}


// A raw connection that says Hello for `room` (with a key, or without) on a new link of the world: what a client does before it has a session
net::Connection* say_hello(PWorld& w, const std::string& room, const net::SeatKey& key = net::SeatKey{}) {
    net::Connection* link = w.open_link();
    net::HelloMsg hello;
    hello.name = "Raw";
    hello.room = room;
    hello.key = key;
    hello.have_turns = 0;
    link->send(net::encode(hello));
    return link;
}

// The codes of a report's items in the order in which they were decided
std::string decided_in_order(const RestoreReport& report) {
    std::string out;
    for (const RestoreItem& i : report.items) out += (out.empty() ? "" : " ") + i.code;
    return out;
}

// The records of the folder as bytes, by name (to see that nothing was written to them)
std::map<std::string, std::vector<uint8_t>> folder_bytes(const PWorld& w) {
    std::map<std::string, std::vector<uint8_t>> out;
    for (const std::string& path : w.record_files()) out[fs::path(path).filename().string()] = read_all_bytes(path);
    return out;
}

}  // namespace

void run_restore_tests() {
    TEST_CASE("S3.113 The Turn Reader (RestartTurnReader, The Pull Reader That A Replay In Slices Needs): It Gives Exactly The Turns Of A Record, One At A Time And In Order, Of Every Kind That The Record Tests Make (A Writer's Record With A Turn To A Frame And Checkpoints Between, Frames Of 20 And Of 4096 Turns, A Match That Was Really Played, A Record With No Turn), Read Whole Or Streaming; It Stops At A Torn Tail Where The Streaming Read Stops; It Fails (failed(), why()) For A Record That Was Not Judged Good And For Bytes That Are Not What Was Judged (Cut Inside A Frame Or At The End Of One, Renumbered, A Frame With Bytes After Its Last Turn, A Turn With Too Many Commands); Two Readers Of One Record Do Not Disturb Each Other; for_each_restart_turn Is The Same Walk") {
        RestartConfig cfg = test_restart_config("restore-113");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const auto read_both = [&](const std::string& path) { return std::make_pair(read_restart_record(path, cfg.max_record_bytes), read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming)); };
        // the reader gives exactly the turns that the whole read keeps, from the whole record and from the streamed one, and ends without a fault
        const auto expect_all = [&](const std::string& path, uint32_t expected, const char* what) {
            const auto both = read_both(path);
            ASSERT_MSG(both.first.ok() && both.second.ok() && both.first.turn_count == expected && both.second.turn_count == expected && both.first.turns.size() == expected, what);
            for (const RestartLoaded* rec : {&both.first, &both.second}) {
                const WalkedTurns w = walk_turns(*rec);
                ASSERT_MSG(!w.failed && w.why.empty() && w.turns.size() == expected && w.read == expected && w.stays_ended, what);
                ASSERT_MSG(same_turns(w.turns, both.first.turns, expected), what);
                for (uint32_t i = 0; i < expected; ++i) ASSERT_MSG(w.turns[i].turn == i, what);       // (numbered from 0 without a hole)
                uint32_t seen = 0;                                                                     // for_each_restart_turn is the same walk
                ASSERT_MSG(for_each_restart_turn(*rec, [&](const net::TurnMsg& t) { return t.turn == seen++; }) && seen == expected, what);
            }
        };
        // ---- a record that the writer made: one turn to a frame, a checkpoint after every 20th turn, commands of every kind that sample_turn makes (a Drop among them) -------------------------------
        std::string path_a;
        {
            auto writer = store.create(sample_head("RD-A"), why);
            ASSERT_TRUE(writer != nullptr);
            for (uint32_t n = 0; n < 200; ++n) {
                ASSERT_TRUE(writer->append_turn(sample_turn(n)));
                if ((n + 1) % net::kHashEveryTurns == 0) ASSERT_TRUE(writer->append_check(n, 0x1000u + n));
            }
            ASSERT_TRUE(writer->sync());
            path_a = writer->path();
        }
        expect_all(path_a, 200, "the writer's record");
        // ---- frames of many turns (20, then 4096 empty ones) and no turn at all ------------------------------------------------------------------------------------------------------------------
        const std::string path_b = store.path_for("RD-B");
        {
            std::vector<net::TurnMsg> twenty;
            std::vector<net::TurnMsg> next_ten;
            for (uint32_t n = 0; n < 20; ++n) twenty.push_back(sample_turn(n));
            for (uint32_t n = 20; n < 30; ++n) next_ten.push_back(sample_turn(n));
            const std::vector<uint8_t> head = encode_restart_head(sample_head("RD-B"));
            write_all_bytes(path_b, with_magic(concat({head, test_frame(2, turns_payload(0, twenty)), test_frame(3, check_payload(19, 77)), test_frame(2, turns_payload(20, next_ten))})));
        }
        expect_all(path_b, 30, "frames of 20 and of 10 turns with a checkpoint between");
        const std::string path_big = store.path_for("REC-1");
        write_all_bytes(path_big, empty_turns_record(10000, 4096));                                    // (4096 + 4096 + 1808)
        expect_all(path_big, 10000, "frames of 4096 turns");
        const std::string path_none = store.path_for("RD-E");
        write_all_bytes(path_none, with_magic(encode_restart_head(sample_head("RD-E"))));
        expect_all(path_none, 0, "a record that holds no turn");
        // ---- a match that was really played (the turns of a real room: orders of every kind that the machines give) -----------------------------------------------------------------------------------
        {
            PWorld w("restore-113w");
            w.start_server(500);
            crash_with_record_of(w, "RD-C", 16000);
            const RestartLoaded real = w.read_record("RD-C");
            ASSERT_TRUE(real.ok() && real.turn_count > 250 && !real.checks.empty());
            expect_all(w.record_path("RD-C"), real.turn_count, "a match that was played");
        }
        // ---- a torn tail: the reader stops where the streaming read stops, without a fault -------------------------------------------------------------------------------------------------------------
        {
            std::vector<uint8_t> bytes = read_all_bytes(path_a);
            const std::vector<uint8_t> next = test_frame(2, turns_payload(200, {sample_turn(200)}));
            bytes.insert(bytes.end(), next.begin(), next.begin() + 9);
            const std::string torn_path = store.path_for("RD-T");
            write_all_bytes(torn_path, bytes);
            const auto both = read_both(torn_path);
            ASSERT_TRUE(both.first.ok() && both.second.ok() && both.first.torn && both.second.torn && both.second.good_bytes < both.second.file_bytes);
            for (const RestartLoaded* rec : {&both.first, &both.second}) {
                const WalkedTurns w = walk_turns(*rec);
                ASSERT_TRUE(!w.failed && w.turns.size() == 200 && w.read == 200 && w.stays_ended);        // (the bytes of the torn frame are not read as a turn)
                ASSERT_TRUE(same_turns(w.turns, both.first.turns, 200));
            }
        }
        // ---- bytes that are not what was judged ------------------------------------------------------------------------------------------------------------------------------------------------------
        const RestartLoaded base = read_restart_record(path_a, cfg.max_record_bytes, RestartRead::Streaming);
        ASSERT_TRUE(base.ok() && !base.bytes.empty());
        const std::vector<std::pair<size_t, size_t>> frames = frames_of(base.bytes);
        std::vector<size_t> turn_frames;
        for (size_t i = 0; i < frames.size(); ++i) {
            if (base.bytes[frames[i].first] == 2) turn_frames.push_back(i);
        }
        ASSERT_EQ(turn_frames.size(), size_t{200});
        {   // cut inside a frame: the turns before it are given, then the reader fails (and says so again)
            RestartLoaded cut = base;
            cut.bytes.resize(frames[turn_frames[50]].second - 3);
            const WalkedTurns w = walk_turns(cut);
            ASSERT_TRUE(w.failed && w.why.find("end inside a frame") != std::string::npos && w.turns.size() == 50 && w.read == 50 && w.stays_ended);
            size_t seen = 0;
            ASSERT_FALSE(for_each_restart_turn(cut, [&](const net::TurnMsg&) { ++seen; return true; }));       // the walk that fails is no complete walk
            ASSERT_EQ(seen, size_t{50});
        }
        {   // cut at the end of a frame: every frame that is left is whole, but the record held more turns than that
            RestartLoaded cut = base;
            cut.bytes.resize(frames[turn_frames[50]].second);
            const WalkedTurns w = walk_turns(cut);
            ASSERT_TRUE(w.failed && w.why.find("fewer turns") != std::string::npos && w.turns.size() == 51 && w.read == 51 && w.stays_ended);
            size_t seen = 0;
            ASSERT_FALSE(for_each_restart_turn(cut, [&](const net::TurnMsg&) { ++seen; return true; }));
            ASSERT_EQ(seen, size_t{51});
        }
        {   // a whole record whose turn list is shorter than the count that it was judged to hold
            RestartLoaded shorter = read_restart_record(path_a, cfg.max_record_bytes);
            shorter.turns.resize(120);
            const WalkedTurns w = walk_turns(shorter);
            ASSERT_TRUE(w.failed && w.why.find("fewer turns") != std::string::npos && w.turns.size() == 120);
        }
        {   // renumbered: the first turn of a frame is not the next turn
            RestartLoaded renumbered = base;
            renumbered.bytes[frames[turn_frames[30]].first + 5] = static_cast<uint8_t>(renumbered.bytes[frames[turn_frames[30]].first + 5] + 1);
            const WalkedTurns w = walk_turns(renumbered);
            ASSERT_TRUE(w.failed && w.why.find("not numbered") != std::string::npos && w.turns.size() == 30 && w.stays_ended);
        }
        {   // a frame of 20 turns that says 19: the bytes of the twentieth are after its last turn
            RestartLoaded shortened = read_restart_record(path_b, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(shortened.ok());
            const auto b_frames = frames_of(shortened.bytes);
            ASSERT_TRUE(shortened.bytes[b_frames[1].first] == 2 && shortened.bytes[b_frames[1].first + 9] == 20);
            shortened.bytes[b_frames[1].first + 9] = 19;
            const WalkedTurns w = walk_turns(shortened);
            ASSERT_TRUE(w.failed && w.why.find("bytes after its last turn") != std::string::npos && w.turns.size() == 19 && w.stays_ended);
        }
        {   // a turn that says it holds more commands than a turn may
            RestartLoaded greedy = read_restart_record(path_big, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(greedy.ok());
            const auto g_frames = frames_of(greedy.bytes);
            const size_t at = g_frames[1].first + 5 + 6 + 2 * 5;                                       // the command count of the sixth turn of the first frame
            ASSERT_TRUE(greedy.bytes[at] == 0 && greedy.bytes[at + 1] == 0);
            greedy.bytes[at] = 0xFF;
            greedy.bytes[at + 1] = 0xFF;
            const WalkedTurns w = walk_turns(greedy);
            ASSERT_TRUE(w.failed && w.why.find("more commands than a turn may") != std::string::npos && w.turns.size() == 5 && w.stays_ended);
        }
        {   // a record that was not judged good gives nothing and says why (a flipped bit in its head)
            std::vector<uint8_t> bytes = read_all_bytes(path_a);
            bytes[sizeof(kRestartMagic) + 9] ^= 0x10;
            const std::string rotten_path = store.path_for("RD-R");
            write_all_bytes(rotten_path, bytes);
            const RestartLoaded rotten = read_restart_record(rotten_path, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_FALSE(rotten.ok());
            const WalkedTurns w = walk_turns(rotten);
            ASSERT_TRUE(w.failed && w.turns.empty() && w.why.find("not read as a good one") != std::string::npos && w.stays_ended);
            ASSERT_FALSE(for_each_restart_turn(rotten, [](const net::TurnMsg&) { return true; }));
        }
        {   // two readers of one record, taken turn by turn in turns: each gives the whole record (the state of a reader is its own: the manager holds several jobs)
            RestartTurnReader first(base);
            RestartTurnReader second(base);
            std::vector<net::TurnMsg> a;
            std::vector<net::TurnMsg> b;
            net::TurnMsg t;
            for (int round = 0; round < 400; ++round) {
                RestartTurnReader& r = (round % 3 == 0) ? second : first;
                std::vector<net::TurnMsg>& into = (round % 3 == 0) ? b : a;
                if (r.next(t)) into.push_back(t);
            }
            while (first.next(t)) a.push_back(t);
            while (second.next(t)) b.push_back(t);
            ASSERT_TRUE(!first.failed() && !second.failed() && a.size() == 200 && b.size() == 200);
            const RestartLoaded kept = read_restart_record(path_a, cfg.max_record_bytes);
            ASSERT_TRUE(same_turns(a, kept.turns, 200) && same_turns(b, kept.turns, 200));
        }
    } TEST_END();

    TEST_CASE("S3.114 The Replay In Slices (Room::begin_replay, Room::replay_step): A Match That Was Played (Two Persons And A Bot, One Person Gone For Good) Is Replayed By Rooms Of Their Own In One Call And In Slices Of 1, 7 And 100 ms (By A Clock That Runs On The Room's Own Work): Every One Is The Same Room (The State Hash Is That Of An Independent Replay, The Seats Are Held And Dropped The Same, The Bot Sits Down, /busy Counts The Same); A Slice Ends Within 20 Turns' Work Of Its Time And Not Before It, The Time Between Slices Is Not The Room's Work (The Cap Of A Room Is On The Sum Of Its Own Slices, To The Millisecond), A Disagreeing Checkpoint Refuses The Room In The Slice That Meets It; What Is Not A Replay Under Way Is Refused; A Half-Built Room Gives Its Log Back; The Record May Go When The Replay Is Over") {
        PWorld w("restore-114");
        w.start_server(500);
        RoomSpec spec = held_spec("RS-0", 3);
        spec.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Easy});
        ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
        RClient& ann = w.connect("Ann", "RS-0");
        RClient& bob = w.connect("Bob", "RS-0");
        w.run(15000 + kPre);
        ASSERT_TRUE(w.status("RS-0").state == RoomState::Running && w.status("RS-0").bot_controller);
        const uint8_t seat_bob = bob.lobby->my_seat();
        bob.session->leave();                                                              // Bob quits: his Drop is in the record, his seat is gone for good
        w.run(1500);
        ASSERT_TRUE(w.status("RS-0").state == RoomState::Running && !w.status("RS-0").paused);
        ann.reconnects = false;
        bob.reconnects = false;
        w.stop_server(false);
        const RestartLoaded whole = w.read_record("RS-0");
        ASSERT_TRUE(whole.ok() && whole.turn_count > 300 && whole.checks.size() >= 15);
        const uint32_t n = whole.turn_count;
        const uint32_t last_boundary = (n / net::kHashEveryTurns) * net::kHashEveryTurns;      // the clock is looked at after turns 19, 39, ...: the last look is after this many turns
        const uint64_t independent = replay_hash(whole, n);                                // the state that an engine run through the turns by itself stands at
        const std::string maps = maps_dir();
        // a room as the manager makes one for a record: from its head, with the server's store and memory for logs
        RestartStore store(w.restart);
        std::string why;
        ASSERT_TRUE(store.prepare(why));                                                   // (the server that made the records is gone: the folder is free)
        net::LogBudget budget(256ull * 1024 * 1024);
        const auto make_room = [&](const RestartLoaded& rec, RestartStore& with_store) {
            MapEntry entry;
            std::string problem;
            if (!MapStore(maps).find(rec.head.map, entry, &problem)) throw std::runtime_error("map: " + problem);
            assets::LevelData level;
            if (!level.load_from_file(entry.path)) throw std::runtime_error("level");
            auto room = std::make_unique<Room>(room_spec_of(rec.head), std::move(entry), std::move(level), rec.head.start.seed, 1000u, &budget);
            room->set_restart_store(&with_store);
            return room;
        };
        const auto picture = [](Room& room, uint32_t now) {                                // everything that makes a restored room what it is
            const RoomStatus s = room.status(now);
            const RoomBusy b = room.busy(now);
            std::ostringstream out;
            out << room.code().substr(0, 2) << " " << room_state_name(s.state) << " hash " << s.restored_hash << " ticks " << s.ticks << " turns " << s.turns << " restored " << s.restored << "/" << s.restored_turns
                << " paused " << s.paused << " joined " << static_cast<int>(s.joined) << " bots " << s.bots.size() << "/" << s.bot_controller << " busy " << b.match << "/" << b.players << " absent";
            for (const RoomStatus::Absent& a : s.absent) out << " " << static_cast<int>(a.seat) << (a.catching_up ? "c" : "a");
            out << " vote " << static_cast<int>(s.vote_seat);
            return out.str();
        };
        // the clock of these tests runs on the room's own work: a millisecond for every turn that its replay has read (times `per_turn`), and on whatever time other rooms took between its slices
        Room* under_test = nullptr;
        uint32_t per_turn = 1;
        uint32_t foreign = 0;
        const std::function<uint32_t()> work_clock = [&]() { return (under_test != nullptr ? under_test->replay_progress() * per_turn : 0u) + foreign; };
        int next_code = 1;
        const auto copy_for_room = [&]() {                                                 // the same record under a code of its own: a room that begins writes its record again
            const std::string code = "RS-" + std::to_string(next_code++);
            copy_record_as(w, "RS-0", code, std::chrono::seconds(0));
            return code;
        };
        // ---- one call: the room that every slice size is compared with --------------------------------------------------------------------------------------------------------------------------------
        std::string reference;
        {
            const std::string code = copy_for_room();
            auto rec = std::make_unique<RestartLoaded>(read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming));
            ASSERT_TRUE(rec->ok() && rec->turn_count == n);
            auto room = make_room(*rec, store);
            under_test = room.get();
            ASSERT_TRUE(room->replay(*rec, 90000, why, work_clock) == Room::ReplayStep::Replayed);
            under_test = nullptr;
            rec.reset();                                                                   // (the record may go when the replay is over: the room needs no byte of it any more)
            ASSERT_TRUE(room->begin_restored(5000, why));
            ASSERT_EQ(room->status(5000).restored_hash, independent);                      // the replay is the match: an engine that was run through the turns by itself stands at the same state
            ASSERT_TRUE(room->status(5000).paused && room->status(5000).absent.size() == 1 && room->status(5000).bot_controller);        // (one person is held: Bob is gone for good, and the bot has sat down)
            for (const RoomStatus::Absent& a : room->status(5000).absent) ASSERT_TRUE(a.seat != seat_bob);
            reference = picture(*room, 5000);
            ASSERT_TRUE(reference.find("RS running") != std::string::npos && reference.find("restored 1/" + std::to_string(n)) != std::string::npos);
            room->update(5100);                                                            // (the room goes on from what the replay made)
            ASSERT_TRUE(room->state() == RoomState::Running);
        }
        // ---- the same record in slices: every one is the same room, a slice ends within 20 turns' work of its time and not before it -------------------------------------------------------------------
        for (const uint32_t slice : {1u, 7u, 100u, 20000u}) {
            const std::string code = copy_for_room();
            auto rec = std::make_unique<RestartLoaded>(read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming));
            auto room = make_room(*rec, store);
            under_test = room.get();
            foreign = 0;
            ASSERT_TRUE(room->begin_replay(*rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_EQ(room->replay_progress(), 0u);
            int steps = 0;
            uint32_t before = 0;
            Room::ReplayStep step = Room::ReplayStep::More;
            while (step == Room::ReplayStep::More) {
                step = room->replay_step(slice, work_clock, why);
                ++steps;
                ASSERT_MSG(step != Room::ReplayStep::Refused, why);
                const uint32_t done = room->replay_progress() - before;
                before = room->replay_progress();
                ASSERT_MSG(done <= slice + net::kHashEveryTurns - 1, "a slice ran more than 20 turns past its time (slice " + std::to_string(slice) + ", " + std::to_string(done) + " turns)");
                if (step == Room::ReplayStep::More) ASSERT_MSG(done >= slice, "a slice stopped before its time (slice " + std::to_string(slice) + ", " + std::to_string(done) + " turns)");
                foreign += 1000;                                                           // (other rooms' work between the slices)
                ASSERT_TRUE(steps < 100000);
            }
            under_test = nullptr;
            ASSERT_MSG(room->replay_progress() == n, "every turn was read");
            ASSERT_MSG(room->replay_work_ms() == n, "the room's work is the sum of its slices, not the time between them (slice " + std::to_string(slice) + ")");
            if (slice == 1) ASSERT_TRUE(steps >= static_cast<int>(n / net::kHashEveryTurns));         // (a slice of 1 ms is 20 turns: as many steps as looks at the clock)
            if (slice == 20000) ASSERT_EQ(steps, 1);                                                  // (a slice longer than the replay is the replay in one go)
            rec.reset();
            ASSERT_TRUE(room->begin_restored(5000, why));
            ASSERT_EQ(room->status(5000).restore_ms, n);                                  // (the status says how long the room's own work was)
            ASSERT_MSG(picture(*room, 5000) == reference, picture(*room, 5000) + " != " + reference);
            room->update(5100);
            ASSERT_TRUE(room->state() == RoomState::Running);
        }
        // ---- a clock that stands still: a slice of 5 ms is never used up, the replay is done in one step -----------------------------------------------------------------------------------------------
        {
            const std::string code = copy_for_room();
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            auto room = make_room(rec, store);
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_TRUE(room->replay_step(5, []() { return 77u; }, why) == Room::ReplayStep::Replayed);
            ASSERT_EQ(room->replay_work_ms(), 0u);
        }
        // ---- the cap is on the sum of the room's own slices, to the millisecond (RestartConfig::replay_budget_ms; 10 ms for every turn here) -----------------------------------------------------
        for (const uint32_t slice : {1u, 100u, 5000u}) {
            const uint32_t total = last_boundary * 10;                                     // what the clock reads at the last look
            ASSERT_TRUE(total > 3000);
            for (const bool refuse : {false, true}) {
                RestartConfig capped = w.restart;
                capped.replay_budget_ms = refuse ? total - 1 : total;                      // (the cap is passed when the sum is MORE than it)
                RestartStore capped_store(capped);                                         // (not prepared: the first store holds the folder's lock, and this one only carries the cap)
                const std::string code = copy_for_room();
                RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
                auto room = make_room(rec, capped_store);
                under_test = room.get();
                per_turn = 10;
                foreign = 0;
                ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
                Room::ReplayStep step = Room::ReplayStep::More;
                int steps = 0;
                while (step == Room::ReplayStep::More && steps < 100000) {
                    step = room->replay_step(slice, work_clock, why);
                    foreign += 100000;                                                     // (an hour of other rooms' work in all: not the room's)
                    ++steps;
                }
                under_test = nullptr;
                per_turn = 1;
                ASSERT_MSG(step == (refuse ? Room::ReplayStep::Refused : Room::ReplayStep::Replayed), std::string("slice ") + std::to_string(slice) + (refuse ? " refused" : " not refused") + ": " + why);
                if (refuse) {
                    ASSERT_TRUE(why.find("longer than the " + std::to_string((total - 1) / 1000) + " s that a restore may") != std::string::npos);
                    ASSERT_MSG(room->replay_progress() == last_boundary, "the room is refused at the look that passes the cap, not later");
                }
            }
        }
        // ---- a checkpoint that the replay disagrees with refuses the room in the slice that meets it: the third checkpoint, turn 59, in the third slice of 1 ms -----------------------------------
        {
            const std::string code = copy_for_room();
            std::vector<uint8_t> bytes = read_all_bytes(w.record_path(code));
            const auto frames = frames_of(bytes);
            size_t checks_seen = 0;
            size_t tampered = 0;
            for (size_t i = 1; i < frames.size() && tampered == 0; ++i) {
                if (bytes[frames[i].first] == 3 && ++checks_seen == 3) tampered = i;
            }
            ASSERT_TRUE(tampered != 0);
            std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].first + 5), bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].second - 4));
            payload[8] = static_cast<uint8_t>(payload[8] ^ 1);
            const std::vector<uint8_t> frame = test_frame(3, payload);
            std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].first));
            out.insert(out.end(), frame.begin(), frame.end());
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].second), bytes.end());
            write_all_bytes(w.record_path(code), out);
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            ASSERT_TRUE(rec.ok());
            auto room = make_room(rec, store);
            under_test = room.get();
            foreign = 0;
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::More && room->replay_progress() == 20);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::More && room->replay_progress() == 40);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::Refused);
            ASSERT_TRUE(why.find("does not agree with the state hash") != std::string::npos && why.find("turn 59") != std::string::npos);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::Refused && why.find("no replay is under way") != std::string::npos);     // (a refused replay is over)
            under_test = nullptr;
            ASSERT_FALSE(room->begin_restored(5000, why));                                  // the room has not been replayed
        }
        // ---- bytes that are not what was judged (a record that was changed after it was read): the replay is refused where the reader fails, with the reader's reason ---------------------------------------
        {
            const std::string code = copy_for_room();
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            ASSERT_TRUE(rec.ok());
            const auto frames = frames_of(rec.bytes);
            size_t turn_frame = 0;
            size_t turn_frames_seen = 0;
            for (size_t i = 1; i < frames.size() && turn_frame == 0; ++i) {
                if (rec.bytes[frames[i].first] == 2 && ++turn_frames_seen == 30) turn_frame = i;
            }
            ASSERT_TRUE(turn_frame != 0);
            rec.bytes[frames[turn_frame].first + 5] = static_cast<uint8_t>(rec.bytes[frames[turn_frame].first + 5] + 1);       // the 30th turn says that it is the 31st
            auto room = make_room(rec, store);
            under_test = room.get();
            foreign = 0;
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            Room::ReplayStep step = Room::ReplayStep::More;
            while (step == Room::ReplayStep::More) step = room->replay_step(1, work_clock, why);
            under_test = nullptr;
            ASSERT_TRUE(step == Room::ReplayStep::Refused && why.find("could not be read again") != std::string::npos && why.find("not numbered") != std::string::npos);
            ASSERT_EQ(room->replay_progress(), 29u);                                       // (the 29 turns before the bad one were read)
        }
        // ---- what is not a replay under way is refused; a room that is thrown away half built gives its log back ----------------------------------------------------------------------------
        {
            const std::string code = copy_for_room();
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            auto room = make_room(rec, store);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::Refused && why.find("no replay is under way") != std::string::npos);        // (nothing was begun)
            RestartLoaded wrong = rec;
            wrong.head.code = "OTHER-ROOM";
            ASSERT_TRUE(room->begin_replay(wrong, 90000, why) == Room::ReplayBegin::Refused && why.find("not one that a record can be restored into") != std::string::npos);      // (another room's record)
            RestartLoaded refused_record = read_restart_record(w.record_path("RS-0"), 1ull << 30);
            refused_record.status = RestartLoaded::Status::Corrupt;
            ASSERT_TRUE(room->begin_replay(refused_record, 90000, why) == Room::ReplayBegin::Refused);                                                                // (a record that was not judged good)
            const uint64_t used_before = budget.used();
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Refused);                                                                           // (once)
            under_test = room.get();
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::More);
            under_test = nullptr;
            ASSERT_TRUE(budget.used() > used_before);                                       // the turns of the half-replayed match are in the room's log
            room.reset();                                                                   // thrown away in the middle
            ASSERT_EQ(budget.used(), used_before);                                          // ... and the memory is the server's again
        }
    } TEST_END();

    TEST_CASE("S3.106 The Restore Is Bounded, And It Is In Slices (M2 Of The Review, Phase 2): The Budgets Are A Slice Of Work For Every Pass Of The Server And A Cap Of Work For Every Room; Seven Copies Of One Record Are Judged And Queued At The Start And Restored Newest First, One Piece Of Work A Pass: Until Its Room Is Back Every Record Is On Disk As It Was, Every Code Is Taken (create_room Says 409) And /busy Counts The Match; One Room's Replay May Not Pass Its Own Cap (A Failed Room, Its Record Refused); A Stop Flag While The Records Are Judged Ends The Restore At Once And Leaves Every Record As It Was") {
        {   // the budgets of a server that restores
            const RestartConfig defaults;
            ASSERT_TRUE(defaults.replay_budget_ms == 20000 && defaults.restore_slice_ms == 5 && !defaults.clock_ms);
            ASSERT_EQ(ServerLimits().park_timeout_ms, 60000u);                                      // (a Hello waits a minute for its room, no more)
        }
        {   // seven copies, one piece of work a pass: the newest come back first, nothing is written to a record whose room has not begun, the codes are taken, /busy counts the matches
            PWorld w("persist-106a");
            w.start_server(500);
            const int rooms = 7;
            const auto code_of = [](int i) { return "CP-" + std::to_string(i); };
            crash_with_record_of(w, code_of(0), 20000, 3);
            const RestartLoaded original = w.read_record(code_of(0));
            ASSERT_TRUE(original.ok() && original.turn_count > 300);
            for (int i = 1; i < rooms; ++i) copy_record_as(w, code_of(0), code_of(i), std::chrono::seconds(0));
            for (int i = 0; i < rooms; ++i) age_record(w, code_of(i), 10 * (rooms - i));               // the last copy is the newest: not the order of the names
            const std::map<std::string, std::vector<uint8_t>> on_disk = folder_bytes(w);
            ASSERT_EQ(on_disk.size(), static_cast<size_t>(rooms));
            w.restart.restore_slice_ms = 0;                                                            // a pass does one piece of work (the making of a room, or 20 turns): the order can be seen
            w.limits.max_rooms = 8;                                                                    // (a queued record has its place: seven of the eight are theirs)
            w.start_server(500, false);
            ASSERT_TRUE(w.report.queued == static_cast<size_t>(rooms) && w.report.items.empty() && !w.report.stopped);
            ASSERT_TRUE(w.mgr->restoring_count() == static_cast<size_t>(rooms) && w.mgr->room_count() == 0);
            ASSERT_EQ(w.mgr->create_room(held_spec(code_of(0), 2), w.server_now()).http_status, 409);        // its code is taken
            ASSERT_EQ(w.mgr->create_room(held_spec(code_of(6), 2), w.server_now()).http_status, 409);
            ASSERT_TRUE(w.mgr->create_room(held_spec("FREE-1", 2), w.server_now()).ok);                      // the eighth place is free
            ASSERT_EQ(w.mgr->create_room(held_spec("FREE-2", 2), w.server_now()).http_status, 503);          // and then the server is full: the queue holds seven
            RoomStatus none;
            ASSERT_FALSE(w.mgr->status(code_of(3), none, w.server_now()));                                   // (a room that is not back is no room: the control interface does not know it)
            ASSERT_TRUE(w.mgr->busy(w.server_now()).matches == static_cast<uint32_t>(rooms) && w.mgr->busy(w.server_now()).players == 3u * static_cast<uint32_t>(rooms));
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || n.find("7 restart record(s) wait for their replay") != std::string::npos;
            ASSERT_TRUE(noted);
            for (int pass = 0; pass < 5; ++pass) w.mgr->update(w.server_now());                        // the first room is made and begins to replay: nothing is written to any record
            ASSERT_TRUE(w.mgr->restoring_count() == static_cast<size_t>(rooms) && folder_bytes(w) == on_disk);
            int passes = 5;
            while (w.mgr->restoring_count() > 0 && passes < 100000) {
                w.mgr->update(w.server_now());
                ++passes;
            }
            w.collect();
            ASSERT_TRUE(passes > rooms * 20 && passes < 100000);                                       // (a piece of work a pass: a room takes more than 20 of them)
            const RestoreReport& done = w.mgr->restore_report();
            ASSERT_EQ(done.items.size(), static_cast<size_t>(rooms));
            for (int i = 0; i < rooms; ++i) {                                                          // newest first: the order in which the rooms came back
                const RestoreItem& item = done.items[static_cast<size_t>(i)];
                ASSERT_MSG(item.code == code_of(rooms - 1 - i) && item.outcome == RestoreItem::Outcome::Restored && item.turns == original.turn_count, decided_in_order(done));
                ASSERT_MSG(item.replay_ms == w.status(item.code).restore_ms && item.note.find("replayed in " + std::to_string(item.replay_ms) + " ms") != std::string::npos, item.code);       // (the room's own work, as the status and the log say it)
            }
            for (int i = 0; i < rooms; ++i) {
                const RoomStatus s = w.status(code_of(i));
                ASSERT_MSG(s.state == RoomState::Running && s.restored && s.paused && s.absent.size() == 3 && s.record_kept, code_of(i));
            }
            ASSERT_TRUE(folder_bytes(w) == on_disk);                                                   // the rooms go on writing the same files: nothing is added while they are paused
            ASSERT_TRUE(w.mgr->busy(w.server_now()).matches == static_cast<uint32_t>(rooms) && w.mgr->busy(w.server_now()).players == 3u * static_cast<uint32_t>(rooms));
        }
        {   // the room's own cap: the work of its slices, summed, may not pass it (here by a clock that costs 100 ms a reading, so that a batch of 20 turns is 100 ms of work): refused as too slow, a failed room, the record refused
            PWorld w("persist-106b");
            w.start_server(500);
            crash_with_record_of(w, "CAP-1", 20000, 2);
            copy_record_as(w, "CAP-1", "CAP-2", std::chrono::seconds(10));
            FakeClock clock;
            w.restart.clock_ms = clock.as_function();
            w.restart.replay_budget_ms = 1000;                                                     // (a record of 399 turns is 19 looks at the clock: 1.9 s of work)
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Ended) == 2 && w.report.count(RestoreItem::Outcome::Restored) == 0 && w.report.count(RestoreItem::Outcome::Unreadable) == 0);
            for (const RestoreItem& i : w.report.items) ASSERT_TRUE(i.note.find("longer than the 1 s") != std::string::npos);
            ASSERT_TRUE(w.status("CAP-1").state == RoomState::Failed && w.status("CAP-1").reason.find("longer than the 1 s") != std::string::npos && w.record_files().empty());
        }
        {   // what the report, the status and the log say of a room's replay is its own work, the sum of its slices (a clock that costs 100 ms a reading: a real clock says 0 ms for a record this short)
            PWorld w("persist-106e");
            w.start_server(500);
            crash_with_record_of(w, "WK-1", 20000, 2);
            FakeClock clock;
            w.restart.clock_ms = clock.as_function();
            w.restart.restore_slice_ms = 0;                                                        // (every piece of work is a slice of its own: 19 of them for a record of 399 turns)
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.items.size() == 1);
            const RestoreItem& item = w.report.items[0];
            ASSERT_TRUE(item.replay_ms >= 1500 && item.replay_ms == w.status("WK-1").restore_ms);
            ASSERT_TRUE(item.note.find("replayed in " + std::to_string(item.replay_ms) + " ms") != std::string::npos);
        }
        {   // the server is told to stop while the records are judged: before the first one, between two; every record stays as it was and nothing is restored; the restart then restores all three
            PWorld w("persist-106d");
            w.start_server(500);
            crash_with_record_of(w, "SP-0", 20000, 2);
            for (int i = 1; i < 3; ++i) copy_record_as(w, "SP-0", "SP-" + std::to_string(i), std::chrono::seconds(10 * i));
            age_record(w, "SP-0", 1);
            const std::map<std::string, std::vector<uint8_t>> on_disk = folder_bytes(w);
            int polls = 0;
            const auto stop_at = [&polls](int n) { return [&polls, n]() { return ++polls >= n; }; };
            const struct { const char* what; int at; } cases[] = {
                {"before the first record", 1},
                {"between the first record and the second", 2},
            };
            for (const auto& c : cases) {
                polls = 0;
                w.restore_should_stop = stop_at(c.at);
                w.start_server(500, false);
                ASSERT_MSG(w.report.stopped && w.report.left_on_disk == 3 && w.report.items.empty() && w.report.queued == 0, c.what);
                ASSERT_MSG(w.mgr->room_count() == 0 && w.mgr->restoring_count() == 0, c.what);                    // nothing was restored, nothing is a failed room, nothing waits
                ASSERT_MSG(folder_bytes(w) == on_disk, c.what);                                                  // every record is as it was, byte for byte
                ASSERT_MSG(polls == c.at, c.what);                                                               // and the restore asked no more once it was told
                bool noted = false;
                for (const std::string& n : w.notices) noted = noted || n.find("restore was stopped") != std::string::npos;
                ASSERT_MSG(noted, c.what);
                ASSERT_MSG(w.mgr->shutdown(w.server_now()) == 0, c.what);
                w.stop_server(false);
            }
            w.restore_should_stop = nullptr;
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 3 && !w.report.stopped);
        }
    } TEST_END();

    TEST_CASE("S3.115 The Server Keeps Serving While A Long Record Restores (An Hour Of Turns, As No Match Is Played: A Real Match And Turns Added After It; The Busiest Map's Three Minutes Of Four Players; With ANTS_RESTORE_MEASURE_LONG Also The Longest Real Match: TREASURE, Four Players, 14,000 Turns): The Restore Is Spread Over The Passes Of The Server (A Piece Of Work A Pass, Or Slices Of 1 And Of 5 ms, Never In One); The Longest Pass Takes The Slice Plus A Margin (The Making Of A Room Reads Its Record Again And Makes Its Engine: The Margin), Not The Seconds That The Replay Takes; A Match That Is Made And Played Meanwhile Runs At Its Own Pace (Never Paused, Its Machines Never Lost) While The Record Is Still Queued; The Room Comes Back With Every Turn; /busy Counts The Match From The First Pass To The Last") {
        const bool heavy = std::getenv("ANTS_RESTORE_MEASURE_LONG") != nullptr;
        const uint32_t hour_turns = 3600u * net::kTurnsPerSecond;
        struct Kind {
            const char* map;
            uint8_t players;
            uint32_t play_ms;                // the match that is played before the server is lost
            uint32_t total_turns;            // the record is made as long as this with turns that nobody played (0: as it was played): no match is an hour long, so the match is over when it is brought back
            std::vector<uint32_t> slices;    // RestartConfig::restore_slice_ms to restore it with (0: a piece of work a pass: the same on every machine)
            bool opt_in;
        };
        const std::vector<Kind> kinds = {
            {"TINY.LVL", 3, 20000, hour_turns, {0}, false},                              // an hour of turns, a piece of work a pass: 3,600 passes of 20 turns, 36 s of the world's time (the engine does next to nothing after the end of a match)
            {"TREASURE.LVL", 4, 180000, 0, {1, 5}, false},                               // the busiest map, three minutes of four players' play (3,600 turns, a replay of about a second); the product's slice is 5 ms
            {"TREASURE.LVL", 4, 11u * 60000u + 20000u, 0, {5}, true},                    // the longest real match that the test world plays (about 14,000 turns); seconds of replay
        };
        int index = 0;
        for (const Kind& k : kinds) {
            ++index;
            if (k.opt_in && !heavy) continue;
            PWorld w(("restore-115-" + std::to_string(index)).c_str());
            w.restart.replay_budget_ms = 10u * 60u * 1000u;                                    // (a slow machine must not refuse the record as too slow: the cap is S3.114's business)
            w.start_server(500);
            RoomSpec spec = held_spec("LONG-1", k.players, k.map);
            spec.run_ms = 24u * 3600u * 1000u;
            for (RClient* p : play_room(w, spec, k.play_ms)) p->reconnects = false;
            w.stop_server(false);
            const uint32_t played = w.read_record("LONG-1").turn_count;
            ASSERT_TRUE(played > 300);
            if (k.total_turns > played) lengthen_record(w, "LONG-1", k.total_turns - played);
            const uint32_t turns = std::max(played, k.total_turns);
            const std::map<std::string, std::vector<uint8_t>> record_bytes_before = folder_bytes(w);
            ASSERT_EQ(record_bytes_before.size(), size_t{1});
            for (const uint32_t slice : k.slices) {
                w.restart.restore_slice_ms = slice;
                w.start_server(500, false);                                                    // (the record is judged and queued: the server is up)
                ASSERT_TRUE(w.report.queued == 1 && w.mgr->restoring_count() == 1 && w.mgr->room_count() == 0);
                // a match that is made while the record waits, and played by two machines
                ASSERT_TRUE(w.mgr->create_room(held_spec("LIVE-1", 2), w.server_now()).ok);
                RClient& ann = w.connect("Ann", "LIVE-1");
                RClient& bob = w.connect("Bob", "LIVE-1");
                w.longest_update_ms = 0;
                w.time_updates = true;
                const uint32_t world_began = w.now;
                const auto real_began = std::chrono::steady_clock::now();
                uint64_t passes = 0;
                bool busy_counted_all_along = true;
                while (w.mgr->restoring_count() > 0 && passes < 5000000) {
                    w.run(10);
                    ++passes;
                    busy_counted_all_along = busy_counted_all_along && w.mgr->busy(w.server_now()).matches >= 1;      // (the record is a match from the first pass to the last)
                }
                const double real_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - real_began).count();
                const uint32_t world_ms = w.now - world_began;
                const double longest_ms = w.longest_update_ms;
                w.time_updates = false;
                const std::string what = std::string(k.map) + " slice " + std::to_string(slice);
                ASSERT_MSG(w.mgr->restoring_count() == 0 && busy_counted_all_along, what);
                // the record is a room again, with every turn: running and holding its seats, or (a match that is longer than any match is) finished as the engine says
                const RoomStatus s = w.status("LONG-1");
                ASSERT_MSG(s.restored && s.restored_turns == turns && s.turns == turns, what);
                if (k.total_turns == 0) {
                    ASSERT_MSG(s.state == RoomState::Running && s.paused && s.absent.size() == k.players && s.record_kept, what + ": " + room_state_name(s.state) + " absent " + std::to_string(s.absent.size()) + " " + s.reason);
                    const std::map<std::string, std::vector<uint8_t>> after = folder_bytes(w);          // (the folder holds the record of the match that was made meanwhile too)
                    ASSERT_MSG(after.count(record_bytes_before.begin()->first) == 1 && after.at(record_bytes_before.begin()->first) == record_bytes_before.begin()->second, what + ": the record changed");
                } else {
                    ASSERT_MSG(s.state == RoomState::Finished && s.reason.find("had ended") != std::string::npos, what + ": " + room_state_name(s.state) + " " + s.reason);
                }
                ASSERT_TRUE(w.report.items.empty() && w.mgr->restore_report().items.size() == 1 && w.mgr->restore_report().items[0].outcome == RestoreItem::Outcome::Restored);
                // the match that was made meanwhile ran at its own pace: made, started, played, both machines there, while the record was queued
                const RoomStatus live = w.status("LIVE-1");
                ASSERT_MSG(live.state == RoomState::Running && !live.paused && !ann.lost && !bob.lost && ann.session != nullptr && bob.session != nullptr, what);
                ASSERT_MSG(passes > 10, "the restore was spread over the passes of the server: " + what);                  // (a replay of a second is more than 100 passes of 5 ms; a machine that is several times faster is still more than 10)
                if (slice == 0) ASSERT_MSG(passes >= turns / net::kHashEveryTurns && live.turns > 300, what);         // (a piece of work a pass: 3,600 passes, 36 s of the world's time, in which the match played its first 15 s and more)
                if (world_ms > kPre + 3000) ASSERT_MSG(live.turns + 2 >= (world_ms - kPre - 3000) / net::kTurnMs && live.turns <= world_ms / net::kTurnMs, what);      // (a turn for every 50 ms of the world's time since the match began: nothing stalled it)
#ifdef NDEBUG
                ASSERT_MSG(longest_ms < static_cast<double>(slice) + 60.0, "the longest pass took " + std::to_string(longest_ms) + " ms of CPU: " + what);       // the slice and a margin of 60 ms: the first piece of a room reads its record again and makes its engine
#endif
                std::cout << "\n      [restore in slices] " << what << " ms: a record of " << turns << " turns (" << turns / net::kTurnsPerSecond / 60 << " minutes of play, " << k.map << " with " << static_cast<int>(k.players)
                          << ") was brought back in " << std::fixed << std::setprecision(2) << real_seconds << " s over " << passes << " passes of the server; the longest pass took " << longest_ms
                          << " ms of CPU; a match of two machines was served all the while (" << live.turns << " turns in " << world_ms / 1000 << " s of the world's time)" << std::flush;
                ann.reconnects = false;
                bob.reconnects = false;
                w.mgr->close_room("LIVE-1", w.server_now());                                    // (its record goes: the next restore has the long record only)
                w.stop_server(false);
            }
        }
    } TEST_END();

    TEST_CASE("S3.116 A Hello For A Room That Waits For Its Replay Is Parked, Not Answered And Not Dropped, However Long The Door's Own Hello Timeout Is (A Minute Is The Limit Of A Parked One): When The Room Is Restored The Hello Goes Through The Door As Any Other Does: The Player's Key Gets The Rejoin Welcome And Catches Up To The State Of The Restored Room, A Stranger And A Wrong Key Are Told MatchRunning; A Hello For A Code That No Record Has Is Answered At Once (NoSuchRoom), And A New Room Is Served Meanwhile") {
        PWorld w("restore-116");
        w.limits.hello_timeout_ms = 1000;                                                     // (the door's hello timeout is shorter than the wait below: a parked Hello has none)
        w.restart.restore_slice_ms = 0;                                                       // (a piece of work a pass: the long record takes several seconds of the world's time)
        w.start_server(500);
        crash_with_record_of(w, "PK-1", 20000, 3);
        const RestartLoaded original = w.read_record("PK-1");
        lengthen_record(w, "PK-1", 5200 - original.turn_count);                               // 5,200 turns (the match of TINY ends at 5,599: the room must still run): 260 pieces of work, 2.6 s of the world's time
        w.start_server(500, false);
        const uint64_t refused_before = w.mgr->connections_refused();
        ASSERT_EQ(w.mgr->restoring_count(), size_t{1});
        // a player comes back with its key (a machine from nothing: a page that was reloaded), a stranger, a key that fits no seat, and a code that no record has
        RClient& back = w.connect(original.head.start.names[0], "PK-1", 0, original.head.keys[0]);
        net::Connection* stranger = say_hello(w, "PK-1");
        net::Connection* wrong = say_hello(w, "PK-1", test_key(9));
        net::Connection* unknown = say_hello(w, "NO-SUCH-ROOM");
        w.run(300);
        ASSERT_EQ(reject_on(unknown), static_cast<int>(net::RejectReason::NoSuchRoom));       // (a code that is no record's is no business of the queue: answered at once)
        ASSERT_EQ(w.mgr->parked_count(), size_t{3});
        // a room that is made now is served at once: its Hello is answered while the record waits
        ASSERT_TRUE(w.mgr->create_room(held_spec("NEW-1", 2), w.server_now()).ok);
        RClient& newcomer = w.connect("Nia", "NEW-1");
        w.run(500);
        ASSERT_TRUE(w.status("NEW-1").joined == 1 && newcomer.lobby != nullptr && !newcomer.lost && w.mgr->restoring_count() == 1);
        // the door's hello timeout (1 s) is over: the parked Hellos are still there, unanswered
        w.run(1200);
        ASSERT_TRUE(w.mgr->parked_count() == 3 && w.mgr->restoring_count() == 1 && w.mgr->connections_refused() == refused_before + 1);       // (only the unknown code was turned away)
        ASSERT_TRUE(back.session == nullptr && !back.lost && !back.was_rejected && reject_on(stranger) == 0 && reject_on(wrong) == 0);
        // the room is restored: the Hellos go through the door
        ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 30000));
        ASSERT_TRUE(w.until([&]() { return w.status("PK-1").rejoins == 1; }, 30000));
        ASSERT_TRUE(w.mgr->parked_count() == 0 && w.report.queued == 1);
        const RoomStatus s = w.status("PK-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && s.rejoins == 1 && s.absent.size() == 2 && s.turns == 5200u);
        ASSERT_TRUE(back.session != nullptr && !back.lost && !back.was_rejected);
        ASSERT_EQ(back.sim.state_hash().total, s.restored_hash);                              // the catch-up through a room that was restored in slices ends at the restored state
        ASSERT_EQ(reject_on(stranger), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(reject_on(wrong), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_FALSE(newcomer.lost);
    } TEST_END();

    TEST_CASE("S3.117 A Room For Which A Hello Waits Is Restored First, Then The Newest Record (An Older Room With A Parked Hello Before Newer Rooms Without One; A Hello That Comes While A Newer Room's Replay Is Under Way Takes Over, And The Newer Room Goes On Afterwards And Is Restored Whole)") {
        PWorld w("restore-117");
        w.restart.restore_slice_ms = 0;                                                       // (a piece of work a pass: a room takes about a second of the world's time)
        w.start_server(500);
        crash_with_record_of(w, "OR-A", 20000, 2);
        const RestartLoaded original = w.read_record("OR-A");
        copy_record_as(w, "OR-A", "OR-B", std::chrono::seconds(0));
        copy_record_as(w, "OR-A", "OR-C", std::chrono::seconds(0));
        for (const char* code : {"OR-A", "OR-B", "OR-C"}) lengthen_record(w, code, 2000);
        age_record(w, "OR-A", 30);                                                             // A is the oldest, C the newest: the queue's order is C, B, A
        age_record(w, "OR-B", 20);
        age_record(w, "OR-C", 10);
        w.start_server(500, false);
        ASSERT_EQ(w.mgr->restoring_count(), size_t{3});
        RClient& for_a = w.connect(original.head.start.names[0], "OR-A", 0, original.head.keys[0]);       // A waits for a player from the first pass on
        ASSERT_TRUE(w.until([&]() { return w.mgr->parked_count() == 1; }, 1000));
        ASSERT_TRUE(w.until([&]() { return w.mgr->restore_report().items.size() == 1; }, 30000));
        ASSERT_EQ(decided_in_order(w.mgr->restore_report()), std::string("OR-A"));            // the oldest record came back first: the player of its room was waiting
        // C is under way now (the next in the queue's order); a player of B, the last, comes: B takes over, C is half done and goes on afterwards
        w.run(150);
        ASSERT_TRUE(w.mgr->restoring_count() == 2 && w.mgr->restore_report().items.size() == 1);
        RClient& for_b = w.connect(original.head.start.names[0], "OR-B", 0, original.head.keys[0]);
        ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 60000));
        ASSERT_EQ(decided_in_order(w.mgr->restore_report()), std::string("OR-A OR-B OR-C"));
        for (const char* code : {"OR-A", "OR-B", "OR-C"}) {
            const RoomStatus s = w.status(code);
            ASSERT_MSG(s.state == RoomState::Running && s.restored && s.paused && s.restored_turns == original.turn_count + 2000 && s.absent.size() >= 1, code);       // (C, the room that was half done, is whole)
        }
        ASSERT_EQ(w.status("OR-C").absent.size(), size_t{2});                                   // (nobody came for C: both seats are held)
        ASSERT_TRUE(w.until([&]() { return w.status("OR-A").rejoins == 1 && w.status("OR-B").rejoins == 1; }, 30000));
        ASSERT_TRUE(for_a.session != nullptr && for_b.session != nullptr && !for_a.lost && !for_b.lost);
        ASSERT_EQ(for_a.sim.state_hash().total, w.status("OR-A").restored_hash);
        ASSERT_EQ(for_b.sim.state_hash().total, w.status("OR-B").restored_hash);
    } TEST_END();

    TEST_CASE("S3.118 What Waits For A Room Is Bounded And Does Not Pile Up: A Room Takes As Many Parked Connections As It Would Keep (The Fourth Hello For A Room Of Three Is Dropped Without An Answer, Not Told Full: A Reject Is Final For A Client, Whose Key Goes With It) And The Door As Many As It Lets Wait For A Hello (max_pending: The Next Link Is Dropped Without An Answer As Well); A Parked Connection That Closes Is Dropped At Once; One That Sends More Than A Client Does While It Waits Is Dropped As A Flood; One That Has Waited For park_timeout_ms Is Dropped (The Limit Is A Minute); What The Peer Sends After Its Hello Is Kept And Reaches The Room: A Ping That Was Sent While The Hello Waited Is Answered When The Room Is Back") {
        {   // the bounds and the drops
            PWorld w("restore-118");
            w.limits.park_timeout_ms = 3000;                                                  // (a minute by default: asserted in S3.106)
            w.limits.max_pending = 5;
            w.restart.restore_slice_ms = 0;
            w.start_server(500);
            crash_with_record_of(w, "BD-1", 20000, 3);
            rewrite_head(w, "BD-1", [](RestartHead& h) { h.max_connections = 3; });           // (a room that keeps three connections at most)
            copy_record_as(w, "BD-1", "BD-2", std::chrono::seconds(10));
            for (const char* code : {"BD-1", "BD-2"}) lengthen_record(w, code, 20000);        // (a thousand pieces of work each: ten seconds of the world's time)
            w.start_server(500, false);
            const uint64_t refused_before = w.mgr->connections_refused();
            std::vector<net::Connection*> a;
            for (int i = 0; i < 4; ++i) a.push_back(say_hello(w, "BD-1"));
            w.run(300);
            ASSERT_EQ(w.mgr->parked_count(), size_t{3});                                      // the room's own bound: the fourth link is dropped without an answer (a Full is final for a client: its key goes), and counted as refused
            ASSERT_TRUE(!a[3]->is_open() && reject_on(a[3]) == 0);
            ASSERT_EQ(w.mgr->connections_refused(), refused_before + 1);
            std::vector<net::Connection*> b;
            for (int i = 0; i < 3; ++i) b.push_back(say_hello(w, "BD-2"));
            w.run(300);
            ASSERT_EQ(w.mgr->parked_count(), size_t{5});                                      // the door's bound (5): the next link is dropped without an answer (a Reject is final for a client: it tries again)
            ASSERT_TRUE(!b[2]->is_open() && reject_on(b[2]) == 0);
            ASSERT_EQ(w.mgr->connections_refused(), refused_before + 2);
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(a[static_cast<size_t>(i)]->is_open() && reject_on(a[static_cast<size_t>(i)]) == 0);
            // a connection that closes is dropped at once, and its place is free
            a[0]->close();
            w.run(100);
            ASSERT_EQ(w.mgr->parked_count(), size_t{4});
            net::Connection* again = say_hello(w, "BD-1");
            w.run(300);
            ASSERT_EQ(w.mgr->parked_count(), size_t{5});
            // a flood after the Hello (a client that waits for its Welcome sends nothing: ten messages are a flood): dropped, closed
            ASSERT_TRUE(a[1]->is_open());
            net::PingMsg ping;
            ping.nonce = 1;
            for (int i = 0; i < 10; ++i) a[1]->send(net::encode_ping(ping));
            w.run(300);
            ASSERT_TRUE(w.mgr->parked_count() == 4 && !a[1]->is_open());
            // what has waited for park_timeout_ms (3 s here) is dropped, counted from the moment it was parked and not before (the oldest has waited 2.8 s after the first run): every one of them, with no answer
            w.run(1500);
            ASSERT_EQ(w.mgr->parked_count(), size_t{4});
            w.run(1500);
            ASSERT_EQ(w.mgr->parked_count(), size_t{0});
            for (net::Connection* link : {a[2], b[0], b[1], again}) ASSERT_TRUE(!link->is_open() && reject_on(link) == 0);
            ASSERT_TRUE(w.mgr->restoring_count() == 2 && w.mgr->pending_count() == 0);
        }
        {   // what the peer sends after its Hello reaches the room: a Ping that waited with the Hello is answered when the room is back
            PWorld w("restore-118b");
            w.restart.restore_slice_ms = 0;
            w.start_server(500);
            crash_with_record_of(w, "BE-1", 20000, 3);
            const RestartLoaded original = w.read_record("BE-1");
            lengthen_record(w, "BE-1", 2000);                                                  // (a second of the world's time at a piece of work a pass)
            w.start_server(500, false);
            net::Connection* link = say_hello(w, "BE-1", original.head.keys[1]);
            net::PingMsg ping;
            ping.nonce = 4242;
            ping.sent_ms = 7;
            link->send(net::encode_ping(ping));
            w.run(300);
            ASSERT_TRUE(w.mgr->parked_count() == 1 && w.mgr->restoring_count() == 1);
            ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 30000));
            bool welcome = false;
            bool pong = false;
            for (int round = 0; round < 300 && !pong; ++round) {
                w.run(10);
                std::vector<uint8_t> msg;
                while (link->poll(msg)) {
                    welcome = welcome || net::peek_type(msg) == net::MsgType::Welcome;
                    net::PingMsg got;
                    if (net::peek_type(msg) == net::MsgType::Pong && net::decode_ping(msg.data(), msg.size(), got) && got.nonce == 4242) pong = true;
                }
            }
            ASSERT_TRUE(welcome && pong && w.mgr->parked_count() == 0);
        }
    } TEST_END();

    TEST_CASE("S3.131 A Public Room Is Still A Public Room After A Restart (Protocol 15): Its Record Says So, The Restored Room Counts Against The Limit Of Public Rooms Again, And The Room Of The Control Interface Next To It Is Not One") {
        ServerLimits limits;
        limits.demo_rooms = 1;                                                                  // (one public room at a time)
        limits.demo_map = "TINY.LVL";
        PWorld w("restart-131", limits);
        w.start_server(500);
        RClient& ann = w.connect_creating("Ann", "pubaaaaa", block_of("", 2));                  // a visitor's room: its first Hello brings the block
        RClient& bob = w.connect("Bob", "pubaaaaa");
        ann.record_hashes = true;
        bob.record_hashes = true;
        const std::vector<RClient*> control = play_room(w, held_spec("CTRL-131", 2), 6000);      // a room of the control interface, running beside it
        w.run(6000 + kPre);
        RoomStatus s = w.status("pubaaaaa");
        ASSERT_TRUE(s.state == RoomState::Running && s.public_room && s.record_kept && s.joined == 2);
        ASSERT_TRUE(w.status("CTRL-131").state == RoomState::Running && !w.status("CTRL-131").public_room);
        ASSERT_TRUE(w.read_record("pubaaaaa").head.public_room && !w.read_record("CTRL-131").head.public_room);          // what the record says
        for (RClient* p : control) p->reconnects = false;
        ann.reconnects = false;
        bob.reconnects = false;
        w.stop_server(false);                                                                   // the server dies
        w.start_server(500);
        s = w.status("pubaaaaa");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.public_room);              // the room that comes back is a public one ...
        ASSERT_TRUE(w.status("CTRL-131").state == RoomState::Running && w.status("CTRL-131").restored && !w.status("CTRL-131").public_room);          // ... and the other is not
        RClient& cat = w.connect_creating("Cat", "pubbbbbb", block_of("", 2));                  // ... so the one public place is taken: another visitor's block makes no room
        w.run(300);
        ASSERT_TRUE(cat.was_rejected && cat.rejected == net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(w.status("pubbbbbb").code.empty() && w.mgr->room_count() == 2);
    } TEST_END();

    TEST_CASE("S3.119 A Record That Is Refused In Its Replay Becomes The Failed Room, And Its Parked Hellos Are Told NoSuchRoom (The Replay Disagrees With A Checkpoint In A Slice Of Its Own): The Record Is Refused (Kept For A Day), The Room's Code Is Taken Until Its Keep Time Is Over; A Public Room's Failed Room Is Replaced By A New Room At The Hello That Carries A Block, As It Always Was, And The Parked Hello Joins That Room; A Record That Cannot Be Read When Its Turn Comes (It Was Changed While It Waited), And A Map That Changed Meanwhile, Are Refused With Their Reasons And Release The Hellos That Waited") {
        ServerLimits limits;
        limits.demo_rooms = 1;                                                                  // (one public room at a time: a queued record of a public room is one)
        limits.demo_map = "TINY.LVL";
        {   // a replay that disagrees with a checkpoint: a failed room, NoSuchRoom for the Hellos that waited; a public room's code makes a new room for the Hello that carries a block
            PWorld w("restore-119", limits);
            w.start_server(500);
            crash_with_record_of(w, "RF-1", 20000, 2);
            const RestartLoaded original = w.read_record("RF-1");
            lengthen_record(w, "RF-1", 2000);                                                  // (RF-2, the good one, takes a second of the world's time at a piece of work a pass; the tampered ones are refused at turn 59)
            copy_record_as(w, "RF-1", "pubrf001", std::chrono::seconds(0));
            rewrite_head(w, "pubrf001", [](RestartHead& h) { h.public_room = true; });         // (the record of a room that a visitor's block made)
            copy_record_as(w, "RF-1", "RF-2", std::chrono::seconds(0));
            tamper_checkpoint(w, "RF-1", 3);
            tamper_checkpoint(w, "pubrf001", 3);
            age_record(w, "RF-2", 10);                                                          // the queue's order: RF-2, RF-1, the public room's
            age_record(w, "RF-1", 20);
            age_record(w, "pubrf001", 30);
            w.restart.restore_slice_ms = 0;
            w.start_server(500, false);
            ASSERT_EQ(w.mgr->restoring_count(), size_t{3});
            net::Connection* keyed = say_hello(w, "RF-1", original.head.keys[0]);
            net::Connection* stranger = say_hello(w, "RF-1");
            RClient& dee = w.connect_creating("Dee", "pubrf001", block_of("", 2));              // (a public room's code, a block: the player comes without a key, as a page does)
            RClient& back = w.connect(original.head.start.names[0], "RF-2", 0, original.head.keys[0]);       // (a record that is good: its player is served)
            RClient& other_new = w.connect_creating("Eli", "pubother", block_of("", 2));       // (another code with a block: the queued record of a public room counts for the limit of one, so no room is made for it)
            w.run(300);
            ASSERT_EQ(w.mgr->parked_count(), size_t{4});
            ASSERT_TRUE(other_new.was_rejected && other_new.rejected == net::RejectReason::NoSuchRoom);
            ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 60000));
            ASSERT_EQ(w.report.queued, size_t{3});
            const RestoreReport& done = w.mgr->restore_report();
            ASSERT_TRUE(done.count(RestoreItem::Outcome::Ended) == 2 && done.count(RestoreItem::Outcome::Restored) == 1);
            for (const RestoreItem& i : done.items) {
                if (i.outcome == RestoreItem::Outcome::Ended) ASSERT_TRUE(i.note.find("does not agree with the state hash") != std::string::npos && i.note.find("turn 59") != std::string::npos);
            }
            ASSERT_TRUE(w.status("RF-1").state == RoomState::Failed && w.status("RF-1").reason.find("turn 59") != std::string::npos);
            ASSERT_TRUE(w.status("RF-2").state == RoomState::Running && w.status("RF-2").restored);
            w.run(300);
            ASSERT_EQ(reject_on(keyed), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_EQ(reject_on(stranger), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_TRUE(w.mgr->create_room(held_spec("RF-1", 2), w.server_now()).http_status == 409);         // the failed room has its code until its keep time is over
            ASSERT_TRUE(w.record_files().size() == 1 && fs::exists(w.record_path("RF-2")));                    // (the refused records are put by in refused/)
            // the public room's code: the failed room is replaced by a new room, and the Hello that waited (it carries the block) is in it
            ASSERT_TRUE(w.until([&]() { return w.status("pubrf001").state == RoomState::Waiting && w.status("pubrf001").joined == 1; }, 5000));
            ASSERT_TRUE(!dee.lost && !dee.was_rejected && dee.lobby != nullptr);
            ASSERT_TRUE(w.until([&]() { return w.status("RF-2").rejoins == 1; }, 30000));
            ASSERT_TRUE(back.session != nullptr && !back.lost);
            const std::vector<RoomStatus> ended = w.mgr->take_ended(w.server_now());              // the ends of the refused rooms are reported as any room's (the log line and the result file)
            bool reported_rf1 = false;
            for (const RoomStatus& e : ended) reported_rf1 = reported_rf1 || (e.code == "RF-1" && e.state == RoomState::Failed);
            ASSERT_TRUE(ended.size() >= 2 && reported_rf1);
        }
        {   // a record that was changed while it waited: it cannot be read when its turn comes: no room, the Hello that waited is told NoSuchRoom, the file is gone, the log says why
            PWorld w("restore-119b");
            w.start_server(500);
            crash_with_record_of(w, "CH-1", 20000, 2);
            const RestartLoaded original = w.read_record("CH-1");
            w.restart.restore_slice_ms = 0;
            w.start_server(500, false);
            ASSERT_EQ(w.mgr->restoring_count(), size_t{1});
            net::Connection* link = say_hello(w, "CH-1", original.head.keys[0]);
            std::vector<uint8_t> garbage(300, 0x5A);
            write_all_bytes(w.record_path("CH-1"), garbage);                                    // (somebody changed the file)
            ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 10000));
            w.collect();
            ASSERT_TRUE(w.mgr->restore_report().items.size() == 1 && w.mgr->restore_report().items[0].outcome == RestoreItem::Outcome::Unreadable);
            RoomStatus none;
            ASSERT_FALSE(w.mgr->status("CH-1", none, w.server_now()));
            w.run(300);
            ASSERT_EQ(reject_on(link), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_TRUE(w.record_files().empty() && w.mgr->parked_count() == 0);
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("room-CH-1") != std::string::npos && n.find("was not restored") != std::string::npos);
            ASSERT_TRUE(noted);
            ASSERT_TRUE(w.mgr->create_room(held_spec("CH-1", 2), w.server_now()).ok);              // (the code is free again)
        }
        {   // a record that was swapped for another room's record while it waited: it can be read, and it is not the one that was judged: no room, the file is put by (refused/), the Hello that waited is told NoSuchRoom
            PWorld w("restore-119d");
            w.start_server(500);
            crash_with_record_of(w, "CS-1", 20000, 2);
            const RestartLoaded original = w.read_record("CS-1");
            lengthen_record(w, "CS-1", 2000);
            copy_record_as(w, "CS-1", "CS-2", std::chrono::seconds(0));
            age_record(w, "CS-1", 10);                                                          // the queue's order: CS-1, CS-2
            age_record(w, "CS-2", 20);
            w.restart.restore_slice_ms = 0;
            w.start_server(500, false);
            ASSERT_EQ(w.mgr->restoring_count(), size_t{2});
            net::Connection* link = say_hello(w, "CS-2", original.head.keys[0]);
            write_all_bytes(w.record_path("CS-2"), read_all_bytes(w.record_path("CS-1")));      // (CS-2's file now holds CS-1's record)
            ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 20000));
            w.collect();
            const RestoreReport& done = w.mgr->restore_report();
            ASSERT_TRUE(done.items.size() == 2 && done.items[0].code == "CS-2" && done.items[0].outcome == RestoreItem::Outcome::Unreadable && done.items[0].note.find("changed since it was judged") != std::string::npos);
            ASSERT_TRUE(done.items[1].code == "CS-1" && done.items[1].outcome == RestoreItem::Outcome::Restored);
            RoomStatus none;
            ASSERT_FALSE(w.mgr->status("CS-2", none, w.server_now()));
            w.run(300);
            ASSERT_EQ(reject_on(link), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_TRUE(fs::exists(fs::path(w.restart.dir) / "refused" / fs::path(w.record_path("CS-2")).filename()));
            ASSERT_TRUE(w.record_files().size() == 1 && fs::exists(w.record_path("CS-1")));
        }
        {   // a record that got more turns while it waited (the same room's file, good all through, and not the record that was judged: the turns that were counted are not these): refused as changed, the Hello that waited is told NoSuchRoom
            PWorld w("restore-119e");
            w.start_server(500);
            crash_with_record_of(w, "LG-1", 20000, 2);
            const RestartLoaded original = w.read_record("LG-1");
            w.restart.restore_slice_ms = 0;
            w.start_server(500, false);
            ASSERT_EQ(w.mgr->restoring_count(), size_t{1});
            net::Connection* link = say_hello(w, "LG-1", original.head.keys[0]);
            lengthen_record(w, "LG-1", 100);
            ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 10000));
            w.collect();
            ASSERT_TRUE(w.mgr->restore_report().items.size() == 1 && w.mgr->restore_report().items[0].outcome == RestoreItem::Outcome::Unreadable && w.mgr->restore_report().items[0].note.find("changed since it was judged") != std::string::npos);
            RoomStatus none;
            ASSERT_FALSE(w.mgr->status("LG-1", none, w.server_now()));
            w.run(300);
            ASSERT_EQ(reject_on(link), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_TRUE(w.record_files().empty() && fs::exists(fs::path(w.restart.dir) / "refused" / fs::path(w.record_path("LG-1")).filename()));
        }
        {   // a map that changed while the record waited: the room is refused when its turn comes, with the reason (a failed room), the record is put by, the Hello that waited is told NoSuchRoom; the room that was made before the change is restored
            const std::string maps_copy = (fs::path(temp_dir_for("restore-119c-maps")) / "Maps").string();
            fs::create_directories(maps_copy);
            fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(maps_copy) / "TINY.LVL", fs::copy_options::overwrite_existing);
            PWorld w("restore-119c", ServerLimits(), maps_copy);
            w.start_server(500);
            crash_with_record_of(w, "MP-1", 20000, 2);
            const RestartLoaded original = w.read_record("MP-1");
            lengthen_record(w, "MP-1", 2000);
            copy_record_as(w, "MP-1", "MP-2", std::chrono::seconds(0));
            age_record(w, "MP-1", 10);                                                          // the queue's order: MP-1, MP-2
            age_record(w, "MP-2", 20);
            w.restart.restore_slice_ms = 0;
            w.start_server(500, false);
            ASSERT_EQ(w.mgr->restoring_count(), size_t{2});
            w.run(100);                                                                         // MP-1 is made and begins to replay
            std::ofstream(fs::path(maps_copy) / "TINY.LVL", std::ios::binary | std::ios::app).put('x');                  // (the map file changes after the records were judged)
            net::Connection* link = say_hello(w, "MP-2", original.head.keys[0]);               // a player of MP-2 comes: MP-2 is next, and its map is not the match's
            w.run(100);
            ASSERT_TRUE(w.status("MP-2").state == RoomState::Failed && w.mgr->restoring_count() == 1 && w.mgr->parked_count() == 0);       // (MP-2 was next because its player waited, and was refused while MP-1 is still under way)
            ASSERT_TRUE(w.until([&]() { return w.mgr->restoring_count() == 0; }, 20000));
            const RoomStatus s = w.status("MP-2");
            ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("not the one that the match was played on") != std::string::npos);
            ASSERT_TRUE(w.status("MP-1").state == RoomState::Running && w.status("MP-1").restored);
            w.run(300);
            ASSERT_EQ(reject_on(link), static_cast<int>(net::RejectReason::NoSuchRoom));
            ASSERT_TRUE(w.record_files().size() == 1 && fs::exists(w.record_path("MP-1")) && w.mgr->parked_count() == 0);
        }
    } TEST_END();

    TEST_CASE("S3.120 /busy Counts A Record That Waits For Its Replay As A Match That A Restart Brought Back, And Its Persons As People (A Bot Is None), From The Start Of The Restore To Its End Without A Gap (A Second Restart In The Middle Of A Restore Must Not Be Let In By It): The Numbers Are The Same At Every Pass While Rooms Go From The Queue To The Server; A Record That Is Refused In Its Replay Stops Counting When It Is Refused; Five Minutes After The Restore Nobody Counts") {
        PWorld w("restore-120");
        w.start_server(500);
        std::vector<RClient*> three = play_room(w, held_spec("Q-1", 3), 15000);                // three persons
        RoomSpec with_bot = held_spec("Q-2", 3);                                                // two persons and a bot
        with_bot.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Easy});
        ASSERT_TRUE(w.mgr->create_room(with_bot, w.server_now()).ok);
        RClient& pa = w.connect("Pa", "Q-2");
        RClient& pb = w.connect("Pb", "Q-2");
        ASSERT_TRUE(w.until([&]() { return w.status("Q-2").state == RoomState::Running; }, 20000));
        w.run(15000);
        for (RClient* p : three) p->reconnects = false;
        pa.reconnects = false;
        pb.reconnects = false;
        w.stop_server(false);
        copy_record_as(w, "Q-1", "Q-3", std::chrono::seconds(0));                              // a third match: three persons, and a replay that will disagree with its third checkpoint
        tamper_checkpoint(w, "Q-3", 3);
        age_record(w, "Q-1", 10);                                                               // the newest first: Q-1, Q-2, Q-3
        age_record(w, "Q-2", 20);
        age_record(w, "Q-3", 30);
        w.restart.restore_slice_ms = 0;                                                         // (a piece of work a pass: the three rooms take a while, in passes)
        w.start_server(500, false);
        const auto busy = [&](uint32_t after_ms = 0) { return w.mgr->busy(w.server_now() + after_ms); };
        ASSERT_EQ(w.mgr->restoring_count(), size_t{3});
        ASSERT_TRUE(busy().matches == 3 && busy().players == 8);                               // 3 + 2 + 3 persons: the bot of Q-2 is no person
        int passes = 0;
        bool never_a_gap = true;
        bool q1_was_back_while_others_waited = false;
        while (w.mgr->restoring_count() > 0 && passes < 100000) {
            const BusyCounts b = busy();
            never_a_gap = never_a_gap && b.matches == 3 && b.players == 8;                     // (a room that comes back counts as a restored room at once: queue or server, the numbers are the same)
            RoomStatus s;
            q1_was_back_while_others_waited = q1_was_back_while_others_waited || (w.mgr->restoring_count() == 2 && w.mgr->status("Q-1", s, w.server_now()));
            w.mgr->update(w.server_now());
            ++passes;
        }
        ASSERT_TRUE(never_a_gap && q1_was_back_while_others_waited && passes < 100000);
        w.collect();
        const RestoreReport& done = w.mgr->restore_report();
        ASSERT_TRUE(done.count(RestoreItem::Outcome::Restored) == 2 && done.count(RestoreItem::Outcome::Ended) == 1);
        ASSERT_TRUE(w.status("Q-3").state == RoomState::Failed && w.status("Q-1").restored && w.status("Q-2").restored);
        ASSERT_TRUE(busy().matches == 2 && busy().players == 5);                               // Q-3 was refused: it counts nothing; the others count inside the window
        ASSERT_TRUE(busy(kRestoredBusyWindowMs - 1).matches == 2 && busy(kRestoredBusyWindowMs - 1).players == 5);
        ASSERT_TRUE(busy(kRestoredBusyWindowMs).matches == 0 && busy(kRestoredBusyWindowMs).players == 0);      // nobody came back: five minutes after the restore they hold no deploy
    } TEST_END();

    TEST_CASE("S3.121 A Stop While Records Wait Or Are Half Replayed Leaves Every Record As It Was, Byte For Byte: The Server That Is Told To Stop (shutdown) At The Start Of The Restore, In The Middle Of The First Replay, And With One Room Back And The Next Half Replayed Writes Nothing For The Rooms That Are Not Back (No Record Is Opened Again, None Is Cut, None Is Deleted), Says How Many Records It Left, Keeps The Record Of The Room That Is Back, And The Next Start Restores All; The Memory Of The Half-Built Rooms Is Given Back") {
        PWorld w("restore-121");
        w.start_server(500);
        crash_with_record_of(w, "G-0", 20000, 2);
        for (int i = 1; i < 3; ++i) copy_record_as(w, "G-0", "G-" + std::to_string(i), std::chrono::seconds(0));
        for (const char* code : {"G-0", "G-1", "G-2"}) lengthen_record(w, code, 4000);          // 200 more pieces of work for each
        age_record(w, "G-0", 10);                                                               // the newest first: G-0, G-1, G-2
        age_record(w, "G-1", 20);
        age_record(w, "G-2", 30);
        const std::map<std::string, std::vector<uint8_t>> on_disk = folder_bytes(w);
        ASSERT_EQ(on_disk.size(), size_t{3});
        w.restart.restore_slice_ms = 0;
        const struct {
            const char* what;
            int passes;                     // how many passes of the server before it is told to stop
            size_t back;                    // the rooms that are back by then
        } cases[] = {
            {"at the start, before any pass", 0, 0},
            {"in the middle of the first replay", 40, 0},
            {"with one room back and the next half replayed", 240, 1},
        };
        for (const auto& c : cases) {
            w.start_server(500, false);
            for (int pass = 0; pass < c.passes; ++pass) w.mgr->update(w.server_now());
            ASSERT_MSG(w.mgr->restore_report().items.size() == c.back && w.mgr->restoring_count() == 3 - c.back, c.what);
            const uint64_t logs_in_use = w.mgr->log_bytes();
            ASSERT_MSG(c.passes == 0 || logs_in_use > 0, c.what);                              // (the half-built rooms hold turn logs)
            const size_t kept = w.mgr->shutdown(w.server_now());                               // SIGTERM: the rooms that run make their records durable and leave them, the queue is dropped
            ASSERT_MSG(kept == c.back && w.mgr->restoring_count() == 0 && (c.back > 0 || w.mgr->log_bytes() == 0), c.what);       // (the memory of the half-built rooms is the server's again)
            w.stop_server(false);
            ASSERT_MSG(folder_bytes(w) == on_disk, c.what);                                    // every record is as it was, byte for byte: nothing was written, cut or deleted for the rooms that were not back
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || n.find("the restore was stopped by the server's stop: " + std::to_string(3 - c.back) + " restart record(s) are left on disk as they were") != std::string::npos;
            ASSERT_MSG(noted, c.what);
            w.notices.clear();
        }
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 3 && !w.report.stopped && w.mgr->restoring_count() == 0);
        for (const char* code : {"G-0", "G-1", "G-2"}) ASSERT_TRUE(w.status(code).restored && w.status(code).restored_turns == w.read_record(code).turn_count);
        {   // the Hellos that wait for a room that is not back are let go with the stop (the server leaves: its links close), and the records are as they were
            PWorld p("restore-121b");
            p.start_server(500);
            crash_with_record_of(p, "H-0", 20000, 2);
            lengthen_record(p, "H-0", 4000);
            const std::map<std::string, std::vector<uint8_t>> before = folder_bytes(p);
            p.restart.restore_slice_ms = 0;
            p.start_server(500, false);
            net::Connection* link = say_hello(p, "H-0");
            p.run(50);
            ASSERT_TRUE(p.mgr->parked_count() == 1 && p.mgr->restoring_count() == 1);
            ASSERT_EQ(p.mgr->shutdown(p.server_now()), size_t{0});
            ASSERT_TRUE(p.mgr->parked_count() == 0 && p.mgr->restoring_count() == 0);
            p.stop_server(false);
            ASSERT_TRUE(folder_bytes(p) == before && reject_on(link) == 0);                      // (no answer: the link was let go, not rejected)
        }
    } TEST_END();

    TEST_CASE("S3.122A Room's Clocks Start When ITS Replay Ends, Not When The Restore Began And Not When Another Room's Replay Ended: Two Rooms Whose Replays End Ten Seconds Apart Hold Their Seats For As Long As Each Has Been Back (The Seconds Of Absence Of Each Are The Seconds Since Its Own Restore), The Vote About An Absent Seat Opens 90 s After A Room's OWN Restore (The Earlier Room's Is Open When The Later Room's Is Not Yet), And The Wall-Clock Limit Counts The Play And Not The Wait For The Replays Of The Others") {
        PWorld w("restore-122");
        w.start_server(500);
        crash_with_record_of(w, "CK-1", 20000, 2);
        copy_record_as(w, "CK-1", "CK-2", std::chrono::seconds(0));
        for (const char* code : {"CK-1", "CK-2"}) lengthen_record(w, code, 4000);              // (200 pieces of work, 2 s of the world's time a room; the world's pass is 10 ms)
        age_record(w, "CK-1", 10);                                                              // CK-1 first, CK-2 about two seconds later
        age_record(w, "CK-2", 20);
        w.restart.restore_slice_ms = 0;
        w.start_server(500, false);
        const uint32_t restore_began = w.now;
        uint32_t back1 = 0;
        uint32_t back2 = 0;
        while ((back1 == 0 || back2 == 0) && w.now - restore_began < 60000) {
            w.run(10);
            if (back1 == 0 && w.status("CK-1").restored) back1 = w.now;
            if (back2 == 0 && w.status("CK-2").restored) back2 = w.now;
        }
        ASSERT_TRUE(back1 != 0 && back2 != 0 && back2 - back1 >= 1500);                       // the replays ended seconds apart
        const auto absent_for = [&](const std::string& code) {
            uint32_t most = 0;
            for (const RoomStatus::Absent& a : w.status(code).absent) most = std::max(most, a.away_s);
            return most;
        };
        // 40 s after CK-2 came back: CK-1 has been back 42 s, CK-2 40 s: each has been absent for its own time
        w.run(40000);
        ASSERT_TRUE(absent_for("CK-2") >= 38 && absent_for("CK-2") <= 42);
        ASSERT_TRUE(absent_for("CK-1") >= 38 + (back2 - back1) / 1000 - 1 && absent_for("CK-1") <= 42 + (back2 - back1) / 1000 + 1);
        ASSERT_TRUE(w.status("CK-1").vote_seat == 255 && w.status("CK-2").vote_seat == 255);   // (the vote opens after 90 s, for each room after its own restore)
        // 90 s after CK-1 came back and not yet 90 s after CK-2: the vote of CK-1 is open and CK-2's is not
        while (w.now - back1 < 90000 + 700) w.run(100);
        ASSERT_TRUE(w.now - back2 < 90000 - 500);
        ASSERT_TRUE(w.status("CK-1").vote_seat != 255 && w.status("CK-2").vote_seat == 255);
        while (w.now - back2 < 90000 + 700) w.run(100);
        ASSERT_TRUE(w.status("CK-1").vote_seat != 255 && w.status("CK-2").vote_seat != 255);
    } TEST_END();

    TEST_CASE("S3.123 The Record's Flush Keeps Its Pace By Its Own Time (L6 Of The Review Of Reconnect On For Every Room): A Flush That Takes More Than slow_sync_ms (20 ms Of Real Time) Doubles The Room's Interval To The Next One, To max_sync_every_ms (10 s) At The Most, So That A Slow Disk Is Asked Less Often; A Flush That Takes 20 ms Or Less Brings The Interval Back To sync_every_ms (A Second); The Status Says The Interval, And A Room Without A Record Has None") {
        const RestartConfig defaults;
        ASSERT_TRUE(defaults.sync_every_ms == 1000 && defaults.slow_sync_ms == 20 && defaults.max_sync_every_ms == 10000);
        PWorld w("restore-123");
        uint32_t fake = 0;
        uint32_t step = 1;
        uint32_t readings = 0;
        w.restart.clock_ms = [&]() { ++readings; fake += step; return fake; };                // every reading costs `step` ms: a flush of the record "takes" that long (it reads the clock twice)
        w.start_server(500);
        ASSERT_TRUE(w.mgr->create_room(held_spec("FL-0", 2), w.server_now()).ok);
        ASSERT_EQ(w.status("FL-0").record_sync_ms, 0u);                                          // (no match, no record, no flush to time)
        {   // the interval is set when the record is made, before any flush (the first is within a second of the match's start)
            ASSERT_TRUE(w.mgr->create_room(held_spec("FL-2", 2), w.server_now()).ok);
            w.connect("P0", "FL-2");
            w.connect("P1", "FL-2");
            ASSERT_TRUE(w.until([&]() { return w.status("FL-2").record_kept; }, 5000));
            ASSERT_TRUE(w.status("FL-2").state == RoomState::Loading && w.status("FL-2").record_sync_ms == 1000u);
            ASSERT_TRUE(w.mgr->close_room("FL-2", w.server_now()));                              // (it has made its point: its flushes are not to be counted below)
        }
        std::vector<RClient*> m = play_room(w, held_spec("FL-1", 2), 3000);
        ASSERT_EQ(w.status("FL-1").record_sync_ms, 1000u);
        // quick flushes: one a second, and the interval stays
        readings = 0;
        w.run(10000);
        ASSERT_TRUE(readings >= 18 && readings <= 22);
        ASSERT_EQ(w.status("FL-1").record_sync_ms, 1000u);
        // a flush of exactly slow_sync_ms is not slow
        step = 20;
        w.run(5000);
        ASSERT_EQ(w.status("FL-1").record_sync_ms, 1000u);
        // slow flushes: the interval doubles at every one, to the limit and no further; the room is flushed far less often than once a second
        step = 30;
        readings = 0;
        std::vector<uint32_t> seen;
        uint32_t last = 1000;
        for (uint32_t t = 0; t < 40000; t += 10) {
            w.run(10);
            const uint32_t now_interval = w.status("FL-1").record_sync_ms;
            if (now_interval != last) seen.push_back(now_interval);
            last = now_interval;
        }
        ASSERT_TRUE(seen == std::vector<uint32_t>({2000u, 4000u, 8000u, 10000u}));
        ASSERT_TRUE(readings >= 11 && readings <= 13);                                           // (6 flushes in 40 s, not 40)
        // a flush of 21 ms is slow, one of 20 ms is not: right at the edge
        step = 1;
        ASSERT_TRUE(w.until([&]() { return w.status("FL-1").record_sync_ms == 1000; }, 10500)); // the next flush is quick: back to a second at once, not by halves
        step = 21;
        ASSERT_TRUE(w.until([&]() { return w.status("FL-1").record_sync_ms == 2000; }, 1500));
        step = 20;
        ASSERT_TRUE(w.until([&]() { return w.status("FL-1").record_sync_ms == 1000; }, 2500));
        step = 1;
        readings = 0;
        w.run(5000);
        ASSERT_TRUE(readings >= 8);                                                              // (a flush a second again)
        ASSERT_TRUE(w.status("FL-1").state == RoomState::Running && w.status("FL-1").record_kept && w.status("FL-1").record_note.empty());
        // a room that comes back from its record starts at the configured interval, and keeps its pace from there
        step = 30;
        ASSERT_TRUE(w.until([&]() { return w.status("FL-1").record_sync_ms == 2000; }, 1500));
        w.stop_server(true);
        step = 1;
        w.start_server(500);
        ASSERT_TRUE(w.status("FL-1").restored && w.status("FL-1").record_kept);
        ASSERT_EQ(w.status("FL-1").record_sync_ms, 1000u);
        (void)m;
    } TEST_END();

    TEST_CASE("S3.124 A Record That The Server Stopped In The Middle Of Replaying Is Not Replayed Again (L10 Of The Review: The Crash-Loop Guard): Its Marker Is On Disk While A Piece Of Its Replay Runs And Gone When The Replay Has Ended, Restored Or Refused; A Marker That A Start Finds Puts The Record By In The Folder Refused, Unread, With A Line In The Log And An Item In The Report, And The Records Next To It Are Restored As Always; The Marker Of A Record That Is Not There Is Removed When The Folder Is Prepared") {
        {   // the marker is on disk while the replay's pieces run, and gone when it has ended: restored, and refused in its replay
            PWorld w("restore-124a");
            w.start_server(500);
            crash_with_record_of(w, "CL-1", 12000, 2);
            copy_record_as(w, "CL-1", "CL-2", std::chrono::seconds(0));
            tamper_checkpoint(w, "CL-2", 2);                                                       // (CL-2's replay is refused at its second checkpoint)
            const std::string marker_1 = w.record_path("CL-1") + ".replaying";
            const std::string marker_2 = w.record_path("CL-2") + ".replaying";
            uint32_t fake = 0;
            uint32_t readings = 0;
            uint32_t marked = 0;
            w.restart.clock_ms = [&]() {
                ++readings;
                if (fs::exists(marker_1) != fs::exists(marker_2)) ++marked;                       // (one record at a time is worked on: one of the two markers is there)
                return fake += 6;
            };
            w.start_server(500, false);
            ASSERT_TRUE(!fs::exists(marker_1) && !fs::exists(marker_2));                           // (judged, not replayed: no piece of work yet)
            w.finish_restore();
            ASSERT_TRUE(marked > 6 && marked < readings);                                           // (the pieces read the clock with the marker there; the pass between them does not)
            ASSERT_TRUE(!fs::exists(marker_1) && !fs::exists(marker_2));
            ASSERT_TRUE(w.status("CL-1").restored && w.status("CL-1").state == RoomState::Running);
            ASSERT_EQ(w.report.count(RestoreItem::Outcome::Restored), size_t{1});
            ASSERT_EQ(w.report.count(RestoreItem::Outcome::Ended), size_t{1});
        }
        {   // a marker that the last run left: the record is put by unread and not replayed; the others are restored
            PWorld w("restore-124b");
            w.start_server(500);
            crash_with_record_of(w, "CL-1", 8000, 2);
            copy_record_as(w, "CL-1", "CL-2", std::chrono::seconds(0));
            const std::vector<uint8_t> bytes_1 = read_all_bytes(w.record_path("CL-1"));
            const std::string file_1 = fs::path(w.record_path("CL-1")).filename().string();
            const std::string marker_1 = w.record_path("CL-1") + ".replaying";
            {
                RestartStore store(w.restart);
                ASSERT_FALSE(store.was_replaying(w.record_path("CL-1")));
                ASSERT_TRUE(store.mark_replaying(w.record_path("CL-1")));                          // what a crash in the middle of CL-1's replay leaves
                ASSERT_TRUE(store.was_replaying(w.record_path("CL-1")) && !store.was_replaying(w.record_path("CL-2")));
                ASSERT_TRUE(store.mark_replaying(w.record_path("CL-1")));                          // (marking a record that is marked is no failure)
                store.unmark_replaying(w.record_path("CL-2"));                                     // (nor is taking off a marker that is not there)
                ASSERT_TRUE(fs::exists(marker_1));
            }
            w.start_server(500);
            ASSERT_EQ(w.report.items.size(), size_t{2});
            const RestoreItem* skipped = nullptr;
            for (const RestoreItem& i : w.report.items) skipped = i.file == file_1 ? &i : skipped;
            ASSERT_TRUE(skipped != nullptr && skipped->outcome == RestoreItem::Outcome::Unreadable && skipped->code.empty());
            ASSERT_TRUE(skipped->note.find("stopped in the middle") != std::string::npos && skipped->note.find("not replayed again") != std::string::npos);
            RoomStatus none;
            ASSERT_FALSE(w.mgr->status("CL-1", none, w.server_now()));                              // no room, no failed room
            ASSERT_TRUE(w.status("CL-2").restored && w.status("CL-2").state == RoomState::Running);   // the record next to it came back
            ASSERT_FALSE(fs::exists(w.record_path("CL-1")));
            ASSERT_FALSE(fs::exists(marker_1));
            const fs::path kept = fs::path(w.restart.dir) / "refused" / file_1;
            ASSERT_TRUE(fs::exists(kept) && read_all_bytes(kept.string()) == bytes_1);              // (kept as it was: the owner may want it)
            bool said = false;
            for (const std::string& line : w.notices) said = said || (line.find(file_1) != std::string::npos && line.find("was not restored: the server stopped in the middle of this record's replay") != std::string::npos);
            ASSERT_TRUE(said);
            // and it stays so: the next start has nothing of it to replay
            w.stop_server(false);
            w.start_server(500);
            ASSERT_EQ(w.report.items.size(), size_t{1});
            ASSERT_TRUE(w.status("CL-2").restored);
        }
        {   // the marker of a record that is not there is removed when the folder is prepared: a record of that name made later is not taken for one that stopped the server
            PWorld w("restore-124c");
            w.start_server(500);
            const std::string path = w.record_path("OR-1");
            {
                RestartStore store(w.restart);
                ASSERT_TRUE(store.mark_replaying(path));
            }
            ASSERT_TRUE(fs::exists(path + ".replaying") && !fs::exists(path));
            w.stop_server(false);
            w.start_server(500);
            ASSERT_FALSE(fs::exists(path + ".replaying"));
            crash_with_record_of(w, "OR-1", 6000, 2);
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Restored);
            ASSERT_TRUE(w.status("OR-1").restored);
        }
    } TEST_END();
}



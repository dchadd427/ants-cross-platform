#include "ants_server/map_pictures.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#endif

namespace ants::server {

namespace fs = std::filesystem;

namespace {

// A change of the picture's look (the renderer, the box, the encoder) that should show on the maps that already have one: raise this number, and the pictures of the folder are made again
constexpr int kPictureVersion = 1;
constexpr const char* kVersionFile = "version";

bool is_test_stem(const std::string& stem) { return stem.compare(0, 5, "test-") == 0; }

bool good_stem(const std::string& stem) {
    if (stem.empty() || stem.size() > 64) return false;
    return std::all_of(stem.begin(), stem.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'; });
}

#if !defined(_WIN32)
constexpr uint64_t kMaxMapFileBytes = 4u * 1024u * 1024u;       // MapStore::kMaxMapBytes: a larger file is no map of the server's
constexpr int kChildNice = 10;                                  // the picture maker never competes with the rooms: at least this nice value

std::string printable(std::string text, size_t most) {
    for (char& c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
    }
    if (text.size() > most) text.resize(most);
    return text;
}

/// Closes every descriptor from 3 up: what the server has open (its sockets, its files) is not the picture maker's. Only calls that are safe between fork and exec.
void close_inherited_descriptors(long open_max) {
    bool closed = false;
#if defined(__linux__) && defined(SYS_close_range)
    closed = syscall(SYS_close_range, 3u, ~0u, 0u) == 0;
#endif
    if (!closed) {
        for (long fd = 3; fd < open_max; ++fd) ::close(static_cast<int>(fd));
    }
}

std::string first_line_of(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string line(256, '\0');
    in.read(&line[0], static_cast<std::streamsize>(line.size()));
    line.resize(static_cast<size_t>(in.gcount()));
    const size_t end = line.find('\n');
    if (end != std::string::npos) line.resize(end);
    return printable(line, 160);
}
#endif

uint32_t be32(const std::string& s, size_t at) {
    return (uint32_t{static_cast<unsigned char>(s[at])} << 24) | (uint32_t{static_cast<unsigned char>(s[at + 1])} << 16) | (uint32_t{static_cast<unsigned char>(s[at + 2])} << 8) | uint32_t{static_cast<unsigned char>(s[at + 3])};
}

bool is_png_of_side(const std::string& bytes, uint32_t most_side) {
    static const char kSignature[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    static const char kEnd[12] = {'\0', '\0', '\0', '\0', 'I', 'E', 'N', 'D', '\xAE', '\x42', '\x60', '\x82'};
    if (bytes.size() < 8 + 25 + 12) return false;
    if (bytes.compare(0, 8, kSignature, 8) != 0) return false;
    if (be32(bytes, 8) != 13 || bytes.compare(12, 4, "IHDR") != 0) return false;
    const uint32_t w = be32(bytes, 16);
    const uint32_t h = be32(bytes, 20);
    if (w < 1 || h < 1 || w > most_side || h > most_side) return false;
    return bytes.compare(bytes.size() - 12, 12, kEnd, 12) == 0;
}

}  // namespace

MapPictures::MapPictures(MapPicturesConfig config) : config_(std::move(config)) {}

MapPictures::~MapPictures() {
#if !defined(_WIN32)
    if (running_pid_ != 0) {
        kill(static_cast<pid_t>(running_pid_), SIGKILL);
        int status = 0;
        waitpid(static_cast<pid_t>(running_pid_), &status, 0);
        running_pid_ = 0;
        std::error_code ec;
        fs::remove(running_temp_, ec);
        fs::remove(running_log_, ec);
    }
#endif
}

std::string MapPictures::stem_of(const std::string& map_path) {
    const std::string name = fs::path(map_path).filename().string();
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return std::string();
    const std::string ending = name.substr(dot);
    if (ending != ".lvl" && ending != ".LVL") return std::string();
    const std::string stem = name.substr(0, dot);
    return good_stem(stem) ? stem : std::string();
}

std::string MapPictures::picture_path(const std::string& stem) const { return (fs::path(config_.dir) / (stem + ".png")).string(); }

bool MapPictures::is_good_png(const std::string& path) const {
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    if (ec || size == 0 || size > kMaxPictureBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return is_png_of_side(bytes, static_cast<uint32_t>(kMaxSide));
}

bool MapPictures::open(std::string& why) {
    enabled_ = false;
    ready_.clear();
    if (config_.dir.empty()) {
        why = "no folder for the pictures";
        return false;
    }
    std::error_code ec;
    // every path is made absolute now: the child's working folder is the root folder, so that a relative path of the operator's would not name the same file there
    config_.dir = fs::absolute(config_.dir, ec).string();
    if (!config_.program.empty()) config_.program = fs::absolute(config_.program, ec).string();
    if (!config_.archive.empty()) config_.archive = fs::absolute(config_.archive, ec).string();
    ec.clear();
    fs::create_directories(config_.dir, ec);
    if (ec || !fs::is_directory(config_.dir, ec)) {
        why = "the folder of the pictures cannot be made: " + (ec ? ec.message() : std::string("not a folder"));
        return false;
    }
#if !defined(_WIN32)
    ::chmod(config_.dir.c_str(), 0700);
#endif
    // the pictures of another look are made again; what an earlier run left half made goes
    const fs::path marker = fs::path(config_.dir) / kVersionFile;
    int version = 0;
    {
        std::ifstream in(marker);
        if (in) in >> version;
    }
    const bool stale = version != kPictureVersion;
    for (const fs::directory_entry& entry : fs::directory_iterator(config_.dir, ec)) {
        std::error_code rc;
        if (!entry.is_regular_file(rc)) continue;
        const std::string ending = entry.path().extension().string();
        if (ending == ".tmp" || ending == ".err" || (stale && ending == ".png")) {
            fs::remove(entry.path(), rc);
        } else if (ending == ".png") {
            const std::string stem = entry.path().stem().string();
            if (good_stem(stem) && is_good_png(entry.path().string())) ready_.insert(stem);
            else fs::remove(entry.path(), rc);
        }
    }
    if (stale) {
        std::ofstream out(marker, std::ios::trunc);
        out << kPictureVersion << "\n";
        if (!out) {
            why = "the folder of the pictures cannot be written";
            return false;
        }
    }
#if defined(_WIN32)
    if (!config_.program.empty()) notices_.push_back("map pictures are off: this platform cannot start the picture maker");
    return true;
#else
    if (config_.program.empty()) return true;                                      // (the pictures that are there are served, no new one is made)
    if (config_.archive.empty()) {
        notices_.push_back("map pictures: no archive for the picture maker, so no new picture is made");
        return true;
    }
    struct stat st {};
    if (::stat(config_.program.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || ::access(config_.program.c_str(), X_OK) != 0) {
        notices_.push_back("map pictures: the picture maker cannot be run, so no new picture is made");
        return true;
    }
    if (::stat(config_.archive.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        notices_.push_back("map pictures: the game's archive cannot be read, so no new picture is made");
        return true;
    }
    enabled_ = true;
    return true;
#endif
}

bool MapPictures::ready(const std::string& map_path) const { return ready_.count(stem_of(map_path)) != 0; }

void MapPictures::want(const std::string& map_path) {
    if (!enabled_) return;
    const std::string stem = stem_of(map_path);
    if (stem.empty() || ready_.count(stem) != 0 || queued_.count(stem) != 0) return;
    if (running_pid_ != 0 && running_.stem == stem) return;
    const auto failed = failed_.find(stem);
    if (failed != failed_.end() && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - failed->second).count() < static_cast<int64_t>(kRetryAfterMs)) return;
    // two allowances: the sweep of the start (hundreds of maps) must never keep out a test map that a person waits for
    const bool test = is_test_stem(stem);
    if (test ? test_queued_ >= config_.max_test_queue : queue_.size() - test_queued_ >= config_.max_queue) {
        bool& said = test ? test_queue_full_said_ : queue_full_said_;
        if (!said) notices_.push_back("map pictures: " + std::to_string(test ? config_.max_test_queue : config_.max_queue) + (test ? " test maps" : " maps") + " are waiting for their picture, no more are queued for now");
        said = true;
        return;
    }
    (test ? test_queue_full_said_ : queue_full_said_) = false;
    if (test) {
        queue_.push_front(Job{stem, map_path});                     // (a test map's picture has a person waiting for it: it goes before the maps that the start asks for)
        ++test_queued_;
    } else {
        queue_.push_back(Job{stem, map_path});
    }
    queued_.insert(stem);
}

std::string MapPictures::get(const std::string& map_path) {
    const std::string stem = stem_of(map_path);
    if (stem.empty()) return std::string();
    if (ready_.count(stem) != 0) {
        std::error_code ec;
        const uint64_t size = fs::file_size(picture_path(stem), ec);
        if (!ec && size > 0 && size <= kMaxPictureBytes) {
            std::ifstream in(picture_path(stem), std::ios::binary);
            std::string bytes(static_cast<size_t>(size), '\0');
            if (in && in.read(&bytes[0], static_cast<std::streamsize>(size))) return bytes;
        }
        ready_.erase(stem);                                              // (the file was taken away: made again)
    }
    want(map_path);
    return std::string();
}

void MapPictures::fail(const std::string& stem, const std::string& why) {
    ++failures_;
    const bool first = failed_.find(stem) == failed_.end();
    failed_[stem] = Clock::now();
    if (first) notices_.push_back("map picture " + stem.substr(0, 8) + ": " + why + " (no picture; asked again after " + std::to_string(kRetryAfterMs / 60000u) + " minutes at the earliest)");
}

#if !defined(_WIN32)

MapPictures::Start MapPictures::start(const Job& job) {
    std::error_code ec;
    const uint64_t size = fs::is_regular_file(job.map_path, ec) ? fs::file_size(job.map_path, ec) : 0;
    if (ec || size == 0 || size > kMaxMapFileBytes) {
        fail(job.stem, "the map file is gone or is not a map file of the server");
        return Start::MapFailed;
    }
    running_temp_ = (fs::path(config_.dir) / (job.stem + ".tmp")).string();
    running_log_ = (fs::path(config_.dir) / (job.stem + ".err")).string();
    // everything that the child needs is made before the fork: between fork and exec only calls that are safe there are made
    const std::string map_path = fs::absolute(job.map_path, ec).string();                      // (the child's folder is the root: the path must not depend on the server's own)
    const std::vector<std::string> args = {config_.program, "--chd", config_.archive, "--out", running_temp_, "--memory-mb", std::to_string(config_.memory_mb),
                                           "--seconds", std::to_string(std::max<uint32_t>(1, config_.timeout_ms / 1000u)), map_path};
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    static char env_video[] = "SDL_VIDEODRIVER=dummy";
    static char env_audio[] = "SDL_AUDIODRIVER=dummy";
    static char env_render[] = "SDL_RENDER_DRIVER=software";
    char* envp[] = {env_video, env_audio, env_render, nullptr};
    long open_max = sysconf(_SC_OPEN_MAX);
    if (open_max < 0 || open_max > 4096) open_max = 4096;
    const char* const log_path = running_log_.c_str();
    const pid_t server = getpid();
    errno = 0;
    const int own_nice = getpriority(PRIO_PROCESS, 0);
    const int child_nice = errno == 0 && own_nice > kChildNice ? own_nice : kChildNice;          // (never a better priority than the server's own)

    const pid_t pid = config_.fork_hook ? static_cast<pid_t>(config_.fork_hook()) : fork();
    if (pid < 0) {
        const int why = errno;
        if (!transient_said_) notices_.push_back(std::string("map pictures: a process cannot be started now (") + std::strerror(why) + "); the pictures wait and are tried again in " + std::to_string(config_.fork_retry_ms / 1000u + (config_.fork_retry_ms % 1000u != 0 ? 1u : 0u)) + " s");
        transient_said_ = true;
        return Start::Transient;
    }
    if (pid == 0) {
        // the child, before it is the picture maker: its own folder is the root (never the results folder, which holds the control secret and the database), the lowest priority, and a SIGKILL when the
        // server dies (Linux; a server that died between the fork and here leaves a parent that is no longer the server)
        if (chdir("/") != 0) _exit(126);
        setpriority(PRIO_PROCESS, 0, child_nice);
#if defined(__linux__)
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != server) _exit(126);
#else
        (void)server;
#endif
        const int in = ::open("/dev/null", O_RDONLY);
        const int out = ::open("/dev/null", O_WRONLY);
        const int err = ::open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (in < 0 || out < 0 || err < 0 || dup2(in, 0) < 0 || dup2(out, 1) < 0 || dup2(err, 2) < 0) _exit(126);
        close_inherited_descriptors(open_max);
        execve(argv[0], argv.data(), envp);
        _exit(127);
    }
    transient_said_ = false;
    running_pid_ = static_cast<long>(pid);
    running_ = job;
    started_ = Clock::now();
    return Start::Started;
}

void MapPictures::put_back(const Job& job, const std::string& why) {
    queue_.push_front(job);
    queued_.insert(job.stem);
    if (is_test_stem(job.stem)) ++test_queued_;
    retry_at_ = Clock::now() + std::chrono::milliseconds(config_.fork_retry_ms);
    if (!why.empty() && !setup_said_) {
        notices_.push_back("map pictures: " + why + "; the pictures wait and are tried again");
        setup_said_ = true;
    }
}

void MapPictures::finish(bool exited_ok, const std::string& how) {
    const Job job = running_;
    running_pid_ = 0;
    std::error_code ec;
    if (!exited_ok && how.find("code 126") != std::string::npos) {        // (the child could not set itself up: descriptors, folder; nothing about the map)
        fs::remove(running_temp_, ec);
        fs::remove(running_log_, ec);
        put_back(job, "the picture maker could not be set up");
        return;
    }
    if (exited_ok && is_good_png(running_temp_)) {
        fs::rename(running_temp_, picture_path(job.stem), ec);
        if (!ec) {
            ready_.insert(job.stem);
            setup_said_ = false;
            failed_.erase(job.stem);
            fs::remove(running_log_, ec);
            return;
        }
    }
    const std::string said = first_line_of(running_log_);
    fs::remove(running_temp_, ec);
    fs::remove(running_log_, ec);
    fail(job.stem, exited_ok ? "the picture maker made no usable picture" : how + (said.empty() ? std::string() : ": " + said));
}

void MapPictures::poll() {
    if (!enabled_) return;
    if (running_pid_ != 0) {
        int status = 0;
        const pid_t r = waitpid(static_cast<pid_t>(running_pid_), &status, WNOHANG);
        if (r == static_cast<pid_t>(running_pid_)) {
            if (WIFEXITED(status)) finish(WEXITSTATUS(status) == 0, "the picture maker ended with code " + std::to_string(WEXITSTATUS(status)));
            else finish(false, "the picture maker was stopped by signal " + std::to_string(WIFSIGNALED(status) ? WTERMSIG(status) : 0));
        } else if (r < 0 && errno != EINTR) {
            finish(false, "the picture maker was lost");
        } else if (r == 0 && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_).count() > static_cast<int64_t>(config_.timeout_ms)) {
            kill(static_cast<pid_t>(running_pid_), SIGKILL);
            int dead = 0;
            waitpid(static_cast<pid_t>(running_pid_), &dead, 0);
            finish(false, "the picture maker took longer than " + std::to_string(config_.timeout_ms / 1000u) + " s and was stopped");
        }
        return;
    }
    if (Clock::now() < retry_at_) return;
    while (!queue_.empty() && running_pid_ == 0) {
        const Job job = queue_.front();
        queue_.pop_front();
        queued_.erase(job.stem);
        if (is_test_stem(job.stem)) --test_queued_;
        if (ready_.count(job.stem) != 0) continue;
        if (start(job) == Start::Transient) {                          // (no process now: not the map's fault; it keeps its place, the queue waits, nothing is drained)
            put_back(job, std::string());
            break;
        }
    }
}

#else

MapPictures::Start MapPictures::start(const Job&) { return Start::MapFailed; }
void MapPictures::put_back(const Job&, const std::string&) {}
void MapPictures::finish(bool, const std::string&) {}
void MapPictures::poll() {}

#endif

size_t MapPictures::prune(const std::function<bool(const std::string& stem)>& keep) {
    size_t gone = 0;
    std::error_code ec;
    if (config_.dir.empty() || !fs::is_directory(config_.dir, ec)) return 0;
    std::vector<fs::path> doomed;
    for (const fs::directory_entry& entry : fs::directory_iterator(config_.dir, ec)) {
        std::error_code rc;
        if (!entry.is_regular_file(rc) || entry.path().extension() != ".png") continue;
        const std::string stem = entry.path().stem().string();
        if (!good_stem(stem) || !keep(stem)) doomed.push_back(entry.path());
    }
    for (const fs::path& p : doomed) {
        std::error_code rc;
        if (fs::remove(p, rc)) ++gone;
        ready_.erase(p.stem().string());
        failed_.erase(p.stem().string());
    }
    for (auto it = failed_.begin(); it != failed_.end();) {                     // (a map that failed and is gone)
        it = keep(it->first) ? std::next(it) : failed_.erase(it);
    }
    return gone;
}

std::vector<std::string> MapPictures::queue_order() const {
    std::vector<std::string> out;
    for (const Job& j : queue_) out.push_back(j.stem);
    return out;
}

std::vector<std::string> MapPictures::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

}  // namespace ants::server

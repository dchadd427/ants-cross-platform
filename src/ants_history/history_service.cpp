#include "ants_history/history_service.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <system_error>

#include "ants_db/migrations.hpp"
#include "ants_net/clock.hpp"
#include "ants_server/control.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/submissions.hpp"

namespace fs = std::filesystem;

namespace ants::history {

namespace {

// A steady millisecond clock for both threads (a function-local static is made once, safely)
uint32_t steady_ms() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

bool is_history_path(const std::string& path) { return path == "/history" || path.compare(0, 9, "/history/") == 0; }

constexpr uint32_t kBusyShortMs = 250;                 // how long the HTTP side waits for a lock (only a file without WAL has one): a request must not hold the loop for seconds
constexpr uint32_t kApprovedMissLookMs = 5000;         // a map name that the list does not know makes the list be read again at most this often (a map approved a moment ago); a read that failed, or a recording that waits for its map, the same
constexpr uint32_t kPendingWaitMs = 5;                 // the counter's wait between passes while recordings wait for their pace

}  // namespace

// The maps of the players that the game server's operator approved: "<Name>.lvl" is the file name that a match names the map by, and its file is in the submissions folder of the game server. They are read from the game
// server's database (read only) when a recording asks for one that the list does not hold, and again now and then. The counter thread only.
class HistoryService::ApprovedMaps {
public:
    ApprovedMaps(const ServiceConfig& cfg, std::function<void(const std::string&)> say) : game_db_(cfg.game_db_path), dir_(cfg.submissions_dir), refresh_ms_(cfg.approved_refresh_s * 1000u), say_(std::move(say)) {}

    /// The file of the approved map `name` ("" when there is none), the list read again when it is old, or when it does not know the name and has not been read for a moment
    std::string path_of(const std::string& name, uint32_t now_ms) {
        if (!loaded_ || net::time_reached(now_ms, loaded_ms_ + interval_ms())) load(now_ms);
        auto it = files_.find(name);
        if (it == files_.end() && net::time_reached(now_ms, loaded_ms_ + kApprovedMissLookMs)) {
            load(now_ms);
            it = files_.find(name);
        }
        return it == files_.end() ? std::string() : it->second;
    }

    /// Reads the list again when it is due (the counter asks while a recording waits for its map: the list is read when a name is asked for, and that is not asked while the recording is left out)
    void poll(uint32_t now_ms) {
        if (!loaded_ || net::time_reached(now_ms, loaded_ms_ + interval_ms())) load(now_ms);
    }
    /// Moves up whenever a read finds another list than the one before (also the first list that is read after reads that failed): a recording that was left out for want of its map may be countable now
    uint64_t generation() const noexcept { return generation_; }

private:
    uint32_t interval_ms() const { return failing_ ? std::min(refresh_ms_, kApprovedMissLookMs) : refresh_ms_; }

    void load(uint32_t now_ms) {
        loaded_ = true;
        loaded_ms_ = now_ms;
        if (!db_.is_open()) {
            db::OpenOptions options;
            options.create = false;
            options.read_only = true;
            options.busy_timeout_ms = 1000;
            const db::Error e = db_.open(game_db_, options);
            if (!e.ok()) return trouble("the approved maps of the players cannot be looked up (the game server's database cannot be read: " + e.message + "); a match on one is left out until it can, then counted");
        }
        db::Statement st;
        db::Error e = db_.prepare(st, "SELECT final_name, file FROM map_submissions WHERE state = 'approved' AND final_name IS NOT NULL");
        std::map<std::string, std::string> found;
        while (e.ok()) {
            const db::Statement::Step step = st.step();
            if (step == db::Statement::Step::Failed) e = st.error();
            if (step != db::Statement::Step::Row) break;
            found[server::names::file_name_of(st.column_text(0))] = server::submission_file_path(dir_, st.column_text(1));
        }
        if (!e.ok()) {
            db_.close();                                                     // (opened again at the next look)
            return trouble("the approved maps of the players cannot be looked up (" + e.message + "); a match on one is left out until they can, then counted");
        }
        if (found != files_) ++generation_;
        files_ = std::move(found);
        if (failing_) say_("the approved maps of the players can be looked up again");
        failing_ = false;
    }

    void trouble(const std::string& line) {
        if (!failing_) say_(line);                                           // (once for as long as it lasts)
        failing_ = true;
    }

    std::string game_db_;
    std::string dir_;
    uint32_t refresh_ms_;
    std::function<void(const std::string&)> say_;
    db::Database db_;
    std::map<std::string, std::string> files_;
    bool loaded_{false};
    bool failing_{false};
    uint64_t generation_{0};
    uint32_t loaded_ms_{0};
};

HistoryService::HistoryService(ServiceConfig config) : cfg_(std::move(config)) {}

HistoryService::~HistoryService() { stop(); }

void HistoryService::say(const std::string& line) {
    if (!cfg_.log) return;
    const std::lock_guard<std::mutex> lock(log_mutex_);
    cfg_.log(line);
}

void HistoryService::flush_notes(server::ReplayStore* replays, server::HistoryStore* history) {
    if (replays != nullptr) {
        for (const std::string& line : replays->take_notes()) say(line);
    }
    if (history != nullptr) {
        for (const std::string& line : history->take_notes()) say(line);
    }
}

bool HistoryService::start(std::string& why) {
    why.clear();
    if (started_) {
        why = "the service has started already";
        return false;
    }
    std::error_code ec;
    const auto folder = [&](const std::string& path, const char* what, bool needed) {
        if (path.empty() && !needed) return true;
        if (path.empty() || !fs::is_directory(fs::u8path(path), ec)) {
            why = std::string("the ") + what + " folder '" + path + "' is not there or is not a folder";
            return false;
        }
        return true;
    };
    if (cfg_.db_path.empty()) {
        why = "no database file for the history is named";
        return false;
    }
    if (!folder(cfg_.recordings_dir, "recordings", true) || !folder(cfg_.maps_dir, "maps", true) || !folder(cfg_.community_dir, "community maps", false)) return false;
    if (!cfg_.game_db_path.empty() && cfg_.submissions_dir.empty()) {
        why = "a game database is named but not the folder of its submitted maps";
        return false;
    }
    // The service's own file, made when it is not there: the writer's connection, brought to the schema of this program (a file of another program, or a newer one, is refused and left as it was)
    fs::create_directories(fs::u8path(cfg_.db_path).parent_path(), ec);
    int version = 0;
    std::string journal;
    db::Error e = db::open_and_migrate(writer_db_, cfg_.db_path, server::history_db_migrations(), db::OpenOptions(), &version, &journal);
    if (!e.ok()) {
        why = "the history database '" + cfg_.db_path + "' cannot be used: " + e.message;
        return false;
    }
    say("database " + cfg_.db_path + ": schema version " + std::to_string(version) + " (journal mode " + journal + ")" +
        (journal == "wal" ? std::string() : std::string("; the file system gave no WAL: the rollback journal is used, and a list may wait a moment for a write")));
    // The history of the game server's file, once (a file that was imported already, or has a history of its own, is left as it is)
    if (!cfg_.game_db_path.empty()) {
        std::string import_why;
        if (!import_game_history(writer_db_, cfg_.game_db_path, import_report_, import_why)) {
            why = import_why;
            writer_db_.close();
            return false;
        }
        say(import_report_.note);
    }
    db::OpenOptions reading;
    reading.create = false;
    reading.read_only = true;
    reading.busy_timeout_ms = kBusyShortMs;
    e = reader_db_.open(cfg_.db_path, reading);
    if (!e.ok()) {
        why = "the history database '" + cfg_.db_path + "' cannot be opened for reading: " + e.message;
        writer_db_.close();
        return false;
    }
    server::ReplayConfig rc;
    rc.dir = cfg_.recordings_dir;
    rc.keep_days = cfg_.keep_days;
    rc.max_bytes = cfg_.max_bytes;
    rc.settle_s = cfg_.settle_s;
    rc.min_free_bytes = 0;
    rc.read_only = true;
    rc.clock_s = cfg_.clock_s;
    counter_replays_ = std::make_unique<server::ReplayStore>(rc);
    http_replays_ = std::make_unique<server::ReplayStore>(rc);
    server::HistoryStore::Config hc;
    hc.clock_us = cfg_.history_clock_us;
    hc.retry_ms = cfg_.store_retry_ms;
    counter_history_ = std::make_unique<server::HistoryStore>(writer_db_, hc);
    http_history_ = std::make_unique<server::HistoryStore>(reader_db_, hc);
    std::string store_why;
    const bool ready = counter_replays_->prepare(store_why) && http_replays_->prepare(store_why) && counter_history_->prepare(store_why) && http_history_->prepare(store_why);
    if (!ready) {
        why = store_why;
        reader_db_.close();
        writer_db_.close();
        return false;
    }
    flush_notes(counter_replays_.get(), counter_history_.get());
    http_history_->take_notes();                                                     // (the HTTP side's start says the same as the counter's: only what goes wrong on this side is said later)
    http_replays_->take_notes();
    // A history and no recording at all: a quiet month, or a folder that is not the game server's (a mount that is empty). Nothing is deleted for it (the service deletes no record and no marker); the operator is told
    if (counter_replays_->count() == 0 && counter_history_->count() > 0) {
        folder_empty_with_history_ = true;
        say("WARNING: the recordings folder '" + cfg_.recordings_dir + "' holds no recording, and the history holds " + std::to_string(counter_history_->count()) +
            " match(es): is this the folder that the game server keeps its recordings in? Every match is shown as removed until its recording is there; nothing is deleted from the history");
    }
    if (!cfg_.game_db_path.empty()) approved_ = std::make_unique<ApprovedMaps>(cfg_, [this](const std::string& line) { say(line); });
    feeder_ = std::make_unique<server::HistoryFeeder>(*counter_history_, *counter_replays_, cfg_.maps_dir, cfg_.work_clock_us, cfg_.community_dir);
    feeder_->set_pace(cfg_.pace_ms, cfg_.pace_factor);
    feeder_->set_prune_markers(false);                                               // (the service never deletes: only the folder's owner knows that its folder is the right one)
    if (approved_) {
        ApprovedMaps* approved = approved_.get();
        feeder_->set_approved_map_path([approved](const std::string& name) { return approved->path_of(name, steady_ms()); });
    }
    matches_ = counter_history_->count();
    started_ = true;
    counter_ = std::thread([this]() { counter_main(); });
    return true;
}

void HistoryService::counter_main() {
    const auto idle = []() { return true; };                                         // (the counter's thread is its own: nothing is held up while it plays a recording)
    uint64_t seen_approved = approved_ ? approved_->generation() : 0;
    while (!stop_) {
        counter_replays_->rescan();                                                  // (one look at the folder's time when nothing changed)
        if (approved_ && feeder_->left_out_for_map() > 0) {                          // (a recording that was left out for want of its map: the list of approved maps may have changed, or can be read again)
            approved_->poll(steady_ms());
            if (approved_->generation() != seen_approved) {
                seen_approved = approved_->generation();
                feeder_->retry_skipped_maps();
            }
        }
        const bool played = feeder_->step(idle, steady_ms());
        flush_notes(counter_replays_.get(), counter_history_.get());
        matches_ = counter_history_->count();
        counted_ = feeder_->counted();
        left_out_ = feeder_->left_out();
        pending_ = feeder_->pending();
        ++passes_;
        if (played) continue;                                                        // (the next recording, when its pace allows; step() says no until then, and the wait below follows)
        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(feeder_->pending() > 0 ? kPendingWaitMs : cfg_.look_ms), [this]() { return stop_.load(); });
    }
}

void HistoryService::stop() {
    if (!started_) return;
    {
        const std::lock_guard<std::mutex> lock(wake_mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (counter_.joinable()) counter_.join();
    flush_notes(counter_replays_.get(), counter_history_.get());
    feeder_.reset();
    approved_.reset();
    http_history_.reset();
    http_replays_.reset();
    reader_db_.close();                                                              // (the writer goes last: the file is left whole, with no -wal behind it)
    counter_history_.reset();
    counter_replays_.reset();
    writer_db_.close();
    started_ = false;
}

HistoryService::Stats HistoryService::stats() const {
    Stats s;
    s.matches = matches_;
    s.counted = counted_;
    s.left_out = left_out_;
    s.pending = pending_;
    s.passes = passes_;
    return s;
}

ctl::HttpResponse HistoryService::handle(const ctl::HttpRequest& request, uint32_t now_ms) {
    if (!started_) {
        ctl::HttpResponse r;
        r.status = 503;
        r.body = "{\"error\":\"the match history is unavailable\"}";
        return r;
    }
    if (!http_look_armed_ || net::time_reached(now_ms, http_next_look_ms_)) {          // (this side's own look at the folder, so that a match that the counter has just counted shows its recording)
        http_look_armed_ = true;
        http_next_look_ms_ = now_ms + cfg_.http_look_ms;
        http_replays_->rescan();
        http_replays_->take_notes();
    }
    http_history_->maintain(now_ms);
    std::optional<server::HistoryStore::ReadScope> scope;
    if (is_history_path(request.path)) scope.emplace(*http_history_);                 // (one state of the file for the whole answer)
    ctl::HttpResponse response = server::public_history_service_response(http_history_.get(), http_replays_.get(), request);
    scope.reset();
    flush_notes(nullptr, http_history_.get());
    return response;
}

}  // namespace ants::history

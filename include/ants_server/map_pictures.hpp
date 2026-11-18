#pragma once

// The pictures of the players' maps (docs/MAP_SUBMISSIONS.md, "Pictures"): the game's own drawing of each map, made by the program ants_thumb (src/ants_thumb/main.cpp) and kept as a PNG in a folder of its own.
//
// What the server promises:
//  * Never on the game thread. The server's loop only asks (want) and collects (poll): one picture at a time is made by a CHILD PROCESS that the server starts (the operator's program, with no shell, the
//    names of two files and a few numbers on its command line, an empty environment but SDL's dummy drivers, standard input and output closed, every other descriptor of the server closed). The program
//    limits its own memory, CPU time and file size; the server kills it when it has run longer than the time limit. A map that makes the program fail costs one process and one line in the log.
//  * Only stored files. A picture is made of a map file that a store of the server wrote (a submission's canonical file, a test map's), never of bytes that came in with a request; the stores check
//    every map (the core checker, twice) before they write it, and the program reads it again under the game's own limits.
//  * The picture is the program's output, checked again here before it is kept: a PNG of at most kMaxSide pixels a side and kMaxPictureBytes bytes, written under a temporary name and renamed. A picture is
//    named for the map file ("<file name without .lvl>.png": the id of a submission or "test-<8 hex>"), which never changes, so a picture is made once and kept; prune() drops the pictures whose file is gone.
//  * A failure leaves no picture (get gives nothing): the pages show their empty square. A map that failed is not tried again for kRetryAfterMs, and the log says so once.
//  * POSIX only: another platform (Windows) has no pictures, and says so when it starts.

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ants::server {

struct MapPicturesConfig {
    std::string dir;                                  // the folder of the pictures (made 0700 when opened)
    std::string program;                              // the picture maker (ants_thumb); empty: no pictures are made
    std::string archive;                              // the game's archive (ants.chd) that the program reads
    uint32_t timeout_ms{30000};                       // one picture may take this long, then the process is killed
    uint32_t memory_mb{1536};                         // the address space that the program limits itself to (--memory-mb)
    size_t max_queue{256};                            // maps waiting for their picture; more are not queued (the next request or the next start asks again)
};

class MapPictures {
public:
    static constexpr int kMaxSide = 300;                                  // the side of the setup screen's box (map_thumbnail.hpp)
    static constexpr size_t kMaxPictureBytes = 512u * 1024u;
    static constexpr uint32_t kRetryAfterMs = 10u * 60u * 1000u;          // a map whose picture failed is tried again after this long

    explicit MapPictures(MapPicturesConfig config);
    ~MapPictures();

    /// Makes the folder (0700), forgets the temporary files of an earlier run and lists the pictures that are there. False with a sentence when the folder cannot be used. Nothing is made without a program.
    bool open(std::string& why);
    /// Whether pictures are made at all: opened, a program and an archive given, and a platform that can start a process
    bool enabled() const { return enabled_; }

    /// The name that the picture of a map file has: the file name without its ending ("" when the path has no usable name: letters, digits, '-' and '_' only, at most 64)
    static std::string stem_of(const std::string& map_path);

    /// Asks for the picture of the stored map file at `map_path`; returns at once. Nothing happens when the picture is made, queued, being made or failed lately, or when pictures are off.
    void want(const std::string& map_path);
    /// The PNG of the map file at `map_path`; empty when there is none yet, and then it is asked for (want). One small read of the file, on the caller's thread.
    std::string get(const std::string& map_path);
    /// Whether the picture of this map file is made
    bool ready(const std::string& map_path) const;

    /// Start the next process, or collect the one that ran: call it on every pass of the server's loop (a pass costs one waitpid while a picture is being made, nothing otherwise)
    void poll();
    /// Deletes the pictures (and leftovers) of `stem`s for which `keep` says no; false from `keep` for a name that no file of the server has. Returns how many were deleted.
    size_t prune(const std::function<bool(const std::string& stem)>& keep);
    /// Lines for the server's log (no path of a person's, no name; the reason of a failure is the program's own first line, cut to printable text)
    std::vector<std::string> take_notices();

    size_t queued() const { return queue_.size(); }
    bool busy() const { return running_pid_ != 0; }
    size_t made() const { return ready_.size(); }
    size_t failures() const { return failures_; }

    MapPictures(const MapPictures&) = delete;
    MapPictures& operator=(const MapPictures&) = delete;

private:
    struct Job {
        std::string stem;
        std::string map_path;
    };
    using Clock = std::chrono::steady_clock;
    std::string picture_path(const std::string& stem) const;
    bool start(const Job& job);
    void finish(bool exited_ok, const std::string& how);
    void fail(const std::string& stem, const std::string& why);
    bool is_good_png(const std::string& path) const;

    MapPicturesConfig config_;
    bool enabled_{false};
    std::set<std::string> ready_;
    std::deque<Job> queue_;
    std::set<std::string> queued_;                    // the stems of queue_ (a map is asked for every time a page asks for its picture)
    std::map<std::string, Clock::time_point> failed_;
    long running_pid_{0};
    Job running_;
    Clock::time_point started_;
    std::string running_temp_;
    std::string running_log_;
    std::vector<std::string> notices_;
    size_t failures_{0};
    bool queue_full_said_{false};
};

}  // namespace ants::server

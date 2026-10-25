#pragma once

// The test maps of the Map Maker's "With other players" (docs/MAP_SUBMISSIONS.md, "Test maps"): a draft map that a player puts on the server for a lobby room of their own. This is the store: the same core
// checker as a real submission, a folder, a list in memory and the limits. It knows no database, no account and no owner's queue, so it works on a server where the submissions door is switched off, and
// a test map can never become a library map: nothing here writes a row, and the folder is its own. The HTTP side (the one route, the limits by address) is test_maps_http.hpp. One thread, like everything else
// of the server.
//
// What the store promises:
//  * Only a map that the server's own check passes is kept, and what is kept is the CANONICAL file (the core's rewrite of the map, checked again), under a name that the server makes
//    (test-<8 random hex digits>.lvl in the store's folder), never the bytes of the upload and never a name from the player. The name that rooms and guests use is "<Stem> ~test-<8 hex>.lvl": a tilde cannot be
//    typed in, so a test map cannot take the name of a library map or of an approved one.
//  * A map too big for a room (OnlineLimits) is refused. At most kMaxTestMaps are held, at most kMaxTestPerAddress for one address, at most kMaxHeldBytes in all; one lives kTestMapTtlS seconds.
//  * A new map takes the place of the oldest one only when that one is not in use (set_in_use: a room has it) and has been held for kTestMinKeepS: when none can go the upload is refused.
//  * The folder is made 0700 and the files 0600 (POSIX), and it is wiped when the store opens: a restart keeps no test map.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_server/map_store.hpp"
#include "ants_server/upload_common.hpp"

namespace ants::server {

struct TestMapConfig {
    std::string dir;                                  // the folder of the test maps (made when the store opens; only files that the store makes are ever deleted from it)
    uint64_t max_held_bytes{64ull << 20};             // all held files together: a full store drops the oldest that may go, else refuses
    uint64_t min_free_bytes{256ull << 20};            // the disk keeps this much free after a file is written
};

struct TestMapInput {
    std::string kind;                                 // "classic" or "extended": what the sender says it is (the server checks it)
    std::string name;                                 // as typed
    std::string bytes;                                // the map file as sent
    std::string address;                              // the client's address identity (client_identity); "" means none (a test only)
    std::function<bool()> allow_check;                // asked right before the map is checked (the costly part): false answers 503 busy; empty: always
};

struct TestMapOutcome {
    bool ok{false};
    Failure fail;
    ApprovedMap map;
    std::string clean_name;
    std::string hash_text;
    bool usable[4]{false, false, false, false};
};

class TestMapStore {
public:
    static constexpr size_t kMaxTestMaps = 64;
    static constexpr size_t kMaxTestPerAddress = 4;
    static constexpr int64_t kTestMapTtlS = 6 * 3600;
    static constexpr int64_t kTestMinKeepS = 120;
    static constexpr uint32_t kUpkeepEveryMs = 60 * 1000;          // how often upkeep() looks for maps that are six hours old (the server's loop calls it on every pass)
    static constexpr size_t kClassicBytes = 512u * 1024u;          // the file of a Classic map (LevelLimits::for_new_maps())
    static constexpr size_t kMaxBodyBytes = 2u * 1024u * 1024u;    // the largest upload of any kind: a 256 x 256 Extended map (the most a room plays) is under 1 MB

    explicit TestMapStore(TestMapConfig config);

    /// Makes the folder (0700) and deletes the test maps that an earlier run left (only files named test-<8 hex>.lvl or .tmp-*, never anything else). False with a sentence when the folder cannot be used.
    bool open(std::string& why);
    bool is_open() const { return open_; }
    const TestMapConfig& config() const { return config_; }

    /// The size of the largest upload of this kind (the limit that the door applies before it reads a byte)
    static size_t max_upload_bytes(const std::string& kind);

    /// Checks a draft map and keeps its canonical file. Refusals carry the codes of a submission (`not_a_map`, `kind_mismatch`, `map_fails_checks`, `map_too_big`) and `map_too_big_online`, `map_too_many_ants_online`, `too_large`, `busy`,
    /// `storage_full`, `test_limit` (429, one address holds its share) and `test_full` (503).
    TestMapOutcome submit(const TestMapInput& in, int64_t now);
    /// The held maps (those that are too old are dropped first)
    std::vector<ApprovedMap> maps(int64_t now);
    /// Drops the maps that are too old (cheap: it looks at the held maps only)
    void maintenance(int64_t now);
    /// What the server's loop calls on every pass: runs maintenance() when kUpkeepEveryMs have passed since the last run (`now_ms` is a steady millisecond clock), so a map is gone within a minute of its six hours.
    /// True when the set of held maps changed since the last call of take_changed().
    bool upkeep(int64_t now, uint32_t now_ms);
    /// Says whether a room uses the test map of this file name now (the server's rooms); a map in use is not dropped to make room. Unset: no map is in use.
    void set_in_use(std::function<bool(const std::string& file_name)> fn) { in_use_ = std::move(fn); }
    /// Whether the set of held maps changed since the last call (one came in, or one was dropped)
    bool take_changed();
    /// The bytes of all held files
    uint64_t held_bytes() const;

    TestMapStore(const TestMapStore&) = delete;
    TestMapStore& operator=(const TestMapStore&) = delete;

private:
    struct Kept {
        ApprovedMap map;
        int64_t created_at{0};
        std::string owner;                                // the address identity that sent it (never written to a file or a log)
    };
    void prune(int64_t now);
    /// The index of the oldest held map that may be dropped (held for kTestMinKeepS, no room has it, of `only_owner` when that is not empty, not one of `taken`), or kept_.size() when there is none
    size_t droppable(int64_t now, const std::string& only_owner, const std::vector<size_t>& taken) const;

    TestMapConfig config_;
    std::vector<Kept> kept_;
    std::function<bool(const std::string& file_name)> in_use_;
    bool open_{false};
    bool changed_{false};
    bool upkept_{false};
    uint32_t last_upkeep_ms_{0};
};

}  // namespace ants::server

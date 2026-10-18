#pragma once

// The map submissions of the game server (docs/MAP_SUBMISSIONS.md): a player sends a map, it waits, and only the owner makes it public. This is the store: the rules, the database rows and the
// files. The HTTP side (the routes, the owner's session, the rate limits by address) is submissions_http.hpp. One thread, like everything else of the server.
//
// What the store promises:
//  * A submission is accepted only when the server's own check says so (check_map of the map core, under the limits of a new Classic map or of an Extended map): the client's claim is never trusted.
//    What is kept is the CANONICAL file (the core's rewrite of the map), under a file name that the server makes (32 random hex digits), never the bytes of the upload and never a name from the player.
//  * Limits: 3 submissions for each address and UTC day (a keyed hash of the address is counted, never the address), a total for all addresses, 200 waiting at most, a size budget for the waiting files
//    and for all kept files, free disk space left. A withdrawn submission frees its place in the queue (not its place in the day's count).
//  * Names: a map's name is protected from the moment it is approved, for good (a unique index on the folded name; taken down does not free it); the names of the library maps are protected too.
//    Approving never replaces a map. Only the owner renames, and only at approval.
//  * A maker has no account: the submissions of one browser are found by a token that the server makes (256 random bits); only its SHA-256 is kept.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_ctl/json.hpp"

namespace ants::db {
class Database;
}

namespace ants::server {

struct ApprovedMap;

/// What the store is told at start. The defaults are the server's own.
struct SubmissionConfig {
    std::string dir;                                  // the submissions folder; the files are in dir/files
    size_t max_waiting{200};
    size_t per_address_per_day{3};
    size_t per_day_total{300};                        // all addresses together: a flood from many addresses fills the day, not the disk
    uint64_t max_waiting_bytes{256ull << 20};
    uint64_t max_kept_bytes{1024ull << 20};
    uint64_t min_free_bytes{256ull << 20};            // the disk keeps this much free after a file is written
    uint32_t declined_file_days{30};                  // a declined map's file is deleted after this long (the owner can look at it until then)
    uint32_t old_row_days{90};                        // a declined or withdrawn submission's row is deleted after this long
};

/// A refusal: the HTTP status, a stable code (docs/MAP_SUBMISSIONS.md lists them) and one sentence in plain words for the person
struct Failure {
    int status{0};
    std::string code;
    std::string message;
    ctl::JsonValue extra;                             // an object whose members are added to the answer (the findings of a map that fails, a suggested name), or null
};

/// One row of map_submissions
struct Submission {
    std::string id, state, kind, name, final_name, maker, note, owner_message, description, hash, file, findings_json;
    bool renamed{false};
    bool file_kept{true};
    int64_t width{0}, height{0}, minutes{0}, players{0}, size{0}, created_at{0}, decided_at{0};
};

struct SubmitInput {
    std::string kind;                                 // "classic" or "extended": what the sender says it is (the server checks it)
    std::string name, maker, note;                    // as typed
    std::string bytes;                                // the map file as sent
    std::string address;                              // the real client address (from the trusted proxy header)
    std::string token;                                // the maker's token when the browser has one, else empty
    std::function<bool()> allow_check;                // asked right before the map is checked (the costly part): false answers 503 busy; empty: always
};

struct SubmitOutcome {
    bool ok{false};
    Failure fail;
    Submission submission;
    std::string token;                                // the maker's token (the one that was given, or a new one)
    bool token_is_new{false};
    bool name_taken{false};                           // the name is the name of a map already: the owner may change it or add a number (not an error)
};

/// The four lists of the owner page and their counts
struct ReviewCounts {
    size_t waiting{0}, approved{0}, declined{0}, taken_down{0};
};

class SubmissionStore {
public:
    /// `library_has` says whether a name key (names::name_key) is the name of a map of the library (the originals and the community folder)
    SubmissionStore(db::Database& database, SubmissionConfig config, std::function<bool(const std::string& key)> library_has);
    ~SubmissionStore();

    /// Makes the folders, reads or makes the address salt, and deletes the temporary files of an earlier crash. False with a sentence when the folder cannot be used.
    bool open(int64_t now, std::string& why);
    bool is_open() const { return open_; }
    const SubmissionConfig& config() const { return config_; }

    /// The size of the largest body that a submission of this kind may have (the limit that the door applies before it reads a byte)
    static size_t max_upload_bytes(const std::string& kind);

    SubmitOutcome submit(const SubmitInput& in, int64_t now);

    // the maker's side: the token's own submissions
    bool list_for_token(const std::string& token, std::vector<Submission>& out, Failure& why);
    bool get_for_token(const std::string& id, const std::string& token, Submission& out, Failure& why);
    bool withdraw(const std::string& id, const std::string& token, int64_t now, Submission& out, Failure& why);

    // the owner's side
    bool list(const std::string& state, std::vector<Submission>& out, Failure& why);
    ReviewCounts counts();
    bool get(const std::string& id, Submission& out);
    /// `name` is the owner's name for the map (empty: the maker's name); `message` an optional short note for the maker
    bool approve(const std::string& id, const std::string& name, const std::string& message, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why);
    bool decline(const std::string& id, const std::string& reason, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why);
    bool take_down(const std::string& id, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why);
    bool put_back(const std::string& id, const std::string& actor_id, const std::string& actor_name, int64_t now, Submission& out, Failure& why);
    /// The next free name for `name` ("Treasure" -> "Treasure 2", or "Treasure 3" when that is taken); false when the name is no map name
    bool next_name(const std::string& name, std::string& out, Failure& why);
    /// The PNG of a kept map (empty when the file is gone)
    std::string preview_png(const std::string& id);

    /// The approved maps, for the lobby's list (taken down ones are not in it)
    std::vector<ApprovedMap> approved_maps();
    /// Whether the set of approved maps changed since the last call (an approval, a take-down, a put-back)
    bool take_approved_changed();
    /// Deletes what is old (files of declined maps, rows of declined and withdrawn ones, the day counters); cheap, call it once an hour
    void maintenance(int64_t now);
    /// At open: moves the files of the submissions folder that no row owns (a crash between the rename of a file and the commit of its row, or a database that was reset) into submissions/orphans/, says how many in the notices, and deletes what has been there for 30 days
    void remove_orphans();
    /// Lines for the server's log (no address, no token, no text of a person)
    std::vector<std::string> take_notices();

    /// Whether a name key is taken: a library map, or an approved or taken-down submission
    bool name_taken(const std::string& key);

    SubmissionStore(const SubmissionStore&) = delete;
    SubmissionStore& operator=(const SubmissionStore&) = delete;

private:
    bool fetch_one(const std::string& where, const std::string& value, Submission& out);
    bool fetch_many(const std::string& where, const std::string& value, std::vector<Submission>& out);
    bool owns(const std::string& id, const std::string& token);
    int64_t scalar(const std::string& sql, const std::vector<std::string>& args);
    bool decide(const std::string& id, const char* from_state, Submission& out, Failure& why);
    void audit(int64_t now, const std::string& actor_id, const std::string& actor_name, const char* action, const std::string& id, const std::string& detail);
    bool database_failed(Failure& why, const char* where);
    std::string address_key(const std::string& address, int64_t day) const;

    db::Database& db_;
    SubmissionConfig config_;
    std::function<bool(const std::string& key)> library_has_;
    std::string salt_;
    std::vector<std::string> notices_;
    bool open_{false};
    bool approved_changed_{true};
};

/// The maker's view of a submission, and the owner's, as JSON (docs/MAP_SUBMISSIONS.md has the members)
ctl::JsonValue maker_view(const Submission& s);
ctl::JsonValue owner_view(const Submission& s, bool name_is_taken);

}  // namespace ants::server

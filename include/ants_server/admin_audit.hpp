#pragma once

// The log of what an administrator did on the admin page (docs/MAP_SUBMISSIONS.md, "The admin page"): one line for each decision on a map, with the administrator's username and reference (the hub's name for an account,
// never a Discord id: admin_pass.hpp), the action, the map's name and a short detail. It keeps 180 days and at most 20,000 lines. Nothing else about a visitor is kept: no address, no password, no session. One thread, like
// the rest of the server.

#include <cstdint>
#include <string>
#include <vector>

namespace ants::db {
class Database;
}

namespace ants::server {

/// Who does a thing, for the log: the username as the hub gave it (or the reference when it gave none) and the reference
struct Actor {
    std::string ref;
    std::string name;
};

struct AuditEntry {
    int64_t at{0};
    std::string actor, ref, action, target, detail;
};

struct AuditConfig {
    int64_t days{180};
    size_t max_lines{20000};
};

class AdminAudit {
public:
    AdminAudit(db::Database& database, AuditConfig config = AuditConfig());

    /// Whether the log's table is there. False with a sentence when the database cannot be used.
    bool open(std::string& why);
    bool is_open() const { return open_; }
    const AuditConfig& config() const { return config_; }

    /// One more line. A refused write is not an error of the request that was done (it is told once to the operator's log: take_notices).
    void record(const Actor& by, int64_t now, const char* action, const std::string& target, const std::string& detail);
    /// The newest lines first
    std::vector<AuditEntry> list(size_t limit);
    /// Deletes what is old; cheap, call it once an hour
    void maintenance(int64_t now);
    std::vector<std::string> take_notices();

private:
    db::Database& db_;
    AuditConfig config_;
    std::vector<std::string> notices_;
    bool open_{false};
};

}  // namespace ants::server

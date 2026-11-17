#include "ants_server/admin_audit.hpp"

#include "ants_db/database.hpp"

namespace ants::server {

AdminAudit::AdminAudit(db::Database& database, AuditConfig config) : db_(database), config_(config) {}

bool AdminAudit::open(std::string& why) {
    db::Statement st;
    if (!db_.prepare(st, "SELECT COUNT(*) FROM admin_audit").ok() || st.step() != db::Statement::Step::Row) {
        why = "the database has no log table";
        return false;
    }
    open_ = true;
    return true;
}

void AdminAudit::record(const Actor& by, int64_t now, const char* action, const std::string& target, const std::string& detail) {
    db::Statement st;
    const bool done = db_.prepare(st, "INSERT INTO admin_audit (at, actor, actor_ref, action, target, detail) VALUES (?, ?, ?, ?, ?, ?)").ok() && st.bind_int64(1, now).ok() && st.bind_text(2, by.name).ok() &&
                      st.bind_text(3, by.ref).ok() && st.bind_text(4, action).ok() && st.bind_text(5, target).ok() && st.bind_text(6, detail).ok() && st.step() == db::Statement::Step::Done;
    if (!done && notices_.size() < 100) notices_.push_back("admin log: the database refused a line");
}

std::vector<AuditEntry> AdminAudit::list(size_t limit) {
    std::vector<AuditEntry> out;
    db::Statement st;
    if (!db_.prepare(st, "SELECT at, actor, actor_ref, action, target, detail FROM admin_audit ORDER BY id DESC LIMIT ?").ok() || !st.bind_int64(1, static_cast<int64_t>(limit)).ok()) return out;
    while (st.step() == db::Statement::Step::Row) {
        AuditEntry e;
        e.at = st.column_int64(0);
        e.actor = st.column_text(1);
        e.ref = st.column_text(2);
        e.action = st.column_text(3);
        e.target = st.column_text(4);
        e.detail = st.column_text(5);
        out.push_back(std::move(e));
    }
    return out;
}

void AdminAudit::maintenance(int64_t now) {
    if (!open_) return;
    db::Statement old;
    if (!db_.prepare(old, "DELETE FROM admin_audit WHERE at < ?").ok() || !old.bind_int64(1, now - config_.days * 86400).ok() || old.step() != db::Statement::Step::Done) {
        if (notices_.size() < 100) notices_.push_back("admin log: the database refused to drop old lines");
    }
    db::Statement over;
    if (!db_.prepare(over, "DELETE FROM admin_audit WHERE id <= (SELECT id FROM admin_audit ORDER BY id DESC LIMIT 1 OFFSET ?)").ok() || !over.bind_int64(1, static_cast<int64_t>(config_.max_lines)).ok() || over.step() != db::Statement::Step::Done) {
        if (notices_.size() < 100) notices_.push_back("admin log: the database refused to trim the log");
    }
}

std::vector<std::string> AdminAudit::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

}  // namespace ants::server

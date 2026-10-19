#include "ants_server/admin_accounts.hpp"

#include <algorithm>
#include <functional>
#include <variant>

#include "ants_ctl/http.hpp"
#include "ants_db/database.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/secret.hpp"

namespace ants::server {

namespace {

constexpr char kPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";       // 56 characters: no I, O, l, o, 0, 1
constexpr size_t kAlphabetSize = sizeof(kPasswordAlphabet) - 1;
constexpr size_t kSaltBytes = 16;
constexpr size_t kHashBytes = 32;
constexpr uint32_t kMaxStoredRounds = 10000000;           // a stored hash that claims more is not one this program made
constexpr char kScheme[] = "pbkdf2-sha256";
constexpr size_t kMaxTypedName = 64;

using Arg = std::variant<int64_t, std::string>;

Failure failure(int status, const char* code, const char* message) {
    Failure f;
    f.status = status;
    f.code = code;
    f.message = message;
    return f;
}

bool bind_args(db::Statement& st, const std::vector<Arg>& args) {
    for (size_t i = 0; i < args.size(); ++i) {
        const int index = static_cast<int>(i) + 1;
        const bool ok = std::holds_alternative<int64_t>(args[i]) ? st.bind_int64(index, std::get<int64_t>(args[i])).ok() : st.bind_text(index, std::get<std::string>(args[i])).ok();
        if (!ok) return false;
    }
    return true;
}

// Runs one statement that returns no rows
bool run(db::Database& db, const std::string& sql, const std::vector<Arg>& args) {
    db::Statement st;
    return db.prepare(st, sql).ok() && bind_args(st, args) && st.step() == db::Statement::Step::Done;
}

// Calls `row` for each row; false when the statement fails
bool each(db::Database& db, const std::string& sql, const std::vector<Arg>& args, const std::function<void(db::Statement&)>& row) {
    db::Statement st;
    if (!db.prepare(st, sql).ok() || !bind_args(st, args)) return false;
    for (;;) {
        const auto step = st.step();
        if (step == db::Statement::Step::Failed) return false;
        if (step == db::Statement::Step::Done) return true;
        row(st);
    }
}

const char* const kUserColumns = "id, name, role, must_change, disabled, session_gen, created_at, last_login_at, created_by";

User read_user(db::Statement& st) {
    User u;
    u.id = st.column_int64(0);
    u.name = st.column_text(1);
    u.role = st.column_text(2);
    u.must_change = st.column_int64(3) != 0;
    u.disabled = st.column_int64(4) != 0;
    u.session_gen = st.column_int64(5);
    u.created_at = st.column_int64(6);
    u.last_login_at = st.column_int64(7);
    u.created_by = st.column_text(8);
    return u;
}

bool split_hash(std::string_view stored, std::string_view parts[4]) {
    size_t from = 0;
    for (int i = 0; i < 4; ++i) {
        const size_t dollar = stored.find('$', from);
        if (i < 3 ? dollar == std::string_view::npos : dollar != std::string_view::npos) return false;
        parts[i] = stored.substr(from, dollar == std::string_view::npos ? std::string_view::npos : dollar - from);
        from = dollar + 1;
    }
    return true;
}

bool parse_rounds(std::string_view text, uint32_t& out) {
    if (text.empty() || text.size() > 8) return false;
    uint64_t v = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v == 0 || v > kMaxStoredRounds) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ passwords

std::string password_hash(std::string_view password, uint32_t rounds) {
    if (rounds == 0 || rounds > kMaxStoredRounds) return std::string();
    uint8_t salt[kSaltBytes];
    if (!random_bytes(salt, sizeof salt)) return std::string();
    const std::string salt_text(reinterpret_cast<const char*>(salt), sizeof salt);
    std::string key;
    if (!pbkdf2_hmac_sha256(password, salt_text, rounds, kHashBytes, key)) return std::string();
    return std::string(kScheme) + "$" + std::to_string(rounds) + "$" + base64url_encode(salt_text) + "$" + base64url_encode(key);
}

bool password_verify(std::string_view password, std::string_view stored, uint32_t rounds, bool* outdated) {
    if (outdated != nullptr) *outdated = false;
    std::string_view parts[4];
    if (!split_hash(stored, parts) || parts[0] != kScheme) return false;
    uint32_t stored_rounds = 0;
    std::string salt, want;
    if (!parse_rounds(parts[1], stored_rounds) || !base64url_decode(parts[2], salt) || !base64url_decode(parts[3], want)) return false;
    if (salt.size() < 8 || salt.size() > 64 || want.size() != kHashBytes) return false;
    std::string got;
    if (!pbkdf2_hmac_sha256(password, salt, stored_rounds, kHashBytes, got)) return false;
    if (!ctl::constant_time_equal(got, want)) return false;
    if (outdated != nullptr) *outdated = stored_rounds < rounds;
    return true;
}

std::string generate_password() {
    std::string out;
    while (out.size() < kStartingPasswordChars) {
        uint8_t bytes[64];
        if (!random_bytes(bytes, sizeof bytes)) return std::string();
        for (const uint8_t b : bytes) {
            if (b >= 224) continue;                                       // (224 = 4 * 56: every character is equally likely)
            out.push_back(kPasswordAlphabet[b % kAlphabetSize]);
            if (out.size() == kStartingPasswordChars) break;
        }
    }
    return out;
}

std::string password_problem(std::string_view password, std::string_view username) {
    if (password.size() > kPasswordMaxBytes) return "too_long";
    std::vector<char32_t> chars;
    if (!names::decode_utf8(password, chars)) return "characters";
    for (const char32_t c : chars) {
        if (c < 0x20 || (c >= 0x7F && c < 0xA0)) return "characters";
    }
    if (chars.size() < kPasswordMinChars) return "too_short";
    if (username_key(password) == username_key(username)) return "same_as_name";
    return std::string();
}

bool clean_username(std::string_view typed, std::string& out, std::string& reason) {
    out.clear();
    reason.clear();
    if (typed.size() > 32) {
        reason = "too_long";
        return false;
    }
    for (const char c : typed) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) {
            reason = "characters";
            return false;
        }
    }
    if (typed.size() < 3) {
        reason = "too_short";
        return false;
    }
    out.assign(typed);
    return true;
}

std::string username_key(std::string_view name) {
    std::string out(name);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ the store

AdminAccounts::AdminAccounts(db::Database& database, AccountConfig config) : db_(database), config_(config) {}

bool AdminAccounts::open(std::string& why) {
    db::Statement st;
    if (!db_.prepare(st, "SELECT value FROM meta WHERE key = 'admin_salt'").ok()) {
        why = "the database has no meta table";
        return false;
    }
    if (st.step() == db::Statement::Step::Row) {
        salt_ = st.column_text(0);
    } else {
        const std::string fresh = generate_secret_text();
        if (fresh.empty()) {
            why = "the operating system's random number generator is not available";
            return false;
        }
        if (!run(db_, "INSERT INTO meta (key, value) VALUES ('admin_salt', ?)", {fresh})) {
            why = "the database cannot be written";
            return false;
        }
        salt_ = fresh;
    }
    const std::string unknown = generate_password();
    dummy_hash_ = password_hash(unknown, config_.rounds);
    if (dummy_hash_.empty()) {
        why = "the operating system's random number generator is not available";
        return false;
    }
    const int64_t rows = count("SELECT COUNT(*) FROM admin_locks");
    if (rows < 0) {
        why = "the database has no accounts tables";
        return false;
    }
    lock_rows_ = static_cast<size_t>(rows);
    open_ = true;
    return true;
}

std::string AdminAccounts::keyed(const char* scope, const std::string& text) const { return hex_encode(hmac_sha256(salt_, std::string(scope) + "|" + text)).substr(0, 32); }

std::string AdminAccounts::address_hash(const std::string& identity) const { return identity.empty() ? std::string() : keyed("addr", identity); }

int64_t AdminAccounts::count(const std::string& sql) {
    db::Statement st;
    if (!db_.prepare(st, sql).ok()) return -1;
    return st.step() == db::Statement::Step::Row ? st.column_int64(0) : -1;
}

bool AdminAccounts::fail_db(Failure& why, const char* where) {
    notices_.push_back(std::string("accounts: the database refused ") + where);
    why = failure(503, "storage_error", "The server could not save this just now. Please try again later.");
    return false;
}

std::vector<std::string> AdminAccounts::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

bool AdminAccounts::find(int64_t id, User& out) {
    bool found = false;
    each(db_, std::string("SELECT ") + kUserColumns + " FROM admin_users WHERE id = ?", {id}, [&](db::Statement& st) {
        out = read_user(st);
        found = true;
    });
    return found;
}

std::vector<User> AdminAccounts::list() {
    std::vector<User> out;
    each(db_, std::string("SELECT ") + kUserColumns + " FROM admin_users ORDER BY id", {}, [&](db::Statement& st) { out.push_back(read_user(st)); });
    return out;
}

bool AdminAccounts::any_admin(bool& ok) {
    const int64_t n = count("SELECT COUNT(*) FROM admin_users WHERE role = 'admin'");
    ok = n >= 0;
    return n > 0;
}

// the admins other than `excluding_id` that are enabled
bool AdminAccounts::admin_count_ok(int64_t excluding_id, int64_t& others) {
    int64_t n = -1;
    each(db_, "SELECT COUNT(*) FROM admin_users WHERE role = 'admin' AND disabled = 0 AND id <> ?", {excluding_id}, [&](db::Statement& st) { n = st.column_int64(0); });
    others = n;
    return n >= 0;
}

// ------------------------------------------------------------------------------------------------ the log

void AdminAccounts::audit(const Actor& by, int64_t now, const char* action, const std::string& target, const std::string& detail) {
    run(db_, "INSERT INTO admin_audit (at, actor, actor_id, action, target, detail, addr_key) VALUES (?, ?, ?, ?, ?, ?, ?)",
        {now, by.name, by.id, std::string(action), target, detail, address_hash(by.address)});
    const std::string a = action;
    if (a == "sign_in_failed" || a == "locked") {                       // (a flood of failures cannot push the real actions out of the log)
        run(db_, "DELETE FROM admin_audit WHERE action IN ('sign_in_failed', 'locked') AND id <= (SELECT id FROM admin_audit WHERE action IN ('sign_in_failed', 'locked') ORDER BY id DESC LIMIT 1 OFFSET ?)",
            {static_cast<int64_t>(config_.audit_failed_max)});
    }
}

std::vector<AuditEntry> AdminAccounts::audit_list(size_t limit) {
    std::vector<AuditEntry> out;
    each(db_, "SELECT at, actor, action, target, detail FROM admin_audit ORDER BY id DESC LIMIT ?", {static_cast<int64_t>(limit)}, [&](db::Statement& st) {
        AuditEntry e;
        e.at = st.column_int64(0);
        e.actor = st.column_text(1);
        e.action = st.column_text(2);
        e.target = st.column_text(3);
        e.detail = st.column_text(4);
        out.push_back(e);
    });
    return out;
}

void AdminAccounts::maintenance(int64_t now) {
    if (!open_) return;
    run(db_, "DELETE FROM admin_audit WHERE at < ?", {now - config_.audit_days * 86400});
    run(db_, "DELETE FROM admin_audit WHERE id <= (SELECT id FROM admin_audit ORDER BY id DESC LIMIT 1 OFFSET ?)", {static_cast<int64_t>(config_.audit_max)});
    run(db_, "DELETE FROM admin_locks WHERE locked_until <= ? AND first_at + ? <= ?", {now, config_.window_seconds, now});
    run(db_, "DELETE FROM admin_known_addr WHERE last_ok < ?", {now - config_.known_days * 86400});
    const int64_t rows = count("SELECT COUNT(*) FROM admin_locks");
    if (rows >= 0) lock_rows_ = static_cast<size_t>(rows);
}

// ------------------------------------------------------------------------------------------------ guessing

AdminAccounts::LockRow AdminAccounts::lock_row(const char* scope, const std::string& key) {
    LockRow r;
    each(db_, "SELECT fails, first_at, locked_until FROM admin_locks WHERE scope = ? AND key = ?", {std::string(scope), key}, [&](db::Statement& st) {
        r.found = true;
        r.fails = st.column_int64(0);
        r.first_at = st.column_int64(1);
        r.locked_until = st.column_int64(2);
    });
    return r;
}

// One more wrong try in this row's window. With `lock`, reaching `tries` locks the row for a window. True when this try locked it.
bool AdminAccounts::bump(const char* scope, const std::string& key, const std::string& user_ref, int64_t now, int tries, bool lock) {
    const LockRow r = lock_row(scope, key);
    int64_t fails = 1, first = now;
    if (r.found && r.first_at + config_.window_seconds > now) {
        fails = r.fails + 1;
        first = r.first_at;
    }
    int64_t until = 0;
    bool locked_now = false;
    if (lock && fails >= tries) {
        until = now + config_.window_seconds;
        locked_now = true;
    }
    if (run(db_, "INSERT OR REPLACE INTO admin_locks (scope, key, user_ref, fails, first_at, locked_until) VALUES (?, ?, ?, ?, ?, ?)", {std::string(scope), key, user_ref, fails, first, until}) && !r.found) ++lock_rows_;
    return locked_now;
}

void AdminAccounts::clear_locks_of(const std::string& user_ref) {
    run(db_, "DELETE FROM admin_locks WHERE user_ref = ?", {user_ref});
    const int64_t rows = count("SELECT COUNT(*) FROM admin_locks");
    if (rows >= 0) lock_rows_ = static_cast<size_t>(rows);
}

bool AdminAccounts::lock_table_full(int64_t now) {
    if (lock_rows_ < config_.lock_rows_max) return false;
    run(db_, "DELETE FROM admin_locks WHERE locked_until <= ? AND first_at + ? <= ?", {now, config_.window_seconds, now});
    const int64_t rows = count("SELECT COUNT(*) FROM admin_locks");
    if (rows >= 0) lock_rows_ = static_cast<size_t>(rows);
    return lock_rows_ >= config_.lock_rows_max;
}

bool AdminAccounts::take_check(bool reserved, uint32_t now_ms) {
    if (!window_started_ || static_cast<uint32_t>(now_ms - window_start_ms_) >= config_.hash_window_ms) {
        window_started_ = true;
        window_start_ms_ = now_ms;
        window_used_ = 0;
        window_reserved_used_ = 0;
    }
    if (window_used_ < config_.checks_per_window) {
        ++window_used_;
        return true;
    }
    if (reserved && window_reserved_used_ < 1) {                 // (the place kept for an address that signed this user in before)
        ++window_reserved_used_;
        return true;
    }
    return false;
}

bool AdminAccounts::known_address(int64_t id, const std::string& addr, int64_t now) {
    int64_t n = 0;
    each(db_, "SELECT COUNT(*) FROM admin_known_addr WHERE user_id = ? AND addr_key = ? AND last_ok >= ?", {id, addr, now - config_.known_days * 86400}, [&](db::Statement& st) { n = st.column_int64(0); });
    return n > 0;
}

void AdminAccounts::remember_address(int64_t id, const std::string& addr, int64_t now) {
    run(db_, "INSERT OR REPLACE INTO admin_known_addr (user_id, addr_key, last_ok) VALUES (?, ?, ?)", {id, addr, now});
    run(db_, "DELETE FROM admin_known_addr WHERE user_id = ? AND addr_key NOT IN (SELECT addr_key FROM admin_known_addr WHERE user_id = ? ORDER BY last_ok DESC, addr_key LIMIT ?)",
        {id, id, static_cast<int64_t>(config_.known_addresses)});
}

// The one place where a password is checked. Whether the name exists or not, the same locks apply, the same work is done and the same answers come back.
Attempt AdminAccounts::attempt(const std::string& typed_name, const User* user, const std::string& password, const std::string& identity, int64_t now, uint32_t now_ms, User* out) {
    if (!open_) return Attempt::Error;
    const std::string name_key = username_key(typed_name.substr(0, kMaxTypedName));
    const std::string addr = address_hash(identity);
    const std::string user_ref = keyed("user", name_key);
    const std::string pair_key = keyed("pair", name_key + "|" + addr);
    if (lock_table_full(now)) return Attempt::Locked;
    const LockRow addr_row = lock_row("addr", addr);
    if (addr_row.locked_until > now) return Attempt::Locked;
    const LockRow pair_row = lock_row("pair", pair_key);
    if (pair_row.locked_until > now) return Attempt::Locked;
    const bool usable = user != nullptr && !user->disabled;
    const bool known = usable && known_address(user->id, addr, now);
    const LockRow user_row = lock_row("user", user_ref);
    const bool slow = user_row.found && user_row.fails >= config_.user_tries && user_row.first_at + config_.window_seconds > now;
    if (slow && !known) return Attempt::Locked;
    if (!take_check(known, now_ms)) return Attempt::Busy;

    std::string stored;
    if (usable) {
        each(db_, "SELECT pw_hash FROM admin_users WHERE id = ?", {user->id}, [&](db::Statement& st) { stored = st.column_text(0); });
    }
    bool outdated = false;
    bool match = false;
    if (usable && !stored.empty()) match = password_verify(password, stored, config_.rounds, &outdated);
    else password_verify(password, dummy_hash_, config_.rounds, nullptr);                     // (an unknown or disabled name costs the same work)
    if (match) {
        run(db_, "DELETE FROM admin_locks WHERE (scope = 'pair' AND key = ?) OR (scope = 'user' AND key = ?)", {pair_key, user_ref});
        remember_address(user->id, addr, now);
        if (outdated) {
            const std::string fresh = password_hash(password, config_.rounds);
            if (!fresh.empty()) run(db_, "UPDATE admin_users SET pw_hash = ? WHERE id = ?", {fresh, user->id});
        }
        if (out != nullptr) *out = *user;
        return Attempt::Ok;
    }
    const bool pair_locked = bump("pair", pair_key, user_ref, now, config_.pair_tries, true);
    const bool addr_locked = bump("addr", addr, std::string(), now, config_.address_tries, true);
    bump("user", user_ref, user_ref, now, 0, false);
    Actor who;
    who.address = identity;
    if (user != nullptr) {
        who.name = user->name;
        who.id = user->id;
        audit(who, now, "sign_in_failed", std::string(), std::string());
    }
    if (pair_locked || addr_locked) {
        std::string shown, reason;
        who.id = 0;
        who.name = clean_username(typed_name, shown, reason) ? shown : std::string("(not a username)");
        audit(who, now, "locked", std::string(), pair_locked ? "wrong tries of one name from one address" : "wrong tries from one address");
        return Attempt::Locked;
    }
    return Attempt::Wrong;
}

Attempt AdminAccounts::sign_in(const std::string& typed_name, const std::string& password, const std::string& identity, int64_t now, uint32_t now_ms, User& user) {
    User found;
    bool exists = false;
    const std::string key = username_key(typed_name.substr(0, kMaxTypedName));
    each(db_, std::string("SELECT ") + kUserColumns + " FROM admin_users WHERE name_key = ?", {key}, [&](db::Statement& st) {
        found = read_user(st);
        exists = true;
    });
    const Attempt result = attempt(typed_name, exists ? &found : nullptr, password, identity, now, now_ms, &user);
    if (result == Attempt::Ok) {
        run(db_, "UPDATE admin_users SET last_login_at = ? WHERE id = ?", {now, user.id});
        user.last_login_at = now;
        Actor who;
        who.id = user.id;
        who.name = user.name;
        who.address = identity;
        audit(who, now, "sign_in", std::string(), std::string());
    }
    return result;
}

Attempt AdminAccounts::confirm_password(const User& user, const std::string& password, const std::string& identity, int64_t now, uint32_t now_ms) {
    return attempt(user.name, &user, password, identity, now, now_ms, nullptr);
}

bool AdminAccounts::password_is(const User& user, const std::string& password) {
    std::string stored;
    each(db_, "SELECT pw_hash FROM admin_users WHERE id = ?", {user.id}, [&](db::Statement& st) { stored = st.column_text(0); });
    return !stored.empty() && password_verify(password, stored, config_.rounds, nullptr);
}

// ------------------------------------------------------------------------------------------------ changes

bool AdminAccounts::create(const Actor& by, const std::string& typed_name, const std::string& role, int64_t now, User& out, std::string& password, Failure& why) {
    std::string name, reason;
    if (!clean_username(typed_name, name, reason)) {
        why = failure(400, "bad_username", reason == "too_short" ? "A username has at least 3 characters." : reason == "too_long" ? "A username has at most 32 characters." : "A username may hold letters, digits, dots, dashes and underscores only.");
        why.extra = ctl::JsonValue::make_object();
        why.extra.set("reason", ctl::JsonValue::make_string(reason));
        return false;
    }
    if (role != "admin" && role != "moderator") {
        why = failure(400, "bad_role", "The role is Admin or Moderator.");
        return false;
    }
    int64_t taken = -1;
    each(db_, "SELECT COUNT(*) FROM admin_users WHERE name_key = ?", {username_key(name)}, [&](db::Statement& st) { taken = st.column_int64(0); });
    if (taken < 0) return fail_db(why, "a read");
    if (taken > 0) {
        why = failure(409, "username_taken", "That username is taken already.");
        return false;
    }
    password = generate_password();
    const std::string hash = password.empty() ? std::string() : password_hash(password, config_.rounds);
    if (hash.empty()) {
        password.clear();
        why = failure(503, "no_random", "The server could not make a random password just now.");
        return false;
    }
    if (!run(db_, "INSERT INTO admin_users (name, name_key, role, pw_hash, must_change, disabled, session_gen, created_at, created_by) VALUES (?, ?, ?, ?, 1, 0, 1, ?, ?)",
             {name, username_key(name), role, hash, now, by.name})) {
        password.clear();
        return fail_db(why, "a write");
    }
    audit(by, now, "user_added", name, role);
    return find(db_.last_insert_rowid(), out) || fail_db(why, "a read");
}

bool AdminAccounts::create_first_admin(const std::string& typed_name, int64_t now, User& out, std::string& password, Failure& why) {
    bool ok = false;
    const bool any = any_admin(ok);
    if (!ok) return fail_db(why, "a read");
    if (any) {
        why = failure(409, "admin_exists", "An Admin exists already. Sign in at /admin, or use the reset command for a forgotten password.");
        return false;
    }
    return create(Actor(), typed_name, "admin", now, out, password, why);
}

bool AdminAccounts::reset(const Actor& by, int64_t id, int64_t now, std::string& password, Failure& why, bool console) {
    User user;
    if (!find(id, user)) {
        why = failure(404, "not_found", "There is no such user.");
        return false;
    }
    password = generate_password();
    const std::string hash = password.empty() ? std::string() : password_hash(password, config_.rounds);
    if (hash.empty()) {
        password.clear();
        why = failure(503, "no_random", "The server could not make a random password just now.");
        return false;
    }
    if (!run(db_, std::string("UPDATE admin_users SET pw_hash = ?, must_change = 1, session_gen = session_gen + 1") + (console ? ", disabled = 0" : "") + " WHERE id = ?", {hash, id})) {
        password.clear();
        return fail_db(why, "a write");
    }
    clear_locks_of(keyed("user", username_key(user.name)));
    run(db_, "DELETE FROM admin_known_addr WHERE user_id = ?", {id});
    audit(by, now, "password_reset", user.name, std::string());
    return true;
}

bool AdminAccounts::set_disabled(const Actor& by, int64_t id, bool disabled, int64_t now, Failure& why) {
    User user;
    if (!find(id, user)) {
        why = failure(404, "not_found", "There is no such user.");
        return false;
    }
    if (disabled && user.role == "admin" && !user.disabled) {
        int64_t others = 0;
        if (!admin_count_ok(id, others)) return fail_db(why, "a read");
        if (others == 0) {
            why = failure(409, "last_admin", "This is the last Admin, so this account cannot be disabled. Add another Admin first.");
            return false;
        }
    }
    if (!run(db_, "UPDATE admin_users SET disabled = ?, session_gen = session_gen + 1 WHERE id = ?", {disabled ? int64_t{1} : int64_t{0}, id})) return fail_db(why, "a write");
    audit(by, now, disabled ? "user_disabled" : "user_enabled", user.name, std::string());
    return true;
}

bool AdminAccounts::remove(const Actor& by, int64_t id, int64_t now, Failure& why) {
    User user;
    if (!find(id, user)) {
        why = failure(404, "not_found", "There is no such user.");
        return false;
    }
    if (user.role == "admin" && !user.disabled) {
        int64_t others = 0;
        if (!admin_count_ok(id, others)) return fail_db(why, "a read");
        if (others == 0) {
            why = failure(409, "last_admin", "This is the last Admin, so this account cannot be removed. Add another Admin first.");
            return false;
        }
    }
    if (!run(db_, "DELETE FROM admin_users WHERE id = ?", {id})) return fail_db(why, "a write");
    clear_locks_of(keyed("user", username_key(user.name)));
    run(db_, "DELETE FROM admin_known_addr WHERE user_id = ?", {id});
    audit(by, now, "user_removed", user.name, std::string());
    return true;
}

bool AdminAccounts::change_password(const Actor& by, int64_t id, const std::string& new_password, int64_t now, int64_t& gen, Failure& why) {
    User user;
    if (!find(id, user)) {
        why = failure(404, "not_found", "There is no such user.");
        return false;
    }
    const std::string hash = password_hash(new_password, config_.rounds);
    if (hash.empty()) {
        why = failure(503, "no_random", "The server could not save a password just now.");
        return false;
    }
    if (!run(db_, "UPDATE admin_users SET pw_hash = ?, must_change = 0, session_gen = session_gen + 1 WHERE id = ?", {hash, id})) return fail_db(why, "a write");
    gen = user.session_gen + 1;
    audit(by, now, "password_changed", user.name, std::string());
    return true;
}

}  // namespace ants::server

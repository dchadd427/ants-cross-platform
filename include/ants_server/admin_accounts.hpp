#pragma once

// The accounts of the owner's page (docs/MAP_SUBMISSIONS.md, "Accounts"): a username and a password, two roles (Admin: everything, including the users; Moderator: approve, decline, take down and
// put back maps), a log of who did what, and the rules against guessing. This is the store and the rules; the routes, the cookie and the CSRF token are submissions_http.hpp. One thread, like the
// rest of the server.
//
// What this file promises:
//  * A password is kept only as a salted, slow hash: PBKDF2-HMAC-SHA-256 (RFC 8018, built on the program's own tested SHA-256 and HMAC: crypto.hpp), 600,000 rounds, a random 16-byte salt for each
//    password, written as "pbkdf2-sha256$<rounds>$<salt>$<hash>" so that a later change of the cost is seen and the hash is made again at the next good sign-in. The password itself is never
//    stored, logged, or put in the audit log. Hashes are compared in constant time.
//  * An unknown user, a disabled user and a wrong password cost the same work (the unknown name is checked against a hash made at start) and get the same answer.
//  * Random passwords (20 characters from an alphabet without look-alikes, about 116 bits) come from the operating system's generator only; when it fails, nothing is made.
//  * Guessing is limited three ways, all kept in the database so that a restart does not forget them (tables admin_locks, admin_known_addr):
//      - one user from one address: 5 wrong tries lock that pair for 10 minutes (an unknown name is locked the same way, so the lock tells nothing about which names exist);
//      - one address: 20 wrong tries, whatever the names, lock the address for 10 minutes;
//      - one user from everywhere: after 10 wrong tries in 10 minutes only an address that signed that user in before (in the last 30 days, at most 10 are remembered) is let through, so a
//        stranger cannot lock the real person out by failing from far away, and a crowd of strangers still cannot guess faster than the 5 a pair allows.
//    A password check costs about a third of a second of the one thread, so the number of checks in a time window is capped (a flood is answered "busy"; one place in the window is kept for
//    an address that signed the user in before).
//  * Roles never change after the account is made. The last enabled Admin can be neither disabled nor removed. A reset, a disable, a removal and a password change end every session of that user
//    (the user's session generation goes up; a cookie carries the generation it was made at).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ants_server/submissions.hpp"

namespace ants::db {
class Database;
}

namespace ants::server {

// ------------------------------------------------------------------------------------------------ passwords

constexpr uint32_t kPasswordRounds = 600000;       // PBKDF2 rounds of a new hash
constexpr size_t kPasswordMinChars = 12;
constexpr size_t kPasswordMaxBytes = 256;
constexpr size_t kStartingPasswordChars = 20;

/// "pbkdf2-sha256$<rounds>$<salt>$<hash>" for the password (a fresh random salt). Empty when the operating system's random generator fails or `rounds` is 0.
std::string password_hash(std::string_view password, uint32_t rounds);
/// Whether the password makes the stored hash. A text that is not a hash of this format never matches. `outdated` (optional) is set when it matches but was made with fewer rounds than `rounds`.
bool password_verify(std::string_view password, std::string_view stored, uint32_t rounds, bool* outdated = nullptr);
/// A random starting password: kStartingPasswordChars characters of "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789" (no I, O, l, o, 0, 1). Empty when the generator fails.
std::string generate_password();
/// "": the password is acceptable as a new one; else why not: "too_short" (under 12 characters), "too_long", "characters" (not UTF-8, or a control character), "same_as_name"
std::string password_problem(std::string_view password, std::string_view username);
/// A username as it is kept: 3 to 32 of letters, digits, dot, dash and underscore. "" and a reason ("too_short", "too_long", "characters") when it is not one.
bool clean_username(std::string_view typed, std::string& out, std::string& reason);
/// The key that makes two names the same name: ASCII letters in lower case
std::string username_key(std::string_view name);

// ------------------------------------------------------------------------------------------------ accounts

struct AccountConfig {
    uint32_t rounds{kPasswordRounds};
    int64_t window_seconds{600};              // how long a count of wrong tries is kept, and how long a lock lasts
    int pair_tries{5};                        // wrong tries of one user from one address that lock the pair
    int address_tries{20};                    // wrong tries from one address (any names) that lock the address
    int user_tries{10};                       // wrong tries of one user from everywhere after which only a known address is let through
    size_t lock_rows_max{50000};              // rows of admin_locks; a table that is full of live rows answers "too many tries" to everybody new (it takes thousands of addresses to fill it)
    size_t checks_per_window{3};              // password checks in a window of hash_window_ms for the whole server (a check costs the one thread about 0.3 s)
    uint32_t hash_window_ms{10000};
    size_t known_addresses{10};               // how many addresses are remembered for each user
    int64_t known_days{30};
    int64_t audit_days{180};
    size_t audit_max{20000};
    size_t audit_failed_max{1000};            // failed sign-ins kept (a flood of them cannot push the real actions out)
};

struct User {
    int64_t id{0};
    std::string name;                         // as typed when it was made
    std::string role;                         // "admin" or "moderator"
    bool must_change{false};                  // the password is still the starting one: the session can only choose a new password
    bool disabled{false};
    int64_t session_gen{1};
    int64_t created_at{0};
    int64_t last_login_at{0};                 // 0: never
    std::string created_by;
};

/// Who does a thing, for the log: a user (id, name) or the server's console (id 0, name "console"); `address` is the identity text of the request (hashed before it is written)
struct Actor {
    int64_t id{0};
    std::string name{"console"};
    std::string address;
};

enum class Attempt { Ok, Wrong, Locked, Busy, Error };

struct AuditEntry {
    int64_t at{0};
    std::string actor, action, target, detail;
};

class AdminAccounts {
public:
    AdminAccounts(db::Database& database, AccountConfig config = AccountConfig());

    /// Reads or makes the salt that keys the address hashes, makes the hash that unknown names are checked against. False with a sentence when the database cannot be used.
    bool open(std::string& why);
    bool is_open() const { return open_; }
    /// How many passwords were checked by a sign-in or a confirmation (a test holds that an unknown name costs the same as a known one)
    size_t verifies() const { return verifies_; }
    const AccountConfig& config() const { return config_; }

    /// The identity of a request (client_identity) as a keyed hash, never the address
    std::string address_hash(const std::string& identity) const;

    // sign in, and every other check of a password
    /// A sign-in. `identity` is the request's address identity. Ok fills `user`; Wrong is "wrong username or password" (unknown, disabled and wrong password alike); Locked is "too many tries";
    /// Busy is the server's budget for password checks (try again in a minute); Error is a database that failed.
    Attempt sign_in(const std::string& typed_name, const std::string& password, const std::string& identity, int64_t now, uint32_t now_ms, User& user);
    /// The same checks for a signed-in user who proves the current password (changing it): wrong tries count toward the same locks
    Attempt confirm_password(const User& user, const std::string& password, const std::string& identity, int64_t now, uint32_t now_ms);
    /// Whether the password is the one kept for this user (no lock, no count: for the rule "not the starting password" on a session that already signed in)
    bool password_is(const User& user, const std::string& password);

    bool find(int64_t id, User& out);
    std::vector<User> list();
    /// Whether any Admin exists (disabled or not); false also when the database fails (`ok` says)
    bool any_admin(bool& ok);

    // changes: each one is logged; `why` explains a refusal
    bool create(const Actor& by, const std::string& typed_name, const std::string& role, int64_t now, User& out, std::string& password, Failure& why);
    /// A new random password; the user must choose another at the next sign-in; every session of the user ends; the locks of the user are cleared. `console` also enables a disabled account (the server's console is the way back in)
    bool reset(const Actor& by, int64_t id, int64_t now, std::string& password, Failure& why, bool console = false);
    bool set_disabled(const Actor& by, int64_t id, bool disabled, int64_t now, Failure& why);
    bool remove(const Actor& by, int64_t id, int64_t now, Failure& why);
    /// The user's own new password (checked with password_problem by the caller): ends every session, clears must_change. `gen` receives the new session generation.
    bool change_password(const Actor& by, int64_t id, const std::string& new_password, int64_t now, int64_t& gen, Failure& why);
    /// The first Admin, from the server's console: refused when an Admin exists already
    bool create_first_admin(const std::string& typed_name, int64_t now, User& out, std::string& password, Failure& why);

    // the log
    void audit(const Actor& by, int64_t now, const char* action, const std::string& target, const std::string& detail);
    std::vector<AuditEntry> audit_list(size_t limit);
    /// Deletes what is old (log lines, expired locks, forgotten addresses); cheap, call it once an hour
    void maintenance(int64_t now);
    std::vector<std::string> take_notices();

private:
    struct LockRow {
        bool found{false};
        int64_t fails{0}, first_at{0}, locked_until{0};
    };
    Attempt attempt(const std::string& typed_name, const User* user, const std::string& password, const std::string& identity, int64_t now, uint32_t now_ms, User* out);
    LockRow lock_row(const char* scope, const std::string& key);
    bool bump(const char* scope, const std::string& key, const std::string& user_ref, int64_t now, int tries, bool lock);
    void clear_locks_of(const std::string& user_ref);
    bool lock_table_full(int64_t now);
    bool take_check(bool reserved, uint32_t now_ms);
    bool known_address(int64_t id, const std::string& addr, int64_t now);
    void remember_address(int64_t id, const std::string& addr, int64_t now);
    std::string keyed(const char* scope, const std::string& text) const;
    int64_t count(const std::string& sql);
    bool admin_count_ok(int64_t excluding_id, int64_t& others);
    bool fail_db(Failure& why, const char* where);

    db::Database& db_;
    AccountConfig config_;
    std::string salt_;
    std::string dummy_hash_;
    std::vector<std::string> notices_;
    bool open_{false};
    uint32_t window_start_ms_{0};
    size_t window_used_{0}, window_reserved_used_{0};
    bool window_started_{false};
    size_t lock_rows_{0};
    size_t verifies_{0};
};

}  // namespace ants::server

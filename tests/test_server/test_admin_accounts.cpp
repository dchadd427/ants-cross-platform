// Tests of the accounts of the owner's page (admin_accounts.hpp, admin_cli.hpp, docs/MAP_SUBMISSIONS.md "Accounts"): PBKDF2 against published vectors, the hash format and its upgrade, the random password, the
// rules for names and passwords, the work an unknown name costs, the three limits on guessing, the generation of the sessions, the guard of the last Admin, the log, that no password or address is kept, the
// migration of a copy of a version 5 database, and the command line that makes the first Admin. The routes (roles, the forced change, the user pages) are test_admin_api.cpp. No sockets.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include "../common/test_assert.hpp"
#include "ants_server/admin_accounts.hpp"
#include "ants_server/admin_cli.hpp"
#include "ants_server/crypto.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define ASSERT_MSG(cond, msg) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n    " << (msg) << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

constexpr uint32_t kFast = 200;                         // rounds in the tests that do not test the cost

std::string pbkdf2_hex(const std::string& password, const std::string& salt, uint32_t rounds, size_t length) {
    std::string out;
    return pbkdf2_hmac_sha256(password, salt, rounds, length, out) ? hex_encode(out) : std::string("<refused>");
}

// An account store on a memory database, with a clock the test moves
struct Accounts {
    db::Database database;
    std::unique_ptr<AdminAccounts> accounts;
    int64_t now{kT0};
    uint32_t now_ms{1000};

    explicit Accounts(const std::function<void(AccountConfig&)>& tune = nullptr, const std::string& path = "") {
        open_server_db(database, path);
        AccountConfig config;
        config.rounds = kFast;
        config.checks_per_window = 100000;                // (the budget has its own test)
        if (tune) tune(config);
        accounts = std::make_unique<AdminAccounts>(database, config);
        std::string why;
        accounts->open(why);
    }

    // a user whose password is chosen already (the state of an account in use)
    User add(const std::string& name, const std::string& role, const std::string& password = "a long password of my own") {
        User u;
        std::string starting;
        Failure why;
        if (!accounts->create(Actor(), name, role, now, u, starting, why)) return User();
        int64_t gen = 0;
        accounts->change_password(Actor(), u.id, password, now, gen, why);
        accounts->find(u.id, u);
        return u;
    }

    Attempt sign_in(const std::string& name, const std::string& password, const std::string& address = "198.51.100.7", User* out = nullptr) {
        User u;
        const Attempt a = accounts->sign_in(name, password, address, now, now_ms, u);
        if (out != nullptr) *out = u;
        return a;
    }

    int64_t scalar(const std::string& sql) {
        db::Statement st;
        if (!database.prepare(st, sql).ok()) return -1;
        return st.step() == db::Statement::Step::Row ? st.column_int64(0) : -1;
    }
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    std::cout << "=======================================================\n THE ACCOUNTS OF THE OWNER'S PAGE\n=======================================================\n";

    TEST_CASE("ACC1.1 PBKDF2-HMAC-SHA-256 gives the published vectors (RFC 7914 section 11 and the usual ones), for short and long keys and salts and for more than one block") {
        ASSERT_EQ(pbkdf2_hex("passwd", "salt", 1, 64), std::string("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"));
        ASSERT_EQ(pbkdf2_hex("Password", "NaCl", 80000, 64), std::string("4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d"));
        ASSERT_EQ(pbkdf2_hex("password", "salt", 1, 32), std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
        ASSERT_EQ(pbkdf2_hex("password", "salt", 2, 32), std::string("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
        ASSERT_EQ(pbkdf2_hex("password", "salt", 4096, 32), std::string("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
        ASSERT_EQ(pbkdf2_hex("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40), std::string("348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9"));
        ASSERT_EQ(pbkdf2_hex(std::string(100, 'p'), std::string(70, 's'), 3, 100),
                  std::string("3584195e1fd8367d0db04e5d443515cfb23a463bd6d3e2db6a1b2b277f2de0c7c482407c70b71ebdf9485296f1cda6e8c4f6fbbf01e4456f3ff5015fa7eb4c5ce2d59136d76d63ab33213aa32081403e615f878ce7493277dfb8c69d52d7d20b1f495561"));
        ASSERT_EQ(pbkdf2_hex("", "", 1, 32), std::string("f7ce0b653d2d72a4108cf5abe912ffdd777616dbbb27a70e8204f3ae2d0f6fad"));
        ASSERT_EQ(pbkdf2_hex("password", "salt", 0, 32), std::string("<refused>"));                 // (no rounds, no length, a silly length)
        ASSERT_EQ(pbkdf2_hex("password", "salt", 1, 0), std::string("<refused>"));
        ASSERT_EQ(pbkdf2_hex("password", "salt", 1, 1025), std::string("<refused>"));
        ASSERT_EQ(pbkdf2_hex("password", "salt", 1, 5), std::string("120fb6cffc"));                // (a short result is the start of the longer one)
    } TEST_END();

    TEST_CASE("ACC1.2 The comparison of two secrets looks at every byte of the longer one whatever the answer, so that its time does not say where they differ") {
        size_t steps = 0;
        ASSERT_TRUE(equal_constant_time("abcdef", "abcdef", &steps));
        ASSERT_EQ(steps, size_t{6});
        ASSERT_FALSE(equal_constant_time("Xbcdef", "abcdef", &steps));                              // (the first byte differs: all six are still looked at)
        ASSERT_EQ(steps, size_t{6});
        ASSERT_FALSE(equal_constant_time("abcdeX", "abcdef", &steps));
        ASSERT_EQ(steps, size_t{6});
        ASSERT_FALSE(equal_constant_time("abc", "abcdef", &steps));                                 // (another length: the longer one is walked)
        ASSERT_EQ(steps, size_t{6});
        ASSERT_FALSE(equal_constant_time("abcdef", "", &steps));
        ASSERT_EQ(steps, size_t{6});
        ASSERT_TRUE(equal_constant_time("", "", &steps));
        ASSERT_EQ(steps, size_t{0});
        ASSERT_FALSE(equal_constant_time("abc", std::string("abc\0", 4)));                          // (another length is another secret, even when the extra bytes are zero)
        ASSERT_FALSE(equal_constant_time(std::string("abc\0\0", 5), "abc"));
        ASSERT_FALSE(equal_constant_time(std::string("ab\0c", 4), std::string("ab\0d", 4)));
        ASSERT_TRUE(equal_constant_time(std::string("ab\0c", 4), std::string("ab\0c", 4)));
    } TEST_END();

    TEST_CASE("ACC1.3 A password is kept as pbkdf2-sha256$600000$salt$hash with a salt of its own; the cost of a new one is at least 600,000 rounds; a hash made with fewer rounds still matches and is called outdated") {
        ASSERT_TRUE(kPasswordRounds >= 600000);
        const std::string real = password_hash("a long password of my own", kPasswordRounds);
        ASSERT_TRUE(real.compare(0, 21, "pbkdf2-sha256$600000$") == 0);
        ASSERT_TRUE(password_verify("a long password of my own", real, kPasswordRounds));
        ASSERT_FALSE(password_verify("a long password of my owm", real, kPasswordRounds));
        ASSERT_TRUE(real.find("a long password") == std::string::npos);
        const std::string a = password_hash("same password", kFast), b = password_hash("same password", kFast);
        ASSERT_TRUE(!a.empty() && !b.empty() && a != b);                                            // (a salt for each hash)
        ASSERT_TRUE(a.compare(0, 18, "pbkdf2-sha256$200$") == 0);
        std::vector<std::string> parts;
        std::stringstream split(a);
        for (std::string piece; std::getline(split, piece, '$');) parts.push_back(piece);
        ASSERT_TRUE(parts.size() == 4 && parts[2].size() == 22 && parts[3].size() == 43);          // (16 bytes of salt, 32 bytes of hash, in base64url)
        bool outdated = true;
        ASSERT_TRUE(password_verify("same password", a, kFast, &outdated) && !outdated);
        ASSERT_TRUE(password_verify("same password", a, kFast + 1, &outdated) && outdated);        // (a later program asks for more rounds)
        ASSERT_TRUE(password_verify("same password", a, kFast - 1, &outdated) && !outdated);
        const uint64_t compares = equal_constant_time_calls();                                      // (the hash is compared by the comparison that looks at every byte, and by nothing else)
        ASSERT_TRUE(password_verify("same password", a, kFast));
        ASSERT_EQ(equal_constant_time_calls(), compares + 1);
        ASSERT_FALSE(password_verify("another password", a, kFast));
        ASSERT_EQ(equal_constant_time_calls(), compares + 2);
        ASSERT_FALSE(password_verify("same password", "not a hash", kFast));
        ASSERT_EQ(equal_constant_time_calls(), compares + 2);
        ASSERT_TRUE(password_hash("x", 0).empty());
        ASSERT_TRUE(password_hash("x", 10000001).empty());
    } TEST_END();

    TEST_CASE("ACC1.4 Text that is not a hash of this program never matches, whatever the password (an empty one, another scheme, no rounds, too many rounds, a short salt, a wrong size, a missing or extra part)") {
        const std::string good = password_hash("some password here", kFast);
        ASSERT_TRUE(password_verify("some password here", good, kFast));
        const size_t last = good.rfind('$');
        const std::string salt_part = good.substr(good.find('$', 14) + 1, 22);
        const std::vector<std::string> bad = {"",
                                              "plain",
                                              "pbkdf2-sha1" + good.substr(13),
                                              "pbkdf2-sha256$0" + good.substr(good.find('$', 14)),
                                              "pbkdf2-sha256$99999999" + good.substr(good.find('$', 14)),
                                              "pbkdf2-sha256$x200" + good.substr(good.find('$', 14)),
                                              "pbkdf2-sha256$200$AAAA" + good.substr(last),
                                              good.substr(0, last + 1) + "AAAA",
                                              good + "$extra",
                                              good.substr(0, last),
                                              "pbkdf2-sha256$200$" + salt_part + "$",
                                              "$$$"};
        for (const std::string& s : bad) ASSERT_MSG(!password_verify("some password here", s, kFast), s);
    } TEST_END();

    TEST_CASE("ACC1.5 A starting password is 20 characters from an alphabet without look-alikes, never the same twice, and every character of the alphabet turns up") {
        std::set<std::string> seen;
        std::map<char, int> counts;
        for (int i = 0; i < 2000; ++i) {
            const std::string p = generate_password();
            ASSERT_EQ(p.size(), size_t{20});
            ASSERT_TRUE(seen.insert(p).second);
            for (const char c : p) {
                ASSERT_MSG(std::string("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789").find(c) != std::string::npos, std::string("a character outside the alphabet: ") + c);
                ++counts[c];
            }
            ASSERT_TRUE(password_problem(p, "someone").empty());
        }
        for (const char c : std::string("IlOo01")) ASSERT_EQ(counts.count(c), size_t{0});
        ASSERT_EQ(counts.size(), size_t{56});
        for (const auto& kv : counts) ASSERT_MSG(kv.second > 40000 / 56 * 8 / 10 && kv.second < 40000 / 56 * 12 / 10, std::string("uneven: ") + kv.first);          // (40,000 draws: about 714 each, five standard deviations either way; a modulo that is not rejected makes 32 of the characters a quarter more likely)
    } TEST_END();

    TEST_CASE("ACC1.6 What a new password must be: 12 characters (not bytes), printable, valid UTF-8, at most 256 bytes, not the username") {
        ASSERT_EQ(password_problem("elevenchars", "x"), std::string("too_short"));
        ASSERT_EQ(password_problem("twelve chars", "x"), std::string());
        ASSERT_EQ(password_problem(std::string("\xC3\xA9") + "bcdefghijk", "x"), std::string("too_short"));        // (11 characters, 12 bytes)
        ASSERT_EQ(password_problem(std::string("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"), "x"), std::string());     // (12 characters, 24 bytes)
        ASSERT_EQ(password_problem("tab\there is it ok", "x"), std::string("characters"));
        ASSERT_EQ(password_problem(std::string("twelve\x01 chars"), "x"), std::string("characters"));
        ASSERT_EQ(password_problem(std::string("twelve\x7f chars"), "x"), std::string("characters"));
        ASSERT_EQ(password_problem("bad \xFF\xFE utf eight text", "x"), std::string("characters"));
        ASSERT_EQ(password_problem("bad \xC0\xAF overlong text", "x"), std::string("characters"));
        ASSERT_EQ(password_problem(std::string(256, 'a'), "x"), std::string());
        ASSERT_EQ(password_problem(std::string(257, 'a'), "x"), std::string("too_long"));
        ASSERT_EQ(password_problem("Moderator-Mia", "moderator-mia"), std::string("same_as_name"));
        ASSERT_EQ(password_problem("some spaces are fine", "x"), std::string());
    } TEST_END();

    TEST_CASE("ACC1.7 A username is 3 to 32 letters, digits, dot, dash or underscore; two names that differ only in capitals are the same name") {
        std::string out, reason;
        ASSERT_TRUE(clean_username("abc", out, reason) && out == "abc");
        ASSERT_TRUE(clean_username(std::string(32, 'a'), out, reason));
        ASSERT_TRUE(clean_username("A.b-c_9", out, reason) && out == "A.b-c_9");
        ASSERT_FALSE(clean_username("ab", out, reason));
        ASSERT_EQ(reason, std::string("too_short"));
        ASSERT_FALSE(clean_username(std::string(33, 'a'), out, reason));
        ASSERT_EQ(reason, std::string("too_long"));
        for (const char* bad : {"has space", "slash/name", "semi;colon", "quote\"s", "<script>", "tab\tname", "new\nline", "caf\xC3\xA9", "a@b.c", "per%cent", "back\\slash", "nul\x01xx"}) {
            ASSERT_MSG(!clean_username(bad, out, reason) && reason == "characters", bad);
        }
        ASSERT_EQ(username_key("Mia.B"), std::string("mia.b"));
        ASSERT_EQ(username_key("mIa.b"), username_key("MIA.B"));
    } TEST_END();

    TEST_CASE("ACC2.1 The first Admin: made once, refused when an Admin exists; the starting password is shown to the caller and kept nowhere but as a hash; the account must choose its own password") {
        Accounts t;
        User u;
        std::string password;
        Failure why;
        ASSERT_TRUE(t.accounts->create_first_admin("Owner", t.now, u, password, why));
        ASSERT_TRUE(u.name == "Owner" && u.role == "admin" && u.must_change && !u.disabled && u.session_gen == 1 && password.size() == 20);
        const User first = u;
        ASSERT_FALSE(t.accounts->create_first_admin("Other", t.now, u, password, why));
        ASSERT_TRUE(why.status == 409 && why.code == "admin_exists");
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_users"), int64_t{1});
        t.accounts->set_disabled(Actor(), first.id, true, t.now, why);                              // (even a disabled Admin counts: the way back is the reset command)
        ASSERT_FALSE(t.accounts->create_first_admin("Third", t.now, u, password, why));
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_users"), int64_t{1});
        Accounts moderators_only;                                                                   // (a database with only a Moderator has no Admin: the command is allowed)
        std::string pw;
        ASSERT_TRUE(moderators_only.accounts->create(Actor(), "mod", "moderator", moderators_only.now, u, pw, why));
        bool ok = false;
        ASSERT_FALSE(moderators_only.accounts->any_admin(ok));
        ASSERT_TRUE(ok);
        ASSERT_TRUE(moderators_only.accounts->create_first_admin("boss", moderators_only.now, u, pw, why));
    } TEST_END();

    TEST_CASE("ACC2.2 A sign-in: the right password is Ok; a wrong password, an unknown name and a disabled account give the same answer and cost the same work; names are the same in any capitals") {
        Accounts t;
        const User dave = t.add("Dave", "admin", "dave's long password");
        t.add("Mia", "moderator", "mia's long password");
        const size_t before = t.accounts->verifies();
        User u;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.7", &u), Attempt::Ok);
        ASSERT_TRUE(u.id == dave.id && u.role == "admin" && !u.must_change);
        ASSERT_EQ(t.accounts->verifies(), before + 1);
        ASSERT_EQ(t.sign_in("dAVE", "dave's long password"), Attempt::Ok);                          // (the name is not case sensitive; the password is)
        ASSERT_EQ(t.sign_in("Dave", "Dave's long password"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("nobody", "dave's long password"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("Dave", ""), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("", "dave's long password"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in(std::string(5000, 'x'), "dave's long password"), Attempt::Wrong);
        Failure why;
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), t.accounts->list()[1].id, true, t.now, why));
        ASSERT_EQ(t.sign_in("Mia", "mia's long password"), Attempt::Wrong);                        // (disabled: right password, same answer as a wrong one)
        // the same work for each kind of failure: a password check is done in every case
        const size_t base = t.accounts->verifies();
        const uint64_t runs = pbkdf2_runs();
        t.sign_in("Dave", "wrong wrong wrong", "203.0.113.1");
        ASSERT_TRUE(t.accounts->verifies() == base + 1 && pbkdf2_runs() == runs + 1);
        t.sign_in("nobody", "wrong wrong wrong", "203.0.113.2");
        ASSERT_TRUE(t.accounts->verifies() == base + 2 && pbkdf2_runs() == runs + 2);                // (an unknown name: the same work, really done)
        t.sign_in("Mia", "mia's long password", "203.0.113.3");
        ASSERT_TRUE(t.accounts->verifies() == base + 3 && pbkdf2_runs() == runs + 3);                // (a disabled account: the same work too)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.4", &u), Attempt::Ok);
        ASSERT_TRUE(t.scalar("SELECT last_login_at FROM admin_users WHERE name = 'Dave'") == t.now && u.last_login_at == t.now);
        ASSERT_EQ(t.scalar("SELECT last_login_at FROM admin_users WHERE name = 'Mia'"), int64_t{0});
    } TEST_END();

    TEST_CASE("ACC2.3 Five wrong tries of one name from one address lock that pair for 10 minutes, to the second; the right password does not help while it lasts; another address is not touched; a good sign-in starts the count again") {
        Accounts t;
        t.add("Dave", "admin", "dave's long password");
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.9"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.9"), Attempt::Locked);                      // (the fifth locks)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.9"), Attempt::Locked);       // (the right password too)
        ASSERT_EQ(t.sign_in("dave", "dave's long password", "203.0.113.9"), Attempt::Locked);       // (the same name in other capitals)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.10"), Attempt::Ok);         // (another address)
        const int64_t locked_at = t.now;
        t.now = locked_at + 599;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.9"), Attempt::Locked);
        t.now = locked_at + 600;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.9"), Attempt::Ok);
        // four wrong tries and a good one: the count starts again
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.11"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.11"), Attempt::Ok);
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.11"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.11"), Attempt::Ok);
        // tries older than 10 minutes do not count
        t.now += 1000;
        for (int i = 0; i < 3; ++i) ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.12"), Attempt::Wrong);
        t.now += 601;
        for (int i = 0; i < 3; ++i) ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.12"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.12"), Attempt::Ok);
        // an unknown name is locked the same way, after the same number of tries: the lock tells nothing about which names exist
        for (int i = 0; i < 4; ++i) ASSERT_EQ(t.sign_in("ghost", "wrong", "203.0.113.13"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("ghost", "wrong", "203.0.113.13"), Attempt::Locked);
        ASSERT_EQ(t.sign_in("Ghost", "wrong", "203.0.113.13"), Attempt::Locked);
        ASSERT_EQ(t.sign_in("ghost", "wrong", "203.0.113.14"), Attempt::Wrong);
        // a wrong password during a lock is not counted again, and the lock does not grow
        const int64_t base = t.now;
        for (int i = 0; i < 5; ++i) t.sign_in("Dave", "wrong", "203.0.113.15");
        t.now = base + 300;
        for (int i = 0; i < 20; ++i) ASSERT_EQ(t.sign_in("Dave", "wrong", "203.0.113.15"), Attempt::Locked);
        t.now = base + 600;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.15"), Attempt::Ok);
    } TEST_END();

    TEST_CASE("ACC2.4 Twenty wrong tries from one address, whatever the names, lock the address for 10 minutes") {
        Accounts t;
        t.add("Dave", "admin", "dave's long password");
        for (int i = 0; i < 19; ++i) ASSERT_EQ(t.sign_in("name" + std::to_string(i), "wrong", "203.0.113.20"), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("name19", "wrong", "203.0.113.20"), Attempt::Locked);                   // (the twentieth)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.20"), Attempt::Locked);      // (the whole address)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.21"), Attempt::Ok);
        t.now += 599;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.20"), Attempt::Locked);
        t.now += 1;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.20"), Attempt::Ok);
        // two addresses of one /64 are one address (the identity is made by submissions_http: here an identity is just text)
        Accounts u;
        for (int i = 0; i < 20; ++i) u.sign_in("name" + std::to_string(i), "wrong", "v6:20010db800000001");
        ASSERT_EQ(u.sign_in("another", "wrong", "v6:20010db800000001"), Attempt::Locked);
        ASSERT_EQ(u.sign_in("another", "wrong", "v6:20010db800000002"), Attempt::Wrong);
    } TEST_END();

    TEST_CASE("ACC2.5 A crowd of strangers cannot lock the real user out, and still cannot guess faster than the pair rule: after 10 wrong tries from everywhere only an address that signed the user in before is let through") {
        Accounts t;
        t.add("Dave", "admin", "dave's long password");
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Ok);          // (his address, known now)
        for (int i = 0; i < 10; ++i) ASSERT_EQ(t.sign_in("Dave", "guess", "203.0.113." + std::to_string(100 + i)), Attempt::Wrong);
        // a new address with the right password: refused without a look at the password
        const size_t checks = t.accounts->verifies();
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.150"), Attempt::Locked);
        ASSERT_EQ(t.accounts->verifies(), checks);
        ASSERT_EQ(t.sign_in("Dave", "guess", "203.0.113.151"), Attempt::Locked);
        // his own address gets in; so does it again; a stranger's wrong guess from a known address is just wrong
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Ok);
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Ok);
        // the same for a name that does not exist: nothing tells the two apart
        for (int i = 0; i < 10; ++i) ASSERT_EQ(t.sign_in("ghost", "guess", "203.0.113." + std::to_string(200 + i)), Attempt::Wrong);
        ASSERT_EQ(t.sign_in("ghost", "guess", "203.0.113.250"), Attempt::Locked);
        // the slowdown ends after 10 minutes, and a successful sign-in from a known address clears it
        t.now += 601;
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.150"), Attempt::Ok);
        // an address that signed in more than 30 days ago is not known any more
        Accounts old;
        old.add("Dave", "admin", "dave's long password");
        ASSERT_EQ(old.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Ok);
        old.now += 31 * 86400;
        for (int i = 0; i < 10; ++i) old.sign_in("Dave", "guess", "203.0.113." + std::to_string(100 + i));
        ASSERT_EQ(old.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Locked);
    } TEST_END();

    TEST_CASE("ACC2.6 Only the 10 newest addresses of a user are remembered, and a reset or a removal forgets them all") {
        Accounts t;
        const User dave = t.add("Dave", "admin", "dave's long password");
        for (int i = 0; i < 12; ++i) {
            ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100." + std::to_string(i)), Attempt::Ok);
            t.now += 10;
        }
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_known_addr WHERE user_id = " + std::to_string(dave.id)), int64_t{10});
        std::string password;
        Failure why;
        ASSERT_TRUE(t.accounts->reset(Actor(), dave.id, t.now, password, why));
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_known_addr"), int64_t{0});
    } TEST_END();

    TEST_CASE("ACC2.7 The table of counts cannot grow without end: a flood of new names from many addresses is answered Locked when it is full of live rows, and nothing is stored for it") {
        Accounts t([](AccountConfig& c) { c.lock_rows_max = 30; });
        t.add("Dave", "admin", "dave's long password");
        int locked = 0;
        for (int i = 0; i < 40; ++i) {
            if (t.sign_in("name" + std::to_string(i), "wrong", "203.0.113." + std::to_string(i)) == Attempt::Locked) ++locked;
        }
        ASSERT_TRUE(locked > 0);
        ASSERT_TRUE(t.scalar("SELECT COUNT(*) FROM admin_locks") <= 33);
        t.now += 700;                                                                              // (a window later every row is old and goes)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.77"), Attempt::Ok);
        ASSERT_TRUE(t.scalar("SELECT COUNT(*) FROM admin_locks") < 5);
    } TEST_END();

    TEST_CASE("ACC2.8 The number of password checks in a window is capped for the whole server (each costs the one thread a third of a second); one place is kept for an address that signed the user in before") {
        Accounts t([](AccountConfig& c) { c.checks_per_window = 3; });
        t.add("Dave", "admin", "dave's long password");
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Ok);          // (1, and his address is known)
        ASSERT_EQ(t.sign_in("x1", "wrong", "203.0.113.1"), Attempt::Wrong);                         // (2)
        ASSERT_EQ(t.sign_in("x2", "wrong", "203.0.113.2"), Attempt::Wrong);                         // (3)
        ASSERT_EQ(t.sign_in("x3", "wrong", "203.0.113.3"), Attempt::Busy);                          // (a stranger: over the budget, and not counted as a wrong try)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "203.0.113.4"), Attempt::Busy);         // (an address that is not known)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Ok);          // (the kept place)
        ASSERT_EQ(t.sign_in("Dave", "dave's long password", "198.51.100.1"), Attempt::Busy);        // (only one)
        t.now_ms += 9999;
        ASSERT_EQ(t.sign_in("x4", "wrong", "203.0.113.5"), Attempt::Busy);                          // (the window is 10 s)
        t.now_ms += 1;
        ASSERT_EQ(t.sign_in("x4", "wrong", "203.0.113.5"), Attempt::Wrong);
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_locks WHERE scope = 'pair'"), int64_t{3});    // (x1, x2, x4: a refused check leaves no mark)
    } TEST_END();

    TEST_CASE("ACC2.9 Every change that ends a user's sessions raises the generation of the user's sessions: a reset, a disable, an enable, a new password; a removal takes the user away") {
        Accounts t;
        const User a = t.add("alpha", "admin");
        t.add("beta", "admin");
        std::string password;
        Failure why;
        User u;
        ASSERT_EQ(a.session_gen, int64_t{2});                                                       // (made: 1, the first password change: 2)
        ASSERT_TRUE(t.accounts->reset(Actor(), a.id, t.now, password, why));
        t.accounts->find(a.id, u);
        ASSERT_EQ(u.session_gen, int64_t{3});
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), a.id, true, t.now, why));
        t.accounts->find(a.id, u);
        ASSERT_EQ(u.session_gen, int64_t{4});
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), a.id, false, t.now, why));
        t.accounts->find(a.id, u);
        ASSERT_EQ(u.session_gen, int64_t{5});
        int64_t gen = 0;
        ASSERT_TRUE(t.accounts->change_password(Actor(), a.id, "another long password", t.now, gen, why));
        t.accounts->find(a.id, u);
        ASSERT_TRUE(u.session_gen == 6 && gen == 6 && !u.must_change);
        ASSERT_TRUE(t.accounts->remove(Actor(), a.id, t.now, why));
        ASSERT_FALSE(t.accounts->find(a.id, u));
        ASSERT_FALSE(t.accounts->reset(Actor(), a.id, t.now, password, why));
        ASSERT_EQ(why.status, 404);
        ASSERT_FALSE(t.accounts->set_disabled(Actor(), a.id, true, t.now, why));
        ASSERT_FALSE(t.accounts->remove(Actor(), a.id, t.now, why));
        ASSERT_FALSE(t.accounts->change_password(Actor(), a.id, "another long password", t.now, gen, why));
    } TEST_END();

    TEST_CASE("ACC2.10 The last Admin cannot be disabled or removed; a Moderator, a second Admin and a disabled Admin can; only enabled Admins count") {
        Accounts t;
        const User dave = t.add("dave", "admin");
        const User mia = t.add("mia", "moderator");
        Failure why;
        ASSERT_FALSE(t.accounts->remove(Actor(), dave.id, t.now, why));
        ASSERT_TRUE(why.status == 409 && why.code == "last_admin");
        ASSERT_FALSE(t.accounts->set_disabled(Actor(), dave.id, true, t.now, why));
        ASSERT_TRUE(why.status == 409 && why.code == "last_admin");
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), mia.id, true, t.now, why));                   // (a Moderator is not an Admin)
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), mia.id, false, t.now, why));
        const User second = t.add("second", "admin");
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), second.id, true, t.now, why));                // (dave is still there)
        ASSERT_FALSE(t.accounts->remove(Actor(), dave.id, t.now, why));                             // (the second is disabled: it does not count)
        ASSERT_EQ(why.code, std::string("last_admin"));
        ASSERT_FALSE(t.accounts->set_disabled(Actor(), dave.id, true, t.now, why));
        ASSERT_TRUE(t.accounts->remove(Actor(), second.id, t.now, why));                            // (a disabled Admin can go)
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), second.id + 100, false, t.now, why) == false);
        const User third = t.add("third", "admin");
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), dave.id, true, t.now, why));                  // (a second enabled Admin: now it is allowed)
        ASSERT_FALSE(t.accounts->set_disabled(Actor(), third.id, true, t.now, why));                // (and the other is the last now)
        ASSERT_FALSE(t.accounts->remove(Actor(), third.id, t.now, why));
        ASSERT_TRUE(t.accounts->remove(Actor(), mia.id, t.now, why));
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), dave.id, false, t.now, why));
        ASSERT_TRUE(t.accounts->remove(Actor(), dave.id, t.now, why));
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_users WHERE role = 'admin' AND disabled = 0"), int64_t{1});
    } TEST_END();

    TEST_CASE("ACC2.11 A user is made with a role that never changes, a name that is free in any capitals, and a starting password that must be changed; the roles are Admin and Moderator and nothing else") {
        Accounts t;
        User u;
        std::string password;
        Failure why;
        ASSERT_TRUE(t.accounts->create(Actor(), "Mia", "moderator", t.now, u, password, why));
        ASSERT_TRUE(u.role == "moderator" && u.must_change && password.size() == 20 && u.created_by == "console");
        ASSERT_EQ(t.sign_in("Mia", password), Attempt::Ok);                                         // (the starting password signs in)
        ASSERT_FALSE(t.accounts->create(Actor(), "mia", "admin", t.now, u, password, why));
        ASSERT_TRUE(why.status == 409 && why.code == "username_taken");
        ASSERT_TRUE(password.empty() || password.size() == 20);
        for (const char* role : {"", "owner", "ADMIN", "root", "admin "}) {
            ASSERT_MSG(!t.accounts->create(Actor(), "newname", role, t.now, u, password, why) && why.code == "bad_role" && why.status == 400, role);
        }
        ASSERT_FALSE(t.accounts->create(Actor(), "<b>", "admin", t.now, u, password, why));
        ASSERT_TRUE(why.code == "bad_username" && why.extra.get("reason").str() == "characters");
        ASSERT_FALSE(t.accounts->create(Actor(), "ab", "admin", t.now, u, password, why));
        ASSERT_EQ(why.extra.get("reason").str(), std::string("too_short"));
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_users"), int64_t{1});
        // there is no way to change a role afterwards
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_users WHERE role = 'moderator'"), int64_t{1});
    } TEST_END();

    TEST_CASE("ACC2.12 A reset gives a new random password, makes the user choose another, ends the sessions, clears the locks and forgets the addresses; the old password and the old starting password are dead") {
        Accounts t;
        const User dave = t.add("dave", "admin", "dave's long password");
        t.add("other", "admin");
        for (int i = 0; i < 5; ++i) t.sign_in("dave", "wrong", "203.0.113.9");
        ASSERT_EQ(t.sign_in("dave", "dave's long password", "203.0.113.9"), Attempt::Locked);
        std::string password;
        Failure why;
        ASSERT_TRUE(t.accounts->reset(Actor(), dave.id, t.now, password, why));
        ASSERT_EQ(password.size(), size_t{20});
        User u;
        t.accounts->find(dave.id, u);
        ASSERT_TRUE(u.must_change && u.session_gen == 3);
        ASSERT_EQ(t.sign_in("dave", "dave's long password", "203.0.113.9"), Attempt::Wrong);       // (the old password is dead; the lock is gone)
        ASSERT_EQ(t.sign_in("dave", password, "203.0.113.9", &u), Attempt::Ok);
        ASSERT_TRUE(u.must_change);
        // a disabled account stays disabled by a reset from a page, and is enabled by the console's reset
        ASSERT_TRUE(t.accounts->set_disabled(Actor(), dave.id, true, t.now, why));
        ASSERT_TRUE(t.accounts->reset(Actor(), dave.id, t.now, password, why));
        t.accounts->find(dave.id, u);
        ASSERT_TRUE(u.disabled);
        ASSERT_TRUE(t.accounts->reset(Actor(), dave.id, t.now, password, why, true));
        t.accounts->find(dave.id, u);
        ASSERT_FALSE(u.disabled);
        ASSERT_EQ(t.sign_in("dave", password, "203.0.113.30"), Attempt::Ok);
    } TEST_END();

    TEST_CASE("ACC2.13 A hash made with fewer rounds is made again, with the current number, at the next good sign-in") {
        Accounts weak([](AccountConfig& c) { c.rounds = 50; });
        weak.add("dave", "admin", "dave's long password");
        std::string stored;
        {
            db::Statement st;
            weak.database.prepare(st, "SELECT pw_hash FROM admin_users");
            st.step();
            stored = st.column_text(0);
        }
        ASSERT_TRUE(stored.compare(0, 17, "pbkdf2-sha256$50$") == 0);
        AdminAccounts stronger(weak.database, [] { AccountConfig c; c.rounds = 120; c.checks_per_window = 1000; return c; }());
        std::string why;
        ASSERT_TRUE(stronger.open(why));
        User u;
        ASSERT_EQ(stronger.sign_in("dave", "wrong", "203.0.113.1", weak.now, 1000, u), Attempt::Wrong);
        {
            db::Statement st;
            weak.database.prepare(st, "SELECT pw_hash FROM admin_users");
            st.step();
            ASSERT_EQ(st.column_text(0), stored);                                                   // (a wrong password changes nothing)
        }
        ASSERT_EQ(stronger.sign_in("dave", "dave's long password", "203.0.113.1", weak.now, 1000, u), Attempt::Ok);
        db::Statement st;
        weak.database.prepare(st, "SELECT pw_hash FROM admin_users");
        st.step();
        ASSERT_TRUE(st.column_text(0).compare(0, 18, "pbkdf2-sha256$120$") == 0);
        ASSERT_TRUE(password_verify("dave's long password", st.column_text(0), 120));
        ASSERT_EQ(stronger.sign_in("dave", "dave's long password", "203.0.113.1", weak.now, 1000, u), Attempt::Ok);
    } TEST_END();

    TEST_CASE("ACC2.14 The log: who did what and when, for every change and sign-in, with no password and no address in it; failed sign-ins are capped, so a flood cannot push the real actions out") {
        Accounts t;
        const User dave = t.add("dave", "admin", "dave's long password");
        Actor by;
        by.id = dave.id;
        by.name = "dave";
        by.address = "198.51.100.77";
        User mia;
        std::string starting, password;
        Failure why;
        ASSERT_TRUE(t.accounts->create(by, "mia", "moderator", t.now, mia, starting, why));
        ASSERT_TRUE(t.accounts->reset(by, mia.id, t.now, password, why));
        ASSERT_TRUE(t.accounts->set_disabled(by, mia.id, true, t.now, why));
        ASSERT_TRUE(t.accounts->set_disabled(by, mia.id, false, t.now, why));
        ASSERT_TRUE(t.accounts->remove(by, mia.id, t.now, why));
        ASSERT_EQ(t.sign_in("dave", "dave's long password", "198.51.100.77"), Attempt::Ok);
        ASSERT_EQ(t.sign_in("dave", "wrong", "198.51.100.77"), Attempt::Wrong);
        for (int i = 0; i < 4; ++i) t.sign_in("<img src=x onerror=alert(1)>", "wrong", "203.0.113.9");
        t.sign_in("<img src=x onerror=alert(1)>", "wrong", "203.0.113.9");
        std::vector<std::string> actions;
        for (const AuditEntry& e : t.accounts->audit_list(200)) actions.push_back(e.actor + ":" + e.action + ":" + e.target);
        const std::vector<std::string> wanted = {"(not a username):locked:", "dave:sign_in_failed:", "dave:sign_in:", "dave:user_removed:mia", "dave:user_enabled:mia", "dave:user_disabled:mia", "dave:password_reset:mia", "dave:user_added:mia"};
        ASSERT_MSG(actions.size() >= wanted.size(), std::to_string(actions.size()));
        for (size_t i = 0; i < wanted.size(); ++i) ASSERT_MSG(actions[i] == wanted[i], actions[i] + " != " + wanted[i]);
        // nothing secret in any row of the database: no password, no address in the clear
        std::string dump;
        db::Statement st;
        t.database.prepare(st, "SELECT at, actor, actor_id, action, target, detail, addr_key FROM admin_audit");
        while (st.step() == db::Statement::Step::Row) {
            for (int c = 0; c < 7; ++c) dump += st.column_text(c) + "|";
        }
        for (const std::string& secret : {starting, password, std::string("dave's long password"), std::string("198.51.100.77"), std::string("203.0.113.9")}) ASSERT_MSG(dump.find(secret) == std::string::npos, secret);
        ASSERT_EQ(t.scalar("SELECT COUNT(DISTINCT addr_key) FROM admin_audit WHERE addr_key <> ''"), int64_t{2});     // (the address is a keyed hash; the same address, the same hash)
        ASSERT_EQ(t.scalar("SELECT COUNT(*) FROM admin_audit WHERE addr_key = '" + t.accounts->address_hash("198.51.100.77") + "'"), int64_t{7});
        // a flood of failures keeps only the newest 1000, and leaves the other lines
        Accounts f([](AccountConfig& c) { c.audit_failed_max = 5; });
        f.add("dave", "admin", "dave's long password");
        for (int i = 0; i < 30; ++i) {
            f.sign_in("dave", "wrong", "203.0.113." + std::to_string(i));
            f.sign_in("dave", "wrong", "203.0.113." + std::to_string(i));
        }
        ASSERT_EQ(f.scalar("SELECT COUNT(*) FROM admin_audit WHERE action IN ('sign_in_failed', 'locked')"), int64_t{5});          // (the cap)
        ASSERT_TRUE(f.scalar("SELECT COUNT(*) FROM admin_audit WHERE action = 'user_added'") == 1);
        // old lines go in maintenance, and the cap of all lines holds
        f.now += 181 * 86400;
        f.accounts->maintenance(f.now);
        ASSERT_EQ(f.scalar("SELECT COUNT(*) FROM admin_audit"), int64_t{0});
    } TEST_END();

    TEST_CASE("ACC2.15 No password is in the database file, in clear: not the starting one, not the chosen one, not a wrong one that was typed") {
        TempDir dir;
        const std::string path = (dir.path / "ants.db").string();
        std::string starting, chosen = "my very own long password", typed = "an unusual wrong guess";
        {
            Accounts t(nullptr, path);
            User u;
            Failure why;
            ASSERT_TRUE(t.accounts->create_first_admin("Owner", t.now, u, starting, why));
            int64_t gen = 0;
            ASSERT_TRUE(t.accounts->change_password(Actor(), u.id, chosen, t.now, gen, why));
            t.sign_in("Owner", typed);
            t.sign_in("Owner", chosen);
            t.database.close();
        }
        std::string all;
        for (const auto& entry : fs::directory_iterator(dir.path)) all += slurp(entry.path());
        ASSERT_TRUE(all.size() > 1000);
        ASSERT_TRUE(all.find(starting) == std::string::npos && all.find(chosen) == std::string::npos && all.find(typed) == std::string::npos);
        ASSERT_TRUE(all.find("pbkdf2-sha256$") != std::string::npos);
    } TEST_END();

    TEST_CASE("ACC3.1 The migration from a copy of a version 5 database: the maps and the log stay as they were, decided maps get the name that decided them, the new tables are empty, running it again changes nothing, an older program refuses the result") {
        TempDir dir;
        const std::string live = (dir.path / "live.db").string();
        const std::string copy = (dir.path / "copy.db").string();
        const std::vector<db::Migration> all = db_migrations();
        ASSERT_TRUE(all.size() >= 6 && all.back().version == 6 && all[4].version == 5);
        const std::vector<db::Migration> v5(all.begin(), all.begin() + 5);
        {
            db::Database db5;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db5, live, v5, db::OpenOptions(), &version).ok() && version == 5);
            const std::string id1(32, 'a'), id2(32, 'b'), id3(32, 'c'), id4(32, 'd');
            const auto row = [&](const std::string& id, const std::string& state, const std::string& final_name) {
                return "INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, final_name, final_key, renamed, maker, note, owner_message, width, height, minutes, players, description, size, hash, findings, file, "
                       "file_kept, created_at, decided_at) VALUES ('" + id + "', '" + std::string(64, '1') + "', '" + state + "', 'classic', 'N" + id.substr(0, 1) + "', 'n" + id.substr(0, 1) + "', " +
                       (final_name.empty() ? std::string("NULL, NULL") : "'" + final_name + "', '" + final_name + "'") + ", 0, 'Mia', '', '', 31, 31, 5, 2, 'd', 100, '0123456789abcdef', '[]', 'f" + id.substr(0, 1) + "', 1, 1000, " +
                       (state == "waiting" ? std::string("NULL") : std::string("2000")) + ")";
            };
            ASSERT_TRUE(db5.exec(row(id1, "approved", "na").c_str()).ok());
            ASSERT_TRUE(db5.exec(row(id2, "declined", "").c_str()).ok());
            ASSERT_TRUE(db5.exec(row(id3, "waiting", "").c_str()).ok());
            ASSERT_TRUE(db5.exec(row(id4, "taken_down", "nd").c_str()).ok());
            ASSERT_TRUE(db5.exec("INSERT INTO review_log (at, actor_id, actor_name, action, submission, detail) VALUES (1500, '1', 'Old Owner', 'approve', '" + id1 + "', ''),"
                                 " (1600, '1', 'Old Owner', 'take_down', '" + id4 + "', ''), (1700, '1', 'Later Name', 'put_back', '" + id4 + "', ''), (1800, '1', 'Later Name', 'take_down', '" + id4 + "', ''),"
                                 " (1900, '1', 'Old Owner', 'decline', '" + id2 + "', 'x')").ok());
            ASSERT_TRUE(db5.exec("INSERT INTO meta (key, value) VALUES ('submission_salt', 'keepme')").ok());
            db5.close();
        }
        fs::copy_file(live, copy);                                                                  // (what the owner backs up before the first start)
        const std::string before_bytes = slurp(copy);
        {   // an older program (versions up to 5) refuses a version 6 file, and the file stays as it was: checked on a second copy below
            db::Database db6;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db6, live, all, db::OpenOptions(), &version).ok() && version == 6);
            const auto count = [&](const std::string& sql) {
                db::Statement st;
                db6.prepare(st, sql);
                return st.step() == db::Statement::Step::Row ? st.column_int64(0) : int64_t{-1};
            };
            ASSERT_EQ(count("SELECT COUNT(*) FROM map_submissions"), int64_t{4});
            ASSERT_EQ(count("SELECT COUNT(*) FROM review_log"), int64_t{5});
            ASSERT_EQ(count("SELECT COUNT(*) FROM meta WHERE key = 'submission_salt' AND value = 'keepme'"), int64_t{1});
            for (const char* table : {"admin_users", "admin_locks", "admin_known_addr", "admin_audit"}) ASSERT_EQ(count(std::string("SELECT COUNT(*) FROM ") + table), int64_t{0});
            const auto decided_by = [&](const std::string& id) {
                db::Statement st;
                db6.prepare(st, "SELECT decided_by FROM map_submissions WHERE id = '" + id + "'");
                return st.step() == db::Statement::Step::Row ? st.column_text(0) : std::string("<none>");
            };
            ASSERT_EQ(decided_by(std::string(32, 'a')), std::string("Old Owner"));
            ASSERT_EQ(decided_by(std::string(32, 'b')), std::string("Old Owner"));
            ASSERT_EQ(decided_by(std::string(32, 'c')), std::string(""));                           // (a waiting map was decided by nobody)
            ASSERT_EQ(decided_by(std::string(32, 'd')), std::string("Later Name"));                 // (the last decision)
            db6.close();
            db::Database again;
            version = 0;
            ASSERT_TRUE(db::open_and_migrate(again, live, all, db::OpenOptions(), &version).ok() && version == 6);          // (idempotent)
            db::Statement st;
            again.prepare(st, "SELECT COUNT(*) FROM schema_version");
            st.step();
            ASSERT_EQ(st.column_int64(0), int64_t{6});
            again.close();
        }
        {   // the copy that was made before: migrate it the same way, then an older program must refuse it and leave it byte for byte
            db::Database mig;
            ASSERT_TRUE(db::open_and_migrate(mig, copy, all, db::OpenOptions()).ok());
            mig.close();
            for (const char* suffix : {"-wal", "-shm"}) fs::remove(copy + suffix);
            const std::string migrated = slurp(copy);
            ASSERT_TRUE(migrated != before_bytes);
            db::Database old_program;
            const std::vector<db::Migration> only5(all.begin(), all.begin() + 5);
            const db::Error refused = db::open_and_migrate(old_program, copy, only5, db::OpenOptions());
            ASSERT_TRUE(!refused.ok() && refused.code == db::kErrNewerSchema);
            ASSERT_FALSE(old_program.is_open());
            for (const char* suffix : {"-wal", "-shm"}) fs::remove(copy + suffix);
            ASSERT_TRUE(slurp(copy) == migrated);
        }
        {   // the failure of a migration leaves version 5: a database with a table of the same name already there is not touched halfway
            db::Database broken;
            const std::string bad = (dir.path / "bad.db").string();
            ASSERT_TRUE(db::open_and_migrate(broken, bad, v5, db::OpenOptions()).ok());
            ASSERT_TRUE(broken.exec("CREATE TABLE admin_audit (x INTEGER)").ok());
            broken.close();
            int version = 0;
            db::Database third;
            ASSERT_FALSE(db::open_and_migrate(third, bad, all, db::OpenOptions(), &version).ok());
            db::Database check;
            ASSERT_TRUE(db::open_and_migrate(check, bad, v5, db::OpenOptions(), &version).ok() && version == 5);            // (still version 5, no half of the new tables)
            db::Statement st;
            check.prepare(st, "SELECT COUNT(*) FROM sqlite_master WHERE name IN ('admin_users', 'admin_locks', 'admin_known_addr')");
            st.step();
            ASSERT_EQ(st.column_int64(0), int64_t{0});
        }
    } TEST_END();

    TEST_CASE("ACC4.1 The first-account command prints the starting password once, to standard output only, refuses when an Admin exists, and the database holds only its hash; the reset command gives a new one") {
        TempDir dir;
        AdminCommand create;
        create.kind = AdminCommand::Kind::Create;
        create.name = "Owner";
        create.db_path = (dir.path / "ants.db").string();
        create.rounds = 0;                                                                         // (the program's own cost: 600,000 rounds)
        std::ostringstream out, err;
        ASSERT_EQ(run_admin_command(create, out, err), 0);
        ASSERT_EQ(err.str(), std::string());
        const std::string text = out.str();
        ASSERT_TRUE(text.compare(0, 24, "Admin account created.\nU") == 0);
        const size_t at = text.find("Starting password: ");
        ASSERT_TRUE(at != std::string::npos);
        const std::string password = text.substr(at + 19, 20);
        ASSERT_TRUE(text.find("Username: Owner\n") != std::string::npos && text.find("Shown once. Sign in at /admin and change it.\n") != std::string::npos);
        ASSERT_TRUE(text.find("Owner account") == std::string::npos);                              // (the word is Admin)
        ASSERT_EQ(text.size(), text.find('\n', text.find("Shown once")) + 1);
        {
            db::Database database;
            ASSERT_TRUE(open_server_db(database, create.db_path));
            db::Statement st;
            database.prepare(st, "SELECT pw_hash, must_change, role FROM admin_users");
            ASSERT_TRUE(st.step() == db::Statement::Step::Row);
            ASSERT_TRUE(st.column_text(0).compare(0, 21, "pbkdf2-sha256$600000$") == 0 && st.column_int64(1) == 1 && st.column_text(2) == "admin");
            ASSERT_TRUE(password_verify(password, st.column_text(0), kPasswordRounds));
        }
        std::ostringstream out2, err2;
        ASSERT_EQ(run_admin_command(create, out2, err2), 1);                                       // (an Admin exists)
        ASSERT_TRUE(out2.str().empty() && err2.str().find("exists already") != std::string::npos && err2.str().find(password) == std::string::npos);
        {
            db::Database database;                                                                  // (the account is disabled: the command is the way back in, so the reset enables it)
            ASSERT_TRUE(open_server_db(database, create.db_path));
            ASSERT_TRUE(database.exec("UPDATE admin_users SET disabled = 1").ok());
        }
        AdminCommand reset;
        reset.kind = AdminCommand::Kind::Reset;
        reset.name = "owner";
        reset.db_path = create.db_path;
        reset.rounds = kFast;
        std::ostringstream out3, err3;
        ASSERT_EQ(run_admin_command(reset, out3, err3), 0);
        ASSERT_TRUE(err3.str().empty() && out3.str().find("Password reset.") == 0 && out3.str().find("New starting password: ") != std::string::npos);
        const std::string fresh = out3.str().substr(out3.str().find("New starting password: ") + 23, 20);
        ASSERT_TRUE(fresh != password && fresh.size() == 20);
        {
            db::Database database;
            ASSERT_TRUE(open_server_db(database, create.db_path));
            AccountConfig config;
            config.rounds = kPasswordRounds;
            AdminAccounts accounts(database, config);
            std::string why;
            ASSERT_TRUE(accounts.open(why));
            User u;
            ASSERT_EQ(accounts.sign_in("Owner", password, "198.51.100.1", kT0, 1000, u), Attempt::Wrong);        // (the first starting password is dead)
            ASSERT_EQ(accounts.sign_in("Owner", fresh, "198.51.100.1", kT0, 100000, u), Attempt::Ok);
            ASSERT_TRUE(u.must_change && u.session_gen == 2);
        }
        reset.name = "nobody";
        std::ostringstream out4, err4;
        ASSERT_EQ(run_admin_command(reset, out4, err4), 1);
        ASSERT_TRUE(out4.str().empty() && err4.str().find("no user") != std::string::npos);
        AdminCommand no_db = create;
        no_db.db_path.clear();
        std::ostringstream out5, err5;
        ASSERT_EQ(run_admin_command(no_db, out5, err5), 1);
        ASSERT_TRUE(out5.str().empty() && err5.str().find("--db") != std::string::npos);
        AdminCommand bad_name = create;
        bad_name.name = "x";
        bad_name.db_path = (dir.path / "other.db").string();
        std::ostringstream out6, err6;
        ASSERT_EQ(run_admin_command(bad_name, out6, err6), 1);
        ASSERT_TRUE(out6.str().empty() && err6.str().find("at least 3") != std::string::npos);
    } TEST_END();

    TEST_CASE("ACC4.2 The command refuses a file that is not a database or that a newer program made, and leaves it as it was") {
        TempDir dir;
        const std::string junk = (dir.path / "junk.db").string();
        {
            std::ofstream f(junk, std::ios::binary);
            f << "this is not a database file, not at all, and it is long enough to be a header of something";
        }
        const std::string before = slurp(junk);
        AdminCommand create;
        create.name = "Owner";
        create.db_path = junk;
        create.rounds = kFast;
        std::ostringstream out, err;
        ASSERT_EQ(run_admin_command(create, out, err), 1);
        ASSERT_TRUE(out.str().empty() && slurp(junk) == before);
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

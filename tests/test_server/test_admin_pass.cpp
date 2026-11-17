// Tests of the admin page's pass (admin_pass.hpp) and of the door that checks it (SubmissionApi, docs/MAP_SUBMISSIONS.md "The admin page"): the key and its shape, the keys that are made from it (against the known
// answers of docs/HUB.md), every reason that a pass is refused (a wrong mac, a wrong key, an old pass, an odd role, a missing or extra member, a used pass), the one answer that a visitor without a good pass gets,
// the token of a change, the routes that are gone, the decisions on maps signed with the username and the reference, and that no id is shown. Requests are made by hand: no sockets.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>

#include "../common/test_assert.hpp"
#include "ants_server/admin_audit.hpp"
#include "ants_server/admin_pass.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/submissions.hpp"
#include "ants_server/submissions_http.hpp"
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

using ctl::HttpRequest;
using ctl::HttpResponse;

// A made-up key (the known answers of docs/HUB.md are for it) and made-up references: no real key and no Discord id is anywhere in this file
const char* const kSecret = "canary-gate-key-5d8f0c1e7a9b4263-q";
const char* const kPassKeyHex = "13be5dab7dcbeb06e979f0fa5e5f817d178fa6b40da7c92bc02588e80ae3dd25";
const char* const kRefDave = "0123456789abcdef";
const char* const kRefMia = "fedcba9876543210";
// A pass that tests/scripts/admin_pass.py made (stdlib only, from docs/HUB.md alone), for now = kT0, nonce 00112233445566ff, ref kRefDave, name dave, role admin: the C++ check agrees with an independent maker
const char* const kPythonPass = "v1.eyJleHAiOjE3OTE0Njk5ODksIm5hbWUiOiJkYXZlIiwibm9uY2UiOiIwMDExMjIzMzQ0NTU2NmZmIiwicmVmIjoiMDEyMzQ1Njc4OWFiY2RlZiIsInJvbGUiOiJhZG1pbiJ9.afd6fe9f584feba9ccf135438615e07b3b3565ddd7b4271bcb9948f3b6b8e253";
const char* const kPythonCsrfDave = "79891ac82c8051666594cf557a22a28a";                         // the page's token for kRefDave under kSecret, made by the same script
const char* const kPythonCsrfMia = "1eac3a583cc7d0f4c5aa9d817e988618";

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

// The JSON text of a pass's payload, members in the order that the hub writes them (sorted)
std::string payload_json(const std::string& ref, const std::string& name, const std::string& role, int64_t exp, const std::string& nonce) {
    return "{\"exp\":" + std::to_string(exp) + ",\"name\":" + quoted(name) + ",\"nonce\":" + quoted(nonce) + ",\"ref\":" + quoted(ref) + ",\"role\":" + quoted(role) + "}";
}

// `v1.<payload>.<mac>` for a payload text, signed with the RAW key (the hub's way) or with whatever bytes a test passes as the key
std::string sign_with(const std::string& key, const std::string& payload) {
    const std::string body = "v1." + base64url_encode(payload);
    return body + "." + hex_encode(hmac_sha256(key, body));
}

std::string hub_key(const std::string& secret) { return hmac_sha256(secret, "ants-admin-pass-v1"); }

std::string nonce_of(uint64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(n));
    return buf;
}

// A pass as the hub's gate makes it: good for `now`, a new nonce each time
struct Maker {
    std::string secret{kSecret};
    uint64_t counter{0x1000};
    std::string make(int64_t now, const std::string& ref = kRefDave, const std::string& name = "dave", const std::string& role = "admin", int64_t life = 60) {
        return sign_with(hub_key(secret), payload_json(ref, name, role, now + life, nonce_of(++counter)));
    }
};

// The checks of the gate and every refusal of the door run on one made-up clock; a door is a whole API over a memory database
struct Door {
    TempDir dir;
    db::Database database;
    SubmissionConfig config;
    std::unique_ptr<SubmissionStore> store;
    std::unique_ptr<AdminAudit> audit;
    std::unique_ptr<SubmissionApi> api;
    Maker maker;
    int64_t now{kT0};
    uint32_t now_ms{1000};
    int refreshed{0};

    explicit Door(const std::function<void(SubmissionApiConfig&)>& tune = nullptr, const std::string& api_secret = kSecret) {
        open_server_db(database, "");
        config.dir = (dir.path / "s").string();
        config.min_free_bytes = 1;
        config.per_address_per_day = 1000;
        config.per_day_total = 100000;
        config.max_waiting = 1000;
        store = std::make_unique<SubmissionStore>(database, config, [](const std::string&) { return false; });
        std::string why;
        store->open(now, why);
        audit = std::make_unique<AdminAudit>(database);
        audit->open(why);
        SubmissionApiConfig sc;
        sc.gate_secret = api_secret;
        sc.check_budget_ms = 3600u * 1000u;
        if (tune) tune(sc);
        api = std::make_unique<SubmissionApi>(*store, *audit, sc);
        api->set_on_approved_changed([this] { ++refreshed; });
    }

    HttpRequest req(const std::string& method, const std::string& path, const std::map<std::string, std::string>& headers = {}, const std::string& body = "", const std::string& query = "") {
        HttpRequest r;
        r.method = method;
        r.path = path;
        r.query = query;
        r.headers.insert(headers.begin(), headers.end());
        r.body = body;
        r.peer = "172.18.0.4";
        return r;
    }

    HttpResponse call(const HttpRequest& r) { return api->handle(r, now, now_ms); }

    // a call with a pass in the header, as nginx makes it
    HttpResponse get(const std::string& path, const std::string& pass, const std::string& query = "", std::map<std::string, std::string> extra = {}) {
        if (!pass.empty()) extra["x-ants-admin-pass"] = pass;
        return call(req("GET", path, extra, "", query));
    }

    std::string csrf_for(const std::string& ref, const std::string& name = "dave", const std::string& role = "admin") {
        return parse_text(get("/admin/api/me", maker.make(now, ref, name, role)).body).get("csrf").str();
    }

    HttpResponse post(const std::string& path, const std::string& body = "{}", const std::string& ref = kRefDave, const std::string& name = "dave", const std::string& role = "admin", std::map<std::string, std::string> extra = {}) {
        std::map<std::string, std::string> h = {{"x-ants-admin-pass", maker.make(now, ref, name, role)}, {"x-ants-csrf", csrf_for(ref, name, role)}, {"content-type", "application/json"}, {"sec-fetch-site", "same-origin"}};
        for (const auto& e : extra) {
            if (e.second == "<erase>") h.erase(e.first);
            else h[e.first] = e.second;
        }
        return call(req("POST", path, h, body));
    }

    std::string submit_map(const std::string& name, const std::string& address = "203.0.113.5") {
        SubmitInput in;
        in.kind = "classic";
        in.name = name;
        in.maker = "Mia";
        std::ifstream f(fs::path(maps_dir()) / "TINY.LVL", std::ios::binary);
        in.bytes = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        in.address = address;
        const SubmitOutcome out = store->submit(in, now);
        return out.ok ? out.submission.id : std::string();
    }
};

const char* const kClosedBody = "{\"error\":\"closed\",\"message\":\"The admin page is closed.\"}";

}  // namespace

int main() {
    std::cout << "=======================================================\n THE ADMIN PAGE'S PASS AND THE DOOR THAT CHECKS IT\n=======================================================\n";

    TEST_CASE("PASS1.1 The keys are made as docs/HUB.md says (the RAW digest of HMAC with the secret as KEY and the label as MESSAGE), against the known answer; a pass that an independent program made is accepted, one made with the hex text of the key, or with key and message swapped, is not") {
        ASSERT_EQ(hex_encode(hub_key(kSecret)), std::string(kPassKeyHex));                           // (the test's own maker is the hub's: the known answer of the document)
        AdminGate gate(kSecret);
        ASSERT_TRUE(gate.open());
        AdminIdentity who;
        ASSERT_TRUE(gate.check(kPythonPass, kT0, who) == PassVerdict::Ok);                           // (made by tests/scripts/admin_pass.py)
        ASSERT_TRUE(who.ref == kRefDave && who.name == "dave" && who.role == "admin" && who.nonce == "00112233445566ff" && who.exp == kT0 + 60);
        const std::string payload = payload_json(kRefDave, "dave", "admin", kT0 + 60, nonce_of(1));
        const std::string hex_as_key = sign_with(kPassKeyHex, payload);                              // (the 64-character text fed as the key)
        const std::string swapped = sign_with(hmac_sha256("ants-admin-pass-v1", kSecret), payload);  // (label as key, secret as message)
        const std::string plain_secret = sign_with(kSecret, payload);                                // (no derived key at all)
        for (const std::string& bad : {hex_as_key, swapped, plain_secret}) {
            AdminGate g(kSecret);
            ASSERT_TRUE(g.check(bad, kT0, who) == PassVerdict::BadPass);
        }
        AdminGate right(kSecret);
        ASSERT_TRUE(right.check(sign_with(hub_key(kSecret), payload), kT0, who) == PassVerdict::Ok);
        // the page's token has a key of the server's own: it is not the hub's token (docs/HUB.md: label ants-admin-csrf-v1), and it is the one that an independent program makes
        AdminIdentity dave, mia;
        dave.ref = kRefDave;
        mia.ref = kRefMia;
        ASSERT_EQ(gate.csrf_token(dave), std::string(kPythonCsrfDave));
        ASSERT_EQ(gate.csrf_token(mia), std::string(kPythonCsrfMia));
        ASSERT_EQ(AdminGate("another-secret-that-is-long-enough-1234").csrf_token(dave).size(), size_t{32});
        ASSERT_NE(AdminGate("another-secret-that-is-long-enough-1234").csrf_token(dave), gate.csrf_token(dave));
        ASSERT_EQ(hex_encode(hmac_sha256(kSecret, "ants-admin-csrf-v1")), std::string("03a2b6b94fabcf8ae018852e8e6c0b1ec691375286a0263168238796556e3018"));       // (the hub's key for its own token)
        ASSERT_TRUE(gate.csrf_token(dave).find("03a2b6b9") == std::string::npos);
    } TEST_END();

    TEST_CASE("PASS1.2 The key: 32 to 256 printable characters without a blank and at least 8 different ones, cut of the blanks at both ends; anything else (or nothing) is no key and every pass is refused, and a wrong value never throws") {
        const std::string good = std::string(kSecret);
        ASSERT_TRUE(AdminGate(good).open() && AdminGate(good).given());
        ASSERT_TRUE(AdminGate(" \t\r\n" + good + "\n \x0b\x0c").open());                              // (the blanks that the hub cuts)
        AdminIdentity who;
        {   // the same key with blanks around it checks the passes that the cut key signs
            AdminGate padded("\n" + good + "  \r\n");
            Maker m;
            ASSERT_TRUE(padded.check(m.make(kT0), kT0, who) == PassVerdict::Ok);
        }
        const std::string short31(31, 'a');
        ASSERT_FALSE(gate_secret_is_good(short31 + "b"));                                            // (32 characters, 2 different)
        ASSERT_FALSE(gate_secret_is_good("abcdefghijklmnopqrstuvwxyz01234"));                        // (31 characters)
        ASSERT_TRUE(gate_secret_is_good("abcdefghijklmnopqrstuvwxyz012345"));                        // (32)
        ASSERT_FALSE(gate_secret_is_good("abcdefgh" + std::string(249, 'x')));                       // (257: one over)
        ASSERT_TRUE(gate_secret_is_good("abcdefgh" + std::string(248, 'x')));                        // (256, 9 different)
        ASSERT_FALSE(gate_secret_is_good("abcdefg" + std::string(30, 'a')));                         // (37 characters but 7 different)
        ASSERT_TRUE(gate_secret_is_good("abcdefgh" + std::string(30, 'a')));                         // (8 different)
        ASSERT_FALSE(gate_secret_is_good("abcdefghijklmnopqrstuvwxyz 0123456789"));                  // (a blank inside)
        ASSERT_FALSE(gate_secret_is_good(std::string("abcdefghijklmnopqrstuvwxyz0123456789\x01")));
        ASSERT_FALSE(gate_secret_is_good("abcdefghijklmnopqrstuvwxyz0123456789\xc3\xa9"));            // (not ASCII)
        for (const std::string& bad : {std::string(), std::string("   "), short31, std::string(300, 'k'), std::string("abcdefghijklmnopqrstuvwxyz 0123456789ABC"), std::string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")}) {
            AdminGate shut(bad);
            ASSERT_FALSE(shut.open());
            ASSERT_EQ(shut.given(), !bad.empty() && bad.find_first_not_of(" ") != std::string::npos);
            Maker m;
            m.secret = good;
            ASSERT_TRUE(shut.check(m.make(kT0), kT0, who) == PassVerdict::NoKey);                    // (no key: nothing is accepted, whatever the pass)
            ASSERT_EQ(shut.csrf_token(who), std::string());
            ASSERT_EQ(shut.remembered(), size_t{0});
        }
    } TEST_END();

    TEST_CASE("PASS1.3 A pass is refused for a wrong mac (of another key, of a changed payload, of a changed character), a pass of another form, one that has ended or lasts too long, a role that is not admin or owner, and a payload that is not exactly the five members") {
        AdminIdentity who;
        const std::string key = hub_key(kSecret);
        const auto verdict = [&](const std::string& pass, int64_t at = kT0) {
            AdminGate gate(kSecret);
            return gate.check(pass, at, who);
        };
        const std::string base = payload_json(kRefDave, "dave", "admin", kT0 + 60, nonce_of(7));
        ASSERT_TRUE(verdict(sign_with(key, base)) == PassVerdict::Ok);
        // the mac
        ASSERT_TRUE(verdict(sign_with(hub_key("some-other-key-that-is-long-enough-1234567"), base)) == PassVerdict::BadPass);              // (another secret: a mismatch between the two services)
        std::string flipped = sign_with(key, base);
        flipped.back() = flipped.back() == '0' ? '1' : '0';
        ASSERT_TRUE(verdict(flipped) == PassVerdict::BadPass);
        std::string upper = sign_with(key, base);
        for (size_t i = upper.rfind('.') + 1; i < upper.size(); ++i) upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper[i])));
        ASSERT_TRUE(verdict(upper) == PassVerdict::BadPass || upper == sign_with(key, base));      // (hex in capitals is not the lower-case text that the hub writes; a mac of digits only is the same text)
        // a payload that was changed after it was signed: the role, the reference, the time
        const std::string signed_pass = sign_with(key, payload_json(kRefMia, "mia", "moderator", kT0 + 60, nonce_of(8)));
        const std::string mac = signed_pass.substr(signed_pass.rfind('.'));
        ASSERT_TRUE(verdict("v1." + base64url_encode(payload_json(kRefMia, "mia", "admin", kT0 + 60, nonce_of(8))) + mac) == PassVerdict::BadPass);
        ASSERT_TRUE(verdict("v1." + base64url_encode(payload_json(kRefDave, "mia", "moderator", kT0 + 60, nonce_of(8))) + mac) == PassVerdict::BadPass);
        ASSERT_TRUE(verdict("v1." + base64url_encode(payload_json(kRefMia, "mia", "moderator", kT0 + 9999, nonce_of(8))) + mac) == PassVerdict::BadPass);
        for (size_t len : {size_t{0}, size_t{1}, size_t{63}, size_t{65}}) {                            // (a mac of another length)
            ASSERT_TRUE(verdict(sign_with(key, base).substr(0, sign_with(key, base).rfind('.') + 1) + std::string(len, 'a')) != PassVerdict::Ok);
        }
        // another form
        ASSERT_TRUE(verdict("") == PassVerdict::NoPass);
        for (const std::string& odd : {std::string("garbage"), std::string("v1."), std::string("v1.."), std::string("v1.abc"), std::string(".."), std::string("v1.a.b.c"), std::string("v2.") + sign_with(key, base).substr(3),
                                       std::string("V1.") + sign_with(key, base).substr(3), std::string(2000, 'x'), sign_with(key, base) + "."}) {
            const PassVerdict v = verdict(odd);
            ASSERT_MSG(v == PassVerdict::Malformed || v == PassVerdict::BadPass, odd.substr(0, 40));
        }
        ASSERT_TRUE(verdict(sign_with(key, base) + "x") == PassVerdict::Malformed);                      // (a mac of 65 characters)
        const auto signed_text = [&](const std::string& payload) { return sign_with(key, payload); };
        ASSERT_TRUE(verdict(signed_text("not json")) == PassVerdict::Malformed);
        ASSERT_TRUE(verdict(signed_text("[1,2,3]")) == PassVerdict::Malformed);
        ASSERT_TRUE(verdict(signed_text("null")) == PassVerdict::Malformed);
        ASSERT_TRUE(verdict(signed_text("")) == PassVerdict::Malformed || verdict(signed_text("")) == PassVerdict::BadPass);
        ASSERT_TRUE(verdict("v1.!!!!." + hex_encode(hmac_sha256(key, "v1.!!!!"))) == PassVerdict::Malformed);                        // (a mac that is right for a payload that is not base64)
        ASSERT_TRUE(verdict(signed_text(std::string(600, ' ') + base)) == PassVerdict::Malformed);                               // (a payload over 512 bytes)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + std::to_string(kT0 + 60) + ",\"exp\":" + std::to_string(kT0 + 60) + ",\"name\":\"d\",\"nonce\":\"" + nonce_of(9) + "\",\"ref\":\"" + kRefDave + "\",\"role\":\"admin\"}")) == PassVerdict::Malformed);   // (a member twice)
        // the time: a pass is good until its end (not at it), and only for as long as the hub makes one
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0, nonce_of(10)))) == PassVerdict::Expired);                       // (ends now)
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 - 1, nonce_of(11)))) == PassVerdict::Expired);
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 + 1, nonce_of(12)))) == PassVerdict::Ok);                         // (one second left)
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 + 60 + 30, nonce_of(13)))) == PassVerdict::Ok);                    // (the slack for a clock that is a little off)
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 + 60 + 31, nonce_of(14)))) == PassVerdict::BadFields);             // (a pass that lasts longer than the hub makes them)
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 + 60, nonce_of(15))), kT0 + 59) == PassVerdict::Ok);
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 + 60, nonce_of(16))), kT0 + 60) == PassVerdict::Expired);
        // the role: admin or owner, and no other word, in no other capitals
        for (const char* role : {"moderator", "mod", "Admin", "OWNER", "administrator", "", "admin ", "none", "root"}) {
            ASSERT_MSG(verdict(sign_with(key, payload_json(kRefDave, "dave", role, kT0 + 60, nonce_of(20)))) == PassVerdict::BadRole, role);
        }
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "dave", "owner", kT0 + 60, nonce_of(21)))) == PassVerdict::Ok && who.role == "owner");
        // exactly the five members, of the right kinds
        const std::string exp = std::to_string(kT0 + 60), n = nonce_of(30);
        const std::string full = "\"name\":\"dave\",\"nonce\":\"" + n + "\",\"ref\":\"" + kRefDave + "\",\"role\":\"admin\"";
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + "," + full + "}")) == PassVerdict::Ok);
        ASSERT_TRUE(verdict(signed_text("{" + full + "}")) == PassVerdict::BadFields);                                                                     // (no exp)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ",\"nonce\":\"" + n + "\",\"ref\":\"" + kRefDave + "\",\"role\":\"admin\"}")) == PassVerdict::BadFields);   // (no name)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ",\"name\":\"dave\",\"ref\":\"" + kRefDave + "\",\"role\":\"admin\"}")) == PassVerdict::BadFields);        // (no nonce)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ",\"name\":\"dave\",\"nonce\":\"" + n + "\",\"role\":\"admin\"}")) == PassVerdict::BadFields);             // (no reference)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ",\"name\":\"dave\",\"nonce\":\"" + n + "\",\"ref\":\"" + kRefDave + "\"}")) == PassVerdict::BadFields);      // (no role)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + "," + full + ",\"admin\":true}")) == PassVerdict::BadFields);                                   // (an extra member)
        ASSERT_TRUE(verdict(signed_text("{\"expires\":" + exp + "," + full + "}")) == PassVerdict::BadFields);                                             // (five members, but one has another name)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + "," + full + ",\"id\":\"123456789012345678\"}")) == PassVerdict::BadFields);                    // (an id has no place in a pass: the extra member is refused whatever it is)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":\"" + exp + "\"," + full + "}")) == PassVerdict::BadFields);                                              // (the time as text)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ".0," + full + "}")) == PassVerdict::BadFields);                                                // (the time as a fraction)
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ",\"name\":7,\"nonce\":\"" + n + "\",\"ref\":\"" + kRefDave + "\",\"role\":\"admin\"}")) == PassVerdict::BadFields);
        ASSERT_TRUE(verdict(signed_text("{\"exp\":" + exp + ",\"name\":\"dave\",\"nonce\":\"" + n + "\",\"ref\":\"" + kRefDave + "\",\"role\":true}")) == PassVerdict::BadFields);
        for (const char* ref : {"0123456789ABCDEF", "0123456789abcde", "0123456789abcdef0", "0123456789abcdeg", "", "0123456789abcde "}) {      // (a reference is 16 lower-case hex characters)
            ASSERT_MSG(verdict(sign_with(key, payload_json(ref, "dave", "admin", kT0 + 60, nonce_of(31)))) == PassVerdict::BadFields, ref);
        }
        for (const char* bad_nonce : {"0011223344556677A", "00112233445566", "", "zzzzzzzzzzzzzzzz", "00112233445566FF"}) {
            ASSERT_MSG(verdict(sign_with(key, payload_json(kRefDave, "dave", "admin", kT0 + 60, bad_nonce))) == PassVerdict::BadFields, bad_nonce);
        }
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, std::string(65, 'n'), "admin", kT0 + 60, nonce_of(32)))) == PassVerdict::BadFields);          // (a name over 64 bytes)
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, std::string(64, 'n'), "admin", kT0 + 60, nonce_of(33)))) == PassVerdict::Ok);
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "da\\u0001ve", "admin", kT0 + 60, nonce_of(34)))) == PassVerdict::BadFields);                  // (a control character in a name)
        ASSERT_TRUE(verdict(sign_with(key, payload_json(kRefDave, "da\\u007fve", "admin", kT0 + 60, nonce_of(35)))) == PassVerdict::BadFields);
        // the words that the log uses for each reason
        ASSERT_EQ(std::string(pass_verdict_word(PassVerdict::BadPass)), std::string("bad_pass"));
        ASSERT_EQ(std::string(pass_verdict_word(PassVerdict::Replayed)), std::string("replay"));
    } TEST_END();

    TEST_CASE("PASS1.4 A pass is used once: the second use is refused, a new nonce is accepted, the table forgets a pass when it has ended, and a full table accepts nothing new until some have ended") {
        AdminGate gate(kSecret, 4);
        AdminIdentity who;
        Maker m;
        const std::string once = m.make(kT0);
        ASSERT_TRUE(gate.check(once, kT0, who) == PassVerdict::Ok);
        ASSERT_TRUE(gate.check(once, kT0, who) == PassVerdict::Replayed);                            // (a copy of a pass that was seen on the way)
        ASSERT_TRUE(gate.check(once, kT0 + 30, who) == PassVerdict::Replayed);
        ASSERT_EQ(gate.remembered(), size_t{1});
        ASSERT_TRUE(gate.check(m.make(kT0), kT0, who) == PassVerdict::Ok);                           // (the next pass of the same account has its own nonce)
        // a pass with the nonce of another but another reference is still a used nonce (the nonce is new for each pass, whoever it is for)
        ASSERT_TRUE(gate.check(sign_with(hub_key(kSecret), payload_json(kRefMia, "mia", "admin", kT0 + 60, nonce_of(0x1001))), kT0, who) == PassVerdict::Replayed);
        ASSERT_TRUE(gate.check(m.make(kT0), kT0, who) == PassVerdict::Ok);
        ASSERT_TRUE(gate.check(m.make(kT0), kT0, who) == PassVerdict::Ok);
        ASSERT_EQ(gate.remembered(), size_t{4});
        ASSERT_TRUE(gate.check(m.make(kT0), kT0, who) == PassVerdict::Full);                         // (the table is full: nothing new, and nothing that was remembered is lost)
        ASSERT_TRUE(gate.check(once, kT0, who) == PassVerdict::Replayed);
        ASSERT_EQ(gate.remembered(), size_t{4});
        // when the remembered passes have ended they are forgotten, and room is made
        ASSERT_TRUE(gate.check(m.make(kT0 + 61), kT0 + 61, who) == PassVerdict::Ok);
        ASSERT_EQ(gate.remembered(), size_t{1});
        ASSERT_TRUE(gate.check(once, kT0 + 61, who) == PassVerdict::Expired);                        // (a pass that has ended is refused for that, whatever is remembered)
        // a refused pass leaves nothing behind: not its nonce
        AdminGate clean(kSecret, 4);
        const std::string expired = m.make(kT0 - 100);
        ASSERT_TRUE(clean.check(expired, kT0, who) == PassVerdict::Expired);
        ASSERT_TRUE(clean.check(sign_with("not the key", payload_json(kRefDave, "dave", "admin", kT0 + 60, nonce_of(0x77))), kT0, who) == PassVerdict::BadPass);
        ASSERT_TRUE(clean.check(sign_with(hub_key(kSecret), payload_json(kRefDave, "dave", "moderator", kT0 + 60, nonce_of(0x78))), kT0, who) == PassVerdict::BadRole);
        ASSERT_EQ(clean.remembered(), size_t{0});
        ASSERT_TRUE(clean.check(sign_with(hub_key(kSecret), payload_json(kRefDave, "dave", "admin", kT0 + 60, nonce_of(0x78))), kT0, who) == PassVerdict::Ok);       // (the nonce of a pass that was refused for its role is free)
        ASSERT_EQ(AdminGate("x").remembered(), size_t{0});
        ASSERT_EQ(kPassNoncesMax, size_t{50000});
        ASSERT_EQ(kPassSeconds, int64_t{60});
    } TEST_END();

    TEST_CASE("PASS1.5 The mac is compared in constant time (a comparison written as == would give the same answers), before the payload is read") {
        AdminGate gate(kSecret);
        AdminIdentity who;
        Maker m;
        const uint64_t before = ctl::constant_time_equal_calls();
        ASSERT_TRUE(gate.check(m.make(kT0), kT0, who) == PassVerdict::Ok);
        ASSERT_EQ(ctl::constant_time_equal_calls(), before + 1);
        const std::string key = hub_key(kSecret);
        ASSERT_TRUE(gate.check(sign_with(key, "not json at all"), kT0, who) == PassVerdict::Malformed);
        ASSERT_EQ(ctl::constant_time_equal_calls(), before + 2);                                      // (a payload that is junk is read only after its mac was compared)
        std::string bad = m.make(kT0);
        bad.back() = bad.back() == 'a' ? 'b' : 'a';
        ASSERT_TRUE(gate.check(bad, kT0, who) == PassVerdict::BadPass);
        ASSERT_EQ(ctl::constant_time_equal_calls(), before + 3);
        ASSERT_TRUE(gate.check("", kT0, who) == PassVerdict::NoPass);
        ASSERT_EQ(ctl::constant_time_equal_calls(), before + 3);                                      // (nothing to compare)
        const std::string junk_payload = "v1.!!!!." + hex_encode(hmac_sha256(key, "v1.!!!!"));
        ASSERT_TRUE(gate.check(junk_payload, kT0, who) == PassVerdict::Malformed);
        ASSERT_EQ(ctl::constant_time_equal_calls(), before + 4);
    } TEST_END();

    TEST_CASE("PASS1.6 The name that a pass shows: the username as the hub gave it; the reference stands in when there is none, and when the name is only digits and as long as an id (a Discord id is never shown, stored or logged as a name)") {
        AdminGate gate(kSecret);
        AdminIdentity who;
        Maker m;
        ASSERT_TRUE(gate.check(m.make(kT0, kRefDave, "dave.the_admin"), kT0, who) == PassVerdict::Ok && who.name == "dave.the_admin");
        ASSERT_TRUE(gate.check(m.make(kT0, kRefDave, ""), kT0, who) == PassVerdict::Ok && who.name == kRefDave);
        for (const std::string& fake_id : {std::string("123456789012345678"), std::string("123456789012345"), std::string("12345678901234567890")}) {            // (15 to 20 digits: the shape of an id)
            ASSERT_TRUE(gate.check(m.make(kT0, kRefDave, fake_id), kT0, who) == PassVerdict::Ok);
            ASSERT_MSG(who.name == kRefDave, fake_id);
        }
        for (const std::string& fine : {std::string("12345678901234"), std::string("123456789012345678901"), std::string("12345678901234x"), std::string("1234567890123456a8")}) {
            ASSERT_TRUE(gate.check(m.make(kT0, kRefDave, fine), kT0, who) == PassVerdict::Ok);
            ASSERT_MSG(who.name == fine, fine);
        }
        ASSERT_TRUE(gate.check(m.make(kT0, kRefDave, "<b>mia</b>"), kT0, who) == PassVerdict::Ok && who.name == "<b>mia</b>");          // (typed text is kept as it is: the page shows it as text, and every answer marks it)
    } TEST_END();

    TEST_CASE("ADM1.1 With a good pass /admin/api/me tells the account (name, reference, role) and the page's token, sets no cookie, and has nothing of the retired sign-in; the operator's account is an owner") {
        Door t;
        const HttpResponse r = t.get("/admin/api/me", t.maker.make(t.now));
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(r.set_cookies.empty());
        const ctl::JsonValue me = parse_text(r.body);
        ASSERT_TRUE(me.get("name").str() == "dave" && me.get("ref").str() == kRefDave && me.get("role").str() == "admin");
        ASSERT_EQ(me.get("csrf").str(), std::string(kPythonCsrfDave));
        ASSERT_TRUE(me.get("untrusted").as_bool_or(false) && me.get("untrusted_fields").size() == 1 && me.get("untrusted_fields").at(0).str() == "name");
        ASSERT_EQ(me.size(), size_t{6});                                                              // (name, ref, role, csrf, untrusted, untrusted_fields: no signed_in, no must_change)
        const ctl::JsonValue owner = parse_text(t.get("/admin/api/me", t.maker.make(t.now, kRefMia, "mia", "owner")).body);
        ASSERT_TRUE(owner.get("role").str() == "owner" && owner.get("ref").str() == kRefMia && owner.get("csrf").str() == kPythonCsrfMia);
        ASSERT_EQ(t.call(t.req("POST", "/admin/api/me", {{"x-ants-admin-pass", t.maker.make(t.now)}})).status, 404);       // (a read only)
        ASSERT_EQ(t.call(t.req("PUT", "/admin/api/me", {{"x-ants-admin-pass", t.maker.make(t.now)}})).status, 404);
        // a cookie is not looked at (the old sign-in cookie of a browser that still has one)
        const HttpResponse old = t.call(t.req("GET", "/admin/api/me", {{"cookie", "ants_admin=v2.stale.cookie"}}));
        ASSERT_TRUE(old.status == 403 && old.body == kClosedBody && old.set_cookies.empty());
    } TEST_END();

    TEST_CASE("ADM1.2 Without a good pass every call under /admin/api gets one answer, byte for byte (403 closed), whatever the path, the method and the reason: no pass, a bad one, an old one, a used one, a pass for another key, a role that is not admin or owner, and a server with no key") {
        Door t;
        const std::string some_id(32, 'a');
        const std::vector<std::pair<std::string, std::string>> calls = {
            {"GET", "/admin/api/me"}, {"GET", "/admin/api/maps"}, {"GET", "/admin/api/maps?state=approved"}, {"GET", "/admin/api/next-name"}, {"GET", "/admin/api/audit"}, {"GET", "/admin/api/maps/" + some_id + "/picture"},
            {"POST", "/admin/api/maps/" + some_id + "/approve"}, {"POST", "/admin/api/maps/" + some_id + "/decline"}, {"POST", "/admin/api/maps/" + some_id + "/take-down"}, {"POST", "/admin/api/maps/" + some_id + "/put-back"},
            {"GET", "/admin/api/users"}, {"POST", "/admin/api/login"}, {"POST", "/admin/api/password"}, {"GET", "/admin/api/does-not-exist"}, {"GET", "/admin/api"}, {"GET", "/admin/api/"}, {"PUT", "/admin/api/me"}, {"DELETE", "/admin/api/maps"}};
        Maker other;
        other.secret = "another-secret-that-is-long-enough-1234";
        Maker moderator;
        AdminGate gate_for_used(kSecret);
        const std::string used = t.maker.make(t.now);
        ASSERT_EQ(t.get("/admin/api/me", used).status, 200);
        const std::vector<std::string> passes = {"", "garbage", used, other.make(t.now), t.maker.make(t.now - 100), t.maker.make(t.now, kRefDave, "dave", "moderator"), sign_with(hub_key(kSecret), "{}")};
        for (const auto& call : calls) {
            for (const std::string& pass : passes) {
                std::map<std::string, std::string> h = {{"content-type", "application/json"}, {"x-ants-csrf", "whatever"}};
                if (!pass.empty()) h["x-ants-admin-pass"] = pass;
                const std::string path = call.second.substr(0, call.second.find('?'));
                const std::string query = call.second.find('?') == std::string::npos ? "" : call.second.substr(call.second.find('?') + 1);
                const HttpResponse r = t.call(t.req(call.first, path, h, "", query));
                ASSERT_MSG(r.status == 403 && r.body == kClosedBody && r.content_type == "application/json" && r.set_cookies.empty(), call.first + " " + call.second + " with pass '" + pass.substr(0, 12) + "'");
            }
        }
        Door off(nullptr, "");                                                                         // (no key: closed whatever comes)
        Door too_short(nullptr, "only-thirty-one-characters-ab12");
        Door half(nullptr, "short");
        for (Door* d : {&off, &too_short, &half}) {
            ASSERT_FALSE(d->api->admin_api_on());
            for (const auto& call : calls) {
                const HttpResponse r = d->call(d->req(call.first, call.second.substr(0, call.second.find('?')), {{"x-ants-admin-pass", t.maker.make(t.now)}, {"content-type", "application/json"}}));
                ASSERT_MSG(r.status == 403 && r.body == kClosedBody, call.first + " " + call.second);
            }
        }
        ASSERT_FALSE(off.api->gate_key_given());
        ASSERT_TRUE(too_short.api->gate_key_given() && half.api->gate_key_given());
        ASSERT_TRUE(t.api->admin_api_on());
        // a good pass still opens the door after all of that (a refusal costs nothing), and a path that is not a route is a 404 for an account that is let in
        const HttpResponse missing = t.get("/admin/api/does-not-exist", t.maker.make(t.now));
        ASSERT_TRUE(missing.status == 404 && missing.body == "{\"error\":\"not_found\",\"message\":\"Not found.\"}");
        ASSERT_EQ(t.get("/admin/api", t.maker.make(t.now)).status, 404);
        ASSERT_EQ(t.get("/admin/api/", t.maker.make(t.now)).status, 404);
        ASSERT_EQ(t.get("/admin/api/maps", t.maker.make(t.now)).status, 200);
    } TEST_END();

    TEST_CASE("ADM1.3 The routes of the retired sign-in are gone: login, logout, the password, the users and the sign-in log's old kinds answer 404 to an account that is let in, and set no cookie") {
        Door t;
        for (const char* path : {"/admin/api/login", "/admin/api/logout", "/admin/api/password", "/admin/api/users", "/admin/api/users/1", "/admin/api/users/1/reset", "/admin/api/users/1/disable", "/admin/api/users/1/enable", "/admin/api/users/1/remove"}) {
            const HttpResponse g = t.get(path, t.maker.make(t.now));
            ASSERT_MSG(g.status == 404 && g.set_cookies.empty(), std::string("GET ") + path);
            const HttpResponse p = t.post(path, "{\"username\":\"evil\",\"role\":\"admin\",\"password\":\"x\"}");
            ASSERT_MSG(p.status == 404 && p.set_cookies.empty() && p.body == g.body, std::string("POST ") + path);
        }
    } TEST_END();

    TEST_CASE("ADM1.4 A change needs the page's token of that account, a same-origin request, the JSON type and a JSON object; a read does not come from another site; the token of another account or of another key is no token") {
        Door t;
        const std::string id = t.submit_map("River Run");
        ASSERT_FALSE(id.empty());
        const std::string path = "/admin/api/maps/" + id + "/approve";
        const std::string csrf = t.csrf_for(kRefDave);
        ASSERT_EQ(csrf, std::string(kPythonCsrfDave));
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"x-ants-csrf", "<erase>"}}).status, 403);                // (no token)
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"x-ants-csrf", ""}}).status, 403);
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"x-ants-csrf", csrf.substr(1)}}).status, 403);
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"x-ants-csrf", csrf + "0"}}).status, 403);
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"x-ants-csrf", t.csrf_for(kRefMia, "mia")}}).status, 403);   // (the token of another account)
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"x-ants-csrf", "03a2b6b94fabcf8ae018852e8e6c0b1e"}}).status, 403);   // (the start of the hub's key is no token)
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"sec-fetch-site", "cross-site"}}).status, 404);               // (from another site: the answer of a path that is not there)
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"sec-fetch-site", "same-site"}}).status, 404);
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"sec-fetch-site", "none"}}).status, 403);                     // (a typed address may read, but a change is made by the page itself)
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"content-type", "text/plain"}}).status, 415);
        ASSERT_EQ(t.post(path, "{}", kRefDave, "dave", "admin", {{"content-type", "<erase>"}}).status, 415);
        ASSERT_EQ(t.post(path, "[1]").status, 400);
        ASSERT_EQ(t.post(path, "{not json").status, 400);
        Submission s;
        ASSERT_TRUE(t.store->get(id, s) && s.state == "waiting");                                      // (nothing of that changed anything)
        ASSERT_EQ(t.refreshed, 0);
        const uint64_t compares = ctl::constant_time_equal_calls();
        ASSERT_EQ(t.post(path).status, 200);
        ASSERT_EQ(ctl::constant_time_equal_calls(), compares + 3);                                    // (the pass of the token's own call, the pass of the change, and the token: each compared in constant time)
        ASSERT_EQ(t.refreshed, 1);
        ASSERT_EQ(t.get("/admin/api/maps", t.maker.make(t.now), "", {{"sec-fetch-site", "cross-site"}}).status, 404);     // (a read that another site sets off learns nothing)
        ASSERT_EQ(t.get("/admin/api/maps", t.maker.make(t.now), "", {{"sec-fetch-site", "same-origin"}}).status, 200);
        ASSERT_EQ(t.get("/admin/api/maps", t.maker.make(t.now), "", {{"sec-fetch-site", "none"}}).status, 200);
        // the same token serves the same account for every pass (it does not change from one pass to the next)
        ASSERT_EQ(t.csrf_for(kRefDave), csrf);
        // a server with another key makes another token for the same account
        Door other(nullptr, "another-secret-that-is-long-enough-1234");
        other.maker.secret = "another-secret-that-is-long-enough-1234";
        ASSERT_NE(other.csrf_for(kRefDave), csrf);
    } TEST_END();

    TEST_CASE("ADM1.5 Every request needs a pass of its own: a pass that was used is refused with the one answer, the reason is told to the operator's log as a kind and nothing else (once a minute for each kind), and it never holds a pass, a name or an address") {
        Door t;
        const std::string pass = t.maker.make(t.now);
        ASSERT_EQ(t.get("/admin/api/maps", pass).status, 200);
        ASSERT_TRUE(t.api->take_notices().empty());
        const HttpResponse again = t.get("/admin/api/maps", pass);
        ASSERT_TRUE(again.status == 403 && again.body == kClosedBody);
        for (int i = 0; i < 5; ++i) t.get("/admin/api/maps", pass);
        std::vector<std::string> notices = t.api->take_notices();
        ASSERT_EQ(notices.size(), size_t{1});                                                          // (six refusals, one line)
        ASSERT_EQ(notices[0], std::string("admin_gate kind=replay"));
        t.get("/admin/api/maps", "");
        t.get("/admin/api/maps", "junk");
        t.get("/admin/api/maps", t.maker.make(t.now - 100));
        Maker other;
        other.secret = "another-secret-that-is-long-enough-1234";
        t.get("/admin/api/maps", other.make(t.now, kRefMia, "mia"));
        t.get("/admin/api/maps", t.maker.make(t.now, kRefDave, "dave", "moderator"));
        notices = t.api->take_notices();
        std::set<std::string> kinds(notices.begin(), notices.end());
        ASSERT_TRUE(kinds == (std::set<std::string>{"admin_gate kind=no_pass", "admin_gate kind=malformed", "admin_gate kind=expired", "admin_gate kind=bad_pass", "admin_gate kind=bad_role"}));
        for (const std::string& line : notices) {
            ASSERT_TRUE(line.find(kSecret) == std::string::npos && line.find("mia") == std::string::npos && line.find("dave") == std::string::npos && line.find(kRefMia) == std::string::npos && line.find("v1.") == std::string::npos);
        }
        t.now += 61;
        t.get("/admin/api/maps", "");
        ASSERT_EQ(t.api->take_notices().size(), size_t{1});                                            // (a minute later the same kind is told again)
        Door off(nullptr, "");
        off.get("/admin/api/maps", off.maker.make(off.now));
        {
            const std::vector<std::string> told = off.api->take_notices();
            ASSERT_TRUE(told.size() == 1 && told[0] == "admin_gate kind=no_key");
        }
        for (int i = 0; i < 300; ++i) t.get("/admin/api/maps", "x" + std::to_string(i));
        ASSERT_TRUE(t.api->take_notices().size() <= 2);                                                // (a flood of refusals does not flood the log)
    } TEST_END();

    TEST_CASE("ADM1.6 The decisions on maps through the door: approve, decline, take down and put back are signed with the username (the maps) and the reference (the review log and the admin log); both roles may; the lobby is told; the list shows who decided") {
        Door t;
        const std::string a = t.submit_map("Run One", "203.0.113.5");
        const std::string b = t.submit_map("Run Two", "203.0.113.6");
        ASSERT_FALSE(a.empty() || b.empty());
        ASSERT_EQ(t.post("/admin/api/maps/" + a + "/approve", "{}", kRefDave, "dave", "admin").status, 200);
        ASSERT_EQ(t.post("/admin/api/maps/" + b + "/decline", "{\"reason\":\"too small\"}", kRefMia, "mia", "owner").status, 200);
        ASSERT_EQ(t.post("/admin/api/maps/" + a + "/take-down", "{}", kRefMia, "mia", "owner").status, 200);
        ASSERT_EQ(t.post("/admin/api/maps/" + a + "/put-back", "{}", kRefDave, "dave", "admin").status, 200);
        ASSERT_EQ(t.refreshed, 3);                                                                     // (an approval, a take-down and a put-back change the lobby's list; a decline does not)
        Submission sa, sb;
        ASSERT_TRUE(t.store->get(a, sa) && t.store->get(b, sb));
        ASSERT_TRUE(sa.state == "approved" && sa.decided_by == "dave" && sb.state == "declined" && sb.decided_by == "mia");
        const ctl::JsonValue approved = parse_text(t.get("/admin/api/maps", t.maker.make(t.now), "state=approved").body);
        ASSERT_EQ(approved.get("maps").size(), size_t{1});
        ASSERT_EQ(approved.get("maps").at(0).get("decided_by").str(), std::string("dave"));
        bool decided_by_marked = false;
        for (size_t i = 0; i < approved.get("untrusted_fields").size(); ++i) decided_by_marked = decided_by_marked || approved.get("untrusted_fields").at(i).str() == "decided_by";
        ASSERT_TRUE(approved.get("untrusted").as_bool_or(false) && decided_by_marked);
        // the review log holds the reference in the place of the account's number
        db::Statement st;
        ASSERT_TRUE(t.database.prepare(st, "SELECT group_concat(actor_id || ':' || actor_name || ':' || action, ',') FROM (SELECT * FROM review_log ORDER BY id)").ok() && st.step() == db::Statement::Step::Row);
        ASSERT_EQ(st.column_text(0), std::string(std::string(kRefDave) + ":dave:approve," + kRefMia + ":mia:decline," + kRefMia + ":mia:take_down," + kRefDave + ":dave:put_back"));
        // the admin log: who (the username), the reference, what, which map, the reason of a decline
        const ctl::JsonValue log = parse_text(t.get("/admin/api/audit", t.maker.make(t.now)).body);
        ASSERT_EQ(log.get("entries").size(), size_t{4});
        const ctl::JsonValue& newest = log.get("entries").at(0);
        ASSERT_TRUE(newest.get("who").str() == "dave" && newest.get("ref").str() == kRefDave && newest.get("action").str() == "map_put_back" && newest.get("target").str() == "Run One" && newest.get("at").as_int_or(0) == t.now);
        const ctl::JsonValue& declined = log.get("entries").at(2);
        ASSERT_TRUE(declined.get("who").str() == "mia" && declined.get("ref").str() == kRefMia && declined.get("action").str() == "map_declined" && declined.get("target").str() == "Run Two" && declined.get("detail").str() == "too small");
        ASSERT_TRUE(log.get("untrusted").as_bool_or(false) && log.get("untrusted_fields").size() == 3);
        const std::string whole = t.get("/admin/api/audit", t.maker.make(t.now)).body + t.get("/admin/api/maps", t.maker.make(t.now), "state=declined").body;
        ASSERT_TRUE(whole.find("password") == std::string::npos && whole.find("addr") == std::string::npos);
        // a decision that is refused is not logged, and not signed
        const size_t lines = t.audit->list(200).size();
        ASSERT_EQ(t.post("/admin/api/maps/" + a + "/approve").status, 409);
        ASSERT_EQ(t.audit->list(200).size(), lines);
        ASSERT_EQ(t.post("/admin/api/maps/" + std::string(32, 'f') + "/approve").status, 404);
    } TEST_END();

    TEST_CASE("ADM1.7 No id is shown or stored: a name that is only digits and as long as an id is signed as the reference, in the answer of /me, in the maps, in the review log and in the admin log") {
        Door t;
        const std::string fake_id = "123456789012345678";
        const ctl::JsonValue me = parse_text(t.get("/admin/api/me", t.maker.make(t.now, kRefDave, fake_id)).body);
        ASSERT_TRUE(me.get("name").str() == kRefDave && me.get("ref").str() == kRefDave);
        const std::string id = t.submit_map("River Run");
        ASSERT_EQ(t.post("/admin/api/maps/" + id + "/approve", "{}", kRefDave, fake_id).status, 200);
        Submission s;
        ASSERT_TRUE(t.store->get(id, s) && s.decided_by == kRefDave);
        std::string everything;
        db::Statement st;
        for (const char* sql : {"SELECT group_concat(decided_by) FROM map_submissions", "SELECT group_concat(actor_id || actor_name) FROM review_log", "SELECT group_concat(actor || actor_ref || target || detail) FROM admin_audit"}) {
            ASSERT_TRUE(t.database.prepare(st, sql).ok() && st.step() == db::Statement::Step::Row);
            everything += st.column_text(0) + "|";
        }
        everything += t.get("/admin/api/audit", t.maker.make(t.now, kRefDave, fake_id)).body + t.get("/admin/api/maps", t.maker.make(t.now), "state=approved").body;
        for (const std::string& line : t.api->take_notices()) everything += line;
        ASSERT_TRUE(everything.find(fake_id) == std::string::npos);
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

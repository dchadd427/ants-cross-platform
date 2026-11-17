#pragma once

// The pass of the admin page (docs/MAP_SUBMISSIONS.md, "The admin page"): the one thing that tells this server who is asking at /admin/api. The site's nginx asks the chat hub's gate before it passes a request on, the gate
// answers with a signed pass, and nginx hands the pass to this server in the header X-Ants-Admin-Pass (what the visitor sent in a header of that name never goes on). The text and the keys are made by the hub (docs/HUB.md,
// "The admin page's door"); this file only checks them. Nobody types a password anywhere, and there is no account in this program.
//
// What this file promises:
//  * The key is the setting ANTS_ADMIN_GATE_SECRET, the same value as the hub's: cut of the blanks at both ends, 32 to 256 printable ASCII characters without a blank and at least 8 different ones, and not the made-up
//    key of the documents' known answer (kSampleGateKey: a setting copied from the documents stays closed). Anything else (or nothing) is NO key, and with no key every pass is refused (the admin page is closed); a wrong
//    value never stops the server.
//  * A pass is `v1.<payload>.<mac>`. The pass key is the RAW 32 bytes of HMAC-SHA-256 with the secret as the KEY and `ants-admin-pass-v1` as the MESSAGE (never the hex text, never the two swapped); the mac is the hex HMAC-SHA-256
//    of `v1.<payload>` under that key, compared in constant time before anything else is looked at. The payload is base64url (no padding) of a JSON object with exactly the members ref (16 lower-case hex characters: the
//    account's reference), name (the Discord username), role ("admin" or "owner", no other word), exp (seconds since 1970, an integer) and nonce (16 hex characters).
//  * exp is checked against this program's clock: a pass that has ended is refused, and so is one that claims to last longer than the hub makes them (60 seconds, with a little slack).
//  * A pass is used ONCE: the nonces of the passes that were accepted are remembered until the pass would have ended anyway, and a second use of one is refused. nginx asks the gate for every request, so a request always
//    brings a new pass; a copy of one that was seen on the way between the two services is of no use. (The table is bounded: when it is full, nothing new is accepted.)
//  * A Discord id is never in a pass, and a name that is as long as an id and all digits is not used as a name (the reference is shown instead).
//  * The token of a change (the page's CSRF token) is made from the account's reference under a key of this server's own (HMAC of the secret with the label `ants-admin-csrf-server-v1`): it is not the hub's token and cannot
//    be made from it, and it is the same for the account until the secret changes.
// One thread, like the rest of the server.

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ants::server {

constexpr size_t kGateSecretMinChars = 32;
constexpr size_t kGateSecretMaxChars = 256;
constexpr size_t kGateSecretMinDistinct = 8;
/// The made-up key whose derived keys docs/HUB.md prints as the known answer: published, so never a key (the hub refuses it as well)
constexpr std::string_view kSampleGateKey = "canary-gate-key-5d8f0c1e7a9b4263-q";
constexpr int64_t kPassSeconds = 60;                 // how long the hub makes a pass last
constexpr int64_t kPassSlackSeconds = 30;            // a pass that ends later than this past kPassSeconds is not one that the hub made
constexpr size_t kPassNoncesMax = 50000;             // the passes of the last minute and a half that are remembered (an administrator's own page makes a handful a minute)

/// Who a pass names: all four are checked
struct AdminIdentity {
    std::string ref;                                 // the account's reference: 16 lower-case hex characters
    std::string name;                                // the name to show: the Discord username, or the reference when the name is no good
    std::string role;                                // "admin" or "owner"
    std::string nonce;                               // 16 hex characters, new for each pass
    int64_t exp{0};
};

/// Why a pass is not good (the word is what the log says; the person at the browser is told only that the page is closed)
enum class PassVerdict { Ok, NoKey, NoPass, Malformed, BadPass, Expired, BadFields, BadRole, Replayed, Full };
const char* pass_verdict_word(PassVerdict verdict);

/// Whether a setting is a good key as it stands (after the blanks at both ends are cut): see the top of this file
bool gate_secret_is_good(std::string_view cut_setting);

class AdminGate {
public:
    /// `setting` is the text of ANTS_ADMIN_GATE_SECRET ("" when it is not set); `max_remembered` is the bound of the table of used passes (a test makes it small)
    explicit AdminGate(std::string_view setting, size_t max_remembered = kPassNoncesMax);

    /// Whether passes can be checked at all
    bool open() const { return open_; }
    /// Whether the setting was there at all (to tell "not set" from "not good" in the log)
    bool given() const { return given_; }
    /// Whether the setting is the documents' sample key (said in the log, apart from "not good")
    bool sample() const { return sample_; }

    /// Checks the whole value of the header X-Ants-Admin-Pass at the time `now` (seconds since 1970). Ok fills `out`, and the pass's nonce is remembered, so that the same pass is refused the next time.
    PassVerdict check(std::string_view header, int64_t now, AdminIdentity& out);

    /// The page's token for the account (32 lower-case hex characters). "" with no key.
    std::string csrf_token(const AdminIdentity& who) const;

    /// The passes that are remembered now (a test holds the bound)
    size_t remembered() const { return seen_.size(); }

private:
    bool open_{false};
    bool given_{false};
    bool sample_{false};
    size_t max_remembered_{kPassNoncesMax};
    std::string pass_key_;                           // 32 raw bytes
    std::string csrf_key_;                           // 32 raw bytes
    std::unordered_set<std::string> seen_;           // the nonces of accepted passes
    std::deque<std::pair<int64_t, std::string>> by_age_;       // (exp, nonce) in the order they were accepted: the front is forgotten once it has ended
};

}  // namespace ants::server

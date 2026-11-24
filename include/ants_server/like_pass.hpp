#pragma once

// The pass of a person who likes a replay (docs/REPLAY_COUNTS.md, "The like door"): the one thing that tells this server which person is asking at /counts. The site's nginx asks the chat hub's like gate before it passes
// a request on, the gate answers with a signed pass for the visitor's login, and nginx hands the pass to this server in the header X-Ants-Like-Pass (what the visitor sent in a header of that name never goes on). The hub
// makes a pass of its own for the one call of Forget me. The text and the keys are made by the hub (hub/likedoor.py); this file only checks them. It is the pass of admin_pass.hpp with its own label and its own members,
// so a pass of one kind is never good for the other.
//
// What this file promises:
//  * The key is the setting ANTS_ADMIN_GATE_SECRET (the one key of the hub and this server; the same rules as admin_pass.hpp: gate_secret_is_good). With no good key every pass is refused (likes are off); a wrong value
//    never stops the server.
//  * A pass is `v1.<payload>.<mac>`. The pass key is the RAW 32 bytes of HMAC-SHA-256 with the secret as the KEY and `ants-like-pass-v1` as the MESSAGE; the mac is the hex HMAC-SHA-256 of `v1.<payload>` under that key,
//    compared in constant time before anything else is looked at. The payload is base64url (no padding) of a JSON object with exactly the members ref (16 lower-case hex characters: the account's reference, never an
//    account id), exp (seconds since 1970, an integer) and nonce (16 hex characters).
//  * exp is checked against this program's clock: a pass that has ended is refused, and so is one that claims to last longer than the hub makes them.
//  * A pass is used ONCE: the nonces of the accepted passes are remembered until the pass would have ended anyway (bounded: when the table is full nothing new is accepted).
//  * The token of a change (the page's CSRF token) is the first 32 hex characters of HMAC-SHA-256 under a key of its own (the secret with the label `ants-like-csrf-v1`) of `csrf1\0` and the reference: the same for a
//    person until the secret changes, and not the token of the admin page.
// One thread, like the rest of the server.

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "ants_server/admin_pass.hpp"

namespace ants::server {

/// Who a like pass names
struct PersonIdentity {
    std::string ref;                                 // the account's reference: 16 lower-case hex characters
    std::string nonce;                               // 16 hex characters, new for each pass
    int64_t exp{0};
};

class LikeGate {
public:
    /// `setting` is the text of ANTS_ADMIN_GATE_SECRET ("" when it is not set); `max_remembered` bounds the table of used passes (a test makes it small)
    explicit LikeGate(std::string_view setting, size_t max_remembered = kPassNoncesMax);

    /// Whether passes can be checked at all
    bool open() const { return open_; }
    /// Checks the whole value of the header X-Ants-Like-Pass at the time `now` (seconds since 1970). Ok fills `out`, and the nonce is remembered, so that the same pass is refused the next time.
    PassVerdict check(std::string_view header, int64_t now, PersonIdentity& out);
    /// The token of the page's changes for the person (32 lower-case hex characters). "" with no key.
    std::string csrf_token(std::string_view ref) const;
    /// The passes that are remembered now (a test holds the bound)
    size_t remembered() const { return seen_.size(); }

private:
    bool open_{false};
    size_t max_remembered_{kPassNoncesMax};
    std::string pass_key_;                           // 32 raw bytes
    std::string csrf_key_;                           // 32 raw bytes
    std::unordered_set<std::string> seen_;
    std::deque<std::pair<int64_t, std::string>> by_age_;
};

}  // namespace ants::server

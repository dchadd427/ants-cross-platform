#pragma once

// The check of names on a server run with --verified-names. A person's Hello may carry a name pass (a short string that the hub signed for one name) in its `token` field: a pass that
// belongs to the name of the Hello is a verified name, and everybody else plays as a guest ("Guest 123456"). A pass can only make a name better, never keep anybody out: whatever is wrong with it,
// the person is seated under a guest name.
//
// The pass: "n1." and the base64url (no padding) of 36 bytes: the account number (8 bytes, little endian), the end of the pass (4 bytes, Unix seconds, little endian), the nonce (8 random bytes, the
// pass's own number) and the first 16 bytes of HMAC-SHA256(secret, "ants-name-ticket-1" 0x00 the first 20 bytes the name). The hub makes it (hub/ticket.py); this file checks it. A pass works once: its
// nonce goes into a window of the passes that have been used and have not run out, at most kNameWindowCap of them (a window that is full seats the newcomer as a guest and drops nothing).
//
// The settings are in docs/PLAYER_NAMES.md. Nothing here writes a name, a pass, an account number or the secret to a log.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "ants_net/person_name.hpp"

namespace ants::server {

inline constexpr size_t kNameWindowCap = 32768;           // used passes that have not run out
inline constexpr uint32_t kNameTicketLifeS = 600;         // the hub's life of a pass
inline constexpr uint32_t kNameTicketSkewS = 30;          // a pass that ends later than life + skew from now is not believed
inline constexpr size_t kNameTicketChars = 51;            // "n1." and 48 characters

/// The guest form (net::is_guest_name): exactly "Guest " and six digits, the first not 0
inline bool is_guest_name(const std::string& name) { return net::is_guest_name(name); }

/// Whether a name is one that a pass may be for: printable ASCII, 2 to 32 characters, no blank at either end, not a bot's name and not like a guest's ("guest" and digits, blanks and case left out)
bool name_fit_for_ticket(const std::string& name);

/// A guest name drawn from `source` (secret.hpp random_bytes by default): "Guest " and a number 100000 .. 999999, every number as likely as any other (a draw that falls in the uneven rest is thrown away).
/// Empty when the source fails.
std::string draw_guest_name(const std::function<bool(uint8_t*, size_t)>& source);
std::string draw_guest_name();

struct NameTicketCheck {
    enum class Result : uint8_t {
        None,         // no pass (the token is empty, or the server has no secret)
        Malformed,    // not a pass: another prefix or length, a character outside base64url, or not 36 bytes
        Bad,          // the MAC is not the one of this name, or the end is too far away, or the name is not one that a pass may be for
        Late,         // the pass has run out (now >= its end)
        Reused,       // the nonce is in the window: the pass was used
        Full,         // the window holds kNameWindowCap passes that have not run out
        Good          // the name is the pass's, and the pass is used up now
    };
    struct Counts {
        uint64_t none{0}, malformed{0}, bad{0}, late{0}, reused{0}, full{0}, good{0};
        bool operator==(const Counts& o) const {
            return none == o.none && malformed == o.malformed && bad == o.bad && late == o.late && reused == o.reused && full == o.full && good == o.good;
        }
    };

    /// `secret`: the bytes of ANTS_NAME_TICKET_SECRET ("": there is none, every check is None). `cap`: the window's size (a test makes it small).
    explicit NameTicketCheck(std::string secret, size_t cap = kNameWindowCap) : secret_(std::move(secret)), cap_(cap) {}

    /// Checks `token` for `name` at `now_unix` (the wall clock, Unix seconds). Good puts the pass's nonce into the window.
    Result check(const std::string& token, const std::string& name, uint64_t now_unix);

    const Counts& counts() const noexcept { return counts_; }
    size_t window_size() const noexcept { return seen_.size(); }
    bool has_secret() const noexcept { return !secret_.empty(); }

private:
    struct Ending {
        uint32_t end;
        uint64_t nonce;
        bool operator>(const Ending& o) const { return end > o.end; }
    };
    Result judge(const std::string& token, const std::string& name, uint64_t now_unix);
    void count(Result r);
    void drop_ended(uint64_t now_unix);

    std::string secret_;
    size_t cap_;
    std::unordered_map<uint64_t, uint32_t> seen_;                                          // nonce -> end
    std::priority_queue<Ending, std::vector<Ending>, std::greater<Ending>> by_end_;        // the passes by their end: the next to run out first
    Counts counts_;
};

/// The door's use of the check: the name that a Hello for a NEW seat gets. A good pass: the name of the Hello. Else a guest name: the Hello's own when it has the guest form, else a drawn one.
class NameGate {
public:
    explicit NameGate(std::string secret, std::function<bool(uint8_t*, size_t)> source = nullptr);

    /// The name for a new seat (see above); counts what happened. `guest` (when given) is set to whether the name was NOT checked: the seat is a guest's, whatever its name looks like
    /// (a second equal guest name of a room without keys is "Guest 123456 (2)": no guest form, still a guest)
    std::string name_for(const std::string& token, const std::string& name, uint64_t now_unix, bool* guest = nullptr);

    NameTicketCheck& check() noexcept { return check_; }
    const NameTicketCheck& check() const noexcept { return check_; }
    uint64_t guests_named() const noexcept { return guests_named_; }
    /// People whose name a room changed because it was another's (the rooms tell the gate; the minute line shows it)
    void add_renamed(uint64_t n) noexcept { renamed_ += n; }

    /// The lines for the log, at most one count line a minute and only when a count moved: "name tickets: good N, none N, malformed N, bad N, late N, reused N, full N, guests named N, renamed in room N";
    /// and, when ten minutes have passed with at least five bad passes and no good one, at most once an hour, "every name ticket failed its check: are the two secrets the same?". Empty when there is nothing to say.
    std::vector<std::string> report(uint64_t now_unix);

private:
    NameTicketCheck check_;
    std::function<bool(uint8_t*, size_t)> source_;
    uint64_t guests_named_{0};
    uint64_t renamed_{0};
    uint64_t drawn_{0};
    // the minute line
    uint64_t last_line_unix_{0};
    NameTicketCheck::Counts shown_counts_;
    uint64_t shown_guests_{0}, shown_renamed_{0};
    // the "same secrets?" line: a window of ten minutes
    uint64_t window_start_unix_{0};
    NameTicketCheck::Counts window_base_;
    uint64_t last_secret_line_unix_{0};
    bool secret_line_said_{false};
};

}  // namespace ants::server

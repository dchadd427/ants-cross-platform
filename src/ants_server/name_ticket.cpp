#include "ants_server/name_ticket.hpp"

#include <algorithm>

#include "ants_ai/bot_names.hpp"
#include "ants_net/protocol.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/secret.hpp"

namespace ants::server {

namespace {

constexpr char kPrefix[] = "n1.";
constexpr size_t kPrefixChars = 3;
constexpr size_t kRawBytes = 36;
constexpr size_t kMacBytes = 16;

uint32_t le32(const std::string& s, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(s[at])) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24);
}

uint64_t le64(const std::string& s, size_t at) { return static_cast<uint64_t>(le32(s, at)) | (static_cast<uint64_t>(le32(s, at + 4)) << 32); }

}  // namespace

bool name_fit_for_ticket(const std::string& name) {
    if (name.size() < 2 || name.size() > net::kMaxNameChars) return false;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) return false;
    }
    if (name.front() == ' ' || name.back() == ' ') return false;
    if (ai::is_reserved_bot_name(name)) return false;
    std::string compact;                                       // (blanks left out, letters in lower case)
    for (const char c : name) {
        if (c == ' ') continue;
        compact.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    if (compact.compare(0, 5, "guest") == 0 && compact.find_first_not_of("0123456789", 5) == std::string::npos) return false;
    return true;
}

std::string draw_guest_name(const std::function<bool(uint8_t*, size_t)>& source) {
    for (int attempt = 0; attempt < 64; ++attempt) {
        uint8_t bytes[4];
        if (!source || !source(bytes, sizeof(bytes))) return std::string();
        std::string name;
        if (net::guest_name_from_bytes(bytes, name)) return name;
    }
    return std::string();
}

std::string draw_guest_name() { return draw_guest_name([](uint8_t* out, size_t n) { return random_bytes(out, n); }); }

void NameTicketCheck::count(Result r) {
    switch (r) {
        case Result::None: ++counts_.none; break;
        case Result::Malformed: ++counts_.malformed; break;
        case Result::Bad: ++counts_.bad; break;
        case Result::Late: ++counts_.late; break;
        case Result::Reused: ++counts_.reused; break;
        case Result::Full: ++counts_.full; break;
        case Result::Good: ++counts_.good; break;
    }
}

void NameTicketCheck::drop_ended(uint64_t now_unix) {
    while (!by_end_.empty() && by_end_.top().end <= now_unix) {
        seen_.erase(by_end_.top().nonce);
        by_end_.pop();
    }
}

NameTicketCheck::Result NameTicketCheck::check(const std::string& token, const std::string& name, uint64_t now_unix) {
    const Result r = judge(token, name, now_unix);
    count(r);
    return r;
}

NameTicketCheck::Result NameTicketCheck::judge(const std::string& token, const std::string& name, uint64_t now_unix) {
    if (secret_.empty() || token.empty()) return Result::None;
    if (token.size() != kNameTicketChars || token.compare(0, kPrefixChars, kPrefix) != 0) return Result::Malformed;
    std::string raw;
    if (!base64url_decode(std::string_view(token).substr(kPrefixChars), raw) || raw.size() != kRawBytes) return Result::Malformed;
    std::string signed_bytes("ants-name-ticket-1", 18);
    signed_bytes.push_back('\0');
    signed_bytes.append(raw, 0, 20);
    signed_bytes.append(name);
    const std::string mac = hmac_sha256(secret_, signed_bytes);
    if (!equal_constant_time(std::string_view(mac).substr(0, kMacBytes), std::string_view(raw).substr(20, kMacBytes))) return Result::Bad;
    const uint64_t end = le32(raw, 8);
    if (now_unix >= end) return Result::Late;
    if (end > now_unix + kNameTicketLifeS + kNameTicketSkewS) return Result::Bad;
    drop_ended(now_unix);
    const uint64_t nonce = le64(raw, 12);
    if (seen_.count(nonce) != 0) return Result::Reused;
    if (seen_.size() >= cap_) return Result::Full;
    if (!name_fit_for_ticket(name)) return Result::Bad;
    seen_.emplace(nonce, static_cast<uint32_t>(end));
    by_end_.push(Ending{static_cast<uint32_t>(end), nonce});
    return Result::Good;
}

NameGate::NameGate(std::string secret, std::function<bool(uint8_t*, size_t)> source)
    : check_(std::move(secret)), source_(source ? std::move(source) : std::function<bool(uint8_t*, size_t)>([](uint8_t* out, size_t n) { return random_bytes(out, n); })) {}

std::string NameGate::name_for(const std::string& token, const std::string& name, uint64_t now_unix) {
    if (check_.check(token, name, now_unix) == NameTicketCheck::Result::Good) return name;
    ++guests_named_;
    if (is_guest_name(name)) return name;
    std::string drawn = draw_guest_name(source_);
    if (drawn.empty()) drawn = "Guest " + std::to_string(100000u + static_cast<uint32_t>(drawn_++ % 900000u));       // (the system's generator failed: the room still makes the name its own)
    return drawn;
}

std::vector<std::string> NameGate::report(uint64_t now_unix) {
    std::vector<std::string> lines;
    const NameTicketCheck::Counts& c = check_.counts();
    if (now_unix >= last_line_unix_ + 60 && (!(c == shown_counts_) || guests_named_ != shown_guests_ || renamed_ != shown_renamed_)) {
        last_line_unix_ = now_unix;
        shown_counts_ = c;
        shown_guests_ = guests_named_;
        shown_renamed_ = renamed_;
        lines.push_back("name tickets: good " + std::to_string(c.good) + ", none " + std::to_string(c.none) + ", malformed " + std::to_string(c.malformed) + ", bad " + std::to_string(c.bad) + ", late " +
                        std::to_string(c.late) + ", reused " + std::to_string(c.reused) + ", full " + std::to_string(c.full) + ", guests named " + std::to_string(guests_named_) + ", renamed in room " + std::to_string(renamed_));
    }
    if (window_start_unix_ == 0) {
        window_start_unix_ = now_unix;
        window_base_ = c;
    } else if (now_unix >= window_start_unix_ + 600) {
        const uint64_t bad = c.bad - window_base_.bad;
        const uint64_t good = c.good - window_base_.good;
        if (bad >= 5 && good == 0 && (!secret_line_said_ || now_unix >= last_secret_line_unix_ + 3600)) {
            secret_line_said_ = true;
            last_secret_line_unix_ = now_unix;
            lines.push_back("every name ticket failed its check: are the two secrets the same?");
        }
        window_start_unix_ = now_unix;
        window_base_ = c;
    }
    return lines;
}

}  // namespace ants::server

#include "ants_net/person_name.hpp"

#include <algorithm>

#include "ants_ai/bot_names.hpp"

namespace ants::net {

namespace {

constexpr uint32_t kGuestNumbers = 900000;                         // 100000 .. 999999
constexpr uint32_t kGuestLimit = (0xFFFFFFFFu / kGuestNumbers) * kGuestNumbers;
constexpr int kGuestTries = 64;                                    // keys asked for before the suffix way is taken

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string trim_blanks(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

bool is_taken(const std::string& name, const std::vector<std::string>& taken) {
    for (const std::string& t : taken) {
        if (same_name(name, t)) return true;
    }
    return false;
}

}  // namespace

bool is_guest_name(const std::string& name) {
    if (name.size() != 12 || name.compare(0, 6, "Guest ") != 0) return false;
    if (name[6] < '1' || name[6] > '9') return false;
    for (size_t i = 7; i < 12; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return true;
}

bool guest_name_from_bytes(const uint8_t* bytes, std::string& out) {
    const uint32_t v = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    if (v >= kGuestLimit) return false;
    out = "Guest " + std::to_string(100000u + v % kGuestNumbers);
    return true;
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

std::string unique_person_name(const std::string& name, const std::vector<std::string>& taken, const std::function<bool(SeatKey&)>& make_key) {
    if (!is_taken(name, taken)) return name;
    if (is_guest_name(name) && make_key) {
        for (int attempt = 0; attempt < kGuestTries; ++attempt) {
            SeatKey key{};
            if (!make_key(key)) break;
            for (size_t at = 0; at + 4 <= key.size(); at += 4) {
                std::string drawn;
                if (guest_name_from_bytes(key.data() + at, drawn) && !is_taken(drawn, taken)) return drawn;
            }
        }
    }
    for (size_t n = 2; n < 100; ++n) {
        const auto made = [&](const std::string& suffix) { return trim_blanks(trim_blanks(name.substr(0, kMaxNameChars - suffix.size())) + suffix); };
        std::string candidate = made(" (" + std::to_string(n) + ")");
        if (ai::is_reserved_bot_name(candidate)) candidate = made(" " + std::to_string(n));       // ("Bot (2)" is a computer player's name: "Bot 2" is not)
        if (!is_taken(candidate, taken)) return candidate;
    }
    return name;
}

}  // namespace ants::net

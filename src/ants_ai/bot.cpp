#include "ants_ai/bot.hpp"

#include <algorithm>
#include <limits>

#include "ants_ai/idle_bot.hpp"

namespace ants::ai {

namespace {

std::string lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// The text of a user's word in an error message: printable ASCII only, and short
std::string quoted(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (out.size() >= 24) {
            out += "...";
            break;
        }
        const unsigned char u = static_cast<unsigned char>(c);
        out.push_back(u >= 0x20 && u <= 0x7E ? c : '?');
    }
    return "'" + out + "'";
}

constexpr uint8_t bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }

int16_t clamp16(int32_t v) noexcept {
    return static_cast<int16_t>(std::clamp<int32_t>(v, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
}

}  // namespace

// ---- levels and profiles -------------------------------------------------------------------------------------------------------------------------

const char* level_name(Level level) noexcept {
    switch (level) {
        case Level::Easy: return "easy";
        case Level::Medium: return "medium";
        case Level::Hard: return "hard";
    }
    return "medium";
}

bool parse_level(std::string_view text, Level& out) noexcept {
    const auto same = [&](const char* word) {
        const std::string_view w(word);
        if (text.size() != w.size()) return false;
        for (size_t i = 0; i < w.size(); ++i) {
            const char c = text[i] >= 'A' && text[i] <= 'Z' ? static_cast<char>(text[i] - 'A' + 'a') : text[i];
            if (c != w[i]) return false;
        }
        return true;
    };
    if (same("easy")) out = Level::Easy;
    else if (same("medium")) out = Level::Medium;
    else if (same("hard")) out = Level::Hard;
    else return false;
    return true;
}

// The proposals of docs/BOTS.md (tuned by tournaments in a later milestone). Times in ticks of 50 ms: 100 ticks = 5 s.
Profile profile_for(Level level) noexcept {
    Profile p;
    switch (level) {
        case Level::Easy:
            p.decision_interval = 100;
            p.reaction_delay = 60;
            p.rate_milli_cps = 400;
            p.burst = 2;
            p.max_ants_per_command = 12;
            p.intent_ttl = 120;
            p.value_aware_piles = false;
            p.max_ants_per_pile = 4;
            break;
        case Level::Medium:
            p.decision_interval = 20;
            p.reaction_delay = 24;
            p.rate_milli_cps = 1500;
            p.burst = 6;
            break;
        case Level::Hard:
            p.decision_interval = 4;
            p.reaction_delay = 8;
            p.rate_milli_cps = 3000;
            p.burst = 10;
            break;
    }
    return p;
}

// ---- the specification of a bot seat -------------------------------------------------------------------------------------------------------------

bool known_bot_kind(std::string_view kind) noexcept { return kind == "idle" || kind == "worker" || kind == "standard"; }

bool parse_bot_spec(std::string_view text, BotSpec& out, std::string& error) {
    error.clear();
    std::vector<std::string_view> parts;
    size_t from = 0;
    while (true) {
        const size_t colon = text.find(':', from);
        parts.push_back(text.substr(from, colon == std::string_view::npos ? std::string_view::npos : colon - from));
        if (colon == std::string_view::npos) break;
        from = colon + 1;
    }
    if (parts.size() > 3) {
        error = "a bot is SEAT, SEAT:LEVEL, SEAT:KIND or SEAT:KIND:LEVEL, not " + quoted(text);
        return false;
    }
    const std::string_view seat = parts[0];
    if (seat.size() != 1 || seat[0] < '0' || seat[0] > '3') {
        error = "the seat of a bot is 0, 1, 2 or 3 (green, red, blue, black), not " + quoted(seat);
        return false;
    }
    BotSpec spec;
    spec.seat = static_cast<uint8_t>(seat[0] - '0');
    if (parts.size() == 2) {
        const std::string word = lower_ascii(parts[1]);
        Level level = Level::Medium;
        if (parse_level(word, level)) {
            spec.kind = "standard";
            spec.level = level;
        } else if (known_bot_kind(word)) {
            spec.kind = word;
        } else {
            error = "unknown bot " + quoted(parts[1]) + " (easy, medium, hard, idle, worker or standard)";
            return false;
        }
    } else if (parts.size() == 3) {
        const std::string kind = lower_ascii(parts[1]);
        if (!known_bot_kind(kind)) {
            error = "unknown bot kind " + quoted(parts[1]) + " (idle, worker or standard)";
            return false;
        }
        Level level = Level::Medium;
        if (!parse_level(parts[2], level)) {
            error = "unknown bot level " + quoted(parts[2]) + " (easy, medium or hard)";
            return false;
        }
        spec.kind = kind;
        spec.level = level;
    }
    out = std::move(spec);
    return true;
}

std::string bot_display_name(const BotSpec& spec) {
    if (spec.kind == "idle") return "Bot (Idle)";
    if (spec.kind == "worker") return "Bot (Worker)";
    std::string level = level_name(spec.level);
    if (!level.empty()) level[0] = static_cast<char>(level[0] - 'a' + 'A');
    return "Bot (" + level + ")";
}

std::string check_setup(const SetupInfo& info) {
    if (info.bots.empty()) return std::string();
    if (info.fog) return "Bots cannot play with Fog of War: a bot would see through it.";
    uint8_t taken = 0;
    for (const BotSpec& b : info.bots) {
        if (b.seat >= sim::MAX_PLAYERS || (info.roster & bit(b.seat)) == 0) {
            return "No bot can sit at seat " + std::to_string(static_cast<unsigned>(b.seat)) + ": it is not in the game.";
        }
        if ((info.human_mask & bit(b.seat)) != 0) {
            return "No bot can sit at seat " + std::to_string(static_cast<unsigned>(b.seat)) + ": a person is there.";
        }
        if ((taken & bit(b.seat)) != 0) return "Seat " + std::to_string(static_cast<unsigned>(b.seat)) + " has two bots.";
        taken = static_cast<uint8_t>(taken | bit(b.seat));
        if (!known_bot_kind(b.kind)) return "Unknown bot kind " + quoted(b.kind) + ".";
    }
    if (!info.allow_all_bots && (info.human_mask & info.roster) == 0) return "A game needs at least one person besides the bots.";
    return std::string();
}

// ---- Orders ------------------------------------------------------------------------------------------------------------------------------------

void Orders::group(sim::CommandType type, const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority) {
    for (size_t from = 0; from < ants.size(); from += sim::kMaxCommandAnts) {
        const size_t to = std::min(ants.size(), from + sim::kMaxCommandAnts);
        sim::Command c;
        c.type = type;
        c.tile_x = clamp16(tile.x);
        c.tile_y = clamp16(tile.y);
        c.ants.assign(ants.begin() + static_cast<std::ptrdiff_t>(from), ants.begin() + static_cast<std::ptrdiff_t>(to));
        intents_.push_back(Intent{std::move(c), priority});
    }
}

void Orders::alliance(sim::CommandType type, uint8_t other, Priority priority) {
    sim::Command c;
    c.type = type;
    c.other_player = other;
    intents_.push_back(Intent{std::move(c), priority});
}

void Orders::move(const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority) { group(sim::CommandType::GroupMove, ants, tile, priority); }

void Orders::attack(const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority) { group(sim::CommandType::GroupAttack, ants, tile, priority); }

void Orders::special(uint32_t ant, sim::TileCoord tile, Priority priority) { group(sim::CommandType::GroupSpecial, std::vector<uint32_t>{ant}, tile, priority); }

void Orders::stop(const std::vector<uint32_t>& ants) {
    for (size_t from = 0; from < ants.size(); from += sim::kMaxCommandAnts) {
        const size_t to = std::min(ants.size(), from + sim::kMaxCommandAnts);
        sim::Command c;
        c.type = sim::CommandType::Stop;
        c.ants.assign(ants.begin() + static_cast<std::ptrdiff_t>(from), ants.begin() + static_cast<std::ptrdiff_t>(to));
        intents_.push_back(Intent{std::move(c), Priority::Normal});
    }
}

void Orders::hatch() {
    sim::Command c;
    c.type = sim::CommandType::Hatch;
    intents_.push_back(Intent{std::move(c), Priority::Normal});
}

void Orders::invite(uint8_t other) { alliance(sim::CommandType::AllianceInvite, other, Priority::Normal); }
void Orders::accept(uint8_t other) { alliance(sim::CommandType::AllianceAccept, other, Priority::Urgent); }
void Orders::deny(uint8_t other) { alliance(sim::CommandType::AllianceDeny, other, Priority::Urgent); }
void Orders::withdraw(uint8_t other) { alliance(sim::CommandType::AllianceWithdraw, other, Priority::Normal); }

void Orders::break_alliance() {
    sim::Command c;
    c.type = sim::CommandType::AllianceBreak;
    intents_.push_back(Intent{std::move(c), Priority::Urgent});
}

void Orders::push_unchecked(sim::Command command, Priority priority) { intents_.push_back(Intent{std::move(command), priority}); }

// ---- the registry ------------------------------------------------------------------------------------------------------------------------------

std::unique_ptr<Bot> make_bot(const BotSpec& spec) {
    if (!known_bot_kind(spec.kind)) return nullptr;
    return std::make_unique<IdleBot>();          // idle; "worker" and "standard" are the idle bot until the worker bot (B3) and the standard bot (B4) exist
}

}  // namespace ants::ai

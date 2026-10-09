#include "ants_ai/bot.hpp"

#include <algorithm>
#include <limits>

#include "ants_ai/idle_bot.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/worker_bot.hpp"

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

const char* style_name(Style style) noexcept {
    switch (style) {
        case Style::Random: return "random";
        case Style::Aggressive: return "aggressive";
        case Style::Economic: return "economic";
        case Style::Raider: return "raider";
        case Style::Defensive: return "defensive";
    }
    return "random";
}

bool parse_style(std::string_view text, Style& out) noexcept {
    for (const Style s : {Style::Random, Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive}) {
        const std::string_view w(style_name(s));
        if (text.size() != w.size()) continue;
        bool same = true;
        for (size_t i = 0; i < w.size() && same; ++i) {
            const char c = text[i] >= 'A' && text[i] <= 'Z' ? static_cast<char>(text[i] - 'A' + 'a') : text[i];
            same = c == w[i];
        }
        if (same) {
            out = s;
            return true;
        }
    }
    return false;
}

bool style_allowed(Level level, Style style) noexcept {
    if (style == Style::Random) return true;
    return level != Level::Hard || style == Style::Aggressive || style == Style::Raider;
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
    if (parts.size() > 4) {
        error = "a bot is SEAT, SEAT:LEVEL, SEAT:KIND, SEAT:KIND:LEVEL, SEAT:LEVEL:STYLE or SEAT:KIND:LEVEL:STYLE, not " + quoted(text);
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
    } else if (parts.size() == 3 || parts.size() == 4) {
        Level level = Level::Medium;
        size_t level_at = 2;
        const std::string kind = lower_ascii(parts[1]);
        Level lone = Level::Medium;
        if (parts.size() == 3 && parse_level(parts[1], lone)) {                          // SEAT:LEVEL:STYLE
            spec.kind = "standard";
            level = lone;
            level_at = 1;
        } else {
            if (!known_bot_kind(kind)) {
                error = "unknown bot kind " + quoted(parts[1]) + " (idle, worker or standard)";
                return false;
            }
            if (!parse_level(parts[2], level)) {
                error = "unknown bot level " + quoted(parts[2]) + " (easy, medium or hard)";
                return false;
            }
            spec.kind = kind;
        }
        spec.level = level;
        const size_t style_at = level_at + 1;
        if (parts.size() > style_at) {
            Style style = Style::Random;
            if (!parse_style(parts[style_at], style)) {
                error = "unknown bot style " + quoted(parts[style_at]) + " (aggressive, economic, raider, defensive or random)";
                return false;
            }
            if (spec.kind != "standard" && style != Style::Random) {
                error = "the " + spec.kind + " bot has no style (only the standard bot has)";
                return false;
            }
            if (!style_allowed(level, style)) {
                error = std::string("a ") + level_name(level) + " bot plays the aggressive or the raider style, not " + quoted(parts[style_at]);
                return false;
            }
            spec.style = style;
        }
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
        if (!known_bot_kind(b.kind) && std::find(info.extra_kinds.begin(), info.extra_kinds.end(), b.kind) == info.extra_kinds.end()) return "Unknown bot kind " + quoted(b.kind) + ".";
        if (b.style != Style::Random && b.kind != "standard" && b.kind.rfind("standard+", 0) != 0) return "The " + b.kind + " bot has no style.";
        if (!style_allowed(b.level, b.style)) return std::string("A ") + level_name(b.level) + " bot cannot play the " + style_name(b.style) + " style.";
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

void Orders::pick_up(uint32_t ant, sim::TileCoord tile, Priority priority) {
    group(sim::CommandType::GroupMove, std::vector<uint32_t>{ant}, tile, priority);
    intents_.back().pickup = true;
}

void Orders::chain(uint32_t ant, const std::vector<ChainStep>& steps, Priority priority) {
    if (steps.empty()) return;
    const uint32_t number = ++chains_;
    for (size_t i = 0; i < steps.size(); ++i) {
        group(sim::CommandType::GroupMove, std::vector<uint32_t>{ant}, steps[i].tile, priority);
        Intent& in = intents_.back();
        in.pickup = steps[i].pickup;
        in.chain = number;
        in.step = static_cast<uint32_t>(i);
        in.gap_lo = steps[i].gap_lo;
        in.gap_hi = steps[i].gap_hi;
    }
}

void Orders::special(uint32_t ant, sim::TileCoord tile, Priority priority) { group(sim::CommandType::GroupSpecial, std::vector<uint32_t>{ant}, tile, priority); }

void Orders::stop(const std::vector<uint32_t>& ants, Priority priority) {
    for (size_t from = 0; from < ants.size(); from += sim::kMaxCommandAnts) {
        const size_t to = std::min(ants.size(), from + sim::kMaxCommandAnts);
        sim::Command c;
        c.type = sim::CommandType::Stop;
        c.ants.assign(ants.begin() + static_cast<std::ptrdiff_t>(from), ants.begin() + static_cast<std::ptrdiff_t>(to));
        intents_.push_back(Intent{std::move(c), priority});
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

void Orders::push_unchecked(sim::Command command, Priority priority, bool pickup) { intents_.push_back(Intent{std::move(command), priority, pickup}); }

// ---- the registry ------------------------------------------------------------------------------------------------------------------------------

std::unique_ptr<Bot> make_bot(const BotSpec& spec) {
    if (!known_bot_kind(spec.kind)) return nullptr;
    if (spec.kind == "idle") return std::make_unique<IdleBot>();
    if (spec.kind == "worker") return std::make_unique<WorkerBot>();
    return std::make_unique<StandardBot>(spec.level, spec.style);
}

}  // namespace ants::ai

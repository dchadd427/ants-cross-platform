#include "ants_server/history_deaths.hpp"

#include <algorithm>
#include <map>
#include <tuple>

#include "ants_ctl/json.hpp"

namespace ants::server {

namespace {

using ctl::JsonValue;

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr uint32_t kMaxSide = 4095;                       // a tile's x and y are 12 bits each
constexpr uint32_t kMaxSecond = 262143;                   // 18 bits: 72 hours
constexpr uint32_t kMaxCount = 262143;                    // 18 bits
constexpr size_t kExactLetters = 9;
constexpr size_t kSquareLetters = 8;
constexpr uint32_t kMaxTotal = 100000000;                 // what a field may say it holds (the page and the sum refuse more)

int letter_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

void put(std::string& out, uint32_t value, int letters) {          // the low `letters` * 6 bits, low letter first
    for (int i = 0; i < letters; ++i) out.push_back(kAlphabet[(value >> (6 * i)) & 63u]);
}

bool take(const std::string& in, size_t at, int letters, uint32_t& value) {
    value = 0;
    for (int i = 0; i < letters; ++i) {
        const int v = letter_value(in[at + static_cast<size_t>(i)]);
        if (v < 0) return false;
        value |= static_cast<uint32_t>(v) << (6 * i);
    }
    return true;
}

uint32_t clamp_to(int32_t v, uint32_t limit) { return v < 0 ? 0u : static_cast<uint32_t>(v) > limit ? limit : static_cast<uint32_t>(v); }

JsonValue head(uint32_t w, uint32_t h, uint64_t n, const char* kind) {
    JsonValue o = JsonValue::make_object();
    o.set("v", JsonValue::make_int(kDeathsVersion));
    o.set("w", JsonValue::make_int(w));
    o.set("h", JsonValue::make_int(h));
    o.set("n", JsonValue::make_int(static_cast<int64_t>(n)));
    o.set("k", JsonValue::make_string(kind));
    return o;
}

// The squares of side `side` that hold the ants (sorted: the map's order is y, x, team, drowned)
std::vector<DeathSquare> square_up(const std::vector<Death>& ants, uint32_t side) {
    std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, uint32_t> counts;
    for (const Death& d : ants) ++counts[std::make_tuple(d.y / side, d.x / side, d.team, d.drowned)];
    std::vector<DeathSquare> out;
    out.reserve(counts.size());
    for (const auto& c : counts) out.push_back(DeathSquare{std::get<1>(c.first), std::get<0>(c.first), std::get<2>(c.first), std::get<3>(c.first), std::min(c.second, kMaxCount)});
    return out;
}

std::string text_of(const JsonValue& v) { return ctl::to_json(v); }

}  // namespace

std::string pack_deaths(const std::vector<Death>& ants) {
    std::string out;
    out.reserve(ants.size() * kExactLetters);
    for (const Death& d : ants) {
        put(out, std::min(d.x, kMaxSide), 2);
        put(out, std::min(d.y, kMaxSide), 2);
        put(out, std::min(d.second, kMaxSecond), 3);
        put(out, (d.team & 3u) | (static_cast<uint32_t>(d.type & 7u) << 2) | (d.drowned ? 32u : 0u) | (static_cast<uint32_t>(d.killer & 7u) << 6), 2);
    }
    return out;
}

bool unpack_deaths(const std::string& packed, std::vector<Death>& out) {
    out.clear();
    if (packed.size() % kExactLetters != 0) return false;
    for (size_t at = 0; at < packed.size(); at += kExactLetters) {
        uint32_t x = 0, y = 0, t = 0, f = 0;
        if (!take(packed, at, 2, x) || !take(packed, at + 2, 2, y) || !take(packed, at + 4, 3, t) || !take(packed, at + 7, 2, f)) return false;
        Death d;
        d.x = x;
        d.y = y;
        d.second = t;
        d.team = static_cast<uint8_t>(f & 3u);
        d.type = static_cast<uint8_t>((f >> 2) & 7u);
        d.drowned = (f & 32u) != 0;
        d.killer = static_cast<uint8_t>((f >> 6) & 7u);
        if (d.type > 5 || (d.killer > 3 && d.killer != kDeathsNone)) return false;
        out.push_back(d);
    }
    return true;
}

std::string pack_squares(const std::vector<DeathSquare>& cells) {
    std::string out;
    out.reserve(cells.size() * kSquareLetters);
    for (const DeathSquare& c : cells) {
        put(out, std::min(c.x, kMaxSide), 2);
        put(out, std::min(c.y, kMaxSide), 2);
        put(out, (c.team & 3u) | (c.drowned ? 4u : 0u), 1);
        put(out, std::min(c.count, kMaxCount), 3);
    }
    return out;
}

bool unpack_squares(const std::string& packed, std::vector<DeathSquare>& out) {
    out.clear();
    if (packed.size() % kSquareLetters != 0) return false;
    for (size_t at = 0; at < packed.size(); at += kSquareLetters) {
        uint32_t x = 0, y = 0, f = 0, n = 0;
        if (!take(packed, at, 2, x) || !take(packed, at + 2, 2, y) || !take(packed, at + 4, 1, f) || !take(packed, at + 5, 3, n)) return false;
        if (f > 7 || n == 0) return false;
        DeathSquare c;
        c.x = x;
        c.y = y;
        c.team = static_cast<uint8_t>(f & 3u);
        c.drowned = (f & 4u) != 0;
        c.count = n;
        out.push_back(c);
    }
    return true;
}

std::string make_history_deaths(const replay::MatchStats& st) {
    if (!st.ok || st.kills.empty() || st.map_width == 0 || st.map_height == 0 || st.map_width > kMaxSide + 1 || st.map_height > kMaxSide + 1) return std::string();
    std::vector<Death> ants;
    ants.reserve(st.kills.size());
    for (const replay::MatchStats::Kill& k : st.kills) {
        if (k.victim >= replay::MatchStats::kSeats) continue;
        Death d;
        d.x = clamp_to(k.tile_x, st.map_width - 1);                                  // (a tile is always on the map; a value that is not is put on its edge)
        d.y = clamp_to(k.tile_y, st.map_height - 1);
        d.second = std::min(k.turn / 20u, kMaxSecond);
        d.team = k.victim;
        d.type = k.victim_type <= 5 ? k.victim_type : static_cast<uint8_t>(0);
        d.drowned = k.drowned;
        d.killer = k.killer < replay::MatchStats::kSeats ? k.killer : static_cast<uint8_t>(kDeathsNone);
        ants.push_back(d);
    }
    if (ants.empty()) return std::string();
    JsonValue o = head(st.map_width, st.map_height, ants.size(), "x");
    o.set("d", JsonValue::make_string(pack_deaths(ants)));
    std::string text = text_of(o);
    if (text.size() <= kDeathsMaxBytes) return text;
    // too many to keep one by one: counts in squares, the smallest squares that fit, and the first and the last kill as single ants
    const Death* first = nullptr;
    const Death* last = nullptr;
    for (const Death& d : ants) {
        if (d.killer == kDeathsNone) continue;
        if (first == nullptr) first = &d;
        last = &d;
    }
    for (uint32_t side = 2; side <= kMaxSide + 1; side *= 2) {
        JsonValue b = head(st.map_width, st.map_height, ants.size(), "b");
        b.set("s", JsonValue::make_int(side));
        b.set("d", JsonValue::make_string(pack_squares(square_up(ants, side))));
        if (first != nullptr) {
            b.set("f", JsonValue::make_string(pack_deaths({*first})));
            b.set("l", JsonValue::make_string(pack_deaths({*last})));
        }
        text = text_of(b);
        if (text.size() <= kDeathsMaxBytes) return text;
    }
    return std::string();                                                           // (not reached: one square holds at most eight kinds of ant)
}

bool parse_history_deaths(const std::string& text, DeathsView& out) {
    out = DeathsView();
    JsonValue v;
    ctl::JsonLimits limits;
    limits.max_bytes = 2u * kDeathsSumMaxBytes;
    limits.max_elements = 16;
    limits.max_depth = 3;
    if (!ctl::parse_json(text, v, nullptr, limits) || !v.is_object() || v.get("v").as_int_or(0) != kDeathsVersion) return false;
    const int64_t w = v.get("w").as_int_or(0), h = v.get("h").as_int_or(0), n = v.get("n").as_int_or(0);
    if (w < 1 || h < 1 || w > kMaxSide + 1 || h > kMaxSide + 1 || n < 1 || n > kMaxTotal || !v.get("d").is_string()) return false;
    out.width = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    out.total = static_cast<uint32_t>(n);
    const std::string kind = v.get("k").as_string_or();
    if (kind == "x") return unpack_deaths(v.get("d").str(), out.ants);
    if (kind != "b") return false;
    const int64_t s = v.get("s").as_int_or(0);
    if (s < 1 || s > kMaxSide + 1) return false;
    out.squares = true;
    out.side = static_cast<uint32_t>(s);
    return unpack_squares(v.get("d").str(), out.cells);
}

std::string add_up_deaths(const std::string& map, uint32_t width, uint32_t height, const std::vector<std::string>& fields) {
    struct Point {
        uint32_t x, y;                                    // the top left tile of the square (or the tile)
        uint8_t team;
        bool drowned;
        uint32_t count;
    };
    std::vector<Point> points;
    uint32_t side = 1;
    uint64_t total = 0;
    size_t matches = 0;
    for (const std::string& text : fields) {
        DeathsView v;
        if (!parse_history_deaths(text, v) || v.width != width || v.height != height) continue;
        ++matches;
        if (v.squares) {
            side = std::max(side, v.side);
            for (const DeathSquare& c : v.cells) {
                points.push_back(Point{c.x * v.side, c.y * v.side, c.team, c.drowned, c.count});
                total += c.count;
            }
        } else {
            for (const Death& d : v.ants) points.push_back(Point{d.x, d.y, d.team, d.drowned, 1u});
            total += v.ants.size();
        }
    }
    if (matches == 0 || points.empty()) return std::string();
    for (;; side *= 2) {
        std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, uint64_t> counts;
        for (const Point& p : points) counts[std::make_tuple(p.y / side, p.x / side, p.team, p.drowned)] += p.count;
        std::vector<DeathSquare> cells;
        cells.reserve(counts.size());
        for (const auto& c : counts) cells.push_back(DeathSquare{std::get<1>(c.first), std::get<0>(c.first), std::get<2>(c.first), std::get<3>(c.first), static_cast<uint32_t>(std::min<uint64_t>(c.second, kMaxCount))});
        JsonValue o = head(width, height, std::min<uint64_t>(total, kMaxTotal), "b");
        o.set("map", JsonValue::make_string(map));
        o.set("matches", JsonValue::make_int(static_cast<int64_t>(matches)));
        o.set("s", JsonValue::make_int(side));
        o.set("d", JsonValue::make_string(pack_squares(cells)));
        const std::string text = text_of(o);
        if (text.size() <= kDeathsSumMaxBytes || side > kMaxSide) return text.size() <= kDeathsSumMaxBytes ? text : std::string();
    }
}

}  // namespace ants::server

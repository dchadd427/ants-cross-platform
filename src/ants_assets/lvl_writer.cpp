#include "ants_assets/lvl_writer.hpp"

#include "ants_assets/chd_parser.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

// The five chances of a waypoint are copied as they are from a double to the file, which holds little-endian IEEE 754 binary64: every target of the game holds
// doubles so (the reader has the same checks, lvl_parser.cpp).
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8, "the .lvl doubles are IEEE 754 binary64");
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the .lvl writer copies the doubles of a level in the host's byte order: the file's is little-endian"
#endif

namespace ants::assets {

namespace {

constexpr size_t kDescriptionAt = 10;         // version (dword), mode (dword), minutes (word), then the description
constexpr size_t kDescriptionBytes = 30;
constexpr size_t kCountAt = 40;               // the dictionary's count word (the number of names less one)
constexpr size_t kNamesAt = 42;
constexpr size_t kNameBytes = 11;
constexpr size_t kMaxBlockEntries = 0xFFFF;   // every count of the file is a word
constexpr size_t kExtendedDescriptionAt = 18;  // version 9: version (dword), mode (dword), minutes (word), format flags (dword), rules (word), the description's length (word), then its bytes

std::string num(uint64_t v) { return std::to_string(v); }

std::string hex(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    do { out.insert(out.begin(), digits[v & 0xFu]); v >>= 4; } while (v != 0);
    return out;
}

// Where the parts of a level's canonical file are (byte offsets from the start), in 64 bits: a level that the writer refuses may be any size
struct Layout {
    uint64_t names_at{0};
    uint64_t grid_size{0};
    uint64_t layer1{0};
    uint64_t block1{0};
    uint64_t block2{0};
    uint64_t block4{0};
    uint64_t egg_word{0};
    uint64_t size{0};
};

Layout layout_of(const LevelData& level) {
    Layout l;
    l.names_at = level.version == LVL_EXTENDED_VERSION ? kExtendedDescriptionAt + static_cast<uint64_t>(level.description.size()) + 2 : kNamesAt;   // (the dictionary's count word is the two bytes before)
    l.grid_size = l.names_at + kNameBytes * static_cast<uint64_t>(level.tile_dictionary.size());
    l.layer1 = l.grid_size + 8;
    const uint64_t cells = static_cast<uint64_t>(level.layer1_terrain.size());
    l.block1 = l.layer1 + 6 * cells + 6 * static_cast<uint64_t>(level.layer2_interactive.size());
    l.block2 = l.block1 + 2 + 6 * static_cast<uint64_t>(level.anthill_spawns.size());
    uint64_t at = l.block2 + 2;
    for (const FoodSchedule& f : level.food_schedules) at += 10 + 4 * static_cast<uint64_t>(f.variants.size());
    l.block4 = at + 4;                                                                // block 3: two words
    at = l.block4 + 2;
    for (const Waypoint& w : level.waypoints) at += w.flag != 0 ? 52u : 8u;       // row, column, flag; with a trigger the interval and five doubles
    l.egg_word = at;
    l.size = at + 2;
    return l;
}

bool zero_bits(double d) {
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof bits);
    return bits == 0;
}

bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string grid_text(uint32_t rows, uint32_t columns) { return num(rows) + " rows x " + num(columns) + " columns"; }

// The first reason why the reader cannot have made `level` within `limits`; empty when it can
std::string check_writable(const LevelData& level, const LevelLimits& limits) {
    const bool extended = level.version == LVL_EXTENDED_VERSION;
    if (extended && !limits.allow_extended) {
        return "the level says version 9 (an Extended map): this place writes Classic maps (version 8) only";
    }
    if (level.version != LVL_EXPECTED_VERSION && !extended) {
        return "the level says version " + num(level.version) + ": the writer writes version 8, the only one the original reads" +
               (limits.allow_extended ? std::string(", and version 9, an Extended map") : std::string());
    }
    if (extended) {
        if ((level.format_flags & ~LVL_KNOWN_FORMAT_FLAGS) != 0) {
            return "the format flags are 0x" + hex(level.format_flags) + ": this writer knows no flag bit (0x" + hex(LVL_KNOWN_FORMAT_FLAGS) + "), and a reader refuses a file with any other";
        }
        if (level.rules > LVL_RULES_EXTENDED) return "the rules word is " + num(level.rules) + ": only 0 (the original's rules) and 1 (extended rules) are known";
    } else if (level.format_flags != 0 || level.rules != 0) {
        return "a version 8 file has no format flags and no rules word, the level holds flags 0x" + hex(level.format_flags) + " and rules " + num(level.rules);
    }
    const uint32_t rows = level.height;
    const uint32_t columns = level.width;
    if (rows == 0 || columns == 0) return "the grid has " + grid_text(rows, columns) + ": a map needs at least one cell";
    if (rows > limits.rows_for(level.version) || columns > limits.columns_for(level.version)) {
        return "the grid has " + grid_text(rows, columns) + ", more than the " + num(limits.rows_for(level.version)) + " x " + num(limits.columns_for(level.version)) + " allowed here";
    }
    const uint64_t cells = static_cast<uint64_t>(rows) * columns;
    if (cells > limits.cells_for(level.version)) return "the grid has " + grid_text(rows, columns) + ", " + num(cells) + " cells, more than the " + num(limits.cells_for(level.version)) + " allowed here";
    if (level.layer1_terrain.empty() && level.layer2_interactive.empty()) {
        return "the grid has " + grid_text(rows, columns) + " but the level holds no cell (blank_level makes none for a grid beyond the limits it is given)";
    }
    if (level.layer1_terrain.size() != cells || level.layer2_interactive.size() != cells) {
        return "the layers hold " + num(level.layer1_terrain.size()) + " and " + num(level.layer2_interactive.size()) + " cells, the grid has " + num(rows) + " x " +
               num(columns) + " = " + num(cells);
    }
    if (extended) {
        if (level.description.size() > limits.description_bytes()) {
            return "the description has " + num(level.description.size()) + " bytes, more than the " + num(limits.description_bytes()) + " allowed here";
        }
    } else if (level.description.size() > kDescriptionBytes) {
        return "the description has " + num(level.description.size()) + " bytes, the file holds 30";
    }
    const size_t zero = level.description.find('\0');
    if (zero != std::string::npos) return "the description holds a zero byte (at " + num(zero) + "): a reader would end it there";
    const size_t names = level.tile_dictionary.size();
    if (names == 0) return "the dictionary is empty: a file holds at least one name";
    if (names > limits.dictionary_names()) {
        return "the dictionary has " + num(names) + " names, more than the " + num(limits.dictionary_names()) + " allowed here (the original's tables hold " +
               num(LVL_MAX_DICTIONARY_ENTRIES) + ")";
    }
    if (static_cast<size_t>(level.tile_type_count) + 1 != names) {
        return "tile_type_count is " + num(level.tile_type_count) + " but the dictionary has " + num(names) + " names (the file stores the number of names less one)";
    }
    for (size_t i = 0; i < names; ++i) {
        const std::string& name = level.tile_dictionary[i];
        if (name.size() > kNameBytes) return "dictionary name #" + num(i) + " (\"" + name + "\") has " + num(name.size()) + " bytes, a name holds 11";
        if (name.find('\0') != std::string::npos) return "dictionary name #" + num(i) + " holds a zero byte: a reader would end it there";
    }
    if (level.anthill_spawns.size() > kMaxBlockEntries) return "block 1 has " + num(level.anthill_spawns.size()) + " records, a file holds at most 65,535";
    for (size_t i = 0; i < level.anthill_spawns.size(); ++i) {
        const AnthillSpawn& sp = level.anthill_spawns[i];
        const int team = start_marker_team_of_id(sp.tile_id);
        const uint8_t expected = team >= 0 ? static_cast<uint8_t>(team) : uint8_t{255};
        if (sp.team_id != expected) {
            return "block 1 record #" + num(i) + " (tile " + num(sp.tile_id) + ", row " + num(sp.y) + ", column " + num(sp.x) + ") says team " + num(sp.team_id) +
                   ", the team of its tile is " + num(expected) + " (255: no team)";
        }
    }
    if (level.food_schedules.size() > kMaxBlockEntries) return "block 2 has " + num(level.food_schedules.size()) + " food piles, a file holds at most 65,535";
    for (size_t i = 0; i < level.food_schedules.size(); ++i) {
        const FoodSchedule& f = level.food_schedules[i];
        if (f.variants.empty()) {
            return "food pile #" + num(i) + " (row " + num(f.y) + ", column " + num(f.x) + ") has no stages: the original stops reading block 2 at such a pile";
        }
        if (f.variants.size() > kMaxBlockEntries) return "food pile #" + num(i) + " has " + num(f.variants.size()) + " stages, a file holds at most 65,535";
    }
    if (level.waypoints.size() > kMaxBlockEntries) return "block 4 has " + num(level.waypoints.size()) + " waypoints, a file holds at most 65,535";
    for (size_t i = 0; i < level.waypoints.size(); ++i) {
        const Waypoint& w = level.waypoints[i];
        if (w.flag != 0) continue;
        bool chances = false;
        for (double p : w.probabilities) chances = chances || !zero_bits(p);
        if (w.param != 0 || chances) {
            return "waypoint #" + num(i) + " (row " + num(w.y) + ", column " + num(w.x) + ") has no trigger (flag 0) but holds an interval or chances: a file holds them "
                   "only for a waypoint with a trigger";
        }
    }
    const uint64_t size = layout_of(level).size;
    // the limit of a written file, and of one that is read: what the writer returns reads back with the same limits
    const size_t most = std::min(limits.write_bytes(), limits.read_bytes());
    if (size > most) return "the file would have " + num(size) + " bytes, more than the " + num(most) + " allowed here";
    return std::string();
}

class Out {
public:
    explicit Out(size_t size) { bytes_.reserve(size); }
    void u16(uint16_t v) {
        bytes_.push_back(static_cast<uint8_t>(v & 0xFFu));
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        u16(static_cast<uint16_t>(v & 0xFFFFu));
        u16(static_cast<uint16_t>(v >> 16));
    }
    void f64(double v) {
        uint8_t raw[8];
        std::memcpy(raw, &v, sizeof raw);                                            // the reader's memcpy: the bits as they are, a NaN's included
        bytes_.insert(bytes_.end(), raw, raw + sizeof raw);
    }
    void text(const std::string& s, size_t field) {                                  // the bytes of the text, then zeros to the end of the field
        bytes_.insert(bytes_.end(), s.begin(), s.end());
        bytes_.insert(bytes_.end(), field - s.size(), uint8_t{0});
    }
    void cell(const MapCell& c) {
        u16(c.tile_index);
        u16(c.flags);
        u16(c.properties);
    }
    std::vector<uint8_t>& bytes() { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

void serialize(const LevelData& level, Out& o) {
    if (level.version == LVL_EXTENDED_VERSION) {
        o.u32(LVL_EXTENDED_VERSION);
        o.u32(level.game_mode);
        o.u16(level.default_minutes);
        o.u32(level.format_flags);
        o.u16(level.rules);
        o.u16(static_cast<uint16_t>(level.description.size()));
        o.text(level.description, level.description.size());
    } else {
        o.u32(LVL_EXPECTED_VERSION);
        o.u32(level.game_mode);
        o.u16(level.default_minutes);
        o.text(level.description, kDescriptionBytes);
    }
    o.u16(static_cast<uint16_t>(level.tile_dictionary.size() - 1));
    for (const std::string& name : level.tile_dictionary) o.text(name, kNameBytes);
    o.u32(level.height);                                                              // the first dword is the number of ROWS (docs/MAP_FORMAT.md)
    o.u32(level.width);
    for (const MapCell& c : level.layer1_terrain) o.cell(c);                          // rows as the outer loop: cell (row r, column c) is entry r * columns + c
    for (const MapCell& c : level.layer2_interactive) o.cell(c);
    o.u16(static_cast<uint16_t>(level.anthill_spawns.size()));
    for (const AnthillSpawn& sp : level.anthill_spawns) {
        o.u16(sp.tile_id);
        o.u16(sp.y);
        o.u16(sp.x);
    }
    o.u16(static_cast<uint16_t>(level.food_schedules.size()));
    for (const FoodSchedule& f : level.food_schedules) {
        o.u16(f.y);
        o.u16(f.x);
        o.u16(f.initial_delay);
        o.u16(f.respawn_interval);
        o.u16(static_cast<uint16_t>(f.variants.size()));
        for (const FoodItemVariant& v : f.variants) {
            o.u16(v.weight);
            o.u16(v.tile_id);
        }
    }
    o.u16(level.ambient_flag);
    o.u16(level.ambient_tile_or_sound);
    o.u16(static_cast<uint16_t>(level.waypoints.size()));
    for (const Waypoint& w : level.waypoints) {
        o.u16(w.y);
        o.u16(w.x);
        o.u32(w.flag);
        if (w.flag == 0) continue;
        o.u32(w.param);
        for (double p : w.probabilities) o.f64(p);
    }
    o.u16(level.boundary_param);
}

std::string cell_text(const LevelData& level, size_t i) {
    const uint32_t columns = level.width;
    if (columns == 0) return "cell #" + num(i);
    return "row " + num(i / columns) + ", column " + num(i % columns);
}

std::string words(const MapCell& c) { return "(" + num(c.tile_index) + ", " + num(c.flags) + ", " + num(c.properties) + ")"; }

bool has_note(const LevelData& level, LevelProblemKind kind) {
    for (const LevelProblem& p : level.load_notes) {
        if (p.kind == kind) return true;
    }
    return false;
}

}  // namespace

LevelWriteResult write_level(const LevelData& level, const LevelLimits& limits) {
    LevelWriteResult result;
    try {
        result.error = check_writable(level, limits);
        if (!result.error.empty()) return result;
        Out out(static_cast<size_t>(layout_of(level).size));
        serialize(level, out);
        // the writer's promise, checked on every call: the bytes read back, with the same limits, as this level
        LevelData back;
        LevelValidation report;
        if (!LVLParser::load_from_memory(out.bytes().data(), out.bytes().size(), back, &report, limits)) {
            result.error = "the written bytes do not load (a fault of the writer): " + report.reason();
            return result;
        }
        std::string difference;
        if (!same_level(level, back, &difference)) {
            result.error = "the written bytes read back as another level (a fault of the writer): " + difference;
            return result;
        }
        result.bytes = std::move(out.bytes());
        result.ok = true;
    } catch (...) {
        result = LevelWriteResult{};
        result.error = "the level cannot be written (out of memory)";
    }
    return result;
}

bool same_level(const LevelData& a, const LevelData& b, std::string* difference) {
    auto differ = [difference](std::string what) {
        if (difference) *difference = std::move(what);
        return false;
    };
    if (a.version != b.version) return differ("version: " + num(a.version) + " / " + num(b.version));
    if (a.game_mode != b.game_mode) return differ("mode: " + num(a.game_mode) + " / " + num(b.game_mode));
    if (a.default_minutes != b.default_minutes) return differ("minutes: " + num(a.default_minutes) + " / " + num(b.default_minutes));
    if (a.format_flags != b.format_flags) return differ("format flags: " + num(a.format_flags) + " / " + num(b.format_flags));
    if (a.rules != b.rules) return differ("rules: " + num(a.rules) + " / " + num(b.rules));
    if (a.description != b.description) return differ("description: \"" + a.description + "\" / \"" + b.description + "\"");
    if (a.tile_type_count != b.tile_type_count) return differ("tile_type_count: " + num(a.tile_type_count) + " / " + num(b.tile_type_count));
    if (a.tile_dictionary.size() != b.tile_dictionary.size()) return differ("dictionary: " + num(a.tile_dictionary.size()) + " / " + num(b.tile_dictionary.size()) + " names");
    for (size_t i = 0; i < a.tile_dictionary.size(); ++i) {
        if (a.tile_dictionary[i] != b.tile_dictionary[i]) return differ("dictionary name #" + num(i) + ": \"" + a.tile_dictionary[i] + "\" / \"" + b.tile_dictionary[i] + "\"");
    }
    if (a.height != b.height || a.width != b.width) return differ("grid: " + grid_text(a.height, a.width) + " / " + grid_text(b.height, b.width));
    const struct { const char* name; const std::vector<MapCell>& x; const std::vector<MapCell>& y; } layers[2] = {
        {"layer 1", a.layer1_terrain, b.layer1_terrain}, {"layer 2", a.layer2_interactive, b.layer2_interactive}};
    for (const auto& layer : layers) {
        if (layer.x.size() != layer.y.size()) return differ(std::string(layer.name) + ": " + num(layer.x.size()) + " / " + num(layer.y.size()) + " cells");
        for (size_t i = 0; i < layer.x.size(); ++i) {
            const MapCell& p = layer.x[i];
            const MapCell& q = layer.y[i];
            if (p.tile_index != q.tile_index || p.flags != q.flags || p.properties != q.properties) {
                return differ(std::string(layer.name) + ", " + cell_text(a, i) + ": " + words(p) + " / " + words(q));
            }
        }
    }
    if (a.anthill_spawns.size() != b.anthill_spawns.size()) return differ("block 1: " + num(a.anthill_spawns.size()) + " / " + num(b.anthill_spawns.size()) + " records");
    for (size_t i = 0; i < a.anthill_spawns.size(); ++i) {
        const AnthillSpawn& p = a.anthill_spawns[i];
        const AnthillSpawn& q = b.anthill_spawns[i];
        if (p.tile_id != q.tile_id || p.y != q.y || p.x != q.x || p.team_id != q.team_id) {
            return differ("block 1 record #" + num(i) + ": tile " + num(p.tile_id) + " at row " + num(p.y) + ", column " + num(p.x) + " (team " + num(p.team_id) + ") / tile " +
                          num(q.tile_id) + " at row " + num(q.y) + ", column " + num(q.x) + " (team " + num(q.team_id) + ")");
        }
    }
    if (a.food_schedules.size() != b.food_schedules.size()) return differ("block 2: " + num(a.food_schedules.size()) + " / " + num(b.food_schedules.size()) + " food piles");
    for (size_t i = 0; i < a.food_schedules.size(); ++i) {
        const FoodSchedule& p = a.food_schedules[i];
        const FoodSchedule& q = b.food_schedules[i];
        if (p.y != q.y || p.x != q.x || p.initial_delay != q.initial_delay || p.respawn_interval != q.respawn_interval || p.variants.size() != q.variants.size()) {
            return differ("food pile #" + num(i) + ": row " + num(p.y) + ", column " + num(p.x) + ", " + num(p.initial_delay) + " units of " + num(p.respawn_interval) +
                          " points, " + num(p.variants.size()) + " stages / row " + num(q.y) + ", column " + num(q.x) + ", " + num(q.initial_delay) + " units of " +
                          num(q.respawn_interval) + " points, " + num(q.variants.size()) + " stages");
        }
        for (size_t k = 0; k < p.variants.size(); ++k) {
            if (p.variants[k].weight != q.variants[k].weight || p.variants[k].tile_id != q.variants[k].tile_id) {
                return differ("food pile #" + num(i) + ", stage #" + num(k) + ": " + num(p.variants[k].weight) + " units, tile " + num(p.variants[k].tile_id) + " / " +
                              num(q.variants[k].weight) + " units, tile " + num(q.variants[k].tile_id));
            }
        }
    }
    if (a.ambient_flag != b.ambient_flag || a.ambient_tile_or_sound != b.ambient_tile_or_sound) {
        return differ("block 3: " + num(a.ambient_flag) + ", " + num(a.ambient_tile_or_sound) + " / " + num(b.ambient_flag) + ", " + num(b.ambient_tile_or_sound));
    }
    if (a.waypoints.size() != b.waypoints.size()) return differ("block 4: " + num(a.waypoints.size()) + " / " + num(b.waypoints.size()) + " waypoints");
    for (size_t i = 0; i < a.waypoints.size(); ++i) {
        const Waypoint& p = a.waypoints[i];
        const Waypoint& q = b.waypoints[i];
        bool chances = true;
        for (size_t k = 0; k < p.probabilities.size(); ++k) chances = chances && same_bits(p.probabilities[k], q.probabilities[k]);
        if (p.y != q.y || p.x != q.x || p.flag != q.flag || p.param != q.param || !chances) {
            return differ("waypoint #" + num(i) + ": row " + num(p.y) + ", column " + num(p.x) + ", flag " + num(p.flag) + ", interval " + num(p.param) + " / row " + num(q.y) +
                          ", column " + num(q.x) + ", flag " + num(q.flag) + ", interval " + num(q.param) + (chances ? "" : " (the chances differ)"));
        }
    }
    if (a.boundary_param != b.boundary_param) return differ("eggs: " + num(a.boundary_param) + " / " + num(b.boundary_param));
    if (difference) difference->clear();
    return true;
}

const char* to_string(RewriteChangeKind kind) noexcept {
    switch (kind) {
        case RewriteChangeKind::NamePadding: return "name_padding";
        case RewriteChangeKind::DescriptionPadding: return "description_padding";
        case RewriteChangeKind::GridSizeHighWords: return "grid_size_high_words";
        case RewriteChangeKind::TrailingBytes: return "trailing_bytes";
        case RewriteChangeKind::Restructured: return "restructured";
        case RewriteChangeKind::Unexplained: return "unexplained";
    }
    return "?";
}

std::vector<RewriteChange> describe_rewrite(const uint8_t* source, size_t size, const LevelData& read, const std::vector<uint8_t>& canonical) {
    std::vector<RewriteChange> out;
    auto add = [&out](RewriteChangeKind kind, uint64_t count, uint64_t offset, std::string message) {
        RewriteChange c;
        c.kind = kind;
        c.count = static_cast<uint32_t>(std::min<uint64_t>(count, 0xFFFFFFFFu));
        c.offset = static_cast<size_t>(offset);
        c.message = std::move(message);
        out.push_back(std::move(c));
    };
    const size_t m = canonical.size();
    if (source != nullptr && size == m && (m == 0 || std::memcmp(source, canonical.data(), m) == 0)) return out;
    if (read.version == LVL_EXTENDED_VERSION) {
        // a version 9 file is read exactly (its reader copies no quirk of the original's), so only bytes after the egg word can differ from its rewrite
        if (source != nullptr && size > m && std::memcmp(source, canonical.data(), m) == 0) {
            add(RewriteChangeKind::TrailingBytes, size - m, m, num(size - m) + " bytes after the egg word are dropped (a reader never reads them)");
        } else {
            add(RewriteChangeKind::Unexplained, 1, 0, "the canonical bytes differ from the file (" + num(m) + " bytes, the file has " + num(size) + "), and nothing a version 9 reader ignores explains it");
        }
        return out;
    }
    const Layout full = layout_of(read);
    if (source == nullptr || full.size != m || m < kNamesAt || size < kNamesAt) {
        add(RewriteChangeKind::Unexplained, 1, 0, "the canonical bytes are not the rewrite of this level and file (" + num(m) + " bytes, the level makes " + num(full.size) + ")");
        return out;
    }
    // (every offset of the layout is at most m, a size_t)
    const size_t grid_size = static_cast<size_t>(full.grid_size);
    const size_t layer1 = static_cast<size_t>(full.layer1);
    const size_t block1 = static_cast<size_t>(full.block1);
    auto first_difference = [&](size_t from, size_t to) -> size_t {                  // the first offset in [from, to) where the two differ; `to` when none
        for (size_t i = from; i < to; ++i) {
            if (source[i] != canonical[i]) return i;
        }
        return to;
    };
    auto unexplained = [&](size_t at, const std::string& where) {
        add(RewriteChangeKind::Unexplained, 1, at, "byte " + num(at) + " (" + where + ") differs from the rewrite, and nothing that the original ignores is there");
    };
    auto all_zero = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            if (source[i] != 0) return false;
        }
        return true;
    };
    // where the rewrite holds zeros (after the end of a text, the upper words of the grid size): the first offset in [from, to) where it does not; `to` when none
    auto first_canonical_nonzero = [&](size_t from, size_t to) -> size_t {
        for (size_t i = from; i < to; ++i) {
            if (canonical[i] != 0) return i;
        }
        return to;
    };
    // From where on is the file laid out otherwise than its rewrite? (the reader's findings say it: a block 2 that ended early, a block 4 or an egg word cut short)
    size_t structural = m;
    std::string restructured;
    if (has_note(read, LevelProblemKind::FoodBlockEndedEarly)) {
        structural = static_cast<size_t>(full.block2);
        restructured = "block 2 of the file ends early at a food pile without stages (the original stops reading the block there and reads blocks 3, 4 and the egg word from "
                       "the bytes behind it): the rewrite holds the " + num(read.food_schedules.size()) + " piles read before it and the blocks as they were read, complete";
    } else if (has_note(read, LevelProblemKind::WaypointBlockTruncated)) {
        structural = static_cast<size_t>(full.block4);
        restructured = "the file ends inside block 4 (the original keeps the waypoints it read and leaves the egg stock to chance): the rewrite holds the " +
                       num(read.waypoints.size()) + " waypoints read, whole, and an egg word of " + num(read.boundary_param);
    } else if (has_note(read, LevelProblemKind::EggStockMissing)) {
        structural = static_cast<size_t>(full.egg_word);
        restructured = "the file ends before its egg word (the original leaves the egg stock to chance): the rewrite writes " + num(read.boundary_param) + ", what the remake plays";
    }
    const size_t comparable = std::min(structural, size);
    if (comparable < layer1) {
        unexplained(comparable, "the file is shorter than the parts that are read as they are");
        return out;
    }
    // the version, the mode and the minutes are read as they are
    size_t at = first_difference(0, kDescriptionAt);
    if (at != kDescriptionAt) unexplained(at, "header");
    // the description: read up to its end; the bytes after the zero that ends it are never looked at
    const size_t field_end = kDescriptionAt + kDescriptionBytes;
    const size_t kept_end = std::min(kDescriptionAt + read.description.size() + 1, field_end);
    at = first_difference(kDescriptionAt, kept_end);
    const size_t description_nonzero = first_canonical_nonzero(kept_end, field_end);
    if (at != kept_end) {
        unexplained(at, "description");
    } else if (description_nonzero != field_end) {
        unexplained(description_nonzero, "the rewrite's description, after its end");
    } else if (!all_zero(kept_end, field_end)) {
        add(RewriteChangeKind::DescriptionPadding, 1, kept_end,
            "the description has leftover bytes after its end (the original reads it up to its end): the rewrite writes zeros there");
    }
    at = first_difference(kCountAt, kNamesAt);
    if (at != kNamesAt) unexplained(at, "the dictionary's count word");
    // the names: the same, name by name
    size_t padded = 0;
    size_t first_padded = 0;
    for (size_t i = 0; i < read.tile_dictionary.size(); ++i) {
        const size_t start = kNamesAt + kNameBytes * i;
        const size_t end = start + kNameBytes;
        const size_t kept = std::min(start + read.tile_dictionary[i].size() + 1, end);
        at = first_difference(start, kept);
        const size_t name_nonzero = first_canonical_nonzero(kept, end);
        if (at != kept) {
            unexplained(at, "dictionary name #" + num(i));
        } else if (name_nonzero != end) {
            unexplained(name_nonzero, "the rewrite's dictionary name #" + num(i) + ", after its end");
        } else if (!all_zero(kept, end)) {
            if (padded == 0) first_padded = kept;
            ++padded;
        }
    }
    if (padded != 0) {
        add(RewriteChangeKind::NamePadding, padded, first_padded,
            num(padded) + " of the " + num(read.tile_dictionary.size()) + " dictionary names have leftover bytes after their end (the original reads a name up to its end): "
            "the rewrite writes zeros there");
    }
    // the grid size: the original keeps the lower word of each dword
    const size_t g = grid_size;
    const size_t rows_differ = first_difference(g, g + 2);
    const size_t columns_differ = first_difference(g + 4, g + 6);
    const size_t rows_high = first_canonical_nonzero(g + 2, g + 4);
    const size_t columns_high = first_canonical_nonzero(g + 6, g + 8);
    if (rows_differ != g + 2 || columns_differ != g + 6) {
        unexplained(rows_differ != g + 2 ? rows_differ : columns_differ, "the grid size");
    } else if (rows_high != g + 4 || columns_high != g + 8) {
        unexplained(rows_high != g + 4 ? rows_high : columns_high, "the rewrite's grid size, above the lower word");
    } else if (source[g + 2] != 0 || source[g + 3] != 0 || source[g + 6] != 0 || source[g + 7] != 0) {
        add(RewriteChangeKind::GridSizeHighWords, 1, g + 2,
            "the grid size dwords carry bits above the lower word (the original keeps " + grid_text(read.height, read.width) + "): the rewrite writes these numbers alone");
    }
    // the layers and the blocks: byte for byte up to where the file is laid out otherwise
    at = first_difference(layer1, comparable);
    if (at != comparable) unexplained(at, at < block1 ? "the layers" : "the blocks");
    if (structural == m) {
        if (size < m) {
            unexplained(size, "the file is shorter than its rewrite and the reader found nothing missing");
        } else if (size > m) {
            add(RewriteChangeKind::TrailingBytes, size - m, m, num(size - m) + " bytes after the egg word are dropped (the original never reads them)");
        }
    } else {
        add(RewriteChangeKind::Restructured, 1, structural, restructured);
    }
    return out;
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------
// Conversion between the Classic map (version 8) and the Extended map (version 9)
// ------------------------------------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr size_t kClassicMostDescription = kDescriptionBytes;      // the file's field (29 characters and a zero byte end it for the original; the checker says so)

// Why a level cannot be a Classic map; `notes` get what does not stop it (null: not wanted). Every sentence names the number and the limit.
std::vector<std::string> classic_reasons(const LevelData& level, std::vector<std::string>* notes) {
    std::vector<std::string> why;
    if (level.version != LVL_EXPECTED_VERSION && level.version != LVL_EXTENDED_VERSION) {
        why.push_back("the level says version " + num(level.version) + ": a map is version 8 (Classic) or 9 (Extended)");
        return why;
    }
    const uint32_t rows = level.height;
    const uint32_t columns = level.width;
    if (rows == 0 || columns == 0) why.push_back("the grid has " + grid_text(rows, columns) + ": a map needs at least one cell");
    if (columns > LVL_MAX_GRID_SIDE) why.push_back("this map is " + num(columns) + " columns wide; a Classic map has at most " + num(LVL_MAX_GRID_SIDE) + " columns");
    if (rows > LVL_MAX_GRID_SIDE) why.push_back("this map is " + num(rows) + " rows tall; a Classic map has at most " + num(LVL_MAX_GRID_SIDE) + " rows");
    const uint64_t cells = static_cast<uint64_t>(rows) * columns;
    if (level.layer1_terrain.size() != cells || level.layer2_interactive.size() != cells) {
        why.push_back("the layers hold " + num(level.layer1_terrain.size()) + " and " + num(level.layer2_interactive.size()) + " cells, the grid has " + num(rows) + " x " + num(columns) +
                      " = " + num(cells));
        return why;                                                                     // (the anchors cannot be looked at)
    }
    if (level.tile_dictionary.size() > LVL_MAX_DICTIONARY_ENTRIES) {
        why.push_back("the dictionary has " + num(level.tile_dictionary.size()) + " names; a Classic map holds at most " + num(LVL_MAX_DICTIONARY_ENTRIES));
    }
    if (level.description.size() == kClassicMostDescription && notes != nullptr) {
        notes->push_back("the description has 30 bytes: the original reads no zero byte that ends it (29 characters fit), and the original-safe check warns");
    }
    if (level.description.size() > kClassicMostDescription) {
        why.push_back("the description is " + num(level.description.size()) + " bytes; a Classic map holds " + num(kClassicMostDescription) + " (29 characters end with the zero byte that the original needs)");
    }
    if (level.format_flags != 0) why.push_back("the map has format flags (0x" + hex(level.format_flags) + "); a Classic map has none");
    if (level.rules > LVL_RULES_EXTENDED) {
        why.push_back("the rules word is " + num(level.rules) + ": only 0 and 1 are known");
    } else if (level.rules == LVL_RULES_EXTENDED && notes != nullptr) {
        notes->push_back("the map asks for the extended rules (rules 1); a Classic map has the original's rules only, so it may play differently (ants cannot cross tile row 90, "
                         "and a long path order can fail)");
    }
    // every object anchor must fit the byte of a Classic map. This loop (O(cells)) can never fire for a version 9 map of 256 x 256 or less, whose anchors are bytes modulo 256
    // (LevelData::object_anchor), and for a larger map it only adds a reason on top of the size reasons above; it stays so that the list is complete, and tells an editor
    // which objects to move when the map is cropped.
    if (level.version == LVL_EXTENDED_VERSION && rows != 0 && columns != 0) {
        uint64_t beyond = 0;
        uint32_t first_row = 0;
        uint32_t first_column = 0;
        for (uint32_t r = 0; r < rows; ++r) {
            for (uint32_t c = 0; c < columns; ++c) {
                int32_t ar = 0;
                int32_t ac = 0;
                if (!level.object_anchor(r, c, ar, ac)) continue;
                if (ar < 0 || ar > 255 || ac < 0 || ac > 255) {
                    if (beyond++ == 0) {
                        first_row = r;
                        first_column = c;
                    }
                }
            }
        }
        if (beyond != 0) {
            why.push_back(num(beyond) + " object cells are anchored outside the 0 to 255 that a Classic anchor byte holds (the first at row " + num(first_row) + ", column " +
                          num(first_column) + ")");
        }
    }
    return why;
}

LevelLimits conversion_limits(bool extended) { return extended ? LevelLimits::format_ceiling() : LevelLimits{}; }   // (no cap of a place: the ceiling of the format, or the 256 a side of a Classic map)

}  // namespace

std::vector<std::string> needs_extended(const LevelData& level) { return convert_to_classic(level).reasons; }     // (the one code path: the reasons of the conversion, so that they cannot drift)

LevelConversion convert_to_extended(const LevelData& level) {
    LevelConversion result;
    try {
        if (level.version != LVL_EXPECTED_VERSION && level.version != LVL_EXTENDED_VERSION) {
            result.reasons.push_back("the level says version " + num(level.version) + ": a map is version 8 (Classic) or 9 (Extended)");
            return result;
        }
        const uint64_t cells = static_cast<uint64_t>(level.height) * level.width;
        if (level.height == 0 || level.width == 0 || level.layer1_terrain.size() != cells || level.layer2_interactive.size() != cells) {
            result.reasons.push_back("the layers hold " + num(level.layer1_terrain.size()) + " and " + num(level.layer2_interactive.size()) + " cells, the grid has " +
                                     grid_text(level.height, level.width) + ": a map needs rows x columns cells in each layer");
            return result;
        }
        LevelData copy(level);
        copy.load_notes.clear();                                  // (what the reader noticed in the source file is no fact of the new one)
        if (level.version == LVL_EXPECTED_VERSION) {
            copy.version = LVL_EXTENDED_VERSION;
            copy.format_flags = 0;
            copy.rules = LVL_RULES_ORIGINAL;                      // the original's rules: the map plays as it did
            const uint32_t columns = copy.width;
            for (size_t i = 0; i < copy.layer2_interactive.size(); ++i) {
                MapCell& cell = copy.layer2_interactive[i];
                const uint32_t row = static_cast<uint32_t>(i / columns);
                const uint32_t column = static_cast<uint32_t>(i % columns);
                if (!level.has_object_anchor(row, column)) continue;           // (empty cells and the bombs, whose word is the owner, keep their word)
                const uint32_t anchor_row = cell.properties & 0xFFu;
                const uint32_t anchor_column = (cell.properties >> 8) & 0xFFu;
                const uint32_t row_offset = (anchor_row - row) & 0xFFu;           // the signed byte that makes the sum modulo 256 (object_anchor)
                const uint32_t column_offset = (anchor_column - column) & 0xFFu;
                cell.properties = static_cast<uint16_t>(row_offset | (column_offset << 8));
            }
        }
        const LevelWriteResult check = write_level(copy, conversion_limits(true));
        if (!check.ok) {
            result.reasons.push_back("the map cannot be written as an Extended map: " + check.error);
            return result;
        }
        result.level = std::move(copy);
        result.ok = true;
    } catch (...) {
        result = LevelConversion{};
        result.reasons.push_back("the map cannot be converted (out of memory)");
    }
    return result;
}

LevelConversion convert_to_classic(const LevelData& level) {
    LevelConversion result;
    try {
        result.reasons = classic_reasons(level, &result.notes);
        if (!result.reasons.empty()) {
            result.notes.clear();
            return result;
        }
        LevelData copy(level);
        copy.load_notes.clear();
        if (level.version == LVL_EXTENDED_VERSION) {
            copy.version = LVL_EXPECTED_VERSION;
            copy.format_flags = 0;
            copy.rules = 0;
            const uint32_t columns = copy.width;
            for (size_t i = 0; i < copy.layer2_interactive.size(); ++i) {
                const uint32_t row = static_cast<uint32_t>(i / columns);
                const uint32_t column = static_cast<uint32_t>(i % columns);
                int32_t anchor_row = 0;
                int32_t anchor_column = 0;
                if (!level.object_anchor(row, column, anchor_row, anchor_column)) continue;
                copy.layer2_interactive[i].properties = static_cast<uint16_t>((static_cast<uint32_t>(anchor_row) & 0xFFu) | ((static_cast<uint32_t>(anchor_column) & 0xFFu) << 8));
            }
        }
        const LevelWriteResult check = write_level(copy, conversion_limits(false));
        if (!check.ok) {
            result.reasons.push_back("the map cannot be written as a Classic map: " + check.error);
            result.notes.clear();
            return result;
        }
        result.level = std::move(copy);
        result.ok = true;
    } catch (...) {
        result = LevelConversion{};
        result.reasons.push_back("the map cannot be converted (out of memory)");
    }
    return result;
}

std::vector<uint16_t> used_tile_ids(const LevelData& level) {
    std::vector<uint8_t> seen(0x10000, 0);
    auto use = [&seen](uint16_t id) {
        if (id != LVL_EMPTY_TILE) seen[id] = 1;
    };
    for (const MapCell& c : level.layer1_terrain) use(c.tile_index);
    for (const MapCell& c : level.layer2_interactive) use(c.tile_index);
    for (const AnthillSpawn& sp : level.anthill_spawns) use(sp.tile_id);
    for (const FoodSchedule& f : level.food_schedules) {
        for (const FoodItemVariant& v : f.variants) use(v.tile_id);
    }
    use(level.ambient_tile_or_sound);
    std::vector<uint16_t> ids;
    for (size_t id = 0; id < seen.size(); ++id) {
        if (seen[id]) ids.push_back(static_cast<uint16_t>(id));
    }
    return ids;
}

std::vector<std::string> tile_names_from_chd(const uint8_t* data, size_t size) {
    CHDHeader header;
    std::vector<AnimationSequence> animations;
    if (!CHDParser::parse_header(data, size, header) || !CHDParser::parse_table4_animations(data, size, header.table4_offset, animations)) return {};
    std::vector<std::string> names;
    names.reserve(animations.size());
    for (const AnimationSequence& a : animations) names.push_back(a.name);
    return names;
}

std::vector<std::string> dictionary_of_used_tiles(const LevelData& level, const std::vector<std::string>& tile_names) {
    const std::vector<uint16_t> used = used_tile_ids(level);
    if (used.empty()) return {"."};
    std::vector<std::string> names(static_cast<size_t>(used.back()) + 1, ".");
    for (const uint16_t id : used) {
        if (id < tile_names.size() && !tile_names[id].empty()) names[id] = tile_names[id];
    }
    return names;
}

namespace {

LevelData blank_of_version(uint32_t version, uint32_t rows, uint32_t columns, const std::vector<std::string>& tile_names, const LevelLimits& limits) {
    LevelData level;
    level.version = version;
    level.game_mode = LVL_GAME_MODE_STANDARD;
    level.default_minutes = 10;                                                       // GAUNTLET and MEDIUM: 10 minutes and 6 eggs (the six originals: 6 to 12 minutes, 2 to 9 eggs)
    level.height.val = rows;
    level.width.val = columns;
    // the cells of a grid beyond the limits are not made: such a level keeps its size and has no cell, so that nothing of its size is allocated (65,535 x 65,535
    // would be 51 GB) and write_level refuses it with the sentence of its grid
    const uint64_t cells = static_cast<uint64_t>(rows) * columns;
    if (rows <= limits.rows_for(version) && columns <= limits.columns_for(version) && cells <= limits.cells_for(version)) {
        level.layer1_terrain.assign(static_cast<size_t>(cells), MapCell{LVL_BLANK_GROUND_TILE, 0, LVL_LAYER1_THIRD_WORD});
        level.layer2_interactive.assign(static_cast<size_t>(cells), MapCell{LVL_EMPTY_TILE, 0, 0});
    }
    level.ambient_flag = 0;
    level.ambient_tile_or_sound = LVL_EMPTY_TILE;                                     // no default ant type
    level.boundary_param = 6;
    level.tile_dictionary = dictionary_of_used_tiles(level, tile_names);
    level.tile_type_count = static_cast<uint16_t>(level.tile_dictionary.size() - 1);
    return level;
}

}  // namespace

LevelData blank_level(uint16_t rows, uint16_t columns, const std::vector<std::string>& tile_names, const LevelLimits& limits) {
    return blank_of_version(LVL_EXPECTED_VERSION, rows, columns, tile_names, limits);
}

LevelData blank_extended_level(uint32_t rows, uint32_t columns, const std::vector<std::string>& tile_names, const LevelLimits& limits) {
    LevelData level = blank_of_version(LVL_EXTENDED_VERSION, rows, columns, tile_names, limits);
    // a map taller than 90 rows or wider than 256 is not one the original's rules play (the row-90 wall, the 8 bit anchors): it asks for the extended rules
    level.rules = (rows > LVL_ORIGINAL_RULES_MOST_ROWS || columns > LVL_MAX_GRID_SIDE) ? LVL_RULES_EXTENDED : LVL_RULES_ORIGINAL;
    return level;
}

}  // namespace ants::assets

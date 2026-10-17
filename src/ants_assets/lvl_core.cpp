#include "ants_assets/lvl_core.hpp"

#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"

#include <array>
#include <exception>
#include <iterator>
#include <new>

namespace ants::assets::lvl_core {

namespace {

constexpr uint32_t kFault = 2;
constexpr uint32_t kMostTileId = 1344;   // the ids that have a name

const char* const kTeams[4] = {"green", "red", "blue", "black"};

const char* const kTileNames[] = {
#include "lvl_tile_names.inc"
};

bool limits_of(Profile profile, LevelLimits& limits) {
    switch (profile) {
        case Profile::NewMap: limits = LevelLimits::for_new_maps(); return true;
        case Profile::Game: limits = LevelLimits{}; return true;
    }
    return false;
}

const char* profile_name(Profile profile) { return profile == Profile::NewMap ? "new_map" : "game"; }

Result refused(const std::string& sentence) { return Result{1, sentence}; }

// JSON text, built by hand: the keys in a fixed order, a string with every byte outside printable ASCII written as \u00XX (so the text is valid whatever the file held)
void put_string(std::string& out, const std::string& text) {
    static const char* const hex = "0123456789abcdef";
    out += '"';
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20 || c >= 0x7F) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += ch;
    }
    out += '"';
}

void put_key(std::string& out, const char* key) {
    out += '"';
    out += key;
    out += "\":";
}

void put_number(std::string& out, const char* key, long long value) {
    put_key(out, key);
    out += std::to_string(value);
}

void put_bool(std::string& out, const char* key, bool value) {
    put_key(out, key);
    out += value ? "true" : "false";
}

void put_text(std::string& out, const char* key, const std::string& value) {
    put_key(out, key);
    put_string(out, value);
}

void put_problems(std::string& out, const std::vector<LevelProblem>& problems) {
    put_key(out, "findings");
    out += '[';
    for (size_t i = 0; i < problems.size(); ++i) {
        const LevelProblem& p = problems[i];
        out += i == 0 ? "\n{" : ",\n{";
        put_text(out, "severity", to_string(p.severity));
        out += ',';
        put_text(out, "code", to_string(p.kind));
        out += ',';
        put_text(out, "message", p.message);
        out += ',';
        put_number(out, "team", p.team);
        out += ',';
        put_number(out, "count", p.count);
        out += '}';
    }
    out += problems.empty() ? "]" : "\n]";
}

Result read_report_impl(const uint8_t* data, size_t size, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    LevelData level;
    LevelValidation load;
    const bool loaded = level.load_from_memory(data, size, &load, limits);
    std::string out = "{";
    put_number(out, "bytes", static_cast<long long>(size));
    out += ',';
    put_text(out, "profile", profile_name(profile));
    out += ',';
    put_bool(out, "loaded", loaded);
    out += ',';
    put_text(out, "reason", loaded ? std::string() : load.reason());
    if (!loaded) {
        out += ",\n";
        put_problems(out, load.problems);
        out += "}";
        return Result{0, out};
    }
    const LevelValidation v = level.validate(0x0F, limits);
    std::array<uint32_t, 4> markers{};
    std::array<uint32_t, 4> hills{};
    for (const AnthillSpawn& sp : level.anthill_spawns) {
        if (sp.team_id < 4) ++markers[sp.team_id];
    }
    for (const MapCell& c : level.layer2_interactive) {
        const int team = hill_team_of_id(c.tile_index);
        if (team >= 0) ++hills[static_cast<size_t>(team)];
    }
    const std::vector<uint16_t> used = used_tile_ids(level);
    out += ",\n";
    put_number(out, "version", level.version);
    out += ',';
    put_number(out, "mode", level.game_mode);
    out += ',';
    put_number(out, "minutes", level.default_minutes);
    out += ',';
    put_text(out, "description", level.description);
    out += ',';
    put_number(out, "description_bytes", static_cast<long long>(level.description.size()));
    out += ",\n";
    put_number(out, "rows", level.height());
    out += ',';
    put_number(out, "columns", level.width());
    out += ',';
    put_number(out, "dictionary_names", static_cast<long long>(level.tile_dictionary.size()));
    out += ',';
    put_number(out, "tile_ids_used", static_cast<long long>(used.size()));
    out += ',';
    put_number(out, "eggs", level.boundary_param);
    out += ',';
    put_number(out, "default_type_tile", level.ambient_tile_or_sound);
    out += ',';
    put_number(out, "food_piles", static_cast<long long>(level.food_schedules.size()));
    out += ',';
    put_number(out, "waypoints", static_cast<long long>(level.waypoints.size()));
    out += ",\n";
    put_key(out, "teams");
    out += '[';
    for (size_t t = 0; t < 4; ++t) {
        out += t == 0 ? "{" : ",{";
        put_text(out, "team", kTeams[t]);
        out += ',';
        put_number(out, "start_markers", markers[t]);
        out += ',';
        put_number(out, "hill_cells", hills[t]);
        out += '}';
    }
    out += "],\n";
    put_bool(out, "playable", v.playable);
    out += ",\n";
    put_problems(out, v.problems);
    out += "}";
    return Result{0, out};
}

Result check_original_impl(const uint8_t* data, size_t size, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    const OriginalSafeReport r = check_original_safe(data, size, limits);
    std::string out = "{";
    put_bool(out, "judged", r.judged);
    out += ',';
    put_bool(out, "works_in_original_game", r.works_in_original_game());
    out += ',';
    put_bool(out, "fits_the_place", r.fits_the_place());
    out += ",\n";
    put_key(out, "findings");
    out += '[';
    for (size_t i = 0; i < r.issues.size(); ++i) {
        const OriginalSafeIssue& p = r.issues[i];
        out += i == 0 ? "\n{" : ",\n{";
        put_text(out, "severity", to_string(p.severity));
        out += ',';
        put_text(out, "code", to_string(p.code));
        out += ',';
        put_text(out, "message", p.message);
        out += ',';
        put_number(out, "row", p.row);
        out += ',';
        put_number(out, "column", p.column);
        out += ',';
        put_number(out, "team", p.team);
        out += ',';
        put_number(out, "count", p.count);
        out += ',';
        put_bool(out, "place_limit", p.place_limit);
        out += '}';
    }
    out += r.issues.empty() ? "]}" : "\n]}";
    return Result{0, out};
}

// The level of a file, as the reader makes it; false (with the sentence) when it does not load
bool load(const uint8_t* data, size_t size, const LevelLimits& limits, LevelData& level, std::string& why) {
    LevelValidation report;
    if (level.load_from_memory(data, size, &report, limits)) return true;
    why = "does not load: " + report.reason();
    return false;
}

Result rewrite_impl(const uint8_t* data, size_t size, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    LevelData level;
    std::string why;
    if (!load(data, size, limits, level, why)) return refused(why);
    const LevelValidation v = level.validate(0x0F, limits);
    if (!v.playable) return refused("does not play with all four teams: " + v.reason());
    const LevelWriteResult w = write_level(level, limits);
    if (!w.ok) return refused("cannot be written: " + w.error);
    return Result{0, std::string(w.bytes.begin(), w.bytes.end())};
}

Result blank_impl(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    if (rows == 0 || columns == 0 || rows > 65535 || columns > 65535) return refused("a blank map needs 1 to 65535 rows and columns");
    if (ground_tile >= kMostTileId) return refused("the ground tile is a tile id from 0 to " + std::to_string(kMostTileId - 1));
    if (eggs > 65535 || minutes > 65535) return refused("the eggs and the minutes are numbers from 0 to 65535");
    const std::vector<std::string> names = tile_names();
    LevelData level = blank_level(static_cast<uint16_t>(rows), static_cast<uint16_t>(columns), names, limits);
    for (MapCell& cell : level.layer1_terrain) cell.tile_index = static_cast<uint16_t>(ground_tile);
    level.boundary_param = static_cast<uint16_t>(eggs);
    level.default_minutes = static_cast<uint16_t>(minutes);
    level.tile_dictionary = dictionary_of_used_tiles(level, names);
    level.tile_type_count = static_cast<uint16_t>(level.tile_dictionary.size() - 1);
    const LevelWriteResult w = write_level(level, limits);
    if (!w.ok) return refused("a blank map of " + std::to_string(rows) + " rows x " + std::to_string(columns) + " columns cannot be written: " + w.error);
    return Result{0, std::string(w.bytes.begin(), w.bytes.end())};
}

Result same_level_impl(const uint8_t* a, size_t size_a, const uint8_t* b, size_t size_b, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    LevelData first;
    LevelData second;
    std::string why;
    if (!load(a, size_a, limits, first, why)) return refused("the first file " + why);
    if (!load(b, size_b, limits, second, why)) return refused("the second file " + why);
    std::string difference;
    const bool same = ants::assets::same_level(first, second, &difference);
    std::string out = "{";
    put_bool(out, "same", same);
    if (!same) {
        out += ',';
        put_text(out, "difference", difference);
    }
    out += '}';
    return Result{0, out};
}

// A fault as a Result, built so that it cannot throw: the sentence is assigned inside its own try, because a string beyond the small-string size allocates, and the
// memory may be exactly what ran out (an empty payload then, which the callers read as "out of memory")
Result fault(const char* sentence) noexcept {
    Result r;
    r.status = kFault;
    try { r.payload = sentence; } catch (...) { r.payload.clear(); }
    return r;
}

// Nothing leaves a call as an exception: a fault of the core (or no memory left) is a status
template <typename Fn>
Result guarded(Fn&& fn) noexcept {
    try {
        return fn();
    } catch (const std::bad_alloc&) {
        return fault("out of memory");
    } catch (const std::exception& e) {
        Result r = fault("internal error");
        try { r.payload = std::string("internal error: ") + e.what(); } catch (...) {}
        return r;
    } catch (...) {
        return fault("internal error");
    }
}

}  // namespace

Result read_report(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return read_report_impl(data, size, profile); }); }
Result check_original(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return check_original_impl(data, size, profile); }); }
Result rewrite(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return rewrite_impl(data, size, profile); }); }
Result blank(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile) {
    return guarded([&] { return blank_impl(rows, columns, ground_tile, eggs, minutes, profile); });
}
Result same_level(const uint8_t* a, size_t size_a, const uint8_t* b, size_t size_b, Profile profile) {
    return guarded([&] { return same_level_impl(a, size_a, b, size_b, profile); });
}

std::vector<std::string> tile_names() { return std::vector<std::string>(std::begin(kTileNames), std::end(kTileNames)); }

}  // namespace ants::assets::lvl_core

#include "ants_assets/lvl_core.hpp"

#include "ants_assets/lvl_checker.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"

#include <array>
#include <exception>
#include <iterator>
#include <new>
#include <unordered_set>

namespace ants::assets::lvl_core {

namespace {

constexpr uint32_t kFault = 2;
constexpr uint32_t kMostTileId = 1344;   // the ids that have a name
constexpr size_t kClassicField = 30;       // the description's field of a Classic map
constexpr size_t kClassicDescription = 29; // what fits in it with the zero byte that ends the text

const char* const kTeams[4] = {"green", "red", "blue", "black"};

const char* const kTileNames[] = {
#include "lvl_tile_names.inc"
};

bool limits_of(Profile profile, LevelLimits& limits) {
    switch (profile) {
        case Profile::NewMap: limits = LevelLimits::for_new_maps(); return true;
        case Profile::Game: limits = LevelLimits{}; return true;
        case Profile::Extended: limits = LevelLimits::for_extended_maps(); return true;
    }
    return false;
}

const char* profile_name(Profile profile) { return profile == Profile::NewMap ? "new_map" : profile == Profile::Game ? "game" : "extended"; }

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

// The findings of an original-safe or per-kind report: "findings":[ ... ] (the same lines for both)
void put_issues(std::string& out, const std::vector<OriginalSafeIssue>& issues) {
    put_key(out, "findings");
    out += '[';
    for (size_t i = 0; i < issues.size(); ++i) {
        const OriginalSafeIssue& p = issues[i];
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
    out += issues.empty() ? "]" : "\n]";
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
    put_issues(out, r.issues);
    out += '}';
    return Result{0, out};
}

Result check_map_impl(const uint8_t* data, size_t size, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    const MapCheckReport r = ants::assets::check_map(data, size, limits);
    std::string out = "{";
    put_text(out, "kind", to_string(r.kind));
    out += ',';
    put_bool(out, "judged", r.report.judged);
    out += ',';
    put_bool(out, "works", r.works);
    out += ',';
    put_bool(out, "fits_the_place", r.fits_the_place);
    out += ',';
    put_text(out, "headline", r.headline);
    out += ',';
    put_text(out, "badge", r.badge);
    out += ",\n";
    put_issues(out, r.report.issues);
    out += '}';
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

// A blank map of either kind: Classic (version 8) under the limits of `profile`, or Extended (version 9) under the Extended limits
Result blank_of_kind(bool extended, uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile) {
    LevelLimits limits;
    if (!limits_of(extended ? Profile::Extended : profile, limits)) return refused("unknown profile");
    if (rows == 0 || columns == 0 || rows > 65535 || columns > 65535) return refused("a blank map needs 1 to 65535 rows and columns");
    if (ground_tile >= kMostTileId) return refused("the ground tile is a tile id from 0 to " + std::to_string(kMostTileId - 1));
    if (eggs > 65535 || minutes > 65535) return refused("the eggs and the minutes are numbers from 0 to 65535");
    const std::vector<std::string> names = tile_names();
    LevelData level = extended ? blank_extended_level(rows, columns, names, limits) : blank_level(static_cast<uint16_t>(rows), static_cast<uint16_t>(columns), names, limits);
    for (MapCell& cell : level.layer1_terrain) cell.tile_index = static_cast<uint16_t>(ground_tile);
    level.boundary_param = static_cast<uint16_t>(eggs);
    level.default_minutes = static_cast<uint16_t>(minutes);
    level.tile_dictionary = dictionary_of_used_tiles(level, names);
    level.tile_type_count = static_cast<uint16_t>(level.tile_dictionary.size() - 1);
    const LevelWriteResult w = write_level(level, limits);
    if (!w.ok) return refused("a blank map of " + std::to_string(rows) + " rows x " + std::to_string(columns) + " columns cannot be written: " + w.error);
    return Result{0, std::string(w.bytes.begin(), w.bytes.end())};
}

Result blank_impl(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile) {
    return blank_of_kind(false, rows, columns, ground_tile, eggs, minutes, profile);
}

// A size that has no limit (the game's own profile reads a file of any size) is null: the maximum of size_t differs between a native build and the wasm module, the text must not
void put_size_limit(std::string& out, const char* key, size_t value) {
    put_key(out, key);
    out += value >= 0xFFFFFFFFu ? std::string("null") : std::to_string(value);
}

Result profile_limits_impl(Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    std::string out = "{";
    put_text(out, "profile", profile_name(profile));
    out += ',';
    put_bool(out, "allow_extended", limits.allow_extended);
    out += ",\n";
    put_key(out, "classic");
    out += '{';
    put_number(out, "rows", limits.rows_for(LVL_EXPECTED_VERSION));
    out += ',';
    put_number(out, "columns", limits.columns_for(LVL_EXPECTED_VERSION));
    out += ',';
    put_number(out, "cells", limits.cells_for(LVL_EXPECTED_VERSION));
    out += "},\n";
    put_key(out, "extended");
    if (limits.allow_extended) {
        out += '{';
        put_number(out, "rows", limits.rows_for(LVL_EXTENDED_VERSION));
        out += ',';
        put_number(out, "columns", limits.columns_for(LVL_EXTENDED_VERSION));
        out += ',';
        put_number(out, "cells", limits.cells_for(LVL_EXTENDED_VERSION));
        out += ',';
        put_number(out, "description_bytes", static_cast<long long>(limits.description_bytes()));
        out += '}';
    } else {
        out += "null";
    }
    out += ",\n";
    put_size_limit(out, "read_bytes", limits.read_bytes());
    out += ',';
    put_size_limit(out, "write_bytes", limits.write_bytes());
    out += ',';
    put_number(out, "dictionary_names", static_cast<long long>(limits.dictionary_names()));
    out += '}';
    return Result{0, out};
}

// ---- the conversions between the two kinds of map ----

// "reasons":[{"code":..,"message":..}, ...] (or "notes"): the sentences with their stable codes, in the order the conversion made them
void put_coded(std::string& out, const char* key, const std::vector<std::string>& codes, const std::vector<std::string>& texts) {
    put_key(out, key);
    out += '[';
    for (size_t i = 0; i < texts.size(); ++i) {
        out += i == 0 ? "\n{" : ",\n{";
        put_text(out, "code", i < codes.size() ? codes[i] : std::string());
        out += ',';
        put_text(out, "message", texts[i]);
        out += '}';
    }
    out += texts.empty() ? "]" : "\n]";
}

// The payload of a conversion (and of set_properties): a dword (little endian) with the size of the JSON, the JSON, then the bytes of the new file
Result framed(const std::string& json, const char* bytes, size_t count) {
    Result r;
    r.payload.reserve(4 + json.size() + count);
    for (int i = 0; i < 4; ++i) r.payload += static_cast<char>((json.size() >> (8 * i)) & 0xFF);
    r.payload += json;
    r.payload.append(bytes, count);
    return r;
}

// What a conversion made: the conversion of the library (reasons, notes, codes), the new file when it was possible
struct Converted {
    LevelConversion conversion;
    std::vector<uint8_t> bytes;
    bool from_extended{false};
};

// A conversion that cannot be written within the limits of the place is no conversion: the map stays as it is, and the reason has the code beyond_place_limits
void refuse_for_place(Converted& c, const std::string& error) {
    c.conversion.ok = false;
    c.conversion.reasons.push_back("this place does not take the map as it would be: " + error);
    c.conversion.reason_codes.push_back("beyond_place_limits");
    c.conversion.notes.clear();
    c.conversion.note_codes.clear();
}

// The file is read as a place that takes Extended maps reads it (a map of either kind), converted, and the copy written under `place`; false (with the sentence) when the file does not load
bool convert_file(bool to_extended, const uint8_t* data, size_t size, const LevelLimits& place, Converted& c, std::string& why) {
    LevelData level;
    if (!load(data, size, LevelLimits::for_extended_maps(), level, why)) return false;
    c.from_extended = level.version == LVL_EXTENDED_VERSION;
    c.conversion = to_extended ? ants::assets::convert_to_extended(level) : ants::assets::convert_to_classic(level);
    level = LevelData{};                                                         // (the source is not needed any more: less memory at the peak)
    if (c.conversion.ok) {
        LevelWriteResult w = write_level(c.conversion.level, place);
        if (w.ok) c.bytes = std::move(w.bytes);
        else refuse_for_place(c, w.error);
        c.conversion.level = LevelData{};
    }
    return true;
}

// A conversion's JSON, then the new file: the payload of convert_to_extended and convert_to_classic
Result conversion_result(bool to_extended, const Converted& c) {
    std::string json = "{";
    put_text(json, "to", to_extended ? "extended" : "classic");
    json += ',';
    put_text(json, "from", c.from_extended ? "extended" : "classic");
    json += ',';
    put_bool(json, "ok", c.conversion.ok);
    json += ',';
    put_number(json, "bytes", static_cast<long long>(c.bytes.size()));
    json += ",\n";
    put_coded(json, "reasons", c.conversion.reason_codes, c.conversion.reasons);
    json += ",\n";
    put_coded(json, "notes", c.conversion.note_codes, c.conversion.notes);
    json += '}';
    return framed(json, reinterpret_cast<const char*>(c.bytes.data()), c.bytes.size());
}

Result convert_to_extended_impl(const uint8_t* data, size_t size) {
    Converted c;
    std::string why;
    if (!convert_file(true, data, size, LevelLimits::for_extended_maps(), c, why)) return refused(why);
    return conversion_result(true, c);
}

Result convert_to_classic_impl(const uint8_t* data, size_t size, Profile profile) {
    LevelLimits place;
    if (!limits_of(profile, place)) return refused("unknown profile");
    Converted c;
    std::string why;
    if (!convert_file(false, data, size, place, c, why)) return refused(why);
    return conversion_result(false, c);
}

Result needs_extended_impl(const uint8_t* data, size_t size, Profile profile) {
    LevelLimits place;
    if (!limits_of(profile, place)) return refused("unknown profile");
    Converted c;
    std::string why;
    if (!convert_file(false, data, size, place, c, why)) return refused(why);
    std::string out = "{";
    put_bool(out, "needs_extended", !c.conversion.ok);
    out += ",\n";
    put_coded(out, "reasons", c.conversion.reason_codes, c.conversion.reasons);
    out += '}';
    return Result{0, out};
}

// ---- the properties of a map ----

Result fault(const char* sentence) noexcept;      // (below: a fault as a Result)

// One piece of the file that changes: `at` and `removed` are of the old file, `inserted` is what takes its place
struct PropertyEdit {
    size_t at{0};
    size_t removed{0};
    std::string inserted;
};

void put_word(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}

// The properties of a map set in its own bytes: the match length, the description, the default ant type and the eggs of every team. Nothing else of the file is touched (a file is not
// read and written again: its quirks, the bytes after the egg word, the dictionary, the layers stay as they are), so setting the values that a file has returns the very same bytes.
// A refusal (a text that does not fit, a number beyond a word) is status 0 with "ok": false and its reasons; a file that does not load is status 1.
Result set_properties_impl(const uint8_t* data, size_t size, const uint8_t* description, size_t description_size, uint32_t minutes, uint32_t eggs, uint32_t default_ant_type, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    if (description == nullptr && description_size != 0) return refused("the description has no bytes to read");
    LevelData level;
    HeaderLayout layout;
    LevelValidation report;
    if (!LVLParser::load_from_memory(data, size, level, &report, limits, &layout)) return refused("does not load: " + report.reason());
    const bool extended = level.version == LVL_EXTENDED_VERSION;
    const MapKind kind = extended ? MapKind::Extended : MapKind::Classic;
    const size_t fits = extended ? limits.description_bytes() : kClassicDescription;                  // the bytes that fit with no warning (the 30th of a Classic map is the zero that ends the text)
    const size_t most = extended ? fits : kClassicField;                                                // the bytes the file can hold at all
    // a text beyond what the file can hold is refused before it is copied (its size is all the sentence needs)
    const std::string text = description_size == 0 || description_size > most || description == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(description), description_size);

    std::vector<std::pair<std::string, std::string>> reasons;
    std::vector<std::pair<std::string, std::string>> warnings;
    if (description_size > most) reasons.emplace_back("description_too_long", description_too_long_text(kind, description_size));
    else if (description_size > fits) warnings.emplace_back("description_too_long", description_too_long_text(kind, description_size));
    if (text.find('\0') != std::string::npos) reasons.emplace_back("description_has_zero_byte", "the description holds a zero byte (at " + std::to_string(text.find('\0')) + "): a reader would end it there");
    if (minutes > 65535) reasons.emplace_back("minutes_out_of_range", "the minutes are a number from 0 to 65535");
    if (eggs > 65535) reasons.emplace_back("eggs_out_of_range", "the eggs are a number from 0 to 65535");
    if (default_ant_type > 65535) reasons.emplace_back("default_ant_type_out_of_range", "the default ant type is a tile id from 0 to 65535");
    if (reasons.empty() && layout.eggs_at == HeaderLayout::kNone && eggs != level.boundary_param) {
        reasons.emplace_back("eggs_not_in_file", "the file ends before its egg word, so it has no place to keep the eggs");
    }

    std::vector<PropertyEdit> edits;                                                                      // in the order of the file
    if (reasons.empty()) {
        if (minutes != level.default_minutes) {
            PropertyEdit e{layout.minutes_at, 2, std::string()};
            put_word(e.inserted, minutes);
            edits.push_back(std::move(e));
        }
        if (text != level.description) {
            PropertyEdit e{layout.description_at, layout.description_field_bytes, std::string()};
            if (extended) put_word(e.inserted, static_cast<uint32_t>(text.size()));
            e.inserted += text;
            if (!extended) e.inserted.append(kClassicField - text.size(), '\0');
            edits.push_back(std::move(e));
        }
        if (default_ant_type != level.ambient_tile_or_sound) {
            PropertyEdit e{layout.default_type_at, 2, std::string()};
            put_word(e.inserted, default_ant_type);
            edits.push_back(std::move(e));
        }
        if (layout.eggs_at != HeaderLayout::kNone && eggs != level.boundary_param) {
            PropertyEdit e{layout.eggs_at, 2, std::string()};
            put_word(e.inserted, eggs);
            edits.push_back(std::move(e));
        }
    }

    std::string bytes;
    if (reasons.empty()) {
        size_t grown = size;
        for (const PropertyEdit& e : edits) grown = grown - e.removed + e.inserted.size();
        bytes.reserve(grown);
        size_t from = 0;
        for (const PropertyEdit& e : edits) {
            bytes.append(reinterpret_cast<const char*>(data) + from, e.at - from);
            bytes += e.inserted;
            from = e.at + e.removed;
        }
        bytes.append(reinterpret_cast<const char*>(data) + from, size - from);
        if (!edits.empty()) {
            // the file as it is now says what was asked, and nothing else (a fault of the core otherwise, never a file with other values)
            LevelData again;
            const uint8_t* made = reinterpret_cast<const uint8_t*>(bytes.data());
            if (!again.load_from_memory(made, bytes.size(), nullptr, limits) || again.description != text || again.default_minutes != minutes || again.boundary_param != (layout.eggs_at == HeaderLayout::kNone ? level.boundary_param : eggs) ||
                again.ambient_tile_or_sound != default_ant_type) {
                return fault("internal error: the file with the new properties does not read back");
            }
        }
    }

    std::string json = "{";
    put_bool(json, "ok", reasons.empty());
    json += ',';
    put_bool(json, "changed", !edits.empty());
    json += ',';
    put_number(json, "bytes", reasons.empty() ? static_cast<long long>(bytes.size()) : 0);
    json += ',';
    put_number(json, "description_bytes", static_cast<long long>(description_size));
    json += ',';
    put_number(json, "description_limit", static_cast<long long>(fits));
    json += ",\n";
    put_key(json, "edits");
    json += '[';
    for (size_t i = 0; i < edits.size(); ++i) {
        json += i == 0 ? "\n{" : ",\n{";
        put_number(json, "at", static_cast<long long>(edits[i].at));
        json += ',';
        put_number(json, "removed", static_cast<long long>(edits[i].removed));
        json += ',';
        put_number(json, "inserted", static_cast<long long>(edits[i].inserted.size()));
        json += '}';
    }
    json += edits.empty() ? "]" : "\n]";
    json += ",\n";
    std::vector<std::string> reason_codes, reason_texts, warning_codes, warning_texts;
    for (const auto& r : reasons) { reason_codes.push_back(r.first); reason_texts.push_back(r.second); }
    for (const auto& w : warnings) { warning_codes.push_back(w.first); warning_texts.push_back(w.second); }
    put_coded(json, "reasons", reason_codes, reason_texts);
    json += ",\n";
    put_coded(json, "warnings", warning_codes, warning_texts);
    json += '}';
    return framed(json, bytes.data(), bytes.size());
}

// ---- the crop of a map ----

// A key of one anchor of an object: its row and column (which may lie outside the grid) in one number
uint64_t anchor_key(int32_t row, int32_t column) { return (static_cast<uint64_t>(static_cast<uint32_t>(row)) << 32) | static_cast<uint32_t>(column); }

// The part of a map inside a window of `rows` x `columns` cells whose top left cell is (`row`, `column`), as a new map of the same kind (the bytes of the file in, the file of the part out).
// The arguments are checked against the grid before anything is made: a window that is empty or not inside the map is a refusal (status 1). The cells of both layers are copied; an object
// is kept whole or not at all (an object that has a cell outside the window, or an anchor outside it, is taken out of the part with every cell it has there, as the editor's eraser does,
// and its solid bits go with it, except in the first row: the original never clears those), starting ants, food piles and waypoints are kept when they are in the window (and their
// object was kept) and move with it; everything else of the file (the dictionary, the description, the minutes, the eggs, the version and the rules word) stays. A window that is the whole map gives the canonical
// bytes of the file as they are. The file is read and the part written under the limits of `profile`.
Result crop_impl(const uint8_t* data, size_t size, uint32_t row, uint32_t column, uint32_t rows, uint32_t columns, Profile profile) {
    LevelLimits limits;
    if (!limits_of(profile, limits)) return refused("unknown profile");
    LevelData level;
    std::string why;
    if (!load(data, size, limits, level, why)) return refused(why);
    const uint64_t source_rows = level.height;
    const uint64_t source_columns = level.width;
    if (rows == 0 || columns == 0) return refused("the part to keep needs at least one row and one column");
    if (static_cast<uint64_t>(row) + rows > source_rows || static_cast<uint64_t>(column) + columns > source_columns) {
        return refused("the part to keep (rows " + std::to_string(row) + " to " + std::to_string(static_cast<uint64_t>(row) + rows - 1) + ", columns " + std::to_string(column) + " to " +
                       std::to_string(static_cast<uint64_t>(column) + columns - 1) + ") is not inside the map, which has " + std::to_string(source_rows) + " rows and " + std::to_string(source_columns) + " columns");
    }
    const bool whole = row == 0 && column == 0 && rows == source_rows && columns == source_columns;
    const bool extended = level.version == LVL_EXTENDED_VERSION;
    uint64_t removed_cells = 0;
    uint64_t removed_records = 0;
    LevelData part;
    if (whole) {
        part = level;
    } else {
        const uint32_t width = level.width;
        auto inside = [&](int64_t r, int64_t c) { return r >= row && r < static_cast<int64_t>(row) + rows && c >= column && c < static_cast<int64_t>(column) + columns; };
        // the objects that have a cell outside the window and an anchor inside it are cut: their other cells must go too
        std::unordered_set<uint64_t> cut;
        for (uint32_t r = 0; r < level.height; ++r) {
            for (uint32_t c = 0; c < width; ++c) {
                if (inside(r, c)) continue;
                int32_t ar = 0, ac = 0;
                if (level.object_anchor(r, c, ar, ac) && inside(ar, ac)) cut.insert(anchor_key(ar, ac));
            }
        }
        part.version = level.version; part.format_flags = level.format_flags; part.rules = level.rules; part.game_mode = level.game_mode; part.default_minutes = level.default_minutes;
        part.description = level.description; part.tile_type_count = level.tile_type_count; part.tile_dictionary = level.tile_dictionary;
        part.ambient_flag = level.ambient_flag; part.ambient_tile_or_sound = level.ambient_tile_or_sound; part.boundary_param = level.boundary_param;
        part.width.val = columns;
        part.height.val = rows;
        const size_t cells = static_cast<size_t>(rows) * columns;
        part.layer1_terrain.assign(cells, MapCell{});
        part.layer2_interactive.assign(cells, MapCell{LVL_EMPTY_TILE, 0, 0});          // (a cell that is not copied is empty)
        std::vector<uint8_t> gone(cells, 0);                                                    // the cells whose object was not kept
        for (uint32_t r = 0; r < rows; ++r) {
            for (uint32_t c = 0; c < columns; ++c) {
                const size_t from = static_cast<size_t>(row + r) * width + (column + c);
                const size_t to = static_cast<size_t>(r) * columns + c;
                part.layer1_terrain[to] = level.layer1_terrain[from];
                const MapCell& cell = level.layer2_interactive[from];
                int32_t ar = 0, ac = 0;
                if (!cell.is_empty() && level.object_anchor(row + r, column + c, ar, ac)) {
                    if (!inside(ar, ac) || cut.count(anchor_key(ar, ac)) != 0) {
                        gone[to] = 1;
                        ++removed_cells;
                        if (r != 0 && !(cell.tile_index >= 245 && cell.tile_index <= 248)) part.layer1_terrain[to].flags = static_cast<uint16_t>(part.layer1_terrain[to].flags & ~1u);   // (no hill's bit; row 0 keeps its bit)
                        continue;
                    }
                    MapCell kept = cell;
                    if (!extended) kept.properties = static_cast<uint16_t>(((static_cast<uint32_t>(ar) - row) & 0xFFu) | (((static_cast<uint32_t>(ac) - column) & 0xFFu) << 8));    // (a version 9 offset is relative: it stays)
                    part.layer2_interactive[to] = kept;
                } else {
                    part.layer2_interactive[to] = cell;
                }
            }
        }
        auto kept_at = [&](uint32_t y, uint32_t x) {
            if (!inside(y, x)) return false;
            return gone[static_cast<size_t>(y - row) * columns + (x - column)] == 0;
        };
        for (const AnthillSpawn& s : level.anthill_spawns) {
            if (!kept_at(s.y, s.x)) { ++removed_records; continue; }
            AnthillSpawn moved = s;
            moved.y = static_cast<uint16_t>(s.y - row);
            moved.x = static_cast<uint16_t>(s.x - column);
            part.anthill_spawns.push_back(moved);
        }
        for (const FoodSchedule& f : level.food_schedules) {
            if (!kept_at(f.y, f.x)) { ++removed_records; continue; }
            FoodSchedule moved = f;
            moved.y = static_cast<uint16_t>(f.y - row);
            moved.x = static_cast<uint16_t>(f.x - column);
            part.food_schedules.push_back(std::move(moved));
        }
        for (const Waypoint& w : level.waypoints) {
            if (!kept_at(w.y, w.x)) { ++removed_records; continue; }
            Waypoint moved = w;
            moved.y = static_cast<uint16_t>(w.y - row);
            moved.x = static_cast<uint16_t>(w.x - column);
            part.waypoints.push_back(moved);
        }
    }
    level = LevelData{};                                                                        // (the source is not needed any more: less memory at the peak)
    std::vector<std::string> reason_codes, reason_texts, note_codes, note_texts;
    std::vector<uint8_t> bytes;
    LevelWriteResult w = write_level(part, limits);
    if (w.ok) bytes = std::move(w.bytes);
    else {
        reason_codes.push_back("not_writable");
        reason_texts.push_back("the part cannot be written as a map: " + w.error);
    }
    if (reason_codes.empty() && removed_cells != 0) {
        note_codes.push_back("objects_cut");
        note_texts.push_back(std::to_string(removed_cells) + (removed_cells == 1 ? " cell of an object was" : " cells of objects were") + " taken out because the frame cuts the object (an object is kept whole or not at all)");
    }
    if (reason_codes.empty() && removed_records != 0) {
        note_codes.push_back("records_left_out");
        note_texts.push_back(std::to_string(removed_records) + (removed_records == 1 ? " starting ant, food pile or waypoint lies" : " starting ants, food piles and waypoints lie") + " outside the part and " + (removed_records == 1 ? "is" : "are") + " left out");
    }
    std::string json = "{";
    put_text(json, "from", extended ? "extended" : "classic");
    json += ',';
    put_bool(json, "ok", reason_codes.empty());
    json += ',';
    put_number(json, "row", row);
    json += ',';
    put_number(json, "column", column);
    json += ',';
    put_number(json, "rows", rows);
    json += ',';
    put_number(json, "columns", columns);
    json += ',';
    put_number(json, "bytes", static_cast<long long>(bytes.size()));
    json += ',';
    put_number(json, "removed_cells", static_cast<long long>(removed_cells));
    json += ',';
    put_number(json, "removed_records", static_cast<long long>(removed_records));
    json += ",\n";
    put_coded(json, "reasons", reason_codes, reason_texts);
    json += ",\n";
    put_coded(json, "notes", note_codes, note_texts);
    json += '}';
    return framed(json, reinterpret_cast<const char*>(bytes.data()), bytes.size());
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
Result check_map(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return check_map_impl(data, size, profile); }); }
Result convert_to_extended(const uint8_t* data, size_t size) { return guarded([&] { return convert_to_extended_impl(data, size); }); }
Result convert_to_classic(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return convert_to_classic_impl(data, size, profile); }); }
Result needs_extended(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return needs_extended_impl(data, size, profile); }); }
Result rewrite(const uint8_t* data, size_t size, Profile profile) { return guarded([&] { return rewrite_impl(data, size, profile); }); }
Result blank(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, Profile profile) {
    return guarded([&] { return blank_impl(rows, columns, ground_tile, eggs, minutes, profile); });
}
Result blank_extended(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes) {
    return guarded([&] { return blank_of_kind(true, rows, columns, ground_tile, eggs, minutes, Profile::Extended); });
}
Result profile_limits(Profile profile) { return guarded([&] { return profile_limits_impl(profile); }); }
Result set_properties(const uint8_t* data, size_t size, const uint8_t* description, size_t description_size, uint32_t minutes, uint32_t eggs, uint32_t default_ant_type, Profile profile) {
    return guarded([&] { return set_properties_impl(data, size, description, description_size, minutes, eggs, default_ant_type, profile); });
}
Result crop(const uint8_t* data, size_t size, uint32_t row, uint32_t column, uint32_t rows, uint32_t columns, Profile profile) {
    return guarded([&] { return crop_impl(data, size, row, column, rows, columns, profile); });
}
Result same_level(const uint8_t* a, size_t size_a, const uint8_t* b, size_t size_b, Profile profile) {
    return guarded([&] { return same_level_impl(a, size_a, b, size_b, profile); });
}

std::vector<std::string> tile_names() { return std::vector<std::string>(std::begin(kTileNames), std::end(kTileNames)); }

}  // namespace ants::assets::lvl_core

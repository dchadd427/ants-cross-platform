// Real C++17 20 Hz playable match bridge for the 3D browser client (tools/ant_models/play.html).
// Runs authentic SimulationEngine + BotController and streams state/commands over RFC 6455 binary
// WebSocket frames carrying compact UTF-8 JSON.

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_assets/chd_parser.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_ctl/json.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/ws.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/effect_specs.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

namespace {

namespace fs = std::filesystem;
using ants::ctl::JsonValue;

constexpr int32_t kGroundFirst = 431;
constexpr int32_t kGroundLast = 669;
constexpr int32_t kGroundFallbackIdx = 54;  // g01a (485 - 431)
constexpr uint32_t kTickMs = 50;            // 20 Hz simulation tick
constexpr uint32_t kXMarksMs = 450;
constexpr size_t kMaxInitBytes = 60000;     // safely below ants::net::kMaxMessageBytes (65536)
constexpr size_t kDefaultMaxListedCommunity = 120;

std::atomic<bool> g_running{true};

void handle_signal(int /*sig*/) { g_running.store(false); }

JsonValue jval(bool v) { return JsonValue::make_bool(v); }
JsonValue jval(const char* v) { return JsonValue::make_string(v); }
JsonValue jval(const std::string& v) { return JsonValue::make_string(v); }
JsonValue jval(std::string&& v) { return JsonValue::make_string(std::move(v)); }

template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
JsonValue jval(T v) {
    return JsonValue::make_int(static_cast<int64_t>(v));
}

template <typename... Args>
JsonValue jarr(Args&&... args) {
    std::vector<JsonValue> items;
    items.reserve(sizeof...(Args));
    (items.push_back(jval(std::forward<Args>(args))), ...);
    return JsonValue::make_array_from(std::move(items));
}

std::string to_lower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char ch : sv) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

std::string slugify_id(std::string_view stem) {
    std::string out;
    bool prev_us = false;
    for (char ch : stem) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u) != 0) {
            out.push_back(static_cast<char>(std::tolower(u)));
            prev_us = false;
        } else if (!out.empty() && !prev_us) {
            out.push_back('_');
            prev_us = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("map") : out;
}

std::string pretty_label(std::string_view stem) {
    std::string out;
    bool cap = true;
    for (char ch : stem) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (ch == '_' || ch == '-') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            cap = true;
        } else if (cap && std::isalpha(u) != 0) {
            out.push_back(static_cast<char>(std::toupper(u)));
            cap = false;
        } else {
            out.push_back(static_cast<char>(std::tolower(u)));
            if (ch == ' ') cap = true;
        }
    }
    return out.empty() ? std::string(stem) : out;
}

bool has_lvl_ext(const fs::path& p) { return to_lower(p.extension().string()) == ".lvl"; }

// Wire cls_idx order (frozen shared protocol with play-client):
//   0=worker, 1=combat, 2=bomber, 3=swimmer, 4=fire, 5=thief
// Note: ants::sim::AntType is {0=Worker, 1=Bomber, 2=Fire, 3=Thief, 4=Combat, 5=Swimmer}.
uint8_t ant_type_to_wire_cls(ants::sim::AntType t) noexcept {
    static constexpr uint8_t kMap[6] = {0, 2, 4, 5, 1, 3};
    const uint8_t idx = static_cast<uint8_t>(t);
    return idx < 6 ? kMap[idx] : uint8_t{0};
}

const char* powerup_name_of(uint8_t powerup_type) noexcept {
    static constexpr const char* kNames[6] = {"", "pu_bomb", "pu_mason", "pu_thief", "pu_comb", "pu_swim"};
    return powerup_type < 6 ? kNames[powerup_type] : "";
}

const char* hill_name_of_team(int team) noexcept {
    static constexpr const char* kHills[4] = {"GREENHILL", "REDHILL", "BLUEHILL", "BLACKHILL"};
    return (team >= 0 && team < 4) ? kHills[team] : "";
}

// Reads Table-4 animation names directly from ants.chd without decoding bitmaps (~2 ms).
class Table4Catalog {
public:
    bool load(const fs::path& chd_path, std::string& error) {
        std::ifstream f(chd_path, std::ios::binary | std::ios::ate);
        if (!f) { error = "cannot open " + chd_path.string(); return false; }
        const std::streamsize sz = f.tellg();
        if (sz <= 0) { error = "empty chd file: " + chd_path.string(); return false; }
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(sz));
        if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
            error = "cannot read " + chd_path.string();
            return false;
        }
        ants::assets::CHDHeader hdr{};
        if (!ants::assets::CHDParser::parse_header(bytes.data(), bytes.size(), hdr)) {
            error = "invalid CHD header in " + chd_path.string();
            return false;
        }
        std::vector<ants::assets::AnimationSequence> anims;
        if (!ants::assets::CHDParser::parse_table4_animations(bytes.data(), bytes.size(), hdr.table4_offset, anims)) {
            error = "invalid CHD Table 4 in " + chd_path.string();
            return false;
        }
        names_.clear();
        lower_to_id_.clear();
        names_.reserve(anims.size());
        for (size_t i = 0; i < anims.size(); ++i) {
            names_.push_back(anims[i].name);
            const std::string low = to_lower(anims[i].name);
            if (!low.empty() && lower_to_id_.find(low) == lower_to_id_.end()) {
                lower_to_id_[low] = static_cast<int32_t>(i);
            }
        }
        return !names_.empty();
    }

    const std::string& name_of(int32_t t4_id) const noexcept {
        static const std::string kEmpty;
        if (t4_id < 0 || static_cast<size_t>(t4_id) >= names_.size()) return kEmpty;
        return names_[static_cast<size_t>(t4_id)];
    }

    int32_t find_id(std::string_view name) const noexcept {
        const auto it = lower_to_id_.find(to_lower(name));
        return it == lower_to_id_.end() ? -1 : it->second;
    }

    int32_t resolve_tile(const ants::assets::LevelData& level, uint16_t tile_idx) const noexcept {
        if (tile_idx == ants::assets::LVL_EMPTY_TILE || tile_idx == 0xFFFFu) return -1;
        if (tile_idx < level.tile_dictionary.size()) {
            const std::string& dict_name = level.tile_dictionary[tile_idx];
            if (!dict_name.empty() && dict_name != ".") {
                const std::string low = to_lower(dict_name);
                if (tile_idx < names_.size() && to_lower(names_[tile_idx]) == low) return static_cast<int32_t>(tile_idx);
                const int32_t by_name = find_id(low);
                if (by_name >= 0) return by_name;
            }
        }
        return tile_idx < names_.size() ? static_cast<int32_t>(tile_idx) : -1;
    }

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, int32_t> lower_to_id_;
};

struct MapEntry {
    std::string id;
    std::string label;
    std::string file;
    std::string rel_path;
    fs::path abs_path;
    bool is_original{false};
    bool is_featured{false};
};

class MapCatalog {
public:
    void scan(const fs::path& repo_root) {
        repo_root_ = repo_root;
        entries_.clear();
        std::set<std::string> seen;

        struct CanonicalMap { const char* id; const char* label; const char* file; };
        static constexpr CanonicalMap kOriginals[] = {
            {"treasure", "Treasure", "TREASURE.LVL"}, {"gauntlet", "Gauntlet", "GAUNTLET.LVL"},
            {"medium",   "Medium",   "MEDIUM.LVL"},   {"small",    "Small",    "SMALL.LVL"},
            {"tiny",     "Tiny",     "TINY.LVL"},     {"islands",  "Islands",  "ISLANDS.LVL"},
        };
        static constexpr CanonicalMap kFeatured[] = {
            {"popcorn", "Popcorn", "POPcOrN.lvl"},
            {"bombz_away", "Bombz Away", "Bombz Away.lvl"},
            {"hellraiser", "Hellraiser", "HELLRAISER.lvl"},
        };

        auto add = [&](const fs::path& abs, const std::string& rel, const std::string& id,
                       const std::string& label, bool orig, bool feat) {
            std::error_code ec;
            if (!fs::is_regular_file(abs, ec) || !seen.insert(to_lower(rel)).second) return;
            entries_.push_back(MapEntry{id, label, abs.filename().string(), rel, abs, orig, feat});
        };

        const fs::path orig_dir = repo_root / "Original-Ants" / "Maps";
        for (const auto& m : kOriginals) {
            add(orig_dir / m.file, std::string("Original-Ants/Maps/") + m.file, m.id, m.label, true, true);
        }
        scan_dir(orig_dir, "Original-Ants/Maps", true, false, seen);

        const fs::path comm_dir = repo_root / "Community-Maps";
        for (const auto& m : kFeatured) {
            add(comm_dir / m.file, std::string("Community-Maps/") + m.file, m.id, m.label, false, true);
        }
        scan_dir(comm_dir, "Community-Maps", false, false, seen);
    }

    const std::vector<MapEntry>& entries() const noexcept { return entries_; }

    bool resolve(std::string_view query, MapEntry& out, bool allow_direct_path) const {
        if (query.empty()) return false;
        std::string q = to_lower(query);
        std::replace(q.begin(), q.end(), '\\', '/');
        for (const auto& e : entries_) {
            if (to_lower(e.rel_path) == q || to_lower(e.file) == q || to_lower(e.id) == q) {
                out = e;
                return true;
            }
        }
        std::string q_stem = (q.size() > 4 && q.substr(q.size() - 4) == ".lvl") ? q.substr(0, q.size() - 4) : q;
        for (const auto& e : entries_) {
            if (to_lower(fs::path(e.file).stem().string()) == q_stem) {
                out = e;
                return true;
            }
        }
        if (!allow_direct_path) return false;
        std::error_code ec;
        fs::path direct(query);
        if (!fs::is_regular_file(direct, ec)) direct = repo_root_ / fs::path(query);
        if (!fs::is_regular_file(direct, ec)) return false;
        out = MapEntry{};
        out.abs_path = fs::weakly_canonical(direct, ec);
        if (ec) out.abs_path = direct;
        out.file = direct.filename().string();
        const std::string stem = direct.stem().string();
        out.id = slugify_id(stem);
        out.label = pretty_label(stem);
        fs::path rel = fs::relative(out.abs_path, repo_root_, ec);
        out.rel_path = ec ? std::string(query) : rel.generic_string();
        return true;
    }

private:
    void scan_dir(const fs::path& dir, const std::string& rel_prefix, bool orig, bool feat,
                  std::set<std::string>& seen) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        std::vector<fs::path> found;
        for (const auto& it : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (it.is_regular_file(ec) && has_lvl_ext(it.path())) found.push_back(it.path());
        }
        std::sort(found.begin(), found.end(), [](const fs::path& a, const fs::path& b) {
            return to_lower(a.filename().string()) < to_lower(b.filename().string());
        });
        for (const auto& p : found) {
            const std::string fname = p.filename().string();
            const std::string rel = rel_prefix + "/" + fname;
            if (!seen.insert(to_lower(rel)).second) continue;
            const std::string stem = p.stem().string();
            entries_.push_back(MapEntry{slugify_id(stem), pretty_label(stem), fname, rel, p, orig, feat});
        }
    }

    fs::path repo_root_;
    std::vector<MapEntry> entries_;
};

class LocalSink final : public ants::sim::CommandSink {
public:
    explicit LocalSink(ants::sim::SimulationEngine& sim) : sim_(sim) {}
    ants::sim::CommandResult submit(const ants::sim::Command& command) override { return sim_.apply_command(command); }

private:
    ants::sim::SimulationEngine& sim_;
};

struct StaticDecorItem { std::string id; int32_t t4{0}; int32_t col{0}; int32_t row{0}; };
struct HillItem { int32_t team{0}; std::string id; int32_t col{0}; int32_t row{0}; };
struct TransientFx { std::string kind; int32_t px{0}; int32_t py{0}; uint32_t age_ms{0}; uint32_t duration_ms{0}; int32_t team{-1}; };
struct QueuedSfx { uint32_t sound_id{0}; int32_t wx{0}; int32_t wy{0}; };

JsonValue decor_to_json(const StaticDecorItem& d) {
    JsonValue obj = JsonValue::make_object();
    obj.set("id", jval(d.id));
    obj.set("t4", jval(d.t4));
    obj.set("col", jval(d.col));
    obj.set("row", jval(d.row));
    return obj;
}

class BridgeMatch {
public:
    BridgeMatch(const Table4Catalog& t4, const MapCatalog& maps) : t4_(t4), maps_(maps) {}

    void set_starting_score(int32_t score) noexcept { starting_score_ = score; }
    bool paused() const noexcept { return paused_; }
    uint8_t seat() const noexcept { return seat_; }
    const std::string& bots_level() const noexcept { return bots_level_; }
    bool fog() const noexcept { return fog_; }
    uint32_t seed() const noexcept { return seed_; }
    const MapEntry& current_map() const noexcept { return current_map_; }

    bool load_match(std::string_view map_query, std::string_view bots_str, bool fog_req,
                    int seat_req, uint32_t seed_val, bool allow_direct_path, std::string& error) {
        MapEntry entry;
        if (!maps_.resolve(map_query, entry, allow_direct_path)) {
            error = "unknown map '" + std::string(map_query) + "'";
            return false;
        }
        ants::assets::LevelData loaded;
        ants::assets::LevelValidation val;
        if (!ants::assets::LVLParser::load_from_file(entry.abs_path.string(), loaded, &val,
                                                     ants::assets::LevelLimits::for_play_extended())) {
            error = "failed to load map '" + entry.rel_path + "': " + val.reason();
            return false;
        }

        uint8_t present = 0;
        for (uint32_t y = 0; y < loaded.height; ++y) {
            for (uint32_t x = 0; x < loaded.width; ++x) {
                const int team = ants::assets::hill_team_of_id(loaded.get_cell_layer2(x, y).tile_index);
                if (team >= 0 && team < 4) present = static_cast<uint8_t>(present | (1u << team));
            }
        }
        for (const auto& sp : loaded.anthill_spawns) {
            if (sp.team_id < 4) present = static_cast<uint8_t>(present | (1u << sp.team_id));
        }
        if (present == 0) present = 0x0Fu;

        uint8_t chosen_seat = (seat_req >= 0 && seat_req < 4) ? static_cast<uint8_t>(seat_req) : uint8_t{0};
        if (((present >> chosen_seat) & 1u) == 0) {
            for (uint8_t t = 0; t < 4; ++t) {
                if (((present >> t) & 1u) != 0) { chosen_seat = t; break; }
            }
        }

        std::string bots_norm = to_lower(bots_str);
        ants::ai::Level bot_level = ants::ai::Level::Medium;
        bool bots_enabled = false;
        if (bots_norm.empty() || bots_norm == "off" || bots_norm == "none") {
            bots_norm = "off";
        } else if (ants::ai::parse_level(bots_norm, bot_level)) {
            bots_norm = ants::ai::level_name(bot_level);
            bots_enabled = true;
        } else {
            bots_norm = "medium";
            bots_enabled = true;
        }

        if (fog_req && bots_enabled) {
            std::cerr << "[ants_3d_bridge] fog=1 requested with bots=" << bots_norm
                      << "; disabling bots while Fog of War is enabled\n";
            bots_norm = "off";
            bots_enabled = false;
        }

        level_ = std::move(loaded);
        current_map_ = std::move(entry);
        seat_ = chosen_seat;
        roster_mask_ = present;
        fog_ = fog_req;
        seed_ = seed_val;
        bots_level_ = bots_norm;
        paused_ = false;
        extra_sfx_.clear();
        transient_fx_.clear();

        sim_.set_game_mode(ants::sim::GameMode::HighestScore);
        sim_.set_fog_of_war_enabled(fog_);
        sim_.set_viewing_player_id(seat_);
        sim_.init(level_, seed_, roster_mask_);
        if (starting_score_ > 0 && seat_ < ants::sim::MAX_PLAYERS) sim_.set_player_score(seat_, starting_score_);

        bots_.reset();
        bot_sink_.reset();
        if (bots_enabled) {
            bot_sink_ = std::make_unique<LocalSink>(sim_);
            bots_ = std::make_unique<ants::ai::BotController>(sim_, seed_);
            for (uint8_t s = 0; s < ants::sim::MAX_PLAYERS; ++s) {
                if (s == seat_ || ((roster_mask_ >> s) & 1u) == 0) continue;
                ants::ai::BotSpec spec{s, "standard", bot_level, ants::ai::Style::Random};
                sim_.set_player_name(s, ants::ai::bot_display_name(spec));
                std::string bot_err;
                if (!bots_->add(spec, *bot_sink_, bot_err)) {
                    std::cerr << "[ants_3d_bridge] bot seat " << static_cast<unsigned>(s) << " skipped: " << bot_err << "\n";
                }
            }
        }

        extract_static_map_geometry();
        return true;
    }

    void step_tick() {
        if (paused_) return;
        sim_.tick();
        if (bots_) bots_->on_tick(sim_);
        for (auto it = transient_fx_.begin(); it != transient_fx_.end();) {
            it->age_ms += kTickMs;
            it = (it->age_ms >= it->duration_ms) ? transient_fx_.erase(it) : std::next(it);
        }
    }

    // Returns true when clients should receive a fresh "init" payload.
    bool handle_client_json(const JsonValue& msg) {
        if (!msg.is_object()) return false;
        const std::string type = msg.get("type").as_string_or();
        if (type == "move" || type == "attack" || type == "special" || type == "stop") {
            handle_unit_command(type, msg);
            return false;
        }
        if (type == "hatch") {
            ants::sim::Command cmd{};
            cmd.type = ants::sim::CommandType::Hatch;
            cmd.issuer = seat_;
            sim_.apply_command(cmd);
            return false;
        }
        if (type == "pause") {
            paused_ = msg.get("paused").as_bool_or(!paused_);
            return false;
        }
        if (type == "new_match") {
            const std::string map_q = msg.has("map") ? msg.get("map").as_string_or(current_map_.file) : current_map_.file;
            const std::string bots_q = msg.has("bots") ? msg.get("bots").as_string_or(bots_level_) : bots_level_;
            const bool fog_q = msg.has("fog") ? msg.get("fog").as_bool_or(fog_) : fog_;
            const int seat_q = msg.has("seat") ? static_cast<int>(msg.get("seat").as_int_or(seat_)) : static_cast<int>(seat_);
            const uint32_t seed_q = msg.has("seed")
                                        ? static_cast<uint32_t>(std::max<int64_t>(0, msg.get("seed").as_int_or(seed_)))
                                        : seed_;
            std::string err;
            if (!load_match(map_q, bots_q, fog_q, seat_q, seed_q, false, err)) {
                std::cerr << "[ants_3d_bridge] new_match failed: " << err << "\n";
                return false;
            }
            return true;
        }
        if (type == "ally_invite" || type == "ally_accept" || type == "ally_deny" || type == "ally_break") {
            ants::sim::Command cmd{};
            cmd.issuer = seat_;
            cmd.other_player = static_cast<uint8_t>(msg.get("other").as_int_or(255));
            cmd.type = (type == "ally_invite") ? ants::sim::CommandType::AllianceInvite
                     : (type == "ally_accept") ? ants::sim::CommandType::AllianceAccept
                     : (type == "ally_deny")   ? ants::sim::CommandType::AllianceDeny
                                               : ants::sim::CommandType::AllianceBreak;
            sim_.apply_command(cmd);
        }
        return false;
    }

    std::vector<uint8_t> build_init_payload() const {
        JsonValue tiles_arr = JsonValue::make_array();
        for (int32_t idx : ground_tiles_) tiles_arr.push_back(jval(idx));
        JsonValue decor_arr = JsonValue::make_array();
        for (const auto& d : static_decor_) decor_arr.push_back(decor_to_json(d));
        JsonValue plants_arr = JsonValue::make_array();
        for (const auto& p : plants_) plants_arr.push_back(decor_to_json(p));
        JsonValue hills_arr = JsonValue::make_array();
        for (const auto& h : hills_) {
            JsonValue item = JsonValue::make_object();
            item.set("team", jval(h.team)); item.set("id", jval(h.id));
            item.set("col", jval(h.col));   item.set("row", jval(h.row));
            hills_arr.push_back(std::move(item));
        }

        JsonValue map_obj = JsonValue::make_object();
        map_obj.set("id", jval(current_map_.id));         map_obj.set("label", jval(current_map_.label));
        map_obj.set("file", jval(current_map_.file));     map_obj.set("w", jval(level_.width.val));
        map_obj.set("h", jval(level_.height.val));        map_obj.set("tiles", std::move(tiles_arr));
        map_obj.set("static_decor", std::move(decor_arr)); map_obj.set("plants", std::move(plants_arr));
        map_obj.set("hills", std::move(hills_arr));       map_obj.set("seat", jval(seat_));
        map_obj.set("bots", jval(bots_level_));           map_obj.set("fog", jval(fog_));
        map_obj.set("paused", jval(paused_));

        const std::string map_text = ants::ctl::to_json(map_obj);
        const size_t budget_for_maps = map_text.size() + 256 < kMaxInitBytes ? (kMaxInitBytes - map_text.size() - 256) : 0u;
        JsonValue maps_arr = JsonValue::make_array();
        size_t maps_bytes = 2, comm_count = 0;
        for (const auto& m : maps_.entries()) {
            if (!m.is_original && !m.is_featured && comm_count >= kDefaultMaxListedCommunity) break;
            JsonValue item = JsonValue::make_object();
            item.set("id", jval(m.id));     item.set("label", jval(m.label));
            item.set("file", jval(m.file)); item.set("path", jval(m.rel_path));
            const size_t entry_bytes = ants::ctl::to_json(item).size() + 1;
            if (!m.is_original && maps_bytes + entry_bytes > budget_for_maps) break;
            maps_bytes += entry_bytes;
            if (!m.is_original && !m.is_featured) ++comm_count;
            maps_arr.push_back(std::move(item));
        }

        JsonValue root = JsonValue::make_object();
        root.set("type", jval("init"));
        root.set("maps", std::move(maps_arr));
        root.set("map", std::move(map_obj));
        const std::string text = ants::ctl::to_json(root);
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    std::vector<uint8_t> build_tick_payload() {
        const ants::sim::WorldState& w = sim_.get_world_state();
        const ants::sim::Grid& grid = sim_.grid();
        const bool fog_on = w.fog_of_war_enabled;
        const uint8_t ally = sim_.alliance_of(seat_);
        auto tile_visible = [&](int32_t col, int32_t row) -> bool { return !fog_on || w.is_tile_revealed(col, row); };

        const bool over = sim_.is_match_over();
        const int32_t winner = (over && !w.match_result.winning_players.empty())
                                   ? static_cast<int32_t>(w.match_result.winning_players[0])
                                   : -1;

        JsonValue root = JsonValue::make_object();
        root.set("type", jval("tick"));
        root.set("tick", jval(sim_.current_tick()));
        root.set("rem_ms", jval(w.match_time_remaining_ms));
        root.set("over", jval(over));
        root.set("winner", jval(winner));
        root.set("paused", jval(paused_));
        root.set("scores", jarr(w.player_scores[0], w.player_scores[1], w.player_scores[2], w.player_scores[3]));
        root.set("eggs", jarr(w.player_eggs[0], w.player_eggs[1], w.player_eggs[2], w.player_eggs[3]));
        root.set("hatching", jarr(sim_.get_pending_hatch_count(0), sim_.get_pending_hatch_count(1),
                                  sim_.get_pending_hatch_count(2), sim_.get_pending_hatch_count(3)));

        // Ants: [id, team, cls_idx, px, py, facing, hp, max_hp, state, action, loco_clip, loco_frame,
        //        holding, carried_pts, swimming, drowning, stunned, frozen, burn_ms]
        JsonValue ants_arr = JsonValue::make_array();
        for (const auto& a : w.ants) {
            const bool friendly = (a.player_id == seat_) || (ally < ants::sim::MAX_PLAYERS && a.player_id == ally);
            if (!friendly && !tile_visible(a.tile_x, a.tile_y)) continue;
            ants_arr.push_back(jarr(a.id, a.player_id, ant_type_to_wire_cls(a.type), a.px, a.py, a.facing,
                                    a.hp, a.max_hp, static_cast<uint8_t>(a.state), a.action, a.loco_clip,
                                    a.loco_frame, a.is_holding, a.carried_points, a.is_swimming, a.is_drowning,
                                    a.is_stunned, a.frozen, a.burn_elapsed_ms));
        }
        root.set("ants", std::move(ants_arr));

        JsonValue food_arr = JsonValue::make_array(), bombs_arr = JsonValue::make_array(),
                  fires_arr = JsonValue::make_array(), bridges_arr = JsonValue::make_array(),
                  powerups_arr = JsonValue::make_array(), lunchboxes_arr = JsonValue::make_array();
        std::set<uint64_t> emitted_food;
        for (const auto& fo : grid.food_objects()) {
            const uint16_t stage = fo.stage_tile();
            if (fo.remaining == 0 || stage == ants::sim::TILE_EMPTY || stage == 0xFFFFu || stage == ants::sim::TILE_LUNCHBOX) continue;
            if (!grid.in_bounds(fo.col, fo.row) || !emitted_food.insert((static_cast<uint64_t>(fo.row) << 32) | fo.col).second) continue;
            if (!tile_visible(static_cast<int32_t>(fo.col), static_cast<int32_t>(fo.row))) continue;
            const std::string& nm = t4_.name_of(t4_.resolve_tile(level_, stage));
            if (!nm.empty()) food_arr.push_back(jarr(fo.col, fo.row, nm, static_cast<int64_t>(fo.remaining) * fo.value));
        }

        const uint32_t gw = grid.width(), gh = grid.height();
        const auto& cells = grid.cells();
        for (uint32_t r = 0; r < gh; ++r) {
            const size_t row_off = static_cast<size_t>(r) * gw;
            for (uint32_t c = 0; c < gw; ++c) {
                const auto& cell = cells[row_off + c];
                if (cell.is_empty_overlay()) continue;
                const int32_t ic = static_cast<int32_t>(c), ir = static_cast<int32_t>(r);
                if (cell.has_any_bridge()) {
                    bridges_arr.push_back(jarr(ic, ir, cell.interactive_id - ants::sim::TILE_BRIDGE1 + 1));
                    continue;
                }
                if (!tile_visible(ic, ir)) continue;
                if (cell.has_bomb()) bombs_arr.push_back(jarr(ic, ir, (cell.interactive_id - ants::sim::BOMB_BLACK) % 4u));
                else if (cell.has_fire()) fires_arr.push_back(jarr(ic, ir));
                else if (cell.has_powerup()) {
                    const char* pname = powerup_name_of(cell.powerup_type);
                    if (pname[0] != '\0') powerups_arr.push_back(jarr(ic, ir, pname));
                } else if (cell.has_lunchbox()) {
                    lunchboxes_arr.push_back(jarr(ic, ir));
                } else if (cell.has_food() && cell.anchor_x == ic && cell.anchor_y == ir &&
                           emitted_food.insert((static_cast<uint64_t>(r) << 32) | c).second) {
                    const std::string& nm = t4_.name_of(t4_.resolve_tile(level_, cell.interactive_id));
                    if (!nm.empty()) food_arr.push_back(jarr(ic, ir, nm, 25));
                }
            }
        }
        root.set("food", std::move(food_arr));             root.set("bombs", std::move(bombs_arr));
        root.set("fires", std::move(fires_arr));           root.set("bridges", std::move(bridges_arr));
        root.set("powerups", std::move(powerups_arr));     root.set("lunchboxes", std::move(lunchboxes_arr));

        JsonValue droppers_arr = JsonValue::make_array();
        for (const auto& fd : w.flower_droppers) {
            if (!tile_visible(fd.x, fd.y) && !tile_visible(fd.drop_x, fd.drop_y)) continue;
            const int32_t st = fd.is_dropping ? static_cast<int32_t>(fd.powerup_type + 1) : 0;
            const uint8_t fr = fd.is_dropping ? ants::sim::effect_spec::dropper_frame_at(fd.drop_elapsed_ms) : uint8_t{0};
            droppers_arr.push_back(jarr(fd.x, fd.y, st, fr));
        }
        root.set("droppers", std::move(droppers_arr));

        JsonValue fx_arr = JsonValue::make_array();
        for (const auto& e : w.effects) {
            const int32_t tc = e.px / 32, tr = e.py / 32;
            if (e.fog_gated && !tile_visible(tc, tr)) continue;
            std::string kind = to_lower(e.anim_name);
            int32_t team = -1, fx_px = e.px, fx_py = e.py;
            if (kind == "bombex" || kind == "sputter" || kind == "bsputter" || kind == "dsplash" || kind == "bump") {
                if ((fx_px % 32) == 0) fx_px += 16;
                if ((fx_py % 32) == 0) fx_py += 16;
            }
            if (kind == "bsputter") kind = "sputter";
            if (kind == "bombex") {
                for (const auto& a : w.ants) {
                    if (a.frozen && a.burn_elapsed_ms >= 0 && a.tile_x == tc && a.tile_y == tr) {
                        kind = "dud"; team = static_cast<int32_t>(a.player_id); break;
                    }
                }
            }
            fx_arr.push_back(jarr(kind, fx_px, fx_py, e.elapsed_ms, team));
        }
        for (const auto& a : w.ants) {
            if (a.state == ants::sim::UnitState::PoweringUp && tile_visible(a.tile_x, a.tile_y)) {
                fx_arr.push_back(jarr("getpow", a.px, a.py, static_cast<int64_t>(a.loco_frame) * 50, a.player_id));
            }
        }
        for (const auto& tfx : transient_fx_) fx_arr.push_back(jarr(tfx.kind, tfx.px, tfx.py, tfx.age_ms, tfx.team));
        root.set("fx", std::move(fx_arr));

        JsonValue bubbles_arr = JsonValue::make_array();
        for (const auto& b : w.score_bubbles) {
            int32_t team = -1;
            for (const auto& ah : w.anthills) {
                if (b.x == (static_cast<int32_t>(ah.x) + 1) * 32 && b.y == (static_cast<int32_t>(ah.y) + 1) * 32) {
                    team = static_cast<int32_t>(ah.team_id); break;
                }
            }
            bubbles_arr.push_back(jarr(b.x + 16, b.y + 16, b.amount, team, b.elapsed_ms));
        }
        root.set("bubbles", std::move(bubbles_arr));

        JsonValue sfx_arr = JsonValue::make_array();
        for (const auto& ev : sim_.poll_audio_events()) {
            if (!ev.stop && (ev.target_player == 255 || ev.target_player == seat_)) {
                sfx_arr.push_back(jarr(ev.sound_id, ev.world_x, ev.world_y));
            }
        }
        for (const auto& ev : extra_sfx_) sfx_arr.push_back(jarr(ev.sound_id, ev.wx, ev.wy));
        extra_sfx_.clear();
        root.set("sfx", std::move(sfx_arr));

        JsonValue news_arr = JsonValue::make_array();
        for (const auto& ev : sim_.poll_news_events()) {
            if ((ev.target_player == 255 || ev.target_player == seat_) && !ev.message_text.empty()) {
                news_arr.push_back(jval(ev.message_text));
            }
        }
        root.set("news", std::move(news_arr));
        root.set("fog_hex", jval(encode_fog_hex(w)));

        const std::string text = ants::ctl::to_json(root);
        return std::vector<uint8_t>(text.begin(), text.end());
    }

private:
    void extract_static_map_geometry() {
        const uint32_t w = level_.width;
        const uint32_t h = level_.height;

        ground_tiles_.clear();
        ground_tiles_.reserve(static_cast<size_t>(w) * h);
        for (uint32_t r = 0; r < h; ++r) {
            for (uint32_t c = 0; c < w; ++c) {
                const int32_t t4_id = t4_.resolve_tile(level_, level_.get_cell_layer1(c, r).tile_index);
                ground_tiles_.push_back((t4_id >= kGroundFirst && t4_id <= kGroundLast) ? (t4_id - kGroundFirst)
                                                                                        : kGroundFallbackIdx);
            }
        }

        std::array<ants::sim::TileCoord, 4> hill_min{{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}};
        std::array<ants::sim::TileCoord, 4> hill_anchor{{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}};
        for (uint32_t r = 0; r < h; ++r) {
            for (uint32_t c = 0; c < w; ++c) {
                const auto& c2 = level_.get_cell_layer2(c, r);
                const int team = ants::assets::hill_team_of_id(c2.tile_index);
                if (team < 0 || team >= 4) continue;
                const size_t st = static_cast<size_t>(team);
                const int32_t ic = static_cast<int32_t>(c);
                const int32_t ir = static_cast<int32_t>(r);
                if (hill_min[st].x < 0 || ic < hill_min[st].x) hill_min[st].x = ic;
                if (hill_min[st].y < 0 || ir < hill_min[st].y) hill_min[st].y = ir;
                if ((c2.flags & 1u) != 0) hill_anchor[st] = {ic, ir};
            }
        }
        hills_.clear();
        for (int t = 0; t < 4; ++t) {
            if (((roster_mask_ >> t) & 1u) == 0) continue;
            const size_t st = static_cast<size_t>(t);
            if (hill_anchor[st].x < 0 && hill_min[st].x >= 0) hill_anchor[st] = {hill_min[st].x + 1, hill_min[st].y + 1};
            if (hill_anchor[st].x >= 0) {
                hills_.push_back(HillItem{t, hill_name_of_team(t), hill_anchor[st].x, hill_anchor[st].y});
            }
        }

        std::set<uint64_t> food_schedule_cells;
        for (const auto& fs : level_.food_schedules) {
            food_schedule_cells.insert((static_cast<uint64_t>(fs.y) << 32) | fs.x);
        }

        static_decor_.clear();
        for (uint32_t r = 0; r < h; ++r) {
            for (uint32_t c = 0; c < w; ++c) {
                const auto& c2 = level_.get_cell_layer2(c, r);
                const uint16_t tid = c2.tile_index;
                if (c2.is_empty() || tid == 0xFFFFu || tid == 0x7FFEu || (c2.flags & 1u) == 0) continue;
                if (ants::sim::movement::is_powerup_tile(tid) || ants::sim::is_original_bomb_tile(tid)) continue;
                if (tid == ants::sim::TILE_FIREWALL || tid == ants::sim::TILE_LUNCHBOX) continue;
                if (ants::sim::movement::is_bridge_tile(tid)) continue;
                if (ants::assets::hill_team_of_id(tid) >= 0 || ants::assets::start_marker_team_of_id(tid) >= 0) continue;
                if ((ants::sim::movement::tile_flags_of(tid) & ants::sim::movement::kTileFlagFood) != 0) continue;
                if (food_schedule_cells.count((static_cast<uint64_t>(r) << 32) | c) != 0) continue;
                if (tid >= level_.tile_dictionary.size()) continue;
                const std::string& dname = level_.tile_dictionary[tid];
                if (dname.empty() || dname == "." || to_lower(dname).rfind("pu_", 0) == 0) continue;
                const int32_t t4_id = t4_.resolve_tile(level_, tid);
                const std::string& chd_name = t4_.name_of(t4_id);
                if (t4_id >= 0 && !chd_name.empty()) {
                    static_decor_.push_back(StaticDecorItem{chd_name, t4_id, static_cast<int32_t>(c), static_cast<int32_t>(r)});
                }
            }
        }

        plants_.clear();
        for (const auto& sp : level_.anthill_spawns) {
            if (sp.team_id != 255 || !ants::sim::movement::is_plant_object_tile(sp.tile_id)) continue;
            const int32_t t4_id = static_cast<int32_t>(sp.tile_id);
            const std::string& chd_name = t4_.name_of(t4_id);
            if (!chd_name.empty()) {
                plants_.push_back(StaticDecorItem{chd_name, t4_id, static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)});
            }
        }
    }

    void handle_unit_command(const std::string& type, const JsonValue& msg) {
        const JsonValue& ants_v = msg.get("ants");
        if (!ants_v.is_array() || ants_v.size() == 0) return;

        std::vector<uint32_t> all_ids;
        all_ids.reserve(ants_v.size());
        for (const auto& el : ants_v.items()) {
            const int64_t v = el.as_int_or(-1);
            if (v > 0) all_ids.push_back(static_cast<uint32_t>(v));
        }
        if (all_ids.empty()) return;

        int32_t tx = static_cast<int32_t>(msg.get("tx").as_int_or(-1));
        int32_t ty = static_cast<int32_t>(msg.get("ty").as_int_or(-1));
        ants::sim::CommandType ctype = ants::sim::CommandType::GroupMove;
        if (type == "special") {
            ctype = ants::sim::CommandType::GroupSpecial;
        } else if (type == "stop") {
            ctype = ants::sim::CommandType::Stop;
            tx = 0;
            ty = 0;
        } else if (type == "attack") {
            ctype = ants::sim::CommandType::GroupAttack;
            const int64_t target_id = msg.get("target_id").as_int_or(-1);
            if (target_id > 0) {
                for (const auto& a : sim_.get_world_state().ants) {
                    if (a.id == static_cast<uint32_t>(target_id)) {
                        tx = a.tile_x;
                        ty = a.tile_y;
                        break;
                    }
                }
            }
        }
        if (ctype != ants::sim::CommandType::Stop && !sim_.grid().in_bounds(tx, ty)) return;

        uint32_t ack_ant = 0;
        for (size_t off = 0; off < all_ids.size(); off += ants::sim::kMaxCommandAnts) {
            const size_t chunk = std::min(ants::sim::kMaxCommandAnts, all_ids.size() - off);
            ants::sim::Command cmd{};
            cmd.type = ctype;
            cmd.issuer = seat_;
            cmd.tile_x = static_cast<int16_t>(tx);
            cmd.tile_y = static_cast<int16_t>(ty);
            cmd.ants.assign(all_ids.begin() + static_cast<std::ptrdiff_t>(off),
                            all_ids.begin() + static_cast<std::ptrdiff_t>(off + chunk));
            const ants::sim::CommandResult res = sim_.apply_command(cmd);
            if (ack_ant == 0 && res.ack_ant != 0) ack_ant = res.ack_ant;
        }

        if (ctype != ants::sim::CommandType::Stop && ack_ant != 0) {
            for (const auto& a : sim_.get_world_state().ants) {
                if (a.id != ack_ant) continue;
                const uint32_t snd = (ctype == ants::sim::CommandType::GroupMove)
                                         ? ants::sim::get_move_voice_sound(a.type, voice_counter_++)
                                     : (ctype == ants::sim::CommandType::GroupAttack)
                                         ? ants::sim::get_attack_voice_sound(a.type, voice_counter_++)
                                         : ants::sim::get_ability_voice_sound(a.type);
                if (snd != ants::sim::NoVoice) extra_sfx_.push_back(QueuedSfx{snd, a.px, a.py});
                break;
            }
            transient_fx_.push_back(TransientFx{"xmarks", tx * 32 + 16, ty * 32 + 16, 0, kXMarksMs, static_cast<int32_t>(seat_)});
        }
    }

    static std::string encode_fog_hex(const ants::sim::WorldState& w) {
        if (!w.fog_of_war_enabled || w.fog_revealed.empty()) return {};
        static constexpr char kHex[] = "0123456789abcdef";
        const size_t n = w.fog_revealed.size();
        const size_t byte_count = (n + 7u) / 8u;
        std::string out;
        out.reserve(byte_count * 2u);
        for (size_t b = 0; b < byte_count; ++b) {
            uint8_t val = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                const size_t idx = b * 8u + bit;
                if (idx < n && w.fog_revealed[idx] != 0) val = static_cast<uint8_t>(val | (1u << bit));
            }
            out.push_back(kHex[(val >> 4) & 0x0Fu]);
            out.push_back(kHex[val & 0x0Fu]);
        }
        return out;
    }

    const Table4Catalog& t4_;
    const MapCatalog& maps_;
    ants::assets::LevelData level_;
    MapEntry current_map_;
    ants::sim::SimulationEngine sim_;
    std::unique_ptr<LocalSink> bot_sink_;
    std::unique_ptr<ants::ai::BotController> bots_;

    uint8_t seat_{0};
    uint8_t roster_mask_{0x0Fu};
    std::string bots_level_{"medium"};
    bool fog_{false};
    uint32_t seed_{1};
    int32_t starting_score_{0};
    bool paused_{false};
    uint32_t voice_counter_{0};

    std::vector<int32_t> ground_tiles_;
    std::vector<StaticDecorItem> static_decor_;
    std::vector<StaticDecorItem> plants_;
    std::vector<HillItem> hills_;
    std::vector<TransientFx> transient_fx_;
    std::vector<QueuedSfx> extra_sfx_;
};

struct CliOptions {
    uint16_t ws_port{4081};
    std::string map_path{"Original-Ants/Maps/TREASURE.LVL"};
    std::string bots{"medium"};
    int seat{0};
    bool fog{false};
    uint32_t seed{1};
    int32_t starting_score{0};
    bool show_help{false};
};

void print_usage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "  --ws-port <port>        WebSocket listen port on 0.0.0.0 (default: 4081)\n"
              << "  --map <path>            Initial .LVL map path or name (default: Original-Ants/Maps/TREASURE.LVL)\n"
              << "  --bots <level>          AI difficulty for non-human teams: easy|medium|hard|off (default: medium)\n"
              << "  --seat <0..3>           Human team seat: 0=Green, 1=Red, 2=Blue, 3=Black (default: 0)\n"
              << "  --fog <0|1>             Enable Fog of War (default: 0)\n"
              << "  --seed <n>              Deterministic match PRNG seed (default: 1)\n"
              << "  --starting-score <n>    Optional starting score for the human seat (default: 0)\n"
              << "  --help                  Show this help message\n";
}

bool parse_cli(int argc, char* argv[], CliOptions& opts, std::string& error) {
    auto next_arg = [&](int& i, std::string& val) -> bool {
        if (i + 1 >= argc) { error = std::string("missing value for ") + argv[i]; return false; }
        val = argv[++i];
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string val;
        if (arg == "--help" || arg == "-h") { opts.show_help = true; return true; }
        if (arg == "--ws-port" || arg == "--port") {
            if (!next_arg(i, val)) return false;
            const long p = std::strtol(val.c_str(), nullptr, 10);
            if (p < 0 || p > 65535) { error = "invalid port: " + val; return false; }
            opts.ws_port = static_cast<uint16_t>(p);
        } else if (arg == "--map") {
            if (!next_arg(i, opts.map_path)) return false;
        } else if (arg == "--bots") {
            if (!next_arg(i, opts.bots)) return false;
        } else if (arg == "--seat") {
            if (!next_arg(i, val)) return false;
            const long s = std::strtol(val.c_str(), nullptr, 10);
            if (s < 0 || s > 3) { error = "invalid seat (expected 0..3): " + val; return false; }
            opts.seat = static_cast<int>(s);
        } else if (arg == "--fog") {
            if (!next_arg(i, val)) return false;
            opts.fog = (val == "1" || to_lower(val) == "true");
        } else if (arg == "--seed") {
            if (!next_arg(i, val)) return false;
            opts.seed = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (arg == "--starting-score") {
            if (!next_arg(i, val)) return false;
            opts.starting_score = static_cast<int32_t>(std::strtol(val.c_str(), nullptr, 10));
        } else { error = "unknown option: " + arg; return false; }
    }
    return true;
}

fs::path locate_repo_root() {
    std::error_code ec;
    if (fs::is_regular_file(fs::path("Original-Ants") / "ants.chd", ec)) return fs::current_path(ec);
    const fs::path src(ANTS_SOURCE_DIR);
    if (fs::is_regular_file(src / "Original-Ants" / "ants.chd", ec)) return src;
    return fs::current_path(ec);
}

void broadcast(std::vector<std::unique_ptr<ants::net::WsConnection>>& clients, const std::vector<uint8_t>& payload) {
    if (payload.empty()) return;
    if (payload.size() > ants::net::kMaxMessageBytes) {
        std::cerr << "[ants_3d_bridge] dropping oversized frame (" << payload.size() << " B)\n";
        return;
    }
    for (auto& c : clients) {
        if (c && c->state() == ants::net::Connection::State::Open) c->send(payload);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    CliOptions opts;
    std::string err;
    if (!parse_cli(argc, argv, opts, err)) {
        std::cerr << "[ants_3d_bridge] " << err << "\n";
        print_usage(argv[0]);
        return 2;
    }
    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    const fs::path repo_root = locate_repo_root();
    Table4Catalog t4;
    if (!t4.load(repo_root / "Original-Ants" / "ants.chd", err)) {
        std::cerr << "[ants_3d_bridge] " << err << "\n";
        return 1;
    }

    MapCatalog maps;
    maps.scan(repo_root);

    BridgeMatch match(t4, maps);
    match.set_starting_score(opts.starting_score);
    if (!match.load_match(opts.map_path, opts.bots, opts.fog, opts.seat, opts.seed, true, err)) {
        std::cerr << "[ants_3d_bridge] " << err << "\n";
        return 1;
    }

    ants::net::WsServerOptions ws_opts;
    ws_opts.allowed_origins.clear();
    ws_opts.path.clear();
    auto listener = ants::net::WsListener::listen(opts.ws_port, /*loopback_only=*/false, ws_opts);
    if (!listener) {
        std::cerr << "[ants_3d_bridge] failed to bind WebSocket listener on 0.0.0.0:" << opts.ws_port << "\n";
        return 1;
    }

    std::cout << "ants_3d_bridge listening on ws://0.0.0.0:" << listener->port()
              << " map=" << match.current_map().file
              << " seat=" << static_cast<unsigned>(match.seat())
              << " bots=" << match.bots_level()
              << " fog=" << (match.fog() ? 1 : 0)
              << " seed=" << match.seed() << std::endl;

    std::vector<std::unique_ptr<ants::net::WsConnection>> clients;
    using Clock = std::chrono::steady_clock;
    auto next_tick = Clock::now() + std::chrono::milliseconds(kTickMs);
    auto next_paused_heartbeat = Clock::now() + std::chrono::milliseconds(250);

    while (g_running.load()) {
        while (auto conn = listener->accept()) {
            const std::vector<uint8_t> init_msg = match.build_init_payload();
            const std::vector<uint8_t> tick_msg = match.build_tick_payload();
            if (init_msg.size() <= ants::net::kMaxMessageBytes) conn->send(init_msg);
            if (tick_msg.size() <= ants::net::kMaxMessageBytes) conn->send(tick_msg);
            clients.push_back(std::move(conn));
        }

        bool send_init = false;
        bool send_immediate_tick = false;
        std::vector<uint8_t> raw;
        for (auto& c : clients) {
            if (!c) continue;
            while (c->poll(raw)) {
                const std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
                JsonValue parsed;
                std::string jerr;
                if (ants::ctl::parse_json(sv, parsed, &jerr)) {
                    const bool prev_paused = match.paused();
                    if (match.handle_client_json(parsed)) send_init = true;
                    if (match.paused() != prev_paused) send_immediate_tick = true;
                } else {
                    std::cerr << "[ants_3d_bridge] ignored invalid JSON from " << c->peer() << ": " << jerr << "\n";
                }
            }
        }

        clients.erase(
            std::remove_if(clients.begin(), clients.end(),
                           [](const std::unique_ptr<ants::net::WsConnection>& c) {
                               return !c || c->state() != ants::net::Connection::State::Open;
                           }),
            clients.end());

        if (send_init) {
            broadcast(clients, match.build_init_payload());
            broadcast(clients, match.build_tick_payload());
            next_tick = Clock::now() + std::chrono::milliseconds(kTickMs);
        } else if (send_immediate_tick) {
            broadcast(clients, match.build_tick_payload());
        }

        const auto now = Clock::now();
        if (now >= next_tick) {
            if (now - next_tick > std::chrono::milliseconds(250)) next_tick = now;
            next_tick += std::chrono::milliseconds(kTickMs);
            if (!match.paused()) {
                match.step_tick();
                if (!clients.empty()) broadcast(clients, match.build_tick_payload());
            } else if (now >= next_paused_heartbeat) {
                next_paused_heartbeat = now + std::chrono::milliseconds(250);
                if (!clients.empty()) broadcast(clients, match.build_tick_payload());
            }
        }

        const auto remain = next_tick - Clock::now();
        const auto nap = std::min<Clock::duration>(remain, std::chrono::milliseconds(5));
        if (nap > Clock::duration::zero()) std::this_thread::sleep_for(nap);
    }

    for (auto& c : clients) {
        if (c) c->close();
    }
    return 0;
}

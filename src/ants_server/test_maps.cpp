#include "ants_server/test_maps.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>

#include "ants_assets/lvl_core.hpp"
#include "ants_net/netgame.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/history_store.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/online_limits.hpp"
#include "ants_server/secret.hpp"

namespace fs = std::filesystem;

namespace ants::server {

namespace {

using ctl::JsonValue;
namespace core = ants::assets::lvl_core;

constexpr size_t kMaxTestStemChars = 45;

void remove_quietly(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

// A file name that this store makes: "test-" and eight lower-case hex digits and ".lvl"
bool is_store_file_name(const std::string& name) {
    if (name.size() != 17 || name.compare(0, 5, "test-") != 0 || name.compare(13, 4, ".lvl") != 0) return false;
    return std::all_of(name.begin() + 5, name.begin() + 13, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

}  // namespace

TestMapStore::TestMapStore(TestMapConfig config) : config_(std::move(config)) {}

size_t TestMapStore::max_upload_bytes(const std::string& kind) { return kind == "extended" ? kMaxBodyBytes : kClassicBytes; }

bool TestMapStore::open(std::string& why) {
    open_ = false;
    kept_.clear();
    if (config_.dir.empty()) {
        why = "no folder for the test maps";
        return false;
    }
    std::error_code ec;
    fs::create_directories(config_.dir, ec);
    if (ec) {
        why = "the folder " + config_.dir + " cannot be made (" + ec.message() + ")";
        return false;
    }
    fs::permissions(config_.dir, fs::perms::owner_all, fs::perm_options::replace, ec);          // (0700: the test maps are for the server's user only; a folder that already exists is tightened too)
    ec.clear();
    for (fs::directory_iterator it(config_.dir, ec), end; !ec && it != end; it.increment(ec)) {          // (what an earlier run left: only files of this store's own names, never a folder, a link or any other file)
        const std::string name = it->path().filename().string();
        std::error_code status_ec;
        if (!(is_store_file_name(name) || name.compare(0, 5, ".tmp-") == 0)) continue;
        if (fs::symlink_status(it->path(), status_ec).type() != fs::file_type::regular) continue;
        remove_quietly(it->path().string());
    }
    open_ = true;
    return true;
}

void TestMapStore::prune(int64_t now) {
    for (auto it = kept_.begin(); it != kept_.end();) {
        if (now - it->created_at >= kTestMapTtlS) {
            remove_quietly(it->map.path);
            it = kept_.erase(it);
            changed_ = true;
        } else {
            ++it;
        }
    }
}

void TestMapStore::maintenance(int64_t now) { prune(now); }

bool TestMapStore::upkeep(int64_t now, uint32_t now_ms) {
    if (upkept_ && static_cast<uint32_t>(now_ms - last_upkeep_ms_) < kUpkeepEveryMs) return false;
    upkept_ = true;
    last_upkeep_ms_ = now_ms;
    prune(now);
    return take_changed();
}

std::vector<ApprovedMap> TestMapStore::maps(int64_t now) {
    prune(now);
    std::vector<ApprovedMap> out;
    out.reserve(kept_.size());
    for (const Kept& m : kept_) out.push_back(m.map);
    return out;
}

bool TestMapStore::take_changed() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

uint64_t TestMapStore::held_bytes() const {
    uint64_t total = 0;
    for (const Kept& m : kept_) total += m.map.size;
    return total;
}

// The oldest test map that may go: one that has been kept for kTestMinKeepS and that no room uses (of `only_owner` when that is not empty), and is not already in `taken`. kept_.size() when there is none.
size_t TestMapStore::droppable(int64_t now, const std::string& only_owner, const std::vector<size_t>& taken) const {
    for (size_t i = 0; i < kept_.size(); ++i) {
        if (std::find(taken.begin(), taken.end(), i) != taken.end()) continue;
        if (!only_owner.empty() && kept_[i].owner != only_owner) continue;
        if (now - kept_[i].created_at < kTestMinKeepS) continue;
        if (in_use_ && in_use_(kept_[i].map.name)) continue;
        return i;
    }
    return kept_.size();
}

TestMapOutcome TestMapStore::submit(const TestMapInput& in, int64_t now) {
    TestMapOutcome out;
    const auto refuse = [&](int status, const char* code, const char* message) {
        out.fail = make_failure(status, code, message);
        return out;
    };
    if (!open_) return refuse(503, "test_maps_off", "Test rooms are not switched on right now.");
    prune(now);                                                // (a map that is six hours old is gone before anything else is asked: the lobby is told even when this upload is refused)
    if (in.kind != "classic" && in.kind != "extended") return refuse(400, "bad_kind", "Say whether the map is a Classic map or an Extended map.");
    if (in.bytes.empty()) return refuse(400, "not_a_map", "No map file came with the request.");
    if (in.bytes.size() > max_upload_bytes(in.kind)) return refuse(413, "too_large", "This map file is bigger than a map of this kind may be.");
    std::error_code ec;
    const fs::space_info space = fs::space(fs::path(config_.dir), ec);
    if (ec || space.available < config_.min_free_bytes + in.bytes.size()) return refuse(503, "storage_full", "The server has no room for more maps right now. Please try again later.");

    std::string real_kind;
    if (!kind_of_file(in.kind, in.bytes, real_kind, out.fail)) return out;
    if (in.allow_check && !in.allow_check()) return refuse(503, "busy", "The server is busy checking other maps. Please try again in a minute.");

    // What is kept is the CANONICAL file, as for a real submission: the core's rewrite of the map, checked again, never the bytes that came (trailing bytes, unknown blocks, anything the rewrite drops, are gone)
    const auto* data = reinterpret_cast<const uint8_t*>(in.bytes.data());
    const auto size_fits = [&](int64_t rows, int64_t columns) {
        return real_kind == "extended" ? (rows >= 1 && columns >= 1 && rows <= 1024 && columns <= 1024 && rows * columns <= 262144) : (rows >= 1 && columns >= 1 && rows <= 90 && columns <= 256);
    };
    const core::Result rewritten = core::rewrite(data, in.bytes.size(), core::Profile::Extended);
    if (rewritten.status != 0 || rewritten.payload.empty()) {          // (the core writes no map that is bigger than its kind: say so, as the check does, when that is the reason)
        const core::Result first = core::read_report(data, in.bytes.size(), core::Profile::Extended);
        JsonValue first_meta;
        if (first.status == 0 && parse_big_json(first.payload, first_meta) && first_meta.get("loaded").as_bool_or(false) && !size_fits(first_meta.get("rows").as_int_or(0), first_meta.get("columns").as_int_or(0))) {
            return refuse(422, "map_too_big", "This map is bigger than a map of this kind may be.");
        }
        return refuse(400, "not_a_map", "This file could not be read as an Ants map.");
    }
    const std::string& canonical = rewritten.payload;
    const auto* kept = reinterpret_cast<const uint8_t*>(canonical.data());
    const core::Result checked = core::check_map(kept, canonical.size(), core::Profile::Extended);
    const core::Result info = core::read_report(kept, canonical.size(), core::Profile::Extended);
    JsonValue report;
    JsonValue meta;
    if (checked.status != 0 || info.status != 0 || !parse_big_json(checked.payload, report) || !parse_big_json(info.payload, meta) ||
        !report.get("judged").as_bool_or(false) || !meta.get("loaded").as_bool_or(false)) {
        return refuse(400, "not_a_map", "This file could not be read as an Ants map.");
    }
    const int64_t rows = meta.get("rows").as_int_or(0);
    const int64_t columns = meta.get("columns").as_int_or(0);
    if (!size_fits(rows, columns)) {
        out.fail = make_failure(422, "map_too_big", "This map is bigger than a map of this kind may be.");
        out.fail.extra = JsonValue::make_object();
        out.fail.extra.set("findings", kept_findings(report));
        return out;
    }
    if (!OnlineLimits::allows(static_cast<uint64_t>(rows * columns))) {          // (it is a good map: it can be played here against bots, not by a room)
        return refuse(422, "map_too_big_online", "This map is too big to play online yet (65,536 cells at most, like 256 x 256). You can still play it against bots on this computer.");
    }
    const JsonValue& findings = report.get("findings");
    bool team_bad[4] = {false, false, false, false};
    for (size_t i = 0; i < findings.size(); ++i) {
        const JsonValue& f = findings.at(i);
        const std::string& sev = f.get("severity").str();
        const int64_t team = f.get("team").as_int_or(-1);
        const std::string& code = f.get("code").str();
        if (sev == "fatal" && !(team >= 0 && team < 4)) {
            out.fail = make_failure(422, "map_fails_checks", "This map does not pass the checks, so it cannot be played.");
            out.fail.extra = JsonValue::make_object();
            out.fail.extra.set("findings", kept_findings(report));
            return out;
        }
        if (team >= 0 && team < 4 && (sev == "fatal" || code == "hill_missing" || code == "hill_without_entrance" || code == "hills_several" || code == "starting_ants_missing")) {
            team_bad[team] = true;
        }
    }
    int64_t players = 0;
    const JsonValue& teams = meta.get("teams");
    for (size_t s = 0; s < 4 && s < teams.size(); ++s) {
        const JsonValue& t = teams.at(s);
        const bool ok_team = !team_bad[s] && t.get("start_markers").as_int_or(0) > 0 && t.get("hill_cells").as_int_or(0) > 0;
        out.usable[s] = ok_team;
        if (ok_team) ++players;
    }
    if (players < 2) {
        out.fail = make_failure(422, "map_fails_checks", "This map does not pass the checks, so it cannot be played.");
        out.fail.extra = JsonValue::make_object();
        out.fail.extra.set("findings", kept_findings(report));
        return out;
    }

    std::string candidate = in.name;
    if (candidate.size() > 4 && (candidate.compare(candidate.size() - 4, 4, ".lvl") == 0 || candidate.compare(candidate.size() - 4, 4, ".LVL") == 0)) candidate.resize(candidate.size() - 4);
    std::string filtered;
    for (char c : candidate) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == ' ' || c == '\'' || c == '-' || c == '_' || c == '.' || c == '!' || c == '&' || c == '(' || c == ')' || c == ',';
        filtered.push_back(ok ? c : ' ');
    }
    while (!filtered.empty() && !((filtered.front() >= 'a' && filtered.front() <= 'z') || (filtered.front() >= 'A' && filtered.front() <= 'Z') || (filtered.front() >= '0' && filtered.front() <= '9'))) {
        filtered.erase(filtered.begin());
    }
    if (filtered.size() > kMaxTestStemChars) filtered.resize(kMaxTestStemChars);
    while (!filtered.empty() && (filtered.back() == ' ' || filtered.back() == '.')) filtered.pop_back();
    const names::Cleaned cleaned = names::clean_map_name(filtered);
    const std::string stem = cleaned.ok && cleaned.text.size() <= kMaxTestStemChars ? cleaned.text : std::string("Test Map");

    // Room for it: an address that holds its share gives up its own oldest, and a full store (by count or by bytes) the oldest of all. A map that a room uses, or that is new, never goes: when nothing can,
    // the upload is refused and NOTHING is dropped (so a burst of uploads cannot push out the map of a room that has not pressed START yet, and a refused upload costs nobody a map).
    std::vector<size_t> taken;
    uint64_t freed = 0;
    size_t mine = 0;
    for (const Kept& m : kept_) mine += m.owner == in.address ? 1u : 0u;
    while (!in.address.empty() && mine >= kMaxTestPerAddress) {
        const size_t i = droppable(now, in.address, taken);
        if (i == kept_.size()) return refuse(429, "test_limit", "You have as many test maps open as you may. Please try again in a few minutes.");
        taken.push_back(i);
        freed += kept_[i].map.size;
        --mine;
    }
    while (kept_.size() - taken.size() >= kMaxTestMaps || held_bytes() - freed + canonical.size() > config_.max_held_bytes) {
        const size_t i = droppable(now, std::string(), taken);
        if (i == kept_.size()) return refuse(503, "test_full", "The server holds as many test maps as it can right now. Please try again in a few minutes.");
        taken.push_back(i);
        freed += kept_[i].map.size;
    }
    std::string id8;
    for (int attempt = 0; attempt < 8 && id8.empty(); ++attempt) {          // (an id that is held already is drawn again: a file is never written over)
        uint8_t id_raw[4];
        if (!random_bytes(id_raw, sizeof id_raw)) return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");
        const std::string drawn = hex_encode(std::string(reinterpret_cast<const char*>(id_raw), sizeof id_raw));
        const std::string file = "test-" + drawn + ".lvl";
        const bool held = std::any_of(kept_.begin(), kept_.end(), [&](const Kept& m) { return fs::path(m.map.path).filename().string() == file; });
        if (!held) id8 = drawn;
    }
    if (id8.empty()) return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");
    const fs::path test_dir = fs::path(config_.dir);
    fs::create_directories(test_dir, ec);
    fs::permissions(test_dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    const std::string path = (test_dir / ("test-" + id8 + ".lvl")).string();
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(canonical.data(), static_cast<std::streamsize>(canonical.size()));
        file.flush();
        if (!file) {
            file.close();
            remove_quietly(path);
            return refuse(503, "storage_full", "The server has no room for more maps right now. Please try again later.");
        }
    }
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);          // (0600)
    uint64_t hash = 0;
    if (!net::hash_file(path, hash)) {
        remove_quietly(path);
        return refuse(503, "storage_error", "The server could not save this just now. Please try again later.");
    }
    ApprovedMap m;
    m.name = stem + " ~test-" + id8 + ".lvl";
    m.path = path;
    m.hash = hash;
    m.size = canonical.size();
    m.width = static_cast<uint32_t>(columns);
    m.height = static_cast<uint32_t>(rows);
    m.minutes = static_cast<uint32_t>(meta.get("minutes").as_int_or(0));
    m.players = static_cast<uint32_t>(players);
    m.description = names::clean_description(meta.get("description").str());
    m.kind = real_kind;
    std::sort(taken.begin(), taken.end(), std::greater<size_t>());          // (the new map is safe on disk: now the ones that made room go)
    for (const size_t i : taken) {
        remove_quietly(kept_[i].map.path);
        kept_.erase(kept_.begin() + static_cast<std::ptrdiff_t>(i));
        changed_ = true;
    }
    kept_.push_back(Kept{m, now, in.address});
    changed_ = true;
    out.ok = true;
    out.map = std::move(m);
    out.clean_name = stem;
    out.hash_text = map_hash_text(hash);
    return out;
}

}  // namespace ants::server

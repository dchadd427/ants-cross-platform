#include "ants_server/map_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "ants_assets/lvl_parser.hpp"
#include "ants_ctl/json.hpp"
#include "ants_net/netgame.hpp"      // hash_file
#include "ants_net/protocol.hpp"     // valid_map_name
#include "ants_server/history_store.hpp"
#include "ants_server/online_limits.hpp"

namespace fs = std::filesystem;

namespace ants::server {

namespace {

// The preview pictures of the six originals are the front page's own (web/front/, served at /front/): no copy of them is kept with the community's
constexpr const char* kOriginalPreviewDir = "/front/";
constexpr const char* kCommunityPreviewDir = "/maps/previews/";

// A file name as an address: everything but the unreserved characters of RFC 3986 is written as %XX (a name holds spaces, '!', '\'', '#', '$', '&' and '~')
std::string percent_encode(const std::string& name) {
    static const char* const kHex = "0123456789ABCDEF";
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool plain = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '.' || u == '_' || u == '~';
        if (plain) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[u >> 4]);
            out.push_back(kHex[u & 15]);
        }
    }
    return out;
}

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

// A picture of the community folder: a plain file name that ends in ".webp" (it becomes the last part of an address; nothing in it can name another folder)
bool plain_picture_name(const std::string& name) {
    if (name.size() < 6 || name.size() > 80 || name.compare(name.size() - 5, 5, ".webp") != 0 || name[0] == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) { const unsigned char u = static_cast<unsigned char>(c); return u >= 0x20 && u <= 0x7E && c != '/' && c != '\\'; });
}

bool read_json_file(const fs::path& path, ctl::JsonValue& out, std::string& why) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        why = "cannot be read";
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    ctl::JsonLimits limits;
    limits.max_bytes = 4u * 1024u * 1024u;                      // (the community's lists are 120 kB and 70 kB: the default 64 kB is for what a visitor sends)
    return ctl::parse_json(text.str(), out, &why, limits);
}

// The files of one folder that find() would accept, by name
std::vector<std::pair<std::string, uint64_t>> files_of(const std::string& dir) {
    std::vector<std::pair<std::string, uint64_t>> out;
    std::error_code ec;
    if (dir.empty()) return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const std::string name = it->path().filename().string();
        if (!net::valid_map_name(name) || !it->is_regular_file(e2) || e2) continue;
        const uint64_t size = static_cast<uint64_t>(it->file_size(e2));
        if (e2 || size == 0 || size > MapStore::kMaxMapBytes) continue;
        out.emplace_back(name, size);
    }
    return out;
}

}  // namespace

const std::vector<std::pair<std::string, std::string>>& MapStore::originals() {
    static const std::vector<std::pair<std::string, std::string>> kOriginals = {
        {"TINY.LVL", "preview_tiny.png"},     {"SMALL.LVL", "preview_small.png"},       {"MEDIUM.LVL", "preview_medium.png"},
        {"GAUNTLET.LVL", "preview_gauntlet.png"}, {"TREASURE.LVL", "preview_treasure.png"}, {"ISLANDS.LVL", "preview_islands.png"},
    };
    return kOriginals;
}

bool MapStore::find(const std::string& name, MapEntry& out, std::string* why) const {
    auto fail = [&](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (!net::valid_map_name(name)) return fail("not a valid map file name");
    std::error_code ec;
    fs::path path = fs::path(dir_) / name;
    if ((!fs::is_regular_file(path, ec) || ec) && !community_dir_.empty()) {
        ec.clear();
        path = fs::path(community_dir_) / name;
    }
    if (!fs::is_regular_file(path, ec) || ec) {
        ec.clear();
        const ApprovedMap* approved_map = approved(name);              // (the approved maps of the players: Classic and Extended; the owner's take-down removes one from here)
        if (approved_map == nullptr) approved_map = test_map(name);    // (an unlisted test map from the Map Maker)
        if (approved_map == nullptr || (approved_map->kind != "classic" && approved_map->kind != "extended")) return fail("no such map on this server");
        path = approved_map->path;
    }
    if (!fs::is_regular_file(path, ec) || ec) return fail("no such map on this server");
    const uint64_t size = static_cast<uint64_t>(fs::file_size(path, ec));
    if (ec || size == 0 || size > kMaxMapBytes) return fail("the map file has an impossible size");
    uint64_t hash = 0;
    if (!net::hash_file(path.string(), hash)) return fail("the map file cannot be read");
    out.name = name;
    out.path = path.string();
    out.hash = hash;
    out.size = size;
    return true;
}

size_t MapStore::scan(std::vector<std::string>* notes) {
    listing_.clear();
    scanned_ = true;
    auto note = [&](const std::string& line) {
        if (notes != nullptr) notes->push_back(line);
    };
    // what maps.json and previews.json of the community folder say, by file name
    std::map<std::string, ctl::JsonValue> described;
    std::map<std::string, std::pair<std::string, std::string>> pictured;      // file -> (hash, picture)
    if (!community_dir_.empty()) {
        ctl::JsonValue json;
        std::string why;
        const fs::path list = fs::path(community_dir_) / "maps.json";
        std::error_code ec;
        if (fs::exists(list, ec) && !ec) {
            if (!read_json_file(list, json, why)) {
                note("maps.json of the community folder " + why + ": the maps are listed from their own files");
            } else {
                const ctl::JsonValue& maps = json.get("maps");
                for (size_t i = 0; i < maps.size(); ++i) described[maps.at(i).get("file").str()] = maps.at(i);
            }
        } else if (ec) {
            note("maps.json of the community folder cannot be looked at (" + ec.message() + "): the maps are listed from their own files");
        }
        json = ctl::JsonValue();
        const fs::path pictures = fs::path(community_dir_) / "previews.json";
        ec.clear();
        if (fs::exists(pictures, ec) && !ec) {
            if (!read_json_file(pictures, json, why)) {
                note("previews.json of the community folder " + why + ": the maps have no pictures");
            } else {
                for (size_t i = 0; i < json.size(); ++i) pictured[json.key_at(i)] = {json.at(i).get("hash").str(), json.at(i).get("picture").str()};
            }
        } else if (ec) {
            note("previews.json of the community folder cannot be looked at (" + ec.message() + "): the maps have no pictures");
        }
    }
    std::set<std::string> seen;
    auto add = [&](const std::string& name, bool community) {
        if (!seen.insert(name).second) return;                         // (the maps folder wins, as in find())
        MapEntry entry;
        if (!find(name, entry)) return;
        MapListing m;
        m.file = name;
        m.hash = entry.hash;
        m.size = entry.size;
        const auto original = std::find_if(originals().begin(), originals().end(), [&](const auto& o) { return o.first == name; });
        m.original = !community && original != originals().end();
        const auto known = described.find(name);
        if (community && known != described.end() && known->second.get("hash").str() == map_hash_text(entry.hash)) {
            const ctl::JsonValue& d = known->second;
            m.width = static_cast<uint32_t>(d.get("width").as_int_or(0));
            m.height = static_cast<uint32_t>(d.get("height").as_int_or(0));
            m.minutes = static_cast<uint32_t>(d.get("minutes").as_int_or(0));
            m.players = static_cast<uint32_t>(d.get("players").as_int_or(0));
            m.description = d.get("description").str();
            assets::LevelData counted;                                  // (maps.json does not know the ants: they are counted from the file, so that the list can tell a map that a room refuses)
            if (counted.load_from_file(entry.path, nullptr, assets::LevelLimits::for_play_extended())) m.start_ants = OnlineLimits::start_ants(counted);
        } else {
            if (community && known != described.end()) note("maps.json describes " + name + " with another hash than the file has: the file's own header is used");
            assets::LevelData level;
            if (!level.load_from_file(entry.path, nullptr, assets::LevelLimits::for_play_extended())) return;             // (a file that does not load is not offered)
            m.width = level.width;
            m.height = level.height;
            m.minutes = level.default_minutes;
            m.description = level.description;
            std::set<uint8_t> teams;                                    // (the teams that have a start marker: as Community-Maps counts the players, and four when none has)
            for (const assets::AnthillSpawn& a : level.anthill_spawns()) {
                if (a.team_id < 4) teams.insert(a.team_id);
            }
            m.players = teams.empty() ? 4u : static_cast<uint32_t>(teams.size());
            m.start_ants = OnlineLimits::start_ants(level);
        }
        if (m.original) {
            m.thumb = std::string(kOriginalPreviewDir) + original->second;
        } else {
            const auto picture = pictured.find(name);
            if (community && picture != pictured.end() && picture->second.first == map_hash_text(entry.hash) && plain_picture_name(picture->second.second)) {
                m.thumb = std::string(kCommunityPreviewDir) + percent_encode(picture->second.second);
            }
        }
        listing_.push_back(std::move(m));
    };
    for (const auto& f : files_of(dir_)) add(f.first, false);
    for (const auto& f : files_of(community_dir_)) add(f.first, true);
    add_approved_to_listing();
    sort_listing();
    return listing_.size();
}

void MapStore::sort_listing() {
    std::sort(listing_.begin(), listing_.end(), [](const MapListing& a, const MapListing& b) {
        const auto rank = [](const MapListing& m) -> size_t {
            if (!m.original) return originals().size();
            return static_cast<size_t>(std::find_if(originals().begin(), originals().end(), [&](const auto& o) { return o.first == m.file; }) - originals().begin());
        };
        const size_t ra = rank(a);
        const size_t rb = rank(b);
        return ra != rb ? ra < rb : a.file < b.file;
    });
}

// The approved maps of the players join the list after the library: a name that the maps or the community folder has already wins (it cannot be approved any more, but a map added to the library later
// could clash), and a file that is gone is left out.
void MapStore::add_approved_to_listing() {
    listing_.erase(std::remove_if(listing_.begin(), listing_.end(), [](const MapListing& m) { return m.approved; }), listing_.end());
    extended_listing_.clear();
    std::set<std::string> seen;
    for (const MapListing& m : listing_) seen.insert(lower(m.file));
    for (const ApprovedMap& a : approved_) {
        if (!net::valid_map_name(a.name)) continue;
        MapListing m;
        m.file = a.name;
        m.hash = a.hash;
        m.size = a.size;
        m.width = a.width;
        m.height = a.height;
        m.minutes = a.minutes;
        m.players = a.players;
        m.start_ants = a.start_ants;
        m.description = a.description;
        m.approved = true;
        m.maker = a.maker;
        m.kind = a.kind;
        m.thumb = std::string(kCommunityPreviewDir) + percent_encode(a.name.substr(0, a.name.size() - 4)) + ".png";
        if (a.kind == "extended") {
            extended_listing_.push_back(std::move(m));
        } else if (seen.insert(lower(a.name)).second) {
            listing_.push_back(std::move(m));
        }
    }
    std::sort(extended_listing_.begin(), extended_listing_.end(), [](const MapListing& a, const MapListing& b) { return a.file < b.file; });
}

// The start ants of a file of the store: counted from the file, kept by path, size and hash (an approved file never changes, and the list is rebuilt at every approval and take-down)
uint64_t MapStore::start_ants_of_file(const std::string& path, uint64_t size, uint64_t hash) {
    const auto kept = counted_.find(path);
    if (kept != counted_.end() && kept->second.size == size && kept->second.hash == hash) return kept->second.ants;
    assets::LevelData level;
    const uint64_t ants = level.load_from_file(path, nullptr, assets::LevelLimits::for_play_extended()) ? OnlineLimits::start_ants(level) : 0u;          // (a file that does not load starts none: the room's own load says no)
    counted_[path] = Counted{size, hash, ants};
    return ants;
}

void MapStore::count_start_ants() {
    for (ApprovedMap& a : approved_) a.start_ants = start_ants_of_file(a.path, a.size, a.hash);
    for (ApprovedMap& t : test_maps_) t.start_ants = start_ants_of_file(t.path, t.size, t.hash);
    for (auto it = counted_.begin(); it != counted_.end();) {          // (a file that is no longer held is forgotten)
        const bool held = std::any_of(approved_.begin(), approved_.end(), [&](const ApprovedMap& a) { return a.path == it->first; }) ||
                          std::any_of(test_maps_.begin(), test_maps_.end(), [&](const ApprovedMap& t) { return t.path == it->first; });
        it = held ? std::next(it) : counted_.erase(it);
    }
}

void MapStore::set_approved(std::vector<ApprovedMap> maps) {
    approved_ = std::move(maps);
    count_start_ants();
    if (!scanned_) return;
    add_approved_to_listing();
    sort_listing();
}

bool MapStore::library_has_name(const std::string& key) const {
    for (const auto& o : originals()) {
        if (lower(o.first.substr(0, o.first.size() - 4)) == key) return true;
    }
    for (const MapListing& m : listing_) {
        if (!m.approved && m.file.size() > 4 && lower(m.file.substr(0, m.file.size() - 4)) == key) return true;
    }
    return false;
}

const ApprovedMap* MapStore::approved(const std::string& file_name) const {
    for (const ApprovedMap& a : approved_) {
        if (a.name == file_name) return &a;
    }
    return nullptr;
}

bool MapStore::is_test_map_name(const std::string& file_name) noexcept {
    if (file_name.size() < 20 || !net::valid_map_name(file_name)) return false;
    const size_t suffix = file_name.size() - 19;
    if (file_name.compare(suffix, 7, " ~test-") != 0 || (file_name.compare(file_name.size() - 4, 4, ".lvl") != 0 && file_name.compare(file_name.size() - 4, 4, ".LVL") != 0)) return false;
    for (size_t i = suffix + 7; i < suffix + 15; ++i) {
        const char c = file_name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

void MapStore::set_test_maps(std::vector<ApprovedMap> maps) {
    test_maps_ = std::move(maps);
    count_start_ants();
}

const ApprovedMap* MapStore::test_map(const std::string& file_name) const {
    for (const ApprovedMap& m : test_maps_) {
        if (m.name == file_name) return &m;
    }
    return nullptr;
}

namespace {
uint64_t cells_in(uint32_t width, uint32_t height) { return static_cast<uint64_t>(width) * height; }
}  // namespace

namespace {

// Why a room does not play a map of `cells` cells that starts `start_ants` ants ("" when it does): the size first (net::kNoticeMapTooBig), then the ants (OnlineLimits::too_many_ants_notice). The six maps of the
// original game are never refused for their ants: they must always be playable online.
std::string refusal_sentence(bool original, uint64_t cells, uint64_t start_ants) {
    if (!OnlineLimits::allows(cells)) return net::kNoticeMapTooBig;
    if (!original && !OnlineLimits::allows_ants(start_ants)) return OnlineLimits::too_many_ants_notice(start_ants);
    return std::string();
}

std::string refusal_of(const MapListing& m) { return refusal_sentence(m.original, cells_in(m.width, m.height), m.start_ants); }
std::string refusal_of(const ApprovedMap& a) { return refusal_sentence(false, cells_in(a.width, a.height), a.start_ants); }

}  // namespace

std::string MapStore::listed_name(const std::string& name) const {
    const std::string folded = lower(name);
    for (const MapListing& m : listing_) {
        if (m.file == name && refusal_of(m).empty()) return m.file;
    }
    for (const MapListing& m : extended_listing_) {
        if (m.file == name && refusal_of(m).empty()) return m.file;
    }
    for (const ApprovedMap& t : test_maps_) {
        if (t.name == name && refusal_of(t).empty()) return t.name;
    }
    for (const MapListing& m : listing_) {
        if (lower(m.file) == folded && refusal_of(m).empty()) return m.file;
    }
    for (const MapListing& m : extended_listing_) {
        if (lower(m.file) == folded && refusal_of(m).empty()) return m.file;
    }
    for (const ApprovedMap& t : test_maps_) {
        if (lower(t.name) == folded && refusal_of(t).empty()) return t.name;
    }
    return std::string();
}

std::string MapStore::online_refusal(const std::string& name) const {
    const std::string folded = lower(name);
    for (const MapListing& m : listing_) {
        if (lower(m.file) == folded && !refusal_of(m).empty()) return refusal_of(m);
    }
    for (const MapListing& m : extended_listing_) {
        if (lower(m.file) == folded && !refusal_of(m).empty()) return refusal_of(m);
    }
    for (const ApprovedMap& t : test_maps_) {
        if (lower(t.name) == folded && !refusal_of(t).empty()) return refusal_of(t);
    }
    return std::string();
}

// One of the six maps of the original game as the maps folder holds it (find() gave the path of the maps folder, not of the community folder or an approved map that has the same name)
bool MapStore::original_file(const MapEntry& entry) const {
    const auto& names = originals();
    return std::any_of(names.begin(), names.end(), [&](const auto& o) { return o.first == entry.name; }) && entry.path == (fs::path(dir_) / entry.name).string();
}

std::string MapStore::online_refusal(const MapEntry& entry, const assets::LevelData& level) const {
    return refusal_sentence(original_file(entry), cells_in(level.width(), level.height()), OnlineLimits::start_ants(level));
}

uint64_t MapStore::cells_of(const std::string& name) const {
    for (const MapListing& m : listing_) {
        if (m.file == name) return cells_in(m.width, m.height);
    }
    for (const MapListing& m : extended_listing_) {
        if (m.file == name) return cells_in(m.width, m.height);
    }
    for (const ApprovedMap& t : test_maps_) {
        if (t.name == name) return cells_in(t.width, t.height);
    }
    return 0;
}

std::string MapStore::list_json(const std::vector<MapPlays>* plays, bool with_extended) const {
    std::map<std::pair<std::string, std::string>, int64_t> counts;
    if (plays != nullptr) {
        for (const MapPlays& p : *plays) counts[{p.map, p.hash}] = p.plays;
    }
    ctl::JsonValue maps = ctl::JsonValue::make_array();
    std::vector<const MapListing*> shown;
    for (const MapListing& m : listing_) shown.push_back(&m);
    if (with_extended) {
        for (const MapListing& m : extended_listing_) shown.push_back(&m);
    }
    for (const MapListing* mp : shown) {
        const MapListing& m = *mp;
        ctl::JsonValue o = ctl::JsonValue::make_object();
        o.set("file", ctl::JsonValue::make_string(m.file));
        o.set("width", ctl::JsonValue::make_int(m.width));
        o.set("height", ctl::JsonValue::make_int(m.height));
        o.set("minutes", ctl::JsonValue::make_int(m.minutes));
        o.set("players", ctl::JsonValue::make_int(m.players));
        o.set("description", ctl::JsonValue::make_string(m.description));
        const auto found = counts.find({m.file, map_hash_text(m.hash)});
        o.set("plays", ctl::JsonValue::make_int(found == counts.end() ? 0 : found->second));
        if (!m.thumb.empty()) o.set("thumb", ctl::JsonValue::make_string(m.thumb));
        if (m.original) o.set("original", ctl::JsonValue::make_bool(true));
        if (m.approved) o.set("maker", ctl::JsonValue::make_string(m.maker));
        if (with_extended) o.set("kind", ctl::JsonValue::make_string(m.kind));
        if (with_extended && !refusal_of(m).empty()) {                // (a room refuses it: the picker says so; it can still be opened in the Map Maker)
            o.set("online", ctl::JsonValue::make_bool(false));
            if (OnlineLimits::allows(cells_in(m.width, m.height))) o.set("ants", ctl::JsonValue::make_int(static_cast<int64_t>(m.start_ants)));       // (the ants are the reason: the picker names the number)
        }
        maps.push_back(std::move(o));
    }
    ctl::JsonValue root = ctl::JsonValue::make_object();
    root.set("format", ctl::JsonValue::make_int(with_extended ? 2 : 1));
    root.set("maps", std::move(maps));
    return ctl::to_json(root);
}

}  // namespace ants::server

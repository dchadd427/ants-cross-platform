#pragma once

// The maps folder of a game server: the one place from which a room's map is read. A map is named by its file name, exactly as the lobby names it and as the
// clients' own folders hold it; the name must be one that can travel in the protocol (ants::net::valid_map_name: printable text that cannot name a path), so a
// request can never reach a file outside the folder. The hash is the one of the start barrier (FNV-1a 64 of the file, net::hash_file): a client whose copy of
// the file differs cannot take part.
//
// A server has two folders of maps: the maps folder (--maps: the six of the original game in the image, or whatever the operator mounts) and, optionally, the community folder
// (--community-maps: the players' maps, Community-Maps/ of the repository with its maps.json and previews.json, copied into the image where no volume can hide it). A name is looked up
// in the maps folder first. scan() lists and hashes everything once, at the server's start, for GET /maps; find() always reads the file itself.

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ants::server {

struct MapPlays;     // history_store.hpp

struct MapEntry {
    std::string name;          // the file name in the folder
    std::string path;          // folder + name
    uint64_t hash{0};          // FNV-1a 64 of the file
    uint64_t size{0};          // bytes
};

/// One map of the list (GET /maps): what maps.json says (or, for a file it does not know, what the level file itself says), the real hash of the file and the picture of it.
struct MapListing {
    std::string file;
    uint64_t hash{0};
    uint64_t size{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t minutes{0};
    uint32_t players{0};
    uint64_t start_ants{0};    // the start markers of the four colors added up, counted from the file by the store itself (OnlineLimits::start_ants); 0 when the file does not load
    std::string description;
    bool original{false};      // one of the six maps of the original game
    std::string thumb;         // the address of its preview picture on the site, "" when it has none
    bool approved{false};      // a map that a player sent and the owner approved (docs/MAP_SUBMISSIONS.md): its file is in the submissions folder
    std::string maker;         // the maker's display name (an approved map only)
    std::string kind{"classic"};     // "classic" or "extended" (an Extended map is only ever listed by list_json(.., true))
};

/// An approved map as the submissions store hands it over: the file is the canonical one in the submissions folder, `name` the file name that the game knows the map by ("<Name>.lvl")
struct ApprovedMap {
    std::string name;
    std::string path;
    uint64_t hash{0};          // FNV-1a 64 of the file (net::hash_file)
    uint64_t size{0};
    uint32_t width{0}, height{0}, minutes{0}, players{0};
    uint64_t start_ants{0};    // filled in by MapStore::set_approved from the file (never from the caller's number); a test map's is not counted: it is on no list
    std::string description, maker, kind;
};

class MapStore {
public:
    static constexpr uint64_t kMaxMapBytes = 4u * 1024u * 1024u;       // a map is a few hundred kilobytes at most (the largest of the community's is 235 kB)

    /// The six maps of the original game, in the order that the list shows them, and the address of the preview picture of each (pictures of the front page: web/front/)
    static const std::vector<std::pair<std::string, std::string>>& originals();

    explicit MapStore(std::string directory, std::string community_directory = std::string()) : dir_(std::move(directory)), community_dir_(std::move(community_directory)) {}
    const std::string& directory() const noexcept { return dir_; }
    const std::string& community_directory() const noexcept { return community_dir_; }

    /// Finds the map file `name`: the name must be a valid map name, the file must be a regular file of 1 .. kMaxMapBytes bytes. False with a reason otherwise.
    /// (the maps folder is asked first, then the community folder)
    bool find(const std::string& name, MapEntry& out, std::string* why = nullptr) const;

    /// Lists and hashes every map of both folders (a file that find() would refuse is left out) and reads the community folder's maps.json and previews.json. Returns the number of maps; `notes` gets one line
    /// for each thing that is wrong but not fatal (a bad json file, a map that maps.json describes with another hash). The list: the six originals first, in the order of originals(), then the others by file name.
    size_t scan(std::vector<std::string>* notes = nullptr);
    const std::vector<MapListing>& listing() const noexcept { return listing_; }
    bool scanned() const noexcept { return scanned_; }
    /// The name under which the scanned list knows `name` (the same name, or the one that differs in letter case only); "" when it does not. A map that is too big for a room (OnlineLimits::kMaxCells) is
    /// not found here: the lobby may not choose it (it is still on the lists, and its file and picture are still served).
    std::string listed_name(const std::string& name) const;
    /// Whether `name` (exactly, or in another letter case) is an approved or test map that is too big to be played online (online_limits.hpp): the leader who chose it is told so
    bool too_big_online(const std::string& name) const;
    /// The cells (rows times columns) of the list's or the approved or test map `name` (exact name), 0 when it is not known: what a match on it costs (OnlineLimits::weight)
    uint64_t cells_of(const std::string& name) const;
    /// The body of GET /maps: {"format":1,"maps":[{file,width,height,minutes,players,description,plays,thumb[,original][,maker]}]}. `plays` is how many times each map was played (map_plays of the database, keyed
    /// by file name and hash); null when the server keeps none: every count is 0 then. With `with_extended` (GET /maps/all, for the newest version only) the body is format 2: every entry also has "kind",
    /// and the approved Extended maps are in it, which GET /maps leaves out for older pages; a map that is too big for a room carries "online": false (online_limits.hpp).
    std::string list_json(const std::vector<MapPlays>* plays, bool with_extended = false) const;

    /// The approved maps of the submissions store (both Classic and Extended maps can be chosen and found by name; Classic ones join GET /maps, and Extended ones join GET /maps/all). Replaces the set that was given before;
    /// the list is rebuilt at once, the folders are not scanned again.
    void set_approved(std::vector<ApprovedMap> maps);
    /// Whether `key` (the lower-case name without ".lvl") is the name of a map of the library: one of the six originals or a file of the maps or the community folder. Approved maps do not count.
    bool library_has_name(const std::string& key) const;
    /// The approved map that has this file name (exactly), or nullptr. Used to serve its file and picture; a name that is not approved or is taken down is not found.
    const ApprovedMap* approved(const std::string& file_name) const;

    /// Ephemeral unlisted test maps from the Map Maker ("<Name> ~test-<8hex>.lvl"): playable in rooms and served at /maps/files/ and /maps/previews/, never listed in GET /maps or GET /maps/all.
    static bool is_test_map_name(const std::string& file_name) noexcept;
    void set_test_maps(std::vector<ApprovedMap> maps);
    const ApprovedMap* test_map(const std::string& file_name) const;

private:
    std::string dir_;
    std::string community_dir_;
    std::vector<MapListing> listing_;
    std::vector<MapListing> extended_listing_;         // the approved Extended maps (never in listing_)
    std::vector<ApprovedMap> approved_;
    std::vector<ApprovedMap> test_maps_;               // unlisted test maps from the Map Maker (never in listing_ or extended_listing_)
    bool scanned_{false};
    void add_approved_to_listing();
    void sort_listing();
    // The start ants of an approved file (OnlineLimits::start_ants of the loaded map; 0 when it does not load), kept by path, size and hash so that a list that is rebuilt at every approval and take-down loads a file once
    struct Counted {
        uint64_t size{0};
        uint64_t hash{0};
        uint64_t ants{0};
    };
    std::map<std::string, Counted> counted_;
    uint64_t start_ants_of_file(const std::string& path, uint64_t size, uint64_t hash);
    void count_start_ants();
};

}  // namespace ants::server

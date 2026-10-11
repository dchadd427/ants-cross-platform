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
    std::string description;
    bool original{false};      // one of the six maps of the original game
    std::string thumb;         // the address of its preview picture on the site, "" when it has none
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
    /// The name under which the scanned list knows `name` (the same name, or the one that differs in letter case only); "" when it does not
    std::string listed_name(const std::string& name) const;
    /// The body of GET /maps: {"format":1,"maps":[{file,width,height,minutes,players,description,plays,thumb[,original]}]}. `plays` is how many times each map was played (map_plays of the database, keyed
    /// by file name and hash); null when the server keeps none: every count is 0 then.
    std::string list_json(const std::vector<MapPlays>* plays) const;

private:
    std::string dir_;
    std::string community_dir_;
    std::vector<MapListing> listing_;
};

}  // namespace ants::server

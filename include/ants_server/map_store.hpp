#pragma once

// The maps folder of a game server: the one place from which a room's map is read. A map is named by its file name, exactly as the lobby names it and as the
// clients' own folders hold it; the name must be one that can travel in the protocol (ants::net::valid_map_name: printable text that cannot name a path), so a
// request can never reach a file outside the folder. The hash is the one of the start barrier (FNV-1a 64 of the file, net::hash_file): a client whose copy of
// the file differs cannot take part.

#include <cstdint>
#include <string>

namespace ants::server {

struct MapEntry {
    std::string name;          // the file name in the folder
    std::string path;          // folder + name
    uint64_t hash{0};          // FNV-1a 64 of the file
    uint64_t size{0};          // bytes
};

class MapStore {
public:
    static constexpr uint64_t kMaxMapBytes = 4u * 1024u * 1024u;       // a map is a few hundred kilobytes at most (the largest of the community's is 235 kB)

    explicit MapStore(std::string directory) : dir_(std::move(directory)) {}
    const std::string& directory() const noexcept { return dir_; }

    /// Finds the map file `name`: the name must be a valid map name, the file must be a regular file of 1 .. kMaxMapBytes bytes. False with a reason otherwise.
    bool find(const std::string& name, MapEntry& out, std::string* why = nullptr) const;

private:
    std::string dir_;
};

}  // namespace ants::server

#pragma once
// Getting a map file that this machine does not have (the web build: the players' maps are not in the page's virtual file system, which holds the six originals only; web/map_files.js fetches the
// one that a room plays from the site, checks its hash and writes it into the maps folder). The game never waits inside a call: it asks, polls once a frame and goes on with the network.
#include <cstdint>
#include <memory>
#include <string>

namespace ants::app {

class MapFetcher {
public:
    enum class State : uint8_t { Pending, Ready, Failed };
    struct Status {
        State state{State::Pending};
        std::string message;           // Failed: why, in words for the player ("The map X is not on the server ...")
    };

    virtual ~MapFetcher() = default;
    /// Asks for the file `name` to be put into `folder`. `hash` is the FNV-1a 64 that the room's Start names (the file must have it), or 0 when the room named only the map (a head start on the Start).
    /// Asking again for a map that is on its way changes nothing but the hash; asking again after a failure is a new try.
    virtual void request(const std::string& folder, const std::string& name, uint64_t hash) = 0;
    /// Where `name` is. Ready means the file is in the folder and has the hash of the last request that named one.
    virtual Status status(const std::string& name) = 0;
};

/// The browser's fetcher (map_fetch.cpp); null in every other build, where the maps folder is all there is
std::unique_ptr<MapFetcher> make_web_map_fetcher();

}  // namespace ants::app

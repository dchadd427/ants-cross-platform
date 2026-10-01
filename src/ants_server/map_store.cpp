#include "ants_server/map_store.hpp"

#include <filesystem>

#include "ants_net/netgame.hpp"      // hash_file
#include "ants_net/protocol.hpp"     // valid_map_name

namespace fs = std::filesystem;

namespace ants::server {

bool MapStore::find(const std::string& name, MapEntry& out, std::string* why) const {
    auto fail = [&](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (!net::valid_map_name(name)) return fail("not a valid map file name");
    std::error_code ec;
    const fs::path path = fs::path(dir_) / name;
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

}  // namespace ants::server

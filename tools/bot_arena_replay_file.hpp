// bot_arena --save-replays: the file of one match, written the way the game server keeps the matches of its rooms, so that a folder that a server reads shows it in its list.
//
// The name is ants-<MAP>-<YYYYMMDD>-<HHMMSS>Z.antsrep: the map (the letters, digits and '_' of its file name without the extension, any other character a '_', at most 24 of them,
// "match" when nothing is left) and the UTC time of the write, with a "-2", "-3" ... before the extension when that name is taken. The bytes are written whole under "<name>.tmp"
// and renamed, so a crash leaves a whole file or none; a file that is there is never replaced and nothing is deleted. The caller holds a lock: gmtime is not thread safe.
#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace ants::arena {

/// Writes `bytes`, the file of a match on `map_file`, into the folder `dir`: the name that the file got, or "" with the reason in `why`
inline std::string write_replay_file(const std::filesystem::path& dir, const std::string& map_file, const std::vector<uint8_t>& bytes, std::string& why) {
    namespace fs = std::filesystem;
    const size_t dot = map_file.rfind('.');
    std::string stem;
    for (const char c : (dot == std::string::npos || dot == 0 ? map_file : map_file.substr(0, dot))) {
        if (stem.size() >= 24) break;
        stem.push_back((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ? c : '_');
    }
    const std::time_t now = std::time(nullptr);
    char stamp[24] = {0};
    const std::tm* utc = std::gmtime(&now);
    if (utc == nullptr || std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%SZ", utc) == 0) {
        why = "the clock has no date";
        return std::string();
    }
    const std::string base = "ants-" + (stem.empty() ? std::string("match") : stem) + "-" + stamp;
    std::string name = base + ".antsrep";
    std::error_code ec;
    for (unsigned n = 2; n <= 9999 && fs::exists(dir / name, ec); ++n) name = base + "-" + std::to_string(n) + ".antsrep";
    if (fs::exists(dir / name, ec)) {
        why = "every name for a match of this second is taken";
        return std::string();
    }
    const fs::path temp = dir / (name + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fs::remove(temp, ec);
            why = "the disk refused the write";
            return std::string();
        }
    }
    fs::rename(temp, dir / name, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        why = "the file could not be given its name: " + ec.message();
        return std::string();
    }
    return name;
}

}  // namespace ants::arena

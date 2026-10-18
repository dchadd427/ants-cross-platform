#include "ants_server/map_preview.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>
#include <vector>

#include "ants_assets/lvl_core.hpp"
#include "ants_assets/lvl_parser.hpp"

namespace ants::server {

namespace {

constexpr uint16_t kEmptyTile = 0x7FFE;

struct Range {
    uint32_t first, last;
    uint32_t rgb;
};

// The colors of web/front/mapmaker_tiles.js. Ground: [first id, last id, kind] with three shades of each kind, chosen by id % 3.
constexpr uint32_t kGrass[3] = {0x6a9a44, 0x5f8f3c, 0x58873a};
constexpr uint32_t kDirt[3] = {0x9a8460, 0x917c58, 0xa08c68};
constexpr uint32_t kSand[3] = {0xcdbb8c, 0xc6b384, 0xd3c294};
constexpr uint32_t kMud[3] = {0x7a5a3a, 0x725232, 0x82623f};
constexpr uint32_t kWater[3] = {0x2e6fa8, 0x2b69a0, 0x3276b0};

struct GroundRange {
    uint32_t first, last;
    const uint32_t* shades;
};
constexpr GroundRange kGround[] = {{431, 475, kMud}, {476, 484, kWater}, {485, 486, kGrass}, {487, 490, kDirt}, {491, 522, kGrass}, {523, 570, kDirt},
                                   {571, 579, kSand}, {580, 595, kGrass}, {596, 611, kMud}, {612, 659, kSand}, {660, 669, kMud}};
constexpr Range kObjects[] = {{34, 38, 0xa67c52}, {62, 66, 0xd65db1}, {129, 133, 0x161616}, {134, 134, 0x4a4a4a}, {195, 198, 0xe8742a}, {205, 208, 0xa67c52}, {209, 212, 0x161616},
                              {245, 245, 0x262626}, {246, 246, 0x4a5fc8}, {247, 247, 0xc0392b}, {248, 248, 0x2f9a52}, {249, 249, 0x262626}, {250, 250, 0x4a5fc8}, {251, 251, 0x2f9a52},
                              {252, 252, 0xc0392b}, {253, 286, 0xf2c230}, {287, 310, 0x7d7d7d}, {311, 322, 0xf2c230}, {323, 330, 0x2f6b2a}, {340, 426, 0xf2c230}, {404, 421, 0x2f6b2a},
                              {1320, 1329, 0x2f6b2a}};
constexpr uint32_t kOtherObject = 0x8a7a5a;
constexpr uint32_t kTeams[4] = {0x2f9a52, 0xc0392b, 0x4a5fc8, 0x262626};     // green, red, blue, black (the remake's team numbers)

uint32_t ground_color(uint32_t id) {
    for (const GroundRange& r : kGround) {
        if (id >= r.first && id <= r.last) return r.shades[id % 3];
    }
    return kGrass[1];
}

uint32_t object_color(uint32_t id) {
    uint32_t found = kOtherObject;
    for (const Range& r : kObjects) {        // (the later range wins where two overlap, as in the page's table)
        if (id >= r.first && id <= r.last) found = r.rgb;
    }
    return found;
}

const std::unordered_map<std::string, uint32_t>& ids_of_names() {
    static const std::unordered_map<std::string, uint32_t> map = [] {
        std::unordered_map<std::string, uint32_t> m;
        const std::vector<std::string> names = assets::lvl_core::tile_names();
        for (uint32_t i = 0; i < names.size(); ++i) m.emplace(names[i], i);
        return m;
    }();
    return map;
}

uint32_t crc32_of(const unsigned char* data, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void put32(std::string& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<char>((v >> s) & 0xFF));
}

void chunk(std::string& out, const char* type, const std::string& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    std::string body(type, 4);
    body += data;
    out += body;
    put32(out, ~crc32_of(reinterpret_cast<const unsigned char*>(body.data()), body.size()));
}

}  // namespace

std::string render_preview_png(const assets::LevelData& level, uint32_t max_side) {
    const uint32_t w = level.width;
    const uint32_t h = level.height;
    if (w == 0 || h == 0 || max_side == 0 || level.layer1_terrain.size() < static_cast<size_t>(w) * h) return std::string();
    const uint32_t longer = std::max(w, h);
    const uint32_t scale_up = std::max<uint32_t>(1, max_side / longer);
    const uint32_t ow = longer * scale_up <= max_side ? w * scale_up : std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(w) * max_side / longer));
    const uint32_t oh = longer * scale_up <= max_side ? h * scale_up : std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(h) * max_side / longer));
    const auto& ids = ids_of_names();
    const auto tile_id = [&](uint16_t index) -> uint32_t {
        if (index == kEmptyTile || index >= level.tile_dictionary.size()) return 0xFFFFFFFFu;
        const auto it = ids.find(level.tile_dictionary[index]);
        return it == ids.end() ? 0xFFFFFFFFu : it->second;
    };
    // the color of every cell once (a cell is read as often as it covers pixels otherwise)
    std::vector<uint32_t> rgb(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < rgb.size(); ++i) {
        const uint32_t t2 = i < level.layer2_interactive.size() ? tile_id(level.layer2_interactive[i].tile_index) : 0xFFFFFFFFu;
        rgb[i] = t2 != 0xFFFFFFFFu ? object_color(t2) : ground_color(tile_id(level.layer1_terrain[i].tile_index));
    }
    std::vector<uint32_t> pixels(static_cast<size_t>(ow) * oh);
    for (uint32_t y = 0; y < oh; ++y) {
        const uint32_t cy = std::min<uint32_t>(h - 1, static_cast<uint32_t>(static_cast<uint64_t>(y) * h / oh));
        for (uint32_t x = 0; x < ow; ++x) {
            const uint32_t cx = std::min<uint32_t>(w - 1, static_cast<uint32_t>(static_cast<uint64_t>(x) * w / ow));
            pixels[static_cast<size_t>(y) * ow + x] = rgb[static_cast<size_t>(cy) * w + cx];
        }
    }
    // the teams' start markers, so that the picture shows where the hills are: a square of 3 pixels
    for (const assets::AnthillSpawn& a : level.anthill_spawns()) {
        if (a.team_id >= 4 || a.x >= w || a.y >= h) continue;
        const int px = static_cast<int>(static_cast<uint64_t>(a.x) * ow / w);
        const int py = static_cast<int>(static_cast<uint64_t>(a.y) * oh / h);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = px + dx, y = py + dy;
                if (x >= 0 && y >= 0 && x < static_cast<int>(ow) && y < static_cast<int>(oh)) pixels[static_cast<size_t>(y) * ow + static_cast<size_t>(x)] = kTeams[a.team_id];
            }
        }
    }
    std::map<uint32_t, unsigned char> palette_index;
    std::string palette;
    std::string scan;
    scan.reserve(static_cast<size_t>(oh) * (ow + 1));
    for (uint32_t y = 0; y < oh; ++y) {
        scan.push_back('\0');                                           // filter: none
        for (uint32_t x = 0; x < ow; ++x) {
            const uint32_t c = pixels[static_cast<size_t>(y) * ow + x];
            auto it = palette_index.find(c);
            if (it == palette_index.end()) {
                if (palette_index.size() >= 256) return std::string();     // (the color tables have far fewer)
                it = palette_index.emplace(c, static_cast<unsigned char>(palette_index.size())).first;
                palette.push_back(static_cast<char>((c >> 16) & 0xFF));
                palette.push_back(static_cast<char>((c >> 8) & 0xFF));
                palette.push_back(static_cast<char>(c & 0xFF));
            }
            scan.push_back(static_cast<char>(it->second));
        }
    }
    // zlib stream of stored (uncompressed) blocks
    std::string z;
    z.push_back('\x78');
    z.push_back('\x01');
    for (size_t pos = 0; pos < scan.size() || pos == 0;) {
        const size_t n = std::min<size_t>(65535, scan.size() - pos);
        const bool last = pos + n >= scan.size();
        z.push_back(last ? '\x01' : '\x00');
        z.push_back(static_cast<char>(n & 0xFF));
        z.push_back(static_cast<char>(n >> 8));
        z.push_back(static_cast<char>(~n & 0xFF));
        z.push_back(static_cast<char>((~n >> 8) & 0xFF));
        z.append(scan, pos, n);
        pos += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (const char c : scan) {
        a = (a + static_cast<unsigned char>(c)) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(z, (b << 16) | a);
    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    put32(ihdr, ow);
    put32(ihdr, oh);
    ihdr += std::string("\x08\x03\x00\x00\x00", 5);
    chunk(out, "IHDR", ihdr);
    chunk(out, "PLTE", palette);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", std::string());
    return out;
}

}  // namespace ants::server

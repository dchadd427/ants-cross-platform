// The world art (world_art.hpp): the manifest reader and the rule that chooses the picture of a ground tile.
#include "ants_app/world_art.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>

namespace ants::app::world_art {

namespace {

constexpr int32_t kMaxSheet = 16384;           // a side of a sheet that the manifest may state (the device's own limit is asked when the texture is made)
constexpr int32_t kMaxSprite = 512;            // a sprite of the original is at most 262 pixels each way (a leaf of the plants)
constexpr uint32_t kMaxNumber = 100000;

bool to_int(const std::string& text, int64_t& out) {
    if (text.empty() || text.size() > 9) return false;
    size_t i = 0;
    if (text[0] == '-') i = 1;
    if (i >= text.size()) return false;
    for (size_t k = i; k < text.size(); ++k) {
        if (text[k] < '0' || text[k] > '9') return false;
    }
    out = std::strtoll(text.c_str(), nullptr, 10);
    return true;
}

std::vector<std::string> words_of(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

/// The whole file as bytes. Never throws: a file that cannot be read, and a folder where the file should be (the standard library of Linux throws when it reads one), are a failure
bool read_file(const std::string& path, std::string& out) {
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return !f.bad();
    } catch (const std::exception&) {
        return false;
    }
}

uint64_t key_of(uint32_t number, uint8_t variant) { return (static_cast<uint64_t>(number) << 8) | variant; }

}  // namespace

const Picture* Pack::find(uint32_t number, uint8_t variant) const noexcept {
    auto it = index_.find(key_of(number, variant));
    if (it == index_.end() && variant != 0) it = index_.find(key_of(number, 0));
    return it == index_.end() ? nullptr : &pictures_[it->second];
}

const std::string& Pack::sheet_file(int32_t page, int32_t scale) const {
    static const std::string none;
    return in_range(page, scale) ? sheets_[index(page, scale)].file : none;
}

bool Pack::parse(const std::string& manifest) {
    static uint64_t next_generation = 0;
    *this = Pack{};
    const auto fail = [this](const std::string& why) {
        what_ = why;
        pictures_.clear();
        index_.clear();
        loaded_ = false;
        return false;
    };
    std::istringstream in(manifest);
    std::string line;
    int32_t line_no = 0;
    bool header = false;
    int32_t sheet_rows = 0;
    bool sorted_ok = true;
    uint64_t last_key = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') return fail("line " + std::to_string(line_no) + ": the manifest has CR LF line ends");
        if (line.empty()) continue;
        const std::vector<std::string> w = words_of(line);
        const std::string where = "line " + std::to_string(line_no) + ": ";
        if (w.empty()) continue;
        if (!header) {
            if (w.size() != 2 || w[0] != "ants-world-art" || w[1] != "2") return fail(where + "the first record must be \"ants-world-art 2\"");
            header = true;
            continue;
        }
        if (w[0] == "sheet") {
            if (!pictures_.empty()) return fail(where + "the sheet records come before the sprites");
            int64_t page = 0, scale = 0, width = 0, height = 0;
            if (w.size() != 6 || !to_int(w[1], page) || !to_int(w[2], scale) || !to_int(w[4], width) || !to_int(w[5], height)) return fail(where + "a sheet record is: sheet <page> <scale> <png file> <width> <height>");
            if (page != sheet_rows / 3 || scale != sheet_rows % 3 + 1 || page >= kMaxPages) return fail(where + "the sheet records are for the pages in order and, in each page, for scale 1, 2 and 3 in that order, once each");
            if (width < 1 || height < 1 || width > kMaxSheet || height > kMaxSheet) return fail(where + "a sheet's size is out of range");
            if (w[3].find('/') != std::string::npos || w[3].find('\\') != std::string::npos || w[3].find("..") != std::string::npos || w[3] == "." || w[3].empty()) return fail(where + "a sheet's file must be a name in the folder");
            Sheet& sheet = sheets_[static_cast<size_t>(sheet_rows)];
            sheet.file = w[3];
            sheet.w = static_cast<int32_t>(width);
            sheet.h = static_cast<int32_t>(height);
            ++sheet_rows;
            pages_ = (sheet_rows + 2) / 3;
            continue;
        }
        if (w[0] == "sprite") {
            if (sheet_rows == 0 || sheet_rows % 3 != 0) return fail(where + "the three sheet records of each page come before the sprites");
            int64_t v[11] = {};
            if (w.size() != 12) return fail(where + "a sprite record is: sprite <number> <variant> <page> <w> <h> <x1> <y1> <x2> <y2> <x3> <y3>");
            for (size_t k = 0; k < 11; ++k) {
                if (!to_int(w[k + 1], v[k])) return fail(where + "a sprite record holds whole numbers only");
            }
            if (v[0] < 0 || v[0] > static_cast<int64_t>(kMaxNumber)) return fail(where + "a sprite number is out of range");
            if (v[1] < 0 || v[1] >= kVariants) return fail(where + "a variant is 0 to 15");
            if (v[2] < 0 || v[2] >= pages_) return fail(where + "a sprite's page is not one of the manifest's");
            if (v[3] < 1 || v[4] < 1 || v[3] > kMaxSprite || v[4] > kMaxSprite) return fail(where + "a sprite's size is out of range");
            Picture p;
            p.number = static_cast<uint32_t>(v[0]);
            p.variant = static_cast<uint8_t>(v[1]);
            p.page = static_cast<uint8_t>(v[2]);
            p.w = static_cast<int32_t>(v[3]);
            p.h = static_cast<int32_t>(v[4]);
            for (int32_t s = 1; s <= 3; ++s) {
                const int64_t x = v[5 + (s - 1) * 2];
                const int64_t y = v[6 + (s - 1) * 2];
                const Sheet& sheet = sheets_[index(p.page, s)];
                // the picture and its one-pixel ring lie inside the sheet
                if (x < 1 || y < 1 || x + static_cast<int64_t>(p.w) * s + 1 > sheet.w || y + static_cast<int64_t>(p.h) * s + 1 > sheet.h) {
                    return fail(where + "the picture of sprite " + w[1] + " does not lie inside the sheet of its page at scale " + std::to_string(s) + " with its ring");
                }
                p.x[static_cast<size_t>(s - 1)] = static_cast<int32_t>(x);
                p.y[static_cast<size_t>(s - 1)] = static_cast<int32_t>(y);
            }
            const uint64_t key = key_of(p.number, p.variant);
            if (!pictures_.empty() && key <= last_key) sorted_ok = false;
            if (index_.count(key) != 0) return fail(where + "sprite " + w[1] + " variant " + w[2] + " is there twice");
            last_key = key;
            index_[key] = pictures_.size();
            pictures_.push_back(p);
            continue;
        }
        return fail(where + "a record is \"sheet\" or \"sprite\"");
    }
    if (!header) return fail("the manifest is empty");
    if (sheet_rows == 0 || sheet_rows % 3 != 0) return fail("the manifest lacks a sheet record");
    if (!sorted_ok) return fail("the sprite records are not sorted by number and variant");
    generation_ = ++next_generation;
    loaded_ = true;
    return true;
}

bool Pack::load(const std::string& folder) {
    std::string dir = folder;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    std::string text;
    if (!read_file(dir + "world_art.txt", text)) {
        *this = Pack{};
        what_ = "no " + dir + "world_art.txt";
        return false;
    }
    if (!parse(text)) return false;
    folder_ = dir;
    // The sheets are decoded later (when a texture needs one), but they must be there and be the size that the manifest states: the header of each file is read now, so that a folder that lacks a sheet, or has
    // another one, is the original's art from the start and not a ground that is missing
    for (int32_t page = 0; page < pages_; ++page) {
        for (const int32_t scale : kScales) {
            const Sheet& sheet = sheets_[index(page, scale)];
            std::ifstream in(dir + sheet.file, std::ios::binary);
            uint8_t head[33] = {};
            in.read(reinterpret_cast<char*>(head), sizeof(head));
            const bool png = in.gcount() == static_cast<std::streamsize>(sizeof(head)) && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp(head + 12, "IHDR", 4) == 0;
            const auto be32 = [&head](size_t at) { return (static_cast<int64_t>(head[at]) << 24) | (static_cast<int64_t>(head[at + 1]) << 16) | (static_cast<int64_t>(head[at + 2]) << 8) | head[at + 3]; };
            if (!png || be32(16) != sheet.w || be32(20) != sheet.h || head[24] != 8 || head[25] != 6) {
                const std::string why = "the sheet " + dir + sheet.file + " is missing, or is not a " + std::to_string(sheet.w) + " x " + std::to_string(sheet.h) + " picture of 8-bit RGBA";
                *this = Pack{};
                what_ = why;
                return false;
            }
        }
    }
    return true;
}

bool Pack::sheet_pixels(int32_t page, int32_t scale, DecodedImage& out) const {
    out = DecodedImage{};
    const std::string& file = sheet_file(page, scale);
    if (!loaded_ || file.empty()) return false;
    std::string bytes;
    if (!read_file(folder_ + file, bytes)) return false;
    if (!decode_png_rgba(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out)) return false;
    if (out.width != sheet_width(page, scale) || out.height != sheet_height(page, scale)) {
        out = DecodedImage{};
        return false;
    }
    return true;
}

// ============================================================================
// The picture of a ground tile
// ============================================================================

namespace {

// A hash of a cell and a seed; the arithmetic of the approved pictures
uint32_t h32(uint32_t a, uint32_t b, uint32_t s) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ s * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// 3t^2 - 2t^3 of a fraction t in 1/65536 units, in 1/65536 units
uint32_t smooth16(uint32_t t) {
    const uint64_t tt = static_cast<uint64_t>(t) * t;
    return static_cast<uint32_t>((tt * (3u * 65536u - 2u * t)) >> 32);
}

// A slow field over the map, `size` cells to a step: areas, not single tiles. A value in 0 .. 65535 (smoothly interpolated between the hashes of the four lattice points round the cell)
uint32_t patch16(int32_t x, int32_t y, uint32_t seed, int32_t size) {
    const int32_t ix = x / size, iy = y / size;
    const uint32_t tx = smooth16(static_cast<uint32_t>((x % size) * 65536 / size));
    const uint32_t ty = smooth16(static_cast<uint32_t>((y % size) * 65536 / size));
    const auto v = [&](int32_t a, int32_t b) { return static_cast<uint64_t>(h32(static_cast<uint32_t>(a), static_cast<uint32_t>(b), seed) & 0xFFFFu); };
    const uint64_t top = (v(ix, iy) * (65536u - tx) + v(ix + 1, iy) * tx) >> 16;
    const uint64_t bottom = (v(ix, iy + 1) * (65536u - tx) + v(ix + 1, iy + 1) * tx) >> 16;
    return static_cast<uint32_t>((top * (65536u - ty) + bottom * ty) >> 16);
}

}  // namespace

std::vector<uint8_t> ground_variants(const GroundMap& map) {
    const int32_t W = map.width, H = map.height;
    std::vector<uint8_t> out;
    if (W <= 0 || H <= 0) return out;
    const size_t n = static_cast<size_t>(W) * static_cast<size_t>(H);
    out.assign(n, 0);
    if (map.kind.size() != n || map.plain.size() != n || map.object.size() != n) return out;
    const auto at = [W](int32_t x, int32_t y) { return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x); };
    const auto same = [&](int32_t x, int32_t y, char k) { return x >= 0 && y >= 0 && x < W && y < H && map.plain[at(x, y)] != 0 && map.kind[at(x, y)] == k; };

    // how many cells it is to the edge of the cell's own plain ground (1: beside an edge tile or another kind), at most 9. The map's border is not an edge.
    std::vector<uint8_t> dist(n, 0);
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            if (map.plain[at(x, y)] == 0) continue;
            uint8_t d = 9;
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    const int32_t xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    if (!same(xx, yy, map.kind[at(x, y)])) d = 1;
                }
            }
            dist[at(x, y)] = d;
        }
    }
    for (int32_t pass = 0; pass < 8; ++pass) {
        for (int32_t y = 0; y < H; ++y) {
            for (int32_t x = 0; x < W; ++x) {
                if (map.plain[at(x, y)] == 0) continue;
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        if (same(x + dx, y + dy, map.kind[at(x, y)]) && dist[at(x + dx, y + dy)] + 1 < dist[at(x, y)]) dist[at(x, y)] = static_cast<uint8_t>(dist[at(x + dx, y + dy)] + 1);
                    }
                }
            }
        }
    }

    // the fractions of the rule as numbers of a value in 0 .. 65535: "u < k07" is "below 0.07 of the range", "p > kHigh" is "above 0.66"
    constexpr uint32_t k07 = 4588, k50 = 32768, k55 = 36045, k60 = 39321, k65 = 42598, k70 = 45875;     // 0.07, 0.50, 0.55, 0.60, 0.65, 0.70
    constexpr uint32_t kHigh = 43253, kLow = 19661, kCracked = 47185;                                      // 0.66 (above), 0.30 (below), 0.72 (above)
    static constexpr int32_t kBase[5] = {0, 6, 9, 12, 14};
    static constexpr uint32_t kCount[5] = {6, 3, 3, 2, 2};
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            if (map.plain[at(x, y)] == 0) continue;
            const char k = map.kind[at(x, y)];
            int32_t widest = 0;
            bool near_object = false;
            for (int32_t dy = -2; dy <= 2; ++dy) {
                for (int32_t dx = -2; dx <= 2; ++dx) {
                    if (same(x + dx, y + dy, k)) widest = std::max<int32_t>(widest, dist[at(x + dx, y + dy)]);
                    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && x + dx >= 0 && y + dy >= 0 && x + dx < W && y + dy < H && map.object[at(x + dx, y + dy)] != 0) near_object = true;
                }
            }
            const uint32_t r = h32(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 7u);
            const uint32_t u = r & 0xFFFFu;
            const uint32_t p = patch16(x, y, 11u, 5);
            const uint32_t q = patch16(x, y, 23u, 7);
            int32_t cls;
            if (dist[at(x, y)] == 1) cls = u < k65 ? 2 : 1;                         // beside another kind: stones gather, the ground is damp
            else if (widest <= 2) cls = u < k55 ? 3 : 4;                            // a corridor or a pocket: worn
            else if (near_object && u < k50) cls = 1;                               // scuffed round the things that stand on it
            else if (p > kHigh) cls = u < k70 ? 1 : 0;                              // a stonier patch
            else if (p < kLow) cls = u < k70 ? 3 : 0;                               // a pale, dusty patch
            else if (q > kCracked) cls = u < k60 ? 4 : 0;                           // a cracked, darker streak
            else cls = u < k07 ? 1 : 0;                                             // the open plain: calm
            out[at(x, y)] = static_cast<uint8_t>(kBase[cls] + static_cast<int32_t>((r >> 16) % kCount[cls]));
        }
    }
    return out;
}

}  // namespace ants::app::world_art

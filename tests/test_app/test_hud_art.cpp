// The HUD art, the model's part (include/ants_app/hud_art.hpp, png_decode.hpp, src/ants_app/hud_art.cpp; docs/VIEW_AND_HUD.md, "HUD art"): the file reader, the manifest, where every piece stands
// at every shape of the picture, the nine-slice (a long piece repeats its plain run), the choice of the sheet by the output's scale, and the setting. The cases that draw are in test_hud_art_look.cpp.
//   [png]       the reader decodes every filter, fixed and dynamic blocks and the shipped sheets, and refuses what is not an 8-bit RGBA file or is damaged
//   [manifest]  the shipped manifest is read; a manifest that is wrong in any of the ways listed is refused; every animation names a sprite, and every animation the game plays has one
//   [frame]     at all seven shapes the frame's pieces tile the picture but the map view, once; every piece of every animation is inside the canvas; the buttons are the hit zones' size and place
//   [slices]    a long piece is the kept start, the repeated run (the last copy cut) and the kept end, whatever the length
//   [scale]     the sheet is the nearest scale at or above the output's, and the renderer takes it
//   [unusable]  a sheet that cannot be decoded, or that the device cannot make a texture, is said by the renderer and draws nothing (the application then gives the HUD the original's art: test_game_menu.cpp)
//   [look] [classic] [pieces]   (test_hud_art_look.cpp) the real renderer's frame against the reference pictures, the original's art unchanged, the pieces that change with what happens
//   [cursors] [cursor-draw] [cursor-classic] [cursor-damaged]   (test_hud_art_cursors.cpp) the new pointers: the original's click points, times and sizes, drawn at 1x, 2x and 3x, the original's when the art does not fit
// (the setting that chooses between this art and the original's is a row of the settings menu: its cases are in test_game_menu.cpp)
// Usage: test_hud_art. Exit code 0 when every check passes.
#include "hud_art_fixtures.hpp"

#include "ants_app/config_store.hpp"
#include "ants_app/game_menu.hpp"

namespace gm = ants::app::game_menu;

namespace {

// ---- a PNG writer for the reader's cases: stored deflate blocks, the filter of each row as asked -------------------------------------------------------------------------------

uint32_t crc32_of(const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xffu] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body) {
    put32(out, static_cast<uint32_t>(body.size()));
    std::vector<uint8_t> tb(type, type + 4);
    tb.insert(tb.end(), body.begin(), body.end());
    out.insert(out.end(), tb.begin(), tb.end());
    put32(out, crc32_of(tb.data(), tb.size()));
}

int paeth_of(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

/// The filtered rows of an RGBA picture, row y with filter `filters[y % filters.size()]` (the forward filters of RFC 2083)
std::vector<uint8_t> filtered(const std::vector<uint8_t>& rgba, int w, int h, const std::vector<int>& filters) {
    std::vector<uint8_t> out;
    const size_t stride = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        const int f = filters[static_cast<size_t>(y) % filters.size()];
        out.push_back(static_cast<uint8_t>(f));
        for (size_t i = 0; i < stride; ++i) {
            const int x = rgba[static_cast<size_t>(y) * stride + i];
            const int a = i >= 4 ? rgba[static_cast<size_t>(y) * stride + i - 4] : 0;
            const int b = y > 0 ? rgba[static_cast<size_t>(y - 1) * stride + i] : 0;
            const int c = (y > 0 && i >= 4) ? rgba[static_cast<size_t>(y - 1) * stride + i - 4] : 0;
            int predicted = 0;
            if (f == 1) predicted = a;
            else if (f == 2) predicted = b;
            else if (f == 3) predicted = (a + b) / 2;
            else if (f == 4) predicted = paeth_of(a, b, c);
            out.push_back(static_cast<uint8_t>((x - predicted) & 0xff));
        }
    }
    return out;
}

std::vector<uint8_t> png_of(int w, int h, const std::vector<uint8_t>& raw, uint8_t color_type = 6, uint8_t interlace = 0) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(w));
    put32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, color_type, 0, 0, interlace});
    chunk(out, "IHDR", ihdr);
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size() || at == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xff));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xff));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) break;
    }
    uint32_t s1 = 1, s2 = 0;
    for (const uint8_t v : raw) {
        s1 = (s1 + v) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    put32(z, (s2 << 16) | s1);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

std::vector<uint8_t> noise_picture(int w, int h, uint32_t seed) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    uint32_t s = seed;
    for (uint8_t& v : px) {
        s = s * 1664525u + 1013904223u;
        v = static_cast<uint8_t>(s >> 24);
    }
    return px;
}

/// What tools/hud_art's encoder (Pillow's) writes: the same picture as the formula of the two literals below, `pixel(x, y)`
std::array<uint8_t, 4> formula_pixel(int x, int y) {
    return {static_cast<uint8_t>((x * 13 + y * 7) & 255), static_cast<uint8_t>((x * y) & 255), static_cast<uint8_t>(((x ^ y) * 9) & 255), static_cast<uint8_t>(255 - ((x + y) & 127))};
}

// zlib's fixed-Huffman block (8 x 6, Sub filter on every row) and its Huffman-only dynamic block (10 x 8, no filter), made by zlib itself: the two kinds of compressed block that the sheets' own
// encoder may write besides the matches of its default strategy, which the shipped sheets (read below) hold
const uint8_t kFixedPng[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x06,0x08,0x06,0x00,0x00,0x00,0xfe,0x05,0xdf,0xfb,0x00,0x00,0x00,0x62,0x49,0x44,0x41,0x54,0x78,0x01,0x63,0x64,0x60,0x60,0xf8,0xcf,0xcb,0xc0,0x89,0x13,0x33,0xb2,0x33,0x70,0xfe,0xe3,0x65,0xfc,0xfe,0x9f,0x97,0x51,0xfa,0x3f,0x36,0x9a,0x91,0x8f,0x41,0xe8,0x2f,0x2f,0x13,0x50,0x35,0xd3,0xd3,0xff,0x10,0x5a,0xf7,0x3f,0x32,0x9f,0x51,0x94,0x41,0xfa,0x0f,0x2f,0x33,0x50,0x07,0x1c,0xdb,0xff,0x47,0xe6,0x33,0xca,0x30,0xa8,0xfc,0xe6,0x65,0x01,0xea,0x80,0xe3,0x83,0xff,0x91,0xf9,0x8c,0xca,0x0c,0xba,0xbf,0x78,0x59,0x81,0xaa,0x59,0x81,0x76,0x82,0xe9,0xcb,0xff,0x91,0xf9,0x00,0x37,0x2e,0x45,0xaf,0x0b,0x42,0x5b,0x3b,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
const uint8_t kDynamicPng[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x08,0x08,0x06,0x00,0x00,0x00,0xc0,0xfa,0x6e,0xb6,0x00,0x00,0x01,0x43,0x49,0x44,0x41,0x54,0x78,0x01,0x05,0xc1,0xc9,0x2f,0x43,0x41,0x00,0x07,0xe0,0x1f,0xad,0xb6,0xd3,0x54,0xc9,0xa4,0x4d,0xca,0x34,0x0d,0x3a,0x09,0x9a,0x0c,0x9a,0x30,0x24,0x96,0x89,0xe5,0x50,0x24,0xe8,0xc1,0x12,0xdb,0x41,0x49,0x2c,0x4d,0x2c,0x49,0x71,0xa0,0x0e,0x48,0x13,0x5c,0x38,0x71,0x72,0xf3,0xef,0xd1,0xbe,0xfd,0xbd,0x16,0xdf,0x07,0x00,0x7f,0xad,0x20,0xbf,0x9d,0xa0,0x8d,0x5e,0xb0,0xfa,0x30,0xb8,0x37,0x0d,0xe1,0x2e,0x41,0x3a,0x5b,0x50,0xf6,0x21,0x72,0x56,0x09,0x79,0x13,0x41,0x90,0xdf,0x58,0x13,0x1a,0xdd,0xcd,0xac,0x3e,0xe0,0xa3,0xde,0xb8,0x5f,0xb8,0xb9,0x16,0xee,0xac,0x06,0x94,0xbd,0x17,0x94,0xd6,0x69,0x28,0x6f,0xde,0x90,0x9c,0x81,0x28,0x68,0x83,0x35,0xb3,0x7a,0x9f,0x1f,0xde,0x48,0x80,0xb8,0x33,0x21,0xe9,0x2c,0x87,0x95,0xbd,0x1d,0xe1,0xd6,0x51,0x54,0x98,0x97,0xed,0x9b,0xc6,0x3d,0x2d,0xe8,0x88,0x83,0xd5,0x7b,0x7c,0xd4,0x1b,0x0c,0x10,0x77,0x82,0xc0,0x99,0x8f,0x28,0x7b,0xad,0x4d,0x5a,0x05,0x2a,0xcc,0xb3,0x38,0x37,0x6e,0x13,0x05,0xfd,0x89,0x6d,0x6a,0x48,0x82,0x7b,0xfd,0x7e,0xe1,0xca,0x90,0x74,0x66,0x23,0xca,0x5e,0x69,0x87,0xb5,0x13,0x23,0xe6,0x71,0x82,0x1a,0x57,0x49,0xa6,0x3f,0x74,0x15,0xb5,0x57,0x5e,0xaa,0x21,0x0d,0xe1,0x0e,0xb5,0x70,0x67,0x32,0xac,0xec,0x85,0x36,0x69,0xad,0xc7,0x88,0xb9,0xdf,0x01,0xe3,0x3c,0xc5,0xf4,0x72,0x9a,0x6a,0xcf,0x7d,0xa5,0xda,0xbb,0x28,0x56,0x91,0x81,0x74,0x46,0x03,0xca,0x9e,0x8b,0x70,0x2b,0x4f,0x85,0xb9,0x9b,0xa0,0xc6,0x49,0x8a,0xe9,0xd7,0x1c,0xda,0x63,0x86,0xd4,0xde,0x86,0xca,0xd5,0x4f,0x59,0xf9,0x41,0x16,0xca,0x9e,0x0a,0x4a,0x6b,0x31,0x2a,0xcc,0x8d,0x38,0x37,0x0e,0x92,0x4c,0xbf,0x48,0x53,0xed,0x2e,0x43,0x6a,0x2f,0x59,0x54,0x3f,0xc6,0x2a,0x3f,0x5f,0xaa,0xfc,0xfd,0x0f,0xfe,0x97,0x7a,0x65,0x7e,0xa9,0x95,0x65,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};

bool same_picture(const DecodedImage& got, int w, int h, const std::vector<uint8_t>& want) {
    return got.width == w && got.height == h && got.rgba == want;
}

void test_png() {
    group("png", "the reader: every row filter, fixed and dynamic blocks, the shipped sheets; what is not an 8-bit RGBA file, or is damaged, is refused");
    // every filter, on a picture of noise (stored blocks, as many as the data needs: one picture is longer than a block)
    for (const int w : {1, 2, 7, 33}) {
        for (const int h : {1, 5, 600}) {
            const std::vector<uint8_t> px = noise_picture(w, h, static_cast<uint32_t>(w * 1000 + h));
            const std::vector<uint8_t> bytes = png_of(w, h, filtered(px, w, h, {0, 1, 2, 3, 4, 4, 3, 2, 1}));
            DecodedImage got;
            check(decode_png_rgba(bytes.data(), bytes.size(), got) && same_picture(got, w, h, px), std::to_string(w) + " x " + std::to_string(h) + ": all five filters, one after the other, give the picture back");
        }
    }
    for (int f = 0; f <= 4; ++f) {
        const std::vector<uint8_t> px = noise_picture(9, 4, 77);
        const std::vector<uint8_t> bytes = png_of(9, 4, filtered(px, 9, 4, {f}));
        DecodedImage got;
        check(decode_png_rgba(bytes.data(), bytes.size(), got) && same_picture(got, 9, 4, px), "filter " + std::to_string(f) + " on every row");
    }
    // blocks that zlib compressed (a fixed table; a dynamic one with literals only)
    for (const int which : {0, 1}) {
        const uint8_t* data = which == 0 ? kFixedPng : kDynamicPng;
        const size_t size = which == 0 ? sizeof(kFixedPng) : sizeof(kDynamicPng);
        const int w = which == 0 ? 8 : 10, h = which == 0 ? 6 : 8;
        DecodedImage got;
        bool ok = decode_png_rgba(data, size, got) && got.width == w && got.height == h;
        for (int y = 0; ok && y < h; ++y) {
            for (int x = 0; ok && x < w; ++x) {
                const std::array<uint8_t, 4> want = formula_pixel(x, y);
                for (size_t c = 0; c < 4; ++c) ok = ok && got.rgba[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4 + c] == want[c];
            }
        }
        check(ok, which == 0 ? "a fixed-table block (Sub filter) is the picture of its formula" : "a dynamic-table block is the picture of its formula");
    }
    // refusals
    {
        const std::vector<uint8_t> px = noise_picture(6, 6, 5);
        const std::vector<uint8_t> good = png_of(6, 6, filtered(px, 6, 6, {0}));
        DecodedImage got;
        check(decode_png_rgba(good.data(), good.size(), got), "the picture that the refusals start from is read");
        for (size_t cut : {size_t{0}, size_t{7}, size_t{20}, good.size() / 2, good.size() - 14, good.size() - 30}) {
            check(!decode_png_rgba(good.data(), cut, got) && got.rgba.empty(), "cut to " + std::to_string(cut) + " bytes: refused, nothing left");
        }
        std::vector<uint8_t> bad = good;
        bad[1] = 'X';
        check(!decode_png_rgba(bad.data(), bad.size(), got), "not a PNG signature");
        const std::vector<uint8_t> rgb = png_of(6, 6, filtered(px, 6, 6, {0}), 2);
        check(!decode_png_rgba(rgb.data(), rgb.size(), got), "RGB (color type 2): refused");
        const std::vector<uint8_t> interlaced = png_of(6, 6, filtered(px, 6, 6, {0}), 6, 1);
        check(!decode_png_rgba(interlaced.data(), interlaced.size(), got), "interlaced: refused");
        std::vector<uint8_t> raw = filtered(px, 6, 6, {0});
        raw[0] = 9;                                                      // a filter type that does not exist
        const std::vector<uint8_t> odd = png_of(6, 6, raw);
        check(!decode_png_rgba(odd.data(), odd.size(), got) && got.rgba.empty(), "a filter type of 9: refused");
        const std::vector<uint8_t> short_data = png_of(6, 6, std::vector<uint8_t>(raw.begin(), raw.begin() + 40));
        check(!decode_png_rgba(short_data.data(), short_data.size(), got), "fewer rows than the header says: refused");
        const std::vector<uint8_t> long_data = png_of(6, 5, filtered(px, 6, 6, {0}));
        check(!decode_png_rgba(long_data.data(), long_data.size(), got), "more rows than the header says: refused");
        check(!decode_png_rgba(nullptr, 0, got), "nothing at all: refused");
        std::vector<uint8_t> garbage = good;
        for (size_t i = 60; i < garbage.size() - 20; ++i) garbage[i] ^= 0x5a;
        check(!decode_png_rgba(garbage.data(), garbage.size(), got) || got.rgba != px, "a damaged stream never gives the picture back");
        std::vector<uint8_t> out;
        const uint8_t block_type_3[] = {0x07};
        check(!inflate_raw(block_type_3, sizeof(block_type_3), out, 100), "a block of the type that does not exist is refused");
        const uint8_t stored_five[] = {0x01, 0x05, 0x00, 0xfa, 0xff, 1, 2, 3, 4, 5};
        check(inflate_raw(stored_five, sizeof(stored_five), out, 5) && out.size() == 5 && !inflate_raw(stored_five, sizeof(stored_five), out, 4), "a stored block, and the limit on what may come out of it");
    }
    // a folder of the art: the sheets are looked at when the manifest is read, so that a folder that lacks one, or has another, is refused at the start (the original's art is then drawn)
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("ants_hud_art_dir_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
        std::error_code ignored;
        fs::create_directories(dir, ignored);
        fs::copy_file(art_folder() + "/hud_art.txt", dir / "hud_art.txt", fs::copy_options::overwrite_existing, ignored);
        hud_art::Art loaded;
        check(!loaded.load(dir.string()) && loaded.what().find("sheet_1x.png") != std::string::npos && !loaded.loaded(), "a manifest without its sheets is refused, and names the first that is missing");
        for (const char* name : {"sheet_1x.png", "sheet_2x.png", "sheet_3x.png"}) fs::copy_file(art_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
        check(loaded.load(dir.string()) && loaded.loaded() && loaded.folder().find("ants_hud_art_dir_") != std::string::npos, "with its three sheets it is read");
        const std::vector<uint8_t> small = png_of(4, 4, filtered(noise_picture(4, 4, 3), 4, 4, {0}));
        {
            std::ofstream out(dir / "sheet_2x.png", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(small.data()), static_cast<std::streamsize>(small.size()));
        }
        check(!loaded.load(dir.string()) && loaded.what().find("sheet_2x.png") != std::string::npos, "a sheet that is another size is refused, and named");
        {
            const std::vector<uint8_t> rgb = png_of(4, 4, std::vector<uint8_t>(4 * 13, 0), 2);
            std::ofstream out(dir / "sheet_2x.png", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        }
        check(!loaded.load(dir.string()), "a sheet that is not RGBA is refused");
        {
            std::ofstream out(dir / "sheet_2x.png", std::ios::binary | std::ios::trunc);
            out << "this is not a picture";
        }
        check(!loaded.load(dir.string()), "a sheet that is no picture is refused");
        // a folder where a file should be (the standard library of Linux throws when it reads one): a refusal, never a throw
        fs::remove(dir / "sheet_2x.png", ignored);
        fs::create_directories(dir / "sheet_2x.png", ignored);
        bool threw = false;
        bool refused_folder = false;
        try {
            refused_folder = !loaded.load(dir.string());
        } catch (...) {
            threw = true;
        }
        check(!threw && refused_folder, "a sheet that is a folder is refused, and nothing is thrown");
        fs::remove_all(dir / "sheet_2x.png", ignored);
        fs::copy_file(art_folder() + "/sheet_2x.png", dir / "sheet_2x.png", fs::copy_options::overwrite_existing, ignored);
        check(loaded.load(dir.string()), "(the folder is whole again)");
        fs::remove(dir / "sheet_3x.png", ignored);
        fs::create_directories(dir / "sheet_3x.png", ignored);                 // (after the manifest was read: the sheet is read when a texture needs it)
        DecodedImage never;
        threw = false;
        bool decoded = true;
        try {
            decoded = loaded.sheet_pixels(3, never);
        } catch (...) {
            threw = true;
        }
        check(!threw && !decoded && never.rgba.empty(), "a sheet that became a folder reads as a failure, and nothing is thrown");
        fs::remove_all(dir / "sheet_3x.png", ignored);
        fs::remove(dir / "hud_art.txt", ignored);
        fs::create_directories(dir / "hud_art.txt", ignored);
        threw = false;
        refused_folder = false;
        try {
            refused_folder = !loaded.load(dir.string());
        } catch (...) {
            threw = true;
        }
        check(!threw && refused_folder && !loaded.loaded(), "a manifest that is a folder is refused, and nothing is thrown");
        fs::remove_all(dir, ignored);
    }
    // the shipped sheets: every scale decodes to the size of the 1x sheet times the scale
    hud_art::Art art;
    if (load_art(art)) {
        for (const int32_t k : hud_art::Art::kScales) {
            DecodedImage sheet;
            check(art.sheet_pixels(k, sheet) && sheet.width == art.sheet_width() * k && sheet.height == art.sheet_height() * k, "the sheet of " + std::to_string(k) + "x decodes to " + std::to_string(art.sheet_width() * k) + " x " + std::to_string(art.sheet_height() * k));
            // the sheets are one drawing at three sizes: the average color of the 3x sheet is the 1x sheet's (within the rounding of the downscale)
            DecodedImage one;
            if (k > 1 && art.sheet_pixels(1, one)) {
                const auto mean = [](const DecodedImage& im) {
                    double sum = 0;
                    for (size_t i = 0; i < im.rgba.size(); i += 4) sum += im.rgba[i + 1] * (im.rgba[i + 3] / 255.0);
                    return sum / (static_cast<double>(im.width) * im.height);
                };
                check(std::fabs(mean(one) - mean(sheet)) < 3.0, "the sheet of " + std::to_string(k) + "x is the 1x drawing larger (the mean green agrees)");
            }
        }
    }
}

/// A whole file of the repository
std::vector<uint8_t> file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// One channel of one pixel of the 2x picture that a 3x picture gives, in floating point (the weights of derive_two_thirds, written out): the premultiplied average of the 3 x 3 block over 3 / 2 pixels
double derived_channel(const DecodedImage& three, int x, int y, int c) {
    static const int weight[2][3] = {{2, 1, 0}, {0, 1, 2}};
    double cover = 0, sum = 0;
    for (int sy = 0; sy < 3; ++sy) {
        for (int sx = 0; sx < 3; ++sx) {
            const double wgt = weight[y % 2][sy] * weight[x % 2][sx];
            const size_t at = (static_cast<size_t>(y / 2 * 3 + sy) * static_cast<size_t>(three.width) + static_cast<size_t>(x / 2 * 3 + sx)) * 4;
            cover += wgt * three.rgba[at + 3];
            sum += wgt * three.rgba[at + 3] * (c == 3 ? 255 : three.rgba[at + static_cast<size_t>(c)]);
        }
    }
    if (c == 3) return cover / 9.0;
    return cover > 0 ? sum / cover : 0.0;
}

void test_derive() {
    group("derive", "the first rows of a picture; the 2x rows that the game makes from the 3x sheet (the weights, see-through pixels, the shipped sheets)");
    {   // the first rows of a PNG: from the start of its data only
        const int w = 37, h = 200;
        const std::vector<uint8_t> px = noise_picture(w, h, 4242);
        const std::vector<uint8_t> bytes = png_of(w, h, filtered(px, w, h, {0, 1, 2, 3, 4}));
        const size_t row = static_cast<size_t>(w) * 4;
        DecodedImage part;
        check(decode_png_rgba(bytes.data(), bytes.size(), part, 31) && part.width == w && part.height == 31 && part.rgba.size() == 31 * row && std::equal(part.rgba.begin(), part.rgba.end(), px.begin()), "the first 31 rows are the first 31 rows of the picture");
        check(decode_png_rgba(bytes.data(), bytes.size(), part, 1) && part.height == 1 && std::equal(part.rgba.begin(), part.rgba.end(), px.begin()), "one row");
        check(decode_png_rgba(bytes.data(), bytes.size(), part, h) && same_picture(part, w, h, px), "all the rows, asked for by number");
        check(decode_png_rgba(bytes.data(), bytes.size(), part, 0) && same_picture(part, w, h, px), "no number is all the rows");
        check(!decode_png_rgba(bytes.data(), bytes.size(), part, h + 1) && part.rgba.empty(), "more rows than the picture has: refused");
        check(!decode_png_rgba(bytes.data(), bytes.size(), part, -1), "a negative number of rows: refused");
        std::vector<uint8_t> out;
        const uint8_t stored_five[] = {0x01, 0x05, 0x00, 0xfa, 0xff, 1, 2, 3, 4, 5};
        check(inflate_raw_prefix(stored_five, sizeof(stored_five), out, 3) && out.size() >= 3 && out[0] == 1 && out[2] == 3 && inflate_raw_prefix(stored_five, sizeof(stored_five), out, 5) && !inflate_raw_prefix(stored_five, sizeof(stored_five), out, 6), "the start of a stream: what it holds, and not more than it holds");
    }
    {   // the arithmetic: a block of 3 x 3 gives 2 x 2, weights (2, 1, 0) and (0, 1, 2) each way, rounded half up
        DecodedImage three;
        three.width = 3;
        three.height = 3;
        three.rgba.assign(36, 0);
        for (int i = 0; i < 9; ++i) {
            const size_t at = static_cast<size_t>(i) * 4;
            three.rgba[at] = static_cast<uint8_t>(10 * i);
            three.rgba[at + 1] = 200;
            three.rgba[at + 2] = static_cast<uint8_t>(7 * i);
            three.rgba[at + 3] = 255;
        }
        DecodedImage two;
        check(hud_art::derive_two_thirds(three, two) && two.width == 2 && two.height == 2 && two.rgba.size() == 16, "a block of 3 x 3 gives 2 x 2");
        const int red[4] = {13, 27, 53, 67};                         // (4 * 0 + 2 * 10 + 2 * 30 + 40) / 9 = 13.3 and so on, worked out by hand
        const int blue[4] = {9, 19, 37, 47};
        bool ok = two.rgba.size() == 16;
        for (size_t i = 0; ok && i < 4; ++i) ok = two.rgba[i * 4] == red[i] && two.rgba[i * 4 + 1] == 200 && two.rgba[i * 4 + 2] == blue[i] && two.rgba[i * 4 + 3] == 255;
        check(ok, "the four pixels are the weighted averages of the block, to the level (13 27 53 67 in red)");
        for (int i = 0; i < 9; ++i) three.rgba[static_cast<size_t>(i) * 4 + 3] = 128;      // half see-through everywhere, color 50: the color stays 50, alpha stays 128
        for (int i = 0; i < 9; ++i) three.rgba[static_cast<size_t>(i) * 4] = 50;
        check(hud_art::derive_two_thirds(three, two) && two.rgba[0] == 50 && two.rgba[3] == 128 && two.rgba[15] == 128 && two.rgba[12] == 50, "a see-through pixel keeps its alpha and its color");
        for (int i = 0; i < 9; ++i) {
            const size_t at = static_cast<size_t>(i) * 4;
            three.rgba[at] = three.rgba[at + 1] = three.rgba[at + 2] = 100;
            three.rgba[at + 3] = 255;
        }
        three.rgba[0] = 250;                                           // the corner pixel (weight 4 for the first output pixel) is clear: its color counts for nothing
        three.rgba[3] = 0;
        check(hud_art::derive_two_thirds(three, two) && two.rgba[0] == 100 && two.rgba[1] == 100 && two.rgba[3] == 142 && two.rgba[4] == 100 && two.rgba[7] == 255 && two.rgba[15] == 255, "a clear pixel adds nothing to the color, and takes its share of the alpha (5 of 9)");
        DecodedImage six;
        six.width = 6;
        six.height = 3;
        six.rgba.assign(72, 255);
        for (size_t i = 0; i < 9; ++i) six.rgba[(i / 3 * 6 + i % 3) * 4] = 0;       // the first block red 0, the second 255
        check(hud_art::derive_two_thirds(six, two) && two.width == 4 && two.height == 2 && two.rgba[0] == 0 && two.rgba[4] == 0 && two.rgba[8] == 255 && two.rgba[12] == 255, "two blocks do not mix");
        DecodedImage odd;
        odd.width = 4;
        odd.height = 3;
        odd.rgba.assign(48, 0);
        check(!hud_art::derive_two_thirds(odd, two) && two.rgba.empty(), "a picture that is not a whole number of blocks is refused");
        check(!hud_art::derive_two_thirds(DecodedImage{}, two), "an empty picture is refused");
    }
    hud_art::Art art;
    if (!load_art(art)) return;
    const int32_t rows = art.derived_rows();
    check(rows > 0 && rows < art.sheet_height(), "the shipped manifest derives the first rows of the 2x sheet, and not all of them");
    const std::vector<uint8_t> bytes3 = file_bytes(art_folder() + "/" + art.sheet_file(3));
    DecodedImage top3, full3;
    check(decode_png_rgba(bytes3.data(), bytes3.size(), top3, rows * 3) && decode_png_rgba(bytes3.data(), bytes3.size(), full3) && top3.height == rows * 3 &&
              std::equal(top3.rgba.begin(), top3.rgba.end(), full3.rgba.begin()), "the first rows of the 3x sheet (a compressed file) are the first rows of the whole sheet");
    // the 2x file has nothing in those rows: the 2x sheet is smaller by what the 3x sheet already holds
    DecodedImage file2, sheet2;
    const std::vector<uint8_t> bytes2 = file_bytes(art_folder() + "/" + art.sheet_file(2));
    check(decode_png_rgba(bytes2.data(), bytes2.size(), file2) && art.sheet_pixels(2, sheet2) && sheet2.width == file2.width && sheet2.height == file2.height, "the 2x sheet is read, whole");
    const size_t band = static_cast<size_t>(rows) * 2 * static_cast<size_t>(sheet2.width) * 4;
    bool empty = file2.rgba.size() >= band;
    for (size_t i = 0; empty && i < band; ++i) empty = file2.rgba[i] == 0;
    check(empty, "the file of the 2x sheet holds nothing in the derived rows");
    check(file2.rgba.size() == sheet2.rgba.size() && std::equal(file2.rgba.begin() + static_cast<std::ptrdiff_t>(band), file2.rgba.end(), sheet2.rgba.begin() + static_cast<std::ptrdiff_t>(band)), "the rows below are the file's own");
    size_t wrong = 0, seen = 0;
    for (int y = 0; y < rows * 2; y += 3) {
        for (int x = 0; x < sheet2.width; x += 2) {
            for (int c = 0; c < 4; ++c) {
                const double want = derived_channel(top3, x, y, c);
                const double got = sheet2.rgba[(static_cast<size_t>(y) * static_cast<size_t>(sheet2.width) + static_cast<size_t>(x)) * 4 + static_cast<size_t>(c)];
                if (std::fabs(want - got) > 1.0) ++wrong;
                ++seen;
            }
        }
    }
    check(seen > 100000 && wrong == 0, "the derived rows are the premultiplied averages of the 3x rows (every 6th row, every 2nd column, to one level)");
    // where the sprites are: the windows' pictures are in the band, so are the panel's clay and frame pieces; the HUD's own are not
    for (const char* name : {"panel_c", "panel_tl", "panel_t", "dlg_btn_yes_up", "op_on_up", "qh_part_02", "cd_title", "cd_count_well", "cd_row_well", "dlg_btn_fullscreen_up", "qh_pedestal_move"}) {
        const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
        bool shows = false;
        if (s != nullptr && s->sheet_y + s->h <= rows) {
            for (int32_t y = s->sheet_y * 2; !shows && y < (s->sheet_y + s->h) * 2; ++y) {
                for (int32_t x = s->sheet_x * 2; !shows && x < (s->sheet_x + s->w) * 2; ++x) shows = sheet2.rgba[(static_cast<size_t>(y) * static_cast<size_t>(sheet2.width) + static_cast<size_t>(x)) * 4 + 3] > 0;
            }
        }
        check(shows, std::string(name) + ": inside the derived rows, and shows something there at 2x");
    }
    const hud_art::Sprite* top = art.sprite_at(art.find_sprite("top_bar"));
    check(top != nullptr && top->sheet_y >= rows, "the HUD's own pieces are below the derived rows (their 2x pictures are drawn, not derived)");
    // what the web build downloads for the art: the windows' pictures added about 1.4 MB to the 4.65 MB of the HUD's own, the results screen's about 0.5 MB more, and the folder must not grow past what a visitor is asked to wait for
    std::uintmax_t folder_bytes = 0;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(art_folder(), ignored)) folder_bytes += entry.is_regular_file(ignored) ? entry.file_size(ignored) : 0;
    check(folder_bytes > 4000000 && folder_bytes <= 6700000, "the art's folder (sheets, manifest, font) is at most 6.7 MB: " + std::to_string(folder_bytes) + " bytes");
}

}  // namespace

// A second sheet (the first was full): the sprites of `sprite2` are read from its files and drawn from its textures, checked with pixels. The shipped manifest with a second sheet of 16 x 16 canvas pixels (three
// files of one color) and a sprite2 on it, drawn by the real renderer: the pixel that comes out is the second sheet's, never the first sheet's at the same place
void test_second_sheet(const assets::AssetArchive& arc) {
    group("second_sheet", "a sprite on a second sheet is read from its own files and drawn from its own texture");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ants_hud_art_second_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    std::ifstream in(art_folder() + "/hud_art.txt", std::ios::binary);
    std::string manifest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    manifest.insert(manifest.find("sprite "), "sheet2size 16 16\nsheet2 1 sheet2_1x.png\nsheet2 2 sheet2_2x.png\nsheet2 3 sheet2_3x.png\nsprite2 second_dot 8 8 4 4 0 0 left top\n");
    {
        std::ofstream out(dir / "hud_art.txt", std::ios::binary | std::ios::trunc);
        out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
    }
    for (const char* name : {"sheet_1x.png", "sheet_2x.png", "sheet_3x.png"}) fs::copy_file(art_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
    const std::array<uint8_t, 4> color = {10, 200, 60, 255};
    for (int k = 1; k <= 3; ++k) {
        std::vector<uint8_t> px;
        for (int i = 0; i < 16 * k * 16 * k; ++i) px.insert(px.end(), color.begin(), color.end());
        const std::vector<uint8_t> bytes = png_of(16 * k, 16 * k, filtered(px, 16 * k, 16 * k, {0}));
        std::ofstream out(dir / ("sheet2_" + std::to_string(k) + "x.png"), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    hud_art::Art art;
    check(art.load(dir.string()) && art.has_second_sheet() && art.sheet_width(1) == 16, "a set with a second sheet is read, and its three files are checked against its size");
    DecodedImage image;
    check(art.sheet_pixels(1, image, 1) && image.width == 16 && image.rgba[0] == color[0] && image.rgba[1] == color[1] && image.rgba[2] == color[2], "...its own file is what a second sheet's pixels are");
    check(art.sheet_pixels(3, image, 1) && image.width == 48 && image.height == 48 && !art.sheet_pixels(3, image, 2), "...at every scale, and there is no third sheet");
    const QuietStdout quiet;
    SDL_Window* win = SDL_CreateWindow("hud-art-second", 0, 0, 960, 540, SDL_WINDOW_HIDDEN);
    Renderer renderer;
    if (win == nullptr || !renderer.init(win, arc)) {
        check(false, "the renderer is up");
        return;
    }
    renderer.set_canvas_size(960, 540);
    check(renderer.preload_art(art), "the renderer makes the textures of both sheets");
    renderer.begin_frame();
    renderer.draw_art(art, art.find_sprite("second_dot"), 100, 100, 8, 8, 0, 0, 8, 8);
    renderer.draw_art(art, art.card, 300, 100, 182, 116, 0, 0, 182, 116);
    std::vector<uint8_t> px(960u * 540u * 4u, 0);
    SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), 960 * 4);
    const auto at = [&](int x, int y) { return &px[(static_cast<size_t>(y) * 960u + static_cast<size_t>(x)) * 4u]; };
    check(at(103, 103)[0] == color[0] && at(103, 103)[1] == color[1] && at(103, 103)[2] == color[2], "a sprite2 is drawn from the second sheet's pixels (" + std::to_string(at(103, 103)[0]) + ", " + std::to_string(at(103, 103)[1]) + ", " + std::to_string(at(103, 103)[2]) + ")");
    check(!(at(350, 150)[0] == color[0] && at(350, 150)[1] == color[1] && at(350, 150)[2] == color[2]) && (at(350, 150)[0] != 0 || at(350, 150)[1] != 0 || at(350, 150)[2] != 0), "...a sprite of the first sheet still comes from the first");
    renderer.shutdown();
    SDL_DestroyWindow(win);
    fs::remove_all(dir, ignored);
}

void test_manifest();
void test_frame();
void test_look(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_classic(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_pieces(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_cursors(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_cursor_draw(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_cursor_classic(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_cursor_damaged(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_slices();
void test_scale();
void test_unusable();
void test_panel(const hud_art::Art& art);
void test_window_pieces(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_window_model(const assets::AssetArchive& arc);
void test_windows(const assets::AssetArchive& arc, const hud_art::Art& art);
void test_windows_more(const assets::AssetArchive& arc, const hud_art::Art& art);

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                  // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    ensure_sdl();
    test_png();
    test_derive();
    test_manifest();
    test_frame();
    test_slices();
    test_scale();
    test_unusable();
    assets::AssetArchive arc;
    hud_art::Art art;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !load_art(art)) {
        std::fprintf(stderr, "cannot open ants.chd or the HUD art\n");
        return 2;
    }
    test_second_sheet(arc);
    test_panel(art);
    test_window_pieces(arc, art);
    test_window_model(arc);
    test_look(arc, art);
    test_classic(arc, art);
    test_pieces(arc, art);
    test_cursors(arc, art);
    test_cursor_draw(arc, art);
    test_cursor_classic(arc, art);
    test_cursor_damaged(arc, art);
    test_windows(arc, art);
    test_windows_more(arc, art);
    std::printf("\nhud art: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

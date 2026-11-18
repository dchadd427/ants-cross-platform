// The PNG writer of the pictures that the server keeps (include/ants_app/png_encode.hpp, src/ants_app/png_encode.cpp): the deflate stream, the zlib wrapper and the PNG file, read back by the repository's
// own reader (png_decode.hpp's inflate_raw, written independently of the writer) and checked against what the formats say.
//   [huffman]  the code lengths: complete, never longer than the limit (counts that make a 29-deep tree are cut to 15, 12 and 7), the more frequent the shorter, one or no symbol still gets a complete code
//   [deflate]  every kind of input comes back byte for byte: empty, one byte, runs longer than a match, distances at the edge of the window and beyond it, all 256 byte values, noise, text, inputs that
//              need more than one block and ones whose codes have to be cut to 15 bits; a compressible input is much smaller, noise is not larger than a few bytes over its own size
//   [zlib]     the two header bytes (a multiple of 31, deflate, a 32 KiB window), the Adler-32 check against a plain computation
//   [png]      the signature, the chunks in order with their CRC-32, IHDR (8-bit RGB, no interlace), the rows' filter bytes, the pixels after unfiltering; one pixel, one column, one row, a wide and a tall
//              picture; a picture of flat color, of a gradient and of noise; arguments that are refused; the same bytes every time
// Usage: test_png_encode. Exit code 0 when every check passes.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ants_app/png_decode.hpp"
#include "ants_app/png_encode.hpp"

using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

std::vector<uint8_t> noise(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n);
    uint32_t s = seed;
    for (uint8_t& b : out) {
        s = s * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(s >> 24);
    }
    return out;
}

std::vector<uint8_t> text_of(size_t n) {
    static const char* const words[] = {"ant", "hill", "food", "the", "queen", "tunnel", "and", "of", "march", "colony", "flower", "bridge"};
    std::vector<uint8_t> out;
    uint32_t s = 7;
    while (out.size() < n) {
        s = s * 1103515245u + 12345u;
        const char* w = words[(s >> 16) % 12];
        out.insert(out.end(), w, w + std::strlen(w));
        out.push_back(' ');
    }
    out.resize(n);
    return out;
}

bool round_trip(const std::vector<uint8_t>& data, size_t& compressed_size) {
    const std::vector<uint8_t> z = deflate_raw(data.data(), data.size());
    compressed_size = z.size();
    std::vector<uint8_t> back;
    return inflate_raw(z.data(), z.size(), back, data.size() + 16) && back == data;
}

uint32_t be32(const std::string& s, size_t at) {
    return (uint32_t{static_cast<unsigned char>(s[at])} << 24) | (uint32_t{static_cast<unsigned char>(s[at + 1])} << 16) | (uint32_t{static_cast<unsigned char>(s[at + 2])} << 8) | uint32_t{static_cast<unsigned char>(s[at + 3])};
}

uint32_t crc32_plain(const std::string& s, size_t from, size_t count) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = from; i < from + count; ++i) {
        crc ^= static_cast<unsigned char>(s[i]);
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

uint32_t adler32_plain(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    for (const uint8_t v : data) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void test_deflate() {
    group("deflate", "every kind of input comes back byte for byte from the repository's own inflate");
    size_t size = 0;
    check(round_trip({}, size) && size >= 1, "empty input: the one empty final block");
    check(round_trip({0x41}, size), "one byte");
    check(round_trip({0, 0}, size) && round_trip({1, 2, 3}, size), "two zeros, three bytes (shorter than a match, or just one)");
    check(round_trip(std::vector<uint8_t>(100000, 0), size) && size < 400, "100,000 zeros: a run far longer than a match of 258, in " + std::to_string(size) + " bytes");
    check(round_trip(std::vector<uint8_t>(259, 7), size) && round_trip(std::vector<uint8_t>(258, 7), size) && round_trip(std::vector<uint8_t>(257, 7), size), "runs of 257, 258 and 259 (the edges of the longest match)");
    std::vector<uint8_t> all(256);
    for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<uint8_t>(i);
    check(round_trip(all, size), "all 256 byte values once");
    std::vector<uint8_t> twice = all;
    twice.insert(twice.end(), all.begin(), all.end());
    check(round_trip(twice, size) && size < 400, "the 256 values twice: the second copy is one match");
    {   // a repeat at the edge of the window: the same 300 bytes at distance 32,768 (a match) and at 32,769 (too far: the bytes are written again)
        size_t found_size = 0, missed_size = 0;
        for (const size_t gap : {size_t{32768}, size_t{32769}}) {
            std::vector<uint8_t> data = noise(300, 5);
            const std::vector<uint8_t> filler = noise(gap - 300, 9);
            data.insert(data.end(), filler.begin(), filler.end());
            const std::vector<uint8_t> first(data.begin(), data.begin() + 300);
            data.insert(data.end(), first.begin(), first.end());
            size_t size_now = 0;
            check(round_trip(data, size_now), "a copy of 300 bytes at distance " + std::to_string(gap) + " comes back");
            (gap == 32768 ? found_size : missed_size) = size_now;
        }
        check(found_size + 200 < missed_size, "the copy at 32,768 is a match and the one at 32,769 is not: " + std::to_string(found_size) + " and " + std::to_string(missed_size) + " bytes");
    }
    check(round_trip(noise(300000, 1), size) && size <= 300000 + 300000 / 8, "300,000 bytes of noise (several blocks; the size stays within an eighth of the input): " + std::to_string(size));
    check(round_trip(text_of(200000), size) && size < 100000, "200,000 bytes of words: " + std::to_string(size));
    check(round_trip(noise(70000, 2), size) && round_trip(text_of(70000), size), "70,000 bytes (the second block starts inside a match)");
    check(deflate_raw(all.data(), all.size()) == deflate_raw(all.data(), all.size()), "the same input, the same bytes");
}

/// The sum of 2^-length over the symbols that have a code, as a whole number of 2^-max_len
int64_t kraft_sum(const std::vector<uint8_t>& lens, int max_len) {
    int64_t sum = 0;
    for (const uint8_t l : lens) sum += l > 0 ? (int64_t{1} << (max_len - l)) : 0;
    return sum;
}

void test_huffman() {
    group("huffman", "the code lengths are a complete code, as short as the counts ask, and never longer than the limit");
    {   // counts that grow like Fibonacci numbers make the plain Huffman tree 29 deep: the codes are cut to the limit and the code is still complete
        std::vector<uint32_t> fib(30);
        uint32_t a = 1, b = 1;
        for (uint32_t& f : fib) {
            f = a;
            const uint32_t next = a + b;
            a = b;
            b = next;
        }
        for (const int limit : {15, 12, 7}) {
            const std::vector<uint8_t> lens = huffman_code_lengths(fib, limit);
            check(lens.size() == 30 && *std::max_element(lens.begin(), lens.end()) <= limit && *std::min_element(lens.begin(), lens.end()) >= 1, "30 Fibonacci counts, limit " + std::to_string(limit) + ": every symbol has a code of at most the limit");
            check(kraft_sum(lens, limit) == (int64_t{1} << limit), "... and the code is complete");
            check(lens[29] <= lens[0], "... the most frequent symbol's code is not longer than the rarest's");
        }
        const std::vector<uint8_t> free_tree = huffman_code_lengths(fib, 15);
        check(*std::max_element(free_tree.begin(), free_tree.end()) == 15, "30 Fibonacci counts at 15: the longest code is cut to 15 (the plain tree is 29)");
    }
    {   // the counts of a flat alphabet, a lopsided one, and the cases with fewer than two symbols
        const std::vector<uint8_t> flat = huffman_code_lengths(std::vector<uint32_t>(256, 10), 15);
        check(std::all_of(flat.begin(), flat.end(), [](uint8_t l) { return l == 8; }), "256 equal counts: every code is 8 bits");
        std::vector<uint32_t> lopsided(286, 0);
        lopsided[0] = 1000;
        lopsided[1] = 1;
        lopsided[2] = 1;
        lopsided[3] = 1;
        const std::vector<uint8_t> l = huffman_code_lengths(lopsided, 15);
        std::vector<uint8_t> small_ones = {l[1], l[2], l[3]};
        std::sort(small_ones.begin(), small_ones.end());
        check(l[0] == 1 && small_ones == std::vector<uint8_t>{2, 3, 3} && kraft_sum(l, 15) == (int64_t{1} << 15) && l[4] == 0 && l[285] == 0, "a symbol of 1000 and three of 1: the big one has 1 bit, the others 2, 3 and 3 bits; the unused have none");
        for (const size_t used : {size_t{0}, size_t{1}}) {
            std::vector<uint32_t> few(30, 0);
            if (used == 1) few[17] = 5;
            const std::vector<uint8_t> lens = huffman_code_lengths(few, 15);
            const size_t coded = static_cast<size_t>(std::count_if(lens.begin(), lens.end(), [](uint8_t v) { return v > 0; }));
            check(coded == 2 && kraft_sum(lens, 15) == (int64_t{1} << 14) * 2 && (used == 0 || lens[17] == 1), std::to_string(used) + " symbol used: two codes of one bit (a decoder never meets an incomplete code)");
        }
    }
    {   // many random counts: complete, within the limit, ordered by count
        uint32_t s = 99;
        int bad = 0;
        for (size_t round = 0; round < 300; ++round) {
            const size_t n = 2 + (round * 7) % 286;
            std::vector<uint32_t> freq(n);
            for (uint32_t& f : freq) {
                s = s * 1664525u + 1013904223u;
                f = (s >> 28) == 0 ? 0u : 1u + ((s >> 8) % (1u << ((s >> 3) % 20)));
            }
            const int limit = 7 + static_cast<int>(round % 9);
            const std::vector<uint8_t> lens = huffman_code_lengths(freq, limit);
            const size_t coded = static_cast<size_t>(std::count_if(lens.begin(), lens.end(), [](uint8_t v) { return v > 0; }));
            bool ok = coded >= 2 && lens.size() == n && *std::max_element(lens.begin(), lens.end()) <= limit && kraft_sum(lens, limit) == (int64_t{1} << limit);
            if (static_cast<size_t>(std::count_if(freq.begin(), freq.end(), [](uint32_t f) { return f > 0; })) >= (static_cast<size_t>(1) << limit)) ok = true;     // (more symbols than codes of that length: not asked)
            for (size_t i = 0; ok && i < n; ++i) {
                for (size_t j = 0; ok && j < n; ++j) {
                    if (freq[i] > freq[j] && lens[j] > 0 && lens[i] > lens[j]) ok = false;                       // a rarer symbol never has a shorter code
                }
            }
            if (!ok) ++bad;
        }
        check(bad == 0, "300 random sets of counts and limits: complete, within the limit, a more frequent symbol never has a longer code (" + std::to_string(bad) + " wrong)");
    }
}

void test_zlib() {
    group("zlib", "the header and the Adler-32 check");
    for (const std::vector<uint8_t>& data : {std::vector<uint8_t>{}, std::vector<uint8_t>{1}, text_of(5000), noise(70000, 4)}) {
        const std::vector<uint8_t> z = zlib_compress(data.data(), data.size());
        check(z.size() >= 6 && z[0] == 0x78 && ((z[0] * 256 + z[1]) % 31) == 0 && (z[1] & 0x20) == 0, "the header of " + std::to_string(data.size()) + " bytes: deflate with a 32 KiB window, a multiple of 31, no dictionary");
        const uint32_t adler = adler32_plain(data);
        check(z.size() >= 6 && ((uint32_t{z[z.size() - 4]} << 24) | (uint32_t{z[z.size() - 3]} << 16) | (uint32_t{z[z.size() - 2]} << 8) | z[z.size() - 1]) == adler, "the Adler-32 of " + std::to_string(data.size()) + " bytes at the end");
        std::vector<uint8_t> back;
        check(z.size() >= 6 && inflate_raw(z.data() + 2, z.size() - 6, back, data.size() + 16) && back == data, "the stream between them is the deflate stream");
    }
    // the known value: Adler-32 of "Wikipedia" is 0x11E60398
    const std::string wiki = "Wikipedia";
    const std::vector<uint8_t> z = zlib_compress(reinterpret_cast<const uint8_t*>(wiki.data()), wiki.size());
    check(z.size() > 6 && z[z.size() - 4] == 0x11 && z[z.size() - 3] == 0xE6 && z[z.size() - 2] == 0x03 && z[z.size() - 1] == 0x98, "the Adler-32 of \"Wikipedia\" is the published 0x11E60398");
}

struct Chunk {
    std::string type;
    size_t at{0};
    size_t length{0};
};

/// The chunks of a PNG, each with a right CRC-32 (false when a length, a CRC or the order is wrong), and the unfiltered pixels of an RGB image
bool read_png(const std::string& png, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgb, std::vector<uint8_t>* filters = nullptr) {
    if (png.size() < 8 + 25 + 12 || png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
    std::vector<Chunk> chunks;
    size_t pos = 8;
    while (pos + 12 <= png.size()) {
        Chunk c;
        c.length = be32(png, pos);
        c.type = png.substr(pos + 4, 4);
        c.at = pos + 8;
        if (c.at + c.length + 4 > png.size()) return false;
        if (be32(png, c.at + c.length) != crc32_plain(png, pos + 4, 4 + c.length)) return false;
        chunks.push_back(c);
        pos = c.at + c.length + 4;
    }
    if (pos != png.size() || chunks.size() != 3 || chunks[0].type != "IHDR" || chunks[1].type != "IDAT" || chunks[2].type != "IEND" || chunks[0].length != 13 || chunks[2].length != 0) return false;
    w = be32(png, chunks[0].at);
    h = be32(png, chunks[0].at + 4);
    if (png.compare(chunks[0].at + 8, 5, "\x08\x02\x00\x00\x00", 5) != 0) return false;
    const std::vector<uint8_t> idat(png.begin() + static_cast<std::ptrdiff_t>(chunks[1].at), png.begin() + static_cast<std::ptrdiff_t>(chunks[1].at + chunks[1].length));
    if (idat.size() < 6 || idat[0] != 0x78) return false;
    const size_t stride = static_cast<size_t>(w) * 3u;
    std::vector<uint8_t> raw;
    if (!inflate_raw(idat.data() + 2, idat.size() - 6, raw, (stride + 1) * h) || raw.size() != (stride + 1) * h) return false;
    if (adler32_plain(raw) != ((uint32_t{idat[idat.size() - 4]} << 24) | (uint32_t{idat[idat.size() - 3]} << 16) | (uint32_t{idat[idat.size() - 2]} << 8) | idat[idat.size() - 1])) return false;
    rgb.assign(stride * h, 0);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t filter = raw[y * (stride + 1)];
        if (filter > 4) return false;
        if (filters != nullptr) filters->push_back(filter);
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= 3 ? rgb[y * stride + i - 3] : 0;
            const int b = y > 0 ? rgb[(y - 1) * stride + i] : 0;
            const int c = (i >= 3 && y > 0) ? rgb[(y - 1) * stride + i - 3] : 0;
            const int x = raw[y * (stride + 1) + 1 + i];
            int predicted = 0;
            if (filter == 1) predicted = a;
            if (filter == 2) predicted = b;
            if (filter == 3) predicted = (a + b) / 2;
            if (filter == 4) {
                const int p = a + b - c;
                const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                predicted = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            }
            rgb[y * stride + i] = static_cast<uint8_t>(x + predicted);
        }
    }
    return true;
}

std::vector<uint8_t> picture(uint32_t w, uint32_t h, int kind) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3u);
    uint32_t s = 11;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 3u];
            if (kind == 0) {
                p[0] = 40; p[1] = 90; p[2] = 160;
            } else if (kind == 1) {
                p[0] = static_cast<uint8_t>(x * 255u / std::max<uint32_t>(1, w - 1)); p[1] = static_cast<uint8_t>(y * 255u / std::max<uint32_t>(1, h - 1)); p[2] = static_cast<uint8_t>((x + y) & 0xFF);
            } else {
                s = s * 1664525u + 1013904223u;
                p[0] = static_cast<uint8_t>(s >> 24); p[1] = static_cast<uint8_t>(s >> 16); p[2] = static_cast<uint8_t>(s >> 8);
            }
        }
    }
    return px;
}

void test_png() {
    group("png", "the file's frame, its checks, and the pixels after unfiltering");
    struct Shape {
        uint32_t w, h;
    };
    for (const Shape shape : {Shape{1, 1}, Shape{1, 40}, Shape{40, 1}, Shape{3, 3}, Shape{300, 300}, Shape{777, 5}, Shape{5, 777}}) {
        for (int kind = 0; kind < 3; ++kind) {
            const std::vector<uint8_t> px = picture(shape.w, shape.h, kind);
            const std::string png = encode_png_rgb(px.data(), static_cast<int32_t>(shape.w), static_cast<int32_t>(shape.h));
            uint32_t w = 0, h = 0;
            std::vector<uint8_t> back;
            const std::string label = std::to_string(shape.w) + " x " + std::to_string(shape.h) + (kind == 0 ? " flat" : kind == 1 ? " gradient" : " noise");
            check(read_png(png, w, h, back) && w == shape.w && h == shape.h && back == px, label + ": the frame, the checks and every pixel");
            if (shape.w == 300 && shape.h == 300) {
                if (kind == 0) check(png.size() < 1000, label + ": a flat picture is " + std::to_string(png.size()) + " bytes");
                if (kind == 1) check(png.size() < 300 * 300 * 3 / 3, label + ": a gradient is " + std::to_string(png.size()) + " bytes");
                if (kind == 2) check(png.size() < 300 * 300 * 3 + 300 * 3 * 8, label + ": noise is " + std::to_string(png.size()) + " bytes (not much over its own size)");
                check(encode_png_rgb(px.data(), 300, 300) == png, label + ": the same pixels, the same bytes");
            }
        }
    }
    {   // the filters are chosen by row: rows that are best left alone, rows that follow the one above and rows that follow the pixel before
        std::vector<uint8_t> px(64 * 4 * 3);
        uint32_t s = 1;
        for (size_t x = 0; x < 64 * 3; ++x) {
            s = s * 1664525u + 1013904223u;
            px[x] = static_cast<uint8_t>(s >> 24);                                      // row 0: noise
            px[64 * 3 + x] = px[x];                                                     // row 1: a copy of row 0 (Up: all zeros)
            px[2 * 64 * 3 + x] = static_cast<uint8_t>((x / 3) * 4);                     // row 2: a ramp along the row (Sub: a constant)
            px[3 * 64 * 3 + x] = static_cast<uint8_t>(x % 3 * 50);                      // row 3: three flat stripes of one pixel's width, repeating (Sub: zeros)
        }
        const std::string png = encode_png_rgb(px.data(), 64, 4);
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> back, filters;
        check(read_png(png, w, h, back, &filters) && back == px && filters.size() == 4, "four rows of different kinds come back");
        check(filters.size() == 4 && filters[1] == 2 && filters[2] == 1 && filters[3] == 1, "the filters chosen: Up for the copy of the row above, Sub for the ramp and the repeating stripe");
    }
    {
        const std::vector<uint8_t> px = picture(4, 4, 1);
        check(encode_png_rgb(nullptr, 4, 4).empty() && encode_png_rgb(px.data(), 0, 4).empty() && encode_png_rgb(px.data(), 4, 0).empty() && encode_png_rgb(px.data(), -1, 4).empty() && encode_png_rgb(px.data(), 4, 65536).empty() && encode_png_rgb(px.data(), 65536, 4).empty(),
              "no pixels, a side of 0, a negative side or a side over 65,535: no file");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    test_huffman();
    test_deflate();
    test_zlib();
    test_png();
    std::printf("\npng encode: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

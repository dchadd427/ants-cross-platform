#pragma once

// A small PNG reader for the HUD art's sheets (hud_art.hpp): 8-bit RGBA, not interlaced, which is what tools/hud_art writes. It reads the file's bytes (a zlib stream of filtered rows) and
// gives the pixels; anything else (another color type, an interlaced file, a stream that is cut or damaged) is refused, never guessed at. No SDL, no third-party code.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ants::app {

struct DecodedImage {
    int32_t width{0};
    int32_t height{0};
    std::vector<uint8_t> rgba;      // width * height * 4 bytes, rows from the top
};

/// Decodes a PNG of 8-bit RGBA pixels. False (and `out` empty) when the bytes are not such a file or are damaged
bool decode_png_rgba(const uint8_t* data, size_t size, DecodedImage& out);

/// The deflate stream (RFC 1951, without the zlib header) of `data` inflated into `out`; false when it is damaged or longer than `limit` bytes. (Public for the tests.)
bool inflate_raw(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit);

}  // namespace ants::app

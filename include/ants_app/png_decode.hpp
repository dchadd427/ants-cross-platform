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

/// Decodes a PNG of 8-bit RGBA pixels. False (and `out` empty) when the bytes are not such a file or are damaged. `rows` > 0: only the first `rows` rows (`out.height` is that), which needs only the
/// start of the file's data and a part of the memory; more rows than the picture has is refused
bool decode_png_rgba(const uint8_t* data, size_t size, DecodedImage& out, int32_t rows = 0);

/// The deflate stream (RFC 1951, without the zlib header) of `data` inflated into `out`; false when it is damaged or longer than `limit` bytes. (Public for the tests.)
bool inflate_raw(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit);

/// The start of that stream: inflated until `out` holds `want` bytes or more (it may hold a few more, up to the end of a block or copy); false when the stream is damaged or ends before `want`
bool inflate_raw_prefix(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t want);

}  // namespace ants::app

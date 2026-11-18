#pragma once

// A small PNG writer for the pictures that the server keeps of players' maps (map_thumbnail.hpp): 8-bit RGB, not interlaced, the rows filtered (the best of the five filters for each row) and
// deflated with an LZ77 pass and dynamic Huffman codes. No library: the reader that checks it is png_decode.hpp's inflate_raw (an independent piece of code) and, in the tests, the zlib of Python. The
// same pixels give the same bytes on every machine.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ants::app {

/// The code lengths of a complete Huffman code for the counts in `freq` (a symbol that does not occur gets 0; the more often a symbol occurs, the shorter its code; no length is over `max_len`, 1 to 15). At
/// least two symbols get a code, a dummy one when fewer occur, so that no decoder meets an incomplete code. More symbols than 2^max_len: all lengths 0. (Public for the tests.)
std::vector<uint8_t> huffman_code_lengths(std::vector<uint32_t> freq, int max_len);

/// The deflate stream (RFC 1951, without the zlib header) of `size` bytes: blocks with dynamic Huffman codes, matches of 3 to 258 bytes within 32,768 bytes back. An empty input gives the one empty final block.
std::vector<uint8_t> deflate_raw(const uint8_t* data, size_t size);

/// The zlib stream (RFC 1950) of `size` bytes: the two header bytes, deflate_raw and the Adler-32 check of the data
std::vector<uint8_t> zlib_compress(const uint8_t* data, size_t size);

/// The PNG file of an RGB image of width x height pixels (3 bytes each, rows from the top, tightly packed). Empty when a side is not 1 to 65,535, the pixels are more than 1 GiB or `rgb` is null.
std::string encode_png_rgb(const uint8_t* rgb, int32_t width, int32_t height);

}  // namespace ants::app

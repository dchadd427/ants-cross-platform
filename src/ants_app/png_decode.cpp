// A small PNG reader (png_decode.hpp): the chunks, the zlib stream (RFC 1950 / 1951: stored, fixed and dynamic blocks) and the row filters (RFC 2083).
#include "ants_app/png_decode.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace ants::app {

namespace {

// The bits of a deflate stream, least significant first
class Bits {
public:
    Bits(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}

    /// Makes sure that at least `n` (<= 24) bits are in the buffer, zeros past the end (`overrun` tells afterwards that more was asked for than the stream has)
    void need(int n) {
        while (count_ < n) {
            if (p_ < end_) {
                buf_ |= static_cast<uint32_t>(*p_++) << count_;
            } else {
                ++padding_;
            }
            count_ += 8;
        }
    }
    uint32_t take(int n) {
        need(n);
        const uint32_t v = n == 0 ? 0u : (buf_ & ((1u << n) - 1u));
        buf_ >>= n;
        count_ -= n;
        return v;
    }
    uint32_t peek() const { return buf_; }
    void drop(int n) {
        buf_ >>= n;
        count_ -= n;
    }
    /// Whole bytes that were read as zeros past the end, less the bits still unused: true when a decoded value used bits that are not in the stream
    bool overrun() const { return padding_ * 8 > count_; }
    void align() {
        const int drop_bits = count_ & 7;
        buf_ >>= drop_bits;
        count_ -= drop_bits;
    }
    /// A byte of a stored block (after align())
    bool byte(uint8_t& v) {
        if (count_ >= 8) {
            v = static_cast<uint8_t>(buf_ & 0xffu);
            buf_ >>= 8;
            count_ -= 8;
            return !overrun();
        }
        if (p_ >= end_) return false;
        v = *p_++;
        return true;
    }

private:
    const uint8_t* p_;
    const uint8_t* end_;
    uint32_t buf_{0};
    int count_{0};
    int padding_{0};
};

constexpr int kFastBits = 10;

// A canonical Huffman code: a table for the codes of up to kFastBits bits (indexed by the next bits of the stream), and the counts and symbols for the longer ones
struct Huffman {
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 288> symbol{};
    std::array<uint16_t, 1u << kFastBits> fast{};      // (length << 12) | symbol, 0 when the code is longer than kFastBits or not assigned
    bool built{false};

    bool build(const uint8_t* lengths, size_t n) {
        count.fill(0);
        for (size_t i = 0; i < n; ++i) ++count[lengths[i]];
        count[0] = 0;
        int left = 1;
        for (size_t len = 1; len < 16; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;                    // over-subscribed
        }
        std::array<uint16_t, 16> offs{};
        for (size_t len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + count[len]);
        for (size_t i = 0; i < n; ++i) {
            if (lengths[i] != 0) symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
        }
        fast.fill(0);
        uint32_t code = 0;
        size_t index = 0;
        for (size_t len = 1; len <= static_cast<size_t>(kFastBits); ++len) {
            for (size_t k = 0; k < count[len]; ++k, ++index, ++code) {
                uint32_t rev = 0;
                for (size_t b = 0; b < len; ++b) rev |= ((code >> b) & 1u) << (len - 1 - b);
                for (uint32_t fill = rev; fill < (1u << kFastBits); fill += 1u << len) {
                    fast[fill] = static_cast<uint16_t>((len << 12) | symbol[index]);
                }
            }
            code <<= 1;
        }
        built = true;
        return true;
    }

    /// The next symbol, or -1 when the stream holds no code there
    int decode(Bits& bits) const {
        bits.need(15);
        const uint16_t e = fast[bits.peek() & ((1u << kFastBits) - 1u)];
        if (e != 0) {
            bits.drop(e >> 12);
            return e & 0xfff;
        }
        int code = 0;
        int first = 0;
        int index = 0;
        uint32_t buf = bits.peek();
        for (int len = 1; len < 16; ++len) {
            code |= static_cast<int>(buf & 1u);
            buf >>= 1;
            const int c = count[static_cast<size_t>(len)];
            if (code - c < first) {
                bits.drop(len);
                return symbol[static_cast<size_t>(index + (code - first))];
            }
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

constexpr std::array<uint16_t, 29> kLengthBase = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_codes(Bits& bits, const Huffman& lit, const Huffman& dist, std::vector<uint8_t>& out, size_t limit, size_t stop) {
    for (;;) {
        const int sym = lit.decode(bits);
        if (sym < 0 || bits.overrun()) return false;
        if (sym < 256) {
            if (out.size() >= limit) return false;
            out.push_back(static_cast<uint8_t>(sym));
            if (out.size() >= stop) return true;
        } else if (sym == 256) {
            return true;
        } else {
            const int li = sym - 257;
            if (li >= 29) return false;
            const size_t len = kLengthBase[static_cast<size_t>(li)] + bits.take(kLengthExtra[static_cast<size_t>(li)]);
            const int ds = dist.decode(bits);
            if (ds < 0 || ds >= 30) return false;
            const size_t d = kDistBase[static_cast<size_t>(ds)] + bits.take(kDistExtra[static_cast<size_t>(ds)]);
            if (bits.overrun() || d > out.size() || out.size() + len > limit) return false;
            const size_t from = out.size() - d;
            for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]);     // (the copy may overlap itself: a run)
            if (out.size() >= stop) return true;
        }
    }
}

bool fixed_tables(Huffman& lit, Huffman& dist) {
    uint8_t lengths[288];
    for (int i = 0; i < 144; ++i) lengths[i] = 8;
    for (int i = 144; i < 256; ++i) lengths[i] = 9;
    for (int i = 256; i < 280; ++i) lengths[i] = 7;
    for (int i = 280; i < 288; ++i) lengths[i] = 8;
    uint8_t dl[30];
    for (uint8_t& v : dl) v = 5;
    return lit.build(lengths, 288) && dist.build(dl, 30);
}

bool dynamic_tables(Bits& bits, Huffman& lit, Huffman& dist) {
    const int nlen = static_cast<int>(bits.take(5)) + 257;
    const int ndist = static_cast<int>(bits.take(5)) + 1;
    const int ncode = static_cast<int>(bits.take(4)) + 4;
    if (nlen > 286 || ndist > 30) return false;
    static constexpr std::array<uint8_t, 19> order = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t cl[19] = {};
    for (int i = 0; i < ncode; ++i) cl[order[static_cast<size_t>(i)]] = static_cast<uint8_t>(bits.take(3));
    Huffman code_lengths;
    if (!code_lengths.build(cl, 19)) return false;
    uint8_t lengths[320] = {};
    int i = 0;
    while (i < nlen + ndist) {
        const int sym = code_lengths.decode(bits);
        if (sym < 0 || bits.overrun()) return false;
        if (sym < 16) {
            lengths[i++] = static_cast<uint8_t>(sym);
            continue;
        }
        uint8_t value = 0;
        int repeat = 0;
        if (sym == 16) {
            if (i == 0) return false;
            value = lengths[i - 1];
            repeat = 3 + static_cast<int>(bits.take(2));
        } else if (sym == 17) {
            repeat = 3 + static_cast<int>(bits.take(3));
        } else {
            repeat = 11 + static_cast<int>(bits.take(7));
        }
        if (i + repeat > nlen + ndist) return false;
        while (repeat-- > 0) lengths[i++] = value;
    }
    if (lengths[256] == 0) return false;                  // (no end of block)
    return lit.build(lengths, static_cast<size_t>(nlen)) && dist.build(lengths + nlen, static_cast<size_t>(ndist));
}

uint32_t be32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; }

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

namespace {

/// Inflates until the stream ends, or (when `stop` is smaller than the stream) until `out` holds `stop` bytes or more
bool inflate_until(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit, size_t stop) {
    Bits bits(data, size);
    out.clear();
    for (;;) {
        const uint32_t final_block = bits.take(1);
        const uint32_t type = bits.take(2);
        if (type == 0) {
            bits.align();
            uint8_t b[4];
            for (uint8_t& v : b) {
                if (!bits.byte(v)) return false;
            }
            const size_t len = static_cast<size_t>(b[0]) | (static_cast<size_t>(b[1]) << 8);
            const size_t nlen = static_cast<size_t>(b[2]) | (static_cast<size_t>(b[3]) << 8);
            if ((len ^ nlen) != 0xffffu || out.size() + len > limit) return false;
            for (size_t i = 0; i < len; ++i) {
                uint8_t v = 0;
                if (!bits.byte(v)) return false;
                out.push_back(v);
            }
        } else if (type == 1 || type == 2) {
            Huffman lit;
            Huffman dist;
            if (type == 1 ? !fixed_tables(lit, dist) : !dynamic_tables(bits, lit, dist)) return false;
            if (bits.overrun() || !inflate_codes(bits, lit, dist, out, limit, stop)) return false;
        } else {
            return false;
        }
        if (out.size() >= stop) return !bits.overrun();
        if (final_block != 0) return !bits.overrun();
    }
}

}  // namespace

bool inflate_raw(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t limit) {
    return inflate_until(data, size, out, limit, std::numeric_limits<size_t>::max());
}

bool inflate_raw_prefix(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t want) {
    constexpr size_t kOvershoot = 65536 + 258;                  // (the most that the last block or copy can add past `want`)
    return inflate_until(data, size, out, want + kOvershoot, want) && out.size() >= want;
}

bool decode_png_rgba(const uint8_t* data, size_t size, DecodedImage& out, int32_t rows) {
    out = DecodedImage{};
    static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (data == nullptr || size < 8 + 25 || std::memcmp(data, kSignature, 8) != 0) return false;
    size_t at = 8;
    uint32_t width = 0;
    uint32_t height = 0;
    bool header = false;
    std::vector<uint8_t> idat;
    while (at + 12 <= size) {
        const uint32_t len = be32(data + at);
        const char* type = reinterpret_cast<const char*>(data + at + 4);
        if (len > size - at - 12) return false;
        const uint8_t* body = data + at + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return false;
            width = be32(body);
            height = be32(body + 4);
            if (body[8] != 8 || body[9] != 6 || body[10] != 0 || body[11] != 0 || body[12] != 0) return false;      // 8 bits, RGBA, deflate, standard filters, not interlaced
            header = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + len;
    }
    constexpr uint64_t kMaxPixels = 32u * 1024u * 1024u;       // (the sheets are a few million; a file of another size is not ours)
    if (!header || width == 0 || height == 0 || static_cast<uint64_t>(width) * height > kMaxPixels || idat.size() < 6) return false;
    if ((idat[0] & 0x0f) != 8 || ((idat[0] << 8) | idat[1]) % 31 != 0 || (idat[1] & 0x20) != 0) return false;        // zlib: deflate, a good header, no preset dictionary
    if (rows < 0 || (rows > 0 && static_cast<uint32_t>(rows) > height)) return false;
    const uint32_t take = rows > 0 ? static_cast<uint32_t>(rows) : height;         // (the rows to read: all of them, or the first `rows`)
    const size_t stride = static_cast<size_t>(width) * 4u;
    const size_t want = (stride + 1) * take;
    std::vector<uint8_t> raw;
    raw.reserve(want);
    if (take == height ? (!inflate_raw(idat.data() + 2, idat.size() - 2, raw, want) || raw.size() != want) : !inflate_raw_prefix(idat.data() + 2, idat.size() - 2, raw, want)) return false;
    out.width = static_cast<int32_t>(width);
    out.height = static_cast<int32_t>(take);
    out.rgba.assign(stride * take, 0);
    for (uint32_t y = 0; y < take; ++y) {
        const uint8_t filter = raw[y * (stride + 1)];
        const uint8_t* in = raw.data() + y * (stride + 1) + 1;
        uint8_t* row = out.rgba.data() + y * stride;
        const uint8_t* above = y > 0 ? row - stride : nullptr;
        if (filter > 4) {
            out = DecodedImage{};
            return false;
        }
        switch (filter) {
            case 0:
                std::memcpy(row, in, stride);
                break;
            case 1:
                for (size_t i = 0; i < stride; ++i) row[i] = static_cast<uint8_t>(in[i] + (i >= 4 ? row[i - 4] : 0));
                break;
            case 2:
                for (size_t i = 0; i < stride; ++i) row[i] = static_cast<uint8_t>(in[i] + (above != nullptr ? above[i] : 0));
                break;
            case 3:
                for (size_t i = 0; i < stride; ++i) {
                    const int a = i >= 4 ? row[i - 4] : 0;
                    const int b = above != nullptr ? above[i] : 0;
                    row[i] = static_cast<uint8_t>(in[i] + (a + b) / 2);
                }
                break;
            default:
                for (size_t i = 0; i < stride; ++i) {
                    const int a = i >= 4 ? row[i - 4] : 0;
                    const int b = above != nullptr ? above[i] : 0;
                    const int c = (above != nullptr && i >= 4) ? above[i - 4] : 0;
                    row[i] = static_cast<uint8_t>(in[i] + paeth(a, b, c));
                }
                break;
        }
    }
    return true;
}

}  // namespace ants::app

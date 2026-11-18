#include "ants_app/png_encode.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <queue>
#include <utility>

namespace ants::app {

namespace {

constexpr int kWindow = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kMaxChain = 96;                       // how many earlier places with the same three bytes are tried for a match
constexpr int kGoodMatch = 96;                      // a match this long is taken at once
constexpr size_t kBlockTokens = 1u << 15;           // a block ends after this many literals and matches (each block has its own codes)
constexpr int kEndOfBlock = 256;

// the length codes 257 .. 285 and the distance codes 0 .. 29 of RFC 1951 (base value, extra bits)
constexpr uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr uint8_t kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct Token {
    uint16_t length;       // 0: a literal (`value` is the byte); else the match length 3 .. 258
    uint16_t value;        // the byte, or the distance 1 .. 32768
};

int length_symbol(int length) {                       // 257 .. 285
    int i = 28;
    while (kLengthBase[i] > length) --i;
    return 257 + i;
}

int dist_symbol(int dist) {                           // 0 .. 29
    int i = 29;
    while (kDistBase[i] > dist) --i;
    return i;
}

class BitWriter {
public:
    void put(uint32_t bits, int count) {              // `count` low bits of `bits`, least significant first
        acc_ |= static_cast<uint64_t>(bits) << used_;
        used_ += count;
        while (used_ >= 8) {
            out_.push_back(static_cast<uint8_t>(acc_ & 0xFFu));
            acc_ >>= 8;
            used_ -= 8;
        }
    }
    void put_code(uint32_t code, int length) {        // a Huffman code goes out most significant bit first
        uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
        put(reversed, length);
    }
    std::vector<uint8_t> finish() {
        if (used_ > 0) {
            out_.push_back(static_cast<uint8_t>(acc_ & 0xFFu));
            acc_ = 0;
            used_ = 0;
        }
        return std::move(out_);
    }

private:
    std::vector<uint8_t> out_;
    uint64_t acc_{0};
    int used_{0};
};

/// The canonical codes (RFC 1951, 3.2.2) of code lengths
std::vector<uint32_t> canonical_codes(const std::vector<uint8_t>& lens) {
    std::array<uint32_t, 17> count{};
    for (const uint8_t l : lens) ++count[l];
    count[0] = 0;
    std::array<uint32_t, 17> next{};
    uint32_t code = 0;
    for (size_t bits = 1; bits <= 16; ++bits) {
        code = (code + count[bits - 1]) << 1;
        next[bits] = code;
    }
    std::vector<uint32_t> codes(lens.size(), 0);
    for (size_t i = 0; i < lens.size(); ++i) {
        if (lens[i] != 0) codes[i] = next[lens[i]]++;
    }
    return codes;
}

struct CodeLengthItem {
    uint8_t symbol;        // 0 .. 18
    uint8_t extra;         // the value of the extra bits (16, 17 and 18)
};

/// The code lengths as the symbols of the code length alphabet: 0 .. 15 as they are, 16 repeats the one before 3 to 6 times, 17 and 18 are runs of 3 to 10 and 11 to 138 zeros
std::vector<CodeLengthItem> encode_lengths(const std::vector<uint8_t>& lens) {
    std::vector<CodeLengthItem> out;
    size_t i = 0;
    while (i < lens.size()) {
        const uint8_t value = lens[i];
        size_t run = 1;
        while (i + run < lens.size() && lens[i + run] == value) ++run;
        if (value == 0) {
            size_t left = run;
            while (left >= 11) {
                const size_t take = std::min<size_t>(left, 138);
                out.push_back(CodeLengthItem{18, static_cast<uint8_t>(take - 11)});
                left -= take;
            }
            if (left >= 3) {
                out.push_back(CodeLengthItem{17, static_cast<uint8_t>(left - 3)});
                left = 0;
            }
            for (; left > 0; --left) out.push_back(CodeLengthItem{0, 0});
        } else {
            out.push_back(CodeLengthItem{value, 0});
            size_t left = run - 1;
            while (left >= 3) {
                const size_t take = std::min<size_t>(left, 6);
                out.push_back(CodeLengthItem{16, static_cast<uint8_t>(take - 3)});
                left -= take;
            }
            for (; left > 0; --left) out.push_back(CodeLengthItem{value, 0});
        }
        i += run;
    }
    return out;
}

void write_block(BitWriter& bw, const std::vector<Token>& tokens, bool last) {
    std::vector<uint32_t> lit_freq(286, 0);
    std::vector<uint32_t> dist_freq(30, 0);
    for (const Token& t : tokens) {
        if (t.length == 0) {
            ++lit_freq[t.value];
        } else {
            ++lit_freq[static_cast<size_t>(length_symbol(t.length))];
            ++dist_freq[static_cast<size_t>(dist_symbol(t.value))];
        }
    }
    lit_freq[kEndOfBlock] = 1;
    const std::vector<uint8_t> lit_lens = huffman_code_lengths(lit_freq, 15);
    const std::vector<uint8_t> dist_lens = huffman_code_lengths(dist_freq, 15);
    const std::vector<uint32_t> lit_codes = canonical_codes(lit_lens);
    const std::vector<uint32_t> dist_codes = canonical_codes(dist_lens);

    size_t hlit = 286;
    while (hlit > 257 && lit_lens[hlit - 1] == 0) --hlit;
    size_t hdist = 30;
    while (hdist > 1 && dist_lens[hdist - 1] == 0) --hdist;
    std::vector<uint8_t> all(lit_lens.begin(), lit_lens.begin() + static_cast<std::ptrdiff_t>(hlit));
    all.insert(all.end(), dist_lens.begin(), dist_lens.begin() + static_cast<std::ptrdiff_t>(hdist));
    const std::vector<CodeLengthItem> items = encode_lengths(all);
    std::vector<uint32_t> cl_freq(19, 0);
    for (const CodeLengthItem& item : items) ++cl_freq[item.symbol];
    const std::vector<uint8_t> cl_lens = huffman_code_lengths(cl_freq, 7);
    const std::vector<uint32_t> cl_codes = canonical_codes(cl_lens);
    size_t hclen = 19;
    while (hclen > 4 && cl_lens[kCodeLengthOrder[hclen - 1]] == 0) --hclen;

    bw.put(last ? 1u : 0u, 1);
    bw.put(2u, 2);                                                // dynamic Huffman codes
    bw.put(static_cast<uint32_t>(hlit - 257), 5);
    bw.put(static_cast<uint32_t>(hdist - 1), 5);
    bw.put(static_cast<uint32_t>(hclen - 4), 4);
    for (size_t i = 0; i < hclen; ++i) bw.put(cl_lens[kCodeLengthOrder[i]], 3);
    for (const CodeLengthItem& item : items) {
        bw.put_code(cl_codes[item.symbol], cl_lens[item.symbol]);
        if (item.symbol == 16) bw.put(item.extra, 2);
        if (item.symbol == 17) bw.put(item.extra, 3);
        if (item.symbol == 18) bw.put(item.extra, 7);
    }
    for (const Token& t : tokens) {
        if (t.length == 0) {
            bw.put_code(lit_codes[t.value], lit_lens[t.value]);
            continue;
        }
        const int ls = length_symbol(t.length);
        bw.put_code(lit_codes[static_cast<size_t>(ls)], lit_lens[static_cast<size_t>(ls)]);
        bw.put(static_cast<uint32_t>(t.length - kLengthBase[ls - 257]), kLengthExtra[ls - 257]);
        const int ds = dist_symbol(t.value);
        bw.put_code(dist_codes[static_cast<size_t>(ds)], dist_lens[static_cast<size_t>(ds)]);
        bw.put(static_cast<uint32_t>(t.value - kDistBase[ds]), kDistExtra[ds]);
    }
    bw.put_code(lit_codes[kEndOfBlock], lit_lens[kEndOfBlock]);
}

class MatchFinder {
public:
    MatchFinder(const uint8_t* data, size_t size) : data_(data), size_(size), head_(size_t{1} << kHashBits, -1), prev_(size, -1) {}

    /// Makes the three bytes at `pos` findable (a position with fewer than three bytes left is not)
    void insert(size_t pos) {
        if (pos + kMinMatch > size_) return;
        const uint32_t h = hash(pos);
        prev_[pos] = head_[h];
        head_[h] = static_cast<int32_t>(pos);
    }

    /// The longest earlier match for the bytes at `pos` (within the window; the earliest of equals loses to the nearest): its length (0 when shorter than three) and distance
    int find(size_t pos, int& dist_out) const {
        if (pos + kMinMatch > size_) return 0;
        const size_t limit = std::min<size_t>(kMaxMatch, size_ - pos);
        int best = 0;
        int32_t cand = head_[hash(pos)];
        for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain) {
            const size_t dist = pos - static_cast<size_t>(cand);
            if (dist > static_cast<size_t>(kWindow)) break;
            if (data_[static_cast<size_t>(cand) + static_cast<size_t>(best)] == data_[pos + static_cast<size_t>(best)] || best == 0) {
                size_t len = 0;
                while (len < limit && data_[static_cast<size_t>(cand) + len] == data_[pos + len]) ++len;
                if (static_cast<int>(len) > best) {
                    best = static_cast<int>(len);
                    dist_out = static_cast<int>(dist);
                    if (best >= kGoodMatch || len == limit) break;
                }
            }
            cand = prev_[static_cast<size_t>(cand)];
        }
        return best >= kMinMatch ? best : 0;
    }

private:
    uint32_t hash(size_t pos) const {
        const uint32_t v = (static_cast<uint32_t>(data_[pos]) << 16) | (static_cast<uint32_t>(data_[pos + 1]) << 8) | data_[pos + 2];
        return (v * 2654435761u) >> (32 - kHashBits);
    }
    const uint8_t* data_;
    size_t size_;
    std::vector<int32_t> head_;
    std::vector<int32_t> prev_;
};

uint32_t crc32_of(const uint8_t* data, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

void put32(std::string& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<char>((v >> s) & 0xFFu));
}

void chunk(std::string& out, const char* type, const uint8_t* data, size_t size) {
    put32(out, static_cast<uint32_t>(size));
    const size_t start = out.size();
    out.append(type, 4);
    if (size > 0) out.append(reinterpret_cast<const char*>(data), size);
    put32(out, ~crc32_of(reinterpret_cast<const uint8_t*>(out.data()) + start, out.size() - start));
}

}  // namespace

std::vector<uint8_t> huffman_code_lengths(std::vector<uint32_t> freq, int max_len) {
    const size_t n = freq.size();
    std::vector<uint8_t> lens(n, 0);
    std::vector<size_t> used;
    for (size_t i = 0; i < n; ++i) {
        if (freq[i] > 0) used.push_back(i);
    }
    for (size_t i = 0; used.size() < 2 && i < n; ++i) {
        if (freq[i] == 0) {
            freq[i] = 1;
            used.push_back(i);
        }
    }
    std::sort(used.begin(), used.end());
    if (used.size() > (size_t{1} << max_len)) return lens;               // (more symbols than codes of that length: no code is made; the writer never asks for this)
    // the tree: leaves are 0 .. used.size() - 1, then the inner nodes; the two lightest are joined each time (ties: the earlier node)
    struct Node {
        uint64_t weight;
        int parent;
    };
    std::vector<Node> nodes;
    using Item = std::pair<uint64_t, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    for (const size_t s : used) {
        nodes.push_back(Node{freq[s], -1});
        heap.push(Item{freq[s], static_cast<int>(nodes.size()) - 1});
    }
    while (heap.size() > 1) {
        const Item a = heap.top();
        heap.pop();
        const Item b = heap.top();
        heap.pop();
        nodes.push_back(Node{a.first + b.first, -1});
        const int id = static_cast<int>(nodes.size()) - 1;
        nodes[static_cast<size_t>(a.second)].parent = id;
        nodes[static_cast<size_t>(b.second)].parent = id;
        heap.push(Item{a.first + b.first, id});
    }
    std::vector<int> count(static_cast<size_t>(max_len) + 1, 0);
    for (size_t leaf = 0; leaf < used.size(); ++leaf) {
        int depth = 0;
        for (int p = nodes[leaf].parent; p >= 0; p = nodes[static_cast<size_t>(p)].parent) ++depth;
        ++count[static_cast<size_t>(std::min(depth, max_len))];
    }
    // codes that were cut to max_len leave the code too full: move leaves down until the sum of 2^-length is one again
    int64_t total = 0;
    for (int len = 1; len <= max_len; ++len) total += static_cast<int64_t>(count[static_cast<size_t>(len)]) << (max_len - len);
    while (total > (int64_t{1} << max_len)) {
        --count[static_cast<size_t>(max_len)];
        for (int len = max_len - 1; len > 0; --len) {
            if (count[static_cast<size_t>(len)] > 0) {
                --count[static_cast<size_t>(len)];
                count[static_cast<size_t>(len) + 1] += 2;
                break;
            }
        }
        --total;
    }
    // the more often a symbol occurs, the shorter its code
    std::vector<size_t> order = used;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return freq[a] > freq[b]; });
    size_t next = 0;
    for (int len = 1; len <= max_len; ++len) {
        for (int k = 0; k < count[static_cast<size_t>(len)] && next < order.size(); ++k) lens[order[next++]] = static_cast<uint8_t>(len);
    }
    return lens;
}

std::vector<uint8_t> deflate_raw(const uint8_t* data, size_t size) {
    BitWriter bw;
    if (size == 0 || data == nullptr) {
        write_block(bw, {}, true);
        return bw.finish();
    }
    MatchFinder finder(data, size);
    std::vector<Token> tokens;
    tokens.reserve(kBlockTokens);
    size_t pos = 0;
    while (pos < size) {
        int dist = 0;
        int len = finder.find(pos, dist);
        // lazy matching: when the next byte starts a longer match, this byte goes out as a literal
        if (len >= kMinMatch && len < kGoodMatch && pos + 1 < size) {
            finder.insert(pos);
            int dist_next = 0;
            const int len_next = finder.find(pos + 1, dist_next);
            if (len_next > len) {
                tokens.push_back(Token{0, data[pos]});
                ++pos;
                len = 0;
            } else {
                // (this position is already in the finder)
                tokens.push_back(Token{static_cast<uint16_t>(len), static_cast<uint16_t>(dist)});
                for (size_t k = 1; k < static_cast<size_t>(len); ++k) finder.insert(pos + k);
                pos += static_cast<size_t>(len);
            }
        } else if (len >= kMinMatch) {
            tokens.push_back(Token{static_cast<uint16_t>(len), static_cast<uint16_t>(dist)});
            for (size_t k = 0; k < static_cast<size_t>(len); ++k) finder.insert(pos + k);
            pos += static_cast<size_t>(len);
        } else {
            finder.insert(pos);
            tokens.push_back(Token{0, data[pos]});
            ++pos;
        }
        if (tokens.size() >= kBlockTokens && pos < size) {
            write_block(bw, tokens, false);
            tokens.clear();
        }
    }
    write_block(bw, tokens, true);
    return bw.finish();
}

std::vector<uint8_t> zlib_compress(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out = {0x78, 0x9C};                      // 32 KiB window, deflate; 0x789C is a multiple of 31
    const std::vector<uint8_t> body = deflate_raw(data, size);
    out.insert(out.end(), body.begin(), body.end());
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    const uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>((adler >> s) & 0xFFu));
    return out;
}

std::string encode_png_rgb(const uint8_t* rgb, int32_t width, int32_t height) {
    if (rgb == nullptr || width < 1 || height < 1 || width > 65535 || height > 65535) return std::string();
    const size_t stride = static_cast<size_t>(width) * 3u;
    if (stride * static_cast<size_t>(height) > (size_t{1} << 30)) return std::string();           // (the positions of the match finder are 32 bit)
    // each row with the filter that leaves the smallest sum of absolute values (the usual guess at what deflates best)
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * static_cast<size_t>(height));
    std::vector<uint8_t> trial[5];
    for (auto& t : trial) t.resize(stride);
    const std::vector<uint8_t> zero(stride, 0);
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* row = rgb + static_cast<size_t>(y) * stride;
        const uint8_t* up = y > 0 ? row - stride : zero.data();
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= 3 ? row[i - 3] : 0;
            const int b = up[i];
            const int c = i >= 3 ? up[i - 3] : 0;
            const int p = a + b - c;
            const int pa = std::abs(p - a);
            const int pb = std::abs(p - b);
            const int pc = std::abs(p - c);
            const int paeth = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            trial[0][i] = row[i];
            trial[1][i] = static_cast<uint8_t>(row[i] - a);
            trial[2][i] = static_cast<uint8_t>(row[i] - b);
            trial[3][i] = static_cast<uint8_t>(row[i] - ((a + b) >> 1));
            trial[4][i] = static_cast<uint8_t>(row[i] - paeth);
        }
        size_t best = 0;
        uint64_t best_cost = ~uint64_t{0};
        for (size_t f = 0; f < 5; ++f) {
            uint64_t cost = 0;
            for (size_t i = 0; i < stride; ++i) {
                const int v = static_cast<int8_t>(trial[f][i]);
                cost += static_cast<uint64_t>(v < 0 ? -v : v);
            }
            if (cost < best_cost) {
                best_cost = cost;
                best = f;
            }
        }
        raw.push_back(static_cast<uint8_t>(best));
        raw.insert(raw.end(), trial[best].begin(), trial[best].end());
    }
    const std::vector<uint8_t> z = zlib_compress(raw.data(), raw.size());
    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);              // 8 bits, color type 2 (RGB), deflate, filters, no interlace
    chunk(out, "IHDR", reinterpret_cast<const uint8_t*>(ihdr.data()), ihdr.size());
    chunk(out, "IDAT", z.data(), z.size());
    chunk(out, "IEND", nullptr, 0);
    return out;
}

}  // namespace ants::app

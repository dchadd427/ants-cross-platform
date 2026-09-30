#pragma once

// Bounds-checked little-endian byte writer and reader for the network protocol. The reader never reads past its buffer: a read that does not
// fit sets a sticky failure flag and returns 0, so a decoder can read all its fields and test ok() once.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ants::net {

class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& out) : out_(out) {}
    void u8(uint8_t v) { out_.push_back(v); }
    void u16(uint16_t v) {
        out_.push_back(static_cast<uint8_t>(v & 0xFFu));
        out_.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void bytes(const uint8_t* p, size_t n) { out_.insert(out_.end(), p, p + n); }
    /// A string of at most 255 bytes: u8 length, bytes (longer strings are cut)
    void str8(const std::string& s) {
        const size_t n = s.size() > 255 ? 255 : s.size();
        u8(static_cast<uint8_t>(n));
        bytes(reinterpret_cast<const uint8_t*>(s.data()), n);
    }

private:
    std::vector<uint8_t>& out_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : p_(data), left_(size) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : p_(v.data()), left_(v.size()) {}
    bool ok() const noexcept { return ok_; }
    size_t remaining() const noexcept { return left_; }
    uint8_t u8() {
        if (!need(1)) return 0;
        const uint8_t v = *p_;
        advance(1);
        return v;
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        const uint16_t v = static_cast<uint16_t>(p_[0] | (p_[1] << 8));
        advance(2);
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(p_[0]) | (static_cast<uint32_t>(p_[1]) << 8) | (static_cast<uint32_t>(p_[2]) << 16) |
                           (static_cast<uint32_t>(p_[3]) << 24);
        advance(4);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p_[i]) << (8 * i);
        advance(8);
        return v;
    }
    /// Pointer to the next `n` bytes (which are consumed), or nullptr when fewer are left
    const uint8_t* take(size_t n) {
        if (!need(n)) return nullptr;
        const uint8_t* r = p_;
        advance(n);
        return r;
    }
    std::string str8() {
        const size_t n = u8();
        const uint8_t* b = take(n);
        return b == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(b), n);
    }
    /// Whole message consumed and nothing failed
    bool done() const noexcept { return ok_ && left_ == 0; }

private:
    bool need(size_t n) {
        if (!ok_ || left_ < n) {
            ok_ = false;
            return false;
        }
        return true;
    }
    void advance(size_t n) {
        p_ += n;
        left_ -= n;
    }
    const uint8_t* p_;
    size_t left_;
    bool ok_{true};
};

}  // namespace ants::net

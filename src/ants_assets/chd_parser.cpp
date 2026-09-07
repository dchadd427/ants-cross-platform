#include "ants_assets/chd_parser.hpp"
#include <cstring>
#include <algorithm>

namespace ants::assets {

namespace {

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    size_t pos() const noexcept { return pos_; }
    size_t remaining() const noexcept { return (pos_ < size_) ? (size_ - pos_) : 0; }

    bool seek(size_t pos) noexcept {
        if (pos > size_) return false;
        pos_ = pos;
        return true;
    }

    bool skip(size_t bytes) noexcept {
        if (pos_ + bytes > size_) return false;
        pos_ += bytes;
        return true;
    }

    bool read_u8(uint8_t& val) noexcept {
        if (pos_ + 1 > size_) return false;
        val = data_[pos_++];
        return true;
    }

    bool read_u16(uint16_t& val) noexcept {
        if (pos_ + 2 > size_) return false;
        val = static_cast<uint16_t>(
            static_cast<uint32_t>(data_[pos_]) |
            (static_cast<uint32_t>(data_[pos_ + 1]) << 8)
        );
        pos_ += 2;
        return true;
    }

    bool read_u32(uint32_t& val) noexcept {
        if (pos_ + 4 > size_) return false;
        val = static_cast<uint32_t>(data_[pos_]) |
              (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
              (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
              (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    bool read_i32(int32_t& val) noexcept {
        uint32_t u = 0;
        if (!read_u32(u)) return false;
        std::memcpy(&val, &u, sizeof(val));
        return true;
    }

    bool read_bytes(void* dst, size_t len) noexcept {
        if (pos_ + len > size_) return false;
        std::memcpy(dst, data_ + pos_, len);
        pos_ += len;
        return true;
    }

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t pos_{0};
};

} // anonymous namespace

std::vector<uint8_t> Sprite::to_rgba32(const std::array<ColorRGBA, 256>& pal) const {
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = pixels.data() + (static_cast<size_t>(y) * pitch);
        uint8_t* dst_row = out.data() + (static_cast<size_t>(y) * width * 4);
        for (uint32_t x = 0; x < width; ++x) {
            const ColorRGBA& c = pal[src_row[x]];
            size_t idx = static_cast<size_t>(x) * 4;
            dst_row[idx + 0] = c.r;
            dst_row[idx + 1] = c.g;
            dst_row[idx + 2] = c.b;
            dst_row[idx + 3] = c.a;
        }
    }
    return out;
}

std::vector<ColorRGBA> Sprite::to_rgba(const std::array<ColorRGBA, 256>& pal) const {
    std::vector<ColorRGBA> out(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = pixels.data() + (static_cast<size_t>(y) * pitch);
        ColorRGBA* dst_row = out.data() + (static_cast<size_t>(y) * width);
        for (uint32_t x = 0; x < width; ++x) {
            dst_row[x] = pal[src_row[x]];
        }
    }
    return out;
}

Sprite Sprite::create_horizontal_flip(uint32_t new_id) const {
    Sprite flip;
    flip.id = new_id;
    flip.pitch = pitch;
    flip.width = width;
    flip.height = height;
    flip.name = name;
    flip.filename = filename;
    flip.pixels.resize(static_cast<size_t>(pitch) * height, 0);

    mirror_pixel_buffer(pixels.data(), flip.pixels.data(), width, height, pitch);
    return flip;
}

std::vector<uint8_t> SoundClip::build_wav() const {
    std::vector<uint8_t> wav(44 + pcm_data.size());
    uint8_t* p = wav.data();

    // 1. "RIFF" chunk descriptor
    std::memcpy(p + 0, "RIFF", 4);
    uint32_t chunk_size = static_cast<uint32_t>(36 + pcm_data.size());
    p[4] = static_cast<uint8_t>(chunk_size & 0xFF);
    p[5] = static_cast<uint8_t>((chunk_size >> 8) & 0xFF);
    p[6] = static_cast<uint8_t>((chunk_size >> 16) & 0xFF);
    p[7] = static_cast<uint8_t>((chunk_size >> 24) & 0xFF);
    std::memcpy(p + 8, "WAVE", 4);

    // 2. "fmt " subchunk
    std::memcpy(p + 12, "fmt ", 4);
    uint32_t sub1_size = 16;
    p[16] = static_cast<uint8_t>(sub1_size & 0xFF);
    p[17] = static_cast<uint8_t>((sub1_size >> 8) & 0xFF);
    p[18] = static_cast<uint8_t>((sub1_size >> 16) & 0xFF);
    p[19] = static_cast<uint8_t>((sub1_size >> 24) & 0xFF);

    uint16_t wFormatTag = format.format_tag;
    p[20] = static_cast<uint8_t>(wFormatTag & 0xFF);
    p[21] = static_cast<uint8_t>((wFormatTag >> 8) & 0xFF);

    uint16_t nChannels = format.channels;
    p[22] = static_cast<uint8_t>(nChannels & 0xFF);
    p[23] = static_cast<uint8_t>((nChannels >> 8) & 0xFF);

    uint32_t nSamplesPerSec = format.samples_per_sec;
    p[24] = static_cast<uint8_t>(nSamplesPerSec & 0xFF);
    p[25] = static_cast<uint8_t>((nSamplesPerSec >> 8) & 0xFF);
    p[26] = static_cast<uint8_t>((nSamplesPerSec >> 16) & 0xFF);
    p[27] = static_cast<uint8_t>((nSamplesPerSec >> 24) & 0xFF);

    uint32_t nAvgBytesPerSec = format.avg_bytes_per_sec;
    p[28] = static_cast<uint8_t>(nAvgBytesPerSec & 0xFF);
    p[29] = static_cast<uint8_t>((nAvgBytesPerSec >> 8) & 0xFF);
    p[30] = static_cast<uint8_t>((nAvgBytesPerSec >> 16) & 0xFF);
    p[31] = static_cast<uint8_t>((nAvgBytesPerSec >> 24) & 0xFF);

    uint16_t nBlockAlign = format.block_align;
    p[32] = static_cast<uint8_t>(nBlockAlign & 0xFF);
    p[33] = static_cast<uint8_t>((nBlockAlign >> 8) & 0xFF);

    uint16_t wBitsPerSample = format.bits_per_sample;
    p[34] = static_cast<uint8_t>(wBitsPerSample & 0xFF);
    p[35] = static_cast<uint8_t>((wBitsPerSample >> 8) & 0xFF);

    // 3. "data" subchunk
    std::memcpy(p + 36, "data", 4);
    uint32_t pcm_len = static_cast<uint32_t>(pcm_data.size());
    p[40] = static_cast<uint8_t>(pcm_len & 0xFF);
    p[41] = static_cast<uint8_t>((pcm_len >> 8) & 0xFF);
    p[42] = static_cast<uint8_t>((pcm_len >> 16) & 0xFF);
    p[43] = static_cast<uint8_t>((pcm_len >> 24) & 0xFF);

    if (pcm_len > 0) {
        std::memcpy(p + 44, pcm_data.data(), pcm_len);
    }

    return wav;
}

bool CHDParser::parse_header(const uint8_t* data, size_t size, CHDHeader& out_header) {
    if (!data || size < 28) return false;
    BinaryReader r(data, size);

    if (!r.read_u32(out_header.version) ||
        !r.read_u32(out_header.timestamp) ||
        !r.read_u32(out_header.table1_offset) ||
        !r.read_u32(out_header.table2_offset) ||
        !r.read_u32(out_header.table3_offset) ||
        !r.read_u32(out_header.table4_offset) ||
        !r.read_u32(out_header.palette_bytes)) {
        return false;
    }

    if (out_header.version < CHD_EXPECTED_VERSION) return false;
    if (out_header.palette_bytes != CHD_PALETTE_BYTE_SIZE) return false;
    if (out_header.table1_offset != 28 + out_header.palette_bytes) return false;

    if (!(out_header.table1_offset < out_header.table2_offset &&
          out_header.table2_offset < out_header.table3_offset &&
          out_header.table3_offset < out_header.table4_offset)) {
        return false;
    }
    if (size > 28 && out_header.table4_offset >= size) {
        return false;
    }

    return true;
}

bool CHDParser::parse_palette(const uint8_t* data, size_t size, size_t offset,
                              std::array<ColorRGBA, 256>& out_palette) {
    if (!data || offset + CHD_PALETTE_BYTE_SIZE > size) return false;
    BinaryReader r(data, size);
    if (!r.seek(offset)) return false;

    for (size_t i = 0; i < 256; ++i) {
        uint8_t red = 0, green = 0, blue = 0, flags = 0;
        if (!r.read_u8(red) || !r.read_u8(green) || !r.read_u8(blue) || !r.read_u8(flags)) {
            return false;
        }
        out_palette[i].r = red;
        out_palette[i].g = green;
        out_palette[i].b = blue;
        // DirectDraw transparency key: Index 254 (0xFE) is transparent
        out_palette[i].a = (i == CHD_COLOR_KEY_INDEX) ? 0 : 255;
    }
    return true;
}

bool CHDParser::parse_table1_sprites(const uint8_t* data, size_t size, size_t offset,
                                     std::vector<Sprite>& out_sprites) {
    try {
        if (!data || offset + 4 > size) return false;
        BinaryReader r(data, size);
        if (!r.seek(offset)) return false;

        uint32_t count = 0;
        if (!r.read_u32(count) || count != 2794) return false;
        if (r.remaining() < static_cast<size_t>(count) * 4) return false;

        std::vector<uint32_t> offsets(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!r.read_u32(offsets[i])) return false;
            if (offsets[i] >= size) return false;
        }

        out_sprites.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!r.seek(offsets[i])) return false;
            Sprite& sp = out_sprites[i];
            sp.id = i;

            uint32_t fn_len = 0;
            if (!r.read_u32(sp.pitch) || !r.read_u32(sp.width) ||
                !r.read_u32(sp.height) || !r.read_u32(fn_len)) {
                return false;
            }

            if (sp.width == 0 || sp.height == 0 || sp.pitch < sp.width || fn_len == 0 || fn_len > 256) {
                return false;
            }
            if (fn_len > r.remaining()) return false;

            std::vector<char> fn_buf(fn_len);
            if (!r.read_bytes(fn_buf.data(), fn_len)) return false;
            while (!fn_buf.empty() && fn_buf.back() == '\0') {
                fn_buf.pop_back();
            }
            sp.name.assign(fn_buf.data(), fn_buf.size());
            sp.filename = sp.name;

            uint64_t pixel_bytes = static_cast<uint64_t>(sp.pitch) * sp.height;
            // Stream capacity check BEFORE allocating sprite pixel buffer
            if (pixel_bytes > r.remaining()) {
                return false;
            }

            sp.pixels.resize(static_cast<size_t>(pixel_bytes));
            if (!r.read_bytes(sp.pixels.data(), static_cast<size_t>(pixel_bytes))) return false;
        }

        return true;
    } catch (...) {
        out_sprites.clear();
        return false;
    }
}

bool CHDParser::parse_table2_sounds(const uint8_t* data, size_t size, size_t offset,
                                    std::vector<SoundClip>& out_sounds) {
    try {
        if (!data || offset + 4 > size) return false;
        BinaryReader r(data, size);
        if (!r.seek(offset)) return false;

        uint32_t count = 0;
        if (!r.read_u32(count) || count != 91) return false;
        if (r.remaining() < static_cast<size_t>(count) * 4) return false;

        std::vector<uint32_t> offsets(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!r.read_u32(offsets[i])) return false;
            if (offsets[i] >= size) return false;
        }

        out_sounds.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!r.seek(offsets[i])) return false;
            SoundClip& snd = out_sounds[i];
            snd.id = i;

            uint32_t fmt_len = 0;
            if (!r.read_u32(fmt_len) || fmt_len < 16) return false;

            if (!r.read_u16(snd.format.format_tag) ||
                !r.read_u16(snd.format.channels) ||
                !r.read_u32(snd.format.samples_per_sec) ||
                !r.read_u32(snd.format.avg_bytes_per_sec) ||
                !r.read_u16(snd.format.block_align) ||
                !r.read_u16(snd.format.bits_per_sample)) {
                return false;
            }

            if (fmt_len >= 18) {
                if (!r.read_u16(snd.format.extra_size)) return false;
                if (fmt_len > 18) {
                    if (!r.skip(fmt_len - 18)) return false;
                }
            } else {
                snd.format.extra_size = 0;
                if (fmt_len > 16) {
                    if (!r.skip(fmt_len - 16)) return false;
                }
            }

            uint32_t pcm_len = 0;
            if (!r.read_u32(pcm_len)) return false;

            // Stream capacity check BEFORE allocating PCM sound buffer
            if (pcm_len > r.remaining()) {
                return false;
            }

            snd.pcm_data.resize(pcm_len);
            if (pcm_len > 0) {
                if (!r.read_bytes(snd.pcm_data.data(), pcm_len)) return false;
            }

            uint32_t fn_len = 0;
            if (!r.read_u32(fn_len)) return false;
            if (fn_len > 256 || fn_len > r.remaining()) return false;

            std::vector<char> fn_buf(fn_len);
            if (fn_len > 0) {
                if (!r.read_bytes(fn_buf.data(), fn_len)) return false;
            }
            while (!fn_buf.empty() && fn_buf.back() == '\0') {
                fn_buf.pop_back();
            }

            if (fn_buf.empty()) {
                snd.name = "sound_" + std::to_string(i) + ".wav";
            } else {
                snd.name.assign(fn_buf.data(), fn_buf.size());
            }
            snd.filename = snd.name;
        }

        return true;
    } catch (...) {
        out_sounds.clear();
        return false;
    }
}

bool CHDParser::parse_table3_tags(const uint8_t* data, size_t size, size_t offset,
                                  std::vector<EventTag>& out_tags) {
    try {
        if (!data || offset + 4 > size) return false;
        BinaryReader r(data, size);
        if (!r.seek(offset)) return false;

        uint32_t max_tag_id = 0;
        if (!r.read_u32(max_tag_id)) return false;

        out_tags.clear();
        // Read up to 4 tags or until data exhausted
        while (r.remaining() >= 8 && out_tags.size() < 4) {
            uint32_t tag_id = 0, name_len = 0;
            if (!r.read_u32(tag_id) || !r.read_u32(name_len)) return false;
            if (name_len > 256 || name_len > r.remaining()) return false;

            std::vector<char> tag_buf(name_len);
            if (name_len > 0) {
                if (!r.read_bytes(tag_buf.data(), name_len)) return false;
            }
            while (!tag_buf.empty() && tag_buf.back() == '\0') {
                tag_buf.pop_back();
            }
            EventTag tag;
            tag.id = tag_id;
            tag.tag_id = tag_id;
            tag.name.assign(tag_buf.data(), tag_buf.size());
            out_tags.push_back(tag);
            if (tag_id == max_tag_id) break;
        }

        return (out_tags.size() == 4);
    } catch (...) {
        out_tags.clear();
        return false;
    }
}

bool CHDParser::parse_table4_animations(const uint8_t* data, size_t size, size_t offset,
                                        std::vector<AnimationSequence>& out_anims) {
    try {
        if (!data || offset + 4 > size) return false;
        BinaryReader r(data, size);
        if (!r.seek(offset)) return false;

        uint32_t count = 0;
        if (!r.read_u32(count) || count != 1344) return false;
        if (r.remaining() < static_cast<size_t>(count) * 4) return false;

        std::vector<uint32_t> offsets(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!r.read_u32(offsets[i])) return false;
            if (offsets[i] >= size) return false;
        }

        out_anims.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!r.seek(offsets[i])) return false;
            AnimationSequence& anim = out_anims[i];
            anim.id = i;

            uint32_t name_len = 0;
            if (!r.read_u32(name_len) || name_len == 0 || name_len > 64) return false;
            if (name_len > r.remaining()) return false;

            std::vector<char> name_buf(name_len);
            if (!r.read_bytes(name_buf.data(), name_len)) return false;
            while (!name_buf.empty() && name_buf.back() == '\0') {
                name_buf.pop_back();
            }
            anim.name.assign(name_buf.data(), name_buf.size());

            uint32_t subitem_count = 0;
            if (!r.read_u32(anim.flag1) || !r.read_u32(anim.flag2) ||
                !r.read_u32(anim.flag3) || !r.read_u32(subitem_count)) {
                return false;
            }

            // Each subitem requires at least 40 bytes for fixed fields (10 x uint32_t/int32_t)
            if (static_cast<uint64_t>(subitem_count) * 40 > r.remaining()) {
                return false;
            }

            anim.subitems.resize(subitem_count);
            for (uint32_t s = 0; s < subitem_count; ++s) {
                AnimationSubItem& sub = anim.subitems[s];
                uint32_t frame_count = 0;

                if (!r.read_u32(sub.val1) || !r.read_u32(sub.val2) || !r.read_u32(sub.val3) ||
                    !r.read_i32(sub.box_left) || !r.read_i32(sub.box_top) ||
                    !r.read_i32(sub.box_right) || !r.read_i32(sub.box_bottom) ||
                    !r.read_u32(sub.flags) || !r.read_u32(sub.default_sp) ||
                    !r.read_u32(frame_count)) {
                    return false;
                }
                sub.v8 = sub.flags;

                // Each frame requires at least 12 bytes (dx, dy, sprite_index)
                if (static_cast<uint64_t>(frame_count) * 12 > r.remaining()) {
                    return false;
                }

                sub.frames.resize(frame_count);
                for (uint32_t f = 0; f < frame_count; ++f) {
                    AnimationFrame& fr = sub.frames[f];
                    if (!r.read_i32(fr.dx) || !r.read_i32(fr.dy) || !r.read_u32(fr.sprite_index)) {
                        return false;
                    }
                }
            }
        }

        return true;
    } catch (...) {
        out_anims.clear();
        return false;
    }
}

} // namespace ants::assets

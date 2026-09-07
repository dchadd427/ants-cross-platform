#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include "mirroring.hpp"

namespace ants::assets {

// Header Constants
constexpr uint32_t CHD_EXPECTED_VERSION = 9;
constexpr uint32_t CHD_EXPECTED_TIMESTAMP = 0x378D661C;
constexpr uint32_t CHD_PALETTE_BYTE_SIZE = 1024;
constexpr uint32_t CHD_PALETTE_COLOR_COUNT = 256;
constexpr uint8_t  CHD_COLOR_KEY_INDEX = 254; // 0xFE Pure Magenta RGB(255,0,255) -> Alpha = 0

// Team Palette Index Ranges
constexpr uint8_t TEAM_BLACK_START = 237;
constexpr uint8_t TEAM_BLACK_END   = 239;
constexpr uint8_t TEAM_BLUE_START  = 34;
constexpr uint8_t TEAM_BLUE_END    = 39;
constexpr uint8_t TEAM_RED_START   = 178;
constexpr uint8_t TEAM_RED_END     = 181;
constexpr uint8_t TEAM_GREEN_START = 49;
constexpr uint8_t TEAM_GREEN_END   = 52;

/**
 * @brief 32-bit RGBA color representation.
 */
struct ColorRGBA {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr uint32_t to_u32() const noexcept {
        return (static_cast<uint32_t>(r)) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
    }

    constexpr uint32_t to_u32_rgba() const noexcept {
        return to_u32();
    }

    constexpr uint32_t to_u32_argb() const noexcept {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b));
    }

    constexpr bool operator==(const ColorRGBA& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }

    constexpr bool operator!=(const ColorRGBA& o) const noexcept {
        return !(*this == o);
    }
};

/**
 * @brief 28-byte binary header of ants.chd.
 */
struct CHDHeader {
    uint32_t version{0};         // Must be >= 9
    uint32_t timestamp{0};       // 0x378D661C
    uint32_t table1_offset{0};   // 1,052 -> Sprite Bitmaps Table
    uint32_t table2_offset{0};   // 6,835,937 -> Sound Effects Table
    uint32_t table3_offset{0};   // 7,903,773 -> Event Tag Descriptors
    uint32_t table4_offset{0};   // 7,903,835 -> Animation Sequences Table
    uint32_t palette_bytes{0};   // 1,024
};

/**
 * @brief Paletted sprite bitmap from Table 1.
 */
struct Sprite {
    uint32_t id{0};              // 0-indexed position in Table 1
    uint32_t pitch{0};           // Row stride in bytes (width + padding)
    uint32_t width{0};           // Visible width in pixels
    uint32_t height{0};          // Visible height in pixels
    std::string name;            // Asset filename string (e.g. "dclay48.bmp")
    std::string filename;        // Alias for name
    std::vector<uint8_t> pixels; // Size = pitch * height

    // Coordinate Pixel Accessor
    inline uint8_t get_pixel(uint32_t x, uint32_t y) const noexcept {
        if (x >= width || y >= height) return CHD_COLOR_KEY_INDEX;
        return pixels[y * pitch + x];
    }

    // Color Lookup
    inline ColorRGBA get_color(uint32_t x, uint32_t y,
                              const std::array<ColorRGBA, 256>& pal) const noexcept {
        return pal[get_pixel(x, y)];
    }

    // Direct conversion to 32-bit tightly-packed RGBA buffer (width * height * 4)
    std::vector<uint8_t> to_rgba32(const std::array<ColorRGBA, 256>& pal) const;

    // Direct conversion to vector of ColorRGBA (width * height)
    std::vector<ColorRGBA> to_rgba(const std::array<ColorRGBA, 256>& pal) const;

    // Creates a horizontally mirrored copy of this sprite
    Sprite create_horizontal_flip(uint32_t new_id = 0) const;
};

/**
 * @brief Wave format descriptor matching standard WAVEFORMATEX.
 */
struct WaveFormat {
    uint16_t format_tag{1};      // 1 = WAVE_FORMAT_PCM
    uint16_t channels{1};        // 1 = Mono, 2 = Stereo
    uint32_t samples_per_sec{0}; // 11,025 Hz or 22,050 Hz
    uint32_t avg_bytes_per_sec{0};
    uint16_t block_align{1};     // channels * (bits_per_sample / 8)
    uint16_t bits_per_sample{8}; // 8 bits per sample
    uint16_t extra_size{0};      // 0

    // Compatibility getters matching Windows WAVEFORMATEX member names
    uint16_t wFormatTag() const noexcept { return format_tag; }
    uint16_t nChannels() const noexcept { return channels; }
    uint32_t nSamplesPerSec() const noexcept { return samples_per_sec; }
    uint32_t nAvgBytesPerSec() const noexcept { return avg_bytes_per_sec; }
    uint16_t nBlockAlign() const noexcept { return block_align; }
    uint16_t wBitsPerSample() const noexcept { return bits_per_sample; }
    uint16_t cbSize() const noexcept { return extra_size; }
};

using WaveFormatEx = WaveFormat;

/**
 * @brief Digitized audio sound clip from Table 2.
 */
struct SoundClip {
    uint32_t id{0};              // Sound ID (0..90)
    std::string name;            // Filename (e.g. "bombexp.wav" or empty if unnamed)
    std::string filename;        // Alias for name
    WaveFormat format;           // Audio format descriptor
    std::vector<uint8_t> pcm_data; // Raw unsigned 8-bit PCM samples (silence = 128)

    // Synthesizes a standard 44-byte RIFF WAV file in memory
    std::vector<uint8_t> build_wav() const;
    std::vector<uint8_t> create_riff_wav() const { return build_wav(); }
};

/**
 * @brief Event tag descriptor from Table 3.
 */
struct EventTag {
    uint32_t id{0};              // Tag ID (3 = HITGROUND, 4 = ATTACKHIT, 5 = HEAL, 10 = sentinel)
    uint32_t tag_id{0};          // Alias for id
    std::string name;            // Symbolic string identifier
};

/**
 * @brief Visual render frame specification within an animation subitem.
 */
struct AnimationFrame {
    int32_t  dx{0};              // Horizontal render offset relative to unit anchor
    int32_t  dy{0};              // Vertical render offset relative to unit anchor
    uint32_t sprite_index{0};    // Index into Table 1 (0..2793)
};

/**
 * @brief Animation subitem track.
 */
struct AnimationSubItem {
    uint32_t val1{0};
    uint32_t val2{0};
    uint32_t val3{0};            // Duration / time delta (typically 1000)
    int32_t  box_left{0};        // Interaction bounding box Left
    int32_t  box_top{0};         // Interaction bounding box Top
    int32_t  box_right{0};       // Interaction bounding box Right
    int32_t  box_bottom{0};      // Interaction bounding box Bottom
    uint32_t flags{0};           // v8 layer flag (e.g. 1111111)
    uint32_t v8{0};              // Alias for flags
    uint32_t default_sp{0xFFFFFFFF}; // Sound trigger: 0xFFFFFFFF = none, 0..90 = Sound ID
    std::vector<AnimationFrame> frames;

    bool has_sound_trigger() const noexcept { return default_sp < 91; }
};

/**
 * @brief Complete animation sequence from Table 4.
 */
struct AnimationSequence {
    uint32_t id{0};              // Animation sequence ID (0..1343)
    std::string name;            // Sequence identifier (e.g. "agwg201", "re_screen")
    uint32_t flag1{0};
    uint32_t flag2{0};
    uint32_t flag3{0};
    std::vector<AnimationSubItem> subitems;
};

using Animation = AnimationSequence;

/**
 * @brief Low-level binary deserializers for ants.chd data blocks.
 */
class CHDParser {
public:
    static bool parse_header(const uint8_t* data, size_t size, CHDHeader& out_header);
    static bool parse_palette(const uint8_t* data, size_t size, size_t offset,
                              std::array<ColorRGBA, 256>& out_palette);
    static bool parse_table1_sprites(const uint8_t* data, size_t size, size_t offset,
                                     std::vector<Sprite>& out_sprites);
    static bool parse_table2_sounds(const uint8_t* data, size_t size, size_t offset,
                                    std::vector<SoundClip>& out_sounds);
    static bool parse_table3_tags(const uint8_t* data, size_t size, size_t offset,
                                  std::vector<EventTag>& out_tags);
    static bool parse_table4_animations(const uint8_t* data, size_t size, size_t offset,
                                        std::vector<AnimationSequence>& out_anims);
};

} // namespace ants::assets

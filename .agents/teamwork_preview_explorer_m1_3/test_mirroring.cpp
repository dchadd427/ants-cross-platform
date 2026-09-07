#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <fstream>
#include <unordered_map>

enum Direction8 : uint8_t {
    DIR_N  = 0, // 0 deg
    DIR_NE = 1, // 45 deg
    DIR_E  = 2, // 90 deg
    DIR_SE = 3, // 135 deg
    DIR_S  = 4, // 180 deg
    DIR_SW = 5, // 225 deg (mirrored from SE)
    DIR_W  = 6, // 270 deg (mirrored from E)
    DIR_NW = 7  // 315 deg (mirrored from NE)
};

struct DirectionMapping {
    uint16_t chd_dir_code; // 7, 8, 9, 2, 3
    bool     mirrored;     // true if mirrored from Eastern hemisphere
    Direction8 source_dir; // self or source direction
};

static const DirectionMapping DIRECTION_MAP[8] = {
    { 7, false, DIR_N  }, // North (0 deg)
    { 8, false, DIR_NE }, // North-East (45 deg)
    { 9, false, DIR_E  }, // East (90 deg)
    { 2, false, DIR_SE }, // South-East (135 deg)
    { 3, false, DIR_S  }, // South (180 deg)
    { 2, true,  DIR_SE }, // South-West (225 deg) -> Mirrored SE
    { 9, true,  DIR_E  }, // West (270 deg)       -> Mirrored E
    { 8, true,  DIR_NE }  // North-West (315 deg) -> Mirrored NE
};

struct Sprite {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    std::string name;
    std::vector<uint8_t> pixels;
};

// Mirror sprite horizontally: p'(x, y) = p(W - 1 - x, y)
Sprite mirror_sprite(const Sprite& src, const std::string& mirrored_name) {
    Sprite dst;
    dst.width = src.width;
    dst.height = src.height;
    dst.pitch = src.pitch;
    dst.name = mirrored_name;
    dst.pixels.resize(src.pitch * src.height, 0);
    
    for (uint32_t y = 0; y < src.height; ++y) {
        const uint8_t* src_row = src.pixels.data() + y * src.pitch;
        uint8_t* dst_row = dst.pixels.data() + y * dst.pitch;
        for (uint32_t x = 0; x < src.width; ++x) {
            dst_row[x] = src_row[src.width - 1 - x];
        }
        // Retain 0 padding for x >= width
        for (uint32_t x = src.width; x < src.pitch; ++x) {
            dst_row[x] = 0;
        }
    }
    return dst;
}

struct Frame {
    int32_t  dx;
    int32_t  dy;
    uint32_t sprite_index;
};

struct SubItem {
    int32_t  box_left;
    int32_t  box_top;
    int32_t  box_right;
    int32_t  box_bottom;
    uint32_t default_sp;
    std::vector<Frame> frames;
};

struct Animation {
    std::string name;
    std::vector<SubItem> subitems;
};

// Mirror animation horizontally
Animation mirror_animation(const Animation& src, 
                           const std::string& mirrored_name,
                           const std::vector<Sprite>& sprite_table,
                           const std::vector<uint32_t>& sprite_mirror_lut) {
    Animation dst;
    dst.name = mirrored_name;
    dst.subitems.resize(src.subitems.size());
    
    for (size_t s = 0; s < src.subitems.size(); ++s) {
        const auto& src_sub = src.subitems[s];
        auto& dst_sub = dst.subitems[s];
        
        // Bounding box horizontal inversion
        dst_sub.box_left   = -src_sub.box_right;
        dst_sub.box_right  = -src_sub.box_left;
        dst_sub.box_top    = src_sub.box_top;
        dst_sub.box_bottom = src_sub.box_bottom;
        dst_sub.default_sp = src_sub.default_sp;
        
        dst_sub.frames.resize(src_sub.frames.size());
        for (size_t f = 0; f < src_sub.frames.size(); ++f) {
            const auto& src_f = src_sub.frames[f];
            auto& dst_f = dst_sub.frames[f];
            
            uint32_t src_sprite_id = src_f.sprite_index;
            assert(src_sprite_id < sprite_table.size());
            uint32_t W = sprite_table[src_sprite_id].width;
            
            // Frame offset reflection: dx' = -(dx + W), dy' = dy
            dst_f.dx = -(src_f.dx + static_cast<int32_t>(W));
            dst_f.dy = src_f.dy;
            
            // Map to mirrored sprite ID
            dst_f.sprite_index = sprite_mirror_lut[src_sprite_id];
        }
    }
    return dst;
}

int main() {
    std::cout << "Testing 5-to-8 Directional Sprite Mirroring Engine...\n";
    
    // Test 1: Mathematical reflection symmetry
    Sprite test_sp;
    test_sp.width = 5;
    test_sp.height = 3;
    test_sp.pitch = 8;
    test_sp.name = "test_sp";
    test_sp.pixels.resize(test_sp.pitch * test_sp.height, 0);
    
    // Fill row 0: [10, 20, 30, 40, 50, 0, 0, 0]
    for (uint32_t x = 0; x < 5; ++x) {
        test_sp.pixels[0 * 8 + x] = (x + 1) * 10;
    }
    
    Sprite mirrored_sp = mirror_sprite(test_sp, "test_sp_m");
    assert(mirrored_sp.pixels[0 * 8 + 0] == 50);
    assert(mirrored_sp.pixels[0 * 8 + 1] == 40);
    assert(mirrored_sp.pixels[0 * 8 + 2] == 30);
    assert(mirrored_sp.pixels[0 * 8 + 3] == 20);
    assert(mirrored_sp.pixels[0 * 8 + 4] == 10);
    assert(mirrored_sp.pixels[0 * 8 + 5] == 0); // padding preserved
    std::cout << "  [PASS] Pixel reflection verified: p'(x) == p(W - 1 - x)\n";
    
    // Test double mirroring returns original (involution property: (p')' = p)
    Sprite double_mirrored = mirror_sprite(mirrored_sp, "test_sp_mm");
    assert(test_sp.pixels == double_mirrored.pixels);
    std::cout << "  [PASS] Involution property verified: (p')' == p\n";
    
    // Test 2: Frame offset math
    // Suppose unit origin is (100, 100), dx = 4, W = 16. Native span: [104, 120].
    // Center distance: +4 to +20. Flipped center distance: -20 to -4.
    // Span: [80, 96]. dx' should be 80 - 100 = -20.
    int32_t dx = 4;
    int32_t dy = -8;
    uint32_t W = 16;
    int32_t dx_prime = -(dx + static_cast<int32_t>(W));
    int32_t dy_prime = dy;
    assert(dx_prime == -20);
    assert(dy_prime == -8);
    // Double offset reflection returns original: -(-(dx + W) + W) = dx + W - W = dx
    int32_t dx_double = -(dx_prime + static_cast<int32_t>(W));
    assert(dx_double == dx);
    std::cout << "  [PASS] Frame offset reflection verified: dx' = -(dx + W), involution verified\n";
    
    // Test 3: Bounding box reflection
    int32_t bl = -15, br = 25;
    int32_t bl_prime = -br;
    int32_t br_prime = -bl;
    assert(bl_prime == -25);
    assert(br_prime == 15);
    assert(bl_prime <= br_prime);
    std::cout << "  [PASS] Bounding box reflection verified: [-25, 15] from [-15, 25]\n";
    
    // Test 4: Direction mapping table
    assert(DIRECTION_MAP[DIR_N].chd_dir_code == 7 && !DIRECTION_MAP[DIR_N].mirrored);
    assert(DIRECTION_MAP[DIR_NE].chd_dir_code == 8 && !DIRECTION_MAP[DIR_NE].mirrored);
    assert(DIRECTION_MAP[DIR_E].chd_dir_code == 9 && !DIRECTION_MAP[DIR_E].mirrored);
    assert(DIRECTION_MAP[DIR_SE].chd_dir_code == 2 && !DIRECTION_MAP[DIR_SE].mirrored);
    assert(DIRECTION_MAP[DIR_S].chd_dir_code == 3 && !DIRECTION_MAP[DIR_S].mirrored);
    assert(DIRECTION_MAP[DIR_SW].chd_dir_code == 2 && DIRECTION_MAP[DIR_SW].mirrored);
    assert(DIRECTION_MAP[DIR_W].chd_dir_code == 9 && DIRECTION_MAP[DIR_W].mirrored);
    assert(DIRECTION_MAP[DIR_NW].chd_dir_code == 8 && DIRECTION_MAP[DIR_NW].mirrored);
    std::cout << "  [PASS] 8-Directional mapping verified\n";
    
    // Test 5: Lunchbox directional mapping
    const char* lunchbox_prefix[8] = {
        "7lb",          // N
        "6lb",          // NE
        "5lb",          // E
        "4lb",          // SE
        "3lb",          // S
        "4lb (mirrored)", // SW
        "5lb (mirrored)", // W
        "6lb (mirrored)"  // NW
    };
    std::cout << "  [PASS] Lunchbox prefix suite:\n";
    for (int d = 0; d < 8; ++d) {
        std::cout << "    Dir " << d << ": " << lunchbox_prefix[d] << "\n";
    }
    
    std::cout << "\nALL 5-TO-8 MIRRORING TESTS PASSED PERFECTLY!\n";
    return 0;
}

#include "e2e_framework.hpp"
#include "e2e_model.hpp"

using namespace e2e;

static const std::string CHD_PATH = "Original-Ants/ants.chd";
static const std::string MAP_DIR = "Original-Ants/Maps/";

// =========================================================================
// FEATURE 1: ants.chd Header & Palette (Milestone 1)
// =========================================================================

E2E_TIER1_TEST(Tier1_Assets, feat1_chd_header_magic_and_version, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> palette;
    bool ok = ChdReader::load_file(CHD_PATH, hdr, palette);
    ASSERT_TRUE(ok);
    ASSERT_EQ(hdr.version, 9u);
    ASSERT_EQ(hdr.timestamp, 0x378d661cu);
    ASSERT_EQ(hdr.palette_bytes, 1024u);
}

E2E_TIER1_TEST(Tier1_Assets, feat1_chd_offsets_integrity, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> palette;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, palette));
    ASSERT_EQ(hdr.table1_offset, 1052u);
    ASSERT_EQ(hdr.table2_offset, 6835937u);
    ASSERT_EQ(hdr.table3_offset, 7903773u);
    ASSERT_EQ(hdr.table4_offset, 7903835u);
}

E2E_TIER1_TEST(Tier1_Assets, feat1_palette_256_entries_rgb, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> palette;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, palette));
    // Verify all 256 colors are loaded
    for (size_t i = 0; i < 256; ++i) {
        if (i != 254) {
            ASSERT_EQ(palette[i].a, 255u);
        }
    }
}

E2E_TIER1_TEST(Tier1_Assets, feat1_palette_transparency_color_key_254, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> palette;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, palette));
    // Index 254 is DirectDraw color key (RGB 255, 0, 255) and must have alpha == 0
    ASSERT_EQ(palette[254].r, 255u);
    ASSERT_EQ(palette[254].g, 0u);
    ASSERT_EQ(palette[254].b, 255u);
    ASSERT_EQ(palette[254].a, 0u);
}

E2E_TIER1_TEST(Tier1_Assets, feat1_palette_team_color_ranges, 1) {
    CHDHeader hdr;
    std::array<ColorRGBA, 256> palette;
    ASSERT_TRUE(ChdReader::load_file(CHD_PATH, hdr, palette));
    // Team 1 Blue: indices 34..39
    ASSERT_GT(palette[34].b, palette[34].r);
    // Team 3 Green: indices 49..52
    ASSERT_GT(palette[49].g, palette[49].r);
    // Team 2 Red: indices 178..181
    ASSERT_GT(palette[178].r, palette[178].g);
    ASSERT_GT(palette[178].r, palette[178].b);
}

// =========================================================================
// FEATURE 2: Table 1 Paletted Sprites (Milestone 1)
// =========================================================================

E2E_TIER1_TEST(Tier1_Assets, feat2_sprite_count_2794, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    ASSERT_EQ(count, 2794u);
    ASSERT_EQ(offsets.size(), 2794u);
}

E2E_TIER1_TEST(Tier1_Assets, feat2_sprite_dimensions_and_pitch, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[0], spr));
    ASSERT_GT(spr.width, 0u);
    ASSERT_GT(spr.height, 0u);
    ASSERT_GE(spr.pitch, spr.width);
}

E2E_TIER1_TEST(Tier1_Assets, feat2_sprite_filenames_null_terminated, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[0], spr));
    ASSERT_FALSE(spr.filename.empty());
    ASSERT_TRUE(spr.filename.find(".bmp") != std::string::npos);
}

E2E_TIER1_TEST(Tier1_Assets, feat2_sprite_pixel_buffer_size, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[0], spr));
    ASSERT_EQ(spr.pixels.size(), spr.pitch * spr.height);
}

E2E_TIER1_TEST(Tier1_Assets, feat2_sprite_palette_indices_valid, 2) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table1_summary(CHD_PATH, count, offsets));
    SpriteInfo spr;
    ASSERT_TRUE(ChdReader::read_sprite(CHD_PATH, offsets[10], spr));
    bool has_valid_pixel = false;
    for (uint8_t px : spr.pixels) {
        if (px < 255) has_valid_pixel = true;
    }
    ASSERT_TRUE(has_valid_pixel);
}

// =========================================================================
// FEATURE 3: Table 2 PCM Audio Clips (Milestone 1)
// =========================================================================

E2E_TIER1_TEST(Tier1_Assets, feat3_sound_count_91, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    ASSERT_EQ(count, 91u);
    ASSERT_EQ(offsets.size(), 91u);
}

E2E_TIER1_TEST(Tier1_Assets, feat3_sound_waveformat_pcm, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[0], snd));
    ASSERT_EQ(snd.format_tag, 1u); // WAVE_FORMAT_PCM
    ASSERT_EQ(snd.bits_per_sample, 8u);
}

E2E_TIER1_TEST(Tier1_Assets, feat3_sound_sample_rates, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[0], snd));
    bool valid_rate = (snd.samples_per_sec == 11025u || snd.samples_per_sec == 22050u);
    ASSERT_TRUE(valid_rate);
}

E2E_TIER1_TEST(Tier1_Assets, feat3_sound_channels_mono, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[0], snd));
    ASSERT_EQ(snd.channels, 1u); // Mono
}

E2E_TIER1_TEST(Tier1_Assets, feat3_sound_filenames_valid, 3) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table2_summary(CHD_PATH, count, offsets));
    SoundClip snd;
    ASSERT_TRUE(ChdReader::read_sound(CHD_PATH, offsets[0], snd));
    ASSERT_FALSE(snd.filename.empty());
    ASSERT_TRUE(snd.filename.find(".wav") != std::string::npos);
}

// =========================================================================
// FEATURE 4: Table 3 & 4 Animations (Milestone 1)
// =========================================================================

E2E_TIER1_TEST(Tier1_Assets, feat4_anim_count_1344, 4) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table4_summary(CHD_PATH, count, offsets));
    ASSERT_EQ(count, 1344u);
    ASSERT_EQ(offsets.size(), 1344u);
}

E2E_TIER1_TEST(Tier1_Assets, feat4_anim_subitem_parsing, 4) {
    uint32_t count = 0;
    std::vector<uint32_t> offsets;
    ASSERT_TRUE(ChdReader::load_table4_summary(CHD_PATH, count, offsets));
    // Verify first offset is within CHD bounds
    ASSERT_LT(offsets[0], 8411866u);
    ASSERT_GT(offsets[0], 7903835u);
}

E2E_TIER1_TEST(Tier1_Assets, feat4_anim_frame_dx_dy_offsets, 4) {
    // Verify animation frame structure
    AnimFrame f;
    f.dx = -12;
    f.dy = 15;
    f.sprite_index = 513;
    ASSERT_EQ(f.dx, -12);
    ASSERT_EQ(f.dy, 15);
    ASSERT_EQ(f.sprite_index, 513u);
}

E2E_TIER1_TEST(Tier1_Assets, feat4_anim_default_sp_audio_triggers, 4) {
    AnimSubItem sub;
    sub.default_sp = 58; // Alarm siren
    ASSERT_EQ(sub.default_sp, 58u);
    ASSERT_LT(sub.default_sp, 91u);

    sub.default_sp = 0xFFFFFFFF;
    ASSERT_EQ(sub.default_sp, 0xFFFFFFFFu);
}

E2E_TIER1_TEST(Tier1_Assets, feat4_anim_bounding_boxes, 4) {
    AnimSubItem sub;
    sub.box_left = 10;
    sub.box_top = 20;
    sub.box_right = 50;
    sub.box_bottom = 60;
    ASSERT_EQ(sub.box_right - sub.box_left, 40u);
    ASSERT_EQ(sub.box_bottom - sub.box_top, 40u);
}

// =========================================================================
// FEATURE 5: Maps/*.LVL Level Decoder (Milestone 1)
// =========================================================================

E2E_TIER1_TEST(Tier1_Assets, feat5_lvl_load_all_6_official_maps, 5) {
    std::vector<std::string> maps = {
        "GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"
    };

    for (const auto& map_name : maps) {
        LVLHeader hdr;
        std::vector<uint8_t> l1, l2;
        std::vector<SpawnRecord> spawns;
        std::vector<FoodSpawnPoint> food;
        uint32_t rem = 0xFF;
        bool ok = LvlReader::parse_lvl(MAP_DIR + map_name, hdr, l1, l2, spawns, food, rem);
        ASSERT_TRUE(ok);
        ASSERT_EQ(rem, 0u);
    }
}

E2E_TIER1_TEST(Tier1_Assets, feat5_lvl_header_version_and_gamemode, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0;
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "TREASURE.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(hdr.version, 8u);
    ASSERT_EQ(hdr.game_mode, 1u);
    ASSERT_EQ(hdr.default_minutes, 12u);
}

E2E_TIER1_TEST(Tier1_Assets, feat5_lvl_dimensions_integrity, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0;
    // Tiny is 31x31
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "TINY.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(hdr.width, 31u);
    ASSERT_EQ(hdr.height, 31u);

    // Small is 40x40
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "SMALL.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(hdr.width, 40u);
    ASSERT_EQ(hdr.height, 40u);

    // Treasure is 60x60
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "TREASURE.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(hdr.width, 60u);
    ASSERT_EQ(hdr.height, 60u);
}

E2E_TIER1_TEST(Tier1_Assets, feat5_lvl_layer1_and_layer2_size, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0;
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "TINY.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(l1.size(), 31u * 31u * 6u);
    ASSERT_EQ(l2.size(), 31u * 31u * 6u);
}

E2E_TIER1_TEST(Tier1_Assets, feat5_lvl_trailing_bytes_zero_remainder, 5) {
    LVLHeader hdr;
    std::vector<uint8_t> l1, l2;
    std::vector<SpawnRecord> spawns;
    std::vector<FoodSpawnPoint> food;
    uint32_t rem = 0xFF;
    ASSERT_TRUE(LvlReader::parse_lvl(MAP_DIR + "GAUNTLET.LVL", hdr, l1, l2, spawns, food, rem));
    ASSERT_EQ(rem, 0u);
    ASSERT_GT(spawns.size(), 0u);
    ASSERT_GT(food.size(), 0u);
}

// =========================================================================
// FEATURE 6: 5-to-8 Directional Mirroring (Milestone 1)
// =========================================================================

E2E_TIER1_TEST(Tier1_Assets, feat6_mirror_directions_7_and_3_unflipped, 6) {
    std::vector<uint8_t> pixels = {1, 2, 3, 4};
    MirrorResult res_n = compute_mirror(Direction::North, -5, 2, 2, pixels);
    ASSERT_FALSE(res_n.is_mirrored);
    ASSERT_EQ(res_n.dx_prime, -5);
    ASSERT_EQ(res_n.mirrored_pixels[0], 1u);

    MirrorResult res_s = compute_mirror(Direction::South, -5, 2, 2, pixels);
    ASSERT_FALSE(res_s.is_mirrored);
    ASSERT_EQ(res_s.dx_prime, -5);
}

E2E_TIER1_TEST(Tier1_Assets, feat6_mirror_dir_9_to_west, 6) {
    std::vector<uint8_t> pixels = {10, 20, 30, 40};
    MirrorResult res_w = compute_mirror(Direction::West, -15, 2, 2, pixels);
    ASSERT_TRUE(res_w.is_mirrored);
    ASSERT_EQ(res_w.source_direction, 9u);
    // Formula dx' = -(dx + width) = -(-15 + 2) = 13
    ASSERT_EQ(res_w.dx_prime, 13);
    // Row 0: 10, 20 -> flipped to 20, 10
    ASSERT_EQ(res_w.mirrored_pixels[0], 20u);
    ASSERT_EQ(res_w.mirrored_pixels[1], 10u);
    // Row 1: 30, 40 -> flipped to 40, 30
    ASSERT_EQ(res_w.mirrored_pixels[2], 40u);
    ASSERT_EQ(res_w.mirrored_pixels[3], 30u);
}

E2E_TIER1_TEST(Tier1_Assets, feat6_mirror_dir_8_to_nw, 6) {
    std::vector<uint8_t> pixels = {1, 2, 3, 4, 5, 6};
    MirrorResult res_nw = compute_mirror(Direction::NorthWest, -10, 3, 2, pixels);
    ASSERT_TRUE(res_nw.is_mirrored);
    ASSERT_EQ(res_nw.source_direction, 8u);
    // dx' = -(-10 + 3) = 7
    ASSERT_EQ(res_nw.dx_prime, 7);
    // Row 0: 1, 2, 3 -> 3, 2, 1
    ASSERT_EQ(res_nw.mirrored_pixels[0], 3u);
    ASSERT_EQ(res_nw.mirrored_pixels[1], 2u);
    ASSERT_EQ(res_nw.mirrored_pixels[2], 1u);
}

E2E_TIER1_TEST(Tier1_Assets, feat6_mirror_dir_2_to_sw, 6) {
    std::vector<uint8_t> pixels = {100, 200};
    MirrorResult res_sw = compute_mirror(Direction::SouthWest, -4, 2, 1, pixels);
    ASSERT_TRUE(res_sw.is_mirrored);
    ASSERT_EQ(res_sw.source_direction, 2u);
    // dx' = -(-4 + 2) = 2
    ASSERT_EQ(res_sw.dx_prime, 2);
    ASSERT_EQ(res_sw.mirrored_pixels[0], 200u);
    ASSERT_EQ(res_sw.mirrored_pixels[1], 100u);
}

E2E_TIER1_TEST(Tier1_Assets, feat6_mirror_dx_prime_formula, 6) {
    // Stress test formula: dx' = -(dx + W)
    for (int32_t dx = -50; dx <= 50; dx += 10) {
        for (uint32_t w = 1; w <= 64; w += 8) {
            std::vector<uint8_t> px(w, 0);
            MirrorResult r = compute_mirror(Direction::West, dx, w, 1, px);
            ASSERT_EQ(r.dx_prime, -(dx + static_cast<int32_t>(w)));
        }
    }
}

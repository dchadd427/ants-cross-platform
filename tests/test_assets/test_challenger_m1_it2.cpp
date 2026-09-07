#include <iostream>
#include <vector>
#include <fstream>
#include <cstring>
#include <cassert>
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/chd_parser.hpp"
#include "ants_assets/asset_archive.hpp"

using namespace ants::assets;

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        std::cerr << "[-] FAILED assertion at " << __FILE__ << ":" << __LINE__ << ": " << #cond << "\n"; \
        std::exit(1); \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

#include <filesystem>

namespace fs = std::filesystem;

static std::string locate_assets_dir() {
    std::vector<std::string> candidates = {
#ifdef ORIGINAL_ASSETS_DIR
        ORIGINAL_ASSETS_DIR,
#endif
        "Original-Ants",
        "../Original-Ants",
        "../../Original-Ants",
        "../../../Original-Ants",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants"
    };
    for (const auto& path : candidates) {
        if (fs::exists(path) && fs::exists(path + "/ants.chd")) {
            return path;
        }
    }
    return "Original-Ants";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " Challenger M1 Iteration 2 Empirical Stress Test Suite\n";
    std::cout << " Target: libants-assets (Sanitizer & Allocation Bounds)\n";
    std::cout << "=======================================================\n\n";

    std::string assets_dir = locate_assets_dir();

    // -------------------------------------------------------------
    // SECTION 1: Malformed and Oversized Map Dimensions (LVLParser)
    // -------------------------------------------------------------
    std::cout << "[STRESS 1] Malformed and Oversized Map Dimensions to LVLParser...\n";
    {
        // Load authentic TINY.LVL as template
        std::string map_path = assets_dir + "/Maps/TINY.LVL";
        std::ifstream file(map_path, std::ios::binary | std::ios::ate);
        TEST_ASSERT(file.is_open());
        size_t full_size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> valid_lvl(full_size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(valid_lvl.data()), static_cast<std::streamsize>(full_size));
        TEST_ASSERT(valid_lvl.size() > 42);

        // Find dimension offset: 42 + (tile_type_count + 1) * 11
        uint16_t ttc = static_cast<uint16_t>(valid_lvl[40] | (valid_lvl[41] << 8));
        size_t dim_offset = 42 + (static_cast<size_t>(ttc) + 1) * 11;
        TEST_ASSERT(dim_offset + 8 <= valid_lvl.size());

        // 1.1 width = 0x7FFFFFFF, height = 1
        {
            auto bad = valid_lvl;
            uint32_t w = 0x7FFFFFFF;
            uint32_t h = 1;
            std::memcpy(bad.data() + dim_offset, &w, 4);
            std::memcpy(bad.data() + dim_offset + 4, &h, 4);
            LevelData lvl;
            bool ok = LVLParser::load_from_memory(bad.data(), bad.size(), lvl);
            TEST_ASSERT(!ok);
            TEST_ASSERT(lvl.layer1_cells().empty());
        }

        // 1.2 width = 1, height = 0x7FFFFFFF
        {
            auto bad = valid_lvl;
            uint32_t w = 1;
            uint32_t h = 0x7FFFFFFF;
            std::memcpy(bad.data() + dim_offset, &w, 4);
            std::memcpy(bad.data() + dim_offset + 4, &h, 4);
            LevelData lvl;
            bool ok = LVLParser::load_from_memory(bad.data(), bad.size(), lvl);
            TEST_ASSERT(!ok);
            TEST_ASSERT(lvl.layer1_cells().empty());
        }

        // 1.3 width = 0xFFFFFFFF, height = 0xFFFFFFFF (arithmetic overflow scenario)
        {
            auto bad = valid_lvl;
            uint32_t w = 0xFFFFFFFF;
            uint32_t h = 0xFFFFFFFF;
            std::memcpy(bad.data() + dim_offset, &w, 4);
            std::memcpy(bad.data() + dim_offset + 4, &h, 4);
            LevelData lvl;
            bool ok = LVLParser::load_from_memory(bad.data(), bad.size(), lvl);
            TEST_ASSERT(!ok);
            TEST_ASSERT(lvl.layer1_cells().empty());
        }

        // 1.4 width = 1000, height = 1000
        {
            auto bad = valid_lvl;
            uint32_t w = 1000;
            uint32_t h = 1000;
            std::memcpy(bad.data() + dim_offset, &w, 4);
            std::memcpy(bad.data() + dim_offset + 4, &h, 4);
            LevelData lvl;
            bool ok = LVLParser::load_from_memory(bad.data(), bad.size(), lvl);
            TEST_ASSERT(!ok);
            TEST_ASSERT(lvl.layer1_cells().empty());
        }

        // 1.5 width = 257, height = 257 (just above 256 limit)
        {
            auto bad = valid_lvl;
            uint32_t w = 257;
            uint32_t h = 257;
            std::memcpy(bad.data() + dim_offset, &w, 4);
            std::memcpy(bad.data() + dim_offset + 4, &h, 4);
            LevelData lvl;
            bool ok = LVLParser::load_from_memory(bad.data(), bad.size(), lvl);
            TEST_ASSERT(!ok);
            TEST_ASSERT(lvl.layer1_cells().empty());
        }

        // 1.6 width = 256, height = 256 but stream capacity too small (requires 256*256*12 = 786,432 bytes)
        {
            auto bad = valid_lvl;
            uint32_t w = 256;
            uint32_t h = 256;
            std::memcpy(bad.data() + dim_offset, &w, 4);
            std::memcpy(bad.data() + dim_offset + 4, &h, 4);
            LevelData lvl;
            bool ok = LVLParser::load_from_memory(bad.data(), bad.size(), lvl);
            TEST_ASSERT(!ok);
            TEST_ASSERT(lvl.layer1_cells().empty());
        }

        // 1.7 width = 0 or height = 0
        {
            auto bad = valid_lvl;
            uint32_t zero = 0;
            std::memcpy(bad.data() + dim_offset, &zero, 4);
            LevelData lvl;
            TEST_ASSERT(!LVLParser::load_from_memory(bad.data(), bad.size(), lvl));

            bad = valid_lvl;
            std::memcpy(bad.data() + dim_offset + 4, &zero, 4);
            TEST_ASSERT(!LVLParser::load_from_memory(bad.data(), bad.size(), lvl));
        }

        // 1.8 Truncated buffers at structural boundaries
        {
            std::vector<size_t> trunc_lens = { 0, 1, 5, 20, 41, 42, dim_offset, dim_offset + 4, dim_offset + 8 };
            for (size_t tlen : trunc_lens) {
                LevelData lvl;
                TEST_ASSERT(!LVLParser::load_from_memory(valid_lvl.data(), tlen, lvl));
                TEST_ASSERT(lvl.layer1_cells().empty());
            }
        }

        // 1.9 Truncated Trailing Blocks (CWE-789 checks on trailing vectors)
        {
            // Block 1: anthill spawns count claims 50,000 entries
            auto bad = valid_lvl;
            // In TINY.LVL, cell_count = 31*31 = 961. 961 * 12 = 11,532 bytes after dim_offset + 8
            size_t b1_offset = dim_offset + 8 + 961 * 12;
            TEST_ASSERT(b1_offset + 2 <= bad.size());
            uint16_t huge_b1 = 50000;
            std::memcpy(bad.data() + b1_offset, &huge_b1, 2);
            LevelData lvl;
            TEST_ASSERT(!LVLParser::load_from_memory(bad.data(), bad.size(), lvl));
            TEST_ASSERT(lvl.anthill_spawns().empty());
        }
    }
    std::cout << "  -> Passed all LVLParser oversized and truncated buffer checks.\n\n";

    // -------------------------------------------------------------
    // SECTION 2: Corrupted CHD Headers and Tables (CHDParser)
    // -------------------------------------------------------------
    std::cout << "[STRESS 2] Corrupted CHD Headers and Tables to CHDParser...\n";
    {
        std::string chd_path = assets_dir + "/ants.chd";
        std::ifstream file(chd_path, std::ios::binary);
        TEST_ASSERT(file.is_open());
        std::vector<uint8_t> valid_hdr(1052);
        file.read(reinterpret_cast<char*>(valid_hdr.data()), 1052);
        TEST_ASSERT(file.gcount() == 1052);

        // 2.1 size = 20 specifically
        {
            CHDHeader hdr;
            TEST_ASSERT(!CHDParser::parse_header(valid_hdr.data(), 20, hdr));
            AssetArchive arch;
            TEST_ASSERT(!arch.load_from_memory(valid_hdr.data(), 20));
        }

        // 2.2 size < 28 for various values
        {
            for (size_t sz : { 0u, 1u, 2u, 10u, 15u, 20u, 24u, 27u }) {
                CHDHeader hdr;
                TEST_ASSERT(!CHDParser::parse_header(valid_hdr.data(), sz, hdr));
            }
        }

        // 2.3 Corrupted Version < 9
        {
            for (uint32_t v : { 0u, 1u, 5u, 8u }) {
                auto bad = valid_hdr;
                std::memcpy(bad.data(), &v, 4);
                CHDHeader hdr;
                TEST_ASSERT(!CHDParser::parse_header(bad.data(), bad.size(), hdr));
            }
        }

        // 2.4 Corrupted Palette Byte Sizes != 1024
        {
            for (uint32_t p : { 0u, 512u, 1023u, 1025u, 2048u, 0xFFFFFFFFu }) {
                auto bad = valid_hdr;
                std::memcpy(bad.data() + 24, &p, 4);
                CHDHeader hdr;
                TEST_ASSERT(!CHDParser::parse_header(bad.data(), bad.size(), hdr));
            }
        }

        // 2.5 Corrupted table1_offset != 28 + palette_bytes (1052)
        {
            for (uint32_t t1 : { 0u, 28u, 1000u, 1051u, 1053u, 0xFFFFFFFFu }) {
                auto bad = valid_hdr;
                std::memcpy(bad.data() + 8, &t1, 4);
                CHDHeader hdr;
                TEST_ASSERT(!CHDParser::parse_header(bad.data(), bad.size(), hdr));
            }
        }

        // 2.6 Table Offset Non-Monotonicity
        {
            // Table 2 <= Table 1
            auto bad = valid_hdr;
            uint32_t bad_t2 = 1052;
            std::memcpy(bad.data() + 12, &bad_t2, 4);
            CHDHeader hdr;
            TEST_ASSERT(!CHDParser::parse_header(bad.data(), bad.size(), hdr));

            // Table 3 <= Table 2
            bad = valid_hdr;
            uint32_t t2_val = 0;
            std::memcpy(&t2_val, valid_hdr.data() + 12, 4);
            std::memcpy(bad.data() + 16, &t2_val, 4);
            TEST_ASSERT(!CHDParser::parse_header(bad.data(), bad.size(), hdr));

            // Table 4 <= Table 3
            bad = valid_hdr;
            uint32_t t3_val = 0;
            std::memcpy(&t3_val, valid_hdr.data() + 16, 4);
            std::memcpy(bad.data() + 20, &t3_val, 4);
            TEST_ASSERT(!CHDParser::parse_header(bad.data(), bad.size(), hdr));
        }

        // 2.7 table4_offset >= size when size > 28
        {
            CHDHeader hdr;
            TEST_ASSERT(!CHDParser::parse_header(valid_hdr.data(), 5000, hdr));
        }

        // 2.8 Table 1 Sprites Deserializer Fuzzing & Input Bounding (CWE-789)
        {
            // Fake buffer with count = 2794, but offsets claiming huge dimensions
            std::vector<uint8_t> fake_t1(4 + 2794 * 4 + 100, 0);
            uint32_t count = 2794;
            std::memcpy(fake_t1.data(), &count, 4);
            uint32_t sprite_offset = 4 + 2794 * 4;
            for (size_t i = 0; i < 2794; ++i) {
                std::memcpy(fake_t1.data() + 4 + i * 4, &sprite_offset, 4);
            }
            // At sprite_offset, write pitch = 10000, width = 10000, height = 10000, fn_len = 5
            uint32_t p = 10000, w = 10000, h = 10000, fl = 5;
            std::memcpy(fake_t1.data() + sprite_offset, &p, 4);
            std::memcpy(fake_t1.data() + sprite_offset + 4, &w, 4);
            std::memcpy(fake_t1.data() + sprite_offset + 8, &h, 4);
            std::memcpy(fake_t1.data() + sprite_offset + 12, &fl, 4);
            std::vector<Sprite> sprites;
            TEST_ASSERT(!CHDParser::parse_table1_sprites(fake_t1.data(), fake_t1.size(), 0, sprites));
            // Verify no massive allocation took place (sp.pixels must NOT have been allocated)
            if (!sprites.empty()) {
                TEST_ASSERT(sprites[0].pixels.empty());
            }
        }

        // 2.9 Table 2 Sounds Deserializer Input Bounding
        {
            // Fake buffer with count = 91, but sound claims pcm_len = 1,000,000 bytes
            std::vector<uint8_t> fake_t2(4 + 91 * 4 + 100, 0);
            uint32_t count = 91;
            std::memcpy(fake_t2.data(), &count, 4);
            uint32_t snd_offset = 4 + 91 * 4;
            for (size_t i = 0; i < 91; ++i) {
                std::memcpy(fake_t2.data() + 4 + i * 4, &snd_offset, 4);
            }
            // format header: fmt_len = 16
            uint32_t fmt_len = 16;
            std::memcpy(fake_t2.data() + snd_offset, &fmt_len, 4);
            // pcm_len at snd_offset + 4 + 16:
            uint32_t huge_pcm = 1000000;
            std::memcpy(fake_t2.data() + snd_offset + 20, &huge_pcm, 4);
            std::vector<SoundClip> sounds;
            TEST_ASSERT(!CHDParser::parse_table2_sounds(fake_t2.data(), fake_t2.size(), 0, sounds));
            if (!sounds.empty()) {
                TEST_ASSERT(sounds[0].pcm_data.empty());
            }
        }
    }
    std::cout << "  -> Passed all CHDParser malformed header and allocation bounds checks.\n\n";

    // -------------------------------------------------------------
    // SECTION 3: Interface Contract & Dual-Syntax Verification
    // -------------------------------------------------------------
    std::cout << "[STRESS 3] Interface Contract & Dual-Syntax Verification...\n";
    {
        LevelData lvl;
        TEST_ASSERT(lvl.load_lvl(assets_dir + "/Maps/TINY.LVL"));

        // PROJECT.md Contract Accessors:
        uint32_t w = lvl.width();
        uint32_t h = lvl.height();
        TEST_ASSERT(w == 31);
        TEST_ASSERT(h == 31);

        uint16_t t1 = lvl.layer1_terrain(0, 0);
        uint16_t t2 = lvl.layer2_item(0, 0);
        (void)t1; (void)t2;

        const std::vector<AnthillSpawn>& spawns = lvl.anthill_spawns();
        TEST_ASSERT(spawns.size() == 12);

        const std::vector<FoodSchedule>& foods = lvl.food_schedules();
        TEST_ASSERT(foods.size() == 14);

        // Backward compatibility raw-field syntax:
        uint32_t raw_w = lvl.width;
        uint32_t raw_h = lvl.height;
        TEST_ASSERT(raw_w == 31);
        TEST_ASSERT(raw_h == 31);
        TEST_ASSERT(lvl.layer1_terrain.size() == 31 * 31);
    }
    std::cout << "  -> Passed all Interface Contract verifications.\n\n";

    // -------------------------------------------------------------
    // SECTION 4: Multi-Map Adversarial Mutation & Repeated Allocation Fuzz
    // -------------------------------------------------------------
    std::cout << "[STRESS 4] Multi-Map Mutation and Repeated Sanitizer Fuzzing...\n";
    {
        const std::vector<std::string> maps = {
            "TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "ISLANDS.LVL", "TREASURE.LVL"
        };

        for (const auto& mf : maps) {
            std::string p = assets_dir + "/Maps/" + mf;
            std::ifstream file(p, std::ios::binary | std::ios::ate);
            TEST_ASSERT(file.is_open());
            size_t sz = static_cast<size_t>(file.tellg());
            std::vector<uint8_t> buf(sz);
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));

            uint16_t ttc = static_cast<uint16_t>(buf[40] | (buf[41] << 8));
            size_t dim_offset = 42 + (static_cast<size_t>(ttc) + 1) * 11;
            TEST_ASSERT(dim_offset + 8 <= buf.size());

            // Test oversized dimensions across all 6 maps
            for (uint32_t bad_dim : { 257u, 500u, 1000u, 65536u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu }) {
                auto mutant = buf;
                std::memcpy(mutant.data() + dim_offset, &bad_dim, 4);
                LevelData lvl;
                TEST_ASSERT(!LVLParser::load_from_memory(mutant.data(), mutant.size(), lvl));
                TEST_ASSERT(lvl.layer1_cells().empty());

                mutant = buf;
                std::memcpy(mutant.data() + dim_offset + 4, &bad_dim, 4);
                TEST_ASSERT(!LVLParser::load_from_memory(mutant.data(), mutant.size(), lvl));
                TEST_ASSERT(lvl.layer1_cells().empty());
            }

            // Test 10 truncation points per map
            for (int step = 1; step < 10; ++step) {
                size_t trunc_sz = (sz * static_cast<size_t>(step)) / 10;
                LevelData lvl;
                TEST_ASSERT(!LVLParser::load_from_memory(buf.data(), trunc_sz, lvl));
            }
        }

        // Stress memory-bounded leak defense: 500 repeated parses of malformed buffers
        for (int rep = 0; rep < 500; ++rep) {
            uint8_t garbage[128];
            std::memset(garbage, static_cast<uint8_t>(rep & 0xFF), sizeof(garbage));
            LevelData lvl;
            LVLParser::load_from_memory(garbage, sizeof(garbage), lvl);

            CHDHeader hdr;
            CHDParser::parse_header(garbage, sizeof(garbage), hdr);

            AssetArchive arch;
            arch.load_from_memory(garbage, sizeof(garbage));
        }
    }
    std::cout << "  -> Passed all Multi-Map Mutations and Repeated Sanitizer Fuzz loops.\n\n";

    std::cout << "=======================================================\n";
    std::cout << " ALL EMPIRICAL STRESS TESTS PASSED SUCCESSFULLY!\n";
    std::cout << " Total Assertions Evaluated: " << g_tests_passed << "/" << g_tests_run << "\n";
    std::cout << "=======================================================\n";

    return 0;
}

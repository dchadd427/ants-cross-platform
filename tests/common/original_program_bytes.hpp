// The bytes of the original program (Ants.exe) that the table tests read, from one of two sources, each proven against
// pinned SHA-256 digests.
//
// The original program is not part of the repository (the owner keeps a local copy in Original-Ants/; see .gitignore).
// Two tests compare the remake with static tables inside it: tests/test_assets/test_movement_tables.cpp (suite 5) and
// tests/test_sim/test_movement_differential.cpp (its two independent models read their animation tables from it). So
// that both keep their full strength in a clone without the program, every region of Ants.exe that they read is pinned
// below by its virtual address, its size and the SHA-256 of its bytes exactly as the program holds them.
//
//   * With Original-Ants/Ants.exe present, the bytes come from the program: its PE32 layout is checked against the
//     pinned layout and every region against its digest (a mismatch is a different program: the digest test fails).
//   * Without it, the bytes come from the remake's own generated tables (src/ants_sim/movement_tables_data.inc),
//     serialised in the program's layout; they must hash to the same digests, which proves that they are byte for byte
//     the original's, and then exactly those bytes are what the tests read.
//
// In both cases a test reads only bytes of the regions it named (anything else throws), so a test that starts reading
// new bytes needs a new pin. A pinned digest may only be changed with Ants.exe present: the run with the program is what
// verifies a pin.
//
// Not every region can be reproduced: the remake keeps the per-tile results of the terrain-pair list and of the six
// tile-flag lists (kTileTerrain, kTileFlags), not the lists themselves (their order, and the pair list's 25 explicit
// grass entries, are not in the remake). Without the program those regions cannot be read; the test checks instead the
// remake's per-tile arrays against the pinned digests of the arrays that the lists produce (kTileTerrainSha256,
// kTileFlagsSha256), which the run with the program verifies.
//
// The addresses and the layout are those of docs/GAME_REVERSE_ENGINEERING.md (section 2.1 and the tables of 5.32 / 5.33)
// and of tools/extract_movement_tables.py. The digests were computed from the original program (343056 bytes, SHA-256
// 9f7889eff1bdd4a4914bc2505e823e1a78163bdfcd99b380f262824ff03c5ad0, the file named in movement_tables_data.inc).

#ifndef ANTS_TESTS_ORIGINAL_PROGRAM_BYTES_HPP
#define ANTS_TESTS_ORIGINAL_PROGRAM_BYTES_HPP

#include "movement_tables_data.inc"  // the remake's generated tables (include directory src/ants_sim, set by CMake)

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace original_program {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------------------------------------------------

inline uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

inline std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size) {
    static constexpr uint32_t kRound[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    // The padded message (the data, 0x80, zeros, the length in bits as a big-endian 64-bit number) block by block.
    const uint64_t bit_length = static_cast<uint64_t>(size) * 8u;
    const size_t padded = ((size + 9u + 63u) / 64u) * 64u;
    for (size_t offset = 0; offset < padded; offset += 64u) {
        uint8_t block[64];
        for (size_t i = 0; i < 64u; ++i) {
            const size_t pos = offset + i;
            if (pos < size) {
                block[i] = data[pos];
            } else if (pos == size) {
                block[i] = 0x80u;
            } else if (pos >= padded - 8u) {
                block[i] = static_cast<uint8_t>((bit_length >> (8u * (padded - 1u - pos))) & 0xffu);
            } else {
                block[i] = 0u;
            }
        }
        uint32_t w[64];
        for (size_t t = 0; t < 16u; ++t) {
            w[t] = (static_cast<uint32_t>(block[4u * t]) << 24) | (static_cast<uint32_t>(block[4u * t + 1u]) << 16) |
                   (static_cast<uint32_t>(block[4u * t + 2u]) << 8) | static_cast<uint32_t>(block[4u * t + 3u]);
        }
        for (size_t t = 16; t < 64u; ++t) {
            const uint32_t s0 = rotr32(w[t - 15u], 7) ^ rotr32(w[t - 15u], 18) ^ (w[t - 15u] >> 3);
            const uint32_t s1 = rotr32(w[t - 2u], 17) ^ rotr32(w[t - 2u], 19) ^ (w[t - 2u] >> 10);
            w[t] = w[t - 16u] + s0 + w[t - 7u] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (size_t t = 0; t < 64u; ++t) {
            const uint32_t big_s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + big_s1 + choose + kRound[t] + w[t];
            const uint32_t big_s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = big_s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    std::array<uint8_t, 32> digest{};
    for (size_t i = 0; i < 8u; ++i) {
        for (size_t k = 0; k < 4u; ++k) digest[4u * i + k] = static_cast<uint8_t>((state[i] >> (24u - 8u * k)) & 0xffu);
    }
    return digest;
}

inline std::string sha256_hex(const uint8_t* data, size_t size) {
    static const char* const kDigits = "0123456789abcdef";
    std::string hex;
    for (const uint8_t byte : sha256(data, size)) {
        hex += kDigits[byte >> 4];
        hex += kDigits[byte & 0x0fu];
    }
    return hex;
}

inline std::string sha256_hex(const Bytes& bytes) { return sha256_hex(bytes.data(), bytes.size()); }

// The known answers of FIPS 180-4 / NIST CSRC: a broken hash would make every pin below meaningless. Empty when all
// five agree, else the first one that does not.
inline std::string sha256_known_answer_failure() {
    struct Vector {
        std::string message;
        const char* digest;
    };
    const Vector vectors[5] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
        {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };
    for (const Vector& v : vectors) {
        const std::string got = sha256_hex(reinterpret_cast<const uint8_t*>(v.message.data()), v.message.size());
        if (got != v.digest) {
            return "SHA-256 of a " + std::to_string(v.message.size()) + "-byte known-answer message is " + got +
                   ", expected " + v.digest;
        }
    }
    return "";
}

// ---------------------------------------------------------------------------------------------------------------------
// The pinned layout of Ants.exe (docs/GAME_REVERSE_ENGINEERING.md 2.1): PE32, image base 0x01000000, three sections
// ---------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kImageBase = 0x01000000u;

struct Section {
    const char* name;
    uint32_t rva;       // virtual address minus the image base
    uint32_t raw_size;  // bytes of initialised data
    uint32_t raw_ptr;   // file offset of that data
};

inline constexpr Section kSections[3] = {
    {".text", 0x00001000u, 0x00045c00u, 0x00000600u},  // code and the static tables
    {".data", 0x00047000u, 0x00004000u, 0x00046200u},
    {".rsrc", 0x0004d000u, 0x00003400u, 0x0004a200u},
};

// ---------------------------------------------------------------------------------------------------------------------
// The remake's tables serialised in the program's layout (little endian, as the program stores them)
// ---------------------------------------------------------------------------------------------------------------------

namespace mvd = ants::sim::movement::data;

inline void put_u16(Bytes& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
}

inline void put_u32(Bytes& out, uint32_t value) {
    for (unsigned k = 0; k < 4u; ++k) out.push_back(static_cast<uint8_t>((value >> (8u * k)) & 0xffu));
}

inline void put_all_u16(Bytes& out, uint16_t value) { put_u16(out, value); }

template <typename T, size_t N>
void put_all_u16(Bytes& out, const T (&values)[N]) {
    for (const T& v : values) put_all_u16(out, v);
}

// A table of uint16 animation indices, in its row order. The runs of a region are the stored directions 0..4 (or the
// types 0..5) of each row; the virtual ids of the mirrored directions 5..7 and the colour blocks 1..3 are not in the
// remake and are not read by the tests.
template <const auto& Table>
Bytes u16_table() {
    Bytes out;
    put_all_u16(out, Table);
    return out;
}

// One animation index (a single number, not a table), taken by value. As `u16_table<mvd::kIdleWater>` (a reference to an inline constexpr scalar as the template argument) the compiler
// of Visual Studio 2022 17.14 (MSVC 19.44) put other bytes than the number into the region, a different value in every program (found by the Windows job of CI: idle in water, infiltrate
// and get power-up failed their digests; Visual Studio 2026, GCC and clang read the same reference right). The tables are arrays and have no such trouble.
template <uint16_t Value>
Bytes u16_value() {
    Bytes out;
    put_u16(out, Value);
    return out;
}

// The x86 instruction bytes around the immediates that the tests read (the tests assert them too).
inline constexpr uint8_t kOpPushImm32 = 0x68u;    // push imm32
inline constexpr uint8_t kOpOperandSize = 0x66u;  // operand-size prefix: with 0x3d, cmp ax, imm16
inline constexpr uint8_t kOpCmpAxImm = 0x3du;
inline constexpr uint8_t kOpPushImm8 = 0x6au;     // push imm8

// FUN_0101c4f2: push 0xdc (the "bump" effect), 0x101cae3
inline Bytes bump_push() {
    Bytes out{kOpPushImm32};
    put_u32(out, mvd::kBump);
    return out;
}

// FUN_01008b90: cmp ax, imm16 at 0x1008b95 / 9b / a1 / a7 (the four bridge tiles)
inline Bytes bridge_compares() {
    Bytes out;
    for (const uint16_t tile : mvd::kBridgeTiles) {
        out.push_back(kOpOperandSize);
        out.push_back(kOpCmpAxImm);
        put_u16(out, tile);
    }
    return out;
}

// 0x10049b8: uint16 [8], destination walkable by terrain class
inline Bytes passable() {
    Bytes out;
    for (const uint8_t v : mvd::kPassableByTerrain) put_u16(out, v);
    return out;
}

// 0x10049c8: uint32 [6], step weight by terrain class
inline Bytes step_weights() {
    Bytes out;
    for (const uint32_t v : mvd::kStepWeight) put_u32(out, v);
    return out;
}

// FUN_010208e8: cmp ax, 5 at 0x10208fb (its first two bytes) and push imm8 at 0x1020901 (the swimmer's water weight)
inline Bytes swimmer_water_weight() {
    return Bytes{kOpOperandSize, kOpCmpAxImm, kOpPushImm8, static_cast<uint8_t>(mvd::kSwimmerWaterWeight & 0xffu)};
}

// 0x1002b28: int16 [3][3], the direction of a step by [drow + 1][dcol + 1]
inline Bytes direction_table() {
    Bytes out;
    for (const uint8_t v : mvd::kDirTable) put_u16(out, v);
    return out;
}

// 0x1004950: {int32 drow, int32 dcol} [8]
inline Bytes neighbours() {
    Bytes out;
    for (const auto& n : mvd::kNeighbour) {
        put_u32(out, static_cast<uint32_t>(static_cast<int32_t>(n[0])));
        put_u32(out, static_cast<uint32_t>(static_cast<int32_t>(n[1])));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The pinned regions: every byte of Ants.exe that the two tests read
// ---------------------------------------------------------------------------------------------------------------------

struct Region {
    const char* name;        // the table, as the tests name it
    uint32_t va;             // virtual address of the first run
    uint32_t run;            // bytes per run
    uint32_t runs;           // number of runs
    uint32_t stride;         // bytes from the start of one run to the start of the next (0: one run)
    const char* sha256;      // SHA-256 of the runs as Ants.exe holds them, concatenated in address order
    Bytes (*from_remake)();  // the same bytes from the remake's tables; nullptr: the remake does not hold them
    uint32_t size() const { return run * runs; }
    uint32_t run_va(uint32_t k) const { return va + k * stride; }
};

inline const Region kRegions[] = {
    // Locomotion tables of FUN_010175ad (colour-0 block): [type 6][terrain 5][dir 8], [type 6][dir 8], [dir 8]; the runs
    // are the stored directions 0..4 of each row
    {"walk", 0x1002fb8u, 10, 30, 16, "02fcaa037fd985517ff19365598b3294da741e7b8e6dd9429932d5f43464a202", &u16_table<mvd::kWalk>},
    {"carry walk", 0x1003738u, 10, 30, 16, "8ce0f425ffa4895ce189cfa950a63f1b475a51d27a6398c428258c2aab74b340", &u16_table<mvd::kCarryWalk>},
    {"idle", 0x1002cb8u, 10, 6, 16, "4b489be16ce5aa00d64055a82a80a7aef6afdfa280f22efd741fbbdc2fcd2bbe", &u16_table<mvd::kIdle>},
    {"carry idle", 0x1002e38u, 10, 6, 16, "75fade3cb2f3a13c6f3569958ff7dbd079ee390032be520d8867aae2409ac6a4", &u16_table<mvd::kCarryIdle>},
    {"can't go", 0x1004548u, 12, 1, 0, "1dcb42cb000c349b386de1228435103971802b9e146396640ed363833be544fb", &u16_table<mvd::kCantGo>},
    {"carry can't go", 0x1004578u, 12, 1, 0, "ef4635ae40fcf3b763beec82fe73dc398437710500c5b080af959efa8d14f16f", &u16_table<mvd::kCarryCantGo>},
    {"swim", 0x1004838u, 10, 1, 0, "17a4bb64d586f29da6b5e577453cd5aaeea6ba9f85a850c91a147325b3b90ff3", &u16_table<mvd::kSwim>},
    {"dive", 0x1004878u, 10, 1, 0, "0444870ded3c29f44c50074d60c020fd48ba8d423829b078f3340bee13585a78", &u16_table<mvd::kDive>},
    {"climb", 0x10048c0u, 10, 1, 0, "352b131926aa23230cbbd03900bd31134d251347d1033099c27e0531d23c1400", &u16_table<mvd::kClimb>},
    {"idle in water", 0x10048b8u, 2, 1, 0, "75a343cae007ba3927506d9b8f98994e795740f01136d7d80060d76c16dcb20c", &u16_value<mvd::kIdleWater>},
    {"bump push", 0x101cae3u, 5, 1, 0, "497cbde725e3f9eb9237058fa2058bd5057c689b06902eb744f44bbde4e34b06", &bump_push},
    // Action clips of SetAction FUN_0101ad02 (colour-0 blocks): [type 6], [type 6][dir 8] (runs: dirs 0..4), [dir 8]
    {"enter", 0x1003eb8u, 12, 1, 0, "7616d1fc32c45230513805976124633066f4fd29dcc8cc12f3ce0c26488ff299", &u16_table<mvd::kEnter>},
    {"carry enter", 0x1003ee8u, 12, 1, 0, "caeb84eb050a8eea046d22b2a7c6345fd1c79fd8c4b5bbf26689d56cd824e346", &u16_table<mvd::kCarryEnter>},
    {"harvest", 0x1003f18u, 10, 6, 16, "31f3cc0a06320f1e9e19e9c589c6beed44cf788704fede42af2402bf5e680795", &u16_table<mvd::kHarvest>},
    {"attack", 0x1004098u, 10, 6, 16, "649323c1ea4e985731fb6dc330f471e7ba520c1f55f6590ea19d8da18c066b3e", &u16_table<mvd::kAttack>},
    {"hit", 0x1004218u, 10, 6, 16, "4fa0d9f69305b46b68e0b4703a611424d6d20e5c937e8583f8d0e96666b73836", &u16_table<mvd::kHit>},
    {"blown", 0x1004398u, 10, 6, 16, "db5ad8655e343acff0429964498924b3582dc61b549402f89b0bfd52ce0fb4c5", &u16_table<mvd::kBlown>},
    {"burn", 0x1004518u, 12, 1, 0, "9dae3d53f6e138231c50040e951c4f845bb4e82c8c812f87884e5d396682f9ce", &u16_table<mvd::kBurn>},
    {"hatch", 0x10045a8u, 12, 1, 0, "870f2cdcd14cfde921c9dc2dc116e868885f322f71ec855ec2bc2261e7ff2637", &u16_table<mvd::kHatch>},
    {"stun", 0x10045d8u, 12, 1, 0, "da7c8d2f520735de7f8f61d2c6e8319bccc66542fe75c092705504cd5ad34b73", &u16_table<mvd::kStun>},
    {"carry stun", 0x1004608u, 12, 1, 0, "e3f10892a9bb74c8722e06f11538afeaad2431065871e7723897e628f54d4bdd", &u16_table<mvd::kCarryStun>},
    {"drown", 0x1004910u, 12, 1, 0, "6899792bb1c5c78796323f04d1dd127cd5a972985cb9fc32474e3973936ba7ee", &u16_table<mvd::kDrown>},
    {"ignite", 0x1004638u, 10, 1, 0, "824535a997f6382c4631b79ff36089a7c13d569d98d7b3f3b3afcfcf77cbd09e", &u16_table<mvd::kIgnite>},
    {"extinguish", 0x1004678u, 10, 1, 0, "9a4ebc19bd6a76ec3a79ec91068c9e37040b7e134b320abdc7bdc8b067988959", &u16_table<mvd::kExtinguish>},
    {"bridge build water", 0x10046b8u, 10, 1, 0, "1b87cfd5af541a9e925d1520d503c61cfccceb13798bddeab6144e01f0461b85", &u16_table<mvd::kBridgeBuildWater>},
    {"bridge demolish water", 0x10046f8u, 10, 1, 0, "724b5db100f8e75b3961bc4d75e767b5b119be1b14ae68935ffc06926eefdddc", &u16_table<mvd::kBridgeDemolishWater>},
    {"bridge build land", 0x1004738u, 10, 1, 0, "42284f93a69502c0f6001f7a3a196c31f53b2ee0bfbe7a82ceb17c70cc81234f", &u16_table<mvd::kBridgeBuildLand>},
    {"bridge demolish land", 0x1004778u, 10, 1, 0, "f63608dd319f0c433097762a9004030ebbed2919dfa2e6ade2291ea2e17d3d5c", &u16_table<mvd::kBridgeDemolishLand>},
    {"plant", 0x10047b8u, 10, 1, 0, "98e3a713c75afc97884c9a00e6527aa6df5b85ec2b7986b0d0920d633909f959", &u16_table<mvd::kPlant>},
    {"defuse", 0x10047f8u, 10, 1, 0, "5c474ec8a9ccf28018f4408f1e311dc296950148d1a5801beb78e461261788c7", &u16_table<mvd::kDefuse>},
    {"infiltrate", 0x1004900u, 2, 1, 0, "6b02392d87667b615855fc9e150d6964b828666690ec83c7b7d3ebfa8dfe6aea", &u16_value<mvd::kInfiltrate>},
    {"get power-up", 0x1004908u, 2, 1, 0, "bb153e97380f82aa57033add2c11b28a3ae640c8daaa89d2bd5bd317456ecb03", &u16_value<mvd::kGetPow>},
    // Terrain and path tables
    {"bridge compares", 0x1008b95u, 4, 4, 6, "22ed5d9b9d12d3752300c9c2624b5537ae7c0441d6f42d9c17005954a9e11b9e", &bridge_compares},
    {"passable", 0x10049b8u, 16, 1, 0, "8b4697133be69b441b927ab4612f16ba78602228ae1a8f0d730a1e254aff449e", &passable},
    {"step weights", 0x10049c8u, 24, 1, 0, "ddf075a69bf23553aa97f8dc0e72dd8645a2051746b50c3c507d8610567dfbce", &step_weights},
    {"swimmer water weight", 0x10208fbu, 2, 2, 6, "6ea4053ce2f8285d44f788a50b8209cf29972c8590bf36813f5fb58e1250b512", &swimmer_water_weight},
    {"direction table", 0x1002b28u, 18, 1, 0, "71e195079e443ea513bc1601ea82bc4b51f7fb4c70ac7c379b315408f659bd86", &direction_table},
    {"neighbours", 0x1004950u, 64, 1, 0, "ebc7bac928555e28df7ae93a2e0df7ec00b0c176a59284db3bf33e4377f6e2b1", &neighbours},
    // The lists of FUN_0100724c (tile info build): {uint16 tile, uint16 class} pairs and the uint16 tile-id lists of the six
    // flag bits (the 0x10 list has a 12-byte stride: only the first word of a record is read). The remake keeps their per-tile
    // results only (kTileTerrain, kTileFlags; see kTileTerrainSha256 / kTileFlagsSha256 below).
    {"terrain pairs", 0x1001360u, 532, 1, 0, "3f4eed68030debbaa5f20987a28d84f8b9e3af5d575a0ced85533e252a91f08b", nullptr},
    {"flag list 0x01", 0x1001838u, 384, 1, 0, "3a73566c97b1f3b18fa8d3f52fd8fa21faf410cb2f6caf6e4b6743e28a5c367a", nullptr},
    {"flag list 0x02", 0x10019c0u, 174, 1, 0, "eb70a78ff0d177695ca490d6a1ed5ff15001040cd77f940afb6b09ffd55a8e41", nullptr},
    {"flag list 0x04", 0x1001ad8u, 10, 1, 0, "233329cce5de89beee5740ff3ab9f7f80be47bbe9624ed5c27a98b3179b2af6a", nullptr},
    {"flag list 0x08", 0x1001578u, 194, 1, 0, "c9e3adaefa5445726e0885dd78b83953d05001748c4a21f9945d75669b04e0ad", nullptr},
    {"flag list 0x10", 0x1001af8u, 2, 14, 12, "e7be3016d120a63178c6376515a632c7136d96a3e15331ee810bb57ea0a5c832", nullptr},
    {"flag list 0x20", 0x1001818u, 22, 1, 0, "a3eb40a18f66b6e4812eb872357fbca18324357d5892bc9ffd8012cb27e81522", nullptr},
};

// SHA-256 of the 1344 per-tile bytes that the original's lists produce in FUN_0100724c: the terrain class of every tile
// (the pair list; unlisted tiles 0) and the OR of the flag bits (the six lists). With Ants.exe the test computes both
// arrays from the program and checks them against these pins; without it, it checks kTileTerrain and kTileFlags.
inline constexpr const char* kTileTerrainSha256 = "2dc31155de0dfc16a57c91194f945942a66cc594d961a336469151c2d6bdbacf";
inline constexpr const char* kTileFlagsSha256 = "31482332c90f94c565cad0859593a33849a6d4ef7b647354053d55aa16a07766";

inline const Region* find_region(const std::string& name) {
    for (const Region& r : kRegions) {
        if (name == r.name) return &r;
    }
    return nullptr;
}

inline std::vector<std::string> all_region_names() {
    std::vector<std::string> names;
    for (const Region& r : kRegions) names.emplace_back(r.name);
    return names;
}

inline std::string hex_address(uint32_t va) {
    std::ostringstream os;
    os << "0x" << std::hex << va;
    return os.str();
}

// ---------------------------------------------------------------------------------------------------------------------
// The byte source: the reads of the former PeImage readers (virtual addresses, little endian)
// ---------------------------------------------------------------------------------------------------------------------

class ProgramBytes {
public:
    enum class Source { Exe, Remake };

    // Ants.exe at `exe_path` when that file exists, else the remake's tables. `regions` names the pinned regions the test
    // reads; only their bytes can be read.
    static ProgramBytes open(const std::string& exe_path, const std::vector<std::string>& regions) {
        ProgramBytes p;
        std::error_code ec;
        if (std::filesystem::exists(exe_path, ec)) {
            p.source_ = Source::Exe;
            p.load_exe(exe_path, regions);
        } else {
            p.source_ = Source::Remake;
            p.load_remake(regions);
        }
        return p;
    }

    Source source() const noexcept { return source_; }
    bool from_exe() const noexcept { return source_ == Source::Exe; }
    // Ants.exe: a PE32 image was read; the remake: its tables were laid out.
    bool loaded() const noexcept { return loaded_; }
    // Loaded, the layout and every readable region match their pins, and the SHA-256 implementation gives its known answers.
    bool verified() const noexcept { return loaded_ && failures_.empty(); }
    // The checks that were made (the hash's known answers, the layout, one per readable region) and those that failed.
    size_t checks() const noexcept { return checks_; }
    const std::vector<std::string>& failures() const noexcept { return failures_; }
    // The region's bytes can be read from this source (the lists of FUN_0100724c only from Ants.exe).
    bool has(const std::string& region) const {
        for (const std::string& name : readable_) {
            if (name == region) return true;
        }
        return false;
    }
    // The regions that this test reads but this source does not hold.
    const std::vector<std::string>& unavailable() const noexcept { return unavailable_; }

    // The one line that says which source was used.
    std::string summary() const {
        if (from_exe()) {
            if (!loaded_) return "Ants.exe, which could not be read as a PE32 image";
            if (!failures_.empty()) {
                return "Ants.exe, but " + std::to_string(failures_.size()) +
                       " of its checks against the pinned digests FAIL (a different program, or a wrong pin)";
            }
            return "Ants.exe (every region this test reads matches its pinned SHA-256 digest)";
        }
        if (!failures_.empty()) {
            return "the remake's tables, which do NOT match the pinned digests of Ants.exe (" + std::to_string(failures_.size()) +
                   " failed checks)";
        }
        return "the remake's tables, verified against the pinned digests of Ants.exe (no Ants.exe in the assets folder)";
    }

    uint32_t image_base() const noexcept { return image_base_; }

    // n initialised bytes of the program at a virtual address (the layout only: Ants.exe's section table, or the pinned one).
    bool mapped(uint32_t va, uint32_t n) const noexcept {
        const uint32_t rva = va - image_base_;
        for (const SectionInfo& s : sections_) {
            if (rva >= s.rva && static_cast<uint64_t>(rva) + n <= static_cast<uint64_t>(s.rva) + s.raw_size &&
                static_cast<uint64_t>(s.raw_ptr) + s.raw_size <= data_limit_) {
                return true;
            }
        }
        return false;
    }

    uint8_t u8(uint32_t va) const { return byte_at(va); }
    uint16_t u16(uint32_t va) const {
        return static_cast<uint16_t>(static_cast<uint32_t>(byte_at(va)) | (static_cast<uint32_t>(byte_at(va + 1u)) << 8));
    }
    int16_t i16(uint32_t va) const { return static_cast<int16_t>(u16(va)); }
    uint32_t u32(uint32_t va) const {
        return static_cast<uint32_t>(byte_at(va)) | (static_cast<uint32_t>(byte_at(va + 1u)) << 8) |
               (static_cast<uint32_t>(byte_at(va + 2u)) << 16) | (static_cast<uint32_t>(byte_at(va + 3u)) << 24);
    }
    int32_t i32(uint32_t va) const { return static_cast<int32_t>(u32(va)); }
    std::vector<uint16_t> u16s(uint32_t va, uint32_t n) const {
        std::vector<uint16_t> out;
        for (uint32_t i = 0; i < n; ++i) out.push_back(u16(va + 2u * i));
        return out;
    }

private:
    struct SectionInfo {
        uint32_t rva;
        uint32_t raw_size;
        uint32_t raw_ptr;
    };

    uint8_t byte_at(uint32_t va) const {
        const auto it = bytes_.find(va);
        if (it == bytes_.end()) {
            throw std::out_of_range("the byte at " + hex_address(va) +
                                    " is not in a pinned region of Ants.exe that this test reads from this source");
        }
        return it->second;
    }

    void fail(const std::string& what) { failures_.push_back(what); }

    void check_hash() {
        ++checks_;
        const std::string kat = sha256_known_answer_failure();
        if (!kat.empty()) fail(kat);
    }

    // Lays a region's bytes (its runs, concatenated) out at their addresses and checks their digest.
    void place(const Region& r, const Bytes& bytes) {
        ++checks_;
        if (bytes.size() != r.size()) {
            fail(std::string(r.name) + ": " + std::to_string(bytes.size()) + " bytes instead of " + std::to_string(r.size()));
            return;
        }
        for (uint32_t k = 0; k < r.runs; ++k) {
            for (uint32_t i = 0; i < r.run; ++i) bytes_[r.run_va(k) + i] = bytes[k * r.run + i];
        }
        readable_.emplace_back(r.name);
        const std::string got = sha256_hex(bytes);
        if (got != r.sha256) {
            fail(std::string(r.name) + ": the SHA-256 of its " + std::to_string(r.size()) + " bytes at " + hex_address(r.va) +
                 (r.runs > 1 ? " (" + std::to_string(r.runs) + " runs of " + std::to_string(r.run) + ", stride " +
                                   std::to_string(r.stride) + ")"
                             : std::string()) +
                 " is " + got + ", pinned " + r.sha256);
        }
    }

    void load_remake(const std::vector<std::string>& regions) {
        image_base_ = kImageBase;
        for (const Section& s : kSections) {
            sections_.push_back({s.rva, s.raw_size, s.raw_ptr});
            if (static_cast<uint64_t>(s.raw_ptr) + s.raw_size > data_limit_) data_limit_ = static_cast<uint64_t>(s.raw_ptr) + s.raw_size;
        }
        loaded_ = true;
        check_hash();
        for (const std::string& name : regions) {
            const Region* r = find_region(name);
            if (r == nullptr) {
                ++checks_;
                fail("no pinned region is named \"" + name + "\"");
                continue;
            }
            if (r->from_remake == nullptr) {
                unavailable_.push_back(name);
                continue;
            }
            place(*r, r->from_remake());
        }
    }

    void load_exe(const std::string& path, const std::vector<std::string>& regions) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return;
        file_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        loaded_ = parse_pe();
        if (!loaded_) return;
        check_hash();
        // the layout that the tests read through: the image base and the three sections
        ++checks_;
        if (image_base_ != kImageBase) fail("image base " + hex_address(image_base_) + ", pinned " + hex_address(kImageBase));
        ++checks_;
        if (sections_.size() != std::size(kSections)) {
            fail(std::to_string(sections_.size()) + " sections, pinned " + std::to_string(std::size(kSections)));
        } else {
            for (size_t i = 0; i < sections_.size(); ++i) {
                const SectionInfo& got = sections_[i];
                const Section& pin = kSections[i];
                ++checks_;
                if (section_names_[i] != pin.name || got.rva != pin.rva || got.raw_size != pin.raw_size || got.raw_ptr != pin.raw_ptr) {
                    fail("section " + std::to_string(i) + " is " + section_names_[i] + " at RVA " + hex_address(got.rva) + ", " +
                         hex_address(got.raw_size) + " bytes from file offset " + hex_address(got.raw_ptr) + "; pinned " + pin.name +
                         " at RVA " + hex_address(pin.rva) + ", " + hex_address(pin.raw_size) + " bytes from " + hex_address(pin.raw_ptr));
                }
            }
        }
        for (const std::string& name : regions) {
            const Region* r = find_region(name);
            if (r == nullptr) {
                ++checks_;
                fail("no pinned region is named \"" + name + "\"");
                continue;
            }
            Bytes bytes;
            bool inside = true;
            for (uint32_t k = 0; k < r->runs && inside; ++k) {
                const uint32_t start = r->run_va(k);
                inside = mapped(start, r->run);
                for (uint32_t i = 0; i < r->run && inside; ++i) bytes.push_back(file_[file_offset(start + i)]);
            }
            if (!inside) {
                ++checks_;
                fail(std::string(r->name) + ": its bytes at " + hex_address(r->va) + " are not initialised data of the image");
                continue;
            }
            place(*r, bytes);
        }
    }

    // The PE32 header: the image base and the section table (the reads of the former PeImage::load).
    bool parse_pe() {
        if (file_.size() < 0x40u) return false;
        const uint32_t pe = raw_u32(0x3cu);
        if (static_cast<uint64_t>(pe) + 24u > file_.size() || raw_u32(pe) != 0x00004550u) return false;  // "PE\0\0"
        const uint32_t count = raw_u16(pe + 6u);
        const uint32_t opt_size = raw_u16(pe + 20u);
        const uint32_t opt = pe + 24u;
        if (static_cast<uint64_t>(opt) + 32u > file_.size() || raw_u16(opt) != 0x10bu) return false;  // PE32
        image_base_ = raw_u32(opt + 28u);
        const uint32_t table = opt + opt_size;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t s = table + 40u * i;
            if (static_cast<uint64_t>(s) + 40u > file_.size()) return false;
            std::string name;
            for (uint32_t k = 0; k < 8u && file_[s + k] != 0; ++k) name += static_cast<char>(file_[s + k]);
            section_names_.push_back(name);
            sections_.push_back({raw_u32(s + 12u), raw_u32(s + 16u), raw_u32(s + 20u)});
        }
        data_limit_ = file_.size();
        return true;
    }

    size_t file_offset(uint32_t va) const {
        const uint32_t rva = va - image_base_;
        for (const SectionInfo& s : sections_) {
            if (rva >= s.rva && rva < s.rva + s.raw_size) return static_cast<size_t>(s.raw_ptr) + (rva - s.rva);
        }
        throw std::out_of_range("unmapped address in Ants.exe");
    }

    uint32_t raw_u16(size_t off) const {
        return static_cast<uint32_t>(file_.at(off)) | (static_cast<uint32_t>(file_.at(off + 1u)) << 8);
    }
    uint32_t raw_u32(size_t off) const { return raw_u16(off) | (raw_u16(off + 2u) << 16); }

    Source source_{Source::Remake};
    bool loaded_{false};
    uint32_t image_base_{0};
    std::vector<SectionInfo> sections_;
    std::vector<std::string> section_names_;
    uint64_t data_limit_{0};
    Bytes file_;
    std::map<uint32_t, uint8_t> bytes_;  // the readable bytes: those of the regions this test named, from this source
    std::vector<std::string> readable_;
    std::vector<std::string> unavailable_;
    std::vector<std::string> failures_;
    size_t checks_{0};
};

}  // namespace original_program

#endif  // ANTS_TESTS_ORIGINAL_PROGRAM_BYTES_HPP

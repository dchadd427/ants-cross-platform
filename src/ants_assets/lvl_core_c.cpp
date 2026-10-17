// The C interface of the map core (lvl_core.hpp), the whole of what the browser module lvl_core.wasm offers. Plain data in and out, no exceptions, no state between
// calls, and the memory is the caller's to give back:
//
//   uint8_t* lvl_alloc(uint32_t size)       a buffer of `size` bytes for an input (0 when there is no memory left); give it back with lvl_free
//   void lvl_free(void* p)                  frees a buffer of lvl_alloc or a result block (0 is allowed). A result block is yours until then: nothing else frees it.
//   uint8_t* lvl_read_report(data, size, profile)                  JSON report of a file (lvl_core::read_report)
//   uint8_t* lvl_check_original(data, size, profile)               JSON original-safe findings of a file (lvl_core::check_original)
//   uint8_t* lvl_rewrite(data, size, profile)                      the canonical bytes of a file (lvl_core::rewrite)
//   uint8_t* lvl_blank(rows, columns, ground_tile, eggs, minutes, profile)   a new map's bytes (lvl_core::blank)
//   uint8_t* lvl_same_level(a, size_a, b, size_b, profile)         JSON {"same": ...} (lvl_core::same_level)
//   uint8_t* lvl_blank_extended(rows, columns, ground_tile, eggs, minutes)   a new Extended map's bytes (lvl_core::blank_extended)
//   uint8_t* lvl_limits(profile)                                   JSON of the limits of a profile (lvl_core::profile_limits)
//   uint32_t lvl_result_status(const uint8_t* result)              0 the answer, 1 a refusal (the payload is a sentence), 2 a fault or no memory (a sentence, empty when even that did not fit); a null block reads as 2
//   uint32_t lvl_result_size(const uint8_t* result)                the bytes of the payload
//   const uint8_t* lvl_result_data(const uint8_t* result)          the payload (valid until lvl_free(result))
//   uint32_t lvl_interface_version()                               2 (1: the five calls of the first version; 2 adds the Extended profile and the calls that follow them)
//
// `profile`: 0 the limits of a new Classic map (LevelLimits::for_new_maps()), 1 the game's own (LevelLimits{}), 2 a place that takes Extended maps too (LevelLimits::for_extended_maps()). A call returns 0 only when there is no memory left for its answer.
#include "ants_assets/lvl_core.hpp"

#include <cstdlib>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define LVL_CORE_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define LVL_CORE_EXPORT extern "C"
#endif

namespace {

using ants::assets::lvl_core::Profile;
using ants::assets::lvl_core::Result;

constexpr size_t kHeader = 8;   // status and payload size, two little-endian dwords, then the payload

void put_u32(uint8_t* at, uint32_t v) {
    at[0] = static_cast<uint8_t>(v);
    at[1] = static_cast<uint8_t>(v >> 8);
    at[2] = static_cast<uint8_t>(v >> 16);
    at[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t get_u32(const uint8_t* at) { return static_cast<uint32_t>(at[0]) | static_cast<uint32_t>(at[1]) << 8 | static_cast<uint32_t>(at[2]) << 16 | static_cast<uint32_t>(at[3]) << 24; }

uint8_t* block_of(const Result& r) noexcept {
    if (r.payload.size() > 0xFFFFFFFFu - kHeader) return nullptr;
    uint8_t* block = static_cast<uint8_t*>(std::malloc(kHeader + r.payload.size()));
    if (block == nullptr) return nullptr;
    put_u32(block, r.status);
    put_u32(block + 4, static_cast<uint32_t>(r.payload.size()));
    if (!r.payload.empty()) std::memcpy(block + kHeader, r.payload.data(), r.payload.size());
    return block;
}

Profile profile_of(uint32_t p) noexcept { return static_cast<Profile>(p); }

}  // namespace

LVL_CORE_EXPORT uint8_t* lvl_alloc(uint32_t size) { return static_cast<uint8_t*>(std::malloc(size == 0 ? 1 : size)); }
LVL_CORE_EXPORT void lvl_free(void* p) { std::free(p); }

LVL_CORE_EXPORT uint8_t* lvl_read_report(const uint8_t* data, uint32_t size, uint32_t profile) { return block_of(ants::assets::lvl_core::read_report(data, size, profile_of(profile))); }
LVL_CORE_EXPORT uint8_t* lvl_check_original(const uint8_t* data, uint32_t size, uint32_t profile) { return block_of(ants::assets::lvl_core::check_original(data, size, profile_of(profile))); }
LVL_CORE_EXPORT uint8_t* lvl_rewrite(const uint8_t* data, uint32_t size, uint32_t profile) { return block_of(ants::assets::lvl_core::rewrite(data, size, profile_of(profile))); }
LVL_CORE_EXPORT uint8_t* lvl_blank(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes, uint32_t profile) {
    return block_of(ants::assets::lvl_core::blank(rows, columns, ground_tile, eggs, minutes, profile_of(profile)));
}
LVL_CORE_EXPORT uint8_t* lvl_blank_extended(uint32_t rows, uint32_t columns, uint32_t ground_tile, uint32_t eggs, uint32_t minutes) {
    return block_of(ants::assets::lvl_core::blank_extended(rows, columns, ground_tile, eggs, minutes));
}
LVL_CORE_EXPORT uint8_t* lvl_limits(uint32_t profile) { return block_of(ants::assets::lvl_core::profile_limits(profile_of(profile))); }
LVL_CORE_EXPORT uint8_t* lvl_same_level(const uint8_t* a, uint32_t size_a, const uint8_t* b, uint32_t size_b, uint32_t profile) {
    return block_of(ants::assets::lvl_core::same_level(a, size_a, b, size_b, profile_of(profile)));
}

// (a null block, which a call answers when there is no memory left for its answer, reads as status 2, no payload)
LVL_CORE_EXPORT uint32_t lvl_result_status(const uint8_t* result) { return result == nullptr ? 2 : get_u32(result); }
LVL_CORE_EXPORT uint32_t lvl_result_size(const uint8_t* result) { return result == nullptr ? 0 : get_u32(result + 4); }
LVL_CORE_EXPORT const uint8_t* lvl_result_data(const uint8_t* result) { return result == nullptr ? nullptr : result + kHeader; }
LVL_CORE_EXPORT uint32_t lvl_interface_version() { return 2; }

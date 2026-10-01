#pragma once

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include "chd_parser.hpp"
#include "mirroring.hpp"

namespace ants::assets {

/**
 * @brief Master asset archive manager for ants.chd.
 *
 * Provides O(1) indexed and named lookups for all palettes, sprites, audio clips,
 * event tags, and animation sequences. Pre-computes 8-directional mirrored sprites
 * and directional animations in RAM for zero runtime rendering overhead.
 */
class AssetArchive {
public:
    AssetArchive();
    ~AssetArchive();

    AssetArchive(const AssetArchive&) = delete;
    AssetArchive& operator=(const AssetArchive&) = delete;
    AssetArchive(AssetArchive&&) noexcept;
    AssetArchive& operator=(AssetArchive&&) noexcept;

    // Loading Entry Points
    bool load_from_file(const std::string& chd_path);
    bool load_from_memory(const uint8_t* data, size_t size);
    bool load_chd(const std::string& path) { return load_from_file(path); }

    // State Query
    bool is_loaded() const noexcept { return loaded_; }

    // Master Palette (256 Colors)
    const std::array<ColorRGBA, 256>& get_palette() const noexcept { return palette_; }

    // Sprites (Table 1: 2,794 Sprites)
    size_t sprite_count() const noexcept { return sprites_.size(); }
    const Sprite& get_sprite(uint32_t index) const;
    const Sprite* find_sprite(const std::string& name) const noexcept;
    int32_t find_sprite_id(const std::string& name) const noexcept;

    // Sound Clips (Table 2: 91 Sound Clips)
    size_t sound_count() const noexcept { return sounds_.size(); }
    const SoundClip& get_sound(uint32_t sound_id) const;
    const SoundClip* find_sound(const std::string& name) const noexcept;
    int32_t find_sound_id(const std::string& name) const noexcept;

    // Event Tags (Table 3: 4 Event Tags)
    size_t tag_count() const noexcept { return tags_.size(); }
    const EventTag& get_tag(uint32_t index) const;

    // Animations (Table 4: 1,344 Animation Sequences)
    size_t animation_count() const noexcept { return animations_.size(); }
    const AnimationSequence& get_animation(uint32_t anim_id) const;
    const AnimationSequence* find_animation(const std::string& name) const noexcept;
    int32_t find_animation_id(const std::string& name) const noexcept;

    // ========================================================================
    // 5-to-8 Directional Pre-computed Mirroring (O(1) Direct Lookup)
    // ========================================================================

    /**
     * @brief Returns the sprite for a given base sprite ID and discrete compass heading.
     * For directions North (0), NorthEast (1), East (2), SouthEast (3), South (4),
     * returns the unmirrored sprite from Table 1.
     * For directions SouthWest (5), West (6), NorthWest (7), returns the pre-computed
     * horizontally mirrored sprite.
     */
    const Sprite& get_directional_sprite(uint32_t base_sprite_id, Direction dir) const;

    /**
     * @brief Returns the pre-computed horizontally mirrored sprite for any sprite ID.
     */
    const Sprite& get_mirrored_sprite(uint32_t base_sprite_id) const;

    /**
     * @brief Resolves a directional animation sequence.
     * Given an animation prefix (e.g. "agwg") and a heading (0..7), returns the sequence
     * with pre-computed mirrored frame render offsets dx' for Western directions.
     */
    const AnimationSequence* get_directional_animation(const std::string& base_prefix,
                                                      Direction dir) const;

private:
    void build_index_tables();
    void precompute_mirrored_sprites();
    void precompute_directional_animations();

    bool loaded_{false};
    CHDHeader header_{};
    std::array<ColorRGBA, 256> palette_{};
    std::vector<Sprite> sprites_;                   // 2,794 original sprites
    std::vector<Sprite> mirrored_sprites_;          // 2,794 pre-mirrored sprites
    std::vector<SoundClip> sounds_;                 // 91 audio clips
    std::vector<EventTag> tags_;                    // Event tags
    std::vector<AnimationSequence> animations_;     // 1,344 original animations
    std::vector<AnimationSequence> dir_animations_; // Pre-computed mirrored directional animations

    // Hash Maps for O(1) String Lookups
    std::unordered_map<std::string, uint32_t> sprite_name_map_;
    std::unordered_map<std::string, uint32_t> lower_sprite_name_map_;
    std::unordered_map<std::string, uint32_t> sound_name_map_;
    std::unordered_map<std::string, uint32_t> anim_name_map_;
    std::unordered_map<std::string, uint32_t> lower_anim_name_map_;
    std::unordered_map<std::string, uint32_t> dir_anim_map_;
};

} // namespace ants::assets

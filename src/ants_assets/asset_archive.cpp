#include "ants_assets/asset_archive.hpp"
#include <fstream>
#include <cstring>

namespace ants::assets {

static const Sprite EMPTY_SPRITE{};
static const SoundClip EMPTY_SOUND{};
static const EventTag EMPTY_TAG{};
static const AnimationSequence EMPTY_ANIM{};

AssetArchive::AssetArchive() = default;
AssetArchive::~AssetArchive() = default;

AssetArchive::AssetArchive(AssetArchive&&) noexcept = default;
AssetArchive& AssetArchive::operator=(AssetArchive&&) noexcept = default;

bool AssetArchive::load_from_file(const std::string& chd_path) {
    std::ifstream file(chd_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    if (size < 28) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return false;
    }

    return load_from_memory(buffer.data(), buffer.size());
}

bool AssetArchive::load_from_memory(const uint8_t* data, size_t size) {
    loaded_ = false;

    if (!CHDParser::parse_header(data, size, header_)) return false;
    if (!CHDParser::parse_palette(data, size, 28, palette_)) return false;
    if (!CHDParser::parse_table1_sprites(data, size, header_.table1_offset, sprites_)) return false;
    if (!CHDParser::parse_table2_sounds(data, size, header_.table2_offset, sounds_)) return false;
    if (!CHDParser::parse_table3_tags(data, size, header_.table3_offset, tags_)) return false;
    if (!CHDParser::parse_table4_animations(data, size, header_.table4_offset, animations_)) return false;

    build_index_tables();
    precompute_mirrored_sprites();
    precompute_directional_animations();

    loaded_ = true;
    return true;
}

void AssetArchive::build_index_tables() {
    sprite_name_map_.clear();
    lower_sprite_name_map_.clear();
    sprite_name_map_.reserve(sprites_.size());
    lower_sprite_name_map_.reserve(sprites_.size());
    for (size_t i = 0; i < sprites_.size(); ++i) {
        sprite_name_map_[sprites_[i].name] = static_cast<uint32_t>(i);
        std::string low = sprites_[i].name;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        lower_sprite_name_map_.emplace(low, static_cast<uint32_t>(i));
    }

    sound_name_map_.clear();
    sound_name_map_.reserve(sounds_.size());
    for (size_t i = 0; i < sounds_.size(); ++i) {
        sound_name_map_[sounds_[i].name] = static_cast<uint32_t>(i);
    }

    anim_name_map_.clear();
    lower_anim_name_map_.clear();
    anim_name_map_.reserve(animations_.size());
    lower_anim_name_map_.reserve(animations_.size());
    for (size_t i = 0; i < animations_.size(); ++i) {
        anim_name_map_[animations_[i].name] = static_cast<uint32_t>(i);
        std::string low = animations_[i].name;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        lower_anim_name_map_.emplace(low, static_cast<uint32_t>(i));
    }
}

void AssetArchive::precompute_mirrored_sprites() {
    mirrored_sprites_.resize(sprites_.size());
    for (size_t i = 0; i < sprites_.size(); ++i) {
        mirrored_sprites_[i] = sprites_[i].create_horizontal_flip(static_cast<uint32_t>(i));
    }
}

void AssetArchive::precompute_directional_animations() {
    dir_animations_.clear();
    dir_anim_map_.clear();

    // Scan for directional animations (names ending in [7,8,9,2,3] + "01")
    for (size_t i = 0; i < animations_.size(); ++i) {
        const auto& anim = animations_[i];
        if (anim.name.size() < 4) continue;

        char dir_char = anim.name[anim.name.size() - 3];
        std::string suffix = anim.name.substr(anim.name.size() - 2);

        if (suffix == "01" && (dir_char == '7' || dir_char == '8' || dir_char == '9' || dir_char == '2' || dir_char == '3' ||
                               dir_char == '6' || dir_char == '5' || dir_char == '4')) {
            std::string prefix = anim.name.substr(0, anim.name.size() - 3);

            // Register standard unmirrored directions
            if (dir_char == '7') {
                dir_anim_map_[prefix + "_0"] = static_cast<uint32_t>(i);
            } else if (dir_char == '8' || dir_char == '6') {
                dir_anim_map_[prefix + "_1"] = static_cast<uint32_t>(i);

                // Precompute mirrored NorthWest (7) from NorthEast (8/6)
                AnimationSequence nw_anim;
                nw_anim.id = static_cast<uint32_t>(animations_.size() + dir_animations_.size());
                nw_anim.name = prefix + "_mirrored_7";
                nw_anim.flag1 = anim.flag1;
                nw_anim.flag2 = anim.flag2;
                nw_anim.flag3 = anim.flag3;
                nw_anim.subitems.resize(anim.subitems.size());

                for (size_t s = 0; s < anim.subitems.size(); ++s) {
                    const auto& src_sub = anim.subitems[s];
                    auto& dst_sub = nw_anim.subitems[s];
                    dst_sub = src_sub;

                    mirror_bounding_box(src_sub.box_left, src_sub.box_right, dst_sub.box_left, dst_sub.box_right);

                    for (size_t f = 0; f < src_sub.frames.size(); ++f) {
                        const auto& src_f = src_sub.frames[f];
                        auto& dst_f = dst_sub.frames[f];
                        uint32_t w = (src_f.sprite_index < sprites_.size()) ? sprites_[src_f.sprite_index].width : 0;
                        dst_f.dx = mirror_dx(src_f.dx, w);
                        dst_f.dy = mirror_dy(src_f.dy);
                        dst_f.sprite_index = src_f.sprite_index;
                    }
                }

                uint32_t dir_idx = static_cast<uint32_t>(dir_animations_.size());
                dir_animations_.push_back(std::move(nw_anim));
                dir_anim_map_[prefix + "_7"] = 0x80000000u | dir_idx;
            } else if (dir_char == '9' || dir_char == '5') {
                dir_anim_map_[prefix + "_2"] = static_cast<uint32_t>(i);

                // Precompute mirrored West (6) from East (9/5)
                AnimationSequence w_anim;
                w_anim.id = static_cast<uint32_t>(animations_.size() + dir_animations_.size());
                w_anim.name = prefix + "_mirrored_6";
                w_anim.flag1 = anim.flag1;
                w_anim.flag2 = anim.flag2;
                w_anim.flag3 = anim.flag3;
                w_anim.subitems.resize(anim.subitems.size());

                for (size_t s = 0; s < anim.subitems.size(); ++s) {
                    const auto& src_sub = anim.subitems[s];
                    auto& dst_sub = w_anim.subitems[s];
                    dst_sub = src_sub;

                    mirror_bounding_box(src_sub.box_left, src_sub.box_right, dst_sub.box_left, dst_sub.box_right);

                    for (size_t f = 0; f < src_sub.frames.size(); ++f) {
                        const auto& src_f = src_sub.frames[f];
                        auto& dst_f = dst_sub.frames[f];
                        uint32_t w = (src_f.sprite_index < sprites_.size()) ? sprites_[src_f.sprite_index].width : 0;
                        dst_f.dx = mirror_dx(src_f.dx, w);
                        dst_f.dy = mirror_dy(src_f.dy);
                        dst_f.sprite_index = src_f.sprite_index;
                    }
                }

                uint32_t dir_idx = static_cast<uint32_t>(dir_animations_.size());
                dir_animations_.push_back(std::move(w_anim));
                dir_anim_map_[prefix + "_6"] = 0x80000000u | dir_idx;
            } else if (dir_char == '2' || dir_char == '4') {
                dir_anim_map_[prefix + "_3"] = static_cast<uint32_t>(i);

                // Precompute mirrored SouthWest (5) from SouthEast (2/4)
                AnimationSequence sw_anim;
                sw_anim.id = static_cast<uint32_t>(animations_.size() + dir_animations_.size());
                sw_anim.name = prefix + "_mirrored_5";
                sw_anim.flag1 = anim.flag1;
                sw_anim.flag2 = anim.flag2;
                sw_anim.flag3 = anim.flag3;
                sw_anim.subitems.resize(anim.subitems.size());

                for (size_t s = 0; s < anim.subitems.size(); ++s) {
                    const auto& src_sub = anim.subitems[s];
                    auto& dst_sub = sw_anim.subitems[s];
                    dst_sub = src_sub;

                    mirror_bounding_box(src_sub.box_left, src_sub.box_right, dst_sub.box_left, dst_sub.box_right);

                    for (size_t f = 0; f < src_sub.frames.size(); ++f) {
                        const auto& src_f = src_sub.frames[f];
                        auto& dst_f = dst_sub.frames[f];
                        uint32_t w = (src_f.sprite_index < sprites_.size()) ? sprites_[src_f.sprite_index].width : 0;
                        dst_f.dx = mirror_dx(src_f.dx, w);
                        dst_f.dy = mirror_dy(src_f.dy);
                        dst_f.sprite_index = src_f.sprite_index;
                    }
                }

                uint32_t dir_idx = static_cast<uint32_t>(dir_animations_.size());
                dir_animations_.push_back(std::move(sw_anim));
                dir_anim_map_[prefix + "_5"] = 0x80000000u | dir_idx;
            } else if (dir_char == '3') {
                dir_anim_map_[prefix + "_4"] = static_cast<uint32_t>(i);
            }
        }
    }
}

const Sprite& AssetArchive::get_sprite(uint32_t index) const {
    if (index < sprites_.size()) {
        return sprites_[index];
    }
    return EMPTY_SPRITE;
}

const Sprite* AssetArchive::find_sprite(const std::string& name) const noexcept {
    auto it = sprite_name_map_.find(name);
    if (it != sprite_name_map_.end()) {
        return &sprites_[it->second];
    }
    std::string low = name;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it_low = lower_sprite_name_map_.find(low);
    if (it_low != lower_sprite_name_map_.end()) {
        return &sprites_[it_low->second];
    }
    return nullptr;
}

int32_t AssetArchive::find_sprite_id(const std::string& name) const noexcept {
    auto it = sprite_name_map_.find(name);
    if (it != sprite_name_map_.end()) {
        return static_cast<int32_t>(it->second);
    }
    std::string low = name;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it_low = lower_sprite_name_map_.find(low);
    if (it_low != lower_sprite_name_map_.end()) {
        return static_cast<int32_t>(it_low->second);
    }
    return -1;
}

const SoundClip& AssetArchive::get_sound(uint32_t sound_id) const {
    if (sound_id < sounds_.size()) {
        return sounds_[sound_id];
    }
    return EMPTY_SOUND;
}

const SoundClip* AssetArchive::find_sound(const std::string& name) const noexcept {
    auto it = sound_name_map_.find(name);
    if (it != sound_name_map_.end()) {
        return &sounds_[it->second];
    }
    return nullptr;
}

int32_t AssetArchive::find_sound_id(const std::string& name) const noexcept {
    auto it = sound_name_map_.find(name);
    if (it != sound_name_map_.end()) {
        return static_cast<int32_t>(it->second);
    }
    return -1;
}

const EventTag& AssetArchive::get_tag(uint32_t index) const {
    if (index < tags_.size()) {
        return tags_[index];
    }
    return EMPTY_TAG;
}

const AnimationSequence& AssetArchive::get_animation(uint32_t anim_id) const {
    if (anim_id < animations_.size()) {
        return animations_[anim_id];
    }
    return EMPTY_ANIM;
}

const AnimationSequence* AssetArchive::find_animation(const std::string& name) const noexcept {
    auto it = anim_name_map_.find(name);
    if (it != anim_name_map_.end()) {
        return &animations_[it->second];
    }
    std::string low = name;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it_low = lower_anim_name_map_.find(low);
    if (it_low != lower_anim_name_map_.end()) {
        return &animations_[it_low->second];
    }
    return nullptr;
}

int32_t AssetArchive::find_animation_id(const std::string& name) const noexcept {
    auto it = anim_name_map_.find(name);
    if (it != anim_name_map_.end()) {
        return static_cast<int32_t>(it->second);
    }
    std::string low = name;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it_low = lower_anim_name_map_.find(low);
    if (it_low != lower_anim_name_map_.end()) {
        return static_cast<int32_t>(it_low->second);
    }
    return -1;
}

const Sprite& AssetArchive::get_directional_sprite(uint32_t base_sprite_id, Direction dir) const {
    if (get_direction_mapping(dir).mirrored) {
        return get_mirrored_sprite(base_sprite_id);
    }
    return get_sprite(base_sprite_id);
}

const Sprite& AssetArchive::get_mirrored_sprite(uint32_t base_sprite_id) const {
    if (base_sprite_id < mirrored_sprites_.size()) {
        return mirrored_sprites_[base_sprite_id];
    }
    return EMPTY_SPRITE;
}

const AnimationSequence* AssetArchive::get_directional_animation(const std::string& base_prefix,
                                                               Direction dir) const {
    std::string key = base_prefix + "_" + std::to_string(static_cast<int>(dir));
    auto it = dir_anim_map_.find(key);
    if (it != dir_anim_map_.end()) {
        uint32_t val = it->second;
        if (val & 0x80000000u) {
            uint32_t idx = val & ~0x80000000u;
            if (idx < dir_animations_.size()) {
                return &dir_animations_[idx];
            }
        } else {
            if (val < animations_.size()) {
                return &animations_[val];
            }
        }
    }

    // Fallback search: try direct name lookup
    DirectionMapping m = get_direction_mapping(dir);
    std::string direct_name = base_prefix + std::to_string(m.chd_code) + "01";
    auto anim_it = anim_name_map_.find(direct_name);
    if (anim_it != anim_name_map_.end()) {
        return &animations_[anim_it->second];
    }

    return nullptr;
}

} // namespace ants::assets

#include "ants_app/pedestal.hpp"

namespace ants::app {

namespace {

// Three-letter kind codes of the animation names (fire rises with the irregular name trnbwalu)
const char* kind_code(PedestalKind kind) {
    switch (kind) {
        case PedestalKind::Move:   return "mov";
        case PedestalKind::Bomb:   return "bom";
        case PedestalKind::Attack: return "att";
        case PedestalKind::Fire:   return "fir";
        case PedestalKind::Thief:  return "thf";
        case PedestalKind::Ally:   return "aly";
        case PedestalKind::Swim:   return "swm";
        case PedestalKind::Egg:    return "egg";
        default:                   return "";
    }
}

} // namespace

std::string pedestal_up_anim(PedestalKind kind) {
    if (kind == PedestalKind::Swim) return "butswmup"; // irregular name
    return std::string("but") + kind_code(kind) + "u";
}
std::string pedestal_down_anim(PedestalKind kind) { return std::string("but") + kind_code(kind) + "d"; }
std::string pedestal_press_anim(PedestalKind kind) { return std::string("but") + kind_code(kind) + "2d"; }
std::string pedestal_swap_out_anim(PedestalKind kind) { return std::string("trna") + kind_code(kind) + "d"; }
std::string pedestal_swap_in_anim(PedestalKind kind) { return std::string("trna") + kind_code(kind) + "u"; }
std::string pedestal_sink_anim(PedestalKind kind) { return std::string("trnb") + kind_code(kind) + "d"; }
std::string pedestal_rise_anim(PedestalKind kind) {
    if (kind == PedestalKind::Fire) return "trnbwalu"; // irregular name
    return std::string("trnb") + kind_code(kind) + "u";
}

PedestalChain pedestal_chain(PedestalKind current, int current_mode, PedestalKind next, int next_mode) {
    PedestalChain chain;
    if (current == next && current_mode == next_mode) {
        chain.result_kind = current;
        chain.result_mode = current_mode;
        return chain;
    }
    if (next == PedestalKind::Hidden) {
        chain.animations = { pedestal_sink_anim(current) };
        chain.remove_after_last = true;
        chain.result_kind = PedestalKind::Hidden;
        chain.result_mode = 1;
        return chain;
    }
    if (current == PedestalKind::Hidden) {
        // A pedestal always rises raised; pressing it is a separate transition afterwards
        chain.animations = { pedestal_rise_anim(next), pedestal_up_anim(next) };
        chain.result_kind = next;
        chain.result_mode = 1;
        return chain;
    }
    if (current == next) {
        if (next_mode == 2) {
            chain.animations = { pedestal_press_anim(next), pedestal_down_anim(next) };
            chain.result_mode = 2;
        } else {
            chain.animations = { pedestal_up_anim(next) };
            chain.result_mode = 1;
        }
        chain.result_kind = next;
        return chain;
    }
    // Icon swap on the same pedestal
    chain.animations = { pedestal_swap_out_anim(current), pedestal_swap_in_anim(next), pedestal_up_anim(next) };
    chain.result_kind = next;
    chain.result_mode = 1;
    return chain;
}

void PedestalSlot::request(const assets::AssetArchive& archive, PedestalKind kind, int mode, uint32_t now_ms) {
    want_kind_ = kind;
    want_mode_ = (mode == 2) ? 2 : 1;
    advance(archive, now_ms);
    if (!running_) start_if_needed(now_ms);
}

void PedestalSlot::start_if_needed(uint32_t now_ms) {
    PedestalChain chain = pedestal_chain(kind_, mode_, want_kind_, want_mode_);
    if (chain.animations.empty()) return;
    chain_ = std::move(chain);
    index_ = 0;
    start_ms_ = now_ms;
    running_ = true;
    kind_ = chain_.result_kind;
    mode_ = chain_.result_mode;
}

void PedestalSlot::advance(const assets::AssetArchive& archive, uint32_t now_ms) {
    while (running_) {
        // Every animation of a chain is a timed step except the last one, which is the resting state (single frame);
        // a chain that ends in Hidden plays its sink animation completely and then removes the pedestal.
        const size_t timed = chain_.remove_after_last ? chain_.animations.size() : chain_.animations.size() - 1;
        if (index_ >= timed) {
            running_ = false;
            rest_anim_ = chain_.remove_after_last ? std::string() : chain_.animations.back();
            // The requested state may have changed while the chain played
            start_if_needed(start_ms_);
            continue;
        }
        const auto* seq = archive.find_animation(chain_.animations[index_]);
        uint32_t total = 0;
        if (seq) for (const auto& sub : seq->subitems) total += (sub.val3 > 0 ? sub.val3 : 1);
        if (total == 0) total = 1;
        if (now_ms - start_ms_ < total) return; // still playing this animation
        start_ms_ += total;
        ++index_;
    }
}

bool PedestalSlot::draw(IRenderer& renderer, const assets::AssetArchive& archive, uint32_t now_ms, int32_t dx, int32_t dy) {
    advance(archive, now_ms);
    const std::string* name = nullptr;
    uint32_t elapsed = 0;
    if (running_) {
        name = &chain_.animations[index_];
        elapsed = now_ms - start_ms_;
    } else if (!rest_anim_.empty()) {
        name = &rest_anim_;
    }
    if (!name) return false;
    const auto* seq = archive.find_animation(*name);
    if (!seq || seq->subitems.empty()) return false;
    size_t frame = seq->subitems.size() - 1;
    uint32_t end = 0;
    for (size_t i = 0; i < seq->subitems.size(); ++i) {
        end += (seq->subitems[i].val3 > 0 ? seq->subitems[i].val3 : 1);
        if (elapsed < end) { frame = i; break; }
    }
    const auto& parts = seq->subitems[frame].frames;
    for (size_t k = parts.size(); k-- > 0;) {
        renderer.draw_sprite(parts[k].sprite_index, dx + parts[k].dx, dy + parts[k].dy);
    }
    return true;
}

} // namespace ants::app

// The fire wall and the bomb's explosion of the world art, drawn as LIGHT (docs/WORLD_ART.md, "Fire, bombs and the explosion"): see-through by heat, drawn once by their own opacity and once added to what is under them,
// over a warm glow on the ground. The bombs are plain sprite replacements and need nothing here. The pictures of the fire wall (the numbers 50316 .. 50320) are not offered as the original's sprites 316 .. 319, which the
// Fire ant's own animations also show.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ants_app/renderer.hpp"

namespace ants::app {

namespace {

constexpr uint32_t kFireWall = 50000;                  // the number of a fire wall's picture: 50000 + the sprite (316 .. 320)
constexpr uint32_t kBlastLight = 30000;                // the number of an explosion's light: 30000 + the sprite (306 .. 315)
constexpr uint32_t kFirstBlast = 306;
constexpr int kFireBlendMod = 217;                     // the fire is blended at 0.85 of its own opacity and added at 0.25 (the approved A2)
constexpr int kLightAddMod = 64;
constexpr int kGlowW = 64, kGlowH = 40;                // the fire's glow, in world pixels, with its center at (16, 25) of the wall's cell; the strongest added opacity 64 of 255
constexpr int kGlowPeak = 64;
constexpr double kBlastSigma = 7.0;                    // the explosion's glow: its light blurred by 7 world pixels, times 2.2, at most 0.34, warm orange
constexpr double kBlastGain = 2.2;
constexpr double kBlastPeak = 0.34;
constexpr int kBlastMargin = 21;                       // three sigma

void set_light(SDL_Texture* tex, SDL_BlendMode mode, int alpha) {
    SDL_SetTextureBlendMode(tex, mode);
    SDL_SetTextureAlphaMod(tex, static_cast<uint8_t>(std::clamp(alpha, 0, 255)));
}

}  // namespace

void Renderer::drop_effect_textures() {
    for (SDL_Texture*& t : fire_glow_) {
        if (t) SDL_DestroyTexture(t);
        t = nullptr;
    }
    for (BlastGlow& g : blast_glow_) {
        if (g.texture) SDL_DestroyTexture(g.texture);
        g = BlastGlow{};
    }
    for (auto& a : blast_alpha_) a.clear();
    blast_alpha_state_ = 0;
}

// The warm glow under a fire wall: an ellipse of 64 x 40 world pixels that falls smoothly to nothing, orange, added to the ground (and to an ant that stands in it)
SDL_Texture* Renderer::fire_glow_texture(int32_t scale) {
    SDL_Texture*& slot = fire_glow_[static_cast<size_t>(scale - 1)];
    if (slot) return slot;
    const int32_t gw = kGlowW * scale, gh = kGlowH * scale;
    std::vector<uint8_t> px(static_cast<size_t>(gw) * static_cast<size_t>(gh) * 4u);
    for (int32_t y = 0; y < gh; ++y) {
        for (int32_t x = 0; x < gw; ++x) {
            const float dx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(gw) - 0.5f) * 2.0f, dy = ((static_cast<float>(y) + 0.5f) / static_cast<float>(gh) - 0.5f) * 2.0f;
            float t = 1.0f - std::sqrt(dx * dx + dy * dy);
            t = std::max(0.0f, std::min(1.0f, t));
            t = t * t * (3.0f - 2.0f * t);
            uint8_t* o = &px[(static_cast<size_t>(y) * static_cast<size_t>(gw) + static_cast<size_t>(x)) * 4u];
            o[0] = 255;
            o[1] = 140;
            o[2] = 40;
            o[3] = static_cast<uint8_t>(t * static_cast<float>(kGlowPeak) + 0.5f);
        }
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, gw, gh);
    if (!tex) return nullptr;
    SDL_UpdateTexture(tex, nullptr, px.data(), gw * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    slot = tex;
    return slot;
}

// Draws a fire wall at the cell whose top left is (wx, wy) of the world: the glow, then the wall's frame blended by its own opacity and added once more as light. Without a picture or a sheet the original's wall.
void Renderer::draw_fire_wall(int32_t wx, int32_t wy) {
    int32_t scale = world_art_ != nullptr ? world_art_scale_now() : 0;
    SDL_Texture* sheet = world_art_sheet(world_art::kGroundPage, scale);
    if (sheet == nullptr || !archive_ || anim_id_fire_ < 0) {
        draw_template_world(anim_id_fire_, wx, wy);
        return;
    }
    const AnimBounds& b = anim_bounds(anim_id_fire_);
    const int32_t cam_x = static_cast<int32_t>(camera_.x), cam_y = static_cast<int32_t>(camera_.y);
    const LayoutRect view = world_view();
    if (b.valid && (wx + b.x1 + kGlowW <= cam_x || wx + b.x0 - kGlowW >= cam_x + view.w || wy + b.y1 + kGlowH <= cam_y || wy + b.y0 - kGlowH >= cam_y + view.h)) return;
    const int32_t sx = view.x + (wx - cam_x), sy = view.y + (wy - cam_y);
    if (SDL_Texture* glow = fire_glow_texture(scale)) {
        world_art_filter(glow, scale);
        const SDL_Rect dst = placed(sx + 16 - kGlowW / 2, sy + 25 - kGlowH / 2, kGlowW, kGlowH);
        SDL_RenderCopy(renderer_, glow, nullptr, &dst);
    }
    const auto& seq = archive_->get_animation(static_cast<uint32_t>(anim_id_fire_));
    if (seq.subitems.empty()) return;
    const auto& sub = seq.subitems[std::min(template_frame_index(anim_id_fire_), seq.subitems.size() - 1)];
    const size_t k = static_cast<size_t>(scale - 1);
    for (size_t i = sub.frames.size(); i-- > 0;) {
        const auto& f = sub.frames[i];
        const auto& original = archive_->get_sprite(f.sprite_index);
        const world_art::Picture* pic = world_art_->find(kFireWall + f.sprite_index, 0);
        const SDL_Rect dst = placed(sx + f.dx, sy + f.dy, static_cast<int>(original.width), static_cast<int>(original.height));
        if (pic == nullptr || pic->page != world_art::kGroundPage || pic->w != static_cast<int32_t>(original.width) || pic->h != static_cast<int32_t>(original.height)) {
            SDL_Texture* tex = texture_cache_->get_sprite_texture(f.sprite_index, false, TEAM_NONE);
            if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            continue;
        }
        world_art_filter(sheet, scale);
        const SDL_Rect src = {pic->x[k], pic->y[k], pic->w * scale, pic->h * scale};
        set_light(sheet, SDL_BLENDMODE_BLEND, kFireBlendMod);
        SDL_RenderCopy(renderer_, sheet, &src, &dst);
        set_light(sheet, SDL_BLENDMODE_ADD, kLightAddMod);
        SDL_RenderCopy(renderer_, sheet, &src, &dst);
        set_light(sheet, SDL_BLENDMODE_BLEND, 255);
    }
}

// The alpha of the ten light pictures of the explosion at 1x, cut from the 1x sheet: the glow of a frame is made from it
bool Renderer::ensure_blast_alpha() {
    if (blast_alpha_state_ != 0) return blast_alpha_state_ == 1;
    blast_alpha_state_ = 2;
    DecodedImage sheet;
    if (world_art_ == nullptr || !world_art_->sheet_pixels(world_art::kGroundPage, 1, sheet)) return false;
    for (uint32_t i = 0; i < blast_alpha_.size(); ++i) {
        const world_art::Picture* p = world_art_->find(kBlastLight + kFirstBlast + i, 0);
        if (p == nullptr || p->page != world_art::kGroundPage) continue;
        std::vector<uint8_t> alpha(static_cast<size_t>(p->w) * static_cast<size_t>(p->h));
        for (int32_t y = 0; y < p->h; ++y) {
            for (int32_t x = 0; x < p->w; ++x) alpha[static_cast<size_t>(y) * static_cast<size_t>(p->w) + static_cast<size_t>(x)] = sheet.rgba[(static_cast<size_t>(p->y[0] + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(p->x[0] + x)) * 4u + 3u];
        }
        blast_alpha_[i] = std::move(alpha);
        blast_glow_[i].w = p->w;
        blast_glow_[i].h = p->h;
    }
    blast_alpha_state_ = 1;
    return true;
}

// The glow of one frame of the explosion: its light blurred (sigma 7), times 2.2, at most 0.34 of opacity, warm orange, added; made at 1x and enlarged with a filter, for a glow is smooth
const Renderer::BlastGlow* Renderer::blast_glow(uint32_t sprite) {
    if (sprite < kFirstBlast || sprite >= kFirstBlast + blast_glow_.size()) return nullptr;
    BlastGlow& g = blast_glow_[sprite - kFirstBlast];
    if (g.built) return g.texture != nullptr ? &g : nullptr;
    if (!ensure_blast_alpha()) return nullptr;
    g.built = true;
    const std::vector<uint8_t>& alpha = blast_alpha_[sprite - kFirstBlast];
    if (alpha.empty() || g.w <= 0) return nullptr;
    const int32_t m = kBlastMargin, W = g.w + 2 * m, H = g.h + 2 * m;
    std::vector<double> kernel(static_cast<size_t>(2 * m + 1));
    double total = 0.0;
    for (int32_t i = -m; i <= m; ++i) total += kernel[static_cast<size_t>(i + m)] = std::exp(-0.5 * i * i / (kBlastSigma * kBlastSigma));
    for (double& k : kernel) k /= total;
    std::vector<double> a(static_cast<size_t>(W) * static_cast<size_t>(H), 0.0), t(a.size(), 0.0);
    for (int32_t y = 0; y < g.h; ++y) {
        for (int32_t x = 0; x < g.w; ++x) a[static_cast<size_t>(y + m) * static_cast<size_t>(W) + static_cast<size_t>(x + m)] = alpha[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] / 255.0;
    }
    for (int32_t y = 0; y < H; ++y) {                                                // across
        for (int32_t x = 0; x < W; ++x) {
            double s = 0.0;
            for (int32_t i = -m; i <= m; ++i) {
                const int32_t xx = x + i;
                if (xx >= 0 && xx < W) s += a[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(xx)] * kernel[static_cast<size_t>(i + m)];
            }
            t[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] = s;
        }
    }
    std::vector<uint8_t> px(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u);
    bool any = false;
    for (int32_t y = 0; y < H; ++y) {                                                // down
        for (int32_t x = 0; x < W; ++x) {
            double s = 0.0;
            for (int32_t i = -m; i <= m; ++i) {
                const int32_t yy = y + i;
                if (yy >= 0 && yy < H) s += t[static_cast<size_t>(yy) * static_cast<size_t>(W) + static_cast<size_t>(x)] * kernel[static_cast<size_t>(i + m)];
            }
            const double glow = std::clamp(s * kBlastGain, 0.0, 1.0) * kBlastPeak;
            uint8_t* o = &px[(static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u];
            o[0] = 255;
            o[1] = 150;
            o[2] = 60;
            o[3] = static_cast<uint8_t>(std::lround(glow * 255.0));
            any = any || o[3] != 0;
        }
    }
    if (!any) return nullptr;
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
    if (!tex) return nullptr;
    SDL_UpdateTexture(tex, nullptr, px.data(), W * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    g.texture = tex;
    g.margin = m;
    return &g;
}

// One frame of the bomb's explosion as light: a warm glow on the ground, the whole picture by its own opacity (the world art's, or the original's sprite), and the fire part of it added once more
void Renderer::draw_blast_light(const ants::assets::AnimationSubItem& sub, int32_t sx, int32_t sy) {
    for (const auto& f : sub.frames) {
        const BlastGlow* glow = blast_glow(f.sprite_index);
        if (glow == nullptr) continue;
        const SDL_Rect dst = placed(sx + f.dx - glow->margin, sy + f.dy - glow->margin, glow->w + 2 * glow->margin, glow->h + 2 * glow->margin);
        SDL_RenderCopy(renderer_, glow->texture, nullptr, &dst);
    }
    draw_frame_parts(sub, sx, sy);
    int32_t scale = world_art_scale_now();
    SDL_Texture* sheet = world_art_sheet(world_art::kGroundPage, scale);
    if (sheet == nullptr) return;
    const size_t k = static_cast<size_t>(scale - 1);
    for (const auto& f : sub.frames) {
        const world_art::Picture* light = world_art_->find(kBlastLight + f.sprite_index, 0);
        const auto& original = archive_->get_sprite(f.sprite_index);
        if (light == nullptr || light->page != world_art::kGroundPage || light->w != static_cast<int32_t>(original.width) || light->h != static_cast<int32_t>(original.height)) continue;
        world_art_filter(sheet, scale);
        const SDL_Rect src = {light->x[k], light->y[k], light->w * scale, light->h * scale};
        const SDL_Rect dst = placed(sx + f.dx, sy + f.dy, light->w, light->h);
        set_light(sheet, SDL_BLENDMODE_ADD, kLightAddMod);
        SDL_RenderCopy(renderer_, sheet, &src, &dst);
        set_light(sheet, SDL_BLENDMODE_BLEND, 255);
    }
}

}  // namespace ants::app

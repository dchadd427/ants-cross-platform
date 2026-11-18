// The HUD art's cursors (see test_hud_art.cpp; docs/VIEW_AND_HUD.md, "HUD art"): the new pointers that the art draws in the place of the original's sixteen cursor animations.
//   [cursors]        the shipped art has every one of the original's cursors, marked as one, with the original's number of frames, frame times, sprite sizes and offsets from the click point (the hot spots);
//                    every sprite is in the sheet of every scale, and is drawn on that scale's own pixels; the frame that shows at any moment is the original's
//   [cursor-draw]    the real renderer at 1x, 2x and 3x: the art's pointer for every cursor and frame stands where the original's sprite stands (nothing outside its box, the box's corners the
//                    original's), and the pointer's tip is the click point's own pixel in both sets
//   [cursor-classic] without the art, and with an art that cannot give a cursor, the original's sprites are drawn exactly, at their place
//   [cursor-damaged] an art whose cursor changes the click point, a frame time or a sprite's size, or is not marked as a cursor, or is missing, gives the original's cursor, one cursor at a time
// Look only: the same click points, the same clicks.
#include "hud_art_fixtures.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>

namespace {

using hud_art::Art;

constexpr const char* kCursorNames[] = {"c_normal", "c_select", "c_scroll", "c_mov1", "c_targ1", "c_attack", "c_food", "c_cant", "CUR_N", "CUR_NE", "CUR_E", "CUR_SE", "CUR_S", "CUR_SW", "CUR_W", "CUR_NW"};

/// The cursors that the game shows (renderer.hpp, CursorType) and their names in the original's archive; c_scroll and c_cant are in the archive too, and the program never shows them
struct Used {
    CursorType type;
    const char* name;
};
constexpr Used kUsed[] = {{CursorType::Normal, "c_normal"}, {CursorType::Select, "c_select"}, {CursorType::Move, "c_mov1"}, {CursorType::ThiefTarget, "c_targ1"}, {CursorType::Attack, "c_attack"},
                          {CursorType::ScrollN, "CUR_N"},   {CursorType::ScrollNE, "CUR_NE"}, {CursorType::ScrollE, "CUR_E"}, {CursorType::ScrollSE, "CUR_SE"}, {CursorType::ScrollS, "CUR_S"},
                          {CursorType::ScrollSW, "CUR_SW"}, {CursorType::ScrollW, "CUR_W"},   {CursorType::ScrollNW, "CUR_NW"}, {CursorType::Food, "c_food"}};

/// A frame of an original cursor as the archive holds it (read here by the test itself, not through the art's own check)
struct OrigFrame {
    uint32_t ms;                 // its time (the archive's third value)
    int32_t dx, dy;              // its sprite's corner from the click point
    int32_t w, h;                // its sprite's size
    std::string sprite;          // its sprite's file name
    size_t parts;                // how many pieces the frame has
};

std::vector<OrigFrame> original_frames(const assets::AssetArchive& arc, const std::string& name) {
    std::vector<OrigFrame> out;
    const assets::AnimationSequence* seq = arc.find_animation(name);
    if (seq == nullptr) return out;
    for (const assets::AnimationSubItem& sub : seq->subitems) {
        OrigFrame f{sub.val3, 0, 0, 0, 0, std::string(), sub.frames.size()};
        if (!sub.frames.empty()) {
            const assets::Sprite& sp = arc.get_sprite(sub.frames[0].sprite_index);
            f.dx = sub.frames[0].dx;
            f.dy = sub.frames[0].dy;
            f.w = static_cast<int32_t>(sp.width);
            f.h = static_cast<int32_t>(sp.height);
            f.sprite = sp.name;
        }
        out.push_back(f);
    }
    return out;
}

/// How the original picks the frame of a cursor at a moment: the times add up to one loop (a time of 0 counts as 100), the moment is taken inside the loop, and the first frame whose end is after it shows
size_t original_frame_at(const std::vector<OrigFrame>& frames, uint32_t ms) {
    uint32_t total = 0;
    for (const OrigFrame& f : frames) total += f.ms > 0 ? f.ms : 100;
    uint32_t now = total == 0 ? 0 : ms % total;
    uint32_t accum = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const uint32_t dur = frames[i].ms > 0 ? frames[i].ms : 100;
        if (now < accum + dur) return i;
        accum += dur;
    }
    return frames.empty() ? 0 : frames.size() - 1;
}

std::vector<hud_art::OriginalCursorFrame> for_fit(const std::vector<OrigFrame>& frames) {
    std::vector<hud_art::OriginalCursorFrame> out;
    for (const OrigFrame& f : frames) out.push_back(hud_art::OriginalCursorFrame{static_cast<int32_t>(f.ms), f.dx, f.dy, f.w, f.h});
    return out;
}

/// The canvas of 960 x 540 shown in a window of `k` device pixels for each canvas pixel, drawn by the real renderer
struct ScaledRig {
    ScaledRig(const assets::AssetArchive& archive, int32_t scale) : k(scale) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("hud-art-cursors", 0, 0, 960 * k, 540 * k, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(960, 540);
        renderer.set_hud_team(TEAM_NONE);                  // (the original's pointer is drawn from the raw palette: so is the sprite that stands for it)
        ok = true;
    }
    ~ScaledRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    ScaledRig(const ScaledRig&) = delete;
    ScaledRig& operator=(const ScaledRig&) = delete;

    static constexpr uint8_t kBack[3] = {131, 118, 112};
    static constexpr int32_t kNear = 48;                    // canvas pixels round a click point that a picture holds
    /// a new frame of one plain color
    void blank() {
        renderer.begin_frame();
        renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{kBack[0], kBack[1], kBack[2], 255});
    }
    /// the device pixels of the square of canvas pixels round (cx, cy), kNear each way (the square lies inside the picture)
    std::vector<uint8_t> read_near(int32_t cx, int32_t cy) {
        std::vector<uint8_t> px(static_cast<size_t>(2 * kNear * k) * static_cast<size_t>(2 * kNear * k) * 4u, 0);
        const SDL_Rect area = {(cx - kNear) * k, (cy - kNear) * k, 2 * kNear * k, 2 * kNear * k};
        SDL_RenderReadPixels(renderer.get_sdl_renderer(), &area, SDL_PIXELFORMAT_RGBA32, px.data(), 2 * kNear * k * 4);
        return px;
    }
    int32_t k;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

struct Box {
    int32_t x0{1 << 30}, y0{1 << 30}, x1{-1}, y1{-1};      // the pixels that are not the plain color: [x0, x1] x [y0, y1], counted from the square's corner
    int64_t count{0};
    bool any() const { return count > 0; }
};

Box drawn_box(const std::vector<uint8_t>& px, int32_t side) {
    Box b;
    for (int32_t y = 0; y < side; ++y) {
        for (int32_t x = 0; x < side; ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(side) + static_cast<size_t>(x)) * 4;
            if (px[i] == ScaledRig::kBack[0] && px[i + 1] == ScaledRig::kBack[1] && px[i + 2] == ScaledRig::kBack[2]) continue;
            b.x0 = std::min(b.x0, x);
            b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x);
            b.y1 = std::max(b.y1, y);
            ++b.count;
        }
    }
    return b;
}

/// A copy of the shipped art in a new folder with the manifest as `edit` changes it; the sheets are copied as they are. The folders are removed by remove_variants()
std::vector<std::string> g_variants;
std::string art_variant(const std::string& tag, const std::function<std::string(const std::string&)>& edit) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ants_hud_art_cursors_" + tag + "_" + std::to_string(static_cast<long long>(SDL_GetTicks())));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    std::ifstream in(art_folder() + "/hud_art.txt", std::ios::binary);
    const std::string manifest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream out(dir / "hud_art.txt", std::ios::binary | std::ios::trunc);
    out << edit(manifest);
    out.close();
    for (const char* name : {"sheet_1x.png", "sheet_2x.png", "sheet_3x.png"}) fs::copy_file(art_folder() + "/" + name, dir / name, fs::copy_options::overwrite_existing, ignored);
    g_variants.push_back(dir.string());
    return dir.string();
}

void remove_variants() {
    std::error_code ignored;
    for (const std::string& dir : g_variants) std::filesystem::remove_all(dir, ignored);
    g_variants.clear();
}

/// The manifest with the lines of one animation changed: `edit` is given the animation record and its frame records (one string, one line each) and returns the lines that replace them
std::string with_animation(const std::string& manifest, const std::string& name, const std::function<std::vector<std::string>(std::vector<std::string>)>& edit) {
    std::istringstream in(manifest);
    std::string line;
    std::vector<std::string> out;
    bool have = static_cast<bool>(std::getline(in, line));
    while (have) {
        if (line == "anim " + name || line.rfind("anim " + name + " ", 0) == 0) {
            std::vector<std::string> block = {line};
            while ((have = static_cast<bool>(std::getline(in, line))) && line.rfind("frame ", 0) == 0) block.push_back(line);
            for (const std::string& l : edit(block)) out.push_back(l);
            continue;                                       // (the line that ended the block is the next record: it is looked at again)
        }
        out.push_back(line);
        have = static_cast<bool>(std::getline(in, line));
    }
    std::string text;
    for (const std::string& l : out) text += l + "\n";
    return text;
}

std::string replace_first(std::string text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

/// The original's pointer pictures: every frame of cursor `name` as the original draws it (its sprite at the click point plus the frame's offset), the square round the click point of a rig
std::vector<std::vector<uint8_t>> original_pictures(ScaledRig& rig, const std::vector<OrigFrame>& frames, int32_t cx, int32_t cy) {
    std::vector<std::vector<uint8_t>> out;
    for (const OrigFrame& f : frames) {
        rig.blank();
        rig.renderer.draw_named_sprite(f.sprite, cx + f.dx, cy + f.dy);
        out.push_back(rig.read_near(cx, cy));
    }
    return out;
}

bool is_one_of(const std::vector<std::vector<uint8_t>>& pictures, const std::vector<uint8_t>& px) {
    return std::find(pictures.begin(), pictures.end(), px) != pictures.end();
}

}  // namespace

void test_cursors(const assets::AssetArchive& arc, const Art& art) {
    group("cursors", "the art has every one of the original's cursors with its frames, times, sprite sizes and offsets from the click point; every sprite is in every scale's sheet; the frame shown is the original's");
    size_t frames_total = 0;
    std::vector<std::string> sprites_seen;
    for (const char* name : kCursorNames) {
        const std::string at = std::string(name) + ": ";
        const std::vector<OrigFrame> orig = original_frames(arc, name);
        const hud_art::Animation* a = art.find_animation(name);
        check(!orig.empty() && a != nullptr && a->cursor, at + "the art has the cursor, marked as one");
        if (orig.empty() || a == nullptr) continue;
        frames_total += orig.size();
        check(a->frames.size() == orig.size(), at + "it has the original's " + std::to_string(orig.size()) + " frame(s)");
        for (size_t i = 0; i < orig.size() && i < a->frames.size(); ++i) {
            const std::string fat = at + "frame " + std::to_string(i) + ": ";
            const hud_art::Frame& f = a->frames[i];
            check(orig[i].parts == 1 && f.parts.size() == 1, fat + "one piece, as the original's");
            if (orig[i].parts != 1 || f.parts.size() != 1) continue;
            const hud_art::Sprite* s = art.sprite_at(f.parts[0].sprite);
            check(f.ms > 0 && static_cast<uint32_t>(f.ms) == orig[i].ms, fat + "the original's time (" + std::to_string(orig[i].ms) + " ms)");
            check(f.parts[0].x == orig[i].dx && f.parts[0].y == orig[i].dy, fat + "the corner is " + std::to_string(orig[i].dx) + ", " + std::to_string(orig[i].dy) + " from the click point, as the original's");
            check(s != nullptr && s->w == orig[i].w && s->h == orig[i].h, fat + "the sprite is the original's " + std::to_string(orig[i].w) + " x " + std::to_string(orig[i].h));
            if (s != nullptr && std::find(sprites_seen.begin(), sprites_seen.end(), s->name) == sprites_seen.end()) sprites_seen.push_back(s->name);
        }
        check(hud_art::cursor_fits(art, *a, for_fit(orig)), at + "the art's own check agrees that it keeps the click point");
    }
    check(frames_total > 16 && !sprites_seen.empty() && sprites_seen.size() < frames_total, "the sixteen cursors have " + std::to_string(frames_total) + " frames between them, in " + std::to_string(sprites_seen.size()) + " different sprites");

    // every sprite is in the sheet of every scale, drawn on that scale's own pixels: it has opaque pixels, about the scale squared times as many as at 1x
    DecodedImage one;
    check(art.sheet_pixels(1, one), "the sheet of 1x is read");
    for (const int32_t k : hud_art::Art::kScales) {
        DecodedImage sheet;
        if (!art.sheet_pixels(k, sheet) || one.rgba.empty()) {
            check(false, "the sheet of " + std::to_string(k) + "x is read");
            continue;
        }
        const auto opaque = [](const DecodedImage& im, const hud_art::Sprite& s, int32_t scale) {
            int64_t n = 0;
            for (int32_t y = 0; y < s.h * scale; ++y) {
                for (int32_t x = 0; x < s.w * scale; ++x) {
                    const size_t i = (static_cast<size_t>(s.sheet_y * scale + y) * static_cast<size_t>(im.width) + static_cast<size_t>(s.sheet_x * scale + x)) * 4;
                    if (im.rgba[i + 3] >= 128) ++n;
                }
            }
            return n;
        };
        std::string bad;
        for (const std::string& name : sprites_seen) {
            const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
            if (s == nullptr) {
                bad += " " + name;
                continue;
            }
            const int64_t n1 = opaque(one, *s, 1);
            const int64_t nk = opaque(sheet, *s, k);
            const double expected = static_cast<double>(n1) * k * k;
            if (n1 == 0 || nk == 0 || static_cast<double>(nk) < expected * 0.55 || static_cast<double>(nk) > expected * 1.6) bad += " " + name + "(" + std::to_string(n1) + "/" + std::to_string(nk) + ")";
        }
        check(bad.empty(), "at " + std::to_string(k) + "x every cursor sprite is in the sheet, drawn at that scale (opaque pixels about " + std::to_string(k * k) + " times the 1x count)" + bad);
    }

    // the frame shown at a moment is the original's, for every cursor and every millisecond of two loops
    for (const char* name : kCursorNames) {
        const std::vector<OrigFrame> orig = original_frames(arc, name);
        const hud_art::Animation* a = art.find_animation(name);
        if (orig.empty() || a == nullptr) continue;
        uint32_t total = 0;
        for (const OrigFrame& f : orig) total += f.ms;
        size_t wrong = 0;
        for (uint32_t ms = 0; ms < 2 * total + 5; ++ms) wrong += hud_art::frame_at(*a, ms) != original_frame_at(orig, ms) ? 1u : 0u;
        check(wrong == 0, std::string(name) + ": the frame at every millisecond of two loops is the one the original shows (" + std::to_string(wrong) + " differ)");
    }
}

void test_cursor_draw(const assets::AssetArchive& arc, const Art& art) {
    group("cursor-draw", "the real renderer at 1x, 2x and 3x: the art's pointer stands where the original's sprite stands, for every cursor and every frame");
    for (const int32_t k : hud_art::Art::kScales) {
        ScaledRig rig(arc, k);
        check(rig.ok && rig.renderer.art_scale_now() == k, std::to_string(k) + "x: the renderer is up, and takes the sheet of its scale");
        if (!rig.ok) continue;
        const int32_t side = 2 * ScaledRig::kNear * k;
        for (const Used& used : kUsed) {
            const std::vector<OrigFrame> orig = original_frames(arc, used.name);
            if (orig.empty()) continue;
            uint32_t at_ms = 0;
            for (size_t i = 0; i < orig.size(); ++i) {
                const std::string at = std::to_string(k) + "x " + used.name + " frame " + std::to_string(i) + ": ";
                for (const std::pair<int32_t, int32_t> click : {std::make_pair(301, 207), std::make_pair(100, 80)}) {
                    // the original's sprite of the frame, put where the original puts it: the click point plus the frame's offset
                    rig.blank();
                    rig.renderer.draw_named_sprite(orig[i].sprite, click.first + orig[i].dx, click.second + orig[i].dy);
                    const Box classic = drawn_box(rig.read_near(click.first, click.second), side);
                    // the art's pointer at the middle of that frame's time
                    rig.blank();
                    rig.renderer.pin_animation_clock(at_ms + orig[i].ms / 2);
                    rig.renderer.render_software_cursor(used.type, click.first, click.second, 0, &art);
                    rig.renderer.unpin_animation_clock();
                    const Box drawn = drawn_box(rig.read_near(click.first, click.second), side);
                    // the square's corner is (click - 48) canvas pixels; the sprite's corner is (click + dx): 48 + dx canvas pixels into it
                    const int32_t bx0 = (ScaledRig::kNear + orig[i].dx) * k, by0 = (ScaledRig::kNear + orig[i].dy) * k;
                    const int32_t bx1 = bx0 + orig[i].w * k - 1, by1 = by0 + orig[i].h * k - 1;
                    check(classic.any() && drawn.any(), at + "both sets draw something");
                    check(drawn.any() && drawn.x0 >= bx0 && drawn.y0 >= by0 && drawn.x1 <= bx1 && drawn.y1 <= by1, at + "the art's pixels are inside the original's sprite box");
                    // the sides of what is drawn: the upper left within a canvas pixel of the original's (that is where the click point is), the lower right within three (the new pointer's tail is a little
                    // shorter; every other cursor fills its box as the original's does)
                    check(classic.any() && drawn.any() && std::abs(drawn.x0 - classic.x0) <= k && std::abs(drawn.y0 - classic.y0) <= k && std::abs(drawn.x1 - classic.x1) <= 3 * k && std::abs(drawn.y1 - classic.y1) <= 3 * k,
                          at + "its sides are where the original's are (" + std::to_string(drawn.x0 - classic.x0) + " " + std::to_string(drawn.y0 - classic.y0) + " " + std::to_string(drawn.x1 - classic.x1) + " " + std::to_string(drawn.y1 - classic.y1) + " device pixels off)");
                    if (std::string(used.name) == "c_normal") {
                        const int32_t tip = ScaledRig::kNear * k;                  // the click point's own canvas pixel: k device pixels wide
                        check(drawn.x0 >= tip && drawn.x0 < tip + k && drawn.y0 >= tip && drawn.y0 < tip + k && classic.x0 >= tip && classic.x0 < tip + k && classic.y0 >= tip && classic.y0 < tip + k,
                              at + "the tip is in the click point's own pixel, in both sets");
                    }
                }
                at_ms += orig[i].ms;
            }
        }
    }
}

void test_cursor_classic(const assets::AssetArchive& arc, const Art& art) {
    group("cursor-classic", "without the art, and with an art that gives no cursor, the original's sprites are drawn exactly, at their place; the art's pointer is not the original's");
    const std::string empty = art_variant("none", [](const std::string& m) {
        std::istringstream in(m);
        std::string line, out;
        bool in_cursor = false;
        while (std::getline(in, line)) {
            if (line.rfind("anim ", 0) == 0) in_cursor = line.size() > 7 && line.compare(line.size() - 7, 7, " cursor") == 0;
            if (!in_cursor) out += line + "\n";
        }
        return out;
    });
    Art without;
    check(without.load(empty) && without.loaded() && without.find_animation("c_normal") == nullptr && without.find_animation("buthlpu") != nullptr, "an art without the cursors is read, with the rest");
    for (const int32_t k : {1, 3}) {
        ScaledRig rig(arc, k);
        check(rig.ok, std::to_string(k) + "x: the renderer is up");
        if (!rig.ok) continue;
        const int32_t cx = 301, cy = 207;
        for (const Used& used : kUsed) {
            const std::string at = std::to_string(k) + "x " + used.name + ": ";
            const std::vector<std::vector<uint8_t>> frames = original_pictures(rig, original_frames(arc, used.name), cx, cy);
            const auto shown = [&](const Art* with) {
                rig.blank();
                rig.renderer.render_software_cursor(used.type, cx, cy, 0, with);
                return rig.read_near(cx, cy);
            };
            check(is_one_of(frames, shown(nullptr)), at + "with no art the pointer is one of the original's frames, pixel for pixel, at the click point plus its offset");
            check(is_one_of(frames, shown(&without)), at + "with an art that has no cursors it is the same");
            rig.renderer.pin_animation_clock(1);
            check(!is_one_of(frames, shown(&art)), at + "with the art the pointer is not any of the original's frames");
            rig.renderer.unpin_animation_clock();
        }
    }
    remove_variants();
}

void test_cursor_damaged(const assets::AssetArchive& arc, const Art& art) {
    group("cursor-damaged", "an art that changes a cursor's click point, a frame time or a sprite's size, or does not mark it a cursor, gives the original's cursor, one cursor at a time");
    const std::vector<OrigFrame> attack = original_frames(arc, "c_attack");
    const std::vector<OrigFrame> normal = original_frames(arc, "c_normal");
    const std::vector<OrigFrame> move = original_frames(arc, "c_mov1");
    const hud_art::Animation* real_normal = art.find_animation("c_normal");
    check(!attack.empty() && !normal.empty() && !move.empty() && real_normal != nullptr, "the shipped cursors are there");
    if (attack.empty() || normal.empty() || move.empty() || real_normal == nullptr) return;

    const auto variant = [&](const std::string& tag, const std::string& name, const std::function<std::vector<std::string>(std::vector<std::string>)>& edit) {
        const std::string dir = art_variant(tag, [&](const std::string& m) { return with_animation(m, name, edit); });
        auto loaded = std::make_unique<Art>();
        check(loaded->load(dir) && loaded->loaded(), "the art with a changed " + name + " (" + tag + ") is read");
        return loaded;
    };
    const auto fits = [&](const Art& a, const std::string& name, const std::vector<OrigFrame>& orig) {
        const hud_art::Animation* an = a.find_animation(name);
        return an != nullptr && hud_art::cursor_fits(a, *an, for_fit(orig));
    };
    check(fits(art, "c_normal", normal) && fits(art, "c_attack", attack) && fits(art, "c_mov1", move), "the shipped pointer, attack and move cursors fit");
    const auto moved_first = variant("moved1", "c_normal", [](std::vector<std::string> b) {              // the click point moved by a pixel
        b[1] = replace_first(b[1], " 0 0", " 1 0");
        return b;
    });
    check(!fits(*moved_first, "c_normal", normal) && fits(*moved_first, "c_attack", attack), "a pointer whose corner is moved by a pixel does not fit; the others still do");
    const auto moved_last = variant("movedlast", "c_attack", [](std::vector<std::string> b) {
        std::istringstream words(b.back());
        std::string kind, ms, sprite, x, y;
        words >> kind >> ms >> sprite >> x >> y;
        b.back() = kind + " " + ms + " " + sprite + " " + x + " " + std::to_string(std::stoi(y) + 1);
        return b;
    });
    check(!fits(*moved_last, "c_attack", attack) && fits(*moved_last, "c_normal", normal), "...also when it is the last frame that moves");
    const auto slow = variant("time", "c_attack", [](std::vector<std::string> b) {
        b[1] = replace_first(b[1], "frame 100 ", "frame 110 ");
        return b;
    });
    check(!fits(*slow, "c_attack", attack), "a frame time that is not the original's does not fit");
    const auto shorter = variant("short", "c_attack", [](std::vector<std::string> b) {
        b.pop_back();
        return b;
    });
    check(!fits(*shorter, "c_attack", attack), "one frame fewer does not fit");
    const auto longer = variant("long", "c_mov1", [](std::vector<std::string> b) {
        b.push_back(b.back());
        return b;
    });
    check(!fits(*longer, "c_mov1", move), "one frame more does not fit");
    const auto other_size = variant("size", "c_attack", [](std::vector<std::string> b) {
        std::istringstream words(b[1]);
        std::string kind, ms, sprite, x, y;
        words >> kind >> ms >> sprite >> x >> y;
        b[1] = kind + " " + ms + " cursor_c_atk2 " + x + " " + y;
        return b;
    });
    check(!fits(*other_size, "c_attack", attack), "a sprite of another size in a frame does not fit");
    // a sprite that is a pixel wider, or a pixel taller, than the original's (the sprite's own record changes: its sheet is the same)
    const auto sized = [&](const std::string& tag, const std::string& from, const std::string& to) {
        auto loaded = std::make_unique<Art>();
        check(loaded->load(art_variant(tag, [&](const std::string& m) { return replace_first(m, from, to); })) && loaded->loaded(), "the art with a sprite of another size (" + tag + ") is read");
        return loaded;
    };
    const auto wider = sized("wider", "sprite cursor_c_normal 13 23 ", "sprite cursor_c_normal 14 23 ");
    const auto taller = sized("taller", "sprite cursor_c_normal 13 23 ", "sprite cursor_c_normal 13 24 ");
    check(!fits(*wider, "c_normal", normal) && !fits(*taller, "c_normal", normal) && fits(*wider, "c_attack", attack), "a sprite a pixel wider, or a pixel taller, than the original's does not fit");
    const auto unmarked = variant("unmarked", "c_normal", [](std::vector<std::string> b) {                // its pieces would be placed by the layout's rules
        b[0] = "anim c_normal";
        return b;
    });
    check(!fits(*unmarked, "c_normal", normal) && fits(*unmarked, "c_attack", attack), "an animation that is not marked as a cursor does not fit");
    check(!hud_art::cursor_fits(art, *real_normal, {}), "a cursor never fits an original that has no frames");

    // what the renderer draws for those: the original's cursor for the one that does not fit, the art's for the others
    ScaledRig rig(arc, 1);
    check(rig.ok, "the renderer is up");
    if (rig.ok) {
        const int32_t cx = 301, cy = 207;
        const std::vector<std::vector<uint8_t>> original_pointer = original_pictures(rig, normal, cx, cy);
        const std::vector<std::vector<uint8_t>> original_attack = original_pictures(rig, attack, cx, cy);
        const auto shown = [&](CursorType type, const Art& a) {
            rig.blank();
            rig.renderer.pin_animation_clock(1);
            rig.renderer.render_software_cursor(type, cx, cy, 0, &a);
            rig.renderer.unpin_animation_clock();
            return rig.read_near(cx, cy);
        };
        check(is_one_of(original_pointer, shown(CursorType::Normal, *moved_first)), "the renderer draws the original's pointer when the art's would move the click point");
        check(!is_one_of(original_pointer, shown(CursorType::Normal, art)), "...and the art's when it does not");
        check(shown(CursorType::Attack, *moved_first) == shown(CursorType::Attack, art) && !is_one_of(original_attack, shown(CursorType::Attack, art)), "...and the art's cursors of the same art that fit are still the art's");
        check(is_one_of(original_pointer, shown(CursorType::Normal, *wider)) && is_one_of(original_pointer, shown(CursorType::Normal, *taller)), "the renderer draws the original's pointer when the art's sprite is not the original's size");
        check(is_one_of(original_pointer, shown(CursorType::Normal, *unmarked)), "the renderer draws the original's pointer when the art's is not marked as a cursor");
        check(is_one_of(original_attack, shown(CursorType::Attack, *slow)), "the renderer draws the original's attack cursor when the art's times are not the original's");
        check(is_one_of(original_attack, shown(CursorType::Attack, *moved_last)) && is_one_of(original_attack, shown(CursorType::Attack, *shorter)), "...and when its last frame moved, or a frame is missing");
    }
    remove_variants();
}

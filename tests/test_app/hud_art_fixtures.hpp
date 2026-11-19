// The fixtures of the HUD art tests (test_hud_art.cpp holds main() and the model's cases, test_hud_art_look.cpp the cases that draw). Inline or in an anonymous namespace.
#pragma once

#include <cmath>
#include <cstdlib>
#include <iterator>

#include "wide_hud_fixtures.hpp"

#include "ants_app/hud_art.hpp"
#include "ants_app/pedestal.hpp"
#include "ants_app/png_decode.hpp"

namespace {

struct Shape {
    const char* name;
    int32_t w;
    int32_t h;
};

/// The seven shapes of the picture (docs/VIEW_AND_HUD.md): the original's 4:3 and the six wide ones
constexpr Shape kShapes[7] = {{"4:3", 640, 480}, {"16:9", 960, 540}, {"3:2", 960, 640}, {"16:10", 960, 600}, {"20:9", 1200, 540}, {"21:9", 1260, 540}, {"32:9", 1920, 540}};

inline std::string art_folder() { return std::string(ANTS_SOURCE_DIR) + "/assets/hud_art"; }

/// The art that the game ships: false (and a message) when the manifest cannot be read
inline bool load_art(hud_art::Art& art) {
    if (art.load(art_folder())) return true;
    std::fprintf(stderr, "cannot read the HUD art: %s\n", art.what().c_str());
    return false;
}

/// Records the copies of the HUD art that a HUD asks for, besides everything that Spy records
class ArtSpy : public Spy {
public:
    using Spy::Spy;
    struct Copy {
        int32_t sprite{0};
        int32_t x{0}, y{0}, w{0}, h{0};
        int32_t sx{0}, sy{0}, sw{0}, sh{0};
    };
    void draw_art(const hud_art::Art&, int32_t sprite, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) override {
        copies.push_back(Copy{sprite, x, y, w, h, sx, sy, sw, sh});
    }
    /// The copies of one named sprite
    std::vector<Copy> of_sprite(const hud_art::Art& art, const std::string& name) const {
        std::vector<Copy> out;
        const int32_t id = art.find_sprite(name);
        for (const Copy& c : copies) {
            if (c.sprite == id) out.push_back(c);
        }
        return out;
    }
    std::vector<Copy> copies;

    /// The pictures with see-through pixels that the HUD lays (the quick help's kept pictures, the Classic corner), and the lines of the windows' face
    struct Blended {
        int32_t x{0}, y{0}, w{0}, h{0};
        std::vector<uint8_t> rgba;
    };
    struct Line {
        std::string text;
        int32_t x{0}, baseline{0}, em_tenths{0};
    };
    void draw_rgba_blended(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override {
        blended.push_back(Blended{x, y, w, h, std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u)});
    }
    bool dialog_text_ready() const override { return dialog_face; }
    int32_t dialog_text_width(const std::string& text, int32_t em_tenths) const override { return static_cast<int32_t>(text.size()) * em_tenths / 20; }      // (a text of n letters: n x em / 2 pixels)
    void draw_dialog_text(const std::string& text, int32_t x, int32_t baseline, assets::ColorRGBA, int32_t em_tenths) override { lines.push_back(Line{text, x, baseline, em_tenths}); }
    std::vector<Blended> blended;
    std::vector<Line> lines;
    bool dialog_face{false};                       // does this renderer have the windows' face (IRenderer::dialog_text_ready)
};

/// A decoded picture of 8-bit RGBA read from a file of the repository
inline bool read_png(const std::string& path, DecodedImage& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return decode_png_rgba(bytes.data(), bytes.size(), out);
}

}  // namespace

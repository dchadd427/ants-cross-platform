#pragma once

// The text of a window that is drawn in units of its own (the countdown card, the results screen): sizes are given in units, and a unit is `f` canvas pixels. The game's face at any size when the
// renderer has it, else the nearest of the original's sizes (a renderer that only records the calls).

#include <cmath>
#include <cstdint>
#include <string>

#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"

namespace ants::app {

/// What a line measures, in a window of units: the same arithmetic as the pen's, from a renderer that is only asked (const)
class UnitMeter {
public:
    UnitMeter(const IRenderer& renderer, double f) : r_(renderer), f_(f), face_(renderer.face_text_ready()) {}
    /// A size in units as the em of a line, in tenths of a canvas pixel
    int32_t em_tenths(double units) const noexcept { return static_cast<int32_t>(std::lround(units * f_ * 10.0)); }
    int32_t width(const std::string& text, double em_units) const {
        if (face_) return r_.face_text_width(text, em_tenths(em_units));
        return r_.get_text_width(text, nearest(em_units));
    }
    /// The text cut at the end with three dots until it is no wider than `max_w`
    std::string fit(std::string text, int32_t max_w, double em_units) const {
        if (width(text, em_units) <= max_w) return text;
        while (!text.empty() && width(text + "...", em_units) > max_w) cut_last_character(text);
        return text + "...";
    }
    /// The last character of a UTF-8 text taken off (never half of one)
    static void cut_last_character(std::string& text) {
        if (text.empty()) return;
        size_t cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        text.resize(cut);
    }

protected:
    FontSize nearest(double em_units) const {
        const double cell = em_units * f_ * 1.2;
        for (FontSize size : {FontSize::Px12, FontSize::Px14, FontSize::Px18, FontSize::Px20, FontSize::Px24, FontSize::Px27, FontSize::Px35}) {
            if (static_cast<double>(font_cell_height(size)) + 0.5 >= cell) return size;
        }
        return FontSize::Px96;
    }
    const IRenderer& r_;
    double f_;
    bool face_;
};

class UnitPen : public UnitMeter {
public:
    UnitPen(IRenderer& renderer, double f) : UnitMeter(renderer, f), w_(renderer) {}
    /// `x` is the left edge (align 0), the middle (1) or the right edge (2) of the line, `baseline` its baseline in canvas pixels
    void draw(const std::string& text, int32_t x, int32_t baseline, assets::ColorRGBA color, double em_units, int align = 0) const {
        if (text.empty()) return;
        const int32_t w = width(text, em_units);
        const int32_t left = align == 0 ? x : align == 1 ? x - w / 2 : x - w;
        if (face_) {
            w_.draw_face_text(text, left, baseline, color, em_tenths(em_units));
        } else {
            const FontSize size = nearest(em_units);
            w_.draw_text(text, left, baseline - font_cell_height(size) * 4 / 5, color, size);
        }
    }

private:
    IRenderer& w_;
};

}  // namespace ants::app

#include "ants_app/scorecard.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/results_layout.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_sim/game_strings.hpp"

#include <algorithm>

namespace ants::app {

namespace {

// Every label of the screen has the colour 0xdfe7ef (a COLORREF: R 239, G 231, B 223, FUN_01011821(label, 0xdfe7ef, -1, -1))
const assets::ColorRGBA kLabelColour{239, 231, 223, 255};

// One number of a row: the original's single-line label (FUN_010116cb: no wrap, left aligned at its x, a surface as wide as the label that clips what does not fit; docs 5.49) drawn in the original's
// digits, which are 8 px wide (the bundled face's are wider: the text is squeezed to the original's width for as many digits)
void draw_counter(IRenderer& renderer, const std::string& text, int32_t x, int32_t y, int32_t box_w) {
    renderer.set_clip_rect(x, y, box_w, ScorecardModal::LABEL_H);
    renderer.draw_text_squeezed(text, x, y, kLabelColour, FontSize::Px18, kResultsDigitWidth * static_cast<int32_t>(text.size()));
    renderer.clear_clip_rect();
}

} // anonymous namespace

ScorecardModal::ScorecardModal() = default;

// The 16:9 page and the original's page have their own Leave button rectangles (the hit test is the rectangle of the picture that shows)
void ScorecardModal::set_wide_layout(bool wide) {
    if (wide_ == wide) return;
    wide_ = wide;
    const ResultsLayout& l = ResultsLayout::of(wide_);
    quit_ = ScreenButton(ButtonRect{l.leave.x, l.leave.y, l.leave.w, l.leave.h}, ButtonRect{l.leave_pressed.x, l.leave_pressed.y, l.leave_pressed.w, l.leave_pressed.h});
}

// FUN_010153a1: the screen is created; its portraits exist but are not on the screen yet, the Leave button does not exist yet, a label says that it waits, and a
// task that runs 250 ms later builds the rows as soon as every team's scores are in.
void ScorecardModal::show(const sim::MatchResult& result, uint8_t local_player_id) {
    is_active_ = true;
    phase_ = Phase::Waiting;
    elapsed_ms_ = 0.0;
    local_player_id_ = local_player_id;
    quit_.reset();
    audio_to_play_ = 0;
    rows_.clear();
    result_ = result;
    result_.present_mask = static_cast<uint8_t>(result.present_mask & shown_mask_);
}

void ScorecardModal::update(float dt_seconds) {
    if (!is_active_) return;
    elapsed_ms_ += static_cast<double>(dt_seconds) * 1000.0;
    if (phase_ == Phase::Waiting && elapsed_ms_ >= WAIT_MS) build_rows();
}

std::string ScorecardModal::name_of(uint8_t team) const {
    if (!player_names_[team % 4].empty()) return player_names_[team % 4];
    if (team == local_player_id_ && !local_player_name_.empty()) return local_player_name_;
    return sim::strings::colour_name(static_cast<uint8_t>(3u - (team & 3u)));        // strings 100 - 103
}

// FUN_010155ac: the rows (FUN_01015136), the labels and the portraits of every row, the Leave button, and then the cue (0x1015a4a): the winner cue when the local
// team or its ally is the first team of the top row, the loser cue otherwise.
void ScorecardModal::build_rows() {
    rows_.clear();
    for (const sim::ResultRow& r : result_.rows(local_player_id_)) {
        Row row;
        row.first = r.first;
        row.second = r.second;
        row.name = name_of(r.first);
        if (r.has_second()) row.name += " & " + name_of(r.second);
        if (row.name.size() > NAME_MAX_CHARS) row.name.resize(NAME_MAX_CHARS);        // the label's buffer holds 35 characters
        row.numbers = {std::to_string(r.score), std::to_string(r.friendly_lost), std::to_string(r.enemy_killed), std::to_string(r.new_hatched)};
        rows_.push_back(std::move(row));
    }
    audio_to_play_ = result_.is_winner(local_player_id_) ? sim::SoundID::VictoryFanfare : sim::SoundID::PlayerDefeat;
    phase_ = Phase::Rows;
}

bool ScorecardModal::handle_mouse_down(int32_t x, int32_t y) {
    if (!is_active_) return false;

    if (phase_ == Phase::Rows && quit_.on_press(x, y)) {
        play_sfx(sim::SoundID::ButtonClick);   // leave3 carries sound 0 (buttonclick.wav)
        return true;
    }

    return true; // Modal consumes all click events
}

bool ScorecardModal::handle_mouse_up(int32_t x, int32_t y) {
    if (!is_active_) return false;

    if (phase_ == Phase::Rows && quit_.on_release(x, y)) {      // the callback runs at the release, on the button, while it is still captured
        if (on_quit_) on_quit_();
        return true;
    }

    return true;
}

void ScorecardModal::handle_mouse_motion(int32_t x, int32_t y) {
    if (!is_active_) return;
    if (phase_ == Phase::Rows) quit_.on_move(x, y);
    else quit_.reset();
}

// One AntSlot (FUN_01021ba4): the animation agst301 at (x, y) in the colour of the team, running since the screen was created
void ScorecardModal::draw_portrait(IRenderer& renderer, const assets::AssetArchive& assets, uint8_t team, int32_t x, int32_t y) const {
    const auto* anim = assets.find_animation("agst301");
    if (anim == nullptr || anim->subitems.empty()) return;
    const size_t frame = Renderer::get_anim_subitem_by_time(*anim, static_cast<uint32_t>(elapsed_ms_));
    const auto& parts = anim->subitems[frame].frames;
    renderer.set_hud_team(team);
    for (size_t k = parts.size(); k-- > 0;) {
        renderer.draw_sprite(parts[k].sprite_index, x + parts[k].dx, y + parts[k].dy);
    }
    renderer.set_hud_team(0);
}

void ScorecardModal::render(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (!is_active_) return;
    const ResultsLayout& layout = ResultsLayout::of(wide_);

    // 1. The page. In the original's picture: the authentic re_screen composite (149 frame elements from Table 4 Animation 25), rendered in reverse order to produce the authentic 640x480 terracotta
    // layout with frames, banners, headers, boxes and column arrows. In the 16:9 picture: the same pieces recomposed for the whole canvas (results_layout.hpp).
    if (wide_) {
        draw_results_art(renderer, assets);
    } else {
        const auto* anim = assets.find_animation("re_screen");
        if (anim && !anim->subitems.empty()) {
            const auto& frames = anim->subitems[0].frames;
            for (size_t i = frames.size(); i-- > 0; ) {
                const auto& fr = frames[i];
                renderer.draw_sprite(fr.sprite_index, fr.dx, fr.dy);
            }
        } else {
            renderer.fill_rect(0, 0, ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight, assets::ColorRGBA{219, 75, 19, 255});
        }
    }

    // 2. While the scores are awaited: the label (100, 350) 385 x 50, 20 px high lines; nothing else
    if (phase_ == Phase::Waiting) {
        draw_label(renderer, sim::strings::text(sim::strings::kWaitingForScores), layout.waiting.x, layout.waiting.y, layout.waiting.w, kLabelColour, FontSize::Px20, false);
        return;
    }

    // 3. The rows: name (100, Y) 385 wide, the four numbers left aligned at 485 / 534 / 555 / 576 (the wide page: the numbers 320 further right, every row 30 lower), the ant portraits at (60, Y + 20) or
    // (45, Y + 20) and (75, Y + 20)
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        const int32_t y = layout.row_y(i);
        draw_label(renderer, row.name, layout.name_x, y, layout.name_w, kLabelColour, FontSize::Px18, false);
        for (size_t c = 0; c < 4; ++c) draw_counter(renderer, row.numbers[c], layout.column_x[c], y, layout.column_w[c]);
        if (row.has_second()) {
            draw_portrait(renderer, assets, row.first, layout.portrait_pair_x[0], y + layout.portrait_dy);
            draw_portrait(renderer, assets, row.second, layout.portrait_pair_x[1], y + layout.portrait_dy);
        } else {
            draw_portrait(renderer, assets, row.first, layout.portrait_alone_x, y + layout.portrait_dy);
        }
    }

    // 4. Top-right "Leave Game" button: animations leave1 / leave2 (hover) / leave3 (pressed), absolute coordinates (the wide page: 320 further right)
    draw_animation_frame0(renderer, assets, quit_.pressed() ? "leave3" : (quit_.hovered() ? "leave2" : "leave1"), layout.dx, 0);
}

} // namespace ants::app

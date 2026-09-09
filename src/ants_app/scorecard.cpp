#include "ants_app/scorecard.hpp"
#include "ants_app/renderer.hpp"

#include <algorithm>
#include <iostream>

namespace ants::app {

namespace {

const char* PLAYER_NAMES[4] = {
    "Green Team",
    "Red Team",
    "Blue Team",
    "Black Team"
};

} // anonymous namespace

ScorecardModal::ScorecardModal() = default;

void ScorecardModal::show(const sim::MatchResult& result, uint8_t local_player_id) {
    is_active_ = true;
    local_player_id_ = local_player_id;
    quit_hovered_ = false;
    quit_pressed_ = false;

    // Check if local player won
    bool local_won = result.is_winner(local_player_id);
    audio_to_play_ = local_won ? sim::SoundID::VictoryFanfare : sim::SoundID::PlayerDefeat;

    // Find winner entry
    uint8_t winner_id = result.winning_players.empty() ? 0 : result.winning_players[0];
    winner_entry_.player_id = winner_id;
    if (winner_id == local_player_id && !local_player_name_.empty()) {
        winner_entry_.name = local_player_name_;
    } else {
        winner_entry_.name = std::string(PLAYER_NAMES[winner_id % 4]);
    }
    winner_entry_.score = result.final_scores[winner_id % 4];
    winner_entry_.friendly_lost = result.stats[winner_id % 4].friendly_lost;
    winner_entry_.enemy_killed = result.stats[winner_id % 4].enemy_killed;
    winner_entry_.new_hatched = result.stats[winner_id % 4].new_hatched;
    winner_entry_.is_winner = true;

    // Assemble other player entries sorted by score descending
    other_entries_.clear();
    for (uint8_t p = 0; p < 4; ++p) {
        if (p == winner_id) continue;
        PlayerEntry pe;
        pe.player_id = p;
        pe.name = PLAYER_NAMES[p];
        pe.score = result.final_scores[p];
        pe.friendly_lost = result.stats[p].friendly_lost;
        pe.enemy_killed = result.stats[p].enemy_killed;
        pe.new_hatched = result.stats[p].new_hatched;
        pe.is_winner = false;
        other_entries_.push_back(pe);
    }

    std::sort(other_entries_.begin(), other_entries_.end(), [](const PlayerEntry& a, const PlayerEntry& b) {
        return a.score > b.score;
    });
}

bool ScorecardModal::handle_mouse_down(int32_t x, int32_t y) {
    if (!is_active_) return false;

    // Test Leave Game button
    if (x >= QUIT_BTN_X && x < (QUIT_BTN_X + QUIT_BTN_W) &&
        y >= QUIT_BTN_Y && y < (QUIT_BTN_Y + QUIT_BTN_H)) {
        quit_pressed_ = true;
        return true;
    }

    return true; // Modal consumes all click events
}

bool ScorecardModal::handle_mouse_up(int32_t x, int32_t y) {
    if (!is_active_) return false;

    if (quit_pressed_) {
        quit_pressed_ = false;
        if (x >= QUIT_BTN_X && x < (QUIT_BTN_X + QUIT_BTN_W) &&
            y >= QUIT_BTN_Y && y < (QUIT_BTN_Y + QUIT_BTN_H)) {
            if (on_quit_) on_quit_();
            return true;
        }
    }

    return true;
}

void ScorecardModal::handle_mouse_motion(int32_t x, int32_t y) {
    if (!is_active_) return;
    quit_hovered_ = (x >= QUIT_BTN_X && x < (QUIT_BTN_X + QUIT_BTN_W) &&
                     y >= QUIT_BTN_Y && y < (QUIT_BTN_Y + QUIT_BTN_H));
}

void ScorecardModal::render(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (!is_active_) return;

    // 1. Authentic re_screen composite dialog (149 frame elements from Table 4 Animation 25)
    // Rendered in reverse order to produce authentic 640x480 terracotta layout with frames,
    // banners, headers, boxes, and column arrows.
    const auto* anim = assets.find_animation("re_screen");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, fr.dx, fr.dy);
        }
    } else {
        renderer.fill_rect(0, 0, 640, 480, assets::ColorRGBA{219, 75, 19, 255});
    }

    // 2. Top-Right "Leave Game" button at (525, 12)
    const char* leave_spr = quit_pressed_ ? "bleave3.bmp" : (quit_hovered_ ? "bleave2.bmp" : "bleave1.bmp");
    renderer.draw_named_sprite(leave_spr, QUIT_BTN_X, QUIT_BTN_Y);

    // 3. Winner Row (Inside Winner Box at y=222..275)
    int32_t wy = 243;
    // Tinted ant portrait: agst301.bmp at (54, 227) (vertically centered in 50px tall box at y=222..272)
    renderer.set_hud_team(winner_entry_.player_id);
    renderer.draw_named_sprite("agst301.bmp", 54, 227);
    renderer.set_hud_team(0);

    // Winner Name
    renderer.draw_text(winner_entry_.name, 90, wy, {255, 255, 255, 255});

    // 4 Columns aligned with column arrow tips
    auto draw_centered_num = [&](int32_t val, int32_t col_x) {
        std::string s = std::to_string(val);
        int32_t tx = col_x - renderer.get_text_width(s) / 2;
        renderer.draw_text(s, tx, wy, {255, 255, 255, 255});
    };

    draw_centered_num(winner_entry_.score, COL_SCORE_X);
    draw_centered_num(static_cast<int32_t>(winner_entry_.friendly_lost), COL_LOST_X);
    draw_centered_num(static_cast<int32_t>(winner_entry_.enemy_killed), COL_KILLED_X);
    draw_centered_num(static_cast<int32_t>(winner_entry_.new_hatched), COL_HATCHED_X);

    // 4. Other Players Section (Inside other box at y=310..463)
    // Only rendered if other human players are present (multiplayer)
    if (!other_entries_.empty() && !local_player_name_.empty()) {
        int32_t py = 320;
        for (const auto& pe : other_entries_) {
            if (pe.score == 0 && pe.friendly_lost == 0 && pe.enemy_killed == 0 && pe.new_hatched == 0) {
                continue;
            }
            renderer.set_hud_team(pe.player_id);
            renderer.draw_named_sprite("agst301.bmp", 54, py - 4);
            renderer.set_hud_team(0);
            renderer.draw_text(pe.name, 90, py + 4, {220, 220, 220, 255});

            std::string ps_score = std::to_string(pe.score);
            renderer.draw_text(ps_score, COL_SCORE_X - renderer.get_text_width(ps_score) / 2, py + 4, {220, 220, 220, 255});
            std::string ps_lost = std::to_string(pe.friendly_lost);
            renderer.draw_text(ps_lost, COL_LOST_X - renderer.get_text_width(ps_lost) / 2, py + 4, {200, 200, 200, 255});
            std::string ps_killed = std::to_string(pe.enemy_killed);
            renderer.draw_text(ps_killed, COL_KILLED_X - renderer.get_text_width(ps_killed) / 2, py + 4, {200, 200, 200, 255});
            std::string ps_hatched = std::to_string(pe.new_hatched);
            renderer.draw_text(ps_hatched, COL_HATCHED_X - renderer.get_text_width(ps_hatched) / 2, py + 4, {200, 200, 200, 255});

            py += 35;
        }
    }
}

} // namespace ants::app

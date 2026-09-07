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

constexpr assets::ColorRGBA TEAM_COLORS[4] = {
    {83, 147, 43, 255},   // 0: Green
    {251, 51, 91, 255},   // 1: Red
    {119, 175, 239, 255}, // 2: Blue
    {79, 87, 111, 255}    // 3: Black
};

} // anonymous namespace

ScorecardModal::ScorecardModal() = default;

void ScorecardModal::show(const sim::MatchResult& result, uint8_t local_player_id) {
    is_active_ = true;
    local_player_id_ = local_player_id;
    ok_pressed_ = false;
    quit_pressed_ = false;

    // Check if local player won
    bool local_won = result.is_winner(local_player_id);
    audio_to_play_ = local_won ? sim::SoundID::VictoryFanfare : sim::SoundID::PlayerDefeat;

    // Find winner entry
    uint8_t winner_id = result.winning_players.empty() ? 0 : result.winning_players[0];
    winner_entry_.player_id = winner_id;
    winner_entry_.name = std::string(PLAYER_NAMES[winner_id % 4]) + " (Winner)";
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

    // Test OK button
    if (x >= OK_BTN_X && x < (OK_BTN_X + OK_BTN_W) &&
        y >= OK_BTN_Y && y < (OK_BTN_Y + OK_BTN_H)) {
        ok_pressed_ = true;
        return true;
    }

    // Test Quit button
    if (x >= QUIT_BTN_X && x < (QUIT_BTN_X + QUIT_BTN_W) &&
        y >= QUIT_BTN_Y && y < (QUIT_BTN_Y + QUIT_BTN_H)) {
        quit_pressed_ = true;
        return true;
    }

    return true; // Modal consumes all click events
}

bool ScorecardModal::handle_mouse_up(int32_t x, int32_t y) {
    if (!is_active_) return false;

    if (ok_pressed_) {
        ok_pressed_ = false;
        if (x >= OK_BTN_X && x < (OK_BTN_X + OK_BTN_W) &&
            y >= OK_BTN_Y && y < (OK_BTN_Y + OK_BTN_H)) {
            if (on_replay_) on_replay_();
            return true;
        }
    }

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

void ScorecardModal::render(IRenderer& renderer, const assets::AssetArchive&) {
    if (!is_active_) return;

    // 1. Full-screen tiled clay backdrop: dclay96.bmp (96x96)
    for (int32_t y = 0; y < 480; y += 96) {
        for (int32_t x = 0; x < 640; x += 96) {
            renderer.draw_named_sprite("dclay96.bmp", x, y);
        }
    }

    // 2. Beveled Outer Frame Border Trim
    for (int32_t x = 0; x < 640; x += 96) {
        renderer.draw_named_sprite("dfram296.bmp", x, 0);   // top
        renderer.draw_named_sprite("dfram796.bmp", x, 464); // bottom
    }
    for (int32_t y = 0; y < 480; y += 96) {
        renderer.draw_named_sprite("dfram496.bmp", 0, y);   // left
        renderer.draw_named_sprite("dfram596.bmp", 624, y); // right
    }

    // 3. Top Banner Header: resbanr.bmp (340x34) at (140, 0)
    renderer.draw_named_sprite("resbanr.bmp", BANNER_X, BANNER_Y);

    // 4. Title Art: yoscore.bmp (302x127) at (41, 55)
    renderer.draw_named_sprite("yoscore.bmp", TITLE_X, TITLE_Y);

    // 5. Stats Header: newstats.bmp (259x133) at (342, 84)
    renderer.draw_named_sprite("newstats.bmp", STATS_X, STATS_Y);

    // 6. Winner Section
    renderer.draw_named_sprite("winnr.bmp", WINNER_HDR_X, WINNER_HDR_Y); // (117x19)

    // Winner Box: bg50x100.bmp tiled across 558x50 at (40, 222)
    for (int32_t bx = WINNER_BOX_X; bx < (WINNER_BOX_X + WINNER_BOX_W); bx += 100) {
        renderer.draw_named_sprite("bg50x100.bmp", bx, WINNER_BOX_Y);
    }

    // Winner Row Data
    int32_t wy = WINNER_BOX_Y + 16;
    renderer.fill_rect(WINNER_BOX_X + 16, wy + 2, 10, 10, TEAM_COLORS[winner_entry_.player_id % 4]);
    renderer.draw_text(winner_entry_.name, WINNER_BOX_X + 34, wy + 2, {255, 255, 255, 255});

    // 4 Columns aligned with newstats.bmp arrow tips
    renderer.draw_text(std::to_string(winner_entry_.score), COL_SCORE_X, wy, {255, 215, 0, 255});
    renderer.draw_text(std::to_string(winner_entry_.friendly_lost), COL_LOST_X, wy, {255, 255, 255, 255});
    renderer.draw_text(std::to_string(winner_entry_.enemy_killed), COL_KILLED_X, wy, {255, 255, 255, 255});
    renderer.draw_text(std::to_string(winner_entry_.new_hatched), COL_HATCHED_X, wy, {255, 255, 255, 255});

    // 7. Other Players Section
    renderer.draw_named_sprite("otherp.bmp", OTHER_HDR_X, OTHER_HDR_Y); // (203x25)

    // Other Players Box: efrbg100.bmp tiled across 558x130 at (40, 310)
    for (int32_t oy = OTHER_BOX_Y; oy < (OTHER_BOX_Y + OTHER_BOX_H); oy += 100) {
        for (int32_t ox = OTHER_BOX_X; ox < (OTHER_BOX_X + OTHER_BOX_W); ox += 100) {
            renderer.draw_named_sprite("efrbg100.bmp", ox, oy);
        }
    }

    // Other Player Rows
    int32_t py = OTHER_BOX_Y + 12;
    for (const auto& pe : other_entries_) {
        renderer.fill_rect(OTHER_BOX_X + 16, py + 2, 8, 8, TEAM_COLORS[pe.player_id % 4]);
        renderer.draw_text(pe.name, OTHER_BOX_X + 34, py + 2, {220, 220, 220, 255});

        renderer.draw_text(std::to_string(pe.score), COL_SCORE_X, py, {255, 255, 255, 255});
        renderer.draw_text(std::to_string(pe.friendly_lost), COL_LOST_X, py, {200, 200, 200, 255});
        renderer.draw_text(std::to_string(pe.enemy_killed), COL_KILLED_X, py, {200, 200, 200, 255});
        renderer.draw_text(std::to_string(pe.new_hatched), COL_HATCHED_X, py, {200, 200, 200, 255});

        py += 35;
    }

    // 8. Action Buttons
    // OK Button: dbutoku.bmp (46x20)
    renderer.draw_named_sprite("dbutoku.bmp", OK_BTN_X, OK_BTN_Y);

    // Quit / Return Button: breturn1.bmp (98x26)
    renderer.draw_named_sprite("breturn1.bmp", QUIT_BTN_X, QUIT_BTN_Y);
}

} // namespace ants::app

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <functional>

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

/**
 * @brief Results Scorecard Modal dialog (re_screen / Animation 25).
 * Displayed upon match conclusion (clock 0:00) with simulation freeze.
 */
class ScorecardModal {
public:
    // Layout coordinates from reverse engineered Animation 25
    static constexpr int32_t BANNER_X          = 140;
    static constexpr int32_t BANNER_Y          = 0;
    static constexpr int32_t TITLE_X           = 41;
    static constexpr int32_t TITLE_Y           = 55;
    static constexpr int32_t STATS_X           = 342;
    static constexpr int32_t STATS_Y           = 84;
    static constexpr int32_t WINNER_HDR_X      = 40;
    static constexpr int32_t WINNER_HDR_Y      = 195;
    static constexpr int32_t WINNER_BOX_X      = 40;
    static constexpr int32_t WINNER_BOX_Y      = 222;
    static constexpr int32_t WINNER_BOX_W      = 558;
    static constexpr int32_t WINNER_BOX_H      = 50;
    static constexpr int32_t OTHER_HDR_X       = 40;
    static constexpr int32_t OTHER_HDR_Y       = 280;
    static constexpr int32_t OTHER_BOX_X       = 40;
    static constexpr int32_t OTHER_BOX_Y       = 310;
    static constexpr int32_t OTHER_BOX_W       = 558;
    static constexpr int32_t OTHER_BOX_H       = 130;

    // 4 Statistic column X coordinates (aligned with newstats.bmp arrow tips)
    static constexpr int32_t COL_SCORE_X       = 496;
    static constexpr int32_t COL_LOST_X        = 536;
    static constexpr int32_t COL_KILLED_X      = 557;
    static constexpr int32_t COL_HATCHED_X     = 578;

    // Interactive button positions
    static constexpr int32_t OK_BTN_X          = 530;
    static constexpr int32_t OK_BTN_Y          = 448;
    static constexpr int32_t OK_BTN_W          = 46;
    static constexpr int32_t OK_BTN_H          = 20;

    static constexpr int32_t QUIT_BTN_X        = 420;
    static constexpr int32_t QUIT_BTN_Y        = 448;
    static constexpr int32_t QUIT_BTN_W        = 98;
    static constexpr int32_t QUIT_BTN_H        = 26;

    ScorecardModal();
    ~ScorecardModal() = default;

    void show(const sim::MatchResult& result, uint8_t local_player_id);
    void hide() noexcept { is_active_ = false; }
    bool is_open() const noexcept { return is_active_; }

    bool handle_mouse_down(int32_t x, int32_t y);
    bool handle_mouse_up(int32_t x, int32_t y);

    void render(IRenderer& renderer, const assets::AssetArchive& assets);

    // Audio routing query
    uint32_t get_audio_to_play() const noexcept { return audio_to_play_; }
    void clear_audio_to_play() noexcept { audio_to_play_ = 0; }

    // Action callbacks
    void set_on_replay(std::function<void()> cb) { on_replay_ = std::move(cb); }
    void set_on_quit(std::function<void()> cb) { on_quit_ = std::move(cb); }

private:
    struct PlayerEntry {
        uint8_t player_id{0};
        std::string name;
        int32_t score{0};
        uint32_t friendly_lost{0};
        uint32_t enemy_killed{0};
        uint32_t new_hatched{0};
        bool is_winner{false};
        bool is_allied{false};
    };

    bool is_active_{false};
    uint8_t local_player_id_{0};
    uint32_t audio_to_play_{0}; // Sound 56 (winner) vs Sound 41 (loser)
    bool ok_pressed_{false};
    bool quit_pressed_{false};

    PlayerEntry winner_entry_{};
    std::vector<PlayerEntry> other_entries_{};

    std::function<void()> on_replay_;
    std::function<void()> on_quit_;
};

} // namespace ants::app

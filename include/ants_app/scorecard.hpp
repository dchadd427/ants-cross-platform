#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <functional>

#include "ants_app/hud_art.hpp"
#include "ants_app/scorecard_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_button.hpp"

namespace ants::app {

/**
 * @brief The results screen of the original (Ants.exe: constructor FUN_010153a1, rows and layout FUN_010155ac, row builder FUN_01015136, keys FUN_01015b17,
 * Leave FUN_01015b47; docs 5.49).
 *
 * The screen is created when the game-over message arrives and shows the `re_screen` composite. For at least 250 ms it shows "Waiting for scores..." (the original
 * waits for the score messages of the other machines; the shared simulation has them all at once, so only the minimum counts). Then the rows are built: one row per
 * team or alliance (MatchResult::rows: "A & B" with the columns added up, the quitter last, the local team first on equal scores), each with a name label, four
 * number labels and the animated ant portrait of its team (two portraits for an alliance). The winner or loser cue plays once, at that moment, and the Leave button
 * exists from then on. Enter, C, Q and X leave at any time (Application); every other key does nothing.
 *
 * In a 960 x 540 canvas the page is recomposed for the whole picture (results_layout.hpp): the layout is `ResultsLayout::of(wide_layout())`, and the Leave button, the rows and the label of the
 * waiting phase stand where it puts them. The four numbers of a row are the original's single-line labels (left aligned, a surface that clips what is wider), drawn in the original's 8 px digits.
 *
 * With the HUD art New (and a picture of at least 360 units on its shorter side) the page is the NEW one instead: the clay panel over the whole canvas, the rows in wells, laid out in units of whole device
 * pixels by scorecard_layout.hpp for any size and shape of the canvas (render_new, scorecard_new.cpp); the same rows, headings, headline and Leave button, the same keys. Classic art is the page above, untouched.
 *
 * The game mode 187 has its own page (results_layout.hpp, docs/GAMEPLAY.md "187"): the baked column headings are not drawn, three headings of the game's own letters stand instead, a row has three
 * numbers (the kills, which are the score; the ants lost; the ants that are left) and the headline says who won and how: "<Name> is the last colony standing!" (two allied teams that are both left:
 * "<A> and <B> are the last colonies standing!"), "Time is up. The most kills wins!", or "Nobody is left. The most kills wins!". A match that a player's quit ended has no headline (its row is last, as ever).
 */
class ScorecardModal {
public:
    // The layout (FUN_010155ac): the top row's labels start at Y = 235, the others at 50 i + 273; every label is 50 px high with 18 px lines.
    static constexpr double WAIT_MS            = 250.0;    // the wait task's delay (FUN_01031e92(task, 250, 0))
    static constexpr int32_t FIRST_ROW_Y       = 235;
    static constexpr int32_t OTHER_ROWS_Y      = 273;
    static constexpr int32_t ROW_PITCH         = 50;
    static constexpr int32_t LABEL_H           = 50;
    static constexpr int32_t NAME_X            = 100;
    static constexpr int32_t NAME_W            = 385;
    static constexpr size_t  NAME_MAX_CHARS    = 35;
    static constexpr int32_t COLUMN_X[4]       = {485, 534, 555, 576};      // score, friendly lost, enemy killed, new hatched: left aligned
    static constexpr int32_t COLUMN_W[4]       = {49, 19, 21, 20};
    static constexpr int32_t PORTRAIT_Y_OFFSET = 20;                        // the ant sprites are placed at (x, Y + 20)
    static constexpr int32_t PORTRAIT_X_ALONE  = 60;
    static constexpr int32_t PORTRAIT_X_PAIR[2] = {45, 75};
    static constexpr int32_t WAITING_X         = 100;                       // "Waiting for scores..." (string 111)
    static constexpr int32_t WAITING_Y         = 350;
    static constexpr int32_t WAITING_W         = 385;

    // Leave button: animations leave1 / leave2 (525, 12) 99x22 resting, leave3 (524, 14) 98x20 pressed (the hit test is the rectangle of the picture that shows)
    static constexpr int32_t QUIT_BTN_X        = 525;
    static constexpr int32_t QUIT_BTN_Y        = 12;
    static constexpr int32_t QUIT_BTN_W        = 99;
    static constexpr int32_t QUIT_BTN_H        = 22;
    static constexpr ButtonRect QUIT_BTN_PRESSED{524, 14, 98, 20};

    /// One row as shown
    struct Row {
        uint8_t first{0};                          // the team that made the row (the lower one of an alliance)
        uint8_t second{sim::PLAYER_NEUTRAL};       // its ally, PLAYER_NEUTRAL for a single team
        std::string name;                          // "A" or "A & B", at most 35 characters
        std::array<std::string, 4> numbers;        // score, friendly lost, enemy killed, new hatched (187: kills, ants lost, ants left, and nothing)
        bool has_second() const noexcept { return second != sim::PLAYER_NEUTRAL; }
    };

    ScorecardModal();
    ~ScorecardModal() = default;

    void set_local_player_name(std::string name) { local_player_name_ = std::move(name); }
    /// The names of the four teams for the rows (an empty name shows the color word)
    void set_player_names(const std::array<std::string, 4>& names) { player_names_ = names; }
    /// The teams that get a row (bit p = team p): a browser game has no other players, so its idle teams are not listed (default: all)
    void set_shown_teams(uint8_t mask) noexcept { shown_mask_ = static_cast<uint8_t>(mask & 0x0Fu); }
    /// Opens the screen in its waiting phase. `mode` is the rules of the match that ended (the 187 page, or the original's)
    void show(const sim::MatchResult& result, uint8_t local_player_id, sim::GameMode mode = sim::GameMode::HighestScore);
    void hide() noexcept { is_active_ = false; }
    bool is_open() const noexcept { return is_active_; }
    /// The screen's clock: after 250 ms the rows are built and the cue is chosen; the portraits animate with it
    void update(float dt_seconds);
    bool is_waiting() const noexcept { return is_active_ && phase_ == Phase::Waiting; }
    const std::vector<Row>& rows() const noexcept { return rows_; }
    sim::GameMode game_mode() const noexcept { return mode_; }
    /// The 187 page's headline (empty before the rows are built, in the other modes, and for a match that a quit ended)
    const std::string& headline() const noexcept { return headline_; }

    /// The screen as the 16:9 page (the whole 960 x 540 picture, results_layout.hpp) or the original's 640 x 480 page; the Leave button's rectangles move with it
    void set_wide_layout(bool wide);
    bool wide_layout() const noexcept { return wide_; }

    /// The HUD art New (null: Classic, the page above): the new page needs the art to have every piece of it (window_art::has_results_page) and a canvas of at least 360 units on its shorter side
    void set_art(const hud_art::Art* art);
    /// The canvas that the page is drawn on, in canvas pixels, and how many device pixels one of them is (IRenderer::output_scale): the new page's layout follows both
    void set_screen(int32_t canvas_w, int32_t canvas_h, double output_scale);
    /// The renderer that the new page measures its text with (the 187 headline is wrapped into one or two lines, and the layout makes room for them when the rows are built, not when they are drawn). Null:
    /// the headline is one line
    void set_text_source(const IRenderer* renderer);
    /// The new page is the one that shows: its picture is the whole canvas (Application::picture_for_state)
    bool new_look() const noexcept { return new_look_; }
    const scorecard_layout::Scale& scale() const noexcept { return scale_; }
    const scorecard_layout::Layout& new_layout() const noexcept { return layout_; }

    /// The Leave button only exists once the rows do
    bool handle_mouse_down(int32_t x, int32_t y);
    bool handle_mouse_up(int32_t x, int32_t y);
    void handle_mouse_motion(int32_t x, int32_t y);
    /// FUN_01015b47: leave (the keys Enter, C, Q and X do it, at any time)
    void leave() { if (on_quit_) on_quit_(); }

    void render(IRenderer& renderer, const assets::AssetArchive& assets);

    // The cue of the screen (0 = none pending): set when the rows are built, taken by the application
    uint32_t get_audio_to_play() const noexcept { return audio_to_play_; }
    void clear_audio_to_play() noexcept { audio_to_play_ = 0; }

    // Action callbacks
    void set_on_replay(std::function<void()> cb) { on_replay_ = std::move(cb); }
    void set_on_quit(std::function<void()> cb) { on_quit_ = std::move(cb); }
    void set_on_play_sfx(std::function<void(uint32_t)> cb) { on_play_sfx_ = std::move(cb); }
    void play_sfx(uint32_t sound_id) { if (on_play_sfx_) on_play_sfx_(sound_id); }

    bool is_quit_hovered() const noexcept { return quit_.hovered(); }
    bool is_quit_pressed() const noexcept { return quit_.pressed(); }

private:
    enum class Phase : uint8_t { Waiting, Rows };

    void build_rows();
    void update_new_look();                  // the new page's scale, layout and Leave button from the art, the canvas and the rows
    void update_button();                    // the Leave button's rectangles: the new page's or the page's own
    void render_new(IRenderer& renderer, const assets::AssetArchive& assets);
    std::vector<std::string> wrap_headline(int32_t max_w_units) const;      // the 187 headline in one or two lines of the new page (scorecard_new.cpp)
    std::string headline_187() const;
    std::string name_of(uint8_t team) const;
    void draw_portrait(IRenderer& renderer, const assets::AssetArchive& assets, uint8_t team, int32_t x, int32_t y) const;

    std::string local_player_name_{};
    std::array<std::string, 4> player_names_{};
    uint8_t shown_mask_{0x0F};

    const hud_art::Art* art_{nullptr};    // the HUD art that the new page is drawn from (null: Classic)
    uint64_t art_generation_{0};          // ... and the manifest that was read when it was given (a new reading checks the pieces again)
    int32_t canvas_w_{0};                 // the canvas and its output scale, from the application (set_screen)
    int32_t canvas_h_{0};
    double output_{1.0};
    bool new_look_{false};                // the new page shows (art_ has the pieces, the canvas is big enough)
    scorecard_layout::Scale scale_{};
    scorecard_layout::Layout layout_{};
    const IRenderer* text_source_{nullptr};          // what the new page's text is measured with (set_text_source)
    std::vector<std::string> headline_text_{};       // the 187 headline's lines on the new page, as they were wrapped when the layout was made: the frame draws these
    bool wide_{false};                    // the page is the 16:9 one
    bool is_active_{false};
    Phase phase_{Phase::Waiting};
    double elapsed_ms_{0.0};              // the screen's own clock since it was created
    uint8_t local_player_id_{0};
    sim::GameMode mode_{sim::GameMode::HighestScore};    // the rules of the match that ended
    std::string headline_{};              // 187: the line at the top of the page, made with the rows
    sim::MatchResult result_{};           // what the screen was created with, reduced to the shown teams
    std::vector<Row> rows_{};
    uint32_t audio_to_play_{0};           // Sound 56 (winner) vs Sound 42 (loser), set when the rows are built
    ScreenButton quit_{ButtonRect{QUIT_BTN_X, QUIT_BTN_Y, QUIT_BTN_W, QUIT_BTN_H}, QUIT_BTN_PRESSED};      // exists once the rows are there

    std::function<void()> on_replay_;
    std::function<void()> on_quit_;
    std::function<void(uint32_t)> on_play_sfx_{nullptr};
};

} // namespace ants::app

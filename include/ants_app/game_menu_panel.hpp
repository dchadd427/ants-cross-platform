#pragma once

// The settings menu of the desktop game (docs/GAME_MENU.md): a panel that the game draws over the match, from the ONE table of rows that the browser's page draws its own panel from (game_menu.hpp). Esc with
// nothing to cancel, Ctrl+O and the HUD's Options button open it; Resume, Esc and a click on the dimmed picture close it. It has the same sections (Sound, Video, Controls, Game, Quick Chat) and the same
// rows, with the game's own window pieces (the clay panel, the wells, the steel buttons, the On / Off switches and the slider's thumb of the HUD art) in units of whole device pixels, as the countdown
// card and the results screen are. The panel only asks (GameMenuHost): the values live where they always did, and nothing here reaches the simulation, the network or a recording. It has no window and no SDL,
// so that a test can run it with a renderer that records what is drawn.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_app/game_menu.hpp"
#include "ants_app/hud_art.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// What the panel asks of the game (the Application implements it): the value of a row or a fact (game_menu_get), a change, the text of a Quick Chat row, an act, and whether the window is full screen
class GameMenuHost {
public:
    virtual ~GameMenuHost() = default;
    virtual int get(int what) = 0;                                     // a row's value or a game_menu::Fact; -1 for what is not the game's
    virtual bool set(int id, int value, bool save) = 0;                // a change of a row; `save` false: heard now, written at the release (a slider that is dragged)
    virtual std::string text(int id) = 0;                              // the text of a Text row
    virtual void set_text(int id, const std::string& text) = 0;        // ... changed (printable ASCII, at most game_menu::kQuickChatMax characters)
    virtual bool act(int action) = 0;                                  // a game_menu::Action: Leave game, the quick help, the window's fullscreen
    virtual bool fullscreen() = 0;                                     // the window is full screen now
    virtual std::string version() = 0;                                 // the game's version, for the Version row
};

/// The panel's size and place: a unit is `f` canvas pixels, and the HUD art's sheet (1, 2 or 3) that draws a unit exactly. The panel is 720 x 484 units in the middle of the picture; a unit is the
/// largest whole number of device pixels that lets it fit, at least one. A picture too small for one device pixel to the unit shrinks the panel to it.
struct GameMenuScale {
    double f{1.0};
    int32_t sheet{1};
    int32_t x{0};            // the panel's top left corner in the picture
    int32_t y{0};
    int32_t px(int32_t unit) const noexcept;              // a unit coordinate across, as a canvas pixel (the panel's corner added)
    int32_t py(int32_t unit) const noexcept;
    LayoutRect rect(int32_t ux, int32_t uy, int32_t uw, int32_t uh) const noexcept;       // neighbors share an edge
};
inline constexpr int32_t kMenuPanelW = 720;
inline constexpr int32_t kMenuPanelH = 484;
GameMenuScale game_menu_scale(int32_t picture_w, int32_t picture_h, double output) noexcept;

class GameMenuPanel {
public:
    /// What the panel draws and tests: one place of the panel's picture that does something
    enum class Part : uint8_t { Tab, Word, Slider, Button, Edit, Resume, Leave, Stay, Go };
    struct Item {
        Part part{Part::Tab};
        LayoutRect rect;                  // canvas pixels (the track for a slider)
        int row{-1};                      // the row's id (game_menu::RowId) or the section's number for a tab
        int word{0};                      // a Word's index in the row's words
    };

    explicit GameMenuPanel(GameMenuHost& host) : host_(host) {}

    /// The panel opens on its first section; `watching`: a recording (no Game and Quick Chat sections, no seat). It closes itself with `on_close`
    void open(bool watching);
    void close();
    bool is_open() const noexcept { return open_; }
    void set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

    /// The picture that the panel lies in and how many device pixels a canvas pixel is (every call of render and every pointer event uses it)
    void set_picture(int32_t w, int32_t h, double output);

    // The input, in the picture's pixels. Every one returns true: the panel is modal and takes everything
    bool on_mouse_down(int32_t x, int32_t y, uint8_t button);
    bool on_mouse_up(int32_t x, int32_t y, uint8_t button);
    bool on_mouse_move(int32_t x, int32_t y);
    bool on_wheel(int32_t dy);                                          // a notch up is positive
    bool on_key(int32_t key, bool repeat);                              // an SDL key code: Esc, Enter, Backspace, Tab, Up, Down
    bool on_text(const std::string& text);                              // typed characters: the Quick Chat field that has the focus takes the printable ASCII ones
    void cancel_press();                                                // the window lost the pointer: a slider's drag ends where it is, nothing is pressed

    /// Draws the panel (nothing when it is closed): the dim, the panel, the section's rows, the footer and, when it is up, the question of Leave game. `art` may be null (plain rectangles)
    void render(IRenderer& renderer, const hud_art::Art* art, uint32_t now_ms);

    // What a test reads
    int tab() const noexcept { return tab_; }
    int scroll() const noexcept { return scroll_; }
    bool asking() const noexcept { return asking_; }
    int focused_text() const noexcept { return focus_; }
    std::vector<Item> items();                                          // the places that do something, as the last layout put them
    const GameMenuScale& scale() const noexcept { return scale_; }
    LayoutRect panel_rect() const noexcept { return scale_.rect(0, 0, kMenuPanelW, kMenuPanelH); }

private:
    void build_sections();
    void select_tab(int index);
    /// Walks the panel once: collects the places that do something (`items`) and, when there is a renderer, draws them. One walk for both, so that what is drawn is what is clicked
    void paint(IRenderer* renderer, const hud_art::Art* art, std::vector<Item>* items);
    void clamp_scroll();
    int item_at(const std::vector<Item>& items, int32_t x, int32_t y) const;
    void fire(const Item& item);
    void slide_to(const Item& item, int32_t x, bool save);
    void ask_leave();
    void unfocus();

    GameMenuHost& host_;
    std::function<void()> on_close_;
    bool open_{false};
    bool watching_{false};
    std::vector<game_menu::SectionInfo> sections_;
    int tab_{0};
    int scroll_{0};                        // units
    int focus_{-1};                        // a Text row's id
    bool asking_{false};                   // the question of Leave game is up
    int ask_kind_{0};
    int32_t picture_w_{960};
    int32_t picture_h_{540};
    double output_{1.0};
    GameMenuScale scale_{};
    int32_t pointer_x_{-1000};
    int32_t pointer_y_{-1000};
    Item pressed_{};
    bool pressing_{false};
    bool dragging_{false};
    int32_t drag_value_{0};
    uint32_t now_ms_{0};
    int content_h_{0};                     // the section's rows, in units: what the last walk found
    const IRenderer* metrics_{nullptr};    // what measures the text when there is nothing to draw on (the last renderer)
    bool outside_press_{false};            // a press that began on the dim: a release on the dim closes the panel
    bool plain_{false};                    // no art: plain rectangles
};

}  // namespace ants::app

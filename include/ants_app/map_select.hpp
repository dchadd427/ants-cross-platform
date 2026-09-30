#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include "ants_assets/asset_archive.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_button.hpp"

namespace ants::app {

/// One entry of the map list: the file name and what the file's header says (the original reads the header when the entry is chosen)
struct MapSelectEntry {
    std::string filename;
    std::string full_path;
    std::string display_name;
    std::string description;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t minutes{0};
};

/**
 * @brief The setup screen of the original (Ants.exe: base class FUN_01012ce0, host screen FUN_01013b36, refresh FUN_010133ef, map list FUN_01013de7, selection
 * FUN_01013fc9, keys FUN_01014076, START FUN_010140c5; docs 5.50).
 *
 * The maps are every `*.lvl` of the Maps folder in the byte order of their file names. All labels, the portraits and the thumbs appear with the
 * first run of the refresh task, 500 ms after the screen was created. The buttons are the original's button class (ScreenButton: the callback runs at the release);
 * the keys are Up / Down (previous / next map, wrapping), Enter, S and s (START), Q, q, X and x (Leave); nothing else does anything. START locks the screen.
 */
class MapSelectScreen {
public:
    static constexpr double REFRESH_MS = 500.0;    // the refresh task's delay (FUN_01031e92(task, 500, 0)): until then the labels are empty and nobody is listed

    // The labels of the screen (FUN_01012ce0): the map name (36, 312) 179 x 26, its description (36, 380) 293 x 26, the prompt (36, 447) 293 x 35 with 14 px lines,
    // the players' names (415, 95 + 50 i) 120 x 20; every label has 18 px lines (the prompt 14) and the colour (239, 231, 223)
    static constexpr int32_t LABEL_X = 36;
    static constexpr int32_t NAME_Y = 312;
    static constexpr int32_t NAME_W = 179;
    static constexpr int32_t INFO_Y = 380;
    static constexpr int32_t STATUS_Y = 447;
    static constexpr int32_t STATUS_W = 293;
    static constexpr int32_t PLAYER_NAME_X = 415;
    static constexpr int32_t PLAYER_NAME_W = 120;

    // Control rectangles are the union of the resting and pressed art of the original animations (their part offsets
    // are absolute screen coordinates): up1/up3, down1/down3, start1/start3, leave1/leave3, d_on1/d_on3, d_off1/d_off3.
    static constexpr int32_t BTN_UP_X = 224;
    static constexpr int32_t BTN_UP_Y = 299;
    static constexpr int32_t BTN_UP_W = 48;
    static constexpr int32_t BTN_UP_H = 23;

    static constexpr int32_t BTN_DOWN_X = 225;
    static constexpr int32_t BTN_DOWN_Y = 323;
    static constexpr int32_t BTN_DOWN_W = 48;
    static constexpr int32_t BTN_DOWN_H = 22;

    static constexpr int32_t BTN_FOW_ON_X = 522;
    static constexpr int32_t BTN_FOW_ON_Y = 372;
    static constexpr int32_t BTN_FOW_ON_W = 49;
    static constexpr int32_t BTN_FOW_ON_H = 24;

    static constexpr int32_t BTN_FOW_OFF_X = 574;
    static constexpr int32_t BTN_FOW_OFF_Y = 372;
    static constexpr int32_t BTN_FOW_OFF_W = 49;
    static constexpr int32_t BTN_FOW_OFF_H = 24;


    static constexpr int32_t BTN_START_X = 526;
    static constexpr int32_t BTN_START_Y = 439;
    static constexpr int32_t BTN_START_W = 98;
    static constexpr int32_t BTN_START_H = 28;

    static constexpr int32_t BTN_QUIT_X = 524;
    static constexpr int32_t BTN_QUIT_Y = 12;
    static constexpr int32_t BTN_QUIT_W = 100;
    static constexpr int32_t BTN_QUIT_H = 22;

    // The player rows of the Players' Status box: slot i has its portrait origin at (395, 115 + 50 i) and its thumb at
    // (540, 95 + 50 i) (Ants.exe setup screen, SetPos of the AntSlot and thumb sprites)
    static constexpr int32_t PLAYER_PORTRAIT_X = 395;
    static constexpr int32_t PLAYER_PORTRAIT_Y = 115;
    static constexpr int32_t PLAYER_THUMB_X = 540;
    static constexpr int32_t PLAYER_THUMB_Y = 95;
    static constexpr int32_t PLAYER_ROW_PITCH = 50;


    MapSelectScreen();
    ~MapSelectScreen() = default;

    /// Reads the map list: every `*.lvl` of `maps_dir`, sorted by the byte order of the file names (no map is named in the program; an unreadable folder gives an empty list)
    void init(const std::string& maps_dir = "Original-Ants/Maps");
    /// The screen is created (again): the labels are empty until the refresh, nothing is locked, no button is pressed
    void enter();
    /// The screen's clock: 500 ms after `enter` the refresh shows the labels, the portraits and the thumbs
    void update(float dt_seconds);
    bool refreshed() const noexcept { return elapsed_ms_ >= REFRESH_MS; }
    /// START was accepted: up / down, the fog option and a second START do nothing any more (+0x130)
    void lock() noexcept { started_ = true; }
    bool is_locked() const noexcept { return started_; }

    /// Left button only. The buttons capture on the press (their pressed picture and click sound) and act at the release (docs 5.45)
    void handle_mouse_down(int32_t screen_x, int32_t screen_y, uint8_t button);
    void handle_mouse_up(int32_t screen_x, int32_t screen_y, uint8_t button);
    void handle_mouse_motion(int32_t screen_x, int32_t screen_y);
    /// FUN_01014076: Up / Down step the map list (wrapping), Enter, S and s start, Q, q, X and x leave, every other key does nothing (Esc included)
    void handle_key_down(SDL_Keycode key);

    void render(IRenderer& renderer, const ants::assets::AssetArchive& archive);

    void set_on_start(std::function<void(const std::string& map_path)> cb) {
        on_start_ = std::move(cb);
    }
    void set_on_quit(std::function<void()> cb) {
        on_quit_ = std::move(cb);
    }
    void set_on_play_sfx(std::function<void(uint32_t)> cb) {
        on_play_sfx_ = std::move(cb);
    }
    void play_sfx(uint32_t sound_id) {
        if (on_play_sfx_) on_play_sfx_(sound_id);
    }

    int32_t get_selected_index() const noexcept { return selected_index_; }
    void set_selected_index(int32_t idx) noexcept;

    const std::string& get_selected_map_path() const;
    const std::vector<MapSelectEntry>& get_maps() const noexcept { return maps_; }

    bool is_fog_of_war_enabled() const noexcept { return fog_of_war_; }

    /// The thumb beside a player's name (the original's netgood / netok / netbad / netunk animations: connection quality)
    enum class Thumb : uint8_t { Good = 0, Ok = 1, Bad = 2, Unknown = 3 };
    /// One seat of the Players' Status box
    struct RoomSeat {
        bool occupied{false};
        std::string name;
        Thumb thumb{Thumb::Good};
    };
    /// What a network room shows: the seats, who this machine is and whether it may change the setup. `networked` false = the local setup screen.
    struct RoomView {
        bool networked{false};
        bool is_host{true};
        uint8_t my_seat{0};
        std::array<RoomSeat, 4> seats{};
        std::string status;                    // replaces the prompt line while networked
    };
    void set_room(const RoomView& room) { room_ = room; }
    const RoomView& room() const noexcept { return room_; }
    /// The map, the fog option and START belong to the host of a room; a guest's clicks on them do nothing
    bool can_change_setup() const noexcept { return !room_.networked || room_.is_host; }
    /// The host changed the map (its file name) or the fog option through the controls
    void set_on_map_changed(std::function<void(const std::string& filename)> cb) { on_map_changed_ = std::move(cb); }
    void set_on_fog_changed(std::function<void(bool)> cb) { on_fog_changed_ = std::move(cb); }
    /// A guest shows the host's choice (no callbacks fire); false when the map is not in the list
    bool follow_host_choice(const std::string& filename, bool fog);

    void set_player_name(std::string name) { player_name_ = std::move(name); }
    void set_player_team(uint8_t team) noexcept { player_team_ = team; }

private:
    void trigger_start();
    void trigger_quit();
    void step_map(int32_t delta);          // FUN_01013fc9
    void start();                          // FUN_010140c5 (the application decides whether everybody is ready)

    std::vector<MapSelectEntry> maps_;
    int32_t selected_index_{0};

    int32_t mouse_x_{0};
    int32_t mouse_y_{0};
    ScreenButton up_{BTN_UP_X, BTN_UP_Y, BTN_UP_W, BTN_UP_H};
    ScreenButton down_{BTN_DOWN_X, BTN_DOWN_Y, BTN_DOWN_W, BTN_DOWN_H};
    ScreenButton start_{BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H};
    ScreenButton quit_{BTN_QUIT_X, BTN_QUIT_Y, BTN_QUIT_W, BTN_QUIT_H};
    ScreenButton fow_on_{BTN_FOW_ON_X, BTN_FOW_ON_Y, BTN_FOW_ON_W, BTN_FOW_ON_H};
    ScreenButton fow_off_{BTN_FOW_OFF_X, BTN_FOW_OFF_Y, BTN_FOW_OFF_W, BTN_FOW_OFF_H};

    bool fog_of_war_{false};
    bool started_{false};                 // +0x130: START ran
    double elapsed_ms_{0.0};              // since the screen was created
    std::string player_name_{};
    uint8_t player_team_{0};

    RoomView room_{};
    std::function<void(const std::string& filename)> on_map_changed_{nullptr};
    std::function<void(bool)> on_fog_changed_{nullptr};
    void change_fog(bool on);
    std::function<void(const std::string& map_path)> on_start_{nullptr};
    std::function<void()> on_quit_{nullptr};
    std::function<void(uint32_t)> on_play_sfx_{nullptr};
};

} // namespace ants::app

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

namespace ants::app {

struct MapSelectEntry {
    std::string filename;
    std::string full_path;
    std::string display_name;
    std::string description;
    uint32_t width{60};
    uint32_t height{60};
    uint32_t minutes{15};
};

/**
 * @brief Map Selection Screen presented on initial application launch.
 * Plays INTRO.MID on a loop until a map is chosen and match starts.
 */
class MapSelectScreen {
public:


    static constexpr int32_t W_MAP_X = 27;
    static constexpr int32_t W_MAP_Y = 306;
    static constexpr int32_t W_MAP_W = 195;
    static constexpr int32_t W_MAP_H = 39;

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


    static constexpr int32_t INFO_BOX_X = 27;
    static constexpr int32_t INFO_BOX_Y = 376;
    static constexpr int32_t INFO_BOX_W = 304;
    static constexpr int32_t INFO_BOX_H = 36;


    static constexpr int32_t BTN_DROP_X = 576;
    static constexpr int32_t BTN_DROP_Y = 192;
    static constexpr int32_t BTN_DROP_W = 47;
    static constexpr int32_t BTN_DROP_H = 21;


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

    void init(const std::string& maps_dir = "Original-Ants/Maps");

    void handle_mouse_down(int32_t screen_x, int32_t screen_y, uint8_t button);
    void handle_mouse_up(int32_t screen_x, int32_t screen_y, uint8_t button);
    void handle_mouse_motion(int32_t screen_x, int32_t screen_y);
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

    bool is_player_ready(uint8_t player_idx) const noexcept {
        return (player_ready_mask_ & (1u << player_idx)) != 0;
    }
    void toggle_player_ready(uint8_t player_idx) noexcept {
        player_ready_mask_ ^= (1u << player_idx);
    }

    void set_player_name(std::string name) { player_name_ = std::move(name); }
    void set_player_team(uint8_t team) noexcept { player_team_ = team; }

private:
    void trigger_start();
    void trigger_quit();

    std::vector<MapSelectEntry> maps_;
    int32_t selected_index_{0};

    int32_t mouse_x_{0};
    int32_t mouse_y_{0};
    bool btn_start_hovered_{false};
    bool btn_quit_hovered_{false};
    bool btn_up_hovered_{false};
    bool btn_down_hovered_{false};
    bool btn_fow_on_hovered_{false};
    bool btn_fow_off_hovered_{false};

    bool btn_start_pressed_{false};
    bool btn_quit_pressed_{false};
    bool btn_up_pressed_{false};
    bool btn_down_pressed_{false};

    bool fog_of_war_{false};
    uint8_t player_ready_mask_{0b0011}; // Player 0 & 1 ready, Player 2 unready (matching reference)
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

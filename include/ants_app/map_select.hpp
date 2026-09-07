#pragma once

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
    uint32_t anthills_count{4};
    uint32_t minutes{15};
};

/**
 * @brief Map Selection Screen presented on initial application launch.
 * Plays INTRO.MID on a loop until a map is chosen and match starts.
 */
class MapSelectScreen {
public:
    static constexpr int32_t BANNER_X = 140;
    static constexpr int32_t BANNER_Y = 1;

    static constexpr int32_t LOGO_X = 42;
    static constexpr int32_t LOGO_Y = 87;

    static constexpr int32_t PICKMAP_X = 30;
    static constexpr int32_t PICKMAP_Y = 281;

    static constexpr int32_t W_MAP_X = 27;
    static constexpr int32_t W_MAP_Y = 306;
    static constexpr int32_t W_MAP_W = 195;
    static constexpr int32_t W_MAP_H = 39;

    static constexpr int32_t BTN_UP_X = 226;
    static constexpr int32_t BTN_UP_Y = 303;
    static constexpr int32_t BTN_UP_W = 46;
    static constexpr int32_t BTN_UP_H = 22;

    static constexpr int32_t BTN_DOWN_X = 226;
    static constexpr int32_t BTN_DOWN_Y = 327;
    static constexpr int32_t BTN_DOWN_W = 47;
    static constexpr int32_t BTN_DOWN_H = 20;

    static constexpr int32_t MAPINFO_X = 29;
    static constexpr int32_t MAPINFO_Y = 352;

    static constexpr int32_t INFO_BOX_X = 27;
    static constexpr int32_t INFO_BOX_Y = 376;
    static constexpr int32_t INFO_BOX_W = 304;
    static constexpr int32_t INFO_BOX_H = 36;

    static constexpr int32_t STATLINE_X = 25;
    static constexpr int32_t STATLINE_Y = 434;

    static constexpr int32_t PLAYSTAT_X = 368;
    static constexpr int32_t PLAYSTAT_Y = 56;

    static constexpr int32_t PLAYERS_BOX_X = 366;
    static constexpr int32_t PLAYERS_BOX_Y = 82;
    static constexpr int32_t PLAYERS_BOX_W = 204;
    static constexpr int32_t PLAYERS_BOX_H = 204;

    static constexpr int32_t BTN_DROP_X = 576;
    static constexpr int32_t BTN_DROP_Y = 192;
    static constexpr int32_t BTN_DROP_W = 47;
    static constexpr int32_t BTN_DROP_H = 21;

    static constexpr int32_t FOW_HEADER_X = 367;
    static constexpr int32_t FOW_HEADER_Y = 376;

    static constexpr int32_t BTN_FOW_ON_X = 520;
    static constexpr int32_t BTN_FOW_ON_Y = 376;
    static constexpr int32_t BTN_FOW_ON_W = 36;
    static constexpr int32_t BTN_FOW_ON_H = 20;

    static constexpr int32_t BTN_FOW_OFF_X = 572;
    static constexpr int32_t BTN_FOW_OFF_Y = 376;
    static constexpr int32_t BTN_FOW_OFF_W = 46;
    static constexpr int32_t BTN_FOW_OFF_H = 20;

    static constexpr int32_t FOW_TEXT1_X = 368;
    static constexpr int32_t FOW_TEXT1_Y = 400;

    static constexpr int32_t FOW_TEXT2_X = 367;
    static constexpr int32_t FOW_TEXT2_Y = 415;

    static constexpr int32_t BTN_START_X = 526;
    static constexpr int32_t BTN_START_Y = 442;
    static constexpr int32_t BTN_START_W = 98;
    static constexpr int32_t BTN_START_H = 27;

    static constexpr int32_t BTN_QUIT_X = 525;
    static constexpr int32_t BTN_QUIT_Y = 12;
    static constexpr int32_t BTN_QUIT_W = 99;
    static constexpr int32_t BTN_QUIT_H = 22;

    // Backward-compatibility aliases
    static constexpr int32_t CARD_X = W_MAP_X;
    static constexpr int32_t CARD_Y = W_MAP_Y;
    static constexpr int32_t CARD_W = W_MAP_W;
    static constexpr int32_t CARD_H = W_MAP_H;
    static constexpr int32_t CARD_SPACING = 0;

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

    int32_t get_selected_index() const noexcept { return selected_index_; }
    void set_selected_index(int32_t idx) noexcept;

    const std::string& get_selected_map_path() const;
    const std::vector<MapSelectEntry>& get_maps() const noexcept { return maps_; }

    bool is_fog_of_war_enabled() const noexcept { return fog_of_war_; }
    void set_fog_of_war_enabled(bool enabled) noexcept { fog_of_war_ = enabled; }

    bool is_player_ready(uint8_t player_idx) const noexcept {
        return (player_ready_mask_ & (1u << player_idx)) != 0;
    }
    void set_player_ready(uint8_t player_idx, bool ready) noexcept {
        if (ready) player_ready_mask_ |= (1u << player_idx);
        else player_ready_mask_ &= ~(1u << player_idx);
    }
    void toggle_player_ready(uint8_t player_idx) noexcept {
        player_ready_mask_ ^= (1u << player_idx);
    }

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
    bool btn_drop_hovered_{false};
    bool btn_fow_on_hovered_{false};
    bool btn_fow_off_hovered_{false};

    bool btn_start_pressed_{false};
    bool btn_quit_pressed_{false};
    bool btn_up_pressed_{false};
    bool btn_down_pressed_{false};
    bool btn_drop_pressed_{false};

    bool fog_of_war_{true};
    uint8_t player_ready_mask_{0b0011}; // Player 0 & 1 ready, Player 2 unready (matching reference)

    uint32_t connection_ticks_{0};
    std::function<void(const std::string& map_path)> on_start_{nullptr};
    std::function<void()> on_quit_{nullptr};
};

} // namespace ants::app

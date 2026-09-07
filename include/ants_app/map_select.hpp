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
};

/**
 * @brief Map Selection Screen presented on initial application launch.
 * Plays INTRO.MID on a loop until a map is chosen and match starts.
 */
class MapSelectScreen {
public:
    static constexpr int32_t CARD_X = 25;
    static constexpr int32_t CARD_Y = 70;
    static constexpr int32_t CARD_W = 410;
    static constexpr int32_t CARD_H = 48;
    static constexpr int32_t CARD_SPACING = 8;

    static constexpr int32_t PREVIEW_X = 450;
    static constexpr int32_t PREVIEW_Y = 70;
    static constexpr int32_t PREVIEW_W = 165;
    static constexpr int32_t PREVIEW_H = 270;

    static constexpr int32_t BTN_START_X = 450;
    static constexpr int32_t BTN_START_Y = 350;
    static constexpr int32_t BTN_START_W = 165;
    static constexpr int32_t BTN_START_H = 38;

    static constexpr int32_t BTN_QUIT_X = 450;
    static constexpr int32_t BTN_QUIT_Y = 398;
    static constexpr int32_t BTN_QUIT_W = 165;
    static constexpr int32_t BTN_QUIT_H = 34;

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

private:
    void trigger_start();
    void trigger_quit();

    std::vector<MapSelectEntry> maps_;
    int32_t selected_index_{0};
    int32_t hovered_index_{-1};

    int32_t mouse_x_{0};
    int32_t mouse_y_{0};
    bool btn_start_hovered_{false};
    bool btn_quit_hovered_{false};
    bool btn_start_pressed_{false};
    bool btn_quit_pressed_{false};

    uint32_t last_click_timestamp_{0};
    int32_t last_click_card_{-1};

    std::function<void(const std::string& map_path)> on_start_;
    std::function<void()> on_quit_;
};

} // namespace ants::app

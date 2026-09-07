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
    static constexpr int32_t BANNER_X = 149;
    static constexpr int32_t BANNER_Y = 12;

    static constexpr int32_t CARD_X = 45;
    static constexpr int32_t CARD_Y = 86;
    static constexpr int32_t CARD_W = 246;
    static constexpr int32_t CARD_H = 48;
    static constexpr int32_t CARD_SPACING = 6;

    static constexpr int32_t BTN_UP_X = 245;
    static constexpr int32_t BTN_UP_Y = 86;
    static constexpr int32_t BTN_UP_W = 46;
    static constexpr int32_t BTN_UP_H = 22;

    static constexpr int32_t BTN_DOWN_X = 245;
    static constexpr int32_t BTN_DOWN_Y = 110;
    static constexpr int32_t BTN_DOWN_W = 47;
    static constexpr int32_t BTN_DOWN_H = 20;

    static constexpr int32_t BTN_START_X = 190;
    static constexpr int32_t BTN_START_Y = 405;
    static constexpr int32_t BTN_START_W = 98;
    static constexpr int32_t BTN_START_H = 27;

    static constexpr int32_t BTN_QUIT_X = 350;
    static constexpr int32_t BTN_QUIT_Y = 407;
    static constexpr int32_t BTN_QUIT_W = 99;
    static constexpr int32_t BTN_QUIT_H = 24;

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

    int32_t mouse_x_{0};
    int32_t mouse_y_{0};
    bool btn_start_hovered_{false};
    bool btn_quit_hovered_{false};
    bool btn_up_hovered_{false};
    bool btn_down_hovered_{false};

    bool btn_start_pressed_{false};
    bool btn_quit_pressed_{false};
    bool btn_up_pressed_{false};
    bool btn_down_pressed_{false};

    uint32_t connection_ticks_{0};
    std::function<void(const std::string& map_path)> on_start_{nullptr};
    std::function<void()> on_quit_{nullptr};
};

} // namespace ants::app

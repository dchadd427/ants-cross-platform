// The desktop game's window icon (app_icon.hpp).
#include "ants_app/app_icon.hpp"

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include <cstddef>
#include <cstring>

namespace ants::app::app_icon {

namespace {

struct IconColor {
    char key;
    uint8_t r, g, b;
};
#include "app_icon_data.inc"

const IconColor* color_of(char key) {
    for (const IconColor& c : kIconColors) {
        if (c.key == key) return &c;
    }
    return nullptr;
}

}  // namespace

std::vector<uint8_t> rgba(int32_t zoom) {
    if (zoom < 1 || zoom > kMaxZoom) return {};
    const size_t side = static_cast<size_t>(kCells) * static_cast<size_t>(zoom);
    std::vector<uint8_t> out(side * side * 4, 0);
    for (size_t cy = 0; cy < static_cast<size_t>(kCells); ++cy) {
        for (size_t cx = 0; cx < static_cast<size_t>(kCells); ++cx) {
            const IconColor* color = color_of(kIconRows[cy][cx]);
            if (color == nullptr) continue;                          // '.': transparent
            const uint8_t cell[4] = {color->r, color->g, color->b, 255};
            for (size_t y = cy * static_cast<size_t>(zoom); y < (cy + 1) * static_cast<size_t>(zoom); ++y) {
                for (size_t x = cx * static_cast<size_t>(zoom); x < (cx + 1) * static_cast<size_t>(zoom); ++x) {
                    std::memcpy(&out[(y * side + x) * 4], cell, 4);
                }
            }
        }
    }
    return out;
}

int32_t window_zoom() {
#if defined(__APPLE__)
    return 16;
#else
    return 2;
#endif
}

bool set_window_icon(SDL_Window* window) {
    if (window == nullptr) return false;
    const int32_t zoom = window_zoom();
    std::vector<uint8_t> pixels = rgba(zoom);
    if (pixels.empty()) return false;
    const int32_t side = kCells * zoom;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), side, side, 32, side * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) return false;
    SDL_SetWindowIcon(window, surface);                              // (SDL keeps its own copy: the surface and the pixels are ours to free)
    SDL_FreeSurface(surface);
    return true;
}

}  // namespace ants::app::app_icon

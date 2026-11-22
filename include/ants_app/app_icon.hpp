#pragma once

// The desktop game's own icon: the original game's icon (the 32 x 32 picture of 16 colors in Ants.exe, transparent around the ant, as the original has it). The picture is src/ants_app/app_icon_data.inc,
// which tools/make_app_icons.py writes from the one grid that also draws the web icons and the Windows program file's icon (docs/INSTALL_AS_APP.md, "The desktop game's icon"). The window gets it from here.

#include <cstdint>
#include <vector>

struct SDL_Surface;
struct SDL_Window;

namespace ants::app::app_icon {

constexpr int32_t kCells = 32;            // the original's icon is 32 x 32 cells
constexpr int32_t kMaxZoom = 64;

/// The icon as RGBA (8 bits a channel, rows from the top), every cell `zoom` x `zoom` pixels: kCells * zoom pixels across. Cells that the original leaves empty have alpha 0, every other cell alpha 255
/// and one of the original's colors. Empty for a zoom outside 1 .. kMaxZoom.
std::vector<uint8_t> rgba(int32_t zoom);

/// Pixels a cell has in the picture that goes to the window system: 2 (64 x 64) where it takes the picture as given and scales it itself (Windows, Linux); 16 (512 x 512) on macOS, where it is the Dock's picture,
/// shown at 128 pixels or more, and a small one would be stretched.
int32_t window_zoom();

/// The icon as an SDL surface of 32 bits, RGBA in byte order (SDL_PIXELFORMAT_RGBA32), `zoom` as in rgba(). The surface owns its pixels: free it with SDL_FreeSurface. Null for a zoom outside 1 .. kMaxZoom or when SDL cannot make it.
SDL_Surface* make_surface(int32_t zoom);

/// Gives the window (and, where the system has one icon for the whole program, as macOS has in the Dock, the program) the icon, the surface of make_surface(window_zoom()). False when there is no window or the picture could not
/// be made. SDL_SetWindowIcon returns nothing, so a window system that ignores the icon (SDL's dummy video driver, a Wayland compositor) or fails to take it is not seen; the game plays on either way.
bool set_window_icon(SDL_Window* window);

}  // namespace ants::app::app_icon

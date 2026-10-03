// The pointer in the black bars, and the pointer grab of a fullscreen window.
//
// The original owns the whole screen: Ants.exe sets an exclusive DirectDraw mode of 640 x 480 (SetCooperativeLevel 0x53 at 0x102c8eb, SetDisplayMode at
// 0x102c96c) and reads the pointer as GetCursorPos gives it (0x1030a80: no clamp; the file imports no ClipCursor or SetCapture). On a one-monitor machine
// the display mode therefore keeps its pointer on the picture: pushed against an edge it stays on the edge pixel and the map keeps scrolling. (With a
// second monitor the original's pointer can leave the screen, and its input task then ignores it: FUN_0102653f, docs/GAME_REVERSE_ENGINEERING.md 5.43.)
// The remake draws the same picture inside a bigger window (fullscreen on a wide monitor, a window of any shape); SDL maps the pointer over a black bar to
// coordinates outside the picture (negative, or past 639 / 479), which the game used to ignore: no scrolling over a bar, no cursor. A pointer over a bar
// is the pointer at the nearest edge pixel of the picture, which is what the one-monitor original shows. The picture's size is a ScreenLayout's (screen_layout.hpp), and
// where it sits in the canvas is a rectangle; the functions without either are the original's 640 x 480 screen.
#pragma once

#include <cstdint>

#include <SDL.h>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

inline constexpr int32_t kScreenWidth = ScreenLayout::kClassicWidth;
inline constexpr int32_t kScreenHeight = ScreenLayout::kClassicHeight;

inline int32_t clamp_to_screen_x(int32_t x) { return x < 0 ? 0 : (x >= kScreenWidth ? kScreenWidth - 1 : x); }
inline int32_t clamp_to_screen_y(int32_t y) { return y < 0 ? 0 : (y >= kScreenHeight ? kScreenHeight - 1 : y); }

/// The pointer's limits are the picture's: a picture of a layout is `layout.width` x `layout.height` pixels, so x is held to 0 .. width - 1 and y to 0 .. height - 1
inline int32_t clamp_to_screen_x(int32_t x, const ScreenLayout& layout) { return x < 0 ? 0 : (x >= layout.width ? layout.width - 1 : x); }
inline int32_t clamp_to_screen_y(int32_t y, const ScreenLayout& layout) { return y < 0 ? 0 : (y >= layout.height ? layout.height - 1 : y); }

/// Puts a pointer event's position on the picture; every other field and every other kind of event stays as it is. The event's position is the canvas's (what SDL maps the
/// window position to); `picture` is where the picture sits in the canvas (its top left corner and its size): the position becomes the picture's own (the corner subtracted)
/// and is then held to the picture. A picture that is the whole canvas has its corner at (0, 0).
inline void clamp_pointer_event(SDL_Event& e, const LayoutRect& picture) {
    const auto place_x = [&picture](int32_t x) { x -= picture.x; return x < 0 ? 0 : (x >= picture.w ? picture.w - 1 : x); };
    const auto place_y = [&picture](int32_t y) { y -= picture.y; return y < 0 ? 0 : (y >= picture.h ? picture.h - 1 : y); };
    switch (e.type) {
        case SDL_MOUSEMOTION:
            e.motion.x = place_x(e.motion.x);
            e.motion.y = place_y(e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            e.button.x = place_x(e.button.x);
            e.button.y = place_y(e.button.y);
            break;
        default:
            break;
    }
}

/// ... of a picture that is the whole canvas, of the layout's size
inline void clamp_pointer_event(SDL_Event& e, const ScreenLayout& layout) {
    clamp_pointer_event(e, LayoutRect{0, 0, layout.width, layout.height});
}

/// ... of the original's 640 x 480 screen
inline void clamp_pointer_event(SDL_Event& e) {
    clamp_pointer_event(e, ScreenLayout::classic());
}

/// After this event the pointer has gone, although SDL sends no LEAVE: the clamped position would otherwise stay on an edge and keep the map scrolling.
/// - A button released outside the window, not merely outside the picture (`outside_window`, decided on the position before the clamp; a black bar is part
///   of the window). SDL captures the pointer while a button is held, so a drag (the rubber band, the minimap) goes on at the edge of the picture as in the
///   original; on macOS the release outside ends the capture without a LEAVE, and on the web the release comes after the LEAVE.
/// - The lift of a finger: touch arrives as mouse events with `which == SDL_TOUCH_MOUSEID`, and nothing follows a lift (no hover, no LEAVE).
/// The caller marks the pointer as outside once the event has been handled (the handlers of a button event clear that mark); the next pointer event in the
/// window clears it again. A plain motion never counts: SDL sends a LEAVE when an uncaptured pointer leaves.
inline bool pointer_gone_after(const SDL_Event& e, bool outside_window) {
    return e.type == SDL_MOUSEBUTTONUP && (outside_window || e.button.which == SDL_TOUCH_MOUSEID);
}

/// The game asks SDL to grab the pointer (it cannot leave the window) while the window is fullscreen, never in a headless run. Fullscreen is SDL's own
/// (`--fullscreen`: SDL_WINDOW_FULLSCREEN or SDL_WINDOW_FULLSCREEN_DESKTOP) or the operating system's (`os_fullscreen`: a macOS fullscreen Space entered with
/// the green button or Cmd+Ctrl+F, which SDL does not flag). A maximized window is not fullscreen: its taskbar, Dock and second monitor stay reachable.
/// The focus is not an input: SDL applies a grab that was asked for only while the window has the input focus, lets go when another window takes it and
/// grabs again when it returns (SDL_UpdateWindowGrab). So the request follows fullscreen alone, and the caller compares it with SDL's request flag, not with
/// the grab SDL holds: that one is "no" while another window has the focus, so a request compared with it would stay standing when the window leaves
/// fullscreen in the background, and SDL would grab a plain window when the focus came back.
inline bool wants_mouse_grab(bool sdl_fullscreen, bool os_fullscreen, bool headless) {
    return !headless && (sdl_fullscreen || os_fullscreen);
}

/// The Dock and the menu bar of macOS stay away from a fullscreen game (the owner: they came up whenever the pointer touched the bottom or the top edge of the screen).
/// SDL's own fullscreen (SDL_WINDOW_FULLSCREEN_DESKTOP: `--fullscreen`, Alt+Enter) already has it: SDL's window delegate answers the system's question "which presentation options does this
/// fullscreen Space use" with FullScreen | HideDock | HideMenuBar when the window has that flag (read from SDL 2.32.10's delegate, `window:willUseFullScreenPresentationOptions:`), and
/// then neither is merely auto-hidden: nothing brings them up. A Space that the player enters with the green button or Cmd+Ctrl+F (`os_fullscreen` without SDL's flag) gets the
/// system's own proposal, which auto-hides them (they come up at the edge): that one the game asks for itself, and gives back when the window leaves it (Application::update_mouse_grab).
/// Never in a headless run, and nowhere but on macOS (the caller).
inline bool wants_hidden_dock_and_menu_bar(bool sdl_fullscreen, bool os_fullscreen, bool headless) {
    return !headless && os_fullscreen && !sdl_fullscreen;
}

}  // namespace ants::app

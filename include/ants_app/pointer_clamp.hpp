// The pointer in the black bars, and the pointer grab of a fullscreen window.
//
// The original owns the whole screen: Ants.exe sets an exclusive DirectDraw mode of 640 x 480 (SetCooperativeLevel 0x53 at 0x102c8eb, SetDisplayMode at
// 0x102c96c) and reads the pointer as GetCursorPos gives it (0x1030a80: no clamp; the file imports no ClipCursor or SetCapture). On a one-monitor machine
// the display mode therefore keeps its pointer on the picture: pushed against an edge it stays on the edge pixel and the map keeps scrolling. (With a
// second monitor the original's pointer can leave the screen, and its input task then ignores it: FUN_0102653f, docs/GAME_REVERSE_ENGINEERING.md 5.43.)
// The remake draws the same picture inside a bigger window (fullscreen on a wide monitor, a window of any shape); SDL maps the pointer over a black bar to
// coordinates outside the picture (negative, or past 639 / 479), which the game used to ignore: no scrolling over a bar, no cursor. A pointer over a bar
// is the pointer at the nearest edge pixel of the picture, which is what the one-monitor original shows.
#pragma once

#include <cstdint>

#include <SDL.h>

namespace ants::app {

inline constexpr int32_t kScreenWidth = 640;
inline constexpr int32_t kScreenHeight = 480;

inline int32_t clamp_to_screen_x(int32_t x) { return x < 0 ? 0 : (x >= kScreenWidth ? kScreenWidth - 1 : x); }
inline int32_t clamp_to_screen_y(int32_t y) { return y < 0 ? 0 : (y >= kScreenHeight ? kScreenHeight - 1 : y); }

/// Puts a pointer event's position (mouse motion, button down / up) on the picture; every other field and every other kind of event stays as it is.
inline void clamp_pointer_event(SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEMOTION:
            e.motion.x = clamp_to_screen_x(e.motion.x);
            e.motion.y = clamp_to_screen_y(e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            e.button.x = clamp_to_screen_x(e.button.x);
            e.button.y = clamp_to_screen_y(e.button.y);
            break;
        default:
            break;
    }
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

}  // namespace ants::app

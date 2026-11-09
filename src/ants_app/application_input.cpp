// What the player does, as the application takes it (application.hpp): the sounds of the screens, the events of SDL, the window and its fullscreen, the keys, the mouse, and the vote of a room that has ended its match.

#include <algorithm>

#include "ants_app/application.hpp"
#include "ants_app/pointer_clamp.hpp"
#include "application_internal.hpp"

namespace ants::app {

using namespace application_detail;

// A UI sound. The click of a pressed button (buttonclick.wav, the first frame of its pressed clip) belongs to that button's sprite: it is cut when the button is
// released and its clip is replaced by the raised one (FUN_0102c0db -> StopTracked), so a short click is cut short. The other UI sounds are not tied to a release.
void Application::play_ui_sound(uint32_t sound_id) {
    const uint32_t owner = (sound_id == sim::SoundID::ButtonClick) ? kUiPressOwner : 0u;
    play_effect(sound_id, owner);
}

// A sound effect that no tick makes (the buttons, the can't-go cue): a background step makes none, the page is not heard
void Application::play_effect(uint32_t sound_id, uint32_t owner) {
    if (background_stepping_) return;
    audio_mixer_.play_sfx(sound_id, 1.0f, 255, false, owner);
}

void Application::release_ui_sounds() {
    audio_mixer_.stop_owner(kUiPressOwner);
}

void Application::handle_events() {
    SDL_Event event;
    bool pointer_gone = false;                               // the last event took the pointer away (pointer_gone_after: a release outside the window, a lifted finger)
    // A run with a start menu: a left press or release that changes the screen or the panel begins the rule for the rest of its click sequence (swallow_menu_gesture). The change is looked
    // for when the next event comes and when the events end: the handlers below leave their loop body by `continue`.
    uint32_t screen_before = menu_enabled_ ? screen_signature() : 0u;
    bool left_event = false;
    uint32_t left_event_ms = 0;
    const auto watch_screen = [&]() {
        if (!menu_enabled_) return;
        const uint32_t now_screen = screen_signature();
        if (left_event && now_screen != screen_before) begin_menu_gesture(left_event_ms);
        screen_before = now_screen;
        left_event = false;
    };
    bool touch_timers_run = false;                           // the touch model's clock runs once per call, after the events (a finger that rests makes a hold due without any event)
    for (;;) {
        if (!touch_queue_.empty()) {
            if (!take_touch_event(event)) continue;          // (a pan, a zoom, the end of a press: done there; a mouse event is handled below like any that SDL sends)
        } else if (SDL_PollEvent(&event)) {
            if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) {
                if (!game_menu_open_) feed_touch(event.tfinger);   // (what the model says is queued and comes first, before the next event of SDL's; no finger is the game's while the settings panel is up)
                continue;
            }
            if (event.type == touch_cancel_event_type()) {   // (the page's touchcancel, behind the finger events that came before it: cancel_touch_queued)
                cancel_touch();
                continue;
            }
        } else if (!touch_timers_run) {
            touch_timers_run = true;
            queue_touch(touch_.update(touch_now()));
            continue;
        } else {
            break;
        }
        watch_screen();
        left_event = (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_LEFT;
        left_event_ms = left_event ? event.button.timestamp : 0u;
        if (pointer_gone) pointer_outside_ = true;           // ... marked once that event has been handled (its handlers clear the mark)
        update_picture();                                    // (a screen that an earlier event of this frame changed)
        pointer_gone = pointer_gone_after(event, event.type == SDL_MOUSEBUTTONUP && button_outside_window(event.button));   // the position before the clamp
        clamp_pointer_event(event, picture_);                // a pointer over a black bar of a wide window is the pointer on the edge of the picture (pointer_clamp.hpp); the canvas's
                                                             // position becomes the picture's own (the picture's corner subtracted): the 640 x 480 picture centred in a 960 x 540 canvas
        if (event.type == SDL_QUIT) {
            quit();
            return;
        }
        if (event.type == SDL_MOUSEBUTTONUP) release_ui_sounds();                         // (the setup screen takes its mouse events in the loop below)

        if (event.type == SDL_WINDOWEVENT) handle_window_event(event.window);

        // Bypass Print Screen key so OS handles screenshots unimpeded
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PRINTSCREEN) {
            continue;
        }

        // Alt+Enter: fullscreen on / off (native builds). The key is the window's, not a screen's: no screen sees it (Enter would send the chat text), and a held key toggles once.
        if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) && (event.key.keysym.mod & KMOD_ALT) != 0) {
            if (event.key.repeat == 0) toggle_fullscreen();
            continue;
        }

        if (state_ == AppState::Loading) {
            if (event.type == SDL_MOUSEMOTION) note_pointer(event.motion.x, event.motion.y);            // the pointer is global in the original: the next screen knows it
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) note_pointer(event.button.x, event.button.y);
            if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN) {
                finish_loading();
            }
            continue;
        }

        if (state_ == AppState::QuickHelp) {
            if (event.type == SDL_MOUSEMOTION) {
                mouse_screen_x_ = event.motion.x;
                mouse_screen_y_ = event.motion.y;
                mouse_has_moved_ = true;
                pointer_outside_ = false;
                quick_help_move(event.motion.x, event.motion.y);
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                mouse_screen_x_ = event.button.x;
                mouse_screen_y_ = event.button.y;
                mouse_has_moved_ = true;
                pointer_outside_ = false;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (swallow_menu_gesture(event.button.clicks, event.button.timestamp)) quick_help_move(event.button.x, event.button.y);       // (the second click of the one that opened the quick help)
                    else quick_help_press(event.button.x, event.button.y);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                mouse_screen_x_ = event.button.x;
                mouse_screen_y_ = event.button.y;
                if (event.button.button == SDL_BUTTON_LEFT) quick_help_release(event.button.x, event.button.y);
            } else if (event.type == SDL_KEYDOWN) {
                quick_help_key(event.key.keysym.sym);
            }
            continue;
        }

        if (state_ == AppState::StartMenu) {
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && swallow_menu_gesture(event.button.clicks, event.button.timestamp)) {
                note_pointer(event.button.x, event.button.y);
                start_menu_.on_mouse_move(event.button.x, event.button.y);              // the pointer is there (a hover); nothing is pressed
                continue;
            }
            handle_menu_event(event);
            continue;
        }

        if (state_ == AppState::MapSelect) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    room_key_down(event.key.keysym.sym, static_cast<uint16_t>(event.key.keysym.mod), event.key.repeat != 0);       // (the screen's keys, or the chat input of a room)
                    break;
                case SDL_TEXTINPUT:
                    room_text_input(event.text.text);
                    break;
                case SDL_MOUSEMOTION:
                    mouse_screen_x_ = event.motion.x;
                    mouse_screen_y_ = event.motion.y;
                    mouse_has_moved_ = true;
                    pointer_outside_ = false;
                    map_select_.handle_mouse_motion(event.motion.x, event.motion.y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    mouse_screen_x_ = event.button.x;
                    mouse_screen_y_ = event.button.y;
                    mouse_has_moved_ = true;
                    pointer_outside_ = false;
                    if (event.button.button == SDL_BUTTON_LEFT && swallow_the_rest_of_a_click(event.button.clicks, event.button.timestamp)) {
                        map_select_.handle_mouse_motion(event.button.x, event.button.y);       // the pointer is there (a hover); nothing is pressed
                        break;
                    }
                    if (room_mouse_down(event.button.x, event.button.y, event.button.button)) break;                    // (a click on a room's status line opens its chat input)
                    map_select_.handle_mouse_down(event.button.x, event.button.y, event.button.button);
                    break;
                case SDL_MOUSEBUTTONUP:
                    mouse_screen_x_ = event.button.x;
                    mouse_screen_y_ = event.button.y;
                    mouse_has_moved_ = true;
                    pointer_outside_ = false;
                    if (room_mouse_up(event.button.x, event.button.y, event.button.button)) break;                      // (the release of a press that the chat input took)
                    map_select_.handle_mouse_up(event.button.x, event.button.y, event.button.button);
                    break;
                default:
                    break;
            }
            continue;
        }

        if (game_menu_takes(event)) continue;                // (the settings panel is up: its keys, text, clicks and wheel are the panel's)

        // Gameplay Event Dispatch
        switch (event.type) {
            case SDL_TEXTINPUT:
                if (state_ == AppState::Playing && !catch_up_screen_active()) {
                    hud_.handle_text_input(event.text.text);
                }
                break;
            case SDL_KEYDOWN:
                handle_key_down(event.key);
                break;
            case SDL_MOUSEMOTION:
                handle_mouse_motion(event.motion);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                handle_mouse_button(event.button);
                break;
            case SDL_MOUSEWHEEL:
                handle_mouse_wheel(event.wheel);
                break;
            default:
                break;
        }
    }
    watch_screen();
    if (pointer_gone) pointer_outside_ = true;
    end_game_menu_frame();
}

// Is the position of a button event, as SDL delivered it (before the clamp), outside the window? Not merely outside the picture: the black bars are part of
// the window. SDL gives the position in the picture's coordinates (SDL_RenderSetLogicalSize); SDL_RenderLogicalToWindow (SDL 2.0.18; the game already
// needs 2.0.22 for SDL_HINT_VIDEODRIVER) takes it back to the window's. An event that names no window of ours (windowID 0: SDL had no mouse focus, so the
// pointer was not over the window, and the position was not mapped either; the web build's release after a LEAVE) is outside.
bool Application::button_outside_window(const SDL_MouseButtonEvent& button) const {
    if (window_ == nullptr || button.windowID != SDL_GetWindowID(window_)) return true;
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    int wx = button.x;
    int wy = button.y;
    if (renderer_ && renderer_->get_sdl_renderer() != nullptr) {
        SDL_RenderLogicalToWindow(renderer_->get_sdl_renderer(), static_cast<float>(button.x), static_cast<float>(button.y), &wx, &wy);
    }
    return wx < 0 || wy < 0 || wx >= w || wy >= h;
}

void Application::handle_window_event(const SDL_WindowEvent& we) {
    if (we.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        cancel_touch();                                                        // (a finger whose lift will never be heard of must not hold a press, nor block the next ones)
        set_app_active(false);
        if (!config_.headless) SDL_ShowCursor(SDL_ENABLE);                     // not ours to hide while another window has the input
    }
    if (we.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        set_app_active(true);
        if (!config_.headless && !pointer_outside_) SDL_ShowCursor(SDL_DISABLE);
    }
    if (we.event == SDL_WINDOWEVENT_ENTER) {
        pointer_outside_ = false;
        if (!config_.headless) SDL_ShowCursor(SDL_DISABLE);                    // the game's own cursor takes over inside the window
    }
    if (we.event == SDL_WINDOWEVENT_LEAVE) {
        pointer_outside_ = true;                                               // no more motion arrives: the last position must not keep the map scrolling
        if (!config_.headless) SDL_ShowCursor(SDL_ENABLE);
    }
    if (we.event == SDL_WINDOWEVENT_MINIMIZED || we.event == SDL_WINDOWEVENT_HIDDEN) {
        cancel_touch();
        is_paused_ = true;
        midi_player_.pause();
    }
    if (we.event == SDL_WINDOWEVENT_RESTORED || we.event == SDL_WINDOWEVENT_SHOWN) {
        is_paused_ = false;
        midi_player_.resume();
    }
    if (we.event == SDL_WINDOWEVENT_SIZE_CHANGED) cancel_touch();              // (a rotation, a fullscreen toggle, the address bar: the same spot of the glass is another pixel of the picture, and a finger that rests would become a drag)
    if (we.event == SDL_WINDOWEVENT_SIZE_CHANGED || we.event == SDL_WINDOWEVENT_RESIZED || we.event == SDL_WINDOWEVENT_MAXIMIZED ||
        we.event == SDL_WINDOWEVENT_RESTORED) {
        if (renderer_ && window_) {
            config_.fullscreen = (SDL_GetWindowFlags(window_) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
            renderer_->refit_canvas();                           // SDL's logical size applied again for the window's size now: the picture is scaled to what fits, centred
        }
        update_mouse_grab();
    }
}

// In fullscreen the pointer stays in the window while the game has the focus (wants_mouse_grab, pointer_clamp.hpp): SDL's fullscreen (--fullscreen) and a
// macOS fullscreen Space (os_fullscreen) both ask SDL for the grab, and SDL applies it while the window has the input focus; with a second monitor the
// pointer used to slip away at the edge of the picture, and the edge scrolling stopped. The original's exclusive 640 x 480 mode gives the same on a
// one-monitor machine (the display mode does it: Ants.exe never clips the pointer, and with a second monitor its pointer could leave and its edge scrolling
// stopped, as the remake's did). In a window the pointer is free. Limits: a fullscreen toggle of a Linux window manager (F11) is not detected, so it does
// not grab. On X11, when SDL applies the grab (at the start, on entering fullscreen, when the focus returns) while another client holds the pointer (a
// menu, a screenshot tool), SDL retries XGrabPointer 100 times, 50 ms apart, inside that call: the game can stall for up to 5 s, once, because after a
// failed round SDL stops grabbing for the rest of the run (SDL_GetWindowMouseGrab still answers yes); SDL has no hint to shorten it. The web build never
// grabs: the browser decides (pointer lock). Called at the start and on every size event (the focus needs no call: SDL lets go and grabs again itself);
// macOS reports the entry into and the exit from a Space with SIZE_CHANGED, RESIZED and MAXIMIZED or RESTORED (SDL 2.32's Cocoa listener sends them at the
// end of the transition).
void Application::update_mouse_grab() {
#if !defined(__EMSCRIPTEN__)
    if (window_ == nullptr) return;
    const uint32_t flags = SDL_GetWindowFlags(window_);
    const bool sdl_fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    const bool space = !config_.headless && os_fullscreen(window_);                          // (a headless run asks nothing of the window system)
    const bool want = wants_mouse_grab(sdl_fullscreen, space, config_.headless);
    const bool asked = (flags & SDL_WINDOW_MOUSE_GRABBED) != 0;                              // the request (SDL_GetWindowMouseGrab is its effect: no while unfocused)
    if (asked != want) SDL_SetWindowMouseGrab(window_, want ? SDL_TRUE : SDL_FALSE);
#if defined(__APPLE__)
    // The Dock and the menu bar of a fullscreen Space that SDL's flags do not describe (the green button, Cmd+Ctrl+F): hidden for good while the window is in it, the system's own
    // behaviour given back when it leaves (SDL's own fullscreen needs none of this: its delegate asks for both to be hidden, see wants_hidden_dock_and_menu_bar)
    const bool hide = wants_hidden_dock_and_menu_bar(sdl_fullscreen, space, config_.headless);
    if (hide != presentation_hidden_) {
        presentation_hidden_ = hide;
        set_presentation_options(hide ? (kPresentationFullScreen | kPresentationHideDock | kPresentationHideMenuBar) : 0ul);
    }
#endif
#endif
}

bool Application::toggle_fullscreen() {
#if defined(__EMSCRIPTEN__)
    return false;                                         // the page's own button does it (a browser asks for the fullscreen itself)
#else
    if (window_ == nullptr) return false;
    const uint32_t flags = SDL_GetWindowFlags(window_);
    const bool sdl_fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    if (!sdl_fullscreen && !config_.headless && os_fullscreen(window_)) return true;      // a macOS fullscreen Space: the operating system's controls leave it, not SDL's flags
    SDL_SetWindowFullscreen(window_, sdl_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    config_.fullscreen = !sdl_fullscreen;
    update_mouse_grab();                                  // (the size events that follow do it again)
    return !sdl_fullscreen;
#endif
}

WindowRect Application::window_rect() const {
    WindowRect r;
    if (window_ != nullptr) {
        SDL_GetWindowPosition(window_, &r.x, &r.y);
        SDL_GetWindowSize(window_, &r.w, &r.h);
    }
    return r;
}

// The window that is CREATED: its size, and its place where that is known. The size is the shape of the picture that it will show, so that no frame is ever shown in another shape: a window
// that was created 4:3 (the config's 1280 x 960) and made 16:9 by apply_window_layout before the first frame was seen on screen as a 4:3 window for a moment at the start. An explicit
// --window-size is that size; a canvas that is not the original's 4:3 is the size that apply_window_layout would give it (the largest scale in steps of 0.5 that fits the display's usable
// area, with the title bar of a typical window system, as its own fallback assumes: the borders of a window that does not exist yet cannot be asked), the canvas itself at 1x when the display
// cannot be asked; the original's own 4:3 (--aspect 4:3, the settings' key) is the config's 1280 x 960. A cell of the start scripts' grid (--grid CxR --cell N) is created AT ITS rectangle,
// the one that apply_window_layout gives it (the largest rectangle of the canvas's shape that fits the cell, with the same assumed title bar): the four windows of start_game.sh used to be
// created at the config's 4:3 and cut to their cell once they existed, so that a 4:3 window was there at the start, for good on a system that does not apply the cut at once. Fullscreen (the
// config's size, the way back is sized by apply_window_layout) and the web build, whose window is the page's canvas, are as they always were.
WindowRect Application::initial_window_rect() const {
    WindowRect r{SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, config_.window_width, config_.window_height};
#if !defined(__EMSCRIPTEN__)
    const bool in_grid = config_.grid_cols > 0 && config_.grid_rows > 0;
    if (in_grid && !config_.fullscreen) {                    // (as in apply_window_layout the cell wins over --window-size and --window-pos)
        SDL_Rect area{0, 0, 0, 0};
        const int display = std::max(0, config_.display_index);
        if (SDL_GetDisplayUsableBounds(display, &area) == 0 || SDL_GetDisplayBounds(display, &area) == 0) {
            r = grid_cell_window(WindowRect{area.x, area.y, area.w, area.h}, config_.grid_cols, config_.grid_rows, config_.grid_cell, 28, 0, 0, 0, canvas_width_of(aspect_), canvas_height_of(aspect_));
        } else if (aspect_ != Aspect::Classic4x3) {          // (the display cannot be asked: the canvas at 1x, never the config's 4:3)
            r.w = canvas_width_of(aspect_);
            r.h = canvas_height_of(aspect_);
        }
        return r;
    }
    if (!config_.fullscreen) {
        if (config_.has_window_size) {
            r.w = config_.window_w;
            r.h = config_.window_h;
        } else if (aspect_ != Aspect::Classic4x3) {
            SDL_Rect area{0, 0, 0, 0};
            const int display = std::max(0, config_.display_index);
            if (SDL_GetDisplayUsableBounds(display, &area) == 0 || SDL_GetDisplayBounds(display, &area) == 0) {
                r = default_canvas_window(WindowRect{area.x, area.y, area.w, area.h}, canvas_width_of(aspect_), canvas_height_of(aspect_), config_.headless ? 0 : 28, 0, 0, 0);
            } else {
                r.w = canvas_width_of(aspect_);
                r.h = canvas_height_of(aspect_);
            }
        }
    }
    if (config_.has_window_pos) {
        r.x = config_.window_x;
        r.y = config_.window_y;
    }
#endif
    return r;
}

// --grid CxR --cell N puts the window into a cell of a grid over the usable part of the display (the start scripts: four games, one per quarter); --window-pos
// and --window-size say it outright. The window's decoration (title bar, frame) is part of the cell.
void Application::apply_window_layout() {
#if !defined(__EMSCRIPTEN__)
    if (window_ == nullptr) return;
    if (config_.grid_cols > 0 && config_.grid_rows > 0) {
        int display = config_.display_index >= 0 ? config_.display_index : SDL_GetWindowDisplayIndex(window_);
        if (display < 0) display = 0;
        SDL_Rect area{0, 0, 0, 0};
        if (SDL_GetDisplayUsableBounds(display, &area) != 0 && SDL_GetDisplayBounds(display, &area) != 0) return;
        int top = 0;
        int left = 0;
        int bottom = 0;
        int right = 0;
        if (SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right) != 0) {     // a platform that cannot tell: assume a typical title bar
            top = 28;
            left = bottom = right = 0;
        }
        const WindowRect r = grid_cell_window(WindowRect{area.x, area.y, area.w, area.h}, config_.grid_cols, config_.grid_rows, config_.grid_cell, top, left, bottom, right,
                                              canvas_width_of(aspect_), canvas_height_of(aspect_));
        SDL_SetWindowSize(window_, r.w, r.h);
        SDL_SetWindowPosition(window_, r.x, r.y);
        return;
    }
    if (config_.has_window_size) {
        SDL_SetWindowSize(window_, config_.window_w, config_.window_h);
    } else if (aspect_ != Aspect::Classic4x3) {
        // a game of another aspect opens at the largest scale in steps of 0.5 of its canvas that fits the usable area of the display (at least 1x), centred (window_layout.hpp). A game that
        // starts in fullscreen gets the same size: it is the window that Alt+Enter (or the macOS fullscreen button) gives back, and it has the canvas's shape, not the config's 4:3 one
        int display = config_.display_index >= 0 ? config_.display_index : SDL_GetWindowDisplayIndex(window_);
        if (display < 0) display = 0;
        SDL_Rect area{0, 0, 0, 0};
        if (SDL_GetDisplayUsableBounds(display, &area) == 0 || SDL_GetDisplayBounds(display, &area) == 0) {
            int top = 0;
            int left = 0;
            int bottom = 0;
            int right = 0;
            if (!config_.headless && (config_.fullscreen || SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right) != 0)) {      // a fullscreen window has no borders to measure, nor has a platform that cannot tell: assume a typical title bar
                top = 28;
                left = bottom = right = 0;
            }
            const WindowRect r = default_canvas_window(WindowRect{area.x, area.y, area.w, area.h}, canvas_width_of(aspect_), canvas_height_of(aspect_), top, left, bottom, right);
            SDL_SetWindowSize(window_, r.w, r.h);                 // (in fullscreen SDL keeps it as the windowed size, for the way back)
            if (!config_.has_window_pos) SDL_SetWindowPosition(window_, r.x, r.y);
        }
    }
    if (config_.has_window_pos) SDL_SetWindowPosition(window_, config_.window_x, config_.window_y);
#endif
}

void Application::handle_key_down(const SDL_KeyboardEvent& key) {
    const OrdersNoted noted{*this};
    if (scorecard_.is_open()) {
        // FUN_01015b17: Enter and the letters C, Q and X (either case, whatever the modifiers) leave, at any time; nothing else does anything (Esc included)
        const SDL_Keycode sym = key.keysym.sym;
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_c || sym == SDLK_q || sym == SDLK_x) scorecard_.leave();
        return;
    }

    if (handle_way_back_key(key)) return;                    // Esc on the way back asks to leave; nothing else works on the catch-up screen
    if (handle_vote_key(key)) return;                        // F2 and F3 are the vote block's keys while a vote is open (the original's keys are F1 and F9 - F12)

    bool ctrl_or_gui = (key.keysym.mod & KMOD_CTRL) || (key.keysym.mod & KMOD_GUI);

    // Only the original's keys exist (HUD::handle_key_down, FUN_0102609a): there are no screenshot, tile grid, mute, team switch or fullscreen keys.

    // Printable keys reach the always-active chat box as text input events (SDL_TEXTINPUT); the key event only carries the control keys. A dialog
    // reads the keys itself (Y / N of the quit dialog, C / X of the quick help).
    if (!ctrl_or_gui && key.keysym.sym >= 32 && key.keysym.sym <= 126 && !hud_.is_modal_open()) {
        return;
    }

    hud_.handle_key_down(key.keysym.sym, view_sim(), renderer_->camera(), key.keysym.mod, key.repeat != 0);
}

void Application::handle_mouse_motion(const SDL_MouseMotionEvent& motion) {
    mouse_screen_x_ = motion.x;
    mouse_screen_y_ = motion.y;
    mouse_has_moved_ = true;
    pointer_outside_ = false;
    if (catch_up_screen_active() && !hud_.is_quit_dialog_open()) return;       // (no match is on the screen: its hover, rubber band and minimap have nothing to do)

    if (scorecard_.is_open()) {
        scorecard_.handle_mouse_motion(motion.x, motion.y);
        return;
    }

    hud_.handle_mouse_motion(motion.x, motion.y, view_sim(), renderer_->camera());
}

void Application::handle_mouse_button(const SDL_MouseButtonEvent& button) {
    const OrdersNoted noted{*this};
    mouse_screen_x_ = button.x;
    mouse_screen_y_ = button.y;
    mouse_has_moved_ = true;
    pointer_outside_ = false;
    if (button.type == SDL_MOUSEBUTTONUP) release_ui_sounds();                          // before the handlers: what the release itself plays is not cut

    if (scorecard_.is_open()) {
        if (button.button != SDL_BUTTON_LEFT) return;                      // the button class knows the left button only (FUN_01011206): the screen swallows the others
        if (button.type == SDL_MOUSEBUTTONDOWN) {
            scorecard_.handle_mouse_down(button.x, button.y);
        } else if (button.type == SDL_MOUSEBUTTONUP) {
            scorecard_.handle_mouse_up(button.x, button.y);
        }
        return;
    }

    if (catch_up_screen_active() && !hud_.is_quit_dialog_open()) return;  // (no match is on the screen: a click would give orders to a map that nobody sees)
    if (handle_vote_mouse(button)) return;                               // a press and a release on a button of the vote block are that choice (nothing of it reaches the map)

    if (button.button == SDL_BUTTON_MIDDLE) {                            // the original has no use for it; the remake's: back to the zoom 1, towards the pointer (the release is nobody's)
        if (button.type == SDL_MOUSEBUTTONDOWN && view_zoom_allowed(button.x, button.y)) set_zoom(zoom::kNormal, button.x, button.y);
        return;
    }

    uint16_t mod = static_cast<uint16_t>(SDL_GetModState());
    if (button.type == SDL_MOUSEBUTTONDOWN) {
        hud_.handle_mouse_down(button.x, button.y, button.button, view_sim(), renderer_->camera(), mod);
    } else if (button.type == SDL_MOUSEBUTTONUP) {
        hud_.handle_mouse_up(button.x, button.y, button.button, view_sim(), renderer_->camera(), mod);
    }
}

// The keys of the way back: Esc (this machine's link is lost, or it catches up) opens the quit dialog that Ctrl+Q opens; a held Esc asks once. The catch-up screen has no match to give a key to:
// no other key does anything there, except the open dialog's Y, N and Esc.
bool Application::handle_way_back_key(const SDL_KeyboardEvent& key) {
    if (state_ != AppState::Playing || scorecard_.is_open() || !network_active()) return false;
    const bool catching_up = catch_up_screen_active();
    const bool away = catching_up || (net_->phase() == net::NetGame::Phase::Playing && net_->pause_info().reconnecting);
    if (!away) return false;
    if (key.keysym.sym == SDLK_ESCAPE) {
        if (key.repeat != 0) return true;
        if (hud_.is_quit_dialog_open() || (hud_.is_modal_open() && !hud_.is_match_start_modal_active())) return false;      // (a dialog's own Esc)
        hud_.open_quit_dialog();
        return true;
    }
    if (hud_.is_quit_dialog_open()) return false;                        // (the dialog takes its keys: Y and N)
    return catching_up;                                                  // (the match is not on the screen: no other key reaches it)
}

// The vote block's keys: F2 keeps waiting, F3 goes on without the seat. A fresh press of the other key changes the vote (the server counts the last choice of every player). Only while a vote is open
// on this machine's screen and no dialog is over it; a held key sends once.
bool Application::handle_vote_key(const SDL_KeyboardEvent& key) {
    if (key.keysym.sym != SDLK_F2 && key.keysym.sym != SDLK_F3) return false;
    if (state_ != AppState::Playing || scorecard_.is_open() || !network_active()) return false;
    if (hud_.is_modal_open() && !hud_.is_match_start_modal_active()) return false;               // a dialog takes every key
    if (!net_->pause_info().vote_open) return false;
    if (key.repeat == 0) net_->vote(key.keysym.sym == SDLK_F2);
    return true;
}

// The vote block's buttons are the button class of the original's screens: the press captures, the release inside the same button is the choice, and what the button took never reaches the map
bool Application::handle_vote_mouse(const SDL_MouseButtonEvent& button) {
    if (button.button != SDL_BUTTON_LEFT) return false;
    if (state_ != AppState::Playing || scorecard_.is_open() || !network_active()) {
        vote_press_ = 0;
        return false;
    }
    if (button.type == SDL_MOUSEBUTTONDOWN) {
        vote_press_ = 0;
        if (hud_.is_modal_open() && !hud_.is_match_start_modal_active()) return false;
        const NetVoteButtons buttons = net_vote_buttons();
        if (!buttons.open) return false;
        if (buttons.keep.contains(button.x, button.y)) vote_press_ = 1;
        else if (buttons.go_on.contains(button.x, button.y)) vote_press_ = 2;
        return vote_press_ != 0;
    }
    if (vote_press_ == 0) return false;
    const uint8_t pressed = vote_press_;
    vote_press_ = 0;
    const NetVoteButtons buttons = net_vote_buttons();
    if (buttons.open && (pressed == 1 ? buttons.keep : buttons.go_on).contains(button.x, button.y)) net_->vote(pressed == 1);
    return true;                                                         // (the release of a press that this took is nobody else's)
}

} // namespace ants::app

#pragma once

// What application.cpp, application_match.cpp and application_input.cpp share: the name of this computer's user, and the questions that the native window system answers (a fullscreen
// Space of macOS, the Dock and the menu bar). It is not part of the library's interface.

#include "ants_app/application.hpp"
#include "ants_app/start_menu.hpp"

#include <cstdlib>
#include <string>
#if defined(_WIN32)
  #include <winsock2.h>
  #include <windows.h>
#elif !defined(__EMSCRIPTEN__)
  #include <unistd.h>
#endif
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  #if defined(__has_include) && __has_include(<SDL_syswm.h>)
    #include <SDL_syswm.h>
  #else
    #include <SDL2/SDL_syswm.h>
  #endif
  #include <objc/message.h>                                  // os_fullscreen asks the window's NSWindow for its style mask (libobjc, src/ants_app/CMakeLists.txt)
  #include <objc/runtime.h>
#endif

namespace ants::app::application_detail {

inline std::string get_system_username() {
#if defined(__EMSCRIPTEN__)
    return "WebPlayer";
#else
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("USERNAME");
    std::string u = (user && *user) ? user : "Player";
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) == 0 && host[0]) {
        std::string h(host);
        size_t dot = h.find('.');
        if (dot != std::string::npos) {
            h = h.substr(0, dot);
        }
        u += "@" + h;
    }
    // Cut as a typed name is (printable, at most 32 characters): a long machine name pushed the end of the original's news ("... are a team now!") out of its chat entry (100 characters)
    const std::string name = clean_player_name(u);
    return name.empty() ? std::string("Player") : name;
#endif
}

#if !defined(__EMSCRIPTEN__)
// The operating system holds the window fullscreen without SDL's fullscreen flags. macOS: a fullscreen Space (the green button, or Cmd+Ctrl+F, the item
// Toggle Full Screen of SDL's Window menu) carries neither SDL_WINDOW_FULLSCREEN nor SDL_WINDOW_FULLSCREEN_DESKTOP, only SDL_WINDOW_MAXIMIZED, which a plain
// zoom gives too; the NSWindow's style mask tells (NSWindowStyleMaskFullScreen, 1 << 14). Elsewhere false: a fullscreen toggle of a Linux window manager
// (F11, a menu) is NOT detected (SDL does not report it and the game does not ask X11 or Wayland), so such a window does not grab the pointer; Windows
// has no such toggle for a normal window.
inline bool os_fullscreen(SDL_Window* window) {
#if defined(__APPLE__) && defined(SDL_VIDEO_DRIVER_COCOA)
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window == nullptr || SDL_GetWindowWMInfo(window, &info) != SDL_TRUE || info.subsystem != SDL_SYSWM_COCOA || info.info.cocoa.window == nullptr) return false;
    using StyleMaskFn = unsigned long (*)(id, SEL);                     // -[NSWindow styleMask] returns an NSUInteger
    const unsigned long mask = reinterpret_cast<StyleMaskFn>(objc_msgSend)(reinterpret_cast<id>(info.info.cocoa.window), sel_registerName("styleMask"));
    return (mask & (1ul << 14)) != 0;
#else
    (void)window;
    return false;
#endif
}

#if defined(__APPLE__)
// -[NSApplication setPresentationOptions:] through the runtime, as os_fullscreen asks the window (NSApplicationPresentationOptions: HideDock 1 << 1, HideMenuBar 1 << 3, FullScreen 1 << 10).
// What a fullscreen Space needs so that the Dock and the menu bar do not come up when the pointer touches the edge of the screen (wants_hidden_dock_and_menu_bar). An exception of the
// call (the runtime raises one for an invalid combination) is caught: the game plays on with what the system gave it.
constexpr unsigned long kPresentationHideDock = 1ul << 1;
constexpr unsigned long kPresentationHideMenuBar = 1ul << 3;
constexpr unsigned long kPresentationFullScreen = 1ul << 10;
inline void set_presentation_options(unsigned long options) {
    try {
        using SharedFn = id (*)(Class, SEL);
        using SetFn = void (*)(id, SEL, unsigned long);
        const id app = reinterpret_cast<SharedFn>(objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
        if (app != nullptr) reinterpret_cast<SetFn>(objc_msgSend)(app, sel_registerName("setPresentationOptions:"), options);
    } catch (...) {
    }
}
#endif
#endif

} // namespace ants::app::application_detail

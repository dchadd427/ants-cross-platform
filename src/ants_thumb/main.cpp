// ants_thumb: the picture of a map, made by the game's own drawing, with no window.
//
//   ants_thumb --chd ants.chd --out PICTURE.png [--memory-mb N] [--seconds N] MAP.lvl
//
// It loads the map the way the setup screen does, has the game's renderer (SDL's dummy video driver and software renderer: the same pixels on every machine) draw the whole world and reduces it to
// the 300 x 300 box of the setup screen (map_thumbnail.hpp), the picture that the library's maps have, and writes it as a PNG (png_encode.hpp). The game server runs it for the maps of players
// (docs/MAP_SUBMISSIONS.md, "Pictures"), one process for one picture, so that a map that goes wrong costs the server nothing but that process: the program limits itself (--memory-mb, default
// 1536: the address space; --seconds, default 25: the CPU time; the size of the file it may write) before it reads a byte of the map, and it is run by the server with a time limit of its own.
//
// Exit codes: 0 the picture is written; 2 the command line is wrong; 3 the archive or the map cannot be read; 4 the map cannot be drawn; 5 the picture cannot be written. One line of the reason goes to
// standard error. It reads only the two files it is given and writes only the one it is told to.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include <SDL.h>

#include "ants_app/map_thumbnail.hpp"
#include "ants_app/png_encode.hpp"
#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace {

struct Options {
    std::string chd;
    std::string out;
    std::string map;
    long memory_mb{1536};
    long seconds{25};
};

bool number(const char* text, long low, long high, long& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < low || v > high) return false;
    out = v;
    return true;
}

bool parse(int argc, char* argv[], Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has_value = i + 1 < argc;
        if (a == "--chd" && has_value) {
            o.chd = argv[++i];
        } else if (a == "--out" && has_value) {
            o.out = argv[++i];
        } else if (a == "--memory-mb" && has_value) {
            if (!number(argv[++i], 256, 65536, o.memory_mb)) return false;
        } else if (a == "--seconds" && has_value) {
            if (!number(argv[++i], 1, 3600, o.seconds)) return false;
        } else if (!a.empty() && a[0] != '-' && o.map.empty()) {
            o.map = a;
        } else {
            return false;
        }
    }
    return !o.chd.empty() && !o.out.empty() && !o.map.empty();
}

/// The limits of this process (POSIX; Windows has none here): the address space, the CPU time, the size of a file it writes, no core file. A limit that cannot be set (macOS keeps no address-space limit)
/// is left as it is: the server's own time limit stands behind them.
void limit_self(const Options& o) {
#if !defined(_WIN32)
    const auto set = [](int resource, rlim_t value) {
        rlimit lim{};
        if (getrlimit(resource, &lim) != 0) return;
        if (lim.rlim_max != RLIM_INFINITY && value > lim.rlim_max) value = lim.rlim_max;
        lim.rlim_cur = value;
        setrlimit(resource, &lim);
    };
    set(RLIMIT_AS, static_cast<rlim_t>(o.memory_mb) * 1024u * 1024u);
    set(RLIMIT_CPU, static_cast<rlim_t>(o.seconds));
    set(RLIMIT_FSIZE, static_cast<rlim_t>(4) * 1024u * 1024u);
    set(RLIMIT_CORE, 0);
#else
    (void)o;
#endif
}

int fail(int code, const std::string& why) {
    std::cerr << "ants_thumb: " << why << "\n";
    return code;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options o;
    if (!parse(argc, argv, o)) {
        return fail(2, "usage: ants_thumb --chd ants.chd --out PICTURE.png [--memory-mb N] [--seconds N] MAP.lvl");
    }
    limit_self(o);

    ants::assets::AssetArchive archive;
    if (!archive.load_from_file(o.chd)) return fail(3, "the archive cannot be read");
    ants::assets::LevelData level;
    ants::assets::LevelValidation verdict;
    if (!level.load_from_file(o.map, &verdict, ants::assets::LevelLimits::for_play_extended())) {
        return fail(3, verdict.first_fatal() ? "the map cannot be read: " + verdict.reason() : std::string("the map cannot be read"));
    }

    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return fail(4, std::string("SDL: ") + SDL_GetError());
    constexpr int kCanvasW = 960;
    constexpr int kCanvasH = 540;
    SDL_Window* window = SDL_CreateWindow("ants_thumb", 0, 0, kCanvasW, kCanvasH, SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        SDL_Quit();
        return fail(4, std::string("SDL window: ") + SDL_GetError());
    }
    int code = 0;
    std::string png;
    {
        ants::app::Renderer renderer;
        std::streambuf* const cout_before = std::cout.rdbuf(nullptr);              // (the renderer reports the font it found on standard output: nothing of that is wanted here)
        const bool up = renderer.init(window, archive);
        std::cout.rdbuf(cout_before);
        if (!up) {
            code = fail(4, "the renderer is not up");
        } else {
            renderer.set_canvas_size(kCanvasW, kCanvasH);
            const ants::app::MapThumbnail thumb = ants::app::make_map_thumbnail(renderer, level, archive.get_palette());
            if (!thumb.valid()) {
                code = fail(4, thumb.why.empty() ? std::string("the map cannot be drawn") : "the map cannot be drawn: " + thumb.why);
            } else {
                png = ants::app::encode_png_rgb(thumb.rgb.data(), thumb.width, thumb.height);
                if (png.empty()) code = fail(4, "the picture cannot be encoded");
            }
        }
        renderer.shutdown();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (code != 0) return code;
    std::ofstream out(o.out, std::ios::binary | std::ios::trunc);
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    out.close();
    if (!out) return fail(5, "the picture cannot be written");
    return 0;
}

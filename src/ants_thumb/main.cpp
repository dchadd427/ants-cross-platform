// ants_thumb: the picture of a map, made by the game's own drawing, with no window.
//
//   ants_thumb --chd ants.chd --out PICTURE.png [--memory-mb N] [--seconds N] [--say-sandbox] MAP.lvl
//   ants_thumb --sandbox-probe FILE            (what the program takes away from itself, and that it works: the lines it prints are below)
//
// It loads the map the way the setup screen does, has the game's renderer (SDL's dummy video driver and software renderer: the same pixels on every machine) draw the whole world and reduces it to
// the 300 x 300 box of the setup screen (map_thumbnail.hpp), the picture that the library's maps have, and writes it as a PNG (png_encode.hpp). The game server runs it for the maps of players
// (docs/MAP_SUBMISSIONS.md, "Pictures"), one process for one picture, so that a map that goes wrong costs the server nothing but that process.
//
// The program limits itself before it reads a byte of the map: --memory-mb (default 1536) the address space, --seconds (default 25) the CPU time, and the size of the file that it may write. Then it holds
// what it needs open (the archive read, the map's bytes read, the window and the renderer up, the picture's file open) and takes the rest away (sandbox.hpp: no file, no socket, no new program, where the
// kernel allows) BEFORE it parses the map: from there on the bytes of a map reach a program that can neither read the server's secrets nor send anything anywhere.
//
// --say-sandbox puts one line on standard error, when the sandbox has been applied and before the map is read: "ants_thumb: sandbox: <what is in force>" (the tests, and an operator who wants to see it).
//
// Exit codes: 0 the picture is written; 2 the command line is wrong; 3 the archive or the map cannot be read; 4 the map cannot be drawn; 5 the picture cannot be written. One line of the reason goes to
// standard error. It reads only the two files it is given and writes only the one it is told to; a failure after the picture's file was opened leaves that file empty (the caller deletes it).
//
// --sandbox-probe FILE prints what a test or an operator wants to know: "before: opened|refused" (FILE can be read), "sandbox: <what is in force>", "after: opened|refused", "inet: made|refused" and
// "unix: made|refused" (a socket can be made); it needs no archive and no map.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <SDL.h>

#include "sandbox.hpp"
#include "ants_app/map_thumbnail.hpp"
#include "ants_app/png_encode.hpp"
#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

struct Options {
    std::string chd;
    std::string out;
    std::string map;
    long memory_mb{1536};
    long seconds{25};
    std::string probe;
    bool say_sandbox{false};
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
        } else if (a == "--say-sandbox") {
            o.say_sandbox = true;
        } else if (a == "--sandbox-probe" && has_value) {
            o.probe = argv[++i];
        } else if (a == "--memory-mb" && has_value) {
            if (!number(argv[++i], 16, 65536, o.memory_mb)) return false;
        } else if (a == "--seconds" && has_value) {
            if (!number(argv[++i], 1, 3600, o.seconds)) return false;
        } else if (!a.empty() && a[0] != '-' && o.map.empty()) {
            o.map = a;
        } else {
            return false;
        }
    }
    if (!o.probe.empty()) return o.chd.empty() && o.out.empty() && o.map.empty();
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

/// Whether FILE can be opened for reading (POSIX; Windows has no sandbox here, and the probe says "refused" nowhere)
bool can_read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    char byte = 0;
    return in.read(&byte, 1).gcount() == 1;
}

bool can_make_socket(int family) {
#if !defined(_WIN32)
    const int fd = socket(family, SOCK_STREAM, 0);
    if (fd >= 0) close(fd);
    return fd >= 0;
#else
    (void)family;
    return true;
#endif
}

int probe(const std::string& file) {
    const auto word = [](bool yes, const char* if_yes, const char* if_no) { return yes ? if_yes : if_no; };
    std::cout << "before: " << word(can_read(file), "opened", "refused") << "\n";
    const ants_thumb::Sandbox sandbox = ants_thumb::apply_sandbox();
    std::cout << "sandbox: " << ants_thumb::describe(sandbox) << "\n";
    std::cout << "after: " << word(can_read(file), "opened", "refused") << "\n";
#if !defined(_WIN32)
    std::cout << "inet: " << word(can_make_socket(AF_INET), "made", "refused") << "\n";
    std::cout << "unix: " << word(can_make_socket(AF_UNIX), "made", "refused") << "\n";
#endif
    return 0;
}

/// The bytes of the map file, at most the largest file that the server keeps (MapStore::kMaxMapBytes) and not empty; false otherwise
bool read_map_bytes(const std::string& path, std::vector<uint8_t>& out) {
    constexpr std::streamoff kMostBytes = 4 * 1024 * 1024;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > kMostBytes) return false;
    out.resize(static_cast<size_t>(size));
    in.seekg(0);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), size));
}

}  // namespace

int main(int argc, char* argv[]) {
    Options o;
    if (!parse(argc, argv, o)) {
        return fail(2, "usage: ants_thumb --chd ants.chd --out PICTURE.png [--memory-mb N] [--seconds N] [--say-sandbox] MAP.lvl | --sandbox-probe FILE");
    }
    if (!o.probe.empty()) return probe(o.probe);
    limit_self(o);

    // everything that needs a file comes first: the archive, the map's bytes (not yet read as a map), the picture's file
    ants::assets::AssetArchive archive;
    if (!archive.load_from_file(o.chd)) return fail(3, "the archive cannot be read");
    std::vector<uint8_t> map_bytes;
    if (!read_map_bytes(o.map, map_bytes)) return fail(3, "the map cannot be read: the file cannot be opened or is not of a size that a map has");
    std::ofstream out(o.out, std::ios::binary | std::ios::trunc);
    if (!out) return fail(5, "the picture cannot be written");

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
            const ants_thumb::Sandbox sandbox = ants_thumb::apply_sandbox();     // (what is not here now is not reached by the bytes of the map: sandbox.hpp)
            if (o.say_sandbox) std::cerr << "ants_thumb: sandbox: " << ants_thumb::describe(sandbox) << "\n";
            ants::assets::LevelData level;
            ants::assets::LevelValidation verdict;
            if (!level.load_from_memory(map_bytes.data(), map_bytes.size(), &verdict, ants::assets::LevelLimits::for_play_extended())) {
                code = fail(3, verdict.first_fatal() ? "the map cannot be read: " + verdict.reason() : std::string("the map cannot be read"));
            } else {
                const ants::app::MapThumbnail thumb = ants::app::make_map_thumbnail(renderer, level, archive.get_palette());
                if (!thumb.valid()) {
                    code = fail(4, thumb.why.empty() ? std::string("the map cannot be drawn") : "the map cannot be drawn: " + thumb.why);
                } else {
                    png = ants::app::encode_png_rgb(thumb.rgb.data(), thumb.width, thumb.height);
                    if (png.empty()) code = fail(4, "the picture cannot be encoded");
                }
            }
        }
        renderer.shutdown();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (code != 0) return code;
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    out.close();
    if (!out) return fail(5, "the picture cannot be written");
    return 0;
}

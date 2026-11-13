// A small program that the web image's Dockerfile compiles and links twice, before it copies any of the game's files, so that Emscripten makes its system libraries (libc, libc++, the
// allocator, ...) and fetches and builds its ports (SDL2, SDL2_ttf, FreeType, HarfBuzz) in a layer that depends on the Emscripten version and on this file only. A release then does not
// make them again (about 100 seconds of every build). The two builds use the flags of the two programs of CMakeLists.txt and src/ants_app/CMakeLists.txt (tests/scripts/test_docker_layers.py
// keeps them together): WARM_CORE is the second program's, the map core; with the macro unset this file is the game's. Nothing of this is in the image.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef WARM_CORE

extern "C" int warm_core(int n) {
    try {
        if (n < 0) throw std::runtime_error("negative");
        std::vector<int> v(static_cast<std::size_t>(n) + 1U, 3);
        std::map<int, std::string> m{{n, std::to_string(n)}};
        std::sort(v.begin(), v.end());
        return static_cast<int>(v.size() + m.size());
    } catch (const std::exception& e) {
        return static_cast<int>(std::string(e.what()).size());
    }
}

#else

#include <SDL.h>
#include <SDL_ttf.h>
#include <emscripten.h>

int main(int argc, char* argv[]) {
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0 || TTF_Init() != 0) return 1;
    SDL_Window* window = SDL_CreateWindow("warm", 0, 0, 64, 64, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font* font = TTF_OpenFont("warm.ttf", 12);
    SDL_Surface* text = font ? TTF_RenderUTF8_Blended(font, "warm", SDL_Color{255, 255, 255, 255}) : nullptr;
    SDL_AudioSpec want{};
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    std::mt19937 rng(static_cast<unsigned>(argc));
    std::ofstream out("warm.txt");
    out << std::filesystem::current_path().string() << rng() << std::chrono::steady_clock::now().time_since_epoch().count() << std::endl;
    std::cout << (text != nullptr) << audio << (renderer != nullptr) << std::endl;
    emscripten_run_script("console.log('warm')");
    SDL_Quit();
    return 0;
}

#endif

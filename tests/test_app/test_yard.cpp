// The Yard's client (docs/YARD.md): the real game with a picture of the map alone and a world that the batches of a server rewrite, driven headless with made-up batches (no server, no network).
// The groups: L the picture's geometry, P the puppet world, S the order sink, H the HUD in the Yard's layout (the gates behind `layout_.yard`), F the command line, A, Z, W, I, T, Q, E, B, K the
// application (start, size, frame, mouse, keys and fingers, zoom, the edge scroll, the page's calls, the settings). The normal game's HUD, picture and zoom are pinned by test_yard_isolation and by
// the tests that were there before the Yard. Usage: test_yard. Exit code 0 when every check passes. The mutants of the gates: tests/data/yard_mutants.json.
#include "yard_fixtures.hpp"

void yard_model_groups(const ants::assets::AssetArchive& arc);
void yard_app_groups();
void yard_command_line_group();

int main(int argc, char* argv[]) {
    using namespace yardtest;
    (void)argc;
    (void)argv;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    if (yard_map_file().empty()) {
        std::fprintf(stderr, "cannot write the test map\n");
        return 2;
    }
    {
        const QuietStdout quiet;
        yard_model_groups(arc);
        yard_app_groups();
        yard_command_line_group();
    }
    SDL_Quit();
    std::printf("\nyard: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

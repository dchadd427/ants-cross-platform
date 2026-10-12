// HUD layout tests: the real HUD drives a recording IRenderer, and the emitted draw calls are checked against the
// coordinates and rules of the original (Ants.exe) - no pixels, no SDL video needed.
#include "hud_layout_fixtures.hpp"

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    test_clock_digits(arc);
    test_score_digits(arc);
    test_score_boxes(arc);
    test_home_panel(arc);
    test_ally_pedestal(arc);
    test_rubber_band(arc);
    test_screens(arc);
    test_room_screen(arc);
    test_leader_screen(arc);
    test_held_keys_on_original_screens();
    test_button_zones(arc);
    test_latency_corner(arc);
    test_net_overlay();
    test_more_button_zones(arc);
    test_pedestal_chains(arc);
    test_pedestal_timeline(arc);
    test_minimap(arc);
    test_minimap_dots(arc);
    test_chat_log(arc);
    test_static_shell(arc);
    test_cursor_rules(arc);
    test_button_states(arc);
    test_text_sizes(arc);
    test_results_screen(arc);
    test_label_wrap();
    test_alliance_dialog_layout(arc);
    test_alliance_answers(arc);
    std::printf("\nhud layout: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

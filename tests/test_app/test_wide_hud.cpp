// The wide match screen (milestone M3 of the widescreen work): the 16:9 picture of 960 x 540 with its frame grown from the original's own art, and everything that follows from it.
//   * the anchoring model (include/ants_app/shell_layout.hpp): each of the 14 pieces of the original's frame animation `uishell` at 960 x 540 (and at other sizes): where it is, how big it
//     is, which of them are stretched, and that the spans of a piece tile it without a gap or an overlap;
//   * the cuts: for every stretched piece the repeated line lies inside a run of identical lines of the piece's own plain part (measured on the art of ants.chd: the top bar's columns
//     138 - 143, the bottom strip's three cuts in 13 - 17, 186 - 190 and 345 - 348, each identical to its neighbours on all 19 rows, one in each gap between the recesses of the score boxes, and never the panel's own fill 458 - 482, the left strip's rows 294 - 314) and, for the right panel, the three pieces that span the
//     mock-up's row (canvas y = 357) are plain there between their ant decorations; a cut where the art is not plain fails the same tests;
//   * the picture: the frame composed from the spans is, pixel for pixel, the mock-up's semantics (one line repeated) written out independently, at several sizes, and with the
//     original's picture the plain 14 pieces; the real renderer draws the spans exactly (draw_sprite_region, set_origin);
//   * the HUD: it draws the frame first, offsets the panel's animations by the layout, puts the options window and the quick help of a match over the middle of the map view (the frame
//     stays around them) like its dialogs, and takes the pointer back to the numbers of those windows in the events AND in the 50 ms poll of HUD::update (every control is clicked
//     through it); the score boxes are slots (no 4 in the layout code), spread over the strip by its three cuts (columns 15, 188, 346: dx in thirds); the readout of a network match stays
//     clear of them (the match's limit is the layout's);
//   * the view: the camera clamps (a big map, a map smaller than the view: centred, black around), the edge strips of the whole picture, the start view, the minimap's frame, the cursor
//     outside a small map (a click there does nothing at all), nothing drawn onto the black around a small map (ants at the edges, levels cut from TINY.LVL), the listener;
//   * the application: the picture of a match is the whole canvas and so is every other screen (each has its own wide page: test_wide_pages), the pointer stays where it is; only a picture
//     that is smaller than the canvas (a match of the original's layout, a test hook) holds a pointer that was beside it at its nearest edge pixel; the default is 16:9 on a desktop
//     (`--aspect 4:3` and the settings key give the classic picture).
// Usage: test_wide_hud. Exit code 0 when every check passes.
#include "wide_hud_fixtures.hpp"

// @@MORE_TESTS_4@@


int main(int argc, char* argv[]) {
    (void)argc; (void)argv;                                     // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    ensure_sdl();
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    test_anchoring(arc);
    test_cuts(arc);
    test_composition(arc);
    test_renderer_draws_spans(arc);
    test_hud_draws_the_frame(arc);
    test_score_slots(arc);
    test_dialogs_and_pages(arc);
    test_window_controls_through_update(arc);
    test_latency_clear_of_scores(arc);
    test_camera_clamps();
    test_edge_strips();
    test_start_view_and_minimap(arc);
    test_small_map_pointer(arc);
    test_small_map_pointer_rows(arc);
    test_small_map_clip(arc);
    test_picture_per_screen();
    test_every_shape_screens();
    test_small_levels_in_the_application();
    test_margin_and_pages_in_match();
    test_options_dim(arc);
    test_default_and_options();
    test_pointer_edges_in_match();

    std::printf("\nwide hud: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

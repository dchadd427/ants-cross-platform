// The zoom of the map view in the renderer, the HUD and the application (milestone M4 of the widescreen work; many levels since the batch after v0.2.0). The model (the levels, the limit, anchoring,
// the camera, the edge scroll in screen pixels, the plan of the world pass) is test_zoom_model; this program is what is drawn and what is done with it:
//   * RENDERER: the world pass at a zoom is checked against the direct pass, which draws what it always drew. At the zoom 1 the offscreen target (forced through a test hook) gives the
//     direct picture pixel for pixel; at 2 the view is the nearest-neighbour 2 x 2 enlargement of the direct picture of the same origin (and, from a half-pixel origin, the same shifted by
//     one screen pixel); at 0.5 it is the 2 x 2 average of the direct picture of a view twice as large (0.25: the 4 x 4 average). Group "levels": EVERY level of a map (the series 2^(k/4)
//     from 2 to the map's limit) against the picture that the design specifies, built from the zoom 1 picture of the whole map: the exact levels pixel for pixel, the others (between 1 and 2 nearest, below 1 as the smooth
//     picture (the software renderer is a pixel off at most), never a gap in the view, a scroll that only moves the picture, frames of the levels in any order. Group "place": where the
//     software renderer puts the picture of a level (the smallest whole-pixel rectangle that holds the exact one), and that the pass paints the margins around a small map itself. Group "fog":
//     with Fog of War on every level draws what the zoom 1 draws (the fog over every unexplored tile, nothing that it hides), at every level of GAUNTLET and TINY. Ants of
//     every type, effects, score bubbles, selection markers, the click marker, the fog of war, the hill's brackets, and the small maps (centred, black around them) all pass through it. The
//     hit point digits are one size at every zoom; nothing is drawn outside the view.
//   * HUD: the cursor, the clicks and orders (also at the first and the last pixel of the view), the rubber band, the minimap and the edge scroll follow the zoom, each against an
//     independent computation; a dialog, a captured press and the panels do not zoom.
//   * APPLICATION: the wheel (up zooms in, down out, one level a notch, natural scrolling, the precise deltas, over the map only, not during a drag or with a dialog open), the middle
//     button, the anchoring at the pointer, the same levels in a local game and in a match of the network (no limit of the kind of match), the settings key, --zoom, the start view and the
//     sound listener at every zoom, a network match in which the two machines have different zooms and stay identical, and the setup screen after a zoomed match (the map preview is the
//     zoom 1 preview, the wheel does nothing there).
// Usage: test_zoom_view. Exit code 0 when every check passes.
#include "zoom_view_fixtures.hpp"

int main(int argc, char* argv[]) {
    // (SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one)
    std::string only;                                           // --only NAME: run the test NAME alone (pass, out, levels, place, fog, state, cursor, orders, band, scroll, radar, gating, ctrln, wheel, middle, api, ants, setup, fair, settings, start, client, net, guard, garbage)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
        } else {
            std::fprintf(stderr, "usage: test_zoom_view [--only NAME]\n");
            return 2;
        }
    }
    const auto run = [&only](const char* name) { return only.empty() || only == name; };
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // (the environment survives SDL_Quit, which the end of an application calls; a hint does not): nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    if (run("pass")) test_world_pass(arc);
    if (run("out")) test_zoom_out_pass(arc);
    if (run("levels")) test_level_pictures(arc);
    if (run("place")) test_lattice_placement(arc);
    if (run("fog")) test_fog_levels(arc);
    if (run("centre")) test_click_centre(arc);
    if (run("state")) test_pass_state(arc);
    if (run("cursor")) test_hud_cursor(arc);
    if (run("orders")) test_hud_orders(arc);
    if (run("band")) test_hud_rubber_band(arc);
    if (run("scroll")) test_hud_minimap_and_scroll(arc);
    if (run("radar")) test_hud_radar_frame(arc);
    if (run("gating")) test_hud_gating(arc);
    if (run("ctrln")) test_hud_ctrl_n(arc);
    if (run("wheel")) test_app_wheel();
    if (run("middle")) test_app_middle_button();
    if (run("api")) test_app_zoom_api();
    if (run("ants")) test_app_ants();
    if (run("fair")) test_app_fairness_local();
    if (run("setup")) test_app_setup_screen();
    if (run("settings")) test_app_settings();
    if (run("start")) test_app_start_view_and_listener();
    if (run("client")) test_local_determinism(arc);
    if (run("net")) test_network_match();
    if (run("guard")) test_app_guards();
    if (run("garbage")) test_app_wheel_garbage();
    std::printf("\nzoom view: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// View fingerprint: the safety net of the widescreen / zoom / touch work (milestone M0). It pins what the game DRAWS and what the pointer DOES at the classic
// 640 x 480 picture, as a few hundred 64-bit numbers, so that the later milestones (a parametric screen layout, a wide default picture, a wider HUD, zoom, touch)
// can prove that the classic picture stays exactly what it is. M0 itself changes no production code and no other test.
//
// WHAT IS FINGERPRINTED
//   1. Draw calls (names "hud.*", "screen.*"): HUD::render for 97 scenes (every local colour, the start dialog, selections of every ant type, the hill, the chat with
//      entries and typing, the minimap with all four teams, at every corner of the map and on maps of other sizes, fog of war, allied / dropped / absent score boxes,
//      a crowd, the quit / options / quick-help / alliance dialogs, hovered and pressed buttons, the rubber band) and the setup, network room, guest and results
//      screens. A recording IRenderer takes EVERY mutating call with ALL its parameters in final screen coordinates (sprite ids, named sprites, rectangles,
//      frames, fills, texts with position, size and colour, the HUD team, clips, the image bytes of the minimap); the fingerprint is FNV-1a 64 over a canonical
//      little-endian serialisation (no pointers, no padding, no floats, no locale) plus the number of calls.
//   2. The pointer (names "ptr.*", "view.camera.*"): for EVERY pixel of the 640 x 480 screen the step of the edge scroll (the 60 x 60 map at the default rate from a
//      camera away from the borders and at each of the eight borders and corners; other rates and map sizes; a ring outside the screen), the cursor that
//      HUD::evaluate_cursor chooses (34 selection states and cameras: idle, own ants, an enemy under inspection, hills, latched pedestals, fog, a dialog, a rubber band; every
//      cursor of the original is met), the minimap's hit test, world point and scroll, the playfield / chat classification, the start view, the camera's limits and its two
//      conversions, what a press and a release do (which control reacts, what is selected, which command is issued, what is heard; every zone found to the exact pixel) in 17 HUD states
//      and on the dialogs, the setup and results screens, and the application's own pointer gate.
//   3. Pixels (names "px.*"): what the real software renderer puts on the 640 x 480 canvas: Renderer::render_world at the corners and edges of two maps and at
//      the middle of the others (ants of every type, colour and facing, effects, score bubbles, fog, selection markers, hit point digits, a crowd, all straddling the
//      borders of the playfield), the single-frame cursors at every edge of the screen, and whole frames of an Application (the loading screen, the setup screen, the
//      quick help, the match screen at the corners of the map). TrueType text is never part of a pixel hash (its pixels depend on the machine's font library): the
//      boxes where the classic picture has text and the frame-rate plate are masked, with fixed rectangles. The numbers are the same on every machine (checked on
//      macOS / clang and on Linux / gcc).
//   4. A self-check: the hash of a recorded frame changes when one parameter of one recorded call changes, when two calls are swapped, when one call is dropped
//      (47 parameters), a masked pixel does not change a pixel hash and any other one does, and the pointer hashes move with the rate, the camera and a rectangle.
// The scenes are built by hand (a plain sim::WorldState, a sim::Grid, engine worlds made with the test hooks) from fixed formulas; nothing here reads the wall
// clock (the HUD's clock is injected, the renderer's animation clock is pinned), a random device, a file other than ants.chd and the original maps, or the
// network. The program runs in about two seconds of CPU time.
//
//   5. The wide match screen (milestone M3; names "*.wide.*", 226 more fingerprints with golden numbers made at the commit that introduced it, the classic ones above did not move):
//      the same families for the 16:9 picture of 960 x 540: HUD::render draw calls of 82 scenes (the frame in 14 pieces and 18 parts, the panel moved by (320, 0) and (320, 60), the
//      score slots of 2, 3, 4 teams, the pages of the original centred over the clay and the dialogs over the middle of the map view as origins, maps smaller than the view), the pointer
//      at every pixel of the 960 x 540 picture (edge strips from nine cameras and on small maps, the zones, the minimap, the cursor in 13 states, refined click sweeps), the camera of
//      the 762 x 500 view (a map smaller than the view is centred) and the pixels of the real renderer on the 960 x 540 canvas (six maps, nine cameras of two of them, two synthetic small
//      maps, and whole Application frames: the setup screen, the quick help and the results, each composed for the whole canvas (their wide pages: the quick help and the results were centred pages of the original until then), the match screen and its windows).
//
// WHAT IS NOT COVERED (the blind spots; the places that hard-code 640 / 480 / 442 / 440 / the margins with a covered / not covered note)
//   - the glyph pixels of TrueType text (the CALLS that draw it are fingerprinted: string, place, size, colour; the text boxes are masked in the pixel hashes);
//   - the frame-rate counter, its sparkline and the version text (Application::render_frame: their width depends on the font and the version changes every release);
//   - the network overlay (Application::render_net_overlay: it needs a network match and sockets);
//   - the four animated cursors (Move, Target, Attack, Food: Renderer::render_software_cursor follows the wall clock);
//   - the pedestal glow of the HUD (it follows SDL_GetTicks) and the tile grid overlay (a debug aid with TrueType text);
//   - how SDL scales the 640 x 480 canvas into a window (every hash is of the logical canvas read back at scale 1: fractional scales, bars in a full screen), the
//     save_screenshot output, sound and music, the web build.
//
// USAGE
//   test_view_fingerprint               runs everything and compares with the golden numbers below; a difference is reported as
//                                       "FAIL [scenario]: expected hash 0x... / N, actual hash 0x... / N" (N = draw calls, or samples for a sweep)
//   test_view_fingerprint --print       prints the golden table for regeneration. USE IT DELIBERATELY AND ONLY WHEN THE PICTURE (OR THE POINTER) IS MEANT TO
//                                       CHANGE (a milestone that changes the classic picture on purpose); NEVER TO SILENCE A FAILURE: a failure that you did not
//                                       cause is a regression of the classic 640 x 480 view and is to be fixed in the code, not in this file.
//   test_view_fingerprint --list        lists the scenarios
//   test_view_fingerprint --dump NAME   prints the recorded draw calls of a "hud.*" or "screen.*" scenario (to see what moved when its hash changed)
//   test_view_fingerprint --save DIR    writes the canvas of every "px.*" scenario as DIR/<name>.bmp (masked pixels magenta), to look at what a pixel hash is about
//   test_view_fingerprint --only PREFIX runs only the scenarios whose name starts with PREFIX (a developer's shortcut: the comparison for unused golden numbers is
//                                       skipped)
#include "view_fingerprint_fixtures.hpp"

namespace {

// =====================================================================================================================================================
// The golden numbers, computed at the commit of v0.0.88 (the classic 640 x 480 view). Regenerate ONLY deliberately: see the notes at the top of the file.
// =====================================================================================================================================================


// BEGIN GOLDEN TABLE
const Golden kGoldens[] = {
    {"hud.start.modal.p0", 0xc468a06a43ee26b0, 106},
    {"hud.start.modal.p1", 0x98e01a098d348967, 106},
    {"hud.start.modal.p2", 0x2f3e22b7fbf391bb, 106},
    {"hud.start.modal.p3", 0x79672a6a0f5c9a13, 106},
    {"hud.start.modal.1500ms", 0x71900accd71f78d9, 106},
    {"hud.start.modal.closed_by_timer", 0xfaf34595df510ce8, 79},
    {"hud.start.idle.p0", 0xaa58258cae06214c, 80},
    {"hud.start.idle.p1", 0x8dc6824bac18596a, 80},
    {"hud.start.idle.p2", 0x4e9c2b45813aeee2, 80},
    {"hud.start.idle.p3", 0xf3b3b3fa9e40f1f4, 80},
    {"hud.start.idle.dismissed", 0xaa58258cae06214c, 80},
    {"hud.clock.720000", 0xc7ce095780e7460b, 81},
    {"hud.clock.659000", 0x1fea9a9d9aa0282d, 81},
    {"hud.clock.61000", 0x398b96d597d5daca, 80},
    {"hud.clock.0", 0xddf27e8120defc20, 80},
    {"hud.sel.one.worker", 0x2381b6115afb2eae, 85},
    {"hud.sel.one.worker2", 0x2381b6115afb2eae, 85},
    {"hud.sel.one.bomber", 0x2a8dd8d8f7ea0aaa, 88},
    {"hud.sel.one.fire", 0x34837a980c65d4bd, 88},
    {"hud.sel.one.thief", 0x7bae27f834c87a3d, 88},
    {"hud.sel.one.combat", 0xe23e10fde1f6c280, 88},
    {"hud.sel.one.swimmer", 0x1bcfa9564814fa3b, 88},
    {"hud.sel.group", 0x4190e311b8d7b4b2, 85},
    {"hud.sel.group.carrying", 0xdff900c793c5d541, 86},
    {"hud.sel.enemy", 0xfaf34595df510ce8, 79},
    {"hud.sel.hill.own", 0x9033f768ea584d71, 93},
    {"hud.sel.hill.own.noeggs", 0x3afc3aca262c9d99, 81},
    {"hud.sel.hill.own.3eggs", 0xf2783dae07681798, 87},
    {"hud.sel.hill.enemy", 0x291219265740ff75, 82},
    {"hud.sel.one.p2", 0xccb6bb8fe4d8825c, 85},
    {"hud.pedestal.rising.100", 0x8ad26dce211191dd, 84},
    {"hud.pedestal.rising.250", 0x5295943b84e18c43, 83},
    {"hud.pedestal.rising.450", 0xf2ca0144839f8613, 85},
    {"hud.pedestal.rising.700", 0x2a8dd8d8f7ea0aaa, 88},
    {"hud.pedestal.rising.1200", 0x2a8dd8d8f7ea0aaa, 88},
    {"hud.chat.typing.caret_on", 0x4ee0cd89581d50ee, 88},
    {"hud.chat.typing.caret_off", 0xdb9ae1c45e60df34, 87},
    {"hud.chat.typing.long", 0xc3d816005e83e7b5, 81},
    {"hud.chat.scrolled", 0xbd5ed8436043ba9f, 89},
    {"hud.chat.team.buttons", 0x2cfcd57d89d234c4, 90},
    {"hud.chat.team.hover_all", 0x6ff996639ae4f193, 91},
    {"hud.chat.team.hover_team", 0x93c383ee3eb4e72a, 91},
    {"hud.chat.team.pressed_team", 0x0cdaa5486e023efe, 90},
    {"hud.chat.off", 0x35d52cf4800acfa1, 88},
    {"hud.minimap.teams", 0xaa58258cae06214c, 80},
    {"hud.minimap.frame.tl", 0xaa58258cae06214c, 80},
    {"hud.minimap.frame.tr", 0xaa4064c2fca28942, 80},
    {"hud.minimap.frame.bl", 0xbda799e3d9165f3a, 80},
    {"hud.minimap.frame.br", 0x9ae8f3a41ffd0284, 80},
    {"hud.minimap.frame.top", 0xef047400777a4532, 80},
    {"hud.minimap.frame.left", 0xd1dab31d39bd4e81, 80},
    {"hud.minimap.frame.mid", 0x041feb51be38a653, 80},
    {"hud.minimap.frame.beyond", 0x9ae8f3a41ffd0284, 80},
    {"hud.minimap.size.31x31", 0x413daec1d5321929, 80},
    {"hud.minimap.size.40x40", 0xf04fae819de9a865, 80},
    {"hud.minimap.size.14x14", 0xf0463ec0ec6324c2, 80},
    {"hud.minimap.size.80x50", 0xc59c1a49ceff9126, 80},
    {"hud.minimap.no_world", 0x8cc7b48707a931ec, 44},
    {"hud.fog.on", 0xbce789aa3cc6de13, 75},
    {"hud.scores.allied", 0xc4546ba30c7a3075, 83},
    {"hud.scores.allied.local_p2", 0xe019dec48388ea17, 83},
    {"hud.scores.allied.bottom_pair", 0x0423f0be9ddc30bc, 81},
    {"hud.scores.dropped", 0x876befe3137b386f, 78},
    {"hud.scores.absent", 0x5b81f3deae29e617, 77},
    {"hud.scores.two_players", 0x58c11df59bf8000d, 73},
    {"hud.scores.big", 0xe548db61f117131f, 83},
    {"hud.scores.names", 0x8f1b2c621e9951fc, 80},
    {"hud.antheavy", 0x318a4da814fd948b, 306},
    {"hud.quit.open", 0xb223ea350257db2f, 104},
    {"hud.quit.hover_yes", 0x3ab0dbbea79e786c, 104},
    {"hud.quit.pressed_no", 0x954d9d4e786333c1, 104},
    {"hud.options.open", 0x90929d9ea096b478, 304},
    {"hud.options.open.caret_off", 0x939680e725ee1154, 302},
    {"hud.options.changed", 0xd3d8ed43e3379edf, 305},
    {"hud.options.hover_return", 0x6b229c8602572af0, 305},
    {"hud.quickhelp.open", 0x9e37c41fa167033e, 114},
    {"hud.quickhelp.hover_return", 0x1154ad49a219c1fe, 115},
    {"hud.quickhelp.pressed_return", 0x7f1a8cd98154a559, 115},
    {"hud.dialog.invitation", 0x6d8ed2d84c285fee, 104},
    {"hud.dialog.invitation.hover_accept", 0x1b8abd43b3a57051, 104},
    {"hud.dialog.invitation.pressed_accept", 0x36f28bbd520e5db3, 104},
    {"hud.dialog.waiting", 0x2389d97621743512, 103},
    {"hud.dialog.breakconfirm", 0x7b6ac39fa22cf0bf, 104},
    {"hud.dialog.invitation.replaces_team", 0x657eb98e85cf1da3, 109},
    {"hud.topbar.hover_help", 0xbc0af5b7b52fea22, 82},
    {"hud.topbar.pressed_help", 0x7c36bcafa7bab12c, 81},
    {"hud.topbar.hover_options", 0x1f83ee6dea43bdcb, 82},
    {"hud.topbar.pressed_options", 0x090c0e6a8256f48f, 81},
    {"hud.topbar.hover_quit", 0xd2130c4d274dae7f, 82},
    {"hud.topbar.pressed_quit", 0xd3786463033e6b60, 81},
    {"hud.marquee.band", 0x83739c9b7d5c0eb4, 81},
    {"hud.marquee.dot", 0xf865c13ac053b5d6, 81},
    {"hud.marquee.clamped", 0xca5cc055f52bd08b, 81},
    {"hud.marquee.clamped_low", 0xf0c460abb382fdb4, 81},
    {"hud.status.flash.odd", 0xfaf34595df510ce8, 79},
    {"hud.status.flash.even", 0x1df336887628e5c6, 80},
    {"hud.status.long", 0xf9288f3ca266edd1, 80},
    {"screen.setup.before_refresh", 0x42e853aa8cdf3bf2, 135},
    {"screen.setup.default", 0xd4c2c2429fc06d95, 144},
    {"screen.setup.map0", 0x0ee6396e1b92b719, 144},
    {"screen.setup.map1", 0x2fd274d55b8c75b5, 144},
    {"screen.setup.map2", 0x1867fd88bdc42ec6, 144},
    {"screen.setup.map3", 0xeb3ac3dcf744b223, 144},
    {"screen.setup.map4", 0xa8e6b148ca2bd5e5, 144},
    {"screen.setup.map5", 0xd4c2c2429fc06d95, 144},
    {"screen.setup.fog_on", 0xb18ae62e5d7400b2, 144},
    {"screen.setup.hover_start", 0xe7dbf65917ac99f5, 144},
    {"screen.setup.pressed_start", 0x4d61f01332d97a95, 144},
    {"screen.setup.pressed_up", 0x2b78b15b3a13fcb5, 144},
    {"screen.setup.pressed_down", 0x7e2b7c2bcc68a8ed, 144},
    {"screen.setup.hover_leave", 0x7e7207f8142158e3, 144},
    {"screen.setup.locked", 0x7e7207f8142158e3, 144},
    {"screen.setup.team2_alice", 0x72e3b1d43258f4f7, 144},
    {"screen.setup.notice", 0x16053631afaeda43, 143},
    {"screen.room.host", 0x3c134aa968ce9108, 154},
    {"screen.room.host.thumb_good", 0x9d3bc23adab0a6df, 154},
    {"screen.room.host.thumb_ok", 0x3c134aa968ce9108, 154},
    {"screen.room.host.thumb_bad", 0x8f6c60cde6e12f55, 154},
    {"screen.room.host.thumb_unknown", 0x948ec4837938f846, 154},
    {"screen.room.host.four_players", 0xb80130a9e035a803, 158},
    {"screen.room.guest", 0x52301f71cb4cb15a, 146},
    {"screen.room.guest.fog_off", 0xef9c44da2487110a, 145},
    {"screen.room.guest.hover_leave", 0xa31f3e746517183c, 145},
    {"screen.room.guest.connection_lost", 0x0137bf09dbd127d6, 129},
    {"screen.room.guest.server_seat3", 0x46f213e744fbf523, 144},
    {"screen.wide.room.leader_empty", 0xb1d15745992ccdf1, 241},
    {"screen.wide.room.leader_lines", 0xa55867b74f9e2d48, 246},
    {"screen.wide.room.leader_typed_caret", 0x0973048a72fcf279, 248},
    {"screen.wide.room.leader_typed", 0xa34b68aaff170ca9, 247},
    {"screen.wide.room.guest_empty", 0x233a911b5a9ce3ed, 236},
    {"screen.wide.room.guest_typed_caret", 0x135fde3e34ea4d5d, 243},
    {"screen.room.before_refresh", 0x42e853aa8cdf3bf2, 135},
    {"screen.results.2teams.waiting", 0xae029099741ec334, 150},
    {"screen.results.2teams.rows", 0x6f89094de148c8c7, 182},
    {"screen.results.2teams.rows.later", 0xd007fb5670a1dc07, 182},
    {"screen.results.2teams.hover_leave", 0x32657987395aa25d, 182},
    {"screen.results.2teams.pressed_leave", 0xe4d9222bdb8e2d79, 182},
    {"screen.results.4teams.waiting", 0xae029099741ec334, 150},
    {"screen.results.4teams.rows", 0xa7ab576d931964ce, 201},
    {"screen.results.4teams.rows.local3", 0x7f2583f638f44226, 201},
    {"screen.results.4teams.quitter", 0x3042da4471779bf7, 214},
    {"screen.results.3teams.gap", 0xdc787da4ea6328d6, 198},
    {"ptr.edge.60x60.rate50.cam_tl", 0xf3b9a29a8192b052, 307200},
    {"ptr.edge.60x60.rate50.cam_t", 0x192ef46972c41e4a, 307200},
    {"ptr.edge.60x60.rate50.cam_tr", 0xa65f5dc2d2fb8e95, 307200},
    {"ptr.edge.60x60.rate50.cam_l", 0x44d95cd06c149201, 307200},
    {"ptr.edge.60x60.rate50.cam_mid", 0xb5aa479aa4b1eabd, 307200},
    {"ptr.edge.60x60.rate50.cam_r", 0x08f03e660ee22d99, 307200},
    {"ptr.edge.60x60.rate50.cam_bl", 0x493ae82616f8a316, 307200},
    {"ptr.edge.60x60.rate50.cam_b", 0x9b603d85dacfc0e2, 307200},
    {"ptr.edge.60x60.rate50.cam_br", 0x9a03779e9dc092d9, 307200},
    {"ptr.edge.60x60.rate0.all9", 0x28108b8a28bac4cd, 2764800},
    {"ptr.edge.60x60.rate99.all9", 0x70b85ef997a256dd, 2764800},
    {"ptr.edge.31x31.rate50.all9", 0x99d56a4a8c614c05, 2764800},
    {"ptr.edge.40x40.rate25.all9", 0xe86054b36acdae85, 2764800},
    {"ptr.edge.14x14.rate50.all9", 0xb8d7190b9a70d96d, 2764800},
    {"ptr.edge.80x50.rate75.all9", 0x732df304a733c98d, 2764800},
    {"ptr.edge.13x9.rate50.all9", 0xadb1227d3b21e325, 2764800},
    {"ptr.edge.60x60.rate50.outside_ring.mid", 0x388fc3bb96c96325, 36864},
    {"ptr.minimap.rect", 0x0b25e95b5dba108a, 307200},
    {"ptr.class.map_rect", 0x5118168d40602715, 307200},
    {"ptr.class.chat_view", 0x2c1e5ef1c71f181f, 307200},
    {"ptr.minimap.point.60x60", 0x7a4d020bb3b135c5, 307200},
    {"ptr.minimap.point.31x31", 0x70fb2306ad804525, 307200},
    {"ptr.minimap.point.40x40", 0x8a89cb2ff8b99071, 307200},
    {"ptr.minimap.point.14x14", 0x64d8598e20fc521d, 307200},
    {"ptr.minimap.point.80x50", 0x0c9d6d8b3686338d, 307200},
    {"ptr.minimap.scroll.60x60.all9", 0xf6cba4d215c4062d, 2764800},
    {"ptr.minimap.scroll.31x31.all9", 0x07f20a65afef290d, 2764800},
    {"ptr.minimap.scroll.14x14.all9", 0xb6e874de1d5dfbc5, 2764800},
    {"ptr.start_view.origins", 0x4838c15f5514941b, 10357},
    {"view.camera.clamp", 0x8bfb67b5df86cb33, 1445},
    {"view.camera.center_on", 0xd12c987530a61d84, 7178},
    {"view.camera.scroll_pixels", 0xff501334e20df5c2, 2000},
    {"view.camera.screen_to_world.origin", 0xa9f0c776211d3e2d, 307200},
    {"view.camera.world_to_screen.origin", 0x3eb17232d096191c, 4489},
    {"view.camera.screen_to_world.mid", 0x02626afcce7b4a75, 307200},
    {"view.camera.world_to_screen.mid", 0x3eb17232d096191c, 4489},
    {"view.camera.screen_to_world.far", 0xb57ce937e9d86e0d, 307200},
    {"view.camera.world_to_screen.far", 0x3eb17232d096191c, 4489},
    {"ptr.cursor.idle.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.own_worker.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.own_group.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.own_bomber.cam_mid400", 0xf41b03be4b263c45, 307200},
    {"ptr.cursor.own_thief.cam_mid400", 0x768356841f8c0445, 307200},
    {"ptr.cursor.own_combat.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.bomber_latched.cam_mid400", 0x8fa1a6f79882ad95, 307200},
    {"ptr.cursor.worker_move_latched.cam_mid400", 0x75cbe31e34598845, 307200},
    {"ptr.cursor.enemy_inspected.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.hill_own.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.hill_enemy.cam_mid400", 0x6040d676d0686b85, 307200},
    {"ptr.cursor.fog_idle.cam_mid400", 0x0855934140435385, 307200},
    {"ptr.cursor.fog_own_worker.cam_mid400", 0xe5b3fc8eb7ac9045, 307200},
    {"ptr.cursor.options_open.cam_mid400", 0x156ed4086987e325, 307200},
    {"ptr.cursor.band_dragging.cam_mid400", 0x156ed4086987e325, 307200},
    {"ptr.cursor.button_captured.cam_mid400", 0xce34ac41c08e0a85, 307200},
    {"ptr.cursor.idle.cam_tl", 0x65353ee3cf7ff411, 307200},
    {"ptr.cursor.idle.cam_t", 0xeb2f8aad825491e5, 307200},
    {"ptr.cursor.idle.cam_tr", 0x2fc636444d3fc3e5, 307200},
    {"ptr.cursor.idle.cam_l", 0x811930464ec06c45, 307200},
    {"ptr.cursor.idle.cam_mid", 0x5b89762dd00f7635, 307200},
    {"ptr.cursor.idle.cam_r", 0xcc8ed382e5663a45, 307200},
    {"ptr.cursor.idle.cam_bl", 0x82f3de948bb990a5, 307200},
    {"ptr.cursor.idle.cam_b", 0xb051a60fe68ad485, 307200},
    {"ptr.cursor.idle.cam_br", 0xb72d2f0a94d178e5, 307200},
    {"ptr.cursor.own_worker.cam_tl", 0xb466e978b0b5d681, 307200},
    {"ptr.cursor.own_worker.cam_t", 0x70f5b611f99c4d85, 307200},
    {"ptr.cursor.own_worker.cam_tr", 0xae6ee6a13fc06125, 307200},
    {"ptr.cursor.own_worker.cam_l", 0xf42cd6ca26c00985, 307200},
    {"ptr.cursor.own_worker.cam_mid", 0x94d40074049a40a5, 307200},
    {"ptr.cursor.own_worker.cam_r", 0x4b3783dfd7e6d785, 307200},
    {"ptr.cursor.own_worker.cam_bl", 0xf607851863b92de5, 307200},
    {"ptr.cursor.own_worker.cam_b", 0x8c72fa0482876b45, 307200},
    {"ptr.cursor.own_worker.cam_br", 0x35d5df6787521625, 307200},
    {"ptr.click.idle", 0x25e9631b11c72826, 41520},
    {"ptr.click.own_worker", 0x87ac9df1afb78f91, 55278},
    {"ptr.click.own_bomber", 0x84bb4fa3ddf53841, 55632},
    {"ptr.click.own_group", 0xe749c6285810c707, 55278},
    {"ptr.click.hill_own", 0xdea7ba8677946edf, 42006},
    {"ptr.click.hill_enemy", 0xa96821ebf1d04023, 41520},
    {"ptr.click.hill_enemy.ally_break", 0xe6ec16981d803e7d, 41874},
    {"ptr.click.hill_enemy.ally_offer", 0xd061cb77a8cdc43e, 41769},
    {"ptr.click.own_worker.right_button", 0x69a53e523e1501f4, 54291},
    {"ptr.click.own_bomber.right_button", 0x6396ebd7277a76e1, 54291},
    {"ptr.click.dialog.quit", 0x34aa0b29422c725a, 19644},
    {"ptr.click.dialog.quickhelp", 0x469e152ae536aee9, 19542},
    {"ptr.click.dialog.options", 0x953dcab625cd97a3, 25938},
    {"ptr.click.dialog.start_modal", 0x749e3efaba777625, 19200},
    {"ptr.click.dialog.invitation", 0xb9a52bf986228688, 19812},
    {"ptr.click.dialog.waiting", 0x56aa60a19a74acde, 19506},
    {"ptr.click.dialog.breakconfirm", 0xb95b9ad2953a85e4, 19644},
    {"ptr.screen.setup.local", 0x5c569fa1d2ea11d1, 20832},
    {"ptr.screen.setup.room_host", 0x858be58b30149cb6, 20832},
    {"ptr.screen.setup.room_guest", 0x48104980ac71541a, 19695},
    {"ptr.screen.results.waiting", 0x0d445389d7deca25, 19200},
    {"ptr.screen.results.rows", 0x215c4766e9f43c0a, 19695},
    {"px.world.MEDIUM.cam_tl", 0xfe286b53a530b787, 307200},
    {"px.world.MEDIUM.cam_t", 0x0f99ee9d6bd0c995, 307200},
    {"px.world.MEDIUM.cam_tr", 0x56b872ced0415aac, 307200},
    {"px.world.MEDIUM.cam_l", 0xd816170f3fa31b2e, 307200},
    {"px.world.MEDIUM.cam_mid", 0x6091d0ded2e718e8, 307200},
    {"px.world.MEDIUM.cam_r", 0x90b4a69baf901a2a, 307200},
    {"px.world.MEDIUM.cam_bl", 0x69746ce56092c13d, 307200},
    {"px.world.MEDIUM.cam_b", 0x815d61b64e050ce4, 307200},
    {"px.world.MEDIUM.cam_br", 0x9e40e11b2b5a6186, 307200},
    {"px.world.TINY.cam_tl", 0xbb382443938e689c, 307200},
    {"px.world.TINY.cam_t", 0x7b3c2a84a9d0c12d, 307200},
    {"px.world.TINY.cam_tr", 0x3cae5c2eb8fa9403, 307200},
    {"px.world.TINY.cam_l", 0xb604a9335535e7cd, 307200},
    {"px.world.TINY.cam_mid", 0x1aa74bff656f404e, 307200},
    {"px.world.TINY.cam_r", 0x8cccf1922c3a51d2, 307200},
    {"px.world.TINY.cam_bl", 0x21fbe3989f32daa2, 307200},
    {"px.world.TINY.cam_b", 0x3c734bf5ba16f868, 307200},
    {"px.world.TINY.cam_br", 0x07696b06f903f602, 307200},
    {"px.world.GAUNTLET.cam_tl", 0x844e4036876a8749, 307200},
    {"px.world.GAUNTLET.cam_mid", 0x9bb7cab3d7bfde0a, 307200},
    {"px.world.ISLANDS.cam_tl", 0xde4650ebc03b9d4a, 307200},
    {"px.world.ISLANDS.cam_mid", 0x8b9052d2a8f7fe17, 307200},
    {"px.world.SMALL.cam_tl", 0xca82937de3bbbdb5, 307200},
    {"px.world.SMALL.cam_mid", 0x35f7cd96d48a2f51, 307200},
    {"px.world.TREASURE.cam_tl", 0x50923948b5d19619, 307200},
    {"px.world.TREASURE.cam_mid", 0x2765945e6cba34e7, 307200},
    {"px.world.MEDIUM.origin_1_1", 0xbea317edc6ac0898, 307200},
    {"px.world.MEDIUM.origin_32_64", 0xd5fde999a640df4e, 307200},
    {"px.world.MEDIUM.origin_17_49", 0x265b449a1ea1fe71, 307200},
    {"px.world.MEDIUM.origin_1477_1479", 0x83195aeec913dc75, 307200},
    {"px.world.MEDIUM.origin_733_1", 0xa0360a8a2eba817f, 307200},
    {"px.world.MEDIUM.selected", 0x24d1c6b333ffe89f, 307200},
    {"px.world.MEDIUM.selected_hp", 0x0cc19ed7e1ebe2eb, 307200},
    {"px.world.MEDIUM.hill_selected", 0x43240c98f09c3a36, 307200},
    {"px.world.MEDIUM.crowd", 0x581384e8758e49ec, 307200},
    {"px.world.MEDIUM.fog", 0xe028297bde386974, 307200},
    {"px.world.MEDIUM.fog.corner_tl", 0x14005e3a5876b7d8, 307200},
    {"px.world.MEDIUM.click_marker", 0x14c3a2f828db7240, 307200},
    {"px.cursor.normal", 0xc8d42c5e91a8baf9, 307200},
    {"px.cursor.select", 0x5532c4ca983dceed, 307200},
    {"px.cursor.n", 0x0fb8ca1f83770200, 307200},
    {"px.cursor.ne", 0xebca3c1c95d05372, 307200},
    {"px.cursor.e", 0x01e6a6ca0ec90f90, 307200},
    {"px.cursor.se", 0xbe546741889273c9, 307200},
    {"px.cursor.s", 0x472431c6f70f149d, 307200},
    {"px.cursor.sw", 0xb902630ad3916a6c, 307200},
    {"px.cursor.w", 0x69a84740954105d3, 307200},
    {"px.cursor.nw", 0x05b9ca230fec312e, 307200},
    {"px.app.loading.start", 0x2a225b383fef7593, 307200},
    {"px.app.loading.tick12", 0xb2148702c4ad6bef, 307200},
    {"px.app.loading.tick25", 0x8875b5a2b0827923, 307200},
    {"px.app.loading.cursor", 0xe8897a816f1dd05a, 307200},
    {"px.app.setup.before_refresh", 0x4fcb4343bae50ef3, 307200},
    {"px.app.setup.refreshed", 0x5a8f68f4ab4a40de, 307200},
    {"px.app.setup.hover_start", 0x2b68054ae9dea39b, 307200},
    {"px.app.setup.cursor", 0x1498efbca958ffbc, 307200},
    {"px.app.quickhelp.rest", 0x8b011e0fb7cc3a9b, 307200},
    {"px.app.quickhelp.hover_start", 0x4d5a60c68a1aa0e7, 307200},
    {"px.app.quickhelp.pressed_start", 0x9938fed9d1959abf, 307200},
    {"px.app.quickhelp.cursor", 0xa8d009d12219564d, 307200},
    {"px.app.match.modal", 0x635a5ce25c7d86c6, 307200},
    {"px.app.match.start_view", 0x673ef86b26cd9338, 307200},
    {"px.app.match.cam_tl", 0xecbe581afae0ead5, 307200},
    {"px.app.match.cam_t", 0xbcc22ae1a17c9bf2, 307200},
    {"px.app.match.cam_tr", 0x94da8a0e285b54cf, 307200},
    {"px.app.match.cam_l", 0x3175a8b3493709d9, 307200},
    {"px.app.match.cam_mid", 0x289c495d11f8c6dd, 307200},
    {"px.app.match.cam_r", 0x577a083c9ecd70c1, 307200},
    {"px.app.match.cam_bl", 0x56ec07f221fc712e, 307200},
    {"px.app.match.cam_b", 0xbc40d07adb5e608f, 307200},
    {"px.app.match.cam_br", 0xde277b9a60fb497f, 307200},
    {"px.app.match.quit_dialog", 0xc6a4bf5cd49ed1bd, 307200},
    {"px.app.match.cursor_map", 0x06ce12a18534a0e5, 307200},
    {"px.app.match.cursor_edge", 0x6e50f3c14f5e43be, 307200},
    {"ptr.app.edge_gate.cam_mid", 0x1cd7067e7e69e285, 325376},
    {"ptr.app.edge_gate.cam_tl", 0x715bff47f7920212, 325376},
    {"ptr.app.edge_gate.cam_br", 0xb5c037bc6ed1d351, 325376},
    {"ptr.app.edge_gate.left_window", 0x06fca21281eeb67b, 214},
    // the desktop start menu (remake only; new: the classic screens above are unchanged): every panel, a selection, the bots, the fields, a failure, the room's code
    {"screen.menu.main", 0x3590c780bf8d6ce0, 146},
    {"screen.menu.main.second_selected", 0x7ec2b1c259beacce, 146},
    {"screen.menu.main.quit_selected", 0x647ac943406089ce, 146},
    {"screen.menu.main.notice", 0xe9d5e1ba26ad5f25, 156},
    {"screen.menu.single.empty", 0x227af479f1885934, 218},                // the three Single panel goldens: regenerated for the name field (+ the Teams row of two_bots)
    {"screen.menu.single.two_bots", 0xc7548f9283cdd916, 242},
    {"screen.menu.single.own_seat_2", 0xb2edf0250758b271, 218},
    {"screen.menu.join.empty", 0xa3719b0cf65673ec, 148},
    {"screen.menu.join.typed", 0xee59ae3037c1cc35, 149},
    {"screen.menu.join.caret_off", 0x02f6dbba4e83ab42, 148},
    {"screen.menu.join.name_refused", 0xa4d022926ae89c6f, 160},
    {"screen.menu.connecting", 0xa95f45bb91fe2012, 114},
    {"screen.menu.join.error", 0xdffb5d28277afaa0, 158},
    // The host-panel rows below moved deliberately in bot games part B (network protocol 13: a level for each bot and teams chosen before the start were asked for): the panel has a row for
    // each seat after the leader's (the bot that START puts there) and, with three or four players, the Teams row, in rows of 18 px text, with the caption under them (the line says what the standard bot
    // does: "Bots gather food, raid and fight back."). screen.menu.room.fill_medium did NOT move: the room's panel for the same level in every seat is what it was; the rows with seats_and_teams and
    // three_players are new. The two room panels with teams moved once more in the review fixes of part B, deliberately: the teams are the room's own now (a word of its code in protocol 13, its create
    // block's since 15: the room makes them for every start), so the sentence says "Room teams: Green + Red against Blue + Black." where it said "Teams: ..." (same draw calls, one string longer).
    // Nine of the menu rows moved again in v0.11.0 (protocol 15): join.typed, join.caret_off, join.name_refused, join.error, room.code, room.full, room.fill_medium, room.seats_and_teams and
    // room.three_players. Their scenarios now type and show the codes that the menu makes ("k7m2xq", "b7x2qk", where they had "demo-small-2p-x7k2" and the like), and the room panel shows a
    // code of six characters in two groups of three ("b7x 2qk"; room_code_display). The draw calls are the same (the counts did not move), the string in them is another; a code of another length
    // is shown as it is, so screen.menu.room.longest_code did not move.
    {"screen.menu.host.default", 0x18b4d4169368ccd1, 282},
    {"screen.menu.host.map_and_players", 0x3a08eeb197374920, 210},
    {"screen.menu.room.code", 0x007724f8d78c993d, 150},
    {"screen.menu.room.full", 0x4acdd352530ed1be, 150},
    {"screen.menu.room.longest_code", 0x02e344112faca6ac, 150},
    {"screen.menu.room.fill_medium", 0x1544abeb764d9c2a, 150},
    {"screen.menu.host.fill_medium", 0x3a6b94fd4d05862f, 282},
    {"screen.menu.host.seats_and_teams", 0x4f4c7690d7c20560, 282},
    {"screen.menu.room.seats_and_teams", 0x143c01485ed43d61, 151},
    {"screen.menu.host.three_players", 0x30fe4a8bffb71fdf, 258},
    {"screen.menu.room.three_players", 0x815f9e2122cf3d67, 151},
    // ---- the wide match screen (milestone M3, 960 x 540), computed at the commit that introduced it (the classic numbers above did not move); regenerate only deliberately, see the notes at the top.
    //      The review fixes of M3 moved 105 of these deliberately (all of the wide ones that draw the bottom strip or the options window and quick help, none of the classic): the score boxes are
    //      spread over the strip (seven draw calls for the strip instead of three, the boxes at x 213, 468 and 722), and the in-match options window and quick help are drawn over the map view
    //      with the HUD around them (no clay margin). The 121 that are pointer sweeps, cameras and world pixels did not move.
    {"hud.wide.start.modal.p0", 0x7203784daeef60da, 124},
    {"hud.wide.start.modal.p1", 0xc06d1005ad6f8c3f, 124},
    {"hud.wide.start.modal.p2", 0x907ff3981d20ca29, 124},
    {"hud.wide.start.modal.p3", 0x65031440507c3db2, 124},
    {"hud.wide.start.idle.p0", 0x7a90cfaad34abb92, 96},
    {"hud.wide.start.idle.p1", 0xc8021b2f8d847688, 96},
    {"hud.wide.start.idle.p2", 0xa7bc78f8a9249aec, 96},
    {"hud.wide.start.idle.p3", 0xa7d42ca526dbcfd1, 96},
    {"hud.wide.sel.one.worker", 0x2ce6536fbd8d3126, 101},
    {"hud.wide.sel.one.worker2", 0x2ce6536fbd8d3126, 101},
    {"hud.wide.sel.one.bomber", 0xf8c1bd03046f8ce5, 104},
    {"hud.wide.sel.one.fire", 0xcbe5d8e78fc2e932, 104},
    {"hud.wide.sel.one.thief", 0xec0a842d82e966c2, 104},
    {"hud.wide.sel.one.combat", 0xd1a39124e3195fcf, 104},
    {"hud.wide.sel.one.swimmer", 0xcbfd378073c49020, 104},
    {"hud.wide.sel.group", 0x4db4114addd05802, 101},
    {"hud.wide.sel.group.carrying", 0xb83c0a05cbd84876, 102},
    {"hud.wide.sel.enemy", 0x9a8b862299375224, 95},
    {"hud.wide.sel.hill.own", 0xfc8d102c83550430, 109},
    {"hud.wide.sel.hill.own.3eggs", 0x957049efc2b400ad, 103},
    {"hud.wide.sel.hill.enemy", 0xd2db89c618e4c53f, 98},
    {"hud.wide.sel.one.p2", 0x7970dae5c17d8e60, 101},
    {"hud.wide.pedestal.rising.100", 0xc8198e0c882bedf2, 100},
    {"hud.wide.pedestal.rising.450", 0x0b9cf1a9d9814a3b, 101},
    {"hud.wide.pedestal.rising.1200", 0xf8c1bd03046f8ce5, 104},
    {"hud.wide.chat.typing.caret_on", 0x39cd6ff0c76e0d51, 108},
    {"hud.wide.chat.typing.caret_off", 0xc60dfcd83b6c80a4, 107},
    {"hud.wide.chat.typing.long", 0xf36630de6418e2da, 97},
    {"hud.wide.chat.scrolled", 0x7423e8342968df8e, 109},
    {"hud.wide.chat.team.buttons", 0xfc84812fa8dce6c5, 110},
    {"hud.wide.chat.team.hover_all", 0xea6f1f12c30759fb, 111},
    {"hud.wide.chat.team.pressed_team", 0xea6af213d130546f, 110},
    {"hud.wide.chat.off", 0x0a89c59ae1694f5c, 108},
    {"hud.wide.minimap.teams", 0x7a90cfaad34abb92, 96},
    {"hud.wide.minimap.frame.tl", 0x7a90cfaad34abb92, 96},
    {"hud.wide.minimap.frame.tr", 0xda6ffa9e66b9f4b3, 96},
    {"hud.wide.minimap.frame.bl", 0xf7c9614e61d3192d, 96},
    {"hud.wide.minimap.frame.br", 0x5e6a412e286f0e00, 96},
    {"hud.wide.minimap.frame.top", 0x0bd2dbd53bf79197, 96},
    {"hud.wide.minimap.frame.left", 0x9e41bf081a796cff, 96},
    {"hud.wide.minimap.frame.mid", 0x2c4961de7223944a, 96},
    {"hud.wide.minimap.size.31x31", 0xa2109bf3a703a038, 96},
    {"hud.wide.minimap.size.40x40", 0x20920673c1e358b6, 96},
    {"hud.wide.minimap.size.14x14", 0x0b8a9ca561d3046f, 96},
    {"hud.wide.minimap.size.16x16", 0x64b5bef5c67ef882, 96},
    {"hud.wide.minimap.size.12x12", 0x61fa829aaa1607e0, 96},
    {"hud.wide.minimap.size.80x50", 0xbb8fa408b68b29bf, 96},
    {"hud.wide.minimap.size.13x9", 0xbd01aed8225c47c9, 96},
    {"hud.wide.fog.on", 0x38764f47ca34d524, 91},
    {"hud.wide.scores.allied", 0x72dd6a0a8709e622, 99},
    {"hud.wide.scores.allied.bottom_pair", 0xdbf1945e582315cc, 97},
    {"hud.wide.scores.dropped", 0x48e2c3dd817fb422, 94},
    {"hud.wide.scores.roster.3", 0xae0a7d7f95f2dbce, 89},
    {"hud.wide.scores.roster.7", 0x0ba6c360bbf542d0, 92},
    {"hud.wide.scores.roster.11", 0xfa761ddee88e3184, 93},
    {"hud.wide.scores.roster.15", 0x7a90cfaad34abb92, 96},
    {"hud.wide.scores.roster.13", 0xa645b62f069e2d3f, 92},
    {"hud.wide.scores.two_players.black_local", 0x6ab45d5480acddd8, 89},
    {"hud.wide.scores.names", 0xb1c936bda1772418, 96},
    {"hud.wide.quit.open", 0xdf73f3d91f954a2e, 122},
    {"hud.wide.quit.hover_yes", 0x3b9355f8d7566b1f, 122},
    {"hud.wide.quit.pressed_no", 0x9e0fbc84dc5a9ed4, 122},
    {"hud.wide.options.open", 0x79616d4bc655cc19, 365},
    {"hud.wide.options.changed", 0xdbc80485cf1d9692, 366},
    {"hud.wide.options.hover_return", 0xcd2418dca46b9e1f, 366},
    {"hud.wide.quickhelp.open", 0x436c19acc3031fd1, 134},
    {"hud.wide.quickhelp.hover_return", 0xa0bc1f1910c52526, 134},
    {"hud.wide.quickhelp.pressed_return", 0xf31e23101333c461, 134},
    {"hud.wide.dialog.invitation", 0x5125a9f3007cb230, 122},
    {"hud.wide.dialog.invitation.hover_accept", 0xe31fd09629a03e79, 122},
    {"hud.wide.dialog.waiting", 0xefc4d1fa1d476c3e, 121},
    {"hud.wide.dialog.breakconfirm", 0x4fcfda5742ebe96d, 122},
    {"hud.wide.topbar.hover_help", 0x91f7bd0685c650c8, 98},
    {"hud.wide.topbar.pressed_help", 0x1cdbe1a6828ff810, 97},
    {"hud.wide.topbar.hover_options", 0xc2d74e4d7be191f1, 98},
    {"hud.wide.topbar.pressed_options", 0xbf45bd918e9ae3a0, 97},
    {"hud.wide.topbar.hover_quit", 0x9b494bf20f5bce09, 98},
    {"hud.wide.topbar.pressed_quit", 0xe95e0a9f434d8283, 97},
    {"hud.wide.marquee.band", 0xcc991fc944b5fe6a, 97},
    {"hud.wide.marquee.clamped", 0xf593254f26da31f4, 97},
    {"hud.wide.marquee.clamped_low", 0xd0d89bae7b201b93, 97},
    {"hud.wide.status.long", 0xd1168ba03511ba31, 96},
    {"ptr.wide.class.map_view", 0x5a1dd554dd5daacd, 518400},
    {"ptr.wide.class.minimap", 0x412ac51fedbd6f8a, 518400},
    {"ptr.wide.class.chat_view", 0xd33a82eac3bc5ed7, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_tl", 0x6120e14a1274a015, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_t", 0x7845bacfec608f1f, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_tr", 0xd0a24e8428148eef, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_l", 0xcef20df609d2b663, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_mid", 0x89a379e25c82e2cd, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_r", 0x256d9d28c4c52373, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_bl", 0x01c9e9d3197b3293, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_b", 0x76375f7cfd86cacf, 518400},
    {"ptr.wide.edge.60x60.rate50.cam_br", 0x038a2cf579102b41, 518400},
    {"ptr.wide.edge.60x60.rate0.all9", 0xfd5a1ca0dba5320d, 4665600},
    {"ptr.wide.edge.60x60.rate99.all9", 0x1ea2f90a915374fd, 4665600},
    {"ptr.wide.edge.31x31.rate50.all9", 0x8c4e8a6f60a65ee5, 4665600},
    {"ptr.wide.edge.40x40.rate25.all9", 0x47ccf33f6f94b285, 4665600},
    {"ptr.wide.edge.14x14.rate50.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.edge.16x16.rate50.all9", 0x9ef1663cfe94c0cd, 4665600},
    {"ptr.wide.edge.12x12.rate50.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.edge.80x50.rate75.all9", 0xde8fc67ee07f143d, 4665600},
    {"ptr.wide.edge.13x9.rate50.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.edge.60x60.rate50.outside_ring.mid", 0xa8728f11e3933925, 49024},
    {"ptr.wide.minimap.point.60x60", 0x56f0c5e5df3660f1, 518400},
    {"ptr.wide.minimap.point.31x31", 0xa9401273887159b5, 518400},
    {"ptr.wide.minimap.point.40x40", 0x3af9ace6fb459db1, 518400},
    {"ptr.wide.minimap.point.14x14", 0x5bda0cccc11bcc79, 518400},
    {"ptr.wide.minimap.point.16x16", 0x1d4f96be52e849e9, 518400},
    {"ptr.wide.minimap.point.80x50", 0x2f4b533746c5632d, 518400},
    {"ptr.wide.minimap.scroll.60x60.all9", 0x7f6bf7cee9748295, 4665600},
    {"ptr.wide.minimap.scroll.31x31.all9", 0x9c1e5777a849e54d, 4665600},
    {"ptr.wide.minimap.scroll.14x14.all9", 0x823aba4206d1b725, 4665600},
    {"ptr.wide.minimap.scroll.16x16.all9", 0xf7e6e1bc803b0425, 4665600},
    {"ptr.wide.start_view.origins", 0xaff608621198d0b4, 10613},
    {"view.wide.camera.clamp", 0x5a8385469ae3abc4, 2023},
    {"view.wide.camera.center_on", 0x9d5f1216d75aeef5, 7648},
    {"view.wide.camera.scroll_pixels", 0xc60723ba128ef67b, 2800},
    {"view.wide.camera.clamp.corner", 0x0693cd6ef68b2b7c, 2023},
    {"view.wide.camera.center_on.corner", 0xc8338d93cf5a2d35, 7648},
    {"view.wide.camera.scroll_pixels.corner", 0x75825320271573b3, 2800},
    {"view.wide.camera.screen_to_world.origin", 0x77e1d653db94b099, 518400},
    {"view.wide.camera.world_to_screen.origin", 0x0b317b1f5c708ef4, 7622},
    {"view.wide.camera.screen_to_world.mid", 0x3e1c574045e31501, 518400},
    {"view.wide.camera.world_to_screen.mid", 0x0b317b1f5c708ef4, 7622},
    {"view.wide.camera.screen_to_world.far", 0x105c85add91308d9, 518400},
    {"view.wide.camera.world_to_screen.far", 0x0b317b1f5c708ef4, 7622},
    {"view.wide.camera.screen_to_world.small16", 0x000e87d6179e245d, 518400},
    {"view.wide.camera.world_to_screen.small16", 0x0b317b1f5c708ef4, 7622},
    {"ptr.wide.cursor.idle.cam_mid400", 0xd8b30bda747f1305, 518400},
    {"ptr.wide.cursor.own_worker.cam_mid400", 0x431ae1bca23bf625, 518400},
    {"ptr.wide.cursor.own_bomber.cam_mid400", 0xc80b40b060c6aa25, 518400},
    {"ptr.wide.cursor.own_thief.cam_mid400", 0x4e65737ce1987225, 518400},
    {"ptr.wide.cursor.bomber_latched.cam_mid400", 0x34d4c425f3c590cd, 518400},
    {"ptr.wide.cursor.worker_move_latched.cam_mid400", 0x431ae1bca23bf625, 518400},
    {"ptr.wide.cursor.enemy_inspected.cam_mid400", 0xd8b30bda747f1305, 518400},
    {"ptr.wide.cursor.hill_own.cam_mid400", 0xd8b30bda747f1305, 518400},
    {"ptr.wide.cursor.fog_own_worker.cam_mid400", 0x4f69e73cb07afe25, 518400},
    {"ptr.wide.cursor.options_open.cam_mid400", 0x59af61e6ef65d725, 518400},
    {"ptr.wide.cursor.quit_open.cam_mid400", 0x59af61e6ef65d725, 518400},
    {"ptr.wide.cursor.band_dragging.cam_mid400", 0x59af61e6ef65d725, 518400},
    {"ptr.wide.cursor.button_captured.cam_mid400", 0xe7244f0d10442a25, 518400},
    {"ptr.wide.cursor.idle.cam_tl", 0x2f04b20490c0bd41, 518400},
    {"ptr.wide.cursor.idle.cam_t", 0x667ee8dabe9fcb45, 518400},
    {"ptr.wide.cursor.idle.cam_tr", 0x9d84544b5f12b5b5, 518400},
    {"ptr.wide.cursor.idle.cam_l", 0x82009ba1806fc619, 518400},
    {"ptr.wide.cursor.idle.cam_mid", 0x49c68f64404e1065, 518400},
    {"ptr.wide.cursor.idle.cam_r", 0x7320b165fdde6b15, 518400},
    {"ptr.wide.cursor.idle.cam_bl", 0x2308e686e9780175, 518400},
    {"ptr.wide.cursor.idle.cam_b", 0xa5e7f1faf8adbd05, 518400},
    {"ptr.wide.cursor.idle.cam_br", 0x6a30a3f35183dab5, 518400},
    {"ptr.wide.cursor.own_worker.cam_tl", 0xcd1732376a270051, 518400},
    {"ptr.wide.cursor.own_worker.cam_t", 0x1e05154b1615f945, 518400},
    {"ptr.wide.cursor.own_worker.cam_tr", 0x3b0d9a4009a6fe95, 518400},
    {"ptr.wide.cursor.own_worker.cam_l", 0x58d456a9d6624875, 518400},
    {"ptr.wide.cursor.own_worker.cam_mid", 0xd546aeee415dae85, 518400},
    {"ptr.wide.cursor.own_worker.cam_r", 0x10a9f75aa872b3f5, 518400},
    {"ptr.wide.cursor.own_worker.cam_bl", 0x11629c939ffc0a55, 518400},
    {"ptr.wide.cursor.own_worker.cam_b", 0x37e0b46b21df0a25, 518400},
    {"ptr.wide.cursor.own_worker.cam_br", 0x07b9e9e7fc182395, 518400},
    {"ptr.wide.click.idle", 0x141e464550b1cbd4, 58422},
    {"ptr.wide.click.own_worker", 0x70f92850bb3579a2, 89352},
    {"ptr.wide.click.own_bomber", 0xff1af76ddf37cbe9, 89706},
    {"ptr.wide.click.hill_own", 0x0cc7d92020d63abb, 58908},
    {"ptr.wide.click.own_worker.right_button", 0xda185c6d9bc471ee, 88590},
    {"ptr.wide.click.dialog.quit", 0xbc1fb4292cecb626, 32820},
    {"ptr.wide.click.dialog.quickhelp", 0x46d4abb250a3d044, 32754},
    {"ptr.wide.click.dialog.options", 0x3ddb1c261f808228, 39138},
    {"ptr.wide.click.dialog.start_modal", 0x99b29917ecef4c65, 32400},
    {"px.wide.world.MEDIUM.cam_tl", 0x95dc3673d504c028, 518400},
    {"px.wide.world.MEDIUM.cam_t", 0x66635d7a3a1c809e, 518400},
    {"px.wide.world.MEDIUM.cam_tr", 0xe0e0c8292973d82b, 518400},
    {"px.wide.world.MEDIUM.cam_l", 0x29e7dd3ad0a61612, 518400},
    {"px.wide.world.MEDIUM.cam_mid", 0x952eaa77934a2da3, 518400},
    {"px.wide.world.MEDIUM.cam_r", 0x7a120c104dfc74e0, 518400},
    {"px.wide.world.MEDIUM.cam_bl", 0x4e20f8b4530bca8f, 518400},
    {"px.wide.world.MEDIUM.cam_b", 0x3f2726530c6b9456, 518400},
    {"px.wide.world.MEDIUM.cam_br", 0xb52a74ecb39993d3, 518400},
    {"px.wide.world.TINY.cam_tl", 0xbc330da064f78288, 518400},
    {"px.wide.world.TINY.cam_t", 0xdd6c67bcc0006d25, 518400},
    {"px.wide.world.TINY.cam_tr", 0x2171c244a56e67de, 518400},
    {"px.wide.world.TINY.cam_l", 0xadf781277aa85eb1, 518400},
    {"px.wide.world.TINY.cam_mid", 0x9b962f23d4045bc6, 518400},
    {"px.wide.world.TINY.cam_r", 0x2d58241d85081213, 518400},
    {"px.wide.world.TINY.cam_bl", 0x82bc31e0e4b4b1e2, 518400},
    {"px.wide.world.TINY.cam_b", 0xd374c99d013b8958, 518400},
    {"px.wide.world.TINY.cam_br", 0xea93e816fb085f57, 518400},
    {"px.wide.world.GAUNTLET.cam_tl", 0xd699dba2e89e5476, 518400},
    {"px.wide.world.GAUNTLET.cam_mid", 0x903e0ba3fbac81c2, 518400},
    {"px.wide.world.ISLANDS.cam_tl", 0x85654509def1e63c, 518400},
    {"px.wide.world.ISLANDS.cam_mid", 0x15370608dcf36412, 518400},
    {"px.wide.world.SMALL.cam_tl", 0xc9ef1b48aa675ee9, 518400},
    {"px.wide.world.SMALL.cam_mid", 0x5c82fc366812687d, 518400},
    {"px.wide.world.TREASURE.cam_tl", 0x1fbd14a1af47079c, 518400},
    {"px.wide.world.TREASURE.cam_mid", 0x2edba5c122d49a2c, 518400},
    {"px.wide.world.small16", 0xda3d4b1c95658b25, 518400},
    {"px.wide.world.small12", 0xf388b740365b8b25, 518400},
    {"px.wide.app.setup.before_refresh", 0x59b5c5c52c351532, 518400},
    {"px.wide.app.setup.refreshed", 0xfe0a3b2c88226c05, 518400},
    {"px.wide.app.setup.hover_start", 0x946835c689ad3ad4, 518400},
    {"px.wide.app.setup.cursor", 0xba4230e31aad5a3c, 518400},
    {"px.wide.app.setup.room_leader", 0xa06a1b577f93ec06, 518400},
    {"px.wide.app.setup.room_guest", 0xf8ed8630e8ced504, 518400},
    {"px.wide.app.quickhelp.rest", 0x169fb94096dad790, 518400},
    {"px.wide.app.quickhelp.hover_start", 0x2fa88736e7f1b0af, 518400},
    {"px.wide.app.results", 0x8d6ae83a6750f4d7, 518400},
    {"px.wide.app.match.modal", 0x3ddb897f72c9d3d6, 518400},
    {"px.wide.app.match.start_view", 0x595d3def1a48bb8b, 518400},
    {"px.wide.app.match.cam_tl", 0xf19b355a5395c138, 518400},
    {"px.wide.app.match.cam_t", 0x3a8f693eab398c3b, 518400},
    {"px.wide.app.match.cam_tr", 0xdc84467c2cc25cdb, 518400},
    {"px.wide.app.match.cam_l", 0xca013b90f2f1e7ef, 518400},
    {"px.wide.app.match.cam_mid", 0x7d97859e8b29c4d7, 518400},
    {"px.wide.app.match.cam_r", 0xe4b044b9be8bd77d, 518400},
    {"px.wide.app.match.cam_bl", 0x35f62f16f716266a, 518400},
    {"px.wide.app.match.cam_b", 0xbb57cdb5c8e1e1d7, 518400},
    {"px.wide.app.match.cam_br", 0xb82828fd3358d899, 518400},
    {"px.wide.app.match.quit_dialog", 0xcb9a7f2c39318ead, 518400},
    {"px.wide.app.match.options_page", 0xe594c3d9822c0d2f, 518400},
    {"px.wide.app.match.quickhelp_page", 0xb19e23863dbf22c2, 518400},
    {"px.wide.app.match.cursor_map", 0x8c1e413493cf0e22, 518400},
    {"px.wide.app.match.cursor_edge", 0xa62d92275e38ffb9, 518400},
    {"px.wide.app.match.cursor_edge_east", 0xf806b33e9b8d9fd4, 518400},
    {"px.wide.app.start.TINY", 0x8c15e5245f01c313, 518400},
    {"px.wide.app.start.SMALL", 0xfe362f5f666236d8, 518400},
    {"px.wide.app.start.ISLANDS", 0x77dc81d9280a5b04, 518400},
    {"px.wide.app.start.GAUNTLET", 0x79e2b3de5175248b, 518400},
    {"px.wide.app.start.TREASURE", 0xffb65b08c16cda77, 518400},
};

}  // namespace

// END GOLDEN TABLE



const Golden* find_golden(const std::string& name) {
    for (const Golden& g : kGoldens) {
        if (name == g.name) return &g;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--print") g_print = true;
        else if (a == "--list") list_only = true;
        else if (a == "--dump" && i + 1 < argc) { g_dump_name = argv[++i]; g_keep_logs = true; }
        else if (a == "--only" && i + 1 < argc) g_only_prefix = argv[++i];
        else if (a == "--save" && i + 1 < argc) g_save_dir = argv[++i];
        else {
            std::fprintf(stderr, "usage: test_view_fingerprint [--print | --list | --dump NAME | --only PREFIX | --save DIR]\n");
            return 2;
        }
    }
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

    self_check_scenarios();
    self_check_wide_scenarios();
    hud_scenarios(arc);
    screen_scenarios(arc);
    menu_scenarios(arc);
    edge_scenarios();
    minimap_scenarios();
    camera_scenarios();
    cursor_scenarios();
    click_scenarios();
    screen_click_scenarios();
    world_pixel_scenarios(arc);
    app_pixel_scenarios();
    app_pointer_scenarios();
    wide_scenarios(arc);

    SDL_Quit();
    if (list_only) {
        for (const auto& m : g_measured) std::printf("%s\n", m.first.c_str());
        return 0;
    }
    if (g_print) {
        std::printf("    // %zu fingerprints\n", g_measured.size());
        for (const auto& m : g_measured) {
            std::printf("    {\"%s\", %s, %llu},\n", m.first.c_str(), hex64(m.second.hash).c_str(), static_cast<unsigned long long>(m.second.count));
        }
        return g_failures == 0 ? 0 : 1;
    }
    if (g_only_prefix.empty()) {
        for (const Golden& g : kGoldens) {
            ++g_checks;
            if (std::find(g_seen.begin(), g_seen.end(), g.name) == g_seen.end()) {
                ++g_failures;
                std::fprintf(stderr, "  FAIL [%s]: a golden number without a scenario\n", g.name);
            }
        }
    }
    std::printf("\nview fingerprint: %zu fingerprints, %d checks, %d failures\n", g_measured.size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

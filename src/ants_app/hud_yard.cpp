// The HUD in the Yard (docs/YARD.md): what is left of it when the picture is the map alone (ScreenLayout::map_only). The panel, the top bar, the scores, the status line, the chat, the pedestals and
// the dialogs do not exist there; what stays is the pointer model (click, drag box, right click, cursors, order ring: hud_input.cpp, unchanged), the minimap and the rubber band, and a few keys.
// Every function here is used behind `layout_.yard`, which only ScreenLayout::map_only sets: no other layout reaches this file, and tests/test_app/test_yard_isolation.cpp pins what the
// HUD does in the two layouts of the game itself.
#include "ants_app/hud.hpp"

namespace ants::app {

void HUD::apply_yard_layout() {
    for (UIButton* b : {&help_button_, &options_button_, &quit_button_, &move_pedestal_button_, &ability_pedestal_button_, &stop_button_, &hatch_button_, &team_up_button_, &send_to_button_,
                        &team_button_, &yes_button_, &no_button_}) {
        b->w = b->h = 0;                                   // (a rectangle of no size contains no point: a click, a tap or a hover never reaches a control that is not drawn)
    }
}

void HUD::render_yard(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world, const ViewportCamera& camera) {
    render_radar(renderer, assets, world, camera);
    if (is_dragging_) render_marquee_box(renderer);
}

bool HUD::yard_takes_key(int32_t key, bool ctrl) noexcept {
    if (key == SDLK_ESCAPE) return true;                   // (deselect)
    return ctrl && (key == 'a' || key == 'A' || key == 'n' || key == 'N' || key == 'p' || key == 'P' || key == 's' || key == 'S');       // (all of mine, next, previous, stop)
}

EdgeScroll HUD::yard_edge_dwell(EdgeScroll step) noexcept {
    if (step.dx == 0 && step.dy == 0) {                    // (not where the view scrolls: the dwell starts over)
        yard_edge_ticks_ = 0;
        return step;
    }
    if (yard_edge_ticks_ < kYardEdgeDwellTicks) {
        ++yard_edge_ticks_;
        step.dx = 0;
        step.dy = 0;
    }
    return step;
}

}  // namespace ants::app

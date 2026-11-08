// The alliance dialogs of the match screen (hud.hpp): the invitation, the waiting dialog and the confirmation, their buttons, and the request to team up.
#include "ants_app/hud.hpp"
#include "hud_internal.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_sim/game_strings.hpp"

#include <string>
#include <utility>

namespace ants::app {

using hud_detail::draw_std_dialog;

// ------------------------------------------------------------------------------------------------
// Alliance dialogs (Ants.exe FUN_01015b65 invitation, FUN_010160e2 waiting, FUN_01016438 confirmation; docs 5.42)
// ------------------------------------------------------------------------------------------------

std::string HUD::alliance_color_word(uint8_t team) const {
    return sim::strings::color_name(static_cast<uint8_t>(3u - (team & 3u)));      // strings 100 - 103: black, blue, red, green by color index
}

bool HUD::ally_pedestal_possible(const sim::WorldState& world) const {
    if (local_player_id_ < sim::MAX_PLAYERS && (world.dropped_mask & (1u << local_player_id_)) != 0) return false;
    size_t live = 0;
    for (const auto& hill : world.anthills) {
        if (hill.team_id >= sim::MAX_PLAYERS || (world.dropped_mask & (1u << hill.team_id)) == 0) ++live;
    }
    return live > 2;
}

std::string HUD::alliance_name(uint8_t team) const {
    return team < team_names_.size() && !team_names_[team].empty() ? team_names_[team] : alliance_color_word(team);
}

void HUD::open_alliance_dialog(AllianceDialog kind, uint8_t other, std::string text) {
    release_capture();
    alliance_dialog_ = kind;
    alliance_other_ = other;
    alliance_text_ = std::move(text);
    // the buttons are the original's animations (dad_bacc / dad_bdec, dw_bwith, dyn_byes / dyn_bno) whose single part sits at these offsets from the
    // dialog's origin (100, 100)
    alliance_button_a_ = {};
    alliance_button_b_ = {};
    switch (kind) {
        case AllianceDialog::Invitation:
            alliance_button_a_ = {152, 260, 80, 24, false, false};      // Accept  (accpt1.bmp at part offset (52, 160))
            alliance_button_b_ = {284, 260, 80, 24, false, false};      // Decline (decl1.bmp at (184, 160))
            break;
        case AllianceDialog::Waiting:
            alliance_button_a_ = {220, 260, 80, 23, false, false};      // Withdraw (withd1.bmp at (120, 160))
            break;
        case AllianceDialog::BreakConfirm:
            alliance_button_a_ = {180, 260, 49, 24, false, false};      // Yes (yes1.bmp at (80, 160))
            alliance_button_b_ = {292, 260, 49, 24, false, false};      // No  (no1.bmp at (192, 160))
            break;
        case AllianceDialog::None:
            break;
    }
}

void HUD::close_alliance_dialog() noexcept {
    alliance_dialog_ = AllianceDialog::None;
    alliance_other_ = 255;
    alliance_text_.clear();
    alliance_replaces_team_ = false;
    alliance_button_a_ = {};
    alliance_button_b_ = {};
}

// The dialogs follow the simulation's state, not an event, so that they are the same on every machine of a network match and a dialog cannot be missed.
// An answer takes a few turns to reach the simulation in a network match: the question or the waiting dialog of an offer that was just answered does not
// come back meanwhile (`suppressed_*`). The original closes the dialogs of a team that dropped out (FUN_0100c4ed); the simulation clears its offers then.
void HUD::update_alliance_dialog(const sim::WorldState& world) {
    const uint8_t me = local_player_id_;
    if (me >= sim::MAX_PLAYERS) return;
    if (suppressed_invite_from_ != 255 && world.pending_invite_from[me] != suppressed_invite_from_) suppressed_invite_from_ = 255;
    if (suppressed_wait_for_ != 255 && world.pending_invite_from[suppressed_wait_for_] != me) suppressed_wait_for_ = 255;
    switch (alliance_dialog_) {
        case AllianceDialog::Invitation:
            if (world.pending_invite_from[me] != alliance_other_) close_alliance_dialog();
            break;
        case AllianceDialog::Waiting:
            if (alliance_other_ >= sim::MAX_PLAYERS || world.pending_invite_from[alliance_other_] != me) close_alliance_dialog();
            break;
        case AllianceDialog::BreakConfirm:
            if (world.player_alliances[me] != alliance_other_) {                      // nothing left to break
                close_alliance_dialog();
                pending_break_ = PendingBreak{};
            }
            break;
        case AllianceDialog::None:
            break;
    }
    if (alliance_dialog_ != AllianceDialog::None || is_modal_open()) return;          // one dialog at a time, none over another
    const uint8_t from = world.pending_invite_from[me];
    if (from < sim::MAX_PLAYERS && from != suppressed_invite_from_) {
        const uint8_t old_ally = world.player_alliances[me];
        const bool replaces = old_ally < sim::MAX_PLAYERS && old_ally != from;       // string 2 when accepting ends the present team
        open_alliance_dialog(AllianceDialog::Invitation, from,
                             replaces ? sim::strings::format(sim::strings::kInviteBreakDialog, alliance_name(from), alliance_color_word(from),
                                                             alliance_name(old_ally), alliance_color_word(old_ally))
                                      : sim::strings::format(sim::strings::kInviteDialog, alliance_name(from), alliance_color_word(from)));
        alliance_replaces_team_ = replaces;
        return;
    }
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        if (t != me && world.pending_invite_from[t] == me && t != suppressed_wait_for_) {
            open_alliance_dialog(AllianceDialog::Waiting, t, sim::strings::format(sim::strings::kWaitingForAnswer, alliance_name(t), alliance_color_word(t)));
            return;
        }
    }
}

void HUD::render_alliance_dialog(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;
    draw_std_dialog(renderer, assets);
    // the label (30, 10) of the dialog, 24 px, centred, color (31, 23, 51); 270 px wide in the invitation, 240 px in the other two
    draw_label(renderer, alliance_text_, 130, 110, alliance_dialog_ == AllianceDialog::Invitation ? 270 : 240, ColorRGBA{31, 23, 51, 255}, FontSize::Px24, true);
    auto button = [&](const UIButton& b, const char* up, const char* hover, const char* down) {
        if (b.w > 0) draw_animation_frame0(renderer, assets, b.is_pressed ? down : (b.is_active ? hover : up), 100, 100);
    };
    switch (alliance_dialog_) {
        case AllianceDialog::Invitation:
            button(alliance_button_a_, "dad_bacc1", "dad_bacc2", "dad_bacc3");
            button(alliance_button_b_, "dad_bdec1", "dad_bdec2", "dad_bdec3");
            break;
        case AllianceDialog::Waiting:
            button(alliance_button_a_, "dw_bwith1", "dw_bwith2", "dw_bwith3");
            break;
        case AllianceDialog::BreakConfirm:
            button(alliance_button_a_, "dyn_byes1", "dyn_byes2", "dyn_byes3");
            button(alliance_button_b_, "dyn_bno1", "dyn_bno2", "dyn_bno3");
            break;
        case AllianceDialog::None:
            break;
    }
}

void HUD::answer_alliance_dialog(sim::SimulationEngine& sim, bool yes) {
    const uint8_t me = local_player_id_;
    const uint8_t other = alliance_other_;
    auto command = [&](sim::CommandType type, uint8_t target = 255) {
        sim::Command c;
        c.type = type;
        c.issuer = me;
        c.other_player = target;
        submit_command(sim, c);
    };
    const AllianceDialog kind = alliance_dialog_;
    const bool replaces = alliance_replaces_team_;
    PendingBreak pending = std::move(pending_break_);
    pending_break_ = PendingBreak{};
    close_alliance_dialog();
    switch (kind) {
        case AllianceDialog::Invitation:                                              // FUN_01016081
            suppressed_invite_from_ = other;
            if (yes) {
                if (replaces) command(sim::CommandType::AllianceBreak);              // the present team is broken first (FUN_01010d26)
                command(sim::CommandType::AllianceAccept, other);
            } else {
                command(sim::CommandType::AllianceDeny, other);
            }
            break;
        case AllianceDialog::Waiting:                                                 // callback 0x10163f1: the offer is taken back
            suppressed_wait_for_ = other;
            command(sim::CommandType::AllianceWithdraw, other);
            break;
        case AllianceDialog::BreakConfirm:                                            // callbacks FUN_0100c838 and FUN_01020076
            if (!yes) break;
            command(sim::CommandType::AllianceBreak);
            if (pending.action == PendingBreak::Action::Invite) {
                command(sim::CommandType::AllianceInvite, pending.target);
            } else if (pending.action == PendingBreak::Action::Attack) {
                issue_group_order(sim, pending.tile, false, true, pending.ants);
            }
            break;
        case AllianceDialog::None:
            break;
    }
}

void HUD::request_team_up(sim::SimulationEngine& sim, uint8_t target) {
    const uint8_t me = local_player_id_;
    if (me >= sim::MAX_PLAYERS || target >= sim::MAX_PLAYERS || target == me) return;
    const uint8_t ally = sim.get_world_state().player_alliances[me];
    if (ally < sim::MAX_PLAYERS) {                                                    // FUN_0100c7ac: with an ally the confirmation comes first
        if (is_modal_open()) return;
        pending_break_ = PendingBreak{PendingBreak::Action::Invite, target, {}, {}};
        open_alliance_dialog(AllianceDialog::BreakConfirm, ally,
                             sim::strings::format(sim::strings::kBreakTeamConfirm, alliance_name(ally), alliance_color_word(ally)));
        return;
    }
    sim::Command invite;                                                              // FUN_0100c838(1): the offer goes out
    invite.type = sim::CommandType::AllianceInvite;
    invite.issuer = me;
    invite.other_player = target;
    submit_command(sim, invite);
}

} // namespace ants::app

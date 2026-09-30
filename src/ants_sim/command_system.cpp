// SimulationEngine::apply_command: the validated entry through which players change the simulation (include/ants_sim/command.hpp).
//
// Every machine of a lock-step match runs this with the same commands in the same order, so everything here must be deterministic and must
// not trust the command: the issuer is stamped by the transport, ants are looked up by id and kept only when they belong to the issuer, tiles
// are range checked, an answer to an invitation needs the invitation.
#include "ants_sim/sim_engine.hpp"
#include "sim_engine_impl.hpp"

#include <algorithm>
#include <cstdlib>

namespace ants::sim {

CommandResult SimulationEngine::apply_command(const Command& cmd) {
    using Status = CommandResult::Status;
    CommandResult res;
    if (cmd.issuer >= MAX_PLAYERS || (impl_->roster_mask_ & (1u << cmd.issuer)) == 0) {    // a team without a player has no voice
        res.status = Status::RejectedIssuer;
        return res;
    }
    if (cmd.type == CommandType::None || cmd.type > CommandType::Last) {
        res.status = Status::RejectedMalformed;
        return res;
    }
    if (impl_->match_state_ == MatchState::GameOver) {           // nothing changes after the end of the match
        res.status = Status::Ignored;
        return res;
    }
    if ((impl_->dropped_mask_ & (1u << cmd.issuer)) != 0) {      // a team that dropped out no longer acts (its ants are dying)
        res.status = Status::Ignored;
        return res;
    }

    // The issuer's own ants among the named ones, each once, in the order given (the group order sorts them with an exchange sort that is not
    // stable, so the order of the selection is part of the outcome)
    auto own_ants = [&](const Command& c) {
        std::vector<uint32_t> own;
        for (uint32_t id : c.ants) {
            const AntUnit* u = impl_->find_unit(id);
            if (u == nullptr || u->removed || u->player_id != c.issuer) continue;
            if (std::find(own.begin(), own.end(), id) != own.end()) continue;
            own.push_back(id);
        }
        return own;
    };
    auto valid_other = [&](const Command& c) { return c.other_player < MAX_PLAYERS && c.other_player != c.issuer; };

    switch (cmd.type) {
        case CommandType::GroupMove:
        case CommandType::GroupSpecial:
        case CommandType::GroupAttack: {
            if (cmd.ants.empty() || cmd.ants.size() > kMaxCommandAnts || !impl_->grid_.in_bounds(cmd.tile_x, cmd.tile_y)) {
                res.status = Status::RejectedMalformed;
                return res;
            }
            const std::vector<uint32_t> own = own_ants(cmd);
            res.ants_ordered = static_cast<uint32_t>(own.size());
            if (own.empty()) {
                res.status = Status::Ignored;
                return res;
            }
            const TileCoord tile{cmd.tile_x, cmd.tile_y};
            if (cmd.type == CommandType::GroupMove) res.ack_ant = issue_group_move_order(own, tile, impl_->grid_.has_bomb_at(tile));
            else if (cmd.type == CommandType::GroupSpecial) res.ack_ant = issue_group_special_order(own, tile);
            else res.ack_ant = issue_group_attack_order(own, tile);
            res.status = Status::Applied;
            return res;
        }
        case CommandType::Stop: {
            if (cmd.ants.empty() || cmd.ants.size() > kMaxCommandAnts) {
                res.status = Status::RejectedMalformed;
                return res;
            }
            const std::vector<uint32_t> own = own_ants(cmd);
            if (own.empty()) {
                res.status = Status::Ignored;
                return res;
            }
            for (uint32_t id : own) {
                if (stop_ant(id)) ++res.ants_ordered;
            }
            res.status = Status::Applied;
            return res;
        }
        case CommandType::Hatch:
            res.hatch_result = static_cast<uint8_t>(try_hatch(cmd.issuer, AntType::Worker, false));
            res.status = Status::Applied;
            return res;
        case CommandType::AllianceInvite:
            if (!valid_other(cmd)) {
                res.status = Status::RejectedMalformed;
                return res;
            }
            propose_alliance(cmd.issuer, cmd.other_player);
            res.status = Status::Applied;
            return res;
        case CommandType::AllianceAccept:
        case CommandType::AllianceDeny: {
            if (!valid_other(cmd)) {
                res.status = Status::RejectedMalformed;
                return res;
            }
            const AllianceInvite& invite = impl_->stats_.get_pending_invite(cmd.issuer);
            if (!invite.active || invite.from_player != cmd.other_player) {      // there is no such invitation to answer
                res.status = Status::RejectedNotAllowed;
                return res;
            }
            if (cmd.type == CommandType::AllianceAccept) accept_alliance(cmd.issuer, cmd.other_player);
            else deny_alliance(cmd.issuer, cmd.other_player);
            res.status = Status::Applied;
            return res;
        }
        case CommandType::AllianceWithdraw:
            if (!valid_other(cmd)) {
                res.status = Status::RejectedMalformed;
                return res;
            }
            withdraw_alliance_offer(cmd.issuer, cmd.other_player);
            res.status = Status::Applied;
            return res;
        case CommandType::AllianceBreak:
            break_alliance(cmd.issuer);
            res.status = Status::Applied;
            return res;
        case CommandType::Drop:                                  // system command of the sequencer (a peer left or fell silent)
            drop_player(cmd.issuer);
            res.status = Status::Applied;
            return res;
        default:
            break;
    }
    res.status = Status::RejectedMalformed;
    return res;
}

// FUN_0100d03b: a team leaves the match (Ants.exe 0x100d03b, docs 5.47). Nothing happens for a team that is not in the match or has already
// dropped (the team's +0x64 flag). The cue plays unless the match is over, the News Flash of string 46 is written, every ant of the team gets
// SetAction(death) (the ants die through the ordinary death and removal), the team's alliance ends and an egg in the incubator is lost (its
// owner is gone, nobody would send the newborn).
void SimulationEngine::drop_player(uint8_t player_id) {
    if (player_id >= MAX_PLAYERS) return;
    const uint8_t bit = static_cast<uint8_t>(1u << player_id);
    if ((impl_->roster_mask_ & bit) == 0 || (impl_->dropped_mask_ & bit) != 0) return;
    impl_->dropped_mask_ = static_cast<uint8_t>(impl_->dropped_mask_ | bit);
    trigger_player_dropout(player_id);
    for (size_t i = 0; i < impl_->ants_.size(); ++i) {
        AntUnit* a = impl_->ants_[i].get();
        if (a == nullptr || a->removed || a->player_id != player_id) continue;
        impl_->set_action(*a, AntUnit::kActionDeath, static_cast<uint8_t>(a->facing), -1, -1, false);
    }
    impl_->stats_.break_alliance(player_id);
    impl_->hatch_[player_id].active = false;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::is_player_dropped(uint8_t player_id) const noexcept {
    return player_id < MAX_PLAYERS && (impl_->dropped_mask_ & (1u << player_id)) != 0;
}

// The ant that would acknowledge a group order given now, picked as group_order / issue_group_attack_order pick it (the entries that can take
// the order and are not already carrying it out, the closest first: the exchange sort keeps the first of several equally close ants at the
// front) and only when its order would queue a path: an enemy hill is no goal for anything but a thief (GoTo stops the ant), and a special
// order needs a valid target for the ant's type. Whatever else can refuse the goal later (a blocked tile) is not predicted.
uint32_t SimulationEngine::predict_order_ack(const Command& cmd) const {
    if (!is_group_order(cmd.type) || cmd.issuer >= MAX_PLAYERS || cmd.ants.empty() || cmd.ants.size() > kMaxCommandAnts ||
        !impl_->grid_.in_bounds(cmd.tile_x, cmd.tile_y) || impl_->match_state_ == MatchState::GameOver ||
        (impl_->roster_mask_ & (1u << cmd.issuer)) == 0 || (impl_->dropped_mask_ & (1u << cmd.issuer)) != 0) {
        return 0;
    }
    const TileCoord target{cmd.tile_x, cmd.tile_y};
    int hill_team = -1;
    for (const auto& ah : impl_->grid_.anthills()) {
        if (target.x >= static_cast<int32_t>(ah.x) && target.x <= static_cast<int32_t>(ah.x) + 3 &&
            target.y >= static_cast<int32_t>(ah.y) && target.y <= static_cast<int32_t>(ah.y) + 3) {
            hill_team = ah.team_id;
            break;
        }
    }
    const AntUnit* best = nullptr;
    uint32_t best_d = 0;
    std::vector<uint32_t> seen;
    for (uint32_t id : cmd.ants) {
        const AntUnit* a = impl_->find_unit(id);
        if (a == nullptr || a->removed || a->player_id != cmd.issuer) continue;
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);
        if (!impl_->can_take_user_order(*a)) continue;
        const uint8_t o = a->orig_order;
        if (cmd.type == CommandType::GroupAttack) {
            if (o == AntUnit::kOrderAttack && a->orig_order_tile == target) continue;
        } else {
            if ((o == AntUnit::kOrderMove || o == AntUnit::kOrderPowerUp || o == AntUnit::kOrderHarvest) && a->orig_order_tile == target) continue;
            if (o == AntUnit::kOrderHome && hill_team == a->player_id) continue;
        }
        const int32_t dr = std::abs(a->pixel_y / 32 - target.y);
        const int32_t dc = std::abs(a->pixel_x / 32 - target.x);
        const uint32_t d = static_cast<uint32_t>(std::max(dr, dc)) << 4;
        if (best == nullptr || d < best_d) {
            best = a;
            best_d = d;
        }
    }
    if (best == nullptr) return 0;
    if (hill_team >= 0 && hill_team != best->player_id && best->type != AntType::Thief) return 0;       // GoTo: stop_sync, no path
    // Only the ability types can refuse the target: a worker, combat ant or thief keeps its order 0 and simply walks to the tile
    if (cmd.type == CommandType::GroupSpecial && (best->type == AntType::Bomber || best->type == AntType::Fire || best->type == AntType::Swimmer) &&
        hill_team < 0 && !is_special_target_valid(best->type, target, false, best->player_id)) {
        return 0;
    }
    return best->id;
}

}  // namespace ants::sim

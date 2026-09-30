// SimulationEngine::apply_command: the validated entry through which players change the simulation (include/ants_sim/command.hpp).
//
// Every machine of a lock-step match runs this with the same commands in the same order, so everything here must be deterministic and must
// not trust the command: the issuer is stamped by the transport, ants are looked up by id and kept only when they belong to the issuer, tiles
// are range checked, an answer to an invitation needs the invitation.
#include "ants_sim/sim_engine.hpp"
#include "sim_engine_impl.hpp"

#include <algorithm>

namespace ants::sim {

CommandResult SimulationEngine::apply_command(const Command& cmd) {
    using Status = CommandResult::Status;
    CommandResult res;
    if (cmd.issuer >= MAX_PLAYERS) {
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
        default:
            break;
    }
    res.status = Status::RejectedMalformed;
    return res;
}

}  // namespace ants::sim

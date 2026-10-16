#include "ants_replay/match_stats.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

#include "ants_sim/sim_engine.hpp"

namespace ants::replay {

namespace {

constexpr size_t kSeats = MatchStats::kSeats;

bool plays(uint8_t roster, size_t seat) { return seat < kSeats && ((roster >> seat) & 1u) != 0; }

// An ability type is the type of an ant whose special order is more than a stop: the first such ant of a group names what the order is
bool has_ability(sim::AntType type) { return type != sim::AntType::Worker && type != sim::AntType::Combat; }

}  // namespace

const char* MatchStats::order_kind_name(OrderKind kind) noexcept {
    switch (kind) {
        case OrderKind::Move: return "move";
        case OrderKind::Attack: return "attack";
        case OrderKind::Bomb: return "bomb";
        case OrderKind::Fire: return "fire";
        case OrderKind::Bridge: return "bridge";
        case OrderKind::Raid: return "raid";
        case OrderKind::Special: return "special";
        case OrderKind::Stop: return "stop";
        case OrderKind::Hatch: return "hatch";
        case OrderKind::Team: return "team";
        case OrderKind::Count: break;
    }
    return "?";
}

MatchStatsCollector::MatchStatsCollector(const Replay& replay) : roster_(replay.head.roster) {
    stats_.roster = replay.head.roster;
    stats_.samples.reserve(replay.total_turns / MatchStats::kSampleTurns + 1);
    ants_.resize(64);
}

Hooks MatchStatsCollector::hooks() {
    Hooks h;
    h.before_command = [this](uint32_t turn, const sim::Command& command, sim::SimulationEngine& engine) { on_command(turn, command, engine); };
    h.after_tick = [this](uint32_t turns, sim::SimulationEngine& engine) { on_tick(turns, engine); };
    return h;
}

MatchStats MatchStatsCollector::take(uint32_t turns_played) {
    stats_.turns = turns_played;
    stats_.ok = !failed_;
    return std::move(stats_);
}

void MatchStatsCollector::fail(const char* what) {
    if (failed_) return;
    failed_ = true;
    stats_.why = what;
}

// ---- the orders -------------------------------------------------------------------------------------------------------------------------

void MatchStatsCollector::on_command(uint32_t turn, const sim::Command& c, sim::SimulationEngine& engine) {
    if (failed_ || !plays(roster_, c.issuer)) return;
    try {
        using Kind = MatchStats::OrderKind;
        Kind kind = Kind::Move;
        switch (c.type) {
            case sim::CommandType::GroupMove: kind = Kind::Move; break;
            case sim::CommandType::GroupAttack: kind = Kind::Attack; break;
            case sim::CommandType::GroupSpecial: kind = Kind::Special; break;
            case sim::CommandType::Stop: kind = Kind::Stop; break;
            case sim::CommandType::Hatch: kind = Kind::Hatch; break;
            case sim::CommandType::AllianceInvite:
            case sim::CommandType::AllianceAccept:
            case sim::CommandType::AllianceDeny:
            case sim::CommandType::AllianceWithdraw:
            case sim::CommandType::AllianceBreak: kind = Kind::Team; break;
            default: return;                                           // a Quit, a Drop: no order
        }
        // the ants that the order names: those that are in the match and are the issuer's (the others are not there, as the list of orders says)
        uint8_t named = 0;
        bool have_ability = false;
        sim::AntType ability = sim::AntType::Worker;
        for (const uint32_t id : c.ants) {
            try {
                const sim::AntUnit& ant = engine.get_unit(id);
                if (ant.removed || ant.player_id != c.issuer) continue;
                const sim::AntType type = engine.ant_type(ant);
                named = static_cast<uint8_t>(named | (1u << static_cast<unsigned>(type)));
                if (!have_ability && has_ability(type)) {
                    have_ability = true;
                    ability = type;
                }
            } catch (const std::runtime_error&) {                      // (no such ant)
            }
        }
        if (kind == Kind::Special && have_ability) {
            switch (ability) {
                case sim::AntType::Bomber: kind = Kind::Bomb; break;
                case sim::AntType::Fire: kind = Kind::Fire; break;
                case sim::AntType::Swimmer: kind = Kind::Bridge; break;
                case sim::AntType::Thief: kind = Kind::Raid; break;
                default: break;
            }
        }
        std::vector<std::array<uint32_t, kSeats>>& by_minute = stats_.orders[static_cast<size_t>(kind)];
        const size_t minute = turn / MatchStats::kMinuteTurns;
        if (by_minute.size() <= minute) by_minute.resize(minute + 1);
        ++by_minute[minute][c.issuer];
        for (unsigned t = 0; t < 6; ++t) {
            if ((named & (1u << t)) != 0) ++stats_.orders_naming[c.issuer][t];
        }
    } catch (const std::exception&) {
        fail("an order could not be read");
    }
}

// ---- what the world says after a tick -------------------------------------------------------------------------------------------------------

void MatchStatsCollector::on_tick(uint32_t turns, sim::SimulationEngine& engine) {
    if (failed_) return;
    try {
        // 1. the totals: the score's rises are what the hill banked (a hatch is paid out of it: the cost is the one rule of hatch_request, so a rise in the tick of a hatch is not hidden by it)
        std::array<int32_t, kSeats> score{};
        std::array<uint32_t, kSeats> hatched{};
        uint32_t planted = 0, defused = 0;
        for (size_t s = 0; s < kSeats; ++s) {
            const sim::PlayerMatchStats& st = engine.stats_manager().get_player_stats(static_cast<uint8_t>(s));
            score[s] = st.score;
            hatched[s] = st.new_hatched;
            planted += st.bombs_planted;
            defused += st.bombs_defused;
            if (!plays(roster_, s)) continue;
            int64_t cost = 0;
            if (hatched[s] > prev_hatched_[s] && engine.game_mode() != sim::GameMode::Kills187) cost = std::min<int64_t>(sim::HATCH_COST_POINTS, std::max<int32_t>(0, prev_score_[s]));
            const int64_t rise = int64_t{score[s]} - prev_score_[s] + cost;
            if (rise > 0) stats_.banked[s] += rise;
            stats_.hatch_cost[s] += cost;
        }
        prev_score_ = score;
        prev_hatched_ = hatched;

        // 2. the cues of this tick: a bomb went off
        bool bomb_cue = false;
        for (const sim::AudioEvent& cue : engine.poll_audio_events()) {
            if (cue.sound_id == sim::SoundID::BombDetonate && !cue.stop) bomb_cue = true;
        }

        // 3. the ants: hit points that fell, a type that changed, food picked up, a raid that began; and the ants that are gone
        const sim::WorldState& world = engine.get_world_state();
        std::array<uint32_t, kSeats> alive{};
        for (const sim::AntSnapshot& snap : world.ants) {
            if (snap.id >= ants_.size()) ants_.resize(std::max<size_t>(ants_.size() * 2, size_t{snap.id} + 1));
            Ant& ant = ants_[snap.id];
            ant.last_seen = turns;
            if (snap.player_id < kSeats) ++alive[snap.player_id];
            if (!ant.alive) {                                           // a new ant (the ants of the start, a hatched one)
                ant = Ant{true, snap.player_id, static_cast<uint8_t>(snap.raw_type), snap.hp, snap.is_holding, snap.target_team_id, snap.tile_x, snap.tile_y, turns};
                continue;
            }
            if (snap.player_id < kSeats) {
                if (snap.hp < ant.hp) {                                 // it lost hit points; the damage is the team's of the last damage it took
                    uint8_t source = MatchStats::kNoKiller;
                    try {
                        const uint8_t killer = engine.get_unit(snap.id).killer_team;
                        if (killer < kSeats) source = killer;
                    } catch (const std::runtime_error&) {
                    }
                    const uint32_t lost = static_cast<uint32_t>(ant.hp - snap.hp);
                    stats_.damage_taken[snap.player_id] += lost;
                    if (source != MatchStats::kNoKiller) {
                        stats_.damage_from[source][snap.player_id] += lost;
                        if (source != snap.player_id) stats_.damage_dealt[source] += lost;
                    }
                }
                const uint8_t type = static_cast<uint8_t>(snap.raw_type);
                if (type != ant.own_type && type >= 1 && type <= 5) ++stats_.powerups[snap.player_id][type - 1u];
                if (snap.is_holding && !ant.holding) {                  // it picked something up: food, or (a thief) the loot of a raid, which the engine flags itself
                    bool loot = false;
                    try {
                        loot = engine.get_unit(snap.id).is_thief_steal;
                    } catch (const std::runtime_error&) {
                    }
                    if (!loot) {
                        ++stats_.food_trips[snap.player_id];
                        stats_.food_points[snap.player_id] += snap.carried_points;
                    }
                }
                if (snap.target_team_id < kSeats && ant.target_team >= kSeats && snap.type == sim::AntType::Thief) {   // (the field names a team only while a thief raids its hill)
                    int32_t points = 0;
                    try {
                        points = engine.get_unit(snap.id).raid_amount;
                    } catch (const std::runtime_error&) {
                    }
                    stats_.raids.push_back(MatchStats::Raid{turns, snap.player_id, snap.target_team_id, points});
                }
            }
            ant.hp = snap.hp;
            ant.own_type = static_cast<uint8_t>(snap.raw_type);
            ant.holding = snap.is_holding;
            ant.target_team = snap.target_team_id;
            ant.tile_x = snap.tile_x;
            ant.tile_y = snap.tile_y;
        }
        for (size_t id = 1; id < ants_.size(); ++id) {
            Ant& ant = ants_[id];
            if (!ant.alive || ant.last_seen == turns) continue;
            ant.alive = false;                                          // gone (RemoveAnt): the engine keeps the entry, with the team of the last damage
            MatchStats::Kill kill;
            kill.turn = turns;
            kill.victim = ant.player;
            kill.victim_type = ant.own_type;
            try {
                const sim::AntUnit& unit = engine.get_unit(static_cast<uint32_t>(id));
                kill.drowned = unit.death_status == sim::DeathStatus::Drowned;
                if (unit.killer_team < kSeats && unit.killer_team != ant.player) kill.killer = unit.killer_team;     // (a team does not kill itself: the engine counts no kill then)
            } catch (const std::runtime_error&) {
            }
            if (ant.player < kSeats) stats_.kills.push_back(kill);
        }

        // 4. the bombs on the map: looked at only in a tick in which a bomb was planted or defused or went off; one that is gone and was not defused went off
        if (planted != bombs_planted_ || defused != bombs_defused_ || bomb_cue) {
            std::vector<Bomb> now;
            const sim::Grid& grid = engine.grid();
            for (uint32_t y = 0; y < grid.height(); ++y) {
                for (uint32_t x = 0; x < grid.width(); ++x) {
                    if (engine.has_bomb_at(sim::TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)})) now.push_back(Bomb{static_cast<int32_t>(x), static_cast<int32_t>(y), grid.get_cell(x, y).interactive_owner});
                }
            }
            uint32_t gone_defused = defused > bombs_defused_ ? defused - bombs_defused_ : 0;
            for (const Bomb& old : bombs_) {
                const bool still = std::any_of(now.begin(), now.end(), [&](const Bomb& b) { return b.x == old.x && b.y == old.y; });
                if (still) continue;
                if (gone_defused > 0 && !bomb_cue) {
                    --gone_defused;                                     // taken away by an ant (the counter says so)
                    continue;
                }
                if (old.owner < kSeats) ++stats_.bombs_off[old.owner];
            }
            bombs_ = std::move(now);
            bombs_planted_ = planted;
            bombs_defused_ = defused;
        }

        // 5. a seat without an ant: wiped out (an egg gives it one for free), and out for good when it has no egg and no hatch either
        for (size_t s = 0; s < kSeats; ++s) {
            if (!plays(roster_, s) || turns <= 40) continue;
            if (alive[s] == 0 && !was_wiped_[s]) {
                was_wiped_[s] = true;
                ++stats_.wipes[s];
                if (stats_.wipe_turn[s] < 0) stats_.wipe_turn[s] = static_cast<int32_t>(turns);
            }
            if (alive[s] != 0) was_wiped_[s] = false;
            if (stats_.out_turn[s] < 0 && alive[s] == 0 && engine.get_player_eggs(static_cast<uint8_t>(s)) == 0 && engine.get_pending_hatch_count(static_cast<uint8_t>(s)) == 0) {
                stats_.out_turn[s] = static_cast<int32_t>(turns);
            }
        }

        // 6. every five seconds: the totals of every seat
        if (turns % MatchStats::kSampleTurns == 0) {
            MatchStats::Sample sample;
            sample.turn = turns;
            for (size_t s = 0; s < kSeats; ++s) {
                const sim::PlayerMatchStats& st = engine.stats_manager().get_player_stats(static_cast<uint8_t>(s));
                sample.box[s] = engine.get_display_score(static_cast<uint8_t>(s));
                sample.score[s] = st.score;
                sample.killed[s] = st.enemy_killed;
            }
            stats_.samples.push_back(sample);
        }
    } catch (const std::exception&) {
        fail("the world could not be read");
    }
}

StatsPlay play_with_stats(const Replay& replay, const assets::LevelData& level) {
    StatsPlay out;
    MatchStatsCollector collector(replay);
    out.outcome = play(replay, level, collector.hooks());
    out.stats = collector.take(out.outcome.turns);
    if (!out.outcome.ok) {
        out.stats.ok = false;
        if (out.stats.why.empty()) out.stats.why = out.outcome.error.empty() ? std::string("the match did not play out") : out.outcome.error;
    }
    return out;
}

}  // namespace ants::replay

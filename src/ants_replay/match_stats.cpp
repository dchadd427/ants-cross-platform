#include "ants_replay/match_stats.hpp"

#include <algorithm>
#include <cstdlib>
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

const char* MatchStats::action_name(Action kind) noexcept {
    switch (kind) {
        case Action::Death: return "death";
        case Action::Bomb: return "bomb";
        case Action::Theft: return "theft";
        case Action::Fire: return "fire";
        case Action::Hit: return "hit";
        case Action::Count: break;
    }
    return "?";
}

MatchStatsCollector::MatchStatsCollector(const Replay& replay) : MatchStatsCollector(replay.head.roster, replay.total_turns) {}

MatchStatsCollector::MatchStatsCollector(uint8_t roster, uint32_t expected_turns) : roster_(roster) {
    stats_.roster = roster;
    stats_.samples.reserve(expected_turns / MatchStats::kSampleTurns + 1);
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

// ---- the work of the special ants -------------------------------------------------------------------------------------------------------

// What the ant is doing that this pass counts, and the tile it works on: a Fire Ant putting a fire out, a Swimmer building a bridge or digging one away (what it IS, as the world shows it; the tile is the one the engine
// keeps for the ability's order, the original's +0xb0)
void MatchStatsCollector::work_of(const sim::AntSnapshot& snap, sim::SimulationEngine& engine, Work& kind, int32_t& x, int32_t& y) const {
    kind = Work::None;
    if (snap.type == sim::AntType::Fire && snap.action == sim::AntUnit::kActionExtinguish) kind = Work::PutOut;
    else if (snap.type == sim::AntType::Swimmer && snap.action == sim::AntUnit::kActionBridgeBuild) kind = Work::Build;
    else if (snap.type == sim::AntType::Swimmer && snap.action == sim::AntUnit::kActionBridgeDemolish) kind = Work::Dig;
    if (kind == Work::None) return;
    try {
        const sim::TileCoord tile = engine.get_unit(snap.id).orig_special_tile;
        x = tile.x;
        y = tile.y;
    } catch (const std::runtime_error&) {
        kind = Work::None;
    }
}

// True the first time a piece of work (a kind on a tile) is found done in this tick
bool MatchStatsCollector::first_to_settle(Work kind, int32_t x, int32_t y) {
    const std::array<int32_t, 3> key{static_cast<int32_t>(kind), x, y};
    if (std::find(settled_.begin(), settled_.end(), key) != settled_.end()) return false;
    settled_.push_back(key);
    return true;
}

// An ant that was at work at the last look: is the work done now? `now` is what it does at this look (None for an ant that is gone), judged by the tile (the engine keeps no count of its own that is raised):
//  - a fire is put out when it is gone and the ant is not at it any more, unless it was at its last tick (timer 1: it burns out before an ant's clip can end; a fire that never burns out, lit in the last 3 minutes,
//    has timer 0);
//  - a bridge is built when the tile has the fourth stage (the build ends in the same call that sets it: no look finds the ant still at work on a finished bridge, and a bridge that a dig that was cut short puts
//    back is the work of a Swimmer that digs, not of one that builds);
//  - a bridge is dug away when the tile has no bridge at all and the ant is not at it any more (a dig that is cut short puts the finished bridge back; a bridge that falls by itself under the dig ends the watch, in
//    note_work, so that an ant that is gone afterwards is not given the empty tile).
// Credit goes to the seat of the ant; two ants at one fire or one bridge are one fire put out or one bridge dug away.
void MatchStatsCollector::settle(Ant& ant, Work now, int32_t x, int32_t y, sim::SimulationEngine& engine) {
    if (ant.work == Work::None || ant.player >= kSeats) return;
    const sim::TileCoord tile{ant.work_x, ant.work_y};
    const bool still = now == ant.work && x == ant.work_x && y == ant.work_y;
    uint32_t* tally = nullptr;
    switch (ant.work) {
        case Work::PutOut:
            if (!still && !engine.has_fire_at(tile) && ant.work_state != 1) tally = &stats_.fires_out[ant.player];
            break;
        case Work::Build:
            if (engine.get_bridge_stage(tile) == 4) tally = &stats_.bridges_built[ant.player];
            break;
        case Work::Dig:
            if (!still && engine.get_bridge_stage(tile) == 0) tally = &stats_.bridges_undug[ant.player];
            break;
        case Work::None: break;
    }
    if (tally != nullptr && first_to_settle(ant.work, tile.x, tile.y)) ++*tally;
}

// What the tile is like now, for the next look: a fire's timer; and the end of the watch when there is nothing on the tile to work on any more
void MatchStatsCollector::note_work(Ant& ant, Work now, int32_t x, int32_t y, sim::SimulationEngine& engine) const {
    ant.work = now;
    if (now == Work::None) return;
    ant.work_x = x;
    ant.work_y = y;
    const sim::TileCoord tile{x, y};
    if (now == Work::PutOut) {
        if (!engine.has_fire_at(tile)) {                                // (nothing left to put out: it burnt out, or another ant was first)
            ant.work = Work::None;
            return;
        }
        ant.work_state = engine.get_fire_timer(tile);
    } else if (engine.get_bridge_stage(tile) == 0) {                    // (no bridge on the tile: it fell by itself, or the build was cut short)
        ant.work = Work::None;
    }
}

// More events of a kind in the bucket of the turn (a bucket is the 5 seconds of kSampleTurns turns)
void MatchStatsCollector::count(MatchStats::Action kind, uint32_t turns, uint32_t events) {
    const size_t bucket = turns / MatchStats::kSampleTurns;
    std::vector<uint32_t>& series = stats_.action[static_cast<size_t>(kind)];
    if (series.size() <= bucket) series.resize(bucket + 1);
    series[bucket] += events;
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
        settled_.clear();
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
            stats_.bombs_planted[s] = st.bombs_planted;
            stats_.fires_lit[s] = st.fires_lit;
            if (st.fires_lit > prev_fires_[s]) count(MatchStats::Action::Fire, turns, st.fires_lit - prev_fires_[s]);          // (a fire lit in this tick)
            prev_fires_[s] = st.fires_lit;
            if (!plays(roster_, s)) continue;
            int64_t cost = 0;
            if (hatched[s] > prev_hatched_[s] && engine.game_mode() != sim::GameMode::Kills187) cost = std::min<int64_t>(sim::HATCH_COST_POINTS, std::max<int32_t>(0, prev_score_[s]));
            const int64_t rise = int64_t{score[s]} - prev_score_[s] + cost;
            if (rise > 0) stats_.banked[s] += rise;
            stats_.hatch_cost[s] += cost;
        }
        prev_score_ = score;
        prev_hatched_ = hatched;

        // 2. the grid's size and the hills, looked at once (they never change while a match plays)
        if (stats_.map_width == 0) {
            stats_.map_width = engine.grid().width();
            stats_.map_height = engine.grid().height();
            for (size_t s = 0; s < kSeats; ++s) {
                const auto* hill = engine.grid().find_anthill(static_cast<uint8_t>(s));
                if (hill == nullptr) continue;
                stats_.hill_x[s] = hill->x;
                stats_.hill_y[s] = hill->y;
            }
        }

        // 3. the ants: hit points that fell, a type that changed, food picked up, a raid that began; and the ants that are gone
        const std::vector<sim::AntSnapshot>& ants = engine.get_ant_snapshots();          // (the ants alone: a catch-up asks for them hundreds of times a frame, and the grid is not copied for each)
        std::array<uint32_t, kSeats> alive{};
        for (const sim::AntSnapshot& snap : ants) {
            if (snap.id >= ants_.size()) ants_.resize(std::max<size_t>(ants_.size() * 2, size_t{snap.id} + 1));
            Ant& ant = ants_[snap.id];
            ant.last_seen = turns;
            if (snap.player_id < kSeats) ++alive[snap.player_id];
            if (!ant.alive) {                                           // a new ant (the ants of the start, a hatched one)
                ant = Ant{true, snap.player_id, static_cast<uint8_t>(snap.raw_type), snap.hp, snap.is_holding, snap.target_team_id, snap.tile_x, snap.tile_y, turns};
                ant.thief = snap.type == sim::AntType::Thief;
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
                    if (ant.hit_bucket != turns / MatchStats::kSampleTurns) {          // (once for an ant in a bucket: a fight that goes on does not make a bigger one)
                        ant.hit_bucket = turns / MatchStats::kSampleTurns;
                        count(MatchStats::Action::Hit, turns);
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
                    } else {
                        ant.loot = ant.raid;                            // (the loot of the raid it began; empty hands after a raid on a hill with no points never come here)
                        ant.raid = -1;
                    }
                } else if (!snap.is_holding && ant.holding && ant.loot >= 0) {      // its hands are empty again and it is still here: the loot went into its own hill (the clip of entering banks it); a thief that is killed on the way is gone, not here
                    if (static_cast<size_t>(ant.loot) < stats_.raids.size()) {
                        ++stats_.raids_home[snap.player_id];
                        stats_.loot_home[snap.player_id] += stats_.raids[static_cast<size_t>(ant.loot)].points;
                    }
                    ant.loot = -1;
                }
                if (snap.target_team_id < kSeats && ant.target_team >= kSeats && snap.type == sim::AntType::Thief) {   // (the field names a team only while a thief raids its hill)
                    int32_t points = 0;
                    try {
                        points = engine.get_unit(snap.id).raid_amount;
                    } catch (const std::runtime_error&) {
                    }
                    ant.raid = static_cast<int32_t>(stats_.raids.size());
                    stats_.raids.push_back(MatchStats::Raid{turns, snap.player_id, snap.target_team_id, points, snap.tile_x, snap.tile_y});
                    count(MatchStats::Action::Theft, turns);
                }
            }
            Work work = Work::None;                                     // what a Fire Ant or a Swimmer has done since the last look: a fire put out, a bridge built or dug away
            int32_t work_x = 0, work_y = 0;
            work_of(snap, engine, work, work_x, work_y);
            settle(ant, work, work_x, work_y, engine);
            note_work(ant, work, work_x, work_y, engine);
            ant.hp = snap.hp;
            ant.own_type = static_cast<uint8_t>(snap.raw_type);
            ant.holding = snap.is_holding;
            ant.target_team = snap.target_team_id;
            ant.thief = snap.type == sim::AntType::Thief;
            ant.tile_x = snap.tile_x;
            ant.tile_y = snap.tile_y;
        }
        const size_t first_new_kill = stats_.kills.size();
        for (size_t id = 1; id < ants_.size(); ++id) {
            Ant& ant = ants_[id];
            if (!ant.alive || ant.last_seen == turns) continue;
            settle(ant, Work::None, 0, 0, engine);                      // (an ant that is gone at its work: a death completes a put out or a dig, as the original's SetAction says)
            ant.work = Work::None;
            ant.alive = false;                                          // gone (RemoveAnt): the engine keeps the entry, with the team of the last damage
            MatchStats::Kill kill;
            kill.turn = turns;
            kill.victim = ant.player;
            kill.victim_type = ant.own_type;
            kill.tile_x = ant.tile_x;
            kill.tile_y = ant.tile_y;
            if (ant.thief) {                                            // (what it was doing at its last look: raiding a hill, carrying the loot of a raid)
                if (ant.target_team < kSeats) kill.raid_on = ant.target_team;
                kill.carried_loot = ant.loot >= 0;
            }
            try {
                const sim::AntUnit& unit = engine.get_unit(static_cast<uint32_t>(id));
                kill.drowned = unit.death_status == sim::DeathStatus::Drowned;
                if (unit.killer_team < kSeats && unit.killer_team != ant.player) kill.killer = unit.killer_team;     // (a team does not kill itself: the engine counts no kill then)
            } catch (const std::runtime_error&) {
            }
            if (ant.player < kSeats) {
                stats_.kills.push_back(kill);
                count(MatchStats::Action::Death, turns);
            }
        }

        // the ants of a victim's own seat that are still about it (a group that is wiped out leaves none): looked at in the snapshot of this tick, so the ants that fell in it are not counted
        for (size_t k = first_new_kill; k < stats_.kills.size(); ++k) {
            MatchStats::Kill& kill = stats_.kills[k];
            uint32_t mates = 0;
            for (const sim::AntSnapshot& snap : ants) {
                if (snap.player_id == kill.victim && std::max(std::abs(snap.tile_x - kill.tile_x), std::abs(snap.tile_y - kill.tile_y)) <= MatchStats::kMatesTiles) ++mates;
            }
            kill.mates_near = static_cast<uint8_t>(std::min<uint32_t>(mates, 255u));
        }

        // 4. the bombs on the map: which went off
        track_bombs(engine, turns, planted, defused, ants);

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

// The bombs that lie on the map, row by row: tile and planter
std::vector<MatchStatsCollector::Spot> MatchStatsCollector::scan_bombs(sim::SimulationEngine& engine) {
    std::vector<Spot> out;
    const sim::Grid& grid = engine.grid();
    for (uint32_t y = 0; y < grid.height(); ++y) {
        for (uint32_t x = 0; x < grid.width(); ++x) {
            const sim::TileCell& cell = grid.get_cell(x, y);
            if (cell.has_bomb()) out.push_back(Spot{static_cast<int32_t>(x), static_cast<int32_t>(y), cell.interactive_owner});
        }
    }
    return out;
}

// The bombs: a bomb leaves the map only when an ant takes it away (the engine's count of bombs defused says so) or when an ant walks onto it and it goes off, so the bombs that are gone since the last look, less the ones that the
// count of defused bombs rose by, went off (and, whatever a game queued in its cues, nothing of them is read here). New bombs come only with the count of bombs planted rising: the map is looked through then, and in no other tick
// (a bomb that is known is asked about by its tile, a few asks a tick). When a bomb was defused in the tick in which another went off nothing in the counts says which was which, but the ant that a bomb throws stands on the
// bomb's tile in the action of a bomb's victim (the world of that tick shows it) and the ant that defuses stands beside it; if that does not settle it either the first ones in the order of the map are taken to be the defused.
void MatchStatsCollector::track_bombs(sim::SimulationEngine& engine, uint32_t turns, uint32_t planted, uint32_t defused, const std::vector<sim::AntSnapshot>& ants) {
    std::vector<Spot> gone;
    if (planted != bombs_planted_) {
        std::vector<Spot> now = scan_bombs(engine);
        for (const Spot& old : bombs_) {
            const bool still = std::any_of(now.begin(), now.end(), [&](const Spot& b) { return b.x == old.x && b.y == old.y; });
            if (!still) gone.push_back(old);
        }
        bombs_ = std::move(now);
    } else if (!bombs_.empty()) {
        std::vector<Spot> kept;
        for (const Spot& old : bombs_) {
            (engine.has_bomb_at(sim::TileCoord{old.x, old.y}) ? kept : gone).push_back(old);
        }
        bombs_ = std::move(kept);
    }
    const size_t taken_away = defused > bombs_defused_ ? defused - bombs_defused_ : 0;
    bombs_planted_ = planted;
    bombs_defused_ = defused;
    if (gone.size() <= taken_away) return;                               // (every bomb that left was taken away)
    std::vector<Spot> off = gone;
    if (taken_away > 0) {
        off.clear();
        for (const Spot& bomb : gone) {
            const bool struck = std::any_of(ants.begin(), ants.end(), [&](const sim::AntSnapshot& ant) { return ant.tile_x == bomb.x && ant.tile_y == bomb.y && ant.action == sim::AntUnit::kActionBlast; });
            if (struck) off.push_back(bomb);
        }
        if (off.size() != gone.size() - taken_away) off.assign(gone.begin() + static_cast<std::ptrdiff_t>(taken_away), gone.end());      // (not settled)
    }
    for (const Spot& bomb : off) {
        if (bomb.owner >= kSeats) continue;                              // (a bomb of the map: nobody planted it)
        ++stats_.bombs_off[bomb.owner];
        count(MatchStats::Action::Bomb, turns);
    }
}

StatsPlayback::StatsPlayback(const Replay& replay, const assets::LevelData& level) : collector_(replay), playback_(replay, level, collector_.hooks()) {}

StatsPlay StatsPlayback::take() {
    StatsPlay out;
    out.outcome = playback_.outcome();
    out.stats = collector_.take(out.outcome.turns);
    if (!out.outcome.ok) {
        out.stats.ok = false;
        if (out.stats.why.empty()) out.stats.why = out.outcome.error.empty() ? std::string("the match did not play out") : out.outcome.error;
    }
    return out;
}

StatsPlay play_with_stats(const Replay& replay, const assets::LevelData& level) {
    StatsPlayback playback(replay, level);
    while (!playback.advance(replay.total_turns + 1u)) {
    }
    return playback.take();
}

}  // namespace ants::replay

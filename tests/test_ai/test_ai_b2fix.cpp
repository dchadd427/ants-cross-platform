// Tests that came out of the adversarial review of milestone B2 (docs/audit/B2_notes.md, "Review"): each of them fails without the fix of the finding it belongs to.
//
//   AI1.21  the state of another team's ants is what the screen draws (an ant that has an order but still stands in its idle clip is idle)
//   AI1.21b the same on a level with a default ant type: the guard of a default Combat ant, swimming for a default swimmer on its lake
//   AI1.22  takes_orders() against the engine for swimmers (diving in, climbing out), stunned ants and combat ants at rest
//   AI1.23  the score boxes of a team that dropped out or is not in the match are covered: 0
//   AI1.24  a lunchbox is worth the same to every bot (its real value is the dead ant's hidden carried points)
//   AI1.5b  predict_ack against apply_command: the issuer of another team, and the documented exceptions (differential, with typed ants, water, dropped, off the map)
//   AI1.25  non-interference: two engines that differ only in hidden data give the same view of the observing seat
//   AI1.26  ticks_left() against the engine's clock, TeamRow::present against the roster
//   AI1.27 .. AI1.35  MapInfo: the dynamic rules of approach_now, the queue row, the first hill, the last step, row 90, lunchboxes, sums, conformance cases, walking time
//   AI4.5 .. AI4.12   the arena: sink latency model, result fields, refusals, counters, replay checks
#include "ai_test.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <set>

#include "ants_ai/arena.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/rng.hpp"
#include "ants_sim/movement_tables.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) {
    for (const AntView& a : ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

Command move_of(uint8_t issuer, std::vector<uint32_t> ants, int16_t x, int16_t y, CommandType type = CommandType::GroupMove) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

bool walking_family(sim::UnitState s) { return s == sim::UnitState::Walking || s == sim::UnitState::DivingInWater || s == sim::UnitState::ExitingWater; }

// An oracle for "the screen draws this ant walking" that does not share the production table: the clip that is playing moves the ant (some frame has a displacement)
bool clip_moves(uint16_t chd) {
    if (chd >= sim::movement::kTileIdCount) return false;
    const sim::movement::MotionClip c = sim::movement::clip_by_chd(chd, false);
    for (uint16_t i = 0; c.valid() && i < c.count; ++i) {
        if (c.dx(i) != 0 || c.dy(i) != 0) return true;
    }
    return false;
}

const sim::AntSnapshot* snapshot_of(const sim::SimulationEngine& sim, uint32_t id) {
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

void water_rect(sim::SimulationEngine& sim, int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
    }
}

struct Walk {
    bool delivered{false};
    bool failed{false};
    std::vector<TileCoord> path;
    uint32_t ant{0};
};

// Puts a worker of `team` on `from`, gives it the group move to `to` and waits until the path manager has answered: the path it delivered, or a failure
Walk order_and_wait(sim::SimulationEngine& sim, uint8_t team, TileCoord from, TileCoord to, uint32_t max_ticks = 120) {
    Walk w;
    sim.set_locomotion_trace_enabled(true);
    w.ant = sim.spawn_unit(team, sim::AntType::Worker, from);
    sim.apply_command(move_of(team, {w.ant}, static_cast<int16_t>(to.x), static_cast<int16_t>(to.y)));
    size_t seen = sim.locomotion_trace().size();
    for (uint32_t t = 0; t < max_ticks && !w.delivered && !w.failed; ++t) {
        sim.tick();
        const auto& trace = sim.locomotion_trace();
        for (; seen < trace.size(); ++seen) {
            if (trace[seen].ant_id != w.ant) continue;
            if (trace[seen].kind == sim::LocoTraceEvent::Kind::PathDelivered) w.delivered = true;
            if (trace[seen].kind == sim::LocoTraceEvent::Kind::PathFailed) w.failed = true;
        }
    }
    if (w.delivered) w.path = sim.get_unit(w.ant).waypoints;
    return w;
}

int64_t path_cost(const sim::Grid& grid, const std::vector<TileCoord>& path) {
    int64_t sum = 0;
    for (size_t i = 1; i < path.size(); ++i) sum += MapInfo::step_cost(grid, path[i - 1], path[i]);
    return sum;
}

// A worker sent to `click` (an ant of `team` that stands on `from`, nobody else on the field) harvests by itself: the points the team has banked after `ticks`
int32_t harvest_for(sim::SimulationEngine& sim, uint8_t team, TileCoord from, TileCoord click, uint32_t ticks) {
    const uint32_t worker = sim.spawn_unit(team, sim::AntType::Worker, from);
    sim.apply_command(move_of(team, {worker}, static_cast<int16_t>(click.x), static_cast<int16_t>(click.y)));
    for (uint32_t t = 0; t < ticks; ++t) {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
    }
    return sim.get_player_score(team);
}

// A world with every kind of ant (12 per team, the six types twice), a 20 x 20 lake, a block of rocks, two bombs and a fire wall
void build_rich_world(sim::SimulationEngine& sim, uint32_t seed) {
    sim.init_test_world(60, 60, seed, 720000);
    const TileCoord hills[sim::MAX_PLAYERS] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    water_rect(sim, 20, 20, 39, 39);
    for (int y = 28; y < 34; ++y) {
        for (int x = 8; x < 18; ++x) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
    }
    sim.grid_mut().place_bomb(14, 20, 0);
    sim.grid_mut().place_bomb(30, 15, 1);
    sim.set_fire_at(tc(15, 15), 3000);
    const sim::AntType types[6] = {sim::AntType::Worker, sim::AntType::Bomber, sim::AntType::Fire, sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer};
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        const int32_t sx = hills[p].x < 30 ? 1 : -1;
        const int32_t sy = hills[p].y < 30 ? 1 : -1;
        for (int32_t i = 0; i < 12; ++i) sim.spawn_unit(p, types[i % 6], tc(hills[p].x + sx * (1 + i % 6), hills[p].y + sy * (6 + i / 6)));
    }
}

// ---- a view as a list of bytes, field by field, for the non-interference test ----------------------------------------------------------------------------------------------------

// Adding a field to one of these structs changes its size and stops the build here: put the field into same_view / the dump below, then change the number.
static_assert(sizeof(AntView) == 24, "AntView changed: extend view_text() and AI1.25");
static_assert(sizeof(TeamRow) == 12, "TeamRow changed: extend view_text() and AI1.25");
static_assert(sizeof(PileView) == 20, "PileView changed: extend view_text() and AI1.25");
static_assert(sizeof(PowerUpView) == 16, "PowerUpView changed: extend view_text() and AI1.25");
static_assert(sizeof(BombView) == 12, "BombView changed: extend view_text() and AI1.25");
static_assert(sizeof(FireWallView) == 8, "FireWallView changed: extend view_text() and AI1.25");

std::string view_text(const BotView& v) {
    std::string s;
    const auto put = [&s](int64_t value) { s += std::to_string(value) + ","; };
    put(static_cast<int64_t>(v.tick()));
    put(v.ticks_left());
    put(v.seat());
    put(v.score());
    put(v.ally());
    put(v.invite_from());
    put(v.eggs());
    put(v.hatching() ? 1 : 0);
    for (const TeamRow& r : v.rows()) {
        put(r.present);
        put(r.dropped);
        put(r.score);
        put(r.ally);
        s += ";";
    }
    for (const std::vector<AntView>* list : {&v.mine(), &v.others()}) {
        for (const AntView& a : *list) {
            put(a.id);
            put(a.team);
            put(static_cast<int>(a.type));
            put(a.tile.x);
            put(a.tile.y);
            put(a.hp);
            put(static_cast<int>(a.state));
            put(a.holding ? 1 : 0);
            put(a.carried_points);
            s += "/";
        }
        s += ";";
    }
    for (const PileView& p : v.piles()) {
        put(p.index);
        put(p.anchor.x);
        put(p.anchor.y);
        put(p.remaining);
        put(p.value);
        put(p.lunchbox ? 1 : 0);
        s += "/";
    }
    s += "P";
    for (const PowerUpView& p : v.powerups()) {
        put(p.tile.x);
        put(p.tile.y);
        put(static_cast<int>(p.kind));
        put(p.standing_team);
        put(p.standing_ant);
        s += "/";
    }
    s += "B";
    for (const BombView& b : v.bombs()) {
        put(b.tile.x);
        put(b.tile.y);
        put(b.owner);
        s += "/";
    }
    s += "F";
    for (const FireWallView& w : v.fire_walls()) {
        put(w.tile.x);
        put(w.tile.y);
        s += "/";
    }
    return s;
}

// ---- scripted bots for the arena tests ------------------------------------------------------------------------------------------------------------------------------------------

BotSpec seat_spec(uint8_t seat, const char* kind, Level level) {
    BotSpec s;
    s.seat = seat;
    s.kind = kind;
    s.level = level;
    return s;
}

// A scripted walker without a random generator of its own: at every look it sends one of its ants to a tile that depends on the tick alone. It remembers when each of its commands was
// released (on_command), and at every look which of its ants still have a path request queued.
class Walker final : public Bot {
public:
    struct Notes {
        std::vector<uint64_t> sent;                                  // the tick of every released command, in release order
        std::vector<std::pair<uint64_t, std::set<uint32_t>>> looks;  // (tick, own ants with a path request queued) of every look
        uint64_t rng_seed{0};
    };
    explicit Walker(Notes* notes = nullptr) : notes_(notes) {}
    const char* kind() const noexcept override { return "walker"; }
    void start(const BotContext& c) override {
        if (notes_ != nullptr) notes_->rng_seed = c.rng_seed;
    }
    void think(const BotView& view, Orders& orders) override {
        if (notes_ != nullptr) {
            std::set<uint32_t> pending;
            for (const AntView& a : view.mine()) {
                if (view.has_pending_path(a.id)) pending.insert(a.id);
            }
            notes_->looks.emplace_back(view.tick(), pending);
        }
        if (view.mine().empty()) return;
        const uint64_t t = view.tick();
        const AntView& a = view.mine()[(t / 4) % view.mine().size()];
        const int32_t w = static_cast<int32_t>(view.grid().width());
        const int32_t h = static_cast<int32_t>(view.grid().height());
        const int32_t x = std::clamp(a.tile.x + static_cast<int32_t>((t * 7) % 15) - 7, 0, w - 1);
        const int32_t y = std::clamp(a.tile.y + static_cast<int32_t>((t * 3) % 15) - 7, 0, h - 1);
        orders.move({a.id}, TileCoord{x, y});
    }
    void on_command(const Command&, Fate fate, uint64_t tick) override {
        if (notes_ != nullptr && fate == Fate::Sent) notes_->sent.push_back(tick);
    }

private:
    Notes* notes_;
};

// A bot that harvests: idle ants go to the nearest pile that has units left (MapInfo's click tile), and with `hatch` it hatches an egg as soon as it can pay for one
class Harvester final : public Bot {
public:
    explicit Harvester(bool hatch) : hatch_(hatch) {}
    const char* kind() const noexcept override { return "harvester"; }
    void start(const BotContext&) override {}
    void think(const BotView& v, Orders& o) override {
        if (hatch_ && v.score() >= 200 && v.eggs() > 0 && !v.hatching()) o.hatch();
        const MapInfo* map = v.map();
        if (map == nullptr) return;
        std::vector<uint32_t> idle;
        for (const AntView& a : v.mine()) {
            if (a.idle()) idle.push_back(a.id);
        }
        if (idle.empty()) return;
        const PileInfo* best = nullptr;
        for (const PileView& p : v.piles()) {
            const PileInfo* info = map->pile(p.index);
            if (p.lunchbox || info == nullptr || !info->approach[v.seat()].reachable()) continue;
            if (best == nullptr || info->approach[v.seat()].cost < best->approach[v.seat()].cost) best = info;
        }
        if (best != nullptr) o.move(idle, best->approach[v.seat()].click);
    }

private:
    bool hatch_;
};

// A bot that fights: every own ant attacks the foreign ant that is nearest to the first own ant
class Fighter final : public Bot {
public:
    const char* kind() const noexcept override { return "fighter"; }
    void start(const BotContext&) override {}
    void think(const BotView& v, Orders& o) override {
        if (v.mine().empty() || v.others().empty()) return;
        std::vector<uint32_t> ids;
        for (const AntView& a : v.mine()) ids.push_back(a.id);
        const AntView& me = v.mine().front();
        const AntView* best = nullptr;
        int32_t best_d = 1 << 30;
        for (const AntView& f : v.others()) {
            const int32_t d = std::max(std::abs(f.tile.x - me.tile.x), std::abs(f.tile.y - me.tile.y));
            if (d < best_d) {
                best_d = d;
                best = &f;
            }
        }
        if (best != nullptr) o.attack(ids, best->tile);
    }
};

struct EngineNumbers {
    int32_t score{0};
    int32_t shown{0};
    uint32_t eggs{0};
    uint32_t hatched{0};
    uint32_t kills{0};
    uint32_t losses{0};
    uint32_t alive{0};
};

// What the engine itself says about the seats at the end of a match (the hook of ArenaSpec::inspect), read with the getters and NOT with read_seat_result
std::array<EngineNumbers, sim::MAX_PLAYERS> numbers_of(const sim::SimulationEngine& sim) {
    std::array<EngineNumbers, sim::MAX_PLAYERS> out{};
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        out[t].score = sim.get_player_score(t);
        out[t].shown = sim.get_display_score(t);
        out[t].eggs = sim.get_player_eggs(t);
        out[t].hatched = sim.get_player_hatched(t);
        out[t].kills = sim.get_player_stats(t).enemy_killed;
        out[t].losses = sim.get_player_stats(t).friendly_lost;
        for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
            if (a.player_id == t && a.hp > 0 && a.state != sim::UnitState::Dead && a.state != sim::UnitState::Drowning) ++out[t].alive;
        }
    }
    return out;
}

ArenaSpec arena_spec(const std::string& map, uint32_t seed, uint64_t ticks, uint32_t latency, const std::vector<BotSpec>& bots) {
    ArenaSpec s;
    s.level = &level_of(map);
    s.seed = seed;
    s.max_ticks = ticks;
    s.latency_ticks = latency;
    s.bots = bots;
    return s;
}

}  // namespace

void run_b2fix_tests() {
    TEST_CASE("AI1.21 The State Of Another Team's Ants Is What The Screen Draws: An Ant That Has Been Ordered But Still Stands In Its Idle Clip (Path Not Delivered, Or Waiting Behind A Blocker) Is Idle, A Walking Clip Is Walking; Own Ants Keep The Engine's Label") {
        sim::SimulationEngine sim;
        build_world(sim, 31, 24);
        water_rect(sim, 24, 40, 34, 46);
        const uint32_t combat = sim.spawn_unit(2, sim::AntType::Combat, TileCoord{30, 36});
        const uint32_t swimmer = sim.spawn_unit(3, sim::AntType::Swimmer, TileCoord{28, 43});
        const std::vector<uint32_t> team0 = ants_of(sim, 0);
        const std::vector<uint32_t> team1 = ants_of(sim, 1);
        ASSERT_EQ(team1.size(), 24u);
        // two crowds sent to the same place (they meet, block each other and wait), a combat ant and a swimmer in a lake sent away
        ASSERT_TRUE(sim.apply_command(move_of(1, team1, 30, 30)).accepted());
        ASSERT_TRUE(sim.apply_command(move_of(0, team0, 31, 30)).accepted());
        ASSERT_TRUE(sim.apply_command(move_of(2, {combat}, 30, 10)).accepted());
        ASSERT_TRUE(sim.apply_command(move_of(3, {swimmer}, 28, 10)).accepted());
        size_t hidden = 0;                                                                // ant-ticks where the engine says "walking" and the screen shows a standing ant
        size_t shown = 0;                                                                 // ant-ticks that really walk
        size_t pending_listed = 0;
        for (int t = 1; t <= 420; ++t) {
            sim.tick();
            if (t == 1) {
                // the order was given a moment ago: the engine says walking for every ordered ant, no path is delivered yet for 23 of the 24, and the sprites all stand
                size_t engine_walking = 0;
                for (uint32_t id : team1) engine_walking += snapshot_of(sim, id)->state == sim::UnitState::Walking ? 1u : 0u;
                ASSERT_EQ(engine_walking, 24u);
                const BotView v = BotView::build(sim, 0);
                size_t view_walking = 0;
                for (const AntView& a : v.others()) view_walking += (a.team == 1 && a.state == sim::UnitState::Walking) ? 1u : 0u;
                ASSERT_EQ(view_walking, 0u);
                const AntView* c = find_ant(v.others(), combat);
                const AntView* s = find_ant(v.others(), swimmer);
                ASSERT_TRUE(c != nullptr && s != nullptr);
                ASSERT_TRUE(snapshot_of(sim, combat)->state == sim::UnitState::Walking && snapshot_of(sim, swimmer)->state == sim::UnitState::Walking);   // (the engine's label)
                ASSERT_TRUE(c->state == sim::UnitState::GuardIdle);                       // a combat ant that stands is the guard
                ASSERT_TRUE(s->state == sim::UnitState::Swimming);                        // a swimmer that floats on its lake is swimming at rest
            }
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{3}}) {
                const BotView v = BotView::build(sim, seat);
                for (const AntView& a : v.others()) {
                    const sim::AntSnapshot* e = snapshot_of(sim, a.id);
                    ASSERT_TRUE(e != nullptr);
                    const bool drawn_walking = walking_family(e->state) && clip_moves(e->loco_clip);
                    ASSERT_EQ(walking_family(a.state), drawn_walking);
                    if (sim.has_pending_path(a.id)) {
                        ++pending_listed;
                        ASSERT_FALSE(walking_family(a.state));                            // an order whose path is still queued shows nowhere
                    }
                    if (seat == 3) {
                        if (walking_family(e->state) && !drawn_walking) ++hidden;
                        if (drawn_walking) ++shown;
                    }
                }
                for (const AntView& a : v.mine()) {
                    const sim::AntSnapshot* e = snapshot_of(sim, a.id);
                    ASSERT_TRUE(e != nullptr && a.state == e->state);                     // an own ant: the engine's label, whatever the sprite shows
                }
            }
        }
        ASSERT_TRUE(hidden > 100);                                                        // the scenario did produce ordered-but-standing ants (queued paths and blocked ants)
        ASSERT_TRUE(shown > 1000);                                                        // and the walking ants are still seen walking
        ASSERT_TRUE(pending_listed > 100);
    } TEST_END();

    TEST_CASE("AI1.21b On A Level With A Default Ant Type The Ordered-But-Standing Ant Of Another Team Reads As The Idle Label Of The Type It Is: The Guard For A Default Combat Ant, Swimming For A Default Swimmer On Its Lake") {
        // seen_state() reads the type that the sprite shows (AntView::type: the ant's own type, the level's default for an ant that never took a power-up), not the own type field: a worker
        // of a default Combat level that has just been ordered and still stands in its idle clip is the guard, and a default swimmer that floats on a lake is swimming at rest
        for (const uint16_t tile : {uint16_t{62}, uint16_t{65}}) {
            sim::SimulationEngine sim;
            build_world(sim, 31, 0);
            sim.grid_mut().set_default_ant_tile(tile);
            water_rect(sim, 24, 40, 34, 46);
            const uint32_t id = sim.spawn_unit(3, sim::AntType::Worker, tile == 62 ? TileCoord{30, 36} : TileCoord{28, 43});
            ASSERT_TRUE(sim.apply_command(move_of(3, {id}, 30, 10)).accepted());
            sim.tick();                                                                   // ordered a moment ago: the engine says walking, the sprite still stands in its idle clip
            ASSERT_TRUE(snapshot_of(sim, id)->state == sim::UnitState::Walking);
            const BotView v = BotView::build(sim, 0);
            const AntView* seen = find_ant(v.others(), id);
            ASSERT_TRUE(seen != nullptr);
            ASSERT_TRUE(seen->type == (tile == 62 ? sim::AntType::Combat : sim::AntType::Swimmer));
            ASSERT_TRUE(seen->state == (tile == 62 ? sim::UnitState::GuardIdle : sim::UnitState::Swimming));
        }
    } TEST_END();

    TEST_CASE("AI1.22 takes_orders() Against The Engine: A Swimmer Diving Into The Water Or Climbing Out Of It Takes Orders (The Engine Maps Both To The Walking Action), So Does A Stunned Ant And A Combat Ant At Rest") {
        // a swimmer crosses a lake: at every tick takes_orders() false means the engine refuses, and the two water states take orders
        {
            sim::SimulationEngine sim;
            build_world(sim, 32, 2);
            water_rect(sim, 24, 20, 33, 39);
            const uint32_t swimmer = sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{20, 30});
            ASSERT_TRUE(sim.apply_command(move_of(0, {swimmer}, 40, 30)).accepted());
            std::map<sim::UnitState, int> seen;
            for (int t = 0; t < 400; ++t) {
                sim.tick();
                const BotView v = BotView::build(sim, 0);
                const AntView* a = find_ant(v.mine(), swimmer);
                ASSERT_TRUE(a != nullptr);
                ++seen[a->state];
                const uint32_t ack = v.predict_ack(move_of(0, {swimmer}, 3, 30));
                if (!a->takes_orders()) ASSERT_EQ(ack, 0u);
                if (a->state == sim::UnitState::DivingInWater || a->state == sim::UnitState::ExitingWater) {
                    ASSERT_TRUE(a->takes_orders());
                    ASSERT_EQ(ack, swimmer);                                              // the engine acknowledges: the hint must not say no
                }
            }
            ASSERT_TRUE(seen[sim::UnitState::DivingInWater] >= 5 && seen[sim::UnitState::ExitingWater] >= 5);
        }
        // a stunned ant (a dud bomb blows it up, it burns, then it is stunned) and a combat ant at rest (guard)
        bool stunned_tested = false;
        for (uint32_t seed = 1; seed < 100 && !stunned_tested; ++seed) {
            sim::SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 720000);
            const uint32_t worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{15, 15});
            sim.grid_mut().place_bomb(15, 15, 1);
            sim.trigger_bomb_detonation(worker, TileCoord{15, 15});
            if (!sim.get_unit(worker).knock_flag) continue;                               // a real blast, not a flight: the dud ends in a stun
            bool saw = false;
            for (int t = 0; t < 400 && !saw; ++t) {
                sim.tick();
                const BotView v = BotView::build(sim, 0);
                const AntView* a = find_ant(v.mine(), worker);
                if (a == nullptr || a->state != sim::UnitState::Stunned) continue;
                saw = true;
                ASSERT_TRUE(a->takes_orders());
                ASSERT_EQ(v.predict_ack(move_of(0, {worker}, 40, 40)), worker);
            }
            stunned_tested = saw;
        }
        ASSERT_TRUE(stunned_tested);
        sim::SimulationEngine sim;
        build_world(sim, 33, 2);
        const uint32_t guard = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{20, 20});
        sim.tick();
        const BotView v = BotView::build(sim, 0);
        const AntView* g = find_ant(v.mine(), guard);
        ASSERT_TRUE(g != nullptr && g->state == sim::UnitState::GuardIdle);
        ASSERT_TRUE(g->takes_orders() && g->idle());
        ASSERT_EQ(v.predict_ack(move_of(0, {guard}, 40, 40)), guard);
    } TEST_END();

    TEST_CASE("AI1.23 The Score Box Of A Team That Dropped Out Or Is Not In The Match Is Covered: The View Says 0 For It, However The Engine's Score Moves (A Raid On Its Hill); A Live Team Shows Its Number") {
        sim::SimulationEngine sim;
        start_match(sim, "TINY", 1, 0x07);                                                   // seats 0, 1 and 2 play, seat 3 does not
        sim.set_player_score(0, 120);
        sim.set_player_score(1, 80);
        sim.set_player_score(2, 300);
        sim.set_player_score(3, 40);
        sim.tick();
        const BotView before = BotView::build(sim, 0);
        ASSERT_TRUE(before.rows()[0].present && before.rows()[1].present && before.rows()[2].present && !before.rows()[3].present);
        ASSERT_EQ(before.score(), 120);
        ASSERT_EQ(before.rows()[1].score, 80);
        ASSERT_EQ(before.rows()[2].score, 300);
        ASSERT_EQ(sim.get_player_score(3), 40);                                              // (the engine holds a number for the seat that does not play ...)
        ASSERT_EQ(before.rows()[3].score, 0);                                                // ... the view does not: no hill, a covered box
        sim.drop_player(2);
        sim.tick();
        ASSERT_EQ(sim.get_player_score(2), 300);                                             // the engine still counts it
        const BotView after = BotView::build(sim, 0);
        ASSERT_TRUE(after.rows()[2].dropped);
        ASSERT_EQ(after.rows()[2].score, 0);                                                 // the box is covered: nothing is on any screen
        sim.set_player_score(2, 250);                                                        // a thief of team 0 raids the dropped hill
        ASSERT_EQ(BotView::build(sim, 0).rows()[2].score, 0);
        ASSERT_EQ(BotView::build(sim, 0).rows()[1].score, 80);
        ASSERT_EQ(BotView::build(sim, 2).score(), 0);                                        // a seat that dropped has no number of its own either
        ASSERT_EQ(BotView::build(sim, 1).score(), 80);
    } TEST_END();

    TEST_CASE("AI1.24 A Lunchbox Is Worth The Same To Every Bot: The Real Value (What The Dead Ant Carried) Is In No Accessor, A Pile Keeps Its Points Per Unit") {
        sim::SimulationEngine sim;
        build_world(sim, 34);
        const int32_t crackers = place_crackers(sim, 20, 20, 25);
        sim.grid_mut().drop_lunchbox(25, 35, 40);
        sim.grid_mut().drop_lunchbox(26, 36, 7);
        sim.tick();
        ASSERT_EQ(sim.grid().get_lunchbox_points(tc(25, 35)), 40u);                           // the engine knows ...
        ASSERT_EQ(sim.grid().get_lunchbox_points(tc(26, 36)), 7u);
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            const BotView v = BotView::build(sim, seat);
            size_t boxes = 0;
            for (const PileView& p : v.piles()) {
                if (p.lunchbox) {
                    ++boxes;
                    ASSERT_EQ(p.value, kLunchboxNominalValue);                               // ... a bot is told the same figure for both
                    ASSERT_EQ(p.remaining, 1);
                } else {
                    ASSERT_TRUE(p.index == static_cast<uint32_t>(crackers) && p.value == 25);
                }
            }
            ASSERT_EQ(boxes, 2u);
        }
    } TEST_END();

    TEST_CASE("AI1.5b predict_ack Against apply_command: The Issuer Of Another Team Is Never Believed (From Every Seat, About Every Other Team's Ants); A Differential Over Typed Ants, Water, Rocks, Bombs, A Dropped Team And Tiles Off The Map Agrees Except In The Two Documented Ways") {
        // (1) the fairness rule: a command that names another team's ants and carries that team as issuer gets an answer from the engine but none from the view
        {
            sim::SimulationEngine sim;
            build_world(sim, 41);
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                const BotView v = BotView::build(sim, seat);
                for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
                    if (team == seat) continue;
                    const std::vector<uint32_t> theirs = ants_of(sim, team);
                    const Command c = move_of(team, {theirs[0], theirs[1], theirs[2]}, 30, 30);
                    uint32_t engine_needed = 0;
                    ASSERT_TRUE(sim.predict_order_ack(c, &engine_needed) != 0 && engine_needed == 3);      // the engine alone does answer for team `team` ...
                    uint32_t needed = 99;
                    ASSERT_EQ(v.predict_ack(c, &needed), 0u);                                               // ... the view of another seat does not, whoever the command says it is from
                    ASSERT_EQ(needed, 0u);
                }
            }
        }
        // (2) exception 1, a special order onto a tile where a WALKING ant stands: predicted none (the cursor's rule), acknowledged by apply_command (the order's rule)
        for (const sim::AntType type : {sim::AntType::Bomber, sim::AntType::Fire}) {
            for (const uint8_t walker_team : {uint8_t{0}, uint8_t{1}}) {
                sim::SimulationEngine sim;
                build_world(sim, 42, 1);
                const uint32_t special = sim.spawn_unit(0, type, tc(10, 20));
                const uint32_t walker = sim.spawn_unit(walker_team, sim::AntType::Worker, tc(14, 30));
                ASSERT_TRUE(sim.apply_command(move_of(walker_team, {walker}, 50, 30)).accepted());
                TileCoord at{-1, -1};
                for (int t = 0; t < 120 && at.x < 0; ++t) {
                    sim.tick();
                    const sim::AntSnapshot* w = snapshot_of(sim, walker);
                    if (w != nullptr && w->state == sim::UnitState::Walking && clip_moves(w->loco_clip) && w->tile_x >= 24) at = tc(w->tile_x, w->tile_y);
                }
                ASSERT_TRUE(at.x >= 24);
                const Command c = move_of(0, {special}, static_cast<int16_t>(at.x), static_cast<int16_t>(at.y), CommandType::GroupSpecial);
                uint32_t needed = 0;
                const uint32_t predicted = BotView::build(sim, 0).predict_ack(c, &needed);
                const sim::CommandResult r = sim.apply_command(c);
                ASSERT_EQ(predicted, 0u);
                ASSERT_EQ(r.ack_ant, special);                                                              // the ant acknowledges: 0 was not a promise of a refusal
            }
        }
        // (3) exception 2, a goal that the engine's goal adjustment refuses: water with nothing enterable within four tiles. Near the shore both agree, a swimmer is asked and agrees.
        {
            sim::SimulationEngine sim;
            build_world(sim, 43, 2);
            water_rect(sim, 25, 25, 44, 44);
            const uint32_t worker = ants_of(sim, 0)[0];
            const uint32_t swimmer = sim.spawn_unit(0, sim::AntType::Swimmer, tc(10, 10));
            sim.tick();
            const BotView v = BotView::build(sim, 0);
            const auto both = [&](const Command& c) {
                uint32_t needed = 0;
                const uint32_t predicted = v.predict_ack(c, &needed);
                const sim::CommandResult r = sim.apply_command(c);
                return std::make_pair(predicted, r.ack_ant);
            };
            const auto far = both(move_of(0, {worker}, 35, 35));
            ASSERT_TRUE(far.first == worker && far.second == 0u);                                          // predicted as acknowledged, refused by the goal adjustment (status Applied)
            sim.tick();
            const BotView v2 = BotView::build(sim, 0);
            const uint32_t near_predicted = v2.predict_ack(move_of(0, {worker}, 26, 35));
            ASSERT_EQ(near_predicted, sim.apply_command(move_of(0, {worker}, 26, 35)).ack_ant);            // within four tiles of the shore the goal moves to the shore: both agree
            ASSERT_EQ(near_predicted, worker);
            sim.tick();
            const BotView v3 = BotView::build(sim, 0);
            ASSERT_EQ(v3.predict_ack(move_of(0, {swimmer}, 35, 35)), sim.apply_command(move_of(0, {swimmer}, 35, 35)).ack_ant);       // a swimmer may go there
        }
        // (4) a dropped issuer and tiles off the map: nothing is predicted, nothing acknowledged
        {
            sim::SimulationEngine sim;
            build_world(sim, 44);
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            sim.tick();
            const BotView v = BotView::build(sim, 0);
            for (const TileCoord off : {tc(-1, 5), tc(5, -1), tc(60, 5), tc(5, 60), tc(300, 300)}) {
                const Command c = move_of(0, {mine[0]}, static_cast<int16_t>(off.x), static_cast<int16_t>(off.y));
                ASSERT_EQ(v.predict_ack(c), 0u);
                ASSERT_EQ(sim.apply_command(c).ack_ant, 0u);
            }
            ASSERT_TRUE(v.predict_ack(move_of(0, {mine[0]}, 30, 30)) != 0);
            sim.drop_player(0);
            const Command c = move_of(0, {mine[1]}, 30, 30);
            ASSERT_EQ(BotView::build(sim, 0).predict_ack(c), 0u);
            ASSERT_EQ(sim.apply_command(c).ack_ant, 0u);
        }
        // (5) the differential: 6 worlds x 800 random orders from random seats over typed ants (workers, bombers, fire ants, thieves, combat ants, swimmers), a lake, rocks, bombs, a fire
        // wall, ants of every state the fights and bombs put them in, a team that drops out half way and tiles off the map
        size_t total = 0;
        size_t agree = 0;
        size_t exception1 = 0;
        size_t exception2 = 0;
        std::set<sim::UnitState> states;
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            sim::SimulationEngine sim;
            build_rich_world(sim, seed);
            BotRng rng(0xBEEF + seed);
            for (int step = 0; step < 800; ++step) {
                if (step == 400) sim.drop_player(static_cast<uint8_t>(seed % sim::MAX_PLAYERS));
                const uint8_t seat = static_cast<uint8_t>(rng.below(4));
                const BotView v = BotView::build(sim, seat);
                if (v.mine().empty()) {
                    sim.tick();
                    continue;
                }
                Command c;
                const uint32_t pick = rng.below(10);
                c.type = pick < 4 ? CommandType::GroupMove : pick < 6 ? CommandType::GroupAttack : CommandType::GroupSpecial;
                c.issuer = seat;
                const uint32_t n = 1 + rng.below(3);
                for (uint32_t i = 0; i < n; ++i) c.ants.push_back(v.mine()[rng.below(static_cast<uint32_t>(v.mine().size()))].id);
                const uint32_t where = rng.below(10);
                if (where == 0) { c.tile_x = 14; c.tile_y = 20; }                                               // a bomb
                else if (where == 1) { c.tile_x = 15; c.tile_y = 15; }                                           // the fire wall
                else if (where == 2) { c.tile_x = 30; c.tile_y = 30; }                                           // the middle of the lake
                else if (where == 3) { c.tile_x = 12; c.tile_y = 30; }                                           // inside the rocks
                else if (where == 4) { c.tile_x = static_cast<int16_t>(static_cast<int>(rng.below(70)) - 5); c.tile_y = static_cast<int16_t>(static_cast<int>(rng.below(70)) - 5); }
                else { c.tile_x = static_cast<int16_t>(rng.below(60)); c.tile_y = static_cast<int16_t>(rng.below(60)); }
                const TileCoord target = tc(c.tile_x, c.tile_y);
                bool walking_there = false;
                for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                    if (a.tile_x == target.x && a.tile_y == target.y && walking_family(a.state)) walking_there = true;
                }
                for (uint32_t id : c.ants) states.insert(find_ant(v.mine(), id)->state);
                uint32_t needed = 0;
                const uint32_t predicted = v.predict_ack(c, &needed);
                const sim::CommandResult r = sim.apply_command(c);
                ++total;
                ASSERT_EQ(needed, r.needing_order);
                if (predicted == r.ack_ant) {
                    ++agree;
                } else if (predicted == 0 && r.ack_ant != 0) {
                    ASSERT_TRUE(c.type == CommandType::GroupSpecial && walking_there);                         // exception 1, and nothing else
                    ++exception1;
                } else {
                    ASSERT_TRUE(predicted != 0 && r.ack_ant == 0);
                    ASSERT_TRUE(sim.grid().in_bounds(target) && sim.grid().terrain_class_at(target) == sim::movement::kTerrainWater);   // exception 2: a goal on water that the adjustment refuses
                    ++exception2;
                }
                if (step % 3 == 0) {
                    for (int t = 0; t < 1 + static_cast<int>(rng.below(5)); ++t) sim.tick();
                }
            }
        }
        std::cout << "\n    predict_ack differential: " << total << " orders, " << agree << " agree, " << exception1 << " of exception 1, " << exception2 << " of exception 2, " << states.size()
                  << " states of the commanded ants\n";
        ASSERT_TRUE(total > 4000 && agree * 100 > total * 90);
        ASSERT_TRUE(exception1 >= 1 && exception2 >= 1);                                                        // both exceptions really happen (so a change of the engine is noticed)
        ASSERT_TRUE(states.count(sim::UnitState::Idle) && states.count(sim::UnitState::Walking));
        ASSERT_TRUE(states.size() >= 4);                                                                        // ants in fights, flights and bomb stuns were commanded as well
    } TEST_END();

    TEST_CASE("AI1.25 Non-Interference: Two Engines That Differ Only In What Other Teams Hide (Carried Points, Eggs, An Egg Incubating, Orders In Flight) Give Byte-Identical Views To The Observing Seat; What Is On The Screen (A Crumb) Or Is The Seat's Own (Its Hit Points), Or Is Visible By A Project Decision (Another Team's Hit Points), Does Differ") {
        const auto make_pair_of_worlds = [&](uint8_t seat, sim::SimulationEngine& a, sim::SimulationEngine& b, bool with_orders) -> void {
            build_world(a, 51, 12);
            build_world(b, 51, 12);
            // power-ups are on every screen: a free one, one that an ant of the seat stands on and one that an ant of another team stands on (the same in both worlds, B4-1)
            for (sim::SimulationEngine* e : {&a, &b}) {
                e->grid_mut().place_bomb(33, 31, seat);                                                           // a bomb of the seat and one of another team: on the screen, in the owner's colour
                e->grid_mut().place_bomb(34, 31, static_cast<uint8_t>((seat + 1) % sim::MAX_PLAYERS));
                e->grid_mut().place_powerup(30, 30, 3);
                e->grid_mut().place_powerup(31, 30, 4);
                e->grid_mut().place_powerup(32, 30, 2);
                e->spawn_unit(seat, sim::AntType::Worker, tc(31, 30));
                e->spawn_unit(static_cast<uint8_t>((seat + 1) % sim::MAX_PLAYERS), sim::AntType::Worker, tc(32, 30));
            }
            a.set_fire_at(tc(35, 31), 3000);                                                                       // a fire wall: its owner and how long it still burns are on no screen
            b.set_fire_at(tc(35, 31), 150);
            a.set_fire_at(tc(36, 31), 3000);
            b.set_fire_at(tc(36, 31), 150);
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                a.set_player_score(t, 300);
                b.set_player_score(t, 300);
                a.set_player_eggs(t, 3);
                b.set_player_eggs(t, 3);
            }
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                if (t == seat) continue;
                b.set_player_eggs(t, 9);                                                                         // eggs
                for (uint32_t id : ants_of(b, t)) b.get_unit(id).carried_points = 25;                            // carried points (the hit points of every ant are on the view: a project decision, the control below)
                b.set_player_score(t, 500);
                ASSERT_TRUE(b.try_hatch(t) == sim::SimulationEngine::HatchResult::Started);                      // an egg incubating, the score back to 300
                if (with_orders) ASSERT_TRUE(b.apply_command(move_of(t, {ants_of(b, t)[0], ants_of(b, t)[1]}, 30, 30)).accepted());   // orders in flight
            }
        };
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            for (const bool with_orders : {true, false}) {
                sim::SimulationEngine a;
                sim::SimulationEngine b;
                make_pair_of_worlds(seat, a, b, with_orders);
                const int ticks = with_orders ? 0 : 100;                                                         // an order shows as soon as its path is delivered: only the moment of the order is hidden
                for (int t = 0; t <= ticks; ++t) {
                    ASSERT_EQ(view_text(BotView::build(a, seat)), view_text(BotView::build(b, seat)));
                    a.tick();
                    b.tick();
                }
                // the engines really differ in what is hidden (so the equality above is not between two copies of one world)
                uint8_t other = static_cast<uint8_t>((seat + 1) % sim::MAX_PLAYERS);
                ASSERT_TRUE(a.get_player_eggs(other) != b.get_player_eggs(other));
                ASSERT_TRUE(a.get_pending_hatch_count(other) != b.get_pending_hatch_count(other));
                const uint32_t id = ants_of(a, other)[2];
                ASSERT_TRUE(a.get_unit(id).carried_points != b.get_unit(id).carried_points);
            }
            // controls: what the screen shows, what is the seat's own, and the hit points of another team's ant (a project decision), DO change the view (the engine's world state is a cache that a tick rebuilds)
            const uint8_t other = static_cast<uint8_t>((seat + 1) % sim::MAX_PLAYERS);
            for (const int control : {0, 1, 2}) {
                sim::SimulationEngine a;
                sim::SimulationEngine b;
                make_pair_of_worlds(seat, a, b, false);
                a.tick();
                b.tick();
                ASSERT_EQ(view_text(BotView::build(a, seat)), view_text(BotView::build(b, seat)));
                if (control == 1) b.get_unit(ants_of(b, seat)[0]).hp = 3;                                        // the seat's own hit points: its own business
                else if (control == 2) b.get_unit(ants_of(b, other)[0]).hp = 3;                                  // another team's hit points: on the view since a project decision
                else b.get_unit(ants_of(b, other)[0]).holding = 1;                                               // another team's crumb: on the screen
                a.tick();
                b.tick();
                ASSERT_TRUE(view_text(BotView::build(a, seat)) != view_text(BotView::build(b, seat)));
            }
        }
    } TEST_END();

    TEST_CASE("AI1.27 approach_now Follows The Engine's Dynamic Rules: A Bomb Of The Team Itself Or Of Its Ally In The Only Gap Closes The Way (An Enemy's Bomb Does Not), The Queue Row Of A Team That Dropped Out Is Open Ground; The Ally And The Dropped Mask Come From The View") {
        // a wall across the world with one gap, the pile behind it
        const auto world = [](sim::SimulationEngine& sim) {
            sim.init_test_world(40, 30, 61, 720000);
            sim.set_anthill(0, tc(2, 12));
            for (int y = 0; y < 30; ++y) {                                                       // rows 0 to 29: row 0 of a test world is not solid like the original's
                if (y != 14) sim.set_terrain(20, y, sim::TERRAIN_OBSTACLE);
            }
            place_crackers(sim, 30, 14, 25);
        };
        sim::SimulationEngine sim;
        world(sim);
        const MapInfo mi(sim);
        const Approach start = mi.piles()[0].approach[0];
        ASSERT_TRUE(start.reachable() && start.click.x >= 29);
        const WalkContext none;
        ASSERT_TRUE(mi.approach_now(sim.grid(), 0, 0, none).reachable() && mi.approach_now(sim.grid(), 0, 0, none).cost == start.cost);
        // the team's own bomb in the gap
        sim.grid_mut().place_bomb(20, 14, 0);
        ASSERT_FALSE(mi.approach_now(sim.grid(), 0, 0, none).reachable());
        // an enemy's bomb there does not close it
        sim.grid_mut().clear_bomb(20, 14);
        sim.grid_mut().place_bomb(20, 14, 1);
        ASSERT_TRUE(mi.approach_now(sim.grid(), 0, 0, none).reachable());
        // ... but the bomb of an ALLY does, once the context says who the ally is (BotView::walk_context carries it)
        ASSERT_FALSE(mi.approach_now(sim.grid(), 0, 0, WalkContext{1, 0}).reachable());
        sim.form_alliance(0, 1);
        sim.tick();
        const BotView allied = BotView::build(sim, 0);
        ASSERT_EQ(allied.walk_context().ally, 1);
        ASSERT_EQ(allied.walk_context().dropped_mask, 0);
        ASSERT_FALSE(mi.approach_now(sim.grid(), 0, 0, allied.walk_context()).reachable());
        // the engine agrees with each of the four answers: a worker on the queue tile is sent to the click tile
        for (int variant = 0; variant < 4; ++variant) {
            sim::SimulationEngine e;
            world(e);
            const uint8_t owner = variant == 0 ? uint8_t{0} : uint8_t{1};
            if (variant != 3) e.grid_mut().place_bomb(20, 14, owner);                       // 0: own, 1: enemy, 2: ally's, 3: no bomb
            if (variant == 2) e.form_alliance(0, 1);
            const Walk w = order_and_wait(e, 0, mi.hill(0).starts.front(), start.click, 200);
            const bool engine_reaches = w.delivered;
            const bool expected = variant == 1 || variant == 3;
            ASSERT_EQ(engine_reaches, expected);
            ASSERT_TRUE(w.delivered != w.failed);
        }
        // the queue row of a team that dropped out: team 1's hill sits below a wall whose only gap is its three queue tiles
        const auto dropped_world = [](sim::SimulationEngine& e) {
            e.init_test_world(40, 30, 62, 720000);
            e.set_anthill(0, tc(2, 2));
            e.set_anthill(1, tc(20, 12));
            for (int x = 0; x < 40; ++x) {
                if (x < 20 || x > 22) e.set_terrain(x, 11, sim::TERRAIN_OBSTACLE);
            }
            place_crackers(e, 30, 25, 25);
        };
        sim::SimulationEngine dsim;
        dropped_world(dsim);
        const MapInfo dm(dsim);
        ASSERT_FALSE(dm.piles()[0].approach[0].reachable());                                   // the gap is team 1's queue row: closed to team 0
        ASSERT_TRUE(dm.piles()[0].approach[1].reachable());                                    // team 1's own ants may use it
        ASSERT_FALSE(dm.approach_now(dsim.grid(), 0, 0, none).reachable());
        dsim.drop_player(1);
        dsim.tick();
        const BotView after = BotView::build(dsim, 0);
        ASSERT_EQ(after.walk_context().dropped_mask, 2);
        ASSERT_FALSE(dm.approach_now(dsim.grid(), 0, 0, none).reachable());                    // without the mask the analysis still thinks it closed ...
        const Approach open = dm.approach_now(dsim.grid(), 0, 0, after.walk_context());
        ASSERT_TRUE(open.reachable());                                                          // ... with it, the way is open
        // and the engine delivers a path now (before the drop it fails)
        sim::SimulationEngine before;
        dropped_world(before);
        ASSERT_TRUE(order_and_wait(before, 0, dm.hill(0).starts.front(), open.click, 300).failed);
        const Walk through = order_and_wait(dsim, 0, dm.hill(0).starts.front(), open.click, 300);
        ASSERT_TRUE(through.delivered);
        ASSERT_TRUE(path_cost(dsim.grid(), through.path) >= open.cost && path_cost(dsim.grid(), through.path) * 100 <= static_cast<int64_t>(open.cost) * 115);
    } TEST_END();

    TEST_CASE("AI1.28 The Cost Field Starts From Every Walkable Tile Of The Queue Row: A Rock On The Middle Tile Does Not Cut The Hill Off (The Engine's Ants Step Diagonally Onto The Outer Two); A Hill Whose Whole Row Is Blocked Reaches Nothing") {
        const auto world = [](sim::SimulationEngine& sim, int blocked_mask) {
            sim.init_test_world(40, 40, 63, 720000);
            sim.set_anthill(0, tc(10, 10));
            for (int dx = 0; dx <= 2; ++dx) {
                if ((blocked_mask >> dx) & 1) sim.set_terrain(10 + dx, 9, sim::TERRAIN_OBSTACLE);
            }
            place_crackers(sim, 20, 14, 25);
        };
        // the middle tile (bx + 1, by - 1) is a rock
        sim::SimulationEngine sim;
        world(sim, 0b010);
        const MapInfo mi(sim);
        const HillInfo& h = mi.hill(0);
        ASSERT_TRUE(h.present && h.queue == tc(11, 9));
        ASSERT_TRUE(h.starts.size() == 2 && h.starts[0] == tc(10, 9) && h.starts[1] == tc(12, 9));
        ASSERT_EQ(mi.walking_cost(0, tc(10, 9)), 0);
        ASSERT_EQ(mi.walking_cost(0, tc(12, 9)), 0);
        ASSERT_EQ(mi.walking_cost(0, tc(11, 9)), -1);                                          // the rock itself
        ASSERT_TRUE(mi.hill_component(0) >= 0);
        ASSERT_TRUE(mi.piles()[0].approach[0].reachable());
        ASSERT_EQ(mi.reachable_points(0), 100u);
        // the engine banks all of it with one worker
        ASSERT_EQ(harvest_for(sim, 0, tc(16, 8), mi.piles()[0].approach[0].click, 4000), 100);
        // the whole row blocked: nothing leaves that hill, MapInfo and the engine agree
        sim::SimulationEngine shut;
        world(shut, 0b111);
        const MapInfo sm(shut);
        ASSERT_TRUE(sm.hill(0).present && sm.hill(0).starts.empty());
        ASSERT_EQ(sm.hill_component(0), -1);
        ASSERT_FALSE(sm.piles()[0].approach[0].reachable());
        ASSERT_EQ(sm.reachable_points(0), 0u);
        ASSERT_EQ(harvest_for(shut, 0, tc(16, 8), tc(20, 14), 2500), 0);
        // all three open: three starts, the same field as before near the middle tile
        sim::SimulationEngine open;
        world(open, 0);
        const MapInfo om(open);
        ASSERT_TRUE(om.hill(0).starts.size() == 3 && om.hill(0).starts[1] == om.hill(0).queue);
        ASSERT_EQ(om.walking_cost(0, tc(10, 9)) + om.walking_cost(0, tc(11, 9)) + om.walking_cost(0, tc(12, 9)), 0);
    } TEST_END();

    TEST_CASE("AI1.29 The First Hill Of A Team Is The Hill: A Map Without Hill Art Falls Back To Its Start Markers And May Name A Team Twice, The Engine Takes The First (Grid::find_anthill), So Does MapInfo") {
        assets::LevelData level = level_of("TINY");
        size_t renamed = 0;
        for (std::string& name : level.tile_dictionary) {
            std::string upper = name;
            for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (upper == "GREENHILL" || upper == "REDHILL" || upper == "BLUEHILL" || upper == "BLACKHILL") {
                name = "NOHILL";
                ++renamed;
            }
        }
        ASSERT_TRUE(renamed >= 4);
        sim::SimulationEngine sim;
        sim.init(level, 1, 0x0F);
        size_t repeated = 0;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            size_t hills = 0;
            for (const assets::AnthillSpawn& a : sim.grid().anthills()) hills += a.team_id == t ? 1u : 0u;
            repeated += hills > 1 ? 1u : 0u;
        }
        ASSERT_TRUE(repeated >= 1);                                                              // the fallback gave a team more than one hill (the premise)
        const MapInfo mi(sim);
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const assets::AnthillSpawn* first = sim.grid().find_anthill(t);
            ASSERT_TRUE(first != nullptr);
            ASSERT_TRUE(mi.hill(t).present && mi.hill(t).origin == tc(first->x, first->y));
            ASSERT_TRUE(mi.hill(t).queue == tc(first->x + 1, first->y - 1));
        }
    } TEST_END();

    TEST_CASE("AI1.30 The Last Step Onto A Target Is Checked Like Every Other Step: A Power-Up Or A Pile On Water, In A Mound Or On Another Team's Queue Row Is Not Reachable; Stepping Into Water Costs The Engine's 8,000") {
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 64, 720000);
        sim.set_anthill(0, tc(4, 4));
        sim.set_anthill(1, tc(30, 30));
        water_rect(sim, 18, 8, 26, 16);                                                          // a lake in the way
        for (int y = 1; y < 60; ++y) sim.set_terrain(40, y, sim::TERRAIN_WATER);                // a river
        sim.grid_mut().place_powerup(40, 20, 5);                                                 // 0: a swimmer power-up on the river
        sim.grid_mut().place_powerup(7, 5, 1);                                                   // 1: inside team 0's own mound
        sim.grid_mut().place_powerup(31, 29, 2);                                                 // 2: on team 1's queue row
        sim.grid_mut().place_powerup(14, 10, 3);                                                 // 3: the control: on grass, reachable
        place_crackers(sim, 22, 12, 25);                                                         // a pile in the middle of the lake: every cell is water
        const MapInfo mi(sim);
        ASSERT_EQ(mi.powerups().size(), 4u);
        const auto powerup_at = [&](int32_t x, int32_t y) -> const PowerUpInfo& {
            for (const PowerUpInfo& p : mi.powerups()) {
                if (p.tile == tc(x, y)) return p;
            }
            return mi.powerups().front();
        };
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            ASSERT_FALSE(powerup_at(40, 20).approach[t].reachable());                            // water: the engine's step into it is blocked
            ASSERT_FALSE(powerup_at(7, 5).approach[t].reachable());                              // a mound: an order onto it is a walk home, or refused
        }
        ASSERT_FALSE(powerup_at(31, 29).approach[0].reachable());                                // another team's queue row
        ASSERT_TRUE(powerup_at(14, 10).approach[0].reachable() && powerup_at(14, 10).approach[1].reachable());
        ASSERT_FALSE(mi.piles()[0].approach[0].reachable());                                     // a pile whose cells are all water
        ASSERT_EQ(mi.reachable_points(), 0u);
        ASSERT_TRUE(MapInfo::can_step_onto(sim.grid(), 1, tc(31, 29)) && !MapInfo::can_step_onto(sim.grid(), 0, tc(31, 29)));
        ASSERT_TRUE(MapInfo::can_step_onto(sim.grid(), 0, tc(14, 10)) && !MapInfo::can_step_onto(sim.grid(), 0, tc(7, 5)) && !MapInfo::can_step_onto(sim.grid(), 0, tc(40, 20)));
        ASSERT_EQ(MapInfo::step_cost(sim.grid(), tc(39, 20), tc(40, 20)), 8000u);               // grass onto water
        ASSERT_EQ(MapInfo::step_cost(sim.grid(), tc(40, 20), tc(40, 21)), 8000u);               // water onto water
        ASSERT_EQ(MapInfo::step_cost(sim.grid(), tc(38, 20), tc(39, 20)), 20u);                 // grass onto grass: unchanged
        // the engine does not take the power-ups the analysis calls unreachable, and does take the one it calls reachable
        const uint32_t w0 = sim.spawn_unit(0, sim::AntType::Worker, tc(12, 10));
        const uint32_t w1 = sim.spawn_unit(0, sim::AntType::Worker, tc(8, 12));
        const uint32_t w2 = sim.spawn_unit(0, sim::AntType::Worker, tc(36, 20));
        const uint32_t w3 = sim.spawn_unit(0, sim::AntType::Worker, tc(28, 27));
        sim.apply_command(move_of(0, {w0}, 14, 10));
        sim.apply_command(move_of(0, {w1}, 7, 5));
        sim.apply_command(move_of(0, {w2}, 40, 20));
        sim.apply_command(move_of(0, {w3}, 31, 29));
        for (int t = 0; t < 600; ++t) sim.tick();
        ASSERT_FALSE(sim.grid().has_powerup_at(tc(14, 10)));                                    // taken
        ASSERT_TRUE(sim.grid().has_powerup_at(tc(7, 5)) && sim.grid().has_powerup_at(tc(40, 20)) && sim.grid().has_powerup_at(tc(31, 29)));
        ASSERT_TRUE(sim.get_unit(w0).type != sim::AntType::Worker);
        ASSERT_TRUE(sim.get_unit(w1).type == sim::AntType::Worker && sim.get_unit(w2).type == sim::AntType::Worker && sim.get_unit(w3).type == sim::AntType::Worker);
    } TEST_END();

    TEST_CASE("AI1.31 Row 90 Is Not Walkable (A Map Taller Than 90 Rows): The Engine's Path Finder Never Generates A Tile Of It (path_planner.cpp, 0x5a In Ants.exe), So Nothing Can Be Walked Into It And Everything Below It Is Cut Off; MapInfo Models It") {
        const auto world = [](sim::SimulationEngine& sim) {
            sim.init_test_world(100, 100, 65, 720000);
            sim.set_anthill(0, tc(10, 10));
            place_crackers(sim, 50, 88, 25);
            place_crackers(sim, 50, 95, 25);
        };
        sim::SimulationEngine sim;
        world(sim);
        const MapInfo mi(sim);
        ASSERT_FALSE(MapInfo::walkable(sim.grid(), 0, tc(50, 90)));
        ASSERT_TRUE(MapInfo::walkable(sim.grid(), 0, tc(50, 89)) && MapInfo::walkable(sim.grid(), 0, tc(50, 91)));
        ASSERT_FALSE(MapInfo::walkable(sim.grid(), 0, tc(0, 90)) || MapInfo::walkable(sim.grid(), 3, tc(99, 90)));
        ASSERT_TRUE(mi.component(0, tc(50, 89)) >= 0 && mi.component(0, tc(50, 91)) >= 0);
        ASSERT_TRUE(mi.component(0, tc(50, 89)) != mi.component(0, tc(50, 91)));               // the map is cut in two
        ASSERT_EQ(mi.component(0, tc(50, 92)), mi.component(0, tc(50, 98)));
        ASSERT_EQ(mi.component(0, tc(50, 90)), -1);
        ASSERT_TRUE(mi.piles()[0].approach[0].reachable());                                      // row 88
        ASSERT_FALSE(mi.piles()[1].approach[0].reachable());                                     // row 95: below the cut
        ASSERT_EQ(mi.reachable_points(), 100u);
        // the engine: a walk to row 89 works, to row 90 and 91 fails, a walk below the cut works, the pile at row 88 can be reached and the one at row 95 cannot
        const struct { TileCoord from; TileCoord to; bool delivered; } walks[] = {
            {tc(50, 60), tc(50, 89), true}, {tc(50, 60), tc(50, 90), false}, {tc(50, 60), tc(50, 91), false}, {tc(50, 92), tc(50, 98), true}, {tc(50, 60), tc(50, 99), false},
        };
        for (const auto& w : walks) {
            sim::SimulationEngine e;
            world(e);
            const Walk r = order_and_wait(e, 0, w.from, w.to, 400);
            ASSERT_TRUE(r.delivered != r.failed);
            ASSERT_EQ(r.delivered, w.delivered);
        }
        sim::SimulationEngine near_e;
        world(near_e);
        ASSERT_TRUE(order_and_wait(near_e, 0, mi.hill(0).starts.front(), mi.piles()[0].approach[0].click, 600).delivered);
        sim::SimulationEngine far_e;
        world(far_e);
        ASSERT_TRUE(order_and_wait(far_e, 0, mi.hill(0).starts.front(), tc(50, 94), 600).failed);
        // a map of 60 rows has no such row
        sim::SimulationEngine small;
        small.init_test_world(100, 60, 66, 720000);
        ASSERT_TRUE(MapInfo::walkable(small.grid(), 0, tc(50, 59)) && MapInfo::walkable(small.grid(), 0, tc(50, 30)));
    } TEST_END();

    TEST_CASE("AI1.32 approach_now For Every Object Of The Engine's Table, Also One Dropped After The Start (A Lunchbox), A Leftover Crumb Of A Picture Is Not A Pile, And One Field Serves Every Pile (The Same Answers As A Field Per Call)") {
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 67, 720000);
        sim.set_anthill(0, tc(2, 2));
        const MapInfo mi(sim);                                                                   // built with no pile at all
        ASSERT_TRUE(mi.piles().empty() && mi.pile(0) == nullptr);
        sim.grid_mut().drop_lunchbox(14, 14, 50);
        const BotView v = BotView::build(sim, 0, &mi);
        ASSERT_TRUE(v.piles().size() == 1 && v.piles()[0].lunchbox && v.piles()[0].index == 0);
        const Approach box = mi.approach_now(sim.grid(), 0, 0, v.walk_context());
        ASSERT_TRUE(box.reachable() && box.click == tc(14, 14));                                // an index past the analysis' table: asked of the grid
        ASSERT_FALSE(mi.approach_now(sim.grid(), 0, 1, v.walk_context()).reachable());          // not in the table at all
        ASSERT_EQ(harvest_for(sim, 0, tc(10, 6), box.click, 1500), 50);                         // the engine banks it
        // a pile whose last stage is a crumb: eaten, its picture is still food on the map, and approach_now says "nothing left"
        sim::SimulationEngine crumb;
        crumb.init_test_world(40, 40, 68, 720000);
        crumb.set_anthill(0, tc(2, 2));
        const int32_t pile = place_pile(crumb, 20, 20, 4, 25, {{4, 369}, {3, 370}, {2, 371}, {1, 372}, {0, 372}});
        const MapInfo cm(crumb);
        ASSERT_TRUE(cm.piles()[static_cast<size_t>(pile)].approach[0].reachable());
        bool changed = false;
        crumb.grid_mut().take_food(pile, 4, changed);
        crumb.grid_mut().set_food_tile(tc(20, 20), crumb.grid().food_objects()[static_cast<size_t>(pile)].stage_tile());       // as the end of the last bite does
        size_t food_cells = 0;
        for (int y = 15; y < 26; ++y) {
            for (int x = 15; x < 26; ++x) food_cells += crumb.grid().food_object_at_cell(tc(x, y)) == pile ? 1u : 0u;
        }
        ASSERT_TRUE(food_cells >= 1 && crumb.grid().food_objects()[static_cast<size_t>(pile)].remaining == 0);       // the picture is still there, the pile is eaten
        ASSERT_FALSE(cm.approach_now(crumb.grid(), 0, static_cast<uint32_t>(pile)).reachable());
        // the same for the last bite of an ordinary pile before the tile update (the bite takes the unit at once, the picture changes when the clip ends): the units are 0, the footprint is
        // still food, and approach_now says "nothing left"
        sim::SimulationEngine bite;
        bite.init_test_world(40, 40, 75, 720000);
        bite.set_anthill(0, tc(2, 2));
        const int32_t last_pile = place_crackers(bite, 20, 20, 25);
        const MapInfo bm(bite);
        ASSERT_TRUE(bm.piles()[static_cast<size_t>(last_pile)].approach[0].reachable());
        bool unchanged = false;
        bite.grid_mut().take_food(last_pile, 4, unchanged);                                        // every unit taken, the tile not yet updated
        size_t cells_left = 0;
        for (int y = 15; y < 26; ++y) {
            for (int x = 15; x < 26; ++x) cells_left += bite.grid().food_object_at_cell(tc(x, y)) == last_pile ? 1u : 0u;
        }
        ASSERT_TRUE(cells_left >= 1 && bite.grid().food_objects()[static_cast<size_t>(last_pile)].remaining == 0);
        ASSERT_FALSE(bm.approach_now(bite.grid(), 0, static_cast<uint32_t>(last_pile)).reachable());
        // one field for all piles: TREASURE after some piles were bitten, for every team
        sim::SimulationEngine tr;
        start_match(tr, "TREASURE", 1, 0x0F);
        const MapInfo tm(tr);
        for (uint32_t i : {0u, 2u, 5u}) {
            bool ch = false;
            tr.grid_mut().take_food(static_cast<int32_t>(i), 9, ch);
            if (ch) tr.grid_mut().set_food_tile(tc(tr.grid().food_objects()[i].col, tr.grid().food_objects()[i].row), tr.grid().food_objects()[i].stage_tile());
        }
        tr.tick();
        size_t compared = 0;
        for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
            const BotView tv = BotView::build(tr, team, &tm);
            const MapInfo::NowField field = tm.field_now(tr.grid(), team, tv.walk_context());
            ASSERT_TRUE(field.valid());
            for (uint32_t i = 0; i < tr.grid().food_objects().size(); ++i) {
                const Approach one = tm.approach_now(tr.grid(), i, field);
                const Approach each = tm.approach_now(tr.grid(), team, i, tv.walk_context());
                ASSERT_TRUE(one.cost == each.cost && one.click == each.click);
                ++compared;
            }
        }
        ASSERT_EQ(compared, 4u * 18u);
        ASSERT_FALSE(tm.field_now(tr.grid(), 9).valid());
        // a pile of one cell on another team's queue row: closed to the team that does not own the row (through either overload), open to its owner, open to everybody once the owner dropped
        sim::SimulationEngine tiny;
        start_match(tiny, "TINY", 1, 0x0F);
        sim::FoodObject model = tiny.grid().food_objects()[0];
        sim::SimulationEngine row;
        row.init_test_world(40, 40, 74, 720000);
        row.set_anthill(0, tc(2, 2));
        row.set_anthill(1, tc(20, 20));
        model.col = 21;
        model.row = 19;                                                                          // (bx + 1, by - 1): the middle tile of team 1's queue row
        row.grid_mut().add_food_object(model);
        const MapInfo rm(row);
        ASSERT_TRUE(rm.piles()[0].cells.size() == 1 && rm.piles()[0].cells[0] == tc(21, 19));
        const WalkContext live;
        const WalkContext gone{sim::ALLIANCE_NONE, 2};                                       // team 1 has dropped out
        for (const WalkContext& ctx : {live, gone}) {
            for (uint8_t team : {uint8_t{0}, uint8_t{1}}) {
                const MapInfo::NowField field = rm.field_now(row.grid(), team, ctx);
                const Approach one = rm.approach_now(row.grid(), 0, field);
                const Approach each = rm.approach_now(row.grid(), team, 0, ctx);
                ASSERT_TRUE(one.cost == each.cost && one.click == each.click);
                const bool expected = team == 1 || ctx.dropped_mask != 0;                          // the owner always, the other team only when the owner is gone
                ASSERT_EQ(one.reachable(), expected);
            }
        }
    } TEST_END();

    TEST_CASE("AI1.33 The Points Of A Map Are Summed In 64 Bits: Three Piles Of 65,535 Units Of 65,535 Points Each (A File Stores Both As 16 Bits And The Loader Does Not Clamp Them) Add Up To 12,884,508,675") {
        sim::SimulationEngine sim;
        sim.init_test_world(60, 40, 69, 720000);
        sim.set_anthill(0, tc(2, 2));
        const std::vector<std::pair<uint16_t, uint16_t>> stages = {{65535, 369}, {0, kPileGone}};
        for (int32_t col : {12, 24, 36}) place_pile(sim, col, 10, 65535, 65535, stages);
        const MapInfo mi(sim);
        const uint64_t each = 65535ull * 65535ull;
        ASSERT_EQ(mi.total_points(), 3 * each);
        ASSERT_EQ(mi.total_points(), 12884508675ull);
        ASSERT_EQ(mi.reachable_points(), 3 * each);
        ASSERT_EQ(mi.reachable_points(0), 3 * each);
        ASSERT_EQ(mi.reachable_points(2), 0u);                                                   // seat 2 has no hill here
    } TEST_END();

    TEST_CASE("AI1.34 Conformance Cases The Mutations Found Open: A Pile Whose Only Open Neighbour Is Diagonal Is Reachable And Harvested; Two Objects On One Anchor With Different Units Give The Units Of The First Bitten At The Points Of The Last; An Ant On A Tile That Is Not Walkable Belongs To The SMALLEST Component Around It") {
        // a pile of one cell in a niche: the four orthogonal neighbours and three of the four diagonals are rocks
        {
            sim::SimulationEngine tiny;
            start_match(tiny, "TINY", 1, 0x0F);
            const sim::FoodObject model = tiny.grid().food_objects()[0];
            sim::SimulationEngine sim;
            sim.init_test_world(40, 40, 70, 720000);
            sim.set_anthill(0, tc(2, 2));
            for (const TileCoord rock : {tc(20, 19), tc(20, 21), tc(19, 20), tc(21, 20), tc(21, 19), tc(19, 21), tc(21, 21)}) sim.set_terrain(rock.x, rock.y, sim::TERRAIN_OBSTACLE);
            sim::FoodObject o = model;
            o.col = 20;
            o.row = 20;
            sim.grid_mut().add_food_object(o);
            const MapInfo mi(sim);
            ASSERT_EQ(mi.piles()[0].cells.size(), 1u);
            ASSERT_TRUE(mi.piles()[0].approach[0].reachable() && mi.piles()[0].approach[0].click == tc(20, 20));
            ASSERT_TRUE(mi.can_reach_pile(0, tc(10, 10), 0));
            ASSERT_EQ(mi.reachable_points(0), static_cast<uint64_t>(model.units) * model.value);
            ASSERT_TRUE(harvest_for(sim, 0, tc(10, 6), tc(20, 20), 2500) >= static_cast<int32_t>(model.value));       // the engine does walk onto it diagonally and bank
        }
        // two objects on one anchor: 4 units of 20 points first, 6 units of 50 points last: the last one owns the cells, the first one loses the units
        {
            sim::SimulationEngine sim;
            sim.init_test_world(40, 40, 71, 720000);
            sim.set_anthill(0, tc(2, 2));
            const int32_t first = place_pile(sim, 12, 10, 4, 20, {{4, 369}, {3, 370}, {2, 371}, {1, 372}, {0, kPileGone}});
            const int32_t last = place_pile(sim, 12, 10, 6, 50, {{6, 369}, {4, 370}, {2, 371}, {1, 372}, {0, kPileGone}});
            const MapInfo mi(sim);
            ASSERT_EQ(mi.piles()[static_cast<size_t>(last)].bite_index, static_cast<uint32_t>(first));
            ASSERT_EQ(mi.reachable_points(), 200u);                                              // not 6 x 50 = 300
            ASSERT_EQ(mi.reachable_points(0), 200u);
            ASSERT_EQ(mi.total_points(), 4u * 20u + 6u * 50u);
            ASSERT_EQ(harvest_for(sim, 0, tc(10, 6), mi.piles()[static_cast<size_t>(last)].approach[0].click, 4000), 200);        // and the engine banks exactly that
        }
        // an ant on a rock of a wall between two components
        {
            sim::SimulationEngine sim;
            sim.init_test_world(40, 40, 72, 720000);
            sim.set_anthill(0, tc(2, 2));
            for (int y = 0; y < 40; ++y) sim.set_terrain(20, y, sim::TERRAIN_OBSTACLE);
            const MapInfo mi(sim);
            const int32_t left = mi.component(0, tc(10, 30));
            const int32_t right = mi.component(0, tc(30, 30));
            ASSERT_TRUE(left >= 0 && right >= 0 && left != right);
            ASSERT_EQ(mi.component(0, tc(20, 20)), -1);
            ASSERT_EQ(mi.ant_component(0, tc(20, 20)), std::min(left, right));
            ASSERT_EQ(mi.ant_component(0, tc(20, 20)), left);                                    // the left one is numbered first (reading order of the first tile of each component)
        }
    } TEST_END();

    TEST_CASE("AI1.35 The Walking Time: An Orthogonal Run Takes 0.4 Tick Per Unit Of Cost, A Diagonal Run About 10 Percent Less (The Path Finder Charges A Diagonal Step 1.4 Times, Its Clip Takes 1.25 Times); The Trip Model Therefore Overestimates A Diagonal-Heavy Route, And This Is Documented") {
        const auto measure = [](TileCoord from, TileCoord to, int32_t* cost) {
            sim::SimulationEngine w;
            w.init_test_world(80, 80, 73, 720000);
            w.set_locomotion_trace_enabled(true);
            const uint32_t ant = w.spawn_unit(0, sim::AntType::Worker, from);
            w.apply_command(move_of(0, {ant}, static_cast<int16_t>(to.x), static_cast<int16_t>(to.y)));
            *cost = MapInfo::cost_field(w.grid(), 0, from)[static_cast<size_t>(to.y) * 80 + static_cast<size_t>(to.x)];
            uint32_t delivered_at = 0;
            uint32_t arrived_at = 0;
            for (uint32_t t = 1; t < 1500 && arrived_at == 0; ++t) {
                w.tick();
                if (delivered_at == 0) {
                    for (const auto& ev : w.locomotion_trace()) {
                        if (ev.ant_id == ant && ev.kind == sim::LocoTraceEvent::Kind::PathDelivered) delivered_at = t;
                    }
                }
                const sim::AntUnit& u = w.get_unit(ant);
                if (delivered_at != 0 && u.pixel_x / 32 == to.x && u.pixel_y / 32 == to.y) arrived_at = t;
            }
            return static_cast<double>(arrived_at - delivered_at);
        };
        int32_t cost = 0;
        const double orthogonal = measure(tc(10, 30), tc(50, 30), &cost);
        ASSERT_EQ(cost, 800);
        ASSERT_EQ(MapInfo::walking_ticks(cost), 320);
        ASSERT_TRUE(std::abs(orthogonal - 320.0) <= 3.0);                                        // exact on a straight run
        const double diagonal = measure(tc(10, 10), tc(50, 50), &cost);
        ASSERT_EQ(cost, 1120);                                                                   // 40 steps of 28
        ASSERT_EQ(MapInfo::walking_ticks(cost), 448);
        const double ratio = static_cast<double>(MapInfo::walking_ticks(cost)) / diagonal;
        ASSERT_TRUE(ratio > 1.08 && ratio < 1.14);                                               // the model says 448, the ant needs about 401: about 10 percent less
    } TEST_END();

    TEST_CASE("AI1.26 The Clock And The Roster Are Pinned: ticks_left() Is The Engine's Clock In Ticks Over A Match, TeamRow::present Is The Roster (Seats 1 And 3 Do Not Play Here)") {
        sim::SimulationEngine sim;
        start_match(sim, "TINY", 1, 0x05);
        const BotView first = BotView::build(sim, 0);
        ASSERT_EQ(first.ticks_left(), 7200u);                                                                    // TINY plays 7,200 ticks
        ASSERT_TRUE(first.rows()[0].present && !first.rows()[1].present && first.rows()[2].present && !first.rows()[3].present);
        for (int t = 1; t <= 300; ++t) {
            sim.tick();
            if (t % 50 != 0) continue;
            const BotView v = BotView::build(sim, 2);
            ASSERT_EQ(v.ticks_left(), 7200u - static_cast<uint32_t>(sim.current_tick()));
            ASSERT_EQ(static_cast<uint64_t>(v.ticks_left()) + v.tick(), 7200u);
            ASSERT_EQ(v.ticks_left(), sim.get_match_time_remaining_ms() / sim::TICK_MS);
            ASSERT_TRUE(!v.rows()[1].present && !v.rows()[3].present && v.rows()[0].present && v.rows()[2].present);
        }
        sim::SimulationEngine full;
        start_match(full, "TINY", 1, 0x0F);
        const BotView everyone = BotView::build(full, 1);                                                       // (a named view: a range-for over a member of a temporary dangles)
        for (const TeamRow& r : everyone.rows()) ASSERT_TRUE(r.present);
    } TEST_END();

    TEST_CASE("AI4.5 The Sink Latency Model Is Pinned: A Command Released At Tick t Is Applied At The First Turn Boundary (An Even Tick) At Least `latency` Ticks Later, In Canonical Order, BEFORE The Bots Look; A Scripted Match Has One Pinned Hash") {
        for (const uint32_t latency : {3u, 5u, 2u, 1u}) {
            std::array<Walker::Notes, 4> notes;
            ArenaSpec s = arena_spec("TINY", 11, 900, latency, {seat_spec(0, "worker", Level::Hard), seat_spec(1, "worker", Level::Hard), seat_spec(2, "worker", Level::Hard), seat_spec(3, "worker", Level::Hard)});
            s.record = true;
            s.factory = [&](const BotSpec& b) { return std::make_unique<Walker>(&notes[b.seat]); };
            const ArenaResult r = play_match(s);
            ASSERT_TRUE(r.error.empty() && r.log.size() > 150);
            // every command is applied at an even tick (the 100 ms turn boundary of a room), also at latency 1, 3 and 5
            for (const RecordedCommand& c : r.log) {
                ASSERT_EQ(c.tick % 2, 0u);
                ASSERT_EQ(c.step, c.tick);
            }
            // each is applied at the first even tick that is at least `latency` ticks after its release (one issuer's commands keep their release order)
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                size_t k = 0;
                for (const RecordedCommand& c : r.log) {
                    if (c.command.issuer != seat) continue;
                    ASSERT_TRUE(k < notes[seat].sent.size());
                    const uint64_t due = notes[seat].sent[k] + latency;
                    ASSERT_EQ(c.tick, due + due % 2);
                    ++k;
                }
                ASSERT_TRUE(k > 20 && k <= notes[seat].sent.size() && k + 4 >= notes[seat].sent.size());     // all but the last few were applied
            }
            // the commands applied together are in canonical order: by issuer
            size_t groups_with_two_issuers = 0;
            for (size_t i = 1; i < r.log.size(); ++i) {
                if (r.log[i].tick != r.log[i - 1].tick) continue;
                ASSERT_TRUE(r.log[i - 1].command.issuer <= r.log[i].command.issuer);
                groups_with_two_issuers += r.log[i - 1].command.issuer < r.log[i].command.issuer ? 1u : 0u;
            }
            ASSERT_TRUE(groups_with_two_issuers > 10);                                                      // the order was really exercised (a release order would break it)
            // the commands of a turn are applied BEFORE the bots look: a look at the very tick a command is applied already sees its path request queued
            size_t seen_at_apply = 0;
            for (const RecordedCommand& c : r.log) {
                const auto& looks = notes[c.command.issuer].looks;
                for (const auto& look : looks) {
                    if (look.first == c.tick && look.second.count(c.command.ants.front()) != 0) ++seen_at_apply;
                }
            }
            ASSERT_TRUE(seen_at_apply > 10);
        }
        // a scripted match with a pinned state hash: two scripted seats on TINY, 600 ticks, latency 3 (it moves with the engine, the controller or the sink: regenerate it deliberately)
        const ArenaSpec pinned = [&] {
            ArenaSpec s = arena_spec("TINY", 3, 600, 3, {seat_spec(0, "worker", Level::Hard), seat_spec(1, "worker", Level::Medium)});
            s.factory = [](const BotSpec&) { return std::make_unique<Walker>(); };
            s.start_hold = 0;                                // the opening of v0.1.0: the sink model is measured from tick 1, and this hash (and these counts) did not move with the start hold
            return s;
        }();
        const ArenaResult p = play_match(pinned);
        ASSERT_TRUE(p.error.empty() && p.ticks == 600);
        if (p.hash != 0x60174838e5ae019full) std::cout << "\n    the pinned scripted match now ends at hash " << std::hex << p.hash << std::dec << " (" << p.seats[0].stats.released << " + " << p.seats[1].stats.released << " commands)\n";
        ASSERT_EQ(p.hash, 0x60174838e5ae019full);
        ASSERT_TRUE(p.seats[0].stats.released == 98 && p.seats[1].stats.released == 29);
        // the same match with the product's opening (the arena's default, kStartHoldTicks: one token in the bucket to begin with, the refill from the first tick; the first look is on tick 1 + seat as it
        // was in v0.1.0): another match with a pin of its own, and not one command is applied before a bot's first reaction time is over
        ArenaSpec held = pinned;
        held.start_hold = ai::kStartHoldTicks;
        held.record = true;
        const ArenaResult h = play_match(held);
        ASSERT_TRUE(h.error.empty() && h.ticks == 600 && !h.log.empty());
        ASSERT_TRUE(h.log.front().tick >= ai::kStartHoldTicks);
        if (h.hash != 0x95c9094978ca5f46ull) std::cout << "\n    the pinned scripted match with the product's opening now ends at hash " << std::hex << h.hash << std::dec << " (" << h.seats[0].stats.released << " + " << h.seats[1].stats.released << " commands)\n";
        ASSERT_EQ(h.hash, 0x95c9094978ca5f46ull);
        ASSERT_TRUE(h.seats[0].stats.released == 91 && h.seats[1].stats.released == 29);                 // (98 and 29 with the full bucket of v0.1.0: the one token holds the first look's commands back a little)
        // and with a long hold (the mechanism: v0.1.1's 100 ticks, in which the bots waited for the dialog while the simulation ran): another match with a pin of its own, the bots walk 500 ticks of
        // the 600, and not one command is applied before the hold's end
        ArenaSpec longer = pinned;
        longer.start_hold = 100;
        longer.record = true;
        const ArenaResult lr = play_match(longer);
        ASSERT_TRUE(lr.error.empty() && lr.ticks == 600 && !lr.log.empty());
        ASSERT_TRUE(lr.log.front().tick >= 100);
        if (lr.hash != 0x6ce648347dbc5a25ull) std::cout << "\n    the pinned scripted match with a hold of 100 ticks now ends at hash " << std::hex << lr.hash << std::dec << " (" << lr.seats[0].stats.released << " + " << lr.seats[1].stats.released << " commands)\n";
        ASSERT_EQ(lr.hash, 0x6ce648347dbc5a25ull);                                                       // (v0.1.1's pin of this match was 0x5b22a0b40e3231f5 with the same counts: its refill started one tick later)
        ASSERT_TRUE(lr.seats[0].stats.released == 76 && lr.seats[1].stats.released == 25);
    } TEST_END();

    TEST_CASE("AI4.6 The Result Fields Of A Seat Are The Engine's Numbers: Two Harvesting Bots (One Hatches An Egg As Soon As It Can Pay) Score, And The Result Says Exactly What The Engine's Own Getters Say; Points Banked Are The Score Plus The Cost Of The Hatch, Nothing Was Raided") {
        for (const uint32_t latency : {3u, 0u}) {
            ArenaSpec s = arena_spec("TINY", 1, 3600, latency, {seat_spec(0, "worker", Level::Hard), seat_spec(1, "worker", Level::Hard)});
            std::array<EngineNumbers, sim::MAX_PLAYERS> engine{};
            s.factory = [](const BotSpec& b) { return std::make_unique<Harvester>(b.seat == 0); };
            s.inspect = [&](const sim::SimulationEngine& e) { engine = numbers_of(e); };
            const ArenaResult r = play_match(s);
            ASSERT_TRUE(r.error.empty() && r.seats.size() == 2);
            for (const ArenaSeatResult& seat : r.seats) {
                const EngineNumbers& e = engine[seat.spec.seat];
                ASSERT_EQ(seat.score, e.score);
                ASSERT_EQ(seat.shown_score, e.shown);
                ASSERT_EQ(seat.eggs, e.eggs);
                ASSERT_EQ(seat.hatched, e.hatched);
                ASSERT_EQ(seat.kills, e.kills);
                ASSERT_EQ(seat.losses, e.losses);
                ASSERT_EQ(seat.ants, e.alive);
                ASSERT_TRUE(seat.score > 0);                                                                  // they harvested
                ASSERT_EQ(seat.raided, 0u);
                ASSERT_EQ(seat.kills + seat.losses, 0u);
                ASSERT_TRUE(seat.stats.released > 5 && seat.stats.rejected == 0);
            }
            const ArenaSeatResult& hatcher = r.seats[0];
            const ArenaSeatResult& plain = r.seats[1];
            ASSERT_EQ(plain.banked, static_cast<uint32_t>(plain.score));                                      // no hatch: the score IS what was banked
            ASSERT_EQ(plain.hatched, 0u);
            ASSERT_EQ(plain.eggs, 3u);                                                                        // TINY: three eggs
            ASSERT_TRUE(hatcher.hatched >= 1 && hatcher.eggs + hatcher.hatched == 3u);                         // an egg was used: the hatch happened
            ASSERT_EQ(hatcher.banked, static_cast<uint32_t>(hatcher.score) + 200u * hatcher.hatched);         // the 200 points of the hatch are neither a deposit nor a raid
            ASSERT_EQ(hatcher.ants, 3u + hatcher.hatched);
            ASSERT_EQ(plain.ants, 3u);
        }
    } TEST_END();

    TEST_CASE("AI4.7 Kills And Losses Are The Engine's: Two Bots That Attack Each Other Kill And Lose Ants, Each Kill Of One Seat Is A Loss Of The Other, And Every Field Equals The Engine's Own Getter (A Swapped Or Zeroed Field Would Show)") {
        for (const uint32_t latency : {3u, 0u}) {
            ArenaSpec s = arena_spec("TINY", 1, 3000, latency, {seat_spec(0, "worker", Level::Hard), seat_spec(1, "worker", Level::Hard)});
            std::array<EngineNumbers, sim::MAX_PLAYERS> engine{};
            s.factory = [](const BotSpec&) { return std::make_unique<Fighter>(); };
            s.inspect = [&](const sim::SimulationEngine& e) { engine = numbers_of(e); };
            const ArenaResult r = play_match(s);
            ASSERT_TRUE(r.error.empty() && r.seats.size() == 2);
            const ArenaSeatResult& a = r.seats[0];
            const ArenaSeatResult& b = r.seats[1];
            ASSERT_TRUE(a.kills + b.kills >= 4);                                                              // there was a fight
            ASSERT_TRUE(a.kills != a.losses || b.kills != b.losses);                                          // and it was not even (so a swap of the two fields changes a number)
            ASSERT_EQ(a.kills, b.losses);
            ASSERT_EQ(b.kills, a.losses);
            for (const ArenaSeatResult& seat : r.seats) {
                const EngineNumbers& e = engine[seat.spec.seat];
                ASSERT_EQ(seat.kills, e.kills);
                ASSERT_EQ(seat.losses, e.losses);
                ASSERT_EQ(seat.ants, e.alive);
                ASSERT_TRUE(seat.ants + seat.losses <= 3u + seat.hatched);                                    // three ants each and the ants that hatched since (the engine replaces a lost ant)
                ASSERT_EQ(seat.score, e.score);
                ASSERT_EQ(seat.banked + seat.raided, 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI4.8 read_seat_result Maps Every Field Of The Engine To The Right Field: Distinct Numbers Set On A Hand-Made Engine (Kills, Losses, Hatched, Eggs, Score, The Score Box With An Ally, Living Ants Without The Dead And The Drowning)") {
        sim::SimulationEngine sim;
        build_world(sim, 81, 12);
        sim.get_unit(ants_of(sim, 2)[0]).state = sim::UnitState::Dead;
        sim.get_unit(ants_of(sim, 2)[1]).state = sim::UnitState::Drowning;
        sim.kill_unit(ants_of(sim, 2)[2]);                                                                     // (the engine counts a removed ant as lost: the statistics are set after it)
        sim.record_player_stat(2, sim::StatType::EnemyKilled, 7);
        sim.record_player_stat(2, sim::StatType::FriendlyLost, 3);
        sim.record_player_stat(2, sim::StatType::NewHatched, 5);
        sim.set_player_eggs(2, 9);
        sim.set_player_score(2, 123);
        sim.set_player_score(3, 41);
        sim.form_alliance(2, 3);
        sim.tick();
        ArenaSeatResult r;
        r.spec = seat_spec(2, "idle", Level::Medium);
        r.banked = 77;                                                                                         // not the engine's: left as it was
        r.raided = 88;
        read_seat_result(sim, 2, r);
        ASSERT_EQ(r.kills, 7u);
        ASSERT_EQ(r.losses, 3u);
        ASSERT_EQ(r.hatched, 5u);
        ASSERT_EQ(r.eggs, 9u);
        ASSERT_EQ(r.score, 123);                                                                               // the individual score
        ASSERT_EQ(r.shown_score, 164);                                                                         // the score box: its own plus its ally's
        ASSERT_EQ(r.ants, 9u);                                                                                 // 12 less the dead one, the drowning one and the one that was removed
        ASSERT_TRUE(r.banked == 77u && r.raided == 88u && r.spec.seat == 2);
        ArenaSeatResult q;
        read_seat_result(sim, 3, q);
        ASSERT_TRUE(q.score == 41 && q.shown_score == 164 && q.ants == 12u && q.kills == 0u && q.losses == 0u && q.hatched == 0u);
    } TEST_END();

    TEST_CASE("AI4.9 The Arena Refuses A Map The Seated Teams Cannot Play (LevelData::validate(roster), As The Server And The Application Do): A Start Marker Outside The Grid Refuses The Roster That Has That Team And Only That; A Seat That Does Not Exist Is Refused Without Shifting By It") {
        assets::LevelData broken = level_of("TINY");
        int bad_team = -1;
        for (assets::AnthillSpawn& sp : broken.anthill_spawns) {
            if (sp.team_id < sim::MAX_PLAYERS && bad_team < 0) {
                sp.x = 999;                                                                                     // a start marker far outside the 31 x 31 grid
                bad_team = sp.team_id;
            }
        }
        ASSERT_TRUE(bad_team >= 0);
        // which seat is it? the one whose roster the level refuses
        int unplayable_seat = -1;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            if (!broken.validate(static_cast<uint8_t>(1u << t)).playable) unplayable_seat = t;
        }
        ASSERT_TRUE(unplayable_seat >= 0);
        ASSERT_FALSE(broken.validate(0x0F).playable);
        std::vector<BotSpec> all;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) all.push_back(seat_spec(seat, "idle", Level::Medium));
        ArenaSpec s;
        s.level = &broken;
        s.seed = 1;
        s.max_ticks = 40;
        s.bots = all;
        const ArenaResult refused = play_match(s);
        ASSERT_FALSE(refused.error.empty());
        ASSERT_TRUE(refused.error.find("cannot be played") != std::string::npos);
        ASSERT_TRUE(refused.ticks == 0 && refused.seats.empty());
        std::vector<BotSpec> without;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            if (seat != unplayable_seat) without.push_back(seat_spec(seat, "idle", Level::Medium));
        }
        s.bots = without;
        const ArenaResult fine = play_match(s);                                                                // the same map, the other seats: it plays
        ASSERT_TRUE(fine.error.empty() && fine.ticks == 40 && fine.seats.size() == 3);
        // seats that do not exist: refused with a reason (the shift of 1u by 32 or 200 was undefined)
        for (const uint8_t seat : {uint8_t{4}, uint8_t{8}, uint8_t{31}, uint8_t{32}, uint8_t{64}, uint8_t{200}, uint8_t{255}}) {
            ArenaSpec t;
            t.level = &level_of("TINY");
            t.seed = 1;
            t.max_ticks = 10;
            t.bots = {seat_spec(0, "idle", Level::Medium), seat_spec(seat, "idle", Level::Medium)};
            const ArenaResult r = play_match(t);
            ASSERT_FALSE(r.error.empty());
            ASSERT_TRUE(r.ticks == 0 && r.seats.empty());
            t.bots = {seat_spec(seat, "idle", Level::Medium)};
            ASSERT_FALSE(play_match(t).error.empty());
        }
    } TEST_END();

    TEST_CASE("AI4.10 The Arena Empties The News And Audio Queues Every Tick (And Counts What They Held): A Match With Orders Posts Events, No Queue Ever Holds More Than A Tick's Worth") {
        ArenaSpec s = arena_spec("TINY", 4, 1500, 3, {seat_spec(0, "worker", Level::Hard), seat_spec(1, "worker", Level::Hard), seat_spec(2, "worker", Level::Hard), seat_spec(3, "worker", Level::Hard)});
        s.factory = [](const BotSpec&) { return std::make_unique<Harvester>(false); };
        const ArenaResult r = play_match(s);
        ASSERT_TRUE(r.error.empty());
        const uint64_t total = r.news_events + r.audio_events;
        ASSERT_TRUE(total > 100);                                                                               // the counters are live: orders post sounds and news
        ASSERT_TRUE(r.peak_queue >= 1 && r.peak_queue < 40);                                                    // one tick's worth ...
        ASSERT_TRUE(static_cast<uint64_t>(r.peak_queue) * 10 < total);                                         // ... and not the whole match: a queue that was never emptied would hold all of it
        ArenaSpec idle = arena_spec("TINY", 4, 600, 3, {seat_spec(0, "idle", Level::Medium), seat_spec(1, "idle", Level::Medium)});
        const ArenaResult q = play_match(idle);
        ASSERT_TRUE(q.error.empty() && q.peak_queue < 10);
    } TEST_END();

    TEST_CASE("AI4.13 The Score Ledger: A Rise Between Two Ticks Is Banked, A Fall Is Raided; The Price Of An Egg (A Hatch Command, Or The Egg The Engine Starts By Itself For A Team That Lost Its Last Ant) Is Neither, And A Deposit In The Same Tick Still Counts") {
        sim::SimulationEngine sim;
        build_world(sim, 91, 1);                                                                               // one ant per team
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) sim.set_player_eggs(t, 3);
        ScoreLedger ledger;
        ledger.start(sim);
        const auto step = [&]() {
            sim.tick();
            ledger.sample(sim);
        };
        sim.set_player_score(0, 120);
        sim.set_player_score(1, 90);
        step();
        ASSERT_TRUE(ledger.banked(0) == 120u && ledger.banked(1) == 90u && ledger.raided(0) == 0u && ledger.raided(1) == 0u);
        sim.set_player_score(1, 60);                                                                           // a raid took 30
        step();
        ASSERT_TRUE(ledger.raided(1) == 30u && ledger.banked(1) == 90u);
        ASSERT_TRUE(ledger.banked(7) == 0u && ledger.raided(7) == 0u);                                         // a seat that does not exist
        // a hatch command costs 200 while it is applied: that is neither a deposit nor a raid, and a deposit in the tick right after it is still a deposit
        sim.set_player_score(0, 500);
        step();
        ASSERT_EQ(ledger.banked(0), 500u);
        ASSERT_TRUE(sim.try_hatch(0) == sim::SimulationEngine::HatchResult::Started);
        sim.set_player_score(0, sim.get_player_score(0) + 50);                                                 // a deposit of 50 in the next tick
        step();
        ASSERT_EQ(sim.get_player_score(0), 350);
        ASSERT_TRUE(ledger.raided(0) == 0u && ledger.banked(0) == 550u);
        // the engine's own hatch for a team that lost its last ant (it charges min(score, 200) too): seat 2 has 150 points
        sim.set_player_score(2, 150);
        step();
        ASSERT_EQ(ledger.banked(2), 150u);
        sim.kill_unit(ants_of(sim, 2)[0]);
        step();
        ASSERT_TRUE(sim.get_player_hatched(2) == 1u && sim.get_player_score(2) == 0);                          // (the premise: the engine hatched and charged)
        ASSERT_EQ(ledger.raided(2), 0u);
        ASSERT_EQ(ledger.banked(2), 150u);
        // and a fall without any hatch is a raid
        sim.set_player_score(3, 80);
        step();
        sim.set_player_score(3, 10);
        step();
        ASSERT_TRUE(ledger.banked(3) == 80u && ledger.raided(3) == 70u);
        ASSERT_TRUE(ledger.banked(0) == 550u && ledger.raided(0) == 0u);                                       // (and the price of seat 0's egg was not taken off again at every tick since)
    } TEST_END();

    TEST_CASE("AI4.11 The Match Seed Reaches The Bots And The Seats Come Back In Order: A Bot's Own Seed Differs From Seed To Seed And From Seat To Seat, Is Reproducible, And Seats Given In Reverse Order Are Reported In Seat Order") {
        std::array<std::array<Walker::Notes, 4>, 3> notes;
        const auto play = [&](uint32_t seed, size_t slot, bool reversed) {
            std::vector<BotSpec> bots;
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) bots.push_back(seat_spec(seat, "worker", Level::Medium));
            if (reversed) std::reverse(bots.begin(), bots.end());
            ArenaSpec s = arena_spec("TINY", seed, 30, 3, bots);
            s.factory = [&](const BotSpec& b) { return std::make_unique<Walker>(&notes[slot][b.seat]); };
            return play_match(s);
        };
        const ArenaResult one = play(1, 0, false);
        const ArenaResult again = play(1, 1, false);
        const ArenaResult two = play(2, 2, true);
        ASSERT_TRUE(one.error.empty() && two.error.empty());
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            ASSERT_EQ(notes[0][seat].rng_seed, notes[1][seat].rng_seed);                                       // reproducible
            ASSERT_TRUE(notes[0][seat].rng_seed != notes[2][seat].rng_seed);                                   // another match seed, another bot seed
            if (seat > 0) ASSERT_TRUE(notes[0][seat].rng_seed != notes[0][seat - 1].rng_seed);                // and every seat has its own
        }
        ASSERT_EQ(two.seats.size(), 4u);
        for (size_t i = 0; i < 4; ++i) {
            ASSERT_EQ(two.seats[i].spec.seat, static_cast<uint8_t>(i));                                        // reversed in, in order out
            ASSERT_EQ(one.seats[i].spec.seat, static_cast<uint8_t>(i));
        }
    } TEST_END();

    TEST_CASE("AI4.12 A Replay Is Checked Completely: A Checkpoint That Was Never Reached, A Command After The Last Step, A Wrong Last Hash And A Wrong Checkpoint Each Fail It, And An Added Command Fails It Whoever Gave The Command (Twelve Seeds)") {
        const ArenaSpec s = [] {
            ArenaSpec a = arena_spec("TINY", 7, 800, 3, {seat_spec(0, "worker", Level::Hard), seat_spec(1, "worker", Level::Hard), seat_spec(2, "worker", Level::Hard), seat_spec(3, "worker", Level::Hard)});
            a.record = true;
            a.factory = [](const BotSpec&) { return std::make_unique<Walker>(); };
            return a;
        }();
        const ArenaResult played = play_match(s);
        ASSERT_TRUE(played.error.empty() && played.log.size() > 100);
        ASSERT_TRUE(replay_commands(s, played).ok);
        ArenaResult extra_checkpoint = played;
        extra_checkpoint.checkpoints.push_back(played.hash);                                                    // the original claims one more sample than the replay can make
        ASSERT_FALSE(replay_commands(s, extra_checkpoint).ok);
        ArenaResult after_end = played;
        RecordedCommand late = played.log.back();
        late.step = played.steps + 5;
        late.tick = played.steps + 5;
        after_end.log.push_back(late);
        const ReplayResult rl = replay_commands(s, after_end);
        ASSERT_FALSE(rl.ok);
        ASSERT_TRUE(rl.error.find("after the last step") != std::string::npos);
        ArenaResult wrong_hash = played;
        wrong_hash.hash ^= 1;
        ASSERT_FALSE(replay_commands(s, wrong_hash).ok);
        ArenaResult wrong_checkpoint = played;
        wrong_checkpoint.checkpoints[3] ^= 1;
        const ReplayResult rc = replay_commands(s, wrong_checkpoint);
        ASSERT_TRUE(!rc.ok && rc.first_bad_tick == 4 * kArenaHashPeriod);
        ArenaResult without_checkpoints = played;
        without_checkpoints.checkpoints.clear();
        ASSERT_FALSE(replay_commands(s, without_checkpoints).ok);
        // a command added at step 40 for an ant that belongs to the issuer who gave the command: whoever the seat, the replay notices
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            ArenaSpec t = s;
            t.seed = seed;
            t.max_ticks = 600;
            const ArenaResult p = play_match(t);
            ASSERT_TRUE(p.error.empty() && !p.log.empty() && replay_commands(t, p).ok);
            ArenaResult tampered = p;
            RecordedCommand more = p.log.front();                                                                // issuer and ant belong together
            more.command.tile_x = 2;
            more.command.tile_y = 2;
            more.step = 40;
            more.tick = 40;
            tampered.log.insert(tampered.log.begin(), more);
            std::stable_sort(tampered.log.begin(), tampered.log.end(), [](const RecordedCommand& a, const RecordedCommand& b) { return a.step < b.step; });
            ASSERT_FALSE(replay_commands(t, tampered).ok);
        }
    } TEST_END();
}

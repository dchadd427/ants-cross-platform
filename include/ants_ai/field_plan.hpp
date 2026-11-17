#pragma once

// The field breach (docs/BOT_GATES.md, "The field breach"): how ONE ant opens the way to a pile that lies beyond a field of bombs, fire walls and water, where the carriers' way from
// the hill crosses fire cells or bombs and a walk with only one kind of them open does not exist (the pile is Sealed or Mixed for GateInfo: POPcOrN and its family). A pure function of
// the grid as it is and the look: no orders, no clock, no randomness. FieldTask (field_task.hpp) carries the steps out.
//
//   route       the carriers' way from the hill to the pile, walking and swimming: its fire cells are put out by a Fire Ant, its bombs defused by a Bomber (the cheapest pile of the
//               breach: FieldHints::piles, else the piles that are Sealed or Mixed at this look)
//   fire way    the way of a Fire Ant from its source (one of the bot's Fire Ants, or a Fire power-up) to a side of a fire cell of the route: it walks through fire walls; a bomb
//               on it is defused first, a water tile on it is bridged first (a Fire Ant can do neither)
//   swim way    the way of a Swimmer from its source to the land beside the water of the fire way (it swims; a bomb on it is defused first)
//   the work    the bombs of the three ways and of the carriers' walks between the hill and the pile (a Bomber's: the engine's walk goes over another team's bomb, so every walk
//               within half a step of the cheapest, and the bombs round the pile), the water of the fire way (a Swimmer's), the fire cells of the route (a Fire Ant's), in this
//               order: a Swimmer cannot defuse, a Fire Ant can neither defuse nor swim. A route that swims needs a Swimmer to carry at the end.
//   the step    what the breach ant does next: it takes a power-up of the kind the work needs, steps one tile on its way, or works a target from a side of it (N, S, W or E); a
//               Bomber that no dig brings to its work hops with a bomb of its own (the fewest bombs first: where a chain over the map's bombs stops at a dud the ant stays).
//               An ant that took a power-up leaves its own on a free land tile beside the one it takes: it never takes one where that would shut it in.
//               The engine's paths walk over bombs, and an ability is worked from the first free side of the target nearest the ant (N, S, W, E; a bomb counts as free):
//               so in the field the ant steps to a tile next to it when the engine's own walk (the port of its A*, EngineWalks) to a tile further on would cross a bomb, and
//               walks several tiles when that walk crosses none; a bomb ahead is defused with ONE order (the engine walks to the side it takes) when the walk to that side
//               crosses none either. An ant that stands on the power-up it needs takes it with a click on its own tile. A plain ant of a seat that has no ant to start the work
//               crosses ONE bomb of another team by setting it off (FieldHints::stalled; the rule is `set_off_ok` in field_plan.cpp).
//               An ant takes only a power-up that SERVES the work (FieldPlan::serving: the kind it then is has a way to the ground the work is done on), and a Bomber that no single
//               hop brings to such a power-up hops first to ground from which a second hop does (a fire wall, then a lake). No bomb is planted on a tile that an ant stands on.

#include <cstdint>
#include <vector>

#include "ants_ai/bot_view.hpp"
#include "ants_ai/gate_info.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/ant_unit.hpp"

namespace ants::ai {

/// What the breach ant does next
enum class FieldAction : uint8_t {
    None,         // nothing: no pile behind the field, or the work is done
    Take,         // a move onto the power-up at `tile` (next to the ant, or the ant's own tile: the click of a person on the power-up under an ant): the ant becomes its kind
    Step,         // a move onto `tile`, next to the ant
    Walk,         // a move to `tile`, several tiles away, by a walk that the engine's own search finds clear of bombs of other teams (EngineWalks)
    Defuse,       // a special order on the bomb at `tile`: the ant stands on a side of it
    Bridge,       // a special order on the water at `tile`: a Swimmer stands on a side of it
    Extinguish,   // a special order on the fire cell at `tile`: a Fire Ant stands on a side of it
    Plant,        // a special order on the free tile `tile` (the bomb of `hop`): a Bomber stands on a side of it
    Fly,          // a move onto the own bomb at `tile` (hop.bomb) from hop.from, where the ant stands: the throw of `hop`
    SetOff,       // a move onto the bomb of another team at `tile`, next to the ant: a plain ant sets it off (2 hit points, thrown back; the bomb is gone) to get across
    Heal,         // into the hill: the ant is on the hill's side and has not the hit points of a full ant for the hop it needs
    Stuck,        // nothing this ant can do reaches the work
};

/// The work, in the order of the breach
enum class FieldWork : uint8_t { None, Bombs, Bridges, Fire, Carrier };

struct FieldStep {
    FieldAction action{FieldAction::None};
    FieldWork work{FieldWork::None};
    sim::TileCoord tile{-1, -1};
    BombHop hop;                         // the hop chosen (Plant, Fly, and the digs and steps that prepare it): bomb T, from P, facing, the chain
};

/// The analysis of one look (see the top)
struct FieldPlan {
    bool field{false};                   // a pile lies behind the field: its best route has fire cells or bombs, and it is Sealed or Mixed for the hill
    uint32_t pile{0};                    // the pile of the route (MapInfo::piles() index)
    bool swim{false};                    // the route swims: its carriers are Swimmers
    bool has_swimmer{false};             // the bot has a Swimmer
    int8_t carrier_at{-1};               // the breach ant is a Swimmer next to the end of a route that swims (1), or a Swimmer away from it (0); -1: neither
    std::vector<sim::TileCoord> route;   // from the hill to the tile next to the pile
    std::vector<sim::TileCoord> fires;   // the route's fire cells
    std::vector<sim::TileCoord> bombs;   // the Bomber's work: the bombs of the route, of the fire way and of the swim way, each once
    std::vector<sim::TileCoord> waters;  // the Swimmer's work: the water tiles of the fire way
    sim::TileCoord fire_source{-1, -1};  // where the Fire Ant comes from: a Fire power-up, or the tile of one of the bot's Fire Ants
    sim::TileCoord swim_source{-1, -1};  // where the Swimmer comes from
    std::vector<sim::TileCoord> fire_way;
    std::vector<sim::TileCoord> swim_way;
    /// The power-ups of the kind the work needs that SERVE it: the ant that takes one has a way, with the moves of its new kind (a Swimmer swims, a Fire Ant walks through fire; the bombs on the way
    /// count as work that a Bomber does and the water on a Fire Ant's way as work that a Swimmer does), to the ground the work is done on (a tile of the route or of the ways). A Swimmer power-up on a strip that fire walls cut
    /// off from all of it is not one: the Bomber that hopped there would be a Swimmer that can do nothing. Only meaningful with `serving_known` (the Bomber's work is reached by a hop as well as by
    /// a dig: every Bomber power-up serves).
    std::vector<sim::TileCoord> serving;
    bool serving_known{false};
    bool serves(sim::TileCoord token) const noexcept;
    /// The work that is next: the bombs, then the bridges, then the fire, then (a route that swims, with no Swimmer) a Swimmer
    FieldWork work() const noexcept;
    /// Every kind of work has a source: a Fire Ant's way to the route's fire, a Swimmer's way to the fire way's water (the bot decides at the start whether it plays the breach)
    bool feasible() const noexcept { return field && (fires.empty() || !fire_way.empty()) && (waters.empty() || !swim_way.empty()); }
};

/// The rules of the breach that a plan switch can turn off (LevelPlan::field_*, `bot_arena --tune fpick=0,fwalk=0,fsetoff=0,fserve=0,freplan=0`): all on is the bot that ships (the second hop and the planted tile have no switch)
struct FieldRules {
    bool pick{true};                     // an ant that stands on the power-up it needs takes it with a click on its own tile (else it steps off and back on, and a boxed-in ant is Stuck)
    bool walk{true};                     // the walks and the defuses of the dig are ordered by the engine's own walk (EngineWalks), several tiles at a time (else: over ground with no bomb on or next to it)
    bool set_off{true};                  // a seat that has no ant to start the work lets a plain ant set one bomb off to get to a power-up of the kind (FieldHints::stalled)
    bool serve{true};                    // an ant takes only a power-up whose kind then reaches the work from it (FieldPlan::serving); else any power-up of the kind
    bool replan{true};                   // a Stuck step gets one fresh plan (the ways and sources kept from before dropped) before its ant is given up; else it is given up at once
};

/// What the task keeps from one step to the next. The plan of one look depends on what the work has done (a dug corridor makes other fire cells and sources cheaper): the route and the
/// sources chosen are kept while they hold, so the work goes on toward the same targets, and the hop under way is kept while it holds
struct FieldHints {
    std::vector<uint32_t> piles;         // the piles of the breach (Tactics::field_piles); empty: the piles that are Sealed or Mixed at this look
    uint32_t ant{0};                     // the breach ant: the source of its kind
    uint32_t pile{0};
    std::vector<sim::TileCoord> route;   // empty: none chosen yet
    sim::TileCoord fire_source{-1, -1};
    sim::TileCoord swim_source{-1, -1};
    std::vector<sim::TileCoord> fire_way;
    std::vector<sim::TileCoord> swim_way;
    BombHop hop;                         // bomb T, from P, facing; hop.bombs == 0: none
    /// The bombs of the seat (or its ally) that lay on the map at the start (many levels give the map's bombs to team 0): defused like the map's other bombs. Unknown (before the first
    /// look of the task): every bomb of the seat counts as one of them
    std::vector<sim::TileCoord> map_bombs;
    bool map_bombs_known{false};
    FieldRules rules;
    /// No ant of the bot could start the work for FieldTask::Params::stalled_ticks: a plain ant may cross one bomb by setting it off (rules.set_off)
    bool stalled{false};
};

/// The pile, its route and the work, from the grid of the look (`view.grid()`). Empty (field false) without a grid or a hill, or when no pile is behind the field.
FieldPlan plan_field(const BotView& view, const MapInfo& map, uint8_t seat, const FieldHints& hints);

/// The next step of the breach ant `ant` for the work of `plan`
FieldStep next_field_step(const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant, const FieldHints& hints);

/// What it costs `ant` to become the breach ant for the work of `plan`: 0 for an ant of the kind the work needs, else its way to the nearest power-up of that kind; more for an ant
/// away from the hill's side or hurt. -1: it cannot get one.
int32_t field_ant_cost(const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant, const FieldHints& hints = FieldHints{});

/// The breach can start: an ant of the bot gets the kind of the first work (field_ant_cost)
bool field_startable(const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat, const FieldHints& hints = FieldHints{});

}  // namespace ants::ai

#pragma once

// The rules of the fights that answer a blow (FightTask, standard_tasks.hpp), kept apart from the task so that a test can ask them one by one, and the numbers that every fight of the standard bot
// is weighed with: what a blow takes, how many blows kill, and the strength of a group. docs/BOT_PEOPLE.md, "Step 1: everyone hits back", has the measurements.
//
//   who answers   can_defend       any ant that can strike (LevelPlan::defend_all_types), apart from the few that a job of their own holds; the ant that was struck first of all (victim_answers)
//   how many      defence_wanted   as many as the enemy has near the target, and defend_extra more, at most defend_max (defend_by_strength)
//   how long      keep_defence     the fight is kept up through the target's hit clips while the force is as strong as the enemy's near the target, the target is within the leash and blows fall
//
// Everything here reads what a person of the seat sees: the types, tiles and hit points of the ants on the view, the bot's own ledger of its tasks, and its plan.

#include <cstddef>
#include <cstdint>

#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

/// The real numbers of a blow (docs/GAME_REVERSE_ENGINEERING.md 5.36): every ant has 10 hit points, a Combat Ant's punch takes 2 of them and every other blow 1
uint32_t blow_damage(sim::AntType type) noexcept;

/// The blows that an ant of `damage` per blow needs to take `hp` hit points (0 for an ant that has none)
uint32_t blows_to_kill(uint32_t hp, uint32_t damage) noexcept;

/// The strength of a group in a fight, the number of the square law of attrition: the sum of the hit points times the sum of the damage of one round of blows
struct Strength {
    uint32_t hp{0};
    uint32_t dmg{0};
    uint32_t n{0};
    void add(uint32_t h, uint32_t d) noexcept {
        hp += h;
        dmg += d;
        ++n;
    }
    uint64_t value() const noexcept { return static_cast<uint64_t>(hp) * dmg; }
};

/// What answers a blow near `centre`: the enemy ants within `radius` tiles that do not carry food (a carrier is no fighter) and are not `exclude`, the `cap` strongest of them. The ally's ants
/// are no enemies. Hit points are on the view for every ant (a project decision).
Strength enemy_strength(const BotView& view, sim::TileCoord centre, int32_t radius, uint32_t exclude, uint32_t cap);

/// Whether `ant` may be taken into a fight that answers a blow (the defence rules of the plan: defend_all_types). It must take orders (not be stunned), hold nothing, have at least 4 hit
/// points and not stand on a power-up, and it must not be
///   the Fire Ant that keeps the walls of the own thief hole, while a thief threatens (tactics.wall_keeper, wall_demand);
///   a Thief in its raid clip, or one that the raid task holds within 6 tiles of the raid tile of an enemy hill (it is about to raid);
///   an ant of the island play (the crew of the expedition, the Swimmers that dig) or of a task that the fights never take from (its rank is the fight's or higher: another fight, a gate goal, the rush).
/// The ants of the other tasks (a pick-up trip, the walls that no thief threatens, the mines, the bombs, the sabotage, the strike, the harassment, the economy) may be taken: they go back to their
/// task when the fight ends. `struck`: the ant is the one that was struck, and it may still be in its hit clip (it takes orders as soon as the clip is over).
bool can_defend(const BotView& view, const AntView& ant, const Tactics& tactics, const AntLedger& ledger, const MapInfo& map, bool struck = false);

/// How many ants a fight that answers a blow asks for: the level's defenders (one more in the last minute of a leader that guards); with defend_by_strength as many as the enemy has near the target
/// (`enemies`) and plan.defend_extra more, at least the level's defenders and at most plan.defend_max
size_t defence_wanted(const LevelPlan& plan, size_t enemies, bool guard_stance) noexcept;

/// Whether a fight that answers a blow is kept up (defend_by_strength): the own strength is at least plan.defend_keep_percent of the enemy's, the target is no further than the leash from
/// the place of the first blow (`from_anchor`), and a blow fell within plan.fight_linger_ticks times 5 ticks (`since_blow`: a blow on an own ant, or one that hurt the target)
bool keep_defence(const LevelPlan& plan, const Strength& own, const Strength& enemy, int32_t from_anchor, uint64_t since_blow) noexcept;

/// What a fight that answers a blow looks at once per look (defend_by_strength): the number of defenders it asks for and the strength of the enemy ants within plan.defend_radius of the target
struct DefenceFrame {
    size_t wanted{0};
    Strength theirs;
};
DefenceFrame defence_frame(const BotView& view, const LevelPlan& plan, sim::TileCoord target, bool guard_stance);

}  // namespace ants::ai

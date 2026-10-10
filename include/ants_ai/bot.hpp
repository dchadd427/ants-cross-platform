#pragma once

// The vocabulary of the computer players (docs/BOTS.md): difficulty levels and their profiles, the specification of a bot seat as the command line and a
// room name it, the checks that decide whether a game with bots may start, and the Bot interface with the one door that leads out of it (Orders).
//
// A bot is a VIRTUAL CLIENT (project rule 8): it reads the world through a read-only BotView and sends ants::sim::Commands through the same door a
// person uses. It never touches the simulation, so a game without bots runs none of this code and every golden hash stays as it is.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ants_sim/command.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

class BotView;
class MapInfo;

// ---- levels and profiles -------------------------------------------------------------------------------------------------------------------------

enum class Level : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

/// "easy", "medium", "hard"
const char* level_name(Level level) noexcept;
/// The words of level_name, in either case; false (and `out` unchanged) for anything else
bool parse_level(std::string_view text, Level& out) noexcept;

// ---- styles -------------------------------------------------------------------------------------------------------------------------------------------

/// The STYLE of a standard bot (docs/BOTS.md, "Styles"): which tactics it prefers and how early it plays them, so that bots of one level do not all play alike. A bot draws its style at
/// the start of a match from its own seat's generator (the same match seed and seat give the same style; a replay needs no bot anyway), or the spec pins it (`--bot 2:standard:hard:raider`).
/// The style never changes what a level may do (the profile: look interval, reaction time, command budget; and the tactics that the level unlocks): it picks among them and says how early
/// and how hard. What each one does is in tactics.cpp, plan_for(level, style, rng):
///   Aggressive  one more ant for the contest of the middle of the map in the opening, one more defender, raids for two thirds of the loot, and the harassment squad: its Combat Ants go
///               for the carriers of the best opponent that are within eight tiles of them (at Medium only with a clear advantage); at Hard also a second Combat Ant, the sabotage of
///               the best opponent's gate with a stolen Fire Ant, and the strike when it is behind
///   Economic    no contest of the middle, the fire walls of the thief hole before a thief shows, raids only for three times the loot
///   Raider      Fire and Thief first in the opening (no Bomber), raids for half the loot, a shorter wait after a hill that could not be reached
///   Defensive   the fire walls before a thief shows, one defender more, the ally's blows answered, no contest of the middle
/// Hard bots are Aggressive or Raider only (Hard bots should be really aggressive); Easy keeps its plan (a style changes little there: only the numbers move).
enum class Style : uint8_t { Random = 0, Aggressive = 1, Economic = 2, Raider = 3, Defensive = 4 };

/// "random", "aggressive", "economic", "raider", "defensive"
const char* style_name(Style style) noexcept;
/// The words of style_name, in either case; false (and `out` unchanged) for anything else
bool parse_style(std::string_view text, Style& out) noexcept;
/// Whether a bot of the level may play the style: Random always, every style at Easy and Medium, Aggressive and Raider only at Hard
bool style_allowed(Level level, Style style) noexcept;

/// The most ants one command of a bot names: a chosen conservative cap (the remake's HUD lets a person send up to 32 = sim::kMaxCommandAnts with a drag or Ctrl+A;
/// how many the original's selection holds has not been established). A bot never exceeds it, whatever its profile says.
inline constexpr uint32_t kHudAntCap = 24;

/// Everything that makes a level. Times are simulation ticks of 50 ms, rates are thousandths of a command per second.
struct Profile {
    uint32_t decision_interval{20};      // the bot looks at the world only this often
    uint32_t reaction_delay{24};         // a command it proposes is released this many ticks later ...
    uint32_t jitter_percent{25};         // ... plus or minus this many percent, drawn from the seat's own generator
    uint32_t rate_milli_cps{1500};       // the token bucket's refill: commands per second x 1000
    uint32_t burst{6};                   // and its depth in commands
    uint32_t max_ants_per_command{24};   // a larger command is split (never above kHudAntCap)
    uint32_t intent_ttl{60};             // a command that cannot be paid is dropped this many ticks after its release time: the world has moved on
    uint32_t reissue_cooldown{10};       // no second order to the same ant within this many ticks (unless urgent): each order replaces the ant's queued path
    bool value_aware_piles{true};        // economy (the worker bot, B3): pick food piles by points per trip (Medium, Hard), not the nearest (Easy)
    uint32_t max_ants_per_pile{8};
};

Profile profile_for(Level level) noexcept;

// ---- the specification of a bot seat -------------------------------------------------------------------------------------------------------------

/// Which bot sits at which seat. `kind` is "idle" (stands still: the plumbing's test bot), "worker" (harvest only: the fixed yardstick of the tournaments) or "standard" (the bot of the
/// three levels, with its tactics and its style). `level` picks the Profile; `style` (only the standard bot has one) is pinned or drawn.
struct BotSpec {
    uint8_t seat{0};
    std::string kind{"standard"};
    Level level{Level::Medium};
    Style style{Style::Random};          // Random: the bot draws its own style at the start of the match (only the standard bot has a style)
};

/// True for the kinds that make_bot knows ("idle", "worker", "standard")
bool known_bot_kind(std::string_view kind) noexcept;

/// The text of --bot (after the option): `SEAT`, `SEAT:LEVEL` (easy, medium, hard), `SEAT:KIND` (idle, worker, standard), `SEAT:KIND:LEVEL`, `SEAT:LEVEL:STYLE` or `SEAT:KIND:LEVEL:STYLE`
/// (STYLE: aggressive, economic, raider, defensive, random), e.g. "2", "2:hard", "2:idle", "2:worker:easy", "2:hard:raider", "2:standard:medium:defensive". SEAT is one digit 0 to 3.
/// The default is the standard bot at medium level with a random style. On failure `error` says why.
bool parse_bot_spec(std::string_view text, BotSpec& out, std::string& error);

/// What the seat is called: "Bot (Medium)" for the standard bot (whatever its style), "Bot (Idle)", "Bot (Worker)": printable ASCII, at most 32 characters. A person can never take
/// a name that starts with "Bot (" (the lobby renames it), so the name tells a bot from a player.
std::string bot_display_name(const BotSpec& spec);

/// The facts of a game that is about to start: the seats that play, the seats of persons, the bots, and whether Fog of War is on
struct SetupInfo {
    uint8_t roster{0};                   // bit s: seat s takes part
    uint8_t human_mask{0};               // bit s: a person sits at seat s
    std::vector<BotSpec> bots;
    bool fog{false};
    bool allow_all_bots{false};          // only the headless arena plays a match without a person
    std::vector<std::string> extra_kinds;   // kinds that a factory supplies besides the registry's (the bench bots of the arena: ArenaSpec::extra_kinds); never set by the game or a room
};

/// "" when a game with these bots may start, else the reason it may not: Fog of War is on (a bot would see through it), a bot sits at a seat that is not in
/// the match, at a person's seat or at a seat that another bot has, a kind that does not exist, or nobody is a person.
std::string check_setup(const SetupInfo& info);

// ---- the door out of a bot ---------------------------------------------------------------------------------------------------------------------------

/// Which of a bot's wishes goes first when the budget is short
enum class Priority : uint8_t { Background = 0, Normal = 1, Urgent = 2 };

/// A command that a bot proposes. The issuer is not the bot's to say: the controller stamps the seat.
struct Intent {
    sim::Command command;
    Priority priority{Priority::Normal};
    /// A planned PICK-UP of a power-up: a plain click (GroupMove) of ONE ant on the tile of a power-up (Orders::pick_up). The controller lets a move onto a power-up tile through only
    /// with this mark: a click on one takes it for the ant that arrives, so no other order of a bot (a rally, a guard post, a spread) may name such a tile by accident.
    bool pickup{false};
    /// A TIMED CHAIN (Orders::chain, docs/BOTS.md "Timed clicks"): the intents of one chain have the same `chain` number (not 0) and `step` 0, 1, 2, ... The controller releases step 0 like any intent (the
    /// reaction time, the budget); every later step is released at the first tick at which the sink would APPLY it between `gap_lo` and `gap_hi` ticks after the step before was applied, and never
    /// otherwise: a step that would be applied later is dropped with the rest of its chain (Fate::Expired). A chain the first step of which never leaves leaves no step.
    uint32_t chain{0};
    uint32_t step{0};
    uint32_t gap_lo{0};
    uint32_t gap_hi{0};
};

/// One click of a timed chain: the tile, whether it is a planned pick-up (the tile holds a power-up), and for every step but the first the ticks between the moment the step before is applied and the
/// moment this one is
struct ChainStep {
    sim::TileCoord tile{};
    bool pickup{false};
    uint32_t gap_lo{0};
    uint32_t gap_hi{0};
};

/// The only way out of a bot: what a person could click, as data. The named methods cannot express Quit or Drop (push_unchecked exists for the tests of the
/// controller's filter, and the controller filters Quit, Drop and None whatever way they come).
class Orders {
public:
    /// A group move; more ants than one command may hold (kMaxCommandAnts) become several intents. An empty list is nothing.
    void move(const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority = Priority::Normal);
    /// A group attack on the ant that stands at `tile`. The controller lets it through only when an ant of another team (not an ally) stands there and the tile is not part of a
    /// hill: what a person's click on an enemy ant sends (the attack cursor shows over an ant of another colour, and an ant on a hill tile gets the plain move cursor).
    void attack(const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority = Priority::Urgent);
    /// A planned pick-up: ONE ant is clicked onto the tile of a power-up (a plain GroupMove, the click that takes it when the ant arrives). Nothing else may name a power-up tile.
    void pick_up(uint32_t ant, sim::TileCoord tile, Priority priority = Priority::Normal);
    /// The clicks of ONE ant at moments that are counted from the moment the click before is APPLIED (the sink's own tick of it, CommandSink::applied_at): a person who clicks again the moment the ant
    /// stands on a tile. What the controller does with them is in Intent (chain). The first step is an ordinary click (the reaction delay, the queue, the budget); every later one skips those but is paid for all the same: it leaves at its moment even when the bucket is empty, and the bucket goes into debt, which holds back the ordinary orders afterwards.
    void chain(uint32_t ant, const std::vector<ChainStep>& steps, Priority priority = Priority::Urgent);
    /// A special order (bomb, defuse, fire, extinguish, bridge, thief raid) of ONE ant: the HUD sends it for a single selected ant only
    void special(uint32_t ant, sim::TileCoord tile, Priority priority = Priority::Normal);
    void stop(const std::vector<uint32_t>& ants, Priority priority = Priority::Normal);
    void hatch();
    void invite(uint8_t other);
    void accept(uint8_t other);
    void deny(uint8_t other);
    void withdraw(uint8_t other);
    void break_alliance();

    const std::vector<Intent>& intents() const noexcept { return intents_; }
    void clear() noexcept {
        intents_.clear();
        chains_ = 0;
    }
    /// The chains that were put in (their `chain` numbers are 1 to this)
    uint32_t chains() const noexcept { return chains_; }

    /// For the tests of the controller's filter ONLY: puts any command into the list, however malformed (and, with `pickup`, marked as a planned pick-up whatever it names). No bot uses it.
    void push_unchecked(sim::Command command, Priority priority = Priority::Normal, bool pickup = false);

private:
    void group(sim::CommandType type, const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority);
    void alliance(sim::CommandType type, uint8_t other, Priority priority);
    std::vector<Intent> intents_;
    uint32_t chains_{0};
};

/// What a bot is told when it starts. Not the engine: the BotView (and the MapInfo, the analysis of the map as it stood at the start) is the only window a bot has
/// on the game, so what a person could not know cannot be read by a bot.
struct BotContext {
    uint8_t seat;
    Profile profile;
    uint64_t rng_seed;                   // a seed for the bot's own generator (BotRng): derived from the match seed, the seat and the kind
    const MapInfo* map{nullptr};         // the analysis of the match (owned by the controller, valid as long as the bot is seated); null in a context made by hand
};

class Bot {
public:
    /// What became of a command that think() proposed
    enum class Fate : uint8_t {
        Sent,                            // released through the budget at `tick` (with the ants that were still alive)
        Expired,                         // it could not be paid in time and was dropped
        Pruned,                          // every ant of it died first: it never left
        Superseded,                      // a newer order that was due at the same time named all its ants (the later click wins): it never left
        Filtered                         // the controller's filter refused it when it was proposed (what a person could not click): it never left; `tick` is the tick of the look
    };

    virtual ~Bot() = default;
    virtual const char* kind() const noexcept = 0;
    /// Once, when the seat is added to the match
    virtual void start(const BotContext& context) = 0;
    /// Every decision_interval ticks: look at the view, put wishes into `orders`
    virtual void think(const BotView& view, Orders& orders) = 0;
    /// A bot never sees the answer of the simulation to a command (a lock-step room gives none): it finds the result in the next view
    virtual void on_command(const sim::Command& command, Fate fate, uint64_t tick) {
        (void)command;
        (void)fate;
        (void)tick;
    }
};

/// The bot of a spec; null for a kind that does not exist. "idle" is the IdleBot, "worker" the WorkerBot (B3), "standard" the StandardBot (B4-1) of the spec's level, playing the
/// spec's style or one that it draws from its own seat's generator at start().
std::unique_ptr<Bot> make_bot(const BotSpec& spec);

}  // namespace ants::ai

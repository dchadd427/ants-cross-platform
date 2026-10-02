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

/// Which bot sits at which seat. `kind` is "idle" (stands still: the plumbing's test bot), "worker" (harvest only, B3) or "standard" (the bot of the three levels;
/// until B4 exists it is an alias of the worker). `level` picks the Profile.
struct BotSpec {
    uint8_t seat{0};
    std::string kind{"standard"};
    Level level{Level::Medium};
};

/// True for the kinds that make_bot knows ("idle", "worker", "standard")
bool known_bot_kind(std::string_view kind) noexcept;

/// The text of --bot (after the option): `SEAT`, `SEAT:LEVEL` (easy, medium, hard), `SEAT:KIND` (idle, worker, standard) or `SEAT:KIND:LEVEL`, e.g.
/// "2", "2:hard", "2:idle", "2:worker:easy". SEAT is one digit 0 to 3. The default is the standard bot at medium level. On failure `error` says why.
bool parse_bot_spec(std::string_view text, BotSpec& out, std::string& error);

/// What the seat is called: "Bot (Medium)" for the standard bot, "Bot (Idle)", "Bot (Worker)": printable ASCII, at most 32 characters. A person can never take
/// a name that starts with "Bot (" (the lobby renames it), so the name tells a bot from a player.
std::string bot_display_name(const BotSpec& spec);

/// The facts of a game that is about to start: the seats that play, the seats of persons, the bots, and whether Fog of War is on
struct SetupInfo {
    uint8_t roster{0};                   // bit s: seat s takes part
    uint8_t human_mask{0};               // bit s: a person sits at seat s
    std::vector<BotSpec> bots;
    bool fog{false};
    bool allow_all_bots{false};          // only the headless arena plays a match without a person
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
};

/// The only way out of a bot: what a person could click, as data. The named methods cannot express Quit or Drop (push_unchecked exists for the tests of the
/// controller's filter, and the controller filters Quit, Drop and None whatever way they come).
class Orders {
public:
    /// A group move; more ants than one command may hold (kMaxCommandAnts) become several intents. An empty list is nothing.
    void move(const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority = Priority::Normal);
    /// A group attack on the ant that stands at `tile`
    void attack(const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority = Priority::Urgent);
    /// A special order (bomb, defuse, fire, extinguish, bridge, thief raid) of ONE ant: the HUD sends it for a single selected ant only
    void special(uint32_t ant, sim::TileCoord tile, Priority priority = Priority::Normal);
    void stop(const std::vector<uint32_t>& ants);
    void hatch();
    void invite(uint8_t other);
    void accept(uint8_t other);
    void deny(uint8_t other);
    void withdraw(uint8_t other);
    void break_alliance();

    const std::vector<Intent>& intents() const noexcept { return intents_; }
    void clear() noexcept { intents_.clear(); }

    /// For the tests of the controller's filter ONLY: puts any command into the list, however malformed. No bot uses it.
    void push_unchecked(sim::Command command, Priority priority = Priority::Normal);

private:
    void group(sim::CommandType type, const std::vector<uint32_t>& ants, sim::TileCoord tile, Priority priority);
    void alliance(sim::CommandType type, uint8_t other, Priority priority);
    std::vector<Intent> intents_;
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
        Superseded                       // a newer order that was due at the same time named all its ants (the later click wins): it never left
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

/// The bot of a spec; null for a kind that does not exist. "idle" is the IdleBot, "worker" the WorkerBot (B3); "standard" is an alias of the worker bot until the standard bot
/// (B4) exists (kind() of what it returns says "worker").
std::unique_ptr<Bot> make_bot(const BotSpec& spec);

}  // namespace ants::ai

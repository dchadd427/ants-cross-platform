#pragma once

// The refusal memory (docs/BOT_UNSTUCK.md, "An order that is refused is not given again and again"; plan.refusal_memory): a standard bot learns from the ants that stand where they stood after an
// order. An order that left and was not carried out (the ant is still on its tile, idle or in the can't-go clip, a while after, and no path is on its way) is a REFUSAL of that kind of click by
// that ant onto that tile. The second refusal of the same click within a window blocks it for a time that doubles with every refusal, and the click is taken out of the orders of a look before
// they leave (the tasks are told like for a click that the controller refuses, Bot::Fate::Filtered, so none of them proposes it again at every look): "Can't go there." is heard once or twice
// for a click that cannot work, not for the rest of the match. The first refusal is never a block: a team-mate on the doorway, a bomb that goes off or a fire wall that burns out make a click work
// the next time.
//
// It reads the view and nothing else, and it changes no order that a task proposed but takes ants out of one that was refused before.

#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"

namespace ants::ai {

class RefusalMemory {
public:
    struct Params {
        uint32_t settle_ticks{40};           // an order is judged this long after it left (the path manager answers one request a tick; the clip of the can't-go state is 8 to 18 ticks)
        uint32_t watch_ticks{240};           // and not after this long (an ant that walks about for longer than that obeyed)
        uint32_t window_ticks{1500};         // a refusal is forgotten after this long without another
        uint32_t first_block_ticks{600};     // the block after the second refusal; it doubles with every one after
        uint32_t max_block_ticks{4800};
    };
    RefusalMemory() = default;
    explicit RefusalMemory(const Params& params) : params_(params) {}

    /// At every look, before the tasks: judges the orders that left (and forgets the ones that were obeyed)
    void look(const BotView& view);
    /// An order that LEFT (Bot::Fate::Sent) at `tick`: the ants it names are watched, from the tile they stood on at the last look
    void sent(const sim::Command& command, uint64_t tick);
    /// Takes the ants whose click is blocked out of the orders of this look and returns the commands that were taken out whole or in part (as proposed: the ants that were taken out, the tile). A group
    /// order keeps the ants that may go; one with none left goes whole. Timed chains are never touched.
    std::vector<sim::Command> filter(const BotView& view, Orders& orders);

    // ---- for the tests and the reports ----
    uint32_t refusals() const noexcept { return refusals_; }              // orders that were judged refused (one for each ant and click)
    uint32_t blocked() const noexcept { return blocked_; }                // ants taken out of an order because their click was blocked
    bool is_blocked(uint32_t ant, sim::CommandType type, sim::TileCoord tile, uint64_t tick) const noexcept;
    const Params& params() const noexcept { return params_; }

private:
    struct Key {
        uint32_t ant{0};
        uint8_t type{0};
        int16_t x{0}, y{0};
        bool operator<(const Key& o) const noexcept { return std::tie(ant, type, x, y) < std::tie(o.ant, o.type, o.x, o.y); }
    };
    struct Watch {
        Key key;
        uint64_t sent{0};
        sim::TileCoord from{};
    };
    struct Record {
        uint32_t count{0};
        uint64_t last{0};
        uint64_t until{0};
    };
    static Key key_of(uint32_t ant, const sim::Command& c) noexcept { return Key{ant, static_cast<uint8_t>(c.type), c.tile_x, c.tile_y}; }
    Params params_;
    std::vector<Watch> watching_;
    std::map<Key, Record> records_;
    struct Seen {
        sim::TileCoord tile{};
        bool loaded{false};                                                // it holds food or loot: a carrier stands at the gate in the queue of its hill, which is no refusal
    };
    std::map<uint32_t, Seen> seen_;                                       // where every own ant stood at the last look
    std::vector<sim::TileCoord> hills_;                                   // the origins of the hills (the walk into a mound ends in the queue of the hill, which is no refusal either)
    uint32_t refusals_{0};
    uint32_t blocked_{0};
};

}  // namespace ants::ai

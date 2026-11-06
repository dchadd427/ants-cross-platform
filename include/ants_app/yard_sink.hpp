#pragma once

// Where the HUD's orders go in the Yard (docs/YARD.md): not into an engine but into a queue of intents, which the page's connection takes and sends. An intent names a tile and the visitor's own
// ants as the SERVER knows them (yard::Intent); the HUD names them by the slots of the puppet (yard_puppet.hpp), and the sink turns the slots back into the server's ids, leaves out every ant
// that is not the visitor's own, and leaves out a tile that is not on the map. It answers as the engine would, so that the voice, the status text and the order ring still happen.

#include <cstddef>
#include <vector>

#include "ants_app/yard_puppet.hpp"
#include "ants_sim/command.hpp"
#include "ants_yard/yard_types.hpp"

namespace ants::app {

class YardSink final : public sim::CommandSink {
public:
    /// Intents that wait to be taken: when more come, the oldest is dropped (a page that does not take them must not make the queue grow without a limit)
    static constexpr size_t kMaxQueued = 64;

    explicit YardSink(const YardPuppet& puppet) : puppet_(puppet) {}

    /// A group move, a group attack and a stop of the visitor's ants become intents; every other command (a special, a hatch, an alliance, a quit) is refused: the Yard has none of them
    sim::CommandResult submit(const sim::Command& command) override;

    /// The intents that wait, the oldest first
    const std::vector<yard::Intent>& queued() const noexcept { return queue_; }
    /// ... handed over: the queue is empty afterwards
    std::vector<yard::Intent> take();
    void clear() noexcept { queue_.clear(); }

private:
    const YardPuppet& puppet_;
    std::vector<yard::Intent> queue_;
};

}  // namespace ants::app

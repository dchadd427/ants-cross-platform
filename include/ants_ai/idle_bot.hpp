#pragma once

// The bot that stands still: it reads the world every decision_interval ticks and never sends a command. It exists to prove the plumbing (a controller
// whose bots only read leaves every state hash unchanged) and as the opponent of the later bots' tests.

#include "ants_ai/bot.hpp"

namespace ants::ai {

class IdleBot final : public Bot {
public:
    const char* kind() const noexcept override { return "idle"; }
    void start(const BotContext&) override {}
    void think(const BotView&, Orders&) override {}
};

}  // namespace ants::ai

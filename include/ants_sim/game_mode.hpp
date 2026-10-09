#pragma once

#include <cstdint>

namespace ants::sim {

/// The rules a match is played by, chosen with the room (the lobby's "Game type") and the same on every machine.
/// HighestScore is the original's game and every match that existed before the mode did. Kills187 ("187") is the remake's own mode (docs/GAMEPLAY.md,
/// "187"): the map as it is, without food and power-ups; the score of a team is the number of enemy ants it has killed, and the match ends when one side
/// is left with ants or eggs (the last one standing wins outright) or when the clock runs out (the most kills wins).
enum class GameMode : uint8_t {
    HighestScore = 0,
    Kills187     = 1
};

/// The highest mode number that this build knows: a wire byte, a replay field or a command line above it is refused, never played as another mode
constexpr uint8_t kLastGameMode = 1;

constexpr bool valid_game_mode(uint8_t mode) noexcept { return mode <= kLastGameMode; }

/// The words of the modes (the room's JSON, the replay list, the logs)
constexpr const char* game_mode_name(GameMode mode) noexcept {
    return mode == GameMode::Kills187 ? "187" : "highest-score";
}

} // namespace ants::sim

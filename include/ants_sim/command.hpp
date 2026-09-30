#pragma once

// Player commands: everything that a player can ask of the simulation, as plain data.
//
// The original game is not lock-step: every machine simulates only its own team's ants and broadcasts the results. The remake runs ONE
// deterministic simulation on every machine (docs/GAME_REVERSE_ENGINEERING.md, network port), so only the players' intent crosses the wire:
// a Command is what the HUD used to do by calling the engine directly (the group order FUN_010287b5 with its special and attack flags, the
// Stop button FUN_01028a60, the hatch pedestal FUN_01010aca, the alliance protocol). The engine applies a Command with
// SimulationEngine::apply_command, which validates it: the issuer is stamped by the transport (never taken from a peer's payload),
// a group order only touches ants of its issuer, tiles must be on the map, an answer needs the invitation it answers.
//
// Wire format (little endian, decode() checks every field and never reads past `size`):
//   u8 type, u8 issuer, u8 other_player, i16 tile_x, i16 tile_y, u8 ant count, u32 ant id * count      (8 + 4 * count bytes)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ants::sim {

enum class CommandType : uint8_t {
    None = 0,
    GroupMove = 1,          // FUN_010287b5(tile, special 0, attack 0)
    GroupSpecial = 2,       // FUN_010287b5(tile, special 1, attack 0)
    GroupAttack = 3,        // FUN_010287b5(tile, special 0, attack 1)
    Stop = 4,               // FUN_01028a60 for the listed ants
    Hatch = 5,              // FUN_01010aca (the hatch pedestal)
    AllianceInvite = 6,     // FUN_0100c7ac: the issuer asks `other_player` to team up
    AllianceAccept = 7,     // the issuer accepts the invitation of `other_player`
    AllianceDeny = 8,       // the issuer refuses it
    AllianceWithdraw = 9,   // FUN_0100c5fa: the issuer takes its invitation to `other_player` back
    AllianceBreak = 10,     // the issuer leaves its team
    Drop = 11,              // FUN_0100d03b: the issuer's team drops out of the match. A SYSTEM command: only the sequencer creates it (a peer
                            // that left or stopped answering); a client cannot send it (is_client_command)
    Last = Drop
};

/// The most ants one command may name (the original's drag select holds 24; a team never has more than ten eggs' worth of ants).
inline constexpr size_t kMaxCommandAnts = 32;
inline constexpr size_t kCommandHeaderBytes = 8;

struct Command {
    CommandType type{CommandType::None};
    uint8_t issuer{255};           // the player whose intent this is
    uint8_t other_player{255};     // alliance commands: the other side
    int16_t tile_x{0};             // group orders: the clicked tile
    int16_t tile_y{0};
    std::vector<uint32_t> ants;    // group orders and Stop: the selected ants in selection order

    bool operator==(const Command& o) const noexcept {
        return type == o.type && issuer == o.issuer && other_player == o.other_player && tile_x == o.tile_x && tile_y == o.tile_y && ants == o.ants;
    }
    bool operator!=(const Command& o) const noexcept { return !(*this == o); }
};

/// Why a byte string is not a command.
enum class DecodeError : uint8_t { None = 0, Truncated, UnknownType, BadCount };

inline bool is_group_order(CommandType t) noexcept {
    return t == CommandType::GroupMove || t == CommandType::GroupSpecial || t == CommandType::GroupAttack;
}
inline bool has_ant_list(CommandType t) noexcept { return is_group_order(t) || t == CommandType::Stop; }
/// The commands a player may send: everything except the system command Drop.
inline bool is_client_command(CommandType t) noexcept { return t != CommandType::None && t < CommandType::Drop; }

inline size_t encoded_size(const Command& c) noexcept { return kCommandHeaderBytes + 4 * c.ants.size(); }

/// Appends the wire form of `c` to `out`.
inline void encode(const Command& c, std::vector<uint8_t>& out) {
    out.push_back(static_cast<uint8_t>(c.type));
    out.push_back(c.issuer);
    out.push_back(c.other_player);
    out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(c.tile_x) & 0xFFu));
    out.push_back(static_cast<uint8_t>((static_cast<uint16_t>(c.tile_x) >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(c.tile_y) & 0xFFu));
    out.push_back(static_cast<uint8_t>((static_cast<uint16_t>(c.tile_y) >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(std::min<size_t>(c.ants.size(), 255u)));
    for (size_t i = 0; i < c.ants.size() && i < 255u; ++i) {
        const uint32_t id = c.ants[i];
        out.push_back(static_cast<uint8_t>(id & 0xFFu));
        out.push_back(static_cast<uint8_t>((id >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>((id >> 16) & 0xFFu));
        out.push_back(static_cast<uint8_t>((id >> 24) & 0xFFu));
    }
}

/// Reads one command from the start of `data`. Every length and count is checked before it is used: an unknown type, more ants than
/// kMaxCommandAnts, an ant list on a command that has none, an empty list on one that needs it, or a buffer that ends early are errors.
/// On success `consumed` (if given) is the number of bytes used.
inline DecodeError decode(const uint8_t* data, size_t size, Command& out, size_t* consumed = nullptr) {
    if (data == nullptr || size < kCommandHeaderBytes) return DecodeError::Truncated;
    const uint8_t type = data[0];
    if (type == static_cast<uint8_t>(CommandType::None) || type > static_cast<uint8_t>(CommandType::Last)) return DecodeError::UnknownType;
    const size_t count = data[7];
    const CommandType t = static_cast<CommandType>(type);
    if (count > kMaxCommandAnts) return DecodeError::BadCount;
    if (has_ant_list(t) ? count == 0 : count != 0) return DecodeError::BadCount;
    const size_t need = kCommandHeaderBytes + 4 * count;
    if (size < need) return DecodeError::Truncated;
    Command c;
    c.type = t;
    c.issuer = data[1];
    c.other_player = data[2];
    c.tile_x = static_cast<int16_t>(static_cast<uint16_t>(data[3] | (data[4] << 8)));
    c.tile_y = static_cast<int16_t>(static_cast<uint16_t>(data[5] | (data[6] << 8)));
    c.ants.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = data + kCommandHeaderBytes + 4 * i;
        c.ants.push_back(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                         (static_cast<uint32_t>(p[3]) << 24));
    }
    out = std::move(c);
    if (consumed != nullptr) *consumed = need;
    return DecodeError::None;
}

/// The canonical order in which the commands of one turn are applied on every machine: by issuer, and each issuer's commands keep their
/// submission order (stable), so the arrival order at the sequencer cannot change the outcome.
inline void canonical_order(std::vector<Command>& commands) {
    std::stable_sort(commands.begin(), commands.end(), [](const Command& a, const Command& b) { return a.issuer < b.issuer; });
}

/// What apply_command did.
struct CommandResult {
    enum class Status : uint8_t {
        Applied = 0,            // the command was carried out (an order may still have been refused by every ant: see ack_ant)
        Ignored,                // valid, but nothing to do (no ant of the issuer named, unknown or finished match)
        RejectedIssuer,         // the issuer is not a player of the match
        RejectedMalformed,      // a field is out of range (tile outside the map, empty or oversized list, unknown player)
        RejectedNotAllowed      // the command asks for something that its state does not allow (answering an invitation nobody made)
    };
    Status status{Status::Ignored};
    uint32_t ack_ant{0};        // group orders: the ant that acknowledges (voice, text); 0 = none
    uint32_t ants_ordered{0};   // group orders: ants of the issuer that were considered; Stop: ants that received the stop order
    uint8_t hatch_result{0};    // Hatch: SimulationEngine::HatchResult
    bool accepted() const noexcept { return status == Status::Applied; }
};

/// Where the HUD sends the player's commands. The single-player sink applies them at once; the network sink of the turn manager queues
/// them for the agreed turn. Either way the HUD asks it for the immediate feedback (voice, marker) of the order.
class CommandSink {
public:
    virtual ~CommandSink() = default;
    virtual CommandResult submit(const Command& command) = 0;
};

}  // namespace ants::sim

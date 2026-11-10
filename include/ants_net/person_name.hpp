#pragma once

// The names of people in a room that checks its names (a server run with --verified-names): the guest form, a guest name drawn from random bytes, and the rule that no two people
// of a room go by the same name. Pure functions: no clock, no state, no generator of their own (the caller brings the bytes).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

/// A guest's name: exactly "Guest " and six digits, the first not 0 ("Guest 123456"; "guest 123456", "Guest 012345", "Guest 1234567" are not).
bool is_guest_name(const std::string& name);

/// One step of drawing a guest name: the first four bytes of `bytes` as a little endian number, thrown away (false) when it falls in the uneven rest that 900,000 does not divide, else
/// "Guest " and the number 100000 .. 999999 that it stands for. Every number has the same chance: there is no lean towards any of them.
bool guest_name_from_bytes(const uint8_t* bytes, std::string& out);

/// Whether two names are the same for the room: equal when the ASCII letters are read in one case.
bool same_name(const std::string& a, const std::string& b);

/// The name that a new person of a room gets when `taken` holds the names of the people who are in it already: `name` itself when nobody has it (case ignored); else for a guest's name a new
/// guest name that nobody has, drawn from the bytes that `make_key` gives (unset or failing: the other way); else `name` + " (2)", " (3)", " (4)" ... with the base cut so that the whole
/// stays within kMaxNameChars characters, and trimmed.
std::string unique_person_name(const std::string& name, const std::vector<std::string>& taken, const std::function<bool(SeatKey&)>& make_key);

}  // namespace ants::net

#pragma once

// The names of people on the wire (protocol 29, docs/NETWORK_PORT.md "Protocol 29"). A name travels as UTF-8: printable ASCII (0x20 - 0x7E) and the
// letters of the table in name_letters_data.inc (made by tools/gen_name_letters.py: the letters of the Latin and Cyrillic alphabets that the game's font draws, Vietnamese and the modern Cyrillic ones
// among them), at most kMaxNameChars characters and kMaxNameBytes bytes, and nothing else (no control character, no combining mark, no letter of another script, no byte that is not UTF-8). The page's
// twin is web/front/name_letters.js; tests/scripts/web_name_letters_check.js and tests/test_net/test_lobby_names.cpp hold both to the same cases.
//
// A person's name is one of three kinds, and the room tells them apart by the name alone:
//   * a PASS name: the hub signed it (docs/NETWORK_PORT.md, "Protocol 29"): the nickname of a person who is logged in, with the tag before it when there is one ("[TAG] Nickname"), or the name that their login
//     has when they chose no nickname. The server takes such a name as it is when a Hello or a NameMsg shows the pass, and only then (a dedicated server that was given the hub's key);
//   * a GUEST name: everything else on such a server. "Wren (Guest)": the letters, digits, spaces and . _ - ' of the name cut to 16 characters, then " (Guest)" (guest_name). The one name without the
//     suffix is the number that a page makes for a visitor who typed none, "Guest 482913" (is_guest_number);
//   * on a LAN, a direct host and a server that has no key, a name is only checked by valid_wire_name (nothing is signed there).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ants::net {

/// A name on the wire: at most this many characters (code points) ...
inline constexpr size_t kMaxNameChars = 32;
/// ... and bytes (a letter of the table is two or three bytes)
inline constexpr size_t kMaxNameBytes = 96;
/// The room's own notices (a Chat from kRoomSender) name people, so they may hold a name's letters too: at most kMaxChatChars characters and this many bytes (a length byte holds 255)
inline constexpr size_t kMaxNoticeBytes = 240;
/// A nickname and a guest's name: at most this many characters (the tag adds 7: "[ANTS1] "; the guest's suffix adds 8)
inline constexpr size_t kMaxNicknameChars = 16;
/// The pass of a name (hub -> page -> server): "<seconds since 1970>.<32 hex digits>", printable ASCII without a blank, at most this long
inline constexpr size_t kMaxPassChars = 64;
/// What follows a guest's name
inline constexpr const char* kGuestSuffix = " (Guest)";

/// The alphabet of a letter that a name may hold: 0 Latin, 1 Cyrillic, -1 for anything else (a digit, a sign, a letter that the table does not have)
int name_alphabet(uint32_t code_point) noexcept;
/// Whether a character may stand in a name on the wire: printable ASCII or a letter of the table
bool is_name_code_point(uint32_t code_point) noexcept;

/// The code points of strict UTF-8 (no overlong form, no surrogate, nothing above U+10FFFF, no cut sequence); false for anything else, and `out` is then empty
bool decode_utf8(std::string_view text, std::vector<uint32_t>& out);
/// `code_point` as UTF-8, appended to `out`
void append_utf8(std::string& out, uint32_t code_point);
/// The number of characters of a text that decode_utf8 accepts (0 for one that it refuses)
size_t utf8_length(std::string_view text);

/// Whether `text` is a name that the wire takes (see the top): valid UTF-8, every character printable ASCII or a letter of the table, at most `max_chars` characters and kMaxNameBytes bytes. The empty
/// text is valid (a seat that holds nobody has none); the callers that need a name ask for more.
bool valid_wire_name(std::string_view text, size_t max_chars = kMaxNameChars, size_t max_bytes = kMaxNameBytes);
/// The characters of `text` that a name may hold, in order, at most `max_chars` of them and kMaxNameBytes bytes (the way a client makes its name before it sends it: an invalid byte or a character outside the
/// table is left out, and so is the rest of a UTF-8 sequence that was cut). The result is always a valid_wire_name.
std::string clip_wire_name(std::string_view text, size_t max_chars = kMaxNameChars, size_t max_bytes = kMaxNameBytes);

/// Whether `text` is the number that a page makes for a visitor who typed no name: "Guest " and six digits, the first not 0
bool is_guest_number(std::string_view text);
/// A guest's name from whatever a client said: the number of a visitor is kept as it is; else a suffix " (Guest)" is taken off (once), what is left is cut to the letters of the table, digits, spaces and
/// . _ - ' (a bracket, a parenthesis and the rest of the signs go), runs of spaces become one, the ends are trimmed, it is cut at kMaxNicknameChars characters (and trimmed again), and the suffix is put on.
/// A name with nothing left is `fallback` with the suffix.
std::string guest_name(std::string_view raw, std::string_view fallback);

/// Whether `pass` has the shape of a pass (kMaxPassChars at most, digits, a dot, 32 lower-case hex digits); the key and the clock decide whether it is a good one (the game server's name pass)
bool valid_pass_shape(std::string_view pass);

}  // namespace ants::net

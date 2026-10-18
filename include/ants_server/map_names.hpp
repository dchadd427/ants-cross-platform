#pragma once

// The rules for the text that a player sends with a map (docs/MAP_SUBMISSIONS.md, "Text"): the name of the map, the maker's display name and the note. Nothing here touches a file or the
// database: plain functions of the text, so that every rule has a test.
//
//  * The bytes must be strict UTF-8 (no overlong form, no surrogate, nothing above U+10FFFF). Spaces of other kinds become one plain space; zero-width and other invisible format characters
//    (U+200B..U+200F, U+202A..U+202E, U+2060..U+2064, U+2066..U+206F, U+00AD, U+FEFF, variation selectors, tags) are removed before anything is judged, so a name cannot hide in them.
//    A control character (anything below U+0020 but a note's line end, U+007F, U+0080..U+009F) refuses the text.
//  * Normalization form C without tables: a text may only hold characters that are their own NFC form and never start a combining sequence (see allowed_display_char), so there is no
//    second spelling of the same text. A combining mark, a compatibility form or a letter outside the allowed scripts refuses the text; it is never silently changed.
//  * A MAP NAME is more strict: the game takes the map's file name as its name in the network protocol (valid_map_name: printable ASCII), so a name is letters, digits, spaces and a few
//    marks, 3 to 60 characters, and the file is "<name>.lvl". No separator, no leading or trailing dot, no ".." and no name that a Windows machine reserves for a device.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ants::server::names {

constexpr size_t kMinNameChars = 3;
constexpr size_t kMaxNameChars = 60;           // "<name>.lvl" is at most 64 characters, the protocol's limit
constexpr size_t kMaxMakerChars = 24;
constexpr size_t kMaxNoteChars = 280;
constexpr size_t kMaxReasonChars = 200;        // the owner's decline reason or message to a maker

/// What a cleaning answers: `ok` and the cleaned text, or not ok and a stable reason code: "not_utf8", "empty", "too_short", "too_long", "characters", "control", "reserved".
struct Cleaned {
    bool ok{false};
    std::string text;
    std::string reason;
};

/// Decodes strict UTF-8; false for any malformed sequence
bool decode_utf8(std::string_view bytes, std::vector<char32_t>& out);
void append_utf8(std::string& out, char32_t c);

/// The name of a map. Cleaned.text is the name to keep (ASCII).
Cleaned clean_map_name(std::string_view raw);
/// The maker's display name (1 .. kMaxMakerChars characters, one line)
Cleaned clean_maker(std::string_view raw);
/// A note or a reason: 0 .. max_chars characters, line ends kept (at most three in a row are kept as two), `required` false lets an empty text through
Cleaned clean_note(std::string_view raw, size_t max_chars, bool required);

/// The key under which two names are the same name: the cleaned name in lower case
std::string name_key(std::string_view cleaned_name);
/// The file name of a map of that name ("<name>.lvl")
std::string file_name_of(std::string_view cleaned_name);

/// The first name "<base> N" (N from 2) that `taken` (given the key of a candidate) does not call taken; the base loses a number it already ends in ("Treasure 2" -> "Treasure 3"), and is cut
/// so that the name fits. Empty when none of N = 2 .. 999 is free.
std::string next_free_name(std::string_view cleaned_name, const std::function<bool(const std::string& key)>& taken);

}  // namespace ants::server::names

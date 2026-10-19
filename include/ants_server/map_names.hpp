#pragma once

// The rules for the text that a player sends with a map (docs/MAP_SUBMISSIONS.md, "Text"): the name of the map, the maker's display name and the note. Nothing here touches a file or the
// database: plain functions of the text, so that every rule has a test.
//
//  * The bytes must be strict UTF-8 (no overlong form, no surrogate, nothing above U+10FFFF). Spaces of other kinds become one plain space; zero-width and other invisible format characters
//    (U+200B..U+200F, U+202A..U+202E, U+2060..U+2064, U+2066..U+206F, U+00AD, U+FEFF, variation selectors, tags) are removed before anything is judged, so a name cannot hide in them.
//    A control character (anything below U+0020 but a note's line end, U+007F, U+0080..U+009F) refuses the text.
//  * Normalization form C without tables: a text may only hold characters that are their own NFC form and never start a combining sequence (see allowed_display_char), so there is no
//    second spelling of the same text. A combining mark, a compatibility form or a letter outside the allowed scripts refuses the text; it is never silently changed.
//  * The maker's name has three more rules (docs/MAP_SUBMISSIONS.md, "The text"): a letter of the Cyrillic or Greek alphabet that is drawn like a Latin one is folded to it before the name is judged, a name
//    whose folded letters spell a word that only the owner or the site may use ("owner", "admin", "system", "claude" ...) or that looks like a bot's (ai::is_reserved_bot_name) is refused with the reason
//    "reserved", and so is a word that mixes Latin letters with Cyrillic or Greek ones. A note and a maker's name may not hold a web address ("://", "www.") or three backticks (reason "link").
//  * The description inside a map file is player text too: clean_description makes the form that the database and the lists keep (the file keeps its own bytes).
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
constexpr size_t kMaxDescriptionChars = 100;   // the map's description as the database and the lists keep it

/// What a cleaning answers: `ok` and the cleaned text, or not ok and a stable reason code: "not_utf8", "empty", "too_short", "too_long", "characters", "control", "reserved", "link".
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
/// The maker's display name (1 .. kMaxMakerChars characters, one line): also refused are a web address or three backticks ("link") and a name that only the owner or the site may have, or that mixes
/// scripts in one word, or that looks like a bot's ("reserved")
Cleaned clean_maker(std::string_view raw);
/// The text with every look-alike letter of the Cyrillic and Greek alphabets as its Latin letter and every ASCII letter in lower case (the form on which a maker's name is judged); nothing else changes
std::string fold_lookalikes(std::string_view text);
/// Does the (cleaned) text hold "://", "www." (any case) or three backticks in a row: what a note and a maker's name may not have
bool has_link_markup(std::string_view cleaned_text);
/// A note or a reason: 0 .. max_chars characters, line ends kept (at most three in a row are kept as two), `required` false lets an empty text through
Cleaned clean_note(std::string_view raw, size_t max_chars, bool required);

/// The description of a map as the database and the lists keep it. `as_reported` is the text as the map core reports it (UTF-8 whose characters are the file's bytes, each U+0000 .. U+00FF): the bytes
/// are read as UTF-8 when they are valid UTF-8, else as Latin-1, and the text is then made by tidy_description. Never fails.
std::string clean_description(std::string_view as_reported);
/// The second half of clean_description, for text that is already characters: invisible and bidirectional characters and the control characters are dropped (a tab or a line end is a space), a character
/// that a note may not have becomes "?", runs of blanks become one space, the text is trimmed and cut to kMaxDescriptionChars characters. Cleaning twice gives the same text, so the readers of the
/// database use it on every row (a row from before the rule is cleaned too).
std::string tidy_description(std::string_view text);

/// The key under which two names are the same name: the cleaned name in lower case
std::string name_key(std::string_view cleaned_name);
/// The file name of a map of that name ("<name>.lvl")
std::string file_name_of(std::string_view cleaned_name);

/// The first name "<base> N" (N from 2) that `taken` (given the key of a candidate) does not call taken; the base loses a number it already ends in ("Treasure 2" -> "Treasure 3"), and is cut
/// so that the name fits. Empty when none of N = 2 .. 999 is free.
std::string next_free_name(std::string_view cleaned_name, const std::function<bool(const std::string& key)>& taken);

}  // namespace ants::server::names

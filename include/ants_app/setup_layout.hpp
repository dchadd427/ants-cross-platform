#pragma once

// The wide setup screen (widescreen work, after milestone M3): the setup / map selection screen of the original, recomposed for the 16:9 picture of 960 x 540 with a MAP PREVIEW.
//
// The original's setup screen is a 640 x 480 page: the host's screen is the animation 106 `st_screen` (Host Game Set-Up, Pick a Map, Players' Status, Fog of War, START, Leave Game), the
// screen of a guest is the animation 107 `nh_start` (WAITING FOR GAME TO START!, a fixed "Fog of War?" box). In a 960 x 540 picture the page is not a centred window any more: every piece of its
// art is put where the owner chose in the mock-ups of 2026-10-02 (option C) and the pieces that have to be bigger are made bigger from the art itself, by repeating (or dropping) only lines
// that are IDENTICAL in every piece that crosses them, so that no seam can show and nothing is scaled:
//   * the clay is the tile dclay96 laid at its own pitch of 96; the frame is the pieces dfram1 ... dfram8 (corners, dfram296 / dfram796 strips with their 16 pixel pieces dfram2 / dfram7, the
//     side strips dfram496 / dfram596 stacked as whole pieces and cut once, at a row that is identical to the piece's own neighbour);
//   * every black box is the original's own construction (the efram corners, the 1 x 4 top line efram1100, the dithered bottom edge efram4100 and the side strips efram2100 / efram3100 or
//     efram2b / efram3b) at the size the screen wants: the Players' Status box (inner 200 x 260), the Map Info box (370 x 30), the map preview (248 x 248, or 300 x 300 when there is no chat),
//     the chat box (209 x 99);
//   * the map list box w_map and the status box statline are made 70 columns wider by repeating one column inside each of three runs of identical columns; the chat input box is statline's
//     box (rows 11 .. 40, without its "status" label) made 96 columns narrower by dropping columns inside runs of identical columns.
// All numbers are the owner-approved mock-ups' own (2026-10-02, option C: "I like the chat and map preview"; the chat column restyled in the setup screen's own style: black efram box, statline's
// input box, TrueType in the labels' colours); tests/test_app/test_wide_setup.cpp compares every rectangle with them, checks every seam against the art of ants.chd and the composed pictures against
// the mock-ups pixel for pixel.
//
// THREE VARIANTS of the wide screen:
//   Single  the host's screen of a game on this machine (no network): the map preview is big (inner 300 x 300 at (349, 46)), its caption under it;
//   Online  the host's screen of a network game (a LAN host, the leader of a server's room): the preview is inner 248 x 248 at (375, 79); under it, in the column that is RESERVED for the
//           waiting-room chat, a "Chat" label, the chat box and its input box; in the Players' Status box, at the foot, the two lines of the bot-fill choice ("Empty seats at START:" /
//           "Medium bots"). This step draws nothing there unless asked to (MapSelectScreen::set_chat_panel / set_fill_footer);
//   Guest   the screen of a player who is not the host (animation 107): the same column, the same reserved chat area, no controls but Leave Game.
// The classic 640 x 480 screens are not touched by any of this: a canvas that is not 960 x 540 draws the original's page, centred, exactly as before.
//
// All rectangles are half-open (LayoutRect). The geometry (SetupLayout, the strips' spans) is plain numbers; only draw_setup_art needs a renderer (the header includes renderer.hpp for its IRenderer).

#include <cstddef>
#include <cstdint>

#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

enum class SetupVariant : uint8_t {
    Single,     // the host's screen of a local game
    Online,     // the host's screen of a network game (a LAN host, the leader of a room)
    Guest       // the screen of a player who is not the host
};

/// A run of lines of a piece of art as it is drawn along one axis: the `src_count` lines from `src` fill `dst_count` lines of the picture. Equal counts are a plain copy of that run; one
/// source line (src_count 1) with a bigger dst_count is that one line repeated; a smaller dst_count is not used (a collapsed run is a plain copy of fewer lines: the run's other lines are
/// skipped). A strip is a list of spans; the lines of the picture are the spans' dst_counts in order.
struct LineSpan {
    int16_t src;
    int16_t src_count;
    int16_t dst_count;
};

/// One strip of art drawn from line spans: the piece `sprite`, along its columns (`columns`) or rows, over the lines [cross_first, cross_first + cross_count) of the other axis
/// (cross_count 0 = all of them); `length` is the number of lines of the picture that the spans make (the sum of their dst_counts).
struct SetupStrip {
    const char* id;
    const char* sprite;
    bool columns;
    int32_t cross_first;
    int32_t cross_count;
    int32_t length;
    const LineSpan* spans;
    size_t span_count;
};

/// Every strip that the wide setup screen draws from spans (the frame's side strips, the bottom and side edges of the black boxes, the widened w_map and statline, the narrowed statline of
/// the chat input), for the tests of the seams. `count` is set to their number.
const SetupStrip* setup_strips(size_t& count) noexcept;

/// The rectangles of the chat column of the Online and Guest screens (the area that the online-rooms UI fills): the label "Chat" (TrueType, the text's top left), the chat box (the black efram
/// box, outer), its inner area where the lines go, the input box (statline's box) and where the typed text starts.
struct SetupChatLayout {
    int32_t label_x{0};
    int32_t label_y{0};
    LayoutRect box;             // outer rectangle of the black chat box
    LayoutRect lines;           // its inner area: the lines are wrapped to lines.w - 8 and end at the bottom (lines.y + lines.h - 1)
    LayoutRect input_box;       // the input box
    LayoutRect input_text;      // where the typed text is drawn (x, y of the text, w = the width of the edit field)
    constexpr bool valid() const noexcept { return box.w > 0; }
};

/// The geometry of one variant of the wide setup screen at 960 x 540. Rectangles of art are the sprite's rectangle; those of texts (`*_text`, `prompt_text`) are the label's box (the text is
/// drawn from its top left, `fit_text` / `draw_label` cut it at the width); a rectangle with w = 0 is not on that variant's screen.
struct SetupLayout {
    static constexpr int32_t kWidth = 960;
    static constexpr int32_t kHeight = 540;
    /// What the original's classic positions are moved by (docs / mock-ups): the right column and the bottom groups
    static constexpr int32_t kRightDx = 320;      // the right column: Players' Status, Fog of War, START, Leave Game (320 = 960 - 640)
    static constexpr int32_t kBottomDy = 60;      // the groups at the bottom: the map list, Map Info, the status box, Fog of War, START (60 = 540 - 480)
    static constexpr int32_t kListDx = 70;        // the map list is 70 columns wider: Up and Down move right by that
    static constexpr int32_t kBoxInnerPlayersW = 200;
    static constexpr int32_t kBoxInnerPlayersH = 260;

    /// A canvas that this screen is made for: 960 x 540 and nothing else (any other canvas draws the original's page centred)
    static constexpr bool supports(int32_t w, int32_t h) noexcept { return w == kWidth && h == kHeight; }
    static const SetupLayout& of(SetupVariant variant) noexcept;

    SetupVariant variant{SetupVariant::Single};
    LayoutRect title;               // the banner (hostbanr.bmp, nhbanr.bmp for the guest), over the top of the frame
    LayoutRect art;                 // GAME SET UP! (gamesetup.bmp), WAITING FOR GAME TO START! (waiting.bmp)
    LayoutRect map_label;           // "Pick a Map" (pickmap.bmp), "Map" (nhmap.bmp)
    LayoutRect map_box;             // the map list box, widened (w_map.bmp)
    LayoutRect up;                  // the buttons' resting pictures (up1, down1, d_on1, d_off1, start1, leave1); the hit tests are these, moved from the classic rectangles
    LayoutRect down;
    LayoutRect info_label;          // "Map Info" (mapinfo.bmp)
    LayoutRect info_box;            // the Map Info box (efram construction)
    LayoutRect status_box;          // the status box with its label (statline.bmp, widened)
    LayoutRect name_text;           // the labels: the map's name, its description, the prompt (14 px lines, wrapped)
    LayoutRect info_text;
    LayoutRect prompt_text;
    LayoutRect players_label;       // "Players' Status" (playstat.bmp)
    LayoutRect players_box;         // the black box (inner 200 x 260)
    LayoutRect fog_label;           // "Fog of War" (fowar.bmp)
    LayoutRect fog_text1;           // the two lines of small text under it (fowtext1.bmp, fowtext2.bmp)
    LayoutRect fog_text2;
    LayoutRect fog_on;              // Fog On / Fog Off (the host's screens)
    LayoutRect fog_off;
    LayoutRect start;               // START! (the host's screens)
    LayoutRect leave;               // Leave Game
    LayoutRect fog_fixed;           // the guest's fixed "No" box (fowno.bmp) and its "?" (Q_mark.bmp)
    LayoutRect fog_mark;
    /// The players' rows (four of them): the name's label (name_x, seat_y + pitch * row) w x 18, the thumb at (thumb_x, seat_y + pitch * row), the portrait's origin (portrait_x, portrait_y + pitch * row)
    int32_t seat_name_x{0};
    int32_t seat_name_w{0};
    int32_t seat_y{0};
    int32_t seat_pitch{0};
    int32_t thumb_x{0};
    int32_t portrait_x{0};
    int32_t portrait_y{0};
    /// The map preview: its black box (outer), the inner square the picture is centred in, and the caption under it (centred on caption_centre_x, top at caption_y)
    LayoutRect preview_box;
    int32_t preview_inner{0};
    int32_t caption_centre_x{0};
    int32_t caption_y{0};
    /// The chat column (Online, Guest) and the bot-fill footer in the Players' Status box (Online): the two lines' top left corners, 18 px each
    SetupChatLayout chat;
    int32_t footer_x{0};
    int32_t footer_y1{0};
    int32_t footer_y2{0};

    /// The inner square of the preview's box, and where a picture of w x h pixels sits in it (centred)
    LayoutRect preview_area() const noexcept { return LayoutRect{preview_box.x + 4, preview_box.y + 4, preview_inner, preview_inner}; }
    LayoutRect preview_picture(int32_t w, int32_t h) const noexcept {
        const LayoutRect a = preview_area();
        return LayoutRect{a.x + (a.w - w) / 2, a.y + (a.h - h) / 2, w, h};
    }
    bool has_host_controls() const noexcept { return up.w > 0; }
};

/// What the static art of the screen is drawn with: the variant, and whether the chat column's frame (the black chat box and the input box) is drawn
struct SetupArtOptions {
    SetupVariant variant{SetupVariant::Single};
    bool chat_frame{false};
};

/// Draws the screen's art that does not change with the game's state, in the order of the mock-ups: the clay and the frame, the banner, the left column's art (the title art, the map list box,
/// Map Info, the status box), the right column's (Players' Status, Fog of War and its texts, the guest's fixed box), the preview's black box and, when asked, the chat box and the input box.
/// The buttons, the labels and the portraits are not here: the screen draws them (they change).
void draw_setup_art(IRenderer& renderer, const ants::assets::AssetArchive& archive, const SetupArtOptions& options);

}  // namespace ants::app

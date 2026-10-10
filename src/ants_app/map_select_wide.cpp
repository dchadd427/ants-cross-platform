// The setup screen at 960 x 540 (setup_layout.hpp): the same screen as map_select.cpp draws on the original's 640 x 480 page, in the approved layout, with a map preview.
// Everything that decides WHAT the screen shows is the same as on the page (the selected map or the room's, the prompt, the seats and their thumbs, the buttons' pictures); only where it
// stands changes, and the preview, the chat column and the bot-fill footer are new.
#include <algorithm>
#include <filesystem>
#include <iostream>

#include "ants_app/map_select.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_assets/lvl_parser.hpp"

namespace fs = std::filesystem;

namespace ants::app {

namespace {

constexpr ButtonRect moved(const ButtonRect& r, int32_t dx, int32_t dy) noexcept { return ButtonRect{r.x + dx, r.y + dy, r.w, r.h}; }

constexpr ants::assets::ColorRGBA kLabelCream{239, 231, 223, 255};     // every label of the screen and the players' names: 0xdfe7ef
constexpr ants::assets::ColorRGBA kChatTeal{59, 151, 111, 255};        // the engraved labels' teal ("Chat" is TrueType: the original's art has no C and no h) ...
constexpr ants::assets::ColorRGBA kChatInk{7, 11, 15, 255};            // ... and their ink, one pixel down and right as the shadow

}  // namespace

// The buttons are the original's, at the places the wide layout puts them: the pictures' rectangles moved by what moves their pictures (the map list's two buttons by 70 right and 60 down, the
// right column's and the bottom groups' by 320 and 60, Leave Game by 320)
void MapSelectScreen::place_buttons() {
    const int32_t ldx = wide_ ? SetupLayout::kListDx : 0;
    const int32_t rdx = wide_ ? SetupLayout::kRightDx : 0;
    const int32_t dy = wide_ ? SetupLayout::kBottomDy : 0;
    up_ = ScreenButton(moved(ButtonRect{BTN_UP_X, BTN_UP_Y, BTN_UP_W, BTN_UP_H}, ldx, dy), moved(BTN_UP_PRESSED, ldx, dy));
    down_ = ScreenButton(moved(ButtonRect{BTN_DOWN_X, BTN_DOWN_Y, BTN_DOWN_W, BTN_DOWN_H}, ldx, dy), moved(BTN_DOWN_PRESSED, ldx, dy));
    start_ = ScreenButton(moved(ButtonRect{BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H}, rdx, dy), moved(BTN_START_PRESSED, rdx, dy));
    quit_ = ScreenButton(moved(ButtonRect{BTN_QUIT_X, BTN_QUIT_Y, BTN_QUIT_W, BTN_QUIT_H}, rdx, 0), moved(BTN_QUIT_PRESSED, rdx, 0));
    fow_on_ = ScreenButton(BTN_FOW_ON_X + rdx, BTN_FOW_ON_Y + dy, BTN_FOW_ON_W, BTN_FOW_ON_H);
    fow_off_ = ScreenButton(BTN_FOW_OFF_X + rdx, BTN_FOW_OFF_Y + dy, BTN_FOW_OFF_W, BTN_FOW_OFF_H);
}

void MapSelectScreen::set_wide_layout(bool wide) {
    if (wide_ == wide) return;
    wide_ = wide;
    place_buttons();
}

// The picture of a map for a box of inner x inner pixels: made once and kept (the last few; a map that is shown again is not drawn again); a file that cannot be read is remembered too, so
// that it is not read again every frame. The picture is the game's own drawing of the map's world, made when the map is first shown (render_map_preview_world); when the renderer cannot
// draw offscreen (or the level cannot be played by any team) the older minimap-colour picture is used instead, and the first time that happens the reason is logged (once per screen).
const MapPreview* MapSelectScreen::preview_of(const MapSelectEntry& entry, int32_t inner, IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    for (const PreviewSlot& slot : previews_) {
        if (slot.inner == inner && slot.path == entry.full_path) return slot.preview.valid() ? &slot.preview : nullptr;
    }
    PreviewSlot slot;
    slot.path = entry.full_path;
    slot.inner = inner;
    ants::assets::LevelData level;
    if (level.load_from_file(entry.full_path)) {
        std::string why;
        slot.preview = render_map_preview_world(renderer, level, inner, &why);
        if (!slot.preview.valid()) {
            if (!fallback_logged_) {
                fallback_logged_ = true;
                std::cerr << "[MapSelect] The map preview is not drawn from the game's own renderer (" << why << "): the minimap colours are shown instead." << std::endl;
            }
            slot.preview = render_map_preview_in_box(level, archive.get_palette(), inner);
        }
    }
    if (previews_.size() >= kMaxPreviews) previews_.erase(previews_.begin());
    previews_.push_back(std::move(slot));
    return previews_.back().preview.valid() ? &previews_.back().preview : nullptr;
}

int32_t MapSelectScreen::footer_width() noexcept { return SetupLayout::kBoxInnerPlayersW - 2; }

FontSize MapSelectScreen::footer_font(const IRenderer& renderer, const std::string& text) {
    for (const FontSize size : {FontSize::Px18, FontSize::Px14}) {
        if (renderer.get_text_width(text, size) <= footer_width()) return size;
    }
    return FontSize::Px12;
}

bool MapSelectScreen::prompt_fits(const IRenderer& renderer, const std::string& text) const {
    const int32_t width = wide_ ? SetupLayout::of(setup_variant()).prompt_text.w : STATUS_W;
    return wrap_label_text(renderer, text, width, FontSize::Px14).size() <= STATUS_LINES;
}

void MapSelectScreen::render_wide(IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    const SetupVariant variant = setup_variant();
    const SetupLayout& layout = SetupLayout::of(variant);
    const bool guest = is_guest();
    const bool room_choice = guest || leads_server_room();                    // the labels show the room's map, not the entry that the list has selected (the leader has the host's screen)
    const bool chat_frame = chat_panel_.visible && layout.chat.valid();

    // 1. The art: the clay, the frame, the banner, the boxes and the labels that never change (animation 106 st_screen / 107 nh_start, recomposed)
    draw_setup_art(renderer, archive, SetupArtOptions{variant, chat_frame});

    // 2. The buttons: Leave Game on both screens, the map list's Up / Down, the Fog of War pair and START! on the host's (the same animations as the page, moved with their rectangles)
    draw_animation_frame0(renderer, archive, quit_.pressed() ? "leave3" : (quit_.hovered() ? "leave2" : "leave1"), SetupLayout::kRightDx, 0);
    if (!guest) {
        draw_animation_frame0(renderer, archive, up_.pressed() ? "up3" : (up_.hovered() ? "up2" : "up1"), SetupLayout::kListDx, SetupLayout::kBottomDy);
        draw_animation_frame0(renderer, archive, down_.pressed() ? "down3" : (down_.hovered() ? "down2" : "down1"), SetupLayout::kListDx, SetupLayout::kBottomDy);
        draw_animation_frame0(renderer, archive, (fog_of_war_ || fow_on_.pressed()) ? "d_on3" : (fow_on_.hovered() ? "d_on2" : "d_on1"), SetupLayout::kRightDx, SetupLayout::kBottomDy);
        draw_animation_frame0(renderer, archive, (!fog_of_war_ || fow_off_.pressed()) ? "d_off3" : (fow_off_.hovered() ? "d_off2" : "d_off1"), SetupLayout::kRightDx,
                              SetupLayout::kBottomDy);
        draw_animation_frame0(renderer, archive, start_.pressed() ? "start3" : (start_.hovered() ? "start2" : "start1"), SetupLayout::kRightDx, SetupLayout::kBottomDy);
    }

    // The labels start empty: the refresh task's first run, 500 ms after the screen was created, fills them and puts the portraits, the thumbs and the preview on the screen
    if (!refreshed()) return;
    const MapSelectEntry* shown = nullptr;                                    // the map that the screen is about
    if (room_choice) {
        for (const auto& m : maps_) {
            if (!room_.map_file.empty() && m.filename == room_.map_file) shown = &m;
        }
    } else if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        shown = &maps_[static_cast<size_t>(selected_index_)];
    }

    // 3. The map's name and description (the guest and the leader show the room's choice: the description comes from this machine's own copy of the file, "???" when it has none)
    if (room_choice) {
        std::string shown_name = fs::path(room_.map_file).stem().string();
        std::string info_text = "???";
        if (shown != nullptr) {
            shown_name = shown->display_name;
            info_text = shown->description;
            if (shown->minutes > 0) info_text += " (" + std::to_string(shown->minutes) + " min)";
        }
        if (!shown_name.empty()) renderer.draw_text(fit_text(renderer, shown_name, layout.name_text.w, FontSize::Px18), layout.name_text.x, layout.name_text.y, kLabelCream, FontSize::Px18);
        renderer.draw_text(fit_text(renderer, info_text, layout.info_text.w, FontSize::Px18), layout.info_text.x, layout.info_text.y, kLabelCream, FontSize::Px18);
        if (guest && fog_of_war_) draw_animation_frame0(renderer, archive, "d_fowyes", SetupLayout::kRightDx, SetupLayout::kBottomDy);
    } else if (shown != nullptr) {
        renderer.draw_text(fit_text(renderer, shown->display_name, layout.name_text.w, FontSize::Px18), layout.name_text.x, layout.name_text.y, kLabelCream, FontSize::Px18);
        std::string info_text = shown->description;
        if (shown->minutes > 0) info_text += " (" + std::to_string(shown->minutes) + " min)";
        renderer.draw_text(fit_text(renderer, info_text, layout.info_text.w, FontSize::Px18), layout.info_text.x, layout.info_text.y, kLabelCream, FontSize::Px18);
    }

    // 4. The prompt
    const std::string prompt = !room_.status.empty() ? room_.status : std::string("Press START when all players' thumbs have appeared.");
    draw_label(renderer, prompt, layout.prompt_text.x, layout.prompt_text.y, layout.prompt_text.w, kLabelCream, FontSize::Px14, false);

    // 5. Players' Status: the portrait animation agst301 (12 frames, 1650 ms loop) with its origin at the row, the name and the thumb
    const auto* anim_stand = archive.find_animation("agst301");
    auto draw_portrait = [&](uint8_t team, int32_t row) {
        if (anim_stand == nullptr || anim_stand->subitems.empty()) return;
        const size_t frame = Renderer::get_anim_subitem_by_time(*anim_stand, static_cast<uint32_t>(elapsed_ms_));
        const auto& parts = anim_stand->subitems[frame].frames;
        renderer.set_hud_team(team);
        for (size_t k = parts.size(); k-- > 0;) {
            renderer.draw_sprite(parts[k].sprite_index, layout.portrait_x + parts[k].dx, layout.portrait_y + row + parts[k].dy);
        }
        renderer.set_hud_team(0);
    };
    if (room_.networked) {
        const std::array<int8_t, 4> rows = row_seats(room_);
        for (size_t r = 0; r < rows.size(); ++r) {
            if (rows[r] < 0) continue;
            const size_t seat = static_cast<size_t>(rows[r]);
            const int32_t row = static_cast<int32_t>(r) * layout.seat_pitch;
            draw_row_light(renderer, r);
            draw_portrait(static_cast<uint8_t>(seat), row);
            std::string name = room_.seats[seat].name.empty() ? std::string("Player") : room_.seats[seat].name;
            if (name.size() > 16) name.resize(16);
            renderer.draw_text(fit_text(renderer, name, layout.seat_name_w, FontSize::Px18), layout.seat_name_x, layout.seat_y + row, kLabelCream, FontSize::Px18);
            static const char* const kThumbs[4] = {"thumb1.bmp", "thumb2.bmp", "thumb3.bmp", "thumb4.bmp"};   // netgood, netok, netbad, netunk
            const size_t thumb = seat == room_.my_seat ? 0u : (static_cast<size_t>(room_.seats[seat].thumb) & 3u);
            renderer.draw_named_sprite(kThumbs[thumb], layout.thumb_x, layout.seat_y + row);
        }
    } else {
        const std::string display_user = player_name_.empty() ? "Player" : player_name_;
        draw_portrait(player_team_, 0);
        renderer.draw_text(fit_text(renderer, display_user, layout.seat_name_w, FontSize::Px18), layout.seat_name_x, layout.seat_y, kLabelCream, FontSize::Px18);
        renderer.draw_named_sprite("thumb1.bmp", layout.thumb_x, layout.seat_y);
    }

    // 6. The bot-fill footer of the leader's Players' Status box (the online rooms' choice; nothing until it is set)
    if (variant == SetupVariant::Online) {
        const int32_t ys[2] = {layout.footer_y1, layout.footer_y2};
        for (size_t line = 0; line < 2; ++line) {
            if (fill_footer_[line].empty()) continue;
            const FontSize size = footer_font(renderer, fill_footer_[line]);
            const std::string text = size == FontSize::Px12 ? fit_text(renderer, fill_footer_[line], footer_width(), size) : fill_footer_[line];       // (no line leaves the box)
            renderer.draw_text(text, layout.footer_x, ys[line], kLabelCream, size);
        }
    }

    // 7. The map preview: the picture in the black box (centred), its caption under the box; a map that this machine does not have, or cannot read, has a "No preview" box
    const MapPreview* preview = shown != nullptr ? preview_of(*shown, layout.preview_inner, renderer, archive) : nullptr;
    std::string caption;
    if (preview != nullptr) {
        const LayoutRect picture = layout.preview_picture(preview->width, preview->height);
        renderer.draw_rgba_image(picture.x, picture.y, picture.w, picture.h, preview->rgba.data());
        caption = map_preview_caption(*preview);
    } else {
        const LayoutRect area = layout.preview_area();
        const std::string none = "No preview";
        const int32_t tw = renderer.get_text_width(none, FontSize::Px18);
        const int32_t th = renderer.get_text_height(FontSize::Px18);
        renderer.draw_text(none, area.x + (area.w - tw) / 2, area.y + (area.h - th) / 2, kLabelCream, FontSize::Px18);
        if (room_choice && !room_.map_file.empty() && shown == nullptr) caption = "Map not on this computer";
        else if (shown != nullptr) caption = "Map cannot be read";
    }
    if (!caption.empty()) {
        const int32_t tw = renderer.get_text_width(caption, FontSize::Px18);
        renderer.draw_text(caption, layout.caption_centre_x - tw / 2, layout.caption_y, kLabelCream, FontSize::Px18);
    }

    // 8. The chat column (the online rooms' chat; nothing until it is turned on)
    if (chat_frame) {
        const SetupChatLayout& chat = layout.chat;
        renderer.draw_text("Chat", chat.label_x + 1, chat.label_y + 1, kChatInk, FontSize::Px20);
        renderer.draw_text("Chat", chat.label_x, chat.label_y, kChatTeal, FontSize::Px20);
        // the lines: the newest at the bottom, as many as fit; a notice is a status text (14 px), a message has the players' names' size (18 px); all in their colour
        struct Row {
            std::string text;
            FontSize size;
            int32_t height;
        };
        std::vector<Row> rows;
        for (const ChatPanel::Line& line : chat_panel_.lines) {
            const FontSize size = line.notice ? FontSize::Px14 : FontSize::Px18;
            for (const std::string& part : wrap_label_text(renderer, line.text, chat.lines.w - 8, size)) rows.push_back(Row{part, size, font_cell_height(size)});
        }
        int32_t total = 0;
        size_t first = rows.size();
        while (first > 0 && total + rows[first - 1].height <= chat.lines.h - 2) {
            --first;
            total += rows[first].height;
        }
        int32_t y = chat.lines.y + chat.lines.h - 1 - total;
        for (size_t i = first; i < rows.size(); ++i) {
            renderer.draw_text(rows[i].text, chat.lines.x + 4, y, kLabelCream, rows[i].size);
            y += rows[i].height;
        }
        draw_edit_line(renderer, chat_panel_.typed, chat.input_text.x, chat.input_text.y, chat.input_text.w, true, chat_panel_.caret, kLabelCream, FontSize::Px18, SetupLayout::kWidth);
    }
}

}  // namespace ants::app

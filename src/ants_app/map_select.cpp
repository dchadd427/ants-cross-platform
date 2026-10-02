#include "ants_app/map_select.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_app/ui_anim.hpp"
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace ants::app {

MapSelectScreen::MapSelectScreen() = default;

namespace {

// One map of the list: the name without its extension, and what the header of the file says (description, size, minutes)
MapSelectEntry make_entry(const std::string& file_name, const std::string& full_path) {
    MapSelectEntry entry;
    entry.filename = file_name;
    entry.full_path = full_path;
    entry.display_name = fs::path(file_name).stem().string();
    entry.width = 0;
    entry.height = 0;
    entry.minutes = 0;
    ants::assets::LevelData lvl;
    if (lvl.load_from_file(full_path)) {
        entry.minutes = lvl.default_minutes;
        entry.width = lvl.width();
        entry.height = lvl.height();
        entry.description = lvl.description;
    }
    return entry;
}

}  // namespace

// FUN_01013de7: FindFirstFile / FindNextFile over the Maps folder's `*.lvl`; every name is inserted into the list in front of the first entry that compares greater
// (strcmp, the byte order of the names as the file system gives them), so the list is sorted. No map is named in the program: what is in the folder is what the
// list shows, and what the header of a file says is what the labels show.
void MapSelectScreen::init(const std::string& maps_dir) {
    maps_.clear();

    std::vector<std::pair<std::string, std::string>> found;      // file name, path
    std::error_code ec;
    if (!maps_dir.empty() && fs::exists(maps_dir, ec) && fs::is_directory(maps_dir, ec)) {
        for (const auto& dir_entry : fs::directory_iterator(maps_dir, ec)) {
            if (!dir_entry.is_regular_file(ec)) continue;
            std::string ext = dir_entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".lvl") found.emplace_back(dir_entry.path().filename().string(), dir_entry.path().string());
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& f : found) maps_.push_back(make_entry(f.first, f.second));

    selected_index_ = 0;
    fog_of_war_ = false;
    enter();
}

void MapSelectScreen::enter() {
    elapsed_ms_ = 0.0;
    started_ = false;
    up_.reset();
    down_.reset();
    start_.reset();
    quit_.reset();
    fow_on_.reset();
    fow_off_.reset();
}

// FUN_010133ef shows slot i of the machine's own peer table in row i: the local machine is slot 0 on every machine, a guest's slot 1 is the host (docs 5.50)
std::array<int8_t, 4> MapSelectScreen::row_seats(const RoomView& room) noexcept {
    std::array<int8_t, 4> rows{-1, -1, -1, -1};
    if (!room.networked || room.is_host) {                                   // the host and the local screen: the seats in their places, holes stay blank
        for (size_t s = 0; s < rows.size(); ++s) rows[s] = room.seats[s].occupied ? static_cast<int8_t>(s) : static_cast<int8_t>(-1);
        return rows;
    }
    size_t next = 0;                                                         // a guest: itself first, then the others in ascending seat order, compact
    if (room.my_seat < room.seats.size() && room.seats[room.my_seat].occupied) rows[next++] = static_cast<int8_t>(room.my_seat);
    for (size_t s = 0; s < room.seats.size() && next < rows.size(); ++s) {
        if (!room.seats[s].occupied || s == room.my_seat) continue;
        rows[next++] = static_cast<int8_t>(s);
    }
    return rows;
}

void MapSelectScreen::update(float dt_seconds) {
    elapsed_ms_ += static_cast<double>(dt_seconds) * 1000.0;
}

void MapSelectScreen::set_selected_index(int32_t idx) noexcept {
    if (maps_.empty() || !can_change_setup()) return;
    const int32_t count = static_cast<int32_t>(maps_.size());
    const int32_t before = selected_index_;
    selected_index_ = ((idx % count) + count) % count;
    if (selected_index_ != before && room_.networked && on_map_changed_) {
        on_map_changed_(maps_[static_cast<size_t>(selected_index_)].filename);
    }
}

bool MapSelectScreen::follow_host_choice(const std::string& filename, bool fog) {
    fog_of_war_ = fog;
    for (size_t i = 0; i < maps_.size(); ++i) {
        if (maps_[i].filename == filename) {
            selected_index_ = static_cast<int32_t>(i);
            return true;
        }
    }
    return false;
}

void MapSelectScreen::change_fog(bool on) {
    if (!can_change_setup() || fog_of_war_ == on) return;
    fog_of_war_ = on;
    if (room_.networked && on_fog_changed_) on_fog_changed_(on);
}

const std::string& MapSelectScreen::get_selected_map_path() const {
    static const std::string empty_path = "";
    if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        return maps_[static_cast<size_t>(selected_index_)].full_path;
    }
    return empty_path;
}

void MapSelectScreen::trigger_start() {
    if (on_start_ && !maps_.empty() && selected_index_ >= 0 &&
        selected_index_ < static_cast<int32_t>(maps_.size())) {
        on_start_(maps_[static_cast<size_t>(selected_index_)].full_path);
    }
}

void MapSelectScreen::trigger_quit() {
    if (on_quit_) {
        on_quit_();
    }
}

void MapSelectScreen::handle_mouse_motion(int32_t screen_x, int32_t screen_y) {
    if (is_guest()) {                                                         // the guest screen has no Up / Down / START / Fog buttons: only Leave (FUN_01014228)
        for (ScreenButton* b : {&up_, &down_, &start_, &fow_on_, &fow_off_}) b->reset();
        quit_.on_move(screen_x, screen_y);
        return;
    }
    for (ScreenButton* b : {&up_, &down_, &start_, &quit_, &fow_on_, &fow_off_}) b->on_move(screen_x, screen_y);
}

// FUN_01013fc9: the previous / next entry of the list, wrapping round; nothing once START has run. (The map, the fog option and START belong to the host of a room.)
void MapSelectScreen::step_map(int32_t delta) {
    if (started_ || !can_change_setup()) return;
    set_selected_index(selected_index_ + delta);
}

void MapSelectScreen::start() {
    if (started_ || !has_start_button()) return;
    if (leads_server_room()) {                                                // a server's room starts when the server says so: the leader asks it
        if (on_request_start_) on_request_start_();
        return;
    }
    trigger_start();
}

// The press captures a button (FUN_01011206): its pressed animation shows and plays the sound that it carries (up3, down3, start3, leave3: Sound 0, the click;
// d_on3 and d_off3 are silent); the action comes with the release
void MapSelectScreen::handle_mouse_down(int32_t screen_x, int32_t screen_y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return;
    if (quit_.on_press(screen_x, screen_y)) play_sfx(sim::SoundID::ButtonClick);       // every player of a room can leave
    if (is_guest() || started_) return;                                       // (the leader of a server's room has the host's buttons: they act or do not act when released)
    if (up_.on_press(screen_x, screen_y)) play_sfx(sim::SoundID::ButtonClick);
    if (down_.on_press(screen_x, screen_y)) play_sfx(sim::SoundID::ButtonClick);
    fow_on_.on_press(screen_x, screen_y);
    fow_off_.on_press(screen_x, screen_y);
    if (start_.on_press(screen_x, screen_y)) play_sfx(sim::SoundID::ButtonClick);
}

// The release runs the callback of the button that is still captured
void MapSelectScreen::handle_mouse_up(int32_t screen_x, int32_t screen_y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return;
    if (is_guest()) {                                                         // the buttons that are not on the guest screen get no pointer events either
        for (ScreenButton* b : {&up_, &down_, &start_, &fow_on_, &fow_off_}) b->reset();
        if (quit_.on_release(screen_x, screen_y)) trigger_quit();
        return;
    }
    const bool fire_up = up_.on_release(screen_x, screen_y);
    const bool fire_down = down_.on_release(screen_x, screen_y);
    const bool fire_start = start_.on_release(screen_x, screen_y);
    const bool fire_quit = quit_.on_release(screen_x, screen_y);
    const bool fire_on = fow_on_.on_release(screen_x, screen_y);
    const bool fire_off = fow_off_.on_release(screen_x, screen_y);
    if (fire_up) step_map(-1);
    if (fire_down) step_map(1);
    if (fire_on && !started_) change_fog(true);
    if (fire_off && !started_) change_fog(false);
    if (fire_start) start();
    if (fire_quit) trigger_quit();
}

// FUN_01014076: the keys are Up (0xe) and Down (0xf), Enter (0x18), 'S' and 's' (start), 'Q', 'q', 'X' and 'x' (leave); nothing else does anything, Esc included
void MapSelectScreen::handle_key_down(SDL_Keycode key, bool repeat) {
    switch (key) {
        case SDLK_UP:
            step_map(-1);
            break;
        case SDLK_DOWN:
            step_map(1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_s:
            if (repeat && leads_server_room()) break;                         // (a held key is no new press for the leader's START; the original's own screens act on it)
            start();
            break;
        case SDLK_q:
        case SDLK_x:
            trigger_quit();
            break;
        default:
            break;
    }
}

void MapSelectScreen::render(IRenderer& renderer, const ants::assets::AssetArchive& archive) {
    using ants::assets::ColorRGBA;

    // 1. The setup screen's composite: the HOST screen (FUN_01013b36) is animation 106 "st_screen" (Host Game Set-Up, Pick a Map, GAME SET UP!), the GUEST screen
    // (FUN_01014228) is animation 107 "nh_start" (Game Set-Up, Map, WAITING FOR GAME TO START!, a fixed "Fog of War?" box showing No); the local screen is the host's.
    // The frames are drawn in reverse order to produce the 640x480 layout with frames, banners, headers, boxes and the Fog of War texts.
    const bool guest = is_guest();
    const bool room_choice = guest || leads_server_room();                    // the labels show the room's map, not the entry that the list has selected (the leader has the host's screen)
    const auto* anim = archive.find_animation(guest ? "nh_start" : "st_screen");
    if (anim == nullptr || anim->subitems.empty()) anim = archive.find_animation("st_screen");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            const auto& fr = frames[i];
            renderer.draw_sprite(fr.sprite_index, fr.dx, fr.dy);
        }
    } else {
        renderer.fill_rect(0, 0, 640, 480, ColorRGBA{219, 75, 19, 255});
    }

    // 2. Leave Game button (both screens): animations leave1 / leave2 (hover) / leave3 (pressed), absolute coordinates
    draw_animation_frame0(renderer, archive, quit_.pressed() ? "leave3" : (quit_.hovered() ? "leave2" : "leave1"));

    if (!guest) {
        // Up/Down stepper buttons: animations up1..3 and down1..3 (up / hover / pressed), absolute coordinates
        draw_animation_frame0(renderer, archive, up_.pressed() ? "up3" : (up_.hovered() ? "up2" : "up1"));
        draw_animation_frame0(renderer, archive, down_.pressed() ? "down3" : (down_.hovered() ? "down2" : "down1"));

        // 3. Fog of War On / Off (animations d_on1..3, d_off1..3): the chosen one is shown down (so is a button that is being pressed), the other one up or hover
        draw_animation_frame0(renderer, archive, (fog_of_war_ || fow_on_.pressed()) ? "d_on3" : (fow_on_.hovered() ? "d_on2" : "d_on1"));
        draw_animation_frame0(renderer, archive, (!fog_of_war_ || fow_off_.pressed()) ? "d_off3" : (fow_off_.hovered() ? "d_off2" : "d_off1"));

        // 4. START! button: animations start1 / start2 (hover) / start3 (pressed), absolute coordinates
        draw_animation_frame0(renderer, archive, start_.pressed() ? "start3" : (start_.hovered() ? "start2" : "start1"));
    }

    // The labels start empty: the refresh task's first run, 500 ms after the screen was created, fills them and puts the portraits and thumbs on the screen
    if (!refreshed()) return;
    const ColorRGBA label_colour{239, 231, 223, 255};                 // every label: 0xdfe7ef

    // 5. The map name (36, 312) 179 x 26 and its description (36, 380) 293 x 26, 18 px lines. A guest shows the HOST's choice (game message 0x22: the file name; the
    // description comes from the guest's OWN copy of the file, "???" when it has none, and before the host's first message the name is empty).
    if (room_choice) {
        const MapSelectEntry* own = nullptr;
        for (const auto& m : maps_) {
            if (!room_.map_file.empty() && m.filename == room_.map_file) own = &m;
        }
        std::string shown_name = fs::path(room_.map_file).stem().string();
        std::string info_text = "???";
        if (own != nullptr) {
            shown_name = own->display_name;
            info_text = own->description;
            if (own->minutes > 0) info_text += " (" + std::to_string(own->minutes) + " min)";
        }
        if (!shown_name.empty()) renderer.draw_text(fit_text(renderer, shown_name, NAME_W, FontSize::Px18), LABEL_X, NAME_Y, label_colour, FontSize::Px18);
        renderer.draw_text(fit_text(renderer, info_text, STATUS_W, FontSize::Px18), LABEL_X, INFO_Y, label_colour, FontSize::Px18);
        if (guest && fog_of_war_) draw_animation_frame0(renderer, archive, "d_fowyes");        // the "Yes" over the fixed "No" box (animation 108, (534, 366)); the leader's buttons show it
    } else if (selected_index_ >= 0 && selected_index_ < static_cast<int32_t>(maps_.size())) {
        const auto& cur = maps_[static_cast<size_t>(selected_index_)];
        renderer.draw_text(fit_text(renderer, cur.display_name, NAME_W, FontSize::Px18), LABEL_X, NAME_Y, label_colour, FontSize::Px18);
        std::string info_text = cur.description;
        if (cur.minutes > 0) info_text += " (" + std::to_string(cur.minutes) + " min)";
        renderer.draw_text(fit_text(renderer, info_text, STATUS_W, FontSize::Px18), LABEL_X, INFO_Y, label_colour, FontSize::Px18);
    }

    // 6. The prompt: the label (36, 447) 293 x 35 with 14 px lines, wrapped at its width
    const std::string prompt = !room_.status.empty() ? room_.status : std::string("Press START when all players' thumbs have appeared.");
    draw_label(renderer, prompt, LABEL_X, STATUS_Y, STATUS_W, label_colour, FontSize::Px14, false);

    // 7. Players' Status, slot 0: the portrait animation agst301 (12 frames, 1650 ms loop, running since the screen was created) has its origin at (395,115) and
    // the thumbs-up sprite sits at (540,95); the sprite's own part offsets place the ant relative to that origin.
    const auto* anim_stand = archive.find_animation("agst301");
    auto draw_portrait = [&](uint8_t team, int32_t row) {
        if (anim_stand == nullptr || anim_stand->subitems.empty()) return;
        const size_t frame = Renderer::get_anim_subitem_by_time(*anim_stand, static_cast<uint32_t>(elapsed_ms_));
        const auto& parts = anim_stand->subitems[frame].frames;
        renderer.set_hud_team(team);
        for (size_t k = parts.size(); k-- > 0;) {
            renderer.draw_sprite(parts[k].sprite_index, PLAYER_PORTRAIT_X + parts[k].dx, PLAYER_PORTRAIT_Y + row + parts[k].dy);
        }
        renderer.set_hud_team(0);
    };
    if (room_.networked) {
        // a room: the rows of FUN_010133ef (row_seats: the local player first on a guest), each with the portrait in the seat's colour (the colour follows the
        // player, not the row), the name and the thumb of a player who is here; the local machine's own thumb is always the good one (latency 0, ready from the start)
        const std::array<int8_t, 4> rows = row_seats(room_);
        for (size_t r = 0; r < rows.size(); ++r) {
            if (rows[r] < 0) continue;
            const size_t seat = static_cast<size_t>(rows[r]);
            const int32_t row = static_cast<int32_t>(r) * PLAYER_ROW_PITCH;
            draw_portrait(static_cast<uint8_t>(seat), row);
            std::string name = room_.seats[seat].name.empty() ? std::string("Player") : room_.seats[seat].name;
            if (name.size() > 16) name.resize(16);
            renderer.draw_text(fit_text(renderer, name, PLAYER_NAME_W, FontSize::Px18), PLAYER_NAME_X, PLAYER_THUMB_Y + row, label_colour, FontSize::Px18);      // the label (415, 95 + 50 * row) 120 x 20
            static const char* const kThumbs[4] = {"thumb1.bmp", "thumb2.bmp", "thumb3.bmp", "thumb4.bmp"};   // netgood, netok, netbad, netunk
            const size_t thumb = seat == room_.my_seat ? 0u : (static_cast<size_t>(room_.seats[seat].thumb) & 3u);
            renderer.draw_named_sprite(kThumbs[thumb], PLAYER_THUMB_X, PLAYER_THUMB_Y + row);
        }
    } else {
        const std::string display_user = player_name_.empty() ? "Player" : player_name_;
        draw_portrait(player_team_, 0);
        renderer.draw_text(fit_text(renderer, display_user, PLAYER_NAME_W, FontSize::Px18), PLAYER_NAME_X, PLAYER_THUMB_Y, label_colour, FontSize::Px18);
        renderer.draw_named_sprite("thumb1.bmp", PLAYER_THUMB_X, PLAYER_THUMB_Y);
    }
}

} // namespace ants::app

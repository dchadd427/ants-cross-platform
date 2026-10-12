// HUD layout, part 2: the full-screen screens (setup, room, leader, results), the held keys, the button zones, the text sizes and the label wrap.
// (See test_hud_layout.cpp for what the whole program checks.)
#include "hud_layout_fixtures.hpp"

void test_screens(const assets::AssetArchive& arc) {
    std::printf("[hud] options, quit, quick help and start screens\n");
    // Options: op_screen composite only (its own dither), no extra dither grid or card fill
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_options();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        size_t dither200 = 0;
        for (const auto& s : rr.sprites) if (s.name == "dith200.bmp") ++dither200;
        const auto* op = arc.find_animation("op_screen");
        size_t expect = 0;
        if (op) for (const auto& part : op->subitems[0].frames) if (arc.get_sprite(part.sprite_index).name == "dith200.bmp") ++expect;
        check(dither200 == expect, "options: only the composite's own dith200 tiles (" + std::to_string(dither200) + " of " + std::to_string(expect) + ")");
        bool card_fill = false;
        for (const auto& f : rr.fills) if (f.x == 18 && f.y == 20 && f.w == 442 && f.h == 440) card_fill = true;
        check(!card_fill, "options: no opaque card fill");
    }
    // Quick help: qh_screen composite (qh2 at (267,10), qh1 at (10,9)) + qh_return1, no dim fill
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_quick_help();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("qh2.bmp", 267, 10), "quick help: qh2 at (267,10)");
        check(rr.has_sprite_at("qh1.bmp", 10, 9), "quick help: qh1 at (10,9)");
        check(rr.has_sprite_at("breturn1.bmp", 529, 437), "quick help: return button at (529,437)");
        bool dim = false;
        for (const auto& f : rr.fills) if (f.w == 640 && f.h == 480) dim = true;
        check(!dim, "quick help: no full-screen dim");
    }
    // Quit dialog: origin (100,100), yes (180,260), no (292,260), no dim
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_quit_dialog();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("yes1.bmp", 180, 260), "quit: yes at (180,260)");
        check(rr.has_sprite_at("no1.bmp", 292, 260), "quit: no at (292,260)");
        bool dim = false;
        for (const auto& f : rr.fills) if (f.w == 640 && f.h == 480) dim = true;
        check(!dim, "quit: no full-screen dim");
        // the std_dialg frame starts at the origin: dfram1 part (0,0) => (100,100)
        check(rr.has_sprite_at("dfram1.bmp", 100, 100), "quit: dialog frame at origin (100,100)");
    }
    // Match start modal: origin (100,100), label in (130,110,240,160), portrait anchored at (245,250)
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.start_match_modal();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("dfram1.bmp", 100, 100), "modal: dialog frame at origin (100,100)");
        bool portrait = false;
        const auto* ant = arc.find_animation("agst301");
        if (ant) for (const auto& part : ant->subitems[0].frames)
            if (rr.has_sprite_at("agst301.bmp", 245 + part.dx, 250 + part.dy)) portrait = true;
        check(portrait, "modal: worker portrait anchored at (245,250)");
    }
}

namespace {

MapSelectScreen::RoomView view_of_host_for_art() {
    MapSelectScreen::RoomView v;
    v.networked = true;
    v.is_host = true;
    v.my_seat = 0;
    v.seats[0] = {true, "Host", MapSelectScreen::Thumb::Good};
    return v;
}

}  // namespace

// The setup screen as a network room: a row per occupied seat (portrait, name, the thumb of the connection quality), the status line, and the
// host-only controls (the map, the fog option and START belong to the host; every player can leave).
void test_room_screen(const assets::AssetArchive& arc) {
    std::printf("[room] names with a thumb per seat (netgood / netok / netbad / netunk), the status line, host-only controls\n");
    using MS = MapSelectScreen;
    MS screen;
    screen.init();
    screen.update(0.5f);                                                            // the refresh has run: labels, portraits and thumbs are shown
    MS::RoomView view;
    view.networked = true;
    view.is_host = true;
    view.my_seat = 0;
    view.seats[0] = {true, "Alice", MS::Thumb::Good};
    view.seats[1] = {true, "Bob", MS::Thumb::Ok};
    view.seats[3] = {true, "A Very Long Player Name", MS::Thumb::Unknown};      // seat 2 is empty
    view.status = "Press START when all players' thumbs have appeared.";
    screen.set_room(view);
    RecordingRenderer rr(arc);
    screen.render(rr, arc);
    check(rr.has_sprite_at("thumb1.bmp", 540, 95), "room: seat 0 shows the good thumb at (540,95)");
    check(rr.has_sprite_at("thumb2.bmp", 540, 145), "room: seat 1 shows the ok thumb at (540,145)");
    bool row2 = false;
    for (const auto& sp : rr.sprites) row2 = row2 || (sp.x == 540 && sp.y == 195 && sp.name.rfind("thumb", 0) == 0);
    check(!row2, "room: an empty seat shows no thumb");
    check(rr.has_sprite_at("thumb4.bmp", 540, 245), "room: seat 3 shows the question mark at (540,245)");
    auto text_at = [&](const std::string& t, int32_t x) {
        for (const auto& tx : rr.texts) {
            if (tx.text == t && tx.x == x) return true;
        }
        return false;
    };
    check(text_at("Alice", 415) && text_at("Bob", 415), "room: the names are drawn at x = 415");
    check(text_at("A Very Long Pla", 415) || text_at("A Very Long Play", 415), "room: a long name is cut to fit before the thumb");
    {
        // the prompt is the label (36, 447) 293 wide with 14 px lines: it wraps like every label (the recorder measures 6 px per character)
        const auto lines = wrap_label_text(rr, "Press START when all players' thumbs have appeared.", MS::STATUS_W, FontSize::Px14);
        bool ok = lines.size() == 2;
        for (size_t i = 0; i < lines.size(); ++i) {
            bool found = false;
            for (const auto& tx : rr.texts) found = found || (tx.text == lines[i] && tx.x == 36 && tx.y == 447 + static_cast<int32_t>(i) * 14 && tx.size == FontSize::Px14);
            ok = ok && found;
        }
        check(ok, "room: the status line is the original's prompt, wrapped in its label at (36, 447)");
    }
    int portraits = 0;
    for (const auto& sp : rr.sprites) portraits += sp.name.rfind("agst30", 0) == 0 ? 1 : 0;
    check(portraits >= 3, "room: a portrait for each occupied seat");
    // every quality has its own thumb
    for (const auto& [quality, name] : {std::pair{MS::Thumb::Good, "thumb1.bmp"}, std::pair{MS::Thumb::Ok, "thumb2.bmp"},
                                         std::pair{MS::Thumb::Bad, "thumb3.bmp"}, std::pair{MS::Thumb::Unknown, "thumb4.bmp"}}) {
        MS::RoomView v = view;
        v.seats[1].thumb = quality;
        screen.set_room(v);
        RecordingRenderer r(arc);
        screen.render(r, arc);
        check(r.has_sprite_at(name, 540, 145), std::string("room: quality ") + name + " at (540,145)");
    }

    // host controls: the map and the fog option go to the room, START starts, LEAVE quits
    screen.set_room(view);
    std::vector<std::string> maps_chosen;
    std::vector<bool> fog_chosen;
    int started = 0;
    int left = 0;
    screen.set_on_map_changed([&](const std::string& f) { maps_chosen.push_back(f); });
    screen.set_on_fog_changed([&](bool on) { fog_chosen.push_back(on); });
    screen.set_on_start([&](const std::string&) { ++started; });
    screen.set_on_quit([&]() { ++left; });
    const int32_t before = screen.get_selected_index();
    click(screen, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
    check(screen.get_selected_index() != before && maps_chosen.size() == 1, "room host: the down button changes the map and tells the room");
    check(maps_chosen[0] == screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename, "room host: the room gets the file name of the map");
    click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);
    click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);                         // the same choice again changes nothing
    check(fog_chosen.size() == 1 && fog_chosen[0], "room host: the fog option is reported once per change");
    click(screen, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
    check(started == 1, "room host: START starts");
    click(screen, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
    check(left == 1, "room host: LEAVE quits");

    // a guest cannot change anything but can leave; it follows the host without any callback
    MS::RoomView guest = view;
    guest.is_host = false;
    guest.my_seat = 1;
    screen.set_room(guest);
    maps_chosen.clear();
    fog_chosen.clear();
    started = 0;
    left = 0;
    const int32_t idx = screen.get_selected_index();
    const bool fog_before = screen.is_fog_of_war_enabled();
    click(screen, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
    click(screen, MS::BTN_FOW_OFF_X + 2, MS::BTN_FOW_OFF_Y + 2);
    click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);
    click(screen, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
    screen.handle_key_down(SDLK_DOWN);
    screen.handle_key_down(SDLK_s);
    screen.handle_key_down(SDLK_RETURN);
    check(screen.get_selected_index() == idx && screen.is_fog_of_war_enabled() == fog_before, "room guest: the map and the fog option cannot be changed");
    check(started == 0 && maps_chosen.empty() && fog_chosen.empty(), "room guest: START does nothing and nothing is reported");
    click(screen, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
    check(left == 1, "room guest: LEAVE quits");
    screen.handle_key_down(SDLK_ESCAPE);
    check(left == 1, "room guest: Esc does nothing");
    screen.handle_key_down(SDLK_q);
    screen.handle_key_down(SDLK_x);
    check(left == 3, "room guest: Q and X leave");
    check(screen.follow_host_choice("SMALL.LVL", true) && screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename == "SMALL.LVL" &&
              screen.is_fog_of_war_enabled() && maps_chosen.empty() && fog_chosen.empty(),
          "room guest: follows the host's map and fog without telling anybody");
    check(!screen.follow_host_choice("NOSUCH.LVL", false), "room guest: an unknown map name is refused");

    // not networked: the local screen keeps its single row and its placeholders
    MS local;
    local.init();
    local.update(0.5f);
    RecordingRenderer lr(arc);
    local.render(lr, arc);
    check(lr.has_sprite_at("thumb1.bmp", 540, 95) && lr.named("thumb2.bmp").empty(), "local setup screen: one row with the good thumb");
    int local_maps = 0;
    local.set_on_map_changed([&](const std::string&) { ++local_maps; });
    click(local, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
    check(local_maps == 0, "local setup screen: no room to tell about a map change");

    // ---- the rows (FUN_010133ef: row i is slot i of the machine's own table, the local machine is slot 0) ----------------------------------------------------------------------------------
    {
        auto view_of = [](bool host, uint8_t my, std::initializer_list<uint8_t> occupied) {
            MS::RoomView v;
            v.networked = true;
            v.is_host = host;
            v.my_seat = my;
            for (uint8_t s : occupied) v.seats[s] = {true, std::string("P") + std::to_string(s), MS::Thumb::Ok};
            return v;
        };
        using Rows = std::array<int8_t, 4>;
        check(MS::row_seats(view_of(true, 0, {0, 1, 3})) == Rows{0, 1, -1, 3}, "rows: a host shows the seats in their places, a seat that is not taken leaves its row blank");
        check(MS::row_seats(view_of(false, 1, {0, 1, 3})) == Rows{1, 0, 3, -1}, "rows: a guest (seat 1) shows itself first, then the host (seat 0), then the others, compact");
        check(MS::row_seats(view_of(false, 3, {0, 1, 2, 3})) == Rows{3, 0, 1, 2}, "rows: a guest in seat 3 of four: itself, then the seats 0, 1, 2");
        check(MS::row_seats(view_of(false, 2, {0, 2})) == Rows{2, 0, -1, -1}, "rows: a guest and the host: [me, host]");
        check(MS::row_seats(view_of(false, 255, {0, 1, 3})) == Rows{0, 1, 3, -1}, "rows: a guest without a seat shows the occupied seats in ascending order");
        check(MS::row_seats(view_of(false, 2, {0, 1})) == Rows{0, 1, -1, -1}, "rows: a guest whose own seat is not in the room (yet) shows the others");
        {
            MS::RoomView solo;                                                       // the local screen is not a room: the seats stay where they are
            solo.seats[0] = {true, "Me", MS::Thumb::Good};
            check(MS::row_seats(solo) == Rows{0, -1, -1, -1}, "rows: the local screen shows its one row");
        }
    }

    // ---- the GUEST screen (FUN_01014228): another composite, no Up / Down / START / Fog buttons, the fixed "Fog of War?" box, only Leave --------------------------------------------------
    {
        MS g;
        g.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = false;
        v.my_seat = 2;
        const char* const names[4] = {"Alice", "Bob", "Cara", "Dave"};
        for (size_t i = 0; i < 4; ++i) v.seats[i] = {true, names[i], MS::Thumb::Ok};
        v.seats[2].thumb = MS::Thumb::Bad;                                          // the local row is always the good thumb
        v.map_file = "SMALL.LVL";
        g.set_room(v);
        RecordingRenderer early(arc);
        g.render(early, arc);                                                       // before the first refresh: the art and Leave only
        check(early.has_sprite_at("nhbanr.bmp", 140, 0) && early.has_sprite_at("bleave1.bmp", 525, 12), "guest: before the refresh the guest art and Leave show");
        check(early.texts.empty() && early.named("fowyes.bmp").empty(), "guest: no label and no fog overlay before the first refresh");
        g.update(0.5f);
        RecordingRenderer gr(arc);
        g.render(gr, arc);
        check(gr.has_sprite_at("nhbanr.bmp", 140, 0) && gr.has_sprite_at("waiting.bmp", 20, 39) && gr.has_sprite_at("nhmap.bmp", 25, 278), "guest: the banner 'Game Set-Up', the 'WAITING FOR GAME TO START!' art and the 'Map' caption");
        check(gr.has_sprite_at("fowno.bmp", 529, 361) && gr.has_sprite_at("Q_mark.bmp", 511, 372), "guest: the fixed 'Fog of War?' box shows No");
        check(gr.has_sprite_at("bleave1.bmp", 525, 12), "guest: Leave is the one button");
        bool host_art = false;
        for (const char* n : {"hostbanr.bmp", "gamesetup.bmp", "pickmap.bmp", "up1.bmp", "down1.bmp", "bstart1.bmp", "dbutonu.bmp", "dbutoffu.bmp", "optoffd.bmp", "optond.bmp"}) host_art = host_art || !gr.named(n).empty();
        check(!host_art, "guest: none of the host's art or buttons (banner, 'GAME SET UP!', 'Pick a Map', Up, Down, START, Fog On / Off)");
        check(gr.named("fowyes.bmp").empty(), "guest: fog off = no 'Yes' over the box");
        // the rows: the guest itself first (the colour follows the player: seat 2), then the others in ascending seat order
        auto portrait_team = [&](int32_t row_y) -> int {
            for (const auto& sp : gr.sprites) {
                if (sp.name.rfind("agst30", 0) == 0 && sp.x >= 383 && sp.x <= 385 && sp.y >= 85 + row_y && sp.y <= 89 + row_y) return sp.team;      // the part offsets move with the frame
            }
            return -1;
        };
        check(portrait_team(0) == 2 && portrait_team(50) == 0 && portrait_team(100) == 1 && portrait_team(150) == 3, "guest: the portraits of the rows are seats 2, 0, 1, 3 (each in its own colour)");
        auto text_row = [&](const char* t, int32_t y) {
            for (const auto& tx : gr.texts) {
                if (tx.text == t && tx.x == 415 && tx.y == y) return true;
            }
            return false;
        };
        check(text_row("Cara", 95) && text_row("Alice", 145) && text_row("Bob", 195) && text_row("Dave", 245), "guest: the names are Cara, Alice, Bob, Dave from the top (its own name first)");
        check(gr.has_sprite_at("thumb1.bmp", 540, 95) && gr.has_sprite_at("thumb2.bmp", 540, 145) && gr.has_sprite_at("thumb2.bmp", 540, 195), "guest: the local row has the good thumb whatever its measured quality, the others their own");
        // the map labels: the host's choice, the description from the guest's own copy of the file
        bool name_label = false, info_label = false;
        for (const auto& tx : gr.texts) {
            if (tx.text == "SMALL" && tx.x == 36 && tx.y == 312) name_label = true;
            if (tx.x == 36 && tx.y == 380 && tx.text != "???") info_label = true;
        }
        check(name_label && info_label, "guest: the map name without its extension at (36, 312) and the description of its own copy at (36, 380)");
        // keys and mouse: Leave only
        int guest_left = 0;
        int guest_started = 0;
        g.set_on_quit([&]() { ++guest_left; });
        g.set_on_start([&](const std::string&) { ++guest_started; });
        g.handle_mouse_motion(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3);
        RecordingRenderer hov(arc);
        g.render(hov, arc);
        check(hov.named("up2.bmp").empty() && hov.named("up1.bmp").empty(), "guest: nothing to hover where the host's Up button is");
        click(g, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        check(!g.start_button().hovered() && !g.start_button().pressed() && !g.up_button().hovered() && !g.down_button().hovered(), "guest: the host's buttons have no pointer state after a click on their places");
        g.handle_key_down(SDLK_RETURN);
        g.handle_key_down(SDLK_s);
        g.handle_key_down(SDLK_UP);
        g.handle_key_down(SDLK_DOWN);
        g.handle_key_down(SDLK_ESCAPE);
        check(guest_started == 0 && guest_left == 0, "guest: START, Enter, S, the arrows and Esc do nothing");
        g.handle_key_down(SDLK_q);
        g.handle_key_down(SDLK_x);
        click(g, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        check(guest_left == 3, "guest: Q, X and the Leave button leave");
        // fog on: the 'Yes' overlay over the box; a map the guest does not have: the host's name and '???'
        check(g.follow_host_choice("SMALL.LVL", true), "guest: follows the host's fog choice");
        RecordingRenderer fog(arc);
        g.render(fog, arc);
        check(fog.has_sprite_at("fowyes.bmp", 534, 366), "guest: fog on shows 'Yes' at (534, 366)");
        {
            MS fresh;
            fresh.init();
            fresh.set_room(v);
            fresh.follow_host_choice("SMALL.LVL", true);
            RecordingRenderer before_refresh(arc);
            fresh.render(before_refresh, arc);                                              // no update yet: the 'Yes' overlay is put on by the first refresh, 500 ms after the screen
            check(before_refresh.named("fowyes.bmp").empty(), "guest: fog on, but the 'Yes' overlay waits for the first refresh");
        }
        v.map_file = "NOSUCH.LVL";
        g.set_room(v);
        RecordingRenderer missing(arc);
        g.render(missing, arc);
        bool shown_name = false, unknown = false;
        for (const auto& tx : missing.texts) {
            if (tx.text == "NOSUCH" && tx.x == 36 && tx.y == 312) shown_name = true;
            if (tx.text == "???" && tx.x == 36 && tx.y == 380) unknown = true;
        }
        check(shown_name && unknown, "guest: a map that the guest does not have: the host's name and the question marks for the description");
        v.map_file.clear();
        g.set_room(v);
        RecordingRenderer none(arc);
        g.render(none, arc);
        bool any_name = false, still_unknown = false;
        for (const auto& tx : none.texts) {
            if (tx.x == 36 && tx.y == 312) any_name = true;
            if (tx.text == "???" && tx.x == 36 && tx.y == 380) still_unknown = true;
        }
        check(!any_name && still_unknown, "guest: before the host's first message the name is empty and the description is the question marks");

        // the host's screen and the local screen keep the host art
        MS h;
        h.init();
        h.update(0.5f);
        MS::RoomView hv = view_of_host_for_art();
        h.set_room(hv);
        RecordingRenderer hr(arc);
        h.render(hr, arc);
        check(hr.has_sprite_at("hostbanr.bmp", 140, 0) && hr.has_sprite_at("bstart1.bmp", 526, 439) && hr.named("nhbanr.bmp").empty() && hr.named("fowyes.bmp").empty(), "host: the host screen keeps its art and its controls");
    }
}

// The LEADER's screen: the first player of a dedicated server's room (protocol 7) has the HOST's composite and buttons (st_screen) in a "server room" mode: the room's map and fog
// are shown and cannot be changed, START asks the server to start, Leave works; the rows are the guest's (itself first)
void test_leader_screen(const assets::AssetArchive& arc) {
    std::printf("[leader] the first player of a server's room: the host's screen with START, the room's setup shown and not changeable, the guest's rows\n");
    using MS = MapSelectScreen;
    {
        MS l;
        l.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = false;
        v.leader = true;
        v.my_seat = 2;
        const char* const names[4] = {"Alice", "Bob", "Cara", "Dave"};
        for (size_t i = 0; i < 4; ++i) v.seats[i] = {true, names[i], MS::Thumb::Ok};
        v.seats[2].thumb = MS::Thumb::Bad;                                          // the local row is always the good thumb
        v.map_file = "SMALL.LVL";
        v.status = "Press START when all players' thumbs have appeared.";
        l.set_room(v);
        check(!l.is_guest() && l.leads_server_room() && l.has_start_button() && !l.can_change_setup(), "leader: not a guest, leads a server's room, has START, cannot change the setup");
        {   // the same questions for every kind of screen
            MS local;
            check(!local.is_guest() && !local.leads_server_room() && local.has_start_button() && local.can_change_setup(), "local screen: START, the setup is its own");
            MS host;
            MS::RoomView hv = view_of_host_for_art();
            host.set_room(hv);
            check(!host.is_guest() && !host.leads_server_room() && host.has_start_button() && host.can_change_setup(), "host screen: START, the setup is its own");
            MS guest;
            MS::RoomView gv = v;
            gv.leader = false;
            guest.set_room(gv);
            check(guest.is_guest() && !guest.leads_server_room() && !guest.has_start_button() && !guest.can_change_setup(), "guest screen: no START, no setup");
            MS::RoomView both = v;
            both.is_host = true;                                                    // (a host that is also marked leader is a host: the leader flag is for guests of a server's room)
            guest.set_room(both);
            check(!guest.is_guest() && !guest.leads_server_room() && guest.has_start_button() && guest.can_change_setup(), "a host with the leader flag is still a host");
        }
        l.follow_host_choice("SMALL.LVL", true);                                    // what the application does for a leader, as it does for a guest
        RecordingRenderer early(arc);
        l.render(early, arc);                                                       // before the first refresh: the host's art and buttons, no label
        check(early.has_sprite_at("hostbanr.bmp", 140, 0) && early.has_sprite_at("bstart1.bmp", 526, 439) && early.has_sprite_at("bleave1.bmp", 525, 12), "leader: before the refresh the host's art and its buttons show");
        check(early.texts.empty() && early.named("thumb1.bmp").empty(), "leader: no label, no portrait and no thumb before the first refresh");
        l.update(0.5f);
        RecordingRenderer lr(arc);
        l.render(lr, arc);
        check(lr.has_sprite_at("hostbanr.bmp", 140, 0) && lr.has_sprite_at("bstart1.bmp", 526, 439) && lr.has_sprite_at("up1.bmp", 226, 299) && lr.has_sprite_at("down1.bmp", 226, 323) &&
                  lr.has_sprite_at("bleave1.bmp", 525, 12),
              "leader: the host's screen: banner, START, Up, Down and Leave");
        bool guest_art = false;
        for (const char* n : {"nhbanr.bmp", "waiting.bmp", "nhmap.bmp", "fowno.bmp", "Q_mark.bmp", "fowyes.bmp"}) guest_art = guest_art || !lr.named(n).empty();
        check(!guest_art, "leader: none of the guest's art (banner, 'WAITING FOR GAME TO START!', the fixed Fog box)");
        check(lr.has_sprite_at("optond.bmp", 522, 372) && lr.has_sprite_at("dbutoffu.bmp", 576, 373), "leader: the Fog buttons show the room's choice (on): ON down, OFF up");
        // the rows are the guest's: itself first (seat 2, the good thumb), then the others in ascending order
        {
            auto text_row = [&](const char* t, int32_t y) {
                for (const auto& tx : lr.texts) {
                    if (tx.text == t && tx.x == 415 && tx.y == y) return true;
                }
                return false;
            };
            check(text_row("Cara", 95) && text_row("Alice", 145) && text_row("Bob", 195) && text_row("Dave", 245), "leader: the names are Cara, Alice, Bob, Dave from the top (its own name first)");
            check(lr.has_sprite_at("thumb1.bmp", 540, 95) && lr.has_sprite_at("thumb2.bmp", 540, 145), "leader: the local row has the good thumb, the others their own");
            check(MS::row_seats(v) == std::array<int8_t, 4>{2, 0, 1, 3}, "leader: row_seats = itself first, then the others");
        }
        // the labels: the room's map (the file name of the room, the description from the leader's own copy), '???' when this machine has no such file
        {
            bool name_label = false, info_label = false, prompt = false;
            for (const auto& tx : lr.texts) {
                if (tx.text == "SMALL" && tx.x == 36 && tx.y == 312) name_label = true;
                if (tx.x == 36 && tx.y == 380 && tx.text != "???") info_label = true;
                if (tx.x == 36 && tx.y == 447 && tx.size == FontSize::Px14) prompt = true;
            }
            check(name_label && info_label, "leader: the room's map name at (36, 312) and the description of its own copy at (36, 380)");
            check(prompt, "leader: the prompt (the original's) at (36, 447)");
            MS::RoomView missing = v;
            missing.map_file = "NOSUCH.LVL";
            l.set_room(missing);
            RecordingRenderer mr(arc);
            l.render(mr, arc);
            bool shown_name = false, unknown = false;
            for (const auto& tx : mr.texts) {
                if (tx.text == "NOSUCH" && tx.x == 36 && tx.y == 312) shown_name = true;
                if (tx.text == "???" && tx.x == 36 && tx.y == 380) unknown = true;
            }
            check(shown_name && unknown, "leader: a map that this machine does not have: the room's name and the question marks (not another map of the list)");
            l.set_room(v);
        }
        // the controls: Up, Down, the wrap and Fog change nothing and tell nobody; START, Enter, the keypad's Enter, S and s ask; the room is the room's
        int asked = 0, started = 0, left = 0;
        std::vector<std::string> maps_chosen;
        std::vector<bool> fog_chosen;
        l.set_on_request_start([&]() { ++asked; });
        l.set_on_start([&](const std::string&) { ++started; });
        l.set_on_quit([&]() { ++left; });
        l.set_on_map_changed([&](const std::string& f) { maps_chosen.push_back(f); });
        l.set_on_fog_changed([&](bool on) { fog_chosen.push_back(on); });
        const int32_t idx = l.get_selected_index();
        click(l, MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3);
        click(l, MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3);
        click(l, MS::BTN_FOW_OFF_X + 2, MS::BTN_FOW_OFF_Y + 2);
        for (int i = 0; i < 14; ++i) {                                              // (more than the six maps: the wrap changes nothing either)
            l.handle_key_down(SDLK_DOWN);
            l.handle_key_down(SDLK_UP);
        }
        l.handle_key_down(SDLK_UP);
        l.set_selected_index(idx + 1);
        check(l.get_selected_index() == idx && l.is_fog_of_war_enabled(), "leader: Up, Down, the wrap and the Fog buttons change neither the map nor the fog option");
        check(maps_chosen.empty() && fog_chosen.empty() && asked == 0 && started == 0, "leader: and tell nobody; nothing starts");
        // the buttons answer the pointer as the host's do (the same pictures), they only do nothing
        l.handle_mouse_motion(MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        RecordingRenderer hov(arc);
        l.render(hov, arc);
        check(hov.has_sprite_at("bstart2.bmp", 526, 439), "leader: START shows its hover picture");
        l.handle_mouse_down(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        RecordingRenderer prs(arc);
        l.render(prs, arc);
        check(prs.has_sprite_at("up3.bmp", 224, 301), "leader: Up shows its pressed picture while it is held");
        l.handle_mouse_up(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        // START
        click(l, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        check(asked == 1 && started == 0, "leader: the START button asks the server (and does not start by itself)");
        l.handle_key_down(SDLK_RETURN);
        check(asked == 2, "leader: Enter asks");
        l.handle_key_down(SDLK_KP_ENTER);
        check(asked == 3, "leader: the keypad's Enter asks");
        l.handle_key_down(SDLK_s);
        check(asked == 4, "leader: S and s ask (the key is 's' whatever the shift)");
        l.handle_key_down(SDLK_ESCAPE);
        l.handle_key_down(SDLK_x + 1000);
        check(asked == 4 && started == 0 && left == 0, "leader: every other key does nothing (Esc included)");
        l.lock();                                                                   // the server's Start arrived: the screen is locked, nothing more to ask
        click(l, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        l.handle_key_down(SDLK_RETURN);
        l.handle_key_down(SDLK_s);
        check(asked == 4, "leader: a locked screen asks nobody (a second click on START while the match loads)");
        l.unlock();                                                                 // the start was cancelled: the leader may ask again
        l.handle_key_down(SDLK_s);
        check(asked == 5, "leader: after a cancelled start START asks again");
        // Leave
        click(l, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        l.handle_key_down(SDLK_q);
        l.handle_key_down(SDLK_x);
        check(left == 3, "leader: the Leave button, Q and X leave");
        l.lock();
        click(l, MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        check(left == 4, "leader: Leave works on a locked screen too (every player can leave)");
        // a leader screen that nobody gave a request callback: START is harmless
        MS bare;
        bare.init();
        bare.set_room(v);
        bare.handle_key_down(SDLK_RETURN);
        click(bare, MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        check(!bare.is_locked(), "leader: START without a callback does nothing at all");
    }
    {   // a HELD key: the repeat of START (Enter, the keypad's Enter, S, s) does not ask; a press does; the other keys act on a repeat (Leave, Up and Down scroll the list while held)
        MS h;
        h.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = false;
        v.leader = true;
        v.my_seat = 0;
        v.seats[0] = {true, "Alice", MS::Thumb::Good};
        v.seats[1] = {true, "Bob", MS::Thumb::Good};
        v.map_file = "TINY.LVL";
        h.set_room(v);
        int asked = 0, left = 0;
        h.set_on_request_start([&]() { ++asked; });
        h.set_on_quit([&]() { ++left; });
        for (const SDL_Keycode key : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) h.handle_key_down(key, true);
        check(asked == 0, "leader: a held Enter, keypad Enter or S (the repeat of a key) does not ask: START needs a press");
        for (const SDL_Keycode key : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) h.handle_key_down(key, false);
        check(asked == 3, "leader: the press of each of them asks");
        h.handle_key_down(SDLK_RETURN);                                             // (the default is a press)
        check(asked == 4, "leader: handle_key_down(key) is a press");
        h.handle_key_down(SDLK_RETURN, false);
        for (int i = 0; i < 30; ++i) h.handle_key_down(SDLK_RETURN, true);          // the key is held down: one press, thirty repeats
        check(asked == 5, "leader: a press and a key that stays down ask once");
        h.handle_key_down(SDLK_x, true);
        h.handle_key_down(SDLK_q, true);
        check(left == 2, "leader: Leave (Q, X) acts on a repeat as on a press");
        h.lock();                                                                   // (a locked screen asks nobody, with or without a repeat)
        h.handle_key_down(SDLK_s, false);
        check(asked == 5, "leader: a locked screen asks nobody");
    }
}

// What the setup screens do with a held key. The ORIGINAL's own screens act on a repeat as on a press: its key translation (FUN_01031bbd) never reads the repeat bit of a key message, its
// input queue admits one key event per 50 ms for a key (FUN_0103078d), and the screen's key handler (FUN_01014076) does not look at it either; so the repeat of START starts a local game
// exactly as the press does, and Up and Down scroll while they are held. The LOCAL game's screen keeps that. The screens of a ROOM do not (the review of the online rooms: an Enter that
// closed the chat input, or the quick help, and was still held started a match that nobody meant to start): the leader's screen of a server's room (v0.0.93) and the host's screen of a
// room on the local network need a PRESS.
void test_held_keys_on_original_screens() {
    std::printf("[held keys] the local game's screen acts on a key repeat as on a press (the original's); the screens of a room (the leader's, a LAN host's) need a press\n");
    using MS = MapSelectScreen;
    {   // the local screen: the original's behaviour, untouched
        MS local;
        local.init();
        int started = 0;
        local.set_on_start([&](const std::string&) { ++started; });
        local.handle_key_down(SDLK_RETURN, true);
        local.handle_key_down(SDLK_KP_ENTER, true);
        local.handle_key_down(SDLK_s, true);
        check(started == 3, "local screen: the repeat of Enter, the keypad's Enter and S starts the game, as the press does");
        const int32_t index = local.get_selected_index();
        local.handle_key_down(SDLK_DOWN, true);
        local.handle_key_down(SDLK_DOWN, true);
        check(local.get_selected_index() == (index + 2) % static_cast<int32_t>(local.get_maps().size()), "local screen: Down held steps the list on every repeat");
    }
    {   // a LAN host (a networked screen whose machine hosts)
        MS host;
        host.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = true;
        v.my_seat = 0;
        v.seats[0] = {true, "Alice", MS::Thumb::Good};
        host.set_room(v);
        int started = 0;
        host.set_on_start([&](const std::string&) { ++started; });
        host.handle_key_down(SDLK_RETURN, true);
        host.handle_key_down(SDLK_s, true);
        host.handle_key_down(SDLK_KP_ENTER, true);
        check(started == 0, "LAN host: the repeat of START starts nothing (a screen of a room: the chat input exists there)");
        host.handle_key_down(SDLK_s, false);
        check(started == 1, "LAN host: the press of S starts the match");
    }
    {   // a guest of a server's room that is not its leader has no START at all, with or without a repeat
        MS guest;
        guest.init();
        MS::RoomView v;
        v.networked = true;
        v.is_host = false;
        v.leader = false;
        v.my_seat = 1;
        v.seats[0] = {true, "Alice", MS::Thumb::Good};
        v.seats[1] = {true, "Bob", MS::Thumb::Good};
        guest.set_room(v);
        int started = 0, asked = 0;
        guest.set_on_start([&](const std::string&) { ++started; });
        guest.set_on_request_start([&]() { ++asked; });
        guest.handle_key_down(SDLK_RETURN, true);
        guest.handle_key_down(SDLK_RETURN, false);
        check(started == 0 && asked == 0, "guest: no START, press or repeat");
    }
}

// The original's button class tests the rectangle of the picture that shows (docs 5.52): the START! buttons of the quick help and of the setup screen, the zones
// that follow from FUN_010112e9 / FUN_01011281 / FUN_01011206, and the plate that the remake draws in the corner
void test_button_zones(const assets::AssetArchive& arc) {
    std::printf("[zones] the START buttons: positions in each state, the hit rectangle of the picture that shows, dead strips, the corner plate\n");
    using MS = MapSelectScreen;
    auto rect = [&](const char* n) { const UIRect r = animation_bounds(arc, n); return ButtonRect{r.x, r.y, r.w, r.h}; };
    const ButtonRect qh_up{529, 437, 98, 27};
    const ButtonRect qh_down{528, 438, 97, 24};
    const ButtonRect st_up{526, 439, 98, 27};
    const ButtonRect st_down{527, 443, 97, 24};
    check(rect("qh_start1") == qh_up && rect("qh_start2") == qh_up && rect("qh_start3") == qh_down, "quick help START pictures: qh_start1 / 2 (529, 437) 98 x 27, qh_start3 (528, 438) 97 x 24");
    check(rect("start1") == st_up && rect("start2") == st_up && rect("start3") == st_down, "setup START pictures: start1 / 2 (526, 439) 98 x 27, start3 (527, 443) 97 x 24");
    // the shift between the two screens is authored in the CHD (both buttons are built at (0, 0), both windows are (0, 0, 640, 480)): the original moves it too
    check(st_up.x - qh_up.x == -3 && st_up.y - qh_up.y == 2 && st_down.x - qh_down.x == -1 && st_down.y - qh_down.y == 5,
          "the setup START sits (-3, +2) from the quick help START (up, hover) and (-1, +5) (pressed): as in the original");
    check(MS::BTN_START_X == st_up.x && MS::BTN_START_Y == st_up.y && MS::BTN_START_W == st_up.w && MS::BTN_START_H == st_up.h && MS::BTN_START_PRESSED == st_down, "the screen's constants are those pictures");

    // click zones: the press must hit the up / hover picture, every move until the release must stay on the pressed picture
    auto make = [](ButtonRect up, ButtonRect down) { return ScreenButton(up, down); };
    auto fires = [](ScreenButton b, int32_t x, int32_t y) {
        b.on_move(x, y);
        b.on_press(x, y);
        return b.on_release(x, y);
    };
    auto shows_pressed = [](ScreenButton b, int32_t x, int32_t y) {
        b.on_move(x, y);
        b.on_press(x, y);
        return b.pressed();
    };
    auto sounds = [](ScreenButton b, int32_t x, int32_t y) {      // the press that captured (the pressed animation starts, its sound plays)
        b.on_move(x, y);
        return b.on_press(x, y);
    };
    const ScreenButton qh = make(qh_up, qh_down);
    check(fires(qh, 529, 438) && fires(qh, 624, 461) && fires(qh, 580, 450), "quick help START: fires in [529, 625) x [438, 462)");
    check(!fires(qh, 528, 450) && !sounds(qh, 528, 450), "quick help START: x = 528 is on the pressed picture only: no capture, no sound");
    check(!fires(qh, 529, 437) && sounds(qh, 529, 437) && !shows_pressed(qh, 529, 437), "quick help START: y = 437 is on the up picture only: the press is cancelled at once (the pressed picture never shows)");
    check(!fires(qh, 625, 450) && !fires(qh, 529, 462) && !fires(qh, 626, 463) && !fires(qh, 627, 450), "quick help START: x = 625, 626, y = 462, 463 and x = 627 fire nothing");
    const ScreenButton st = make(st_up, st_down);
    check(fires(st, 527, 443) && fires(st, 623, 465) && fires(st, 580, 450), "setup START: fires in [527, 624) x [443, 466)");
    check(!fires(st, 623, 466) && !sounds(st, 623, 466), "setup START: y = 466 is on the pressed picture only: no capture");
    check(!fires(st, 526, 450) && !fires(st, 531, 440) && !fires(st, 527, 442) && !fires(st, 527, 439), "setup START: x = 526 and y = 439 .. 442 are on the up picture only: nothing fires");
    check(!fires(st, 624, 450), "setup START: x = 624 is outside both pictures");
    // the hover picture at rest: the up rectangle (the pointer on the pressed-only strip does not hover)
    {
        ScreenButton b = st;
        b.on_move(530, 440);
        check(b.hovered() && !b.pressed(), "a pointer on the up picture hovers");
        b.on_move(623, 466);
        check(!b.hovered(), "a pointer below the up picture does not");
        b.on_move(530, 450);
        b.on_press(530, 450);
        check(b.pressed() && b.hovered(), "a captured button is pressed and hovered");
        b.on_move(526, 450);                                     // x = 526 is on the up picture only: the pressed picture does not contain it any more: the capture is gone
        check(!b.pressed() && !b.hovered(), "a move onto the up-only strip while captured cancels the capture for good");
        b.on_move(560, 450);
        check(b.hovered() && !b.pressed(), "coming back only hovers");
    }
    // the corner plate (frame rate, sparkline, version) starts below the pressed START of the setup screen and ends at the last row
    check(FPS_OVERLAY_TOP >= st_down.y + st_down.h && FPS_OVERLAY_TOP >= qh_up.y + qh_up.h, "the corner plate starts below both START buttons, pressed or not");
    check(FPS_OVERLAY_BOTTOM <= 480 && FPS_OVERLAY_SPARK_Y + font_cell_height(FontSize::Px12) <= 480, "and the plate and the 12 px text end inside the 480 rows");
}

// The same rule for the other buttons of the class that the release converted: the setup screen's Up, Down and Leave, the results screen's Leave, the options
// screen's Return; and the host's screen after a cancelled start
void test_more_button_zones(const assets::AssetArchive& arc) {
    std::printf("[zones] Up, Down, Leave (setup and results), Return (options): click zones; unlock after a cancelled start\n");
    using MS = MapSelectScreen;
    // setup Up: up1 [226, 272) x [299, 321), up3 [224, 271) x [301, 322): the zone is [226, 271) x [301, 321)
    {
        MS screen;
        screen.init();
        const int32_t n = static_cast<int32_t>(screen.get_maps().size());
        auto steps = [&](int32_t x, int32_t y) {
            const int32_t before = screen.get_selected_index();
            click(screen, x, y);
            return screen.get_selected_index() != before;
        };
        check(n >= 2, "setup zones: the map list has two maps or more (the six of the original)");
        check(steps(230, 305) && steps(226, 301) && steps(270, 320), "setup Up: fires in [226, 271) x [301, 321)");
        check(!steps(225, 305), "setup Up: x = 225 is on the pressed picture only: no capture");
        check(!steps(230, 300) && !steps(271, 305), "setup Up: y = 300 and x = 271 are on the resting picture only: captured, then cancelled");
        // setup Down: down1 [226, 273) x [323, 343), down3 [225, 272) x [324, 345): the zone is [226, 272) x [324, 343)
        check(steps(230, 330) && steps(226, 324) && steps(271, 342), "setup Down: fires in [226, 272) x [324, 343)");
        check(!steps(225, 330) && !steps(272, 330) && !steps(230, 323) && !steps(230, 343), "setup Down: x = 225, 272 and y = 323, 343 fire nothing");
        // setup Leave: leave1 [525, 624) x [12, 34), leave3 [524, 622) x [14, 34): the zone is [525, 622) x [14, 34)
        int left = 0;
        screen.set_on_quit([&]() { ++left; });
        click(screen, 530, 20);
        click(screen, 525, 14);
        click(screen, 621, 33);
        check(left == 3, "setup Leave: fires in [525, 622) x [14, 34)");
        click(screen, 524, 20);
        click(screen, 530, 12);
        click(screen, 623, 20);
        click(screen, 530, 34);
        check(left == 3, "setup Leave: x = 524, y = 12, x = 623 and y = 34 fire nothing");
    }
    // results Leave: the same pictures
    {
        ScorecardModal card;
        sim::MatchResult mr;
        mr.is_over = true;
        card.show(mr, 0);
        card.update(0.25f);                                                                    // the rows, and with them the Leave button
        int left = 0;
        card.set_on_quit([&]() { ++left; });
        auto leave = [&](int32_t x, int32_t y) {
            card.handle_mouse_motion(x, y);
            card.handle_mouse_down(x, y);
            card.handle_mouse_up(x, y);
        };
        leave(530, 20);
        leave(525, 14);
        leave(621, 33);
        check(left == 3, "results Leave: fires in [525, 622) x [14, 34)");
        leave(524, 20);
        leave(530, 12);
        leave(623, 20);
        leave(530, 34);
        check(left == 3, "results Leave: the strips of one picture only fire nothing");
        card.handle_mouse_motion(530, 20);
        check(card.is_quit_hovered(), "results Leave: hover on the resting picture");
        card.handle_mouse_down(530, 20);
        card.handle_mouse_motion(623, 20);                                                     // x = 623 is on the resting picture only: the capture is gone for good
        card.handle_mouse_motion(530, 20);
        card.handle_mouse_up(530, 20);
        check(left == 3 && !card.is_quit_pressed(), "results Leave: leaving the pressed picture cancels the capture for good");
    }
    // options Return: breturn1 [351, 449) x [425, 451), breturn3 [353, 450) x [427, 451): the zone is [353, 449) x [427, 451)
    {
        ScreenButton ret(ButtonRect{OptionsScreen::RETURN_X, OptionsScreen::RETURN_Y, OptionsScreen::RETURN_W, OptionsScreen::RETURN_H}, OptionsScreen::RETURN_PRESSED);
        auto fires = [&](int32_t x, int32_t y) {
            ScreenButton b = ret;
            b.on_move(x, y);
            b.on_press(x, y);
            return b.on_release(x, y);
        };
        check(ret.up_rect() == ButtonRect({351, 425, 98, 26}) && ret.pressed_rect() == ButtonRect({353, 427, 97, 24}), "options Return: breturn1 / 2 and breturn3 rectangles");
        const UIRect r1 = animation_bounds(arc, "breturn1");
        const UIRect r3 = animation_bounds(arc, "breturn3");
        check(r1.x == 351 && r1.y == 425 && r1.w == 98 && r1.h == 26 && r3.x == 353 && r3.y == 427 && r3.w == 97 && r3.h == 24, "options Return: the rectangles are the CHD's");
        check(fires(353, 427) && fires(448, 450) && fires(400, 440), "options Return: fires in [353, 449) x [427, 451)");
        check(!fires(351, 440) && !fires(400, 425) && !fires(449, 440), "options Return: x = 351, y = 425 and x = 449 fire nothing");
    }
    // the host's screen after a cancelled start: unlock gives the controls back
    {
        MS screen;
        screen.init();
        screen.update(0.5f);
        const int32_t first = screen.get_selected_index();
        screen.lock();
        screen.handle_key_down(SDLK_DOWN);
        check(screen.is_locked() && screen.get_selected_index() == first, "locked (START ran): the map cannot be changed");
        screen.unlock();
        screen.handle_key_down(SDLK_DOWN);
        check(!screen.is_locked() && screen.get_selected_index() != first, "unlocked (the start was cancelled): the map can be chosen again");
    }
}

// The original's label fonts (Ants.exe label constructors 0x10116cb / 0x1011856, docs 5.14): every text uses its own cell height
void test_text_sizes(const assets::AssetArchive& arc) {
    std::printf("[text] every label uses the original's font height\n");
    auto find = [](const RecordingRenderer& rr, const std::string& text) -> const RecordingRenderer::Text* {
        for (const auto& t : rr.texts) {
            if (t.text == text) return &t;
        }
        return nullptr;
    };
    // the HUD: status line and chat 12, the score bars' labels 14 (right aligned ending at x = 399 for the local player)
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.post_status("Ready!");
        hud.add_chat_entry("Bob", "Hello there");
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        const auto* status = find(rr, "Ready!");
        check(status != nullptr && status->size == FontSize::Px12 && status->x == 481 && status->y == 254, "hud: the status line is 12 px high at (481, 254)");
        const auto* chat = find(rr, "Hello there");
        check(chat != nullptr && chat->size == FontSize::Px12, "hud: the chat log is 12 px high");
        const auto* header = find(rr, "Bob:");
        check(header != nullptr && header->size == FontSize::Px12 && chat != nullptr && chat->y == header->y + 12, "hud: chat lines are one 12 px cell apart");
        const auto* mine = find(rr, "Player:");                      // the HUD's default player name
        check(mine != nullptr && mine->size == FontSize::Px14 && mine->x + 7 * 6 == 399 && mine->y == 4, "hud: the own score label is 14 px high, right aligned to x = 399, at the top of its box");
    }
    // the quit dialog: one centred label of 24 px letters in (130, 180) 260 x 160
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.open_quit_dialog();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        const auto* prompt = find(rr, "Do you really want to quit?");
        check(prompt != nullptr && prompt->size == FontSize::Px24 && prompt->y == 180 && prompt->x == 130 + (260 - 27 * 6) / 2,
              "quit: the prompt is 24 px high, centred in its box, at y = 180");
    }
    // the start dialog: 35 px letters wrapped at 240 px, one cell (35 px) between the lines, "Waiting for others..." 24 px in (130, 290)
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        HUD hud;
        hud.init(0);
        hud.start_match_modal();
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
        // with 6 px per character a line of up to 39 characters fits in 240 px
        const std::string line1 = "Get ready to play!  You are the Green";
        const std::string line2 = "Ants.";
        const auto* l1 = find(rr, line1);
        const auto* l2 = find(rr, line2);
        check(l1 != nullptr && l1->size == FontSize::Px35 && l1->y == 110 && l1->x == 130 + (240 - 37 * 6) / 2, "start dialog: the first line is 35 px high, centred, at y = 110");
        check(l2 != nullptr && l2->size == FontSize::Px35 && l2->y == 110 + 35 && l2->x == 130 + (240 - 5 * 6) / 2, "start dialog: the second line follows one 35 px cell lower");
        const auto* footer = find(rr, "Waiting for others...");
        check(footer != nullptr && footer->size == FontSize::Px24 && footer->y == 290 && footer->x == 130 + (240 - 21 * 6) / 2, "start dialog: the footer is 24 px high at y = 290");
    }
    // the setup screen (after its refresh): map name and description 18 px at x = 36 (y 312 and 380), the prompt 14 px in its label (36, 447), the player's name 18 px
    // at the row's top; every label in (239, 231, 223)
    {
        MapSelectScreen screen;
        screen.init();
        screen.update(0.5f);
        RecordingRenderer rr(arc);
        screen.render(rr, arc);
        const auto& maps = screen.get_maps();
        const auto& cur = maps[static_cast<size_t>(screen.get_selected_index())];
        const auto* name = find(rr, cur.display_name);
        check(name != nullptr && name->size == FontSize::Px18 && name->x == 36 && name->y == 312, "setup: the map name is 18 px high at (36, 312) (the original's label top)");
        check(name != nullptr && name->colour.r == 239 && name->colour.g == 231 && name->colour.b == 223, "setup: the labels' colour is (239, 231, 223)");
        const auto* info = find(rr, cur.description + " (" + std::to_string(cur.minutes) + " min)");
        check(info != nullptr && info->size == FontSize::Px18 && info->x == 36 && info->y == 380, "setup: the map description is 18 px high at (36, 380)");
        const auto lines = wrap_label_text(rr, "Press START when all players' thumbs have appeared.", MapSelectScreen::STATUS_W, FontSize::Px14);
        const auto* first = lines.empty() ? nullptr : find(rr, lines[0]);
        check(first != nullptr && first->size == FontSize::Px14 && first->x == 36 && first->y == 447, "setup: the prompt is 14 px high in its label at (36, 447)");
        const auto* player = find(rr, "Player");
        check(player != nullptr && player->size == FontSize::Px18 && player->x == 415 && player->y == 95, "setup: the player's name is 18 px high at (415, 95)");
    }
    // the results screen: see test_results_screen
}

// The results screen (Ants.exe FUN_010153a1 constructor, FUN_010155ac rows and layout, FUN_01015136 row builder, docs 5.49)
void test_results_screen(const assets::AssetArchive& arc) {
    std::printf("[results] waiting phase, rows, positions, portraits, the cue, the Leave button\n");
    sim::MatchResult result;
    result.is_over = true;
    result.present_mask = 0x0F;
    result.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};                 // teams 0 and 1 are allied
    result.stats[0].score = 300; result.stats[0].friendly_lost = 1; result.stats[0].enemy_killed = 2; result.stats[0].new_hatched = 3;
    result.stats[1].score = 200; result.stats[1].friendly_lost = 10; result.stats[1].enemy_killed = 20; result.stats[1].new_hatched = 30;
    result.stats[2].score = 450; result.stats[2].friendly_lost = 4; result.stats[2].enemy_killed = 5; result.stats[2].new_hatched = 6;
    result.stats[3].score = 100;

    ScorecardModal card;
    card.set_player_names({"Alice", "Bob", "Carol", "Dave"});
    card.show(result, 2);
    check(card.is_open() && card.is_waiting(), "results: the screen opens in its waiting phase");
    check(card.get_audio_to_play() == 0, "results: no cue while it waits (one cue per machine, when the rows are built)");
    {
        RecordingRenderer rr(arc);
        card.render(rr, arc);
        check(rr.texts.size() == 1 && rr.texts[0].text == "Waiting for scores..." && rr.texts[0].x == 100 && rr.texts[0].y == 350 && rr.texts[0].size == FontSize::Px20,
              "results: the label \"Waiting for scores...\" is 20 px high at (100, 350)");
        check(!rr.texts.empty() && rr.texts[0].colour.r == 239 && rr.texts[0].colour.g == 231 && rr.texts[0].colour.b == 223, "results: its colour is (239, 231, 223)");
        bool leave = false, portrait = false;
        for (const auto& sp : rr.sprites) {
            leave = leave || sp.name.rfind("bleave", 0) == 0;
            portrait = portrait || sp.name.rfind("agst30", 0) == 0;
        }
        check(!leave, "results: there is no Leave button while it waits");
        check(!portrait, "results: there are no portraits while it waits");
        check(!card.handle_mouse_down(550, 20) || !card.is_quit_pressed(), "results: a press where the Leave button will be does nothing yet");
    }
    card.update(0.200f);
    check(card.is_waiting() && card.get_audio_to_play() == 0, "results: 200 ms later it still waits");
    card.update(0.049f);
    check(card.is_waiting(), "results: 249 ms: still waiting");
    card.update(0.001f);
    check(!card.is_waiting(), "results: after 250 ms the rows are built");
    check(card.get_audio_to_play() == sim::SoundID::PlayerDefeat, "results: the cue is chosen when the rows appear: team 2 is not in the top row (the loser cue)");
    {
        RecordingRenderer rr(arc);
        card.render(rr, arc);
        check(card.rows().size() == 3, "results: one row per alliance or single team: \"Alice & Bob\", Carol, Dave");
        auto text_at = [&](const std::string& text, int32_t x, int32_t y) {
            for (const auto& t : rr.texts) if (t.text == text && t.x == x && t.y == y && t.size == FontSize::Px18) return &t;
            return static_cast<const RecordingRenderer::Text*>(nullptr);
        };
        // the top row stands at Y = 235, the others at 50 i + 273
        const auto* n0 = text_at("Alice & Bob", 100, 235);
        const auto* n1 = text_at("Carol", 100, 323);
        const auto* n2 = text_at("Dave", 100, 373);
        check(n0 != nullptr && n1 != nullptr && n2 != nullptr, "results: the names stand at x = 100, y = 235, 323, 373 (18 px)");
        check(n0 != nullptr && n0->colour.r == 239 && n0->colour.g == 231 && n0->colour.b == 223, "results: the name colour is (239, 231, 223)");
        // the numbers are left aligned at 485 / 534 / 555 / 576 and add up the two members of an alliance
        check(text_at("500", 485, 235) != nullptr && text_at("11", 534, 235) != nullptr && text_at("22", 555, 235) != nullptr && text_at("33", 576, 235) != nullptr,
              "results: the alliance row adds up its members and the numbers start at x = 485, 534, 555, 576");
        check(text_at("450", 485, 323) != nullptr && text_at("4", 534, 323) != nullptr && text_at("5", 555, 323) != nullptr && text_at("6", 576, 323) != nullptr,
              "results: the second row (Carol)");
        check(text_at("100", 485, 373) != nullptr && text_at("0", 534, 373) != nullptr, "results: rows with nothing to show still have their numbers (no all-zero row is hidden)");
        // the counters are drawn on one line whatever their width (the substitute font does not fit two digits into the 19 px label)
        ScorecardModal wide;
        sim::MatchResult big = result;
        big.stats[2].friendly_lost = 1234;
        wide.show(big, 2);
        wide.update(0.3f);
        RecordingRenderer rw(arc);
        wide.render(rw, arc);
        bool one_line = false;
        for (const auto& t : rw.texts) one_line = one_line || (t.text == "1234" && t.x == 534 && t.y == 323 && t.size == FontSize::Px18);
        check(one_line, "results: a counter that is wider than its label stays on one line");
        // the ant portraits: one per team at (60, Y + 20), an alliance shows both at (45, Y + 20) and (75, Y + 20), each in its own colour, animated
        const auto* anim = arc.find_animation("agst301");
        check(anim != nullptr && !anim->subitems.empty(), "results: the portrait animation exists");
        if (anim != nullptr && !anim->subitems.empty()) {
            const auto& parts = anim->subitems[Renderer::get_anim_subitem_by_time(*anim, 250)].frames;
            auto portrait_at = [&](int32_t x, int32_t y, uint8_t team) {
                size_t found = 0;
                for (const auto& part : parts) {
                    for (const auto& sp : rr.sprites) {
                        if (sp.id == part.sprite_index && sp.x == x + part.dx && sp.y == y + part.dy && sp.team == team) { ++found; break; }
                    }
                }
                return found == parts.size();
            };
            check(portrait_at(45, 255, 0) && portrait_at(75, 255, 1), "results: the alliance row shows both ants at (45, 255) and (75, 255) in their colours");
            check(portrait_at(60, 343, 2), "results: a single team's ant stands at (60, Y + 20) in its colour");
            check(portrait_at(60, 393, 3), "results: the last row's ant");
            // the portrait moves with the screen's clock: later the clip shows another frame
            const size_t f_now = Renderer::get_anim_subitem_by_time(*anim, 250);
            const size_t f_later = Renderer::get_anim_subitem_by_time(*anim, 850);
            check(f_now != f_later, "results: frames 250 ms and 850 ms into the clip differ (a moving portrait is testable)");
            card.update(0.600f);
            RecordingRenderer later(arc);
            card.render(later, arc);
            const auto& parts_later = anim->subitems[f_later].frames;
            size_t found = 0;
            for (const auto& part : parts_later) {
                for (const auto& sp : later.sprites) {
                    if (sp.id == part.sprite_index && sp.x == 60 + part.dx && sp.y == 343 + part.dy && sp.team == 2) { ++found; break; }
                }
            }
            check(found == parts_later.size(), "results: 600 ms later the portrait shows the next frame of its clip");
        }
        // the Leave button exists now
        bool leave = false;
        for (const auto& sp : rr.sprites) leave = leave || sp.name.rfind("bleave", 0) == 0;
        check(leave, "results: the Leave button is there once the rows are");
        bool left = false;
        card.set_on_quit([&]() { left = true; });
        card.handle_mouse_down(550, 20);
        check(card.is_quit_pressed(), "results: a press on Leave presses it");
        card.handle_mouse_up(550, 20);
        check(left, "results: the release leaves");
    }
    // the cue for the other members: Bob's ally is the top row's first team (Alice), so Bob hears the winner cue
    {
        ScorecardModal bob;
        bob.show(result, 1);
        bob.update(0.3f);
        check(bob.get_audio_to_play() == sim::SoundID::VictoryFanfare, "results: the winner cue for the ally of the top row's first team");
        ScorecardModal alice;
        alice.show(result, 0);
        alice.update(0.3f);
        check(alice.get_audio_to_play() == sim::SoundID::VictoryFanfare, "results: and for the first team itself");
    }
    // teams that are not shown (a browser game has no other players) have no row; a team that dropped has none either
    {
        ScorecardModal only;
        only.set_shown_teams(0x05);
        only.show(result, 2);
        only.update(0.3f);
        check(only.rows().size() == 2 && only.rows()[0].first == 2 && only.rows()[1].first == 0, "results: only the shown teams have rows (Alice's alliance partner is not shown: a single row)");
        sim::MatchResult dropped = result;
        dropped.present_mask = 0x0D;                                               // team 1 dropped out
        ScorecardModal d;
        d.set_player_names({"Alice", "Bob", "Carol", "Dave"});
        d.show(dropped, 0);
        d.update(0.3f);
        check(d.rows().size() == 3 && d.rows()[0].name == "Carol" && d.rows()[1].name == "Alice" && d.rows()[2].name == "Dave", "results: a dropped team has no row");
    }
    // names: without a name the colour word; the local player's own name; at most 35 characters
    {
        ScorecardModal n;
        n.set_local_player_name("Me");
        n.show(result, 3);
        n.update(0.3f);
        bool me = false, word = false;
        for (const auto& r : n.rows()) {
            me = me || r.name == "Me";
            word = word || r.name == "Red" || r.name == "Blue" || r.name.find("Red") != std::string::npos;
        }
        check(me && word, "results: the local player's own name, the colour words for unnamed teams");
        ScorecardModal longname;
        longname.set_player_names({std::string(30, 'a'), std::string(30, 'b'), "C", "D"});
        longname.show(result, 0);
        longname.update(0.3f);
        check(longname.rows()[0].name.size() == 35, "results: a row name is at most 35 characters (the label's limit)");
    }
}

namespace {

// The original's greedy word wrap (FUN_0102b0b5), checked on a renderer that measures 15 px per character
class MonoRenderer : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA) override { drawn.push_back({text, x, y, FontSize::Px12}); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA, FontSize size) override { drawn.push_back({text, x, y, size}); }
    int32_t get_text_width(const std::string& text, FontSize = FontSize::Px12) const override { return static_cast<int32_t>(text.size()) * 15; }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override {}
    struct Drawn { std::string text; int32_t x; int32_t y; FontSize size; };
    std::vector<Drawn> drawn;
};

}  // namespace

void test_label_wrap() {
    std::printf("[text] the original's label wrap and drawing\n");
    MonoRenderer mono;
    auto lines = wrap_label_text(mono, "Get ready to play!  You are the Green Ants.", 240, FontSize::Px35);
    // 15 px per character: a prefix of 16 characters reaches 240 px; the line ends at the last word end before that
    check(lines.size() == 3 && lines[0] == "Get ready to" && lines[1] == "play!  You are" && lines[2] == "the Green Ants.", "wrap: the start dialog's text at 240 px, 15 px per character");
    // a word that is wider than the box is cut one character before the width is reached
    lines = wrap_label_text(mono, "Supercalifragilistic", 100, FontSize::Px18);
    check(lines.size() == 4 && lines[0] == "Superc" && lines[1] == "alifra" && lines[2] == "gilist" && lines[3] == "ic", "wrap: a long word is cut before the width");
    // leading spaces of a line are skipped, empty text has no line, a box without width keeps the text in one line
    lines = wrap_label_text(mono, "   hello", 300, FontSize::Px12);
    check(lines.size() == 1 && lines[0] == "hello", "wrap: leading spaces are skipped");
    check(wrap_label_text(mono, "", 300, FontSize::Px12).empty(), "wrap: no text, no line");
    lines = wrap_label_text(mono, "no room at all", 0, FontSize::Px12);
    check(lines.size() == 1 && lines[0] == "no room at all", "wrap: a box without width does not loop");
    lines = wrap_label_text(mono, "one\ntwo words here", 300, FontSize::Px12);
    check(lines.size() == 2 && lines[0] == "one" && lines[1] == "two words here", "wrap: a newline ends a line");
    // fit_text: characters are dropped from the end until the text fits
    check(fit_text(mono, "abcdefghij", 60, FontSize::Px12) == "abcd" && fit_text(mono, "abc", 60, FontSize::Px12) == "abc" && fit_text(mono, "x", 1, FontSize::Px12) == "x",
          "fit_text: cut to the width, one character stays");
    // draw_label: centred lines, one cell height apart, from the top of the box
    mono.drawn.clear();
    const int32_t h = draw_label(mono, "Get ready to play!  You are the Green Ants.", 130, 110, 240, assets::ColorRGBA{}, FontSize::Px35, true);
    check(h == 3 * 35 && mono.drawn.size() == 3, "draw_label: three lines of 35 px");
    check(mono.drawn.size() == 3 && mono.drawn[0].y == 110 && mono.drawn[1].y == 145 && mono.drawn[2].y == 180, "draw_label: the lines are 35 px apart from the top of the box");
    check(mono.drawn.size() == 3 && mono.drawn[0].x == 130 + (240 - 12 * 15) / 2 && mono.drawn[1].x == 130 + (240 - 14 * 15) / 2 && mono.drawn[2].x == 130 + (240 - 15 * 15) / 2,
          "draw_label: every line is centred in the box");
    mono.drawn.clear();
    draw_label(mono, "left text", 40, 50, 300, assets::ColorRGBA{}, FontSize::Px18, false);
    check(mono.drawn.size() == 1 && mono.drawn[0].x == 40 && mono.drawn[0].y == 50 && mono.drawn[0].size == FontSize::Px18, "draw_label: a left aligned label starts at the box's left edge");
}

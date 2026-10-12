// View fingerprint, part 1: the self-checks, the draw calls of HUD::render and of the full-screen screens and of the start menu.
// (See test_view_fingerprint.cpp for what the whole program pins.)
#include "view_fingerprint_fixtures.hpp"

namespace {
constexpr int32_t kMaxCamX = static_cast<int32_t>(kMap) * 32 - PLAYFIELD_W;
constexpr int32_t kMaxCamY = static_cast<int32_t>(kMap) * 32 - PLAYFIELD_H;
}  // namespace

// =====================================================================================================================================================
// HUD::render scenarios (recording renderer)
// =====================================================================================================================================================


void hud_scenarios(const assets::AssetArchive& arc) {
    using T = sim::AntType;

    // The start of a match: the "Get ready" dialog over the whole HUD, for each local colour (the shell art, the sprites' colour and the backing fill depend on it)
    for (uint8_t p = 0; p < 4; ++p) {
        Scene s(arc, p);
        s.hud.start_match_modal();
        s.look(100, 80);
        s.shoot("hud.start.modal.p" + std::to_string(p));
    }
    {
        Scene s(arc, 0);
        s.hud.start_match_modal();
        s.tick(30);                                              // 1.5 s into the dialog: the portrait is at another frame
        s.shoot("hud.start.modal.1500ms");
        s.tick(70);                                              // 5.0 s: the modal has closed itself
        s.shoot("hud.start.modal.closed_by_timer");
    }
    // The same with the dialog closed: the idle HUD of each colour (welcome status, the "Game started" news flash, the score boxes)
    for (uint8_t p = 0; p < 4; ++p) {
        Scene s(arc, p);
        s.shoot("hud.start.idle.p" + std::to_string(p));
    }
    {
        Scene s(arc, 0);
        s.hud.start_match_modal();
        s.hud.dismiss_match_start_modal();
        s.shoot("hud.start.idle.dismissed");
    }
    {
        Scene s(arc, 0);                                         // the match clock: 12:00, 10:59, 1:01, 0:00 (the tens of minutes are skipped when 0)
        const uint32_t clocks[4] = {12u * 60000u, 10u * 60000u + 59000u, 61000u, 0u};
        for (uint32_t i = 0; i < 4; ++i) {
            s.world.match_time_remaining_ms = clocks[i];
            s.shoot("hud.clock." + std::to_string(clocks[i]));
        }
    }

    // Selections: one own ant of every type (status text of the type, the move pedestal, the ability pedestal, the Stop button)
    {
        const char* names[7] = {"worker", "worker2", "bomber", "fire", "thief", "combat", "swimmer"};
        for (uint32_t i = 0; i < 7; ++i) {
            Scene s(arc, 0);
            s.hud.select_ant(1 + i);
            s.tick(0);
            s.shoot(std::string("hud.sel.one.") + names[i]);
        }
    }
    {
        Scene s(arc, 0);                                         // several own ants: the group text, no ability pedestal
        s.hud.set_selected_ant_ids({1, 2, 3, 4, 5});
        s.tick(0);
        s.shoot("hud.sel.group");
    }
    {
        Scene s(arc, 0);                                         // every selected ant carries food: the lunchbox indicator
        for (auto& a : s.world.ants) if (a.player_id == 0) a.is_holding = true;
        s.hud.set_selected_ant_ids({1, 2});
        s.tick(0);
        s.shoot("hud.sel.group.carrying");
    }
    {
        Scene s(arc, 0);                                         // an enemy ant under inspection (the status line stays empty)
        s.hud.select_ant(12);
        s.tick(0);
        s.shoot("hud.sel.enemy");
    }
    {
        Scene s(arc, 0);                                         // the own hill: eggs, the hatch pedestal, the Stop button
        s.hud.select_base(0);
        s.tick(0);
        s.shoot("hud.sel.hill.own");
        Scene noeggs(arc, 0);
        noeggs.world.player_eggs[0] = 0;
        noeggs.hud.select_base(0);
        noeggs.tick(0);
        noeggs.shoot("hud.sel.hill.own.noeggs");
        Scene few(arc, 0);
        few.world.player_eggs[0] = 3;
        few.hud.select_base(0);
        few.tick(0);
        few.shoot("hud.sel.hill.own.3eggs");
        Scene enemy(arc, 0);                                     // another player's hill: the ally pedestal
        enemy.hud.select_base(1);
        enemy.tick(0);
        enemy.shoot("hud.sel.hill.enemy");
    }
    {
        Scene s(arc, 2);                                         // the same selections for another colour (blue)
        s.hud.select_ant(21);
        s.tick(0);
        s.shoot("hud.sel.one.p2");
    }
    {
        // the pedestals while they rise (the chains of Ants.exe FUN_01028491 play in real time): a few moments of the move pedestal and the ability pedestal
        const uint32_t moments[5] = {100, 250, 450, 700, 1200};
        for (uint32_t i = 0; i < 5; ++i) {
            Scene s(arc, 0);
            s.hud.select_ant(3);                                 // the bomber: both pedestals rise
            s.tick(0);
            s.shoot("hud.pedestal.rising." + std::to_string(moments[i]), moments[i]);
        }
    }

    // The chat: entries of every colour (wrapped bodies, a news flash), the text being typed with the caret on and off, the log scrolled, the buttons
    auto fill_chat = [](Scene& s) {
        s.hud.add_chat_entry("Alice", "Hello everybody, this is a rather long message that has to wrap over several lines of the log window.", false, 3);
        s.hud.add_chat_entry("Bob", "Team only: attack the red hill together!", true, 2);
        s.hud.add_chat_entry("Carol", "ok", false, 1);
        s.hud.add_news_flash(65000, "Black dropped out of the game!");
    };
    {
        Scene s(arc, 0);
        fill_chat(s);
        s.hud.set_chat_input("Typing a message to all of you");
        s.shoot("hud.chat.typing.caret_on", 10000);              // (10000 - 0) / 150 is even: the caret shows
        s.shoot("hud.chat.typing.caret_off", 10100);             // 10100 / 150 is odd: it does not
    }
    {
        Scene s(arc, 0);                                         // a text that is longer than the box: its end shows
        s.hud.set_chat_input("This message is much too long to fit into the one line box of the chat input field at the bottom");
        s.shoot("hud.chat.typing.long");
    }
    {
        Scene s(arc, 0);                                         // many entries: the log follows the newest (the window has moved)
        for (int i = 0; i < 14; ++i) s.hud.add_chat_entry("Player" + std::to_string(i), "message number " + std::to_string(i) + " of the long conversation", i % 3 == 0, i % 4);
        for (int i = 0; i < 60; ++i) {
            g_now_ms += 50;
            s.hud.update(s.world, 1);
        }
        s.shoot("hud.chat.scrolled", 9000);
    }
    {
        Scene s(arc, 0);                                         // an ally exists: the [Team] button is there; hovered / pressed art of the two buttons
        s.world.player_alliances = {1, 0, 255, 255};
        fill_chat(s);
        s.tick(0);                                               // refreshes the "is on team" flag
        s.shoot("hud.chat.team.buttons");
        s.move(555, 455);                                        // over [All] (532, 443, 44 x 24)
        s.shoot("hud.chat.team.hover_all");
        s.move(600, 455);                                        // over [Team] (579, 443, 46 x 24)
        s.shoot("hud.chat.team.hover_team");
        s.press(600, 455);
        s.shoot("hud.chat.team.pressed_team");
    }
    {
        Scene s(arc, 0);                                         // chat switched off in the options: the input box is covered, no buttons
        s.hud.options().chat = false;
        fill_chat(s);
        s.shoot("hud.chat.off");
    }

    // The minimap: ants of every team, plants, food, power-ups, a fire wall, rocks, the hills, the view frame at the corners and edges of the map
    {
        Scene s(arc, 0);
        s.shoot("hud.minimap.teams");
    }
    {
        const struct { const char* name; int32_t x, y; } cams[] = {
            {"tl", 0, 0}, {"tr", kMaxCamX, 0}, {"bl", 0, kMaxCamY}, {"br", kMaxCamX, kMaxCamY}, {"top", 700, 0}, {"left", 0, 700},
            {"mid", 700, 700}, {"beyond", kMaxCamX + 21, kMaxCamY + 33}};
        for (const auto& c : cams) {
            Scene s(arc, 0);
            s.look(c.x, c.y);
            s.shoot(std::string("hud.minimap.frame.") + c.name);
        }
    }
    {
        // other map sizes: the minimap's scale and the view frame's size depend on them (31 x 31 = TINY, 40 x 40 = SMALL, 14 x 14 and 80 x 50)
        const struct { uint32_t w, h; } sizes[] = {{31, 31}, {40, 40}, {14, 14}, {80, 50}};
        for (const auto& sz : sizes) {
            Scene s(arc, 0, sz.w, sz.h);
            s.look(static_cast<int32_t>(sz.w) * 32 - PLAYFIELD_W, static_cast<int32_t>(sz.h) * 32 - PLAYFIELD_H);
            if (s.camera.world_x < 0) s.look(0, 0);
            s.shoot("hud.minimap.size." + std::to_string(sz.w) + "x" + std::to_string(sz.h));
        }
    }
    {
        Scene s(arc, 0);                                         // no map at all (before the first snapshot): a black minimap box
        s.world = sim::WorldState{};
        s.shoot("hud.minimap.no_world");
    }

    // Fog of war: unexplored cells use the fog colours, ants and objects in the fog are hidden on the minimap
    {
        Scene s(arc, 0);
        s.world.fog_of_war_enabled = true;
        s.world.fog_revealed.assign(static_cast<size_t>(kMap) * kMap, 0);
        for (uint32_t y = 0; y < kMap; ++y) {
            for (uint32_t x = 0; x < kMap; ++x) {
                const int32_t dx = static_cast<int32_t>(x) - 14;
                const int32_t dy = static_cast<int32_t>(y) - 12;
                if (dx * dx + dy * dy < 120 || (x >= 40 && y >= 40 && (x + y) % 3 == 0)) s.world.fog_revealed[static_cast<size_t>(y) * kMap + x] = 1;
            }
        }
        s.hud.select_ant(1);
        s.tick(0);
        s.shoot("hud.fog.on");
    }

    // The score boxes: allied boxes (half and half, the summed score), a dropped team, an absent team, scores over six digits, names, 15 characters at most
    {
        Scene s(arc, 0);
        s.world.player_alliances = {1, 0, 255, 255};
        s.world.player_scores = {500, 500, 450, 100};
        s.shoot("hud.scores.allied");
        Scene t(arc, 2);                                         // the same from the point of view of the third team: two allied boxes in the bottom slots
        t.world.player_alliances = {1, 0, 255, 255};
        t.world.player_scores = {500, 500, 450, 100};
        t.shoot("hud.scores.allied.local_p2");
        Scene u(arc, 0);
        u.world.player_alliances = {255, 255, 3, 2};
        u.world.player_scores = {10, 20, 530, 530};
        u.shoot("hud.scores.allied.bottom_pair");
    }
    {
        Scene s(arc, 0);
        s.world.dropped_mask = 0x04;                             // team 2 dropped out: its box is covered
        s.shoot("hud.scores.dropped");
        Scene t(arc, 0);
        t.hud.set_roster_mask(0x0B);                             // team 2 never was in the match: no label, its box is covered
        t.shoot("hud.scores.absent");
        Scene u(arc, 1);
        u.hud.set_roster_mask(0x03);                             // a two player match, the local player is team 1
        u.shoot("hud.scores.two_players");
    }
    {
        Scene s(arc, 0);
        s.world.player_scores = {1234567, 999999, 0, 7};
        s.shoot("hud.scores.big");
    }
    {
        Scene s(arc, 0);
        s.hud.set_player_name("Local Player With A Long Name");
        s.hud.set_team_names({"", "Redmond", "A Very Long Name Indeed", "Dave"});
        s.shoot("hud.scores.names");
    }

    // A crowd: the minimap with 240 dots, the selection of a big group
    {
        Scene s(arc, 0);
        s.world.ants.clear();
        uint32_t seed = 777u;
        for (uint32_t i = 0; i < 240; ++i) {
            const int32_t tx = static_cast<int32_t>(lcg(seed) % 58u) + 1;
            const int32_t ty = static_cast<int32_t>(lcg(seed) % 58u) + 1;
            const int32_t ox = static_cast<int32_t>(lcg(seed) % 32u);                  // (one statement each: the order in which the arguments of a call are evaluated is the compiler's)
            const int32_t oy = static_cast<int32_t>(lcg(seed) % 32u);
            add_ant(s.world, 1000 + i, static_cast<uint8_t>(i % 4), static_cast<T>(i % 6), tx, ty, ox, oy, i % 5 == 0);
        }
        std::vector<uint32_t> own;
        for (const auto& a : s.world.ants) if (a.player_id == 0) own.push_back(a.id);
        s.hud.set_selected_ant_ids(own);
        s.look(300, 300);
        s.tick(0);
        s.shoot("hud.antheavy");
    }

    // Dialogs: the quit dialog (resting, hover Yes, pressed No), the options screen, the in-match quick help, the three alliance dialogs
    {
        Scene s(arc, 0);
        s.hud.open_quit_dialog();
        s.shoot("hud.quit.open");
        s.move(200, 270);                                        // Yes (180, 260, 49 x 24)
        s.shoot("hud.quit.hover_yes");
        s.move(300, 270);
        s.press(300, 270);                                       // No (292, 260)
        s.shoot("hud.quit.pressed_no");
    }
    {
        Scene s(arc, 0);
        s.hud.open_options();
        s.shoot("hud.options.open");
        s.shoot("hud.options.open.caret_off", 10150);
        Scene t(arc, 0);
        t.hud.options().sound_volume = 30;
        t.hud.options().music_volume = 80;
        t.hud.options().scroll_speed = 99;
        t.hud.options().chat = false;
        t.hud.options().quick_help = false;
        t.hud.options().quick_chat[0] = "Hello!";
        t.hud.options().quick_chat[2] = "A very long quick chat text that does not fit into its edit field of 141 pixels";
        t.hud.open_options();
        t.shoot("hud.options.changed");
        t.move(375, 435);                                        // hover Return (351, 425, 98 x 26)
        t.shoot("hud.options.hover_return");
    }
    {
        Scene s(arc, 0);
        s.hud.open_quick_help();
        s.shoot("hud.quickhelp.open");
        s.move(540, 445);                                        // the Return button (529, 437, 98 x 26)
        s.shoot("hud.quickhelp.hover_return");
        s.press(540, 445);
        s.shoot("hud.quickhelp.pressed_return");
    }
    {
        Scene s(arc, 1);                                         // an invitation from team 0 to the local team 1
        s.world.pending_invite_from[1] = 0;
        s.tick(0);
        s.shoot("hud.dialog.invitation");
        s.move(160, 270);                                        // Accept (152, 260, 80 x 24)
        s.shoot("hud.dialog.invitation.hover_accept");
        s.press(160, 270);
        s.shoot("hud.dialog.invitation.pressed_accept");
        Scene w(arc, 0);                                         // the proposer waits for the answer of team 1
        w.world.pending_invite_from[1] = 0;
        w.tick(0);
        w.shoot("hud.dialog.waiting");
        Scene b(arc, 0);                                         // the confirmation before an alliance is broken (an engine owns the alliance)
        b.sim.form_alliance(0, 2);
        b.hud.request_team_up(b.sim, 1);
        b.shoot("hud.dialog.breakconfirm");
        Scene r(arc, 1);                                         // an invitation that replaces the invitee's present team: string 2
        r.world.player_alliances = {255, 3, 255, 1};
        r.world.pending_invite_from[1] = 0;
        r.tick(0);
        r.shoot("hud.dialog.invitation.replaces_team");
    }

    // The top bar's buttons (the Help, Options, Quit art: resting art is part of the shell, hovering draws the small label, pressing replaces the art)
    {
        const struct { const char* name; int32_t x; } buttons[] = {{"help", 490}, {"options", 550}, {"quit", 600}};
        for (const auto& b : buttons) {
            Scene s(arc, 0);
            s.move(b.x, 18);
            s.shoot(std::string("hud.topbar.hover_") + b.name);
            s.press(b.x, 18);
            s.shoot(std::string("hud.topbar.pressed_") + b.name);
        }
    }

    // The rubber band over the map, the status line with a flash, an unknown pointer position
    {
        Scene s(arc, 0);
        s.press(120, 100);
        s.move(330, 260);
        s.shoot("hud.marquee.band");
        Scene t(arc, 0);
        t.press(300, 300);                                       // a stationary press is a 2 x 2 dot
        t.shoot("hud.marquee.dot");
        Scene u(arc, 0);
        u.press(120, 100);
        u.move(700, 700);                                        // the band is kept one pixel inside the view
        u.shoot("hud.marquee.clamped");
        Scene v(arc, 0);
        v.press(120, 100);
        v.move(2, 2);                                            // ... and one pixel inside the view's top left corner
        v.shoot("hud.marquee.clamped_low");
    }
    {
        Scene s(arc, 0);
        s.hud.post_status("Flash: the text is hidden on the odd steps", true);
        s.hud.update(s.world, 1);
        s.shoot("hud.status.flash.odd");
        s.hud.update(s.world, 1);
        s.shoot("hud.status.flash.even");
        s.hud.post_status("A status text that is much too long to fit into the status box of 139 pixels width");
        s.shoot("hud.status.long");
    }
}

namespace {

void click(MapSelectScreen& screen, int32_t x, int32_t y) {
    screen.handle_mouse_motion(x, y);
    screen.handle_mouse_down(x, y, 1);
    screen.handle_mouse_up(x, y, 1);
}

}  // namespace

void screen_scenarios(const assets::AssetArchive& arc) {
    using MS = MapSelectScreen;

    // The setup screen of a local game
    {
        MS screen = make_setup_screen();
        frame("screen.setup.before_refresh", [&](FrameRecorder& r) { screen.render(r, arc); });                  // the labels, portraits and thumbs appear 500 ms after the screen is created
        screen.update(0.5f);
        frame("screen.setup.default", [&](FrameRecorder& r) { screen.render(r, arc); });                         // a fresh screen: TREASURE.LVL is highlighted (the one deliberate deviation: the original highlights the first entry)
        screen.set_selected_index(0);                                                                            // the series below walks the list from its first entry, as it always did
        frame("screen.setup.map0", [&](FrameRecorder& r) { screen.render(r, arc); });
        for (size_t i = 1; i < screen.get_maps().size(); ++i) {                                                  // every map of the list: its name, description, size and minutes
            screen.handle_key_down(SDLK_DOWN);
            frame("screen.setup.map" + std::to_string(i), [&](FrameRecorder& r) { screen.render(r, arc); });
        }
        screen.set_selected_index(2);
        click(screen, MS::BTN_FOW_ON_X + 5, MS::BTN_FOW_ON_Y + 5);
        frame("screen.setup.fog_on", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_motion(MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        frame("screen.setup.hover_start", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_down(MS::BTN_START_X + 5, MS::BTN_START_Y + 5, 1);
        frame("screen.setup.pressed_start", [&](FrameRecorder& r) { screen.render(r, arc); });
    }
    {
        MS screen = make_setup_screen();
        screen.update(0.5f);
        screen.handle_mouse_down(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        frame("screen.setup.pressed_up", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_up(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        screen.handle_mouse_down(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
        frame("screen.setup.pressed_down", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.handle_mouse_up(MS::BTN_DOWN_X + 3, MS::BTN_DOWN_Y + 3, 1);
        screen.handle_mouse_motion(MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        frame("screen.setup.hover_leave", [&](FrameRecorder& r) { screen.render(r, arc); });
        screen.lock();
        frame("screen.setup.locked", [&](FrameRecorder& r) { screen.render(r, arc); });
    }
    {
        MS screen = make_setup_screen();                                                                         // another player's colour and name
        screen.set_player_name("Alice");
        screen.set_player_team(2);
        screen.update(0.5f);
        frame("screen.setup.team2_alice", [&](FrameRecorder& r) { screen.render(r, arc); });
        MS notice = make_setup_screen();                                                                         // a refusal on the status line of the local screen
        MS::RoomView local;
        local.status = "The host left the room.";
        notice.set_room(local);
        notice.update(0.5f);
        frame("screen.setup.notice", [&](FrameRecorder& r) { notice.render(r, arc); });
    }

    // The network room (a host with seats and thumbs) and the guest screen
    {
        MS screen = make_setup_screen();
        screen.update(0.5f);
        MS::RoomView view;
        view.networked = true;
        view.is_host = true;
        view.my_seat = 0;
        view.seats[0] = {true, "Alice", MS::Thumb::Good};
        view.seats[1] = {true, "Bob", MS::Thumb::Ok};
        view.seats[3] = {true, "A Very Long Player Name", MS::Thumb::Unknown};                                   // seat 2 is empty
        view.status = "Press START when all players' thumbs have appeared.";
        screen.set_room(view);
        frame("screen.room.host", [&](FrameRecorder& r) { screen.render(r, arc); });
        const struct { MS::Thumb thumb; const char* name; } qualities[] = {{MS::Thumb::Good, "good"}, {MS::Thumb::Ok, "ok"}, {MS::Thumb::Bad, "bad"}, {MS::Thumb::Unknown, "unknown"}};
        for (const auto& q : qualities) {
            MS::RoomView v = view;
            v.seats[1].thumb = q.thumb;
            screen.set_room(v);
            frame(std::string("screen.room.host.thumb_") + q.name, [&](FrameRecorder& r) { screen.render(r, arc); });
        }
        MS::RoomView full = view;
        full.seats[2] = {true, "Dave", MS::Thumb::Bad};
        full.seats[3].name = "Eve";
        full.status = "Waiting for Dave: the connection is bad.";
        screen.set_room(full);
        frame("screen.room.host.four_players", [&](FrameRecorder& r) { screen.render(r, arc); });

        MS::RoomView guest = view;                                                                               // the guest's screen (nh_start): no Up / Down / START / Fog buttons
        guest.is_host = false;
        guest.my_seat = 1;
        guest.map_file = screen.get_maps().empty() ? std::string() : screen.get_maps()[1].filename;
        MS guest_screen = make_setup_screen();
        guest_screen.update(0.5f);
        guest_screen.set_room(guest);
        guest_screen.follow_host_choice(guest.map_file, true);
        frame("screen.room.guest", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        guest_screen.follow_host_choice(guest.map_file, false);
        frame("screen.room.guest.fog_off", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        guest_screen.handle_mouse_motion(MS::BTN_QUIT_X + 5, MS::BTN_QUIT_Y + 5);
        frame("screen.room.guest.hover_leave", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        MS::RoomView lost = guest;                                                                               // the guest after the host's connection is lost: the notice only
        lost.seats = {};
        lost.status = "The connection to the host was lost.";
        guest_screen.set_room(lost);
        frame("screen.room.guest.connection_lost", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
        guest_screen.set_room(guest);
        guest_screen.update(0.0f);
        MS::RoomView server = guest;                                                                             // a room on a dedicated server: everybody is a guest, seat 3
        server.my_seat = 3;
        server.status = "Waiting for the host to start the game.";
        guest_screen.set_room(server);
        frame("screen.room.guest.server_seat3", [&](FrameRecorder& r) { guest_screen.render(r, arc); });
    }

    // The 16:9 setup screen of a room with its chat box (online rooms, protocol 11): the draw calls of the leader's screen (a server's room: the Online variant, the fill footer "Empty seats at START:" /
    // "Medium bots") and of a guest's (the Guest variant) with the lines of a conversation (a notice among them, in the status text's size), the typed line and the caret on and off, and with
    // an empty box; the text's places, sizes and colours are what these pin (the pixel fingerprints below mask the TrueType text). The map list holds the six original maps; ISLANDS is the room's.
    {
        const auto room_screen = [&](bool leader) {
            MS screen = make_setup_screen();
            screen.set_wide_layout(true);
            screen.update(0.5f);
            MS::RoomView view;
            view.networked = true;
            view.is_host = false;
            view.leader = leader;
            view.my_seat = leader ? 0 : 1;
            view.seats[0] = {true, leader ? "Ana" : "Lea", MS::Thumb::Good};
            view.seats[1] = {true, leader ? "Ben" : "Ben", leader ? MS::Thumb::Ok : MS::Thumb::Good};
            view.map_file = "ISLANDS.LVL";
            view.status = leader ? "Press START: the empty seats get Medium bots." : "Waiting for the host to start the game...";
            screen.set_room(view);
            return screen;
        };
        const auto conversation = [](bool typed, bool caret) {
            MS::ChatPanel panel;
            panel.visible = true;
            panel.lines = {{"Fog of War is on, so START seats no bots.", true}, {"Ben: hi, which map?", false}, {"Ana: TINY first, then ISLANDS", false}, {"Ben: ok, ready when you are", false}};
            if (typed) panel.typed = "ready in a minute";
            panel.caret = caret;
            return panel;
        };
        MS leader = room_screen(true);
        MS::ChatPanel empty_box;
        empty_box.visible = true;
        leader.set_chat_panel(empty_box);
        leader.set_fill_footer("Empty seats at START:", "Medium bots");
        frame("screen.wide.room.leader_empty", [&](FrameRecorder& r) { leader.render(r, arc); });
        leader.set_chat_panel(conversation(false, false));
        frame("screen.wide.room.leader_lines", [&](FrameRecorder& r) { leader.render(r, arc); });
        leader.set_chat_panel(conversation(true, true));
        frame("screen.wide.room.leader_typed_caret", [&](FrameRecorder& r) { leader.render(r, arc); });
        leader.set_chat_panel(conversation(true, false));
        frame("screen.wide.room.leader_typed", [&](FrameRecorder& r) { leader.render(r, arc); });
        MS guest = room_screen(false);
        guest.set_chat_panel(empty_box);
        frame("screen.wide.room.guest_empty", [&](FrameRecorder& r) { guest.render(r, arc); });
        guest.set_chat_panel(conversation(true, true));
        guest.set_fill_footer("Empty seats at START:", "Medium bots");                                        // (a guest has no such choice: nothing is drawn for it)
        frame("screen.wide.room.guest_typed_caret", [&](FrameRecorder& r) { guest.render(r, arc); });
    }
    {
        MS fresh = make_setup_screen();                                                                          // a room before the refresh: labels, portraits and thumbs are not shown yet
        MS::RoomView view;
        view.networked = true;
        view.is_host = true;
        view.seats[0] = {true, "Host", MS::Thumb::Good};
        fresh.set_room(view);
        frame("screen.room.before_refresh", [&](FrameRecorder& r) { fresh.render(r, arc); });
    }

    // The results screen: the waiting phase, the rows of 2 and of 4 teams (an alliance in one row), the Leave button, the animated portraits
    auto make_result = [](uint8_t present, bool allied) {
        sim::MatchResult result;
        result.is_over = true;
        result.present_mask = present;
        if (allied) result.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
        result.stats[0].score = 300;  result.stats[0].friendly_lost = 1;  result.stats[0].enemy_killed = 2;  result.stats[0].new_hatched = 3;
        result.stats[1].score = 200;  result.stats[1].friendly_lost = 10; result.stats[1].enemy_killed = 20; result.stats[1].new_hatched = 30;
        result.stats[2].score = 450;  result.stats[2].friendly_lost = 4;  result.stats[2].enemy_killed = 5;  result.stats[2].new_hatched = 6;
        result.stats[3].score = 100;  result.stats[3].friendly_lost = 1234; result.stats[3].enemy_killed = 0; result.stats[3].new_hatched = 0;
        result.decide_winners();
        return result;
    };
    {
        ScorecardModal card;                                                                                     // two teams
        card.set_player_names({"Alice", "Bob", "", ""});
        card.set_shown_teams(0x03);
        card.show(make_result(0x03, false), 0);
        frame("screen.results.2teams.waiting", [&](FrameRecorder& r) { card.render(r, arc); });
        card.update(0.25f);
        frame("screen.results.2teams.rows", [&](FrameRecorder& r) { card.render(r, arc); });
        card.update(0.6f);
        frame("screen.results.2teams.rows.later", [&](FrameRecorder& r) { card.render(r, arc); });
        card.handle_mouse_motion(550, 20);                                                                       // the Leave button (525, 12, 99 x 22)
        frame("screen.results.2teams.hover_leave", [&](FrameRecorder& r) { card.render(r, arc); });
        card.handle_mouse_down(550, 20);
        frame("screen.results.2teams.pressed_leave", [&](FrameRecorder& r) { card.render(r, arc); });
    }
    {
        ScorecardModal card;                                                                                     // four teams, 0 and 1 allied, the local team is the winner's ally
        card.set_player_names({"Alice", "Bob", "Carol", "Dave"});
        card.show(make_result(0x0F, true), 0);
        frame("screen.results.4teams.waiting", [&](FrameRecorder& r) { card.render(r, arc); });
        card.update(0.25f);
        frame("screen.results.4teams.rows", [&](FrameRecorder& r) { card.render(r, arc); });
        ScorecardModal loser;                                                                                    // the same for team 3 (the loser's cue and order)
        loser.set_player_names({"Alice", "Bob", "Carol", "Dave"});
        loser.show(make_result(0x0F, true), 3);
        loser.update(0.4f);
        frame("screen.results.4teams.rows.local3", [&](FrameRecorder& r) { loser.render(r, arc); });
        ScorecardModal plain;                                                                                    // four teams without alliance, default names, a 35 character name
        plain.set_player_names({"A Name Of Exactly Thirty-Five Chars", "", "", ""});
        sim::MatchResult res = make_result(0x0F, false);
        res.quitter = 1;
        res.decide_winners();
        plain.show(res, 2);
        plain.update(0.9f);
        frame("screen.results.4teams.quitter", [&](FrameRecorder& r) { plain.render(r, arc); });
        ScorecardModal three;                                                                                    // a team that is not in the match has no row
        three.set_player_names({"", "", "", ""});
        three.show(make_result(0x0D, false), 0);
        three.update(0.3f);
        frame("screen.results.3teams.gap", [&](FrameRecorder& r) { three.render(r, arc); });
    }
}

// The desktop start menu (include/ants_app/start_menu.hpp; remake only, so these fingerprints are new: none of the classic screens above changes): every panel, a selection, the
// bots, the fields, a failure, the room's code
void menu_scenarios(const assets::AssetArchive& arc) {
    const auto menu_frame = [&](const std::string& name, const StartMenu& menu) { frame(name, [&](FrameRecorder& r) { render_start_menu(r, arc, menu); }); };
    const auto make = [](uint8_t own_seat) {
        StartMenu menu;
        MenuSettings settings;
        settings.name = "Player";
        menu.set_own_seat(own_seat);
        menu.set_settings(settings);
        menu.set_server(ServerAddress{});
        menu.show_main();
        return menu;
    };
    // (a key as a person presses it: the panel has been up for longer than StartMenu::kSettleMs, within which Enter and Space do nothing)
    const auto key = [](StartMenu& m, SDL_Keycode k) {
        if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE) m.update(static_cast<float>(StartMenu::kSettleMs + 10) / 1000.0f);
        m.on_key(k, 0, false);
    };
    {
        StartMenu m = make(0);
        menu_frame("screen.menu.main", m);
        key(m, SDLK_DOWN);
        menu_frame("screen.menu.main.second_selected", m);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        menu_frame("screen.menu.main.quit_selected", m);
        m.show_main("The connection to the other players was lost.");
        menu_frame("screen.menu.main.notice", m);
    }
    {
        StartMenu m = make(0);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.single.empty", m);
        key(m, SDLK_DOWN);                                                                                       // the first row is selected on arrival: Black, then Blue
        key(m, SDLK_DOWN);
        key(m, SDLK_RIGHT);
        key(m, SDLK_UP);
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);
        menu_frame("screen.menu.single.two_bots", m);
        StartMenu own = make(2);
        key(own, SDLK_RETURN);
        key(own, SDLK_DOWN);
        key(own, SDLK_DOWN);
        key(own, SDLK_RIGHT);
        key(own, SDLK_RIGHT);
        key(own, SDLK_RIGHT);
        menu_frame("screen.menu.single.own_seat_2", own);
    }
    {
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.join.empty", m);
        m.on_text("k7m2xq");
        menu_frame("screen.menu.join.typed", m);
        m.update(0.15f);
        menu_frame("screen.menu.join.caret_off", m);
        key(m, SDLK_UP);
        m.on_text("Bot (Hard)");
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.join.name_refused", m);
        m.on_text("Maya");
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.connecting", m);
        m.connection_failed("There is no room with the code k7m2xq on beta.playants.org:4001. Check the code (capital letters matter).");
        menu_frame("screen.menu.join.error", m);
    }
    {
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        menu_frame("screen.menu.host.default", m);
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);
        key(m, SDLK_DOWN);
        key(m, SDLK_RIGHT);
        menu_frame("screen.menu.host.map_and_players", m);
        m.show_room("k3n7pq", 1, 2);
        menu_frame("screen.menu.room.code", m);
        m.set_room_players(2, 2);
        menu_frame("screen.menu.room.full", m);
        m.show_room(std::string(32, 'W'), 1, 4);
        menu_frame("screen.menu.room.longest_code", m);
    }
    {   // the bots of the Host panel's seat rows (network protocol 13: a level for each seat): Medium in all three seats after the leader's, and what the room's panel says START will do (the
        // sentence of protocol 11: this room panel is the one that it was)
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);                                                                                       // map, players, then the Red seat
        for (int seat = 0; seat < 3; ++seat) {
            key(m, SDLK_RIGHT);
            key(m, SDLK_RIGHT);                                                                                  // Medium bot
            key(m, SDLK_DOWN);                                                                                   // the next seat
        }
        menu_frame("screen.menu.host.fill_medium", m);
        m.show_room("b7x2qk", 1, 4);
        menu_frame("screen.menu.room.fill_medium", m);
    }
    {   // a level for each seat and the teams (protocol 13): Easy in the Red seat, nobody in the Blue seat, Hard in the Black seat, Green and Red against Blue and Black; the room's panel says both
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);                                                                                       // map, players, then the Red seat
        key(m, SDLK_RIGHT);                                                                                      // Easy bot
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);                                                                                       // (the Blue seat stays empty) the Black seat
        for (int i = 0; i < 3; ++i) key(m, SDLK_RIGHT);                                                          // Hard bot
        key(m, SDLK_DOWN);                                                                                       // the teams
        key(m, SDLK_RIGHT);                                                                                      // Green + Red against Blue + Black
        menu_frame("screen.menu.host.seats_and_teams", m);
        m.show_room("b7x2qk", 1, 4);
        menu_frame("screen.menu.room.seats_and_teams", m);
    }
    {   // a room of three players: two seat rows and the teams of three (Red + Blue against Green); the room of two (one seat row, no teams) is screen.menu.host.map_and_players
        StartMenu m = make(0);
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);
        key(m, SDLK_RETURN);
        key(m, SDLK_DOWN);                                                                                       // the players
        key(m, SDLK_LEFT);                                                                                       // 3 players
        key(m, SDLK_DOWN);
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);
        key(m, SDLK_RIGHT);                                                                                      // the Red seat: Hard bot
        key(m, SDLK_DOWN);
        key(m, SDLK_DOWN);                                                                                       // (the Blue seat stays empty) the teams
        for (int i = 0; i < 3; ++i) key(m, SDLK_RIGHT);                                                          // Red + Blue against Green
        menu_frame("screen.menu.host.three_players", m);
        m.show_room("b7x2q3", 1, 3);
        menu_frame("screen.menu.room.three_players", m);
    }
}

namespace {

const char* const kParamNames[kParamCount] = {
    "hud team", "sprite id", "sprite x", "sprite y", "sprite mirrored", "named sprite name", "named x", "named y", "named mirrored",
    "fill x", "fill y", "fill w", "fill h", "fill r", "fill g", "fill b", "fill a",
    "frame x", "frame y", "frame w", "frame h", "frame r", "frame g", "frame b", "frame a",
    "text string", "text x", "text y", "text size", "text r", "text g", "text b", "text a",
    "short text x", "short text y",
    "clip x", "clip y", "clip w", "clip h",
    "image x", "image y", "image w", "image h", "image byte",
    "second sprite id", "second sprite x", "second sprite y"};

Params base_params() {
    return Params{2,   2721, 10,  20, 0,  0,  30, 40, 0,            // team, sprite, named sprite
                  50,  60,   70,  14, 7,  67, 47, 255,              // fill
                  80,  90,   100, 20, 251, 251, 255, 255,           // frame
                  0,   110,  120, 14, 255, 255, 255, 255,           // text
                  130, 140,                                         // the 4 argument text
                  482, 299,  138, 101,                              // clip
                  480, 35,   2,   2,  17,                           // image
                  2722, 150, 160};                                  // a sprite inside the clip
}

}  // namespace

void self_check_scenarios() {
    // the recorder: the same calls give the same hash; one parameter of one call changed by one gives another; a swap and a dropped call as well
    const Params base = base_params();
    const Measure reference = replayed(base);
    check(reference.count == 11, "the reference frame has 11 calls (it has " + std::to_string(reference.count) + ")");
    check(replayed(base).hash == reference.hash, "sensitivity: the same calls give the same hash");
    for (size_t i = 0; i < kParamCount; ++i) {
        Params changed = base;
        changed[i] += 1;
        const Measure m = replayed(changed);
        check(m.hash != reference.hash, std::string("sensitivity: the hash changes when '") + kParamNames[i] + "' changes by one");
        check(m.count == reference.count, std::string("sensitivity: '") + kParamNames[i] + "' does not change the number of calls");
    }
    check(replayed(base, true, false).hash != reference.hash, "sensitivity: the hash changes when two draw calls are swapped");
    {
        const Measure dropped = replayed(base, false, true);
        check(dropped.hash != reference.hash && dropped.count + 1 == reference.count, "sensitivity: the hash and the count change when one call is dropped");
    }
    check(FrameRecorder{}.result().count == 0, "sensitivity: an empty frame has no calls");
    {
        FrameRecorder a;                                                               // the 4 argument text is the 12 px text (one operation, as the real renderer treats it)
        FrameRecorder b;
        a.draw_text("t", 1, 2, assets::ColorRGBA{9, 9, 9, 255});
        b.draw_text("t", 1, 2, assets::ColorRGBA{9, 9, 9, 255}, FontSize::Px12);
        check(a.result().hash == b.result().hash, "the 4 argument text and the 12 px text are the same call");
        FrameRecorder c;
        c.draw_text("t", 1, 2, assets::ColorRGBA{9, 9, 9, 255}, FontSize::Px14);
        check(a.result().hash != c.result().hash, "sensitivity: the font size is part of a text call");
    }

    // a pixel hash: one pixel changes it, a masked pixel does not
    {
        Canvas a;
        Canvas b;
        for (size_t i = 0; i < a.px.size(); ++i) a.px[i] = b.px[i] = static_cast<uint8_t>(i * 7u);
        const std::vector<MaskRect> mask = {{10, 10, 20, 20}};
        const Measure ma = canvas_measure(a, mask);
        b.px[(static_cast<size_t>(15) * kW + 15u) * 4u + 1u] ^= 0x40;                       // inside the mask
        check(canvas_measure(b, mask).hash == ma.hash, "a pixel inside a masked rectangle does not change the hash");
        b.px[(static_cast<size_t>(9) * kW + 15u) * 4u + 2u] ^= 0x01;                        // one pixel above the mask
        check(canvas_measure(b, mask).hash != ma.hash, "sensitivity: one bit of one pixel outside the masks changes the hash");
    }

    // a pointer hash: a rate, a camera and a pixel's classification each change it
    {
        const Measure base_edge = edge_sweep(60, 60, 50, {Cam{"c", 400, 400}});
        check(edge_sweep(60, 60, 50, {Cam{"c", 400, 400}}).hash == base_edge.hash, "sensitivity: the same sweep gives the same hash");
        check(edge_sweep(60, 60, 51, {Cam{"c", 400, 400}}).hash != base_edge.hash, "sensitivity: the scroll rate changes the edge hash");
        check(edge_sweep(60, 60, 50, {Cam{"c", 0, 400}}).hash != base_edge.hash, "sensitivity: a camera at the west border changes the edge hash");
        check(edge_sweep(60, 60, 50, {Cam{"c", 0, 0}}).hash != base_edge.hash, "sensitivity: the camera at the corner changes the edge hash");
        const Measure rect = sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_map_rect(x, y)); });
        const Measure moved = sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_map_rect(x - 1, y)); });
        check(rect.hash != moved.hash, "sensitivity: a rectangle that moves by one pixel changes the classification hash");
    }
}

/// The wide family's own recorder calls (milestone M3): a part of a sprite and the origin of a window are calls with every parameter in the hash; setting the origin that is set is no call
void self_check_wide_scenarios() {
    const std::array<int32_t, 9> base{7, 100, 200, 50, 60, 3, 4, 1, 9};              // sprite id, x, y, w, h, sx, sy, sw, sh
    auto region_hash = [](const std::array<int32_t, 9>& p) {
        FrameRecorder rec;
        rec.draw_sprite_region(static_cast<uint32_t>(p[0]), p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
        return rec.result();
    };
    const Measure reference = region_hash(base);
    check(reference.count == 1 && region_hash(base).hash == reference.hash, "wide self-check: a region is one call, the same call gives the same hash");
    const char* names[9] = {"sprite id", "x", "y", "w", "h", "sx", "sy", "sw", "sh"};
    for (size_t i = 0; i < base.size(); ++i) {
        std::array<int32_t, 9> changed = base;
        changed[i] += 1;
        check(region_hash(changed).hash != reference.hash, std::string("wide self-check: the hash changes when the region's '") + names[i] + "' changes by one");
    }
    {
        FrameRecorder a, b, c, d;
        a.set_origin(137, 59);
        b.set_origin(137, 60);
        c.set_origin(138, 59);
        d.set_origin(137, 59);
        check(a.result().hash == d.result().hash && a.result().hash != b.result().hash && a.result().hash != c.result().hash && a.calls() == 1, "wide self-check: an origin is one call and both numbers are in the hash");
        FrameRecorder none;
        none.set_origin(0, 0);
        none.set_origin(0, 0);
        check(none.calls() == 0 && none.result().hash == FrameRecorder{}.result().hash, "wide self-check: the origin (0, 0) that is set already is no call (the classic scenes set it around their dialogs)");
        FrameRecorder moved;
        moved.set_origin(5, 6);
        moved.set_origin(5, 6);
        moved.set_origin(0, 0);
        check(moved.calls() == 2, "wide self-check: a change of the origin is a call, the same again is none, back to (0, 0) is one");
    }
    // the pointer and pixel families of the wide picture move with their inputs
    {
        const Measure a = wide_edge_sweep(60, 60, 50, {Cam{"c", 400, 400}});
        check(wide_edge_sweep(60, 60, 50, {Cam{"c", 400, 400}}).hash == a.hash, "wide self-check: the same sweep gives the same hash");
        check(wide_edge_sweep(60, 60, 51, {Cam{"c", 400, 400}}).hash != a.hash, "wide self-check: the scroll rate changes the wide edge hash");
        check(wide_edge_sweep(60, 60, 50, {Cam{"c", 0, 400}}).hash != a.hash, "wide self-check: a camera at the west border changes the wide edge hash");
        const Measure plain = sweep_wide([](Fnv64& h, int32_t x, int32_t y) { h.flag(kWide.view().contains(x, y)); });
        const Measure moved = sweep_wide([](Fnv64& h, int32_t x, int32_t y) { h.flag(kWide.view().contains(x - 1, y)); });
        check(plain.hash != moved.hash && plain.count == kWPixels, "wide self-check: a rectangle that moves by one pixel changes the wide classification hash");
    }
}

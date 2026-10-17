// Tests of the desktop start menu: the first panel, the mouse, the single-player panel, the join panel and the host panel up to its seats (M1.1 - M4.3)
#include "start_menu_fixtures.hpp"

void run_start_menu_panels_tests() {
    TEST_CASE("M1.1 First panel: the entries in their order, the first selected; Up and Down wrap round, Tab and Shift+Tab do the same; Esc is Quit and asks nothing else") {
        Rig r;
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        const std::vector<MenuId> expected = {MenuId::Single, MenuId::JoinWithCode, MenuId::HostOnline, MenuId::Quit};
        ASSERT_TRUE(control_ids(r.menu) == expected);
        ASSERT_EQ(element_text(r.menu.elements(), MenuKind::Title), std::string("Welcome to Ants!"));
        ASSERT_EQ(r.element(MenuId::Single).text, std::string("Single player"));
        ASSERT_EQ(r.element(MenuId::JoinWithCode).text, std::string("Join with a code"));
        ASSERT_EQ(r.element(MenuId::HostOnline).text, std::string("Host an online match"));
        ASSERT_EQ(r.element(MenuId::Quit).text, std::string("Quit"));
        ASSERT_EQ(r.menu.selected(), MenuId::Single);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::JoinWithCode);
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);                                      // wraps to the top
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);                                        // and to the bottom
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);
        r.key(SDLK_TAB, KMOD_SHIFT);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);
        ASSERT_FALSE(r.menu.has_request());                                                // moving asks for nothing
        r.key(SDLK_ESCAPE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);                             // Esc on the first panel: Quit, no question
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
    } TEST_END();

    TEST_CASE("M1.2 First panel: Enter on an entry opens its panel (Quit asks to quit); a key that is held (repeat) does not act twice; Space acts like Enter on a button") {
        Rig r;
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);                                      // the entry that was left is the selection again
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.selected(), MenuId::JoinWithCode);
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.selected(), MenuId::HostOnline);
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN, 0, true);                                                       // a held Enter: ignored
        ASSERT_FALSE(r.menu.has_request());
        r.key(SDLK_SPACE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
        r.key(SDLK_ESCAPE, 0, true);                                                       // a held Esc: ignored
        ASSERT_FALSE(r.menu.has_request());
        r.key(SDLK_KP_ENTER);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
    } TEST_END();

    TEST_CASE("M1.3 Mouse: the pointer selects a button it is over; a click is a press and a release on the same button (the click sound at the press); leaving the button, another button at the release and the right button do nothing") {
        Rig r;
        r.mouse_move(MenuId::HostOnline);
        ASSERT_EQ(r.menu.selected(), MenuId::HostOnline);
        r.menu.on_mouse_move(5, 5);                                                        // over nothing: the selection stays
        ASSERT_EQ(r.menu.selected(), MenuId::HostOnline);
        const MenuElement e = r.element(MenuId::JoinWithCode);
        const int32_t x = e.rect.x + 4;
        const int32_t y = e.rect.y + 4;
        r.menu.on_mouse_move(x, y);
        ASSERT_TRUE(r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT));
        ASSERT_EQ(r.sounds.count(sim::SoundID::ButtonClick), 1);                           // the click of the pressed button
        ASSERT_TRUE(r.element(MenuId::JoinWithCode).pressed);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);                                        // nothing happens at the press
        r.menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);                                        // the action comes with the release
        r.menu.show_main();
        // pressed, then the pointer leaves the button: no action at the release even when it comes back
        r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
        r.menu.on_mouse_move(5, 5);
        ASSERT_FALSE(r.element(MenuId::JoinWithCode).pressed);
        r.menu.on_mouse_move(x, y);
        r.menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        // pressed on one button, released on another: nothing
        r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
        const MenuElement q = r.element(MenuId::Quit);
        r.menu.on_mouse_up(q.rect.x + 2, q.rect.y + 2, SDL_BUTTON_LEFT);
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        // the pointer over a field does not take the focus from the field that has it (typing would go to the wrong one); a click does
        r.menu.show_main();
        r.click(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        r.mouse_move(MenuId::Name);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        r.mouse_move(MenuId::Join);
        ASSERT_EQ(r.menu.selected(), MenuId::Join);                                        // (a button is selected by the pointer)
        r.menu.show_main();
        r.menu.on_mouse_move(5, 5);
        // the right button never presses
        ASSERT_FALSE(r.menu.on_mouse_down(x, y, SDL_BUTTON_RIGHT));
        r.menu.on_mouse_up(x, y, SDL_BUTTON_RIGHT);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        // a click on the background does nothing, a click on Quit asks for it
        ASSERT_FALSE(r.menu.on_mouse_down(3, 3, SDL_BUTTON_LEFT));
        r.click(MenuId::Quit);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
    } TEST_END();

    TEST_CASE("M2.1 Single player: one row per other seat in the color's own words (own seat green: Red, Blue, Black; own seat blue: Green, Red, Black), every row Empty, no fog line, Continue is the selection and asks for no bots") {
        for (uint8_t own = 0; own < 4; ++own) {
            Rig r(own);
            r.to_panel(MenuId::Single);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
            const std::vector<MenuElement> all = r.menu.elements();
            size_t rows = 0;
            std::vector<std::string> colors;
            for (const MenuElement& e : all) {
                if (e.kind == MenuKind::Cycler) {
                    ++rows;
                    ASSERT_EQ(e.value, std::string("Empty"));
                }
                if (e.kind == MenuKind::Portrait) colors.push_back(std::to_string(e.team));
            }
            ASSERT_EQ(rows, static_cast<size_t>(3));
            ASSERT_EQ(colors.size(), static_cast<size_t>(3));
            // the seat of the player has no row, the other three do (their portraits are in their colors)
            std::vector<std::string> want;
            for (uint8_t s = 0; s < 4; ++s) {
                if (s != own) want.push_back(std::to_string(s));
            }
            ASSERT_TRUE(colors == want);
            ASSERT_FALSE(r.exists(static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + own)));
            ASSERT_FALSE(has_text(all, "fog of war"));
            ASSERT_TRUE(has_text(all, StartMenu::kNoBotsLine));                            // nobody seated: the panel says that this is the original's game (L7)
            const uint8_t first_other = own == 0 ? uint8_t{1} : uint8_t{0};
            ASSERT_EQ(r.menu.selected(), static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + first_other));   // the first INPUT, never a button that acts (M2)
            ASSERT_TRUE(r.menu.bots().empty());
        }
        Rig r0(0);
        r0.to_panel(MenuId::Single);
        ASSERT_TRUE(has_text(r0.menu.elements(), "Red") && has_text(r0.menu.elements(), "Blue") && has_text(r0.menu.elements(), "Black") && !has_text(r0.menu.elements(), "Green ants"));
        ASSERT_TRUE(has_text(r0.menu.elements(), "You play the green ants."));
        Rig r2(2);
        r2.to_panel(MenuId::Single);
        ASSERT_TRUE(has_text(r2.menu.elements(), "Green") && has_text(r2.menu.elements(), "Red") && has_text(r2.menu.elements(), "Black"));
        ASSERT_TRUE(has_text(r2.menu.elements(), "You play the blue ants."));
        r0.click(MenuId::Continue);                                                        // Continue with nobody: the original's single-player game
        const MenuRequest request = r0.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.empty());
    } TEST_END();

    TEST_CASE("M2.2 Single player: a row cycles Empty, Easy, Medium, Hard and round by click, Enter and Right (Left goes back); the bots are the standard bot of the level at their seat as --bot SEAT:LEVEL would give them; the rule-8 line shows while any seat has a bot") {
        Rig r;
        r.to_panel(MenuId::Single);
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);                                                                  // the last row (Black, seat 3): the first row is selected on arrival
        ASSERT_EQ(r.menu.selected(), MenuId::Seat3);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Empty"));
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Easy bot"));
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Medium bot"));
        r.click(MenuId::Seat3);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Hard bot"));
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Empty"));                   // round
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Hard bot"));                // and back round
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Medium bot"));
        ASSERT_TRUE(has_text(r.menu.elements(), "Bots play without fog of war."));         // rule 5: a bot never sees through the fog
        r.click(MenuId::Seat1);                                                            // Red: Easy
        const std::vector<ai::BotSpec> bots = r.menu.bots();
        ASSERT_EQ(bots.size(), static_cast<size_t>(2));
        ASSERT_TRUE(bots[0].seat == 1 && bots[0].kind == "standard" && bots[0].level == ai::Level::Easy);
        ASSERT_TRUE(bots[1].seat == 3 && bots[1].kind == "standard" && bots[1].level == ai::Level::Medium);
        ai::BotSpec parsed;
        std::string why;
        ASSERT_TRUE(ai::parse_bot_spec("1:easy", parsed, why) && parsed.seat == bots[0].seat && parsed.kind == bots[0].kind && parsed.level == bots[0].level);
        ASSERT_TRUE(ai::parse_bot_spec("3:medium", parsed, why) && parsed.seat == bots[1].seat && parsed.kind == bots[1].kind && parsed.level == bots[1].level);
        ASSERT_EQ(ai::bot_display_name(bots[1]), std::string("Bot (Medium)"));
        r.click(MenuId::Continue);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.size() == 2 && request.bots[0].seat == 1 && request.bots[1].seat == 3);
        // empty again: the line goes
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat3);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Hard bot"));
        r.click(MenuId::Seat3);
        ASSERT_TRUE(r.menu.bots().empty());
        ASSERT_FALSE(has_text(r.menu.elements(), "fog of war"));
        // every change is reported (the owner writes it to the settings file), a move of the selection is not
        const size_t before = r.changes.size();
        r.key(SDLK_UP);
        ASSERT_EQ(r.changes.size(), before);
        r.click(MenuId::Seat1);
        ASSERT_TRUE(r.changes.size() == before + 1 && r.changes.back() == MenuSetting::Bots);
        // Back (and Esc) return to the first panel with the choice kept
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        r.click(MenuId::Single);
        ASSERT_EQ(r.element(MenuId::Seat1).value, std::string("Easy bot"));
    } TEST_END();

    TEST_CASE("M2.3 Single player: a bot never sits at the player's own seat, whatever the seat is (the choice stored for that seat is not used)") {
        for (uint8_t own = 0; own < 4; ++own) {
            Rig r(own);
            MenuSettings s;
            s.name = "Player";
            for (auto& seat : s.seats) seat = SeatChoice::Hard;                            // a stored choice for every seat
            r.menu.set_settings(s);
            const std::vector<ai::BotSpec> bots = r.menu.bots();
            ASSERT_EQ(bots.size(), static_cast<size_t>(3));
            for (const ai::BotSpec& b : bots) ASSERT_TRUE(b.seat != own && b.seat < 4 && b.level == ai::Level::Hard);
        }
    } TEST_END();

    TEST_CASE("M2.4 Single player: a Teams choice appears under the seats once two or more of them have a bot (with fewer there is nothing to choose: the panel is the one that it always was), cycles Free for all, You + each bot and round by click, Enter and Left / Right, stands between the seats and Continue in the Tab order, and Continue asks for the teams as --teams would give them") {
        Rig r;
        r.to_panel(MenuId::Single);
        ASSERT_FALSE(r.exists(MenuId::Teams));                                             // nobody seated
        const ButtonRect continue_none = r.element(MenuId::Continue).rect;
        r.click(MenuId::Seat1);                                                            // Red: Easy
        ASSERT_FALSE(r.exists(MenuId::Teams));                                             // one bot: a team would be the whole match (it would end at the first point), nothing to choose
        ASSERT_TRUE(r.element(MenuId::Continue).rect.y == continue_none.y && r.element(MenuId::Continue).rect.h == continue_none.h);
        ASSERT_TRUE(r.menu.teams() == LocalTeams{});
        ASSERT_TRUE(r.menu.team_choices().size() == 1);
        r.click(MenuId::Seat3);                                                            // Black: Easy: two bots
        ASSERT_TRUE(r.exists(MenuId::Teams));
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));
        ASSERT_TRUE(r.element(MenuId::Teams).kind == MenuKind::Cycler);
        ASSERT_TRUE(has_text(r.menu.elements(), "Teams") && has_text(r.menu.elements(), StartMenu::kBotsLine));
        // the Tab order: the name, the rows, Teams, Continue, Back
        const std::vector<MenuId> ids = control_ids(r.menu);
        ASSERT_TRUE((ids == std::vector<MenuId>{MenuId::SingleName, MenuId::Seat1, MenuId::Seat2, MenuId::Seat3, MenuId::Teams, MenuId::Continue, MenuId::Back}));
        // the controls lie inside the panel, clear of each other, and under the seats
        {
            const std::vector<MenuElement> all = r.menu.elements();
            for (size_t i = 0; i < all.size(); ++i) {
                ASSERT_TRUE(all[i].rect.x >= 16 && all[i].rect.y >= 16 && all[i].rect.x + all[i].rect.w <= StartMenu::kWidth - 16 && all[i].rect.y + all[i].rect.h <= StartMenu::kHeight - 16);
                for (size_t j = i + 1; j < all.size(); ++j) ASSERT_FALSE(intersects(all[i].rect, all[j].rect));
            }
            ASSERT_TRUE(r.element(MenuId::Teams).rect.y >= r.element(MenuId::Seat3).rect.y + r.element(MenuId::Seat3).rect.h);
            ASSERT_TRUE(r.element(MenuId::Continue).rect.y >= r.element(MenuId::Teams).rect.y + r.element(MenuId::Teams).rect.h);
        }
        // the cycle: by click, Enter, Right, Left
        const size_t before = r.changes.size();
        r.click(MenuId::Teams);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        ASSERT_TRUE(r.changes.size() == before + 1 && r.changes.back() == MenuSetting::Teams);
        ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 0, 1}));
        ASSERT_EQ(r.menu.selected(), MenuId::Teams);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
        ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 0, 3}));
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));            // round
        ASSERT_TRUE(r.menu.teams() == LocalTeams{});
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));             // and back round
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        r.key(SDLK_UP);                                                                    // a move of the selection is no change
        const size_t after = r.changes.size();
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.changes.size(), after);
        // Continue asks for the bots and the teams
        r.click(MenuId::Continue);
        MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.size() == 2 && request.bots[0].seat == 1 && request.bots[1].seat == 3);
        ASSERT_TRUE(request.teams == LocalTeams({true, 0, 1}));
        // the choice survives a change of the other seats that keeps its bot, and Back and the way in again
        r.click(MenuId::Seat3);                                                            // Black: Medium
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        r.key(SDLK_ESCAPE);
        r.click(MenuId::Single);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        // a third bot: one more choice
        r.click(MenuId::Seat2);
        ASSERT_TRUE(r.menu.team_choices().size() == 4);
        r.click(MenuId::Teams);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Blue"));
        r.click(MenuId::Teams);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
        // the team's bot leaves (its row goes round to Empty): free for all again, written (the file never says a team with a seat that has no bot), and with one bot left the row is gone
        r.click(MenuId::Seat3);                                                            // Black: Hard (still a bot: the choice stays)
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
        r.click(MenuId::Seat3);                                                            // Black: Empty
        ASSERT_TRUE(r.menu.settings().teams == LocalTeams{});
        ASSERT_TRUE(r.changes.size() >= 2 && r.changes.back() == MenuSetting::Teams && r.changes[r.changes.size() - 2] == MenuSetting::Bots);       // (the seat, then the team that went with it)
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));            // (Red and Blue are bots: still a choice)
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);                                                            // Blue: Empty: one bot left
        ASSERT_FALSE(r.exists(MenuId::Teams));
        r.click(MenuId::Continue);
        request = r.take();
        ASSERT_TRUE(request.bots.size() == 1 && request.teams == LocalTeams{});
    } TEST_END();

    TEST_CASE("M2.5 Single player: the Teams choice is the player's seat with a bot, whatever the seat is (own seat blue: You + Green, You + Red), a team that is stored for seats that have no bots is no choice (free for all, and the cycler goes on from there), the words of the cycler, and the store keeps the choice under `teams`") {
        {
            Rig r(2);
            r.to_panel(MenuId::Single);
            r.click(MenuId::Seat0);                                                        // Green
            r.click(MenuId::Seat3);                                                        // Black
            ASSERT_TRUE(r.exists(MenuId::Teams));
            r.click(MenuId::Teams);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Green"));
            ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 2, 0}));
            r.click(MenuId::Teams);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
            ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 2, 3}));
            r.click(MenuId::Continue);
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.teams == LocalTeams({true, 2, 3}) && request.bots.size() == 2 && request.bots[0].seat == 0 && request.bots[1].seat == 3);
        }
        ASSERT_EQ(StartMenu::teams_text(LocalTeams{}, 0), std::string("Free for all"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 0, 1}), 0), std::string("You + Red"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 0, 2}), 0), std::string("You + Blue"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 0, 3}), 0), std::string("You + Black"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 2, 0}), 2), std::string("You + Green"));
        {   // a stored team whose seat has no bot is no choice: the panel says free for all and the first click goes to the first choice (not to the one after the stored one)
            Rig r;
            MenuSettings s;
            s.name = "Player";
            s.seats = {SeatChoice::Empty, SeatChoice::Empty, SeatChoice::Hard, SeatChoice::Hard};
            s.teams = LocalTeams({true, 0, 1});
            r.menu.set_settings(s);
            ASSERT_TRUE(r.menu.teams() == LocalTeams{});
            r.to_panel(MenuId::Single);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));
            r.click(MenuId::Teams);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Blue"));
            // and the same for a team of a seat that is not the player's partner at all (not 0 + N for the player at seat 0)
            s.teams = LocalTeams({true, 2, 3});
            r.menu.set_settings(s);
            ASSERT_TRUE(r.menu.teams() == LocalTeams{});
        }
        {   // the store: written under `teams` as ffa or A+B, read back; anything else is free for all
            TempDir temp;
            {
                ConfigStore store;
                store.set_location(temp.file("settings.ini"));
                MenuSettings s;
                s.write(store, MenuSetting::Teams);
                ASSERT_EQ(store.get_string("teams", "?", 99), std::string("ffa"));
                s.teams = LocalTeams({true, 0, 3});
                s.write(store, MenuSetting::Teams);
                ASSERT_EQ(store.get_string("teams", "?", 99), std::string("0+3"));
                ASSERT_FALSE(store.has("bots") || store.has("name"));                      // only that one
            }
            ConfigStore later;
            later.set_location(temp.file("settings.ini"));
            ASSERT_TRUE(later.load());
            MenuSettings back;
            back.load(later);
            ASSERT_TRUE(back.teams == LocalTeams({true, 0, 3}));
            for (const char* junk : {"teams=\n", "teams=0+0\n", "teams=0+9\n", "teams=red\n", "teams=0+1+2\n", "teams=ffa\n", ""}) {
                ConfigStore bad;
                bad.parse(junk);
                MenuSettings d;
                d.teams = LocalTeams({true, 1, 2});                                         // (what load() does not read it must not keep)
                d.load(bad);
                ASSERT_TRUE(d.teams == LocalTeams{});
            }
            ConfigStore padded;
            padded.parse("teams= 0+2 \n");
            MenuSettings p;
            p.load(padded);
            ASSERT_TRUE(p.teams == LocalTeams({true, 0, 2}));
        }
        {   // the menu writes a change of the Teams choice at once and only that one
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            Rig r;
            r.menu.set_on_change([&](MenuSetting which) { r.menu.settings().write(store, which); });
            r.to_panel(MenuId::Single);
            r.click(MenuId::Seat1);
            r.click(MenuId::Seat2);
            ASSERT_FALSE(store.has("teams"));                                              // (a seat is `bots`)
            r.click(MenuId::Teams);
            ASSERT_EQ(store.get_string("teams", "?", 99), std::string("0+1"));
            r.click(MenuId::Seat1);
            r.click(MenuId::Seat1);
            r.click(MenuId::Seat1);                                                        // Red: Empty: the team is gone, and so is the stored word
            ASSERT_EQ(store.get_string("teams", "?", 99), std::string("ffa"));
            ASSERT_EQ(store.get_string("bots", "?", 99), std::string("off,off,easy,off"));   // (the seats by color: green is the player's, red is empty again, blue is Easy)
        }
    } TEST_END();

    TEST_CASE("M2.6 Single player: the player's name is a field of the panel, the same text as Join's and Host's (one remembered name), proposed as the settings have it, focused by the keyboard (Up from the first row: its text is selected, so typing replaces it) or by a click (nothing selected), edited like the others (printable ASCII, 32 at most), Enter in it and Continue ask for the game with the cleaned name, a name that Join would refuse is refused on the field with the same words (shown in the rule's place until the next key, with the can't-go cue, asking for nothing), and the name is written when the game is asked for") {
        TempDir temp;
        ConfigStore store;
        store.set_location(temp.file("settings.ini"));
        Rig r(0, "Maya");
        r.menu.set_on_change([&](MenuSetting which) {
            r.changes.push_back(which);
            r.menu.settings().write(store, which);
        });
        r.to_panel(MenuId::Single);
        ASSERT_TRUE(r.exists(MenuId::SingleName));
        ASSERT_TRUE(r.element(MenuId::SingleName).kind == MenuKind::Field);
        ASSERT_EQ(r.element(MenuId::SingleName).text, std::string("Maya"));                // the name that the settings have (the owner puts --name there too)
        ASSERT_TRUE(has_text(r.menu.elements(), "Your name"));
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);                                       // arriving: the first row, as it was (never a field that takes typing, never a button that acts)
        // by the keyboard: Up from the first row selects the field and its text
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::SingleName);
        ASSERT_TRUE(r.element(MenuId::SingleName).all_selected);
        r.type("Zed");
        ASSERT_EQ(r.menu.name(), std::string("Zed"));                                      // typing replaced "Maya"
        ASSERT_FALSE(r.element(MenuId::SingleName).all_selected);
        ASSERT_EQ(r.element(MenuId::SingleName).text, std::string("Zed"));
        r.type(std::string(60, 'x'));
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax);                              // the limit of the room's protocol, as on the other panels
        r.key(SDLK_BACKSPACE);
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);
        r.type("\x01\xc3\xa9");                                                            // a control character and an accent are not typed ...
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);
        ASSERT_TRUE(has_text(r.menu.elements(), StartMenu::kRefusedCharsText));            // ... and the panel says why (in the rule's place)
        r.type("y");                                                                       // the next key clears the line
        ASSERT_FALSE(has_text(r.menu.elements(), StartMenu::kRefusedCharsText));
        ASSERT_TRUE(has_text(r.menu.elements(), StartMenu::kNoBotsLine));
        ASSERT_FALSE(store.has("name"));                                                   // (nothing is written at every key)
        r.key(SDLK_DOWN);                                                                  // the field is left: now it is
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);
        ASSERT_EQ(store.get_string("name", "", 99), "Zed" + std::string(28, 'x') + "y");
        ASSERT_TRUE(r.changes.back() == MenuSetting::Name);
        // one name for the three panels
        r.key(SDLK_ESCAPE);
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.name(), "Zed" + std::string(28, 'x') + "y");
        ASSERT_EQ(r.element(MenuId::Name).text, r.menu.name());
        r.key(SDLK_UP);
        r.type("Ruth");
        r.key(SDLK_ESCAPE);
        r.click(MenuId::Single);
        ASSERT_EQ(r.element(MenuId::SingleName).text, std::string("Ruth"));                // what was typed on Join is the single player's name
        r.to_panel(MenuId::HostOnline);
        ASSERT_EQ(r.element(MenuId::HostName).text, std::string("Ruth"));
        // by the mouse: a click gives the field the focus and selects nothing (typing goes on at the end); the pointer over it selects nothing
        r.to_panel(MenuId::Single);
        r.mouse_move(MenuId::SingleName);
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);
        r.click(MenuId::SingleName);
        ASSERT_EQ(r.menu.selected(), MenuId::SingleName);
        ASSERT_FALSE(r.element(MenuId::SingleName).all_selected);
        r.type("!");
        ASSERT_EQ(r.menu.name(), std::string("Ruth!"));
        // Tab order: the name, then the rows, Continue, Back (and round)
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);
        r.key(SDLK_TAB, KMOD_SHIFT);
        ASSERT_EQ(r.menu.selected(), MenuId::SingleName);
        // Enter in the field asks for the game: with the cleaned name (the blanks at both ends go), the bots and the teams of the panel; and it is written
        r.key(SDLK_a, KMOD_CTRL);                                                          // (select the text: the next typing replaces it)
        r.type("  Dave  ");
        r.key(SDLK_RETURN);
        MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.empty() && request.teams == LocalTeams{});
        ASSERT_EQ(request.name, std::string("Dave"));
        ASSERT_EQ(store.get_string("name", "", 99), std::string("  Dave  "));              // (the field's text, as the other panels write it; the settings clean it when they are read)
        // Continue asks as well, with the same name; a second ask with nothing new writes nothing new
        const size_t writes = r.changes.size();
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat3);
        r.changes.clear();
        r.click(MenuId::Continue);
        request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.size() == 2 && request.name == "Dave");
        ASSERT_TRUE(std::find(r.changes.begin(), r.changes.end(), MenuSetting::Name) == r.changes.end());
        (void)writes;
        // a name that Join would refuse is refused here with the same words, on the field, with the cue, and nothing is asked
        for (const auto& refused : std::vector<std::pair<std::string, std::string>>{{"", "Type your name first."},
                                                                                  {"   ", "Type your name first."},
                                                                                  {"Bot (Hard)", "A name like \"Bot (...)\" or \"Pip (bot)\" is for computer players. Please choose another name."},
                                                                                  {" bot(x", "A name like \"Bot (...)\" or \"Pip (bot)\" is for computer players. Please choose another name."},
                                                                                  {"Pip (bot)", "A name like \"Bot (...)\" or \"Pip (bot)\" is for computer players. Please choose another name."},
                                                                                  {"Pip (Bot, Hard)", "A name like \"Bot (...)\" or \"Pip (bot)\" is for computer players. Please choose another name."},
                                                                                  {"Ann ( bot , whatever )", "A name like \"Bot (...)\" or \"Pip (bot)\" is for computer players. Please choose another name."}}) {
            Rig b(0, "Player");
            b.to_panel(MenuId::Single);
            b.key(SDLK_UP);
            b.key(SDLK_BACKSPACE);                                                         // (the selected text goes)
            b.type(refused.first);
            const int cues = b.sounds.count(sim::SoundID::CantGo);
            b.click(MenuId::Continue);
            ASSERT_FALSE(b.menu.has_request());
            ASSERT_EQ(b.menu.panel(), MenuPanel::Single);
            ASSERT_EQ(b.menu.message(), refused.second);
            ASSERT_TRUE(has_text(b.menu.elements(), refused.second));
            ASSERT_EQ(b.menu.selected(), MenuId::SingleName);
            ASSERT_EQ(b.sounds.count(sim::SoundID::CantGo), cues + 1);
            std::string clean;
            std::string why;
            ASSERT_FALSE(check_player_name(refused.first, clean, why));                    // the Join panel's rule says the same
            ASSERT_EQ(why, refused.second);
            b.key(SDLK_RETURN);                                                            // Enter in the field is the same ask, refused the same
            ASSERT_FALSE(b.menu.has_request());
            ASSERT_EQ(b.sounds.count(sim::SoundID::CantGo), cues + 2);
            b.type("Bob");                                                                 // the next key takes the line away, and a good name goes through
            ASSERT_TRUE(b.menu.message().empty());
            b.click(MenuId::Continue);
            const MenuRequest ok = b.take();
            ASSERT_TRUE(ok.type == MenuRequest::Type::Single && ok.name == "Bob");
        }
        // Esc leaves the panel and writes what was typed (a name that is not asked for yet is still kept)
        Rig e(0, "Player");
        e.menu.set_on_change([&](MenuSetting which) { e.menu.settings().write(store, which); });
        e.to_panel(MenuId::Single);
        e.key(SDLK_UP);
        e.type("Esme");
        e.key(SDLK_ESCAPE);
        ASSERT_EQ(e.menu.panel(), MenuPanel::Main);
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Esme"));
    } TEST_END();

    TEST_CASE("M3.1 Join: the name and the code fields take printable characters up to 32 and Backspace; Up, Down and Tab move between the fields and the buttons; the keyboard selects the text of a field (the next typing replaces it), a click does not") {
        Rig r(0, "Player");
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);                                        // the name is there already: the code is next
        r.type("abc");
        ASSERT_EQ(r.menu.code(), std::string("abc"));
        r.key(SDLK_BACKSPACE);
        ASSERT_EQ(r.menu.code(), std::string("ab"));
        r.type(std::string(60, 'x'));
        ASSERT_EQ(r.menu.code().size(), static_cast<size_t>(32));                          // cut at the room code's limit
        ASSERT_EQ(r.menu.code().substr(0, 2), std::string("ab"));
        r.key(SDLK_UP);                                                                    // to the name: its text is selected
        ASSERT_EQ(r.menu.selected(), MenuId::Name);
        ASSERT_TRUE(r.element(MenuId::Name).all_selected);
        r.type("Dave");
        ASSERT_EQ(r.menu.name(), std::string("Dave"));                                     // typing replaced "Player"
        ASSERT_FALSE(r.element(MenuId::Name).all_selected);
        r.type(std::string(60, 'y'));
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax);                              // the name's limit is what a Hello carries
        r.key(SDLK_BACKSPACE);
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);
        r.type("\x01\x7f\n\t\xc3\xa9é");                                                  // control characters and anything that is not ASCII are not typed
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);                          // (none of those got in)
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        ASSERT_FALSE(r.element(MenuId::Code).all_selected && r.menu.code().empty());
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Join);
        r.type("zzz");                                                                     // text with no field has nowhere to go
        ASSERT_EQ(r.menu.code().substr(0, 2), std::string("ab"));
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Back);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);                                        // wraps
        // a click gives the focus without selecting the text
        r.menu.show_main();
        r.click(MenuId::JoinWithCode);
        r.click(MenuId::Name);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);
        ASSERT_FALSE(r.element(MenuId::Name).all_selected);
        r.type("!");
        ASSERT_EQ(r.menu.name(), std::string("Dave" + std::string(27, 'y') + "!"));        // appended at the end (31 characters were there)
    } TEST_END();

    TEST_CASE("M3.2 Join: Ctrl+V and Cmd+V paste the clipboard into the field that has the focus (line ends dropped, the code trimmed, cut to the field's length, never case-changed); Ctrl+A selects, Ctrl+Backspace clears; pasting where there is no field does nothing") {
        Rig r;
        r.to_panel(MenuId::JoinWithCode);
        r.clipboard.text = "  Demo-Small-X7K2\r\n";
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_EQ(r.menu.code(), std::string("Demo-Small-X7K2"));                          // trimmed, and the case is the server's: never changed
        r.key(SDLK_a, KMOD_CTRL);                                                          // Ctrl+A: select all
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.clipboard.text = "second";
        r.key(SDLK_v, KMOD_CTRL);                                                          // pasting replaces the selection
        ASSERT_EQ(r.menu.code(), std::string("second"));
        r.key(SDLK_v, KMOD_CTRL);                                                          // and appends when nothing is selected
        ASSERT_EQ(r.menu.code(), std::string("secondsecond"));
        r.clipboard.text = std::string(80, 'q');
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_EQ(r.menu.code().size(), static_cast<size_t>(32));
        r.key(SDLK_BACKSPACE, KMOD_CTRL);                                                  // Ctrl+Backspace: clear the field
        ASSERT_TRUE(r.menu.code().empty());
        r.key(SDLK_UP);                                                                    // the name: pasted text keeps its inner blanks
        r.clipboard.text = "Mary Ann\n";
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_EQ(r.menu.name(), std::string("Mary Ann"));
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);                                                                  // on Join: no field
        ASSERT_EQ(r.menu.selected(), MenuId::Join);
        r.clipboard.text = "nowhere";
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_TRUE(r.menu.code().empty() && r.menu.name() == "Mary Ann");
        // no clipboard at all: the key does nothing
        StartMenu bare;
        bare.set_settings(MenuSettings{});
        bare.show_main();
        bare.on_key(SDLK_DOWN, 0);
        bare.on_key(SDLK_RETURN, 0);
        bare.on_key(SDLK_v, KMOD_GUI);
        ASSERT_TRUE(bare.code().empty());
    } TEST_END();

    TEST_CASE("M3.3 Join: a name that looks like a computer player's is refused on the panel with the reason, whatever its blanks and case; so are an empty name and a bad or empty code; nothing is asked of the application, the panel stays, the can't-go cue plays; a code typed as it is shown (two groups of three, with a blank between) is the same code without the blank") {
        const std::vector<std::string> bot_names = {"Bot (Hard)", "bot (x)", "BOT (Easy)", " Bot (x)", "B o t (x)", "bot(x)", "Bot (", "  bOt(  "};
        for (const std::string& name : bot_names) {
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.key(SDLK_UP);
            r.type(name);
            r.key(SDLK_TAB);
            r.type("abc-1");
            r.key(SDLK_RETURN);                                                            // Enter on the code field: Join
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
            ASSERT_TRUE(r.menu.message().find("Bot (") != std::string::npos);
            ASSERT_EQ(r.menu.selected(), MenuId::Name);                                    // the focus goes to the name that is wrong
            ASSERT_EQ(r.sounds.count(sim::SoundID::CantGo), 1);
            ASSERT_TRUE(has_text(r.menu.elements(), "computer players"));
            r.type("Dave");                                                                // typing the better name clears the message
            ASSERT_TRUE(r.menu.message().empty());
        }
        const std::vector<std::string> fine = {"Robot", "Bobby", "bo t", "Bot", "Bot Smith", "bot-9", "Abot (x)"};
        for (const std::string& name : fine) {
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.key(SDLK_UP);
            r.type(name);
            r.key(SDLK_TAB);
            r.type("abc-1");
            r.key(SDLK_RETURN);
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.type == MenuRequest::Type::Join);
            ASSERT_EQ(request.name, name);
        }
        Rig empty;
        empty.to_panel(MenuId::JoinWithCode);
        empty.key(SDLK_UP);
        empty.key(SDLK_BACKSPACE);                                                         // the selected "Player" is deleted
        ASSERT_TRUE(empty.menu.name().empty());
        empty.key(SDLK_TAB);
        empty.type("abc");
        empty.key(SDLK_RETURN);
        ASSERT_FALSE(empty.menu.has_request());
        ASSERT_EQ(empty.menu.panel(), MenuPanel::Join);
        ASSERT_TRUE(has_text(empty.menu.elements(), "name"));
        Rig blank;
        blank.to_panel(MenuId::JoinWithCode);
        blank.key(SDLK_UP);
        blank.type("   ");
        blank.key(SDLK_TAB);
        blank.type("abc");
        blank.key(SDLK_RETURN);
        ASSERT_FALSE(blank.menu.has_request());                                            // a name of blanks is no name
        // the code
        const std::vector<std::string> bad_codes = {"", "  ", "bad!", "a/b", "\xc3\xbc\xc3\xaf", "dot.dot", "q?", "has space!"};
        for (const std::string& code : bad_codes) {
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.type(code);
            r.key(SDLK_RETURN);
            if (r.menu.has_request()) std::cout << "\n    the code '" << code << "' was accepted as '" << r.menu.take_request().room << "'";
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
            ASSERT_FALSE(r.menu.message().empty());
            ASSERT_EQ(r.menu.selected(), MenuId::Code);
            ASSERT_EQ(r.sounds.count(sim::SoundID::CantGo), 1);
        }
        {   // the code as the screens show it (two groups of three) is typed or pasted with its blank, and is read without it (protocol 15); the blank is the only thing that is dropped
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.key(SDLK_UP);
            r.type("Ann");
            r.key(SDLK_TAB);
            r.type(" k7m 2xq ");
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.menu.has_request());
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.type == MenuRequest::Type::Join);
            ASSERT_EQ(request.room, std::string("k7m2xq"));
            ASSERT_EQ(request.name, std::string("Ann"));
        }
    } TEST_END();

    TEST_CASE("M3.4 Join: a good name and code ask the application to join (the name and the code cleaned, the code's case kept) and the panel shows 'Connecting to <server>...'; a failure comes back to the panel with its line, a cancel without") {
        Rig r;
        r.menu.set_server(ServerAddress{"play.example.org", 4010});
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_UP);
        r.type("  Dave Smith ");
        r.key(SDLK_TAB);
        r.type("Demo-Small-X7K2");
        r.click(MenuId::Join);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Join);
        ASSERT_EQ(request.name, std::string("Dave Smith"));
        ASSERT_EQ(request.room, std::string("Demo-Small-X7K2"));
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        ASSERT_TRUE(has_text(r.menu.elements(), "Connecting to play.example.org:4010..."));
        ASSERT_EQ(r.menu.selected(), MenuId::None);                                        // nothing is preselected here: an Enter that comes twice must not cancel the attempt (M2)
        ASSERT_EQ(r.menu.connect_origin(), MenuPanel::Join);
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Cancel);                                      // Tab (or the pointer) reaches the button
        // Esc and the Cancel button both ask to cancel (the application cancels, then says so)
        r.key(SDLK_ESCAPE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Cancel);
        r.click(MenuId::Cancel);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Cancel);
        r.menu.connection_cancelled();
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_TRUE(r.menu.message().empty());
        ASSERT_EQ(r.menu.selected(), MenuId::Join);                                        // the button to try again is lit, not a field
        ASSERT_EQ(r.menu.name(), std::string("  Dave Smith "));                            // the fields are as the player left them
        ASSERT_EQ(r.menu.code(), std::string("Demo-Small-X7K2"));
        // a failure
        r.key(SDLK_RETURN);
        r.take();
        r.menu.connection_failed("There is no room with that code.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_EQ(r.menu.message(), std::string("There is no room with that code."));
        ASSERT_TRUE(has_text(r.menu.elements(), "There is no room with that code."));
        ASSERT_EQ(r.menu.selected(), MenuId::Join);
        r.key(SDLK_ESCAPE);                                                                // Back: the first panel, the error is gone (it is not carried to the first panel as a notice)
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        ASSERT_TRUE(r.menu.message().empty());
        ASSERT_FALSE(has_text(r.menu.elements(), "There is no room with that code."));
        r.click(MenuId::JoinWithCode);
        ASSERT_TRUE(r.menu.message().empty());
        // Enter in the name field goes on to the code field and joins nothing; Enter in the code field joins
        Rig e;
        e.to_panel(MenuId::JoinWithCode);
        e.key(SDLK_UP);
        e.type("Dave");
        e.key(SDLK_TAB);
        e.type("k7m 2xq");                                                               // (the code as the screens show it, in two groups of three)
        e.key(SDLK_UP);                                                                    // back to the name, with a code written already
        ASSERT_EQ(e.menu.selected(), MenuId::Name);
        e.key(SDLK_RETURN);
        ASSERT_EQ(e.menu.selected(), MenuId::Code);
        ASSERT_FALSE(e.menu.has_request());
        ASSERT_EQ(e.menu.panel(), MenuPanel::Join);
        e.key(SDLK_RETURN);
        const MenuRequest joined = e.take();
        ASSERT_TRUE(joined.type == MenuRequest::Type::Join && joined.room == "k7m2xq" && joined.name == "Dave");
    } TEST_END();

    TEST_CASE("M4.1 Host: the map goes round the six maps of the original game in the page's order and the players round 2, 3, 4 (Left, Right, Enter, click); the request carries the map, the players and the cleaned name") {
        Rig r;
        r.to_panel(MenuId::HostOnline);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        ASSERT_EQ(r.menu.selected(), MenuId::HostMap);                                     // the first input is selected on arrival, not the Host button (M2)
        const std::vector<std::string> maps = {"Tiny", "Small", "Medium", "Gauntlet", "Treasure", "Islands"};
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));              // the panel opens on the default map (M4.4); the walk below starts from the first of the list
        r.key(SDLK_RIGHT);                                                                 // Treasure -> Islands
        r.key(SDLK_RIGHT);                                                                 // Islands -> Tiny: the list goes round
        for (int lap = 0; lap < 2; ++lap) {
            for (size_t i = 0; i < maps.size(); ++i) {
                ASSERT_EQ(r.element(MenuId::HostMap).value, maps[i]);
                r.key(SDLK_RIGHT);
            }
        }
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Islands"));               // Left from the first goes to the last
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Tiny"));
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostPlayers);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("4 players"));         // the page's default
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("2 players"));
        r.click(MenuId::HostPlayers);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("3 players"));
        r.key(SDLK_LEFT);
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("4 players"));
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("3 players"));
        r.key(SDLK_DOWN);                                                                  // the Red seat's bot at START (protocol 13: M4.3)
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat1);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Leave empty"));
        r.key(SDLK_DOWN);                                                                  // the Blue seat (a room of three has it)
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat2);
        r.key(SDLK_DOWN);                                                                  // the teams (three players have the choice: M4.5)
        ASSERT_EQ(r.menu.selected(), MenuId::HostTeams);
        r.key(SDLK_DOWN);                                                                  // the name
        ASSERT_EQ(r.menu.selected(), MenuId::HostName);
        ASSERT_TRUE(r.element(MenuId::HostName).all_selected);
        r.type(" Maya ");
        r.key(SDLK_RIGHT);                                                                 // no cycler here: nothing changes
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Tiny"));
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host);
        ASSERT_TRUE(request.map == 0 && request.players == 3 && request.name == "Maya" && request.fill == net::FillLevel::None && !request.teams.set);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        ASSERT_EQ(r.menu.connect_origin(), MenuPanel::Host);
        r.menu.connection_failed("The server is busy.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);                                        // back where it came from
        ASSERT_TRUE(has_text(r.menu.elements(), "The server is busy."));
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Tiny"));                  // the choice is kept
        // a name that looks like a bot's is refused here too
        r.key(SDLK_UP);
        r.type("Bot (Easy)");
        r.key(SDLK_RETURN);                                                                // Enter in the name field hosts
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        ASSERT_TRUE(has_text(r.menu.elements(), "Bot ("));
        // the changes of the map and the players are reported for the settings file
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostMap) >= 13);
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostPlayers) >= 5);
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::Name) >= 1);
    } TEST_END();

    TEST_CASE("M4.2 Host: the room's panel shows the code in large letters and the players that are in; Copy puts the code on the clipboard (the call is checked) and says Copied! for two seconds; Ctrl+C / Cmd+C copies too; a clipboard that fails says so and shows the code to write down") {
        Rig r;
        r.menu.show_room("k7m2xq", 1, 3);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Room);
        ASSERT_EQ(element_text(r.menu.elements(), MenuKind::Code), std::string("k7m 2xq"));                // (in two groups of three: it is read out and typed)
        ASSERT_TRUE(has_text(r.menu.elements(), "Your room:"));
        ASSERT_TRUE(has_text(r.menu.elements(), "Players in the room: 1 of 3"));
        r.menu.set_room_players(2, 3);
        ASSERT_TRUE(has_text(r.menu.elements(), "Players in the room: 2 of 3"));
        ASSERT_EQ(r.menu.selected(), MenuId::EnterRoom);
        const std::vector<MenuId> expected = {MenuId::Copy, MenuId::EnterRoom, MenuId::Back};
        ASSERT_TRUE(control_ids(r.menu) == expected);
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copy"));
        r.click(MenuId::Copy);
        ASSERT_TRUE(r.clipboard.writes.size() == 1 && r.clipboard.writes[0] == "k7m2xq");                  // (the plain code is what is copied)
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copied!"));
        r.menu.update(1.0f);
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copied!"));
        r.menu.update(1.5f);
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copy"));                      // two seconds later: the word is back
        r.key(SDLK_c, KMOD_CTRL);
        ASSERT_EQ(r.clipboard.writes.size(), static_cast<size_t>(2));
        r.key(SDLK_c, KMOD_GUI);
        ASSERT_EQ(r.clipboard.writes.size(), static_cast<size_t>(3));
        ASSERT_FALSE(r.menu.has_request());
        // Enter continues to the room, Esc and Back leave it
        r.key(SDLK_DOWN);                                                                  // (the clicks above moved the selection to Copy)
        ASSERT_EQ(r.menu.selected(), MenuId::EnterRoom);
        r.key(SDLK_RETURN);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::EnterRoom);
        r.key(SDLK_ESCAPE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::LeaveRoom);
        r.click(MenuId::Back);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::LeaveRoom);
        // the room is gone: back to the Host panel with the reason
        r.menu.room_left("The connection to the server was lost.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        ASSERT_TRUE(has_text(r.menu.elements(), "The connection to the server was lost."));
        // a clipboard that cannot be written
        Rig bad;
        bad.clipboard.writable = false;
        bad.menu.show_room("qqq777", 1, 2);
        bad.click(MenuId::Copy);
        ASSERT_EQ(bad.clipboard.writes.size(), static_cast<size_t>(1));
        ASSERT_FALSE(bad.menu.copied());
        ASSERT_TRUE(has_text(bad.menu.elements(), "Copy failed") && element_text(bad.menu.elements(), MenuKind::Code) == "qqq 777");   // (the code stands in full in its box)
        bad.clipboard.writable = true;                                                      // the clipboard comes back: the failure line goes with the next copy that works
        bad.click(MenuId::Copy);
        ASSERT_TRUE(bad.menu.copied());
        ASSERT_FALSE(has_text(bad.menu.elements(), "Copy failed"));
        bad.menu.show_room("rrr777", 1, 2);                                               // a new room: "Copied!" of the old one is not carried over
        ASSERT_FALSE(bad.menu.copied());
        ASSERT_EQ(bad.element(MenuId::Copy).text, std::string("Copy"));
        // no clipboard at all
        StartMenu none;
        none.set_settings(MenuSettings{});
        none.show_room("code", 1, 2);
        none.on_key(SDLK_c, KMOD_GUI);
        ASSERT_FALSE(none.copied());
    } TEST_END();

    TEST_CASE("M4.3 Host: a row for each seat after the leader's (Red, Blue and Black, as many as the room has) goes round Leave empty, Easy bot, Medium bot, Hard bot (Left, Right, Enter, click), each on its own, and is remembered; the request of Host carries the levels of the seats that the room has; the room's panel says what START will do; every control of both panels lies inside the screen and no two overlap") {
        using L = net::FillLevel;
        // the words of a seat's choice
        ASSERT_EQ(std::string(fill_seat_text(L::None)), std::string("Leave empty"));
        ASSERT_EQ(std::string(fill_seat_text(L::Easy)), std::string("Easy bot"));
        ASSERT_EQ(std::string(fill_seat_text(L::Medium)), std::string("Medium bot"));
        ASSERT_EQ(std::string(fill_seat_text(L::Hard)), std::string("Hard bot"));
        // what the room's panel says START will do: the same level in every seat of the room keeps the one sentence of protocol 11, any other plan names its seats (the short form when the long one is
        // longer than the line), the seats beyond the room and the leader's own seat are nobody's business
        ASSERT_EQ(fill_choice_sentence(L::Medium), std::string("Empty seats will be Medium bots."));
        ASSERT_EQ(fill_choice_sentence(L::Easy), std::string("Empty seats will be Easy bots."));
        ASSERT_EQ(fill_choice_sentence(L::Hard), std::string("Empty seats will be Hard bots."));
        ASSERT_EQ(fill_choice_sentence(L::None), std::string("Empty seats stay empty."));
        const auto plan = [](L red, L blue, L black) {
            net::FillPlan p;
            p.level[1] = red;
            p.level[2] = blue;
            p.level[3] = black;
            return p;
        };
        ASSERT_EQ(fill_choice_sentence(plan(L::Easy, L::None, L::Hard)), std::string("At START: Red gets an Easy bot, Black a Hard bot."));
        ASSERT_EQ(fill_choice_sentence(plan(L::None, L::Medium, L::None)), std::string("At START: Blue gets a Medium bot."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Medium, L::Medium, L::Medium)), std::string("Empty seats will be Medium bots."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Easy, L::Medium, L::Hard)), std::string("At START: Red Easy, Blue Medium, Black Hard."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Hard, L::Easy, L::Easy), 2), std::string("Empty seats will be Hard bots."));            // (a room of two has the Red seat only)
        ASSERT_EQ(fill_choice_sentence(plan(L::None, L::Easy, L::Easy), 2), std::string("Empty seats stay empty."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Easy, L::Hard, L::Hard), 3), std::string("At START: Red gets an Easy bot, Blue a Hard bot."));
        net::FillPlan own;
        own.level[0] = L::Hard;
        ASSERT_EQ(fill_choice_sentence(own), std::string("Empty seats stay empty."));
        for (size_t i = 0; i < 64; ++i) {                                                  // every plan: one line of the room's panel (520 px: the estimate of Recorder is 9 px a character at 18 px)
            const net::FillPlan p = plan(static_cast<L>(i % 4), static_cast<L>((i / 4) % 4), static_cast<L>(i / 16));
            for (int players = 2; players <= 4; ++players) ASSERT_TRUE(fill_choice_sentence(p, players).size() * 9 <= 520);
        }
        Rig r;
        r.to_panel(MenuId::HostOnline);
        // the rows: a label and a cycler for each seat after the leader's, in the order of the keys; the old single choice is gone
        const std::vector<MenuId> four = {MenuId::HostMap, MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3, MenuId::HostTeams, MenuId::HostName, MenuId::Host, MenuId::Back};
        ASSERT_TRUE(control_ids(r.menu) == four);
        for (const char* label : {"Red at START", "Blue at START", "Black at START", "Teams", "Map", "Players", "Your name"}) ASSERT_TRUE(has_text(r.menu.elements(), label));
        ASSERT_FALSE(has_text(r.menu.elements(), "Empty seats at START"));
        for (const MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
            ASSERT_EQ(r.element(id).kind, MenuKind::Cycler);
            ASSERT_EQ(r.element(id).value, std::string("Leave empty"));                    // nothing is seated unless the player chose it
        }
        // a room of three has two seat rows after the leader's and a room of two has one (the Teams row needs three)
        {
            Rig three;
            three.to_panel(MenuId::HostOnline);
            three.click(MenuId::HostPlayers);                                              // 4 -> 2
            const std::vector<MenuId> two_players = {MenuId::HostMap, MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostName, MenuId::Host, MenuId::Back};
            ASSERT_TRUE(control_ids(three.menu) == two_players);
            ASSERT_TRUE(has_text(three.menu.elements(), "Red at START") && !has_text(three.menu.elements(), "Blue at START") && !has_text(three.menu.elements(), "Black at START"));
            three.click(MenuId::HostPlayers);                                              // 2 -> 3
            const std::vector<MenuId> three_players = {MenuId::HostMap, MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostTeams, MenuId::HostName, MenuId::Host, MenuId::Back};
            ASSERT_TRUE(control_ids(three.menu) == three_players);
            ASSERT_TRUE(has_text(three.menu.elements(), "Blue at START") && !has_text(three.menu.elements(), "Black at START"));
        }
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat1);
        const char* const words[4] = {"Leave empty", "Easy bot", "Medium bot", "Hard bot"};
        for (int lap = 0; lap < 2; ++lap) {
            for (const char* word : words) {
                ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string(word));
                r.key(SDLK_RIGHT);
            }
        }
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Hard bot"));            // Left from the first goes to the last
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Leave empty"));         // Enter goes on, round
        r.click(MenuId::HostSeat1);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Easy bot"));
        ASSERT_EQ(r.element(MenuId::HostSeat2).value, std::string("Leave empty"));         // each row is its own
        ASSERT_EQ(r.element(MenuId::HostSeat3).value, std::string("Leave empty"));
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostFill) >= 11);       // every change is reported for the settings file
        r.key(SDLK_RIGHT);                                                                 // Red: Medium bot
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat2);
        for (int i = 0; i < 3; ++i) r.key(SDLK_RIGHT);                                     // Blue: Hard bot
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat3);
        r.click(MenuId::HostSeat3);                                                        // Black: Easy bot
        ASSERT_TRUE(r.menu.settings().host_fill.level[0] == L::None && r.menu.settings().host_fill.level[1] == L::Medium && r.menu.settings().host_fill.level[2] == L::Hard &&
                    r.menu.settings().host_fill.level[3] == L::Easy);
        // the request carries the levels of the seats that the room has (never the leader's seat, never a seat beyond the room), and a failed attempt keeps them
        r.key(SDLK_DOWN);                                                                  // the teams
        r.key(SDLK_DOWN);                                                                  // the name
        r.key(SDLK_RETURN);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.players == 4 && request.fill == plan(L::Medium, L::Hard, L::Easy));
        r.menu.connection_failed("The server is busy.");
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Medium bot"));
        ASSERT_EQ(r.element(MenuId::HostSeat2).value, std::string("Hard bot"));
        ASSERT_EQ(r.element(MenuId::HostSeat3).value, std::string("Easy bot"));
        r.click(MenuId::HostPlayers);                                                      // 2 players: the Red seat only
        r.click(MenuId::Host);
        const MenuRequest two = r.take();
        ASSERT_TRUE(two.type == MenuRequest::Type::Host && two.players == 2 && two.fill == plan(L::Medium, L::None, L::None));
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                      // 3 players
        r.click(MenuId::Host);
        const MenuRequest three = r.take();
        ASSERT_TRUE(three.type == MenuRequest::Type::Host && three.players == 3 && three.fill == plan(L::Medium, L::Hard, L::None));
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                      // back to 4: the Black seat's choice was kept all along
        ASSERT_EQ(r.element(MenuId::HostSeat3).value, std::string("Easy bot"));
        // the room's panel says what START will do
        r.menu.show_room("k7m2xq", 1, 4);
        ASSERT_TRUE(has_text(r.menu.elements(), "At START: Red Medium, Blue Hard, Black Easy."));
        ASSERT_FALSE(has_text(r.menu.elements(), "Empty seats stay empty."));
        r.menu.show_room("k7m2xq", 1, 2);
        ASSERT_TRUE(has_text(r.menu.elements(), "Empty seats will be Medium bots."));      // (the room of two has the Red seat only)
        Rig uniform;
        uniform.to_panel(MenuId::HostOnline);
        for (const MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
            uniform.click(id);
            uniform.click(id);
        }
        uniform.menu.show_room("k7m2xq", 1, 4);
        ASSERT_TRUE(has_text(uniform.menu.elements(), "Empty seats will be Medium bots."));
        Rig none;
        none.menu.show_room("k7m2xq", 1, 4);
        ASSERT_TRUE(has_text(none.menu.elements(), "Empty seats stay empty.") && !has_text(none.menu.elements(), "bots."));
        // the settings file: written under `host_fill` (one word when every seat is the same, else four), read back; an old file's one word is every seat after the leader's; anything else is the default
        {
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            MenuSettings s;
            s.host_fill = plan(L::Easy, L::None, L::Hard);
            s.write(store, MenuSetting::HostFill);
            ASSERT_EQ(store.get_string("host_fill", "", 99), std::string("none,easy,none,hard"));
            MenuSettings back;
            back.load(store);
            ASSERT_TRUE(back.host_fill == s.host_fill);
            MenuSettings same;
            same.host_fill = net::FillPlan(L::Hard);
            same.write(store, MenuSetting::HostFill);
            ASSERT_EQ(store.get_string("host_fill", "", 99), std::string("hard"));
            back.load(store);
            ASSERT_TRUE(back.host_fill == plan(L::Hard, L::Hard, L::Hard));                // (the leader's seat gets nothing)
            store.set_string("host_fill", "  MEDIUM ");
            back.load(store);
            ASSERT_TRUE(back.host_fill == plan(L::Medium, L::Medium, L::Medium));          // (any case, blanks cut)
            store.set_string("host_fill", " Easy , none ,HARD, none");
            back.load(store);
            ASSERT_TRUE(back.host_fill == plan(L::None, L::Hard, L::None));                // (four words: the first is the leader's seat and counts for nothing)
            for (const char* bad : {"loud", "easy,hard", "easy,easy,easy,easy,easy", "easy,,easy,easy", "none,none,none,loud", ""}) {
                store.set_string("host_fill", bad);
                back.load(store);
                ASSERT_FALSE(back.host_fill.any());
            }
            MenuSettings fresh;
            ASSERT_FALSE(fresh.host_fill.any());                                           // nothing is seated unless the player chose it
        }
        // the geometry, in every state: every control and every line inside the screen, no two controls over each other, the label of each row beside its control and the caption under the last
        // of the rows
        for (int players = 2; players <= 4; ++players) {
            for (const MenuPanel panel : {MenuPanel::Host, MenuPanel::Room}) {
                Rig g;
                g.to_panel(MenuId::HostOnline);
                while (g.menu.settings().host_players != players) g.click(MenuId::HostPlayers);
                for (const MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
                    if (g.exists(id)) g.click(id);
                }
                if (g.exists(MenuId::HostTeams)) g.click(MenuId::HostTeams);
                if (panel == MenuPanel::Room) g.menu.show_room("k7m2xq", 1, players);
                const std::vector<MenuElement> all = g.menu.elements();
                for (size_t i = 0; i < all.size(); ++i) {
                    ASSERT_TRUE(all[i].rect.x >= 16 && all[i].rect.y >= 16 && all[i].rect.x + all[i].rect.w <= 624 && all[i].rect.y + all[i].rect.h <= 464);
                    for (size_t j = i + 1; j < all.size(); ++j) {
                        if (all[i].id != MenuId::None && all[j].id != MenuId::None) ASSERT_FALSE(intersects(all[i].rect, all[j].rect));
                    }
                }
            }
        }
        for (int players = 2; players <= 4; ++players) {
            Rig g;
            g.to_panel(MenuId::HostOnline);
            while (g.menu.settings().host_players != players) g.click(MenuId::HostPlayers);
            const std::vector<MenuElement> all = g.menu.elements();
            const struct { const char* label; MenuId id; } rows[] = {{"Map", MenuId::HostMap},         {"Players", MenuId::HostPlayers}, {"Red at START", MenuId::HostSeat1},
                                                                    {"Blue at START", MenuId::HostSeat2}, {"Black at START", MenuId::HostSeat3}, {"Teams", MenuId::HostTeams},
                                                                    {"Your name", MenuId::HostName}};
            MenuElement last;                                                              // the last row of the choices (the name is no choice)
            for (const auto& row : rows) {
                if (!g.exists(row.id)) continue;
                MenuElement label;
                for (const MenuElement& e : all) {
                    if (e.text == row.label && e.id == MenuId::None) label = e;
                }
                const MenuElement control = g.element(row.id);
                ASSERT_EQ(label.text, std::string(row.label));
                ASSERT_TRUE(label.rect.x + label.rect.w <= control.rect.x && label.rect.y < control.rect.y + control.rect.h && control.rect.y < label.rect.y + label.rect.h);
                if (row.id != MenuId::HostName) last = control;
            }
            Recorder rec;                                                                  // every label is drawn whole (a text that is cut would lose its end)
            render_start_menu(rec, archive(), g.menu);
            for (const auto& row : rows) {
                if (!g.exists(row.id)) continue;
                bool whole = false;
                for (const Recorder::Text& t : rec.texts) whole = whole || t.text == row.label;
                ASSERT_TRUE(whole);
            }
            // the caption under the rows (the panel says what the standard bot does, it gathers food, raids and fights back, and must not promise more): exact words, dim, between the last row and the name,
            // drawn whole and 14 px
            ASSERT_EQ(std::string(fill_choice_caption()), std::string("Bots gather food, raid and fight back."));
            MenuElement caption;
            MenuElement name_label;
            for (const MenuElement& e : all) {
                if (e.text == fill_choice_caption()) caption = e;
                if (e.text == "Your name") name_label = e;
            }
            ASSERT_EQ(caption.text, std::string(fill_choice_caption()));
            ASSERT_TRUE(caption.id == MenuId::None && caption.tone == MenuTone::Dim && caption.font == FontSize::Px14 && caption.centered);
            ASSERT_TRUE(caption.rect.y >= last.rect.y + last.rect.h && caption.rect.y + caption.rect.h <= name_label.rect.y);                    // under the last row, above the name
            ASSERT_TRUE(caption.rect.x <= last.rect.x && caption.rect.x + caption.rect.w >= last.rect.x + last.rect.w);                            // centred under the whole control
            bool caption_drawn = false;
            for (const Recorder::Text& t : rec.texts) caption_drawn = caption_drawn || (t.text == fill_choice_caption() && t.size == FontSize::Px14);
            ASSERT_TRUE(caption_drawn);
        }
        Rig room_panel;
        room_panel.menu.show_room("k7m2xq", 1, 4);
        ASSERT_FALSE(has_text(room_panel.menu.elements(), fill_choice_caption()));                                                       // (only the panel that has the choice)
    } TEST_END();
}

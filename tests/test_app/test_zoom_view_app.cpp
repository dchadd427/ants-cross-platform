// Zoom view, part 3: the application (the wheel, the middle button, the zoom API, the setup screen, the guards, the settings, the ants, the start view, a network match).
// (See test_zoom_view.cpp for what the whole program checks.)
#include "zoom_view_fixtures.hpp"

// =====================================================================================================================================================
// Application: the wheel, the middle button, the zoom API, fairness, the settings key, the start view, the listener
// =====================================================================================================================================================

void test_app_wheel() {
    group("wheel", "the wheel zooms towards the pointer, over the map only, and never while a dialog is open or a press holds the mouse");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const std::string pic = wide ? "wide: " : "classic: ";
        AppRig rig(aspect);
        check(rig.ok, pic + "the application is up");
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        const LayoutRect mini = app.layout().minimap();
        check(app.state() == AppState::Playing && app.zoom() == 1.0f && app.remembered_zoom() == 1.0f, pic + "a match starts at the zoom 1");
        // the levels of GAUNTLET (1920 x 1920): from 2 down to the map's width in this view, 762 / 1920 = 0.397 in the wide one, 442 / 1920 = 0.230 in the classic one
        const std::vector<float> levels = app.zoom_levels();
        const std::vector<double> want_levels = wide ? std::vector<double>{2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.59460, 0.5, 0.42045, 0.396875}
                                                     : std::vector<double>{2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.70711, 0.59460, 0.5, 0.42045, 0.35355, 0.29730, 0.25, 0.230208};
        bool listed = levels.size() == want_levels.size();
        for (size_t i = 0; listed && i < levels.size(); ++i) listed = std::fabs(static_cast<double>(levels[i]) - want_levels[i]) < 5e-5;
        check(listed, pic + "a local game offers the series from 2 down to the map's limit on GAUNTLET: " + std::to_string(levels.size()) + " levels");
        if (!listed || levels.size() < 9) continue;
        const float in1 = levels[3];                                         // 1.19: one notch from 1
        const float out1 = levels[5];                                        // 0.84
        const float bottom = levels.back();
        const int32_t px = view.x + 284;
        const int32_t py = view.y + 183;
        app.note_pointer(px, py);
        const auto near_world = [](const std::pair<int32_t, int32_t>& a, const std::pair<int32_t, int32_t>& b, int32_t tolerance) { return std::abs(a.first - b.first) <= tolerance && std::abs(a.second - b.second) <= tolerance; };

        // up zooms in towards the pointer: the world point under it stays under it (to the rounding of the origin to the lattice of the new zoom: a world pixel)
        const auto before = rig.world_at(px, py);
        notch(app, +1);
        check(app.zoom() == in1, pic + "a notch away zooms in to the next level, 1.19");
        check(near_world(rig.world_at(px, py), before, 1), pic + "the world point under the pointer is the same after the zoom in, to a world pixel: (" + std::to_string(before.first) + ", " + std::to_string(before.second) + ")");
        check(app.remembered_zoom() == in1, pic + "the level is remembered");
        for (int i = 2; i >= 0; --i) {                                       // one level a notch, up to 2
            notch(app, +1);
            check(app.zoom() == levels[static_cast<size_t>(i)], pic + "a notch away is the next level: " + std::to_string(static_cast<double>(levels[static_cast<size_t>(i)])));
            check(near_world(rig.world_at(px, py), before, 1), pic + "... with the same world point under the pointer");
        }
        notch(app, +1);
        check(app.zoom() == 2.0f, pic + "there is no level above 2");
        for (size_t i = 1; i <= 4; ++i) {                                    // one level a notch, back down to 1
            notch(app, -1);
            check(app.zoom() == levels[i], pic + "a notch toward is the next level out: " + std::to_string(static_cast<double>(levels[i])));
        }
        check(app.zoom() == 1.0f && near_world(rig.world_at(px, py), before, 1), pic + "a notch per level back to 1, with the same world point under the pointer");
        notch(app, -1);
        check(app.zoom() == out1, pic + "another notch toward zooms out to 0.84");
        const auto after_out = rig.world_at(px, py);
        check(near_world(after_out, before, 1), pic + "the world point under the pointer stays (within a world pixel)");
        for (size_t i = 6; i < levels.size(); ++i) {                         // and on down to the map's limit
            notch(app, -1);
            check(app.zoom() == levels[i], pic + "a notch toward is the next level out: " + std::to_string(static_cast<double>(levels[i])));
        }
        check(app.zoom() == bottom, pic + "the last level is the map's limit: " + std::to_string(static_cast<double>(bottom)));
        {   // at the limit the view shows the map's whole width and nothing outside the map
            const ViewportCamera& cam = app.renderer().camera();
            check(cam.x >= 0.0f && cam.y >= 0.0f && cam.x + static_cast<float>(view.w) / bottom <= 1920.0f + 1e-3f && cam.y + static_cast<float>(view.h) / bottom <= 1920.0f + 1e-3f && cam.x == 0.0f, pic + "at the limit the camera shows the whole width of the map and nothing beyond it: (" + std::to_string(static_cast<double>(cam.x)) + ", " + std::to_string(static_cast<double>(cam.y)) + ")");
        }
        notch(app, -1);
        check(app.zoom() == bottom, pic + "there is no level below the map's limit");
        notch(app, +1);
        check(app.zoom() == levels[levels.size() - 2], pic + "a notch away from the limit is the level above it");
        for (size_t i = levels.size() - 2; i > 4; --i) notch(app, +1);
        check(app.zoom() == 1.0f, pic + "(back at 1)");

        // natural scrolling: SDL has inverted the numbers and says so (FLIPPED); the wheel rolled away still zooms in
        notch(app, +1, true);
        check(app.zoom() == in1, pic + "a flipped event of a wheel rolled away (its numbers are negative) zooms in");
        notch(app, -1, true);
        check(app.zoom() == 1.0f, pic + "a flipped event of a wheel rolled toward zooms out");
        // the same numbers without the flag are the other way round
        app.handle_mouse_wheel(wheel_event(-1, -1.0f, false));
        check(app.zoom() == out1, pic + "the same negative numbers, not flipped, zoom out");
        app.handle_mouse_wheel(wheel_event(-1, -1.0f, true));
        check(app.zoom() == 1.0f, pic + "... and flipped they zoom in");

        // a trackpad: the precise deltas add up to one step
        app.handle_mouse_wheel(wheel_event(0, 0.4f));
        app.handle_mouse_wheel(wheel_event(0, 0.4f));
        check(app.zoom() == 1.0f, pic + "0.4 + 0.4 is not a step");
        app.handle_mouse_wheel(wheel_event(0, 0.4f));
        check(app.zoom() == in1, pic + "0.4 + 0.4 + 0.4 is one step: in");
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        check(app.zoom() == in1, pic + "-0.9 is not a step yet (the left-over 0.2 of the last step was dropped by the change of direction)");
        app.handle_mouse_wheel(wheel_event(0, -0.3f));
        check(app.zoom() == 1.0f, pic + "-1.2 is one step: out");
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        app.handle_mouse_wheel(wheel_event(0, 0.6f, false, 2000));
        check(app.zoom() == 1.0f, pic + "a pause of two seconds forgets half a notch: 0.6, a pause, 0.6 is no step");

        // a mouse notch that the browser reports as 120, which the page counts at 100 a step: 1.2. One level a notch, however many come in a burst: the 0.2 is not kept (it made the fifth notch
        // of a burst two levels)
        for (int i = 0; i < 5; ++i) {
            app.handle_mouse_wheel(wheel_event(-1, -1.2f, false, 80));
            check(app.zoom() == levels[static_cast<size_t>(5 + i)], pic + "notch " + std::to_string(i + 1) + " of five of 120 within 400 ms is one level out: " + std::to_string(static_cast<double>(levels[static_cast<size_t>(5 + i)])));
        }
        for (int i = 0; i < 5; ++i) {
            app.handle_mouse_wheel(wheel_event(1, 1.2f, false, 80));
            check(app.zoom() == levels[static_cast<size_t>(8 - i)], pic + "notch " + std::to_string(i + 1) + " of five of 120 the other way is one level in: " + std::to_string(static_cast<double>(levels[static_cast<size_t>(8 - i)])));
        }
        check(app.zoom() == 1.0f, pic + "five of 120 out and five in are back at 1");

        // only over the map view
        const std::vector<std::pair<std::pair<int32_t, int32_t>, std::string>> outside = {
            {{mini.x + 10, mini.y + 10}, "the minimap"},
            {{view.right() + 30, view.y + 250}, "the right panel"},
            {{view.x + 100, 8}, "the top bar"},
            {{view.x + 100, view.bottom() + 8}, "the bottom strip"},
            {{view.x - 1, view.y + 100}, "the pixel left of the view"},
            {{view.right(), view.y + 100}, "the pixel right of the view"},
            {{view.x + 100, view.y - 1}, "the pixel above the view"},
            {{view.x + 100, view.bottom()}, "the pixel below the view"}};
        for (const auto& o : outside) {
            app.note_pointer(o.first.first, o.first.second);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "the wheel over " + o.second + " does not zoom");
        }
        for (const auto& edge : {std::pair<int32_t, int32_t>{view.x, view.y}, std::pair<int32_t, int32_t>{view.right() - 1, view.bottom() - 1}, std::pair<int32_t, int32_t>{view.x, view.bottom() - 1}, std::pair<int32_t, int32_t>{view.right() - 1, view.y}}) {
            app.note_pointer(edge.first, edge.second);
            notch(app, +1);
            check(app.zoom() == in1, pic + "the wheel over the view's corner pixel (" + std::to_string(edge.first) + ", " + std::to_string(edge.second) + ") zooms");
            notch(app, -1);
            check(app.zoom() == 1.0f, pic + "... and back");
        }
        // what a panel or a dialog got is not half a notch for the map
        app.note_pointer(px, py);
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        app.note_pointer(mini.x + 10, mini.y + 10);
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        app.note_pointer(px, py);
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        check(app.zoom() == 1.0f, pic + "a wheel event that is ignored forgets the half notch before it: 0.6, (0.6 on the minimap), 0.6 is no step");

        // not while the pointer is out of the window
        app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_LEAVE; return we; }());
        notch(app, +1);
        check(app.zoom() == 1.0f, pic + "the wheel does nothing while the pointer is outside the window");
        app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_ENTER; return we; }());
        app.note_pointer(px, py);
        notch(app, +1);
        check(app.zoom() == in1, pic + "... and zooms again when it is back");
        notch(app, -1);

        // not while a press holds the mouse: the rubber band, the minimap, a button, the right button, the chat log
        {
            app.note_pointer(px, py);
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, px, py));
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom during a rubber band");
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, px, py));
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom after it");
            notch(app, -1);
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, mini.x + 20, mini.y + 20));
            app.note_pointer(px, py);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while the minimap is held");
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, mini.x + 20, mini.y + 20));
            app.note_pointer(px, py);
            app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, px, py));
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while the right button is held on the map");
            app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, px, py));
            const UIButton help = app.hud().help_button();
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, help.x + help.w / 2, help.y + help.h / 2));
            check(app.hud().help_button().is_pressed, pic + "(the Help button is pressed)");
            app.note_pointer(px, py);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while a button is pressed");
            app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, help.x + help.w / 2, help.y + help.h / 2));
            app.hud().close_quick_help();
            app.note_pointer(px, py);                                       // (the release left the pointer on the button)
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom after it");
            notch(app, -1);
        }
        // not while a dialog or a page is open: quit, options, quick help, "get ready", an alliance question
        {
            app.note_pointer(px, py);
            app.hud().open_quit_dialog();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the quit dialog open");
            app.hud().close_quit_dialog();
            app.hud().open_options();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the options window open");
            app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera());     // Enter closes it
            app.hud().open_quick_help();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the quick help open");
            app.hud().close_quick_help();
            app.hud().start_match_modal();
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with the \"get ready\" dialog open");
            app.hud().dismiss_match_start_modal();
            app.sim().form_alliance(0, 2);
            app.sim().tick();
            app.hud().request_team_up(app.sim(), 1);
            check(app.hud().alliance_dialog() == HUD::AllianceDialog::BreakConfirm, pic + "(an alliance question is open)");
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom with an alliance question open");
            app.hud().handle_key_down('n', app.sim(), app.renderer().camera());
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom when everything is closed again");
            notch(app, -1);
            // the results screen
            app.scorecard().show(app.sim().get_world_state().match_result, 0);
            notch(app, +1);
            check(app.zoom() == 1.0f, pic + "no zoom while the results are shown");
            app.scorecard().hide();
            notch(app, +1);
            check(app.zoom() == in1, pic + "... and zoom after");
            notch(app, -1);
        }
        check(app.zoom() == 1.0f, pic + "(the test's own bookkeeping: back at 1)");
    }

    // the way the window delivers it: an SDL_MOUSEWHEEL event in the queue and a frame
    {
        AppRig rig(Aspect::Wide16x9);
        check(rig.ok, "the application for the event queue is up");
        if (rig.ok) {
            Application& app = rig.app;
            app.note_pointer(300, 250);
            SDL_Event e{};
            e.type = SDL_MOUSEWHEEL;
            e.wheel = wheel_event(1, 1.0f);
            SDL_PushEvent(&e);
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == zoom::series(1), "a wheel event in the queue zooms the match in the next frame");
            SDL_Event m{};
            m.type = SDL_MOUSEBUTTONDOWN;
            m.button = button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 250);
            SDL_PushEvent(&m);
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 1.0f, "a middle button press in the queue goes back to 1");
        }
    }
}

void test_app_middle_button() {
    group("middle", "the middle button goes back to the zoom 1 towards the pointer, with the same rules as the wheel, and is nobody else's");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const std::string pic = wide ? "wide: " : "classic: ";
        AppRig rig(aspect);
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        const LayoutRect mini = app.layout().minimap();
        const int32_t px = view.x + 200;
        const int32_t py = view.y + 120;
        app.note_pointer(px, py);
        for (const float level : {2.0f, 0.5f}) {
            check(app.set_zoom(level, px, py), pic + "the level " + zoom::level_name(level) + " is set");
            const auto before = rig.world_at(px, py);
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
            check(app.zoom() == 1.0f, pic + "a middle press goes back to 1 from " + zoom::level_name(level));
            const auto after = rig.world_at(px, py);
            check(std::abs(after.first - before.first) <= 1 && std::abs(after.second - before.second) <= 1, pic + "towards the pointer: the world point under it stays");
            check(app.remembered_zoom() == 1.0f, pic + "the level is remembered");
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONUP, px, py));
            check(app.zoom() == 1.0f, pic + "the release does nothing");
        }
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 1.0f, pic + "at 1 a middle press changes nothing");
        // not over a panel
        check(app.set_zoom(2.0f, px, py), pic + "(zoomed in again)");
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, mini.x + 10, mini.y + 10));
        check(app.zoom() == 2.0f, pic + "a middle press on the minimap does nothing");
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, view.right() + 20, view.y + 200));
        check(app.zoom() == 2.0f, pic + "... nor on the right panel");
        // not during a press that holds the mouse
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, px, py));
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 2.0f, pic + "... nor during a rubber band");
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, px, py));
        // with a dialog open
        app.hud().open_options();
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 2.0f, pic + "... nor with the options window open");
        app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera());
        // the middle button never reaches the HUD: a middle release does not end a minimap drag (it used to clear every pressed state)
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, mini.x + 20, mini.y + 20));
        check(app.hud().is_input_captured(), pic + "(the minimap is held)");
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, mini.x + 20, mini.y + 20));
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONUP, mini.x + 20, mini.y + 20));
        check(app.hud().is_input_captured(), pic + "a middle press and release do not let go of the minimap");
        app.handle_mouse_button(button_event(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, mini.x + 20, mini.y + 20));
        check(!app.hud().is_input_captured(), pic + "(released)");
        // a middle press on the results screen is still nothing
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, px, py));
        check(app.zoom() == 2.0f, pic + "a middle press on the results does nothing");
    }
}

void test_app_zoom_api() {
    group("api", "set_zoom, step_zoom and zoom_levels, the API of the touch work");
    AppRig rig(Aspect::Wide16x9);
    if (!rig.ok) return;
    Application& app = rig.app;
    const LayoutRect view = app.layout().view();
    const std::vector<float> levels = app.zoom_levels();                      // GAUNTLET in the wide view: 2 ... 1 ... 0.397
    check(levels.size() == 11 && levels.front() == 2.0f && levels[4] == 1.0f && levels.back() == zoom::floor_zoom(app.zoom_fit()), "the levels of a local game on GAUNTLET in the wide view: 11, from 2 down to the map's limit");
    check(!app.set_zoom(1.0f, 300, 200), "the level that is set already changes nothing (false)");
    for (const float bad : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        check(!app.set_zoom(bad, 300, 200) && app.zoom() == 1.0f, "a number that is no zoom is refused: " + num(static_cast<double>(bad)));
    }
    check(app.set_zoom(0.75f, 300, 200) && app.zoom() == zoom::series(-2), "a zoom that is not a level goes to the nearest level: 0.75 is 0.71");
    check(app.set_zoom(4.0f, 300, 200) && app.zoom() == 2.0f, "beyond the largest level it goes to 2");
    check(app.set_zoom(0.01f, 300, 200) && app.zoom() == levels.back(), "below the map's limit it goes to the limit (a pinch that goes too far is held there)");
    check(app.set_zoom(1.0f, 300, 200) && app.zoom() == 1.0f, "1 is a level like the others");
    check(app.set_zoom(2.0f, 300, 200) && app.zoom() == 2.0f, "a level of the list is taken (true)");
    check(app.set_zoom(0.5f, 300, 200) && app.zoom() == 0.5f, "from 2 straight to 0.5 (the API goes to any offered level)");
    for (size_t i = 8; i-- > 0;) check(app.step_zoom(+1, 300, 200) && app.zoom() == levels[i], "step_zoom(+1) goes up one level: " + num(static_cast<double>(levels[i])));
    check(!app.step_zoom(+1, 300, 200) && app.zoom() == 2.0f, "... and then says no at 2");
    for (size_t i = 1; i < levels.size(); ++i) check(app.step_zoom(-1, 300, 200) && app.zoom() == levels[i], "step_zoom(-1) goes down one level: " + num(static_cast<double>(levels[i])));
    check(!app.step_zoom(-1, 300, 200) && app.zoom() == levels.back(), "... and then says no at the map's limit");
    check(!app.step_zoom(0, 300, 200), "no direction, no step");
    // the anchor is a point of the picture: outside the view it is held to the view's nearest pixel
    check(app.set_zoom(1.0f, -500, -500) && app.set_zoom(2.0f, 5000, 5000) && app.zoom() == 2.0f, "an anchor outside the picture is held to the view");
    app.set_zoom(1.0f, view.x + 1, view.y + 1);
    {
        // ... to the view's own pixel: an anchor beyond an edge gives the camera that the edge pixel gives, and the anchor does matter inside the view
        const uint32_t tiles_w = app.sim().grid().width();
        const uint32_t tiles_h = app.sim().grid().height();
        const auto zoom_from_700 = [&](int32_t ax, int32_t ay) {
            app.set_zoom(1.0f, view.x + 1, view.y + 1);
            app.renderer().camera().set_origin(700.0, 700.0, tiles_w, tiles_h);
            app.set_zoom(2.0f, ax, ay);
            return std::pair<double, double>{app.renderer().camera().x, app.renderer().camera().y};
        };
        check(zoom_from_700(view.x - 100, view.y + 50) == zoom_from_700(view.x, view.y + 50), "an anchor left of the view is the view's left column");
        check(zoom_from_700(view.x + 50, view.y - 100) == zoom_from_700(view.x + 50, view.y), "an anchor above the view is the view's top row");
        check(zoom_from_700(view.right() + 200, view.y + 50) == zoom_from_700(view.right() - 1, view.y + 50), "an anchor right of the view is the view's last column");
        check(zoom_from_700(view.x + 50, view.bottom() + 200) == zoom_from_700(view.x + 50, view.bottom() - 1), "an anchor below the view is the view's last row");
        check(zoom_from_700(view.x, view.y).first != zoom_from_700(view.right() - 1, view.y).first && zoom_from_700(view.x, view.y).second != zoom_from_700(view.x, view.bottom() - 1).second, "(and the anchor decides where the camera goes)");
    }
    app.set_zoom(1.0f, view.x + 100, view.y + 100);
    app.renderer().camera().set_origin(700.0, 700.0, app.sim().grid().width(), app.sim().grid().height());
    const auto before = rig.world_at(view.x + 400, view.y + 250);
    app.set_zoom(2.0f, view.x + 400, view.y + 250);
    check(rig.world_at(view.x + 400, view.y + 250) == before, "the anchor of set_zoom is a point of the picture: its world point stays");
    check(app.zoom_levels() == levels, "the levels that are offered now are the same (a local game on a big map)");
    {   // the device's texture size: the world seen can be no more than a texture holds less the pass's margins, so a small texture raises the limit of the zoom-out (the software renderer says
        // no limit; the test hook says one)
        check(app.renderer().max_world_extent() == 0 && app.zoom_fit().max_world == 0, "the software renderer reports no texture limit: nothing is raised");
        app.renderer().set_texture_side_for_test(1100);
        const int32_t room = 1100 - 64;                                      // (the 64 texels that the pass keeps free of a texture: Renderer::kPassMargin)
        check(app.renderer().max_world_extent() == room && app.zoom_fit().max_world == room, "a device whose textures hold 1100 texels a side leaves a world of 1100 less the margins of the pass: " + std::to_string(room));
        const std::vector<float> small_device = app.zoom_levels();
        const float raised = zoom::floor_zoom(app.zoom_fit());
        check(std::fabs(static_cast<double>(raised) - 762.0 / static_cast<double>(room)) < 1e-6 && small_device.size() == 7 && small_device.front() == 2.0f && small_device[5] == zoom::series(-1) && small_device.back() == raised,
              "GAUNTLET in the wide view on that device: the limit rises to 762 / " + std::to_string(room) + ", the levels end there (2 ... 0.84, 0.7355): " + std::to_string(small_device.size()) + " levels");
        app.renderer().set_texture_side_for_test(0);
        check(app.renderer().max_world_extent() == 0 && app.zoom_levels() == levels, "the device's own answer is back with 0: no limit, the same levels");
    }
    // not in a screen that is not a match
    AppRig setup(Aspect::Wide16x9, 1.0f, false, std::string(), false);
    check(setup.ok && setup.app.state() == AppState::MapSelect, "the setup screen is up");
    if (setup.ok) {
        check(!setup.app.set_zoom(2.0f, 300, 200) && setup.app.zoom() == 1.0f, "set_zoom does nothing outside a match");
        check(!setup.app.view_zoom_allowed(300, 200), "and the wheel is not allowed there");
        notch(setup.app, +1);
        check(setup.app.zoom() == 1.0f, "a wheel event on the setup screen does nothing");
    }
}

namespace {

// The setup screen after a match that was zoomed (a match leaves the renderer's camera at its zoom until the next match sets it): the screen's first frame makes the map preview with the
// live renderer, and it is the same picture at every zoom (pixel for pixel); the wheel and the middle button do nothing on the setup screen, with the preview or the list under the pointer
struct SetupAfterMatch {
    bool ok{false};
    float live_zoom{1.0f};                                     // the camera's zoom while the setup screen is up (the last match's)
    float remembered{1.0f};
    std::vector<uint8_t> preview;                              // the preview's inner square on the canvas, RGBA
    int colours{0};                                            // distinct colours in it (a picture of the map, not the "No preview" box)
    int32_t area_x{0};
    int32_t area_y{0};
};

SetupAfterMatch setup_after_zoomed_match(float level, const std::string& map, int32_t shown_index) {
    SetupAfterMatch out;
    AppRig rig(Aspect::Wide16x9);
    if (!rig.ok) return out;
    Application& app = rig.app;
    const LayoutRect view = app.layout().view();
    app.hud().dismiss_match_start_modal();
    app.note_pointer(view.x + 284, view.y + 183);
    if (level != 1.0f && !app.set_zoom(level, view.x + 284, view.y + 183)) return out;
    if (!app.start_game(maps_dir() + map + ".LVL")) return out;      // (a match that is started at the remembered level, the new match's own camera)
    app.hud().dismiss_match_start_modal();
    if (app.zoom() != level) return out;
    app.return_to_map_select();
    if (app.state() != AppState::MapSelect) return out;
    app.map_select().set_selected_index(shown_index);              // (the list's own selection: the map whose preview is made, not the match's)
    for (int i = 0; i < 6; ++i) app.run_frame_with_delta(0.1f);    // the setup screen's refresh task runs at 500 ms (a frame counts for at most 100 ms): the frame after it, with a map shown, makes the preview
    const Picture frame = read_canvas(app, kWideW, kWideH);
    const LayoutRect area = SetupLayout::of(SetupVariant::Single).preview_area();
    out.area_x = area.x;
    out.area_y = area.y;
    std::vector<uint32_t> seen;
    for (int32_t y = 0; y < area.h; ++y) {
        for (int32_t x = 0; x < area.w; ++x) {
            const uint8_t* p = frame.at(area.x + x, area.y + y);
            out.preview.insert(out.preview.end(), p, p + 4);
            const uint32_t key = static_cast<uint32_t>(p[0]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[2];
            if (std::find(seen.begin(), seen.end(), key) == seen.end()) seen.push_back(key);
        }
    }
    out.colours = static_cast<int>(seen.size());
    out.live_zoom = app.zoom();
    out.remembered = app.remembered_zoom();
    out.ok = true;
    return out;
}

}  // namespace

void test_app_setup_screen() {
    group("setup", "the map preview is the same picture after a match at any zoom; the wheel and the middle button do nothing on the setup screen, the loading screen and the quick help");
    struct Pair {
        const char* match;                                    // the map of the match that is zoomed
        int32_t shown;                                        // the index of the map that the setup screen shows afterwards (the list's own order)
    };
    for (const Pair& pair : {Pair{"TINY", 1}, Pair{"GAUNTLET", 4}}) {
        const std::string name = std::string("match on ") + pair.match + ", list at " + std::to_string(pair.shown) + ": ";
        const SetupAfterMatch at1 = setup_after_zoomed_match(1.0f, pair.match, pair.shown);
        check(at1.ok && at1.preview.size() > 100000u && at1.colours > 30, name + "(setup) the preview after a match at the zoom 1 is a picture of the map (" + std::to_string(at1.colours) + " colours)");
        if (!at1.ok) continue;
        for (const float level : {zoom::series(-1), zoom::series(2), 2.0f}) {                  // (levels that both maps offer: TINY's own limit is 0.768 in this view)
            const std::string label = name + "after a match at the zoom " + zoom::level_name(level) + ": ";
            const SetupAfterMatch other = setup_after_zoomed_match(level, pair.match, pair.shown);
            check(other.ok && other.live_zoom == level && other.remembered == level, label + "the setup screen is up, the camera still has the match's zoom and the level is remembered");
            size_t different = 0;
            if (other.preview.size() == at1.preview.size()) {
                for (size_t i = 0; i < at1.preview.size(); ++i) different += at1.preview[i] != other.preview[i] ? 1u : 0u;
            } else {
                different = 1;
            }
            check(other.ok && other.preview.size() == at1.preview.size() && different == 0, label + "the preview is the zoom 1 preview, byte for byte (" + std::to_string(different) + " bytes differ)");
        }
    }
    // the wheel and the middle button on the setup screen of a zoomed match: nothing moves, nothing is remembered anew
    {
        AppRig rig(Aspect::Wide16x9);
        check(rig.ok, "(setup) the application is up");
        if (rig.ok) {
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            app.hud().dismiss_match_start_modal();
            app.note_pointer(view.x + 284, view.y + 183);
            check(app.set_zoom(2.0f, view.x + 284, view.y + 183), "(setup) the match is zoomed to 2");
            app.return_to_map_select();
            app.render_frame();
            check(app.state() == AppState::MapSelect, "(setup) the setup screen is up");
            const ViewportCamera camera = app.renderer().camera();
            const LayoutRect preview = SetupLayout::of(SetupVariant::Single).preview_area();
            const LayoutRect list = SetupLayout::of(SetupVariant::Single).map_box;
            const std::vector<std::pair<std::pair<int32_t, int32_t>, std::string>> places = {
                {{preview.x + preview.w / 2, preview.y + preview.h / 2}, "the preview"},
                {{list.x + list.w / 2, list.y + list.h / 2}, "the map list"},
                {{view.x + 284, view.y + 183}, "where the match's map view was"},
                {{5, 5}, "the corner of the canvas"},
            };
            for (const auto& place : places) {
                const std::string label = "the setup screen, pointer over " + place.second + ": ";
                app.note_pointer(place.first.first, place.first.second);
                check(!app.view_zoom_allowed(place.first.first, place.first.second), label + "the wheel is not allowed");
                notch(app, +1);
                notch(app, -1);
                notch(app, -1);
                app.handle_mouse_wheel(wheel_event(0, 0.6f));
                app.handle_mouse_wheel(wheel_event(0, 0.6f));
                app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, place.first.first, place.first.second));
                app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONUP, place.first.first, place.first.second));
                const ViewportCamera& now = app.renderer().camera();
                check(app.zoom() == 2.0f && app.remembered_zoom() == 2.0f && now.x == camera.x && now.y == camera.y && now.zoom == camera.zoom, label + "the wheel and the middle button changed nothing (zoom, level, origin)");
                check(app.state() == AppState::MapSelect, label + "the setup screen is still up");
            }
            app.render_frame();
            check(app.zoom() == 2.0f, "(setup) a frame after all of it: the zoom is as it was");
        }
    }
    // the loading screen and the quick help: the same
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false);
        check(rig.ok, "(setup) an application that starts on its loading screen is up");
        if (rig.ok) {
            Application& app = rig.app;
            const AppState first = app.state();
            app.note_pointer(300, 200);
            notch(app, +1);
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 200));
            check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f && !app.view_zoom_allowed(300, 200), "the first screen (" + std::to_string(static_cast<int>(first)) + "): the wheel and the middle button do nothing");
            app.finish_loading();
            const AppState second = app.state();
            notch(app, +1);
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 200));
            check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f && !app.view_zoom_allowed(300, 200), "the screen after loading (" + std::to_string(static_cast<int>(second)) + "): the wheel and the middle button do nothing");
            if (app.state() == AppState::QuickHelp) {
                app.quick_help_key(SDLK_RETURN);
                notch(app, +1);
                check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f, "the setup screen after the quick help: the wheel does nothing");
            }
        }
    }
}

void test_app_fairness_local() {
    group("fair", "a local game and a game with bots offer the zoom-out (and so does a match of the network: the next group)");
    {
        AppRig rig(Aspect::Wide16x9);
        if (rig.ok) {
            check(rig.app.zoom_limits() == zoom::Limits::any() && rig.app.zoom_levels().front() == 2.0f && rig.app.zoom_levels().back() < 0.5f, "a local game: any level, down to the map's limit");
        }
    }
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = base_config(Aspect::Wide16x9);
        ai::BotSpec spec;
        std::string why;
        check(ai::parse_bot_spec("1:easy", spec, why), "a bot spec parses");
        cfg.bots.push_back(spec);
        Application app;
        check(app.init(cfg) && !app.network_active() && app.bots() != nullptr, "a game with a bot is up and is not a game of the network");
        check(app.zoom_limits() == zoom::Limits::any() && app.zoom_levels().size() == 11 && app.zoom_levels().back() < 0.5f, "a game with bots offers the zoom-out, down to the map's limit");
        app.renderer().camera().set_origin(700.0, 700.0, app.sim().grid().width(), app.sim().grid().height());
        app.note_pointer(300, 200);
        notch(app, -1);
        check(app.zoom() == zoom::series(-1), "... and the wheel zooms out in it, one level a notch");
    }
}

namespace {

// ---- the review of v0.1.0: the camera's zoom is always one that is allowed and drawn (no offscreen target, a network match), and a wheel event that is not a number ----
// The frames that a test runs with the program's stderr caught (the Application's and the Renderer's reports go there): returns what was written
std::string frames_with_stderr(Application& app, int frames) {
    std::ostringstream sink;
    std::streambuf* const old = std::cerr.rdbuf(sink.rdbuf());
    for (int i = 0; i < frames; ++i) app.run_frame_with_delta(0.016f);
    std::cerr.rdbuf(old);
    return sink.str();
}

int lines_with(const std::string& text, const std::string& needle) {
    int n = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.find(needle) != std::string::npos) ++n;
    }
    return n;
}

}  // namespace

void test_app_guards() {
    group("guard", "the zoom is always a level that is allowed and drawn: no offscreen target (the hook and a refused texture), a network match, and a wheel that is not a number");
    std::vector<float> all_levels;                                      // (the levels of MEDIUM in the wide view, taken from the first match below)
    const std::vector<float> only_one{1.0f};

    // 1. the target cannot be made (the hook: the pass finds it failing): the zoom drops to 1 at the next frame, only 1 is offered, one line says so, and a later success restores the levels
    for (const float start_zoom : {0.5f, 2.0f}) {
        const std::string at = "from " + num(static_cast<double>(start_zoom)) + ": ";
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), at + "a match is up");
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        app.note_pointer(view.x + 200, view.y + 150);
        if (all_levels.empty()) all_levels = app.zoom_levels();
        check(all_levels.size() == 11 && all_levels.front() == 2.0f && all_levels.back() < 0.5f, at + "(the levels of MEDIUM in the wide view: 11)");
        check(app.set_zoom(start_zoom, view.x + 200, view.y + 150), at + "the zoom is set");
        check(app.zoom() == start_zoom && app.zoom_levels() == all_levels, at + "the zoom is " + num(static_cast<double>(start_zoom)) + " and the whole series is offered");
        check(frames_with_stderr(app, 3).empty() && app.renderer().world_target_passes() > 0, at + "its frames go through the offscreen target and write nothing");
        const auto centre_before = rig.world_at(view.x + view.w / 2, view.y + view.h / 2);
        const uint64_t passes = app.renderer().world_target_passes();
        app.renderer().set_fail_world_target(true);
        check(app.renderer().world_target_failed() && app.zoom_levels() == only_one && app.zoom_limits() == zoom::Limits::only_normal(), at + "while the target fails only the level 1 is offered (at once)");
        const std::string said = frames_with_stderr(app, 60);
        check(app.zoom() == 1.0f, at + "one frame took the camera to the zoom 1 (the picture is the zoom 1: the camera, the clicks, the minimap and the scroll agree with it)");
        check(app.zoom_levels() == only_one, at + "the levels offered are {1}");
        check(app.remembered_zoom() == start_zoom, at + "what the player chose last stays remembered (the failure is not the player's choice)");
        check(lines_with(said, "") == 1 && lines_with(said, "the zoom is 1 until it can be") == 1, at + "60 frames wrote exactly ONE line: \"" + said + "\"");
        check(app.renderer().world_target_passes() == passes, at + "no pass tried the target while it fails");
        const auto centre_after = rig.world_at(view.x + view.w / 2, view.y + view.h / 2);
        check(std::abs(centre_after.first - centre_before.first) <= 2 && std::abs(centre_after.second - centre_before.second) <= 2,
              at + "the drop is anchored at the view's centre: the world point there stays (within the grid of the zoom): (" + std::to_string(centre_before.first) + ", " + std::to_string(centre_before.second) + ") -> (" + std::to_string(centre_after.first) + ", " + std::to_string(centre_after.second) + ")");
        // nothing zooms while it lasts, by wheel, middle button or API
        notch(app, -1);
        notch(app, +1);
        check(app.zoom() == 1.0f, at + "the wheel does nothing");
        check(!app.set_zoom(0.5f, 300, 200) && !app.set_zoom(2.0f, 300, 200) && !app.step_zoom(+1, 300, 200) && !app.step_zoom(-1, 300, 200) && app.zoom() == 1.0f, at + "set_zoom and step_zoom refuse every level but 1");
        // a match that starts now starts at 1 (the remembered level is not offered), and the level that was chosen is still remembered
        check(start_ticked_match(app, "MEDIUM") && app.zoom() == 1.0f && app.remembered_zoom() == start_zoom, at + "a match that starts while it fails starts at 1");
        // it works again: the levels come back at the next frame, and the player's level is offered once more
        app.renderer().set_fail_world_target(false);
        check(!app.renderer().world_target_failed() && app.zoom_levels() == all_levels, at + "when the failure is over the three levels are offered at once");
        check(frames_with_stderr(app, 3).empty(), at + "(and nothing is written)");
        app.note_pointer(view.x + 200, view.y + 150);
        notch(app, start_zoom < 1.0f ? -1 : +1);
        check(app.zoom() == (start_zoom < 1.0f ? zoom::series(-1) : zoom::series(1)), at + "the wheel zooms again, a level a notch");
        // a second failure is a new one: one line again
        app.renderer().set_fail_world_target(true);
        const std::string again = frames_with_stderr(app, 20);
        check(app.zoom() == 1.0f && lines_with(again, "") == 1, at + "a second failure drops the zoom again and writes one line more");
    }

    // 2. the failure of the texture itself (SDL_CreateTexture says no, here by the test hook): the renderer reports it once, the Application drops to 1 and says so once; the renderer tries again
    // now and then and, when it works, the levels are back; a try that fails again writes nothing
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), "(refused texture) a match is up");
        if (rig.ok) {
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            app.note_pointer(view.x + 200, view.y + 150);
            check(frames_with_stderr(app, 2).empty() && app.renderer().world_target_error().empty(), "(refused texture) at the zoom 1 no target is needed and nothing is wrong");
            all_levels = app.zoom_levels();
            app.renderer().set_fail_world_target_creation(true);
            notch(app, +1);
            check(app.zoom() == zoom::series(1), "(refused texture) the wheel zooms in: nothing is known to be wrong yet");
            const std::string first = frames_with_stderr(app, 2);
            check(app.renderer().world_target_failed() && !app.renderer().world_target_error().empty(), "(refused texture) the pass that could not make the target keeps the failure");
            check(app.zoom() == 1.0f && app.zoom_levels() == only_one, "(refused texture) and the next frame has the camera at 1 with only that level offered");
            check(lines_with(first, "[Renderer]") == 1 && lines_with(first, "[Application]") == 1 && lines_with(first, "") == 2, "(refused texture) one line of the renderer (what SDL said) and one of the application: \"" + first + "\"");
            const uint64_t passes = app.renderer().world_target_passes();
            const std::string long_run = frames_with_stderr(app, 2 * Renderer::kWorldTargetRetryFrames + 20);
            check(long_run.empty() && app.renderer().world_target_failed() && app.zoom() == 1.0f && app.renderer().world_target_passes() == passes,
                  "(refused texture) while it still fails the retries write nothing, the zoom stays 1 and no pass uses a target (\"" + long_run + "\")");
            app.renderer().set_fail_world_target_creation(false);
            const std::string recovered = frames_with_stderr(app, Renderer::kWorldTargetRetryFrames + 5);
            check(!app.renderer().world_target_failed() && app.renderer().world_target_error().empty() && recovered.empty(), "(refused texture) once the texture can be made the renderer finds out within its retry interval, silently");
            check(app.zoom_levels() == all_levels && app.zoom() == 1.0f, "(refused texture) the three levels are offered again (the camera stays at the 1 it was taken to)");
            const uint64_t before = app.renderer().world_target_passes();
            notch(app, +1);
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == zoom::series(1) && app.renderer().world_target_passes() > before, "(refused texture) the player zooms in again and the frame is drawn through the target");
        }
    }

    // 3. a camera that something put outside the allowed levels is taken back at the next frame: never below the map's limit (defence in depth: the wheel, the API and the match start
    // already keep to it), and the levels in between are left alone
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), "(clamp) a local match is up");
        if (rig.ok) {
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            app.renderer().camera().set_zoom(0.5f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 0.5f, "(clamp) in a local game a camera at 0.5 is left alone");
            const float limit = zoom::floor_zoom(app.zoom_fit());
            check(limit > 0.39f && limit < 0.4f, "(clamp) the limit of MEDIUM in the wide view is 762 / 1920");
            app.renderer().camera().set_zoom(0.25f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            check(app.zoom() == 0.25f, "(clamp) (forced) the camera is below the map's limit, where the view would show more than the map");
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == limit && app.renderer().camera().x >= 0.0f && app.renderer().camera().x + static_cast<float>(view.w) / limit <= 1920.0f + 1e-3f, "(clamp) the next frame takes it to the map's limit, and the view shows no more than the map");
            check(app.remembered_zoom() == 1.0f, "(clamp) what the player chose last (nothing: the camera was set by hand) is not touched by the correction");
            app.renderer().camera().set_zoom(0.7f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 0.7f, "(clamp) a zoom between two levels (not a level, but inside the limits) is left alone");
            app.renderer().camera().set_zoom(2.0f, view.w / 2, view.h / 2, app.sim().grid().width(), app.sim().grid().height());
            app.run_frame_with_delta(0.016f);
            check(app.zoom() == 2.0f, "(clamp) 2 is the most, and is left alone");
        }
    }
}

// A wheel event whose precise amount is not a number or is infinite is no event: the accumulator keeps what it had, the zoom does not move (static_cast<int>(NaN) is undefined, and a NaN in the
// accumulator would make every later event of the session 0)
void test_app_wheel_garbage() {
    group("garbage", "a wheel event that is not a number is ignored and poisons nothing");
    AppRig rig(Aspect::Wide16x9);
    check(rig.ok && start_ticked_match(rig.app, "MEDIUM"), "a match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.note_pointer(300, 200);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float v : {nan, inf, -inf, -nan}) {
        app.handle_mouse_wheel(wheel_event(0, v));
        check(app.zoom() == 1.0f, "a precise amount of " + num(static_cast<double>(v)) + " does not zoom");
    }
    app.handle_mouse_wheel(wheel_event(3, nan));
    check(app.zoom() == 1.0f, "a NaN with a whole amount of 3 does not zoom either (the event cannot be trusted)");
    g_clock_ms += 2000;
    const std::vector<float> levels = app.zoom_levels();                        // MEDIUM in the wide view: 11 levels, 2 down to 0.397
    check(levels.size() == 11, "(MEDIUM offers 11 levels in the wide view)");
    notch(app, +1);
    check(app.zoom() == zoom::series(1), "a normal notch afterwards still zooms (the accumulator is not poisoned)");
    notch(app, -1);
    check(app.zoom() == 1.0f, "and back");
    // a trackpad's fractions around a NaN: 0.4 + (NaN) + 0.7 is one step with 0.1 left; nothing was forgotten and nothing was added
    app.handle_mouse_wheel(wheel_event(0, 0.4f));
    app.handle_mouse_wheel(wheel_event(0, nan, false, 16));
    check(app.zoom() == 1.0f, "0.4 and a NaN: no step yet");
    app.handle_mouse_wheel(wheel_event(0, 0.7f));
    check(app.zoom() == zoom::series(1), "... and 0.7 completes the step: the 0.4 was not forgotten");
    // an absurd amount is one event's worth at most: the accumulator's bound, 8 levels (the series has 11 here)
    app.handle_mouse_wheel(wheel_event(0, -1.0e30f, false, 2000));
    check(app.zoom() == levels.back(), "an absurd amount toward zooms out as far as there are levels (at most 8 of them)");
    app.handle_mouse_wheel(wheel_event(0, 1.0e30f, false, 2000));
    check(app.zoom() == levels[levels.size() - 1 - static_cast<size_t>(zoom::WheelAccumulator::kMaxSteps)], "an absurd amount away zooms in by 8 levels, the most that one event can ask for");
}

void test_app_settings() {
    group("settings", "the key `zoom` of the settings file, and --zoom");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ants_zoom_view_test_" + std::to_string(static_cast<long>(SDL_GetTicks())) + "_" + std::to_string(reinterpret_cast<uintptr_t>(&g_clock_ms) % 100000));
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "settings.ini").string();
    auto read_file = [&]() {
        std::ifstream in(file);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        check(rig.ok && rig.app.zoom() == 1.0f && rig.app.remembered_zoom() == 1.0f, "no key: the zoom 1");
        check(read_file().find("zoom") == std::string::npos, "a start writes no zoom");
        rig.app.note_pointer(300, 200);
        notch(rig.app, +1);
        check(read_file().find("zoom=1.19") != std::string::npos, "the wheel writes the key at once, the level's name in three digits: zoom=1.19");
        notch(rig.app, -1);
        notch(rig.app, -1);
        check(read_file().find("zoom=0.841") != std::string::npos && read_file().find("zoom=1.19") == std::string::npos, "and the next change replaces it: zoom=0.841");
        check(rig.app.set_zoom(2.0f, 300, 200) && read_file().find("zoom=2") != std::string::npos, "(2 again)");
    }
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        check(rig.ok && rig.app.remembered_zoom() == 2.0f && rig.app.zoom() == 2.0f, "the next run starts at the remembered level, 2");
        check(rig.app.start_game(maps_dir() + "TINY.LVL") && rig.app.zoom() == 2.0f, "and so does a match that starts later");
    }
    {
        AppRig rig(Aspect::Wide16x9, 0.5f, true, file);
        check(rig.ok && rig.app.remembered_zoom() == 0.5f && rig.app.zoom() == 0.5f, "--zoom 0.5 wins over the key");
        check(read_file().find("zoom=2") != std::string::npos, "and it does not change the file");
    }
    {   // any zoom of the range is taken, and a match starts at the nearest level that its map offers in its view (GAUNTLET, wide: 2 ... 0.5, 0.42, 0.397)
        AppRig rig(Aspect::Wide16x9, 1.5f, true, file);
        check(rig.ok && rig.app.remembered_zoom() == 1.5f && rig.app.zoom() == zoom::series(2), "--zoom 1.5 is remembered as it is and the match starts at the nearest level, 1.41");
        AppRig fine(Aspect::Wide16x9, 0.6f, true, file);
        check(fine.ok && fine.app.remembered_zoom() == 0.6f && fine.app.zoom() == zoom::series(-3), "--zoom 0.6 starts at 0.59");
        AppRig low(Aspect::Wide16x9, 0.1f, true, file);
        check(low.ok && low.app.remembered_zoom() == 0.1f && low.app.zoom() == zoom::floor_zoom(low.app.zoom_fit()) && low.app.zoom() > 0.39f && low.app.zoom() < 0.4f, "--zoom 0.1 is beyond the map's limit: the match starts at the limit, 0.397");
        check(low.app.start_game(maps_dir() + "TINY.LVL") && low.app.zoom() == zoom::floor_zoom(low.app.zoom_fit()) && low.app.zoom() > 0.76f && low.app.zoom() < 0.77f && low.app.remembered_zoom() == 0.1f,
              "... and on TINY the limit is 0.768; what was asked for stays remembered");
        check(low.app.start_game(maps_dir() + "TREASURE.LVL") && low.app.zoom() > 0.39f && low.app.zoom() < 0.4f, "... and TREASURE's is 0.397 again");
        AppRig whole(Aspect::Wide16x9, 0.5f, true, file);
        check(whole.ok && whole.app.start_game(maps_dir() + "TINY.LVL") && whole.app.zoom() > 0.76f && whole.app.zoom() < 0.77f, "--zoom 0.5 on TINY (992 px: 0.5 would show more than the map) starts at the limit");
    }
    {   // a settings key is a zoom too: the same rule
        {
            std::ofstream out(file, std::ios::trunc);
            out << "zoom=0.35\n";
        }
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        check(rig.ok && rig.app.remembered_zoom() == 0.35f && rig.app.zoom() == zoom::floor_zoom(rig.app.zoom_fit()), "zoom=0.35 in the file: remembered as it is, the match starts at the map's limit (0.397)");
        AppRig classic(Aspect::Classic4x3, 1.0f, false, file);
        check(classic.ok && classic.app.zoom() == zoom::series(-6), "... and in the classic view, where 0.35 is a level, it starts there");
    }
    for (const char* bad : {"3", "abc", "", "0.04", "2.01", "-1", "0x1", "2 ", "0.5.0"}) {
        {
            std::ofstream out(file, std::ios::trunc);
            out << "zoom=" << bad << "\n";
        }
        std::ostringstream err;
        std::streambuf* old = std::cerr.rdbuf(err.rdbuf());
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file);
        std::cerr.rdbuf(old);
        check(rig.ok && rig.app.remembered_zoom() == 1.0f && rig.app.zoom() == 1.0f, std::string("a settings file with zoom=") + bad + " is ignored, the game starts at 1");
        if (std::string(bad) != "") check(err.str().find("zoom") != std::string::npos, std::string("... and says so: ") + err.str());
    }
    {   // the command line
        auto parse = [](std::vector<std::string> args) {
            std::vector<char*> argv;
            args.insert(args.begin(), "ants");
            for (auto& a : args) argv.push_back(a.data());
            return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
        };
        ApplicationConfig none = parse({"--map", "x.LVL"});
        check(!none.zoom_given && none.zoom == 1.0f && none.startup_error.empty(), "no --zoom: not given, 1");
        for (const char* ok : {"0.5", "1", "2", "2.0", ".5", "1.5", "1.41", "0.397", "0.05", "0.6", "1.99"}) {
            ApplicationConfig c = parse({"--zoom", ok, "--map", "x.LVL"});
            check(c.zoom_given && c.zoom >= zoom::kSmallest && c.zoom <= zoom::kIn && c.startup_error.empty(), std::string("--zoom ") + ok + " is taken");
        }
        for (const char* bad : {"3", "x", "0", "0.04", "2.01", "-1", "1e0", "1,5"}) {
            ApplicationConfig c = parse({"--zoom", bad});
            check(!c.zoom_given && c.startup_error.find("--zoom") != std::string::npos && c.startup_error.find("a zoom from 0.05 to 2") != std::string::npos, std::string("--zoom ") + bad + " is refused: " + c.startup_error);
        }
        ApplicationConfig missing = parse({"--zoom"});
        check(!missing.zoom_given && missing.startup_error.find("--zoom needs") != std::string::npos, "--zoom without a value is refused: " + missing.startup_error);
        // a game that starts with a bad option does not start
        ApplicationConfig refused = parse({"--zoom", "3", "--headless"});
        Application app;
        std::ostringstream err;
        std::streambuf* old = std::cerr.rdbuf(err.rdbuf());
        const bool started = app.init(refused);
        std::cerr.rdbuf(old);
        check(!started, "a game with a refused --zoom does not start");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void test_app_ants() {
    group("ants", "the ants of a real match are drawn at every zoom (after the first simulation ticks; the frames before the first tick are test_prestart_view.cpp's)");
    for (const Aspect aspect : {Aspect::Classic4x3, Aspect::Wide16x9}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const int32_t cw = wide ? kWideW : kClassicW;
        const int32_t ch = wide ? kWideH : kClassicH;
        for (const char* map : {"GAUNTLET", "TINY"}) {
            const std::string at = std::string(wide ? "wide" : "classic") + " " + map + ": ";
            AppRig rig(aspect, 1.0f, true);
            check(rig.ok, at + "the application is up");
            if (!rig.ok) continue;
            Application& app = rig.app;
            app.renderer().pin_animation_clock(1500);
            check(start_ticked_match(app, map), at + "the match starts and ticks");
            check(app.sim().current_tick() >= 3, at + "the simulation has ticked: " + std::to_string(app.sim().current_tick()));
            bool own = false;
            for (const auto& a : app.sim().get_world_state().ants) own = own || a.player_id == 0;
            check(own, at + "the local player has ants");
            const LayoutRect view = app.layout().view();
            check(app.zoom() == 1.0f, at + "the match is at the zoom 1");
            // the camera is put where one of the local player's ants stands (a whole pixel origin on the grid of every zoom: even numbers), so that the ants are in the view at every zoom
            const sim::AntSnapshot* mine = nullptr;
            for (const auto& a : app.sim().get_world_state().ants) {
                if (a.player_id == 0 && mine == nullptr) mine = &a;
            }
            if (mine == nullptr) continue;
            const double ox = static_cast<double>(std::max(0, mine->px - 100) & ~1);
            const double oy = static_cast<double>(std::max(0, mine->py - 110) & ~1);
            ViewportCamera& cam = app.renderer().camera();
            const uint32_t tiles_w = app.sim().grid().width();
            const uint32_t tiles_h = app.sim().grid().height();
            const auto look = [&](float z) {
                cam.zoom = z;
                cam.set_origin(ox, oy, tiles_w, tiles_h);
            };

            // zoom 1: the sprites make a difference to the view that the same world without ants does not have
            look(1.0f);
            const double cx1 = static_cast<double>(cam.x);
            const double cy1 = static_cast<double>(cam.y);
            const Picture with1 = world_with_ants(app, cw, ch);
            const Picture without1 = world_without_ants(app, cw, ch);
            const int shown1 = differ(with1, without1, view);
            check(shown1 > 500, at + "at the zoom 1 the ants are drawn: " + std::to_string(shown1) + " pixels of the view differ from the same world without them");

            // zoom 2 from the same origin: the nearest enlargement of the zoom 1 picture, ants included, and the ants are in it
            look(2.0f);
            check(static_cast<double>(cam.x) == cx1 && static_cast<double>(cam.y) == cy1, at + "the same origin at the zoom 2");
            const Picture with2 = world_with_ants(app, cw, ch);
            const Picture without2 = world_without_ants(app, cw, ch);
            const int bad2 = differ(with2, enlarge2(with1, view, false, false), view);
            check(bad2 == 0, at + "at the zoom 2 the view is the nearest enlargement of the zoom 1 picture, ants included: " + std::to_string(bad2) + " pixels differ");
            const int shown2 = differ(with2, without2, view);
            check(shown2 > 500, at + "at the zoom 2 the ants are drawn: " + std::to_string(shown2));

            // zoom 0.5: the ants are in the picture
            look(0.5f);
            const Picture with05 = world_with_ants(app, cw, ch);
            const Picture without05 = world_without_ants(app, cw, ch);
            const int shown05 = differ(with05, without05, view);
            check(shown05 > 100, at + "at the zoom 0.5 the ants are drawn: " + std::to_string(shown05));
            app.renderer().set_show_hp(true);
        }
    }
}

void test_app_start_view_and_listener() {
    group("start", "the start view keeps the hill in view and the sound listener is the middle of the world that is seen, at every zoom, on every map and for every player");
    const char* maps[6] = {"GAUNTLET", "ISLANDS", "MEDIUM", "SMALL", "TINY", "TREASURE"};
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        for (const float asked : {0.5f, 1.0f, 2.0f, 1.5f, 0.7f, 0.3f}) {
            AppRig rig(aspect, asked, true);
            check(rig.ok, "the application is up");
            if (!rig.ok) continue;
            Application& app = rig.app;
            const LayoutRect view = app.layout().view();
            for (const char* map : maps) {
                check(app.start_game(maps_dir() + map + ".LVL"), std::string("the match on ") + map + " starts");
                // a match starts at the nearest level that its map offers in this view to the one that was asked for (or remembered): 0.5 on TINY is its limit, 0.768
                const float z = app.zoom();
                const std::vector<float> offered = app.zoom_levels();
                check(z == zoom::nearest(asked, offered) && std::find(offered.begin(), offered.end(), z) != offered.end() && app.remembered_zoom() == asked,
                      std::string(wide ? "wide" : "classic") + " " + map + ": a match starts at the nearest level to " + num(static_cast<double>(asked)) + " that the map offers, " + zoom::level_name(z) + ", and the level that was asked for stays remembered");
                if (asked == 1.0f || asked == 2.0f) check(z == asked, std::string(wide ? "wide" : "classic") + " " + map + ": 1 and 2 are offered on every map");
                const int32_t map_w = static_cast<int32_t>(app.sim().grid().width()) * 32;
                const int32_t map_h = static_cast<int32_t>(app.sim().grid().height()) * 32;
                for (uint8_t player = 0; player < 4; ++player) {
                    app.set_local_player(player);
                    const auto* base = app.sim().grid().find_anthill(player);
                    if (base == nullptr) continue;
                    const int32_t ax = (base->x + 1) * 32 + 16;
                    const int32_t ay = (base->y + 1) * 32 + 16;
                    const ViewportCamera& cam = app.renderer().camera();
                    const double vis_w = view.w / static_cast<double>(z);
                    const double vis_h = view.h / static_cast<double>(z);
                    const std::string at = std::string(wide ? "wide" : "classic") + " " + map + " player " + std::to_string(player) + " at " + zoom::level_name(z) + ": ";
                    check(ax >= static_cast<double>(cam.x) && ax < static_cast<double>(cam.x) + vis_w && ay >= static_cast<double>(cam.y) && ay < static_cast<double>(cam.y) + vis_h, at + "the hill's anchor (" + std::to_string(ax) + ", " + std::to_string(ay) + ") is in view from the origin (" + num(cam.x) + ", " + num(cam.y) + ")");
                    // the original's rule over the world that is seen: just far enough right / down to show the square's far edge
                    const double want_x = wide && map_w <= vis_w ? (map_w - vis_w) / 2.0 : std::max(0, std::min(ax + 192, map_w) - zoom::visible(view.w, z));
                    const double want_y = wide && map_h <= vis_h ? (map_h - vis_h) / 2.0 : std::max(0, std::min(ay + 192, map_h) - zoom::visible(view.h, z));
                    // (rounded to the lattice of the zoom and held in the range whose far end is on the lattice: at the levels that are not powers of two the origin is not the whole number of the rule)
                    const auto lattice = [z](double v, double map_px, double vis) { return std::clamp(std::floor(v * static_cast<double>(z) + 0.5) / static_cast<double>(z), 0.0, std::floor((map_px - vis) * static_cast<double>(z) + 1e-9) / static_cast<double>(z)); };
                    if (map_w > vis_w && map_h > vis_h) check(std::fabs(cam.x - static_cast<float>(lattice(want_x, map_w, vis_w))) < 1e-3f && std::fabs(cam.y - static_cast<float>(lattice(want_y, map_h, vis_h))) < 1e-3f, at + "the origin is the original's start view over the world that is seen: (" + num(want_x) + ", " + num(want_y) + "), it is (" + num(cam.x) + ", " + num(cam.y) + ")");
                    // the listener: the middle of the world that is seen
                    app.update_simulation(0.0f);
                    const int32_t listen_x = static_cast<int32_t>(std::floor(cam.x + vis_w / 2.0));
                    const int32_t listen_y = static_cast<int32_t>(std::floor(cam.y + vis_h / 2.0));
                    check(app.audio_mixer().listener_x() == listen_x && app.audio_mixer().listener_y() == listen_y, at + "the sound listener is the middle of the world that is seen: (" + std::to_string(listen_x) + ", " + std::to_string(listen_y) + "), it is (" + std::to_string(app.audio_mixer().listener_x()) + ", " + std::to_string(app.audio_mixer().listener_y()) + ")");
                }
            }
        }
    }
    // the listener follows a zoom
    {
        AppRig rig(Aspect::Wide16x9);
        if (rig.ok) {
            Application& app = rig.app;
            app.note_pointer(300, 200);
            app.update_simulation(0.0f);
            const int32_t at_one = app.audio_mixer().listener_x();
            check(at_one == app.renderer().camera().world_x + 381, "at 1 the listener is the origin plus half the 762 px view");
            check(app.set_zoom(2.0f, 16, 21), "(zoomed in at the view's corner)");
            app.update_simulation(0.0f);
            check(app.audio_mixer().listener_x() == app.renderer().camera().world_x + 190 && app.audio_mixer().listener_x() != at_one, "at 2 the listener is the origin plus half the 381 world pixels that are seen: it moved");
        }
    }
}

void test_local_determinism(const assets::AssetArchive&) {
    group("client", "the zoom is the player's own: the same orders at the same ticks give the same match at any zoom");
    // The same script of orders (to world points) on runs that differ in nothing but the zoom, the scrolling and the pointer (one run for every level of the series, 0.397 to 2): the clicks are
    // computed from the camera so that they hit the same world pixels, and the state hash of the simulation must be the same
    std::vector<sim::StateHash> hashes;
    std::vector<uint64_t> ticks;
    const std::vector<float> run_levels = zooms_for(true);
    for (const float z : run_levels) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        AppRig rig(Aspect::Wide16x9, z, true);
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect view = app.layout().view();
        app.hud().dismiss_match_start_modal();
        const auto* hill = app.sim().grid().find_anthill(0);
        const int32_t hx = hill != nullptr ? hill->x * 32 + 64 : 400;
        const int32_t hy = hill != nullptr ? hill->y * 32 + 64 : 400;
        for (int step = 0; step < 400; ++step) {
            if (step % 40 == 0) {
                // select every ant, then order a move to a world point near the hill by clicking the pixel that shows it (the camera is brought to the hill first)
                app.hud().select_all_friendly(app.sim().get_world_state());
                const int32_t target_x = hx + ((step / 40) % 5) * 70 - 100;
                const int32_t target_y = hy + ((step / 40) % 3) * 90 - 60;
                ViewportCamera& cam = app.renderer().camera();
                cam.center_on(target_x, target_y, app.sim().grid().width(), app.sim().grid().height());
                // find a pixel of the view whose world pixel is in the target's tile (the tile decides the order)
                int32_t cx = -1;
                int32_t cy = -1;
                for (int32_t sy = view.y + 20; sy < view.bottom() - 20 && cx < 0; ++sy) {
                    for (int32_t sx = view.x + 20; sx < view.right() - 20; ++sx) {
                        if (world_under_f(cam.x, z, sx - view.x) / 32 == target_x / 32 && world_under_f(cam.y, z, sy - view.y) / 32 == target_y / 32) {
                            cx = sx;
                            cy = sy;
                            break;
                        }
                    }
                }
                check(cx >= 0, "the target tile of step " + std::to_string(step) + " is in view at the zoom " + zoom::level_name(z));
                if (cx >= 0) {
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, cx, cy));
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, cx, cy));
                }
            }
            if (step % 7 == 0) app.note_pointer(view.x + 100 + (step * 13) % 400, view.y + 80 + (step * 7) % 300);
            app.hud().input_tick(app.renderer().camera(), app.sim().grid().width(), app.sim().grid().height(), view.right() + 3, view.y + 100);     // a scroll-ish poke at the panel's edge
            app.update_simulation(0.05f);
        }
        hashes.push_back(app.sim().state_hash());
        ticks.push_back(app.sim().current_tick());
    }
    bool same_ticks = hashes.size() == run_levels.size() && ticks.size() == run_levels.size();
    bool same_hash = same_ticks;
    for (size_t i = 1; same_ticks && i < ticks.size(); ++i) {
        same_ticks = same_ticks && ticks[i] == ticks[0];
        same_hash = same_hash && hashes[i] == hashes[0];
    }
    check(same_ticks && ticks[0] >= 300, "the runs at every level ran the same number of ticks: " + std::to_string(ticks.empty() ? 0 : ticks[0]));
    check(same_hash, "the state hash of the simulation is the same at every level of the series: " + std::to_string(run_levels.size()) + " runs");
}

// =====================================================================================================================================================
// A match of the network: the same levels as a local game, and the view is the client's own
// =====================================================================================================================================================

void test_network_match() {
    group("net", "a match of the network offers the same levels as a local game (host or guest), and the view stays the client's own");
    // 1. a guest that remembered 0.5, on TINY (992 px: the limit of the zoom-out is 762 / 992 = 0.768 in the wide view)
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(0.5f, true)), "the guest and the host are in the match on TINY");
        if (app.state() == AppState::Playing) {
            check(app.network_active() && !app.net()->is_host(), "it is a match of the network and this machine is a guest");
            const float limit = zoom::floor_zoom(app.zoom_fit());
            check(limit > 0.76f && limit < 0.77f, "the limit of the zoom-out on TINY in the wide view is 0.768: " + num(static_cast<double>(limit)));
            check(app.zoom() == limit && app.remembered_zoom() == 0.5f, "a network match starts at the nearest level to the remembered 0.5 that the map offers, its limit (and 0.5 stays remembered)");
            check(app.zoom_limits() == zoom::Limits::any(), "a match of the network has no limit of its own: the same levels as a local game");
            {   // (the levels that a local game has on TINY in this view, written out: another Application cannot be made here, the end of one closes SDL for the other)
                const std::vector<double> want = {2.0, 1.68179, 1.41421, 1.18921, 1.0, 0.84090, 0.768145};
                const std::vector<float> got = app.zoom_levels();
                bool same = got.size() == want.size();
                for (size_t i = 0; same && i < got.size(); ++i) same = std::fabs(static_cast<double>(got[i]) - want[i]) < 5e-5;
                check(same, "the levels of the match of the network are the levels of a local game on TINY in this view: 2 ... 0.84, 0.768");
            }
            app.hud().dismiss_match_start_modal();
            // the camera is held to the map's limit at every frame, in a match of the network as anywhere, and the level that the player remembered is not touched
            {
                const LayoutRect gview = app.layout().view();
                app.renderer().camera().set_zoom(0.5f, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
                check(app.zoom() == 0.5f && app.network_active(), "(forced) the camera is at 0.5, below TINY's limit, in a match of the network");
                app.run_frame_with_delta(0.016f);
                check(app.zoom() == limit && app.remembered_zoom() == 0.5f, "the next frame takes the camera to the map's limit (the view would show more than the map)");
                app.renderer().camera().set_zoom(2.0f, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
                app.run_frame_with_delta(0.016f);
                check(app.zoom() == 2.0f, "a camera at 2 is left alone");
                app.renderer().camera().set_zoom(0.9f, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
                app.run_frame_with_delta(0.016f);
                check(app.zoom() == 0.9f, "a camera between two levels, inside the limits, is left alone");
                app.renderer().camera().set_zoom(limit, gview.w / 2, gview.h / 2, app.sim().grid().width(), app.sim().grid().height());
            }
            app.note_pointer(300, 200);
            notch(app, -1);
            check(app.zoom() == limit, "the wheel toward does not go below the map's limit");
            check(!app.step_zoom(-1, 300, 200) && app.zoom() == limit, "step_zoom(-1) says no at the limit");
            check(app.set_zoom(0.3f, 300, 200) == false && app.zoom() == limit, "set_zoom(0.3) goes to the nearest level, the limit, where it already is");
            app.handle_mouse_wheel(wheel_event(0, -0.6f));
            app.handle_mouse_wheel(wheel_event(0, -0.6f));
            check(app.zoom() == limit, "a trackpad's creep toward does not go below it either");
            check(app.remembered_zoom() == limit || app.remembered_zoom() == 0.5f, "(what is remembered is the player's own choice)");
            // the wheel away zooms in, a level a notch, and the way back goes to the limit and no further
            notch(app, +1);
            check(app.zoom() == zoom::series(-1), "the wheel away zooms in to the next level, 0.84");
            notch(app, -1);
            check(app.zoom() == limit, "and back to the limit");
            check(app.set_zoom(2.0f, 300, 200) && app.zoom() == 2.0f, "(2 again)");
            app.handle_mouse_button(button_event(SDL_BUTTON_MIDDLE, SDL_MOUSEBUTTONDOWN, 300, 200));
            check(app.zoom() == 1.0f, "the middle button goes back to 1 from 2");
            check(app.remembered_zoom() == 1.0f, "(the player's own choice 1 is what is remembered now)");
        }
        // the match is over for this machine: back to the setup screen
        app.return_to_map_select();
        check(!app.network_active(), "the network is gone");
        check(app.zoom_limits() == zoom::Limits::any(), "a local game offers any level too");
    }
    // 2. a guest on a bigger map: the whole series, the zoom-out included, down to the map's limit, as in a local game
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(0.5f, true), "TREASURE"), "(the second guest is in the match on TREASURE)");
        if (app.state() == AppState::Playing) {
            check(app.network_active() && app.zoom() == 0.5f && app.remembered_zoom() == 0.5f, "a match of the network on TREASURE starts at the remembered 0.5: the zoom-out is offered in a match of the network");
            const std::vector<float> levels = app.zoom_levels();
            check(levels.size() == 11 && levels.front() == 2.0f && levels[8] == 0.5f && levels.back() == zoom::floor_zoom(app.zoom_fit()), "TREASURE offers 2 ... 0.5, 0.42 and its limit 0.397 to a guest");
            app.hud().dismiss_match_start_modal();
            app.note_pointer(300, 200);
            check(app.set_zoom(0.5f, 300, 200) == false, "(at 0.5 already)");
            notch(app, -1);
            check(app.zoom() == levels[9], "the wheel toward zooms out a level, in a match of the network");
            notch(app, -1);
            check(app.zoom() == levels.back(), "... and to the limit");
            notch(app, -1);
            check(app.zoom() == levels.back(), "... and no further");
            check(app.step_zoom(+1, 300, 200) && app.zoom() == levels[9], "step_zoom(+1) goes in a level");
            // a local game that follows keeps what the player chose last
            app.return_to_map_select();
            check(app.start_game(maps_dir() + "TREASURE.LVL") && app.zoom() == levels[9], "a local game after it starts at the level that was chosen last, 0.42");
        }
    }
    // 3. a host: the same levels; the view stays the client's own: a zoomed machine and a machine without a view play the same match
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = network_config(0.5f, true);
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        check(app.init(cfg) && app.network_active() && app.net()->is_host(), "the host application is up");
        Peer bob;
        check(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"), "Bob joins the room");
        Duo duo{app, bob};
        check(duo.until([&]() { return app.net()->can_start(); }, 8000), "the room can start");
        app.map_select().handle_key_down(SDLK_RETURN);
        check(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000), "the match starts on both machines");
        if (app.state() == AppState::Playing) {
            const std::vector<float> host_levels = app.zoom_levels();
            check(host_levels == zoom::levels(app.zoom_fit(), zoom::Limits::any()) && host_levels.size() >= 7 && host_levels.back() < 0.8f, "a host is offered the whole series, the zoom-out included");
            check(app.zoom() == zoom::nearest(0.5f, host_levels) && app.remembered_zoom() == 0.5f, "a host starts at the nearest level to the remembered 0.5 that its map offers");
            const size_t start_index = static_cast<size_t>(std::find(host_levels.begin(), host_levels.end(), app.zoom()) - host_levels.begin());
            app.hud().dismiss_match_start_modal();
            const LayoutRect view = app.layout().view();
            app.note_pointer(view.x + 300, view.y + 200);
            notch(app, +1);
            check(start_index > 0 && start_index < host_levels.size() && app.zoom() == host_levels[start_index - 1], "the host zooms in a level");
            const auto* hill = app.sim().grid().find_anthill(0);
            const int32_t hx = hill != nullptr ? hill->x * 32 + 64 : 400;
            const int32_t hy = hill != nullptr ? hill->y * 32 + 64 : 400;
            // 40 seconds of play: the host orders through its HUD with clicks at the zoom 2 and scrolls; Bob orders through its NetGame
            uint32_t next = 0;
            int host_orders = 0;
            for (uint32_t t = 0; t < 40000; t += 10) {
                if (t >= next) {
                    next = t + 1000;
                    app.hud().select_all_friendly(app.sim().get_world_state());
                    ViewportCamera& cam = app.renderer().camera();
                    cam.center_on(hx + static_cast<int32_t>((t / 1000) % 7) * 40, hy + static_cast<int32_t>((t / 1000) % 5) * 30, app.sim().grid().width(), app.sim().grid().height());
                    const int32_t sx = view.x + view.w / 2 + static_cast<int32_t>((t / 1000) % 9) * 6 - 24;
                    const int32_t sy = view.y + view.h / 2 + static_cast<int32_t>((t / 1000) % 4) * 8 - 12;
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, sx, sy));
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, sx, sy));
                    host_orders += 1;
                    const auto theirs = ants_of(bob.sim, 1);
                    if (!theirs.empty()) {
                        sim::Command c;
                        c.type = sim::CommandType::GroupMove;
                        c.issuer = 1;
                        c.tile_x = static_cast<int16_t>((t / 10) % 31);
                        c.tile_y = static_cast<int16_t>((t / 20) % 31);
                        c.ants.push_back(theirs[(t / 1000) % theirs.size()]);
                        bob.net.submit(c);
                    }
                }
                if (t == 15000) notch(app, -1);                           // back to 1 in the middle of the match
                duo.step(10);
            }
            check(host_orders >= 39, "the host gave its orders");
            app.net()->freeze();                                          // (the host stops sealing turns: Bob catches up with the last one)
            duo.step(3000);
            check(app.sim().state_hash() == bob.sim.state_hash(), "the host (zoomed, scrolled, clicking) and Bob (no view) are in the same state");
            check(!app.net()->desynced() && app.sim().current_tick() == bob.sim.current_tick() && app.sim().current_tick() > 700, "no desync, the same tick: " + std::to_string(app.sim().current_tick()));
        }
    }
    // 4. a guest that zooms and scrolls: the same
    {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(1.0f, false)), "(the zooming guest is in the match)");
        if (app.state() == AppState::Playing) {
            Duo duo{app, host};
            app.hud().dismiss_match_start_modal();
            const LayoutRect view = app.layout().view();
            app.note_pointer(view.x + 300, view.y + 200);
            notch(app, +1);
            uint32_t next = 0;
            for (uint32_t t = 0; t < 30000; t += 10) {
                if (t >= next) {
                    next = t + 900;
                    app.hud().select_all_friendly(app.sim().get_world_state());
                    const int32_t sx = view.x + 60 + static_cast<int32_t>((t / 900) % 11) * 50;
                    const int32_t sy = view.y + 40 + static_cast<int32_t>((t / 900) % 7) * 50;
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, sx, sy));
                    app.handle_mouse_button(button_event(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, sx, sy));
                    app.hud().input_tick(app.renderer().camera(), app.sim().grid().width(), app.sim().grid().height(), view.right() - 2, view.y + 200);
                    const auto theirs = ants_of(host.sim, 0);
                    if (!theirs.empty()) {
                        sim::Command c;
                        c.type = sim::CommandType::GroupMove;
                        c.issuer = 0;
                        c.tile_x = static_cast<int16_t>((t / 10) % 31);
                        c.tile_y = static_cast<int16_t>((t / 25) % 31);
                        c.ants.push_back(theirs[(t / 900) % theirs.size()]);
                        host.net.submit(c);
                    }
                }
                duo.step(10);
            }
            host.net.freeze();
            duo.step(3000);
            check(app.sim().state_hash() == host.sim.state_hash(), "the zoomed guest and the host are in the same state");
            check(!app.net()->desynced() && app.sim().current_tick() == host.sim.current_tick() && app.sim().current_tick() > 500, "no desync, the same tick: " + std::to_string(app.sim().current_tick()));
        }
    }
}

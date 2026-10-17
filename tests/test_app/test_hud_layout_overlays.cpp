// HUD layout, part 3: the chat log, the static shell, the cursors, the button states, the network overlay, the latency corner and the alliance dialog.
// (See test_hud_layout.cpp for what the whole program checks.)
#include "hud_layout_fixtures.hpp"

namespace {

// The chat log window (Ants.exe 0x1011e78 and its line class 0x10123e2, docs 5.56): entries of two labels stacked one pixel apart, the window that follows the newest entry
// (CHATAPPD), the drag that shows earlier lines while the button is held (0x1012015 / 0x1012096 / 0x101228a) and its auto-repeat (CHATSCRL).
uint32_t g_chat_clock = 0;

uint32_t chat_clock() { return g_chat_clock; }

// A renderer with proportional letters: 'i', 'l' and ' ' are 3 px wide, everything else 7 px
class NarrowRenderer : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string&, int32_t, int32_t, assets::ColorRGBA) override {}
    int32_t get_text_width(const std::string& text, FontSize = FontSize::Px12) const override {
        int32_t w = 0;
        for (char c : text) w += (c == 'i' || c == 'l' || c == ' ') ? 3 : 7;
        return w;
    }
    void set_hud_team(uint8_t) override {}
};

}  // namespace

void test_chat_log(const assets::AssetArchive& arc) {
    std::printf("[chat] the log window: one pixel between entries, pixel wrapping, the follow task, the drag and its auto-repeat, the clip\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1);
    ViewportCamera camera;
    auto text_at = [](const RecordingRenderer& rr, const std::string& text) -> const RecordingRenderer::Text* {
        for (const auto& t : rr.texts) if (t.text == text) return &t;
        return nullptr;
    };

    HUD hud;
    hud.init(0);
    hud.set_ticks_function(&chat_clock);
    g_chat_clock = 1000;
    // the start message: a 12 px header and a body of two 12 px lines (36 px); the log ends one pixel below
    check(hud.chat_content_end() == 37 && hud.chat_follow_pos() == 0 && hud.chat_follow_target() == 0, "the start message fills 36 px and the log ends at 37");
    hud.add_chat_entry("Ann", "hi", false, 2);                   // 24 px: top 37, bottom 61
    check(hud.chat_content_end() == 62, "an entry starts one pixel below the one before: 37 + 24 + 1 = 62");
    check(hud.chat_follow_target() == 0, "a log that fits in the 101 px view does not move the window");

    // what is drawn: headers at the view's left edge, bodies 10 px right of it, the log from y = 299, inside a clip of the view's size
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), camera);
        const auto* news = text_at(rr, "[0:00] News Flash:");
        const auto* ann = text_at(rr, "Ann:");
        const auto* hi = text_at(rr, "hi");
        check(news && news->x == 482 && news->y == 299, "the first header is at (482, 299)");
        check(ann && ann->x == 482 && ann->y == 299 + 37, "the next header is 37 px below it (a 36 px entry and one pixel)");
        check(hi && hi->x == 492 && hi->y == 299 + 37 + 12, "a body is 10 px right of its header and one header line below it");
        check(rr.clips.size() == 1 && rr.clips[0] == std::array<int32_t, 4>{482, 299, 138, 101} && rr.clip_clears == 1 && !rr.clip_on, "the log is clipped to its 138 x 101 view");
    }

    // the third entry ends at 112 - 0 > 101: the window has to follow. Its target puts the new bottom on the view's last row (bottom 111 - 101 = 10); the task is due at once
    hud.add_chat_entry("Bob", "hello", false, 3);                // top 62, bottom 86, end 87
    check(hud.chat_follow_target() == 0, "87 px still fit");
    hud.add_chat_entry("Cy", "there", false, 1);                 // top 87, bottom 111, end 112
    check(hud.chat_content_end() == 112 && hud.chat_follow_target() == 10 && hud.chat_follow_pos() == 0, "the log ends at 112: the window's target is bottom - 101 = 10");
    hud.update(sim.get_world_state(), 1);                        // the task's first pass: 5 px
    check(hud.chat_follow_pos() == 5, "the follow task moves the window 5 px at its first pass");
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_follow_pos() == 5, "...and waits 50 ms for the next");
    g_chat_clock += 49;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_follow_pos() == 5, "49 ms are not enough");
    g_chat_clock += 1;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_follow_pos() == 10, "the next pass at 50 ms reaches the target 10");
    g_chat_clock += 50;
    hud.update(sim.get_world_state(), 1);                        // the pass that finds the target reached ends the task
    g_chat_clock += 50;
    hud.add_chat_entry("Di", "x", false, 0);                      // top 112, bottom 136, end 137: target 35; the task was gone, so this starts it again (window at its target)
    check(hud.chat_follow_target() == 35 && hud.chat_follow_pos() == 10, "a new entry moves the target");
    hud.add_chat_entry("Ed", "y", false, 0);                      // top 137, bottom 161, end 162: target 60; the task is running (the window is away from its target): only the target moves
    check(hud.chat_follow_target() == 60, "the target follows the newest bottom");
    for (int pass = 0; pass < 40; ++pass) { g_chat_clock += 50; hud.update(sim.get_world_state(), 1); }
    check(hud.chat_follow_pos() == 60, "the window arrives at 60, 5 px per 50 ms pass and never beyond");

    // the window is drawn from its position: the first entry is out of the view, the partly visible ones are drawn (the clip cuts them), Cy's body ends at the view's last row
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), camera);
        check(text_at(rr, "[0:00] News Flash:") == nullptr, "an entry whose bottom is above the window is not drawn (36 <= 60)");
        const auto* ann = text_at(rr, "Ann:");
        check(ann && ann->y == 299 + 37 - 60, "Ann's header is drawn above the view, at y = 299 + 37 - 60 (only its last rows would show: entry bottom 61 > 60)");
        const auto* ed = text_at(rr, "Ed:");
        check(ed && ed->y == 299 + 137 - 60, "the newest header at 299 + 137 - 60 = 376");
        const auto* y_body = text_at(rr, "y");
        check(y_body && y_body->y == 299 + 137 - 60 + 12 && y_body->y + 12 == 299 + 101, "its body ends on the last row of the view (y + 12 = 400)");
    }

    // The drag (0x1012015): a left press inside the view starts it from the window's position; a press outside or with the right button does nothing
    const int32_t follow = hud.chat_follow_pos();
    hud.handle_mouse_down(470, 350, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a press left of the view starts nothing");
    hud.handle_mouse_down(625, 350, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a press right of the view starts nothing");
    hud.handle_mouse_down(500, 405, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a press below the view (the divider bar) starts nothing");
    hud.handle_mouse_down(500, 350, SDL_BUTTON_RIGHT, sim, camera);
    check(!hud.chat_dragging(), "the right button does not drag the log");
    hud.handle_mouse_up(500, 350, SDL_BUTTON_RIGHT, sim, camera);
    hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim, camera);
    check(hud.chat_dragging() && hud.chat_drag_offset() == follow && hud.chat_view_offset() == follow, "a press inside the view starts the drag at the window's position");
    // a move inside the view scrolls by the distance that the pointer went UP; the window is clamped to [0, end - 101] = [0, 61]
    hud.handle_mouse_motion(500, 380, sim, camera);                // 30 px down: earlier lines
    check(hud.chat_drag_offset() == follow - 30, "30 px down shows 30 px earlier");
    hud.handle_mouse_motion(500, 300, sim, camera);                // 80 px up
    check(hud.chat_drag_offset() == 61, "80 px up is clamped at the end of the log minus the view (162 - 101 = 61)");
    hud.handle_mouse_motion(500, 398, sim, camera);                // 98 px down
    check(hud.chat_drag_offset() == 0, "and at 0 at the top");
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), camera);
        const auto* news = text_at(rr, "[0:00] News Flash:");
        check(news && news->y == 299, "while dragging the log is drawn from the dragged position (here the top)");
    }
    // outside the view a move scrolls nothing; CHATSCRL (100 ms) scrolls 15 px per pass, up while the pointer is below the view and down while it is above it
    hud.handle_mouse_motion(500, 420, sim, camera);                // below the view
    check(hud.chat_drag_offset() == 0, "a move outside the view does not scroll");
    hud.handle_mouse_motion(500, 250, sim, camera);                // above the view
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 15, "above the view: +15 at a pass of CHATSCRL");
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 15, "the next pass is 100 ms later");
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 30, "+15 again");
    hud.handle_mouse_motion(500, 420, sim, camera);                // below the view
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 15, "below the view: -15");
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    g_chat_clock += 100;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 0, "...down to the top and not beyond");
    // the release ends it: the log shows the window that follows the newest entry again, and nothing scrolls any more
    hud.handle_mouse_motion(500, 250, sim, camera);
    hud.handle_mouse_up(500, 250, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging() && hud.chat_view_offset() == hud.chat_follow_pos(), "the release snaps the log back to the newest entries");
    g_chat_clock += 500;
    hud.update(sim.get_world_state(), 1);
    check(hud.chat_drag_offset() == 0 && hud.chat_view_offset() == hud.chat_follow_pos(), "no task scrolls after the release");

    // the window keeps following while it is dragged: the dragged view is not touched
    hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim, camera);
    const int32_t dragged = hud.chat_drag_offset();
    hud.add_chat_entry("Flo", "z", false, 2);
    check(hud.chat_drag_offset() == dragged && hud.chat_follow_target() == hud.chat_content_end() - 1 - 101, "a new entry moves the target, not the dragged position");
    hud.handle_mouse_up(500, 350, SDL_BUTTON_LEFT, sim, camera);

    // a dialog takes the press: with the options screen open the log is not dragged
    hud.open_options();
    hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim, camera);
    check(!hud.chat_dragging(), "a modal window takes every press");
    hud.handle_mouse_up(500, 350, SDL_BUTTON_LEFT, sim, camera);
    hud.close_options();

    // a header that is wider than its 138 px shows its end (the label's tail flag): 6 px per character here, 23 characters fit
    {
        HUD h2;
        h2.init(0);
        h2.add_chat_entry(std::string(30, 'A'), "x", false, 3);
        RecordingRenderer rr(arc);
        h2.render(rr, arc, sim.get_world_state(), camera);
        bool found = false;
        for (const auto& t : rr.texts) if (t.text.size() == 23 && t.text.back() == ':' && t.text[0] == 'A') found = t.x == 482 + 138 - 23 * 6;
        check(found, "a header wider than 138 px is drawn right aligned with its front cut off");
    }

    // the body wraps by PIXELS with the measure that the HUD was given (the original measures a label with GDI when it is made): 'i' is 3 px, so eight "iiii" words (117 px) fit
    {
        NarrowRenderer narrow;
        HUD h3;
        h3.init(0);
        h3.set_text_metrics(&narrow);
        const size_t before = h3.get_chat_log().size();
        h3.add_chat_entry("Ann", "iiii iiii iiii iiii iiii iiii iiii iiii iiii iiii", false, 2);
        const auto& log = h3.get_chat_log();
        check(log.size() == before + 3 && log[before] == "Ann:" && log[before + 1] == "iiii iiii iiii iiii iiii iiii iiii iiii" && log[before + 2] == "iiii iiii",
              "with proportional letters a line holds eight words (39 characters), not 20");
        h3.set_text_metrics(nullptr);                           // the entries are measured again with 6 px per character
        check(h3.get_chat_log().size() > before + 3, "a new measure lays every entry out again");
    }

    // the picture's shape changes while the log is open (the browser's selector): a window that shows the newest line keeps showing it in a shorter view (16:10 has a 221 px log, the classic
    // picture 101 px)
    {
        const ScreenLayout tall = ScreenLayout::with_size(960, 600);
        check(tall.chat_view().h == 221 && ScreenLayout::classic().chat_view().h == 101, "the logs of the 16:10 and the classic picture are 221 and 101 px high");
        HUD h5;
        h5.init(0);
        h5.set_ticks_function(&chat_clock);
        g_chat_clock = 9000;
        h5.set_layout(tall);
        for (int i = 0; i < 14; ++i) h5.add_chat_entry("Ann", "a line of the chat", false, 2);
        for (int i = 0; i < 400 && h5.chat_follow_pos() != h5.chat_follow_target(); ++i) {
            g_chat_clock += 50;
            h5.update(sim.get_world_state(), 1);
        }
        const int32_t end = h5.chat_content_end();
        check(end > 221 + 40 && h5.chat_follow_pos() == h5.chat_follow_target() && h5.chat_follow_pos() == end - 1 - 221, "16:10: the window has followed the newest line to the log's end");
        h5.set_layout(ScreenLayout::classic());
        check(h5.chat_follow_pos() == end - 1 - 101 && h5.chat_follow_target() == h5.chat_follow_pos(), "... in the classic picture's shorter log it still shows the newest line (the last row is the entry's)");
        h5.set_layout(tall);
        check(h5.chat_follow_pos() == end - 1 - 221 && h5.chat_follow_target() == h5.chat_follow_pos(), "... and back in 16:10 it ends at the log's end again");
        h5.set_layout(ScreenLayout::classic());
        h5.set_layout(ScreenLayout::classic());
        check(h5.chat_follow_pos() == end - 1 - 101, "the same shape again changes nothing");

        // the follow task has not run yet: the window is still at the top of the log, away from its target, and the target is the new end
        HUD h7;
        h7.init(0);
        h7.set_ticks_function(&chat_clock);
        h7.set_layout(tall);
        for (int i = 0; i < 14; ++i) h7.add_chat_entry("Cy", "a line of the chat", false, 1);
        h7.set_layout(ScreenLayout::classic());
        check(h7.chat_follow_pos() <= h7.chat_follow_target() && h7.chat_follow_target() == h7.chat_content_end() - 1 - 101, "a window on its way to the newest line is sent to the new end");
    }

    // the transcript the original writes to chat.txt at the end of the program (0x10122d4): "date @ time", a blank line, "header body" per entry. The start of the original's own file:
    {
        HUD h4;
        h4.init(0);
        check(h4.chat_transcript("04/17/98 @ 18:19:56") == "04/17/98 @ 18:19:56\n\n[0:00] News Flash: Game started! Go get that food!\n", "the transcript starts like the original's chat.txt");
        h4.add_chat_entry("Ann", "hello there", true, 2);
        check(h4.chat_transcript("d @ t") == "d @ t\n\n[0:00] News Flash: Game started! Go get that food!\nAnn (To Teammate): hello there\n", "every entry is one line: header, a space, the body");
    }
}

// The static HUD shell is the animation `uishell` (14 parts) drawn last part first, nothing static is drawn twice, the
// chat cover appears only when chat is switched off, and the lunchbox indicator needs every selected ant to carry food.
void test_static_shell(const assets::AssetArchive& arc) {
    std::printf("[hud] static shell composite, chat cover and lunchbox indicator\n");
    sim::WorldState world;
    sim::AntSnapshot carrier{};
    carrier.id = 7; carrier.player_id = 0; carrier.hp = 10; carrier.tile_x = 5; carrier.tile_y = 5;
    carrier.px = 176; carrier.py = 176; carrier.is_holding = true;
    sim::AntSnapshot empty_handed = carrier;
    empty_handed.id = 8; empty_handed.tile_x = 6; empty_handed.px = 208; empty_handed.is_holding = false;
    world.ants.push_back(carrier);
    world.ants.push_back(empty_handed);

    const auto* shell = arc.find_animation("uishell");
    check(shell && !shell->subitems.empty() && shell->subitems[0].frames.size() == 14, "uishell has 14 parts");
    if (!shell || shell->subitems.empty()) return;
    {
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        std::vector<size_t> draw_index;
        for (const auto& part : shell->subitems[0].frames) {
            const std::string name = arc.get_sprite(part.sprite_index).name;
            size_t count = 0;
            size_t at = SIZE_MAX;
            for (size_t i = 0; i < rr.sprites.size(); ++i) {
                if (rr.sprites[i].name != name) continue;
                ++count;
                if (rr.sprites[i].x == part.dx && rr.sprites[i].y == part.dy) at = i;
            }
            check(count == 1 && at != SIZE_MAX, "shell part " + name + " drawn once at (" + std::to_string(part.dx) + "," + std::to_string(part.dy) + ")");
            draw_index.push_back(at);
        }
        bool last_first = true;
        for (size_t k = 1; k < draw_index.size(); ++k) if (draw_index[k] >= draw_index[k - 1]) last_first = false;
        check(last_first, "shell parts are drawn last part first");
    }

    // Chat cover: three tiles, only after chat is switched off in the options
    const auto* cover = arc.find_animation("chatcovr");
    check(cover && !cover->subitems.empty() && cover->subitems[0].frames.size() == 3, "chatcovr has 3 parts");
    if (cover && !cover->subitems.empty()) {
        const std::string tile = arc.get_sprite(cover->subitems[0].frames[0].sprite_index).name;
        HUD hud;
        hud.init(0);
        RecordingRenderer with_chat(arc);
        render_settled(hud, arc, world, with_chat);
        check(with_chat.named(tile).empty(), "chat on: no chat cover");

        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera camera;
        hud.open_options();
        hud.handle_mouse_down(160, 300, 1, sim, camera);   // the options' Chat OFF button spans x 151..200, y 289..313
        hud.handle_mouse_up(160, 300, 1, sim, camera);     // a switch acts at the release
        hud.close_options();
        check(!hud.is_chat_enabled(), "options chat OFF button switches chat off");
        RecordingRenderer no_chat(arc);
        render_settled(hud, arc, world, no_chat);
        check(no_chat.named(tile).size() == 3, "chat off: three cover tiles");
        check(no_chat.has_sprite_at(tile, 478, 421) && no_chat.has_sprite_at(tile, 478, 436) && no_chat.has_sprite_at(tile, 478, 445),
              "chat cover tiles at (478,421), (478,436), (478,445)");
    }

    // Lunchbox indicator UI_LBOX at (597,131)
    const auto* lbox = arc.find_animation("UI_LBOX");
    check(lbox && !lbox->subitems.empty() && !lbox->subitems[0].frames.empty(), "UI_LBOX exists");
    if (lbox && !lbox->subitems.empty() && !lbox->subitems[0].frames.empty()) {
        const std::string name = arc.get_sprite(lbox->subitems[0].frames[0].sprite_index).name;
        auto lunchbox_drawn = [&](std::vector<uint32_t> selection) {
            HUD hud;
            hud.init(0);
            hud.set_selected_ant_ids(std::move(selection));
            RecordingRenderer rr(arc);
            render_settled(hud, arc, world, rr);
            return rr.has_sprite_at(name, 597, 131);
        };
        check(lunchbox_drawn({7}), "lunchbox shown while the only selected ant carries food");
        check(!lunchbox_drawn({8}), "lunchbox hidden while the selected ant carries nothing");
        check(!lunchbox_drawn({7, 8}), "lunchbox hidden unless every selected ant carries food");
    }
}

// Cursor rules (Ants.exe 0x1026d6a, 0x1026d98): the map cursor only applies inside the map view (16,21)-(458,461) and
// stays the plain arrow while a selection box larger than 4 px is dragged.
void test_cursor_rules(const assets::AssetArchive&) {
    std::printf("[hud] cursor view rectangle and drag rule\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 100, 60000);
    sim::WorldState world;
    world.width = 60;
    world.height = 60;
    sim::AntSnapshot enemy{};
    enemy.id = 9; enemy.player_id = 1; enemy.hp = 10; enemy.tile_x = 5; enemy.tile_y = 5; enemy.px = 176; enemy.py = 176;
    sim::AntSnapshot far_east = enemy;
    far_east.id = 10; far_east.tile_x = 13; far_east.px = 443;
    world.ants.push_back(enemy);
    world.ants.push_back(far_east);

    ViewportCamera camera;
    camera.viewport_w = 441;
    camera.viewport_h = 439;
    camera.world_x = 0;
    camera.world_y = 0;

    HUD hud;
    hud.init(0);
    check(hud.evaluate_cursor(193, 198, world, sim.grid(), camera) == CursorType::Select, "an enemy ant under the pointer shows the select cursor");
    check(hud.evaluate_cursor(460, 198, world, sim.grid(), camera) == CursorType::Normal, "no map cursor right of x = 458");

    // Drag box of 10 px: the pointer stays normal
    hud.handle_mouse_down(193, 198, 1, sim, camera);
    hud.handle_mouse_motion(203, 208, sim, camera);
    check(hud.evaluate_cursor(203, 208, world, sim.grid(), camera) == CursorType::Normal, "dragging a box wider than 4 px keeps the plain pointer");
    hud.handle_mouse_up(203, 208, 1, sim, camera);

    // Drag of 2 px: still the map cursor
    HUD small;
    small.init(0);
    small.handle_mouse_down(193, 198, 1, sim, camera);
    small.handle_mouse_motion(195, 199, sim, camera);
    check(small.evaluate_cursor(195, 199, world, sim.grid(), camera) == CursorType::Select, "a drag of 4 px or less keeps the map cursor");
}

// Buttons are drawn from the original's three-state animations with absolute coordinates: up, hover (a small "r"
// label over the up art) and pressed; the setup screen controls sit where those animations put them.
void test_button_states(const assets::AssetArchive& arc) {
    std::printf("[hud] button hover/pressed art, option controls, quit dialog and setup screen\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 100, 60000);
    sim::WorldState world;
    ViewportCamera camera;

    // Top bar: hover draws buthelpr at (484,10) over buthelpu at (476,7); pressed draws buthelpd
    {
        HUD hud;
        hud.init(0);
        hud.handle_mouse_motion(490, 15, sim, camera);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("buthelpr.bmp", 484, 10) && rr.has_sprite_at("buthelpu.bmp", 476, 7), "help hover: r label at (484,10) over the up art");
        check(rr.named("buthelpd.bmp").empty(), "help hover: no pressed art");
        hud.handle_mouse_down(490, 15, 1, sim, camera);
        RecordingRenderer down(arc);
        render_settled(hud, arc, world, down);
        check(down.has_sprite_at("buthelpd.bmp", 476, 7), "help pressed: buthelpd at (476,7)");
        hud.handle_mouse_up(490, 15, 1, sim, camera);
    }
    {
        HUD hud;
        hud.init(0);
        hud.handle_mouse_motion(600, 15, sim, camera);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("butquitr.bmp", 588, 10) && rr.has_sprite_at("butquitu.bmp", 579, 7), "quit hover: r label at (588,10)");
        hud.handle_mouse_motion(540, 15, sim, camera);
        RecordingRenderer opt(arc);
        render_settled(hud, arc, world, opt);
        check(opt.has_sprite_at("butoptr.bmp", 528, 10) && opt.has_sprite_at("butoptu.bmp", 525, 7), "options hover: r label at (528,10)");
    }
    // Send-to buttons: hover label of All at (543,447)
    {
        HUD hud;
        hud.init(0);
        hud.handle_mouse_motion(550, 450, sim, camera);
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("butallr.bmp", 543, 447) && rr.has_sprite_at("butallu.bmp", 532, 443), "All hover: r label at (543,447) over the up art at (532,443)");
    }

    // The Sound slider applies the option at the RELEASE and then plays the test voice (gantrdy, 14); the Music slider applies at the release; a press changes nothing and
    // dragging only moves the thumb (FUN_010115ca, FUN_0101161b, callback 0x1015058); the applied value is what the thumb stands for: (pos - 211) 100 / 185
    {
        HUD hud;
        hud.init(0);
        std::vector<int32_t> applied_sfx;
        std::vector<int32_t> applied_music;
        std::vector<uint32_t> played;
        hud.set_on_sfx_volume([&](int32_t v) { applied_sfx.push_back(v); });
        hud.set_on_music_volume([&](int32_t v) { applied_music.push_back(v); });
        hud.set_on_play_sfx([&](uint32_t s) { played.push_back(s); });
        hud.open_options();
        hud.handle_mouse_down(300, 185, 1, sim, camera);                      // press on the Sound slider
        check(hud.options_screen().slider(0).position() == 395 && hud.get_sound_volume() == 100, "sound slider: the press moves nothing");
        hud.handle_mouse_motion(330, 185, sim, camera);                       // drag
        hud.handle_mouse_motion(350, 185, sim, camera);
        check(hud.options_screen().slider(0).thumb_left() == 327, "sound slider: the thumb follows the pointer (x 350 - 23)");
        check(applied_sfx.empty() && played.empty(), "sound slider: nothing is applied and no voice plays while the thumb is dragged");
        hud.handle_mouse_up(350, 185, 1, sim, camera);
        check(applied_sfx.size() == 1 && applied_sfx[0] == 75, "sound slider: the option is applied once, at the release: (350 - 211) 100 / 185 = 75");
        check(played.size() == 1 && played[0] == sim::SoundID::GeneralReady, "sound slider: the test voice gantrdy (14) plays after it");
        played.clear();
        hud.handle_mouse_down(300, 222, 1, sim, camera);                      // the Music slider
        hud.handle_mouse_motion(260, 222, sim, camera);
        check(applied_music.empty(), "music slider: nothing is applied while dragging");
        hud.handle_mouse_up(260, 222, 1, sim, camera);
        check(applied_music.size() == 1 && applied_music[0] == 26 && played.empty(), "music slider: applied once at the release (26), no voice");
        check(applied_sfx.size() == 1, "and the sound option stays as it was");
        // every event is preceded by the move (FUN_0102737e calls FUN_0102653f first): the release moves the thumb to the pointer even when no motion came between, so a
        // plain click sets the value of the clicked position; the far end of the track is 99, the Sound Volume 100 of the start is out of reach
        HUD other;
        other.init(0);
        std::vector<int32_t> again;
        other.set_on_sfx_volume([&](int32_t v) { again.push_back(v); });
        other.open_options();
        other.handle_mouse_down(300, 185, 1, sim, camera);
        other.handle_mouse_up(300, 185, 1, sim, camera);
        check(again.size() == 1 && again[0] == 48, "a click on the Sound slider without a move sets the value of the clicked position: (300 - 211) 100 / 185 = 48");
        other.handle_mouse_down(418, 190, 1, sim, camera);
        other.handle_mouse_up(418, 190, 1, sim, camera);
        check(again.size() == 2 && again[1] == 99, "a click at the far end: 99");
    }

    // Option screen controls (op_screen): toggles, Return, slider thumbs at the original's defaults 100 / 65 / 50
    {
        HUD hud;
        hud.init(0);
        hud.open_options();
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(hud.is_chat_enabled() && hud.is_quick_help_enabled(), "options default: chat on, quick help on");
        check(hud.get_sound_volume() == 100 && hud.get_music_volume() == 65 && hud.get_scroll_speed() == 50, "options default: sound 100, music 65, scroll 50");
        check(rr.has_sprite_at("optond.bmp", 102, 289) && rr.has_sprite_at("dbutoffu.bmp", 151, 290), "chat on: ON down at (102,289), OFF up at (151,290)");
        check(rr.has_sprite_at("optond.bmp", 355, 289) && rr.has_sprite_at("dbutoffu.bmp", 404, 290), "quick help on: ON down at (355,289), OFF up at (404,290)");
        check(rr.has_sprite_at("breturn1.bmp", 351, 425), "Return button up art at (351,425)");
        check(rr.has_sprite_at("slidd.bmp", 372, 178) && rr.has_sprite_at("slidd.bmp", 309, 215) && rr.has_sprite_at("slidd.bmp", 281, 252),
              "slider thumbs at x = 188 + min(184, 185 v / 99) on rows 178 / 215 / 252");
        // hover over the OFF toggle of the chat pair shows its hover art (op_coffr: r label over the up art)
        hud.handle_mouse_motion(170, 300, sim, camera);
        RecordingRenderer hov(arc);
        render_settled(hud, arc, world, hov);
        check(hov.has_sprite_at("optoffr.bmp", 158, 292), "chat OFF hover: optoffr label at (158,292)");
        // Return pressed: breturn3 at (353,427)
        hud.handle_mouse_down(400, 440, 1, sim, camera);
        RecordingRenderer down(arc);
        render_settled(hud, arc, world, down);
        check(down.has_sprite_at("breturn3.bmp", 353, 427), "Return pressed: breturn3 at (353,427)");
        hud.handle_mouse_up(400, 440, 1, sim, camera);
        check(!hud.is_options_open(), "releasing Return closes the options");
        // Toggle hit rectangles are the union of the resting and pressed art; a switch acts at the release
        hud.open_options();
        auto click = [&](int32_t x, int32_t y) {
            hud.handle_mouse_down(x, y, 1, sim, camera);
            hud.handle_mouse_up(x, y, 1, sim, camera);
        };
        click(102, 289);
        click(150, 312);
        check(hud.is_chat_enabled(), "(150,312) is outside the chat OFF toggle");
        click(151, 289);
        check(!hud.is_chat_enabled(), "(151,289) is inside the chat OFF toggle");
        click(102, 289);
        check(hud.is_chat_enabled(), "(102,289) is inside the chat ON toggle");
        click(404, 289);
        check(!hud.is_quick_help_enabled(), "(404,289) is inside the quick help OFF toggle (the startup help is skipped)");
        click(355, 289);
        check(hud.is_quick_help_enabled(), "(355,289) is inside the quick help ON toggle");
        hud.handle_mouse_down(151, 289, 1, sim, camera);
        check(hud.is_chat_enabled(), "a press on a switch changes nothing");
        hud.handle_mouse_up(151, 289, 1, sim, camera);
        hud.close_options();
    }

    // The options screen's controls in every state (FUN_01011281: up, hover, down, down + hover) and its edit fields (FUN_010119a8, label FUN_0102b36a): 12 px text in
    // (239, 231, 223) at the field's corner, the caret "_" behind the text, blinking every 150 ms from the focus; a focused text that does not fit shows its end
    {
        auto drew = [&](const RecordingRenderer& rr, const char* anim) {
            const auto* seq = arc.find_animation(anim);
            if (!seq || seq->subitems.empty()) return false;
            for (const auto& part : seq->subitems[0].frames) {
                if (!rr.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx, part.dy)) return false;
            }
            return true;
        };
        for (const char* name : {"op_conu", "op_conr", "op_cond", "op_condr", "op_coffu", "op_coffr", "op_coffd", "op_coffdr", "op_honu", "op_honr", "op_hond", "optondr", "op_hoffu",
                                 "op_hoffr", "op_hoffd", "op_hoffdr", "breturn1", "breturn2", "breturn3", "op_screen"}) {
            check(arc.find_animation(name) != nullptr, std::string("the options animation ") + name + " exists");
        }
        HUD hud;
        hud.init(0);
        hud.set_ticks_function(&test_clock);
        g_now_ms = 1000;
        hud.open_options();
        ViewportCamera cam;
        auto frame = [&](uint32_t now) {
            g_now_ms = now;
            RecordingRenderer rr(arc);
            hud.render(rr, arc, world, cam);
            return rr;
        };
        auto text_at = [&](const RecordingRenderer& rr, const std::string& text, int32_t x, int32_t y) {
            for (const auto& t : rr.texts) {
                if (t.text == text && t.x == x && t.y == y && t.size == FontSize::Px12 && t.color.r == 239 && t.color.g == 231 && t.color.b == 223) return true;
            }
            return false;
        };
        RecordingRenderer f = frame(1000);
        check(text_at(f, "Now you are in for it!", 92, 370) && text_at(f, "Let me be!", 92, 402) && text_at(f, "Attack!", 302, 370) && text_at(f, "Do you want to ally?", 302, 402),
              "the four quick chats at the corners of their fields (92, 370), (92, 402), (302, 370), (302, 402) in 12 px (239, 231, 223)");
        check(text_at(f, "_", 92 + 22 * 6, 370), "the F9 field has the focus: the caret right behind its text");
        check(text_at(frame(1149), "_", 92 + 22 * 6, 370) && !text_at(frame(1150), "_", 92 + 22 * 6, 370) && !text_at(frame(1299), "_", 92 + 22 * 6, 370) &&
                  text_at(frame(1300), "_", 92 + 22 * 6, 370),
              "the caret shows 150 ms, is gone 150 ms, shows again (from the moment the screen opened)");
        // a press on the F10 field moves the focus and the caret
        hud.handle_mouse_down(150, 410, 1, sim, camera);
        RecordingRenderer g = frame(1400);
        check(text_at(g, "_", 92 + 10 * 6, 402) && !text_at(g, "_", 92 + 22 * 6, 370), "a press on F10: the caret is behind its text (the phase starts at the press: visible)");
        // typed text appears at once
        hud.handle_text_input(" ok");
        check(text_at(frame(1400), "Let me be! ok", 92, 402) && hud.get_quick_chat_key(1) == "Let me be! ok", "typed text shows in the field and is the quick chat");
        // a click on the window takes the focus: no caret at all
        hud.handle_mouse_down(300, 330, 1, sim, camera);
        RecordingRenderer h = frame(1400);
        bool any_caret = false;
        for (const auto& t : h.texts) if (t.text == "_" && t.color.r == 239 && t.color.g == 231 && t.color.b == 223) any_caret = true;
        check(!any_caret, "no field has the focus: no caret");
        // a text that does not fit (135 px here: 22 characters of 6 px): focused it shows its end, unfocused its beginning
        hud.options().quick_chat[0] = "0123456789abcdefghijklmnopqrstuvwxyz";
        hud.close_options();
        hud.open_options();
        RecordingRenderer j = frame(2000);
        check(text_at(j, "efghijklmnopqrstuvwxyz", 92 + 135 - 132, 370) && text_at(j, "_", 92 + 135, 370), "focused and too long: the end shows, right aligned before the caret's room");
        hud.handle_mouse_down(300, 330, 1, sim, camera);
        RecordingRenderer k = frame(2000);
        check(text_at(k, "0123456789abcdefghijkl", 92, 370), "unfocused: the beginning shows, cut at the right edge of the field");
        hud.close_options();

        // the pictures of a switch in every state
        hud.open_options();
        hud.handle_mouse_motion(170, 300, sim, camera);                         // over Chat OFF (not latched)
        RecordingRenderer a1 = frame(3000);
        check(drew(a1, "op_coffr") && !drew(a1, "op_coffdr"), "hover over an unlatched switch: the hover picture");
        hud.handle_mouse_down(170, 300, 1, sim, camera);
        RecordingRenderer a2 = frame(3000);
        check(drew(a2, "op_coffd"), "pressed: the down picture");
        hud.handle_mouse_motion(172, 300, sim, camera);
        RecordingRenderer a3 = frame(3000);
        check(drew(a3, "op_coffdr"), "held down and moved inside: down + hover");
        hud.handle_mouse_up(172, 300, 1, sim, camera);
        hud.update(world, 1);                                                    // the 50 ms poll of the pointer
        RecordingRenderer a4 = frame(3000);
        check(drew(a4, "op_coffdr") && drew(a4, "op_conu"), "released: Chat OFF is latched (down + hover under the pointer), Chat ON is up");
        hud.handle_mouse_motion(300, 330, sim, camera);
        hud.update(world, 1);
        RecordingRenderer a5 = frame(3000);
        check(drew(a5, "op_coffd") && !drew(a5, "op_coffdr") && drew(a5, "op_conu"), "the pointer elsewhere: Chat OFF down, Chat ON up");
        hud.close_options();
    }

    // The chat input box is the edit control of FUN_0100dbe2 (docs 5.51): the rectangle (481, 424) - (620, 436), 12 px letters in (7, 11, 15), a caret that toggles every
    // 150 ms from the moment the screen was built, the end of a text that does not fit, nothing while the chat option is off
    {
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 5000;
        hud.init(0);                                              // the control gets the focus now: the caret origin is 5000
        ViewportCamera cam;
        auto frame = [&](uint32_t now) {
            g_now_ms = now;
            RecordingRenderer rr(arc);
            hud.render(rr, arc, world, cam);
            return rr;
        };
        auto text_at = [&](const RecordingRenderer& rr, const std::string& text, int32_t x, int32_t y) {
            for (const auto& t : rr.texts) {
                if (t.text == text && t.x == x && t.y == y && t.size == FontSize::Px12 && t.color.r == 7 && t.color.g == 11 && t.color.b == 15) return true;
            }
            return false;
        };
        check(text_at(frame(5000), "_", 481, 424) && text_at(frame(5149), "_", 481, 424), "empty box: the caret at its corner (481, 424), visible for the first 150 ms");
        check(!text_at(frame(5150), "_", 481, 424) && !text_at(frame(5299), "_", 481, 424) && text_at(frame(5300), "_", 481, 424), "then gone for 150 ms, then visible again");
        hud.set_chat_input("hello");
        RecordingRenderer a = frame(5000);
        check(text_at(a, "hello", 481, 424) && text_at(a, "_", 481 + 5 * 6, 424), "the text at (481, 424) in (7, 11, 15), the caret right behind it");
        // a text wider than the box (139 px less the caret's 6 here: 22 characters of 6 px) shows its end, the caret at the right end of the box
        hud.set_chat_input("0123456789abcdefghijklmnopqrstuvwxyz");
        RecordingRenderer b = frame(5000);
        check(text_at(b, "efghijklmnopqrstuvwxyz", 481 + 133 - 132, 424) && text_at(b, "_", 481 + 133, 424), "a text that does not fit shows its end; the caret at the right end");
        // chat off: the box is hidden and covered
        hud.open_options();
        hud.handle_mouse_down(160, 300, 1, sim, cam);
        hud.handle_mouse_up(160, 300, 1, sim, cam);
        hud.close_options();
        RecordingRenderer c = frame(5000);
        bool any = false;
        for (const auto& t : c.texts) if (t.color.r == 7 && t.color.g == 11 && t.color.b == 15 && (t.text == "_" || t.text.rfind("0123", 0) == 0 || t.text.find("efgh") != std::string::npos)) any = true;
        check(!any, "with the chat option off nothing of the input box is drawn");
    }

    // Quit dialog buttons: yes1 at (180,260); pressed yes3 has its part at (0,1) relative to the origin
    {
        HUD hud;
        hud.init(0);
        hud.open_quit_dialog();
        RecordingRenderer rr(arc);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("yes1.bmp", 180, 260) && rr.has_sprite_at("no1.bmp", 292, 260), "quit dialog: yes1 (180,260) and no1 (292,260)");
        hud.handle_mouse_down(200, 270, 1, sim, camera);
        RecordingRenderer down(arc);
        render_settled(hud, arc, world, down);
        check(down.has_sprite_at("yes3.bmp", 180, 261), "yes pressed: yes3 one pixel lower at (180,261)");
        hud.handle_mouse_up(700, 700, 1, sim, camera);
    }

    // Setup screen: every button has the rectangle of its resting / hovering picture and that of its pressed picture (the original's hit test is the rectangle of the
    // picture that shows, docs 5.52); the Fog of War pair keeps the union of its pictures; art at the animation coordinates
    {
        auto union_of = [&](const char* a, const char* b) {
            const UIRect ra = animation_bounds(arc, a);
            const UIRect rb = animation_bounds(arc, b);
            const int32_t x0 = std::min(ra.x, rb.x), y0 = std::min(ra.y, rb.y);
            const int32_t x1 = std::max(ra.x + ra.w, rb.x + rb.w), y1 = std::max(ra.y + ra.h, rb.y + rb.h);
            return UIRect{x0, y0, x1 - x0, y1 - y0};
        };
        auto same = [&](const UIRect& r, int32_t x, int32_t y, int32_t w, int32_t h) { return r.x == x && r.y == y && r.w == w && r.h == h; };
        auto same_pressed = [&](const UIRect& r, const ButtonRect& b) { return same(r, b.x, b.y, b.w, b.h); };
        using MS = MapSelectScreen;
        check(same(animation_bounds(arc, "up1"), MS::BTN_UP_X, MS::BTN_UP_Y, MS::BTN_UP_W, MS::BTN_UP_H) && same(animation_bounds(arc, "up2"), MS::BTN_UP_X, MS::BTN_UP_Y, MS::BTN_UP_W, MS::BTN_UP_H),
              "setup: up button rect = up1 = up2");
        check(same_pressed(animation_bounds(arc, "up3"), MS::BTN_UP_PRESSED), "setup: up button pressed rect = up3");
        check(same(animation_bounds(arc, "down1"), MS::BTN_DOWN_X, MS::BTN_DOWN_Y, MS::BTN_DOWN_W, MS::BTN_DOWN_H) && same(animation_bounds(arc, "down2"), MS::BTN_DOWN_X, MS::BTN_DOWN_Y, MS::BTN_DOWN_W, MS::BTN_DOWN_H),
              "setup: down button rect = down1 = down2");
        check(same_pressed(animation_bounds(arc, "down3"), MS::BTN_DOWN_PRESSED), "setup: down button pressed rect = down3");
        check(same(animation_bounds(arc, "start1"), MS::BTN_START_X, MS::BTN_START_Y, MS::BTN_START_W, MS::BTN_START_H) && same(animation_bounds(arc, "start2"), MS::BTN_START_X, MS::BTN_START_Y, MS::BTN_START_W, MS::BTN_START_H),
              "setup: start button rect = start1 = start2 = (526, 439, 98, 27)");
        check(same_pressed(animation_bounds(arc, "start3"), MS::BTN_START_PRESSED) && MS::BTN_START_PRESSED == ButtonRect{527, 443, 97, 24}, "setup: start button pressed rect = start3 = (527, 443, 97, 24)");
        check(same(animation_bounds(arc, "leave1"), MS::BTN_QUIT_X, MS::BTN_QUIT_Y, MS::BTN_QUIT_W, MS::BTN_QUIT_H) && same(animation_bounds(arc, "leave2"), MS::BTN_QUIT_X, MS::BTN_QUIT_Y, MS::BTN_QUIT_W, MS::BTN_QUIT_H),
              "setup: leave button rect = leave1 = leave2");
        check(same_pressed(animation_bounds(arc, "leave3"), MS::BTN_QUIT_PRESSED), "setup: leave button pressed rect = leave3");
        check(same(union_of("d_on1", "d_on3"), MS::BTN_FOW_ON_X, MS::BTN_FOW_ON_Y, MS::BTN_FOW_ON_W, MS::BTN_FOW_ON_H), "setup: Fog of War ON rect");
        check(same(union_of("d_off1", "d_off3"), MS::BTN_FOW_OFF_X, MS::BTN_FOW_OFF_Y, MS::BTN_FOW_OFF_W, MS::BTN_FOW_OFF_H), "setup: Fog of War OFF rect");

        MapSelectScreen screen;
        screen.init();
        RecordingRenderer early(arc);
        screen.render(early, arc);                                                  // before the refresh (500 ms): the buttons are there, nothing else
        check(early.has_sprite_at("bstart1.bmp", 526, 439) && early.texts.empty() && early.named("thumb1.bmp").empty(),
              "setup: before the first refresh (500 ms) the buttons show, the labels, the portrait and the thumb do not");
        screen.update(0.5f);
        RecordingRenderer rr(arc);
        screen.render(rr, arc);
        check(rr.has_sprite_at("bstart1.bmp", 526, 439), "setup: START up art at (526,439)");
        check(rr.has_sprite_at("up1.bmp", 226, 299) && rr.has_sprite_at("down1.bmp", 226, 323), "setup: up (226,299) and down (226,323)");
        check(rr.has_sprite_at("bleave1.bmp", 525, 12), "setup: leave at (525,12)");
        check(rr.has_sprite_at("dbutonu.bmp", 524, 373) && rr.has_sprite_at("optoffd.bmp", 574, 372), "setup: Fog of War off shows ON up (524,373) and OFF down (574,372)");
        check(rr.has_sprite_at("thumb1.bmp", 540, 95), "setup: thumbs-up at (540,95)");
        bool portrait_ok = false;
        for (const auto& sp : rr.sprites) {
            if (sp.name.rfind("agst30", 0) == 0 && sp.x >= 383 && sp.x <= 385 && sp.y >= 85 && sp.y <= 87) portrait_ok = true;
        }
        check(portrait_ok, "setup: portrait agst30x drawn from its origin (395,115) with the part offset");
        click(screen, MS::BTN_FOW_ON_X + 2, MS::BTN_FOW_ON_Y + 2);
        RecordingRenderer on(arc);
        screen.render(on, arc);
        check(on.has_sprite_at("optond.bmp", 522, 372) && on.has_sprite_at("dbutoffu.bmp", 576, 373), "setup: Fog of War on shows ON down (522,372) and OFF up (576,373)");
        screen.handle_mouse_motion(MS::BTN_START_X + 5, MS::BTN_START_Y + 5);
        RecordingRenderer hov(arc);
        screen.render(hov, arc);
        check(hov.has_sprite_at("bstart2.bmp", 526, 439), "setup: START hover art");
        screen.handle_mouse_down(MS::BTN_UP_X + 3, MS::BTN_UP_Y + 3, 1);
        RecordingRenderer prs(arc);
        screen.render(prs, arc);
        check(prs.has_sprite_at("up3.bmp", 224, 301), "setup: up pressed art at (224,301)");
    }
}

// The line that a network match shows at the top of the playfield (net_overlay.hpp): which one wins, and the words of the lag policy of a server ("Catching up...",
// "Bob is lagging (12 s behind)").
void test_net_overlay() {
    std::printf("[overlay] the line of a network match: priorities and words\n");
    NetOverlayInput in;
    check(net_overlay_line(in).text.empty() && !net_overlay_line(in).alarm, "overlay: nothing to say says nothing");
    in.notice = "Bob is the host now.";
    check(net_overlay_line(in).text == "Bob is the host now." && !net_overlay_line(in).alarm, "overlay: the match notice is shown when nothing else is");
    // the lag policy of a server
    in.lag_seat = 1;
    in.lag_name = "Bob";
    in.lag_behind_ms = 12000;
    check(net_overlay_line(in).text == "Bob is lagging (12 s behind)" && !net_overlay_line(in).alarm, "overlay: the others are told who lags, in seconds");
    in.lag_behind_ms = 12499;
    check(net_overlay_line(in).text == "Bob is lagging (12 s behind)", "overlay: 12.499 s is 12 s");
    in.lag_behind_ms = 12500;
    check(net_overlay_line(in).text == "Bob is lagging (13 s behind)", "overlay: 12.5 s is 13 s (rounded)");
    in.lag_behind_ms = 3000;
    check(net_overlay_line(in).text == "Bob is lagging (3 s behind)", "overlay: the notice starts at 3 s");
    in.lag_behind_ms = 59000;
    check(net_overlay_line(in).text == "Bob is lagging (59 s behind)", "overlay: and goes on to the drop at 60 s");
    in.lag_name.clear();
    in.lag_seat = 2;
    check(net_overlay_line(in).text == "Player 3 is lagging (59 s behind)", "overlay: a player without a name is \"Player\" and its seat's number");
    in.lag_seat = -1;
    check(net_overlay_line(in).text == "Bob is the host now.", "overlay: no notice any more: the match notice again");
    in.lag_seat = 4;
    check(net_overlay_line(in).text == "Bob is the host now.", "overlay: a seat that does not exist is no notice");
    in.lag_seat = 0;
    in.lag_name = "Ann";
    // the server's word that THIS player is the one who lags (its link is slow: nothing is queued in its game, so nothing else tells it): before the notice about the others
    in.self_lag_behind_ms = 12000;
    check(net_overlay_line(in).text == "You are lagging (12 s behind)" && !net_overlay_line(in).alarm, "overlay: a player whose own link is slow is told that it lags, in seconds");
    in.self_lag_behind_ms = 12499;
    check(net_overlay_line(in).text == "You are lagging (12 s behind)", "overlay: the own notice rounds like the others': 12.499 s is 12 s");
    in.self_lag_behind_ms = 12500;
    check(net_overlay_line(in).text == "You are lagging (13 s behind)", "overlay: 12.5 s is 13 s");
    in.self_lag_behind_ms = 3000;
    check(net_overlay_line(in).text == "You are lagging (3 s behind)", "overlay: from 3 s");
    in.self_lag_behind_ms = 0;
    check(net_overlay_line(in).text == "Ann is lagging (59 s behind)", "overlay: no notice about itself: the one about the others shows");
    in.self_lag_behind_ms = 8000;
    // this machine's own state comes before the others'
    in.catching_up = true;
    check(net_overlay_line(in).text == "Catching up...", "overlay: a machine that is more than 3 s behind says so before it says who else lags (and before it says that it lags)");
    in.stalled_ms = 999;
    check(net_overlay_line(in).text == "Catching up...", "overlay: a wait shorter than a second is not shown");
    in.stalled_ms = 1000;
    check(net_overlay_line(in).text == "Waiting for the other players...", "overlay: a machine that has waited a second for a turn says so, before the rest");
    in.waiting_for = "Cat";
    check(net_overlay_line(in).text == "Waiting for Cat...", "overlay: a host with a seat names the player that holds the game up");
    in.electing = true;
    check(net_overlay_line(in).text == "The host left. Choosing a new host...", "overlay: an election comes before a wait");
    in.desynced = true;
    check(net_overlay_line(in).text == "Out of sync: the match has stopped." && net_overlay_line(in).alarm, "overlay: a desync comes first and is drawn in red");
    check(NET_WAIT_MESSAGE_MS == 1000, "overlay: the wait message shows at one second");

    // ---- the way back, the missing seats, the vote and the countdown (net_overlay.hpp rules 1 - 4) ----
    check(net_overlay_clock(0) == "0:00" && net_overlay_clock(12) == "0:12" && net_overlay_clock(60) == "1:00" && net_overlay_clock(125) == "2:05" && net_overlay_clock(3720) == "62:00",
          "overlay: the clock is m:ss");
    {   // 1. this machine's own way back: two lines, the seconds, the attempt from the second link on; it comes before everything else
        NetOverlayInput w;
        w.reconnecting = true;
        w.away_s = 12;
        w.attempts = 1;
        NetOverlayLine l = net_overlay_line(w);
        check(l.lines.size() == 2 && l.lines[0] == "Connection lost. Reconnecting... 0:12" && l.lines[1] == "Esc leaves the match" && l.text == l.lines[0] && !l.alarm && !l.vote.open,
              "overlay: the way back says Connection lost and the seconds, and that Esc leaves the match");
        w.attempts = 0;
        check(net_overlay_line(w).lines[0] == "Connection lost. Reconnecting... 0:12", "overlay: before the first link there is no attempt to name");
        w.attempts = 2;
        check(net_overlay_line(w).lines[0] == "Connection lost. Reconnecting... 0:12 (attempt 2)", "overlay: the first attempt has failed: the second is named");
        w.attempts = 3;
        w.away_s = 125;
        check(net_overlay_line(w).lines[0] == "Connection lost. Reconnecting... 2:05 (attempt 3)", "overlay: minutes and seconds, and the third attempt");
        // ... above every other rule
        w.desynced = true;
        w.electing = true;
        w.stalled_ms = 5000;
        w.waiting_for = "Cat";
        w.catching_up = true;
        w.self_lag_behind_ms = 9000;
        w.lag_seat = 1;
        w.lag_name = "Bob";
        w.lag_behind_ms = 9000;
        w.notice = "Bob is the host now.";
        w.held_ms = 5000;
        w.missing = {NetOverlaySeat{1, "Bob", 40, false, 0}};
        w.vote_open = true;
        w.vote_seat = 1;
        w.vote_name = "Bob";
        w.resume_seconds_left = 7;
        l = net_overlay_line(w);
        check(l.lines.size() == 2 && l.lines[0] == "Connection lost. Reconnecting... 2:05 (attempt 3)" && !l.alarm && !l.vote.open, "overlay: the way back comes before every other line, a desync and a vote included");
        w.reconnecting = false;
        w.way_back_catching_up = true;
        l = net_overlay_line(w);
        check(l.lines.empty() && l.text.empty() && !l.vote.open, "overlay: while the match is given to this machine the catch-up screen is drawn instead of the match: no overlay");
    }
    {   // 3. the seats that are missing: a second of pause first, one line each, at most three, longest away first
        NetOverlayInput m;
        m.missing = {NetOverlaySeat{1, "Bob", 42, false, 0}};
        m.held_ms = 999;
        check(net_overlay_line(m).lines.empty(), "overlay: a blip of less than a second is not worth a banner");
        m.held_ms = NET_HELD_MESSAGE_MS;
        NetOverlayLine l = net_overlay_line(m);
        check(l.lines.size() == 1 && l.lines[0] == "Bob (Red) lost the connection, waiting 0:42" && l.text == l.lines[0] && !l.alarm && !l.vote.open, "overlay: a seat that is missing: name, color and the seconds");
        m.missing = {NetOverlaySeat{0, "Ann", 65, false, 0}, NetOverlaySeat{2, "", 20, false, 0}, NetOverlaySeat{3, "Dee", 7, true, 45}};
        l = net_overlay_line(m);
        check(l.lines.size() == 3 && l.lines[0] == "Ann (Green) lost the connection, waiting 1:05" && l.lines[1] == "Player 3 (Blue) lost the connection, waiting 0:20" &&
                  l.lines[2] == "Dee (Black) is coming back... 45%",
              "overlay: one line each in the order given (longest away first), a nameless seat is \"Player 3\", a seat that is back says how far it has caught up");
        m.missing.push_back(NetOverlaySeat{1, "Bob", 1, false, 0});
        check(net_overlay_line(m).lines.size() == 3, "overlay: at most three lines");
        m.missing = {NetOverlaySeat{3, "Dee", 7, true, 250}};
        check(net_overlay_line(m).lines[0] == "Dee (Black) is coming back... 100%", "overlay: the percent is never more than 100");
        // they come before today's lines (a desync is not shown while a seat is missing: the match is held anyway)
        m.desynced = true;
        m.notice = "x";
        check(net_overlay_line(m).lines[0] == "Dee (Black) is coming back... 100%" && !net_overlay_line(m).alarm, "overlay: the missing seats come before today's lines");
    }
    {   // 2. a vote: the block, the counts, this player's choice, and no wait for a second; a seat that is back but keeps flapping has a line of its own
        NetOverlayInput v;
        v.missing = {NetOverlaySeat{1, "Bob", 45, false, 0}};
        v.vote_open = true;
        v.vote_seat = 1;
        v.vote_name = "Bob";
        v.votes_continue = 1;
        v.voters = 2;
        v.held_ms = 0;                                                           // (a seat with a long total absence that is lost again: the vote is open at once)
        NetOverlayLine l = net_overlay_line(v);
        check(l.lines.size() == 1 && l.lines[0] == "Bob (Red) lost the connection, waiting 0:45" && l.vote.open && l.vote.count == "1 of 2 voted to continue" && l.vote.keep == "F2 Keep waiting" &&
                  l.vote.go_on == "F3 Continue without Bob" && !l.vote.keep_pressed && !l.vote.go_on_pressed,
              "overlay: a vote shows the seat that is missing, the count and the two buttons with their keys, at once");
        v.my_vote = NetOverlayInput::Choice::Continue;
        l = net_overlay_line(v);
        check(l.vote.go_on_pressed && !l.vote.keep_pressed, "overlay: this player's choice is the button that is pressed (continue)");
        v.my_vote = NetOverlayInput::Choice::KeepWaiting;
        l = net_overlay_line(v);
        check(l.vote.keep_pressed && !l.vote.go_on_pressed, "overlay: and keep waiting");
        v.my_vote = NetOverlayInput::Choice::None;
        v.vote_name.clear();
        v.vote_seat = 2;
        v.missing = {NetOverlaySeat{2, "", 45, false, 0}};
        check(net_overlay_line(v).vote.go_on == "F3 Continue without Player 3", "overlay: a seat without a name is \"Player 3\" on the button too");
        // a seat that flaps and is back: nobody is missing, the vote is about it, the block names it
        NetOverlayInput f;
        f.vote_open = true;
        f.vote_seat = 3;
        f.vote_name = "Dee";
        f.votes_continue = 0;
        f.voters = 2;
        l = net_overlay_line(f);
        check(l.lines.size() == 1 && l.lines[0] == "Dee (Black) keeps losing the connection" && l.vote.open && l.vote.count == "0 of 2 voted to continue" && l.vote.go_on == "F3 Continue without Dee",
              "overlay: a seat that keeps losing its connection and is back is put to the vote: the block says which seat");
        // a vote comes before the countdown and before today's lines
        f.resume_seconds_left = 7;
        f.back_name = "Dee";
        f.desynced = true;
        l = net_overlay_line(f);
        check(l.lines.size() == 1 && l.lines[0] == "Dee (Black) keeps losing the connection" && !l.alarm, "overlay: a vote comes before the countdown and before today's lines");
    }
    {   // 4. the countdown after a pause: the seat that came back, or nobody; only when nothing above applies
        NetOverlayInput c;
        c.resume_seconds_left = 7;
        c.back_name = "Bob";
        NetOverlayLine l = net_overlay_line(c);
        check(l.lines.size() == 1 && l.lines[0] == "Bob is back: the match goes on in 7" && !l.vote.open, "overlay: the countdown names the seat that came back");
        c.back_name.clear();
        check(net_overlay_line(c).lines[0] == "The match goes on in 7", "overlay: and says nothing of anybody when nobody came back (the others voted to go on without it)");
        c.resume_seconds_left = 0;
        check(net_overlay_line(c).lines.empty(), "overlay: no countdown, no line");
        c.resume_seconds_left = 10;
        c.desynced = true;
        c.notice = "Bob is the host now.";
        c.lag_seat = 1;
        c.lag_behind_ms = 9000;
        c.stalled_ms = 2000;
        check(net_overlay_line(c).lines.size() == 1 && net_overlay_line(c).lines[0] == "The match goes on in 10" && !net_overlay_line(c).alarm, "overlay: the countdown comes before today's lines");
    }
    {   // the priorities as a table: the states that apply (in the columns) against the first line that is shown
        struct Row {
            const char* what;
            bool way_back, vote, missing, countdown, desync, wait, lag;
            const char* first;
        };
        const Row rows[] = {
            {"nothing",                                false, false, false, false, false, false, false, ""},
            {"lag only",                               false, false, false, false, false, false, true,  "Bob is lagging (9 s behind)"},
            {"wait and lag",                           false, false, false, false, false, true,  true,  "Waiting for the other players..."},
            {"desync and wait",                        false, false, false, false, true,  true,  false, "Out of sync: the match has stopped."},
            {"countdown and desync",                   false, false, false, true,  true,  false, false, "Bob is back: the match goes on in 7"},
            {"missing and countdown",                  false, false, true,  true,  false, false, false, "Bob (Red) lost the connection, waiting 0:45"},
            {"missing and wait and desync",            false, false, true,  false, true,  true,  false, "Bob (Red) lost the connection, waiting 0:45"},
            {"vote and missing",                       false, true,  true,  false, false, false, false, "Bob (Red) lost the connection, waiting 0:45"},
            {"vote and countdown and lag",             false, true,  false, true,  false, false, true,  "Bob (Red) keeps losing the connection"},
            {"way back and vote and missing",          true,  true,  true,  true,  true,  true,  true,  "Connection lost. Reconnecting... 0:05"},
            {"way back and wait",                      true,  false, false, false, false, true,  false, "Connection lost. Reconnecting... 0:05"},
            {"way back and desync",                    true,  false, false, false, true,  false, false, "Connection lost. Reconnecting... 0:05"},
        };
        for (const Row& r : rows) {
            NetOverlayInput t;
            t.reconnecting = r.way_back;
            t.away_s = 5;
            t.attempts = 1;
            t.held_ms = 3000;
            if (r.vote || r.missing) t.missing = {NetOverlaySeat{1, "Bob", 45, false, 0}};
            if (r.vote) {
                t.vote_open = true;
                t.vote_seat = 1;
                t.vote_name = "Bob";
                t.voters = 2;
                if (!r.missing) t.missing.clear();
            }
            if (r.countdown) {
                t.resume_seconds_left = 7;
                t.back_name = "Bob";
            }
            t.desynced = r.desync;
            if (r.wait) t.stalled_ms = 4000;
            if (r.lag) {
                t.lag_seat = 1;
                t.lag_name = "Bob";
                t.lag_behind_ms = 9000;
            }
            const NetOverlayLine line = net_overlay_line(t);
            const std::string first = line.lines.empty() ? std::string() : line.lines[0];
            if (first != r.first) std::printf("    row \"%s\": the first line is \"%s\"\n", r.what, first.c_str());
            check(first == r.first && line.text == first, "overlay: the priorities table (way back, vote, missing, countdown, today's lines)");
            check(line.alarm == (!r.way_back && !r.vote && !r.missing && !r.countdown && r.desync), "overlay: only a desync that nothing above hides is red");
        }
    }
    {   // a name is cut at the name: every line and every label fits the width that the caller gives, whatever the name is
        const std::string name32(32, 'W');
        for (const int32_t width : {422, 400, 380}) {                                                          // (the lines' own words are 330 px of the 8 px font: the name gets what is left)
            NetOverlayInput f;
            f.max_width = width;
            f.measure = [](const std::string& text) { return static_cast<int32_t>(text.size()) * 8; };         // (a font of 8 px a character)
            f.held_ms = 5000;
            f.missing = {NetOverlaySeat{1, name32, 42, false, 0}, NetOverlaySeat{2, name32, 41, true, 99}, NetOverlaySeat{3, "", 40, false, 0}};
            f.vote_open = true;
            f.vote_seat = 1;
            f.vote_name = name32;
            f.votes_continue = 1;
            f.voters = 3;
            NetOverlayLine l = net_overlay_line(f);
            check(l.lines.size() == 3 && l.vote.open, "overlay: the lines and the block are made for a name of 32 characters");
            for (const std::string& text : l.lines) {
                if (f.measure(text) > width) std::printf("    width %d: \"%s\" is %d px\n", width, text.c_str(), f.measure(text));
                check(f.measure(text) <= width, "overlay: every line fits the width, the name cut");
            }
            check(f.measure(l.vote.count) <= width, "overlay: the count fits");
            check(f.measure(l.vote.keep) + 16 + 8 + f.measure(l.vote.go_on) + 16 <= width, "overlay: the two buttons fit in one row");
            check(l.lines[0].find("...") != std::string::npos, "overlay: a name that does not fit ends with the dots, at the name");
            check(l.lines[0].find("lost the connection, waiting 0:42") != std::string::npos && l.lines[1].find("is coming back... 99%") != std::string::npos,
                  "overlay: what is cut is the name, never the seconds or the percent");
            check(l.vote.go_on.compare(0, 20, "F3 Continue without ") == 0, "overlay: and the label keeps its key and its words");
        }
        NetOverlayInput tiny;                                                     // a width that nothing fits in: the name is gone, the words stay, and nothing loops or breaks
        tiny.max_width = 40;
        tiny.measure = [](const std::string& text) { return static_cast<int32_t>(text.size()) * 8; };
        tiny.held_ms = 5000;
        tiny.missing = {NetOverlaySeat{1, name32, 42, false, 0}};
        tiny.vote_open = true;
        tiny.vote_seat = 1;
        tiny.vote_name = name32;
        check(net_overlay_line(tiny).lines.size() == 1 && net_overlay_line(tiny).lines[0].compare(0, 3, "...") == 0 && net_overlay_line(tiny).vote.go_on == "F3 Continue without ...", "overlay: a width that nothing fits in leaves the dots for the name");
        NetOverlayInput plain;                                                    // no measure, no limit: nothing is cut
        plain.held_ms = 5000;
        plain.missing = {NetOverlaySeat{1, name32, 42, false, 0}};
        check(net_overlay_line(plain).lines[0] == name32 + " (Red) lost the connection, waiting 0:42", "overlay: without a measure nothing is cut");
    }
    {   // where it all stands: the first line where it always stood, the others under it, the block under them; everything inside the view, at the original's picture and at the wide one
        const LayoutRect views[] = {LayoutRect{16, 21, 442, 440}, ScreenLayout::with_size(960, 540).view()};
        for (const LayoutRect& view : views) {
            NetOverlayMetrics m;
            m.line_w = {300, 250, 200};
            m.text_h = 17;
            m.count_w = 150;
            m.keep_w = 100;
            m.go_on_w = 180;
            const NetOverlayLayout lay = net_overlay_layout(view, m, true);
            const NetOverlayBox first = net_overlay_box(view, 300, 17);
            check(lay.lines.size() == 3 && lay.lines[0].text_x == first.text_x && lay.lines[0].text_y == first.text_y && lay.lines[0].box == first.box, "overlay layout: the first line is where the one line always was");
            for (size_t i = 1; i < lay.lines.size(); ++i) {
                check(lay.lines[i].box.y >= lay.lines[i - 1].box.y + lay.lines[i - 1].box.h, "overlay layout: a line stands under the one before it, boxes apart");
            }
            check(lay.has_vote && lay.count.box.y >= lay.lines.back().box.y + lay.lines.back().box.h && lay.keep.y >= lay.count.box.y + lay.count.box.h, "overlay layout: the count line is under the lines and the buttons under it");
            check(lay.keep.x + lay.keep.w < lay.go_on.x && lay.keep.y == lay.go_on.y && lay.keep.h == lay.go_on.h, "overlay layout: the two buttons stand side by side, a gap between");
            check(lay.keep.w == 100 + 16 && lay.go_on.w == 180 + 16 && lay.keep_text_x == lay.keep.x + 8 && lay.go_on_text_x == lay.go_on.x + 8, "overlay layout: a button is its label and 8 px on both sides");
            const int32_t row_left = lay.keep.x;
            const int32_t row_right = lay.go_on.x + lay.go_on.w;
            check(row_left >= view.x && row_right <= view.x + view.w && lay.keep.y + lay.keep.h <= view.y + view.h, "overlay layout: the buttons are inside the view");
            check(std::abs((row_left - view.x) - (view.x + view.w - row_right)) <= 2, "overlay layout: the row is centred");
            for (const NetOverlayBox& b : lay.lines) check(b.box.x >= view.x && b.box.x + b.box.w <= view.x + view.w, "overlay layout: every line is inside the view");
            const NetOverlayLayout none = net_overlay_layout(view, m, false);
            check(!none.has_vote && none.lines.size() == 3, "overlay layout: without a vote there is no block");
            check(net_overlay_max_width(view) == view.w - 20, "overlay layout: a line may be the view's width less its boxes' sides and a margin");
        }
    }
    {   // what the browser check reads of the lines (ants_probe 16 and 10000 +, read-only): how many there are, and each character of each line
        const auto text_of = [](const NetOverlayLine& overlay, int line) {
            std::string out;
            for (int index = 0; index < 1000; ++index) {
                const int code = net_overlay_probe(overlay, 10000 + 1000 * line + index);
                if (code <= 0) break;
                out.push_back(static_cast<char>(code));
            }
            return out;
        };
        const NetOverlayLine none;
        check(net_overlay_probe(none, 16) == 0 && net_overlay_probe(none, 10000) == -1, "overlay probe: nothing shown is no line, and no character");
        NetOverlayLine shown;
        shown.lines = {"Bob is back: the match goes on in 7", "Esc leaves the match", ""};
        check(net_overlay_probe(shown, 16) == 3, "overlay probe: it counts the lines");
        check(text_of(shown, 0) == "Bob is back: the match goes on in 7" && text_of(shown, 1) == "Esc leaves the match" && text_of(shown, 2).empty(), "overlay probe: the characters of each line, read one at a time, are the line");
        check(net_overlay_probe(shown, 10000 + 35) == 0 && net_overlay_probe(shown, 10000 + 999) == 0 && net_overlay_probe(shown, 10000 + 1000 * 2) == 0, "overlay probe: past the end of a line (or an empty one) is 0");
        check(net_overlay_probe(shown, 10000 + 1000 * 3) == -1 && net_overlay_probe(shown, 10000 + 1000 * 9 + 5) == -1, "overlay probe: a line that is not there is -1");
        NetOverlayLine accent;
        accent.lines = {std::string("\xE9\xFF")};
        check(net_overlay_probe(accent, 10000) == 0xE9 && net_overlay_probe(accent, 10001) == 0xFF, "overlay probe: a byte is a code from 0 to 255 (never a negative number)");
        for (const int what : {-1, 0, 15, 17, 100, 9999}) check(net_overlay_probe(shown, what) == -1, "overlay probe: any other number is -1");
        NetOverlayLine wide;
        std::string long_text;
        for (int i = 0; i < 350; ++i) long_text.push_back(static_cast<char>('A' + (i * 7 + i / 26) % 26));
        wide.lines = {"x", long_text};
        check(text_of(wide, 1) == long_text && net_overlay_probe(wide, 10000 + 1000 + 350) == 0 && net_overlay_probe(wide, 10000 + 1000 + 999) == 0,
              "overlay probe: a line of 350 characters is read to its last character (the index runs to 999), and past it is 0");
        NetOverlayInput lost;
        lost.reconnecting = true;
        lost.away_s = 12;
        const NetOverlayLine real = net_overlay_line(lost);
        check(net_overlay_probe(real, 16) == 2 && text_of(real, 0) == "Connection lost. Reconnecting... 0:12" && text_of(real, 1) == "Esc leaves the match", "overlay probe: it reads what the model makes for a lost connection");
    }
}

// The network's share of the corner: "ping NN ms" and "delay NN ms" next to the frame rate (latency_corner.hpp). The strings, where the readout is drawn at all, the
// layout on each screen at 640 x 480 (the recording renderer measures 6 px per character: the real font's geometry is checked with the application's renderer in
// test_network_app) and the draw calls: two texts, in the frame rate's style and size, and nothing else.
void test_latency_corner(const assets::AssetArchive& arc) {
    std::printf("[latency] ping / delay next to the frame rate: strings, which screens, layout, draw calls\n");
    using NP = net::NetGame::Phase;
    // the strings; nothing measured yet is "-"
    check(ping_text(std::nullopt) == "ping -" && delay_text(std::nullopt) == "delay -", "latency: nothing measured is shown as a dash");
    check(ping_text(0u) == "ping 0 ms" && ping_text(42u) == "ping 42 ms" && ping_text(1234u) == "ping 1234 ms", "latency: \"ping NN ms\"");
    check(delay_text(0u) == "delay 0 ms" && delay_text(230u) == "delay 230 ms" && delay_text(9999u) == "delay 9999 ms", "latency: \"delay NN ms\"");
    check(ping_text(10000u) == "ping 9999 ms" && delay_text(4000000000u) == "delay 9999 ms", "latency: a number is cut at four digits (the widest text is bounded)");

    // which screens: only the room and the match of a network game; nothing for a game of one machine, whatever the phase and the screen
    for (const NP phase : {NP::Off, NP::Connecting, NP::Room, NP::Loading, NP::Playing, NP::Over, NP::Failed}) {
        for (const CornerScreen screen : {CornerScreen::Other, CornerScreen::Setup, CornerScreen::Match, CornerScreen::Results}) {
            check(!latency_left_limit(false, phase, screen).has_value(), "latency: a game of one machine draws nothing new, on any screen");
        }
    }
    check(latency_left_limit(true, NP::Room, CornerScreen::Setup) == LATENCY_LEFT_LIMIT_SETUP, "latency: the room shows it");
    check(latency_left_limit(true, NP::Loading, CornerScreen::Setup) == LATENCY_LEFT_LIMIT_SETUP, "latency: the room while the map loads shows it");
    check(latency_left_limit(true, NP::Playing, CornerScreen::Match) == LATENCY_LEFT_LIMIT_MATCH, "latency: the match shows it");
    check(latency_left_limit(true, NP::Playing, CornerScreen::Results) == LATENCY_LEFT_LIMIT_RESULTS, "latency: the results screen of the match shows it");
    for (const NP phase : {NP::Off, NP::Connecting, NP::Over, NP::Failed}) {
        for (const CornerScreen screen : {CornerScreen::Setup, CornerScreen::Match, CornerScreen::Results}) {
            check(!latency_left_limit(true, phase, screen).has_value(), "latency: a connection that is being made, is over or failed shows nothing");
        }
    }
    for (const NP phase : {NP::Room, NP::Loading, NP::Playing}) {
        check(!latency_left_limit(true, phase, CornerScreen::Other).has_value(), "latency: the loading and quick help screens show nothing");
    }

    // the corner row as the application computes it (render_frame): [version] [sparkline plate] [frame rate]; the recording renderer's text is 6 px per character
    RecordingRenderer probe(arc);
    const int32_t text_w = probe.get_text_width("60 FPS", FontSize::Px12);
    const int32_t text_h = probe.get_text_height(FontSize::Px12);
    const int32_t text_x = 632 - text_w;
    const int32_t spark_x = text_x - 36 - 6;
    const int32_t ver_x = spark_x - probe.get_text_width("v0.0.91", FontSize::Px12) - 6;
    const int32_t text_y = FPS_OVERLAY_SPARK_Y + (FPS_OVERLAY_SPARK_H - text_h) / 2;
    const int32_t widest = probe.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + probe.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);

    // a number never moves the layout: for every ping and delay the choice between the row and the two lines is the same on a screen, and every text stays on the screen
    for (const int32_t limit : {LATENCY_LEFT_LIMIT_SETUP, LATENCY_LEFT_LIMIT_MATCH, LATENCY_LEFT_LIMIT_RESULTS}) {
        const bool first_stacked = layout_latency_corner(probe.get_text_width(ping_text(0u), FontSize::Px12), probe.get_text_width(delay_text(0u), FontSize::Px12), widest, text_h, ver_x, text_y, limit).stacked;
        bool constant = true;
        bool on_screen = true;
        for (const uint32_t ms : {0u, 9u, 10u, 99u, 100u, 999u, 1000u, 9999u, 20000u}) {
            const std::string p = ping_text(ms);
            const std::string d = delay_text(ms);
            const LatencyCornerLayout l = layout_latency_corner(probe.get_text_width(p, FontSize::Px12), probe.get_text_width(d, FontSize::Px12), widest, text_h, ver_x, text_y, limit);
            constant = constant && l.stacked == first_stacked;
            on_screen = on_screen && l.ping_x >= 0 && l.delay_x >= 0 && l.ping_x + probe.get_text_width(p, FontSize::Px12) <= 640 && l.delay_x + probe.get_text_width(d, FontSize::Px12) <= 640 &&
                        l.ping_y >= 0 && l.delay_y + text_h <= 480;
        }
        check(constant, "latency: the layout does not change with the numbers");
        check(on_screen, "latency: every text lies on the 640 x 480 screen");
    }

    // setup screen and results screen: one row to the left of the version, in the corner row, clear of the version and of the left limit
    for (const int32_t limit : {LATENCY_LEFT_LIMIT_SETUP, LATENCY_LEFT_LIMIT_RESULTS}) {
        const std::string p = ping_text(1234u);
        const std::string d = delay_text(1234u);
        const int32_t pw = probe.get_text_width(p, FontSize::Px12);
        const int32_t dw = probe.get_text_width(d, FontSize::Px12);
        const LatencyCornerLayout l = layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, limit);
        check(!l.stacked && l.ping_y == text_y && l.delay_y == text_y, "latency: where the row has room, both texts stand in the corner row, level with the frame rate");
        check(l.ping_x >= limit && l.ping_x + pw + LATENCY_TEXT_GAP == l.delay_x && l.delay_x + dw + LATENCY_VERSION_GAP == ver_x,
              "latency: the row ends 6 px left of the version, its texts are 8 px apart, and it begins at or right of the screen's limit");
    }
    {   // the setup screen's prompt box (36 .. 329 wide, its picture to 336) is left of the row for the largest numbers
        const LatencyCornerLayout l = layout_latency_corner(probe.get_text_width(ping_text(9999u), FontSize::Px12), probe.get_text_width(delay_text(9999u), FontSize::Px12), widest, text_h, ver_x, text_y, LATENCY_LEFT_LIMIT_SETUP);
        check(!l.stacked && l.ping_x >= MapSelectScreen::LABEL_X + MapSelectScreen::STATUS_W + 4, "latency: on the setup screen even the largest numbers stay right of the prompt box");
    }

    // match screen: the bottom row is the score boxes, so two lines above the corner row, right aligned with the frame rate and clear of the sparkline plate
    {
        const std::string p = ping_text(42u);
        const std::string d = delay_text(230u);
        const int32_t pw = probe.get_text_width(p, FontSize::Px12);
        const int32_t dw = probe.get_text_width(d, FontSize::Px12);
        const LatencyCornerLayout l = layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, LATENCY_LEFT_LIMIT_MATCH);
        check(l.stacked, "latency: in the match the texts stand on two lines (the score boxes take the row)");
        check(l.ping_x + pw == LATENCY_RIGHT_EDGE && l.delay_x + dw == LATENCY_RIGHT_EDGE && LATENCY_RIGHT_EDGE == text_x + text_w, "latency: both lines end where the frame rate ends");
        check(l.ping_y + text_h == l.delay_y && l.delay_y + text_h < FPS_OVERLAY_TOP, "latency: the lines are one above the other, one clear row above the sparkline plate");
        check(l.ping_y >= 436, "latency: and below the chat input box (rows 423 .. 436)");
    }

    // draw calls: the two texts, 12 px, the frame rate's white, and nothing else (no plate, no frame, no sprite)
    {
        RecordingRenderer rr(arc);
        LatencyReadout readout;
        readout.ping_ms = 57u;
        draw_latency_corner(rr, readout, ver_x, text_y, LATENCY_LEFT_LIMIT_SETUP);
        check(rr.texts.size() == 2 && rr.sprites.empty() && rr.fills.empty() && rr.rects.empty() && rr.images.empty(), "latency: the corner draws two texts and nothing else");
        check(rr.texts.size() == 2 && rr.texts[0].text == "ping 57 ms" && rr.texts[1].text == "delay -", "latency: the texts are ping first, then delay (a dash before the first command)");
        bool style = rr.texts.size() == 2;
        for (const auto& t : rr.texts) style = style && t.size == FontSize::Px12 && t.color.r == 255 && t.color.g == 255 && t.color.b == 255 && t.color.a == 255;
        check(style, "latency: in the frame rate's size (12 px) and color (white)");
        RecordingRenderer match(arc);
        readout.delay_ms = 230u;
        draw_latency_corner(match, readout, ver_x, text_y, LATENCY_LEFT_LIMIT_MATCH);
        check(match.texts.size() == 2 && match.texts[0].text == "ping 57 ms" && match.texts[1].text == "delay 230 ms" && match.texts[0].y < match.texts[1].y && match.texts[1].y < text_y,
              "latency: in the match the two lines stand above the corner row");
    }
}

namespace {

// ------------------------------------------------------------------------------------------------------------------------------------------
// The alliance dialogs (Ants.exe FUN_01015b65 / FUN_010160e2 / FUN_01016438): what the original shows, where, and what each answer does
// ------------------------------------------------------------------------------------------------------------------------------------------

// Four teams with a hill each in a test world (the ally pedestal needs more than two hills), named Alice, Bob, Carol and Dave
struct AllianceTable {
    sim::SimulationEngine sim;
    ViewportCamera camera{0, 0};
    std::vector<uint32_t> sounds;
    AllianceTable() {
        sim.init_test_world(60, 60, 5, 600000);
        const sim::TileCoord hills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
        for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, hills[p]);
        const char* names[4] = {"Alice", "Bob", "Carol", "Dave"};
        for (uint8_t p = 0; p < 4; ++p) sim.set_player_name(p, names[p]);
    }
    void prepare(HUD& hud, uint8_t local) {
        hud.init(local);
        hud.set_team_names({"Alice", "Bob", "Carol", "Dave"});
        hud.set_on_play_sfx([this](uint32_t id) { sounds.push_back(id); });
    }
    void refresh(HUD& hud) { hud.update(sim.get_world_state(), 1); }
    static sim::Command command(sim::CommandType type, uint8_t issuer, uint8_t other = 255) {
        sim::Command c;
        c.type = type;
        c.issuer = issuer;
        c.other_player = other;
        return c;
    }
    bool news_has(const std::string& text) {
        bool found = false;
        for (const auto& n : sim.poll_news_events()) found = found || n.message_text.find(text) != std::string::npos;
        return found;
    }
};

// A sink that keeps the commands until they are applied, like the turn of a network match
struct DelayedSink : sim::CommandSink {
    std::vector<sim::Command> held;
    sim::CommandResult submit(const sim::Command& c) override {
        held.push_back(c);
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::Applied;
        return r;
    }
};

}  // namespace

void test_alliance_dialog_layout(const assets::AssetArchive& arc) {
    std::printf("[alliance] the question, the waiting dialog and the confirmation: text, buttons, hover and pressed art\n");
    using D = HUD::AllianceDialog;
    AllianceTable t;
    HUD hud;
    t.prepare(hud, 1);
    t.sim.apply_command(AllianceTable::command(sim::CommandType::AllianceInvite, 0, 1));          // Alice asks Bob
    t.refresh(hud);
    check(hud.alliance_dialog() == D::Invitation && hud.alliance_dialog_team() == 0 && hud.is_modal_open(), "question: the invitee sees the invitation of Alice");
    check(hud.alliance_dialog_text() == sim::strings::format(sim::strings::kInviteDialog, "Alice", "Green"), "question: string 1 with the name and the color word");
    {
        RecordingRenderer rr(arc);
        sim::WorldState world = t.sim.get_world_state();
        hud.render(rr, arc, world, t.camera);
        check(rr.has_sprite_at("dfram1.bmp", 100, 100), "question: the dialog frame at the origin (100, 100)");
        check(rr.has_sprite_at("accpt1.bmp", 152, 260) && rr.has_sprite_at("decl1.bmp", 284, 260), "question: Accept at (152, 260) and Decline at (284, 260)");
        bool all24 = !rr.texts.empty();
        int64_t first_y = -1;
        int lines = 0;
        for (const auto& tx : rr.texts) {
            if (tx.size != FontSize::Px24) continue;
            ++lines;
            if (first_y < 0) first_y = tx.y;
            all24 = all24 && tx.y == first_y + (lines - 1) * 24 && tx.x >= 130 && tx.x + static_cast<int32_t>(tx.text.size()) * 6 <= 130 + 270;
        }
        check(lines >= 2 && first_y == 110 && all24, "question: the text is 24 px high, from y = 110, one cell per line, inside the 270 px box");
    }
    // hover and pressed art, the click sound at the press, the answer at the release
    hud.handle_mouse_motion(160, 270, t.sim, t.camera);
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, t.sim.get_world_state(), t.camera);
        check(rr.has_sprite_at("accpt2.bmp", 152, 260), "question: the hovered Accept shows accpt2");
    }
    t.sounds.clear();
    hud.handle_mouse_down(160, 270, 1, t.sim, t.camera);
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, t.sim.get_world_state(), t.camera);
        check(rr.has_sprite_at("accpt3.bmp", 152, 261), "question: the pressed Accept shows accpt3, one pixel lower (part offset (52, 161))");
        check(t.sounds.size() == 1 && t.sounds[0] == sim::SoundID::ButtonClick, "question: the click sound plays at the press");
    }
    hud.handle_mouse_up(400, 400, 1, t.sim, t.camera);                                            // released elsewhere: cancelled
    check(hud.alliance_dialog() == D::Invitation && !t.sim.stats_manager().are_allies(0, 1), "question: a release off the button does not answer");

    // the waiting dialog of the proposer
    HUD proposer;
    t.prepare(proposer, 0);
    t.refresh(proposer);
    check(proposer.alliance_dialog() == D::Waiting && proposer.alliance_dialog_team() == 1, "waiting: the proposer waits for Bob");
    check(proposer.alliance_dialog_text() == sim::strings::format(sim::strings::kWaitingForAnswer, "Bob", "Red"), "waiting: string 3 with the name and the color word");
    {
        RecordingRenderer rr(arc);
        proposer.render(rr, arc, t.sim.get_world_state(), t.camera);
        check(rr.has_sprite_at("withd1.bmp", 220, 260), "waiting: Withdraw at (220, 260)");
        check(rr.named("accpt1.bmp").empty() && rr.named("decl1.bmp").empty(), "waiting: no Accept and Decline");
    }

    // the confirmation before a team is broken
    AllianceTable u;
    u.sim.form_alliance(0, 2);
    HUD leader;
    u.prepare(leader, 0);
    leader.request_team_up(u.sim, 1);                                                             // Alice (allied with Carol) offers Bob a team
    check(leader.alliance_dialog() == D::BreakConfirm && leader.alliance_dialog_team() == 2, "confirmation: asked about the team with Carol");
    check(leader.alliance_dialog_text() == sim::strings::format(sim::strings::kBreakTeamConfirm, "Carol", "Blue"), "confirmation: string 4 with the ally's name and color word");
    {
        RecordingRenderer rr(arc);
        leader.render(rr, arc, u.sim.get_world_state(), u.camera);
        check(rr.has_sprite_at("yes1.bmp", 180, 260) && rr.has_sprite_at("no1.bmp", 292, 260), "confirmation: Yes at (180, 260) and No at (292, 260)");
        check(u.sim.get_world_state().pending_invite_from[1] == 255, "confirmation: nothing is offered before the answer");
    }
}

void test_alliance_answers(const assets::AssetArchive&) {
    std::printf("[alliance] every answer and what it sends: accept, decline, withdraw, confirm, the keys, dropped teams, dialogs over dialogs\n");
    using D = HUD::AllianceDialog;
    using CT = sim::CommandType;
    // Accept (A): the team is made, the texts and cues of the original, both dialogs close
    {
        AllianceTable t;
        HUD alice, bob;
        t.prepare(alice, 0);
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(alice);
        t.refresh(bob);
        t.sim.clear_audio_events();
        bob.handle_key_down('a', t.sim, t.camera);
        check(bob.alliance_dialog() == D::None && t.sim.stats_manager().are_allies(0, 1), "accept: A answers yes, the team is made");
        check(t.sim.get_world_state().pending_invite_from[1] == 255, "accept: the offer is gone");
        check(t.news_has("Alice (Green) and Bob (Red) are a team now!"), "accept: the News Flash of string 39");
        t.refresh(alice);
        check(alice.alliance_dialog() == D::None, "accept: the proposer's waiting dialog closes");
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None, "accept: the question does not come back");
    }
    // Decline: D, Esc and the button; the proposer hears "rejected"
    for (int way = 0; way < 3; ++way) {
        AllianceTable t;
        HUD alice, bob;
        t.prepare(alice, 0);
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(alice);
        t.refresh(bob);
        if (way == 0) bob.handle_key_down('d', t.sim, t.camera);
        if (way == 1) bob.handle_key_down(SDLK_ESCAPE, t.sim, t.camera);
        if (way == 2) {
            bob.handle_mouse_down(300, 270, 1, t.sim, t.camera);
            bob.handle_mouse_up(300, 270, 1, t.sim, t.camera);
        }
        check(bob.alliance_dialog() == D::None && !t.sim.stats_manager().are_allies(0, 1), "decline (" + std::to_string(way) + "): no team");
        check(t.sim.get_world_state().pending_invite_from[1] == 255, "decline (" + std::to_string(way) + "): the offer is gone");
        check(t.news_has("Bob rejected teaming up"), "decline (" + std::to_string(way) + "): the proposer reads string 80");
        t.refresh(alice);
        check(alice.alliance_dialog() == D::None, "decline (" + std::to_string(way) + "): the waiting dialog closes");
    }
    // Withdraw: W and Esc take the offer back; the invitee's question closes and it reads "withdrew"
    for (int way = 0; way < 2; ++way) {
        AllianceTable t;
        HUD alice, bob;
        t.prepare(alice, 0);
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(alice);
        t.refresh(bob);
        alice.handle_key_down(way == 0 ? SDLK_w : SDLK_ESCAPE, t.sim, t.camera);
        check(alice.alliance_dialog() == D::None && t.sim.get_world_state().pending_invite_from[1] == 255, "withdraw (" + std::to_string(way) + "): the offer is taken back");
        check(t.news_has("Alice withdrew offer to team up"), "withdraw (" + std::to_string(way) + "): the invitee reads string 82");
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None, "withdraw (" + std::to_string(way) + "): the question closes");
    }
    // Keys that mean nothing change nothing, the dialog takes every key
    {
        AllianceTable t;
        HUD bob;
        t.prepare(bob, 1);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        bool taken = true;
        for (int key : std::vector<int>{'x', 'y', 'n', 'w', static_cast<int>(SDLK_RETURN), static_cast<int>(SDLK_F1)}) taken = taken && bob.handle_key_down(key, t.sim, t.camera);
        check(taken && bob.alliance_dialog() == D::Invitation, "keys: the question only answers to A, D and Esc, and takes every key");
        bob.handle_key_down('q', t.sim, t.camera, KMOD_CTRL);                                    // not even Ctrl+Q opens the quit dialog over it
        check(!bob.is_quit_dialog_open(), "keys: no quit dialog over the question");
    }
    // A team is replaced: the question is string 2, Accept breaks the present team first (News Flash of string 40), then makes the new one
    {
        AllianceTable t;
        t.sim.form_alliance(0, 2);
        HUD alice;
        t.prepare(alice, 0);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 3, 0));                    // Dave asks Alice, who is allied with Carol
        t.refresh(alice);
        check(alice.alliance_dialog() == D::Invitation && alice.alliance_dialog_text() ==
                  sim::strings::format(sim::strings::kInviteBreakDialog, "Dave", "Black", "Carol", "Blue"), "replace: the question says that the team with Carol ends");
        t.sim.poll_news_events();
        alice.handle_key_down('a', t.sim, t.camera);
        check(t.sim.stats_manager().are_allies(0, 3) && !t.sim.stats_manager().are_allies(0, 2) && !t.sim.stats_manager().are_allies(2, 3), "replace: Alice and Dave are a team, Carol is free");
        check(t.news_has("are no longer a team!"), "replace: the old team's break is announced (string 40)");
    }
    // The confirmation: Yes breaks the team and sends the offer, No changes nothing; both the keys and the buttons
    for (int way = 0; way < 3; ++way) {
        AllianceTable t;
        t.sim.form_alliance(0, 2);
        HUD alice;
        t.prepare(alice, 0);
        alice.request_team_up(t.sim, 1);
        check(alice.alliance_dialog() == D::BreakConfirm, "confirmation (" + std::to_string(way) + "): asks first");
        if (way == 0) alice.handle_key_down('y', t.sim, t.camera);
        if (way == 1) alice.handle_key_down(SDLK_ESCAPE, t.sim, t.camera);
        if (way == 2) {
            alice.handle_mouse_down(190, 270, 1, t.sim, t.camera);
            alice.handle_mouse_up(190, 270, 1, t.sim, t.camera);
        }
        const bool yes = way != 1;
        check(alice.alliance_dialog() == D::None, "confirmation (" + std::to_string(way) + "): the dialog closes");
        check(t.sim.stats_manager().are_allies(0, 2) == !yes, "confirmation (" + std::to_string(way) + "): the team with Carol " + (yes ? "ends" : "stays"));
        check((t.sim.get_world_state().pending_invite_from[1] == 0) == yes, "confirmation (" + std::to_string(way) + "): the offer to Bob " + (yes ? "is out" : "is not sent"));
    }
    // Without an ally the offer goes out at once; the pedestal of an enemy hill does it (only with more than two players)
    {
        AllianceTable t;
        HUD alice;
        t.prepare(alice, 0);
        alice.select_base(1);
        alice.handle_mouse_down(500, 190, 1, t.sim, t.camera);
        alice.handle_mouse_up(500, 190, 1, t.sim, t.camera);
        check(t.sim.get_world_state().pending_invite_from[1] == 0, "pedestal: the ally pedestal of an enemy hill sends the offer at once");
        t.refresh(alice);
        check(alice.alliance_dialog() == D::Waiting, "pedestal: and the waiting dialog follows");
    }
    // A dropped team takes its offers and dialogs with it
    {
        AllianceTable t;
        HUD bob, alice;
        t.prepare(bob, 1);
        t.prepare(alice, 0);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        t.refresh(alice);
        check(bob.alliance_dialog() == D::Invitation && alice.alliance_dialog() == D::Waiting, "drop: both dialogs are open");
        t.sim.apply_command(AllianceTable::command(CT::Drop, 0));
        t.refresh(bob);
        t.refresh(alice);
        check(bob.alliance_dialog() == D::None && t.sim.get_world_state().pending_invite_from[1] == 255, "drop: the question of a team that dropped closes");
    }
    // One dialog at a time: the question waits while another dialog is open
    {
        AllianceTable t;
        HUD bob;
        t.prepare(bob, 1);
        bob.open_quit_dialog();
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None && bob.is_quit_dialog_open(), "queue: no question over the quit dialog");
        bob.close_quit_dialog();
        t.refresh(bob);
        check(bob.alliance_dialog() == D::Invitation, "queue: the question comes when the quit dialog is closed");
    }
    // In a network match the answer reaches the simulation a few turns later: the question does not come back meanwhile
    {
        AllianceTable t;
        HUD bob;
        t.prepare(bob, 1);
        DelayedSink sink;
        bob.set_command_sink(&sink);
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 0, 1));
        t.refresh(bob);
        bob.handle_key_down('a', t.sim, t.camera);
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None && sink.held.size() == 1 && sink.held[0].type == CT::AllianceAccept && sink.held[0].other_player == 0 &&
                  sink.held[0].issuer == 1, "network: Accept is handed to the turn manager with the proposer, and the question stays away");
        for (const auto& c : sink.held) t.sim.apply_command(c);                                   // the turn executes it
        t.refresh(bob);
        check(bob.alliance_dialog() == D::None && t.sim.stats_manager().are_allies(0, 1), "network: the team is made when the turn comes");
        // a new offer from somebody else opens a new question
        t.sim.apply_command(AllianceTable::command(CT::AllianceInvite, 2, 1));
        t.refresh(bob);
        check(bob.alliance_dialog() == D::Invitation && bob.alliance_dialog_team() == 2, "network: a later offer asks again");
    }
    // The commands of a confirmed attack on the ally: the team is broken first, then the order is given
    {
        AllianceTable t;
        t.sim.form_alliance(0, 1);
        const uint32_t mine = t.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{20, 20});
        t.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{22, 20});
        HUD alice;
        t.prepare(alice, 0);
        DelayedSink sink;
        alice.set_command_sink(&sink);
        alice.select_ant(mine, false);
        alice.order_selected(t.sim, sim::TileCoord{22, 20}, false, true);
        check(alice.alliance_dialog() == D::BreakConfirm && sink.held.empty(), "attack on the ally: the order waits for the answer");
        alice.handle_key_down('y', t.sim, t.camera);
        check(sink.held.size() == 2 && sink.held[0].type == CT::AllianceBreak && sink.held[1].type == CT::GroupAttack && sink.held[1].tile_x == 22,
              "attack on the ally: Yes sends the break and then the attack");
    }
}

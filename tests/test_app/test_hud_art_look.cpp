// The HUD art, the cases that draw (see test_hud_art.cpp; docs/VIEW_AND_HUD.md, "HUD art"):
//   [look]     the real renderer's frame of an idle match screen at 16:9 and at 20:9, 1x, against the committed reference pictures (tests/data/hud_art/, made from the sheets by tools/hud_art/reference_frame.py,
//              which composes them by the manifest's own rules and shares no code with the game); the same HUD with the original's art is not that picture
//   [classic]  a HUD that never had the art, and one that had it and gave it back, draw the same pixels; with the art the original's frame pieces and score covers are not drawn, without it they are
//   [pieces]   the pieces that change with what is happening (the pedestal, the rim flash, Stop, the eggs, the buttons) are the art's, in their places, and every hit zone is where it was
// Look only: the same layout, the same rectangles, the same clicks.
#include "hud_art_fixtures.hpp"

namespace {

using hud_art::Art;

/// What the HUD writes with the game's own hand on an idle screen, which a reference picture does not hold: the clock, the four names and score boxes, the minimap's picture, the status line, the
/// chat log and the chat input
std::vector<LayoutRect> own_content(const ScreenLayout& layout) {
    std::vector<LayoutRect> out;
    out.push_back(LayoutRect{60, 2, 70, 20});                                              // the clock's window
    for (size_t k = 0; k < 4; ++k) {
        const ScoreSlot s = layout.score_slot(k);
        out.push_back(LayoutRect{s.label_left, s.top - 2, s.box_left + 58 - s.label_left, 20});
    }
    out.push_back(layout.minimap());
    out.push_back(LayoutRect{layout.right(478), 252, 146, 17});                            // the status box
    out.push_back(LayoutRect{layout.chat_view().x - 3, layout.chat_view().y - 3, layout.chat_view().w + 6, layout.chat_view().h + 6});
    out.push_back(LayoutRect{layout.right(478), layout.bottom(422), 146, 16});             // the chat input
    return out;
}

bool in_any(const std::vector<LayoutRect>& rects, int32_t x, int32_t y) {
    for (const LayoutRect& r : rects) {
        if (r.contains(x, y)) return true;
    }
    return false;
}

struct Frame {
    std::vector<uint8_t> px;       // RGBA of the canvas
    int32_t w{0};
    int32_t h{0};
};

/// One frame of an idle match screen through the real renderer at 1x: a HUD of the layout over an empty world, with the art given (null: the original's)
Frame idle_frame(const assets::AssetArchive& arc, const ScreenLayout& layout, const Art* art, bool give_back = false) {
    Frame out;
    out.w = layout.width;
    out.h = layout.height;
    RendererRig rig(arc, layout.width, layout.height);
    if (!rig.ok) return out;
    rig.renderer.set_layout(layout);
    rig.renderer.set_hud_team(0);
    HudRig hud(arc, layout);
    if (art != nullptr) hud.hud.set_art(art);
    if (give_back) hud.hud.set_art(nullptr);
    rig.renderer.begin_frame();
    hud.hud.render(rig.renderer, arc, hud.world(), hud.cam);
    out.px = rig.read();
    return out;
}

/// The pixels of the frame that differ from the reference by more than `tolerance` in any color, among those that the reference draws and the game does not draw itself; and how many were compared
void compare(const Frame& frame, const DecodedImage& ref, const ScreenLayout& layout, int tolerance, int64_t& compared, int64_t& bad) {
    compared = 0;
    bad = 0;
    if (frame.px.size() != ref.rgba.size() || frame.w != ref.width || frame.h != ref.height) {
        bad = -1;
        return;
    }
    const std::vector<LayoutRect> skip = own_content(layout);
    for (int32_t y = 0; y < frame.h; ++y) {
        for (int32_t x = 0; x < frame.w; ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(frame.w) + static_cast<size_t>(x)) * 4;
            if (ref.rgba[i + 3] != 255 || in_any(skip, x, y)) continue;
            ++compared;
            for (size_t c = 0; c < 3; ++c) {
                if (std::abs(static_cast<int>(frame.px[i + c]) - static_cast<int>(ref.rgba[i + c])) > tolerance) {
                    ++bad;
                    break;
                }
            }
        }
    }
}

}  // namespace

void test_look(const assets::AssetArchive& arc, const Art& art) {
    group("look", "the real renderer's frame of an idle match screen at 16:9 and 20:9 (1x) is the reference picture, pixel for pixel; the original's art is not");
    const struct {
        int32_t w, h;
        const char* file;
    } cases[] = {{960, 540, "frame_16x9_1x.png"}, {1200, 540, "frame_20x9_1x.png"}};
    for (const auto& c : cases) {
        const std::string at = std::to_string(c.w) + " x " + std::to_string(c.h) + ": ";
        DecodedImage ref;
        if (!read_png(std::string(TEST_DATA_DIR) + "/hud_art/" + c.file, ref)) {
            check(false, at + "the reference picture is read");
            continue;
        }
        const ScreenLayout layout = ScreenLayout::with_size(c.w, c.h);
        int64_t compared = 0, bad = 0;
        compare(idle_frame(arc, layout, &art), ref, layout, 2, compared, bad);
        check(compared * 8 > static_cast<int64_t>(c.w) * c.h && bad == 0, at + "the frame is the reference (" + std::to_string(bad) + " of " + std::to_string(compared) + " pixels differ by more than 2)");
        compare(idle_frame(arc, layout, nullptr), ref, layout, 2, compared, bad);
        check(bad > compared / 2, at + "the same HUD with the original's art is not the reference (" + std::to_string(bad) + " of " + std::to_string(compared) + " differ)");
    }
}

void test_classic(const assets::AssetArchive& arc, const Art& art) {
    group("classic", "a HUD that never had the art and one that gave it back draw the same pixels; with the art the original's frame pieces and covers are not drawn");
    for (const Shape& shape : {kShapes[0], kShapes[1], kShapes[4]}) {
        const std::string at = std::string(shape.name) + ": ";
        const ScreenLayout layout = ScreenLayout::with_size(shape.w, shape.h);
        const Frame never = idle_frame(arc, layout, nullptr);
        const Frame back = idle_frame(arc, layout, &art, true);
        check(!never.px.empty() && never.px == back.px, at + "the original's art is what it was after the art was given and taken away");
        const Frame with = idle_frame(arc, layout, &art);
        check(!with.px.empty() && with.px != never.px, at + "the art changes the picture");
    }
    // what the HUD asks the renderer for
    for (const Shape& shape : {kShapes[0], kShapes[1], kShapes[4]}) {
        const ScreenLayout layout = ScreenLayout::with_size(shape.w, shape.h);
        const std::string at = std::string(shape.name) + ": ";
        for (const bool with_art : {false, true}) {
            HudRig rig(arc, layout);
            if (with_art) rig.hud.set_art(&art);
            ArtSpy spy(arc);
            rig.hud.render(spy, arc, rig.world(), rig.cam);
            size_t frame_pieces = 0, regions = 0;
            for (const ShellRule& rule : kShellRules) {
                for (const Spy::Ev& e : spy.events) {
                    if ((e.kind == Spy::Kind::Sprite || e.kind == Spy::Kind::Region) && e.name == rule.sprite) ++frame_pieces;
                }
            }
            for (const Spy::Ev& e : spy.events) regions += e.kind == Spy::Kind::Region ? 1u : 0u;
            if (with_art) {
                check(frame_pieces == 0 && regions == 0, at + "with the art none of the original's frame pieces is drawn");
                check(!spy.copies.empty() && !spy.of_sprite(art, "top_bar").empty() && !spy.of_sprite(art, "card").empty(), at + "the art's pieces are drawn");
            } else {
                check(frame_pieces > 0 && spy.copies.empty(), at + "without the art the original's frame pieces are drawn and the art's are not");
            }
        }
    }
    // a team that does not play: the original covers its score box; the art has a window only where a team plays
    {
        const ScreenLayout layout = ScreenLayout::with_size(960, 540);
        for (const bool with_art : {false, true}) {
            HudRig rig(arc, layout);
            if (with_art) rig.hud.set_art(&art);
            rig.hud.set_roster_mask(0x03);                                    // seats 0 and 1 play
            ArtSpy spy(arc);
            rig.hud.render(spy, arc, rig.world(), rig.cam);
            const ScoreSlot first = layout.score_slot(1);                    // (the other team: seat 1)
            const ScoreSlot second = layout.score_slot(2);                   // (seat 2: does not play)
            const auto window_at = [&](const ScoreSlot& s) {
                for (const ArtSpy::Copy& c : spy.of_sprite(art, "score_window")) {
                    if (c.x == s.box_left - 2 && c.y == s.top - 1) return true;
                }
                return false;
            };
            const auto cover_at = [&](const ScoreSlot& s) {            // (the cover is drawn by name)
                for (const Spy::Ev& e : spy.events) {
                    if (e.kind == Spy::Kind::Named && e.name == "scorcovr.bmp" && e.x == s.box_left - 2 && e.y == s.top - 1) return true;
                }
                return false;
            };
            if (with_art) {
                check(window_at(first) && !window_at(second) && !cover_at(second), "with the art a team that plays has its window and one that does not has none, and no cover");
            } else {
                check(!window_at(first) && !cover_at(first) && cover_at(second), "without the art the original's cover hides the score box of a team that does not play");
            }
        }
    }
}

void test_pieces(const assets::AssetArchive& arc, const Art& art) {
    group("pieces", "the pieces that change with what is happening are the art's, in their places; every hit zone and rectangle is where it was");
    for (const Shape& shape : {kShapes[0], kShapes[1], kShapes[4], kShapes[6]}) {
        const ScreenLayout layout = ScreenLayout::with_size(shape.w, shape.h);
        const std::string at = std::string(shape.name) + ": ";
        // a worker of the local team is selected: the Move pedestal rises, Stop and the rim flash show; a bomber has the second pedestal
        for (const sim::AntType type : {sim::AntType::Worker, sim::AntType::Bomber}) {
            HudRig rig(arc, layout);
            rig.hud.set_art(&art);
            const uint32_t ant = rig.sim.spawn_unit(0, type, sim::TileCoord{5, 5});
            rig.sim.tick();
            rig.hud.select_ant(ant);
            rig.hud.update(rig.sim.get_world_state(), 0);
            ArtSpy first(arc);
            g_clock = 0;
            rig.hud.render(first, arc, rig.world(), rig.cam);
            g_clock = 10000;
            ArtSpy spy(arc);
            rig.hud.render(spy, arc, rig.world(), rig.cam);
            const LayoutPoint v{layout.width - 960, layout.height - 540};              // (the art's places are those of 960 x 540; the right-anchored pieces move with the width)
            const auto stands = [&](const std::string& name, int32_t x960, int32_t y960) {
                for (const ArtSpy::Copy& c : spy.of_sprite(art, name)) {
                    if (c.x == x960 + v.x && c.y == y960 && c.sx == 0 && c.sy == 0) return true;
                }
                return false;
            };
            check(stands("pedestal_move_up", 796, 156) && stands("label_move", 794, 136), at + "the Move pedestal and its label stand in the left slot, at rest");
            check(stands("stop_up", 908, 172), at + "Stop stands where the original's did");
            if (type == sim::AntType::Bomber) check(stands("pedestal_bomb_up", 857, 156) && stands("label_bomb", 855, 136), at + "the Bomber's pedestal stands in the right slot");
            else check(spy.of_sprite(art, "pedestal_bomb_up").empty(), at + "a worker has no second pedestal");
            bool classic_pedestal = false;
            for (const Spy::Ev& e : spy.events) classic_pedestal = classic_pedestal || (e.kind == Spy::Kind::Sprite && (e.name == "butup.bmp" || e.name == "butmovu.bmp" || e.name == "labmov.bmp"));
            check(!classic_pedestal, at + "none of the original's pedestal pictures is drawn");
        }
        // the rise of the pedestal plays the art's own frames at the art's own times (nine of 60 ms), then rests
        {
            HudRig rig(arc, layout);
            rig.hud.set_art(&art);
            const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
            rig.sim.tick();
            rig.hud.select_ant(ant);
            rig.hud.update(rig.sim.get_world_state(), 0);
            const auto frame_at = [&](uint32_t ms) {
                g_clock = ms;
                ArtSpy spy(arc);
                rig.hud.render(spy, arc, rig.world(), rig.cam);
                return spy;
            };
            const ArtSpy t0 = frame_at(0);
            check(!t0.of_sprite(art, "pedestal_rise1").empty() && t0.of_sprite(art, "pedestal_move_up").empty(), at + "the pedestal starts its rise with the art's first frame");
            const ArtSpy t130 = frame_at(130);
            check(!t130.of_sprite(art, "pedestal_rise3").empty(), at + "after 130 ms (60 ms a frame) it is at the third");
            const ArtSpy t310 = frame_at(310);
            check(!t310.of_sprite(art, "pedestal_move_rise6").empty() && t310.of_sprite(art, "pedestal_rise3").empty(), at + "after 310 ms it has the Move picture, in the sixth frame");
            const ArtSpy t900 = frame_at(900);
            check(!t900.of_sprite(art, "pedestal_move_up").empty() && !t900.of_sprite(art, "label_move").empty(), at + "after the rise it rests, with its label");
            g_clock = 0;
        }
        // the egg tray and the lunch box: the own hill with five eggs; an ant that carries food
        {
            HudRig rig(arc, layout);
            rig.hud.set_art(&art);
            ArtSpy empty(arc);
            rig.hud.render(empty, arc, rig.world(), rig.cam);
            check(empty.of_sprite(art, "egg").empty() && empty.of_sprite(art, "lunch_box").empty(), at + "nothing selected: no eggs, no lunch box");
            sim::WorldState world = rig.world();
            if (!world.player_eggs.empty()) world.player_eggs[0] = 5;
            rig.override_world = world;
            rig.hud.select_base(0);
            rig.hud.update(world, 0);
            g_clock = 0;
            ArtSpy first(arc);
            rig.hud.render(first, arc, world, rig.cam);
            g_clock = 10000;
            ArtSpy tray(arc);
            rig.hud.render(tray, arc, world, rig.cam);
            const int32_t dx = layout.width - 960;
            bool places = true;
            for (const ArtSpy::Copy& c : tray.of_sprite(art, "egg")) places = places && c.x >= 851 + dx && c.x < 900 + dx && c.y >= 158 && c.y < 214;
            check(tray.of_sprite(art, "egg").size() == 5 && places, at + "five eggs in the tray, each of the art's, inside the tray's place");
            check(!tray.of_sprite(art, "pedestal_hatch_up").empty() && !tray.of_sprite(art, "stop_up").empty(), at + "the hatch pedestal and Stop are the art's");
        }
        {
            HudRig rig(arc, layout);
            rig.hud.set_art(&art);
            const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
            rig.sim.tick();
            sim::WorldState world = rig.sim.get_world_state();
            for (sim::AntSnapshot& a : world.ants) a.is_holding = a.id == ant ? true : a.is_holding;
            rig.override_world = world;
            rig.hud.select_ant(ant);
            rig.hud.update(world, 0);
            ArtSpy spy(arc);
            rig.hud.render(spy, arc, world, rig.cam);
            const std::vector<ArtSpy::Copy> box = spy.of_sprite(art, "lunch_box");
            check(box.size() == 1 && box[0].x == 917 + layout.width - 960 && box[0].y == 131, at + "an ant that carries food: the art's lunch box, where the original's was");
        }
        // the three buttons of the top bar, up, hover and pressed: every frame draws one whole button
        {
            HudRig rig(arc, layout);
            rig.hud.set_art(&art);
            ArtSpy rest(arc);
            rig.hud.render(rest, arc, rig.world(), rig.cam);
            const auto count = [&](const ArtSpy& s, const char* name) { return s.of_sprite(art, name).size(); };
            check(count(rest, "button_help_up") == 1 && count(rest, "button_options_up") == 1 && count(rest, "button_quit_up") == 1, at + "at rest each button of the top bar is drawn, up, once");
            check(count(rest, "send_all_up") == 1 && count(rest, "send_team_up") == 0, at + "the All button is drawn, up; the Team button only while there is an ally");
            // the pointer over Help (a hit zone of the original's size and place)
            const LayoutRect help{layout.right(476 - 8), 7, 46, 23};            // (8 pixels left of the original's place: the Fullscreen button stands right of Quit)
            rig.hud.handle_mouse_motion(help.x + 10, help.y + 10, rig.sim, rig.cam);
            ArtSpy over(arc);
            rig.hud.render(over, arc, rig.world(), rig.cam);
            check(count(over, "button_help_hover") == 1 && count(over, "button_help_up") == 0 && count(over, "button_options_up") == 1, at + "the pointer over Help: its hover picture replaces its up picture");
            rig.hud.handle_mouse_down(help.x + 10, help.y + 10, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            ArtSpy down(arc);
            rig.hud.render(down, arc, rig.world(), rig.cam);
            check(count(down, "button_help_down") == 1 && count(down, "button_help_hover") == 0, at + "pressed: its pressed picture");
            rig.hud.handle_mouse_up(help.x + 10, help.y + 10, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            check(rig.hud.is_modal_open(), at + "the click on the art's Help button (the original's hit zone) opens the quick help");
            ArtSpy open(arc);
            rig.hud.render(open, arc, rig.world(), rig.cam);
            check(count(open, "button_help_down") == 1, at + "while the quick help is open Help stays pressed");
        }
    }
    // the layout does not know the art: the same rectangles with and without it
    for (const Shape& shape : kShapes) {
        const ScreenLayout layout = ScreenLayout::with_size(shape.w, shape.h);
        HudRig plain(arc, layout);
        HudRig dressed(arc, layout);
        dressed.hud.set_art(&art);
        check(plain.hud.layout() == dressed.hud.layout() && plain.hud.chat_box_rect() == dressed.hud.chat_box_rect(), std::string(shape.name) + ": the art changes no rectangle of the HUD");
    }
}

// ---- the windows of the match in the new look, and the Fullscreen button -----------------------------------------------------------------------------------------------------------------------

namespace {

using window_art::kDialogRect;

/// The copies of the nine pieces of the panel (not the HUD frame's panel_top)
size_t panel_copies(const ArtSpy& spy, const Art& art) {
    size_t n = 0;
    for (const char* name : {"panel_tl", "panel_t", "panel_tr", "panel_l", "panel_c", "panel_r", "panel_bl", "panel_b", "panel_br"}) n += spy.of_sprite(art, name).size();
    return n;
}

size_t copies_of(const ArtSpy& spy, const Art& art, const std::string& name) { return spy.of_sprite(art, name).size(); }

/// True when `name` was drawn exactly once at (x, y)
bool once_at(const ArtSpy& spy, const Art& art, const std::string& name, int32_t x, int32_t y) {
    const std::vector<ArtSpy::Copy> c = spy.of_sprite(art, name);
    return c.size() == 1 && c[0].x == x && c[0].y == y && c[0].sx == 0 && c[0].sy == 0;
}

/// The copies of every sprite whose name starts with `prefix`
size_t copies_with_prefix(const ArtSpy& spy, const Art& art, const std::string& prefix) {
    size_t n = 0;
    for (const hud_art::Sprite& s : art.sprites()) {
        if (s.name.compare(0, prefix.size(), prefix) == 0) n += copies_of(spy, art, s.name);
    }
    return n;
}

ArtSpy render_once(HudRig& rig, const assets::AssetArchive& arc, bool face = true, double output = 1.0) {
    ArtSpy spy(arc);
    spy.dialog_face = face;
    spy.face_ready = face;
    spy.output = output;
    rig.hud.render(spy, arc, rig.world(), rig.cam);
    return spy;
}

bool has_text(const ArtSpy& spy, const std::string& text, FontSize size) {
    for (const ArtSpy::Ev& e : spy.of(ArtSpy::Kind::Text)) {
        if (e.name == text && e.size == size) return true;
    }
    return false;
}

/// The manifest without an animation and its frames
std::string without_anim_text(const std::string& text, const std::string& name) {
    const size_t at = text.find("\nanim " + name + "\n");
    if (at == std::string::npos) return text;
    size_t end = at + 1;
    for (;;) {
        end = text.find('\n', end);
        if (end == std::string::npos) return text.substr(0, at);
        end += 1;
        if (text.compare(end, 6, "frame ") != 0) break;
    }
    return text.substr(0, at) + "\n" + text.substr(end);
}

std::string manifest_text() {
    std::ifstream f(art_folder() + "/hud_art.txt");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

void test_windows(const assets::AssetArchive& arc, const Art& art) {
    group("windows-draw", "the Fullscreen button in both looks; the quit and alliance questions, Get ready, the quick help and the countdown card in the new look, and each as the original's without it");

    // ---- the Fullscreen button right of Quit
    for (const Shape& shape : kShapes) {
        const ScreenLayout layout = ScreenLayout::with_size(shape.w, shape.h);
        const std::string at = std::string(shape.name) + ": ";
        const int32_t dx = layout.dx();
        {   // the new look
            HudRig rig(arc, layout);
            rig.hud.set_art(&art);
            const UIButton& fb = rig.hud.fullscreen_button();
            check(fb.x == layout.right(619) && fb.y == 7 && fb.w == 21 && fb.h == 23 && fb.x + fb.w == layout.width, at + "new art: the button stands right of Quit, 21 x 23, ending at the picture's edge");
            check(rig.hud.help_button().x == layout.right(468) && rig.hud.options_button().x == layout.right(517) && rig.hud.quit_button().x == layout.right(571), at + "new art: Help, Options and Quit stand 8 pixels left of the original's places");
            check(rig.hud.help_button().x + rig.hud.help_button().w <= rig.hud.options_button().x && rig.hud.quit_button().x + rig.hud.quit_button().w <= fb.x, at + "new art: the four buttons do not overlap");
            int calls = 0;
            rig.hud.set_on_fullscreen([&] { ++calls; });
            const ArtSpy rest = render_once(rig, arc);
            check(once_at(rest, art, "button_fullscreen_up", fb.x, fb.y) && copies_with_prefix(rest, art, "button_fullscreen") == 1, at + "new art: at rest the button's up picture, once, on its rectangle");
            rig.hud.handle_mouse_motion(fb.x + 5, fb.y + 5, rig.sim, rig.cam);
            check(once_at(render_once(rig, arc), art, "button_fullscreen_hover", fb.x, fb.y), at + "new art: the pointer on it: its hover picture");
            rig.hud.handle_mouse_down(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            check(once_at(render_once(rig, arc), art, "button_fullscreen_down", fb.x, fb.y) && calls == 0, at + "new art: pressed: its pressed picture, and nothing acts at the press");
            rig.hud.set_fullscreen_state(true);
            check(once_at(render_once(rig, arc), art, "button_fullscreen_leave_down", fb.x, fb.y), at + "new art: while the game is full screen the picture is the one that leaves");
            rig.hud.handle_mouse_up(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            check(calls == 1 && rig.hud.fullscreen_clicks() == 1 && !rig.hud.is_modal_open(), at + "new art: the release on the button acts once (and opens no window)");
            check(once_at(render_once(rig, arc), art, "button_fullscreen_leave_hover", fb.x, fb.y), at + "new art: the picture that leaves, hovered");
            rig.hud.set_fullscreen_state(false);
            rig.hud.handle_mouse_down(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            rig.hud.handle_mouse_motion(fb.x - 30, fb.y + 100, rig.sim, rig.cam);
            rig.hud.handle_mouse_up(fb.x - 30, fb.y + 100, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            check(calls == 1, at + "new art: a press on it and a release away from it acts on nothing");
            rig.hud.handle_mouse_down(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            rig.hud.handle_mouse_up(fb.x - 30, fb.y + 100, SDL_BUTTON_LEFT, rig.sim, rig.cam);          // (a release that comes with no move before it, as a lifted finger does)
            check(calls == 1 && !rig.hud.fullscreen_button().is_pressed, at + "new art: ...also when the release is the first news of the pointer away from the button");
            rig.hud.handle_mouse_down(fb.x + 5, fb.y + 5, SDL_BUTTON_RIGHT, rig.sim, rig.cam);
            rig.hud.handle_mouse_up(fb.x + 5, fb.y + 5, SDL_BUTTON_RIGHT, rig.sim, rig.cam);
            check(calls == 1, at + "new art: the right button does not act on it");
            check(rig.hud.fullscreen_button_active(), at + "new art: the button is active over the match");
            rig.hud.open_quick_help();
            rig.hud.handle_mouse_down(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            check(!rig.hud.fullscreen_button().is_pressed && !rig.hud.fullscreen_button_active(), at + "new art: with the quick help open the button takes no press and is not active");
        }
        {   // the Classic look
            HudRig rig(arc, layout);
            const UIButton& fb = rig.hud.fullscreen_button();
            ArtSpy rest(arc);
            rig.hud.render(rest, arc, rig.world(), rig.cam);
            if (layout.is_classic()) {
                check(fb.w == 0 && rig.hud.help_button().x == 476 && rig.hud.options_button().x == 525 && rig.hud.quit_button().x == 579, at + "the original's own picture with the original's art has no Fullscreen button and its buttons stay");
                check(rest.blended.empty() && rest.has_sprite_at("buthelpu.bmp", 476, 7) == false, at + "...and draws nothing of the corner that the other pictures draw");
                check(!rig.hud.fullscreen_button_active(), at + "...nothing to click");
                continue;
            }
            check(fb.x == layout.right(619) && fb.y == 7 && fb.w == 20 && fb.h == 23 && rig.hud.help_button().x == layout.right(468) && rig.hud.quit_button().x == layout.right(571), at + "classic art: the button and the three moved buttons stand where the new art's do (20 wide)");
            check(rest.has_sprite_at("buthelpu.bmp", 468 + dx, 7) && rest.has_sprite_at("butoptu.bmp", 517 + dx, 7) && rest.has_sprite_at("butquitu.bmp", 571 + dx, 7), at + "classic art: the original's three buttons are drawn at their new places, at rest too");
            bool plate = false, button = false;
            for (const ArtSpy::Blended& b : rest.blended) {
                plate = plate || (b.x == layout.right(469) && b.y == 7 && b.w == 171 && b.h == 24);
                button = button || (b.x == fb.x && b.y == fb.y && b.w == 20 && b.h == 23);
            }
            check(plate && button && rest.blended.size() == 2, at + "classic art: the plate is laid over the old places, and the button is the original's pieces with a picture");
            rig.hud.handle_mouse_down(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            ArtSpy held(arc);
            rig.hud.render(held, arc, rig.world(), rig.cam);
            check(held.blended.size() == 2 && held.blended.back().rgba != rest.blended.back().rgba, at + "classic art: held, the button's picture changes");
            int calls = 0;
            rig.hud.set_on_fullscreen([&] { ++calls; });
            rig.hud.handle_mouse_up(fb.x + 5, fb.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            check(calls == 1, at + "classic art: the release acts");
            rig.hud.handle_mouse_motion(layout.right(476 - 8) + 5, 12, rig.sim, rig.cam);        // the pointer on Help: the original's small label is laid over the picture, at the new place
            ArtSpy over(arc);
            rig.hud.render(over, arc, rig.world(), rig.cam);
            check(over.has_sprite_at("buthelpr.bmp", 476 + dx, 10), at + "classic art: the label of a hovered Help is 8 pixels left of the original's place (484 + dx)");
        }
    }

    // ---- the dialogs
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    {
        HudRig rig(arc, wide);
        rig.hud.set_art(&art);
        rig.hud.open_quit_dialog();
        const ArtSpy spy = render_once(rig, arc);
        const size_t panel = hud_art::panel_draws(art, kDialogRect).size();
        check(panel > 8 && panel_copies(spy, art) == panel, "quit: the dialog's panel is the nine pieces, over the original's rectangle (100, 100) 320 x 224");
        check(once_at(spy, art, "dlg_btn_yes_up", 180, 260) && once_at(spy, art, "dlg_btn_no_up", 292, 260), "quit: Yes and No are the set's, at the original's places");
        check(!spy.has_sprite_at("yes1.bmp", 180, 260) && !spy.has_sprite_at("no1.bmp", 292, 260) && spy.count(ArtSpy::Kind::Sprite) < 40, "quit: none of the original's dialog pieces is drawn");
        check(spy.lines.size() == 1 && spy.lines[0].text == "Do you really want to quit?" && spy.lines[0].baseline == 196 && spy.lines[0].em_tenths == 200 && spy.lines[0].x == 260 - 270 / 2, "quit: the sentence is one line in the windows' face, centred in the dialog, its baseline 96 down");
        check(!has_text(spy, "Do you really want to quit?", FontSize::Px24), "quit: ...and not also the original's text");
        const ArtSpy plain = render_once(rig, arc, false);
        check(plain.lines.empty() && has_text(plain, "Do you really want to quit?", FontSize::Px24) && once_at(plain, art, "dlg_btn_yes_up", 180, 260), "quit: a renderer without the windows' face draws the original's text in the new window");
        const LayoutPoint m = wide.modal_offset();
        rig.hud.handle_mouse_motion(m.x + 204, m.y + 272, rig.sim, rig.cam);
        check(once_at(render_once(rig, arc), art, "dlg_btn_yes_hover", 180, 260), "quit: the pointer on Yes: its hover picture");
        rig.hud.handle_mouse_down(m.x + 204, m.y + 272, SDL_BUTTON_LEFT, rig.sim, rig.cam);
        check(once_at(render_once(rig, arc), art, "dlg_btn_yes_down", 180, 260), "quit: pressed: its pressed picture");
        rig.hud.handle_mouse_up(m.x + 204, m.y + 272, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    }
    {
        HudRig rig(arc, wide);                                                             // the same dialog without the art, and with a set that lacks one of its pictures
        rig.hud.open_quit_dialog();
        const ArtSpy classic = render_once(rig, arc);
        check(classic.has_sprite_at("yes1.bmp", 180, 260) && classic.has_sprite_at("no1.bmp", 292, 260) && panel_copies(classic, art) == 0 && classic.lines.empty(), "quit without the art: the original's dialog, its buttons and its text");
        Art damaged;
        check(damaged.parse(without_anim_text(manifest_text(), "no3")), "(a set without the pressed picture of No is read)");
        rig.hud.set_art(&damaged);
        const ArtSpy fallback = render_once(rig, arc);
        check(fallback.has_sprite_at("yes1.bmp", 180, 260) && fallback.has_sprite_at("no1.bmp", 292, 260) && panel_copies(fallback, damaged) == 0 && fallback.lines.empty(), "quit with a set that lacks a picture of the dialog: the original's whole dialog");
        check(has_text(fallback, "Do you really want to quit?", FontSize::Px24), "...with the original's text");
    }
    {
        HudRig rig(arc, wide);
        rig.hud.set_art(&art);
        sim::WorldState w = rig.sim.get_world_state();
        w.pending_invite_from[0] = 1;                                                      // Bea (Red) asks this player
        rig.hud.update(w, 0);
        rig.override_world = w;
        const ArtSpy spy = render_once(rig, arc);
        check(once_at(spy, art, "dlg_btn_accept_up", 152, 260) && once_at(spy, art, "dlg_btn_decline_up", 284, 260) && copies_with_prefix(spy, art, "dlg_btn_") == 2, "invitation: Accept and Decline are the set's, at the original's places");
        bool box = spy.lines.size() >= 2;
        for (size_t i = 0; i < spy.lines.size(); ++i) {
            box = box && spy.lines[i].baseline == 100 + 30 + static_cast<int32_t>(i) * 26 && spy.dialog_text_width(spy.lines[i].text, spy.lines[i].em_tenths) <= 292;
        }
        check(box, "invitation: the sentence wraps in the dialog's inner width less a margin (292), 26 pixels from line to line, the first baseline 30 down");
        rig.hud.handle_mouse_motion(wide.modal_offset().x + 192, wide.modal_offset().y + 272, rig.sim, rig.cam);
        check(once_at(render_once(rig, arc), art, "dlg_btn_accept_hover", 152, 260), "invitation: the pointer on Accept: its hover picture");
    }
    {
        HudRig rig(arc, wide);
        rig.hud.set_art(&art);
        rig.hud.start_match_modal(true);
        const ArtSpy spy = render_once(rig, arc);
        check(panel_copies(spy, art) > 0 && copies_with_prefix(spy, art, "dlg_btn_") == 0, "get ready: the new panel, no button");
        check(spy.lines.size() >= 2 && spy.lines.back().text == "Waiting for others..." && spy.lines.back().baseline == 308 && spy.lines.back().em_tenths == 180 && spy.lines[0].em_tenths == 260 && spy.lines[0].baseline == 138, "get ready: the words in the windows' face (26 and 18), the footer's baseline 208 down");
    }
}

void test_windows_more(const assets::AssetArchive& arc, const Art& art) {
    group("windows-more", "the quick help, the countdown card and the windows' face with the real renderer");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);

    // ---- the quick help
    {
        HudRig rig(arc, wide);
        rig.hud.set_art(&art);
        rig.hud.open_quick_help();
        const ArtSpy spy = render_once(rig, arc);
        const LayoutPoint q = wide.quick_help_offset();
        check(panel_copies(spy, art) > 0 && copies_with_prefix(spy, art, "qh_part_") >= 1, "quick help: the new panel and the window's pieces");
        check(once_at(spy, art, "qh_pedestal_move", 217, 201) && once_at(spy, art, "qh_pedestal_special", 474, 404), "quick help: the two pedestal pictures at the original's places");
        check(once_at(spy, art, "cursor_curfd01", 226, 344) && copies_of(spy, art, "cursor_c_normal") == 2, "quick help: the new food cursor and pointer");
        window_art::KeptPictures kept;
        check(kept.build(arc) && spy.blended.size() == kept.pictures().size(), "quick help: the original's pictures are laid over the wells, all of them");
        bool same = spy.blended.size() == kept.pictures().size();
        for (size_t i = 0; same && i < spy.blended.size(); ++i) same = spy.blended[i].x == kept.pictures()[i].x && spy.blended[i].y == kept.pictures()[i].y && spy.blended[i].rgba == kept.pictures()[i].rgba;
        check(same, "quick help: ...at the places and with the pixels that were cut");
        check(once_at(spy, art, "dlg_btn_return_up", 529, 437), "quick help: Return to Game at the original's place");
        rig.hud.handle_mouse_motion(q.x + 578, q.y + 450, rig.sim, rig.cam);
        check(once_at(render_once(rig, arc), art, "dlg_btn_return_hover", 529, 437), "quick help: the pointer on Return: its hover picture");
        rig.hud.handle_mouse_down(q.x + 578, q.y + 450, SDL_BUTTON_LEFT, rig.sim, rig.cam);
        check(once_at(render_once(rig, arc), art, "dlg_btn_return_down", 529, 437), "quick help: pressed: its pressed picture");
        rig.hud.handle_mouse_up(q.x + 578, q.y + 450, SDL_BUTTON_LEFT, rig.sim, rig.cam);
        check(!rig.hud.is_modal_open(), "quick help: the release on Return closes it");
        HudRig plain(arc, wide);
        plain.hud.open_quick_help();
        const ArtSpy original = render_once(plain, arc);
        check(panel_copies(original, art) == 0 && original.blended.size() == 2 /* (the Classic corner's plate and button, no picture of the quick help) */ && [&] { for (const ArtSpy::Ev& e : original.events) if (e.name == "breturn1.bmp" && e.x == 529 && e.y == 437) return true; return false; }(), "quick help without the art: the original's two bitmaps and Return button");
    }

    // ---- the countdown card: the new look (countdown_card.hpp: 480 x 236 units at a scale of its own), the pieces in the card's units, the writing in the game's faces
    {
        HudRig rig(arc, wide);
        rig.hud.set_art(&art);
        std::array<CountdownSeat, 4> seats{};
        seats[0] = CountdownSeat{true, false, "Ann", net::PeerLink::None, 38, true};
        seats[1] = CountdownSeat{true, false, "Bea", net::PeerLink::Direct, 24, true};
        seats[2] = CountdownSeat{true, true, "Pip (bot, Hard)", net::PeerLink::None, -1, false};
        seats[3] = CountdownSeat{false, false, ""};
        rig.hud.start_match_countdown(23400, seats);
        const LayoutRect view = wide.view();
        const CountdownCard c = countdown_card(view, countdown_card_scale(view, wide.height, 1.0), 3);
        const ArtSpy spy = render_once(rig, arc);
        check(c.scale.f == 1.0 && c.scale.sheet == 1 && c.card.w == 480 && c.card.h == 236, "countdown: at one device pixel to a canvas pixel the card is 480 x 236, a unit a canvas pixel");
        check(panel_copies(spy, art) > 0 && once_at(spy, art, "cd_title", c.card.x + 132, c.card.y + 18) && once_at(spy, art, "cd_count_well", c.card.x + 19, c.card.y + 69), "countdown: the panel, the title and the count's well, in the middle of the map view");
        const std::vector<ArtSpy::Copy> rows = spy.of_sprite(art, "cd_row_well");
        bool rows_ok = rows.size() == 3;
        for (size_t i = 0; rows_ok && i < 3; ++i) rows_ok = rows[i].x == c.row[i].x - 5 && rows[i].y == c.row[i].y - 5;
        check(rows_ok && c.row[1].y - c.row[0].y == 30 && c.row[0].x == c.card.x + 188, "countdown: one well for each color that plays, 30 units apart, in one list");
        check(once_at(spy, art, "cd_dot_0", c.dot[0].x, c.dot[0].y) && once_at(spy, art, "cd_dot_1", c.dot[1].x, c.dot[1].y) && once_at(spy, art, "cd_dot_2", c.dot[2].x, c.dot[2].y) && copies_of(spy, art, "cd_dot_3") == 0,
              "countdown: a color square in each well, the colors of the seats that play");
        check(once_at(spy, art, "dlg_btn_fullscreen_up", c.button.x, c.button.y), "countdown: the native game's Fullscreen button, on the card's place");
        check(rig.hud.countdown_button_rect() == c.button, "countdown: the button's place is the new card's");
        const ArtSpy::Face* count = spy.face("24");
        const ArtSpy::Face* ann = spy.face("Ann (you)");
        const ArtSpy::Face* ping = spy.face("38 ms");
        check(count != nullptr && count->em_tenths == 820 && count->color.r == 246 && ann != nullptr && ann->em_tenths == 135 && spy.face("Bea") != nullptr && spy.face("Pip") != nullptr,
              "countdown: the count and the names are the game's face, at the sizes of the picture (82 and 13.5 units)");
        check(spy.face("server") != nullptr && spy.face("direct") != nullptr && spy.face("bot, Hard") != nullptr && spy.face("24 ms") != nullptr && ping != nullptr && spy.face("seconds to the start") != nullptr,
              "countdown: the small words (server for yourself, direct, a computer player's level) and the numbers");
        check(ping != nullptr && ping->color.g == 240 && spy.face("24 ms")->color.g == 240, "countdown: a number under 80 ms is green");
        bool lines = false;
        for (const ArtSpy::Line& line : spy.lines) lines = lines || (line.text == "Everybody is in. Say hello in the chat." && line.x == c.x(24) && line.baseline == c.y(211) && line.em_tenths == 120);
        check(lines && spy.lines.size() == 2, "countdown: the card's own words (the subtitle and the hint) are in the windows' face, the hint at the left");
        check(!spy.sheets.empty() && spy.sheets.front() == c.scale.sheet && spy.sheets.back() == 0, "countdown: the card picks its own sheet and gives the choice back");
        rig.hud.handle_mouse_down(c.button.x + 5, c.button.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);
        check(once_at(render_once(rig, arc), art, "dlg_btn_fullscreen_down", c.button.x, c.button.y), "countdown: held, the button's pressed picture");
        rig.hud.handle_mouse_up(c.button.x + c.button.w + 60, c.button.y + 5, SDL_BUTTON_LEFT, rig.sim, rig.cam);        // (let go away from it)
        // no face of the game (a renderer that only records): the nearest of the original's sizes
        const ArtSpy nofont = render_once(rig, arc, false);
        check(nofont.face_lines.empty() && has_text(nofont, "Ann (you)", FontSize::Px18) && has_text(nofont, "38 ms", FontSize::Px18) && has_text(nofont, "server", FontSize::Px14), "countdown: without the game's face the writing is the original's nearest sizes");
        // a larger screen: a whole number of device pixels to a unit, the sheet of that number
        const ArtSpy big = render_once(rig, arc, true, 2.0);
        const CountdownCard c2 = countdown_card(view, countdown_card_scale(view, wide.height, 2.0), 3);
        check(c2.scale.f == 1.5 && c2.scale.sheet == 3 && c2.card.w == 720 && c2.card.h == 354, "countdown: at 2 device pixels to a canvas pixel a unit is 3 of them: 1.5 canvas pixels, the 3x sheet");
        check(!big.sheets.empty() && big.sheets.front() == 3 && once_at(big, art, "dlg_btn_fullscreen_up", c2.button.x, c2.button.y), "countdown: and it is drawn so");
        HudRig plain(arc, wide);
        plain.hud.start_match_countdown(23400, seats);
        const ArtSpy original = render_once(plain, arc);
        check(panel_copies(original, art) == 0 && !original.of(ArtSpy::Kind::Fill).empty() && plain.hud.countdown_button_rect() == classic_countdown_layout(view).button, "countdown without the art: the original's flat card, at its own place");
    }
    // the card while the player looks round the map: the panel and its frame fade a lot, the wells, the writing and the button a little, the sentences turn light; solid again after a second
    {
        HudRig rig(arc, wide);
        rig.hud.set_art(&art);
        std::array<CountdownSeat, 4> seats{};
        seats[0] = CountdownSeat{true, false, "Ann", net::PeerLink::None, 38, true};
        seats[1] = CountdownSeat{true, false, "Bea", net::PeerLink::Relay, 118, true};
        rig.hud.start_match_countdown(23400, seats);
        const auto alphas = [&](const ArtSpy& spy) {
            std::pair<int32_t, int32_t> out{-1, -1};
            const std::vector<ArtSpy::Copy> panel = spy.of_sprite(art, "panel_c");
            const std::vector<ArtSpy::Copy> well = spy.of_sprite(art, "cd_count_well");
            if (!panel.empty()) out.first = panel[0].alpha;
            if (!well.empty()) out.second = well[0].alpha;
            return out;
        };
        check(alphas(render_once(rig, arc)) == std::make_pair(255, 255) && rig.hud.countdown_card_look() == 0, "look round: solid at the start");
        rig.hud.note_view_moved();
        rig.hud.update(rig.world(), 2);
        const std::pair<int32_t, int32_t> see = alphas(render_once(rig, arc));
        check(rig.hud.countdown_card_look() == 255 && see.first == kCardBackSeeThrough && see.second == kCardFrontSeeThrough, "look round: 100 ms after the view moved the panel shows 34 % of itself and the wells 86 %");
        const ArtSpy mid = render_once(rig, arc);
        const ArtSpy::Face* sentence_face = mid.face("38 ms");
        check(sentence_face != nullptr && sentence_face->alpha == kCardFrontSeeThrough, "look round: the numbers keep 86 % too");
        rig.hud.note_view_moved();
        for (int i = 0; i < 19; ++i) rig.hud.update(rig.world(), 1);                      // 0.95 s without a move: still see-through
        check(rig.hud.countdown_card_look() == 255, "look round: it stays see-through for a second after the view stopped");
        rig.hud.update(rig.world(), 1);
        check(rig.hud.countdown_card_look() < 255 && rig.hud.countdown_card_look() > 0, "look round: then it fades back in");
        for (int i = 0; i < 6; ++i) rig.hud.update(rig.world(), 1);
        check(rig.hud.countdown_card_look() == 0 && alphas(render_once(rig, arc)) == std::make_pair(255, 255) && rig.hud.countdown_card_opacity() == 255, "look round: solid again 300 ms later");
        rig.hud.note_view_moved();
        rig.hud.update(rig.world(), 2);
        check(rig.hud.countdown_card_opacity() == kCardFrontSeeThrough, "look round: the page's button follows the wells' level");
        // the classic card is one level, the whole card
        HudRig plain(arc, wide);
        plain.hud.start_match_countdown(23400, seats);
        plain.hud.note_view_moved();
        plain.hud.update(plain.world(), 2);
        ArtSpy flat(arc);
        plain.hud.render(flat, arc, plain.world(), plain.cam);
        check(std::count(flat.alphas_set.begin(), flat.alphas_set.end(), kClassicCardSeeThrough) == 1 && std::count(flat.alphas_set.begin(), flat.alphas_set.end(), kCardBackSeeThrough) == 0, "look round: the classic card is one level, the whole card");
    }

    // ---- the windows' face with the real renderer
    {
        RendererRig rig(arc, 960, 540);
        check(rig.ok, "(a renderer)");
        if (rig.ok) {
            check(!rig.renderer.dialog_text_ready(), "the real renderer has no windows' face until it is given the file");
            rig.renderer.set_dialog_font_path(art_folder() + "/LibreBaskerville-Regular.ttf");
            const int32_t w = rig.renderer.dialog_text_width("Do you really want to quit?", 200);
            check(rig.renderer.dialog_text_ready() && w > 200 && w < 296, "the shipped face opens; the quit question at 20 pixels is a line of about 270 pixels (" + std::to_string(w) + ")");
            check(rig.renderer.dialog_text_width("Do you really want to quit?", 260) > w && rig.renderer.dialog_text_width("", 200) == 0, "a larger size is wider; no text, no width");
            // a name in Cyrillic (Libre Baskerville has Latin only): the sentence is measured and drawn in the game's face, whole; a name in Latin letters stays in the windows' face
            const std::string cyrillic = "[\xD0\xA0\xD0\xA3\xD0\xA1] \xD0\x81\xD0\xB6\xD0\xB8\xD0\xBA wins the match.";
            check(rig.renderer.dialog_text_width(cyrillic, 200) == rig.renderer.face_text_width(cyrillic, 200) && rig.renderer.dialog_text_width("Zo\xC3\xAB wins the match.", 200) != rig.renderer.face_text_width("Zo\xC3\xAB wins the match.", 200),
                  "a sentence with a Cyrillic name is measured in the game's face, and one with a name in Latin letters (an e with a diaeresis too) in the windows' face");
            rig.renderer.begin_frame();
            rig.renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{219, 75, 19, 255});
            rig.renderer.draw_dialog_text("Do you really want to quit?", 100, 200, window_art::kInk, 200);
            const std::vector<uint8_t> px = rig.read();
            int ink = 0, below = 0;
            for (int32_t y = 180; y < 206; ++y) {
                for (int32_t x = 100; x < 100 + w; ++x) {
                    const uint8_t* p = &px[(static_cast<size_t>(y) * 960u + static_cast<size_t>(x)) * 4u];
                    ink += (std::abs(p[0] - 31) < 40 && std::abs(p[1] - 23) < 40 && std::abs(p[2] - 51) < 40) ? 1 : 0;
                }
            }
            for (int32_t x = 100; x < 100 + w; ++x) below += px[(static_cast<size_t>(215) * 960u + static_cast<size_t>(x)) * 4u] == 219 ? 0 : 1;
            check(ink > 300 && below == 0, "the real renderer draws the sentence in the dialogs' ink on its baseline (and nothing 15 pixels below it)");
            {   // the Cyrillic sentence is the game's face's picture, the same pixels as draw_face_text makes (no box in its place)
                rig.renderer.begin_frame();
                rig.renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{219, 75, 19, 255});
                rig.renderer.draw_dialog_text(cyrillic, 100, 200, window_art::kInk, 200);
                const std::vector<uint8_t> dialog_px = rig.read();
                rig.renderer.begin_frame();
                rig.renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{219, 75, 19, 255});
                rig.renderer.draw_face_text(cyrillic, 100, 200, window_art::kInk, 200);
                const std::vector<uint8_t> face_px = rig.read();
                check(dialog_px == face_px, "a sentence with a Cyrillic name is drawn as the game's face draws it (not as the windows' face, whose box stands for a letter that it lacks)");
            }
            // the game's own face at any size (the countdown card writes in it at the card's scale)
            check(rig.renderer.face_text_ready(), "the real renderer has the game's face");
            const int32_t fw = rig.renderer.face_text_width("38 ms", 135);
            check(fw > 15 && rig.renderer.face_text_width("38 ms", 270) > fw * 3 / 2 && rig.renderer.face_text_width("", 135) == 0, "the game's face: a larger size is wider (" + std::to_string(fw) + " at 13.5 pixels), no text no width");
            rig.renderer.begin_frame();
            rig.renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{20, 20, 20, 255});
            rig.renderer.draw_face_text("38 ms", 100, 300, assets::ColorRGBA{154, 240, 180, 255}, 135);
            const std::vector<uint8_t> fpx = rig.read();
            int lit = 0, under = 0;
            for (int32_t y = 288; y < 301; ++y) {
                for (int32_t x = 100; x < 100 + fw; ++x) lit += fpx[(static_cast<size_t>(y) * 960u + static_cast<size_t>(x)) * 4u + 1] > 150 ? 1 : 0;
            }
            for (int32_t x = 100; x < 100 + fw; ++x) under += fpx[(static_cast<size_t>(310) * 960u + static_cast<size_t>(x)) * 4u + 1] > 30 ? 1 : 0;
            check(lit > 20 && under == 0, "the game's face: the line stands on its baseline in its color");
            // the alpha scale: what is drawn under it keeps that share of its opacity
            rig.renderer.begin_frame();
            rig.renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{0, 0, 0, 255});
            rig.renderer.set_alpha_scale(128);
            rig.renderer.fill_rect(10, 10, 20, 20, assets::ColorRGBA{200, 100, 50, 255});
            rig.renderer.draw_face_text("38 ms", 100, 300, assets::ColorRGBA{200, 200, 200, 255}, 135);
            rig.renderer.set_alpha_scale(255);
            rig.renderer.fill_rect(40, 10, 20, 20, assets::ColorRGBA{200, 100, 50, 255});
            const std::vector<uint8_t> apx = rig.read();
            const auto at = [&](int32_t x, int32_t y, size_t c) { return static_cast<int>(apx[(static_cast<size_t>(y) * 960u + static_cast<size_t>(x)) * 4u + c]); };
            check(std::abs(at(20, 20, 0) - 100) <= 2 && std::abs(at(20, 20, 1) - 50) <= 2 && at(50, 20, 0) == 200, "the alpha scale: a fill under 128 is half of itself over what is behind, after it is whole again");
            int dim = 0;
            for (int32_t x = 100; x < 100 + fw; ++x) dim += at(x, 296, 0) > 0 && at(x, 296, 0) <= 130 ? 1 : 0;
            check(rig.renderer.alpha_scale() == 255 && dim > 0, "the alpha scale: the game's face is dimmed by it too");
            // a sheet of the card's own choice: the 11 x 11 color square of the first seat drawn at 33 x 33 from the 3x sheet is the sheet's pixels, 3 to a pixel
            const int32_t dot = art.find_sprite("cd_dot_0");
            check(dot >= 0, "the color square is in the set");
            if (dot >= 0) {
                rig.renderer.begin_frame();
                rig.renderer.fill_rect(0, 0, 960, 540, assets::ColorRGBA{255, 0, 255, 255});
                rig.renderer.set_art_scale(3);
                rig.renderer.draw_art(art, dot, 100, 100, 33, 33, 0, 0, 11, 11);
                rig.renderer.set_art_scale(0);
                const std::vector<uint8_t> dpx = rig.read();
                const auto dc = [&](int32_t x, int32_t y, size_t c) { return static_cast<int>(dpx[(static_cast<size_t>(y) * 960u + static_cast<size_t>(x)) * 4u + c]); };
                check(std::abs(dc(116, 116, 0) - 63) <= 2 && std::abs(dc(116, 116, 1) - 174) <= 2 && std::abs(dc(116, 116, 2) - 106) <= 2, "the card's sheet: the middle of the square is its color");
                check(dc(101, 116, 0) < 60 && dc(101, 116, 1) < 60 && std::abs(dc(100 + 4, 116, 0) - 63) <= 2 && dc(100 + 2, 116, 0) < 60 && dc(100 + 36, 116, 0) == 255, "the card's sheet: a dark rim 3 pixels wide, then the color, and nothing outside the 33 x 33");
            }
            rig.renderer.set_dialog_font_path(art_folder() + "/no_such_face.ttf");
            check(!rig.renderer.dialog_text_ready(), "a face that does not open: no face, the original's text is drawn");
        }
    }
}

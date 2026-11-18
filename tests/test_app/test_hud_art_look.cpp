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
            const LayoutRect help{layout.right(476), 7, 46, 23};
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

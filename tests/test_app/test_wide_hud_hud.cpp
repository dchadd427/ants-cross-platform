// The wide match screen, part 2: the HUD over the frame, the score slots, the dialogs and pages, the windows through update().
// (See test_wide_hud.cpp for what the whole program checks.)
#include "wide_hud_fixtures.hpp"

void test_hud_draws_the_frame(const assets::AssetArchive& arc) {
    group("hud-frame", "the HUD draws the frame first, piece by piece in the original's order, and the panel's animations where the layout puts them");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const auto* seq = arc.find_animation("uishell");
    const auto& frames = seq->subitems[0].frames;
    // the expected calls of the frame: the 14 pieces, the last stored first, as the spans of the model
    struct Expect {
        bool region;
        uint32_t id;
        LayoutRect dst;
        LayoutRect src;
    };
    auto expected_calls = [&](const ScreenLayout& layout) {
        std::vector<Expect> out;
        for (size_t k = frames.size(); k-- > 0;) {
            const assets::Sprite& sp = arc.get_sprite(frames[k].sprite_index);
            const ShellSpans spans = shell_spans(shell_rule(sp.name), frames[k].dx, frames[k].dy, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height), layout);
            for (size_t i = 0; i < spans.count; ++i) out.push_back({!spans.span[i].whole, frames[k].sprite_index, spans.span[i].dst, spans.span[i].src});
        }
        return out;
    };
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const bool classic = layout.is_classic();
        const std::string at = std::string(classic ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        const Spy spy = rig.frame();
        const std::vector<Expect> want = expected_calls(layout);
        // the first sprite or region calls of the frame are the pieces
        std::vector<Spy::Ev> drawn;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite || e.kind == Spy::Kind::Region) drawn.push_back(e);
            if (drawn.size() == want.size()) break;
        }
        bool same = drawn.size() == want.size();
        for (size_t i = 0; same && i < want.size(); ++i) {
            const Spy::Ev& e = drawn[i];
            if (want[i].region) {
                same = e.kind == Spy::Kind::Region && e.id == want[i].id && LayoutRect{e.x, e.y, e.w, e.h} == want[i].dst && LayoutRect{e.sx, e.sy, e.sw, e.sh} == want[i].src;
            } else {
                same = e.kind == Spy::Kind::Sprite && e.id == want[i].id && e.x == want[i].dst.x && e.y == want[i].dst.y;
            }
        }
        check(same, at + "the first " + std::to_string(want.size()) + " calls are the frame's spans, the last stored piece first");
        check(classic ? spy.count(Spy::Kind::Region) == 0 : spy.count(Spy::Kind::Region) == 22, at + (classic ? "no piece is drawn in parts" : "six pieces are drawn in parts: the bottom strip in seven (three cuts), the top bar and the four taller pieces in three each (22 calls)"));
        // the panel's backing fill comes before the frame, the whole panel
        const LayoutRect fill = layout.panel_fill();
        bool fill_first = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Fill) {
                fill_first = LayoutRect{e.x, e.y, e.w, e.h} == fill;
                break;
            }
            if (e.kind == Spy::Kind::Sprite) break;
        }
        check(fill_first, at + "the backing fill " + str(fill) + " is drawn before the frame");
    }

    // the animations of the panel: where the original draws them plus (dx, dy) of the part of the panel they are in
    {
        const sim::AntType type = sim::AntType::Bomber;
        for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
            const int32_t dx = layout.dx(), dy = layout.dy();
            const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
            HudRig rig(arc, layout);
            const uint32_t ant = rig.sim.spawn_unit(0, type, sim::TileCoord{5, 5});
            rig.sim.tick();
            rig.hud.select_ant(ant);
            rig.hud.update(rig.sim.get_world_state(), 0);
            const Spy spy = rig.frame();
            // the move pedestal at rest (butmovu: labmov (483, 141), butmovu (490, 162), butup (477, 157)), the bomb pedestal (butbomu: butbomu (549, 162), labbom (544, 140), butup (538, 157)) and
            // the Stop button (labcan (595, 180), butcanu (595, 198))
            check(spy.has_sprite_at("labmov.bmp", 483 + dx, 141) && spy.has_sprite_at("butmovu.bmp", 490 + dx, 162) && spy.has_sprite_at("butup.bmp", 477 + dx, 157), at + "the move pedestal is at (477, 157) + (dx, 0)");
            check(spy.has_sprite_at("butbomu.bmp", 549 + dx, 162) && spy.has_sprite_at("labbom.bmp", 544 + dx, 140) && spy.has_sprite_at("butup.bmp", 538 + dx, 157), at + "the bomb pedestal is at (538, 157) + (dx, 0)");
            check(spy.has_sprite_at("labcan.bmp", 595 + dx, 180) && spy.has_sprite_at("butcanu.bmp", 595 + dx, 198), at + "the Stop button is at (595, 180) + (dx, 0)");
            check(spy.has_sprite_at("butup.bmp", 477 + dx, 157), at + "(the pedestals are in the top part of the panel: down by 0)");
            // the buttons "Send to" and the covers of the chat input
            check(spy.has_sprite_at("butallu.bmp", 532 + dx, 443 + dy), at + "[All] is at (532, 443) + (dx, dy)");
            rig.hud.options().chat = false;
            const Spy off = rig.frame();
            check(off.has_sprite_at("chcovr2.bmp", 478 + dx, 421 + dy) && off.has_sprite_at("chcovr2.bmp", 478 + dx, 436 + dy) && off.has_sprite_at("chcovr2.bmp", 478 + dx, 445 + dy),
                  at + "with the chat off the three covers are at (478, 421 / 436 / 445) + (dx, dy)");
            rig.hud.options().chat = true;
            // the top bar's buttons: the small label of a hovered button and the art of a pressed one are the animations' parts moved by (dx, 0)
            const struct { const char* name; int32_t x; const char* hover; const char* pressed; } buttons[] = {{"Help", 476, "buthlpr", "buthlpd"}, {"Options", 525, "butoptr", "butoptd"}, {"Quit", 579, "butqitr", "butqitd"}};
            for (const auto& b : buttons) {
                for (const bool pressed : {false, true}) {
                    HudRig other(arc, layout);
                    other.hud.handle_mouse_motion(b.x + dx + 5, 7 + 5, other.sim, other.cam);
                    if (pressed) other.hud.handle_mouse_down(b.x + dx + 5, 7 + 5, SDL_BUTTON_LEFT, other.sim, other.cam);
                    const Spy shot = other.frame();
                    const auto* anim = arc.find_animation(pressed ? b.pressed : b.hover);
                    bool all = anim != nullptr && !anim->subitems.empty();
                    if (all) {
                        for (const auto& part : anim->subitems[0].frames) all = all && shot.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx + dx, part.dy);
                    }
                    check(all, at + std::string(b.name) + (pressed ? " pressed: the art is at its place + (dx, 0)" : " hovered: the label is at its place + (dx, 0)"));
                }
            }
        }
        // every sprite of the panel that the original draws right of x = 477 is moved by dx and no sprite of the panel is left at the old place: the pedestal art is not at x = 477
        HudRig rig(arc, wide);
        const uint32_t ant = rig.sim.spawn_unit(0, type, sim::TileCoord{5, 5});
        rig.sim.tick();
        rig.hud.select_ant(ant);
        rig.hud.update(rig.sim.get_world_state(), 0);
        const Spy spy = rig.frame();
        check(!spy.has_sprite_at("butup.bmp", 477, 157) && !spy.has_sprite_at("labcan.bmp", 595, 180), "960 x 540: nothing of the panel is left at the original's place");
    }
    // the pedestal slot draws its animation's parts moved by the offset it is given, both ways
    {
        PedestalSlot slot;
        slot.request(arc, PedestalKind::Move, 1, 0);
        Spy plain(arc);
        slot.draw(plain, arc, 20000);                                          // (the chain has run: the resting picture)
        Spy moved(arc);
        slot.draw(moved, arc, 20000, 7, 9);
        bool same = !plain.events.empty() && plain.events.size() == moved.events.size();
        for (size_t i = 0; same && i < plain.events.size(); ++i) same = moved.events[i].name == plain.events[i].name && moved.events[i].x == plain.events[i].x + 7 && moved.events[i].y == plain.events[i].y + 9;
        check(same, "PedestalSlot::draw moves every part of its picture by (dx, dy)");
        check(plain.has_sprite_at("butup.bmp", 477, 157), "(and with no offset the pedestal is the original's at (477, 157))");
    }
    // the egg tray and the lunchbox follow
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const int32_t dx = layout.dx();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        sim::WorldState world = rig.sim.get_world_state();
        world.player_eggs[0] = 3;
        rig.override_world = world;
        rig.hud.select_base(0);
        const Spy spy = rig.frame();
        const auto* egg3 = arc.find_animation("egg3");
        bool eggs = egg3 != nullptr && !egg3->subitems.empty();
        if (eggs) {
            for (const auto& part : egg3->subitems[0].frames) eggs = eggs && spy.has_sprite_at("egg.bmp", part.dx + dx, part.dy);
        }
        check(eggs, at + "the egg tray egg3 is at its place + (dx, 0)");
        check(spy.has_sprite_at("labcan.bmp", 595 + dx, 180) && spy.has_sprite_at("butcanu.bmp", 595 + dx, 198), at + "the Stop button of a selected hill is at (595, 180) + (dx, 0)");
    }
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const int32_t dx = layout.dx();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
        rig.sim.tick();
        sim::WorldState world = rig.sim.get_world_state();
        for (auto& a : world.ants) a.is_holding = true;
        rig.override_world = world;
        rig.hud.select_ant(ant);
        rig.hud.update(world, 0);
        const Spy spy = rig.frame();
        check(spy.has_sprite_at("lunchicon.bmp", 597 + dx, 131), at + "the lunchbox indicator is at (597, 131) + (dx, 0)");
    }
    // the glow of the Move pedestal (it follows the wall clock: any of its pictures): the pointer over the map with an own ant selected shows the Move cursor, and the settled pedestal glows
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const int32_t dx = layout.dx();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
        rig.sim.tick();
        rig.hud.select_ant(ant);
        rig.hud.update(rig.sim.get_world_state(), 0);
        rig.frame(1000);                                                       // the pedestal rises
        const sim::WorldState world = rig.sim.get_world_state();
        check(rig.hud.evaluate_cursor(300, 200, world, rig.sim.grid(), rig.cam) == CursorType::Move, at + "the pointer over the map with an own ant selected: the Move cursor");
        const Spy spy = rig.frame(20000);
        int glow = 0;
        bool placed = true;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite && e.name.rfind("butdef", 0) == 0) {
                ++glow;
                placed = placed && e.x >= 477 + dx && e.x < 540 + dx;
            }
        }
        check(glow >= 3 && placed, at + "the Move pedestal's glow (butdef pictures, " + std::to_string(glow) + " parts) is drawn at (477 .. 540) + dx");
    }
}

void test_score_slots(const assets::AssetArchive& arc) {
    group("slots", "the score boxes are slots: the local team's in the top bar, the others' in the bottom strip, in the order of the teams; the bottom boxes are spread evenly over the strip");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    // the model: the bottom strip is widened at three cuts, dx shared in thirds (the left cut takes the remainder), a box and its label move by what the cuts left of the box add
    check(classic.bottom_slot_count() == 3 && wide.bottom_slot_count() == 3 && ScreenLayout::with_size(1920, 1080).bottom_slot_count() == 3, "every picture holds three bottom slots (the original's)");
    check(wide.cut_share(0, 3) == 108 && wide.cut_share(1, 3) == 106 && wide.cut_share(2, 3) == 106 && wide.cut_share(0, 1) == 320 && wide.cut_share(0, 3) + wide.cut_share(1, 3) + wide.cut_share(2, 3) == 320,
          "960 x 540: the three cuts add 108, 106 and 106 (320 in all); a piece with one cut gets all of dx");
    check(wide.score_slot(1) == ScoreSlot{113, 209, 524, 213} && wide.score_slot(2) == ScoreSlot{377, 465, 524, 468} && wide.score_slot(3) == ScoreSlot{632, 719, 524, 722},
          "960 x 540: the boxes are at x 213, 468 and 722 (the labels anchored to them), 60 rows down");
    check(wide.score_slot(2).box_left - wide.score_slot(1).box_left == 255 && wide.score_slot(3).box_left - wide.score_slot(2).box_left == 254, "... spaced 255 and 254 apart: even");
    check(wide.score_slot(0) == ScoreSlot{632, 719, 4, 722}, "the local team's slot stays in the top bar, right anchored (box at 722)");
    check(wide.score_slot(4) == wide.score_slot(3) && wide.score_slot(40) == wide.score_slot(3) && classic.score_slot(4) == classic.score_slot(3) && classic.score_slot(9) == classic.score_slot(3),
          "a slot past the third is the third");
    {
        // an independent rule for any width: the share of the leftmost cut is dx - 2 * (dx / 3), the others' dx / 3
        bool all = true;
        bool even = true;
        for (const int32_t w : {640, 641, 642, 643, 644, 700, 854, 960, 1000, 1280, 1366, 1920, 2560, 3840}) {
            const ScreenLayout l = ScreenLayout::with_size(w, 480);
            const int32_t dx = w - 640;
            const int32_t third = dx / 3;
            const int32_t left = dx - 2 * third;
            const int32_t shift[3] = {left, left + third, left + 2 * third};
            const int32_t classic_box[3] = {105, 254, 402};
            const int32_t classic_label_right[3] = {101, 251, 399};
            const int32_t classic_label_w[3] = {96, 88, 87};                                   // the original's labels: [5, 101), [163, 251), [312, 399)
            for (size_t b = 0; b < 3; ++b) {
                const ScoreSlot s = l.score_slot(b + 1);
                all = all && s.box_left == classic_box[b] + shift[b] && s.label_right == classic_label_right[b] + shift[b] && s.label_left == s.label_right - classic_label_w[b] && s.top == 464;
            }
            all = all && l.score_slot(3).box_left == 402 + dx;                               // the last box is where it always was relative to the panel: right anchored
            even = even && (l.score_slot(2).box_left - l.score_slot(1).box_left) - (l.score_slot(3).box_left - l.score_slot(2).box_left) == 1;      // the original's own gaps are 149 and 148
        }
        check(all, "the slots of fourteen picture widths follow the rule (left cut dx - 2 * (dx / 3), the others dx / 3): box and label move together, the last box is right anchored");
        check(even, "... and the boxes stay evenly spaced: the first gap is exactly one pixel wider than the second, as the original's own (149 and 148), whatever the width");
    }
    // the art under the boxes: the recess of every box in the composed wide frame is the strip's own recess, so a box sits on its recess (and the labels' band stays plain)
    {
        const Pic frame = compose_from_spans(arc, wide);
        const assets::Sprite& strip = *arc.find_sprite("x17y461.bmp");
        for (size_t b = 1; b <= 3; ++b) {
            const ScoreSlot cl = classic.score_slot(b);
            const ScoreSlot ws = wide.score_slot(b);
            bool same = true;
            for (int32_t j = 0; j < 14; ++j) {
                for (int32_t i = 0; i < 54; ++i) {
                    const uint8_t v = strip.get_pixel(static_cast<uint32_t>(cl.box_left - 17 + i), static_cast<uint32_t>(cl.top + j - 461));
                    same = same && frame.at(ws.box_left + i, ws.top + j) == static_cast<int16_t>(v);
                }
            }
            check(same, "960 x 540: the recess under the box of slot " + std::to_string(b) + " (54 x 14 at " + std::to_string(ws.box_left) + ", " + std::to_string(ws.top) + ") is the strip's own recess, moved with its box");
        }
    }
    // the HUD: which team has which slot, by the roster and the local team
    struct Case {
        const char* what;
        uint8_t local;
        uint8_t roster;
    };
    const Case cases[] = {
        {"two teams (green local, red)", 0, 0x03},      {"three teams (green local, red, blue)", 0, 0x07}, {"four teams", 0, 0x0F},
        {"four teams, blue is the local team", 2, 0x0F}, {"two teams, black local, green", 3, 0x09},       {"three teams, red local", 1, 0x0D},
    };
    const uint8_t colors[4][3] = {{7, 67, 47}, {119, 0, 0}, {43, 39, 107}, {39, 39, 59}};
    for (const ScreenLayout& layout : {classic, wide}) {
        for (const Case& c : cases) {
            HudRig rig(arc, layout, c.local);
            rig.hud.set_roster_mask(c.roster);
            const Spy spy = rig.frame();
            const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": " + c.what + ": ";
            size_t other = 0;
            bool all = true;
            for (uint8_t team = 0; team < 4; ++team) {
                const size_t slot = team == c.local ? 0 : 1 + other++;
                const ScoreSlot want = layout.score_slot(slot);
                const bool exists = ((c.roster >> team) & 1u) != 0;
                bool box = false;
                bool cover = false;
                for (const Spy::Ev& e : spy.events) {
                    if (e.kind == Spy::Kind::Fill && e.w == 54 && e.h == 14 && e.color.r == colors[team][0] && e.color.g == colors[team][1] && e.color.b == colors[team][2]) box = e.x == want.box_left && e.y == want.top;
                    if (e.kind == Spy::Kind::Named && e.name == "scorcovr.bmp" && e.x == want.box_left - 2 && e.y == want.top - 1) cover = true;
                }
                all = all && (exists ? (box && !cover) : (!box && cover));
            }
            check(all, at + "every team that plays has its box in its slot, every team that does not has the slot covered");
        }
    }
    // the labels: right aligned to the slot's label_right, 14 px high, in white
    {
        HudRig rig(arc, wide, 0);
        rig.hud.set_roster_mask(0x0F);
        rig.hud.set_team_names({"", "Reddy", "Bluey", "Blacky"});
        const Spy spy = rig.frame();
        const ScoreSlot s1 = wide.score_slot(1), s3 = wide.score_slot(3);
        bool red = false, black = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Text && e.size == FontSize::Px14 && e.name == "Reddy:") red = e.x + 6 * 6 == s1.label_right && e.y == s1.top;
            if (e.kind == Spy::Kind::Text && e.size == FontSize::Px14 && e.name == "Blacky:") black = e.x + 7 * 6 == s3.label_right && e.y == s3.top;
        }
        check(red && black, "960 x 540: the labels end at their slots' label_right (the interface measures 6 px per character)");
    }
}

// =====================================================================================================================================================
// The network's ping and delay stay clear of the score boxes
// =====================================================================================================================================================

/// The ping and delay of a network match (latency_corner.hpp) stand in the corner row left of the version when the row is free, and on two lines above the plate when it is not. "Free" is the
/// match's limit: where the row of score boxes ends. It was the original's picture's number (460) in every layout: at 960 x 540, where the boxes are spread over the strip (the third one
/// ends at 778), a row of texts at x 644 .. 828 stood on the third box.
void test_latency_clear_of_scores(const assets::AssetArchive& arc) {
    group("latency", "the ping / delay readout of a network match does not touch a score box or cover, with two, three and four teams, in both pictures");
    using NP = net::NetGame::Phase;
    RendererRig measure(arc, 960, 540);                                      // the application's own font metrics
    check(measure.ok, "a renderer for the text metrics");
    if (!measure.ok) return;
    const assets::Sprite& cover = *arc.find_sprite("scorcovr.bmp");
    struct Roster { const char* what; uint8_t mask; };
    const Roster rosters[] = {{"two teams", 0x03}, {"three teams", 0x07}, {"four teams", 0x0F}};
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1280, 720), ScreenLayout::with_size(1920, 1080)}) {
        const std::string pic = std::to_string(layout.width) + " x " + std::to_string(layout.height) + ": ";
        const CornerPlate plate = CornerPlate::for_canvas(layout.width, layout.height);
        const int32_t text_h = measure.renderer.get_text_height(FontSize::Px12);
        const int32_t fps_w = measure.renderer.get_text_width("60 FPS", FontSize::Px12);
        const int32_t ver_w = measure.renderer.get_text_width(std::string(ants::VERSION_STRING), FontSize::Px12);
        const CornerRow row = CornerRow::of(plate, fps_w, ver_w, 36, FPS_OVERLAY_SPARK_H, text_h);
        const std::optional<int32_t> limit = latency_left_limit(true, NP::Playing, CornerScreen::Match, layout);
        check(limit.has_value() && *limit == layout.score_row_right() && (!layout.is_classic() || *limit == 460), pic + "the match's limit is the layout's (" + std::to_string(limit ? *limit : -1) + ", and the original's 460 in its own picture)");
        int32_t strip_right = 0;                                              // the right edge of what the bottom row holds, over every roster
        for (const Roster& r : rosters) {
            HudRig rig(arc, layout, 0);
            rig.hud.set_roster_mask(r.mask);
            const Spy spy = rig.frame();
            std::vector<LayoutRect> row_items;                                // the boxes and the covers of the bottom row
            const int32_t top = layout.score_slot(1).top;
            for (const Spy::Ev& e : spy.events) {
                if (e.kind == Spy::Kind::Fill && e.w == 54 && e.h == 14 && e.y == top) row_items.push_back(LayoutRect{e.x, e.y, 54, 14});
                if (e.kind == Spy::Kind::Named && e.name == "scorcovr.bmp" && e.y == top - 1) row_items.push_back(LayoutRect{e.x, e.y, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height)});
            }
            check(row_items.size() == 3, pic + r.what + ": the bottom row holds three boxes or covers (" + std::to_string(row_items.size()) + ")");
            int32_t right_edge = 0;
            for (const LayoutRect& it : row_items) right_edge = std::max(right_edge, it.right());
            strip_right = std::max(strip_right, right_edge);
            // the readout: nothing measured yet, a typical one and the widest, each laid out as the application does
            const struct { const char* what; std::optional<uint32_t> ping, delay; } readouts[] = {{"nothing measured", std::nullopt, std::nullopt}, {"12 / 80 ms", 12u, 80u}, {"9999 / 9999 ms", 9999u, 9999u}};
            for (const auto& ro : readouts) {
                const int32_t pw = measure.renderer.get_text_width(ping_text(ro.ping), FontSize::Px12);
                const int32_t dw = measure.renderer.get_text_width(delay_text(ro.delay), FontSize::Px12);
                const int32_t widest = measure.renderer.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + measure.renderer.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
                const LatencyCornerLayout l = layout_latency_corner(pw, dw, widest, text_h, row.version_x, row.text_y, *limit, plate);
                const LayoutRect ping_rect{l.ping_x, l.ping_y, pw, text_h};
                const LayoutRect delay_rect{l.delay_x, l.delay_y, dw, text_h};
                bool touches = false;
                for (const LayoutRect& it : row_items) {
                    touches = touches || (ping_rect.x < it.right() && it.x < ping_rect.right() && ping_rect.y < it.bottom() && it.y < ping_rect.bottom());
                    touches = touches || (delay_rect.x < it.right() && it.x < delay_rect.right() && delay_rect.y < it.bottom() && it.y < delay_rect.bottom());
                }
                check(!touches, pic + r.what + ", " + ro.what + ": the texts (" + (l.stacked ? "stacked" : "in a row") + ", ping at x " + std::to_string(l.ping_x) + ") touch no box or cover");
                check(l.ping_x >= right_edge, pic + r.what + ", " + ro.what + ": the texts begin right of the score row (" + std::to_string(right_edge) + ")");
            }
        }
        check(*limit == strip_right + ScreenLayout::kScoreRowMargin, pic + "the limit is the right edge of the last cover (" + std::to_string(strip_right) + ") and the margin of " + std::to_string(ScreenLayout::kScoreRowMargin));
        // the control: the original's number (460) in a wider picture, where the boxes are further right, puts the row of texts on the third box
        if (!layout.is_classic()) {
            const int32_t widest = measure.renderer.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + measure.renderer.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
            const LatencyCornerLayout old = layout_latency_corner(measure.renderer.get_text_width(ping_text(12u), FontSize::Px12), measure.renderer.get_text_width(delay_text(80u), FontSize::Px12), widest, text_h, row.version_x,
                                                                  row.text_y, LATENCY_LEFT_LIMIT_MATCH, plate);
            check(!old.stacked && old.ping_x < strip_right, pic + "the control: with the original's limit (460) the texts stand in a row at x " + std::to_string(old.ping_x) + ", on the boxes (they end at " + std::to_string(strip_right) + ")");
        }
    }
}

namespace {

// =====================================================================================================================================================
// 6. The pages of the original and its dialogs
// =====================================================================================================================================================

struct AllianceTable {
    sim::SimulationEngine sim;
    ViewportCamera cam;
    AllianceTable() {
        sim.init_test_world(60, 60, 5, 600000);
        const sim::TileCoord hills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
        for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, hills[p]);
        const char* names[4] = {"Alice", "Bob", "Carol", "Dave"};
        for (uint8_t p = 0; p < 4; ++p) sim.set_player_name(p, names[p]);
    }
};

}  // namespace

void test_dialogs_and_pages(const assets::AssetArchive& arc) {
    group("windows", "the dialogs, the options window and the quick help sit over the middle of the map view with the HUD visible around them, the pointer is taken back to their numbers");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    // (the page offset, (160, 30) at 960 x 540, is gone: no page of the original is centred over a margin any more, every screen outside a match is composed for the whole canvas: test_wide_pages)
    check(wide.options_offset() == LayoutPoint{160, 30} && wide.quick_help_offset() == LayoutPoint{77, 31} && classic.options_offset() == LayoutPoint{0, 0} && classic.quick_help_offset() == LayoutPoint{0, 0},
          "the options window's offset is (160, 30) and the quick help's (77, 31) at 960 x 540: the 442 x 440 card (16, 21) and the 640 x 480 page go to the middle (397, 271) of the view; (0, 0) in the original's picture");
    for (const ScreenLayout& l : {wide, ScreenLayout::with_size(1280, 720), ScreenLayout::with_size(1920, 1080)}) {
        const LayoutPoint o = l.options_offset();
        const LayoutPoint q = l.quick_help_offset();
        const LayoutRect v = l.view();
        const std::string at = std::to_string(l.width) + " x " + std::to_string(l.height) + ": ";
        check(16 + o.x + 221 == v.x + v.w / 2 && 21 + o.y + 220 == v.y + v.h / 2, at + "the options card's centre is the view's centre");
        check(q.x + 320 == v.x + v.w / 2 && q.y + 240 == v.y + v.h / 2, at + "the quick help page's centre is the view's centre");
        check(q.x >= v.x && q.y >= v.y && q.x + 640 <= v.right() && q.y + 480 <= v.bottom(), at + "the quick help page lies inside the map view: the HUD around it stays visible");
    }
    check(wide.modal_offset() == LayoutPoint{137, 59} && classic.modal_offset() == LayoutPoint{0, 0}, "the dialog offset is (137, 59) at 960 x 540: the frame (100, 100) 320 x 224 goes to the middle (397, 271) of the view; (0, 0) in the original's picture");
    {
        const LayoutPoint m = wide.modal_offset();
        const LayoutRect view = wide.view();
        check(100 + m.x + 160 == view.x + view.w / 2 && 100 + m.y + 112 == view.y + view.h / 2, "... the dialog's centre is the view's centre");
        const ScreenLayout big = ScreenLayout::with_size(1920, 1080);
        const LayoutPoint bm = big.modal_offset();
        check(100 + bm.x + 160 == big.view().x + big.view().w / 2 && 100 + bm.y + 112 == big.view().y + big.view().h / 2, "... and in a bigger picture too");
    }

    // --- the quit dialog
    for (const ScreenLayout& layout : {classic, wide}) {
        const bool is_classic = layout.is_classic();
        const LayoutPoint m = layout.modal_offset();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        rig.hud.open_quit_dialog();
        const Spy spy = rig.frame();
        // the dialog is drawn with its own numbers, between an origin and its reset
        std::vector<Spy::Ev> origins = spy.of(Spy::Kind::Origin);
        check(origins.size() >= 2 && origins.front().x == m.x && origins.front().y == m.y && origins.back().x == 0 && origins.back().y == 0, at + "the dialog is drawn under the origin (" + std::to_string(m.x) + ", " + std::to_string(m.y) + ") and the origin is cleared after it");
        bool frame_ok = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite && e.name == "dfram1.bmp") frame_ok = e.x == 100 && e.y == 100 && e.ox == m.x && e.oy == m.y;
        }
        check(frame_ok, at + "the frame is drawn at (100, 100) under that origin: it lands at (" + std::to_string(100 + m.x) + ", " + std::to_string(100 + m.y) + ")");
        // the pointer: a press on Yes at its place on screen
        bool quit = false;
        rig.hud.set_on_quit([&quit]() { quit = true; });
        const UIButton yes = rig.hud.quit_yes_button();
        rig.click(yes.x + m.x + 10, yes.y + m.y + 10);
        check(quit, at + "a click on Yes at its place on screen (" + std::to_string(yes.x + m.x + 10) + ", " + std::to_string(yes.y + m.y + 10) + ") quits");
        if (!is_classic) {
            quit = false;
            rig.hud.open_quit_dialog();
            rig.click(yes.x + 10, yes.y + 10);                                     // the original's place: now over the map, under the dialog's modal
            check(!quit && rig.hud.is_quit_dialog_open(), at + "a click at the original's place of Yes does nothing: the dialog is where the layout put it");
            // No closes it, at its place
            const UIButton no = rig.hud.quit_no_button();
            rig.click(no.x + m.x + 10, no.y + m.y + 10);
            check(!rig.hud.is_quit_dialog_open() && !quit, at + "No, at its place, closes the dialog");
            // hover follows: the Yes button is lit when the pointer is on it at its place
            rig.hud.open_quit_dialog();
            rig.hud.handle_mouse_motion(yes.x + m.x + 10, yes.y + m.y + 10, rig.sim, rig.cam);
            const Spy hover = rig.frame();
            check(hover.has_sprite_at("yes2.bmp", yes.x, yes.y), at + "the Yes button shows its hover picture when the pointer is on it at its place");
            rig.hud.handle_mouse_motion(yes.x + 10, yes.y + 10, rig.sim, rig.cam);
            const Spy not_hover = rig.frame();
            check(!not_hover.has_sprite_at("yes2.bmp", yes.x, yes.y), at + "... and not at the old place");
        }
    }

    // --- the "get ready" dialog
    {
        HudRig rig(arc, wide);
        rig.hud.start_match_modal();
        const Spy spy = rig.frame();
        bool framed = false, portrait = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite && e.name == "dfram1.bmp") framed = e.ox == 137 && e.oy == 59;
            if (e.kind == Spy::Kind::Sprite && e.ox == 137 && e.oy == 59 && e.x >= 200 && e.y >= 200 && e.x < 300 && e.y < 300) portrait = true;
        }
        check(framed && portrait, "960 x 540: the start dialog and the portrait in it are drawn under the dialog origin");
    }

    // --- the alliance dialogs: the pointer is taken back to the dialog's own numbers
    for (const ScreenLayout& layout : {classic, wide}) {
        const LayoutPoint m = layout.modal_offset();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        AllianceTable t;
        HUD hud;
        hud.set_ticks_function(&clock_fn);
        hud.init(1);
        hud.set_layout(layout);
        hud.set_team_names({"Alice", "Bob", "Carol", "Dave"});
        t.cam.set_view(layout.view());
        sim::Command invite;
        invite.type = sim::CommandType::AllianceInvite;
        invite.issuer = 0;
        invite.other_player = 1;
        t.sim.apply_command(invite);
        hud.update(t.sim.get_world_state(), 1);
        check(hud.alliance_dialog() == HUD::AllianceDialog::Invitation, at + "Bob sees the invitation of Alice");
        // Accept is at (152, 260) 80 x 24 of the dialog: the press at the old place (without the move) is nothing, at its place on screen it accepts
        auto press_release = [&](int32_t x, int32_t y) {
            hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, t.sim, t.cam);
            hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, t.sim, t.cam);
            hud.update(t.sim.get_world_state(), 1);
        };
        if (!layout.is_classic()) {
            press_release(152 + 10, 260 + 10);
            check(hud.alliance_dialog() == HUD::AllianceDialog::Invitation && t.sim.get_world_state().player_alliances[1] != 0, at + "a click at the original's place of Accept does nothing");
        }
        {
            Spy spy(arc);
            g_clock = 5000;
            hud.render(spy, arc, t.sim.get_world_state(), t.cam);
            bool accept_drawn = false;
            for (const Spy::Ev& e : spy.events) {
                if (e.kind == Spy::Kind::Sprite && e.name == "accpt1.bmp") accept_drawn = e.x == 152 && e.y == 260 && e.ox == m.x && e.oy == m.y;
            }
            check(accept_drawn, at + "Accept is drawn at (152, 260) under the dialog origin");
        }
        hud.handle_mouse_motion(152 + m.x + 10, 260 + m.y + 10, t.sim, t.cam);                 // the hover picture of Accept, at its place on screen only
        {
            Spy hover(arc);
            g_clock = 6000;
            hud.render(hover, arc, t.sim.get_world_state(), t.cam);
            check(hover.has_sprite_at("accpt2.bmp", 152, 260), at + "Accept shows its hover picture when the pointer is on it at its place on screen");
            hud.handle_mouse_motion(152 + 10, 260 + 10, t.sim, t.cam);
            Spy away(arc);
            hud.render(away, arc, t.sim.get_world_state(), t.cam);
            check(layout.is_classic() ? away.has_sprite_at("accpt2.bmp", 152, 260) : !away.has_sprite_at("accpt2.bmp", 152, 260), at + (layout.is_classic() ? "(the original's own place is its place)" : "... and not at the original's place of it"));
            hud.handle_mouse_motion(152 + m.x + 10, 260 + m.y + 10, t.sim, t.cam);
        }
        press_release(152 + m.x + 10, 260 + m.y + 10);
        check(t.sim.get_world_state().player_alliances[1] == 0 && t.sim.get_world_state().player_alliances[0] == 1, at + "a click at Accept's place on screen forms the team of Bob and Alice");
    }

    // --- the options window and the quick help of the original during a match: its own pictures, drawn over the map view with the HUD around them
    for (const ScreenLayout& layout : {classic, wide}) {
        const bool is_classic = layout.is_classic();
        const LayoutPoint o = layout.options_offset();
        const LayoutPoint q = layout.quick_help_offset();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        {   // the options: Return to Game is (351, 425) 98 x 26 of the window's own numbers
            HudRig rig(arc, layout);
            rig.hud.open_options();
            const Spy spy = rig.frame();
            // the HUD is drawn first, and still: the 14 pieces of the frame, then the window under its origin
            size_t i_origin = spy.events.size(), i_reset = spy.events.size(), i_clip = spy.events.size(), i_unclip = spy.events.size();
            bool clay = false;
            for (size_t i = 0; i < spy.events.size(); ++i) {
                const Spy::Ev& e = spy.events[i];
                if (e.kind == Spy::Kind::Origin && e.x == o.x && e.y == o.y && i_origin == spy.events.size()) i_origin = i;
                if (e.kind == Spy::Kind::Origin && e.x == 0 && e.y == 0 && i > i_origin && i_reset == spy.events.size()) i_reset = i;
                if (e.kind == Spy::Kind::Clip && i > i_origin && i_clip == spy.events.size()) i_clip = i;
                if (e.kind == Spy::Kind::ClearClip && i > i_clip && i_unclip == spy.events.size()) i_unclip = i;
                clay = clay || (e.kind == Spy::Kind::Fill && e.color.r == 219 && e.color.g == 75 && e.color.b == 19);
            }
            check(!clay, at + "no clay margin: the options window does not cover the match");
            size_t pieces_before = 0;
            for (size_t i = 0; i < i_origin && i < spy.events.size(); ++i) pieces_before += (spy.events[i].kind == Spy::Kind::Sprite || spy.events[i].kind == Spy::Kind::Region) ? size_t{1} : size_t{0};
            check(pieces_before >= 14 && spy.has_sprite_at("x480y126.bmp", 480 + layout.dx(), 126), at + "the HUD is drawn before the window: the 14 pieces of the frame (the status card at its place) come first");
            if (is_classic) {
                check(i_clip == spy.events.size(), at + "the original's own picture needs no clip");
            } else {
                const LayoutRect v = layout.view();
                bool clip_ok = false;
                for (const Spy::Ev& e : spy.events) {
                    if (e.kind == Spy::Kind::Clip && e.ox == o.x && e.oy == o.y) clip_ok = e.x == v.x - o.x && e.y == v.y - o.y && e.w == v.w && e.h == v.h;
                }
                check(i_origin < i_clip && i_clip < i_unclip && i_unclip <= i_reset && i_reset < spy.events.size() && clip_ok, at + "the window: its origin (160, 30), the clip of the map view (it stops at the view), the window, the clip off, the origin off");
            }
            // the hover picture of Return: the pointer on it at its place on screen
            rig.hud.handle_mouse_motion(351 + o.x + 10, 425 + o.y + 10, rig.sim, rig.cam);
            check(rig.frame().has_sprite_at("breturn2.bmp", 351, 425), at + "the options' Return button shows its hover picture when the pointer is on it at its place on screen");
            if (!is_classic) {
                rig.hud.handle_mouse_motion(351 + 10, 425 + 10, rig.sim, rig.cam);
                check(!rig.frame().has_sprite_at("breturn2.bmp", 351, 425), at + "... and not when it is at Return's own numbers");
            }
            // the pointer: Return at its place on screen closes the options; at the window's own numbers (without the move) it does not
            check(rig.hud.is_options_open(), at + "the options are open");
            if (!is_classic) {
                rig.click(351 + 10, 425 + 10);
                check(rig.hud.is_options_open(), at + "a click at Return's own numbers (351, 425) does nothing");
                rig.click(351 + o.x - 20, 425 + o.y + 10);
                check(rig.hud.is_options_open(), at + "a click left of Return does nothing");
            }
            rig.click(351 + o.x + 10, 425 + o.y + 10);
            check(!rig.hud.is_options_open(), at + "a click on Return at its place on screen (" + std::to_string(351 + o.x + 10) + ", " + std::to_string(425 + o.y + 10) + ") closes the options");
        }
        {   // the quick help: Return is (529, 437) 98 x 26
            HudRig rig(arc, layout);
            rig.hud.open_quick_help();
            const Spy spy = rig.frame();
            check(rig.hud.is_quick_help_open(), at + "the quick help is open");
            bool clay = false;
            for (const Spy::Ev& e : spy.events) clay = clay || (e.kind == Spy::Kind::Fill && e.color.r == 219 && e.color.g == 75 && e.color.b == 19);
            check(!clay, at + "no clay margin: the quick help does not cover the match");
            check(spy.has_sprite_at("x480y126.bmp", 480 + layout.dx(), 126), at + "the HUD is drawn under it (the status card at its place)");
            rig.hud.handle_mouse_motion(529 + q.x + 10, 437 + q.y + 10, rig.sim, rig.cam);
            check(rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "the quick help's Return button shows its hover picture when the pointer is on it at its place on screen");
            if (!is_classic) {
                rig.hud.handle_mouse_motion(529 + 10, 437 + 10, rig.sim, rig.cam);
                check(!rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "... and not when it is at Return's own numbers");
                rig.hud.handle_mouse_motion(529 + o.x + 90, 437 + o.y + 10, rig.sim, rig.cam);                // (the right end of the button at the options window's place: outside the quick help's own)
                check(!rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "... nor at the place of the options window's offset (the quick help has its own)");
                rig.click(529 + 10, 437 + 10);
                check(rig.hud.is_quick_help_open(), at + "a click at the Return button's own numbers does nothing");
                bool one_origin = false;
                for (const Spy::Ev& e : spy.events) one_origin = one_origin || (e.kind == Spy::Kind::Origin && e.x == 77 && e.y == 31);
                check(one_origin, at + "the quick help is drawn under the origin (77, 31)");
            }
            rig.click(529 + q.x + 10, 437 + q.y + 10);
            check(!rig.hud.is_quick_help_open(), at + "a click on Return at its place on screen closes the quick help");
        }
    }
}

/// The options window and the quick help take the pointer from the 50 ms poll of HUD::update as well as from the events (the INPUT task of the original): a control that is pressed and
/// released while frames and ticks run in between must still be clicked (the poll once fed the window the raw position of the pointer, the original's own numbers moved by the window's
/// offset: Return, the ON / OFF switches of the options and the quick help's Return could not be clicked in the 16:9 match, as the poll took the pointer off the control and cancelled the press)
void test_window_controls_through_update(const assets::AssetArchive& arc) {
    group("window-controls", "every control of the options window and of the quick help can be clicked through the HUD's 50 ms poll (press, update, release), at its place on screen only");
    const ScreenLayout classic = ScreenLayout::classic();
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    for (const ScreenLayout& layout : {classic, wide}) {
        const LayoutPoint o = layout.options_offset();
        const LayoutPoint q = layout.quick_help_offset();
        const bool is_classic = layout.is_classic();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        // press, run `polls` updates (the poll: HUD::update re-feeds the pointer to the window), release; `place` moves the control's own numbers to the screen
        auto press_update_release = [](HudRig& rig, int32_t x, int32_t y, int polls) {
            rig.hud.handle_mouse_motion(x, y, rig.sim, rig.cam);
            rig.hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            for (int i = 0; i < polls; ++i) rig.hud.update(rig.sim.get_world_state(), 1);
            rig.hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
        };
        // --- the options window
        {
            HudRig rig(arc, layout);
            rig.hud.open_options();
            auto at_screen = [&](int32_t x, int32_t y) { return std::make_pair(x + o.x, y + o.y); };
            // Return to Game
            {
                const auto p = at_screen(351 + 40, 425 + 12);
                press_update_release(rig, p.first, p.second, 3);
                check(!rig.hud.is_options_open(), at + "Return to Game: press, three polls, release: the window closes");
            }
            rig.hud.open_options();
            // Chat Off, Chat On, Quick Help Off, Quick Help On
            struct Toggle { const char* name; int32_t x; bool OptionsState::*field; bool value; };
            const Toggle toggles[] = {{"Chat Off", OptionsScreen::CHAT_OFF_X, &OptionsState::chat, false}, {"Chat On", OptionsScreen::CHAT_ON_X, &OptionsState::chat, true},
                                      {"Quick Help Off", OptionsScreen::HELP_OFF_X, &OptionsState::quick_help, false}, {"Quick Help On", OptionsScreen::HELP_ON_X, &OptionsState::quick_help, true}};
            for (const Toggle& t : toggles) {
                const auto p = at_screen(t.x + 20, OptionsScreen::TOGGLE_Y + 10);
                press_update_release(rig, p.first, p.second, 2);
                check(rig.hud.options().*(t.field) == t.value && rig.hud.is_options_open(), at + std::string(t.name) + ": press, two polls, release: the switch changes");
            }
            // the switches at the window's own numbers (without the offset) do nothing in a 16:9 picture
            if (!is_classic) {
                rig.hud.options().chat = true;
                press_update_release(rig, OptionsScreen::CHAT_OFF_X + 20, OptionsScreen::TOGGLE_Y + 10, 2);
                check(rig.hud.options().chat, at + "Chat Off at the window's own numbers (not moved) does nothing");
            }
            // the sliders: press on the thumb, drag, poll, release: the value follows
            const int32_t* values[3] = {&rig.hud.options().sound_volume, &rig.hud.options().music_volume, &rig.hud.options().scroll_speed};
            for (int i = 0; i < 3; ++i) {
                const int32_t before = *values[i];
                const int32_t thumb_x = OptionsScreen::SLIDER_X + ScreenSlider::TRACK_LEFT_OFFSET + (ScreenSlider::TRACK_SPAN * before) / 99;
                const int32_t y = OptionsScreen::SLIDER_Y[i] + 8;
                const int32_t target_x = thumb_x + (before > 50 ? -60 : 60);
                const auto from = at_screen(thumb_x, y);
                const auto to = at_screen(target_x, y);
                rig.hud.handle_mouse_motion(from.first, from.second, rig.sim, rig.cam);
                rig.hud.handle_mouse_down(from.first, from.second, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                rig.hud.handle_mouse_motion(to.first, to.second, rig.sim, rig.cam);
                rig.hud.update(rig.sim.get_world_state(), 1);              // the poll re-feeds the same position: the drag goes on
                rig.hud.handle_mouse_up(to.first, to.second, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                check(before > 50 ? *values[i] < before - 20 : *values[i] > before + 20, at + "slider " + std::to_string(i) + ": press on the thumb, drag, poll, release: the value moved from " + std::to_string(before) + " to " + std::to_string(*values[i]));
            }
            // an edit field takes the focus and the typed text
            {
                const auto p = at_screen(OptionsScreen::EDIT_X[2] + 20, OptionsScreen::EDIT_Y[2] + 5);
                press_update_release(rig, p.first, p.second, 1);
                rig.hud.handle_text_input("Q");
                check(!rig.hud.options().quick_chat[2].empty() && rig.hud.options().quick_chat[2].back() == 'Q', at + "the F11 field: a click at its place gives it the focus and it takes the typed letter");
            }
            check(rig.hud.is_options_open(), at + "(the window is still open: nothing but Return closes it)");
        }
        // --- the windows opened the way a player opens them: by CLICKING the HUD's own Options and Help buttons (press, release, an update between every event), then Return (the web build's
        //     report: after the click on Options a click on Return did nothing, even a second one, in 16:9; Ctrl+O first did not show it)
        {
            HudRig rig(arc, layout);
            auto step = [&](int32_t x, int32_t y, bool down_event, bool up_event) {
                rig.hud.update(rig.sim.get_world_state(), 1);
                rig.hud.handle_mouse_motion(x, y, rig.sim, rig.cam);
                rig.hud.update(rig.sim.get_world_state(), 1);
                if (down_event) {
                    rig.hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                    rig.hud.update(rig.sim.get_world_state(), 1);
                    rig.hud.update(rig.sim.get_world_state(), 1);
                }
                if (up_event) {
                    rig.hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                    rig.hud.update(rig.sim.get_world_state(), 1);
                }
            };
            const UIButton opt = rig.hud.options_button();
            step(opt.x + opt.w / 2, opt.y + opt.h / 2, true, true);
            check(rig.hud.is_options_open() && !rig.hud.options_button().is_pressed, at + "a click on the HUD's Options button opens the options window (updates between every event)");
            // a few frames of the pointer resting on the window's Return, then the click
            const int32_t rx = 351 + o.x + 40, ry = 425 + o.y + 12;
            step(rx, ry, false, false);
            check(rig.hud.is_options_open(), at + "(the window is still open while the pointer rests on Return)");
            step(rx, ry, true, true);
            check(!rig.hud.is_options_open(), at + "the first click on Return closes the window that the Options button opened");
            const UIButton help = rig.hud.help_button();
            step(help.x + help.w / 2, help.y + help.h / 2, true, true);
            check(rig.hud.is_quick_help_open() && !rig.hud.help_button().is_pressed, at + "a click on the HUD's Help button opens the quick help");
            const int32_t qx = 529 + q.x + 40, qy = 437 + q.y + 12;
            step(qx, qy, false, false);
            step(qx, qy, true, true);
            check(!rig.hud.is_quick_help_open(), at + "the first click on its Return closes it");
            // and again: the same buttons open them a second time and Return closes them a second time
            step(opt.x + opt.w / 2, opt.y + opt.h / 2, true, true);
            step(rx, ry, true, true);
            check(!rig.hud.is_options_open(), at + "the Options button and Return a second time: closed");
            // the click that closes a window does not act on the HUD under it: the pointer is over the map there, the selection and the pedestals stay as they were
            check(!rig.hud.is_quick_help_open() && !rig.hud.is_options_open(), at + "no window is left open");
        }
        // --- the quick help
        {
            HudRig rig(arc, layout);
            rig.hud.open_quick_help();
            press_update_release(rig, 529 + q.x + 40, 437 + q.y + 12, 3);
            check(!rig.hud.is_quick_help_open(), at + "the quick help's Return: press, three polls, release: it closes");
            if (!is_classic) {
                rig.hud.open_quick_help();
                press_update_release(rig, 529 + o.x + 40, 437 + o.y + 12, 3);           // where the OPTIONS window's offset would put it
                check(rig.hud.is_quick_help_open(), at + "the quick help's Return at the options window's offset does nothing (the quick help has its own place)");
                press_update_release(rig, 529 + 40, 437 + 12, 3);
                check(rig.hud.is_quick_help_open(), at + "... nor at its own numbers");
            }
        }
    }
}

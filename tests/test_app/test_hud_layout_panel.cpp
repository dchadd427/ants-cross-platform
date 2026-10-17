// HUD layout, part 1: the top bar, the score boxes, the home panel, the pedestals, the rubber band and the minimap.
// (See test_hud_layout.cpp for what the whole program checks.)
#include "hud_layout_fixtures.hpp"

namespace {

Off glyph_offset(const assets::AssetArchive& arc, const std::string& anim) {
    const auto* seq = arc.find_animation(anim);
    if (!seq || seq->subitems.empty() || seq->subitems[0].frames.empty()) return {0, 0};
    return {seq->subitems[0].frames[0].dx, seq->subitems[0].frames[0].dy};
}

void run_top_bar(const assets::AssetArchive& arc, uint32_t remaining_ms, int32_t local_score,
                 RecordingRenderer& rr, uint8_t local_player = 0) {
    sim::WorldState world;
    world.match_time_remaining_ms = remaining_ms;
    world.player_scores[local_player] = local_score;
    HUD hud;
    hud.init(local_player);
    ViewportCamera camera;
    hud.render(rr, arc, world, camera);
}

}  // namespace

void test_clock_digits(const assets::AssetArchive& arc) {
    std::printf("[hud] match clock uses the digit sprites at the original positions\n");
    struct Case { uint32_t ms; int tens_min; int min; int tens_sec; int sec; };
    static const Case cases[] = {
        { 12u * 60000u, 1, 2, 0, 0 },
        { 9u * 60000u + 5000u, -1, 9, 0, 5 },        // tens of minutes skipped when 0
        { 10u * 60000u + 59000u, 1, 0, 5, 9 },
        { 61000u, -1, 1, 0, 1 },
        { 0u, -1, 0, 0, 0 },
    };
    for (const auto& c : cases) {
        RecordingRenderer rr(arc);
        run_top_bar(arc, c.ms, 0, rr);
        auto expect_digit = [&](int digit, int x, const char* what) {
            const Off o = glyph_offset(arc, "dig" + std::to_string(digit));
            check(rr.has_sprite_at("dig" + std::to_string(digit) + ".bmp", x + o.dx, 6 + o.dy),
                  std::string("clock ") + std::to_string(c.ms) + " ms: " + what + " digit " + std::to_string(digit));
        };
        if (c.tens_min >= 0) expect_digit(c.tens_min, 70, "tens of minutes");
        else {
            // nothing is drawn in the tens-of-minutes slot
            bool any = false;
            for (const auto& s : rr.sprites) if (s.y < 10 && s.x >= 66 && s.x < 78 && s.name.rfind("dig", 0) == 0) any = true;
            check(!any, "tens of minutes slot empty");
        }
        expect_digit(c.min, 80, "minutes");
        const Off colon = glyph_offset(arc, "digc");
        check(rr.has_sprite_at("digc.bmp", 90 + colon.dx, 6 + colon.dy), "clock colon at x=90");
        expect_digit(c.tens_sec, 97, "tens of seconds");
        expect_digit(c.sec, 107, "seconds");
    }
}

void test_score_digits(const assets::AssetArchive& arc) {
    std::printf("[hud] scores use the digit sprites in a right-aligned 6-slot field\n");
    struct Case { int32_t score; };
    static const int32_t scores[] = { 0, 7, 200, 1250, 45000, 999999, 1234567 };
    for (int32_t score : scores) {
        RecordingRenderer rr(arc);
        run_top_bar(arc, 60000, score, rr, 0);
        // local score box (402,4): digits at (401, 6), 9 px slots
        const int32_t v = std::min<int32_t>(score, 999999);
        std::string s = std::to_string(v);
        const int first_slot = 6 - static_cast<int>(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            const std::string name = std::string("dig") + s[i];
            const Off o = glyph_offset(arc, name);
            const int32_t x = 401 + 9 * (first_slot + static_cast<int>(i)) + o.dx;
            check(rr.has_sprite_at(name + ".bmp", x, 6 + o.dy),
                  "score " + std::to_string(score) + " digit " + std::string(1, s[i]) + " at x=" + std::to_string(x));
        }
        // the box is filled with the team color first
        bool filled = false;
        for (const auto& f : rr.fills) if (f.x == 402 && f.y == 4 && f.w == 54 && f.h == 14) filled = true;
        check(filled, "local score box filled");
    }
}

// The score boxes (Ants.exe FUN_0100dbe2 0x100e1f0 - 0x100e222 for the slots, FUN_01021e36 for the boxes; docs 5.53): the slot follows the team index, absent and dropped teams
// are covered with scorcovr, an allied team's box is half its color and half its ally's with the two scores added
void test_score_boxes(const assets::AssetArchive& arc) {
    std::printf("[hud] score boxes: slots by team index, scorcovr for absent and dropped teams, allied boxes split with the summed score\n");
    const assets::ColorRGBA color[4] = {{7, 67, 47, 255}, {119, 0, 0, 255}, {43, 39, 107, 255}, {39, 39, 59, 255}};
    auto fill_at = [](const RecordingRenderer& rr, int32_t x, int32_t y, int32_t w, const assets::ColorRGBA& c) {
        for (const auto& f : rr.fills) {
            if (f.x == x && f.y == y && f.w == w && f.h == 14 && f.color.r == c.r && f.color.g == c.g && f.color.b == c.b) return true;
        }
        return false;
    };
    auto digits_at = [&](const RecordingRenderer& rr, int32_t x, int32_t y, int32_t value) {
        const std::string text = std::to_string(value);
        const int first_slot = 6 - static_cast<int>(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const std::string name = std::string("dig") + text[i];
            const Off o = glyph_offset(arc, name);
            if (!rr.has_sprite_at(name + ".bmp", x + 9 * (first_slot + static_cast<int>(i)) + o.dx, y + o.dy)) return false;
        }
        return true;
    };
    auto render = [&](HUD& hud, const sim::WorldState& world, RecordingRenderer& rr) {
        ViewportCamera camera;
        hud.render(rr, arc, world, camera);
    };
    auto has_text = [](const RecordingRenderer& rr, const std::string& t) {
        for (const auto& x : rr.texts) if (x.text == t) return true;
        return false;
    };

    // four teams, no alliances, local team 0: the top bar's box and the three bottom slots in index order; nothing is covered
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(fill_at(rr, 402, 4, 54, color[0]) && digits_at(rr, 401, 6, 10), "local team 0: its box in the top bar with its score");
        check(fill_at(rr, 105, 464, 54, color[1]) && digits_at(rr, 104, 466, 20), "team 1: bottom slot 0");
        check(fill_at(rr, 254, 464, 54, color[2]) && digits_at(rr, 253, 466, 30), "team 2: bottom slot 1");
        check(fill_at(rr, 402, 464, 54, color[3]) && digits_at(rr, 401, 466, 40), "team 3: bottom slot 2");
        check(rr.named("scorcovr.bmp").empty(), "nothing is covered");
    }
    // the local team is 1: the others keep index order (0, 2, 3 in slots 0, 1, 2)
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        HUD hud;
        hud.init(1);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(fill_at(rr, 402, 4, 54, color[1]) && fill_at(rr, 105, 464, 54, color[0]) && fill_at(rr, 254, 464, 54, color[2]) && fill_at(rr, 402, 464, 54, color[3]),
              "local team 1: team 0, 2 and 3 in the bottom slots 0, 1, 2");
    }
    // teams 0 and 2 play: team 1's slot (slot 0) and team 3's (slot 2) are covered, team 2 keeps slot 1 (the slots do not close up)
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        HUD hud;
        hud.init(0);
        hud.set_roster_mask(0x05);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(rr.has_sprite_at("scorcovr.bmp", 103, 463), "team 1 is absent: its box (105, 464) is covered at (103, 463) by scorcovr");
        check(fill_at(rr, 254, 464, 54, color[2]) && digits_at(rr, 253, 466, 30), "team 2 is in slot 1 (254, 464), not in the first slot");
        check(rr.has_sprite_at("scorcovr.bmp", 400, 463), "team 3 is absent: its box (402, 464) is covered at (400, 463)");
        check(!fill_at(rr, 105, 464, 54, color[2]) && !fill_at(rr, 105, 464, 54, color[1]), "nothing is filled in the covered slots");
        check(!has_text(rr, "Red:") && !has_text(rr, "Black:") && has_text(rr, "Blue:"), "only the teams that exist have a label");
        const auto cover = arc.find_sprite("scorcovr.bmp");
        check(cover != nullptr && cover->width == 58 && cover->height == 17, "scorcovr is 58 x 17");
    }
    // a team that drops out: its box is covered, its label stays
    {
        sim::WorldState world;
        world.player_alliances = {255, 255, 255, 255};
        world.player_scores = {10, 20, 30, 40};
        world.dropped_mask = 0x02;
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(rr.has_sprite_at("scorcovr.bmp", 103, 463) && !fill_at(rr, 105, 464, 54, color[1]), "team 1 has dropped out: its box is covered, its score is gone");
        check(has_text(rr, "Red:"), "its label stays");
        check(fill_at(rr, 254, 464, 54, color[2]), "the others keep their slots");
    }
    // teams 0 and 1 are allied: each box is half its color and half its ally's (the right half from left + 26) and shows the two scores added. The engine's
    // player_scores are the sums already (display scores): the box draws its team's entry once. (An earlier box added the ally's entry again: 1500.)
    {
        sim::WorldState world;
        world.player_alliances = {1, 0, 255, 255};
        world.player_scores = {750, 750, 30, 40};
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(fill_at(rr, 402, 4, 54, color[0]) && fill_at(rr, 428, 4, 28, color[1]), "the local box: green, and red from x = 428");
        check(digits_at(rr, 401, 6, 750), "it shows 300 + 450");
        check(fill_at(rr, 105, 464, 54, color[1]) && fill_at(rr, 131, 464, 28, color[0]), "the ally's box: red, and green from x = 131");
        check(digits_at(rr, 104, 466, 750), "it shows the same sum");
        check(!fill_at(rr, 254, 464, 28, color[0]) && !fill_at(rr, 254 + 26, 464, 28, color[0]), "a team without an ally has a single color");
        check(!digits_at(rr, 401, 6, 1500) && !digits_at(rr, 104, 466, 1500), "the sum is not added twice");
    }
    // the same with the real engine: two allied teams with 300 and 450 points, the boxes show 750, and a team that plays alone shows its own score
    {
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 60000);
        sim.set_player_score(0, 300);
        sim.set_player_score(1, 450);
        sim.set_player_score(2, 30);
        sim.form_alliance(0, 1);
        const sim::WorldState& world = sim.get_world_state();
        HUD hud;
        hud.init(0);
        RecordingRenderer rr(arc);
        render(hud, world, rr);
        check(digits_at(rr, 401, 6, 750), "the engine's allied boxes: the local box shows 300 + 450 = 750");
        check(digits_at(rr, 104, 466, 750), "and the ally's box shows the same 750");
        check(digits_at(rr, 253, 466, 30), "a team that plays alone shows its own 30");
    }
}

// Home anthill panel: egg tray = animation egg<N> (N = min(eggs, 9)) with absolute part coordinates, hatch pedestal only
// while eggs remain, Stop button from butcanu / butcand.
void test_home_panel(const assets::AssetArchive& arc) {
    std::printf("[hud] home anthill panel: egg tray, hatch pedestal and Stop button\n");
    static const uint32_t egg_counts[] = { 0, 1, 2, 5, 9, 12 };
    for (uint32_t eggs : egg_counts) {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.player_eggs[0] = eggs;
        HUD hud;
        hud.init(0);
        hud.select_base(0);
        render_settled(hud, arc, world, rr);

        const uint32_t n = std::min<uint32_t>(eggs, 9u);
        if (n > 0) {
            const auto* seq = arc.find_animation("egg" + std::to_string(n));
            check(seq != nullptr, "egg" + std::to_string(n) + " animation exists");
            if (seq) {
                for (const auto& part : seq->subitems[0].frames) {
                    check(rr.has_sprite_at("egg.bmp", part.dx, part.dy),
                          std::to_string(eggs) + " eggs: egg drawn at " + std::to_string(part.dx) + "," + std::to_string(part.dy));
                }
                check(rr.named("egg.bmp").size() == seq->subitems[0].frames.size(),
                      std::to_string(eggs) + " eggs: exactly " + std::to_string(n) + " egg sprites");
            }
            // hatch pedestal buteggu: base (477,157), icon (490,165), label (483,140)
            check(rr.has_sprite_at("butup.bmp", 477, 157), "hatch pedestal base at (477,157)");
            check(rr.has_sprite_at("buthatup.bmp", 490, 165), "hatch icon at (490,165)");
            check(rr.has_sprite_at("labhatch.bmp", 483, 140), "hatch label at (483,140)");
        } else {
            check(rr.named("egg.bmp").empty(), "no eggs: no egg sprites");
            check(rr.named("buthatup.bmp").empty(), "no eggs: no hatch pedestal");
            check(rr.named("labhatch.bmp").empty(), "no eggs: no hatch label");
        }
        // Stop button: label (595,180), button (595,198)
        check(rr.has_sprite_at("labcan.bmp", 595, 180), "Stop label at (595,180)");
        check(rr.has_sprite_at("butcanu.bmp", 595, 198), "Stop button at (595,198)");
    }
}

// Ally pedestal of a selected enemy base: animation butalyu / butalyd at absolute coordinates. Slot 1 of another player's hill holds it only
// with more than two players and while its owner is not allied with the local player (FUN_01027f07 mode 2).
void test_ally_pedestal(const assets::AssetArchive& arc) {
    std::printf("[hud] ally pedestal position\n");
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.anthills.resize(3);
        for (size_t i = 0; i < 3; ++i) world.anthills[i].team_id = static_cast<uint8_t>(i);
        HUD hud;
        hud.init(0);
        hud.select_base(1);
        render_settled(hud, arc, world, rr);
        check(rr.has_sprite_at("butup.bmp", 477, 157), "ally pedestal base at (477,157)");
        check(rr.has_sprite_at("butdipu.bmp", 485, 164), "ally icon at (485,164)");
        check(rr.has_sprite_at("labdib.bmp", 477, 142), "ally label at (477,142)");
    }
    {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.anthills.resize(2);                                  // two players: no ally pedestal
        for (size_t i = 0; i < 2; ++i) world.anthills[i].team_id = static_cast<uint8_t>(i);
        HUD hud;
        hud.init(0);
        hud.select_base(1);
        render_settled(hud, arc, world, rr);
        check(rr.named("butdipu.bmp").empty() && rr.named("labdib.bmp").empty(), "two players: no ally pedestal");
    }
    // the players that dropped out do not count (FUN_0100c58a counts the teams whose +0x64 is clear) and a dropped local team has none (0x1028188)
    const auto ally_icon_shown = [&](size_t hills, uint8_t dropped_mask) {
        RecordingRenderer rr(arc);
        sim::WorldState world;
        world.anthills.resize(hills);
        for (size_t i = 0; i < hills; ++i) world.anthills[i].team_id = static_cast<uint8_t>(i);
        world.dropped_mask = dropped_mask;
        HUD hud;
        hud.init(0);
        hud.select_base(1);
        render_settled(hud, arc, world, rr);
        return !rr.named("butdipu.bmp").empty();
    };
    check(ally_icon_shown(4, 0x00), "four live players: the ally pedestal");
    check(ally_icon_shown(4, 0x08), "four hills, one team dropped: three live players, the ally pedestal");
    check(!ally_icon_shown(4, 0x0C), "four hills, two teams dropped: two live players, no ally pedestal");
    check(!ally_icon_shown(3, 0x04), "three hills, one team dropped: two live players, no ally pedestal");
    check(!ally_icon_shown(4, 0x01), "the local team has dropped out itself: no ally pedestal");
}

// The rubber band (FUN_0102653f): a 1 px frame in (255, 0, 0) around the press point and the pointer, the pointer kept 1 px inside the view
// (x in [17, 457], y in [22, 460]), a direction without extent widened by 1 px on both sides, none while a pedestal is latched.
void test_rubber_band(const assets::AssetArchive& arc) {
    std::printf("[hud] rubber band\n");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1, 60000);
    const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    sim.tick();
    ViewportCamera cam;
    cam.world_x = 200;
    cam.world_y = 200;
    auto band = [&](HUD& hud, int32_t px, int32_t py, int32_t mx, int32_t my) {
        hud.handle_mouse_down(px, py, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_motion(mx, my, sim, cam);
        RecordingRenderer rr(arc);
        hud.render(rr, arc, sim.get_world_state(), cam);
        std::vector<FillDraw> red;
        for (const auto& r : rr.rects) if (r.color.r == 255 && r.color.g == 0 && r.color.b == 0) red.push_back(r);
        hud.handle_mouse_up(mx, my, SDL_BUTTON_LEFT, sim, cam);
        return red;
    };
    {
        HUD hud;
        hud.init(0);
        auto r = band(hud, 100, 100, 150, 140);
        check(r.size() == 1 && r[0].x == 100 && r[0].y == 100 && r[0].w == 50 && r[0].h == 40, "the band is the bounding box of the press and the pointer");
        r = band(hud, 150, 140, 100, 100);
        check(r.size() == 1 && r[0].x == 100 && r[0].y == 100 && r[0].w == 50 && r[0].h == 40, "dragging up and to the left gives the same box");
        r = band(hud, 100, 100, 100, 100);
        check(r.size() == 1 && r[0].x == 99 && r[0].y == 99 && r[0].w == 2 && r[0].h == 2, "a stationary press is a 2 x 2 dot");
        r = band(hud, 100, 100, 160, 100);
        check(r.size() == 1 && r[0].x == 100 && r[0].y == 99 && r[0].w == 60 && r[0].h == 2, "a zero height is widened by 1 px on both sides");
        r = band(hud, 300, 300, 500, 500);
        check(r.size() == 1 && r[0].x == 300 && r[0].y == 300 && r[0].w == 157 && r[0].h == 160, "the pointer is kept inside the view: (457, 460)");
        r = band(hud, 300, 300, 0, 0);
        check(r.size() == 1 && r[0].x == 17 && r[0].y == 22 && r[0].w == 283 && r[0].h == 278, "and at the top left: (17, 22)");
        check(r.size() == 1 && r[0].color.a == 255, "opaque");
    }
    {
        HUD hud;
        hud.init(0);
        hud.select_ant(ant);
        sim.tick();
        hud.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, sim, cam);     // the move pedestal: latched, so no band exists
        hud.handle_mouse_up(500, 170, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_move_latched(), "the move pedestal is latched");
        auto r = band(hud, 100, 100, 150, 140);
        check(r.empty(), "no band while a pedestal is latched");
    }
}

namespace {

// Pedestal transition chains: every row of the original's transition table (Ants.exe FUN_01028491, table 0x1002b68)
struct ChainRow { const char* cur; int cur_mode; const char* next; int next_mode; int remove; const char* chain; };

const ChainRow kChainRows[] = {
    {"hidden", 1, "move", 1, 0, "trnbmovu|butmovu"},
    {"hidden", 1, "move", 2, 0, "trnbmovu|butmovu"},
    {"hidden", 1, "bomb", 1, 0, "trnbbomu|butbomu"},
    {"hidden", 1, "bomb", 2, 0, "trnbbomu|butbomu"},
    {"hidden", 1, "thief", 1, 0, "trnbthfu|butthfu"},
    {"hidden", 1, "thief", 2, 0, "trnbthfu|butthfu"},
    {"hidden", 1, "egg", 1, 0, "trnbeggu|buteggu"},
    {"hidden", 1, "egg", 2, 0, "trnbeggu|buteggu"},
    {"move", 1, "hidden", 1, 1, "trnbmovd"},
    {"move", 1, "hidden", 2, 1, "trnbmovd"},
    {"move", 1, "move", 2, 0, "butmov2d|butmovd"},
    {"move", 1, "bomb", 1, 0, "trnamovd|trnabomu|butbomu"},
    {"move", 1, "thief", 1, 0, "trnamovd|trnathfu|butthfu"},
    {"move", 1, "egg", 1, 0, "trnamovd|trnaeggu|buteggu"},
    {"move", 2, "hidden", 1, 1, "trnbmovd"},
    {"move", 2, "hidden", 2, 1, "trnbmovd"},
    {"move", 2, "move", 1, 0, "butmovu"},
    {"move", 2, "bomb", 1, 0, "trnamovd|trnabomu|butbomu"},
    {"move", 2, "thief", 1, 0, "trnamovd|trnathfu|butthfu"},
    {"move", 2, "egg", 1, 0, "trnamovd|trnaeggu|buteggu"},
    {"bomb", 1, "hidden", 1, 1, "trnbbomd"},
    {"bomb", 1, "hidden", 2, 1, "trnbbomd"},
    {"bomb", 1, "move", 1, 0, "trnabomd|trnamovu|butmovu"},
    {"bomb", 1, "bomb", 2, 0, "butbom2d|butbomd"},
    {"bomb", 1, "thief", 1, 0, "trnabomd|trnathfu|butthfu"},
    {"bomb", 1, "egg", 1, 0, "trnabomd|trnaeggu|buteggu"},
    {"bomb", 2, "hidden", 1, 1, "trnbbomd"},
    {"bomb", 2, "hidden", 2, 1, "trnbbomd"},
    {"bomb", 2, "move", 1, 0, "trnabomd|trnamovu|butmovu"},
    {"bomb", 2, "bomb", 1, 0, "butbomu"},
    {"bomb", 2, "thief", 1, 0, "trnabomd|trnathfu|butthfu"},
    {"bomb", 2, "egg", 1, 0, "trnabomd|trnaeggu|buteggu"},
    {"thief", 1, "hidden", 1, 1, "trnbthfd"},
    {"thief", 1, "hidden", 2, 1, "trnbthfd"},
    {"thief", 1, "move", 1, 0, "trnathfd|trnamovu|butmovu"},
    {"thief", 1, "bomb", 1, 0, "trnathfd|trnabomu|butbomu"},
    {"thief", 1, "thief", 2, 0, "butthf2d|butthfd"},
    {"thief", 1, "egg", 1, 0, "trnathfd|trnaeggu|buteggu"},
    {"thief", 2, "hidden", 1, 1, "trnbthfd"},
    {"thief", 2, "hidden", 2, 1, "trnbthfd"},
    {"thief", 2, "move", 1, 0, "trnathfd|trnamovu|butmovu"},
    {"thief", 2, "bomb", 1, 0, "trnathfd|trnabomu|butbomu"},
    {"thief", 2, "thief", 1, 0, "butthfu"},
    {"thief", 2, "egg", 1, 0, "trnathfd|trnaeggu|buteggu"},
    {"egg", 1, "hidden", 1, 1, "trnbeggd"},
    {"egg", 1, "hidden", 2, 1, "trnbeggd"},
    {"egg", 1, "move", 1, 0, "trnaeggd|trnamovu|butmovu"},
    {"egg", 1, "bomb", 1, 0, "trnaeggd|trnabomu|butbomu"},
    {"egg", 1, "thief", 1, 0, "trnaeggd|trnathfu|butthfu"},
    {"egg", 1, "egg", 2, 0, "butegg2d|buteggd"},
    {"egg", 2, "hidden", 1, 1, "trnbeggd"},
    {"egg", 2, "hidden", 2, 1, "trnbeggd"},
    {"egg", 2, "move", 1, 0, "trnaeggd|trnamovu|butmovu"},
    {"egg", 2, "bomb", 1, 0, "trnaeggd|trnabomu|butbomu"},
    {"egg", 2, "thief", 1, 0, "trnaeggd|trnathfu|butthfu"},
    {"egg", 2, "egg", 1, 0, "buteggu"},
};

PedestalKind kind_from_name(const std::string& n) {
    if (n == "move") return PedestalKind::Move;
    if (n == "bomb") return PedestalKind::Bomb;
    if (n == "attack") return PedestalKind::Attack;
    if (n == "fire") return PedestalKind::Fire;
    if (n == "thief") return PedestalKind::Thief;
    if (n == "ally") return PedestalKind::Ally;
    if (n == "swim") return PedestalKind::Swim;
    if (n == "egg") return PedestalKind::Egg;
    return PedestalKind::Hidden;
}

}  // namespace

void test_pedestal_chains(const assets::AssetArchive& arc) {
    std::printf("[hud] pedestal transition chains equal the original's table\n");
    for (const auto& row : kChainRows) {
        const PedestalChain c = pedestal_chain(kind_from_name(row.cur), row.cur_mode, kind_from_name(row.next), row.next_mode);
        std::string got;
        for (size_t i = 0; i < c.animations.size(); ++i) got += (i ? "|" : "") + c.animations[i];
        check(got == row.chain, std::string(row.cur) + "," + std::to_string(row.cur_mode) + " -> " + row.next + "," +
                                std::to_string(row.next_mode) + ": " + got + " expected " + row.chain);
        check(c.remove_after_last == (row.remove != 0), std::string(row.cur) + " -> " + row.next + " removal flag");
        for (const auto& name : c.animations) check(arc.find_animation(name) != nullptr, "animation exists: " + name);
    }
    // Every kind has all seven animations
    static const PedestalKind kinds[] = { PedestalKind::Move, PedestalKind::Bomb, PedestalKind::Attack, PedestalKind::Fire,
                                          PedestalKind::Thief, PedestalKind::Ally, PedestalKind::Swim, PedestalKind::Egg };
    for (PedestalKind k : kinds) {
        for (const std::string& name : { pedestal_up_anim(k), pedestal_down_anim(k), pedestal_press_anim(k), pedestal_swap_out_anim(k),
                                         pedestal_swap_in_anim(k), pedestal_sink_anim(k), pedestal_rise_anim(k) }) {
            check(arc.find_animation(name) != nullptr, "pedestal animation exists: " + name);
        }
    }
}

// The hatch pedestal rises with trnbeggu (9 x 60 ms) and then rests on buteggu; deselecting sinks it with trnbeggd
void test_pedestal_timeline(const assets::AssetArchive& arc) {
    std::printf("[hud] pedestal rise and sink play in real time\n");
    sim::WorldState world;
    world.player_eggs[0] = 3;
    HUD hud;
    hud.init(0);
    hud.select_base(0);
    hud.set_ticks_function(&test_clock);
    ViewportCamera camera;
    const auto* rise = arc.find_animation("trnbeggu");
    check(rise != nullptr && rise->subitems.size() == 9, "trnbeggu has 9 frames");
    if (!rise) return;
    for (uint32_t t : { 0u, 59u, 60u, 300u, 539u }) {
        g_now_ms = t;
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);
        const size_t frame = std::min<size_t>(t / 60, 8);
        for (const auto& part : rise->subitems[frame].frames) {
            check(rr.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx, part.dy),
                  "rise frame " + std::to_string(frame) + " at t=" + std::to_string(t));
        }
        check(rr.named("buteggu.bmp").empty() && rr.named("buthatup.bmp").empty(), "resting art not shown while rising");
    }
    g_now_ms = 540;
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);
        check(rr.has_sprite_at("buthatup.bmp", 490, 165), "resting hatch pedestal at 540 ms");
    }
    // deselect: the pedestal sinks (trnbeggd, 9 x 60 ms) and then disappears
    hud.select_base(-1);
    g_now_ms = 1000;
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);   // sink starts
        const auto* sink = arc.find_animation("trnbeggd");
        check(sink != nullptr, "trnbeggd exists");
        if (sink) for (const auto& part : sink->subitems[0].frames)
            check(rr.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx, part.dy), "sink frame 0");
        if (std::getenv("HUD_DEBUG")) {
            for (const auto& sp : rr.sprites) if (sp.x >= 470 && sp.x < 600 && sp.y >= 130 && sp.y < 240) std::fprintf(stderr, "  drawn %s at %d,%d\n", sp.name.c_str(), sp.x, sp.y);
            if (sink) for (const auto& part : sink->subitems[0].frames) std::fprintf(stderr, "  expect %s at %d,%d\n", arc.get_sprite(part.sprite_index).name.c_str(), part.dx, part.dy);
        }
    }
    g_now_ms = 1000 + 540;
    {
        RecordingRenderer rr(arc);
        hud.render(rr, arc, world, camera);
        check(rr.named("buthatup.bmp").empty() && rr.named("butup.bmp").empty(), "pedestal gone after the sink");
    }
}

// Minimap (Ants.exe FUN_01009596): a 119x91 image at (480,35) with the class speckle colors, the object table and the fog table.
// The dots of the ants and plants (FUN_01009988) and the view frame are drawn over it as rectangles (test_minimap_dots).
void test_minimap(const assets::AssetArchive& arc) {
    std::printf("[hud] minimap image\n");
    const auto& pal = arc.get_palette();
    auto rgb_of = [&](int idx) { return std::array<uint8_t, 3>{ pal[static_cast<size_t>(idx)].r, pal[static_cast<size_t>(idx)].g, pal[static_cast<size_t>(idx)].b }; };
    auto pixel = [&](const RecordingRenderer::Image& im, int x, int y) {
        const uint8_t* p = &im.rgba[(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x)) * 4u];
        return std::array<uint8_t, 3>{ p[0], p[1], p[2] };
    };

    sim::WorldState world;
    world.width = 60; world.height = 60;
    world.cells.assign(3600, sim::TileCell{});
    // west third: water (class 2), middle: mud (class 3), east third: dirt (class 4); rest gravel (class 0)
    for (uint32_t y = 0; y < 60; ++y) for (uint32_t x = 0; x < 60; ++x) {
        auto& c = world.cells[y * 60 + x];
        if (x < 10) { c.terrain_type = sim::TERRAIN_WATER; c.surface_type = sim::SurfaceType::Water; }
        else if (x >= 20 && x < 30) { c.surface_type = sim::SurfaceType::Mud; c.is_mud = true; }
        else if (x >= 40 && x < 50) { c.surface_type = sim::SurfaceType::Gravel; }
    }
    // an anthill tile of the black colony (animation 245, color 239), the original's bomb id 129 at (15,40), the remake's own planted bomb (id 100 + team) at (16,40), a
    // rock (animation 293) at (12,20)
    world.cells[30 * 60 + 15].interactive_id = 245;
    world.cells[40 * 60 + 15].interactive_id = 129;
    world.cells[40 * 60 + 16].interactive_id = sim::BOMB_GREEN;
    world.cells[20 * 60 + 12].interactive_id = 293;

    RecordingRenderer rr(arc);
    HUD hud;
    hud.init(0);
    ViewportCamera camera;
    hud.render(rr, arc, world, camera);
    check(rr.images.size() == 1, "minimap drawn as one image");
    if (rr.images.empty()) return;
    const auto& im = rr.images[0];
    check(im.x == 480 && im.y == 35 && im.w == 119 && im.h == 91, "minimap rect (480,35) 119x91");

    auto in_set = [&](std::array<uint8_t, 3> c, std::initializer_list<int> idxs) {
        for (int i : idxs) if (rgb_of(i) == c) return true;
        return false;
    };
    // pixel (x,y) -> cell (x*60/119, y*60/91); sample interior pixels of each band
    bool water_ok = true, gravel_ok = true, mud_ok = true, dirt_ok = true;
    for (int y = 2; y < 30; ++y) {
        for (int x = 2; x < 17; ++x) if (!in_set(pixel(im, x, y), {37})) water_ok = false;          // cells 1..8
        for (int x = 22; x < 38; ++x) if (!in_set(pixel(im, x, y), {251, 201, 249})) gravel_ok = false; // cells 11..19
        for (int x = 42; x < 58; ++x) if (!in_set(pixel(im, x, y), {77})) mud_ok = false;           // cells 21..29
        for (int x = 82; x < 98; ++x) if (!in_set(pixel(im, x, y), {231, 232, 233})) dirt_ok = false; // cells 41..49
    }
    check(water_ok, "water cells use class color 37");
    check(gravel_ok, "gravel cells use the class-0 speckle colors 251/201/249");
    check(mud_ok, "mud cells use class color 77");
    check(dirt_ok, "dirt cells use the class-4 speckle colors 231/232/233");

    // the painter gives every pixel of a cell the color of the object in it: the hill (pixel (30,46) is cell (15,30)) and the rock of tile (12,20) (pixels 24 .. 25 x 31)
    check(pixel(im, 30, 46) == rgb_of(239), "hill drawn with color 239 at its cell");
    check(pixel(im, 24, 31) == rgb_of(51) && pixel(im, 25, 31) == rgb_of(51), "a rock (animation 293) shows color 51 on its cell");
    // a bomb shows the terrain: the original's ids 129 .. 132 and the remake's own (100 .. 103) alike (pixels (30,61) and (32,61) are cells (15,40) and (16,40))
    check(in_set(pixel(im, 30, 61), {251, 201, 249}), "the bomb of id 129 is not drawn on the minimap");
    check(in_set(pixel(im, 32, 61), {251, 201, 249}), "a bomb planted by the remake is not drawn on the minimap");
    // no dot per object cell any more: the hill's cell next to it (16,30) shows the terrain
    check(in_set(pixel(im, 32, 46), {251, 201, 249}), "no dot around an object: the next cell shows the terrain");

    // a tile id that the table has no record for has color 0 (the table is cleared first); the placeholder of a bomb that is being planted (0xa0) is one
    world.cells[5 * 60 + 40].interactive_id = sim::TILE_RESERVED;
    // a fire wall (134, color 250) and a power-up dropped by the remake (0x8000 | type 4, the pu_comb tile 62, color 171)
    world.cells[5 * 60 + 45].interactive_id = sim::TILE_FIREWALL;
    world.cells[5 * 60 + 47].interactive_id = static_cast<uint16_t>(0x8000u | 4u);
    world.cells[5 * 60 + 47].is_powerup = true;
    world.cells[5 * 60 + 47].powerup_type = 4;
    RecordingRenderer ro(arc);
    hud.render(ro, arc, world, camera);
    const auto& oim = ro.images[0];
    check(pixel(oim, 80, 8) == rgb_of(0), "a tile id without a record in the color table shows palette color 0");
    check(pixel(oim, 90, 8) == rgb_of(250), "a fire wall shows color 250");
    check(pixel(oim, 94, 8) == rgb_of(171), "a dropped power-up shows the color of its tile (171)");

    // Fog: unexplored cells use the fog colors by class
    world.fog_of_war_enabled = true;
    world.fog_revealed.assign(3600, 0);
    for (uint32_t y = 0; y < 60; ++y) for (uint32_t x = 20; x < 30; ++x) world.fog_revealed[y * 60 + x] = 1;   // reveal the mud band
    RecordingRenderer rf(arc);
    hud.render(rf, arc, world, camera);
    const auto& fim = rf.images[0];
    check(pixel(fim, 5, 20) == rgb_of(225), "unexplored water shows fog color 225");
    check(pixel(fim, 30, 20) == rgb_of(244), "unexplored gravel shows fog color 244");
    check(pixel(fim, 50, 20) == rgb_of(77), "explored mud keeps its color");
    check(pixel(fim, 90, 20) == rgb_of(236), "unexplored dirt (class 4) shows fog color 236");
    // in the fog only what gets removed or given out falls back to the terrain (power-ups, food, fire walls); every other object keeps its color, the hills of the enemy too
    check(pixel(fim, 30, 46) == rgb_of(239), "a hill keeps its color in the fog");
    check(pixel(fim, 24, 31) == rgb_of(51), "a rock keeps its color in the fog");
    check(pixel(fim, 80, 8) == rgb_of(0), "an unlisted tile id shows color 0 in the fog as well");
    check(pixel(fim, 90, 8) == rgb_of(236), "an unexplored fire wall shows the fog color (of its class, dirt: 236)");
    check(pixel(fim, 94, 8) == rgb_of(236), "an unexplored power-up shows the fog color");
    // food (animation 253, color 171) in the fog: hidden while its cell is unexplored, shown once it is explored
    world.cells[15 * 60 + 15].interactive_id = 253;
    world.cells[15 * 60 + 25].interactive_id = 253;                       // in the revealed mud band
    RecordingRenderer rfood(arc);
    hud.render(rfood, arc, world, camera);
    const auto& food = rfood.images[0];
    check(pixel(food, 30, 23) == rgb_of(244), "unexplored food shows the fog color (pixel (30,23) is cell (15,15))");
    check(pixel(food, 50, 23) == rgb_of(171), "explored food shows its color 171");
    // a bridge is an object like the rock: it keeps its color (77) in the fog
    world.cells[15 * 60 + 35].interactive_id = sim::TILE_BRIDGE2;
    RecordingRenderer rbridge(arc);
    hud.render(rbridge, arc, world, camera);
    check(pixel(rbridge.images[0], 70, 23) == rgb_of(77), "a bridge shows color 77 in the fog");
}

// The dots and the frame (FUN_01009988, FUN_01009899, 0x1009ae2 - 0x1009b6f): drawn over the image in screen coordinates, in list order (plants, then ants).
void test_minimap_dots(const assets::AssetArchive& arc) {
    std::printf("[hud] minimap dots, plants and the view frame\n");
    const auto& pal = arc.get_palette();
    auto color_of = [&](int idx) { return assets::ColorRGBA{ pal[static_cast<size_t>(idx)].r, pal[static_cast<size_t>(idx)].g, pal[static_cast<size_t>(idx)].b, 255 }; };
    auto same = [](const assets::ColorRGBA& a, const assets::ColorRGBA& b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
    auto find_fill = [&](const RecordingRenderer& rr, int x, int y, int w, int h, int palette_index) {
        for (const auto& f : rr.fills) if (f.x == x && f.y == y && f.w == w && f.h == h && same(f.color, color_of(palette_index))) return true;
        return false;
    };
    auto world_of = [](uint32_t w, uint32_t h) {
        sim::WorldState world;
        world.width = w; world.height = h;
        world.cells.assign(static_cast<size_t>(w) * h, sim::TileCell{});
        return world;
    };
    auto ant_at = [](uint32_t id, uint8_t team, int32_t px, int32_t py) {
        sim::AntSnapshot a{};
        a.id = id; a.player_id = team; a.hp = 10; a.px = px; a.py = py; a.tile_x = px / 32; a.tile_y = py / 32;
        return a;
    };
    ViewportCamera camera;

    // An ant is a rectangle at its PIXEL position, not snapped to its tile: size max(trunc(32 / scale), 2) = 2 x 2 on a 60 x 60 map, centred (left = x - 1, top = y - 1,
    // right = x + 1, bottom = y + 1). Ant (990, 500): image (61, 23) -> screen (480 + 60, 35 + 22)
    {
        sim::WorldState world = world_of(60, 60);
        world.ants.push_back(ant_at(1, 0, 990, 500));                      // image (990 * 119 / 1920, 500 * 91 / 1920) = (61, 23)
        world.ants.push_back(ant_at(2, 3, 1000, 500));                     // ten world pixels further: still the same image pixel (61, 23)
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 540, 57, 2, 2, 47), "a green ant is a 2x2 dot of color 47 at its pixel position");
        check(find_fill(rr, 540, 57, 2, 2, 239), "the black ant's dot (color 239) is drawn at the same place, over the green one");
        // the dots come after the image and before the frame
        check(rr.images.size() == 1 && rr.rects.size() == 1, "one image, one frame");
    }
    // a dying ant (no hit points left, or drowning) keeps its dot until it is removed
    {
        sim::WorldState world = world_of(60, 60);
        sim::AntSnapshot dying = ant_at(1, 1, 640, 640);
        dying.hp = 0;
        sim::AntSnapshot drowning = ant_at(2, 2, 800, 640);
        drowning.is_drowning = true;
        world.ants.push_back(dying);
        world.ants.push_back(drowning);
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        // (640, 640) -> image (39, 30); (800, 640) -> (49, 30)
        check(find_fill(rr, 480 + 38, 35 + 29, 2, 2, 158), "an ant without hit points keeps its dot (red, color 158)");
        check(find_fill(rr, 480 + 48, 35 + 29, 2, 2, 211), "a drowning ant keeps its dot (blue, color 211)");
    }
    // The size grows with the scale: on a 31 x 31 map 32 px are trunc(119 / 31) = 3 wide and trunc(91 / 31) = 2 high; the top edge is taken from half of the WIDTH
    // (y - 1, where half of the height would also give y - 1). Ant at the tile centre (15, 15) = (496, 496): image (496 * 119 / 992, 496 * 91 / 992) = (59, 45)
    {
        sim::WorldState world = world_of(31, 31);
        world.ants.push_back(ant_at(1, 0, 496, 496));
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 480 + 58, 35 + 44, 3, 2, 47), "on a 31x31 map the dot is 3x2: left x - 1, top y - 1, right x + 2, bottom y + 1");
    }
    // the quirk of the top edge: a 16 x 16 map has 32 px = trunc(119 / 16) = 7 wide and trunc(91 / 16) = 5 high; the top edge is y - 3 (half of 7), the bottom y + 3 (5 - 2)
    {
        sim::WorldState world = world_of(16, 16);
        world.ants.push_back(ant_at(1, 0, 256, 256));                      // image (256 * 119 / 512, 256 * 91 / 512) = (59, 45)
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 480 + 59 - 3, 35 + 45 - 3, 7, 6, 47), "on a 16x16 map the top edge is taken from half the width: a 7x6 rectangle (y - 3 .. y + 3)");
    }

    // Plants (Block 1 flowers and clovers, 0x100e3b0): a dot of the color and size flag of the tile's record (flower1 421: color 51, two cells) at the centre of the cell,
    // 3 x 3 on a 60 x 60 map. Plant (20, 10): centre (656, 336) -> image (40, 15) -> left 39, top 14. Drawn before the ants, in the fog as well. A plant whose tile has no
    // record, or a size flag of 0, has no dot.
    {
        sim::WorldState world = world_of(60, 60);
        world.plants.push_back(sim::MapPlant{421, 20, 10});
        world.plants.push_back(sim::MapPlant{422, 30, 30});               // no record: no dot
        world.fog_of_war_enabled = true;
        world.fog_revealed.assign(3600, 0);
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(find_fill(rr, 480 + 39, 35 + 14, 3, 3, 51), "a flower plant is a 3x3 dot of color 51 at the centre of its cell, even in the fog");
        int plant_dots = 0;
        for (const auto& f : rr.fills) if (f.w == 3 && f.h == 3 && f.x >= 480 && f.x < 599 && f.y >= 35 && f.y < 126) ++plant_dots;
        check(plant_dots == 1, "only the plant with a record has a dot");
    }

    // Fog: an own ant and an ally's always show, any other ant only on an explored cell (under the ant's pixel position)
    {
        sim::WorldState world = world_of(60, 60);
        world.fog_of_war_enabled = true;
        world.fog_revealed.assign(3600, 0);
        world.player_alliances[0] = 2;                                      // team 0 and team 2 are allies
        world.player_alliances[2] = 0;
        world.ants.push_back(ant_at(1, 0, 100, 100));                       // own, unexplored
        world.ants.push_back(ant_at(2, 2, 300, 100));                       // ally, unexplored
        world.ants.push_back(ant_at(3, 1, 500, 100));                       // enemy, unexplored
        world.ants.push_back(ant_at(4, 3, 700, 100));                       // enemy, explored (tile (21, 3))
        world.fog_revealed[3 * 60 + 21] = 1;
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        // the dots: (100, 100) -> image (6, 4); (300, 100) -> (18, 4); (500, 100) -> (30, 4); (700, 100) -> (43, 4)
        check(find_fill(rr, 480 + 5, 35 + 3, 2, 2, 47), "in fog the own ant shows");
        check(find_fill(rr, 480 + 17, 35 + 3, 2, 2, 211), "in fog an ally's ant shows");
        check(!find_fill(rr, 480 + 29, 35 + 3, 2, 2, 158), "in fog an enemy on an unexplored cell does not show");
        check(find_fill(rr, 480 + 42, 35 + 3, 2, 2, 239), "in fog an enemy on an explored cell shows");
        // seen from the ally's side, the first ant is the ally
        RecordingRenderer rr2(arc);
        HUD hud2;
        hud2.init(2);
        hud2.render(rr2, arc, world, camera);
        check(find_fill(rr2, 480 + 5, 35 + 3, 2, 2, 47), "team 2 sees its ally's ant (team 0)");
    }

    // The view frame: size (trunc(442 / scale) + 1, trunc(440 / scale) + 1) = 28 x 21 on a 60 x 60 map, at the view's origin divided by the scale, in (251, 251, 255); moved
    // inside the image when it would end beyond the right or bottom edge; it is the last thing drawn
    {
        sim::WorldState world = world_of(60, 60);
        auto frame_of = [&](float cx, float cy) {
            RecordingRenderer rr(arc);
            HUD hud;
            hud.init(0);
            ViewportCamera cam;
            cam.x = cx; cam.y = cy;
            hud.render(rr, arc, world, cam);
            struct R { int x, y, w, h; bool color; size_t count; } r{0, 0, 0, 0, false, rr.rects.size()};
            if (!rr.rects.empty()) {
                const auto& f = rr.rects[0];
                r = {f.x, f.y, f.w, f.h, f.color.r == 251 && f.color.g == 251 && f.color.b == 255, rr.rects.size()};
            }
            return r;
        };
        auto f0 = frame_of(0.0f, 0.0f);
        check(f0.count == 1 && f0.x == 480 && f0.y == 35 && f0.w == 28 && f0.h == 21 && f0.color, "frame at the origin: (480, 35) 28x21 in (251, 251, 255)");
        auto mid = frame_of(960.0f, 960.0f);                                // (960 * 119 / 1920, 960 * 91 / 1920) = (59, 45)
        check(mid.x == 480 + 59 && mid.y == 35 + 45 && mid.w == 28 && mid.h == 21, "the frame follows the view's origin, truncated");
        auto some = frame_of(1400.0f, 100.0f);                              // x: 1400 * 119 / 1920 = 86 -> right edge 480 + 86 + 28 = 594 < 599: not moved
        check(some.x == 480 + 86 && some.y == 35 + 4, "a frame that fits stays where it is");
        auto far = frame_of(1478.0f, 1480.0f);                              // x: 91 -> 480 + 91 + 28 = 599 reaches the edge: right = 599, left = 571; y: 70 -> bottom 126 -> top 105
        check(far.x == 571 && far.y == 105 && far.w == 28 && far.h == 21, "at the far corner the frame ends on the last pixel of the image (571, 105)");
        auto beyond = frame_of(1600.0f, 1600.0f);                           // (an over-scrolled view is still moved inside the image)
        check(beyond.x == 571 && beyond.y == 105 && beyond.w == 28 && beyond.h == 21, "a frame beyond the edge is moved inside the image");
    }
    // the frame of a small map: the view is larger than a 31 x 31 map's share: trunc(442 * 119 / 992) + 1 = 54 and trunc(440 * 91 / 992) + 1 = 41
    {
        sim::WorldState world = world_of(31, 31);
        RecordingRenderer rr(arc);
        HUD hud;
        hud.init(0);
        hud.render(rr, arc, world, camera);
        check(rr.rects.size() == 1 && rr.rects[0].w == 54 && rr.rects[0].h == 41, "on a 31x31 map the frame is 54x41");
    }
}

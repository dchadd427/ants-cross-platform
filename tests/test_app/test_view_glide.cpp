// Tests of the glide of an ant that the prediction has moved (include/ants_app/view_glide.hpp): a model without SDL. A rebuild of the predicted engine puts an ant where the engine has it, and the picture
// slides it there from where it was shown instead of jumping: the rules of that slide (how long, what is too big to slide, what a second shift does), and the property that matters on the screen, that an
// ant which walks and is corrected never moves by more than its walk and the slide's share in a frame.
#include "ants_app/view_glide.hpp"

#include "ants_app/health_changes.hpp"
#include "ants_app/others_view.hpp"
#include "ants_app/hud.hpp"
#include "ants_sim/command.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"

using ants::app::ViewGlide;

namespace {

// What the HUD sends, recorded and applied (a game of one machine)
class RecordingSink final : public ants::sim::CommandSink {
public:
    explicit RecordingSink(ants::sim::SimulationEngine& sim) : sim_(sim) {}
    ants::sim::CommandResult submit(const ants::sim::Command& c) override {
        log.push_back(c);
        return sim_.apply_command(c);
    }
    std::vector<ants::sim::Command> log;

private:
    ants::sim::SimulationEngine& sim_;
};

}  // namespace

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(120) << name.substr(0, 120) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "\n=======================================================\n [VIEW GLIDE] the slide of an ant that a rebuild moved\n=======================================================\n";

    TEST_CASE("VG0 The Slide Lasts As Long As A Few Ticks Of Walking And Reads As Walking, Not As A Jump Or A Lag: Between A Tenth And A Quarter Of A Second") {
        ASSERT_TRUE(ViewGlide::kDurationMs >= 100 && ViewGlide::kDurationMs <= 250);
        ASSERT_EQ(ViewGlide::kMaxShiftPx, 64);                                              // (two tiles: the most that a few ticks of a walking ant's correction can be)
    } TEST_END();

    TEST_CASE("VG1 An Ant That Was Shown Elsewhere Starts The Slide Where It Was Shown, Is Half Way After Half Of The Time, And Is Where The Engine Has It When The Time Is Over") {
        ViewGlide glide;
        int32_t dx = 0;
        int32_t dy = 0;
        ASSERT_FALSE(glide.offset_of(7, dx, dy));
        glide.add(7, 12, -8);
        ASSERT_TRUE(glide.offset_of(7, dx, dy));
        ASSERT_TRUE(dx == 12 && dy == -8);                                                  // the picture still shows it where it was
        glide.advance(ViewGlide::kDurationMs / 2);
        ASSERT_TRUE(glide.offset_of(7, dx, dy));
        ASSERT_TRUE(dx == 6 && dy == -4);
        glide.advance(ViewGlide::kDurationMs / 2 - 1);
        glide.offset_of(7, dx, dy);                                                         // (a millisecond left: less than a pixel of it)
        ASSERT_TRUE(std::abs(dx) <= 1 && std::abs(dy) <= 1);
        glide.advance(1);
        ASSERT_FALSE(glide.offset_of(7, dx, dy));
        ASSERT_TRUE(dx == 0 && dy == 0);
        ASSERT_EQ(glide.size(), 0u);
        int32_t ox = 0;
        int32_t oy = 0;
        glide.add(1, 5, 5);
        ASSERT_FALSE(glide.offset_of(2, ox, oy));                                           // (another ant: nothing)
    } TEST_END();

    TEST_CASE("VG2 A Second Shift Of An Ant That Is Still Sliding Is Added To What Is Left Of The First, The Slide Starts Again, And Two Shifts That Cancel Leave Nothing To Slide") {
        ViewGlide glide;
        int32_t dx = 0;
        int32_t dy = 0;
        glide.add(3, 20, 0);
        glide.advance(ViewGlide::kDurationMs / 2);                                           // 10 left
        glide.add(3, 6, 4);
        ASSERT_TRUE(glide.offset_of(3, dx, dy));
        ASSERT_TRUE(dx == 16 && dy == 4);
        glide.advance(ViewGlide::kDurationMs / 2);
        ASSERT_TRUE(glide.offset_of(3, dx, dy) && dx == 8 && dy == 2);                       // a whole new slide: half of it is gone after half of the time
        glide.add(3, -8, -2);
        ASSERT_FALSE(glide.offset_of(3, dx, dy));
        ASSERT_EQ(glide.size(), 0u);
    } TEST_END();

    TEST_CASE("VG3 A Shift Of More Than Two Tiles Is No Correction Of A Walk (A Throw, A Teleport Into A Hill): It Is Shown At Once, And It Ends The Slide Of The Ant That Was Sliding; No Shift At All Is Nothing") {
        ViewGlide glide;
        int32_t dx = 0;
        int32_t dy = 0;
        glide.add(5, ViewGlide::kMaxShiftPx, -ViewGlide::kMaxShiftPx);
        ASSERT_TRUE(glide.offset_of(5, dx, dy) && dx == ViewGlide::kMaxShiftPx);             // (exactly two tiles: still a slide)
        glide.add(5, 1, 0);                                                                  // (what is left plus one pixel is beyond two tiles: the ant jumps)
        ASSERT_FALSE(glide.offset_of(5, dx, dy));
        glide.add(6, ViewGlide::kMaxShiftPx + 1, 0);
        ASSERT_FALSE(glide.offset_of(6, dx, dy));
        glide.add(8, 0, 0);
        ASSERT_EQ(glide.size(), 0u);
        glide.add(9, 3, 3);
        glide.clear();
        ASSERT_FALSE(glide.offset_of(9, dx, dy));
    } TEST_END();

    TEST_CASE("VG4 On The Screen An Ant That Walks And Is Corrected Never Moves By More In A Frame Than Its Walk Plus The Slide's Share: A Tile Of Correction Is Spread Over The Frames Of A Fifth Of A Second, Not Taken In One") {
        for (const int32_t shift : {4, 12, 24, 32, 64}) {
            for (const uint32_t frame_ms : {7u, 16u, 17u, 33u}) {
                ViewGlide glide;
                // the ant walks 4 px every 50 ms (the engine's pace); the picture had it `shift` px ahead of where the engine has it now
                const double speed = 4.0 / 50.0;
                double engine = 1000.0;
                glide.add(1, shift, 0);
                int32_t dx = 0;
                int32_t dy = 0;
                glide.offset_of(1, dx, dy);
                double drawn_before = engine + dx;
                double worst = 0.0;
                uint32_t elapsed = 0;
                while (elapsed < ViewGlide::kDurationMs + 3 * frame_ms) {
                    glide.advance(frame_ms);
                    elapsed += frame_ms;
                    engine += speed * frame_ms;
                    glide.offset_of(1, dx, dy);
                    const double drawn = engine + dx;
                    worst = std::max(worst, std::abs(drawn - drawn_before - speed * frame_ms));
                    drawn_before = drawn;
                }
                const double share = static_cast<double>(shift) * frame_ms / ViewGlide::kDurationMs;      // the slide's own part of a frame
                ASSERT_TRUE(worst <= share + 1.5);                                                         // (a pixel and a half for the rounding)
                ASSERT_TRUE(drawn_before == engine);                                                       // at the end the picture is the engine's
            }
        }
    } TEST_END();

    TEST_CASE("VG5 The Slide Never Shows What The Fog Hides: An Ant Of Another Team Whose Engine Tile Is Explored But Whose Slide Would Draw It On Unexplored Ground Is Drawn Where The Engine Has It; An Own Ant, An Ally's, And Every Ant Without Fog Slide As They Are") {
        ants::sim::WorldState world;
        world.width = 10;
        world.height = 10;
        world.fog_of_war_enabled = true;
        world.fog_revealed.assign(100, 0);
        for (int32_t x = 4; x <= 6; ++x) world.fog_revealed[static_cast<size_t>(5 * 10 + x)] = 1;      // tiles 4 to 6 of row 5 are explored
        world.player_alliances.fill(255);
        ants::sim::AntSnapshot ant;
        ant.id = 9;
        ant.player_id = 1;
        ant.tile_x = 5;
        ant.tile_y = 5;
        ant.px = 5 * 32 + 16;
        ant.py = 5 * 32 + 16;
        ViewGlide glide;
        glide.add(9, -30, 0);                                                              // shown 30 px to the left of the engine: on tile 4 (explored)
        ASSERT_EQ(ants::app::glided_ant(glide, world, 0, ant).px, ant.px - 30);
        ViewGlide far;
        far.add(9, -64, 0);                                                                // 64 px to the left: tile 3, which the fog covers
        ASSERT_EQ(ants::app::glided_ant(far, world, 0, ant).px, ant.px - 48);              // the enemy is drawn with the most of the slide that stays on explored ground (three quarters: tile 4)
        ASSERT_EQ(ants::app::glided_ant(far, world, 0, ant).tile_x, 5);                    // (everything else reads the engine's tile)
        world.fog_revealed[static_cast<size_t>(5 * 10 + 4)] = 0;                           // with tile 4 covered too only a quarter of the slide stays on explored ground (tile 5)
        ASSERT_EQ(ants::app::glided_ant(far, world, 0, ant).px, ant.px - 16);
        world.fog_revealed[static_cast<size_t>(5 * 10 + 4)] = 1;
        ASSERT_EQ(ants::app::glided_ant(far, world, 1, ant).px, ant.px - 64);              // (its own team sees its own ground)
        world.player_alliances[0] = 1;
        world.player_alliances[1] = 0;
        ASSERT_EQ(ants::app::glided_ant(far, world, 0, ant).px, ant.px - 64);              // (an ally's too)
        world.player_alliances[0] = 255;
        world.player_alliances[1] = 255;
        world.fog_of_war_enabled = false;
        ASSERT_EQ(ants::app::glided_ant(far, world, 0, ant).px, ant.px - 64);              // (no fog: nothing is hidden)
        ASSERT_EQ(ants::app::glided_ant(far, world, 0, ant).id, 9u);
        world.fog_of_war_enabled = true;                                                   // the engine's own tile covered (the ant has left into the fog) and the slide's start explored: it is not drawn, and it does not slide
        world.fog_revealed[static_cast<size_t>(5 * 10 + 5)] = 0;
        ViewGlide left;
        left.add(9, -30, 0);
        ASSERT_EQ(ants::app::glided_ant(left, world, 0, ant).px, ant.px);
    } TEST_END();

    TEST_CASE("VG6 The Pointer Picks A Sliding Ant Where It Is Drawn: The Cursor Over The Drawn Enemy Ant Is The Attack Cursor And A Click There Orders An Attack On The Tile Where The ENGINE Has It; Where The Engine Has It, And The Picture Shows Nothing, A Click Is A Plain Move; The Rubber Band Takes An Own Ant Where It Is Drawn; Without A Slide Everything Is Where The Engine Has It") {
        using namespace ants;
        using namespace ants::app;
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 600000);
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);
        ViewportCamera cam;
        cam.world_x = 400;
        cam.world_y = 400;
        RecordingSink sink(sim);
        hud.set_command_sink(&sink);
        ViewGlide glide;
        hud.set_draw_offset([&glide](const sim::WorldState& world, uint8_t team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy) { return glide_offset(glide, world, team, ant, dx, dy); });
        const uint32_t mine = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{18, 18});
        const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{24, 20});
        sim.tick();
        const auto snap = [&](uint32_t id) {
            for (const auto& a : sim.get_world_state().ants) {
                if (a.id == id) return a;
            }
            return sim::AntSnapshot{};
        };
        const sim::AntSnapshot enemy = snap(foe);
        ASSERT_TRUE(enemy.tile_x == 24 && enemy.tile_y == 20);
        const auto sx = [&](int32_t wx) { return wx - cam.world_x + HUD::PLAYFIELD_X; };
        const auto sy = [&](int32_t wy) { return wy - cam.world_y + HUD::PLAYFIELD_Y; };
        const auto cursor = [&](int32_t wx, int32_t wy) { return hud.evaluate_cursor(sx(wx), sy(wy), sim.get_world_state(), sim.grid(), cam); };
        const auto click = [&](int32_t wx, int32_t wy) {
            hud.handle_mouse_down(sx(wx), sy(wy), SDL_BUTTON_LEFT, sim, cam, 0);
            hud.handle_mouse_up(sx(wx), sy(wy), SDL_BUTTON_LEFT, sim, cam, 0);
        };
        hud.select_ant(mine);
        // no slide: the enemy ant is picked where the engine has it
        ASSERT_TRUE(cursor(enemy.px, enemy.py - 8) == CursorType::Attack);
        ASSERT_TRUE(cursor(enemy.px + 48, enemy.py - 8) == CursorType::Move);
        // it slides: the picture has it 48 px to the right of the engine's place (a tile and a half: outside the box of 20 px to each side)
        glide.add(foe, 48, 0);
        ASSERT_TRUE(cursor(enemy.px + 48, enemy.py - 8) == CursorType::Attack);              // the cursor over the drawn ant
        ASSERT_TRUE(cursor(enemy.px, enemy.py - 8) == CursorType::Move);                     // ... and not where the engine has it
        click(enemy.px + 48, enemy.py - 8);
        ASSERT_EQ(sink.log.size(), 1u);
        ASSERT_TRUE(sink.log[0].type == sim::CommandType::GroupAttack);
        ASSERT_TRUE(sink.log[0].tile_x == 24 && sink.log[0].tile_y == 20);                   // the order names the tile where the engine has the ant, not the tile under the pointer
        ASSERT_EQ(sim.get_unit(mine).orig_order, sim::AntUnit::kOrderAttack);
        hud.select_ant(mine);
        click(enemy.px, enemy.py - 8);                                                       // where the engine has it the picture shows open ground: a plain move
        ASSERT_EQ(sink.log.size(), 2u);
        ASSERT_TRUE(sink.log[1].type == sim::CommandType::GroupMove);
        glide.advance(ViewGlide::kDurationMs / 2);                                           // half of the slide is over: the ant is drawn 24 px from the engine's place
        ASSERT_TRUE(cursor(enemy.px + 24, enemy.py - 8) == CursorType::Attack);
        ASSERT_TRUE(cursor(enemy.px + 48, enemy.py - 8) == CursorType::Move);
        glide.advance(ViewGlide::kDurationMs);                                               // the slide is over
        ASSERT_TRUE(cursor(enemy.px, enemy.py - 8) == CursorType::Attack);
        // the rubber band takes an own ant where it is drawn
        sim.stop_ant(mine);
        for (int i = 0; i < 40; ++i) sim.tick();
        const sim::AntSnapshot own = snap(mine);
        glide.add(mine, 0, 60);
        hud.clear_selection();
        hud.select_ants_in_rect(own.px - 10, own.py + 60 - 20, own.px + 10, own.py + 60, sim.get_world_state(), false);        // a band around the drawn ant, below every part of the engine's box
        ASSERT_TRUE(hud.is_ant_selected(mine));
        hud.clear_selection();
        hud.select_ants_in_rect(own.px - 10, own.py - 30, own.px + 10, own.py - 10, sim.get_world_state(), false);            // ... and one around the engine's place, which the picture shows empty
        ASSERT_FALSE(hud.is_ant_selected(mine));
        glide.clear();
        hud.select_ants_in_rect(own.px - 10, own.py - 30, own.px + 10, own.py - 10, sim.get_world_state(), false);
        ASSERT_TRUE(hud.is_ant_selected(mine));
    } TEST_END();

    TEST_CASE("VG7 An Ant That The Picture Does Not Draw Is Not There For The Pointer Either: An Enemy Ant Whose Engine Tile The Fog Covers Has No Slide At All, Though Its Slide Began On Explored Ground (It Left Into The Fog): Nothing Is Drawn, The Cursor Over Where The Slide Would Have It Is The Plain Move Cursor, And A Click There Is A Move, Not An Attack On The Covered Tile") {
        using namespace ants;
        using namespace ants::app;
        sim::SimulationEngine sim;
        sim.set_fog_of_war_enabled(true);
        sim.init_test_world(60, 60, 1, 600000);
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);
        ViewportCamera cam;
        cam.world_x = 400;
        cam.world_y = 400;
        RecordingSink sink(sim);
        hud.set_command_sink(&sink);
        ViewGlide glide;
        hud.set_draw_offset([&glide](const sim::WorldState& world, uint8_t team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy) { return glide_offset(glide, world, team, ant, dx, dy); });
        const uint32_t mine = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{18, 18});       // it has explored the 13 x 13 tiles around it: columns 12 to 24
        const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{26, 18});        // two tiles into the fog
        sim.tick();
        const sim::WorldState& world = sim.get_world_state();
        ASSERT_TRUE(world.is_tile_revealed(24, 18) && !world.is_tile_revealed(25, 18) && !world.is_tile_revealed(26, 18));
        sim::AntSnapshot enemy;
        for (const auto& a : world.ants) {
            if (a.id == foe) enemy = a;
        }
        ASSERT_TRUE(enemy.id == foe && enemy.tile_x == 26);
        ASSERT_FALSE(health::ant_drawn(world, 0, enemy));                                    // the picture does not draw it
        glide.add(foe, -64, 0);                                                              // it was shown two tiles to the left, on explored ground, a moment ago
        int32_t dx = 7;
        int32_t dy = 7;
        ASSERT_FALSE(glide_offset(glide, world, 0, enemy, dx, dy));                          // no slide for the viewer who cannot see it ...
        ASSERT_TRUE(dx == 0 && dy == 0);
        ASSERT_EQ(glided_ant(glide, world, 0, enemy).px, enemy.px);
        ASSERT_TRUE(glide_offset(glide, world, 1, enemy, dx, dy) && dx == -64);              // (... its own team sees it slide)
        const auto sx = [&](int32_t wx) { return wx - cam.world_x + HUD::PLAYFIELD_X; };
        const auto sy = [&](int32_t wy) { return wy - cam.world_y + HUD::PLAYFIELD_Y; };
        hud.select_ant(mine);
        const int32_t spot_x = enemy.px - 64;                                                // where the slide would have it: an explored tile, and nothing is drawn there
        const int32_t spot_y = enemy.py - 8;
        ASSERT_TRUE(hud.evaluate_cursor(sx(spot_x), sy(spot_y), world, sim.grid(), cam) == CursorType::Move);
        ASSERT_TRUE(hud.pick_ant_at(world, spot_x, spot_y) == nullptr);
        hud.handle_mouse_down(sx(spot_x), sy(spot_y), SDL_BUTTON_LEFT, sim, cam, 0);
        hud.handle_mouse_up(sx(spot_x), sy(spot_y), SDL_BUTTON_LEFT, sim, cam, 0);
        ASSERT_EQ(sink.log.size(), 1u);
        ASSERT_TRUE(sink.log[0].type == sim::CommandType::GroupMove);                        // a move to the tile under the pointer
        ASSERT_TRUE(sink.log[0].tile_x == spot_x / 32 && sink.log[0].tile_y == spot_y / 32); // ... which is not the covered tile of the ant
    } TEST_END();

    TEST_CASE("VG8 The Picture Of Others = Confirmed Is One World Of Two Engines: The Viewer's Own Ants Are The Predicted Engine's, Every Other Player's The Confirmed Engine's (An Ant That Only The Engine Of Its Owner Has Is Drawn, One That Only The Other Has Is Not), In The Engine's Order; The Ground, The Effects And The Scores Are The Predicted Engine's, What Is Explored The Confirmed Engine's") {
        using namespace ants;
        using ants::app::OthersView;
        const auto ant = [](uint32_t id, uint8_t player, int32_t px) {
            sim::AntSnapshot a;
            a.id = id;
            a.player_id = player;
            a.px = px;
            a.py = 50;
            a.tile_x = px / 32;
            a.tile_y = 1;
            return a;
        };
        sim::WorldState shown;
        sim::WorldState confirmed;
        for (sim::WorldState* w : {&shown, &confirmed}) {
            w->width = 4;
            w->height = 4;
            w->cells.assign(16, sim::TileCell{});
            w->player_alliances.fill(255);
        }
        shown.match_time_remaining_ms = 1000;
        confirmed.match_time_remaining_ms = 2000;
        shown.player_scores[0] = 7;
        confirmed.player_scores[0] = 3;
        shown.cells[5].interactive_id = 9;                                                   // a tile that only the predicted engine has changed (food taken, a bridge built)
        sim::VisualEffect boom;
        boom.anim_name = "bombex";
        shown.effects.push_back(boom);
        shown.fog_of_war_enabled = false;                                                    // the predicted engine's own fog is not what is drawn: the confirmed engine's is
        confirmed.fog_of_war_enabled = true;
        confirmed.fog_revealed.assign(16, 1);
        confirmed.fog_revealed[5] = 0;
        shown.ants = {ant(1, 0, 100), ant(2, 1, 200), ant(4, 1, 400), ant(6, 0, 600), ant(7, 2, 700)};
        confirmed.ants = {ant(1, 0, 90), ant(2, 1, 190), ant(3, 0, 300), ant(5, 1, 500), ant(7, 2, 690)};
        OthersView view;
        const sim::WorldState& world = view.compose(shown, confirmed, 0, OthersView::Stamp{});
        std::vector<uint32_t> ids;
        for (const auto& a : world.ants) ids.push_back(a.id);
        ASSERT_TRUE((ids == std::vector<uint32_t>{1, 2, 5, 6, 7}));                          // own 1 and 6 (6: only the predicted engine has it), others 2, 5 (only the confirmed engine has it) and 7
        ASSERT_EQ(world.ants[0].px, 100);                                                    // the own ant: the predicted engine's place
        ASSERT_EQ(world.ants[1].px, 190);                                                    // another player's: the confirmed engine's, never the predicted one's
        ASSERT_EQ(world.ants[2].px, 500);
        ASSERT_EQ(world.ants[3].px, 600);
        ASSERT_EQ(world.ants[4].px, 690);                                                    // (an ally is another player)
        ASSERT_EQ(world.match_time_remaining_ms, 1000u);                                     // everything that is not an ant is the predicted engine's
        ASSERT_EQ(world.player_scores[0], 7);
        ASSERT_EQ(world.cells[5].interactive_id, 9);
        ASSERT_EQ(world.effects.size(), 1u);
        ASSERT_TRUE(world.fog_of_war_enabled && world.fog_revealed == confirmed.fog_revealed);        // ... except what is explored
        ASSERT_FALSE(world.is_tile_revealed(1, 1));
        // a world that the same stamp made stands: the frame, the pointer and the keys of a moment share it; the engines' every change is a new stamp
        ASSERT_EQ(view.made(), 1u);
        const OthersView::Stamp same{};
        view.compose(shown, confirmed, 0, same);
        ASSERT_EQ(view.made(), 1u);
        for (int field = 0; field < 5; ++field) {
            OthersView::Stamp other;
            (field == 0 ? other.shown_tick : field == 1 ? other.confirmed_tick : field == 2 ? other.rebuilds : field == 3 ? other.starts : other.orders) = 1;
            const uint64_t before = view.made();
            view.compose(shown, confirmed, 0, other);
            ASSERT_EQ(view.made(), before + 1);
            view.compose(shown, confirmed, 0, other);
            ASSERT_EQ(view.made(), before + 1);
            view.compose(shown, confirmed, 0, OthersView::Stamp{});                         // (back to the first stamp: made again, the world that stood was another's)
            ASSERT_EQ(view.made(), before + 2);
        }
        const uint64_t made = view.made();
        const sim::WorldState& theirs = view.compose(shown, confirmed, 1, OthersView::Stamp{});      // the same engines for another viewer: another picture
        ASSERT_EQ(view.made(), made + 1);
        ASSERT_EQ(theirs.ants[0].px, 90);                                                    // (seat 1's own ants are the predicted engine's: here seat 0's ant 1 is another player's)
        view.forget();
        view.compose(shown, confirmed, 1, OthersView::Stamp{});
        ASSERT_EQ(view.made(), made + 2);
        // the rule over many pairs of engines: the ids that are drawn are exactly the own ants of the predicted engine and the other players' ants of the confirmed one, once each, in order, and each is
        // the very ant of that engine
        std::mt19937 rng(77);
        for (int round = 0; round < 300; ++round) {
            sim::WorldState a;
            sim::WorldState b;
            const uint8_t own = static_cast<uint8_t>(rng() % 4u);
            for (uint32_t id = 1; id <= 24; ++id) {
                const uint8_t player = static_cast<uint8_t>(rng() % 4u);
                if (rng() % 3u != 0u) a.ants.push_back(ant(id, player, static_cast<int32_t>(rng() % 900u)));
                if (rng() % 3u != 0u) b.ants.push_back(ant(id, player, static_cast<int32_t>(rng() % 900u)));
            }
            OthersView random_view;
            const sim::WorldState& w = random_view.compose_now(a, b, own);
            std::vector<uint32_t> expected;
            for (uint32_t id = 1; id <= 24; ++id) {
                const sim::AntSnapshot* mine = nullptr;
                const sim::AntSnapshot* theirs_ant = nullptr;
                for (const auto& x : a.ants) if (x.id == id) mine = &x;
                for (const auto& x : b.ants) if (x.id == id) theirs_ant = &x;
                const sim::AntSnapshot* from = mine != nullptr && mine->player_id == own ? mine : (theirs_ant != nullptr && theirs_ant->player_id != own ? theirs_ant : nullptr);
                if (mine != nullptr && theirs_ant != nullptr && mine->player_id != theirs_ant->player_id) from = nullptr;     // (an id with two owners cannot be: the engines agree on who an ant belongs to)
                if (mine != nullptr && theirs_ant != nullptr && mine->player_id != theirs_ant->player_id) continue;
                if (from == nullptr) continue;
                expected.push_back(id);
                const sim::AntSnapshot* drawn = nullptr;
                for (const auto& x : w.ants) if (x.id == id) drawn = &x;
                ASSERT_TRUE(drawn != nullptr && drawn->px == from->px);
            }
            ASSERT_EQ(w.ants.size(), expected.size());
        }
    } TEST_END();

    TEST_CASE("VG9 An Attack On An Ant That Is Drawn From The Confirmed Engine Aims At The Tile Where The Predicted Engine Has It, As Far As The Picture Could Show The Ant There: Not Into Ground That The Fog Covers (The Player Has Not Seen The Ant Go There), Not When The Engine Has Lost The Ant; An Ally's Ant Is Never Hidden") {
        using namespace ants;
        sim::WorldState predicted;
        predicted.width = 20;
        predicted.height = 20;
        predicted.player_alliances.fill(255);
        sim::AntSnapshot there;
        there.id = 9;
        there.player_id = 1;
        there.tile_x = 12;
        there.tile_y = 5;
        predicted.ants.push_back(there);
        sim::AntSnapshot drawn = there;                                                      // the confirmed engine has it a tile and a half behind
        drawn.tile_x = 11;
        sim::TileCoord tile{11, 5};
        ASSERT_TRUE(ants::app::aim_at_predicted_ant(predicted, 0, drawn, tile));
        ASSERT_TRUE(tile.x == 12 && tile.y == 5);
        predicted.fog_of_war_enabled = true;                                                 // the fog covers the tile that the predicted engine has it on
        predicted.fog_revealed.assign(400, 1);
        predicted.fog_revealed[static_cast<size_t>(5 * 20 + 12)] = 0;
        tile = sim::TileCoord{11, 5};
        ASSERT_FALSE(ants::app::aim_at_predicted_ant(predicted, 0, drawn, tile));
        ASSERT_TRUE(tile.x == 11 && tile.y == 5);                                            // (the tile is left as it was: where the ant is drawn)
        predicted.player_alliances[0] = 1;
        predicted.player_alliances[1] = 0;
        ASSERT_TRUE(ants::app::aim_at_predicted_ant(predicted, 0, drawn, tile) && tile.x == 12);      // an ally's ant is drawn wherever it is
        predicted.player_alliances.fill(255);
        predicted.fog_revealed[static_cast<size_t>(5 * 20 + 12)] = 1;
        ASSERT_TRUE(ants::app::aim_at_predicted_ant(predicted, 0, drawn, tile) && tile.x == 12);      // (the ground is explored now)
        ASSERT_TRUE(ants::app::aim_at_predicted_ant(predicted, 1, drawn, tile));             // ... and for the viewer who owns the ant the fog never matters
        drawn.id = 10;                                                                       // an ant that the predicted engine does not have (it died in the ticks ahead)
        tile = sim::TileCoord{11, 5};
        ASSERT_FALSE(ants::app::aim_at_predicted_ant(predicted, 0, drawn, tile));
        ASSERT_TRUE(tile.x == 11);
    } TEST_END();

    TEST_CASE("VG10 The Pointer Picks The Other Player's Ant Where It Is Drawn, From The Confirmed Engine (The Cursor, The Click, The Rubber Band), And An Attack On It Names The Tile Where The Predicted Engine Has It; Without The Aim It Names The Tile Where It Is Drawn, Which A Walking Ant Has Left By The Time The Order Lands; The Order Itself Is Run In The Engine That It Meets") {
        using namespace ants;
        using namespace ants::app;
        sim::SimulationEngine confirmed;
        confirmed.init_test_world(60, 60, 1, 600000);
        const uint32_t mine = confirmed.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{18, 18});
        const uint32_t foe = confirmed.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{34, 20});
        confirmed.tick();
        sim::SimulationEngine predicted(confirmed);
        sim::Command walk;                                                                   // the other player's ant walks west (an order that both engines know)
        walk.type = sim::CommandType::GroupMove;
        walk.issuer = 1;
        walk.tile_x = 10;
        walk.tile_y = 20;
        walk.ants.assign(1u, foe);
        confirmed.apply_command(walk);
        predicted.apply_command(walk);
        for (int i = 0; i < 24; ++i) {
            confirmed.tick();
            predicted.tick();
        }
        for (int i = 0; i < 12; ++i) predicted.tick();                                       // the predicted engine is 12 ticks ahead: the ant has walked about 48 px further
        const auto snap = [](const sim::SimulationEngine& e, uint32_t id) {
            for (const auto& a : e.get_world_state().ants) {
                if (a.id == id) return a;
            }
            return sim::AntSnapshot{};
        };
        const sim::AntSnapshot drawn = snap(confirmed, foe);
        const sim::AntSnapshot ahead = snap(predicted, foe);
        ASSERT_TRUE(drawn.px - ahead.px >= 40 && drawn.tile_x != ahead.tile_x);               // (it walks west: the predicted engine has it further left, on another tile)
        OthersView view;
        const sim::WorldState& world = view.compose(predicted.get_world_state(), confirmed.get_world_state(), 0, OthersView::Stamp{});
        for (const bool aimed : {true, false}) {
            HUD hud;
            hud.init(0);
            hud.set_sim_query(&predicted);
            hud.set_shown_world(&world);
            ViewportCamera cam;
            cam.world_x = std::max(0, drawn.px - 220);
            cam.world_y = std::max(0, drawn.py - 200);
            RecordingSink sink(predicted);
            hud.set_command_sink(&sink);
            if (aimed) hud.set_aim_tile([&](const sim::AntSnapshot& picked, sim::TileCoord& tile) { return aim_at_predicted_ant(predicted.get_world_state(), 0, picked, tile); });
            const auto sx = [&](int32_t wx) { return wx - cam.world_x + HUD::PLAYFIELD_X; };
            const auto sy = [&](int32_t wy) { return wy - cam.world_y + HUD::PLAYFIELD_Y; };
            hud.select_ant(mine);
            ASSERT_TRUE(hud.evaluate_cursor(sx(drawn.px), sy(drawn.py - 8), world, predicted.grid(), cam) == CursorType::Attack);       // the cursor over the ant as drawn
            ASSERT_TRUE(hud.evaluate_cursor(sx(ahead.px), sy(ahead.py - 8), world, predicted.grid(), cam) == CursorType::Move);         // ... and nothing where the predicted engine has it
            ASSERT_TRUE(hud.pick_ant_at(world, drawn.px, drawn.py - 8) != nullptr && hud.pick_ant_at(world, drawn.px, drawn.py - 8)->px == drawn.px);
            hud.handle_mouse_down(sx(drawn.px), sy(drawn.py - 8), SDL_BUTTON_RIGHT, predicted, cam, 0);
            hud.handle_mouse_up(sx(drawn.px), sy(drawn.py - 8), SDL_BUTTON_RIGHT, predicted, cam, 0);
            ASSERT_EQ(sink.log.size(), 1u);
            ASSERT_TRUE(sink.log[0].type == sim::CommandType::GroupAttack);
            const sim::AntSnapshot want = aimed ? ahead : drawn;
            ASSERT_TRUE(sink.log[0].tile_x == want.tile_x && sink.log[0].tile_y == want.tile_y);       // the tile of the predicted engine's ant, or of the one that is drawn
            ASSERT_EQ(predicted.get_unit(mine).orig_order, aimed ? sim::AntUnit::kOrderAttack : sim::AntUnit::kOrderMove);       // the order ran in the predicted engine at once: an attack on the ant where it is, a plain move to the tile that it has left
            predicted.stop_ant(mine);
            hud.select_ant(mine);
            hud.handle_mouse_down(sx(drawn.px), sy(drawn.py - 8), SDL_BUTTON_LEFT, predicted, cam, 0);       // the left click over the attack cursor is the same order
            hud.handle_mouse_up(sx(drawn.px), sy(drawn.py - 8), SDL_BUTTON_LEFT, predicted, cam, 0);
            ASSERT_EQ(sink.log.size(), 2u);
            ASSERT_TRUE(sink.log[1].type == sim::CommandType::GroupAttack && sink.log[1].tile_x == want.tile_x && sink.log[1].tile_y == want.tile_y);
            hud.clear_selection();
            hud.handle_mouse_down(sx(drawn.px), sy(drawn.py - 8), SDL_BUTTON_LEFT, predicted, cam, 0);       // with nothing selected it is the inspect panel of the ant as drawn
            hud.handle_mouse_up(sx(drawn.px), sy(drawn.py - 8), SDL_BUTTON_LEFT, predicted, cam, 0);
            ASSERT_EQ(sink.log.size(), 2u);                                                      // (no order)
            ASSERT_EQ(hud.get_selected_ant_id(), foe);
        }
        // the world is the picture's only for the HUD that was told so: another HUD, or this one when it is told no more, asks the engine
        HUD plain;
        plain.init(0);
        ASSERT_TRUE(plain.pick_ant_at(predicted.get_world_state(), ahead.px, ahead.py - 8) != nullptr);
        ASSERT_TRUE(plain.pick_ant_at(world, ahead.px, ahead.py - 8) == nullptr);              // (the composed world has the ant where the confirmed engine has it)
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " VIEW GLIDE TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

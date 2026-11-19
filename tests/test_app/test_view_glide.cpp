// Tests of the glide of an ant that the prediction has moved (include/ants_app/view_glide.hpp): a model without SDL. A rebuild of the predicted engine puts an ant where the engine has it, and the picture
// slides it there from where it was shown instead of jumping: the rules of that slide (how long: the pace of a walk for another player's ant, the quick slide for the player's own; what is too big to
// slide, what a second shift does), the property that matters on the screen (an ant which walks and is corrected never moves by more than its walk and the slide's share in a frame, and no correction of
// another player's ant is faster than a walk), and how a sliding ant is drawn (walking the way it moves).
#include "ants_app/view_glide.hpp"

#include "ants_app/health_changes.hpp"
#include "ants_app/hud.hpp"
#include "ants_sim/command.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
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

    TEST_CASE("VG0 The Quick Slide (The Player's Own Ants, And The Shortest Of Any) Lasts Between A Tenth And A Quarter Of A Second, Another Player's Ant Is Carried At The Pace Of A Walk (4 px A Tick), And No Slide Lasts More Than 0.6 s") {
        ASSERT_TRUE(ViewGlide::kDurationMs >= 100 && ViewGlide::kDurationMs <= 250);
        ASSERT_EQ(ViewGlide::kWalkPxPerSecond, 80);                                         // (4 px every 50 ms: the engine's pace on open ground)
        ASSERT_EQ(ViewGlide::kMaxDurationMs, 600u);
        ASSERT_EQ(ViewGlide::kMaxShiftPx, 64);                                              // (two tiles: the most that a few ticks of a walking ant's correction can be)
    } TEST_END();

    TEST_CASE("VG1 An Ant That Was Shown Elsewhere Starts The Slide Where It Was Shown, Is Half Way After Half Of The Time, And Is Where The Engine Has It When The Time Is Over (The Player's Own Ant In The Quick Slide, Another Player's Ant In The Time Of Its Distance)") {
        for (const bool own : {true, false}) {
            ViewGlide glide;
            int32_t dx = 0;
            int32_t dy = 0;
            ASSERT_FALSE(glide.offset_of(7, dx, dy));
            const uint32_t total = ViewGlide::duration_ms(12, -8, own);
            ASSERT_EQ(total, own ? ViewGlide::kDurationMs : 181u);                           // (14.4 px at 80 px/s: 181 ms, a little more than the quick slide)
            glide.add(7, 12, -8, own);
            ASSERT_TRUE(glide.offset_of(7, dx, dy));
            ASSERT_TRUE(dx == 12 && dy == -8);                                              // the picture still shows it where it was
            glide.advance(total / 2);
            ASSERT_TRUE(glide.offset_of(7, dx, dy));
            ASSERT_TRUE(std::abs(dx - 6) <= 1 && std::abs(dy + 4) <= 1);
            glide.advance(total - total / 2 - 1);
            glide.offset_of(7, dx, dy);                                                     // (a millisecond left: less than a pixel of it)
            ASSERT_TRUE(std::abs(dx) <= 1 && std::abs(dy) <= 1);
            glide.advance(1);
            ASSERT_FALSE(glide.offset_of(7, dx, dy));
            ASSERT_TRUE(dx == 0 && dy == 0);
            ASSERT_EQ(glide.size(), 0u);
            int32_t ox = 0;
            int32_t oy = 0;
            glide.add(1, 5, 5, own);
            ASSERT_FALSE(glide.offset_of(2, ox, oy));                                       // (another ant: nothing)
        }
    } TEST_END();

    TEST_CASE("VG2 A Second Shift Of An Ant That Is Still Sliding Is Added To What Is Left Of The First, The Slide Starts Again In The Time Of The New Sum, And Two Shifts That Cancel Leave Nothing To Slide") {
        ViewGlide glide;
        int32_t dx = 0;
        int32_t dy = 0;
        glide.add(3, 20, 0, false);                                                                // 250 ms at the pace of a walk
        glide.advance(125);                                                                 // 10 left
        glide.add(3, 6, 4, false);
        ASSERT_TRUE(glide.offset_of(3, dx, dy));
        ASSERT_TRUE(dx == 16 && dy == 4);
        const uint32_t again = ViewGlide::duration_ms(16, 4, false);                       // (16.5 px: 207 ms)
        ASSERT_EQ(again, 207u);
        glide.advance(again / 2);
        ASSERT_TRUE(glide.offset_of(3, dx, dy) && std::abs(dx - 8) <= 1 && std::abs(dy - 2) <= 1);      // a whole new slide: half of it is gone after half of the time
        glide.add(3, -dx, -dy, false);
        ASSERT_FALSE(glide.offset_of(3, dx, dy));
        ASSERT_EQ(glide.size(), 0u);
        ViewGlide mine;                                                                     // an own ant stays in the quick slide however its slide is added to
        mine.add(4, 30, 0, true);
        mine.advance(50);
        mine.add(4, 10, 0, true);
        mine.advance(ViewGlide::kDurationMs / 2);
        ASSERT_TRUE(mine.offset_of(4, dx, dy));
        mine.advance(ViewGlide::kDurationMs / 2);
        ASSERT_FALSE(mine.offset_of(4, dx, dy));
    } TEST_END();

    TEST_CASE("VG3 A Shift Of More Than Two Tiles Is No Correction Of A Walk (A Throw, A Teleport Into A Hill): It Is Shown At Once, And It Ends The Slide Of The Ant That Was Sliding; No Shift At All Is Nothing") {
        ViewGlide glide;
        int32_t dx = 0;
        int32_t dy = 0;
        glide.add(5, ViewGlide::kMaxShiftPx, -ViewGlide::kMaxShiftPx, false);
        ASSERT_TRUE(glide.offset_of(5, dx, dy) && dx == ViewGlide::kMaxShiftPx);             // (exactly two tiles: still a slide)
        glide.add(5, 1, 0, false);                                                                  // (what is left plus one pixel is beyond two tiles: the ant jumps)
        ASSERT_FALSE(glide.offset_of(5, dx, dy));
        glide.add(6, ViewGlide::kMaxShiftPx + 1, 0, false);
        ASSERT_FALSE(glide.offset_of(6, dx, dy));
        glide.add(8, 0, 0, false);
        ASSERT_EQ(glide.size(), 0u);
        glide.add(9, 3, 3, false);
        glide.clear();
        ASSERT_FALSE(glide.offset_of(9, dx, dy));
    } TEST_END();

    TEST_CASE("VG4 On The Screen An Ant That Walks And Is Corrected Never Moves By More In A Frame Than Its Walk Plus The Slide's Share: A Tile Of Correction Is Spread Over The Frames Of Its Slide, Not Taken In One (The Quick Slide Of An Own Ant, The Pace Of A Walk For Another Player's)") {
        for (const bool own : {true, false}) {
            for (const int32_t shift : {4, 12, 24, 32, 64}) {
                for (const uint32_t frame_ms : {7u, 16u, 17u, 33u}) {
                    ViewGlide glide;
                    // the ant walks 4 px every 50 ms (the engine's pace); the picture had it `shift` px ahead of where the engine has it now
                    const double speed = 4.0 / 50.0;
                    double engine = 1000.0;
                    glide.add(1, shift, 0, own);
                    const uint32_t total = ViewGlide::duration_ms(shift, 0, own);
                    int32_t dx = 0;
                    int32_t dy = 0;
                    glide.offset_of(1, dx, dy);
                    double drawn_before = engine + dx;
                    double worst = 0.0;
                    uint32_t elapsed = 0;
                    while (elapsed < total + 3 * frame_ms) {
                        glide.advance(frame_ms);
                        elapsed += frame_ms;
                        engine += speed * frame_ms;
                        glide.offset_of(1, dx, dy);
                        const double drawn = engine + dx;
                        worst = std::max(worst, std::abs(drawn - drawn_before - speed * frame_ms));
                        drawn_before = drawn;
                    }
                    const double share = static_cast<double>(shift) * frame_ms / total;                  // the slide's own part of a frame
                    ASSERT_TRUE(worst <= share + 1.5);                                                    // (a pixel and a half for the rounding)
                    ASSERT_TRUE(drawn_before == engine);                                                  // at the end the picture is the engine's
                }
            }
        }
    } TEST_END();

    TEST_CASE("VG4b No Correction Of Another Player's Ant Moves It Faster Than It Walks, Up To The Ceiling: Along The Line, In Any Direction, At Any Frame Rate, The Slide Carries An Ant At 80 px/s At Most (A Few Pixels A Frame More For The Rounding) Until A Slide Would Take Longer Than 0.6 s; A Short Correction Takes The Quick Slide And Never Less; A Longer One Takes 0.6 s And Is Carried A Little Faster; The Player's Own Ants Are Not Slowed By It") {
        const int32_t shifts[][2] = {{4, 0}, {0, -8}, {8, 6}, {12, 12}, {24, 0}, {-24, 24}, {32, 0}, {0, 34}, {-30, -30}, {40, 0}, {48, 0}, {0, -48}, {34, 34}, {64, 0}, {64, -64}, {50, 20}};
        for (const auto& s : shifts) {
            const double length = std::sqrt(static_cast<double>(s[0]) * s[0] + static_cast<double>(s[1]) * s[1]);
            const uint32_t total = ViewGlide::duration_ms(s[0], s[1], false);
            ASSERT_TRUE(total >= ViewGlide::kDurationMs && total <= ViewGlide::kMaxDurationMs);
            if (length <= 12.0) ASSERT_EQ(total, ViewGlide::kDurationMs);                                // a short correction is as quick as ever
            if (length <= 48.0) ASSERT_TRUE(length * 1000.0 / total <= ViewGlide::kWalkPxPerSecond + 0.01);   // at the pace of a walk or slower ...
            if (length > 48.0) ASSERT_EQ(total, ViewGlide::kMaxDurationMs);                              // ... and past the ceiling the time stops growing
            if (length > 12.0 && length <= 48.0) ASSERT_TRUE(length * 1000.0 / total >= ViewGlide::kWalkPxPerSecond - 1.0);   // (and not slower than a walk either: the ant is carried, it does not drift)
            for (const uint32_t frame_ms : {1u, 7u, 16u, 17u, 33u}) {
                ViewGlide glide;
                glide.add(5, s[0], s[1], false);
                int32_t px = 0;
                int32_t py = 0;
                glide.offset_of(5, px, py);
                double worst = 0.0;
                for (uint32_t elapsed = 0; elapsed < total + 2 * frame_ms; elapsed += frame_ms) {
                    glide.advance(frame_ms);
                    int32_t nx = 0;
                    int32_t ny = 0;
                    glide.offset_of(5, nx, ny);
                    worst = std::max(worst, std::hypot(static_cast<double>(nx - px), static_cast<double>(ny - py)));
                    px = nx;
                    py = ny;
                }
                const double pace = std::max(length * 1000.0 / total, static_cast<double>(ViewGlide::kWalkPxPerSecond));
                ASSERT_TRUE(worst <= pace * frame_ms / 1000.0 + 1.5);                                      // (the rounding of both axes: a pixel and a half)
                ASSERT_TRUE(px == 0 && py == 0);
            }
            ASSERT_EQ(ViewGlide::duration_ms(s[0], s[1], true), ViewGlide::kDurationMs);                  // the player's own ants: the quick slide, whatever the distance
        }
    } TEST_END();

    TEST_CASE("VG4c The Pull Of A Slide Is Constant (Pixels A Second Towards The Engine's Place, Against The Offset), What It Has Carried Is The Distance Covered, And An Ant That Does Not Slide Has Neither") {
        ViewGlide glide;
        int32_t vx = 7;
        int32_t vy = 7;
        ASSERT_FALSE(glide.velocity_of(1, vx, vy));
        ASSERT_TRUE(vx == 0 && vy == 0);
        ASSERT_EQ(glide.carried_px(1), 0);
        ASSERT_FALSE(glide.is_own(1));
        glide.add(1, 24, 0, false);                                                                // 300 ms at 80 px/s
        glide.add(2, 0, -16, true);
        ASSERT_TRUE(glide.velocity_of(1, vx, vy));
        ASSERT_TRUE(vx == -80 && vy == 0);
        ASSERT_TRUE(glide.velocity_of(2, vx, vy));
        ASSERT_TRUE(vx == 0 && vy == 107);                                                  // 16 px in the quick slide: a little over a walk
        ASSERT_FALSE(glide.is_own(1));
        ASSERT_TRUE(glide.is_own(2));
        glide.advance(150);
        ASSERT_TRUE(glide.velocity_of(1, vx, vy) && vx == -80);                             // (the same pull half way)
        ASSERT_EQ(glide.carried_px(1), 12);
        glide.advance(150);
        ASSERT_FALSE(glide.velocity_of(1, vx, vy));
        ASSERT_EQ(glide.carried_px(1), 0);
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
        glide.add(9, -30, 0, false);                                                              // shown 30 px to the left of the engine: on tile 4 (explored)
        ASSERT_EQ(ants::app::glided_ant(glide, world, 0, ant).px, ant.px - 30);
        ViewGlide far;
        far.add(9, -64, 0, false);                                                                // 64 px to the left: tile 3, which the fog covers
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
        left.add(9, -30, 0, false);
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
        glide.add(foe, 48, 0, false);
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
        glide.advance(ViewGlide::duration_ms(48, 0, false) / 2);                             // half of the slide is over: the ant is drawn 24 px from the engine's place
        ASSERT_TRUE(cursor(enemy.px + 24, enemy.py - 8) == CursorType::Attack);
        ASSERT_TRUE(cursor(enemy.px + 48, enemy.py - 8) == CursorType::Move);
        glide.advance(ViewGlide::kMaxDurationMs);                                            // the slide is over
        ASSERT_TRUE(cursor(enemy.px, enemy.py - 8) == CursorType::Attack);
        // the rubber band takes an own ant where it is drawn
        sim.stop_ant(mine);
        for (int i = 0; i < 40; ++i) sim.tick();
        const sim::AntSnapshot own = snap(mine);
        glide.add(mine, 0, 60, false);
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
        glide.add(foe, -64, 0, false);                                                              // it was shown two tiles to the left, on explored ground, a moment ago
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

    TEST_CASE("VG8 A Sliding Ant Of Another Player Is Drawn Walking The Way It Moves: Standing While The Engine Walks It The Way The Slide Pulls It Back (Not A Walk Cycle On The Spot, Not Legs That Run Forward On An Ant That Is Carried Backward), Walking In The Heading Of Its Drawn Motion While The Engine Stands It, And As The Engine Has It When It Does Not Slide, Is The Player's Own, Does Something Else Than Walk Or Stand, Or Was Cut Short By The Fog") {
        using namespace ants;
        using namespace ants::app;
        namespace mv = ants::sim::movement;
        const mv::MotionClip walk_east = mv::walk_clip(0, mv::kTerrainGrass, 2, false);
        const mv::MotionClip idle_east = mv::idle_clip(0, 2, false);
        ASSERT_TRUE(walk_east.valid() && idle_east.valid());
        const auto walking_ant = [&]() {                                                     // an ant that walks east: the engine's walk clip, a frame in
            sim::AntSnapshot a;
            a.id = 5;
            a.player_id = 1;
            a.type = sim::AntType::Worker;
            a.state = sim::UnitState::Walking;
            a.action = 1;
            a.facing = 2;
            a.loco_clip = walk_east.chd_index;
            a.loco_mirrored = walk_east.mirrored;
            a.loco_frame = 3;
            a.loco_left_ms = 20;
            a.px = 300;
            a.py = 300;
            return a;
        };
        const auto standing_ant = [&]() {                                                    // an ant that is ordered and stands for the engine's three ticks of start-up
            sim::AntSnapshot a = walking_ant();
            a.action = 0;
            a.loco_clip = idle_east.chd_index;
            a.loco_mirrored = idle_east.mirrored;
            a.loco_frame = 0;
            return a;
        };
        const auto posed = [&](const ViewGlide& glide, const sim::AntSnapshot& engine) {
            sim::WorldState world;
            world.width = 40;
            world.height = 40;
            world.player_alliances.fill(255);
            return slid_pose(glide, engine, glided_ant(glide, world, 0, engine));
        };
        const auto with_slide = [&](const sim::AntSnapshot& engine, int32_t dx, int32_t dy, bool own) {
            ViewGlide glide;
            glide.add(engine.id, dx, dy, own);
            return posed(glide, engine);
        };
        // no slide: the engine's own picture of the ant
        {
            ViewGlide none;
            const sim::AntSnapshot a = walking_ant();
            const sim::AntSnapshot drawn = posed(none, a);
            ASSERT_TRUE(drawn.loco_clip == a.loco_clip && drawn.loco_frame == a.loco_frame && drawn.loco_left_ms == a.loco_left_ms && drawn.facing == a.facing);
        }
        // the engine walks it east at 80 px/s and the slide pulls it west at 80 px/s (it was drawn 24 px east of the engine's place): it goes nowhere, so it is drawn STANDING, facing east
        {
            const sim::AntSnapshot a = with_slide(walking_ant(), 24, 0, false);
            ASSERT_TRUE(a.loco_clip == idle_east.chd_index);                                // the idle clip of its heading, not the walk clip that runs on the spot
            ASSERT_TRUE(a.loco_clip != walk_east.chd_index);
            ASSERT_TRUE(a.loco_frame == 0);
            ASSERT_EQ(a.px, 324);                                                           // (where it is drawn is not touched)
        }
        // the same slide of an own ant is not touched: the quick slide, the engine's walk cycle as it was
        {
            const sim::AntSnapshot a = with_slide(walking_ant(), 24, 0, true);
            ASSERT_TRUE(a.loco_clip == walk_east.chd_index && a.loco_mirrored == walk_east.mirrored && a.facing == 2 && a.loco_frame == 3 && a.loco_left_ms == 20);       // (west is the mirror of the same clip: the facing and the mirror say it is east still)
            const sim::AntSnapshot b = with_slide(standing_ant(), 24, 24, true);
            ASSERT_TRUE(b.loco_clip == idle_east.chd_index && b.facing == 2 && b.loco_frame == 0);                                                                    // (an own ant that stands is not walked north-west)
        }
        // the engine stands it and the slide pulls it north-west (it was drawn south-east of the engine's place) at 80 px/s: it is drawn WALKING north-west
        {
            const mv::MotionClip walk_nw = mv::walk_clip(0, mv::kTerrainGrass, 7, false);
            ASSERT_TRUE(walk_nw.valid());
            const sim::AntSnapshot a = with_slide(standing_ant(), 24, 24, false);
            ASSERT_TRUE(a.loco_clip == walk_nw.chd_index && a.loco_mirrored == walk_nw.mirrored);
            ASSERT_TRUE(a.facing == 7);
            ASSERT_TRUE(a.loco_frame < walk_nw.count);
            ASSERT_TRUE(a.loco_left_ms == 0xFFFFu);                                             // (no step of the clip moves the drawn ant besides the slide)
            const mv::MotionClip walk_s = mv::walk_clip(0, mv::kTerrainGrass, 4, false);   // pulled south (drawn north of the engine's place)
            const sim::AntSnapshot b = with_slide(standing_ant(), 0, -24, false);
            ASSERT_TRUE(b.loco_clip == walk_s.chd_index && b.facing == 4);
            const mv::MotionClip walk_w = mv::walk_clip(0, mv::kTerrainGrass, 6, false);   // west: the mirror of east
            const sim::AntSnapshot c = with_slide(standing_ant(), 24, 0, false);
            ASSERT_TRUE(c.loco_clip == walk_w.chd_index && c.loco_mirrored == walk_w.mirrored && c.facing == 6);
        }
        // the engine walks it east and the slide carries it east too: it goes on walking east, in the engine's walk cycle
        {
            const sim::AntSnapshot a = with_slide(walking_ant(), -24, 0, false);
            ASSERT_TRUE(a.loco_clip == walk_east.chd_index && a.loco_frame == 3 && a.facing == 2);
        }
        // the engine walks it east and the slide pulls it south at 80 px/s: it walks south-east (the sum of the two), in its own walk cycle
        {
            const mv::MotionClip walk_se = mv::walk_clip(0, mv::kTerrainGrass, 3, false);
            const sim::AntSnapshot a = with_slide(walking_ant(), 0, -24, false);
            ASSERT_TRUE(a.loco_clip == walk_se.chd_index && a.facing == 3 && a.loco_frame == 3 % walk_se.count);
        }
        // not an ant that walks or stands: a blow, a flinch, a throw ... is the engine's picture
        {
            sim::AntSnapshot a = standing_ant();
            a.state = sim::UnitState::Attacking;
            const sim::AntSnapshot drawn = with_slide(a, 24, 24, false);
            ASSERT_TRUE(drawn.loco_clip == a.loco_clip && drawn.facing == a.facing);
            a.state = sim::UnitState::Flinch;
            ASSERT_TRUE(with_slide(a, 24, 24, false).loco_clip == a.loco_clip);
            const mv::MotionClip walk_nw = mv::walk_clip(0, mv::kTerrainGrass, 7, false);
            for (const sim::UnitState idle : {sim::UnitState::Idle, sim::UnitState::GuardIdle}) {                                   // (an ant that stands is carried: it walks the way the slide carries it)
                a.state = idle;
                const sim::AntSnapshot carried = with_slide(a, 24, 24, false);
                ASSERT_TRUE(carried.loco_clip == walk_nw.chd_index && carried.loco_mirrored == walk_nw.mirrored && carried.facing == 7);
            }
        }
        // a swimmer, an ant that another system animates and a frozen one are the engine's picture (the swim clips are its own, not a walk on land)
        {
            sim::AntSnapshot a = walking_ant();
            a.is_swimming = true;
            ASSERT_TRUE(with_slide(a, 24, 24, false).loco_clip == a.loco_clip);
            ASSERT_TRUE(with_slide(a, 24, 24, false).facing == a.facing);
            sim::AntSnapshot f = standing_ant();
            f.frozen = true;
            ASSERT_TRUE(with_slide(f, 24, 24, false).loco_clip == f.loco_clip);
            sim::AntSnapshot x = standing_ant();
            x.loco_clip = 0x7FFE;
            ASSERT_TRUE(with_slide(x, 24, 24, false).loco_clip == 0x7FFE);
        }
        // the engine walks it west and the slide pulls it east at 80 px/s: it stands, facing west (the mirror of the idle clip east)
        {
            sim::AntSnapshot a = walking_ant();
            const mv::MotionClip walk_w = mv::walk_clip(0, mv::kTerrainGrass, 6, false);
            const mv::MotionClip idle_w = mv::idle_clip(0, 6, false);
            a.facing = 6;
            a.loco_clip = walk_w.chd_index;
            a.loco_mirrored = walk_w.mirrored;
            const sim::AntSnapshot drawn = with_slide(a, -24, 0, false);
            ASSERT_TRUE(drawn.loco_clip == idle_w.chd_index && drawn.loco_mirrored == idle_w.mirrored && drawn.facing == 6 && drawn.loco_frame == 0);
        }
        // a carried ant walks with the crumb it carries
        {
            sim::AntSnapshot a = walking_ant();
            a.is_holding = true;
            const mv::MotionClip carry_east = mv::walk_clip(0, mv::kTerrainGrass, 2, true);
            a.loco_clip = carry_east.chd_index;
            a.loco_mirrored = carry_east.mirrored;
            const sim::AntSnapshot drawn = with_slide(a, 0, -24, false);
            ASSERT_TRUE(drawn.loco_clip == mv::walk_clip(0, mv::kTerrainGrass, 3, true).chd_index);
        }
        // on mud the engine walks the ant at a third of the pace on grass: a slide that pulls it back at 80 px/s carries it back (it is not drawn standing as on grass), in the mud walk
        {
            sim::AntSnapshot m = walking_ant();
            const mv::MotionClip mud_east = mv::walk_clip(0, mv::kTerrainMud, 2, false);
            const mv::MotionClip mud_west = mv::walk_clip(0, mv::kTerrainMud, 6, false);
            ASSERT_TRUE(mud_east.valid() && mud_west.valid());
            m.loco_clip = mud_east.chd_index;
            m.loco_mirrored = mud_east.mirrored;
            m.is_on_mud = true;
            const sim::AntSnapshot drawn = with_slide(m, 24, 0, false);
            ASSERT_TRUE(drawn.loco_clip == mud_west.chd_index && drawn.loco_mirrored == mud_west.mirrored && drawn.facing == 6);
        }
        // the fog cut the slide short (the drawn place is not the whole slide): the ant is drawn as the engine has it
        {
            sim::WorldState world;
            world.width = 10;
            world.height = 10;
            world.fog_of_war_enabled = true;
            world.fog_revealed.assign(100, 0);
            for (int32_t x = 4; x <= 6; ++x) world.fog_revealed[static_cast<size_t>(5 * 10 + x)] = 1;
            world.player_alliances.fill(255);
            sim::AntSnapshot a = standing_ant();
            a.tile_x = 5;
            a.tile_y = 5;
            a.px = 5 * 32 + 16;
            a.py = 5 * 32 + 16;
            ViewGlide glide;
            glide.add(a.id, -64, 0, false);                                                        // 64 px to the left: tile 3, which the fog covers
            const sim::AntSnapshot drawn = glided_ant(glide, world, 0, a);
            ASSERT_EQ(drawn.px, a.px - 48);                                                 // (cut to three quarters)
            const sim::AntSnapshot posed_one = slid_pose(glide, a, drawn);
            ASSERT_TRUE(posed_one.loco_clip == a.loco_clip && posed_one.facing == a.facing);
            ViewGlide inside;
            inside.add(a.id, -30, 0, false);                                                       // 30 px: on explored ground, the whole slide is drawn
            ASSERT_TRUE(slid_pose(inside, a, glided_ant(inside, world, 0, a)).loco_clip != a.loco_clip);
        }
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

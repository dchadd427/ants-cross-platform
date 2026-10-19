// Application integration tests: suite 13, a game of this computer on an Extended map taller than 90 rows (100 x 100, rules 1: tests/common/crowd_level.hpp), the way the desktop game
// starts it (--map) and the way the web page starts the map maker's test game (--play with the file in the maps folder), against bots; the setup screen's list; the maps that are too big;
// the camera, the edge scroll and the minimap at that size. (The refusals of a room are in test_map_fetch_app.cpp, the ticks of four bots twice in tests/test_ai/test_ai_extended.cpp.)
#include "app_integration_fixtures.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/edge_scroll.hpp"
#include "crowd_level.hpp"

namespace {

// A folder of this run with the maps given (bytes written as they are), removed at the end
struct Folder {
    std::filesystem::path path;
    explicit Folder(const std::string& name) : path(temp_path_of_this_run(name)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }
    ~Folder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
};

std::string slurp_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The three seats of the bots (1 to 3) at the medium level; the player sits at seat 0
std::vector<ants::ai::BotSpec> three_bots() {
    std::vector<ants::ai::BotSpec> bots(3);
    for (uint8_t i = 0; i < 3; ++i) {
        bots[i].seat = static_cast<uint8_t>(i + 1);
        bots[i].level = ants::ai::Level::Medium;
    }
    return bots;
}

size_t living(Application& app) {
    size_t n = 0;
    for (const auto& a : app.sim().get_world_state().ants) n += a.hp > 0 ? 1u : 0u;
    return n;
}

// What the game writes to its error stream while the object lives
struct CerrCapture {
    std::ostringstream buffer;
    std::streambuf* old;
    CerrCapture() : old(std::cerr.rdbuf(buffer.rdbuf())) {}
    ~CerrCapture() { std::cerr.rdbuf(old); }
    CerrCapture(const CerrCapture&) = delete;
    CerrCapture& operator=(const CerrCapture&) = delete;
    std::string text() const { return buffer.str(); }
};

}  // namespace

void run_suite_13_extended_play() {
    TEST_CASE("13.1 --map on a 100 x 100 Extended map with three bots: the game starts at once, the bots play, the ants fight (ticks advance, the ants alive change)") {
        Folder dir("ants_ext_map");
        const std::string file = (dir.path / "Big.lvl").string();
        ASSERT_TRUE(ants_test::write_crowd_file(file, ants_test::crowd_level()));
        const std::string before = slurp_file(file);
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        cfg.default_map_path = file;
        cfg.bots = three_bots();
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.match_running() && app.bots() != nullptr);
        ASSERT_EQ(app.sim().grid().width(), 100u);
        ASSERT_EQ(app.sim().grid().height(), 100u);
        ASSERT_EQ(app.sim().grid().rules(), LVL_RULES_EXTENDED);
        ASSERT_EQ(living(app), size_t{100});
        const uint64_t start_tick = app.sim().current_tick();
        for (int i = 0; i < 2400; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.sim().current_tick(), start_tick + 2400u);
        ASSERT_TRUE(living(app) != 100u);                                       // (the ants fight: some die, some hatch)
        ASSERT_EQ(slurp_file(file), before);                                    // (the map file is only read)
        app.shutdown();
    } TEST_END();

    TEST_CASE("13.2 The web page's order (--play with the map in the maps folder, loading screen, quick help, the get-ready dialog, three bots): the same game; back to the setup screen, which leaves the file as it was") {
        Folder dir("ants_ext_play");
        const std::string file = (dir.path / "playtest.lvl").string();
        ASSERT_TRUE(ants_test::write_crowd_file(file, ants_test::crowd_level()));
        const std::string before = slurp_file(file);
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.play_at_once = true;
        cfg.default_map_path = file;
        cfg.bots = three_bots();
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.match_running() && app.hud().is_match_start_modal_active() && app.bots() != nullptr);
        ASSERT_EQ(app.sim().grid().width(), 100u);
        ASSERT_EQ(app.sim().grid().height(), 100u);
        for (int i = 0; i < 700; ++i) app.update_simulation(0.05f);              // (the dialog, then a few hundred ticks)
        ASSERT_TRUE(!app.hud().is_match_start_modal_active() && app.sim().current_tick() > 100);
        app.return_to_map_select();
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());
        ASSERT_EQ(slurp_file(file), before);
        app.shutdown();
    } TEST_END();

    TEST_CASE("13.3 The setup screen's list skips an Extended file (a room's list is Classic maps only); a START on one that the page or the command line named is a game of this computer") {
        Folder dir("ants_ext_list");
        std::error_code ec;
        std::filesystem::copy_file("Original-Ants/Maps/TINY.LVL", dir.path / "TINY.LVL", ec);
        ASSERT_FALSE(ec);
        const std::string big = (dir.path / "Big.lvl").string();
        ASSERT_TRUE(ants_test::write_crowd_file(big, ants_test::crowd_level()));
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.maps_dir = dir.path.string();
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        bool tiny_listed = false, big_listed = false;
        for (const auto& m : app.map_select().get_maps()) {
            tiny_listed = tiny_listed || m.filename == "TINY.LVL";
            big_listed = big_listed || m.filename == "Big.lvl";
        }
        ASSERT_TRUE(tiny_listed);
        ASSERT_FALSE(big_listed);
        ASSERT_TRUE(app.start_game(big));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.sim().grid().height(), 100u);
        app.shutdown();
    } TEST_END();

    TEST_CASE("13.4 The largest maps the game plays (120 columns, 128 rows) start; a map one column wider or one row taller is refused at the start with the reason, in a game of this computer too") {
        Folder dir("ants_ext_limits");
        for (const auto& shape : {std::pair<uint32_t, uint32_t>{128, 120}, std::pair<uint32_t, uint32_t>{121, 100}}) {
            const std::string file = (dir.path / "Max.lvl").string();
            ASSERT_TRUE(ants_test::write_crowd_file(file, ants_test::crowd_level(shape.first, shape.second)));
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = file;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.sim().grid().height(), shape.first);
            ASSERT_EQ(app.sim().grid().width(), shape.second);
            app.shutdown();
        }
        for (const auto& shape : {std::pair<uint32_t, uint32_t>{100, 121}, std::pair<uint32_t, uint32_t>{129, 100}}) {
            const std::string file = (dir.path / "Over.lvl").string();
            ASSERT_TRUE(ants_test::write_crowd_file(file, ants_test::crowd_level(shape.first, shape.second)));
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = file;
            CerrCapture said;
            ASSERT_FALSE(app.init(cfg));
            ASSERT_TRUE(said.text().find("Failed to load Level") != std::string::npos);
            ASSERT_TRUE(said.text().find("Extended maps can't be played online yet") == std::string::npos);      // (the sentence of a room is for a room)
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("13.5 The camera, the edge scroll and the minimap at 100 x 100 (3,200 pixels a side): the view stays inside the map at every border, the minimap maps its corners to the corners of the map") {
        constexpr int32_t kTiles = 100;
        constexpr int32_t kPixels = kTiles * 32;
        ViewportCamera cam;
        for (const float v : {-500.0f, 0.0f, 1000.0f, 3000.0f, 3200.0f, 9000.0f}) {
            cam.x = v;
            cam.y = v;
            cam.clamp_to_bounds(kTiles, kTiles);
            ASSERT_TRUE(cam.world_x >= 0 && cam.world_y >= 0);
            ASSERT_TRUE(cam.world_x + cam.visible_w() <= kPixels && cam.world_y + cam.visible_h() <= kPixels);
        }
        cam.set_origin(1.0e6, 1.0e6, kTiles, kTiles);
        ASSERT_EQ(cam.world_x, kPixels - cam.visible_w());                      // the far corner is reachable to the pixel
        ASSERT_EQ(cam.world_y, kPixels - cam.visible_h());
        cam.center_on(kPixels - 1, kPixels - 1, kTiles, kTiles);
        ASSERT_EQ(cam.world_x, kPixels - cam.visible_w());
        cam.center_on(0, 0, kTiles, kTiles);
        ASSERT_TRUE(cam.world_x == 0 && cam.world_y == 0);
        // the edge scroll: at the left border of the screen the view goes left and stops at 0; at the bottom right it goes right and down and stops at the far corner
        ants::app::EdgeScroll step = ants::app::edge_scroll_step(0, 240, 50, 0, 0, kTiles, kTiles);
        ASSERT_TRUE(step.dx == 0 && step.dy == 0);
        step = ants::app::edge_scroll_step(0, 240, 50, 1000, 1000, kTiles, kTiles);
        ASSERT_TRUE(step.dx < 0);
        const int32_t far_x = kPixels - ants::app::kOrigViewW, far_y = kPixels - ants::app::kOrigViewH;
        step = ants::app::edge_scroll_step(639, 479, 50, far_x, far_y, kTiles, kTiles);
        ASSERT_TRUE(step.dx == 0 && step.dy == 0);
        step = ants::app::edge_scroll_step(639, 479, 50, 0, 0, kTiles, kTiles);
        ASSERT_TRUE(step.dx > 0 && step.dy > 0);
        // the minimap (480, 35) - (599, 126): its corners are the corners of the map, the middle is the middle (the point in tiles, then pixels)
        int32_t wx = -1, wy = -1;
        ants::app::minimap_point(480, 35, kTiles, kTiles, wx, wy);
        ASSERT_TRUE(wx <= 1 && wy <= 1 && wx >= -kPixels / 20 && wy >= -kPixels / 20);
        ants::app::minimap_point(598, 125, kTiles, kTiles, wx, wy);
        ASSERT_TRUE(wx >= kPixels - kPixels / 20 - 1 && wx <= kPixels + kPixels / 20 && wy >= kPixels - kPixels / 20 - 1 && wy <= kPixels + kPixels / 20);
        ants::app::minimap_point(539, 80, kTiles, kTiles, wx, wy);
        ASSERT_TRUE(wx > kPixels / 2 - kPixels / 10 && wx < kPixels / 2 + kPixels / 10 && wy > kPixels / 2 - kPixels / 10 && wy < kPixels / 2 + kPixels / 10);
    } TEST_END();
}

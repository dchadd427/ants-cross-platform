// The Yard, part 1: the model and the HUD without a window (docs/YARD.md). The picture's geometry (ScreenLayout::map_only), the puppet world that the batches of the server rewrite (YardPuppet), the
// sink that turns the HUD's orders into intents (YardSink), and what the HUD keeps, drops and changes in the Yard's layout (the gates behind `layout_.yard`).
#include "yard_fixtures.hpp"

using namespace yardtest;

namespace {

constexpr int32_t kTilesW = 128;
constexpr int32_t kTilesH = 96;
constexpr uint32_t kMapW = 128u;       // (the same, for the calls that take the map's size as unsigned numbers)
constexpr uint32_t kMapH = 96u;

/// The first index of the animations (0 .. 1343) that is not one of the ants' clips of the tables
uint16_t first_index_that_is_no_clip() {
    for (uint16_t i = 0; i < sim::movement::kTileIdCount; ++i) {
        if (!sim::movement::clip_by_chd(i, false).valid()) return i;
    }
    return yard::kNoClip;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// L: the picture
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void layout_cases() {
    group("L1", "ScreenLayout::map_only: the whole picture is the map view, a slot in the top right corner is the minimap, nothing else");
    const ScreenLayout y = ScreenLayout::map_only(960, 540);
    check(y.yard && y.width == 960 && y.height == 540 && y.view_x == 0 && y.view_y == 0, "960 x 540: the flag, the size and the corner of the view");
    check_rect(y.view(), LayoutRect{0, 0, 960, 540}, "the view is the whole picture");
    check_rect(y.minimap(), LayoutRect{960 - 119 - 14, 14, 119, 91}, "the minimap's slot: the game's 119 x 91, 14 px from the top and the right");
    check_rect(y.chat_view(), LayoutRect{}, "no chat log");
    check_rect(y.panel_fill(), LayoutRect{}, "no panel");
    check(!y.chat_view().contains(0, 0) && !y.chat_view().contains(959, 539) && !y.panel_fill().contains(900, 100), "an empty rectangle contains no point");
    check(!y.is_classic(), "the Yard's picture is not the original's");
    check(y == ScreenLayout::map_only(960, 540) && y != ScreenLayout::map_only(959, 540) && y != ScreenLayout::map_only(960, 541), "equal for the same size, not for another");
    check(y != ScreenLayout::with_size(960, 540), "the Yard's 960 x 540 is not the game's 960 x 540");
    ScreenLayout without_flag = y;
    without_flag.yard = false;
    check(without_flag != y && !(without_flag == y), "the flag alone makes two layouts of the same numbers differ");

    group("L2", "every canvas from 320 x 240 is a picture; the minimap keeps inside it");
    const struct {
        int32_t w, h;
    } sizes[] = {{320, 240}, {390, 600}, {360, 640}, {480, 320}, {700, 400}, {960, 540}, {1280, 720}, {1920, 1080}, {4096, 4096}};
    for (const auto& s : sizes) {
        const ScreenLayout l = ScreenLayout::map_only(s.w, s.h);
        const std::string at = std::to_string(s.w) + " x " + std::to_string(s.h);
        check(l.width == s.w && l.height == s.h, at + ": the canvas is the picture");
        check_rect(l.view(), LayoutRect{0, 0, s.w, s.h}, at + ": the view");
        const LayoutRect m = l.minimap();
        check(m.x >= 0 && m.y == 14 && m.right() == s.w - 14 && m.bottom() <= s.h && m.w == 119 && m.h == 91, at + ": the minimap in the corner, inside the picture: " + text_of(m));
        const LayoutRect wide = l.minimap_for_map(kTilesW, kTilesH);                  // (the Yard's map is wider than tall: the slot's top part)
        check(wide.x == m.x && wide.y == m.y && wide.w == 119 && wide.h == 89, at + ": the minimap of a 128 x 96 map is 119 x 89 at the slot's top: " + text_of(wide));
    }
    check_rect(ScreenLayout::map_only(100, 50).view(), LayoutRect{0, 0, 320, 240}, "a canvas below 320 x 240 is taken as 320 x 240");
    check(ScreenLayout::map_only(100, 50).width == 320 && ScreenLayout::map_only(100, 50).height == 240, "... in both sizes");
    check(ScreenLayout::map_only(390, 600).minimap().x == 257, "an upright phone's minimap is at x 257 (as in the pictures)");

    group("L3", "the layouts of the game itself have no Yard in them and give what they always gave");
    const ScreenLayout c = ScreenLayout::classic();
    const ScreenLayout w = ScreenLayout::with_size(960, 540);
    check(!c.yard && !w.yard && !ScreenLayout::with_size(640, 480).yard && !ScreenLayout::with_size(1260, 540).yard, "no yard flag anywhere but map_only");
    check(c.is_classic() && c == ScreenLayout::with_size(640, 480) && ScreenLayout{} == c, "the classic layout is still itself");
    check_rect(c.view(), LayoutRect{16, 21, 442, 440}, "classic: the view");
    check_rect(c.minimap(), LayoutRect{480, 35, 119, 91}, "classic: the minimap");
    check_rect(c.chat_view(), LayoutRect{482, 299, 138, 101}, "classic: the chat log");
    check_rect(c.panel_fill(), LayoutRect{480, 22, 160, 458}, "classic: the panel");
    check_rect(w.view(), LayoutRect{16, 21, 762, 500}, "16:9: the view");
    check_rect(w.minimap(), LayoutRect{800, 35, 119, 91}, "16:9: the minimap");
    check_rect(w.chat_view(), LayoutRect{802, 299, 138, 161}, "16:9: the chat log");
    check_rect(w.panel_fill(), LayoutRect{800, 22, 160, 518}, "16:9: the panel");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// P: the puppet world
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct PuppetRig {
    PuppetRig() {
        engine.init_test_world(kTilesW, kTilesH, 1, 600000);
        puppet = std::make_unique<YardPuppet>(engine);
    }
    sim::SimulationEngine engine;
    std::unique_ptr<YardPuppet> puppet;
};

void puppet_cases() {
    group("P1", "the pool: units that are removed until a batch uses them");
    {
        PuppetRig r;
        check(r.puppet->slots() == YardPuppet::kSlots && YardPuppet::kSlots == 160, "160 slots");
        check(YardPuppet::kSlots >= yard::kMaxOwnAnts + yard::kMaxForeignAnts, "room for the visitor's ants and the most that a batch holds of the others'");
        check(r.engine.get_world_state().ants.empty() && r.puppet->shown() == 0, "no ant is shown before the first batch");
        check(r.engine.current_tick() == 0, "the engine has not ticked");
        check(r.engine.get_world_state().width == kTilesW && r.engine.get_world_state().height == kTilesH, "the world is the map's");
    }

    group("P2", "a batch rewrites the ants of the engine: the picture's fields are the record's");
    {
        PuppetRig r;
        const std::vector<yard::AntRecord> batch = {make_ant(101, 0, 980, 790, idle_clip_of(4)), make_ant(102, 0, 1020, 812, walk_clip_of(3), 2), make_ant(201, 1, 1130, 740, walk_clip_of(6), 1),
                                                     make_ant(202, 2, 1190, 800, action_clip_of(ActionClip::Attack, 6), 3), make_ant(203, 3, 1160, 806, action_clip_of(ActionClip::Hit, 2), 2)};
        check(r.puppet->apply(batch) == 5, "five ants are shown");
        const sim::WorldState& world = r.engine.get_world_state();
        check(world.ants.size() == 5 && r.puppet->shown() == 5 && r.puppet->left_out() == 0, "five ants in the world, none left out");
        for (const yard::AntRecord& rec : batch) {
            const uint32_t slot = r.puppet->slot_id(rec.id);
            const sim::AntSnapshot* a = find_ant(world, slot);
            const std::string who = "ant " + std::to_string(rec.id);
            check(slot != 0 && a != nullptr, who + " has a slot and is in the world");
            if (a == nullptr) continue;
            check(r.puppet->server_id(slot) == rec.id, who + ": the slot gives the server's id back");
            check(a->player_id == rec.color && a->px == rec.px && a->py == rec.py && a->tile_x == rec.px / 32 && a->tile_y == rec.py / 32, who + ": color and place");
            check(a->loco_clip == rec.clip && a->loco_mirrored == rec.mirrored && a->loco_frame == rec.frame && a->loco_left_ms == rec.left_ms, who + ": clip, mirrored, frame, ms left");
            check(a->hp == 10 && a->max_hp == 10 && a->type == sim::AntType::Worker && a->raw_type == sim::AntType::Worker, who + ": a whole worker");
            check(!a->frozen && !a->is_holding && !a->is_drowning && !a->is_swimming && a->burn_elapsed_ms < 0, who + ": nothing else is shown");
            const bool idle = rec.clip == idle_clip_of(4).chd_index;
            check(a->state == (idle ? sim::UnitState::Idle : sim::UnitState::Walking), who + ": an idle clip is the idle state (the renderer loops it between ticks), every other is not");
            check(r.puppet->is_own(slot) == (rec.color == 0), who + ": own exactly when color 0");
        }
        check(r.engine.current_tick() == 0, "applying a batch does not tick the engine");
    }

    group("P3", "an ant keeps its slot from batch to batch; a gone ant frees its slot, and the slot is the last to come back");
    {
        PuppetRig r;
        std::vector<yard::AntRecord> batch = {make_ant(1, 0, 100, 100, idle_clip_of(4)), make_ant(2, 1, 200, 200, idle_clip_of(4)), make_ant(3, 2, 300, 300, idle_clip_of(4))};
        r.puppet->apply(batch);
        const uint32_t slot1 = r.puppet->slot_id(1);
        const uint32_t slot2 = r.puppet->slot_id(2);
        const uint32_t slot3 = r.puppet->slot_id(3);
        check(slot1 != 0 && slot2 != 0 && slot3 != 0 && slot1 != slot2 && slot2 != slot3 && slot1 != slot3, "three ants, three slots");
        batch[0].px = 150;
        batch[1].px = 250;
        std::swap(batch[0], batch[2]);                                              // (another order in the batch: the slots stay with the ants)
        r.puppet->apply(batch);
        check(r.puppet->slot_id(1) == slot1 && r.puppet->slot_id(2) == slot2 && r.puppet->slot_id(3) == slot3, "the same slots after another batch in another order");
        check(find_ant(r.engine.get_world_state(), slot1)->px == 150, "... and the new place");
        batch.erase(std::remove_if(batch.begin(), batch.end(), [](const yard::AntRecord& a) { return a.id == 2; }), batch.end());
        r.puppet->apply(batch);
        check(r.puppet->slot_id(2) == 0 && r.puppet->server_id(slot2) == 0 && !r.puppet->is_own(slot2) && find_ant(r.engine.get_world_state(), slot2) == nullptr, "ant 2 is gone: no slot, nothing in the world");
        check(r.puppet->shown() == 2, "two ants are shown");
        batch.push_back(make_ant(4, 1, 400, 400, idle_clip_of(4)));
        r.puppet->apply(batch);
        check(r.puppet->slot_id(4) != 0 && r.puppet->slot_id(4) != slot2, "a newcomer does not take the slot that was just freed");
        batch.push_back(make_ant(2, 1, 220, 220, idle_clip_of(4)));                 // (an ant that comes back is a newcomer)
        r.puppet->apply(batch);
        check(r.puppet->slot_id(2) != 0 && find_ant(r.engine.get_world_state(), r.puppet->slot_id(2)) != nullptr, "an ant that comes back is shown again");
        r.puppet->apply({});
        check(r.puppet->shown() == 0 && r.engine.get_world_state().ants.empty(), "an empty batch removes every ant");
    }

    group("P4", "a slot that the caller still points at is never given to another ant");
    {
        PuppetRig r;
        std::vector<yard::AntRecord> batch = {make_ant(1, 0, 100, 100, idle_clip_of(4)), make_ant(2, 0, 140, 100, idle_clip_of(4)), make_ant(3, 1, 400, 400, idle_clip_of(4))};
        r.puppet->apply(batch);
        const uint32_t pointed = r.puppet->slot_id(2);
        const uint32_t other = r.puppet->slot_id(3);
        batch.erase(batch.begin() + 1);                                             // ant 2 leaves (it dies)
        batch.erase(batch.begin() + 1);                                             // ... and ant 3
        r.puppet->apply(batch, {pointed});
        check(find_ant(r.engine.get_world_state(), pointed) == nullptr, "the slot of a gone ant is empty even when it is pointed at");
        bool taken = false;
        for (uint16_t id = 1000; id < 1159; ++id) {                                 // 159 newcomers: every slot but one is used
            batch.push_back(make_ant(id, 1, 100 + (id % 50) * 40, 100, idle_clip_of(4)));
        }
        r.puppet->apply(batch, {pointed});
        for (uint16_t id = 1000; id < 1159; ++id) taken = taken || r.puppet->slot_id(id) == pointed;
        check(!taken, "no newcomer has the pointed slot, though the pool is nearly full");
        check(r.puppet->slot_id(1) != 0 && find_ant(r.engine.get_world_state(), pointed) == nullptr, "the pointed slot stays empty");
        check(r.puppet->shown() == 159 && r.puppet->left_out() == 1, "159 of the 160 ants are shown: the pool has one slot less while it is pointed at");
        bool other_used = false;
        for (uint16_t id = 1000; id < 1159; ++id) other_used = other_used || r.puppet->slot_id(id) == other;
        check(other_used, "a slot that nobody points at is used again");
        r.puppet->apply(batch, {});
        check(r.puppet->shown() == 160 && r.puppet->left_out() == 0, "when nothing points at the slot any more it is free again: the pool is full");
    }

    group("P5", "what the server may send is checked: a record that cannot be shown is left out, never a crash");
    {
        PuppetRig r;
        yard::AntRecord no_id = make_ant(0, 0, 100, 100, idle_clip_of(4));
        yard::AntRecord bad_color = make_ant(2, 4, 100, 100, idle_clip_of(4));
        yard::AntRecord bad_clip = make_ant(3, 0, 100, 100, idle_clip_of(4));
        bad_clip.clip = yard::kNoClip;
        yard::AntRecord unknown_clip = make_ant(4, 0, 100, 100, idle_clip_of(4));
        unknown_clip.clip = first_index_that_is_no_clip();                           // (an animation of the archive, but not one of the ants' clips)
        check(unknown_clip.clip != yard::kNoClip, "the tables do not hold every animation");
        yard::AntRecord good = make_ant(5, 0, 100, 100, idle_clip_of(4));
        yard::AntRecord twice = make_ant(5, 1, 900, 900, idle_clip_of(4));          // (the same id again: the first is shown)
        check(r.puppet->apply({no_id, bad_color, bad_clip, unknown_clip, good, twice}) == 1, "one ant is shown");
        check(r.puppet->shown() == 1, "ant 5 is shown once");
        check(r.puppet->left_out() == 5, "the five others are left out: " + std::to_string(r.puppet->left_out()));
        const sim::AntSnapshot* a = find_ant(r.engine.get_world_state(), r.puppet->slot_id(5));
        check(a != nullptr && a->player_id == 0 && a->px == 100, "the first record of an id is the one that is shown");
        check(r.puppet->slot_id(0) == 0 && r.puppet->slot_id(2) == 0 && r.puppet->slot_id(3) == 0, "an id that was refused has no slot");
        check(r.puppet->server_id(999999) == 0 && r.puppet->server_id(0) == 0 && r.puppet->slot_id(70000) == 0 && !r.puppet->is_own(999999), "unknown slots and ids answer 0");
    }
    {
        PuppetRig r;                                                                // places and frames that are off the map or the clip are brought in, not refused
        yard::AntRecord far = make_ant(1, 0, 100, 100, walk_clip_of(3));
        far.px = 65535;
        far.py = 65535;
        far.frame = 255;
        far.left_ms = 65535;
        r.puppet->apply({far});
        const sim::AntSnapshot* a = find_ant(r.engine.get_world_state(), r.puppet->slot_id(1));
        check(a != nullptr && a->px == kTilesW * 32 - 1 && a->py == kTilesH * 32 - 1, "a place beyond the map is the map's last pixel");
        check(a != nullptr && a->loco_frame == walk_clip_of(3).count - 1 && a->loco_left_ms == 65535, "a frame beyond the clip is its last frame");
        check(a != nullptr && a->tile_x == kTilesW - 1 && a->tile_y == kTilesH - 1, "the tile is the map's last");
    }

    group("P6", "more ants than the pool holds: the visitor's own are never pushed out");
    {
        PuppetRig r;
        std::vector<yard::AntRecord> batch;
        for (uint16_t i = 0; i < 200; ++i) batch.push_back(make_ant(static_cast<uint16_t>(1000 + i), static_cast<uint8_t>(1 + i % 3), 100 + (i % 60) * 60, 100 + (i / 60) * 60, idle_clip_of(4)));
        for (uint16_t i = 0; i < 3; ++i) batch.push_back(make_ant(static_cast<uint16_t>(1 + i), 0, 500 + i * 40, 500, idle_clip_of(4)));      // (the last in the batch)
        const size_t shown = r.puppet->apply(batch);
        check(shown == YardPuppet::kSlots && r.puppet->shown() == YardPuppet::kSlots, "the pool is full: 160");
        check(r.puppet->left_out() == 43, "43 of the others' are left out: " + std::to_string(r.puppet->left_out()));
        check(r.puppet->slot_id(1) != 0 && r.puppet->slot_id(2) != 0 && r.puppet->slot_id(3) != 0 && r.puppet->own_shown() == 3, "the visitor's own three are shown");
        check(r.engine.get_world_state().ants.size() == YardPuppet::kSlots, "the world holds 160 ants");
        const std::vector<yard::AntRecord> again = batch;
        const size_t second = r.puppet->apply(again);                               // (the same batch again: the same ants)
        check(second == YardPuppet::kSlots && r.puppet->slot_id(1) != 0, "the same batch again changes nothing");
    }

    group("P7", "where the engine's own ids begin does not matter, and a unit on water is no problem");
    {
        sim::SimulationEngine engine;
        engine.init_test_world(kTilesW, kTilesH, 1, 600000);
        for (int32_t i = 0; i < 7; ++i) engine.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{60 + i, 60});         // (units that are there before the puppet: its ids do not start at 1)
        for (int32_t y = 0; y < 3; ++y) {
            for (int32_t x = 0; x < kTilesW; ++x) engine.grid_mut().set_terrain(x, y, sim::TERRAIN_WATER);               // (water where the pool would stand first)
        }
        const size_t before = engine.get_world_state().ants.size();
        YardPuppet puppet(engine);
        check(before == 7 && engine.get_world_state().ants.size() == 7, "the pool is removed: only the seven units are in the world");
        const size_t shown = puppet.apply({make_ant(1, 0, 100, 100, idle_clip_of(4)), make_ant(2, 1, 200, 200, idle_clip_of(4))});
        check(shown == 2 && engine.get_world_state().ants.size() == 9, "two ants are shown");
        const sim::AntSnapshot* a = find_ant(engine.get_world_state(), puppet.slot_id(1));
        check(a != nullptr && a->px == 100 && !a->is_drowning && a->hp == 10, "the ant is whole: nothing drowned");
        bool all_alive = true;
        for (const sim::AntSnapshot& s : engine.get_world_state().ants) all_alive = all_alive && s.hp > 0 && !s.is_drowning;
        check(all_alive, "no unit of the world is dead or drowning");
        check(engine.current_tick() == 0, "no tick");
    }
    {
        sim::SimulationEngine engine;                                               // (a map that is water all over: the pool still stands somewhere)
        engine.init_test_world(8, 8, 1, 600000);
        for (int32_t y = 0; y < 8; ++y) {
            for (int32_t x = 0; x < 8; ++x) engine.grid_mut().set_terrain(x, y, sim::TERRAIN_WATER);
        }
        YardPuppet puppet(engine);
        check(puppet.slots() == YardPuppet::kSlots && puppet.apply({make_ant(1, 0, 50, 50, idle_clip_of(4))}) == 1, "a pool on a map without land");
    }

    group("P8", "the places with an open game are the game's own hills, each with its own team");
    {
        PuppetRig r;
        check(r.puppet->set_hills({}) == 0 && r.engine.get_world_state().anthills.empty(), "no place: no hill");
        const std::vector<yard::Hill> hills = {{1, 36, 20}, {2, 24, 30}, {3, 100, 80}};
        check(r.puppet->set_hills(hills) == 3, "three hills");
        const sim::WorldState& world = r.engine.get_world_state();
        check(world.anthills.size() == 3 && world.anthills[0].team_id == 1 && world.anthills[0].x == 36 && world.anthills[0].y == 20 && world.anthills[2].team_id == 3 && world.anthills[2].x == 100, "their ids and places");
        check(r.engine.grid().anthills().size() == 3, "the grid lists them");
        std::vector<yard::Hill> bad = {{0, 10, 10}, {9, 10, 10}, {5, 126, 10}, {5, 10, 94}, {6, 124, 92}, {6, 30, 30}, {255, 40, 40}, {7, 20, 20}};
        const size_t set = r.puppet->set_hills(bad);
        check(set == 2, "an id of 0 or above 8, a place that does not fit the map (4 x 4 tiles) and an id that comes twice are left out: " + std::to_string(set));
        check(r.engine.get_world_state().anthills.size() == 2 && r.engine.get_world_state().anthills[0].team_id == 6 && r.engine.get_world_state().anthills[0].x == 124 &&
                  r.engine.get_world_state().anthills[0].y == 92 && r.engine.get_world_state().anthills[1].team_id == 7 && r.engine.get_world_state().anthills[1].x == 20,
              "the hills that fit: the first of a twice-named id and the one after");
        std::vector<yard::Hill> many;
        for (uint8_t i = 1; i <= 8; ++i) many.push_back(yard::Hill{i, static_cast<uint16_t>(i * 10), 10});
        many.push_back(yard::Hill{1, 5, 5});
        check(r.puppet->set_hills(many) == 8 && r.engine.get_world_state().anthills.size() == 8, "at most eight, the ninth is left out");
        check(r.puppet->set_hills({}) == 0 && r.engine.get_world_state().anthills.empty(), "an empty list takes them all away");
    }

    group("P9", "the questions that the sink and the page ask");
    {
        PuppetRig r;
        r.puppet->apply({make_ant(1, 0, 100, 200, idle_clip_of(4)), make_ant(2, 0, 300, 400, idle_clip_of(4)), make_ant(10, 1, 1000, 1000, idle_clip_of(4)), make_ant(11, 2, 1010, 1010, idle_clip_of(4)),
                         make_ant(12, 3, 1100, 1000, idle_clip_of(4))});
        check(r.puppet->foreign_ant_at(1000 / 32, 1000 / 32) == 11, "two others' ants on a tile: the last of them");
        check(r.puppet->foreign_ant_at(1100 / 32, 1000 / 32) == 12 && r.puppet->foreign_ant_at(100 / 32, 200 / 32) == 0, "another tile; an own ant is nobody's target");
        check(r.puppet->foreign_ant_at(5, 5) == 0 && r.puppet->foreign_ant_at(-1, -1) == 0, "an empty tile, a tile off the map");
        int32_t x = 0;
        int32_t y = 0;
        check(r.puppet->own_center(x, y) && x == 200 && y == 300 && r.puppet->own_shown() == 2, "the middle of the visitor's own ants");
        check(r.puppet->on_map(0, 0) && r.puppet->on_map(127, 95) && !r.puppet->on_map(128, 0) && !r.puppet->on_map(0, 96) && !r.puppet->on_map(-1, 5), "the tiles of the map");
        r.puppet->apply({make_ant(10, 1, 1000, 1000, idle_clip_of(4))});
        check(!r.puppet->own_center(x, y) && r.puppet->own_shown() == 0, "no own ant: no middle");
    }

    group("P10", "a batch costs the client little");
    {
        PuppetRig r;
        std::vector<yard::AntRecord> batch;
        for (uint16_t i = 0; i < 130; ++i) batch.push_back(make_ant(static_cast<uint16_t>(1 + i), static_cast<uint8_t>(i % 4), 200 + (i % 13) * 70, 200 + (i / 13) * 60, walk_clip_of(i % 5), i % 4));
        for (int round = 0; round < 300; ++round) {
            batch[0].px = static_cast<uint16_t>(batch[0].px + (round % 2 != 0 ? 1 : -1));
            if (r.puppet->apply(batch) != 130 || r.engine.get_world_state().ants.size() != 130) {
                check(false, "round " + std::to_string(round) + ": 130 ants are shown");
                break;
            }
        }
        check(r.puppet->shown() == 130 && r.engine.current_tick() == 0, "300 batches of 130 ants: still 130, still no tick");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// S: the sink
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

sim::Command command(sim::CommandType type, int16_t tx, int16_t ty, std::vector<uint32_t> ants) {
    sim::Command c;
    c.type = type;
    c.issuer = 0;
    c.tile_x = tx;
    c.tile_y = ty;
    c.ants = std::move(ants);
    return c;
}

void sink_cases() {
    group("S1", "a group move becomes an intent with the server's ids; the answer lets the voice and the order ring happen");
    PuppetRig r;
    r.puppet->apply({make_ant(101, 0, 100, 100, idle_clip_of(4)), make_ant(102, 0, 140, 100, idle_clip_of(4)), make_ant(103, 0, 180, 100, idle_clip_of(4)), make_ant(201, 1, 400, 400, idle_clip_of(4)),
                     make_ant(202, 2, 420, 400, idle_clip_of(4))});
    YardSink sink(*r.puppet);
    const uint32_t s101 = r.puppet->slot_id(101);
    const uint32_t s102 = r.puppet->slot_id(102);
    const uint32_t s103 = r.puppet->slot_id(103);
    const uint32_t s201 = r.puppet->slot_id(201);
    {
        const sim::CommandResult res = sink.submit(command(sim::CommandType::GroupMove, 33, 20, {s103, s101, s102}));
        check(res.status == sim::CommandResult::Status::Applied && res.accepted(), "applied");
        check(res.ack_ant == s103 && res.needing_order == 3 && res.ants_ordered == 3, "the first ant answers, three need the order");
        check(sink.queued().size() == 1, "one intent waits");
        const yard::Intent& in = sink.queued()[0];
        check(in.kind == yard::IntentKind::Move && in.tile_x == 33 && in.tile_y == 20 && in.target == 0, "a move to (33, 20)");
        check(in.ants == std::vector<uint16_t>({103, 101, 102}), "the ants are the server's ids, in the HUD's order");
    }

    group("S2", "only the visitor's own ants, at most six, never twice");
    {
        sink.clear();
        const sim::CommandResult res = sink.submit(command(sim::CommandType::GroupMove, 10, 10, {s101, s201, s101, 99999, 0}));
        check(res.status == sim::CommandResult::Status::Applied && res.needing_order == 1, "the other's ant, the repeat, an unknown slot and 0 are dropped");
        check(sink.queued().size() == 1 && sink.queued()[0].ants == std::vector<uint16_t>({101}), "one ant is ordered");
        sink.clear();
        const sim::CommandResult none = sink.submit(command(sim::CommandType::GroupMove, 10, 10, {s201, 99999}));
        check(none.status == sim::CommandResult::Status::Ignored && none.needing_order == 0 && none.ack_ant == 0 && sink.queued().empty(), "no own ant: nothing is sent, nothing is needed");
        const sim::CommandResult empty = sink.submit(command(sim::CommandType::GroupMove, 10, 10, {}));
        check(empty.status == sim::CommandResult::Status::Ignored && sink.queued().empty(), "no ant at all");
    }
    {
        PuppetRig big;
        std::vector<yard::AntRecord> own;
        for (uint16_t i = 0; i < 9; ++i) own.push_back(make_ant(static_cast<uint16_t>(500 + i), 0, 100 + i * 40, 100, idle_clip_of(4)));
        big.puppet->apply(own);
        std::vector<uint32_t> slots;
        for (uint16_t i = 0; i < 9; ++i) slots.push_back(big.puppet->slot_id(static_cast<uint16_t>(500 + i)));
        YardSink s(*big.puppet);
        const sim::CommandResult res = s.submit(command(sim::CommandType::GroupMove, 5, 5, slots));
        check(res.status == sim::CommandResult::Status::Applied && res.needing_order == yard::kMaxOrderAnts && s.queued()[0].ants.size() == yard::kMaxOrderAnts, "nine own ants named: six are sent");
        check(s.queued()[0].ants == std::vector<uint16_t>({500, 501, 502, 503, 504, 505}), "the first six of the HUD's order (the nearest)");
    }

    group("S3", "a tile off the map is refused, a stop has no tile");
    {
        sink.clear();
        const std::vector<std::pair<int16_t, int16_t>> off_the_map = {{-1, 5}, {5, -1}, {128, 5}, {5, 96}, {32767, 0}, {0, -32768}};
        for (const auto& t : off_the_map) {
            const sim::CommandResult res = sink.submit(command(sim::CommandType::GroupMove, t.first, t.second, {s101}));
            check(res.status == sim::CommandResult::Status::RejectedMalformed && res.needing_order == 0, "tile (" + std::to_string(t.first) + ", " + std::to_string(t.second) + ") is refused");
        }
        check(sink.queued().empty(), "nothing waits");
        check(sink.submit(command(sim::CommandType::GroupMove, 127, 95, {s101})).accepted() && sink.submit(command(sim::CommandType::GroupMove, 0, 0, {s101})).accepted(), "the corners of the map are fine");
        sink.clear();
        const sim::CommandResult res = sink.submit(command(sim::CommandType::Stop, 0, 0, {s101, s102}));
        check(res.status == sim::CommandResult::Status::Applied && res.needing_order == 2, "a stop of two ants");
        check(sink.queued().size() == 1 && sink.queued()[0].kind == yard::IntentKind::Stop && sink.queued()[0].tile_x == 0 && sink.queued()[0].tile_y == 0 && sink.queued()[0].target == 0 &&
                  sink.queued()[0].ants == std::vector<uint16_t>({101, 102}), "a stop intent: no tile, no target");
        check(sink.submit(command(sim::CommandType::Stop, -5, 9000, {s101})).accepted(), "a stop does not look at the tile");
    }

    group("S4", "an attack names the ant that stands on the tile");
    {
        sink.clear();
        const sim::CommandResult res = sink.submit(command(sim::CommandType::GroupAttack, 400 / 32, 400 / 32, {s101, s102}));
        check(res.status == sim::CommandResult::Status::Applied && res.needing_order == 2, "applied");
        check(sink.queued().size() == 1 && sink.queued()[0].kind == yard::IntentKind::Attack && sink.queued()[0].target == 201 && sink.queued()[0].tile_x == 400 / 32, "an attack on the ant 201");
        sink.clear();
        const sim::CommandResult nobody = sink.submit(command(sim::CommandType::GroupAttack, 3, 3, {s101}));
        check(nobody.status == sim::CommandResult::Status::Ignored && nobody.needing_order == 0 && sink.queued().empty(), "nobody stands there any more: an attack on nothing is not sent");
        const sim::CommandResult own_there = sink.submit(command(sim::CommandType::GroupAttack, 100 / 32, 100 / 32, {s101}));
        check(own_there.status == sim::CommandResult::Status::Ignored && sink.queued().empty(), "an own ant is not a target");
    }

    group("S5", "what the Yard does not have is refused");
    {
        sink.clear();
        for (const sim::CommandType t : {sim::CommandType::GroupSpecial, sim::CommandType::Hatch, sim::CommandType::AllianceInvite, sim::CommandType::AllianceAccept, sim::CommandType::AllianceDeny,
                                          sim::CommandType::AllianceWithdraw, sim::CommandType::AllianceBreak, sim::CommandType::Quit, sim::CommandType::Drop, sim::CommandType::None}) {
            const sim::CommandResult res = sink.submit(command(t, 5, 5, {s101}));
            check(res.status == sim::CommandResult::Status::RejectedNotAllowed && !res.accepted() && res.needing_order == 0, "command type " + std::to_string(static_cast<unsigned>(t)) + " is refused");
        }
        check(sink.queued().empty(), "nothing waits");
    }

    group("S6", "the queue is bounded and is handed over whole");
    {
        sink.clear();
        for (int16_t i = 0; i < 100; ++i) sink.submit(command(sim::CommandType::GroupMove, i, 1, {s101}));
        check(sink.queued().size() == YardSink::kMaxQueued && YardSink::kMaxQueued == 64, "64 wait, the oldest are dropped");
        check(sink.queued().front().tile_x == 36 && sink.queued().back().tile_x == 99, "the newest 64 are the ones that wait, oldest first");
        const std::vector<yard::Intent> taken = sink.take();
        check(taken.size() == 64 && sink.queued().empty() && sink.take().empty(), "take() hands them over and empties the queue");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// H: the HUD in the Yard's layout
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

std::vector<yard::AntRecord> three_and_others() {
    return {make_ant(101, 0, 980, 790, idle_clip_of(4)), make_ant(102, 0, 1020, 812, idle_clip_of(3)), make_ant(103, 0, 980, 836, idle_clip_of(4)), make_ant(201, 1, 1130, 740, walk_clip_of(3), 2),
            make_ant(202, 2, 1190, 800, action_clip_of(ActionClip::Attack, 6), 3), make_ant(203, 3, 1160, 806, action_clip_of(ActionClip::Hit, 2), 2)};
}

void hud_cases(const assets::AssetArchive& arc) {
    group("H1", "no button of the top bar, the panel or the dialogs has a size: nothing that is not drawn can be hit");
    {
        YardHud r;
        const UIButton* buttons[] = {&r.hud.help_button(), &r.hud.options_button(), &r.hud.quit_button(), &r.hud.stop_button(), &r.hud.send_to_button(), &r.hud.team_button(), &r.hud.hatch_button(),
                                     &r.hud.team_up_button(), &r.hud.quit_yes_button(), &r.hud.quit_no_button(), &r.hud.get_move_pedestal_button(), &r.hud.get_ability_pedestal_button()};
        size_t hit = 0;
        bool sized = false;
        for (const UIButton* b : buttons) sized = sized || b->w != 0 || b->h != 0;
        check(!sized, "every one of the twelve buttons is of no size");
        for (int32_t y = -10; y < 560; ++y) {
            for (int32_t x = -10; x < 980; ++x) {
                for (const UIButton* b : buttons) hit += b->contains(x, y) ? 1u : 0u;
            }
        }
        check(hit == 0, "no pixel of the picture is in a button");
        check_rect(r.hud.minimap_rect(), r.layout.minimap(), "the minimap is the layout's slot");
        check(r.hud.layout().yard, "the HUD has the Yard's layout");
        YardHud after;                                                                // (the buttons stay of no size when the layout is set again, as a resize does)
        after.hud.set_layout(ScreenLayout::map_only(390, 600));
        after.hud.init(0, false);
        after.hud.set_layout(ScreenLayout::map_only(700, 400));
        check(after.hud.help_button().w == 0 && after.hud.quit_button().h == 0 && after.hud.get_move_pedestal_button().w == 0 && after.hud.quit_yes_button().w == 0, "set_layout and init keep them so");
        YardHud plain;                                                                // (and in the game's own layout they are what they were: the control)
        plain.hud.set_layout(ScreenLayout::with_size(960, 540));
        check(plain.hud.help_button().w == 46 && plain.hud.help_button().x == 476 + 320 && plain.hud.get_move_pedestal_button().h == 73 && plain.hud.quit_yes_button().w == 49, "in the 16:9 layout of the game the buttons have their sizes");
    }

    group("H2", "a click anywhere opens nothing and presses nothing");
    {
        YardHud r;
        r.apply(three_and_others());
        r.look(900, 700);
        size_t clicks = 0;
        for (int32_t y = 0; y < 540; y += 9) {
            for (int32_t x = 0; x < 960; x += 11) {
                for (const uint8_t which : {static_cast<uint8_t>(SDL_BUTTON_LEFT), static_cast<uint8_t>(SDL_BUTTON_RIGHT)}) {
                    r.click(x, y, which);
                    ++clicks;
                }
            }
        }
        check(clicks > 8000, "about 9,000 clicks were made");
        check(!r.hud.is_quit_dialog_open() && !r.hud.is_quick_help_open() && !r.hud.is_options_open() && !r.hud.is_modal_open(), "no dialog, no window, no page");
        check(r.hud.get_chat_input().empty() && !r.hud.is_dragging(), "no chat text, no rubber band left");
        check(!r.hud.help_button().is_pressed && !r.hud.options_button().is_pressed && !r.hud.quit_button().is_pressed && !r.hud.send_to_button().is_pressed && !r.hud.stop_button().is_pressed,
              "no button is pressed");
        for (const yard::Intent& in : r.sink->queued()) check(in.kind != yard::IntentKind::Stop, "no click makes a stop (the Stop pedestal is not there)");
    }

    group("H3", "the keys: only Esc and Ctrl (Cmd) with A, N, P and S do anything");
    {
        YardHud r;
        r.apply(three_and_others());
        r.look(900, 700);
        r.click(r.screen_of(980, 800).first, r.screen_of(980, 800).second);        // (an own ant is selected)
        const std::vector<uint32_t> selected = r.hud.get_selected_ant_ids();
        check(selected.size() == 1, "one own ant is selected");
        const float cam_x = r.camera.x;
        const float cam_y = r.camera.y;
        const std::string status = r.hud.status_line().text();
        const health::Setting marks = r.hud.ant_health();
        std::vector<std::pair<int32_t, uint16_t>> blocked;
        const std::vector<int32_t> plain_keys = {SDLK_F1, SDLK_F2, SDLK_F5, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12, SDLK_RETURN, SDLK_KP_ENTER, SDLK_BACKSPACE, SDLK_TAB, SDLK_SPACE, SDLK_UP, SDLK_DOWN,
                                                 SDLK_LEFT, SDLK_RIGHT, '0', '9', 'c', 'x', 'y', 'z'};
        for (const int32_t k : plain_keys) {
            blocked.emplace_back(k, uint16_t{0});
            blocked.emplace_back(k, static_cast<uint16_t>(KMOD_LCTRL));
        }
        for (const int32_t k : {'h', 'H', 'l', 'L', 'o', 'O', 'q', 'Q', 'c', 'x', 'y'}) {                                // (the game's other Ctrl keys, and plain letters)
            blocked.emplace_back(k, static_cast<uint16_t>(KMOD_LCTRL));
            blocked.emplace_back(k, static_cast<uint16_t>(KMOD_LGUI));
            blocked.emplace_back(k, uint16_t{0});
        }
        for (const int32_t k : {'a', 'A', 'n', 'N', 'p', 'P', 's', 'S'}) blocked.emplace_back(k, uint16_t{0});             // (the kept letters without Ctrl are plain letters: nothing)
        for (const auto& k : blocked) {
            r.key(k.first, k.second);
            const bool unchanged = r.hud.get_selected_ant_ids() == selected && r.hud.get_selected_base_team_id() == -1 && !r.hud.is_modal_open() && r.hud.get_chat_input().empty() && r.camera.x == cam_x &&
                                   r.camera.y == cam_y && r.sink->queued().empty() && r.hud.ant_health().when == marks.when && r.hud.ant_health().style == marks.style && r.hud.status_line().text() == status;
            if (!unchanged) check(false, "key " + std::to_string(k.first) + " with modifiers " + std::to_string(k.second) + " changed something");
        }
        check(true, std::to_string(blocked.size()) + " keys changed nothing");
        check(r.hud.get_chat_input().empty() && r.hud.options().chat == false, "the chat is off and stays so");

        r.key(SDLK_ESCAPE);
        check(r.hud.get_selected_ant_ids().empty() && !r.hud.has_selection(), "Esc deselects");
        r.key('a', static_cast<uint16_t>(KMOD_LCTRL));
        check(r.hud.get_selected_ant_ids().size() == 3, "Ctrl+A selects all three own ants");
        r.hud.clear_selection();
        r.key('A', static_cast<uint16_t>(KMOD_LGUI));
        check(r.hud.get_selected_ant_ids().size() == 3, "Cmd+A does the same");
        r.key(SDLK_ESCAPE);
        r.key('n', static_cast<uint16_t>(KMOD_LCTRL));
        check(r.hud.get_selected_ant_ids().size() == 1, "Ctrl+N selects one own ant");
        const uint32_t first = r.hud.get_selected_ant_ids().front();
        r.key('n', static_cast<uint16_t>(KMOD_LCTRL));
        check(r.hud.get_selected_ant_ids().size() == 1 && r.hud.get_selected_ant_ids().front() != first, "... and the next one the next ant");
        r.key('p', static_cast<uint16_t>(KMOD_LCTRL));
        check(r.hud.get_selected_ant_ids().size() == 1 && r.hud.get_selected_ant_ids().front() == first, "Ctrl+P goes back");
        r.hud.set_selected_ant_ids({r.puppet->slot_id(101), r.puppet->slot_id(102)});
        r.sink->clear();
        r.key('s', static_cast<uint16_t>(KMOD_LCTRL));
        check(r.sink->queued().size() == 1 && r.sink->queued()[0].kind == yard::IntentKind::Stop && r.sink->queued()[0].ants == std::vector<uint16_t>({101, 102}), "Ctrl+S stops the selected ants");
        r.sink->clear();
        r.hud.clear_selection();
        r.key('s', static_cast<uint16_t>(KMOD_LCTRL));
        check(r.sink->queued().empty(), "Ctrl+S with nothing selected sends nothing");
    }

    group("H4", "the HUD draws the minimap and the rubber band and nothing else");
    {
        YardHud r;
        r.apply(three_and_others());
        r.puppet->set_hills({{1, 36, 20}});
        r.hud.select_base(1);
        r.hud.set_selected_ant_ids({r.puppet->slot_id(101)});
        r.look(900, 700);
        BoxRecorder rec;
        r.hud.render(rec, arc, r.world(), r.camera);
        const LayoutRect mini = r.layout.minimap();
        check(!rec.calls.empty() && rec.count('i') == 1 && rec.count('t') == 0 && rec.count('n') == 0, "the minimap's picture; no text and no named piece of the frame");
        bool inside = true;
        for (const BoxRecorder::Call& c : rec.calls) {
            if (c.kind == 's' || c.kind == 'n' || c.kind == 't') {
                inside = false;                                                      // (a sprite or text with no size: the HUD draws none in the Yard)
                continue;
            }
            inside = inside && c.x >= mini.x - 12 && c.y >= mini.y - 8 && c.x + c.w <= mini.right() + 12 && c.y + c.h <= mini.bottom() + 12;
        }
        check(inside, "every call lies on the minimap and its edge (" + std::to_string(rec.calls.size()) + " calls)");
        check(rec.count('b') == 1, "the frame of the view on the minimap is the only rectangle: no rubber band yet");
        r.move(300, 300);
        r.press(300, 300);
        r.move(420, 380);
        BoxRecorder with_band;
        r.hud.render(with_band, arc, r.world(), r.camera);
        check(with_band.count('b') == 2 && with_band.calls.size() == rec.calls.size() + 1, "while the left button drags, one rectangle more: the rubber band");
        bool band_found = false;
        for (const BoxRecorder::Call& c : with_band.calls) band_found = band_found || (c.kind == 'b' && c.x == 300 && c.y == 300 && c.w == 120 && c.h == 80);
        check(band_found, "the band is the box from the press to the pointer");
        r.release(420, 380);
        BoxRecorder after;
        r.hud.render(after, arc, r.world(), r.camera);
        check(after.calls.size() == rec.calls.size(), "after the release the band is gone");

        YardHud game;                                                                 // the control: the same HUD in the game's own 16:9 layout draws its panel, its texts and its pieces
        game.hud.set_layout(ScreenLayout::with_size(960, 540));
        game.camera.set_view(ScreenLayout::with_size(960, 540).view());
        BoxRecorder panel;
        game.hud.render(panel, arc, game.world(), game.camera);
        check(panel.count('s') + panel.count('n') + panel.count('r') > 30 && panel.calls.size() > 60, "in the game's layout the same call draws " + std::to_string(panel.calls.size()) + " pieces, the panel's and the frame's");
    }

    group("H5", "the view moves at the edge only after the pointer has stayed there for 250 ms; the minimap moves it at once");
    {
        YardHud r;
        r.look(1500, 1000);
        const int32_t ex = 959;
        const int32_t ey = 270;                                                      // the east edge, in the middle
        const float x0 = r.camera.x;
        for (int tick = 1; tick <= 5; ++tick) {
            const bool moved = r.hud.input_tick(r.camera, kMapW, kMapH, ex, ey);
            check(!moved && r.camera.x == x0, "tick " + std::to_string(tick) + " at the edge: the view stays");
        }
        check(r.hud.input_tick(r.camera, kMapW, kMapH, ex, ey) && r.camera.x > x0, "the sixth tick (250 ms later) moves the view");
        const float x1 = r.camera.x;
        check(r.hud.input_tick(r.camera, kMapW, kMapH, ex, ey) && r.camera.x > x1, "and every tick after it");
        for (int tick = 0; tick < 3; ++tick) r.hud.input_tick(r.camera, kMapW, kMapH, ex, ey);
        check(!r.hud.input_tick(r.camera, kMapW, kMapH, 480, 270), "away from the edge nothing moves");
        const float x2 = r.camera.x;
        for (int tick = 1; tick <= 5; ++tick) check(!r.hud.input_tick(r.camera, kMapW, kMapH, ex, ey) && r.camera.x == x2, "back at the edge the dwell starts again (tick " + std::to_string(tick) + ")");
        check(r.hud.input_tick(r.camera, kMapW, kMapH, ex, ey), "and the sixth moves");

        YardHud corner;                                                               // the other edges, and where the view cannot go the dwell does not run up
        corner.look(0, 0);
        for (int tick = 0; tick < 12; ++tick) check(!corner.hud.input_tick(corner.camera, kMapW, kMapH, 0, 270) && corner.camera.x == 0.0f, "at the west edge of the map the view does not move (tick " + std::to_string(tick) + ")");
        const int32_t points[][2] = {{480, 0}, {480, 539}, {0, 270}, {959, 0}, {0, 539}};
        for (const auto& p : points) {
            YardHud e;
            e.look(1500, 1000);
            const float ax = e.camera.x;
            const float ay = e.camera.y;
            bool moved_early = false;
            for (int tick = 0; tick < 5; ++tick) moved_early = e.hud.input_tick(e.camera, kMapW, kMapH, p[0], p[1]) || moved_early;
            check(!moved_early && e.camera.x == ax && e.camera.y == ay, "the edge at (" + std::to_string(p[0]) + ", " + std::to_string(p[1]) + "): five ticks, no move");
            check(e.hud.input_tick(e.camera, kMapW, kMapH, p[0], p[1]) && (e.camera.x != ax || e.camera.y != ay), "... and the sixth moves");
        }

        YardHud radar;                                                                // the minimap's drag has no dwell
        radar.look(0, 0);
        const LayoutRect mini = radar.hud.minimap_rect();
        radar.press(mini.x + mini.w / 4, mini.y + mini.h / 4);
        check(radar.hud.input_tick(radar.camera, kMapW, kMapH, mini.x + mini.w * 3 / 4, mini.y + mini.h * 3 / 4) && radar.camera.x > 0.0f, "a drag on the minimap moves the view at the first tick");
        radar.release(mini.x + mini.w * 3 / 4, mini.y + mini.h * 3 / 4);

        YardHud band;                                                                 // a rubber band at the edge scrolls nothing, and the dwell starts when it is let go
        band.look(1500, 1000);
        band.move(400, 270);
        band.press(400, 270);
        const float bx = band.camera.x;
        for (int tick = 0; tick < 8; ++tick) check(!band.hud.input_tick(band.camera, kMapW, kMapH, 959, 270) && band.camera.x == bx, "no scroll while the left button holds a band (tick " + std::to_string(tick) + ")");
        band.release(959, 270);
        for (int tick = 1; tick <= 5; ++tick) check(!band.hud.input_tick(band.camera, kMapW, kMapH, 959, 270), "after the release the dwell is of 250 ms (tick " + std::to_string(tick) + ")");
        check(band.hud.input_tick(band.camera, kMapW, kMapH, 959, 270), "and then the view moves");
    }

    group("H6", "what the HUD points at keeps its ant: it never turns into a stranger");
    {
        YardHud r;
        r.apply(three_and_others());
        r.hud.select_all_friendly(r.world());
        const std::vector<uint32_t> mine = r.hud.get_selected_ant_ids();
        check(mine.size() == 3, "three own ants are selected");
        const uint32_t dying = r.puppet->slot_id(103);
        std::vector<yard::AntRecord> batch = three_and_others();
        batch.erase(batch.begin() + 2);                                              // ant 103 dies and is gone
        r.apply(batch);
        check(find_ant(r.world(), dying) == nullptr, "its slot is empty");
        std::vector<yard::AntRecord> crowd = batch;
        for (uint16_t id = 2000; id < 2150; ++id) crowd.push_back(make_ant(id, static_cast<uint8_t>(1 + id % 3), 100 + (id % 60) * 60, 100 + (id / 60) * 30, idle_clip_of(4)));
        r.apply(crowd);
        check(find_ant(r.world(), dying) == nullptr, "150 others come, and none of them is at the slot that the selection still names");
        check(r.hud.get_selected_ant_ids() == mine, "the selection is as it was");
        r.hud.select_ant(r.puppet->slot_id(2001));                                   // an other's ant under inspection
        const uint32_t looked_at = r.hud.get_selected_ant_id();
        std::vector<yard::AntRecord> fewer = batch;
        r.apply(fewer);
        check(find_ant(r.world(), looked_at) == nullptr, "the other's ant goes away");
        for (uint16_t id = 3000; id < 3150; ++id) fewer.push_back(make_ant(id, 1, 100 + (id % 60) * 60, 100, idle_clip_of(4)));
        r.apply(fewer);
        check(find_ant(r.world(), looked_at) == nullptr && r.hud.get_selected_ant_id() == looked_at, "its slot is not used again while it is looked at");
        r.hud.clear_selection();
        r.apply(fewer);
        r.apply({});
        r.apply(crowd);
        check(r.puppet->shown() > 100, "when nothing points at a slot it can be used again");
    }

    group("H7", "the pointer model works in the Yard as it does in the game: box, click, right click, brackets");
    {
        YardHud r;
        r.apply(three_and_others());
        r.puppet->set_hills({{1, 36, 20}, {2, 24, 30}});
        r.look(700, 600);
        const auto a = r.screen_of(980, 790);
        const auto b = r.screen_of(1020, 836);
        r.drag(a.first - 40, a.second - 60, b.first + 40, b.second + 20);
        check(r.hud.get_selected_ant_ids().size() == 3, "a drag box around the three own ants selects them");
        bool all_mine = true;
        for (const uint32_t id : r.hud.get_selected_ant_ids()) all_mine = all_mine && r.puppet->is_own(id);
        check(all_mine, "and only them");
        r.hud.clear_selection();
        r.drag(40, 60, 600, 300);                                                    // (a box over the other visitors' ants too)
        bool only_mine = true;
        for (const uint32_t id : r.hud.get_selected_ant_ids()) only_mine = only_mine && r.puppet->is_own(id);
        check(only_mine && r.hud.get_selected_ant_ids().size() == 3, "a box over others' ants as well still selects only the visitor's own");
        r.sink->clear();
        const auto g = r.screen_of(1060, 900);
        r.click(g.first, g.second, SDL_BUTTON_RIGHT);
        check(r.sink->queued().size() == 1 && r.sink->queued()[0].kind == yard::IntentKind::Move && r.sink->queued()[0].tile_x == 1060 / 32 && r.sink->queued()[0].tile_y == 900 / 32 &&
                  r.sink->queued()[0].ants.size() == 3, "a right click on the ground: a move of the three to the tile");
        r.sink->clear();
        const auto foe = r.screen_of(1190, 790);
        r.click(foe.first, foe.second, SDL_BUTTON_RIGHT);
        check(r.sink->queued().size() == 1 && r.sink->queued()[0].kind == yard::IntentKind::Attack && r.sink->queued()[0].target == 202 && r.sink->queued()[0].ants.size() == 3,
              "a right click on another visitor's ant: an attack on it, by the server's id");
        r.sink->clear();
        const auto hill = r.screen_of(36 * 32 + 64, 20 * 32 + 64);
        r.click(hill.first, hill.second, SDL_BUTTON_RIGHT);
        check(r.sink->queued().empty(), "a right click on a hill orders nothing (the Yard has no hill of its own to go home to)");
        r.click(hill.first, hill.second);
        check(r.hud.get_selected_base_team_id() == 1 && r.hud.get_selected_ant_ids().empty(), "a left click on a hill selects it (the game's brackets)");
        const auto hill2 = r.screen_of(24 * 32 + 64, 30 * 32 + 64);
        r.click(hill2.first, hill2.second);
        check(r.hud.get_selected_base_team_id() == 2, "the other hill is the second place");
        r.click(foe.first, foe.second);
        check(r.hud.get_selected_ant_id() == r.puppet->slot_id(202) && !r.puppet->is_own(r.hud.get_selected_ant_id()), "a left click on another's ant inspects it");
        r.sink->clear();
        r.click(g.first, g.second, SDL_BUTTON_RIGHT);
        check(r.sink->queued().empty(), "with another's ant selected a right click gives no order (nothing of the visitor's is selected)");
    }
}

}  // namespace

void yard_model_groups(const assets::AssetArchive& arc) {
    layout_cases();
    puppet_cases();
    sink_cases();
    hud_cases(arc);
}

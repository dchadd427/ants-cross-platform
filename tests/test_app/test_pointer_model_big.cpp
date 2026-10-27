// Pointer model tests: a click or Stop on more ants than one command holds (sim::kMaxCommandAnts = 32). Ctrl+A or a drag selects every own ant; a move, a group attack and Stop name ALL of
// them as several commands of the same kind, the ants nearest the clicked tile first across the whole selection (nearest_first, ants_app/group_order_sort.hpp; HUD::submit_in_chunks, docs/CONTROLS.md,
// "A big selection"), at most HUD::kMaxCommandsPerClick of them (the ants left out are the farthest), and the engine decides
// what each ant does (the original's logic: an ant that finds no free tile stops). A SPECIAL order (steal, bomb, ...) is never chunked: one command of at most 32 ants, as before. A click of at
// most 32 ants sends what it always sent. The voice and the pedestal feedback follow the click as a whole.
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/group_order_sort.hpp"
#include "pointer_model_fixtures.hpp"

namespace {

// `count` own ants of `type` on distinct tiles of the 60 x 60 world (rows from y = 5)
std::vector<uint32_t> spawn_own(Fixture& f, size_t count, AntType type) {
    std::vector<uint32_t> ids;
    for (int32_t ty = 5; ty < 55 && ids.size() < count; ++ty) {
        for (int32_t tx = 5; tx < 55 && ids.size() < count; ++tx) ids.push_back(f.spawn(0, type, tx, ty));
    }
    f.settle();
    return ids;
}

void select_all(Fixture& f) { f.hud.handle_key_down('a', f.sim, f.cam, KMOD_CTRL, false); }
void press_stop(Fixture& f) { f.hud.handle_key_down('s', f.sim, f.cam, KMOD_CTRL, false); }

sim::Command make_cmd(sim::CommandType type, int16_t tx, int16_t ty, std::vector<uint32_t> ants) {
    sim::Command c;
    c.type = type;
    c.issuer = 0;
    c.tile_x = tx;
    c.tile_y = ty;
    c.ants = std::move(ants);
    return c;
}

// Every ant walks to a goal of its own (row 50) and the paths have come in: a walking ant can be stopped, and what it does is what an order may take away
void make_walk(Fixture& f, const std::vector<uint32_t>& ids) {
    for (size_t i = 0; i < ids.size(); ++i) f.sim.apply_command(make_cmd(sim::CommandType::GroupMove, static_cast<int16_t>(5 + i % 40), 50, {ids[i]}));
    for (int t = 0; t < 400; ++t) {
        f.settle();
        bool pending = false;
        for (uint32_t id : ids) pending = pending || f.sim.has_pending_path(id);
        if (!pending && t > 5) break;
    }
    for (int t = 0; t < 3; ++t) f.settle();
}

// The order in which a move or an attack on tile (tx, ty) names the ants `ids`: nearest first by 16 x the Chebyshev distance between the tile of the ant's pixel position and (tx, ty), ants at the same
// distance in the order of `ids`. Written here by hand (an insertion sort is stable), not with the HUD's function.
uint32_t key_of(Fixture& f, uint32_t id, int32_t tx, int32_t ty) {
    const int32_t dx = std::abs(f.unit(id).pixel_x / 32 - tx);
    const int32_t dy = std::abs(f.unit(id).pixel_y / 32 - ty);
    return static_cast<uint32_t>(dx > dy ? dx : dy) * 16u;
}

std::vector<uint32_t> by_distance(Fixture& f, const std::vector<uint32_t>& ids, int32_t tx, int32_t ty) {
    std::vector<uint32_t> out;
    for (uint32_t id : ids) {
        size_t at = out.size();
        while (at > 0 && key_of(f, out[at - 1], tx, ty) > key_of(f, id, tx, ty)) --at;
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), id);
    }
    return out;
}

std::vector<uint8_t> bytes_of(const sim::Command& c) {
    std::vector<uint8_t> out;
    sim::encode(c, out);
    return out;
}

size_t ants_in(const std::vector<sim::Command>& log) {
    size_t n = 0;
    for (const auto& c : log) n += c.ants.size();
    return n;
}

std::vector<uint32_t> ants_sent(const std::vector<sim::Command>& log) {
    std::vector<uint32_t> out;
    for (const auto& c : log) out.insert(out.end(), c.ants.begin(), c.ants.end());
    return out;
}

// The sizes of the commands: 32 each and the rest in the last one
bool shaped(const std::vector<sim::Command>& log, sim::CommandType type, size_t total) {
    if (log.size() != (total + 31) / 32) return false;
    for (size_t i = 0; i < log.size(); ++i) {
        if (log[i].type != type || log[i].issuer != 0 || log[i].ants.size() != (i + 1 < log.size() ? 32u : total - 32 * i)) return false;
    }
    return true;
}

// A sink that records the commands and answers with what a test says (the engine is not asked)
class ScriptedSink final : public sim::CommandSink {
public:
    std::vector<sim::Command> log;
    std::vector<sim::CommandResult> answers;
    sim::CommandResult submit(const sim::Command& c) override {
        log.push_back(c);
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::Applied;
        if (log.size() <= answers.size()) r = answers[log.size() - 1];
        return r;
    }
};

// The status box clips at 139 px (HUD::render_status_line): the width that this renderer measures (the shipped font where it is found, else the plain one)
bool fits_status_box(const std::string& text) {
    const Renderer renderer;
    return renderer.get_text_width(text, FontSize::Px12) <= 139;
}

sim::CommandResult answer(uint32_t needing, uint32_t ack) {
    sim::CommandResult r;
    r.status = sim::CommandResult::Status::Applied;
    r.needing_order = needing;
    r.ack_ant = ack;
    return r;
}

// What a special click sent before the click on a big selection existed: ONE command with the first 32 of the selected ants
sim::Command old_special(const std::vector<uint32_t>& ids, int16_t tx, int16_t ty) {
    return make_cmd(sim::CommandType::GroupSpecial, tx, ty, std::vector<uint32_t>(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(std::min(ids.size(), sim::kMaxCommandAnts))));
}

// The commands that a move of the selection `ids` is, built by hand: chunks of 32 in the order (what the engine gets from the HUD)
std::vector<sim::Command> hand_chunks(sim::CommandType type, int16_t tx, int16_t ty, const std::vector<uint32_t>& ids) {
    std::vector<sim::Command> out;
    for (size_t from = 0; from < ids.size(); from += sim::kMaxCommandAnts) {
        const size_t to = std::min(ids.size(), from + sim::kMaxCommandAnts);
        out.push_back(make_cmd(type, tx, ty, std::vector<uint32_t>(ids.begin() + static_cast<std::ptrdiff_t>(from), ids.begin() + static_cast<std::ptrdiff_t>(to))));
    }
    return out;
}

}  // namespace

void test_big_selection() {
    g_group = "big selection";
    std::printf("[pointer] a move, an attack or Stop on more ants than a command holds names every ant in several commands (up to the click's budget); a special order is one command of at most 32; a click of at most 32 ants sends what it always sent\n");
    // 200 thieves, Ctrl+A, a plain move on the enemy hill (a right click on the minimap, or on a hill in the fog: the pointer over it is the plain move there, not a special target): seven commands name all 200 ants, and every thief raids
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const std::vector<uint32_t> ids = spawn_own(f, 200, AntType::Thief);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        check(f.hud.get_selected_ant_ids() == ids, "Ctrl+A selects all 200 own ants in the order of the world");
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{41, 41}, false, false);
        check(shaped(sink.log, sim::CommandType::GroupMove, 200), "200 ants: seven group moves, 32 ants but the last (8)");
        check(!sink.log.empty() && sink.log[0].tile_x == 41 && sink.log[0].tile_y == 41, "of the clicked tile");
        check(ants_sent(sink.log) == by_distance(f, ids, 41, 41), "together they name every selected ant once, the nearest the clicked tile first");
        size_t raiding = 0;
        for (uint32_t id : ids) raiding += f.unit(id).orig_order == sim::AntUnit::kOrderRaid ? size_t{1} : size_t{0};
        check(raiding == 200, "every one of the 200 ants raids (" + std::to_string(raiding) + ")");
        check(f.hud.status_line().text().find("Only") == std::string::npos, "and the line does not speak of a limit");
        // the feedback is the click's, once: the sounds of a click on 30 ants
        const size_t big = f.sounds.size();
        Fixture g;
        g.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        spawn_own(g, 30, AntType::Thief);
        select_all(g);
        g.sounds.clear();
        g.hud.order_selected(g.sim, TileCoord{41, 41}, false, false);
        check(big != 0 && big == g.sounds.size(), "the voice and the click sound are heard once, as for 30 ants (" + std::to_string(big) + " and " + std::to_string(g.sounds.size()) + " sounds)");
    }
    // 40 WALKING workers and 200 thieves, a plain move on the enemy hill: no special case for anyone, all 240 are named (eight commands) and the ENGINE decides, as the original does: the thieves raid, the
    // workers are stopped by the click
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const std::vector<uint32_t> workers = spawn_own(f, 40, AntType::Worker);
        std::vector<uint32_t> ids = workers;
        for (int32_t tx = 5; tx < 45; ++tx) for (int32_t ty = 20; ty < 25; ++ty) ids.push_back(f.spawn(0, AntType::Thief, tx, ty));
        f.settle();
        make_walk(f, workers);
        for (uint32_t id : workers) check(f.unit(id).orig_order == sim::AntUnit::kOrderMove, "the workers walk before the click");
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        check(f.hud.get_selected_ant_ids().size() == 240, "Ctrl+A selects 240 ants");
        f.hud.order_selected(f.sim, TileCoord{41, 41}, false, false);
        size_t raiding = 0, stopped = 0;
        for (size_t i = 40; i < ids.size(); ++i) raiding += f.unit(ids[i]).orig_order == sim::AntUnit::kOrderRaid ? size_t{1} : size_t{0};
        for (uint32_t id : workers) stopped += f.unit(id).orig_order == sim::AntUnit::kOrderNone ? size_t{1} : size_t{0};
        check(shaped(sink.log, sim::CommandType::GroupMove, 240) && ants_sent(sink.log).size() == 240, "all 240 ants are named, in eight commands");
        check(raiding == 200 && stopped == 40, "the engine's result: 200 thieves raid (" + std::to_string(raiding) + ") and the 40 workers stop (" + std::to_string(stopped) + ")");
    }
    // a move on the player's own hill is the goal search of the entrance: 200 walking workers are all named (seven commands) and the engine's answer is what the same commands give it directly
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const std::vector<uint32_t> ids = spawn_own(f, 200, AntType::Worker);
        make_walk(f, ids);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{21, 21}, false, false);
        check(shaped(sink.log, sim::CommandType::GroupMove, 200) && ants_sent(sink.log) == by_distance(f, ids, 21, 21), "own hill: all 200 named in seven commands");
        check(f.hud.status_line().text().find("Only") == std::string::npos && f.hud.status_line().text().find("walk here") == std::string::npos, "and no line about a limit");
    }
    // 80 workers and a move click (the right button, by the cursor): three commands and every ant has the order
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 80, AntType::Worker);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.sounds.clear();
        f.click_tile(30, 30, SDL_BUTTON_RIGHT);
        check(shaped(sink.log, sim::CommandType::GroupMove, 80), "80 ants: 32, 32 and 16");
        size_t moving = 0;
        for (uint32_t id : ids) moving += f.unit(id).orig_order == sim::AntUnit::kOrderMove ? size_t{1} : size_t{0};
        check(moving == 80, "every one of the 80 ants carries the move order (" + std::to_string(moving) + ")");
        check(!f.sounds.empty() && f.hud.status_line().text().find("Only") == std::string::npos, "the click had its voice and sound, and says nothing of a limit");
    }
    // 200 WALKING workers and a move click: no cap of 81 ants. All 200 are named (seven commands: 32 six times and 8); the goal search of the engine gives a tile to the ants that find a free one within four
    // tiles of the clicked tile, and an ant that finds none STOPS where it stands: that is the original's logic, which the click does not change. The engine gets exactly the commands that a hand-built
    // list of the same chunks is: the same state at every one of the next ticks
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 200, AntType::Worker);
        make_walk(f, ids);
        Fixture g;
        const std::vector<uint32_t> same = spawn_own(g, 200, AntType::Worker);
        make_walk(g, same);
        check(same == ids && f.sim.state_hash() == g.sim.state_hash(), "the two worlds are the same before the click");
        std::vector<TileCoord> goal;
        for (uint32_t id : ids) goal.push_back(f.unit(id).orig_order_tile);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(30, 30, SDL_BUTTON_RIGHT);
        check(shaped(sink.log, sim::CommandType::GroupMove, 200) && ants_sent(sink.log) == by_distance(f, ids, 30, 30), "200 workers: seven commands name every ant, the nearest the clicked tile first");
        size_t new_goal = 0, stopped = 0, kept = 0;
        for (size_t i = 0; i < ids.size(); ++i) {
            const sim::AntUnit& u = f.unit(ids[i]);
            new_goal += u.orig_order == sim::AntUnit::kOrderMove && u.orig_order_tile != goal[i] ? size_t{1} : size_t{0};
            stopped += u.orig_order == sim::AntUnit::kOrderNone ? size_t{1} : size_t{0};
            kept += u.orig_order == sim::AntUnit::kOrderMove && u.orig_order_tile == goal[i] ? size_t{1} : size_t{0};
        }
        check(new_goal > 0 && new_goal <= 81 && stopped == 200 - new_goal && kept == 0, "the engine's result: " + std::to_string(new_goal) + " take a tile around the click (at most 81), the other " + std::to_string(stopped) + " stop, none is left alone");
        for (const sim::Command& c : hand_chunks(sim::CommandType::GroupMove, 30, 30, by_distance(g, same, 30, 30))) g.sim.apply_command(c);
        bool equal = f.sim.state_hash() == g.sim.state_hash();
        for (int t = 0; t < 40 && equal; ++t) {
            f.settle();
            g.settle();
            equal = f.sim.state_hash() == g.sim.state_hash();
        }
        check(equal, "the HUD's commands do to the engine what the hand-built chunks do, at every tick");
        check(f.hud.status_line().text().find("Only") == std::string::npos, "and the line says nothing of a limit");
    }
    // an attack order has no limit either: 100 walking combat ants all take the attack on an enemy worker, four commands name all 100
    {
        Fixture g;
        const std::vector<uint32_t> fighters = spawn_own(g, 100, AntType::Combat);
        make_walk(g, fighters);
        g.spawn(1, AntType::Worker, 30, 30);
        g.settle();
        RecordingSink gsink(g.sim);
        g.hud.set_command_sink(&gsink);
        select_all(g);
        g.hud.order_selected(g.sim, TileCoord{30, 30}, false, true);
        size_t attacking = 0;
        for (uint32_t id : fighters) attacking += g.unit(id).orig_order == sim::AntUnit::kOrderAttack ? size_t{1} : size_t{0};
        check(shaped(gsink.log, sim::CommandType::GroupAttack, 100) && ants_sent(gsink.log) == by_distance(g, fighters, 30, 30) && attacking == 100, "100 walking combat ants: four attack commands name all 100 and every ant attacks (" + std::to_string(attacking) + ")");
    }
    // at most 32 ants: one command, byte for byte the one that was always sent (a move, a special order, an attack and Stop)
    for (size_t n : {size_t{1}, size_t{10}, size_t{32}}) {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, n, AntType::Worker);
        f.spawn(1, AntType::Worker, 40, 40);
        f.settle();
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(30, 30, SDL_BUTTON_RIGHT);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(make_cmd(sim::CommandType::GroupMove, 30, 30, ids)), std::to_string(n) + " ants: one command with the bytes of the old click");
        sink.log.clear();
        f.hud.order_selected(f.sim, TileCoord{31, 30}, true, false);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(old_special(ids, 31, 30)), std::to_string(n) + " ants: a special order is one command with the bytes of the old one");
        sink.log.clear();
        f.hud.order_selected(f.sim, TileCoord{40, 40}, false, true);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(make_cmd(sim::CommandType::GroupAttack, 40, 40, ids)), std::to_string(n) + " ants: an attack is one command with the bytes of the old one");
        sink.log.clear();
        press_stop(f);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(make_cmd(sim::CommandType::Stop, 0, 0, ids)), std::to_string(n) + " ants: Stop is one command with the bytes of the old one");
    }
    // 33 ants: 32 and 1 for a move, an attack and Stop
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 33, AntType::Worker);
        f.spawn(1, AntType::Worker, 40, 40);
        f.settle();
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(30, 30, SDL_BUTTON_RIGHT);
        check(shaped(sink.log, sim::CommandType::GroupMove, 33), "33 ants: a command of 32 and a command of 1");
        const auto paths_pending = [&]() {
            for (uint32_t id : ids) if (f.sim.has_pending_path(id)) return true;
            return false;
        };
        for (int t = 0; t < 400 && paths_pending(); ++t) f.settle();       // (the paths come in: the ants walk, and a walking ant can be stopped, one that waits for its path cannot)
        for (int t = 0; t < 5; ++t) f.settle();
        std::vector<std::pair<int32_t, int32_t>> at;
        for (uint32_t id : ids) at.emplace_back(f.unit(id).pixel_x, f.unit(id).pixel_y);
        sink.log.clear();
        press_stop(f);
        for (int t = 0; t < 40; ++t) f.settle();                    // (a walking ant covers 4 px a tick: one that was not stopped would be 160 px on)
        size_t stopped = 0;
        for (size_t i = 0; i < ids.size(); ++i) stopped += std::abs(f.unit(ids[i]).pixel_x - at[i].first) <= 32 && std::abs(f.unit(ids[i]).pixel_y - at[i].second) <= 32 ? size_t{1} : size_t{0};
        check(shaped(sink.log, sim::CommandType::Stop, 33) && stopped == 33, "Stop names all 33 ants in two commands and every one of them stops (" + std::to_string(stopped) + ")");
        check(f.hud.status_line().text() == "Stopping.", "with its own line");
        sink.log.clear();
        f.hud.order_selected(f.sim, TileCoord{40, 40}, false, true);
        check(shaped(sink.log, sim::CommandType::GroupAttack, 33), "an attack order is split into attack commands");
    }
    // a SPECIAL order is never chunked: of 1, 10, 32, 33 and 200 selected ants it is ONE command with the first 32 (33 and 200: exactly the first 32), the bytes of the old command, and the unnamed ants are not touched
    for (size_t n : {size_t{1}, size_t{10}, size_t{32}, size_t{33}, size_t{200}}) {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, n, AntType::Bomber);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{31, 30}, true, false);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(old_special(ids, 31, 30)), std::to_string(n) + " ants: ONE special command with the bytes of the old one (the first " + std::to_string(std::min(n, size_t{32})) + ")");
        size_t touched = 0;
        for (size_t i = 32; i < ids.size(); ++i) touched += f.unit(ids[i]).orig_order != sim::AntUnit::kOrderNone ? size_t{1} : size_t{0};
        check(touched == 0, std::to_string(n) + " ants: the ants beyond the 32 are not touched");
        check(f.hud.status_line().text().find("Only") == std::string::npos, std::to_string(n) + " ants: no line about a limit (a special order is the one-at-a-time order, not a click that left ants out)");
        // the voice: a special order speaks only when exactly one ant needed it (the old rule, in the one command)
        check(n == 1 ? f.sounds.size() == 2 : f.sounds.size() == 1, std::to_string(n) + " ants: " + (n == 1 ? "the voice and the click sound" : "the click sound only, no voice"));
    }
    // the same on the scripted sink: the answers of the one command decide the feedback, whatever the other ants of the selection are
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 200, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        const uint32_t ability = sim::get_ability_voice_sound(AntType::Bomber);
        f.sounds.clear();
        sink.answers = {answer(1, ids[3])};
        check(f.hud.order_selected(f.sim, TileCoord{31, 30}, true, false) == ids[3] && sink.log.size() == 1 && sink.log[0].ants.size() == 32, "200 bombers, one of the first 32 needs the order: ONE command of 32, and the answer is that ant's");
        check(f.sounds.size() == 2 && f.sounds[0] == ability, "that one ant speaks (the old rule), then the click");
        sink.log.clear();
        sink.answers = {answer(0, 0)};
        f.sounds.clear();
        check(f.hud.order_selected(f.sim, TileCoord{31, 30}, true, false) == 0 && sink.log.size() == 1 && f.sounds.empty(), "no ant of the first 32 needs it: one command, nothing happens");
    }
    // the budget: 1,600 thieves make 48 commands of the first 1,536 for a move; the rest are not ordered and the status line says so; an attack and Stop have the same budget
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const std::vector<uint32_t> ids = spawn_own(f, 1600, AntType::Thief);
        check(ids.size() == 1600, "the world holds 1,600 own ants");
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{41, 41}, false, false);
        check(HUD::kMaxCommandsPerClick == 48 && HUD::kMaxAntsPerClick == 1536, "the budget is 48 commands of 32 ants");
        const std::vector<uint32_t> nearest = by_distance(f, ids, 41, 41);
        check(sink.log.size() == 48 && ants_sent(sink.log) == std::vector<uint32_t>(nearest.begin(), nearest.begin() + 1536), "a click sends 48 commands naming the 1,536 ants nearest the clicked tile, nearest first");
        size_t ordered = 0;
        bool nearest_only = true;
        for (size_t i = 0; i < nearest.size(); ++i) {
            const bool has = f.unit(nearest[i]).orig_order == sim::AntUnit::kOrderRaid;
            ordered += has ? size_t{1} : size_t{0};
            nearest_only = nearest_only && has == (i < 1536);
        }
        check(ordered == 1536 && nearest_only, "the 1,536 nearest ants carry the order and no other: the 64 farthest are left out (" + std::to_string(ordered) + ")");
        const std::string said = f.hud.status_line().text();
        check(said == "Only 1536 ants ordered.", "the status line says how many were ordered: '" + said + "'");
        check(fits_status_box(said), "and it fits the status box (139 px: the box clips at 139 px, the shipped font needs about 112 for this one)");
        sink.log.clear();
        press_stop(f);
        check(sink.log.size() == 48 && ants_in(sink.log) == 1536 && f.hud.status_line().text() == "Only 1536 ants ordered.", "Stop: 48 commands, 1,536 ants and the same line");
    }
    // the attack's budget: 1,600 combat ants, an attack on an enemy worker: 48 commands, the same line
    {
        Fixture f;
        const std::vector<uint32_t> fighters = spawn_own(f, 1600, AntType::Combat);
        f.spawn(1, AntType::Worker, 58, 58);
        f.settle();
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{58, 58}, false, true);
        size_t sent = 0;
        for (const auto& c : sink.log) sent += c.type == sim::CommandType::GroupAttack ? c.ants.size() : size_t{0};
        check(sink.log.size() == 48 && sent == 1536 && f.hud.status_line().text() == "Only 1536 ants ordered.", "an attack of 1,600 ants: 48 commands, 1,536 ants, the same line");
        const std::vector<uint32_t> nearest = by_distance(f, fighters, 58, 58);
        check(ants_sent(sink.log) == std::vector<uint32_t>(nearest.begin(), nearest.begin() + 1536), "an attack of 1,600 ants names the 1,536 nearest the target, nearest first: the 64 farthest are left out");
    }
    // the same for a move by the pointer (a right click): 1,600 workers, the click on free ground below them
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 1600, AntType::Worker);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(5, 40, SDL_BUTTON_RIGHT);
        const std::vector<uint32_t> nearest = by_distance(f, ids, 5, 40);
        check(sink.log.size() == 48 && ants_sent(sink.log) == std::vector<uint32_t>(nearest.begin(), nearest.begin() + 1536), "a right click with 1,600 ants: the 1,536 nearest the clicked tile, nearest first; the 64 farthest are left out");
    }
    // a SPECIAL order of 1,600 ants is one command of 32 and no line (it is not a click that left ants out: the one-at-a-time order)
    {
        Fixture f;
        spawn_own(f, 1600, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{30, 30}, true, false);
        check(sink.log.size() == 1 && sink.log[0].ants.size() == 32 && f.hud.status_line().text().find("Only") == std::string::npos, "a special order of 1,600 ants: one command of 32, no line");
    }
    // the budget is exact: 1,536 ants are all sent and the line stays silent
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        spawn_own(f, 1536, AntType::Thief);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{41, 41}, false, false);
        check(sink.log.size() == 48 && ants_in(sink.log) == 1536 && f.hud.status_line().text().find("Only") == std::string::npos, "1,536 ants: 48 commands, nothing left out, no line about a limit");
    }
    // a click that no ant needs (every command says so) still tells that ants were left out: 1,600 workers, the sink answers "none needs an order"
    {
        Fixture f;
        spawn_own(f, 1600, AntType::Worker);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{30, 30}, false, false);
        check(sink.log.size() == 48 && f.sounds.empty() && f.hud.status_line().text() == "Only 1536 ants ordered.", "nothing needed an order: no voice, no sound, and the line says that 1,536 were ordered");
    }
    // a move of 100 ants that nothing needs: four commands and no line (nothing was left out)
    {
        Fixture f;
        spawn_own(f, 100, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{30, 30}, false, false);
        check(shaped(sink.log, sim::CommandType::GroupMove, 100) && f.sounds.empty() && f.hud.status_line().text().find("Only") == std::string::npos, "a move of 100 ants that nothing needs: all 100 named in four commands, silent, no line");
    }
    // a held Stop key is not a stream of orders: the repeats of the key do nothing (a Stop of 1,500 ants is 47 commands, and 20 a second of them would fill the sequencer's carry and cost the player its seat)
    {
        Fixture f;
        spawn_own(f, 1500, AntType::Worker);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        for (int repeats = 0; repeats < 30; ++repeats) f.hud.handle_key_down('s', f.sim, f.cam, KMOD_CTRL, true);
        check(sink.log.empty(), "30 repeats of the held key send nothing");
        f.hud.handle_key_down('s', f.sim, f.cam, KMOD_CTRL, false);
        check(sink.log.size() == 47 && ants_in(sink.log) == 1500, "the press itself sends the Stop: 47 commands, 1,500 ants");
        sink.log.clear();
        f.hud.handle_key_down('S', f.sim, f.cam, KMOD_CTRL, true);
        check(sink.log.empty(), "also with a capital S");
    }
    // the feedback follows the click as a whole, not its first command: answers are scripted (65 bombers: commands of 32, 32 and 1 ants)
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 65, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        f.hud.set_selected_ant_ids(ids);
        const TileCoord tile{30, 30};
        const auto click = [&](bool attack, std::vector<sim::CommandResult> answers) {
            sink.log.clear();
            sink.answers = std::move(answers);
            f.sounds.clear();
            return f.hud.order_selected(f.sim, tile, false, attack);
        };
        check(click(false, {answer(0, 0), answer(1, ids[40]), answer(0, 0)}) == ids[40] && sink.log.size() == 3 && f.sounds.size() == 2, "a move that one ant needs, in the second command: the voice of that ant and the click");
        check(click(false, {answer(1, ids[0]), answer(1, ids[40]), answer(0, 0)}) == ids[0] && f.sounds.size() == 2, "two ants need it, one in each command: the voice is the first command's");
        check(click(false, {answer(0, 0), answer(0, 0), answer(0, 0)}) == 0 && sink.log.size() == 3 && f.sounds.empty(), "no ant needs an order in any command: the click does nothing, and all three were sent");
        check(click(false, {answer(2, 0), answer(5, ids[40]), answer(0, 0)}) == 0 && f.sounds.size() == 1, "the first command with ants in need is refused (no ack): no voice, the click sound only");
        check(click(false, {answer(0, 0), answer(5, ids[40]), answer(3, ids[64])}) == ids[40] && f.sounds.size() == 2, "a move: the voice is that of the first command with ants in need");
        check(click(true, {answer(0, 0), answer(5, ids[40]), answer(3, ids[64])}) == ids[40] && sink.log.size() == 3 && f.sounds.size() == 2, "an attack of 65 ants: three commands, the voice of the first command with ants in need");
    }
}

namespace {

sim::AntSnapshot snap(uint32_t id, int32_t px, int32_t py) {
    sim::AntSnapshot a{};
    a.id = id;
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    return a;
}

// A world of `n` ants with ids 100.. at random pixels of a 60 x 60 tile map, and a random order of (some of) them as the selection
struct RandomGroup {
    std::vector<sim::AntSnapshot> world;
    std::vector<uint32_t> selection;
};

RandomGroup random_group(uint32_t seed, size_t n, int32_t span_tiles) {
    uint32_t state = seed * 2654435761u + 12345u;
    const auto next = [&state](uint32_t bound) {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) % bound;
    };
    RandomGroup g;
    for (size_t i = 0; i < n; ++i) g.world.push_back(snap(static_cast<uint32_t>(100 + i), static_cast<int32_t>(next(static_cast<uint32_t>(span_tiles * 32))), static_cast<int32_t>(next(static_cast<uint32_t>(span_tiles * 32)))));
    for (const auto& a : g.world) g.selection.push_back(a.id);
    for (size_t i = g.selection.size(); i > 1; --i) std::swap(g.selection[i - 1], g.selection[next(static_cast<uint32_t>(i))]);
    return g;
}

uint32_t world_key(const std::vector<sim::AntSnapshot>& world, uint32_t id, TileCoord t) {
    for (const auto& a : world) {
        if (a.id == id) return static_cast<uint32_t>(std::max(std::abs(a.py / 32 - t.y), std::abs(a.px / 32 - t.x))) * 16u;
    }
    return UINT32_MAX;
}

}  // namespace

void test_nearest_first() {
    g_group = "nearest first";
    std::printf("[pointer] a move or an attack on a big selection names the ants nearest the clicked tile first across the whole selection (stable, the engine's key); at most 32 ants, a special order and Stop are as they were\n");
    // the key is the engine's: 16 x Chebyshev of the tiles of the pixel positions
    check(group_order_distance(0, 0, TileCoord{0, 0}) == 0 && group_order_distance(31, 31, TileCoord{0, 0}) == 0, "the key of an ant on the clicked tile is 0, wherever it is in the tile");
    check(group_order_distance(32, 0, TileCoord{0, 0}) == 16 && group_order_distance(63, 0, TileCoord{0, 0}) == 16 && group_order_distance(64, 0, TileCoord{0, 0}) == 32, "16 per tile, by the tile of the pixel (pixel / 32)");
    check(group_order_distance(96, 96, TileCoord{0, 0}) == 48 && group_order_distance(96, 160, TileCoord{0, 0}) == 80 && group_order_distance(160, 96, TileCoord{0, 0}) == 80, "the larger of the two distances (Chebyshev), not their sum or their root");
    check(group_order_distance(0, 0, TileCoord{3, 5}) == 80 && group_order_distance(10 * 32, 9 * 32, TileCoord{3, 5}) == 112, "the clicked tile can be anywhere");
    // Chebyshev, not Euclid: 33 ants in a row at distance 4 and one on the diagonal at (3, 3), which is farther by Euclid (4.2) and nearer by Chebyshev (3)
    {
        std::vector<sim::AntSnapshot> world;
        std::vector<uint32_t> sel;
        for (uint32_t i = 0; i < 33; ++i) {
            world.push_back(snap(i + 1, (10 + 4) * 32, 10 * 32));          // (14, 10): 4 east of the click at (10, 10)
            sel.push_back(i + 1);
        }
        world.push_back(snap(500, 13 * 32, 13 * 32));                       // (13, 13): 3 and 3
        sel.push_back(500);
        const std::vector<uint32_t> out = nearest_first(sel, world, TileCoord{10, 10});
        check(out.size() == 34 && out[0] == 500, "the ant at (3, 3) is nearer than the ones at 4 (Chebyshev)");
    }
    // the position is the pixel position, whatever the snapshot's tile says (the engine reads pixel / 32)
    {
        std::vector<sim::AntSnapshot> world;
        std::vector<uint32_t> sel;
        for (uint32_t i = 0; i < 40; ++i) {
            world.push_back(snap(i + 1, 40 * 32, 40 * 32));
            sel.push_back(i + 1);
        }
        world[39] = snap(40, 11 * 32, 10 * 32);
        world[39].tile_x = 50;                                              // a tile that disagrees with the pixels
        world[39].tile_y = 50;
        check(nearest_first(sel, world, TileCoord{10, 10})[0] == 40, "the ant is placed by its pixels");
    }
    // at most 32 ants: returned as they are, whatever their distances (random groups of 0 .. 32 ants)
    {
        bool same = true;
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            const size_t n = seed % 33;
            const RandomGroup g = random_group(seed, n, 60);
            const TileCoord t{static_cast<int32_t>(seed % 60), static_cast<int32_t>((seed * 7) % 60)};
            same = same && nearest_first(g.selection, g.world, t) == g.selection;
        }
        check(same, "300 random groups of 0 .. 32 ants: the list comes back as it was");
    }
    // 33 ants and more: a permutation, nearest first, ties in the selection order (random groups; few distinct distances on a small map give many ties)
    {
        bool permutation = true, ascending = true, ties = true, unchanged_input = true;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            const size_t n = 33 + seed * 7 % 400;
            const RandomGroup g = random_group(seed, n, seed % 2 == 0 ? 8 : 60);
            const TileCoord t{static_cast<int32_t>(seed % 8), static_cast<int32_t>((seed * 3) % 8)};
            const std::vector<uint32_t> before = g.selection;
            const std::vector<uint32_t> out = nearest_first(g.selection, g.world, t);
            unchanged_input = unchanged_input && g.selection == before;
            std::vector<uint32_t> a = out, b = g.selection;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            permutation = permutation && a == b;
            std::vector<size_t> rank(1000, 0);
            for (size_t i = 0; i < g.selection.size(); ++i) rank[g.selection[i] - 100] = i;
            for (size_t i = 1; i < out.size(); ++i) {
                const uint32_t k0 = world_key(g.world, out[i - 1], t), k1 = world_key(g.world, out[i], t);
                ascending = ascending && k0 <= k1;
                ties = ties && (k0 != k1 || rank[out[i - 1] - 100] < rank[out[i] - 100]);
            }
        }
        check(permutation && unchanged_input, "200 random selections of 33 .. 439 ants: the same ants once each, the input untouched");
        check(ascending, "and never a farther ant before a nearer one");
        check(ties, "ants at the same distance keep the selection order (a stable sort)");
    }
    // an id that the world does not hold goes last
    {
        std::vector<sim::AntSnapshot> world;
        std::vector<uint32_t> sel{999};
        for (uint32_t i = 0; i < 40; ++i) {
            world.push_back(snap(i + 1, static_cast<int32_t>(30 + i % 5) * 32, 30 * 32));
            sel.push_back(i + 1);
        }
        const std::vector<uint32_t> out = nearest_first(sel, world, TileCoord{30, 30});
        check(out.size() == 41 && out.back() == 999, "an ant that is not in the world is named last");
    }
    // a click on a big selection through the HUD: the whole command list is nearest first, and so is each block
    for (const bool attack : {false, true}) {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 600, attack ? AntType::Combat : AntType::Worker);
        f.spawn(1, AntType::Worker, 30, 17);
        f.settle();
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{30, 17}, false, attack);
        const sim::CommandType type = attack ? sim::CommandType::GroupAttack : sim::CommandType::GroupMove;
        const std::string what = attack ? "attack" : "move";
        check(shaped(sink.log, type, 600), "600 ants, a " + what + ": eighteen commands of 32 and one of 24");
        const std::vector<uint32_t> sent = ants_sent(sink.log);
        check(sent == by_distance(f, ids, 30, 17), "a " + what + ": the commands name the ants in the order of their distance to the clicked tile, from the first ant of the first command to the last of the last");
        bool global = true, per_block = true;
        for (size_t i = 1; i < sent.size(); ++i) global = global && key_of(f, sent[i - 1], 30, 17) <= key_of(f, sent[i], 30, 17);
        for (const auto& c : sink.log) {
            for (size_t i = 1; i < c.ants.size(); ++i) per_block = per_block && key_of(f, c.ants[i - 1], 30, 17) <= key_of(f, c.ants[i], 30, 17);
        }
        check(global && per_block, "a " + what + ": nearest first across the whole list and inside each command");
        // the 50 ants of the last row (one tile from the click) were the last ants of the selection: the old order named them in the last two commands
        check(key_of(f, sink.log[0].ants[0], 30, 17) == 16 && key_of(f, sink.log.back().ants.back(), 30, 17) > key_of(f, sink.log[0].ants[31], 30, 17), "the nearest ant leads the first command and the farthest ends the last");
        check(sink.log[0].ants != std::vector<uint32_t>(ids.begin(), ids.begin() + 32), "the first command is no longer the first 32 selected ants");
        check(sink.log.size() == 19 && std::all_of(sink.log.begin(), sink.log.end(), [&](const sim::Command& c) { return c.tile_x == 30 && c.tile_y == 17 && c.issuer == 0; }), "every command has the clicked tile and the issuer");
    }
    // the same through the pointer (a right click on the ground, which a touch tap and the keyboard's order go through as well)
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 300, AntType::Worker);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(8, 50, SDL_BUTTON_RIGHT);
        check(shaped(sink.log, sim::CommandType::GroupMove, 300) && ants_sent(sink.log) == by_distance(f, ids, 8, 50), "a right click: nearest the clicked tile first");
    }
    // at most 32 ants through the HUD, random groups in a random order, move and attack: ONE command with the ants exactly in the selection order (the bytes that were always sent)
    {
        Fixture f;
        const std::vector<uint32_t> all = spawn_own(f, 120, AntType::Worker);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        bool identical = true;
        for (uint32_t seed = 1; seed <= 120; ++seed) {
            const RandomGroup g = random_group(seed, all.size(), 4);
            const size_t n = 1 + seed % 32;
            std::vector<uint32_t> group;
            for (size_t i = 0; i < n; ++i) group.push_back(all[g.selection[i] - 100]);
            f.hud.set_selected_ant_ids(group);
            const int16_t tx = static_cast<int16_t>(5 + seed % 50), ty = static_cast<int16_t>(5 + (seed * 11) % 50);
            for (const bool attack : {false, true}) {
                sink.log.clear();
                f.hud.order_selected(f.sim, TileCoord{tx, ty}, false, attack);
                identical = identical && sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(make_cmd(attack ? sim::CommandType::GroupAttack : sim::CommandType::GroupMove, tx, ty, group));
            }
        }
        check(identical, "120 random groups of 1 .. 32 ants, a move and an attack each: one command with the bytes of the old click");
    }
    // a special order is the first 32 SELECTED ants, not the nearest (the click is on the far end of the rows), and Stop names the selection in its own order
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 300, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{30, 54}, true, false);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(old_special(ids, 30, 54)), "300 bombers, a special order far from the first ones: the first 32 selected ants, in the selection order");
        sink.log.clear();
        press_stop(f);
        check(sink.log.size() == 10 && ants_sent(sink.log) == ids, "Stop: the selection order, 300 ants in ten commands");
    }
    // the budget: 1,600 ants (rows 5 to 36), the click at the bottom left: the 64 left out are the 64 farthest (the top row), not the last 64 selected, and every ant that is ordered is nearer than every ant that is not
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const std::vector<uint32_t> ids = spawn_own(f, 1600, AntType::Thief);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{5, 36}, false, false);
        const std::vector<uint32_t> sent = ants_sent(sink.log);
        std::vector<uint32_t> left = ids;
        left.erase(std::remove_if(left.begin(), left.end(), [&](uint32_t id) { return std::find(sent.begin(), sent.end(), id) != sent.end(); }), left.end());
        uint32_t far_sent = 0, near_left = UINT32_MAX;
        for (uint32_t id : sent) far_sent = std::max(far_sent, key_of(f, id, 5, 36));
        for (uint32_t id : left) near_left = std::min(near_left, key_of(f, id, 5, 36));
        check(sent.size() == 1536 && left.size() == 64 && far_sent <= near_left, "1,536 ordered and 64 left out, and the ones left out are not nearer than any ordered ant");
        const std::vector<uint32_t> nearest = by_distance(f, ids, 5, 36);
        check(sent == std::vector<uint32_t>(nearest.begin(), nearest.begin() + 1536), "the ordered ants are the 1,536 nearest, nearest first");
        check(f.hud.status_line().text() == "Only 1536 ants ordered.", "and the line is the one it was");
    }
    // 600 walking workers, the click on the tile of one of them in the crowd: the ants at the click come before the ants ten tiles away, in whatever block they were
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 600, AntType::Worker);
        make_walk(f, ids);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        const int32_t tx = f.unit(ids[300]).pixel_x / 32, ty = f.unit(ids[300]).pixel_y / 32;
        f.hud.order_selected(f.sim, TileCoord{tx, ty}, false, false);
        const std::vector<uint32_t> sent = ants_sent(sink.log);
        size_t last_near = 0, first_far = sent.size();
        bool any_far = false;
        for (size_t i = 0; i < sent.size(); ++i) {
            const uint32_t k = key_of(f, sent[i], tx, ty);
            if (k <= 16) last_near = i;
            if (k >= 160 && !any_far) {
                any_far = true;
                first_far = i;
            }
        }
        check(any_far && last_near < first_far && sent == by_distance(f, ids, tx, ty), "the ants at the click come before the ants ten tiles away, in whatever block they were");
    }
}

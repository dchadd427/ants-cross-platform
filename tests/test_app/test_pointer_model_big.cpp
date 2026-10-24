// Pointer model tests: a click or Stop on more ants than one command holds (sim::kMaxCommandAnts = 32). Ctrl+A or a drag selects every own ant; the HUD sends the click as several
// commands of the same kind in the selection order (HUD::submit_in_chunks, docs/CONTROLS.md, "A big selection"), at most HUD::kMaxCommandsPerClick of them, and a click of at most 32 ants
// sends what it always sent. The voice and the pedestal feedback follow the click as a whole.
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

}  // namespace

void test_big_selection() {
    g_group = "big selection";
    std::printf("[pointer] a click or Stop on more ants than a command holds goes out as several commands in the selection order, up to the click's budget; a click of at most 32 ants sends what it always sent\n");
    // 100 thieves, Ctrl+A, a special click on the enemy hill: four commands of 32, 32, 32 and 4 ants, and every thief has the raid order (a raid takes no tile of its own: the probe of
    // tests/test_app/test_pointer_model_big.cpp, and of the HUD's budget, is that a special or attack order has no limit but the 48 commands)
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const std::vector<uint32_t> ids = spawn_own(f, 100, AntType::Thief);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        check(f.hud.get_selected_ant_ids() == ids, "Ctrl+A selects all 100 own ants in the order of the world");
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{41, 41}, true, false);
        check(shaped(sink.log, sim::CommandType::GroupSpecial, 100), "100 ants: four special orders, 32 ants but the last (4)");
        check(!sink.log.empty() && sink.log[0].tile_x == 41 && sink.log[0].tile_y == 41, "of the clicked tile");
        check(ants_sent(sink.log) == ids, "together they name every selected ant once, in the selection order");
        size_t raiding = 0;
        for (uint32_t id : ids) raiding += f.unit(id).orig_order == sim::AntUnit::kOrderRaid ? size_t{1} : size_t{0};
        check(raiding == 100, "every one of the 100 ants carries the order (" + std::to_string(raiding) + ")");
        // the feedback is the click's, once: the sounds of a click on 30 ants
        const size_t big = f.sounds.size();
        Fixture g;
        g.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        spawn_own(g, 30, AntType::Thief);
        select_all(g);
        g.sounds.clear();
        g.hud.order_selected(g.sim, TileCoord{41, 41}, true, false);
        check(big != 0 && big == g.sounds.size(), "the voice and the click sound are heard once, as for 30 ants (" + std::to_string(big) + " and " + std::to_string(g.sounds.size()) + " sounds)");
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
    // a move click names at most 81 ants (HUD::kMaxAntsPerMove): the goal search gives a tile to 81 at the most and an ant that finds none STOPS, so 100 WALKING workers: three commands (32, 32, 17),
    // the first 81 take the new goals, the other 19 are not named and keep walking where they walked
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 100, AntType::Worker);
        make_walk(f, ids);
        std::vector<TileCoord> goal;
        for (uint32_t id : ids) goal.push_back(f.unit(id).orig_order_tile);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(30, 30, SDL_BUTTON_RIGHT);
        check(shaped(sink.log, sim::CommandType::GroupMove, 81) && ants_sent(sink.log) == std::vector<uint32_t>(ids.begin(), ids.begin() + 81), "100 workers: three commands name the first 81");
        size_t new_goal = 0, kept = 0;
        for (size_t i = 0; i < ids.size(); ++i) {
            const sim::AntUnit& u = f.unit(ids[i]);
            new_goal += i < 81 && u.orig_order == sim::AntUnit::kOrderMove && u.orig_order_tile != goal[i] ? size_t{1} : size_t{0};
            kept += i >= 81 && u.orig_order == sim::AntUnit::kOrderMove && u.orig_order_tile == goal[i] ? size_t{1} : size_t{0};
        }
        check(new_goal == 81, "the 81 take a tile around the click (" + std::to_string(new_goal) + ")");
        check(kept == 19, "the 19 that were not named keep walking to their own goals (" + std::to_string(kept) + ")");
        check(f.hud.status_line().text() == "Only 81 ants fit here.", "the status line says why: '" + f.hud.status_line().text() + "'");
        // an attack order and Stop have no such limit: 100 walking combat ants all take the attack on an enemy worker
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
        check(shaped(gsink.log, sim::CommandType::GroupAttack, 100) && attacking == 100, "100 walking combat ants: four attack commands and every ant attacks (" + std::to_string(attacking) + ")");
    }
    // at most 32 ants: one command, byte for byte the one that was always sent (a move and Stop)
    for (size_t n : {size_t{1}, size_t{10}, size_t{32}}) {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, n, AntType::Worker);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.click_tile(30, 30, SDL_BUTTON_RIGHT);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(make_cmd(sim::CommandType::GroupMove, 30, 30, ids)), std::to_string(n) + " ants: one command with the bytes of the old click");
        sink.log.clear();
        press_stop(f);
        check(sink.log.size() == 1 && bytes_of(sink.log[0]) == bytes_of(make_cmd(sim::CommandType::Stop, 0, 0, ids)), std::to_string(n) + " ants: Stop is one command with the bytes of the old one");
    }
    // 33 ants: 32 and 1; Stop, an attack and a special order are split the same way
    {
        Fixture f;
        const std::vector<uint32_t> ids = spawn_own(f, 33, AntType::Worker);
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
        sink.log.clear();
        f.hud.order_selected(f.sim, TileCoord{41, 40}, true, false);
        check(shaped(sink.log, sim::CommandType::GroupSpecial, 33), "a special order is split into special commands");
    }
    // the budget: 1,600 thieves make 48 commands of the first 1,536; the rest are not ordered and the status line says so; Stop has the same budget
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const std::vector<uint32_t> ids = spawn_own(f, 1600, AntType::Thief);
        check(ids.size() == 1600, "the world holds 1,600 own ants");
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{41, 41}, true, false);
        check(HUD::kMaxCommandsPerClick == 48 && HUD::kMaxAntsPerClick == 1536 && HUD::kMaxAntsPerMove == 81, "the budget is 48 commands of 32 ants, a move names 81");
        check(sink.log.size() == 48 && ants_sent(sink.log) == std::vector<uint32_t>(ids.begin(), ids.begin() + 1536), "a click sends 48 commands naming the first 1,536 ants in the selection order");
        size_t ordered = 0;
        bool prefix = true;
        for (size_t i = 0; i < ids.size(); ++i) {
            const bool has = f.unit(ids[i]).orig_order == sim::AntUnit::kOrderRaid;
            ordered += has ? size_t{1} : size_t{0};
            prefix = prefix && has == (i < 1536);
        }
        check(ordered == 1536 && prefix, "the first 1,536 ants carry the order and no other (" + std::to_string(ordered) + ")");
        const std::string said = f.hud.status_line().text();
        check(said == "Only 1536 ants ordered.", "the status line says how many were ordered: '" + said + "'");
        check(fits_status_box(said) && fits_status_box("Only 81 ants fit here."), "and both lines fit the status box (139 px: the box clips at 139 px, the shipped font needs about 112 for this one)");
        sink.log.clear();
        press_stop(f);
        check(sink.log.size() == 48 && ants_in(sink.log) == 1536 && f.hud.status_line().text() == "Only 1536 ants ordered.", "Stop: 48 commands, 1,536 ants and the same line");
    }
    // the budget is exact: 1,536 ants are all sent and the line stays silent
    {
        Fixture f;
        f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        spawn_own(f, 1536, AntType::Thief);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.hud.order_selected(f.sim, TileCoord{41, 41}, true, false);
        check(sink.log.size() == 48 && ants_in(sink.log) == 1536 && f.hud.status_line().text().find("Only") == std::string::npos, "1,536 ants: 48 commands, nothing left out, no line about a limit");
    }
    // a click that no ant needs (every command says so) still tells that ants were left out: 1,600 bombers, the sink answers "none needs an order"
    {
        Fixture f;
        spawn_own(f, 1600, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{30, 30}, true, false);
        check(sink.log.size() == 48 && f.sounds.empty() && f.hud.status_line().text() == "Only 1536 ants ordered.", "nothing needed an order: no voice, no sound, and the line says that 1,536 were ordered");
    }
    // the same for a move, whose limit is 81: 100 bombers, three commands, nothing needs an order, the line says why
    {
        Fixture f;
        spawn_own(f, 100, AntType::Bomber);
        ScriptedSink sink;
        f.hud.set_command_sink(&sink);
        select_all(f);
        f.sounds.clear();
        f.hud.order_selected(f.sim, TileCoord{30, 30}, false, false);
        check(shaped(sink.log, sim::CommandType::GroupMove, 81) && f.sounds.empty() && f.hud.status_line().text() == "Only 81 ants fit here.", "a move of 100 ants: 81 named in three commands, and the line says that only 81 fit");
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
        const uint32_t ability = sim::get_ability_voice_sound(AntType::Bomber);
        const TileCoord tile{30, 30};
        const auto click = [&](bool special, std::vector<sim::CommandResult> answers) {
            sink.log.clear();
            sink.answers = std::move(answers);
            f.sounds.clear();
            return f.hud.order_selected(f.sim, tile, special, false);
        };
        check(click(true, {answer(0, 0), answer(1, ids[40]), answer(0, 0)}) == ids[40] && sink.log.size() == 3 && f.sounds.size() == 2 && f.sounds[0] == ability,
              "a special order that one ant needs, in the second command: the voice of that ant and the click");
        check(click(true, {answer(1, ids[0]), answer(1, ids[40]), answer(0, 0)}) == ids[0] && f.sounds.size() == 1, "two ants need it, one in each command: no voice, only the click");
        check(click(false, {answer(0, 0), answer(0, 0), answer(0, 0)}) == 0 && sink.log.size() == 3 && f.sounds.empty(), "no ant needs an order in any command: the click does nothing, and all three were sent");
        check(click(false, {answer(2, 0), answer(5, ids[40]), answer(0, 0)}) == 0 && f.sounds.size() == 1, "the first command with ants in need is refused (no ack): no voice, the click sound only");
        check(click(false, {answer(0, 0), answer(5, ids[40]), answer(3, ids[64])}) == ids[40] && f.sounds.size() == 2, "a move: the voice is that of the first command with ants in need");
    }
}

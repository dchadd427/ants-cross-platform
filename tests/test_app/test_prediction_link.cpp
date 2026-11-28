// Tests of the prediction of one's own orders through the REAL input path over links with a delay and jitter (include/ants_net/prediction.hpp, docs/NETWORK_PORT.md "Prediction of one's own orders").
//
// What a person feels in a room at a round trip of 30, 100, 180 or 300 ms is not what the engine can say about an order: the order of a click lands `lag` ticks after the screen that the click was aimed
// at, and two plays of the original game depend on that: "humping" (a re-order of a walking ant a moment after it crosses a tile line, which pays on mud inside a window of about 150 ms) and the
// attack click on a walking ant (the order names the ant's tile, and an ant that has left it is no longer the target). Here a headless Application plays a room of a real RoomManager over links of a
// chosen round trip and jitter on a virtual clock (tests/test_app/shaped_link_fixtures.hpp), and a person at it clicks with SDL mouse events, deciding from what the screen shows (view_sim), with
// the prediction on and off:
//   - humping on mud: the pixels that the re-orders add to a walk of 9 s (the confirmed engine, the one that decides), against the same walk with one order;
//   - the attack click on an enemy ant that walks: the share of the clicks that are still an attack when the order lands;
//   - the hop of a re-order: the engine puts a walking ant on the middle of its tile when it is given a new order, and the picture draws no jump of it, in a game of one machine and over a link.
// The numbers are printed (ANTS_LINK_TABLE=1 plays the whole grid); the assertions hold what the prediction promises.
#include <SDL.h>

#include "shaped_link_fixtures.hpp"
#include "slide_cause.hpp"
#include "ants_app/health_changes.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "../common/test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ants;
using namespace ants::app;
using namespace shaped;

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

namespace {

// ANTS_LINK_SEED=n: the printed grids are played with other seeds (the links' jitter, the frames' and the person's phases), to see how much a number depends on them
uint32_t seed_offset() {
    const char* v = std::getenv("ANTS_LINK_SEED");
    return v != nullptr ? static_cast<uint32_t>(std::atoi(v)) * 1000u : 0u;
}

bool table_wanted() {
    const char* v = std::getenv("ANTS_LINK_TABLE");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

// What the person's hand has to do at a moment: a click at the first frame that starts at or after `at`
struct Due {
    uint32_t at;
    uint8_t button;
    int32_t sx;
    int32_t sy;
};

class Hand {
public:
    explicit Hand(Session& s) : s_(s) {
        s_.on_frame_start = [this]() {
            while (!queue_.empty() && queue_.front().at <= s_.now()) {
                const Due d = queue_.front();
                queue_.pop_front();
                s_.click(d.button, d.sx, d.sy);
            }
        };
    }
    ~Hand() {
        s_.on_frame_start = nullptr;
        s_.on_frame_end = nullptr;
    }
    void at(uint32_t when, uint8_t button, int32_t sx, int32_t sy) { queue_.push_back(Due{when, button, sx, sy}); }
    bool idle() const { return queue_.empty(); }

private:
    Session& s_;
    std::deque<Due> queue_;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// A match that is played by two people at the mouse and the keys, and what it leaves behind
// ---------------------------------------------------------------------------------------------------------------------------------

// The application's person clicks about: now and then he selects an ant with the mouse and sends it somewhere on the screen with the right button; the other person gives orders to ants of his own
// (the bare machine's own orders). Both go on for `ms`.
struct Players {
    Session& s;
    uint32_t next_person{0};
    uint32_t next_peer{0};
    uint32_t rounds{0};
    bool person{true};
    bool peer{true};

    explicit Players(Session& session) : s(session) {}

    void play(uint32_t ms) {
        const uint32_t end = s.now() + ms;
        while (s.now() < end) {
            if (person && s.now() >= next_person) {
                next_person = s.now() + 450 + static_cast<uint32_t>(s.rng()() % 400u);
                person_clicks();
            }
            if (peer && s.now() >= next_peer) {
                next_peer = s.now() + 600 + static_cast<uint32_t>(s.rng()() % 500u);
                peer_orders();
            }
            s.run_ms(10);
        }
    }

    void person_clicks() {
        const std::vector<uint32_t> mine = Session::ants_of(s.app().view_sim(), 0);
        if (mine.empty()) return;
        const uint32_t ant = mine[s.rng()() % mine.size()];
        const sim::AntSnapshot* found = s.shown_ant(ant);
        if (found == nullptr) return;
        const sim::AntSnapshot shown = *found;                                              // (a copy: the engine's snapshot is rewritten when the engine moves on)
        s.camera_on(shown.tile_x, shown.tile_y);
        int32_t sx = 0;
        int32_t sy = 0;
        if (!s.screen_of_world(shown.px, shown.py, sx, sy)) return;
        s.click(SDL_BUTTON_LEFT, sx, sy);
        if (s.app().hud().get_selected_ant_id() != ant) return;
        const int32_t tx = std::clamp(shown.tile_x + static_cast<int32_t>(s.rng()() % 13u) - 6, 2, 28);
        const int32_t ty = std::clamp(shown.tile_y + static_cast<int32_t>(s.rng()() % 13u) - 6, 2, 28);
        if (!s.screen_of_tile(tx, ty, sx, sy)) return;
        s.click(SDL_BUTTON_RIGHT, sx, sy);
        ++rounds;
    }

    void peer_orders() {
        for (size_t k = 0; k < s.peer_count(); ++k) {
            const uint8_t seat = static_cast<uint8_t>(k + 1);
            const std::vector<uint32_t> theirs = Session::ants_of(s.app().sim(), seat);
            if (theirs.empty()) continue;
            std::vector<uint32_t> pick;
            for (size_t i = 0; i < theirs.size() && pick.size() < 3; ++i) pick.push_back(theirs[(i + s.rng()()) % theirs.size()]);
            s.order(seat, sim::CommandType::GroupMove, pick, 2 + static_cast<int32_t>(s.rng()() % 27u), 2 + static_cast<int32_t>(s.rng()() % 27u));
        }
    }
};

// The recording that the application made of the match plays out, on the engine alone, to the state in which the match ended
bool recording_plays_to(Session& s, std::string& why) {
    replay::Replay rep;
    const std::vector<uint8_t>& bytes = s.app().last_replay();
    if (bytes.empty() || !replay::decode(bytes.data(), bytes.size(), rep, why)) {
        if (why.empty()) why = "the application kept no recording";
        return false;
    }
    assets::LevelData level;
    if (!replay::load_map(rep.head, maps_folder().string(), level, why)) return false;
    const replay::Outcome outcome = replay::play(rep, level);
    if (!outcome.ok) {
        why = outcome.error;
        return false;
    }
    if (outcome.hash != s.app().sim().state_hash().total) {
        why = "the recording ends in another state than the match did";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Humping
// ---------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kStartTile = kMudFrom + 1;       // the ant starts the walk standing on tile 9 of the lane
constexpr int32_t kGoalA = kMudTo - 1;             // the two goals that the person alternates between: tiles 21 and 20 of the lane
constexpr int32_t kGoalB = kMudTo - 2;
constexpr uint32_t kWalkTicks = 180;               // 9 s

// The ant to its place for the next walk
bool return_to_start(Session& s, uint32_t ant) {
    const sim::AntSnapshot* c = s.confirmed_ant(ant);
    if (c == nullptr) return false;
    if (c->tile_x != kStartTile || c->tile_y != kLaneRow) {
        if (!s.bring(0, ant, kStartTile, kLaneRow, 40000)) return false;
    }
    return true;
}

struct WalkResult {
    bool ok{false};
    int px{0};              // the pixels east that the confirmed ant has walked at the confirmed tick that is 180 ticks after the tick it stood at when the first order was clicked
    int clicks{0};
};

// One walk of 9 s: select the ant with a click, order it far along the lane with a click, then (when `hump`) click the other of two goals `reaction_ms` after the shown ant has crossed a tile line
WalkResult walk(Session& s, uint32_t ant, bool hump, int reaction_ms, int noise_ms) {
    WalkResult out;
    if (!return_to_start(s, ant)) {
        if (table_wanted()) std::printf("    [walk: the ant did not get to its start]\n");
        return out;
    }
    s.camera_on(16, kLaneRow);
    int32_t sx = 0;
    int32_t sy = 0;
    const sim::AntSnapshot* found = s.shown_ant(ant);
    if (found == nullptr) return out;
    const sim::AntSnapshot shown = *found;                                                 // (a copy: the engine's snapshot is rewritten as time passes below)
    if (!s.screen_of_world(shown.px, shown.py, sx, sy)) {
        std::printf("    [walk: the ant is not on the screen (%d,%d)]\n", shown.px, shown.py);
        return out;
    }
    s.run_ms(static_cast<uint32_t>(s.rng()() % 50u));                              // (a phase of the tick grid and of the frames: every trial is another one)
    s.click(SDL_BUTTON_LEFT, sx, sy);
    if (s.app().hud().get_selected_ant_id() != ant) {
        std::printf("    [walk: the click did not select the ant: at (%d,%d), selected %u]\n", sx, sy, s.app().hud().get_selected_ant_id());
        return out;
    }
    Hand hand(s);
    int32_t gx = 0;
    int32_t gy = 0;
    if (!s.screen_of_tile(kGoalA, kLaneRow, gx, gy)) return out;
    int32_t hx = 0;
    int32_t hy = 0;
    if (!s.screen_of_tile(kGoalB, kLaneRow, hx, hy)) return out;
    const uint64_t tick0 = s.app().sim().current_tick();
    std::vector<std::pair<uint64_t, int>> history;                                 // (the confirmed tick, the ant's px) at the end of every frame
    int32_t last_tile = shown.tile_x;
    bool next_is_b = true;
    hand.at(s.now(), SDL_BUTTON_RIGHT, gx, gy);                                    // the first order, at the next frame
    const uint32_t start_now = s.now();
    s.on_frame_end = [&]() {
        if (const sim::AntSnapshot* c = s.confirmed_ant(ant)) history.emplace_back(s.app().sim().current_tick(), c->px);
        const sim::AntSnapshot* seen = s.shown_ant(ant);
        if (seen == nullptr) return;
        if (!hump) return;
        if (seen->tile_x != last_tile) {                                           // the shown ant has crossed a tile line: the person reacts
            if (seen->tile_x > last_tile && s.now() > start_now + 600) {
                hand.at(s.now() + static_cast<uint32_t>(std::max(0, reaction_ms + (noise_ms > 0 ? static_cast<int>(s.rng()() % static_cast<uint32_t>(2 * noise_ms + 1)) - noise_ms : 0))), SDL_BUTTON_RIGHT, next_is_b ? hx : gx, next_is_b ? hy : gy);
                next_is_b = !next_is_b;
            }
            last_tile = seen->tile_x;
        }
    };
    const bool done = s.run_until([&]() { return s.app().sim().current_tick() >= tick0 + kWalkTicks + 1; }, 15000);
    s.on_frame_end = nullptr;
    if (!done) {
        std::printf("    [walk: the walk did not end]\n");
        return out;
    }
    const uint64_t target = tick0 + kWalkTicks;
    for (size_t i = 1; i < history.size(); ++i) {
        if (history[i].first >= target && history[i - 1].first <= target) {
            const double span = static_cast<double>(history[i].first - history[i - 1].first);
            const double frac = span > 0.0 ? static_cast<double>(target - history[i - 1].first) / span : 0.0;
            out.px = static_cast<int>(history[i - 1].second + frac * (history[i].second - history[i - 1].second) + 0.5);
            out.ok = true;
            break;
        }
    }
    if (!out.ok) std::printf("    [walk: no frame at the target tick (%zu frames)]\n", history.size());
    out.clicks = static_cast<int>(s.clicks_given());
    s.order(0, sim::CommandType::Stop, {ant}, 0, 0);
    s.run_ms(600);
    return out;
}

struct HumpCell {
    int rtt{0};
    int jitter{0};
    bool predict{false};
    int reaction_ms{0};
    double single_px{0};
    double hump_px{0};
    double gain{0};
    int good{0};            // humps that gained
    int trials{0};
    uint64_t sealed[3]{0, 0, 0};   // the person's orders that the host sealed at the tick that the screen applied them at, earlier, later (the prediction's own count; nothing without it)
};

// A cell of the grid: `trials` single-order walks and as many humping ones in one match of one link
HumpCell hump_cell(int rtt, int jitter, bool predict, int reaction_ms, int noise_ms, int trials, uint32_t seed) {
    Params p;
    p.map = "MUD.LVL";
    p.local = rtt < 0;                                                             // a negative round trip: a game on this computer
    p.link.rtt_ms = static_cast<uint32_t>(std::max(rtt, 0));
    p.link.jitter_ms = static_cast<uint32_t>(jitter);
    p.predict = predict;
    p.seed = seed;
    Session s(p);
    HumpCell cell;
    cell.rtt = rtt;
    cell.jitter = jitter;
    cell.predict = predict;
    cell.reaction_ms = reaction_ms;
    if (!s.begin()) return cell;
    const std::vector<uint32_t> mine = Session::ants_of(s.app().sim(), 0);
    if (mine.empty()) return cell;
    const uint32_t ant = mine[0];
    s.run_ms(2500);
    std::vector<int> single;
    std::vector<int> humped;
    for (int i = 0; i < trials; ++i) {
        const WalkResult a = walk(s, ant, false, 0, 0);
        if (a.ok) single.push_back(a.px);
        const WalkResult b = walk(s, ant, true, reaction_ms, noise_ms);
        if (b.ok) humped.push_back(b.px);
    }
    if (single.empty() || humped.empty()) return cell;
    for (int v : single) cell.single_px += v;
    for (int v : humped) cell.hump_px += v;
    cell.single_px /= static_cast<double>(single.size());
    cell.hump_px /= static_cast<double>(humped.size());
    cell.gain = cell.hump_px - cell.single_px;
    for (int v : humped) cell.good += v > cell.single_px ? 1 : 0;
    cell.trials = static_cast<int>(std::min(single.size(), humped.size()));
    if (!p.local && s.app().net()->prediction() != nullptr) {
        const net::Prediction::Stats& st = s.app().net()->prediction()->stats();
        cell.sealed[0] = st.sealed_exact;
        cell.sealed[1] = st.sealed_early;
        cell.sealed[2] = st.sealed_late;
    }
    return cell;
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The attack click on an enemy ant that walks
// ---------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kAttackerTile = 5;               // the person's ant waits on tile 5 of row 3, the other person's on tile 21 (clear of the third colony's hill, which a game on one computer has)
constexpr int32_t kEnemyTile = 21;
constexpr int32_t kEnemyGoal = 9;

struct AttackResult {
    bool clicked{false};            // the person's click was made on the enemy ant as the screen showed it
    uint8_t shown_order{0};         // what the screen's engine said the attacker's order was right after the click (3: an attack)
    uint8_t landed_order{0};        // what the confirmed engine made of it when the order landed (3: an attack; 1: a move)
};

// One attack click: the other person's ant is sent west; when it walks and `phase_ms` (plus up to `spread_ms` more) after its third tile on the screen, the person right-clicks on it with the attacker selected. With `turn_ms` the
// other person gives his ant another goal every so often (the next row, then back: it changes course as an ant does that is being steered in a fight), which the screen cannot know before the order arrives
AttackResult attack_click(Session& s, uint32_t attacker, uint32_t enemy, int phase_ms, int turn_ms = 0, int spread_ms = 0) {
    AttackResult out;
    if (!s.bring(0, attacker, kAttackerTile, kLaneRow)) {
        std::printf("    [attack: the person's ant did not get to its place]\n");
        return out;
    }
    if (!s.bring(1, enemy, kEnemyTile, kLaneRow)) {
        std::printf("    [attack: the other ant did not get to its place]\n");
        return out;
    }
    s.camera_on(15, kLaneRow);
    int32_t sx = 0;
    int32_t sy = 0;
    const sim::AntSnapshot* mine = s.shown_ant(attacker);
    if (mine == nullptr || !s.screen_of_world(mine->px, mine->py, sx, sy)) {
        std::printf("    [attack: the person's ant is not on the screen]\n");
        return out;
    }
    s.run_ms(static_cast<uint32_t>(s.rng()() % 50u));
    s.click(SDL_BUTTON_LEFT, sx, sy);
    if (s.app().hud().get_selected_ant_id() != attacker) {
        std::printf("    [attack: the click did not select the person's ant]\n");
        return out;
    }
    s.order(1, sim::CommandType::GroupMove, {enemy}, kEnemyGoal, kLaneRow);
    bool armed = false;
    uint32_t click_at = 0;
    bool done = false;
    uint32_t next_turn = s.now() + static_cast<uint32_t>(turn_ms);
    bool other_row = true;
    s.on_frame_start = [&]() {
        if (turn_ms > 0 && !done && s.now() >= next_turn) {                        // the other person steers his ant
            next_turn = s.now() + static_cast<uint32_t>(turn_ms);
            s.order(1, sim::CommandType::GroupMove, {enemy}, kEnemyGoal, other_row ? kLaneRow - 1 : kLaneRow);
            other_row = !other_row;
        }
        if (done || !armed || s.now() < click_at) return;
        done = true;
        const sim::AntSnapshot* seen = s.shown_ant(enemy);
        int32_t ex = 0;
        int32_t ey = 0;
        if (seen == nullptr || !s.screen_of_world(seen->px, seen->py, ex, ey)) return;
        s.click(SDL_BUTTON_RIGHT, ex, ey);
        out.clicked = true;
        out.shown_order = s.app().view_sim().get_unit(attacker).orig_order;
    };
    s.on_frame_end = [&]() {
        if (armed || done) return;
        const sim::AntSnapshot* seen = s.shown_ant(enemy);
        if (seen != nullptr && seen->tile_x <= kEnemyTile - 3) {                  // the enemy ant has walked three tiles on the screen
            armed = true;
            click_at = s.now() + static_cast<uint32_t>(phase_ms) + (spread_ms > 0 ? static_cast<uint32_t>(s.rng()() % static_cast<uint32_t>(spread_ms)) : 0u);       // (with a spread the click phases do not fall on a grid that the tile's edge can be aligned with)
        }
    };
    const bool clicked = s.run_until([&]() { return done; }, 8000);
    s.on_frame_end = nullptr;
    if (!clicked) {
        std::printf("    [attack: the other ant did not walk three tiles in time]\n");
        s.on_frame_start = nullptr;
        return out;
    }
    s.run_until([&]() { return s.app().sim().get_unit(attacker).orig_order != sim::AntUnit::kOrderNone; }, 3000);
    out.landed_order = s.app().sim().get_unit(attacker).orig_order;
    s.on_frame_start = nullptr;
    s.order(0, sim::CommandType::Stop, {attacker}, 0, 0);
    s.order(1, sim::CommandType::Stop, {enemy}, 0, 0);
    s.run_ms(1200);
    return out;
}

struct AttackCell {
    int rtt{0};
    int jitter{0};
    bool predict{false};
    bool others{true};              // the other player's ants are drawn from the confirmed engine (the person looks at what the pointer picks: ants_app/others_view.hpp), the product's picture
    bool aim{true};                 // ... and the click names the tile where the predicted engine has the ant
    int clicks{0};
    int shown_attack{0};
    int landed_attack{0};
    int landed_move{0};
    int no_order{0};                // clicks that gave no order at all (the pointer was not on an ant that the screen's engine could pick)
};

// A cell: `phases` clicks in one match of one link, the click `i * 400 / phases` ms after the third tile of the walk
AttackCell attack_cell(int rtt, int jitter, bool predict, int phases, uint32_t seed, int turn_ms = 0, bool others = true, bool aim = true, int spread_ms = 0) {
    Params p;
    p.map = "GRASS.LVL";
    p.local = rtt < 0;
    p.use_peer = rtt >= 0;
    p.link.rtt_ms = static_cast<uint32_t>(std::max(rtt, 0));
    p.link.jitter_ms = static_cast<uint32_t>(jitter);
    p.peer_link.rtt_ms = 30;
    p.predict = predict;
    p.others_confirmed = others;
    p.others_aim = aim;
    p.seed = seed;
    Session s(p);
    AttackCell cell;
    cell.rtt = rtt;
    cell.jitter = jitter;
    cell.predict = predict;
    cell.others = others;
    cell.aim = aim;
    if (!s.begin()) {
        std::printf("    [attack: the match did not begin]\n");
        return cell;
    }
    const std::vector<uint32_t> mine = Session::ants_of(s.app().sim(), 0);
    const std::vector<uint32_t> theirs = Session::ants_of(s.app().sim(), 1);
    if (mine.empty() || theirs.empty()) {
        std::printf("    [attack: ants %zu / %zu; peer seat %d]\n", mine.size(), theirs.size(), static_cast<int>(s.peer().net.my_seat()));
        return cell;
    }
    if (p.local) {                                                                 // (a game on this computer has all four colonies: the other two wait at the bottom of the map, off the lane)
        for (const uint8_t seat : {uint8_t{2}, uint8_t{3}}) s.order(seat, sim::CommandType::GroupMove, Session::ants_of(s.app().sim(), seat), 15, 26);
        s.run_ms(15000);
    }
    s.run_ms(2500);
    for (int i = 0; i < phases; ++i) {
        const AttackResult r = attack_click(s, mine[0], theirs[0], i * 400 / phases, turn_ms, spread_ms);
        if (!r.clicked) continue;
        ++cell.clicks;
        cell.shown_attack += r.shown_order == sim::AntUnit::kOrderAttack ? 1 : 0;
        cell.landed_attack += r.landed_order == sim::AntUnit::kOrderAttack ? 1 : 0;
        cell.landed_move += r.landed_order != sim::AntUnit::kOrderAttack && r.landed_order != sim::AntUnit::kOrderNone ? 1 : 0;
        cell.no_order += r.landed_order == sim::AntUnit::kOrderNone ? 1 : 0;
    }
    return cell;
}

// The cells of several seeds added up (other phases of the frames, the ticks and the links' jitter)
AttackCell attack_row(int rtt, int jitter, bool predict, int phases, const std::vector<uint32_t>& seeds, int turn_ms = 0, bool others = true, bool aim = true, int spread_ms = 0) {
    AttackCell sum;
    sum.rtt = rtt;
    sum.jitter = jitter;
    sum.predict = predict;
    sum.others = others;
    sum.aim = aim;
    for (const uint32_t seed : seeds) {
        const AttackCell c = attack_cell(rtt, jitter, predict, phases, seed + seed_offset(), turn_ms, others, aim, spread_ms);
        sum.clicks += c.clicks;
        sum.shown_attack += c.shown_attack;
        sum.landed_attack += c.landed_attack;
        sum.landed_move += c.landed_move;
        sum.no_order += c.no_order;
    }
    return sum;
}

double share_of(const AttackCell& c) { return c.clicks > 0 ? 100.0 * c.landed_attack / c.clicks : 0.0; }

void print_attack(const AttackCell& c, const char* enemy) {
    char link[96];
    if (c.rtt < 0) std::snprintf(link, sizeof link, "a game on this computer    ");
    else std::snprintf(link, sizeof link, "rtt %3d jit %2d prediction %-3s%s", c.rtt, c.jitter, c.predict ? "on" : "off", !c.others ? "" : c.aim ? " others confirmed, aimed" : " others confirmed, unaimed");
    std::printf("    attack (%s)  %s: %2d clicks on the walking ant, the screen took %2d as an attack, %2d landed as an attack (%3.0f %%), %2d as a plain move, %d gave no order\n", enemy, link, c.clicks,
                c.shown_attack, c.landed_attack, share_of(c), c.landed_move, c.no_order);
}


// ---------------------------------------------------------------------------------------------------------------------------------
// A blow as the screen draws it: the attacker's swing and the victim's reaction
// ---------------------------------------------------------------------------------------------------------------------------------

struct BlowResult {
    bool ok{false};
    int swing_ms{0};          // the attacker's swing begins on the screen at this time ...
    int hurt_ms{0};           // ... the victim's hit points fall on the screen at this time, and its flinch begins
    int flinch_ms{0};
    uint32_t lead{0};         // the lead of the prediction when the blow was struck, ticks
};

// Two ants stand next to each other on the lane and one of them is ordered to attack the other, who stands still. The time of the swing and of the reaction is read from the world that the frame draws
// (Session::shown_ant) every millisecond of the match's clock. `own_strikes`: the application's own ant strikes (else the other person's ant strikes the application's)
BlowResult blow(Session& s, bool own_strikes) {
    BlowResult out;
    const std::vector<uint32_t> mine = Session::ants_of(s.app().sim(), 0);
    const std::vector<uint32_t> theirs = Session::ants_of(s.app().sim(), 1);
    if (mine.empty() || theirs.empty()) return out;
    const uint32_t a = mine[0];
    const uint32_t b = theirs[0];
    if (!s.bring(0, a, 15, kLaneRow) || !s.bring(1, b, 16, kLaneRow)) return out;
    const uint32_t striker = own_strikes ? a : b;
    const uint32_t victim = own_strikes ? b : a;
    const sim::AntSnapshot* v0 = s.shown_ant(victim);
    if (v0 == nullptr) return out;
    const uint16_t hp0 = v0->hp;
    s.order(own_strikes ? 0 : 1, sim::CommandType::GroupAttack, {striker}, own_strikes ? 16 : 15, kLaneRow);
    const uint32_t t0 = s.now();
    int swing = -1;
    int hurt = -1;
    int flinch = -1;
    s.run_until([&]() {
        const sim::AntSnapshot* st = s.shown_ant(striker);
        const sim::AntSnapshot* vi = s.shown_ant(victim);
        if (st != nullptr && swing < 0 && st->state == sim::UnitState::Attacking) swing = static_cast<int>(s.now() - t0);
        if (vi != nullptr && hurt < 0 && vi->hp < hp0) hurt = static_cast<int>(s.now() - t0);
        if (vi != nullptr && flinch < 0 && vi->state == sim::UnitState::Flinch) flinch = static_cast<int>(s.now() - t0);
        return swing >= 0 && hurt >= 0 && flinch >= 0;
    }, 6000);
    if (swing >= 0 && hurt >= 0 && flinch >= 0) {
        out.ok = true;
        out.swing_ms = swing;
        out.hurt_ms = hurt;
        out.flinch_ms = flinch;
        out.lead = s.app().net() != nullptr && s.app().net()->prediction() != nullptr ? s.app().net()->prediction()->lead_ticks() : 0u;
    }
    s.order(0, sim::CommandType::Stop, {a}, 0, 0);
    s.order(1, sim::CommandType::Stop, {b}, 0, 0);
    s.run_ms(1500);
    return out;
}

// The same blow in a fresh match: `how` 0 = the plain picture (every ant is the predicted engine's), 1 = others = confirmed, 2 = no prediction (the confirmed engine is the picture)
BlowResult blow_in(int rtt, int how, bool own_strikes, uint32_t seed) {
    Params p;
    p.map = "GRASS.LVL";
    p.link.rtt_ms = static_cast<uint32_t>(rtt);
    p.link.jitter_ms = 0;
    p.peer_link.rtt_ms = 30;
    p.predict = how != 2;
    p.others_confirmed = how == 1;
    p.seed = seed;
    Session s(p);
    if (!s.begin()) return BlowResult{};
    s.run_ms(2500);
    return blow(s, own_strikes);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// How the other player's ants move on the screen: a computer player's ants over a shaped link, frame by frame
// ---------------------------------------------------------------------------------------------------------------------------------

struct DrawnMotion {
    double minutes{0.0};
    int hops8{0};                   // frames in which an ant of the computer player is drawn 8 px or more from where the last frame drew it
    int hops16{0};                  // ... 16 px or more
    int backwards{0};               // ... 8 px or more AGAINST the way it was walking the frame before (a correction that puts it back)
    int tile_flips{0};              // times that what stands on a tile (food, a power-up, a bomb, a fire wall, a bridge) goes back to what it was a moment before (a second at most): a tile that flickers
    int frames{0};
    uint64_t corrections{0};        // ants that the predicted engine's rebuilds moved by 8 px or more (what the plain picture slides; the prediction's own measure)
    uint64_t correction_px_total{0};
    uint32_t correction_px_max{0};
};

// One match of the person (idle) against a Hard computer player, every frame drawn, for `ms` of the match's clock. `how`: 0 the plain picture (the predicted engine, the slide of a correction), 1 others =
// confirmed, 2 no prediction (the confirmed engine alone: what a game that never predicted draws)
DrawnMotion drawn_motion(int rtt, int how, uint32_t seed, uint32_t ms) {
    Params p;
    p.map = "GRASS.LVL";
    p.seats = 2;
    p.use_peer = false;
    p.render = true;
    p.link.rtt_ms = static_cast<uint32_t>(rtt);
    p.link.jitter_ms = 30;
    p.predict = how != 2;
    p.others_confirmed = how == 1;
    ai::BotSpec bot;
    bot.seat = 1;
    bot.level = ai::Level::Hard;
    p.bots.push_back(bot);
    p.seed = seed;
    DrawnMotion out;
    Session s(p);
    if (!s.begin()) return out;
    if (s.app().net()->prediction() != nullptr) s.app().net()->prediction()->set_measure_corrections(true);
    std::map<uint32_t, std::pair<int32_t, int32_t>> last;
    std::map<uint32_t, std::pair<int32_t, int32_t>> velocity;
    std::vector<uint16_t> tile_now;
    std::vector<uint16_t> tile_before;
    std::vector<int> tile_changed_at;
    const uint32_t start = s.now();
    s.on_frame_end = [&]() {
        const sim::WorldState& world = s.app().shown_world();
        ++out.frames;
        if (tile_now.size() != world.cells.size()) {
            tile_now.assign(world.cells.size(), 0);
            tile_before.assign(world.cells.size(), 0);
            tile_changed_at.assign(world.cells.size(), -1000);
            for (size_t i = 0; i < world.cells.size(); ++i) tile_now[i] = static_cast<uint16_t>(world.cells[i].interactive_id);
        }
        for (size_t i = 0; i < world.cells.size(); ++i) {                    // what stands on each tile, frame by frame
            const uint16_t id = static_cast<uint16_t>(world.cells[i].interactive_id);
            if (id == tile_now[i]) continue;
            if (id == tile_before[i] && out.frames - tile_changed_at[i] <= 60) ++out.tile_flips;
            tile_before[i] = tile_now[i];
            tile_now[i] = id;
            tile_changed_at[i] = out.frames;
        }
        for (const sim::AntSnapshot& engine_ant : world.ants) {
            if (engine_ant.player_id != 1 || engine_ant.hp == 0) continue;
            const sim::AntSnapshot a = glided_ant(s.app().renderer().view_glide(), world, 0, engine_ant);          // (where the frame draws it)
            const auto it = last.find(a.id);
            if (it != last.end()) {
                const int32_t dx = a.px - it->second.first;
                const int32_t dy = a.py - it->second.second;
                const int32_t d = std::max(std::abs(dx), std::abs(dy));
                const auto v = velocity[a.id];
                if (d >= 8) {
                    ++out.hops8;
                    if (std::max(std::abs(v.first), std::abs(v.second)) >= 1 && static_cast<int64_t>(dx) * v.first + static_cast<int64_t>(dy) * v.second < 0) ++out.backwards;
                }
                if (d >= 16) ++out.hops16;
                if (d <= 4) velocity[a.id] = {dx, dy};                      // (the way it was walking: the last small step)
            }
            last[a.id] = {a.px, a.py};
        }
    };
    s.run_ms(ms);
    s.on_frame_end = nullptr;
    out.minutes = static_cast<double>(s.now() - start) / 60000.0;
    if (const net::Prediction* prediction = s.app().net()->prediction()) {
        out.corrections = prediction->stats().ants_moved;
        out.correction_px_total = prediction->stats().move_px_total;
        out.correction_px_max = prediction->stats().max_move_px;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// What a person sees of the other players' ants over a link, a minute of play at a time: the plain picture against the default one (a table: ANTS_LINK_TABLE=1)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Seen {
    double minutes{0.0};
    int slid_others{0};             // slides of 8 px or more that began on an ant of another player (the plain picture's corrections of the other players' ants, drawn, and the hops of the engine's own snaps in both)
    int corrected_others{0};        // ... of them those that no move of the engine's own ant explains: in the picture of others = confirmed there are none (the plain picture's corrections ARE moves of the engine that it draws, the predicted one)
    int slid_own{0};                // ... on an ant of the player's own
    int hops8_others{0};            // frames in which an ant of another player is drawn 8 px or more from where the last frame drew it
    int backwards_others{0};        // ... against the way it was walking
    double lag_ms_sum{0.0};         // the time that the other players' ants are drawn behind the player's own (the predicted engine's tick less the confirmed one's, a tick being 50 ms)
    int frames{0};
};

// scene 0: Treasure on a small map against a Hard computer player, the person idle; 1: the same against another person who gives orders now and then; 2: a duel (game mode 187) against a Hard computer
// player, the person clicking about. `others`: the default picture (false: the plain one).
Seen seen_over_link(int scene, int rtt, int jitter, bool others, uint32_t seed, uint32_t ms) {
    Params p;
    p.map = "GRASS.LVL";
    p.seats = 2;
    p.render = true;
    p.link.rtt_ms = static_cast<uint32_t>(rtt);
    p.link.jitter_ms = static_cast<uint32_t>(jitter);
    p.peer_link.rtt_ms = 60;
    p.others_confirmed = others;
    p.seed = seed;
    p.use_peer = scene == 1;
    if (scene != 1) {
        ai::BotSpec bot;
        bot.seat = 1;
        bot.level = ai::Level::Hard;
        p.bots.push_back(bot);
    }
    if (scene == 2) p.mode = 1;
    Seen out;
    Session s(p);
    if (!s.begin()) return out;
    struct Last {
        int32_t x{0};
        int32_t y{0};
        int32_t vx{0};
        int32_t vy{0};
        double slid{0.0};
    };
    std::map<uint32_t, Last> last;
    SlideCause cause;
    s.on_frame_end = [&]() {
        const sim::WorldState& world = s.app().shown_world();
        ++out.frames;
        out.lag_ms_sum += static_cast<double>(s.app().view_sim().current_tick() - s.app().sim().current_tick()) * 50.0;
        for (const sim::AntSnapshot& engine_ant : world.ants) {
            if (engine_ant.hp == 0) continue;
            int32_t gx = 0;
            int32_t gy = 0;
            s.app().renderer().view_glide().offset_of(engine_ant.id, gx, gy);
            const double mag = std::hypot(static_cast<double>(gx), static_cast<double>(gy));
            Last& l = last[engine_ant.id];
            bool began = false;
            const bool unexplained = engine_ant.player_id != 0 && cause.unexplained(engine_ant, gx, gy, mag >= 8.0, began);
            if (mag >= 8.0 && (l.slid == 0.0 || mag > l.slid + 1.5)) {
                if (engine_ant.player_id == 0) ++out.slid_own;
                else {
                    ++out.slid_others;
                    if (unexplained && began) ++out.corrected_others;
                }
            }
            l.slid = mag >= 8.0 ? mag : 0.0;
            if (engine_ant.player_id == 0) continue;
            const sim::AntSnapshot a = glided_ant(s.app().renderer().view_glide(), world, 0, engine_ant);
            if (l.x != 0 || l.y != 0) {
                const int32_t dx = a.px - l.x;
                const int32_t dy = a.py - l.y;
                if (std::max(std::abs(dx), std::abs(dy)) >= 8) {
                    ++out.hops8_others;
                    if (std::max(std::abs(l.vx), std::abs(l.vy)) >= 1 && static_cast<int64_t>(dx) * l.vx + static_cast<int64_t>(dy) * l.vy < 0) ++out.backwards_others;
                }
                if (std::max(std::abs(dx), std::abs(dy)) <= 4) {
                    l.vx = dx;
                    l.vy = dy;
                }
            }
            l.x = a.px;
            l.y = a.py;
        }
    };
    if (scene == 0) {
        s.run_ms(ms);
    } else {
        Players players(s);
        players.peer = scene == 1;
        players.play(ms);
    }
    s.on_frame_end = nullptr;
    out.minutes = static_cast<double>(ms) / 60000.0;
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The hop of a re-order: the engine puts an ant that walks and is given a new order on the middle of its tile (the original's GoTo does, FUN_0101fc50), up to 16 px from where the picture had it
// ---------------------------------------------------------------------------------------------------------------------------------

struct HopRun {
    int engine_snap{0};             // the largest move of the engine's ant between two frames after the order (px, the larger of the two axes): the snap
    int walk_max{0};                // the largest move of the DRAWN ant between two frames before the order (a walk)
    int after_max{0};               // ... and after it, over the next 700 ms (the order's hop and the walk that follows)
    int back_max{0};                // the most that the drawn ant moved WEST (against the way that it walks) in one frame after the order
    int pick_tried{0};              // a click that picks the ant where the frame draws it, made while it slides (a slide of 8 px or more)
    int pick_ok{0};                 // ... that picked it
    int pick_foil_ok{0};            // a click on the engine's side of it, outside the drawn ant's box: picked nothing
    bool ended_clean{false};        // a second after the order nothing slides
    bool ok{false};
};

// The ant (seat `seat`) walks east along row 3 of the lane, and is given another goal when `phase` ms of its walk have passed: with the mouse for the player's own ant (a click on it, a right click on
// the goal), through the other person's machine for another seat's. The drawn place is followed frame by frame (Renderer::drawn_point). `kind`: 0 a goal further on, 1 a goal behind it, 2 a goal one row up
HopRun hop_once(Session& s, uint8_t seat, uint32_t ant, uint32_t phase, int kind, uint32_t& flip) {
    HopRun out;
    if (!s.bring(seat, ant, seat == 0 ? 6 : 12, kLaneRow, 30000)) return out;                 // (to the start of the lane: another seat's ant has its own, away from the hills)
    s.order(seat, sim::CommandType::GroupMove, {ant}, 24, kLaneRow);
    s.run_ms(900 + phase);
    struct Point {
        int32_t x{0};
        int32_t y{0};
    };
    std::vector<Point> path;
    std::vector<Point> engine;
    const auto glide_of = [&](int32_t& gx, int32_t& gy) { return s.app().renderer().view_glide().offset_of(ant, gx, gy); };
    bool tried = false;
    s.on_frame_end = [&]() {
        const sim::WorldState& world = s.app().shown_world();
        const sim::AntSnapshot* a = Session::ant_in(world, ant);
        if (a == nullptr) return;
        Point pt;
        if (s.app().renderer().drawn_point(world, *a, pt.x, pt.y)) path.push_back(pt);
        engine.push_back(Point{a->px, a->py});
        int32_t gx = 0;
        int32_t gy = 0;
        if (seat != 0 || tried || engine.size() < 2 || !glide_of(gx, gy) || std::max(std::abs(gx), std::abs(gy)) < 8 || gy != 0) return;
        tried = true;                                                                         // the click of the pointer: where the frame draws the ant, and where the engine has it
        const sim::AntSnapshot shown = *a;
        const int32_t toward = gx > 0 ? 1 : -1;
        int32_t sx = 0;
        int32_t sy = 0;
        s.camera_on(shown.tile_x, shown.tile_y);
        s.app().hud().clear_selection();
        ++out.pick_tried;
        if (s.screen_of_world(shown.px + gx + toward * 18, shown.py - 8, sx, sy)) s.click(SDL_BUTTON_LEFT, sx, sy);
        if (s.app().hud().get_selected_ant_id() == ant) ++out.pick_ok;
        s.app().hud().clear_selection();
        if (s.screen_of_world(shown.px - toward * 18, shown.py - 8, sx, sy)) s.click(SDL_BUTTON_LEFT, sx, sy);
        if (s.app().hud().get_selected_ant_id() != ant) ++out.pick_foil_ok;
        s.app().hud().clear_selection();
    };
    s.run_ms(100);
    const size_t before = path.size();
    const sim::AntSnapshot* walking = s.shown_ant(ant);
    if (walking == nullptr || walking->action != 1) {
        s.on_frame_end = nullptr;
        return out;
    }
    const sim::AntSnapshot there = *walking;
    flip ^= 1u;
    const int32_t goal_x = std::clamp(kind == 1 ? there.tile_x - 3 - static_cast<int32_t>(flip) : there.tile_x + 8 + static_cast<int32_t>(flip), 2, 28);
    const int32_t goal_y = kind == 2 ? kLaneRow - 1 : kLaneRow;
    if (seat == 0) {
        int32_t sx = 0;
        int32_t sy = 0;
        s.camera_on(there.tile_x, there.tile_y);
        if (!s.screen_of_world(there.px, there.py, sx, sy)) return out;
        s.click(SDL_BUTTON_LEFT, sx, sy);                                                     // (the ant is picked ...)
        if (s.app().hud().get_selected_ant_id() != ant) return out;
        if (!s.screen_of_tile(goal_x, goal_y, sx, sy)) return out;
        s.click(SDL_BUTTON_RIGHT, sx, sy);                                                    // (... and sent to a goal that is on the screen)
    } else {
        s.order(seat, sim::CommandType::GroupMove, {ant}, goal_x, goal_y);
    }
    const size_t engine_before = engine.size();
    s.run_ms(700);
    s.run_ms(300);
    s.on_frame_end = nullptr;
    for (size_t i = std::max<size_t>(engine_before, 1); i < engine.size(); ++i) {
        out.engine_snap = std::max(out.engine_snap, std::max(std::abs(engine[i].x - engine[i - 1].x), std::abs(engine[i].y - engine[i - 1].y)));
    }
    for (size_t i = 1; i < path.size(); ++i) {
        const int jump = std::max(std::abs(path[i].x - path[i - 1].x), std::abs(path[i].y - path[i - 1].y));
        if (i < before) out.walk_max = std::max(out.walk_max, jump);
        else if (i < before + 45) {
            out.after_max = std::max(out.after_max, jump);                                      // (the 700 ms after the order: about 42 frames)
            out.back_max = std::max(out.back_max, path[i - 1].x - path[i].x);
        }
    }
    int32_t gx = 0;
    int32_t gy = 0;
    out.ended_clean = !glide_of(gx, gy);
    out.ok = true;
    return out;
}

struct HopSweep {
    int runs{0};
    int hops{0};                    // runs in which the engine moved the ant by 8 px or more in one frame (its snap)
    int snap_worst{0};
    int walk_worst{0};
    int after_worst{0};
    int back_worst{0};
    int pick_tried{0};
    int pick_ok{0};
    int pick_foil_ok{0};
    int unclean{0};
};

// The phase of the order is swept over a walk of 1.3 tiles (the ant crosses a tile line once) in steps of 34 ms
HopSweep hop_sweep(const Params& p, uint8_t seat, int kind) {
    HopSweep out;
    Session s(p);
    if (!s.begin()) return out;
    const std::vector<uint32_t> mine = Session::ants_of(s.app().view_sim(), seat);
    if (mine.empty()) return out;
    uint32_t flip = 0;
    for (uint32_t phase = 0; phase < 520; phase += 34) {
        const HopRun r = hop_once(s, seat, mine[0], phase, kind, flip);
        if (!r.ok) continue;
        ++out.runs;
        if (r.engine_snap >= 8) ++out.hops;
        out.snap_worst = std::max(out.snap_worst, r.engine_snap);
        out.walk_worst = std::max(out.walk_worst, r.walk_max);
        out.after_worst = std::max(out.after_worst, r.after_max);
        out.back_worst = std::max(out.back_worst, r.back_max);
        out.pick_tried += r.pick_tried;
        out.pick_ok += r.pick_ok;
        out.pick_foil_ok += r.pick_foil_ok;
        if (!r.ended_clean) ++out.unclean;
    }
    return out;
}
}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    net::NetGame::default_prediction_budget_ns() = UINT64_MAX;      // (a busy machine stalls the test process now and then: that is not the prediction's cost)
    std::cout << "\n=======================================================\n [PREDICTION OVER SHAPED LINKS] the real input path\n=======================================================\n";

    TEST_CASE("LK1 A Room Over A Shaped Link: The Application Joins, The Match Begins, The Person Selects An Ant And Orders It With The Mouse, The Order Lands After The Link's Lag") {
        Params p;
        p.link.rtt_ms = 100;
        p.link.jitter_ms = 20;
        Session s(p);
        ASSERT_TRUE(s.begin());
        const std::vector<uint32_t> mine = Session::ants_of(s.app().sim(), 0);
        ASSERT_FALSE(mine.empty());
        ASSERT_TRUE(s.app().net()->predicting());
        const WalkResult w = walk(s, mine[0], false, 0, 0);
        ASSERT_TRUE(w.ok);
        std::printf("\n    one walk of 9 s on grass: %d px\n", w.px);
    } TEST_END();

    TEST_CASE("LK2 Humping On Mud Through The Mouse: With The Screen On The Predicted Engine A Person Who Clicks The Other Goal Up To A Tenth Of A Second After The Ant Is Seen To Cross A Tile Line Gains Ground At Every Round Trip, No More Than In A Game On His Own Computer And No Less Than A Click Two Ticks Later Gains There (The Order Lands At The Tick That Was Aimed At Or The Next, Hardly Ever Before It); Without It The Window Of The Re-Order Is Past Before The Order Lands") {
        const std::vector<int> rtts{-1, 30, 100, 180, 300};
        const std::vector<int> local_reactions{0, 25, 50, 75, 100, 125, 150, 175, 200, 250};
        const std::vector<int> reactions = table_wanted() ? std::vector<int>{0, 25, 50, 75, 100, 125, 150, 200} : std::vector<int>{0, 25, 50, 75, 100, 125};
        const int trials = table_wanted() ? 20 : 8;
        const int jitter = 30;
        const int noise = std::getenv("ANTS_LINK_NOISE") != nullptr ? std::atoi(std::getenv("ANTS_LINK_NOISE")) : 30;        // the person's own timing: each click a little earlier or later, up to this many ms
        std::vector<double> local;                                                            // the game on this computer, by local_reactions
        const auto local_at = [&](int reaction) {                                             // (between two of them: a straight line)
            for (size_t i = 1; i < local_reactions.size(); ++i) {
                if (reaction > local_reactions[i]) continue;
                const double f = static_cast<double>(reaction - local_reactions[i - 1]) / static_cast<double>(local_reactions[i] - local_reactions[i - 1]);
                return local[i - 1] + f * (local[i] - local[i - 1]);
            }
            return local.back();
        };
        double local_best = 0.0;
        for (const int rtt : rtts) {
            for (const bool predict : {false, true}) {
                if (rtt < 0 && predict) continue;
                const std::vector<int>& grid = rtt < 0 ? local_reactions : reactions;
                std::vector<double> gains;
                uint64_t sealed[3] = {0, 0, 0};
                for (const int reaction : grid) {
                    const HumpCell c = hump_cell(rtt, jitter, predict, reaction, noise, trials, 5u + seed_offset() + static_cast<uint32_t>(reaction));
                    ASSERT_TRUE(c.trials > 0);
                    gains.push_back(c.gain);
                    for (int k = 0; k < 3; ++k) sealed[k] += c.sealed[k];
                }
                const double best = *std::max_element(gains.begin(), gains.end());
                std::printf("    hump  rtt %3d jit %2d prediction %-3s (timing +-%d ms): gain (px over one order in 9 s) by reaction", rtt < 0 ? 0 : rtt, rtt < 0 ? 0 : jitter, predict ? "on" : "off", noise);
                for (size_t i = 0; i < gains.size(); ++i) std::printf(" %d ms: %+.0f", grid[i], gains[i]);
                std::printf("  | best %+.0f", best);
                if (predict) std::printf("  | orders sealed at the tick they were applied at %llu, earlier %llu, later %llu", static_cast<unsigned long long>(sealed[0]), static_cast<unsigned long long>(sealed[1]), static_cast<unsigned long long>(sealed[2]));
                std::printf("\n");
                if (rtt < 0) {
                    local = gains;
                    local_best = best;
                    ASSERT_TRUE(local_best > 30.0);                                           // (the reference works: the game on this computer gains on mud, as the original's snap does)
                    continue;
                }
                for (size_t i = 0; i < grid.size(); ++i) {
                    if (grid[i] > 125) continue;                                              // (the window: up to an eighth of a second after the crossing is seen)
                    if (predict) {
                        if (grid[i] <= 100) ASSERT_TRUE(gains[i] > 0.0);                      // with the prediction every re-order of the first tenth of a second gains, at every round trip ...
                        ASSERT_TRUE(gains[i] <= local_at(grid[i]) + 10.0);                    // ... no more than the same click gains in the local game ...
                        ASSERT_TRUE(gains[i] >= local_at(grid[i] + 100) - 10.0);              // ... and no less than a click two ticks later gains there (the order lands at the tick that was aimed at or the next, now and then the one after)
                    } else if (rtt >= 100) {
                        ASSERT_TRUE(gains[i] < 0.0);                                          // without it, from 100 ms on, every re-order loses pixels
                    }
                }
                if (predict) {
                    ASSERT_TRUE(best >= 0.45 * local_best);                                   // (the best re-order gains about what the local game's best does: +28 to +53 against +54 in the measurement)
                    ASSERT_TRUE(sealed[1] * 10u <= sealed[0] + sealed[1] + sealed[2]);        // (an order is hardly ever sealed before the tick that it was aimed at: under a tenth of them)
                } else if (rtt >= 100) {
                    ASSERT_TRUE(best < 0.25 * local_best);
                }
            }
        }
    } TEST_END();

    TEST_CASE("LK3 In The Plain Picture (--others predicted) The Attack Click On A Walking Ant: The Order Names The Tile Of The Ant As The Screen Shows It, So That With The Prediction (The Screen Is The Engine The Order Lands In) It Is Still An Attack When It Lands, At Every Round Trip, For An Ant That Walks On And For One That Its Owner Steers; Without It The Ant Has Left The Tile Before The Order Lands, And The Click Is A Plain Move; A Game On This Computer Is The Reference") {
        // (the plain picture, --others predicted, expires with v0.22.3 at the latest: this test goes with it, AGENTS.md rule 9)
        const std::vector<uint32_t> seeds = table_wanted() ? std::vector<uint32_t>{7u, 8u, 9u, 10u} : std::vector<uint32_t>{7u, 8u};
        const int rtts[5] = {2, 30, 100, 180, 300};
        struct Kind {
            const char* name;
            int turn_ms;
        };
        for (const Kind kind : {Kind{"it walks on", 0}, Kind{"it is steered", 900}}) {
            const AttackCell local = attack_row(-1, 0, false, 16, seeds, kind.turn_ms);
            print_attack(local, kind.name);
            ASSERT_EQ(local.clicks, 16 * static_cast<int>(seeds.size()));
            ASSERT_EQ(local.no_order, 0);
            ASSERT_TRUE(share_of(local) >= 95.0);                                             // (the reference: on one computer the click is an attack, the order lands in the screen's own state)
            double off_share[5] = {0, 0, 0, 0, 0};
            for (int k = 0; k < 5; ++k) {
                for (const bool predict : {false, true}) {
                    const AttackCell c = attack_row(rtts[k], 30, predict, 16, seeds, kind.turn_ms, false);
                    print_attack(c, kind.name);
                    ASSERT_EQ(c.clicks, 16 * static_cast<int>(seeds.size()));
                    const double share = share_of(c);
                    if (!predict) {
                        off_share[k] = share;
                        continue;
                    }
                    ASSERT_EQ(c.no_order, 0);
                    ASSERT_EQ(c.shown_attack, c.clicks);                                      // (the screen takes every click as the attack that it is)
                    ASSERT_TRUE(share >= (kind.turn_ms == 0 ? 80.0 : 60.0));                  // ... and the order is still one when it lands (the ant may leave its tile in the tick that the seal is late by, and a steered ant may have been given another course that the screen did not know yet)
                    ASSERT_TRUE(share >= off_share[k]);
                    if (rtts[k] >= 100) ASSERT_TRUE(share >= off_share[k] + 20.0);
                }
            }
            ASSERT_TRUE(off_share[4] <= 30.0 && off_share[2] <= 75.0);                       // (without it: a plain move for many clicks at 100 ms and for most at 300 ms)
        }
    } TEST_END();

    TEST_CASE("LK12 The Attack Click On A Walking Ant When The Other Player's Ants Are Drawn From The Confirmed Engine (Others = Confirmed): The Ant Is Drawn A Lead Behind The Engine That The Order Meets, So A Click That Names The Tile Where It Is Drawn Misses It Again; A Click That Names The Tile Where The Predicted Engine Has It Is Still An Attack, At Every Round Trip") {
        const std::vector<uint32_t> seeds = table_wanted() ? std::vector<uint32_t>{7u, 8u, 9u, 10u} : std::vector<uint32_t>{7u, 8u};
        const int rtts[4] = {30, 100, 180, 300};
        struct Kind {
            const char* name;
            int turn_ms;
        };
        for (const Kind kind : {Kind{"it walks on", 0}, Kind{"it is steered", 900}}) {
            for (int k = 0; k < 4; ++k) {
                const AttackCell today = attack_row(rtts[k], 30, true, 32, seeds, kind.turn_ms, false, true, 12);
                const AttackCell unaimed = attack_row(rtts[k], 30, true, 32, seeds, kind.turn_ms, true, false, 12);
                const AttackCell aimed = attack_row(rtts[k], 30, true, 32, seeds, kind.turn_ms, true, true, 12);
                print_attack(today, kind.name);
                print_attack(unaimed, kind.name);
                print_attack(aimed, kind.name);
                for (const AttackCell* c : {&today, &unaimed, &aimed}) {
                    ASSERT_EQ(c->clicks, 32 * static_cast<int>(seeds.size()));
                    ASSERT_EQ(c->no_order, 0);
                }
                ASSERT_EQ(aimed.shown_attack, aimed.clicks);                              // (the predicted engine takes every aimed click as the attack that it is)
                ASSERT_TRUE(share_of(today) >= 80.0 && share_of(aimed) >= 80.0);          // today's picture, and this one with the aim: the click is still an attack when the order lands
                ASSERT_TRUE(share_of(aimed) >= share_of(today) - 8.0);                    // ... as often (the order is the same command at the same moment)
                if (rtts[k] >= 100) {
                    ASSERT_TRUE(share_of(unaimed) + 15.0 <= share_of(aimed));             // without the aim the ant has left the tile where it is drawn: the click is a plain move again
                    ASSERT_TRUE(unaimed.shown_attack < unaimed.clicks);                   // (the predicted engine already says so, right after the click)
                }
                const double ceiling[4] = {80.0, 75.0, 45.0, 15.0};                      // what the click was before the prediction existed (69, 56, 38 and 0 %): the drawn ant is as far behind as the screen was
                ASSERT_TRUE(share_of(unaimed) <= ceiling[k]);
            }
        }
    } TEST_END();

    TEST_CASE("LK13 What Looks Odd In A Fight When The Other Player's Ants Are Drawn From The Confirmed Engine: A Blow Of The Player's Own Ant Seems To Land LATE (His Swing Is Drawn At Once, The Other Ant's Reaction A Lead Later) And A Blow Of The Other Player's Ant Seems To Land EARLY (His Own Ant Is Hurt, From The Predicted Engine, A Lead Before The Swing Is Drawn): By The Lead, And By Nothing Else; With No Prediction At All Or In The Plain Picture The Swing And The Reaction Are Together As The Engine Has Them") {
        std::printf("\n    blow (ms from the order to the swing, to the hit points falling, to the flinch; the lead in ticks)");
        for (const int rtt : {30, 100, 180, 300}) {
            for (const bool own : {true, false}) {
                const BlowResult plain = blow_in(rtt, 0, own, 3);
                const BlowResult others = blow_in(rtt, 1, own, 3);
                const BlowResult confirmed = blow_in(rtt, 2, own, 3);
                ASSERT_TRUE(plain.ok && others.ok && confirmed.ok);
                std::printf("\n    rtt %3d, %-22s plain: swing %4d hurt %4d flinch %4d (reaction %+4d)   others confirmed: swing %4d hurt %4d flinch %4d (reaction %+4d)   no prediction: swing %4d hurt %4d (reaction %+4d)   lead %u",
                            rtt, own ? "the player's ant strikes" : "the other's ant strikes", plain.swing_ms, plain.hurt_ms, plain.flinch_ms, plain.hurt_ms - plain.swing_ms, others.swing_ms, others.hurt_ms,
                            others.flinch_ms, others.hurt_ms - others.swing_ms, confirmed.swing_ms, confirmed.hurt_ms, confirmed.hurt_ms - confirmed.swing_ms, others.lead);
                const int lead_ms = static_cast<int>(others.lead) * 50;
                const int plain_gap = plain.hurt_ms - plain.swing_ms;
                const int others_gap = others.hurt_ms - others.swing_ms;
                ASSERT_TRUE(std::abs(plain_gap - (confirmed.hurt_ms - confirmed.swing_ms)) <= 40);          // (the plain picture and the confirmed engine alone draw the blow in one time: a game on one computer's)
                ASSERT_TRUE(std::abs(others_gap - plain_gap - (own ? lead_ms : -lead_ms)) <= 60);           // ... this one is off by the lead: later for the player's blow, earlier for the other's (a frame and a tick of slack)
            }
        }
        std::printf("\n");
    } TEST_END();

    TEST_CASE("LK14 How A Computer Player's Ants Move On The Screen Over A Link Of 150 And 250 ms (Treasure On A Small Map, A Hard Bot, The Person Idle, Every Frame Drawn): With Others = Confirmed They Are Drawn As A Game That Never Predicted Draws Them, Frame By Frame; The Plain Picture Adds The Hops And The Slides Of Its Corrections (Moves Of 8 px And More In A Frame, Some Of Them Against The Way The Ant Was Walking)") {
        const uint32_t minutes = table_wanted() ? 4u : 2u;
        for (const int rtt : {150, 250}) {
            const DrawnMotion plain = drawn_motion(rtt, 0, 5, minutes * 60000u);
            const DrawnMotion others = drawn_motion(rtt, 1, 5, minutes * 60000u);
            const DrawnMotion confirmed = drawn_motion(rtt, 2, 5, minutes * 60000u);
            ASSERT_TRUE(plain.frames > 1000 && others.frames > 1000 && confirmed.frames > 1000);
            const auto per_minute = [](int n, const DrawnMotion& m) { return n / m.minutes; };
            std::printf("\n    rtt %3d, frames in which a Hard bot's ant is drawn 8 px or more from where the last frame drew it, a minute of play (16 px or more; against its walk):  plain %.1f (%.1f; %.1f)   others confirmed %.1f (%.1f; %.1f)   no prediction %.1f (%.1f; %.1f)", rtt,
                        per_minute(plain.hops8, plain), per_minute(plain.hops16, plain), per_minute(plain.backwards, plain), per_minute(others.hops8, others), per_minute(others.hops16, others), per_minute(others.backwards, others),
                        per_minute(confirmed.hops8, confirmed), per_minute(confirmed.hops16, confirmed), per_minute(confirmed.backwards, confirmed));
            std::printf("\n    rtt %3d, what the predicted engine's rebuilds moved of the computer player's and the person's ants (the plain picture slides it, this one never draws it for the computer player): %.1f ants a minute, %.1f px on average, %u px at most", rtt,
                        static_cast<double>(plain.corrections) / plain.minutes, plain.corrections > 0 ? static_cast<double>(plain.correction_px_total) / static_cast<double>(plain.corrections) : 0.0, plain.correction_px_max);
            std::printf("\n    rtt %3d, tiles that went back to what stood on them a second before: plain %d, others confirmed %d, no prediction %d (in %.0f minutes)", rtt, plain.tile_flips, others.tile_flips, confirmed.tile_flips, plain.minutes);
            ASSERT_TRUE(others.backwards <= plain.backwards);                                  // (no correction is drawn: the ants are the confirmed engine's, only ever as it walks them)
        }
        std::printf("\n");
    } TEST_END();

    TEST_CASE("LK16 What The Other Players' Ants Do On The Screen Over Links Of 22 To 210 ms Mean Round Trip (A Hard Computer Player In Treasure, Another Person In Treasure, A Hard Computer Player In A Duel; The Same Match In The Plain Picture And In The Default One): The Default One Draws No Correction Of Another Player's Ant, Not One Slide And Not One Frame Of An Ant That Goes Back, And Draws Them A Lead Behind (A Table: ANTS_LINK_TABLE=1 Plays All Of It; The Default Run Plays One Cell)") {
        struct Link {
            int rtt;
            int jitter;
        };
        const std::vector<Link> links = table_wanted() ? std::vector<Link>{{14, 8}, {48, 16}, {102, 24}, {170, 40}} : std::vector<Link>{{102, 24}};         // (a mean round trip of 22, 64, 126 and 210 ms)
        const std::vector<int> scenes = table_wanted() ? std::vector<int>{0, 1, 2} : std::vector<int>{0};
        const uint32_t minutes = table_wanted() ? 4u : 1u;
        const char* names[3] = {"Treasure, Hard computer player", "Treasure, another person", "duel, Hard computer player"};
        for (const int scene : scenes) {
            for (const Link link : links) {
                const Seen plain = seen_over_link(scene, link.rtt, link.jitter, false, 9, minutes * 60000u);
                const Seen now = seen_over_link(scene, link.rtt, link.jitter, true, 9, minutes * 60000u);
                ASSERT_TRUE(plain.frames > 1000 && now.frames > 1000);
                const auto rate = [](int n, const Seen& m) { return n / m.minutes; };
                std::printf("\n    %-30s mean rtt %3d ms: slides of the other players' ants (8 px or more a minute: the plain picture's corrections, the hops of the engine's own snaps in both) %5.1f -> %.1f, of the default's those that no move of the engine's own ant explains %.1f; frames of one drawn 8 px or more from the last frame's place %5.1f -> %.1f, against its walk %5.1f -> %.1f;"
                            " their own ants' slides %4.1f -> %4.1f; the other players' ants are drawn %3.0f ms (mean) behind the player's own, the plain picture 0",
                            names[scene], link.rtt + link.jitter, rate(plain.slid_others, plain), rate(now.slid_others, now), rate(now.corrected_others, now), rate(plain.hops8_others, plain), rate(now.hops8_others, now), rate(plain.backwards_others, plain),
                            rate(now.backwards_others, now), rate(plain.slid_own, plain), rate(now.slid_own, now), now.lag_ms_sum / std::max(1, now.frames));
                ASSERT_EQ(now.corrected_others, 0);                                             // not one correction of another player's ant is drawn (the hop of its own snap slides, and that is no correction)
                ASSERT_TRUE(now.backwards_others <= plain.backwards_others);                    // ... and none goes back that the plain picture did not
                ASSERT_TRUE(now.lag_ms_sum / now.frames >= 40.0 && plain.lag_ms_sum / plain.frames >= 40.0);            // (the lead: the two engines stand apart, so the check is not empty; the picture of the others is that far behind)
            }
        }
        std::printf("\n");
    } TEST_END();

    // Every frame of a whole match checked against the two engines (the default picture: ants_app/others_view.hpp): what the frame draws of another player is that player's ant in the CONFIRMED engine,
    // field for field and never slid, and what it draws of the player himself is the predicted engine's
    struct Followed {
        int frames{0};
        int others_drawn{0};                // frames of an ant of another player (a person or a computer player, a friend or an enemy)
        int others_off{0};                  // ... that were not the confirmed engine's ant exactly
        int others_slid{0};                 // ... that the glide moved from the confirmed engine's place by a slide that no move of the engine's own ant explains (a correction drawn: there must be none; the hop of a snap is not one)
        int others_hop_slid{0};             // ... that the glide moved by the slide of such a hop (view_glide.hpp, follow)
        SlideCause cause;
        int others_ahead{0};                // ... whose ant stood elsewhere in the predicted engine (what the plain picture would have drawn: the check is not empty)
        int own_drawn{0};
        int own_off{0};                     // frames of an own ant that was not the predicted engine's ant exactly
        int own_slid{0};
        int missing{0};                     // an ant of the confirmed engine (another player's) or of the predicted one (an own) that the world of the frame lacks, or one that it has and no engine does
        int foes_visible{0};                // frames of an ant of another team that the frame draws (Fog of War lets it)
        int peeks_into_fog{0};              // ... of them on ground that the confirmed engine has not explored
        uint8_t foe_from{1};                // the seats from this one up are not the player's friends
        double lag_sum{0.0};                // the ticks that the predicted engine is ahead of the confirmed one, over the frames
        uint32_t lag_max{0};
        static bool same(const sim::AntSnapshot& a, const sim::AntSnapshot& b) {
            return a.id == b.id && a.player_id == b.player_id && a.px == b.px && a.py == b.py && a.tile_x == b.tile_x && a.tile_y == b.tile_y && a.facing == b.facing && a.hp == b.hp && a.state == b.state &&
                   a.action == b.action && a.loco_clip == b.loco_clip && a.loco_frame == b.loco_frame && a.loco_left_ms == b.loco_left_ms && a.is_holding == b.is_holding && a.is_stunned == b.is_stunned;
        }
        void look(Session& s) {
            ++frames;
            const sim::WorldState& drawn = s.app().shown_world();
            const sim::WorldState& predicted = s.app().view_sim().get_world_state();
            const sim::WorldState& confirmed = s.app().sim().get_world_state();
            const uint64_t lag = s.app().view_sim().current_tick() - s.app().sim().current_tick();
            lag_sum += static_cast<double>(lag);
            lag_max = std::max(lag_max, static_cast<uint32_t>(lag));
            const uint8_t me = 0;
            for (const sim::AntSnapshot& a : drawn.ants) {
                int32_t gx = 0;
                int32_t gy = 0;
                const bool slid = s.app().renderer().view_glide().offset_of(a.id, gx, gy);
                if (a.player_id == me) {
                    ++own_drawn;
                    const sim::AntSnapshot* from = Session::ant_in(predicted, a.id);
                    if (from == nullptr || !same(a, *from)) ++own_off;
                    own_slid += slid ? 1 : 0;
                    continue;
                }
                ++others_drawn;
                const sim::AntSnapshot* from = Session::ant_in(confirmed, a.id);
                if (from == nullptr || !same(a, *from)) ++others_off;
                bool began = false;
                const bool unexplained = cause.unexplained(a, gx, gy, slid, began);
                others_slid += unexplained ? 1 : 0;
                others_hop_slid += slid && !unexplained ? 1 : 0;
                const sim::AntSnapshot* ahead = Session::ant_in(predicted, a.id);
                if (ahead != nullptr && !same(a, *ahead)) ++others_ahead;
                if (a.player_id >= foe_from && ants::app::health::ant_drawn(drawn, me, a)) {
                    ++foes_visible;
                    if (!confirmed.is_tile_revealed(a.tile_x, a.tile_y)) ++peeks_into_fog;
                }
            }
            for (const sim::AntSnapshot& a : confirmed.ants) {
                if (a.player_id != me && Session::ant_in(drawn, a.id) == nullptr) ++missing;
            }
            for (const sim::AntSnapshot& a : predicted.ants) {
                if (a.player_id == me && Session::ant_in(drawn, a.id) == nullptr) ++missing;
            }
            for (const sim::AntSnapshot& a : drawn.ants) {
                if (Session::ant_in(a.player_id == me ? predicted : confirmed, a.id) == nullptr) ++missing;
            }
        }
    };

    TEST_CASE("LK15 A Whole Match Followed Frame By Frame In The Picture That The Game Draws (Others = Confirmed, The Default): Two People Play 40 s At RTT 180 With Jitter And At RTT 300 (One Match With Fog), And A Person Plays 40 s Against A Hard Computer Player, And Are Corrected Many Times, And Not One Frame Draws Another Player's Ant Anywhere But Where The Confirmed Engine Has It (Never Corrected, Never Slid, Never On Ground That The Fog Covers), While Every Frame Draws The Player's Own Ants As The Predicted Engine Has Them; The Match Is The Referee's") {
        for (const int rtt : {180, 300}) {
            Params p;
            p.link.rtt_ms = static_cast<uint32_t>(rtt);
            p.link.jitter_ms = 30;
            p.peer_link.rtt_ms = 60;
            p.peer_link.jitter_ms = 20;
            p.fog = rtt == 300;                                                             // (one match with Fog of War, one without)
            p.render = true;
            p.seed = 71;
            Session s(p);
            ASSERT_TRUE(s.begin());
            Followed f;
            s.on_frame_end = [&]() { f.look(s); };
            Players players(s);
            players.play(40000);
            ASSERT_TRUE(players.rounds > 20);
            ASSERT_TRUE(s.app().net()->predicting());
            const net::Prediction::Stats st = s.app().net()->prediction()->stats();
            std::printf("\n    rtt %d: %d frames; the other player's ant: %d drawn, %d off the confirmed engine's, %d slid, %d where the predicted engine has it elsewhere; the player's own: %d drawn, %d off the predicted engine's, %d sliding; %d missing; the predicted engine ahead by %.1f ticks on average (%u at most); %llu rebuilds, %llu for the other's orders",
                        rtt, f.frames, f.others_drawn, f.others_off, f.others_slid, f.others_ahead, f.own_drawn, f.own_off, f.own_slid, f.missing, f.lag_sum / std::max(1, f.frames), f.lag_max,
                        static_cast<unsigned long long>(st.rebuilds), static_cast<unsigned long long>(st.rebuilds_foreign));
            ASSERT_TRUE(f.frames > 1500 && f.others_drawn > 5000 && f.own_drawn > 5000);
            ASSERT_TRUE(st.rebuilds_foreign > 10u);                                         // (the other person's orders were corrections of the predicted engine: the plain picture slides them)
            ASSERT_TRUE(f.others_ahead > 500);                                              // (and the check is not empty: the predicted engine had the other's ants elsewhere in many frames)
            ASSERT_EQ(f.others_off, 0);                                                     // the other player's ant: the confirmed engine's, whole, in every frame ...
            ASSERT_EQ(f.others_slid, 0);                                                    // ... and never corrected (the only slide that it has is the hop of its own snap: SlideCause)
            ASSERT_EQ(f.own_off, 0);                                                        // the player's own ant: the predicted engine's, whole, in every frame
            ASSERT_EQ(f.missing, 0);                                                        // (no ant is missing from the picture, and none is in it that an engine of its owner does not have)
            std::printf(", %d frames of a foe drawn, %d of them on ground that the confirmed engine had not explored", f.foes_visible, f.peeks_into_fog);
            if (!p.fog) ASSERT_TRUE(f.foes_visible > 1000);
            ASSERT_EQ(f.peeks_into_fog, 0);
            s.on_frame_end = nullptr;
            s.quit(1);
            ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 20000));
            s.run_ms(2000);
            ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
            ASSERT_FALSE(s.app().net()->desynced() || s.peer().net.desynced());
        }
        {                                                                                   // the same against a Hard computer player, whose orders the server sends ahead of their turns (hints): the predicted engine takes them, the picture of the computer player's ants does not
            Params p;
            p.seats = 2;
            p.use_peer = false;
            ai::BotSpec bot;
            bot.seat = 1;
            bot.level = ai::Level::Hard;
            p.bots.push_back(bot);
            p.link.rtt_ms = 150;
            p.link.jitter_ms = 30;
            p.render = true;
            p.seed = 73;
            Session s(p);
            ASSERT_TRUE(s.begin());
            Followed f;
            s.on_frame_end = [&]() { f.look(s); };
            Players players(s);
            players.peer = false;
            players.play(40000);
            ASSERT_TRUE(s.app().net()->predicting());
            const net::Prediction::Stats st = s.app().net()->prediction()->stats();
            std::printf("\n    against a Hard computer player, rtt 150: %d frames; its ants: %d drawn, %d off the confirmed engine's, %d slid, %d where the predicted engine has them elsewhere; the player's own: %d drawn, %d off the predicted engine's; %d missing; %llu hints taken",
                        f.frames, f.others_drawn, f.others_off, f.others_slid, f.others_ahead, f.own_drawn, f.own_off, f.missing, static_cast<unsigned long long>(st.hints_accepted));
            ASSERT_TRUE(f.frames > 1500 && f.others_drawn > 5000 && f.own_drawn > 5000);
            ASSERT_TRUE(st.hints_accepted > 10u);                                           // (the server's hints reached the prediction: its engine moved the computer player's ants by them)
            ASSERT_TRUE(f.others_ahead > 500);
            ASSERT_EQ(f.others_off, 0);
            ASSERT_EQ(f.others_slid, 0);
            ASSERT_EQ(f.own_off, 0);
            ASSERT_EQ(f.missing, 0);
            s.on_frame_end = nullptr;
        }
        std::printf("\n");
    } TEST_END();

    TEST_CASE("LK4 The Confirmed Match Is Exact Whatever The Link Does To The Picture: Two People Play 40 s At RTT 180 With Jitter And Spikes, The Prediction Stays On And Loses Nothing, The Application's Engine Ends In The Referee's State, And Its Recording Plays To The Same State") {
        for (const bool predict : {true, false}) {
            Params p;
            p.link.rtt_ms = 180;
            p.link.jitter_ms = 30;
            p.link.spike_ms = 150;
            p.link.spike_per_mille = 6;
            p.peer_link.rtt_ms = 60;
            p.peer_link.jitter_ms = 20;
            p.predict = predict;
            p.seed = 11;
            Session s(p);
            ASSERT_TRUE(s.begin());
            Players players(s);
            players.play(40000);
            ASSERT_TRUE(players.rounds > 20);
            if (predict) {
                ASSERT_TRUE(s.app().net()->prediction() != nullptr);
                const net::Prediction::Stats& st = s.app().net()->prediction()->stats();
                std::printf("\n    the person's rounds %u, orders predicted %llu, lost %llu, rebuilds %llu", players.rounds, static_cast<unsigned long long>(st.commands_predicted),
                            static_cast<unsigned long long>(st.commands_lost), static_cast<unsigned long long>(st.rebuilds));
                ASSERT_TRUE(st.commands_predicted > 20);                                  // (the person's clicks that gave an order: a click on an ant or a hill gives none)
                ASSERT_EQ(st.commands_lost, 0u);                                          // (every order of the person was sealed)
                ASSERT_TRUE(st.rebuilds > 0);
                std::printf("\n    prediction on: %llu orders predicted, %llu rebuilds (%llu foreign, %llu own timing), %llu strikes, %llu cool-downs; sealed at the tick they were applied at %llu, earlier %llu, later %llu; the lead changed %llu times",
                            static_cast<unsigned long long>(st.commands_predicted), static_cast<unsigned long long>(st.rebuilds), static_cast<unsigned long long>(st.rebuilds_foreign), static_cast<unsigned long long>(st.rebuilds_own_timing),
                            static_cast<unsigned long long>(st.over_budget), static_cast<unsigned long long>(st.cooldowns), static_cast<unsigned long long>(st.sealed_exact), static_cast<unsigned long long>(st.sealed_early),
                            static_cast<unsigned long long>(st.sealed_late), static_cast<unsigned long long>(st.lead_changes));
                ASSERT_EQ(st.sealed_exact + st.sealed_early + st.sealed_late + s.app().net()->prediction()->pending_orders(), st.commands_predicted);      // (every order came back in a turn, but the last ones that are still on their way)
                ASSERT_TRUE(st.sealed_early * 5u <= st.commands_predicted);                               // (hardly ever before the tick it was aimed at, even on a link with spikes)
                ASSERT_TRUE(st.lead_changes * 4u <= st.commands_predicted);                               // (the lead does not go up and down with every order: the picture is not moved a tick back and forth)
            } else {
                ASSERT_TRUE(s.app().net()->prediction() == nullptr);
            }
            s.quit(1);
            ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 20000));
            s.run_ms(2000);
            ASSERT_TRUE(s.app().sim().is_match_over());
            ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
            ASSERT_TRUE(s.app().sim().state_hash() == s.peer().sim.state_hash());
            ASSERT_FALSE(s.app().net()->desynced() || s.peer().net.desynced());
            std::string why;
            ASSERT_TRUE(recording_plays_to(s, why));
        }
    } TEST_END();

    TEST_CASE("LK5 Against Computer Players Only: The Person Plays 30 s In A Room Of Three Bots At RTT 100, Their Orders Are Rebuilds (Never A Lost Order), And The Match Ends In The Referee's State With A Recording That Plays To It") {
        Params p;
        p.map = "SHORT.LVL";                                                              // (a match of one minute: it ends by itself, the bots do not quit)
        p.link.rtt_ms = 100;
        p.link.jitter_ms = 20;
        p.seats = 4;
        p.use_peer = false;
        for (uint8_t seat = 1; seat < 4; ++seat) {
            ai::BotSpec bot;
            bot.seat = seat;
            bot.level = ai::Level::Hard;
            p.bots.push_back(bot);
        }
        p.seed = 21;
        Session s(p);
        ASSERT_TRUE(s.begin());
        Players players(s);
        players.peer = false;
        players.play(25000);
        ASSERT_TRUE(players.rounds > 15);
        ASSERT_TRUE(s.app().net()->predicting());
        const net::Prediction::Stats& st = s.app().net()->prediction()->stats();
        ASSERT_EQ(st.commands_lost, 0u);
        ASSERT_TRUE(st.rebuilds_foreign + st.rebuilds_hint > 0);                           // (the bots gave orders: each is learned from its hint, protocol 26, or from its turn)
        ASSERT_TRUE(st.hints_accepted > 10 && st.hints_confirmed > 5);                      // (the server sent every order of a bot ahead of its turn, and the turns confirmed them)
        ASSERT_EQ(st.cooldowns, 0u);
        std::printf("\n    %llu orders, %llu rebuilds (%llu for the bots' orders), %.3f ms the longest\n", static_cast<unsigned long long>(st.commands_predicted), static_cast<unsigned long long>(st.rebuilds),
                    static_cast<unsigned long long>(st.rebuilds_foreign + st.rebuilds_hint), static_cast<double>(st.rebuild_ns_max) / 1.0e6);
        ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 90000));                // (the minute is over)
        s.run_ms(1500);
        ASSERT_TRUE(s.app().sim().is_match_over());
        ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
        std::string why;
        ASSERT_TRUE(recording_plays_to(s, why));
    } TEST_END();

    TEST_CASE("LK6 A Cable Pulled In The Middle Of The Match: The Prediction Is Off While The Application Is Away And Catches Up, It Begins Again From The Confirmed Engine On The New Link, Nothing Is Left Of The Old Link's Orders In The Picture, And The Match Ends In The Referee's State") {
        Params p;
        p.link.rtt_ms = 100;
        p.link.jitter_ms = 20;
        p.peer_link.rtt_ms = 40;
        p.seed = 31;
        Session s(p);
        ASSERT_TRUE(s.begin());
        Players players(s);
        players.play(12000);
        ASSERT_TRUE(s.app().net()->predicting());
        const uint64_t starts = s.app().net()->prediction()->stats().starts;
        ASSERT_TRUE(s.app().net()->prediction()->pending_orders() <= 4u);
        s.cut_app_link();
        ASSERT_TRUE(s.run_until([&]() { return !s.app().net()->predicting(); }, 3000));
        ASSERT_TRUE(&s.app().view_sim() == &s.app().sim());                                  // (the screen shows the confirmed engine while the link is away)
        players.person = false;                                                              // (nobody clicks on a screen that says the link is lost)
        ASSERT_TRUE(s.run_until([&]() { return s.app().net()->phase() == net::NetGame::Phase::Playing && s.app().net()->predicting(); }, 30000));
        ASSERT_EQ(s.app().net()->prediction()->stats().starts, starts + 1);                  // (a new beginning from the confirmed engine on the new link, not the old one resumed)
        ASSERT_EQ(s.app().net()->prediction()->stats().commands_lost, 0u);
        players.person = true;
        players.play(10000);
        ASSERT_TRUE(s.app().net()->predicting());
        s.quit(1);
        ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 20000));
        s.run_ms(2000);
        ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
        ASSERT_FALSE(s.app().net()->desynced());
    } TEST_END();

    // The frames of a session in which a walking ant moves by more than 20 px and no more than 64: where the picture draws it (the engine's place and what is left of its slide), and where a picture
    // without the slide would (the engine's place alone). An order given to a walking ant snaps it by up to 16 px in the engine itself, as the original's does: more than 20 px in one frame is a hop of
    // the prediction; a throw or a teleport is none.
    struct Jumps {
        int drawn{0};
        int engine{0};
        int frames{0};
        int ant_frames{0};
        int sliding{0};             // frames of an ant that is drawn away from where the engine has it ...
        int sliding_far{0};         // ... by 20 px and more (outside the box of 20 px to each side of the engine's place: the HUD picks by the drawn place, view_glide.hpp)
        std::map<uint32_t, std::pair<int32_t, int32_t>> last_drawn;
        std::map<uint32_t, std::pair<int32_t, int32_t>> last_engine;
        void look(Session& s) {
            ++frames;
            for (const auto& a : s.app().shown_world().ants) {                              // (the world that the frame draws: the predicted engine's, or with others = confirmed the other players' ants from the confirmed one)
                int32_t gx = 0;
                int32_t gy = 0;
                s.app().renderer().view_glide().offset_of(a.id, gx, gy);
                ++ant_frames;
                if (gx != 0 || gy != 0) {
                    ++sliding;
                    if (std::max(std::abs(gx), std::abs(gy)) >= 20) ++sliding_far;
                }
                const std::pair<int32_t, int32_t> at{a.px, a.py};
                const std::pair<int32_t, int32_t> shown{a.px + gx, a.py + gy};
                const auto was_drawn = last_drawn.find(a.id);
                if (was_drawn != last_drawn.end() && a.state == sim::UnitState::Walking) {
                    const auto hop = [](const std::pair<int32_t, int32_t>& x, const std::pair<int32_t, int32_t>& y) { return std::max(std::abs(x.first - y.first), std::abs(x.second - y.second)); };
                    const int32_t by_picture = hop(shown, was_drawn->second);
                    const int32_t by_engine = hop(at, last_engine[a.id]);
                    drawn += by_picture > 20 && by_picture <= 64 ? 1 : 0;
                    engine += by_engine > 20 && by_engine <= 64 ? 1 : 0;
                }
                last_drawn[a.id] = shown;
                last_engine[a.id] = at;
            }
        }
    };

    TEST_CASE("LK7 In The Plain Picture (--others predicted) What The Screen Shows When The Other Person's Orders Arrive: Their Ants Are Put Where The Engine Has Them By A Slide, Not A Jump (At RTT 180 The Frames In Which An Ant Of The Picture Moves By More Than 20 px Are None), And Without The Slide There Are Such Frames") {
        // (the plain picture, --others predicted, expires with v0.22.3 at the latest: this test goes with it, AGENTS.md rule 9)
        Params p;
        p.others_confirmed = false;
        p.link.rtt_ms = 180;
        p.link.jitter_ms = 30;
        p.peer_link.rtt_ms = 60;
        p.render = true;
        p.seed = 41;
        Session s(p);
        ASSERT_TRUE(s.begin());
        Jumps jumps;
        s.on_frame_end = [&]() { jumps.look(s); };
        Players players(s);
        players.person = false;
        players.play(30000);
        ASSERT_TRUE(jumps.frames > 1500);
        ASSERT_TRUE(s.app().net()->prediction()->stats().rebuilds_foreign > 10u);           // (the other person's orders were corrected)
        std::printf("\n    frames in which a walking ant of the picture moved by more than 20 px: %d without the slide, %d with it; of %d frames of an ant, %d drew it sliding, %d of them 20 px and more from where the engine has it (the pointer picks it where it is drawn)\n",
                    jumps.engine, jumps.drawn, jumps.ant_frames, jumps.sliding, jumps.sliding_far);
        ASSERT_TRUE(jumps.sliding > 0);
        ASSERT_TRUE(jumps.engine > 3);
        ASSERT_EQ(jumps.drawn, 0);
    } TEST_END();

    TEST_CASE("LK10 When The Prediction Stops And Begins Again (A Page That Is Hidden And Shown, As A Pause Or A Link That Was Away Do) The Screen Changes Engine And Every Walking Ant Stands A Lead Of Ticks Elsewhere: The Picture Slides Them (No Frame In Which An Ant Moves By More Than 20 px), And Without The Slide Every Change Is A Jump; The Match Is The Referee's (In Both Pictures: In The Default One Only The Player's Own Ants Change Place At A Change, The Other Players' Never)") {
        for (const bool others : {false, true}) {
            Params p;
            p.others_confirmed = others;
            p.link.rtt_ms = 300;                                                             // (a lead of about seven ticks: 28 px of walking)
            p.link.jitter_ms = 20;
            p.peer_link.rtt_ms = 60;
            p.render = true;
            p.seed = 43;
            Session s(p);
            ASSERT_TRUE(s.begin());
            Jumps jumps;
            s.on_frame_end = [&]() { jumps.look(s); };
            Players players(s);
            players.play(6000);                                                              // (both people have ants walking, and the lead has learned the link)
            ASSERT_TRUE(s.app().net()->predicting());
            ASSERT_TRUE(s.app().net()->prediction()->lead_ticks() >= 6u);
            const int before = jumps.engine;
            const uint64_t starts = s.app().net()->prediction()->stats().starts;
            int changes = 0;
            for (int round = 0; round < 6; ++round) {
                s.app().set_page_hidden(true);                                               // the prediction stops with the next update: the screen shows the confirmed engine
                ASSERT_TRUE(s.run_until([&]() { return !s.app().net()->predicting(); }, 500));
                players.play(700);
                const double clock_before_show = s.app().net_clock_ms();
                s.app().set_page_hidden(false);                                              // ... and begins again with the next tick
                ASSERT_EQ(s.app().net_clock_ms(), clock_before_show);                        // (showing the page is no time that the match did not have: the virtual clock is the application's only one)
                ASSERT_TRUE(s.run_until([&]() { return s.app().net()->predicting(); }, 2000));
                players.play(1800);
                changes += 2;
            }
            ASSERT_EQ(s.app().net()->prediction()->stats().starts, starts + 6);
            std::printf("\n    %s picture, %d changes of the shown engine: frames in which a walking ant moved by more than 20 px: %d without the slide (%d before the changes), %d with it\n", others ? "default" : "plain", changes,
                        jumps.engine, before, jumps.drawn);
            ASSERT_TRUE(jumps.engine >= before + 6);                                         // (without the slide the changes are jumps)
            ASSERT_EQ(jumps.drawn, 0);
            s.on_frame_end = nullptr;
            s.quit(1);
            ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 20000));
            s.run_ms(2000);
            ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
            ASSERT_FALSE(s.app().net()->desynced());
        }
    } TEST_END();

    // How the slides of a match are paced: every slide of an ant is followed from the frame that shows it begin to the frame that shows it over. No ant is carried faster than it walks (80 px/s: a slide of
    // 16 to 56 px takes at least 12.5 ms a pixel), and none lasts under 150 ms or over 700 ms, whoever's ant it is and whatever moves it (view_glide.hpp)
    struct Paces {
        struct Last {
            int32_t x{0};
            int32_t y{0};
            double peak{0.0};
            uint32_t began{0};
            uint32_t seen{0};           // the time of the last frame
        };
        std::map<uint32_t, Last> last;
        int slides{0};
        int measured{0};                    // the slides of 16 to 64 px that were followed from the start to the end (a second shift that comes while an ant slides begins the sum again: not one of these)
        int too_fast{0};                    // ... of 16 to 56 px (what a walk carries in the longest slide) that took less than a walk would
        int out_of_bounds{0};               // ... that lasted under 150 ms or over 700 ms (with a frame of 17 ms at each end)
        double longest{0.0};
        void look(Session& s) {
            const uint32_t now = s.now();
            for (const auto& a : s.app().shown_world().ants) {                              // (the world that the frame draws: the predicted engine's, or with others = confirmed the other players' ants from the confirmed one)
                int32_t gx = 0;
                int32_t gy = 0;
                s.app().renderer().view_glide().offset_of(a.id, gx, gy);
                Last& l = last[a.id];
                const double mag = std::hypot(static_cast<double>(gx), static_cast<double>(gy));
                const double was = std::hypot(static_cast<double>(l.x), static_cast<double>(l.y));
                const double sheds = ViewGlide::kWalkPxPerSecond * static_cast<double>(now - l.seen) / 1000.0 + 2.0;     // (the most that a slide at the pace of a walk sheds between two frames, and a pixel of rounding on each side)
                l.seen = now;
                const bool merged = was > 0.0 && was - mag > sheds;                              // (a second shift came and took off more than the slide does: the sum is a new, shorter slide, or none)
                if (mag > 0.0 && (was == 0.0 || mag > was + 1.5 || merged)) {                    // a slide begins (or a new shift begins it again)
                    l.peak = mag;
                    l.began = now;
                    ++slides;
                } else if (mag > 0.0) {
                    l.peak = std::max(l.peak, mag);
                } else if (was > 0.0 && !merged && l.peak >= 16.0 && l.peak <= 64.0) {          // it is over (one that was taken off at once is not: the sum was none, or past two tiles)
                    ++measured;
                    const double took = static_cast<double>(now - l.began);
                    longest = std::max(longest, took);
                    if (l.peak <= 56.0 && took + 34.0 < 12.5 * l.peak) ++too_fast;                  // (a frame of 17 ms at each end)
                    if (took + 34.0 < ViewGlide::kMinDurationMs || took > ViewGlide::kMaxDurationMs + 34.0) ++out_of_bounds;
                }
                l.x = gx;
                l.y = gy;
            }
        }
    };

    TEST_CASE("LK11 No Slide Is Faster Than A Walk, Whatever Moves The Ant: The Other Person's Orders And The Prediction Stopping And Beginning Again (A Slide Of 16 To 56 px Takes At Least 12.5 ms A Pixel, And None Lasts Under 150 Or Over 700 ms), And The Match Is The Referee's (In Both Pictures; In The Default One Only The Player's Own Ants Slide)") {
        for (const bool others : {false, true}) {
            Params p;
            p.others_confirmed = others;
            p.link.rtt_ms = 300;                                                                 // (a lead of about seven ticks: 28 px of walking at every stop and start)
            p.link.jitter_ms = 20;
            p.peer_link.rtt_ms = 60;
            p.render = true;
            p.seed = 43;
            Session s(p);
            ASSERT_TRUE(s.begin());
            Paces paces;
            s.on_frame_end = [&]() { paces.look(s); };
            Players players(s);
            players.play(6000);
            ASSERT_TRUE(s.app().net()->predicting());
            for (int round = 0; round < 6; ++round) {
                s.app().set_page_hidden(true);
                ASSERT_TRUE(s.run_until([&]() { return !s.app().net()->predicting(); }, 500));
                players.play(700);
                s.app().set_page_hidden(false);
                ASSERT_TRUE(s.run_until([&]() { return s.app().net()->predicting(); }, 2000));
                players.play(1800);
            }
            players.play(8000);
            std::printf("\n    slides: %d, of them %d of 16 to 64 px followed from the start to the end (the longest %.0f ms), %d of those faster than a walk, %d out of 150 to 700 ms\n", paces.slides, paces.measured, paces.longest, paces.too_fast, paces.out_of_bounds);
            ASSERT_TRUE(paces.measured >= 8);                                                    // (the rule was asked)
            ASSERT_EQ(paces.too_fast, 0);
            ASSERT_EQ(paces.out_of_bounds, 0);
            s.on_frame_end = nullptr;
            s.quit(1);
            ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 20000));
            s.run_ms(2000);
            ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
            ASSERT_FALSE(s.app().net()->desynced());
        }
    } TEST_END();

    TEST_CASE("LK8 Four People In Teams With Fog Of War (Seats 0 And 1 Against 2 And 3, Round Trips Of 120 And 50 ms): Everything The Picture Of The Application Explores Is Explored By The Confirmed Engine When It Gets To That Tick, The Orders Of Its Ally Are Corrections Like Any Other's, And The Match Ends In The Referee's State With A Recording That Plays To It (In Both Pictures: In The Plain One Because The Predicted Engine Adopts The Confirmed Engine's Explored Ground, In The Default One Because The Picture Takes It From There)") {
        for (const bool others : {false, true}) {
            Params p;
            p.others_confirmed = others;
            p.map = "SHORT.LVL";
            p.link.rtt_ms = 120;
            p.link.jitter_ms = 30;
            p.peer_link.rtt_ms = 50;
            p.peer_link.jitter_ms = 10;
            p.seats = 4;
            p.peers = 3;
            p.fog = true;
            p.teams = sim::StartTeams{true, 0, 1};
            p.render = true;
            p.seed = 61;
            Session s(p);
            ASSERT_TRUE(s.begin());
            std::map<uint64_t, std::vector<uint8_t>> shown;                                      // the ground that the picture had explored at each tick that it showed
            std::map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> shown_enemies;           // the tiles of the ants of other teams that the picture drew at each tick that it showed
            int checked = 0;
            int explored_by_picture_only = 0;
            int enemies_drawn_early = 0;
            int enemies_drawn_unexplored = 0;
            int enemy_frames = 0;
            s.on_frame_end = [&]() {
                const sim::SimulationEngine& view = s.app().view_sim();
                const sim::WorldState& drawn = s.app().shown_world();                              // (the world that the frame draws: with others = confirmed the other players' ants are the confirmed engine's)
                const uint64_t tick = view.current_tick();
                if (shown.find(tick) == shown.end()) {
                    shown[tick] = drawn.fog_revealed;
                    auto& tiles = shown_enemies[tick];
                    for (const auto& a : drawn.ants) {
                        if (a.player_id > 1 && ants::app::health::ant_drawn(drawn, 0, a)) tiles.emplace_back(a.tile_x, a.tile_y);       // (seats 2 and 3: the ally, seat 1, is always drawn)
                    }
                }
                for (const auto& a : drawn.ants) {                                                 // an ant of another team that the frame draws stands on ground that the confirmed engine has explored THIS very moment (the picture is no tick ahead of it)
                    if (a.player_id > 1 && ants::app::health::ant_drawn(drawn, 0, a)) {
                        ++enemy_frames;
                        if (!s.app().sim().get_world_state().is_tile_revealed(a.tile_x, a.tile_y)) ++enemies_drawn_unexplored;
                    }
                }
                const uint64_t now_tick = s.app().sim().current_tick();
                const auto it = shown.find(now_tick);                                              // the confirmed engine has got to a tick that the picture showed some frames ago
                if (it == shown.end()) return;
                const sim::WorldState& confirmed_world = s.app().sim().get_world_state();
                const std::vector<uint8_t>& confirmed = confirmed_world.fog_revealed;
                ++checked;
                for (size_t i = 0; i < it->second.size() && i < confirmed.size(); ++i) {
                    if (it->second[i] != 0 && confirmed[i] == 0) ++explored_by_picture_only;
                }
                for (const auto& tile : shown_enemies[now_tick]) {                                 // a tile that the picture drew an ant of another team on: the confirmed engine has explored it by now
                    if (!confirmed_world.is_tile_revealed(tile.first, tile.second)) ++enemies_drawn_early;
                }
                shown.erase(shown.begin(), std::next(it));
                shown_enemies.erase(shown_enemies.begin(), shown_enemies.find(now_tick) == shown_enemies.end() ? shown_enemies.begin() : std::next(shown_enemies.find(now_tick)));
            };
            Players players(s);
            players.play(25000);
            ASSERT_TRUE(players.rounds > 10);
            ASSERT_TRUE(checked > 300);
            std::printf("\n    %s picture: ground explored by the picture and not by the engine at that tick: %d tiles over %d frames; enemy ants drawn on ground that the engine had not explored: %d\n", others ? "default" : "plain", explored_by_picture_only, checked, enemies_drawn_early);
            std::printf("    frames of an enemy ant drawn: %d, on ground that the confirmed engine had not explored at that moment: %d\n", enemy_frames, enemies_drawn_unexplored);
            ASSERT_EQ(enemies_drawn_early, 0);
            ASSERT_EQ(explored_by_picture_only, 0);                                                // (the picture never explored ground that the engine had not explored when it got to that tick)
            ASSERT_TRUE(enemy_frames > 0);                                                         // (the fog was lifted in the match: the check was asked)
            ASSERT_EQ(enemies_drawn_unexplored, 0);                                                // (nothing of another team is drawn that the confirmed engine does not show: not a tick ahead of it either)
            ASSERT_TRUE(s.app().net()->predicting());
            ASSERT_EQ(s.app().net()->prediction()->stats().commands_lost, 0u);
            ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 90000));
            s.run_ms(1500);
            ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
            for (size_t k = 0; k < s.peer_count(); ++k) ASSERT_TRUE(s.app().sim().state_hash() == s.peer(k).sim.state_hash());
            ASSERT_FALSE(s.app().net()->desynced());
            std::string why;
            ASSERT_TRUE(recording_plays_to(s, why));
        }
    } TEST_END();

    TEST_CASE("LK9 Soak (ANTS_LINK_SOAK=N sessions of a few minutes over links of every kind, with bots, teams and fog where they go together; skipped without it): The Confirmed Match Is The Referee's, The Recording Plays To It, Nothing Is Lost, The Prediction Never Gives Up, And A Rebuild Is Cheap") {
        const char* soak = std::getenv("ANTS_LINK_SOAK");
        if (soak == nullptr || std::atoi(soak) <= 0) return;
        const int sessions = std::atoi(soak);
        uint64_t worst_rebuild_ns = 0;
        uint64_t soak_sealed[3] = {0, 0, 0};
        uint64_t soak_lead_changes = 0;
        for (int n = 0; n < sessions; ++n) {
            std::mt19937 engine(1000u + static_cast<uint32_t>(n));
            const auto pick = [&engine](uint32_t below) { return static_cast<uint32_t>(engine() % below); };      // (a number below `below`: the engine's own type is wider than 32 bits on some systems)
            Params p;
            const bool long_game = n % 3 == 0 && n % 2 == 1;                                  // (some two-person matches run for four minutes and end with the other person's Quit)
            p.map = long_game ? "GRASS.LVL" : "SHORT.LVL";
            const uint32_t rtts[6] = {20, 60, 100, 180, 250, 300};
            p.link.rtt_ms = rtts[pick(6)];
            p.link.jitter_ms = pick(4) == 0 ? 0u : 10u + pick(50);
            if (pick(3) == 0) {
                p.link.spike_ms = 120 + pick(200);
                p.link.spike_per_mille = 3 + pick(12);
            }
            p.peer_link.rtt_ms = rtts[pick(6)];
            p.peer_link.jitter_ms = pick(40);
            p.seed = 2000u + static_cast<uint32_t>(n);
            const int kind = n % 3;                                                           // 0: two people; 1: one person and three bots; 2: four people, teams, fog
            if (kind == 1) {
                p.seats = 4;
                p.use_peer = false;
                for (uint8_t seat = 1; seat < 4; ++seat) {
                    ai::BotSpec bot;
                    bot.seat = seat;
                    bot.level = seat == 1 ? ai::Level::Hard : ai::Level::Medium;
                    p.bots.push_back(bot);
                }
            } else if (kind == 2) {
                p.seats = 4;
                p.peers = 3;
                p.fog = true;
                p.teams = sim::StartTeams{true, 0, 1};
            }
            Session s(p);
            ASSERT_TRUE(s.begin());
            Players players(s);
            players.peer = kind != 1;
            players.play(long_game ? 240000 : 50000);
            ASSERT_TRUE(s.app().net()->predicting() || s.app().net()->prediction_cooling_down() == false);
            const net::Prediction::Stats st = s.app().net()->prediction()->stats();
            ASSERT_EQ(st.commands_lost, 0u);
            ASSERT_EQ(st.cooldowns, 0u);
            worst_rebuild_ns = std::max(worst_rebuild_ns, st.rebuild_ns_max);
            if (long_game) s.quit(1);
            ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 120000));
            s.run_ms(1500);
            ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
            ASSERT_FALSE(s.app().net()->desynced());
            std::string why;
            ASSERT_TRUE(recording_plays_to(s, why));
            std::printf("\n    soak %d: kind %d, rtt %u jit %u spikes %u/%u: %llu orders (sealed at their tick %llu, earlier %llu, later %llu), the lead changed %llu times, %llu rebuilds, longest %.2f ms", n, kind, p.link.rtt_ms,
                        p.link.jitter_ms, p.link.spike_ms, p.link.spike_per_mille, static_cast<unsigned long long>(st.commands_predicted), static_cast<unsigned long long>(st.sealed_exact),
                        static_cast<unsigned long long>(st.sealed_early), static_cast<unsigned long long>(st.sealed_late), static_cast<unsigned long long>(st.lead_changes), static_cast<unsigned long long>(st.rebuilds),
                        static_cast<double>(st.rebuild_ns_max) / 1.0e6);
            soak_sealed[0] += st.sealed_exact;
            soak_sealed[1] += st.sealed_early;
            soak_sealed[2] += st.sealed_late;
            soak_lead_changes += st.lead_changes;
        }
        std::printf("\n    soak: the longest rebuild of all %d sessions %.2f ms; orders sealed at the tick they were applied at %llu, earlier %llu, later %llu; the lead changed %llu times\n", sessions,
                    static_cast<double>(worst_rebuild_ns) / 1.0e6, static_cast<unsigned long long>(soak_sealed[0]), static_cast<unsigned long long>(soak_sealed[1]), static_cast<unsigned long long>(soak_sealed[2]),
                    static_cast<unsigned long long>(soak_lead_changes));
        ASSERT_TRUE(soak_sealed[1] * 10u <= soak_sealed[0] + soak_sealed[1] + soak_sealed[2]);            // (over every kind of link: under a tenth of the orders land before the tick that they were aimed at)
    } TEST_END();

    TEST_CASE("LK17 The Hop Of A Re-Order Is Drawn As A Slide, Not A Jump: A Walking Ant That Is Given A New Order (A Goal Further On, Behind, One Row Up) Is Put On The Middle Of Its Tile By The Engine, Up To Half A Tile At Once, As The Original Does, And The Picture Never Moves It By More Than A Step Of Its Walk Between Two Frames, At Every Phase Of The Walk, In A Game Of One Machine And Over A Link At 2 And 20 ms, For The Player's Own Ant (Clicked) And For Another Player's; The Pointer Picks The Sliding Ant Where It Is Drawn") {
        struct Scene {
            const char* name;
            bool local;
            uint32_t rtt;
            uint8_t seat;
        };
        const char* kinds[3] = {"a goal further on", "a goal behind", "a goal one row up"};
        const std::vector<Scene> scenes = table_wanted() ? std::vector<Scene>{{"a game of one machine", true, 0, 0}, {"a link of 2 ms", false, 2, 0}, {"a link of 20 ms", false, 20, 0}, {"another player's ant over 20 ms", false, 20, 1}}
                                                          : std::vector<Scene>{{"a game of one machine", true, 0, 0}, {"a link of 20 ms", false, 20, 0}, {"another player's ant over 20 ms", false, 20, 1}};
        struct Played {
            Scene scene;
            int kind;
            HopSweep r;
        };
        std::vector<Played> played;
        for (const Scene& scene : scenes) {
            for (int kind = 0; kind < 3; ++kind) {
                Params p;
                p.map = "GRASS.LVL";
                p.render = true;
                p.local = scene.local;
                p.use_peer = !scene.local;
                p.link.rtt_ms = scene.rtt;
                p.peer_link.rtt_ms = 20;
                played.push_back(Played{scene, kind, hop_sweep(p, scene.seat, kind)});
                const HopSweep& r = played.back().r;
                std::printf("\n    %s, %s: %d runs; the engine snapped the ant by 8 px or more in %d (by %d px at most); a frame moves the drawn ant by %d px at most in its walk, by %d px after the order; the pointer picked the sliding ant %d of %d times (and missed it on the engine's side %d of %d times)",
                            scene.name, kinds[kind], r.runs, r.hops, r.snap_worst, r.walk_worst, r.after_worst, r.pick_ok, r.pick_tried, r.pick_foil_ok, r.pick_tried);
            }
        }
        for (const Played& m : played) {
            const HopSweep& r = m.r;
            ASSERT_TRUE(r.runs >= 14);
            ASSERT_TRUE(r.hops >= 3 && r.snap_worst >= 12);                                    // (the engine does snap the ant: the half tile is there, the check is not empty)
            ASSERT_TRUE(r.walk_worst <= 4);                                                    // (a walk: a step of 4 px a frame at most)
            ASSERT_TRUE(r.after_worst <= r.walk_worst + 1);                                    // the order's hop is not drawn: no frame moves the ant by more than its walk (and a pixel for the slide's rounding)
            ASSERT_EQ(r.unclean, 0);                                                           // (and a second later nothing of it is left)
            if (m.kind == 0) ASSERT_EQ(r.back_worst, 0);                                       // an ant that walks on and is given a goal further on is never drawn stepping BACK, whatever the engine's snap does: the picture holds it until the engine's ant has passed
            ASSERT_EQ(r.pick_ok, r.pick_tried);                                                // the pointer picks the ant where the frame draws it ...
            ASSERT_EQ(r.pick_foil_ok, r.pick_tried);                                           // ... and not where the engine has it
            if (m.scene.seat == 0) ASSERT_TRUE(r.pick_tried >= 3);                             // (the click of the player's own ant: slides of 8 px and more were met)
        }
        std::printf("\n");
    } TEST_END();

    TEST_CASE("LK18 The Player's Own Corrections Per Order (The Cause Of A Rubber Band In One's Own Ants): A Person Orders His Ants For 75 s After A Warm-Up Of 15 s Over Round Trips Of 2 To 210 ms With Jitter, Nobody Else Orders; The Orders That The Host Seals At Another Tick Than The One The Screen Applied Them At, The Rebuilds That They Cause And What The Rebuilds Move On The Screen Are Counted; The Host Seals An Order At The Tick It Is Stamped With, So Hardly One Is Corrected") {
        struct Row {
            uint32_t rtt;
            uint32_t jitter;
        };
        const std::vector<Row> rows = table_wanted() ? std::vector<Row>{{2, 0}, {30, 10}, {60, 20}, {100, 30}, {150, 30}, {210, 30}, {210, 60}} : std::vector<Row>{{2, 0}, {60, 20}, {150, 30}, {210, 30}};
        std::printf("\n    own orders after a warm-up of 15 s (a person's clicks, nobody else orders): orders / sealed at another tick than applied / rebuilds / corrections that moved an ant / ants moved (px mean) / lead at the end\n");
        for (const Row& row : rows) {
            Params p;
            p.link.rtt_ms = row.rtt;
            p.link.jitter_ms = row.jitter;
            p.seed = 18u + seed_offset() + row.rtt;
            Session s(p);
            ASSERT_TRUE(s.begin());
            Players players(s);
            players.peer = false;
            players.play(15000);
            ASSERT_TRUE(s.app().net()->prediction() != nullptr);
            net::Prediction& pred = *s.app().net()->prediction();
            pred.set_measure_corrections(true);
            const net::Prediction::Stats warm = pred.stats();
            players.play(75000);
            const net::Prediction::Stats& st = pred.stats();
            const uint64_t orders = st.commands_predicted - warm.commands_predicted;
            const uint64_t late = (st.sealed_late - warm.sealed_late) + (st.sealed_early - warm.sealed_early);
            const uint64_t rebuilds = st.rebuilds - warm.rebuilds;
            std::printf("    rtt %3u jitter %2u: %3llu orders / %3llu later or earlier (%4.1f %%) / %3llu rebuilds / %3llu moved an ant / %.1f px mean / lead %u\n", row.rtt, row.jitter,
                        static_cast<unsigned long long>(orders), static_cast<unsigned long long>(late), orders != 0 ? 100.0 * static_cast<double>(late) / static_cast<double>(orders) : 0.0, static_cast<unsigned long long>(rebuilds),
                        static_cast<unsigned long long>(st.corrections_visible), st.ants_moved != 0 ? static_cast<double>(st.move_px_total) / static_cast<double>(st.ants_moved) : 0.0, pred.lead_ticks());
            ASSERT_TRUE(orders >= 30);
            ASSERT_EQ(st.commands_lost, 0u);
            ASSERT_TRUE(st.order_acks + 8u >= st.commands_predicted);                         // (the host answered the orders: the lead was learned from the answers)
            ASSERT_TRUE(late * 10u <= orders);                                                // at most one order in ten is sealed at another tick than the screen applied it at, on every link (the first orders of a match are the late ones: before the lead has the lag)
            if (row.jitter <= 20) ASSERT_TRUE(late * 25u <= orders);                          // and at most one in 25 where the link scatters its packets by a fifth of a tick or less
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " PREDICTION SHAPED LINKS TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

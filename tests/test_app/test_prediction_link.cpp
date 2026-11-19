// Tests of the prediction of one's own orders through the REAL input path over links with a delay and jitter (include/ants_net/prediction.hpp, docs/NETWORK_PORT.md "Prediction of one's own orders").
//
// What a person feels in a room at a round trip of 30, 100, 180 or 300 ms is not what the engine can say about an order: the order of a click lands `lag` ticks after the screen that the click was aimed
// at, and two plays of the original game depend on that: "humping" (a re-order of a walking ant a moment after it crosses a tile line, which pays on mud inside a window of about 150 ms) and the
// attack click on a walking ant (the order names the ant's tile, and an ant that has left it is no longer the target). Here a headless Application plays a room of a real RoomManager over links of a
// chosen round trip and jitter on a virtual clock (tests/test_app/shaped_link_fixtures.hpp), and a person at it clicks with SDL mouse events, deciding from what the screen shows (view_sim), with
// the prediction on and off:
//   - humping on mud: the pixels that the re-orders add to a walk of 9 s (the confirmed engine, the one that decides), against the same walk with one order;
//   - the attack click on an enemy ant that walks: the share of the clicks that are still an attack when the order lands.
// The numbers are printed (ANTS_LINK_TABLE=1 plays the whole grid); the assertions hold what the prediction promises.
#include <SDL.h>

#include "shaped_link_fixtures.hpp"
#include "ants_app/health_changes.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "../common/test_assert.hpp"

#include <algorithm>
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
    bool others{false};             // the other player's ants are drawn from the confirmed engine (the person looks at what the pointer picks: ants_app/others_view.hpp)
    bool aim{true};                 // ... and the click names the tile where the predicted engine has the ant
    int clicks{0};
    int shown_attack{0};
    int landed_attack{0};
    int landed_move{0};
    int no_order{0};                // clicks that gave no order at all (the pointer was not on an ant that the screen's engine could pick)
};

// A cell: `phases` clicks in one match of one link, the click `i * 400 / phases` ms after the third tile of the walk
AttackCell attack_cell(int rtt, int jitter, bool predict, int phases, uint32_t seed, int turn_ms = 0, bool others = false, bool aim = true, int spread_ms = 0) {
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
AttackCell attack_row(int rtt, int jitter, bool predict, int phases, const std::vector<uint32_t>& seeds, int turn_ms = 0, bool others = false, bool aim = true, int spread_ms = 0) {
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

    TEST_CASE("LK3 The Attack Click On A Walking Ant: The Order Names The Tile Of The Ant As The Screen Shows It, So That With The Prediction (The Screen Is The Engine The Order Lands In) It Is Still An Attack When It Lands, At Every Round Trip, For An Ant That Walks On And For One That Its Owner Steers; Without It The Ant Has Left The Tile Before The Order Lands, And The Click Is A Plain Move; A Game On This Computer Is The Reference") {
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
                    const AttackCell c = attack_row(rtts[k], 30, predict, 16, seeds, kind.turn_ms);
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
        ASSERT_TRUE(st.rebuilds_foreign > 0);                                              // (the bots gave orders)
        ASSERT_EQ(st.cooldowns, 0u);
        std::printf("\n    %llu orders, %llu rebuilds (%llu for the bots' orders), %.3f ms the longest\n", static_cast<unsigned long long>(st.commands_predicted), static_cast<unsigned long long>(st.rebuilds),
                    static_cast<unsigned long long>(st.rebuilds_foreign), static_cast<double>(st.rebuild_ns_max) / 1.0e6);
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
            for (const auto& a : s.app().view_sim().get_world_state().ants) {
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

    TEST_CASE("LK7 What The Screen Shows When The Other Person's Orders Arrive: Their Ants Are Put Where The Engine Has Them By A Slide, Not A Jump (At RTT 180 The Frames In Which An Ant Of The Picture Moves By More Than 20 px Are None), And Without The Slide There Are Such Frames") {
        Params p;
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

    TEST_CASE("LK10 When The Prediction Stops And Begins Again (A Page That Is Hidden And Shown, As A Pause Or A Link That Was Away Do) The Screen Changes Engine And Every Walking Ant Stands A Lead Of Ticks Elsewhere: The Picture Slides Them (No Frame In Which An Ant Moves By More Than 20 px), And Without The Slide Every Change Is A Jump; The Match Is The Referee's") {
        Params p;
        p.link.rtt_ms = 300;                                                                 // (a lead of about seven ticks: 28 px of walking)
        p.link.jitter_ms = 20;
        p.peer_link.rtt_ms = 60;
        p.render = true;
        p.seed = 43;
        Session s(p);
        ASSERT_TRUE(s.begin());
        Jumps jumps;
        s.on_frame_end = [&]() { jumps.look(s); };
        Players players(s);
        players.play(6000);                                                                  // (both people have ants walking, and the lead has learned the link)
        ASSERT_TRUE(s.app().net()->predicting());
        ASSERT_TRUE(s.app().net()->prediction()->lead_ticks() >= 6u);
        const int before = jumps.engine;
        const uint64_t starts = s.app().net()->prediction()->stats().starts;
        int changes = 0;
        for (int round = 0; round < 6; ++round) {
            s.app().set_page_hidden(true);                                                   // the prediction stops with the next update: the screen shows the confirmed engine
            ASSERT_TRUE(s.run_until([&]() { return !s.app().net()->predicting(); }, 500));
            players.play(700);
            s.app().set_page_hidden(false);                                                  // ... and begins again with the next tick
            ASSERT_TRUE(s.run_until([&]() { return s.app().net()->predicting(); }, 2000));
            players.play(1800);
            changes += 2;
        }
        ASSERT_EQ(s.app().net()->prediction()->stats().starts, starts + 6);
        std::printf("\n    %d changes of the shown engine: frames in which a walking ant moved by more than 20 px: %d without the slide (%d before the changes), %d with it\n", changes, jumps.engine, before, jumps.drawn);
        ASSERT_TRUE(jumps.engine >= before + 6);                                             // (without the slide the changes are jumps)
        ASSERT_EQ(jumps.drawn, 0);
        s.on_frame_end = nullptr;
        s.quit(1);
        ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 20000));
        s.run_ms(2000);
        ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
        ASSERT_FALSE(s.app().net()->desynced());
    } TEST_END();

    TEST_CASE("LK8 Four People In Teams With Fog Of War (Seats 0 And 1 Against 2 And 3, Round Trips Of 120 And 50 ms): Everything The Picture Of The Application Explores Is Explored By The Confirmed Engine When It Gets To That Tick, The Orders Of Its Ally Are Corrections Like Any Other's, And The Match Ends In The Referee's State With A Recording That Plays To It") {
        Params p;
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
        s.on_frame_end = [&]() {
            const sim::SimulationEngine& view = s.app().view_sim();
            const uint64_t tick = view.current_tick();
            if (shown.find(tick) == shown.end()) {
                shown[tick] = view.get_world_state().fog_revealed;
                auto& tiles = shown_enemies[tick];
                for (const auto& a : view.get_world_state().ants) {
                    if (a.player_id > 1 && ants::app::health::ant_drawn(view.get_world_state(), 0, a)) tiles.emplace_back(a.tile_x, a.tile_y);       // (seats 2 and 3: the ally, seat 1, is always drawn)
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
        std::printf("\n    ground explored by the picture and not by the engine at that tick: %d tiles over %d frames; enemy ants drawn on ground that the engine had not explored: %d\n", explored_by_picture_only, checked, enemies_drawn_early);
        ASSERT_EQ(enemies_drawn_early, 0);
        ASSERT_EQ(explored_by_picture_only, 0);                                                // (the picture never explored ground that the engine had not explored when it got to that tick)
        ASSERT_TRUE(s.app().net()->predicting());
        ASSERT_EQ(s.app().net()->prediction()->stats().commands_lost, 0u);
        ASSERT_TRUE(s.run_until([&]() { return s.room_finished(); }, 90000));
        s.run_ms(1500);
        ASSERT_EQ(s.app().sim().state_hash().total, s.referee_hash());
        for (size_t k = 0; k < s.peer_count(); ++k) ASSERT_TRUE(s.app().sim().state_hash() == s.peer(k).sim.state_hash());
        ASSERT_FALSE(s.app().net()->desynced());
        std::string why;
        ASSERT_TRUE(recording_plays_to(s, why));
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

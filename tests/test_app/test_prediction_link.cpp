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
        const sim::AntSnapshot* shown = s.shown_ant(ant);
        if (shown == nullptr) return;
        s.camera_on(shown->tile_x, shown->tile_y);
        int32_t sx = 0;
        int32_t sy = 0;
        if (!s.screen_of_world(shown->px, shown->py, sx, sy)) return;
        s.click(SDL_BUTTON_LEFT, sx, sy);
        if (s.app().hud().get_selected_ant_id() != ant) return;
        const int32_t tx = std::clamp(shown->tile_x + static_cast<int32_t>(s.rng()() % 13u) - 6, 2, 28);
        const int32_t ty = std::clamp(shown->tile_y + static_cast<int32_t>(s.rng()() % 13u) - 6, 2, 28);
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
    const sim::AntSnapshot* shown = s.shown_ant(ant);
    if (shown == nullptr || !s.screen_of_world(shown->px, shown->py, sx, sy)) {
        std::printf("    [walk: the ant is not on the screen (%d,%d)]\n", shown ? shown->px : -1, shown ? shown->py : -1);
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
    int32_t last_tile = shown->tile_x;
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
    return cell;
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The attack click on an enemy ant that walks
// ---------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kAttackerTile = 5;               // the person's ant waits on tile 5 of row 3, the other person's on tile 25
constexpr int32_t kEnemyTile = 25;
constexpr int32_t kEnemyGoal = 9;

struct AttackResult {
    bool clicked{false};            // the person's click was made on the enemy ant as the screen showed it
    uint8_t shown_order{0};         // what the screen's engine said the attacker's order was right after the click (3: an attack)
    uint8_t landed_order{0};        // what the confirmed engine made of it when the order landed (3: an attack; 1: a move)
};

// One attack click: the other person's ant is sent west; when it walks and `phase_ms` after its third tile on the screen, the person right-clicks on it with the attacker selected
AttackResult attack_click(Session& s, uint32_t attacker, uint32_t enemy, int phase_ms) {
    AttackResult out;
    if (!s.bring(0, attacker, kAttackerTile, kLaneRow)) {
        return out;
    }
    if (!s.bring(1, enemy, kEnemyTile, kLaneRow)) {
        return out;
    }
    s.camera_on(15, kLaneRow);
    int32_t sx = 0;
    int32_t sy = 0;
    const sim::AntSnapshot* mine = s.shown_ant(attacker);
    if (mine == nullptr || !s.screen_of_world(mine->px, mine->py, sx, sy)) {
        return out;
    }
    s.run_ms(static_cast<uint32_t>(s.rng()() % 50u));
    s.click(SDL_BUTTON_LEFT, sx, sy);
    if (s.app().hud().get_selected_ant_id() != attacker) {
        return out;
    }
    s.order(1, sim::CommandType::GroupMove, {enemy}, kEnemyGoal, kLaneRow);
    bool armed = false;
    uint32_t click_at = 0;
    bool done = false;
    s.on_frame_start = [&]() {
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
            click_at = s.now() + static_cast<uint32_t>(phase_ms);
        }
    };
    const bool clicked = s.run_until([&]() { return done; }, 8000);
    s.on_frame_end = nullptr;
    if (!clicked) {
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
    int clicks{0};
    int shown_attack{0};
    int landed_attack{0};
    int landed_move{0};
    int no_order{0};                // clicks that gave no order at all (the pointer was not on an ant that the screen's engine could pick)
};

// A cell: `phases` clicks in one match of one link, the click `i * 400 / phases` ms after the third tile of the walk
AttackCell attack_cell(int rtt, int jitter, bool predict, int phases, uint32_t seed) {
    Params p;
    p.map = "GRASS.LVL";
    p.local = rtt < 0;
    p.use_peer = rtt >= 0;
    p.link.rtt_ms = static_cast<uint32_t>(std::max(rtt, 0));
    p.link.jitter_ms = static_cast<uint32_t>(jitter);
    p.peer_link.rtt_ms = 30;
    p.predict = predict;
    p.seed = seed;
    Session s(p);
    AttackCell cell;
    cell.rtt = rtt;
    cell.jitter = jitter;
    cell.predict = predict;
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
    s.run_ms(2500);
    for (int i = 0; i < phases; ++i) {
        const AttackResult r = attack_click(s, mine[0], theirs[0], i * 400 / phases);
        if (!r.clicked) continue;
        ++cell.clicks;
        cell.shown_attack += r.shown_order == sim::AntUnit::kOrderAttack ? 1 : 0;
        cell.landed_attack += r.landed_order == sim::AntUnit::kOrderAttack ? 1 : 0;
        cell.landed_move += r.landed_order != sim::AntUnit::kOrderAttack && r.landed_order != sim::AntUnit::kOrderNone ? 1 : 0;
        cell.no_order += r.landed_order == sim::AntUnit::kOrderNone ? 1 : 0;
    }
    return cell;
}

void print_attack(const AttackCell& c) {
    std::printf("    attack  rtt %3d jit %2d  prediction %-3s: %2d clicks on the walking ant, the screen took %2d as an attack, %2d landed as an attack (%3.0f %%), %2d as a plain move, %d gave no order\n", c.rtt, c.jitter,
                c.predict ? "on" : "off", c.clicks, c.shown_attack, c.landed_attack, c.clicks > 0 ? 100.0 * c.landed_attack / c.clicks : 0.0, c.landed_move, c.no_order);
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

    TEST_CASE("LK2 Humping On Mud Through The Mouse: A Person Who Clicks The Other Goal A Moment After The Ant Is Seen To Cross A Tile Line Gains As In A Game On His Own Computer, At Every Round Trip, When The Screen Is The Predicted Engine; Without It The Window Of The Re-Order Is Past Before The Order Lands") {
        const std::vector<int> rtts = table_wanted() ? std::vector<int>{-1, 30, 100, 180, 300} : std::vector<int>{-1, 30, 100, 180, 300};
        const std::vector<int> reactions = table_wanted() ? std::vector<int>{0, 25, 50, 75, 100, 125, 150, 200} : std::vector<int>{0, 50, 75, 100, 125};
        const int trials = table_wanted() ? 20 : 8;
        const int jitter = 30;
        const int noise = std::getenv("ANTS_LINK_NOISE") != nullptr ? std::atoi(std::getenv("ANTS_LINK_NOISE")) : 30;        // the person's own timing: each click a little earlier or later, up to this many ms
        std::vector<double> local;
        double local_best = 0.0;
        for (const int rtt : rtts) {
            for (const bool predict : {false, true}) {
                if (rtt < 0 && predict) continue;
                std::vector<double> gains;
                for (const int reaction : reactions) {
                    const HumpCell c = hump_cell(rtt, jitter, predict, reaction, noise, trials, 5u + static_cast<uint32_t>(reaction));
                    ASSERT_TRUE(c.trials > 0);
                    gains.push_back(c.gain);
                }
                const double best = *std::max_element(gains.begin(), gains.end());
                std::printf("    hump  rtt %3d jit %2d prediction %-3s (timing +-%d ms): gain (px over one order in 9 s) by reaction", rtt < 0 ? 0 : rtt, rtt < 0 ? 0 : jitter, predict ? "on" : "off", noise);
                for (size_t i = 0; i < gains.size(); ++i) std::printf(" %d ms: %+.0f", reactions[i], gains[i]);
                std::printf("  | best %+.0f\n", best);
                if (rtt < 0) {
                    local = gains;
                    local_best = best;
                    ASSERT_TRUE(local_best > 30.0);                                           // (the reference works: the game on this computer gains on mud, as the original's snap does)
                    continue;
                }
                for (size_t i = 0; i < reactions.size(); ++i) {
                    if (reactions[i] < 75 || reactions[i] > 125) continue;                    // (the middle of the window: 75 to 125 ms after the crossing is seen)
                    if (predict) ASSERT_TRUE(std::abs(gains[i] - local[i]) <= 12.0);           // with the prediction the gain is the local game's, at every round trip
                    else if (rtt >= 100) ASSERT_TRUE(gains[i] < 0.0);                          // without it, from 100 ms on, every re-order there loses pixels
                }
                if (predict) ASSERT_TRUE(best >= 0.55 * local_best);
                else if (rtt >= 100) ASSERT_TRUE(best < 0.25 * local_best);
            }
        }
    } TEST_END();

    TEST_CASE("LK3 The Attack Click On A Walking Ant: The Order Names The Tile Of The Ant As The Screen Shows It, So That With The Prediction (The Screen Is The Engine The Order Lands In) It Is Still An Attack When It Lands, At Every Round Trip; Without It The Ant Has Left The Tile Before The Order Lands, And The Click Is A Plain Move") {
        double off_share[5] = {0, 0, 0, 0, 0};
        const int rtts[5] = {2, 30, 100, 180, 300};
        for (int k = 0; k < 5; ++k) {
            for (const bool predict : {false, true}) {
                const AttackCell c = attack_cell(rtts[k], 30, predict, 16, 7u);
                print_attack(c);
                ASSERT_EQ(c.clicks, 16);
                ASSERT_EQ(c.no_order, 0);
                const double share = 100.0 * c.landed_attack / c.clicks;
                if (!predict) {
                    off_share[k] = share;
                    ASSERT_EQ(c.shown_attack, 0);                                             // (the screen's engine is the confirmed one: it has no order yet when the click is made)
                } else {
                    ASSERT_EQ(c.shown_attack, c.clicks);                                      // (the screen takes every click as the attack that it is)
                    ASSERT_TRUE(share >= 90.0);                                               // ... and the order is still one when it lands (the ant may leave its tile in the tick that the order is sealed off by)
                    ASSERT_TRUE(share >= off_share[k]);
                    if (rtts[k] >= 100) ASSERT_TRUE(share >= off_share[k] + 25.0);
                }
            }
        }
        ASSERT_TRUE(off_share[4] <= 25.0 && off_share[2] <= 70.0);                           // (the study's numbers: 62 % at 100 ms, 12 % at 300 ms)
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
                std::printf("\n    prediction on: %llu orders predicted, %llu rebuilds (%llu foreign, %llu own timing), %llu strikes, %llu cool-downs", static_cast<unsigned long long>(st.commands_predicted),
                            static_cast<unsigned long long>(st.rebuilds), static_cast<unsigned long long>(st.rebuilds_foreign), static_cast<unsigned long long>(st.rebuilds_own_timing),
                            static_cast<unsigned long long>(st.over_budget), static_cast<unsigned long long>(st.cooldowns));
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

    TEST_CASE("LK7 What The Screen Shows When The Other Person's Orders Arrive: Their Ants Are Put Where The Engine Has Them By A Slide, Not A Jump (At RTT 180 The Frames In Which An Ant Of The Picture Moves By More Than 20 px Are None), And Without The Slide There Are Such Frames") {
        int jumps[2] = {0, 0};
        for (const bool glide : {false, true}) {
            Params p;
            p.link.rtt_ms = 180;
            p.link.jitter_ms = 30;
            p.peer_link.rtt_ms = 60;
            p.render = true;
            p.glide = glide;
            p.seed = 41;
            Session s(p);
            ASSERT_TRUE(s.begin());
            std::map<uint32_t, std::pair<int32_t, int32_t>> last;                        // the picture's position of every ant at the end of the last frame
            int frames = 0;
            s.on_frame_end = [&]() {
                ++frames;
                for (const auto& a : s.app().view_sim().get_world_state().ants) {
                    int32_t gx = 0;
                    int32_t gy = 0;
                    s.app().renderer().view_glide().offset_of(a.id, gx, gy);
                    const std::pair<int32_t, int32_t> drawn{a.px + gx, a.py + gy};
                    const auto it = last.find(a.id);
                    if (it != last.end() && a.state == sim::UnitState::Walking) {
                        const int32_t jump = std::max(std::abs(drawn.first - it->second.first), std::abs(drawn.second - it->second.second));
                        if (jump > 20 && jump <= 64) {
                            ++jumps[glide ? 1 : 0];
                        }               // (an order given to a walking ant snaps it by up to 16 px in the engine itself, as the original's does: more than 20 px in one frame is a hop of the correction; a throw or a teleport is no correction)
                    }
                    last[a.id] = drawn;
                }
            };
            Players players(s);
            players.person = false;
            players.play(30000);
            ASSERT_TRUE(frames > 1500);
            ASSERT_TRUE(s.app().net()->prediction()->stats().rebuilds_foreign > 10u);       // (the other person's orders were corrected)
        }
        std::printf("\n    frames in which a walking ant of the picture moved by more than 20 px: %d without the slide, %d with it\n", jumps[0], jumps[1]);
        ASSERT_TRUE(jumps[0] > 3);
        ASSERT_EQ(jumps[1], 0);
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
        std::map<uint64_t, std::vector<uint32_t>> shown_enemies;                              // the ants of other teams that the picture drew at each tick that it showed
        int checked = 0;
        int explored_by_picture_only = 0;
        int enemies_drawn_early = 0;
        s.on_frame_end = [&]() {
            const sim::SimulationEngine& view = s.app().view_sim();
            const uint64_t tick = view.current_tick();
            if (shown.find(tick) == shown.end()) {
                shown[tick] = view.get_world_state().fog_revealed;
                std::vector<uint32_t>& ids = shown_enemies[tick];
                for (const auto& a : view.get_world_state().ants) {
                    if (a.player_id != 0 && ants::app::health::ant_drawn(view.get_world_state(), 0, a)) ids.push_back(a.id);
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
            for (const uint32_t id : shown_enemies[now_tick]) {                                // an ant of another team that the picture drew: the engine draws it too, at that tick
                const sim::AntSnapshot* truth = Session::ant_in(s.app().sim(), id);
                if (truth != nullptr && !ants::app::health::ant_drawn(confirmed_world, 0, *truth)) ++enemies_drawn_early;
            }
            shown.erase(shown.begin(), std::next(it));
            shown_enemies.erase(shown_enemies.begin(), shown_enemies.find(now_tick) == shown_enemies.end() ? shown_enemies.begin() : std::next(shown_enemies.find(now_tick)));
        };
        Players players(s);
        players.play(25000);
        ASSERT_TRUE(players.rounds > 10);
        ASSERT_TRUE(checked > 300);
        std::printf("\n    ground explored by the picture and not by the engine at that tick: %d tiles over %d frames; enemy ants drawn that the engine would not draw: %d\n", explored_by_picture_only, checked, enemies_drawn_early);
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
        for (int n = 0; n < sessions; ++n) {
            std::mt19937 pick(1000u + static_cast<uint32_t>(n));
            Params p;
            const bool long_game = n % 3 == 0 && n % 2 == 1;                                  // (some two-person matches run for four minutes and end with the other person's Quit)
            p.map = long_game ? "GRASS.LVL" : "SHORT.LVL";
            const uint32_t rtts[6] = {20, 60, 100, 180, 250, 300};
            p.link.rtt_ms = rtts[pick() % 6];
            p.link.jitter_ms = pick() % 4 == 0 ? 0u : 10u + pick() % 50u;
            if (pick() % 3 == 0) {
                p.link.spike_ms = 120 + pick() % 200;
                p.link.spike_per_mille = 3 + pick() % 12;
            }
            p.peer_link.rtt_ms = rtts[pick() % 6];
            p.peer_link.jitter_ms = pick() % 40;
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
            std::printf("\n    soak %d: kind %d, rtt %u jit %u spikes %u/%u: %llu orders, %llu rebuilds, longest %.2f ms", n, kind, p.link.rtt_ms, p.link.jitter_ms, p.link.spike_ms, p.link.spike_per_mille,
                        static_cast<unsigned long long>(st.commands_predicted), static_cast<unsigned long long>(st.rebuilds), static_cast<double>(st.rebuild_ns_max) / 1.0e6);
        }
        std::printf("\n    soak: the longest rebuild of all %d sessions %.2f ms\n", sessions, static_cast<double>(worst_rebuild_ns) / 1.0e6);
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

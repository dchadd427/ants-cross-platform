// The hits without pauses (docs/BOT_FIGHT.md, "Hits without pauses"): the Hard bot orders the next blow while the victim is still in its hit clip (hit_in_clip) and clicks again at once on an ant that has
// struck (hit_follow_up: the controller's follow-up lane, Profile::follow_up_reaction). Medium and Easy are as they were. Hand-made worlds (b41_helpers.hpp) and the controller with a scripted bot.
//
//   AI55.1  the profiles: Hard has the lane (4 ticks), Medium and Easy none; the plans: Hard has both rules, Medium and Easy neither; without_hit_cadence and the keys hit, hitclip and hitquick of
//           bot_arena do what they say
//   AI55.2  the lane in the controller: a marked attack click of an ant that struck leaves 4 ticks after the look, an unmarked one after the reaction delay, a marked one of an ant that never struck
//           or of a profile without the lane after the reaction delay, a marked move after the reaction delay; the window (kFollowUpWindow) has its edge
//   AI55.3  the lane keeps the rules of the door: it costs a token (three clicks at once leave one by one), and it never leaves before an older click that names the same ant
//   AI55.4  the order during the hit clip: with the rule on, an ant two tiles from a target in its clip is ordered, one that stands next to it is not; with the rule off nobody is; the fight ends sooner
//   AI55.5  the follow-up mark: FightTask marks the click of an ant that struck and is idle again, never the first order of a defender; with the rule off nothing is marked
//   AI55.6  the same in the 187 brawl: a pack that struck is clicked again with the mark, a pack that was just formed is not
//   AI55.7  the click after a blow comes sooner, through the controller (the lane) and the engine: the median time from a blow to the next attack click in 187 and in a fight of the food game
//   AI55.8  Medium and Easy play as before: a marked click is an unmarked one where the profile has no lane (the same commands and state hash, the food game and 187); the shipped Hard plays differently with it
//   AI55.9  a hurt Bomber that an enemy worker keeps striking is answered by the shipped Hard bot, over forty seeds and both styles: the Bomber is ordered at the worker at once, the bot's ants strike the worker
//           soon and six times by tick 440 and no refusal shuns the worker, and the Bomber lands a blow itself in about a third of the runs (AI34.4 has the Bomber's own blow at the cadence of before)
//   AI55.10 a refusal that is no clip (an enemy shut in by rocks) still shuns the target for 900 ticks, with both rules on
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "bot_arena_tune.hpp"
#include "bot_watch_people.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

BotSpec spec_of(uint8_t seat, Level level) {
    BotSpec s;
    s.seat = seat;
    s.kind = "script";
    s.level = level;
    return s;
}

void run_ticks(sim::SimulationEngine& sim, BotController& c, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        sim.tick();
        c.on_tick(sim);
    }
}

TileCoord tile_of_team(const sim::SimulationEngine& sim, uint8_t team) {
    for (const auto& a : sim.get_world_state().ants) {
        if (a.player_id == team) return TileCoord{a.tile_x, a.tile_y};
    }
    return TileCoord{-1, -1};
}

// What a scripted seat did in one scene: the tick of every look, what left the controller (tick, command), the ants of the seat and the tile of an enemy ant
struct Scene {
    std::vector<uint64_t> looks;
    std::vector<std::pair<uint64_t, Command>> log;
    std::vector<uint32_t> mine;
    TileCoord enemy{};
    bool ok{false};
};

// A scene: sixteen workers of seat 0 on the field of build_world, a full bucket (the budget is not what most cases are about); the first look clicks the ants 0 .. 9 at an enemy ant as one ordinary
// click, every later look is `think` (the view, the orders, the scene so far, the number of the look)
void play_scene(Scene& out, Level level, uint32_t match_seed, uint32_t ticks, const std::function<void(const BotView&, Orders&, const Scene&, uint32_t)>& think) {
    sim::SimulationEngine sim;
    build_world(sim, 21, 16);
    RecordingSink sink(sim);
    BotController c(sim, match_seed);
    c.set_start_hold(0);
    out.mine = ants_of(sim, 0);
    out.enemy = tile_of_team(sim, 1);
    uint32_t round = 0;
    auto bot = std::make_unique<ScriptBot>([&](const BotView& v, Orders& o) {
        if (round == 0) o.attack(std::vector<uint32_t>(out.mine.begin(), out.mine.begin() + 10), out.enemy);
        else think(v, o, out, round);
        ++round;
    });
    ScriptBot* raw = bot.get();
    std::string why;
    if (!c.add(spec_of(0, level), std::move(bot), sink, why)) return;
    run_ticks(sim, c, ticks);
    out.looks = raw->thought;
    out.log = sink.log;
    out.ok = true;
}

// The plan of a level with nothing but the fights that answer a blow in play (as test_ai_defence.cpp plays them), with the two rules as the case says
LevelPlan fight_plan(Level level, bool in_clip, bool follow_up) {
    LevelPlan p = peace_plan(level);
    without_kill_batch(p);
    p.hunt = false;
    p.catchup = false;
    p.endgame = false;
    p.defend_all_types = p.victim_answers = p.defend_by_strength = true;
    p.hit_in_clip = in_clip;
    p.hit_follow_up = follow_up;
    return p;
}

// A bot by hand like b41's Rig (no budget, no filter, every intent applied `delay` ticks after the look) that keeps the intents as they were proposed, with the mark of the follow-up click
class IntentRig {
public:
    struct Entry {
        uint64_t tick;
        Intent intent;
    };
    IntentRig(sim::SimulationEngine& sim, uint8_t seat, Level level, std::unique_ptr<Bot> bot, uint32_t interval, uint32_t delay) : sim_(sim), seat_(seat), profile_(profile_for(level)), map_(sim), bot_(std::move(bot)), delay_(delay) {
        profile_.jitter_percent = 0;
        profile_.decision_interval = interval;
        bot_->start(BotContext{seat, profile_, 1, &map_});
    }
    void tick() {
        sim_.tick();
        sim_.clear_news_events();
        sim_.clear_audio_events();
        const uint64_t now = sim_.current_tick();
        for (size_t i = 0; i < waiting_.size();) {
            if (waiting_[i].first > now) {
                ++i;
                continue;
            }
            Command c = waiting_[i].second;
            waiting_.erase(waiting_.begin() + static_cast<std::ptrdiff_t>(i));
            c.issuer = seat_;
            sim_.apply_command(c);
            bot_->on_command(c, Bot::Fate::Sent, now);
        }
        if (now >= next_look_) {
            next_look_ = now + profile_.decision_interval;
            const BotView view = BotView::build(sim_, seat_, &map_);
            Orders orders;
            bot_->think(view, orders);
            for (const Intent& in : orders.intents()) {
                log.push_back(Entry{now, in});
                waiting_.emplace_back(now + delay_, in.command);
            }
        }
    }
    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) tick();
    }
    template <class T>
    T& as() {
        return static_cast<T&>(*bot_);
    }
    std::vector<Entry> log;

private:
    sim::SimulationEngine& sim_;
    uint8_t seat_;
    Profile profile_;
    MapInfo map_;
    std::unique_ptr<Bot> bot_;
    uint32_t delay_;
    std::vector<std::pair<uint64_t, Command>> waiting_;
    uint64_t next_look_{1};
};

bool in_clip(const sim::SimulationEngine& sim, uint32_t id) {
    const sim::AntSnapshot* a = snapshot_of(sim, id);
    return a != nullptr && (a->state == sim::UnitState::Flinch || a->state == sim::UnitState::Knockback);
}

// The ticks at which the hit points of an ant fell (the blows that landed), read tick by tick
struct BlowClock {
    uint16_t hp{10};
    std::vector<uint64_t> at;
    void watch(const sim::SimulationEngine& sim, uint32_t id) {
        const sim::AntSnapshot* a = snapshot_of(sim, id);
        const uint16_t now_hp = a == nullptr ? 0 : a->hp;
        if (now_hp < hp) at.push_back(sim.current_tick());
        hp = now_hp;
    }
};

// The marks on the attack clicks that a bot proposed: how many were marked, how many were a repeat of an ant's click, and whether a marked one was ever the first click of an ant
struct Marks {
    size_t marked{0};
    size_t again{0};
    bool first_marked{false};
};

Marks marks_of(const std::vector<IntentRig::Entry>& log) {
    Marks m;
    std::set<uint32_t> ordered;                                                                           // the ants that were clicked at an enemy before
    for (const IntentRig::Entry& e : log) {
        if (e.intent.command.type != CommandType::GroupAttack) continue;
        bool all_before = true;
        for (const uint32_t id : e.intent.command.ants) all_before = all_before && ordered.count(id) != 0;
        if (e.intent.follow_up) {
            ++m.marked;
            m.first_marked = m.first_marked || !all_before;
        }
        m.again += all_before ? 1u : 0u;
        for (const uint32_t id : e.intent.command.ants) ordered.insert(id);
    }
    return m;
}

}  // namespace

void run_hit_tests() {
    TEST_CASE("AI55.1 The Profiles And The Plans: Hard Has The Follow-Up Lane (3 Ticks) And Both Rules, Medium And Easy Have Neither; Taking The Rules Out And The Keys Of bot_arena Do What They Say") {
        ASSERT_EQ(profile_for(Level::Hard).follow_up_reaction, 4u);
        ASSERT_EQ(profile_for(Level::Medium).follow_up_reaction, 0u);
        ASSERT_EQ(profile_for(Level::Easy).follow_up_reaction, 0u);
        ASSERT_TRUE(plan_for(Level::Hard).hit_in_clip);
        ASSERT_TRUE(plan_for(Level::Hard).hit_follow_up);
        for (const Level level : {Level::Medium, Level::Easy}) {
            ASSERT_FALSE(plan_for(level).hit_in_clip);
            ASSERT_FALSE(plan_for(level).hit_follow_up);
        }
        ASSERT_FALSE(LevelPlan{}.hit_in_clip);                                                          // (a plan made by hand has neither)
        ASSERT_FALSE(LevelPlan{}.hit_follow_up);
        LevelPlan p = plan_for(Level::Hard);
        without_hit_cadence(p);
        ASSERT_FALSE(p.hit_in_clip);
        ASSERT_FALSE(p.hit_follow_up);
        std::string err;
        ASSERT_TRUE(apply_tune(p, "hit", 1, err));
        ASSERT_TRUE(p.hit_in_clip && p.hit_follow_up);
        ASSERT_TRUE(apply_tune(p, "hit", 0, err));
        ASSERT_FALSE(p.hit_in_clip || p.hit_follow_up);
        ASSERT_TRUE(apply_tune(p, "hitclip", 1, err));
        ASSERT_TRUE(p.hit_in_clip && !p.hit_follow_up);
        ASSERT_TRUE(apply_tune(p, "hitquick", 1, err));
        ASSERT_TRUE(p.hit_in_clip && p.hit_follow_up);
        ASSERT_TRUE(apply_tune(p, "hitclip", 0, err));
        ASSERT_TRUE(!p.hit_in_clip && p.hit_follow_up);
        ASSERT_TRUE(apply_tune(p, "hitquick", 0, err));
        ASSERT_FALSE(p.hit_in_clip || p.hit_follow_up);
    } TEST_END();

    TEST_CASE("AI55.2 The Lane In The Controller: A Marked Attack Click Of An Ant That Struck Leaves 3 Ticks After The Look, An Unmarked One After The Reaction Delay; A Marked One Of An Ant That Never Struck, Of A Profile Without The Lane Or A Marked Move After The Reaction Delay; The Window Has Its Edge") {
        // (a) one marked click of an ant that struck, and one unmarked: 4 ticks after the look, and the reaction delay (8, plus or minus 25 percent: 6 .. 10)
        for (const bool marked : {true, false}) {
            Scene s;
            play_scene(s, Level::Hard, 5, 60, [&](const BotView&, Orders& o, const Scene& now, uint32_t round) {
                if (round == 6) o.attack({now.mine[0]}, now.enemy, Priority::Urgent, marked);
            });
            ASSERT_TRUE(s.ok);
            ASSERT_EQ(s.log.size(), 2u);
            const uint64_t delay = s.log[1].first - s.looks[6];
            if (marked) ASSERT_EQ(delay, 4u);
            else ASSERT_TRUE(delay >= 6u && delay <= 10u);
        }
        // (b) a marked click of an ant that never struck (the first click named the ants 0 .. 9): the reaction delay; the same for a click of one that struck and one that did not (the whole click waits)
        {
            Scene s;
            play_scene(s, Level::Hard, 5, 80, [&](const BotView&, Orders& o, const Scene& now, uint32_t round) {
                if (round == 6) o.attack({now.mine[12]}, now.enemy, Priority::Urgent, true);
                if (round == 12) o.attack({now.mine[0], now.mine[13]}, now.enemy, Priority::Urgent, true);
            });
            ASSERT_TRUE(s.ok);
            ASSERT_EQ(s.log.size(), 3u);
            ASSERT_TRUE(s.log[1].first - s.looks[6] >= 6u && s.log[1].first - s.looks[6] <= 10u);
            ASSERT_TRUE(s.log[2].first - s.looks[12] >= 6u && s.log[2].first - s.looks[12] <= 10u);
        }
        // (b2) a marked click of an ant that was only MOVED (no blow): the reaction delay
        {
            Scene s;
            play_scene(s, Level::Hard, 5, 80, [&](const BotView&, Orders& o, const Scene& now, uint32_t round) {
                if (round == 2) o.move({now.mine[14]}, TileCoord{20, 20}, Priority::Urgent);
                if (round == 10) o.attack({now.mine[14]}, now.enemy, Priority::Urgent, true);
            });
            ASSERT_TRUE(s.ok);
            ASSERT_EQ(s.log.size(), 3u);
            ASSERT_TRUE(s.log[2].second.type == CommandType::GroupAttack);
            ASSERT_TRUE(s.log[2].first - s.looks[10] >= 6u && s.log[2].first - s.looks[10] <= 10u);
        }
        // (c) a profile without the lane (Medium: 24 ticks, plus or minus 25 percent)
        {
            Scene s;
            play_scene(s, Level::Medium, 5, 200, [&](const BotView&, Orders& o, const Scene& now, uint32_t round) {
                if (round == 5) o.attack({now.mine[0]}, now.enemy, Priority::Urgent, true);
            });
            ASSERT_TRUE(s.ok);
            ASSERT_EQ(s.log.size(), 2u);
            ASSERT_TRUE(s.log[1].first - s.looks[5] >= 18u && s.log[1].first - s.looks[5] <= 30u);
        }
        // (d) a marked move of an ant that struck: no blow, the mark is ignored
        {
            Scene s;
            play_scene(s, Level::Hard, 5, 60, [&](const BotView&, Orders& o, const Scene& now, uint32_t round) {
                if (round != 6) return;
                o.move({now.mine[0]}, TileCoord{20, 20}, Priority::Urgent);
                o.edit().back().follow_up = true;
            });
            ASSERT_TRUE(s.ok);
            ASSERT_TRUE(s.log.size() == 2u && s.log[1].second.type == CommandType::GroupMove);
            ASSERT_TRUE(s.log[1].first - s.looks[6] >= 6u);
        }
        // (e) the window: the ants 0 .. 9 struck at one tick. From the look 12 ticks before the edge a marked click of the next ant is proposed at every look (4 ticks apart): it leaves 3 ticks after the look while
        // the look is within kFollowUpWindow ticks of the first release, and after the reaction delay when it is later. The first release is on tick 7 to 11 (the jitter of the match seed), so over thirty
        // seeds a look falls on the very tick of the edge, and the edge is inside
        {
            size_t inside = 0;
            size_t outside = 0;
            size_t on_the_edge = 0;
            for (uint32_t seed = 1; seed <= 30; ++seed) {
                Scene s;
                uint32_t next = 0;
                std::vector<uint64_t> look_of;                                                             // the look at which the click of ant k was proposed
                play_scene(s, Level::Hard, seed, 320, [&](const BotView& v, Orders& o, const Scene& now, uint32_t) {
                    if (v.tick() >= 12 + kFollowUpWindow - 12 && next < 10) {
                        o.attack({now.mine[next]}, now.enemy, Priority::Urgent, true);
                        look_of.push_back(v.tick());
                        ++next;
                    }
                });
                ASSERT_TRUE(s.ok);
                ASSERT_TRUE(!s.log.empty());
                const uint64_t struck = s.log[0].first;                                                    // all ten ants struck on this tick
                ASSERT_EQ(s.log.size(), 1u + look_of.size());
                for (size_t k = 0; k < look_of.size(); ++k) {
                    const uint64_t delay = s.log[k + 1].first - look_of[k];
                    if (look_of[k] <= struck + kFollowUpWindow) {
                        ASSERT_EQ(delay, 4u);
                        ++inside;
                        on_the_edge += look_of[k] == struck + kFollowUpWindow ? 1u : 0u;
                    } else {
                        ASSERT_TRUE(delay >= 6u);
                        ++outside;
                    }
                }
            }
            ASSERT_TRUE(inside >= 1u && outside >= 1u);                                                    // (both sides of the edge were played)
            ASSERT_TRUE(on_the_edge >= 1u);                                                                // (and a click on the tick of the edge itself)
        }
    } TEST_END();

    TEST_CASE("AI55.3 The Lane Keeps The Rules Of The Door: A Follow-Up Click Costs A Token (Three At Once Leave One By One), And It Never Leaves Before An Older Click That Names The Same Ant") {
        // (a) the bucket: ten ordinary clicks empty it; the next look's three marked clicks are due 4 ticks later and wait for their tokens, one every 6 or 7 ticks
        {
            sim::SimulationEngine sim;
            build_world(sim, 23);
            RecordingSink sink(sim);
            BotController c(sim, 5);
            c.set_start_hold(0);
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            const TileCoord enemy = tile_of_team(sim, 1);
            uint32_t round = 0;
            uint64_t second = 0;
            auto bot = std::make_unique<ScriptBot>([&](const BotView& v, Orders& o) {
                if (round == 0) {
                    for (size_t i = 0; i < 10; ++i) o.attack({mine[i]}, enemy);
                } else if (!sink.log.empty() && second == 0) {
                    second = v.tick();                                                                     // the first look after the first click left
                    for (size_t i = 0; i < 3; ++i) o.attack({mine[i]}, enemy, Priority::Urgent, true);
                }
                ++round;
            });
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, Level::Hard), std::move(bot), sink, why));
            run_ticks(sim, c, 120);
            ASSERT_TRUE(second != 0u);
            ASSERT_EQ(sink.log.size(), 13u);                                                                // (every click left in the end)
            // the three follow-ups are the last three clicks: the refill of the level's rate (3 a second) spaces them, 6 or 7 ticks
            ASSERT_TRUE(sink.log[11].first - sink.log[10].first >= 6u && sink.log[12].first - sink.log[11].first >= 6u);
            ASSERT_TRUE(sink.log[10].first >= second + 4u);
        }
        // (b) an older click of the ant that is still waiting: the follow-up is not released before it. Over thirty seeds (the jitter differs) the older click is never applied after the follow-up, and in
        // some of them the follow-up was held back for it
        {
            size_t held = 0;
            for (uint32_t seed = 1; seed <= 30; ++seed) {
                Scene s;
                uint64_t second_look = 0;
                play_scene(s, Level::Hard, seed, 70, [&](const BotView& v, Orders& o, const Scene& now, uint32_t round) {
                    if (round == 6) o.move({now.mine[0]}, TileCoord{20, 20}, Priority::Urgent);          // (an older click of the ant, still waiting for its time)
                    if (round == 7) {
                        second_look = v.tick();
                        o.attack({now.mine[0]}, now.enemy, Priority::Urgent, true);                       // (the follow-up, one look later: on its own it would leave 4 ticks after the look)
                    }
                });
                ASSERT_TRUE(s.ok && second_look != 0u);
                int move_at = -1;
                int attack_at = -1;
                for (size_t i = 1; i < s.log.size(); ++i) {
                    if (s.log[i].second.type == CommandType::GroupMove) move_at = static_cast<int>(i);
                    if (s.log[i].second.type == CommandType::GroupAttack) attack_at = static_cast<int>(i);
                }
                ASSERT_TRUE(attack_at >= 1);
                ASSERT_TRUE(move_at < 0 || move_at < attack_at);                                           // (the move never comes after the attack that followed it)
                if (s.log[static_cast<size_t>(attack_at)].first - second_look > 4u) ++held;
            }
            ASSERT_TRUE(held >= 1u);
        }
    } TEST_END();

    TEST_CASE("AI55.4 The Order During The Hit Clip: With The Rule On An Ant Two Tiles From A Target In Its Clip Is Ordered At It, An Ant That Stands Next To It Is Not; With The Rule Off Nobody Is, And The Enemy Dies Later") {
        // (a) three workers at two and more tiles from an enemy worker that does not strike: the rule on and off
        uint64_t dead_with_rule = 0;
        for (const bool rule : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 551);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            IntentRig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(fight_plan(Level::Hard, rule, false)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;                                                                  // (a blow that the view shows)
            size_t seen = 0;
            size_t during = 0;                                                                            // the orders proposed while the enemy was in its clip
            size_t near_during = 0;                                                                       // ... that named an ant that stood next to it
            uint64_t dead_at = 0;
            for (int t = 0; t < 900 && dead_at == 0; ++t) {
                rig.tick();
                const bool clip = in_clip(sim, enemy);
                for (; seen < rig.log.size(); ++seen) {
                    const IntentRig::Entry& e = rig.log[seen];
                    if (e.intent.command.type != CommandType::GroupAttack || e.tick != sim.current_tick() || !clip) continue;
                    ++during;
                    const sim::AntSnapshot* en = snapshot_of(sim, enemy);
                    for (const uint32_t id : e.intent.command.ants) {
                        const sim::AntSnapshot* a = snapshot_of(sim, id);
                        if (a != nullptr && en != nullptr && std::max(std::abs(a->tile_x - en->tile_x), std::abs(a->tile_y - en->tile_y)) < 2) ++near_during;
                    }
                }
                if (!alive(sim, enemy)) dead_at = sim.current_tick();
            }
            ASSERT_TRUE(dead_at != 0u);
            if (rule) {
                ASSERT_TRUE(during >= 1u);
                ASSERT_EQ(near_during, 0u);
                dead_with_rule = dead_at;
            } else {
                ASSERT_EQ(during, 0u);
                ASSERT_TRUE(dead_with_rule < dead_at);                                                     // (the enemy dies sooner with the rule: the order is not a clip late)
            }
        }
        // (b) three workers that all stand next to an enemy worker that stands in a pocket of rock: a blow cannot throw it away, so every ant stays next to it while it plays the clip, and none of them is
        // ordered at it then (the first strikes, the others arrive in its clip and are refused, and ordered again when it is over)
        {
            sim::SimulationEngine sim;
            empty_field(sim, 552);
            for (const TileCoord t : {TileCoord{21, 17}, TileCoord{22, 17}, TileCoord{23, 17}, TileCoord{21, 18}, TileCoord{23, 18}}) sim.set_terrain(t.x, t.y, sim::TERRAIN_OBSTACLE);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 19});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 19});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 19});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            IntentRig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(fight_plan(Level::Hard, true, false)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            size_t seen = 0;
            size_t clips = 0;
            size_t near_during = 0;
            for (int t = 0; t < 400; ++t) {
                rig.tick();
                clips += in_clip(sim, enemy) ? 1u : 0u;
                for (; seen < rig.log.size(); ++seen) {
                    const IntentRig::Entry& e = rig.log[seen];
                    if (e.intent.command.type != CommandType::GroupAttack || e.tick != sim.current_tick() || !in_clip(sim, enemy)) continue;
                    const sim::AntSnapshot* en = snapshot_of(sim, enemy);
                    for (const uint32_t id : e.intent.command.ants) {
                        const sim::AntSnapshot* a = snapshot_of(sim, id);
                        if (a != nullptr && en != nullptr && std::max(std::abs(a->tile_x - en->tile_x), std::abs(a->tile_y - en->tile_y)) < 2) ++near_during;
                    }
                }
            }
            ASSERT_TRUE(clips >= 20u);                                                                    // (the premise: the enemy was struck and played its clip)
            ASSERT_EQ(near_during, 0u);
        }
    } TEST_END();

    TEST_CASE("AI55.5 The Follow-Up Mark In The Fights Of The Food Game: The Click Of An Ant That Struck And Is Idle Again Is Marked, The First Order Of A Defender Never; With The Rule Off Nothing Is") {
        for (const bool rule : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 552);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            IntentRig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(fight_plan(Level::Hard, true, rule)), 4, 4);
            rig.run(12);
            sim.get_unit(victim).hp = 9;
            rig.run(500);
            const Marks m = marks_of(rig.log);
            ASSERT_TRUE(m.again >= 2u);                                                                    // (the premise: ants were clicked again)
            ASSERT_FALSE(m.first_marked);                                                                  // (never the first click of an ant)
            if (rule) ASSERT_TRUE(m.marked >= 2u);
            else ASSERT_EQ(m.marked, 0u);
        }
    } TEST_END();

    TEST_CASE("AI55.6 The Same In The 187 Brawl: A Pack That Struck Is Clicked Again With The Mark, A Pack That Was Just Formed Is Not; With The Rule Off Nothing Is Marked") {
        for (const bool rule : {true, false}) {
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            empty_field(sim, 553);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + i % 3, 20 + i / 3});
            for (int i = 0; i < 2; ++i) sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22 + i, 17});
            LevelPlan plan = plan_for(Level::Hard);
            plan.hit_follow_up = rule;
            IntentRig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            for (int i = 0; i < 2; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24 + i, 21});    // (ants that come late join a pack that has struck already: their first click is no follow-up)
            rig.run(400);
            const Marks m = marks_of(rig.log);
            ASSERT_TRUE(m.again >= 2u);
            ASSERT_FALSE(m.first_marked);
            if (rule) ASSERT_TRUE(m.marked >= 2u);
            else ASSERT_EQ(m.marked, 0u);
        }
    } TEST_END();

    TEST_CASE("AI55.7 The Click After A Blow Comes Sooner: Through The Controller And The Engine, The Median Time From A Blow To The Next Attack Click Of The Fight Is Shorter With The Rules On, For One Hard Ant Against A Standing Enemy In 187 And For Three In The Food Game") {
        // the median of the ticks from each blow that landed (a hit point of the enemy fell) to the first attack click that left after it
        const auto median_wait = [](const std::vector<uint64_t>& blows, const std::vector<std::pair<uint64_t, Command>>& log) {
            std::vector<uint64_t> waits;
            for (const uint64_t b : blows) {
                for (const auto& e : log) {
                    if (e.second.type == CommandType::GroupAttack && e.first > b) {
                        waits.push_back(e.first - b);
                        break;
                    }
                }
            }
            if (waits.size() < 3u) return 0.0;
            std::sort(waits.begin(), waits.end());
            return static_cast<double>(waits[waits.size() / 2]);
        };
        // (a) 187: one ant at a standing enemy worker
        const auto brawl_wait = [&](bool rules) {
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            empty_field(sim, 554);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 24});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 17});
            RecordingSink sink(sim, true);                                                                // (applied at once)
            BotController c(sim, 9);
            LevelPlan plan = plan_for(Level::Hard);
            plan.hit_in_clip = plan.hit_follow_up = rules;
            std::string why;
            if (!c.add(spec_of(0, Level::Hard), std::make_unique<StandardBot>(plan), sink, why)) return 0.0;
            BlowClock clock;
            for (int t = 0; t < 700 && alive(sim, enemy); ++t) {
                sim.tick();
                c.on_tick(sim);
                clock.watch(sim, enemy);
            }
            return median_wait(clock.at, sink.log);
        };
        const double slow = brawl_wait(false);
        const double fast = brawl_wait(true);
        ASSERT_TRUE(slow > 0.0 && fast > 0.0);
        ASSERT_TRUE(fast + 2.0 <= slow);                                                              // (the lane: 4 ticks after the look instead of 8, minus the wait for the look)
        // (b) the food game: three ants against a standing enemy (the struck ant starts the fight)
        const auto fight_wait = [&](bool rules) {
            sim::SimulationEngine sim;
            empty_field(sim, 555);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 22});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18});
            RecordingSink sink(sim, true);
            BotController c(sim, 9);
            std::string why;
            if (!c.add(spec_of(0, Level::Hard), std::make_unique<StandardBot>(fight_plan(Level::Hard, rules, rules)), sink, why)) return 0.0;
            for (int t = 0; t < 12; ++t) {
                sim.tick();
                c.on_tick(sim);
            }
            sim.get_unit(victim).hp = 9;
            BlowClock clock;
            for (int t = 0; t < 900 && alive(sim, enemy); ++t) {
                sim.tick();
                c.on_tick(sim);
                clock.watch(sim, enemy);
            }
            return median_wait(clock.at, sink.log);
        };
        const double slow_fight = fight_wait(false);
        const double fast_fight = fight_wait(true);
        ASSERT_TRUE(slow_fight > 0.0 && fast_fight > 0.0);
        ASSERT_TRUE(fast_fight + 8.0 <= slow_fight);                                                  // (the order in the clip and the lane: about 15)
    } TEST_END();

    TEST_CASE("AI55.8 Medium And Easy Play As Before: A Marked Click Is An Unmarked One Where The Profile Has No Lane (The Same Commands And State Hash Through The Controller, In The Food Game And In 187), And The Shipped Hard Plays Differently With The Follow-Up On") {
        struct Outcome {
            uint32_t released;
            uint64_t hash;
            bool operator==(const Outcome& o) const { return released == o.released && hash == o.hash; }
        };
        // a fixed scene through the real controller (the profile of the plan's level), the commands applied at once; `follow_up` is the plan's switch (the order during the clip stays as the plan has it)
        const auto play = [](Level level, bool follow_up, bool brawl) {
            sim::SimulationEngine sim;
            if (brawl) sim.set_game_mode(sim::GameMode::Kills187);
            empty_field(sim, brawl ? 557 : 556);
            sim.set_player_eggs(0, 5);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21 + i % 3, 23 + i / 3});
            std::vector<uint32_t> enemies;
            for (int i = 0; i < 3; ++i) enemies.push_back(sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21 + i, 17}));
            RecordingSink sink(sim, true);
            BotController c(sim, 9);
            LevelPlan plan = plan_for(level);
            plan.hit_follow_up = follow_up;
            std::string why;
            if (!c.add(spec_of(0, level), std::make_unique<StandardBot>(plan), sink, why)) return Outcome{0, 0};
            for (int t = 0; t < 600; ++t) {
                if (!brawl) {
                    for (const uint32_t e : enemies) keep_attacking(sim, 1, e, victim);
                }
                sim.tick();
                c.on_tick(sim);
            }
            return Outcome{c.stats(0).released, sim.state_hash().total};
        };
        for (const bool brawl : {false, true}) {
            for (const Level level : {Level::Medium, Level::Easy}) {
                ASSERT_TRUE(play(level, true, brawl).released > 0u);
                ASSERT_TRUE(play(level, true, brawl) == play(level, false, brawl));
            }
            ASSERT_TRUE(play(Level::Hard, true, brawl).released > 0u);
            ASSERT_FALSE(play(Level::Hard, true, brawl) == play(Level::Hard, false, brawl));
        }
    } TEST_END();

    TEST_CASE("AI55.9 A Hurt Bomber That An Enemy Worker Strikes Again And Again Is Answered By The Shipped Hard Bot, Over Forty Seeds And Both Styles Of Hard: It Is Named In An Attack Click Within 100 Ticks Of The First Blow On It, The Bot's Ants Strike The Worker Within 200 Ticks And Six Times By Tick 440 Without A Shun, And The Bomber Lands A Blow Itself In About A Third Of The Runs (AI34.4 Has It At The Cadence Of Before)") {
        for (const Style style : {Style::Aggressive, Style::Raider}) {
            size_t own_blow = 0;
            size_t named = 0;
            size_t runs = 0;
            for (uint32_t seed = 1; seed <= 40; ++seed) {
                sim::SimulationEngine sim;
                empty_field(sim, 344);
                add_pile(sim, 30, 12, 40, 25);
                const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{16, 10});
                sim.get_unit(bomber).hp = 6;
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 8});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 10});
                RecordingSink sink(sim, true);
                BotController c(sim, seed);
                std::string why;
                ASSERT_TRUE(c.add(spec_of(0, Level::Hard), std::make_unique<StandardBot>(Level::Hard, style), sink, why));
                watch::PeopleObserver seen;
                for (int t = 0; t < 440; ++t) {
                    sim.tick();
                    sim.clear_news_events();
                    sim.clear_audio_events();
                    c.on_tick(sim);
                    seen.tick(sim);
                    if (sim.current_tick() >= 40 && sim.current_tick() % 20 == 0 && alive(sim, enemy)) {        // (it strikes again at the first tick that 20 divides at which it is idle)
                        const sim::AntSnapshot* e = snapshot_of(sim, enemy);
                        const sim::AntSnapshot* b = snapshot_of(sim, bomber);
                        if (e != nullptr && b != nullptr && b->hp > 0 && (e->state == sim::UnitState::Idle || e->state == sim::UnitState::GuardIdle)) {
                            sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, b->tile_x, b->tile_y));
                        }
                    }
                }
                uint64_t first_blow = 0;
                uint64_t first_answer = 0;
                size_t answers = 0;
                bool bomber_struck = false;
                for (const watch::Blow& b : seen.people().blows()) {
                    if (first_blow == 0 && b.victim == bomber) first_blow = b.tick;
                    if (b.victim == enemy && b.striker_seat == 0) {
                        ++answers;
                        if (first_answer == 0) first_answer = b.tick;
                        bomber_struck = bomber_struck || b.striker == bomber;
                    }
                }
                uint64_t first_click = 0;                                                                     // the first attack click that names the Bomber after the first blow on it
                for (const auto& e : sink.log) {
                    if (first_blow != 0 && e.first >= first_blow && e.second.type == CommandType::GroupAttack && std::find(e.second.ants.begin(), e.second.ants.end(), bomber) != e.second.ants.end()) {
                        first_click = e.first;
                        break;
                    }
                }
                const StandardBot* standard = dynamic_cast<const StandardBot*>(c.bot(0));
                ASSERT_TRUE(standard != nullptr);
                ASSERT_TRUE(first_blow != 0u && first_answer != 0u && first_answer <= first_blow + 200);       // (the bot's ants strike the worker soon after the first blow on the Bomber ...)
                ASSERT_TRUE(answers >= 6u);                                                                   // ... and six times by tick 440
                ASSERT_EQ(standard->fight().shunned_count(), 0u);                                             // (an ant that arrived in the clip of another's blow was refused: that does not shun the worker)
                ++runs;
                own_blow += bomber_struck ? 1u : 0u;
                named += (first_click != 0u && first_click <= first_blow + 100) ? 1u : 0u;
            }
            // the struck Bomber is ordered at the worker at once in the runs in which a fight takes it (in the others, 6 of the 40, another task of the bot holds it: the older cadence has the same 6),
            // and it lands a blow itself in about a third of the runs (29 of 80; the older cadence 45 of 80: its workers are quicker now and strike the worker more than half as often again, 8.6 times in 440 ticks against 5.5)
            ASSERT_EQ(runs, 40u);
            ASSERT_TRUE(named >= 30u);
            ASSERT_TRUE(own_blow >= 12u);
        }
    } TEST_END();

    TEST_CASE("AI55.10 A Refusal That Is No Clip Still Shuns The Target: An Enemy Shut In By Rocks Is Left Alone For 900 Ticks After The Attack Shows Can't Go, With Both Rules On, As At Every Level (AI7.8 Has It Without Them)") {
        sim::SimulationEngine sim;
        empty_field(sim, 6);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) sim.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
            }
        }
        const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28, 30});
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 28});
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 32});
        const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 30});            // shut in: no path leads to its tile, and it never plays a clip
        LevelPlan plan = fight_plan(Level::Hard, true, true);
        plan.reach_guard = false;                                                                      // (the order leaves and the path finder refuses it)
        IntentRig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
        rig.run(10);
        sim.get_unit(victim).hp = 9;                                                                   // "hit" (the enemy cannot reach anybody: the test makes the blow)
        rig.run(300);
        const FightTask& fight = rig.as<StandardBot>().fight();
        ASSERT_TRUE(fight.fights_started() >= 1u);
        ASSERT_EQ(fight.shunned_count(), 1u);
        ASSERT_TRUE(fight.shunned(enemy, sim.current_tick()));
        ASSERT_EQ(fight.fights(), 0u);
    } TEST_END();
}

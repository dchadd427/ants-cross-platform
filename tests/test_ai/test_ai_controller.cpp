// Tests of the controller that stands between a bot and the door of a person: schedule, reaction delay, budget, time to live, priorities, ants that died,
// splitting, the rules of the HUD, the issuer, the anti-thrash cool-down, and above all that a controller whose bots only read changes nothing (AI2.3 .. AI2.16); AI2.21: a refused
// click has a fate (Bot::Fate::Filtered); AI2.23 .. AI2.28: timed chains (Orders::chain, CommandSink::applied_at).
#include "ai_test.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "ants_ai/idle_bot.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

constexpr uint8_t kAll = 0x0F;

BotSpec spec_of(uint8_t seat, Level level, const char* kind = "idle") {
    BotSpec s;
    s.seat = seat;
    s.kind = kind;
    s.level = level;
    return s;
}

// Seats a scripted bot; returns it (the controller owns it)
ScriptBot* seat_script(BotController& c, sim::SimulationEngine&, const BotSpec& spec, sim::CommandSink& sink, ScriptBot::Think think = {}) {
    auto bot = std::make_unique<ScriptBot>(std::move(think));
    ScriptBot* raw = bot.get();
    std::string why;
    if (!c.add(spec, std::move(bot), sink, why)) {
        std::cout << "    add failed: " << why << "\n";
        return nullptr;
    }
    return raw;
}

void run_ticks(sim::SimulationEngine& sim, BotController& c, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        sim.tick();
        c.on_tick(sim);
    }
}

// How many commands left in the ticks first .. last (inclusive)
size_t released_between(const RecordingSink& sink, uint64_t first, uint64_t last) {
    size_t n = 0;
    for (const auto& e : sink.log) n += (e.first >= first && e.first <= last) ? 1u : 0u;
    return n;
}

uint32_t level_ticks(Level l) { return profile_for(l).decision_interval; }

// The token bucket of the start hold, by hand (the specification, not a copy of the controller): ONE token to begin with, nothing added up to tick `first_refill - 1`, from `first_refill` on
// rate / 20 thousandths of a command per tick (after t ticks exactly floor(rate * t / 20): the controller carries the remainder), never more than the level's depth. `n` commands that are all due
// on tick `due` leave one per token, each on the first tick (from `due` on) that holds one. Returns the ticks they leave on. (The product's hold is the first tick, so its first_refill is 1.)
std::vector<uint64_t> bucket_departures(const Profile& p, uint64_t first_refill, uint64_t due, size_t n) {
    std::vector<uint64_t> out;
    int64_t tokens = 1000;
    const int64_t depth = static_cast<int64_t>(p.burst) * 1000;
    const int64_t rate = p.rate_milli_cps;
    int64_t refills = 0;
    for (uint64_t t = 0; out.size() < n && t < due + 100000; ++t) {
        if (t >= first_refill) {
            ++refills;
            tokens = std::min<int64_t>(tokens + rate * refills / 20 - rate * (refills - 1) / 20, depth);
        }
        if (t < due) continue;
        while (tokens >= 1000 && out.size() < n) {
            tokens -= 1000;
            out.push_back(t);
        }
    }
    return out;
}

// How many one-ant commands one look of a level proposes in the start hold tests: all of them can leave inside the time to live, and more than a bucket of one token pays at once
size_t look_size(Level l) { return l == Level::Hard ? 8u : (l == Level::Medium ? 4u : 3u); }

// A longer hold than the product's, for the tests of the MECHANISM of the hold (v0.1.1 held the bots for the 100 ticks of the dialog, which then ran with the simulation; since v0.2.0 the
// simulation waits for the dialog and the product's hold is kStartHoldTicks = 1, but the controller's hold is a number, and a number that is larger must still do what it says)
constexpr uint32_t kLongHold = 100u;

// A sink that says when it would apply a command (CommandSink::applied_at): `lag` ticks after the call, or on the next even tick when `even` is set (the arena's turns of two ticks); it applies nothing,
// it records (the tick of the call, the tick it said, the command)
class TimedSink final : public sim::CommandSink {
public:
    struct Entry {
        uint64_t sent;
        uint64_t applied;
        sim::Command command;
    };
    TimedSink(sim::SimulationEngine& sim, uint32_t lag, bool even) : sim_(sim), lag_(lag), even_(even) {}
    sim::CommandResult submit(const sim::Command& c) override {
        log.push_back(Entry{sim_.current_tick(), applied_at(sim_.current_tick()), c});
        sim::CommandResult r;
        r.status = refuse ? sim::CommandResult::Status::Ignored : sim::CommandResult::Status::Applied;     // (a room that is paused, an order with no ant to take it)
        return r;
    }
    uint64_t applied_at(uint64_t now) const override {
        const uint64_t due = now + lag_;
        return even_ ? due + due % 2 : due;
    }
    void set_lag(uint32_t lag) noexcept { lag_ = lag; }
    bool refuse{false};
    std::vector<Entry> log;

private:
    sim::SimulationEngine& sim_;
    uint32_t lag_;
    bool even_;
};

std::vector<ChainStep> steps_of(const std::vector<std::tuple<int32_t, uint32_t, uint32_t>>& list) {      // (x, gap_lo, gap_hi); y is 30
    std::vector<ChainStep> out;
    for (const auto& e : list) {
        ChainStep st;
        st.tile = TileCoord{std::get<0>(e), 30};
        st.gap_lo = std::get<1>(e);
        st.gap_hi = std::get<2>(e);
        out.push_back(st);
    }
    return out;
}

}  // namespace

void run_controller_tests() {
    TEST_CASE("AI2.3 Budget: A Bot That Floods Never Gets More Commands Out Than The Bucket And The Rate Allow, In Any Window, At Every Level") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            build_world(sim, 3);
            RecordingSink sink(sim);
            BotController c(sim, 7);
            c.set_start_hold(0);   // from tick 0 against the opening bucket of v0.1.0 (a full one): the budget of the start hold is AI2.18
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            ASSERT_TRUE(mine.size() >= 12);
            size_t next = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, level), sink, [&](const BotView&, Orders& o) {
                for (int i = 0; i < 5; ++i) o.move({mine[next++ % mine.size()]}, TileCoord{30, 30});           // five commands at every look, one ant each: the cool-down never binds
            });
            ASSERT_TRUE(bot != nullptr);
            const Profile p = profile_for(level);
            run_ticks(sim, c, 1200);                                                                             // one minute
            const auto& st = c.stats(0);
            ASSERT_TRUE(st.released == sink.log.size() && st.released > 0);
            ASSERT_EQ(st.rejected, 0u);
            ASSERT_EQ(st.filtered, 0u);
            // every window that starts at a release: n commands in `len` ticks need n tokens, the bucket held at most `burst` and the refills during the window add the rest
            for (size_t i = 0; i < sink.log.size(); ++i) {
                for (size_t j = i; j < sink.log.size(); ++j) {
                    const uint64_t len = sink.log[j].first - sink.log[i].first + 1;
                    const uint64_t n = j - i + 1;
                    ASSERT_TRUE(n * 1000u <= static_cast<uint64_t>(p.burst) * 1000u + (len * p.rate_milli_cps + 19u) / 20u);
                }
            }
            // and the budget is used: a minute at the full rate (the bucket starts full)
            const double allowed = p.burst + p.rate_milli_cps / 1000.0 * 60.0;
            ASSERT_TRUE(st.released >= allowed * 0.9 && st.released <= allowed + 1);
            // the first `burst` commands leave together, as soon as their reaction time is over
            ASSERT_TRUE(released_between(sink, 0, 1 + 0 + p.reaction_delay * 2) >= std::min<size_t>(p.burst, 5));
            // nothing is lost without a trace: every command that was queued was released, expired, or is still waiting
            ASSERT_EQ(st.intents, st.released + st.expired + st.superseded + static_cast<uint32_t>(c.pending(0)) + static_cast<uint32_t>(bot->count(Bot::Fate::Pruned)));
        }
    } TEST_END();

    TEST_CASE("AI2.4 Reaction Delay: Every Command Leaves Between 75 And 125 Percent Of The Level's Reaction Time After The Decision; The Same Seat Always Waits The Same, Another Seat Or Match Does Not") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const Profile p = profile_for(level);
            const uint32_t spread = p.reaction_delay * p.jitter_percent / 100u;
            uint32_t lowest = 1000000;
            uint32_t highest = 0;
            std::vector<uint32_t> first_run;
            for (uint32_t seed = 1; seed <= 120; ++seed) {
                sim::SimulationEngine sim;
                build_world(sim, seed);
                RecordingSink sink(sim);
                BotController c(sim, seed);
                c.set_start_hold(0);   // the reaction delay of every look from tick 1 (the start hold's first look is AI2.17)
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                size_t next = 0;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, level), sink, [&](const BotView&, Orders& o) { o.move({mine[next++ % mine.size()]}, TileCoord{30, 30}); });
                ASSERT_TRUE(bot != nullptr);
                const uint32_t looks = std::min<uint32_t>(p.burst, 8);                                           // one command per look, the bucket pays for all of them
                run_ticks(sim, c, looks * p.decision_interval + p.reaction_delay * 2 + 5);
                ASSERT_TRUE(sink.log.size() >= looks && bot->thought.size() >= looks);
                for (uint32_t k = 0; k < looks; ++k) {
                    const uint32_t delay = static_cast<uint32_t>(sink.log[k].first - bot->thought[k]);
                    ASSERT_TRUE(delay >= p.reaction_delay - spread && delay <= p.reaction_delay + spread);
                    lowest = std::min(lowest, delay);
                    highest = std::max(highest, delay);
                    if (seed == 1) first_run.push_back(delay);
                }
            }
            ASSERT_EQ(lowest, p.reaction_delay - spread);                                                        // the whole range is used
            ASSERT_EQ(highest, p.reaction_delay + spread);
            ASSERT_TRUE(spread > 0);
            // reproducible: the same match seed and seat give the same delays; another match seed, or another seat, give others
            auto delays_of = [&](uint32_t match_seed, uint8_t seat) {
                sim::SimulationEngine sim;
                build_world(sim, 1);
                RecordingSink sink(sim);
                BotController c(sim, match_seed);
                c.set_start_hold(0);   // the reaction delay of every look from tick 1 (the start hold's first look is AI2.17)
                const std::vector<uint32_t> mine = ants_of(sim, seat);
                size_t next = 0;
                ScriptBot* bot = seat_script(c, sim, spec_of(seat, level), sink, [&](const BotView&, Orders& o) { o.move({mine[next++ % mine.size()]}, TileCoord{30, 30}); });
                run_ticks(sim, c, 8 * p.decision_interval + p.reaction_delay * 2 + 5);
                std::vector<uint32_t> out;
                for (size_t k = 0; k < sink.log.size() && k < bot->thought.size() && k < 8; ++k) out.push_back(static_cast<uint32_t>(sink.log[k].first - bot->thought[k]));
                return out;
            };
            ASSERT_TRUE(delays_of(9, 0) == delays_of(9, 0));
            ASSERT_TRUE(delays_of(9, 0) != delays_of(10, 0));
            ASSERT_TRUE(delays_of(9, 0) != delays_of(9, 1));
        }
    } TEST_END();

    TEST_CASE("AI2.5 Schedule: A Bot Looks Every decision_interval Ticks, The First Time On The Hold's First Tick Plus Its Seat (Tick 1 + Seat By Default And With The Hold Off, 100 + Seat With A Long Hold), So The Seats Never All Look On The Same Tick; Its Own Ants Are Its Own") {
        // The first look of every seat is on tick `hold + seat` with a start hold (the default is kStartHoldTicks = 1: the match clock waits for the "Get ready to play!" dialog, so the first tick of the
        // simulation is the first on which anybody can act; a longer hold, the mechanism, is kLongHold) and on tick `1 + seat` with the hold off (the opening of v0.1.0, kept under test).
        // Everything else about the schedule is the same.
        for (const uint32_t hold : {kStartHoldTicks, kLongHold, 0u}) {
            const uint32_t first = hold != 0 ? hold : 1u;
            sim::SimulationEngine sim;
            build_world(sim, 4);
            RecordingSink sink(sim);
            BotController c(sim, 1);
            c.set_start_hold(hold);
            std::vector<uint32_t> mine_seen[3];
            std::vector<uint32_t> others_seen[3];
            ScriptBot* b[3] = {};
            const Level levels[3] = {Level::Easy, Level::Medium, Level::Hard};
            for (uint8_t seat = 0; seat < 3; ++seat) {
                b[seat] = seat_script(c, sim, spec_of(seat, levels[seat]), sink, [&, seat](const BotView& v, Orders&) {
                    ASSERT_EQ(v.seat(), seat);
                    mine_seen[seat].push_back(static_cast<uint32_t>(v.mine().size()));
                    others_seen[seat].push_back(static_cast<uint32_t>(v.others().size()));
                    for (const AntView& a : v.mine()) ASSERT_EQ(a.team, seat);
                    for (const AntView& a : v.others()) ASSERT_TRUE(a.team != seat && a.hp > 0 && a.hp <= 10);     // another team's hit points are on the view (the owner's decision)
                });
                ASSERT_TRUE(b[seat] != nullptr && b[seat]->started && b[seat]->seat == seat);
            }
            run_ticks(sim, c, first + 420);
            for (uint8_t seat = 0; seat < 3; ++seat) {
                const uint32_t interval = level_ticks(levels[seat]);
                ASSERT_TRUE(b[seat]->thought.size() >= 420 / interval);
                ASSERT_EQ(b[seat]->thought[0], first + seat);
                for (size_t k = 1; k < b[seat]->thought.size(); ++k) ASSERT_EQ(b[seat]->thought[k] - b[seat]->thought[k - 1], interval);
                ASSERT_EQ(c.stats(seat).decisions, static_cast<uint32_t>(b[seat]->thought.size()));
                ASSERT_EQ(mine_seen[seat][0], 12u);
                ASSERT_EQ(others_seen[seat][0], 36u);
            }
            ASSERT_TRUE(b[0]->thought[0] != b[1]->thought[0] && b[1]->thought[0] != b[2]->thought[0]);
            // a bot that is seated later starts counting from then on (with the hold: seated after its end, so without a hold of its own)
            const uint32_t seated_at = hold != 0 ? hold + 50 : 50u;
            sim::SimulationEngine late;
            build_world(late, 4);
            RecordingSink late_sink(late);
            BotController lc(late, 1);
            lc.set_start_hold(hold);
            for (uint32_t i = 0; i < seated_at; ++i) late.tick();
            ScriptBot* lb = seat_script(lc, late, spec_of(2, Level::Medium), late_sink);
            ASSERT_TRUE(lb != nullptr);
            run_ticks(late, lc, 30);
            ASSERT_TRUE(!lb->thought.empty() && lb->thought[0] == seated_at + 3u);                                   // tick (when seated) + 1 + seat
        }
    } TEST_END();

    TEST_CASE("AI2.6 Time To Live: A Command That Cannot Be Paid Is Dropped Intent_ttl Ticks After Its Release Time And The Bot Is Told (Expired); The Rest Are Paid As The Bucket Refills") {
        sim::SimulationEngine sim;
        build_world(sim, 5);
        RecordingSink sink(sim);
        BotController c(sim, 3);
        c.set_start_hold(0);   // the time to live against the opening bucket of v0.1.0 (a full one of two): the start hold's bucket is AI2.18
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        bool proposed = false;
        ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Easy), sink, [&](const BotView&, Orders& o) {
            if (proposed) return;
            proposed = true;
            for (size_t i = 0; i < 10; ++i) o.move({mine[i]}, TileCoord{30, 30});                                // ten commands against a bucket of two and 0.4 per second
        });
        ASSERT_TRUE(bot != nullptr);
        const Profile p = profile_for(Level::Easy);
        const uint32_t spread = p.reaction_delay * p.jitter_percent / 100u;
        run_ticks(sim, c, 600);
        const auto& st = c.stats(0);
        ASSERT_EQ(st.intents, 10u);
        ASSERT_TRUE(st.expired > 0 && st.released >= 2);
        ASSERT_EQ(st.released + st.expired, 10u);                                                                  // each one was paid or dropped, none is left
        ASSERT_EQ(c.pending(0), 0u);
        ASSERT_EQ(bot->count(Bot::Fate::Expired), static_cast<size_t>(st.expired));
        ASSERT_EQ(bot->count(Bot::Fate::Sent), static_cast<size_t>(st.released));
        for (const auto& seen : bot->fates) {
            if (seen.fate != Bot::Fate::Expired) continue;
            const uint64_t after = seen.tick - bot->thought[0];                                                    // the look was at tick 1
            ASSERT_TRUE(after >= p.reaction_delay - spread + p.intent_ttl + 1 && after <= p.reaction_delay + spread + p.intent_ttl + 1);
            ASSERT_EQ(seen.command.type, CommandType::GroupMove);
            ASSERT_EQ(seen.command.ants.size(), 1u);
        }
        // the first two leave at once (the bucket), the others one by one as it refills: 0.4 per second is one command per 50 ticks
        ASSERT_TRUE(sink.log.size() >= 2 && sink.log[1].first <= 1 + p.reaction_delay + spread);
        for (size_t k = 3; k < sink.log.size(); ++k) ASSERT_TRUE(sink.log[k].first - sink.log[k - 1].first >= 49);
    } TEST_END();

    TEST_CASE("AI2.7 Ants That Died: They Leave The Commands That Wait; A Command With None Left Never Leaves (Pruned), One With Some Leaves With Fewer; Another Team's Ants Are Never Named") {
        sim::SimulationEngine sim;
        build_world(sim, 6);
        RecordingSink sink(sim);
        BotController c(sim, 3);
        c.set_start_hold(0);   // ants that die, from tick 1 (the start hold: AI2.17 - AI2.20)
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        const std::vector<uint32_t> theirs = ants_of(sim, 1);
        bool proposed = false;
        ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Medium), sink, [&](const BotView&, Orders& o) {
            if (proposed) return;
            proposed = true;
            o.move({mine[0], mine[1]}, TileCoord{30, 30});                          // A: one of them will die
            o.move({mine[2], mine[3], mine[4]}, TileCoord{31, 30});                  // B: all of them will die
            o.move({mine[5], theirs[0]}, TileCoord{32, 30});                         // C: a stranger among them
            o.move({theirs[1]}, TileCoord{33, 30});                                  // D: only a stranger
            o.stop({mine[6], mine[7]});                                              // E: a Stop loses its dead as well
        });
        ASSERT_TRUE(bot != nullptr);
        run_ticks(sim, c, 1);                                                        // the look: everything is queued, nothing has left (the reaction time has not passed)
        ASSERT_TRUE(proposed && sink.log.empty() && c.pending(0) == 5);
        sim.kill_unit(mine[0]);
        sim.kill_unit(mine[2]);
        sim.kill_unit(mine[3]);
        sim.kill_unit(mine[4]);
        sim.kill_unit(mine[6]);
        run_ticks(sim, c, 60);
        ASSERT_EQ(sink.log.size(), 3u);                                              // A, C and E left
        std::vector<std::vector<uint32_t>> sent;
        for (const auto& e : sink.log) sent.push_back(e.second.ants);
        ASSERT_TRUE(std::find(sent.begin(), sent.end(), std::vector<uint32_t>{mine[1]}) != sent.end());          // A with the one that lives
        ASSERT_TRUE(std::find(sent.begin(), sent.end(), std::vector<uint32_t>{mine[5]}) != sent.end());          // C without the stranger
        ASSERT_TRUE(std::find(sent.begin(), sent.end(), std::vector<uint32_t>{mine[7]}) != sent.end());          // E
        const auto& st = c.stats(0);
        ASSERT_EQ(st.pruned, 5u);                                                    // A, B, C, D and E each lost ants
        ASSERT_EQ(st.released, 3u);
        ASSERT_EQ(st.expired, 0u);
        ASSERT_EQ(bot->count(Bot::Fate::Pruned), 2u);                                // B and D never left
        ASSERT_EQ(bot->count(Bot::Fate::Sent), 3u);
        for (const auto& seen : bot->fates) {
            if (seen.fate == Bot::Fate::Sent) ASSERT_TRUE(seen.command.issuer == 0 && seen.command.ants.size() == 1);    // the bot is told the ants that were still there
        }
        // after a Pruned command the same ant may be ordered again at once: nothing was recorded for it
        ASSERT_EQ(c.pending(0), 0u);
    } TEST_END();

    TEST_CASE("AI2.8 Issuer And Filter: The Seat Speaks Whatever The Bot Wrote; Quit, Drop, None, 33 Ants, No Ants, A Special Order Of Two Ants, A Tile Off The Map, A Bad Alliance Partner Never Leave") {
        sim::SimulationEngine sim;
        build_world(sim, 7);
        RecordingSink sink(sim);
        BotController c(sim, 3);
        c.set_start_hold(0);   // the filter and the issuer, from tick 1 (the start hold: AI2.17 - AI2.20)
        const std::vector<uint32_t> mine = ants_of(sim, 1);
        std::vector<uint32_t> many;
        for (uint32_t i = 0; i < 33; ++i) many.push_back(mine[i % mine.size()]);
        const auto raw = [&](Orders& o, CommandType type, std::vector<uint32_t> ants, int16_t x = 30, int16_t y = 30, uint8_t other = 255) {
            Command cmd;
            cmd.type = type;
            cmd.issuer = 3;                                                           // not this seat's to say
            cmd.other_player = other;
            cmd.tile_x = x;
            cmd.tile_y = y;
            cmd.ants = std::move(ants);
            o.push_unchecked(std::move(cmd));
        };
        bool done = false;
        ScriptBot* bot = seat_script(c, sim, spec_of(1, Level::Hard), sink, [&](const BotView&, Orders& o) {
            if (done) return;
            done = true;
            raw(o, CommandType::Quit, {});
            raw(o, CommandType::Drop, {});
            raw(o, CommandType::None, {});
            raw(o, static_cast<CommandType>(200), {});
            raw(o, CommandType::GroupMove, many);                                    // more than one command may hold
            raw(o, CommandType::GroupMove, {});
            raw(o, CommandType::GroupSpecial, {mine[0], mine[1]});                   // the HUD sends a special order for one ant
            raw(o, CommandType::GroupAttack, {mine[0]}, 999, 5);                     // off the map
            raw(o, CommandType::GroupMove, {mine[0]}, -1, 5);
            raw(o, CommandType::Stop, {});
            raw(o, CommandType::Hatch, {mine[0]});                                   // a command without an ant list names no ants
            raw(o, CommandType::AllianceInvite, {}, 0, 0, 1);                        // itself
            raw(o, CommandType::AllianceAccept, {}, 0, 0, 9);                        // nobody
            raw(o, CommandType::AllianceDeny, {}, 0, 0, 255);
            // what is fine, with the wrong issuer: it leaves as seat 1
            raw(o, CommandType::GroupMove, {mine[2]}, 20, 20);
            raw(o, CommandType::Hatch, {});
            raw(o, CommandType::AllianceInvite, {}, 0, 0, 2);
            o.break_alliance();
        });
        ASSERT_TRUE(bot != nullptr);
        run_ticks(sim, c, 60);
        const auto& st = c.stats(1);
        ASSERT_EQ(st.filtered, 15u);                                                   // (the break of an alliance that does not exist is the 15th)
        ASSERT_EQ(st.intents, 3u);
        ASSERT_EQ(st.released, 3u);
        ASSERT_EQ(sink.log.size(), 3u);
        for (const auto& e : sink.log) {
            ASSERT_EQ(e.second.issuer, 1);                                            // the seat, not 3
            ASSERT_TRUE(e.second.type != CommandType::Quit && e.second.type != CommandType::Drop && e.second.type != CommandType::None);
        }
        // the order of the proposals is kept inside a class: the group move first, the break of the alliance (urgent) among the first to leave
        ASSERT_TRUE(std::any_of(sink.log.begin(), sink.log.end(), [](const auto& e) { return e.second.type == CommandType::GroupMove && e.second.tile_x == 20; }));
        ASSERT_TRUE(std::any_of(sink.log.begin(), sink.log.end(), [](const auto& e) { return e.second.type == CommandType::AllianceInvite && e.second.other_player == 2; }));
        ASSERT_FALSE(std::any_of(sink.log.begin(), sink.log.end(), [](const auto& e) { return e.second.type == CommandType::AllianceBreak; }));      // no ally: nothing to break
        ASSERT_EQ(bot->count(Bot::Fate::Sent), 3u);
        // a bot that does not exist in the registry cannot be seated through the registry's door, and the Orders type has no way to say Quit or Drop
        Orders orders;
        orders.move({mine[0]}, TileCoord{1, 1});
        orders.attack({mine[0]}, TileCoord{1, 1});
        orders.special(mine[0], TileCoord{1, 1});
        orders.stop({mine[0]});
        orders.hatch();
        orders.invite(0);
        orders.accept(0);
        orders.deny(0);
        orders.withdraw(0);
        orders.break_alliance();
        ASSERT_EQ(orders.intents().size(), 10u);
        for (const Intent& in : orders.intents()) ASSERT_TRUE(in.command.type != CommandType::Quit && in.command.type != CommandType::Drop && in.command.issuer == 255);
        ASSERT_TRUE(orders.intents()[1].priority == Priority::Urgent && orders.intents()[0].priority == Priority::Normal && orders.intents()[6].priority == Priority::Urgent);
        orders.move({}, TileCoord{1, 1});
        ASSERT_EQ(orders.intents().size(), 10u);                                      // an empty list is nothing
        orders.clear();
        ASSERT_TRUE(orders.intents().empty());
        std::vector<uint32_t> forty;
        for (uint32_t i = 0; i < 40; ++i) forty.push_back(i + 1);
        orders.move(forty, TileCoord{100000, -100000});
        ASSERT_TRUE(orders.intents().size() == 2 && orders.intents()[0].command.ants.size() == 32 && orders.intents()[1].command.ants.size() == 8);   // a command holds 32 at the most
        ASSERT_TRUE(orders.intents()[0].command.tile_x == 32767 && orders.intents()[0].command.tile_y == -32768);                                    // (a tile that does not fit is off the map, not wrapped onto it)
    } TEST_END();

    TEST_CASE("AI2.9 Splitting And HUD Rules: A Command Is Cut Into Parts Of At Most 24 Ants (12 At Easy), Each Part Pays; A Special Order Names One Ant; Nobody Attacks An Ally") {
        {   // splitting at the three levels: the cap of the level, never above 24
            struct Case {
                Level level;
                std::vector<size_t> sizes;
            };
            const Case cases[] = {{Level::Medium, {24, 8, 8}}, {Level::Hard, {24, 8, 8}}, {Level::Easy, {12, 12, 8, 8}}};
            for (const Case& cs : cases) {
                sim::SimulationEngine sim;
                build_world(sim, 8, 40);
                RecordingSink sink(sim);
                BotController c(sim, 3);
                c.set_start_hold(0);   // splitting and the HUD's rules, from tick 1 (the start hold: AI2.17 - AI2.20)
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                ASSERT_EQ(mine.size(), 40u);
                bool done = false;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, cs.level), sink, [&](const BotView&, Orders& o) {
                    if (done) return;
                    done = true;
                    o.move(mine, TileCoord{30, 30});                                    // forty ants: the Orders type cuts at 32, the controller at the HUD's 24
                });
                ASSERT_TRUE(bot != nullptr);
                run_ticks(sim, c, 700);                                                  // (Easy pays four commands slowly)
                ASSERT_EQ(c.stats(0).intents, static_cast<uint32_t>(cs.sizes.size()));
                ASSERT_EQ(c.stats(0).released + c.stats(0).expired, static_cast<uint32_t>(cs.sizes.size()));
                std::multiset<size_t> got;
                std::set<uint32_t> named;
                for (const auto& e : sink.log) {
                    got.insert(e.second.ants.size());
                    ASSERT_TRUE(e.second.ants.size() <= profile_for(cs.level).max_ants_per_command && e.second.ants.size() <= kHudAntCap);
                    for (const uint32_t id : e.second.ants) ASSERT_TRUE(named.insert(id).second);               // no ant twice
                }
                if (c.stats(0).expired == 0) {
                    ASSERT_TRUE(got == std::multiset<size_t>(cs.sizes.begin(), cs.sizes.end()));
                    ASSERT_EQ(named.size(), 40u);
                }
                ASSERT_EQ(c.stats(0).released, static_cast<uint32_t>(sink.log.size()));
                if (cs.level == Level::Easy && sink.log.size() >= 3) {                  // every part pays: the bucket of Easy holds two, the third part waits for the refill (0.4 per second)
                    ASSERT_TRUE(sink.log[2].first >= sink.log[0].first + 50);
                }
            }
        }
        {   // a special order names one ant; a thief's raid is one (the bot gives the enemy hill as the tile)
            sim::SimulationEngine sim;
            build_world(sim, 9);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // splitting and the HUD's rules, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            bool done = false;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (done) return;
                done = true;
                o.special(mine[0], TileCoord{51, 5});
                Command two;
                two.type = CommandType::GroupSpecial;
                two.tile_x = 51;
                two.tile_y = 5;
                two.ants = {mine[1], mine[2]};
                o.push_unchecked(two);
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 40);
            ASSERT_EQ(sink.log.size(), 1u);
            ASSERT_TRUE(sink.log[0].second.type == CommandType::GroupSpecial && sink.log[0].second.ants.size() == 1 && sink.log[0].second.ants[0] == mine[0]);
            ASSERT_EQ(c.stats(0).filtered, 1u);
        }
        {   // no attack on an ally: the alliance has to be broken first (the HUD asks a question); an attack on anybody else is fine
            sim::SimulationEngine sim;
            build_world(sim, 10);
            sim.form_alliance(0, 1);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // splitting and the HUD's rules, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            const auto tile_of = [&](uint8_t team) {
                for (const auto& a : sim.get_world_state().ants) {
                    if (a.player_id == team) return TileCoord{a.tile_x, a.tile_y};
                }
                return TileCoord{-1, -1};
            };
            const TileCoord ally = tile_of(1);
            const TileCoord enemy = tile_of(2);
            uint32_t round = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (round++ != 0) return;
                o.attack({mine[0]}, ally);
                o.attack({mine[1]}, enemy);
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 40);
            ASSERT_EQ(sink.log.size(), 1u);
            ASSERT_TRUE(sink.log[0].second.type == CommandType::GroupAttack && sink.log[0].second.tile_x == enemy.x && sink.log[0].second.tile_y == enemy.y);
            ASSERT_EQ(c.stats(0).filtered, 1u);
            sim.break_alliance(0, 1);                                                    // once the alliance is broken the same click is an attack
            uint32_t round1 = 0;
            ScriptBot* again = seat_script(c, sim, spec_of(1, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (round1++ != 0) return;
                o.attack({ants_of(sim, 1)[0]}, tile_of(0));
            });
            ASSERT_TRUE(again != nullptr);
            run_ticks(sim, c, 40);
            ASSERT_EQ(c.stats(1).filtered, 0u);
            ASSERT_EQ(c.stats(1).released, 1u);
        }
    } TEST_END();

    TEST_CASE("AI2.10 Priorities And Anti-Thrash: Urgent Before Normal Before Background (First Come First Served Inside A Class); An Ant Is Not Ordered Twice Within 10 Ticks Unless The Order Is Urgent") {
        {   // twelve commands against a starved bucket (Easy: two at once, then one per 50 ticks): after the first two the classes leave in order
            sim::SimulationEngine sim;
            build_world(sim, 11);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // priorities and the cool-down, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            bool done = false;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Easy), sink, [&](const BotView&, Orders& o) {
                if (done) return;
                done = true;
                // class in tile_x (0 background, 1 normal, 2 urgent), position inside the class in tile_y; proposed in a scrambled order
                const int order[12][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {1, 1}, {0, 2}, {1, 2}, {2, 2}, {1, 3}, {0, 3}, {2, 3}};
                for (size_t i = 0; i < 12; ++i) {
                    const Priority p = order[i][0] == 0 ? Priority::Background : (order[i][0] == 1 ? Priority::Normal : Priority::Urgent);
                    o.move({mine[i]}, TileCoord{order[i][0], order[i][1]}, p);
                }
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 900);
            ASSERT_TRUE(sink.log.size() >= 4);
            for (size_t k = 2; k + 1 < sink.log.size(); ++k) {                           // from the third on every command finds a crowd that is due
                const auto& a = sink.log[k].second;
                const auto& b = sink.log[k + 1].second;
                ASSERT_TRUE(a.tile_x > b.tile_x || (a.tile_x == b.tile_x && a.tile_y < b.tile_y));
            }
            ASSERT_EQ(bot->count(Bot::Fate::Sent) + bot->count(Bot::Fate::Expired), 12u);
        }
        {   // the same ant, an order that already left and then a newer one a look later (so the first is not superseded): a NORMAL order waits for the cool-down of
            // 10 ticks in every seed; an URGENT one does not wait for it (over the seeds it leaves less than 10 ticks after the first at least once)
            bool urgent_inside_cooldown = false;
            for (const Priority second : {Priority::Normal, Priority::Urgent}) {
                for (uint32_t seed = 1; seed <= 60; ++seed) {
                    sim::SimulationEngine sim;
                    build_world(sim, seed);
                    RecordingSink sink(sim);
                    BotController c(sim, seed);
                    c.set_start_hold(0);   // priorities and the cool-down, from tick 1 (the start hold: AI2.17 - AI2.20)
                    const std::vector<uint32_t> mine = ants_of(sim, 0);
                    uint32_t looks = 0;
                    ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                        ++looks;
                        if (looks == 1) o.move({mine[0]}, TileCoord{10, 10}, Priority::Normal);                     // A
                        if (looks == 4) o.move({mine[0]}, TileCoord{11, 10}, second);                               // a look 12 ticks later: A has left (it leaves 6 .. 10 ticks after its look)
                    });
                    ASSERT_TRUE(bot != nullptr);
                    run_ticks(sim, c, 80);
                    ASSERT_EQ(sink.log.size(), 2u);                                                              // both left: the held one waited, it did not expire (60 ticks to live)
                    ASSERT_EQ(sink.log[0].second.tile_x, 10);
                    const uint64_t gap = sink.log[1].first - sink.log[0].first;
                    if (second == Priority::Normal) ASSERT_TRUE(gap >= 10);
                    else if (gap < 10) urgent_inside_cooldown = true;
                }
            }
            ASSERT_TRUE(urgent_inside_cooldown);
        }
        {   // two different ants do not hold each other, and an ant is free again after exactly the cool-down
            sim::SimulationEngine sim;
            build_world(sim, 12);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // priorities and the cool-down, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            uint32_t looks = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                ++looks;
                if (looks % 3 == 1 && looks <= 13) o.move({mine[0]}, TileCoord{10 + static_cast<int>(looks), 10});     // the same ant at every third look (every 12 ticks): the one before has left
                if (looks == 1) o.move({mine[1]}, TileCoord{40, 40});
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 160);
            ASSERT_EQ(sink.log.size(), 6u);
            std::vector<uint64_t> of_first;
            for (const auto& e : sink.log) {
                if (e.second.ants[0] == mine[0]) of_first.push_back(e.first);
            }
            ASSERT_EQ(of_first.size(), 5u);
            for (size_t k = 1; k < of_first.size(); ++k) ASSERT_TRUE(of_first[k] - of_first[k - 1] >= profile_for(Level::Hard).reissue_cooldown);
            ASSERT_EQ(bot->count(Bot::Fate::Expired), 0u);
        }
    } TEST_END();

    TEST_CASE("AI2.11 A Controller Whose Bots Only Read Changes Nothing: The State Hash Of A Match With Idle Bots At Hard Level Equals The One Without, At Every Tick, On Four Maps") {
        for (const char* map : {"TINY", "MEDIUM", "TREASURE", "ISLANDS"}) {
            sim::SimulationEngine a;
            sim::SimulationEngine b;
            start_match(a, map, 7, kAll);
            start_match(b, map, 7, kAll);
            RecordingSink sink(b, true);
            BotController c(b, 7);
            std::string why;
            for (uint8_t seat = 1; seat < 4; ++seat) ASSERT_TRUE(c.add(spec_of(seat, Level::Hard), sink, why));           // idle bots at the fastest level: a look every 4 ticks
            ASSERT_TRUE(a.state_hash() == b.state_hash());
            const std::vector<uint32_t> mine = ants_of(a, 0);
            ASSERT_TRUE(mine.size() >= 3);
            int first_difference = -1;
            const int ticks = 3000;
            for (int t = 1; t <= ticks && first_difference < 0; ++t) {
                if (t % 50 == 1) {                                                                                       // the person at seat 0 plays, identically in both games
                    sim::Command cmd;
                    cmd.type = CommandType::GroupMove;
                    cmd.issuer = 0;
                    cmd.tile_x = static_cast<int16_t>(4 + (t / 50) % 25);
                    cmd.tile_y = static_cast<int16_t>(4 + (t / 25) % 25);
                    cmd.ants = {mine[(static_cast<size_t>(t) / 50) % 3]};
                    a.apply_command(cmd);
                    b.apply_command(cmd);
                }
                a.tick();
                b.tick();
                c.on_tick(b);
                if (a.state_hash().total != b.state_hash().total) first_difference = t;
            }
            ASSERT_EQ(first_difference, -1);
            for (uint8_t seat = 1; seat < 4; ++seat) {
                // they did look, every 4 ticks from the first tick on (the first look is on tick 1 + seat: the match clock waits for the "Get ready" dialog, so the first tick is the first anybody can look at)
                ASSERT_TRUE(c.stats(seat).decisions >= static_cast<uint32_t>(ticks / 4 - 1));
                ASSERT_TRUE(c.stats(seat).decisions <= static_cast<uint32_t>(ticks / 4 + 1));
                ASSERT_EQ(c.stats(seat).released, 0u);
                ASSERT_EQ(c.stats(seat).intents, 0u);
            }
            ASSERT_TRUE(sink.log.empty());
        }
    } TEST_END();

    TEST_CASE("AI2.12 The End: Nothing After The Match Is Over Or Once The Team Dropped; Seating Is Refused (Fog, Seat, Twice, Kind, No Bot); Reproducible; The Order Of --bot Does Not Matter") {
        {   // the match ends: the controller stops at once
            sim::SimulationEngine sim;
            build_world(sim, 13);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // the match ends after 3 s = 60 ticks: measured from tick 1 with the opening of v0.1.0 (the opening has its own tests: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            size_t next = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) { o.move({mine[next++ % mine.size()]}, TileCoord{30, 30}); });
            ASSERT_TRUE(bot != nullptr);
            sim.set_match_time_remaining_ms(3000);
            uint32_t ticks = 0;
            while (!sim.is_match_over() && ticks < 400) {
                sim.tick();
                c.on_tick(sim);
                ++ticks;
            }
            ASSERT_TRUE(sim.is_match_over());
            const uint32_t looks = c.stats(0).decisions;
            const uint32_t sent = c.stats(0).released;
            const size_t log_size = sink.log.size();
            ASSERT_TRUE(looks > 0 && sent > 0);
            for (int i = 0; i < 100; ++i) c.on_tick(sim);                              // (the engine does not tick any more: the controller must not act either)
            ASSERT_TRUE(c.stats(0).decisions == looks && c.stats(0).released == sent && sink.log.size() == log_size);
        }
        {   // a controller that is called for the first time after the match has ended, with a look that is long due, does not look and sends nothing
            sim::SimulationEngine sim;
            build_world(sim, 13);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // a look that is long due when the match has ended, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) { o.move({mine[0]}, TileCoord{30, 30}); });
            ASSERT_TRUE(bot != nullptr);
            sim.set_match_time_remaining_ms(3000);
            for (uint32_t ticks = 0; !sim.is_match_over() && ticks < 400; ++ticks) sim.tick();      // (the controller is not called)
            ASSERT_TRUE(sim.is_match_over());
            for (int i = 0; i < 20; ++i) c.on_tick(sim);
            ASSERT_TRUE(c.stats(0).decisions == 0 && c.stats(0).released == 0 && sink.log.empty() && bot->thought.empty());
        }
        {   // a team that dropped out has nothing left to say, the others go on
            sim::SimulationEngine sim;
            build_world(sim, 14);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // a team that drops out after its bot has been at work for 60 ticks, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine1 = ants_of(sim, 1);
            const std::vector<uint32_t> mine2 = ants_of(sim, 2);
            size_t n1 = 0;
            size_t n2 = 0;
            ScriptBot* b1 = seat_script(c, sim, spec_of(1, Level::Hard), sink, [&](const BotView&, Orders& o) { o.move({mine1[n1++ % mine1.size()]}, TileCoord{30, 30}); });
            ScriptBot* b2 = seat_script(c, sim, spec_of(2, Level::Hard), sink, [&](const BotView&, Orders& o) { o.move({mine2[n2++ % mine2.size()]}, TileCoord{30, 31}); });
            ASSERT_TRUE(b1 != nullptr && b2 != nullptr);
            run_ticks(sim, c, 60);
            sim.drop_player(1);
            ASSERT_FALSE(sim.is_match_over());
            const uint32_t looks1 = c.stats(1).decisions;
            const uint32_t looks2 = c.stats(2).decisions;
            run_ticks(sim, c, 100);
            ASSERT_EQ(c.stats(1).decisions, looks1);
            ASSERT_EQ(c.pending(1), 0u);
            ASSERT_TRUE(c.stats(2).decisions > looks2);
        }
        {   // what cannot be seated
            sim::SimulationEngine sim;
            start_match(sim, "TINY", 1, 0x03);
            RecordingSink sink(sim);
            BotController c(sim, 1);
            std::string why;
            ASSERT_FALSE(c.add(spec_of(2, Level::Medium), sink, why));
            ASSERT_TRUE(why.find("not in the match") != std::string::npos);
            ASSERT_FALSE(c.add(spec_of(7, Level::Medium), sink, why));
            ASSERT_TRUE(why.find("not in the match") != std::string::npos);
            ASSERT_FALSE(c.add(spec_of(1, Level::Medium, "genius"), sink, why));
            ASSERT_TRUE(why.find("genius") != std::string::npos);
            ASSERT_FALSE(c.add(spec_of(1, Level::Medium), std::unique_ptr<Bot>(), sink, why));
            ASSERT_FALSE(why.empty());
            ASSERT_EQ(c.seat_mask(), 0u);
            ASSERT_TRUE(c.add(spec_of(1, Level::Medium), sink, why) && why.empty());
            ASSERT_FALSE(c.add(spec_of(1, Level::Hard), sink, why));
            ASSERT_TRUE(why.find("a bot already") != std::string::npos);
            ASSERT_EQ(c.seat_mask(), 0x02u);
            ASSERT_TRUE(c.has_seat(1) && !c.has_seat(0) && c.bot(1) != nullptr && c.bot(0) == nullptr && c.profile(1) != nullptr && c.profile(0) == nullptr);
            ASSERT_EQ(std::string(c.bot(1)->kind()), "idle");
            const BotController::SeatStats& none = c.stats(0);                           // a seat without a bot: all zero
            ASSERT_TRUE(none.decisions == 0 && none.released == 0 && none.filtered == 0 && c.pending(0) == 0);
            // Fog of War: a bot would see through it
            sim::SimulationEngine fog;
            fog.set_fog_of_war_enabled(true);
            start_match(fog, "TINY", 1, 0x03);
            RecordingSink fog_sink(fog);
            BotController fc(fog, 1);
            ASSERT_FALSE(fc.add(spec_of(1, Level::Medium), fog_sink, why));
            ASSERT_TRUE(why.find("Fog of War") != std::string::npos);
            ASSERT_EQ(fc.seat_mask(), 0u);
        }
        {   // reproducible from the match seed (nothing else is read): two runs leave the same commands at the same ticks, another seed does not
            auto trace = [](uint32_t match_seed) {
                sim::SimulationEngine sim;
                build_world(sim, 15);
                RecordingSink sink(sim, true);
                BotController c(sim, match_seed);
                const std::vector<uint32_t> m0 = ants_of(sim, 0);
                const std::vector<uint32_t> m1 = ants_of(sim, 1);
                size_t n0 = 0;
                size_t n1 = 0;
                seat_script(c, sim, spec_of(1, Level::Medium), sink, [&](const BotView&, Orders& o) { const size_t k = n1++; o.move({m1[k % m1.size()]}, TileCoord{20 + static_cast<int>(k % 9), 20}); });
                seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) { const size_t k = n0++; o.move({m0[k % m0.size()]}, TileCoord{25, 20 + static_cast<int>(k % 9)}); });
                run_ticks(sim, c, 500);
                std::vector<std::pair<uint64_t, std::vector<uint8_t>>> out;
                for (const auto& e : sink.log) {
                    std::vector<uint8_t> bytes;
                    sim::encode(e.second, bytes);
                    out.emplace_back(e.first, std::move(bytes));
                }
                return std::make_pair(out, sim.state_hash().total);
            };
            const auto first = trace(5);
            const auto second = trace(5);
            ASSERT_TRUE(first.first.size() > 50);
            ASSERT_TRUE(first == second);
            ASSERT_TRUE(trace(6).first != first.first);
        }
        {   // seats are processed in seat order whatever the order they were added in (the order of the --bot options)
            sim::SimulationEngine sim;
            build_world(sim, 16);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(3, Level::Hard), sink, why) && c.add(spec_of(1, Level::Hard), sink, why) && c.add(spec_of(2, Level::Hard), sink, why));
            ASSERT_EQ(c.seat_mask(), 0x0Eu);
        }
    } TEST_END();

    TEST_CASE("AI2.13 One Reaction Time Per Look: The Commands Of One Decision Leave In The Order They Were Proposed, At Every Level, Over Many Seeds; Looks Never Overtake Each Other; The Delay Still Varies From Seed To Seed") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            uint64_t earliest = ~0ull;
            uint64_t latest = 0;
            for (uint32_t seed = 1; seed <= 150; ++seed) {
                sim::SimulationEngine sim;
                build_world(sim, 21);
                RecordingSink sink(sim);
                BotController c(sim, seed);
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                uint32_t looks = 0;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, level), sink, [&](const BotView&, Orders& o) {
                    if (++looks > 2) return;
                    const int base = looks == 1 ? 0 : 3;                                    // two looks, three commands each, all Normal, different ants
                    o.stop({mine[static_cast<size_t>(base)]});
                    o.move({mine[static_cast<size_t>(base + 1)]}, TileCoord{10 + base, 10});
                    o.move({mine[static_cast<size_t>(base + 2)]}, TileCoord{11 + base, 11});
                });
                ASSERT_TRUE(bot != nullptr);
                run_ticks(sim, c, 800);
                const Profile level_profile = profile_for(level);
                ASSERT_TRUE(sink.log.size() >= std::min<size_t>(level_profile.burst, 6u));        // (a short bucket at Easy lets only some of the six leave in time)
                size_t last_index = 0;
                for (size_t k = 0; k < sink.log.size(); ++k) {                                    // what left, left in the order proposed: stop, move, move, stop, move, move
                    const auto& e = sink.log[k].second;
                    const size_t ant_index = static_cast<size_t>(std::find(mine.begin(), mine.end(), e.ants[0]) - mine.begin());
                    ASSERT_TRUE(ant_index < 6u && (k == 0 || ant_index > last_index));
                    ASSERT_TRUE((ant_index % 3 == 0) == (e.type == CommandType::Stop));
                    last_index = ant_index;
                }
                const uint64_t first_look = bot->thought[0];
                earliest = std::min(earliest, sink.log[0].first - first_look);
                latest = std::max(latest, sink.log[0].first - first_look);
                const Profile p = profile_for(level);
                const uint32_t spread = p.reaction_delay * p.jitter_percent / 100u;
                ASSERT_TRUE(sink.log[0].first - first_look >= p.reaction_delay - spread && sink.log[0].first - first_look <= p.reaction_delay + spread + 2u);   // (+ the ticks a token may take)
            }
            ASSERT_TRUE(latest > earliest);                                                   // the jitter is real: another seed waits another time
        }
    } TEST_END();

    TEST_CASE("AI2.14 The Newest Order Wins: Among The Orders That Are Due, A Later Order For An Ant Takes It Out Of The Older Ones, An Older Order Left With No Ant Never Leaves (Superseded); An Order That Is Not Due Yet Takes Nothing; A Bot That Re-Issues At Every Look Is Not Starved") {
        {   // inside one look: stop {0, 1}, then move {1, 2}: the stop keeps ant 0, the move has ants 1 and 2
            sim::SimulationEngine sim;
            build_world(sim, 22);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // the newest order wins, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            uint32_t looks = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (++looks != 1) return;
                o.stop({mine[0], mine[1]});
                o.move({mine[1], mine[2]}, TileCoord{20, 20});
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 100);
            ASSERT_EQ(sink.log.size(), 2u);
            ASSERT_TRUE(sink.log[0].second.type == CommandType::Stop && sink.log[0].second.ants == std::vector<uint32_t>{mine[0]});
            ASSERT_TRUE(sink.log[1].second.type == CommandType::GroupMove && sink.log[1].second.ants == (std::vector<uint32_t>{mine[1], mine[2]}));
            ASSERT_EQ(c.stats(0).superseded, 0u);                                              // nothing was left empty
            ASSERT_EQ(bot->count(Bot::Fate::Superseded), 0u);
        }
        {   // inside one look: two moves for the same ant: only the second leaves
            sim::SimulationEngine sim;
            build_world(sim, 22);
            RecordingSink sink(sim);
            BotController c(sim, 3);
            c.set_start_hold(0);   // the newest order wins, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            uint32_t looks = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (++looks != 1) return;
                o.move({mine[0]}, TileCoord{10, 10});
                o.move({mine[0]}, TileCoord{20, 20});
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 100);
            ASSERT_EQ(sink.log.size(), 1u);
            ASSERT_EQ(sink.log[0].second.tile_x, 20);
            ASSERT_EQ(c.stats(0).superseded, 1u);
            ASSERT_EQ(bot->count(Bot::Fate::Superseded), 1u);
            ASSERT_EQ(c.stats(0).intents, c.stats(0).released + c.stats(0).superseded);
        }
        {   // across looks: a look 4 ticks later retargets the ant before the first order has left (it leaves 6 .. 10 ticks after its look). The first order is NOT cancelled
            // by an order that is not due yet: it leaves when it is due (in the rare seat where both are due on the same tick the newer one wins and the older never leaves),
            // the newer one follows after the cool-down, and whatever happens the LAST order that leaves is the newest one
            size_t both = 0;
            size_t newest_only = 0;
            for (uint32_t seed = 1; seed <= 80; ++seed) {
                sim::SimulationEngine sim;
                build_world(sim, 23);
                RecordingSink sink(sim);
                BotController c(sim, seed);
                c.set_start_hold(0);   // the newest order wins, from tick 1 (the start hold: AI2.17 - AI2.20)
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                uint32_t looks = 0;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                    ++looks;
                    if (looks == 1) o.move({mine[0]}, TileCoord{10, 10});
                    if (looks == 2) o.move({mine[0]}, TileCoord{20, 20});                      // 4 ticks later
                    if (looks == 6) o.move({mine[0]}, TileCoord{30, 30});                      // 20 ticks after the second look: the second has left by now
                });
                ASSERT_TRUE(bot != nullptr);
                run_ticks(sim, c, 160);
                ASSERT_TRUE(sink.log.size() == 2u || sink.log.size() == 3u);
                ASSERT_EQ(sink.log.back().second.tile_x, 30);                                 // the newest order is the last one that left
                if (sink.log.size() == 3u) {
                    ++both;
                    ASSERT_TRUE(sink.log[0].second.tile_x == 10 && sink.log[1].second.tile_x == 20);
                    ASSERT_TRUE(sink.log[1].first - sink.log[0].first >= 10);                  // the cool-down spaced them out
                    ASSERT_EQ(bot->count(Bot::Fate::Superseded), 0u);
                } else {
                    ++newest_only;                                                             // both were due on the same tick: only the newer left
                    ASSERT_EQ(sink.log[0].second.tile_x, 20);
                    ASSERT_EQ(bot->count(Bot::Fate::Superseded), 1u);
                }
            }
            ASSERT_TRUE(both > 0 && newest_only + both == 80u);
        }
        {   // a bot that re-issues an order at EVERY look (every 4 ticks at Hard, while its reaction time is 6 .. 10 ticks) is not starved by its own newer looks: orders leave
            // as they come due, one per cool-down, and the last one it proposed is the last that leaves
            sim::SimulationEngine sim;
            build_world(sim, 24);
            RecordingSink sink(sim);
            BotController c(sim, 5);
            c.set_start_hold(0);   // the newest order wins, from tick 1 (the start hold: AI2.17 - AI2.20)
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            uint32_t looks = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (++looks <= 50) o.move({mine[0]}, TileCoord{10 + static_cast<int>(looks % 2) * 4, 10});
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 400);
            ASSERT_TRUE(sink.log.size() >= 15);                                               // about one per cool-down over the 200 ticks of looks
            for (size_t k = 1; k < sink.log.size(); ++k) ASSERT_TRUE(sink.log[k].first - sink.log[k - 1].first >= 10);
            ASSERT_EQ(c.stats(0).intents, c.stats(0).released + c.stats(0).superseded + c.stats(0).expired + static_cast<uint32_t>(c.pending(0)));
        }
    } TEST_END();

    TEST_CASE("AI2.15 Alliance Commands Follow The HUD: An Answer Needs Its Invitation, A Withdrawal Its Offer, A Break An Ally, An Offer A Team That Plays; Nobody Else Is Addressed") {
        sim::SimulationEngine sim;
        build_world(sim, 24);
        RecordingSink sink(sim, true);                                                        // forwarded to the engine: the offers and answers are real
        BotController c(sim, 3);
        uint32_t looks = 0;
        std::function<void(Orders&)> next;
        ScriptBot* b1 = seat_script(c, sim, spec_of(1, Level::Hard), sink, [&](const BotView&, Orders& o) {
            ++looks;
            if (!next) return;
            const std::function<void(Orders&)> once = std::move(next);                       // each script runs at ONE look
            next = nullptr;
            once(o);
        });
        ASSERT_TRUE(b1 != nullptr);
        const auto run_look = [&](std::function<void(Orders&)> what) {
            next = std::move(what);
            for (uint32_t i = 0; i < 200 && next; ++i) run_ticks(sim, c, 1);                 // until a look has used it
            run_ticks(sim, c, 30);                                                            // the commands of the look leave
        };
        const auto sent = [&](CommandType t) { return std::count_if(sink.log.begin(), sink.log.end(), [&](const auto& e) { return e.second.type == t; }); };
        // nothing to answer, nothing to take back, nobody to break with: all filtered
        run_look([](Orders& o) { o.accept(0); o.deny(0); o.withdraw(0); o.break_alliance(); });
        ASSERT_EQ(c.stats(1).filtered, 4u);
        ASSERT_EQ(sink.log.size(), 0u);
        // an offer to a team that plays is fine; to a team that does not exist or that dropped out it is not
        run_look([](Orders& o) { o.invite(0); o.invite(3); });
        ASSERT_EQ(sent(CommandType::AllianceInvite), 2);
        sim.drop_player(3);
        run_look([](Orders& o) { o.invite(3); });
        ASSERT_EQ(c.stats(1).filtered, 5u);
        // the offer to seat 0 waits: seat 1 may take it back, seat 1 may not take back an offer to seat 2 that was never made, nor answer one it did not get
        run_look([](Orders& o) { o.withdraw(2); o.accept(0); });
        ASSERT_EQ(c.stats(1).filtered, 7u);
        run_look([](Orders& o) { o.withdraw(0); });
        ASSERT_EQ(sent(CommandType::AllianceWithdraw), 1);
        // an invitation from seat 2 to seat 1: seat 1 may answer it (not seat 0's, which it did not get)
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.issuer = 2;
        invite.other_player = 1;
        ASSERT_TRUE(sim.apply_command(invite).status == sim::CommandResult::Status::Applied);
        run_look([](Orders& o) { o.deny(0); o.accept(2); });
        ASSERT_EQ(c.stats(1).filtered, 8u);
        ASSERT_EQ(sent(CommandType::AllianceAccept), 1);
        ASSERT_EQ(sim.get_ally_id(1), 2);                                                     // it really is an alliance now
        // and now seat 1 has an ally: it may break it
        run_look([](Orders& o) { o.break_alliance(); });
        ASSERT_EQ(sent(CommandType::AllianceBreak), 1);
        ASSERT_TRUE(sim.get_ally_id(1) >= sim::MAX_PLAYERS);
    } TEST_END();

    TEST_CASE("AI2.16 The View Shows What A Player Could Know: The Score Boxes' Numbers (Own Plus Ally, Never Below 0), Other Teams' Hit Points And Carried Points Hidden, Own Ants Exact, Dropped Teams And The Invitations") {
        sim::SimulationEngine sim;
        build_world(sim, 25);
        sim.set_player_score(0, 100);
        sim.set_player_score(1, 50);
        sim.set_player_score(2, -70);                                                         // the score box draws 0 for it
        sim.set_player_score(3, 30);
        sim.form_alliance(0, 1);
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.issuer = 3;
        invite.other_player = 2;
        ASSERT_TRUE(sim.apply_command(invite).status == sim::CommandResult::Status::Applied);
        sim.tick();
        const sim::WorldState& ws = sim.get_world_state();
        bool engine_knows_other_hp = false;
        for (const sim::AntSnapshot& a : ws.ants) engine_knows_other_hp = engine_knows_other_hp || (a.player_id != 0 && a.hp > 0);
        ASSERT_TRUE(engine_knows_other_hp);                                                   // (so the hit points below come from the engine: the view hides only the carried points)
        const BotView v0 = BotView::build(sim, 0);
        ASSERT_EQ(v0.score(), 150);                                                           // 100 + its ally's 50
        ASSERT_EQ(v0.rows()[1].score, 150);
        ASSERT_EQ(v0.rows()[2].score, 0);                                                     // -70: the box draws 0
        ASSERT_EQ(v0.rows()[3].score, 30);
        ASSERT_EQ(v0.ally(), 1);
        ASSERT_EQ(v0.rows()[1].ally, 0);
        ASSERT_EQ(v0.invite_from(), 255);
        ASSERT_EQ(v0.mine().size(), 12u);
        ASSERT_EQ(v0.others().size(), 36u);
        for (const AntView& a : v0.mine()) ASSERT_TRUE(a.team == 0 && a.hp > 0);              // the own ants are exact
        for (const AntView& a : v0.others()) ASSERT_TRUE(a.team != 0 && a.hp > 0 && a.carried_points == 0);
        const BotView v2 = BotView::build(sim, 2);
        ASSERT_EQ(v2.invite_from(), 3);                                                       // the invitation that waits for seat 2
        ASSERT_EQ(v2.score(), 0);
        ASSERT_EQ(BotView::build(sim, 3).invite_from(), 255);
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) ASSERT_TRUE(v0.rows()[t].present && !v0.rows()[t].dropped);
        sim.drop_player(3);
        sim.tick();
        const BotView after = BotView::build(sim, 0);
        ASSERT_TRUE(after.rows()[3].dropped);
        for (const AntView& a : after.others()) ASSERT_TRUE(a.team != 3);                     // its ants are gone from the screen
        ASSERT_EQ(after.others().size(), 24u);
    } TEST_END();

    // ---- the opening of a seat (the start hold): kStartHoldTicks = 1 in every product path (since v0.2.0: the match clock waits for the "Get ready to play!" dialog, the simulation does not run while it
    // is up, so no bot can look or act while it is up; v0.1.1 held the bots for the dialog's 100 ticks while the simulation ran behind it). The mechanism takes any number: the tests below run
    // with the product's hold and with a long one (kLongHold). The rest of this suite measures the controller from tick 0 with the hold off. ----

    TEST_CASE("AI2.17 The Opening: A Bot That Wants To Act At Every Look Does Nothing Before The Simulation Runs (The Controller Acts On A Tick Only), Looks First On The Hold's First Tick + Seat (Tick 1 + Seat By Default: Nothing Is Held Back Behind The Dialog Any More), At Every Level From Every Seat, And Its First Command Leaves 75 To 125 Percent Of The Reaction Time After That Look; A Long Hold Still Holds") {
        ASSERT_EQ(kStartHoldTicks, 1u);                                                                  // the product's hold: the first tick
        for (const uint32_t hold : {kStartHoldTicks, kLongHold}) {
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                const Profile p = profile_for(level);
                const uint32_t spread = p.reaction_delay * p.jitter_percent / 100u;
                for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                    uint64_t earliest = ~0ull;
                    uint64_t latest = 0;
                    for (uint32_t seed = 1; seed <= 30; ++seed) {
                        sim::SimulationEngine sim;
                        build_world(sim, seed);
                        RecordingSink sink(sim);
                        BotController c(sim, seed);
                        if (hold != kStartHoldTicks) c.set_start_hold(hold);                           // (the default is the hold of every product path)
                        ASSERT_EQ(c.start_hold(), hold);
                        const std::vector<uint32_t> mine = ants_of(sim, seat);
                        size_t next = 0;
                        ScriptBot* bot = seat_script(c, sim, spec_of(seat, level), sink, [&](const BotView&, Orders& o) { o.move({mine[next++ % mine.size()]}, TileCoord{30, 30}); });
                        ASSERT_TRUE(bot != nullptr);
                        ASSERT_TRUE(bot->thought.empty() && c.stats(seat).decisions == 0 && c.stats(seat).intents == 0 && c.pending(seat) == 0 && sink.log.empty());   // nothing before the first tick
                        run_ticks(sim, c, hold - 1);                                                     // the ticks before the hold's first: nothing (none at all with the product's hold)
                        ASSERT_TRUE(bot->thought.empty() && c.stats(seat).decisions == 0 && c.stats(seat).intents == 0 && c.pending(seat) == 0 && sink.log.empty());
                        run_ticks(sim, c, p.reaction_delay * 2 + 8);                                     // from the hold's first tick on
                        ASSERT_FALSE(bot->thought.empty());
                        ASSERT_EQ(bot->thought[0], hold + seat);                                         // the first look: the hold's first tick and the seat's own stagger
                        ASSERT_FALSE(sink.log.empty());
                        for (const auto& e : sink.log) ASSERT_TRUE(e.first >= hold);
                        const uint64_t look = bot->thought[0];
                        const uint64_t first = sink.log.front().first;
                        ASSERT_TRUE(first >= look + p.reaction_delay - spread && first <= look + p.reaction_delay + spread);       // a reaction time of 75 to 125 percent after the look ...
                        ASSERT_TRUE(first >= hold + p.reaction_delay * 3 / 4 && first <= hold + p.reaction_delay * 5 / 4 + seat + 1);   // ... which is hold + 75 percent to hold + 125 percent + seat + 1
                        earliest = std::min(earliest, first - look);
                        latest = std::max(latest, first - look);
                    }
                    ASSERT_TRUE(latest > earliest);                                                      // the jitter is still real: another seed waits another time
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI2.18 The Opening's Bucket: A Seat Starts With ONE Token And Gets No More Before The Hold's First Tick, Then It Refills At The Level's Rate From That Tick On (From The First Tick By Default): The Commands Of One Look Leave One By One On The Ticks The Bucket Allows (No Burst In One Tick), At Every Level; A Flood Gets No More Out Than The Bucket Made") {
        // n one-ant commands of ONE look are all due on the same tick (one reaction time after the look); the departures are the law of the bucket
        for (const uint32_t hold : {kStartHoldTicks, kLongHold}) {
            for (const Level level : {Level::Hard, Level::Medium, Level::Easy}) {
                const Profile p = profile_for(level);
                const size_t n = look_size(level);
                size_t at_once_most = 0;
                for (uint32_t seed = 1; seed <= 40; ++seed) {
                    sim::SimulationEngine sim;
                    build_world(sim, seed);
                    RecordingSink sink(sim);
                    BotController c(sim, seed);
                    if (hold != kStartHoldTicks) c.set_start_hold(hold);
                    const std::vector<uint32_t> mine = ants_of(sim, 0);
                    bool done = false;
                    ScriptBot* bot = seat_script(c, sim, spec_of(0, level), sink, [&](const BotView&, Orders& o) {
                        if (done) return;
                        done = true;
                        for (size_t i = 0; i < n; ++i) o.move({mine[i]}, TileCoord{30 + static_cast<int32_t>(i), 30});     // one ant and one tile each: nothing is superseded, no cool-down binds
                    });
                    ASSERT_TRUE(bot != nullptr);
                    run_ticks(sim, c, hold + 400);
                    ASSERT_TRUE(sink.log.size() == n && c.stats(0).expired == 0 && bot->thought[0] == hold);
                    const uint64_t due = sink.log.front().first;                                            // the first leaves the moment it is due: the bucket holds its token
                    ASSERT_TRUE(due >= hold + p.reaction_delay * 3 / 4 && due <= hold + p.reaction_delay * 5 / 4);
                    const std::vector<uint64_t> expect = bucket_departures(p, hold, due, n);
                    ASSERT_EQ(expect.size(), n);
                    size_t at_once = 0;
                    for (size_t k = 0; k < n; ++k) {
                        ASSERT_EQ(sink.log[k].first, expect[k]);                                          // exactly the ticks of one token at the start and a refill from the hold's first tick
                        at_once += sink.log[k].first == due ? 1u : 0u;
                    }
                    at_once_most = std::max(at_once_most, at_once);
                }
                // v0.1.0's full bucket let min(n, burst) leave on the first tick: Hard 8, Medium 4. Now at most what one token and the refill of the reaction time make.
                if (level != Level::Easy) ASSERT_TRUE(at_once_most < n);
                ASSERT_TRUE(at_once_most <= 3u);
            }
            // a flood (five one-ant commands at every look) for twelve hundred ticks after the hold's first tick: no command leaves before its token is there, and the budget is used. The tokens made by
            // tick t are the one the seat started with and rate * (t - hold + 1) / 20000 more (a refill on every tick from the hold's first on).
            for (const Level level : {Level::Hard, Level::Medium, Level::Easy}) {
                const Profile p = profile_for(level);
                sim::SimulationEngine sim;
                build_world(sim, 3);
                RecordingSink sink(sim);
                BotController c(sim, 7);
                if (hold != kStartHoldTicks) c.set_start_hold(hold);
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                size_t next = 0;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, level), sink, [&](const BotView&, Orders& o) {
                    for (int i = 0; i < 5; ++i) o.move({mine[next++ % mine.size()]}, TileCoord{30, 30});
                });
                ASSERT_TRUE(bot != nullptr);
                run_ticks(sim, c, hold - 1 + 1200);
                ASSERT_TRUE(c.stats(0).released == sink.log.size() && !sink.log.empty());
                for (size_t i = 0; i < sink.log.size(); ++i) {
                    ASSERT_TRUE(sink.log[i].first >= hold);
                    ASSERT_TRUE(1000 * (static_cast<int64_t>(i) + 1) <= 1000 + static_cast<int64_t>(p.rate_milli_cps) * static_cast<int64_t>(sink.log[i].first - hold + 1) / 20);
                }
                const double made = 1.0 + p.rate_milli_cps / 1000.0 * 60.0;                              // twelve hundred ticks are a minute
                ASSERT_TRUE(c.stats(0).released >= made * 0.95 && c.stats(0).released <= made + 0.001);
            }
        }
    } TEST_END();

    TEST_CASE("AI2.19 The Hold, The Clamp On Its Own: A Look Before The Hold's First Tick (The Hold Is Raised From 20 To 100 Ticks After The Seat Was Seated, So It Looks On Tick 20 + Seat) Releases Nothing Before Tick 100; Two Commands Due Then Leave On Different Ticks, The Second One Token Later (6 Ticks At Hard, 13 At Medium, 49 At Easy)") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const Profile p = profile_for(level);
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                sim::SimulationEngine sim;
                build_world(sim, 9);
                RecordingSink sink(sim);
                BotController c(sim, 5);
                c.set_start_hold(20);                                                                    // a short hold: the seat's first look is on tick 20 + seat ...
                const std::vector<uint32_t> mine = ants_of(sim, seat);
                bool done = false;
                ScriptBot* bot = seat_script(c, sim, spec_of(seat, level), sink, [&](const BotView&, Orders& o) {
                    if (done) return;
                    done = true;
                    o.move({mine[0]}, TileCoord{30, 30});
                    o.move({mine[1]}, TileCoord{31, 30});
                });
                ASSERT_TRUE(bot != nullptr);
                c.set_start_hold(kLongHold);                                                             // ... and now the hold is 100 ticks: a look before the hold's first tick, which the schedule never makes
                run_ticks(sim, c, 99);
                ASSERT_TRUE(!bot->thought.empty() && bot->thought[0] == 20u + seat);                      // the look happened (and more looks followed, nothing more was proposed) ...
                ASSERT_EQ(c.stats(seat).intents, 2u);
                ASSERT_TRUE(sink.log.empty() && c.pending(seat) == 2);                                   // ... and its two commands wait, whatever their reaction time (6 to 10 ticks at Hard) says
                run_ticks(sim, c, 120);
                ASSERT_EQ(sink.log.size(), 2u);
                ASSERT_EQ(sink.log[0].first, 100u);                                                      // due on the hold's first tick: the first leaves with the one token the seat has
                const std::vector<uint64_t> expect = bucket_departures(p, 100, 100, 2);                   // nothing was added during the wait, the refill starts on tick 100 itself: 6, 13 and 49 ticks for a token
                ASSERT_EQ(expect.size(), 2u);
                ASSERT_EQ(expect[0], 100u);
                ASSERT_TRUE(expect[1] > 100u);
                ASSERT_EQ(sink.log[1].first, expect[1]);
                ASSERT_EQ(sink.log[1].first, 100u + (level == Level::Hard ? 6u : (level == Level::Medium ? 13u : 49u)));
                ASSERT_EQ(c.stats(seat).expired, 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI2.20 The Hold, A Seat That Comes Later: Seated Before The Hold's First Tick It First Looks On That Tick + Seat; Seated After It There Is No Hold For It (Its First Look Is On The Next Tick + Seat, The Refill Starts At Once) And It Starts With One Token Too") {
        for (const uint32_t hold : {kStartHoldTicks, kLongHold}) {
            for (const Level level : {Level::Hard, Level::Medium, Level::Easy}) {
                const Profile p = profile_for(level);
                const size_t n = look_size(level);
                for (const uint8_t seat : {uint8_t{0}, uint8_t{2}, uint8_t{3}}) {
                    for (const uint32_t seated_at : {0u, 1u, 60u, 99u, 100u, 150u, 300u}) {
                        sim::SimulationEngine sim;
                        build_world(sim, 4);
                        for (uint32_t i = 0; i < seated_at; ++i) sim.tick();                              // the match is under way, the controller is not there yet
                        RecordingSink sink(sim);
                        BotController c(sim, 3);
                        if (hold != kStartHoldTicks) c.set_start_hold(hold);
                        const std::vector<uint32_t> mine = ants_of(sim, seat);
                        bool done = false;
                        ScriptBot* bot = seat_script(c, sim, spec_of(seat, level), sink, [&](const BotView&, Orders& o) {
                            if (done) return;
                            done = true;
                            for (size_t i = 0; i < n; ++i) o.move({mine[i]}, TileCoord{30 + static_cast<int32_t>(i), 30});
                        });
                        ASSERT_TRUE(bot != nullptr);
                        run_ticks(sim, c, 500);
                        ASSERT_TRUE(!bot->thought.empty());
                        ASSERT_EQ(bot->thought[0], std::max<uint64_t>(seated_at + 1u, hold) + seat);       // before the hold's first tick: that tick + seat; after it: the next tick + seat (no hold)
                        ASSERT_TRUE(sink.log.size() == n && c.stats(seat).expired == 0);
                        const uint64_t due = sink.log.front().first;
                        ASSERT_TRUE(due >= bot->thought[0] + p.reaction_delay * 3 / 4 && due >= hold);
                        // the bucket: one token, and a refill from the hold's first tick, or from the tick after the seat came when that is later
                        const std::vector<uint64_t> expect = bucket_departures(p, std::max<uint64_t>(seated_at + 1u, hold), due, n);
                        ASSERT_EQ(expect.size(), n);
                        for (size_t k = 0; k < n; ++k) ASSERT_EQ(sink.log[k].first, expect[k]);
                        if (seated_at >= 300u && level == Level::Hard) {                                  // (a late seat is not a full bucket: v0.1.0 would have let all eight leave on the same tick)
                            size_t first_tick = 0;
                            for (const auto& e : sink.log) first_tick += e.first == due ? 1u : 0u;
                            ASSERT_TRUE(first_tick < n);
                        }
                    }
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI2.21 A Refused Click Has A Fate: The Bot Is Told (Fate::Filtered, At The Tick Of The Look, The Command As It Proposed It) Once For Every Intent The Filter Refuses, Before Anything Else Of That Look; The Count Of `filtered` Agrees; What Passes Is Sent And Never Filtered") {
        sim::SimulationEngine sim;
        build_world(sim, 7);
        sim.grid_mut().place_powerup(20, 20, 3);
        RecordingSink sink(sim);
        BotController c(sim, 3);
        c.set_start_hold(0);                                                              // the filter, from tick 1 (the start hold: AI2.17 - AI2.20)
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        size_t looks = 0;
        ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Medium), sink, [&](const BotView&, Orders& o) {
            if (++looks > 5) return;
            o.move({mine[0]}, tc(20, 20));                                                // refused: a plain click onto a power-up (only a planned pick-up may name one)
            if (looks == 2) o.special(mine[1], tc(20, 20));                               // refused: a special order onto a power-up tile
            if (looks == 3) o.move({mine[2]}, tc(22, 20));                                // passes: next to the power-up
            if (looks == 4) o.attack({mine[3]}, tc(25, 25));                              // refused: no enemy ant stands there
        });
        ASSERT_TRUE(bot != nullptr);
        run_ticks(sim, c, 300);
        ASSERT_TRUE(looks > 5);                                                              // (five looks proposed, the rest were silent)
        const BotController::SeatStats& st = c.stats(0);
        ASSERT_EQ(st.filtered, 7u);                                                       // five plain clicks, one special order, one attack
        ASSERT_EQ(st.released, 1u);
        ASSERT_EQ(bot->count(Bot::Fate::Filtered), static_cast<size_t>(st.filtered));     // once per refused intent
        ASSERT_EQ(bot->count(Bot::Fate::Sent), 1u);
        size_t at = 0;
        for (const ScriptBot::Seen& seen : bot->fates) {
            if (seen.fate != Bot::Fate::Filtered) continue;
            ASSERT_TRUE(seen.command.type == CommandType::GroupMove || seen.command.type == CommandType::GroupSpecial || seen.command.type == CommandType::GroupAttack);
            ASSERT_TRUE(seen.command.ants.size() == 1);                                   // the command as proposed
            ASSERT_TRUE(seen.tick >= 1 && std::count(bot->thought.begin(), bot->thought.end(), seen.tick) == 1);       // the tick of a look (told at once, not when the release would have been due)
            ++at;
        }
        ASSERT_EQ(at, 7u);
        const ScriptBot::Seen& first = bot->fates.front();                                // the first thing the bot hears is the refusal of its first click, in the look that proposed it
        ASSERT_TRUE(first.fate == Bot::Fate::Filtered && first.command.type == CommandType::GroupMove && first.command.tile_x == 20 && first.command.tile_y == 20 && first.command.ants[0] == mine[0]);
        ASSERT_EQ(first.tick, bot->thought.front());
        ASSERT_EQ(sink.log.size(), 1u);                                                   // nothing refused left
        ASSERT_TRUE(sink.log[0].second.tile_x == 22 && sink.log[0].second.ants.size() == 1 && sink.log[0].second.ants[0] == mine[2]);
    } TEST_END();

    TEST_CASE("AI2.22 A Special Order Never Names A Tile That A Living Ant Stands On, Whatever Team It Is (The Seat's Own, Its Ally, Either Enemy): The HUD's Target Cursor Shows Only Where No Ant Is Under The Pointer, So The Click Is Refused (Fate::Filtered) And Counted; The Same Order At A Free Tile Or At A Tile Where An Ant Has Died And Is Gone Passes, One That Is Still Dying Does Not (The HUD's Pick Lists It); The Simulation Is Not Changed By It") {
        sim::SimulationEngine sim;
        build_world(sim, 7, 4);
        sim.form_alliance(0, 3);
        // one ant of each kind of occupant on a tile of its own, in a row far from every hill
        const uint32_t own = sim.spawn_unit(0, sim::AntType::Worker, tc(20, 20));
        sim.spawn_unit(3, sim::AntType::Worker, tc(21, 20));
        sim.spawn_unit(1, sim::AntType::Worker, tc(22, 20));
        sim.spawn_unit(2, sim::AntType::Fire, tc(23, 20));                                        // (a Fire Ant stands on a wall: the same)
        const uint32_t dead = sim.spawn_unit(1, sim::AntType::Worker, tc(24, 20));
        sim.kill_unit(dead);
        for (int i = 0; i < 60; ++i) sim.tick();                                                  // (the dead ant is gone from the screen)
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        std::vector<uint32_t> senders;
        for (const uint32_t id : mine) {
            if (id != own) senders.push_back(id);
        }
        ASSERT_TRUE(senders.size() >= 4);
        RecordingSink sink(sim);
        BotController c(sim, 3);
        c.set_start_hold(0);
        const std::vector<std::pair<int32_t, bool>> tiles = {{20, false}, {21, false}, {22, false}, {23, false}, {24, true}, {25, true}};     // (x, passes)
        size_t looks = 0;
        ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
            if (looks < tiles.size()) o.special(senders[looks % senders.size()], tc(tiles[looks].first, 20));
            ++looks;
        });
        ASSERT_TRUE(bot != nullptr);
        run_ticks(sim, c, 300);
        ASSERT_TRUE(looks > tiles.size());
        const BotController::SeatStats& st = c.stats(0);
        ASSERT_EQ(st.filtered, 4u);                                                               // the own ant's tile, the ally's, the two enemies'
        ASSERT_EQ(st.released, 2u);                                                               // the tile of the dead ant and the free tile
        ASSERT_EQ(bot->count(Bot::Fate::Filtered), 4u);
        ASSERT_EQ(bot->count(Bot::Fate::Sent), 2u);
        for (const auto& e : sink.log) ASSERT_TRUE(e.second.type == CommandType::GroupSpecial && e.second.tile_x >= 24);
        ASSERT_EQ(sink.log.size(), 2u);                                                           // nothing refused left

        {   // (b) an ant that dies on its clip (about 42 ticks) is still under the pointer: the order of the first look, released while it dies, is refused; the same order after the clip passes
            sim::SimulationEngine sim2;
            build_world(sim2, 7, 4);
            const uint32_t sender = ants_of(sim2, 0)[0];
            const uint32_t dying = sim2.spawn_unit(1, sim::AntType::Worker, tc(22, 20));
            sim2.get_unit(dying).hp = 1;
            const uint32_t striker = sim2.spawn_unit(0, sim::AntType::Worker, tc(21, 20));
            sim2.execute_melee_attack(striker, dying);                                            // (the blow kills it: it dies on its clip, which lists it for about 42 ticks)
            for (int i = 0; i < 2; ++i) sim2.tick();
            {   // (it is dying, and the world lists it)
                size_t n = 0;
                for (const sim::AntSnapshot& a : sim2.get_world_state().ants) n += a.tile_x == 22 && a.tile_y == 20 && a.hp == 0 ? 1u : 0u;
                ASSERT_EQ(n, 1u);
            }
            RecordingSink sink2(sim2);
            BotController c2(sim2, 3);
            c2.set_start_hold(0);
            bool first = false;
            bool second = false;
            ScriptBot* bot2 = seat_script(c2, sim2, spec_of(0, Level::Hard), sink2, [&](const BotView& v, Orders& o) {
                if (!first) {
                    first = true;
                    o.special(sender, tc(22, 20));
                } else if (v.tick() >= 90 && !second) {
                    second = true;
                    o.special(sender, tc(22, 20));
                }
            });
            ASSERT_TRUE(bot2 != nullptr);
            run_ticks(sim2, c2, 200);
            ASSERT_TRUE(first && second);
            ASSERT_EQ(bot2->count(Bot::Fate::Filtered), 1u);
            ASSERT_EQ(bot2->count(Bot::Fate::Sent), 1u);
            ASSERT_EQ(sink2.log.size(), 1u);
        }
    } TEST_END();

    TEST_CASE("AI2.23 Orders::chain: One Intent Per Click, Numbered By Chain And Step, With The Windows And The Pick-Up Marks As Given; An Empty Chain Puts In Nothing And Takes No Number; clear() Starts The Numbers Again") {
        Orders o;
        ASSERT_EQ(o.chains(), 0u);
        o.chain(7, {});
        ASSERT_TRUE(o.intents().empty() && o.chains() == 0);
        std::vector<ChainStep> a = steps_of({{20, 0, 0}, {21, 10, 11}, {22, 18, 19}});
        a[1].pickup = true;
        o.chain(7, a);
        o.move({9}, TileCoord{5, 5});
        o.chain(8, steps_of({{30, 0, 0}, {31, 12, 13}}), Priority::Normal);
        ASSERT_EQ(o.chains(), 2u);
        ASSERT_EQ(o.intents().size(), 6u);
        const std::vector<Intent>& in = o.intents();
        for (size_t i = 0; i < 3; ++i) {
            ASSERT_TRUE(in[i].chain == 1 && in[i].step == i && in[i].priority == Priority::Urgent);
            ASSERT_TRUE(in[i].command.type == CommandType::GroupMove && in[i].command.ants == std::vector<uint32_t>{7});
            ASSERT_TRUE(in[i].command.tile_x == 20 + static_cast<int>(i) && in[i].command.tile_y == 30);
        }
        ASSERT_TRUE(in[1].gap_lo == 10 && in[1].gap_hi == 11 && in[2].gap_lo == 18 && in[2].gap_hi == 19);
        ASSERT_TRUE(in[1].pickup && !in[0].pickup && !in[2].pickup);
        ASSERT_TRUE(in[3].chain == 0 && in[3].step == 0 && !in[3].pickup);                                       // the single click is none of the chains
        ASSERT_TRUE(in[4].chain == 2 && in[4].step == 0 && in[5].chain == 2 && in[5].step == 1 && in[5].gap_lo == 12 && in[4].priority == Priority::Normal);
        o.clear();
        ASSERT_TRUE(o.intents().empty() && o.chains() == 0);
        o.chain(7, steps_of({{20, 0, 0}, {21, 10, 11}}));
        ASSERT_EQ(o.intents()[0].chain, 1u);
    } TEST_END();

    TEST_CASE("AI2.24 A Timed Chain Leaves By The Sink's Clock: The First Click Like Any Order, Every Later One At The First Tick At Which The Sink Would Apply It Inside Its Window (10 To 11 Ticks After The Click Before, Then 18 To 19, Then 10 To 12), With A Sink That Applies At Once, After 1, 2 Or 4 Ticks, And On The Even Ticks Of The Arena, At Every Reaction Time; No Click Is Dropped, Each Costs A Token") {
        const std::vector<std::tuple<int32_t, uint32_t, uint32_t>> chain = {{20, 0, 0}, {21, 10, 11}, {22, 18, 19}, {23, 10, 12}};
        for (const auto& [lag, even] : std::vector<std::pair<uint32_t, bool>>{{0, false}, {1, false}, {2, true}, {3, true}, {4, true}, {5, false}}) {
            for (uint32_t seed = 1; seed <= 6; ++seed) {
                sim::SimulationEngine sim;
                build_world(sim, seed);
                TimedSink sink(sim, lag, even);
                BotController c(sim, seed);
                c.set_start_hold(0);
                const uint32_t ant = ants_of(sim, 0)[0];
                bool proposed = false;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                    if (proposed) return;
                    proposed = true;
                    o.chain(ant, steps_of(chain));
                });
                ASSERT_TRUE(bot != nullptr);
                run_ticks(sim, c, 120);
                ASSERT_TRUE(proposed);
                ASSERT_EQ(sink.log.size(), 4u);
                for (size_t i = 0; i < 4; ++i) {
                    ASSERT_TRUE(sink.log[i].command.type == CommandType::GroupMove && sink.log[i].command.issuer == 0);
                    ASSERT_TRUE(sink.log[i].command.ants == std::vector<uint32_t>{ant} && sink.log[i].command.tile_x == 20 + static_cast<int>(i));
                    if (i == 0) continue;
                    const uint64_t d = sink.log[i].applied - sink.log[i - 1].applied;                                        // the sink's own ticks of the two clicks
                    ASSERT_TRUE(d >= std::get<1>(chain[i]) && d <= std::get<2>(chain[i]));
                    ASSERT_TRUE(sink.log[i].sent > sink.log[i - 1].sent);
                }
                ASSERT_EQ(bot->count(Bot::Fate::Sent), 4u);
                ASSERT_EQ(bot->count(Bot::Fate::Expired) + bot->count(Bot::Fate::Pruned) + bot->count(Bot::Fate::Filtered), 0u);
                const auto& st = c.stats(0);
                ASSERT_TRUE(st.released == 4 && st.expired == 0 && st.rejected == 0);
                ASSERT_EQ(c.pending(0), 0u);
                if (!even) {                                                                                                 // a sink with a fixed lag: every click leaves as soon as its window opens
                    ASSERT_EQ(sink.log[1].sent - sink.log[0].sent, 10u);
                    ASSERT_EQ(sink.log[2].sent - sink.log[1].sent, 18u);
                    ASSERT_EQ(sink.log[3].sent - sink.log[2].sent, 10u);
                }
                if (even) for (const auto& e : sink.log) ASSERT_EQ(e.applied % 2, 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI2.25 A Click That Would Be Applied Late Is Not Sent: When The Sink's Delay Jumps After The First Click (From 0 To 30 Ticks), The Second Cannot Be Applied In Its Window, And It Is Dropped With The Rest Of The Chain (Fate::Expired, Told To The Bot), The First Having Left; Nothing Waits In The Queue") {
        sim::SimulationEngine sim;
        build_world(sim, 4);
        TimedSink sink(sim, 0, false);
        BotController c(sim, 9);
        c.set_start_hold(0);
        const uint32_t ant = ants_of(sim, 0)[0];
        bool proposed = false;
        ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
            if (proposed) return;
            proposed = true;
            o.chain(ant, steps_of({{20, 0, 0}, {21, 10, 11}, {22, 10, 11}}));
        });
        ASSERT_TRUE(bot != nullptr);
        for (int i = 0; i < 100 && sink.log.empty(); ++i) run_ticks(sim, c, 1);
        ASSERT_EQ(sink.log.size(), 1u);
        sink.set_lag(30);
        run_ticks(sim, c, 80);
        ASSERT_EQ(sink.log.size(), 1u);
        ASSERT_EQ(bot->count(Bot::Fate::Sent), 1u);
        ASSERT_EQ(bot->count(Bot::Fate::Expired), 2u);
        ASSERT_EQ(c.stats(0).expired, 2u);
        ASSERT_EQ(c.pending(0), 0u);
        size_t last = 0;
        for (const auto& seen : bot->fates) {
            if (seen.fate == Bot::Fate::Expired) {
                ASSERT_TRUE(seen.command.tile_x == 21 + static_cast<int>(last) && seen.command.ants == std::vector<uint32_t>{ant});
                ++last;
            }
        }
    } TEST_END();

    TEST_CASE("AI2.26 A Chain With A Click That The HUD Would Refuse Leaves No Click At All: A Step Off The Map, A Step On A Power-Up Without The Pick-Up Mark, Or A Pick-Up Mark On A Tile With Nothing To Take Is Filtered With The Rest (Fate::Filtered For Each, At The Look), The First Step Included; The Same Chain Without It Leaves Whole; A Chain Of Another Team's Ant Is Pruned (Its First Step) And Expired (The Rest)") {
        sim::SimulationEngine sim;
        build_world(sim, 5);
        const uint32_t ant = ants_of(sim, 0)[0];
        const uint32_t lone = ants_of(sim, 1)[0];
        sim.grid_mut().place_powerup(25, 30, 1);                                                                   // (a power-up that variant 2 names without the mark)
        for (int variant = 0; variant < 5; ++variant) {
            TimedSink sink(sim, 3, true);
            BotController c(sim, 9);
            c.set_start_hold(0);
            bool proposed = false;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (proposed) return;
                proposed = true;
                std::vector<ChainStep> steps = steps_of({{20, 0, 0}, {21, 10, 11}, {22, 10, 11}});
                if (variant == 1) steps[2].tile = TileCoord{-4, 30};                                               // off the map
                if (variant == 2) steps[2].tile = TileCoord{25, 30};                                               // on a power-up, unmarked
                if (variant == 3) steps[1].pickup = true;                                                          // (marked, and nothing to take there)
                if (variant == 4) o.chain(lone, steps);                                                            // another team's ant
                else o.chain(ant, steps);
            });
            ASSERT_TRUE(bot != nullptr);
            run_ticks(sim, c, 100);
            ASSERT_TRUE(proposed);
            if (variant == 0) {
                ASSERT_EQ(sink.log.size(), 3u);
                ASSERT_EQ(bot->count(Bot::Fate::Filtered), 0u);
            } else if (variant == 4) {
                ASSERT_TRUE(sink.log.empty());
                ASSERT_TRUE(bot->count(Bot::Fate::Pruned) == 1 && bot->count(Bot::Fate::Expired) == 2 && bot->count(Bot::Fate::Filtered) == 0);
                ASSERT_EQ(c.pending(0), 0u);
            } else {
                ASSERT_TRUE(sink.log.empty());
                ASSERT_EQ(bot->count(Bot::Fate::Filtered), 3u);
                ASSERT_EQ(c.stats(0).filtered, 3u);
                ASSERT_EQ(c.pending(0), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI2.27 A Newer Order For The Ant Ends Its Chain, And So Does Its Death: The Clicks After The One Sent Are Dropped (Expired After A Newer Order, Pruned After A Death), Nothing Else Of The Chain Leaves, And The Newer Order Leaves Once") {
        for (const bool kill : {false, true}) {
            sim::SimulationEngine sim;
            build_world(sim, 8);
            TimedSink sink(sim, 0, false);
            BotController c(sim, 5);
            c.set_start_hold(0);
            const uint32_t ant = ants_of(sim, 0)[0];
            int looks = 0;
            bool killed = false;
            bool moved = false;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                ++looks;
                if (looks == 1) {
                    o.chain(ant, steps_of({{20, 0, 0}, {21, 30, 31}, {22, 30, 31}}));
                } else if (!kill && !moved && !sink.log.empty()) {                                                     // (the look after the first click left: once)
                    moved = true;
                    o.move({ant}, TileCoord{40, 40}, Priority::Urgent);
                }
            });
            ASSERT_TRUE(bot != nullptr);
            for (int i = 0; i < 100 && sink.log.empty(); ++i) run_ticks(sim, c, 1);
            ASSERT_EQ(sink.log.size(), 1u);
            if (kill) {
                sim.kill_unit(ant);
                killed = true;
            }
            run_ticks(sim, c, 100);
            if (kill) {
                ASSERT_TRUE(killed);
                ASSERT_EQ(sink.log.size(), 1u);
                ASSERT_EQ(bot->count(Bot::Fate::Pruned), 2u);
                ASSERT_EQ(bot->count(Bot::Fate::Expired), 0u);
            } else {
                ASSERT_EQ(sink.log.size(), 2u);
                ASSERT_TRUE(sink.log[1].command.tile_x == 40 && sink.log[1].command.tile_y == 40);
                ASSERT_EQ(bot->count(Bot::Fate::Expired), 2u);
                ASSERT_EQ(bot->count(Bot::Fate::Sent), 2u);
            }
            ASSERT_EQ(c.pending(0), 0u);
        }
    } TEST_END();

    TEST_CASE("AI2.28 A Chain Is Paid For: A Bot That Floods With Five Clicks At Every Look And Sends A Chain Of Four Every Fifth Look Gets No More Out In A Minute Than The Bucket And The Rate Allow (A Few Clicks Over At Most: The Bucket Goes Into Debt For The Clicks After The First, And The Orders After Them Wait Until It Is Paid), At Every Level, And The Chains Are Whole") {
        for (const Level level : {Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            build_world(sim, 3);
            TimedSink sink(sim, 3, true);
            BotController c(sim, 7);
            c.set_start_hold(0);
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            size_t next = 0;
            size_t looks = 0;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, level), sink, [&](const BotView& v, Orders& o) {
                ++looks;
                if (looks % 5 == 0) {                                                                                // (a chain lasts about 30 ticks: four ants take turns, none is named again while its chain runs)
                    if (v.tick() < 1100) o.chain(mine[(looks / 5) % 4], steps_of({{20, 0, 0}, {21, 10, 11}, {22, 10, 11}, {23, 10, 11}}));
                    return;
                }
                for (int i = 0; i < 5; ++i) o.move({mine[4 + next++ % (mine.size() - 4)]}, TileCoord{30, 30});
            });
            ASSERT_TRUE(bot != nullptr);
            const Profile p = profile_for(level);
            run_ticks(sim, c, 1200);
            const auto& st = c.stats(0);
            const double allowed = p.burst + p.rate_milli_cps / 1000.0 * 60.0;
            ASSERT_TRUE(st.released <= allowed + 4 && st.released >= allowed * 0.8);
            size_t chain_clicks = 0;
            for (const auto& e : sink.log) {
                const bool of_chain = e.command.tile_y == 30 && e.command.tile_x >= 20 && e.command.tile_x <= 23 && e.command.ants.size() == 1 &&
                                      std::find(mine.begin(), mine.begin() + 4, e.command.ants[0]) != mine.begin() + 4;
                chain_clicks += of_chain ? 1u : 0u;
            }
            ASSERT_TRUE(chain_clicks >= 12);                                                                          // (Hard: a chain every 20 ticks; Medium: every 100)
            ASSERT_EQ(chain_clicks % 4, 0u);                                                                           // a chain that was begun was finished
            ASSERT_EQ(st.rejected, 0u);
        }
    } TEST_END();

    TEST_CASE("AI2.29 The First Click Of A Chain Goes Before The Other Orders That Are Due On Its Tick, Whatever Their Age: Two Urgent Clicks Of Other Ants Proposed Before The Chain At The Same Look Leave After Its First Click (And In The Order They Were Proposed), At Every Reaction Time And With Every Lag; The Chain Is Whole")
    {
        for (const uint32_t lag : {0u, 3u}) {
            for (uint32_t seed = 1; seed <= 5; ++seed) {
                sim::SimulationEngine sim;
                build_world(sim, seed);
                TimedSink sink(sim, lag, lag > 0);
                BotController c(sim, seed);
                c.set_start_hold(0);
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                bool proposed = false;
                ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                    if (proposed) return;
                    proposed = true;
                    o.move({mine[1]}, TileCoord{40, 40}, Priority::Urgent);
                    o.move({mine[2]}, TileCoord{41, 40}, Priority::Urgent);
                    o.chain(mine[0], steps_of({{20, 0, 0}, {21, 10, 11}}));
                });
                ASSERT_TRUE(bot != nullptr);
                run_ticks(sim, c, 120);
                ASSERT_EQ(sink.log.size(), 4u);
                ASSERT_TRUE(sink.log[0].command.ants == std::vector<uint32_t>{mine[0]} && sink.log[0].command.tile_x == 20);
                ASSERT_TRUE(sink.log[1].command.ants == std::vector<uint32_t>{mine[1]} && sink.log[2].command.ants == std::vector<uint32_t>{mine[2]});
                ASSERT_TRUE(sink.log[0].sent == sink.log[1].sent && sink.log[1].sent == sink.log[2].sent);                 // (the same tick: the order inside it is the point)
                ASSERT_TRUE(sink.log[3].command.ants == std::vector<uint32_t>{mine[0]} && sink.log[3].command.tile_x == 21);
                ASSERT_EQ(bot->count(Bot::Fate::Expired) + bot->count(Bot::Fate::Superseded), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI2.30 A Sink That Does Not Accept A Click Ends Its Chain: When The First Click Is Answered Ignored (A Room That Is Paused) The Rest Is Dropped (Fate::Expired); When A Later One Is, The Ones After It Are; Nothing Is Left In The Queue Either Way")
    {
        for (const bool first : {true, false}) {
            sim::SimulationEngine sim;
            build_world(sim, 6);
            TimedSink sink(sim, 0, false);
            sink.refuse = first;
            BotController c(sim, 7);
            c.set_start_hold(0);
            const uint32_t ant = ants_of(sim, 0)[0];
            bool proposed = false;
            ScriptBot* bot = seat_script(c, sim, spec_of(0, Level::Hard), sink, [&](const BotView&, Orders& o) {
                if (proposed) return;
                proposed = true;
                o.chain(ant, steps_of({{20, 0, 0}, {21, 10, 11}, {22, 10, 11}, {23, 10, 11}}));
            });
            ASSERT_TRUE(bot != nullptr);
            if (!first) {
                for (int i = 0; i < 100 && sink.log.empty(); ++i) run_ticks(sim, c, 1);
                ASSERT_EQ(sink.log.size(), 1u);
                sink.refuse = true;                                                                          // (the second click is submitted and refused)
            }
            run_ticks(sim, c, 120);
            ASSERT_EQ(sink.log.size(), first ? 1u : 2u);
            ASSERT_EQ(bot->count(Bot::Fate::Expired), first ? 3u : 2u);
            ASSERT_EQ(c.pending(0), 0u);
        }
    } TEST_END();

}

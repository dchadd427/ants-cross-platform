// Tests of the match runner that the bot arena is made of (AI4.1 .. AI4.4): one match with the real engine and the controller, bit-reproducible for a fixed seed, and the
// proof that a bot match is nothing but its commands: they replay into a FRESH engine without any bot to the same state hash at every 20th tick and at the end.
// (tools/bot_arena.cpp adds the command line, the threads, the report; its own checks run as `bot_arena --selftest`, suite 2.21.)
#include "ai_test.hpp"

#include <algorithm>

#include "ants_ai/arena.hpp"
#include "ants_ai/rng.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

// A scripted bot (the registry's worker is the economy of B3, the standard bot of B4 is not there yet): at every look it sends one of its ants to a tile near it, so that a match holds
// commands for the replay to check whatever the registry's bots do
class WalkerBot final : public Bot {
public:
    const char* kind() const noexcept override { return "walker"; }
    void start(const BotContext& context) override { rng_ = BotRng(context.rng_seed); }
    void think(const BotView& view, Orders& orders) override {
        if (view.mine().empty()) return;
        const AntView& a = view.mine()[rng_.below(static_cast<uint32_t>(view.mine().size()))];
        const int32_t w = static_cast<int32_t>(view.grid().width());
        const int32_t h = static_cast<int32_t>(view.grid().height());
        const int32_t x = std::clamp(a.tile.x + static_cast<int32_t>(rng_.below(15)) - 7, 0, w - 1);
        const int32_t y = std::clamp(a.tile.y + static_cast<int32_t>(rng_.below(15)) - 7, 0, h - 1);
        orders.move({a.id}, TileCoord{x, y});
    }

private:
    BotRng rng_;
};

// A bot that sends six orders (six ants, six tiles: a team of TREASURE has six) at its first look and nothing after it: the opening bucket decides how they leave (a full one: all on one
// tick; one token: one by one)
class BurstBot final : public Bot {
public:
    const char* kind() const noexcept override { return "burst"; }
    void start(const BotContext&) override {}
    void think(const BotView& view, Orders& orders) override {
        if (done_ || view.mine().size() < 6) return;
        done_ = true;
        const int32_t w = static_cast<int32_t>(view.grid().width());
        const int32_t h = static_cast<int32_t>(view.grid().height());
        for (size_t i = 0; i < 6; ++i) {
            const AntView& a = view.mine()[i];
            orders.move({a.id}, TileCoord{std::clamp(a.tile.x + 3 + static_cast<int32_t>(i), 0, w - 1), std::clamp(a.tile.y + 5, 0, h - 1)});
        }
    }

private:
    bool done_{false};
};

BotSpec seat_spec(uint8_t seat, const char* kind, Level level) {
    BotSpec s;
    s.seat = seat;
    s.kind = kind;
    s.level = level;
    return s;
}

ArenaSpec walkers(const std::string& map, uint32_t seed, uint64_t ticks, uint32_t latency, uint8_t roster = 0x0F) {
    ArenaSpec s;
    s.level = &level_of(map);
    s.seed = seed;
    s.max_ticks = ticks;
    s.latency_ticks = latency;
    s.record = true;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (((roster >> seat) & 1u) != 0) s.bots.push_back(seat_spec(seat, "worker", seat % 2 == 0 ? Level::Hard : Level::Medium));
    }
    s.factory = [](const BotSpec&) { return std::make_unique<WalkerBot>(); };
    return s;
}

bool same_log(const ArenaResult& a, const ArenaResult& b) {
    if (a.log.size() != b.log.size()) return false;
    for (size_t i = 0; i < a.log.size(); ++i) {
        if (a.log[i].step != b.log[i].step || a.log[i].tick != b.log[i].tick || !(a.log[i].command == b.log[i].command)) return false;
    }
    return true;
}

}  // namespace

void run_arena_tests() {
    TEST_CASE("AI4.1 The Arena Is Bit-Reproducible: The Same Map, Seed And Seats Give The Same Match (Hash At Every 20th Tick, Every Applied Command, Every Counter), Another Seed Another Match; The Sink Latency Is Part Of The Arguments") {
        for (const char* map : {"TINY", "MEDIUM", "TREASURE", "ISLANDS"}) {
            const ArenaSpec s = walkers(map, 11, 1200, 3);
            const ArenaResult a = play_match(s);
            const ArenaResult b = play_match(s);
            ASSERT_TRUE(a.error.empty() && b.error.empty());
            ASSERT_EQ(a.ticks, 1200u);
            ASSERT_EQ(a.checkpoints.size(), 60u);
            ASSERT_EQ(a.hash, b.hash);
            ASSERT_TRUE(a.checkpoints == b.checkpoints);
            ASSERT_TRUE(same_log(a, b));
            ASSERT_TRUE(a.log.size() > 100);                                                    // the walkers did act
            ASSERT_EQ(a.seats.size(), 4u);
            for (size_t i = 0; i < 4; ++i) {
                ASSERT_EQ(a.seats[i].spec.seat, static_cast<uint8_t>(i));
                ASSERT_EQ(a.seats[i].stats.released, b.seats[i].stats.released);
                ASSERT_EQ(a.seats[i].stats.decisions, b.seats[i].stats.decisions);
                ASSERT_EQ(a.seats[i].stats.rejected, 0u);                                       // the engine never refused a released command
                ASSERT_EQ(a.seats[i].stats.filtered, 0u);
                ASSERT_EQ(a.seats[i].runs, "walker");
            }
            ArenaSpec other = s;
            other.seed = 12;
            ASSERT_TRUE(play_match(other).hash != a.hash);                                      // another seed, another match
            ArenaSpec direct = s;
            direct.latency_ticks = 0;
            const ArenaResult d = play_match(direct);
            ASSERT_TRUE(d.hash != a.hash);                                                      // the latency is an argument: commands that reach the engine at other ticks make another match
            ASSERT_TRUE(d.hash == play_match(direct).hash);
            // with latency 0 every released command is applied at once, and the log has them all
            for (size_t i = 0; i < 4; ++i) {
                size_t applied = 0;
                for (const RecordedCommand& r : d.log) applied += r.command.issuer == i ? 1u : 0u;
                ASSERT_EQ(applied, d.seats[i].stats.released);
            }
            // with latency 3 a command waits for the next turn: all but the last few are applied, none twice, each carries its seat as the issuer
            for (size_t i = 0; i < 4; ++i) {
                size_t applied = 0;
                for (const RecordedCommand& r : a.log) applied += r.command.issuer == i ? 1u : 0u;
                ASSERT_TRUE(applied <= a.seats[i].stats.released && applied + 4 >= a.seats[i].stats.released);
            }
        }
    } TEST_END();

    TEST_CASE("AI4.2 A Bot Match Is Nothing But Its Commands: The Applied Commands Replay Into A Fresh Engine Without Any Bot To The Same State Hash At Every 20th Tick And At The End (Four Maps, Four Bots, Latency 3 And 0, A Full Match, A Replay That Is Tampered With Fails)") {
        for (const char* map : {"TINY", "MEDIUM", "TREASURE", "ISLANDS"}) {
            for (const uint32_t latency : {3u, 0u}) {
                const ArenaSpec s = walkers(map, 5, 1600, latency);
                const ArenaResult played = play_match(s);
                ASSERT_TRUE(played.error.empty() && !played.log.empty());
                const ReplayResult r = replay_commands(s, played);
                ASSERT_TRUE(r.ok);
                ASSERT_EQ(r.first_bad_tick, 0u);
                ASSERT_EQ(r.hash, played.hash);
            }
        }
        // a four-bot match at the default latency on TINY from the first tick to the clock: the call that ends a match does not advance the tick count, and the replay makes the same calls
        ArenaSpec full = walkers("TINY", 2, 0, 3);
        const ArenaResult f = play_match(full);
        ASSERT_TRUE(f.error.empty() && f.match_over && f.initial_ticks == 7200 && f.ticks >= 7200 && f.steps == f.ticks + 1);
        ASSERT_TRUE(f.log.size() > 1000);
        const ReplayResult fr = replay_commands(full, f);
        ASSERT_TRUE(fr.ok && fr.hash == f.hash);
        // two bots only: seats 1 and 3 have no hill, no ants, and the replay knows it
        ArenaSpec two = walkers("SMALL", 3, 2400, 3, 0x05);
        const ArenaResult tp = play_match(two);
        ASSERT_TRUE(tp.error.empty() && tp.seats.size() == 2 && tp.seats[0].spec.seat == 0 && tp.seats[1].spec.seat == 2);
        ASSERT_TRUE(replay_commands(two, tp).ok);
        // the check can fail: a command missing, a command changed, a command at another step, a command added
        const ArenaSpec s = walkers("TINY", 7, 1500, 3);
        const ArenaResult played = play_match(s);
        ASSERT_TRUE(replay_commands(s, played).ok);
        ArenaResult dropped = played;
        dropped.log.erase(dropped.log.begin() + static_cast<std::ptrdiff_t>(dropped.log.size() / 3));
        ASSERT_FALSE(replay_commands(s, dropped).ok);
        ArenaResult moved = played;
        moved.log[moved.log.size() / 2].command.tile_y = static_cast<int16_t>(moved.log[moved.log.size() / 2].command.tile_y + 4);
        ASSERT_FALSE(replay_commands(s, moved).ok);
        ArenaResult late = played;
        late.log[late.log.size() / 2].step += 11;
        ASSERT_FALSE(replay_commands(s, late).ok);
        ArenaResult extra = played;
        RecordedCommand more = played.log.front();                                                // the issuer and the ant of one command belong together (the engine ignores an order for another team's ant): the added command always reaches the engine (AI4.12 runs it over twelve seeds)
        more.command.tile_x = 3;
        more.command.tile_y = 3;
        more.step = 40;
        more.tick = 40;
        extra.log.insert(extra.log.begin() + 5, more);
        std::stable_sort(extra.log.begin(), extra.log.end(), [](const RecordedCommand& a, const RecordedCommand& b) { return a.step < b.step; });
        ASSERT_FALSE(replay_commands(s, extra).ok);
        ArenaResult wrong_hash = played;
        wrong_hash.hash ^= 1;
        ASSERT_FALSE(replay_commands(s, wrong_hash).ok);
        ArenaResult wrong_checkpoint = played;
        wrong_checkpoint.checkpoints[wrong_checkpoint.checkpoints.size() / 2] ^= 1;
        const ReplayResult bad = replay_commands(s, wrong_checkpoint);
        ASSERT_TRUE(!bad.ok && bad.first_bad_tick == (wrong_checkpoint.checkpoints.size() / 2 + 1) * kArenaHashPeriod);     // and it names the tick
        // a match that was refused has nothing to replay
        ArenaSpec none;
        const ArenaResult refused = play_match(none);
        ASSERT_FALSE(refused.error.empty());
        ASSERT_FALSE(replay_commands(none, refused).ok);
    } TEST_END();

    TEST_CASE("AI4.3 The Length Of Every Shipped Match (TINY 7200 Ticks, SMALL 9600, MEDIUM 12000, GAUNTLET 12000, TREASURE 14400, ISLANDS 14400) And The Limits: max_ticks Stops A Match, A Full Match Ends By The Clock (The Queues Of News And Audio Are Emptied Every Tick: AI4.10)") {
        struct Row { const char* map; uint64_t ticks; };
        for (const Row& r : {Row{"TINY", 7200}, Row{"SMALL", 9600}, Row{"MEDIUM", 12000}, Row{"GAUNTLET", 12000}, Row{"TREASURE", 14400}, Row{"ISLANDS", 14400}}) {
            ArenaSpec s;
            s.level = &level_of(r.map);
            s.seed = 1;
            s.max_ticks = 40;
            for (uint8_t seat = 0; seat < 4; ++seat) s.bots.push_back(seat_spec(seat, "idle", Level::Medium));
            const ArenaResult a = play_match(s);
            ASSERT_TRUE(a.error.empty() && a.initial_ticks == r.ticks && a.ticks == 40 && !a.match_over && a.steps == 40);
            ASSERT_EQ(a.checkpoints.size(), 2u);
        }
        // a full match of idle bots: nobody scores, nobody loses an ant, the clock ends it
        ArenaSpec idle;
        idle.level = &level_of("TINY");
        idle.seed = 4;
        for (uint8_t seat = 0; seat < 4; ++seat) idle.bots.push_back(seat_spec(seat, "idle", Level::Hard));
        const ArenaResult a = play_match(idle);
        ASSERT_TRUE(a.error.empty() && a.match_over && a.ticks >= 7200);
        for (const ArenaSeatResult& s : a.seats) {
            ASSERT_TRUE(s.score == 0 && s.shown_score == 0 && s.banked == 0 && s.raided == 0 && s.kills == 0 && s.losses == 0 && s.hatched == 0);
            ASSERT_TRUE(s.ants == 3 && s.eggs == 3);                                            // TINY: three ants and three eggs per hill
            ASSERT_EQ(s.stats.released, 0u);
            ASSERT_EQ(s.milli_commands_per_second(a.ticks), 0u);
            ASSERT_EQ(s.runs, "idle");
        }
        ASSERT_TRUE(a.log.empty());
        ASSERT_EQ(a.checkpoints.size(), a.ticks / kArenaHashPeriod);
        // the kinds that play: "worker" is the worker bot (B3), "standard" is an alias of it until the standard bot (B4) exists, and the result says which one actually played
        ArenaSpec placeholders;
        placeholders.level = &level_of("TINY");
        placeholders.seed = 4;
        placeholders.max_ticks = 300;
        placeholders.bots = {seat_spec(0, "worker", Level::Hard), seat_spec(1, "standard", Level::Easy), seat_spec(2, "idle", Level::Medium)};
        const ArenaResult p = play_match(placeholders);
        ASSERT_TRUE(p.error.empty() && p.seats.size() == 3);
        ASSERT_TRUE(p.seats[0].runs == "worker" && p.seats[1].runs == "worker" && p.seats[2].runs == "idle");
        ASSERT_TRUE(p.seats[0].spec.kind == "worker" && p.seats[1].spec.kind == "standard");
        // The arena's default opening is the product's (kStartHoldTicks = 1: the match clock waits for the "Get ready to play!" dialog in the game, so the arena's tick 0 is the game's): the first
        // look of seat s is on tick 1 + s, Hard looks on 1, 5, ... 297 (75 looks in 300 ticks), Easy (seat 1) on 2, 102 and 202. With the hold off (the opening of v0.1.0) the looks are the same: the
        // two differ in the bucket only. A longer hold (v0.1.1 had 100 ticks) moves the first look to tick 100 + s: Hard looks on 100, 104, ... 300 (51 looks), Easy on 101 and 201.
        ASSERT_EQ(p.seats[0].stats.decisions, 75u);                                              // Hard looks every 4 ticks (the first look is on tick 1)
        ASSERT_EQ(p.seats[1].stats.decisions, 3u);                                               // Easy every 100
        ArenaSpec no_hold = placeholders;
        no_hold.start_hold = 0;
        const ArenaResult n = play_match(no_hold);
        ASSERT_TRUE(n.error.empty() && n.seats.size() == 3);
        ASSERT_EQ(n.seats[0].stats.decisions, 75u);
        ASSERT_EQ(n.seats[1].stats.decisions, 3u);
        ArenaSpec long_hold = placeholders;
        long_hold.start_hold = 100;
        const ArenaResult l = play_match(long_hold);
        ASSERT_TRUE(l.error.empty() && l.seats.size() == 3);
        ASSERT_EQ(l.seats[0].stats.decisions, 51u);
        ASSERT_EQ(l.seats[1].stats.decisions, 2u);
        // commands per second: the released commands of the whole match
        ArenaSpec w = walkers("TINY", 6, 1200, 3, 0x03);
        const ArenaResult wr = play_match(w);
        ASSERT_TRUE(wr.seats[0].stats.released > 50 && wr.seats[0].milli_commands_per_second(wr.ticks) == static_cast<uint32_t>(static_cast<uint64_t>(wr.seats[0].stats.released) * 20000u / wr.ticks));
        ASSERT_TRUE(wr.seats[0].stats.released <= 10u + 3u * 60u);                                  // Hard: at most 3 commands per second (60 s here) plus the bucket of 10
    } TEST_END();

    TEST_CASE("AI4.4 The Arena Refuses What Cannot Be Played: No Map, No Bot, Two Bots On A Seat, A Kind That Does Not Exist, A Factory That Gives Nothing; Nothing Is Played And The Reason Is Told") {
        ArenaSpec s;
        ASSERT_FALSE(play_match(s).error.empty());                                              // no map
        s.level = &level_of("TINY");
        ASSERT_FALSE(play_match(s).error.empty());                                              // no bot
        s.bots = {seat_spec(0, "idle", Level::Medium), seat_spec(0, "idle", Level::Hard)};
        ASSERT_FALSE(play_match(s).error.empty());                                              // two on a seat
        s.bots = {seat_spec(1, "nonsense", Level::Medium)};
        ASSERT_FALSE(play_match(s).error.empty());                                              // unknown kind
        s.bots = {seat_spec(1, "idle", Level::Medium), seat_spec(2, "idle", Level::Medium)};
        s.factory = [](const BotSpec&) { return std::unique_ptr<Bot>(); };
        const ArenaResult none = play_match(s);
        ASSERT_FALSE(none.error.empty());                                                       // the factory gave no bot
        ASSERT_EQ(none.ticks, 0u);
        ASSERT_TRUE(none.seats.empty() && none.log.empty());
        s.factory = nullptr;
        s.max_ticks = 10;
        const ArenaResult fine = play_match(s);
        ASSERT_TRUE(fine.error.empty() && fine.ticks == 10);
        // seats 1 and 2 only: seats 0 and 3 have no hill and no ants
        ASSERT_EQ(fine.seats.size(), 2u);
    } TEST_END();
    TEST_CASE("AI4.14 The Arena Opens A Match Like The Product Does: ArenaSpec's Default Start Hold And A Bare BotController's Are The Product's (kStartHoldTicks = 1: The First Look On Tick 1 + Seat, A Bucket Of ONE Token); A Hard Bot's Six Orders Of One Look Leave Within A Few Ticks One By One (At Most Three On The First Tick), With The Opening Of v0.1.0 (Hold 0, A Full Bucket) All Six On One Tick, With A Long Hold Nothing Before Tick 100; The Default Is The Number Spelled Out") {
        ASSERT_EQ(kStartHoldTicks, 1u);
        ASSERT_EQ(ArenaSpec{}.start_hold, kStartHoldTicks);                                      // the arena's default is the product's ...
        {
            sim::SimulationEngine sim;
            sim.init(level_of("TREASURE"), 1, 0x03);
            BotController c(sim, 1);
            ASSERT_EQ(c.start_hold(), kStartHoldTicks);                                          // ... and so is the controller's, which every product path uses (the application, the LAN host, the server's rooms)
        }
        const auto play = [&](bool set, uint32_t hold) {
            ArenaSpec s;
            s.level = &level_of("TREASURE");
            s.seed = 5;
            s.max_ticks = 400;
            s.latency_ticks = 0;                                                                  // (a command is applied the tick it leaves: the log says when the bucket let it go)
            s.record = true;
            s.bots = {seat_spec(0, "worker", Level::Hard), seat_spec(1, "idle", Level::Hard)};
            s.factory = [](const BotSpec& b) { return b.seat == 0 ? std::unique_ptr<Bot>(std::make_unique<BurstBot>()) : make_bot(b); };
            if (set) s.start_hold = hold;
            return play_match(s);
        };
        const ArenaResult by_default = play(false, 0);
        const ArenaResult spelled = play(true, kStartHoldTicks);
        const ArenaResult v010 = play(true, 0);
        const ArenaResult long_hold = play(true, 100);
        for (const ArenaResult* r : {&by_default, &spelled, &v010, &long_hold}) ASSERT_TRUE(r->error.empty() && r->log.size() == 6);
        ASSERT_TRUE(by_default.hash == spelled.hash && same_log(by_default, spelled));        // the default is the product's number, spelled out
        const auto at_first_tick = [](const ArenaResult& r) {
            size_t n = 0;
            for (const RecordedCommand& c : r.log) n += c.tick == r.log.front().tick ? 1u : 0u;
            return n;
        };
        ASSERT_TRUE(at_first_tick(by_default) >= 1 && at_first_tick(by_default) <= 3);           // one token and a refill from the first tick: the order that waited the reaction time may find a second token, never six
        ASSERT_TRUE(by_default.log.back().tick > by_default.log.front().tick + 20);              // the rest follow one token at a time (a Hard bot: one in about 7 ticks)
        ASSERT_TRUE(by_default.log.front().tick >= 1 + 6 && by_default.log.front().tick <= 1 + 10 + 1);   // the first look is on tick 1 (seat 0), its reaction time 6 to 10 ticks
        ASSERT_EQ(at_first_tick(v010), 6u);                                                     // the opening of v0.1.0: a full bucket, a burst of six on one tick
        ASSERT_TRUE(by_default.hash != v010.hash);
        ASSERT_TRUE(long_hold.log.front().tick >= 100 && long_hold.hash != by_default.hash);     // a long hold holds back: nothing before tick 100
    } TEST_END();
}

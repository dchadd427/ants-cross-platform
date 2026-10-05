// AI22: the can't-go counter of the arena (docs/BOTS.md, "The can't-go loop"): what CantGoTally counts of the "Can't go there." and "Can't do that..." reactions and of the orders that
// were followed by one, in hand-made worlds with the real engine, and that play_match reports it. The counter changes nothing in a match (AI22.8); bot_arena --selftest checks that it
// reaches the report.
//
//   AI22.1  an order into a pocket that no walk reaches is one refused order, a group order is one however many ants it names, and what is no walk is no order
//   AI22.2  a refusal that comes later than the window (a wall of fire closes the target while the ant walks) is a reaction but not a refused order
//   AI22.3  the window: an order of exactly kRefusedWindow ticks before the reaction counts, one tick more does not; of two orders for an ant the later one counts
//   AI22.4  a can't-go loop is one beginning and many reactions (the ring of fire round a hill's gate with an ant on its queue row)
//   AI22.5  an ability that the engine refuses at once ("Can't do that...") is counted as well
//   AI22.6  a reaction of an ant that no counted order names is not a refused order
//   AI22.7  the seats are counted apart
//   AI22.8  scanning changes nothing in the engine (the state hash after 400 ticks is the same with and without the tally)
//   AI22.9  play_match reports it: three scripted bots' refused orders, equal to the commands of the log, with either sink
#include <algorithm>
#include <array>
#include <cstdint>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// The loop of the arena in little: an order is given to the engine and told to the tally; every tick is followed by the count of what the engine posted
struct Watch {
    sim::SimulationEngine& sim;
    CantGoTally tally;
    explicit Watch(sim::SimulationEngine& s) : sim(s) {}
    void order(const Command& c) {
        sim.apply_command(c);
        tally.command(c, sim.current_tick());
    }
    void tick() {
        sim.tick();
        tally.scan(sim, sim.poll_news_events());
        sim.clear_audio_events();
    }
    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) tick();
    }
};

constexpr TileCoord kPocket{40, 30};

// A tile that no walk reaches: the eight tiles round it are rocks
void shut_in_by_rocks(sim::SimulationEngine& sim, TileCoord t) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0) sim.set_terrain(t.x + dx, t.y + dy, sim::TERRAIN_OBSTACLE);
        }
    }
}

// The field of the walks: four hills, grass, and a pocket at kPocket
void pocket_field(sim::SimulationEngine& sim) {
    empty_field(sim, 1);
    shut_in_by_rocks(sim, kPocket);
}

// The ticks between an order into the pocket and the tick at which the tally sees the ant in the can't-go state: the engine's answer to a walk that finds no path
uint64_t ticks_to_refusal() {
    sim::SimulationEngine sim;
    pocket_field(sim);
    const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
    Watch watch(sim);
    watch.run(5);
    watch.order(command_of(CommandType::GroupMove, 0, {a}, kPocket.x, kPocket.y));
    const uint64_t t0 = sim.current_tick();
    for (int i = 0; i < 60; ++i) {
        watch.tick();
        if (watch.tally.seat(0).began != 0) return sim.current_tick() - t0;
    }
    return 0;
}

// The world of the loop: team 1's hill with the ring of fire round its gate (the walls that the sabotage lights) and two workers of team 1 standing on the queue row, shut in
struct RingWorld {
    sim::SimulationEngine sim;
    uint32_t victim{0};
    uint32_t victim2{0};
    void build() {
        empty_field(sim, 62);
        const MapInfo m(sim);
        const HillInfo& h1 = m.hill(1);
        victim = sim.spawn_unit(1, sim::AntType::Worker, h1.queue);
        victim2 = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{h1.queue.x + 1, h1.queue.y});
        for (const TileCoord& t : SabotageTask::ring_of(h1)) sim.set_fire_at(t, 3600);
    }
};

// A bot that sends its first three ants, at its first look, to the pile that its team cannot reach on foot (SMALL: the one in the lake): a click that the engine answers "Can't go there." to
class PokeBot final : public Bot {
public:
    const char* kind() const noexcept override { return "poke"; }
    void start(const BotContext& context) override {
        seat_ = context.seat;
        map_ = context.map;
    }
    void think(const BotView& view, Orders& orders) override {
        if (done_ || map_ == nullptr || view.mine().size() < 3) return;
        done_ = true;
        for (const PileInfo& p : map_->piles()) {
            if (p.cells.empty() || p.approach[seat_].reachable()) continue;
            for (size_t i = 0; i < 3; ++i) orders.move({view.mine()[i].id}, p.cells[0]);
            return;
        }
    }

private:
    uint8_t seat_{0};
    const MapInfo* map_{nullptr};
    bool done_{false};
};

}  // namespace

void run_cantgo_tests() {
    TEST_CASE("AI22.1 An Order Into A Pocket That No Walk Reaches Is One Refused Order (The Reaction Of The Ant, Its First One); A Walk That Is Carried Out Is Not Refused; A Stop And An Empty List Are No Order At All, And Neither Is It When The Ids Name No Ant")
    {
        sim::SimulationEngine sim;
        pocket_field(sim);
        const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        const uint32_t b = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
        const uint32_t c = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 20});
        Watch watch(sim);
        const CantGoTally::Seat& s = watch.tally.seat(0);
        watch.run(5);
        watch.order(command_of(CommandType::GroupMove, 0, {a}, kPocket.x, kPocket.y));
        watch.run(40);
        ASSERT_EQ(s.orders, 1u);
        ASSERT_EQ(s.refused, 1u);
        ASSERT_EQ(s.reactions, 1u);
        ASSERT_EQ(s.began, 1u);
        watch.order(command_of(CommandType::GroupMove, 0, {b, c}, 30, 20));                                    // one order, two ants, a walk that is carried out
        watch.order(command_of(CommandType::Stop, 0, {a}, 0, 0));
        watch.tally.command(command_of(CommandType::GroupMove, 0, {}, 30, 20), sim.current_tick());           // (the controller never releases an empty list: the tally ignores it)
        watch.run(200);
        ASSERT_EQ(s.orders, 2u);
        ASSERT_EQ(s.refused, 1u);
        ASSERT_EQ(s.reactions, 1u);
        ASSERT_EQ(s.began, 1u);
        ASSERT_TRUE(sim.get_unit(b).pos.x >= 29 && sim.get_unit(c).pos.x >= 29);                               // (the premise: they walked)
        watch.tally.command(command_of(CommandType::GroupMove, 0, {(1u << 20) + 5u}, 30, 20), sim.current_tick());   // an id that names no ant: an order, nothing else
        ASSERT_EQ(s.orders, 3u);
        ASSERT_EQ(&watch.tally.seat(99), &watch.tally.seat(0));                                                // (a seat out of range reads as seat 0, not outside the table)
    } TEST_END();

    TEST_CASE("AI22.2 A Refusal That Comes Later Than The Window Is A Reaction That Began, Not A Refused Order: A Walk That Was Fine When It Was Ordered, And A Wall Of Fire Round The Target While The Ant Walks")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 1);
        const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 30});
        Watch watch(sim);
        watch.run(5);
        watch.order(command_of(CommandType::GroupMove, 0, {a}, kPocket.x, kPocket.y));
        const uint64_t ordered = sim.current_tick();
        watch.run(20);
        ASSERT_EQ(watch.tally.seat(0).reactions, 0u);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) sim.set_fire_at(TileCoord{kPocket.x + dx, kPocket.y + dy}, 3600);
            }
        }
        uint64_t reacted = 0;
        for (int i = 0; i < 600 && reacted == 0; ++i) {
            watch.tick();
            if (watch.tally.seat(0).began != 0) reacted = sim.current_tick();
        }
        ASSERT_TRUE(reacted != 0);
        ASSERT_TRUE(reacted - ordered > CantGoTally::kRefusedWindow);                                          // (the premise: the ant meets the wall long after the window)
        watch.run(100);
        const CantGoTally::Seat& s = watch.tally.seat(0);
        ASSERT_EQ(s.orders, 1u);
        ASSERT_EQ(s.began, 1u);
        ASSERT_EQ(s.reactions, 1u);
        ASSERT_EQ(s.refused, 0u);
    } TEST_END();

    TEST_CASE("AI22.3 The Window: An Order Told Exactly kRefusedWindow Ticks Before The Reaction Counts As Refused, One Tick More Does Not; Of Two Orders For An Ant The Later One Is The One That Counts")
    {
        const uint64_t d = ticks_to_refusal();                                                                 // (the engine's answer to a walk: a few ticks)
        ASSERT_TRUE(d >= 1 && d < CantGoTally::kRefusedWindow);
        for (const uint64_t age : {CantGoTally::kRefusedWindow - d, CantGoTally::kRefusedWindow - d + 1}) {
            sim::SimulationEngine sim;
            pocket_field(sim);
            const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
            Watch watch(sim);
            watch.run(30);
            const Command into = command_of(CommandType::GroupMove, 0, {a}, kPocket.x, kPocket.y);
            watch.tally.command(into, sim.current_tick() - age);                                               // (told as an order that is `age` ticks old: only the tally's clock is moved)
            sim.apply_command(into);
            watch.run(30);
            ASSERT_EQ(watch.tally.seat(0).began, 1u);
            ASSERT_EQ(watch.tally.seat(0).refused, age + d <= CantGoTally::kRefusedWindow ? 1u : 0u);
        }
        {   // the first order for the ant (a walk that is fine) is 35 ticks old when the second, into the pocket, is refused: the second is the one that counts
            sim::SimulationEngine sim;
            pocket_field(sim);
            const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
            Watch watch(sim);
            watch.run(5);
            watch.order(command_of(CommandType::GroupMove, 0, {a}, 30, 20));
            watch.run(30);
            watch.order(command_of(CommandType::GroupMove, 0, {a}, kPocket.x, kPocket.y));
            watch.run(30);
            ASSERT_EQ(watch.tally.seat(0).orders, 2u);
            ASSERT_EQ(watch.tally.seat(0).refused, 1u);
            ASSERT_EQ(watch.tally.seat(0).began, 1u);
        }
    } TEST_END();

    TEST_CASE("AI22.4 A Can't-Go Loop Is One Beginning And Many Reactions, And A Group Order Is One Refused Order However Many Ants Turn Back: The Ring Of Fire Round A Gate With Two Workers On The Queue Row, One Order Out For Both, 300 Ticks: The Engine Sends Each Ant To Its Waiting Tile Again Every Few Ticks, And Each Time It Is Refused")
    {
        RingWorld world;
        world.build();
        Watch watch(world.sim);
        watch.run(5);
        watch.order(command_of(CommandType::GroupMove, 1, {world.victim, world.victim2}, 30, 30));
        watch.run(300);
        const CantGoTally::Seat& s = watch.tally.seat(1);
        ASSERT_EQ(s.orders, 1u);
        ASSERT_EQ(s.refused, 1u);                                                                              // (one order: not one for each ant)
        ASSERT_EQ(s.began, 2u);                                                                                // (an ant never leaves the state between two ticks)
        ASSERT_TRUE(s.reactions >= 60);                                                                        // (about one in 8 ticks for each)
        ASSERT_TRUE(world.sim.get_unit(world.victim).state == sim::UnitState::CantGo);
        ASSERT_TRUE(world.sim.get_unit(world.victim2).state == sim::UnitState::CantGo);
        ASSERT_EQ(watch.tally.seat(0).reactions, 0u);
    } TEST_END();

    TEST_CASE("AI22.5 An Ability That The Engine Refuses At Once (\"Can't Do That...\": A Fire Ant Sent To Light A Rock) Is Counted As A Reaction And As A Refused Order")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 1);
        sim.set_terrain(30, 30, sim::TERRAIN_OBSTACLE);
        const uint32_t f = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{20, 20});
        Watch watch(sim);
        watch.run(5);
        watch.order(command_of(CommandType::GroupSpecial, 0, {f}, 30, 30));
        watch.run(30);
        const CantGoTally::Seat& s = watch.tally.seat(0);
        ASSERT_EQ(s.orders, 1u);
        ASSERT_EQ(s.refused, 1u);
        ASSERT_EQ(s.reactions, 1u);
        ASSERT_EQ(s.began, 1u);
    } TEST_END();

    TEST_CASE("AI22.6 A Reaction Of An Ant That No Counted Order Names Is Not A Refused Order (It Began, And It Was Heard); An Order For Another Ant Is Not Refused By It")
    {
        sim::SimulationEngine sim;
        pocket_field(sim);
        const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        const uint32_t b = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
        Watch watch(sim);
        watch.run(5);
        watch.order(command_of(CommandType::GroupMove, 0, {a}, 30, 20));                                       // an order for a that is carried out ...
        sim.apply_command(command_of(CommandType::GroupMove, 0, {b}, kPocket.x, kPocket.y));                   // ... and a walk of b that the tally was not told of
        watch.run(40);
        const CantGoTally::Seat& s = watch.tally.seat(0);
        ASSERT_EQ(s.orders, 1u);
        ASSERT_EQ(s.began, 1u);
        ASSERT_EQ(s.reactions, 1u);
        ASSERT_EQ(s.refused, 0u);
    } TEST_END();

    TEST_CASE("AI22.7 The Seats Are Counted Apart: The Reaction Goes To The Seat That Hears It, The Order And Its Refusal To The Seat That Gave It")
    {
        sim::SimulationEngine sim;
        pocket_field(sim);
        const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        const uint32_t b = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 12});
        const uint32_t c = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{46, 12});
        Watch watch(sim);
        watch.run(5);
        watch.order(command_of(CommandType::GroupMove, 0, {a}, kPocket.x, kPocket.y));
        watch.order(command_of(CommandType::GroupMove, 1, {b}, 30, 12));
        sim.apply_command(command_of(CommandType::GroupMove, 1, {c}, kPocket.x, kPocket.y));                   // (the tally was not told: seat 1 hears it, nothing of seat 1 is refused)
        watch.run(60);
        const CantGoTally::Seat& s0 = watch.tally.seat(0);
        const CantGoTally::Seat& s1 = watch.tally.seat(1);
        ASSERT_EQ(s0.orders, 1u);
        ASSERT_EQ(s0.refused, 1u);
        ASSERT_EQ(s0.reactions, 1u);
        ASSERT_EQ(s0.began, 1u);
        ASSERT_EQ(s1.orders, 1u);
        ASSERT_EQ(s1.refused, 0u);
        ASSERT_EQ(s1.reactions, 1u);
        ASSERT_EQ(s1.began, 1u);
        ASSERT_EQ(watch.tally.seat(2).reactions + watch.tally.seat(3).reactions + watch.tally.seat(2).orders + watch.tally.seat(3).orders, 0u);
    } TEST_END();

    TEST_CASE("AI22.8 Counting Changes Nothing In The Engine: The Ring World (A Loop, 400 Ticks) Reaches The Same State Hash With The Tally Scanning Every Tick As Without It, And The News Items Are The Same")
    {
        const auto run = [&](bool counted, uint64_t& news_items) {
            RingWorld world;
            world.build();
            Watch watch(world.sim);
            const Command out = command_of(CommandType::GroupMove, 1, {world.victim}, 30, 30);
            for (int i = 0; i < 400; ++i) {
                world.sim.tick();
                if (i == 5) {
                    world.sim.apply_command(out);
                    if (counted) watch.tally.command(out, world.sim.current_tick());
                }
                const std::vector<sim::NewsEvent> news = world.sim.poll_news_events();
                news_items += news.size();
                if (counted) watch.tally.scan(world.sim, news);
            }
            return world.sim.state_hash().total;
        };
        uint64_t with_tally = 0;
        uint64_t without = 0;
        const auto h1 = run(true, with_tally);
        const auto h2 = run(false, without);
        ASSERT_TRUE(with_tally > 20);                                                                          // (the premise: the loop posted its news)
        ASSERT_EQ(with_tally, without);
        ASSERT_TRUE(h1 == h2);
    } TEST_END();

    TEST_CASE("AI22.9 The Arena Reports It: Two Seats Of Scripted Bots Send Three Ants Each To The Pile In The Lake Of SMALL (Which No Walk Reaches): Per Seat 3 Orders, As The Commands Of The Log Say, All 3 Refused, 3 Reactions And 3 First Ones, With Either Sink; The Same Match Twice Gives The Same Numbers")
    {
        for (const uint32_t latency : {0u, 3u}) {
            ArenaSpec spec;
            spec.level = &level_of("SMALL");
            spec.seed = 1;
            spec.max_ticks = 400;
            spec.latency_ticks = latency;
            spec.record = true;
            for (uint8_t seat = 0; seat < 2; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "worker";
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            spec.factory = [](const BotSpec&) { return std::make_unique<PokeBot>(); };
            const ArenaResult r = play_match(spec);
            const ArenaResult again = play_match(spec);
            ASSERT_EQ(r.seats.size(), 2u);
            for (size_t i = 0; i < r.seats.size(); ++i) {
                const ArenaSeatResult& seat = r.seats[i];
                uint32_t logged = 0;
                for (const RecordedCommand& rc : r.log) logged += rc.command.issuer == seat.spec.seat && sim::is_group_order(rc.command.type) && !rc.command.ants.empty() ? 1u : 0u;
                ASSERT_EQ(logged, 3u);
                ASSERT_EQ(seat.orders, logged);
                ASSERT_EQ(seat.refused_orders, 3u);
                ASSERT_EQ(seat.cantgo, 3u);
                ASSERT_EQ(seat.cantgo_began, 3u);
                ASSERT_EQ(again.seats[i].orders, seat.orders);
                ASSERT_EQ(again.seats[i].refused_orders, seat.refused_orders);
                ASSERT_EQ(again.seats[i].cantgo, seat.cantgo);
                ASSERT_EQ(again.seats[i].cantgo_began, seat.cantgo_began);
            }
        }
    } TEST_END();

    TEST_CASE("AI22.10 The Bot's Own Refused Orders Stay Under A Bound: Four Hard Standard Bots Play Whole Matches On TREASURE (Seeds 1 To 4, Four Matches): At Most 6 Of 1,000 Orders Are Refused (3.0 With The Fixes Of The Can't-Go Loop: 23 Of 7,635; 9.8 Without Them, cg=0: 83 Of 8,427); The Loops Themselves Are Reported, Not Bounded")
    {
        uint64_t orders = 0;
        uint64_t refused = 0;
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            ArenaSpec spec;
            spec.level = &level_of("TREASURE");
            spec.seed = seed;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_TRUE(r.match_over);
            for (const ArenaSeatResult& seat : r.seats) {
                orders += seat.orders;
                refused += seat.refused_orders;
            }
        }
        ASSERT_TRUE(orders >= 6000);                                                  // the matches were played (7,635 orders when this was pinned)
        ASSERT_TRUE(refused * 1000u <= orders * 6u);                                  // the bound, with twice the measured rate in hand
    } TEST_END();
}

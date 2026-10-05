// The styles of the standard bot and the plans of the levels (B4-1, AI11.x): "some aggressiveness / randomness between the bots" (the owner). Hand-made worlds and plain logic, quick enough for
// suite 2.20. What a style is: docs/BOTS.md, "Styles".
//
//   AI11.1  the words of the styles, which style a level may play (Hard: aggressive or raider only)
//   AI11.2  --bot SEAT:KIND:LEVEL:STYLE and SEAT:LEVEL:STYLE: every spelling, every refusal, the name of the seat (the style is not in it), the setup check
//   AI11.3  the draw: the same match seed and seat give the same style and the same plan, other seeds and seats give other ones (all four styles at Easy and Medium, two at Hard)
//   AI11.4  pinning: the style of a spec is the style that plays, through the registry and through the arena; a spec without one draws
//   AI11.5  the level's limits do not move with the style: the profile of the controller, and what the level unlocks (the gate, the theft, the second Thief, the raids, the Combat Ants)
//   AI11.7  the Raider's signature: Fire and Thief first in the opening and no Bomber, raids for small loot
//   AI11.8  the Economic style's signature: nobody to the centre, raids only for large loot, walls up before a thief shows (it defends like the others: the Combat Ant is taken in the opening)
//   AI11.9  the Aggressive style's signature: more ants to the centre, one more defender, a squad that goes for carriers within 8 tiles (with careful odds at Medium), at Hard a stolen Fire Ant
//   AI11.10 the Defensive style's signature: walls up before a thief shows (no interception: measured as a loss), one more defender, nobody to the centre
//   AI11.11 what counts as an attack on the bot (Memory::last_attacked: a blow on an ant that is no Thief with an enemy that is no Thief next to it) and the adaptive Combat policy: the second
//           Combat Ant (Hard) is wanted once the bot is attacked, and not before
//   AI11.6  the shipped plans: the tactics that the tournaments measured as losses are off at every level, and the ones that pay are on
#include "ai_test.hpp"
#include "b41_helpers.hpp"

#include "ants_ai/arena.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

constexpr Style kStyles[4] = {Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive};

// The plan that a bot of (level, spec style) plays in the match of `match_seed` at `seat`: what the controller would hand it (seat_seed, then the first number of the seat's generator)
LevelPlan plan_in_match(Level level, Style style, uint32_t match_seed, uint8_t seat, Style* played = nullptr) {
    BotRng seat_rng(seat_seed(match_seed, seat, "standard"));
    StandardBot bot(level, style);
    bot.start(BotContext{seat, profile_for(level), seat_rng.next(), nullptr});
    if (played != nullptr) *played = bot.style();
    return bot.tactics().plan;
}

// Every field of a plan that a style or the bot's variations may move, in one comparable line
std::string plan_line(const LevelPlan& p) {
    std::string s;
    const auto add = [&](int64_t v) { s += std::to_string(v) + ","; };
    add(static_cast<int>(p.style));
    add(p.defenders);
    add(p.leash_tiles);
    add(p.fight_linger_ticks);
    add(p.contest_opening_ants);
    add(p.contest_low);
    add(p.contest_high);
    add(static_cast<int>(p.wall_trigger));
    add(p.wall_latch_ticks);
    add(p.renew_lead_ticks);
    add(p.secure_kinds);
    add(p.ally_help);
    add(p.combat_when_attacked);
    add(p.intercepts);
    add(p.max_combat);
    add(p.max_thief);
    add(p.steals);
    add(p.strike_margin);
    add(p.hatch_window);
    add(p.avoids_guarded_hills);
    add(p.raid_min_loot);
    add(p.raid_black_ticks);
    for (const sim::AntType t : p.opening_order) add(static_cast<int>(t));
    return s;
}

// The switches of a plan that say what a level may do at all (not how much), for the comparison of styles within a level
std::string unlocks_line(const LevelPlan& p) {
    std::string s;
    const auto add = [&](bool v) { s += v ? '1' : '0'; };
    add(p.takes_combat);
    add(p.takes_thief);
    add(p.raids);
    add(p.gate);
    add(p.secure_side);
    add(p.counters);
    add(p.typed_harvest);
    add(p.fire_aware);
    add(p.combat_harvests);
    add(p.guards);
    add(p.contest_aware);
    add(p.hatches);
    add(p.wipe_focus);
    add(p.ambush);
    add(p.steals);
    return s;
}

}  // namespace

void run_b41_style_tests() {
    TEST_CASE("AI11.1 The Words Of The Styles: Aggressive, Economic, Raider, Defensive (And Random For A Bot That Draws); Every Level Plays Every Style But Hard, Which Plays Aggressive Or Raider Only") {
        for (const Style s : {Style::Random, Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive}) {
            Style back = Style::Defensive;
            ASSERT_TRUE(parse_style(style_name(s), back) && back == s);
            std::string upper = style_name(s);
            for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
            back = Style::Defensive;
            ASSERT_TRUE(parse_style(upper, back) && back == s);                                                // either case
        }
        Style keep = Style::Raider;
        for (const char* bad : {"", "aggressiv", "raiders", "agressive", " economic", "defensive ", "hard", "medium", "a", "x\xC3\xA9"}) ASSERT_FALSE(parse_style(bad, keep));
        ASSERT_TRUE(keep == Style::Raider);                                                                    // nothing changes on a refusal
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            ASSERT_TRUE(style_allowed(level, Style::Random));
            ASSERT_TRUE(style_allowed(level, Style::Aggressive));
            ASSERT_TRUE(style_allowed(level, Style::Raider));
            ASSERT_EQ(style_allowed(level, Style::Economic), level != Level::Hard);
            ASSERT_EQ(style_allowed(level, Style::Defensive), level != Level::Hard);
        }
    } TEST_END();

    TEST_CASE("AI11.2 --bot With A Style: SEAT:KIND:LEVEL:STYLE And SEAT:LEVEL:STYLE, Either Case; A Style Of A Level That May Not Play It, Of A Bot That Has None Or That Does Not Exist Is Refused (The Spec Stays As It Was); The Name Of The Seat Does Not Tell The Style; The Setup Check Refuses The Same")
    {
        BotSpec s;
        std::string why;
        ASSERT_TRUE(parse_bot_spec("2:standard:hard:aggressive", s, why) && s.seat == 2 && s.kind == "standard" && s.level == Level::Hard && s.style == Style::Aggressive && why.empty());
        ASSERT_TRUE(parse_bot_spec("0:hard:raider", s, why) && s.seat == 0 && s.kind == "standard" && s.level == Level::Hard && s.style == Style::Raider);
        ASSERT_TRUE(parse_bot_spec("1:Medium:Defensive", s, why) && s.kind == "standard" && s.level == Level::Medium && s.style == Style::Defensive);
        ASSERT_TRUE(parse_bot_spec("3:STANDARD:EASY:ECONOMIC", s, why) && s.seat == 3 && s.level == Level::Easy && s.style == Style::Economic);
        ASSERT_TRUE(parse_bot_spec("2:standard:medium:random", s, why) && s.style == Style::Random);
        ASSERT_TRUE(parse_bot_spec("2:hard", s, why) && s.style == Style::Random);                              // no style named: it draws
        ASSERT_TRUE(parse_bot_spec("2", s, why) && s.style == Style::Random);
        ASSERT_TRUE(parse_bot_spec("2:worker:easy", s, why) && s.style == Style::Random);
        // refusals
        BotSpec keep;
        keep.seat = 1;
        keep.kind = "idle";
        keep.level = Level::Hard;
        keep.style = Style::Raider;
        const char* const bad[] = {"2:hard:economic", "2:standard:hard:defensive", "2:worker:easy:raider", "2:idle:medium:aggressive", "2:standard:medium:wild", "2:medium:", "2:hard:raider:x", "2:standard:hard:raider:x",
                                   "2:easy:medium", "2:standard:aggressive", "2:aggressive", "2:hard:Hard"};
        for (const char* text : bad) {
            BotSpec t = keep;
            std::string reason;
            ASSERT_FALSE(parse_bot_spec(text, t, reason));
            ASSERT_FALSE(reason.empty());
            ASSERT_TRUE(reason.size() < 200u);
            ASSERT_TRUE(t.seat == 1 && t.kind == "idle" && t.level == Level::Hard && t.style == Style::Raider);
            for (const char c : reason) ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
        }
        // the seat is called "Bot (Level)" whatever the style: nothing pretends that a bot is a person, nothing tells the style before the match
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            BotSpec a;
            a.level = level;
            BotSpec b = a;
            b.style = Style::Raider;
            ASSERT_EQ(bot_display_name(a), bot_display_name(b));
            ASSERT_TRUE(bot_display_name(a).rfind("Bot (", 0) == 0);
        }
        // the setup check
        SetupInfo info;
        info.roster = 0x0F;
        info.human_mask = 0x01;
        BotSpec ok;
        ok.seat = 1;
        ok.level = Level::Hard;
        ok.style = Style::Raider;
        info.bots = {ok};
        ASSERT_EQ(check_setup(info), std::string());
        info.bots[0].style = Style::Economic;
        ASSERT_FALSE(check_setup(info).empty());                                                                // a Hard bot does not play it
        info.bots[0].level = Level::Medium;
        ASSERT_EQ(check_setup(info), std::string());
        info.bots[0].kind = "worker";
        ASSERT_FALSE(check_setup(info).empty());                                                                // the worker has no style
        info.bots[0].style = Style::Random;
        ASSERT_EQ(check_setup(info), std::string());
    } TEST_END();

    TEST_CASE("AI11.3 The Draw: The Same Match Seed And Seat Give The Same Style And The Same Plan (A Replay Needs No Bot, A Restart Of The Match Plays Alike); Other Seeds And Seats Give Other Ones: All Four Styles At Easy And Medium, Aggressive And Raider At Hard, And The Numbers Of The Plans Vary")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            std::map<int, int> count;
            std::set<uint32_t> loot;
            std::set<std::string> plans;
            for (uint32_t seed = 1; seed <= 16; ++seed) {
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    Style a = Style::Random;
                    Style b = Style::Random;
                    const LevelPlan pa = plan_in_match(level, Style::Random, seed, seat, &a);
                    const LevelPlan pb = plan_in_match(level, Style::Random, seed, seat, &b);
                    ASSERT_TRUE(a != Style::Random && a == b);                                                  // drawn, and the same the second time
                    ASSERT_EQ(plan_line(pa), plan_line(pb));
                    ASSERT_TRUE(pa.style == a);
                    ASSERT_TRUE(style_allowed(level, a));
                    ++count[static_cast<int>(a)];
                    loot.insert(pa.raid_min_loot);
                    plans.insert(plan_line(pa));
                }
            }
            // 64 draws: every style that the level allows comes up (each of four at the expected 16, each of two at 32; a quarter of that at least), nothing else does
            for (const Style s : kStyles) {
                const int n = count[static_cast<int>(s)];
                if (style_allowed(level, s)) ASSERT_TRUE(n >= (level == Level::Hard ? 16 : 8));
                else ASSERT_EQ(n, 0);
            }
            ASSERT_TRUE(loot.size() >= 3u);                                                                     // the thresholds vary from bot to bot
            ASSERT_TRUE(plans.size() >= 20u);
        }
        // another match seed or another seat is another draw: two seats of one match do not all play alike
        std::set<int> seats_styles;
        for (uint8_t seat = 0; seat < 4; ++seat) {
            Style s = Style::Random;
            plan_in_match(Level::Medium, Style::Random, 5, seat, &s);
            seats_styles.insert(static_cast<int>(s));
        }
        std::set<int> seeds_styles;
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            Style s = Style::Random;
            plan_in_match(Level::Medium, Style::Random, seed, 0, &s);
            seeds_styles.insert(static_cast<int>(s));
        }
        ASSERT_TRUE(seeds_styles.size() >= 3u);
        ASSERT_TRUE(seats_styles.size() >= 2u);
    } TEST_END();

    TEST_CASE("AI11.4 Pinning: The Style Of The Spec Is The Style That Plays (Through The Registry And Through A Match In The Arena), Whatever The Seed; A Spec Without One Draws; A Bot With A Plan Of Its Own Plays That Plan")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (const Style s : kStyles) {
                if (!style_allowed(level, s)) continue;
                for (uint32_t seed : {1u, 2u, 77u}) {
                    Style played = Style::Random;
                    const LevelPlan p = plan_in_match(level, s, seed, static_cast<uint8_t>(seed % 4), &played);
                    ASSERT_TRUE(played == s && p.style == s);
                }
                BotSpec spec;
                spec.seat = 1;
                spec.level = level;
                spec.style = s;
                const std::unique_ptr<Bot> bot = make_bot(spec);
                ASSERT_TRUE(bot != nullptr);
                bot->start(BotContext{1, profile_for(level), 12345u, nullptr});
                ASSERT_TRUE(static_cast<const StandardBot&>(*bot).style() == s);
            }
        }
        {   // a bot with a plan of its own is not styled: it plays the plan as it is
            LevelPlan plan = plan_for(Level::Medium);
            plan.raid_min_loot = 77;
            StandardBot bot(plan);
            bot.start(BotContext{0, profile_for(Level::Medium), 99u, nullptr});
            ASSERT_TRUE(bot.style() == Style::Random);
            ASSERT_EQ(plan_line(bot.tactics().plan), plan_line(plan));
        }
        {   // the arena reports the style that played, pinned or drawn
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL"));
            ArenaSpec spec;
            spec.level = &tiny;
            spec.seed = 3;
            spec.max_ticks = 200;
            const Style pinned[4] = {Style::Aggressive, Style::Defensive, Style::Random, Style::Random};
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = Level::Medium;
                b.style = pinned[seat];
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_EQ(r.seats[0].style, std::string("aggressive"));
            ASSERT_EQ(r.seats[1].style, std::string("defensive"));
            ASSERT_TRUE(!r.seats[2].style.empty() && r.seats[2].style != "random");                              // drawn
            ASSERT_TRUE(!r.seats[3].style.empty() && r.seats[3].style != "random");
            const ArenaResult again = play_match(spec);                                                          // and the same again: the same match seed and seats
            for (size_t i = 0; i < 4; ++i) ASSERT_EQ(r.seats[i].style, again.seats[i].style);
            ASSERT_EQ(r.hash, again.hash);
        }
    } TEST_END();

    TEST_CASE("AI11.5 The Limits Of A Level Do Not Move With The Style: The Controller's Profile (Look Interval, Reaction, Budget) Is The Level's For Every Style, And What The Level Unlocks (The Gate And The Theft At Hard Only, At Most The Thieves And Combat Ants Of The Level, Raids At Medium And Hard) Is The Same For Every Style; Easy Plays The Same Plan Whatever The Style But For Its Numbers")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 5);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            // the profile: the controller's seat for every style
            for (const Style s : {Style::Random, Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive}) {
                if (!style_allowed(level, s)) continue;
                BotController c(sim, 9);
                RecordingSink sink(sim);
                BotSpec spec;
                spec.seat = 0;
                spec.level = level;
                spec.style = s;
                std::string why;
                ASSERT_TRUE(c.add(spec, sink, why));
                const Profile* got = c.profile(0);
                const Profile want = profile_for(level);
                ASSERT_TRUE(got != nullptr);
                ASSERT_TRUE(got->decision_interval == want.decision_interval && got->reaction_delay == want.reaction_delay && got->rate_milli_cps == want.rate_milli_cps && got->burst == want.burst &&
                            got->max_ants_per_command == want.max_ants_per_command && got->intent_ttl == want.intent_ttl && got->jitter_percent == want.jitter_percent);
            }
            // the unlocks, over the whole draw
            const LevelPlan neutral = plan_for(level);
            std::set<std::string> unlock_lines;
            for (uint32_t seed = 1; seed <= 12; ++seed) {
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    const LevelPlan p = plan_in_match(level, Style::Random, seed, seat);
                    unlock_lines.insert(unlocks_line(p));
                    ASSERT_EQ(p.gate, level == Level::Hard);
                    ASSERT_EQ(p.steals, level == Level::Hard);
                    ASSERT_EQ(p.takes_thief, level != Level::Easy);
                    ASSERT_EQ(p.raids, level != Level::Easy);
                    ASSERT_EQ(p.takes_combat, level != Level::Easy);
                    ASSERT_TRUE(p.max_thief <= neutral.max_thief);
                    ASSERT_TRUE(p.max_combat <= (level == Level::Hard ? 2u : level == Level::Medium ? 1u : 0u));
                    ASSERT_TRUE(p.defenders <= neutral.defenders + 1u);
                    if (level != Level::Hard) ASSERT_TRUE(!p.sabotage && !p.strikes && p.fire_extra == 0u);               // (theft and the strike are Hard's)
                    if (level == Level::Easy) ASSERT_TRUE(!p.harass);
                    ASSERT_TRUE(p.contest_opening_ants <= neutral.contest_opening_ants + 1u);
                    if (level == Level::Easy) ASSERT_EQ(p.opening_order[0], sim::AntType::Fire);
                }
            }
            ASSERT_EQ(unlock_lines.size(), 1u);                                                                 // no style unlocks or locks a tactic of the level
            if (level == Level::Easy) {                                                                          // Easy: only the numbers move
                std::set<std::string> flags;
                for (uint32_t seed = 1; seed <= 12; ++seed) {
                    const LevelPlan p = plan_in_match(level, Style::Random, seed, static_cast<uint8_t>(seed % 4));
                    flags.insert(std::to_string(p.defenders) + std::to_string(static_cast<int>(p.wall_trigger)) + std::to_string(p.secure_kinds) + std::to_string(p.max_combat) + std::to_string(p.max_thief) + std::to_string(p.intercepts) +
                                 std::to_string(p.ally_help) + std::to_string(p.contest_opening_ants) + std::to_string(p.combat_when_attacked));
                }
                ASSERT_EQ(flags.size(), 1u);
            }
        }
    } TEST_END();

    // ---- the signatures ----------------------------------------------------------------------------------------------------------------------------------------
    // A bot of a pinned style in a hand-made world: the plan that the style makes for the match (with its variations), played as it is (so that the Rig's fixed seed does not matter)
    struct Pinned {
        static std::unique_ptr<Bot> bot(Level level, Style style, uint32_t seed = 3, uint8_t seat = 0) { return std::make_unique<StandardBot>(plan_in_match(level, style, seed, seat)); }
        static LevelPlan plan(Level level, Style style, uint32_t seed = 3, uint8_t seat = 0) { return plan_in_match(level, style, seed, seat); }
    };

    TEST_CASE("AI11.7 The Raider: Fire And Thief Are Its First Trips (The Bomber Is Not Taken), It Raids A Hill That Shows 25 Points Where The Neutral Plan Of Medium (30) And The Economic Style Do Not, At Medium And Hard")
    {
        for (const Level level : {Level::Medium, Level::Hard}) {
            std::vector<TileCoord> raider_order;
            for (const bool raider : {true, false}) {
                sim::SimulationEngine sim;
                start_match(sim, "TREASURE", 5, 0x0F);
                Rig rig(sim, 0, level, raider ? Pinned::bot(level, Style::Raider) : std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(900);
                std::vector<TileCoord> order;
                for (const auto& e : rig.proposed) {
                    if (e.second.type == CommandType::GroupMove && e.second.ants.size() == 1 && (e.second.tile_x == 57 || e.second.tile_x == 48 || e.second.tile_x == 35) &&
                        (e.second.tile_y == 24 || e.second.tile_y == 25 || e.second.tile_y == 14)) {
                        order.push_back(tc(e.second.tile_x, e.second.tile_y));
                    }
                }
                ASSERT_TRUE(order.size() >= 2);
                ASSERT_TRUE(order[0] == tc(57, 24));                                                              // the Fire first, in both
                if (raider) {
                    ASSERT_TRUE(order[1] == tc(35, 14));                                                          // then the Thief
                    ASSERT_TRUE(sim.grid().has_powerup_at(tc(48, 25)));                                           // the Bomber of the side stays where it is
                    ASSERT_EQ(count_type(sim, 0, sim::AntType::Bomber), 0u);
                } else {
                    ASSERT_TRUE(order[1] == tc(48, 25));                                                          // the Bomber second
                    ASSERT_FALSE(sim.grid().has_powerup_at(tc(48, 25)));
                }
                ASSERT_FALSE(sim.grid().has_powerup_at(tc(35, 14)));                                              // the Thief is taken either way
                ASSERT_FALSE(sim.grid().has_powerup_at(tc(57, 24)));
            }
        }
        // the raid threshold: team 1 shows 25 points
        for (const Style style : {Style::Raider, Style::Economic, Style::Aggressive}) {
            sim::SimulationEngine sim;
            empty_field(sim, 31);
            sim.set_player_score(1, 25);
            sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
            Rig rig(sim, 0, Level::Medium, Pinned::bot(Level::Medium, style), 4, 4);
            rig.run(40);
            const size_t raids = rig.as<StandardBot>().raids().raids_ordered();
            ASSERT_EQ(raids >= 1, style != Style::Economic && style != Style::Aggressive ? true : style == Style::Aggressive ? (rig.as<StandardBot>().tactics().plan.raid_min_loot <= 25) : false);
        }
        sim::SimulationEngine sim;
        empty_field(sim, 31);
        sim.set_player_score(1, 25);
        sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
        Rig neutral(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
        neutral.run(40);
        ASSERT_EQ(neutral.as<StandardBot>().raids().raids_ordered(), 0u);                                         // 25 is below the neutral plan's 30
        // Hard's neutral plan raids a hill that shows 20 points (a raid is a swing of 30 and a trip of a few hundred ticks: it pays), Medium's (30) does not
        for (const Level level : {Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim20;
            empty_field(sim20, 31);
            sim20.set_player_score(1, 20);
            sim20.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
            Rig rig20(sim20, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            rig20.run(40);
            ASSERT_EQ(rig20.as<StandardBot>().raids().raids_ordered() >= 1, level == Level::Hard);
        }
    } TEST_END();

    TEST_CASE("AI11.8 The Economic Style: Nobody Is Sent To The Contested Centre (The Neutral Medium Sends One Ant), A Hill That Shows 60 Points Is Not Raided (150 Is), The Combat Ant Is Taken In The Opening Like Everywhere, And The Fire Walls Of The Thief Hole Stand Before A Thief Shows (An Enemy Does Not Even Have To Play)")
    {
        {   // the centre of TREASURE
            sim::SimulationEngine probe;
            start_match(probe, "TREASURE", 7, 0x0F);
            const MapInfo pmap(probe);
            size_t centre = 0;
            for (const PileInfo& p : pmap.piles()) {
                if (p.anchor.x == 30 && p.anchor.y == 29) centre = p.index;
            }
            const size_t piles = probe.grid().food_objects().size();
            for (const bool economic : {true, false}) {
                sim::SimulationEngine sim;
                start_match(sim, "TREASURE", 7, 0x0F);
                LevelPlan neutral = plan_for(Level::Medium);
                neutral.race = false;                                                                          // (the opening of v0.5.0: one ant for the neutral Medium; the race of the contest batch has AI20.1)
                Rig rig(sim, 0, Level::Medium, economic ? Pinned::bot(Level::Medium, Style::Economic) : std::make_unique<StandardBot>(neutral), 4, 4);
                rig.run(60);
                size_t at_centre = 0;
                for (const auto& e : rig.proposed) {
                    if (e.first > 60 || e.second.type != CommandType::GroupMove || sim.grid().has_powerup_at(tc(e.second.tile_x, e.second.tile_y))) continue;
                    if (pile_of_tile(rig.map(), piles, tc(e.second.tile_x, e.second.tile_y)) == static_cast<int>(centre)) at_centre += e.second.ants.size();
                }
                ASSERT_EQ(at_centre, economic ? 0u : 1u);
            }
        }
        for (const int32_t score : {60, 150}) {                                                                    // raid thresholds: the Economic style's 90 (67 to 112 with its variations)
            sim::SimulationEngine sim;
            empty_field(sim, 31);
            sim.set_player_score(1, score);
            sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
            Rig rig(sim, 0, Level::Medium, Pinned::bot(Level::Medium, Style::Economic), 4, 4);
            rig.run(40);
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered() >= 1, score == 150);
        }
        for (const bool economic : {true, false}) {                                                                // the Combat Ant: it defends like the others (a worker that fights costs nothing), in the opening
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x0F);
            sim.apply_command(command_of(CommandType::GroupMove, 1, {ants_of(sim, 1)[0]}, 30, 27));                // an enemy that plays
            Rig rig(sim, 0, Level::Medium, economic ? Pinned::bot(Level::Medium, Style::Economic) : std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(1100);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Combat), 1u);
            ASSERT_FALSE(sim.grid().has_powerup_at(tc(37, 2)));
        }
        {   // walls before a thief shows: a Thief power-up that an enemy reaches, and no enemy that plays
            for (const bool economic : {true, false}) {
                WallWorld w;
                w.build(false, 1);
                LevelPlan plan = economic ? Pinned::plan(Level::Medium, Style::Economic) : plan_for(Level::Medium);
                plan.secure_side = false;
                Rig rig(w.sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(900);
                ASSERT_EQ(walls_east(w.sim, kFightHills[0]), economic ? 3u : 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI11.9 The Aggressive Style: Medium Sends Two Ants To The Contested Centre And Hard Three (One More Than The Neutral Plans), One Defender More Answers A Blow, The Squad Goes For A Carrier Within 8 Tiles And Not For One Beyond It (Medium Only When It Is Clearly Stronger Than What Stands Near The Carrier, Hard When It Is As Strong), And At Hard The Bot Steals A Second Fire Ant For The Sabotage Of The Best Opponent's Gate")
    {
        {   // the centre
            sim::SimulationEngine probe;
            start_match(probe, "TREASURE", 7, 0x0F);
            const MapInfo pmap(probe);
            size_t centre = 0;
            for (const PileInfo& p : pmap.piles()) {
                if (p.anchor.x == 30 && p.anchor.y == 29) centre = p.index;
            }
            const size_t piles = probe.grid().food_objects().size();
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                start_match(sim, "TREASURE", 7, 0x0F);
                LevelPlan aggressive = Pinned::plan(level, Style::Aggressive);
                aggressive.race = false;                                                                       // (the opening of v0.5.0: one ant more than the neutral plan; the race of the contest batch has AI20.1)
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(aggressive), 4, 4);
                rig.run(60);
                size_t at_centre = 0;
                for (const auto& e : rig.proposed) {
                    if (e.first > 60 || e.second.type != CommandType::GroupMove || sim.grid().has_powerup_at(tc(e.second.tile_x, e.second.tile_y))) continue;
                    if (pile_of_tile(rig.map(), piles, tc(e.second.tile_x, e.second.tile_y)) == static_cast<int>(centre)) at_centre += e.second.ants.size();
                }
                ASSERT_EQ(at_centre, level == Level::Medium ? 2u : 3u);
            }
        }
        for (const Level level : {Level::Medium, Level::Hard}) {                                                   // one defender more than the neutral plan
            sim::SimulationEngine sim;
            empty_field(sim, 3);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 19});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{25, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 18});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 22});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 27});
            LevelPlan plan = Pinned::plan(level, Style::Aggressive);
            plan.harass = false;                                                                                   // (the strike back is the subject here)
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
            for (int t = 0; t < 300; ++t) {
                keep_attacking(sim, 1, enemy, victim);
                rig.tick();
            }
            const std::pair<uint64_t, Command>* first = nullptr;
            for (const auto& e : rig.sent) {
                if (e.second.type == CommandType::GroupAttack && first == nullptr) first = &e;
            }
            ASSERT_TRUE(first != nullptr);
            ASSERT_EQ(first->second.ants.size(), static_cast<size_t>(plan_for(level).defenders + 1u));
        }
        // the squad's reach: a Combat Ant at (30, 30), a carrier of team 1 on (38, 30) (8 tiles) or on (43, 30) (13 tiles), no other enemy near it
        for (const Level level : {Level::Medium, Level::Hard}) {
            for (const int32_t x : {38, 43}) {
                sim::SimulationEngine sim;
                empty_field(sim, 61);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
                const uint32_t carrier = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{x, 30});
                sim.get_unit(carrier).pick_up_food(1, 25);
                Rig rig(sim, 0, level, Pinned::bot(level, Style::Aggressive), 4, 4);
                rig.run(30);
                const bool goes = rig.proposed_count(CommandType::GroupAttack) >= 1;
                ASSERT_EQ(goes, x == 38);                                                                         // 8 tiles but not 13, at both levels
            }
        }
        {   // the odds: one Combat Ant (8) against six idle workers near the carrier (6): Hard (100 percent) goes, Medium (150 percent: 8 against 9) does not
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                empty_field(sim, 61);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
                const uint32_t carrier = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 30});
                sim.get_unit(carrier).pick_up_food(1, 25);
                for (int i = 0; i < 6; ++i) sim.spawn_unit(1, sim::AntType::Worker, TileCoord{37 + i % 3, 32 + i / 3});
                Rig rig(sim, 0, level, Pinned::bot(level, Style::Aggressive), 4, 4);
                rig.run(30);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) >= 1, level == Level::Hard);
            }
        }
        {   // the squad learns: a member that is hurt (below 7 hit points) leaves, and the team of the ant next to it is left alone (3,000 ticks, twice as long the next time), another team's carrier is not
            sim::SimulationEngine sim;
            empty_field(sim, 61);
            const uint32_t fighter = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
            const uint32_t c1 = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 30});
            sim.get_unit(c1).pick_up_food(1, 25);
            const uint32_t c2 = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{30, 36});
            sim.get_unit(c2).pick_up_food(2, 25);
            Rig rig(sim, 0, Level::Hard, Pinned::bot(Level::Hard, Style::Aggressive), 4, 4);
            rig.run(60);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.harass().pauses(), 0u);
            ASSERT_TRUE(bot.harass().attacks_ordered() >= 1);
            sim.get_unit(fighter).hp = 6;                                                                   // it was hurt, with an enemy of team 1 next to it
            sim.get_unit(fighter).pos.x = sim.get_unit(c1).pos.x - 1;
            sim.get_unit(fighter).pos.y = sim.get_unit(c1).pos.y;
            rig.run(8);
            ASSERT_EQ(bot.harass().pauses(), 1u);
            ASSERT_EQ(bot.harass().squad(), 0u);                                                            // it left
            sim.get_unit(fighter).hp = 10;
            const size_t before = rig.proposed.size();
            rig.run(120);
            bool on_team1 = false;
            bool on_team2 = false;
            for (size_t i = before; i < rig.proposed.size(); ++i) {
                const Command& c = rig.proposed[i].second;
                if (c.type != CommandType::GroupAttack) continue;
                on_team1 = on_team1 || (std::abs(c.tile_x - 36) <= 2 && std::abs(c.tile_y - 30) <= 2);                // (a blow throws the victim a tile or two: the tiles where the carriers stood)
                on_team2 = on_team2 || (std::abs(c.tile_x - 30) <= 2 && std::abs(c.tile_y - 36) <= 2);
            }
            ASSERT_FALSE(on_team1);                                                                         // the team that hurt it is no prey now (carrier c1 stays idle in range)
            ASSERT_TRUE(on_team2);
        }
        {   // a second Fire Ant by theft (the neighbours' ants are away from home: their Fire power-ups are unguarded), the Raider and the neutral plan keep the one of their side
            for (const Style style : {Style::Aggressive, Style::Raider}) {
                sim::SimulationEngine sim;
                start_match(sim, "TREASURE", 5, 0x0F);
                for (uint8_t t = 1; t < 4; ++t) {
                    for (const uint32_t id : ants_of(sim, t)) sim.get_unit(id).hp = 0;
                }
                const uint32_t walker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{58, 58});
                sim.apply_command(command_of(CommandType::GroupMove, 1, {walker}, 50, 58));                         // an enemy plays, far from every power-up
                Rig rig(sim, 0, Level::Hard, Pinned::bot(Level::Hard, style), 4, 4);
                rig.run(1500);
                ASSERT_EQ(count_type(sim, 0, sim::AntType::Fire), style == Style::Aggressive ? 2u : 1u);
            }
        }
    } TEST_END();

    TEST_CASE("AI11.10 The Defensive Style: The Fire Walls Of The Thief Hole Stand Before A Thief Shows, An Enemy Thief That Stands Near The Hill Is Left To The Walls As In Every Plan (Interception Measured As A Loss), One Defender More Answers A Blow, Nobody Is Sent To The Contested Centre")
    {
        {   // walls before a thief shows
            for (const bool defensive : {true, false}) {
                WallWorld w;
                w.build(false, 1);
                LevelPlan plan = defensive ? Pinned::plan(Level::Medium, Style::Defensive) : plan_for(Level::Medium);
                plan.secure_side = false;
                Rig rig(w.sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(900);
                ASSERT_EQ(walls_east(w.sim, kFightHills[0]), defensive ? 3u : 0u);
            }
        }
        {   // an enemy Thief that stands near the hill (idle, empty-handed, 6 tiles from the raid tile)
            for (const bool defensive : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 71);
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 8});
                const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{13, 7});
                LevelPlan plan = defensive ? Pinned::plan(Level::Medium, Style::Defensive) : plan_for(Level::Medium);
                plan.secure_side = false;
                plan.wall_trigger = WallTrigger::Never;                                                             // (the attack is the subject, not the walls)
                Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(200);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);                                        // nobody goes for it, in the style or without
                ASSERT_TRUE(alive(sim, thief) && sim.get_unit(thief).hp == 10);
                ASSERT_FALSE(Pinned::plan(Level::Medium, Style::Defensive).intercepts);
            }
        }
        {   // one defender more
            sim::SimulationEngine sim;
            empty_field(sim, 3);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 19});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{25, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23, 18});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 27});
            Rig rig(sim, 0, Level::Medium, Pinned::bot(Level::Medium, Style::Defensive), 4, 4);
            for (int t = 0; t < 300; ++t) {
                keep_attacking(sim, 1, enemy, victim);
                rig.tick();
            }
            const std::pair<uint64_t, Command>* first = nullptr;
            for (const auto& e : rig.sent) {
                if (e.second.type == CommandType::GroupAttack && first == nullptr) first = &e;
            }
            ASSERT_TRUE(first != nullptr);
            ASSERT_EQ(first->second.ants.size(), 3u);                                                              // Medium's 2 and one more
        }
        {   // the centre: nobody
            sim::SimulationEngine probe;
            start_match(probe, "TREASURE", 7, 0x0F);
            const MapInfo pmap(probe);
            size_t centre = 0;
            for (const PileInfo& p : pmap.piles()) {
                if (p.anchor.x == 30 && p.anchor.y == 29) centre = p.index;
            }
            const size_t piles = probe.grid().food_objects().size();
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 7, 0x0F);
            Rig rig(sim, 0, Level::Medium, Pinned::bot(Level::Medium, Style::Defensive), 4, 4);
            rig.run(60);
            size_t at_centre = 0;
            for (const auto& e : rig.proposed) {
                if (e.first > 60 || e.second.type != CommandType::GroupMove || sim.grid().has_powerup_at(tc(e.second.tile_x, e.second.tile_y))) continue;
                if (pile_of_tile(rig.map(), piles, tc(e.second.tile_x, e.second.tile_y)) == static_cast<int>(centre)) at_centre += e.second.ants.size();
            }
            ASSERT_EQ(at_centre, 0u);
        }
        {   // the help for the ally (AI8.6 plays it) is the Defensive style's alone: no other style and no neutral plan answers a blow on an ally's ant
            ASSERT_TRUE(Pinned::plan(Level::Medium, Style::Defensive).ally_help);
            for (const Style style : {Style::Aggressive, Style::Economic, Style::Raider}) ASSERT_FALSE(Pinned::plan(Level::Medium, style).ally_help);
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_FALSE(plan_for(level).ally_help);
        }
    } TEST_END();

    TEST_CASE("AI11.11 What Counts As An Attack On The Bot: A Blow On An Ant That Is No Thief With An Enemy That Is No Thief Next To It; Not A Hit On Its Thief (The Defenders Of A Raided Hill Answer It), Not A Blast (Nobody Next To The Ant), Not A Thief Standing Next To A Hit Worker; And The Adaptive Combat Policy: The Second Combat Ant Of Hard Is Wanted Once The Bot Is Attacked, Not Before")
    {
        const auto last_attacked = [&](bool hit_worker, bool hit_thief, bool enemy_worker_near, bool enemy_thief_near, bool enemy_far) {
            sim::SimulationEngine sim;
            empty_field(sim, 81);
            const uint32_t worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{23, 20});
            if (enemy_worker_near) sim.spawn_unit(1, sim::AntType::Worker, TileCoord{enemy_far ? 40 : 21, enemy_far ? 40 : 21});
            if (enemy_thief_near) sim.spawn_unit(1, sim::AntType::Thief, TileCoord{enemy_far ? 40 : 22, enemy_far ? 41 : 21});
            const MapInfo map(sim);
            Memory memory;
            memory.update(BotView::build(sim, 0, &map), map);
            sim.tick();
            sim.clear_news_events();
            if (hit_worker) sim.get_unit(worker).hp = 8;
            if (hit_thief) sim.get_unit(thief).hp = 8;
            memory.update(BotView::build(sim, 0, &map), map);
            return memory.last_attacked();
        };
        ASSERT_TRUE(last_attacked(true, false, true, false, false) != 0);                                    // a worker hit, an enemy worker next to it: an attack
        ASSERT_TRUE(last_attacked(true, true, true, false, false) != 0);
        ASSERT_EQ(last_attacked(false, true, true, false, false), 0u);                                       // only the thief was hit (the defenders of the hill it raids answer it): none
        ASSERT_EQ(last_attacked(true, false, false, false, false), 0u);                                      // a blast: nobody next to the ant
        ASSERT_EQ(last_attacked(true, false, true, false, true), 0u);                                        // the enemy is far
        ASSERT_EQ(last_attacked(true, false, false, true, false), 0u);                                       // only a Thief next to it
        ASSERT_EQ(last_attacked(false, false, true, false, false), 0u);                                      // nothing was hit
        // the second Combat Ant: two Combat power-ups on the map (one of the own side at (14, 10), one that no side owns in the middle), an enemy that plays
        for (const bool attacked : {false, true}) {
            for (const uint32_t extra : {0u, 1u}) {
                sim::SimulationEngine sim;
                empty_field(sim, 82);
                sim.grid_mut().place_powerup(14, 10, 4);
                sim.grid_mut().place_powerup(28, 28, 4);
                std::vector<uint32_t> mine;
                for (int i = 0; i < 6; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 9 + i / 3}));
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 36});
                LevelPlan plan = plan_for(Level::Hard);
                plan.combat_extra = extra;
                plan.steals = true;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, 31, 36));                  // an enemy that plays
                rig.run(500);
                ASSERT_EQ(count_type(sim, 0, sim::AntType::Combat), 1u);                                      // the one of the opening
                if (attacked) {                                                                               // a worker of the bot is hit by the enemy worker, which walks up to it
                    const uint32_t victim = first_of_type(sim, 0, sim::AntType::Worker);
                    for (int t = 0; t < 600 && rig.as<StandardBot>().tactics().memory.last_attacked() == 0; ++t) {
                        keep_attacking(sim, 1, enemy, victim);
                        rig.tick();
                    }
                    ASSERT_TRUE(rig.as<StandardBot>().tactics().memory.last_attacked() != 0);
                    rig.run(500);
                }
                ASSERT_EQ(count_type(sim, 0, sim::AntType::Combat), attacked && extra == 1 ? 2u : 1u);
            }
        }
    } TEST_END();

    TEST_CASE("AI11.6 The Shipped Plans: The Tactics That The Tournaments Measured As Losses Are Off In The Plan Of Every Level (The Parked Combat Ant, The Harassment Squad, The Sabotage Of Another Gate, The Ambush At A Thief Hole, The Strict Contest Order, Strikes, Hatching For Fights, The Wipe-Out Focus) And Only The Aggressive Style Of Medium (The Squad, Short) And Of Hard (The Squad, The Sabotage, The Strike) Turns Some Of Them On; What Pays Is On At Medium And Hard: The Combat Ant That Harvests And Is Taken In The Opening, The Raids, The Gate And The Theft (Hard)")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan n = plan_for(level);                                                           // the level's neutral plan
            ASSERT_FALSE(n.guards || n.harass || n.sabotage || n.ambush || n.contest_aware || n.strikes || n.hatches || n.hatch_for_squad || n.wipe_focus);
            ASSERT_EQ(n.harass_workers, 0u);
            for (uint32_t seed = 1; seed <= 12; ++seed) {
                const LevelPlan p = plan_in_match(level, Style::Random, seed, static_cast<uint8_t>(seed % 4));
                ASSERT_FALSE(p.guards || p.ambush || p.contest_aware || p.hatches || p.hatch_for_squad || p.wipe_focus);
                ASSERT_EQ(p.harass_workers, 0u);
                if (p.style != Style::Aggressive || level == Level::Easy) ASSERT_FALSE(p.harass || p.sabotage || p.strikes);
                if (p.style == Style::Aggressive && level != Level::Easy) ASSERT_TRUE(p.harass);
                if (p.style == Style::Aggressive && level == Level::Hard) ASSERT_TRUE(p.sabotage && p.strikes && p.fire_extra == 1u);
                if (p.style == Style::Aggressive && level == Level::Medium) ASSERT_FALSE(p.sabotage || p.strikes);
                if (level == Level::Easy) continue;
                ASSERT_TRUE(p.combat_harvests);
                ASSERT_TRUE(p.max_combat >= 1);
                ASSERT_TRUE(p.raids);
                ASSERT_TRUE(p.secure_side);
                ASSERT_FALSE(p.combat_when_attacked);                                                            // taken in the opening, by every style
            }
        }
        const LevelPlan hard = plan_for(Level::Hard);
        ASSERT_TRUE(hard.gate && hard.steals && hard.max_thief == 2u && hard.max_combat == 1u && hard.combat_extra == 1u);
        ASSERT_FALSE(plan_for(Level::Medium).gate);
        ASSERT_FALSE(plan_for(Level::Medium).steals);
        ASSERT_EQ(plan_for(Level::Medium).max_thief, 1u);
    } TEST_END();

    TEST_CASE("AI11.12 The Squad's Memory Of Who Answers: A Team Whose Ants Answer The Squad Three At Once (Drawn In Their Attack Clip Within 8 Tiles Of A Member) Is Left Alone For A While; A Team That Hurts The Squad Again After Its Pause Is Left Alone Twice As Long (3,000 Ticks, Then 6,000)")
    {
        const auto attacks_in = [](const std::vector<std::pair<uint64_t, Command>>& proposed, uint64_t from, uint64_t to) {      // (the only enemy in these worlds is the target of every attack order)
            size_t n = 0;
            for (const auto& e : proposed) n += e.first >= from && e.first <= to && e.second.type == CommandType::GroupAttack ? 1u : 0u;
            return n;
        };
        {   // three ants of team 1 are in their attack clip (on a decoy of the bot's, away from the carrier: they do not count against the squad's odds there), 6 tiles from the member
            for (const bool rule : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 61);
                sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
                const uint32_t decoy = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 35});
                for (const TileCoord t : {TileCoord{38, 30}, TileCoord{37, 28}, TileCoord{39, 32}}) {              // three carriers, 8 tiles away: more than a blow throws one
                    const uint32_t carrier = sim.spawn_unit(1, sim::AntType::Worker, t);
                    sim.get_unit(carrier).pick_up_food(1, 25);
                }
                std::array<uint32_t, 3> answerers{};
                for (size_t i = 0; i < 3; ++i) answerers[i] = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{static_cast<int32_t>(29 + i), 36});
                LevelPlan plan = Pinned::plan(Level::Hard, Style::Aggressive);
                ASSERT_EQ(plan.harass_strong_defence, 3u);
                plan.harass_retreat_hp = 0;                                                                        // (the retreat of a hurt member is the next block's subject)
                if (!rule) plan.harass_strong_defence = 0;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                for (int t = 0; t < 400; ++t) {
                    for (const uint32_t a : answerers) keep_attacking(sim, 1, a, decoy);
                    rig.tick();
                }
                size_t on_carrier = 0;                                                                             // the orders at the carriers (a blow throws one up to four tiles)
                for (const auto& e : rig.proposed) on_carrier += e.second.type == CommandType::GroupAttack && e.second.tile_x >= 34 && e.second.tile_y >= 26 && e.second.tile_y <= 34 ? 1u : 0u;
                const uint32_t pauses = rig.as<StandardBot>().harass().pauses();
                if (rule) {
                    ASSERT_TRUE(pauses >= 1u);
                    ASSERT_TRUE(on_carrier <= 1u);                                                                 // (the order that was out before the team showed its ants)
                } else {
                    ASSERT_EQ(pauses, 0u);
                    ASSERT_TRUE(on_carrier >= 3u);
                }
            }
        }
        {   // the pause of a team doubles every time it hurts the squad: 3,000 ticks, 6,000, ...
            sim::SimulationEngine sim;
            empty_field(sim, 61, 40000);
            const uint32_t fighter = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
            const uint32_t c1 = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 30});
            sim.get_unit(c1).pick_up_food(1, 25);
            LevelPlan plan = Pinned::plan(Level::Hard, Style::Aggressive);
            plan.harass_strong_defence = 0;
            const uint64_t pause = plan.harass_pause_ticks;
            ASSERT_EQ(pause, 3000u);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            const StandardBot& bot = rig.as<StandardBot>();
            const auto hurt = [&]() -> uint64_t {                                                                  // the member is hurt with an enemy of team 1 next to it
                sim.get_unit(c1).hp = 10;
                sim.get_unit(fighter).hp = 6;
                sim.get_unit(fighter).pos.x = sim.get_unit(c1).pos.x - 1;
                sim.get_unit(fighter).pos.y = sim.get_unit(c1).pos.y;
                rig.run(8);
                sim.get_unit(fighter).hp = 10;
                return sim.current_tick();
            };
            const auto run_near = [&](uint64_t ticks) {                                                            // the carrier of team 1 would walk home: the member is put next to it every 50 ticks, so that a target is always in reach
                for (uint64_t done = 0; done < ticks; done += 50) {
                    sim.get_unit(c1).hp = 10;
                    sim.get_unit(fighter).pos.x = sim.get_unit(c1).pos.x - 1;
                    sim.get_unit(fighter).pos.y = sim.get_unit(c1).pos.y;
                    rig.run(std::min<uint64_t>(50, ticks - done));
                }
            };
            run_near(60);
            ASSERT_TRUE(bot.harass().attacks_ordered() >= 1u);
            const uint64_t t1 = hurt();
            ASSERT_EQ(bot.harass().pauses(), 1u);
            run_near(pause - 100);
            ASSERT_EQ(attacks_in(rig.proposed, t1 + 20, sim.current_tick()), 0u);                          // left alone for the whole pause
            run_near(250);
            ASSERT_TRUE(attacks_in(rig.proposed, t1 + pause - 30, sim.current_tick()) >= 1u);             // and hunted again after it (the pause began a few ticks before the member was seen hurt: t1)
            const uint64_t t2 = hurt();
            ASSERT_EQ(bot.harass().pauses(), 2u);
            run_near(pause + 300);                                                                                 // longer than the first pause: still left alone
            ASSERT_EQ(attacks_in(rig.proposed, t2 + 20, sim.current_tick()), 0u);
            run_near(pause);                                                                                       // 2 x 3,000 ticks after the second blow: hunted again
            ASSERT_TRUE(attacks_in(rig.proposed, t2 + 2 * pause - 30, sim.current_tick()) >= 1u);
        }
    } TEST_END();
}

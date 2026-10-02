// Tests of the vocabulary of the bots: the text of --bot, the names of the seats, the profiles of the levels, the registry, and the checks that decide whether a game
// with bots may start (AI2.1, AI2.2).
#include "ai_test.hpp"

#include "ants_ai/idle_bot.hpp"
#include "ants_ai/rng.hpp"

using namespace ai_test;
using namespace ants::ai;

void run_setup_tests() {
    TEST_CASE("AI2.1 --bot SEAT[:SPEC]: Every Spelling, Every Refusal (Nothing Changes On A Refusal), The Names Of The Seats, The Profiles, The Registry") {
        BotSpec s;
        std::string why;
        ASSERT_TRUE(parse_bot_spec("2", s, why) && s.seat == 2 && s.kind == "standard" && s.level == Level::Medium && why.empty());
        ASSERT_TRUE(parse_bot_spec("0:hard", s, why) && s.seat == 0 && s.kind == "standard" && s.level == Level::Hard);
        ASSERT_TRUE(parse_bot_spec("3:easy", s, why) && s.seat == 3 && s.kind == "standard" && s.level == Level::Easy);
        ASSERT_TRUE(parse_bot_spec("1:idle", s, why) && s.seat == 1 && s.kind == "idle" && s.level == Level::Medium);
        ASSERT_TRUE(parse_bot_spec("1:worker", s, why) && s.kind == "worker" && s.level == Level::Medium);
        ASSERT_TRUE(parse_bot_spec("1:standard", s, why) && s.kind == "standard" && s.level == Level::Medium);
        ASSERT_TRUE(parse_bot_spec("2:worker:easy", s, why) && s.seat == 2 && s.kind == "worker" && s.level == Level::Easy);
        ASSERT_TRUE(parse_bot_spec("2:idle:hard", s, why) && s.kind == "idle" && s.level == Level::Hard);
        ASSERT_TRUE(parse_bot_spec("3:STANDARD:Hard", s, why) && s.seat == 3 && s.kind == "standard" && s.level == Level::Hard);       // either case
        ASSERT_TRUE(parse_bot_spec("1:MEDIUM", s, why) && s.kind == "standard" && s.level == Level::Medium);
        // what is refused: the spec stays as it was and the reason is given
        BotSpec keep;
        keep.seat = 2;
        keep.kind = "idle";
        keep.level = Level::Hard;
        const char* const bad[] = {"", ":", "4", "7", "-1", "a", "12", "01", " 1", "1 ", "+1", "1:", "1::", "1:fast", "1:idle:fast", "1:fast:easy", "1:idle:easy:x", "1:easy:easy",
                                   "1:hard:idle", "1:idle:", "1:idle:Medium2", "one:hard", "2:\x01", "2:caf\xC3\xA9"};
        for (const char* text : bad) {
            BotSpec t = keep;
            std::string reason;
            ASSERT_FALSE(parse_bot_spec(text, t, reason));
            ASSERT_FALSE(reason.empty());
            ASSERT_EQ(reason.size() < 200u, true);
            ASSERT_TRUE(t.seat == 2 && t.kind == "idle" && t.level == Level::Hard);
            for (const char c : reason) ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);       // printable ASCII: it goes to a terminal and a status line
        }
        {   // a long word is shortened in the message
            BotSpec t;
            std::string reason;
            ASSERT_FALSE(parse_bot_spec("1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", t, reason));
            ASSERT_TRUE(reason.size() < 120u && reason.find("...") != std::string::npos);
        }
        // level words
        for (const Level l : {Level::Easy, Level::Medium, Level::Hard}) {
            Level back = Level::Easy;
            ASSERT_TRUE(parse_level(level_name(l), back) && back == l);
        }
        Level untouched = Level::Hard;
        ASSERT_FALSE(parse_level("", untouched) || parse_level("mediumm", untouched) || parse_level("idle", untouched));
        ASSERT_EQ(static_cast<int>(untouched), static_cast<int>(Level::Hard));
        // the names: printable ASCII, at most 32 characters, always "Bot (": the lobby refuses a person that name
        BotSpec n;
        n.kind = "standard";
        for (const Level l : {Level::Easy, Level::Medium, Level::Hard}) {
            n.level = l;
            const std::string name = bot_display_name(n);
            ASSERT_TRUE(name.rfind("Bot (", 0) == 0 && name.back() == ')' && name.size() <= 32u);
        }
        n.level = Level::Medium;
        ASSERT_EQ(bot_display_name(n), "Bot (Medium)");
        n.level = Level::Hard;
        ASSERT_EQ(bot_display_name(n), "Bot (Hard)");
        n.level = Level::Easy;
        ASSERT_EQ(bot_display_name(n), "Bot (Easy)");
        n.kind = "idle";
        ASSERT_EQ(bot_display_name(n), "Bot (Idle)");
        n.kind = "worker";
        ASSERT_EQ(bot_display_name(n), "Bot (Worker)");
        // the profiles: the levels are ordered by how fast and how much they act, and no profile may break the rules of the controller
        const Profile e = profile_for(Level::Easy);
        const Profile m = profile_for(Level::Medium);
        const Profile h = profile_for(Level::Hard);
        ASSERT_TRUE(e.decision_interval > m.decision_interval && m.decision_interval > h.decision_interval);
        ASSERT_TRUE(e.reaction_delay > m.reaction_delay && m.reaction_delay > h.reaction_delay);
        ASSERT_TRUE(e.rate_milli_cps < m.rate_milli_cps && m.rate_milli_cps < h.rate_milli_cps);
        ASSERT_TRUE(e.burst < m.burst && m.burst < h.burst);
        for (const Profile& p : {e, m, h}) {
            ASSERT_TRUE(p.burst >= 1 && p.decision_interval >= 1 && p.jitter_percent <= 100 && p.reissue_cooldown >= 1);
            ASSERT_TRUE(p.max_ants_per_command >= 1 && p.max_ants_per_command <= kHudAntCap);
        }
        ASSERT_EQ(kHudAntCap, 24u);                                              // the original's drag selection
        ASSERT_EQ(h.rate_milli_cps, 3000u);
        ASSERT_EQ(m.rate_milli_cps, 1500u);
        ASSERT_EQ(e.rate_milli_cps, 400u);
        // the registry: idle, worker and standard exist ("standard" is an alias of the worker bot of B3 until the standard bot of B4 exists), anything else does not
        for (const char* kind : {"idle", "worker", "standard"}) {
            BotSpec k;
            k.kind = kind;
            ASSERT_TRUE(known_bot_kind(kind));
            std::unique_ptr<Bot> bot = make_bot(k);
            ASSERT_TRUE(bot != nullptr);
            ASSERT_EQ(std::string(bot->kind()), std::string(kind) == "idle" ? "idle" : "worker");
        }
        BotSpec unknown;
        unknown.kind = "genius";
        ASSERT_FALSE(known_bot_kind("genius") || known_bot_kind("") || known_bot_kind("Idle"));
        ASSERT_TRUE(make_bot(unknown) == nullptr);
        // the generator of a seat: splitmix64, the first outputs of seed 0 are the published ones; same (match seed, seat, kind) gives the same seed, any difference another
        BotRng zero(0);
        ASSERT_TRUE(zero.next() == 0xE220A8397B1DCDAFull && zero.next() == 0x6E789E6AA1B965F4ull && zero.next() == 0x06C45D188009454Full);
        ASSERT_TRUE(seat_seed(5, 1, "idle") == seat_seed(5, 1, "idle"));
        ASSERT_TRUE(seat_seed(5, 1, "idle") != seat_seed(5, 2, "idle") && seat_seed(5, 1, "idle") != seat_seed(6, 1, "idle") && seat_seed(5, 1, "idle") != seat_seed(5, 1, "worker"));
        BotRng r(seat_seed(1, 0, "idle"));
        uint32_t seen[7] = {};
        for (int i = 0; i < 700; ++i) ++seen[r.below(7)];
        for (const uint32_t count : seen) ASSERT_TRUE(count > 40);               // below(n) covers 0 .. n - 1
        ASSERT_EQ(r.below(0), 0u);
        ASSERT_EQ(r.below(1), 0u);
    } TEST_END();

    TEST_CASE("AI2.2 check_setup: A Game With Bots Is Refused For Fog Of War, A Seat That Is Not In The Game, A Person's Seat, Two Bots On A Seat, An Unknown Kind, No Person At All") {
        const auto bot = [](uint8_t seat, const char* kind = "standard", Level level = Level::Medium) {
            BotSpec b;
            b.seat = seat;
            b.kind = kind;
            b.level = level;
            return b;
        };
        SetupInfo ok;
        ok.roster = 0x07;
        ok.human_mask = 0x01;
        ok.bots = {bot(1), bot(2, "idle", Level::Hard)};
        ASSERT_EQ(check_setup(ok), "");
        {   // no bots: nothing to refuse, whatever else is set
            SetupInfo none;
            none.roster = 0x01;
            none.human_mask = 0x01;
            none.fog = true;
            ASSERT_EQ(check_setup(none), "");
        }
        {   // fog: a bot would see through it
            SetupInfo fog = ok;
            fog.fog = true;
            const std::string why = check_setup(fog);
            ASSERT_TRUE(why.find("Fog of War") != std::string::npos);
        }
        {   // a seat that is not in the game: outside the roster, or not a seat at all
            SetupInfo out = ok;
            out.bots = {bot(3)};
            ASSERT_TRUE(check_setup(out).find("seat 3") != std::string::npos);
            out.bots = {bot(9)};
            ASSERT_FALSE(check_setup(out).empty());
        }
        {   // a person's seat
            SetupInfo clash = ok;
            clash.bots = {bot(0)};
            ASSERT_TRUE(check_setup(clash).find("a person is there") != std::string::npos);
            clash.human_mask = 0x03;                                             // two persons, one bot on a free seat is fine
            clash.bots = {bot(2)};
            ASSERT_EQ(check_setup(clash), "");
        }
        {   // two bots on one seat
            SetupInfo twice = ok;
            twice.bots = {bot(1), bot(1, "idle")};
            ASSERT_TRUE(check_setup(twice).find("two bots") != std::string::npos);
        }
        {   // a kind that does not exist
            SetupInfo kind = ok;
            kind.bots = {bot(1, "genius")};
            ASSERT_TRUE(check_setup(kind).find("genius") != std::string::npos);
        }
        {   // nobody is a person: only the arena plays that
            SetupInfo all = ok;
            all.human_mask = 0;
            all.roster = 0x06;
            all.bots = {bot(1), bot(2)};
            ASSERT_TRUE(check_setup(all).find("at least one person") != std::string::npos);
            all.allow_all_bots = true;
            ASSERT_EQ(check_setup(all), "");
            all.fog = true;                                                      // (fog is refused for the arena too)
            ASSERT_FALSE(check_setup(all).empty());
        }
        {   // a person that is not in the roster does not count as a person
            SetupInfo gone = ok;
            gone.human_mask = 0x08;
            ASSERT_FALSE(check_setup(gone).empty());
        }
    } TEST_END();
}

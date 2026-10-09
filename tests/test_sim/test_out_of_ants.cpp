// Tests of the line "<Name> (<Colour>) is out of ants!" of the game mode 187 (include/ants_sim/game_mode.hpp, docs/GAMEPLAY.md "187"): when a team of the match has no ant, no egg and no
// hatch left, every player reads it once as a News Flash of the chat log and hears the cue of a player that drops out; a team that dropped out has its own line and gets none, a team that is
// not in the match gets none, and a match of the original's rules says and plays nothing of it. The news and the cues are presentation: they stay out of the state hash, and an engine copy
// carries what has been said.
//   1.x  the line: its words, its moment (the tick the last ant is gone), once, to everybody, with the cue
//   2.x  the eggs are lives; a team that dropped out; a team that is not in the match; a match that ends with it
//   3.x  the original's rules say nothing; the engine copy and the state hash; two machines say the same at the same ticks
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "ants_test_paths.hpp"

using namespace ants::sim;
using ants::assets::LevelData;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace {

constexpr uint16_t kLine = strings::kOutOfAnts;

LevelData load_map(const std::string& name) {
    LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL")) std::cout << "  (cannot load " << name << ")\n";
    return level;
}

// What an engine said so far (its two queues, emptied after every tick)
struct Heard {
    std::vector<NewsEvent> news;
    std::vector<AudioEvent> audio;
    void take(SimulationEngine& e) {
        for (NewsEvent& n : e.poll_news_events()) news.push_back(std::move(n));
        for (AudioEvent& a : e.poll_audio_events()) audio.push_back(std::move(a));
    }
    std::vector<NewsEvent> lines(uint16_t string_id) const {
        std::vector<NewsEvent> out;
        for (const NewsEvent& n : news) {
            if (n.string_id == string_id) out.push_back(n);
        }
        return out;
    }
    size_t cues(uint32_t sound_id) const {
        size_t n = 0;
        for (const AudioEvent& a : audio) n += a.sound_id == sound_id ? 1u : 0u;
        return n;
    }
};

size_t ants_in_world(const SimulationEngine& e, uint8_t team) {
    size_t n = 0;
    for (const AntSnapshot& a : e.get_world_state().ants) n += a.player_id == team ? 1u : 0u;      // (the world state leaves out the ants that are removed)
    return n;
}

// A 40 x 40 test map with the given rules and `per_team` combat ants for each of the four teams in a row, no eggs for anybody; the teams are named so that the lines can be told apart
SimulationEngine world(GameMode mode, uint32_t per_team = 2) {
    SimulationEngine e;
    e.set_game_mode(mode);
    e.init_test_world(40, 40, 11, 720000);
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) e.set_player_eggs(p, 0);
    for (uint8_t t = 0; t < MAX_PLAYERS; ++t) {
        for (uint32_t i = 0; i < per_team; ++i) e.spawn_unit(t, AntType::Combat, TileCoord{4 + 8 * static_cast<int32_t>(t) + 2 * static_cast<int32_t>(i), 6});
    }
    e.set_player_name(0, "Juniper");
    e.set_player_name(1, "Odile");
    e.set_player_name(2, "Marta");
    e.set_player_name(3, "Theo");
    return e;
}

std::vector<uint32_t> ids_of(const SimulationEngine& e, uint8_t team) {
    std::vector<uint32_t> ids;
    for (const AntSnapshot& a : e.get_world_state().ants) {
        if (a.player_id == team) ids.push_back(a.id);
    }
    return ids;
}

void kill_team(SimulationEngine& e, uint8_t team) {
    for (uint32_t id : ids_of(e, team)) e.kill_unit(id);
}

// an ant of `killer_team` stands next to the ant `victim` (one hit point left) and strikes it: the victim is thrown and dies on its clip, ticks later (the contact needs the two to touch)
void melee_kill(SimulationEngine& e, uint8_t killer_team, uint32_t victim) {
    AntUnit& v = e.get_unit(victim);
    v.hp = 1;
    const uint32_t killer = e.spawn_unit(killer_team, AntType::Combat, TileCoord{v.pos.x + 1, v.pos.y});
    e.execute_melee_attack(killer, victim);
}

// ticks the engine and collects what it said; stops when the match is over
void run(SimulationEngine& e, Heard& heard, int ticks) {
    for (int i = 0; i < ticks && !e.is_match_over(); ++i) {
        e.tick();
        heard.take(e);
    }
}

}  // namespace

int main() {
    std::cout << "=== Out of ants (187) ===\n";

    TEST_CASE("1.1 the words: \"Name (Colour) is out of ants!\", a News Flash of the chat log for every player, with the match time of the moment, once, and the cue of a player that drops out") {
        SimulationEngine e = world(GameMode::Kills187);
        Heard heard;
        run(e, heard, 40);
        ASSERT_EQ(heard.lines(kLine).size(), 0u);                                  // everybody has ants: nothing is said
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 0u);
        kill_team(e, 1);
        run(e, heard, 200);
        const std::vector<NewsEvent> lines = heard.lines(kLine);
        ASSERT_EQ(lines.size(), 1u);
        ASSERT_EQ(lines[0].message_text, std::string("Odile (Red) is out of ants!"));
        ASSERT_EQ(lines[0].target_player, 255);
        ASSERT_TRUE(lines[0].channel == NewsChannel::ChatLog);
        ASSERT_FALSE(lines[0].blink);
        ASSERT_TRUE(lines[0].timestamp_ms >= 40u * 50u && lines[0].timestamp_ms <= 240u * 50u);      // "[m:ss]" is the time played when the last ant was gone
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 1u);
        ASSERT_EQ(heard.audio.back().target_player, 255);                           // heard by every player
        ASSERT_EQ(heard.audio.back().sound_id, SoundID::PlayerDropOut);
        run(e, heard, 400);
        ASSERT_EQ(heard.lines(kLine).size(), 1u);                                  // never again
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 1u);
    } TEST_END();

    TEST_CASE("1.2 each team has its own line with its own name and colour (green, red, blue, black), and without a name the colour word stands in both places, as in the other lines of the game") {
        SimulationEngine e = world(GameMode::Kills187, 1);
        Heard heard;
        kill_team(e, 0);
        run(e, heard, 200);
        kill_team(e, 2);
        run(e, heard, 200);
        ASSERT_FALSE(e.is_match_over());                                          // team 1 and team 3 are two sides: the match goes on
        const std::vector<NewsEvent> lines = heard.lines(kLine);
        ASSERT_EQ(lines.size(), 2u);
        ASSERT_EQ(lines[0].message_text, std::string("Juniper (Green) is out of ants!"));
        ASSERT_EQ(lines[1].message_text, std::string("Marta (Blue) is out of ants!"));
        SimulationEngine bare;
        bare.set_game_mode(GameMode::Kills187);
        bare.init_test_world(40, 40, 11, 720000);
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) bare.set_player_eggs(p, 0);
        for (uint8_t t = 0; t < MAX_PLAYERS; ++t) bare.spawn_unit(t, AntType::Combat, TileCoord{4 + 8 * static_cast<int32_t>(t), 6});
        Heard bare_heard;
        kill_team(bare, 3);
        run(bare, bare_heard, 200);
        const std::vector<NewsEvent> bare_lines = bare_heard.lines(kLine);
        ASSERT_EQ(bare_lines.size(), 1u);
        ASSERT_EQ(bare_lines[0].message_text, std::string("Black (Black) is out of ants!"));
    } TEST_END();

    TEST_CASE("1.3 the moment: the line is said when the last ant of the team is gone, at the next tick and not before (while it dies it is still an ant), the team's death a real fight's") {
        SimulationEngine e = world(GameMode::Kills187, 1);
        Heard heard;
        run(e, heard, 10);
        melee_kill(e, 2, ids_of(e, 1)[0]);
        bool gone_before = false;                                                  // no ant of the team was left at the end of the tick before
        bool seen_dying = false;
        bool seen_gone = false;
        for (int t = 0; t < 200; ++t) {
            e.tick();
            heard.take(e);
            ASSERT_EQ(heard.lines(kLine).size() == 1u, gone_before);               // said at the tick after the last ant has left the world, never earlier, never twice
            const bool gone = ants_in_world(e, 1) == 0u;
            seen_dying = seen_dying || (!gone && !ids_of(e, 1).empty() && e.get_unit(ids_of(e, 1)[0]).hp == 0);
            seen_gone = seen_gone || gone;
            gone_before = gone;
        }
        ASSERT_TRUE(seen_dying);                                                   // (the check saw the ant fly and die: its clips take a while)
        ASSERT_TRUE(seen_gone);
        ASSERT_EQ(heard.lines(kLine).size(), 1u);
        ASSERT_EQ(heard.lines(kLine)[0].message_text, std::string("Odile (Red) is out of ants!"));
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 1u);
    } TEST_END();

    TEST_CASE("2.1 an egg is a life: a team whose last ant dies hatches one for free and is not out (not while the egg hatches, not when the newborn stands); it is out when that ant is gone too") {
        SimulationEngine e;
        e.set_game_mode(GameMode::Kills187);
        e.init(load_map("TINY"), 4u);                                              // a real map: its hills are where the newborn appears
        e.set_player_name(1, "Odile");
        e.set_player_eggs(1, 1);
        Heard heard;
        run(e, heard, 10);
        kill_team(e, 1);
        run(e, heard, 100);
        ASSERT_EQ(e.get_player_eggs(1), 0u);                                       // the egg is hatching: no ant in the world, nothing said
        ASSERT_EQ(ants_in_world(e, 1), 0u);
        ASSERT_EQ(heard.lines(kLine).size(), 0u);
        run(e, heard, 300);                                                        // the hatch takes 8 seconds
        ASSERT_EQ(ants_in_world(e, 1), 1u);
        ASSERT_EQ(heard.lines(kLine).size(), 0u);
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 0u);
        kill_team(e, 1);
        run(e, heard, 100);
        ASSERT_EQ(heard.lines(kLine).size(), 1u);
        ASSERT_EQ(heard.lines(kLine)[0].message_text, std::string("Odile (Red) is out of ants!"));
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 1u);
    } TEST_END();

    TEST_CASE("2.2 a team that dropped out of the match has the line \"dropped out of the game!\" and gets no second one, not when its ants are gone either") {
        SimulationEngine e = world(GameMode::Kills187, 2);
        Heard heard;
        run(e, heard, 10);
        e.drop_player(1);
        run(e, heard, 400);                                                        // its ants die and are removed
        ASSERT_EQ(ants_in_world(e, 1), 0u);
        ASSERT_EQ(heard.lines(kLine).size(), 0u);
        size_t dropped = 0;
        for (const NewsEvent& n : heard.news) dropped += n.string_id == strings::kDroppedOut ? 1u : 0u;
        ASSERT_EQ(dropped, 1u);
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 1u);                         // the drop-out's own cue, once
        kill_team(e, 2);
        run(e, heard, 200);                                                        // another team that is out is still said
        ASSERT_EQ(heard.lines(kLine).size(), 1u);
        ASSERT_EQ(heard.lines(kLine)[0].message_text, std::string("Marta (Blue) is out of ants!"));
    } TEST_END();

    TEST_CASE("2.3 a team that is not in the match gets none: a roster of two teams on a shipped map, and a team that is out in the same match is said once") {
        SimulationEngine e;
        e.set_game_mode(GameMode::Kills187);
        e.init(load_map("TINY"), 4u, 0x05);                                        // teams 0 and 2
        e.set_player_name(0, "Juniper");
        e.set_player_name(2, "Marta");
        Heard heard;
        run(e, heard, 300);
        ASSERT_EQ(heard.lines(kLine).size(), 0u);                                  // teams 1 and 3 have no ants and no eggs: they are not in the match
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 0u);
        e.set_player_eggs(2, 0);
        kill_team(e, 2);
        run(e, heard, 400);
        ASSERT_EQ(heard.lines(kLine).size(), 1u);
        ASSERT_EQ(heard.lines(kLine)[0].message_text, std::string("Marta (Blue) is out of ants!"));
        ASSERT_TRUE(e.is_match_over());                                            // the last side standing ends the match
    } TEST_END();

    TEST_CASE("2.4 the match ends with it: every team that fell is said once, the last one before the end of the match, and nothing is said after the end") {
        SimulationEngine e = world(GameMode::Kills187, 1);
        Heard heard;
        kill_team(e, 1);
        kill_team(e, 2);
        kill_team(e, 3);
        run(e, heard, 400);
        ASSERT_TRUE(e.is_match_over());
        ASSERT_EQ(e.get_world_state().match_result.standing, 0);
        ASSERT_EQ(heard.lines(kLine).size(), 3u);
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 3u);
        const size_t lines = heard.lines(kLine).size();
        for (int i = 0; i < 100; ++i) {
            e.tick();                                                              // a match that is over does not tick
            heard.take(e);
        }
        ASSERT_EQ(heard.lines(kLine).size(), lines);
    } TEST_END();

    TEST_CASE("3.1 the original's rules say nothing and play nothing of it: the same fall of a team, no line, no cue, and the same ids as before in its news") {
        SimulationEngine e = world(GameMode::HighestScore);
        Heard heard;
        run(e, heard, 40);
        kill_team(e, 1);
        run(e, heard, 400);
        ASSERT_EQ(ants_in_world(e, 1), 0u);
        ASSERT_EQ(heard.lines(kLine).size(), 0u);
        ASSERT_EQ(heard.cues(SoundID::PlayerDropOut), 0u);
        for (const NewsEvent& n : heard.news) ASSERT_TRUE(n.string_id != kLine);
        SimulationEngine fresh;
        fresh.init_test_world(40, 40, 11, 720000);                                // an engine that never heard of a mode
        ASSERT_TRUE(fresh.game_mode() == GameMode::HighestScore);
    } TEST_END();

    TEST_CASE("3.2 the line is presentation, not state: one engine's queues are emptied every tick and the other's never, and the two hash alike at every tick") {
        SimulationEngine polled = world(GameMode::Kills187);
        SimulationEngine silent = world(GameMode::Kills187);
        Heard heard;
        kill_team(polled, 1);
        kill_team(silent, 1);
        for (int t = 0; t < 300; ++t) {
            polled.tick();
            heard.take(polled);                                                    // one engine's queues are emptied every tick, the other's never
            silent.tick();
            ASSERT_TRUE(polled.state_hash() == silent.state_hash());
        }
        ASSERT_EQ(heard.lines(kLine).size(), 1u);
        const std::vector<NewsEvent> queued = silent.poll_news_events();           // the other engine kept it all, the line among it
        size_t said = 0;
        for (const NewsEvent& n : queued) said += n.string_id == kLine ? 1u : 0u;
        ASSERT_EQ(said, 1u);
    } TEST_END();

    TEST_CASE("3.3 an engine copy carries what has been said: a copy made before the line says it once, a copy made after it says nothing, and the three hash alike at every tick") {
        SimulationEngine a = world(GameMode::Kills187, 1);
        Heard heard_a;
        run(a, heard_a, 10);
        SimulationEngine before(a);                                                // taken before the team is out
        Heard heard_before;
        melee_kill(a, 2, ids_of(a, 1)[0]);
        melee_kill(before, 2, ids_of(before, 1)[0]);
        run(a, heard_a, 3);
        run(before, heard_before, 3);
        ASSERT_EQ(heard_a.lines(kLine).size(), 0u);                                // (the ant is still flying: the team is not out)
        bool copied = false;
        SimulationEngine after;
        Heard heard_after;
        for (int t = 0; t < 300; ++t) {
            a.tick();
            heard_a.take(a);
            before.tick();
            heard_before.take(before);
            if (copied) {
                after.tick();
                heard_after.take(after);
            }
            if (!copied && heard_a.lines(kLine).size() == 1u) {                    // the line has just been said: copy the engine now
                after = a;
                copied = true;
            }
            ASSERT_TRUE(a.state_hash() == before.state_hash());
            if (copied) ASSERT_TRUE(a.state_hash() == after.state_hash());
        }
        ASSERT_TRUE(copied);
        ASSERT_EQ(heard_a.lines(kLine).size(), 1u);
        ASSERT_EQ(heard_before.lines(kLine).size(), 1u);                           // (the copy taken before says it once, at the same tick)
        ASSERT_EQ(heard_after.lines(kLine).size(), 0u);                            // (the copy taken after it does not say it again)
        ASSERT_EQ(heard_before.lines(kLine)[0].timestamp_ms, heard_a.lines(kLine)[0].timestamp_ms);
        ASSERT_EQ(heard_before.lines(kLine)[0].message_text, heard_a.lines(kLine)[0].message_text);
        SimulationEngine assigned = world(GameMode::HighestScore);
        assigned = a;                                                              // the assignment too
        ASSERT_TRUE(assigned.game_mode() == GameMode::Kills187);
        Heard heard_assigned;
        run(assigned, heard_assigned, 100);
        ASSERT_EQ(heard_assigned.lines(kLine).size(), 0u);
    } TEST_END();

    TEST_CASE("3.4 two machines say the same at the same ticks: two engines of the same match give the same lines and cues, tick by tick, with the same hashes (a real map, a fight)") {
        const LevelData tiny = load_map("TINY");
        SimulationEngine a;
        SimulationEngine b;
        for (SimulationEngine* e : {&a, &b}) {
            e->set_game_mode(GameMode::Kills187);
            e->init(tiny, 21u);
            e->set_player_name(0, "Juniper");
            e->set_player_name(1, "Odile");
            e->set_player_name(2, "Marta");
            e->set_player_name(3, "Theo");
            for (uint8_t t = 1; t < MAX_PLAYERS; ++t) e->set_player_eggs(t, 0);
        }
        Heard heard_a;
        Heard heard_b;
        for (int t = 0; t < 600; ++t) {
            if (t == 100) {
                for (SimulationEngine* e : {&a, &b}) kill_team(*e, 1);
            }
            if (t == 250) {
                for (SimulationEngine* e : {&a, &b}) kill_team(*e, 3);
            }
            a.tick();
            b.tick();
            heard_a.take(a);
            heard_b.take(b);
            ASSERT_TRUE(a.state_hash() == b.state_hash());
            ASSERT_EQ(heard_a.news.size(), heard_b.news.size());
            ASSERT_EQ(heard_a.audio.size(), heard_b.audio.size());
        }
        ASSERT_EQ(heard_a.lines(kLine).size(), 2u);
        ASSERT_EQ(heard_a.lines(kLine)[0].message_text, std::string("Odile (Red) is out of ants!"));
        ASSERT_EQ(heard_a.lines(kLine)[1].message_text, std::string("Theo (Black) is out of ants!"));
        for (size_t i = 0; i < heard_a.news.size(); ++i) {
            ASSERT_EQ(heard_a.news[i].string_id, heard_b.news[i].string_id);
            ASSERT_EQ(heard_a.news[i].timestamp_ms, heard_b.news[i].timestamp_ms);
            ASSERT_EQ(heard_a.news[i].message_text, heard_b.news[i].message_text);
        }
        ASSERT_EQ(heard_a.cues(SoundID::PlayerDropOut), 2u);
    } TEST_END();

    TEST_CASE("3.5 a new match of the same engine says it afresh: init forgets who has been said, and the mode stays") {
        SimulationEngine e = world(GameMode::Kills187, 1);
        Heard heard;
        kill_team(e, 2);
        run(e, heard, 200);
        ASSERT_EQ(heard.lines(kLine).size(), 1u);
        e.init_test_world(40, 40, 11, 720000);
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) e.set_player_eggs(p, 0);
        for (uint8_t t = 0; t < MAX_PLAYERS; ++t) e.spawn_unit(t, AntType::Combat, TileCoord{4 + 8 * static_cast<int32_t>(t), 6});
        e.poll_news_events();
        e.poll_audio_events();
        Heard second;
        kill_team(e, 2);
        run(e, second, 200);
        ASSERT_TRUE(e.game_mode() == GameMode::Kills187);
        ASSERT_EQ(second.lines(kLine).size(), 1u);
        e.init(load_map("TINY"), 5u);                                              // and a real map's init
        e.poll_news_events();
        e.poll_audio_events();
        Heard third;
        e.set_player_eggs(2, 0);
        kill_team(e, 2);
        run(e, third, 100);
        ASSERT_EQ(third.lines(kLine).size(), 1u);
    } TEST_END();

    std::cout << "\n" << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    return g_test_failures == 0 ? 0 : 1;
}

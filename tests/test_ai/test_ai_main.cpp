// The bot suite (docs/BOTS.md): the vocabulary, the controller, the bot seat in a room, what a bot sees, the analysis of the map and the match runner. One program;
// run_tests.sh runs it as suites 2.20 and 2.20.1, ctest as test_ai and test_ai_part2.
//
// `test_ai` runs every case. `test_ai --part 1` runs the groups up to and including the reach groups (about 42 percent of the CPU time of a run), `test_ai --part 2` the groups from the bombs' on: the two halves
// run at the same time, so that the longest test of a CI leg is a good deal shorter (the Windows legs cut a test off at 900 seconds, and the whole suite took 860 to 880 on a slow runner). A new group goes
// where it keeps the halves even; with no --part, ANTS_TEST_FILTER selects cases of either half as before.
#include <cstdlib>
#include <cstring>

#include "ai_test.hpp"

int main(int argc, char** argv) {
    int part = 0;                                                                                // 0: every group
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--part") == 0 && i + 1 < argc && (std::strcmp(argv[i + 1], "1") == 0 || std::strcmp(argv[i + 1], "2") == 0)) {
            part = std::atoi(argv[++i]);
        } else {
            std::cout << "usage: test_ai [--part 1|2]\n";
            return 2;
        }
    }
    std::cout << "\n=======================================================\n [SUITE] Computer players (ants_ai: controller, idle bot, bot seats in rooms, the view, the map analysis, the arena)\n"
                 "=======================================================\n";
    if (part != 2) {
        run_setup_tests();
        run_controller_tests();
        run_net_tests();
        run_view_tests();
        run_map_tests();
        run_arena_tests();
        run_b2fix_tests();
        run_b41_tests();
        run_b41_team_tests();
        run_b41_fight_tests();
        run_b41_gate_tests();
        run_b41_style_tests();
        run_b41_offence_tests();
        run_b41_cost_tests();
        run_race_tests();
        run_deny_tests();
        run_contest_tests();
        run_island_tests();
        run_island_task_tests();
        run_island_expedition_tests();
        run_island_ferry_tests();
        run_ring_mines_tests();
        run_flower_link_tests();
        run_island_bridges_tests();
        run_island_small_tests();
        run_token_group_tests();
        run_power_up_cap_tests();
        run_merge_tests();
        run_cantgo_tests();
        run_reach_tests();
    }
    if (part != 1) {
        run_bombs_tests();
        run_flower_tests();
        run_war_tests();
        run_stand_tests();
        run_name_tests();
        run_gate_tests();
        run_gate_goal_tests();
        run_field_tests();
        run_brawl_tests();
        run_kill_tests();
        run_people_tests();
        run_defence_tests();
        run_heal_tests();
        run_gang2_tests();
        run_group_tests();
        run_hit_tests();
        run_homemines_tests();
        run_ownground_tests();
        run_drops_tests();
        run_thief_tests();
        run_gone_tests();
        run_unstick_tests();
        run_idle_tests();
        run_powerstand_tests();
        run_fightplan_tests();
        run_strip_tests();
    }
    if (ai_test::g_test_count == 0) {
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite (a misspelt or forgotten filter must not turn the suite green)\n";
        ++ai_test::g_test_failures;
    }
    std::cout << "\n=======================================================\n Total Test Cases: " << ai_test::g_test_count << "\n Total Assertions: " << ai_test::g_assert_count
              << "\n Failed:           " << ai_test::g_test_failures << "\n=======================================================\n";
    return ai_test::g_test_failures == 0 ? 0 : 1;
}

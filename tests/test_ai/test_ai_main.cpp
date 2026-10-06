// The bot suite (docs/BOTS.md): the vocabulary, the controller, the bot seat in a room, what a bot sees, the analysis of the map and the match runner. One program;
// run_tests.sh runs it as suite 2.20.
#include "ai_test.hpp"

int main() {
    std::cout << "\n=======================================================\n [SUITE] Computer players (ants_ai: controller, idle bot, bot seats in rooms, the view, the map analysis, the arena)\n"
                 "=======================================================\n";
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
    run_contest_tests();
    run_island_tests();
    run_island_task_tests();
    run_island_expedition_tests();
    run_island_ferry_tests();
    run_island_small_tests();
    run_merge_tests();
    run_cantgo_tests();
    if (ai_test::g_test_count == 0) {
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite (a misspelt or forgotten filter must not turn the suite green)\n";
        ++ai_test::g_test_failures;
    }
    std::cout << "\n=======================================================\n Total Test Cases: " << ai_test::g_test_count << "\n Total Assertions: " << ai_test::g_assert_count
              << "\n Failed:           " << ai_test::g_test_failures << "\n=======================================================\n";
    return ai_test::g_test_failures == 0 ? 0 : 1;
}

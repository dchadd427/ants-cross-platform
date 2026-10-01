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
    std::cout << "\n=======================================================\n Total Test Cases: " << ai_test::g_test_count << "\n Total Assertions: " << ai_test::g_assert_count
              << "\n Failed:           " << ai_test::g_test_failures << "\n=======================================================\n";
    return ai_test::g_test_failures == 0 ? 0 : 1;
}

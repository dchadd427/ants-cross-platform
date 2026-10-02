// The worker bot suite (docs/BOTS.md, B3): the economy of the computer players on every shipped map, the pinned numbers, the task model. One program; run_tests.sh runs it as suite 2.22.
#include "ai_test.hpp"

void run_worker_tests();

int main() {
    std::cout << "\n=======================================================\n [SUITE] Worker bot (ants_ai: harvest, learning, endgame, levels, budget, pinned baselines)\n"
                 "=======================================================\n";
    run_worker_tests();
    if (ai_test::g_test_count == 0) {
        std::cout << "\n no test ran: the filter W_ONLY matches no test of this suite (a misspelt or forgotten W_ONLY must not turn the suite green)\n";
        ++ai_test::g_test_failures;
    }
    std::cout << "\n=======================================================\n Total Test Cases: " << ai_test::g_test_count << "\n Total Assertions: " << ai_test::g_assert_count
              << "\n Failed:           " << ai_test::g_test_failures << "\n=======================================================\n";
    return ai_test::g_test_failures == 0 ? 0 : 1;
}

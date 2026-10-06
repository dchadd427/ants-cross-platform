// The island matches (docs/BOTS.md, "Islands"): whole matches of four standard bots on ISLANDS and SMALL at every level, the bots as they play. One program; run_tests.sh runs it as suite 2.28.
#include "ai_test.hpp"

void run_island_match_tests();

int main() {
    std::cout << "\n=======================================================\n [SUITE] Island matches (ants_ai: the expedition, the bridges and the ferry in whole matches of four bots)\n"
                 "=======================================================\n";
    run_island_match_tests();
    if (ai_test::g_test_count == 0) {
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite (a misspelt or forgotten filter must not turn the suite green)\n";
        ++ai_test::g_test_failures;
    }
    std::cout << "\n=======================================================\n Total Test Cases: " << ai_test::g_test_count << "\n Total Assertions: " << ai_test::g_assert_count
              << "\n Failed:           " << ai_test::g_test_failures << "\n=======================================================\n";
    return ai_test::g_test_failures == 0 ? 0 : 1;
}

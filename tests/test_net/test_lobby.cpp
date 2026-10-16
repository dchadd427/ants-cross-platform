// Tests of the room before a match: the lobby messages, joining and refusing, leaving, the start barrier (Start, Loaded, Begin), cancelled
// starts, kicking, hostile guests, a whole match started through the lobby, and the rematch of a lobby room.
#include "lobby_fixtures.hpp"

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: room, joining and the start barrier\n"
                 "=======================================================\n";

    run_lobby_basics_tests();
    run_lobby_rejoin_tests();
    run_lobby_rooms_tests();
    run_lobby_rematch_tests();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

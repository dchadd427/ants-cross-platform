// Tests of the dedicated game server (docs/NETWORK_PORT.md): the map store, the room manager and its door (Hello routing), a room from the first Hello to the result
// with a host that has no seat, the control interface's calls, and one run over real sockets. The cases are in the test_server_*.cpp files; this file runs them in their order.
#include "server_fixtures.hpp"

int main() {
    std::cout << "=======================================================\n";
    std::cout << " Dedicated game server: map store, rooms, the door, control calls\n";
    std::cout << "=======================================================\n";
    run_store_tests();
    run_manager_tests();
    run_demo_tests();
    run_lobby_room_tests();
    run_rematch_tests();
    run_hardening_tests();
    run_map_tests();
    run_match_tests();
    run_leader_tests();
    run_control_tests();
    run_socket_tests();
    run_secret_tests();
    run_reconnect_tests();
    run_bot_tests();
    run_countdown_tests();
    run_persist_tests();
    run_persist_server_tests();
    run_persist_server_tests_2();
    run_persist_server_tests_3();
    run_persist_server_tests_4();
    run_persist_server_tests_5();
    run_persist_server_tests_6();
    run_persist_review_tests();
    run_site_stats_tests();
    run_replay_tests();
    run_bot_match_tests();
    run_restore_tests();
#if !defined(_WIN32) && defined(ANTS_SERVER_BINARY)
    run_persist_process_tests();
    run_persist_review_process_tests();
    run_persist_review_process_tests_2();
#endif
    std::cout << "=======================================================\n";
    std::cout << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n";
    std::cout << "=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

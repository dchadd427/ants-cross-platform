// Tests of the lock-step network core (docs/NETWORK_PORT.md): the wire protocol, the in-memory network, the host's turn sequencer, the client's
// turn runner, and whole matches of a host and three clients over links with latency and jitter, including a desync, a stalled peer, a dropped
// peer and a hostile peer.
#include "lockstep_fixtures.hpp"

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: lock-step core (protocol, sequencer, runner, sessions)\n"
                 "=======================================================\n";
    run_protocol_tests();
    run_network_tests();
    run_sequencer_tests();
    run_flood_budget_tests();
    run_runner_tests();
    run_reconnect_core_tests();
    run_match_tests();
    run_failure_tests();
    run_dropout_tests();
    run_migration_tests();
    run_reconnect_session_tests();
    run_reconnect_session_tests_2();
    run_protocol11_tests();
    run_team_chat_tests();
    run_protocol12_tests();
    run_protocol13_tests();
    run_protocol15_tests();
    run_protocol16_tests();
    run_protocol17_tests();
    run_restart_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

// Tests of the network in the application: the command line (names, --host, --join), a headless Application as the host of a room and as a guest of
// one (the other side is a bare NetGame with its own simulation), the setup screen as the room with names and thumbs, the start, a match driven by the
// lock-step runner with commands and chat, a guest that leaves, a host that leaves, a hidden page (the web build: no frames, the server's messages wake the
// game: N5.32 - N5.39). Real sockets on the loopback interface; one Application per test (SDL is initialised once per process).
#include "network_app_fixtures.hpp"

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    std::cout << "\n=======================================================\n [SUITE] Network port: the application (names, room, thumbs, start, match)\n"
                 "=======================================================\n";
    run_command_line_tests();
    run_bot_tests();
    run_window_tests();
    run_host_tests();
    run_guest_tests();
    run_leader_tests();
    run_latency_tests();
    run_hidden_page_tests();
    run_room_bot_tests();
    run_room_chat_ui_tests();
    run_room_chat_box_tests();
    run_one_card_tests();
    run_rematch_app_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

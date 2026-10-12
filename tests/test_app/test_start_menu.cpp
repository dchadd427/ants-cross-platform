// The desktop start menu, as a model (include/ants_app/start_menu.hpp): navigation by the keys and the mouse, the text fields, the seats of the single-player panel and the
// rule-8 line, the join and host panels, the room's code and the clipboard, the servers, the room codes and the names that the menu accepts, the remembered settings, where
// every control lies on the 640 x 480 screen, what is drawn and how long texts are cut, and which command-line options show or skip the menu. No window, no sockets: the
// application's paths against a real server are in test_start_menu_app.
#include "start_menu_fixtures.hpp"

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    std::cout << "=== Start menu ===\n";

    run_start_menu_panels_tests();
    run_start_menu_hosting_tests();

    std::cout << "\nstart menu model: " << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}

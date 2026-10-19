// Application integration tests: main() and the order of the suites (the suites are in the test_app_integration_*.cpp files)
#include "app_integration_fixtures.hpp"

// ============================================================================
// Master Test Runner Main
// ============================================================================
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=======================================================\n"
              << " ANTS REMAKE — HEADLESS INTEGRATION HARNESS\n"
              << "=======================================================\n";

    run_suite_1_surface_compositing();
    run_suite_2_camera_transforms();
    run_suite_3_hud_and_radar();
    run_suite_4_audio_mixer();
    run_suite_5_midi_player();
    run_suite_6_scorecard_and_audio_routing();
    run_suite_7_input_controls();
    run_suite_8_unit_health_and_map_select();
    run_suite_9_gameplay_mechanics_and_options();
    run_suite_10_egg_economy_incubation_teamup_abilities();
    run_suite_11_anthill_queuing_and_priority();
    run_suite_12_unit_selection_and_occupied_tile_movement();
    run_suite_12_unit_selection_and_occupied_tile_movement_2();
    run_suite_12_unit_selection_and_occupied_tile_movement_3();
    run_suite_12_unit_selection_and_occupied_tile_movement_4();
    run_suite_13_extended_play();

    std::cout << "\n=======================================================\n"
              << " INTEGRATION TEST SUMMARY\n"
              << "=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Passed:           " << (g_test_count - g_test_failures) << "\n"
              << " Failed:           " << g_test_failures << "\n"
              << "=======================================================\n";

    return (g_test_failures == 0) ? 0 : 1;
}

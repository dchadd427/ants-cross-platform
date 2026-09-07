#include "e2e_framework.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

using namespace e2e;

int main(int argc, char* argv[]) {
    bool run_tier1 = true;
    bool run_tier2 = true;
    bool run_tier3 = true;
    bool run_tier4 = true;
    bool verbose = false;
    bool list_only = false;

    if (argc > 1) {
        // If specific flags are passed, reset defaults
        bool specified_tier = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--tier" && i + 1 < argc) {
                if (!specified_tier) {
                    run_tier1 = run_tier2 = run_tier3 = run_tier4 = false;
                    specified_tier = true;
                }
                std::string t = argv[++i];
                if (t == "1") run_tier1 = true;
                else if (t == "2") run_tier2 = true;
                else if (t == "3") run_tier3 = true;
                else if (t == "4") run_tier4 = true;
            } else if (arg == "--all") {
                run_tier1 = run_tier2 = run_tier3 = run_tier4 = true;
                specified_tier = true;
            } else if (arg == "--verbose" || arg == "-v") {
                verbose = true;
            } else if (arg == "--list") {
                list_only = true;
            }
        }
    }

    const auto& all_tests = TestRegistry::instance().get_tests();

    if (list_only) {
        std::cout << "Registered E2E Tests (" << all_tests.size() << " total):\n";
        for (const auto& t : all_tests) {
            std::cout << "  [" << tier_to_string(t.tier) << "] "
                      << t.suite_name << "." << t.test_name << "\n";
        }
        return 0;
    }

    std::cout << "======================================================================\n";
    std::cout << "             ANTS REMAKE - OPAQUE-BOX E2E TEST RUNNER                 \n";
    std::cout << "======================================================================\n";

    uint32_t total_run = 0;
    uint32_t total_passed = 0;
    uint32_t total_failed = 0;

    uint32_t t1_run = 0, t1_pass = 0;
    uint32_t t2_run = 0, t2_pass = 0;
    uint32_t t3_run = 0, t3_pass = 0;
    uint32_t t4_run = 0, t4_pass = 0;

    std::vector<std::string> failure_reports;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (const auto& test : all_tests) {
        bool should_run = false;
        if (test.tier == Tier::Tier1_Feature && run_tier1) should_run = true;
        if (test.tier == Tier::Tier2_Boundary && run_tier2) should_run = true;
        if (test.tier == Tier::Tier3_Pairwise && run_tier3) should_run = true;
        if (test.tier == Tier::Tier4_Scenario && run_tier4) should_run = true;

        if (!should_run) continue;

        total_run++;
        if (test.tier == Tier::Tier1_Feature) t1_run++;
        else if (test.tier == Tier::Tier2_Boundary) t2_run++;
        else if (test.tier == Tier::Tier3_Pairwise) t3_run++;
        else if (test.tier == Tier::Tier4_Scenario) t4_run++;

        bool passed = false;
        std::string err_msg;

        auto test_start = std::chrono::high_resolution_clock::now();
        try {
            test.test_func();
            passed = true;
        } catch (const TestFailureException& e) {
            err_msg = e.what();
        } catch (const std::exception& e) {
            err_msg = std::string("Standard exception: ") + e.what();
        } catch (...) {
            err_msg = "Unknown unhandled exception";
        }
        auto test_end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(test_end - test_start).count();

        if (passed) {
            total_passed++;
            if (test.tier == Tier::Tier1_Feature) t1_pass++;
            else if (test.tier == Tier::Tier2_Boundary) t2_pass++;
            else if (test.tier == Tier::Tier3_Pairwise) t3_pass++;
            else if (test.tier == Tier::Tier4_Scenario) t4_pass++;

            if (verbose) {
                std::cout << "[PASS] " << test.suite_name << "." << test.test_name
                          << " (" << std::fixed << std::setprecision(2) << elapsed_ms << " ms)\n";
            }
        } else {
            total_failed++;
            std::cout << "[FAIL] " << test.suite_name << "." << test.test_name << "\n";
            std::cout << "       " << err_msg << "\n";
            failure_reports.push_back(test.suite_name + "." + test.test_name + ": " + err_msg);
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double total_elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "                       E2E TEST RESULTS SUMMARY                       \n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << " Tier 1 (Feature Coverage):        " << t1_pass << " / " << t1_run << " passed\n";
    std::cout << " Tier 2 (Boundary & Corner Cases): " << t2_pass << " / " << t2_run << " passed\n";
    std::cout << " Tier 3 (Cross-Feature Pairwise):  " << t3_pass << " / " << t3_run << " passed\n";
    std::cout << " Tier 4 (Real-World Scenarios):    " << t4_pass << " / " << t4_run << " passed\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << " TOTAL:                            " << total_passed << " / " << total_run
              << " passed in " << std::fixed << std::setprecision(2) << total_elapsed_ms << " ms\n";
    std::cout << "======================================================================\n";

    if (total_failed > 0) {
        std::cout << "\nFAILED TESTS (" << total_failed << "):\n";
        for (const auto& f : failure_reports) {
            std::cout << "  - " << f << "\n";
        }
        return static_cast<int>(total_failed);
    }

    std::cout << "\n>>> ALL E2E TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

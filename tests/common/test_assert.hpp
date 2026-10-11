// The one assertion and test-case harness of the C++ test programs (everything under tests/ except the E2E runner, whose
// assertions throw: tests/e2e/e2e_framework.hpp). A test program keeps its own counters and its own run_test_case(); this
// header holds only the macros, so there is one definition of ASSERT_TRUE for all of them.
//
//   - A failed ASSERT_x prints "FAILED!" and the failed expression with file and line, counts one failure and returns from
//     the enclosing test body (a lambda passed to TEST_CASE, or a void function): the rest of that test case is skipped.
//   - Every assertion that is evaluated counts one in the assertion counter.
//
// The program defines before the include (or leaves at the default):
//   ANTS_TEST_ASSERT_COUNT  the assertion counter       (default: g_assert_count)
//   ANTS_TEST_FAILURES      the failure counter         (default: g_test_failures)
//   ANTS_TEST_RUNNER        the test-case runner        (default: run_test_case)
//   ANTS_TEST_REPORT(text)  prints one failure and counts it; `text` is the stringified expression (default: the line
//                           "FAILED!\n    Assertion failed: <text> at <file>:<line>" on std::cout, then ++ANTS_TEST_FAILURES)
// A program with a richer macro of its own (ASSERT_EQ that prints the two values, an ASSERT_NEAR on floats) `#undef`s the
// shared one and defines its own after the include.
#pragma once

#include <iostream>

#ifndef ANTS_TEST_ASSERT_COUNT
#define ANTS_TEST_ASSERT_COUNT g_assert_count
#endif
#ifndef ANTS_TEST_FAILURES
#define ANTS_TEST_FAILURES g_test_failures
#endif
#ifndef ANTS_TEST_RUNNER
#define ANTS_TEST_RUNNER run_test_case
#endif
#ifndef ANTS_TEST_REPORT
namespace ants_test {
// One failure: the line on std::cout and one more in the failure counter. A function, not an expansion at each of the
// thousands of assertions, so the failing branch of an assertion stays small.
inline void report_failure(const char* expr, const char* file, int line, int& failures) {
    std::cout << "FAILED!\n    Assertion failed: " << expr << " at " << file << ":" << line << "\n";
    ++failures;
}
}  // namespace ants_test
#define ANTS_TEST_REPORT(text) ::ants_test::report_failure((text), __FILE__, __LINE__, ANTS_TEST_FAILURES)
#endif

#define TEST_CASE(name) ANTS_TEST_RUNNER(name, [&]()
#define TEST_END() );

#define ASSERT_TRUE(cond)                                                                                                \
    do {                                                                                                                 \
        ++ANTS_TEST_ASSERT_COUNT;                                                                                        \
        if (!(cond)) {                                                                                                   \
            ANTS_TEST_REPORT(#cond);                                                                                     \
            return;                                                                                                      \
        }                                                                                                                \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_LE(a, b) ASSERT_TRUE((a) <= (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_GE(a, b) ASSERT_TRUE((a) >= (b))
#define ASSERT_NEAR(a, b, tol) ASSERT_TRUE(((a) > (b) ? (a) - (b) : (b) - (a)) <= (tol))

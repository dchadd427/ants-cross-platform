#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace e2e {

enum class Tier {
    Tier1_Feature,
    Tier2_Boundary,
    Tier3_Pairwise,
    Tier4_Scenario
};

inline const char* tier_to_string(Tier t) {
    switch (t) {
        case Tier::Tier1_Feature: return "Tier 1: Feature Coverage";
        case Tier::Tier2_Boundary: return "Tier 2: Boundary & Corner Cases";
        case Tier::Tier3_Pairwise: return "Tier 3: Cross-Feature Combinations";
        case Tier::Tier4_Scenario: return "Tier 4: Real-World Scenarios";
    }
    return "Unknown Tier";
}

struct TestCase {
    std::string suite_name;
    std::string test_name;
    Tier tier;
    uint32_t feature_id;
    std::function<void()> test_func;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void register_test(const std::string& suite, const std::string& name, Tier tier, uint32_t feature_id, std::function<void()> func) {
        tests_.push_back({suite, name, tier, feature_id, std::move(func)});
    }

    const std::vector<TestCase>& get_tests() const {
        return tests_;
    }

private:
    std::vector<TestCase> tests_;
};

class TestFailureException : public std::exception {
public:
    TestFailureException(std::string msg, const char* file, int line)
        : message_(std::move(msg)), file_(file), line_(line) {
        std::ostringstream oss;
        oss << file_ << ":" << line_ << ": Assertion failed: " << message_;
        full_msg_ = oss.str();
    }

    const char* what() const noexcept override {
        return full_msg_.c_str();
    }

    const std::string& message() const { return message_; }
    const char* file() const { return file_; }
    int line() const { return line_; }

private:
    std::string message_;
    const char* file_;
    int line_;
    std::string full_msg_;
};

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name, Tier tier, uint32_t feature_id, std::function<void()> func) {
        TestRegistry::instance().register_test(suite, name, tier, feature_id, std::move(func));
    }
};

} // namespace e2e

#define E2E_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << msg; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_TRUE(cond) E2E_ASSERT((cond), "Expected true: " #cond)
#define ASSERT_FALSE(cond) E2E_ASSERT(!(cond), "Expected false: " #cond)

#define ASSERT_EQ(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a != val_b) { \
            std::ostringstream oss; \
            oss << "Expected " #a " == " #b " (" << val_a << " vs " << val_b << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a == val_b) { \
            std::ostringstream oss; \
            oss << "Expected " #a " != " #b " (" << val_a << " == " << val_b << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_LE(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (!(val_a <= val_b)) { \
            std::ostringstream oss; \
            oss << "Expected " #a " <= " #b " (" << val_a << " > " << val_b << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_GE(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (!(val_a >= val_b)) { \
            std::ostringstream oss; \
            oss << "Expected " #a " >= " #b " (" << val_a << " < " << val_b << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_LT(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (!(val_a < val_b)) { \
            std::ostringstream oss; \
            oss << "Expected " #a " < " #b " (" << val_a << " >= " << val_b << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_GT(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (!(val_a > val_b)) { \
            std::ostringstream oss; \
            oss << "Expected " #a " > " #b " (" << val_a << " <= " << val_b << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, tol) \
    do { \
        auto diff = std::abs((a) - (b)); \
        if (diff > (tol)) { \
            std::ostringstream oss; \
            oss << "Expected |" #a " - " #b "| <= " #tol " (diff=" << diff << " > " << tol << ")"; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_STREQ(a, b) \
    do { \
        std::string sa = (a); \
        std::string sb = (b); \
        if (sa != sb) { \
            std::ostringstream oss; \
            oss << "Expected string equality: \"" << sa << "\" vs \"" << sb << "\""; \
            throw ::e2e::TestFailureException(oss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define E2E_TEST_CASE_EXPAND(suite, name, tier, feat, id) \
    void e2e_test_##suite##_##name(); \
    static ::e2e::TestRegistrar e2e_reg_##suite##_##name##_##id(#suite, #name, tier, feat, e2e_test_##suite##_##name); \
    void e2e_test_##suite##_##name()

#define E2E_TIER1_TEST(suite, name, feat) E2E_TEST_CASE_EXPAND(suite, name, ::e2e::Tier::Tier1_Feature, feat, __LINE__)
#define E2E_TIER2_TEST(suite, name, feat) E2E_TEST_CASE_EXPAND(suite, name, ::e2e::Tier::Tier2_Boundary, feat, __LINE__)
#define E2E_TIER3_TEST(suite, name)       E2E_TEST_CASE_EXPAND(suite, name, ::e2e::Tier::Tier3_Pairwise, 0, __LINE__)
#define E2E_TIER4_TEST(suite, name)       E2E_TEST_CASE_EXPAND(suite, name, ::e2e::Tier::Tier4_Scenario, 0, __LINE__)

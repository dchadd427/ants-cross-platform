// The small harness of the ants_db suite: DB_CASE runs one named case, DB_CHECK counts an assertion and ends the case at the first failure. (Named differently from the other suites'
// ASSERT_TRUE on purpose: the shared assertion header that will replace those is not in the tree yet.)
#pragma once

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <process.h>
#define ANTS_DB_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_DB_GETPID getpid
#endif

namespace dbtest {

inline int g_cases = 0;
inline int g_failures = 0;
inline int g_checks = 0;

inline void run_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {        // (run_tests.sh clears it)
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_cases;
    const int before = g_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "  FAILED: " << name << ": exception " << e.what() << "\n";
        ++g_failures;
        return;
    }
    if (g_failures == before) std::cout << "  PASS: " << name << "\n";
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = std::filesystem::temp_directory_path() / ("ants_test_db_" + std::to_string(ANTS_DB_GETPID()) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    std::string file(const std::string& name) const { return (path / name).string(); }
};

inline std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void write_all(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

}  // namespace dbtest

#define DB_CASE(name) dbtest::run_case(name, [&]()
#define DB_END );
#define DB_CHECK(cond) \
    do { \
        ++dbtest::g_checks; \
        if (!(cond)) { \
            std::cout << "  FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++dbtest::g_failures; \
            return; \
        } \
    } while (0)

void run_migration_tests();

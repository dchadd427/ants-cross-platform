// What the tests of the live door share (test_server: replay_tests.hpp, test_rejoin.cpp): the numbers of the header that a snapshot carries.
#pragma once

#include <cstdio>
#include <cstdint>
#include <string>

#include "ants_ctl/http.hpp"

namespace {

// The header X-Ants-Live of a snapshot ("now=<turns run>; shown=<turns the bytes show>; delay=<turns>"): true with its three numbers when the answer has exactly that one header, of that shape
bool live_header(const ants::ctl::HttpResponse& r, uint32_t& now, uint32_t& shown, uint32_t& delay) {
    if (r.headers.size() != 1 || r.headers[0].first != "X-Ants-Live") return false;
    unsigned long n = 0, sh = 0, d = 0;
    int consumed = 0;
    if (std::sscanf(r.headers[0].second.c_str(), "now=%lu; shown=%lu; delay=%lu%n", &n, &sh, &d, &consumed) != 3 || static_cast<size_t>(consumed) != r.headers[0].second.size()) return false;
    now = static_cast<uint32_t>(n);
    shown = static_cast<uint32_t>(sh);
    delay = static_cast<uint32_t>(d);
    return true;
}

}  // namespace

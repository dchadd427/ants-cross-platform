// Tests of the control interface primitives (ants_ctl): the strict JSON library (valid and invalid documents of the JSONTestSuite kind, the limits, the
// typed accessors, the serializer, round trips of random values, 200,000 random and mutated documents checked against an independent recognizer) and the
// HTTP server over real loopback sockets with a raw client (the secret, every status the server can answer itself, a request in single bytes, pipelining,
// clients that vanish, the connection limit, response safety, the clock). Nothing here needs the game's simulation or assets.
#include "ctl_fixtures.hpp"

int main() {
#ifdef _WIN32
    WsaGuard wsa;
#endif
    std::cout << "=======================================================\n ANTS CONTROL INTERFACE TESTS (JSON, HTTP)\n"
              << "=======================================================\n";

    run_json_tests();
    run_http_tests();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}

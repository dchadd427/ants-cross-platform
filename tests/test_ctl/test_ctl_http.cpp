// Tests of the control interface: the HTTP server over real loopback sockets (CTL2.*)
#include "ctl_fixtures.hpp"

void run_http_tests() {
    // ============================================================================================
    // HTTP: the server over real loopback sockets
    // ============================================================================================

    TEST_CASE("CTL2.1 HTTP listen: a secret is required, the port is exclusive and the loopback address only") {
        ASSERT_TRUE(HttpServer::listen(0, "") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "has space") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, " ") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "tab\tsecret") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "line\nbreak") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "line\rbreak") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, std::string("nul\0byte", 8)) == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "del\x7f") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "caf\xC3\xA9") == nullptr);
        ASSERT_TRUE(HttpServer::listen(0, "", false) == nullptr);                // an empty secret also refuses a server open to the network
        auto server = HttpServer::listen(0, "Ab-._~+/=:;!@#$%^&*()[]{}<>?,|`'\"\\x", true);
        ASSERT_TRUE(server != nullptr);                                           // every visible ASCII character may be part of a secret
        ASSERT_TRUE(server->port() != 0);
        ASSERT_EQ(server->connection_count(), 0u);
        // a second server on the same port does not start
        ASSERT_TRUE(HttpServer::listen(server->port(), "another-secret", true) == nullptr);
        const uint16_t port = server->port();
        server.reset();                                                            // and the port is free again when the first one is gone
        auto again = HttpServer::listen(port, "another-secret", true);
        ASSERT_TRUE(again != nullptr);
        ASSERT_EQ(again->port(), port);
    } TEST_END();

    TEST_CASE("CTL2.2 HTTP listen: only the loopback address answers (a connection to the machine's other address is refused)") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Client c;
        ASSERT_TRUE(rig.connect(c));                                              // 127.0.0.1 works
        const std::string other = other_local_address();
        if (other.empty()) {
            std::cout << "(no other network interface here, skipped) " << std::flush;
            return;
        }
        Client remote;                                                            // (not `far`: that is an empty macro in the Windows headers)
        ASSERT_FALSE(remote.connect_to(rig.port(), other.c_str()));
    } TEST_END();

    TEST_CASE("CTL2.3 HTTP /healthz: answered by the server without a secret, the handler never sees it") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Reply r = rig.exchange("GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n");
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.reason, "OK");
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_TRUE(r.header("content-type") != nullptr && *r.header("content-type") == "application/json");
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(r.body.size()));
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store");
        ASSERT_TRUE(r.header("connection") != nullptr && *r.header("connection") == "close");
        ASSERT_TRUE(r.header("www-authenticate") == nullptr);
        ASSERT_EQ(rig.calls, 0);
        // a wrong secret does not matter for it, nor does a query or HTTP/1.0, or a body
        r = rig.exchange("GET /healthz HTTP/1.1\r\nAuthorization: Bearer nonsense\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange("GET /healthz?probe=1&x=2 HTTP/1.1\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange("GET /healthz HTTP/1.0\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange("GET /healthz HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_EQ(rig.calls, 0);
        // only that exact request is open: other methods, other paths, other spellings need the secret
        for (const char* request : {"POST /healthz HTTP/1.1\r\nContent-Length: 0\r\n\r\n", "DELETE /healthz HTTP/1.1\r\n\r\n", "GET /healthz/ HTTP/1.1\r\n\r\n",
                                    "GET /Healthz HTTP/1.1\r\n\r\n", "GET /healthz2 HTTP/1.1\r\n\r\n", "GET /health HTTP/1.1\r\n\r\n", "GET //healthz HTTP/1.1\r\n\r\n",
                                    "GET /healthz%2F HTTP/1.1\r\n\r\n", "GET /rooms HTTP/1.1\r\n\r\n", "GET / HTTP/1.1\r\n\r\n"}) {
            r = rig.exchange(request);
            ASSERT_MSG(r.status == 401, std::string("needs the secret: ") + shown(request));
        }
        ASSERT_EQ(rig.calls, 0);
        // with the secret, /healthz is still the server's own answer for GET; the other methods go to the handler
        r = rig.exchange(make_request("GET", "/healthz"));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_EQ(rig.calls, 0);
        r = rig.exchange(make_request("DELETE", "/healthz"));
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.4 HTTP: a call with the right secret reaches the handler with method, path, query, lower-cased headers and body") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        rig.handler = [&](const HttpRequest& req) {
            ++rig.calls;
            rig.seen.push_back(req);
            HttpResponse resp;
            resp.status = 201;
            resp.content_type = "text/plain; charset=utf-8";
            resp.body = "made " + req.path;
            return resp;
        };
        Reply r = rig.exchange(make_request("POST", "/rooms", "{\"seats\":4,\"map\":\"A B\"}", "Content-Type: application/json\r\nX-Custom-Header:   Some Value  \r\n"));
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(r.reason, "Created");
        ASSERT_EQ(r.body, "made /rooms");
        ASSERT_TRUE(r.header("content-type") != nullptr && *r.header("content-type") == "text/plain; charset=utf-8");
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == "11");
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store");
        ASSERT_EQ(rig.calls, 1);
        const HttpRequest& q = rig.seen[0];
        ASSERT_EQ(q.method, "POST");
        ASSERT_EQ(q.path, "/rooms");
        ASSERT_EQ(q.query, "");
        ASSERT_EQ(q.body, "{\"seats\":4,\"map\":\"A B\"}");
        ASSERT_TRUE(q.header("content-type") != nullptr && *q.header("content-type") == "application/json");
        ASSERT_TRUE(q.header("x-custom-header") != nullptr && *q.header("x-custom-header") == "Some Value");      // lower-cased name, trimmed value
        ASSERT_TRUE(q.header("host") != nullptr && *q.header("host") == "127.0.0.1");
        ASSERT_TRUE(q.header("authorization") != nullptr);
        ASSERT_TRUE(q.header("X-Custom-Header") == nullptr);                    // names are stored in lower case only
        ASSERT_EQ(q.headers.count("content-length"), 1u);
        ASSERT_EQ(q.header("content-length") != nullptr ? *q.header("content-length") : "", std::to_string(q.body.size()));

        r = rig.exchange(make_request("GET", "/rooms/ABCD?x=1&y=%20z"));
        ASSERT_EQ(rig.calls, 2);
        ASSERT_EQ(rig.seen[1].method, "GET");
        ASSERT_EQ(rig.seen[1].path, "/rooms/ABCD");
        ASSERT_EQ(rig.seen[1].query, "x=1&y=%20z");                             // not decoded
        ASSERT_EQ(rig.seen[1].body, "");
        r = rig.exchange(make_request("DELETE", "/rooms/ABCD"));
        ASSERT_EQ(rig.calls, 3);
        ASSERT_EQ(rig.seen[2].method, "DELETE");
        ASSERT_EQ(rig.seen[2].path, "/rooms/ABCD");
        ASSERT_EQ(r.status, 201);

        // edges of the target
        r = rig.exchange(make_request("GET", "/"));
        ASSERT_EQ(rig.seen.back().path, "/");
        r = rig.exchange(make_request("GET", "/?"));
        ASSERT_EQ(rig.seen.back().path, "/");
        ASSERT_EQ(rig.seen.back().query, "");
        r = rig.exchange(make_request("GET", "/a?b?c=d"));
        ASSERT_EQ(rig.seen.back().path, "/a");
        ASSERT_EQ(rig.seen.back().query, "b?c=d");                              // only the first ? splits
        r = rig.exchange(make_request("GET", "/a%2Fb/%00?q=%0d%0a"));
        ASSERT_EQ(rig.seen.back().path, "/a%2Fb/%00");                          // nothing is decoded
        ASSERT_EQ(rig.seen.back().query, "q=%0d%0a");
        r = rig.exchange(make_request("GET", "/../../etc/passwd"));
        ASSERT_EQ(rig.seen.back().path, "/../../etc/passwd");                   // not normalized: the router decides
        // the scheme is case-insensitive, and so are the header names; repeated ordinary headers are joined
        r = rig.exchange("GET /a HTTP/1.1\r\nauthorization: bearer " + std::string(Rig::kSecret) + "\r\nX-Multi: one\r\nx-multi: two\r\n\r\n");
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(rig.seen.back().header("x-multi") != nullptr && *rig.seen.back().header("x-multi") == "one, two");
        r = rig.exchange("GET /a HTTP/1.1\r\nAUTHORIZATION: BEARER   " + std::string(Rig::kSecret) + "\r\n\r\n");     // several spaces before the token
        ASSERT_EQ(r.status, 201);
        r = rig.exchange("GET /a HTTP/1.1\r\nAuthorization:Bearer " + std::string(Rig::kSecret) + "\r\n\r\n");            // no space after the colon
        ASSERT_EQ(r.status, 201);
        r = rig.exchange("GET /a HTTP/1.1\r\nAuthorization: \t Bearer " + std::string(Rig::kSecret) + " \t \r\n\r\n");    // white space around the value
        ASSERT_EQ(r.status, 201);
        r = rig.exchange(make_request("GET", "/a").replace(0, 15, "GET /a HTTP/1.0"));                                        // HTTP/1.0
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(rig.seen.back().method, "GET");
        // a body of 64 KB with every byte value
        std::string all(HttpServer::kMaxBodyBytes, 'x');
        for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<char>(i * 131 + (i >> 8));
        r = rig.exchange(make_request("POST", "/rooms", all));
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(rig.seen.back().body == all);
    } TEST_END();

    TEST_CASE("CTL2.5 HTTP auth: a missing, wrong, shortened or lengthened secret gets 401 and never reaches the handler") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string secret = Rig::kSecret;
        const std::vector<std::pair<std::string, std::string>> tries = {
            {"no header", ""},
            {"empty value", "Authorization:\r\n"},
            {"scheme only", "Authorization: Bearer\r\n"},
            {"scheme and spaces", "Authorization: Bearer    \r\n"},
            {"wrong secret", "Authorization: Bearer wrong\r\n"},
            {"first half", "Authorization: Bearer " + secret.substr(0, secret.size() / 2) + "\r\n"},
            {"one character short", "Authorization: Bearer " + secret.substr(0, secret.size() - 1) + "\r\n"},
            {"one character only", "Authorization: Bearer " + secret.substr(0, 1) + "\r\n"},
            {"missing first character", "Authorization: Bearer " + secret.substr(1) + "\r\n"},
            {"one character longer", "Authorization: Bearer " + secret + "x\r\n"},
            {"twice the secret", "Authorization: Bearer " + secret + secret + "\r\n"},
            {"secret and a space and more", "Authorization: Bearer " + secret + " more\r\n"},
            {"a high byte after the secret", "Authorization: Bearer " + secret + "\x80\r\n"},
            {"last character changed", "Authorization: Bearer " + secret.substr(0, secret.size() - 1) + "X\r\n"},
            {"first character changed", "Authorization: Bearer X" + secret.substr(1) + "\r\n"},
            {"upper case secret", "Authorization: Bearer " + std::string(secret.size(), 'T') + "\r\n"},
            {"swapped case", "Authorization: Bearer TEST-SECRET-0123456789\r\n"},
            {"basic scheme", "Authorization: Basic " + secret + "\r\n"},
            {"token scheme", "Authorization: Token " + secret + "\r\n"},
            {"no scheme", "Authorization: " + secret + "\r\n"},
            {"scheme glued to the secret", "Authorization: Bearer" + secret + "\r\n"},
            {"scheme in a tab", "Authorization: Bearer\t" + secret + "\r\n"},
            {"quoted secret", "Authorization: Bearer \"" + secret + "\"\r\n"},
            {"colon scheme", "Authorization: Bearer: " + secret + "\r\n"},
            {"secret in another header", "X-Authorization: Bearer " + secret + "\r\n"},
            {"secret in a cookie", "Cookie: Bearer " + secret + "\r\n"},
            {"secret as a query-like header", "X-Api-Key: " + secret + "\r\n"},
            {"proxy header", "Proxy-Authorization: Bearer " + secret + "\r\n"},
        };
        for (const auto& t : tries) {
            const Reply r = rig.exchange("GET /rooms/ABCD HTTP/1.1\r\nHost: x\r\n" + t.second + "\r\n");
            ASSERT_MSG(r.complete && r.status == 401, "should be 401: " + t.first + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.header("www-authenticate") != nullptr && *r.header("www-authenticate") == "Bearer", "WWW-Authenticate: " + t.first);
            ASSERT_MSG(r.body == "{\"error\":\"unauthorized\"}", "body: " + t.first + " " + r.body);
            ASSERT_MSG(r.header("content-type") != nullptr && *r.header("content-type") == "application/json", t.first);
            ASSERT_MSG(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(r.body.size()), t.first);
            ASSERT_MSG(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store", t.first);
            ASSERT_MSG(rig.calls == 0, "the handler must not be called: " + t.first);
        }
        // a NUL (or any control character) in the head is a malformed request: 400, before the secret is even looked at
        Reply nul = rig.exchange(std::string("GET /rooms/ABCD HTTP/1.1\r\nAuthorization: Bearer ") + secret + std::string(1, '\0') + "\r\n\r\n");
        ASSERT_EQ(nul.status, 400);
        ASSERT_EQ(rig.calls, 0);
        // the same with POST and a body, DELETE, an unknown method and a framing the server would otherwise refuse: the answer is 401 first
        Reply r = rig.exchange(make_request("POST", "/rooms", "{\"a\":1}", "", false));
        ASSERT_EQ(r.status, 401);
        r = rig.exchange(make_request("DELETE", "/rooms/ABCD", "", "", false));
        ASSERT_EQ(r.status, 401);
        r = rig.exchange(make_request("PUT", "/rooms/ABCD", "", "", false));
        ASSERT_EQ(r.status, 401);
        r = rig.exchange(make_request("POST", "/rooms", "", "", false, false));                                      // no Content-Length either
        ASSERT_EQ(r.status, 401);
        r = rig.exchange("POST /rooms HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
        ASSERT_EQ(r.status, 401);
        r = rig.exchange("POST /rooms HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n");
        ASSERT_EQ(r.status, 401);
        ASSERT_EQ(rig.calls, 0);
        // and with the right secret the same server answers
        r = rig.exchange(make_request("GET", "/rooms/ABCD"));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 1);
        // two Authorization headers are ambiguous: refused, even when both are right
        r = rig.exchange("GET /a HTTP/1.1\r\n" + auth_line() + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 400);
        r = rig.exchange("GET /a HTTP/1.1\r\nAuthorization: Bearer wrong\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 400);
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.6 HTTP auth: constant_time_equal compares correctly") {
        ASSERT_TRUE(constant_time_equal("", ""));
        ASSERT_TRUE(constant_time_equal("a", "a"));
        ASSERT_TRUE(constant_time_equal("secret", "secret"));
        ASSERT_FALSE(constant_time_equal("secret", "secreT"));
        ASSERT_FALSE(constant_time_equal("secret", "secre"));
        ASSERT_FALSE(constant_time_equal("secre", "secret"));
        ASSERT_FALSE(constant_time_equal("secret", ""));
        ASSERT_FALSE(constant_time_equal("", "secret"));
        ASSERT_FALSE(constant_time_equal(std::string_view("ab\0", 3), std::string_view("ab", 2)));     // a trailing NUL is a difference
        ASSERT_FALSE(constant_time_equal(std::string_view("ab", 2), std::string_view("ab\0", 3)));
        ASSERT_TRUE(constant_time_equal(std::string_view("a\0b", 3), std::string_view("a\0b", 3)));
        ASSERT_FALSE(constant_time_equal(std::string_view("a\0b", 3), std::string_view("a\0c", 3)));
        const std::string base(64, 'k');
        for (size_t i = 0; i < base.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                std::string other = base;
                other[i] = static_cast<char>(other[i] ^ (1 << bit));
                ASSERT_FALSE(constant_time_equal(base, other));
                ASSERT_FALSE(constant_time_equal(other, base));
            }
        }
        ASSERT_TRUE(constant_time_equal(base, std::string(64, 'k')));
        Rng r(77);
        for (int i = 0; i < 20000; ++i) {
            const std::string a = random_string(r);
            const std::string b = r.chance(50) ? a : random_string(r);
            ASSERT_EQ(constant_time_equal(a, b), a == b);
        }
    } TEST_END();

    TEST_CASE("CTL2.7 HTTP: other methods get 405 (with Allow), a bad method token gets 400, the handler is not called") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        for (const char* method : {"PUT", "PATCH", "HEAD", "OPTIONS", "TRACE", "CONNECT", "FOO", "get", "Get", "POSTX", "GETS", "LINK", "PROPFIND", "A"}) {
            const Reply r = rig.exchange(make_request(method, "/rooms/ABCD"));
            ASSERT_MSG(r.complete && r.status == 405, std::string("405 for ") + method + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.header("allow") != nullptr && *r.header("allow") == "GET, POST, DELETE", method);
            ASSERT_MSG(r.body == "{\"error\":\"method not allowed\"}", method);
        }
        ASSERT_EQ(rig.calls, 0);
        for (const char* method : {"G(T", "GE T", "G\"T", "G,T", "G;T", "G/T", "G@T", "G[T", "G{T", "G<T", "G=T"}) {
            const Reply r = rig.exchange(std::string(method) + " /a HTTP/1.1\r\n" + auth_line() + "\r\n");
            ASSERT_MSG(r.status == 400, std::string("400 for the method token ") + method + " (got " + std::to_string(r.status) + ")");
        }
        const Reply long_method = rig.exchange(std::string(40, 'A') + " /a HTTP/1.1\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(long_method.status, 400);
        ASSERT_EQ(rig.calls, 0);
        // the three methods that are allowed are
        ASSERT_EQ(rig.exchange(make_request("GET", "/a")).status, 200);
        ASSERT_EQ(rig.exchange(make_request("POST", "/a", "{}")).status, 200);
        ASSERT_EQ(rig.exchange(make_request("DELETE", "/a")).status, 200);
        ASSERT_EQ(rig.calls, 3);
    } TEST_END();

    TEST_CASE("CTL2.8 HTTP bodies: Content-Length only (411), at most 64 KB (413), the length must be a plain number (400)") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        // 411
        Reply r = rig.exchange(make_request("POST", "/rooms", "", "", true, false));
        ASSERT_EQ(r.status, 411);
        ASSERT_EQ(r.reason, "Length Required");
        ASSERT_TRUE(r.body.find("\"error\"") != std::string::npos);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Transfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        r = rig.exchange("GET /rooms HTTP/1.1\r\n" + auth_line() + "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        r = rig.exchange("DELETE /rooms/A HTTP/1.1\r\n" + auth_line() + "Transfer-Encoding: identity\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "transfer-encoding: gzip, chunked\r\n\r\n");
        ASSERT_EQ(r.status, 411);
        ASSERT_EQ(rig.calls, 0);
        // 413: answered on the head, the body does not have to be sent (or read)
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 65537\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 99999999999999999999999\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 4294967296\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 18446744073709551616\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange("GET /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 70000\r\n\r\n");
        ASSERT_EQ(r.status, 413);
        r = rig.exchange(make_request("POST", "/rooms", std::string(65537, 'a')));                       // the whole body is sent anyway
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 413);
        r = rig.exchange(make_request("POST", "/rooms", std::string(1000000, 'a')));
        ASSERT_EQ(r.status, 413);
        ASSERT_EQ(rig.calls, 0);
        // exactly the limit passes; leading zeros are fine
        r = rig.exchange(make_request("POST", "/rooms", std::string(65536, 'a')));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen.back().body.size(), 65536u);
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 000000000000000000000003\r\n\r\nabc");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().body, "abc");
        // Content-Length 0 on a POST: an empty body, the handler is called
        r = rig.exchange(make_request("POST", "/rooms", "", "Content-Length: 0\r\n", true, false));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().body, "");
        // a GET or DELETE may carry a body
        r = rig.exchange("GET /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 3\r\n\r\nabc");
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().body, "abc");
        r = rig.exchange("DELETE /rooms/A HTTP/1.1\r\n" + auth_line() + "Content-Length: 2\r\n\r\nxy");
        ASSERT_EQ(rig.seen.back().body, "xy");
        // the body is exactly as long as announced: what follows is not part of it
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 3\r\n\r\nabcdef");
        ASSERT_EQ(rig.seen.back().body, "abc");
        // 400: the length is not a plain number, or there are two (the handler is not called for any of them)
        const int calls_before = rig.calls;
        for (const char* bad : {"Content-Length: abc\r\n", "Content-Length: -1\r\n", "Content-Length: +5\r\n", "Content-Length: 5, 5\r\n", "Content-Length: 1 2\r\n",
                                "Content-Length: 0x10\r\n", "Content-Length: 1.5\r\n", "Content-Length: \r\n", "Content-Length: 5\r\nContent-Length: 5\r\n",
                                "Content-Length: 5\r\nContent-Length: 6\r\n", "Content-Length: 5\r\ncontent-length: 5\r\n",
                                "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n", "Transfer-Encoding: chunked\r\nContent-Length: 5\r\n"}) {
            r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + bad + "\r\nhello");
            ASSERT_MSG(r.status == 400, std::string("400 for ") + shown(bad) + " (got " + std::to_string(r.status) + ")");
        }
        ASSERT_EQ(rig.calls, calls_before);
    } TEST_END();

    TEST_CASE("CTL2.9 HTTP: Expect: 100-continue is answered, other expectations are refused") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Client c;
        ASSERT_TRUE(rig.connect(c));
        c.send_all("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: 100-continue\r\nContent-Length: 5\r\n\r\n");
        const std::string interim = rig.quiet_for(c, 500);
        ASSERT_EQ(interim, "HTTP/1.1 100 Continue\r\n\r\n");
        ASSERT_EQ(rig.calls, 0);
        c.send_all("hello");
        const Reply r = rig.read_reply(c);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen.back().body, "hello");
        // a body that already came with the head needs no 100
        const Reply r2 = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: 100-continue\r\nContent-Length: 2\r\n\r\nhi");
        ASSERT_EQ(r2.status, 200);
        ASSERT_EQ(r2.responses, 1);
        ASSERT_TRUE(r2.raw.compare(0, 12, "HTTP/1.1 200") == 0);
        // an unknown expectation, and a request that is refused before its body: no 100
        ASSERT_EQ(rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: magic\r\nContent-Length: 2\r\n\r\nhi").status, 417);
        const Reply r3 = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Expect: 100-continue\r\nContent-Length: 70000\r\n\r\n");
        ASSERT_EQ(r3.status, 413);
        ASSERT_EQ(r3.responses, 1);
        const Reply r4 = rig.exchange("POST /rooms HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n");
        ASSERT_EQ(r4.status, 401);
        ASSERT_EQ(r4.responses, 1);
        ASSERT_EQ(rig.calls, 2);
    } TEST_END();

    TEST_CASE("CTL2.10 HTTP: a request that is not well formed gets 400, a wrong version 505, a head that is too large 431") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string a = auth_line();
        const std::vector<std::pair<std::string, std::string>> bad = {
            {"no version", "GET /\r\n" + a + "\r\n"},
            {"a fourth word", "GET / HTTP/1.1 extra\r\n" + a + "\r\n"},
            {"a space in the target", "GET /a b HTTP/1.1\r\n" + a + "\r\n"},
            {"two spaces", "GET  / HTTP/1.1\r\n" + a + "\r\n"},
            {"leading space", " GET / HTTP/1.1\r\n" + a + "\r\n"},
            {"trailing space", "GET / HTTP/1.1 \r\n" + a + "\r\n"},
            {"tab as separator", "GET\t/\tHTTP/1.1\r\n" + a + "\r\n"},
            {"no target", "GET  HTTP/1.1\r\n" + a + "\r\n"},
            {"only a method", "GET\r\n" + a + "\r\n"},
            {"blank request line", "\r\n" + a + "\r\n"},
            {"garbage", "\x01\x02\x03\x04 garbage\r\n\r\n"},
            {"binary", std::string("\xFF\xFE\xFD\r\n\r\n")},
            {"lower case version", "GET / http/1.1\r\n" + a + "\r\n"},
            {"short version", "GET / HTTP/1\r\n" + a + "\r\n"},
            {"long version", "GET / HTTP/1.11\r\n" + a + "\r\n"},
            {"version letters", "GET / HTTP/x.y\r\n" + a + "\r\n"},
            {"no slash in version", "GET / HTTP1.1\r\n" + a + "\r\n"},
            {"absolute form", "GET http://example.com/ HTTP/1.1\r\n" + a + "\r\n"},
            {"asterisk form", "GET * HTTP/1.1\r\n" + a + "\r\n"},
            {"authority form", "GET example.com:80 HTTP/1.1\r\n" + a + "\r\n"},
            {"no leading slash", "GET rooms HTTP/1.1\r\n" + a + "\r\n"},
            {"fragment", "GET /a#frag HTTP/1.1\r\n" + a + "\r\n"},
            {"control character in the target", "GET /\x01 HTTP/1.1\r\n" + a + "\r\n"},
            {"DEL in the target", "GET /\x7f HTTP/1.1\r\n" + a + "\r\n"},
            {"non-ASCII in the target", "GET /\xC3\xA9 HTTP/1.1\r\n" + a + "\r\n"},
            {"NUL in the target", std::string("GET /a\0b HTTP/1.1\r\n", 19) + a + "\r\n"},
            {"header without a colon", "GET / HTTP/1.1\r\n" + a + "Host\r\n\r\n"},
            {"space in a header name", "GET / HTTP/1.1\r\n" + a + "Bad Name: x\r\n\r\n"},
            {"space before the colon", "GET / HTTP/1.1\r\n" + a + "Name : x\r\n\r\n"},
            {"empty header name", "GET / HTTP/1.1\r\n" + a + ": x\r\n\r\n"},
            {"folded header (space)", "GET / HTTP/1.1\r\n" + a + "X: a\r\n b\r\n\r\n"},
            {"folded header (tab)", "GET / HTTP/1.1\r\n" + a + "X: a\r\n\tb\r\n\r\n"},
            {"folded first header", "GET / HTTP/1.1\r\n folded\r\n" + a + "\r\n"},
            {"bare LF in a header", "GET / HTTP/1.1\r\n" + a + "X: a\nb: c\r\n\r\n"},
            {"bare CR in a header", "GET / HTTP/1.1\r\n" + a + "X: a\rb: c\r\n\r\n"},
            {"NUL in a header", std::string("GET / HTTP/1.1\r\n") + a + std::string("X: a\0b\r\n\r\n", 10)},
            {"control character in a header", "GET / HTTP/1.1\r\n" + a + "X: a\x01" "b\r\n\r\n"},
            {"DEL in a header", "GET / HTTP/1.1\r\n" + a + "X: a\x7f" "b\r\n\r\n"},
            {"bad character in a header name", "GET / HTTP/1.1\r\n" + a + "X(Y): z\r\n\r\n"},
            {"duplicate Host", "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n" + a + "\r\n"},
        };
        for (const auto& t : bad) {
            const Reply r = rig.exchange(t.second);
            ASSERT_MSG(r.complete && r.status == 400, "should be 400: " + t.first + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.body.find("\"error\"") != std::string::npos, "an error body: " + t.first);
            ASSERT_MSG(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(r.body.size()), t.first);
            ASSERT_MSG(r.responses == 1, t.first);
        }
        // a bare LF as the end of the head is refused at once, not after a timeout
        Client c;
        ASSERT_TRUE(rig.connect(c));
        c.send_all("GET / HTTP/1.1\n" + a + "\n");
        const Reply lf = rig.read_reply(c, 500);
        ASSERT_EQ(lf.status, 400);
        ASSERT_EQ(rig.calls, 0);
        // versions
        for (const char* v : {"HTTP/2.0", "HTTP/0.9", "HTTP/1.2", "HTTP/3.0", "HTTP/9.9", "HTTP/1.9"}) {
            const Reply r = rig.exchange(std::string("GET / ") + v + "\r\n" + a + "\r\n");
            ASSERT_MSG(r.status == 505, std::string("505 for ") + v + " (got " + std::to_string(r.status) + ")");
        }
        ASSERT_EQ(rig.calls, 0);
        // 431: more than 8 KB of head, with and without the end of the head, and more than 100 headers
        Reply r = rig.exchange("GET / HTTP/1.1\r\n" + a + "X: " + std::string(9000, 'v') + "\r\n\r\n");
        ASSERT_EQ(r.status, 431);
        r = rig.exchange("GET /" + std::string(9000, 'a') + " HTTP/1.1\r\n" + a + "\r\n");
        ASSERT_EQ(r.status, 431);
        r = rig.exchange("GET /" + std::string(9000, 'a'));                                          // no end of the head at all
        ASSERT_EQ(r.status, 431);
        r = rig.exchange(std::string(100000, 'a'));
        ASSERT_EQ(r.status, 431);
        std::string many = "GET / HTTP/1.1\r\n" + a;
        for (int i = 0; i < 99; ++i) many += "H" + std::to_string(i) + ": v\r\n";                    // 100 headers with the secret
        r = rig.exchange(many + "\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange(many + "H99: v\r\n\r\n");                                                    // 101
        ASSERT_EQ(r.status, 431);
        // the head may be exactly 8192 bytes, not one more
        const std::string prefix = "GET / HTTP/1.1\r\n" + a + "X: ";
        const size_t pad = HttpServer::kMaxHeadBytes - prefix.size() - 4;
        r = rig.exchange(prefix + std::string(pad, 'p') + "\r\n\r\n");
        ASSERT_EQ(r.status, 200);
        r = rig.exchange(prefix + std::string(pad + 1, 'p') + "\r\n\r\n");
        ASSERT_EQ(r.status, 431);
        // the 8 KB are about the head: a body of 64 KB behind a small head is fine (covered in CTL2.4); the handler count is what the 200s made
        ASSERT_EQ(rig.calls, 2);
    } TEST_END();

    TEST_CASE("CTL2.11 HTTP: parse_http_head on its own") {
        HttpHead h;
        std::string e;
        ASSERT_EQ(parse_http_head("GET /a/b?c=d HTTP/1.1\r\nHost: Example\r\nX-Y:  z \r\n\r\n", h, &e), 0);
        ASSERT_EQ(h.request.method, "GET");
        ASSERT_EQ(h.request.path, "/a/b");
        ASSERT_EQ(h.request.query, "c=d");
        ASSERT_EQ(h.request.headers.size(), 2u);
        ASSERT_EQ(*h.request.header("host"), "Example");
        ASSERT_EQ(*h.request.header("x-y"), "z");
        ASSERT_FALSE(h.has_content_length);
        ASSERT_EQ(h.body_status, 0);
        ASSERT_EQ(parse_http_head("POST / HTTP/1.1\r\nContent-Length: 12\r\n\r\n", h, &e), 0);
        ASSERT_TRUE(h.has_content_length);
        ASSERT_EQ(h.content_length, 12u);
        ASSERT_EQ(h.body_status, 0);
        ASSERT_EQ(parse_http_head("POST / HTTP/1.1\r\n\r\n", h, &e), 0);
        ASSERT_EQ(h.body_status, 411);
        ASSERT_EQ(parse_http_head("POST / HTTP/1.1\r\nContent-Length: 70000\r\n\r\n", h, &e), 0);
        ASSERT_EQ(h.body_status, 413);
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\nExpect: 100-Continue\r\nContent-Length: 1\r\n\r\n", h, &e), 0);
        ASSERT_TRUE(h.expect_continue);
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\n", h, &e), 400);                  // not the whole head
        ASSERT_EQ(parse_http_head("", h, &e), 400);
        ASSERT_EQ(parse_http_head("\r\n\r\n", h, &e), 400);
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\n\r\nX: y\r\n\r\n", h, &e), 400);   // a blank line in the middle
        ASSERT_EQ(parse_http_head("GET / HTTP/2.0\r\n\r\n", h, &e), 505);
        ASSERT_EQ(parse_http_head(std::string(HttpServer::kMaxHeadBytes + 1, 'a'), h, &e), 431);
        ASSERT_FALSE(e.empty());
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\n\r\n", h, nullptr), 0);               // no error pointer is fine
        ASSERT_EQ(parse_http_head("GET / HTTP/1.1\r\nbad\r\n\r\n", h, nullptr), 400);
    } TEST_END();

    TEST_CASE("CTL2.12 HTTP: a request that is not complete 5 seconds after accept gets 408 (the clock is the caller's)") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        // an idle connection
        {
            Client c;
            rig.now = 10000;
            ASSERT_TRUE(rig.connect_accepted(c));                                        // accepted at 10000
            rig.now = 14999;
            int rc = 0;
            std::string raw = rig.quiet_for(c, 20, &rc);
            ASSERT_TRUE(raw.empty() && rc == -1);                                        // nothing yet, the connection is open
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now = 15000;
            const Reply r = rig.read_reply(c);
            ASSERT_TRUE(r.complete);
            ASSERT_EQ(r.status, 408);
            ASSERT_EQ(r.reason, "Request Timeout");
            ASSERT_EQ(r.body, "{\"error\":\"request timeout\"}");
            ASSERT_TRUE(r.header("connection") != nullptr && *r.header("connection") == "close");
            ASSERT_EQ(rig.calls, 0);
        }
        // an incomplete head
        {
            Client c;
            rig.now = 20000;
            ASSERT_TRUE(rig.connect_accepted(c));
            c.send_all("GET /rooms HTTP/1.1\r\n" + auth_line() + "X-Slow: ");
            rig.pump();
            rig.now = 24999;
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = 25000;
            const Reply r = rig.read_reply(c);
            ASSERT_EQ(r.status, 408);
            ASSERT_EQ(rig.calls, 0);
        }
        // a head without its body
        {
            Client c;
            rig.now = 30000;
            ASSERT_TRUE(rig.connect_accepted(c));
            c.send_all("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 10\r\n\r\n12345");
            rig.pump();
            rig.now = 34999;
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = 35000;
            const Reply r = rig.read_reply(c);
            ASSERT_EQ(r.status, 408);
            ASSERT_EQ(rig.calls, 0);
        }
        // a request that completes just in time is served
        {
            Client c;
            rig.now = 40000;
            ASSERT_TRUE(rig.connect_accepted(c));
            c.send_all("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 4\r\n\r\n12");
            rig.pump();
            rig.now = 44999;
            rig.pump();
            c.send_all("34");
            const Reply r = rig.read_reply(c);
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(rig.calls, 1);
            ASSERT_EQ(rig.seen.back().body, "1234");
        }
        // the timeout can be changed
        {
            rig.server->set_request_timeout_ms(200);
            Client c;
            rig.now = 50000;
            ASSERT_TRUE(rig.connect_accepted(c));
            rig.now = 50199;
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = 50200;
            ASSERT_EQ(rig.read_reply(c).status, 408);
            rig.server->set_request_timeout_ms(HttpServer::kRequestTimeoutMs);
        }
        // the clock may wrap around 2^32
        {
            Client c;
            rig.now = 0xFFFFFFFFu - 999u;
            ASSERT_TRUE(rig.connect_accepted(c));
            rig.now = rig.now + 4999u;                                                   // wrapped past zero
            ASSERT_TRUE(rig.now < 10000u);
            int rc = 0;
            ASSERT_TRUE(rig.quiet_for(c, 20, &rc).empty() && rc == -1);
            rig.now = rig.now + 1u;
            ASSERT_EQ(rig.read_reply(c).status, 408);
        }
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.13 HTTP: a request that arrives one byte at a time is read whole") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string body = "{\"seats\":4,\"note\":\"one byte at a time\"}";
        const std::string request = make_request("POST", "/rooms?x=1", body, "X-Piece: yes\r\n");
        Client c;
        ASSERT_TRUE(rig.connect(c));
        for (size_t i = 0; i < request.size(); ++i) {
            ASSERT_EQ(c.send_all(request.substr(i, 1)), 1u);
            rig.pump();
            if (i % 8 == 0) sleep_ms(1);
            ASSERT_MSG(rig.calls == 0 || i + 1 == request.size(), "the handler ran before the request was complete, at byte " + std::to_string(i));
        }
        const Reply r = rig.read_reply(c);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen[0].method, "POST");
        ASSERT_EQ(rig.seen[0].path, "/rooms");
        ASSERT_EQ(rig.seen[0].query, "x=1");
        ASSERT_EQ(rig.seen[0].body, body);
        ASSERT_TRUE(rig.seen[0].header("x-piece") != nullptr && *rig.seen[0].header("x-piece") == "yes");
        // the same with CRLF split across pieces, and a refusal in pieces
        Client d;
        ASSERT_TRUE(rig.connect(d));
        const std::string bad = "GET / HTTP/1.1\r\nBad Name: x\r\n\r\n";
        for (size_t i = 0; i < bad.size(); ++i) {
            d.send_all(bad.substr(i, 1));
            rig.pump();
        }
        ASSERT_EQ(rig.read_reply(d).status, 400);
        ASSERT_EQ(rig.calls, 1);
    } TEST_END();

    TEST_CASE("CTL2.14 HTTP: a second request behind the first on the same connection is ignored, the connection closes") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string first = make_request("GET", "/first");
        const std::string second = make_request("GET", "/second");
        Reply r = rig.exchange(first + second);
        ASSERT_TRUE(r.complete);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_EQ(rig.seen[0].path, "/first");
        // a POST whose body is followed by another request
        r = rig.exchange(make_request("POST", "/p1", "abc") + make_request("POST", "/p2", "def"));
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 2);
        ASSERT_EQ(rig.seen[1].body, "abc");
        // the first one refused: still one answer
        r = rig.exchange(make_request("GET", "/first", "", "", false) + second);
        ASSERT_EQ(r.status, 401);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(rig.calls, 2);
        // /healthz first, then a valid request: the server answers the first only
        r = rig.exchange("GET /healthz HTTP/1.1\r\n\r\n" + second);
        ASSERT_EQ(r.responses, 1);
        ASSERT_EQ(r.body, "{\"ok\":true}");
        ASSERT_EQ(rig.calls, 2);
        // a lot of data behind the request does not make the answer get lost: the server has answered long before the client has stopped sending,
        // and a server that closed the socket then would answer the rest with a reset that wipes the unread answer out of the client's buffer
        {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(first);
            rig.pump_n(3);
            size_t sent = 0;
            const std::string junk(8192, 'z');
            for (int i = 0; i < 40; ++i) {
                sent += c.send_all(junk);
                rig.pump();
            }
            ASSERT_EQ(sent, 40u * 8192u);
            r = rig.read_reply(c);
            ASSERT_TRUE(r.complete);
            ASSERT_FALSE(r.reset);
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(r.responses, 1);
            ASSERT_EQ(rig.calls, 3);
        }
        r = rig.exchange(first + std::string(60000, 'z'));
        ASSERT_TRUE(r.complete);
        ASSERT_FALSE(r.reset);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.calls, 4);
        // a request the server refuses on its head, with a body that is on its way
        r = rig.exchange("POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 65537\r\n\r\n" + std::string(65537, 'b'));
        ASSERT_FALSE(r.reset);
        ASSERT_EQ(r.status, 413);
        ASSERT_EQ(rig.calls, 4);
    } TEST_END();

    TEST_CASE("CTL2.15 HTTP: after the answer the connection lingers until the client closes or 2 s pass, then it is gone") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Client c;
        rig.now = 1000;
        ASSERT_TRUE(rig.connect(c));
        c.send_all(make_request("GET", "/a"));
        const Reply r = rig.read_reply(c);
        ASSERT_TRUE(r.complete);                                                        // the answer, then the end of the stream
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.server->connection_count(), 1u);                                  // the client has not closed: the server waits
        rig.now = 1000 + HttpServer::kLingerMs - 1;
        rig.pump_n(5);
        ASSERT_EQ(rig.server->connection_count(), 1u);
        rig.now = 1000 + HttpServer::kLingerMs;
        ASSERT_TRUE(rig.wait_count(0));
        // a client that closes first frees the slot at once
        Client d;
        rig.now = 5000;
        ASSERT_TRUE(rig.connect(d));
        d.send_all(make_request("GET", "/a"));
        ASSERT_TRUE(rig.read_reply(d).complete);
        d.close();
        ASSERT_TRUE(rig.wait_count(0));
        // a client that half-closes right after its request still gets the answer
        Client e;
        ASSERT_TRUE(rig.connect(e));
        e.send_all(make_request("GET", "/half"));
        e.shutdown_write();
        const Reply h = rig.read_reply(e);
        ASSERT_EQ(h.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/half");
    } TEST_END();

    TEST_CASE("CTL2.16 HTTP: clients that disconnect in the middle of a request are dropped quietly") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string head = "POST /rooms HTTP/1.1\r\n" + auth_line() + "Content-Length: 100\r\n\r\n";
        const std::vector<std::string> pieces = {"", "G", "GET /ro", "GET /rooms HTTP/1.1\r\n", "GET /rooms HTTP/1.1\r\n" + auth_line(), head, head + "0123456789"};
        for (const bool reset : {false, true}) {
            for (const std::string& p : pieces) {
                Client c;
                ASSERT_TRUE(rig.connect(c));
                if (!p.empty()) c.send_all(p);
                rig.pump_n(3);
                if (reset) c.abort_connection();
                else c.close();
                ASSERT_MSG(rig.wait_count(0), "a vanished client is dropped: piece " + shown(p) + (reset ? " (reset)" : " (closed)"));
            }
        }
        ASSERT_EQ(rig.calls, 0);
        // a client that closes while the answer is on its way
        {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(make_request("GET", "/a"));
            c.abort_connection();
            ASSERT_TRUE(rig.wait_count(0));
        }
        // and the server is still fine
        const Reply r = rig.exchange(make_request("GET", "/after"));
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/after");
    } TEST_END();

    TEST_CASE("CTL2.17 HTTP: at most 32 connections at once; a new one takes the slot of the oldest connection that has not sent its head, so idle ones can never lock the interface out; freed slots are used again") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        // 40 clients connect and say nothing: every one beyond the 32nd takes the slot of the oldest silent one (clients 0 - 7 are evicted, 8 - 39 remain)
        std::vector<Client> clients(40);
        for (Client& c : clients) ASSERT_TRUE(rig.connect(c));
        ASSERT_TRUE(rig.wait_count(HttpServer::kMaxConnections));
        for (size_t i = 0; i < clients.size() - HttpServer::kMaxConnections; ++i) {             // the server closes the evicted ones without an answer
            std::string raw;
            int rc = -1;
            for (int round = 0; round < 2000 && rc == -1; ++round) {
                rig.pump();
                rc = clients[i].recv_some(raw);
                if (rc == -1) sleep_ms(1);
            }
            ASSERT_MSG(rc == 0 || rc == -2, "connection " + std::to_string(i) + " was evicted");
            ASSERT_TRUE(raw.empty());
        }
        ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
        size_t closed = 0;
        size_t open = 0;
        for (size_t i = 0; i < clients.size(); ++i) {
            std::string raw;
            const int rc = clients[i].recv_some(raw);
            if (rc == 0 || rc == -2) ++closed;
            if (rc == -1) ++open;
            ASSERT_MSG((i >= clients.size() - HttpServer::kMaxConnections) == (rc == -1), "connection " + std::to_string(i) + (rc == -1 ? " is open" : " is closed"));
            ASSERT_TRUE(raw.empty());
        }
        ASSERT_EQ(open, 32u);
        ASSERT_EQ(closed, 8u);
        // the newest one is served; a request that comes while all 32 are held by silent connections is served too (that is the point)
        clients[8].send_all(make_request("GET", "/first"));
        const Reply r = rig.read_reply(clients[8]);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/first");
        clients[8].close();
        ASSERT_TRUE(rig.wait_count(31));
        Client fresh;
        ASSERT_TRUE(rig.connect(fresh));
        ASSERT_TRUE(rig.wait_count(32));
        Client real;                                                                           // all 32 slots are held by silent connections: the real request still gets in
        ASSERT_TRUE(rig.connect(real));
        real.send_all(make_request("GET", "/real"));
        ASSERT_EQ(rig.read_reply(real).status, 200);
        ASSERT_EQ(rig.seen.back().path, "/real");
        std::string raw;
        ASSERT_EQ(fresh.recv_some(raw), -1);                                                   // (the oldest silent one, clients[9], is the one that gave way)
        int rc9 = -1;
        for (int round = 0; round < 2000 && rc9 == -1; ++round) {
            rig.pump();
            rc9 = clients[9].recv_some(raw);
            if (rc9 == -1) sleep_ms(1);
        }
        ASSERT_TRUE(rc9 == 0 || rc9 == -2);
        fresh.send_all(make_request("GET", "/fresh"));
        ASSERT_EQ(rig.read_reply(fresh).status, 200);
        // the remaining idle ones time out together after 5 seconds
        rig.now += HttpServer::kRequestTimeoutMs;
        int answered = 0;
        for (size_t i = 10; i < clients.size(); ++i) {
            if (rig.read_reply(clients[i], 500).status == 408) ++answered;
        }
        ASSERT_EQ(answered, 30);
        ASSERT_TRUE(rig.server->connection_count() >= 30u);                         // they wait for their clients to close (or for the linger time)
        rig.now += HttpServer::kLingerMs;
        ASSERT_TRUE(rig.wait_count(0));
        // connections that are being answered are never evicted: with every slot busy writing or receiving a body, a further connection is closed at once
        {
            std::vector<Client> bodies(HttpServer::kMaxConnections);
            const std::string head = "POST /body HTTP/1.1\r\nHost: x\r\n" + auth_line() + "Content-Length: 100\r\n\r\n";
            for (Client& c : bodies) {
                ASSERT_TRUE(rig.connect(c));
                c.send_all(head);                                                              // the head is complete; the body never comes
            }
            for (int i = 0; i < 30; ++i) {
                rig.pump();
                sleep_ms(2);
            }
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
            Client turned_away;
            ASSERT_TRUE(rig.connect(turned_away));
            int rc = -1;
            const std::string nothing = rig.quiet_for(turned_away, 2000, &rc);
            ASSERT_TRUE(rc == 0 || rc == -2);
            ASSERT_TRUE(nothing.empty());
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
            for (Client& c : bodies) c.close();
            ASSERT_TRUE(rig.wait_count(0));
        }
        // a flood of connections that connect and leave does not leak slots
        for (int i = 0; i < 300; ++i) {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            if (i % 3 == 0) c.send_all("GET");
            if (i % 50 == 0) rig.pump();
        }
        fresh.close();
        real.close();
        ASSERT_TRUE(rig.wait_count(0));
    } TEST_END();

    TEST_CASE("CTL2.17b HTTP: when every slot is taken, silent connections give way before connections that only wait to close, and the oldest of the kind first; a request that is waiting is never the one that gives way") {
        // every slot waits to close after its answer (32 clients got an answer and keep their side open): a new connection takes the slot of the oldest
        {
            Rig rig;
            ASSERT_TRUE(rig.ok());
            std::vector<Client> clients(HttpServer::kMaxConnections);
            for (size_t i = 0; i < clients.size(); ++i) {
                ASSERT_TRUE(rig.connect(clients[i]));
                clients[i].send_all(make_request("GET", "/linger" + std::to_string(i)));
                ASSERT_EQ(rig.read_reply(clients[i]).status, 200);                      // answered; the server now waits for the client to close
            }
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
            Client fresh;
            ASSERT_TRUE(rig.connect(fresh));
            fresh.send_all(make_request("GET", "/new"));
            ASSERT_EQ(rig.read_reply(fresh).status, 200);                               // it got in
            ASSERT_EQ(rig.seen.back().path, "/new");
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);     // one of the waiting connections was closed to make room (the server's end of a
                                                                                       // finished answer is half-closed anyway: the client cannot tell which one from its side)
        }
        // 16 that only wait to close (the oldest) and 16 silent ones (newer): the new connection takes a SILENT slot, the waiting ones stay
        {
            Rig rig;
            ASSERT_TRUE(rig.ok());
            std::vector<Client> clients(HttpServer::kMaxConnections);
            for (size_t i = 0; i < 16; ++i) {
                ASSERT_TRUE(rig.connect(clients[i]));
                clients[i].send_all(make_request("GET", "/linger"));
                ASSERT_EQ(rig.read_reply(clients[i]).status, 200);
            }
            for (size_t i = 16; i < clients.size(); ++i) ASSERT_TRUE(rig.connect(clients[i]));
            ASSERT_TRUE(rig.wait_count(HttpServer::kMaxConnections));
            Client fresh;
            ASSERT_TRUE(rig.connect(fresh));
            fresh.send_all(make_request("GET", "/new"));
            ASSERT_EQ(rig.read_reply(fresh).status, 200);
            std::string raw;
            int rc = -1;
            for (int round = 0; round < 2000 && rc == -1; ++round) {                    // the oldest SILENT connection (16) gave way
                rig.pump();
                rc = clients[16].recv_some(raw);
                if (rc == -1) sleep_ms(1);
            }
            ASSERT_TRUE(rc == 0 || rc == -2);
            ASSERT_EQ(clients[17].recv_some(raw), -1);                                  // only that one: the next silent one is still open
            ASSERT_EQ(rig.server->connection_count(), HttpServer::kMaxConnections);
        }
        // a request that has arrived but has not been read yet is not idle: 32 connections with their requests already waiting, a 33rd connects in the same
        // pass: the 33rd is turned away, and every one of the 32 requests is answered
        {
            Rig rig;
            ASSERT_TRUE(rig.ok());
            std::vector<Client> clients(HttpServer::kMaxConnections + 1);
            for (size_t i = 0; i < HttpServer::kMaxConnections; ++i) {
                ASSERT_TRUE(rig.connect(clients[i]));
                clients[i].send_all(make_request("GET", "/waiting" + std::to_string(i)));
            }
            ASSERT_TRUE(rig.connect(clients[HttpServer::kMaxConnections]));            // (nothing has been pumped yet: the server has accepted nothing)
            size_t answered = 0;
            for (size_t i = 0; i < HttpServer::kMaxConnections; ++i) {
                if (rig.read_reply(clients[i]).status == 200) ++answered;
            }
            ASSERT_EQ(answered, HttpServer::kMaxConnections);
        }
    } TEST_END();

    TEST_CASE("CTL2.18 HTTP response safety: the handler's output cannot split a response, bad statuses and exceptions become 500") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const auto answer_with = [&](const HttpResponse& resp) {
            rig.handler = [resp, &rig](const HttpRequest&) {
                ++rig.calls;
                return resp;
            };
            return rig.exchange(make_request("GET", "/x"));
        };
        // a content type with CR / LF / controls is replaced; nothing of it can start a header
        for (const char* type : {"text/plain\r\nX-Evil: 1", "text/plain\nX-Evil: 1", "text/plain\rX-Evil: 1", "a\x01" "b", "tab\there", "x\x7f", "caf\xC3\xA9", ""}) {
            HttpResponse resp;
            resp.content_type = type;
            resp.body = "ok";
            const Reply r = answer_with(resp);
            ASSERT_MSG(r.status == 200, "status kept for " + shown(type));
            ASSERT_MSG(r.header("x-evil") == nullptr, "no injected header for " + shown(type));
            ASSERT_MSG(r.header("content-type") != nullptr && *r.header("content-type") == "application/json", "fallback type for " + shown(type));
            ASSERT_MSG(r.raw.find("X-Evil") == std::string::npos || r.raw.find("\r\nX-Evil") == std::string::npos, "no injected line for " + shown(type));
            ASSERT_MSG(r.body == "ok", "body for " + shown(type));
            ASSERT_MSG(r.responses == 1, shown(type));
        }
        HttpResponse html;
        html.content_type = "text/html; charset=utf-8";
        html.body = "<p>hi</p>\r\n\r\nHTTP/1.1 200 OK";                                  // CR LF in the body are the handler's business and stay in the body
        Reply r = answer_with(html);
        ASSERT_TRUE(r.header("content-type") != nullptr && *r.header("content-type") == "text/html; charset=utf-8");
        ASSERT_EQ(r.body, html.body);
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == std::to_string(html.body.size()));
        // statuses outside 200 - 599 are the server's 500
        for (const int status : {0, 99, 100, 101, 199, 600, 999, -5, 100000}) {
            HttpResponse resp;
            resp.status = status;
            resp.body = "secret handler text";
            r = answer_with(resp);
            ASSERT_MSG(r.status == 500, "500 for status " + std::to_string(status) + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.body == "{\"error\":\"internal error\"}", "body for status " + std::to_string(status));
        }
        for (const int status : {200, 201, 202, 301, 302, 304, 400, 403, 404, 409, 422, 429, 500, 503, 599}) {
            HttpResponse resp;
            resp.status = status;
            resp.body = "{}";
            r = answer_with(resp);
            ASSERT_MSG(r.status == status, "status " + std::to_string(status) + " is passed on (got " + std::to_string(r.status) + ")");
        }
        // 204 has no body, no type and no length
        HttpResponse none;
        none.status = 204;
        none.body = "must not be sent";
        r = answer_with(none);
        ASSERT_EQ(r.status, 204);
        ASSERT_EQ(r.body, "");
        ASSERT_TRUE(r.header("content-length") == nullptr);
        ASSERT_TRUE(r.header("content-type") == nullptr);
        // an empty body has a length of 0
        HttpResponse empty;
        empty.status = 200;
        r = answer_with(empty);
        ASSERT_EQ(r.body, "");
        ASSERT_TRUE(r.header("content-length") != nullptr && *r.header("content-length") == "0");
        // a handler that throws, throws something else, or is missing
        rig.handler = [](const HttpRequest&) -> HttpResponse { throw std::runtime_error("boom with details /secret/path"); };
        r = rig.exchange(make_request("GET", "/x"));
        ASSERT_EQ(r.status, 500);
        ASSERT_EQ(r.body, "{\"error\":\"internal error\"}");
        ASSERT_TRUE(r.raw.find("boom") == std::string::npos);
        rig.handler = [](const HttpRequest&) -> HttpResponse { throw 42; };
        ASSERT_EQ(rig.exchange(make_request("GET", "/x")).status, 500);
        rig.handler = nullptr;
        ASSERT_EQ(rig.exchange(make_request("GET", "/x")).status, 500);
        ASSERT_EQ(rig.exchange("GET /healthz HTTP/1.1\r\n\r\n").status, 200);              // /healthz does not need a handler at all
        // a body above 1 MB is refused
        HttpResponse oversized;
        oversized.body.assign(HttpServer::kMaxResponseBytes + 1, 'h');
        ASSERT_EQ(answer_with(oversized).status, 500);
        // the server's own answers never repeat what the client sent
        rig.handler = [&](const HttpRequest&) {
            ++rig.calls;
            return HttpResponse();
        };
        r = rig.exchange("GET /\xC3\xA9<script>alert(1)</script> HTTP/1.1\r\nX-Evil: \"quoted\\ text\"\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 400);
        ASSERT_TRUE(r.raw.find("script") == std::string::npos && r.raw.find("quoted") == std::string::npos);
        r = rig.exchange("GET /a%0d%0aX-Evil:%201 HTTP/1.1\r\n" + auth_line() + "\r\n");
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(r.header("x-evil") == nullptr);
        ASSERT_TRUE(r.raw.find("%0d") == std::string::npos);
        // every answer carries the same safety headers
        r = rig.exchange(make_request("GET", "/x"));
        ASSERT_TRUE(r.header("x-content-type-options") != nullptr && *r.header("x-content-type-options") == "nosniff");
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store");
        ASSERT_TRUE(r.header("connection") != nullptr && *r.header("connection") == "close");
    } TEST_END();

    TEST_CASE("CTL2.19 HTTP: a large answer reaches a slow reader whole, a reader that never reads is dropped") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        const std::string big = body_pattern(900000);
        rig.handler = [&](const HttpRequest&) {
            ++rig.calls;
            HttpResponse resp;
            resp.content_type = "application/octet-stream";
            resp.body = big;
            return resp;
        };
        {
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(make_request("GET", "/big"));
            rig.pump_n(20);                                                              // the reader is slow: the buffers fill, the server waits
            const Reply r = rig.read_reply(c, 5000);
            ASSERT_TRUE(r.complete);
            ASSERT_EQ(r.status, 200);
            ASSERT_EQ(r.body.size(), big.size());
            ASSERT_TRUE(r.body == big);
        }
        {   // small buffers on both sides keep the answer stuck in the server's write: the system's own can take all of it (Linux's take 4 MB at once),
            // or more of it later (macOS grows them), and a write that finished was dropped kLingerMs after it, or never once the clock stood still
            rig.server->set_send_buffer_bytes(16 * 1024);
            Client c;
            c.set_receive_buffer(16 * 1024);
            rig.now = 100000;
            ASSERT_TRUE(rig.connect(c));
            const int before = rig.calls;
            c.send_all(make_request("GET", "/big"));
            ASSERT_TRUE(rig.wait_calls(before + 1));                                     // answered (and stuck in the write) at 100000
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now += HttpServer::kLingerMs;                                            // a finished answer would be dropped now, a stuck one is not
            rig.pump_n(5);
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now = 100000 + HttpServer::kWriteTimeoutMs - 1;
            rig.pump_n(5);
            ASSERT_EQ(rig.server->connection_count(), 1u);
            rig.now += 1;                                                                // dropped kWriteTimeoutMs after the answer
            ASSERT_TRUE(rig.wait_count(0));
        }
    } TEST_END();

    TEST_CASE("CTL2.20 HTTP: garbage on real connections never stops the server") {
        Rig rig;
        ASSERT_TRUE(rig.ok());
        Rng r(0xBAD5EED);
        const std::vector<std::string> seeds = {make_request("GET", "/rooms/ABCD"), make_request("POST", "/rooms", "{\"seats\":4}"), "GET /healthz HTTP/1.1\r\n\r\n",
                                                make_request("DELETE", "/rooms/ABCD?x=1", "", "Expect: 100-continue\r\n")};
        for (int i = 0; i < 300; ++i) {
            std::string data;
            if (r.chance(40)) {
                const uint32_t n = r.below(300);
                for (uint32_t k = 0; k < n; ++k) data.push_back(static_cast<char>(r.below(256)));
            } else {
                data = seeds[r.below(static_cast<uint32_t>(seeds.size()))];
                const uint32_t edits = 1 + r.below(5);
                for (uint32_t e = 0; e < edits && !data.empty(); ++e) {
                    const uint32_t at = r.below(static_cast<uint32_t>(data.size()));
                    switch (r.below(4)) {
                        case 0: data[at] = static_cast<char>(r.below(256)); break;
                        case 1: data.erase(at, 1 + r.below(6)); break;
                        case 2: data.insert(at, 1, "\r\n :,/?#%0"[r.below(10)]); break;
                        default: data.resize(at); break;
                    }
                }
            }
            Client c;
            ASSERT_TRUE(rig.connect(c));
            c.send_all(data);
            rig.pump_n(2);
            if (r.chance(30)) c.abort_connection();
            else if (r.chance(50)) c.shutdown_write();
            rig.pump_n(2);
            std::string raw;
            while (c.recv_some(raw) > 0) {}
            if (!raw.empty()) {                                                          // whatever it answered is a well formed response
                ASSERT_MSG(raw.compare(0, 9, "HTTP/1.1 ") == 0, "an answer starts with the status line: " + shown(data) + " -> " + shown(raw));
                const Reply a = parse_reply(raw);
                ASSERT_MSG(a.status >= 100 && a.status <= 599, "a sane status: " + shown(raw));
                ASSERT_MSG(a.header("content-length") != nullptr || a.status == 204 || a.status == 100, "a length: " + shown(raw));
            }
            c.close();
        }
        ASSERT_TRUE(rig.wait_count(0));
        const Reply ok = rig.exchange(make_request("GET", "/still-alive"));
        ASSERT_EQ(ok.status, 200);
        ASSERT_EQ(rig.seen.back().path, "/still-alive");
    } TEST_END();

    TEST_CASE("CTL2.21 HTTP: 100,000 random and mutated request heads parse to a request that is safe to use, or are refused") {
        const std::vector<std::string> seeds = {
            "GET /rooms/ABCD HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer s\r\n\r\n",
            "POST /rooms?x=1&y=2 HTTP/1.1\r\nHost: a\r\nContent-Length: 12\r\nContent-Type: application/json\r\nX-A: b\r\n\r\n",
            "DELETE /rooms/ABCD HTTP/1.0\r\nExpect: 100-continue\r\nX-Multi: 1\r\nX-Multi: 2\r\n\r\n",
            "GET / HTTP/1.1\r\n\r\n",
            "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
        };
        const std::string alphabet = "\r\n :,/?#%\tabcXYZ019-_.GETPOSDLHTP";
        Rng r(0x4EAD);
        int accepted = 0;
        for (int i = 0; i < 100000; ++i) {
            std::string head = seeds[r.below(static_cast<uint32_t>(seeds.size()))];
            const uint32_t edits = r.below(4);
            for (uint32_t e = 0; e < edits && !head.empty(); ++e) {
                const uint32_t at = r.below(static_cast<uint32_t>(head.size()));
                switch (r.below(5)) {
                    case 0: head[at] = alphabet[r.below(static_cast<uint32_t>(alphabet.size()))]; break;
                    case 1: head[at] = static_cast<char>(r.below(256)); break;
                    case 2: head.erase(at, 1 + r.below(5)); break;
                    case 3: head.insert(at, 1, alphabet[r.below(static_cast<uint32_t>(alphabet.size()))]); break;
                    default: head.insert(at, head.substr(at, 1 + r.below(10))); break;
                }
            }
            if (r.chance(10)) head = std::string(r.below(20), static_cast<char>(r.below(256)));
            HttpHead h;
            std::string error;
            const int status = parse_http_head(head, h, &error);
            if (status != 0) {
                ASSERT_MSG(status == 400 || status == 431 || status == 505, "a refusal status: " + std::to_string(status));
                ASSERT_MSG(!error.empty(), "an error text: " + shown(head));
                continue;
            }
            ++accepted;
            const HttpRequest& q = h.request;
            ASSERT_MSG(!q.method.empty() && q.method.find_first_of(" \r\n\t") == std::string::npos, "method: " + shown(head));
            ASSERT_MSG(!q.path.empty() && q.path[0] == '/', "path: " + shown(head));
            ASSERT_MSG(q.path.find_first_of(" \r\n\t#?") == std::string::npos && q.query.find_first_of(" \r\n\t#") == std::string::npos, "target: " + shown(head));
            for (const auto& kv : q.headers) {
                ASSERT_MSG(!kv.first.empty() && kv.first.find_first_of(" \r\n\t:") == std::string::npos, "header name: " + shown(head));
                ASSERT_MSG(std::none_of(kv.first.begin(), kv.first.end(), [](char c) { return c >= 'A' && c <= 'Z'; }), "lower case name: " + shown(head));
                ASSERT_MSG(kv.second.find_first_of("\r\n") == std::string::npos, "header value: " + shown(head));
                ASSERT_MSG(kv.second.empty() || (kv.second.front() != ' ' && kv.second.front() != '\t' && kv.second.back() != ' ' && kv.second.back() != '\t') ||
                               q.headers.count(kv.first) > 1 || kv.second.find(", ") != std::string::npos,
                           "trimmed value: " + shown(head));
            }
            ASSERT_MSG(!h.has_content_length || h.content_length <= (size_t{1} << 30), "length: " + shown(head));
            if (h.body_status == 0 && h.has_content_length) ASSERT_MSG(h.content_length <= HttpServer::kMaxBodyBytes, "length within the limit: " + shown(head));
            if (h.body_status != 0) ASSERT_MSG(h.body_error != nullptr && h.body_error[0] != '\0', "body error text: " + shown(head));
            ASSERT_MSG(h.body_status == 0 || h.body_status == 411 || h.body_status == 413 || h.body_status == 417, "body status: " + shown(head));
        }
        ASSERT_TRUE(accepted > 1000);
    } TEST_END();

    TEST_CASE("CTL2.22 HTTP public door (listen_public, the replays' list behind the site): no secret is asked, a plain GET reaches the handler whoever sends it, /healthz is the server's own answer, every other method gets 405 with Allow: GET, a request with a body is refused") {
        Rig rig(true);
        ASSERT_TRUE(rig.ok());
        Reply r = rig.exchange("GET /replays HTTP/1.1\r\nHost: x\r\n\r\n");
        ASSERT_TRUE(r.complete && r.status == 200 && r.body == "{\"handled\":true}");
        ASSERT_TRUE(r.header("cache-control") != nullptr && *r.header("cache-control") == "no-store" && r.header("www-authenticate") == nullptr);
        ASSERT_EQ(rig.calls, 1);
        ASSERT_TRUE(rig.seen[0].method == "GET" && rig.seen[0].path == "/replays" && rig.seen[0].query.empty());
        r = rig.exchange("GET /replays/ants-X-20261008-143209Z.antsrep?limit=5 HTTP/1.1\r\nAuthorization: Bearer nonsense\r\n\r\n");      // (a header that means nothing here)
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(rig.calls == 2 && rig.seen[1].path == "/replays/ants-X-20261008-143209Z.antsrep" && rig.seen[1].query == "limit=5");
        r = rig.exchange(make_request("GET", "/replays"));                                                      // (and the secret of the other door is no key to anything)
        ASSERT_TRUE(r.status == 200 && rig.calls == 3);
        // /healthz is answered by the server itself
        r = rig.exchange("GET /healthz HTTP/1.1\r\n\r\n");
        ASSERT_TRUE(r.status == 200 && r.body == "{\"ok\":true}" && rig.calls == 3);
        // nothing but GET gets as far as the handler
        for (const char* method : {"POST", "DELETE", "PUT", "PATCH", "HEAD", "OPTIONS", "TRACE", "CONNECT", "FOO"}) {
            r = rig.exchange(std::string(method) + " /replays HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
            ASSERT_MSG(r.complete && r.status == 405, std::string("405 for ") + method + " (got " + std::to_string(r.status) + ")");
            ASSERT_MSG(r.header("allow") != nullptr && *r.header("allow") == "GET", method);
            ASSERT_MSG(r.body == "{\"error\":\"method not allowed\"}", method);
            r = rig.exchange(std::string(method) + " /healthz HTTP/1.1\r\n\r\n");                              // (not even the server's own path, for a method that is not GET)
            ASSERT_MSG(r.status == 405, method);
        }
        for (const char* method : {"G(T", "GE T", "get"}) ASSERT_EQ(rig.exchange(std::string(method) + " /replays HTTP/1.1\r\n\r\n").status, method == std::string("get") ? 405 : 400);
        ASSERT_EQ(rig.calls, 3);
        // a body: refused, never read, never handed over
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello").status, 400);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n").status, 411);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: 70000\r\n\r\n").status, 413);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: x\r\n\r\n").status, 400);
        ASSERT_EQ(rig.calls, 3);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\nContent-Length: 0\r\n\r\n").status, 200);           // (an empty body is none)
        ASSERT_EQ(rig.calls, 4);
        // a head that is not well formed is refused like at the other door
        ASSERT_EQ(rig.exchange("GET /replays HTTP/2.0\r\n\r\n").status, 505);
        ASSERT_EQ(rig.exchange("GET replays HTTP/1.1\r\n\r\n").status, 400);
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\nHost: x\n\n").status, 400);
        ASSERT_EQ(rig.calls, 4);
        // a handler that throws, or answers with a status that is none, is an internal error here too
        rig.handler = [](const HttpRequest&) -> HttpResponse { throw std::runtime_error("boom"); };
        r = rig.exchange("GET /replays HTTP/1.1\r\n\r\n");
        ASSERT_TRUE(r.status == 500 && r.body == "{\"error\":\"internal error\"}");
        rig.handler = [](const HttpRequest&) {
            HttpResponse resp;
            resp.status = 99;
            return resp;
        };
        ASSERT_EQ(rig.exchange("GET /replays HTTP/1.1\r\n\r\n").status, 500);
        ASSERT_TRUE(rig.wait_count(0));
    } TEST_END();

    TEST_CASE("CTL2.23 HTTP public door: the port is exclusive like the other door's, the loopback address is the default and only that; a door open to the network says so explicitly") {
        auto door = HttpServer::listen_public(0, true);
        ASSERT_TRUE(door != nullptr && door->port() != 0);
        const uint16_t port = door->port();
        ASSERT_TRUE(HttpServer::listen_public(port, true) == nullptr);
        ASSERT_TRUE(HttpServer::listen(port, "another-secret", true) == nullptr);
        door.reset();
        auto again = HttpServer::listen_public(port, true);
        ASSERT_TRUE(again != nullptr && again->port() == port);
        again.reset();
        auto open_door = HttpServer::listen_public(0, false);
        ASSERT_TRUE(open_door != nullptr && open_door->port() != 0);
        Rig rig(true);
        ASSERT_TRUE(rig.ok());
        Client c;
        ASSERT_TRUE(rig.connect(c));                                                                            // (the loopback address answers)
        ASSERT_TRUE(rig.wait_count(1));
        const std::string other = other_local_address();
        if (other.empty()) {
            std::cout << "(no other network interface here, the check of the other address skipped) " << std::flush;
            return;
        }
        Client remote;                                                                                          // (not `far`: that is an empty macro in the Windows headers)
        ASSERT_FALSE(remote.connect_to(rig.port(), other.c_str()));                                             // the default door: the machine's other address is refused ...
        Client reach;
        ASSERT_TRUE(reach.connect_to(open_door->port(), other.c_str()));                                        // ... and the door that was opened to the network answers on it
    } TEST_END();

    // ---- the door of the map submissions (listen_door): no secret, GET and POST, a body up to the door's own limit that the handler may lower on the head ----
    TEST_CASE("CTL2.24 HTTP door (listen_door): a POST body reaches the handler with the peer's address, GET and POST only, a POST needs its length, a chunked body and a body on a GET are refused") {
        Rig rig(true);
        DoorOptions options;
        options.max_body_bytes = 1024 * 1024;
        rig.server = HttpServer::listen_door(0, options, true);
        ASSERT_TRUE(rig.ok());
        const std::string body = body_pattern(100 * 1000);
        Reply r = rig.exchange("POST /submissions?kind=classic HTTP/1.1\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
        ASSERT_TRUE(r.complete && r.status == 200 && rig.calls == 1);
        ASSERT_TRUE(rig.seen[0].method == "POST" && rig.seen[0].path == "/submissions" && rig.seen[0].query == "kind=classic" && rig.seen[0].body == body);
        ASSERT_EQ(rig.seen[0].peer, std::string("127.0.0.1"));
        ASSERT_TRUE(rig.seen[0].header("content-type") != nullptr && *rig.seen[0].header("content-type") == "application/octet-stream");
        ASSERT_EQ(rig.exchange("GET /submissions HTTP/1.1\r\nAuthorization: Bearer abc\r\n\r\n").status, 200);
        ASSERT_EQ(rig.calls, 2);
        for (const char* method : {"PUT", "DELETE", "PATCH", "HEAD", "OPTIONS", "TRACE", "CONNECT"}) {
            r = rig.exchange(std::string(method) + " /submissions HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
            ASSERT_MSG(r.status == 405 && r.header("allow") != nullptr && *r.header("allow") == "GET, POST", method);
        }
        ASSERT_EQ(rig.exchange("POST /submissions HTTP/1.1\r\n\r\n").status, 411);                                  // (a POST without a length)
        ASSERT_EQ(rig.exchange("POST /submissions HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n").status, 411);
        ASSERT_EQ(rig.exchange("GET /submissions HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello").status, 400);          // (a GET has no body)
        ASSERT_EQ(rig.exchange("POST /submissions HTTP/1.1\r\nContent-Length: 0\r\n\r\n").status, 200);              // (an empty body is none: the handler judges it)
        ASSERT_EQ(rig.calls, 3);
        r = rig.exchange("GET /healthz HTTP/1.1\r\n\r\n");
        ASSERT_TRUE(r.status == 200 && r.body == "{\"ok\":true}" && rig.calls == 3);
    } TEST_END();

    TEST_CASE("CTL2.25 HTTP door: a Content-Length above the limit is answered 413 on the head and the body is never read; the handler's own limit for a request can only lower the door's") {
        Rig rig(true);
        DoorOptions options;
        options.max_body_bytes = 600 * 1000;
        options.body_limit = [](const HttpRequest& head) { return head.path == "/small" ? size_t{1000} : size_t{1} << 40; };     // (asked for more than the door has: the door's limit still holds)
        rig.server = HttpServer::listen_door(0, options, true);
        ASSERT_TRUE(rig.ok());
        // only the head is sent: the answer comes at once, with no body waited for
        Client c;
        ASSERT_TRUE(rig.connect(c));
        c.send_all("POST /small HTTP/1.1\r\nContent-Length: 1001\r\n\r\n");
        Reply r = rig.read_reply(c);
        ASSERT_TRUE(r.complete && r.status == 413 && rig.calls == 0);
        Client d;
        ASSERT_TRUE(rig.connect(d));
        d.send_all("POST /big HTTP/1.1\r\nContent-Length: 600001\r\n\r\n");
        r = rig.read_reply(d);
        ASSERT_TRUE(r.complete && r.status == 413 && rig.calls == 0);
        ASSERT_EQ(rig.exchange("POST /big HTTP/1.1\r\nContent-Length: 4294967296\r\n\r\n").status, 413);
        ASSERT_EQ(rig.exchange("POST /small HTTP/1.1\r\nContent-Length: 1000\r\n\r\n" + std::string(1000, 'x')).status, 200);
        ASSERT_EQ(rig.exchange("POST /big HTTP/1.1\r\nContent-Length: 600000\r\n\r\n" + std::string(600000, 'y')).status, 200);
        ASSERT_EQ(rig.calls, 2);
    } TEST_END();

    TEST_CASE("CTL2.26 HTTP door: only max_large_bodies bodies above 64 KB are read at once (the next is answered 503 on its head), a body that does not come in time is 408, bad options make no door") {
        Rig rig(true);
        DoorOptions options;
        options.max_body_bytes = 300 * 1000;
        options.max_large_bodies = 1;
        options.request_timeout_ms = 3000;
        rig.server = HttpServer::listen_door(0, options, true);
        ASSERT_TRUE(rig.ok());
        const std::string body = body_pattern(100 * 1000);
        const std::string head = "POST /x HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        Client slow;
        ASSERT_TRUE(rig.connect(slow));
        slow.send_all(head + body.substr(0, 10 * 1000));                                                         // (a large body that is half way)
        rig.pump_n(20);
        Reply busy = rig.exchange(head + body);
        ASSERT_TRUE(busy.status == 503 && rig.calls == 0);                                                       // the second one is turned away on its head
        slow.send_all(body.substr(10 * 1000));
        Reply done = rig.read_reply(slow);
        ASSERT_TRUE(done.status == 200 && rig.calls == 1);
        ASSERT_EQ(rig.exchange(head + body).status, 200);                                                        // (the place is free again)
        slow.close();
        Client stalled;
        ASSERT_TRUE(rig.connect_accepted(stalled));
        stalled.send_all(head + body.substr(0, 1000));
        rig.pump_n(5);
        rig.now += 3001;                                                                                         // the clock of the test: the time of the whole request is over
        Reply late = rig.read_reply(stalled);
        ASSERT_EQ(late.status, 408);
        DoorOptions bad;
        bad.max_body_bytes = 0;
        ASSERT_TRUE(HttpServer::listen_door(0, bad, true) == nullptr);
        bad.max_body_bytes = size_t{1} << 29;
        ASSERT_TRUE(HttpServer::listen_door(0, bad, true) == nullptr);
        bad = DoorOptions();
        bad.max_large_bodies = 0;
        ASSERT_TRUE(HttpServer::listen_door(0, bad, true) == nullptr);
    } TEST_END();

    TEST_CASE("CTL2.27 HTTP door: an answer is at most the door's max_response_bytes (a map file is bigger than the control interface's 1 MiB)") {
        Rig rig(true);
        DoorOptions options;
        options.max_response_bytes = 3 * 1024 * 1024;
        rig.server = HttpServer::listen_door(0, options, true);
        ASSERT_TRUE(rig.ok());
        rig.handler = [](const HttpRequest&) {
            HttpResponse resp;
            resp.content_type = "application/octet-stream";
            resp.body = std::string(2 * 1024 * 1024, 'm');
            return resp;
        };
        const Reply big = rig.exchange("GET /f HTTP/1.1\r\n\r\n");
        ASSERT_TRUE(big.status == 200 && big.body.size() == 2 * 1024 * 1024);
        Rig plain(true);
        ASSERT_TRUE(plain.ok());
        plain.handler = rig.handler;
        ASSERT_EQ(plain.exchange("GET /f HTTP/1.1\r\n\r\n").status, 500);                                       // (the public door keeps its 1 MiB)
    } TEST_END();
}

#pragma once

// The HTTP side of the control interface of the game server: a minimal HTTP/1.1 server with which a lobby backend creates and inspects game rooms
// (POST /rooms, GET /rooms/<code>, DELETE /rooms/<code>; the routes and what they mean are the business of the code that supplies the handler, this
// file only carries requests to it and answers back). Native builds only, like the sockets of ants_net.
//
// How a backend calls it (the secret here is made up; the port is whatever the server was started with):
//
//     curl -s http://127.0.0.1:4100/healthz                                   # {"ok":true}, no secret needed (a Docker HEALTHCHECK)
//     curl -s -X POST http://127.0.0.1:4100/rooms -H "Authorization: Bearer example-secret-0123456789" -H "Content-Type: application/json" -d '{"seats":4}'
//                                                                              # (the body is whatever the room logic defines)
//     curl -s http://127.0.0.1:4100/rooms/ABCD -H "Authorization: Bearer example-secret-0123456789"
//     curl -s -X DELETE http://127.0.0.1:4100/rooms/ABCD -H "Authorization: Bearer example-secret-0123456789"
//
// It is a door into the game server, so it is small and strict rather than complete:
//  (listen_public() makes the same server without a secret, for pages that are public and read only: see there. Everything below is about the one with the secret.)
//  - It listens on 127.0.0.1 only (`loopback_only`, the default); a deployment whose backend is on another machine or container says so explicitly.
//    A server without a secret refuses to start: listen() returns nullptr for an empty secret, or one with spaces or control characters (it could
//    not be sent in a header). Use a long random secret (32 or more characters); there is no rate limit, the loopback address and the secret are
//    the protection.
//  - Every request except `GET /healthz` needs `Authorization: Bearer <secret>` (the word Bearer in any case). The comparison takes the same time
//    wherever the two texts differ. A missing or wrong secret gets 401 with `WWW-Authenticate: Bearer`, and the handler is never called for it.
//  - The checks run in this order, and the first that fails answers: the request is well formed (400, 431, 505), the sender is authenticated (401),
//    the method is allowed (405), the body is framed acceptably (411, 413, 417), the body has arrived (408 when it does not in time), and only then
//    the handler is called. So someone without the secret learns nothing about methods, bodies or routes: everything but a malformed request is 401.
//  - Methods are GET, POST and DELETE (anything else gets 405 with an Allow header). Bodies travel with Content-Length only: a POST without it, or
//    any Transfer-Encoding (chunked), gets 411; a body above 64 KB gets 413 (the server answers on the headers and never reads it); the request
//    line and headers together are at most 8 KB and 100 headers (431); a request that is not complete 5 seconds after the connection was accepted
//    gets 408; a request that is not well formed gets 400 (bare LF or CR, control characters, folded headers, a space before the colon, a duplicate
//    Content-Length, Transfer-Encoding, Authorization, Host or Expect, both Content-Length and Transfer-Encoding, a target that is not an absolute
//    path: nothing is guessed or repaired); HTTP versions other than 1.0 and 1.1 get 505; `Expect: 100-continue` is answered, any other expectation
//    gets 417. The path and the query reach the handler as they came, not percent-decoded and not normalized: the router decides what they mean.
//  - At most 32 connections are open at once. A further one takes the slot of the oldest connection that is waiting for its request head and has nothing
//    unread on its socket (such a connection is idle or hostile: a real client sends its request at once; a request that has arrived but not been read yet
//    is never sacrificed), or else of the oldest one that only waits for its client to close after its answer; if every slot is busy answering,
//    receiving a body or has a request waiting, the further connection is accepted and closed at once. Every connection carries one request: the answer has
//    `Connection: close`, and whatever follows the first request on the same connection (a pipelined second one) is ignored.
//  - The server's own answers are JSON `{"error":"..."}` (a fixed text, never an echo of the request), with an exact Content-Length, `Content-Type:
//    application/json` and `Cache-Control: no-store`. No header is built from request data, and a handler's content type with a control character
//    in it is replaced, so a request cannot split a response.
//  - Not provided, on purpose: TLS (the traffic never leaves the machine; a deployment that opens the port to a network puts a TLS proxy in front),
//    keep-alive, HEAD, and any protection against a program that keeps opening connections faster than real requests can finish (each new connection evicts one
//    that is still waiting for its head; the loopback address and the secret are the protection).
//
// Like everything in the game's network code nothing blocks and nothing runs in a thread: the program calls update() from its main loop with a
// millisecond clock, and update() accepts, reads, parses, calls the handler (synchronously, for a request that is complete and authenticated), writes
// and closes, as far as the sockets allow right now. The clock is the caller's, so a test drives time without sleeping.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace ants::ctl {

struct HttpRequest {
    std::string method;                                        // "GET", "POST" or "DELETE"
    std::string path;                                          // the target before '?', as it came (not percent-decoded): "/rooms/ABCD"
    std::string query;                                         // after the first '?', without it (not decoded); empty when there is none
    std::map<std::string, std::string, std::less<>> headers;   // names in lower case, values without the white space around them
    std::string body;                                          // exactly Content-Length bytes (empty without one)

    /// The value of a header (the name in lower case), or nullptr
    const std::string* header(std::string_view lower_case_name) const {
        const auto it = headers.find(lower_case_name);
        return it == headers.end() ? nullptr : &it->second;
    }
};

struct HttpResponse {
    int status{200};                                           // 200 - 599; anything else is answered as 500
    std::string content_type{"application/json"};              // visible ASCII only, else replaced by application/json
    std::string body;                                          // at most HttpServer::kMaxResponseBytes, else 500
};

class HttpServer {
public:
    static constexpr size_t kMaxHeadBytes = 8 * 1024;          // request line and headers, with the blank line
    static constexpr size_t kMaxHeaders = 100;
    static constexpr size_t kMaxBodyBytes = 64 * 1024;
    static constexpr size_t kMaxResponseBytes = 1024 * 1024;
    static constexpr size_t kMaxConnections = 32;
    static constexpr uint32_t kRequestTimeoutMs = 5000;        // from accept to a complete request (head and body)
    static constexpr uint32_t kWriteTimeoutMs = 5000;          // a client that does not take its answer
    static constexpr uint32_t kLingerMs = 2000;                // after the answer the server waits this long for the client to close first

    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    /// Listens on `port` (0 = any free port, see port()); `loopback_only` binds 127.0.0.1, otherwise every interface. nullptr on failure, and when
    /// `bearer_secret` is empty or contains anything but visible ASCII (no spaces, no control characters).
    static std::unique_ptr<HttpServer> listen(uint16_t port, std::string bearer_secret, bool loopback_only = true);

    /// A door that needs no secret and can only read, for the pages of the game server that are public (the list of the replays that it keeps, behind the site's reverse proxy). It has the same parser, limits and
    /// timeouts as listen(), but a request is answered by the handler whoever sends it: only GET is allowed (anything else is 405, `Allow: GET`), a request with a body is refused and the body is never read (400
    /// for a Content-Length above 0; 411 for a chunked one, 413 for one that is too large, 417 for an `Expect` that is not 100-continue), and `GET /healthz` is answered by the server itself as before. The handler has no authority to check: it must decide for itself what may be seen, and change nothing. nullptr on failure.
    static std::unique_ptr<HttpServer> listen_public(uint16_t port, bool loopback_only = true);

    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    uint16_t port() const;

    /// Accepts, reads, answers and closes whatever the sockets allow right now; never blocks. `handler` is called, in this call, once for each
    /// request that is complete, well formed and authenticated (GET /healthz excepted: the server answers that itself); what it returns is sent.
    /// A handler that throws, or an empty one, makes the answer 500. `now_ms` is a monotonic millisecond clock (wrapping at 2^32 is fine).
    void update(uint32_t now_ms, const std::function<HttpResponse(const HttpRequest&)>& handler);

    /// Connections that are open now (diagnostics and tests)
    size_t connection_count() const;
    /// Changes the time a request may take from accept to complete (tests; the default is kRequestTimeoutMs)
    void set_request_timeout_ms(uint32_t ms);
    /// Gives each connection accepted from now on a kernel send buffer of `bytes` (tests: a client that never reads then fills it at once, whatever
    /// the system's buffers are; 0, the default, keeps the system's)
    void set_send_buffer_bytes(int bytes);

private:
    struct Impl;
    explicit HttpServer(std::unique_ptr<Impl> impl);
    static std::unique_ptr<HttpServer> open(uint16_t port, std::string bearer_secret, bool loopback_only, bool read_only);
    std::unique_ptr<Impl> impl_;
};

// ---- what the server is made of, exposed so that the tests can reach it without a socket ----

/// What the head of a request says (see parse_http_head)
struct HttpHead {
    HttpRequest request;                 // method, path, query and headers; the body is empty
    size_t content_length{0};            // valid when has_content_length
    bool has_content_length{false};
    bool expect_continue{false};         // "Expect: 100-continue"
    int body_status{0};                  // 0, or the status the framing of the body deserves: 411 (length required), 413 (too large), 417 (expectation)
    const char* body_error{""};          // the text for body_status
};

/// Parses the request line and the headers: `head` is everything up to and including the blank line (CRLF CRLF). Returns 0 when it is well formed
/// (`out` is filled), else the status to answer with (400 malformed, 431 too large, 505 version) and a fixed text in `error` (when not null).
/// Whether the method is allowed and whether the sender is authenticated are not asked here.
int parse_http_head(std::string_view head, HttpHead& out, std::string* error);

/// True when the two texts are equal; the time depends on the longer of them and not on where they differ
bool constant_time_equal(std::string_view a, std::string_view b);

}  // namespace ants::ctl

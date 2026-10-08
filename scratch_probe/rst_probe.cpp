// A probe, on a scratch branch only: does a TCP reset discard the bytes that the receiver has not read yet?
//
// S1 "the closer had unread data": the server writes a byte to the client, which never reads it; the client then writes "LEAVE" and closes. Closing a socket with unread data resets it
//    (RST instead of FIN). After 50 ms the server reads: does it get LEAVE, or an error?
// S2 "the answer to a closed link": the client writes "PING" and "LEAVE" in two writes and closes (no unread data, so FIN). The server reads only the first 4 bytes (PING) and answers
//    PONG to the closed client, which answers with a reset. After 50 ms the server reads again: does it still get LEAVE, which had arrived before the reset, or an error?
// S3 "the same with the reset before the read of PING": as S2, but the server waits 50 ms before its first read.
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <string>
#include <thread>

namespace {

int listener(uint16_t& port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(fd, 8) != 0) return -1;
    socklen_t len = sizeof a;
    getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
    return fd;
}

int connect_to(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return -1;
    return fd;
}

// what the server gets when it reads the rest: "LEAVE", "eof" (a clean end without it), or the error
std::string read_rest(int fd) {
    std::string got;
    char buf[64];
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n > 0) {
            got.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) return got.empty() ? "eof" : got + "+eof";
        return got.empty() ? std::string("error:") + strerror(errno) : got + "+error:" + strerror(errno);
    }
    return got;
}

void ms(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

std::string scenario(int which) {
    uint16_t port = 0;
    const int l = listener(port);
    const int c = connect_to(port);
    const int s = accept(l, nullptr, nullptr);
    std::string result;
    if (which == 1) {
        send(s, "S", 1, 0);
        ms(20);                                   // it is in the client's receive buffer, unread
        send(c, "LEAVE", 5, 0);
        close(c);
        ms(50);
        result = read_rest(s);
    } else {
        send(c, "PING", 4, 0);
        ms(2);
        send(c, "LEAVE", 5, 0);
        close(c);
        ms(20);                                   // both are in the server's receive buffer, with the FIN
        if (which == 3) ms(50);
        char first[4];
        const ssize_t n = recv(s, first, sizeof first, 0);        // PING
        if (n != 4) result = "first read: " + std::to_string(n) + " ";
        send(s, "PONG", 4, 0);                    // to a closed client: it answers with a reset
        ms(50);
        result += read_rest(s);
    }
    close(s);
    close(l);
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    const int rounds = argc > 1 ? std::stoi(argv[1]) : 100;
    for (int which = 1; which <= 3; ++which) {
        int leave = 0;
        int other = 0;
        std::string sample;
        for (int i = 0; i < rounds; ++i) {
            const std::string r = scenario(which);
            if (r.rfind("LEAVE", 0) == 0) ++leave;
            else {
                ++other;
                if (sample.empty()) sample = r;
            }
        }
        std::printf("S%d: %d of %d rounds delivered LEAVE; %d did not (first: %s)\n", which, leave, rounds, other, sample.c_str());
    }
    return 0;
}

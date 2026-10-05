#pragma once

// Where a player's machine keeps the keys of its seats, so that a game that was closed or a page that was reloaded can take its seat in a running match again (docs/NETWORK_PORT.md,
// "What the clients do in release B"). NetGame hands a key out (set_on_key) when the match of a server's room that holds seats has begun (the Start; a rejoin's Welcome says it again) and lets go of
// it (set_on_forget_key) when it is of no more use; the application writes both here, and a join that names the room looks the key up (Application::init, the start menu's "Rejoin your match").
//
// A KEY IS A SECRET: whoever has it can take the seat. It is kept where only its owner can read it (a file of mode 0600; the browser's local storage), and it never appears in a URL, a log
// line, a status text or a test's output.
//
//   desktop  one file, `rejoin.txt`, beside the settings file: a line per key, tab-separated: server, room, seat, key (32 hex digits), written (Unix seconds). Made with mode 0600 on POSIX
//            (the temp file of the atomic write too: opened with 0600, then fchmod); on Windows the per-user folder is private already. At most 8 entries (the newest are kept); an entry
//            older than 3 hours is not given back and is not written again; every write is a temp file that is renamed over the old one.
//   web      localStorage `ants.rejoin.<room>.<seat>` = {"k": hex, "s": server, "t": epoch ms} (the same storage as the settings); entries older than 3 hours are removed when read.
//
// The classes know nothing of sockets, SDL or the window: the tests run them with a clock of their own.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"

namespace ants::app {

/// A key as the store gives it back
struct RejoinEntry {
    std::string server;                // "host:port" of a native client, the URL of the browser build (what RejoinKey::server says; never the token)
    std::string room;                  // the room's code
    uint8_t seat{255};                 // 0 .. 3
    net::SeatKey key{};
    int64_t written_ms{0};             // the wall clock when the Welcome gave it (a file keeps whole seconds)
};

/// An entry is of no use after this long: a match of a public server is over well within it (a demo room's play is 30 minutes at the most, its pauses 10), and a key that outlives its match is a button
/// for a match that is gone (the review: it was 24 hours). The same limit judges an entry from the future (a clock that was set back).
inline constexpr int64_t kRejoinMaxAgeMs = int64_t{3} * 3600 * 1000;
/// The file keeps this many entries (the newest)
inline constexpr size_t kRejoinMaxEntries = 8;

/// The wall clock in milliseconds since 1970 (what the stores are given by default)
int64_t wall_clock_ms();

/// The text of a server as a key's entry names it: "host:port" ("[v6]:port" for an IPv6 address) for a native client, the URL for the browser build. The same text as NetGame's JoinTarget gives
/// RejoinKey::server (a test holds the two together).
std::string rejoin_server_text(const std::string& address, uint16_t port, const std::string& url);

/// A key as 32 lower-case hex digits, and back (false for anything else: wrong length, other characters, all zero)
std::string rejoin_key_hex(const net::SeatKey& key);
bool rejoin_key_from_hex(const std::string& hex, net::SeatKey& out);

/// What can be stored: a room of the server's own rules (1 - 32 letters, digits, '-' and '_'), a seat 0 .. 3, a key that is not zero, a server of 1 - 255 printable ASCII characters
bool rejoin_storable(const net::RejoinKey& key);

class RejoinStore {
public:
    virtual ~RejoinStore() = default;
    /// Keeps the key, replacing the entry of the same server, room and seat (its time is now). False when it cannot be kept (a key that is not storable, a write that failed).
    virtual bool put(const net::RejoinKey& key) = 0;
    /// Lets go of the entry of that server, room and seat that holds that key: another key of the seat (a newer match of the same room) stays
    virtual void forget(const net::RejoinKey& key) = 0;
    /// The fresh entries (not older than kRejoinMaxAgeMs), newest first
    virtual std::vector<RejoinEntry> entries() = 0;

    /// The newest entry of that server and room (and that seat, unless it is 255)
    std::optional<RejoinEntry> find(const std::string& server, const std::string& room, uint8_t seat = 255);
    /// The newest entry of all
    std::optional<RejoinEntry> newest();
};

/// The keys of a run that has no place to keep them (a headless run without a settings file: the tests') and the common rules of the file: replace, order, cap, age
class MemoryRejoinStore final : public RejoinStore {
public:
    explicit MemoryRejoinStore(std::function<int64_t()> clock = wall_clock_ms) : clock_(std::move(clock)) {}
    bool put(const net::RejoinKey& key) override;
    void forget(const net::RejoinKey& key) override;
    std::vector<RejoinEntry> entries() override;

private:
    std::function<int64_t()> clock_;
    std::vector<RejoinEntry> kept_;
};

/// The desktop's file (see the top of this file). A line that is not an entry is ignored, and so is every entry older than kRejoinMaxAgeMs; nothing is written by a read.
class FileRejoinStore final : public RejoinStore {
public:
    explicit FileRejoinStore(std::string path, std::function<int64_t()> clock = wall_clock_ms) : path_(std::move(path)), clock_(std::move(clock)) {}
    bool put(const net::RejoinKey& key) override;
    void forget(const net::RejoinKey& key) override;
    std::vector<RejoinEntry> entries() override;
    const std::string& path() const noexcept { return path_; }
    /// The tests: told the temp file's name when it is complete and before it is renamed over the file (what a second window that reads at that moment sees, and the temp file's mode)
    void set_before_rename_for_test(std::function<void(const std::string&)> fn) { before_rename_ = std::move(fn); }

    /// The file's lines as the store writes them (one entry per line, the newest first) and reads them back: for the tests
    static std::string serialise(const std::vector<RejoinEntry>& entries);
    static std::vector<RejoinEntry> parse(const std::string& text, int64_t now_ms);

private:
    std::vector<RejoinEntry> read(int64_t now_ms) const;
    bool write(const std::vector<RejoinEntry>& entries) const;

    std::string path_;
    std::function<int64_t()> clock_;
    std::function<void(const std::string&)> before_rename_;
};

/// A key-value storage with the shape of the browser's localStorage: the web backend works through it, and the tests give it a map
class KeyValueStorage {
public:
    virtual ~KeyValueStorage() = default;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual bool set(const std::string& key, const std::string& value) = 0;
    virtual void remove(const std::string& key) = 0;
    virtual std::vector<std::string> keys(const std::string& prefix) = 0;
};

class MapStorage final : public KeyValueStorage {
public:
    std::optional<std::string> get(const std::string& key) override;
    bool set(const std::string& key, const std::string& value) override;
    void remove(const std::string& key) override;
    std::vector<std::string> keys(const std::string& prefix) override;
    std::map<std::string, std::string>& items() noexcept { return items_; }

private:
    std::map<std::string, std::string> items_;
};

#if defined(__EMSCRIPTEN__)
/// The browser's window.localStorage (the same access as the settings of the web build: ConfigStore); every call is made under a try, so a browser that has none (a private window) keeps nothing
class BrowserStorage final : public KeyValueStorage {
public:
    std::optional<std::string> get(const std::string& key) override;
    bool set(const std::string& key, const std::string& value) override;
    void remove(const std::string& key) override;
    std::vector<std::string> keys(const std::string& prefix) override;
};
#endif

/// The web backend: `ants.rejoin.<room>.<seat>` holding {"k": hex, "s": server, "t": epoch ms}
class LocalStorageRejoinStore final : public RejoinStore {
public:
    static constexpr const char* kKeyPrefix = "ants.rejoin.";
    LocalStorageRejoinStore(KeyValueStorage& storage, std::function<int64_t()> clock = wall_clock_ms) : storage_(storage), clock_(std::move(clock)) {}
    bool put(const net::RejoinKey& key) override;
    void forget(const net::RejoinKey& key) override;
    std::vector<RejoinEntry> entries() override;

    static std::string storage_key(const std::string& room, uint8_t seat);
    static std::string value_of(const RejoinEntry& entry);
    static bool parse_value(const std::string& text, std::string& hex, std::string& server, int64_t& written_ms);

private:
    std::vector<RejoinEntry> read_all();                                   // every fresh entry, newest first (too old: removed)

    KeyValueStorage& storage_;
    std::function<int64_t()> clock_;
};

}  // namespace ants::app

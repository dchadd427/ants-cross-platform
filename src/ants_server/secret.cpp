#include "ants_server/secret.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
// windows.h first
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ants::server {

bool random_bytes(uint8_t* out, size_t n) {
    if (n == 0) return true;
    if (out == nullptr) return false;
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, out + got, n - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            ::close(fd);
            return false;
        }
        got += static_cast<size_t>(r);
    }
    ::close(fd);
    return true;
#endif
}

namespace {

constexpr size_t kMinSecret = 32;
constexpr size_t kMaxSecret = 256;
constexpr size_t kMaxFile = 1024;       // a secret file is one short line: anything bigger is not one (and never more than this + 1 byte of it is read)

std::string to_hex(const uint8_t* bytes, size_t n) {
    static const char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        text.push_back(kHex[bytes[i] >> 4]);
        text.push_back(kHex[bytes[i] & 15u]);
    }
    return text;
}

#ifndef _WIN32
std::string error_text(int code) { return std::error_code(code, std::generic_category()).message(); }
#endif

// At most kMaxFile + 1 bytes of the file, never more (a bigger file is refused without being read): false and the reason when it cannot be read or is no regular file.
bool read_head(const std::string& path, std::string& text, std::string& why) {
#ifdef _WIN32
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        why = "cannot be read";
        return false;
    }
    text.assign(kMaxFile + 1, '\0');
    in.read(&text[0], static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    return true;
#else
    // O_NONBLOCK: opening a FIFO that nobody writes to would wait for ever; the check of the descriptor (not of the name) cannot be fooled by a swap after the look
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        why = "cannot be read (" + error_text(errno) + ")";
        return false;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        why = "is not a regular file";
        return false;
    }
    text.assign(kMaxFile + 1, '\0');
    size_t got = 0;
    while (got < text.size()) {
        const ssize_t r = ::read(fd, &text[got], text.size() - got);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) {
            const int e = errno;
            ::close(fd);
            why = "cannot be read (" + error_text(e) + ")";
            return false;
        }
        if (r == 0) break;
        got += static_cast<size_t>(r);
    }
    ::close(fd);
    text.resize(got);
    return true;
#endif
}

// The text of a secret file without the line end (and without trailing blanks, which an editor may add); false and the reason when it is not a usable secret.
bool read_secret_file(const std::string& path, std::string& secret, std::string& why) {
    std::string text;
    if (!read_head(path, text, why)) return false;
    if (text.size() > kMaxFile) {
        why = "is too big to be a secret";
        return false;
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) text.pop_back();
    if (!usable_secret_text(text)) {
        why = "does not hold a usable secret (one line of 32 to 256 visible characters, no spaces)";
        return false;
    }
    secret = std::move(text);
    return true;
}

SecretResult failure(const std::string& path, const std::string& what) {
    SecretResult r;
    r.path = path;
    r.error = what;
    return r;
}

// A name for the temporary file next to the final one: random, so that a stale one of a crashed start (a server in a container is process 1 every time) never blocks the next.
bool temporary_name(const std::string& path, std::string& out) {
    uint8_t bytes[8];
    if (!random_bytes(bytes, sizeof bytes)) return false;
    out = path + "." + to_hex(bytes, sizeof bytes) + ".tmp";
    return true;
}

// Makes the file with a new secret, all at once: the complete secret is written to a temporary file in the same folder, which is then given the final name by a step that
// fails when the name is taken (so nobody ever sees half a secret, and the loser of a race reads the winner's complete file). The file is for its owner only on POSIX; on
// Windows it takes the folder's permissions. 1: made, 0: it exists now (somebody else made it first), -1: could not (`why` says why).
int create_secret_file(const std::string& path, const std::string& secret, std::string& why) {
    const std::string line = secret + "\n";
    std::string tmp;
#ifdef _WIN32
    const fs::path final_path(path);
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 8 && h == INVALID_HANDLE_VALUE; ++tries) {
        if (!temporary_name(path, tmp)) {
            why = "cannot be created (no random numbers for a temporary name)";
            return -1;
        }
        h = CreateFileW(fs::path(tmp).wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD opened = GetLastError();
            if (opened != ERROR_FILE_EXISTS && opened != ERROR_ALREADY_EXISTS) {        // a name that is taken: try another one
                why = "cannot be created (Windows error " + std::to_string(static_cast<unsigned long>(opened)) + ")";
                return -1;
            }
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        why = "cannot be created (no free temporary name)";
        return -1;
    }
    size_t done = 0;
    bool good = true;
    while (done < line.size()) {
        DWORD written = 0;
        if (!WriteFile(h, line.data() + done, static_cast<DWORD>(line.size() - done), &written, nullptr) || written == 0) {
            good = false;
            break;
        }
        done += static_cast<size_t>(written);
    }
    if (good && !FlushFileBuffers(h)) good = false;
    if (!CloseHandle(h)) good = false;
    const std::wstring wtmp = fs::path(tmp).wstring();
    if (!good) {
        DeleteFileW(wtmp.c_str());
        why = "cannot be written";
        return -1;
    }
    // no MOVEFILE_REPLACE_EXISTING: the move fails when the name is taken
    if (MoveFileExW(wtmp.c_str(), final_path.wstring().c_str(), MOVEFILE_WRITE_THROUGH)) return 1;
    const DWORD moved = GetLastError();
    DeleteFileW(wtmp.c_str());
    if (moved == ERROR_ALREADY_EXISTS || moved == ERROR_FILE_EXISTS) return 0;
    why = "cannot be created (Windows error " + std::to_string(static_cast<unsigned long>(moved)) + ")";
    return -1;
#else
    int fd = -1;
    for (int tries = 0; tries < 8 && fd < 0; ++tries) {
        if (!temporary_name(path, tmp)) {
            why = "cannot be created (no random numbers for a temporary name)";
            return -1;
        }
        fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) {
            why = "cannot be created (" + error_text(errno) + ")";
            return -1;
        }
    }
    if (fd < 0) {
        why = "cannot be created (no free temporary name)";
        return -1;
    }
    size_t done = 0;
    int bad = 0;                                        // errno of the first thing that went wrong
    while (done < line.size()) {
        const ssize_t w = ::write(fd, line.data() + done, line.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            bad = w < 0 ? errno : EIO;
            break;
        }
        done += static_cast<size_t>(w);
    }
    if (bad == 0 && ::fsync(fd) != 0) bad = errno;
    if (::close(fd) != 0 && bad == 0) bad = errno;
    if (bad != 0) {
        ::unlink(tmp.c_str());                          // never leave half a secret under any name that matters
        why = "cannot be written (" + error_text(bad) + ")";
        return -1;
    }
    // link() fails with EEXIST when the name is taken (a dangling symbolic link counts as taken); it never replaces and never follows a link
    const int linked = ::link(tmp.c_str(), path.c_str());
    const int link_error = errno;
    ::unlink(tmp.c_str());
    if (linked == 0) return 1;
    if (link_error == EEXIST) return 0;
    why = "cannot be created (" + error_text(link_error) + "; the server makes the file with a hard link, which this file system may not support)";
    return -1;
#endif
}

}  // namespace

bool usable_secret_text(const std::string& text) {
    if (text.size() < kMinSecret || text.size() > kMaxSecret) return false;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 33 || u > 126) return false;
    }
    return true;
}

std::string generate_secret_text() {
    uint8_t bytes[32];
    if (!random_bytes(bytes, sizeof bytes)) return std::string();
    return to_hex(bytes, sizeof bytes);
}

SecretResult resolve_secret(const char* from_env, const std::string& file) {
    if (from_env != nullptr && *from_env != '\0') {
        SecretResult r;
        r.ok = true;
        r.secret = from_env;
        r.source = SecretSource::Environment;
        return r;
    }
    if (file.empty()) {
        return failure(file, "the control interface needs a secret: set ANTS_SERVER_SECRET, or give --results-dir or --secret-file so that the server can make one and keep it");
    }

    for (int attempt = 0; attempt < 4; ++attempt) {      // another round: a start at the same moment made the file between our look and our create
        std::error_code ec;
        const fs::file_status st = fs::status(file, ec);          // follows a symbolic link
        if (ec && st.type() == fs::file_type::none) return failure(file, "the secret file '" + file + "' cannot be looked at (" + ec.message() + ")");
        if (fs::exists(st)) {
            if (!fs::is_regular_file(st)) return failure(file, "the secret file '" + file + "' is not a regular file");
            std::string secret, why;
            if (!read_secret_file(file, secret, why)) {
                return failure(file, "the secret file '" + file + "' " + why + "; nothing was changed: fix it, or delete it and the server makes a new secret");
            }
            SecretResult r;
            r.ok = true;
            r.secret = secret;
            r.source = SecretSource::File;
            r.path = file;
            return r;
        }

        // nothing there, or a symbolic link whose target does not exist: that is no place for a new file (a link is never replaced, and the target is not made)
        std::error_code link_ec;
        const fs::file_status link_st = fs::symlink_status(file, link_ec);
        if (!link_ec && fs::is_symlink(link_st)) {
            return failure(file, "the secret file '" + file + "' is a symbolic link to nothing (the file it points to does not exist); nothing was changed: make that file, or remove the link and the server makes a new secret");
        }

        const fs::path parent = fs::path(file).parent_path();
        if (!parent.empty()) fs::create_directories(parent, ec);
        const std::string secret = generate_secret_text();
        if (secret.empty()) return failure(file, "the operating system's random number generator is not available: set ANTS_SERVER_SECRET instead");
        std::string why;
        const int made = create_secret_file(file, secret, why);
        if (made < 0) return failure(file, "the secret file '" + file + "' " + why + ": set ANTS_SERVER_SECRET instead, or give --secret-file a place the server may write");
        if (made == 1) {
            SecretResult r;
            r.ok = true;
            r.secret = secret;
            r.source = SecretSource::Generated;
            r.path = file;
            return r;
        }
    }
    return failure(file, "the secret file '" + file + "' is made and removed again and again by something else");
}

}  // namespace ants::server

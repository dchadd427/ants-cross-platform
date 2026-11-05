#include "ants_server/whole_file.hpp"

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ants::server {

namespace {

// Has what the stream holds on the disk: flushed, then fsync'ed (_commit on Windows). The rename that follows must not give a file its name before its data is there.
bool flush_to_disk(std::FILE* f) {
    if (std::fflush(f) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    for (;;) {
        if (fsync(fileno(f)) == 0) return true;
        if (errno != EINTR) return false;
    }
#endif
}

}  // namespace

bool write_whole_file(const std::string& path, const std::string& text, std::string& why) {
    const std::string tmp = path + ".tmp";
    // The temporary file is always a new plain file: what is found in its place (a leftover, a link, a pipe, another name of a file) is removed first and never opened as it is. A folder is left alone (the open then
    // fails and says so). The folder of the results is trusted to the extent that it always was: whoever can write there can also write the result files themselves.
    std::error_code seen;
    const std::filesystem::file_type kind = std::filesystem::symlink_status(tmp, seen).type();
    if (kind != std::filesystem::file_type::not_found && kind != std::filesystem::file_type::none && kind != std::filesystem::file_type::directory) std::filesystem::remove(tmp, seen);
    std::FILE* f = std::fopen(tmp.c_str(), "wb");                          // (mode 0666 less the umask, as the file always had)
    if (f == nullptr) {
        why = "cannot write " + tmp + " (" + std::error_code(errno, std::generic_category()).message() + ")";
        return false;
    }
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    int err = ok ? 0 : errno;                                              // (errno of the first thing that went wrong)
    if (ok && !flush_to_disk(f)) {
        ok = false;
        err = errno;
    }
    if (std::fclose(f) != 0 && ok) {
        ok = false;
        err = errno;
    }
    std::error_code ec;
    if (ok) std::filesystem::rename(tmp, path, ec);                        // (whole file or the old one: never half)
    if (!ok || ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);                             // (a file that this call made, never left behind on any failure)
        why = ok ? "cannot rename " + tmp + ": " + ec.message() : "cannot write " + tmp + " (" + std::error_code(err, std::generic_category()).message() + ")";
        return false;
    }
    return true;
}

}  // namespace ants::server

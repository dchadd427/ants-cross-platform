#include "ants_server/private_files.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace ants::server {

PrivateCreation::PrivateCreation() {
#ifndef _WIN32
    old_ = static_cast<int>(::umask(077));
#endif
}

PrivateCreation::~PrivateCreation() {
#ifndef _WIN32
    ::umask(static_cast<mode_t>(old_));
#endif
}

std::vector<std::string> tighten_private_files(const std::string& db_path, const std::string& folder) {
    std::vector<std::string> lines;
#ifndef _WIN32
    const auto tighten = [&](const std::string& path, mode_t want, const char* what) {
        struct stat st;
        if (path.empty() || ::stat(path.c_str(), &st) != 0) return;
        if ((st.st_mode & 077) == 0) return;
        const bool done = ::chmod(path.c_str(), want) == 0;
        lines.push_back(std::string(what) + " " + path + (done ? " was readable by others: made private" : " is readable by others and could not be made private (change it by hand)"));
    };
    tighten(db_path, 0600, "database file");
    tighten(db_path + "-wal", 0600, "database file");
    tighten(db_path + "-shm", 0600, "database file");
    tighten(folder, 0700, "folder");
#else
    (void)db_path;
    (void)folder;
#endif
    return lines;
}

}  // namespace ants::server

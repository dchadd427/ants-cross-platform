#pragma once

// The files that hold the server's records are private to the user who runs the server (docs/MAP_SUBMISSIONS.md, "Behavior notes"): the database and its -wal and -shm files are mode 0600, the results folder 0700
// (POSIX; nothing on Windows).

#include <string>
#include <vector>

namespace ants::server {

/// While one of these objects lives, new files and folders are made private (umask 077), so that the database is never, even for a moment, readable by others.
class PrivateCreation {
public:
    PrivateCreation();
    ~PrivateCreation();
    PrivateCreation(const PrivateCreation&) = delete;
    PrivateCreation& operator=(const PrivateCreation&) = delete;
private:
    int old_{0};
};
/// Makes the database file, its -wal and -shm files (when they exist) and `folder` (when it is not empty) private if they are looser; one line for each change, for the log (a path, never a secret).
std::vector<std::string> tighten_private_files(const std::string& db_path, const std::string& folder);

}  // namespace ants::server

#pragma once

// The server's command line for the owner's accounts (docs/MAP_SUBMISSIONS.md, "Accounts"): `ants_server --admin-create <username>` makes the first Admin and `ants_server --admin-reset <username>` gives
// a user who forgot the password a new one. Both print the new random starting password once, to the terminal (standard output), and nowhere else: it is not logged, not kept, and the database holds
// only its hash. They work on the database file, so they can be run beside a running server (SQLite's own locks keep the two apart).

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace ants::server {

struct AdminCommand {
    enum class Kind { Create, Reset };
    Kind kind{Kind::Create};
    std::string name;
    std::string db_path;
    std::string folder;                     // the results folder, when given (made private); empty for --db alone
    uint32_t rounds{0};                     // 0: the program's own cost (kPasswordRounds); a test passes a small number
};

/// Files and folders that hold the accounts are private to the user who runs the server: the database and its -wal and -shm files are mode 0600, the results folder 0700 (POSIX; nothing on Windows).
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

/// Runs the command. 0: done. 1: refused or failed (an Admin exists already, no such user, the database cannot be used; the reason is written to `err`). The password goes to `out` only.
int run_admin_command(const AdminCommand& command, std::ostream& out, std::ostream& err);

}  // namespace ants::server

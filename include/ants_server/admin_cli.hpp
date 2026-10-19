#pragma once

// The server's command line for the owner's accounts (docs/MAP_SUBMISSIONS.md, "Accounts"): `ants_server --admin-create <username>` makes the first Admin and `ants_server --admin-reset <username>` gives
// a user who forgot the password a new one. Both print the new random starting password once, to the terminal (standard output), and nowhere else: it is not logged, not kept, and the database holds
// only its hash. They work on the database file, so they can be run beside a running server (SQLite's own locks keep the two apart).

#include <cstdint>
#include <iosfwd>
#include <string>

namespace ants::server {

struct AdminCommand {
    enum class Kind { Create, Reset };
    Kind kind{Kind::Create};
    std::string name;
    std::string db_path;
    uint32_t rounds{0};                     // 0: the program's own cost (kPasswordRounds); a test passes a small number
};

/// Runs the command. 0: done. 1: refused or failed (an Admin exists already, no such user, the database cannot be used; the reason is written to `err`). The password goes to `out` only.
int run_admin_command(const AdminCommand& command, std::ostream& out, std::ostream& err);

}  // namespace ants::server

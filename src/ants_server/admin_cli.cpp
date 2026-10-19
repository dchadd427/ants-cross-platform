#include "ants_server/admin_cli.hpp"

#include <ctime>
#include <filesystem>
#include <ostream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_server/admin_accounts.hpp"
#include "ants_server/db_schema.hpp"

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

int run_admin_command(const AdminCommand& command, std::ostream& out, std::ostream& err) {
    if (command.db_path.empty()) {
        err << "needs the database: give --db FILE or --results-dir DIR (the file is ants.db in it)\n";
        return 1;
    }
    const PrivateCreation private_files;
    std::error_code made;
    std::filesystem::create_directories(std::filesystem::u8path(command.db_path).parent_path(), made);          // (as the server does before it opens the file: a first account may be made before the first start)
    db::Database database;
    const db::Error opened = db::open_and_migrate(database, command.db_path, db_migrations(), db::OpenOptions());
    if (!opened.ok()) {
        err << "database " << command.db_path << ": " << opened.message << "\n(the file was not changed)\n";
        return 1;
    }
    for (const std::string& line : tighten_private_files(command.db_path, command.folder)) err << line << "\n";
    AccountConfig config;
    if (command.rounds != 0) config.rounds = command.rounds;
    AdminAccounts accounts(database, config);
    std::string why;
    if (!accounts.open(why)) {
        err << "accounts: " << why << "\n";
        return 1;
    }
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    Failure failure;
    std::string password;
    if (command.kind == AdminCommand::Kind::Create) {
        User user;
        if (!accounts.create_first_admin(command.name, now, user, password, failure)) {
            err << failure.message << "\n";
            return 1;
        }
        out << "Admin account created.\nUsername: " << user.name << "\nStarting password: " << password << "\nShown once. Sign in at /admin and change it.\n";
        return 0;
    }
    int64_t id = 0;
    for (const User& u : accounts.list()) {
        if (username_key(u.name) == username_key(command.name)) id = u.id;
    }
    if (id == 0) {
        err << "There is no user with that name.\n";
        return 1;
    }
    if (!accounts.reset(Actor(), id, now, password, failure, true)) {
        err << failure.message << "\n";
        return 1;
    }
    out << "Password reset.\nUsername: " << command.name << "\nNew starting password: " << password << "\nShown once. This user was signed out, and must choose a new password at the next sign-in.\n";
    return 0;
}

}  // namespace ants::server

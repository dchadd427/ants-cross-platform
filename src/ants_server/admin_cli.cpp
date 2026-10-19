#include "ants_server/admin_cli.hpp"

#include <ctime>
#include <filesystem>
#include <ostream>

#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_server/admin_accounts.hpp"
#include "ants_server/db_schema.hpp"

namespace ants::server {

int run_admin_command(const AdminCommand& command, std::ostream& out, std::ostream& err) {
    if (command.db_path.empty()) {
        err << "needs the database: give --db FILE or --results-dir DIR (the file is ants.db in it)\n";
        return 1;
    }
    std::error_code made;
    std::filesystem::create_directories(std::filesystem::u8path(command.db_path).parent_path(), made);          // (as the server does before it opens the file: a first account may be made before the first start)
    db::Database database;
    const db::Error opened = db::open_and_migrate(database, command.db_path, db_migrations(), db::OpenOptions());
    if (!opened.ok()) {
        err << "database " << command.db_path << ": " << opened.message << "\n(the file was not changed)\n";
        return 1;
    }
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

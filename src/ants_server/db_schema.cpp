// The migrations of the server's database. Version 1 holds the foundation only: no record is stored yet.
#include "ants_server/db_schema.hpp"

namespace ants::server {

const std::vector<db::Migration>& db_migrations() {
    static const std::vector<db::Migration> list = {
        {1, "meta",
         "CREATE TABLE meta (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID;"
         "INSERT INTO meta (key, value) VALUES ('created_at', strftime('%Y-%m-%dT%H:%M:%SZ', 'now'));"},
    };
    return list;
}

}  // namespace ants::server

// The schema of the server's database (docs/SERVER.md "The database"): the ordered list of migrations. A change to the schema is a NEW entry at the end, never an edit of an old one.
#pragma once

#include "ants_db/migrations.hpp"

#include <vector>

namespace ants::server {

const std::vector<db::Migration>& db_migrations();
inline int db_schema_version() { return db_migrations().back().version; }       // the version this program writes

}  // namespace ants::server

// Where this checkout is, for the tests and tools that read Original-Ants/, tests/data or web/. CMake knows the folders and writes them into one tiny source per build folder
// (cmake/ants_test_paths.cpp.in, the library ants_test_paths), so that no compile command carries a folder: ccache then reuses every other object in a second worktree.
// ORIGINAL_ASSETS_DIR, TEST_DATA_DIR and ANTS_SOURCE_DIR are const char* expressions, not literals: write std::string(ORIGINAL_ASSETS_DIR) + "/Maps". A build that still
// defines one on the command line keeps it.
#pragma once

namespace ants_test_paths {
const char* original_assets_dir();   // <checkout>/Original-Ants
const char* test_data_dir();         // <checkout>/tests/data
const char* source_dir();            // <checkout>
}  // namespace ants_test_paths

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR (::ants_test_paths::original_assets_dir())
#endif
#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR (::ants_test_paths::test_data_dir())
#endif
#ifndef ANTS_SOURCE_DIR
#define ANTS_SOURCE_DIR (::ants_test_paths::source_dir())
#endif

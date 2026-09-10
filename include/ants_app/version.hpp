#pragma once

#include <string_view>

namespace ants {

// Central application version string and semantic version components.
// Starts at v0.01 for pre-release and is continuously incremented.
inline constexpr std::string_view VERSION_STRING = "v0.01";
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

} // namespace ants

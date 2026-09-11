#pragma once

#include <string_view>

namespace ants {

// Central application version string and semantic version components.
// Starts at v0.0.1 for pre-release and is continuously incremented.
inline constexpr std::string_view VERSION_STRING = "v0.0.11";
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 0;
inline constexpr int VERSION_PATCH = 11;

} // namespace ants

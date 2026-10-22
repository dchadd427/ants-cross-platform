// library_guard: the owner's rule for the tools, in one place. No tool may modify, re-save, repair, re-render or delete a map of the library (Original-Ants/ and
// Community-Maps/, the six originals and the players' maps, anything the site serves under /maps), and no tool writes over the file it reads. Every tool that writes a
// file whose path a person gives it (lvl_tool, map_sweep, bot_arena) asks `write_refusal` first and, when it answers, writes nothing, says the sentence and exits non-zero.
// Reading the library stays fine. The same rule for the python tools is tools/library_guard.py; docs/WORKFLOW.md ("Tools never touch the map library") has the words.
//
// A path is refused when
//   - one of its folders (or the file itself) is named Original-Ants or Community-Maps, in any case (the files of Windows and macOS fold case; Linux is held to the same
//     names, which is stricter and harmless), as written (with its `.` and `..` taken out) and again after its symbolic links are followed, so that `../Community-Maps/x.lvl`,
//     `Community-Maps/../Community-Maps/x.lvl` and a link to the folder or to a file in it are all caught;
//   - one of its existing folders is this checkout's own Original-Ants or Community-Maps (the same folder by the system's own test, so that another case, a short name on
//     Windows or a bind mount is caught too);
//   - it is an existing file with more than one name (a hard link; a library map may be the other name);
//   - it is the file the command reads (the same file by the system's own test: another spelling, a link, a hard link, another case).
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "ants_test_paths.hpp"

namespace library_guard {

// The name of a folder or file of the library, in lower case (ASCII only: the names are)
inline std::string folded(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline bool library_name(const std::string& part) {
    const std::string f = folded(part);
    return f == "original-ants" || f == "community-maps";
}

inline bool names_library(const std::filesystem::path& path) {
    for (const std::filesystem::path& part : path) {
        try {
            if (library_name(part.string())) return true;
        } catch (const std::exception&) {
            // (a name that the narrow text cannot hold is not one of these ASCII names)
        }
    }
    return false;
}

// This checkout's own library folders
inline std::vector<std::filesystem::path> checkout_library() {
    return {std::filesystem::path(std::string(ORIGINAL_ASSETS_DIR)), std::filesystem::path(std::string(ANTS_SOURCE_DIR)) / "Community-Maps"};
}

// True when `path` or one of the folders above it is the same folder as one of `library`
inline bool inside_folders(const std::filesystem::path& path, const std::vector<std::filesystem::path>& library) {
    std::error_code ec;
    std::filesystem::path at = path;
    for (int depth = 0; depth < 256 && !at.empty(); ++depth) {
        if (std::filesystem::exists(at, ec)) {
            for (const std::filesystem::path& folder : library) {
                ec.clear();
                if (std::filesystem::exists(folder, ec) && std::filesystem::equivalent(at, folder, ec) && !ec) return true;
            }
        }
        const std::filesystem::path up = at.parent_path();
        if (up == at) break;
        at = up;
    }
    return false;
}

// "" when the tool may write `path`; else the reason, in plain words (the tool puts its own name in front). `inputs` are the files the command reads.
// `library` is the folders to hold apart (default: this checkout's); a test names its own copy of the layout.
inline std::string write_refusal(const std::string& path, const std::vector<std::string>& inputs = {}, const std::vector<std::filesystem::path>& library = checkout_library()) {
    if (path.empty()) return "";
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path absolute = fs::absolute(fs::path(path), ec).lexically_normal();
    fs::path real = fs::weakly_canonical(absolute, ec);
    if (ec) real = absolute;
    if (names_library(absolute) || names_library(real) || inside_folders(real, library)) {
        return path + " is in the map library (Original-Ants or Community-Maps): no tool writes there, the library maps are never changed, re-saved, repaired, re-rendered or deleted; write to a copy somewhere else";
    }
    ec.clear();
    if (fs::is_regular_file(real, ec) && fs::hard_link_count(real, ec) > 1 && !ec) {
        return path + " is a file with more than one name (a hard link), and the other name may be a map of the library: write to a new file";
    }
    for (const std::string& input : inputs) {
        ec.clear();
        if (!input.empty() && fs::exists(input, ec) && fs::exists(real, ec) && fs::equivalent(input, real, ec) && !ec) {
            return path + " is the file that is read: a tool never writes over its own input, write to another file";
        }
    }
    return "";
}

// For a tool that writes `path` on a command's option `what`: true (after one line on `err` that says why) when the tool must not write it
inline bool refused(const char* tool, const char* what, const std::string& path, const std::vector<std::string>& inputs, std::FILE* err) {
    const std::string why = write_refusal(path, inputs);
    if (!why.empty()) std::fprintf(err, "%s: %s: %s; nothing written\n", tool, what, why.c_str());
    return !why.empty();
}

}  // namespace library_guard

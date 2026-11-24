#pragma once

// The sheets of a pack of new pictures: the one reader of what the HUD art (hud_art.hpp) and the world art (world_art.hpp) share, and the one home of how a pack is split over sheets.
//
// A pack's pictures are on PNG sheets, and a texture holds 4096 pixels a side at most (what every graphics card takes), so a pack that has more pictures than one sheet holds has them on more
// than one PAGE. A page is three sheets, one for each scale (1x, 2x and 3x: the same drawing at 1, 2 and 3 pixels for each world or canvas pixel), and a picture is on one page. The packer
// fills a page as far as it goes, in its order, and the rest go on the next page (tools/art_sheets.py: fill_pages); the world art also starts a page of its own for what a batch adds, so that a
// view that draws nothing of it never makes its textures. Each reader makes the texture of a page's sheet when the first picture of it is drawn.
//
// Both manifests (one record a line) say it the same way, once for every sheet of every page, before their pictures:
//     sheet <page> <scale> <png file> <width> <height>
// Pages are 0, 1, 2 ... in order, each with its three records for the scales 1, 2 and 3 in that order; the file is a name in the pack's folder, and width and height are the size of that file in
// pixels (8-bit RGBA). Where a pack's pictures sit on its sheets, and what else its manifest says, is for hud_art.hpp and world_art.hpp.
//
// This class holds those records, looks at the files' headers (a folder that lacks a sheet, or has another, is the original's art from the start, not a hole) and decodes a sheet when a texture
// needs it. No SDL: a test runs it.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/png_decode.hpp"

namespace ants::app {

/// How many pages a manifest may have
inline constexpr int32_t kArtMaxPages = 4;
/// The scales of the sheets
inline constexpr std::array<int32_t, 3> kArtScales = {1, 2, 3};

/// The whole file as bytes (a manifest or a sheet). Never throws: a file that cannot be read, and a folder where the file should be (the standard library of Linux throws when it reads one), are a failure
bool read_art_file(const std::string& path, std::string& out);

class ArtSheets {
public:
    /// One `sheet` record, as its words: "" when it is taken, else what is wrong with it (not five fields, a number that is not one, a page or a scale out of order, a size that is not 1 to 16384 or a file
    /// that is not a name in the folder)
    std::string add(const std::vector<std::string>& words);
    /// After the last record: "" when there is at least one page and every page has its three records, else what is missing
    std::string finish() const;

    int32_t pages() const noexcept { return rows_ / 3; }
    bool has(int32_t page, int32_t scale) const noexcept { return page >= 0 && page < kArtMaxPages && scale >= 1 && scale <= 3 && page * 3 + scale <= rows_; }
    const std::string& file(int32_t page, int32_t scale) const;
    int32_t width(int32_t page, int32_t scale) const noexcept { return has(page, scale) ? sheets_[index(page, scale)].w : 0; }
    int32_t height(int32_t page, int32_t scale) const noexcept { return has(page, scale) ? sheets_[index(page, scale)].h : 0; }

    /// Looks at the header of every sheet file of `folder` (which ends with a slash): "" when each is there, a PNG of 8-bit RGBA and the size that the record says, else the sentence for the first that
    /// is not
    std::string check_files(const std::string& folder) const;
    /// The pixels of a sheet, read and decoded from its file in `folder`; with `rows` > 0 only that many rows (the picture's height is then not looked at). False when the sheet is not there, or the
    /// file is missing or damaged or is not the size that the record says
    bool pixels(const std::string& folder, int32_t page, int32_t scale, DecodedImage& out, int32_t rows = 0) const;

private:
    struct Sheet {
        std::string file;
        int32_t w{0};
        int32_t h{0};
    };
    static size_t index(int32_t page, int32_t scale) noexcept { return static_cast<size_t>(page) * 3u + static_cast<size_t>(scale - 1); }
    int32_t rows_{0};                                                                // the records taken so far
    std::array<Sheet, static_cast<size_t>(kArtMaxPages) * 3u> sheets_{};
};

}  // namespace ants::app

// The sheets of a pack of new pictures (art_sheets.hpp): the `sheet` records, the look at the files' headers and the decoding of a sheet.
#include "ants_app/art_sheets.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>

namespace ants::app {

namespace {

constexpr int32_t kMaxSide = 16384;           // a side that a record may state (the device's own limit is asked when the texture is made)

bool to_int(const std::string& text, int64_t& out) {
    if (text.empty() || text.size() > 9) return false;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
    }
    out = std::strtoll(text.c_str(), nullptr, 10);
    return true;
}

}  // namespace

bool read_art_file(const std::string& path, std::string& out) {
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return !f.bad();
    } catch (const std::exception&) {
        return false;
    }
}

const std::string& ArtSheets::file(int32_t page, int32_t scale) const {
    static const std::string none;
    return has(page, scale) ? sheets_[index(page, scale)].file : none;
}

std::string ArtSheets::add(const std::vector<std::string>& w) {
    int64_t page = 0, scale = 0, width = 0, height = 0;
    if (w.size() != 6 || w[0] != "sheet" || !to_int(w[1], page) || !to_int(w[2], scale) || !to_int(w[4], width) || !to_int(w[5], height)) return "a sheet record is: sheet <page> <scale> <png file> <width> <height>";
    const size_t at = static_cast<size_t>(rows_);
    if (at >= sheets_.size() || page != rows_ / 3 || scale != rows_ % 3 + 1) return "the sheet records are for the pages in order (at most " + std::to_string(kArtMaxPages) + ") and, in each page, for scale 1, 2 and 3 in that order, once each";
    if (width < 1 || height < 1 || width > kMaxSide || height > kMaxSide) return "a sheet's size is out of range";
    const std::string& file = w[3];
    if (file.empty() || file == "." || file.find('/') != std::string::npos || file.find('\\') != std::string::npos || file.find("..") != std::string::npos) return "a sheet's file must be a name in the folder";
    Sheet& sheet = sheets_[at];
    sheet.file = file;
    sheet.w = static_cast<int32_t>(width);
    sheet.h = static_cast<int32_t>(height);
    ++rows_;
    return "";
}

std::string ArtSheets::finish() const {
    if (rows_ == 0 || rows_ % 3 != 0) return "the manifest lacks a sheet record: every page has one for each of the scales 1, 2 and 3";
    return "";
}

std::string ArtSheets::check_files(const std::string& folder) const {
    for (int32_t page = 0; page < pages(); ++page) {
        for (const int32_t scale : kArtScales) {
            const Sheet& sheet = sheets_[index(page, scale)];
            std::ifstream in(folder + sheet.file, std::ios::binary);
            uint8_t head[33] = {};
            in.read(reinterpret_cast<char*>(head), sizeof(head));
            const bool png = in.gcount() == static_cast<std::streamsize>(sizeof(head)) && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp(head + 12, "IHDR", 4) == 0;
            const auto be32 = [&head](size_t at) { return (static_cast<int64_t>(head[at]) << 24) | (static_cast<int64_t>(head[at + 1]) << 16) | (static_cast<int64_t>(head[at + 2]) << 8) | head[at + 3]; };
            if (!png || be32(16) != sheet.w || be32(20) != sheet.h || head[24] != 8 || head[25] != 6) {
                return "the sheet " + folder + sheet.file + " is missing, or is not a " + std::to_string(sheet.w) + " x " + std::to_string(sheet.h) + " picture of 8-bit RGBA";
            }
        }
    }
    return "";
}

bool ArtSheets::pixels(const std::string& folder, int32_t page, int32_t scale, DecodedImage& out, int32_t rows) const {
    out = DecodedImage{};
    if (!has(page, scale)) return false;
    const Sheet& sheet = sheets_[index(page, scale)];
    std::string bytes;
    if (!read_art_file(folder + sheet.file, bytes)) return false;
    if (!decode_png_rgba(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out, rows)) return false;
    if (out.width != sheet.w || (rows <= 0 && out.height != sheet.h)) {
        out = DecodedImage{};
        return false;
    }
    return true;
}

}  // namespace ants::app

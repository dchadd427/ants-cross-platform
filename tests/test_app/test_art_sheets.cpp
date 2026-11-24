// The sheets of a pack of new pictures (include/ants_app/art_sheets.hpp, src/ants_app/art_sheets.cpp): the one reader that the HUD art and the world art share, held once.
//   [records]  the `sheet` records of a manifest: pages in order with three sheets each (scale 1, 2 and 3), every way to break a record, a page that is not whole, more pages than the reader keeps
//   [files]    a folder's sheets are looked at when a pack is read: each is there, a PNG of 8-bit RGBA and the size that its record states; a cut file, a file that is not a PNG, a folder in its place
//   [pixels]   a sheet is decoded when a texture needs it, whole or its first rows, from the file of its page and scale
// Usage: test_art_sheets. Exit code 0 when every check passes.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "ants_app/art_sheets.hpp"
#include "ants_app/png_encode.hpp"
#include "ants_test_paths.hpp"

using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

std::vector<std::string> words_of(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

/// The result of giving a sequence of records to a fresh reader: "" when every record was taken, else the first complaint
std::string take(ArtSheets& sheets, const std::vector<std::string>& records) {
    for (const std::string& r : records) {
        const std::string why = sheets.add(words_of(r));
        if (!why.empty()) return why;
    }
    return "";
}

const std::vector<std::string> kTwoPages = {"sheet 0 1 a1.png 100 80", "sheet 0 2 a2.png 200 160", "sheet 0 3 a3.png 300 240", "sheet 1 1 b1.png 50 40", "sheet 1 2 b2.png 100 80", "sheet 1 3 b3.png 150 120"};

std::vector<std::string> with(std::vector<std::string> records, size_t at, const std::string& line) {
    records[at] = line;
    return records;
}

void test_records() {
    group("records", "the `sheet` records, page by page, and every way to break one");
    ArtSheets sheets;
    check(take(sheets, kTwoPages).empty() && sheets.finish().empty() && sheets.pages() == 2, "two pages of three records are taken and make a whole manifest");
    check(sheets.file(0, 2) == "a2.png" && sheets.width(0, 3) == 300 && sheets.height(0, 3) == 240 && sheets.file(1, 1) == "b1.png" && sheets.width(1, 2) == 100 && sheets.height(1, 3) == 120, "each sheet has its file and its size, by page and scale");
    check(sheets.has(1, 3) && !sheets.has(2, 1) && !sheets.has(-1, 1) && !sheets.has(0, 0) && !sheets.has(0, 4) && sheets.file(2, 1).empty() && sheets.width(0, 4) == 0 && sheets.height(9, 1) == 0, "a page or a scale that is not there has nothing");

    ArtSheets none;
    check(!none.finish().empty() && none.pages() == 0, "a manifest without a record is not whole");
    ArtSheets part;
    take(part, {kTwoPages[0], kTwoPages[1]});
    check(!part.finish().empty(), "...nor is one with only some of a page's records");
    take(part, {kTwoPages[2]});
    check(part.finish().empty() && part.pages() == 1, "...it is whole when the page has its three");
    ArtSheets four;
    take(four, {kTwoPages[0], kTwoPages[1], kTwoPages[2], kTwoPages[3]});
    check(!four.finish().empty() && four.pages() == 1, "a page that has one record of three is not counted");

    const auto refused = [](const std::vector<std::string>& records, const std::string& what) {
        ArtSheets s;
        check(!take(s, records).empty(), "refused: " + what);
    };
    refused(with(kTwoPages, 0, "sheet 0 2 a1.png 100 80"), "scale 2 first");
    refused(with(kTwoPages, 1, "sheet 0 1 a2.png 200 160"), "scale 1 twice");
    refused(with(kTwoPages, 3, "sheet 2 1 b1.png 50 40"), "a page that skips one");
    refused(with(kTwoPages, 0, "sheet 1 1 a1.png 100 80"), "page 1 before page 0");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png 100"), "a record of five fields");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png 100 80 7"), "a record of seven fields");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png wide 80"), "a word for a number");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png -100 80"), "a negative width");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png 0 80"), "no width");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png 100 0"), "no height");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png 16385 80"), "a width beyond what a sheet may be");
    refused(with(kTwoPages, 0, "sheet 0 1 a1.png 100 16385"), "a height beyond what a sheet may be");
    refused(with(kTwoPages, 0, "sheet 0 1 ../a1.png 100 80"), "a file in another folder");
    refused(with(kTwoPages, 0, "sheet 0 1 sub/a1.png 100 80"), "a file in a folder");
    refused(with(kTwoPages, 0, "sheet 0 1 sub\\a1.png 100 80"), "a file with a back slash");
    refused(with(kTwoPages, 0, "sheet 0 1 . 100 80"), "a file called a dot");
    refused(with(kTwoPages, 0, "sprite 0 1 a1.png 100 80"), "a record that is not a sheet's");
    std::vector<std::string> many;
    for (int32_t page = 0; page <= kArtMaxPages; ++page) {
        for (int32_t scale = 1; scale <= 3; ++scale) many.push_back("sheet " + std::to_string(page) + " " + std::to_string(scale) + " f" + std::to_string(page * 3 + scale) + ".png 10 10");
    }
    ArtSheets full;
    check(take(full, std::vector<std::string>(many.begin(), many.begin() + kArtMaxPages * 3)).empty() && full.pages() == kArtMaxPages && full.finish().empty(), "as many pages as the reader keeps (" + std::to_string(kArtMaxPages) + ") are taken");
    ArtSheets over;
    check(!take(over, many).empty() && over.pages() == kArtMaxPages, "...one more is refused");
}

// ---- files -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace fs = std::filesystem;

std::string sheet_source(int32_t scale) { return std::string(ANTS_SOURCE_DIR) + "/assets/world_art/plants_" + std::to_string(scale) + "x.png"; }

/// The size that a PNG file's header states (false when it cannot be read)
bool header_size(const std::string& path, int32_t& w, int32_t& h) {
    std::ifstream in(path, std::ios::binary);
    uint8_t head[33] = {};
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(head))) return false;
    const auto be32 = [&head](size_t at) { return static_cast<int32_t>((static_cast<uint32_t>(head[at]) << 24) | (static_cast<uint32_t>(head[at + 1]) << 16) | (static_cast<uint32_t>(head[at + 2]) << 8) | head[at + 3]); };
    w = be32(16);
    h = be32(20);
    return true;
}

std::vector<std::string> g_folders;

/// A folder with copies of the plants' three sheets as a1.png, a2.png and a3.png, and the sheet's records that name them with their true sizes (`grow` makes a record's width that much more than the file's)
struct Pack {
    explicit Pack(const std::string& tag, int32_t grow = 0) {
        static int serial = 0;
        dir = (fs::temp_directory_path() / ("ants_art_sheets_" + tag + "_" + std::to_string(++serial) + "_" + std::to_string(static_cast<long long>(fs::file_time_type::clock::now().time_since_epoch().count())))).string() + "/";
        std::error_code ignored;
        fs::remove_all(dir, ignored);
        fs::create_directories(dir, ignored);
        g_folders.push_back(dir);
        for (int32_t scale = 1; scale <= 3; ++scale) {
            fs::copy_file(sheet_source(scale), dir + "a" + std::to_string(scale) + ".png", fs::copy_options::overwrite_existing, ignored);
            int32_t w = 0, h = 0;
            header_size(sheet_source(scale), w, h);
            sizes[static_cast<size_t>(scale - 1)] = {w, h};
            add("sheet 0 " + std::to_string(scale) + " a" + std::to_string(scale) + ".png " + std::to_string(w + (scale == 1 ? grow : 0)) + " " + std::to_string(h));
        }
    }
    void add(const std::string& record) { ok = ok && sheets.add(words_of(record)).empty(); }
    std::string dir;
    ArtSheets sheets;
    std::array<std::pair<int32_t, int32_t>, 3> sizes{};
    bool ok{true};
};

void cut_after_header(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> head(80);
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    const std::streamsize got = in.gcount();
    in.close();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(head.data(), got);
}

void test_files() {
    group("files", "the sheets of a folder are looked at when a pack is read");
    {
        Pack good("good");
        check(good.ok && good.sheets.finish().empty() && good.sheets.check_files(good.dir).empty(), "three RGBA sheets of the sizes that the records state are accepted: " + good.sheets.check_files(good.dir));
        check(good.sizes[0].first > 0 && good.sizes[2].first == 3 * good.sizes[0].first, "(the sheets of the fixture are 1x, 2x and 3x of one drawing)");
    }
    {
        Pack wrong("wrong", 1);
        check(wrong.ok && wrong.sheets.check_files(wrong.dir).find("a1.png") != std::string::npos, "a sheet that is not the size its record states is refused, and the sentence names the file");
    }
    {
        Pack missing("missing");
        fs::remove(missing.dir + "a2.png");
        check(!missing.sheets.check_files(missing.dir).empty(), "a sheet that is not in the folder is refused");
    }
    {
        Pack text("text");
        std::ofstream(text.dir + "a3.png", std::ios::binary | std::ios::trunc) << "this is not a picture, though it is as long as a header needs: 0123456789 0123456789";
        check(!text.sheets.check_files(text.dir).empty(), "a file that is not a PNG is refused");
    }
    {
        Pack rgb("rgb");
        const std::vector<uint8_t> pixels(static_cast<size_t>(rgb.sizes[0].first) * static_cast<size_t>(rgb.sizes[0].second) * 3u, 7);
        const std::string png = encode_png_rgb(pixels.data(), rgb.sizes[0].first, rgb.sizes[0].second);
        std::ofstream(rgb.dir + "a1.png", std::ios::binary | std::ios::trunc).write(png.data(), static_cast<std::streamsize>(png.size()));
        check(!rgb.sheets.check_files(rgb.dir).empty(), "a PNG of 8-bit RGB (no alpha) is refused");
    }
    {
        Pack dir("dir");
        fs::remove(dir.dir + "a1.png");
        fs::create_directories(dir.dir + "a1.png");
        check(!dir.sheets.check_files(dir.dir).empty(), "a folder in the place of a sheet is refused, and nothing is thrown");
        DecodedImage image;
        bool threw = false;
        bool read = true;
        try {
            read = dir.sheets.pixels(dir.dir, 0, 1, image);
        } catch (...) {
            threw = true;
        }
        check(!threw && !read && image.rgba.empty(), "...and a decode of it is a failure, and nothing is thrown");
    }
    {
        Pack cut("cut");
        cut_after_header(cut.dir + "a2.png");
        DecodedImage image;
        check(cut.sheets.check_files(cut.dir).empty() && !cut.sheets.pixels(cut.dir, 0, 2, image) && image.width == 0 && image.rgba.empty(), "a sheet with a good header and no body passes the look at its header, and cannot be decoded when it is needed");
    }
    {
        Pack two("two");
        two.add("sheet 1 1 a1.png 10 10");
        two.add("sheet 1 2 a2.png 20 20");
        two.add("sheet 1 3 a3.png 30 30");
        check(two.ok && two.sheets.pages() == 2 && !two.sheets.check_files(two.dir).empty(), "the sheets of every page are looked at, not the first page's alone (the second page's records state sizes the files do not have)");
    }
}

void test_pixels() {
    group("pixels", "a sheet is decoded from the file of its page and scale, whole or its first rows");
    Pack pack("pixels");
    DecodedImage image;
    check(pack.sheets.pixels(pack.dir, 0, 2, image) && image.width == pack.sizes[1].first && image.height == pack.sizes[1].second && image.rgba.size() == static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u, "a sheet is decoded whole, at the size its record states");
    DecodedImage rows;
    check(pack.sheets.pixels(pack.dir, 0, 2, rows, 7) && rows.width == image.width && rows.height == 7 && std::equal(rows.rgba.begin(), rows.rgba.end(), image.rgba.begin()), "its first rows alone are the first rows of the whole");
    check(!pack.sheets.pixels(pack.dir, 0, 4, image) && image.rgba.empty() && !pack.sheets.pixels(pack.dir, 1, 1, image) && !pack.sheets.pixels(pack.dir, -1, 1, image) && !pack.sheets.pixels(pack.dir, 0, 0, image), "a sheet that is not there is not decoded");
    Pack grown("pixels_grown", 1);
    check(!grown.sheets.pixels(grown.dir, 0, 1, image) && image.rgba.empty() && !grown.sheets.pixels(grown.dir, 0, 1, image, 5), "a file that is not the size of its record is not decoded, whole or in rows");
    Pack other("pixels_other");                      // a second page whose records name the same files in the other order, with their own sizes: each page reads the file of its own record
    for (int32_t scale = 1; scale <= 3; ++scale) {
        const auto& size = pack.sizes[static_cast<size_t>(3 - scale)];
        other.add("sheet 1 " + std::to_string(scale) + " a" + std::to_string(4 - scale) + ".png " + std::to_string(size.first) + " " + std::to_string(size.second));
    }
    DecodedImage first_page, second_page;
    check(other.ok && other.sheets.pages() == 2 && other.sheets.pixels(other.dir, 0, 1, first_page) && other.sheets.pixels(other.dir, 1, 1, second_page) && first_page.width == pack.sizes[0].first && second_page.width == pack.sizes[2].first, "a page's sheet is read from its own record's file");
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    test_records();
    test_files();
    test_pixels();
    std::error_code ignored;
    for (const std::string& dir : g_folders) fs::remove_all(dir, ignored);
    std::printf("\nart sheets: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Tests of the pictures of the players' maps (MapPictures, docs/MAP_SUBMISSIONS.md "Pictures"): what the server asks of the picture maker and what it keeps. The picture maker of most cases is this very
// program, started again by the server with the words of the real program's command line (the name of the archive's file is the behaviour that the case wants: --chd .../mode-ok, mode-fail, mode-hang ...), so that every case runs a real
// child process with the real start, kill and collect. The last cases run the real maker, ants_thumb, on real maps; they need its path in ANTS_THUMB (ctest sets it) and are left out without it. On Windows
// there is no child process to start: the cases that need one are left out.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern char** environ;
#endif

#include "../common/test_assert.hpp"
#include "ants_server/map_pictures.hpp"
#include "ants_server/map_store.hpp"
#include "big_map_fixtures.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

namespace {

std::string g_self;          // this program, which the server starts as the picture maker

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const std::string& bytes) { std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes; }

uint32_t crc32_of(const std::string& s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const char ch : s) {
        crc ^= static_cast<unsigned char>(ch);
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

void put32(std::string& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<char>((v >> s) & 0xFFu));
}

// The frame of a PNG of w x h pixels (the signature, IHDR, IEND), as long as `size` when that is more than the frame: what the server looks at. The pixels are the picture maker's; the real one's are
// looked at by the last cases.
std::string framed_png(uint32_t w, uint32_t h, size_t size = 0) {
    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr("IHDR", 4);
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);
    put32(out, 13);
    out += ihdr;
    put32(out, crc32_of(ihdr));
    if (size > out.size() + 12 + 12) {                                 // (a chunk of a kind that nobody reads, to make the file as long as wanted)
        std::string pad("zTXt", 4);
        pad.append(size - out.size() - 12 - 12, 'x');
        put32(out, static_cast<uint32_t>(pad.size() - 4));
        out += pad;
        put32(out, crc32_of(pad));
    }
    put32(out, 0);
    out += "IEND";
    put32(out, crc32_of("IEND"));
    return out;
}

#if !defined(_WIN32)
// The picture maker of the cases: the arguments are the real program's (--chd MODE --out FILE --memory-mb N --seconds N MAP), the behaviour is MODE
int fake_thumbnailer(int argc, char** argv) {
    const std::string mode = fs::path(argv[2]).filename().string().substr(5);          // (the archive is a file called mode-<behaviour>)
    std::string out;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--out") out = argv[i + 1];
    }
    if (out.empty()) return 2;
    const fs::path dir = fs::path(out).parent_path();
    std::string report;
    if (mode == "report") {                                            // (what the child has when it starts: its descriptors and environment, looked at before it opens a file of its own)
        report += "fds:";
        for (int fd = 3; fd < 256; ++fd) {
            if (fcntl(fd, F_GETFD) != -1) report += " " + std::to_string(fd);
        }
        report += "\nenv:";
        std::set<std::string> names;
        for (char** e = environ; *e != nullptr; ++e) names.insert(std::string(*e).substr(0, std::string(*e).find('=')));
        for (const std::string& n : names) report += " " + n;
        char byte = 0;
        report += "\nstdin: " + std::to_string(read(0, &byte, 1)) + "\nargc: " + std::to_string(argc) + "\n";
    }
    {
        std::ofstream runs(dir / "runs.log", std::ios::app);
        runs << mode << " " << fs::path(argv[argc - 1]).filename().string() << "\n";
    }
    if (mode == "ok") {
        write_file(out, framed_png(300, 200));
    } else if (mode == "report") {
        write_file(out, framed_png(300, 300));
        write_file(dir / "report.txt", report);
    } else if (mode == "fail") {
        std::cerr << "the map cannot be read: a sentence of the program\nsecond line\n";
        return 3;
    } else if (mode == "hang") {
        write_file(dir / "hung.pid", std::to_string(getpid()));
        std::this_thread::sleep_for(std::chrono::seconds(60));
    } else if (mode == "crash") {
        kill(getpid(), SIGTERM);                                       // (a signal that leaves no core file)
        std::this_thread::sleep_for(std::chrono::seconds(5));
    } else if (mode == "junk") {
        write_file(out, "this is not a picture");
    } else if (mode == "unsigned") {
        std::string png = framed_png(10, 10);
        png[1] = 'G';                                                  // (a frame like a PNG's, not the signature of one)
        write_file(out, png);
    } else if (mode == "wide") {
        write_file(out, framed_png(301, 10));
    } else if (mode == "heavy") {
        write_file(out, framed_png(10, 10, MapPictures::kMaxPictureBytes + 1));
    } else if (mode == "nothing") {
        return 0;
    } else {
        return 4;
    }
    return 0;
}
#endif

// A folder of pictures with a picture maker that behaves as `mode` says, and the map files that it is asked about (the stem names the picture)
struct PictureRig {
    TempDir dir;
    MapPicturesConfig config;
    std::unique_ptr<MapPictures> pictures;
    bool opened{false};

    explicit PictureRig(const std::string& mode = "ok", const std::function<void(MapPicturesConfig&)>& tune = nullptr) {
        config.dir = (dir.path / "pictures").string();
        config.program = g_self;
        config.archive = (dir.path / ("mode-" + mode)).string();
        write_file(config.archive, "an archive that is no archive: the picture maker of these cases takes its behaviour from its name");
        config.timeout_ms = 20000;
        if (tune) tune(config);
        pictures = std::make_unique<MapPictures>(config);
        std::string why;
        opened = pictures->open(why);
    }

    std::string map(const std::string& stem, const std::string& bytes = "map bytes") {
        const fs::path path = dir.path / (stem + ".lvl");
        write_file(path, bytes);
        return path.string();
    }
    fs::path picture_file(const std::string& stem) const { return fs::path(config.dir) / (stem + ".png"); }
    std::string runs() const { return read_file(fs::path(config.dir) / "runs.log"); }
    size_t run_count() const {
        const std::string text = runs();
        return static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
    }
    // The server's loop: one pass after another until nothing is queued or running (or `ms` have passed); the most processes that ran at once is `most_at_once`
    bool settle(int ms = 20000) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < until) {
            pictures->poll();
            if (pictures->busy()) most_at_once = std::max<size_t>(most_at_once, 1);
            if (!pictures->busy() && pictures->queued() == 0) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    size_t most_at_once{0};
};

}  // namespace

int main(int argc, char* argv[]) {
#if !defined(_WIN32)
    if (argc >= 3 && std::string(argv[1]) == "--chd") return fake_thumbnailer(argc, argv);          // (the server started this program as its picture maker)
#endif
    std::error_code absolute_error;
    g_self = fs::absolute(argv[0], absolute_error).string();
    std::cout << "=======================================================\n THE PICTURES OF THE PLAYERS' MAPS (THE PICTURE MAKER AND WHAT THE SERVER KEEPS)\n=======================================================\n";

    TEST_CASE("MP1.1 The picture is named for the map file: letters, digits, '-' and '_' of a .lvl file, nothing else") {
        ASSERT_EQ(MapPictures::stem_of("/data/submissions/files/0123456789abcdef0123456789abcdef.lvl"), std::string("0123456789abcdef0123456789abcdef"));
        ASSERT_EQ(MapPictures::stem_of("/data/test-maps/test-1a2b3c4d.lvl"), std::string("test-1a2b3c4d"));
        ASSERT_EQ(MapPictures::stem_of("Some_Map-2.LVL"), std::string("Some_Map-2"));
        for (const char* bad : {"", "/x/.lvl", "/x/a b.lvl", "/x/a.b.lvl", "/x/a.png", "/x/a", "/x/a~b.lvl", "/x/a%2fb.lvl", "/x/\xc3\xa9.lvl"}) ASSERT_EQ(MapPictures::stem_of(bad), std::string());
        ASSERT_EQ(MapPictures::stem_of("/x/" + std::string(64, 'a') + ".lvl"), std::string(64, 'a'));
        ASSERT_EQ(MapPictures::stem_of("/x/" + std::string(65, 'a') + ".lvl"), std::string());
    } TEST_END();

#if defined(_WIN32)
    TEST_CASE("MP1.2 On this platform no process is started: the pictures that are in the folder are served, none is made, and the log says so") {
        PictureRig rig;
        ASSERT_TRUE(rig.opened);
        ASSERT_FALSE(rig.pictures->enabled());
        rig.pictures->want(rig.map("0000000000000001"));
        ASSERT_EQ(rig.pictures->queued(), size_t{0});
        ASSERT_EQ(rig.pictures->take_notices().size(), size_t{1});
    } TEST_END();
#else
    TEST_CASE("MP2.1 open() makes the folder private, keeps the pictures that are good and removes leftovers, pictures that are no PNG of the right size and (with a version that is not this one) every picture") {
        TempDir dir;
        const fs::path folder = dir.path / "pictures";
        fs::create_directories(folder);
        write_file(folder / "version", "1\n");
        write_file(folder / "good-one.png", framed_png(300, 150));
        write_file(folder / "wide-one.png", framed_png(301, 150));
        write_file(folder / "not-one.png", "text");
        write_file(folder / "half.tmp", "x");
        write_file(folder / "half.err", "x");
        write_file(folder / "keep me.txt", "not ours");
        MapPicturesConfig c;
        c.dir = folder.string();
        c.program = g_self;
        c.archive = (dir.path / "mode-ok").string();
        write_file(c.archive, "x");
        {
            MapPictures p(c);
            std::string why;
            ASSERT_TRUE(p.open(why));
            ASSERT_TRUE(p.enabled());
            ASSERT_EQ(p.made(), size_t{1});
            ASSERT_TRUE(fs::exists(folder / "good-one.png") && !fs::exists(folder / "wide-one.png") && !fs::exists(folder / "not-one.png"));
            ASSERT_TRUE(!fs::exists(folder / "half.tmp") && !fs::exists(folder / "half.err") && fs::exists(folder / "keep me.txt"));
            struct stat st {};
            ASSERT_TRUE(stat(folder.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
            ASSERT_TRUE(p.ready((dir.path / "good-one.lvl").string()));
            ASSERT_EQ(p.get((dir.path / "good-one.lvl").string()), framed_png(300, 150));
        }
        write_file(folder / "version", "0\n");                                         // (another look: every picture is made again)
        MapPictures q(c);
        std::string why;
        ASSERT_TRUE(q.open(why));
        ASSERT_EQ(q.made(), size_t{0});
        ASSERT_FALSE(fs::exists(folder / "good-one.png"));
        ASSERT_EQ(read_file(folder / "version"), std::string("1\n"));
        MapPicturesConfig none;
        MapPictures r(none);
        ASSERT_FALSE(r.open(why));                                                      // (no folder to keep them in)
        ASSERT_FALSE(r.enabled());
    } TEST_END();

    TEST_CASE("MP2.2 Without a program, or with one that cannot be run, or without the archive, no picture is made (a line says so, except where the operator chose none) and the pictures that are there are served") {
        for (const int variant : {0, 1, 2}) {
            PictureRig rig("ok", [&](MapPicturesConfig& c) {
                fs::create_directories(c.dir);
                write_file(fs::path(c.dir) / "version", "1\n");
                write_file(fs::path(c.dir) / "00000000000000aa.png", framed_png(10, 10));          // (a picture that an earlier run made)
                if (variant == 0) c.program.clear();
                if (variant == 1) c.program = (fs::path(c.dir).parent_path() / "no-such-program").string();
                if (variant == 2) c.archive.clear();
            });
            ASSERT_TRUE(rig.opened);
            ASSERT_FALSE(rig.pictures->enabled());
            ASSERT_EQ(rig.pictures->get(rig.map("00000000000000aa")), framed_png(10, 10));
            rig.pictures->want(rig.map("00000000000000bb"));
            ASSERT_EQ(rig.pictures->queued(), size_t{0});
            ASSERT_EQ(rig.pictures->get(rig.map("00000000000000cc")), std::string());
            ASSERT_EQ(rig.pictures->queued(), size_t{0});
            ASSERT_EQ(rig.run_count(), size_t{0});
            ASSERT_EQ(rig.pictures->take_notices().size(), variant == 0 ? size_t{0} : size_t{1});
        }
    } TEST_END();

    TEST_CASE("MP3.1 A map that is asked for gets its picture from a child process, once: the picture is kept under the map's name, asking again starts nothing, and get() hands over the bytes") {
        PictureRig rig;
        ASSERT_TRUE(rig.opened && rig.pictures->enabled());
        const std::string file = rig.map("aaaa1111");
        ASSERT_FALSE(rig.pictures->ready(file));
        rig.pictures->want(file);
        rig.pictures->want(file);                                                      // (asked twice: queued once)
        ASSERT_EQ(rig.pictures->queued(), size_t{1});
        ASSERT_TRUE(rig.settle());
        ASSERT_TRUE(rig.pictures->ready(file));
        ASSERT_EQ(rig.pictures->get(file), framed_png(300, 200));
        ASSERT_EQ(read_file(rig.picture_file("aaaa1111")), framed_png(300, 200));
        rig.pictures->want(file);
        ASSERT_EQ(rig.pictures->queued(), size_t{0});
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.run_count(), size_t{1});
        ASSERT_EQ(rig.pictures->failures(), size_t{0});
        ASSERT_TRUE(rig.pictures->take_notices().empty());
        for (const fs::directory_entry& e : fs::directory_iterator(rig.config.dir)) ASSERT_TRUE(e.path().extension() != ".tmp" && e.path().extension() != ".err");          // (nothing is left behind)
    } TEST_END();

    TEST_CASE("MP3.2 get() of a picture that is not there answers nothing and asks for it; the pictures are made one at a time, in the order they were asked for") {
        PictureRig rig;
        const std::string a = rig.map("map-a"), b = rig.map("map-b"), c = rig.map("map-c");
        ASSERT_EQ(rig.pictures->get(a), std::string());
        ASSERT_EQ(rig.pictures->get(a), std::string());
        rig.pictures->want(b);
        rig.pictures->want(c);
        ASSERT_EQ(rig.pictures->queued(), size_t{3});
        size_t most = 0;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while ((rig.pictures->busy() || rig.pictures->queued() > 0) && std::chrono::steady_clock::now() < until) {
            rig.pictures->poll();
            most = std::max<size_t>(most, rig.pictures->busy() ? 1u : 0u);
            ASSERT_TRUE(rig.run_count() <= rig.pictures->made() + 1);                   // (the next process starts only after the one before was collected)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        ASSERT_EQ(rig.runs(), std::string("ok map-a.lvl\nok map-b.lvl\nok map-c.lvl\n"));
        ASSERT_TRUE(rig.pictures->ready(a) && rig.pictures->ready(b) && rig.pictures->ready(c));
        ASSERT_EQ(rig.pictures->made(), size_t{3});
        ASSERT_EQ(most, size_t{1});
    } TEST_END();

    TEST_CASE("MP3.2b The picture of a test map (a person is waiting for it) goes before the pictures that wait for the start's sweep") {
        PictureRig rig;
        rig.pictures->want(rig.map("0000000000000001"));
        rig.pictures->want(rig.map("0000000000000002"));
        rig.pictures->want(rig.map("test-0a1b2c3d"));
        rig.pictures->want(rig.map("test-0a1b2c3e"));
        ASSERT_EQ(rig.pictures->queued(), size_t{4});
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.runs(), std::string("ok test-0a1b2c3e.lvl\nok test-0a1b2c3d.lvl\nok 0000000000000001.lvl\nok 0000000000000002.lvl\n"));          // (the last test map asked for is the first made; the others keep their order)
    } TEST_END();

    TEST_CASE("MP3.3 At most max_queue maps wait; the rest are not queued (one line says so) and are asked for again by their next request") {
        PictureRig rig("ok", [](MapPicturesConfig& c) { c.max_queue = 2; });
        const std::string a = rig.map("q-a"), b = rig.map("q-b"), c = rig.map("q-c");
        rig.pictures->want(a);
        rig.pictures->want(b);
        rig.pictures->want(c);
        rig.pictures->want(c);
        ASSERT_EQ(rig.pictures->queued(), size_t{2});
        ASSERT_EQ(rig.pictures->take_notices().size(), size_t{1});
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.pictures->get(c), std::string());                                // (asked for again: it is queued now)
        ASSERT_EQ(rig.pictures->queued(), size_t{1});
        ASSERT_TRUE(rig.settle());
        ASSERT_TRUE(rig.pictures->ready(a) && rig.pictures->ready(b) && rig.pictures->ready(c));
    } TEST_END();

    TEST_CASE("MP4.1 A picture maker that fails leaves no picture, costs one line (its own first line, cut to printable text) and is not asked about the same map again for a while") {
        PictureRig rig("fail");
        const std::string file = rig.map("failing-1");
        rig.pictures->want(file);
        ASSERT_TRUE(rig.settle());
        ASSERT_FALSE(rig.pictures->ready(file));
        ASSERT_EQ(rig.pictures->failures(), size_t{1});
        const std::vector<std::string> notices = rig.pictures->take_notices();
        ASSERT_EQ(notices.size(), size_t{1});
        ASSERT_TRUE(notices[0].find("code 3") != std::string::npos && notices[0].find("the map cannot be read: a sentence of the program") != std::string::npos && notices[0].find("second line") == std::string::npos);
        ASSERT_TRUE(notices[0].find("failing-") != std::string::npos && notices[0].find(rig.dir.path.string()) == std::string::npos);          // (a short name of the picture, never a path)
        ASSERT_EQ(rig.pictures->get(file), std::string());
        rig.pictures->want(file);
        ASSERT_EQ(rig.pictures->queued(), size_t{0});                                  // (not tried again at once)
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.run_count(), size_t{1});
        ASSERT_FALSE(fs::exists(rig.picture_file("failing-1")) || fs::exists(fs::path(rig.config.dir) / "failing-1.tmp") || fs::exists(fs::path(rig.config.dir) / "failing-1.err"));
        rig.pictures->want(rig.map("next-one"));                                       // (the next map is not held up by it)
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.run_count(), size_t{2});
    } TEST_END();

    TEST_CASE("MP4.2 A picture maker that is stopped by a signal, ends with no file, or writes what is not a PNG of at most 300 pixels and 512 KiB makes no picture; nothing it wrote stays") {
        for (const char* mode : {"crash", "nothing", "junk", "unsigned", "wide", "heavy"}) {
            PictureRig rig(mode);
            const std::string file = rig.map("bad-output");
            rig.pictures->want(file);
            ASSERT_TRUE(rig.settle());
            ASSERT_FALSE(rig.pictures->ready(file));
            ASSERT_EQ(rig.pictures->failures(), size_t{1});
            ASSERT_EQ(rig.pictures->take_notices().size(), size_t{1});
            ASSERT_EQ(rig.run_count(), size_t{1});
            size_t files = 0;
            for (const fs::directory_entry& e : fs::directory_iterator(rig.config.dir)) files += e.path().extension() == ".tmp" || e.path().extension() == ".err" || e.path().extension() == ".png" ? 1u : 0u;
            ASSERT_EQ(files, size_t{0});
        }
    } TEST_END();

    TEST_CASE("MP4.3 A picture maker that runs too long is killed at the time limit, its process is gone, the queue goes on with the next map") {
        PictureRig rig("hang", [](MapPicturesConfig& c) { c.timeout_ms = 400; });
        const std::string slow = rig.map("slow-map");
        rig.pictures->want(slow);
        rig.pictures->want(rig.map("after-it"));
        const auto began = std::chrono::steady_clock::now();
        ASSERT_TRUE(rig.settle(15000));
        const long long took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
        ASSERT_TRUE(took >= 800 && took < 8000);                                       // (two maps of 400 ms each, never the 60 s that the program would sleep)
        ASSERT_EQ(rig.pictures->failures(), size_t{2});
        const std::vector<std::string> notices = rig.pictures->take_notices();
        ASSERT_TRUE(notices.size() == 2 && notices[0].find("took longer than") != std::string::npos);
        const long pid = std::strtol(read_file(fs::path(rig.config.dir) / "hung.pid").c_str(), nullptr, 10);
        ASSERT_TRUE(pid > 1);
        ASSERT_TRUE(kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH);          // (collected: not even a zombie is left)
        ASSERT_EQ(rig.run_count(), size_t{2});
    } TEST_END();

    TEST_CASE("MP4.4 Destroying the server's object while a picture is made kills the child and leaves no file") {
        pid_t pid = 0;
        fs::path dir_path;
        {
            PictureRig rig("hang");
            rig.pictures->want(rig.map("going-down"));
            rig.pictures->poll();
            ASSERT_TRUE(rig.pictures->busy());
            for (int i = 0; i < 500 && !fs::exists(fs::path(rig.config.dir) / "hung.pid"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            pid = static_cast<pid_t>(std::strtol(read_file(fs::path(rig.config.dir) / "hung.pid").c_str(), nullptr, 10));
            ASSERT_TRUE(pid > 1 && kill(pid, 0) == 0);
            dir_path = rig.config.dir;
            rig.pictures.reset();
            ASSERT_TRUE(kill(pid, 0) != 0 && errno == ESRCH);
            ASSERT_FALSE(fs::exists(dir_path / "going-down.tmp") || fs::exists(dir_path / "going-down.err"));
        }
    } TEST_END();

    TEST_CASE("MP5.1 The child gets only what it needs: no descriptor of the server's (a pipe and a file that the server has open are not in it), an empty standard input, three variables of SDL's dummy drivers and the command line") {
        int fds[2] = {-1, -1};
        ASSERT_TRUE(pipe(fds) == 0);
        const int held = open("/dev/null", O_RDONLY);                                   // (descriptors that a server has open, not marked to close on exec: the sockets and files)
        ASSERT_TRUE(held >= 0 && fcntl(fds[0], F_GETFD) != -1);
        {
            PictureRig rig("report");
            rig.pictures->want(rig.map("report-map"));
            ASSERT_TRUE(rig.settle());
            ASSERT_TRUE(rig.pictures->ready((rig.dir.path / "report-map.lvl").string()));
            const std::string report = read_file(fs::path(rig.config.dir) / "report.txt");
            ASSERT_TRUE(report.find("fds:\n") != std::string::npos);                    // (none above the three that are the child's own: nothing after "fds:")
            for (const char* name : {"SDL_AUDIODRIVER", "SDL_RENDER_DRIVER", "SDL_VIDEODRIVER"}) ASSERT_TRUE(report.find(name) != std::string::npos);
            for (const char* name : {"PATH", "HOME", "USER", "ANTS_"}) ASSERT_TRUE(report.substr(report.find("env:"), report.find("stdin:") - report.find("env:")).find(name) == std::string::npos);          // (nothing of the server's)
            ASSERT_TRUE(report.find("stdin: 0\n") != std::string::npos);
            ASSERT_TRUE(report.find("argc: 10\n") != std::string::npos);                 // (program, --chd, archive, --out, file, --memory-mb, N, --seconds, N, map: ten)
        }
        close(fds[0]);
        close(fds[1]);
        close(held);
    } TEST_END();

    TEST_CASE("MP5.2 A map file that is gone, empty or larger than the server keeps is no job: no process is started and the picture failed") {
        PictureRig rig;
        rig.pictures->want((rig.dir.path / "gone.lvl").string());
        rig.pictures->want(rig.map("empty-one", ""));
        rig.pictures->want(rig.map("large-one", std::string(static_cast<size_t>(MapStore::kMaxMapBytes) + 1u, 'x')));
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.run_count(), size_t{0});
        ASSERT_EQ(rig.pictures->failures(), size_t{3});
        ASSERT_EQ(rig.pictures->made(), size_t{0});
        rig.pictures->want(rig.map("fine-one"));
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.run_count(), size_t{1});
    } TEST_END();

    TEST_CASE("MP6.1 prune() deletes the pictures of maps that are gone (and what a failed run left), keeps the others, and forgets the failure of a map that is gone") {
        PictureRig rig;
        const std::string keep_file = rig.map("stays-1"), gone_file = rig.map("goes-1");
        rig.pictures->want(keep_file);
        rig.pictures->want(gone_file);
        ASSERT_TRUE(rig.settle());
        write_file(fs::path(rig.config.dir) / "odd name.png", "x");                     // (a file that is no picture of ours: a name that no map file has)
        ASSERT_EQ(rig.pictures->prune([](const std::string& stem) { return stem == "stays-1"; }), size_t{2});
        ASSERT_TRUE(rig.pictures->ready(keep_file) && !rig.pictures->ready(gone_file));
        ASSERT_TRUE(fs::exists(rig.picture_file("stays-1")) && !fs::exists(rig.picture_file("goes-1")) && !fs::exists(fs::path(rig.config.dir) / "odd name.png"));
        ASSERT_EQ(rig.pictures->prune([](const std::string&) { return true; }), size_t{0});
        ASSERT_EQ(rig.pictures->get(gone_file), std::string());                          // (a map that is asked for again is made again)
        ASSERT_TRUE(rig.settle());
        ASSERT_TRUE(rig.pictures->ready(gone_file));
    } TEST_END();

    TEST_CASE("MP6.2 A picture that was taken out of the folder is made again; a map whose name no picture can have (a space, a dot) is never asked for") {
        PictureRig rig;
        const std::string file = rig.map("vanishes");
        rig.pictures->want(file);
        ASSERT_TRUE(rig.settle());
        fs::remove(rig.picture_file("vanishes"));
        ASSERT_EQ(rig.pictures->get(file), std::string());
        ASSERT_EQ(rig.pictures->queued(), size_t{1});
        ASSERT_TRUE(rig.settle());
        ASSERT_EQ(rig.pictures->get(file), framed_png(300, 200));
        rig.pictures->want(rig.map("a b"));
        rig.pictures->want(rig.map("a.b"));
        ASSERT_EQ(rig.pictures->queued(), size_t{0});
    } TEST_END();

    // =================================================================================================================================================
    // The real picture maker, ants_thumb, on real maps
    // =================================================================================================================================================
    const char* const thumb_env = std::getenv("ANTS_THUMB");
    const std::string thumb = thumb_env != nullptr ? thumb_env : std::string();
    const std::string archive = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";

    // the size of a PNG (IHDR) and its frame: the server's own look at it; the pixels are looked at by tests/scripts/test_thumbnail_png.py with the zlib of Python
    const auto size_of_png = [](const std::string& png, uint32_t& w, uint32_t& h) {
        if (png.size() < 33 || png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0 || png.compare(12, 4, "IHDR") != 0) return false;
        const auto be = [&](size_t at) { return (uint32_t{static_cast<unsigned char>(png[at])} << 24) | (uint32_t{static_cast<unsigned char>(png[at + 1])} << 16) | (uint32_t{static_cast<unsigned char>(png[at + 2])} << 8) | uint32_t{static_cast<unsigned char>(png[at + 3])}; };
        w = be(16);
        h = be(20);
        return true;
    };

    if (thumb.empty()) {
        std::cout << "  (ANTS_THUMB is not set: the cases of the real picture maker are left out)\n";
    } else {
        TEST_CASE("MP7.1 ants_thumb draws a shipped map as the setup screen does: a PNG of 300 x 300 pixels of a few dozen kilobytes, the same bytes every time, in well under the time limit") {
            PictureRig rig("unused", [&](MapPicturesConfig& c) { c.program = thumb; c.archive = archive; });
            ASSERT_TRUE(rig.opened && rig.pictures->enabled());
            const std::string first_file = rig.map("tiny-a", read_file(fs::path(maps_dir()) / "TINY.LVL"));
            const std::string second_file = rig.map("tiny-b", read_file(fs::path(maps_dir()) / "TINY.LVL"));
            const auto began = std::chrono::steady_clock::now();
            rig.pictures->want(first_file);
            ASSERT_TRUE(rig.settle(30000));
            const long long took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
            rig.pictures->want(second_file);
            ASSERT_TRUE(rig.settle(30000));
            const std::string a = rig.pictures->get(first_file), b = rig.pictures->get(second_file);
            uint32_t w = 0, h = 0;
            ASSERT_TRUE(size_of_png(a, w, h) && w == 300 && h == 300);
            ASSERT_TRUE(a.size() > 5000 && a.size() < 200000);
            ASSERT_EQ(a, b);                                                             // (the same map, the same bytes: the picture does not depend on when or where it was made)
            ASSERT_EQ(rig.pictures->failures(), size_t{0});
            ASSERT_TRUE(took < 20000);
        } TEST_END();

        TEST_CASE("MP7.2 A map that is not square is drawn in the middle of the 300 x 300 box; a map too big for one image still gets a picture (the minimap colors, as the setup screen shows it)") {
            PictureRig rig("unused", [&](MapPicturesConfig& c) { c.program = thumb; c.archive = archive; });
            const std::string wide = grown_tiny(40, 120);
            const std::string big = grown_tiny(170, 420);                                // (71,400 cells: the world is over 2^25 pixels, which the renderer does not draw as one image)
            ASSERT_TRUE(!wide.empty() && !big.empty());
            const std::string wide_file = rig.map("wide-map", wide);
            const std::string big_file = rig.map("big-map", big);
            rig.pictures->want(wide_file);
            rig.pictures->want(big_file);
            ASSERT_TRUE(rig.settle(60000));
            uint32_t w = 0, h = 0;
            ASSERT_TRUE(size_of_png(rig.pictures->get(wide_file), w, h) && w == 300 && h == 300);
            ASSERT_TRUE(size_of_png(rig.pictures->get(big_file), w, h) && w == 300 && h == 300);
            ASSERT_EQ(rig.pictures->failures(), size_t{0});
        } TEST_END();

        TEST_CASE("MP7.3 Bytes that are no map (garbage, a cut file, an empty one) make no picture and cost one process each; the next map is drawn as usual") {
            PictureRig rig("unused", [&](MapPicturesConfig& c) { c.program = thumb; c.archive = archive; });
            const std::string tiny = read_file(fs::path(maps_dir()) / "TINY.LVL");
            const std::string garbage = rig.map("garbage-map", std::string(5000, '\x5a'));
            const std::string cut = rig.map("cut-map", tiny.substr(0, tiny.size() / 2));
            const std::string good = rig.map("good-map", tiny);
            rig.pictures->want(garbage);
            rig.pictures->want(cut);
            rig.pictures->want(good);
            ASSERT_TRUE(rig.settle(60000));
            ASSERT_FALSE(rig.pictures->ready(garbage) || rig.pictures->ready(cut));
            ASSERT_TRUE(rig.pictures->ready(good));
            ASSERT_EQ(rig.pictures->failures(), size_t{2});
            const std::vector<std::string> notices = rig.pictures->take_notices();
            ASSERT_TRUE(notices.size() == 2 && notices[0].find("code 3") != std::string::npos && notices[0].find("cannot be read") != std::string::npos);
        } TEST_END();

        TEST_CASE("MP7.4 ants_thumb refuses a bad command line (exit 2) and an archive that is not one (exit 3), and writes no file") {
            const fs::path out = fs::temp_directory_path() / ("ants_thumb_cli_" + std::to_string(getpid()) + ".png");
            fs::remove(out);
            const std::string quoted = "\"" + thumb + "\"";
            const std::string tiny = (fs::path(maps_dir()) / "TINY.LVL").string();
            ASSERT_EQ(std::system((quoted + " > /dev/null 2>&1").c_str()) >> 8, 2);
            ASSERT_EQ(std::system((quoted + " --chd x --out \"" + out.string() + "\" --memory-mb 1 \"" + tiny + "\" > /dev/null 2>&1").c_str()) >> 8, 2);
            ASSERT_EQ(std::system((quoted + " --chd \"" + (fs::temp_directory_path() / "no-archive.chd").string() + "\" --out \"" + out.string() + "\" \"" + tiny + "\" > /dev/null 2>&1").c_str()) >> 8, 3);
            ASSERT_FALSE(fs::exists(out));
        } TEST_END();
    }
#endif

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

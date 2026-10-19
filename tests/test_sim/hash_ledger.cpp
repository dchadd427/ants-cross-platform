// The hash ledger: one line for each of the 540 maps of the library with the full state hash of the scripted play (the four teams, the map sweep's players and seeds,
// tests/common/map_library.hpp) at tick 0, tick 200 and tick 2,400, written to a file that two builds can be compared with (`diff`): a change that must not move any play of
// any map (docs/TESTING.md, "The hash ledger") leaves the file byte for byte as it was on main.
//
//   hash_ledger --out FILE [--extended] [--only TEXT] [--ticks A,B,...]
//
// By default the maps are played as the library has them (version 8). With --extended every map goes up to an Extended map of rules 0 (convert_to_extended) and the simulation
// plays that one: its ledger must equal the other (the simulation reads the Extended map as it reads the Classic one). A line is `NAME TAB hash@t0 TAB hash@t1 ...` (16 hex
// digits each); a play that ends before a tick (the match is over) gives the hash of its last tick there, marked with a `!` after the hash, so that two builds that disagree about
// the end of a match differ in the file too. Exit status 0 when the file is written, 1 when a map does not load or a conversion fails, 2 for a bad option.
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/sim_engine.hpp"
#include "map_library.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using ants::assets::LevelData;

namespace {

std::string hex16(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

bool parse_ticks(const std::string& text, std::vector<uint32_t>& out) {
    out.clear();
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ',')) {
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return false;
        const unsigned long v = std::strtoul(part.c_str(), nullptr, 10);
        if (v % ants_test::kLibraryTicksPerSecond != 0 || v > 1000000) return false;     // (the ledger reads the checkpoints of play_hashes: every game second)
        out.push_back(static_cast<uint32_t>(v));
    }
    return !out.empty();
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string out_path;
    std::string only;
    bool extended = false;
    std::vector<uint32_t> ticks{0, 200, 2400};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--only" && i + 1 < argc) {
            only = argv[++i];
        } else if (arg == "--extended") {
            extended = true;
        } else if (arg == "--ticks" && i + 1 < argc) {
            if (!parse_ticks(argv[++i], ticks)) {
                std::cerr << "--ticks wants whole game seconds in ticks (multiples of 20), comma separated\n";
                return 2;
            }
        } else {
            std::cerr << "usage: hash_ledger --out FILE [--extended] [--only TEXT] [--ticks A,B,...]\n";
            return 2;
        }
    }
    if (out_path.empty()) {
        std::cerr << "usage: hash_ledger --out FILE [--extended] [--only TEXT] [--ticks A,B,...]\n";
        return 2;
    }
    const uint32_t last = *std::max_element(ticks.begin(), ticks.end());
    std::string text;
    int failures = 0;
    int written = 0;
    for (const ants_test::LibraryMap& m : ants_test::library()) {
        if (!only.empty() && m.name.find(only) == std::string::npos) continue;
        LevelData level;
        if (!level.load_from_file(m.path)) {
            std::cerr << m.name << ": does not load\n";
            ++failures;
            continue;
        }
        if (extended) {
            const ants::assets::LevelConversion up = ants::assets::convert_to_extended(level);
            if (!up.ok) {
                std::cerr << m.name << ": does not go up to an Extended map\n";
                ++failures;
                continue;
            }
            level = up.level;
        }
        const std::vector<ants::sim::StateHash> hashes = ants_test::play_hashes(level, last);
        text += m.name;
        for (const uint32_t t : ticks) {
            const size_t index = t / ants_test::kLibraryTicksPerSecond;
            const bool ended_before = index >= hashes.size();
            text += "\t" + hex16(hashes[ended_before ? hashes.size() - 1 : index].total) + (ended_before ? "!" : "");
        }
        text += "\n";
        ++written;
    }
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        std::cerr << "cannot write " << out_path << "\n";
        return 1;
    }
    std::cout << "hash ledger: " << written << " maps, ticks";
    for (const uint32_t t : ticks) std::cout << " " << t;
    std::cout << (extended ? " (as Extended maps)" : "") << " -> " << out_path << "\n";
    return failures == 0 ? 0 : 1;
}

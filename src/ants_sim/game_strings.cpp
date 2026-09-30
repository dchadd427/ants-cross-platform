#include "ants_sim/game_strings.hpp"

#include <cstddef>

namespace ants::sim::strings {

namespace {

struct Entry {
    uint16_t id;
    const char* text;
    bool blink;
};

// Sorted by id. Every text is the original's (string_ids of Ants.exe); the title of string 5 is inserted by the caller.
constexpr Entry kTable[] = {
    {1, "%s (%s) invites you to form a team.  Would you like to accept?", false},
    {2, "%s (%s) invites you to form a team.  This will remove you from the team you have with %s (%s).  Would you like to accept?", false},
    {3, "Waiting for %s (%s) to respond to your offer to team up.", false},
    {4, "Doing this will break your team with %s (%s).  Continue?", false},
    {5, "Welcome to %s!", false},
    {6, "Ready!", false},
    {7, "BomberAnt selected.", false},
    {8, "Where to?", false},
    {9, "Thief here", false},
    {10, "Yessir!", false},
    {11, "SwimmerAnt selected.", false},
    {12, "Ready!", false},
    {13, "You need 200 points to hatch!", false},
    {14, "An Ant is already hatching!", false},
    {15, "Hatching a new Ant!", false},
    {16, "No eggs to hatch!", false},
    {17, "Can't - already have food.", true},
    {18, "Now you are in for it!", false},
    {19, "Let me be!", false},
    {20, "Attack!", false},
    {21, "Do you want to ally?", false},
    {39, "%s (%s) and %s (%s) are a team now!", false},
    {40, "%s (%s) and %s (%s) are no longer a team!", false},
    {46, "%s dropped out of the game!", false},
    {48, "Can't do that...", false},
    {49, "1 minute left in the game.", true},
    {50, "30 seconds left in the game.", true},
    {51, "Ant dead.", false},
    {52, "Ant drowned.", false},
    {53, "A ThiefAnt is at your anthill!", true},
    {54, "Stopping.", false},
    {55, "Ouch!", false},
    {56, "Bomb dropped.", false},
    {57, "Bomb defused.", false},
    {58, "Can't go there.", false},
    {59, "10 seconds and counting...", true},
    {60, "Got Food!", false},
    {61, "Score going up...", false},
    {62, "Food stolen...", false},
    {63, "Ready!", false},
    {64, "Fire put out.", false},
    {65, "Starting a fire...", false},
    {66, "On my way.", false},
    {67, "Attack!", false},
    {68, "Movin' out.", false},
    {69, "My pleasure...", false},
    {70, "Here I go...", false},
    {71, "Burn...", false},
    {75, "A team has been made.", true},
    {80, "%s rejected teaming up", false},
    {81, "%s accepted teaming up", false},
    {82, "%s withdrew offer to team up", false},
    {100, "Black", false},
    {101, "Blue", false},
    {102, "Red", false},
    {103, "Green", false},
    {104, "Waiting for others...", false},
    {105, "Get ready to play!  You are the %s Ants.", false},
    {111, "Waiting for scores...", false},
};

const Entry* find(uint16_t id) noexcept {
    size_t lo = 0;
    size_t hi = sizeof(kTable) / sizeof(kTable[0]);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (kTable[mid].id == id) return &kTable[mid];
        if (kTable[mid].id < id) lo = mid + 1; else hi = mid;
    }
    return nullptr;
}

}  // namespace

const char* text(uint16_t id) noexcept {
    const Entry* e = find(id);
    return e ? e->text : nullptr;
}

bool blinks(uint16_t id) noexcept {
    const Entry* e = find(id);
    return e && e->blink;
}

std::string format(uint16_t id, const std::string& a, const std::string& b, const std::string& c, const std::string& d) {
    const char* t = text(id);
    if (!t) return {};
    const std::string* args[4] = {&a, &b, &c, &d};
    size_t next_arg = 0;
    std::string out;
    for (const char* p = t; *p; ++p) {
        if (p[0] == '%' && p[1] == 's') {
            if (next_arg < 4) out += *args[next_arg++];
            ++p;
        } else {
            out += *p;
        }
    }
    return out;
}

const char* colour_name(uint8_t colour_index) noexcept {
    return colour_index < 4 ? text(static_cast<uint16_t>(kColourBlack + colour_index)) : "";
}

}  // namespace ants::sim::strings

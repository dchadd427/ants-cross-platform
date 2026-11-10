// Tests of the chat limit of a guest in a room of a server that checks names (flood.hpp kGuestChatRate, HostLobby::Config guest_chat): a burst of 3 lines, then one every 3 s (20 a minute), an excess of 10;
// a person whose name was checked keeps 5, one a second, 20; a room that does not check names is what it was. The seat's own flag decides (set at the door), never the form of its name (G5.1 - G5.5).
#include "lobby_fixtures.hpp"

#include "ants_net/flood.hpp"
#include "ants_net/person_name.hpp"

namespace {

// ---- the reference: the generic cell rate algorithm, which says the same as a token bucket in a different way (a theoretical arrival time, no tokens), in whole milliseconds ----------------------------------
// `period` ms is the time of one line, `burst` lines may come at once. A line is within the budget when the theoretical time of the line before it is at most (burst - 1) periods away; the lines that are not go
// to the excess, which is the same again with its own burst. A line that is not within a budget does not move its time.
struct Gcra {
    int64_t period;
    int64_t burst;
    int64_t excess;
    bool lines_set{false};
    bool excess_set{false};
    int64_t lines_at{0};
    int64_t excess_at{0};
    char take(int64_t now) {
        const int64_t t = lines_set ? std::max(lines_at, now) : now;
        if (t - now <= (burst - 1) * period) {
            lines_set = true;
            lines_at = t + period;
            return 'R';
        }
        const int64_t e = excess_set ? std::max(excess_at, now) : now;
        if (e - now <= (excess - 1) * period) {
            excess_set = true;
            excess_at = e + period;
            return 'D';
        }
        return 'O';
    }
};
Gcra logged_in_model() { return Gcra{1000, int64_t{kChatBurst}, int64_t{kChatExcessBurst}}; }
Gcra guest_model() { return Gcra{60000 / int64_t{kGuestChatPerMinute}, int64_t{kGuestChatBurst}, int64_t{kGuestChatExcessBurst}}; }

struct Draw {                                    // a small seeded generator for the fresh sequences
    uint64_t s;
    explicit Draw(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 12345u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 11);
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// A gap (ms) between two lines, mostly small, with the numbers around the edges of both rates (1 s and 3 s) over-represented
uint32_t random_gap(Draw& r, bool in_tens) {
    static const uint32_t kGaps[] = {0, 0, 0, 10, 20, 50, 100, 300, 500, 990, 1000, 1010, 1500, 2000, 2990, 3000, 3010, 4000, 6000, 9000, 20000};
    const uint32_t g = kGaps[r.below(sizeof(kGaps) / sizeof(kGaps[0]))] + (in_tens ? r.below(8) * 10 : r.below(8));
    return g;
}

std::string run_budget(const std::vector<uint32_t>& times, const ChatRate* rate, bool old_call) {
    ChatBudget b;
    std::string out;
    for (const uint32_t t : times) {
        const ChatBudget::Verdict v = rate != nullptr ? b.take(t, *rate) : (old_call ? b.take(t) : b.take(t, 5, 1, 20));
        out += v == ChatBudget::Verdict::Relay ? 'R' : v == ChatBudget::Verdict::Drop ? 'D' : 'O';
    }
    return out;
}

// ---- a server's lobby room whose guests come through the door: the Hello is read by the caller and handed over with the flag, as RoomManager does ------------------------------------------------------------
HostLobby::Config door_config(uint32_t seed, bool guest_chat, bool keys = true) {
    HostLobby::Config hc = lobby_room_config(seed);
    hc.unique_names = true;
    hc.fixed_names = true;
    hc.guest_chat = guest_chat;
    hc.forgive_ms = 0;                           // (nothing is forgiven: the count of violations is the model's)
    if (!keys) hc.make_key = nullptr;            // (an operator's room without reconnect: a second equal guest name gets " (2)")
    return hc;
}

size_t join_door(Room& room, const ClientLobby::Config& cc, bool unverified) {
    auto ends = room.net.connect({10, 0});
    room.guests.emplace_back();
    Room::Guest& g = room.guests.back();
    g.host_end = ends.first;
    g.client_end = ends.second;
    g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
    std::vector<uint8_t> hello;
    for (int i = 0; i < 100 && !ends.first->poll(hello); ++i) {
        room.now += 10;
        room.net.set_time(room.now);
        g.lobby->update(room.now);
    }
    room.host.add_connection(ends.first, room.now, std::string(), hello, false, unverified);
    room.run(100);
    return room.guests.size() - 1;
}
size_t join_door(Room& room, const std::string& name, bool unverified) {
    ClientLobby::Config cc;
    cc.name = name;
    return join_door(room, cc, unverified);
}

void say(Room& room, size_t guest, const std::string& text = "line") {
    ChatMsg m;
    m.text = text;
    room.guests[guest].client_end->send(encode(m));
}

size_t relayed(Room& room, size_t guest) {
    size_t n = 0;
    const uint8_t seat = room.guests[guest].lobby->my_seat();
    for (const ChatLine& l : room.host.take_chat()) n += !l.notice() && l.seat == seat ? size_t{1} : size_t{0};
    return n;
}

// Said `lines` at once, run 100 ms, and how many were relayed
size_t burst_of(Room& room, size_t guest, int lines) {
    for (int i = 0; i < lines; ++i) say(room, guest, "b" + std::to_string(i));
    room.run(100);
    return relayed(room, guest);
}

std::string name_of(const Room& room, size_t guest) { return room.host.room().slots[room.guests[guest].lobby->my_seat()].name; }

}  // namespace

void run_lobby_guest_chat_tests() {
    TEST_CASE("G5.1 Chat Rates (The Arithmetic): The Old Rate (5, One A Second, 20) Gives The Verdicts Of A Recorded Sequence, Whichever Call Is Used; The Guest Rate (3, 20 A Minute, 10) Takes A Line After Exactly 3000 ms And Not After 2999; Both Equal A Reference Model On 600 Fresh Random Sequences, Also Across The Wrap Of The Clock") {
        static const uint32_t kRecorded[] = {0, 0, 0, 0, 0, 0, 0, 400, 900, 1000, 1000, 1900, 2000, 2000, 2900, 3000, 3000, 3100, 3500, 4000, 4000, 4000, 4000, 4000, 5000, 5000, 6000, 6000, 7000, 7500, 7600,
                                              7700, 7800, 7900, 8000, 8100, 8200, 8300, 8400, 8500, 8600, 8700, 8800, 8900, 9000, 9100, 9200, 9300, 9400, 9500, 9600, 9700, 9800, 9900, 10000, 10100, 10200,
                                              10300, 10400, 10500, 10600, 10700, 10800};
        const std::vector<uint32_t> recorded(std::begin(kRecorded), std::end(kRecorded));
        // the verdicts of the old rate on that sequence, as the old arithmetic gave them (the same string comes from a build without the guest rate: the pinned numbers)
        const std::string old_verdicts = "RRRRRDDDDRDDRDDRDDDRDDDDRDRDRDDDDDRDDDDDDOOORDOOOOOOOORDOOOOOOO";
        ASSERT_EQ(run_budget(recorded, nullptr, true), old_verdicts);
        ASSERT_EQ(run_budget(recorded, nullptr, false), old_verdicts);                       // (the explicit numbers of the old call)
        ASSERT_EQ(run_budget(recorded, &kChatRate, false), old_verdicts);                    // (the same rate in lines a minute says the same)
        ASSERT_EQ(run_budget(recorded, &kGuestChatRate, false), std::string("RRRDDDDDDDDDDOORDOOOOOOOOORDOOOOOOOOOOOOOOOORDOOOOOOOOOOOOOOOOO"));
        ASSERT_TRUE(kChatRate.burst == kChatBurst && kChatRate.per_minute == 60u * kChatPerSecond && kChatRate.excess_burst == kChatExcessBurst);
        ASSERT_TRUE(kGuestChatRate.burst == 3u && kGuestChatRate.per_minute == 20u && kGuestChatRate.excess_burst == 10u);
        for (const uint32_t after : {2999u, 3000u}) {
            ChatBudget b;
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(b.take(1000, kGuestChatRate) == ChatBudget::Verdict::Relay);
            ASSERT_TRUE(b.take(1000, kGuestChatRate) == ChatBudget::Verdict::Drop);          // the fourth at once
            ASSERT_TRUE(b.take(1000 + after, kGuestChatRate) == (after == 3000u ? ChatBudget::Verdict::Relay : ChatBudget::Verdict::Drop));
        }
        {   // the unit of the per-minute count over a long run: 25 lines at exactly 3000 ms apart are all relayed, and one 2999 ms after the last is dropped (a unit one too small would let it through)
            ChatBudget b;
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(b.take(0, kGuestChatRate) == ChatBudget::Verdict::Relay);
            uint32_t at = 0;
            for (int i = 0; i < 25; ++i) {
                at += 3000;
                ASSERT_TRUE(b.take(at, kGuestChatRate) == ChatBudget::Verdict::Relay);
            }
            ASSERT_TRUE(b.take(at + 2999, kGuestChatRate) == ChatBudget::Verdict::Drop);
        }
        {   // a quiet minute fills the burst again, and no more than the burst
            ChatBudget b;
            for (int i = 0; i < 30; ++i) b.take(0, kGuestChatRate);
            std::string out;
            for (int i = 0; i < 6; ++i) out += b.take(600000, kGuestChatRate) == ChatBudget::Verdict::Relay ? 'R' : '-';
            ASSERT_EQ(out, std::string("RRR---"));
        }
        {   // the excess of a guest: 10 lines beyond the budget, then every line is an offence; it refills at 20 a minute
            ChatBudget b;
            for (int i = 0; i < 3; ++i) b.take(0, kGuestChatRate);
            int drops = 0;
            while (b.take(0, kGuestChatRate) == ChatBudget::Verdict::Drop) ++drops;
            ASSERT_EQ(drops, 10);
            ASSERT_TRUE(b.take(0, kGuestChatRate) == ChatBudget::Verdict::Offence);
            ASSERT_TRUE(b.take(2999, kGuestChatRate) == ChatBudget::Verdict::Offence);       // (the excess is refilled at the rate of the lines: one in 3 s)
            ASSERT_TRUE(b.take(3000, kGuestChatRate) == ChatBudget::Verdict::Relay);        // (a line is back in the lines' bucket at 3000 ms ...)
            ASSERT_TRUE(b.take(3000, kGuestChatRate) == ChatBudget::Verdict::Drop);         // (... and one in the excess's)
            ASSERT_TRUE(b.take(3000, kGuestChatRate) == ChatBudget::Verdict::Offence);
        }
        // fresh sequences: both rates against the model, every time of the clock shifted so that some runs cross the wrap of 32 bits
        size_t checked = 0;
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            Draw r(seed * 7919u + 3u);
            const uint32_t start = seed % 3 == 0 ? 0xFFFFF000u : (seed % 3 == 1 ? 0u : r.next());
            std::vector<uint32_t> times;
            std::vector<int64_t> exact;
            int64_t t = 0;
            for (int i = 0; i < 160; ++i) {
                t += random_gap(r, false);
                times.push_back(start + static_cast<uint32_t>(t));
                exact.push_back(t);
            }
            for (int kind = 0; kind < 2; ++kind) {
                Gcra model = kind == 0 ? logged_in_model() : guest_model();
                std::string expected;
                for (const int64_t at : exact) expected += model.take(at);
                const std::string got = kind == 0 ? run_budget(times, nullptr, true) : run_budget(times, &kGuestChatRate, false);
                ASSERT_MSG(got == expected, "seed " + std::to_string(seed) + " kind " + std::to_string(kind) + "\n" + got + "\n" + expected);
                if (kind == 0) ASSERT_EQ(run_budget(times, &kChatRate, false), got);        // (the old rate through the per-minute call: the same verdicts)
                ++checked;
            }
        }
        ASSERT_EQ(checked, size_t{600});
    } TEST_END();

    TEST_CASE("G5.2 A Guest In The Waiting Room: Lines 1 To 3 Are Relayed At Once, The Fourth Is Dropped, One More Goes After 3000 ms And Not After 2990; A Logged-in Person In The Same Room Keeps 5 And One A Second; Each Person Has A Budget Of Their Own") {
        {   // the burst, and the one line that comes back after three seconds
            Room room(door_config(300, true));
            const size_t guest = join_door(room, "Guest 111111", true);
            const size_t other = join_door(room, "Ann", false);
            ASSERT_EQ(burst_of(room, guest, 4), size_t{3});
            room.run(2900);                                                                  // (the dropped line was handled about 100 ms ago; the lines of this step come 3000 ms after it)
            ASSERT_EQ(burst_of(room, guest, 1), size_t{1});                                  // 3000 ms after the fourth line was handled
            ASSERT_EQ(burst_of(room, guest, 1), size_t{0});                                  // and not a second one
            // a logged-in person in the same room: 5 at once, then one after a second (not after 990 ms)
            ASSERT_EQ(burst_of(room, other, 7), size_t{5});
            room.run(800);
            ASSERT_EQ(burst_of(room, other, 1), size_t{0});                                  // (900 ms after the burst: a line is not whole yet)
            ASSERT_EQ(burst_of(room, other, 1), size_t{1});                                  // (1000 ms after it: it is)
            ASSERT_TRUE(room.host.occupied(room.guests[guest].lobby->my_seat()) && room.host.occupied(room.guests[other].lobby->my_seat()));
        }
        {   // the edge: the guest's fourth line is dropped at H; a line handled at H + 2990 is dropped, one handled at H + 3000 is relayed (two rooms, the same steps)
            for (const uint32_t wait : {2990u, 3000u}) {
                Room room(door_config(301, true));
                const size_t guest = join_door(room, "Guest 222222", true);
                for (int i = 0; i < 4; ++i) say(room, guest, "e" + std::to_string(i));       // (all four are handled in the same step: the fourth is dropped there)
                room.run(wait);
                say(room, guest, "edge");                                                    // (sent `wait` ms after the four: handled `wait` ms after the fourth)
                room.run(100);
                ASSERT_EQ(relayed(room, guest), size_t{3} + (wait == 3000u ? 1u : 0u));
            }
        }
        {   // two seats, two budgets: one person's lines take nothing from the other's
            Room room(door_config(302, true));
            const size_t a = join_door(room, "Guest 333333", true);
            const size_t b = join_door(room, "Guest 444444", true);
            ASSERT_EQ(burst_of(room, a, 9), size_t{3});
            ASSERT_EQ(burst_of(room, b, 9), size_t{3});
        }
    } TEST_END();

    TEST_CASE("G5.3 The Seat's Own Flag Decides, Not The Form Of Its Name: A Seat That Was Not Checked Has The Guest's Rate Whatever It Is Called (Also A Second Equal Guest Name, Which A Room Without Keys Makes Guest 123456 (2)); A Checked Seat With A Name Of The Guest Form Has The Logged-in Rate; With The Setting Off, Or On A Host That Does Not Check Names, Everybody Has 5 And One A Second") {
        {   // a room without keys: the second person with the same guest name is "Guest 123456 (2)", which has no guest form, and is limited all the same
            Room room(door_config(310, true, false));
            const size_t first = join_door(room, "Guest 123456", true);
            const size_t second = join_door(room, "Guest 123456", true);
            ASSERT_EQ(name_of(room, first), std::string("Guest 123456"));
            ASSERT_EQ(name_of(room, second), std::string("Guest 123456 (2)"));
            ASSERT_TRUE(is_guest_name(name_of(room, first)) && !is_guest_name(name_of(room, second)));
            ASSERT_EQ(burst_of(room, first, 8), size_t{3});
            ASSERT_EQ(burst_of(room, second, 8), size_t{3});
            ASSERT_TRUE(room.host.unverified(room.guests[second].lobby->my_seat()));
        }
        {   // a seat that was checked and is called like a guest (nothing can make that on the real door; the flag is what counts): 5, not 3
            Room room(door_config(311, true));
            const size_t checked = join_door(room, "Guest 555555", false);
            const size_t drawn = join_door(room, "Zed", true);                               // (a name that is no guest name, not checked: limited)
            ASSERT_FALSE(room.host.unverified(room.guests[checked].lobby->my_seat()));
            ASSERT_EQ(burst_of(room, checked, 8), size_t{5});
            ASSERT_EQ(burst_of(room, drawn, 8), size_t{3});
        }
        {   // the setting off: a server room that does not set it, whatever the flag of the seat
            Room room(door_config(312, false));
            const size_t guest = join_door(room, "Guest 666666", true);
            ASSERT_EQ(burst_of(room, guest, 8), size_t{5});
            room.run(1000);
            ASSERT_EQ(burst_of(room, guest, 3), size_t{1});
        }
        {   // a LAN or direct host: no door, no flag, the connection comes by the old call
            Room room(lobby_room_config(313));
            const size_t a = room.join_seat("Guest 777777");
            ASSERT_FALSE(room.host.unverified(room.guests[a].lobby->my_seat()));
            ASSERT_EQ(burst_of(room, a, 8), size_t{5});
        }
        {   // no seat: nothing is unverified
            Room room(door_config(314, true));
            ASSERT_FALSE(room.host.unverified(0) || room.host.unverified(3) || room.host.unverified(200));
        }
    } TEST_END();

    TEST_CASE("G5.4 The Excess: A Guest Who Keeps Typing A Line A Second Is Dropped, Then Each Line Is An Offence After The Excess Of 10, And Eight Throw Them Out (The Line Is Counted And Is The Model's); A Logged-in Person At The Same Pace Is Never Touched; A Line Every 3.1 s Is Never Dropped") {
        {
            Room room(door_config(320, true));
            const size_t guest = join_door(room, "Guest 888888", true);
            const size_t other = join_door(room, "Ann", false);
            Gcra model = guest_model();
            int offences = 0, lines = 0, expected_lines = 0;
            size_t relayed_lines = 0, expected_relayed = 0;
            int64_t at = 0;
            for (int i = 0; i < 400 && expected_lines == 0; ++i) {                           // (the model: the line at which the eighth offence comes)
                const char v = model.take(at);
                if (v == 'R') ++expected_relayed;
                if (v == 'O' && ++offences == 8) expected_lines = i + 1;
                at += 1000;
            }
            ASSERT_TRUE(expected_lines > 30 && expected_lines < 80);                         // (3 at once, 10 more of excess, a third of a line refilled each second)
            const uint8_t seat = room.guests[guest].lobby->my_seat();
            while (room.host.occupied(seat) && lines < 400) {
                say(room, guest, "l" + std::to_string(lines));
                say(room, other, "o" + std::to_string(lines));
                ++lines;
                room.run(1000);
                for (const ChatLine& l : room.host.take_chat()) relayed_lines += !l.notice() && l.seat == seat ? size_t{1} : size_t{0};
            }
            ASSERT_EQ(lines, expected_lines);                                                // thrown out by the line that the model says
            ASSERT_EQ(relayed_lines, expected_relayed);
            ASSERT_TRUE(room.host.occupied(room.guests[other].lobby->my_seat()));            // the logged-in person typed as fast: a line a second is the budget's own rate
        }
        {   // a person who keeps to a line every 3.1 s says every line for ten minutes
            Room room(door_config(321, true));
            const size_t guest = join_door(room, "Guest 999999", true);
            size_t heard = 0;
            for (int i = 0; i < 190; ++i) {
                say(room, guest, "slow " + std::to_string(i));
                room.run(3100);
                heard += relayed(room, guest);
            }
            ASSERT_EQ(heard, size_t{190});
            ASSERT_TRUE(room.host.occupied(room.guests[guest].lobby->my_seat()));
        }
        {   // two lines in three seconds are the guest's rate too, once the burst is used: a line every 1.5 s for ten minutes drops a half and throws nobody out
            Room room(door_config(322, true));
            const size_t guest = join_door(room, "Guest 121212", true);
            Gcra model = guest_model();
            size_t heard = 0, expected = 0;
            for (int i = 0; i < 400; ++i) {
                say(room, guest, "two " + std::to_string(i));
                expected += model.take(int64_t{i} * 1500) == 'R' ? size_t{1} : size_t{0};
                room.run(1500);
                heard += relayed(room, guest);
            }
            ASSERT_TRUE(room.host.occupied(room.guests[guest].lobby->my_seat()));
            ASSERT_EQ(heard, expected);
            ASSERT_TRUE(heard >= 200 && heard <= 206);                                       // 20 a minute for ten minutes, and the burst
        }
    } TEST_END();

    TEST_CASE("G5.5 Fresh Inputs: 60 Rooms With A Guest And A Logged-in Person Each Say 120 Random Lines At Random Gaps; What Is Relayed, Dropped And Who Is Thrown Out Is The Model's For Each Of The Two") {
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            Draw r(seed * 104729u + 17u);
            Room room(door_config(400 + seed, true));
            const size_t guest = join_door(room, "Guest 13579" + std::to_string(seed % 10), true);
            const size_t person = join_door(room, "Ann", false);
            const uint8_t seats[2] = {room.guests[guest].lobby->my_seat(), room.guests[person].lobby->my_seat()};
            const size_t who[2] = {guest, person};
            Gcra model[2] = {guest_model(), logged_in_model()};
            int offences[2] = {0, 0};
            bool out[2] = {false, false};
            size_t expected[2] = {0, 0}, got[2] = {0, 0};
            // one timeline of 10 ms steps; each seat has its own next line; the clock of the room is the model's clock plus a constant (every line is handled 10 ms after it is sent)
            int64_t next[2] = {0, 0};
            int sent[2] = {0, 0};
            for (int64_t t = 0; t < 400000 && (sent[0] < 120 || sent[1] < 120); t += 10) {
                for (int k = 0; k < 2; ++k) {
                    while (sent[k] < 120 && next[k] == t) {
                        say(room, who[k], "r" + std::to_string(sent[k]));
                        if (!out[k]) {
                            const char v = model[k].take(t);
                            if (v == 'R') ++expected[k];
                            if (v == 'O' && ++offences[k] == 8) out[k] = true;
                        }
                        ++sent[k];
                        next[k] += random_gap(r, true);
                    }
                }
                room.run(10);
                for (const ChatLine& l : room.host.take_chat()) {
                    if (l.notice()) continue;
                    for (int k = 0; k < 2; ++k) got[k] += l.seat == seats[k] ? size_t{1} : size_t{0};
                }
            }
            room.run(200);
            for (const ChatLine& l : room.host.take_chat()) {
                for (int k = 0; k < 2; ++k) got[k] += !l.notice() && l.seat == seats[k] ? size_t{1} : size_t{0};
            }
            for (int k = 0; k < 2; ++k) {
                ASSERT_MSG(got[k] == expected[k], "seed " + std::to_string(seed) + " seat " + std::to_string(k) + ": " + std::to_string(got[k]) + " relayed, the model says " + std::to_string(expected[k]));
                ASSERT_MSG(room.host.occupied(seats[k]) == !out[k], "seed " + std::to_string(seed) + " seat " + std::to_string(k) + ": thrown out or not");
            }
        }
    } TEST_END();

    TEST_CASE("G5.6 The Flag Goes With The Seat: A Guest Who Moves To Another Color Keeps The Guest's Rate, A Seat That Changes Places With A Checked Person Takes Its Flag With It; A Rematch Hands The Flag On (A Guest Is A Guest In The Seat Reserved For Them, A Checked Person Is Not)") {
        {
            Room room(door_config(330, true));
            const size_t guest = join_door(room, "Guest 141414", true);
            const size_t person = join_door(room, "Ann", false);
            const uint8_t from = room.guests[guest].lobby->my_seat(), to = room.guests[person].lobby->my_seat();
            ASSERT_TRUE(room.host.move_seat(from, 3));                                       // a free color
            room.run(100);
            ASSERT_TRUE(room.host.unverified(3) && !room.host.unverified(from));
            ASSERT_EQ(burst_of(room, guest, 8), size_t{3});
            ASSERT_TRUE(room.host.move_seat(3, to));                                         // the two change places
            room.run(100);
            ASSERT_TRUE(room.host.unverified(to) && !room.host.unverified(3));
            ASSERT_EQ(burst_of(room, person, 8), size_t{5});
        }
        {   // the match of a lobby room, then its rematch: the flag is in what the match gives and in the seat that waits
            Room room(door_config(331, true));
            const size_t ga = join_door(room, page_config("Guest 151515"), true);
            const size_t pa = join_door(room, page_config("Ann"), false);
            using K = PlanKind;
            room.guests[ga].lobby->request_plan(plan_of({K::Open, K::Open, K::Nobody, K::Nobody}));
            room.run(100);
            room.guests[ga].lobby->request_start();
            room.run(100);
            const SeatKey kg = room.guests[ga].lobby->key();
            const SeatKey kp = room.guests[pa].lobby->key();
            const uint8_t sg = room.guests[ga].lobby->my_seat(), sp = room.guests[pa].lobby->my_seat();
            room.guests[ga].client_end->close();
            room.guests[pa].client_end->close();
            room.run(100);
            const size_t xg = join_keyed(room, "Guest 151515", kg);
            const size_t xp = join_keyed(room, "Ann", kp);
            room.run(200);
            room.host.take_events();
            room.host.start(5, 6, room.now);
            room.run(100);
            room.guests[xg].lobby->report_loaded(true);
            room.guests[xp].lobby->report_loaded(true);
            room.host.host_loaded(true);
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            const Rematch r = room.host.rematch();
            ASSERT_EQ(r.people.size(), size_t{2});
            for (const Rematch::Person& p : r.people) ASSERT_EQ(p.unverified, p.seat == sg);
            ASSERT_TRUE(sg != sp);
            Room again(door_config(332, true), false);                                       // the server opens the room again for the people of the match
            ASSERT_TRUE(again.host.reopen(r, again.now));
            ASSERT_TRUE(again.host.unverified(sg) && !again.host.unverified(sp));
            ClientLobby::Config cg = page_config("Guest 151515"), cp = page_config("Ann");
            cg.key = kg;
            cp.key = kp;
            const size_t back_g = join_with(again, cg);
            const size_t back_p = join_with(again, cp);
            again.run(300);
            ASSERT_EQ(burst_of(again, back_g, 8), size_t{3});
            ASSERT_EQ(burst_of(again, back_p, 8), size_t{5});
        }
        {   // a seat that comes back with its key (a held seat, a page that was reloaded) keeps the flag it was given at the door, whatever the Hello says now
            Room room(door_config(333, true));
            const size_t guest = join_door(room, "Guest 161616", true);
            const SeatKey key = room.guests[guest].lobby->key();
            const uint8_t seat = room.guests[guest].lobby->my_seat();
            room.guests[guest].client_end->close();
            room.run(300);
            const size_t back = join_keyed(room, "Ann", key);                                // (the door does not look at the name of a key that fits a seat: the seat keeps its own)
            room.run(300);
            ASSERT_TRUE(room.host.unverified(seat));
            ASSERT_EQ(name_of(room, back), std::string("Guest 161616"));
            ASSERT_EQ(burst_of(room, back, 8), size_t{3});
        }
    } TEST_END();
}

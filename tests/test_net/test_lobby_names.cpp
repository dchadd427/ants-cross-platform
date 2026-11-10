// Tests of the names of a room on a server that checks its names (HostLobby::Config unique_names and fixed_names, ants_net/person_name.hpp): no two people of a room go by the same name,
// and a seat keeps the name it was given. A room without them (a LAN or direct host) is what it was.
#include "lobby_fixtures.hpp"

#include "ants_net/person_name.hpp"

namespace {

// A server's lobby room with the checked names on
HostLobby::Config checked_room_config(uint32_t seed) {
    HostLobby::Config hc = lobby_room_config(seed);
    hc.unique_names = true;
    hc.fixed_names = true;
    return hc;
}

std::string name_of(const Room& room, size_t guest) { return room.host.room().slots[room.guests[guest].lobby->my_seat()].name; }

bool guest_form(const std::string& s) { return is_guest_name(s); }

}  // namespace

void run_lobby_names_tests() {
    TEST_CASE("N4.55 Names Of A Room (Unique Names): A Second Person With The Same Name, Case Ignored, Gets The Name With (2), The Third (3); The Cut Keeps The Whole At 32 Characters; A Held Seat Still Has Its Name; A Name Nobody Has Is Kept; Without The Setting Two People Can Share A Name") {
        {
            Room room(checked_room_config(200));
            const size_t a = room.join_seat("Ann");
            const size_t b = room.join_seat("Ann");
            const size_t c = room.join_seat("ANN");
            const size_t d = room.join_seat("Dee");
            ASSERT_EQ(name_of(room, a), std::string("Ann"));
            ASSERT_EQ(name_of(room, b), std::string("Ann (2)"));
            ASSERT_EQ(name_of(room, c), std::string("ANN (3)"));
            ASSERT_EQ(name_of(room, d), std::string("Dee"));
            ASSERT_EQ(room.host.renamed_for_uniqueness(), 2u);
            ASSERT_EQ(room.guests[a].lobby->room().slots[room.guests[c].lobby->my_seat()].name, std::string("ANN (3)"));       // (the Room message that everybody gets shows it)
        }
        {   // the base is cut so that the whole is 32 characters, and trimmed (a blank at the cut is not kept before the number)
            Room room(checked_room_config(201));
            const std::string long_name(32, 'x');
            const size_t a = room.join_seat(long_name);
            const size_t b = room.join_seat(long_name);
            ASSERT_EQ(name_of(room, a), long_name);
            ASSERT_EQ(name_of(room, b), std::string(28, 'x') + " (2)");
            ASSERT_EQ(name_of(room, b).size(), size_t{32});
            const std::string blank_at_cut = std::string(27, 'y') + " zzzz";
            ASSERT_EQ(blank_at_cut.size(), size_t{32});
            const size_t c = room.join_seat(blank_at_cut);
            const size_t d = room.join_seat(blank_at_cut);
            ASSERT_EQ(name_of(room, c), blank_at_cut);
            ASSERT_EQ(name_of(room, d), std::string(27, 'y') + " (2)");
        }
        {   // a seat that is held (its connection ended) still has its person: a newcomer with the name is the second
            Room room(checked_room_config(202));
            const size_t a = room.join_seat("Ann");
            room.join_seat("Bob");
            room.guests[a].client_end->close();
            room.run(300);
            const size_t c = room.join_seat("Ann");
            ASSERT_EQ(name_of(room, c), std::string("Ann (2)"));
        }
        {   // a name of no characters (a Hello that gave none) is a name too: the second one is "(2)", with no blank before the bracket
            Room room(checked_room_config(204));
            const size_t a = room.join_seat("");
            const size_t b = room.join_seat("");
            ASSERT_EQ(name_of(room, a), std::string());
            ASSERT_EQ(name_of(room, b), std::string("(2)"));
        }
        {   // without the setting a name is taken as it is: two Anns
            Room room(lobby_room_config(203));
            const size_t a = room.join_seat("Ann");
            const size_t b = room.join_seat("Ann");
            ASSERT_EQ(name_of(room, a), std::string("Ann"));
            ASSERT_EQ(name_of(room, b), std::string("Ann"));
            ASSERT_EQ(room.host.renamed_for_uniqueness(), 0u);
        }
    } TEST_END();

    TEST_CASE("N4.56 Names Of A Room (Guest Names): Two People With The Same Guest Name Get Different Ones, Each In The Guest Form; A Room That Cannot Draw Gives The Number Form; The Pure Rules: The Form, The Draw (Every Number, No Lean), The Case") {
        {
            Room room(checked_room_config(210));
            const size_t a = room.join_seat("Guest 123456");
            const size_t b = room.join_seat("Guest 123456");
            const size_t c = room.join_seat("guest 123456");                  // (not of the form: a name like any other, but the first with the case ignored)
            ASSERT_EQ(name_of(room, a), std::string("Guest 123456"));
            ASSERT_TRUE(guest_form(name_of(room, b)) && name_of(room, b) != "Guest 123456");
            ASSERT_EQ(name_of(room, c), std::string("guest 123456 (2)"));
        }
        {
            HostLobby::Config hc = checked_room_config(211);
            hc.make_key = nullptr;                                            // (no generator: the other way)
            Room room(hc);
            room.join_seat("Guest 654321");
            const size_t b = room.join_seat("Guest 654321");
            ASSERT_EQ(name_of(room, b), std::string("Guest 654321 (2)"));
        }
        ASSERT_TRUE(is_guest_name("Guest 123456") && is_guest_name("Guest 999999") && is_guest_name("Guest 100000"));
        for (const char* bad : {"guest 123456", "Guest 012345", "Guest 1234567", "Guest 12345", "Guest  23456", "Guest 12345a", "Guest123456", "Guest 123456 ", " Guest 123456", "", "Guest 1234\xd9" "6"}) {
            ASSERT_MSG(!is_guest_name(bad), bad);
        }
        {   // the draw: the first four bytes as a little endian word; a word below 4,294,800,000 (4,772 times 900,000) gives its number 100000 .. 999999 (every number the same count of words), the rest at the top is thrown away
            std::string out;
            const auto word = [](uint32_t w) { return std::array<uint8_t, 4>{static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 24)}; };
            ASSERT_TRUE(guest_name_from_bytes(word(0).data(), out) && out == "Guest 100000");
            ASSERT_TRUE(guest_name_from_bytes(word(899999u).data(), out) && out == "Guest 999999");
            ASSERT_TRUE(guest_name_from_bytes(word(900000u).data(), out) && out == "Guest 100000");
            ASSERT_TRUE(guest_name_from_bytes(word(4294800000u - 1u).data(), out) && out == "Guest 999999");     // (the last word that is kept ends a whole run of 900,000)
            ASSERT_FALSE(guest_name_from_bytes(word(4294800000u).data(), out));
            ASSERT_FALSE(guest_name_from_bytes(word(0xFFFFFFFFu).data(), out));
            ASSERT_TRUE(guest_name_from_bytes(word(0x01020304u).data(), out) && out == "Guest " + std::to_string(100000u + 0x01020304u % 900000u));       // (little endian)
        }
        ASSERT_TRUE(same_name("Ann", "aNN") && !same_name("Ann", "Anna") && !same_name("Ann", "Anm") && same_name("", ""));
    } TEST_END();

    TEST_CASE("N4.57 Names Of A Room (Fixed Names): A Name Message Is Read As Ever But Not Acted On: The Name Stays, It Counts As Ignored, The First 16 Are Free And Each After Is An Offence, A Garbled One Is An Offence And Not Counted; Without The Setting A Person Still Renames") {
        {
            Room room(checked_room_config(220));
            const size_t a = room.join_seat("Ann");
            const size_t b = room.join_seat("Bob");
            const uint8_t b_seat = room.guests[b].lobby->my_seat();
            ASSERT_TRUE(room.guests[b].lobby->request_name("Robert"));
            room.run(100);
            ASSERT_EQ(room.host.room().slots[b_seat].name, std::string("Bob"));
            ASSERT_EQ(room.host.renames(), 0u);
            ASSERT_EQ(room.host.ignored_names(), 1u);
            const std::vector<uint8_t> wish = encode(NameMsg{"Robert"});
            for (uint32_t i = 1; i < kIgnoredNamesAllowed; ++i) room.guests[b].client_end->send(wish);
            room.run(300);
            ASSERT_EQ(room.host.ignored_names(), kIgnoredNamesAllowed);
            ASSERT_TRUE(room.host.occupied(b_seat));
            for (int i = 0; i < 7; ++i) room.guests[b].client_end->send(wish);       // the 17th to the 23rd are offences, seven are not enough
            room.run(300);
            ASSERT_TRUE(room.host.occupied(b_seat));
            room.guests[b].client_end->send(wish);                                   // the eighth offence
            room.run(300);
            ASSERT_FALSE(room.host.occupied(b_seat));
            ASSERT_EQ(room.host.renames(), 0u);
            ASSERT_TRUE(room.host.occupied(room.guests[a].lobby->my_seat()));        // (nobody else was touched)
        }
        {   // a garbled one (a name of no characters) is an offence from the first, and is no ignored name
            Room room(checked_room_config(221));
            room.join_seat("Ann");
            const size_t b = room.join_seat("Bob");
            const uint8_t b_seat = room.guests[b].lobby->my_seat();
            const std::vector<uint8_t> garbled = {static_cast<uint8_t>(MsgType::Name), 0};
            for (int i = 0; i < 7; ++i) room.guests[b].client_end->send(garbled);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(b_seat));
            ASSERT_EQ(room.host.ignored_names(), 0u);
            room.guests[b].client_end->send(garbled);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(b_seat));
        }
        {   // fixed names alone, unique names alone: each does its own thing and not the other's
            HostLobby::Config only_fixed = lobby_room_config(222);
            only_fixed.fixed_names = true;
            Room room(only_fixed);
            room.join_seat("Ann");
            const size_t b = room.join_seat("Ann");
            ASSERT_EQ(name_of(room, b), std::string("Ann"));
            ASSERT_TRUE(room.guests[b].lobby->request_name("Zed"));
            room.run(100);
            ASSERT_EQ(name_of(room, b), std::string("Ann"));
            HostLobby::Config only_unique = lobby_room_config(223);
            only_unique.unique_names = true;
            Room second(only_unique);
            second.join_seat("Ann");
            const size_t c = second.join_seat("Ann");
            ASSERT_EQ(name_of(second, c), std::string("Ann (2)"));
            ASSERT_TRUE(second.guests[c].lobby->request_name("Zed"));
            second.run(100);
            ASSERT_EQ(name_of(second, c), std::string("Zed"));                       // (still renames: only unique names was set)
        }
        {   // without either setting: as it was
            Room room(lobby_room_config(224));
            room.join_seat("Ann");
            const size_t b = room.join_seat("Bob");
            ASSERT_TRUE(room.guests[b].lobby->request_name("Robert"));
            room.run(100);
            ASSERT_EQ(name_of(room, b), std::string("Robert"));
            ASSERT_EQ(room.host.renames(), 1u);
            ASSERT_EQ(room.host.ignored_names(), 0u);
        }
    } TEST_END();
}

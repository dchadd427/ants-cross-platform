// Tests of the names of people in a room (network protocol 29, docs/NETWORK_PORT.md "Protocol 29"): what a name may hold on the wire (printable ASCII and the letters of the table, as UTF-8), the guest's form
// "Name (Guest)", the hub's pass that a server with the hub's key asks for, a rename, and the room's own notices that name people (N4.70 - N4.75)
#include "lobby_fixtures.hpp"

#include "ants_net/name_rules.hpp"

namespace {

const std::string kZoe = "[\xC3\x84NT] Zo\xC3\xAB";                                // "[ÄNT] Zoë"
const std::string kYozhik = "[\xD0\xA0\xD0\xA3\xD0\xA1] \xD0\x81\xD0\xB6\xD0\xB8\xD0\xBA";    // "[РУС] Ёжик"
const std::string kGoodPass = "1700000000.0123456789abcdef0123456789abcdef";            // (the shape of a pass: the test's gate decides what is good)
const std::string kOtherPass = "1700000000.fedcba9876543210fedcba9876543210";

// A room whose server has the hub's key: it vouches for kZoe with kGoodPass and for kYozhik with kOtherPass, and for nothing else
HostLobby::Config gated_config(uint32_t seed) {
    HostLobby::Config hc = lobby_room_config(seed);
    hc.name_gate = [](const std::string& name, const std::string& pass) { return (name == kZoe && pass == kGoodPass) || (name == kYozhik && pass == kOtherPass); };
    return hc;
}

ClientLobby::Config page_with(const std::string& name, const std::string& pass = std::string()) {
    ClientLobby::Config cc = page_config(name);
    cc.name_pass = pass;
    return cc;
}

std::vector<std::string> names_of(const RoomMsg& room) {
    std::vector<std::string> out;
    for (const auto& slot : room.slots) {
        if (slot.state != SlotState::Empty) out.push_back(slot.name);
    }
    return out;
}

}  // namespace

void run_lobby_names_tests() {
    TEST_CASE("N4.70 Names On The Wire (Protocol 29): UTF-8 Of Printable ASCII And The Letters Of The Table, 32 Characters And 96 Bytes; Anything Else Is No Name In A Hello, A Room, A Start Or A NameMsg") {
        const std::string vietnamese32 = [] { std::string s; for (int i = 0; i < 32; ++i) s += "\xE1\xBA\xA0"; return s; }();         // 32 letters of three bytes: 96 bytes
        ASSERT_TRUE(valid_wire_name("Zo\xC3\xAB") && valid_wire_name(kZoe) && valid_wire_name(kYozhik) && valid_wire_name(vietnamese32) && valid_wire_name("") && valid_wire_name("Ann Lee ~!{}"));
        ASSERT_TRUE(utf8_length(vietnamese32) == 32 && vietnamese32.size() == 96);
        ASSERT_FALSE(valid_wire_name(vietnamese32 + "a"));                                    // 33 characters
        ASSERT_FALSE(valid_wire_name(std::string(33, 'x')));
        const std::vector<std::string> refused = {
            "\xCE\x91\xCE\xBB\xCE\xAD\xCE\xBE",                                               // Greek: the game's font has no such letters
            "\xE9\xBE\x8D", "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", "\xF0\x9F\x98\x80",           // CJK, Hebrew, an emoji
            "Zo\xC3", "\xC3\xAB\xC3", "\x80", "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80",   // a cut letter, a stray byte, an overlong form, a surrogate, past U+10FFFF
            "Zoe\n", std::string("a\0b", 3), "a\x7F", "a\xC2\xA0" "b",                       // controls, DEL and a no-break space
            "e\xCC\x81",                                                                       // a combining mark: names are NFC
            "\xC3\x97", "\xC3\xB7",                                                            // the signs U+00D7 and U+00F7 are no letters
        };
        for (const std::string& name : refused) {
            ASSERT_FALSE(valid_wire_name(name));
            HelloMsg hello;
            hello.name = name;
            std::vector<uint8_t> bytes = encode(hello);                                        // (the encoder keeps what a name may hold ...)
            HelloMsg back;
            ASSERT_TRUE(decode(bytes.data(), bytes.size(), back) && valid_wire_name(back.name));
            std::vector<uint8_t> raw = {static_cast<uint8_t>(MsgType::Hello)};               // ... and the decoder refuses the bytes that are none
            for (const uint8_t b : {static_cast<uint8_t>(kProtocolVersion & 0xFF), static_cast<uint8_t>(kProtocolVersion >> 8)}) raw.push_back(b);
            raw.push_back(static_cast<uint8_t>(name.size()));
            for (const char c : name) raw.push_back(static_cast<uint8_t>(c));
            for (const uint8_t b : std::vector<uint8_t>{0, 0, 255, 0, 0}) raw.push_back(b);   // port, want_seat, room, token
            for (int i = 0; i < 16; ++i) raw.push_back(0);                                     // key
            for (const uint8_t b : std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0}) raw.push_back(b);   // have_turns, platform, client_kind, no pass
            HelloMsg refused_hello;
            ASSERT_FALSE(decode(raw.data(), raw.size(), refused_hello));
        }
        // the decoder of UTF-8 itself is strict (a name is one of its users): the forms that nothing may use are refused, and so is a letter that is cut or whose follower is no follower
        std::vector<uint32_t> points;
        for (const std::string& bad : std::vector<std::string>{"\xC3\x28", "\xE1\xBA\x28", "\xF0\x9F\x98", "\xC0\x80", "\xC1\xBF", "\xE0\x9F\x80", "\xF0\x8F\xBF\xBF", "\xED\xA0\x80", "\xED\xBF\xBF",
                                                                  "\xF4\x90\x80\x80", "\xF8\x88\x80\x80\x80", "\xFF", "Zo\xC3"}) {
            ASSERT_FALSE(decode_utf8(bad, points));
            ASSERT_TRUE(points.empty());
            ASSERT_TRUE(utf8_length(bad) == 0);
        }
        ASSERT_TRUE(decode_utf8("a\xC3\xAB\xE1\xBA\xA0\xF0\x9F\x98\x80", points) && points == (std::vector<uint32_t>{'a', 0xEB, 0x1EA0, 0x1F600}));
        ASSERT_TRUE(decode_utf8("\xED\x9F\xBF", points) && points == (std::vector<uint32_t>{0xD7FF}));        // (the last code point before the surrogates, the first after them, the last of all)
        ASSERT_TRUE(decode_utf8("\xEE\x80\x80", points) && points == (std::vector<uint32_t>{0xE000}));
        ASSERT_TRUE(decode_utf8("\xF4\x8F\xBF\xBF", points) && points == (std::vector<uint32_t>{0x10FFFF}));
        // a name is cut to its room in whole letters and no more bytes than it may have; a letter that is not drawn, a control and a cut letter are left out
        ASSERT_EQ(clip_wire_name("\xE1\xBA\xA0\xE1\xBA\xA0\xE1\xBA\xA0", 32, 7), std::string("\xE1\xBA\xA0\xE1\xBA\xA0"));
        ASSERT_EQ(clip_wire_name("\xE1\xBA\xA0\xE1\xBA\xA0\xE1\xBA\xA0", 2, 96), std::string("\xE1\xBA\xA0\xE1\xBA\xA0"));
        ASSERT_EQ(clip_wire_name("a\xCE\x91" "b\x01" "c\xC3", 32, 96), std::string("abc"));
        // the bytes are counted as well as the letters (a notice has 100 letters and 240 bytes, a name 32 and 96)
        ASSERT_FALSE(valid_wire_name("\xE1\xBA\xA0\xE1\xBA\xA0\xE1\xBA\xA0", 32, 8));
        ASSERT_TRUE(valid_wire_name("\xE1\xBA\xA0\xE1\xBA\xA0", 32, 6));
        // the letters of a name are the table's: Latin (Latin-1, Latin Extended-A, Romanian, Vietnamese) and Cyrillic, nothing in between
        ASSERT_TRUE(name_alphabet(0x41) == 0 && name_alphabet(0xFF) == 0 && name_alphabet(0x17E) == 0 && name_alphabet(0x1EA0) == 0 && name_alphabet(0x1EF9) == 0 && name_alphabet(0x400) == 1 && name_alphabet(0x45F) == 1 && name_alphabet(0x491) == 1);
        ASSERT_TRUE(name_alphabet(0x17F) < 0 && name_alphabet(0x3B1) < 0 && name_alphabet(0x460) < 0 && name_alphabet(0x1EFA) < 0 && name_alphabet(0xD7) < 0 && name_alphabet('0') < 0 && name_alphabet(0x4E00) < 0);
    } TEST_END();

    TEST_CASE("N4.71 Names On The Wire: The Room Message, The Start Message And The NameMsg Carry UTF-8 Names And Refuse The Rest; The NameMsg Carries The Pass Last") {
        RoomMsg room;
        room.slots[0].state = SlotState::Host;
        room.slots[0].name = kZoe;
        room.slots[2].state = SlotState::Client;
        room.slots[2].name = kYozhik;
        room.you = 0;
        std::vector<uint8_t> bytes = encode(room);
        RoomMsg back;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), back) && back.slots[0].name == kZoe && back.slots[2].name == kYozhik);
        room.slots[2].name = "\xCE\x91";                                                       // the encoder drops what the wire does not take: "" (and a seat that holds somebody has a name or none)
        bytes = encode(room);
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), back) && back.slots[2].name.empty());
        NameMsg m;
        m.name = kZoe;
        m.pass = kGoodPass;
        bytes = encode(m);
        ASSERT_TRUE(bytes[0] == 33 && bytes[1] == kZoe.size() && bytes.size() == 2 + kZoe.size() + 1 + kGoodPass.size());
        NameMsg got;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), got) && got.name == kZoe && got.pass == kGoodPass);
        m.pass.clear();                                                                         // no pass: a length byte of 0
        bytes = encode(m);
        ASSERT_TRUE(bytes.back() == 0 && decode(bytes.data(), bytes.size(), got) && got.pass.empty());
        m.pass = "not-a-pass";                                                                  // a text that is no pass is not sent
        bytes = encode(m);
        ASSERT_TRUE(bytes.back() == 0);
        std::vector<uint8_t> bad = encode(NameMsg{kZoe, kGoodPass});
        bad.pop_back();                                                                         // a pass that is cut
        ASSERT_FALSE(decode(bad.data(), bad.size(), got));
        bad = encode(NameMsg{kZoe, kGoodPass});
        bad[bad.size() - 3] = 'Z';                                                              // a pass that is no pass (not hex)
        ASSERT_FALSE(decode(bad.data(), bad.size(), got));
        bad = encode(NameMsg{"Zoe", std::string()});
        bad.pop_back();                                                                         // the old layout (no pass byte) is no NameMsg
        ASSERT_FALSE(decode(bad.data(), bad.size(), got));
        HelloMsg hello;
        hello.name = kYozhik;
        hello.name_pass = kOtherPass;
        bytes = encode(hello);
        HelloMsg hello_back;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), hello_back) && hello_back.name == kYozhik && hello_back.name_pass == kOtherPass && !hello_back.create);
        hello.create = CreateBlock{};
        hello.create->seats = 4;
        hello.create->map_name = "TINY.LVL";
        bytes = encode(hello);
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), hello_back) && hello_back.name_pass == kOtherPass && hello_back.create && hello_back.create->map_name == "TINY.LVL");      // (the pass comes before the create block)
        HelloMsg prefix;
        ASSERT_TRUE(decode_hello_prefix(bytes.data(), bytes.size(), prefix) && prefix.name == kYozhik);
    } TEST_END();

    TEST_CASE("N4.72 A Guest's Name: The Letters, Digits, Spaces And . _ - ' Of The Name Cut To 16 Characters, Then \" (Guest)\"; The Number Of A Visitor Stays As It Is; Nothing Left Is The Fallback") {
        const std::string fallback = "Player 2";
        ASSERT_EQ(guest_name("Wren", fallback), "Wren (Guest)");
        ASSERT_EQ(guest_name("Wren (Guest)", fallback), "Wren (Guest)");                       // (the suffix is taken off once and put on again)
        ASSERT_EQ(guest_name("Wren (Guest) (Guest)", fallback), "Wren Guest (Guest)");         // (the suffix is taken off once; the parentheses of the rest are no part of a name)
        ASSERT_EQ(guest_name("Guest 482913", fallback), "Guest 482913");
        ASSERT_EQ(guest_name("Guest 012345", fallback), "Guest 012345 (Guest)");              // (a number does not start with 0)
        ASSERT_EQ(guest_name("[ANT] Zed", fallback), "ANT Zed (Guest)");                       // (a bracket is no sign of a name: what is left stays)
        ASSERT_EQ(guest_name("  a   b  ", fallback), "a b (Guest)");
        ASSERT_EQ(guest_name("Zo\xC3\xAB", fallback), "Zo\xC3\xAB (Guest)");
        ASSERT_EQ(guest_name("[][]()", fallback), fallback + " (Guest)");
        ASSERT_EQ(guest_name("", fallback), fallback + " (Guest)");
        ASSERT_EQ(guest_name("Wolfgang Amadeus Mozart The Great", fallback), "Wolfgang Amadeus (Guest)");           // 16 characters
        ASSERT_EQ(guest_name("Wolfgang Amadeu s", fallback), "Wolfgang Amadeu (Guest)");        // (cut at 16, the end trimmed)
        ASSERT_EQ(guest_name("a.b_c-d'e", fallback), "a.b_c-d'e (Guest)");
        ASSERT_TRUE(is_guest_number("Guest 482913") && !is_guest_number("Guest 48291") && !is_guest_number("guest 482913") && !is_guest_number("Guest 4829130") && !is_guest_number("Guest 48291x"));
        ASSERT_TRUE(valid_pass_shape(kGoodPass) && !valid_pass_shape("") && !valid_pass_shape("1700000000") && !valid_pass_shape(".0123456789abcdef0123456789abcdef") && !valid_pass_shape("17x.0123456789abcdef0123456789abcdef"));
        ASSERT_FALSE(valid_pass_shape("1700000000.0123456789ABCDEF0123456789abcdef"));         // (lower-case hex only)
        ASSERT_FALSE(valid_pass_shape("1700000000.0123456789abcdef0123456789abcde"));          // (31 digits)
        ASSERT_FALSE(valid_pass_shape("1700000000000.0123456789abcdef0123456789abcdef"));      // (a time of more than 12 digits)
    } TEST_END();

    TEST_CASE("N4.73 A Server With The Hub's Key: A Name Is Taken As It Is Only With A Good Pass For Exactly That Name; Every Other Name Is A Guest's (A Hello And A Rename)") {
        Room room(gated_config(701));
        const size_t zoe = join_with(room, page_with(kZoe, kGoodPass));
        room.run(100);
        const size_t wren = join_with(room, page_with("Wren"));
        room.run(100);
        const size_t fake = join_with(room, page_with(kZoe, kOtherPass));                       // a pass for another name: the name is not vouched for
        room.run(100);
        const size_t number = join_with(room, page_with("Guest 482913"));
        room.run(100);
        ASSERT_EQ(names_of(room.host.room()), (std::vector<std::string>{kZoe, "Wren (Guest)", "\xC3\x84NT Zo\xC3\xAB (Guest)", "Guest 482913"}));
        ASSERT_TRUE(room.guests[zoe].lobby->room().slots[room.guests[zoe].lobby->my_seat()].name == kZoe && room.guests[wren].lobby->room().slots[room.guests[wren].lobby->my_seat()].name == "Wren (Guest)");
        ASSERT_TRUE(room.guests[fake].lobby->my_seat() < sim::MAX_PLAYERS && room.guests[number].lobby->my_seat() < sim::MAX_PLAYERS);
        // a rename: with the pass of the new name it is taken; without, or with another's pass, it is a guest's; a name with nothing left keeps what the person has
        room.guests[wren].lobby->request_name(kYozhik, kOtherPass);
        room.run(100);
        ASSERT_EQ(room.host.room().slots[room.guests[wren].lobby->my_seat()].name, kYozhik);
        room.run(1500);
        room.guests[wren].lobby->request_name(kZoe, kOtherPass);
        room.run(100);
        ASSERT_EQ(room.host.room().slots[room.guests[wren].lobby->my_seat()].name, "\xC3\x84NT Zo\xC3\xAB (Guest)");
        room.run(1500);
        room.guests[wren].lobby->request_name("Wren");
        room.run(100);
        ASSERT_EQ(room.host.room().slots[room.guests[wren].lobby->my_seat()].name, "Wren (Guest)");
        room.run(1500);
        room.guests[wren].lobby->request_name("[]");
        room.run(100);
        ASSERT_EQ(room.host.room().slots[room.guests[wren].lobby->my_seat()].name, "Wren (Guest)");       // (nothing is left of "[]": the person keeps their name)
    } TEST_END();

    TEST_CASE("N4.74 A Room Without The Hub's Key (A LAN Or Direct Host, A Server That Has None): Every Name That The Wire Takes Is Taken As It Is; A Computer Player's Name Never Is") {
        Room room(lobby_room_config(702));
        const size_t a = join_with(room, page_with(kZoe));
        room.run(100);
        const size_t b = join_with(room, page_with("[BOT] Zo\xC3\xAB"));
        room.run(100);
        const size_t c = join_with(room, page_with("Pip (bot, Hard)"));
        room.run(100);
        ASSERT_TRUE(room.guests[a].lobby->my_seat() < 4 && room.guests[b].lobby->my_seat() < 4 && room.guests[c].lobby->my_seat() < 4);
        ASSERT_EQ(room.host.room().slots[room.guests[a].lobby->my_seat()].name, kZoe);
        ASSERT_EQ(room.host.room().slots[room.guests[b].lobby->my_seat()].name, "Player " + std::to_string(static_cast<unsigned>(room.guests[b].lobby->my_seat()) + 1u));       // (a name that reads as a label: the room's own)
        ASSERT_EQ(room.host.room().slots[room.guests[c].lobby->my_seat()].name, "Player " + std::to_string(static_cast<unsigned>(room.guests[c].lobby->my_seat()) + 1u));
    } TEST_END();

    TEST_CASE("N4.76 A Name Of Nothing Is Nobody's, Whatever The Hub's Key Says; A Name That Leaves Nothing Is The Seat's Own, With The Suffix") {
        HostLobby::Config every = lobby_room_config(703);
        every.name_gate = [](const std::string&, const std::string&) { return true; };        // (a gate that vouches for everything: an empty name is still not a name)
        Room room(every);
        const size_t nobody = join_with(room, page_with(""));
        room.run(100);
        const size_t brackets = join_with(room, page_with("[]"));
        room.run(100);
        const unsigned seat_a = room.guests[nobody].lobby->my_seat(), seat_b = room.guests[brackets].lobby->my_seat();
        ASSERT_EQ(room.host.room().slots[seat_a].name, "Player " + std::to_string(seat_a + 1u) + " (Guest)");
        ASSERT_EQ(room.host.room().slots[seat_b].name, "[]");                                  // (what the gate vouches for is kept as it is)
        Room gated(gated_config(704));
        const size_t only_brackets = join_with(gated, page_with("[]"));
        gated.run(100);
        ASSERT_EQ(gated.host.room().slots[gated.guests[only_brackets].lobby->my_seat()].name, "Player " + std::to_string(gated.guests[only_brackets].lobby->my_seat() + 1u) + " (Guest)");
    } TEST_END();

    TEST_CASE("N4.75 The Room's Own Notices May Name People In Their Letters; A Person's Chat Line Is Printable ASCII Still") {
        ChatMsg notice;
        notice.sender = kRoomSender;
        notice.text = kZoe + "'s game did not come in time.";
        std::vector<uint8_t> bytes = encode(notice);
        ChatMsg back;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), back) && back.text == notice.text);
        ChatMsg line;
        line.sender = 1;
        line.text = "hello " + kZoe;
        bytes = encode(line);                                                                   // (the encoder keeps printable ASCII of a person's line)
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), back) && back.text == "hello [NT] Zo");
        std::vector<uint8_t> raw = {static_cast<uint8_t>(MsgType::Chat), 1, 0, 2, 0xC3, 0xAB};      // a person's line with a letter on the wire is no line
        ASSERT_FALSE(decode(raw.data(), raw.size(), back));
        raw = {static_cast<uint8_t>(MsgType::Chat), kRoomSender, 0, 2, 0xC3, 0xAB};
        ASSERT_TRUE(decode(raw.data(), raw.size(), back) && back.text == "\xC3\xAB");
        raw = {static_cast<uint8_t>(MsgType::Chat), kRoomSender, 0, 2, 0xCE, 0x91};              // Greek letters are no letters of the table, in a notice too
        ASSERT_FALSE(decode(raw.data(), raw.size(), back));
        for (const size_t letters : {size_t{80}, size_t{81}}) {                                  // a notice has 240 bytes at the most: 80 letters of three bytes, not 81
            raw = {static_cast<uint8_t>(MsgType::Chat), kRoomSender, 0, static_cast<uint8_t>(letters * 3)};
            for (size_t i = 0; i < letters; ++i) {
                for (const uint8_t b : {uint8_t{0xE1}, uint8_t{0xBA}, uint8_t{0xA0}}) raw.push_back(b);
            }
            ASSERT_EQ(decode(raw.data(), raw.size(), back), letters == 80);
        }
    } TEST_END();
}

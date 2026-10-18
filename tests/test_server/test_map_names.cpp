// Tests of the small pieces under the map submissions (docs/MAP_SUBMISSIONS.md): the rules for the text that a player sends (strict UTF-8, invisible characters, spaces, controls, the characters allowed, NFC without
// tables, a name that is a safe file name), SHA-256 and HMAC-SHA-256 against the published vectors, base64url, the address that a request counts under, and the query of a call. Pure functions: no files, no network.
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>

#include "../common/test_assert.hpp"
#include "ants_ctl/http.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/submissions_http.hpp"

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

using namespace ants::server;

namespace {

std::string hex_of_sha(const std::string& text) { return hex_encode(sha256(text)); }

std::string hex_to_bytes(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

ants::ctl::HttpRequest request_with(const std::map<std::string, std::string>& headers, const std::string& peer = "") {
    ants::ctl::HttpRequest r;
    r.method = "POST";
    r.path = "/submissions";
    r.headers.insert(headers.begin(), headers.end());
    r.peer = peer;
    return r;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n MAP SUBMISSIONS: NAMES, TEXT, SHA-256, THE ADDRESS\n=======================================================\n";

    TEST_CASE("NM1.1 SHA-256 gives the published digests, also at every length around a block (55, 56, 57, 63, 64, 65, 119, 120, 128, 1000)") {
        ASSERT_EQ(hex_of_sha(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        ASSERT_EQ(hex_of_sha("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        ASSERT_EQ(hex_of_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
        const std::map<size_t, std::string> expected = {
            {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},  {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
            {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},  {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
            {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},  {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
            {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"}, {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
            {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"}, {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"}};
        for (const auto& e : expected) ASSERT_TRUE(hex_of_sha(std::string(e.first, 'a')) == e.second);
        ASSERT_EQ(sha256("abc").size(), size_t{32});
    } TEST_END();

    TEST_CASE("NM1.2 HMAC-SHA-256 gives the vectors of RFC 4231 (a short key, a key of the block's size and a longer one), and a different key or text gives a different value") {
        ASSERT_EQ(hex_encode(hmac_sha256(std::string(20, '\x0b'), "Hi There")), std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
        ASSERT_EQ(hex_encode(hmac_sha256("Jefe", "what do ya want for nothing?")), std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
        ASSERT_EQ(hex_encode(hmac_sha256(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")), std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
        ASSERT_EQ(hex_encode(hmac_sha256(std::string(131, '\xaa'), "This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.")),
                  std::string("9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"));
        ASSERT_NE(hmac_sha256("k1", "text"), hmac_sha256("k2", "text"));
        ASSERT_NE(hmac_sha256("k1", "text"), hmac_sha256("k1", "texu"));
    } TEST_END();

    TEST_CASE("NM1.3 base64url and hex: the RFC 4648 vectors without padding, the whole byte range round trips, and anything else is refused") {
        const std::map<std::string, std::string> vectors = {{"", ""}, {"f", "Zg"}, {"fo", "Zm8"}, {"foo", "Zm9v"}, {"foob", "Zm9vYg"}, {"fooba", "Zm9vYmE"}, {"foobar", "Zm9vYmFy"}};
        for (const auto& v : vectors) {
            ASSERT_EQ(base64url_encode(v.first), v.second);
            std::string back;
            ASSERT_TRUE(base64url_decode(v.second, back) && back == v.first);
        }
        std::string all;
        for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
        for (size_t n : {size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{31}, size_t{32}, size_t{255}, size_t{256}}) {
            std::string back;
            ASSERT_TRUE(base64url_decode(base64url_encode(all.substr(0, n)), back) && back == all.substr(0, n));
        }
        ASSERT_EQ(base64url_encode(std::string("\xfb\xff\xfe", 3)), std::string("-__-"));            // (the url alphabet: - and _ for + and /)
        std::string out;
        for (const char* bad : {"Zg==", "Zm9v+", "Zm9v/", "Z", "ZmFyY", "Zm 9v", "Zm9v\n", "Z\xff"}) ASSERT_FALSE(base64url_decode(bad, out));
        ASSERT_EQ(hex_encode(std::string("\x00\x01\xab\xff", 4)), std::string("0001abff"));
        ASSERT_EQ(hex_to_bytes("0001abff"), std::string("\x00\x01\xab\xff", 4));
    } TEST_END();

    TEST_CASE("NM2.1 UTF-8 is read strictly: overlong forms, surrogates, a value above U+10FFFF, a cut sequence and a lone continuation byte are all refused") {
        std::vector<char32_t> v;
        ASSERT_TRUE(names::decode_utf8("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", v) && v.size() == 4 && v[1] == 0xE9 && v[2] == 0x20AC && v[3] == 0x1F600);
        for (const std::string& bad : {std::string("\xC0\x80"), std::string("\xC1\xBF"), std::string("\xE0\x80\x80"), std::string("\xE0\x9F\xBF"), std::string("\xF0\x80\x80\x80"), std::string("\xED\xA0\x80"),
                                       std::string("\xED\xBF\xBF"), std::string("\xF4\x90\x80\x80"), std::string("\xF5\x80\x80\x80"), std::string("\xC3"), std::string("\xE2\x82"), std::string("\xF0\x9F\x98"),
                                       std::string("\x80"), std::string("\xBF"), std::string("A\xFF"), std::string("\xC3\x28"), std::string("\xE2\x28\xA1")}) {
            ASSERT_TRUE(!names::decode_utf8(bad, v));
        }
        std::string out;
        names::append_utf8(out, 0x20AC);
        names::append_utf8(out, 0x1F600);
        ASSERT_EQ(out, std::string("\xE2\x82\xAC\xF0\x9F\x98\x80"));
    } TEST_END();

    TEST_CASE("NM2.2 A map's name: what is kept, what is folded (spaces, invisible characters), what is refused and why") {
        const auto clean = [](const std::string& raw) { return names::clean_map_name(raw); };
        ASSERT_TRUE(clean("River Run").ok && clean("River Run").text == "River Run");
        ASSERT_TRUE(clean("  Big   Lake  ").ok && clean("  Big   Lake  ").text == "Big Lake");
        ASSERT_TRUE(clean("It's a map!").ok && clean("Tom & Jerry (2)").ok && clean("a-b_c.d,e").ok);
        ASSERT_EQ(clean("Big\xC2\xA0Lake").text, std::string("Big Lake"));                      // (a no-break space is a space)
        ASSERT_EQ(clean("Big\xE2\x80\x8B" "Lake").text, std::string("BigLake"));                  // (a zero-width space is gone, not a space)
        ASSERT_EQ(clean("Bi\xC2\xADg Lake").text, std::string("Big Lake"));                        // (a soft hyphen too)
        ASSERT_EQ(clean("\xEF\xBB\xBF" "Lake Map").text, std::string("Lake Map"));                // (a byte order mark)
        ASSERT_EQ(clean("\xE2\x80\xAE" "Lake Map").text, std::string("Lake Map"));                // (a right-to-left override: it cannot reverse what a page shows)
        ASSERT_EQ(clean("Lake\xEF\xB8\x8F Map").text, std::string("Lake Map"));                   // (a variation selector)
        ASSERT_EQ(clean("\xF3\xA0\x80\x81" "Lake Map").text, std::string("Lake Map"));            // (a tag character)
        ASSERT_EQ(clean("").reason, std::string("empty"));
        ASSERT_EQ(clean("   ").reason, std::string("empty"));
        ASSERT_EQ(clean("\xE2\x80\x8B\xE2\x80\x8B").reason, std::string("empty"));                // (nothing but invisible characters)
        ASSERT_EQ(clean("ab").reason, std::string("too_short"));
        ASSERT_EQ(clean(std::string(60, 'a')).ok, true);
        ASSERT_EQ(clean(std::string(61, 'a')).reason, std::string("too_long"));
        ASSERT_EQ(clean(std::string(100000, 'a')).reason, std::string("too_long"));
        ASSERT_EQ(clean("a\tb c").reason, std::string("control"));
        ASSERT_EQ(clean("a\nb c").reason, std::string("control"));
        ASSERT_EQ(clean(std::string("ab\0cd", 5)).reason, std::string("control"));
        ASSERT_EQ(clean("ab\x7F" "cd").reason, std::string("control"));
        ASSERT_EQ(clean("ab\xC2\x85" "cd").text, std::string("ab cd"));                          // (U+0085 is a line end of the old terminals: it is white space here)
        ASSERT_EQ(clean("ab\xC2\x9F" "cd").reason, std::string("control"));
        ASSERT_EQ(clean("Caf\xC3\xA9").reason, std::string("characters"));
        ASSERT_EQ(clean("\xEF\xBC\xA1\xEF\xBC\xA2\xEF\xBC\xA3").reason, std::string("characters"));      // (full-width letters)
        ASSERT_EQ(clean("\xD0\xB0pple").reason, std::string("characters"));                                // (a Cyrillic a in an English word)
        ASSERT_EQ(clean("a\xCC\x81" "bc").reason, std::string("characters"));
        ASSERT_EQ(clean("\xC3\x28").reason, std::string("not_utf8"));
        for (const char* bad : {"a/b", "a\\b", "..", "../x", "a/../b", "x..y", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", ".hidden", "name.", "-- --", "!! !", "<script>", "a%2Fb", "a;b", "a=b", "a#b", "a$b", "a~b", "a+b", "a@b", "a[b]", "a{b}", "a^b", "a`b"}) {
            ASSERT_TRUE(!clean(bad).ok);
        }
        for (const char* reserved : {"CON", "con", "Prn", "AUX", "nul", "COM1", "com9", "LPT1", "lpt9", "CON.txt", "nul.x", "con ", "Clock$"}) {
            ASSERT_TRUE(!clean(reserved).ok);
        }
        ASSERT_TRUE(clean("COM0").ok && clean("Console").ok && clean("Communal").ok);               // (only the devices' own names)
    } TEST_END();

    TEST_CASE("NM2.3 Every name that is kept is a safe file name in the game's protocol, with nothing a path or a shell could use") {
        // a sweep of all the characters up to U+00FF in front of and inside a name: whatever is kept is valid_map_name once ".lvl" follows
        for (int c = 0; c < 256; ++c) {
            std::string raw = "Ab";
            names::append_utf8(raw, static_cast<char32_t>(c));
            raw += "cd";
            const names::Cleaned n = names::clean_map_name(raw);
            if (!n.ok) continue;
            ASSERT_TRUE(n.text.size() >= 3 && n.text.size() <= 60);
            for (const char ch : n.text) {
                const unsigned char u = static_cast<unsigned char>(ch);
                ASSERT_TRUE(u >= 0x20 && u < 0x7F && std::string("/\\:*?\"<>|").find(ch) == std::string::npos);
            }
            ASSERT_TRUE(n.text[0] != '.' && n.text.find("..") == std::string::npos);
            ASSERT_EQ(names::file_name_of(n.text), n.text + ".lvl");
        }
        ASSERT_EQ(names::name_key("River RUN"), std::string("river run"));
        ASSERT_EQ(names::name_key(names::clean_map_name("RIVER   run ").text), std::string("river run"));
    } TEST_END();

    TEST_CASE("NM2.4 The maker's name and the note: letters of several scripts are kept as they are, a combining mark or a compatibility form is refused (so the text is always its own NFC form), angle brackets and controls are refused") {
        const auto maker = [](const std::string& raw) { return names::clean_maker(raw); };
        ASSERT_TRUE(maker("Mia").ok && maker("J\xC3\xBCrgen").text == "J\xC3\xBCrgen" && maker("\xE6\x97\xA5\xE6\x9C\xAC").ok && maker("\xD0\x98\xD0\xB2\xD0\xB0\xD0\xBD").ok && maker("\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1").ok);
        ASSERT_TRUE(maker("\xED\x95\x9C\xEA\xB8\x80").ok && maker("\xE3\x81\x82\xE3\x82\xA2").ok && maker("O'Neil-Smith").ok);
        ASSERT_EQ(maker("e\xCC\x81").reason, std::string("characters"));                           // e + a combining acute: NFC would make one letter, so the text is refused instead
        ASSERT_EQ(maker("\xE2\x84\xAB").reason, std::string("characters"));                         // the Angstrom sign: NFC is another character
        ASSERT_EQ(maker("\xEF\xAC\x81").reason, std::string("characters"));                         // the ligature "fi": a compatibility form
        ASSERT_EQ(maker("\xE2\x84\xA6").reason, std::string("characters"));                         // the Ohm sign
        ASSERT_EQ(maker("\xE1\x84\x92\xE1\x85\xA1").reason, std::string("characters"));            // Hangul jamo that NFC would join
        ASSERT_EQ(maker("\xF0\x9F\x98\x80").reason, std::string("characters"));                    // an emoji
        ASSERT_EQ(maker("<b>x</b>").reason, std::string("characters"));
        ASSERT_EQ(maker("a\nb").reason, std::string("control"));
        ASSERT_EQ(maker("").reason, std::string("empty"));
        ASSERT_EQ(maker(std::string(24, 'm')).ok, true);
        ASSERT_EQ(maker(std::string(25, 'm')).reason, std::string("too_long"));
        ASSERT_EQ(maker("\xE2\x80\x8D" "Mia\xE2\x80\x8B").text, std::string("Mia"));
        const auto note = [](const std::string& raw, bool required = false) { return names::clean_note(raw, 280, required); };
        ASSERT_TRUE(note("").ok && note("").text.empty());
        ASSERT_EQ(note("", true).reason, std::string("empty"));
        ASSERT_EQ(note("a\r\nb").text, std::string("a\nb"));
        ASSERT_EQ(note("a\n\n\n\nb").text, std::string("a\n\nb"));
        ASSERT_EQ(note("  a  \n  b  ").text, std::string("a\nb"));
        ASSERT_EQ(note("tab\there").text, std::string("tab here"));
        ASSERT_EQ(note("1 < 2").reason, std::string("characters"));
        ASSERT_EQ(note("say \"hi\" & 'bye'").text, std::string("say \"hi\" & 'bye'"));              // (quotes and an ampersand stay: the page shows text, never HTML)
        ASSERT_EQ(note(std::string(280, 'n')).ok, true);
        ASSERT_EQ(note(std::string(281, 'n')).reason, std::string("too_long"));
        ASSERT_EQ(note(std::string(1000000, 'n')).reason, std::string("too_long"));
        ASSERT_EQ(note("a\x1B[31m").reason, std::string("control"));
    } TEST_END();

    TEST_CASE("NM3.1 The next free name: from 2, the number it ends in is replaced, a taken number is skipped, the name is cut to fit, no name is free after 999") {
        std::set<std::string> taken;
        const auto next = [&](const std::string& name) { return names::next_free_name(name, [&](const std::string& key) { return taken.count(key) != 0; }); };
        ASSERT_EQ(next("Treasure"), std::string("Treasure 2"));
        taken = {"treasure 2", "treasure 3"};
        ASSERT_EQ(next("Treasure"), std::string("Treasure 4"));
        ASSERT_EQ(next("Treasure 2"), std::string("Treasure 4"));
        ASSERT_EQ(next("Treasure 7"), std::string("Treasure 4"));
        ASSERT_EQ(next("Map 2023"), std::string("Map 2"));                                          // (a year is a number too: the owner types another name if he wants)
        const std::string long_name(60, 'x');
        const std::string cut = next(long_name);
        ASSERT_TRUE(cut.size() <= 60 && cut.compare(cut.size() - 2, 2, " 2") == 0);
        taken.clear();
        for (int i = 2; i < 1000; ++i) taken.insert("busy " + std::to_string(i));
        ASSERT_EQ(next("Busy"), std::string());
    } TEST_END();

    TEST_CASE("NM4.1 The address that a request counts under: the proxy's header when it is trusted and well formed, else the connection; IPv6 is its /64; a mapped IPv4 address is the IPv4 one; nonsense is no address") {
        const auto id = [](const std::map<std::string, std::string>& headers, const std::string& peer, bool trust = true) { return client_identity(request_with(headers, peer), trust); };
        ASSERT_EQ(id({{"x-client-address", "203.0.113.7"}}, "172.18.0.4"), std::string("203.0.113.7"));
        ASSERT_EQ(id({}, "172.18.0.4"), std::string("172.18.0.4"));
        ASSERT_EQ(id({{"x-client-address", "203.0.113.7"}}, "172.18.0.4", false), std::string("172.18.0.4"));   // (not trusted: the visitor's own header means nothing)
        ASSERT_EQ(id({{"x-client-address", "not an address"}}, "172.18.0.4"), std::string("172.18.0.4"));
        ASSERT_EQ(id({{"x-client-address", "999.1.1.1"}}, "172.18.0.4"), std::string("172.18.0.4"));
        ASSERT_EQ(id({{"x-client-address", "1.2.3.4, 5.6.7.8"}}, "172.18.0.4"), std::string("172.18.0.4"));         // (a list is no address: the proxy gives one value)
        ASSERT_EQ(id({{"x-client-address", std::string(60, '1')}}, "172.18.0.4"), std::string("172.18.0.4"));
        ASSERT_EQ(id({{"x-client-address", ""}}, "172.18.0.4"), std::string("172.18.0.4"));
        ASSERT_EQ(id({{"x-client-address", "2001:db8:1:2:aaaa:bbbb:cccc:dddd"}}, ""), id({{"x-client-address", "2001:db8:1:2::1"}}, ""));       // (one /64 is one address)
        ASSERT_NE(id({{"x-client-address", "2001:db8:1:2::1"}}, ""), id({{"x-client-address", "2001:db8:1:3::1"}}, ""));
        ASSERT_EQ(id({{"x-client-address", "::ffff:203.0.113.7"}}, ""), std::string("203.0.113.7"));
        ASSERT_EQ(id({{"x-client-address", "203.0.113.07"}}, "10.0.0.1").empty(), false);
        ASSERT_EQ(id({}, ""), std::string());
        ASSERT_EQ(id({{"x-client-address", "garbage"}}, ""), std::string());
        ASSERT_EQ(id({{"x-client-address", "2001:db8::1"}}, "").compare(0, 3, "v6:"), 0);
    } TEST_END();

    TEST_CASE("NM4.2 The query of a call: a value is decoded, a bad escape is refused, a key is matched whole, the first of a repeated key wins") {
        std::string v;
        ASSERT_TRUE(query_value("kind=classic&name=Big%20Lake&maker=J%C3%BCrgen", "name", v) && v == "Big Lake");
        ASSERT_TRUE(query_value("kind=classic&name=Big%20Lake&maker=J%C3%BCrgen", "maker", v) && v == "J\xC3\xBCrgen");
        ASSERT_TRUE(query_value("a=1&a=2", "a", v) && v == "1");
        ASSERT_TRUE(query_value("name=a+b", "name", v) && v == "a+b");
        ASSERT_FALSE(query_value("kind=classic", "name", v));
        ASSERT_FALSE(query_value("xname=1", "name", v));
        ASSERT_FALSE(query_value("name=%zz", "name", v));
        ASSERT_FALSE(query_value("name=%4", "name", v));
        ASSERT_FALSE(query_value("name", "name", v));
        ASSERT_TRUE(query_value("name=", "name", v) && v.empty());
        ASSERT_TRUE(query_value("name=a%00b", "name", v) && v == std::string("a\0b", 3));            // (decoded: the cleaning of the name refuses the NUL)
        std::string out;
        ASSERT_TRUE(percent_decode_text("a%2Fb%20c", out) && out == "a/b c");
        ASSERT_FALSE(percent_decode_text("%", out));
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}

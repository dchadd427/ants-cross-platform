// Tests of the control interface: the strict JSON library (CTL1.*)
#include "ctl_fixtures.hpp"

void run_json_tests() {
    // ============================================================================================
    // JSON: documents that must parse
    // ============================================================================================

    TEST_CASE("CTL1.1 JSON: valid documents parse, serialize and parse again to an equal value") {
        const std::vector<std::string> valid = {
            "null", "true", "false", "0", "-0", "1", "-1", "123456789", "1.5", "-1.5e3", "1E+2", "1e-2", "0.0", "0e0", "0E0", "0.1e1", "1e5",
            "\"\"", "\"a\"", "[]", "{}", " [ ] ", "\t\r\n[1 , 2]\n", "[null,true,false,0,\"x\",[],{}]", "{\"a\":1,\"b\":[1,2,{\"c\":null}]}",
            "{\"a\":1,\"A\":2}", "{\"\":1}", "{\"a\":{\"a\":1}}", "[[],[]]", "[{},{}]", "[[[[]]]]", "{\"a\":[{\"a\":[]}]}",
            "\"\\u0041\"", "\"\\ud83d\\ude00\"", "\"\\uD83D\\uDE00\"", "\"\\u0000\"", "\"\\/\"", "\"\\b\\f\\n\\r\\t\\\"\\\\\"", "\"\\u00e9\\u00E9\"",
            "\"\\uffff\"", "\"\\ue000\"", "\"\\ud7ff\"", "\"\\udbff\\udfff\"", "\"\\ud800\\udc00\"", "[\"\\u0022\"]", "{\"\\u0061\":1}",
            "1e308", "-1e308", "5e-324", "1e-400", "-1e-400", "2.2250738585072014e-308", "1.7976931348623157e308", "0.000000000000000000000000000001",
            "9223372036854775807", "-9223372036854775808", "9223372036854775808", "-9223372036854775809", "18446744073709551615",
            "123456789012345678901234567890", "[1,2,3,4,5,6,7,8,9,10]", "{\"k1\":1,\"k2\":2,\"k3\":3}",
            lit("\"\x7f\""), lit("\"\xC2\x80\""), lit("\"\xDF\xBF\""), lit("\"\xE0\xA0\x80\""), lit("\"\xED\x9F\xBF\""), lit("\"\xEE\x80\x80\""),
            lit("\"\xEF\xBF\xBD\""), lit("\"\xEF\xBF\xBF\""), lit("\"\xF0\x90\x80\x80\""), lit("\"\xF4\x8F\xBF\xBF\""), lit("\"\xF0\x9F\x98\x80\""),
            lit("\"caf\xC3\xA9\""), lit("{\"\xE2\x82\xAC\":\"\xE2\x82\xAC\"}"), lit("\"\xEF\xBB\xBF\""),                    // a BOM inside a string is just text
        };
        for (const std::string& text : valid) {
            JsonValue v;
            std::string error;
            ASSERT_MSG(parse_json(text, v, &error), "should parse: " + shown(text) + "  (" + error + ")");
            ASSERT_MSG(error.empty(), "an empty error on success: " + shown(text));
            const std::string out = to_json(v);
            JsonValue again;
            ASSERT_MSG(parse_json(out, again, &error), "serialized form should parse: " + shown(out));
            ASSERT_MSG(again == v, "round trip changed the value: " + shown(text) + " -> " + shown(out));
            ASSERT_MSG(to_json(again) == out, "serialization is not stable: " + shown(out));
            ASSERT_MSG(recognizes(text), "the independent recognizer disagrees: " + shown(text));
        }
    } TEST_END();

    TEST_CASE("CTL1.2 JSON: documents that must be refused (JSONTestSuite style)") {
        const std::vector<std::string> invalid = {
            // empty and broken literals
            "", " ", "\n", "nul", "nulL", "NULL", "Null", "True", "FALSE", "tru", "fals", "truee", "nullx", "null null", "n", "t", "f",
            // structure
            "[", "]", "{", "}", "[1,]", "[,1]", "[,]", "[1 2]", "[1,,2]", "[1}", "{]", "{\"a\":1,}", "{,}", "{\"a\"}", "{\"a\":}", "{\"a\" 1}", "{\"a\":1 \"b\":2}",
            "{a:1}", "{'a':1}", "['a']", "{1:2}", "{null:1}", "{\"a\":1,,\"b\":2}", "{\"a\"::1}", "[\"a\":1]", "{\"a\",1}", "[1,2", "{\"a\":[1,2}", "[[]", "[]]", "{}}",
            "[1][2]", "{} {}", "1 2", "[] x", "\"a\" \"b\"", "[\"a\" \"b\"]", "{\"a\":1}}", "[1,2,3]]", "{\"a\":1}x",
            // numbers
            "01", "-01", "00", "-00", "1.", "-1.", ".5", "-.5", "1.e2", "1e", "1e+", "1e-", "1E", "+1", "+0", "- 1", "--1", "-", "0x10", "1_000", "Infinity", "-Infinity", "NaN",
            "-NaN", "1e1.5", "0.1.2", "1..2", "1e2e3", "1.5.", "1,5", "0e", "0.e1", "1 e5", "1e 5", "\xD9\xA1\xD9\xA2\xD9\xA3", "1\xCE\xBC", "0b1", "1f", "1d", "1L", "inf", "nan", "1e999", "-1e999",
            "[1e999]", "{\"a\":1e400}", "[01]", "[1.]", "[.5]", "[-]", "[+1]",
            // strings
            "\"abc", "\"", "\"a\nb\"", "\"a\tb\"", "\"a\rb\"", lit("\"\x01\""), lit("\"\x1f\""), lit("\"a\0b\""), "\"\\x41\"", "\"\\a\"", "\"\\'\"", "\"\\", "\"\\\"",
            "\"\\u\"", "\"\\u1\"", "\"\\u12\"", "\"\\u123\"", "\"\\u12G4\"", "\"\\u+123\"", "\"\\u-123\"", "\"\\u 123\"", "\"\\U0041\"", "\"\\0\"", "\"\\1\"", "\"\\ \"", "\"\\/\\\"",
            "'abc'", "abc", "\"a\"b\"",
            // surrogates
            "\"\\ud800\"", "\"\\udbff\"", "\"\\udc00\"", "\"\\udfff\"", "\"\\ud800\\u0041\"", "\"\\ud800\\ud800\"", "\"\\ud800\\udbff\"", "\"\\udc00\\ud800\"", "\"\\udc00\\udc00\"",
            "\"\\ud800x\"", "\"\\ud800\\\"", "\"\\ud800\\n\"", "\"\\ud800\\u\"", "\"\\ud800\\u00\"", "\"\\ud83d\\ude0\"", "\"\\ud83d \\ude00\"", "[\"\\ud800\"]", "{\"\\udc00\":1}",
            // UTF-8
            lit("\"\x80\""), lit("\"\xBF\""), lit("\"\xC0\x80\""), lit("\"\xC1\xBF\""), lit("\"\xC2\""), lit("\"\xC2\x20\""), lit("\"\xC2\xC2\""), lit("\"\xE0\x80\x80\""),
            lit("\"\xE0\x9F\xBF\""), lit("\"\xE2\x82\""), lit("\"\xE2\x82\x20\""), lit("\"\xED\xA0\x80\""), lit("\"\xED\xBF\xBF\""), lit("\"\xED\xAF\xBF\xED\xB0\x80\""),
            lit("\"\xF0\x80\x80\x80\""), lit("\"\xF0\x8F\xBF\xBF\""), lit("\"\xF0\x9F\x98\""), lit("\"\xF4\x90\x80\x80\""), lit("\"\xF5\x80\x80\x80\""), lit("\"\xF7\xBF\xBF\xBF\""),
            lit("\"\xF8\x88\x80\x80\x80\""), lit("\"\xFC\x84\x80\x80\x80\x80\""), lit("\"\xFE\""), lit("\"\xFF\""), lit("{\"\xFF\":1}"), lit("{\"\xC0\xAF\":1}"),
            lit("\"\xE2\x28\xA1\""), lit("\"\xF0\x28\x8C\xBC\""),
            // outside a string: UTF-8 is not white space or a value
            lit("\xEF\xBB\xBF[]"), lit("\xEF\xBB\xBF" "1"), lit("\xC2\xA0[]"), lit("[]\xC2\xA0"), lit("\xE2\x80\xA8[]"), lit("[\xE2\x80\x83" "1]"), lit("\xFF"), lit("\x80"),
            // control characters and other white space
            lit("[1]\0"), lit("\0"), lit("[\0]"), lit("[1,\0" "2]"), lit("\x01"), "\f[]", "\v[]", lit("\x0b" "1"), lit("[1]\x1a"), lit("\x7f"), "\x85[]",
            // comments and extensions
            "// c\n1", "/* c */ 1", "[1,/*c*/2]", "# comment", "[1] // x", "[1] /* x */", "{\"a\":1 /* c */}", "[1,\n// c\n2]", "{\"a\":1}//", "[0x1]", "[1,2,]", "{\"a\":+1}", "[undefined]", "[NaN]",
            "[Infinity]", "[-Infinity]", "[.1]", "['a']", "{\"a\":'b'}", "[`a`]", "[a]", "[\"a\",]", "{\"a\":\"b\",}", "(1)", "<1>", "1;", "1,", ",1",
            // repeated keys
            "{\"a\":1,\"a\":2}", "{\"a\":1,\"a\":1}", "{\"a\":1,\"b\":2,\"a\":3}", "{\"a\":1,\"\\u0061\":2}", "{\"\\u0061\":1,\"a\":2}", lit("{\"\\u0000\":1,\"\\u0000\":2}"),
            "{\"a\":{\"x\":1,\"x\":2}}", "[{\"a\":1,\"a\":1}]", "{\"a\":[{\"b\":1,\"b\":2}]}", "{\"\":1,\"\":2}", lit("{\"\xC3\xA9\":1,\"\\u00e9\":2}"), "{\"a\\n\":1,\"a\\n\":2}",
            "{\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8,\"k9\":9,\"k10\":10,\"k11\":11,\"k12\":12,\"k13\":13,\"k14\":14,\"k15\":15,\"k16\":16,\"k17\":17,\"k5\":0}",
            "{\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8,\"k9\":9,\"k10\":10,\"k11\":11,\"k12\":12,\"k13\":13,\"k14\":14,\"k15\":15,\"k16\":16,\"k16\":0}",
        };
        for (const std::string& text : invalid) {
            JsonValue v = JsonValue::make_bool(true);
            std::string error;
            ASSERT_MSG(!parse_json(text, v, &error), "should be refused: " + shown(text));
            ASSERT_MSG(v.is_null(), "out is left null after a refusal: " + shown(text));
            ASSERT_MSG(!error.empty() && error.find("byte") != std::string::npos, "an error text with a position for: " + shown(text) + "  (" + error + ")");
            ASSERT_FALSE(parse_json(text, v, nullptr));                       // without an error pointer
        }
    } TEST_END();

    TEST_CASE("CTL1.3 JSON: error messages say what was wrong") {
        ASSERT_TRUE(error_of("{\"a\":1,\"a\":2}").find("duplicate key") != std::string::npos);
        ASSERT_TRUE(error_of("[1,]").find("unexpected character") != std::string::npos);
        ASSERT_TRUE(error_of("01").find("leading zero") != std::string::npos);
        ASSERT_TRUE(error_of("\"\\ud800\"").find("surrogate") != std::string::npos);
        ASSERT_TRUE(error_of(lit("\"\xFF\"")).find("UTF-8") != std::string::npos);
        ASSERT_TRUE(error_of("\"a\nb\"").find("control character") != std::string::npos);
        ASSERT_TRUE(error_of("[1] 2").find("after the value") != std::string::npos);
        ASSERT_TRUE(error_of("1e999").find("out of range") != std::string::npos);
        ASSERT_TRUE(error_of("").find("empty") != std::string::npos);
        ASSERT_TRUE(error_of("{\"a\":1,\"a\":2}").find("at byte 7") != std::string::npos);       // the position of the second "a"
        ASSERT_TRUE(error_of("[1,x]").find("at byte 3") != std::string::npos);
    } TEST_END();

    // ============================================================================================
    // JSON: limits
    // ============================================================================================

    TEST_CASE("CTL1.4 JSON limits: depth 16 passes, 17 does not (arrays, objects, mixed), and a hostile depth cannot overflow the stack") {
        ASSERT_TRUE(parses(nested_arrays(16)));
        ASSERT_FALSE(parses(nested_arrays(17)));
        ASSERT_TRUE(parses(nested_objects(16)));
        ASSERT_FALSE(parses(nested_objects(17)));
        ASSERT_TRUE(parses("[[[[[[[[[[[[[[[{\"a\":1}]]]]]]]]]]]]]]]"));         // 15 arrays and an object: 16
        ASSERT_FALSE(parses("[[[[[[[[[[[[[[[[{\"a\":1}]]]]]]]]]]]]]]]]"));      // 16 and an object: 17
        ASSERT_TRUE(error_of(nested_arrays(17)).find("too deep") != std::string::npos);
        ASSERT_TRUE(parses("1"));                                                // a scalar is depth 0
        ASSERT_TRUE(parses("[1]"));
        // a document of the largest size made of nothing but opening brackets
        ASSERT_FALSE(parses(std::string(64 * 1024, '[')));
        ASSERT_FALSE(parses(std::string(64 * 1024, '{')));
        ASSERT_FALSE(parses("{\"a\":" + std::string(60000, '[')));
        // JsonLimits can lower or raise the depth, but never above 128
        JsonLimits shallow;
        shallow.max_depth = 2;
        ASSERT_TRUE(error_of("[[1]]", shallow).empty());
        ASSERT_FALSE(error_of("[[[1]]]", shallow).empty());
        JsonLimits zero;
        zero.max_depth = 0;
        ASSERT_TRUE(error_of("1", zero).empty());
        ASSERT_FALSE(error_of("[]", zero).empty());
        JsonLimits deep;
        deep.max_depth = 100000;
        ASSERT_TRUE(error_of(nested_arrays(128), deep).empty());
        ASSERT_FALSE(error_of(nested_arrays(129), deep).empty());
        deep.max_bytes = 10 * 1024 * 1024;
        ASSERT_FALSE(error_of(std::string(1000000, '['), deep).empty());
    } TEST_END();

    TEST_CASE("CTL1.5 JSON limits: 64 KB for the document") {
        const std::string head = "\"";
        std::string exact = head + std::string(64 * 1024 - 2, 'a') + "\"";
        ASSERT_EQ(exact.size(), 64u * 1024u);
        ASSERT_TRUE(parses(exact));
        std::string over = head + std::string(64 * 1024 - 1, 'a') + "\"";
        ASSERT_FALSE(parses(over));
        ASSERT_TRUE(error_of(over).find("too large") != std::string::npos);
        // white space counts too
        ASSERT_TRUE(parses("1" + std::string(64 * 1024 - 1, ' ')));
        ASSERT_FALSE(parses("1" + std::string(64 * 1024, ' ')));
        JsonLimits tiny;
        tiny.max_bytes = 8;
        ASSERT_TRUE(error_of("[1,2,3]", tiny).empty());
        ASSERT_FALSE(error_of("[1,2,3,4]", tiny).empty());
        JsonValue v;
        ASSERT_FALSE(parse_json(over, v, nullptr));
        ASSERT_TRUE(v.is_null());
    } TEST_END();

    TEST_CASE("CTL1.6 JSON limits: 4096 elements per container, 1024 bytes per key, 128 characters per number") {
        const auto array_of = [](size_t n) {
            std::string s = "[";
            for (size_t i = 0; i < n; ++i) s += (i ? ",1" : "1");
            return s + "]";
        };
        const auto object_of = [](size_t n) {
            std::string s = "{";
            for (size_t i = 0; i < n; ++i) s += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":1";
            return s + "}";
        };
        ASSERT_TRUE(parses(array_of(4096)));
        ASSERT_FALSE(parses(array_of(4097)));
        ASSERT_TRUE(parses(object_of(4096)));
        ASSERT_FALSE(parses(object_of(4097)));
        ASSERT_TRUE(error_of(array_of(4097)).find("too many") != std::string::npos);
        JsonValue big = parse_ok(object_of(4096));
        ASSERT_EQ(big.size(), 4096u);
        ASSERT_EQ(big.get("k4095").as_int_or(0), 1);
        ASSERT_EQ(big.key_at(4095), "k4095");
        // 4096 elements in each of several nested containers is fine as long as every container is within its own limit
        ASSERT_TRUE(parses("[" + array_of(4096) + "," + array_of(4096) + "]"));
        JsonLimits few;
        few.max_elements = 2;
        ASSERT_TRUE(error_of("[1,2]", few).empty());
        ASSERT_FALSE(error_of("[1,2,3]", few).empty());
        ASSERT_TRUE(error_of("{\"a\":1,\"b\":2}", few).empty());
        ASSERT_FALSE(error_of("{\"a\":1,\"b\":2,\"c\":3}", few).empty());
        JsonLimits none;
        none.max_elements = 0;
        ASSERT_TRUE(error_of("[]", none).empty());
        ASSERT_TRUE(error_of("{}", none).empty());
        ASSERT_FALSE(error_of("[1]", none).empty());

        ASSERT_TRUE(parses("{\"" + std::string(1024, 'k') + "\":1}"));
        ASSERT_FALSE(parses("{\"" + std::string(1025, 'k') + "\":1}"));
        ASSERT_TRUE(error_of("{\"" + std::string(1025, 'k') + "\":1}").find("key too long") != std::string::npos);
        std::string accents;
        for (int i = 0; i < 512; ++i) accents += "\\u00e9";                       // 512 characters of two bytes: 1024 bytes after unescaping
        ASSERT_TRUE(parses("{\"" + accents + "\":1}"));
        ASSERT_FALSE(parses("{\"" + accents + "\\u00e9\":1}"));
        ASSERT_TRUE(parses("[\"" + std::string(5000, 'v') + "\"]"));              // a value may be longer than a key
        JsonLimits short_keys;
        short_keys.max_key_chars = 3;
        ASSERT_TRUE(error_of("{\"abc\":1}", short_keys).empty());
        ASSERT_FALSE(error_of("{\"abcd\":1}", short_keys).empty());

        ASSERT_TRUE(parses("0." + std::string(125, '0') + "1"));                  // 128 characters
        ASSERT_FALSE(parses("0." + std::string(126, '0') + "1"));                 // 129
        ASSERT_TRUE(error_of(std::string(200, '9')).find("too long") != std::string::npos);
        ASSERT_FALSE(parses(std::string(64 * 1024, '1')));
        ASSERT_FALSE(parses("1e" + std::string(5000, '9')));
    } TEST_END();

    TEST_CASE("CTL1.7 JSON limits: nothing a hostile document can do takes long or crashes") {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::string> nasty;
        nasty.push_back(std::string(64 * 1024, '['));
        nasty.push_back(std::string(64 * 1024, '{'));
        nasty.push_back("[" + std::string(64 * 1024 - 2, ','));
        nasty.push_back("\"" + std::string(64 * 1024 - 2, '\\') + "\"");
        nasty.push_back("\"" + std::string(32000, '\\') + "u\"");
        nasty.push_back(std::string(64 * 1024, '1'));
        nasty.push_back("[" + std::string(30000, '1') + ",1]");
        nasty.push_back("0." + std::string(64 * 1024 - 3, '9'));
        nasty.push_back("-" + std::string(64 * 1024 - 2, '0'));
        nasty.push_back("{" + std::string(64 * 1024 - 2, '"') + "}");
        {
            std::string s = "{";                                                       // 4096 members, the last one repeats the first: the hash set is used
            for (int i = 0; i < 4096; ++i) s += "\"key" + std::to_string(i) + "\":0,";
            nasty.push_back(s + "\"key0\":0}");
        }
        {
            std::string s = "[";
            for (int i = 0; i < 6000; ++i) s += "[],";                                  // 6000 elements: over the limit after 4096
            nasty.push_back(s + "[]]");
        }
        {
            std::string s;                                                              // many short strings with every kind of escape
            s += "[";
            for (int i = 0; i < 8000; ++i) s += "\"\\u00e9\\ud83d\\ude00\",";
            nasty.push_back(s + "1]");
        }
        for (int round = 0; round < 5; ++round) {
            for (const std::string& n : nasty) {
                JsonValue v;
                std::string e;
                parse_json(n, v, &e);
            }
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        ASSERT_MSG(ms < 10000, "slow: " + std::to_string(ms) + " ms");
    } TEST_END();

    // ============================================================================================
    // JSON: kinds of numbers, accessors
    // ============================================================================================

    TEST_CASE("CTL1.8 JSON numbers keep their kind: integers (int64) and doubles are apart") {
        JsonValue v = parse_ok("1");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(-1), 1);
        v = parse_ok("1.0");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 1.0);
        ASSERT_EQ(v.as_int_or(-1), -1);                                         // a double is not an integer, even when it is a whole number
        v = parse_ok("1e2");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 100.0);
        v = parse_ok("-0");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(-1), 0);
        v = parse_ok("-0.0");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(std::signbit(v.as_double_or(1.0)));
        v = parse_ok("9223372036854775807");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(0), std::numeric_limits<int64_t>::max());
        v = parse_ok("-9223372036854775808");
        ASSERT_TRUE(v.is_int());
        ASSERT_EQ(v.as_int_or(0), std::numeric_limits<int64_t>::min());
        v = parse_ok("9223372036854775808");                                    // one more than int64 holds: a double, so an int lookup falls back
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 9223372036854775808.0);
        ASSERT_EQ(v.as_int_or(-7), -7);
        v = parse_ok("-9223372036854775809");
        ASSERT_TRUE(v.is_double());
        v = parse_ok("123456789012345678901234567890");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 123456789012345678901234567890.0);
        v = parse_ok("0000000000000000000000");                                 // leading zeros are an error even here
        ASSERT_TRUE(v.is_string());                                             // (parse_ok returns the failure as a string)
        v = parse_ok("1e-400");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(1.0) == 0.0);
        v = parse_ok("5e-324");
        ASSERT_TRUE(v.is_double());
        ASSERT_TRUE(v.as_double_or(0) == 5e-324);
        v = parse_ok("1.7976931348623157e308");
        ASSERT_TRUE(v.as_double_or(0) == 1.7976931348623157e308);
        v = parse_ok("0.1");
        ASSERT_TRUE(v.as_double_or(0) == 0.1);
        v = parse_ok("[1, 1.0, 1e0, -1, 10, 1E1]");
        ASSERT_TRUE(v.at(0).is_int() && v.at(1).is_double() && v.at(2).is_double() && v.at(3).is_int() && v.at(4).is_int() && v.at(5).is_double());
        ASSERT_FALSE(v.at(0) == v.at(1));                                       // 1 and 1.0 are different values
    } TEST_END();

    TEST_CASE("CTL1.9 JSON accessors never throw: wrong kinds give fallbacks, lookups give null") {
        const JsonValue doc = parse_ok("{\"room\":{\"seats\":4,\"name\":\"Anthill\",\"open\":true,\"ratio\":0.5,\"tags\":[\"a\",\"b\"],\"none\":null},\"n\":7}");
        ASSERT_TRUE(doc.is_object());
        ASSERT_EQ(doc.size(), 2u);
        ASSERT_EQ(doc.get("room").get("seats").as_int_or(0), 4);
        ASSERT_EQ(doc.get("room").get("name").as_string_or("x"), "Anthill");
        ASSERT_EQ(doc.get("room").get("name").str(), "Anthill");
        ASSERT_TRUE(doc.get("room").get("open").as_bool_or(false));
        ASSERT_TRUE(doc.get("room").get("ratio").as_double_or(0) == 0.5);
        ASSERT_TRUE(doc.get("n").as_double_or(0) == 7.0);                       // an integer reads as a double
        ASSERT_EQ(doc.get("room").get("tags").size(), 2u);
        ASSERT_EQ(doc.get("room").get("tags").at(1).as_string_or(""), "b");
        // missing members and wrong kinds
        ASSERT_TRUE(doc.get("missing").is_null());
        ASSERT_TRUE(doc.get("missing").get("deeper").get("still").is_null());
        ASSERT_EQ(doc.get("missing").as_int_or(42), 42);
        ASSERT_EQ(doc.get("room").get("name").as_int_or(5), 5);
        ASSERT_EQ(doc.get("room").get("seats").as_string_or("fb"), "fb");
        ASSERT_EQ(doc.get("room").get("seats").as_string_or(), "");
        ASSERT_TRUE(doc.get("room").get("seats").as_bool_or(true));
        ASSERT_FALSE(doc.get("room").get("seats").as_bool_or(false));
        ASSERT_EQ(doc.get("room").get("seats").str(), "");
        ASSERT_TRUE(doc.get("room").get("tags").get("x").is_null());            // a key on an array
        ASSERT_TRUE(doc.get("room").get("seats").at(0).is_null());              // an index on a number
        ASSERT_TRUE(doc.get("room").get("tags").at(2).is_null());               // out of range
        ASSERT_TRUE(doc.get("room").get("tags").at(static_cast<size_t>(-1)).is_null());
        ASSERT_EQ(doc.get("room").get("seats").size(), 0u);
        ASSERT_EQ(doc.key_at(0), "room");
        ASSERT_EQ(doc.key_at(1), "n");
        ASSERT_EQ(doc.key_at(2), "");
        ASSERT_EQ(doc.get("room").get("tags").key_at(0), "");
        // find tells a missing member from a null one
        ASSERT_TRUE(doc.get("room").find("none") != nullptr);
        ASSERT_TRUE(doc.get("room").find("none")->is_null());
        ASSERT_TRUE(doc.get("room").find("nope") == nullptr);
        ASSERT_TRUE(doc.find("seats") == nullptr);                              // only direct members
        ASSERT_TRUE(doc.get("room").has("seats"));
        ASSERT_FALSE(doc.get("room").has("Seats"));                             // keys are case sensitive
        ASSERT_TRUE(parse_ok("[1]").find("a") == nullptr);
        ASSERT_TRUE(JsonValue().find("a") == nullptr);
        ASSERT_TRUE(JsonValue().is_null());
        ASSERT_EQ(JsonValue().as_int_or(9), 9);
        // items() and keys() walk a container in order
        ASSERT_EQ(doc.keys().size(), doc.items().size());
        ASSERT_EQ(doc.keys()[0], "room");
        // the elements of an object keep the insertion order of the document
        const JsonValue ordered = parse_ok("{\"z\":1,\"a\":2,\"m\":3}");
        ASSERT_EQ(ordered.key_at(0), "z");
        ASSERT_EQ(ordered.key_at(1), "a");
        ASSERT_EQ(ordered.key_at(2), "m");
        ASSERT_EQ(to_json(ordered), "{\"z\":1,\"a\":2,\"m\":3}");
    } TEST_END();

    TEST_CASE("CTL1.10 JSON building: push_back and set check the kind, set keeps keys distinct and in place") {
        JsonValue arr = JsonValue::make_array();
        ASSERT_TRUE(arr.push_back(JsonValue::make_int(1)));
        ASSERT_TRUE(arr.push_back(JsonValue::make_string("two")));
        ASSERT_FALSE(arr.set("k", JsonValue::make_null()));
        ASSERT_EQ(to_json(arr), "[1,\"two\"]");
        JsonValue obj = JsonValue::make_object();
        ASSERT_TRUE(obj.set("b", JsonValue::make_int(1)));
        ASSERT_TRUE(obj.set("a", JsonValue::make_int(2)));
        ASSERT_TRUE(obj.set("b", JsonValue::make_int(3)));                      // replaces, keeps its place
        ASSERT_EQ(to_json(obj), "{\"b\":3,\"a\":2}");
        ASSERT_EQ(obj.size(), 2u);
        ASSERT_FALSE(obj.push_back(JsonValue::make_null()));
        ASSERT_EQ(obj.size(), 2u);
        JsonValue scalar = JsonValue::make_int(5);
        ASSERT_FALSE(scalar.push_back(JsonValue::make_int(1)));
        ASSERT_FALSE(scalar.set("a", JsonValue::make_int(1)));
        ASSERT_EQ(to_json(scalar), "5");
        JsonValue* b = obj.find("b");
        ASSERT_TRUE(b != nullptr);
        *b = JsonValue::make_string("changed");
        ASSERT_EQ(to_json(obj), "{\"b\":\"changed\",\"a\":2}");
        // make_*_from
        ASSERT_EQ(to_json(JsonValue::make_array_from({JsonValue::make_bool(true), JsonValue::make_null()})), "[true,null]");
        ASSERT_EQ(to_json(JsonValue::make_object_from({"x", "y", "z"}, {JsonValue::make_int(1), JsonValue::make_int(2)})), "{\"x\":1,\"y\":2}");   // the longer is cut
        ASSERT_EQ(to_json(JsonValue::make_object_from({"x"}, {JsonValue::make_int(1), JsonValue::make_int(2)})), "{\"x\":1}");
        // equality
        ASSERT_TRUE(parse_ok("{\"a\":[1,2.5,\"x\",null,true]}") == parse_ok(" { \"a\" : [ 1 , 2.5 , \"x\" , null , true ] } "));
        ASSERT_FALSE(parse_ok("{\"a\":1,\"b\":2}") == parse_ok("{\"b\":2,\"a\":1}"));         // members compare in order
        ASSERT_FALSE(parse_ok("[1,2]") == parse_ok("[1,2,3]"));
        ASSERT_TRUE(parse_ok("[1,2]") != parse_ok("[2,1]"));
        ASSERT_FALSE(parse_ok("0.0") == parse_ok("-0.0"));
        ASSERT_FALSE(JsonValue::make_string("1") == JsonValue::make_int(1));
        ASSERT_TRUE(JsonValue() == JsonValue::make_null());
    } TEST_END();

    // ============================================================================================
    // JSON: the serializer
    // ============================================================================================

    TEST_CASE("CTL1.11 JSON serializer: compact, deterministic, escapes exactly what must be escaped") {
        ASSERT_EQ(to_json(JsonValue()), "null");
        ASSERT_EQ(to_json(JsonValue::make_bool(true)), "true");
        ASSERT_EQ(to_json(JsonValue::make_bool(false)), "false");
        ASSERT_EQ(to_json(JsonValue::make_int(0)), "0");
        ASSERT_EQ(to_json(JsonValue::make_int(-12)), "-12");
        ASSERT_EQ(to_json(JsonValue::make_int(std::numeric_limits<int64_t>::min())), "-9223372036854775808");
        ASSERT_EQ(to_json(JsonValue::make_int(std::numeric_limits<int64_t>::max())), "9223372036854775807");
        ASSERT_EQ(to_json(JsonValue::make_string("")), "\"\"");
        ASSERT_EQ(to_json(JsonValue::make_string("plain /slash/ and DEL \x7f")), "\"plain /slash/ and DEL \x7f\"");      // / and DEL stay as they are
        ASSERT_EQ(to_json(JsonValue::make_string(lit("q\"b\\n\n r\r t\t bs\b ff\f nul\0 u\x01 us\x1f"))),
                  "\"q\\\"b\\\\n\\n r\\r t\\t bs\\b ff\\f nul\\u0000 u\\u0001 us\\u001f\"");
        ASSERT_EQ(to_json(JsonValue::make_string("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80")), "\"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"");     // UTF-8 goes out as it is
        ASSERT_EQ(to_json(JsonValue::make_string("\xE2\x80\xA8")), "\"\xE2\x80\xA8\"");                                                        // U+2028 too
        // invalid UTF-8 is replaced, so that what comes out is valid JSON; every bad byte is one U+FFFD
        ASSERT_EQ(to_json(JsonValue::make_string("a\xFF" "b")), "\"a\xEF\xBF\xBD" "b\"");
        ASSERT_EQ(to_json(JsonValue::make_string("\xE2\x82")), "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");
        ASSERT_EQ(to_json(JsonValue::make_string("\xED\xA0\x80")), "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");
        ASSERT_EQ(to_json(JsonValue::make_string("\xC0\x80")), "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");
        ASSERT_TRUE(parses(to_json(JsonValue::make_string("\xFF\xFE\x80\xC3"))));
        // containers: no white space, insertion order, empty ones
        ASSERT_EQ(to_json(JsonValue::make_array()), "[]");
        ASSERT_EQ(to_json(JsonValue::make_object()), "{}");
        ASSERT_EQ(to_json(parse_ok(" {\"b\" : [ 1 , { \"c\" : null } ] , \"a\" : \"d\" , \"e\" : { } , \"f\" : [ ] } ")), "{\"b\":[1,{\"c\":null}],\"a\":\"d\",\"e\":{},\"f\":[]}");
        JsonValue keyed = JsonValue::make_object();
        keyed.set("quote\"key\n", JsonValue::make_int(1));
        ASSERT_EQ(to_json(keyed), "{\"quote\\\"key\\n\":1}");                    // keys are escaped like strings
        // the same value always gives the same bytes
        const JsonValue v = parse_ok("{\"a\":[1,2,{\"b\":0.1}],\"c\":\"d\"}");
        ASSERT_EQ(to_json(v), to_json(v));
        ASSERT_EQ(to_json(v), "{\"a\":[1,2,{\"b\":0.1}],\"c\":\"d\"}");
    } TEST_END();

    TEST_CASE("CTL1.12 JSON serializer: doubles parse back to the same bits and stay doubles") {
        ASSERT_EQ(to_json(JsonValue::make_double(0.5)), "0.5");
        ASSERT_EQ(to_json(JsonValue::make_double(1.0)), "1.0");
        ASSERT_EQ(to_json(JsonValue::make_double(100.0)), "100.0");
        ASSERT_EQ(to_json(JsonValue::make_double(-0.0)), "-0.0");
        ASSERT_EQ(to_json(JsonValue::make_double(0.0)), "0.0");
        ASSERT_EQ(to_json(JsonValue::make_double(0.1)), "0.1");
        ASSERT_EQ(to_json(JsonValue::make_double(-2.5)), "-2.5");
        ASSERT_EQ(to_json(JsonValue::make_double(0.30000000000000004)), "0.30000000000000004");
        ASSERT_TRUE(to_json(JsonValue::make_double(1e21)).find('e') != std::string::npos);
        ASSERT_TRUE(to_json(JsonValue::make_double(1e-7)).find('e') != std::string::npos);
        // no JSON form: null
        ASSERT_EQ(to_json(JsonValue::make_double(std::numeric_limits<double>::quiet_NaN())), "null");
        ASSERT_EQ(to_json(JsonValue::make_double(std::numeric_limits<double>::infinity())), "null");
        ASSERT_EQ(to_json(JsonValue::make_double(-std::numeric_limits<double>::infinity())), "null");
        ASSERT_EQ(to_json(parse_ok("[1.0,1e0,1E2,-0.0,0.5e1]")), "[1.0,1.0,100.0,-0.0,5.0]");
        Rng r(0xD0D0);
        for (int i = 0; i < 20000; ++i) {
            const double d = random_double(r);
            const JsonValue v = JsonValue::make_double(d);
            const std::string text = to_json(v);
            JsonValue back;
            std::string e;
            ASSERT_MSG(parse_json(text, back, &e), "double text should parse: " + text);
            ASSERT_MSG(back.is_double(), "a double stays a double: " + text);
            ASSERT_MSG(back == v, "double changed in a round trip: " + text);
        }
    } TEST_END();

    TEST_CASE("CTL1.13 JSON serializer: nesting beyond 128 (only a program can build it) is cut, not a crash") {
        JsonValue v = JsonValue::make_int(1);
        for (int i = 0; i < 300; ++i) {
            JsonValue a = JsonValue::make_array();
            a.push_back(std::move(v));
            v = std::move(a);
        }
        const std::string text = to_json(v);
        ASSERT_TRUE(text.find("null") != std::string::npos);
        ASSERT_EQ(std::count(text.begin(), text.end(), '['), 128);
        ASSERT_TRUE(parses(text) == false);                                     // too deep for the parser's own limit, as it should be
        JsonValue obj = JsonValue::make_int(1);
        for (int i = 0; i < 300; ++i) {
            JsonValue o = JsonValue::make_object();
            o.set("a", std::move(obj));
            obj = std::move(o);
        }
        ASSERT_TRUE(to_json(obj).find("null") != std::string::npos);
        JsonLimits deep;
        deep.max_depth = 128;
        JsonValue back;
        ASSERT_TRUE(parse_json(nested_arrays(128), back, nullptr, deep));
        ASSERT_EQ(to_json(back), nested_arrays(128));                           // exactly 128 levels still print in full
    } TEST_END();

    TEST_CASE("CTL1.14 JSON: is_valid_utf8") {
        ASSERT_TRUE(is_valid_utf8(""));
        ASSERT_TRUE(is_valid_utf8("plain ascii"));
        ASSERT_TRUE(is_valid_utf8("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
        ASSERT_TRUE(is_valid_utf8("\xF4\x8F\xBF\xBF"));
        ASSERT_TRUE(is_valid_utf8(lit("with\0nul")));
        ASSERT_FALSE(is_valid_utf8("\x80"));
        ASSERT_FALSE(is_valid_utf8("\xC0\xAF"));
        ASSERT_FALSE(is_valid_utf8("\xED\xA0\x80"));
        ASSERT_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));
        ASSERT_FALSE(is_valid_utf8("\xE2\x82"));
        ASSERT_FALSE(is_valid_utf8("ok\xFF"));
        // every code point round trips through the encoder of the parser and is valid; neighbours of the surrogate range included
        for (uint32_t cp = 0; cp <= 0x10FFFF; cp += (cp < 0x1000 ? 1 : 97)) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            char buf[16];
            if (cp < 0x10000) std::snprintf(buf, sizeof(buf), "\"\\u%04x\"", cp);
            else std::snprintf(buf, sizeof(buf), "\"\\u%04x\\u%04x\"", 0xD800 + ((cp - 0x10000) >> 10), 0xDC00 + ((cp - 0x10000) & 0x3FF));
            JsonValue v;
            ASSERT_MSG(parse_json(buf, v, nullptr), std::string("escape should parse: ") + buf);
            ASSERT_MSG(is_valid_utf8(v.str()), std::string("decoded text is valid UTF-8: ") + buf);
            ASSERT_MSG(!v.str().empty(), buf);
        }
    } TEST_END();

    TEST_CASE("CTL1.14b JSON: numbers do not depend on the decimal point of the C locale") {
        const char* previous = std::setlocale(LC_NUMERIC, nullptr);
        const std::string saved = previous != nullptr ? previous : "C";
        bool comma = false;
        for (const char* name : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "fr_FR", "de_DE.utf8", "fr_FR.utf8"}) {
            if (std::setlocale(LC_NUMERIC, name) != nullptr && std::localeconv() != nullptr && std::strcmp(std::localeconv()->decimal_point, ",") == 0) {
                comma = true;
                break;
            }
        }
        if (!comma) {
            std::setlocale(LC_NUMERIC, saved.c_str());
            std::cout << "(no locale with a decimal comma here, skipped) " << std::flush;
            return;
        }
        JsonValue v;
        const bool parsed = parse_json("[1.5,-0.25,1e2,0.1]", v, nullptr);
        const std::string out = to_json(JsonValue::make_double(1234.5));
        const std::string out2 = to_json(parse_ok("[0.1,2.5e-3]"));
        std::setlocale(LC_NUMERIC, saved.c_str());
        ASSERT_TRUE(parsed);
        ASSERT_TRUE(v.at(0).as_double_or(0) == 1.5);
        ASSERT_TRUE(v.at(1).as_double_or(0) == -0.25);
        ASSERT_TRUE(v.at(3).as_double_or(0) == 0.1);
        ASSERT_EQ(out, "1234.5");
        ASSERT_EQ(out2, "[0.1,0.0025]");
    } TEST_END();

    // ============================================================================================
    // JSON: round trips and fuzzing
    // ============================================================================================

    TEST_CASE("CTL1.15 JSON: 30,000 random values survive to_json and parse_json bit for bit") {
        Rng r(0xC0FFEE);
        for (int i = 0; i < 30000; ++i) {
            const JsonValue v = random_value(r, 0);
            const std::string text = to_json(v);
            JsonValue back;
            std::string e;
            ASSERT_MSG(parse_json(text, back, &e), "serialized value should parse: " + shown(text) + "  (" + e + ")");
            ASSERT_MSG(back == v, "round trip changed the value: " + shown(text));
            ASSERT_MSG(to_json(back) == text, "serialization is not stable: " + shown(text));
            ASSERT_MSG(is_valid_utf8(text), "the text is valid UTF-8: " + shown(text));
            ASSERT_MSG(recognizes(text), "the independent recognizer refuses it: " + shown(text));
        }
    } TEST_END();

    TEST_CASE("CTL1.16 JSON fuzz: 200,000 random and mutated documents never crash or hang, and agree with an independent recognizer") {
        const std::vector<std::string> corpus = {
            "null", "true", "[]", "{}", "0", "-1.5e+10", "\"a\\u00e9\\ud83d\\ude00\\n\"", "[1,2,3]", "{\"a\":1}", "{\"a\":[1,{\"b\":null}],\"c\":\"d\"}",
            "[[[[1]]]]", "{\"k\":{\"k\":{\"k\":{}}}}", "[1.0,2e5,-0,0.5E-3,123456789012345678901234567890]", "\"\\\\\\/\\b\\f\\n\\r\\t\\\"\"",
            "{\"name\":\"Anthill\",\"seats\":4,\"open\":true,\"tags\":[\"a\",\"b\"],\"meta\":{\"x\":1.5,\"y\":null}}", lit("\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\""),
            " [ 1 , { \"a\" : [ ] } , \"b\" ] ", "[\"\\ud800\\udc00\",\"\\uffff\"]", "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9,\"j\":10,\"k\":11,\"l\":12,\"m\":13,\"n\":14,\"o\":15,\"p\":16,\"q\":17,\"r\":18}",
        };
        const std::string alphabet = "{}[]\",:0123456789.eE+-truefalsn\\u/ \t\n\r\x7f";
        Rng r(0xFEED5EED);
        int accepted = 0;
        int refused_but_grammatical = 0;
        size_t slowest_us = 0;
        for (int i = 0; i < 200000; ++i) {
            std::string doc;
            const uint32_t mode = r.below(10);
            if (mode < 3) {                                                     // random bytes, mostly JSON punctuation
                const uint32_t n = r.below(48);
                for (uint32_t k = 0; k < n; ++k) doc.push_back(r.chance(75) ? alphabet[r.below(static_cast<uint32_t>(alphabet.size()))] : static_cast<char>(r.below(256)));
            } else if (mode < 5) {                                              // a random valid value, serialized, then maybe damaged
                doc = to_json(random_value(r, 2));
                if (r.chance(50) && !doc.empty()) doc[r.below(static_cast<uint32_t>(doc.size()))] = alphabet[r.below(static_cast<uint32_t>(alphabet.size()))];
            } else {                                                            // a corpus document with a few mutations
                doc = corpus[r.below(static_cast<uint32_t>(corpus.size()))];
                const uint32_t edits = 1 + r.below(4);
                for (uint32_t e = 0; e < edits; ++e) {
                    const uint32_t op = r.below(7);
                    const uint32_t at = doc.empty() ? 0 : r.below(static_cast<uint32_t>(doc.size()));
                    switch (op) {
                        case 0: if (!doc.empty()) doc[at] = static_cast<char>(static_cast<unsigned char>(doc[at]) ^ (1u << r.below(8))); break;                // flip a bit
                        case 1: doc.insert(doc.begin() + at, alphabet[r.below(static_cast<uint32_t>(alphabet.size()))]); break;   // insert punctuation
                        case 2: doc.insert(doc.begin() + at, static_cast<char>(r.below(256))); break;                              // insert a random byte
                        case 3: if (!doc.empty()) doc.erase(at, 1 + r.below(4)); break;                                             // delete
                        case 4: if (!doc.empty()) doc.insert(at, doc.substr(at, 1 + r.below(8))); break;                            // repeat a piece
                        case 5: doc.resize(at); break;                                                                              // truncate
                        default: doc += corpus[r.below(static_cast<uint32_t>(corpus.size()))]; break;                               // concatenate
                    }
                }
            }
            const auto t0 = std::chrono::steady_clock::now();
            JsonValue v;
            std::string error;
            const bool ok = parse_json(doc, v, &error);
            const auto us = static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
            slowest_us = std::max(slowest_us, us);
            const bool grammatical = recognizes(doc);
            if (ok) {
                ++accepted;
                ASSERT_MSG(grammatical, "accepted but the independent recognizer refuses: " + shown(doc));
                ASSERT_MSG(error.empty(), "error text on success: " + shown(doc));
                const std::string out = to_json(v);
                JsonValue back;
                ASSERT_MSG(parse_json(out, back, nullptr), "serialized form of an accepted document fails: " + shown(doc) + " -> " + shown(out));
                ASSERT_MSG(back == v, "round trip of an accepted document changed it: " + shown(doc) + " -> " + shown(out));
                ASSERT_MSG(to_json(back) == out, "serialization not stable: " + shown(out));
            } else {
                ASSERT_MSG(v.is_null(), "out must be null after a refusal: " + shown(doc));
                ASSERT_MSG(!error.empty(), "an error text: " + shown(doc));
                if (grammatical) {                                              // only what the recognizer cannot know may refuse a grammatical text
                    ++refused_but_grammatical;
                    ASSERT_MSG(error.find("duplicate key") != std::string::npos || error.find("out of range") != std::string::npos,
                               "refused a grammatical document for the wrong reason: " + shown(doc) + "  (" + error + ")");
                }
            }
        }
        std::cout << "(accepted " << accepted << ", grammatical but refused " << refused_but_grammatical << ", slowest " << slowest_us << " us) " << std::flush;
        ASSERT_TRUE(accepted > 5000);                                           // the generator reaches deep into the grammar
        ASSERT_TRUE(refused_but_grammatical > 0);                               // and into the duplicate keys
        ASSERT_MSG(slowest_us < 2000000, "the slowest parse took " + std::to_string(slowest_us) + " us");
    } TEST_END();
}

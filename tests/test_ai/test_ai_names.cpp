// Tests of the names of the computer players (include/ants_ai/bot_names.hpp, docs/BOTS.md "Names"): the list, the picker with its golden vectors and the digests that the
// JavaScript twin (web/front/bot_names.js, run by tests/scripts/web_bot_names_check.js) must give too, the label and its parser, the short form and the reserved-name rule.
// The tables are files that both languages read: tests/data/bot_names_golden.txt, tests/data/bot_label_cases.tsv and tests/data/bot_names_blocklist.txt.
#include "ai_test.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

#include "ants_ai/bot_names.hpp"
#include "ants_ai/rng.hpp"

using namespace ai_test;
using namespace ants::ai;

namespace {

std::vector<std::string> lines_of(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back(line);
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i == 0 ? "" : sep) + v[i];
    return out;
}

std::string data_path(const char* name) { return std::string(TEST_DATA_DIR) + "/" + name; }

std::string lower_of(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

uint64_t hex_key(const std::string& h) { return std::stoull(h, nullptr, 16); }

// FNV-1a over bytes, 32 bits, two start values: the digest that tests/scripts/web_bot_names_check.js writes the same way
struct Fnv {
    uint32_t a{0x811C9DC5u};
    uint32_t b{0xC0FFEE11u};
    void add(const std::string& text) {
        for (const char c : text) {
            a = (a ^ static_cast<unsigned char>(c)) * 16777619u;
            b = (b ^ static_cast<unsigned char>(c)) * 16777619u;
        }
    }
    std::string text() const {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%08x %08x", a, b);
        return buf;
    }
};

std::string picks_digest() {
    Fnv f;
    for (uint64_t i = 0; i < 10000; ++i) {
        const uint64_t key = i % 2 == 0 ? i : mix64(i);
        f.add(join(pick_bot_names(key, static_cast<size_t>(i % 5)), ",") + ";");
    }
    return f.text();
}

std::string fuzz_digest() {
    static const char* const kTokens[] = {"Bot", "bot", " ", "(", ")", ",", ", ", "Hard", "hard", "Easy", "Medium", "idle", "Idle", "worker", "Worker", "Pip", "x", "BOT", "(bot", "(bot)", "(bot, ",
                                          " (bot, Hard)", "Bot (", "bot,", "Seventeen letters"};
    constexpr uint32_t kTokenCount = sizeof(kTokens) / sizeof(kTokens[0]);
    Fnv f;
    uint32_t s = 12345;
    const auto next = [&s]() {
        s = s * 1664525u + 1013904223u;
        return s >> 16;
    };
    for (int i = 0; i < 10000; ++i) {
        std::string text;
        const uint32_t n = 1 + (next() % 6);
        for (uint32_t k = 0; k < n; ++k) text += kTokens[next() % kTokenCount];
        BotLabel p;
        std::string line = parse_bot_label(text, p) ? p.name + "|" + p.tag + "|" + (p.legacy ? "1" : "0") : "FAIL";
        line += "\t" + bot_short_label(text) + "\t" + (is_reserved_bot_name(text) ? "1" : "0") + "\n";
        f.add(line);
    }
    return f.text();
}

/// the text of a column of bot_label_cases.tsv: <empty> is "", \xHH is a character
std::string unescape(const std::string& t) {
    if (t == "<empty>") return std::string();
    std::string out;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '\\' && i + 3 < t.size() && t[i + 1] == 'x') {
            out.push_back(static_cast<char>(std::stoi(t.substr(i + 2, 2), nullptr, 16)));
            i += 3;
        } else {
            out.push_back(t[i]);
        }
    }
    return out;
}

/// the twelve names that the page offers a visitor (web/front/lobby_rules.js NAMES)
std::vector<std::string> web_placeholder_names() {
    std::ifstream in(std::string(ANTS_SOURCE_DIR) + "/web/front/lobby_rules.js", std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    const std::string text = all.str();
    const size_t at = text.find("var NAMES = [");
    std::vector<std::string> out;
    if (at == std::string::npos) return out;
    const size_t end = text.find(']', at);
    for (const std::string& piece : split(text.substr(at + 13, end - at - 13), ',')) {
        const size_t a = piece.find('\'');
        const size_t b = piece.rfind('\'');
        if (a != std::string::npos && b > a) out.push_back(piece.substr(a + 1, b - a - 1));
    }
    return out;
}

std::string render(const BotLabel& p) { return p.name + "|" + p.tag + "|" + (p.legacy ? "1" : "0"); }

}  // namespace

void run_name_tests() {
    TEST_CASE("AI2.40 Names: The List Is 240 To 260 Short ASCII Names, Unique In Any Case, Not The Page's Twelve, None On The Blocklist") {
        const size_t n = bot_name_count();
        ASSERT_TRUE(n >= 240 && n <= 260);
        ASSERT_EQ(std::string(bot_name_at(n)), std::string());
        ASSERT_EQ(std::string(bot_name_at(100000)), std::string());
        std::set<std::string> seen;
        const std::vector<std::string> web = web_placeholder_names();
        ASSERT_EQ(web.size(), size_t{12});
        const std::vector<std::string> block = lines_of(data_path("bot_names_blocklist.txt"));
        ASSERT_TRUE(block.size() >= 100);
        for (size_t i = 0; i < n; ++i) {
            const std::string name = bot_name_at(i);
            ASSERT_TRUE(name.size() >= 3 && name.size() <= 9);
            ASSERT_TRUE(std::isupper(static_cast<unsigned char>(name[0])) != 0);
            for (size_t k = 0; k < name.size(); ++k) {
                if (k > 0) ASSERT_TRUE(name[k] >= 'a' && name[k] <= 'z');
            }
            ASSERT_TRUE(seen.insert(lower_of(name)).second);                                         // (unique in any case)
            ASSERT_TRUE(std::find(web.begin(), web.end(), name) == web.end());
            ASSERT_TRUE(valid_bot_label_name(name));
            for (const std::string& w : block) {
                const std::string l = lower_of(name);
                if (w[0] == '=') ASSERT_TRUE(l != w.substr(1));
                else ASSERT_TRUE(l.find(w) == std::string::npos);
            }
        }
    });

    TEST_CASE("AI2.41 Names: pick_bot_names Gives Distinct Names, The First Ones Of A Longer Pick, The Same Names For The Same Key, And The Golden Vectors Of The JavaScript Twin") {
        ASSERT_TRUE(pick_bot_names(7, 0).empty());
        ASSERT_EQ(pick_bot_names(7, 1).size(), size_t{1});
        std::vector<size_t> first(bot_name_count(), 0);
        for (uint64_t i = 0; i < 2000; ++i) {
            const uint64_t key = i * 7919;
            const std::vector<std::string> four = pick_bot_names(key, 4);
            ASSERT_EQ(four.size(), size_t{4});
            ASSERT_EQ(std::set<std::string>(four.begin(), four.end()).size(), size_t{4});
            ASSERT_TRUE(pick_bot_names(key, 2) == std::vector<std::string>(four.begin(), four.begin() + 2));
            ASSERT_TRUE(pick_bot_names(key, 3) == std::vector<std::string>(four.begin(), four.begin() + 3));
            ASSERT_TRUE(pick_bot_names(key, 4) == four);
            for (size_t k = 0; k < bot_name_count(); ++k) {
                if (four[0] == bot_name_at(k)) ++first[k];
            }
        }
        size_t reached = 0;
        size_t most = 0;
        for (const size_t c : first) {
            reached += c > 0 ? 1u : 0u;
            most = std::max(most, c);
        }
        ASSERT_TRUE(reached * 10 >= bot_name_count() * 9);          // (the first name of 2,000 keys reaches 90% of the list, none is far above the average of 8)
        ASSERT_TRUE(most <= 30);
        // the golden file: the same vectors and digests as the JavaScript twin gives
        size_t vectors = 0;
        std::string picks, fuzz;
        for (const std::string& line : lines_of(data_path("bot_names_golden.txt"))) {
            const std::vector<std::string> f = split(line, ' ');
            if (f[0] == "digest") {
                (f[1] == "picks" ? picks : fuzz) = f[3] + " " + f[4];
            } else if (f[0] == "pick") {
                ASSERT_TRUE(pick_bot_names(hex_key(f[1]), static_cast<size_t>(std::stoul(f[2]))) == split(f[3], ','));
                ++vectors;
            }
        }
        ASSERT_EQ(vectors, size_t{10});
        ASSERT_EQ(picks_digest(), picks);          // 10,000 keys through this implementation and through the JavaScript one: the same digest
        ASSERT_EQ(fuzz_digest(), fuzz);
        // (last: a picker that cannot reach the whole list would loop here)
        {   // more than the list holds: the whole list once each
            const std::vector<std::string> all = pick_bot_names(42, bot_name_count() + 50);
            ASSERT_EQ(all.size(), bot_name_count());
            ASSERT_EQ(std::set<std::string>(all.begin(), all.end()).size(), bot_name_count());
        }
    });

    TEST_CASE("AI2.42 Names: bot_label, parse_bot_label, bot_short_label And is_reserved_bot_name Answer The Table That The JavaScript Twin Answers") {
        size_t cases = 0;
        for (const std::string& line : lines_of(data_path("bot_label_cases.tsv"))) {
            std::vector<std::string> f = split(line, '\t');
            for (std::string& c : f) c = unescape(c);
            ++cases;
            if (f[0] == "label") {
                ASSERT_EQ(bot_label(f[1], f[2]), f[3]);
            } else if (f[0] == "parse") {
                BotLabel p;
                p.name = "kept";
                const bool ok = parse_bot_label(f[1], p);
                ASSERT_EQ(ok ? render(p) : std::string("FAIL"), f[2]);
                if (!ok) ASSERT_EQ(p.name, std::string("kept"));                                       // (a refusal leaves `out` as it was)
            } else if (f[0] == "short") {
                ASSERT_EQ(bot_short_label(f[1]), f[2]);
            } else if (f[0] == "reserved") {
                ASSERT_EQ(is_reserved_bot_name(f[1]) ? std::string("1") : std::string("0"), f[2]);
            } else {
                ASSERT_TRUE(false);
            }
        }
        ASSERT_TRUE(cases >= 100);
        // the overloads: a level, and the spec of a seat (the kinds idle and worker by their kind, every other kind by its level)
        ASSERT_EQ(bot_label("Pip", Level::Easy), std::string("Pip (bot, Easy)"));
        ASSERT_EQ(bot_label("Pip", Level::Medium), std::string("Pip (bot, Medium)"));
        ASSERT_EQ(bot_label("Pip", Level::Hard), std::string("Pip (bot, Hard)"));
        BotSpec spec;
        spec.kind = "standard";
        spec.level = Level::Hard;
        ASSERT_EQ(bot_label("Pip", spec), std::string("Pip (bot, Hard)"));
        spec.kind = "standard+x";
        spec.level = Level::Easy;
        ASSERT_EQ(bot_label("Pip", spec), std::string("Pip (bot, Easy)"));
        spec.kind = "idle";
        spec.level = Level::Hard;
        ASSERT_EQ(bot_label("Pip", spec), std::string("Pip (bot, idle)"));
        spec.kind = "worker";
        ASSERT_EQ(bot_label("Pip", spec), std::string("Pip (bot, worker)"));
        ASSERT_EQ(bot_label("", spec), std::string("Bot (Worker)"));
        // the legacy labels are exactly what bot_display_name says today, and are read and reserved
        for (const char* kind : {"standard", "idle", "worker"}) {
            for (const Level l : {Level::Easy, Level::Medium, Level::Hard}) {
                BotSpec s;
                s.kind = kind;
                s.level = l;
                const std::string legacy = bot_display_name(s);
                ASSERT_EQ(bot_label("", s), legacy);
                BotLabel p;
                ASSERT_TRUE(parse_bot_label(legacy, p) && p.legacy && p.name.empty());
                ASSERT_TRUE(is_reserved_bot_name(legacy));
            }
        }
        // every name of the list with every tag: read back, reserved, and short enough (the label for a player name's 32 characters, the short form for the HUD's 15)
        for (size_t i = 0; i < bot_name_count(); ++i) {
            const std::string name = bot_name_at(i);
            for (const char* tag : {"easy", "medium", "hard", "idle", "worker"}) {
                const std::string label = bot_label(name, tag);
                BotLabel p;
                ASSERT_TRUE(parse_bot_label(label, p) && p.name == name && p.tag == tag && !p.legacy);
                ASSERT_TRUE(is_reserved_bot_name(label) && is_reserved_bot_name(bot_short_label(label)));
                ASSERT_TRUE(label.size() <= 32 && bot_short_label(label).size() <= 15);
            }
            ASSERT_FALSE(is_reserved_bot_name(name));
        }
    });
}

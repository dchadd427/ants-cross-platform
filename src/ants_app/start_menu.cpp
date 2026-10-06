#include "ants_app/start_menu.hpp"

#include <algorithm>
#include <cctype>

#include "ants_net/protocol.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

static_assert(StartMenu::kNameMax == net::kMaxNameChars, "the name field holds what a Hello carries");
static_assert(StartMenu::kCodeMax == net::kMaxRoomCodeChars, "the code field holds what a Hello carries");

namespace {

// ---- text helpers -------------------------------------------------------------------------------------------------------------------------------------

bool printable_char(char c) noexcept { return c >= 0x20 && c <= 0x7E; }

// The printable ASCII of a text. `refused` is set when something else was dropped that a person would miss: an accent, a CJK character, a control character (a line end or a tab, which a
// paste brings, is dropped silently)
std::string printable_text(const std::string& text, bool& refused) {
    std::string out;
    for (char c : text) {
        if (printable_char(c)) out.push_back(c);
        else if (c != '\n' && c != '\r' && c != '\t') refused = true;
    }
    return out;
}

std::string trim_blanks(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

char lower_char(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lower_text(std::string s) {
    for (char& c : s) c = lower_char(c);
    return s;
}

// The six maps of the original game, in the order of web/lobby.html (by size)
constexpr MenuMap kMaps[kMenuMapCount] = {
    {"tiny", "Tiny"}, {"small", "Small"}, {"medium", "Medium"}, {"gauntlet", "Gauntlet"}, {"treasure", "Treasure"}, {"islands", "Islands"},
};

constexpr bool same_word(const char* a, const char* b) noexcept {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static_assert(kDefaultMenuMap >= 0 && static_cast<size_t>(kDefaultMenuMap) < kMenuMapCount && same_word(kMaps[kDefaultMenuMap].key, "treasure"), "the Host panel's default map is Treasure");

bool host_char(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '-' || c == '_';
}

bool ipv6_text(const std::string& host) noexcept {
    size_t colons = 0;
    for (char c : host) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (c == ':') ++colons;
        else if (!hex && c != '.') return false;
    }
    return colons >= 2 && host.size() <= 45;
}

// The words of the setting `bots`
const char* seat_choice_word(SeatChoice choice) noexcept {
    switch (choice) {
        case SeatChoice::Easy: return "easy";
        case SeatChoice::Medium: return "medium";
        case SeatChoice::Hard: return "hard";
        case SeatChoice::Empty: break;
    }
    return "off";
}

SeatChoice seat_choice_of(const std::string& word) noexcept {
    const std::string w = lower_text(word);
    if (w == "easy") return SeatChoice::Easy;
    if (w == "medium") return SeatChoice::Medium;
    if (w == "hard") return SeatChoice::Hard;
    return SeatChoice::Empty;                              // "off", and anything that is not a level
}

ai::Level level_of(SeatChoice choice) noexcept {
    switch (choice) {
        case SeatChoice::Easy: return ai::Level::Easy;
        case SeatChoice::Hard: return ai::Level::Hard;
        case SeatChoice::Medium:
        case SeatChoice::Empty: break;
    }
    return ai::Level::Medium;
}

// ---- the geometry ---------------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kButtonX = 160;
constexpr int32_t kButtonW = 320;
constexpr int32_t kButtonH = 46;
constexpr int32_t kSeatRowH = 44;                  // a seat row of the single-player panel (portrait, colour, cycler): the panel also holds the name and, with two bots, the Teams row
constexpr int32_t kRejoinX = 100;                  // the first panel's "Rejoin your match (CODE)": in the gap between the title and Single player (nothing else of the panel moves), wider than the others for the room's code
constexpr int32_t kRejoinY = 78;
constexpr int32_t kRejoinW = 440;
constexpr int32_t kRejoinH = 34;
constexpr int32_t kHintY = 446;
constexpr int32_t kHintH = 14;
constexpr int32_t kTextX = 60;
constexpr int32_t kTextW = 520;
constexpr const char* kFillCaption = "Bots gather food, raid and fight back.";     // what the standard bot does (docs/BOTS.md "The standard bot"), under the seat rows here and on web/lobby.html

// The Host panel is a column of rows (a label at x = 60, a 380 px control at x = 200, 27 px high at a pitch of 29, 18 px text): the map, the players, a row for each seat after the leader's and, from three
// players, the teams. What follows them (the caption, the name, Host, Back, the note or the error) moves with them, so every state fits the frame without a gap.
constexpr int32_t kHostTop = 78;
constexpr int32_t kHostLabelX = 60;
constexpr int32_t kHostLabelW = 134;
constexpr int32_t kHostControlX = 200;
constexpr int32_t kHostControlW = 380;
constexpr int32_t kHostRowH = 27;
constexpr int32_t kHostRowPitch = 29;
// What the room's panel says about the seats in one line (520 px of the 18 px text): the long form up to this many characters, else the short one
constexpr size_t kSentenceChars = 56;

// The row of a seat's choice on the Host panel: HostSeat1 stands where the old single choice stood (the numbers of the controls after it are in the golden pointer fingerprints of the wide pages)
MenuId host_seat_id(size_t seat) noexcept {
    return seat == 1 ? MenuId::HostSeat1 : (seat == 2 ? MenuId::HostSeat2 : MenuId::HostSeat3);
}

bool host_seat_of(MenuId id, size_t& seat) noexcept {
    switch (id) {
        case MenuId::HostSeat1: seat = 1; return true;
        case MenuId::HostSeat2: seat = 2; return true;
        case MenuId::HostSeat3: seat = 3; return true;
        default: return false;
    }
}

// The teams of the stored choice that a room of `players` offers (free for all when it does not)
sim::StartTeams offered_teams(const sim::StartTeams& stored, int players) {
    const std::vector<sim::StartTeams> choices = sim::room_team_choices(static_cast<uint8_t>(std::clamp(players, 2, 4)));
    return std::find(choices.begin(), choices.end(), stored) != choices.end() ? stored : sim::StartTeams{};
}

ButtonRect centred_button(int32_t y, int32_t h = kButtonH) { return ButtonRect{kButtonX, y, kButtonW, h}; }

}  // anonymous namespace

// ---- servers, codes, names --------------------------------------------------------------------------------------------------------------------------

bool parse_server(const std::string& text_in, ServerAddress& out, std::string& why) {
    const std::string text = trim_blanks(text_in);
    if (text.empty()) {
        why = "The server is empty: write HOST or HOST:PORT, for example beta.playants.org:4001.";
        return false;
    }
    for (char c : text) {
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) > 0x7E) {
            why = "The server '" + text + "' holds a blank or a character that cannot be part of an address.";
            return false;
        }
    }
    std::string host;
    std::string port_text;
    bool bracketed = false;
    if (text[0] == '[') {                                      // [IPv6] or [IPv6]:PORT
        const size_t close = text.find(']');
        if (close == std::string::npos) {
            why = "The server '" + text + "' has a [ without its ].";
            return false;
        }
        bracketed = true;
        host = text.substr(1, close - 1);
        const std::string rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (rest[0] != ':' || rest.size() == 1) {
                why = "The server '" + text + "': after ] only :PORT may follow.";
                return false;
            }
            port_text = rest.substr(1);
        }
    } else {
        const size_t first = text.find(':');
        if (first == std::string::npos) {
            host = text;
        } else if (text.find(':', first + 1) == std::string::npos) {
            host = text.substr(0, first);
            port_text = text.substr(first + 1);
            if (port_text.empty()) {
                why = "The server '" + text + "' has a : without a port.";
                return false;
            }
        } else {
            host = text;                                       // several colons: an IPv6 address without a port
        }
    }
    if (bracketed || host.find(':') != std::string::npos) {
        if (!ipv6_text(host)) {
            why = "The server '" + text + "': '" + host + "' is not an IPv6 address.";
            return false;
        }
    } else {
        if (host.empty() || host.size() > 253 || host.front() == '.' || host.front() == '-' || host.find("..") != std::string::npos) {
            why = "The server '" + text + "' has no usable host name.";
            return false;
        }
        for (char c : host) {
            if (!host_char(c)) {
                why = "The server '" + text + "': a host name has letters, digits, '.', '-' and '_' only.";
                return false;
            }
        }
    }
    uint16_t port = kDefaultServerPort;
    if (!port_text.empty()) {
        uint32_t value = 0;
        for (char c : port_text) {
            if (c < '0' || c > '9' || value > 65535u) {                   // not a digit, or too big already: a long text of digits never wraps round
                value = 0xFFFFFFu;
                break;
            }
            value = value * 10u + static_cast<uint32_t>(c - '0');
        }
        if (value < 1 || value > 65535) {
            why = "The server '" + text + "': the port must be a number from 1 to 65535.";
            return false;
        }
        port = static_cast<uint16_t>(value);
    }
    out.host = host;
    out.port = port;
    return true;
}

std::string server_label(const ServerAddress& server) {
    const bool v6 = server.host.find(':') != std::string::npos;
    return (v6 ? "[" + server.host + "]" : server.host) + ":" + std::to_string(server.port);
}

const char* seat_choice_text(SeatChoice choice) noexcept {
    switch (choice) {
        case SeatChoice::Empty: return "Empty";
        case SeatChoice::Easy: return "Easy bot";
        case SeatChoice::Medium: return "Medium bot";
        case SeatChoice::Hard: return "Hard bot";
    }
    return "";
}

const char* fill_choice_caption() noexcept { return kFillCaption; }

const char* fill_seat_text(net::FillLevel level) noexcept {
    switch (level) {
        case net::FillLevel::None: return "Leave empty";
        case net::FillLevel::Easy: return "Easy bot";
        case net::FillLevel::Medium: return "Medium bot";
        case net::FillLevel::Hard: return "Hard bot";
    }
    return "";
}

std::string fill_choice_sentence(const net::FillPlan& plan, int players) {
    const int count = std::clamp(players, 2, 4);
    std::vector<std::pair<uint8_t, net::FillLevel>> seats;                  // the seats that get a bot (the leader has seat 0)
    bool same = true;
    for (int seat = 1; seat < count; ++seat) {
        const net::FillLevel level = plan.level[static_cast<size_t>(seat)];
        if (level != net::FillLevel::None) seats.emplace_back(static_cast<uint8_t>(seat), level);
        same = same && level == plan.level[1];
    }
    if (seats.empty()) return "Empty seats stay empty.";
    if (same) return "Empty seats will be " + net::fill_level_title(plan.level[1]) + " bots.";
    const std::string full = "At START: " + net::fill_seats_sentence(seats) + ".";
    return full.size() <= kSentenceChars ? full : "At START: " + net::fill_seats_short(seats) + ".";
}

std::string room_teams_sentence(const LocalTeams& teams, int players) {
    const sim::StartTeams chosen = offered_teams(teams, players);
    if (!chosen.set) return std::string();
    return "Room teams: " + StartMenu::host_teams_text(chosen, players) + ".";
}

const MenuMap& menu_map(size_t index) noexcept { return kMaps[index < kMenuMapCount ? index : static_cast<size_t>(kDefaultMenuMap)]; }

int menu_map_index(const std::string& key) noexcept {
    const std::string k = lower_text(key);
    for (size_t i = 0; i < kMenuMapCount; ++i) {
        if (k == kMaps[i].key) return static_cast<int>(i);
    }
    return -1;
}

std::string make_room_code(const std::function<uint32_t()>& random) {
    std::string code;
    const size_t alphabet = std::char_traits<char>::length(kRoomCodeAlphabet);
    for (size_t i = 0; i < kRoomCodeChars; ++i) code.push_back(kRoomCodeAlphabet[random() % alphabet]);
    return code;
}

std::string room_code_display(const std::string& code) {
    if (code.size() != kRoomCodeChars) return code;
    return code.substr(0, kRoomCodeChars / 2) + " " + code.substr(kRoomCodeChars / 2);
}

net::CreateBlock make_create_block(const MenuMap& map, int players, const LocalTeams& teams, bool leader_starts) {
    net::CreateBlock block;
    for (const char* c = map.key; *c != '\0'; ++c) block.map_name.push_back(static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 'a' + 'A' : *c));
    block.map_name += ".LVL";
    block.seats = static_cast<uint8_t>(std::clamp(players, 2, 4));
    block.set_teams(offered_teams(teams, block.seats));                          // (only what a room of this many players offers: a pair that it cannot make is left out)
    if (leader_starts) block.flags = static_cast<uint8_t>(block.flags | net::kCreateLeaderStarts);
    return block;
}

std::string clean_player_name(const std::string& raw) {
    std::string printable;
    for (char c : raw) {
        if (printable_char(c)) printable.push_back(c);
    }
    std::string name = trim_blanks(printable);
    if (name.size() > StartMenu::kNameMax) name = trim_blanks(name.substr(0, StartMenu::kNameMax));
    return name;
}

bool looks_like_bot_name(const std::string& name) {
    static const char kMark[] = "bot(";
    size_t matched = 0;
    for (const char raw : name) {
        if (raw == ' ') continue;
        if (lower_char(raw) != kMark[matched]) return false;
        if (++matched == sizeof(kMark) - 1) return true;
    }
    return false;
}

bool check_player_name(const std::string& raw, std::string& clean, std::string& why) {
    clean = clean_player_name(raw);
    if (clean.empty()) {
        why = "Type your name first.";
        return false;
    }
    if (looks_like_bot_name(clean)) {
        why = "A name that starts with \"Bot (\" is for computer players. Please choose another name.";
        return false;
    }
    return true;
}

bool check_room_code(const std::string& raw, std::string& clean, std::string& why) {
    std::string printable;
    for (char c : raw) {
        if (printable_char(c)) printable.push_back(c);
    }
    clean = trim_blanks(printable);
    clean.erase(std::remove(clean.begin(), clean.end(), ' '), clean.end());           // (a code has no blank: the screens show it in groups of four, and what was copied from there comes back as it was)
    if (clean.empty()) {
        why = "Type the room code first.";
        return false;
    }
    if (clean.size() > StartMenu::kCodeMax) {
        why = "A room code has at most 32 characters.";
        return false;
    }
    if (!net::valid_room_code(clean)) {
        why = "A room code has letters, digits, - and _ only.";
        return false;
    }
    return true;
}

// ---- the remembered values -------------------------------------------------------------------------------------------------------------------------

void MenuSettings::load(const ConfigStore& store) {
    name = clean_player_name(store.get_string(kKeyName, std::string(), StartMenu::kNameMax));
    server = trim_blanks(store.get_string(kKeyServer, std::string(), 255));
    seats = {};
    const std::string bots = store.get_string(kKeyBots, std::string(), 64);
    size_t from = 0;
    for (size_t s = 0; s < seats.size() && from <= bots.size(); ++s) {
        const size_t comma = bots.find(',', from);
        seats[s] = seat_choice_of(trim_blanks(bots.substr(from, comma == std::string::npos ? std::string::npos : comma - from)));
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    const int map = menu_map_index(store.get_string(kKeyHostMap, std::string(), 16));
    host_map = map >= 0 ? map : kDefaultMenuMap;                                    // (nothing stored, or anything that is not one of the six words: Treasure; a stored map wins)
    host_players = store.get_int(kKeyHostPlayers, 4, 2, 5);                         // 2 <= value < 5
    host_fill = net::FillPlan{};                                                    // (anything that is not one word or four words of the levels is the default)
    std::string why;
    {
        const std::string stored = store.get_string(kKeyHostFill, std::string(), 64);
        std::string words;                                                          // (blanks around the words of a hand-written file are cut)
        size_t word_at = 0;
        for (;;) {
            const size_t comma = stored.find(',', word_at);
            words += trim_blanks(stored.substr(word_at, comma == std::string::npos ? std::string::npos : comma - word_at));
            if (comma == std::string::npos) break;
            words += ',';
            word_at = comma + 1;
        }
        net::FillPlan plan;
        if (net::parse_fill_plan(words, plan, why)) host_fill = plan;
        host_fill.level[0] = net::FillLevel::None;                                  // (seat 0 is the leader's; an old file's one word is the three other seats)
    }
    host_teams = LocalTeams{};                                                      // (anything that is not ffa or a pair of seats is free for all)
    parse_local_teams(trim_blanks(store.get_string(kKeyHostTeams, std::string(), 8)), host_teams, why);
    teams = LocalTeams{};                                                           // (the same for the single-player panel's choice)
    parse_local_teams(trim_blanks(store.get_string(kKeyTeams, std::string(), 8)), teams, why);
}

void MenuSettings::write(ConfigStore& store, MenuSetting setting) const {
    switch (setting) {
        case MenuSetting::Name:
            store.set_string(kKeyName, name);
            break;
        case MenuSetting::Bots: {
            std::string value;
            for (size_t s = 0; s < seats.size(); ++s) value += (s == 0 ? "" : ",") + std::string(seat_choice_word(seats[s]));
            store.set_string(kKeyBots, value);
            break;
        }
        case MenuSetting::HostMap:
            store.set_string(kKeyHostMap, menu_map(static_cast<size_t>(std::clamp(host_map, 0, static_cast<int>(kMenuMapCount) - 1))).key);
            break;
        case MenuSetting::HostPlayers:
            store.set_int(kKeyHostPlayers, std::clamp(host_players, 2, 4));
            break;
        case MenuSetting::HostFill:
            store.set_string(kKeyHostFill, net::fill_plan_text(host_fill));
            break;
        case MenuSetting::Teams:
            store.set_string(kKeyTeams, local_teams_text(teams));
            break;
        case MenuSetting::HostTeams:
            store.set_string(kKeyHostTeams, local_teams_text(host_teams));
            break;
    }
}

// ---- the menu ---------------------------------------------------------------------------------------------------------------------------------------------

StartMenu::StartMenu() {
    settings_.host_map = kDefaultMenuMap;
    settings_.host_players = 4;
}

void StartMenu::set_own_seat(uint8_t seat) noexcept { own_seat_ = seat < 4 ? seat : uint8_t{0}; }

void StartMenu::set_settings(const MenuSettings& settings) {
    settings_ = settings;
    settings_.host_map = std::clamp(settings_.host_map, 0, static_cast<int>(kMenuMapCount) - 1);
    settings_.host_players = std::clamp(settings_.host_players, 2, 4);
    settings_.host_fill.level[0] = net::FillLevel::None;                    // (the leader's seat gets no bot)
    name_ = clean_player_name(settings_.name);
    settings_.name = name_;
}

void StartMenu::set_clipboard(std::function<std::string()> get, std::function<bool(const std::string&)> set) {
    clipboard_get_ = std::move(get);
    clipboard_set_ = std::move(set);
}

void StartMenu::notify(MenuSetting setting) {
    if (on_change_) on_change_(setting);
}

void StartMenu::play(uint32_t sound_id) {
    if (on_play_sfx_) on_play_sfx_(sound_id);
}

bool StartMenu::any_bot() const noexcept {
    for (size_t s = 0; s < settings_.seats.size(); ++s) {
        if (s != own_seat_ && settings_.seats[s] != SeatChoice::Empty) return true;
    }
    return false;
}

std::vector<ai::BotSpec> StartMenu::bots() const {
    std::vector<ai::BotSpec> out;
    for (size_t s = 0; s < settings_.seats.size(); ++s) {
        if (s == own_seat_ || settings_.seats[s] == SeatChoice::Empty) continue;
        ai::BotSpec spec;
        spec.seat = static_cast<uint8_t>(s);
        spec.kind = "standard";
        spec.level = level_of(settings_.seats[s]);
        out.push_back(spec);
    }
    return out;
}

std::vector<LocalTeams> StartMenu::team_choices() const {
    uint8_t filled = 0;
    for (size_t s = 0; s < settings_.seats.size(); ++s) {
        if (s != own_seat_ && settings_.seats[s] != SeatChoice::Empty) filled = static_cast<uint8_t>(filled | (1u << s));
    }
    return local_team_choices(own_seat_, filled);
}

LocalTeams StartMenu::teams() const {
    const std::vector<LocalTeams> choices = team_choices();
    return std::find(choices.begin(), choices.end(), settings_.teams) != choices.end() ? settings_.teams : LocalTeams{};
}

std::string StartMenu::teams_text(const LocalTeams& teams, uint8_t own_seat) {
    if (!teams.set) return "Free for all";
    const uint8_t other = teams.a == own_seat ? teams.b : teams.a;
    return std::string("You + ") + sim::strings::colour_name(static_cast<uint8_t>(3u - other));
}

net::FillPlan StartMenu::host_fill() const {
    net::FillPlan plan;
    const size_t players = static_cast<size_t>(std::clamp(settings_.host_players, 2, 4));
    for (size_t seat = 1; seat < players; ++seat) plan.level[seat] = settings_.host_fill.level[seat];
    return plan;
}

std::vector<LocalTeams> StartMenu::host_team_choices() const {
    return sim::room_team_choices(static_cast<uint8_t>(std::clamp(settings_.host_players, 2, 4)));
}

LocalTeams StartMenu::host_teams() const { return offered_teams(settings_.host_teams, settings_.host_players); }

std::string StartMenu::host_teams_text(const LocalTeams& teams, int players) {
    const int count = std::clamp(players, 2, 4);
    return sim::start_teams_title(teams, static_cast<uint8_t>((1u << count) - 1u));
}

std::vector<size_t> StartMenu::other_seats() const {
    std::vector<size_t> seats;
    for (size_t s = 0; s < 4; ++s) {
        if (s != own_seat_) seats.push_back(s);
    }
    return seats;
}

MenuRequest StartMenu::take_request() {
    MenuRequest r = std::move(request_);
    request_ = MenuRequest{};
    return r;
}

void StartMenu::request(MenuRequest::Type type) {
    request_ = MenuRequest{};
    request_.type = type;
}

// ---- what happens to the menu ---------------------------------------------------------------------------------------------------------------------

void StartMenu::go(MenuPanel panel) {
    flush();                                                    // (a name that was typed on the panel that is left)
    panel_ = panel;
    pressed_ = MenuId::None;
    all_selected_ = false;
    caret_since_ms_ = elapsed_ms_;
    panel_since_ms_ = elapsed_ms_;
    select_first();
}

// What a new panel preselects is the first INPUT, never a button that acts: an Enter that is pressed twice, or a key that is held, must not act on the panel that the first one opened
// (Single player's Continue started the game, the Host panel's Host made a room, the Connecting panel's Cancel gave up the attempt). The Room panel's way on is the one thing to do there.
void StartMenu::select_first() {
    switch (panel_) {
        case MenuPanel::Main: selected_ = main_selection_; break;
        case MenuPanel::Single: {
            const std::vector<size_t> seats = other_seats();
            selected_ = static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + seats.front());
            break;
        }
        case MenuPanel::Join:                                   // the name when it is empty, else the code, with the text that is there (a code of an earlier room) selected: typing replaces it
            selected_ = name_.empty() ? MenuId::Name : MenuId::Code;
            all_selected_ = !field_text(selected_)->empty();
            break;
        case MenuPanel::Host: selected_ = MenuId::HostMap; break;
        case MenuPanel::Connecting: selected_ = MenuId::None; break;       // Esc cancels, Tab or the pointer reaches Cancel
        case MenuPanel::Room: selected_ = MenuId::EnterRoom; break;
    }
}

void StartMenu::flush() {
    if (!name_dirty_) return;
    name_dirty_ = false;
    settings_.name = name_;
    notify(MenuSetting::Name);
}

void StartMenu::name_changed() {
    settings_.name = name_;
    name_dirty_ = true;
}

// The first panel's offer to take the seat in a running match again. The selection that stood on the button is not left on a button that is gone.
void StartMenu::set_rejoin(std::optional<RejoinOffer> offer) {
    rejoin_ = std::move(offer);
    if (!rejoin_ && main_selection_ == MenuId::Rejoin) main_selection_ = MenuId::Single;
    if (!rejoin_ && selected_ == MenuId::Rejoin) selected_ = MenuId::Single;
}

void StartMenu::show_main(const std::string& notice) {
    message_ = notice;
    message_tone_ = MenuTone::Normal;
    go(MenuPanel::Main);
}

void StartMenu::connection_failed(const std::string& message) {
    message_ = message;
    message_tone_ = MenuTone::Bad;
    if (origin_ == MenuPanel::Main) {                           // a Rejoin that failed: back at the first panel, with the reason
        go(MenuPanel::Main);
        return;
    }
    const MenuPanel to = origin_ == MenuPanel::Host ? MenuPanel::Host : MenuPanel::Join;
    go(to);
    selected_ = to == MenuPanel::Host ? MenuId::Host : MenuId::Join;
    all_selected_ = false;
}

void StartMenu::connection_cancelled() {
    message_.clear();
    if (origin_ == MenuPanel::Main) {
        go(MenuPanel::Main);
        return;
    }
    const MenuPanel to = origin_ == MenuPanel::Host ? MenuPanel::Host : MenuPanel::Join;
    go(to);
    selected_ = to == MenuPanel::Host ? MenuId::Host : MenuId::Join;
    all_selected_ = false;
}

void StartMenu::show_room(const std::string& code, int players_in, int capacity) {
    room_code_ = code;
    room_players_ = players_in;
    room_capacity_ = capacity;
    copied_ms_ = 0.0;
    message_.clear();
    go(MenuPanel::Room);
}

void StartMenu::set_room_players(int players_in, int capacity) {
    room_players_ = players_in;
    room_capacity_ = capacity;
}

void StartMenu::room_left(const std::string& message) {
    message_ = message;
    message_tone_ = MenuTone::Bad;
    room_code_.clear();
    go(MenuPanel::Host);
}

void StartMenu::update(float dt_seconds) {
    elapsed_ms_ += static_cast<double>(dt_seconds) * 1000.0;
    if (copied_ms_ > 0.0) {
        copied_ms_ += static_cast<double>(dt_seconds) * 1000.0;
        if (copied_ms_ >= static_cast<double>(kCopiedMs)) copied_ms_ = 0.0;
    }
}

// ---- the elements -------------------------------------------------------------------------------------------------------------------------------------

MenuElement StartMenu::control(MenuId id, MenuKind kind, ButtonRect rect, const std::string& text, FontSize font) const {
    MenuElement e;
    e.id = id;
    e.kind = kind;
    e.rect = rect;
    e.text = text;
    e.font = font;
    e.centered = kind == MenuKind::Button;
    return e;
}

void StartMenu::add_title(std::vector<MenuElement>& out, const std::string& text) const {
    MenuElement e = control(MenuId::None, MenuKind::Title, ButtonRect{120, kTitleY, 400, kTitleH}, text, FontSize::Px35);
    e.centered = true;
    e.group = MenuGroup::Top;
    out.push_back(e);
}

void StartMenu::add_hint(std::vector<MenuElement>& out, const std::string& text) const {
    MenuElement e = control(MenuId::None, MenuKind::Text, ButtonRect{40, kHintY, 560, kHintH}, text, FontSize::Px12);
    e.tone = MenuTone::Dim;
    e.centered = true;
    e.group = MenuGroup::Bottom;
    out.push_back(e);
}

void StartMenu::add_message(std::vector<MenuElement>& out, int32_t y, int32_t h, FontSize font) const {
    if (message_.empty()) return;
    MenuElement e = control(MenuId::None, MenuKind::Notice, ButtonRect{kTextX, y, kTextW, h}, message_, font);
    e.tone = message_tone_;
    out.push_back(e);
}

void StartMenu::add_server_line(std::vector<MenuElement>& out, int32_t y) const {
    MenuElement e = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, y, kTextW, 14}, "Server: " + server_label(server_), FontSize::Px12);
    e.tone = MenuTone::Dim;
    e.centered = true;
    e.group = MenuGroup::Bottom;
    out.push_back(e);
}

void StartMenu::finish(std::vector<MenuElement>& out) const {
    if (wide_) {                                            // the 16:9 menu: every element goes to the middle (+160) and down by what its group says (the model's numbers are the original's page)
        for (MenuElement& e : out) {
            const int32_t dy = e.group == MenuGroup::Top ? kWideTopDy : (e.group == MenuGroup::Bottom ? kWideBottomDy : kWideMiddleDy);
            e.rect.x += kWideDx;
            e.rect.y += dy;
        }
    }
    const bool blink = ((static_cast<uint64_t>(std::max(0.0, elapsed_ms_ - caret_since_ms_))) / kCaretHalfPeriodMs) % 2 == 0;
    for (MenuElement& e : out) {
        if (e.id == MenuId::None) continue;
        e.selected = e.id == selected_;
        e.pressed = e.id == pressed_;
        if (e.kind == MenuKind::Field) {
            e.caret = e.selected && blink;
            e.all_selected = e.selected && all_selected_ && !e.text.empty();
        }
    }
}

std::vector<MenuElement> StartMenu::elements() const {
    std::vector<MenuElement> out;
    switch (panel_) {
        case MenuPanel::Main: {
            add_title(out, sim::strings::format(sim::strings::kWelcome, "Ants"));
            if (rejoin_) out.push_back(control(MenuId::Rejoin, MenuKind::Button, ButtonRect{kRejoinX, kRejoinY, kRejoinW, kRejoinH}, "Rejoin your match (" + rejoin_->room + ")", FontSize::Px18));
            out.push_back(control(MenuId::Single, MenuKind::Button, centred_button(118, 50), "Single player"));
            out.push_back(control(MenuId::JoinWithCode, MenuKind::Button, centred_button(184, 50), "Join with a code"));
            out.push_back(control(MenuId::HostOnline, MenuKind::Button, centred_button(250, 50), "Host an online match"));
            out.push_back(control(MenuId::Quit, MenuKind::Button, centred_button(316, 50), "Quit"));
            add_message(out, 382, 52);
            add_hint(out, "Up / Down: choose     Enter: select     Esc: quit");
            break;
        }
        case MenuPanel::Single: {
            add_title(out, "Single player");
            std::string own = sim::strings::colour_name(static_cast<uint8_t>(3u - own_seat_));
            if (!own.empty()) own[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(own[0])));
            MenuElement intro = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 80, kTextW, 22}, "You play the " + own + " ants. Who plays against you?", FontSize::Px18);
            intro.centered = true;
            out.push_back(intro);
            // The player's name, in the column of the controls (the rows below it have a portrait, a colour and a cycler there): the same text as the Join and Host panels, so one remembered name
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{136, 106 + 5, 130, 24}, "Your name", FontSize::Px24));
            out.push_back(control(MenuId::SingleName, MenuKind::Field, ButtonRect{280, 106, 280, 34}, name_, FontSize::Px18));
            // With two or more bots there is a choice of teams: a Teams row under the seats (the same row of a label and a cycler) that pushes the rule below it
            const bool teams_row = team_choices().size() > 1;
            int32_t y = 146;
            for (const size_t s : other_seats()) {
                MenuElement portrait = control(MenuId::None, MenuKind::Portrait, ButtonRect{84, y, 40, kSeatRowH}, std::string(), FontSize::Px18);
                portrait.team = static_cast<uint8_t>(s);
                out.push_back(portrait);
                MenuElement name = control(MenuId::None, MenuKind::Text, ButtonRect{136, y + 10, 130, 24}, sim::strings::colour_name(static_cast<uint8_t>(3u - s)), FontSize::Px24);
                out.push_back(name);
                MenuElement cycler = control(static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + s), MenuKind::Cycler, ButtonRect{280, y, 280, kSeatRowH}, std::string(), FontSize::Px24);
                cycler.value = seat_choice_text(settings_.seats[s]);
                out.push_back(cycler);
                y += kSeatRowH + 4;
            }
            if (teams_row) {
                out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{136, y + 7, 130, 24}, "Teams", FontSize::Px24));
                MenuElement teams_cycler = control(MenuId::Teams, MenuKind::Cycler, ButtonRect{280, y, 280, 38}, std::string(), FontSize::Px24);
                teams_cycler.value = teams_text(teams(), own_seat_);
                out.push_back(teams_cycler);
                y += 42;
            }
            {                                                           // what the seats mean, in both states: the original's game when nobody is seated, the bots' rule when somebody is; a refusal (the name) takes its place until the next key
                const bool refused = !message_.empty();
                MenuElement rule = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, y, kTextW, teams_row ? 28 : 42}, refused ? message_ : (any_bot() ? kBotsLine : kNoBotsLine), refused ? FontSize::Px14 : FontSize::Px18);
                if (refused) rule.tone = message_tone_;
                rule.centered = true;
                out.push_back(rule);
            }
            out.push_back(control(MenuId::Continue, MenuKind::Button, centred_button(362, 40), "Continue"));
            out.push_back(control(MenuId::Back, MenuKind::Button, centred_button(406, 34), "Back"));
            add_hint(out, "Up / Down: choose     Left / Right: change a seat     Enter: change a seat or continue     Esc: back");
            break;
        }
        case MenuPanel::Join: {
            add_title(out, "Join with a code");
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{110, 90, 420, 22}, "Your name", FontSize::Px18));
            out.push_back(control(MenuId::Name, MenuKind::Field, ButtonRect{110, 114, 420, 34}, name_, FontSize::Px18));
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{110, 160, 420, 22}, "Room code", FontSize::Px18));
            out.push_back(control(MenuId::Code, MenuKind::Field, ButtonRect{110, 184, 420, 34}, code_, FontSize::Px18));
            out.push_back(control(MenuId::Join, MenuKind::Button, centred_button(236), "Join"));
            out.push_back(control(MenuId::Back, MenuKind::Button, centred_button(290, 40), "Back"));
            add_message(out, 344, 68);
            add_server_line(out, 424);
            add_hint(out, "Tab: next field     Ctrl+V / Cmd+V: paste the code     Enter: join     Esc: back");
            break;
        }
        case MenuPanel::Host: {
            add_title(out, "Host an online match");
            const int players = std::clamp(settings_.host_players, 2, 4);
            int32_t y = kHostTop;
            const auto row = [&](const std::string& label, MenuId id, const std::string& value) {
                out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{kHostLabelX, y + 4, kHostLabelW, 22}, label, FontSize::Px18));
                MenuElement cycler = control(id, MenuKind::Cycler, ButtonRect{kHostControlX, y, kHostControlW, kHostRowH}, std::string(), FontSize::Px18);
                cycler.value = value;
                out.push_back(cycler);
                y += kHostRowPitch;
            };
            row("Map", MenuId::HostMap, menu_map(static_cast<size_t>(settings_.host_map)).name);
            row("Players", MenuId::HostPlayers, std::to_string(players) + " players");
            for (int seat = 1; seat < players; ++seat) {                      // the seats after the leader's, each with the bot that START puts there when nobody has taken it
                row(std::string(sim::strings::colour_name(static_cast<uint8_t>(3 - seat))) + " at START", host_seat_id(static_cast<size_t>(seat)), fill_seat_text(settings_.host_fill.level[static_cast<size_t>(seat)]));
            }
            if (players >= 3) row("Teams", MenuId::HostTeams, host_teams_text(host_teams(), players));
            // under the rows: what the bots do (the standard bot gathers, raids and fights back: docs/BOTS.md)
            MenuElement caption = control(MenuId::None, MenuKind::Text, ButtonRect{kHostControlX, y + 1, kHostControlW, 14}, kFillCaption, FontSize::Px14);
            caption.tone = MenuTone::Dim;
            caption.centered = true;
            out.push_back(caption);
            y += 18;
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{kHostLabelX, y + 5, kHostLabelW, 22}, "Your name", FontSize::Px18));
            out.push_back(control(MenuId::HostName, MenuKind::Field, ButtonRect{kHostControlX, y, kHostControlW, 28}, name_, FontSize::Px18));
            y += 33;
            out.push_back(control(MenuId::Host, MenuKind::Button, centred_button(y, 32), "Host"));
            y += 36;
            out.push_back(control(MenuId::Back, MenuKind::Button, centred_button(y, 32), "Back"));
            y += 38;
            if (!message_.empty()) {
                add_message(out, y, 42, FontSize::Px14);
            } else {
                MenuElement info = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, y, kTextW, 28}, "The server makes a room for you. You get a code to send to the other players.", FontSize::Px14);
                info.centered = true;
                out.push_back(info);
            }
            add_server_line(out, 424);
            add_hint(out, "Up / Down / Tab: choose     Left / Right: change     Enter: change or host     Esc: back");
            break;
        }
        case MenuPanel::Connecting: {
            add_title(out, "Connecting");
            MenuElement text = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 150, kTextW, 100}, connecting_text_, FontSize::Px24);
            text.centered = true;
            out.push_back(text);
            out.push_back(control(MenuId::Cancel, MenuKind::Button, ButtonRect{200, 290, 240, kButtonH}, "Cancel"));
            add_hint(out, "Esc: cancel");
            break;
        }
        case MenuPanel::Room: {
            add_title(out, "Host an online match");
            MenuElement caption = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 82, kTextW, 28}, "Your room:", FontSize::Px24);
            caption.centered = true;
            out.push_back(caption);
            MenuElement code = control(MenuId::None, MenuKind::Code, ButtonRect{80, 112, 480, 70}, room_code_display(room_code_), FontSize::Px35);
            code.centered = true;
            out.push_back(code);
            MenuElement info = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 190, kTextW, 38},
                                       "Send this code to the players who should join. The match starts when the room is full, or when you press START in the room.", FontSize::Px18);
            info.centered = true;
            out.push_back(info);
            MenuElement fill = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 228, kTextW, 22}, fill_choice_sentence(settings_.host_fill, room_capacity_), FontSize::Px18);   // what START will do
            fill.centered = true;
            out.push_back(fill);
            MenuElement count = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 250, kTextW, 22},
                                        "Players in the room: " + std::to_string(room_players_) + " of " + std::to_string(room_capacity_), FontSize::Px18);
            count.centered = true;
            out.push_back(count);
            if (!message_.empty()) {
                add_message(out, 274, 26);
            } else {                                                           // the teams of START (a room of three or four players with a Teams choice) stand where a message would
                const std::string teams = room_teams_sentence(settings_.host_teams, room_capacity_);
                if (!teams.empty()) {
                    MenuElement line = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 274, kTextW, 22}, teams, FontSize::Px18);
                    line.centered = true;
                    out.push_back(line);
                }
            }
            out.push_back(control(MenuId::Copy, MenuKind::Button, centred_button(300, 44), copied() ? "Copied!" : "Copy"));
            out.push_back(control(MenuId::EnterRoom, MenuKind::Button, centred_button(350, 44), "Continue to the room"));
            out.push_back(control(MenuId::Back, MenuKind::Button, centred_button(400, 38), "Back"));
            add_hint(out, "Ctrl+C / Cmd+C: copy the code     Enter: continue     Esc: leave the room");
            break;
        }
    }
    finish(out);
    return out;
}

std::vector<MenuId> StartMenu::controls() const {
    std::vector<MenuId> ids;
    for (const MenuElement& e : elements()) {
        if (e.id != MenuId::None) ids.push_back(e.id);
    }
    return ids;
}

bool StartMenu::find_element(MenuId id, MenuElement& out) const {
    for (const MenuElement& e : elements()) {
        if (e.id == id && id != MenuId::None) {
            out = e;
            return true;
        }
    }
    return false;
}

MenuId StartMenu::control_at(int32_t x, int32_t y) const {
    const std::vector<MenuElement> all = elements();
    for (size_t i = all.size(); i-- > 0;) {
        if (all[i].id != MenuId::None && all[i].rect.contains(x, y)) return all[i].id;
    }
    return MenuId::None;
}

// ---- selection and actions ---------------------------------------------------------------------------------------------------------------------------

void StartMenu::set_selected(MenuId id) {
    if (id == selected_) return;
    if (selected_ == MenuId::Name || selected_ == MenuId::HostName || selected_ == MenuId::SingleName) flush();       // the field is left
    selected_ = id;
    if (panel_ == MenuPanel::Main && id != MenuId::None) main_selection_ = id;
    all_selected_ = false;
    caret_since_ms_ = elapsed_ms_;
}

void StartMenu::move_selection(int delta) {
    const std::vector<MenuId> ids = controls();
    if (ids.empty()) return;
    size_t at = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == selected_) at = i;
    }
    const size_t n = ids.size();
    const size_t next = delta >= 0 ? (at + 1) % n : (at + n - 1) % n;
    set_selected(ids[next]);
    if (is_field(selected_)) {                                  // arriving by the keyboard selects the text: typing replaces it
        const std::string* text = field_text(selected_);
        all_selected_ = text != nullptr && !text->empty();
    }
}

std::string* StartMenu::field_text(MenuId id) noexcept {
    if (id == MenuId::Name || id == MenuId::HostName || id == MenuId::SingleName) return &name_;
    if (id == MenuId::Code) return &code_;
    return nullptr;
}

bool StartMenu::is_cycler(MenuId id) noexcept {
    size_t seat = 0;
    return (id >= MenuId::Seat0 && id <= MenuId::Seat3) || id == MenuId::Teams || id == MenuId::HostMap || id == MenuId::HostPlayers || id == MenuId::HostTeams || host_seat_of(id, seat);
}

void StartMenu::cycle(MenuId id, int delta) {
    if (id >= MenuId::Seat0 && id <= MenuId::Seat3) {
        const size_t seat = static_cast<size_t>(static_cast<uint8_t>(id) - static_cast<uint8_t>(MenuId::Seat0));
        const int n = static_cast<int>(kSeatChoiceCount);
        const int next = (static_cast<int>(settings_.seats[seat]) + delta + n) % n;
        settings_.seats[seat] = static_cast<SeatChoice>(next);
        notify(MenuSetting::Bots);
        if (settings_.teams.set && teams() != settings_.teams) {      // the team that was chosen has a seat that is no bot any more: free for all again
            settings_.teams = LocalTeams{};
            notify(MenuSetting::Teams);
        }
    } else if (id == MenuId::Teams) {
        const std::vector<LocalTeams> choices = team_choices();
        const LocalTeams now = teams();
        size_t at = 0;
        for (size_t i = 0; i < choices.size(); ++i) {
            if (choices[i] == now) at = i;
        }
        settings_.teams = choices[(at + (delta >= 0 ? 1 : choices.size() - 1)) % choices.size()];
        notify(MenuSetting::Teams);
    } else if (id == MenuId::HostMap) {
        const int n = static_cast<int>(kMenuMapCount);
        settings_.host_map = (settings_.host_map + delta + n) % n;
        notify(MenuSetting::HostMap);
    } else if (id == MenuId::HostPlayers) {
        settings_.host_players = 2 + (settings_.host_players - 2 + delta + 3) % 3;
        notify(MenuSetting::HostPlayers);
        if (settings_.host_teams.set && host_teams() != settings_.host_teams) {     // the teams that were chosen are not one of this room's choices: free for all again
            settings_.host_teams = LocalTeams{};
            notify(MenuSetting::HostTeams);
        }
    } else if (size_t seat = 0; host_seat_of(id, seat)) {
        const int n = static_cast<int>(net::kFillLevelLast) + 1;
        net::FillLevel& level = settings_.host_fill.level[seat];
        level = static_cast<net::FillLevel>((static_cast<int>(level) + delta + n) % n);
        notify(MenuSetting::HostFill);
    } else if (id == MenuId::HostTeams) {
        const std::vector<LocalTeams> choices = host_team_choices();
        const LocalTeams now = host_teams();
        size_t at = 0;
        for (size_t i = 0; i < choices.size(); ++i) {
            if (choices[i] == now) at = i;
        }
        settings_.host_teams = choices[(at + (delta >= 0 ? 1 : choices.size() - 1)) % choices.size()];
        notify(MenuSetting::HostTeams);
    }
}

void StartMenu::refuse(const std::string& message, MenuId focus) {
    message_ = message;
    message_tone_ = MenuTone::Bad;
    set_selected(focus);
    if (is_field(focus)) {
        const std::string* text = field_text(focus);
        all_selected_ = text != nullptr && !text->empty();
    }
    play(sim::SoundID::CantGo);
}

void StartMenu::try_join() {
    std::string clean_name;
    std::string clean_code;
    std::string why;
    if (!check_player_name(name_, clean_name, why)) return refuse(why, MenuId::Name);
    if (!check_room_code(code_, clean_code, why)) return refuse(why, MenuId::Code);
    message_.clear();
    origin_ = MenuPanel::Join;
    connecting_text_ = "Connecting to " + server_label(server_) + "...";
    go(MenuPanel::Connecting);
    request(MenuRequest::Type::Join);
    request_.name = clean_name;
    request_.room = clean_code;
}

void StartMenu::try_host() {
    std::string clean_name;
    std::string why;
    if (!check_player_name(name_, clean_name, why)) return refuse(why, MenuId::HostName);
    message_.clear();
    origin_ = MenuPanel::Host;
    connecting_text_ = "Connecting to " + server_label(server_) + "...";
    const int map = settings_.host_map;
    const int players = settings_.host_players;
    const net::FillPlan fill = host_fill();
    const LocalTeams teams = host_teams();
    go(MenuPanel::Connecting);
    request(MenuRequest::Type::Host);
    request_.name = clean_name;
    request_.map = map;
    request_.players = players;
    request_.fill = fill;
    request_.teams = teams;
}

// "Rejoin your match": the room, the seat and the server are the offer's; the name is the panel's when it is one the server would take (else the game's own "Player": the key decides the seat)
void StartMenu::try_rejoin() {
    if (!rejoin_) return;
    std::string clean_name;
    std::string why;
    if (!check_player_name(name_, clean_name, why)) clean_name = "Player";
    message_.clear();
    origin_ = MenuPanel::Main;
    connecting_text_ = "Rejoining your match in room " + rejoin_->room + "...";
    const RejoinOffer offer = *rejoin_;
    go(MenuPanel::Connecting);
    request(MenuRequest::Type::Rejoin);
    request_.name = clean_name;
    request_.room = offer.room;
    request_.seat = offer.seat;
    request_.server = offer.server;
}

// Continue (and Enter in the name field): the name is checked as Join and Host check it, the bots and the teams are the panel's. The name is written now (the program may end with the game).
void StartMenu::try_single() {
    std::string clean_name;
    std::string why;
    if (!check_player_name(name_, clean_name, why)) return refuse(why, MenuId::SingleName);
    message_.clear();
    flush();
    request(MenuRequest::Type::Single);
    request_.name = clean_name;
    request_.bots = bots();
    request_.teams = teams();
}

void StartMenu::copy_code() {
    if (room_code_.empty()) return;
    const bool ok = clipboard_set_ && clipboard_set_(room_code_);
    if (ok) {
        copied_ms_ = 1.0;
        message_.clear();
    } else {
        copied_ms_ = 0.0;
        message_ = "Copy failed. Write down the code above.";                // (the code stands in full in the box above: a text that repeated it was cut)
        message_tone_ = MenuTone::Bad;
    }
}

void StartMenu::activate(MenuId id) {
    switch (id) {
        case MenuId::None: break;
        case MenuId::Single:
            message_.clear();
            go(MenuPanel::Single);
            break;
        case MenuId::JoinWithCode:
            message_.clear();
            go(MenuPanel::Join);
            break;
        case MenuId::HostOnline:
            message_.clear();
            go(MenuPanel::Host);
            break;
        case MenuId::Rejoin: try_rejoin(); break;
        case MenuId::Quit: request(MenuRequest::Type::Quit); break;
        case MenuId::Seat0:
        case MenuId::Seat1:
        case MenuId::Seat2:
        case MenuId::Seat3:
        case MenuId::Teams:
        case MenuId::HostMap:
        case MenuId::HostPlayers:
        case MenuId::HostSeat1:
        case MenuId::HostSeat2:
        case MenuId::HostSeat3:
        case MenuId::HostTeams: cycle(id, 1); break;
        case MenuId::Continue:
        case MenuId::SingleName: try_single(); break;
        case MenuId::Name:
            set_selected(MenuId::Code);
            all_selected_ = !code_.empty();
            break;
        case MenuId::Code:
        case MenuId::Join: try_join(); break;
        case MenuId::HostName:
        case MenuId::Host: try_host(); break;
        case MenuId::Cancel: request(MenuRequest::Type::Cancel); break;
        case MenuId::Copy: copy_code(); break;
        case MenuId::EnterRoom: request(MenuRequest::Type::EnterRoom); break;
        case MenuId::Back: back(); break;
    }
}

void StartMenu::back() {
    switch (panel_) {
        case MenuPanel::Main:
            if (settled()) request(MenuRequest::Type::Quit);        // (an Esc that comes in a hurry after the one that left another panel is the same gesture: it does not quit)
            break;
        case MenuPanel::Single:
        case MenuPanel::Join:
        case MenuPanel::Host:
            message_.clear();
            show_main();
            break;
        case MenuPanel::Connecting: request(MenuRequest::Type::Cancel); break;
        case MenuPanel::Room: request(MenuRequest::Type::LeaveRoom); break;
    }
}

// ---- input ------------------------------------------------------------------------------------------------------------------------------------------

void StartMenu::on_mouse_move(int32_t x, int32_t y) {
    const MenuId id = control_at(x, y);
    if (pressed_ != MenuId::None && id != pressed_) pressed_ = MenuId::None;       // leaving a pressed button cancels it for good (the original's button class)
    if (id != MenuId::None && !is_field(id)) set_selected(id);                       // the hover selects buttons; a field gets the focus by a click or the keyboard
}

bool StartMenu::on_mouse_down(int32_t x, int32_t y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return false;
    const MenuId id = control_at(x, y);
    if (id == MenuId::None) return false;
    set_selected(id);
    if (is_field(id)) {
        all_selected_ = false;
        caret_since_ms_ = elapsed_ms_;
        return false;
    }
    pressed_ = id;
    play(sim::SoundID::ButtonClick);                                // the click of a pressed button (the original's buttons carry it in their pressed picture)
    return true;
}

void StartMenu::on_mouse_up(int32_t x, int32_t y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return;
    const MenuId id = control_at(x, y);
    const MenuId captured = pressed_;
    pressed_ = MenuId::None;
    if (captured != MenuId::None && captured == id) activate(id);
}

void StartMenu::edit(MenuId id, const std::string& typed) {
    std::string* text = field_text(id);
    if (text == nullptr) return;
    const size_t limit = id == MenuId::Code ? kCodeMax : kNameMax;
    bool refused = false;
    const std::string add = printable_text(typed, refused);
    if (add.empty() && !refused) return;
    if (!add.empty()) {
        if (all_selected_) text->clear();
        all_selected_ = false;
        for (char c : add) {
            if (text->size() >= limit) break;
            text->push_back(c);
        }
        caret_since_ms_ = elapsed_ms_;
        if (id != MenuId::Code) name_changed();
    }
    message_.clear();
    if (refused) {                                                  // never silently: the person sees why a letter did not appear
        message_ = kRefusedCharsText;
        message_tone_ = MenuTone::Bad;
    }
}

void StartMenu::on_text(const std::string& text) {
    if (is_field(selected_)) edit(selected_, text);
}

void StartMenu::paste(const std::string& text) {
    if (!is_field(selected_)) return;
    bool refused = false;
    std::string add = printable_text(text, refused);                // (a line end or a tab in the clipboard is dropped)
    if (selected_ == MenuId::Code) add = trim_blanks(add);
    edit(selected_, add);
    if (refused) {
        message_ = kRefusedCharsText;
        message_tone_ = MenuTone::Bad;
    }
}

void StartMenu::on_key(SDL_Keycode key, uint16_t modifiers, bool repeat) {
    const bool command = (modifiers & (KMOD_CTRL | KMOD_GUI)) != 0;
    const bool shift = (modifiers & KMOD_SHIFT) != 0;
    const bool in_field = is_field(selected_);
    if (command) {
        if (key == SDLK_v && in_field && clipboard_get_) paste(clipboard_get_());
        else if (key == SDLK_a && in_field) all_selected_ = !field_text(selected_)->empty();
        else if (key == SDLK_c && panel_ == MenuPanel::Room) copy_code();
        else if (key == SDLK_BACKSPACE && in_field) {
            field_text(selected_)->clear();
            all_selected_ = false;
            message_.clear();
            if (selected_ != MenuId::Code) name_changed();
        }
        return;
    }
    switch (key) {
        case SDLK_ESCAPE:
            if (!repeat) back();
            break;
        case SDLK_UP:
            move_selection(-1);
            break;
        case SDLK_DOWN:
            move_selection(1);
            break;
        case SDLK_TAB:
            move_selection(shift ? -1 : 1);
            break;
        case SDLK_LEFT:
            if (is_cycler(selected_)) cycle(selected_, -1);
            break;
        case SDLK_RIGHT:
            if (is_cycler(selected_)) cycle(selected_, 1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!repeat && settled()) activate(selected_);           // (a key that is held, or pressed again within kSettleMs of the panel's appearing, does not act on that panel)
            break;
        case SDLK_SPACE:
            if (!repeat && !in_field && settled()) activate(selected_);   // (in a field the space is text, and arrives as text input)
            break;
        case SDLK_BACKSPACE:
            if (in_field) {
                std::string* text = field_text(selected_);
                if (all_selected_) text->clear();
                else if (!text->empty()) text->pop_back();
                all_selected_ = false;
                caret_since_ms_ = elapsed_ms_;
                message_.clear();
                if (selected_ != MenuId::Code) name_changed();
            }
            break;
        default:
            break;
    }
}

}  // namespace ants::app

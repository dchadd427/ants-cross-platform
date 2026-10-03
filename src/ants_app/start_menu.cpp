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

// The six maps of the original game, in the order of web/four.html (by size)
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
constexpr int32_t kHintY = 446;
constexpr int32_t kHintH = 14;
constexpr int32_t kTextX = 60;
constexpr int32_t kTextW = 520;
constexpr const char* kFillCaption = "Bots gather food; they do not fight yet.";     // (until the bots fight: B4b of docs/BOTS.md) under "Empty seats at START" here and on web/four.html

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

const char* fill_choice_text(net::FillLevel level) noexcept {
    switch (level) {
        case net::FillLevel::None: return "Leave empty";
        case net::FillLevel::Easy: return "Easy bots";
        case net::FillLevel::Medium: return "Medium bots";
        case net::FillLevel::Hard: return "Hard bots";
    }
    return "";
}

std::string fill_choice_sentence(net::FillLevel level) {
    if (level == net::FillLevel::None) return "Empty seats stay empty.";
    return "Empty seats will be " + net::fill_level_title(level) + " bots.";
}

const MenuMap& menu_map(size_t index) noexcept { return kMaps[index < kMenuMapCount ? index : static_cast<size_t>(kDefaultMenuMap)]; }

int menu_map_index(const std::string& key) noexcept {
    const std::string k = lower_text(key);
    for (size_t i = 0; i < kMenuMapCount; ++i) {
        if (k == kMaps[i].key) return static_cast<int>(i);
    }
    return -1;
}

std::string make_room_code(const MenuMap& map, int players, const std::function<uint32_t()>& random) {
    const int n = std::clamp(players, 2, 4);
    std::string code = std::string("demo-") + map.key + "-" + std::to_string(n) + "p-";
    const size_t alphabet = std::char_traits<char>::length(kRoomCodeAlphabet);
    for (size_t i = 0; i < kRoomCodeRandomChars; ++i) code.push_back(kRoomCodeAlphabet[random() % alphabet]);
    return code;
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
    host_fill = net::FillLevel::None;                                               // (anything that is not one of the four words is the default)
    net::parse_fill_level(trim_blanks(store.get_string(kKeyHostFill, std::string(), 16)), host_fill);
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
            store.set_string(kKeyHostFill, net::fill_level_name(host_fill));
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

void StartMenu::show_main(const std::string& notice) {
    message_ = notice;
    message_tone_ = MenuTone::Normal;
    go(MenuPanel::Main);
}

void StartMenu::connection_failed(const std::string& message) {
    message_ = message;
    message_tone_ = MenuTone::Bad;
    const MenuPanel to = origin_ == MenuPanel::Host ? MenuPanel::Host : MenuPanel::Join;
    go(to);
    selected_ = to == MenuPanel::Host ? MenuId::Host : MenuId::Join;
    all_selected_ = false;
}

void StartMenu::connection_cancelled() {
    message_.clear();
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

void StartMenu::add_message(std::vector<MenuElement>& out, int32_t y, int32_t h) const {
    if (message_.empty()) return;
    MenuElement e = control(MenuId::None, MenuKind::Notice, ButtonRect{kTextX, y, kTextW, h}, message_, FontSize::Px18);
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
            MenuElement intro = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 90, kTextW, 22}, "You play the " + own + " ants. Who plays against you?", FontSize::Px18);
            intro.centered = true;
            out.push_back(intro);
            int32_t y = 124;
            for (const size_t s : other_seats()) {
                MenuElement portrait = control(MenuId::None, MenuKind::Portrait, ButtonRect{84, y, 40, kButtonH}, std::string(), FontSize::Px18);
                portrait.team = static_cast<uint8_t>(s);
                out.push_back(portrait);
                MenuElement name = control(MenuId::None, MenuKind::Text, ButtonRect{136, y + 11, 130, 24}, sim::strings::colour_name(static_cast<uint8_t>(3u - s)), FontSize::Px24);
                out.push_back(name);
                MenuElement cycler = control(static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + s), MenuKind::Cycler, ButtonRect{280, y, 280, kButtonH}, std::string(), FontSize::Px24);
                cycler.value = seat_choice_text(settings_.seats[s]);
                out.push_back(cycler);
                y += 56;
            }
            {                                                           // what the seats mean, in both states: the original's game when nobody is seated, the bots' rule when somebody is
                MenuElement rule = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 290, kTextW, 42}, any_bot() ? kBotsLine : kNoBotsLine, FontSize::Px18);
                rule.centered = true;
                out.push_back(rule);
            }
            out.push_back(control(MenuId::Continue, MenuKind::Button, centred_button(334), "Continue"));
            out.push_back(control(MenuId::Back, MenuKind::Button, centred_button(388, 40), "Back"));
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
            // (the labels are wide enough for "Empty seats at START"; every control stands in the column at x = 290; the three choices are 36 high so that the line under the last one fits
            // above the name without moving Host and Back, whose places the click rules of the panels were tested with)
            const MenuMap& map = menu_map(static_cast<size_t>(settings_.host_map));
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{60, 78 + 7, 220, 22}, "Map", FontSize::Px18));
            MenuElement map_cycler = control(MenuId::HostMap, MenuKind::Cycler, ButtonRect{290, 78, 250, 36}, std::string(), FontSize::Px24);
            map_cycler.value = map.name;
            out.push_back(map_cycler);
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{60, 118 + 7, 220, 22}, "Players", FontSize::Px18));
            MenuElement players_cycler = control(MenuId::HostPlayers, MenuKind::Cycler, ButtonRect{290, 118, 250, 36}, std::string(), FontSize::Px24);
            players_cycler.value = std::to_string(settings_.host_players) + " players";
            out.push_back(players_cycler);
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{60, 158 + 7, 220, 22}, "Empty seats at START", FontSize::Px18));
            MenuElement fill_cycler = control(MenuId::HostFill, MenuKind::Cycler, ButtonRect{290, 158, 250, 36}, std::string(), FontSize::Px24);
            fill_cycler.value = fill_choice_text(settings_.host_fill);
            out.push_back(fill_cycler);
            // under the choice: what the bots do (the room's bots only harvest, until the bots learn to fight: docs/BOTS.md)
            MenuElement caption = control(MenuId::None, MenuKind::Text, ButtonRect{240, 196, 350, 14}, kFillCaption, FontSize::Px14);
            caption.tone = MenuTone::Dim;
            caption.centered = true;
            out.push_back(caption);
            out.push_back(control(MenuId::None, MenuKind::Text, ButtonRect{60, 214 + 6, 220, 22}, "Your name", FontSize::Px18));
            out.push_back(control(MenuId::HostName, MenuKind::Field, ButtonRect{290, 214, 250, 34}, name_, FontSize::Px18));
            out.push_back(control(MenuId::Host, MenuKind::Button, centred_button(266, 44), "Host"));
            out.push_back(control(MenuId::Back, MenuKind::Button, centred_button(316, 38), "Back"));
            if (!message_.empty()) {
                add_message(out, 364, 56);
            } else {
                MenuElement info = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 366, kTextW, 40}, "The server makes a room for you. You get a code to send to the other players.", FontSize::Px18);
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
            MenuElement code = control(MenuId::None, MenuKind::Code, ButtonRect{80, 112, 480, 70}, room_code_, FontSize::Px35);
            code.centered = true;
            out.push_back(code);
            MenuElement info = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 190, kTextW, 38},
                                       "Send this code to the players who should join. The match starts when the room is full, or when you press START in the room.", FontSize::Px18);
            info.centered = true;
            out.push_back(info);
            MenuElement fill = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 228, kTextW, 22}, fill_choice_sentence(settings_.host_fill), FontSize::Px18);   // what START will do
            fill.centered = true;
            out.push_back(fill);
            MenuElement count = control(MenuId::None, MenuKind::Text, ButtonRect{kTextX, 250, kTextW, 22},
                                        "Players in the room: " + std::to_string(room_players_) + " of " + std::to_string(room_capacity_), FontSize::Px18);
            count.centered = true;
            out.push_back(count);
            add_message(out, 274, 26);
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
    if (selected_ == MenuId::Name || selected_ == MenuId::HostName) flush();       // the field is left
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
    if (id == MenuId::Name || id == MenuId::HostName) return &name_;
    if (id == MenuId::Code) return &code_;
    return nullptr;
}

void StartMenu::cycle(MenuId id, int delta) {
    if (id >= MenuId::Seat0 && id <= MenuId::Seat3) {
        const size_t seat = static_cast<size_t>(static_cast<uint8_t>(id) - static_cast<uint8_t>(MenuId::Seat0));
        const int n = static_cast<int>(kSeatChoiceCount);
        const int next = (static_cast<int>(settings_.seats[seat]) + delta + n) % n;
        settings_.seats[seat] = static_cast<SeatChoice>(next);
        notify(MenuSetting::Bots);
    } else if (id == MenuId::HostMap) {
        const int n = static_cast<int>(kMenuMapCount);
        settings_.host_map = (settings_.host_map + delta + n) % n;
        notify(MenuSetting::HostMap);
    } else if (id == MenuId::HostPlayers) {
        settings_.host_players = 2 + (settings_.host_players - 2 + delta + 3) % 3;
        notify(MenuSetting::HostPlayers);
    } else if (id == MenuId::HostFill) {
        const int n = static_cast<int>(net::kFillLevelLast) + 1;
        settings_.host_fill = static_cast<net::FillLevel>((static_cast<int>(settings_.host_fill) + delta + n) % n);
        notify(MenuSetting::HostFill);
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
    const net::FillLevel fill = settings_.host_fill;
    go(MenuPanel::Connecting);
    request(MenuRequest::Type::Host);
    request_.name = clean_name;
    request_.map = map;
    request_.players = players;
    request_.fill = fill;
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
        case MenuId::Quit: request(MenuRequest::Type::Quit); break;
        case MenuId::Seat0:
        case MenuId::Seat1:
        case MenuId::Seat2:
        case MenuId::Seat3:
        case MenuId::HostMap:
        case MenuId::HostPlayers:
        case MenuId::HostFill: cycle(id, 1); break;
        case MenuId::Continue:
            request(MenuRequest::Type::Single);
            request_.bots = bots();
            break;
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
            if (selected_ >= MenuId::Seat0 && selected_ <= MenuId::Seat3) cycle(selected_, -1);
            else if (selected_ == MenuId::HostMap || selected_ == MenuId::HostPlayers || selected_ == MenuId::HostFill) cycle(selected_, -1);
            break;
        case SDLK_RIGHT:
            if (selected_ >= MenuId::Seat0 && selected_ <= MenuId::Seat3) cycle(selected_, 1);
            else if (selected_ == MenuId::HostMap || selected_ == MenuId::HostPlayers || selected_ == MenuId::HostFill) cycle(selected_, 1);
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

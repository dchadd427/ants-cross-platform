// The fixtures of the tests that play a match over links with a delay and jitter (test_prediction_link.cpp): a headless Application joins a room of a real RoomManager (the door, the lobby, the room,
// the referee, the sequencer's seals) over net::LoopbackNetwork links of a chosen round trip and jitter, on a virtual clock that moves 1 ms at a time, and a bare second machine plays the other person.
// Nothing is real time: a run is the same every time, and a match of a minute takes a moment.
//
// THE PERSON. What the person at the application does goes through the application's own door: SDL mouse events (Application::handle_mouse_button: the HUD's pick, the cursor, the order, NetGame::submit,
// the link, the seal, the jitter buffer, the confirmed engine), at the instant of a frame (the event queue is read at the start of a frame, as Application::run_frame_with_delta does), and what the person
// decides comes from what the screen shows (Application::view_sim()), as a person's does.
#pragma once

#include <SDL.h>

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace shaped {

using namespace ants;
using namespace ants::app;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------------------------
// The lane: in the MUD map TINY.LVL's tile row 3 is mud from tile 8 to tile 22 with a solid wall on both sides of it (rows 2 and 4, from tile 7 to tile 23), so that the path finder, which goes round
// mud, has no way but the mud; the GRASS map is TINY as it is. (Rows 2 and 3 of TINY are open ground from tile 2 to tile 28.)
// ---------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kLaneRow = 3;
constexpr int32_t kMudFrom = 8;
constexpr int32_t kMudTo = 22;
constexpr uint16_t kMudTile = 439;                 // a mud tile of TINY's own dictionary

inline std::string original_maps() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }

// A folder of this process alone, made new and removed with everything in it when the program ends
class Scratch {
public:
    Scratch() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_link_" + std::to_string(static_cast<long>(getpid())) + "_" + suffix);
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot make a scratch folder");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

inline const fs::path& scratch_root() {
    static Scratch root;
    return root.path();
}

// The folder of the two lane maps (made once): GRASS.LVL and MUD.LVL, both TINY rewritten, so that the server and every game read the same files from it
inline const fs::path& maps_folder() {
    static fs::path dir;
    if (!dir.empty()) return dir;
    dir = scratch_root() / "maps";
    fs::create_directories(dir);
    struct Kind {
        const char* name;
        bool mud;
        uint16_t minutes;
    };
    for (const Kind kind : {Kind{"GRASS.LVL", false, 0}, Kind{"MUD.LVL", true, 0}, Kind{"SHORT.LVL", false, 1}}) {       // (SHORT: a match of one minute, that ends by itself)
        const bool mud = kind.mud;
        assets::LevelData level;
        if (!level.load_lvl(original_maps() + "/TINY.LVL")) throw std::runtime_error("TINY.LVL does not load");
        if (kind.minutes != 0) level.default_minutes = kind.minutes;
        if (sim::movement::terrain_class_of_tile(kMudTile) != sim::movement::kTerrainMud) throw std::runtime_error("the mud tile is not mud");
        if (mud) {
            for (int32_t x = kMudFrom; x <= kMudTo; ++x) level.layer1_terrain[static_cast<size_t>(kLaneRow) * level.width() + static_cast<size_t>(x)].tile_index = kMudTile;
            for (const int32_t y : {kLaneRow - 1, kLaneRow + 1}) {
                for (int32_t x = kMudFrom - 1; x <= kMudTo + 1; ++x) level.layer1_terrain[static_cast<size_t>(y) * level.width() + static_cast<size_t>(x)].flags |= 1u;      // (bit 0: the cell is solid)
            }
        }
        const assets::LevelWriteResult written = assets::write_level(level, assets::LevelLimits{});
        if (!written.ok) throw std::runtime_error("the lane map cannot be written: " + written.error);
        std::ofstream out(dir / kind.name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(written.bytes.data()), static_cast<std::streamsize>(written.bytes.size()));
    }
    return dir;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The links
// ---------------------------------------------------------------------------------------------------------------------------------

// A link that the network owns, as the room's door and the game own theirs (a Connection that forwards)
class Wire final : public net::Connection {
public:
    explicit Wire(net::Connection* c) : c_(c) {}
    bool send(const std::vector<uint8_t>& m) override { return c_->send(m); }
    bool poll(std::vector<uint8_t>& m) override { return c_->poll(m); }
    State state() const override { return c_->state(); }
    void close() override { c_->close(); }

private:
    net::Connection* c_;
};

struct LinkSpec {
    uint32_t rtt_ms{0};               // the least round trip: twice the one-way delay
    uint32_t jitter_ms{0};            // each message takes up to this much more, each way (the mean round trip is rtt + jitter)
    uint32_t spike_ms{0};
    uint32_t spike_per_mille{0};
};

// ---------------------------------------------------------------------------------------------------------------------------------
// The other person: a bare machine (an engine and a NetGame, no screen)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    std::string dir;
    uint64_t ticks{0};

    explicit Peer(std::string maps) : dir(std::move(maps)) {
        net.set_discovery(0);
        net.set_prediction_enabled(false);                                          // (a plain machine: the other person's screen is not what the tests look at)
        net.set_on_tick([this]() { ++ticks; });
    }
    void frame(uint32_t now) {
        net.update(now);
        for (const net::NetGame::Event& ev : net.take_events()) {
            if (ev.type != net::NetGame::Event::Type::StartRequested) continue;
            const net::StartMsg& s = net.start_info();
            assets::LevelData level;
            uint64_t hash = 0;
            const std::string path = dir + "/" + s.map_name;
            const bool ok = level.load_lvl(path) && net::hash_file(path, hash) && hash == s.map_hash;
            if (ok) {
                sim.set_fog_of_war_enabled(s.fog);
                sim.set_game_mode(s.game_mode());
                sim.init(level, s.seed, s.roster);
                for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.set_player_name(p, s.names[p]);
                sim::apply_start_teams(sim, s.teams());
            }
            net.report_loaded(ok);
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// The session: one room, the application, the other person, one clock
// ---------------------------------------------------------------------------------------------------------------------------------

struct Params {
    std::string map{"GRASS.LVL"};
    LinkSpec link;                    // the application's link
    LinkSpec peer_link;               // the other person's
    bool predict{true};
    uint32_t seed{1};                 // the links' jitter and the frame grids
    double fps{60.0};
    bool fog{false};
    uint8_t seats{2};
    std::vector<ai::BotSpec> bots;
    bool use_peer{true};
    uint8_t peers{1};                 // the other people (bare machines) when use_peer: seats 1 .. peers
    sim::StartTeams teams;            // the room's teams (set: a pair of seats against the other two)
    bool render{false};               // every frame of the application is drawn too (the ants' glide is fed by the frame that draws)
    bool glide{true};                 // the prediction keeps the ants that its rebuilds move (the application draws them gliding)
    bool local{false};                // a game on this computer (no room, no links, no other person): the reference that the original's feel is measured against
};

class Session {
public:
    explicit Session(const Params& params)
        : p_(params), net_(params.seed * 7919u + 13u), rng_(params.seed * 2654435761u + 1u) {
        app_next_ = static_cast<double>(now_) + 17.0 * static_cast<double>(rng_() % 1000u) / 1000.0;      // the frame grids: a phase of their own
        peer_next_ = static_cast<double>(now_) + 17.0 * static_cast<double>(rng_() % 1000u) / 1000.0;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.lan_port = 0;
        cfg.aspect = Aspect::Wide16x9;                                              // (the web game's picture: a map view of 762 x 500, 23 tiles across)
        cfg.maps_dir = maps_folder().string();
        static uint32_t sessions_made = 0;                                               // (every session has a settings file of its own: no key of an earlier session is found by a later one)
        cfg.settings_path = (scratch_root() / ("settings_" + std::to_string(++sessions_made) + ".ini")).string();
        cfg.prediction = p_.predict;
        if (p_.local) {
            cfg.default_map_path = (maps_folder() / p_.map).string();
            cfg.play_at_once = true;
            app_ = std::make_unique<Application>();
            if (!app_->init(cfg)) throw std::runtime_error("the application could not start");
            app_->hud().dismiss_match_start_modal();
            return;
        }
        server::ServerLimits limits;
        mgr_ = std::make_unique<server::RoomManager>(server::MapStore(maps_folder().string()), limits);
        server::RoomSpec spec;
        spec.code = "LINK-1";
        spec.map = p_.map;
        spec.players = p_.seats;
        spec.fog = p_.fog;
        spec.bots = p_.bots;
        spec.teams = p_.teams;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.reconnect = true;
        spec.resume_countdown_ms = 0;
        if (!mgr_->create_room(spec, now_).ok) throw std::runtime_error("the room cannot be made");
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = 1;
        cfg.net_room = "LINK-1";
        cfg.player_name = "Ann";
        cfg.net_link_maker = [this]() { return make_link(p_.link); };
        app_ = std::make_unique<Application>();
        if (!app_->init(cfg)) throw std::runtime_error("the application could not start");
        if (!p_.glide) app_->net()->set_prediction_shifts(false);
        if (p_.use_peer) {
            run_ms(300);                                                              // (the application is in the room first: seat 0)
            for (uint8_t i = 0; i < std::max<uint8_t>(1, p_.peers); ++i) {
                peers_.push_back(std::make_unique<Peer>(maps_folder().string()));
                Peer& peer = *peers_.back();
                peer.net.set_link_maker_for_test([this]() { return make_link(p_.peer_link); });
                if (!peer.net.join("127.0.0.1", 1, "Bob", 255, "LINK-1")) throw std::runtime_error("the peer cannot join");
                run_ms(300);                                                          // (seats are given in the order of the Hellos)
            }
        }
    }

    Application& app() { return *app_; }
    Peer& peer(size_t i = 0) { return *peers_.at(i); }
    size_t peer_count() const noexcept { return peers_.size(); }
    uint32_t now() const noexcept { return now_; }
    server::RoomManager& manager() { return *mgr_; }
    net::LoopbackNetwork& network() { return net_; }
    std::mt19937& rng() { return rng_; }

    // ---- the clock ----------------------------------------------------------------------------------------------------------------
    // One millisecond: the links, the server's pass (every 2 ms, as ants_server's loop), the other person's frame, the application's frame
    void step_one() {
        ++now_;
        if (mgr_) {
            net_.set_time(now_);
            if ((now_ & 1u) == 0) mgr_->update(now_);
        }
        if (!peers_.empty() && static_cast<double>(now_) >= peer_next_) {
            for (auto& peer : peers_) peer->frame(now_);
            peer_next_ += 1000.0 / p_.fps;
        }
        if (static_cast<double>(now_) >= app_next_) {
            app_frame();
            app_next_ += 1000.0 / p_.fps;
        }
    }
    void run_ms(uint32_t ms) {
        for (uint32_t i = 0; i < ms; ++i) step_one();
    }
    bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t i = 0; i < max_ms; ++i) {
            if (cond()) return true;
            step_one();
        }
        return cond();
    }
    // The match is on: the application is playing, has run ticks, and so has the other person
    bool match_running() {
        if (p_.local) return app_->state() == AppState::Playing && app_->sim().current_tick() >= 40;
        if (app_->state() != AppState::Playing || app_->net() == nullptr || app_->net()->phase() != net::NetGame::Phase::Playing || app_->sim().current_tick() < 40) return false;
        for (const auto& peer : peers_) {
            if (peer->net.phase() != net::NetGame::Phase::Playing || peer->ticks < 40) return false;
        }
        return true;
    }
    bool begin() { return run_until([this]() { return match_running(); }, 40000); }

    // ---- the person's hand ------------------------------------------------------------------------------------------------------
    // Called at the start of every frame of the application, before the frame's own work (the event queue of a frame): what the person's hand does at that instant
    std::function<void()> on_frame_start;
    // Called after every frame: what the person sees now (the shown engine) and decides
    std::function<void()> on_frame_end;

    // A mouse click through the application's own door
    void click(uint8_t button, int32_t sx, int32_t sy) {
        for (const uint32_t type : {static_cast<uint32_t>(SDL_MOUSEBUTTONDOWN), static_cast<uint32_t>(SDL_MOUSEBUTTONUP)}) {
            SDL_MouseButtonEvent b{};
            b.type = type;
            b.button = button;
            b.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
            b.clicks = 1;
            b.x = sx;
            b.y = sy;
            app_->handle_mouse_button(b);
        }
        ++clicks_given_;
    }
    // The camera on a tile, and the pixel of the screen where the tile's middle is drawn
    bool camera_on(int32_t tx, int32_t ty) {
        const sim::Grid& grid = app_->sim().grid();
        app_->renderer().camera().center_on(tx * 32 + 16, ty * 32 + 16, grid.width(), grid.height());
        return true;
    }
    bool screen_of_world(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) { return app_->renderer().camera().world_to_screen(wx, wy, sx, sy); }
    bool screen_of_tile(int32_t tx, int32_t ty, int32_t& sx, int32_t& sy) { return screen_of_world(tx * 32 + 16, ty * 32 + 16, sx, sy); }
    uint32_t clicks_given() const noexcept { return clicks_given_; }

    // ---- what the engines hold ------------------------------------------------------------------------------------------------------
    static const sim::AntSnapshot* ant_in(const sim::SimulationEngine& sim, uint32_t id) {
        for (const auto& a : sim.get_world_state().ants) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    static std::vector<uint32_t> ants_of(const sim::SimulationEngine& sim, uint8_t seat) {
        std::vector<uint32_t> out;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == seat && a.hp > 0) out.push_back(a.id);
        }
        return out;
    }
    const sim::AntSnapshot* confirmed_ant(uint32_t id) { return ant_in(app_->sim(), id); }
    const sim::AntSnapshot* shown_ant(uint32_t id) { return ant_in(app_->view_sim(), id); }

    // An order that is not the person's click (a setup move: the ant to its place): the command goes through the application's NetGame like the HUD's
    void order(uint8_t seat, sim::CommandType type, const std::vector<uint32_t>& ants, int32_t tx, int32_t ty) {
        sim::Command c;
        c.type = type;
        c.issuer = seat;
        c.tile_x = static_cast<int16_t>(tx);
        c.tile_y = static_cast<int16_t>(ty);
        c.ants = ants;
        if (p_.local) app_->sim().apply_command(c);                                // (a game on this computer: the HUD's sink applies at once)
        else if (seat == 0) app_->net()->submit(c);
        else if (seat <= peers_.size()) peers_[seat - 1u]->net.submit(c);
    }
    // ---- the room ----------------------------------------------------------------------------------------------------------------
    bool room_status(server::RoomStatus& out) const { return mgr_->status("LINK-1", out, now_); }
    bool room_finished() const {
        server::RoomStatus st;
        return room_status(st) && st.state == server::RoomState::Finished;
    }
    uint64_t referee_hash() const {
        server::RoomStatus st;
        return room_status(st) ? st.referee_hash : 0u;
    }
    // The application's link is cut (a cable pulled: both ends see it closed); the way back makes a new one
    void cut_app_link() {
        if (app_wire_ != nullptr) net_.cut(app_wire_, false);
    }
    // A seat leaves the match (the Quit command of the other person's game, or the application's own)
    void quit(uint8_t seat) {
        sim::Command q;
        q.type = sim::CommandType::Quit;
        q.issuer = seat;
        if (seat == 0) app_->net()->submit(q);
        else if (seat <= peers_.size()) peers_[seat - 1u]->net.submit(q);
    }

    // An ant stands still on `tile` in the confirmed engine and in the shown one
    bool settled_on(uint32_t id, int32_t tx, int32_t ty) {
        const sim::AntSnapshot* c = confirmed_ant(id);
        const sim::AntSnapshot* s = shown_ant(id);
        if (c == nullptr || s == nullptr) return false;
        return c->action == 0 && s->action == 0 && c->tile_x == tx && c->tile_y == ty && s->tile_x == tx && s->tile_y == ty;
    }
    bool bring(uint8_t seat, uint32_t id, int32_t tx, int32_t ty, uint32_t max_ms = 20000) {
        order(seat, sim::CommandType::GroupMove, {id}, tx, ty);
        if (!run_until([&]() { return settled_on(id, tx, ty); }, max_ms)) return false;
        run_ms(400);
        return settled_on(id, tx, ty);
    }

private:
    std::unique_ptr<net::Connection> make_link(const LinkSpec& spec) {
        net::LoopbackNetwork::Link link;
        link.latency_ms = std::max<uint32_t>(1u, spec.rtt_ms / 2u);
        link.jitter_ms = spec.jitter_ms;
        link.spike_ms = spec.spike_ms;
        link.spike_per_mille = spec.spike_per_mille;
        const auto ends = net_.connect(link);
        mgr_->add_connection(std::make_unique<Wire>(ends.first), "127.0.0.1", now_);
        if (&spec == &p_.link) app_wire_ = ends.second;                              // (the application's link: the one that a test can cut)
        return std::make_unique<Wire>(ends.second);
    }
    void app_frame() {
        const double dt = std::max(0.0, static_cast<double>(now_ - last_app_frame_)) / 1000.0;
        last_app_frame_ = now_;
        if (on_frame_start) on_frame_start();
        app_->pump_network(static_cast<float>(dt));
        app_->update_simulation(static_cast<float>(dt));
        if (p_.render) app_->render_frame();
        if (on_frame_end) on_frame_end();
    }

    Params p_;
    net::LoopbackNetwork net_;
    std::mt19937 rng_;
    uint32_t now_{1000};
    uint32_t last_app_frame_{1000};
    double app_next_{0.0};
    double peer_next_{0.0};
    uint32_t clicks_given_{0};
    net::Connection* app_wire_{nullptr};
    std::unique_ptr<server::RoomManager> mgr_;
    std::unique_ptr<Application> app_;
    std::vector<std::unique_ptr<Peer>> peers_;
};

}  // namespace shaped

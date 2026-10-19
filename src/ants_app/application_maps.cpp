// The map of a match of the network (Application::net_load_match): a map that the maps folder holds is loaded at once; a map that it does not (the web build: the players' maps are not in the page's file
// system) is fetched first by the MapFetcher (map_fetch.hpp), which also checks its hash against the Start's. The match loads when the file is here and fails, with the reason on the status line, when it
// cannot be had: the lock-step never waits on it (the room's load barrier does: 60 seconds, then the start is cancelled for everybody and the room is as it was).

#include <iostream>

#include "ants_app/application.hpp"
#include "ants_net/protocol.hpp"

namespace ants::app {

namespace {

/// Whether the maps folder's list (read when the game started) holds the file: the originals, in the web build
bool listed(const std::vector<MapSelectEntry>& maps, const std::string& name) {
    for (const auto& entry : maps) {
        if (entry.filename == name) return true;
    }
    return false;
}

}  // namespace

// Start was received: load the map exactly as named, check it is the same file as the host's, initialise the simulation with the shared seed,
// roster and fog option, and report. Every machine does this on the setup screen; the match begins when everybody has reported.
void Application::net_load_match() {
    const net::StartMsg& start = net_->start_info();
    map_wait_.clear();
    if (map_fetcher_ && net::valid_map_name(start.map_name) && !listed(map_select_.get_maps(), start.map_name)) {
        map_fetcher_->request(config_.maps_dir, start.map_name, start.map_hash);
        map_wait_ = start.map_name;
        poll_map_fetch();                                                    // (a map that is here already carries on at once)
        return;
    }
    net_finish_load();
}

void Application::net_finish_load() {
    const net::StartMsg& start = net_->start_info();
    std::string path;
    for (const auto& entry : map_select_.get_maps()) {
        if (entry.filename == start.map_name) path = entry.full_path;
    }
    if (path.empty()) path = config_.maps_dir + "/" + start.map_name;
    uint64_t hash = 0;
    local_player_id_ = net_->my_seat();
    bool ok = net::hash_file(path, hash) && hash == start.map_hash;
    if (!ok) std::cerr << "[Application] The map " << start.map_name << " here is not the host's file" << std::endl;
    ok = ok && load_match(path, start.seed, start.roster, start.fog, start.mode, false);
    if (ok) {
        if (renderer_) renderer_->set_hud_team(local_player_id_);
        apply_team_names(start.names, start.roster);
        sim::apply_start_teams(sim_, start.teams());                         // (protocol 13: the teams of the Start, with the original's own commands, after init and the names and before the first tick: every machine, the referee and a machine that comes back from nothing do the same)
        begin_net_recording();                                               // (the runner's tap, attach_net, tells it every turn from the first, a catch-up's too)
    } else {
        recorder_.reset();
    }
    net_->report_loaded(ok);
}

// Once a frame (pump_network). A Start that waits for its map: carried on when the file is here, failed (the status line says why) when it cannot be had, dropped when the start was cancelled meanwhile.
// In the room before it: the room's map is asked for as soon as the room names it, so that it is here when the Start comes (no hash yet: the Start's asks again with it).
void Application::poll_map_fetch() {
    if (!map_fetcher_ || !net_) return;
    if (!map_wait_.empty()) {
        if (net_->phase() != net::NetGame::Phase::Loading || net_->start_info().map_name != map_wait_) {
            map_wait_.clear();
            return;
        }
        const MapFetcher::Status status = map_fetcher_->status(map_wait_);
        if (status.state == MapFetcher::State::Pending) return;
        const std::string name = map_wait_;
        map_wait_.clear();
        if (status.state == MapFetcher::State::Ready) {
            net_finish_load();
            return;
        }
        std::cerr << "[Application] The map " << name << " could not be fetched: " << status.message << std::endl;
        local_player_id_ = net_->my_seat();
        recorder_.reset();
        net_->report_loaded(false, status.message);
        return;
    }
    if (net_->phase() != net::NetGame::Phase::Room) return;
    const std::string& name = net_->room().map_name;
    if (name.empty() || name == map_asked_ || !net::valid_map_name(name) || listed(map_select_.get_maps(), name)) return;
    map_asked_ = name;
    map_fetcher_->request(config_.maps_dir, name, 0);
}

}  // namespace ants::app

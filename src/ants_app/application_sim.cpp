// The simulation inside the application (application.hpp): the orders that the input has given, the step of the engine, what follows a tick (the HUD, the computer players, the hash), the end of a match.

#include <iostream>

#include "ants_app/application.hpp"

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>

// Tells the page that embeds this game (web/lobby.html) the tick and the state hash (high word first, 16 hex digits). A game in a frame posts to its parent; a game in
// a window of its own posts on the broadcast channel "ants-sync" of its origin, with its room code, for the page that opened it or any page of the site that listens.
// EM_JS and not EM_ASM: the `$0` of EM_ASM is a warning under -Wpedantic.
extern "C" {
EM_JS(void, ants_post_sync_to_parent, (int seat, int tick, int hash_high, int hash_low), {
    var hash = (hash_high >>> 0).toString(16).padStart(8, '0') + (hash_low >>> 0).toString(16).padStart(8, '0');
    if (window.parent !== window) {
        window.parent.postMessage({ants: 'sync', seat: seat, tick: tick, hash: hash}, location.origin);
    } else if (typeof BroadcastChannel !== "undefined") {
        try {
            if (!window.antsSyncChannel) window.antsSyncChannel = new BroadcastChannel("ants-sync");
            window.antsSyncChannel.postMessage({ants: "sync", seat: seat, tick: tick, hash: hash, room: new URLSearchParams(location.search).get("room") || ""});
        } catch (e) {}
    }
});
}
#endif

namespace ants::app {

// The orders that input has given the predicted engine since the last look are felt from this frame (net::FeltDelayMeter). Every order of the player is a mouse button or a key, and those two
// handlers look when they are done.
void Application::note_orders() {
    const uint64_t taken = net_ != nullptr && net_->prediction() != nullptr ? net_->prediction()->stats().commands_predicted : 0u;
    for (; orders_seen_ < taken; ++orders_seen_) felt_delay_.on_order(frame_ms());
    orders_seen_ = taken;
}

sim::SimulationEngine& Application::view_sim() {
    return network_active() ? net_->view_engine() : sim_;       // (the confirmed engine while the prediction is off, and in every game of one machine)
}

void Application::update_simulation(float dt) {
    if (state_ == AppState::Loading) {
        tick_accumulator_ += dt;
        while (tick_accumulator_ >= 0.050f) {
            intro_ticks_++;
            tick_accumulator_ -= 0.050f;
            if (intro_ticks_ >= 30) {
                finish_loading();
                break;
            }
        }
        return;
    }
    if (state_ == AppState::QuickHelp) {
        return;
    }

    if (state_ == AppState::MapSelect || state_ == AppState::StartMenu || is_paused_) {
        return;
    }

    if (replay_mode_) {
        run_replay(dt);
        update_auto_camera(dt);
    } else if (network_active() && net_->phase() == net::NetGame::Phase::Playing) {
        // The ticks come from the lock-step runner (pump_network); only what shows between two ticks is advanced here
        tick_accumulator_ = static_cast<float>(net_->sub_tick_ms()) / 1000.0f;
        // The "Get ready" dialog of the match's start: no tick runs while it is up (the host seals the first turn kMatchStartDelayMs after the match began) and it ends with the first turn that
        // runs (post_tick). Meanwhile the HUD is stepped in real time, 50 ms at a time, so that its portrait moves and the wait does not look like a hang.
        tick_net_countdown();
        if (hud_.is_match_start_modal_active()) {
            start_dialog_clock_ms_ += static_cast<double>(dt) * 1000.0;
            while (start_dialog_clock_ms_ >= static_cast<double>(sim::TICK_MS) && hud_.is_match_start_modal_active()) {
                start_dialog_clock_ms_ -= static_cast<double>(sim::TICK_MS);
                hud_.update(view_sim().get_world_state(), 1);
            }
        }
    } else if (!network_active()) {                                  // (a game of the network that is not in a match, the second begin of a session while its NetGame starts again from nothing, has no tick of its own: its engine is the runner's)
        tick_accumulator_ += dt;
        while (tick_accumulator_ >= 0.050f) {
            if (hud_.is_match_start_modal_active()) {
                // A local game: the "Get ready" dialog counts REAL time, in the 50 ms steps of this clock (the HUD closes it after kMatchStartDialogMs of them), and the simulation waits for it: tick 0
                // runs on the step after the dialog's last one, the match's clock shows its full time behind the dialog, and the whole time of the match is playable (the original runs the clock
                // behind the dialog: a deliberate deviation, sim_engine.hpp)
                hud_.update(sim_.get_world_state(), 1);
                tick_local_countdown();
            } else if (!sim_.is_match_over()) {
                if (!local_match_reported_ && !network_active()) {              // the first tick of a game on this computer: told once (the web page has the server count it)
                    local_match_reported_ = true;
                    if (on_local_match_started_) on_local_match_started_();
                }
                sim_.tick();
                if (recorder_) recorder_->on_tick(sim_);
                post_tick();
            }
            tick_accumulator_ -= 0.050f;
        }
    }

    // Update spatial audio listener position
    audio_mixer_.set_listener_position(renderer_->camera().centre_world_x(),
                                       renderer_->camera().centre_world_y());      // (the middle of the world that the view shows, at any zoom: the exact middle, not the world pixel under a pixel's centre)

    if (renderer_) {
        renderer_->update_transient_effects(dt);
    }

}

// What one simulation tick shows: the HUD, the events of the tick, the sounds and, once, the end of the match.
void Application::post_tick() {
    // The first turn of a match of the network has executed: the ants can move now, and the "Get ready" dialog (which every machine opened when the match began, while the host waited for it)
    // is gone. (A local game's dialog ends before its first tick, in update_simulation.)
    if (hud_.is_match_start_modal_active()) hud_.dismiss_match_start_modal();
    const sim::SimulationEngine& shown = view_sim();        // (what the screen shows: with the prediction on, the predicted engine, a tick further for every tick)
    const auto& world = shown.get_world_state();
    hud_.update(world, 1);
    // The first tick after a catch-up: the engine queued the sounds and news of every turn it ran without a picture. They are not played or said now (the news flashes of the chat log stay).
    const bool replayed = catch_up_backlog_;
    catch_up_backlog_ = false;
    hud_.poll_sim_events(sim_, replayed);                    // (the news are the confirmed engine's: they are told once, whatever was shown before)

    if (page_hidden_) ++hidden_ticks_;                       // (the console's line about a hidden period says how far the match went)
    if (net_ != nullptr && net_->predicting()) felt_delay_.on_tick_shown();      // (what an order of this frame did is in the picture now: the next frame that is drawn shows it)
    net::Prediction* prediction = net_ != nullptr ? net_->prediction() : nullptr;
    if (prediction != nullptr && prediction->stats().cooldowns != prediction_cooldowns_reported_) {
        prediction_cooldowns_reported_ = prediction->stats().cooldowns;
        std::cerr << "[Application] The prediction of your orders switched itself off for " << (prediction->cooldown_ticks_left() * sim::TICK_MS + 999u) / 1000u
                  << " s: its work took longer than its budget too often. The match goes on as it did without it, and the prediction tries again afterwards." << std::endl;
    }
    auto audio_events = sim_.poll_audio_events();            // (drained in every case: the queue must not grow)
    if (replay_mode_) {                                      // the camera of a replay reads what the engine says (every seat's cues, not only the ones a player would hear) and the ants that were hit
        auto_camera_.set_speed(replay_speed_);
        for (const sim::AudioEvent& cue : audio_events) auto_camera_.observe_cue(cue);
        auto_camera_.observe_world(world);
    }
    if (replayed) audio_events.clear();
    std::vector<sim::AudioEvent> predicted_cues;
    if (prediction != nullptr || !cue_router_.idle()) {
        // The cues of the own ants' own actions come from the predicted engine, when it runs the tick that makes them (in step with the picture), and the confirmed engine's copies of them
        // are dropped when they come; every other cue is the confirmed engine's (net::CueRouter). The predicted engine's news are never told: the confirmed engine's are. The router runs
        // while there is a prediction, and after it is gone for as long as a cue that it played still waits for its copy; with neither, every cue is the confirmed engine's as it was.
        const auto owns_in = [this](const sim::SimulationEngine& engine) {
            return [&engine, me = local_player_id_](uint32_t ant_id) {
                for (const auto& a : engine.get_world_state().ants) {
                    if (a.id == ant_id) return a.player_id == me;
                }
                return false;
            };
        };
        if (prediction != nullptr) {
            predicted_cues = cue_router_.from_predicted(prediction->take_audio(), owns_in(shown));
            prediction->take_news();
        }
        audio_events = cue_router_.from_confirmed(std::move(audio_events), sim_.current_tick() - 1, owns_in(sim_));
    }
    if (!background_stepping_) {                             // a background step makes no sound: its events are dropped, not saved up
        audio_mixer_.ingest_simulation_events(predicted_cues, local_player_id_);
        audio_mixer_.ingest_simulation_events(audio_events, local_player_id_);
    }

#if defined(__EMSCRIPTEN__)
    // A page that embeds several games (web/lobby.html) shows that the machines stay in step: every 100 ticks the game tells its parent page the tick and the
    // state hash (the page compares the hashes of the same tick)
    if (network_active() && sim_.current_tick() % 100 == 0) {
        const sim::StateHash h = sim_.state_hash();
        ants_post_sync_to_parent(static_cast<int>(local_player_id_), static_cast<int>(sim_.current_tick()), static_cast<int>(h.total >> 32), static_cast<int>(h.total & 0xFFFFFFFFu));
    }
#endif

    check_match_over();
    if (bots_ && !sim_.is_match_over()) bots_->on_tick(sim_);        // the computer players, last: they act on the world that this tick made
}

void Application::check_match_over() {
    if (!sim_.is_match_over() || match_over_handled_) return;
    match_over_handled_ = true;
    finish_recording();                                          // (the replay is the match as it ended: every command and every tick are in it)
    const auto& world = sim_.get_world_state();
    scorecard_.show(world.match_result, local_player_id_, world.game_mode);       // "Waiting for scores..."; the cue plays when the rows appear (update_scorecard)
    update_picture();                                            // the results are the whole canvas (their wide page, or the original's own page)
    if (background_stepping_) pending_music_ = PendingMusic::Closed;     // (a hidden page changes no sound: the music closes when the page is shown)
    else close_music();                                          // FUN_010226da closes the music sequencer at once (0x1022714); nothing restarts it
    music_resume_on_activate_ = false;
    if (on_match_over_ && network_active() && !net_->join_target().room.empty() && net_->my_seat() < sim::MAX_PLAYERS) {      // (a server's room: the page may offer the rematch, with the key that is still kept)
        const std::function<void(const std::string&)> fn = on_match_over_;
        fn(match_over_json(net_->join_target().room, net_->my_seat(), net_->start_info(), world.match_result));
    }
    if (net_) net_->freeze();                                    // the host stops sealing turns (and the key is let go of: the room is over)
}

// Once per frame: a match that has ended without a tick of this application (a command, a drop-out) opens the results screen too; the screen's clock builds its
// rows, and with them the one cue of the machine, 250 ms after it opened
void Application::update_results(float dt) {
    update_picture();                                            // (the results screen is the whole canvas, whoever opened it)
    if (state_ == AppState::Playing) check_match_over();
    if (!scorecard_.is_open()) return;
    scorecard_.update(dt);
    if (mouse_has_moved_ && !pointer_outside_) scorecard_.handle_mouse_motion(mouse_screen_x_, mouse_screen_y_);   // the INPUT task's poll: the Leave button that appears under a resting pointer hovers
    const uint32_t cue = scorecard_.get_audio_to_play();
    if (cue > 0) {
        audio_mixer_.play_sfx(cue, 1.0f, 255);
        scorecard_.clear_audio_to_play();
    }
}

// FUN_0101453f: the quit dialog's Yes. With exactly one other side left (FUN_0100c5b1) the quit is the end of the match: the game-over message names the
// quitter, whose row goes last on every results screen, and the results (with Leave) follow. With more sides left the original sends the drop message and
// exits; here the player leaves the same way as before (the network announces the departure to the others).
void Application::confirm_quit() {
    if (replay_mode_) {                                          // (a recording is not a match to leave: back to the list)
        leave_replay();
        return;
    }
    // A match that is held (a seat is missing, the countdown runs, this machine is on its way back) seals no turn, and no session plays in the lobby phases of a match that starts again after a BadRequest (the
    // catch-up screen is up): a Quit command would be lost and the player would stay. The player leaves for good instead (NetGame::leave: the key is let go of, the others are told when the link is up).
    if (match_held()) {      // (protocol 21: the countdown card has a Quit button, and the first turn is not sealed yet either)
        leave_game();
        return;
    }
    if (state_ == AppState::Playing && !sim_.is_match_over() && sim_.other_sides(local_player_id_) == 1) {
        sim::Command quit_command;
        quit_command.type = sim::CommandType::Quit;
        quit_command.issuer = local_player_id_;
        if (network_active()) {
            net_->submit(quit_command);                          // it reaches the simulation with the turn that carries it, on every machine alike
        } else {
            if (recorder_) recorder_->on_command(quit_command);
            sim_.apply_command(quit_command);
            check_match_over();
        }
        return;
    }
    leave_game();                                                // (a network game that the start menu led to: back to the menu)
}

} // namespace ants::app

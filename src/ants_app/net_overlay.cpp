#include "ants_app/net_overlay.hpp"

namespace ants::app {

NetOverlayLine net_overlay_line(const NetOverlayInput& in) {
    NetOverlayLine line;
    if (in.desynced) {
        line.text = "Out of sync: the match has stopped.";
        line.alarm = true;
    } else if (in.electing) {
        line.text = "The host left. Choosing a new host...";
    } else if (in.stalled_ms >= NET_WAIT_MESSAGE_MS) {
        line.text = in.waiting_for.empty() ? std::string("Waiting for the other players...") : "Waiting for " + in.waiting_for + "...";
    } else if (in.catching_up) {
        line.text = "Catching up...";
    } else if (in.self_lag_behind_ms > 0) {
        line.text = "You are lagging (" + std::to_string((in.self_lag_behind_ms + 500u) / 1000u) + " s behind)";
    } else if (in.lag_seat >= 0 && in.lag_seat < 4) {
        const std::string name = in.lag_name.empty() ? "Player " + std::to_string(in.lag_seat + 1) : in.lag_name;
        line.text = name + " is lagging (" + std::to_string((in.lag_behind_ms + 500u) / 1000u) + " s behind)";
    } else {
        line.text = in.notice;
    }
    return line;
}

}  // namespace ants::app

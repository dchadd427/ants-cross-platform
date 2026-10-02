#pragma once

// The calls of the control interface (docs/NETWORK_PORT.md), as a function of the room manager: a lobby's backend makes, inspects and closes rooms with them.
// The HTTP server (ants::ctl::HttpServer) checks the bearer secret before this function is ever called; everything here is JSON in, JSON out.
//
//   POST   /rooms          {"map": "File name.lvl", "players": 2, "fog": false, "early_start": true, "code": "ROOM-1", "seed": 123, "wait_seconds": 120, "load_seconds": 60, "keep_seconds": 600, "max_run_seconds": 7200}
//                          -> 201 {"code": "...", "state": "waiting", ...}   (code and seed are optional: the server draws them; "early_start" is true unless it says false: the room's
//                          leader, the first player who joined, may then start the match with the players who are there, two at least, protocol 7)
//   GET    /rooms/<code>   -> 200 the room's status; 404
//   GET    /rooms          -> 200 {"rooms": [status, ...]}
//   DELETE /rooms/<code>   -> 200 the status after the close; 404
//   GET    /stats          -> 200 {"rooms": n, "pending": n, "created": n, "refused": n}
//
// A status: {"code", "state": "waiting|loading|running|finished|failed", "map", "fog", "expected", "joined", "early_start", "leader": seat | null (while the room waits or
// loads), "ignored_start_requests", "players": [{"seat", "name"}], "ticks", "turns", "age_seconds", "reason",
// "result": {"quitter": seat | null, "rows": [{"names": [..], "score", "lost", "killed", "hatched", "winner"}]}}

#include <cstdint>
#include <string>

#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"
#include "ants_server/room_manager.hpp"

namespace ants::server {

ctl::JsonValue status_to_json(const RoomStatus& status);
/// Reads a RoomSpec from the JSON body of POST /rooms; false with a reason when a field is missing or of the wrong kind
bool spec_from_json(const ctl::JsonValue& body, RoomSpec& out, std::string& error);

ctl::HttpResponse handle_control(RoomManager& rooms, const ctl::HttpRequest& request, uint32_t now_ms);

}  // namespace ants::server

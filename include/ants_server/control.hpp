#pragma once

// The calls of the control interface (docs/NETWORK_PORT.md), as a function of the room manager: a lobby's backend makes, inspects and closes rooms with them.
// The HTTP server (ants::ctl::HttpServer) checks the bearer secret before this function is ever called; everything here is JSON in, JSON out.
//
//   POST   /rooms          {"map": "File name.lvl", "players": 2, "fog": false, "early_start": true, "code": "ROOM-1", "seed": 123, "wait_seconds": 120, "load_seconds": 60, "keep_seconds": 600,
//                           "max_run_seconds": 7200, "reconnect": true, "hold_vote_seconds": 30, "max_pause_seconds": 1800, "max_catch_up_seconds": 300, "resume_countdown_seconds": 10, "record": true}
//                          -> 201 {"code": "...", "state": "waiting", ...}   (code and seed are optional: the server draws them; "early_start" is true unless it says false: the room's
//                          leader, the first player who joined, may then start the match with the players who are there, two at least, protocol 7. "reconnect" (protocol 10): the room
//                          holds the seat of a player whose connection is lost, pauses the match for everybody and takes the player back with its key; what the server was started with
//                          (on unless --no-reconnect) unless the body says; "hold_vote_seconds" 5 .. 3600 (default 30): the others may vote on going on without a seat once it has been
//                          away this long in all; "max_pause_seconds" 60 .. 86400 (default 1800): the match's total pause is capped, at the cap every seat that is not present is dropped;
//                          "max_catch_up_seconds" 10 .. 3600 (default 300): one absence may spend this long catching up in all, over all its attempts; "resume_countdown_seconds" 0 .. 60
//                          (default 10, 0 = none): after a pause of 3 s or more the match is held this long before it goes on; "record" (default true): the match is kept as a replay when the server keeps
//                          replays; false: this room's match is not)
//   GET    /rooms/<code>   -> 200 the room's status; 404
//   GET    /rooms          -> 200 {"rooms": [status, ...]}
//   DELETE /rooms/<code>   -> 200 the status after the close; 404
//   GET    /stats          -> 200 {"rooms": n, "pending": n, "created": n, "refused": n, "log_bytes": n, "log_budget_bytes": n}
//   GET    /replays[?limit=N]    -> 200 {"enabled": bool, "count", "bytes", "keep_days", "max_bytes", "replays": [{"file", "bytes", "ended" (seconds since 1970 UTC), "readable", "map", "players": ["Green", "Red (Bot (Medium))"],
//                                    "turns", "seconds", "finished", "game", "rules"}, ...]} the newest N (default 200, at most 1000) of the matches that the server keeps (replay_store.hpp)
//   GET    /replays/<file> -> 200 the .antsrep file (application/octet-stream); 404
//   DELETE /replays/<file> -> 200 {"deleted": "<file>"}; 404; 500 (the file is in the list but could not be deleted)
//   (with --replay-port the server also has a PUBLIC, read-only door with no secret: GET /replays -> 200 {"replays": [...], "count", "keep_days"}, the newest 200 files that this build can read, each as above
//   but without "readable", and "count" is the number of those files; GET /replays/<file> -> the file of one of them; 404 for everything else: handle_public_replays)
//
// A status: {"code", "state": "waiting|loading|running|finished|failed", "map", "fog", "expected", "joined", "early_start", "leader": seat | null (while the room waits or
// loads), "ignored_start_requests", "seat_moves" (the colours the leader moved a player to), "ignored_seat_moves", "players": [{"seat", "name"}], "ticks", "turns" (one per tick: turns of 50 ms, protocol 8), "age_seconds", "reason",
// "reconnect": bool, "hold_vote_seconds", "max_pause_seconds", "max_catch_up_seconds", "resume_countdown_seconds", "paused": bool (the match is held: a seat is missing, or the countdown
// after a pause runs), "resume_seconds" (the seconds that are left of that countdown, 0: none), "absent": [{"seat", "name", "state": "absent|catching_up", "away_seconds",
// "progress"}] (the seats that are missing, longest away first), "vote": {"seat", "continue", "voters"} | null, "paused_seconds" (the match's total pause so far), "rejoins",
// "drops_by_vote", "drops_by_cap", "rejoins_refused" (Hellos with a key that a budget refused), "catch_up_expired", "streamed_bytes" (the log, streamed to returning players), "log": {"turns", "bytes", "usable"} (the turn log that a returning player is given; it is freed when the match is over and keeps what it held),
// "record": {"kept": bool (a restart of the server would bring this match back: restart_record.hpp), "bytes", "note" (why not: the server keeps no records, the room holds no seats, the disk
// refused, the turn log passed its limit, ...)}, "replay": {"kept": bool (the match is kept as a replay: replay_store.hpp), "file", "bytes", "note" (why not: the server keeps none, the room was made with "record": false,
// the match was too short, ...; empty while the match may still be kept)}, "restored": {"turns", "replay_ms", "state_hash"} | null (the room came back from a restart record: the turns that were replayed, how long it took,
// and the referee's state hash at the restored tick),
// "result": {"quitter": seat | null, "rows": [{"names": [..], "score", "lost", "killed", "hatched", "winner"}]}}
// All the reconnect and record keys are additions: a lobby that ignores them works as before. A seat's key is never in any of it, nor in a result file, nor in a log line. A match that a restart could
// not bring back (another network protocol, a changed map, ...) is a FAILED room whose "reason" says so, from the moment the server has started.

#include <cstdint>
#include <string>

#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"
#include "ants_server/room_manager.hpp"

namespace ants::server {

/// The status of a room as JSON. `in_list`: the entry of GET /rooms, which leaves out the room's `replay` object unless the match was kept (an answer is at most HttpServer::kMaxResponseBytes, 1 MiB, and a
/// list of a thousand rooms, the restore check of tests/scripts/test_ants_server.sh, has little to spare: the note of a room that is not kept is in its own status).
ctl::JsonValue status_to_json(const RoomStatus& status, bool in_list = false);
/// Reads a RoomSpec from the JSON body of POST /rooms; false with a reason when a field is missing, of the wrong kind or out of its range. `out` holds the defaults when it is called (the
/// server's own, RoomManager::default_spec; a fresh RoomSpec is the built-in ones): only what the body says is changed, and on failure `out` is left as it was.
bool spec_from_json(const ctl::JsonValue& body, RoomSpec& out, std::string& error);

ctl::HttpResponse handle_control(RoomManager& rooms, const ctl::HttpRequest& request, uint32_t now_ms);

/// The routes of the replays that handle_control hands over (/replays and /replays/<file>; the secret is checked before)
ctl::HttpResponse handle_replays(RoomManager& rooms, const ctl::HttpRequest& request);

/// The public door of the replays (ants_server --replay-port, ctl::HttpServer::listen_public): GET /replays (the newest 200 files that this build can read) and GET /replays/<file>, nothing else, no query. Every
/// other request is 404. What a file or the list says of the players is a colour, or "Bot (Medium)": never a name that a person typed.
ctl::HttpResponse handle_public_replays(const RoomManager& rooms, const ctl::HttpRequest& request);

}  // namespace ants::server

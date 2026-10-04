# Site statistics: the numbers of the front page (state of the branch `site-stats`)

Handover notes of the branch: what it does, what was checked, what is left. Short on purpose; the details are in the tests and in `docs/NETWORK_PORT.md` ("The site statistics").

## What the branch does

- **`SiteStats`** (`include/ants_server/site_stats.hpp`, in `ants_server_core`): `online` (a match that ran one tick and ended, any room, counted when `RoomManager::take_ended` tells its end) and `local` (a single-player game that a browser reported), each as the last 24 hours (24 hourly buckets on the server's clock) and in all, plus `since` (the UTC day the counters were made). At most 120 reports count in any 60 seconds. One file, `site-stats.json` of `--results-dir`: written whole and renamed, at most every 10 s while something changed and at a clean stop; a file that is no usable file is put aside as `.broken-<seconds>`.
- **The door** (`ants_net/ws`): `GET /stats` beside `/busy` (`more_status`), and one POST path (`post_path`, `post_action`) that takes no body and no query and answers 204; the parser stays pure and says `posted`, the listener runs the action once.
- **`ants_server`**: the wiring in `main.cpp` (count every ended room, the two endpoints, `save_if_due` each pass, `save` at the stop).
- **nginx**: `/stats` (GET, two a second per address) and `/stats/local` (POST, six a minute per address with a burst of two, never a body passed on).
- **The web game**: `Application::set_on_local_match_started`, told at the first tick of a game on this computer (once for each match, never in a match of the network); the web build sets it to `antsReportLocalGame()` of `web/shell.html`.

## Verified

- Quick tier `./run_tests.sh --fast` on the branch's last code commit: 52 suites, 0 failed.
- `test_ws` W1.25, `test_server` S3.140 - S3.149, `test_app_integration` 7.6h, `test_network_app` N5.90, `test_ants_server.sh --part rooms` (77 checks), `test_nginx_stats.py` (the two blocks in front of a stand-in server, in a real nginx container), `web_stats_check.js` (38 checks), the routes check against a real nginx (199 checks).
- Mutants (`tools/mutate.py`, specs not in the repository), 180 in all: `SiteStats` 82, the door 28, nginx 41, the page's function 13, the application's hook 6, the server's wiring 10. The first round let nine through (six tests added, two dead lines removed, see the commit); the last round: every mutant caught, except that the hook's `!network_active()` guard is caught only by the text that `test_web_stats.py` pins (no state of a network match reaches it).
- AddressSanitizer + UBSan (`build_asan`, no optimisation): `test_server` 120 cases, 74038 assertions, 0 failed, no report (S3.140 - S3.149 among them); the real `ants_server` through the statistics sequence of the end-to-end script (130 reports, a restart, a broken file, a place that cannot be written, no results folder): as expected, no report, clean exits; `test_ws`: W1.24 and W1.25 pass, no report, and W1.18 (the ping flood's own limit of 1.5 s of server time) took 1.7 s in that unoptimised build, which has nothing to do with this branch. The `--asan` configuration with `-Werror` does not compile here at -O0 (GCC 13.3: `include/ants_sim/match_stats.hpp:159`, a `-Wsign-conversion` error that is there without this branch), so the build ran without `-Werror`.
- The web build's part of `application.cpp` (the `EM_JS` hook and its installation) compiles under `emcc` 3.1.58 with the repository's warning flags (syntax only, with the host's SDL headers: the SDL port could not be fetched here).

## Not verified, and worth knowing

- **The web image was not built here** (the Emscripten link, the page in a real browser): the first `antsReportLocalGame` that reaches a server is the CI's web image and the owner's browser. `tests/scripts/web_stats_check.js` runs the function with a fake `fetch`, nothing more.
- **Per-address limits behind another proxy.** The site's nginx sits behind a reverse proxy that ends TLS (`README.md`, `docker-compose.stack.yml`) and `docker/nginx.conf` has no `real_ip` setting, so `$binary_remote_addr` there is the proxy's address unless the proxy is set up to keep the client's: then the six reports a minute of `/stats/local` are the whole site's, and the single-player number falls short on a busy day. The server's own cap (120 a minute) is the limit that matters; if the owner's proxy passes the client address, a `real_ip_header` makes the nginx limit per visitor.
- A room that the owner names `site-stats` would write its result file over the counters' file (`<code>.json`); the next start puts it aside. Not guarded.
- A match that a stop of the server ends (a room without a restart record) is not counted; a restart does not reset the cap's minute.

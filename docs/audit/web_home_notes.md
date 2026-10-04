# Web home: the front page with single player in it, the windowed margin (state of the branch `web-home`)

Handover notes of the branch: what it does, what was checked, what is left. Short on purpose; the details are in the tests and in `docs/NETWORK_PORT.md` ("The front page ...", "The windowed margin").

## What the branch does

- **The front page is the lobby.** `web/four.html` became `web/lobby.html` (title "Ants (1998)"), served at `/`. The game page (`web/shell.html`, built to `index.html`) also answers at `/play.html` (a copy made by the Dockerfile).
- **Address rules** (`docker/nginx.conf`, checked by `tests/scripts/web_routes_check.py` in a container): `/` is the lobby, except that an address with a non-empty `join=` or with `embed=1` opens the game page, so every old game link works; `/four.html` with any query answers `301` to `/` with the same query; the lobby's state addresses (`?room=`, `?map=`, `&players=`, `&fill=`, `&play=here`, `&aspect=`) work at `/`; the headers of the html pages are as before (no-cache, must-revalidate, ETag, cross-origin).
- **Players 1 - 4.** 1 plays on this computer in THIS tab (button "Play", row "Opponents": None / Easy / Medium / Hard, remembered under `ants-solo-bots`): the page opens `play.html?map=<key>[&bots=<level>]&name=<name>&aspect=<shape>`. 2 - 4 are "Create the match" as before; Join and the host's own seat play in this tab; "All seats in separate windows" stays. First visit: Treasure, 1 player, Medium bots.
- **The game page** reads the local parameters through a whitelist (`ANTS_PAGE.localArguments`: the map by key, the bots' level, a name of printable ASCII up to 32 characters; no parameter, no argument). Its header "Play online" is now "Menu" (back to the front page in the same tab, asking first during a match). One catalog name on both pages: "Sprites and sounds" (short "Sprites").
- **`--play`** (C++, `Application`): the setup screen's own START at its first visit (the "Get ready" dialog, the start sound, the music, the `--bot` seats), for a game of this machine without the start menu; a room and the menu ignore it; the direct `--map` start of the tests and screenshots is unchanged.
- **The windowed margin** (commit f7bb9bd): the map keeps scrolling while the pointer is within 96 CSS px of the game's box (corners as squares); beyond it, over a control, out of the window, after a scroll or resize it stops; not in frames, with touch, with a locked pointer or in fullscreen. The first mouse leave of a page's life (dropped by SDL) is now seen too.
- **Aspect check.** `web_aspect_check.py` failed 7 of 535 checks on the v0.2.0 web build because it tapped the 16:9 quick help's START at the 4:3 page's place. The page is right (START works for a person at both shapes); the check now computes START from the layout's own rectangle (`quick_help_start`, read from `src/ants_app/page_layout.cpp`) and opens the game page at `/play.html`.

## Verified

- Quick tier `./run_tests.sh --fast`: 48 suites, 0 failed (on the final tree of the branch).
- Node: `web_lobby_check.js` 127 checks, `web_name_check.js` 272, `web_edge_check.js` 106, 0 failures.
- nginx routes in a real nginx container: 184 checks; 8 mutants of `docker/nginx.conf` all caught.
- Margin: 70 browser checks; 9 mutants of `web/shell.html` all caught (31 of 68 checks failed on the old page).
- Front page in a real browser on the built web image (`web_home_check.py`, opt-in): 45 checks, 0 failed: Play goes loading screen, quick help, Enter, match with the "Get ready" dialog; the Medium bots' scores move within 45 s; nothing moves alone; Menu asks and No / Yes work; old addresses and the host's "Play in this tab" work.
- The web image builds; the version, build id and staging label substitutions work on both pages; the CI page check was updated and run by hand.
- Aspect check on the web image of the front page: the whole `web_aspect_check.py`, 535 checks, 0 failed (the image was built before two comment-only edits of `web/shell.html`).

## Not verified yet (the next steps, in this order)

1. Build the web image from the committed tree and run against it: `web_routes_check.py`, `web_home_check.py`, `web_edge_check.py`, and the whole `web_aspect_check.py` (535 passed on the earlier image).
2. Full-tier C++ cases written but NOT run locally: `AI6.7` (`test_network_app`) and `A1.1b` (`test_start_menu_app`); build the two programs with warnings as errors and run them.
3. Mutation proofs of `--play` (each must fail its case): remove the `start_game` call in `enter_map_select` (7.6d); drop `!networked` (AI6.7); drop `!menu_enabled_` (A1.1b); remove `if (cfg.play_at_once) cfg.start_in_map_select = true;` (M9.3); remove `mode_given = true` in the `--play` branch (M9.1); remove `play_pending_ = false` (7.6d's one-shot check).
4. Leak scan of the added lines of the branch (local paths, hostnames, tokens), then the CI run of the pull request.
5. Merge notes: `tests/scripts/test_ants_server.sh` line 101 still names `web/four.html` (another branch changes that line); two comments in `src/ants_app/application.cpp` (near lines 40 and 2030) still name `web/four.html`; `ants_probe` and `application.*` are also touched by the rollback branch (the hunks lie apart); `build_web.sh`'s local server shows the lobby at `/lobby.html` (its python server serves the game page at `/`); `run_tests.sh` needs no new line (the new `test_*.py` files are found by suite 5.2).
6. `VERSION`, `CHANGELOG.md`, `STATUS.md` and the version line of `README.md` are not touched (the release batch does them).

## Draft changelog lines

- The Play online page is now the front page, with single player in it: Players 1 - 4 (1 plays on this computer in this tab, alone or against Easy, Medium or Hard bots; 2 - 4 as before). `/four.html` goes to `/`; the game page's "Play online" button is now "Menu" (same tab, asks first during a match); local games open at `/play.html`; every old game link still works. First visit: Treasure, 1 player, Medium bots.
- The map keeps scrolling when the mouse goes a little past the game's edge (in a window), within about an inch (96 CSS px), corners included; it stops farther out, over a button or link of the page, or out of the window; clicks, the wheel and the menu there are the page's. Also: the first time the pointer left the game in a page's life the map used to keep scrolling.

# What’s new

What changed in each release of the Ants remake, newest first, a few lines each: what a player sees and, only when it changed, the rules or the network protocol. The version is the one line of the file `VERSION` (how it moves: [`docs/WORKFLOW.md`](docs/WORKFLOW.md)); every build also has a build id, the short git commit, shown by `ants --version`, `ants_server --version` and the footer of the web page.

An entry is 5 - 15 lines in a fixed template: the heading `## vX.Y.Z - YYYY-MM-DD - title`, then **For players:** (1 - 6 bullets), **Rules / network:** (only if the rules or the network protocol changed: what, and the protocol number), **Fixes:** (optional, one line each), **For operators:** (optional: what only the person who runs a server or the site sees) and **Details:** (a link to the commit range). The "What's new" window of the site shows the bullets under **For players:** and **Fixes:** and nothing else, so a line about the administration page, a server setting, an environment variable or the database goes under **For operators:**; a release that has only such lines has no **For players:** paragraph. No test counts and no mutation or review lists: those belong in commit messages and documents. A change that goes live writes its entry under **Next** (a section headed `## Next`, above the newest release) in its own pull request, and `tools/release.py` turns it into the entry when `VERSION` moves (each such change bumps the version, by default the patch number).

<!--
Template of an entry (copy it, keep the labels and the order, leave out a paragraph that has nothing to say):

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet: what a player sees or can do now

**Rules / network:** only when the rules or the protocol changed: what, and "network protocol N: a vA.B.C game cannot join"

**Fixes:**
- one line each (optional)

**For operators:** only for what a person who runs a server or the site sees (the administration page, a setting, an environment variable, the database); a release with only such lines has no "For players" paragraph
- one bullet each

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ed3f1214...b2150865)

Work that is not released yet is written in the same template under a heading that says only "## Next" (no version, no date), above the newest release;
`tools/release.py X.Y.Z "title"` turns that heading into "## vX.Y.Z - date - title" and refuses when there is no "## Next" or it is empty.
-->

## v0.22.1 - 2026-11-27 - Smoother ants online, fewer freezes, Show ping

**For players:**
- **Show ping.** A new setting in the Video tab of the settings menu, off by default: switched on, the corner of a network game shows "ping NN ms" above the frame rate, and a click on it opens the list of connections as before. Off, the corner is the frame rate and the version only. The corner no longer shows "delay". On the desktop it is `show_ping=on` in the settings file or `--show-ping on` for one run.
- **Tags keep your capitals.** A tag is now shown the way you type it ("[Ant] Zoe", "[aNt] Zoe"), no longer turned into capitals. The checks still read it in capitals (reserved, staff and rude words), tags that are already set stay as they are, and changing only the capitals counts as a tag change, so it waits a day like any other ([`docs/HUB_NICKNAMES.md`](docs/HUB_NICKNAMES.md)).
- Smoother ants in network matches: a corrected ant moves at a walk's pace at most.
- The Legal page has a new part, "Counting visits": no cookie and no address kept, and a browser that asks not to be tracked is counted as one visit and its country ([`docs/ANALYTICS.md`](docs/ANALYTICS.md)).

**Fixes:**
- Fewer short freezes in online matches: the server no longer lists its whole folder of replays in one go after every match it keeps, and starts the matches of computer players in a quiet moment while people play.

**For operators:**
- The log tells a slow pass of the server's main loop (over 25 ms) in one line with where its time went, `slow pass: 36.0 ms (rooms 5.1, store 21.0)`, at most one line in ten seconds ([`docs/SERVER.md`](docs/SERVER.md#the-slow-pass-log)). The replay store's look at its folder runs in 2 ms slices, and its own saves and deletes no longer call for one ([`docs/SERVER_REPLAYS.md`](docs/SERVER_REPLAYS.md)).
- The admin page has a new tab, **Analytics**; the hub has a door for it, `POST /hub/a`, and a file, `analytics.db`; `ANTS_HUB_ANALYTICS=off` switches it off. The hub's image fetches a DB-IP country list when built: pass `ANTS_GEO_MONTH` once a month. The counter keeps no time finer than the hour and names a referrer only by its registered name (and in the permanent totals only when 3 visitors came from it); a day keeps at most 30,000 events, one address range makes at most 50 new visitors a day, and `analytics.db` is limited to 64 MiB as `hub.db` is ([`docs/ANALYTICS.md`](docs/ANALYTICS.md)).
- For search engines the game page now names `/play` as its address (it still says `noindex`, so no `/play?...` address is listed), and a `.txt` file of the site, such as the licence text of the front page's font, sends `X-Robots-Tag: noindex` ([`docs/BUILD_AND_RUN.md`](docs/BUILD_AND_RUN.md#search-engines-and-share-cards)).

**Details:** [commits](https://github.com/dchadd427/ants-server/compare/bb9239fc...f3e8fea2)

## v0.22.0 - 2026-11-26 - Nicknames, tags and guest names, a welcome for guests, a better Play of the game and Fill a box

**For players:**
- **A better Play of the game.** It is now one player's best 10 seconds: 10 points a kill, more for knocking a player out, saving your hill from a Thief, stopping a Thief with loot and wiping out a whole group. The card says it ("Gory: triple kill in 8 seconds", "saved the hill").
- **Fill a box in the Map Maker.** A switch under the Objects, Food and Ants tools: drag a box and it fills with every cell, every other cell, every Nth row or column or a random pattern, as one Undo step. Water, rock and things in the way are skipped and counted; one fill puts at most 250 ants. An AI that you connect has it too (`fill_area`).
- **Nicknames, tags and guest names.** Log in and choose a nickname (3 to 16 letters and digits, yours alone) and, if you like, a tag ("[ANTS] Zoe") on one sheet, Your name, from the name in the chat menu. The chat, the Players list, the rooms, the results and the match history show it; finished matches keep the names they were played with. A guest can type a name too: it shows as "Wren (Guest)" and stays in this browser. Latin (with Vietnamese) and Cyrillic letters work. **A welcome for guests.** The first time you open Ants & Uncles without logging in, a card offers Sign in with Discord to chat, Set a display name if you do not want to chat, or Not now. It does not come back once you answer it, set a name or log in. **Long names fit.** The results show an alliance on two lines when its names are long, and a long name ends in dots in a room and on the countdown card.
- **The Games list fills itself with Open lobby cards.** As many as the window has room for with nothing to scroll (more on a tall window, none when real games fill the list), and the dashed plus on a card hosts a game, the same as Host here.

**Rules / network:** network protocol 29 (a name is UTF-8, and a name that is not a guest's carries the hub's signature, [`docs/HUB_NICKNAMES.md`](docs/HUB_NICKNAMES.md)): an older page or game cannot join a room of this version, so reload the page. The rules and the recordings are unchanged.

**Fixes:**
- The live match cards of the Games list and their scoreboard name a computer player "Kit (bot)", never with its level. A connected AI's `put_ants` puts the ant type that it names, and `get_region` names the types right (they were mixed up: asking for a combat ant gave a bomber, and a swimmer gave a combat ant).

**For operators:**
- Moderators have `/rename NAME`, which takes a nickname and a tag away. The key `ANTS_ADMIN_GATE_SECRET` also signs the pass of a name: with it the game server keeps a nickname only when the hub signed it (without it names are checked as before). The hub adds its tables `nicknames`, `nick_hold` and `login_claim` and the column `lines.tag` by itself.

**Details:** [commits](https://github.com/dchadd427/ants-server/compare/7e54f31a...8f1fafc9)

## v0.21.6 - 2026-11-26 - Live scores, open lobbies, Top Thief and bots that fight for the flower

**For players:**
- **Top Thief** replaces Biggest raid on the results screen: the player with the most raids whose loot got home. 187 keeps Last stand.
- **Games list.** Dashed Open lobby cards fill the empty places (Host here), and a live match shows its scores in a strip that opens a scoreboard.
- **Copy link on a phone.** A live view and a replay have the button in the top bar on a phone now (a share symbol, 44 px; on a computer it says Copy link as before). It opens the phone's share sheet where there is one, otherwise copies the match's address and says Copied!. The bar shows Matches and Full instead of All matches and Fullscreen on a narrow phone, so that all five buttons fit in one row.
- **Kills first on the Podium.** The results show Killed before Lost (then Hatched), for the winner and for each of the other players.
- **Computer players fight for the flower and get stuck less on ISLANDS.** Hard bots strike at the ants of other teams that wait with them at a flower's strip when they have the edge, and win more of the flower's power-ups (ISLANDS: 3.3 a bot and match against 2.9). An ant with food or loot that meets another team's ant at the one tile of a bridge's landing steps back and lets it pass, and an ant that holds a load far from home gets its bridge from one Swimmer. Standing on a power-up when hurt and fighting only with an edge were built and measured too; they did not win, so they are off. Bots only.

**Fixes:**
- On a phone, a live view's "Jump to live" no longer covers the clock beside the play button: the clock keeps its room, the button goes under it when the row is short, and Picture sits beside the camera button. The speed buttons and Picture are 44 px high.
- The menu's "players online" counts the same people as the Players list, not only those in a room or a match.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/717a2ef9...af4a8282)

## v0.21.5 - 2026-11-24 - The Podium results, messages on phones and smarter bots

**For players:**
- **Computer players stand idle less.** On ISLANDS Hard bots take the Thief and Fire hats and dig to a rival's hill to steal; at every level ants that meet on a bridge sort it out. Bots only.
- **A new results screen.** With the New HUD the end of a match is a page that fits any window or phone: Victory! or Defeat, the winner and the others ranked, the damage, three honors and a Play of the game, with Watch on the desktop. "All stats" opens every player's numbers and "Back to results" returns (the arrow keys work too).
- **You choose after a room's match.** The results of a match that began in a lobby room have two buttons, "Back to the room" and "Back to Ants & Uncles", and wait until you press one.
- **Messages are under the bell.** The bell's list has two tabs, **Alerts** and **Messages** (your conversations, with a count on the tab), and the bell's number counts both, so your messages are one press away from every view and every size. A conversation opens from there: its arrow comes back to the Messages tab and its X closes everything. The Messages button of the chat is gone. The messages and their report card have the site's own look (the dark panels, green frames and steel buttons) instead of indigo.

**Fixes:**
- Private messages on a phone: the conversation is a sheet that follows what you can see, so with the keyboard open the field sits just above it, the head with the name and the X stays on the screen and the messages fill the room over the field. The X, the arrow, Escape, a pull down on the head and the phone's Back button each close the sheet and leave you on the page where you were, not on the bell's list.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/df4b4255...36deb771)

## v0.21.4 - 2026-11-24 - Special ants on the match page, What's new fits a phone

**For players:**
- **Special ants on a match's page.** A new section under the Scoreboard shows what each type of ant did: a card for each type that somebody had (Swimmer: bridges built and taken down; Fire Ant: fires lit and put out; Bomber: bombs set, defused and gone off; Thief: raids that brought loot home, "5 of 8", and the loot), and a card for food and fighting. A type that nobody used is left out with one line. Bombs set, Defused and Fires lit moved there from the Scoreboard. A match played before this release shows what was counted then (no bridges, fires put out or loot).

**Fixes:**
- On a phone, the "What's new" window fits the screen, so its Close button is no longer below the bottom of the page.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/3430f45a...e6b312bf)

## v0.21.3 - 2026-11-24 - Follows, private messages, replay likes and the activity curve

**For players:**
- **Where the action is, on a replay.** A soft curve over the replay's timeline shows the fights: deaths most of all, then bombs, thefts, fires and hits. On a computer it shows while the pointer is over the timeline, with the time and what happened ("2:41 · 3 deaths, 1 bomb"); on a phone it is always there, faint, and solid under your finger. The timeline has a row of its own on a computer now, as wide as the picture. Matches played before this version show a curve of their deaths alone.
- **Plays and likes of replays.** A replay counts as played after 10 seconds of playing (pauses do not count), once a day in a browser. The list of matches and a match's page show how often it was played and how many people like it; a logged-in person taps the heart to like and taps again to take it back, and nobody sees who liked. New sorts, **Most watched** and **Most liked**; the Sort box is wider so no name is cut. Not logged in, a tap on a heart says "Log in to like a replay."
- **Follow** a player from the same menu (or **/follow NAME**): a **Following** tab in the players list, a star by their name, and an alert with Join when they open a public room. Nobody is told that you follow them, and nobody sees who follows whom.
- Write to a player **privately**: **Message** in the menu, a **Messages** button in the chat (a panel, or a sheet on a phone), a count and a bell row for each new message. Messages are kept for 7 days and only the two of you can read them; **Ignore** blocks a person, and **Report** sends a message and the 5 before it to the admins.

**Fixes:**
- Watching a replay on a match page has the Settings button too, and a match you played shows on the Matches page right after it ends, not only when the server is idle.

**For operators:**
- The game server's database moves to schema version 12 (the table `history_action`, the counts that the activity curve is drawn from, and the plays and likes of replays; the history service's own file to its version 6): an older program refuses the newer file, and the update does not copy it, so stop the server and copy `ants.db` before the update; that copy is the way back ([`docs/HEATMAPS.md`](docs/HEATMAPS.md#the-activity-curve), [`docs/REPLAY_COUNTS.md`](docs/REPLAY_COUNTS.md)).
- Plays and likes of replays: a new door of the game server (`--counts-port`, 4032 in the stack files; [`docs/REPLAY_COUNTS.md`](docs/REPLAY_COUNTS.md)) and a like gate of the hub that signs a short pass for the person's login (the admin page's key, `ANTS_ADMIN_GATE_SECRET`; without a good one likes are off and plays still count). Only numbers are kept for plays, and for a like the hub's opaque reference of the account. **Forget me** asks the door to take the person's likes back (`ANTS_HUB_COUNTS_URL`, `http://ants-server:4032` in the stack; empty it when `ANTS_COUNTS_PORT` is 0) and never depends on it: a door that does not answer is asked again by the hub for a day.
- New `follows`, `dms` (private messages, 7 days) and `reports` tables, made by the first use and deleted by Forget me; reports are read with `python -m hub.admin reports` and closed with `report-close ID`, and each report, look and close is a row of the moderators' log ([private messages](docs/HUB_MESSAGES.md)). The Legal page has a section for them.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5e23309d...266e2c4c)

## v0.21.2 - 2026-11-23 - Invites, mentions, the bell, map pages and Random map

**For players:**
- The bell shows your recent alerts, newest first, with the number of unread ones: invitations, mentions, people who joined your room and your own finished match (with a View link). The settings are behind a gear in that list; a visitor keeps a short list in the browser.
- Right-click a player in the list, or a name in the chat (long-press on a phone), to **Invite to my room**. They see it as a line in their chat (a toast when the chat is not on screen) and in their bell, with Join; it turns grey when the room closes.
- **/invite NAME**, with a helper that narrows the names as you type.
- Type **@** in the chat to pick a player: a mention is a pill for everyone, gold for the person named, and an alert in their bell. No @here, no @everyone.
- **Random map.** The map list of a room has a **Random map** row under the sorts: **Pick one** puts a random map from the maps shown into the room at once, **Pick another** picks again. The Maps page has a **Random map** button that opens the page of a random map of the whole library.
- **A page for every map.** The six maps of the 1998 game and the players' maps each have a page at /map/NAME (picture, size, game length, food and power-ups, the map's own line, the maker when its line names one, and how often it was played here) with **Play it**, which opens a room on that map, and **Open in Map maker**; /map-library lists them with a search and filters. The Map Maker opens a library map from /mapmaker?mapfile=NAME.lvl.

**For operators:**
- The hub keeps an account's last 30 alerts of 7 days and its `/ignore` list (new `alerts` and `ignores` tables, made by the first one; Forget me deletes them), and limits invitations and mentions ([the hub's document](docs/HUB_PEOPLE.md)).
- The map pages are made when the image is built (`tools/map_pages.py`, [`docs/MAP_PAGES.md`](docs/MAP_PAGES.md)) and listed in `sitemap-library.xml`; the maps that players send and you approve have no page yet.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/dc2bfafd...e0d5b1bf)

## v0.21.1 - 2026-11-23 - Connect an AI in the Map Maker

**For players:**
- **Connect an AI in the Map Maker.** A new Connect an AI sheet makes a key (look only, or look and edit; it ends after 7, 30 or 90 days) for your own AI assistant (Claude Code, Cursor, any program that speaks MCP or plain web calls). Turn on "Let an AI work in this tab" and it can look at the map you have open and, with an edit key, change it while you watch: a strip with Stop and Undo AI changes stays on top, and each change is one step of your own history.
- **Six skills for your AI**, to download from /ai/skills/ (one zip with all of them): the map file's words, the terrain, the layout of a fair map, the Map Maker's tools, and how to check and fix a map.

**For operators:**
- The keys are off until the hub is started with `ANTS_HUB_AI_KEYS=on`; with it off every key door answers 503 and no tab can be armed ([`docs/MAP_AI.md`](docs/MAP_AI.md)).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2bdfe98e...d65954fa)

## v0.21.0 - 2026-11-23 - A new room, Fog of War switch, the can't-go gap

**For players:**
- **A new, simpler room.** The colors are slim rows with the map card and one big START! or READY button beside them; Share, Rules and Leave sit in the room's header, and a press on an open color opens a short menu (Open, Easy, Medium or Hard bot, Closed). Match history is in the top bar.
- **Phones.** Held upright, the room shows the colors as big tiles, two by two, with a sheet for each color; START stays in reach at the bottom.
- **Fog of War switch.** The room's leader can turn Fog of War on in the Rules sheet; everyone sees it, and the Games list marks rooms with fog. Computer players and fog don't mix yet.
- **The can't-go gap, as in the original.** A Thief shut out of its hill can be clicked free in the short gap between its "Can't go there" clips, the way the 1998 game allows.
- **Computer players get unstuck.** They take a power-up that blocks the way, stop repeating refused clicks, free a walled-in Thief with a Fire Ant, and click a looping Thief away like a person would (Hard).

**Rules / network:** rules number 14 (game mode 187: 15): the path manager answers an event's request after the turn's commands, as in the original. Network protocol 28 (the Fog of War byte in the leader's plan): a v0.20 game cannot join a v0.21 room. Older recordings still play.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5ed83928...d0e1305a)

## v0.20.6 - 2026-11-22 - 3D ant models test page

**For players:**
- A test page at /test_3d/ shows stylized 3D models of the six ant types, with their animations, next to the original 1998 sprites.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/857ce588...3519146b)

## v0.20.5 - 2026-11-22 - Real error pages, clean addresses, share cards

**For players:**
- The desktop game has the original Ants icon too.
- Chat images in the installed app: Share, Copy link and Save, instead of a link that trapped you in the app; Copy link in the browser as well.
- An address that does not exist shows a "Page not found" page instead of starting a solo game; old .html addresses go to their clean ones.
- "What's new" replaces "Changelog" in the menus.
- Links to the site show a preview card, and search engines get a proper title and description for each page.

**Fixes:**
- The moderator menu on a chat image says "An image" instead of an empty box.

**For operators:**
- The web image builds with Emscripten 3.1.74 and the server image runs on Debian 13; /robots.txt and /sitemap.xml are real files.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a56438f6...236540e7)

## v0.20.4 - 2026-11-21 - Encrypted desktop joins, fairer limits, the original icon

**For players:**
- The site's icons are the original game's own icon, on a dark tile on a phone's home screen.
- Computer players on HELLRAISER and maps like it no longer stall: allies stop defusing each other's bombs and Bombers stop blocking each other.
- The desktop game joins online games over an encrypted connection (wss), with the site's certificate checked.
- A big match is no longer lost from Matches: a recording can be up to 16 MB.
- A picture in the chat has a frame as wide as the picture on a phone.

**For operators:**
- Orders are limited per seat (a large allowance for armies, a small one for other commands); a failed recording names the seat that sent the most.
- Limits per network address: 16 open rooms, 10 new rooms a minute, slower room-code guessing; `ANTS_TRUSTED_PROXIES` names the proxies whose address header is believed.
- The stack files no longer publish the unencrypted TCP game port 4001.
- Images are pinned by digest, Pillow 13.0.0 by hash, and Dependabot watches home-next.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/e615bc78...33515c3d)

## v0.20.3 - 2026-11-21 - Images in the chat can be posted

**Fixes:**
- Images in the chat can be posted: the chat service could not write its new images folder, so posting stayed off after v0.20.2.

**For operators:**
- The hub's image makes its /images folder its own, so a new or still-empty images volume is writable by the hub (no manual step on the host).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/bdc4b393...0f09b20b)

## v0.20.2 - 2026-11-21 - Images in the chat

**For players:**
- **Images in the chat.** People with the Active Anters role, moderators and administrators can paste, drop or pick an image (or an animated GIF) in the global chat, with an optional caption; readers see a thumbnail that opens full size. GIFs play while on screen, and a switch (or your device's reduced-motion setting) stops them.
- **Home on a phone held sideways.** Short windows now get the phone layout too, so the room fits without a cramped scrolling panel.

**Fixes:**
- Every image is checked and saved again by the site before anyone sees it, which removes hidden data such as a photo's location.

**For operators:**
- A new container, ants-img, converts images on a private network; the hub reaches it through ANTS_HUB_IMAGE_CONVERTER_URL (on by default; set it empty to turn images off). Who may post: ANTS_HUB_ROLE_IMAGES plus administrators and moderators; administrators take the right away with /noimages and give it back with /images (docs/HUB_IMAGES.md).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/88d22002...ea0bfcf6)

## v0.20.1 - 2026-11-21 - Look around during the countdown

**For players:**
- **Look around during the countdown.** Before a match begins you can move the view (drag the map, the minimap, the edge, the wheel or a finger); the card turns see-through while the view moves. Nothing can be selected or ordered yet, and Fog of War shows nothing new.
- **A new countdown card** with each player's connection: one number per player in its color of quality; click the ping in the corner for the list of connections.

**Fixes:**
- In a server's room the desktop game no longer shares its network address with the other players or opens a port for them.

**For operators:**
- The desktop game's listener for host migration in a LAN game takes at most 8 waiting connections, each for 5 s.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ee60e4bf...1a591c31)

## v0.20.0 - 2026-11-20 - The room inside Home, Ready up

**For players:**
- **The room lives inside Home.** A room you host or join takes the place of the Games list, and the chat has two tabs, Ants & Uncles and Room, with a dot on the one that has something new. Home fits the window without scrolling from 1280x800 up.
- **Ready up.** In a room, everyone except the host presses Ready; the host's START waits until all are ready and shows who is not.

**Rules / network:** network protocol 27 (Ready up): a v0.19 game cannot join a v0.20 room.

**Fixes:**
- The "Leave the game?" box and the box that copies a recording's link no longer make the game's sound repeat loudly while they are open.
- A match whose player lost their connection during the countdown no longer stays stuck before the first turn: after 60 s the missing seat is dropped and the match goes on.

**For operators:**
- The relay hands a credential only once a match with two or more people has begun, at most two per player, lasting as long as the match can plus 10 minutes; it listens and relays on ANTS_TURN_EXTERNAL_IP only (an address the machine must hold) and writes its pid file to a small memory folder.
- New server option --countdown-hold-seconds (default 60; control key countdown_hold_seconds).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5cd7452b...faa0179c)

## v0.19.3 - 2026-11-20 - Commands in the chat

**For players:**
- **Commands in the chat.** Type "/" to see the commands you can use, with a helper that fills them in. Everyone has /help, /ignore and /unignore. A mistyped command is answered only to you, and "//" at the start posts a line that begins with a slash.

**For operators:**
- Administrators can start computer-player matches from the chat with /bots (up to 10,000 in a queue, run 80 at a time with a load guard), see them with /bots status and end them with /bots stop. The hub reaches the game server through ANTS_HUB_GAME_ADMIN_URL (docs/HUB_COMMANDS.md).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/0f2ebe70...9454806f)

## v0.19.2 - 2026-11-20 - Invite only rooms, fewer stuck computer players

**For players:**
- **"Invite only" instead of "Private".** A room you keep to yourself shows in Open games, locked, and people join only with your link; the switch and every message now say so.
- **Computer players on POPcOrN get stuck less.** Fewer seats end a match without a point, and they take more power-ups.

**For operators:**
- A schedule on the admin page can space its matches out ("Start one every X minutes"), like Start now.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a89fd745...cd31da55)

## v0.19.1 - 2026-11-20 - Numbers over the ants without a border

**For players:**
- **Health, damage and heal numbers have no black border.** The heal number now shows white digits with a green plus, so it reads on pale ground and over a green ant.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ceabe493...cf1bbe63)

## v0.19.0 - 2026-11-20 - Smoother ants online, a direct-connection switch

**For players:**
- **Other players' ants move more smoothly online.** During a match your game hears the other players' orders a little earlier, through a relay, so their ants jump back into place less often. The server still decides everything, exactly as before.
- **A "Direct connection" switch in the room**, off by default. With it on for both of you, the link skips the relay and can be faster for players near each other, but you can see each other's IP address. The countdown card shows "relay" or "direct" next to each player.
- **Computer players' orders arrive early too,** so their ants are corrected less on your screen.

**Rules / network:** network protocol 26: a v0.18 page or game cannot join a v0.19 match, so reload the page. The rules and the recordings are unchanged.

**For operators:**
- The relay (coturn) runs in the stack with `COMPOSE_PROFILES=relay` and the `ANTS_TURN_*` settings, including `ANTS_TURN_PORT` when another TURN server holds 3478 (docs/SERVER.md).
- The admin page's Bot matches can use any map by name, an editable rotation, and "Start one every X minutes" (database migration 10).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/06f28456...fb4e3ef0)

## v0.18.2 - 2026-11-19 - Private rooms show in Open games, locked

**For players:**
- **Private rooms show in Open games, locked.** You see the map, the host and the seats with a padlock and a PRIVATE tag; you can join only with the room's link. Quick join skips them. Matches in private rooms stay watchable as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/cd4ac593...0b39b23a)

## v0.18.1 - 2026-11-19 - A note when a new version is out; computer players use the endgame

**For players:**
- **A note when a new version is out.** A strip at the top of Ants & Uncles, a room and the other pages offers Reload or Later, and "What's new" lists what changed since your last visit. Nothing shows during a match; it waits for the results.
- **Computer players waste less time once the food is gone.** An idle Bomber or Swimmer fetches a Fire power-up, a Swimmer becomes the Thief for the last steals, hurt ants walk home to heal, and late fights need a clear advantage.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1b9ed00f...bdc50e13)

## v0.18.0 - 2026-11-19 - Dropped food can be taken only once

**For players:**
- **Food dropped by a dying ant can be taken only once.** When a thief carrying food died on a spot where a food pile had already been emptied, its lunch box could be eaten again and again for full points. Now the first ant to take it empties it, and it is gone. The 1998 game has the same flaw; this fix is a deliberate change from it.

**Rules / network:** the original game's rules number moves from 9 to 13 (game mode 187 keeps 12); recordings of earlier numbers still play as they were recorded. Network protocol 25: a v0.17 page or game cannot join a v0.18 match, so reload the page.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/837d81c1...9e729c46)

## v0.17.13 - 2026-11-19 - Phone fixes: keyboard in Options, watch bar, countdown, full-screen exit

**For players:**
- **Phones: the quick chat keys (F9 - F12) in Options open the keyboard** when tapped, and keep what you type.
- **Phones: the watch page's controls fit on one row** at every phone width; the settings gear sits above the end of the timeline.
- **Phones: no Fullscreen button covers the players on a small countdown card;** the Full button at the top is the way.
- **iPhone held sideways in full screen:** the round exit button sits in the black margin beside the picture, no longer over the minimap.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.12 page or game still joins.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b842a06e...11d4a225)

## v0.17.12 - 2026-11-19 - New windows, a Fullscreen button, live matches at once, alerts

**For players:**
- **A new look for the game's windows.** Options, Quick Help, the quit question and the other windows over the match get brushed-metal buttons, sliders and switches with the New art; Classic keeps the original's. The game behind an open window is darkened evenly.
- **A Fullscreen button in the game's top bar,** right of Quit. On an iPhone, where Safari has no full screen for pages, it uses the page's own full screen.
- **Matches show as in progress the moment they start,** with Watch. A match between people is shown to watchers 30 seconds late; matches of computer players are shown almost live.
- **Alerts on Ants & Uncles:** a note when someone joins your room, and the tab blinks when your match starts while you are in another tab. Both can be turned off under the bell, next to the menu button.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.11 page or game still joins. Under the hood: the game, the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/30ddce44...9548c0a5)

## v0.17.11 - 2026-11-18 - Your orders act at once online, one window for watching

**For players:**
- **Your own orders act at once in online matches.** Your ants now answer a click right away on your screen, as in a game on your own computer, whatever your connection. Attack clicks on a moving ant land far more often, and a well-timed re-order on mud works again with a slow connection. The server still decides every result, so nothing changes for the other players.
- **Watching no longer leaves you with two front pages.** Watch and Matches open in the same tab; while you sit in a room they open a new tab, and Home in that tab closes it and brings you back to your room.
- **The Games count includes matches in progress.**

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.10 page or game still joins. Under the hood: the game and the web pages are rebuilt. To switch the new behavior off for one game, add `?prediction=off` to the game page's address.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/cb9b01b0...c59ad097)

## v0.17.10 - 2026-11-18 - Bot matches on the administration page, longer replay storage

**For players:**
- **Replays are kept longer.** The server now keeps up to 1 GB of matches for up to 90 days, and matches of computer players have their own smaller share, so they do not push out matches played by people.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.9 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**For operators:**
- **Computer-player matches can be started and scheduled from the site's administration page.** Start one or several at once, stop them, and set repeating schedules in Pacific time. Setting `ANTS_BOT_MATCH_EVERY_MIN=0` switches schedules off on the server.
- **The database moves to schema 9 on the first start.** An older server cannot open it afterward.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/109000f6...4a6b395e)

## v0.17.9 - 2026-11-18 - New pointers, Map Maker clumps and real map pictures

**For players:**
- **New pointers with the New HUD art.** Every cursor is redrawn to match the new look, sharp at larger window sizes, and the two places where the frame's pieces met badly are mended. Classic keeps the original's.
- **Map Maker clumps.** Each tap in Clump puts a new clump of up to 250 ants and leaves the earlier ones where they are. Pick up moves one clump, and Erase takes away only the clump you touch.
- **Better pictures for players' maps.** Submitted, approved and test maps now get a picture drawn by the game itself, like the library's maps.
- **More maps in the computer players' own matches.** POPcOrN, HELLRAISER and Bombz Away join Highest score, and Fight Island joins 187.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.8 page or game still joins. Under the hood: the game server and the web pages are rebuilt, and the new front page at playants.org may read the public match numbers.

**Fixes:**
- On a phone, the Map Maker's map could stay stretched after choosing a tool, so a tap landed on the wrong cell.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/7c0381af...0469f31d)

## v0.17.8 - 2026-11-17 - Hard computer players keep hitting

**For players:**
- **Hard computer players keep hitting.** In a fight a Hard bot now gives its next blow far sooner, so an ant it attacks gets away less often. Medium and Easy play as before.
- **Computer players on ISLANDS** no longer send an ant over a bridge that is about to go.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.7 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/48c230ad...6c9f09fc)

## v0.17.7 - 2026-11-17 - Back to the room after a match, ISLANDS bridges, Map Maker edges

**For players:**
- **Back to the room after a match.** The game's own Leave Game on the results screen now brings you back to your room, like the button under the game, and a public room stays public for the rematch.
- **Hosting and watching a replay.** While you are in a room, Matches opens in a new tab, so your room stays yours; a page that comes back within a minute takes its seat again, and a seat that ended says so.
- **Computer players on ISLANDS dig bridges** (Hard): a Swimmer digs the cheapest flower link first, a bridge home for hurt or stranded ants, to food on another island and to a Fire or Bomber power-up.
- **Map Maker:** edge pieces show while you paint (no black squares), new water gets a ring of mud beside it with Auto edges on, and places where the game has no edge piece are marked.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.6 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b7ff8ec0...845c2214)

## v0.17.6 - 2026-11-17 - One admin secret, and a closed admin page says why

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.5 page or game still joins. Under the hood: the chat hub, the game server and the web pages are rebuilt.

**For operators:**
- **For the site's admins:** the admin page needs one secret setting instead of two, and when it is closed it now says which part is off. Players see no change.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d148d9ea...711aff60)

## v0.17.5 - 2026-11-17 - A new look for the match screen, faster match history, 187 computer-player matches

**For players:**
- **A new look for the match screen.** The frame, the panel, the pedestals and the buttons are redrawn, sharp on phones and big screens, in the original's colors and shapes. The original's art stays one setting away (the settings menu: HUD art, New or Classic).
- **Matches show up in the history within seconds** of their end, also while the server's own computer-player matches run, and a player's own approved map shows its picture in the history and in Open games.
- **The server's computer-player matches play 187 too:** a coin decides each one, and the Live list marks a 187 match.
- **Map Maker:** typing in a box (the maker's name, the numbers of the Ants panel, the description) keeps the cursor in it.
- **Chat:** an administrator's long message no longer draws over the next one on a phone.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.4 page or game still joins. Under the hood: the game server and the web pages are rebuilt; the web download is larger (the new art: about 12 MB on the wire).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1ec8e125...dd11f9f7)

## v0.17.4 - 2026-11-17 - The screen stays on, steadier Menu, bots that cross bomb fields, a Discord admin page

**For players:**
- **The screen stays on.** A phone or tablet no longer goes dark while you play a match or watch a replay (where the browser supports it); a paused replay lets it sleep again.
- **The front page's Menu button stays put** while the page loads on a phone, instead of jumping from the left to the right.
- **Computer players handle bomb fields better.** They walk several tiles per order through a bomb field instead of one, take a power-up they are standing on by clicking their own tile, hop over a fire wall and a lake to reach a power-up, never plant a bomb under an ant, and try a fresh plan when they get stuck (POPcOrN, the CrUnChY BiTe maps and the other bomb-field maps).

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.3 page or game still joins. Under the hood: the game server's database moves to version 8 (the old admin accounts are removed; an older server cannot open the new file), and the chat hub and the web pages are rebuilt.

**For operators:**
- **A new admin page.** Site admins sign in with Discord on the admin page to review the maps players send, handle chat bans and mutes, read the moderators' log and see how the site is doing; the old admin passwords are gone. Players see no change.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/433de635...4fb44bf6)

## v0.17.3 - 2026-11-16 - A Shift button for touch screens, moderation from the Players list, cleaner scores

**For players:**
- **A Shift button for touch screens.** The hotkey row on a phone or tablet has a Shift button: tap it and your next tap or drag on the map is a Shift-tap, as with a keyboard's Shift (add an ant to the selection or take one out, so a Bomber can be picked on its own); press and hold it to keep Shift on until you tap it again.
- **Moderators act from the Players list.** A moderator or admin opens the same moderation sheet from a person in the front page's Players list, not only from a chat line. Players see no change.
- **Cleaner scores and clock.** The scores and the match clock in the game's top bar use the game's numbers without the black outline.
- **ISLANDS computer players (Hard) waste fewer ants.** Spare ants go and take the Swimmer the flower drops, and corner rows that nobody works are reached on foot.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.2 page or game still joins. Under the hood: the game server, the chat hub and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/6fb219d9...81ef4f92)

## v0.17.2 - 2026-11-16 - Fairer computer players in 187, chat moderation from a phone

**For players:**
- **Fairer computer players in 187.** Each computer player now opens on a different side instead of all of them rushing the same one (on TINY that was always the first seat), and a side down to four ants or fewer sends one ant at each enemy instead of two that get in each other's way.
- **Chat moderators can act from a phone.** A moderator or admin right-clicks a line in the front page chat, or presses and holds it on a phone (or picks it with the arrow keys and Enter), to delete it, mute, kick or ban its writer, or see that person's recent messages in the chat. Players see no change.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.1 page or game still joins. Under the hood: the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/da6092fb...443b5fbb)

## v0.17.1 - 2026-11-16 - Longer chat lines, bold numbers on every results column, computer players defend better against Thieves

**For players:**
- **Longer chat lines and a faster pace.** A line in the front page chat can be 1,000 characters (was 200). You can send 10 lines at once and then two a second, up to 600 lines an hour and 3,000 a day. The emoji picker follows the new length. The room chat in a game is unchanged.
- **Every number on the results page is bold.** Ants lost, ants killed and hatched now use the same bold face with an outline as the score, on the normal and the 187 results page. In the narrow columns of the classic page a three-digit number is squeezed to fit, where the old face cut off its last digit.
- **Computer players defend better against Thieves.** A Thief on its way to a raid looks again: when walls, bombs or a closed way meet it, it turns to another hill; a Fire Ant is no longer sent to a bank it cannot walk home from.

**Rules / network:** none: network protocol 24 and the rules numbers are unchanged, so a v0.17.0 page or game still joins. Under the hood: the chat server (it reconnects by itself within seconds), the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d63874e8...4a74eadd)

## v0.17.0 - 2026-11-15 - 187 drops nothing, leave your room from Home, a better emoji picker, bots walk round bombs

**For players:**
- **187 puts nothing on the ground.** In a 187 match an ant of a special type (Combat, Bomber, Fire, Swimmer, Thief and the rest) that dies no longer drops its power-up, so a 187 match has no power-ups, no food and no healing from start to end. The original's game is unchanged: there a special ant still drops its power-up.
- **Leave your room from Home.** A small red X on the corner of the My room button leaves the room (when other people are in it you are asked first), and the button now reads My room with its count at every window size. The Games button is gone from Home's top bar: the open games are always on Home, and a phone's bottom strip has Games.
- **A better emoji picker** in the front page chat: 879 emoji in nine groups, a search field (type cat, heart, ant...), your recently used ones, a name line, full keyboard use, and on a phone held upright a sheet that takes the keyboard's place.
- **Computer players walk round bombs** they can see instead of into them, a Bomber clears a bomb that blocks the way to food, and they skip food piles they cannot reach. A Bomber's flight checks its landing spot first, so it no longer lands in the water when something lies there.

**Rules / network:** network protocol 24: a v0.16.11 page or game cannot join (an old page says that a newer version is out: reload it). The replay rules number of 187 is 12; 187 recordings made before keep their old rule and play as they were recorded. The original's game, its rules number and its recordings are unchanged. Under the hood: the game server and the web pages are rebuilt; matches running at the update end.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/06727fbd...2f426f13)

## v0.16.11 - 2026-11-15 - Away on the Players list, press outside a dialog to cancel, computer players that walk where they go

**For players:**
- **People who leave the page stay on the Players list as Away.** When you switch tabs, minimize the window or lock your phone, your name stays in the list for 10 minutes, dimmed, in an Away group at the bottom (staff who are away stay under Staff). It goes back to normal the moment the page returns, and the chat reconnects at once if the connection dropped while the page slept. Logging out, a kick or a ban removes the row right away. Nothing about Away is saved on disk.
- **Press outside a dialog to cancel it.** On every popup (host a game, How it works, the map list, the in-game menu, the Map Maker's boxes, the admin page's boxes) a press on the dimmed page around the box does what Cancel does. A drag that starts inside the box, a text selection or a scroll does not close it. In the game menu with the Leave game question showing, it means Stay.
- **Computer players no longer give orders an ant cannot walk to.** The "Can't go there" messages from computer players on ISLANDS are almost gone, and their ants no longer waste time on those orders.
- **Hard computer players on ISLANDS fly the Bomber to the ring pile earlier.** A Bomber is no longer held back by a mine it cannot reach, so the flight starts much earlier. Easy, Medium and the other maps play as before.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.10 page or game still joins. Under the hood: the chat server, the game server and the web pages are rebuilt.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/25a64658...6b375eeb)

## v0.16.10 - 2026-11-15 - Chat box on one row, computer players take back power-ups and contest the ISLANDS ring

**For players:**
- **The chat box starts as one row.** On the front page the field, the emoji and microphone buttons and Send are on one row in every window. When your words need a second line, the field takes the top row and the buttons and Send move to a row under it at the right; it goes back to one row when the words fit again. The box has the same rounded corners as the site's buttons.
- **Computer players take back power-ups.** Medium and Hard computer players now pick up a power-up that was dropped by an ant of a kind their team has lost, such as a Combat or Swimmer power-up. Hard also goes for dropped Fire, Bomber and Thief power-ups of other sides. Easy plays as before.
- **Hard computer players on ISLANDS contest the ring.** One of their Bombers flies to the island of a ring pile and lays mines on the way to it, so a rival's Swimmers are thrown back. A Swimmer that was hit is sent back to work, each ring pile gets three Swimmers, and the crew digs the one tile that links the ring to the flower strip beside it so that Swimmer power-ups can be fetched. Easy, Medium and the other maps are unchanged.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.9 page or game still joins. Under the hood: the game server and the web pages are rebuilt; the chat server is unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/491ca15c...c52809d1)

## v0.16.9 - 2026-11-14 - Numbers in the game's font, chat links and times, Discord server nicknames, a resizable front page

**For players:**
- **Numbers in the game's own font.** Hit points (Ctrl+L), damage and heal numbers and score numbers are drawn in the same letters as the game's labels, white with a thin dark outline (heal numbers pale green) and sharp at every window size. A damage or heal number now starts on the middle of the ant, keeps one size, floats up, fades and shrinks as it goes.
- **Chat links and times.** An address that starts with http://, https:// or www. in a chat line is a link that opens in a new tab (long ones are shortened in the middle; the whole address is in its tip). Every line starts with the time, in your own time zone and clock style, with a small Today / Yesterday / date divider between days. Lines no longer show numbers; staff see the number in the tip of the time.
- **Your Discord server nickname is your chat name.** The name shown is now your nickname in the project's Discord server, else your Discord display name, else your username. The switch in the chat menu still offers the username. A changed nickname shows from the next roles check, within a few minutes; old lines keep the name they had.
- **A front page you can resize.** On a wide window (701 pixels and up), drag the join between Open games and Players, or the top of Global chat, to give one more room. Arrow keys move a handle, Enter or a double click puts it back, and the sizes are kept in this browser. A phone has no handles.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.8 page or game still joins. Under the hood: the chat server changed, so it restarts and chat reconnects by itself within seconds; it also holds code for a future admin page that no page uses yet.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a364d921...f2d101bf)

## v0.16.8 - 2026-11-14 - Game page top bar, Playing in the Players list, one chat box, Discord pictures without a new login

**For players:**
- **The game page has the site's top bar.** A game against the computer, a joined match, a recording and a live view show the same bar as every other page: Home, the links, the Settings gear, Fullscreen and Menu. The footer is gone; the version is the last line of the Menu. During a match, Home asks first and the other links open in a new tab, and while the Menu is open the keys stay with the bar and go back to the game afterwards. Reload page, which asks first during a game, is in the Menu.
- **The Players list shows who is Playing.** A public match shows its map and players, a private room or game shows a lock and no details, and a game against the computer reads "<map> . vs computer". The Games list no longer shows an empty "In progress" box while no match is running.
- **One chat box everywhere.** The box with the field on top and the emoji, microphone and Send buttons under it is now used in every window; the field grows to two lines on a wide window and the chat area is a little taller.
- **Discord pictures arrive without logging in again.** A picture is picked up while you are logged in: within a few minutes for members of the project's Discord server, otherwise about a day. The Legal page says so.
- **Live mark.** The red "N live" mark reads "N live: see them on the front page", shows its dot in forced-colors mode and updates when you come back to the tab. The old Live now addresses lead to the front page in any letter case.
- **Chat staff commands.** Staff can name a person by the number of the chat line or by their row in the Players list, list the mutes with /mutes, and a ban can also delete that person's lines of the last hour.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.7 page or game still joins. Under the hood: the chat server changed, so it restarts and chat reconnects by itself within seconds.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f6d76998...1b43c0b5)

## v0.16.7 - 2026-11-14 - Phone hotkey buttons, damage and heal numbers, Automatic picture shape

**For players:**
- **Hotkey buttons on a phone.** While you play, a phone shows buttons for the game's keys, All ants first, then Stop, Home, Next and None, and a More button for the other keys. Held sideways they form a rail beside the picture, held upright a row under it. The "Touch buttons" row of the game's Settings turns them off.
- **Damage and heal numbers over the ants.** A small -N shows over an ant that loses hit points and a +N over an ant that is healed at its hill. They are on by default, never shown under fog of war, and the "Damage & heal numbers" row of the game's Settings (Video) switches them off.
- **Automatic picture shape.** The Picture shape list starts with "Automatic": the shape nearest your screen when the phone is held sideways, 16:9 when held upright, following the way you hold it. A shape you pick is kept for the way the phone was held; every browser starts on Automatic once.
- **The countdown card's Fullscreen button fits the card on a phone.** It takes the size and place of the card's own button and stays inside the card.
- **Quick host, My room and Join use your Discord name when you are logged in.** A guest answer is no longer kept until the page is reloaded.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.6 page or game still joins. Under the hood: the numbers change nothing in a match.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f9011b41...a795f86a)

## v0.16.6 - 2026-11-13 - Chat roles from Discord and Discord pictures in the Players list

**For players:**
- **Chat moderators and administrators now come from the project's Discord server roles.** Whoever holds the moderator or administrator role there has those powers in the front page chat, within a few minutes of getting or losing the role, and the chat shows them: staff names get a color, the Players list puts them first, and moderators see line numbers. The commands are /mute, /unmute, /del and /kick for both roles, and /ban, /unban and /bans for administrators; a moderator cannot act on another staff member. If the check with Discord cannot be made, no power is given and the chat keeps working. The old hand list setting (ANTS_HUB_MODS) no longer does anything.
- **Discord profile pictures in the Players list.** When you log in with Discord, the site keeps one small copy of your profile picture, only while you have a login, and shows it in your circle in the Players list and in the account button at the top. It is shown to everyone who opens the front page unless you switch it off with "Show my Discord picture" in the account menu, or the site's operator hides it. Chat lines still show names only. Switching it off deletes the copy at once. The Legal page says what is kept and for how long.

**Rules / network:** none: network protocol 23 and the rules numbers are unchanged, so a v0.16.5 page or game still joins. Under the hood: the chat server gained the role and picture code and its tests.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/59cbc736...3b57c6ae)

## Older versions (v0.16.5 and before)

Every release before v0.16.6, and the long notes of v0.16.5, v0.16.4, v0.16.3, v0.16.2, v0.16.1, v0.16.0, v0.15.1, v0.15.0, v0.14.1, v0.14.0, v0.13.2, v0.13.1, v0.13.0, v0.12.152 - v0.12.69, v0.12.59 - v0.12.55, v0.12.53 - v0.12.48, v0.12.46 - v0.12.35, v0.12.33 - v0.12.26, v0.12.24 - v0.12.18, v0.12.16 - v0.12.0, v0.11.0, v0.10.0, v0.9.1, v0.9.0, v0.8.3, v0.8.2, v0.8.1, v0.8.0, v0.7.0, v0.6.0, v0.5.1, v0.5.0, v0.4.0, v0.3.0, v0.2.0, v0.1.3, v0.1.2, v0.1.1, v0.1.0 and v0.0.99 - v0.0.90, are only in the git history.

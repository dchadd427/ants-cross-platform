# Home and the Room: the front page's two screens

The front page (`web/lobby.html`, served at `/`) is one page with one WebSocket and two screens: **Home**, a top bar over a picture of the Yard, and the **Room**, the room page that [Play in the browser](PLAY_IN_BROWSER.md#your-room) describes, under a slim bar. `<body data-screen="home">` or `data-screen="room"` says which one shows. The page's client of the game server (`web/front/lobby_net.js`), its model and its session storage are the same for both, so going from one screen to the other is a class and one render: instant, no request, and the seat is never dropped. Both screens are drawn as the approved pictures draw them. Everything new is in `web/home/` (`home.js`, `home.css`, and `games.js`, `games.css`, which load after the first paint), because `web/front/` is at its size cap and the page at its line limit.

## Home

- **The top bar:** the logo, **Quick host**, **Quick join**, **My room** (with how many of the colors that are in the match play, "3/4", once there is a room), **Games** (with how many games are open), and at the right the Menu button (a login chip has its place there and stays hidden). Quick host and Games are lit by `games.js` when it has loaded; Quick join is drawn but disabled until the piece that lights it is in, and nothing on the bar is a dead button.
- **My room** makes a private room the first time it is pressed (a new code, the first color, the remembered or a picked name) and shows it; every later press only shows it. **A plain visit makes no room.** A tab that holds a seat takes it back in the background, so My room stays live.
- **The Menu** (also in the Room's bar): Matches, Live now, Map maker, How it works, the Discord server, then the small links of the footer (Sprites and sounds, Changelog, GitHub, Feedback, Legal), the live numbers and the version. It is at most as tall as the window leaves under the bar and scrolls inside itself (a phone held sideways is about 360 px high). The footer is not drawn on Home.
- **The Yard** is a stand-in (CSS and two small pictures drawn as data): a sandy field with food, rocks, three of your ants and two of other colors, a dashed edge and a label that prints the size that the Yard will have. The real Yard mounts into `#yard`. Every target is at least 44 px high and wide, and there is no sideways scrollbar at 390 x 844, 844 x 390 and 1280 x 800.
- **The strips** (the notices, the Rejoin button, "Back from the match") stand above both screens; Home is as tall as the window, strips included, so it never scrolls.

## Games, Quick host and the public Room

`games.js` and `games.css` come after the first paint (the page appends them to the head; a script that does not come leaves the buttons disabled and the page as it was) and own everything about public rooms. The page's `client`, model and session stay the page's own; the script reads them through the small bridge that `lobby.html` hands to `home.js` (`client()`, `join`, `typed`, `name`, with `info`, `make`, `leave`, `how`).

- **Notices.** The notices of this part (the public room, "Your match is running.", "Joined Pebble's room.") are one strip with the page's own look: with the page's strips above the bar on Home, and under the Room's slim bar on the Room, as drawn.
- **The Room's switch.** The host sees **Private** (link only) and **Public** (listed in Open games) in the card, a guest reads "Public room" or "Private room". A press sends one `Visibility` through `LobbyClient.setPublic` (the page's pace applies: a request that is held back sends nothing and says "One moment, then press again."); the switch shows what the last Room message said and never what a press asked. A room that went private after 30 minutes says so to its host with a one-tap **Make it public**; another person's lead says "This room is private again because the host changed." While the Room screen shows, the tab is visible and the host looks at a public room, the page says "public" again every 10 minutes (a refresh that counts as use; never from a hidden tab, from Home or by a guest).
- **Quick host** flips the open room that you lead to Public and shows the Room ("Your room is public now. It shows in Open games; ..."); with no room it makes one (My room's machinery) and sends `Visibility(public)` once the Room message says that the room is there; a public room says "Your room is already public."; as a guest it asks first ("Leave Pebble's room and host your own?"), and only then leaves and makes one; START waiting or a match to go back to says "Your match is running." with **Rejoin**; a full server gives the page's own "The server is full right now" strip.
- **The Games list** is the popover under the button: `GET /lobbies` ([`PUBLIC_ROOMS.md`](PUBLIC_ROOMS.md)) as rows (host, map, the seats as four colored dots and n of the size, the bots, **Join**), the page's own room left out by its code, "No open games right now." with **Host one!** when nothing is open, "Games list is off right now" when the door answers 404, 503 or something that is not a list, and **Join by code** at the bottom (it replaces the Room card's "Have a code?"). A Join opens a second client that only joins; the page's own room is let go of **only when the new one welcomes the page**, so a refusal (full, started, gone, another version, no answer in 15 s) costs nothing and is said in plain words; a room with other people in it is asked about first.
- **Polling:** every 30 s with the popover closed, every 5 s while it is open and at once when it opens, +-20 % each, doubled after every failure up to 5 minutes (1 minute while open), never while the tab is hidden or the Room shows. The count on the button caps at "50+" and is hidden while the list is off.
- **START** puts the list away and stops its looks, the way it takes the bars' presses; a hand-off that has not gone after 15 seconds has failed, and the list is back.
- **Untrusted text.** Every field of a row is checked (the nine fields of the answer, their types and ranges; a name is printable ASCII, a map a file name, a code lower case), a row that does not fit is dropped, at most 50 rows are shown, and all of it is drawn with `textContent`: never markup, never a link.

## The Room

The room page as it was, with the logo row replaced by a slim bar: **Home** (the room stays open), the logo, **Leave room** (leaves it and shows Home; the next My room makes a new room) and the Menu. The old header's buttons live in the Menu and are hidden.

## Addresses

| Address | Screen |
|---|---|
| `/` | Home |
| `/#room` | the Room (My room, or the Home button, pushes it: Back goes to Home, Forward back to the Room) |
| `/?room=CODE` | the Room, behind the name card (a link opens the Room directly) |
| `/#room` after a reload, in a tab that holds a seat | the Room again; in a tab with none, Home |

A reload keeps the screen. The page writes nothing to the history until a button is pressed; the entry that it opened on is marked with its screen the first time a button leaves it, with its address unchanged.

## START while a guest sits on Home

The hidden Room still takes the hand-off: the key of the seat is written for the game page (`ants.rejoin.<room>.<seat>`), the tab goes to the game page, and Home shows **Opening the game…** at once. The hand-off never looks at the screen, and from then on both bars take no press (a Leave room would take the seat out from under the game that is about to take it over).

## Tests

`tests/scripts/test_web_home_screen.py` (the page, the folder and its budgets, the style, the way into the image, the mutants), `tests/scripts/web_home_screen_check.js` (the page in the fake browser of `tests/scripts/web_page_harness.js`: a plain visit, My room, Home, Back and Forward, a link, a reload, Leave room, START on Home, the strips, the Menu) and the front page against a real `ants_server` in `tests/scripts/web_lobby_server_check.js` (a guest on Home takes START). The opt-in real-browser checks that open the plain front page press My room through `tests/scripts/web_front_room.py`. `tests/data/home_screen_mutants.json` (`tools/mutate.py`) changes one thing at a time: the room left on Home, the hand-off ignored while Home is up, Back, a reload on Home, the buttons, the Menu, the style and the wiring; every one must be caught.

The Games list and the public Room have `tests/scripts/test_web_home_games.py` (the page's wiring, the folder and its budgets, the style, the way into the image), `tests/scripts/web_home_games_check.js` (the rows, the polling schedule on a fake clock, the popover, a hostile list, a list that is off) and `tests/scripts/web_home_games_room_check.js` (the switch, the notices, the 10-minute repeat, Quick host in every state, Join and its refusals), all in the fake browser; the real server (`tests/scripts/web_lobby_server_check.js`: a host makes a room public, a second page sees it in the real list and joins), `tests/data/home_games_mutants.json` (`tools/mutate.py`), and, opt-in, `tests/scripts/web_home_games_browser.js` (two real Chromium pages through Playwright against a real `ants_server`, behind a small stand-in for the site's nginx; `--shots DIR` takes the pictures of the scenes at 390 x 844, 844 x 390 and 1280 x 800).

# Home and the Room: the front page's two screens

The front page (`web/lobby.html`, served at `/`) is one page with one WebSocket and two screens: **Home**, a top bar over a picture of the Yard, and the **Room**, the room page that [Play in the browser](PLAY_IN_BROWSER.md#your-room) describes, under a slim bar. `<body data-screen="home">` or `data-screen="room"` says which one shows. The page's client of the game server (`web/front/lobby_net.js`), its model and its session storage are the same for both, so going from one screen to the other is a class and one render: instant, no request, and the seat is never dropped. Both screens are drawn as the approved pictures draw them. Everything new is in `web/home/` (`home.js`, `home.css`, and the chat dock's `chat.js`, `chat.css`), because `web/front/` is at its size cap and the page at its line limit.

## Home

- **The top bar:** the logo, **Quick host**, **Quick join**, **My room** (with how many of the colors that are in the match play, "3/4", once there is a room), **Games**, and at the right the Menu button (a login chip has its place there and stays hidden). Quick host, Quick join and Games are drawn but disabled: the pieces that light them are separate changes, and nothing on the bar is a dead button.
- **My room** makes a private room the first time it is pressed (a new code, the first color, the remembered or a picked name) and shows it; every later press only shows it. **A plain visit makes no room.** A tab that holds a seat takes it back in the background, so My room stays live.
- **The Menu** (also in the Room's bar): Matches, Live now, Map maker, How it works, the Discord server, then the small links of the footer (Sprites and sounds, Changelog, GitHub, Feedback, Legal), the live numbers and the version. It is at most as tall as the window leaves under the bar and scrolls inside itself (a phone held sideways is about 360 px high). The footer is not drawn on Home.
- **The Yard** is a stand-in (CSS and two small pictures drawn as data): a sandy field with food, rocks, three of your ants and two of other colors, a dashed edge and a label that prints the size that the Yard will have. The real Yard mounts into `#yard`. Every target is at least 44 px high and wide, and there is no sideways scrollbar at 390 x 844, 844 x 390 and 1280 x 800.
- **The strips** (the notices, the Rejoin button, "Back from the match") stand above both screens; Home is as tall as the window, strips included, so it never scrolls.

## The Room

The room page as it was, with the logo row replaced by a slim bar: **Home** (the room stays open), the logo, **Leave room** (leaves it and shows Home; the next My room makes a new room) and the Menu. The old header's buttons live in the Menu and are hidden. Below 340 px the picture of the logo is left out of the bar (the heading keeps its name).

## Addresses

| Address | Screen |
|---|---|
| `/` | Home |
| `/#room` | the Room (My room, or the Home button, pushes it: Back goes to Home, Forward back to the Room) |
| `/?room=CODE` | the Room, behind the name card (a link opens the Room directly) |
| `/#room` after a reload, in a tab that holds a seat | the Room again; in a tab with none, Home |

A reload keeps the screen. The page writes nothing to the history until a button is pressed; the entry that it opened on is marked with its screen the first time a button leaves it, with its address unchanged.

## START while a guest sits on Home

The hidden Room still takes the hand-off: the key of the seat is written for the game page (`ants.rejoin.<room>.<seat>`), the tab goes to the game page, and Home shows **Opening the game…** at once. The hand-off never looks at the screen, and from then on both bars take no press (a Leave room would take the seat out from under the game that is about to take it over). A hand-off that is still here 15 seconds later has failed, was stopped or is only slow: the card goes and Home takes presses again, but this page's room stays as it was (a page on its way takes no message), so the notice strip says "The game did not open. Reload this page to get your seat back." with a **Reload** button (the page that comes up offers **Rejoin it**), and **Leave room** stays off: a Leave would end the START for everybody and leave a game page that is only slow with no seat to take over (a game that comes late takes the seat with the key).

## The chat dock

Home carries the site's global chat in a panel in its lower-left corner: 380 x 380 on a desktop, which the minus button folds to its header; on a phone or a short window, a strip 62 px high along the bottom that a press anywhere on it opens upward (490 px high upright, 420 x 286 sideways). `chat.js` and `chat.css` load once, after the first paint, when Home is first shown (a link that opens the Room loads nothing until Home shows), and no other page loads them or opens a hub socket. The socket stays open while the Room shows, so the **unread count** on the strip and the header goes on: the lines that came while the dock was folded, on the Room or in a hidden tab, not the visitor's own and not the history.

- **The log** shows `name: text`, the moderators' lines marked "mod", 100 lines at most (`del` takes one away). All of it is text from strangers: made with `textContent` only, never markup or a link, cut to 32 and 200 characters, the invisible characters that the hub removes taken out here too (a test keeps the two lists in step; a name loses its variation selectors as well), each name and line in a `<bdi>`; a frame that is too big, not JSON or of a kind that is not known is ignored. The log is `role="log"`, polite, and a flood is not read out line by line: two lines in 3 s are, then one summary ("8 more messages").
- **Who may write** is the hub's welcome (`docs/HUB.md`). Without `you` the dock is read-only: "Chat is read-only for now", no field and no Send; with `login:true` and no `you`, "Log in with Discord to chat". With `you` the field and Send are there. `#gchat-login-slot` is the place of the login chip: empty and hidden until the login change fills it. A line goes out only on an open, welcomed socket, at the page's own pace (two at once, then one every 2.5 s; the hub's is three and 2 s), and every `err` code has its plain words (a line with `<` or `>` is not sent: "Lines cannot hold < or >"; a text that went in the last 31 s is not sent again, as the hub remembers every one). "7 here now" says "500+" over the hub's 500.
- **Offline.** A hub that is not there, slow or garbage is "Chat is offline" and "reconnecting…": a try after 1 s, doubling to 60 s, each wait spread from half to one and a half times (never under 1 s or over 60 s), so that pages that lost one hub do not come back in waves; a link must hold 30 s to start again at 1 s; a tab or a network that comes back tries at once. A ping every 25 s, and a hub that does not answer for 30 s is let go. A hidden tab keeps the socket and paints nothing. Nothing is kept in the browser, and nothing the chat does can block Home, the Room or START.

## Tests

`tests/scripts/test_web_home_screen.py` (the page, the folder and its budgets, the style, the way into the image, the mutants), `tests/scripts/web_home_screen_check.js` (the page in the fake browser of `tests/scripts/web_page_harness.js`: a plain visit, My room, Home, Back and Forward, a link, a reload, Leave room, START on Home, the strips, the Menu) and the front page against a real `ants_server` in `tests/scripts/web_lobby_server_check.js` (a guest on Home takes START). The opt-in real-browser checks that open the plain front page press My room through `tests/scripts/web_front_room.py`. `tests/data/home_screen_mutants.json` (`tools/mutate.py`) changes one thing at a time: the room left on Home, the hand-off ignored while Home is up, Back, a reload on Home, the buttons, the Menu, the style and the wiring; every one must be caught. The chat dock has its own: `tests/scripts/test_web_home_chat.py` (budgets, wiring, style, the other pages, the mutants of `tests/data/home_chat_mutants.json`), `tests/scripts/web_home_chat_check.js` (the dock against a fake hub and a fake clock), `tests/scripts/web_home_chat_hub_check.js` (against `tests/scripts/fake_hub.js` on a real socket) and, opt-in, `tests/scripts/web_home_chat_browser.js` (Chromium through Playwright, `--shots DIR`).

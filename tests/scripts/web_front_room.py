"""The way into a room for the opt-in browser checks of the front page (docs/HOME_SCREEN.md).

The front page opens on Home and makes no room: a visitor presses My room, which makes the room the first time and only shows it when the tab holds a seat (a reload on Home takes the seat back in the
background, and the Room is hidden behind Home). The checks that open the plain page and then wait for the room (`#code`, `#slots li.k-person`) and work with it (press its buttons, measure it, move the
focus) press My room the same way: `press_my_room(ev)` does so once for a page that is on Home, and is meant to be called again and again while the check waits (a second call does nothing: the button
is marked). `ev` evaluates a JavaScript expression in the page (each check's own Tab.ev). The page's own markup is all that it relies on: `.hbar .mine` (My room) and `data-screen`. Run by web_home_check.py,
web_lobby_chat_browser.py and web_map_picker_browser.py (and through their Visitor by web_game_type_browser.py and web_rematch_browser.py).
"""

PRESS_MY_ROOM = ("(function () { var b = document.querySelector('.hbar .mine'); "
                 "if (b && document.body.getAttribute('data-screen') === 'home' && !b.getAttribute('data-pressed')) { b.setAttribute('data-pressed', '1'); b.click(); } return 1; })()")


def press_my_room(ev):
    """Presses My room on a front page that is on Home (once per page load); returns nothing."""
    ev(PRESS_MY_ROOM)

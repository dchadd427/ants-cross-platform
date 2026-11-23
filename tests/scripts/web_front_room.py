"""The way into a room for the opt-in browser checks of the front page (docs/HOME_SCREEN.md).

The front page opens on Home and makes no room: a visitor presses Quick host and answers its question (Public or Invite only; the checks want a room of their own that stays private, so they choose Invite only).
The page then makes the room, shows it and takes the seat. The checks that open the plain page and then wait for the room (`#code`, `#slots li.k-person`) and work with it (press its buttons, measure it,
move the focus) call `host_a_private_room(ev)`, which does this once for a page that is on Home and is meant to be called again and again while the check waits (a second call does nothing: the button is marked
once the question has been answered). `ev` evaluates a JavaScript expression in the page (each check's own Tab.ev). The page's own markup is all that it relies on: Quick host (`.hbar .quick.host`), the card's
buttons (`.gcbtns button`) and `data-screen`. Run by web_home_check.py, web_lobby_chat_browser.py and web_map_picker_browser.py (and through their Visitor by web_game_type_browser.py and web_rematch_browser.py).
"""

HOST_A_PRIVATE_ROOM = ("(function () { var s = document.body.getAttribute('data-screen'), b = document.querySelector('.hbar .quick.host'); "
                 "if (s !== 'home' || !b || b.disabled) return 1; "
                 "var ask = Array.prototype.filter.call(document.querySelectorAll('.gcbtns button'), function (x) { return /^Invite only$/.test(x.textContent.trim()); })[0]; "
                 "if (ask) { ask.click(); return 1; } "
                 "if (!document.querySelector('.gconfirm') && !b.getAttribute('data-pressed')) { b.setAttribute('data-pressed', '1'); b.click(); } return 1; })()")


def host_a_private_room(ev):
    """Makes the page's own, private room on a front page that is on Home (Quick host, then Invite only); returns nothing."""
    ev(HOST_A_PRIVATE_ROOM)

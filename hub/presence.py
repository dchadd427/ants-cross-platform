"""Who is on the front page right now (docs/HUB.md, "Presence"): the list that Home shows beside the Yard. No sockets here: a peer is the object that hub.chat talks to.

A socket that is let in with a login is a named person (the Discord username that the hub already shows in the chat); a socket without one is a guest, counted and never named. A page says what its
visitor is doing (looking, waiting in a room that has not started, in a match) and asks for the list; the hub sends the list once and then the changes, at most every `who_seconds`. All of it is
memory, for as long as a socket is open: nothing goes to the database, the log has no name or number of a person, and a person is out of the list one send after the socket closes.

Messages (JSON, docs/HUB.md has the table):
  up    {"t":"who"}                                    watch the list: it is answered at once with the whole list
        {"t":"here","at":"look"|"wait"|"play", ...}    what this page's visitor is doing (a login is needed); a room that is public may say its map, seats and code, a private one nothing
  down  {"t":"who","full":bool,"n","g","more","rows":[row,...],"gone":[id,...]}
A row is {"i": a number that names it while the person is here, "name", "at", "mod" (moderators only), and for a public room "pub", "map" (a file name), "n", "of", "code"}. A socket that said nothing is looking.
A message that is no `here` is a mistake of the socket; a second `who` too soon is not (it costs the socket a frame more, like any refusal that is not its doing).
"""
import collections
import re

from . import text as textrules

AT = ("look", "wait", "play")
WEIGHT = {"look": 0, "wait": 1, "play": 2}              # of the sockets of one account, the one that is doing the most is the account's row
RANK = {"wait": 0, "play": 1, "look": 2}                # when there are more people than rows: moderators first, then those who wait (a Join is there), those who play, those who look
SEATS = 4
ROOM_CODE = re.compile(r"[a-z0-9_-]{1,32}\Z")           # (the codes that the game server makes for a room: lower case, the page's own rule)
MAP_FILE = re.compile(r"[ -~]{5,64}\Z")                 # (a map's file name as the game server lists it: printable ASCII, 5 to 64 characters, a .LVL ending, none of / \ : * ? " < > | and no leading dot)
NOT_IN_A_FILE_NAME = frozenset('/\\:*?"<>|')

Where = collections.namedtuple("Where", "at map n of pub code seq")


def whole(value, low, high):
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def map_file(value):
    """The file name if it is one (and goes through the text rules unchanged), else None."""
    if not isinstance(value, str) or not MAP_FILE.match(value) or NOT_IN_A_FILE_NAME & set(value) or value[0] == "." or not value.endswith((".lvl", ".LVL")):
        return None
    text, why = textrules.clean_line(value)
    return value if why is None and text == value else None


def parse_here(msg, seq):
    """What a page said it is doing, as a Where; None when it is not what a page says (a field of the wrong kind or size). Details are kept only where they belong and only for a room that
    the page calls public: a private room is "a private room" and nothing more (no map, no seats, never a code), a match has no code, and a visitor who is looking has none of it."""
    at = msg.get("at")
    if at not in AT or ("pub" in msg and not isinstance(msg["pub"], bool)):
        return None
    n, size, file, code = msg.get("n"), msg.get("of"), msg.get("map"), msg.get("code")
    if (n is not None and not whole(n, 0, SEATS)) or (size is not None and not whole(size, 1, SEATS)) or (n is not None and size is not None and n > size):
        return None
    if (file is not None and map_file(file) is None) or (code is not None and not (isinstance(code, str) and ROOM_CODE.match(code))):
        return None
    if at == "look" or msg.get("pub") is not True:
        return Where(at, None, None, None, False, None, seq)
    if at == "play":
        return Where(at, file, n, None, True, None, seq)
    return Where(at, file, n, size, True, code, seq)


def row_of(ident, name, mod, where):
    row = {"i": ident, "name": name, "at": where.at if where else "look"}
    if mod:
        row["mod"] = True
    if where is not None and where.pub:
        row["pub"] = True
        for key, value in (("map", where.map), ("n", where.n), ("of", where.of), ("code", where.code)):
            if value is not None:
                row[key] = value
    return row


class Presence:
    def __init__(self, chat, limits, clock, dumps):
        self.chat = chat
        self.limits = limits
        self.clock = clock
        self.dumps = dumps
        self.where = {}                 # peer -> Where: what its page last said (a socket that never said anything is looking)
        self.watchers = set()           # the peers that asked for the list
        self.asked = {}                 # peer -> when it last asked for the whole list
        self.ids = {}                   # account id -> the number that names its row for as long as the account is here (never sent)
        self.next_id = 1
        self.seq = 0                    # the order in which pages said what they do (the newer one wins between two sockets of one account that do the same)
        self.sent = None                # the list as it was last sent: {"n", "g", "more", "rows": {id: row}}
        self.sent_bytes = None          # ... as one frame, for the next page that asks
        self.refreshed = float("-inf")

    def alive(self, peer):
        return peer in self.chat.peers and not getattr(peer, "closed", False)

    def people(self):
        """How many people are here: the accounts that have a socket open (one for each, however many tabs) and the sockets that have no login (a guest is one socket). The chat's "here now" is this too."""
        accounts, guests = set(), 0
        for peer in self.chat.peers:
            if getattr(peer, "closed", False):
                continue
            if peer.identity is None:
                guests += 1
            else:
                accounts.add(peer.identity.account_id)
        return len(accounts) + guests

    # ---- the list --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def view(self):
        """The list as it is now: the people (an account once), who of them is where, in the order that decides who is left out when there are more than `who_rows_max`."""
        best, guests = {}, 0
        for peer in self.chat.peers:
            if getattr(peer, "closed", False):
                continue
            who = peer.identity
            if who is None:
                guests += 1
                continue
            where = self.where.get(peer)
            key = (WEIGHT[where.at], where.seq) if where else (0, 0)
            if who.account_id not in best or key > best[who.account_id][0]:
                best[who.account_id] = (key, who.name, where)
        mods = self.chat.mods
        order = sorted(best.items(), key=lambda item: (item[0] not in mods, RANK[item[1][2].at if item[1][2] else "look"], item[1][1], item[0]))
        rows = {}
        for account, (_, name, where) in order[:self.limits.who_rows_max]:
            ident = self.ids.get(account)
            if ident is None:
                ident = self.ids[account] = self.next_id
                self.next_id += 1
            rows[ident] = row_of(ident, name, account in mods, where)
        for account in [a for a in self.ids if a not in best]:
            del self.ids[account]                   # (a person who left is a new row if they come back)
        return {"n": len(best) + guests, "g": guests, "more": max(0, len(best) - self.limits.who_rows_max), "rows": rows}

    def frame(self, view, previous):
        """The frame that takes a page that has `previous` to `view`: the rows that changed and the numbers of those that went, or the whole list when the page has none or the change is big."""
        numbers = {"n": view["n"], "g": view["g"], "more": view["more"]}
        if previous is not None:
            up = [row for ident, row in view["rows"].items() if previous["rows"].get(ident) != row]
            gone = [ident for ident in previous["rows"] if ident not in view["rows"]]
            if len(up) + len(gone) <= self.limits.who_diff_max:
                return dict({"t": "who", "full": False}, **numbers, rows=up, gone=gone)
        return dict({"t": "who", "full": True}, **numbers, rows=list(view["rows"].values()))

    def refresh(self, now, soon):
        """Look at the list, and send the change to the pages that watch it: at most every `who_seconds` (`who_soon_seconds` for a page that asks for it for the first time)."""
        if now - self.refreshed < (self.limits.who_soon_seconds if soon else self.limits.who_seconds):
            return
        self.refreshed = now
        view = self.view()
        if view == self.sent:
            return
        previous, self.sent, self.sent_bytes = self.sent, view, None
        self.watchers = {p for p in self.watchers if self.alive(p)}
        if self.watchers:
            data = self.dumps(self.frame(view, previous))
            for peer in list(self.watchers):
                peer.send_text(data)

    def snapshot(self):
        if self.sent_bytes is None:
            self.sent_bytes = self.dumps(self.frame(self.sent, None))
        return self.sent_bytes

    # ---- messages from a peer --------------------------------------------------------------------------------------------------------------------------------------------------------------

    def on_who(self, peer):
        now = self.clock()
        last = self.asked.get(peer)
        if last is not None and now - last < self.limits.who_resend_seconds:
            return peer.charge()                    # (a page that needs the list again has waited: this is no mistake, and no free frame either)
        self.asked[peer] = now
        self.refresh(now, True)
        self.watchers.add(peer)                     # (after the send above: what it is sent now is the list as last sent, and every change after that is sent to it too)
        peer.send_text(self.snapshot())

    def on_here(self, peer, msg):
        if peer.identity is None:
            return self.chat.mistake(peer, "login")
        self.seq += 1
        where = parse_here(msg, self.seq)
        if where is None:
            return self.chat.mistake(peer, "bad")
        self.where[peer] = where

    # ---- the clock's work ------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def flush(self, now):
        """Once a tick: forget the sockets that are gone, and send the change if somebody watches."""
        alive = {p for p in self.chat.peers if not getattr(p, "closed", False)}
        self.where = {p: w for p, w in self.where.items() if p in alive}
        self.asked = {p: t for p, t in self.asked.items() if p in alive}
        self.watchers &= alive
        here = {p.identity.account_id for p in alive if p.identity is not None}
        self.ids = {a: i for a, i in self.ids.items() if a in here}
        if self.watchers:
            self.refresh(now, False)

"""Who is on the front page right now (docs/HUB.md, "Presence"): the list that Home shows beside the Yard. No sockets here: a peer is the object that hub.chat talks to.

A socket that is let in with a login is a named person (by the name that the chat shows: the person's Discord display name or username, as they picked; a name is not unique, so a row is told
from another only by its number `i`, and the person's own row by `me`, never by the name); a socket without a login is a guest, counted and never named, and so is an account that is banned
from the chat. A row never holds a username that the person chose not to show: the list is one frame for every page, so it holds nothing that only moderators may see. A page says what its visitor is doing (looking, waiting in a room that has not started, in a match) and asks for the list; the hub sends the list once and then the changes, to every page that
watches it at once, at most every `who_seconds`. All of it is memory, for as long as a socket is open: nothing goes to the database, the log has no name or number of a person, and a person is out of the list
one send after the socket closes.

Messages (JSON, docs/HUB.md has the table):
  up    {"t":"who"}                                    watch the list: it is answered at once with the whole list as it was last sent
        {"t":"here","at":"look"|"wait"|"play", ...}    what this page's visitor is doing (a login is needed); a room that is public may say its map, seats and code, a private one only that it is private (`pub`:false), a game against the computer `solo`:true and its map; an account that
                                                       may not chat (muted, too new) has none of it kept
  down  {"t":"who","full":bool,"n","g","more","rows":[row,...],"gone":[id,...]}
        the welcome of a login says `me`, the number of the row that is the account's own
A private room or match is a row with `priv`:true (kept for an account that may not chat too: a fact, never a map, seats or code); a game against the computer is `solo`:true, with `map` where the page said one. A row is {"i": a number that names it while the person is here, "name", "at", "pic" (the public name of the account's picture, 32 hex digits, where it has one that may be shown), "mod" and "role" (`mod` or `admin`: moderators only), and for a public room "pub", "map" (a file name), "n", "of", "code"}. A socket that said nothing is looking.
A `here` of a page that waits in a room may carry `inv` {"code","map","n","of"}: the room's code (even of an invite-only room), its map's file name and its seats, kept for invitations only (docs/HUB_PEOPLE.md) and never in a row or a frame.
A `here` that is not what a page says is refused with the code `here` and is a mistake of the socket; a second `who` too soon is not (it costs the socket a frame more, like any refusal that is not its doing).
"""
import collections
import re

from .store import StoreError

AT = ("look", "wait", "play")                         # (what a page may say; the hub itself adds "away" to a row: an account whose last socket closed a short while ago)
AWAY = "away"
WEIGHT = {"look": 0, "wait": 1, "play": 2}              # of the sockets of one account, the one that is doing the most is the account's row
RANK = {"wait": 0, "play": 1, "look": 2, "away": 3}                # when there are more people than rows: moderators first, then those who wait (a Join is there), those who play, those who look
SEATS = 4
REFUSED = "here"                                        # the code of the `err` for a `here` that is not what a page says (the chat's own codes are for its lines)
ROOM_CODE = re.compile(r"[a-z0-9_-]{1,32}\Z")           # (the codes that the game server makes for a room: lower case, the page's own rule)
MAP_FILE = re.compile(r"[ -~]{5,64}\Z")                 # (a map's file name as the game takes it, net::valid_map_name: printable ASCII, 5 to 64 characters; double spaces and dots are fine)
NOT_IN_A_FILE_NAME = frozenset('/\\:*?"<>|')

Room = collections.namedtuple("Room", "code map n of seq")        # the room that a page that waits in one is in, as it told the hub for invitations (docs/HUB_PEOPLE.md): never in the list, never to a page, only in an invitation's alert
Where = collections.namedtuple("Where", "at map n of pub code seq priv solo", defaults=(False, False))       # priv: the page said the room or match is private (a fact and nothing else); solo: a game against the computer (no room)


def whole(value, low, high):
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def map_file(value):
    """The file name if the game takes it as the name of a map (net::valid_map_name: printable ASCII, 5 to 64 characters, none of / \\ : * ? " < > |, no leading dot, ending in .lvl or .LVL), else None."""
    if not isinstance(value, str) or not MAP_FILE.match(value) or NOT_IN_A_FILE_NAME & set(value) or value[0] == "." or not value.endswith((".lvl", ".LVL")):
        return None
    return value


def parse_here(msg, seq):
    """What a page said it is doing, as a Where; None when it is not what a page says (a field of the wrong kind or size). Details are kept only where they belong and only for a room that
    the page calls public: a private room is "a private room" and nothing more (no map, no seats, never a code), a match has no code, and a visitor who is looking has none of it."""
    at = msg.get("at")
    if at not in AT or ("pub" in msg and not isinstance(msg["pub"], bool)) or ("solo" in msg and not isinstance(msg["solo"], bool)):
        return None
    n, size, file, code = msg.get("n"), msg.get("of"), msg.get("map"), msg.get("code")
    if (n is not None and not whole(n, 0, SEATS)) or (size is not None and not whole(size, 1, SEATS)) or (n is not None and size is not None and n > size):
        return None
    if (file is not None and map_file(file) is None) or (code is not None and not (isinstance(code, str) and ROOM_CODE.match(code))):
        return None
    if at == "look":
        return Where(at, None, None, None, False, None, seq)
    if msg.get("pub") is not True:
        if msg.get("pub") is False:
            return Where(at, None, None, None, False, None, seq, True)             # (private: the mark is kept, never a map, seats or code)
        if at == "play" and msg.get("solo") is True:
            return Where(at, file, None, None, False, None, seq, False, True)      # (a game against the computer: its map only)
        return Where(at, None, None, None, False, None, seq)
    if at == "play":
        return Where(at, file, n, None, True, None, seq)
    return Where(at, file, n, size, True, code, seq)


def parse_room(msg, seq):
    """The `inv` of a `here` (a page that waits in a room says which, so that the account can invite: its code, its map's file name and its seats): a Room, None when the frame has none, and False when it is not what a
    page says (not an object of exactly these fields, a code, a map or seats that are none of the rules'). It is for invitations only: it is kept in memory for as long as the socket is open and the page says it, and no
    list and no other page ever has it, whether the room is public or not."""
    inv = msg.get("inv")
    if inv is None:
        return None
    if not isinstance(inv, dict) or not set(inv) <= {"code", "map", "n", "of"} or not isinstance(inv.get("code"), str) or not ROOM_CODE.match(inv["code"]):
        return False
    file, n, size = inv.get("map"), inv.get("n"), inv.get("of")
    if (file is not None and map_file(file) is None) or not whole(n, 0, SEATS) or not whole(size, 1, SEATS) or n > size:
        return False
    return Room(inv["code"], file, n, size, seq)


AWAY_WHERE = Where(AWAY, None, None, None, False, None, 0)      # (the whole of what an Away row says: the word)


def bare(where):
    """What a person is doing and nothing about the room: what is kept of an account that may not chat."""
    return Where(where.at, None, None, None, False, None, where.seq, where.priv)


def row_of(ident, name, mod, where, pic=None):
    """`mod` is False, True, or the role's word (`mod`, `admin`): a moderator's row says `mod` (for both roles, as it always did) and, with the word, `role`."""
    row = {"i": ident, "name": name, "at": where.at if where else "look"}
    if pic:
        row["pic"] = pic                        # (the public name of the account's picture, 32 hex digits, where the account has one that may be shown)
    if mod:
        row["mod"] = True
        if mod in ("mod", "admin"):
            row["role"] = mod
    if where is not None and where.pub:
        row["pub"] = True
        for key, value in (("map", where.map), ("n", where.n), ("of", where.of), ("code", where.code)):
            if value is not None:
                row[key] = value
    elif where is not None and where.priv:
        row["priv"] = True                      # (a private room or match: a fact, with no map, seats or code, and no way in)
    elif where is not None and where.solo:
        row["solo"] = True                      # (a game against the computer, with the map if the page said one)
        if where.map is not None:
            row["map"] = where.map
    return row


class Peers(set):
    """The open sockets of the chat (a plain set for everything that reads it). `version` goes up at every change of the set, and when a socket in it closes (hub.server.Peer), so that the number of people is
    counted again only after a change and not for every ping."""

    def __init__(self):
        super().__init__()
        self.version = 0


def _bumping(name):
    base = getattr(set, name)

    def method(self, *args):
        self.version += 1
        return base(self, *args)
    method.__name__ = name
    return method


for _name in ("add", "discard", "remove", "clear", "pop", "update", "difference_update", "intersection_update", "symmetric_difference_update", "__ior__", "__iand__", "__isub__", "__ixor__"):
    setattr(Peers, _name, _bumping(_name))


class Presence:
    def __init__(self, chat, limits, clock, dumps):
        self.chat = chat
        self.limits = limits
        self.clock = clock
        self.dumps = dumps
        self.where = {}                 # peer -> Where: what its page last said (a socket that never said anything is looking)
        self.rooms = {}                 # peer -> Room: the room that its page said it waits in, for invitations (docs/HUB_PEOPLE.md); no list shows it
        self.watchers = set()           # the peers that asked for the list
        self.asked = {}                 # peer -> when it last asked for the whole list
        self.ids = {}                   # account id -> the number that names its row for as long as the account has a socket open (the welcome says it to the account's own sockets, never the account)
        self.next_id = 1
        self.seq = 0                    # the order in which pages said what they do (the newer one wins between two sockets of one account that do the same)
        self.sent = None                # the list as it was last sent: {"n", "g", "more", "rows": {id: row}}
        self.sent_bytes = None          # ... as one frame, for the next page that asks
        self.sent_at = float("-inf")    # when the list was last sent: the next send is `who_seconds` after that at the soonest, whatever happens
        self.limited = (frozenset(), frozenset())       # the accounts that are banned and muted, as the store last said
        self.limited_seq = None                         # ... the store's number of its last change of them when it said so (None: it has not said yet)
        self.limited_until = float("inf")               # ... and when the soonest of those mutes runs out
        self.away = collections.OrderedDict()   # account id -> [until, name]: the accounts whose last socket closed, oldest first, for `away_seconds` (the row keeps its number; nothing of a room)
        self.known = {}                 # account id -> the name of the accounts that had a socket open at the last look of the clock (to see which one closed since)
        self.counted = 0                # the number of people, and the version of the sockets that it was counted for
        self.counted_version = None

    def alive(self, peer):
        return peer in self.chat.peers and not getattr(peer, "closed", False)

    def people(self):
        """How many people are here: the accounts that have a socket open (one for each, however many tabs) and the sockets that have no login (a guest is one socket). The chat's "here now" is this too.
        Counted again only when the sockets changed (the pings, which are most of what asks, are answered from the last count)."""
        peers = self.chat.peers
        version = getattr(peers, "version", None)
        if version is not None and version == self.counted_version:
            return self.counted
        accounts, guests = set(), 0
        for peer in peers:
            if getattr(peer, "closed", False):
                continue
            if peer.identity is None:
                guests += 1
            else:
                accounts.add(peer.identity.account_id)
        count = len(accounts) + guests
        if version is not None:
            self.counted, self.counted_version = count, version
        return count

    def row_id(self, account):
        """The number of the account's row: made when it is first asked for (the welcome of its first socket) and kept while the account has a socket open."""
        ident = self.ids.get(account)
        if ident is None:
            ident = self.ids[account] = self.next_id
            self.next_id += 1
        return ident

    def restricted(self):
        """The accounts that are banned and the accounts that are muted now, as the store says. A look that finds nothing changed is one small read, whatever the number of bans and mutes: the two lists are
        read again only when the store's number of its last change of them is another (a ban or a mute made or lifted here or by the command line) or when the soonest mute has run out. A store that does
        not answer leaves what it said last."""
        try:
            store = self.chat.store
            seq = store.last_restriction()
            if seq != self.limited_seq or store.clock() >= self.limited_until:
                bans, mutes = store.bans(), store.mutes()
                self.limited = (frozenset(row[0] for row in bans), frozenset(row[0] for row in mutes))
                self.limited_seq = seq
                self.limited_until = min((row[2] for row in mutes), default=float("inf"))
        except StoreError:
            pass
        return self.limited

    # ---- the list --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def view(self):
        """The list as it is now: the people (an account once), who of them is where, in the order that decides who is left out when there are more than `who_rows_max`. An account that is banned from the chat is
        a guest here (counted, never named); one that is muted, or too new to chat, is named and has none of the room's details: what a page says is kept for the accounts that may speak."""
        banned, muted = self.restricted()
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
                best[who.account_id] = (key, who.name, where, who.young)
        # (the numbers of people who left are dropped once a tick, in flush: a look between a close and the tick must not take the number of a person who is about to be Away)
        for account in banned.intersection(best):
            del best[account]
            guests += 1
        gone = AWAY_WHERE
        soon = self.clock()
        away = {a: (None, entry[1], gone, False) for a, entry in self.away.items() if a not in best and a not in banned and entry[0] > soon}       # (an Away row is never a second row of a person who is here again, and never of a banned account)
        mods = {account: self.chat.roles.role_of(account) for account in list(best) + list(away)}
        mods = {account: role for account, role in mods.items() if role != "none"}
        people = list(best.items()) + list(away.items())
        order = sorted(people, key=lambda item: (item[0] not in mods, RANK[item[1][2].at if item[1][2] else "look"], item[1][1], item[0]))
        rows = {}
        for account, (_, name, where, young) in order[:self.limits.who_rows_max]:
            ident = self.row_id(account)
            rows[ident] = row_of(ident, name, mods.get(account, False), bare(where) if where is not None and (young or account in muted) else where, self.picture_of(account))
        return {"n": len(best) + guests, "g": guests, "more": max(0, len(people) - self.limits.who_rows_max), "rows": rows}

    def picture_of(self, account):
        """The name of the account's picture, where the hub has pictures and the account has one that may be shown (a banned account is never in the list, so its picture is not either)."""
        pictures = getattr(self.chat, "pictures", None)
        return pictures.name_of(self.chat.ref(account)) if pictures is not None else None

    def frame(self, view, previous):
        """The frame that takes a page that has `previous` to `view`: the rows that changed and the numbers of those that went, or the whole list when the page has none or the change is big."""
        numbers = {"n": view["n"], "g": view["g"], "more": view["more"]}
        if previous is not None:
            up = [row for ident, row in view["rows"].items() if previous["rows"].get(ident) != row]
            gone = [ident for ident in previous["rows"] if ident not in view["rows"]]
            if len(up) + len(gone) <= self.limits.who_diff_max:
                return dict({"t": "who", "full": False}, **numbers, rows=up, gone=gone)
        return dict({"t": "who", "full": True}, **numbers, rows=list(view["rows"].values()))

    def refresh(self, now, force=False):
        """Look at the list, and send the change to the pages that watch it: every page gets the same frame, and not sooner than `who_seconds` after the last send, whatever happens (a person who flaps, a
        crowd that comes: changes that come while the last send is young wait for it to be old, and are one send). `force` looks at once and sends to nobody who was not watching: for the first page that
        asks when nobody keeps the list current."""
        if not force and now - self.sent_at < self.limits.who_seconds:
            return
        view = self.view()
        if view == self.sent:
            return
        self.sent_at = now
        previous, self.sent, self.sent_bytes = self.sent, view, None
        self.watchers = {p for p in self.watchers if self.alive(p)}
        if self.watchers:
            data = self.dumps(self.frame(view, previous))
            for peer in list(self.watchers):
                peer.send_text(data)
        self.chat.follows.after_refresh()           # (the people who follow somebody are sent their list again: the numbers of the rows of those who came or went)

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
        if self.sent is None or not any(self.alive(p) for p in self.watchers):
            self.refresh(now, force=True)           # (nobody has kept the list current: look now; nobody else is sent anything)
        self.watchers.add(peer)                     # (every watcher has the list as last sent, and every change after that is sent to it too)
        peer.send_text(self.snapshot())

    def on_here(self, peer, msg):
        if peer.identity is None:
            return self.chat.mistake(peer, "login")
        self.seq += 1
        where = parse_here(msg, self.seq)
        room = parse_room(msg, self.seq)
        if where is None or room is False or (room is not None and where.at != "wait"):
            return self.chat.mistake(peer, REFUSED)
        self.where[peer] = where
        if room is None:
            self.rooms.pop(peer, None)
        else:
            self.rooms[peer] = room
        self.chat.follows.room_opened(peer, where)          # (a public room that its host's page says: the people who follow the host are told, hub/follows.py)

    # ---- what an account is doing, for invitations ----------------------------------------------------------------------------------------------------------------------------------------

    def sockets(self, account):
        return [p for p in self.chat.peers if p.identity is not None and p.identity.account_id == account and not getattr(p, "closed", False)]

    def doing(self, account):
        """What the account is doing now, as its row says it (`look`, `wait` or `play`: the sockets' newest word of the one that does the most), or None when it has no socket open."""
        best = None
        for peer in self.sockets(account):
            where = self.where.get(peer)
            key = (WEIGHT[where.at], where.seq) if where else (0, 0)
            if best is None or key > best[0]:
                best = (key, where.at if where else "look")
        return best[1] if best else None

    def room_of(self, account):
        """The Room that the account's newest page that waits in a room said it is in, or None (no socket, no room, a page that said nothing of one)."""
        best = None
        for peer in self.sockets(account):
            room, where = self.rooms.get(peer), self.where.get(peer)
            if room is not None and where is not None and where.at == "wait" and (best is None or room.seq > best.seq):
                best = room
        return best

    # ---- the clock's work ------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def forget(self, account):
        """The account's Away row is taken now, and it is not made again by the sockets that close with this call (a logout, a forget-me, a kick or a ban, a login that ran out)."""
        self.away.pop(account, None)
        self.known.pop(account, None)

    def flush(self, now):
        """Once a tick: forget the sockets that are gone, keep the row of an account whose last socket closed (Away, for `away_seconds`), and send the change if somebody watches."""
        alive = {p for p in self.chat.peers if not getattr(p, "closed", False)}
        self.counted_version = None                 # (the number of people is counted again at least once a tick)
        self.where = {p: w for p, w in self.where.items() if p in alive}
        self.rooms = {p: r for p, r in self.rooms.items() if p in alive}
        self.asked = {p: t for p, t in self.asked.items() if p in alive}
        self.watchers &= alive
        here = {p.identity.account_id: p.identity.name for p in alive if p.identity is not None}
        left = [a for a in self.known if a not in here]
        if left:
            banned = self.restricted()[0]
            for account in left:
                if account not in banned and self.limits.away_seconds > 0:
                    self.away.pop(account, None)
                    self.away[account] = [now + self.limits.away_seconds, self.known[account]]
            while len(self.away) > self.limits.away_max:
                self.away.popitem(last=False)       # (the oldest goes first: a flood of accounts that come and go never makes the table grow)
        self.known = here
        for account in [a for a, entry in self.away.items() if a in here or entry[0] <= now]:
            del self.away[account]
        self.ids = {a: i for a, i in self.ids.items() if a in here or a in self.away}
        if self.watchers:
            self.refresh(now)

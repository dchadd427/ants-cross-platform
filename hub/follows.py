"""Follows (docs/HUB_PEOPLE.md, "Follows"): a person follows another person, and nobody is told. A follow is one row of the account's own list, by the pseudonymous references of both people (hub/store/, never an id); only its
owner ever gets the list, and no frame, log or page holds who follows whom, or how many people follow somebody. What a follow gives: the Following tab of the players list and a star by the name, and the alert `room`
("<person> opened a room") when the person opens a PUBLIC room. A person who ignores the follower gives them nothing, and nothing tells the follower.

Down, to the account's own sockets only: {"t":"follows","rows":[{"h","name","i","k"}]} (sent after the welcome of a login when the list is not empty, and again whenever it changed: a follow, an unfollow, a person who came or went). `h`
is the person's handle (hub/chat.py `handle`: a keyed mark that is new with every start; a page names a person to the hub by it, and only a handle that the hub sent to this account can be used), `name` the name that is shown now (the
name that was shown when the follow was made, for a person who is not here), `i` the number of the person's row in the list of the front page while they are here, `k` the key that marks the person's lines (hub/chat.py `key_of`) where the
hub knows the account, so that a sheet that opens on a line can tell. A page never gets a reference or an id.

The commands (hub/commands.py): /follow WORD and /unfollow WORD, where WORD is a shown name, `row:N` (a row of the list), `#N` (the author of a line in the history) or `h:HANDLE`; the answers are the private `reply` of the command: `ok` {to, n}, `usage`,
`nobody`, `ambiguous`, `self`, `full`, and for /unfollow `not`.

ROOM ALERTS. When a page that waits in a PUBLIC room says its code (the host's page does; hub/presence.py), the people who follow the host and are here get a `room` alert (hub/people.py: the same row as an invitation's, `from`, `map`,
`n`, `of`, `o` and `code` while the room is open, and the same end: the room closes, fills or is left), unless they are in a match or in that room already, ignore the host, have had 6 room alerts in the day or were told of a room of the host in
the last hour. The host is never told that anybody was. Never an invite-only room: its page says no code to the hub's list.
"""
import collections
import re

from .limits import RateByAccount
from .people import Live
from .store import StoreError

LINE_WORD = re.compile(r"#[0-9]{1,9}\Z")
ROW_WORD = re.compile(r"row:[0-9]{1,9}\Z")
HANDLE_WORD = re.compile(r"h:[0-9a-f]{12}\Z")
KNOWN_MAX = 400                                  # the handles that the hub remembers it told one account (the oldest go first)

Person = collections.namedtuple("Person", "ref account name")      # who a word means: the reference, the account when the hub knows it (a person who is here, or wrote lately) else None, and the name to say


class Follows:
    def __init__(self, chat, limits, clock, dumps):
        self.chat = chat
        self.limits = limits
        self.clock = clock
        self.dumps = dumps
        self.hosts = RateByAccount(1, limits.follow_room_host_seconds)                                      # a room alert for the people who follow a host: one wave in this long (keyed by the run's key of the host)
        self.days = RateByAccount(limits.follow_room_day, 86400.0 / max(1, limits.follow_room_day))        # the room alerts that a person gets from everybody (keyed by the run's key of the person)
        self.rooms = {}                          # account -> the code of the public room that its page last said it waits in (memory: a room that is said again is not a new one)
        self.lists = {}                          # the reference of an account -> [(the reference of a person it follows, the name shown when it did)] (a copy of the store's, for accounts that have a socket open)
        self.known = {}                          # account -> {handle: (reference, name)}: the people that the hub told this account of (hub/chat.py `handle`), which is all that a page can name by a handle
        self.last = {}                           # account -> the frame that its pages were last sent

    def forget_idle(self, now):
        self.hosts.forget_idle(now)
        self.days.forget_idle(now)
        here = {a for a in list(self.known) + list(self.rooms) + list(self.last) if self.chat.presence.sockets(a)}
        for table in (self.known, self.rooms, self.last):
            for account in [a for a in table if a not in here]:
                del table[account]
        keep = {self.chat.ref(a) for a in here}
        for ref in [r for r in self.lists if r not in keep]:
            del self.lists[ref]

    # ---- people by word and by handle ----------------------------------------------------------------------------------------------------------------------------------------------

    def teach(self, account, ref, name):
        """The handle of the person `ref` for this account's pages, and the hub remembers that it told it (a page can use only a handle that came from the hub)."""
        handle = self.chat.handle(ref)
        mine = self.known.setdefault(account, collections.OrderedDict())
        mine[handle] = (ref, name)
        mine.move_to_end(handle)
        while len(mine) > KNOWN_MAX:
            mine.popitem(last=False)
        return handle

    def by_handle(self, account, handle):
        """The Person that a handle of this account's page means, or None (a handle that the hub did not give this account, or from before a restart)."""
        found = self.known.get(account, {}).get(handle)
        if found is None:
            return None
        ref, name = found
        there = self.chat.account_of_ref(ref)
        return Person(ref, there, (self.chat.shown_name(there) or name) if there is not None else name)

    def resolve(self, me, word, banned_ok=False):
        """(Person, None) for the person that `word` means, or (None, `nobody`), or (None, `ambiguous`) for a name that two people show: a shown name (any letter case; a person who is here or wrote lately), `row:N` (a person who is here),
        `#N` (the author of a line in the history that the pages are sent: the name is the line's own) or `h:HANDLE`. A person who is banned from the chat is nobody here (`banned_ok`: for /ignore, which may name them)."""
        chat = self.chat
        banned = set() if banned_ok else chat.presence.restricted()[0]
        if HANDLE_WORD.match(word):
            person = self.by_handle(me, word[2:])
            return (person, None) if person is not None and (person.account is None or person.account not in banned) else (None, "nobody")
        if ROW_WORD.match(word):
            found = {a: chat.shown_name(a) for a, i in chat.presence.ids.items() if i == int(word[4:]) and chat.presence.sockets(a)}
        elif LINE_WORD.match(word):
            entry = next((l for l in chat.recent() if l.id == int(word[1:])), None)
            found = {entry.account: entry.name} if entry is not None and entry.account else {}
        else:
            found = chat.commands.named(word)
        found = {a: n for a, n in found.items() if a not in banned}
        if len(found) > 1:
            return None, "ambiguous"
        if not found:
            return None, "nobody"
        account, name = next(iter(found.items()))
        return Person(chat.ref(account), account, name), None

    # ---- the list ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def list_of(self, account):
        """[(reference, name)] of the people that the account follows (kept in memory while it has a socket open)."""
        ref = self.chat.ref(account)
        found = self.lists.get(ref)
        if found is None:
            found = self.lists[ref] = self.chat.store.follows(ref)
        return found

    def rows(self, account, online=None):
        """The account's list as its pages are sent it: by name, with the number of the row of each person who is here and the key of their lines where the hub knows them. `online` is {reference: account} of the people with a socket open."""
        chat = self.chat
        mine = self.list_of(account)
        if not mine:
            return []
        online = chat.refs_online() if online is None else online
        banned = chat.presence.restricted()[0]
        lines = None
        out = []
        for ref, stored in mine:
            there = online.get(ref)
            if there is None:
                if lines is None:
                    lines = {chat.ref(l.account): l.account for l in chat.recent() if l.account}
                known = lines.get(ref)
            else:
                known = there
            name = chat.shown_name(there) if there is not None else None
            name = name or stored
            row = {"h": self.teach(account, ref, name), "name": name}
            if there is not None and there not in banned and there in chat.presence.ids:
                row["i"] = chat.presence.ids[there]
            if known is not None:
                row["k"] = chat.key_of(known)
            out.append(row)
        out.sort(key=lambda r: (r["name"].casefold(), r["h"]))
        return out

    def frame_of(self, account, online=None):
        return self.dumps({"t": "follows", "rows": self.rows(account, online)})

    def send_list(self, peer, force=False):
        """The account's list, to one socket after the welcome (a reader has none; nothing is sent when the list is empty, unless `force`)."""
        who = peer.identity
        if who is None:
            return
        try:
            frame = self.frame_of(who.account_id)
            has = bool(self.list_of(who.account_id))
        except StoreError as e:                     # (the store does not answer: the page has no list until it does)
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return
        if force or has:
            peer.send_text(frame)
            if has:
                self.last[who.account_id] = frame

    def send_lists(self, account, online=None):
        """The account's list, to every socket of the account, when it is not what they were last sent."""
        frame = self.frame_of(account, online)
        if self.last.get(account) == frame:
            return
        self.last[account] = frame
        for peer in self.chat.presence.sockets(account):
            peer.send_text(frame)
        if not self.list_of(account):
            del self.last[account]                   # (a page that has an empty list needs no more of them: the next change is a follow)

    def after_refresh(self):
        """The list of the front page was sent again (hub/presence.py): the people who follow somebody are sent theirs, when a person came or went."""
        chat = self.chat
        accounts = {p.identity.account_id for p in chat.peers if p.identity is not None and not getattr(p, "closed", False)}
        online = None
        for account in accounts:
            if not self.list_of(account) and account not in self.last:
                continue
            online = chat.refs_online() if online is None else online
            self.send_lists(account, online)

    # ---- the commands ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def follow(self, peer, word):
        chat, me = self.chat, peer.identity.account_id
        say = lambda code, **fields: chat.commands.reply(peer, "follow", code, **fields)
        person, refusal = self.resolve(me, word)
        if person is None:
            return say(refusal)
        mine = chat.ref(me)
        if person.ref == mine:
            return say("self")
        try:
            kept = chat.store.follow_add(mine, person.ref, person.name, self.limits.follow_max)
        except StoreError as e:
            return chat.trouble(peer, e, "bad")
        if not kept:
            return say("full")
        self.lists.pop(mine, None)
        self.send_lists(me)
        say("ok", to=person.name, n=len(self.list_of(me)))

    def unfollow(self, peer, word):
        chat, me = self.chat, peer.identity.account_id
        say = lambda code, **fields: chat.commands.reply(peer, "unfollow", code, **fields)
        mine = chat.ref(me)
        if word.startswith("h:") or word.startswith("row:") or word.startswith("#"):
            person, refusal = self.resolve(me, word, banned_ok=True)
            if person is None:
                return say("not")
            gone = [person.ref]
        else:
            gone = [ref for ref, name in self.list_of(me) if name.casefold() == word.casefold()]
            for ref, name in list(self.list_of(me)):
                there = chat.account_of_ref(ref)
                if there is not None and ref not in gone and (chat.shown_name(there) or "").casefold() == word.casefold():
                    gone.append(ref)                # (the name that is shown now, for a person who changed it)
        removed = 0
        try:
            for ref in gone:
                removed += 1 if chat.store.follow_remove(mine, ref) else 0
        except StoreError as e:
            return chat.trouble(peer, e, "bad")
        if not removed:
            return say("not")
        self.lists.pop(mine, None)
        self.send_lists(me)
        say("ok", n=len(self.list_of(me)))

    # ---- a room that a host opens ------------------------------------------------------------------------------------------------------------------------------------------------------

    def room_opened(self, peer, where):
        """The page of `peer` said what it is doing (hub/presence.py `on_here`): when it waits in a public room and says its code, which a host's page does, the people who follow the host are told once."""
        who = peer.identity
        if who is None or where.at != "wait" or not where.code:
            return                                  # (a page says a code only for a public room that it hosts: hub/presence.py `parse_here`, so no private room, no match and no guest of a room is here)
        chat, account = self.chat, who.account_id
        if self.rooms.get(account) == where.code:
            return
        self.rooms[account] = where.code
        if who.young or account in chat.presence.restricted()[0] or account in chat.presence.restricted()[1]:
            return                                  # (an account that may not chat has none of its room in the list either)
        now = self.clock()
        if not self.hosts.take(chat.key_of(account), now):
            return
        room = chat.presence.room_of(account)
        n = where.n if where.n is not None else room.n if room is not None else None
        size = where.of if where.of is not None else room.of if room is not None else None
        if n is None or size is None or n >= size:
            return                                  # (no seats known, or none free: there is nothing to join)
        try:
            followers = chat.store.followers(chat.ref(account))
        except StoreError as e:
            if chat.on_store_error:
                chat.on_store_error(e)
            return
        online = chat.refs_online()
        body = {"from": who.name, "n": n, "of": size, "code": where.code, "open": 1}
        if where.map:
            body["map"] = where.map
        for fref in followers:
            target = online.get(fref)
            if target is None:
                continue
            if chat.presence.doing(target) == "play" or (chat.presence.room_of(target) is not None and chat.presence.room_of(target).code == where.code):
                continue
            if chat.commands.ignores_person(target, account) or not self.days.take(chat.key_of(target), now):
                continue
            alert = chat.people.add(target, "room", body, from_account=account)
            if alert is not None:
                chat.people.deliver(target, alert, Live(account, target, fref, where.code, now))

    def forget(self, account):
        """The account forgot itself (hub/login.py): its list and every follow of it are gone from the store; the memory goes too, and the pages that are open are sent their lists again."""
        self.known.pop(account, None)
        self.rooms.pop(account, None)
        self.lists = {}                              # (every list is read again: the follows of the account that went are out of the store)
        self.after_refresh()

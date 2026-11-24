"""The people part of the hub (docs/HUB_PEOPLE.md): the list of alerts that an account's bell shows, invitations to a room (`/invite`) and mentions in the chat (`@name`). No sockets here: a peer is the object that
hub.chat talks to.

An ALERT is one row of an account's own list (hub/store/: kept by the account's pseudonymous reference, the newest 30 of the last 7 days, the same on every device of the account): `invite` (somebody asked the
account into their room), `mention` (somebody wrote the account's name after an `@`), `join` (a person joined the account's room: the lobby's own news, told by the account's page) and `over` (the account's own match
is over: told by the page that came back from it, kept read), `room` (a person that the account follows opened a public room: hub/follows.py; the same row as an invitation) and `msg` (a person sent the account a private message:
hub/messages.py; one row for each person, with none of the words). A page is sent the list
(`{"t":"alerts","rows":[...]}`) after the welcome of a login and again whenever it changed, and every new alert at once (`{"t":"alert","row":{...}}`). A row is {"id", "at", "k", "u": 1 while it is unread}
and the fields of its kind: invite and room {"from", "map", "n", "of", "code", "o": 1 while the room is open}, mention {"from", "text"}, join {"name", "seat"}, over {"map", "place", "match"}, msg {"from", "h"}. Up, an account with a login may say `{"t":"seen","upto":id}` (the
list was looked at: everything up to that number is read), `{"t":"clearalerts"}` and `{"t":"note","k":"join","name":...,"seat":...}` and `{"t":"note","k":"over","map":...[,"place":n][,"match":id]}`; a guest has no list and says none of it (`login`).

INVITATIONS. `/invite NAME`, `/invite row:N` and `/invite #N` (a person of the list of the front page, the author of a line: the same words as a moderator's commands) invite the person to the room that the inviter's page
said it waits in (hub/presence.py `inv`: the room's code, map and seats, which the hub keeps for this and nothing else). The answer is a private `reply` of the command `invite`: `ok` {to}, `usage`, `nobody` (no such person is
here: a guest has no name, a banned account is no person here), `ambiguous` (two people show the name), `self`, `noroom`, `full` {n, of}, `busy` {to} (playing a match), `person` {to, wait} (one invitation to a person in a minute) and
`total` {wait} (five in a minute). A person who ignored the inviter (/ignore), or who is in the inviter's room already (as its own page says), is sent nothing and the inviter is told `ok`: the limits count it all the same, so the cases cannot be told apart. The code of an invite-only
room is in the invitation for as long as the room is open (the inviter's page keeps saying it is in it) and is taken out of the alert, and out of the database, when the room closes, fills or is left for more than a short
grace (a page that connects again must not end it), and in an hour at the most.

MENTIONS. A line that holds `@` at the start of a word, and after it the shown name of exactly one person (any letter case; the longest name wins, so a name with a space works; the name must end at a space, a mark or the
line's end) marks that name: the line's frame carries `m`, [[start, length, key]] in UTF-16 units of `text` (the page's own measure) and the key of the person (`k`: the page tells its own name by it), so every screen draws the same
marks and none guesses from the words. A person who is here or who wrote lately (a line of theirs is in the chat's memory) can be named; a guest has no name and a banned account is nobody; `@here` and `@everyone` are nobody's
(`everyone` and `here` are reserved names: hub/text.py). At most 10 names are marked in a line, and the first 3 people named (never the writer) are alerted, if they have not ignored the writer, if the writer has not
alerted them in the last 2 minutes, and if they have not had 10 mention alerts in the last hour. A line that a moderator deletes takes its mention alerts out of every list. The marks are memory, as the lines' ring is.
"""
import collections
import json
import math
import re

from . import pseudo
from .limits import RateByAccount
from .presence import map_file
from .store import StoreError
from . import text as textrules

LINE_WORD = re.compile(r"#[0-9]{1,9}\Z")      # a person named by the number of one of their lines (no username has a `#`)
ROW_WORD = re.compile(r"row:[0-9]{1,9}\Z")    # ... or by the number of their row in the list of the front page (no username has a `:`)
QUOTE_MAX = 80                                # the code points of a mention's line that its alert keeps
SEATS = ("Green", "Red", "Blue", "Black")     # the colors of a room, as the lobby says them
NUMBER = re.compile(r"[0-9]{1,18}\Z")
MATCH_ID = re.compile(r"ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\Z")      # a match's id (its replay's file name without `.antsrep`: web/history_page.js)


def utf16(text, upto):
    """The length of text[:upto] in UTF-16 units, which is how a page counts a string."""
    return sum(2 if ord(c) > 0xFFFF else 1 for c in text[:upto])


class Live:
    """An invitation whose room is still open: whose it is, where it went, and since when its room has not been seen."""

    __slots__ = ("inviter", "target", "ref", "code", "at", "missing")

    def __init__(self, inviter, target, ref, code, at):
        self.inviter, self.target, self.ref, self.code, self.at, self.missing = inviter, target, ref, code, at, None


class People:
    def __init__(self, chat, limits, clock, dumps):
        self.chat = chat
        self.limits = limits
        self.clock = clock
        self.dumps = dumps
        chat.store.alert_days = limits.alerts_days                  # (the regular purge cuts the alerts that are older: the page says that a list is kept for these days)
        self.dropped_seen = 0                       # how many alerts the store had taken out on its own when the lists were last sent again (a line that purge or make_room deleted)
        self.totals = RateByAccount(limits.invite_burst, limits.invite_refill_seconds)               # an account's invitations, across all its tabs (keyed by the run's key of the account, never the id)
        self.invited = {}                           # (the inviter's key, the person's reference) -> when: one invitation to a person in `invite_person_seconds`
        self.pairs = {}                             # (the writer's reference, the person's reference) -> when the writer last alerted the person by a mention
        self.hours = RateByAccount(limits.mention_hour, 3600.0 / max(1, limits.mention_hour))        # the mention alerts that a person gets from everybody, by the person's reference
        self.notes = RateByAccount(limits.note_burst, limits.note_refill_seconds)                   # the `join` notes of an account's pages
        self.overs = RateByAccount(limits.over_burst, limits.over_refill_seconds)                   # the `over` notes of an account's pages
        self.live = {}                              # alert id -> Live: the invitations whose rooms are open (memory: a hub that starts again closes them all, the first time a list is made)
        self.marks = {}                             # line id -> [[start, length, key]]: the names that a line marks (memory, as the lines' ring is)

    def forget_idle(self, now):
        self.totals.forget_idle(now)
        self.hours.forget_idle(now)
        self.notes.forget_idle(now)
        self.overs.forget_idle(now)
        for pair in [p for p, at in self.invited.items() if now - at >= self.limits.invite_person_seconds]:
            del self.invited[pair]
        for pair in [p for p, at in self.pairs.items() if now - at >= self.limits.mention_pair_seconds]:
            del self.pairs[pair]

    # ---- the list ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def row_of(self, alert, account=None):
        """An alert as the page is sent it, or None for a row that is not one of the kinds' shapes (a body that cannot be read: it is never sent on). `account` is the account whose page it is for: a `msg` row carries the handle of the
        sender, which the hub tells that account's pages by (hub/follows.py `teach`)."""
        try:
            body = json.loads(alert.body)
        except ValueError:
            return None
        if not isinstance(body, dict):
            return None
        row = {"id": alert.id, "at": alert.at, "k": alert.kind}
        if alert.unread:
            row["u"] = 1
        if alert.kind in ("invite", "room"):
            fields = {"from": body.get("from"), "map": body.get("map"), "n": body.get("n"), "of": body.get("of")}
            if not isinstance(fields["from"], str) or not (isinstance(fields["n"], int) and isinstance(fields["of"], int)):
                return None
            row.update({k: v for k, v in fields.items() if v not in (None, "")})
            if alert.id in self.live and body.get("open") == 1 and isinstance(body.get("code"), str):
                row["o"] = 1
                row["code"] = body["code"]
        elif alert.kind == "msg":
            if not isinstance(body.get("from"), str) or not isinstance(body.get("fr"), str) or not pseudo.REFERENCE.match(body["fr"]):
                return None
            row["from"] = body["from"]
            row["h"] = self.chat.follows.teach(account, body["fr"], body["from"]) if account is not None else self.chat.handle(body["fr"])
        elif alert.kind == "mention":
            if not isinstance(body.get("from"), str) or not isinstance(body.get("text"), str):
                return None
            row["from"], row["text"] = body["from"], body["text"]
        elif alert.kind == "over":
            if not isinstance(body.get("map"), str) or map_file(body["map"]) is None:
                return None
            row["map"] = body["map"]
            if type(body.get("place")) is int and 1 <= body["place"] <= 4:
                row["place"] = body["place"]
            if isinstance(body.get("match"), str) and MATCH_ID.match(body["match"]):
                row["match"] = body["match"]
        else:
            if not isinstance(body.get("name"), str) or body.get("seat") not in SEATS:
                return None
            row["name"], row["seat"] = body["name"], body["seat"]
        return row

    def rows(self, account):
        """The account's list as it goes to a page, newest first. An invitation that was open when the hub started is closed now (the hub does not know its room any more)."""
        ref = self.chat.ref(account)
        out = []
        for alert in self.chat.store.alerts(ref, self.limits.alerts_keep, self.limits.alerts_days):
            if alert.kind in ("invite", "room") and alert.id not in self.live:
                self.close(ref, alert)
            row = self.row_of(alert, account)
            if row is not None:
                out.append(row)
        return out

    def close(self, ref, alert):
        """An invitation that has ended: its room's code goes from the stored body, so that nothing of an invite-only room stays in the database."""
        try:
            body = json.loads(alert.body)
        except ValueError:
            return
        if isinstance(body, dict) and (body.get("open") == 1 or "code" in body):
            body.pop("code", None)
            body["open"] = 0
            self.chat.store.change_alert(ref, alert.id, json.dumps(body, ensure_ascii=False, separators=(",", ":")))

    def send_list(self, peer, force=True):
        """The account's list, to one socket (a guest has none). After a welcome (`force` False) an empty list is not sent: a page that has been told nothing has none."""
        who = peer.identity
        if who is None:
            return
        try:
            rows = self.rows(who.account_id)
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return
        if rows or force:
            peer.send_text(self.dumps({"t": "alerts", "rows": rows}))

    def store_dropped(self):
        """After a purge or a make_room: when the store took alerts out on its own (the mention of a line that it deleted), every open page is sent its list again."""
        if self.chat.store.alerts_dropped != self.dropped_seen:
            self.dropped_seen = self.chat.store.alerts_dropped
            self.refresh_all()

    def send_lists(self, account):
        """The account's list, to every socket of the account that is open."""
        for peer in self.chat.presence.sockets(account):
            self.send_list(peer)

    def refresh_all(self):
        """Every account that has a socket open is sent its list again (what a forgotten account or a deleted line took out of the others' lists)."""
        for account in {p.identity.account_id for p in self.chat.peers if p.identity is not None and not getattr(p, "closed", False)}:
            self.send_lists(account)

    def add(self, account, kind, body, from_account=None, line=None, unread=1, replace=False):
        """Keep an alert for the account: the new Alert, or None when the database refused it (the page that asked is not told: an alert is news, and a lost one is not a wrong answer). With `replace`, the account's alerts of this kind from
        the same person go first (one row for each person: the private messages)."""
        chat = self.chat
        try:
            alert = chat.store.add_alert(chat.ref(account), kind, json.dumps(body, ensure_ascii=False, separators=(",", ":")), chat.ref(from_account) if from_account else "", line, self.limits.alerts_keep, self.limits.alerts_days, unread, replace)
        except StoreError as e:
            if chat.on_store_error:
                chat.on_store_error(e)
            return None
        return alert

    def deliver(self, account, alert, live=None):
        """Tell the account's sockets of an alert that was just kept."""
        if live is not None:
            self.live[alert.id] = live
        row = self.row_of(alert, account)
        if row is None:
            return
        frame = self.dumps({"t": "alert", "row": row})
        for peer in self.chat.presence.sockets(account):
            peer.send_text(frame)

    # ---- the frames of a page ----------------------------------------------------------------------------------------------------------------------------------------------------------

    def on_seen(self, peer, msg):
        who = peer.identity
        if who is None:
            return self.chat.mistake(peer, "login")
        upto = msg.get("upto")
        if set(msg) != {"t", "upto"} or type(upto) is not int or not 0 <= upto < 2 ** 62:
            return self.chat.mistake(peer, "bad")
        try:
            changed = self.chat.store.read_alerts(self.chat.ref(who.account_id), upto)
        except StoreError as e:
            return self.chat.trouble(peer, e, "bad")
        if changed:
            self.send_lists(who.account_id)

    def on_clear(self, peer, msg):
        who = peer.identity
        if who is None:
            return self.chat.mistake(peer, "login")
        if set(msg) != {"t"}:
            return self.chat.mistake(peer, "bad")
        try:
            gone = self.chat.store.clear_alerts(self.chat.ref(who.account_id))
        except StoreError as e:
            return self.chat.trouble(peer, e, "bad")
        for alert_id in [i for i, live in self.live.items() if live.target == who.account_id]:
            del self.live[alert_id]
        if gone:
            self.send_lists(who.account_id)

    def on_note(self, peer, msg):
        """`{"t":"note","k":"join","name":...,"seat":...}`: the account's page says that a person joined its room (the lobby's own news); it becomes an alert of the account's own list. `{"t":"note","k":"over","map":...[,"place":n]
        [,"match":id]}`: the page that came back from the account's own match says so; it becomes an alert that is kept read (the page showed it already). Anything else is `bad`."""
        who = peer.identity
        if who is None:
            return self.chat.mistake(peer, "login")
        if msg.get("k") == "over":
            return self.on_over(peer, who, msg)
        name, seat = msg.get("name"), msg.get("seat")
        if set(msg) != {"t", "k", "name", "seat"} or msg.get("k") != "join" or seat not in SEATS:
            return self.chat.mistake(peer, "bad")
        text, why = textrules.clean_line(name)
        if why or len(text) > textrules.MAX_SHOWN:
            return self.chat.mistake(peer, "bad", count=False)        # (the name is the lobby's, not the visitor's: the page tells what it was given, and a name that cannot be used is not a mistake of the socket)
        if not self.notes.take(self.chat.key_of(who.account_id), self.clock()):
            return self.chat.mistake(peer, "rate", count=False)
        alert = self.add(who.account_id, "join", {"name": text, "seat": seat})
        if alert is not None:
            self.deliver(who.account_id, alert)

    def on_over(self, peer, who, msg):
        chat = self.chat
        file, place, match = msg.get("map"), msg.get("place"), msg.get("match")
        if not set(msg) <= {"t", "k", "map", "place", "match"} or not isinstance(file, str) or map_file(file) is None:
            return chat.mistake(peer, "bad")
        if place is not None and not (type(place) is int and 1 <= place <= 4):
            return chat.mistake(peer, "bad")
        if match is not None and not (isinstance(match, str) and MATCH_ID.match(match)):
            return chat.mistake(peer, "bad")
        now, key = self.clock(), chat.key_of(who.account_id)
        if not self.overs.take(key, now):
            return chat.mistake(peer, "rate", count=False)
        body = {"map": file}
        if place is not None:
            body["place"] = place
        if match is not None:
            body["match"] = match
            try:
                if any(r.get("match") == match for r in self.rows(who.account_id) if r["k"] == "over"):      # (the page that is shown again, or another tab of the account, says the same match: one alert)
                    return
            except StoreError as e:
                return chat.trouble(peer, e, "bad")
        alert = self.add(who.account_id, "over", body, unread=0)
        if alert is not None:
            self.deliver(who.account_id, alert)

    # ---- invitations -------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def target_of(self, word):
        """(the account that the word means, or None; `ambiguous` for two people with the name or None; the name to say for it or None): a shown name (any letter case; exactly one person who is here with it), `row:N`
        or `#N` (the author of a line that is in the history that the pages are sent: the name is the line's own, never the account's name now)."""
        chat = self.chat
        banned = chat.presence.restricted()[0]
        said = None
        if ROW_WORD.match(word):
            found = [a for a, i in chat.presence.ids.items() if i == int(word[4:]) and chat.presence.sockets(a)]
        elif LINE_WORD.match(word):
            entry = next((l for l in chat.recent() if l.id == int(word[1:])), None)
            found = [entry.account] if entry is not None and entry.account and chat.presence.sockets(entry.account) else []
            said = entry.name if entry is not None else None
        else:
            found = [a for a in chat.commands.named(word) if chat.presence.sockets(a)]
        found = [a for a in found if a not in banned]
        if len(found) > 1:
            return None, "ambiguous", None
        return (found[0] if found else None), None, said

    def invite(self, peer, word):
        """`/invite WORD` (hub/commands.py): see the top of this file for every answer. The room comes first (a person with no room, or a full one, learns nothing about anybody), and an answer that tells that a
        person is here (`nobody`, `ambiguous`, `busy`) costs one of the account's five invitations a minute, so that nobody can look through the names or the lines for who is here."""
        chat, who = self.chat, peer.identity
        say = lambda code, **fields: chat.commands.reply(peer, "invite", code, **fields)
        room = chat.presence.room_of(who.account_id)
        if room is None:
            return say("noroom")
        if room.n >= room.of:
            return say("full", n=room.n, of=room.of)
        key, now = chat.key_of(who.account_id), self.clock()

        def probe(code, **fields):
            if not self.totals.take(key, now):
                return say("total", wait=self.totals.wait_seconds(key, now))
            say(code, **fields)

        try:
            target, refusal, said = self.target_of(word)
        except StoreError as e:
            return chat.trouble(peer, e, "bad")
        if refusal or target is None:
            return probe(refusal or "nobody")
        if target == who.account_id:
            return say("self")
        shown = said or chat.shown_name(target) or word
        if chat.presence.doing(target) == "play":
            return probe("busy", to=shown)
        tref = chat.ref(target)
        last = self.invited.get((key, tref))
        if last is not None and now - last < self.limits.invite_person_seconds:
            return say("person", to=shown, wait=max(1, math.ceil(self.limits.invite_person_seconds - (now - last))))
        if not self.totals.take(key, now):
            return say("total", wait=self.totals.wait_seconds(key, now))
        self.invited[(key, tref)] = now
        there = chat.presence.room_of(target)
        if not chat.commands.ignores_person(target, who.account_id) and not (there is not None and there.code == room.code):      # (a person who is in the room already, as its own page says, is sent nothing and the inviter is told `ok`, as for a person who ignored them: the answer says nothing of where a person is)
            body = {"from": who.name, "n": room.n, "of": room.of, "code": room.code, "open": 1}
            if room.map:
                body["map"] = room.map
            alert = self.add(target, "invite", body, from_account=who.account_id)
            if alert is not None:
                self.deliver(target, alert, Live(who.account_id, target, tref, room.code, now))
        say("ok", to=shown)

    def sweep(self, now):
        """Once a tick: an invitation ends when its room has not been seen (open, with a free seat, the inviter's page saying it is in it) for the grace, and in an hour whatever happens."""
        for alert_id, live in list(self.live.items()):
            room = self.chat.presence.room_of(live.inviter)
            here = room is not None and room.code == live.code and room.n < room.of
            if now - live.at >= self.limits.invite_live_seconds:
                self.end(alert_id, live)
            elif here:
                live.missing = None
            elif live.missing is None:
                live.missing = now
            elif now - live.missing >= self.limits.invite_grace_seconds:
                self.end(alert_id, live)

    def end(self, alert_id, live):
        self.live.pop(alert_id, None)
        chat = self.chat
        try:
            for alert in chat.store.alerts(live.ref, self.limits.alerts_keep, self.limits.alerts_days):
                if alert.id == alert_id:
                    self.close(live.ref, alert)
                    break
        except StoreError as e:
            if chat.on_store_error:
                chat.on_store_error(e)
            return
        self.send_lists(live.target)

    def forget(self, account):
        """The account forgot itself (hub/login.py): its invitations and those to it end with it, and the others' lists are sent again (what it was in is gone from the store)."""
        for alert_id in [i for i, live in self.live.items() if account in (live.inviter, live.target)]:
            del self.live[alert_id]
        self.refresh_all()

    def lines_gone(self, ids):
        """Chat lines were deleted (a moderator's /del, a ban's purge, a forget): their marks go, and so do the alerts that were of them; the accounts whose list changed are sent it again."""
        for line_id in ids:
            self.marks.pop(line_id, None)
        try:
            refs = self.chat.store.drop_alerts_of_lines(list(ids))
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return
        if refs:
            for peer in list(self.chat.peers):
                who = peer.identity
                if who is not None and not getattr(peer, "closed", False) and self.chat.ref(who.account_id) in refs:
                    self.send_list(peer)

    # ---- mentions ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def candidates(self):
        """{(length, folded name): the accounts that show it}: the people who have a socket open and the authors of the lines that are in the chat's memory (a guest has no account, a line of the hub's own none), minus the banned."""
        banned = self.chat.presence.restricted()[0]
        found = collections.defaultdict(set)
        for peer in self.chat.peers:
            who = peer.identity
            if who is not None and not getattr(peer, "closed", False) and who.account_id not in banned:
                found[(len(who.name), who.name.casefold())].add(who.account_id)
        for entry in self.chat.ring:
            if entry.account and entry.account not in banned:
                found[(len(entry.name), entry.name.casefold())].add(entry.account)
        return found

    def scan(self, text):
        """([[start, length, key], ...], the accounts named, in the order of the line, once each): what the line marks. A name must follow an `@` that starts a word, be exactly one person's, and end at a character that is not a letter, a digit or `_`."""
        if "@" not in text:
            return [], []
        found = self.candidates()
        names = sorted(found, key=lambda n: (-n[0], n[1]))
        marks, accounts, at = [], [], text.find("@")
        while at != -1 and len(marks) < self.limits.mention_marks:
            end = at + 1
            if at == 0 or text[at - 1].isspace():
                rest = text[at + 1:]
                for size, folded in names:
                    after = rest[size:size + 1]
                    if len(rest) >= size and rest[:size].casefold() == folded and (after == "" or not (after.isalnum() or after == "_")):
                        people = found[(size, folded)]
                        if len(people) == 1:
                            account = next(iter(people))
                            marks.append([utf16(text, at), utf16(text, at + 1 + size) - utf16(text, at), self.chat.key_of(account)])
                            if account not in accounts:
                                accounts.append(account)
                            end = at + 1 + size
                        break
            at = text.find("@", end)
        return marks, accounts

    def remember(self, line_id, marks):
        """The marks of a line, kept for the screens that are sent it later (its frame now, the history of a page that comes after); the lines that are no more in the chat's memory lose theirs."""
        if len(self.marks) > 2 * self.limits.history_lines:
            alive = {line.id for line in self.chat.ring}
            self.marks = {i: m for i, m in self.marks.items() if i in alive}
        self.marks[line_id] = marks

    def mentions(self, entry, who, accounts):
        """The line `entry` of `who` was kept and sent: alert the first people that it names."""
        chat = self.chat
        sender, now = who.account_id, self.clock()
        sref, quote = chat.ref(sender), quote_of(entry.text)
        for account in [a for a in accounts if a != sender][:self.limits.mention_people]:
            tref = chat.ref(account)
            if chat.commands.ignores_person(account, sender):
                continue
            last = self.pairs.get((sref, tref))
            if last is not None and now - last < self.limits.mention_pair_seconds:
                continue
            if not self.hours.take(tref, now):
                continue
            self.pairs[(sref, tref)] = now
            alert = self.add(account, "mention", {"from": who.name, "text": quote}, from_account=sender, line=entry.id)
            if alert is not None:
                self.deliver(account, alert)


def quote_of(text):
    """The first QUOTE_MAX code points of a line, with an ellipsis where it was cut."""
    return text if len(text) <= QUOTE_MAX else text[:QUOTE_MAX - 1].rstrip(" ") + "\u2026"


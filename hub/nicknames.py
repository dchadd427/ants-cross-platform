"""Nicknames, tags and the names that the game server takes (docs/HUB_NICKNAMES.md). A person who is logged in has ONE nickname (unique, 3 to 16 characters) and an optional TAG (2 to 5 letters and digits, shown before it:
"[TAG] Nickname"); a guest may type a name for the games, which is shown as "Name (Guest)". The rules of the text are hub/nickrules.py, the letters hub/name_letters.py; this file is who has what, how often it may change,
the frames of the page's "Your name" sheet, and the pass that lets the game server take a name.

Up, from any socket (a reader may only check a guest's name): {"t":"name","r":n,"nick":"Zoe","tag":"ANT","save":false}, or {"t":"name","r":n,"guest":true,"nick":"Wren"}. Down, to the asking socket:
{"t":"name","r":n,"ok":bool,"nick":{"ok":bool,"why":word,"until":seconds}|null,"tag":{...}|null,"shown":"[ANT] Zoe","saved":bool}: each field says whether it is fine (`why` is a word of hub/nickrules.py, or `taken`,
`lookalike`, `soon` with `until`, the time when it may change again, or `rate`); `shown` is how the others will see the person; a field that is the same as the account has now is fine and changes nothing. `save` stores both
fields at once or none (a nickname that is refused leaves the tag as it was); a saved name reaches every socket of the account as a whole `you` frame (hub/chat.py `you_of`: `nick`, `tag`, `nickat`, `nickfree`, `tagfree`).
The page never learns whose nickname made a name a lookalike: `taken` is the same name in other letters or case, `lookalike` a name that reads like another (the two words, not the person).

Who may. A nickname is the account's own: nobody else, guest or logged in, may take its letters in any case, nor a lookalike of it (text.squash: dots, accents, a digit for the letter it looks like, a Cyrillic letter drawn like
a Latin one), nor a name that is a reserved word, a role or a placeholder. It may be changed once in 30 days; the old one is held for the account for 30 days, so that nobody takes it to pass for the person, and a person who
changes back has their old name again (and the 30 days start again). A tag may change once a day, is not unique and is not held. A moderator's /rename takes both away, and the account may choose again at once.
A guest's name may not copy a logged-in person's nickname (or a name that is held), has the same letters, length and word list, is kept in the guest's browser and is not reserved for the guest.

The game server. `play_name` is the name that a person plays under: "[TAG] Nickname", the nickname, or, for a person who has chosen none, the name that the login has (the shown name, then the other names, then the
username) if it passes the rules of a nickname (3 to 16 letters of the table in one alphabet, no reserved word or label of a computer player, nothing rude), is nobody's lookalike and is claimed by
no other account (`claim`: the first account to be passed for a name has it for 30 days); none when no name fits (the page plays as a guest). hub/namepass.py signs it
for two minutes with the key that the game server shares (`/hub/me` hands the pass to the page; the page tells it in its Hello and in each rename). A name that has no pass is a guest's on that server.
"""
import collections
import weakref

from . import name_letters, nickrules, session as sessionmod, text
from .limits import RateByAccount, TokenBucket
from .store import StoreError

CLAIM_SECONDS = 30 * 86400                        # the login name of a person with no nickname is claimed for the game this long after the last pass
NICK_WAIT_SECONDS = 30 * 86400                    # a nickname changes once in this long ...
HOLD_SECONDS = 30 * 86400                         # ... and the old one is held for its person this long
TAG_WAIT_SECONDS = 86400                          # a tag changes once a day
CHECK_BURST, CHECK_REFILL_SECONDS = 12, 0.25      # the checks that one socket may ask: a burst, then four a second (the page waits a moment after the last key)
SAVE_BURST, SAVE_REFILL_SECONDS = 3, 20.0         # the saves of one account, across all its tabs

Reply = collections.namedtuple("Reply", "ok nick tag shown")


def field(ok, why=None, until=None):
    out = {"ok": ok}
    if why:
        out["why"] = why
    if until:
        out["until"] = until
    return out


class Nicknames:
    def __init__(self, chat, limits, clock, dumps):
        self.chat = chat
        self.limits = limits
        self.clock = clock
        self.dumps = dumps
        self.saves = RateByAccount(SAVE_BURST, SAVE_REFILL_SECONDS)
        self.buckets = weakref.WeakKeyDictionary()          # peer -> the bucket of its checks
        self.pass_maker = None                              # namepass.Maker, made by the hub when it has the key that the game server shares

    # ---- the store ----------------------------------------------------------------------------------------------------------------------------------------------------------------

    def wall(self):
        """Seconds since 1970 (the store's clock: what a nickname's times and a pass are written in); `clock` is the hub's own, for the limits of rates."""
        return self.chat.store.clock()

    def row(self, account):
        """The account's nickname row (nick, key, nick_at, tag, tag_at) or None; a store that cannot answer is no nickname for now (the caller's frame goes on)."""
        try:
            return self.chat.store.nick_row(account)
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return None

    def owner_of(self, name, account=None):
        """The account that holds a name that looks like `name` (its nickname, or one that it holds), or None; `account` itself does not count."""
        try:
            found = self.chat.store.nick_owner(text.squash(name))
        except StoreError:
            return None
        return found if found is not None and found[0] != account else None

    def dress(self, who):
        """The Identity of a socket with the account's nickname and tag, when it has them: the nickname is the name that is shown (the switch between the display name and the username is not offered then). An account that
        has none shows what its login says (the display name, or the username when that is what it picked or when it has no display name), unless the name that would be shown is a lookalike of somebody's nickname:
        then it shows its username, which Discord keeps unique, and when that looks like a nickname too, "username (Discord)" (and the other name is not offered)."""
        row = self.row(who.account_id)
        if row is not None:
            return who._replace(name=row.nick, pick="", other="", nick=row.nick, tag=row.tag)
        if self.owner_of(who.name, who.account_id) is None:
            return who._replace(nick="", tag="")
        if who.username != who.name and self.owner_of(who.username, who.account_id) is None:
            return who._replace(name=who.username, pick="username", other="", nick="", tag="")
        return who._replace(name=nickrules.login_shown(who.username), pick="username", other="", nick="", tag="")

    def state(self, who):
        """What a page is told of the account's own name, for `you`: the nickname and the tag when there are some, when each was set, and the time when each may change again (only while it may not)."""
        row = self.row(who.account_id)
        if row is None:
            return {}
        now = self.wall()
        out = {"nick": row.nick, "nickat": row.nick_at}
        if row.nick_at + NICK_WAIT_SECONDS > now:
            out["nickfree"] = row.nick_at + NICK_WAIT_SECONDS
        if row.tag:
            out["tag"] = row.tag
        if row.tag_at and row.tag_at + TAG_WAIT_SECONDS > now:
            out["tagfree"] = row.tag_at + TAG_WAIT_SECONDS
        return out

    # ---- the checks ---------------------------------------------------------------------------------------------------------------------------------------------------------------

    def check_nick(self, account, raw, now):
        """(field, nickname or None) of a nickname for the account (None: a guest): the rules of the text, then who holds it, then the account's own limit."""
        nick, why = nickrules.check_nickname(raw)
        if why:
            return field(False, why), None
        row = self.row(account) if account else None
        if row is not None and row.nick == nick:
            return field(True), nick                           # (what it has: nothing changes, and no limit is asked)
        try:
            found = self.chat.store.nick_owner(text.squash(nick))
        except StoreError:
            return field(False, "busy"), None
        if found is not None and found[0] != account:
            return field(False, "taken" if found[1] == nick.casefold() else "lookalike"), None
        if account and row is not None and row.nick_at + NICK_WAIT_SECONDS > now:
            return field(False, "soon", row.nick_at + NICK_WAIT_SECONDS), None
        return field(True), nick

    def check_tag(self, account, raw, nick, now):
        tag, why = nickrules.check_tag(raw, nick or "")
        if why:
            return field(False, why), None
        row = self.row(account)
        if row is not None and row.tag == tag:
            return field(True), tag
        if row is not None and row.tag_at and row.tag_at + TAG_WAIT_SECONDS > now:
            return field(False, "soon", row.tag_at + TAG_WAIT_SECONDS), None
        return field(True), tag

    def guest_check(self, raw):
        """A guest's name: (field, name). The rules of a nickname, and not a copy of a person's nickname (or one that is held for them)."""
        name, why = nickrules.check_nickname(raw)
        if why:
            return field(False, why), None
        try:
            found = self.chat.store.nick_owner(text.squash(name))
        except StoreError:
            return field(False, "busy"), None
        if found is not None:
            return field(False, "lookalike"), None
        return field(True), name

    # ---- the frames ---------------------------------------------------------------------------------------------------------------------------------------------------------------

    def forget_idle(self, now):
        self.saves.forget_idle(now)

    def bucket(self, peer):
        bucket = self.buckets.get(peer)
        if bucket is None:
            bucket = self.buckets[peer] = TokenBucket(CHECK_BURST, CHECK_REFILL_SECONDS, self.clock())
        return bucket

    def reply(self, peer, r, body):
        body.update({"t": "name", "r": r})
        peer.send_text(self.dumps(body))

    def on_frame(self, peer, msg):
        """One `name` frame: a check, or (with `save`) a change. A frame that is not shaped like one is a mistake of the socket; a rate that is over is answered `rate` (and costs a frame more, as every refusal that is no mistake)."""
        r = msg.get("r", 0)
        nick, tag, guest, save = msg.get("nick"), msg.get("tag", ""), msg.get("guest", False), msg.get("save", False)
        if not (isinstance(r, int) and not isinstance(r, bool) and 0 <= r < 1 << 31) or not isinstance(nick, str) or not isinstance(tag, str) or not isinstance(guest, bool) or not isinstance(save, bool):
            return self.chat.mistake(peer, "bad")
        who = peer.identity
        if guest and save:
            return self.chat.mistake(peer, "bad")                   # (a guest's name is kept in the guest's browser: the hub keeps nothing of it)
        if not guest and who is None:
            return self.chat.mistake(peer, "login")
        mono, now = self.clock(), self.wall()
        if not self.bucket(peer).take(mono):
            peer.charge()
            return self.reply(peer, r, {"ok": False, "why": "rate"})
        if guest:
            checked, name = self.guest_check(nick)
            return self.reply(peer, r, {"ok": checked["ok"], "nick": checked, "tag": None, "shown": nickrules.guest_shown(name) if name else None})
        account = who.account_id
        if save and not self.saves.take(account, mono):
            peer.charge()
            return self.reply(peer, r, {"ok": False, "why": "rate"})
        nick_field, new_nick = self.check_nick(account, nick, now)
        row = self.row(account)
        tag_field, new_tag = self.check_tag(account, tag, new_nick or (row.nick if row else ""), now) if new_nick or row else (None, None)
        if tag_field is None:
            tag_field = field(True) if not tag else field(False, "nick")        # (a tag needs a nickname to stand before)
        ok = nick_field["ok"] and tag_field["ok"]
        shown = nickrules.shown(new_nick, new_tag or "") if ok else None
        saved = False
        if ok and save:
            saved, nick_field = self.save(account, new_nick, new_tag or "", now, nick_field)
            ok = saved
            shown = nickrules.shown(new_nick, new_tag or "") if saved else None
        self.reply(peer, r, {"ok": ok, "nick": nick_field, "tag": tag_field, "shown": shown, "saved": saved})
        if saved:
            self.changed(account)

    def save(self, account, nick, tag, now, nick_field):
        """Store the nickname and the tag in one step: (True, field), or (False, the field of the refusal): a name that somebody took in the meantime is `taken` or `lookalike`."""
        store = self.chat.store
        row = self.row(account)
        nick_at = row.nick_at if row is not None and row.nick == nick else int(now)
        tag_at = row.tag_at if row is not None and row.tag == tag else (int(now) if tag or (row is not None and row.tag) else 0)
        try:
            conflict = store.nick_put(account, nick, nick.casefold(), text.squash(nick), nick_at, tag, tag_at, int(now) + HOLD_SECONDS)
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return False, field(False, "busy")
        if conflict is not None:
            return False, field(False, "taken" if conflict == nick.casefold() else "lookalike")
        return True, nick_field

    def changed(self, account):
        """The account's pages are told: each of its sockets is shown by the names that the account has now (the nickname and the tag, else what its login says) and gets the whole `you`. Lines that were written keep the
        name and the tag that they were written with."""
        try:
            profile = self.chat.store.profile(account)
        except StoreError:
            profile = None
        for peer in list(self.chat.peers):
            who = peer.identity
            if who is None or who.account_id != account or getattr(peer, "closed", False):
                continue
            bare = who._replace(nick="", tag="")
            if profile is not None and text.valid_name(profile.username):
                display = profile.display if text.valid_shown(profile.display) else ""
                bare = sessionmod.renamed(bare, profile.username, display, profile.pick)
            else:
                bare = bare._replace(name=who.username, pick="", other="")
            peer.identity = self.dress(bare)
            peer.send_text(self.dumps({"t": "you", "you": self.chat.you_of(peer.identity)}))

    def reset(self, account, note=None):
        """A moderator's /rename: the nickname and the tag go (and the note is in the moderators' log), the account may choose again at once. True when there was something. A store that cannot do it raises StoreError:
        the command tells the moderator that it did not happen."""
        gone = self.chat.store.nick_drop(account, note)
        if gone:
            self.changed(account)
        return gone

    # ---- the game's name and its pass -------------------------------------------------------------------------------------------------------------------------------------------

    def fits_game(self, name, account):
        """Whether a login's name may be the name of a game: the rules of a nickname (nickrules.check_nickname: 3 to 16 characters of the letters of the table in one alphabet, no reserved word such as owner, host, bot or
        player, nothing rude, not "Guest" and digits), unchanged by them (the name is not cut, trimmed or made over), and not a lookalike of somebody's nickname or of a nickname that is held."""
        if not isinstance(name, str):
            return False
        nick, _ = nickrules.check_nickname(name)
        return nick == name and self.owner_of(name, account) is None

    def claim(self, account, name):
        """Whether the account may play under this login name: no other account has been passed for it (or for a lookalike of it: text.squash) in the last 30 days. The first account to ask has the claim and keeps it as long as
        it asks; an account has one claim, the one that it asked for last. A store that cannot answer is a refusal (the person plays as a guest for now)."""
        try:
            return bool(self.chat.store.login_claim(account, text.squash(name), self.wall() + CLAIM_SECONDS))
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return False

    def play_name(self, who):
        """The name that the account plays under, or None when none fits."""
        row = self.row(who.account_id)
        if row is not None:
            return nickrules.shown(row.nick, row.tag)
        for name in (who.name, who.other, who.username):
            if name and self.fits_game(name, who.account_id) and self.claim(who.account_id, name):
                return name
        return None

    def play(self, who):
        """What /hub/me tells a page that is logged in: the game name and its pass ({"name": ..., "pass": ...}; the pass is absent where the hub has no key shared with the game server)."""
        name = self.play_name(who)
        out = {"name": name}
        if name and self.pass_maker is not None:
            out["pass"] = self.pass_maker.make(name, self.wall())
        return out


def name_ok(ch):
    return name_letters.is_letter(ch) or name_letters.is_digit(ch) or ch in nickrules.SIGNS

"""The commands of the chat (docs/HUB_COMMANDS.md): ONE table says what each is, which role may use it and how its line is read, and the answers are made here and nowhere else.

A line that starts with a single `/` is a command and never a line of the chat: it is not kept, not shown to anybody and not counted as a line (hub/chat.py `on_say` hands it to `Commands.run`;
`//` is one slash of a line that is said as any line is). The table is the only place that says who may use a command (`REGISTRY`; `lines_for` makes the `cmds` frame from it, and `NEEDS`, which
hub/chat.py `allowed` reads, is made from it), and `Commands.run` is the only place that decides that a word is not the account's own: a word that is no command and a command of a higher role get the
SAME answer, `{"t":"reply","c":"unknown","r":"unknown"}` (or, over the account's limit, the same `rate` answer: nothing in either tells which of the two it was). The role comes from hub/roles.py, never
from the page.

Down, besides `cmds` (the account's own commands: `{"t":"cmds","rows":[{"w","a","d","g"}]}`): `{"t":"reply","c":WORD,"r":CODE,...}`, the private answer to a command of this file, to the asking socket only
(the page owns the words). The moderators' eight old words and `/nopic` and `/pic` keep the answers they always had (`ok`, `bans`, `mutes` and the `err` codes: hub/chat.py).

`/bots` goes to the game server through hub/gamedoor.py. Its line is read by `parse_bots` into numbers and words of a fixed list, and the request is built from those; the one free text of the grammar,
the map's name, is looked up in the game server's list of maps and only that list's own file name is ever sent.
"""
import asyncio
import collections
import re

from . import gamedoor, text as textrules
from .limits import RateByAccount
from .store import StoreError

RANK = {"none": 0, "mod": 1, "admin": 2}
GROUPS = ("Everyone", "Moderators", "Administrators")
ROWS_MAX = 24                               # the most rows of a `cmds` frame
DESC_MAX = 40                               # the longest description of a row
MAX_COUNT = 10000                           # `/bots COUNT`: the most matches that one command asks for
COUNT_DIGITS = 5                            # ... and the most digits that it reads before it makes a number of them
MAX_EVERY = 120                             # `every MINUTES`: the longest spacing (minutes), in steps of half a minute
MAP_MAX = 96                                # the longest map name (characters): the game server's own limit
CLOSE_MAX = 3                               # the names that `bad_map` offers
QUEUES_SHOWN = 5                            # the queues that `/bots status` tells of
EVERY = re.compile(r"([0-9]{1,3})(\.5)?\Z")
COUNT = re.compile(r"[0-9]{1,%d}\Z" % COUNT_DIGITS)
HELD_WORDS = ("cap", "places", "people", "busy")       # why the first start of a batch waits (the game server's words)
RULES = {"highest": "highest-score", "187": "187"}
BOT_COUNTS = ("2", "3", "4")
LEVELS = {"easy": "easy", "medium": "medium", "hard": "hard", "mixed": "rmh", "any": "random"}
TEAMS = {"free": ("ffa", ""), "12-34": ("split", "12-34"), "13-24": ("split", "13-24"), "14-23": ("split", "14-23"), "1-23": ("split", "1-23"), "2-13": ("split", "2-13"), "3-12": ("split", "3-12")}
SPLIT_BOTS = {"12-34": "4", "13-24": "4", "14-23": "4", "1-23": "3", "2-13": "3", "3-12": "3"}       # the number of bots that a split is made for
OPTIONS = ("every", "rules", "bots", "levels", "teams", "map")
ROTATION = "random:rotation"                # the map of a start that names none: the hourly rotation's lists

Reply = collections.namedtuple("Reply", "code fields")             # what a reader of a line answers privately instead of doing anything (`reply` frame)
Err = collections.namedtuple("Err", "code")                        # ... or an `err` frame of the old kind (a moderator's command that is wrong)
Bots = collections.namedtuple("Bots", "kind count every_s rules bots levels teams split map_name")
Command = collections.namedtuple("Command", "word rank usage desc group bucket parse run")


def lower(word):
    """The word in lower case when it is plain ASCII, else as it is (so that no letter of another script can pass for one of the grammar's: the Kelvin sign is not a `k`)."""
    return word.lower() if word.isascii() else word


def small(value, high):
    """`value` when it is a whole number from 0 to `high` (a bool is no number), else None: what is taken from an answer of the game server."""
    return value if type(value) is int and 0 <= value <= high else None


# ---- the words that mean something --------------------------------------------------------------------------------------------------------------------

def parse_staff(commands, line):
    """A moderator's or an administrator's command (hub/chat.py `parse`): the job, or the `bad` of an `err` for a line that is not written right."""
    return commands.chat.parse(line) or Err("bad")


def run_staff(commands, peer, job):
    try:
        return commands.chat.command(peer, job)
    except StoreError as e:
        return commands.chat.trouble(peer, e, "bad")                   # (the person is told that the command did not happen, and may type it again)


def parse_help(commands, line):
    return True


def run_help(commands, peer, args):
    commands.reply(peer, "help", "list")


def parse_person(commands, line):
    """`/ignore NAME` and `/unignore NAME`: the name that is shown, the whole rest of the line (a shown name may hold blanks)."""
    name = " ".join(w for w in line.split(" ")[1:] if w)
    if not name:
        return Reply("usage", {})
    return name if len(name) <= textrules.MAX_SHOWN else Reply("nobody", {})        # (no name is shown longer than that: nobody is called so)


def run_ignore(commands, peer, name):
    commands.ignore(peer, name)


def run_unignore(commands, peer, name):
    commands.unignore(peer, name)


def run_invite(commands, peer, word):
    commands.chat.people.invite(peer, word)


def collapsed(words):
    return " ".join(w for w in words if w)


def bad_map(close=()):
    return Reply("bad_map", {"close": list(close)})


def parse_bots(commands, line):
    """The line of `/bots` as a `Bots`, or the `Reply` that answers it: `usage` for nothing or for words after `status` and `stop`, `bad_count` (the count is not 1 to 10,000: digits only, five at the most),
    `bad_every` (not whole minutes or a half, 0 to 120), `bad_option` (a word that is none of the options, an option twice, a value that is none of its words, a split that does not fit the number of bots) and
    `bad_map` (a name of no characters or more than 96, with a control character, or with a slash). `map` goes last and takes the rest of the line."""
    words = line.split(" ")[1:]
    if not words:
        return Reply("usage", {})
    if lower(words[0]) in ("status", "stop"):
        return Bots(lower(words[0]), 0, 0, "random", "random", "rmh", "random", "", None) if len(words) == 1 else Reply("usage", {})
    if not COUNT.match(words[0]) or not 1 <= int(words[0]) <= MAX_COUNT:
        return Reply("bad_count", {})
    chosen = {}
    at = 1
    while at < len(words):
        word = lower(words[at])
        if word not in OPTIONS or word in chosen:
            return Reply("bad_option", {})
        if word == "map":
            name = collapsed(words[at + 1:])
            if not 1 <= len(name) <= MAP_MAX or "/" in name or "\\" in name or any(ord(c) < 32 or ord(c) == 127 for c in name):
                return bad_map()
            chosen["map"] = name
            break
        if at + 1 >= len(words):
            return Reply("bad_every" if word == "every" else "bad_option", {})
        value = lower(words[at + 1])
        at += 2
        if word == "every":
            found = EVERY.match(value)
            minutes = int(found.group(1)) + (0.5 if found.group(2) else 0) if found else None
            if minutes is None or minutes > MAX_EVERY:
                return Reply("bad_every", {})
            chosen["every"] = int(minutes * 60)
        elif (word == "rules" and value not in RULES) or (word == "bots" and value not in BOT_COUNTS) or (word == "levels" and value not in LEVELS) or (word == "teams" and value not in TEAMS):
            return Reply("bad_option", {})
        else:
            chosen[word] = value
    teams = chosen.get("teams", "")
    if teams in SPLIT_BOTS and chosen.get("bots") != SPLIT_BOTS[teams]:
        return Reply("bad_option", {})                                  # (a split is of three or of four bots, and the line must say which)
    kind, split = TEAMS[teams] if teams else ("random", "")
    return Bots("start", int(words[0]), chosen.get("every", 0), RULES.get(chosen.get("rules"), "random"), chosen.get("bots", "random"), LEVELS.get(chosen.get("levels"), "rmh"), kind, split, chosen.get("map"))


def request_of(args, map_file):
    """The JSON of a start, made of the parsed fields alone (the map is the game server's own file name for it, or the rotation): every key the game server reads, in the shape that the admin page sends."""
    return {"count": args.count, "every_s": args.every_s, "map": map_file or ROTATION, "rules": args.rules, "bots": args.bots, "levels": [args.levels] * 4, "teams": args.teams, "split": args.split}


def run_bots(commands, peer, args):
    commands.start_bots(peer, args)


REGISTRY = (
    Command("help", 0, "", "list your commands", GROUPS[0], "other", parse_help, run_help),
    Command("invite", 0, "NAME", "invite someone to your room", GROUPS[0], "other", parse_person, run_invite),
    Command("ignore", 0, "NAME", "hide a person's lines", GROUPS[0], "other", parse_person, run_ignore),
    Command("unignore", 0, "NAME", "show their lines again", GROUPS[0], "other", parse_person, run_unignore),
    Command("mute", 1, "NAME MINUTES", "stop a person writing for a while", GROUPS[1], None, parse_staff, run_staff),
    Command("unmute", 1, "NAME", "let a muted person write again", GROUPS[1], None, parse_staff, run_staff),
    Command("del", 1, "NUMBER", "take one line off every screen", GROUPS[1], None, parse_staff, run_staff),
    Command("kick", 1, "NAME", "remove a person from the chat", GROUPS[1], None, parse_staff, run_staff),
    Command("mutes", 1, "", "list the people who are muted", GROUPS[1], None, parse_staff, run_staff),
    Command("ban", 2, "NAME [purge]", "ban a person from writing", GROUPS[2], None, parse_staff, run_staff),
    Command("unban", 2, "NAME", "lift a ban", GROUPS[2], None, parse_staff, run_staff),
    Command("bans", 2, "", "list the people who are banned", GROUPS[2], None, parse_staff, run_staff),
    Command("nopic", 2, "NAME", "hide a person's Discord picture", GROUPS[2], None, parse_staff, run_staff),
    Command("pic", 2, "NAME", "show it again", GROUPS[2], None, parse_staff, run_staff),
    Command("noimages", 2, "NAME", "stop a person posting images", GROUPS[2], None, parse_staff, run_staff),
    Command("images", 2, "NAME", "let them post images again", GROUPS[2], None, parse_staff, run_staff),
    Command("bots", 2, "COUNT [every MINUTES] [map NAME]  |  status  |  stop", "start matches of computer players", GROUPS[2], "bots", parse_bots, run_bots),
)
TABLE = {c.word: c for c in REGISTRY}
NEEDS = {c.word: c.rank for c in REGISTRY}              # the rank that a command needs: the one table of ranks (hub/chat.py `allowed` reads it)


def lines_for(role):
    """The rows of the `cmds` frame for an account of this role: the commands whose rank the role has, in the order of the table, and none above it."""
    rank = RANK.get(role, 0)
    return [{"w": "/" + c.word, "a": c.usage, "d": c.desc, "g": c.group} for c in REGISTRY if c.rank <= rank]


class Commands:
    def __init__(self, chat, dumps):
        self.chat = chat
        self.dumps = dumps
        limits = chat.limits
        self.limits = {"start": RateByAccount(limits.commands_start_burst, limits.commands_start_refill_seconds),
                       "control": RateByAccount(limits.commands_control_burst, limits.commands_control_refill_seconds),
                       "other": RateByAccount(limits.commands_other_burst, limits.commands_other_refill_seconds)}      # per account, across all its tabs (keyed by the run's key of the account, never the id)
        self.unknown_frame = dumps({"t": "reply", "c": "unknown", "r": "unknown"})
        self.ignores = self.load_ignores()          # the reference of an account -> {the reference of a person that it ignores: the name that was shown when it did}; kept in the store (docs/HUB_PEOPLE.md), read at the start
        self.door = None                            # hub/gamedoor.py GameDoor (the hub makes it)
        self.is_owner = lambda account: False       # whether the account is the operator's (the hub sets it): the role in the game server's pass
        self.flights = {}                           # the run's key of an account -> its `/bots` task that waits for the game server (one at a time for an account)

    def load_ignores(self):
        try:
            return self.chat.store.ignores()
        except StoreError as e:                      # (a store that cannot be read at the start: the lists are empty, and the hub says so; a new /ignore is written again)
            if getattr(self.chat, "on_store_error", None):
                self.chat.on_store_error(e)
            return {}

    def forget_idle(self, now):
        for limits in self.limits.values():
            limits.forget_idle(now)

    def stop(self):
        for task in list(self.flights.values()):
            task.cancel()

    # ---- what is said to a page ----------------------------------------------------------------------------------------------------------------------

    def send_list(self, peer):
        """The account's own commands, to this socket (a reader has none, and is sent nothing)."""
        who = peer.identity
        if who is not None:
            peer.send_text(self.dumps({"t": "cmds", "rows": lines_for(self.chat.roles.role_of(who.account_id))}))

    def reply(self, peer, word, code, **fields):
        peer.send_text(self.dumps(dict({"t": "reply", "c": word, "r": code}, **fields)))

    def over(self, peer, word, bucket):
        """False when the account may go on (a token of its bucket is spent), else True after the socket was told `rate` and the whole seconds that it waits (a refusal is no mistake of the socket, but costs
        it a frame of its budget more, like every refusal that is not its doing)."""
        key, now = self.chat.key_of(peer.identity.account_id), self.chat.clock()
        bucket = self.limits[bucket]
        if bucket.take(key, now):
            return False
        self.reply(peer, word, "rate", wait=bucket.wait_seconds(key, now))
        peer.charge()
        return True

    def not_yours(self, peer):
        """THE answer to a word that is not a command of the account's own, whether it is no command at all or one of a higher role: one frame, one limit."""
        if not self.over(peer, "unknown", "other"):
            peer.send_text(self.unknown_frame)
            peer.charge()

    # ---- one line --------------------------------------------------------------------------------------------------------------------------------------

    def run(self, peer, line):
        """A line that starts with a single `/` from a socket that has an account (the caller has made it clean and has refused a banned, muted or young account): never a line of the chat."""
        who = peer.identity
        word = lower(line.split(" ", 1)[0][1:])
        command = TABLE.get(word)
        if command is None or RANK[self.chat.roles.role_of(who.account_id)] < command.rank:
            return self.not_yours(peer)
        bucket = command.bucket
        if bucket == "bots":
            bucket = "control" if lower((line.split(" ") + [""])[1]) in ("status", "stop") else "start"
        if bucket is not None and self.over(peer, word, bucket):
            return
        args = command.parse(self, line)
        if isinstance(args, Reply):
            return self.reply(peer, word, args.code, **args.fields)
        if isinstance(args, Err):
            return self.chat.mistake(peer, args.code)
        command.run(self, peer, args)

    # ---- /ignore -----------------------------------------------------------------------------------------------------------------------------------------

    def named(self, name):
        """{account: the name shown} of the accounts that show this name now (an open socket) or showed it on a line that is in the chat's memory: matched in any letter case, against the name that is shown
        and never against a username."""
        want, found = name.casefold(), {}
        for peer in self.chat.peers:
            who = peer.identity
            if who is not None and not getattr(peer, "closed", False) and who.name.casefold() == want:
                found[who.account_id] = who.name
        for entry in self.chat.recent():
            if entry.account and entry.name.casefold() == want:
                found.setdefault(entry.account, entry.name)
        return found

    def ignore(self, peer, name):
        chat, me = self.chat, peer.identity.account_id
        found = self.named(name)
        if not found:
            return self.reply(peer, "ignore", "nobody")
        if len(found) > 1:
            return self.reply(peer, "ignore", "ambiguous")
        target, shown = next(iter(found.items()))
        if target == me:
            return self.reply(peer, "ignore", "self")
        if chat.roles.role_of(target) != "none" or self.is_owner(target):
            return self.reply(peer, "ignore", "staff")
        try:
            kept = chat.store.ignore_add(chat.ref(me), chat.ref(target), shown, chat.limits.ignore_max)        # (the store holds the number: a person that is on the list already is let in again)
        except StoreError as e:
            return chat.trouble(peer, e, "bad")
        if not kept:
            return self.reply(peer, "ignore", "full")
        mine = self.ignores.setdefault(chat.ref(me), {})
        mine[chat.ref(target)] = shown
        self.reply(peer, "ignore", "ok", n=len(mine))

    def unignore(self, peer, name):
        mine = self.ignores.get(self.chat.ref(peer.identity.account_id), {})
        gone = [ref for ref, shown in mine.items() if shown.casefold() == name.casefold()]
        if not gone:
            return self.reply(peer, "unignore", "not")
        try:
            for ref in gone:
                self.chat.store.ignore_remove(self.chat.ref(peer.identity.account_id), ref)
        except StoreError as e:
            return self.chat.trouble(peer, e, "bad")
        for ref in gone:
            del mine[ref]
        if not mine:
            del self.ignores[self.chat.ref(peer.identity.account_id)]
        self.reply(peer, "unignore", "ok", n=len(mine))

    def hides(self, entry, who):
        """Whether a line is left out for the account of `who` (an Identity, or None for a reader): its author is on the account's list. A line of the hub's own and the line of a person who holds a role, or
        held one when they wrote it, is never left out (a person who held none when they were ignored may hold one now), and nothing tells the author."""
        if not self.ignores or who is None or not entry.account or entry.mod:
            return False
        mine = self.ignores.get(self.chat.ref(who.account_id))
        if not mine or self.chat.ref(entry.account) not in mine:
            return False
        return not self.chat.is_mod(entry.account)

    def ignores_person(self, owner, person):
        """Whether the account `owner` has the account `person` on its /ignore list and `person` holds no role (a person who held none when they were ignored may hold one now): an invitation or a mention of `person`
        is then not sent to `owner`, and nothing tells `person`."""
        mine = self.ignores.get(self.chat.ref(owner))
        return bool(mine) and self.chat.ref(person) in mine and not self.chat.is_mod(person)

    def forget(self, account):
        """The account forgot itself: its own list is gone, and so is its place on the lists of the others (what Forget me deletes: nothing of it is kept)."""
        ref = self.chat.ref(account)
        self.ignores.pop(ref, None)
        for owner, mine in list(self.ignores.items()):
            mine.pop(ref, None)
            if not mine:
                del self.ignores[owner]

    # ---- /bots -------------------------------------------------------------------------------------------------------------------------------------------

    def start_bots(self, peer, args):
        """The sync part of `/bots`: the game door must be on (`off`), the account must have no call on its way and the hub fewer than `door_inflight` (`rate`), and a role that is not fresh must be asked
        about from the share of the commands that wait (`rate` when it is spent), as for a moderator's command; then the rest waits for the game server in a task of its own and the chat goes on."""
        chat = self.chat
        key = chat.key_of(peer.identity.account_id)
        if self.door is None or not self.door.on:
            return self.reply(peer, "bots", "off")
        if key in self.flights or len(self.flights) >= chat.limits.door_inflight:
            return self.reply(peer, "bots", "rate", wait=1)
        if not chat.roles.fresh([peer.identity.account_id]) and not chat.wait_limits.take(key, chat.clock()):
            return self.reply(peer, "bots", "rate", wait=chat.wait_limits.wait_seconds(key, chat.clock()))
        task = asyncio.ensure_future(self.bots_task(peer, args))
        self.flights[key] = task
        task.add_done_callback(lambda done, key=key: self.landed(key, done))

    def landed(self, key, task):
        self.flights.pop(key, None)
        if not task.cancelled() and task.exception() is not None and self.chat.on_command_error:
            self.chat.on_command_error(task.exception())          # (the class of the error and nothing else is logged)

    async def confirmed(self, account):
        """Whether the account holds the administrator's role by an answer of Discord that is a minute old at the most (the owner account needs none): as before any action of the chat, a role that is
        only believed is not enough, and a lookup that does not answer is a no."""
        roles = self.chat.roles
        if not roles.fresh([account]) and not await roles.confirm([account]):
            return False
        return RANK[roles.role_of(account)] >= NEEDS["bots"]

    async def bots_task(self, peer, args):
        try:
            await self.bots(peer, args)
        except gamedoor.DoorError:
            self.reply(peer, "bots", "door")
        except asyncio.CancelledError:
            raise
        except Exception as e:
            if self.chat.on_command_error:
                self.chat.on_command_error(e)
            self.reply(peer, "bots", "door")

    async def bots(self, peer, args):
        chat, who = self.chat, peer.identity
        if not await self.confirmed(who.account_id):
            return self.reply(peer, "bots", "role")
        if args.kind == "status":
            return self.say_status(peer, await self.door.summary(who))
        map_file = None
        if args.map_name is not None:
            kind, found = gamedoor.find_map(await self.door.catalog(who), args.map_name)
            if kind == "bad_map":
                return self.reply(peer, "bots", "bad_map", close=found)
            if kind == "no_map":
                return self.reply(peer, "bots", "no_map")
            map_file = found                                        # (the game server's own name for the map: what was typed goes no further)
        if getattr(peer, "closed", None):
            return                                                  # (the socket went while it waited: nothing is done)
        try:
            banned = chat.store.is_banned(who.account_id)
            if banned or chat.store.muted_until(who.account_id):
                return chat.mistake(peer, "banned" if banned else "muted")
        except StoreError as e:
            return chat.trouble(peer, e, "bad")
        if args.kind == "stop":
            return self.say_stopped(peer, await self.door.stop_all(who))
        answer = await self.door.start(who, request_of(args, map_file))
        if answer[0] == 200 and "at_once" not in answer[1]:
            try:
                answer[1]["at_once"] = (await self.door.summary(who))[1].get("at_once")           # (the number that runs at once: a start that is done is told whether or not this answers)
            except gamedoor.DoorError:
                pass
        self.say_started(peer, args, answer)

    def note(self, peer, action, detail):
        """The moderators' log gets one row for a start or a stop that the game server did: the account's reference and numbers (never a name, a map or a path)."""
        try:
            self.chat.store.log_action((self.chat.ref(peer.identity.account_id), action, "", detail))
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)                         # (it is done: the answer is not held back for a row of the log)

    def say_started(self, peer, args, answer):
        status, data = answer
        if status != 200:
            return self.refused(peer, status, data)
        asked, started, queued = (small(data.get(k), 10 ** 6) for k in ("asked", "started", "queued"))
        if None in (asked, started, queued):
            raise gamedoor.DoorError("bad")
        fields = {"asked": asked, "started": started, "queued": queued}
        at_once = small(data.get("at_once"), 10 ** 6)
        if at_once is not None:
            fields["at_once"] = at_once
        if small(data.get("every_s"), 10 ** 6) is not None:
            fields["every_s"] = data["every_s"]
        if data.get("held") in HELD_WORDS:
            fields["held"] = data["held"]
        self.note(peer, "bots_start", "%d every %ds" % (args.count, args.every_s))
        self.reply(peer, "bots", "started", **fields)

    def refused(self, peer, status, data):
        """The answer of the game server to a start that it did not take: only its own few words are told on, and nothing else of what it said."""
        code = data.get("error")
        words = {(400, "bad_count"): "bad_count", (400, "bad_spacing"): "bad_every", (409, "no_map"): "no_map", (409, "map_187"): "map_187", (503, "no_replays"): "no_replays", (409, "queue_full"): "queue_full"}
        if (status, code) not in words:
            raise gamedoor.DoorError("bad")
        fields = {}
        if code == "queue_full":
            fields = {k: data[k] for k in ("waiting", "asked", "max") if small(data.get(k), 10 ** 6) is not None}
        self.reply(peer, "bots", words[(status, code)], **fields)

    def say_stopped(self, peer, answer):
        status, data = answer
        stopped, dropped = small(data.get("stopped"), 10 ** 6 - 1), small(data.get("dropped"), 10 ** 6 - 1)
        if status != 200 or stopped is None or dropped is None:
            raise gamedoor.DoorError("bad")
        self.note(peer, "bots_stop", "stopped %d dropped %d" % (stopped, dropped))
        self.reply(peer, "bots", "stopped", stopped=stopped, dropped=dropped)

    def say_status(self, peer, answer):
        status, data = answer
        queues = data.get("queues")
        counts = [small(data.get(k), 10 ** 6) for k in ("at_once", "running", "waiting", "busy")]
        if status != 200 or None in counts or not isinstance(queues, list):
            raise gamedoor.DoorError("bad")
        rows = []
        for q in queues[:QUEUES_SHOWN]:
            if not isinstance(q, dict):
                raise gamedoor.DoorError("bad")
            row = {k: small(q.get(k), 10 ** 6) or 0 for k in ("total", "remaining", "finished", "running", "kept", "every_s")}
            next_in = q.get("next_in")
            row["next_in"] = next_in if small(next_in, 10 ** 7) is not None else -1
            row["held"] = q.get("held") if q.get("held") in HELD_WORDS else ""
            row["by"] = gamedoor.plain_text(q.get("by"))
            rows.append(row)
        self.reply(peer, "bots", "status", at_once=counts[0], running=counts[1], waiting=counts[2], busy=counts[3], queues=rows)

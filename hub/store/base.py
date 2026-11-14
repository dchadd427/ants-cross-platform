"""The contract of the hub's data (docs/HUB.md, "Data"): what every backend does, and the two errors a caller may have to meet.

The hub, the login and the command line talk to a store only through the operations below. A backend that cannot do one of them (it is refused, locked, full, or out of reach)
raises StoreError, and callers catch that and nothing of the backend's own. Operations that only one backend has (for SQLite: checkpoint, unsettled, secure_delete) are not
here: they stay on that backend's class. tests/scripts/test_hub_store_contract.py runs the same checks against every backend.
"""
import collections
import re

Line = collections.namedtuple("Line", "id at account name text mod")
Line.__repr__ = lambda self: "<Line %d>" % self.id                 # (the default shows the account's Discord id: that is like a password and is in no representation)
Action = collections.namedtuple("Action", "id at actor action target detail")         # a row of the moderators' log (see StoreBase.actions)
Usage = collections.namedtuple("Usage", "bytes free lines oldest log")         # (see StoreBase.usage)
Profile = collections.namedtuple("Profile", "username display pick")
PICKS = ("display", "username")
MAX_LINES = 200                                # the store keeps the newest this many chat lines (every line counts, a moderator's and a deleted one too), and no line older than `retention`: whichever limit is smaller deletes first
MODLOG_DAYS = 30                        # the moderators' log keeps an action this long ...
MODLOG_ROWS = 10000                     # ... and at most this many rows (the newest)
ACTIONS = ("mute", "unmute", "del", "kick", "ban", "unban", "nopic", "pic")     # what the log can say was done
TERMINAL = "terminal"                   # the actor of an action that the command line did
REFERENCE = re.compile(r"[0-9a-f]{16}\Z")      # the pseudonymous reference of an account (hub/pseudo.py)
PICTURE_NAME = re.compile(r"[0-9a-f]{32}\Z")        # the public name of a picture: 128 random bits (hub/pictures.py)
PICTURE_DAYS = 400                      # a person's `off` is remembered this long after their last login, then the row goes (the default is on again)
SHOW_ON, SHOW_OFF, SHOW_BLOCKED = 1, 0, 2       # the state of an account's picture: shown (the default), switched off by the person, blocked by the operator
PROFILE_DAYS = 35                       # a profile that has not been refreshed by a login for this long belongs to nobody who is logged in (a login lasts 30 days): purge() deletes it


class StoreError(Exception):
    """The data could not be read or written now (locked, full, refused). Nothing was done; the caller may tell the sender and try again later."""


class StoreUnavailable(StoreError):
    """The data is out of reach (a server that does not answer): a kind of StoreError, so a caller that does not care about the difference catches StoreError."""


def check_note(note):
    """The note that goes into the moderators' log with an action: (actor, action, target, detail) where the actor is a reference or `terminal`, the action one of ACTIONS, the target a reference
    (or '' for an action that has none) and the detail a few plain characters (minutes, a line's number). Anything else is a bug of the caller and is refused: an account id never goes in."""
    actor, action, target, detail = note
    if not ((isinstance(actor, str) and REFERENCE.match(actor)) or actor == TERMINAL) or action not in ACTIONS or not (target == "" or (isinstance(target, str) and REFERENCE.match(target))) \
            or not isinstance(detail, str) or not re.fullmatch(r"[0-9a-z]{0,12}", detail):
        raise ValueError("not a note of the moderators' log")
    return note


class StoreBase:
    """The operations common to every backend. `clock` gives seconds since 1970 and `retention` is how long a line is kept, in seconds."""

    clock = None
    retention = 0
    max_lines = MAX_LINES                       # at most this many lines are kept, the newest ones (an attribute, so that a test may lower it on one store)
    modlog_days = MODLOG_DAYS                   # the moderators' log: how long an action is kept, and how many rows (attributes too)
    max_modlog = MODLOG_ROWS

    def close(self):
        raise NotImplementedError

    # ---- lines ----

    def add_line(self, account, name, text, mod):
        """Keep a line and return it as a Line. `mod` is the role that its author had when it wrote it: 0 (none), 1 (a moderator) or 2 (an administrator); a bool is 0 or 1. It is a mark for the pages, never a reason to allow anything. The lines past the newest `max_lines` are deleted in the same step, so the store never holds more."""
        raise NotImplementedError

    def recent_lines(self, count):
        """The last `count` lines that are not deleted, not past their time and among the newest `max_lines` kept, oldest first."""
        raise NotImplementedError

    def lines(self, count):
        """The newest `count` lines with their state, newest first: (id, at, account, name, text, deleted)."""
        raise NotImplementedError

    def last_name(self, account):
        """The name the account last spoke under (the one that was shown then), or ''."""
        raise NotImplementedError

    def account_of_line(self, line_id):
        """The account that wrote the line (a line that is not deleted), or None."""
        raise NotImplementedError

    def delete_line(self, line_id, by, note=None):
        """Mark a line deleted and give the deletion the next number of the counter; False when there is no such line or it was deleted already. `note` (see check_note) goes into the moderators' log in the same step, and only when a line was deleted."""
        raise NotImplementedError

    def deletions_since(self, seq):
        """[(number, line id)] of the deletions after `seq`, in order."""
        raise NotImplementedError

    def last_deletion(self):
        """The number of the last deletion. It only goes up: a purge of old lines never lowers it."""
        raise NotImplementedError

    def purge(self, chunk=2000):
        """Delete up to `chunk` lines past their time or past the newest `max_lines` (every line counts, a moderator's and a deleted one too), the mutes that are over, the logins that ran out, the profiles that no login has refreshed for PROFILE_DAYS, the pictures whose login has run out (and a person's `off` that no login has touched for PICTURE_DAYS), and the rows of the moderators' log that are older than `modlog_days` or past the newest `max_modlog`; returns how many lines went."""
        raise NotImplementedError

    def size(self):
        """Bytes in use by what is kept (the hub refuses new lines when this is past its limit)."""
        raise NotImplementedError

    def make_room(self, target, chunk=2000, keep=1000):
        """Delete up to `chunk` of the oldest lines to bring the bytes in use down to `target` (the log file of a backend that has one is cut afterwards, so that size() is no more than that). Never the
        newest `keep` lines (a page shows 100 at most), and the moderators' lines and the lines that a moderator deleted only when no other line may go. Mutes, bans, the counter of deletions and the logins are not touched. Returns how many
        lines went: 0 when the store is at or under `target` already, or when no line may go (also when the log file cannot be cut because a reader holds it, and it is long already)."""
        raise NotImplementedError

    def usage(self):
        """Usage(bytes, free, lines, oldest, log) for the command line: the bytes in use by what is kept, the bytes that deleted lines left free to be reused, the lines kept, the time of the oldest
        one (0 for none), and the bytes of the log file that a backend may have (0 for none). size() is bytes + log."""
        raise NotImplementedError

    def forget(self, account, since=0):
        """Remove the account's logins, profile and lines and wipe its name from its mute and ban (which stay). Returns the ids of the lines that a screen may still show."""
        raise NotImplementedError

    # ---- pictures: the small copy of an account's Discord picture (hub/pictures.py), keyed by the account's pseudonymous reference, never by its id ----

    def set_picture(self, account, png, name, until, rows_max, bytes_max):
        """Keep `png` (the bytes the hub rewrote) under the public `name` (32 random hex digits) for the account, until `until` (seconds since 1970); the account's older copy and name are gone in the same step. False,
        and nothing kept, when the account's picture is switched off or blocked, the account has no login or is banned (a fetch that ended too late), or the picture is bigger than `bytes_max`; `until` is cut to the end of the account's last login. At most `rows_max` pictures and `bytes_max` bytes are kept in all: the ones
        fetched longest ago lose their bytes and their name first."""
        raise NotImplementedError

    def clear_picture(self, account):
        """Delete the account's picture and name; its switch, if it was ever changed, stays. True when there was a picture."""
        raise NotImplementedError

    def set_picture_show(self, account, show, operator=False, note=None):
        """Set the account's picture to SHOW_ON, SHOW_OFF or SHOW_BLOCKED; a picture that is not shown is deleted in the same step. A block is lifted by the operator only (`operator=True`): the person's own change of a
        block does nothing. Returns the state the account is in after it. `note` (see check_note) goes into the moderators' log in the same step, and only when the state changed."""
        raise NotImplementedError

    def picture_state(self, account, touch=False):
        """SHOW_ON when the account has no word said about its picture, else the state it was put in. `touch` marks a login of an account that is switched off (its `off` is remembered `PICTURE_DAYS` after its last login)."""
        raise NotImplementedError

    def has_picture(self, account):
        """Whether a picture is kept for the account now."""
        raise NotImplementedError

    def picture(self, name):
        """The bytes kept under the public `name`, or None for a name that is not 32 lower-case hex digits, is not known, or whose account is not shown (switched off or blocked) or whose login has run out."""
        raise NotImplementedError

    def picture_look(self):
        """(the counter of the last change, picture_names(), picture_marks(), picture_end()) read in one read transaction: what a look at the pictures sees is one moment of the data, whatever another process does
        meanwhile."""
        raise NotImplementedError

    def picture_state_by_reference(self, reference):
        """The state (SHOW_OFF, SHOW_ON or SHOW_BLOCKED) of the row of this reference (hub/pseudo.py) in the pictures' table, or None when there is no such row: the one place that the operator's `nopic` and
        `pic` can find an account by its reference after the account's other rows are gone (Forget me leaves a block)."""
        raise NotImplementedError

    def set_picture_show_by_reference(self, reference, show, operator=False, note=None):
        """set_picture_show() for the account that has this reference, when the pictures' table has a row for it; None when it has not."""
        raise NotImplementedError

    def picture_marks(self):
        """{reference: state} of the accounts whose picture is switched off (SHOW_OFF) or blocked (SHOW_BLOCKED): the ones that are not in the default state."""
        raise NotImplementedError

    def picture_end(self):
        """The soonest end of a picture that may be shown now (seconds since 1970), or None when there is none: when the list of names is to be read again for time alone."""
        raise NotImplementedError

    def picture_names(self):
        """{reference: name} of the pictures that may be shown now."""
        raise NotImplementedError

    def last_picture_change(self):
        """The number of the last change of the pictures, by this process or by another that shares the data (the command line): it only goes up."""
        raise NotImplementedError

    def picture_usage(self):
        """(pictures kept, their bytes)."""
        raise NotImplementedError

    def logins(self, account):
        """How many logins the account has now."""
        raise NotImplementedError

    # ---- mutes and bans ----

    def mute(self, account, minutes, by, name="", note=None):
        """Mute for `minutes` from now (0 lifts the mute). Returns the time it ends, or 0. `note` (see check_note) goes into the moderators' log in the same step (a lifted mute: only if there was one)."""
        raise NotImplementedError

    def muted_until(self, account):
        """When the account's mute ends, or 0 when it is not muted now."""
        raise NotImplementedError

    def mutes(self):
        """The mutes that are on: (account, name, until, by), the soonest to end first."""
        raise NotImplementedError

    def ban(self, account, by, name="", note=None):
        """Ban the account (a ban that is there is made again, with a new time). `note` goes into the moderators' log in the same step."""
        raise NotImplementedError

    def unban(self, account, note=None):
        """True when the account was banned (and `note` went into the moderators' log, in the same step; none when it was not)."""
        raise NotImplementedError

    def is_banned(self, account):
        raise NotImplementedError

    def bans(self):
        """The bans: (account, name, at, by), oldest first."""
        raise NotImplementedError

    def last_restriction(self):
        """The number of the last change of the bans or the mutes: a ban or a mute made, lifted or made again, by this process or by another that shares the data (the command line). It only goes up, so a
        reader that keeps the two lists reads them again when it is another number. A mute that runs out changes nothing here: its end is in `mutes()`."""
        raise NotImplementedError

    # ---- logins ----

    def add_session(self, key, account, name, seconds, keep, total):
        """Keep a login by the SHA-256 (hex) of its cookie, for `seconds`; an account keeps at most `keep`, all accounts `total`: the oldest go."""
        raise NotImplementedError

    def session(self, key):
        """(account, name, expires) of the login, or None when there is none or it ran out."""
        raise NotImplementedError

    def delete_session(self, key):
        """End one login; True when there was one."""
        raise NotImplementedError

    def end_sessions(self, account, note=None):
        """End every login of the account (a kick) and return how many there were; the log file is cut by the hub's clock at its next look. `note` goes into the moderators' log in the same step."""
        raise NotImplementedError

    # ---- profiles: who an account is, as its last login said ----

    def set_profile(self, account, username, display, total):
        """Keep what a login of the account just said: its `username` (the key of the account for a moderator) and its `display` name ('' when it has none that may be shown). The account's
        pick stays as it was; a new profile picks the display name when there is one, else the username, and so does one that had no display name and now has one. At most `total` profiles are
        kept: the ones that were refreshed longest ago go."""
        raise NotImplementedError

    def profile(self, account):
        """The Profile (username, display, pick) of the account, or None when no login has made one (an account that logged in before profiles were kept)."""
        raise NotImplementedError

    def set_pick(self, account, pick):
        """Choose which name the account is shown by, one of PICKS. False (nothing changed) when the account has no profile or no display name: there is nothing to choose between."""
        raise NotImplementedError

    def usernames_by_reference(self, references):
        """{reference: username} for the references (hub/pseudo.py) of `references` whose account has a profile, for the command line that reads the moderators' log."""
        raise NotImplementedError

    def account_of_reference(self, reference):
        """The account (id) whose pseudonymous reference this is among the accounts that the data knows (lines, profiles, bans, mutes, logins), when exactly one is; else None. For the command line, which
        prints references and takes them back."""
        raise NotImplementedError

    def account_of_username(self, username):
        """The account whose profile has this username, when exactly one has (usernames are unique at Discord, but a profile is as old as its last login, so a name that was given up and taken by
        another account can be in two); None for none or for several: the caller never guesses which account is meant."""
        raise NotImplementedError


    # ---- the hub's secret and the moderators' log ----

    def secret(self):
        """The 32 random bytes that the pseudonymous references of this hub are keyed with (hub/pseudo.py): made once, when the data is first opened, and the same for every process that opens it."""
        raise NotImplementedError

    def actions(self, since=0, limit=1000):
        """The rows of the moderators' log, newest first, from time `since` (seconds since 1970) on: Action(id, at, actor, action, target, detail). Actor and target are references, never an id."""
        raise NotImplementedError


PUBLIC = tuple(sorted(n for n in vars(StoreBase) if not n.startswith("_") and callable(getattr(StoreBase, n))))

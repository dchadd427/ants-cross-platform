"""The contract of the hub's data (docs/HUB.md, "Data"): what every backend does, and the two errors a caller may have to meet.

The hub, the login and the command line talk to a store only through the operations below. A backend that cannot do one of them (it is refused, locked, full, or out of reach)
raises StoreError, and callers catch that and nothing of the backend's own. Operations that only one backend has (for SQLite: checkpoint, unsettled, secure_delete) are not
here: they stay on that backend's class. tests/scripts/test_hub_store_contract.py runs the same checks against every backend.
"""
import collections

Line = collections.namedtuple("Line", "id at account name text mod")
Usage = collections.namedtuple("Usage", "bytes free lines oldest log")         # (see StoreBase.usage)
Profile = collections.namedtuple("Profile", "username display pick")
PICKS = ("display", "username")
MAX_LINES = 200                                # the store keeps the newest this many chat lines (every line counts, a moderator's and a deleted one too), and no line older than `retention`: whichever limit is smaller deletes first
PROFILE_DAYS = 35                       # a profile that has not been refreshed by a login for this long belongs to nobody who is logged in (a login lasts 30 days): purge() deletes it


class StoreError(Exception):
    """The data could not be read or written now (locked, full, refused). Nothing was done; the caller may tell the sender and try again later."""


class StoreUnavailable(StoreError):
    """The data is out of reach (a server that does not answer): a kind of StoreError, so a caller that does not care about the difference catches StoreError."""


class StoreBase:
    """The operations common to every backend. `clock` gives seconds since 1970 and `retention` is how long a line is kept, in seconds."""

    clock = None
    retention = 0
    max_lines = MAX_LINES                       # at most this many lines are kept, the newest ones (an attribute, so that a test may lower it on one store)

    def close(self):
        raise NotImplementedError

    # ---- lines ----

    def add_line(self, account, name, text, mod):
        """Keep a line and return it as a Line. The lines past the newest `max_lines` are deleted in the same step, so the store never holds more."""
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

    def delete_line(self, line_id, by):
        """Mark a line deleted and give the deletion the next number of the counter; False when there is no such line or it was deleted already."""
        raise NotImplementedError

    def deletions_since(self, seq):
        """[(number, line id)] of the deletions after `seq`, in order."""
        raise NotImplementedError

    def last_deletion(self):
        """The number of the last deletion. It only goes up: a purge of old lines never lowers it."""
        raise NotImplementedError

    def purge(self, chunk=2000):
        """Delete up to `chunk` lines past their time or past the newest `max_lines` (every line counts, a moderator's and a deleted one too), the mutes that are over, the logins that ran out and the profiles that no login has refreshed for PROFILE_DAYS; returns how many lines went."""
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

    # ---- mutes and bans ----

    def mute(self, account, minutes, by, name=""):
        """Mute for `minutes` from now (0 lifts the mute). Returns the time it ends, or 0."""
        raise NotImplementedError

    def muted_until(self, account):
        """When the account's mute ends, or 0 when it is not muted now."""
        raise NotImplementedError

    def mutes(self):
        """The mutes that are on: (account, name, until, by), the soonest to end first."""
        raise NotImplementedError

    def ban(self, account, by, name=""):
        raise NotImplementedError

    def unban(self, account):
        """True when the account was banned."""
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

    def account_of_username(self, username):
        """The account whose profile has this username, when exactly one has (usernames are unique at Discord, but a profile is as old as its last login, so a name that was given up and taken by
        another account can be in two); None for none or for several: the caller never guesses which account is meant."""
        raise NotImplementedError


PUBLIC = tuple(sorted(n for n in vars(StoreBase) if not n.startswith("_") and callable(getattr(StoreBase, n))))

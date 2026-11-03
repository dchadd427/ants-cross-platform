"""The contract of the hub's data (docs/HUB.md, "Data"): what every backend does, and the two errors a caller may have to meet.

The hub, the login and the command line talk to a store only through the operations below. A backend that cannot do one of them (it is refused, locked, full, or out of reach)
raises StoreError, and callers catch that and nothing of the backend's own. Operations that only one backend has (for SQLite: checkpoint, unsettled, secure_delete) are not
here: they stay on that backend's class. tests/scripts/test_hub_store_contract.py runs the same checks against every backend.
"""
import collections

Line = collections.namedtuple("Line", "id at account name text mod")


class StoreError(Exception):
    """The data could not be read or written now (locked, full, refused). Nothing was done; the caller may tell the sender and try again later."""


class StoreUnavailable(StoreError):
    """The data is out of reach (a server that does not answer): a kind of StoreError, so a caller that does not care about the difference catches StoreError."""


class StoreBase:
    """The operations common to every backend. `clock` gives seconds since 1970 and `retention` is how long a line is kept, in seconds."""

    clock = None
    retention = 0

    def close(self):
        raise NotImplementedError

    # ---- lines ----

    def add_line(self, account, name, text, mod):
        """Keep a line and return it as a Line."""
        raise NotImplementedError

    def recent_lines(self, count):
        """The last `count` lines that are not deleted and not past their time, oldest first."""
        raise NotImplementedError

    def lines(self, count):
        """The newest `count` lines with their state, newest first: (id, at, account, name, text, deleted)."""
        raise NotImplementedError

    def account_of_name(self, name):
        """The account that last spoke under this name, or None."""
        raise NotImplementedError

    def last_name(self, account):
        """The name the account last spoke under, or ''."""
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
        """Delete up to `chunk` lines past their time, the mutes that are over and the logins that ran out; returns how many lines went."""
        raise NotImplementedError

    def size(self):
        """Bytes in use by what is kept (the hub refuses new lines when this is past its limit)."""
        raise NotImplementedError

    def forget(self, account, since=0):
        """Remove the account's logins and lines and wipe its name from its mute and ban (which stay). Returns the ids of the lines that a screen may still show."""
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


PUBLIC = tuple(sorted(n for n in vars(StoreBase) if not n.startswith("_") and callable(getattr(StoreBase, n))))

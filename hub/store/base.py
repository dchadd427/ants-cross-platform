"""The contract of the hub's data (docs/HUB.md, "Data"): what every backend does, and the two errors a caller may have to meet.

The hub, the login and the command line talk to a store only through the operations below. A backend that cannot do one of them (it is refused, locked, full, or out of reach)
raises StoreError, and callers catch that and nothing of the backend's own. Operations that only one backend has (for SQLite: checkpoint, unsettled, secure_delete) are not
here: they stay on that backend's class. tests/scripts/test_hub_store_contract.py runs the same checks against every backend.
"""
import collections
import re

Line = collections.namedtuple("Line", "id at account name text mod img", defaults=(None,))        # img: None, or the Image of the line (docs/HUB_IMAGES.md)
Line.__repr__ = lambda self: "<Line %d>" % self.id                 # (the default shows the account's Discord id: that is like a password and is in no representation)
Image = collections.namedtuple("Image", "name x w h tw th anim")     # a posted image: its public name (32 random hex digits), its type (`jpg`, `png` or `gif`), the size of the image and of its thumbnail, and whether it is animated
Image.__repr__ = lambda self: "<Image>"
Action = collections.namedtuple("Action", "id at actor action target detail")         # a row of the moderators' log (see StoreBase.actions)
Usage = collections.namedtuple("Usage", "bytes free lines oldest log")         # (see StoreBase.usage)
Profile = collections.namedtuple("Profile", "username display pick")
Alert = collections.namedtuple("Alert", "id at kind body line unread")         # a row of an account's list of alerts (docs/HUB_PEOPLE.md): `body` is the small JSON text that hub/people.py made, `line` the chat line that a mention is of (or None)
ALERT_KINDS = ("invite", "mention", "join", "over", "room", "msg")
ALERT_BODY_MAX = 600                        # the longest body that is kept (bytes): hub/people.py writes a few short fields, never more
Message = collections.namedtuple("Message", "id at frm to text frm_name to_name")      # a private message (docs/HUB_MESSAGES.md): who sent it and to whom are references, never ids; the names are the ones that were shown when it was sent
Message.__repr__ = lambda self: "<Message %d>" % self.id           # (the default shows the text and the references)
Report = collections.namedtuple("Report", "id at reporter reported reporter_name reported_name why closed messages")      # a report of a message (docs/HUB_MESSAGES.md): `messages` is the copy, [(at, `r` reporter or `d` the reported person, text)] oldest first
Report.__repr__ = lambda self: "<Report %d>" % self.id
REPORT_REASONS = ("abuse", "spam", "other")
AiKey = collections.namedtuple("AiKey", "id ref name scope digest made ends used calls revoked")       # a key of a person's AI (docs/MAP_AI.md): `ref` is the account's reference (hub/pseudo.py), `digest` the SHA-256 of the secret; `revoked` is the time it was revoked, 0 for a key that was not
AiKey.__repr__ = lambda self: "<AiKey %s>" % self.id                  # (the default shows the digest of the secret and the account's reference)
AI_KEY_ID = re.compile(r"[0-9a-f]{8}\Z")                              # the public part of a key: shown in lists and logs
AI_DIGEST = re.compile(r"[0-9a-f]{64}\Z")
AI_SCOPES = ("read", "edit")                                         # what a key may do: look, or look and change
PICKS = ("display", "username")
MAX_LINES = 200                                # the store keeps the newest this many chat lines (every line counts, a moderator's and a deleted one too), and no line older than `retention`: whichever limit is smaller deletes first
MODLOG_DAYS = 30                        # the moderators' log keeps an action this long ...
MODLOG_ROWS = 10000                     # ... and at most this many rows (the newest)
ACTIONS = ("mute", "unmute", "del", "kick", "ban", "unban", "nopic", "pic", "noimages", "images", "del_img", "bots_start", "bots_stop")     # what the log can say was done
BOTS_DETAILS = {"bots_start": re.compile(r"[0-9]{1,6} every [0-9]{1,5}s\Z"), "bots_stop": re.compile(r"stopped [0-9]{1,6} dropped [0-9]{1,6}\Z")}      # the detail of a `/bots` row: numbers and fixed words, each number short of an id's length
TERMINAL = "terminal"                   # the actor of an action that the command line did
REFERENCE = re.compile(r"[0-9a-f]{16}\Z")      # the pseudonymous reference of an account (hub/pseudo.py)
PICTURE_NAME = re.compile(r"[0-9a-f]{32}\Z")        # the public name of a picture: 128 random bits (hub/pictures.py)
IMAGE_NAME = PICTURE_NAME                           # ... and of an image (hub/images.py): the same shape, 128 random bits
IMAGE_TYPES = ("jpg", "png", "gif")                 # what a kept image is (the converter writes nothing else)
NOIMAGES_MAX = 10000                                # the accounts whose posting of images is taken away that are kept in all (the oldest go first)
PICTURE_DAYS = 400                      # a person's `off` is remembered this long after their last login, then the row goes (the default is on again)
SHOW_ON, SHOW_OFF, SHOW_BLOCKED = 1, 0, 2       # the state of an account's picture: shown (the default), switched off by the person, blocked by the operator
PROFILE_DAYS = 35                       # a profile that has not been refreshed by a login for this long belongs to nobody who is logged in (a login lasts 30 days): purge() deletes it


class StoreError(Exception):
    """The data could not be read or written now (locked, full, refused). Nothing was done; the caller may tell the sender and try again later."""


class ImageGone(Exception):
    """The image that a line was to carry is not there for this account: unknown, used already, not its own, or waiting too long. Nothing was written."""


class StoreUnavailable(StoreError):
    """The data is out of reach (a server that does not answer): a kind of StoreError, so a caller that does not care about the difference catches StoreError."""


def check_note(note):
    """The note that goes into the moderators' log with an action: (actor, action, target, detail) where the actor is a reference or `terminal`, the action one of ACTIONS, the target a reference
    (or '' for an action that has none) and the detail a few plain characters (minutes, a line's number; for `bots_start` and `bots_stop` the numbers of the batch, in their one fixed form). Anything else is a bug of the caller and is refused: an account id never goes in."""
    actor, action, target, detail = note
    if not ((isinstance(actor, str) and REFERENCE.match(actor)) or actor == TERMINAL) or action not in ACTIONS or not (target == "" or (isinstance(target, str) and REFERENCE.match(target))) \
            or not isinstance(detail, str) or not (BOTS_DETAILS[action].match(detail) if action in BOTS_DETAILS else re.fullmatch(r"[0-9a-z]{0,12}", detail)):
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

    def add_line(self, account, name, text, mod, image=None):
        """(`image`: the public name of an upload that waits for this account: the line carries it, in the same step, or ImageGone is raised and nothing is written.) Keep a line and return it as a Line. `mod` is the role that its author had when it wrote it: 0 (none), 1 (a moderator) or 2 (an administrator); a bool is 0 or 1. It is a mark for the pages, never a reason to allow anything. The lines past the newest `max_lines` are deleted in the same step, so the store never holds more."""
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

    def delete_lines_since(self, account, since, by, note=None):
        """Mark deleted every line of `account` that is not deleted and was written at or after `since` (seconds since 1970), each with the next number of the counter (as delete_line does), in one step; returns how many. `note` (see check_note) goes into the moderators' log in the same step, and only when a line was deleted."""
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

    # ---- images: what people post (hub/images.py), keyed by the account's pseudonymous reference, never by its id ----

    def add_image(self, account, name, image, nbytes):
        """Keep the row of an upload that waits (`image` is an Image, `nbytes` the size of its two files) for the account to post it with a line. At most the account's waiting ones are counted by pending_images."""
        raise NotImplementedError

    def pending_images(self, account):
        """How many uploads of the account wait for a line (not past `image_pending_seconds`)."""
        raise NotImplementedError

    def image_of_line(self, line_id):
        """The Image of a line that is not deleted, or None."""
        raise NotImplementedError

    def images_gone(self, stale_before):
        """[(name, type)] of the rows whose files are to be deleted: an upload that waited since before `stale_before`, and an image whose line is deleted or gone."""
        raise NotImplementedError

    def drop_images(self, names):
        """Delete these rows (their files are gone)."""
        raise NotImplementedError

    def image_usage(self):
        """(rows, bytes) of every image kept, posted or waiting."""
        raise NotImplementedError

    def image_names(self):
        """The set of names that have a row."""
        raise NotImplementedError

    def last_image_at(self):
        """The time (seconds since 1970) of the newest row of an image, posted or waiting, or None when there is none."""
        raise NotImplementedError

    def set_noimages(self, account, on, by, note=None):
        """Take away (`on`) or give back the account's posting of images: a row of its reference that beats its Discord role. True when that changed something (a flag that is there is not made again, a flag that is not there is not lifted); `note` goes into the moderators' log in the same step, and only then."""
        raise NotImplementedError

    def set_noimages_by_reference(self, reference, on, by, note=None):
        """set_noimages() for the row that has this reference (the account may be gone: Forget me leaves the flag); None when there is no such row to lift."""
        raise NotImplementedError

    def noimages_marks(self):
        """{reference: (time, the actor's reference)} of the accounts whose posting is taken away."""
        raise NotImplementedError

    def noimages_state_by_reference(self, reference):
        """Whether the account with this reference has the flag."""
        raise NotImplementedError

    def last_noimages_change(self):
        """The number of the last change of the flags, by this process or by another (the command line): it only goes up."""
        raise NotImplementedError

    # ---- alerts: the list that an account's bell shows (hub/people.py), keyed by the account's pseudonymous reference, never by its id ----

    def add_alert(self, ref, kind, body, from_ref="", line=None, keep=30, days=7, unread=1, replace=False):
        """Keep one alert for the account with this reference (`kind` is one of ALERT_KINDS, `body` a short JSON text, `from_ref` the reference of the person it is from, `line` the number of the chat line of a mention),
        unread unless `unread` is 0, and give back its Alert. The account keeps its newest `keep` alerts and none older than `days` days: the rest go with this call. With `replace`, the account's alerts of this
        kind from the same person go first (one row for each person). The table is made by the first alert."""
        raise NotImplementedError

    def drop_alerts_from(self, ref, from_ref, kind):
        """The account's alerts of this kind from this person go (a person's private messages were read). Returns how many."""
        raise NotImplementedError

    def alerts(self, ref, keep=30, days=7):
        """The account's alerts, newest first: at most `keep`, none older than `days` days."""
        raise NotImplementedError

    def read_alerts(self, ref, upto):
        """The account's alerts up to the number `upto` are read now. Returns how many were unread."""
        raise NotImplementedError

    def clear_alerts(self, ref):
        """Every alert of the account goes. Returns how many there were."""
        raise NotImplementedError

    def change_alert(self, ref, alert_id, body):
        """The body of one of the account's alerts is another text (an invitation that has ended loses its room's code). False when the account has no such alert."""
        raise NotImplementedError

    def drop_alerts_of_lines(self, line_ids):
        """The mentions of these chat lines go from every list. Returns the references of the accounts whose list changed."""
        raise NotImplementedError

    # An alert is kept `alert_days` days at the most (the regular purge cuts the ones past it, so that a quote of a line does not stay for longer than the page says), and the mention of a line that purge or
    # make_room deleted goes with the line; `alerts_dropped` counts the alerts that went on the store's own account, and the hub sends the lists again when it grew.
    alert_days = 7
    alerts_dropped = 0

    # ---- the people that an account ignores (hub/commands.py): by the references of both, never by an id ----

    def ignores(self):
        """{the reference of an account: {the reference of a person that it ignores: the name that was shown when it did}} for every account."""
        raise NotImplementedError

    def ignore_add(self, ref, other, name, most=50):
        """The account `ref` ignores `other`, shown as `name`. False, and nothing kept, when it already ignores `most` other people. The table is made by the first one."""
        raise NotImplementedError

    def ignore_remove(self, ref, other):
        """The account `ref` does not ignore `other` any more. False when it did not."""
        raise NotImplementedError

    # ---- the people that an account follows (hub/follows.py): by the references of both, never by an id; nobody is told, and the list is nobody's but its owner's ----

    def follows(self, ref):
        """[(the reference of a person that the account `ref` follows, the name that was shown when it did)], the one it followed first first."""
        raise NotImplementedError

    def follow_add(self, ref, other, name, most=100):
        """The account `ref` follows `other`, shown as `name` (a person that is followed already keeps its place and is shown by the new name). False, and nothing kept, when it follows `most` other people. The table is made by the first one."""
        raise NotImplementedError

    def follow_remove(self, ref, other):
        """The account `ref` does not follow `other` any more. False when it did not."""
        raise NotImplementedError

    def followers(self, other):
        """The references of the accounts that follow `other`. Only the hub's own code asks, to tell them of a room: no page, no log and no frame ever holds the answer."""
        raise NotImplementedError

    # ---- private messages (hub/messages.py), by the references of both people, never by an id; kept `dm_days` days ----

    dm_days = 7
    report_open_days = 90
    report_closed_days = 30

    def dm_add(self, frm, to, frm_name, to_name, text, hide=False, keep=300):
        """Keep a message from `frm` to `to` (references) with the names that are shown now, and give back its Message; `hide`: the person it is to never sees it (they ignore the sender: the sender's own copy is as any other). The
        conversation keeps its newest `keep` messages: the rest go with this call. The table is made by the first message."""
        raise NotImplementedError

    def dm_thread(self, ref, other, limit=100):
        """The messages between `ref` and `other` that `ref` may see (not deleted by `ref`, and not hidden from it), oldest first: the newest `limit` of the last `dm_days` days."""
        raise NotImplementedError

    def dm_conversations(self, ref, limit=50):
        """[(the reference of the other person, the newest Message that `ref` may see of the conversation, how many messages to `ref` it has not read)], the newest conversation first."""
        raise NotImplementedError

    def dm_unread(self, ref, other):
        """How many messages of `other` to `ref` it has not read (that it may see)."""
        raise NotImplementedError

    def dm_read(self, ref, other):
        """Every message of `other` to `ref` is read now. Returns how many were not."""
        raise NotImplementedError

    def dm_delete(self, ref, other):
        """The conversation is gone for `ref` (the other person keeps theirs, and a message that nobody can see any more goes from the file). Returns how many messages `ref` had of it."""
        raise NotImplementedError

    def dm_get(self, ref, other, message_id):
        """The message with this number when it is from `other` to `ref` and `ref` may see it, else None."""
        raise NotImplementedError

    def dm_partners(self, ref):
        """The references of the people that `ref` has messages with (that it may see)."""
        raise NotImplementedError

    def dm_usage(self):
        """(messages, reports, bytes) that the two tables hold, the bytes as an estimate that is never less than the text and the names."""
        raise NotImplementedError

    def report_add(self, reporter, reported, reporter_name, reported_name, why, message_id, context=5):
        """A report of the message `message_id` (from `reported` to `reporter`, which `reporter` may see): the message and up to `context` messages before it in the conversation are COPIED into the report, as the store holds them (a
        message that was hidden from the reporter is not in the copy). `why` is one of REPORT_REASONS. Returns the report's number; the same person's second report of the same message is the first one's number. None when the message is not there."""
        raise NotImplementedError

    def reports(self, closed=False):
        """The reports that nobody closed (with `closed`, the closed ones as well), newest first."""
        raise NotImplementedError

    def report_close(self, report_id):
        """The report is closed (the store deletes it `report_closed_days` days later). False when there is none that is open."""
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

    def has_login(self, account):
        """Whether the account has a login that has not run out (no login, no picture: hub/pictures.py looks at Discord only for such an account)."""
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

    # ---- the keys of a person's AI (hub/aikeys.py, docs/MAP_AI.md): keyed by the account's pseudonymous reference, never by its id ----

    def ai_key_add(self, account, key_id, name, scope, digest, seconds, live_max, day_max):
        """Keep a key that works for `seconds` from now, for the account (`digest`: the SHA-256 in hex of the secret, which is never kept). The AiKey that was kept, or a word and nothing kept: `live` (the account has
        `live_max` keys that work), `day` (it made `day_max` keys in the last day, revoked ones too) or `id` (that public id is taken: the caller makes another)."""
        raise NotImplementedError

    def ai_keys(self, account):
        """The AiKey rows of the account's keys that work now (not revoked, not ended), oldest first."""
        raise NotImplementedError

    def ai_key(self, key_id):
        """The AiKey with this public id, revoked or ended or not (the caller judges it), or None for an id that is not 8 hex digits or is not known."""
        raise NotImplementedError

    def ai_key_use(self, key_id):
        """Note one call: the time of the last use is now and the count of calls is one more."""
        raise NotImplementedError

    def ai_key_revoke(self, account, key_id):
        """Revoke the account's own key (the key of another account is not touched): True when a key that worked was revoked."""
        raise NotImplementedError

    def ai_key_revoke_id(self, key_id):
        """Revoke a key by its public id, whoever's it is (the operator's command line): True when a key that worked was revoked."""
        raise NotImplementedError

    def ai_keys_revoke_ref(self, reference):
        """Revoke every key that works of the account with this reference (the operator's command line); returns how many."""
        raise NotImplementedError

    def ai_keys_drop(self, account):
        """Delete every key of the account, and the rows of the log of their calls: what a ban and Forget me do. Returns how many keys went."""
        raise NotImplementedError

    def ai_keys_working(self, limit=1000):
        """The AiKey rows of every key that works now, the most recently used first (the operator's command line)."""
        raise NotImplementedError

    def ai_log_add(self, key_id, ref, tool, cells, ok, millis):
        """One row of the log of the calls (never a secret, a map, a name or a text that a person typed): the key's public id, the account's reference, the tool's name, the cells that it changed, whether it worked, and how long it took."""
        raise NotImplementedError

    def ai_log_recent(self, limit=100):
        """The newest rows of the log of the calls, newest first: (at, key id, reference, tool, cells, ok, millis)."""
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

    def log_action(self, note):
        """Write one row of the moderators' log (see check_note) for an action that changes none of the hub's data (a `/bots` start or stop: the game server did it)."""
        raise NotImplementedError

    def actions(self, since=0, limit=1000):
        """The rows of the moderators' log, newest first, from time `since` (seconds since 1970) on: Action(id, at, actor, action, target, detail). Actor and target are references, never an id."""
        raise NotImplementedError


PUBLIC = tuple(sorted(n for n in vars(StoreBase) if not n.startswith("_") and callable(getattr(StoreBase, n))))

"""The images of the chat (docs/HUB_IMAGES.md): who may post one, where the files live, and how they are deleted. The upload's own door is hub/imgdoor.py; the decoding is the converter's (imgconv/), in a
container of its own: this file never reads an image.

WHO. `state_of(account)` is the one place that says whether an account may post an image now, and what it is to be told:
    `on`    it may (the operator's account; or Discord says so: the images' role, a moderator or an administrator, on an answer that is within its belief, hub/roles.py `images_of`)
    `off`   an administrator took it away (the flag: a row of the account's reference in `noimages`, which beats the Discord role, but never the operator's account)
    `role`  Discord's answer is known and says no
    `wait`  nothing can be said now: Discord has not answered, or the answer is old, or the flags could not be read (fail closed: not allowed, and the person is told that it is not known)
    None    the hub has no converter or no folder: no image at all, and the pages are told nothing
The flags are read into memory when the store's counter says that they changed (here, or by the command line in another process): `sync` returns the references that changed, for the pages of those accounts.

FILES. `<name>.<x>` (`x` is `jpg`, `png` or `gif`) and `<name>.t.jpg` in one folder, `name` 32 random lower-case hex digits that are made from nothing. A file is written under a temporary name, made durable
and renamed; the row (hub/store `images`) follows. The folder is a volume that the site's web server reads and serves (docker/nginx.conf), so a file that is deleted here is gone from the site at once.
DELETING. A row whose line is deleted or gone, and an upload that no line took within `image_pending_seconds`, is a row whose files are to go: `sweep` deletes the files (a file that is not there is fine) and
then the row; a file that could not be deleted keeps its row, so the next sweep tries again. It runs right after every deletion of a line, after Forget me, and by the hub's clock; `scan` (hourly) also
deletes any file that has no row and is older than `image_scan_age_seconds`.
"""
import os
import re
import secrets
import time

from .limits import UploadLimits
from .store.base import IMAGE_TYPES

ON, OFF, ROLE, WAIT = "on", "off", "role", "wait"
FILE = re.compile(r"([0-9a-f]{32})(\.t)?\.(jpg|png|gif)(\.tmp)?\Z")


class Images:
    def __init__(self, cfg, limits, store, roles, clock, wall, log, ref, is_owner, key_of):
        """`clock` is the monotonic clock, `wall` seconds since 1970, `ref(account)` the account's reference and `key_of(account)` the hub's key of it for this run."""
        self.cfg = cfg
        self.limits = limits
        self.store = store
        self.roles = roles
        self.clock = clock
        self.wall = wall
        self.log = log
        self.ref = ref
        self.is_owner = is_owner
        self.key_of = key_of
        self.dir = cfg.image_dir
        self.on = False
        if cfg.image_converter_url:
            try:
                os.makedirs(self.dir, exist_ok=True)
                self.on = os.access(self.dir, os.W_OK)
            except OSError:
                self.on = False
            if not self.on:
                log.event("warning", what="images_off_the_folder_cannot_be_written")
        self.blocked = set()                        # the references whose posting an administrator took away
        self.loaded = False                         # whether `blocked` was read (until it is, nobody but the operator's account may post: fail closed)
        self.seen = None                            # the store's counter of the flags when `blocked` was read
        self.next_sweep = float("-inf")
        self.next_scan = float("-inf")
        self.uploads = UploadLimits(limits)         # what an account may upload (hub/imgdoor.py asks)

    # ---- who -----------------------------------------------------------------------------------------------------------------------------

    def state_of(self, account):
        """`on`, `off`, `role` or `wait` (see the top of this file), or None when the hub has no images."""
        if not self.on:
            return None
        if self.is_owner(account):
            return ON
        if not self.loaded:
            return WAIT
        if self.ref(account) in self.blocked:
            return OFF
        if self.roles.images_of(account):
            return ON
        return ROLE if self.roles.knows(account) else WAIT

    def may_post(self, account):
        return self.state_of(account) == ON

    def sync(self):
        """Read the flags again when the store's counter says that they changed; the set of the references that changed (empty when nothing did). A store that refuses leaves `loaded` as it was
        and says so by StoreError: the caller logs it."""
        seq = self.store.last_noimages_change()
        if self.loaded and seq == self.seen:
            return set()
        marks = set(self.store.noimages_marks())
        changed = marks ^ self.blocked if self.loaded else set(marks)
        self.blocked, self.seen, self.loaded = marks, seq, True
        return changed

    # ---- files -----------------------------------------------------------------------------------------------------------------------------

    @staticmethod
    def new_name():
        return secrets.token_hex(16)

    def path(self, name, x, thumb=False, tmp=False):
        assert x in IMAGE_TYPES and re.fullmatch(r"[0-9a-f]{32}", name)
        return os.path.join(self.dir, "%s%s.%s%s" % (name, ".t" if thumb else "", "jpg" if thumb else x, ".tmp" if tmp else ""))

    def write(self, name, x, full, thumb):
        """Write the two files, the thumbnail first (so that an image whose own file is there has its thumbnail), each under a temporary name, made durable and renamed. A failure leaves no file."""
        made = []
        try:
            for data, final in ((thumb, self.path(name, x, thumb=True)), (full, self.path(name, x))):
                temporary = final + ".tmp"
                made.append(temporary)
                fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
                try:
                    view = memoryview(data)
                    while view:
                        view = view[os.write(fd, view):]
                    os.fsync(fd)
                finally:
                    os.close(fd)
                os.replace(temporary, final)
                made[-1] = final
        except BaseException:
            for path in made:
                self.unlink(path)
            raise

    @staticmethod
    def unlink(path):
        """True when the file is gone (it was not there, or it was deleted)."""
        try:
            os.unlink(path)
            return True
        except FileNotFoundError:
            return True
        except OSError:
            return False

    def room(self, nbytes):
        """Whether an image of `nbytes` fits among the images that are kept (the number of files and the bytes)."""
        rows, size = self.store.image_usage()
        return rows < self.limits.image_files_max and size + nbytes <= self.limits.image_bytes_max

    # ---- deleting ----------------------------------------------------------------------------------------------------------------------------

    def sweep(self):
        """Delete the files of the images whose line is deleted or gone and of the uploads that waited too long, then their rows. The number of rows that went."""
        rows = self.store.images_gone(self.store.clock() - self.limits.image_pending_seconds)
        done = []
        for name, x in rows:
            if x in IMAGE_TYPES and re.fullmatch(r"[0-9a-f]{32}", name) and self.unlink(self.path(name, x)) & self.unlink(self.path(name, x, thumb=True)):
                done.append(name)
        if done:
            self.store.drop_images(done)
        return len(done)

    def scan(self):
        """Delete the files that have no row and are older than `image_scan_age_seconds` (and the temporary files of an upload that died). The number of files that went."""
        known = self.store.image_names()
        gone, seen = 0, 0
        cutoff = time.time() - self.limits.image_scan_age_seconds          # (the files' own times are the machine's: not the hub's clock, which a test moves)
        try:
            entries = os.scandir(self.dir)
        except OSError:
            return 0
        with entries:
            for entry in entries:
                seen += 1
                if seen > self.limits.image_scan_files_max:
                    break
                found = FILE.fullmatch(entry.name)
                try:
                    if found is None or (found.group(1) in known and not found.group(4)) or entry.stat().st_mtime > cutoff:
                        continue
                except OSError:
                    continue
                gone += self.unlink(entry.path)
        return gone

    def on_tick(self, now):
        """The clock's work: the sweep every `image_sweep_seconds`, the scan every `image_scan_seconds`. A store that refuses is the caller's to log."""
        if not self.on:
            return
        if now >= self.next_sweep:
            self.next_sweep = now + self.limits.image_sweep_seconds
            self.sweep()
        if now >= self.next_scan:
            self.next_scan = now + self.limits.image_scan_seconds
            self.scan()

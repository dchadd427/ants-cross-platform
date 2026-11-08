"""The moderator's command line (docs/HUB.md, "Moderation"):

    docker exec ants-hub python -m hub.admin list [--lines N]
    docker exec ants-hub python -m hub.admin mute ACCOUNT MINUTES        (0 lifts a mute)
    docker exec ants-hub python -m hub.admin ban ACCOUNT
    docker exec ants-hub python -m hub.admin unban ACCOUNT
    docker exec ants-hub python -m hub.admin del LINE_ID
    docker exec ants-hub python -m hub.admin status
    docker exec ants-hub python -m hub.admin purge [--keep-lines N]

ACCOUNT is an account id (digits, no leading zero), or a USERNAME (written @name when the name is all digits): the one name that is unique, from the account's profile, which its last login made. A
name that the chat shows is not a username (Discord display names are not unique), so it is never taken for one; `list` shows both where they differ, and an account that has no profile yet (it
has not logged in since profiles were kept) is addressed by its id. It works on hub.db directly (ANTS_HUB_DB or --db), so the running hub needs no network door for it: a mute or a ban counts
from the next line the account says, and a deleted line is taken off every screen within two seconds.
`status` says how full the database is: the pages that hold something, the log file on its own, what is free to reuse. `purge` frees space at once: it deletes the oldest lines (the moderators' and the deleted
ones last, the newest 1,000 never) until the database is at 80% of its limit, which is what the hub does by itself from 90%, or with --keep-lines N all but the newest N (100 or more); the pages they leave are
free for the lines that follow. It works a chunk at a time, with a tenth of a second between two chunks, so that the hub's own writes go on.
Exit status: 0 done, 1 nothing to do or not done (no such account, line or ban; nothing to purge; a reader holds the log file, so that it cannot be cut), 2 wrong use.
"""
import argparse
import sqlite3
import os
import sys
import time

from . import config, limits as limitmod, store as storemod, text

BY = "command line"
MAX_MINUTES = 525600                    # a year
MIN_KEEP_LINES = 100                    # what a page shows: purge never takes a line that may be on a screen
PAUSE_SECONDS = 0.1                     # between two chunks of purge: SQLite's wait for the write lock is not fair, and a tool that takes the lock again at once keeps the hub from its own writes


def when(epoch):
    return time.strftime("%Y-%m-%d %H:%M:%SZ", time.gmtime(epoch))


def resolve(store, who):
    """(account id, username) of ACCOUNT, or (None, why). A name is a username, looked up in the profiles; the name of an account that was given as an id is its username when it has a profile,
    else the name that it last spoke under (which, for an account with no profile, is its username)."""
    if who.startswith("@") or not config.ACCOUNT_ID.match(who):
        name = who[1:] if who.startswith("@") else who
        if not text.valid_name(name):
            return None, "not an account id or a username"
        account = store.account_of_username(name)
        return (account, name) if account else (None, "no account has that username, or more than one does (use the account id)")
    profile = store.profile(who)
    return who, profile.username if profile else store.last_name(who)


def show(store, count, out):
    out.write("bans:\n")
    for account, name, at, by in store.bans():
        out.write("  %s %s since %s by %s\n" % (account, name, when(at), by))
    out.write("mutes:\n")
    for account, name, until, by in store.mutes():
        out.write("  %s %s until %s by %s\n" % (account, name, when(until), by))
    out.write("lines (newest first; x = deleted; the name that was shown, and in brackets the account's username where it is another):\n")
    usernames = {}
    for line_id, at, account, name, line, deleted in store.lines(count):
        if account not in usernames:
            profile = store.profile(account)
            usernames[account] = profile.username if profile else name
        out.write("  %d%s %s %s %s%s: %s\n" % (line_id, "x" if deleted else " ", when(at), account, name, " (%s)" % usernames[account] if usernames[account] != name else "", line))


def megabytes(count):
    return "%.1f MB" % (count / 1048576.0)


def show_usage(store, limit, out):
    """The status line: the pages that hold something, of the limit; the log file, which counts for the limit as well and is told on its own, so that a long one is seen; what deleted lines left
    free to reuse; and the lines that are kept."""
    used = store.usage()
    out.write("database: %s of %s used (%d%%), log file %s; %s free to reuse; %s%s\n" % (
        megabytes(used.bytes), megabytes(limit), used.bytes * 100 // limit, megabytes(used.log), megabytes(used.free),
        "%s lines, the oldest from %s" % (format(used.lines, ","), when(used.oldest)) if used.lines else "no lines",
        "; at the limit (the log file counts): no new line is kept" if used.bytes + used.log >= limit else ""))


def purge(store, limits, keep_lines, out, err, sleep=time.sleep):
    """Delete the oldest lines a chunk at a time, with a pause between two chunks so that the hub's own writes go on: down to the hub's lower mark, or all but the newest `keep_lines`."""
    target, keep = (limits.prune_to_bytes, limits.keep_lines) if keep_lines is None else (0, keep_lines)
    show_usage(store, limits.db_max_bytes, out)
    gone = 0
    while True:
        chunk = store.make_room(target, limits.purge_chunk, keep)
        if not chunk:
            break
        gone += chunk
        sleep(PAUSE_SECONDS)
    for attempt in range(5):                # (the last cut of the log file was refused: the hub itself may be in the middle of a statement, and lets go in a moment)
        if not store.unsettled or store.checkpoint():
            break
        sleep(PAUSE_SECONDS)
    if gone:
        out.write("%s lines deleted\n" % format(gone, ","))
        show_usage(store, limits.db_max_bytes, out)
    if store.unsettled:                     # (still refused: a program has hub.db open for reading, and what went is in the log file until it lets go)
        err.write("hub.admin: a reader holds the log file (another program has hub.db open); the lines that went are in it until the reader lets go, and no more go while it is long\n")
        return 1
    if not gone:
        err.write("hub.admin: nothing to purge\n")
        return 1
    return 0


def run(argv, out=None, err=None, env=None):
    out, err = out or sys.stdout, err or sys.stderr
    env = os.environ if env is None else env
    parser = argparse.ArgumentParser(prog="hub.admin", description="Moderate the chat hub (see the top of hub/admin.py).")
    parser.add_argument("--db", default=env.get("ANTS_HUB_DB", "/data/hub.db"), help="the database (default: ANTS_HUB_DB, else /data/hub.db)")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("list", help="bans, mutes and the newest lines")
    p.add_argument("--lines", type=int, default=20)
    p = sub.add_parser("mute", help="mute an account for MINUTES (0 lifts it)")
    p.add_argument("account")
    p.add_argument("minutes", type=int)
    sub.add_parser("ban", help="ban an account until it is unbanned").add_argument("account")
    sub.add_parser("unban", help="lift a ban").add_argument("account")
    sub.add_parser("del", help="delete a chat line by its id").add_argument("line_id", type=int)
    sub.add_parser("status", help="how full the database is")
    p = sub.add_parser("purge", help="delete the oldest chat lines to free space")
    p.add_argument("--keep-lines", type=int, default=None, help="keep the newest N lines only (%d or more); without it, delete down to the hub's lower mark" % MIN_KEEP_LINES)
    try:
        args = parser.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    if args.command == "mute" and not 0 <= args.minutes <= MAX_MINUTES:
        err.write("hub.admin: MINUTES is 0 to %d\n" % MAX_MINUTES)
        return 2
    if args.command == "del" and not 0 < args.line_id < 2 ** 62:
        err.write("hub.admin: LINE_ID is a number from 1\n")
        return 2
    if args.command == "purge" and args.keep_lines is not None and not MIN_KEEP_LINES <= args.keep_lines < 2 ** 62:
        err.write("hub.admin: KEEP_LINES is %d or more\n" % MIN_KEEP_LINES)
        return 2
    if not os.path.exists(args.db):
        err.write("hub.admin: no database at that path (is the hub's volume mounted?)\n")
        return 1
    try:
        store = storemod.Store(args.db)
    except (storemod.StoreError, RuntimeError) as e:
        err.write("hub.admin: the database cannot be opened (%s)\n" % type(e).__name__)
        return 1
    try:
        if args.command == "list":
            show(store, max(0, min(args.lines, 500)), out)
            return 0
        if args.command == "status":
            show_usage(store, limitmod.Limits().db_max_bytes, out)
            return 0
        if args.command == "purge":
            return purge(store, limitmod.Limits(), args.keep_lines, out, err)
        if args.command == "del":
            if not store.delete_line(args.line_id, BY):
                err.write("hub.admin: no such line, or it is deleted already\n")
                return 1
            out.write("line %d deleted\n" % args.line_id)
            return 0
        account, name = resolve(store, args.account)
        if account is None:
            err.write("hub.admin: %s\n" % name)
            return 1
        if args.command == "mute":
            until = store.mute(account, args.minutes, BY, name)
            out.write("%s muted until %s\n" % (account, when(until)) if until else "%s is not muted\n" % account)
        elif args.command == "ban":
            store.ban(account, BY, name)
            out.write("%s banned\n" % account)
        else:
            if not store.unban(account):
                err.write("hub.admin: %s was not banned\n" % account)
                return 1
            out.write("%s unbanned\n" % account)
        return 0
    except (storemod.StoreError, sqlite3.Error) as e:             # (a full disk, a locked file, a damaged file: a message and not a traceback)
        err.write("hub.admin: the database refused it (%s)\n" % type(e).__name__)
        return 1
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))

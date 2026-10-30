"""The moderator's command line (docs/HUB.md, "Moderation"):

    docker exec ants-hub python -m hub.admin list [--lines N]
    docker exec ants-hub python -m hub.admin mute ACCOUNT MINUTES        (0 lifts a mute)
    docker exec ants-hub python -m hub.admin ban ACCOUNT
    docker exec ants-hub python -m hub.admin unban ACCOUNT
    docker exec ants-hub python -m hub.admin del LINE_ID

ACCOUNT is an account id (digits, no leading zero), or a name that has said something in the last 7 days (written @name when the name is all digits). It works on hub.db directly (ANTS_HUB_DB or
--db), so the running hub needs no network door for it: a mute or a ban counts from the next line the account says, and a deleted line is taken off every screen within two seconds.
Exit status: 0 done, 1 nothing to do (no such account, line or ban), 2 wrong use.
"""
import argparse
import os
import sys
import time

from . import config, store as storemod, text

BY = "command line"
MAX_MINUTES = 525600                    # a year


def when(epoch):
    return time.strftime("%Y-%m-%d %H:%M:%SZ", time.gmtime(epoch))


def resolve(store, who):
    """(account id, name) of ACCOUNT, or (None, why)."""
    if who.startswith("@") or not config.ACCOUNT_ID.match(who):
        name = who[1:] if who.startswith("@") else who
        if not text.valid_name(name):
            return None, "not an account id or a name"
        account = store.account_of_name(name)
        return (account, name) if account else (None, "no line of that name is kept (use the account id)")
    return who, store.last_name(who)


def show(store, count, out):
    out.write("bans:\n")
    for account, name, at, by in store.bans():
        out.write("  %s %s since %s by %s\n" % (account, name, when(at), by))
    out.write("mutes:\n")
    for account, name, until, by in store.mutes():
        out.write("  %s %s until %s by %s\n" % (account, name, when(until), by))
    out.write("lines (newest first; x = deleted):\n")
    for line_id, at, account, name, line, deleted in store.lines(count):
        out.write("  %d%s %s %s %s: %s\n" % (line_id, "x" if deleted else " ", when(at), account, name, line))


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
    except storemod.StoreError as e:                                  # (a full disk, a locked file: a message and not a traceback)
        err.write("hub.admin: the database refused it (%s)\n" % type(e).__name__)
        return 1
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))

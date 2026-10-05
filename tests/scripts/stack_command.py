#!/usr/bin/env python3
"""The command of the game server in docker-compose.stack.yml, as the stack starts it when NOTHING is set in its environment: every ${NAME:-default} is its default.

  stack_command.py docker-compose.stack.yml            the arguments of the server, on one line, separated by blanks (none of them holds a blank)
  stack_command.py docker-compose.stack.yml --demo     only the demo options: --demo-rooms N --demo-map FILE --demo-maps A,B,...

tests/scripts/test_default_map.py (the default map of the stack, quick tier) and tests/scripts/test_ants_server.sh (a real server started with these options) read the file
through this one parser. An environment variable that the owner of a stack sets (ANTS_DEMO_MAP and the others) replaces the default; this reads the defaults of the file.
"""
import json
import re
import sys

SUBSTITUTION = re.compile(r"\$\{[A-Za-z_][A-Za-z0-9_]*:-([^}]*)\}")
SERVICE = "ants-server"


def server_command(compose_text):
    """The `command: [...]` of the service ants-server as a list of strings, the ${NAME:-default} of every argument replaced by its default."""
    in_service = False
    for line in compose_text.splitlines():
        if re.match(r"^  " + re.escape(SERVICE) + r":\s*$", line):
            in_service = True
            continue
        if in_service and re.match(r"^  [A-Za-z0-9_-]+:\s*$", line):         # the next service of the file
            break
        match = re.match(r"^\s+command:\s*(\[.*\])\s*$", line) if in_service else None
        if match:
            return [SUBSTITUTION.sub(lambda m: m.group(1), arg) for arg in json.loads(match.group(1))]
    raise ValueError("docker-compose.stack.yml has no `command: [...]` for the service " + SERVICE)


def demo_options(args):
    """{'--demo-rooms': '48', '--demo-map': 'TREASURE.LVL', '--demo-maps': '...'}: the options of the demo rooms that the command holds (each followed by its value)."""
    found = {}
    for i, arg in enumerate(args):
        if arg in ("--demo-rooms", "--demo-map", "--demo-maps") and i + 1 < len(args):
            found[arg] = args[i + 1]
    return found


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: stack_command.py docker-compose.stack.yml [--demo]\n")
        return 2
    with open(argv[1], encoding="utf-8") as f:
        args = server_command(f.read())
    if "--demo" in argv[2:]:
        options = demo_options(args)
        if sorted(options) != ["--demo-map", "--demo-maps", "--demo-rooms"]:
            sys.stderr.write("the command has no complete set of demo options: %r\n" % (options,))
            return 1
        args = [part for name in ("--demo-rooms", "--demo-map", "--demo-maps") for part in (name, options[name])]
    print(" ".join(args))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

"""One line for each event on standard output: `<time> <event> key=value ...`. Never a secret, a cookie, a chat line or a whole address: a value is cut to plain characters, so text from a
client cannot forge a line, a run of 15 digits or more (an id) is replaced, and the only trace of an address is a short tag (a keyed hash that changes with every start) to tell connections apart."""
import hashlib
import hmac
import os
import re
import sys
import time

SAFE = re.compile(r"[^A-Za-z0-9_.:/-]")
LONG_NUMBER = re.compile(r"[0-9]{15,}")      # a Discord id is 17 to 20 digits and is like a password: no line of the log holds one, whatever a caller puts in it (nothing else here is that long)


class Log:
    def __init__(self, stream=None, clock=time.time):
        self.stream = stream or sys.stdout
        self.clock = clock
        self.salt = os.urandom(16)

    def tag(self, address_key):
        return hmac.new(self.salt, address_key.encode("utf-8"), hashlib.sha256).hexdigest()[:8]

    def event(self, name, **fields):
        parts = [time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(self.clock())), name]
        parts += ["%s=%s" % (k, LONG_NUMBER.sub("#", SAFE.sub("_", str(v)))[:60]) for k, v in fields.items()]
        try:
            self.stream.write(" ".join(parts) + "\n")
            self.stream.flush()
        except (OSError, ValueError):
            pass

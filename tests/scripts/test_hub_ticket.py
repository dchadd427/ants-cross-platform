#!/usr/bin/env python3
"""The hub's pass for the game server's name check (hub/ticket.py; docs/HUB.md "Settings"): the exact bytes of a pass, the fit of the game name, the setting that holds the secret.

Every secret in this file is a made-up test value; none belongs to a stack. The tables are tests/data/name_tickets.txt (passes, read here and by the game server's tests) and
tests/data/game_name_fit.txt (the cases of the fit, read here and by the page's check). The pass is also decoded and its MAC recomputed below WITHOUT hub/ticket.py, so that a change
of the layout cannot go through in both places.
"""
import base64
import contextlib
import hashlib
import hmac
import io
import json
import math
import os
import random
import re
import struct
import sys
import unicodedata
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
from hub import config, server, ticket  # noqa: E402
from hub.limits import Limits  # noqa: E402
from hub.session import Identity  # noqa: E402

SECRET = "x" * 32                       # a test value (obviously fake; so is every secret of the tables)
FIXED = ("n1.AQAAAAAAAABY81NlAAECAwQFBgf_RxBJ31eRktl_RT346Vbz", SECRET, "1", 1700000600, bytes(range(8)), "Ann")     # the pass of the first line of the table, pinned here by hand
NAME_ENV = "ANTS_NAME_TICKET_SECRET"


def read_table(name):
    with open(os.path.join(REPO, "tests", "data", name), encoding="utf-8") as f:
        return [line.rstrip("\n") for line in f if line.strip() and not line.startswith("#")]


def vectors():
    rows = []
    for line in read_table("name_tickets.txt"):
        secret, account, expires, nonce, name, token = line.split("\t")
        rows.append((secret, account, int(expires), bytes.fromhex(nonce), name, token))
    return rows


def fit_cases():
    return [tuple(json.loads(part) for part in line.split("\t")) for line in read_table("game_name_fit.txt")]


def decode(token):
    """The parts of a pass, from the layout alone (not from hub/ticket.py)."""
    assert token.startswith("n1.") and len(token) == 51
    raw = base64.urlsafe_b64decode(token[3:] + "=")
    assert len(raw) == 36
    return struct.unpack("<Q", raw[0:8])[0], struct.unpack("<I", raw[8:12])[0], raw[12:20], raw[20:36], raw[0:20]


def mac_of(secret, head, name):
    return hmac.new(secret.encode(), b"ants-name-ticket-1\x00" + head + name.encode(), hashlib.sha256).digest()[:16]


class TheBytesOfAPass(unittest.TestCase):
    def test_the_table_is_made_by_make_ticket_and_by_the_layout_alone(self):
        rows = vectors()
        self.assertGreaterEqual(len(rows), 8)
        for secret, account, expires, nonce, name, token in rows:
            self.assertEqual(ticket.make_ticket(secret, account, name, expires - 600, nonce), token)
            who, end, n, mac, head = decode(token)
            self.assertEqual((who, end, n), (int(account), expires, nonce))
            self.assertTrue(hmac.compare_digest(mac, mac_of(secret, head, name)))

    def test_a_fixed_pass_pins_every_byte(self):
        token, secret, account, expires, nonce, name = FIXED
        self.assertEqual(ticket.make_ticket(secret, account, name, expires - 600, nonce), token)
        self.assertEqual(vectors()[0][5], token)

    def test_the_table_spans_the_edges(self):
        rows = vectors()
        self.assertIn(1, [int(r[1]) for r in rows])
        self.assertIn(2 ** 63 - 1, [int(r[1]) for r in rows])
        self.assertIn(2 ** 32 - 1, [r[2] for r in rows])
        self.assertIn(256, [len(r[0]) for r in rows])
        self.assertIn(32, [len(r[0]) for r in rows])
        self.assertIn(2, [len(r[4]) for r in rows])
        self.assertIn(32, [len(r[4]) for r in rows])

    def test_each_part_of_the_layout_matters(self):
        """The ways to get the layout wrong that the pinned pass must refuse: another domain string, no NUL after it, the nonce in the other order, a MAC of 15 or 17 bytes, a big endian end."""
        token, secret, account, expires, nonce, name = FIXED
        head = struct.pack("<QI", int(account), expires) + nonce
        wrong = {
            "domain": b"ants-name-ticket-2\x00" + head + name.encode(),
            "no nul": b"ants-name-ticket-1" + head + name.encode(),
            "no name": b"ants-name-ticket-1\x00" + head,
            "name first": b"ants-name-ticket-1\x00" + name.encode() + head,
        }
        for what, message in wrong.items():
            mac = hmac.new(secret.encode(), message, hashlib.sha256).digest()[:16]
            self.assertNotEqual(ticket.PREFIX + base64.urlsafe_b64encode(head + mac).decode().rstrip("="), token, what)
        right = hmac.new(secret.encode(), b"ants-name-ticket-1\x00" + head + name.encode(), hashlib.sha256).digest()
        for size in (15, 17, 32):
            self.assertNotEqual(ticket.PREFIX + base64.urlsafe_b64encode(head + right[:size]).decode().rstrip("="), token, size)
        swapped = struct.pack("<QI", int(account), expires) + nonce[::-1]
        self.assertNotEqual(ticket.PREFIX + base64.urlsafe_b64encode(swapped + right[:16]).decode().rstrip("="), token)
        big = struct.pack("<Q", int(account)) + struct.pack(">I", expires) + nonce
        self.assertNotEqual(ticket.PREFIX + base64.urlsafe_b64encode(big + right[:16]).decode().rstrip("="), token)
        self.assertEqual(token[3:7], "AQAA")                                          # (the account id comes first, little endian: 1, then zeros)

    def test_the_end_is_in_the_second_field_and_is_now_plus_the_life(self):
        self.assertEqual(Limits.ticket_seconds, 600.0)
        for now in (0, 1700000000, 1700000000.9, 2 ** 32 - 601):
            who, end, n, mac, head = decode(ticket.make_ticket(SECRET, "5", "Ann", now, bytes(8)))
            self.assertEqual(end, int(now) + 600)
        self.assertEqual(ticket.ticket_end(1700000000.9), 1700000600)

    def test_a_pass_is_51_printable_characters_and_never_the_same_twice(self):
        seen = set()
        for _ in range(200):
            t = ticket.make_ticket(SECRET, "123456789", "Ann Lee", 1700000000)
            self.assertEqual(len(t), 51)
            self.assertRegex(t, r"\An1\.[A-Za-z0-9_-]{48}\Z")
            self.assertTrue(all(0x21 <= ord(c) <= 0x7e for c in t))
            seen.add(decode(t)[2])
        self.assertEqual(len(seen), 200)                                                # (a new nonce each time)
        self.assertNotEqual(ticket.make_ticket(SECRET, "1", "Ann", 5), ticket.make_ticket(SECRET, "1", "Ann", 5))

    def test_the_name_and_the_secret_and_the_account_change_the_mac(self):
        base = ticket.make_ticket(SECRET, "1", "Ann", 5, bytes(8))
        for other in (ticket.make_ticket(SECRET, "1", "Anne", 5, bytes(8)), ticket.make_ticket("y" * 32, "1", "Ann", 5, bytes(8)), ticket.make_ticket(SECRET, "2", "Ann", 5, bytes(8)),
                      ticket.make_ticket(SECRET, "1", "Ann", 6, bytes(8)), ticket.make_ticket(SECRET, "1", "Ann", 5, bytes(7) + b"\x01")):
            self.assertNotEqual(base[-22:], other[-22:])                                # (the last 16 bytes are the MAC)
        self.assertEqual(decode(base)[3], mac_of(SECRET, decode(base)[4], "Ann"))
        self.assertNotEqual(decode(base)[3], mac_of(SECRET, decode(base)[4], "ann"))


class TheRefusalsOfMakeTicket(unittest.TestCase):
    def refused(self, *args, **kw):
        with self.assertRaises(ValueError) as cm:
            ticket.make_ticket(*args, **kw)
        return str(cm.exception)

    def test_an_account_id_that_is_not_an_account_id(self):
        for bad in ("0", "01", "", "abc", "-1", "1 ", "9223372036854775808", "99999999999999999999", "18446744073709551615", 5, None, b"5", 5.0):
            self.refused(SECRET, bad, "Ann", 1700000000)
        ticket.make_ticket(SECRET, "9223372036854775807", "Ann", 1700000000)          # (the largest is fine)

    def test_a_name_that_the_fit_would_not_give_back_as_itself(self):
        for bad in ("", "a", "x" * 33, " Ann", "Ann ", "A  B", "Jos\u00e9", "Bot (Easy)", "Pip (bot)", "Guest 1", "guest", "\u0418\u0432\u0430\u043d", "A\tB", "A\u00a0B", None, 5, b"Ann"):
            self.refused(SECRET, "1", bad, 1700000000)
        for good in ("Al", "x" * 32, "Guest Star", "Pip (hard)"):
            ticket.make_ticket(SECRET, "1", good, 1700000000)

    def test_an_end_outside_32_bits_or_a_clock_that_is_not_a_number(self):
        for bad in (-601, -1000, 2 ** 32 - 600, 2 ** 40, float("inf"), float("nan"), None, "5", True):
            self.refused(SECRET, "1", "Ann", bad)
        ticket.make_ticket(SECRET, "1", "Ann", 2 ** 32 - 601)
        ticket.make_ticket(SECRET, "1", "Ann", -600)                                   # (an end of 0: inside)
        ticket.make_ticket(SECRET, "1", "Ann", 0)

    def test_a_nonce_that_is_not_8_bytes_and_a_secret_that_is_not_one(self):
        for bad in (b"", bytes(7), bytes(9), "12345678", 12345678, [0] * 8):
            self.refused(SECRET, "1", "Ann", 5, bad)
        for bad in ("", "x" * 31, "x" * 257, "x" * 31 + " ", "x" * 32 + "\n", "x" * 31 + "\x01", "x" * 31 + "\u00e9", None, b"x" * 32):
            self.refused(bad, "1", "Ann", 5)
        ticket.make_ticket("x" * 256, "1", "Ann", 5)

    def test_the_messages_name_nothing(self):
        secret = "Zq" * 20 + "9!"
        for args in ((secret, "0", "Ann", 5), (secret, "1", "Bot (Easy)", 5), (secret + " ", "1", "Ann", 5), (secret, "123456789012345678901", "Ann", 5), (secret, "1", "Ann", 2 ** 40)):
            message = self.refused(*args)
            for canary in (secret, args[1], args[2], "Bot (Easy)", "1234567890"):
                if canary and canary not in ("1", "0", "5", "Ann"):
                    self.assertNotIn(canary, message)
            self.assertLess(len(message), 80)

    def test_the_module_writes_nothing(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            ticket.make_ticket(SECRET, "1", "Ann", 5)
            ticket.fit_game_name("Ann")
            ticket.game_name_of(Identity("1", "Ann", False, "s", 0))
        self.assertEqual((out.getvalue(), err.getvalue()), ("", ""))
        with open(os.path.join(REPO, "hub", "ticket.py"), encoding="utf-8") as f:
            source = f.read()
        for word in ("print(", "logging", "import log", "sys.std", "open("):
            self.assertNotIn(word, source)


class TheFit(unittest.TestCase):
    def test_every_case_of_the_table(self):
        cases = fit_cases()
        self.assertGreaterEqual(len(cases), 60)
        for given, want in cases:
            self.assertEqual(ticket.fit_game_name(given), want, ascii(given))

    def test_the_table_has_every_kind(self):
        given = [c[0] for c in fit_cases()]
        wanted = {c[0]: c[1] for c in fit_cases()}
        self.assertIn("Jos\u00e9", given)
        self.assertEqual(wanted["Jos\u00e9"], "Jose")
        for name in ("Bot (Easy)", "Pip (bot, Hard)", "Guest 123456", "guest", "GUEST42", "Guest Star", "x" * 32, "x" * 33, "     ", "\u0418\u0432\u0430\u043d", "\u0391\u03bb\u03ad\u03be", "\uff21\uff4e\uff4e"):
            self.assertIn(name, given)
        self.assertEqual(wanted["Guest Star"], "Guest Star")
        self.assertTrue(any(0xd800 <= ord(c) or ord(c) > 0xffff for g in given for c in g))                  # (emoji)
        self.assertTrue(any(ord(c) < 0x20 for g in given for c in g))                                       # (controls)
        self.assertTrue(any(len(g) > 256 for g in given))                                                   # (the cut at 256 code points)

    def test_what_is_not_text_is_refused(self):
        for bad in (None, 5, b"Ann", ["Ann"], {"a": 1}, 1.5):
            self.assertEqual(ticket.fit_game_name(bad), "")

    def test_the_guest_rule_alone(self):
        for form in ("Guest", "guest", "GUEST", "gUeSt", "Guest1", "Guest 123456", "guest0", "G U E S T 9 9", "Guest" + "9" * 20, "guest\uff11"):
            self.assertEqual(ticket.fit_game_name(form), "", form)
        for form in ("Guest Star", "Guests", "Gues", "Guest a", "Guest-1", "Guest#1", "Mr Guest", "Guest 1x", "xguest"):
            self.assertEqual(ticket.fit_game_name(form), form, form)

    def test_the_bot_rule_alone(self):
        for form in ("Bot (Easy)", "bot(", "(bot)", "Pip (bot)", "Pip (Bot, Hard)", "Pip ( bot , x y )", "BOT(1)"):
            self.assertEqual(ticket.fit_game_name(form), "", form)
        for form in ("Robot (Easy)", "Bot Fan", "Pip (bot) x", "Pip (bot,(x))", "Pip (bots)"):
            self.assertEqual(ticket.fit_game_name(form), form, form)

    def test_a_letter_that_would_be_lost_refuses_but_a_sign_or_a_mark_just_goes(self):
        self.assertEqual(ticket.fit_game_name("S\u00f8ren"), "")
        self.assertEqual(ticket.fit_game_name("Ann \u0418"), "")
        self.assertEqual(ticket.fit_game_name("Ann \u0663"), "")                         # (a digit of another script)
        self.assertEqual(ticket.fit_game_name("Ann \u2665 \U0001f600"), "Ann")
        self.assertEqual(ticket.fit_game_name("Ann\u0301\u0302"), "Ann")
        self.assertEqual(ticket.fit_game_name("Ann" + " " * 253 + "\u0418"), "Ann")          # (the cut at 256 code points comes first)
        self.assertEqual(ticket.fit_game_name("Ann" + " " * 252 + "\u0418"), "")

    def test_the_cut_counts_code_points_and_not_utf16_units(self):
        e = chr(0x1f600)
        self.assertEqual(ticket.fit_game_name(e * 127 + " Ann"), "Ann")               # (131 code points, 258 units: the page's old cut would lose the end)
        self.assertEqual(ticket.fit_game_name("Ann" + e * 200 + chr(0x418)), "")       # (204 code points: the letter is inside the cut)
        self.assertEqual(ticket.fit_game_name("Ann" + e * 252 + chr(0x418)), "")       # (it is the 256th)
        self.assertEqual(ticket.fit_game_name("Ann" + e * 253 + chr(0x418)), "Ann")    # (the 257th)

    def test_nfkd_and_not_nfkc(self):
        self.assertEqual(ticket.fit_game_name("Cr\u00e8me"), "Creme")                  # (NFKC would keep the accent and refuse)
        self.assertEqual(ticket.fit_game_name("e\u0301e"), "ee")

    def test_both_ends_are_trimmed_and_blanks_are_one(self):
        for given, want in ((" Ann", "Ann"), ("Ann ", "Ann"), ("  A  B  ", "A B"), ("A \U0001f600 \U0001f600 B", "A B"), ("\u00a0Ann\u00a0", "Ann"), ("\u3000Ann", "Ann")):
            self.assertEqual(ticket.fit_game_name(given), want)

    def test_the_length_limits_are_2_and_32(self):
        self.assertEqual(ticket.fit_game_name("ab"), "ab")
        self.assertEqual(ticket.fit_game_name("a"), "")
        self.assertEqual(ticket.fit_game_name("a" * 32), "a" * 32)
        self.assertEqual(ticket.fit_game_name("a" * 33), "")
        self.assertEqual(ticket.fit_game_name(" " + "a" * 32 + " "), "a" * 32)
        self.assertEqual(ticket.fit_game_name("a" + "\u0301" * 40), "")                  # (one character left)

    def test_the_name_of_a_login_is_the_shown_name_then_the_other(self):
        def of(name, other):
            return ticket.game_name_of(Identity("1", name, False, "s", 0, "u", "display", other))
        self.assertEqual(of("Ann", "ann_99"), "Ann")
        self.assertEqual(of("\u0418\u0432\u0430\u043d", "ivan_77"), "ivan_77")
        self.assertEqual(of("Guest 123456", "ann_99"), "ann_99")
        self.assertEqual(of("Bot (Easy)", "ann_99"), "ann_99")
        self.assertEqual(of("\u0418", "\u0418\u0432\u0430\u043d"), "")
        self.assertEqual(of("Ann", None), "Ann")
        self.assertEqual(of("A", "Bo"), "Bo")                                           # (the first is too short)
        self.assertEqual(of("Ann", "Bob"), "Ann")                                       # (never the other when the first fits)
        self.assertEqual(ticket.game_name_of(Identity("1", "Ann", False)), "Ann")       # (a login that has no other name)
        self.assertEqual(ticket.game_name_of(None), "")


def mixed_text(r):
    """A random string that mixes the classes of the table: plain, accented, full-width, other scripts, signs, emoji, marks, controls, blanks, the words that are special."""
    pieces = ["Ann", "bob", "Jos\u00e9", "\u00c5", "\uff21\uff42", "\u0418", "\u0391", "\u7530", "\u0663", "\u00b2", "\u2665", "\U0001f600", "\u0301", "\u200b", "\u00a0", "\u3000", "\t", "\n", "\x00",
              "\x7f", " ", "  ", "Bot (", "(bot)", "(bot, x)", "guest", "Guest ", "123", "(", ")", ",", "!", "~", "x" * 20, "\u00f8", "\u00df"]
    return "".join(r.choice(pieces) for _ in range(r.randrange(0, 9)))


class TheFitOnFreshInputs(unittest.TestCase):
    def test_ten_thousand_random_strings_keep_the_laws(self):
        r = random.Random(20261109)
        kept = 0
        for _ in range(10000):
            text = mixed_text(r)
            out = ticket.fit_game_name(text)
            if out == "":
                continue
            kept += 1
            self.assertTrue(2 <= len(out) <= 32, ascii(text))
            self.assertTrue(all(0x20 <= ord(c) <= 0x7e for c in out), ascii(text))
            self.assertEqual(out, out.strip(" "), ascii(text))
            self.assertNotIn("  ", out, ascii(text))
            compact = out.replace(" ", "").lower()
            self.assertIsNone(re.search(r"^bot\(|\(bot(,[^()]+)?\)$", compact), ascii(text))
            self.assertIsNone(re.match(r"guest[0-9]*$", compact), ascii(text))
            self.assertEqual(ticket.fit_game_name(out), out, ascii(text))             # (fitting a fitted name changes nothing)
            ticket.make_ticket(SECRET, "7", out, 5, bytes(8))                          # (and a pass can be made for it)
        self.assertGreater(kept, 1500)                                                  # (the generator is not all refusals)
        self.assertLess(kept, 9500)                                                     # (nor all passes)

    def test_a_refusal_has_a_reason_in_the_input(self):
        """The other direction on fresh inputs: what is refused has a reason that the test finds by itself (a lost letter or digit, a length, a special form), so a fit that refuses too much fails."""
        r = random.Random(77)
        for _ in range(10000):
            text = mixed_text(r)
            if ticket.fit_game_name(text) != "":
                continue
            s = unicodedata.normalize("NFKD", text[:256])
            lost = any(not 0x20 <= ord(c) <= 0x7e and unicodedata.category(c)[0] in "LN" for c in s)
            s = re.sub(" +", " ", "".join(c for c in s if 0x20 <= ord(c) <= 0x7e)).strip(" ")
            compact = s.replace(" ", "").lower()
            special = re.search(r"^bot\(|\(bot(,[^()]+)?\)$", compact) or re.match(r"guest[0-9]*$", compact)
            self.assertTrue(lost or not 2 <= len(s) <= 32 or special, ascii(text))


class TheSetting(unittest.TestCase):
    NAME = "ANTS_NAME_TICKET_SECRET must be 32 to 256 printable characters with no blank"

    def test_missing_or_empty_is_off(self):
        for env in ({}, {NAME_ENV: ""}):
            cfg = config.load(env)
            self.assertEqual(cfg.name_ticket_secret, "")
            self.assertEqual(cfg.summary()["name_tickets"], 0)

    def test_a_good_value_is_on_and_only_a_count_shows(self):
        for secret in ("x" * 32, "Q" * 256, "aB3$" * 10, "~" * 40, "!" * 32):
            cfg = config.load({NAME_ENV: secret})
            self.assertEqual(cfg.name_ticket_secret, secret)
            self.assertEqual(cfg.summary()["name_tickets"], 1)
            for text in (repr(cfg), str(cfg), json.dumps(cfg.summary()), repr(cfg.summary())):
                self.assertNotIn(secret, text)

    def test_the_summary_gains_the_one_key_and_nothing_else(self):
        off, on = config.load({}).summary(), config.load({NAME_ENV: "x" * 32}).summary()
        self.assertEqual(list(off), ["port", "origin", "mods", "test_login", "login_configured", "min_account_days", "name_tickets"])
        self.assertEqual(set(off), set(on))
        self.assertEqual({k: v for k, v in off.items() if k != "name_tickets"}, {k: v for k, v in on.items() if k != "name_tickets"})

    def test_a_wrong_value_stops_the_start_with_the_name_of_the_variable_and_not_the_value(self):
        bad = ["x" * 31, "x" * 257, "x" * 15 + " " + "x" * 16, "x" * 32 + " ", " " + "x" * 32, "x" * 31 + "\t", "x" * 31 + "\n", "x" * 31 + "\x00", "x" * 31 + "\x7f", "x" * 31 + "\u00e9", "x" * 31 + "\u3000", " ", "\n", "k" * 40 + "\r"]
        for value in bad:
            with self.assertRaises(config.ConfigError) as cm:
                config.load({NAME_ENV: value})
            self.assertEqual(str(cm.exception), self.NAME)
            if len(value.strip()) > 8:
                self.assertNotIn(value.strip(), str(cm.exception))
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                self.assertEqual(server.main({NAME_ENV: value, "ANTS_HUB_PORT": "0"}), 2)
            self.assertEqual(err.getvalue(), "hub: %s\n" % self.NAME)

    def test_a_wrong_value_with_a_recognisable_secret_is_not_in_the_message(self):
        secret = "canary-ticket-secret-9f3a7c1e"                                       # (29 characters: too short)
        with self.assertRaises(config.ConfigError) as cm:
            config.load({NAME_ENV: secret})
        self.assertNotIn("canary", str(cm.exception))
        self.assertNotIn(secret, repr(cm.exception))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(server.main({NAME_ENV: secret}), 2)
        self.assertNotIn("canary", err.getvalue())

    def test_the_field_is_not_in_the_repr_and_is_not_the_login_secret(self):
        field = next(f for f in config.dataclasses.fields(config.Config) if f.name == "name_ticket_secret")
        self.assertFalse(field.repr)
        self.assertEqual(config.load({NAME_ENV: "x" * 32}).login_client_secret, "")

    def test_the_other_settings_are_as_before(self):
        cfg = config.load({"ANTS_HUB_MODS": "123", "ANTS_HUB_PORT": "4041"})
        self.assertEqual((cfg.port, cfg.mods, cfg.name_ticket_secret), (4041, frozenset(["123"]), ""))
        with self.assertRaises(config.ConfigError):
            config.load({"ANTS_HUB_PORT": "x", NAME_ENV: "x" * 32})


class TheDocs(unittest.TestCase):
    def test_the_setting_and_the_life_are_described(self):
        with open(os.path.join(REPO, "docs", "HUB.md"), encoding="utf-8") as f:
            text = f.read()
        self.assertIn("`ANTS_NAME_TICKET_SECRET`", text)
        self.assertIn("name_tickets", text)
        self.assertEqual(Limits.ticket_seconds, 600.0)
        self.assertFalse(math.isnan(Limits.ticket_seconds))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""The images of the chat without a socket (hub/images.py, hub/chat.py, hub/commands.py, hub/store/, hub/admin.py; docs/HUB_IMAGES.md): who may post an image, what a line with an image is, how the files and the rows
go, and the flag that takes a person's posting away. The chat is driven with plain fake peers and a fake clock (tests/scripts/test_hub_core.py World); the upload's own door, the converter's answers and the real
process are tests/scripts/test_hub_images_door.py. Every id and name here is made up.

  TheWho          one function says whether an account may post: the operator's account, the images' role, a moderator, an administrator; the flag beats every role but the operator's; nothing is known (the flags
                  were not read, Discord has not answered) means no; a hub with no converter has no images at all, and says nothing of them
  TheLine         a line with an image: its frame, the history, a restart; the caption; the upload's id (its own, once, for ten minutes); a person who may not; banned, muted, young; never a command
  TheDeletes      a line that goes takes its files with it, by every way a line can go (a moderator, its author, a ban with purge, Forget me, its age, the newest 200, the command line); a failed delete is
                  repaired; a file that has no row is deleted when it is old
  TheFlag         /noimages and /images: only administrators, the table of the powers, the log, the live `you`, the command line, Forget me and a restart
"""
import dataclasses
import io
import json
import os
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..")))
sys.path.insert(0, HERE)
import test_hub_core as core  # noqa: E402
from hub import admin, chat as chatmod, config, images as imagesmod, limits as limitmod, log as logmod, pseudo, session, store as storemod  # noqa: E402
from hub.store.base import Image  # noqa: E402

SITE = "https://chat.example.org"
OWNER = "8100000000000001"
ALICE = session.Identity("8100000000000002", "alice", False)             # may post: she holds the images' role
BOB = session.Identity("8100000000000003", "bob", False)                 # holds nothing
CAROL = session.Identity("8100000000000004", "carol", False)             # a moderator
DAVE = session.Identity("8100000000000005", "dave", False)               # an administrator
ERIN = session.Identity("8100000000000006", "erin", False)               # an administrator too
YOUNG = session.Identity("8100000000000007", "newbie", True)
OPERATOR = session.Identity(OWNER, "olive.owner", False)
JPEG = b"\xff\xd8\xff\xe0made-up jpeg bytes\xff\xd9"
PNG = b"\x89PNG\r\n\x1a\nmade-up png bytes IEND\xaeB`\x82"
GIF = b"GIF89a made-up gif bytes ;"
LIMITS = dataclasses.replace(core.LIMITS, burst=10 ** 6, refill_seconds=0.001, global_burst=10 ** 6, global_refill_seconds=0.001, account_hour_lines=10 ** 6, account_day_lines=10 ** 6)


class ImageWorld(core.World):
    """core.World with the images: a folder for the files, the settings that switch them on, and the hub's own `Images` for the chat."""

    def __init__(self, converter=True, **kw):
        kw.setdefault("limits", LIMITS)
        kw.setdefault("mods", ())
        super().__init__(**kw)
        self.folder_images = tempfile.TemporaryDirectory()
        env = {"ANTS_HUB_PUBLIC_URL": SITE, "ANTS_HUB_IMAGE_DIR": self.folder_images.name}
        if converter:
            env["ANTS_HUB_IMAGE_CONVERTER_URL"] = "http://127.0.0.1:1"
        self.cfg = config.load(env)
        self.log_text = io.StringIO()
        self.roles.roles.update({CAROL.account_id: "mod", DAVE.account_id: "admin", ERIN.account_id: "admin"})
        self.roles.images.add(ALICE.account_id)
        self.images = imagesmod.Images(self.cfg, self.limits, self.store, self.roles, self.clock, self.wall, logmod.Log(self.log_text), self.chat.ref, lambda a: a == OWNER, self.chat.key_of)
        self.chat.images = self.images
        self.chat.on_store_error = lambda e: None
        self.images.sync()
        for who in (ALICE, BOB, CAROL, DAVE, ERIN, OPERATOR):
            self.store.set_profile(who.account_id, who.username, "", 1000)

    def close(self):
        super().close()
        self.folder_images.cleanup()

    def pending(self, who, x="jpg", data=None):
        """An upload that waits for `who` (the converter's work is not the subject here): its files are written and its row is kept; the public name."""
        name = self.images.new_name()
        data = data or {"jpg": JPEG, "png": PNG, "gif": GIF}[x]
        self.images.write(name, x, data, JPEG)
        self.store.add_image(who.account_id, name, Image(name, x, 800, 600, 360, 270, x == "gif"), len(data) + len(JPEG))
        return name

    def say_image(self, peer, text, name):
        self.clock.now += 3
        self.chat.on_text(peer, json.dumps({"t": "say", "text": text, "img": name}))

    def command(self, peer, text):
        self.clock.now += 3
        self.say(peer, text)

    def files(self):
        return sorted(os.listdir(self.folder_images.name))

    def exists(self, name, x="jpg", thumb=False):
        return os.path.exists(self.images.path(name, x, thumb=thumb))


class WorldTest(unittest.TestCase):
    def setUp(self):
        self.w = ImageWorld()
        self.addCleanup(self.w.close)
        self.alice, self.bob, self.carol, self.dave, self.erin, self.operator, self.reader = (self.w.join(i) for i in (ALICE, BOB, CAROL, DAVE, ERIN, OPERATOR, None))


class TheWho(WorldTest):
    def test_the_operator_the_images_role_a_moderator_and_an_administrator_may_and_nobody_else(self):
        for who, state in ((OPERATOR, "on"), (ALICE, "on"), (CAROL, "on"), (DAVE, "on"), (BOB, "role"), (YOUNG, "role")):
            self.assertEqual(self.w.images.state_of(who.account_id), state, who.username)
        self.assertTrue(self.w.images.may_post(ALICE.account_id))
        self.assertFalse(self.w.images.may_post(BOB.account_id))

    def test_nothing_known_is_no_and_is_told_as_wait_not_as_role(self):
        self.w.roles.unknown.update({ALICE.account_id, CAROL.account_id, BOB.account_id})
        for who in (ALICE, CAROL, BOB):
            self.assertEqual(self.w.images.state_of(who.account_id), "wait", who.username)
            self.assertFalse(self.w.images.may_post(who.account_id))
        self.assertEqual(self.w.images.state_of(OPERATOR.account_id), "on", "the operator's account needs no answer")

    def test_the_flag_beats_every_role_but_the_operators_account(self):
        for who in (ALICE, CAROL, DAVE, BOB):
            self.w.store.set_noimages(who.account_id, True, self.w.chat.ref(DAVE.account_id))
        self.w.store.set_noimages(OWNER, True, self.w.chat.ref(DAVE.account_id))
        self.w.images.sync()
        for who in (ALICE, CAROL, DAVE, BOB):
            self.assertEqual(self.w.images.state_of(who.account_id), "off", who.username)
        self.assertEqual(self.w.images.state_of(OWNER), "on")
        self.w.store.set_noimages(ALICE.account_id, False, self.w.chat.ref(DAVE.account_id))
        self.w.images.sync()
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "on")

    def test_until_the_flags_are_read_nobody_but_the_operator_may_post(self):
        w = ImageWorld()
        self.addCleanup(w.close)
        w.images.loaded = False
        self.assertEqual(w.images.state_of(ALICE.account_id), "wait")
        self.assertEqual(w.images.state_of(DAVE.account_id), "wait")
        self.assertEqual(w.images.state_of(OWNER), "on")
        w.images.sync()
        self.assertEqual(w.images.state_of(ALICE.account_id), "on")

    def test_a_hub_with_no_converter_has_no_images_and_says_nothing_of_them(self):
        w = ImageWorld(converter=False)
        self.addCleanup(w.close)
        self.assertFalse(w.images.on)
        alice = w.join(ALICE)
        for who in (ALICE, DAVE, OPERATOR):
            self.assertIsNone(w.images.state_of(who.account_id))
        self.assertNotIn("img", w.chat.you_of(ALICE))
        name = "0" * 32
        w.chat.on_text(alice, json.dumps({"t": "say", "text": "hi", "img": name}))
        self.assertEqual(alice.codes(), ["img"])

    def test_you_says_the_state_and_a_change_of_the_flag_tells_every_socket_of_the_account(self):
        second = self.w.join(ALICE)
        self.assertEqual(self.w.chat.welcome(self.alice)["you"]["img"], "on")
        self.assertEqual(self.w.chat.welcome(self.bob)["you"]["img"], "role")
        self.assertEqual(self.w.chat.welcome(self.reader)["you"], None)
        self.w.command(self.dave, "/noimages alice")
        for socket in (self.alice, second):
            self.assertEqual(socket.kinds("you")[-1]["you"]["img"], "off")
        self.assertEqual(self.bob.kinds("you"), [], "nobody else is told")
        self.w.command(self.dave, "/images alice")
        self.assertEqual(self.alice.kinds("you")[-1]["you"]["img"], "on")
        self.w.roles.unknown.add(ALICE.account_id)
        self.assertEqual(self.w.chat.you_of(ALICE)["img"], "wait")

    def test_a_flag_set_by_another_process_is_read_by_the_clock_and_the_account_is_told(self):
        other = storemod.Store(self.w.db, clock=self.w.wall)
        self.addCleanup(other.close)
        other.set_noimages(ALICE.account_id, True, "terminal")
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "on", "not read yet")
        self.w.chat.images_changed(self.w.images.sync())
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "off")
        self.assertEqual(self.alice.kinds("you")[-1]["you"]["img"], "off")


class TheLine(WorldTest):
    def test_an_image_line_goes_to_everybody_with_the_image_and_nothing_else_of_it(self):
        name = self.w.pending(ALICE, "png")
        self.w.say_image(self.alice, "the best spot", name)
        for peer in (self.bob, self.reader, self.alice):
            line = peer.kinds("line")[-1]
            self.assertEqual(line["text"], "the best spot")
            self.assertEqual(line["img"], {"n": name, "x": "png", "w": 800, "h": 600, "tw": 360, "th": 270})
            self.assertEqual(set(line), {"t", "id", "at", "who", "text", "mod", "k", "img"})

    def test_it_is_in_the_history_of_a_newcomer_and_after_a_restart_and_a_line_without_an_image_has_no_img(self):
        name = self.w.pending(ALICE)
        self.w.say_image(self.alice, "", name)
        self.w.clock.now += 3
        self.w.say(self.bob, "plain words")
        history = self.w.chat.welcome(self.reader)["history"]
        self.assertEqual([h["text"] for h in history], ["", "plain words"])
        self.assertEqual(history[0]["img"]["n"], name)
        self.assertNotIn("img", history[1])
        again = chatmod.Chat(self.w.store, self.w.roles, LIMITS, self.w.clock, True)
        self.assertEqual([again.line_of(l).get("img", {}).get("n") for l in again.ring], [name, None])

    def test_an_animated_image_says_so_and_a_still_one_does_not(self):
        still, moving = self.w.pending(ALICE, "png"), self.w.pending(ALICE, "gif")
        self.w.say_image(self.alice, "", still)
        self.w.say_image(self.alice, "", moving)
        lines = self.reader.kinds("line")
        self.assertNotIn("a", lines[0]["img"])
        self.assertEqual((lines[1]["img"]["x"], lines[1]["img"]["a"]), ("gif", 1))

    def test_a_caption_is_optional_and_has_every_rule_of_a_line(self):
        for text in ("", None):
            self.w.clock.now += 3
            self.w.chat.on_text(self.alice, json.dumps({"t": "say", "text": text, "img": self.w.pending(ALICE)}))
        self.assertEqual([l["text"] for l in self.reader.kinds("line")], ["", ""])
        for text, code in (("a <b> caption", "bad"), ("x" * 1001, "long"), (7, "bad")):
            name = self.w.pending(ALICE)
            self.w.say_image(self.alice, text, name)
            self.assertEqual(self.alice.codes()[-1], code, text)
            self.assertEqual(len(self.reader.kinds("line")), 2, "nothing was posted")
        self.assertEqual(self.w.store.pending_images(ALICE.account_id), 3, "a refused line does not use the upload up")
        self.w.clock.now += 3
        self.w.chat.on_text(self.alice, json.dumps({"t": "say", "img": self.w.pending(ALICE)}))
        self.assertEqual(len(self.reader.kinds("line")), 3, "no `text` at all is no caption")
        self.w.clock.now += 3
        self.w.chat.on_text(self.alice, json.dumps({"t": "say", "text": "", "img": None}))
        self.assertEqual(self.alice.codes()[-1], "empty", "no image and no words is the empty line that it always was")

    def test_a_person_who_may_not_gets_img_and_nothing_is_posted_and_nothing_is_said_of_why(self):
        name = self.w.pending(BOB)
        self.w.say_image(self.bob, "hello", name)
        self.assertEqual(self.bob.codes(), ["img"])
        self.assertEqual(self.reader.kinds("line"), [])
        self.assertEqual(self.bob.strikes, 1, "a mistake of the socket, as every err is")
        self.w.roles.unknown.add(ALICE.account_id)
        self.w.say_image(self.alice, "hello", self.w.pending(ALICE))
        self.assertEqual(self.alice.codes(), ["img"], "Discord has not answered: no")
        self.w.store.set_noimages(CAROL.account_id, True, "terminal")
        self.w.images.sync()
        self.w.say_image(self.carol, "hello", self.w.pending(CAROL))
        self.assertEqual(self.carol.codes(), ["img"], "a moderator with the flag")
        self.assertEqual(self.reader.kinds("line"), [])

    def test_the_upload_is_the_accounts_own_once_and_for_ten_minutes(self):
        mine = self.w.pending(ALICE)
        self.w.say_image(self.bob, "stolen", mine)                          # (bob may not post at all: img)
        self.w.say_image(self.dave, "stolen", mine)                          # (dave may, but it is not his)
        self.assertEqual((self.bob.codes(), self.dave.codes()), (["img"], ["imgid"]))
        self.assertEqual(self.reader.kinds("line"), [])
        self.w.say_image(self.alice, "mine", mine)
        self.assertEqual(len(self.reader.kinds("line")), 1)
        self.w.clock.now += 31                                                # (the same id twice within 30 seconds is a duplicate of the line: `rate`; later it is an id that is used)
        self.w.say_image(self.alice, "again", mine)
        self.assertEqual(self.alice.codes(), ["imgid"], "once")
        for name in ("0" * 32, self.w.images.new_name()):
            self.w.say_image(self.alice, "nothing", name)
        self.assertEqual(self.alice.codes(), ["imgid"] * 3, "an id that is nobody's")
        old = self.w.pending(ALICE)
        self.w.wall.now += 601
        self.w.say_image(self.alice, "late", old)
        self.assertEqual(self.alice.codes()[-1], "imgid", "ten minutes")
        young = self.w.pending(ALICE)
        self.w.wall.now += 599
        self.w.say_image(self.alice, "just in time", young)
        self.assertEqual(len(self.reader.kinds("line")), 2)
        for bad in ("short", "G" * 32, "A" * 32, 5, ["0" * 32], {"a": 1}):
            self.w.say_image(self.alice, "x", bad)
            self.assertEqual(self.alice.codes()[-1], "bad", repr(bad))

    def test_a_banned_muted_or_young_account_posts_no_image(self):
        self.w.store.ban(ALICE.account_id, "terminal")
        self.w.say_image(self.alice, "x", self.w.pending(ALICE))
        self.w.store.unban(ALICE.account_id)
        self.w.store.mute(DAVE.account_id, 5, "terminal")
        self.w.say_image(self.dave, "x", self.w.pending(DAVE))
        young = self.w.join(YOUNG)
        self.w.roles.images.add(YOUNG.account_id)
        self.w.say_image(young, "x", self.w.pending(YOUNG))
        self.assertEqual((self.alice.codes(), self.dave.codes(), young.codes()), (["banned"], ["muted"], ["young"]))
        self.assertEqual(self.reader.kinds("line"), [])

    def test_an_image_has_a_caption_and_never_a_command(self):
        name = self.w.pending(DAVE)
        self.w.say_image(self.dave, "/mute bob 5", name)
        self.assertEqual(self.dave.codes(), ["bad"])
        self.assertEqual(self.w.store.muted_until(BOB.account_id), 0, "nothing was done")
        self.w.say_image(self.dave, "//shrug", name)
        self.assertEqual(self.reader.kinds("line")[-1]["text"], "/shrug", "a slash that is said as a line is a caption")

    def test_two_images_with_no_words_are_no_duplicates_of_each_other_and_a_reader_cannot_post_one(self):
        for _ in range(2):
            self.w.say_image(self.alice, "", self.w.pending(ALICE))
        self.assertEqual(len(self.reader.kinds("line")), 2)
        self.w.chat.on_text(self.reader, json.dumps({"t": "say", "text": "x", "img": self.w.pending(ALICE)}))
        self.assertEqual(self.reader.codes(), ["login"])

    def test_a_forged_img_in_any_other_frame_changes_nothing(self):
        name = self.w.pending(ALICE)
        for frame in ({"t": "ping", "img": name}, {"t": "here", "at": "look", "img": name}, {"t": "who", "img": name}):
            self.w.chat.on_text(self.alice, json.dumps(frame))
        self.assertEqual(self.reader.kinds("line"), [])
        self.assertEqual(self.w.store.pending_images(ALICE.account_id), 1)

    def test_the_images_of_a_person_who_is_ignored_are_not_sent_to_the_one_who_ignores(self):
        self.w.command(self.bob, "/ignore alice")                           # (alice has said nothing yet: the name is found among the open sockets)
        self.w.say_image(self.alice, "", self.w.pending(ALICE))
        self.assertEqual(self.bob.kinds("line"), [])
        self.assertEqual(len(self.carol.kinds("line")), 1)


class TheDeletes(WorldTest):
    def post(self, who, peer, text="words", x="jpg"):
        name = self.w.pending(who, x)
        self.w.say_image(peer, text, name)
        return name, peer.kinds("line")[-1]["id"]

    def gone(self, name, x="jpg"):
        return not self.w.exists(name, x) and not self.w.exists(name, x, thumb=True) and name not in self.w.store.image_names()

    def test_the_files_of_a_line_are_there_while_it_is_and_the_row_is_the_lines(self):
        name, line = self.post(ALICE, self.alice)
        self.assertTrue(self.w.exists(name) and self.w.exists(name, thumb=True))
        self.assertEqual(self.w.store.image_of_line(line), Image(name, "jpg", 800, 600, 360, 270, False))
        self.assertEqual(self.w.images.sweep(), 0, "nothing is to go")
        self.assertTrue(self.w.exists(name))

    def test_a_moderator_deletes_the_line_and_the_files_go_at_once_and_the_log_says_it_was_an_image(self):
        name, line = self.post(ALICE, self.alice)
        self.w.command(self.carol, "/del %d" % line)
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": line}])
        self.assertTrue(self.gone(name))
        rows = self.w.store.actions(0, 10)
        self.assertEqual([(r.action, r.detail) for r in rows], [("del_img", str(line))])
        self.assertEqual((rows[0].actor, rows[0].target), (self.w.chat.ref(CAROL.account_id), self.w.chat.ref(ALICE.account_id)))
        self.assertEqual(self.carol.kinds("ok")[-1], {"t": "ok", "c": "del"})
        _, plain = self.post(ALICE, self.alice, "plain")
        self.w.store.delete_line(plain, "x")
        self.assertEqual(self.w.chat.store.image_of_line(plain), None)

    def test_with_posting_switched_off_the_files_of_a_deleted_line_still_go(self):
        """The converter's address is empty (images are off for everybody: no upload, no `img` in `you`), the folder is still there: a moderator's /del and a poster's own delete take the files away, and the hourly
        look deletes a file that no row has. It is when somebody has turned images off because of an incident that the files must be deletable."""
        w = ImageWorld(converter=False)
        self.addCleanup(w.close)
        self.assertFalse(w.images.on)
        self.assertTrue(w.images.files)
        alice, carol = w.join(ALICE), w.join(CAROL)
        w.roles.roles[CAROL.account_id] = "mod"
        one, two = w.pending(ALICE), w.pending(ALICE, "png")
        for name in (one, two):
            w.store.add_line(ALICE.account_id, "alice", "see", 0, image=name)
        lines = [row.id for row in w.store.recent_lines(10)]
        w.command(carol, "/del %d" % lines[0])
        self.assertTrue(not w.exists(one) and not w.exists(one, thumb=True) and one not in w.store.image_names(), "a moderator's /del")
        w.chat.on_text(alice, json.dumps({"t": "delimg", "id": lines[1]}))
        self.assertEqual(alice.codes(), [], "the author's own delete is taken")
        self.assertTrue(not w.exists(two, "png") and not w.exists(two, "png", thumb=True) and two not in w.store.image_names(), "the author's delete")
        orphan = w.images.path(w.images.new_name(), "jpg")
        with open(orphan, "wb") as f:
            f.write(JPEG)
        os.utime(orphan, (0, 0))
        w.images.next_scan = float("-inf")
        w.images.on_tick(w.clock())
        self.assertFalse(os.path.exists(orphan), "the hourly look")
        self.assertEqual(w.files(), [])

    def test_a_folder_that_cannot_be_written_deletes_nothing_and_a_hub_with_no_folder_makes_none(self):
        with tempfile.TemporaryDirectory() as folder:
            gone = os.path.join(folder, "not-made")
            cfg = config.load({"ANTS_HUB_PUBLIC_URL": SITE, "ANTS_HUB_IMAGE_DIR": gone})
            w = ImageWorld(converter=False)
            self.addCleanup(w.close)
            images = imagesmod.Images(cfg, w.limits, w.store, w.roles, w.clock, w.wall, logmod.Log(io.StringIO()), w.chat.ref, lambda a: False, w.chat.key_of)
            self.assertEqual((images.files, images.on, os.path.exists(gone)), (False, False, False), "no converter: no folder is made, and there is none to delete from")

    def test_a_line_without_an_image_is_deleted_as_it_was_and_logged_as_del(self):
        self.w.say(self.alice, "just words")
        self.w.command(self.carol, "/del %d" % self.reader.kinds("line")[-1]["id"])
        self.assertEqual([r.action for r in self.w.store.actions(0, 10)], ["del"])

    def test_the_author_deletes_its_own_image_and_nobody_elses_and_only_an_image(self):
        name, line = self.post(ALICE, self.alice)
        self.w.chat.on_text(self.bob, json.dumps({"t": "delimg", "id": line}))
        self.assertEqual(self.bob.codes(), ["bad"], "not his")
        self.assertTrue(self.w.exists(name))
        self.w.say(self.alice, "only words")
        words = self.alice.kinds("line")[-1]["id"]
        self.w.chat.on_text(self.alice, json.dumps({"t": "delimg", "id": words}))
        self.assertEqual(self.alice.codes(), ["bad"], "a line with no image is not an image")
        self.w.chat.on_text(self.alice, json.dumps({"t": "delimg", "id": line}))
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": line}])
        self.assertTrue(self.gone(name))
        self.assertEqual(self.w.store.actions(0, 10), [], "a person's own line leaves no row of the moderators' log")
        self.w.chat.on_text(self.alice, json.dumps({"t": "delimg", "id": line}))
        self.assertEqual(self.alice.codes()[-1], "bad", "once")
        for frame in ({"t": "delimg"}, {"t": "delimg", "id": "5"}, {"t": "delimg", "id": True}, {"t": "delimg", "id": -1}, {"t": "delimg", "id": 5, "x": 1}, {"t": "delimg", "id": 2 ** 70}):
            self.w.chat.on_text(self.alice, json.dumps(frame))
            self.assertEqual(self.alice.codes()[-1], "bad", frame)
        self.w.chat.on_text(self.reader, json.dumps({"t": "delimg", "id": line}))
        self.assertEqual(self.reader.codes()[-1], "login")

    def test_the_author_may_delete_its_image_after_the_role_lapsed_and_while_banned_but_the_hub_asks_for_nothing(self):
        name, line = self.post(ALICE, self.alice)
        self.w.roles.images.discard(ALICE.account_id)
        self.w.roles.unknown.add(ALICE.account_id)
        self.w.chat.on_text(self.alice, json.dumps({"t": "delimg", "id": line}))
        self.assertTrue(self.gone(name), "it is the person's own line: the role is not asked")

    def test_a_ban_with_purge_takes_the_images_of_the_last_hour_with_the_lines(self):
        old, _ = self.post(ALICE, self.alice)
        self.w.wall.now += 2 * 3600
        fresh, _ = self.post(ALICE, self.alice)
        self.w.command(self.dave, "/ban alice purge")
        self.assertTrue(self.gone(fresh))
        self.assertTrue(self.w.exists(old), "an older line is not part of the purge")

    def test_forget_me_takes_every_image_of_the_person_and_the_uploads_that_wait(self):
        name, _ = self.post(ALICE, self.alice)
        other, _ = self.post(DAVE, self.dave)
        waiting = self.w.pending(ALICE)
        ids = self.w.store.forget(ALICE.account_id, 0)
        self.w.chat.drop_lines(ids)
        self.assertTrue(self.gone(name))
        self.assertTrue(self.gone(waiting), "an upload that waited is stale at once")
        self.assertTrue(self.w.exists(other), "somebody else's is not touched")
        self.assertEqual(self.w.store.pending_images(ALICE.account_id), 0)

    def test_a_line_that_is_past_its_seven_days_or_past_the_newest_200_takes_its_files_with_it(self):
        name, _ = self.post(ALICE, self.alice)
        self.w.wall.now += 7 * 86400 + 1
        self.w.store.purge()
        self.assertTrue(self.w.exists(name), "the row says nothing until the sweep reads it")
        self.assertEqual(self.w.images.sweep(), 1)
        self.assertTrue(self.gone(name))
        w = ImageWorld(limits=LIMITS)
        self.addCleanup(w.close)
        w.store.max_lines = 3
        alice = w.join(ALICE)
        names = []
        for _ in range(5):
            names.append(w.pending(ALICE))
            w.say_image(alice, "", names[-1])
        w.images.sweep()
        self.assertEqual([w.exists(n) for n in names], [False, False, True, True, True])

    def test_a_delete_that_could_not_remove_a_file_keeps_its_row_and_is_repaired_by_the_next_sweep(self):
        name, line = self.post(ALICE, self.alice)
        with mock.patch.object(imagesmod.Images, "unlink", staticmethod(lambda path: False)):
            self.w.command(self.carol, "/del %d" % line)
        self.assertTrue(self.w.exists(name), "the file is still there")
        self.assertIn(name, self.w.store.image_names(), "and so is its row: the sweep knows what to do")
        self.assertEqual(self.w.images.sweep(), 1)
        self.assertTrue(self.gone(name))

    def test_a_file_with_no_row_is_deleted_when_it_is_old_and_not_before_and_a_file_with_a_row_never(self):
        keep, _ = self.post(ALICE, self.alice)
        orphan = self.w.images.new_name()
        self.w.images.write(orphan, "png", PNG, JPEG)
        stranger = os.path.join(self.w.folder_images.name, "not-an-image.txt")
        open(stranger, "w").close()
        temporary = os.path.join(self.w.folder_images.name, orphan + ".png.tmp")
        open(temporary, "wb").close()
        self.assertEqual(self.w.images.scan(), 0, "young files are an upload that is being written")
        old = time.time() - 2 * self.w.limits.image_scan_age_seconds
        for entry in os.listdir(self.w.folder_images.name):
            os.utime(os.path.join(self.w.folder_images.name, entry), (old, old))
        self.assertEqual(self.w.images.scan(), 3, "the two files of the orphan and the temporary file")
        self.assertTrue(self.w.exists(keep) and os.path.exists(stranger), "a file with a row, and a file that is no image's, stay")

    def test_the_command_line_deleting_a_line_takes_its_files_when_the_hub_reads_the_deletion(self):
        name, line = self.post(ALICE, self.alice)
        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(admin.run(["--db", self.w.db, "del", str(line)], out, err), 0)
        self.assertEqual([(r.actor, r.action) for r in self.w.store.actions(0, 5)], [("terminal", "del_img")])
        self.assertTrue(self.w.exists(name))
        self.w.chat.sync_deletions()
        self.assertTrue(self.gone(name))

    def test_an_upload_that_no_line_took_is_deleted_after_ten_minutes_and_not_before(self):
        name = self.w.pending(ALICE)
        self.w.wall.now += 599
        self.assertEqual(self.w.images.sweep(), 0)
        self.w.wall.now += 2
        self.assertEqual(self.w.images.sweep(), 1)
        self.assertTrue(self.gone(name))

    def test_the_quota_of_files_and_bytes_is_counted_over_the_images_posted_and_the_ones_that_wait(self):
        limits = dataclasses.replace(LIMITS, image_files_max=3, image_bytes_max=10 ** 6)
        w = ImageWorld(limits=limits)
        self.addCleanup(w.close)
        for _ in range(2):
            w.pending(ALICE)
        self.assertTrue(w.images.room(10))
        w.pending(ALICE)
        self.assertFalse(w.images.room(10), "three of three")
        w2 = ImageWorld(limits=dataclasses.replace(LIMITS, image_files_max=100, image_bytes_max=1000))
        self.addCleanup(w2.close)
        w2.pending(ALICE, data=b"\xff\xd8\xff" + b"a" * 500 + b"\xff\xd9")
        self.assertTrue(w2.images.room(10))
        self.assertFalse(w2.images.room(900))


class TheFlag(WorldTest):
    def refs(self):
        return {r.action: (r.actor, r.target) for r in self.w.store.actions(0, 10)}

    def test_an_administrator_takes_posting_away_and_gives_it_back_and_the_log_has_references_only(self):
        self.w.command(self.dave, "/noimages alice")
        self.assertEqual(self.dave.kinds("ok")[-1], {"t": "ok", "c": "noimages"})
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "off")
        self.assertEqual(self.refs(), {"noimages": (self.w.chat.ref(DAVE.account_id), self.w.chat.ref(ALICE.account_id))})
        self.w.command(self.dave, "/images alice")
        self.assertEqual(self.dave.kinds("ok")[-1], {"t": "ok", "c": "images"})
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "on")
        for row in self.w.store.actions(0, 10):
            for field in (row.actor, row.target, row.detail):
                self.assertNotIn(ALICE.account_id, field)
                self.assertNotIn(DAVE.account_id, field)

    def test_a_moderator_may_not_and_the_answer_is_the_one_that_any_word_that_is_not_theirs_gets(self):
        self.w.command(self.carol, "/noimages alice")
        self.w.command(self.carol, "/zzz alice")
        self.w.command(self.carol, "/images alice")
        frames = [json.dumps(m, sort_keys=True) for m in self.carol.kinds("reply")]
        self.assertEqual(len(set(frames)), 1, "the same bytes")
        self.assertEqual(json.loads(frames[0]), {"c": "unknown", "r": "unknown", "t": "reply"})
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "on")
        self.w.command(self.dave, "/noimages alice")
        self.w.command(self.carol, "/images alice")
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "off", "a moderator cannot give posting back")
        self.assertEqual(json.loads(json.dumps(self.carol.kinds("reply")[-1], sort_keys=True)), {"c": "unknown", "r": "unknown", "t": "reply"})
        self.w.command(self.dave, "/images alice")
        self.assertEqual(self.w.images.state_of(ALICE.account_id), "on")
        self.w.command(self.alice, "/noimages bob")
        self.assertEqual(self.w.images.state_of(BOB.account_id), "role", "a person with no role: nothing either")

    def test_the_table_of_the_powers_an_administrator_acts_on_a_moderator_and_nobody_on_an_administrator_the_operator_or_oneself(self):
        self.w.command(self.dave, "/noimages carol")
        self.assertEqual(self.w.images.state_of(CAROL.account_id), "off")
        for aim in ("erin", "olive.owner", "dave"):
            self.w.command(self.dave, "/noimages " + aim)
            self.assertEqual(self.dave.codes()[-1], "power", aim)
        self.assertEqual({self.w.images.state_of(a.account_id) for a in (ERIN, OPERATOR, DAVE)}, {"on"})

    def test_what_is_there_already_or_is_not_there_is_bad_and_a_name_that_is_nobodys_is_who(self):
        self.w.command(self.dave, "/images alice")
        self.assertEqual(self.dave.codes(), ["bad"], "nothing to lift")
        self.w.command(self.dave, "/noimages alice")
        self.w.command(self.dave, "/noimages alice")
        self.assertEqual(self.dave.codes()[-1], "bad", "taken away already")
        self.w.command(self.dave, "/noimages nobody.at.all")
        self.assertEqual(self.dave.codes()[-1], "who")
        self.assertEqual(len(self.w.store.actions(0, 10)), 1, "only the one that was done")

    def test_the_two_words_are_in_the_commands_of_an_administrator_and_of_nobody_else(self):
        for peer, has in ((self.dave, True), (self.carol, False), (self.alice, False)):
            self.w.chat.commands.send_list(peer)
            words = {row["w"] for row in peer.kinds("cmds")[-1]["rows"]}
            self.assertEqual({"/noimages", "/images"} <= words, has)
        row = [r for r in self.dave.kinds("cmds")[-1]["rows"] if r["w"] == "/noimages"][0]
        self.assertEqual((row["a"], row["g"]), ("NAME", "Administrators"))
        self.assertLessEqual(len(row["d"]), 40)

    def test_the_flag_survives_a_restart_forget_me_and_a_lifting_by_reference(self):
        self.w.command(self.dave, "/noimages alice")
        ref = self.w.chat.ref(ALICE.account_id)
        again = storemod.Store(self.w.db, clock=self.w.wall)
        self.addCleanup(again.close)
        self.assertEqual(set(again.noimages_marks()), {ref})
        self.w.store.forget(ALICE.account_id, 0)
        self.assertEqual(set(self.w.store.noimages_marks()), {ref}, "Forget me does not lift it")
        self.assertEqual(list(self.w.store.noimages_marks()[ref])[1], self.w.chat.ref(DAVE.account_id))
        self.assertTrue(self.w.store.set_noimages_by_reference(ref, False, "terminal", note=("terminal", "images", ref, "")))
        self.assertIsNone(self.w.store.set_noimages_by_reference(ref, False, "terminal"), "nothing left to lift")

    def test_the_command_line_takes_away_and_gives_back_by_username_or_reference_and_logs_as_terminal(self):
        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(admin.run(["--db", self.w.db, "noimages", "alice"], out, err), 0)
        self.assertEqual(admin.run(["--db", self.w.db, "noimages", "alice"], out, err), 1)
        self.assertEqual(self.w.store.noimages_marks()[self.w.chat.ref(ALICE.account_id)][1], "terminal")
        printed = out.getvalue() + err.getvalue()
        self.assertIn(self.w.chat.ref(ALICE.account_id), printed)
        self.assertNotIn(ALICE.account_id, printed, "an id is never printed")
        self.w.store.forget(ALICE.account_id, 0)
        out2 = io.StringIO()
        self.assertEqual(admin.run(["--db", self.w.db, "images", self.w.chat.ref(ALICE.account_id)], out2, io.StringIO()), 0)
        self.assertEqual(self.w.store.noimages_marks(), {})
        self.assertEqual([(r.actor, r.action) for r in self.w.store.actions(0, 10)], [("terminal", "images"), ("terminal", "noimages")])
        status = io.StringIO()
        admin.run(["--db", self.w.db, "status"], status, io.StringIO())
        self.assertIn("images: 0 of 600 kept", status.getvalue())

    def test_the_flag_has_a_cap_and_the_oldest_go_first(self):
        self.w.store.max_noimages = 3
        for n in range(5):
            self.w.wall.now += 1
            self.w.store.set_noimages("90000000000000%d" % n, True, "terminal")
        self.assertEqual(len(self.w.store.noimages_marks()), 3)

    def test_an_id_is_in_no_table_of_the_images(self):
        name = self.w.pending(ALICE)
        self.w.say_image(self.alice, "words", name)
        self.w.command(self.dave, "/noimages alice")
        for table in ("images", "noimages", "modlog"):
            rows = self.w.store.db.execute("SELECT * FROM %s" % table).fetchall()
            self.assertTrue(rows)
            self.assertNotIn(ALICE.account_id, repr(rows), table)
            self.assertNotIn(DAVE.account_id, repr(rows), table)
        self.assertNotIn(ALICE.account_id, repr(self.w.files()))


class TheThirdRole(unittest.TestCase):
    def test_the_setting_is_an_id_that_is_not_the_other_two_and_is_never_printed(self):
        env = {"ANTS_HUB_DISCORD_BOT_TOKEN": "t" * 40, "ANTS_HUB_DISCORD_GUILD": "700000000000000001", "ANTS_HUB_ROLE_ADMIN": "700000000000000002", "ANTS_HUB_ROLE_MOD": "700000000000000003",
               "ANTS_HUB_ROLE_IMAGES": "700000000000000005"}
        cfg = config.load(env)
        self.assertEqual(cfg.role_images, "700000000000000005")
        self.assertNotIn("700000000000000005", repr(cfg) + str(cfg.summary()))
        self.assertEqual(cfg.summary()["role_images_configured"], 1)
        for name, value in (("ANTS_HUB_ROLE_IMAGES", "canary-role"), ("ANTS_HUB_ROLE_IMAGES", "0123"), ("ANTS_HUB_ROLE_IMAGES", "700000000000000002"), ("ANTS_HUB_ROLE_IMAGES", "700000000000000003")):
            with self.assertRaises(config.ConfigError) as caught:
                config.load(dict(env, **{name: value}))
            self.assertIn("ANTS_HUB_ROLE", str(caught.exception))
            self.assertNotIn(value, str(caught.exception))
        self.assertEqual(config.load({k: v for k, v in env.items() if k != "ANTS_HUB_ROLE_IMAGES"}).summary()["role_images_configured"], 0)

    def test_the_images_role_alone_is_enough_to_ask_discord(self):
        env = {"ANTS_HUB_DISCORD_BOT_TOKEN": "t" * 40, "ANTS_HUB_DISCORD_GUILD": "700000000000000001", "ANTS_HUB_ROLE_IMAGES": "700000000000000005"}
        self.assertTrue(config.load(env).roles_on)
        self.assertFalse(config.load(dict(env, ANTS_HUB_ROLE_IMAGES="")).roles_on)

    def test_the_converter_address_has_the_shape_of_the_game_servers_and_nothing_more(self):
        cfg = config.load({"ANTS_HUB_IMAGE_CONVERTER_URL": "http://ants-img:4050"})
        self.assertEqual(cfg.image_converter_url, "http://ants-img:4050")
        for bad in ("https://canary-host:4050", "http://canary-host", "http://user:pw@canary-host:4050", "http://canary-host:4050/x", "http://canary-host:70000", "canary-host:4050"):
            with self.assertRaises(config.ConfigError) as caught:
                config.load({"ANTS_HUB_IMAGE_CONVERTER_URL": bad})
            self.assertNotIn("canary-host", str(caught.exception))
        self.assertEqual(config.load({}).image_converter_url, "")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""The legal notice (run by ./run_tests.sh --fast and by the CI): the Legal page (web/legal.html, served at /legal), the same notice in the README's License section, and the link to it in the footers and in the Menu of the bar.

  - the page is the changelog page's look (it links /front/classic.css), loads nothing from another site, runs no script, is short, links the LICENSE and THIRD_PARTY_NOTICES.md of the repository
    (files that exist) and says nothing about what the site stores or for how long (that wording has its own home);
  - the five sentences of the notice are the same words on the page and in the README's License section (the owner approved this wording; a change to one is a change to the other), and what both say about
    the original game's data is the README's old wording;
  - every page of the site that has a footer (the front page and the game page) has the link "Legal" at the end of its footer links, opening in a new tab (a match that runs is not left); the pages with the bar at
    the top have no footer and reach the page from the bar's Menu, and the Legal page carries the credit line that their footers had;
  - the Dockerfile copies the page (the build stage stamps the version and the build into its bar) and nginx.conf sends /legal to it with a plain redirect, as it does /changelog.
"""
import glob
import html
import os
import re
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DISCORD = "https://discord.gg/WaWrXQnKPj"                                             # (the Discord button of the site bar)
GITHUB = "https://github.com/dchadd427/ants-cross-platform"
DB_IP = "https://db-ip.com/"                                                          # (the credit of the country list: IP to Country Lite, CC BY 4.0)
LEGAL_URL = "https://beta.playants.org/legal.html"

# The notice, as the owner approved it (the page's panels in order, the first sentence of the fourth; the README's first paragraph of "License")
NOTICE = (
    "This game is free to play and free to use.",
    'It is provided "as is", with no warranty of any kind, express or implied.',
    "Its authors are not liable for any damage, data loss or other problem from using it, including online play, the bots, the website and the servers.",
    "There is no promise that the online servers will be up: they may be slow, down, restarted or shut down at any time, with no notice.",
    "It is an unofficial fan remake of the 1998 game Ants, not affiliated with or endorsed by the authors or owners of the original game.",
)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def plain(text):
    """The words of a piece of HTML or Markdown: no tags, no links' addresses, entities and curly quotes as plain characters, one space between words."""
    text = re.sub(r"\[([^\]]+)\]\([^)\s]+\)", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = html.unescape(text).replace("“", '"').replace("”", '"').replace("’", "'").replace("**", "")
    return re.sub(r"\s+", " ", text).strip()


def section(markdown, heading):
    """The text under a ## heading of a Markdown file, up to the next one (the heading line itself is not in it)."""
    start = markdown.index("\n## %s\n" % heading) + len("\n## %s\n" % heading)
    end = markdown.find("\n## ", start)
    return markdown[start:end if end != -1 else len(markdown)]


def footer_links(page):
    """[(href, attributes after it, text)] of the links in the nav of the page's footer."""
    footer = page[page.index('<footer class="bar"'):]
    nav = footer[footer.index('<nav aria-label="Footer links">'):footer.index("</nav>")]
    return [(m.group(1), m.group(2), plain(m.group(3))) for m in re.finditer(r'<(?:a|button)\b[^>]*?\bhref="([^"]*)"([^>]*)>(.*?)</(?:a|button)>', nav, re.S)]


def addresses_that_leave_the_site(page):
    """What in a page would load something from, or lead to, another site than this one and the project's own repository: an address in href, src, srcset, action, poster or data that
    is not a path of this site ("/x", never "//host") or an anchor ("#x") or a link into the repository (the repository's address and then "/" or the end), and any @import or url() in a style."""
    found = []
    for name, value in re.findall(r'\b(href|src|srcset|action|poster|data)="([^"]*)"', page):
        for address in ([part.split()[0] for part in value.split(",") if part.strip()] if name == "srcset" else [value]):
            if address.startswith("/") and not address.startswith("//") or address.startswith("#") or address.startswith("@@SITE_ORIGIN@@/"):      # (the site's own address, which the build writes in for the placeholder)
                continue
            if address == GITHUB or address.startswith(GITHUB + "/") or address == DISCORD or (address == DB_IP and name == "href"):          # (the credit that the country list's licence asks for is a link, never something that loads)
                continue
            found.append(address)
    for style in re.findall(r"<style[^>]*>(.*?)</style>", page, re.S):
        found += ["@import" for _ in re.findall(r"@import\b", style)]
        found += ["url(" for _ in re.findall(r"\burl\(", style)]
    return found


def changelog_page():
    sys.path.insert(0, os.path.join(REPO, "tools"))
    try:
        import changelog_to_html
    finally:
        sys.path.pop(0)
    return changelog_to_html.convert(read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000")


def pages_with_a_footer():
    """{name: text} of every page of the site that has a footer bar: the front page and the game page (the pages with the bar at the top have none)."""
    pages = {}
    for path in sorted(glob.glob(os.path.join(REPO, "web", "*.html"))):
        name = os.path.basename(path)
        text = read("web", name)
        if name != "legal.html" and '<footer class="bar"' in text:
            pages[name] = text
    return pages


class TheLegalPage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "legal.html")
        cls.main = cls.page[cls.page.index("<main>"):cls.page.index("</main>")]
        cls.text = plain(cls.main)

    def test_it_is_a_page_in_the_changelog_pages_look(self):
        self.assertTrue(self.page.startswith("<!DOCTYPE html>"))
        self.assertRegex(self.page, r"<title>[^<]+ \u2014 Ants \(1998\)</title>")                      # (the form of every title: tests/scripts/test_web_seo.py reads them all)
        self.assertIn('<link rel="stylesheet" href="/front/classic.css">', self.page)
        self.assertIn('<div class="pt"><h1>Legal</h1><p>', self.page)
        self.assertIn('<link rel="stylesheet" href="/site_nav.css">', self.page)
        self.assertEqual(re.findall(r"<script[^>]*>", self.page), ['<script src="/site_nav.js">', '<script src="/update_notice.js">', '<script src="/analytics.js">'])               # (the site bar's Menu, the update notice and the counter: no other script)
        self.assertIn('<header class="sb" data-live="menu" ', self.page)
        self.assertEqual(re.findall(r'<nav aria-label="Pages">(.*?)</nav>', self.page, re.S)[0].count('aria-current'), 0)      # (Legal is not one of the bar's five: it is in the Menu)
        self.assertEqual(sorted(set(re.findall(r"@@\w+@@", self.page)) - {"@@SITE_ORIGIN@@"}), ["@@BUILD_DATE@@", "@@BUILD_ID@@", "@@GAME_VERSION@@"])      # (what the build fills in: the bar's three stamps, for the last line of the Menu; and the site's address, which the head names)
        for blank in re.findall(r"<a [^>]*target=\"_blank\"[^>]*>", self.page):
            self.assertIn('rel="noopener noreferrer"', blank)

    def test_it_loads_nothing_from_another_site(self):
        self.assertEqual(addresses_that_leave_the_site(self.page), [])

    def test_the_check_of_the_addresses_is_shown_able_to_fail(self):
        """Copies of the page with one address that leaves the site: each must be named by the check (a protocol-relative one and an @import included)."""
        mutants = {
            "a protocol-relative style sheet": ('<link rel="stylesheet" href="/front/classic.css">', '<link rel="stylesheet" href="/front/classic.css"><link rel="stylesheet" href="//cdn.example/x.css">', "//cdn.example/x.css"),
            "a protocol-relative image": ('<div class="pt">', '<img src="//cdn.example/logo.png"><div class="pt">', "//cdn.example/logo.png"),
            "an image of another site": ('<div class="pt">', '<img src="https://cdn.example/logo.png"><div class="pt">', "https://cdn.example/logo.png"),
            "a plain http address": ('<link rel="icon" type="image/png" href="/favicon.png?v=9ea2152a">', '<link rel="icon" type="image/png" href="http://cdn.example/f.png">', "http://cdn.example/f.png"),
            "an address that only starts like the repository's": ('href="https://github.com/dchadd427/ants-cross-platform/issues"', 'href="https://github.com/dchadd427/ants-cross-platform.evil.example/issues"', "ants-cross-platform.evil.example"),
            "an @import of a quoted address": ("main { margin: 26px 0 32px; }", '@import "https://cdn.example/x.css";\nmain { margin: 26px 0 32px; }', "@import"),
            "an @import url()": ("main { margin: 26px 0 32px; }", "@import url(//cdn.example/x.css);\nmain { margin: 26px 0 32px; }", "@import"),
            "a url() in the page's own rules": ("main { margin: 26px 0 32px; }", 'main { margin: 26px 0 32px; background: url("/front/clay.png"); }', "url("),
            "a srcset": ('<div class="pt">', '<img src="/front/clay.png" srcset="//cdn.example/l.png 2x"><div class="pt">', "//cdn.example/l.png"),
        }
        for name, (old, new, needle) in sorted(mutants.items()):
            self.assertGreaterEqual(self.page.count(old), 1, "%s: the mutation's anchor is in the page" % name)
            found = addresses_that_leave_the_site(self.page.replace(old, new, 1))
            self.assertTrue(found and any(needle in f for f in found), "%s: the check did not name it (%s)" % (name, found))

    def test_it_says_the_notice_in_the_words_that_the_owner_approved(self):
        for sentence in NOTICE:
            self.assertIn(sentence, self.text)
        self.assertIn("not legal advice", self.text)
        self.assertEqual(re.findall(r"<strong>(.*?)</strong>", self.main), ["no warranty of any kind", "not liable", "id", "username", "display name", "profile picture", "Show my Discord picture", "Log out", "Forget me", "no cookie", "The counter never writes down your IP address or your browser&rsquo;s identification.", "The web server that sends you the pages keeps its own technical log of requests", "hour", "one visit and its country, once in 30 minutes, and nothing else", "Forget me has nothing to remove here"])       # the two phrases that matter are in bold (and the words of the chat panel and of the counter's)

    def test_it_says_what_the_readme_says_of_the_original_game(self):
        for words in ("belong to their copyright holders", "does not own them and cannot license them", "open an issue", "it will be taken out"):
            self.assertIn(words.lower(), self.text.lower(), words)
            self.assertIn(words.lower(), plain(section(read("README.md"), "License")).lower(), words)
        for words in ("artwork", "animations", "sounds", "music", "maps"):
            self.assertIn(words, self.text)
        self.assertIn("the maps that players made", self.text)

    def without_the_chat_panel(self):
        start = self.main.index('<section class="panel" id="chat-login">')
        text = self.main[:start] + self.main[self.main.index("</section>", start) + len("</section>"):]
        start = text.index('<section class="panel" id="counting">')               # (the panel of the counter of the visits says what is counted, kept and for how long: its own test below)
        text = text[:start] + text[text.index("</section>", start) + len("</section>"):]
        start = text.index('<section class="panel" id="channels">')              # (the panel of the players' channels, protocol 26, says who can see an address and that the relay's log is short-lived: its own test below)
        return plain(text[:start] + text[text.index("</section>", start) + len("</section>"):])

    def test_the_panel_of_the_channels_says_who_can_see_an_address_and_that_off_is_the_default(self):
        start = self.main.index('<section class="panel" id="channels">')
        panel = plain(self.main[start:self.main.index("</section>", start)])
        for words in ("relay", "no player's address reaches another player", "Direct connection", "can see your IP address", "turn it off again", "at any time", "short-lived"):
            self.assertIn(words, panel)
        for word in ("STUN", "TURN", "WebRTC", "ICE"):
            self.assertNotIn(word, panel, "the page speaks plainly")

    def test_it_is_short_and_says_nothing_of_what_the_site_stores_or_for_how_long_apart_from_the_panel_of_the_chat(self):
        text = self.without_the_chat_panel()
        self.assertLessEqual(len(text.split()), 300)                                                           # (250 until the Discord link's panel, and the coffee button's, joined the five of the notice)
        for word in ("record", "recorded", "recordings", "stored", "kept", "retain", "retention", "cookie", "cookies", "privacy", "personal", "days", "names"):
            self.assertNotRegex(text.lower(), r"\b%s\b" % word, word)

    def test_the_numbers_of_the_pictures_paragraph_are_the_numbers_of_the_code(self):
        """A number that the page promises about the pictures is the code's own: the 30 days of a login, the 400 days that an `off` is remembered, the day that a browser may keep a picture, the five minutes of
        nginx's cache and the hour between two purges are read from where they are made, not written twice."""
        sys.path.insert(0, REPO)
        from hub import http as hubhttp, limits as hublimits
        from hub.store import PICTURE_DAYS
        start = self.main.index('<section class="panel" id="chat-login">')
        panel = plain(self.main[start:self.main.index("</section>", start)])
        limits = hublimits.Limits()
        self.assertIn("for at most %d days after the login that fetched it" % (limits.session_seconds // 86400), panel)
        self.assertIn("for %d days after your last login" % PICTURE_DAYS, panel)
        self.assertIn("deleted within the %s" % {3600.0: "hour"}[limits.purge_seconds], panel)
        age = dict(hubhttp.PICTURE_HEADERS)["Cache-Control"]
        self.assertIn("max-age=%d" % 86400, age)                                                                  # (a day: "may keep its own copy for up to a day")
        self.assertIn("for up to a day", panel)
        self.assertIn("up to five minutes", panel)
        self.assertRegex(read("docker", "nginx.conf"), r"proxy_cache ants_hub_pics;(?s:.*?)proxy_cache_valid 200 5m;")      # (the cache in front of the picture's address keeps an answer five minutes)
        self.assertIn("private", age)                                                                             # (and a proxy in front of the site keeps no copy)

    def test_the_panel_of_the_chat_says_what_is_kept_for_how_long_the_cookies_and_how_to_leave(self):
        start = self.main.index('<section class="panel" id="chat-login">')
        panel = plain(self.main[start:self.main.index("</section>", start)])
        for words in ("Discord id, username and display name", "four things only", "if you have set one, your profile picture", "shows the name that you pick and shows your picture", "for nothing else", "no email address and no list of friends or servers", "keeps no Discord access token", "kept for up to 7 days", "only while they are among the newest 200 lines", "with the id and the name that wrote them",
                      "Moderators also see your username next to a display name", "moderators", "deleted for good", "ban or a mute is kept by Discord id",
                      "__Host-ants_oauth", "10 minutes at most", "__Host-ants_hub", "30 days", "Log out", "Forget me", "deletes your chat lines", "removes your username, display name and the copy of your picture", "a ban or a mute, if there is one, stays, and so does the operator's hiding of your picture, kept as one entry under the same kind of code, with no id and no name",
                      "Reading the chat and playing need no login",
                      "a nickname of your own and, if you like, a tag of two to five letters", "keeps them with your Discord id and the time of each change", "held for you for 30 days", "the site keeps for 30 days a note of the login name that it signed for the game", "shown to everybody",
                      "a signature that is good for two minutes and holds your name and nothing of your account", "kept in that browser only; the site checks it once and keeps nothing of it",
                      "with your nickname and tag, the nicknames held for you and the note of your login name",
                      "fetches one small copy of your Discord profile picture from Discord and keeps it on its own server", "it may ask Discord again, every few minutes, whether your picture changed", "shown to everyone who opens the front page, logged in or not, in the round image next to your name",
                      "unless you switch off Show my Discord picture in the menu of your name or the site's operator hides it", "served by this site under a random address that is new every time the site fetches your picture and holds nothing of your Discord id",
                      "other people's browsers do not contact Discord for it", "keeps the copy only while you have a login", "Switching it off, logging out of your last login, or Forget me deletes the copy from the server at once, and so does a kick or a ban",
                      "when your last login runs out the copy stops being served at once", "Its address can still answer for up to five minutes", "may keep its own copy for up to a day",
                      "Your choice to switch the picture off is remembered, by a code made from your Discord id with a secret key and not by the id", "Forget me deletes that too",
                      "If you change your picture at Discord, the site shows the new one within a few minutes for members of the project's Discord server and otherwise within about a day, and at your next login at the latest", "When the operator hides a picture on the server, that is written down in the same way",
                      "hold a moderator or administrator role in the project's Discord server", "the site asks Discord, with the project's bot", "The answers are kept in memory only", "writes down who did it, to which account, what it was and when",
                      "made from the Discord id with a secret key", "so the log itself does not hold the id", "kept for 90 days, at most 10,000 entries", "read only by the operator on the server", "not shown on the site",
                      "A kick or a ban is also shown to everyone in the chat as a line with the person's name, without the moderator's", "Forget me does not remove these entries, nor the line that said you were kicked or banned; that goes with the other lines within 7 days", "The site also asks Discord about any account that a moderator acts on, in the chat or not",
                      "the name you play under", "Games and Players lists", "the match history, which keeps results and names for good, and into the replay files", "for up to 90 days on the site, and for ever in a file that someone has downloaded", "Forget me does not remove it", "whether they are looking around, waiting in a room or playing",
                      "The voice button in the chat uses your browser's speech recognition", "Your browser may send the audio to its maker's speech service (Chrome, Edge and Safari do)", "this site receives no audio and keeps nothing of it",
                      "A heart on a replay (a like) is kept as one entry that holds the match and a code made from your Discord id with a secret key, the same kind of code as in the log above", "it holds no Discord id, no name, no address and no time",
                      "Nobody can see who liked a match", "tells you only which hearts are yours", "Forget me removes your likes", "How often a replay was played is kept as a number alone: nothing about who played it"):
            self.assertIn(words, panel, words)
        self.assertLessEqual(len(panel.split()), 2100)                                                           # (230 until the display name: the third thing that the site gets, the name that is shown, and what moderators see; 252 words, then 90 more for the name that is played under and the Players list; 351 words, then: the newest 200 lines, 9 words more; raised from 352 to 390 on purpose for the voice button's sentence, 35 words: who hears the voice and that the site gets no audio, 386 words, then raised from 390 to 590 on purpose for the two sentences of the roles and of the moderators' log, which the people in the chat are told about: who may moderate and how it is asked of Discord, what is written down about an action and for how long, that the log holds a code and not the id, and that Forget me does not remove it, 185 words, 571; then raised from 590 to 620 on purpose for two clauses: that Forget me does not remove the line that said a person was kicked or banned, and that Discord is asked about an account when a moderator acts on it, in the chat or not, 37 words, 608 now) (raised from 620 to 930 on purpose for the pictures: the fourth thing that the site gets, the paragraph about the small copy of the Discord picture (what is fetched and when, who sees it, how long it is kept and what ends it, the cache, the switch and the operator's hiding), and what Forget me removes of it and what stays; raised from 930 to 1000 on purpose for the refresh of the picture while logged in: that the site asks Discord again every few minutes, through the bot, or once a day for the public profile of an account that is not in the server, and when the new picture shows; then raised from 1100 to 1350 on purpose for the paragraph of the chat's images, 190 words: who may post, what is kept and for how long, what is left out, who can open a picture and how to report one; 1750 with the paragraphs of the private messages, the reports of them the follows, 1750; raised from 1750 to 1900 on purpose for the likes and plays of replays, 96 words: what a like keeps, that nobody sees who liked, that Forget me removes the likes, and that a play is a number alone, 1,834 words; raised from 1900 to 2100 on purpose for the paragraph of the nickname and the tag, 135 words: what is kept of them and of a login name's note, how long a nickname that is given up is held, what is public, the signature that the game server is given, a guest's name, and that Forget me removes them)
        self.assertEqual(re.findall(r"<strong>(.*?)</strong>", self.main[start:self.main.index("</section>", start)]), ["id", "username", "display name", "profile picture", "Show my Discord picture", "Log out", "Forget me"])
        self.assertNotIn("two things only", panel)
        self.assertNotIn("three things only", panel)
        self.assertNotIn("no avatar", panel)
        self.assertNotRegex(self.main[start:self.main.index("</section>", start)], r"href=|src=|<script|<img")        # (the panel loads nothing and links nothing)

    def test_the_paragraph_of_the_images_says_what_is_kept_and_its_numbers_are_the_numbers_of_the_code(self):
        """The sizes and the revalidation that the page promises about the chat's images are the code's own, read from where they are made: the converter's two sides and the nginx header of the picture's door."""
        start = self.main.index('<section class="panel" id="chat-login">')
        panel = plain(self.main[start:self.main.index("</section>", start)])
        converter = read("imgconv", "convert.py")
        side, animated = int(re.search(r"^MAX_SIDE = (\d+)", converter, re.M).group(1)), int(re.search(r"^ANIM_SIDE = (\d+)", converter, re.M).group(1))
        self.assertIn("at most %s pixels on a side" % format(side, ","), panel)
        self.assertIn("a GIF of at most %d pixels that keeps moving" % animated, panel)
        door = re.search(r"location ~ \"\^/hub/img/(?s:.*?)add_header Cache-Control \"(private, [a-z0-9=-]+)\"", read("docker", "nginx.conf"))
        self.assertEqual(door.group(1), "private, no-cache")                                                      # (a browser asks again before it shows a picture again: "asks the site again before it shows it again")
        self.assertIn("a browser that has loaded one asks the site again before it shows it again", panel)
        for words in ("Moderators, administrators and members of the project's Discord server who hold its images role may post images in the chat", "unless an administrator has taken that away from them", "stays after Forget me",
                      "never keeps your image as you sent it", "a new file from the pixels alone", "the location, camera and other details of a photo are left out", "kept only as long as the chat line that shows them",
                      "deleted for good", "when you or a moderator delete the image and when you use Forget me", "random addresses that hold nothing of your Discord id", "anyone who has an address can open the picture",
                      "does not look at what is in them", "tell a moderator on the project's Discord server", "taking image posting away or giving it back, are written down in the log below"):
            self.assertIn(words, panel, words)

    def test_the_panel_of_the_counter_says_what_is_counted_what_is_never_kept_and_its_numbers_are_the_numbers_of_the_code(self):
        """The counter's promises (docs/ANALYTICS.md) in the words of the page; the days and minutes that it names are the code's own, read from where they are made."""
        start = self.main.index('<section class="panel" id="counting">')
        end = self.main.index("</section>", start)
        panel, markup = plain(self.main[start:end]), self.main[start:end]
        for words in ("no cookie", "keeps nothing in your browser", "to nobody else", "which page you opened", "the name of the site that sent you (never the page, and only its main name, such as example.com)", "the width of your window", "your browser's language",
                      "how long the page took to load and how long the game took to start", "the class, the file and the line of an error", "cleaned of addresses and names", "search engines and monitors, are not counted",
                      "The counter never writes down your IP address or your browser's identification", "drops them", "only the country is kept", "only those names are kept",
                      "keeps its own technical log of requests", "the time, your IP address, the address of the page that was asked for", "your browser's identification", "Its log of errors can show the same IP address and the whole address that was asked for",
                      "the counter never reads them", "hold only the hour in which an event came and nothing finer", "cannot be matched to the lines of those logs by the second",
                      "made fresh every day", "midnight to midnight, Pacific time", "destroyed when the day is over", "a code of one day cannot be matched with a code of another",
                      "in its memory only", "never written to a disk", "a restart of the server forgets it",
                      "Do Not Track or Global Privacy Control", "one visit and its country", "and nothing else", "no code, no page, no device, no timing and no error",
                      "deleted for good", "hold no code and no address", "only when at least three visitors came from it that day", "kept without an end", "log in for the first time each day: a number, with no name", "Only the administrators of the site see the numbers",
                      "Nothing in these counts is linked to a Discord account, a name or a person", "Forget me has nothing to remove here", "IP to Country Lite by DB-IP", "licensed under CC BY 4.0"):
            self.assertIn(words, panel, words)
        self.assertIn('<a href="https://db-ip.com/" target="_blank" rel="noopener noreferrer">DB-IP</a>', markup, "the credit that the licence asks for is a link")
        sys.path.insert(0, REPO)
        try:
            from hub import analytics, analytics_store
        finally:
            sys.path.pop(0)
        self.assertIn("kept for %d days and then deleted" % analytics_store.RAW_DAYS, panel)
        self.assertIn("for %d days at most" % (analytics.RETURN_DAYS + 1), panel)
        self.assertIn("once in %d minutes" % (analytics.NT_SECONDS // 60), panel)
        self.assertIn("for those %d minutes" % (analytics.NT_SECONDS // 60), panel)

    def test_the_panel_says_the_web_servers_logs_are_deleted_after_the_days_that_the_image_keeps_them_and_names_no_other_end(self):
        """The panel's days are the script's own (docker/web-log.sh, run by the image: tests/scripts/test_web_log.py runs it); nginx writes the address in the log (the panel's words, in the test of the counter's panel, say so)."""
        start = self.main.index('<section class="panel" id="counting">')
        panel = plain(self.main[start:self.main.index("</section>", start)])
        days = re.findall(r"(?m)^KEEP_DAYS=(\d+)$", read("docker", "web-log.sh"))
        self.assertEqual(len(days), 1)
        self.assertIn("the site deletes both after %s days" % days[0], panel)
        self.assertNotIn("kept until the person who runs the server deletes it", panel)
        self.assertRegex(read("docker", "nginx.conf"), r"log_format ants_noquery '\$remote_addr \[\$time_local\]")      # (the address and the time are in the log: what the panel says)

    def test_it_links_the_licence_the_third_party_notices_and_the_issues_page_of_the_repository(self):
        hrefs = re.findall(r'<a href="([^"]*)"', self.main)
        self.assertEqual(hrefs, [GITHUB + "/issues", DB_IP, GITHUB + "/blob/main/LICENSE", GITHUB + "/blob/main/THIRD_PARTY_NOTICES.md", GITHUB + "/blob/main/LICENSE"])
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            self.assertTrue(os.path.exists(os.path.join(REPO, name)), name)

    def test_it_carries_the_credit_line_that_the_footer_had(self):
        code = self.main[self.main.index('<section class="panel" id="code">'):]
        self.assertIn("Ants 1998 Native Engine Remake &amp; Port &bull; Built with C++17, WebAssembly &amp; SDL2", code)


class TheReadme(unittest.TestCase):
    def test_the_license_section_opens_with_the_notice_and_links_the_page(self):
        text = plain(section(read("README.md"), "License"))
        first = text[:text.index("The source code and the documents written for this project")]
        self.assertTrue(first.startswith(" ".join(NOTICE)), first)
        self.assertIn("The same notice, in plain words, is the site's Legal page; it is not legal advice.", first)
        self.assertIn("(%s)" % LEGAL_URL, section(read("README.md"), "License"))

    def test_the_mit_and_the_original_games_paragraphs_are_still_there(self):
        text = section(read("README.md"), "License")
        for words in ("released under the **MIT License** ([`LICENSE`](LICENSE))", "**The original game is not part of that licence.**", "**The players' maps are not part of that licence either.**",
                      "[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)"):
            self.assertIn(words, text, words)


class TheFooterLinks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pages = pages_with_a_footer()

    def test_the_pages_with_a_footer_are_the_ones_this_test_knows(self):
        names = sorted(self.pages)
        self.assertIn("lobby.html", names)
        self.assertEqual(names, ["lobby.html"])                                                                    # (the game page has the bar: its Menu has the Legal link, tests/scripts/test_web_site_nav.py)

    def test_every_footer_ends_with_the_legal_link_in_a_new_tab(self):
        for name, page in sorted(self.pages.items()):
            links = footer_links(page)
            self.assertEqual(links[-1][0], "/legal", name)
            self.assertEqual(links[-1][2], "Legal", name)
            self.assertIn('target="_blank" rel="noopener noreferrer"', links[-1][1], name)
            self.assertEqual([l for l in links if l[0] == "/legal"], [links[-1]], "%s: one link to the page" % name)
            self.assertEqual(len([l for l in links if l[2] == "Feedback"]), 1, name)
            self.assertEqual([l[2] for l in links][-2:], ["Feedback", "Legal"], name)                                # (after Feedback, the link that the other pages end with)

    def test_the_link_is_the_same_markup_in_every_footer(self):
        for name, page in sorted(self.pages.items()):
            self.assertEqual(page.count('<a href="/legal" target="_blank" rel="noopener noreferrer">Legal</a>'), 1, name)

    def test_the_bar_of_the_game_page_has_it_in_its_menu_only(self):
        game = read("web", "shell.html")
        header = game[game.index('<header class="sb"'):game.index("</header>")]
        self.assertNotIn("legal", header.lower())
        self.assertIn("['Legal', '/legal']", read("web", "site_nav.js"))


class TheRouteAndTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_page_to_the_site(self):
        self.assertIn("\nCOPY --from=builder /src/legal.html /usr/share/nginx/html/legal.html\n", read("Dockerfile"))

    def test_nginx_serves_legal_and_changelog_at_their_clean_addresses_and_the_old_files_answer_200_not_a_redirect(self):
        conf = read("docker", "nginx.conf")
        for name in ("legal", "changelog"):
            block = re.search(r"\n    location = /%s \{([^}]*)\}" % name, conf)
            self.assertIsNotNone(block, "location = /" + name)
            self.assertEqual(block.group(1).split(), ["rewrite", "^", "/%s.html" % name, "last;"])      # (no add_header: a header here would hide the server's own; the file's address sends the page's headers)
        self.assertNotIn("    /legal.html /legal;\n", conf)                                         # (not in the map of old addresses: /legal used to redirect to the file, so the file answers 200 and names the clean address as canonical, tests/scripts/test_nginx_clean_urls.py)
        self.assertNotIn("    /changelog.html /changelog;\n", conf)


if __name__ == "__main__":
    unittest.main()

"""The rules word of a new Extended map and its badge in the real browser (tests/scripts/web_mapmaker_browser.py, part `rules`; docs/MAP_EDITOR.md, E4): kept in its own file, the main one is at the
line limit of the test files. The page runs with the real map core (WebAssembly). A map file that a person would choose is made in the page with a core of its own (the page's is out of reach) or
written to disk here, and given to the file box, so every case starts from bytes of a known rules word; what the page makes is read as Download hands it over (byte 14 of a version 9 file is the
rules word), never from the inside of the page.

  1280 x 800 (a window that is not tall): the first map is a Classic one and has no badge; New map makes an Extended map of 40, 90, 91 and 300 rows with rules 2 (a Classic map has none); the chip
             "Rules 2" is on the heading line of This map and not in the top bar, which is as tall as before; a map that is only opened and downloaded keeps its word; Copy... of a map of rules 0, 1 and
             2 (80 rows) and of rules 1 (120 rows) is a map of rules 2 with the same cells (only byte 14 differs), a map of rules 0 and 91 or 120 rows keeps its wall (rules 0, the same bytes), and the
             dialog has no new row (no checkbox, no word about rules); Start an Extended map from TINY gives rules 2; Save as a new map keeps a draft of the word the rule gives in the browser's
             database; Resize gives the map the word that the rule says, in the one step that Undo takes back (rows gained below the wall have none, a map that was walled keeps its wall);
             Make a Classic map from a map of long orders shows the short note and the two words of the picture;
  1280 x 720: the top bar is not taller with the badge and the Objects column (5 pixels to spare) does not overflow;
  1280 x 960 (a tall window): the badge is in the tag row beside "New version only" and the chip is not shown;
  390 x 844 (a phone): the badges are on a line of their own under the size, nothing scrolls sideways, and the Copy sheet of a map has no new row either;
  an old pair: a wrapper and a module that both say version 4 (two old files of a cache agree with each other, so the check of the versions lets them through) and a core without the call that New map
             and Copy make: the page says that the map code is out of date and offers a reload, and the same pair with the call lets it start."""
import json
import os
import tempfile
import time

wait_for = None
make_extended = None

GIVE_JS = r"""
window.__give = function (rows, columns, word, name) {      // a map file that a person chose: the core's blank Extended map with the rules word asked for, given to the file box
    window.__given = false;
    createLvlCore().then(function (m) {
        var core = LvlCore.wrap(m), made = core.blankExtended(rows, columns, 485, 6, 10).bytes;
        made[14] = word; made[15] = 0;
        var dt = new DataTransfer(), input = document.getElementById('file-in');
        dt.items.add(new File([made], name + '.lvl'));
        input.files = dt.files; input.dispatchEvent(new Event('change', { bubbles: true }));
        window.__given = true;
    });
};
window.__stored = function (name) {                          // a draft of the page's own database: [version, rules word, length], or null
    window.__draft = undefined;
    var q = indexedDB.open('ants-mapmaker', 1);
    q.onerror = function () { window.__draft = null; };
    q.onsuccess = function () {
        var g = q.result.transaction('drafts', 'readonly').objectStore('drafts').getAll();
        g.onsuccess = function () { var r = g.result.filter(function (d) { return d.name === name; })[0]; window.__draft = r ? [r.bytes[0], r.bytes[14] | (r.bytes[15] << 8), r.bytes.length] : null; };
        g.onerror = function () { window.__draft = null; };
    };
};
"""


def word_of(data):
    """the rules word of a file as bytes: 14 and 15 of a version 9 file; -1 for another kind"""
    return data[14] | (data[15] << 8) if len(data) > 15 and data[0] == 9 else -1


def with_word(data, word):
    return data[:14] + bytes([word & 255, word >> 8]) + data[16:]


def same_but_the_word(a, b):
    return len(a) == len(b) and a[:14] == b[:14] and a[16:] == b[16:]


def dialog_button(page, text):
    return page.ev("(function () { var b = [].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (x) { return x.textContent === %s; })[0]; if (b) b.click(); return !!b; })()" % json.dumps(text))


def chip(page):
    """the facts of the badge of long orders on the page: the chip of the heading line of This map (shown or not), the tag row, the top bar and the phone's size line"""
    return json.loads(page.ev("""JSON.stringify((function () {
        function shown(n) { return !!n && getComputedStyle(n).display !== 'none' && n.getBoundingClientRect().width > 0; }
        var chip = document.getElementById('side-rules'), side = document.querySelector('aside.side'), rows = document.getElementById('side-tags'), top = document.getElementById('top-tags'), line = document.getElementById('size-line');
        var inRow = rows.querySelector('.badge-r2'), nv = rows.querySelector('.badge-nv'), s = side.getBoundingClientRect(), c = chip.getBoundingClientRect();
        var lb = line.querySelectorAll('.badge-nv'), ls = line.querySelector('.sizebadges');
        return {
            chip: chip.textContent, chipShown: shown(chip), chipInside: c.width > 0 && c.left >= s.left - 1 && c.right <= s.right + 1,
            rowBadge: !!inRow, rowShown: shown(inRow), rowBeside: !!inRow && !!nv && Math.abs(inRow.getBoundingClientRect().top - nv.getBoundingClientRect().top) < 2,
            topHasIt: !!top.querySelector('.badge-r2'), topHeight: document.getElementById('top').getBoundingClientRect().height,
            lineBadges: [].map.call(lb, function (b) { return b.textContent; }), lineShown: shown(line) && lb.length > 0 && [].every.call(lb, shown), lineOwn: !!ls && ls.className.indexOf('two') >= 0,
            toolsOverflow: document.querySelector('.tools').scrollHeight - document.querySelector('.tools').clientHeight,
            sideScroll: document.documentElement.scrollWidth - window.innerWidth
        };
    })())"""))


def run_rules(page, tab, args, check, env, edges_bytes, edges_js):
    global wait_for, make_extended
    wait_for, make_extended = env["wait_for"], env["make_extended"]
    origin = "/".join(args.web.split("/")[:3])
    scratch = tempfile.mkdtemp(prefix="ants_mapmaker_rules.")
    tiny = os.path.join(env["REPO"], "Original-Ants", "Maps", "TINY.LVL")

    def start(width, height, mobile=False):
        tab.emulate(width, height, 2 if mobile else 1, mobile=mobile)
        tab.call("Storage.clearDataForOrigin", {"origin": origin, "storageTypes": "indexeddb"})          # (the work that the last page left is not this one's to pick up)
        check(page.open() and page.ready(), "%d x %d: the page starts" % (width, height))
        page.ev(edges_js + GIVE_JS)

    def download():
        return edges_bytes(page)["bytes"]

    def open_blank(rows, columns, word, name):
        page.ev("window.__give(%d, %d, %d, %s)" % (rows, columns, word, json.dumps(name)))
        ok = wait_for(lambda: page.ev("window.__given === true") and page.state()["name"] == name, 15)
        check(ok, "the file %s (rules %d, %d x %d) is opened" % (name, word, rows, columns))
        return ok

    def open_file(data, name):
        path = os.path.join(scratch, name + ".lvl")
        with open(path, "wb") as f:
            f.write(data)
        page.choose_file(path)
        ok = wait_for(lambda: page.state()["name"] == name, 15)
        check(ok, "the file %s (rules %d) is opened" % (name, word_of(data)))
        return ok

    print("[web map maker] the rules word of a new Extended map and its badge, 1280 x 800")
    start(1280, 800)
    first = chip(page)
    check(first["chip"] == "" and not first["rowBadge"] and not first["topHasIt"], "the first map is a Classic map: no badge anywhere (%s)" % first)
    check(open_blank(40, 40, 0, "Plain Extended"), "an Extended map of rules 0 is opened, the one without the badge")
    plain = chip(page)
    top_height = plain["topHeight"]
    check(plain["chip"] == "" and not plain["rowBadge"] and not plain["topHasIt"], "an Extended map of rules 0 has no badge (%s)" % plain)
    # New map: an Extended map has rules 2 at every size (the core's blank map has 0 up to 90 rows and 1 above: the page asks for long orders), a Classic map has no word
    for rows, columns in ((40, 40), (90, 60), (91, 20), (300, 60)):
        check(make_extended(page, rows, columns), "New map: an Extended map of %d x %d" % (rows, columns))
        data = download()
        check(word_of(data) == 2, "%d rows: its file has rules 2 (byte 14 is %d)" % (rows, word_of(data)))
        facts = chip(page)
        check(facts["chip"] == "Rules 2" and facts["chipShown"] and facts["chipInside"] and not facts["rowShown"], "%d rows: the chip \"Rules 2\" is on the heading line of This map, inside the panel (%s)" % (rows, facts))
        check(not facts["topHasIt"] and facts["topHeight"] == top_height, "%d rows: and not in the top bar, which is %s pixels tall, as for a map of rules 0 (%s)" % (rows, facts["topHeight"], top_height))
        if rows in (40, 300):
            page.shot(args.shots, "mapmaker-rules2-%d-desktop" % rows)
    page.press_act("new")
    wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
    page.type_into("#size-rows", "31")
    page.type_into("#size-columns", "31")
    dialog_button(page, "Make map")
    check(wait_for(lambda: page.state()["tags"][:1] == ["Classic map"], 8), "New map: a Classic map of 31 x 31")
    facts = chip(page)
    check(facts["chip"] == "" and not facts["chipShown"] and download()[0] == 8, "it has no badge and is a Classic file (%s)" % facts)

    # a map that is opened is not changed: opened and downloaded it keeps its word and its bytes, whatever the word
    for rows, word in ((80, 0), (80, 1), (80, 2), (120, 0), (120, 1)):
        if not open_blank(rows, 30, word, "Opened %d-%d" % (rows, word)):
            continue
        data = download()
        check(word_of(data) == word and len(data) > 1000, "opened and downloaded: %d rows, rules %d stay as they were (byte 14 is %d)" % (rows, word, word_of(data)))
        facts = chip(page)
        check((facts["chip"] == "Rules 2") == (word == 2) and not facts["topHasIt"], "the badge is there for rules 2 only: rules %d says %r" % (word, facts["chip"]))

    # Copy: the rule. The dialog has no new row; the copy is the core's with the word set, or the same bytes where the rule says no
    for rows, word, want, how in ((80, 0, 2, "core"), (80, 1, 2, "core"), (80, 2, 2, "same"), (120, 1, 2, "core"), (120, 0, 0, "same"), (91, 0, 0, "same"), (90, 0, 2, "core")):
        name = "Source %d-%d" % (rows, word)
        if not open_blank(rows, 30, word, name):
            continue
        source = download()
        page.press_act("copy")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg .srccard') && !!document.querySelector('.dlg-foot .btn')"), 20), "%s: the Copy dialog opens" % name)
        seen = json.loads(page.ev("JSON.stringify({boxes: document.querySelectorAll('.dlg input[type=checkbox]').length, radios: document.querySelectorAll('.dlg input[type=radio]').length, text: document.querySelector('.dlg').textContent, "
                                  "fits: document.querySelector('.dlg').scrollHeight <= document.querySelector('.dlg').clientHeight + 1, badge: !!document.querySelector('.dlg .srccard .badge-r2')})"))
        said = seen["text"].replace("Rules 2", "")                       # (the badge on the card of the map is the one place)
        check(seen["boxes"] == 0 and seen["radios"] == 2 and "ong orders" not in said and "ules" not in said, "%s: no new row in the dialog: no checkbox, two kinds, no word about rules" % name)
        check(seen["fits"], "%s: the dialog needs no scrollbar of its own at this size" % name)
        check(seen["badge"] == (word == 2), "%s: the card of the map has the badge of long orders for rules 2 only" % name)
        if (rows, word) in ((80, 2), (120, 0)):
            page.shot(args.shots, "mapmaker-rules2-copy-%d-%d" % (rows, word))
        dialog_button(page, "Make new map")
        check(wait_for(lambda: page.state()["name"] == name + " 2", 10), "%s: the copy takes the screen" % name)
        copy = download()
        check(word_of(copy) == want, "%s: the copy has rules %d (byte 14 is %d)" % (name, want, word_of(copy)))
        if how == "same":
            check(copy == source, "%s: the same bytes as the map it was made from" % name)
        else:
            check(same_but_the_word(copy, source) and copy[14:16] == b"\x02\x00", "%s: the same cells, only the rules word differs" % name)
        check(chip(page)["chip"] == ("Rules 2" if want == 2 else ""), "%s: the badge follows the copy" % name)

    # Save as a new map: the draft that the browser keeps has the word the rule gives, and it takes the screen
    for rows, word, want in ((60, 0, 2), (120, 0, 0), (60, 1, 2), (60, 2, 2)):
        name = "Draft %d-%d" % (rows, word)
        if not open_blank(rows, 30, word, name):
            continue
        page.press_act("save")
        check(wait_for(lambda: page.ev("!!document.querySelector('.dlg input[type=text]')"), 15), "%s: Save as a new map opens" % name)
        dialog_button(page, "Save as a new map")
        check(wait_for(lambda: page.state()["name"] == name + " 2", 15), "%s: the saved map takes the screen as %s 2" % (name, name))
        page.ev("window.__stored(%s)" % json.dumps(name + " 2"))
        wait_for(lambda: page.ev("window.__draft !== undefined"), 8, 0.1)
        stored = page.ev("window.__draft")
        check(stored is not None and stored[0] == 9 and stored[1] == want and stored[2] > 1000, "%s: the draft in the browser has rules %d (%s)" % (name, want, stored))
        check(word_of(download()) == want, "%s: and so has the map on the screen" % name)

    # Start an Extended map from a Classic map (TINY): rules 2; the base of the Resize cases, a map that plays, so that Download can hand it over after it is changed
    page.choose_file(tiny)
    check(wait_for(lambda: page.state()["name"] == "TINY", 15), "the Classic file TINY is opened")
    page.press_act("convert")
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg')"), 10) and dialog_button(page, "Make the new Extended map"), "Start an Extended map from this one")
    check(wait_for(lambda: page.state()["name"] == "TINY Extended", 15), "the Extended map takes the screen")
    base = download()
    check(word_of(base) == 2 and base[0] == 9 and chip(page)["chip"] == "Rules 2", "TINY as an Extended map has rules 2 and the badge (byte 14 is %d)" % word_of(base))

    # Resize: the word goes with the new size in one step, and Undo gives both back
    def resize(rows, columns):
        page.press_act("resize")
        wait_for(lambda: page.ev("!!document.getElementById('resize-rows')"))
        page.type_into("#resize-rows", str(rows))
        page.type_into("#resize-columns", str(columns))
        page.ev("document.querySelector('.dlg-foot .btn:last-child').click()")
        if rows < int(page.state()["rows"]) or columns < int(page.state()["columns"]):
            check(wait_for(lambda: page.ev("!!document.getElementById('crop-use') && !document.getElementById('crop').hidden"), 20), "a smaller size is asked through the crop screen")
            page.ev("document.getElementById('crop-use').click()")
        return wait_for(lambda: page.state()["rows"] == str(rows), 10)

    tall = None
    for word, to_rows, want in ((0, 60, 2), (0, 120, 2), (1, 60, 2), (2, 60, 2)):
        name = "Grow %d-%d" % (to_rows, word)
        if not open_file(with_word(base, word), name):
            continue
        check(resize(to_rows, 40), "%s: resized to %d x 40" % (name, to_rows))
        data = download()
        check(word_of(data) == want, "%s: from rules %d to %d rows: rules %d (byte 14 is %d)" % (name, word, to_rows, want, word_of(data)))
        check(chip(page)["chip"] == "Rules 2", "%s: and the badge is there" % name)
        if (word, to_rows) == (0, 120):
            tall = data
        page.ev("document.getElementById('undo-btn').click()")
        check(wait_for(lambda: page.state()["rows"] == "31", 5) and word_of(download()) == word, "%s: Undo gives the size and the word (%d) back in one step" % (name, word))
    # a map of rules 0 and 120 rows (it has its wall): a Resize keeps the wall while the map stays taller than 90 rows, and has none when it comes down
    if tall is not None:
        for to_rows, want in ((130, 0), (100, 0), (91, 0), (90, 2), (60, 2)):
            name = "Walled %d" % to_rows
            if not open_file(with_word(tall, 0), name):
                continue
            check(resize(to_rows, 40), "%s: the map of rules 0 and 120 rows is resized to %d x 40" % (name, to_rows))
            check(word_of(download()) == want, "%s: rules %d (the wall is %s)" % (name, want, "kept" if want == 0 else "gone"))
            check(chip(page)["chip"] == ("Rules 2" if want == 2 else ""), "%s: and the badge says so" % name)

    # Make a Classic map from a map of long orders: the short note and the two words of the picture
    check(make_extended(page, 40, 40), "an Extended map of 40 x 40 (long orders)")
    download()                                                           # (a map that was saved is left without a question)
    page.press_act("convert")
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg .cboxes')"), 10), "Make a Classic map from this one opens its dialog")
    shown = json.loads(page.ev("JSON.stringify({title: document.querySelector('.dlg h2').textContent, boxes: [].map.call(document.querySelectorAll('.dlg .cbox'), function (b) { return b.textContent; }), notes: [].map.call(document.querySelectorAll('.dlg .note'), function (n) { return n.textContent; })})"))
    check(shown["title"] == "Make a Classic map from this one?", "the title: %s" % shown["title"])
    check("How the map plays, except long orders." in shown["boxes"][0] and "“New version only” and “Rules 2” badges go away." in shown["boxes"][1], "the lines of the two columns: %s" % shown["boxes"])
    check(shown["notes"] == ["iThe map has long orders (rules 2); a Classic map has the original's rules only, so a long path order can fail there."], "the short note: %s" % shown["notes"])
    page.shot(args.shots, "mapmaker-rules2-classic-note")
    dialog_button(page, "Make the new Classic map")
    check(wait_for(lambda: page.state()["tags"][:1] == ["Classic map"], 10) and chip(page)["chip"] == "" and download()[0] == 8, "the Classic map has no badge and no rules word")

    print("[web map maker] the badge at 1280 x 720: the top bar is not taller and the Objects column does not overflow")
    start(1280, 720)
    check(open_blank(40, 40, 0, "Plain Extended"), "an Extended map of rules 0 is opened, the one without the badge")
    plain = chip(page)
    check(make_extended(page, 140, 60), "an Extended map (long orders)")
    facts = chip(page)
    check(facts["chip"] == "Rules 2" and facts["chipShown"] and facts["chipInside"], "1280 x 720: the chip is on the heading line (%s)" % facts)
    check(facts["topHeight"] == plain["topHeight"] and not facts["topHasIt"], "the top bar is %s pixels tall, as for a map of rules 0 (%s)" % (facts["topHeight"], plain["topHeight"]))
    check(plain["toolsOverflow"] <= 0, "(and the Objects column of the map without the badge does not overflow either: %s)" % plain["toolsOverflow"])
    check(facts["toolsOverflow"] <= 0 and facts["sideScroll"] <= 0, "the Objects column does not overflow (%s pixels over) and the page does not scroll sideways" % facts["toolsOverflow"])
    page.shot(args.shots, "mapmaker-rules2-1280x720")

    print("[web map maker] the badge in a tall window, 1280 x 960")
    start(1280, 960)
    check(make_extended(page, 140, 60), "an Extended map (long orders)")
    facts = chip(page)
    check(facts["rowShown"] and facts["rowBeside"] and not facts["chipShown"], "the badge is in the tag row, beside \"New version only\", and the chip is not shown (%s)" % facts)
    check(not facts["topHasIt"], "and not in the top bar")
    page.shot(args.shots, "mapmaker-rules2-1280x960")
    for word in (0, 1):
        if open_blank(80, 30, word, "Rules %d tall" % word):
            facts = chip(page)
            check(not facts["rowBadge"] and facts["chip"] == "" and not facts["topHasIt"], "a map of rules %d has no badge in the tag row either (%s)" % (word, facts))

    print("[web map maker] the badge on a phone, 390 x 844")
    start(390, 844, True)
    page.press_act("menu")
    wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
    page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'New'; })[0].click()")
    wait_for(lambda: page.ev("!!document.querySelector('.kinds')"))
    page.ev("document.querySelectorAll('.kind input')[1].click()")
    page.type_into("#size-rows", "140")
    page.type_into("#size-columns", "60")
    dialog_button(page, "Make map")
    check(wait_for(lambda: page.state()["rows"] == "140", 15), "an Extended map of 140 x 60 on the phone")
    facts = chip(page)
    check(facts["lineShown"] and facts["lineBadges"] == ["New version only", "Rules 2"] and facts["lineOwn"], "the size line has the two badges, on a line of their own (%s)" % facts)
    check(facts["sideScroll"] <= 0 and not facts["topHasIt"], "nothing scrolls sideways (%s pixels over) and the top bar has no badge" % facts["sideScroll"])
    page.shot(args.shots, "mapmaker-rules2-phone")
    open_blank(80, 30, 1, "Phone copy")
    page.press_act("menu")
    wait_for(lambda: page.ev("!!document.querySelector('.menulist')"))
    page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'Copy...'; })[0].click()")
    check(wait_for(lambda: page.ev("!!document.querySelector('.dlg .srccard')"), 20), "the Copy sheet opens on the phone")
    sheet = json.loads(page.ev("JSON.stringify({boxes: document.querySelectorAll('.dlg input[type=checkbox]').length, text: document.querySelector('.dlg').textContent, inside: (function () { var b = document.querySelector('.dlg').getBoundingClientRect(); return b.left >= -1 && b.right <= window.innerWidth + 1; })()})"))
    check(sheet["boxes"] == 0 and "ong orders" not in sheet["text"] and "ules" not in sheet["text"].replace("Rules 2", "") and sheet["inside"], "the sheet has no new row and is inside the screen")
    page.shot(args.shots, "mapmaker-rules2-copy-phone")
    time.sleep(0.2)

    print("[web map maker] an old wrapper beside an old module: both say version 4, so the check of the versions lets them through")
    tab.emulate(1280, 800, 1)
    hook = ("(function () { var real; Object.defineProperty(window, 'LvlCore', { configurable: true, get: function () { return real; }, set: function (v) { var wrap = v.wrap; v.EXPECTED_INTERFACE = 4; "
            "v.moduleVersion = function () { return 4; }; v.wrap = function (m) { var core = wrap(m); %s return core; }; real = v; } }); })();")
    check(page.open(hook % "") and page.ready(), "a wrapper and a module that say 4 and a core that has the call the page needs: the page starts (the control)")
    check(page.open(hook % "delete core.convertToLongOrders;"), "a core without convertToLongOrders: the page answers")
    text = page.ev("document.getElementById('boot').textContent")
    check(not page.ev("document.getElementById('boot').hidden") and "out of date" in text and "Reload the page" in text, "it says that the map code is out of date and offers a reload: %r" % text)
    check(page.ev("[].every.call(document.querySelectorAll('[data-act]'), function (b) { return b.disabled; })"), "and no button works: New map and Copy never run into the missing call")
    page.shot(args.shots, "mapmaker-old-wrapper")

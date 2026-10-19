// The game page's side of the map maker's "Play this map" (docs/MAP_EDITOR.md, "Play this map"; docs/PLAY_IN_BROWSER.md): a test game on the map that is open in the map maker, against bots, in a tab of
// its own. The address says only which hand-over to take and who sits where (play.html?testmap=<id>&seats=<four letters>); the map itself never travels in an address: the map maker's tab keeps
// the bytes and this page asks it for them (window.opener.antsTestGameHandoff), writes them where the game looks for maps and starts the game. Everything the page shows of the map's name is
// put in with textContent. A plain ES5 script that a page and node can load.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsTestGame = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var FILE = 'playtest.lvl';                                  // the one name that the game's maps folder gets (the map's own name is never a file name)
    var MAX_BYTES = 1024 * 1024;                                // as the players' maps (map_files.js): anything above a megabyte is no map
    var TEAMS = ['Green', 'Red', 'Blue', 'Black'];             // by the game's seat numbers
    var LEVELS = { e: 'easy', m: 'medium', h: 'hard' };
    var WORDS = { e: 'Easy', m: 'Medium', h: 'Hard' };
    var EDITOR = '/mapmaker.html';
    var LOOK = 'font-family:inherit;color:#f4ece2;';

    // The arguments of the game for an address: the hand-over's id (6 to 32 letters and digits, lower case) and the four seats in the game's order (Green, Red, Blue, Black), each y (you: exactly one),
    // e, m or h (a bot of that level: at least one) or n (nobody). { id, seats, you, bots: [[seat, letter]], args } or null; nothing of the address reaches the game but these words
    function fromAddress(id, seats) {
        if (typeof id !== 'string' || !/^[a-z0-9]{6,32}$/.test(id) || typeof seats !== 'string' || !/^[yemhn]{4}$/.test(seats)) return null;
        var you = -1, bots = [], args = ['--map', 'Original-Ants/Maps/' + FILE, '--play'];
        for (var s = 0; s < 4; s++) {
            var c = seats.charAt(s);
            if (c === 'y') { if (you >= 0) return null; you = s; } else if (c !== 'n') bots.push([s, c]);
        }
        if (you < 0 || !bots.length) return null;
        args.push('--player', String(you));
        bots.forEach(function (b) { args.push('--bot', b[0] + ':' + LEVELS[b[1]]); });
        return { id: id, seats: seats, you: you, bots: bots, args: args };
    }

    // "You: Red" and "Bots: Black Hard, Blue Medium, Green Easy" (the colors in the alphabet's order, as the map maker's dialog lists them)
    function seatLines(test) {
        var bots = test.bots.map(function (b) { return TEAMS[b[0]] + ' ' + WORDS[b[1]]; }).sort();
        return { you: 'You: ' + TEAMS[test.you], bots: 'Bots: ' + bots.join(', ') };
    }

    // A name for the strip: printable text only (no controls, no direction marks), 40 characters at most
    function cleanName(name) {
        var text = typeof name === 'string' ? name.replace(/\s+/g, ' ').replace(/[\u0000-\u001f\u007f-\u009f\u200b-\u200f\u202a-\u202e\u2066-\u2069\ufeff]/g, '').trim().replace(/ {2,}/g, ' ') : '';
        return Array.from(text).slice(0, 40).join('') || 'Test map';
    }

    // What the map maker's tab holds for this id: { name, bytes (a copy) }, or null (no such tab any more, another id, bytes that are no file)
    function take(win, id) {
        var rec;
        try {
            var opener = win.opener;
            if (!opener || opener.closed || typeof opener.antsTestGameHandoff !== 'function') return null;
            rec = opener.antsTestGameHandoff(id);
            if (!rec || !rec.bytes || !(rec.bytes.length > 0) || rec.bytes.length > MAX_BYTES) return null;
            return { name: cleanName(rec.name), bytes: new Uint8Array(rec.bytes) };
        } catch (e) { return null; }
    }

    function el(doc, tag, css, text) {
        var n = doc.createElement(tag);
        if (css) n.style.cssText = css;
        if (text !== undefined) n.textContent = text;
        return n;
    }

    // The card of a game that cannot start (the loading screen's own box: web/shell.html showDownloadError uses the same classes)
    function card(doc, title, text, button, href) {
        var progress = doc.getElementById('progress-container'), splash = doc.getElementById('splash-overlay'), status = doc.getElementById('status-text');
        if (progress) progress.style.display = 'none';
        if (splash) { splash.classList.add('failed'); splash.style.display = 'flex'; splash.style.opacity = '1'; }
        if (!status) return;
        while (status.firstChild) status.removeChild(status.firstChild);
        var box = el(doc, 'div'), go = el(doc, 'button', '', button);
        box.id = 'load-failure';
        box.className = 'load-failure';
        var t = el(doc, 'div', '', title), d = el(doc, 'div', '', text);
        t.className = 'load-failure-title';
        d.className = 'load-failure-detail';
        go.type = 'button';
        go.className = 'btn sm';
        go.addEventListener('click', function () { doc.defaultView.location.assign(href); });
        box.appendChild(t); box.appendChild(d); box.appendChild(go);
        status.appendChild(box);
    }

    // "Back to editor" asks once; the tab then closes (the map maker's tab is still there, as it was), or, when this tab was not opened from a map maker, goes to the map maker
    function leave(win, id) {
        try { if (win.opener && !win.opener.closed && typeof win.opener.antsTestGameBack === 'function') win.opener.antsTestGameBack(id); } catch (e) { /* the editor's tab does not answer: it is still there */ }
        try { win.close(); } catch (e) { /* the browser keeps a tab that it did not open for a script */ }
        win.setTimeout(function () { if (!win.closed) win.location.assign(EDITOR); }, 300);
    }

    function ask(win, id) {
        var doc = win.document, scrim = el(doc, 'div', 'position:fixed;left:0;top:0;right:0;bottom:0;z-index:100;display:flex;align-items:center;justify-content:center;padding:16px;background:rgba(0,0,0,.6);'), previous = doc.activeElement;
        var box = el(doc, 'div', 'box-sizing:border-box;max-width:460px;width:100%;padding:20px;border:2px solid #2aa58a;border-radius:10px;background:#07120e;' + LOOK);
        box.id = 'test-leave';
        box.setAttribute('role', 'dialog');
        box.setAttribute('aria-modal', 'true');
        box.setAttribute('aria-labelledby', 'test-leave-title');
        var h = el(doc, 'h2', 'margin:0 0 8px;font-size:22px;font-weight:500;color:#f6d058;', 'Leave the test game?');
        h.id = 'test-leave-title';
        var row = el(doc, 'div', 'display:flex;gap:10px;justify-content:flex-end;flex-wrap:wrap;margin-top:16px;');
        var keep = el(doc, 'button', 'min-height:44px;', 'Keep playing'), out = el(doc, 'button', 'min-height:44px;', 'Back to editor');
        keep.type = out.type = 'button';
        keep.id = 'test-leave-keep';
        out.id = 'test-leave-out';
        keep.className = 'btn';
        out.className = 'btn';
        function close() { doc.removeEventListener('keydown', onKey, true); if (scrim.parentNode) scrim.parentNode.removeChild(scrim); if (previous && previous.focus) previous.focus(); }
        function onKey(e) { if (e.key === 'Escape') { e.stopPropagation(); close(); } else if (e.key === 'Tab') { e.preventDefault(); (doc.activeElement === keep ? out : keep).focus(); } }
        keep.addEventListener('click', close);
        out.addEventListener('click', function () { close(); leave(win, id); });
        box.appendChild(h);
        box.appendChild(el(doc, 'p', 'margin:0;font-size:17px;line-height:1.35;', 'The game stops here. You go back to the map exactly as you left it.'));
        row.appendChild(keep); row.appendChild(out); box.appendChild(row); scrim.appendChild(box);
        doc.addEventListener('keydown', onKey, true);
        doc.body.appendChild(scrim);
        keep.focus();
    }

    // The strip above the page: the way back, what this is, who sits where, and that nothing is kept. Two lines on a phone (it wraps), the button 44 pixels high.
    function strip(win, test, rec) {
        var doc = win.document, lines = seatLines(test);
        var bar = el(doc, 'div', 'box-sizing:border-box;width:100%;align-self:stretch;display:flex;flex-wrap:wrap;align-items:center;gap:6px 14px;padding:8px 14px;background:#07120e;border-bottom:3px solid #f6d058;' + LOOK);
        bar.id = 'test-strip';
        bar.setAttribute('role', 'region');
        bar.setAttribute('aria-label', 'Test game');
        var back = el(doc, 'button', 'min-height:44px;', '\u25c0 Back to editor');
        back.type = 'button';
        back.className = 'btn';
        back.addEventListener('click', function () { ask(win, test.id); });
        var title = el(doc, 'strong', 'font-weight:500;font-size:18px;color:#f6d058;', 'Test game');
        var who = el(doc, 'span', 'flex:1 1 260px;font-size:15px;color:#b9c8bf;', ''), name = el(doc, 'span', 'color:#f4ece2;', rec.name);
        who.appendChild(name);
        who.appendChild(doc.createTextNode(' \u00b7 ' + lines.you + ' \u00b7 ' + lines.bots));
        var note = el(doc, 'span', 'font-size:14px;color:#8fa59a;', 'Not saved. Not counted.');
        bar.appendChild(back); bar.appendChild(title); bar.appendChild(who); bar.appendChild(note);
        doc.body.insertBefore(bar, doc.body.firstChild);
        return bar;
    }

    // The game page's start of a test game: the bytes are written where the game looks (its Module.preRun), the strip goes up, and `go` starts the game. A hand-over that is not there (the map maker's tab is
    // closed, or this tab was not opened from one) is a card with a way to the map maker, and the game does not start. A reload of this tab takes the same hand-over again: the game starts over on the same map.
    function begin(test, win, getModule, done) {
        var rec = take(win, test.id);
        if (!rec) {
            card(win.document, 'This test game cannot start', 'A test game is started from the map maker, and the tab that held its map is not there any more. Open the map maker and press Play this map again.', 'Open the map maker', EDITOR);
            return false;
        }
        getModule().preRun.push(win.AntsMapFiles.writer(function () { return getModule().FS; }, FILE, rec.bytes));
        win.antsReportLocalGame = function () { /* a test game is not counted in the front page's numbers */ };
        strip(win, test, rec);
        done();
        return true;
    }

    return { fromAddress: fromAddress, seatLines: seatLines, cleanName: cleanName, take: take, strip: strip, begin: begin, leave: leave, FILE: FILE, MAX_BYTES: MAX_BYTES, TEAMS: TEAMS };
});

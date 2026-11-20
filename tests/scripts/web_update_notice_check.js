// The update notice (web/update_notice.js, docs/SITE_NAV.md "The update notice") RUN in node with the DOM fake of the Map Maker's checks, a clock that the check winds, a fetch that answers what the check says and
// storage that can fail: when the script asks, what it shows, what it keeps, and what it never does.
//   asking:   nothing on a page that is not stamped with a version or that is a frame; the first request 3 seconds after the page opens (0.6 when the window may come), then every 5 minutes while the tab is in
//             front, none while it is hidden, one at once when it comes back after a minute and none when it comes back sooner; every request revalidates; a failed one, a file that is not JSON, a page of HTML and a
//             file of another shape are silent;
//   the strip: only when the file's version is newer than the page's (never for the same or an older one); under the bar of a page, first among the strips of the front page; Reload reloads; Later hides it for
//             this version in this tab, and a still newer version shows it again; it waits while the page says that a match runs, comes when that ends, goes when the next one starts, and the page is told
//             of each change (the game page lays its picture out again);
//   the window: the releases between the version that was seen and the page's, newest first, the newest open and the others folded, the newest five and "All changes" when there are more (or when the file
//             reaches back less far), written down as seen at once (so never twice); a first visit, the same version, an older page, storage that fails (to read or to write), a page that does not
//             have a window (the game page, the admin page) and releases with no line show nothing; close, Escape and a press on the dimmed page close it; Tab stays inside it;
//   text:     a line that holds markup is put in as text (no element is made from it), and no element gets a style;
//   the game page's own call (the block of web/shell.html between ANTS_UPDATE_BEGIN and ANTS_UPDATE_END, run on a page of fakes): silent while the game loads, a match is played, a live match is watched or
//             an online match is joined and its results are not up; free at a game's menu, in a recording and once the results of an online match are up; the match-over call still reaches the
//             functions that wrapped it before; the picture is laid out again when the strip comes or goes.
// usage: node web_update_notice_check.js ../../web/update_notice.js [../../web/shell.html]      (exit 0: every check holds; 1: one fails)
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { makeDocument } = require('./web_mapmaker_fake_dom');

const SCRIPT = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'update_notice.js'));
const SOURCE = fs.readFileSync(SCRIPT, 'utf8');
const SHELL = path.resolve(process.argv[3] || path.join(__dirname, '..', '..', 'web', 'shell.html'));
let checks = 0, failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const same = (label, got, want) => check(label, JSON.stringify(got) === JSON.stringify(want), JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
const flush = () => new Promise((resolve) => setImmediate(resolve));

const seg = (text, bold) => [bold ? 'b' : 't', text];
const rel = (version, date, items, fixes) => ({ version, date, items: items || [[seg('A thing in ' + version + '.', true), seg(' It does something.')]], fixes: fixes || [] });
const file = (version, releases) => ({ version, releases });

// One page: the document, the window with a clock and a fetch, and the script run on it. `o`: own (the page's version: '' none), where ('bar' | 'strips' | 'footer'), top (false: a frame), noWindow,
// answer (what /whats_new.json says: an object, or a function that is asked every time, or 'html' | 'fail' | 'down' | 'text'), seen (what local storage holds), later (what session storage holds),
// storage ('ok' | 'read' | 'write' | 'none').
function page(o) {
    o = Object.assign({ own: 'v1.2.0', where: 'bar', top: true, answer: null, seen: null, later: null, storage: 'ok' }, o);
    const doc = makeDocument();
    const win = { self: null, top: null, now: 1000000, timers: [], nextTimer: 1, fetches: [], reloads: 0, moved: 0, logs: [] };
    win.self = win;
    win.top = o.top ? win : {};
    win.Date = { now: () => win.now };
    win.location = { reload: () => { win.reloads++; } };
    win.setTimeout = (fn, ms) => { const t = { id: win.nextTimer++, at: win.now + ms, fn, every: 0 }; win.timers.push(t); return t.id; };
    win.setInterval = (fn, ms) => { const t = { id: win.nextTimer++, at: win.now + ms, fn, every: ms }; win.timers.push(t); return t.id; };
    win.clearTimeout = win.clearInterval = (id) => { win.timers = win.timers.filter((t) => t.id !== id); };
    win.advance = async (ms) => {                        // the clock goes on by ms, the due timers run in their order, the answers of fetch are let through
        const end = win.now + ms;
        for (;;) {
            await flush();
            const due = win.timers.filter((t) => t.at <= end).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            win.now = Math.max(win.now, due.at);
            if (due.every) due.at += due.every; else win.timers = win.timers.filter((t) => t !== due);
            due.fn();
        }
        win.now = end;
        await flush();
    };
    win.fetch = (url, options) => {
        win.fetches.push({ url, options, at: win.now });
        const a = typeof o.answer === 'function' ? o.answer() : o.answer;
        if (a === 'down') return Promise.reject(new Error('offline'));
        if (a === 'fail') return Promise.resolve({ ok: false, status: 404, text: () => Promise.resolve('') });
        if (a === 'html') return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve('<!doctype html><html><body>the game page</body></html>') });
        if (a === 'text') return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve('{"version": "1.9.0", "releases": ') });
        return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve(JSON.stringify(a)) });
    };
    const area = (initial) => {
        const items = {};
        if (initial !== null && initial !== undefined) items.value = initial;
        return items;
    };
    const local = area(), session = area();
    if (o.seen !== null) local['ants.seenVersion'] = o.seen;
    if (o.later !== null) session['ants.updateLater'] = o.later;
    const store = (items, keep) => ({
        getItem: (k) => { if (o.storage === 'read' || o.storage === 'none') throw new Error('no storage'); return Object.prototype.hasOwnProperty.call(items, k) ? items[k] : null; },
        setItem: (k, v) => { if (keep === 'local' && o.storage === 'write') return; if (o.storage === 'none') throw new Error('no storage'); items[k] = String(v); },
    });
    win.localStorage = store(local, 'local');
    win.sessionStorage = store(session, 'session');
    win.local = local;
    win.session = session;
    doc.visibilityState = 'visible';
    win.document = doc;
    win.doc = doc;
    if (o.where === 'strips') {
        const strips = doc.createElement('div'); strips.className = 'strips';
        const banner = doc.createElement('div'); banner.id = 'banner'; strips.appendChild(banner);
        doc.body.appendChild(strips);
        const foot = doc.createElement('span'); foot.id = 'game-version'; foot.textContent = o.own; doc.body.appendChild(foot);
    } else {
        const head = doc.createElement('header'); head.className = 'sb';
        if (o.own) head.setAttribute('data-version', o.own);
        doc.body.appendChild(head);
        const main = doc.createElement('div'); main.id = 'main'; doc.body.appendChild(main);
    }
    const tag = doc.createElement('script');
    if (o.noWindow) tag.setAttribute('data-window', 'no');
    doc.currentScript = tag;
    new Function('window', 'document', SOURCE)(win, doc);
    win.strip = () => doc.body.all().find((n) => /(^|\s)upd-strip(\s|$)/.test(n.className)) || null;
    win.win = () => doc.body.all().find((n) => /(^|\s)upd-win(\s|$)/.test(n.className)) || null;
    win.find = (root, cls) => root.all().filter((n) => n.className.split(/\s+/).includes(cls));
    win.button = (root, text) => root.all().find((n) => n.tagName === 'button' && n.textContent === text);
    return win;
}
const newer = file('1.3.0', [rel('1.3.0', '2026-01-03')]);

(async () => {
    // ---- asking ----------------------------------------------------------------------------------------------------------------------------
    let p = page({ own: '', answer: newer });
    await p.advance(600000);
    same('a page with no version stamp asks for nothing and shows nothing', [p.fetches.length, !!p.strip(), p.AntsUpdate], [0, false, undefined]);
    p = page({ own: '@@GAME_VERSION@@', answer: newer });
    await p.advance(600000);
    same('a source page whose stamp is still the placeholder asks for nothing', [p.fetches.length, !!p.strip()], [0, false]);
    p = page({ top: false, answer: newer });
    await p.advance(600000);
    same('a frame asks for nothing and shows nothing', [p.fetches.length, !!p.strip()], [0, false]);

    p = page({ answer: file('1.2.0', []) });
    await p.advance(2900);
    check('the first request is not before 3 seconds', p.fetches.length === 0);
    await p.advance(200);
    same('... and comes at 3 seconds, for the data file, revalidated and with no credentials', [p.fetches.length, p.fetches[0].url, p.fetches[0].options && p.fetches[0].options.cache, p.fetches[0].options && p.fetches[0].options.credentials], [1, '/whats_new.json', 'no-cache', 'omit']);
    await p.advance(299000);
    check('the next is not before 5 minutes later', p.fetches.length === 1);
    await p.advance(2000);
    check('... and comes then', p.fetches.length === 2);
    await p.advance(300000 * 3);
    check('and every 5 minutes after', p.fetches.length === 5, p.fetches.length);

    p = page({ answer: file('1.2.0', []) });
    await p.advance(4000);
    p.doc.visibilityState = 'hidden'; p.doc.fire = null;
    for (const f of p.doc.listeners.visibilitychange || []) f();
    const before = p.fetches.length;
    await p.advance(900000);
    check('while the tab is hidden nothing is asked', p.fetches.length === before, p.fetches.length);
    p.doc.visibilityState = 'visible';
    for (const f of p.doc.listeners.visibilitychange || []) f();
    await p.advance(10);
    check('back after more than a minute: one request at once', p.fetches.length === before + 1, p.fetches.length);
    p.doc.visibilityState = 'hidden'; for (const f of p.doc.listeners.visibilitychange || []) f();
    await p.advance(30000);
    p.doc.visibilityState = 'visible'; for (const f of p.doc.listeners.visibilitychange || []) f();
    await p.advance(10);
    check('back within a minute: none at once', p.fetches.length === before + 1, p.fetches.length);
    await p.advance(300000);
    check('... and the 5 minutes go on', p.fetches.length >= before + 2, p.fetches.length);

    p = page({ answer: file('1.2.0', []) });
    p.doc.visibilityState = 'hidden';
    await p.advance(900000);
    check('a page that opens in a hidden tab asks for nothing', p.fetches.length === 0);
    p.doc.visibilityState = 'visible'; for (const f of p.doc.listeners.visibilitychange || []) f();
    await p.advance(10);
    check('... until the tab comes to the front', p.fetches.length === 1);

    for (const [what, answer] of [['a failed request', 'down'], ['a 404', 'fail'], ['a page of HTML (the game page, where nothing answers)', 'html'], ['a file cut short', 'text'], ['a file of another shape', { hello: 1 }], ['no releases list', { version: '9.0.0' }], ['no version', { releases: [] }], ['a version that is not one', { version: 'soon', releases: [] }]]) {
        p = page({ answer });
        await p.advance(320000);
        same(what + ': silent (no strip, no window), and the asking goes on', [!!p.strip(), !!p.win(), p.fetches.length], [false, false, 2]);
    }

    // ---- the strip -------------------------------------------------------------------------------------------------------------------------
    p = page({ answer: newer });
    await p.advance(3100);
    let s = p.strip();
    check('a newer version: the strip comes, under the bar of the page', !!s && s.parentNode === p.doc.body && p.doc.body.children.indexOf(s) === 1, s ? p.doc.body.children.indexOf(s) : 'none');
    check('... it says so in words, as text (a status), with Reload and Later', !!s && /new version of Ants is out/i.test(s.textContent) && s.getAttribute('role') === 'status' && !!p.button(s, 'Reload') && !!p.button(s, 'Later'));
    check('... and the room note is there for the stylesheet to show in a room only', !!s && p.find(s, 'u-room').length === 1);
    p.button(s, 'Reload').click();
    check('Reload reloads the page', p.reloads === 1);
    p.button(s, 'Later').click();
    check('Later takes the strip away', !p.strip());
    check('... and keeps it away for this version in this tab', p.session['ants.updateLater'] === '1.3.0');
    await p.advance(600000);
    check('... after the next requests too', !p.strip());
    p.answer = null;

    let current = newer;
    p = page({ answer: () => current });
    await p.advance(3100);
    p.button(p.strip(), 'Later').click();
    current = file('1.4.0', [rel('1.4.0', '2026-01-04')]);
    await p.advance(300000);
    check('a still newer version shows the strip again', !!p.strip());
    current = file('1.2.0', []);
    await p.advance(300000);
    check('a file that is no longer newer (a rollback) takes it away', !p.strip());
    current = file('1.1.0', []);
    p.later = null;
    await p.advance(300000);
    check('and an older file shows nothing', !p.strip());

    p = page({ answer: newer, later: '1.3.0' });
    await p.advance(3100);
    check('a tab that said Later for this version in an earlier page of the tab shows nothing', !p.strip());
    p = page({ answer: newer, later: '1.2.5' });
    await p.advance(3100);
    check('... but one that said Later for another version does', !!p.strip());

    p = page({ answer: file('1.2.0', [rel('1.2.0', '2026-01-02')]) });
    await p.advance(3100);
    check('the same version shows no strip', !p.strip());
    p = page({ answer: file('v1.3.0', [rel('1.3.0', '2026-01-03')]) });
    await p.advance(3100);
    check('a file that names the version with a v is read the same', !!p.strip());
    p = page({ own: '1.2.0', answer: file('1.10.0', []) });
    await p.advance(3100);
    check('versions compare by number (1.10.0 is newer than 1.2.0)', !!p.strip());

    p = page({ where: 'strips', own: 'v1.2.0', answer: newer });
    await p.advance(3100);
    s = p.strip();
    check('the front page: the strip is the first of its strips', !!s && s.parentNode.className === 'strips' && s.parentNode.children[0] === s);

    // ---- nothing during a match -------------------------------------------------------------------------------------------------------------
    let match = true, moved = 0;
    p = page({ answer: newer });
    p.AntsUpdate.page({ quiet: () => match, moved: () => { moved++; } });
    await p.advance(3100);
    check('while a match runs the strip does not come, though a newer version is known', !p.strip() && p.fetches.length === 1);
    await p.advance(60000);
    check('... for as long as it runs', !p.strip());
    match = false;
    await p.advance(1100);
    check('the match is over: the strip comes, within a second, and the page is told', !!p.strip() && moved === 1, moved);
    match = true;
    await p.advance(1100);
    check('a new match starts: the strip goes, and the page is told', !p.strip() && moved === 2, moved);
    match = false;
    await p.advance(1100);
    p.button(p.strip(), 'Later').click();
    check('Later still works after that, and the page is told', !p.strip() && moved === 4, moved);
    p = page({ answer: file('1.2.0', []) });
    p.AntsUpdate.page({ quiet: () => true });
    await p.advance(3100);
    check('a page that holds back has no timer of its own while there is nothing new to show', p.timers.every((t) => !t.every));
    p = page({ answer: newer });
    p.AntsUpdate.page({ quiet: () => { throw new Error('a hook that fails'); } });
    await p.advance(3100);
    check('a hook that throws does not stop the notice', !!p.strip());

    // ---- the window ------------------------------------------------------------------------------------------------------------------------
    const news = file('1.2.0', [rel('1.2.0', '2026-01-02', [[seg('Newest.', true), seg(' The newest line.')], [seg('Second line.', true)]], [[seg('A fix line.')]]),
                                rel('1.1.0', '2026-01-01', [[seg('Older one.', true), seg(' Its first line.')], [seg('Another.')]]), rel('1.0.0', '2025-12-31')]);
    p = page({ seen: '1.0.0', answer: news });
    await p.advance(500);
    check('the window may come: the request is early (not at 3 seconds)', p.fetches.length === 0);
    await p.advance(200);
    let w = p.win();
    check('after a reload on a newer version the window comes', !!w);
    const text = w ? w.textContent : '';
    same('... with the releases since the version that was seen (not that one), newest first', w ? p.find(w, 'u-rv').map((n) => n.textContent) : [], ['v1.2.0', 'v1.1.0']);
    check('... and says which version it counts from', /Changes since v1\.0\.0/.test(text), text);
    const rels = w ? p.find(w, 'u-rel') : [];
    same('... the newest one open and the older one folded to one line (its first line and how many more)', rels.map((r) => r.tagName), ['section', 'details']);
    check('... the folded one says its first line and how many more', rels.length === 2 && /^Older one \+ 1 more$/.test(p.find(rels[1], 'u-rt')[0].textContent) && /Jan 1/.test(rels[1].textContent), rels.length ? rels[1].textContent : '');
    check('... the open one has its lines, bold lead-ins as bold, its fixes under a label, and its date', rels.length === 2 && p.find(rels[0], 'u-rh')[0].textContent.indexOf('Jan 2') > 0 && rels[0].all().filter((n) => n.tagName === 'b').length === 2 && p.find(rels[0], 'u-fx').length === 1 && /A fix line\./.test(rels[0].textContent));
    check('... no "All changes" when the file reaches back to the version that was seen', !w || !/All changes/.test(w.textContent));
    check('... it is a dialog with a name and a Close button that has the focus', !!w && w.all().some((n) => n.getAttribute('role') === 'dialog' && n.getAttribute('aria-modal') === 'true' && !!n.getAttribute('aria-labelledby')) && p.doc.activeElement === p.button(w, 'Close'));
    check('... and what was seen is written at once, as the page\'s version', p.local['ants.seenVersion'] === '1.2.0', p.local['ants.seenVersion']);
    p.button(w, 'Close').click();
    check('Close closes it', !p.win());
    await p.advance(900000);
    check('... and it does not come again, nor does a strip', !p.win() && !p.strip());
    let second = page({ seen: p.local['ants.seenVersion'], answer: news });
    await second.advance(5000);
    check('the next page on the same version shows nothing', !second.win());

    p = page({ seen: '1.0.0', answer: news });
    await p.advance(800);
    w = p.win();
    p.doc.key('Escape');
    check('Escape closes it', !p.win());
    p = page({ seen: '1.0.0', answer: news });
    await p.advance(800);
    w = p.win();
    w.fire('pointerdown', { target: w }); w.fire('click', { target: w });
    check('a press that starts and ends on the dimmed page closes it', !p.win());
    p = page({ seen: '1.0.0', answer: news });
    await p.advance(800);
    w = p.win();
    const inside = w.all().find((n) => n.className === 'upd-box');
    w.fire('pointerdown', { target: inside }); w.fire('click', { target: w });
    check('a press that starts in the box and ends outside does not', !!p.win());
    w.fire('pointerdown', { target: inside }); w.fire('click', { target: inside });
    check('a press inside the box does not close it', !!p.win());

    p = page({ seen: '1.0.0', answer: news });
    await p.advance(800);
    w = p.win();
    const focusOrder = [];
    p.doc.activeElement = p.button(w, 'Close');
    let e = p.doc.key('Tab');
    check('Tab on the last control goes to the first (the window keeps the keys)', e.prevented && p.doc.activeElement !== p.button(w, 'Close'));
    p.doc.activeElement = w.all().find((n) => n.className.split(/\s+/).includes('u-wb'));
    e = p.doc.key('Tab', { shiftKey: true });
    check('Shift+Tab on the first goes to the last', e.prevented && p.doc.activeElement === p.button(w, 'Close'));
    void focusOrder;

    p = page({ seen: '1.0.0', answer: news });
    p.doc.activeElement = p.doc.body.children[0];
    await p.advance(800);
    p.button(p.win(), 'Close').click();
    check('the focus goes back to where it was', p.doc.activeElement === p.doc.body.children[0]);

    // more releases than five: the newest five and the way to the rest
    const many = file('1.8.0', [8, 7, 6, 5, 4, 3, 2].map((n) => rel('1.' + n + '.0', '2026-02-0' + n)));
    p = page({ own: 'v1.8.0', seen: '1.1.0', answer: many });
    await p.advance(800);
    w = p.win();
    const shown = w ? p.find(w, 'u-rv').map((n) => n.textContent) : [];
    same('seven releases newer than the one that was seen: the newest five', shown, ['v1.8.0', 'v1.7.0', 'v1.6.0', 'v1.5.0', 'v1.4.0']);
    check('... with "All changes" to the changelog, in a new tab, and the count in the header', !!w && p.find(w, 'u-wf')[0].all().some((n) => n.tagName === 'a' && n.getAttribute('href') === '/changelog' && n.getAttribute('target') === '_blank') && /newest 5 shown/.test(w.textContent));
    same('... only the newest is open', (w ? p.find(w, 'u-rel') : []).map((r) => r.tagName), ['section', 'details', 'details', 'details', 'details']);
    p = page({ own: 'v1.8.0', seen: '1.0.0', answer: file('1.8.0', [rel('1.8.0', '2026-02-08'), rel('1.7.0', '2026-02-07')]) });
    await p.advance(800);
    w = p.win();
    check('the file reaches back less far than the version that was seen: "All changes" is offered', !!w && /All changes/.test(w.textContent) && p.find(w, 'u-rv').length === 2);
    p = page({ own: 'v1.8.0', seen: '1.6.0', answer: many });
    await p.advance(800);
    check('a page newer than part of the file shows only what is newer than what was seen, and only up to itself', p.find(p.win(), 'u-rv').map((n) => n.textContent).join() === 'v1.8.0,v1.7.0');
    p = page({ own: 'v1.6.0', seen: '1.2.0', answer: many });
    await p.advance(800);
    check('... never a release newer than the page', p.find(p.win(), 'u-rv').map((n) => n.textContent).join() === 'v1.6.0,v1.5.0,v1.4.0,v1.3.0');

    // what shows nothing
    p = page({ answer: file('1.2.0', [rel('1.2.0', '2026-01-02')]) });
    await p.advance(3100);
    same('a first visit shows nothing, and the version is written down', [!!p.win(), p.local['ants.seenVersion']], [false, '1.2.0']);
    p = page({ seen: '1.2.0', answer: news });
    await p.advance(3100);
    same('the same version shows nothing, and writes nothing new', [!!p.win(), p.local['ants.seenVersion']], [false, '1.2.0']);
    p = page({ seen: '1.5.0', answer: news });
    await p.advance(3100);
    same('an older page (another tab has seen more) shows nothing and does not lower the version that was seen', [!!p.win(), p.local['ants.seenVersion']], [false, '1.5.0']);
    p = page({ seen: 'garbage', answer: news });
    await p.advance(3100);
    same('a damaged value counts as a first visit', [!!p.win(), p.local['ants.seenVersion']], [false, '1.2.0']);
    p = page({ seen: '1.0.0', answer: file('1.2.0', [rel('1.2.0', '2026-01-02', [])]) });
    await p.advance(3100);
    same('releases with no line show nothing, and the version is written down', [!!p.win(), p.local['ants.seenVersion']], [false, '1.2.0']);
    p = page({ seen: '1.0.0', answer: file('1.2.0', []) });
    await p.advance(3100);
    same('a file with no release shows nothing, and the version is written down', [!!p.win(), p.local['ants.seenVersion']], [false, '1.2.0']);
    for (const storage of ['read', 'write', 'none']) {
        p = page({ seen: '1.0.0', answer: news, storage });
        await p.advance(900000);
        same('storage that fails (' + storage + '): no window, no error, no repeat', [!!p.win(), p.fetches.length > 1], [false, true]);
    }
    p = page({ seen: '1.0.0', answer: news, noWindow: true });
    await p.advance(3100);
    same('a page that has no window (the game page, the admin page) shows none and writes nothing', [!!p.win(), p.local['ants.seenVersion']], [false, '1.0.0']);
    p = page({ answer: newer, noWindow: true });
    await p.advance(3100);
    check('... and still has the strip', !!p.strip());
    p = page({ answer: newer, noWindow: true, seen: '1.0.0' });
    await p.advance(500);
    check('... and its first request is at 3 seconds, not early', p.fetches.length === 0);

    // text only
    const markup = '<img src=x onerror=window.pwn=1> <b>not bold</b> &amp;';
    p = page({ seen: '1.0.0', answer: file('1.2.0', [rel('1.2.0', '2026-01-02', [[seg(markup, true), seg(' ' + markup)]], [[seg(markup)]]), rel('1.1.0', '2026-01-01', [[seg(markup, true)]])]) });
    await p.advance(800);
    w = p.win();
    const made = w ? w.all().map((n) => n.tagName) : [];
    check('markup in the file is put in as text: no element is made from it', made.length > 0 && !made.includes('img') && made.filter((n) => n === 'b').length === 2 && w.textContent.split(markup).length === 6, made.join());
    p = page({ seen: '1.0.0', answer: file('1.2.0', [{ version: '1.2.0', date: '<script>', items: [[['b', 5], 'x', null, ['t', { a: 1 }], ['t', 'ok']], 'junk', null], fixes: 'no' }, { version: '1.1.0', date: 7, items: 3, fixes: null }]) });
    await p.advance(800);
    check('a file with damaged lines does not break the window and shows what it can', !!p.win() && /ok/.test(p.win().textContent));
    p = page({ answer: newer });
    await p.advance(3100);
    const all = p.doc.body.all();
    check('no element gets a style (the admin page allows none)', all.every((n) => Object.keys(n.style).length === 0 && !n.hasAttribute('style')));

    // ---- the game page's own call ----------------------------------------------------------------------------------------------------------
    {
        const text = fs.readFileSync(SHELL, 'utf8');
        const begin = text.indexOf('// ANTS_UPDATE_BEGIN'), end = text.indexOf('// ANTS_UPDATE_END');
        check('the game page has the block that this check runs', begin > 0 && end > begin);
        const block = text.slice(begin, end);
        const gamePage = (extra) => {
            const log = { relayouts: [], previous: 0 };
            const context = Object.assign({ isReadyToPlay: true, ANTS_LIVE_MODE: false, ANTS_ARGS: [], Module: { _ants_match_running: () => 0 }, relayout: (force) => log.relayouts.push(force),
                                            window: { antsMatchOver: () => { log.previous++; return 'kept'; }, AntsUpdate: { page: (hooks) => { log.hooks = hooks; } } } }, extra || {});
            vm.runInNewContext(block, context);
            log.context = context;
            log.quiet = () => log.hooks.quiet();
            return log;
        };
        let g = gamePage();
        check('the page hands over its two hooks', !!g.hooks && typeof g.hooks.quiet === 'function' && typeof g.hooks.moved === 'function');
        same('a game on this computer at its menu or at its results: not quiet', g.quiet(), false);
        g.context.Module._ants_match_running = () => 1;
        same('... while its match is played: quiet', g.quiet(), true);
        g.context.Module._ants_match_running = () => 0;
        g.context.isReadyToPlay = false;
        same('... while the game loads: quiet', g.quiet(), true);
        g.context.isReadyToPlay = true;
        g.context.ANTS_LIVE_MODE = true;
        same('... while a live match is watched: quiet', g.quiet(), true);
        g.context.ANTS_LIVE_MODE = false;
        g.context.Module._ants_match_running = () => { throw new Error('the game does not answer'); };
        same('... a game that does not answer is no match', g.quiet(), false);
        g.context.Module = undefined;
        g.context.isReadyToPlay = true;
        same('... and no game at all is no match', g.quiet(), false);

        g = gamePage({ ANTS_ARGS: ['--join-url', 'ws://x/room'] });
        same('an online match that is joined: quiet, though the game says that no match runs (the countdown, the wait)', g.quiet(), true);
        g.context.Module._ants_match_running = () => 1;
        same('... while it is played', g.quiet(), true);
        g.context.Module._ants_match_running = () => 0;
        const answer = g.context.window.antsMatchOver('{}');
        same('... the match over: the call reaches what wrapped it before and its answer is kept, and the page is free', [g.previous, answer, g.quiet()], [1, 'kept', false]);
        g.context.Module._ants_match_running = () => 1;
        same('... a new match of that page is quiet again', g.quiet(), true);
        g.hooks.moved();
        same('the picture is laid out again when the strip comes or goes', g.relayouts, [false]);
        g = gamePage();
        g.context.window.antsMatchOver = undefined;
        vm.runInNewContext(block, Object.assign(g.context, { window: Object.assign(g.context.window, { antsMatchOver: undefined }) }));
        check('a page where nothing wrapped the match-over call before has one that does not fail', (() => { try { g.context.window.antsMatchOver('{}'); return true; } catch (err) { return false; } })());
        check('a page whose script did not load (no update notice) still runs the block', (() => { try { vm.runInNewContext(block, { window: {}, isReadyToPlay: true, ANTS_ARGS: [], Module: {}, relayout() {} }); return true; } catch (err) { return false; } })());
    }

    console.log(failures ? failures + ' of ' + checks + ' checks FAILED' : 'update notice: all ' + checks + ' checks passed');
    process.exit(failures ? 1 : 0);
})().catch((err) => { console.log('FAIL the check itself stopped: ' + (err && err.stack || err)); process.exit(1); });

// Runs the game page's OWN code of the bar's Copy link (web/shell.html: everything between COPY_LINK_BEGIN and COPY_LINK_END) on a page of fakes: the address that a press copies (this site and the match, a
// live match as play?live=<id>, a kept recording as play?replay=<file>, never the page's own address with whatever else is in it), a share sheet on a phone's layout (a wide window never opens it, and a sheet
// that is only closed is nothing more), the clipboard and the toast that says "Copied!" when it has answered, a clipboard that refuses (the hidden field and the copy command, then the browser's own box with
// the address to copy by hand), a browser with no clipboard, a share or a clipboard that throws.
// tests/scripts/test_web_replay.py runs this with node (the quick tier). usage: node web_copy_link_check.js web/shell.html     (exit 0: every row holds; every differing row is printed)
'use strict';
const fs = require('fs');

const path = process.argv[2];
if (!path) { console.log('usage: web_copy_link_check.js shell.html'); process.exit(2); }
const text = fs.readFileSync(path, 'utf8');
const at = text.indexOf('COPY_LINK_BEGIN');
const stop = text.indexOf('COPY_LINK_END');
if (at < 0 || stop < 0) { console.log('FAIL the markers of the Copy link block are missing'); process.exit(1); }
const block = text.slice(text.indexOf('\n', at) + 1, text.lastIndexOf('\n', stop - 1) + 1);

let failed = 0, checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}
const settle = () => new Promise((resolve) => setTimeout(resolve, 5));

const SITE = 'https://ants.example';
const LIVE = 'TREASURE-20261009-082104Z';
const FILE = 'ants-TREASURE-20261008-143209Z.antsrep';
const VIEWPORTS = { phone: [390, 844], landscape: [844, 390], tablet: [768, 1024], wide: [1280, 800], edge: [701, 600] };

// a page: o.view (a key of VIEWPORTS), o.file, o.live (what the page watches), o.clipboard ('ok' | 'refuses' | 'throws' | 'none'), o.share ('ok' | 'closed' | 'refused' | 'throws' | 'none'),
// o.copyCommand (true | false | 'throws'), o.matchMedia (false: the browser has none; 'throws')
function page(o) {
    o = Object.assign({ view: 'wide', file: '', live: LIVE, clipboard: 'ok', share: 'none', copyCommand: true, matchMedia: true }, o || {});
    const [vw, vh] = VIEWPORTS[o.view];
    const seen = { writes: [], shared: [], toasts: [], prompts: [], quiet: 0, queries: [], fields: [], removed: [], commands: [], appended: 0 };
    const listeners = {};
    const button = { addEventListener(type, fn) { (listeners[type] = listeners[type] || []).push(fn); } };
    const matches = (query) => query.split(',').some((part) => {                 // the media query of the page, evaluated for the viewport (a list is "or")
        const m = /\(\s*(max|min)-(width|height)\s*:\s*(\d+)px\s*\)/.exec(part);
        if (!m) return false;
        const size = m[2] === 'width' ? vw : vh;
        return m[1] === 'max' ? size <= +m[3] : size >= +m[3];
    });
    const win = {
        location: { origin: SITE, href: SITE + '/play?live=' + LIVE + '&shape=4%3A3&x=1', search: '?live=' + LIVE + '&shape=4%3A3&x=1' },
        prompt(words, value) { seen.prompts.push([words, value]); return null; },
    };
    if (o.matchMedia === 'throws') win.matchMedia = () => { throw new Error('no media'); };
    else if (o.matchMedia) win.matchMedia = (q) => { seen.queries.push(q); return { matches: matches(q) }; };
    const body = { appendChild(n) { seen.appended++; seen.fields.push(n); n.inPage = true; }, removeChild(n) { seen.removed.push(n); n.inPage = false; } };
    const doc = {
        body,
        createElement(tag) {
            const n = { tag, attrs: {}, style: {}, selected: false, inPage: false, value: '', setAttribute(k, v) { n.attrs[k] = String(v); }, select() { n.selected = true; } };
            return n;
        },
        execCommand(name) {
            seen.commands.push([name, seen.fields.length ? seen.fields[seen.fields.length - 1].selected : null, seen.fields.length ? seen.fields[seen.fields.length - 1].value : null]);
            if (o.copyCommand === 'throws') throw new Error('not allowed');
            const f = seen.fields[seen.fields.length - 1];
            return name === 'copy' && !!f && f.selected && o.copyCommand === true;
        },
    };
    const nav = {};
    if (o.clipboard !== 'none') {
        nav.clipboard = { writeText(t) {
            if (o.clipboard === 'throws') throw new Error('no clipboard');
            seen.writes.push(t);
            return o.clipboard === 'ok' ? Promise.resolve() : Promise.reject(new Error('NotAllowedError'));
        } };
    }
    if (o.share !== 'none') {
        nav.share = (d) => {
            seen.shared.push(d);
            if (o.share === 'throws') throw new Error('no sheet');
            if (o.share === 'ok') return Promise.resolve();
            const err = new Error(o.share === 'closed' ? 'closed' : 'refused'); err.name = o.share === 'closed' ? 'AbortError' : 'NotAllowedError';
            return Promise.reject(err);
        };
    }
    const quietly = (ask) => { seen.quiet++; return ask(); };
    new Function('$', 'say', 'antsQuietly', 'FILE', 'LIVE_ID', 'window', 'document', 'navigator', block)(
        (id) => (id === 'copy-btn' ? button : { addEventListener() {} }), (t) => { seen.toasts.push(t); }, quietly, o.file, o.live, win, doc, nav);
    return { seen, listeners, press() { (listeners.click || []).forEach((f) => f({})); return settle(); } };
}

(async () => {
    let p = page();
    expect('the block puts one click listener on the button and does nothing before a press', [(p.listeners.click || []).length, p.seen.writes, p.seen.toasts, p.seen.shared], [1, [], [], []]);

    // ---- the address ----
    p = page({ view: 'wide' }); await p.press();
    expect('a live match: this site and play?live=<id>, not the page\'s own address with its other words', p.seen.writes, [SITE + '/play?live=' + LIVE]);
    p = page({ view: 'wide', file: FILE }); await p.press();
    expect('a kept recording (also one that was watched live: its address became replay=): play?replay=<file>', p.seen.writes, [SITE + '/play?replay=' + FILE]);
    p = page({ view: 'wide', file: '', live: 'A B&c' }); await p.press();
    expect('what is put in the address is encoded', p.seen.writes, [SITE + '/play?live=A%20B%26c']);
    p = page({ view: 'wide', file: 'a b&c.antsrep' }); await p.press();
    expect('... a file name too', p.seen.writes, [SITE + '/play?replay=a%20b%26c.antsrep']);
    p = page({ view: 'wide', file: '', live: null }); await p.press();
    expect('no match at all: still an address of this site (the page says the match is not live)', p.seen.writes, [SITE + '/play?live=']);

    // ---- a wide window: the clipboard, and the toast when it has answered ----
    p = page({ view: 'wide', share: 'ok' });
    const before = p.seen.toasts.length;
    p.listeners.click[0]({});
    expect('the toast waits for the clipboard: nothing is said at the press itself', [before, p.seen.toasts], [0, []]);
    await settle();
    expect('a wide window with a share sheet in the browser never opens it: the address is copied and the toast says Copied!', [p.seen.shared, p.seen.writes.length, p.seen.toasts, p.seen.prompts, p.seen.appended], [[], 1, ['Copied!'], [], 0]);
    p = page({ view: 'edge', share: 'ok' }); await p.press();
    expect('701 px wide and tall enough is not a phone\'s layout: copied', [p.seen.shared, p.seen.writes.length, p.seen.toasts], [[], 1, ['Copied!']]);
    p = page({ view: 'wide' }); await p.press(); await p.press();
    expect('each press copies and says it', [p.seen.writes.length, p.seen.toasts], [2, ['Copied!', 'Copied!']]);

    // ---- a phone: the share sheet ----
    for (const view of ['phone', 'landscape']) {
        p = page({ view, share: 'ok' }); await p.press();
        expect(view + ' with a share sheet: it opens with the address and nothing else, and nothing is copied or said', [p.seen.shared, p.seen.writes, p.seen.toasts, p.seen.prompts], [[{ url: SITE + '/play?live=' + LIVE }], [], [], []]);
    }
    p = page({ view: 'phone', file: FILE, share: 'ok' }); await p.press();
    expect('a recording on a phone shares its own address', p.seen.shared, [{ url: SITE + '/play?replay=' + FILE }]);
    p = page({ view: 'phone', share: 'closed' }); await p.press();
    expect('a sheet that the person closes is done: no copy, no toast, no box', [p.seen.shared.length, p.seen.writes, p.seen.toasts, p.seen.prompts, p.seen.appended], [1, [], [], [], 0]);
    p = page({ view: 'phone', share: 'refused' }); await p.press();
    expect('a sheet that cannot open is a refusal: the address is copied instead', [p.seen.shared.length, p.seen.writes, p.seen.toasts], [1, [SITE + '/play?live=' + LIVE], ['Copied!']]);
    p = page({ view: 'phone', share: 'throws' }); await p.press();
    expect('a share that throws: the address is copied instead', [p.seen.shared.length, p.seen.writes, p.seen.toasts], [1, [SITE + '/play?live=' + LIVE], ['Copied!']]);
    p = page({ view: 'phone', share: 'none' }); await p.press();
    expect('a phone with no share sheet copies and says Copied!', [p.seen.writes, p.seen.toasts], [[SITE + '/play?live=' + LIVE], ['Copied!']]);
    p = page({ view: 'phone', share: 'ok', matchMedia: false }); await p.press();
    expect('a browser that cannot tell the layout (no matchMedia) is taken for a wide window: copied', [p.seen.shared, p.seen.writes.length, p.seen.toasts], [[], 1, ['Copied!']]);
    p = page({ view: 'phone', share: 'ok', matchMedia: 'throws' }); await p.press();
    expect('... and one whose matchMedia throws as well', [p.seen.shared, p.seen.writes.length, p.seen.toasts], [[], 1, ['Copied!']]);

    // ---- a clipboard that refuses, or is not there ----
    for (const how of ['refuses', 'throws', 'none']) {
        p = page({ view: 'wide', clipboard: how }); await p.press();
        const field = p.seen.fields[0];
        expect('clipboard ' + how + ': the old way, a hidden field that holds the address, is selected, copied with the copy command and taken away again; the toast says Copied!',
            [p.seen.fields.length, field && field.value, field && field.selected, p.seen.commands.map((c) => c[0]), field && field.inPage, p.seen.removed.length, p.seen.toasts, p.seen.prompts.length],
            [1, SITE + '/play?live=' + LIVE, true, ['copy'], false, 1, ['Copied!'], 0]);
        expect('clipboard ' + how + ': the field is read-only (no keyboard comes up on a phone) and see-through', [field && field.attrs.readonly === '', field && field.style.opacity], [true, '0']);
        p = page({ view: 'wide', clipboard: how, copyCommand: false }); await p.press();
        expect('clipboard ' + how + ' and the command refuses: the browser\'s own box shows the address to copy by hand (quietly: the game\'s sound stops while it is up), and nothing says Copied!',
            [p.seen.prompts, p.seen.quiet, p.seen.toasts, p.seen.fields[0] && p.seen.fields[0].inPage], [[['Copy the link to this match:', SITE + '/play?live=' + LIVE]], 1, [], false]);
        p = page({ view: 'wide', clipboard: how, copyCommand: 'throws' }); await p.press();
        expect('clipboard ' + how + ' and the command throws: the same box, and the field is gone', [p.seen.prompts.length, p.seen.toasts, p.seen.fields[0] && p.seen.fields[0].inPage], [1, [], false]);
    }
    p = page({ view: 'phone', share: 'refused', clipboard: 'refuses', copyCommand: false }); await p.press();
    expect('a phone whose share and clipboard both refuse and whose command fails: the box with the address', [p.seen.shared.length, p.seen.prompts.length, p.seen.toasts], [1, 1, []]);
    p = page({ view: 'wide', clipboard: 'refuses' });
    p.listeners.click[0]({});
    expect('a refused clipboard says nothing at the press itself', p.seen.toasts, []);
    await settle();

    // ---- the layout test is the page's phone layout: narrow, or short ----
    p = page({ view: 'tablet', share: 'ok' }); await p.press();
    expect('a tablet (768 x 1024) is a wide window', [p.seen.shared.length, p.seen.writes.length], [0, 1]);

    console.log('copy_link: ' + checks + ' checks, ' + failed + ' failed');
    if (failed === 0) console.log('every row holds');
    process.exit(failed === 0 ? 0 : 1);
})();

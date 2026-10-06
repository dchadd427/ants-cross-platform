// Runs the game page's OWN code for the replay of a match (web/shell.html, the block ANTS_REPLAY_BEGIN .. ANTS_REPLAY_END): antsOfferReplay, which the game calls when a match has ended (with the
// file) and when the next one begins (with nothing), and antsDownloadReplay, which the "Download replay" button runs. The button appears and goes away, the file is saved through a link with the download
// attribute under a name that is safe on every system, and no failure of the page (no button, no Blob, no object URL, a refused click) ever reaches the game.
// tests/scripts/test_web_replay.py runs this with node (the quick tier). usage: node web_replay_check.js web/shell.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const vm = require('vm');

const shellPath = process.argv[2];
if (!shellPath) { console.log('usage: web_replay_check.js shell.html'); process.exit(2); }

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}

const text = fs.readFileSync(shellPath, 'utf8');
const begin = text.indexOf('ANTS_REPLAY_BEGIN');
const end = text.indexOf('ANTS_REPLAY_END');
if (begin < 0 || end < begin || text.indexOf('ANTS_REPLAY_BEGIN', begin + 1) >= 0) { console.log('FAIL the block ANTS_REPLAY_BEGIN .. ANTS_REPLAY_END is missing or twice in ' + shellPath); process.exit(1); }
const block = text.slice(text.indexOf('\n', begin) + 1, text.lastIndexOf('\n', end));

// A page with a button, a body to put a link in, a Blob and the object URLs: everything that the block touches, recorded
function fakePage(options) {
    options = options || {};
    const log = { blobs: [], urls: [], revoked: [], links: [], timers: [], appended: 0, removed: 0 };
    const button = { hidden: true, textContent: 'Download replay' };
    const document = {
        getElementById: (id) => {
            if (options.getElementByIdThrows) throw new Error('no document');
            return id === 'replay-download' && !options.noButton ? button : null;
        },
        createElement: (tag) => {
            if (options.createElementThrows) throw new Error('cannot make a link');
            const link = { tag: tag, style: {}, clicked: 0, click() { this.clicked++; if (options.clickThrows) throw new Error('refused'); } };
            log.links.push(link);
            return link;
        },
        body: { appendChild: () => { log.appended++; }, removeChild: () => { log.removed++; } },
    };
    const context = {
        document,
        Blob: options.noBlob ? undefined : function (parts, opts) { log.blobs.push({ parts: parts, type: opts && opts.type }); this.parts = parts; },
        URL: options.noUrl ? undefined : {
            createObjectURL: (blob) => { if (options.urlThrows) throw new Error('no object url'); const url = 'blob:fake/' + (log.urls.length + 1); log.urls.push({ url: url, blob: blob }); return url; },
            revokeObjectURL: (url) => { log.revoked.push(url); },
        },
        setTimeout: (fn, ms) => { log.timers.push({ fn: fn, ms: ms }); return log.timers.length; },
        Math, String, RegExp, Object,
    };
    vm.createContext(context);
    vm.runInContext(block, context);
    return { context, log, button };
}

const bytes = (n) => { const b = new Uint8Array(n); b[0] = 0x89; return b; };

// 1. the functions are globals of the page (the game's EM_JS code finds antsOfferReplay by its name) and the button starts hidden in the markup
{
    const page = fakePage();
    check('antsOfferReplay is a global of the page', typeof page.context.antsOfferReplay === 'function');
    check('antsDownloadReplay is a global of the page', typeof page.context.antsDownloadReplay === 'function');
    check('the markup has the button, hidden at first, with the click handler', /<button[^>]*id="replay-download"[^>]*\bhidden\b[^>]*onclick="antsDownloadReplay\(\)"/.test(text));
    check('the stylesheet lets `hidden` win over the button class', /\.replay-part\[hidden\]\s*\{\s*display:\s*none;/.test(text));
    check('nothing is offered before the game says so', page.context.antsReplayFile === null && page.button.hidden === true);
}

// 2. a file is offered: the button shows its size, the click saves exactly the file
{
    const page = fakePage();
    const file = bytes(84 * 1024 + 100);
    page.context.antsOfferReplay(file, 'ants-TINY-20261006-143209.antsrep');
    check('the button is shown', page.button.hidden === false);
    same('the button says what it saves and how big it is', page.button.textContent, 'Download replay (84 KB)');
    const result = page.context.antsDownloadReplay();
    check('the click reports that it saved', result === true);
    same('one Blob of the file, as a binary type', page.log.blobs.map((b) => b.type), ['application/octet-stream']);
    check('the Blob holds the very bytes that the game gave', page.log.blobs[0].parts.length === 1 && page.log.blobs[0].parts[0] === file);
    same('one object URL, made from that Blob', page.log.urls.length, 1);
    check('the URL is of the Blob', page.log.urls[0].blob.parts[0] === file);
    const link = page.log.links[0];
    check('one link, an <a>', page.log.links.length === 1 && link.tag === 'a');
    check('its address is the object URL and its download attribute is the name of the file', link.href === page.log.urls[0].url && link.download === 'ants-TINY-20261006-143209.antsrep', JSON.stringify([link.href, link.download]));
    check('it was clicked once, put in the page and taken out again', link.clicked === 1 && page.log.appended === 1 && page.log.removed === 1);
    check('the object URL is let go of later, not at once', page.log.revoked.length === 0 && page.log.timers.length === 1 && page.log.timers[0].ms >= 10000);
    page.log.timers[0].fn();
    same('... and then exactly that one is revoked', page.log.revoked, [page.log.urls[0].url]);
    page.context.antsDownloadReplay();
    check('a second click saves it again (the file stays until the next match)', page.log.urls.length === 2 && page.log.links.length === 2);
}

// 3. the sizes read as people read them
{
    const page = fakePage();
    for (const [count, want] of [[500, '500 bytes'], [1023, '1023 bytes'], [1024, '1 KB'], [1500, '1 KB'], [1536, '2 KB'], [204800, '200 KB'], [1048575, '1024 KB'], [1048576, '1.0 MB'], [3 * 1048576 + 600000, '3.6 MB']]) {
        page.context.antsOfferReplay(bytes(count), 'ants-TINY-20261006-143209.antsrep');
        same('a file of ' + count + ' bytes', page.button.textContent, 'Download replay (' + want + ')');
    }
}

// 4. the next match: the game takes the offer back (null, or an empty file), the button goes and a click saves nothing
for (const nothing of [null, undefined, new Uint8Array(0), []]) {
    const page = fakePage();
    page.context.antsOfferReplay(bytes(1000), 'ants-TINY-20261006-143209.antsrep');
    check('(offered first)', page.button.hidden === false);
    page.context.antsOfferReplay(nothing, '');
    check('taking the offer back (' + JSON.stringify(nothing) + ') hides the button', page.button.hidden === true);
    check('... and forgets the file', page.context.antsReplayFile === null);
    const result = page.context.antsDownloadReplay();
    check('... so a click saves nothing: no Blob, no URL, no link', result === false && page.log.blobs.length === 0 && page.log.urls.length === 0 && page.log.links.length === 0);
}

// 5. a second match's file replaces the first's
{
    const page = fakePage();
    const first = bytes(1000);
    const second = bytes(3000);
    page.context.antsOfferReplay(first, 'ants-TINY-20261006-143209.antsrep');
    page.context.antsOfferReplay(null, '');
    page.context.antsOfferReplay(second, 'ants-TINY-20261006-150000.antsrep');
    page.context.antsDownloadReplay();
    check('the click saves the newest file under its own name', page.log.blobs[0].parts[0] === second && page.log.links[0].download === 'ants-TINY-20261006-150000.antsrep');
    same('the button shows the newest size', page.button.textContent, 'Download replay (3 KB)');
}

// 6. the name is safe: nothing of it can be a path, a hidden file or another kind of file
{
    const page = fakePage();
    const names = [
        ['ants-TINY-20261006-143209.antsrep', 'ants-TINY-20261006-143209.antsrep'],
        ['ants-TREASURE_2-20261006-143209-2.antsrep', 'ants-TREASURE_2-20261006-143209-2.antsrep'],
        ['a b.antsrep', 'a_b.antsrep'],
        ['dir/evil.antsrep', 'dir_evil.antsrep'],
        ['..\\evil.antsrep', 'ants-match.antsrep'],
        ['../evil.antsrep', 'ants-match.antsrep'],
        ['.antsrep', 'ants-match.antsrep'],
        ['.hidden.antsrep', 'ants-match.antsrep'],
        ['x.exe', 'ants-match.antsrep'],
        ['x.antsrep.exe', 'ants-match.antsrep'],
        ['', 'ants-match.antsrep'],
        [undefined, 'ants-match.antsrep'],
        [null, 'ants-match.antsrep'],
        [42, 'ants-match.antsrep'],
        ['<script>.antsrep', '_script_.antsrep'],
        ['café.antsrep', 'caf_.antsrep'],
    ];
    for (const [given, want] of names) {
        page.context.antsOfferReplay(bytes(100), given);
        page.context.antsDownloadReplay();
        const link = page.log.links[page.log.links.length - 1];
        check('the name ' + JSON.stringify(given) + ' is saved as ' + JSON.stringify(want), link.download === want, link.download);
        check('... and is only letters, digits, dot, dash and underscore', /^[A-Za-z0-9._-]+$/.test(link.download) && link.download.endsWith('.antsrep') && link.download.charAt(0) !== '.', link.download);
    }
}

// 7. no failure of the page reaches the game
{
    const failing = [
        ['no document at all', { getElementByIdThrows: true }],
        ['no button in the page', { noButton: true }],
    ];
    for (const [label, options] of failing) {
        const page = fakePage(options);
        let thrown = null;
        try { page.context.antsOfferReplay(bytes(100), 'ants-x.antsrep'); page.context.antsOfferReplay(null, ''); } catch (e) { thrown = e; }
        check(label + ': offering and taking back throw nothing', thrown === null, String(thrown));
    }
    const downloads = [
        ['a browser without Blob', { noBlob: true }],
        ['a browser without object URLs', { noUrl: true }],
        ['an object URL that cannot be made', { urlThrows: true }],
        ['a link that cannot be made', { createElementThrows: true }],
        ['a click that is refused', { clickThrows: true }],
    ];
    for (const [label, options] of downloads) {
        const page = fakePage(options);
        page.context.antsOfferReplay(bytes(100), 'ants-x.antsrep');
        let thrown = null;
        let result;
        try { result = page.context.antsDownloadReplay(); } catch (e) { thrown = e; }
        check(label + ': the click throws nothing', thrown === null, String(thrown));
        check(label + ': and says that nothing was saved', result === false, String(result));
        check(label + ': the offer stands, a second try is allowed', page.context.antsReplayFile !== null);
    }
    {   // the page has no `document` global when the block runs (the game is not a page at all): the functions are still harmless
        const context = vm.createContext({ Math, String, RegExp, Object });
        vm.runInContext(block, context);
        let thrown = null;
        try { context.antsOfferReplay(bytes(10), 'ants-x.antsrep'); context.antsDownloadReplay(); context.antsOfferReplay(null, ''); } catch (e) { thrown = e; }
        check('a context without a document: nothing is thrown', thrown === null, String(thrown));
    }
}

// 8. what the page's text says: the block touches nothing outside the button and the download, and sends nothing anywhere
{
    const code = block.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
    check('the block makes no request (no fetch, no XMLHttpRequest, no sendBeacon, no WebSocket)', !/\b(fetch|XMLHttpRequest|sendBeacon|WebSocket)\b/.test(code));
    check('the block stores nothing (no localStorage, sessionStorage, cookie)', !/\b(localStorage|sessionStorage|cookie|indexedDB)\b/.test(code));
    check('the page has this one block and one button', text.split('ANTS_REPLAY_BEGIN').length === 2 && (text.match(/id="replay-download"/g) || []).length === 1);
}

console.log('web_replay_check: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures === 0 ? 0 : 1);

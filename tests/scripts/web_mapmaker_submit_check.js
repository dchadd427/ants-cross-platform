#!/usr/bin/env node
// Send a map for approval, and My submissions (web/front/mapmaker_submit.js and the sent store of mapmaker_store.js; docs/MAP_SUBMISSIONS.md, docs/MAP_EDITOR.md) on the small fake of the DOM
// (tests/scripts/web_mapmaker_fake_dom.js), a small fake of the browser's database and a fake of the server's submission door. No browser: the real page is held to the same calls by the opt-in
// browser check (tests/scripts/test_web_mapmaker.sh).
//
//   node tests/scripts/web_mapmaker_submit_check.js [--front DIR]
//
//   --front DIR  the folder of the mapmaker_*.js files (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//
//   A  the words: the name rules, the text rules, the status link, the sentence for each answer (the answers of nginx are not JSON), the date
//   B  the sent store on a fake database (and a browser that keeps nothing)
//   C  the dialog: the button works only for a map that works, the door that is off, the name that is taken, Extended maps, the words
//   D  what is sent: the address (%20 and %2B), the bytes as they are, the token, the one call, only calls that cannot replace anything, and every answer of the server (201, a refusal
//      with findings, a limit, the HTML pages of nginx, no answer at all)
//   F  My submissions: the four answers, the buttons, the owner's words as text, the refresh, a door that is off, Withdraw, the status link
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const path = require('path');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.resolve(__dirname, '..', '..', 'web', 'front');
const Store = require(path.join(FRONT, 'mapmaker_store.js')), Submit = require(path.join(FRONT, 'mapmaker_submit.js')), Dialog = require(path.join(FRONT, 'mapmaker_dialog.js'));
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
const later = require('./web_mapmaker_clock.js').settle;

const TOKEN = 'A'.repeat(43), TOKEN2 = 'B'.repeat(43), ID1 = 'a'.repeat(32), ID2 = 'b'.repeat(32), ID3 = 'c'.repeat(32), ID4 = 'd'.repeat(32);

// ---- a fake of the browser's database for the sent store ----
function fakeIdb(opts) {
    opts = opts || {};
    const m = new Map(), log = [];
    function objectStore(tx) {
        function job(fn) {
            const r = {};
            tx.pending++;
            setImmediate(() => { try { r.result = fn(); if (r.onsuccess) r.onsuccess(); } catch (e) { r.error = e; if (r.onerror) r.onerror({ preventDefault() {} }); } });
            setImmediate(() => setImmediate(() => { if (--tx.pending === 0 && tx.oncomplete) tx.oncomplete(); }));
            return r;
        }
        return {
            put(rec) { log.push('put ' + rec.id); const copy = structuredClone(rec); return job(() => { if (opts.full) throw Object.assign(new Error('full'), { name: 'QuotaExceededError' }); m.set(rec.id, copy); }); },
            getAll() { return job(() => Array.from(m.values()).map((v) => structuredClone(v))); },
            delete(id) { log.push('delete ' + id); return job(() => { m.delete(id); }); }
        };
    }
    return {
        m, log,
        open(name, version) {
            log.push('open ' + name + ' ' + version);
            if (opts.throws) throw new Error('SecurityError');
            const r = {};
            setImmediate(() => {
                if (opts.error) return r.onerror && r.onerror({});
                r.result = { transaction() { const tx = { pending: 0 }; tx.objectStore = () => objectStore(tx); return tx; }, createObjectStore(n, o) { log.push('create ' + n + ' ' + o.keyPath); } };
                if (r.onupgradeneeded) r.onupgradeneeded();
                r.onsuccess();
            });
            return r;
        }
    };
}

// ---- a fake of the server's door: answers by rule, keeps every call ----
function fakeServer(rules) {
    const calls = [];
    const state = { off: false, token: TOKEN, tokenIsNew: true, list: [], status: null, body: null, taken: false, net: false, answers: {} };
    function reply(status, json, text) { return { status, text: () => Promise.resolve(text !== undefined ? text : JSON.stringify(json)) }; }
    function fetch(url, init) {
        init = init || {};
        const call = { method: init.method || 'GET', url, headers: init.headers || {}, body: init.body, signal: init.signal };
        calls.push(call);
        return new Promise((resolve, reject) => setImmediate(() => {
            if (init.signal) init.signal.addEventListener('abort', () => reject(new Error('aborted')));
            if (state.hang) { state.held = () => answer(resolve, reject); return; }
            answer(resolve, reject);
        }));
        function answer(resolve, reject) {
            if (state.net) return reject(new Error('offline'));
            const key = call.method + ' ' + url.split('?')[0].replace(/[0-9a-f]{32}/, '<id>');
            if (state.answers[key]) { const a = state.answers[key]; return resolve(reply(a[0], a[1], a[2])); }
            if (state.off) return resolve(reply(503, { error: 'submissions_off', message: 'Sending maps is not switched on right now.' }));
            if (key === 'GET /submissions') return resolve(init.headers && init.headers.Authorization ? reply(200, { submissions: state.list }) : reply(401, { error: 'no_token', message: 'Open this page with the link.' }));
            if (key === 'POST /submissions') {
                const out = { submission: { id: ID1, state: 'waiting', kind: /kind=extended/.test(url) ? 'extended' : 'classic', name: decodeURIComponent((/name=([^&]*)/.exec(url) || [])[1] || ''), width: 31, height: 31, sent_at: Math.floor(Date.now() / 1000) }, name_taken: state.taken, token_is_new: state.tokenIsNew };
                if (state.tokenIsNew) out.token = state.token;
                return resolve(reply(201, out));
            }
            if (key === 'POST /submissions/<id>/withdraw') return resolve(reply(200, { submission: { id: ID1, state: 'withdrawn' } }));
            return resolve(reply(404, { error: 'not_found', message: 'There is no such map.' }));
        }
    }
    return { fetch, calls, state };
}

class FakeAbort { constructor() { const l = []; this.aborted = false; this.signal = { addEventListener: (t, f) => l.push(f) }; this.abort = () => { this.aborted = true; l.forEach((f) => f()); FakeAbort.last = this; }; } }

// ---- the page ----
function page(opts) {
    opts = opts || {};
    const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header');
    [host, top].forEach((n) => doc.body.appendChild(n));
    const timers = [], dialogs = Dialog.create(doc, host, [top]), server = opts.server || fakeServer(), idb = opts.idb || fakeIdb(), events = [];
    const sentStore = Store.createSent({ idb: opts.noIdb ? null : idb });
    const report = opts.report || { works: true, headline: 'Works in the original game', findings: [], kind: 'classic' };
    const file = { kind: opts.kind || 'classic', rows: 31, columns: 31, ok: true };
    const bytes = opts.bytes || new Uint8Array([8, 1, 2, 3]), cfg = { bytes };
    const library = Object.assign({ tiny: true, treasure: true }, opts.library || {});
    const env = {
        doc, dialogs, sent: sentStore, fetch: opts.noFetch ? null : server.fetch, later: (f, ms) => (ms >= 30000 ? timers.push(f) : setImmediate(f)), Abort: opts.noAbort ? null : FakeAbort, now: () => opts.now || Date.now(),
        store: { inLibrary: (n) => !!library[Store.key(n)], libraryState: () => opts.libraryState || 'ready', onLibrary: (f) => f() },
        parse: (b) => (b && b[0] === 8 ? { kind: 'classic', rows: 31, columns: 31 } : b && b[0] === 9 ? { kind: 'extended', rows: 300, columns: 400 } : null),
        thumb: () => doc.createElement('canvas'), tags: (h, kind) => { h.textContent = kind === 'extended' ? 'Extended map|New version only' : 'Classic map'; },
        current: () => (opts.currentError ? { error: opts.currentError } : { name: opts.name || 'Sunny Picnic', bytes: cfg.bytes, file, report }),
        showFinding: (f) => events.push(['show', f.row, f.column]), onOpenCopy: (r, again) => events.push(['copy', r.name, !!again]), onSee: () => events.push(['see']),
        clipboard: opts.clipboard, link: () => 'https://example.org/mapmaker.html'
    };
    const sub = Submit.create(env);
    return { cfg, timers, doc, host, dialogs, server, idb, events, sub, bytes, sentStore, env };
}
function button(p, text) { return p.host.querySelectorAll('button').find((b) => b.textContent === text); }
function text(p) { return p.host.textContent; }
function type(p, id, value) { const n = p.host.querySelector('#' + id); n.value = value; n.fire('input', {}); }
function post(server) { return server.calls.filter((c) => c.method === 'POST'); }
async function loadedPage(opts) { const p = page(opts); await new Promise((r) => p.sub.load(r)); return p; }
async function opened(opts) { const p = await loadedPage(opts); p.sub.open(); await later(); return p; }

// ---- A ----
function caseA() {
    equal(Submit.nameProblem('Sunny Picnic'), '', 'a plain name is fine');
    equal(['Hi', 'x'.repeat(61), 'a<b', 'Big Lake.', 'Big..Lake', '..', 'abé', ' ab', '-ab', 'A_b-c! (1), d&e.f'].map((n) => Submit.nameProblem(n) !== ''), [true, true, true, true, true, true, true, true, true, false], 'the name rules of the server');
    equal(Submit.nameProblem('x'.repeat(60)), '', '60 characters are fine');
    equal([Submit.textProblem('Mia', 24, 'player name'), Submit.textProblem('x'.repeat(25), 24, 'player name') !== '', Submit.textProblem('a<b', 24, 'note') !== '', Submit.textProblem('\u{1F600}'.repeat(24), 24, 'note')], ['', true, true, ''], 'the maker name and the note: a length by characters and no < or >');
    // the maker's name and the links, as the server judges them again: the very cases of the server's NM2.5 and NM2.6 (tests/data/maker_name_cases.json)
    const cases = JSON.parse(require('fs').readFileSync(path.join(__dirname, '..', 'data', 'maker_name_cases.json'), 'utf8'));
    const mine = (n) => Submit.makerProblem(Store.clean(n)) || Submit.textProblem(Store.clean(n), 24, 'player name');
    equal(cases.reserved.filter((n) => Submit.makerProblem(Store.clean(n)) === ''), [], 'every name that only the owner or the site may have, a look-alike, a digit-or-accent disguise of one and a bot-like name is refused');
    equal(cases.ok.filter((n) => mine(n) !== ''), [], 'and an honest name (also of two alphabets) is not');
    equal(cases.link.filter((n) => mine(n) === ''), [], 'a web address or three backticks, also behind an invisible character, are refused in a maker name');
    equal(cases.link.filter((n) => Submit.textProblem(Store.clean(n), 280, 'note') === ''), [], 'and in a note');
    equal(cases.no_link.filter((n) => Submit.textProblem(Store.clean(n), 24, 'note') !== ''), [], 'two backticks, a colon and a slash are fine');
    equal(Submit.skeletons('0wn3r 5'), ['wnr', 'owners'], 'the skeletons: digits left out, and digits as the letters they look like');
    check(/cannot be used/.test(Submit.makerProblem('Owner')), 'with a sentence');
    check(/web address/.test(Submit.textProblem('a://b', 24, 'note')), 'and it says why');
    equal(Submit.tokensOf('#sent=' + TOKEN), [TOKEN], 'a status link has a token');
    equal(Submit.tokensOf('#sent=' + TOKEN + ',' + TOKEN2), [TOKEN, TOKEN2], 'or two');
    equal([Submit.tokensOf('#sent=abc'), Submit.tokensOf('#sent='), Submit.tokensOf(''), Submit.tokensOf('#other=' + TOKEN), Submit.tokensOf('#sent=' + TOKEN + '<b>'), Submit.tokensOf('#sent=' + TOKEN + ',zz')], [[], [], [], [], [], [TOKEN]], 'nothing else is a token');
    equal(Submit.statusOf({ state: 'waiting' }), ['warn', 'Waiting for approval'], 'waiting');
    equal(Submit.statusOf({ state: 'approved' }), ['ok', 'Approved'], 'approved');
    equal(Submit.statusOf({ state: 'approved', renamed_by_owner: true, approved_name: 'Big Lake 2' }), ['ok', 'Approved as “Big Lake 2” (renamed by the owner)'], 'approved and renamed by the owner');
    equal(Submit.statusOf({ state: 'approved', renamed_by_owner: false, approved_name: 'Big Lake' }), ['ok', 'Approved'], 'approved with its own name');
    equal(Submit.statusOf({ state: 'declined' })[1], 'Declined', 'declined');
    check(/no longer in the map list/.test(Submit.statusOf({ state: 'taken_down' })[1]), 'taken down');
    check(/^The server did not answer\./.test(Submit.why({ net: true })), 'no answer');
    check(/not switched on/.test(Submit.why({ status: 503, json: { error: 'submissions_off', message: 'x' } })), 'the door is off (503)');
    check(/not switched on/.test(Submit.why({ status: 404, json: null })), 'there is no door (404)');
    check(Submit.isOff({ status: 503, json: { error: 'submissions_off' } }) && !Submit.isOff({ status: 503, json: null }) && !Submit.isOff({ status: 503, json: { error: 'busy' } }), 'only the door that is off is off: a 503 of nginx is a busy server');
    check(/3 maps today/.test(Submit.why({ status: 429, json: { error: 'address_limit', message: 'zzz' } })), 'the daily limit is a plain line');
    check(/too many tries or the server is busy/.test(Submit.why({ status: 429, json: null })) && /too many tries or the server is busy/.test(Submit.why({ status: 503, json: null })), 'the pages of nginx (429, 503) are read by the status');
    check(/too big/.test(Submit.why({ status: 413, json: null })), 'a body that is too large (413 of nginx)');
    equal(Submit.why({ status: 400, json: { error: 'bad_name', message: 'The name is too short (at least 3 characters).' } }), 'The name is too short (at least 3 characters).', 'the server’s own sentence for the rest');
    check(/error 500/.test(Submit.why({ status: 500, json: null })), 'an unknown answer says its status');
    const noon = new Date(2026, 9, 18, 14, 5).getTime();
    equal([Submit.when(noon / 1000, noon), Submit.when(new Date(2026, 9, 15, 9, 0).getTime() / 1000, noon), Submit.when(new Date(2025, 11, 2, 9, 0).getTime() / 1000, noon)], ['today, 14:05', '15 Oct', '2 Dec 2025'], 'the date of a submission');
}

// ---- B ----
async function caseB() {
    const idb = fakeIdb(), s = Store.createSent({ idb });
    equal(await new Promise((r) => s.open(r)), 'ready', 'the sent store opens');
    equal(idb.log.filter((l) => /^(open|create)/.test(l)), ['open ants-mapmaker-sent 1', 'create sent id'], 'in a database of its own (the drafts’ database is never touched)');
    equal(await new Promise((r) => s.put({ id: ID1, name: 'A' }, r)), '', 'a record is put');
    await new Promise((r) => s.put({ id: '#tokens', list: [TOKEN] }, r));
    const all = await new Promise((r) => s.all((e, l) => r([e, l.map((x) => x.id)])));
    equal(all, ['', [ID1, '#tokens']], 'and read back');
    equal(await new Promise((r) => s.remove(ID1, r)), '', 'removed');
    equal(Array.from(idb.m.keys()), ['#tokens'], 'one record only');
    for (const opts of [{ throws: true }, { error: true }, null]) {
        const t = Store.createSent({ idb: opts ? fakeIdb(opts) : null });
        equal(await new Promise((r) => t.open(r)), 'unavailable', 'a browser that keeps nothing: unavailable');
        equal([await new Promise((r) => t.put({ id: 'x' }, r)), await new Promise((r) => t.all((e) => r(e))), await new Promise((r) => t.remove('x', r))], ['failed', 'failed', 'failed'], 'and every call says failed, never throws');
    }
    const full = Store.createSent({ idb: fakeIdb({ full: true }) });
    await new Promise((r) => full.open(r));
    equal(await new Promise((r) => full.put({ id: 'x' }, r)), 'failed', 'a full disk says failed');
}

// ---- C ----
async function caseC() {
    let p = await opened();
    const send = button(p, 'Submit for approval');
    check(p.host.querySelector('h2').textContent === 'Submit for approval', 'the dialog is named');
    check(/Nothing is published until it is approved\. You keep your own copy\./.test(text(p)), 'the line of the picture');
    check(/Send your map to the owner of the game\. If the owner approves it, it joins the map list for everyone\./.test(text(p)), 'what it does');
    check(/Works in the original game/.test(text(p)) && /No map in the list has this name\./.test(text(p)), 'the check and the free name');
    equal(p.host.querySelector('#sub-name').value, 'Sunny Picnic', 'the name is the map’s');
    check(/Note for the owner \(optional\)/.test(text(p)) && p.host.querySelector('#sub-note') !== null, 'the note box');
    check(p.host.querySelector('#sub-maker') !== null && /Player name shown as the maker/.test(text(p)), 'a player who has no name yet is asked for one');
    check(send.disabled, 'no maker name yet: the button waits');
    type(p, 'sub-maker', '  Mia  ');
    check(!send.disabled, 'with a name the button works (the door was asked, and is on)');
    equal(p.server.calls.filter((c) => c.method === 'GET').length, 1, 'the door is asked once, without a token');
    check(p.server.calls[0].headers.Authorization === undefined, 'and the question has no token');
    type(p, 'sub-name', 'ab');
    check(send.disabled && /3 to 60 characters/.test(text(p)), 'a name that is too short stops the button and says why');
    type(p, 'sub-name', 'Big <Lake>');
    check(send.disabled, 'a name with marks the server refuses stops the button');
    type(p, 'sub-name', 'Treasure');
    check(!send.disabled && /already in the game’s map list\. You can still send it\. The owner may change the name or add a number \(for example “Treasure 2”\)\. You will see the final name in My submissions\./.test(text(p)), 'a name that is taken is told, not refused');
    check(p.host.querySelector('.note.warn') !== null && p.host.querySelector('.note.bad') === null, 'as a yellow note and not as an error');
    type(p, 'sub-note', 'a<b');
    check(send.disabled && /cannot have the marks/.test(text(p)), 'a note with < stops the button');
    type(p, 'sub-note', 'see http://example.org');
    check(send.disabled && /web address/.test(text(p)), 'a note with a web address stops the button');
    type(p, 'sub-note', 'x'.repeat(200));
    check(!send.disabled, 'a long note is fine');
    type(p, 'sub-maker', 'Owner');
    check(send.disabled && /cannot be used/.test(text(p)), 'a maker name that only the owner may have stops the button');
    type(p, 'sub-maker', '\u041e' + 'wner');
    check(send.disabled && /cannot be used/.test(text(p)), 'also with a Cyrillic letter in it');
    type(p, 'sub-maker', 'Mia');
    check(!send.disabled, 'and an honest name lets it go');
    // a map that does not work
    p = await opened({ report: { works: false, headline: 'Does not work in the original game', kind: 'classic', findings: [{ severity: 'fatal', message: 'blue has no hill', row: -1 }, { severity: 'fatal', message: 'red has no starting ants (no start marker, tile 155)', row: 4, column: 9 }, { severity: 'note', message: 'a note' }] } });
    check(button(p, 'Submit for approval').disabled, 'a map that does not work cannot be submitted');
    type(p, 'sub-maker', 'Mia');
    check(button(p, 'Submit for approval').disabled, 'whatever else is filled in');
    check(/Does not work in the original game/.test(text(p)) && /Blue has no hill/.test(text(p)) && /Red has no starting ants/.test(text(p)) && !/A note/.test(p.host.querySelector('.subfin').textContent), 'the reasons are listed (what stops a map and what warns, not the notes)');
    check(/A map that does not work cannot be submitted\. Fix these two things in the editor, then come back\./.test(text(p)), 'the line of the picture');
    button(p, 'Show me').click();
    equal(p.events, [['show', 4, 9]], 'Show me goes to the first place that the checker names');
    check(!p.dialogs.isOpen(), 'and the dialog gives way');
    // the door is off: plain state, the rest is there
    const off = fakeServer(); off.state.off = true;
    p = await opened({ server: off });
    type(p, 'sub-maker', 'Mia');
    check(button(p, 'Submit for approval').disabled, 'a door that is off: no button');
    check(/Sending maps is not switched on right now\./.test(text(p)) && p.host.querySelector('.note.bad') === null, 'a plain line, not an error');
    check(p.host.querySelector('#sub-name') !== null && /Works in the original game/.test(text(p)) && button(p, 'Cancel') !== undefined, 'nothing else is hidden');
    const gone = fakeServer(); gone.state.answers['GET /submissions'] = [404, null, '<html>404</html>'];
    p = await opened({ server: gone }); type(p, 'sub-maker', 'Mia');
    check(button(p, 'Submit for approval').disabled && /not switched on/.test(text(p)), 'no door at all (404 of a server with no door) is the same state');
    const down = fakeServer(); down.state.net = true;
    p = await opened({ server: down }); type(p, 'sub-maker', 'Mia');
    check(!button(p, 'Submit for approval').disabled, 'a server that does not answer the question is tried when the player presses the button');
    // Extended
    p = await opened({ kind: 'extended', bytes: new Uint8Array([9, 1, 2]), report: { works: true, headline: 'Works in the new version', kind: 'extended', findings: [] } });
    check(/only players on the newest version will see it in the map list/.test(text(p)), 'an Extended map gets the line about the newest version');
    check(/Extended map\|New version only/.test(text(p)), 'with its tags');
    p = await opened();
    check(!/only players on the newest version/.test(text(p)), 'a Classic map does not');
    // too big
    p = await opened({ bytes: new Uint8Array(512 * 1024 + 1) });
    type(p, 'sub-maker', 'Mia');
    check(button(p, 'Submit for approval').disabled && /too big to send/.test(text(p)), 'a file over the size of its kind is not sent');
    // the map cannot be read
    p = await loadedPage({ currentError: 'Nothing here.' });
    p.sub.open(); check(/Nothing here\./.test(text(p)) && p.host.querySelector('#sub-name') === null, 'a map that cannot be written as a file is said, not sent');
    // a remembered name
    p = await loadedPage();
    p.idb.m.set('#maker', { id: '#maker', name: 'Zed' });
    p = await loadedPage({ idb: p.idb });
    p.sub.open(); await later();
    check(/Maker shown to players Zed/.test(text(p)) && p.host.querySelector('#sub-maker') === null, 'a remembered name is shown with a link to change it');
    button(p, 'Change my player name').click();
    check(p.host.querySelector('#sub-maker').value === 'Zed', 'and changed in the box');
    // XSS: the name is text
    p = await loadedPage({ name: 'x' });
    p.sub.open(); await later();
    type(p, 'sub-maker', 'Mia');
    check(p.host.querySelectorAll('img, script').length === 0, 'no element comes from text');
}

// ---- D and E ----
async function caseD() {
    let p = await opened({ name: 'Big Lake', now: Date.now() });
    type(p, 'sub-maker', 'Zoë & Co'); type(p, 'sub-name', "Big Lake (it's 2 & 2)"); type(p, 'sub-note', 'Line one\nLine two + more');
    const send = button(p, 'Submit for approval');
    send.click(); send.click(); send.click();
    await later();
    const posts = post(p.server);
    equal(posts.length, 1, 'three quick presses are one call');
    const c = posts[0];
    equal(c.url, "/submissions?kind=classic&name=Big%20Lake%20(it's%202%20%26%202)&maker=Zo%C3%AB%20%26%20Co&note=Line%20one%0ALine%20two%20%2B%20more", 'the address: a space is %20, a plus is %2B, an ampersand is %26');
    check(c.body === p.bytes, 'the body is the very bytes that were checked');
    equal(c.headers['Content-Type'], 'application/octet-stream', 'as a file');
    check(c.headers.Authorization === undefined, 'no token the first time');
    check(p.host.querySelector('h2').textContent === 'Submitted' && /Sent\. Waiting for approval\./.test(text(p)) && /“Big Lake \(it's 2 & 2\)” is with the owner now\./.test(text(p)), 'the dialog that says it was sent');
    check(/The owner looks at it and decides\. This can take a while\./.test(text(p)) && /You can see the answer in My submissions\./.test(text(p)), 'with what happens next');
    equal(p.sub.tokens(), [TOKEN], 'the token is kept');
    equal([p.idb.m.get('#tokens').list, p.idb.m.get('#maker').name, p.idb.m.get(ID1).state], [[TOKEN], 'Zoë & Co', 'waiting'], 'in the browser, with the maker’s name and the record');
    check(JSON.stringify(Array.from(p.idb.m.get(ID1).bytes)) === JSON.stringify(Array.from(p.bytes)), 'and the player’s own copy of the map');
    button(p, 'See my submissions').click();
    equal(p.events, [['see']], 'See my submissions goes to the list');
    // the second map uses the token
    p.server.state.tokenIsNew = false; p.cfg.bytes = new Uint8Array([8, 1, 2, 9]);
    p.sub.open(); await later();
    button(p, 'Submit for approval').click(); await later();
    equal(post(p.server)[1].headers.Authorization, 'Bearer ' + TOKEN, 'the next send carries the token');
    check(p.sub.tokens().length === 1, 'and a token that is not new changes nothing');
    // only calls that cannot replace anything
    check(p.server.calls.every((x) => (x.method === 'GET' && x.url === '/submissions') || (x.method === 'POST' && /^\/submissions(\?|$)/.test(x.url))), 'only GET /submissions and POST /submissions were called: nothing can be replaced');
    check(p.server.calls.every((x) => !/^\/(maps|admin)/.test(x.url)), 'and nothing of the owner’s or the lobby’s');
    // an Extended map goes as an Extended map
    const ex = await opened({ kind: 'extended', bytes: new Uint8Array([9, 1, 2]), report: { works: true, headline: 'Works in the new version', kind: 'extended', findings: [] } });
    type(ex, 'sub-maker', 'Mia'); button(ex, 'Submit for approval').click(); await later();
    check(/^\/submissions\?kind=extended&name=Sunny%20Picnic&maker=Mia$/.test(post(ex.server)[0].url), 'the kind of the file is the kind that is sent: ' + post(ex.server)[0].url);
    // name taken answered by the server
    const s2 = fakeServer(); s2.state.taken = true;
    p = await opened({ server: s2 }); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
    check(/is already in the game’s map list\. The owner may change it or add a number\./.test(text(p)), 'the answer says the name is taken (a note, not a fault)');
    // errors
    const cases = [
        [422, { error: 'map_fails_checks', message: 'This map does not pass the check.', findings: [{ severity: 'fatal', message: 'blue has no hill' }] }, /does not pass the check\.[\s\S]*Blue has no hill/],
        [429, { error: 'address_limit', message: 'zzz' }, /already sent 3 maps today/],
        [429, { error: 'queue_full', message: 'zzz' }, /many maps waiting/],
        [429, { error: 'too_many_requests', message: 'zzz' }, /too many tries/],
        [503, { error: 'busy', message: 'zzz' }, /busy checking other maps/],
        [503, { error: 'submissions_off', message: 'x' }, /not switched on right now/],
        [400, { error: 'bad_name', message: 'The name is too short (at least 3 characters).' }, /too short/],
        [413, null, /too big/], [429, null, /too many tries or the server is busy/], [503, null, /too many tries or the server is busy/], [502, null, /error 502/]
    ];
    for (const [status, json, re] of cases) {
        const s = fakeServer();
        s.state.answers['POST /submissions'] = [status, json, json ? undefined : '<html><body>' + status + ' Too Many</body></html>'];
        p = await opened({ server: s }); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
        check(re.test(p.host.querySelector('#sub-err').textContent), 'the answer ' + status + ' ' + (json ? json.error : 'HTML') + ' is said: ' + p.host.querySelector('#sub-err').textContent);
        check(p.dialogs.isOpen() && p.host.querySelector('h2').textContent === 'Submit for approval', 'and the dialog stays for another try');
        check(p.sub.tokens().length === 0 && p.idb.m.size === 0, 'nothing is kept for a map that was not taken');
        if (status === 503 && json && json.error === 'submissions_off') check(button(p, 'Submit for approval').disabled, 'a door that went off stops the button');
        else check(!button(p, 'Submit for approval').disabled, 'the player can try again');
    }
    const n = fakeServer(); n.state.net = true;
    p = await loadedPage({ server: n }); p.server.state.net = false; p.sub.open(); await later(); type(p, 'sub-maker', 'Mia'); p.server.state.net = true;
    button(p, 'Submit for approval').click(); await later();
    check(/server did not answer/.test(p.host.querySelector('#sub-err').textContent), 'no answer at all is said');
    // a browser that keeps nothing: the send works, and the way to find it again is said
    p = await loadedPage({ noIdb: true }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
    check(/Sent\. Waiting for approval\./.test(text(p)) && /cannot remember them, so copy your status link/.test(text(p)) && p.sub.tokens().length === 1, 'a browser that keeps nothing still sends, and says how to find the map again');
    // a page that has no fetch
    p = await loadedPage({ noFetch: true }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
    check(/server did not answer/.test(p.host.querySelector('#sub-err').textContent), 'a page with no fetch says so, and does not break');
}

// ---- F ----
function view(id, state, extra) { return Object.assign({ id, state, kind: 'classic', name: 'Map ' + id.slice(0, 1), width: 31, height: 31, sent_at: 1700000000 }, extra || {}); }
function listUi(p) {
    const list = p.doc.createElement('div'), note = p.doc.createElement('p'), link = p.doc.createElement('button'), said = [], counts = [];
    link.hidden = true;
    return { list, note, link, said, counts, ui: { list, note, link, say: (t) => said.push(t), count: (n) => counts.push(n) } };
}
async function caseF() {
    const server = fakeServer();
    server.state.list = [
        view(ID1, 'waiting', { name: 'Sunny Picnic', sent_at: 1700000400 }),
        view(ID2, 'approved', { name: 'Big Lake', kind: 'extended', width: 400, height: 300, sent_at: 1700000300, renamed_by_owner: true, approved_name: 'Big Lake 2', message: 'The name was taken, so I added a number. <b>Thank you</b>!' }),
        view(ID3, 'approved', { name: 'Hidden Ponds', sent_at: 1700000200 }),
        view(ID4, 'declined', { name: 'Gauntlet practice', sent_at: 1700000100, message: 'Too much like the Gauntlet map.' }),
        view('e'.repeat(32), 'withdrawn', { name: 'Gone' })
    ];
    const idb = fakeIdb();
    idb.m.set('#tokens', { id: '#tokens', list: [TOKEN] });
    [ID1, ID3, ID4].forEach((id, i) => idb.m.set(id, { id, name: 'Old name', kind: 'classic', state: 'waiting', bytes: new Uint8Array([8, 1, i]), sent_at: 1, tok: TOKEN }));
    const p = await loadedPage({ server, idb });
    const u = listUi(p);
    p.sub.show(u.ui);
    check(/An approved map is never replaced\. To change a map, open your own copy, change it and send the changed copy as a new submission\./.test(u.note.textContent), 'the line of the picture');
    check(u.link.hidden === false, 'the status link button shows');
    await later();
    const rows = u.list.children;
    equal(rows.length, 4, 'four rows: a withdrawn one is not shown');
    const t = rows.map((r) => r.textContent);
    check(/Sunny Picnic/.test(t[0]) && /Waiting for approval/.test(t[0]), 'newest first, waiting with its words');
    check(/Big Lake/.test(t[1]) && /Approved as “Big Lake 2” \(renamed by the owner\)/.test(t[1]) && /Extended map\|New version only/.test(t[1]) && /300 x 400/.test(t[1]), 'renamed by the owner, Extended, with its size');
    check(/The owner wrote: The name was taken, so I added a number\. <b>Thank you<\/b>!/.test(t[1]) && rows[1].querySelectorAll('b').length === 0, 'the owner’s words are text, never markup');
    check(/Hidden Ponds/.test(t[2]) && /Approved/.test(t[2]) && !/renamed/.test(t[2]) && !/The owner wrote/.test(t[2]), 'approved');
    check(/Gauntlet practice/.test(t[3]) && /Declined/.test(t[3]) && /The owner wrote: Too much like the Gauntlet map\./.test(t[3]), 'declined with the owner’s reason');
    const buttons = rows.map((r) => r.querySelectorAll('button').map((b) => b.textContent));
    equal(buttons, [['Withdraw'], [], ['Open my copy'], ['Open my copy', 'Change and send again']], 'the buttons of each answer');
    check(rows[0].querySelectorAll('button').length === 1 && rows[0].querySelectorAll('button')[0].textContent === 'Withdraw', 'only a waiting map can be withdrawn');
    check(rows[1].querySelectorAll('button').length === 0, 'a map that came from another device has no copy to open here');
    equal(u.counts[u.counts.length - 1], 4, 'the count of the tab');
    // the server asked with the token, and the records were refreshed in the browser
    check(server.calls.every((c) => c.method === 'GET' && c.url === '/submissions' && c.headers.Authorization === 'Bearer ' + TOKEN), 'one GET /submissions with the token');
    equal([p.idb.m.get(ID1).name, p.idb.m.get(ID4).state, p.idb.m.get(ID2).state], ['Sunny Picnic', 'declined', 'approved'], 'what the server said is kept (the record of a map from another device too)');
    check(p.idb.m.get(ID3).bytes.length === 3, 'and the player’s own copy is kept');
    // the buttons
    rows[2].querySelectorAll('button')[0].click(); equal(p.events.pop(), ['copy', 'Hidden Ponds', false], 'Open my copy');
    rows[3].querySelectorAll('button')[1].click(); equal(p.events.pop(), ['copy', 'Gauntlet practice', true], 'Change and send again opens the copy and says so');
    // Withdraw: a question first, then the one call
    rows[0].querySelectorAll('button')[0].click();
    check(/will not go to the owner\. You keep your own copy/.test(text(p)) && post(server).length === 0, 'Withdraw asks first and sends nothing');
    button(p, 'Keep it waiting').click(); await later();
    equal(post(server).length, 0, 'Keep it waiting changes nothing');
    rows[0].querySelectorAll('button')[0].click(); button(p, 'Withdraw it').click(); await later();
    equal(post(server).map((c) => c.url + '|' + c.headers.Authorization), ['/submissions/' + ID1 + '/withdraw|Bearer ' + TOKEN], 'Withdraw is the one call, with the token');
    equal(u.list.children.length, 3, 'the row goes');
    check(!p.idb.m.has(ID1), 'and the record of this browser');
    // a withdraw that is too late
    server.state.answers['POST /submissions/<id>/withdraw'] = [409, { error: 'not_waiting', message: 'Only a map that is still waiting can be withdrawn.' }];
    server.state.list = [view(ID3, 'waiting', { name: 'Hidden Ponds' })];
    const q = listUi(p); p.sub.show(q.ui); await later();
    q.list.querySelectorAll('button').find((b) => b.textContent === 'Withdraw').click(); button(p, 'Withdraw it').click(); await later();
    check(/already answered/.test(text(p)) && q.list.querySelectorAll('button').some((b) => b.textContent === 'Withdraw'), 'a map that was answered meanwhile is said, and stays');
    // the door is off: what this browser knows is shown, with one plain line
    const off = fakeServer(); off.state.off = true;
    const p2 = await loadedPage({ server: off, idb });
    const w = listUi(p2); p2.sub.show(w.ui); await later();
    check(/not switched on right now, so the owner’s answers cannot be looked up\. Showing what this browser last knew\./.test(w.list.textContent) && w.list.querySelectorAll('.mrow').length >= 3, 'a door that is off: a plain line and the last known list');
    // nothing sent yet
    const p3 = await loadedPage({ idb: fakeIdb() }); const e = listUi(p3); p3.sub.show(e.ui);
    check(/You have not sent a map yet\./.test(e.list.textContent) && p3.server.calls.length === 0, 'nothing sent: a plain line and no call');
    // the status link
    const copied = [];
    const p4 = await loadedPage({ idb, clipboard: { writeText: (u) => { copied.push(u); return Promise.resolve(); } } });
    const l = listUi(p4); p4.sub.show(l.ui); await later(); l.link.fire('click', {}); await later();
    equal(copied, ['https://example.org/mapmaker.html#sent=' + TOKEN], 'Copy my status link: the token is in the fragment');
    check(!/^https?:\/\/[^#]*sent=/.test(copied[0]), 'never in the part that is sent to a server');
    const p5 = await loadedPage({ idb, clipboard: { writeText: () => Promise.reject(new Error('no')) } });
    const l5 = listUi(p5); p5.sub.show(l5.ui); await later(); l5.link.fire('click', {}); await later();
    check(p5.host.querySelector('input') !== null && p5.host.querySelector('input').value === copied[0] && /Anyone who has it can see them and withdraw them/.test(text(p5)), 'a browser that will not copy shows the link to copy by hand, with the warning');
    const p6 = await loadedPage({ idb: fakeIdb() });
    const l6 = listUi(p6); p6.sub.show(l6.ui); l6.link.fire('click', {}); check(/not sent a map from this browser yet/.test(text(p6)), 'no token yet: no link');
    // arriving by a status link
    const p7 = await loadedPage({ idb: fakeIdb() }); let cleared = 0;
    equal([p7.sub.fromAddress('', () => cleared++), p7.sub.fromAddress('#sent=nope', () => cleared++), cleared], [false, false, 0], 'an address with no token does nothing');
    let added = 0;
    const joined = p7.sub.fromAddress('#sent=' + TOKEN2, () => cleared++, () => added++); await later();
    equal([joined, cleared, added, p7.sub.tokens(), p7.idb.m.has('#tokens')], [true, 1, 0, [], false], 'a status link clears the address and asks first: nothing is adopted yet');
    check(p7.dialogs.isOpen() && /This link adds submissions made by someone else to this browser\. Add them\?/.test(text(p7)), 'the question says whose submissions they are');
    button(p7, 'Do not add').click(); await later();
    equal([added, p7.sub.tokens(), p7.idb.m.has('#tokens'), p7.dialogs.isOpen()], [0, [], false, false], 'Do not add changes nothing');
    p7.sub.fromAddress('#sent=' + TOKEN2, () => cleared++, () => added++); await later();
    button(p7, 'Add them').click(); await later();
    equal([added, p7.sub.tokens(), p7.idb.m.get('#tokens').list, p7.idb.m.get('#tokens').linked], [1, [TOKEN2], [TOKEN2], [TOKEN2]], 'Add them joins the token to this browser and remembers that it came from a link');
    p7.server.state.list = [view(ID1, 'approved', { name: 'From elsewhere' })];
    const l7 = listUi(p7); p7.sub.show(l7.ui); await later();
    equal([p7.idb.m.has(ID1), p7.idb.m.get(ID1).linked, /Added from a link/.test(l7.list.textContent)], [true, true, true], 'the submissions of that link are listed and kept, marked as added from a link');
    // a token from a link is never the one a send uses
    p7.sub.open(); await later(); type(p7, 'sub-maker', 'Mia'); button(p7, 'Submit for approval').click(); await later();
    const linkedSend = p7.server.calls.filter((c) => c.method === 'POST').pop();
    check(linkedSend && !linkedSend.headers.Authorization, 'a send from a browser that only has a linked token goes out with no token, so the server makes a fresh one');
    equal(p7.sub.tokens()[0], TOKEN, 'and the token the server made is the first one');
    check(p7.idb.m.get('#tokens').linked.indexOf(TOKEN) < 0, 'and is not marked as linked');
    // the mark survives a reload of the page
    const p7b = await loadedPage({ idb: p7.idb, server: p7.server });
    const l7b = listUi(p7b); p7b.sub.show(l7b.ui); await later();
    equal([p7b.sub.tokens().length, /Added from a link/.test(l7b.list.textContent)], [2, true], 'after a reload the linked submissions are still marked');
    p7b.cfg.bytes = new Uint8Array([8, 9, 9, 9]); p7b.sub.open(); await later(); button(p7b, 'Submit for approval').click(); await later();
    check(p7b.server.calls.filter((c) => c.method === 'POST').pop().headers.Authorization === 'Bearer ' + TOKEN, 'and a send after the reload still uses the browser\'s own token');
    // a browser that has its own token: a link adds a second one behind it, and a send still uses its own
    const p10 = await loadedPage({ idb: fakeIdb() }); p10.idb.m.set('#tokens', { id: '#tokens', list: [TOKEN] });
    const p11 = await loadedPage({ idb: p10.idb });
    p11.sub.fromAddress('#sent=' + TOKEN2, () => {}, () => {}); await later(); button(p11, 'Add them').click(); await later();
    p11.sub.open(); await later(); type(p11, 'sub-maker', 'Mia'); button(p11, 'Submit for approval').click(); await later();
    equal(p11.server.calls.filter((c) => c.method === 'POST').pop().headers.Authorization, 'Bearer ' + TOKEN, 'with its own token and a linked one, a send uses its own');
    // two cold loads with no send between: the stored mark is not rewritten to nothing, and the next send still uses the browser's own token
    const p14i = fakeIdb(); p14i.m.set('#tokens', { id: '#tokens', list: [TOKEN2, TOKEN], linked: [TOKEN2] });
    await loadedPage({ idb: p14i }); await later();
    await loadedPage({ idb: p14i }); await later();
    equal(p14i.m.get('#tokens').linked, [TOKEN2], 'after two page loads the stored linked list is still there');
    const p14 = await loadedPage({ idb: p14i }); p14.server.state.tokenIsNew = false;
    p14.sub.open(); await later(); type(p14, 'sub-maker', 'Mia'); button(p14, 'Submit for approval').click(); await later();
    equal(p14.server.calls.filter((c) => c.method === 'POST').pop().headers.Authorization, 'Bearer ' + TOKEN, 'and the send after the reloads goes out under the own token, not the linked one');
    const p15i = fakeIdb(); p15i.m.set('#tokens', { id: '#tokens', list: [TOKEN2], linked: [TOKEN2] });
    await loadedPage({ idb: p15i }); await later();
    const p15 = await loadedPage({ idb: p15i });
    p15.sub.open(); await later(); type(p15, 'sub-maker', 'Mia'); button(p15, 'Submit for approval').click(); await later();
    check(!p15.server.calls.filter((c) => c.method === 'POST').pop().headers.Authorization, 'a browser that only has a linked token, loaded twice, still sends with no token');
    // a link adds at most 10 tokens
    const many = Array.from({ length: 14 }, (_, i) => String.fromCharCode(97 + i).repeat(43));
    const p16 = await loadedPage({ idb: fakeIdb() });
    p16.sub.fromAddress('#sent=' + many.join(','), () => {}, () => {}); await later(); button(p16, 'Add them').click(); await later();
    equal(p16.sub.tokens().length, 10, 'a link adds at most 10 tokens');
    // a linked token that is kept first: the send and the record use the browser's own token even when the server makes no new one
    const p13i = fakeIdb(); p13i.m.set('#tokens', { id: '#tokens', list: [TOKEN2, TOKEN], linked: [TOKEN2] });
    const p13 = await loadedPage({ idb: p13i }); p13.server.state.tokenIsNew = false;
    p13.sub.open(); await later(); type(p13, 'sub-maker', 'Mia'); button(p13, 'Submit for approval').click(); await later();
    equal([p13.server.calls.filter((c) => c.method === 'POST').pop().headers.Authorization, p13.sub.tokens()[0], p13.idb.m.get(ID1) && p13.idb.m.get(ID1).tok], ['Bearer ' + TOKEN, TOKEN, TOKEN], 'a send answered with no new token is kept under the browser\'s own token');
    // a link whose tokens this browser has already: no question
    let again = 0;
    const p12 = await loadedPage({ idb: p10.idb });
    p12.sub.fromAddress('#sent=' + TOKEN, () => {}, () => again++); await later();
    equal([again, p12.dialogs.isOpen()], [1, false], 'a link with only tokens that are already here asks nothing');
    // XSS in a name from the server
    server.state.list = [view(ID1, 'waiting', { name: '<img src=x onerror=alert(1)>' })];
    const p8 = await loadedPage({ server, idb: fakeIdb() }); p8.idb.m.set('#tokens', { id: '#tokens', list: [TOKEN] });
    const p9 = await loadedPage({ server, idb: p8.idb }); const x = listUi(p9); p9.sub.show(x.ui); await later();
    check(x.list.querySelectorAll('img').length === 0 && /<img src=x/.test(x.list.textContent), 'a name is text');
}

// ---- G: a send that may or may not have arrived, a second send of the same map, a 404 that is not a door ----
async function caseG() {
    check(!Submit.isOff({ status: 404, json: { error: 'not_found', message: 'There is no such map.' } }) && Submit.isOff({ status: 404, json: null }) && Submit.isOff({ status: 404, json: {} }), 'a 404 with an error code of the server is not a door that is off; one without is');
    equal(Submit.why({ status: 404, json: { error: 'not_found', message: 'There is no such map.' } }), 'There is no such map.', 'and says the server\u2019s sentence');
    check(/did not answer in time/.test(Submit.why({ timeout: true })), 'a timeout has its sentence');
    // the same map is not sent twice
    let p = await opened(); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
    button(p, 'Back to the editor').click();
    p.sub.open(); await later();
    check(button(p, 'Submit for approval').disabled && /already waiting for approval/.test(text(p)) && button(p, 'See my submissions') !== undefined, 'a map that is waiting with these very bytes is not sent again: the dialog says so and offers My submissions');
    button(p, 'See my submissions').click();
    equal(p.events.pop(), ['see'], 'which opens it');
    p.dialogs.close();
    const q = await loadedPage({ bytes: new Uint8Array([8, 1, 2, 4]), idb: p.idb }); q.sub.open(); await later();
    check(!button(q, 'Submit for approval').disabled, 'a changed map can be sent');
    // no answer in time: the request is aborted, and the player is told it may have arrived
    const hang = fakeServer(); hang.state.hang = false;
    p = await loadedPage({ server: hang }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia');
    hang.state.hang = true; button(p, 'Submit for approval').click(); await later();
    check(button(p, 'Submit for approval').disabled, 'while it is sent the button is off');
    p.timers.forEach((f) => f()); await later();
    check(FakeAbort.last && FakeAbort.last.aborted, 'after 30 seconds the request is aborted');
    check(/did not answer in time\. The map may have been received: look at My submissions before you send it again\. If this was the first map you sent, this browser cannot find it again/.test(p.host.querySelector('#sub-err').textContent) && button(p, 'See my submissions') !== undefined, 'and the player is told it may have arrived, and what to do');
    check(!button(p, 'Submit for approval').disabled, 'the player may still try again');
    // Escape while it is sent: no second send; the late answer is kept
    const slow = fakeServer();
    p = await loadedPage({ server: slow }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia');
    slow.state.hang = true; button(p, 'Submit for approval').click(); await later();
    p.doc.key('Escape');
    check(!p.dialogs.isOpen(), 'Escape closes the dialog');
    p.sub.open();
    check(/still being sent/.test(text(p)) && post(slow).length === 1, 'but a second send cannot start while one is on its way');
    p.dialogs.close();
    slow.state.hang = false; slow.state.held(); await later();
    check(p.idb.m.has(ID1) && p.sub.tokens().length === 1, 'the late answer is kept (the token and the record)');
    check(/Sent\. Waiting for approval\./.test(text(p)), 'and shown when no other dialog is open');
    p.dialogs.close();
    p.sub.open(); await later();
    check(/already waiting/.test(text(p)), 'and the map is then known as waiting');
    // a first send with no answer says the map may be lost to this browser; later ones do not
    const net = fakeServer();
    p = await loadedPage({ server: net }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia'); net.state.net = true; button(p, 'Submit for approval').click(); await later();
    check(/may have been received/.test(p.host.querySelector('#sub-err').textContent) && /cannot find it again/.test(p.host.querySelector('#sub-err').textContent), 'no answer at all: it may have gone through (first map: and may be lost to this browser)');
    p = await loadedPage({ idb: (() => { const i = fakeIdb(); i.m.set('#tokens', { id: '#tokens', list: [TOKEN] }); return i; })(), server: net }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
    check(/may have been received/.test(p.host.querySelector('#sub-err').textContent) && !/cannot find it again/.test(p.host.querySelector('#sub-err').textContent), 'with a token, the player is only sent to My submissions');
    // an answer that is not what a 201 is
    const odd = fakeServer(); odd.state.answers['POST /submissions'] = [201, null, 'ok'];
    p = await loadedPage({ server: odd }); p.sub.open(); await later(); type(p, 'sub-maker', 'Mia'); button(p, 'Submit for approval').click(); await later();
    check(/may have been received/.test(p.host.querySelector('#sub-err').textContent), 'a 201 without a submission says it may have been sent');
    // the note: more than two line ends in a row become two
    p = await opened(); type(p, 'sub-maker', 'Mia'); type(p, 'sub-note', 'a\n\n\n\n\nb'); button(p, 'Submit for approval').click(); await later();
    check(/&note=a%0A%0Ab$/.test(post(p.server)[0].url), 'three line ends in a row are two: ' + post(p.server)[0].url);
}

(async () => {
    const parts = [['A the words', caseA], ['B the sent store', caseB], ['C the dialog', caseC], ['D what is sent, and the answers', caseD], ['F My submissions', caseF], ['G a send that may have arrived, twins, a 404', caseG]];
    for (const [name, fn] of parts) {
        const was = failures;
        try { await fn(); } catch (e) { failures++; console.log('    ERROR: ' + (e && e.stack || e)); }
        console.log('RUNNING: ' + name + ' ... ' + (failures === was ? 'PASS' : 'FAIL'));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})();

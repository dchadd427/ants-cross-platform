// Runs the code that decides the name a person plays under on the web (web/front/player_name.js): nobody types a name any more; a person who is logged in with Discord plays under the login's
// name made to fit the game's rules, everybody else under a guest name that this browser keeps. The rules of a name are the page's own (the NAME block of web/lobby.html, run here as the page has it).
//   1. fit: the text of a login made to fit (NFKD, only printable ASCII is kept, blanks, 2 to 32 characters, nameCheck), on a table;
//   2. fromMe: what the answer of /hub/me says, in today's shape ({name, mod}) and in the shape with an other name ({name, mod, pick, other, key}), and what it does not say;
//   3. the guest name: six digits, the whole range of 900,000, a name that nameCheck takes as it is and that is never a computer player's, valid and invalid stored values;
//   4. the person (create): one question when the page opens and shared by everybody who waits for it, a login that is asked again at every seat (a log out without a reload gives a guest), a guest
//      or a site without a login that is never asked again, a failed question that is asked again at the next seat and never in a loop, the limit of the wait and the late answer, the guest name that
//      a login and a log out never change, a browser that refuses its storage;
//   5. forWindow: what the page gives it (its fetch, its storage, its crypto and its clock).
// tests/scripts/test_web_player_name.py runs this with node (the quick tier) and the mutants of tests/data/player_name_mutants.json against it.
// usage: node web_player_name_check.js web/front/player_name.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
globalThis.AntsNameLetters = require(require('path').resolve(__dirname, '..', '..', 'web', 'front', 'name_letters.js'));       // (the letters of a name: front/name_letters.js, a script tag of the page before the script that is checked)

const modulePath = process.argv[2];
const lobbyPath = process.argv[3];
if (!modulePath || !lobbyPath) { console.log('usage: web_player_name_check.js player_name.js lobby.html'); process.exit(2); }

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
function between(text, begin, end, file) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(file + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(file + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}
const P = require(require('path').resolve(modulePath));
const nameCheck = new Function(between(fs.readFileSync(lobbyPath, 'utf8'), 'NAME_BEGIN', 'NAME_END', lobbyPath) + '\nreturn nameCheck;')();
const tick = () => new Promise((resolve) => setImmediate(resolve));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. fit
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
check('the module says its page, its key, its limit and its functions', P.ME === '/hub/me' && P.GUEST_KEY === 'ants.guest' && P.ASK_MS === 2000 && ['fit', 'fromMe', 'guestDigits', 'validDigits', 'guestName', 'create', 'forWindow'].every((k) => typeof P[k] === 'function'));
const FIT = [
    // [what the login says, what the page plays under ('' for: it does not fit)]
    ['maya', 'maya'], ['maya_99', 'maya_99'], ['john.doe', 'john.doe'], ['Mary Ann', 'Mary Ann'], ['ab', 'ab'], ['a'.repeat(32), 'a'.repeat(32)],
    ['Jos\u00e9', 'Jos\u00e9'], ['Zo\u00eb', 'Zo\u00eb'], ['\u00dcnal', '\u00dcnal'], ['cr\u00e8me br\u00fbl\u00e9e', 'cr\u00e8me br\u00fbl\u00e9e'], ['\u00d1and\u00fa', '\u00d1and\u00fa'], ['Jos\u00e9'.normalize('NFD'), 'Jos\u00e9'],      // (a letter of the table stays, as UTF-8: the Latin letters of the game's font; a name in NFD is made NFC)
    ['\u0141ukasz', '\u0141ukasz'], ['\u00d8re', '\u00d8re'], ['S\u00f8ren', 'S\u00f8ren'], ['Stra\u00dfe', 'Stra\u00dfe'], ['\u0131\u015f\u0131k', '\u0131\u015f\u0131k'], ['\u0401\u0436\u0438\u043a', '\u0401\u0436\u0438\u043a'], ['\u041c\u0430\u0440\u0438\u044f 42', '\u041c\u0430\u0440\u0438\u044f 42'], ['Ti\u1ebfng Vi\u1ec7t', 'Ti\u1ebfng Vi\u1ec7t'],      // (Latin Extended, Cyrillic and Vietnamese letters, as they are)
    ['\ufb01sh', ''], ['\uff21\uff4c\uff49\uff43\uff45', ''], ['\ud835\udcd0\ud835\udcf7\ud835\udcf7', ''], ['\u2460\u2461', ''], ['\u338f', ''],      // (a ligature, full-width letters, math letters, circled digits and a unit are no letters of the table: the name is refused as a whole, and the other name gets its turn)
    ['Al\u00a0ice', ''], ['Al\u3000ice', ''], ['Al\u2003ice', ''],                                                                         // (a blank other than the plain one is no letter of the table)
    ['\ud83d\ude00Ana\ud83d\ude00', ''], ['Ana\u540d\u524d', ''], ['\u540d\u524d', ''], ['\ud83d\ude00\ud83d\ude00', ''], ['\u0e2a\u0e27\u0e31\u0e2a\u0e14\u0e35', ''], ['\u0645\u0631\u062d\u0628\u0627', ''], ['Ana \u0663', ''], ['\u0393\u03b5\u03b9\u03ac', ''],
    ['\ud835\udc0c\ud835\udc1a\ud835\udc32\ud835\udc1a', ''], ['Maya \ud83d\ude00', ''], ['Maya \u2728', ''], ['Ma\u00aeya', ''], ['Ana\u0301\u0323', ''],      // (math letters, emoji, signs and combining marks: refused)
    ['a\u200bb', ''], ['\u202eAna', ''], ['An\u202ea', ''], ['a\u0000b', ''], ['a\u007fb', ''], ['a\tb', 'a b'], ['a\nb', 'a b'], ['a\r\nb', 'a b'],      // (a control is refused; a tab and a line break are blanks)
    ['  a   b  ', 'a b'], ['a  b   c', 'a b c'], [' ', ''], ['', ''], ['\u00a0\u00a0', ''], ['   ab', 'ab'], ['ab   ', 'ab'],
    ['a'.repeat(32) + ' ', 'a'.repeat(32)], [' ' + 'a'.repeat(32) + ' ', 'a'.repeat(32)], ['a', ''], ['\u00e9', ''], ['\u0141', ''], ['a'.repeat(33), ''], ['a'.repeat(32) + '\u540d', ''], ['\u00e9'.repeat(32), '\u00e9'.repeat(32)], ['\u00e9'.repeat(33), ''],      // (too short, too long: not cut; a letter of two bytes counts one)
    [' '.repeat(50) + 'abc', 'abc'], ['\ud83d\ude00'.repeat(60) + 'abc', ''], ['a'.repeat(1000), ''], [' '.repeat(300) + 'abc', ''], ['x'.repeat(250) + '\u540d'.repeat(10) + 'y', ''],                                                        // (the text is cut before any work: a name hidden behind a long run of blanks is not found)
    ['Bot (Easy)', ''], ['Pip (bot)', ''], ['Pip (bot, Hard)', ''], ['bot(x', ''], ['B o t (', ''], ['Robot (x)', 'Robot (x)'], ['Bot', 'Bot'], ['Botany', 'Botany'], ['Pip (bots)', 'Pip (bots)'],
    ['<script>alert(1)</script>', '<script>alert(1)</script>'], ['a&b', 'a&b'],                                                                              // (text stays text: the page only ever sets it as text)
    [null, ''], [undefined, ''], [42, ''], [{}, ''], [[], ''], [true, '']
];
FIT.forEach(([text, want]) => {
    const shown = String(JSON.stringify(text)).slice(0, 40);
    same('fit(' + shown + ') is ' + JSON.stringify(want), P.fit(text, nameCheck), want);
    if (want !== '') check('... and what it gives is a name that nameCheck takes as it is: ' + JSON.stringify(want), nameCheck(want).ok && nameCheck(want).name === want && want.length >= 2 && want.length <= 32);
});
same('without a check function a fitted text is taken', P.fit('Jos\u00e9', undefined), 'Jos\u00e9');
same('... and the length is held without a check function too: 2 to 32', [P.fit('a'), P.fit('ab'), P.fit('a'.repeat(32)), P.fit('a'.repeat(33))], ['', 'ab', 'a'.repeat(32), '']);
same('a check that refuses what fits refuses it', P.fit('maya', () => ({ ok: false })), '');
same('a check that gives no name refuses it', P.fit('maya', () => ({ ok: true })), '');
same('a check that gives another name has the last word (the page\'s own cutting of blanks)', P.fit('maya', () => ({ ok: true, name: 'May' })), 'May');
check('fit does not throw for a text that is not well formed (a lone surrogate): the name is refused', (() => { try { return P.fit('ab\ud800cd', nameCheck) === ''; } catch (e) { return false; } })());

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. fromMe
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const ME = (body) => P.fromMe(body, nameCheck);
same('today\'s shape: a login with a name that fits', ME({ login: true, you: { name: 'maya', mod: false } }), { kind: 'login', name: 'maya', pass: '', canSwitch: false, viaOther: false });
same('... a moderator is the same', ME({ login: true, you: { name: 'maya', mod: true } }), { kind: 'login', name: 'maya', pass: '', canSwitch: false, viaOther: false });
same('the shape with an other name: the shown name that fits wins, and the hub says that a switch exists', ME({ login: true, you: { name: 'Maya', mod: false, pick: 'display', other: 'maya_99', key: 'k1' } }), { kind: 'login', name: 'Maya', pass: '', canSwitch: true, viaOther: false });
same('... a shown name with an emoji is no name of the table: the other name is used', ME({ login: true, you: { name: 'Maya \u2728', mod: false, pick: 'display', other: 'maya_99', key: 'k1' } }), { kind: 'login', name: 'maya_99', pass: '', canSwitch: true, viaOther: true });
same('... an accented display name is a name of the table as it is', ME({ login: true, you: { name: 'Jos\u00e9', mod: false, pick: 'display', other: 'jose_1' } }), { kind: 'login', name: 'Jos\u00e9', pass: '', canSwitch: true, viaOther: false });
same('... a display name that cannot fit gives way to the username that does', ME({ login: true, you: { name: '\u540d\u524d', mod: false, pick: 'display', other: 'user_name' } }), { kind: 'login', name: 'user_name', pass: '', canSwitch: true, viaOther: true });
same('... the username shown and a display name that fits: the shown one (name) first, whatever the other is', ME({ login: true, you: { name: 'user_name', other: 'Maya' } }), { kind: 'login', name: 'user_name', pass: '', canSwitch: true, viaOther: false });
same('... a shown name that cannot fit and an other name that cannot either: no name (the guest name is used)', ME({ login: true, you: { name: '\u540d\u524d', other: '\ud83d\ude00' } }), { kind: 'login', name: '', pass: '', canSwitch: true, viaOther: false });
same('... a shown name with a letter that the game cannot draw (Greek) gives way to the other name, which is not a remnant', ME({ login: true, you: { name: 'S\u03c3ren', other: 'soren_k' } }), { kind: 'login', name: 'soren_k', pass: '', canSwitch: true, viaOther: true });
same('... a server nickname that cannot fit gives way to the display name that follows it (next), then to the username', ME({ login: true, you: { name: '\u540d\u524d', pick: 'display', other: 'user_name', next: 'Maya' } }), { kind: 'login', name: 'Maya', pass: '', canSwitch: true, viaOther: true });
same('... a nickname and a display name that cannot fit give way to the username', ME({ login: true, you: { name: '\u540d\u524d', pick: 'display', other: 'user_name', next: '\u540d\u524d' } }), { kind: 'login', name: 'user_name', pass: '', canSwitch: true, viaOther: true });
same('... a nickname that fits is used and next is not read', ME({ login: true, you: { name: 'Nick', pick: 'display', other: 'user_name', next: 'Maya' } }), { kind: 'login', name: 'Nick', pass: '', canSwitch: true, viaOther: false });
same('... a shown name that is a computer player\'s gives way to the other name', ME({ login: true, you: { name: 'Bot (Maya)', other: 'maya_99' } }), { kind: 'login', name: 'maya_99', pass: '', canSwitch: true, viaOther: true });
same('... a shown name that is too long once fitted gives way to the other name', ME({ login: true, you: { name: '\ufb03'.repeat(11), other: 'maya_99' } }), { kind: 'login', name: 'maya_99', pass: '', canSwitch: true, viaOther: true });
same('today\'s shape with a name that cannot fit: a login, and no name', ME({ login: true, you: { name: '\u540d\u524d', mod: false } }), { kind: 'login', name: '', pass: '', canSwitch: false, viaOther: false });
same('an other name that is not text, or is empty, is no switch', [ME({ login: true, you: { name: 'maya', other: 7 } }).canSwitch, ME({ login: true, you: { name: 'maya', other: '' } }).canSwitch, ME({ login: true, you: { name: 'maya', other: null } }).canSwitch], [false, false, false]);
same('a name that is not text is no name', ME({ login: true, you: { name: 42, other: 'maya_99' } }).name, 'maya_99');
same('a login with nobody (you is null) is a guest', ME({ login: true, you: null }), { kind: 'guest', name: '', canSwitch: false });
same('a hub that says it has no login', ME({ login: false, you: null }), { kind: 'none', name: '', canSwitch: false });
[null, undefined, 'x', 7, true, [], {}, { login: 'yes' }, { login: 1, you: null }, { login: true }, { login: true, you: 'maya' }, { login: true, you: 7 }, { login: true, you: [] }, { login: true, you: undefined }].forEach((body) => {
    same('an answer that is not what the hub says is failed: ' + String(JSON.stringify(body)), ME(body), { kind: 'failed', name: '', canSwitch: false });
});

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. the guest name
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('the first number is 100000, the last 999999', [P.guestDigits(0), P.guestDigits(0.9999999999)], ['100000', '999999']);
same('the middle of the range', P.guestDigits(0.5), '550000');
[1, 2, -0.1, NaN, Infinity, -Infinity, '0.5', null, undefined, {}, [], true].forEach((v) => same('guestDigits(' + String(v) + ') is a number all the same (the first)', P.guestDigits(v), '100000'));
{
    const seen = new Set();
    let sixDigits = true;
    for (let k = 0; k < 900000; k++) {
        const d = P.guestDigits((k + 0.5) / 900000);
        if (!/^[1-9][0-9]{5}$/.test(d)) sixDigits = false;
        seen.add(d);
    }
    check('every guest number has six digits and none starts with 0', sixDigits);
    check('900,000 guest numbers can be made (one for each step of the range), so two guests rarely share a name', seen.size === 900000, seen.size);
}
{
    let bad = 0, longest = 0, step = 0;
    for (let k = 0; k < 900000; k += 7) {
        const name = P.guestName(P.guestDigits((k + 0.5) / 900000));
        const checked = nameCheck(name);
        if (!checked.ok || checked.name !== name || P.fit(name, nameCheck) !== name || !/^Guest [1-9][0-9]{5}$/.test(name) || /bot/i.test(name)) bad++;
        longest = Math.max(longest, name.length);
        step++;
    }
    check('a guest name is "Guest" and six digits; nameCheck takes it as it is, fit leaves it as it is, and it is never a computer player\'s (' + step + ' of them)', bad === 0, bad + ' that are not');
    check('a guest name is 12 characters, well under the 32 of a name', longest === 12, longest);
}
{
    // a draw of 20,000 from a seeded generator: the numbers are spread (a clash is the birthday sum, about 1 in 100 for a room of 4,000 guests, not of the 2 to 4 of a room)
    let s = 12345;
    const rnd = () => { s = (s + 0x6d2b79f5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
    const names = new Set();
    for (let k = 0; k < 20000; k++) names.add(P.guestDigits(rnd()));
    check('20,000 draws are almost all different (the spread is even: a clash is rare)', names.size >= 19600, names.size);
}
same('guestName puts the word and a blank before the digits', P.guestName('482173'), 'Guest 482173');
[['482173', true], ['100000', true], ['999999', true], ['012345', false], ['12345', false], ['1234567', false], ['48217a', false], [' 482173', false], ['482173 ', false], ['', false], [482173, false], [null, false], [undefined, false], [{}, false], ['4821 3', false], ['\uff14\uff18\uff12\uff11\uff17\uff13', false]].forEach(([v, ok]) => {
    check('validDigits(' + JSON.stringify(v) + ') is ' + ok, P.validDigits(v) === ok);
});

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. the person
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// A scripted browser: the network (every request waits for reply(i, status, body) or fail(i)), a clock, a storage, a generator of numbers, an AbortController. The clock and the generator refuse to be called as methods of
// another object (`this` must be undefined), as a browser's setTimeout and clearTimeout do: the page hands them over as they are
function rig(o) {
    o = o || {};
    const env = { fetches: [], timers: [], clock: 0, store: Object.assign({}, o.store || {}), sets: [], gets: [], randoms: 0, aborted: [], errors: [] };
    let nextTimer = 1;
    const storage = {
        getItem(k) { env.gets.push(k); if (o.getThrows) throw new Error('no storage'); return Object.prototype.hasOwnProperty.call(env.store, k) ? env.store[k] : null; },
        setItem(k, v) { env.sets.push([k, v]); if (o.setThrows) throw new Error('storage is full'); env.store[k] = String(v); },
        removeItem(k) { if (o.setThrows) throw new Error('storage is full'); delete env.store[k]; }
    };
    class FakeAbort { constructor() { this.signal = { aborted: false }; env.aborted.push(this); } abort() { this.signal.aborted = true; } }
    const real = console.error;
    env.pending = [];
    console.error = (e) => { env.errors.push(String(e && e.message || e)); };
    env.restore = () => { console.error = real; };
    const config = {
        fetch: o.noFetch ? null : (url, init) => {
            if (o.fetchThrows) throw new Error('fetch is blocked');
            const call = { url, init };
            env.fetches.push(call);
            return o.notAPromise ? {} : new Promise((resolve, reject) => { call.resolve = resolve; call.reject = reject; });
        },
        later(fn, ms) { if (this !== undefined) throw new TypeError('Illegal invocation'); const id = nextTimer++; env.timers.push({ id, at: env.clock + ms, fn }); return id; },
        cancel(id) { if (this !== undefined) throw new TypeError('Illegal invocation'); if (!o.cancelDoesNothing) env.timers = env.timers.filter((t) => t.id !== id); },
        storage: o.noStorage ? undefined : () => { if (o.storageThrows) throw new Error('storage is blocked'); return storage; },
        random() { if (this !== undefined) throw new TypeError('Illegal invocation'); env.randoms++; return o.random === undefined ? 0.5 : (typeof o.random === 'function' ? o.random(env.randoms) : o.random); },
        Abort: o.noAbort ? undefined : FakeAbort,
        check: nameCheck
    };
    env.who = P.create(config);
    env.advance = (ms) => {
        const until = env.clock + ms;
        for (;;) {
            const due = env.timers.filter((t) => t.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            env.timers = env.timers.filter((t) => t !== due);
            env.clock = Math.max(env.clock, due.at);
            due.fn();
        }
        env.clock = until;
    };
    env.reply = async (i, status, body) => { env.fetches[i].resolve({ ok: status >= 200 && status < 300, status, json: () => (body instanceof Error ? Promise.reject(body) : Promise.resolve(body)) }); await tick(); };
    // the response comes (status 200) and its body is read later: release(i, body) gives the body
    env.replyBodyLater = async (i) => { env.fetches[i].resolve({ ok: true, status: 200, json: () => new Promise((resolve) => { env.pending[i] = resolve; }) }); await tick(); };
    env.release = async (i, body) => { env.pending[i](body); await tick(); };
    env.fail = async (i) => { env.fetches[i].reject(new Error('network down')); await tick(); };
    env.heard = [];
    env.hows = [];
    env.passes = [];
    env.ready = () => env.who.ready((name, kind, canSwitch, how, pass) => { env.heard.push([name, kind, canSwitch]); env.hows.push(how); env.passes.push(pass); });
    return env;
}
const LOGIN = (name, other) => ({ login: true, you: other === undefined ? { name, mod: false } : { name, mod: false, pick: 'display', other, key: 'k' } });
const GUEST_PAGE = { login: true, you: null };
const GUEST_NAME = 'Guest 550000';                      // (the guest name that a generator that says 0.5 makes)

(async () => {
    // -- the question and what waits for it
    {
        const r = rig();
        r.ready();
        check('a seat that waits makes the question, one request', r.fetches.length === 1 && r.heard.length === 0);
        same('the question is the hub\'s page for it: a GET of /hub/me, with the cookie of this site, never from a cache, no redirect followed', [r.fetches[0].url, r.fetches[0].init.credentials, r.fetches[0].init.cache, r.fetches[0].init.redirect, r.fetches[0].init.method], ['/hub/me', 'same-origin', 'no-store', 'error', undefined]);
        check('... with a signal that can end it', !!r.fetches[0].init.signal && r.fetches[0].init.signal.aborted === false);
        r.ready(); r.who.ask();
        check('a seat that waits too, and the page asking again, share the question that is on its way', r.fetches.length === 1);
        await r.reply(0, 200, LOGIN('maya'));
        same('a login that fits: everybody who waits hears its name once, as a login', r.heard, [['maya', 'login', false], ['maya', 'login', false]]);
        check('... the timer of the wait is gone', r.timers.length === 0);
        check('... and no guest name was made for a person who is logged in', r.sets.length === 0 && r.randoms === 0 && r.gets.length === 0);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 200, LOGIN('Jos\u00e9', 'jose_1'));
        same('an accented name is a name as it is, and the hub\'s switch is told on', r.heard, [['Jos\u00e9', 'login', true]]);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 200, LOGIN('\u540d\u524d', 'user_name'));
        same('a display name that cannot fit and a username that does', r.heard, [['user_name', 'login', true]]);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 200, LOGIN('\u540d\u524d', '\ud83d\ude00'));
        same('a login with no name that fits plays as a guest (the guest name, kept)', r.heard, [[GUEST_NAME, 'guest', false]]);
        check('... and asks again at the next seat (it is a login: it can change)', (r.ready(), r.fetches.length === 2));
        r.restore();
    }
    // -- what the card is told besides the name (how): the other name was used, or a guest could be logged in
    for (const [what, body, want] of [['a login whose shown name fits', LOGIN('maya', 'maya_9'), ''], ['today\'s shape, a username', LOGIN('maya'), ''], ['a login whose shown name did not fit: the other name is used', LOGIN('\u540d\u524d', 'user_name'), 'other'],
        ['a login whose shown name has a letter that the game cannot draw: the other name is used', LOGIN('S\u03c3ren', 'soren_k'), 'other'], ['a login with no name that fits (the guest name is used)', LOGIN('\u540d\u524d', '\ud83d\ude00'), ''],
        ['a guest of a hub that has a login (it could log in)', GUEST_PAGE, 'login']]) {
        const r = rig();
        r.ready();
        await r.reply(0, 200, body);
        same('how: ' + what + ' is ' + JSON.stringify(want), r.hows, [want]);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 200, GUEST_PAGE);
        r.ready();
        await r.reply(1, 200, GUEST_PAGE);
        same('how: a guest answer is asked again at the next seat and says the same (a seat that asks later)', r.hows, ['login', 'login']);
        r.restore();
        const none = rig();
        none.ready();
        await none.reply(0, 404, null);
        const failed = rig();
        failed.ready();
        await failed.reply(0, 503, null);
        const given = rig();
        given.ready();
        given.advance(P.ASK_MS);
        same('how: a site with no login, a failed question and an answer given up have no login to offer a guest', [none.hows, failed.hows, given.hows], [[''], [''], ['']]);
        none.restore(); failed.restore(); given.restore();
    }
    // -- a login is asked again at every seat: a log out without a reload
    {
        const r = rig();
        r.who.ask();
        await r.reply(0, 200, LOGIN('maya'));
        check('the page asks when it opens: one request', r.fetches.length === 1);
        same('a login is no answer to use without asking: known() says nothing', r.who.known(), '');
        r.ready();
        check('a seat after a login asks again (the answer may have changed: a log out leaves the page as it is)', r.fetches.length === 2 && r.heard.length === 0);
        await r.reply(1, 200, LOGIN('maya'));
        same('... and the login is still there: its name', r.heard, [['maya', 'login', false]]);
        r.ready();
        check('every seat asks, one request each', r.fetches.length === 3);
        await r.reply(2, 200, GUEST_PAGE);
        same('the person logged out: the next room is a guest\'s, with the guest name', r.heard.slice(-1), [[GUEST_NAME, 'guest', false]]);
        r.ready();
        check('a guest of a site with a login is asked again too: a login may have come meanwhile', r.fetches.length === 4 && r.heard.length === 2);
        await r.reply(3, 200, GUEST_PAGE);
        same('... and is still a guest', [r.heard[2], r.who.known()], [[GUEST_NAME, 'guest', false], '']);
        const none = rig();
        none.ready();
        await none.reply(0, 404, null);
        none.ready();
        same('a site with no login is never asked again: the guest name is known at once', [none.fetches.length, none.heard.length, none.who.known()], [1, 2, GUEST_NAME]);
        none.who.ask();
        check('the page asking again changes nothing for a site with no login', none.fetches.length === 1);
        none.restore();
        r.restore();
    }
    // -- the guest name never changes
    {
        const r = rig({ random: (n) => (n === 1 ? 0.5 : 0.25) });
        r.ready();
        await r.reply(0, 200, GUEST_PAGE);
        r.ready();
        await r.reply(1, 200, GUEST_PAGE);
        same('a guest\'s name is made once', r.heard.slice(0, 1), [[GUEST_NAME, 'guest', false]]);
        same('... and the same at the next seat', r.heard.slice(1), [[GUEST_NAME, 'guest', false]]);
        same('... and kept in this browser under its own key', [r.sets, r.store], [[['ants.guest', '550000']], { 'ants.guest': '550000' }]);
        r.restore();
        const again = rig({ store: r.store, random: 0.9 });
        again.ready();
        await again.reply(0, 200, GUEST_PAGE);
        same('a page that opens later (a reload, another day) has the same guest name', again.heard, [[GUEST_NAME, 'guest', false]]);
        check('... made nothing and wrote nothing', again.sets.length === 0 && again.randoms === 0);
        again.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 200, GUEST_PAGE);
        r.ready();
        await r.reply(1, 200, GUEST_PAGE);
        r.who.known();
        check('the guest name and the name that the guest typed are read from the storage once each for a page, and the number made once', JSON.stringify(r.gets.slice().sort()) === JSON.stringify(['ants.guest', 'ants.guestname']) && r.randoms === 1 && r.sets.length === 1);
        r.restore();
    }
    {
        // a guest, a login (as somebody else), a log out: the same guest name comes back, and the stored one is never written again
        const r = rig();
        r.ready(); await r.reply(0, 200, GUEST_PAGE);
        const first = r.heard[0][0];
        const l = rig({ store: r.store, random: 0.1 });
        l.ready(); await l.reply(0, 200, LOGIN('maya'));
        l.ready(); await l.reply(1, 200, GUEST_PAGE);
        same('a login and a log out leave the guest name as it was', [first, l.heard[0], l.heard[1]], [GUEST_NAME, ['maya', 'login', false], [GUEST_NAME, 'guest', false]]);
        check('... and write nothing', l.sets.length === 0 && l.randoms === 0 && JSON.stringify(l.store) === JSON.stringify(r.store));
        r.restore(); l.restore();
    }
    for (const [what, value] of [['no digits', ''], ['six digits that start with 0', '012345'], ['five digits', '12345'], ['seven digits', '1234567'], ['a word', 'Guest 482173'], ['letters', 'abcdef'], ['a blank', ' 482173']]) {
        const r = rig({ store: { 'ants.guest': value }, random: 0.75 });
        r.ready();
        await r.reply(0, 200, GUEST_PAGE);
        same('a stored value that is ' + what + ' is not kept: a new guest name, stored', [r.heard, r.store['ants.guest']], [[['Guest 775000', 'guest', false]], '775000']);
        r.restore();
    }
    for (const [what, o] of [['reads', { getThrows: true }], ['writes', { setThrows: true }], ['is given at all', { storageThrows: true }], ['is not there', { noStorage: true }]]) {
        const r = rig(o);
        r.ready(); await r.reply(0, 404, null);
        r.ready(); r.ready();
        same('a browser whose storage ' + what + ' still has a guest name, and it stays the same for the page', r.heard, [[GUEST_NAME, 'guest', false], [GUEST_NAME, 'guest', false], [GUEST_NAME, 'guest', false]]);
        check('... and made once', r.randoms === 1);
        r.restore();
    }
    // -- a site that has no login, and the answers that are no answer
    {
        const r = rig();
        r.ready();
        await r.reply(0, 404, 'not found');
        same('a site with no such page (404) has no login: a guest', r.heard, [[GUEST_NAME, 'guest', false]]);
        r.ready(); r.who.ask();
        check('... and is not asked again (no new request for a site that has no login)', r.fetches.length === 1 && r.heard.length === 2 && r.who.known() === GUEST_NAME);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 200, { login: false, you: null });
        r.ready();
        check('a hub that says login:false has none: a guest, and no new request', r.heard.length === 2 && r.heard[1][1] === 'guest' && r.fetches.length === 1);
        r.restore();
    }
    for (const status of [403, 429, 500, 503]) {
        const r = rig();
        r.ready();
        await r.reply(0, status, LOGIN('maya'));
        check('an answer of ' + status + ' is not believed whatever its body says', r.heard.length === 1 && r.heard[0][1] === 'guest' && r.who.known() === '');
        r.restore();
    }
    for (const status of [400, 401, 403, 405, 429, 500, 502, 503, 504]) {
        const r = rig();
        r.ready();
        await r.reply(0, status, { err: 'x' });
        r.ready();
        check('an answer of ' + status + ' is a failed question: a guest for this seat, and the next seat asks again', r.heard.length === 1 && r.heard[0][1] === 'guest' && r.fetches.length === 2 && r.who.known() === '');
        r.restore();
    }
    for (const [what, body] of [['not JSON', new Error('bad json')], ['a string', 'maya'], ['null', null], ['an array', []], ['a login that is not true or false', { login: 'yes' }], ['no you', { login: true }]]) {
        const r = rig();
        r.ready();
        await r.reply(0, 200, body);
        check('a good status with ' + what + ' is a failed question, a guest', r.heard.length === 1 && r.heard[0][1] === 'guest' && r.who.known() === '');
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.fail(0);
        same('a network that fails: a guest at once, and nothing is shown', r.heard, [[GUEST_NAME, 'guest', false]]);
        r.ready();
        check('a failed question is asked again at the next seat, once for that seat (never in a loop)', r.fetches.length === 2 && r.heard.length === 1);
        await r.reply(1, 200, LOGIN('maya'));
        same('... and it can be a login this time', r.heard.slice(-1), [['maya', 'login', false]]);
        r.restore();
    }
    {
        const r = rig({ noFetch: true });
        r.ready();
        same('a browser with no fetch: a guest at once, no request, no timer', [r.heard, r.fetches.length, r.timers.length], [[[GUEST_NAME, 'guest', false]], 0, 0]);
        r.restore();
    }
    {
        const r = rig({ fetchThrows: true });
        r.ready();
        same('a fetch that throws: a guest at once', [r.heard, r.timers.length], [[[GUEST_NAME, 'guest', false]], 0]);
        r.restore();
    }
    {
        const r = rig({ notAPromise: true });
        r.ready();
        same('a fetch that gives no promise: a guest at once', r.heard, [[GUEST_NAME, 'guest', false]]);
        r.restore();
    }
    // -- the limit of the wait
    {
        const r = rig();
        r.ready();
        r.advance(P.ASK_MS - 1);
        check('a seat waits for the answer up to the limit', r.heard.length === 0 && r.fetches[0].init.signal.aborted === false);
        r.advance(1);
        same('... then goes on as a guest, and the request is given up', [r.heard, r.fetches[0].init.signal.aborted], [[[GUEST_NAME, 'guest', false]], true]);
        await r.reply(0, 200, LOGIN('maya'));
        check('an answer that comes after the limit is not heard (the seat has gone on) and changes nothing', r.heard.length === 1 && r.who.known() === '');
        r.ready();
        check('the next seat asks again', r.fetches.length === 2 && r.heard.length === 1);
        await r.reply(1, 200, LOGIN('maya'));
        same('... and hears a login that comes in time', r.heard.slice(-1), [['maya', 'login', false]]);
        r.restore();
    }
    {
        const r = rig({ noAbort: true });
        r.ready();
        r.advance(P.ASK_MS);
        check('a browser with no AbortController waits for the same time and goes on as a guest', r.heard.length === 1 && r.heard[0][1] === 'guest' && r.fetches[0].init.signal === undefined);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        r.advance(P.ASK_MS - 10);
        await r.reply(0, 200, LOGIN('maya'));
        r.advance(100);
        check('an answer in time stops the timer: nothing else is said at the limit', r.heard.length === 1 && r.heard[0][1] === 'login' && r.timers.length === 0);
        r.restore();
    }
    // -- an answer that comes late changes nothing: not the seat that went on, not the next question
    {
        const r = rig();
        r.ready();
        r.advance(P.ASK_MS);
        await r.reply(0, 200, GUEST_PAGE);
        r.ready();
        check('a guest answer that comes after the limit is not taken either: the next seat asks again', r.fetches.length === 2 && r.heard.length === 1 && r.who.known() === '');
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        r.advance(P.ASK_MS);
        r.ready();
        check('after the limit a new seat makes a new question', r.fetches.length === 2 && r.heard.length === 1);
        await r.fail(0);
        check('... the late failure of the first does not end the second', r.heard.length === 1);
        await r.reply(0, 200, LOGIN('late'));
        check('... nor does its late answer', r.heard.length === 1);
        await r.reply(1, 200, LOGIN('maya'));
        same('... the second has its own answer', r.heard.slice(1), [['maya', 'login', false]]);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        r.advance(P.ASK_MS);
        r.ready();
        await r.reply(0, 404, 'not found');
        check('the late "no such page" of the first does not tell the second that the site has no login', r.heard.length === 1 && r.who.known() === '');
        await r.reply(1, 200, LOGIN('maya'));
        same('... the second has its own answer, a login', r.heard.slice(1), [['maya', 'login', false]]);
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.replyBodyLater(0);
        r.advance(P.ASK_MS);
        r.ready();
        await r.release(0, LOGIN('late'));
        check('the body of a late answer (the response came in time, its text did not) does not end the next question', r.heard.length === 1 && r.heard[0][1] === 'guest');
        await r.reply(1, 200, LOGIN('maya'));
        same('... the next question has its own answer', r.heard.slice(1), [['maya', 'login', false]]);
        r.restore();
    }
    {
        const r = rig({ cancelDoesNothing: true });
        r.ready();
        await r.reply(0, 200, LOGIN('maya'));
        r.advance(100);
        r.ready();
        r.advance(P.ASK_MS - 101);
        check('a timer that could not be taken back does not end a question that began later (it is the old question\'s, and its time has not come)', r.heard.length === 1 && r.fetches.length === 2);
        r.advance(1);
        check('... the old timer says nothing at its time', r.heard.length === 1);
        r.advance(100);
        same('... the new question\'s own limit still ends it', r.heard.length, 2);
        r.restore();
    }
    {
        const r = rig({ cancelDoesNothing: true });
        r.ready();
        await r.reply(0, 404, null);
        r.advance(P.ASK_MS);
        same('a timer that could not be taken back does not undo the answer of a site with no login', [r.who.known(), r.heard.length], [GUEST_NAME, 1]);
        r.restore();
    }
    // -- the callbacks
    {
        const r = rig();
        r.who.ready(() => { throw new Error('a page\'s own mistake'); });
        r.ready();
        await r.reply(0, 200, GUEST_PAGE);
        check('a seat\'s code that throws does not stop the others that wait, and is reported', r.heard.length === 1 && r.errors.length === 1 && /own mistake/.test(r.errors[0]));
        r.restore();
    }
    {
        const r = rig();
        r.ready();
        await r.reply(0, 404, null);
        r.who.ready(() => { throw new Error('later mistake'); });
        r.ready();
        check('... also for a seat that is answered at once', r.heard.length === 2 && r.errors.length === 1);
        r.restore();
    }
    {
        const r = rig();
        const order = [];
        r.who.ready(() => order.push('a')); r.who.ready(() => order.push('b')); r.who.ready(() => order.push('c'));
        await r.reply(0, 200, GUEST_PAGE);
        same('they hear in the order they came', order, ['a', 'b', 'c']);
        r.restore();
    }
    {
        const r = rig();
        let inner = null;
        r.who.ready(() => { r.who.ready((n) => { inner = n; }); });
        await r.reply(0, 404, null);
        check('a seat asked for inside an answer (a site with no login) is answered at once, not lost', inner === GUEST_NAME);
        r.restore();
    }

    // -- a seat never plays under a guest name that an older answer gave while the site has a login (Quick host, Join, Quick join, a rematch all ask through ready)
    {
        // (a) the page opens while the hub says nobody is logged in, the person logs in without a reload, then a seat
        const r = rig();
        r.who.ask();
        await r.reply(0, 200, GUEST_PAGE);
        same('(a) a guest answer is no name to use without asking: known() says nothing', r.who.known(), '');
        r.ready();
        check('(a) a seat after a guest answer asks again', r.fetches.length === 2 && r.heard.length === 0, r.fetches.length + ' requests, ' + r.heard.length + ' heard');
        await r.reply(1, 200, LOGIN('maya'));
        same('(a) ... and plays under the login that came meanwhile', r.heard, [['maya', 'login', false]]);
        r.ready();
        await r.reply(2, 200, GUEST_PAGE);
        r.ready();
        check('(a) a person who stays a guest on a site with a login is asked at every seat (and still gets the guest name)', r.fetches.length === 4 && r.heard.length === 2 && r.heard[1][1] === 'guest', r.fetches.length + ' requests');
        r.restore();
    }
    {
        // (c) a slow hub: the question at page open is given up at the limit; the next seat asks again and hears a login that comes in time
        const r = rig();
        r.who.ask();
        r.advance(P.ASK_MS);
        await r.reply(0, 200, LOGIN('maya'));
        same('(c) a slow hub: nothing is known at page open', r.who.known(), '');
        r.ready();
        check('(c) the seat asks again', r.fetches.length === 2);
        await r.reply(1, 200, LOGIN('maya'));
        same('(c) ... and plays under the login', r.heard, [['maya', 'login', false]]);
        const down = rig();
        down.ready();
        await down.fail(0);
        same('a hub that is down: a guest plays at once (no wait for the limit)', down.heard, [[GUEST_NAME, 'guest', false]]);
        down.restore(); r.restore();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // 5. forWindow
    // -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const calls = [];
        const saved = {};
        const timers = [], cancelled = [];
        const win = {
            fetch(url, init) { calls.push([url, init]); return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(GUEST_PAGE) }); },
            localStorage: { getItem: (k) => (k in saved ? saved[k] : null), setItem: (k, v) => { saved[k] = v; } },
            AbortController: function () { this.signal = { aborted: false }; this.abort = function () { this.signal.aborted = true; }; }
        };
        const heard = [];
        const who = P.forWindow(win, nameCheck, { later: function (fn, ms) { if (this !== undefined) throw new TypeError('Illegal invocation'); timers.push(ms); return timers.length; }, cancel: function (handle) { if (this !== undefined) throw new TypeError('Illegal invocation'); cancelled.push(handle); }, random: function () { if (this !== undefined) throw new TypeError('Illegal invocation'); return 0.25; } });
        who.ready((n, k) => heard.push([n, k]));
        await tick();
        check('a page\'s window gives the question the AbortController of its browser: a signal that ends it', !!calls[0][1].signal && calls[0][1].signal.aborted === false);
        same('a page\'s window: its fetch asks /hub/me, its generator makes the guest name, its storage keeps it, its clock limits the wait', [calls[0][0], heard, saved, timers], ['/hub/me', [['Guest 325000', 'guest']], { 'ants.guest': '325000' }, [P.ASK_MS]]);
        same('... and its own clock ends it: the answer hands the page\'s cancel the handle that the page\'s later returned (not the global clearTimeout)', cancelled, [1]);
        same('... its window\'s own fetch is called as a method of the window', (() => { let me = null; const w2 = Object.assign({}, win, { fetch: function () { me = this; return new Promise(() => {}); } }); P.forWindow(w2, nameCheck, { later: () => 1, cancel: () => {} }).ask(); return me === w2; })(), true);
        const login = Object.assign({}, win, { localStorage: { getItem: () => null, setItem: () => {} }, fetch: () => Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(LOGIN('Bot (Maya)', 'maya_99')) }) });
        const heardLogin = [];
        P.forWindow(login, nameCheck, { later: () => 1, cancel: () => {} }).ready((n, k) => heardLogin.push([n, k]));
        await tick();
        same('the page\'s own rules of a name are the ones that fit uses (a computer player\'s name gives way to the other name)', heardLogin, [['maya_99', 'login']]);
        const bare = { fetch: null, localStorage: undefined };
        const heard2 = [];
        const realRandom = Math.random;
        Math.random = () => 0.75;
        try { P.forWindow(bare, nameCheck, { later: () => 1, cancel: () => {} }).ready((n, k) => heard2.push([n, k])); } finally { Math.random = realRandom; }
        same('a window with no fetch and no storage, and a page that passes no generator, still gives a guest name (the browser\'s own, the name for the page only)', heard2, [['Guest 775000', 'guest']]);
        const timed = [];
        const who3 = P.forWindow({ fetch: () => new Promise(() => {}) }, nameCheck);
        who3.ready((n, k) => timed.push([n, k]));
        check('without a clock of its own it uses the global one (a page that does not pass its own)', timed.length === 0 && typeof setTimeout === 'function');
    }

    // ---- the pass of a name and the name that a guest typed (protocol 29, docs/HUB_NICKNAMES.md) ----
    {
        const PASS = '1700000120.' + 'a1b2c3d4'.repeat(4);
        const play = (name, pass) => ({ login: true, you: { name: 'maya', mod: false }, play: { name, pass } });
        same('fromMe: the name that the hub plays the person under and its pass are what the page takes (a nickname with its tag)', ME(play('[ANT] Zoe', PASS)), { kind: 'login', name: '[ANT] Zoe', pass: PASS, canSwitch: false, viaOther: false });
        same('... a pass of another shape (not digits, a dot and 32 hex digits) is none, and so is a pass that is not text', [ME(play('Zoe', '1700000120.xyz')).pass, ME(play('Zoe', 'a'.repeat(80))).pass, ME(play('Zoe', 5)).pass, ME(play('Zoe', undefined)).pass, ME(play('Zoe', PASS.toUpperCase())).pass], ['', '', '', '', '']);
        same('... a play name that does not fit the game is no name (the guest name is used) and gives no pass', ME(play('\u540d\u524d', PASS)), { kind: 'login', name: '', pass: '', canSwitch: false, viaOther: false });
        same('... a play name with blanks at its ends is cut, and then it is not the name that the pass was made for: no pass', ME(play('  Zoe ', PASS)).pass, '');
        same('... a hub that says no play (an older one) plays the person under the name of the login, with no pass', ME({ login: true, you: { name: 'maya', mod: false } }).pass, '');
        const r = rig();
        r.ready();
        await r.reply(0, 200, { login: true, you: { name: 'maya', mod: false }, play: { name: 'Zoe Q', pass: PASS } });
        same('the person hands the page the name and the pass', [r.heard, r.passes], [[['Zoe Q', 'login', false]], [PASS]]);
        r.restore();
    }
    {
        const r = rig();
        check('a guest has typed nothing: the typed name is none and the name shown is the number', r.who.typed() === '' && r.who.guestShown() === GUEST_NAME);
        same('a name that a guest typed is kept in this browser under its own key, and shown with " (Guest)" after it', [r.who.setTyped('Wren'), r.who.typed(), r.who.guestShown(), r.store['ants.guestname']], [true, 'Wren', 'Wren (Guest)', 'Wren']);
        same('a name that is too short, too long, with a blank at an end or with a letter that the game cannot draw is not kept', [r.who.setTyped('Wr'), r.who.setTyped('a'.repeat(17)), r.who.setTyped(' Wren'), r.who.setTyped('Wren '), r.who.setTyped('Wr\u03c3en'), r.who.typed()], [false, false, false, false, false, 'Wren']);
        same('a name of 16 letters of the table is kept (a letter of two bytes counts one)', [r.who.setTyped('\u00e9'.repeat(16)), r.who.typed()], [true, '\u00e9'.repeat(16)]);
        same('an empty text lets the name go: the number is shown again, and the key is removed', [r.who.setTyped(''), r.who.typed(), r.who.guestShown(), 'ants.guestname' in r.store], [true, '', GUEST_NAME, false]);
        r.restore();
        const kept = rig({ store: { 'ants.guestname': 'Wren' } });
        same('a page that opens in a browser that has the name shows it from the first call', [kept.who.typed(), kept.who.guestShown()], ['Wren', 'Wren (Guest)']);
        kept.restore();
        const junk = rig({ store: { 'ants.guestname': 'x' } });
        const junk2 = rig({ store: { 'ants.guestname': 'a'.repeat(40) } });
        same('a stored text that is no name is none (a short one, a long one)', [junk.who.typed(), junk2.who.typed()], ['', '']);
        junk.restore(); junk2.restore();
        const none = rig({ noStorage: true });
        same('a browser that keeps nothing keeps the name for the page', [none.who.setTyped('Wren'), none.who.typed(), none.who.guestShown()], [true, 'Wren', 'Wren (Guest)']);
        none.restore();
        const refuses = rig({ setThrows: true, getThrows: true });
        same('a browser that throws on its storage still shows and keeps the name for the page', [refuses.who.typed(), refuses.who.setTyped('Wren'), refuses.who.typed()], ['', true, 'Wren']);
        refuses.restore();
    }
    {
        const r = rig({ store: { 'ants.guestname': 'Wren' } });
        r.ready();
        await r.reply(0, 200, GUEST_PAGE);
        same('a guest who typed a name is heard as that name with " (Guest)" (and no pass: the server makes every name without one a guest\'s)', [r.heard, r.passes], [[['Wren (Guest)', 'guest', false]], ['']]);
        r.restore();
    }

    // ---- END ----
    console.log('PlayerName: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });

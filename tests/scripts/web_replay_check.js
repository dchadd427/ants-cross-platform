// Runs the code that the two replay pages share (web/replay_page.js: the matches that run now, web/live.html, and the game page in replay mode, web/shell.html) on tables:
//   - the address of a replay (?replay=<file>): only the shape of the names that the server's store makes, which docker/nginx.conf lets through, is ever asked for (no path, no other ending);
//   - the time of a match in the visitor's own time zone ("Today, 7:32 AM", "Yesterday, ...", "Oct 6, ..."), the length of a match as a clock, the six maps' names;
//   - the players as the list gives them ("Green (Mika)", "Red (Bot (Medium))") and as the game reports them (the roster's bits and the four seats' names);
//   - the chips of a match are built from text only: a name with < > & " is never markup;
//   - a match that is being played (?live=<id>): the id's shape (the page asks the server for nothing else), when it began, the door's answers, whether the playhead is at the present, the words of the tag
//     over the picture, and the list of live matches as the page takes it (an entry that is not of the shape, a name that is not text, a number that is not one).
//   - the camera button of the game page (Auto camera / Manual camera): what the device chose and keeps (a storage that throws changes nothing), the button's words, the note that comes once in a visit when the
//     viewer takes the camera, and that taking it is not a choice that is remembered.
// tests/scripts/test_web_replay.py runs this with node (the quick tier). usage: node web_replay_check.js web/replay_page.js    (exit 0: every check holds; every failure is printed)
'use strict';
process.env.TZ = 'America/Los_Angeles';                      // (the table below is in the visitor's own zone: the code takes whatever zone it runs in, and a fixed one makes the day boundaries a fact)
const path = process.argv[2];
if (!path) { console.log('usage: web_replay_check.js replay_page.js'); process.exit(2); }
const R = require(require('path').resolve(path));

let checks = 0, failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got), b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}

// ---- the address
const good = 'ants-TREASURE-20261008-143200Z.antsrep';
same('a file', R.fileOf('?replay=' + good), good);
same('a file among other parameters', R.fileOf('?aspect=16:9&replay=' + good + '&x=1'), good);
same('a file with a counter', R.fileOf('?replay=ants-TINY-20261008-143200Z-12.antsrep'), 'ants-TINY-20261008-143200Z-12.antsrep');
same('a map name with an underscore and digits', R.fileOf('?replay=ants-MY_MAP2-20261008-143200Z.antsrep'), 'ants-MY_MAP2-20261008-143200Z.antsrep');
for (const bad of ['', '?', '?replay=', '?replay=x', '?replay=../' + good, '?replay=/' + good, '?replay=' + good + '/', '?replay=%2e%2e%2f' + good,
                   '?replay=ants-TINY-20261008-143200Z.antsrep.txt', '?replay=ants-TINY-20261008-143200.antsrep', '?replay=ants--20261008-143200Z.antsrep',
                   '?replay=ants-TI-NY-20261008-143200Z.antsrep', '?replay=ants-TINY-20261008-143200Z-12345.antsrep', '?replay=ants-' + 'A'.repeat(25) + '-20261008-143200Z.antsrep',
                   '?replay=' + good + '%0a', '?replay=http://evil.example/' + good, '?replay=ants-TINY-20261008-143200Z.antsrep&replay=../x', '?replay=javascript:alert(1)']) {
    if (bad.indexOf('&replay=../x') !== -1) { same('the first of two ' + JSON.stringify(bad), R.fileOf(bad), 'ants-TINY-20261008-143200Z.antsrep'); continue; }
    same('refused ' + JSON.stringify(bad), R.fileOf(bad), null);
}
same('no string', R.fileOf(undefined), null);
check('the pattern is anchored', !R.FILE_RE.test('x' + good) && !R.FILE_RE.test(good + 'x') && !R.FILE_RE.test(good + '\n'));

// ---- when it ended (seconds since 1970, UTC, from the stamp in the name)
same('the stamp of a name', R.endedOf(good), Date.UTC(2026, 9, 8, 14, 32, 0) / 1000);
same('a counter changes nothing', R.endedOf('ants-TINY-20261008-143200Z-3.antsrep'), Date.UTC(2026, 9, 8, 14, 32, 0) / 1000);
same('no stamp', R.endedOf('nonsense'), null);
same('not a string', R.endedOf(7), null);

// ---- the day and the time, in the visitor's own zone
const now = new Date(2026, 9, 8, 12, 0, 0);                    // Oct 8, 12:00 PM
const at = (y, mo, d, h, mi) => new Date(y, mo, d, h, mi, 0).getTime() / 1000;
same('today morning', R.whenText(at(2026, 9, 8, 7, 32), now), 'Today, 7:32 AM');
same('today at noon', R.whenText(at(2026, 9, 8, 12, 0), now), 'Today, 12:00 PM');
same('today just after midnight', R.whenText(at(2026, 9, 8, 0, 5), now), 'Today, 12:05 AM');
same('today late (in the future of "now" by a minute: still today)', R.whenText(at(2026, 9, 8, 12, 1), now), 'Today, 12:01 PM');
same('yesterday evening', R.whenText(at(2026, 9, 7, 22, 15), now), 'Yesterday, 10:15 PM');
same('yesterday at 11:59 PM', R.whenText(at(2026, 9, 7, 23, 59), now), 'Yesterday, 11:59 PM');
same('two days ago', R.whenText(at(2026, 9, 6, 11, 30), now), 'Oct 6, 11:30 AM');
same('last month', R.whenText(at(2026, 8, 20, 9, 5), now), 'Sep 20, 9:05 AM');
same('last year', R.whenText(at(2025, 11, 31, 20, 0), now), 'Dec 31, 2025, 8:00 PM');
same('yesterday across a month end', R.whenText(at(2026, 8, 30, 18, 0), new Date(2026, 9, 1, 8, 0, 0)), 'Yesterday, 6:00 PM');
same('yesterday across a year end', R.whenText(at(2025, 11, 31, 18, 0), new Date(2026, 0, 1, 8, 0, 0)), 'Yesterday, 6:00 PM');
same('across the autumn clock change (Nov 1 2026)', R.whenText(at(2026, 9, 31, 20, 0), new Date(2026, 10, 1, 9, 0, 0)), 'Yesterday, 8:00 PM');
same('no time', R.whenText(undefined, now), '');
same('not a number', R.whenText(NaN, now), '');

// ---- the length
same('0 s', R.lengthText(0), '0:00');
same('7 s', R.lengthText(7), '0:07');
same('59.9 s', R.lengthText(59.9), '0:59');
same('12 minutes', R.lengthText(720), '12:00');
same('an hour and a bit', R.lengthText(3665), '61:05');
same('negative', R.lengthText(-4), '0:00');
same('junk', R.lengthText('abc'), '0:00');

// ---- the maps
for (const [file, key, title] of [['TREASURE.LVL', 'treasure', 'Treasure'], ['treasure', 'treasure', 'Treasure'], ['Tiny.lvl', 'tiny', 'Tiny'], ['SMALL', 'small', 'Small'], ['MEDIUM.LVL', 'medium', 'Medium'],
                                  ['GAUNTLET.LVL', 'gauntlet', 'Gauntlet'], ['ISLANDS', 'islands', 'Islands'], ['MY_MAP.LVL', '', 'MY_MAP'], ['', '', ''], ['constructor', '', 'constructor'], ['__proto__', '', '__proto__']]) {
    same('map key of ' + JSON.stringify(file), R.mapKey(file), key);
    same('map title of ' + JSON.stringify(file), R.mapTitle(file), title);
}
same('map key of nothing', R.mapKey(null), '');
same('map title of nothing', R.mapTitle(undefined), '');

// ---- the players
same('a person', R.parsePlayer('Green (Mika)'), { color: 'Green', name: 'Mika' });
same('a computer player', R.parsePlayer('Red (Bot (Medium))'), { color: 'Red', name: 'Bot (Medium)' });
same('nobody typed anything', R.parsePlayer('Blue'), { color: 'Blue', name: '' });
same('a name with a bracket and a space', R.parsePlayer('Black (a (b) c)'), { color: 'Black', name: 'a (b) c' });
same('an empty name in brackets', R.parsePlayer('Green ()'), { color: 'Green', name: '' });
same('junk', R.parsePlayer(42), { color: '', name: '' });
same('the roster 0b0101 with names', R.playersOf(5, ['Mika', '', 'Bot (Easy)', '']), ['Green (Mika)', 'Blue (Bot (Easy))']);
same('the roster 0b1111 without names', R.playersOf(15, []), ['Green', 'Red', 'Blue', 'Black']);
same('the roster 0b1010', R.playersOf(10, ['x', 'Pia', 'y', 'Bot (Hard)']), ['Red (Pia)', 'Black (Bot (Hard))']);
same('the roster 0', R.playersOf(0, ['a', 'b', 'c', 'd']), []);
same('names that are not strings', R.playersOf(3, [7, null]), ['Green', 'Red']);
same('no names at all', R.playersOf(1, null), ['Green']);

// ---- the chips: a small DOM that records what was set and how
function fakeDoc() {
    const make = (tag) => ({ tag, children: [], attrs: {}, className: '', title: '', _text: '', _html: 0,
        appendChild(c) { this.children.push(c); return c; }, setAttribute(k, v) { this.attrs[k] = v; },
        set textContent(v) { this._text = String(v); }, get textContent() { return this._text; },
        set innerHTML(v) { this._html++; }, get innerHTML() { return ''; } });
    return { createElement: make };
}
const doc = fakeDoc();
const nasty = '<img src=x onerror=alert(1)> & "q" \'s\'';
const list = R.chipsOf(doc, ['Green (Mika)', 'Red (Bot (Medium))', 'Blue', 'Black (' + nasty + ')', 'Purple (odd)']);
same('a list of five chips', list.children.length, 5);
same('the list is labelled', list.attrs['aria-label'], 'Who played');
const chip = (i) => list.children[i];
same('color of the first', chip(0).attrs['data-c'], 'Green');
same('name of the first', chip(0).children[1]._text, 'Mika');
same('tip of the first', chip(0).title, 'Green: Mika');
same('name of the second', chip(1).children[1]._text, 'Bot (Medium)');
same('a chip with no name shows the color word', chip(2).children[1]._text, 'Blue');
same('... and its tip is the color word', chip(2).title, 'Blue');
same('a nasty name is text, as it is', chip(3).children[1]._text, nasty);
same('an unknown color has no color', chip(4).attrs['data-c'], '');
let html = 0;
(function walk(n) { html += n._html || 0; (n.children || []).forEach(walk); })(list);
same('no markup was made from any text', html, 0);
same('no chips for nothing', R.chipsOf(doc, null).children.length, 0);

// ---- a match that is being played: the address
const liveGood = 'TREASURE-20261009-082104Z';
same('a live id', R.liveIdOf('?live=' + liveGood), liveGood);
same('a live id among other parameters', R.liveIdOf('?x=1&live=' + liveGood + '&aspect=16:9'), liveGood);
same('a live id with a counter', R.liveIdOf('?live=TINY-20261009-082104Z-2'), 'TINY-20261009-082104Z-2');
same('a map with an underscore and digits', R.liveIdOf('?live=MY_MAP2-20261009-082104Z'), 'MY_MAP2-20261009-082104Z');
same('the longest map stem', R.liveIdOf('?live=' + 'A'.repeat(24) + '-20261009-082104Z'), 'A'.repeat(24) + '-20261009-082104Z');
for (const bad of ['', '?', '?live=', '?live=x', '?live=../' + liveGood, '?live=/' + liveGood, '?live=' + liveGood + '/', '?live=%2e%2e%2f' + liveGood, '?live=' + liveGood + '.antsrep',
                   '?live=TREASURE-20261009-082104', '?live=-20261009-082104Z', '?live=TI-NY-20261009-082104Z', '?live=TINY-20261009-082104Z-12345', '?live=' + 'A'.repeat(25) + '-20261009-082104Z',
                   '?live=' + liveGood + '%0a', '?live=ants-' + liveGood + '.antsrep', '?live=http://evil.example/' + liveGood, '?live=javascript:alert(1)', '?live=' + liveGood + '&live=../x', '?replay=' + good, '?LIVE=' + liveGood]) {
    if (bad.indexOf('&live=../x') !== -1) { same('the first of two ' + JSON.stringify(bad), R.liveIdOf(bad), liveGood); continue; }
    same('refused ' + JSON.stringify(bad), R.liveIdOf(bad), null);
}
same('no string for a live id', R.liveIdOf(undefined), null);
check('the live pattern is anchored', !R.LIVE_RE.test('/' + liveGood) && !R.LIVE_RE.test(liveGood + 'x') && !R.LIVE_RE.test(liveGood + '\n') && !R.LIVE_RE.test(good));
check('a replay file name is no live id and a live id is no file name', !R.LIVE_RE.test(good) && !R.FILE_RE.test(liveGood));
same('the map of an id', R.liveMap(liveGood), 'TREASURE');
same('the map of another shape', R.liveMap('nonsense'), '');
same('the map of no string', R.liveMap(7), '');
same('when it began', R.startedOf(liveGood), Date.UTC(2026, 9, 9, 8, 21, 4) / 1000);
same('a counter changes nothing', R.startedOf('TINY-20261009-082104Z-3'), Date.UTC(2026, 9, 9, 8, 21, 4) / 1000);
same('no stamp in an id', R.startedOf('nonsense'), null);
same('the start of no string', R.startedOf(undefined), null);

// ---- "Started 8:21 AM" in the visitor's own zone
const liveNow = new Date(2026, 9, 9, 9, 0, 0);
same('began this morning', R.startedText(at(2026, 9, 9, 8, 21), liveNow), 'Started 8:21 AM');
same('began just after midnight', R.startedText(at(2026, 9, 9, 0, 5), liveNow), 'Started 12:05 AM');
same('began last evening', R.startedText(at(2026, 9, 8, 23, 50), liveNow), 'Started Yesterday, 11:50 PM');
same('no start', R.startedText(null, liveNow), '');

// ---- the speed as the bar writes it
same('half speed', R.speedText(50), '\u00bd\u00d7');
same('1x', R.speedText(100), '1\u00d7');
same('2x 4x 8x', [R.speedText(200), R.speedText(400), R.speedText(800)], ['2\u00d7', '4\u00d7', '8\u00d7']);

// ---- what the door answers for a match
const kept = 'ants-TREASURE-20261009-083210Z.antsrep';
same('the match runs', R.liveVerdict(200, null).kind, 'running');
same('over and kept', R.liveVerdict(404, { error: 'no such live match', ended: true, replay: kept }), { kind: 'ended', replay: kept });
same('over and not kept', R.liveVerdict(404, { error: 'no such live match', ended: true, replay: '' }), { kind: 'ended', replay: '' });
same('over with a name that is not a file name of the store', R.liveVerdict(404, { ended: true, replay: '../etc/passwd' }), { kind: 'ended', replay: '' });
same('over with a name that is not text', R.liveVerdict(404, { ended: true, replay: 7 }), { kind: 'ended', replay: '' });
same('over with no name', R.liveVerdict(404, { ended: true }), { kind: 'ended', replay: '' });
same('never was', R.liveVerdict(404, { error: 'no such live match', ended: false, replay: '' }), { kind: 'notlive', replay: '' });
same('"ended" that is not true', R.liveVerdict(404, { ended: 'true', replay: kept }), { kind: 'notlive', replay: '' });
same('a 404 that is not JSON (a server with no door)', R.liveVerdict(404, null), { kind: 'notlive', replay: '' });
same('a 404 with a list', R.liveVerdict(404, [1]), { kind: 'notlive', replay: '' });
for (const status of [0, 204, 301, 403, 410, 429, 500, 502, 503]) same('status ' + status + ' is a feed that fails', R.liveVerdict(status, { ended: true, replay: kept }).kind, 'error');

// ---- is the playhead at the present (gap = seconds behind the game's Limit)
check('at the Limit', R.atLive(false, 0, true) === true);
check('just inside the margin', R.atLive(false, 1.49, true) === true);
check('the margin itself is behind', R.atLive(false, 1.5, true) === false);
check('behind, and was behind', R.atLive(false, 5, true) === false);
check('a paused playhead 3 s behind is behind, whatever it was', R.atLive(true, 3, false) === false);
check('following a match in steps: inside the step it stays at the present', R.atLive(true, 4, true) === true && R.atLive(true, 7.9, true) === true);
check('... and leaves it past the step', R.atLive(true, 8, true) === false && R.atLive(true, 30, true) === false);
check('a playhead that was moved away does not come back by itself inside the step', R.atLive(false, 4, true) === false);
check('paused at the present stays at the present only inside the margin', R.atLive(true, 1, false) === true && R.atLive(true, 2, false) === false);
check('no gap, no present', R.atLive(true, NaN, true) === false && R.atLive(true, -1, true) === false && R.atLive(true, undefined, true) === false);

// ---- the words of the tag (after "LIVE")
same('at the present', R.liveTag({ at: true, gap: 0, speed: '1\u00d7' }), 'about 10 s behind');
same('paused 49 s behind the Limit: the 10 s of the picture are added', R.liveTag({ paused: true, gap: 49, speed: '1\u00d7' }), 'Paused \u00b7 0:59 behind');
same('2x, 70 s behind', R.liveTag({ gap: 70, speed: '2\u00d7' }), '2\u00d7 \u00b7 1:20 behind');
same('half speed, 3.4 s behind', R.liveTag({ gap: 3.4, speed: '\u00bd\u00d7' }), '\u00bd\u00d7 \u00b7 0:13 behind');
same('the feed stopped and the playhead caught up', R.liveTag({ waiting: true, at: true, gap: 0, speed: '1\u00d7' }), 'waiting for the match');
same('paused at the present', R.liveTag({ paused: true, at: true, gap: 0, speed: '1\u00d7' }), 'Paused \u00b7 0:10 behind');
same('no gap given', R.liveTag({ speed: '1\u00d7' }), '1\u00d7 \u00b7 0:10 behind');

// ---- the list of the matches that are being played, as the page takes it
const players = ['Green (Mika)', 'Red (Bot (Medium))', 'Blue (Bot (Medium))', 'Black (Bot (Medium))'];
same('the list of the contract', R.liveEntriesOf({ live: [{ id: liveGood, map: 'TREASURE.LVL', started: 1791533464, turns: 13680, seconds: 684, players }], count: 1, sim_rules: 1 }),
     [{ id: liveGood, map: 'TREASURE.LVL', started: 1791533464, seconds: 684, players }]);
same('seconds from the turns, the map and the start from the id', R.liveEntriesOf({ live: [{ id: liveGood, turns: 1230 }] }),
     [{ id: liveGood, map: 'TREASURE', started: Date.UTC(2026, 9, 9, 8, 21, 4) / 1000, seconds: 61, players: [] }]);
same('a number that is not one is 0', R.liveEntriesOf({ live: [{ id: liveGood, seconds: 'many', turns: null }] })[0].seconds, 0);
same('a negative length is 0', R.liveEntriesOf({ live: [{ id: liveGood, seconds: -5 }] })[0].seconds, 0);
same('an id that is not of the shape is not a match', R.liveEntriesOf({ live: [{ id: '../x' }, { id: 7 }, null, 'x', { id: liveGood + '\n' }, { id: '<img src=x onerror=alert(1)>' }] }), []);
same('a player that is not text is dropped, a nasty one is kept as the text it is', R.liveEntriesOf({ live: [{ id: liveGood, players: [7, null, 'Green (<b>x</b>)', { a: 1 }] }] })[0].players, ['Green (<b>x</b>)']);
same('at most four players', R.liveEntriesOf({ live: [{ id: liveGood, players: ['a', 'b', 'c', 'd', 'e'] }] })[0].players.length, 4);
same('at most 50 matches', R.liveEntriesOf({ live: Array.from({ length: 80 }, () => ({ id: liveGood })) }).length, 50);
same('no list', [R.liveEntriesOf(null), R.liveEntriesOf({}), R.liveEntriesOf({ live: 'x' }), R.liveEntriesOf(7)], [[], [], [], []]);


// ---- the camera button of the game page in replay mode: the choice of the device, the button, the note
{
    const mem = () => { const m = {}; return { getItem: k => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); }, m }; };
    const boom = { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); } };
    same('nothing kept: Auto', R.camLoad(mem()), 'auto');
    same('Manual kept', R.camLoad({ getItem: () => 'manual' }), 'manual');
    same('Auto kept', R.camLoad({ getItem: () => 'auto' }), 'auto');
    same('anything else kept is Auto', [R.camLoad({ getItem: () => 'MANUAL' }), R.camLoad({ getItem: () => '' }), R.camLoad({ getItem: () => '<b>x</b>' })], ['auto', 'auto', 'auto']);
    same('no storage: Auto', [R.camLoad(null), R.camLoad(undefined)], ['auto', 'auto']);
    same('a storage that throws: Auto, and no throw', R.camLoad(boom), 'auto');
    const kept = mem();
    check('saving writes the key `ants.cam`', R.camSave(kept, 'manual') === true && kept.m['ants.cam'] === 'manual' && R.CAM_KEY === 'ants.cam');
    R.camSave(kept, 'whatever');
    same('anything but Manual is saved as Auto', kept.m['ants.cam'], 'auto');
    same('saving where the storage throws or is missing is false, and no throw', [R.camSave(boom, 'manual'), R.camSave(null, 'manual')], [false, false]);
    same('the words of the button', [R.camLabel(true), R.camLabel(false)], ['Auto camera', 'Manual camera']);
    check('the title says what the button does', /Press for Manual camera/.test(R.camTitle(true)) && /Press for Auto camera/.test(R.camTitle(false)));
    same('the note is due the first time the game says the camera was taken', [R.camNoteDue(1, 0, false), R.camNoteDue(0, 0, false), R.camNoteDue(2, 1, true), R.camNoteDue(1, 0, true), R.camNoteDue('1', 0, false)], [true, false, false, false, false]);

    // a page of fakes: the elements the control finds by id, the game's two numbers, the timers
    const page = (store) => {
        const els = {};
        const el = (id) => els[id] = els[id] || { id, attrs: {}, hidden: id === 'camnote', textContent: '', title: '', handlers: {}, classes: {},
            setAttribute(k, v) { this.attrs[k] = String(v); }, getAttribute(k) { return this.attrs[k]; },
            classList: { toggle: (c, on) => { els[id].classes[c] = !!on; } },
            addEventListener(type, fn) { (this.handlers[type] = this.handlers[type] || []).push(fn); },
            click() { (this.handlers.click || []).forEach(fn => fn({})); } };
        ['b-cam', 'cam-lbl', 'camnote'].forEach(el);
        const game = { camera: 1, taken: 0, sent: [] };
        const timers = [];
        const env = { doc: { getElementById: el }, storage: store, VALUE: { camera: 10, taken: 11 }, DO: { camera: 7 },
            get: what => (what === 10 ? game.camera : what === 11 ? game.taken : -1),
            command: (what, value) => { game.sent.push([what, value]); if (what === 7) game.camera = value; },
            setTimeout: function (fn, ms) { if (this !== undefined && this !== null && this !== globalThis) throw new TypeError('Illegal invocation'); timers.push({ fn, ms }); return timers.length; } };     // (like a browser's: not with another `this`)
        return { els, game, timers, env, cam: R.cameraControl(env) };
    };
    {   // Auto is the default: the button says so, and the game is told once that it can hear
        const t = page(mem());
        check('the name of the button is not touched by the script', t.els['b-cam'].attrs['aria-label'] === undefined);
        same('the button before the game answers', [t.els['b-cam'].attrs['data-mode'], t.els['cam-lbl'].textContent, t.els['b-cam'].attrs['aria-pressed']], ['auto', 'Auto camera', 'true']);
        t.cam.look(false);
        same('nothing is sent before the game can hear', t.game.sent, []);
        t.cam.look(true);
        t.cam.look(true);
        same('told once, Auto', t.game.sent, [[7, 1]]);
        // the viewer takes the camera: the game says so, the note comes once, the button says Manual, nothing is remembered
        t.game.camera = 0; t.game.taken = 1;
        t.cam.look(true);
        same('the note is up and the button says Manual', [t.els.camnote.hidden, t.els['b-cam'].attrs['data-mode'], t.els['cam-lbl'].textContent], [false, 'manual', 'Manual camera']);
        same('the note stays up for seven seconds', t.timers.map(x => x.ms), [7000]);
        same('taking the camera is not a choice: nothing is saved', t.env.storage.m, {});
        t.timers[0].fn();
        check('the note goes by itself', t.els.camnote.hidden === true);
        // the button returns to Auto and remembers it; taking it again does not bring the note back in this visit
        same('pressed means Auto: Manual is not pressed', t.els['b-cam'].attrs['aria-pressed'], 'false');
        t.cam.press();
        same('pressed again with Auto on', t.els['b-cam'].attrs['aria-pressed'], 'true');
        same('the button sent Auto', t.game.sent[t.game.sent.length - 1], [7, 1]);
        same('Auto is remembered, and the button says Auto', [t.env.storage.m['ants.cam'], t.els['b-cam'].attrs['data-mode']], ['auto', 'auto']);
        t.game.camera = 0; t.game.taken = 2;
        t.cam.look(true);
        check('the second time in a visit there is no note', t.els.camnote.hidden === true);
        // the button in Auto is Manual, remembered
        t.game.camera = 1;
        t.cam.press();
        same('Manual is chosen and remembered', [t.game.sent[t.game.sent.length - 1], t.env.storage.m['ants.cam'], t.els['b-cam'].attrs['data-mode']], [[7, 0], 'manual', 'manual']);
    }
    {   // a device that chose Manual: the game is told Manual, and the button says so from the start
        const t = page({ getItem: () => 'manual', setItem() {} });
        same('the button says Manual before the game answers', t.els['b-cam'].attrs['data-mode'], 'manual');
        t.cam.look(true);
        same('the game is told Manual', t.game.sent, [[7, 0]]);
    }
    {   // the note is put away by a press, and a browser that will not keep anything still works
        const t = page(boom);
        t.cam.look(true);
        t.game.camera = 0; t.game.taken = 1;
        t.cam.look(true);
        check('the note is up', t.els.camnote.hidden === false);
        t.cam.press();
        check('a press puts the note away', t.els.camnote.hidden === true && t.game.camera === 1);
    }
    {   // the events: a click presses; Enter and Space press and the page does the rest; fullscreen lifts the note
        const t = page(mem());
        let after = 0;
        t.cam.wire(() => { after++; });
        t.cam.look(true);
        t.els['b-cam'].click();
        same('a click presses once, and the page repaints', [t.game.camera, after], [0, 1]);
        let prevented = 0;
        const key = (type, k) => (t.els['b-cam'].handlers[type] || []).forEach(fn => fn({ key: k, repeat: false, preventDefault: () => { prevented++; } }));
        key('keydown', 'Enter');
        same('Enter presses', [t.game.camera, after], [1, 2]);
        key('keydown', ' ');
        key('keyup', ' ');
        same('Space presses, and its release is the page\'s too', [t.game.camera, after, prevented], [0, 3, 3]);
        key('keydown', 'a');
        same('another key does nothing', [t.game.camera, after], [0, 3]);
        t.cam.fullscreen(true);
        check('fullscreen lifts the note', t.els.camnote.classes.up === true);
        t.cam.fullscreen(false);
        check('and the page lowers it again', t.els.camnote.classes.up === false);
    }
}


// ---- the bar under a locked pointer (barPointer): the game's cursor is the pointer; over the visible bar it is the bar's
{
    function make(replayMode) {
        const log = [], handlers = {}, classes = { over: true, idle: false };
        const node = (name, parent, extra) => Object.assign({ name, parent: parent || null, tag: 'div', disabled: false, clicks: 0,
            contains(other) { for (let n = other; n; n = n.parent) if (n === this) return true; return false; },
            closest(sel) { const tags = sel.split(',').map((x) => x.trim().replace(/\[.*\]/, '')); for (let n = this; n; n = n.parent) if (tags.includes(n.tag)) return n; return null; },
            click() { this.clicks++; log.push('click ' + this.name); },
            dispatchEvent(ev) { log.push(ev.type + ' ' + this.name + (ev.pointerType ? ' ' + ev.pointerType + '#' + ev.pointerId : '') + ' @' + ev.clientX + ',' + ev.clientY + (ev.buttons === undefined ? '' : ' b' + ev.buttons)); } }, extra);
        const bar = node('bar', null, { classList: { contains: (c) => !!classes[c] } });
        const play = node('play', bar, { tag: 'button' });
        const icon = node('icon', play, { tag: 'svg' });
        const rew = node('rew', bar, { tag: 'button' });
        const rewIcon = node('rewicon', rew, { tag: 'svg' });
        const rail = node('rail', bar, { tag: 'div' });
        const ground = node('ground', bar, { tag: 'div' });
        const picture = node('picture', null, { tag: 'canvas' });
        const made = [];
        const place = { x: 0, y: 0, edge: false, el: picture, locked: true };
        const doc = { getElementById: (id) => (id === 'rbar' ? bar : null), elementFromPoint: () => place.el,
                      createElement: () => { const m = { style: {}, attrs: {}, kids: [], setAttribute(k, v) { this.attrs[k] = v; }, appendChild(x) { this.kids.push(x); } }; made.push(m); return m; },
                      createElementNS: () => ({ style: {}, attrs: {}, kids: [], setAttribute(k, v) { this.attrs[k] = v; }, appendChild(x) { this.kids.push(x); } }) };
        const stage = { appended: [], appendChild(x) { this.appended.push(x); } };
        const left = [];
        const win = { addEventListener(type, fn, opts) { (handlers[type] = handlers[type] || []).push([fn, opts]); } };
        function PointerEvent(type, init) { Object.assign(this, init); this.type = type; }
        function MouseEvent(type, init) { Object.assign(this, init); this.type = type; }
        const state = { handing: false };
        const api = R.barPointer({ doc, win, stage, canvas: picture, PointerEvent, MouseEvent, replayMode: () => replayMode, handing: () => state.handing,
                                   cursorAt: () => (place.locked ? { x: place.x, y: place.y, edge: place.edge } : null), leave: () => left.push('leave') });
        return { api, log, classes, bar, play, icon, rew, rewIcon, rail, ground, picture, place, made, stage, left, handlers, state };
    }
    const ev = (type, extra) => { const e = Object.assign({ type, button: 0, isTrusted: true, target: null, stopped: 0, prevented: 0, stopPropagation() { this.stopped++; }, preventDefault() { this.prevented++; } }, extra); return e; };
    const last = (t) => t.log[t.log.length - 1];
    {
        const t = make(true);
        same('bar: the listeners are on the window, in capture, the wheel not passive', [Object.keys(t.handlers).sort(), t.handlers.wheel[0][1]], [['click', 'mousedown', 'mouseup', 'wheel'], { capture: true, passive: false }]);
        same('bar: over the picture the cursor is the game\'s: no bar, no arrow, nothing made', [t.api.move(0), t.api.mode(), t.made.length, t.log.length, t.left.length], [false, false, 0, 0, 0]);
        t.place.x = 310; t.place.y = 700; t.place.el = t.icon;
        same('bar: over the bar it is the bar\'s: a mouse pointer move (id 1000) for the element under it', [t.api.move(0), t.api.mode(), last(t)], [true, true, 'pointermove icon mouse#1000 @310,700 b0']);
        same('bar: the arrow is made once, in the stage, hidden:false, its tip at the cursor, the game told once', [t.made.length, t.stage.appended.length, t.made[0].style.transform, t.made[0].style.display, t.made[0].attrs['aria-hidden'], t.left.length], [1, 1, 'translate(308px, 698.5px)', 'block', 'true', 1]);
        check('bar: the arrow takes no pointer events itself (it would be under the cursor it marks)', /(^|;)pointer-events:none(;|$)/.test(t.made[0].style.cssText), t.made[0].style.cssText);
        t.api.move(0);
        same('bar: and not again while the cursor stays on the bar', [t.left.length, t.made.length], [1, 1]);
        t.place.el = t.picture;
        same('bar: back on the picture the arrow goes and the game has the cursor', [t.api.move(0), t.api.mode(), t.made[0].style.display, t.made[0].hidden], [false, false, 'none', true]);
        t.log.length = 0; t.place.el = t.icon;
        same('bar: press and release on a button click it once (pointer events of a mouse, then the click)', [t.api.button('mousedown', 0), t.api.button('mouseup', 0), t.play.clicks, t.log.join('|')],
             [true, true, 1, 'pointerdown icon mouse#1000 @310,700 b1|pointerup icon mouse#1000 @310,700 b0|click play']);
        same('bar: the click that follows is the bar\'s, once', [t.api.button('click', 0), t.picture === t.place.el], [true, false]);
        t.place.el = t.picture;
        same('bar: and a click on the picture is the game\'s', t.api.button('click', 0), false);
        // a release that the bar took, no click, then the picture: the next click is the game's (the flag does not outlive the press)
        t.place.el = t.icon;
        t.api.button('mousedown', 0); t.api.button('mouseup', 0);
        t.place.el = t.picture;
        same('bar: a press on the picture clears the flag: its click is the game\'s', [t.api.button('mousedown', 0), t.api.button('mouseup', 0), t.api.button('click', 0)], [false, false, false]);
        // the Back 30 seconds button is a button of the bar: a press and a release on its triangles (or its 30) click it once
        t.log.length = 0; t.play.clicks = 0; t.place.el = t.rewIcon; t.api.button('mousedown', 0); t.api.button('mouseup', 0); t.api.button('click', 0);
        same('bar: Back 30 seconds is clicked once by the locked mouse', [t.rew.clicks, t.play.clicks, last(t)], [1, 0, 'click rew']);
        // a disabled control is not clicked
        t.play.clicks = 0; t.play.disabled = true; t.place.el = t.icon;
        t.api.button('mousedown', 0);
        same('bar: a disabled button is pressed but not clicked', [t.api.button('mouseup', 0), t.play.clicks], [true, 0]);
        t.api.button('click', 0); t.play.disabled = false;
        // a press that ends on another part of the bar is not a click
        t.play.clicks = 0; t.place.el = t.icon; t.api.button('mousedown', 0); t.place.el = t.rail;
        same('bar: a press on a button that ends elsewhere is not a click', [t.api.button('mouseup', 0), t.play.clicks], [true, 0]);
        t.api.button('click', 0);
        // the bar's own ground: a plain click goes to it (the Picture window closes on it)
        t.log.length = 0; t.place.el = t.ground; t.api.button('mousedown', 0); t.api.button('mouseup', 0);
        same('bar: a release on the bar\'s own ground is a click on it (no control)', last(t), 'click ground @310,700');
        t.api.button('click', 0);
        // the icon redrawn between the press and the release
        t.log.length = 0; t.play.clicks = 0; t.place.el = t.icon; t.api.button('mousedown', 0); t.icon.parent = null; t.place.el = t.play;
        same('bar: an icon drawn again while the button is held: the button is still clicked once', [t.api.button('mouseup', 0), t.play.clicks], [true, 1]);
        t.api.button('click', 0); t.icon.parent = t.play;
        // a drag that began on the timeline goes on beyond the bar, and ends there
        t.log.length = 0; t.place.el = t.rail; t.api.button('mousedown', 0); t.place.el = t.picture; t.place.y = 300;
        same('bar: a drag that began on the timeline goes on over the picture', [t.api.move(1), last(t)], [true, 'pointermove rail mouse#1000 @310,300 b1']);
        same('bar: and ends there', [t.api.button('mouseup', 0), last(t)], [true, 'pointerup rail mouse#1000 @310,300 b0']);
        same('bar: the click that follows a drag released over the picture is the bar\'s (swallowed), once', [t.api.button('click', 0), t.api.button('click', 0)], [true, false]);
        // a press that began on the picture and is released over the bar: the game saw the press, so the release is its own (it must see the button go up), and no pointer event is made for the bar
        t.log.length = 0; t.play.clicks = 0; t.place.y = 300; t.place.el = t.picture;
        const pressed = t.api.button('mousedown', 0); t.place.y = 700; t.place.el = t.icon;
        same('bar: a press on the picture released over the bar goes through to the game (not swallowed)', [pressed, t.api.button('mouseup', 0), t.log.length, t.play.clicks], [false, false, 0, 0]);
        t.api.button('click', 0);
        // ... but a button that the game never saw (the right one pressed over the bar) is the bar's wherever it is released
        t.place.el = t.icon; t.api.button('mousedown', 2); t.place.el = t.picture;
        same('bar: a right button pressed over the bar and released over the picture is still the bar\'s', t.api.button('mouseup', 2), true);
        same('bar: and a stray release afterwards is the game\'s', t.api.button('mouseup', 2), false);
        t.place.y = 300; t.place.el = t.picture;
        // a lock let go in the middle of a drag cancels it
        t.place.y = 700; t.place.el = t.rail; t.api.button('mousedown', 0); t.log.length = 0; t.place.locked = false;
        t.api.reset();
        same('bar: a lock let go while the timeline is held sends pointercancel to it and clears the state', [last(t), t.api.mode(), t.made[0].style.display], ['pointercancel rail mouse#1000 @0,0 b0', false, 'none']);
        t.place.locked = true; t.place.el = t.picture;
        same('bar: after that a move is the game\'s', t.api.move(0), false);
        // other buttons: swallowed over the bar, no pointer press
        t.log.length = 0; t.place.el = t.rail;
        same('bar: a right button over the bar is the bar\'s and makes no press', [t.api.button('mousedown', 2), t.api.button('mouseup', 2), t.log.length], [true, true, 0]);
        // the picture's outermost pixels are the game's even where the bar lies
        t.place.edge = true;
        same('bar: at the picture\'s edge a move, a press, a release and a click are the game\'s', [t.api.move(0), t.api.button('mousedown', 0), t.api.button('mouseup', 0), t.api.button('click', 0)], [false, false, false, false]);
        t.place.edge = false;
        // a bar that has gone away, and one that is not over the picture
        t.classes.idle = true;
        same('bar: a bar that has gone away is not hit', [t.api.under(1, 1), t.api.move(0), t.api.button('mousedown', 0)], [null, false, false]);
        t.classes.idle = false; t.classes.over = false;
        same('bar: nor the bar of a page that is not fullscreen', t.api.under(1, 1), null);
        t.classes.over = true;
        t.api.hideMark();
    }
    {   // the window's listeners: trusted, not the page's own, on the canvas, locked; the wheel over the bar does not zoom
        const t = make(true);
        const run = (type, extra) => { const e = ev(type, Object.assign({ target: t.picture }, extra)); (t.handlers[type] || []).forEach(([fn]) => fn(e)); return e; };
        t.place.el = t.icon;
        let e = run('mousedown');
        same('bar: a trusted press on the canvas over the bar is taken (stopped, default prevented)', [e.stopped, e.prevented], [1, 1]);
        e = run('mouseup'); const e2 = run('click');
        same('bar: and its release and click', [e.stopped, e2.stopped, t.play.clicks], [1, 1, 1]);
        e = run('mousedown', { isTrusted: false });
        same('bar: an event that the page made itself is left alone', [e.stopped, t.api.mode()], [0, false]);
        t.state.handing = true; e = run('mousedown'); t.state.handing = false;
        same('bar: and so is one that the page is handing the game', e.stopped, 0);
        e = run('mousedown', { target: t.rail });
        same('bar: and one that is not on the canvas', e.stopped, 0);
        t.place.locked = false; e = run('mousedown'); t.place.locked = true;
        same('bar: and one while the pointer is not locked', e.stopped, 0);
        t.place.el = t.picture; e = run('mousedown'); run('mouseup'); run('click');
        same('bar: a press over the picture is the game\'s', e.stopped, 0);
        t.place.el = t.icon; e = run('wheel');
        same('bar: the wheel over the bar does not zoom the map', [e.stopped, e.prevented], [1, 1]);
        t.place.el = t.picture; e = run('wheel');
        same('bar: the wheel over the picture is the game\'s', [e.stopped, e.prevented], [0, 0]);
        const off = make(false); off.place.el = off.icon;
        const w = ev('wheel', { target: off.picture }); (off.handlers.wheel || []).forEach(([fn]) => fn(w));
        const d = ev('mousedown', { target: off.picture }); (off.handlers.mousedown || []).forEach(([fn]) => fn(d));
        same('bar: a game page (not a replay) has no bar: nothing is taken', [w.stopped, d.stopped, off.api.move(0)], [0, 0, false]);
    }
}

// ---- Back 30 seconds: the turn that a press goes to, and the button the script makes
{
    same('rewind: 30 s is 600 turns', [R.REWIND_SECONDS, R.rewindTurn(1000), R.rewindTurn(601), R.rewindTurn(2400)], [30, 400, 1, 1800]);
    same('rewind: never below the first turn (exactly 600, near 0, at 0)', [R.rewindTurn(600), R.rewindTurn(599), R.rewindTurn(1), R.rewindTurn(0)], [0, 0, 0, 0]);
    same('rewind: a turn that is not a whole number or not a number', [R.rewindTurn(1000.9), R.rewindTurn(NaN), R.rewindTurn(undefined), R.rewindTurn(-5), R.rewindTurn(Infinity)], [400, 0, 0, 0, 0]);
    same('rewind: the seconds and the rate can be given', [R.rewindTurn(1000, 10, 20), R.rewindTurn(1000, 30, 10)], [800, 700]);
    const make = () => {
        const seeks = [], handlers = {}, turn = { v: 1000 };
        const mk = (tag) => ({ tag, attrs: {}, kids: [], hidden: false, handlers: {}, className: '', textContent: '', parentNode: null, nextSibling: null, clicks: 0,
            setAttribute(k, v) { this.attrs[k] = String(v); }, appendChild(x) { this.kids.push(x); return x; },
            addEventListener(t, fn) { (this.handlers[t] = this.handlers[t] || []).push(fn); },
            click() { this.clicks++; (this.handlers.click || []).forEach((fn) => fn({})); } });
        const bar = { kids: [], insertBefore(n, ref) { n.parentNode = this; this.kids.splice(ref ? this.kids.indexOf(ref) : this.kids.length, 0, n); } };
        const play = mk('button'); play.id = 'b-play'; play.parentNode = bar; bar.kids.push(play);
        const tl = mk('div'); bar.kids.push(tl);
        play.nextSibling = tl;
        const doc = { getElementById: (id) => (id === 'b-play' ? play : null), createElement: mk, createElementNS: (ns, tag) => mk(tag) };
        const control = R.rewindControl({ doc, turn: () => turn.v, seek: (sec) => seeks.push(sec) });
        return { control, seeks, turn, bar, play, tl };
    };
    {
        const t = make(), b = t.control.button;
        check('rewind: the button sits right after Play / Pause, in the bar, before the timeline', t.bar.kids[1] === b && t.bar.kids[0] === t.play && t.bar.kids[2] === t.tl && b.parentNode === t.bar);
        same('rewind: a button with the classes of Play (rplay) and rew', [b.tag, b.type, b.id, b.className], ['button', 'button', 'b-rew', 'btn rplay rew']);
        same('rewind: its name and tip', [b.attrs['aria-label'], b.title], ['Back 30 seconds', 'Back 30 seconds']);
        check('rewind: two triangles in an svg that a screen reader skips, and the 30 under it as text', b.kids.length === 2 && b.kids[0].tag === 'svg' && b.kids[0].attrs['aria-hidden'] === 'true' && /^M11\.5 4\.5v15L2 12z/.test(b.kids[0].kids[0].attrs.d) && b.kids[1].className === 'n' && b.kids[1].textContent === '30');
        check('rewind: it is shown at first', b.hidden === false);
        t.control.press();
        same('rewind: a press seeks to 30 s earlier (in seconds, the page\'s seek)', t.seeks, [20]);
        t.turn.v = 599; t.control.press(); t.turn.v = 0; t.control.press(); t.turn.v = 601; t.control.press();
        same('rewind: and not below 0 s (599, 0), 0.05 s at 601', t.seeks, [20, 0, 0, 0.05]);
        t.control.show(false);
        check('rewind: not shown for a live match', b.hidden === true);
        t.control.show(true);
        check('rewind: shown again', b.hidden === false);
        let after = 0; t.seeks.length = 0; t.turn.v = 1200;
        t.control.wire(() => after++);
        b.click();
        same('rewind: a click seeks once and the bar is painted again', [t.seeks, after], [[30], 1]);
        let prevented = 0;
        (b.handlers.keydown || []).forEach((fn) => fn({ key: 'Enter', repeat: false, preventDefault() { prevented++; } }));
        (b.handlers.keydown || []).forEach((fn) => fn({ key: ' ', repeat: false, preventDefault() { prevented++; } }));
        (b.handlers.keydown || []).forEach((fn) => fn({ key: ' ', repeat: true, preventDefault() { prevented++; } }));
        (b.handlers.keydown || []).forEach((fn) => fn({ key: 'x', repeat: false, preventDefault() { prevented++; } }));
        same('rewind: Enter and Space press it once each (a held key does not repeat), another key does nothing', [b.clicks, prevented, t.seeks.length], [3, 3, 3]);
    }
}

console.log(checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);

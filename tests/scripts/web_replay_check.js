// Runs the code that the two replay pages share (web/replay_page.js: the list, web/watch.html, and the game page in replay mode, web/shell.html) on tables:
//   - the address of a replay (?replay=<file>): only the shape of the names that the server's store makes, which docker/nginx.conf lets through, is ever asked for (no path, no other ending);
//   - the time of a match in the visitor's own time zone ("Today, 7:32 AM", "Yesterday, ...", "Oct 6, ..."), the length of a match as a clock, the six maps' names;
//   - the players as the list gives them ("Green (Mika)", "Red (Bot (Medium))") and as the game reports them (the roster's bits and the four seats' names);
//   - the chips of a match are built from text only: a name with < > & " is never markup;
//   - a match that is being played (?live=<id>): the id's shape (the page asks the server for nothing else), when it began, the door's answers, whether the playhead is at the present, the words of the tag
//     over the picture, and the list of live matches as the page takes it (an entry that is not of the shape, a name that is not text, a number that is not one).
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
same('a person', R.parsePlayer('Green (Mika)'), { colour: 'Green', name: 'Mika' });
same('a computer player', R.parsePlayer('Red (Bot (Medium))'), { colour: 'Red', name: 'Bot (Medium)' });
same('nobody typed anything', R.parsePlayer('Blue'), { colour: 'Blue', name: '' });
same('a name with a bracket and a space', R.parsePlayer('Black (a (b) c)'), { colour: 'Black', name: 'a (b) c' });
same('an empty name in brackets', R.parsePlayer('Green ()'), { colour: 'Green', name: '' });
same('junk', R.parsePlayer(42), { colour: '', name: '' });
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
same('colour of the first', chip(0).attrs['data-c'], 'Green');
same('name of the first', chip(0).children[1]._text, 'Mika');
same('tip of the first', chip(0).title, 'Green: Mika');
same('name of the second', chip(1).children[1]._text, 'Bot (Medium)');
same('a chip with no name shows the colour word', chip(2).children[1]._text, 'Blue');
same('... and its tip is the colour word', chip(2).title, 'Blue');
same('a nasty name is text, as it is', chip(3).children[1]._text, nasty);
same('an unknown colour has no colour', chip(4).attrs['data-c'], '');
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

console.log(checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);

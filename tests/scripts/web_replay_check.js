// Runs the code that the two replay pages share (web/replay_page.js: the list, web/watch.html, and the game page in replay mode, web/shell.html) on tables:
//   - the address of a replay (?replay=<file>): only the shape of the names that the server's store makes, which docker/nginx.conf lets through, is ever asked for (no path, no other ending);
//   - the time of a match in the visitor's own time zone ("Today, 7:32 AM", "Yesterday, ...", "Oct 6, ..."), the length of a match as a clock, the six maps' names;
//   - the players as the list gives them ("Green (Mika)", "Red (Bot (Medium))") and as the game reports them (the roster's bits and the four seats' names);
//   - the chips of a match are built from text only: a name with < > & " is never markup.
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

console.log(checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);

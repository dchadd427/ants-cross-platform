// Runs the code that the pages of the match history share (web/history_page.js: the ids, the names of the seats, the sides of a record, the stats of a seat, the state of the list and the words that it
// asks the server for, the pages, the texts of a failed look) on tables. tests/scripts/test_web_matches.py runs it with node (the quick tier) and again on broken copies of the file, which must fail.
// usage: node web_history_check.js web/history_page.js
'use strict';
const path = require('path');
const H = require(path.resolve(process.argv[2]));
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

// ---- records (made up: a 2 v 2 whose allies fell out, a 1 v 1 of an old version, a 187 match, a draw) ----
const seat = (n, color, name, bot, o) => Object.assign({ seat: n, colour: color, name, bot, score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }, o || {});
const rec2v2 = {
    id: 'ants-TREASURE-20261015-051557Z', mode: 'highest-score', format: '2v2', draw: false, result: 'decided', quitter: null, seconds: 720, turns: 14405, game: 'v0.12.41', recording: 'watch', file: 'ants-TREASURE-20261015-051557Z.antsrep',
    seats: [seat(0, 'Green', 'Ann', '', { score: 140, killed: 7, lost: 4 }), seat(1, 'Red', 'Bea', '', { score: 1010, killed: 7, lost: 5 }), seat(2, 'Blue', 'Pip', 'Hard', { score: 2040, killed: 3, lost: 4, winner: true }),
            seat(3, 'Black', 'Rex', 'Hard', { score: 70, killed: 2, lost: 6, hatched: 1 })],
    rows: [{ seats: [2], score: 2040, killed: 3, lost: 4, hatched: 0, winner: true }, { seats: [1], score: 1010, killed: 7, lost: 5, hatched: 0, winner: false }, { seats: [0, 3], score: 210, killed: 9, lost: 10, hatched: 1, winner: false }]
};
const recOld = { id: 'ants-TREASURE-20261009-202133Z', mode: 'highest-score', format: '1v1', draw: false, result: 'decided', quitter: null, seconds: 720, turns: 14405, game: 'v0.12.0', recording: 'old', file: 'ants-TREASURE-20261009-202133Z.antsrep',
    seats: [seat(0, 'Green', 'Ann', '', { score: 2945, killed: 6, lost: 1, winner: true }), seat(3, 'Black', '', 'Hard', { score: 720, killed: 1, lost: 6, hatched: 1 })], rows: [] };
const rec187 = { id: 'ants-ISLANDS-20261015-103932Z', mode: '187', format: 'ffa3', draw: false, result: 'decided', quitter: null, seconds: 300, turns: 6000, game: 'v0.12.41', recording: 'watch', file: 'ants-ISLANDS-20261015-103932Z.antsrep',
    seats: [seat(0, 'Green', 'Ann', '', { score: 12, killed: 12, winner: true }), seat(1, 'Red', 'Bea', '', { score: 5, killed: 5 }), seat(2, 'Blue', 'Cy', '', { score: 0, killed: 0 })],
    rows: [{ seats: [0], score: 12, killed: 12, lost: 3, hatched: 0, winner: true }, { seats: [1], score: 5, killed: 5, lost: 4, hatched: 0, winner: false }, { seats: [2], score: 0, killed: 0, lost: 7, hatched: 0, winner: false }] };
const recDraw = { id: 'ants-TINY-20261011-110334Z', mode: 'highest-score', format: '1v1', draw: true, result: 'draw', quitter: null, seconds: 360, turns: 7200, game: 'v0.12.41', recording: 'removed',
    seats: [seat(0, 'Green', '', 'Medium', { score: 2020 }), seat(1, 'Red', '', 'Hard', { score: 2020 })], rows: [{ seats: [0], score: 2020, killed: 0, lost: 0, hatched: 0, winner: false }, { seats: [1], score: 2020, killed: 0, lost: 0, hatched: 0, winner: false }] };

// ---- ids and file names ----
same('an id of the shape', H.idOf('?id=ants-TREASURE-20261015-051557Z'), 'ants-TREASURE-20261015-051557Z');
same('... with a second counter', H.idOf('?id=ants-MY_MAP-20261015-051557Z-2'), 'ants-MY_MAP-20261015-051557Z-2');
for (const bad of ['', '?id=', '?id=x', '?id=../etc/passwd', '?id=ants-TREASURE-20261015-051557Z.antsrep', '?id=ants-TREASURE-20261015-051557Z%0A', '?id=%20ants-TREASURE-20261015-051557Z', '?x=ants-TREASURE-20261015-051557Z', '?id=ants-TREASURE-2026-051557Z', '?id=ants-' + 'A'.repeat(25) + '-20261015-051557Z']) {
    same('not an id: ' + JSON.stringify(bad), H.idOf(bad), null);
}
same('the file of an id', H.fileOfId('ants-TREASURE-20261015-051557Z'), 'ants-TREASURE-20261015-051557Z.antsrep');
same('no file for what is not an id', [H.fileOfId('x'), H.fileOfId(null), H.fileOfId({})], ['', '', '']);
same('the id of a file', H.idOfFile('ants-TREASURE-20261015-051557Z.antsrep'), 'ants-TREASURE-20261015-051557Z');
same('no id for what is not a file', [H.idOfFile('ants-TREASURE-20261015-051557Z'), H.idOfFile('x.antsrep'), H.idOfFile(5)], ['', '', '']);

// ---- numbers and words ----
same('numbers with thousands', [H.numberText(0), H.numberText(7), H.numberText(1290), H.numberText(14405), H.numberText(1234567)], ['0', '7', '1,290', '14,405', '1,234,567']);
same('a number that is not one is 0', [H.numberText('9'), H.numberText(-3), H.numberText(NaN), H.numberText(null), H.numberText(Infinity)], ['0', '0', '0', '0', '0']);
same('formats and modes', [H.formatText('1v1'), H.formatText('2v2'), H.formatText('ffa4'), H.formatText('solo'), H.formatText('weird'), H.modeText('highest-score'), H.modeText('187'), H.modeText('x')], ['1 v 1', '2 v 2', 'Free for all', 'Alone', 'weird', 'Highest score', '187', 'x']);
same('a format that is not text is nothing', [H.formatText(5), H.formatText(null), H.formatText({})], ['', '', '']);

// ---- the names of the seats ----
const label = (name, bot, color) => H.seatLabel({ name, bot, colour: color || 'Green' });
same('a person', label('Ann', ''), 'Ann');
same('a person who typed nothing is the color', [label('', '', 'Red'), label('', '', 'Black')], ['Red', 'Black']);
same('a computer player with a name: Name (bot, Level)', [label('Pip', 'Hard'), label('Pip', 'Medium'), label('Pip', 'Easy')], ['Pip (bot, Hard)', 'Pip (bot, Medium)', 'Pip (bot, Easy)']);
same('one that a recording of an earlier version stored with no name: Bot (Level)', [label('', 'Hard'), label('', 'Medium')], ['Bot (Hard)', 'Bot (Medium)']);
same('a computer player whose level the name says none of', [label('Pip', 'Bot'), label('', 'Bot')], ['Pip (bot)', 'Bot']);
same('a name that is markup stays the text it is', label('<img src=x onerror=alert(1)>', ''), '<img src=x onerror=alert(1)>');
same('a seat that is not one is nothing', [H.seatLabel(null), H.seatLabel(undefined), H.seatLabel(5), H.seatLabel({}), H.seatLabel({ name: 7, bot: 7, colour: 'Purple' })], ['', '', '', '', '']);

// ---- the sides: the rows of the record ----
{
    const sides = H.sidesOf(rec2v2);
    same('a 2 v 2 whose allies fell out has three sides, in the rows' + "' order", sides.map((s) => s.seats.map((x) => x.name)), [['Pip'], ['Bea'], ['Ann', 'Rex']]);
    same('... the winner is the first row', sides.map((s) => s.winner), [true, false, false]);
    same('... a side has the numbers of its row', sides.map((s) => [s.row.score, s.row.killed, s.row.lost, s.row.hatched]), [[2040, 3, 4, 0], [1010, 7, 5, 0], [210, 9, 10, 1]]);
    same('the name of a side is its seats', sides.map(H.sideName), ['Pip (bot, Hard)', 'Bea', 'Ann & Rex (bot, Hard)']);
    same('the winner side', H.sideName(H.winnerSide(rec2v2, sides)), 'Pip (bot, Hard)');
    same('a draw has no winner side, whatever the rows say', H.winnerSide(Object.assign({}, rec2v2, { draw: true }), sides), null);
    same('no row marked winner: none', H.winnerSide(rec2v2, sides.map((s) => Object.assign({}, s, { winner: false }))), null);
    same('what stands for a winner: a game alone, a draw, else no winner', [H.noWinnerText({ format: 'solo' }), H.noWinnerText({ format: '1v1', draw: true }), H.noWinnerText({ format: '1v1', result: 'cut' }), H.noWinnerText(null)], ['Played alone', 'A draw: nobody won', 'No winner', 'No winner']);
}
{
    const sides = H.sidesOf(recOld);
    same('a record with no rows has one side for each seat, the best score first', sides.map((s) => s.seats[0].colour), ['Green', 'Black']);
    same('... and its seat decides who won', sides.map((s) => s.winner), [true, false]);
    same('... and the numbers of a seat are the side\'s', sides.map((s) => s.row.score), [2945, 720]);
}
same('a row that names a seat that is not there is skipped (and the seats that no row names come after it)', H.sidesOf({ seats: rec2v2.seats, rows: [{ seats: [9], score: 5 }, { seats: [2, 9], score: 7, winner: true }] }).map((s) => s.seats.map((x) => x.seat)), [[2], [0], [1], [3]]);
same('rows that are not rows', H.sidesOf({ seats: rec2v2.seats, rows: [null, 5, 'x', { seats: 'no' }] }).length, 4);
same('a record that is not one has no sides', [H.sidesOf(null).length, H.sidesOf({}).length, H.sidesOf({ seats: 'x', rows: 'y' }).length], [0, 0, 0]);
same('a seat with an unknown color is not a seat', H.seatsOf({ seats: [{ colour: 'Purple' }, null, 4, { colour: 'Red', seat: 1 }] }).length, 1);

{
    // a seat that dropped out has no row: a side of its own after the others, marked left, and no part of the ranking
    const rec = Object.assign({}, rec2v2, { seats: rec2v2.seats.concat([seat(1, 'Red', 'Dee', '', { score: 360, left: true })]).filter((s, i) => i !== 1 || s.name === 'Dee'), rows: rec2v2.rows.filter((r) => r.seats[0] !== 1) });
    const sides = H.sidesOf(rec);
    same('a seat that left is a side after the ranked ones, marked', sides.map((s) => [s.seats.map((x) => x.name).join('&'), s.left]), [['Pip', false], ['Ann&Rex', false], ['Dee', true]]);
    same('... with its own numbers and never a winner', [sides[2].row.score, sides[2].winner], [360, false]);
    same('the ranking has only the sides that stayed', H.rankedOf(sides).map((s) => s.seats[0].name), ['Pip', 'Ann']);
    same('... and the shares are theirs', H.shareOf(rec, H.rankedOf(sides)).length, 2);
    same('a seat that no row names and that did not say it left is a side of its own too', H.sidesOf({ seats: rec2v2.seats, rows: [rec2v2.rows[0]] }).map((s) => s.left), [false, true, true, true]);
}

// ---- the score, who played, how it ended ----
const sides187 = H.sidesOf(rec187);
same('a score is a number with thousands', H.scoreText(rec2v2, H.sidesOf(rec2v2)[0]), '2,040');
same('in mode 187 the score is shown as kills, never as a score', sides187.map((s) => H.scoreText(rec187, s)), ['12 kills', '5 kills', '0 kills']);
same('who played', [H.playersText(rec2v2), H.playersText(recOld), H.playersText(recDraw), H.playersText({ seats: [] })], ['2 people, 2 computer players', '1 person, 1 computer player', '2 computer players', '']);
same('how it ended', [H.howEnded(rec2v2), H.howEnded(recDraw), H.howEnded(Object.assign({}, rec2v2, { result: 'cut' })), H.howEnded(Object.assign({}, rec2v2, { quitter: 2 })), H.howEnded(null)],
    ['The rules ended the match', 'The rules ended it: a draw', 'Cut short: a player left or the clock ran out', 'Ended by a quit', '']);
same('the best of a count', [H.bestOf(rec2v2, 'killed'), H.bestOf(rec2v2, 'hatched'), H.bestOf(rec2v2, 'score'), H.bestOf(null, 'score')], [7, 1, 2040, 0]);
same('the shares add up to 100 and follow the sides', H.shareOf(rec2v2, H.sidesOf(rec2v2)), [{ value: 2040, percent: 63 }, { value: 1010, percent: 31 }, { value: 210, percent: 6 }]);
same('a share is of the kills in mode 187', H.shareOf(rec187, sides187).map((x) => x.percent), [71, 29, 0]);
same('nobody scored: no share', H.shareOf(recDraw, [{ row: { score: 0, killed: 0 } }, { row: { score: 0, killed: 0 } }]), []);
for (const sc of [[1, 1, 1], [1, 1], [2, 1], [5, 3, 3, 1], [99, 1], [1, 2, 3, 4, 5, 6, 7]]) {
    const total = H.shareOf(rec2v2, sc.map((n) => ({ row: { score: n, killed: 0 } }))).reduce((a, x) => a + x.percent, 0);
    check('the shares of ' + JSON.stringify(sc) + ' add up to 100', total === 100, total);
}

// ---- the columns, the state of the recording, the details ----
same('the columns of the scoreboard', H.columnsOf(rec2v2).map((c) => c.key), ['score', 'killed', 'lost', 'hatched', 'bombs_planted', 'bombs_defused', 'fires_lit']);
same('... none is a score in mode 187', H.columnsOf(rec187).map((c) => c.key), ['killed', 'lost', 'hatched', 'bombs_planted', 'bombs_defused', 'fires_lit']);
same('the actions start their own group', H.columnsOf(rec2v2).filter((c) => c.first).map((c) => c.key), ['bombs_planted']);
same('a recording that can be watched', [H.recState(rec2v2).kind, H.recState(rec2v2).label, H.recState(rec2v2).file], ['watch', 'Replay', 'ants-TREASURE-20261015-051557Z.antsrep']);
same('one of an older version cannot be played and says by whom it was made', [H.recState(recOld).kind, H.recState(recOld).label, /v0\.12\.0/.test(H.recState(recOld).note), H.recState(recOld).file], ['old', 'Older version', true, 'ants-TREASURE-20261009-202133Z.antsrep']);
same('one that is gone', [H.recState(recDraw).kind, H.recState(recDraw).label, H.recState(recDraw).file], ['removed', 'Replay removed', '']);
same('a recording that is said to be kept but has no good file name is gone', [H.recState(Object.assign({}, rec2v2, { file: '../x' })).kind, H.recState(Object.assign({}, rec2v2, { file: undefined })).kind, H.recState(Object.assign({}, recOld, { file: 5 })).kind], ['removed', 'removed', 'removed']);
same('a word that is not one is gone', [H.recState(Object.assign({}, rec2v2, { recording: 'later' })).kind, H.recState(null).kind, H.recState({}).kind], ['removed', 'removed', 'removed']);
same('the details are a plain object or nothing', [H.detailsOf({ details: { a: 1 } }), H.detailsOf({ details: [1] }), H.detailsOf({ details: 'x' }), H.detailsOf({ details: null }), H.detailsOf({}), H.detailsOf(null)], [{ a: 1 }, null, null, null, null, null]);

// ---- the state of the list: the address, the words of the server, the page ----
const D = H.defaults();
same('the default state', D, { who: 'all', format: 'all', result: 'all', rec: 'all', colour: 'all', mode: 'all', sort: 'new', q: '', page: 1 });
same('an address with nothing is the default', [H.stateOf(''), H.stateOf('?')], [D, D]);
same('every key is read', H.stateOf('?who=people&format=team&result=cut&rec=old&colour=blue&mode=187&sort=kills&q=Ann&page=4'),
    { who: 'people', format: 'team', result: 'cut', rec: 'old', colour: 'blue', mode: '187', sort: 'kills', q: 'Ann', page: 4 });
same('a word that is not one of the server\'s is the default (nothing is guessed)', H.stateOf('?who=everybody&format=4v4&result=won&rec=gone&colour=Blue&mode=hard&sort=best'), D);
same('the page is a whole number from 1', [H.stateOf('?page=0').page, H.stateOf('?page=-3').page, H.stateOf('?page=x').page, H.stateOf('?page=2.5').page, H.stateOf('?page=999999999').page, H.stateOf('?page=7').page], [1, 1, 1, 2, 1, 7]);
same('a search is printable ASCII, blanks folded, at most 40 characters', [H.cleanQuery('  a   b  '), H.cleanQuery('Käse\u0000\u007féx'), H.cleanQuery('x'.repeat(60)).length, H.cleanQuery(5), H.cleanQuery('a b'.repeat(20)).length <= 40, H.cleanQuery('ab '.repeat(14) + 'zz')], ['a b', 'Ksex', 40, '', true, 'ab ab ab ab ab ab ab ab ab ab ab ab ab a']);
same('the address of a state: only what is not the default, the server\'s words', [H.urlOf(D), H.urlOf(H.stateOf('?who=people&sort=kills')), H.urlOf(H.stateOf('?q=a%20b&page=3')), H.urlOf(D, 2)],
    ['matches.html', 'matches.html?who=people&sort=kills', 'matches.html?q=a%20b&page=3', 'matches.html?page=2']);
for (const q of ['', '?who=computers', '?format=ffa&result=draw&rec=removed&colour=black&mode=highest-score&sort=short&page=12', '?q=Pip%20(bot%2C%20Hard)&page=2', '?q=a%26b%3Dc%25d%2Be']) {
    const st = H.stateOf(q);
    same('an address is read back as it was written: ' + q, H.stateOf(H.urlOf(st).replace(/^matches\.html\??/, '?')), st);
}

// ---- what is asked of the server ----
const NGINX = /^[A-Za-z0-9_=&%+.-]{0,400}$/;                              // docker/nginx.conf: the characters of a query that go through
same('the default view is the list with no query: it serves the first two pages from the newest 50', [H.planOf(D), H.planOf(Object.assign({}, D, { page: 2 }))], [{ query: '', skip: 0, take: 25 }, { query: '', skip: 25, take: 25 }]);
same('a later page is a query with its offset', [H.planOf(Object.assign({}, D, { page: 3 })).query, H.planOf(Object.assign({}, D, { page: 12 })).query], ['limit=25&offset=50', 'limit=25&offset=275']);
same('a filter is a query of the server\'s words', H.planOf(H.stateOf('?who=people&format=team&result=draw&rec=watch&colour=red&mode=187&sort=score')).query, 'who=people&format=team&result=draw&rec=watch&colour=red&mode=187&sort=score&limit=25&offset=0');
same('"all" is never sent', H.planOf(H.stateOf('?who=all&format=all&sort=new&q=x')).query, 'q=x&limit=25&offset=0');
same('a plan that has a query takes the whole answer', H.planOf(H.stateOf('?sort=old')), { query: 'sort=old&limit=25&offset=0', skip: 0, take: 25 });
for (const text of ['Pip (bot, Hard)', "it's!", 'a(b)c*d~e', 'x&y=z%w+v', 'a b', 'Name_1.2-3', '"quoted"', '<b>', 'back\\slash', 'semi;colon', 'a/b', '#hash', '?ask']) {
    const plan = H.planOf(Object.assign({}, D, { q: H.cleanQuery(text) }));
    check('the search ' + JSON.stringify(text) + ' is a query that nginx lets through', NGINX.test(plan.query), plan.query);
    const back = new URLSearchParams(plan.query).get('q');
    check('... and reads back as the text that was typed (the server reads it as it is)', back === H.cleanQuery(text), back);
    check('... and the page address lets through too', NGINX.test(H.urlOf(Object.assign({}, D, { q: H.cleanQuery(text) })).replace(/^matches\.html\??/, '')));
}
{
    const keys = new URLSearchParams(H.planOf(H.stateOf('?who=people&format=ffa&result=cut&rec=old&colour=blue&mode=187&sort=kills&q=Ann&page=3')).query);
    const names = [...keys.keys()];
    check('no key is twice (the server refuses it)', names.length === new Set(names).size, names.join());
    check('every key is one the server knows', names.every((k) => ['who', 'format', 'result', 'rec', 'colour', 'mode', 'sort', 'q', 'limit', 'offset'].includes(k)), names.join());
}
same('the chips of what is not the default', H.activeOf(H.stateOf('?who=computers&format=ffa&q=Pip&sort=kills')).map((a) => [a.key, a.label, a.value]), [['who', 'Who', 'Computers only'], ['format', 'Format', 'Free for all'], ['q', 'Search', '“Pip”']]);
same('the sort is not a filter', H.activeOf(H.stateOf('?sort=long')), []);
same('what a sort by a count shows on a card', [H.sortKeyOf(rec2v2, 'kills'), H.sortKeyOf(rec2v2, 'hatched'), H.sortKeyOf(rec2v2, 'score'), H.sortKeyOf(rec2v2, 'new'), H.sortKeyOf(rec2v2, 'long')],
    [{ label: 'Most kills', value: 7 }, { label: 'Most hatched', value: 1 }, { label: 'Top score', value: 2040 }, null, null]);
same('mode 187 has no score: a sort by score shows the kills, whatever the card', [H.sortKeyOf(rec187, 'score'), H.sortKeyOf(rec187, 'kills')], [{ label: 'Most kills', value: 12 }, { label: 'Most kills', value: 12 }]);
same('the line over the list', [H.rangeText(296, 296, 0, 25, 'new'), H.rangeText(296, 296, 25, 25, 'kills'), H.rangeText(21, 296, 0, 21, 'new'), H.rangeText(5, 5, 0, 0, 'new')],
    ['Showing 1 to 25 of 296 matches, newest first', 'Showing 26 to 50 of 296 matches, most kills', 'Showing 1 to 21 of the 21 that fit (296 in all)', '']);
same('the pager', [H.pagerOf(296, 1, 25), H.pagerOf(296, 3, 25), H.pagerOf(296, 12, 25), H.pagerOf(20, 1, 25), H.pagerOf(26, 2, 25), H.pagerOf(0, 1, 25), H.pagerOf(296, 99, 25).page],
    [{ pages: 12, page: 1, items: [1, 2, 'gap', 12] }, { pages: 12, page: 3, items: [1, 2, 3, 4, 'gap', 12] }, { pages: 12, page: 12, items: [1, 'gap', 11, 12] }, { pages: 1, page: 1, items: [1] },
     { pages: 2, page: 2, items: [1, 2] }, { pages: 1, page: 1, items: [1] }, 12]);

// ---- what a failed look says ----
same('the texts of a failed look: the server\'s words (as text) or ours', [H.problemOf(503, { error: 'the match history is busy: try again in a moment' }), H.problemOf(503, null).retry, H.problemOf(404, null).retry, H.problemOf(400, { error: 'q: bad' }).text, H.problemOf(0, null).retry, H.problemOf(429, null).retry, H.problemOf(500, { error: 5 }).retry],
    [{ text: 'the match history is busy: try again in a moment', retry: true }, true, false, 'q: bad', true, true, true]);
check('the server\'s words are cut', H.problemOf(503, { error: 'x'.repeat(500) }).text.length === 200);

// ---- the size line of a map ----
same('the size of a map from the server\'s list', [H.mapFact('TREASURE.LVL', [{ file: 'TREASURE.LVL', width: 60, height: 60, minutes: 12 }]), H.mapFact('arena.lvl', [{ file: 'ARENA.lvl', width: 40, height: 40, minutes: 10 }]), H.mapFact('x.lvl', [{ file: 'y.lvl', width: 1, height: 1, minutes: 1 }]), H.mapFact('x.lvl', null), H.mapFact(5, [])],
    ['60 × 60, 12 minutes', '40 × 40, 10 minutes', '', '', '']);

console.log(checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);

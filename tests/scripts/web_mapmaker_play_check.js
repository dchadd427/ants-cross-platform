#!/usr/bin/env node
// The map maker's Play this map (web/front/mapmaker_play.js, the game page's side web/front/test_game.js; docs/MAP_EDITOR.md, "Play this map") run on made-up answers of the check, on a small fake of
// the DOM, and, when a built map core is there, on the real core and the 540 maps of the library, with no browser:
//
//   node tests/scripts/web_mapmaker_play_check.js [--front DIR] [--module DIR] [--require]
//
//   --front DIR   the folder of the scripts to hold to the checks (default: web/front; the Python test gives it broken copies, to show that the checks can fail)
//   --module DIR  the folder of lvl_core.js and lvl_core.wasm (default: $ANTS_LVL_CORE_DIR, else build_web/lvl_core of the checkout)
//   --require     a missing module is a failure (the default is a note and no part D: a checkout that has no web build)
//
//   A  the verdict: what the check and the first reading of a file say, as a refusal in plain words (an Extended map, a map of more than 90 rows, a stop that is about no color, fewer than two colors that
//      can play) or as the four seats; the start rules on all 625 choices against a second statement of them; the quick buttons; the letters of the game's address
//   B  the game page's side: the address through a whitelist (a table, and thousands of random addresses), the hand-over from the map maker's tab (a copy, never the tab's own array), the bytes written
//      where the game looks, the strip (text only, the way back asks once and closes the tab, a tab that cannot close goes to the map maker), a hand-over that is not there, a game that is not counted
//   C  the dialogs on a fake page: the start dialog's seats, You moves, Start opens the game's address once with a copy of the open file's bytes (and keeps three), a blocked tab is said, the refusals,
//      a name that holds markup stays text, and nothing here writes a byte of the open map
//   D  the real core (when it is there): every map of the library gets a verdict that agrees with the file's own start markers and hills, the Extended kinds are refused in the words, and the bytes that Start
//      hands over are the file's bytes
// Exit status 0 when everything passes, 1 otherwise.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const FRONT = argv.includes('--front') ? path.resolve(argv[argv.indexOf('--front') + 1]) : path.join(ROOT, 'web', 'front');
const MODULE = argv.includes('--module') ? path.resolve(argv[argv.indexOf('--module') + 1]) : (process.env.ANTS_LVL_CORE_DIR || path.join(ROOT, 'build_web', 'lvl_core'));
const REQUIRE = argv.includes('--require');
const load = (name) => require(path.join(FRONT, name));
const Play = load('mapmaker_play.js'), Game = load('test_game.js'), Dialog = load('mapmaker_dialog.js'), File = load('mapmaker_file.js'), Blocks = load('mapmaker_blocks.js');
const MapFiles = require(path.join(ROOT, 'web', 'map_files.js'));
const { makeDocument } = require('./web_mapmaker_fake_dom.js');

let failures = 0, checks = 0;
function check(ok, what) { checks++; if (!ok) { failures++; console.log('    FAILED: ' + what); } }
function equal(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); }
function digest(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }

// ---- made-up answers of the check (core.checkMap) and of the first reading (core.readReport) --------------------------------------------------------------------------------------------------------------
const NAMES = ['Green', 'Red', 'Blue', 'Black'];
function finding(code, severity, team, message, count) { return { severity, code, message, row: -1, column: -1, team, count: count || 1, place_limit: false }; }
// teams: for each color [ants, hill cells]; extra findings are added as they are
function answers(opts) {
    const o = Object.assign({ kind: 'classic', rows: 31, columns: 31, teams: [[3, 16], [3, 16], [3, 16], [3, 16]], extra: [], judged: true, loaded: true }, opts || {});
    const findings = [];
    o.teams.forEach(([ants, cells], t) => {
        if (!cells) findings.push(finding('hill_missing', 'warning', t, NAMES[t] + ' has no hill'));
        if (!ants) findings.push(finding('starting_ants_missing', 'warning', t, NAMES[t] + ' has no starting ants (no start marker, tile 154)'));
    });
    return { check: { kind: o.kind, judged: o.judged, works: !findings.length, headline: 'x', badge: '', findings: findings.concat(o.extra) },
             read: { loaded: o.loaded, rows: o.rows, columns: o.columns, teams: o.teams.map(([ants, cells], t) => ({ team: NAMES[t].toLowerCase(), start_markers: ants, hill_cells: cells })) } };
}
function verdictOf(opts) { const a = answers(opts); return Play.verdict(a.check, a.read); }
const usable = (v) => v.seats.map((s) => (s.usable ? 1 : 0)).join('');

function verdictChecks() {
    let v = verdictOf();
    check(v.ok && v.seats.length === 4 && usable(v) === '1111', 'four colors with ants and a hill: all four can play');
    equal(v.seats.map((s) => [s.seat, s.name, s.ants, s.hills]), [[0, 'Green', 3, 1], [1, 'Red', 3, 1], [2, 'Blue', 3, 1], [3, 'Black', 3, 1]], 'the seats are the game\'s own: Green 0, Red 1, Blue 2, Black 3, with their ants and hills');
    check(verdictOf({ rows: 90 }).ok && !verdictOf({ rows: 91 }).ok, 'Classic: ninety rows play, ninety-one do not');
    // an Extended map up to 512 x 512 (262,144 cells, 1,024 a side) plays; one bigger is refused with a way to a Classic copy
    v = verdictOf({ kind: 'extended', rows: 100, columns: 100 });
    check(v.ok && v.seats.length === 4 && usable(v) === '1111', 'an Extended map of 100 x 100 plays');
    v = verdictOf({ kind: 'extended', rows: 256, columns: 256 });
    check(v.ok && v.seats.length === 4 && usable(v) === '1111', 'an Extended map of 256 x 256 plays');
    v = verdictOf({ kind: 'extended', rows: 512, columns: 512 });
    check(v.ok && v.seats.length === 4 && usable(v) === '1111', 'an Extended map of 512 x 512 plays');
    v = verdictOf({ kind: 'extended', rows: 1024, columns: 256 });
    check(v.ok && v.seats.length === 4 && usable(v) === '1111', 'an Extended map of 1024 x 256 plays');
    v = verdictOf({ kind: 'extended', rows: 256, columns: 1024 });
    check(v.ok && v.seats.length === 4 && usable(v) === '1111', 'an Extended map of 256 x 1024 plays');
    v = verdictOf({ kind: 'extended', rows: 512, columns: 513 });
    check(!v.ok && /bigger than the game can play yet/.test(v.text) && v.convert === true, 'an Extended map of 512 x 513 (262,656 cells) is refused');
    v = verdictOf({ kind: 'extended', rows: 1025, columns: 100 });
    check(!v.ok && v.title === 'This map cannot be played yet' && /This map is bigger than the game can play yet\./.test(v.text) && v.convert === true && v.seats.length === 0, 'an Extended map of 1025 rows: "bigger than the game can play yet", with a way to a Classic copy');
    v = verdictOf({ kind: 'extended', rows: 100, columns: 1025 });
    check(!v.ok && /bigger than the game can play yet/.test(v.text) && v.convert === true, 'an Extended map of 1025 columns is refused too');
    v = verdictOf({ kind: 'classic', rows: 120 });
    check(!v.ok && /bigger than the game can play yet/.test(v.text) && v.convert === false && /90 rows or fewer/.test(v.note), 'a Classic map of 120 rows is bigger too, and has no Classic copy to offer');
    check(!verdictOf({ kind: 'extended', rows: 600, columns: 600, teams: [[0, 0], [0, 0], [0, 0], [0, 0]] }).ok && /bigger/.test(verdictOf({ kind: 'extended', rows: 600, columns: 600, teams: [[0, 0], [0, 0], [0, 0], [0, 0]] }).text), 'the size is said before anything about hills');
    // a stop that is about no color
    v = verdictOf({ extra: [finding('grid_empty', 'fatal', -1, 'the grid has no cell'), finding('file_cut_short', 'fatal', -1, 'the file ends inside the dictionary')] });
    check(!v.ok && v.title === 'This map cannot be played' && v.list.length === 2 && v.list[0] === 'The grid has no cell' && v.list[1] === 'The file ends inside the dictionary', 'a stop that is about no color is refused with the check\'s own sentences (capitalized)');
    // a stop that is about one color only takes that color away
    v = verdictOf({ extra: [finding('start_marker_outside_map', 'fatal', 2, 'team blue: 1 start marker(s) lie outside the grid')] });
    check(v.ok && usable(v) === '1101' && /Team blue/.test(v.seats[2].why[0]), 'a stop about Blue stops Blue only');
    v = verdictOf({ teams: [[3, 16], [3, 16], [3, 0], [3, 16]] });
    check(v.ok && usable(v) === '1101' && v.seats[2].hills === 0 && v.seats[2].why[0] === 'Blue has no hill', 'a color without a hill cannot play, and the check says why');
    v = verdictOf({ teams: [[3, 16], [0, 16], [3, 16], [3, 16]] });
    check(v.ok && usable(v) === '1011' && /no starting ants/.test(v.seats[1].why[0]), 'a color without starting ants cannot play');
    v = verdictOf({ extra: [finding('hills_several', 'warning', 0, 'Green has 2 hills (entrances at row 3, column 4 and at row 20, column 20)', 2)] });
    check(v.ok && usable(v) === '0111' && v.seats[0].hills === 2, 'two hills of one color: that color cannot play, and says how many');
    v = verdictOf({ extra: [finding('hill_without_entrance', 'warning', 1, 'the hill of Red has no entrance')] });
    check(usable(v) === '1011', 'a hill without an entrance cannot be a start');
    v = verdictOf({ extra: [finding('hill_not_whole', 'info', 3, 'the hill of Black is not whole: one cell differs')] });
    check(usable(v) === '1111', 'a note about a hill that is not whole does not stop a color');
    v = verdictOf({ extra: [finding('food_pile_not_shown', 'warning', -1, 'a pile is not shown'), finding('tile_not_drawn', 'info', -1, 'x')] });
    check(v.ok && usable(v) === '1111', 'warnings and notes about other things do not stop a test game');
    // the check's own findings are not the only reason: a color that the first reading finds empty is out too
    {
        const a = answers({ teams: [[3, 16], [0, 16], [3, 0], [3, 16]] });
        a.check.findings = [];
        v = Play.verdict(a.check, a.read);
        check(v.ok && usable(v) === '1001' && v.seats[1].why[0] === 'Red has no starting ants' && v.seats[2].why[0] === 'Blue has no hill', 'a color with no ants or no hill in the first reading cannot play, though the check said nothing, and says why');
    }
    // the start ants (all four colors added up, as the server counts them): at most 300 are played by an online room, more only against bots on this device
    equal([Play.MAX_ONLINE_ANTS, verdictOf().ants, verdictOf().tooMany], [300, 12, false], 'the limit is 300 and an ordinary map starts 12 ants');
    equal([75, 76].map((n) => { const v2 = verdictOf({ teams: [[75, 16], [75, 16], [75, 16], [n, 16]] }); return [v2.ants, v2.tooMany, v2.ok]; }), [[300, false, true], [301, true, true]], 'the boundary: 300 is in, 301 is over, and both can be played against bots');
    equal([verdictOf({ teams: [[301, 16], [0, 0], [3, 16], [0, 0]] }).tooMany, verdictOf({ teams: [[0, 0], [0, 0], [0, 0], [0, 0]] }).ok], [true, false], 'one color with all of them is over; a map with no ants is refused as before (no seat can play) and says nothing about ants');
    equal(verdictOf({ teams: [[1500, 16], [1500, 16], [1500, 16], [1500, 16]] }).ants, 6000, 'the 6,000-ant map of 1,500 a color');
    equal(verdictOf({ teams: [[200, 16], [200, 0], [0, 16], [100, 16]] }).tooMany, true, 'a color that cannot play still counts: the server counts every start marker');
    equal([Play.onlineAntsText(6000), Play.onlineAntsText(301)], ['This map starts with 6,000 ants; an online game allows up to 300.', 'This map starts with 301 ants; an online game allows up to 300.'], 'the sentence is the server\'s, with the number');
    // fewer than two colors
    v = verdictOf({ teams: [[3, 16], [0, 0], [0, 0], [0, 0]] });
    check(!v.ok && /two colors that each have a hill and starting ants/.test(v.text) && v.list.length === 6 && v.list[0] === 'Red has no hill', 'one color is not enough: refused with the reasons of the others');
    v = verdictOf({ teams: [[0, 0], [0, 0], [0, 0], [0, 0]] });
    check(!v.ok && v.list.length === 8, 'no color at all: all eight reasons');
    check(verdictOf({ teams: [[3, 16], [3, 16], [0, 0], [0, 0]] }).ok, 'two colors are enough (one for you, one for a bot)');
    // a check that could not judge, a file that did not load
    check(!verdictOf({ judged: false }).ok && Play.verdict(null, null).title === 'This map cannot be checked' && !verdictOf({ loaded: false }).ok, 'no judgement, no game');
    // the words are text: the verdict holds strings, never markup that a script of the page would have to trust
    v = verdictOf({ extra: [finding('grid_empty', 'fatal', -1, '<img src=x onerror=alert(1)>')] });
    check(v.list[0] === '<img src=x onerror=alert(1)>', 'a sentence of the check is kept as text');
    // the start rules on every choice: one color is yours, at least one is a bot, every used color can play
    const seats = verdictOf({ teams: [[3, 16], [3, 16], [3, 0], [3, 16]] }).seats;
    let wrong = 0, ok = 0;
    for (let n = 0; n < 625; n++) {
        const choice = [0, 1, 2, 3].map((i) => 'yemhn'[Math.floor(n / Math.pow(5, i)) % 5]);
        const you = choice.filter((c) => c === 'y').length, bots = choice.filter((c) => 'emh'.includes(c)).length, bad = choice.some((c, i) => c !== 'n' && !seats[i].usable);
        const want = you === 1 && bots >= 1 && !bad, got = Play.startable(choice, seats);
        if (got.ok !== want || typeof got.text !== 'string' || !got.text) wrong++;
        if (got.ok) ok++;
    }
    equal([wrong, ok], [0, 45], 'all 625 choices: Start is on exactly when one color is yours, one is a bot at least, and every used color can play');
    equal(Play.startable(['y', 'n', 'n', 'n'], seats), { ok: false, text: 'Add at least one bot.' }, 'alone: "Add at least one bot."');
    equal(Play.startable(['n', 'm', 'n', 'n'], seats).text, 'Choose one color for yourself.', 'no seat of yours');
    equal(Play.startable(['y', 'y', 'n', 'n'], seats).text, 'Choose one color for yourself.', 'two seats of yours');
    equal(Play.startable(['y', 'n', 'e', 'n'], seats).text, 'A color without a hill and starting ants cannot be used.', 'a bot on a color that cannot play');
    equal(Play.startable(['y', 'm', 'n', 'h'], seats), { ok: true, text: 'You and 2 bots.' }, 'two bots');
    // first choice and quick buttons
    equal(Play.firstChoice(seats), ['y', 'm', 'n', 'n'], 'you at the first color that can play, one Medium bot at the next');
    equal(Play.firstChoice(verdictOf({ teams: [[0, 0], [3, 16], [3, 16], [3, 16]] }).seats), ['n', 'y', 'm', 'n'], 'a color that cannot play is skipped');
    equal(Play.quick(['y', 'n', 'n', 'n'], seats, 1), ['y', 'm', 'n', 'n'], '1 bot');
    equal(Play.quick(['y', 'n', 'n', 'n'], seats, 2), ['y', 'm', 'n', 'm'], '2 bots skip the color that cannot play');
    equal(Play.quick(['y', 'n', 'n', 'n'], seats, 3), null, '3 bots are not possible when only three colors can play');
    equal(Play.quick(['n', 'n', 'y', 'n'], verdictOf().seats, 3), ['m', 'm', 'y', 'm'], '3 bots round you');
    equal(Play.quick(['n', 'n', 'n', 'n'], seats, 1), null, 'without a seat of yours nothing is filled');
    equal(Play.code(['y', 'e', 'm', 'h']), 'yemh', 'the letters go in the game\'s order');
}

// ---- the game page's side ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function gameChecks() {
    const ID = 'k7m2xq9abc12', LEVEL = { e: 'easy', m: 'medium', h: 'hard' };
    let g = Game.fromAddress(ID, 'ymnn');
    equal(g && g.args, ['--map', 'Original-Ants/Maps/playtest.lvl', '--play', '--player', '0', '--bot', '1:medium'], 'you at Green, a Medium bot at Red');
    g = Game.fromAddress(ID, 'nnhy');
    equal(g && g.args, ['--map', 'Original-Ants/Maps/playtest.lvl', '--play', '--player', '3', '--bot', '2:hard'], 'you at Black, a Hard bot at Blue');
    g = Game.fromAddress(ID, 'emhy');
    equal(g && g.args.slice(3), ['--player', '3', '--bot', '0:easy', '--bot', '1:medium', '--bot', '2:hard'], 'three bots, each at its own seat');
    equal([g.you, g.bots.length], [3, 3], 'the seats are known');
    const BAD_SEATS = ['', 'yyyy', 'nnnn', 'yyyn', 'ynnn', 'mmmm', 'YMNN', 'ymn', 'ymnnn', 'ymnx', 'y mn', 'ym\nn', 'ymnn ', ' ymnn', 'ymn\u0661', 'constructor', '__proto__', 'ymnn&x=1', 'ymnn;ls', '--bot', 'ymnn\n--name x'];
    BAD_SEATS.forEach((s) => check(Game.fromAddress(ID, s) === null, 'seats ' + JSON.stringify(s) + ' are no seats'));
    ['', 'abc', 'abcde', 'ABCDEFGH', 'k7m2xq9abc12 ', ' k7m2xq9abc12', 'k7m2-q9abc12', 'k7m2.q9abc12', '../k7m2xq', 'k7m2xq9abc12k7m2xq9abc12k7m2xq9abc12', '__proto__', 'k7m2xq\n--name x', 'k7m\u0661xq9a'].forEach((id) => check(Game.fromAddress(id, 'ymnn') === null, 'id ' + JSON.stringify(id.slice(0, 20)) + ' is no hand-over'));
    [null, undefined, 5, {}, [], ['ymnn'], true].forEach((x) => check(Game.fromAddress(x, 'ymnn') === null && Game.fromAddress(ID, x) === null, 'a ' + typeof x + ' is no text'));
    check(Game.fromAddress('abcdef', 'ymnn') !== null && Game.fromAddress('a'.repeat(32), 'ymnn') !== null && Game.fromAddress('a'.repeat(33), 'ymnn') === null && Game.fromAddress('abcde', 'ymnn') === null, 'ids of 6 to 32 characters');
    // thousands of random addresses: whatever the text, the arguments are the fixed words and numbers, and only for a good pair
    {
        let seed = 12345, wrong = 0, good = 0;
        const rnd = (n) => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) % n; };
        const pool = ['y', 'e', 'm', 'h', 'n', 'Y', 'x', ' ', '\n', '-', '--map', '.', '/', '\u0661', 'a', 'k7m2xq9abc12', '%'];
        const allowed = new Set(['--map', 'Original-Ants/Maps/playtest.lvl', '--play', '--player', '0', '1', '2', '3', '--bot']);
        for (let s = 0; s < 4; s++) for (const l of ['easy', 'medium', 'hard']) allowed.add(s + ':' + l);
        for (let i = 0; i < 30000; i++) {
            let seats = '', id = '';
            const n = rnd(3) ? 4 : rnd(8);
            for (let k = 0; k < n; k++) seats += rnd(5) ? 'yemhn'[rnd(5)] : pool[rnd(pool.length)];
            const m = rnd(3) ? 12 : rnd(40);
            for (let k = 0; k < m; k++) id += rnd(6) ? 'abcdefghijklmnopqrstuvwxyz0123456789'[rnd(36)] : pool[rnd(pool.length)];
            const r = Game.fromAddress(id, seats);
            const yous = (seats.match(/y/g) || []).length, bots = (seats.match(/[emh]/g) || []).length;
            const should = /^[a-z0-9]{6,32}$/.test(id) && /^[yemhn]{4}$/.test(seats) && yous === 1 && bots >= 1;
            if (!!r !== should) wrong++;
            if (r) {
                good++;
                if (!r.args.every((a) => allowed.has(a)) || r.args.length !== 5 + 2 * bots + 0) wrong++;
                const you = seats.indexOf('y');
                if (r.args[4] !== String(you)) wrong++;
                for (let s = 0; s < 4; s++) if (/[emh]/.test(seats[s]) && r.args.indexOf(s + ':' + LEVEL[seats[s]]) < 0) wrong++;
            }
        }
        check(wrong === 0 && good > 500, 'thirty thousand random addresses: only a good pair reaches the game, and then only fixed words (' + wrong + ' wrong, ' + good + ' good)');
    }
    equal(Game.seatLines(Game.fromAddress(ID, 'mehy')), { you: 'You: Black', bots: 'Bots: Blue Hard, Green Medium, Red Easy' }, 'the strip names the colors in the alphabet\'s order');
    equal(Game.seatLines(Game.fromAddress(ID, 'nyhn')), { you: 'You: Red', bots: 'Bots: Blue Hard' }, 'one bot');
    // a name for the strip
    equal([Game.cleanName('Mosh Pit'), Game.cleanName('  a\u0000b\u202ec\u2066d\ufeff  e\t\nf '), Game.cleanName(''), Game.cleanName('   '), Game.cleanName(null), Game.cleanName(5), Game.cleanName({})], ['Mosh Pit', 'abcd e f', 'Test map', 'Test map', 'Test map', 'Test map', 'Test map'], 'a name is cleaned of controls and direction marks');
    equal([Array.from(Game.cleanName('x'.repeat(60))).length, Array.from(Game.cleanName('\ud83d\udc1c'.repeat(50))).length, Game.cleanName('\ud83d\udc1c'.repeat(50)).slice(0, 2)], [40, 40, '\ud83d\udc1c'], 'at most 40 characters, never cut in the middle of one');

    const bytes = new Uint8Array([8, 0, 1, 2, 3, 4, 5, 6, 7, 8]);
    function windowOf(opener, extra) {
        const doc = makeDocument(), timers = [], win = Object.assign({ document: doc, opener, closed: false, closes: 0, assigned: [], timers, AntsMapFiles: MapFiles,
            setTimeout(f, ms) { timers.push([f, ms]); return timers.length; }, close() { win.closes++; }, location: { assign(url) { win.assigned.push(url); } } }, extra || {});
        doc.defaultView = win;
        doc.body.appendChild(Object.assign(doc.createElement('div'), { id: 'game-box' }));
        ['progress-container', 'splash-overlay', 'status-text'].forEach((id) => doc.body.appendChild(Object.assign(doc.createElement('div'), { id })));
        return win;
    }
    function tabOf(records) {
        const log = { asked: [], back: [] };
        return Object.assign(log, { closed: false, antsTestGameHandoff(id) { log.asked.push(id); return records[id] || null; }, antsTestGameBack(id) { log.back.push(id); } });
    }
    // take: a copy, from a tab that is there
    let src = bytes.slice(), opener = tabOf({ [ID]: { name: 'Mosh Pit', bytes: src } }), win = windowOf(opener);
    let rec = Game.take(win, ID);
    check(rec && digest(rec.bytes) === digest(bytes) && rec.bytes !== src && rec.name === 'Mosh Pit', 'take: the bytes are the tab\'s, as a copy');
    src[0] = 99;
    check(rec.bytes[0] === 8, 'a change of the tab\'s array afterwards does not reach the copy');
    equal(opener.asked, [ID], 'the tab was asked for the id of the address');
    check(Game.take(windowOf(opener), 'otherid1234') === null, 'another id: nothing');
    check(Game.take(windowOf(null), ID) === null && Game.take(windowOf(undefined), ID) === null, 'no opener: nothing');
    check(Game.take(windowOf(Object.assign(tabOf({ [ID]: { name: 'x', bytes } }), { closed: true })), ID) === null, 'a tab that is closed: nothing');
    check(Game.take(windowOf({}), ID) === null && Game.take(windowOf({ antsTestGameHandoff: 5 }), ID) === null, 'a tab without the hand-over: nothing');
    check(Game.take(windowOf({ antsTestGameHandoff() { throw new Error('gone'); } }), ID) === null, 'a tab that throws: nothing');
    check(Game.take(windowOf(tabOf({ [ID]: { name: 'x', bytes: new Uint8Array(0) } })), ID) === null && Game.take(windowOf(tabOf({ [ID]: { name: 'x' } })), ID) === null && Game.take(windowOf(tabOf({ [ID]: { name: 'x', bytes: new Uint8Array(4 * 1024 * 1024 + 1) } })), ID) === null, 'no bytes, no map; more than four megabytes is no map');
    check(Game.take(windowOf(tabOf({ [ID]: { name: 'x', bytes: new Uint8Array(4 * 1024 * 1024) } })), ID) !== null, 'four megabytes is still taken');
    equal(Game.take(windowOf(tabOf({ [ID]: { name: '<b>x</b>\u0000', bytes } })), ID).name, '<b>x</b>', 'the name is cleaned');
    // begin: the bytes are written where the game looks, the strip is up, the game starts
    {
        const test = Game.fromAddress(ID, 'ymhn'), module = { preRun: [], FS: { calls: [], mkdirTree(p) { this.calls.push(['mkdirTree', p]); }, writeFile(p, b) { this.calls.push(['writeFile', p, digest(b)]); } } };
        const opener2 = tabOf({ [ID]: { name: 'Mosh <b>Pit</b>', bytes: bytes.slice() } }), w = windowOf(opener2);
        const result = Game.begin(test, w, () => module);
        check(result === true, 'begin says that the game may start');
        equal(module.preRun.length, 1, 'one function for the game to run before it looks for maps');
        check(module.FS.calls.length === 0, 'nothing is written until the game\'s file system is there');
        module.preRun[0]();
        equal(module.FS.calls, [['mkdirTree', 'Original-Ants/Maps'], ['writeFile', 'Original-Ants/Maps/playtest.lvl', digest(bytes)]], 'the file system gets the tab\'s bytes under the one name');
        const bar = w.document.getElementById('test-strip');
        check(bar && w.document.body.children[0] === bar, 'the strip is the first thing on the page');
        equal(bar.textContent, '\u25c0 Back to editorTest gameMosh <b>Pit</b> \u00b7 You: Green \u00b7 Bots: Blue Hard, Red MediumNot saved. Not counted.', 'the strip says what this is, with the name as text');
        check(!bar.all().some((n) => n.tagName === 'b'), 'no element was made of the name');
        equal([bar.getAttribute('role'), bar.getAttribute('aria-label')], ['region', 'Test game'], 'the strip is a labeled region');
        check(/min-height:44px/.test(bar.querySelector('button').style.cssText), 'the way back is 44 pixels high');
        // the way back asks once
        const back = bar.querySelector('button');
        back.click();
        let dlg = w.document.querySelector('[role=dialog]');
        check(dlg && /Leave the test game\?/.test(dlg.textContent) && /exactly as you left it/.test(dlg.textContent), 'the question: "Leave the test game?"');
        equal([w.closes, opener2.back.length], [0, 0], 'nothing has happened yet');
        const buttons = dlg.querySelectorAll('button');
        equal(buttons.map((b) => b.textContent), ['Keep playing', 'Back to editor'], 'two answers');
        buttons[0].click();
        check(w.document.querySelector('[role=dialog]') === null && w.closes === 0 && opener2.back.length === 0, 'Keep playing closes the question and stays');
        back.click();
        w.document.key('Escape');
        check(w.document.querySelector('[role=dialog]') === null && w.closes === 0, 'Escape is Keep playing');
        equal((w.document.listeners.keydown || []).length, 0, 'no key listener is left');
        back.click();
        w.document.querySelector('[role=dialog]').querySelectorAll('button')[1].click();
        equal([w.closes, opener2.back, w.timers.length, w.timers[0] && w.timers[0][1]], [1, [ID], 1, 300], 'Back to editor tells the map maker\'s tab, closes this tab, and waits for it');
        w.timers[0][0]();
        equal(w.assigned, ['/mapmaker.html'], 'a tab that the browser did not close goes to the map maker');
        const w2 = windowOf(tabOf({ [ID]: { name: 'x', bytes } })), m2 = { preRun: [], FS: {} };
        Game.begin(test, w2, () => m2);
        w2.document.getElementById('test-strip').querySelector('button').click();
        w2.document.querySelector('[role=dialog]').querySelectorAll('button')[1].click();
        w2.closed = true;
        w2.timers[0][0]();
        equal(w2.assigned, [], 'a tab that closed goes nowhere');
    }
    // begin with no hand-over: a card, no game
    {
        const w = windowOf(null), module = { preRun: [] };
        const result = Game.begin(Game.fromAddress(ID, 'ymnn'), w, () => module);
        check(result === false && module.preRun.length === 0, 'no hand-over: the game does not start and nothing is queued');
        const box = w.document.getElementById('load-failure');
        check(box && /This test game cannot start/.test(box.textContent) && /Open the map maker/.test(box.textContent) && /press Play this map again/.test(box.textContent), 'a card says so, in plain words');
        box.querySelector('button').click();
        equal(w.assigned, ['/mapmaker.html'], 'with a way to the map maker');
        check(w.document.getElementById('test-strip') === null, 'no strip');
        check(w.document.getElementById('splash-overlay').className.indexOf('failed') >= 0 && w.document.getElementById('progress-container').style.display === 'none', 'the loading screen gives way to the card');
    }
}

// ---- the dialogs on a fake page ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function pageOf() {
    const doc = makeDocument(), host = doc.createElement('div'), top = doc.createElement('header'), body = doc.createElement('div'), opener = doc.createElement('button'), timers = [], opened = [];
    [host, top, body].forEach((n) => doc.body.appendChild(n));
    top.appendChild(opener); opener.focus();
    const win = { opened, timers, blocked: false, crypto: null, Uint8Array,
        open(url, target) { opened.push([url, target]); return win.blocked ? null : {}; },
        setTimeout(f, ms) { timers.push([f, ms]); return timers.length; }, clearTimeout(id) { if (timers[id - 1]) timers[id - 1][2] = true; } };
    return { doc, host, win, dialogs: Dialog.create(doc, host, [top, body]), timers, opened };
}
function dialogChecks() {
    const bytes = new Uint8Array(200).map((_, i) => (i * 7) & 255);
    const a = answers({ columns: 40, teams: [[3, 16], [3, 16], [1, 16], [3, 16]] });
    let converted = 0, asked = [];
    function env(p, over) {
        return Object.assign({ doc: p.doc, win: p.win, dialogs: p.dialogs, map: () => ({ name: 'Mosh Pit', bytes, file: {} }), convert: () => { converted++; },
            core: { checkMap(b, profile) { asked.push(['checkMap', b === bytes, profile]); return { status: 0, json: a.check }; }, readReport(b, profile) { asked.push(['readReport', b === bytes, profile]); return { status: 0, json: a.read }; } } }, over || {});
    }
    const q = (p, s) => p.host.querySelector(s), all = (p, s) => p.host.querySelectorAll(s);
    const rowOf = (p, name) => all(p, '.pseat').find((r) => r.querySelector('.pn').textContent === name);
    const checked = (p) => ['Green', 'Red', 'Blue', 'Black'].map((n) => { const r = rowOf(p, n), i = r.querySelectorAll('input').find((x) => x.checked); return i ? i.value : '?'; }).join('');
    const choose = (p, name, value) => { const i = rowOf(p, name).querySelectorAll('input').find((x) => x.value === value); i.checked = true; i.fire('change', {}); };
    const startButton = (p) => all(p, '.dlg-foot button')[1];
    const before = digest(bytes);

    let p = pageOf();
    Play.open(env(p));
    equal(asked, [['checkMap', true, 'extended'], ['readReport', true, 'extended']], 'the check and the first reading are of the open file\'s own bytes, by the profile that takes both kinds');
    check(p.dialogs.isOpen() && q(p, 'h2').textContent === 'Play this map', 'the dialog opens with its name');
    equal(q(p, '.play-lead').textContent, 'A test game on Mosh Pit (31 x 40) as it is now. Nothing is saved and no score is kept.', 'it says what this is, with the name and the size');
    equal(all(p, '.pseat').map((r) => r.querySelector('.pn').textContent), ['Black', 'Blue', 'Green', 'Red'], 'one row for each color, in the alphabet\'s order');
    equal(all(p, '.pseat').map((r) => r.querySelector('.pf').textContent), ['3 ants, 1 hill', '1 ant, 1 hill', '3 ants, 1 hill', '3 ants, 1 hill'], 'with its ants and hills (a singular for one)');
    equal(rowOf(p, 'Red').querySelectorAll('.pc').map((l) => l.textContent), ['You', 'Bot Easy', 'Bot Medium', 'Bot Hard', 'Empty'], 'five choices for each');
    equal(checked(p), 'ymnn', 'it starts with you at Green and a Medium bot at Red');
    equal(q(p, '.play-status').textContent, 'You and 1 bot.', 'and says so');
    check(!startButton(p).disabled && startButton(p).textContent.indexOf('Start the game') > 0, 'Start the game is on');
    equal(all(p, '.play-quick button').map((b) => b.textContent + (b.disabled ? ' off' : '')), ['1 bot', '2 bots', '3 bots'], 'three quick buttons, all on for four colors');
    check(all(p, '[role=radiogroup]').length === 4 && all(p, '[role=radiogroup]').every((g) => g.getAttribute('aria-label')), 'each row is a labeled group');
    check(p.doc.activeElement.tagName === 'input' && p.doc.activeElement.checked, 'the focus is on a chosen answer');
    // You moves
    choose(p, 'Red', 'y');
    equal(checked(p), 'nynn', 'You at Red takes You away from Green (that color is Empty)');
    check(startButton(p).disabled && q(p, '.play-status').textContent === 'Add at least one bot.' && q(p, '.play-status').className.indexOf('no') > 0, 'with no bot Start is off and says what is missing');
    startButton(p).click();
    startButton(p).fire('click', {});
    check(p.opened.length === 0, 'a Start that is off does nothing (not even when the click comes through)');
    choose(p, 'Blue', 'h'); choose(p, 'Black', 'e');
    equal(checked(p), 'nyhe', 'bots of the three levels');
    equal(q(p, '.play-status').textContent, 'You and 2 bots.', 'two bots');
    choose(p, 'Blue', 'n'); choose(p, 'Black', 'n');
    all(p, '.play-quick button')[2].click();
    equal(checked(p), 'mymm', '3 bots: You stay, the rest are Medium bots');
    all(p, '.play-quick button')[0].click();
    equal(checked(p), 'nymn', '1 bot: the next color after yours');
    choose(p, 'Green', 'y');
    equal(checked(p), 'ynmn', 'You moves again');
    // Start
    all(p, '.play-quick button')[1].click();
    equal(checked(p), 'ymmn', '2 bots after Green');
    const go = startButton(p);
    go.click();
    go.fire('click', {});
    equal(p.opened.length, 1, 'Start opens one tab (a second call of it opens no second)');
    const m = /^\/play\.html\?testmap=([a-z0-9]{12})&seats=(ymmn)$/.exec(p.opened[0][0]);
    check(!!m && p.opened[0][1] === '_blank', 'at the game page with the id of a hand-over and the four letters, in a new tab: ' + p.opened[0][0]);
    check(!p.dialogs.isOpen() && p.host.querySelector('.dlg') === null, 'the dialog closes');
    check(typeof p.win.antsTestGameHandoff === 'function' && typeof p.win.antsTestGameBack === 'function', 'the tab can be asked for the hand-over and told of the way back');
    let rec = p.win.antsTestGameHandoff(m[1]);
    check(rec && rec.name === 'Mosh Pit' && digest(rec.bytes) === before, 'the hand-over holds the open file\'s bytes, byte for byte');
    check(rec.bytes !== bytes, 'as a copy: a later stroke of the map maker does not reach the game');
    bytes[3] ^= 0xff;
    check(digest(p.win.antsTestGameHandoff(m[1]).bytes) === before, 'a change of the open file after Start leaves the hand-over as it was');
    bytes[3] ^= 0xff;
    check(p.win.antsTestGameHandoff('zzzzzzzzzzzz') === null && p.win.antsTestGameHandoff('') === null && p.win.antsTestGameHandoff(undefined) === null, 'an id that was never handed out gets nothing');
    equal(digest(bytes), before, 'nothing wrote a byte of the open map');
    // the game page reads the same address and gets the same seats and bytes
    const test = Game.fromAddress(m[1], m[2]);
    check(test && test.you === 0 && test.bots.length === 2, 'the game page accepts the address that the map maker made');
    // back from the game
    p.win.antsTestGameBack(m[1]);
    let toast = p.doc.getElementById('play-toast');
    check(toast && toast.textContent === 'Back from the test game. Your map is as you left it.' && toast.getAttribute('role') === 'status', 'the way back is told in a line');
    p.win.antsTestGameBack(m[1]);
    check(p.doc.body.all().filter((n) => n.id === 'play-toast').length === 1, 'a second way back replaces the line, it does not add one');
    p.timers[p.timers.length - 1][0]();
    check(p.doc.getElementById('play-toast') === null, 'the line goes away by itself');
    p.win.antsTestGameBack('nobody123456');
    check(p.doc.getElementById('play-toast') === null, 'an id that is not ours says nothing');
    // three are kept; a fourth takes the first one's place
    const ids = [m[1]];
    for (let i = 0; i < 3; i++) {
        Play.open(env(p));
        startButton(p).click();
        ids.push(/testmap=([a-z0-9]{12})/.exec(p.opened[p.opened.length - 1][0])[1]);
    }
    check(ids.length === 4 && new Set(ids).size === 4, 'every game has an id of its own');
    check(p.win.antsTestGameHandoff(ids[0]) === null && p.win.antsTestGameHandoff(ids[1]) && p.win.antsTestGameHandoff(ids[3]), 'three hand-overs are kept, the oldest is let go');
    equal(Play.kept().length, 3, 'three in all');
    // a tab that the browser did not open
    p.win.blocked = true;
    const keptBefore = Play.kept().length;
    Play.open(env(p));
    startButton(p).click();
    check(p.dialogs.isOpen() && q(p, 'h2').textContent === 'The test game could not open' && /Allow pop-ups/.test(q(p, '.fatalmsg').textContent) && /Your map was not changed/.test(q(p, '.fatalmsg').textContent), 'a blocked tab is said in plain words');
    equal(Play.kept().length, keptBefore, 'and nothing is kept for it');
    p.dialogs.close();
    // Cancel
    p = pageOf();
    Play.open(env(p));
    all(p, '.dlg-foot button')[0].click();
    check(!p.dialogs.isOpen() && p.opened.length === 0, 'Cancel closes it and opens nothing');
    p = pageOf();
    Play.open(env(p));
    p.doc.key('Escape');
    check(!p.dialogs.isOpen() && p.opened.length === 0, 'so does Escape');
    // a color that cannot play
    const b2 = answers({ teams: [[3, 16], [3, 0], [3, 16], [0, 16]] });
    p = pageOf();
    Play.open(env(p, { core: { checkMap: () => ({ status: 0, json: b2.check }), readReport: () => ({ status: 0, json: b2.read }) } }));
    check(rowOf(p, 'Red').className.indexOf('off') > 0 && rowOf(p, 'Black').className.indexOf('off') > 0 && rowOf(p, 'Red').querySelector('.pf').textContent === 'No hill', 'a color without a hill is marked, in plain words');
    equal(rowOf(p, 'Black').querySelector('.pf').textContent, 'No starting ants', 'and one without ants too, with no color name and no tile number');
    equal(['Green', 'Red', 'Blue', 'Black'].map((n) => rowOf(p, n).querySelectorAll('input').map((i) => (i.disabled ? 1 : 0)).join('')), ['00000', '11110', '00000', '11110'], 'only Empty is open on it');
    equal(rowOf(p, 'Red').querySelectorAll('.pc').map((l) => l.className), ['pc dis', 'pc dis', 'pc dis', 'pc dis', 'pc on'], 'the style marks what is chosen and what is off (a class of its own, no :has)');
    equal(checked(p), 'ynmn', 'the first choice skips it');
    all(p, '.play-quick button')[2].click();
    equal(all(p, '.play-quick button').map((b) => (b.disabled ? 0 : 1)), [1, 0, 0], 'with two colors that can play only "1 bot" is on');
    p.dialogs.close();
    // the refusals
    for (const [what, opts, title, words, convert] of [
        ['an Extended map of 1025 rows', { kind: 'extended', rows: 1025 }, 'This map cannot be played yet', /This map is bigger than the game can play yet\./, true],
        ['a Classic map of 120 rows', { kind: 'classic', rows: 120 }, 'This map cannot be played yet', /90 rows or fewer/, false],
        ['a stop', { extra: [finding('grid_empty', 'fatal', -1, 'the grid has no cell')] }, 'This map cannot be played', /The grid has no cell/, false],
        ['one color', { teams: [[3, 16], [0, 0], [0, 0], [0, 0]] }, 'This map cannot be played yet', /two colors/, false]]) {
        const r = answers(opts);
        p = pageOf(); converted = 0;
        Play.open(env(p, { core: { checkMap: () => ({ status: 0, json: r.check }), readReport: () => ({ status: 0, json: r.read }) } }));
        check(q(p, 'h2').textContent === title && words.test(q(p, '.dlg-in').textContent), what + ': the refusal says ' + title);
        const names = all(p, '.dlg-foot button').map((b) => b.textContent);
        equal(names, convert ? ['Make a Classic copy...', 'OK'] : ['OK'], what + ': its buttons');
        check(all(p, '.pseat').length === 0 && !all(p, '.dlg-foot button').some((b) => /Start/.test(b.textContent)), what + ': no seats and no Start');
        check(q(p, '.note') !== null && /Your map was not changed/.test(q(p, '.note').textContent), what + ': and says that the map was not changed');
        if (convert) { all(p, '.dlg-foot button')[0].click(); check(converted === 0, 'the copy waits until the dialog has closed'); p.timers[p.timers.length - 1][0](); check(converted === 1 && !p.dialogs.isOpen(), 'then the editor\'s own conversion starts'); }
        else { all(p, '.dlg-foot button')[0].click(); check(!p.dialogs.isOpen() && p.opened.length === 0, what + ': OK closes it and opens nothing'); }
    }
    // a core that does not answer
    p = pageOf();
    Play.open(env(p, { core: { checkMap: () => ({ status: 2, error: 'the module stopped' }), readReport: () => ({ status: 0, json: a.read }) } }));
    check(q(p, 'h2').textContent === 'The check did not run' && /The module stopped\./.test(q(p, '.fatalmsg').textContent) && p.opened.length === 0, 'a core that does not answer: said, and no game');
    // a name that holds markup is text, everywhere
    p = pageOf();
    Play.open(env(p, { map: () => ({ name: '<img src=x onerror=alert(1)><b>x</b>', bytes }) }));
    check(!p.host.all().some((n) => n.tagName === 'img') && q(p, '.play-lead').textContent.indexOf('<img src=x onerror=alert(1)><b>x</b>') > 0 && q(p, '.play-lead b').textContent === '<img src=x onerror=alert(1)><b>x</b>', 'a map\'s name is put in as text');
    p.dialogs.close();
    const r = answers({ extra: [finding('grid_empty', 'fatal', -1, '<b>bold</b><script>x</script>')] });
    p = pageOf();
    Play.open(env(p, { core: { checkMap: () => ({ status: 0, json: r.check }), readReport: () => ({ status: 0, json: r.read }) } }));
    check(!q(p, '.play-why').all().some((n) => n.tagName === 'b' || n.tagName === 'script') && q(p, '.play-why li').textContent === '<b>bold</b><script>x</script>', 'a sentence of the check is put in as text');
    // the "With other players" tab: opens /lobby.html?testroom=<id> and hands over a copy of the bytes
    p = pageOf();
    Play.open(env(p));
    const tabs = all(p, '.play-tab');
    equal(tabs.map((t) => [t.getAttribute('data-mode'), t.getAttribute('aria-selected')]), [['bots', 'true'], ['friends', 'false']], 'two mode tabs, Against bots selected first');
    tabs[1].click();
    equal(tabs.map((t) => t.getAttribute('aria-selected')), ['false', 'true'], 'switching to With other players selects the second tab');
    check(q(p, '.play-panel-bots').hidden === true && q(p, '.play-panel-friends').hidden === false, 'the friends panel is shown and the bots panel is hidden');
    equal(all(p, '.pfseat').map((row) => row.querySelector('.pfn').textContent), ['Black', 'Blue', 'Green', 'Red'], 'four seat summary cards in alphabetical order');
    check(!startButton(p).disabled && /Open a room with this map/.test(startButton(p).textContent), 'the action button becomes Open a room with this map');
    startButton(p).click();
    const mr = /^\/lobby\.html\?testroom=([a-z0-9]{12})$/.exec((p.opened[0] || [])[0] || '');
    check(!!mr && p.opened[0][1] === '_blank', 'With other players opens /lobby.html?testroom=<id> in a new tab');
    const rrec = mr && p.win.antsTestGameHandoff(mr[1]);
    check(rrec && rrec.name === 'Mosh Pit' && digest(rrec.bytes) === before && rrec.bytes !== bytes, 'the lobby tab receives a copy of the open map bytes');
    equal(all(p, '.play-panel-friends .play-status').length + all(p, '.play-panel-bots .play-status').filter((n) => n.className.indexOf('no') > 0 && n.textContent.indexOf('slowly') > 0).length, 0, 'a map of 12 ants has no word about ants in either panel');
    // a map of more than 300 start ants: against bots it is allowed with a warning, with other players the sentence of the server is said first and the button is off
    for (const [ants, over] of [[300, false], [301, true], [6000, true]]) {
        const per = [Math.floor(ants / 4), Math.floor(ants / 4), Math.floor(ants / 4), ants - 3 * Math.floor(ants / 4)];
        const big = answers({ teams: per.map((n) => [n, 16]) });
        p = pageOf();
        Play.open(env(p, { core: { checkMap: () => ({ status: 0, json: big.check }), readReport: () => ({ status: 0, json: big.read }) } }));
        const warn = all(p, '.play-panel-bots .play-status').filter((n) => n.textContent.indexOf('slowly') > 0);
        const said = all(p, '.play-panel-friends .play-status');
        const count = ants === 6000 ? '6,000' : String(ants);
        if (!over) { equal([warn.length, said.length], [0, 0], ants + ' ants: no warning and no refusal'); }
        else {
            equal(warn.map((n) => n.textContent), ['This map starts with ' + count + ' ants, so it may run slowly on this device.'], ants + ' ants: against bots a warning, in the bots panel');
            check(!startButton(p).disabled && /Start the game/.test(startButton(p).textContent) && !!/You and 1 bot/.test(q(p, '.play-panel-bots .play-status').textContent), ants + ' ants: the game against bots can still be started');
            startButton(p).click();
            check(p.opened.length === 1 && /^\/play\.html\?testmap=/.test(p.opened[0][0]), ants + ' ants: and it opens the game page');
            p = pageOf();
            Play.open(env(p, { core: { checkMap: () => ({ status: 0, json: big.check }), readReport: () => ({ status: 0, json: big.read }) } }));
        }
        all(p, '.play-tab')[1].click();
        const friendsSaid = all(p, '.play-panel-friends .play-status');
        if (!over) { check(friendsSaid.length === 0 && !startButton(p).disabled, ants + ' ants: with other players, no refusal and the room button is on'); continue; }
        equal(friendsSaid.map((n) => n.textContent), ['This map starts with ' + count + ' ants; an online game allows up to 300. You can still play it against bots on this device.'], ants + ' ants: with other players the sentence is said before a room is made');
        check(startButton(p).disabled && /Open a room with this map/.test(startButton(p).textContent), ants + ' ants: and the room button is off');
        startButton(p).click();
        startButton(p).fire('click', {});
        check(p.opened.length === 0, ants + ' ants: a button that is off opens no room (not even when the click comes through)');
        all(p, '.play-tab')[0].click();
        check(!startButton(p).disabled && /Start the game/.test(startButton(p).textContent), ants + ' ants: back at the game against bots the button is on again');
        all(p, '.play-tab')[1].click();
        check(startButton(p).disabled, ants + ' ants: and off again with other players');
        p.dialogs.close();
    }
    equal(digest(bytes), before, 'at the end the open file is still as it was');
}

// ---- the real core -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
async function coreChecks() {
    const moduleFile = path.join(MODULE, 'lvl_core.js');
    if (!fs.existsSync(moduleFile) || !fs.existsSync(path.join(MODULE, 'lvl_core.wasm'))) {
        if (REQUIRE) { failures++; console.log('    FAILED: no built map core in ' + MODULE); } else console.log('    (no built map core in ' + MODULE + ': part D is not run)');
        return;
    }
    const LvlCore = require(path.join(ROOT, 'web', 'lvl_core_api.js'));
    const core = LvlCore.wrap(await require(moduleFile)({ locateFile: (f) => path.join(MODULE, f) }));
    const files = [];
    for (const dir of ['Original-Ants/Maps', 'Community-Maps']) for (const name of fs.readdirSync(path.join(ROOT, dir)).sort()) if (/\.lvl$/i.test(name)) files.push(path.join(ROOT, dir, name));
    let playable = 0, partial = 0, refused = 0, bigRows = 0, wrong = 0, stopped = 0, first = '';
    const two = [];
    for (const file of files) {
        const bytes = new Uint8Array(fs.readFileSync(file)), before = digest(bytes);
        const c = core.checkMap(bytes, 'extended'), r = core.readReport(bytes, 'extended'), f = File.parse(bytes);
        if (c.status !== 0 || r.status !== 0 || !f.ok) { wrong++; first = first || file; continue; }
        const v = Play.verdict(c.json, r.json), facts = Blocks.facts(f, true);
        // the file's own start markers and hills (read by the page's own reader of the blocks) against what the verdict says
        const own = [0, 1, 2, 3].map((t) => facts.ants[t] > 0 && facts.hills[t] > 0);
        if (f.rows > 90 || f.kind !== 'classic') { if (v.ok) { wrong++; first = first || file + ' (should be refused)'; } refused++; bigRows += f.rows > 90 ? 1 : 0; continue; }
        const stop = c.json.findings.some((x) => x.severity === 'fatal' && !(x.team >= 0));          // (a stop that is about no color: the check says that the map does not load)
        const should = !stop && own.filter(Boolean).length >= 2;
        if (stop) stopped++;
        if (v.ok !== should) { wrong++; first = first || file + ' (ok ' + v.ok + ', should be ' + should + ')'; continue; }
        if (v.ok) {
            const seatsOk = v.seats.every((s) => !s.usable || own[s.seat]);       // (a color that the verdict lets play has ants and a hill in the file)
            const complete = v.seats.every((s, t) => s.usable === own[t]) || c.json.findings.some((x) => x.code === 'hills_several' || x.code === 'hill_without_entrance' || x.severity === 'fatal');
            if (!seatsOk || !complete) { wrong++; first = first || file + ' (seats ' + usable(v) + ' against ' + own.map(Number).join('') + ')'; }
            v.seats.every((s) => s.usable) ? playable++ : partial++;
            if (v.seats.filter((s) => s.usable).length === 2) two.push(path.basename(file));
        } else refused++;
        if (digest(bytes) !== before) { wrong++; first = first || file + ' (changed)'; }
    }
    check(wrong === 0, 'the library: every verdict agrees with the file\'s own start markers and hills (' + wrong + ' wrong' + (first ? ', first: ' + first : '') + ')');
    check(playable > 100 && files.length >= 540, 'many maps of the library can play with all four colors (' + playable + ' of ' + files.length + '; ' + partial + ' with some colors, ' + refused + ' refused, ' + bigRows + ' of those of more than 90 rows)');
    console.log('    library: ' + files.length + ' maps, ' + playable + ' with four colors, ' + partial + ' with fewer, ' + refused + ' refused (' + stopped + ' stopped by the check)');
    // the originals: every one can be played
    for (const name of ['TINY', 'SMALL', 'MEDIUM', 'GAUNTLET', 'TREASURE', 'ISLANDS']) {
        const bytes = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', name + '.LVL')));
        const v = Play.verdict(core.checkMap(bytes, 'extended').json, core.readReport(bytes, 'extended').json);
        check(v.ok && usable(v) === '1111', name + ' plays with four colors');
    }
    // made maps: a blank map has no color, an Extended map up to 512 x 512 plays when it has hills and ants
    const blank = core.blank(31, 31, 485, 6, 10, 'new_map').bytes;
    let v = Play.verdict(core.checkMap(blank, 'extended').json, core.readReport(blank, 'extended').json);
    check(!v.ok && /two colors that each have a hill and starting ants/.test(v.text) && v.list.length === 8 && v.list[0] === 'Green has no hill', 'a blank map: refused, with the eight sentences of the check');
    const big = core.blankExtended(300, 300, 485, 6, 10).bytes, small = core.blankExtended(40, 40, 485, 6, 10).bytes;
    v = Play.verdict(core.checkMap(big, 'extended').json, core.readReport(big, 'extended').json);
    check(!v.ok && /two colors that each have a hill and starting ants/.test(v.text) && !v.convert, 'a 300 x 300 blank Extended map: within 512 x 512, judged for hills and ants');
    v = Play.verdict(core.checkMap(small, 'extended').json, core.readReport(small, 'extended').json);
    check(!v.ok && /two colors that each have a hill and starting ants/.test(v.text) && !v.convert, 'a 40 x 40 blank Extended map: judged for hills and ants like a Classic map');
    const tinyUp = core.convertToExtended(new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL'))), 'extended');
    if (tinyUp.status === 0) { v = Play.verdict(core.checkMap(tinyUp.bytes, 'extended').json, core.readReport(tinyUp.bytes, 'extended').json); check(v.ok && usable(v) === '1111', 'a Classic map that was made an Extended map plays with four colors'); }
    // Start on the real core: the bytes that go to the game are the file's bytes
    const tiny = new Uint8Array(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL'))), p = pageOf();
    Play.open({ doc: p.doc, win: p.win, dialogs: p.dialogs, core, map: () => ({ name: 'TINY', bytes: tiny }), convert() {} });
    p.host.querySelectorAll('.dlg-foot button')[1].click();
    const id = /testmap=([a-z0-9]{12})&seats=(\w{4})/.exec(p.opened[0][0]);
    check(id && id[2] === 'ymnn' && digest(p.win.antsTestGameHandoff(id[1]).bytes) === digest(fs.readFileSync(path.join(ROOT, 'Original-Ants', 'Maps', 'TINY.LVL'))), 'TINY: Start hands the file\'s own bytes to the game (the library file, byte for byte)');
}

(async () => {
    for (const [name, fn] of [['A the verdict', verdictChecks], ['B the game page', gameChecks], ['C the dialogs', dialogChecks], ['D the real core', coreChecks]]) {
        process.stdout.write('  RUNNING: ' + name + ' ... ');
        const before = failures;
        try { await fn(); } catch (e) { failures++; console.log('\n    FAILED: threw ' + (e && e.stack ? e.stack : e)); }
        if (failures === before) console.log('PASS');
    }
    console.log('play check: ' + checks + ' checks' + (failures ? ', ' + failures + ' failed' : ' passed'));
    process.exit(failures ? 1 : 0);
})();

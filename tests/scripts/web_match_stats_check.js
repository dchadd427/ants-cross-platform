// Runs the OWN code of the stats sections of the match page (web/match_stats.js: the band of highlights, "Over time", "Activity", the note of a match without stats) with the real AntsHistory on a small fake of
// the browser's DOM: what each section builds from a record's `details`, that a record without details or with damaged ones builds nothing and throws nothing, that a name is text, and the plain
// functions (the clock, the steps of a chart, the color of a cell) on tables. tests/scripts/test_web_match_stats.py runs it with node (the quick tier), and again on broken copies, which it must refuse.
// usage: node web_match_stats_check.js web/match_stats.js web/history_page.js
'use strict';
const path = require('path');
const [statsPath, historyPath] = process.argv.slice(2).map((p) => path.resolve(p));
const H = require(historyPath);
global.AntsHistory = H;                                                         // (the page loads history_page.js first)
const MS = require(statsPath);
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

// ---- a fake DOM: elements, text, attributes, style, a width ----
class Node {
    constructor(tag, text) { this.tag = tag; this.children = []; this.parentNode = null; this.className = ''; this.attrs = {}; this.style = { setProperty: (k, v) => { this.style[k] = v; } }; this._text = text; this.clientWidth = 0; this.id = ''; this.title = ''; this.href = ''; }
    appendChild(c) { c.parentNode = this; this.children.push(c); return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'class') this.className = String(v); }
    getAttribute(k) { return this.attrs[k]; }
    set textContent(t) { this.children.forEach((c) => { c.parentNode = null; }); this.children = []; this._text = String(t); }
    get textContent() { return (this._text === undefined ? '' : this._text) + this.children.map((c) => c.textContent).join(''); }
    all(test, out = []) { this.children.forEach((c) => { if (test(c)) out.push(c); c.all(test, out); }); return out; }
    byClass(cls) { return this.all((n) => n.className.split(' ').includes(cls)); }
    byTag(tag) { return this.all((n) => n.tag === tag); }
    querySelectorAll(sel) { return sel === '.chartbox' ? this.byClass('chartbox') : []; }
}
const document = { createElement: (t) => new Node(t), createElementNS: (ns, t) => new Node(t), createTextNode: (t) => new Node('#text', t) };
const STYLE = { '--c-green': '#79bf93', '--c-red': '#d4707f', '--c-blue': '#8f8fd6', '--c-black': '#b3afb9' };
const S = MS.sections(document, (name) => STYLE[name]);

function seat(i, name, bot, extra) { return Object.assign({ seat: i, color: ['Green', 'Red', 'Blue', 'Black'][i], name, bot, score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }, extra || {}); }
function record(details, extra) {
    return Object.assign({ v: 1, id: 'ants-TREASURE-20261015-051557Z', ended: 1792041357, map: 'TREASURE.LVL', game: 'v0.12.41', turns: 14405, seconds: 720, finished: true, quitter: null, mode: 'highest-score', format: '2v2', draw: false, result: 'decided', computers_only: false,
        seats: [seat(0, 'Gory', ''), seat(1, 'Dethcon', ''), seat(2, 'Finn', 'Hard', { winner: true }), seat(3, 'Cocoa', 'Hard')], rows: [{ seats: [2], score: 2040, killed: 3, lost: 4, hatched: 0, winner: true }],
        recording: 'watch', file: 'ants-TREASURE-20261015-051557Z.antsrep', keep_days: 30, details: details }, extra || {});
}
// the details of a real match (ants-TREASURE-20261015-051557Z, shortened to 6 points of the series)
const DETAILS = {
    v: 1, turns: 14405, step: 300,
    over_time: { score: [[0, 100, 300, 500, 300, 140], [0, 50, 300, 600, 900, 1010], [0, 90, 400, 900, 1500, 2040], [0, 120, 500, 100, 0, 70]], kills: [[0, 0, 1, 3, 5, 7], [0, 1, 2, 4, 6, 7], [0, 1, 2, 3, 3, 3], [0, 0, 1, 1, 2, 2]] },
    highlights: { first_blood: { t: 967, by: 2, of: 0 }, last_kill: { t: 14294, by: 0, of: 1 }, lead: [[300, 3, -1], [4600, 2, -1]], comeback: { seat: 2, behind: 285, t: 4000 }, wiped_out: [[3, 12272, 1]] },
    activity: { per_minute: [[68, 59, 70, 42], [91, 69, 101, 71], [52, 125, 57, 24], [79, 48, 63, 9]], by_kind: { move: [284, 387, 234, 163], attack: [307, 503, 100, 43], bomb: [2, 0, 15, 48], raid: [7, 25, 24, 6] }, ants: [{ Worker: 30 }, {}, {}, {}] },
    combat: { kills: [[0, 4, 3, 0], [2, 0, 0, 5], [2, 0, 0, 1], [0, 1, 1, 0]], dealt: [96, 115, 18, 50], taken: [67, 87, 57, 68] },
};
const text = (n) => (n ? n.textContent : '');
function build(rec) { const out = { summary: S.summary(rec), special: S.special(rec), overtime: S.overtime(rec), activity: S.activity(rec) }; return out; }
function draw(node, width) { node.byClass('chartbox').forEach((b) => { b.clientWidth = width; }); MS.redraw(node); }

// ---- the plain functions ----
same('the clock is minutes and seconds of 20 turns a second', [MS.clock(0), MS.clock(967), MS.clock(14405), MS.clock(-5), MS.clock('x')], ['0:00', '0:48', '12:00', '0:00', '0:00']);
same('a step of 1, 2 or 5 times a power of ten', [MS.niceStep(2040, 5), MS.niceStep(7, 5), MS.niceStep(100, 4), MS.niceStep(0, 5), MS.niceStep(NaN, 5), MS.niceStep(36000, 5)], [500, 2, 50, 1, 1, 10000]);
same('a cell is the seat color mixed into the dark box, with the ink that reads on it', [MS.tintOf([121, 191, 147], 0).background, MS.tintOf([121, 191, 147], 1).background, MS.tintOf([121, 191, 147], 0).color, MS.tintOf([121, 191, 147], 1).color, MS.tintOf([121, 191, 147], 9).background],
    ['rgb(13,23,31)', 'rgb(121,191,147)', '#f4ece2', '#06120d', 'rgb(121,191,147)']);
same('the lead as a sentence', MS.leadSentence([{ t: 300, seat: 3, ally: -1 }, { t: 4600, seat: 2, ally: 0 }]), ['Changed hands 1 time: ', { seat: 3, ally: -1 }, ' until 3:50', ', then ', { seat: 2, ally: 0 }, ' to the end', '.']);
same('... and when it never changed', MS.leadSentence([{ t: 300, seat: 1, ally: -1 }]), ['Never changed hands: ', { seat: 1, ally: -1 }, ' to the end', '.']);
same('... nothing when nobody led', MS.leadSentence([]), null);

// ---- the sections of a match with stats ----
{
    const rec = record(DETAILS), ctx = MS.contextOf(rec);
    same('the chips: first blood, the lead, the comeback, the busiest, in this order, each to its section', MS.chipsOf(ctx).map((c) => [c.label, c.href]), [['First blood', '#overtime'], ['Lead', '#overtime'], ['Comeback', '#overtime'], ['Busiest', '#activity']]);
    const b = build(rec);
    same('the band is one list of four links', [b.summary.id, b.summary.byTag('li').length, b.summary.byTag('a').map((a) => a.href)], ['summary', 4, ['#overtime', '#overtime', '#overtime', '#activity']]);
    same('the chips say it in words', b.summary.byTag('a').map(text), ['First bloodFinn (bot, Hard) on Gory, 0:48', 'LeadChanged hands 1 time; Finn (bot, Hard) took it at 3:50', 'ComebackFinn (bot, Hard) won from 285 points behind (at 3:20)', 'BusiestDethcon: 332 orders']);
    check('the color squares of the chips are the seats\'', b.summary.byTag('i').filter((i) => i.className === 'sq').map((i) => i.attrs['data-c']).join() === 'Blue,Green,Blue,Blue,Red', b.summary.byTag('i').map((i) => i.attrs['data-c']).join());

    const ot = b.overtime;
    same('over time: its id, its title, and how often it looks', [ot.id, ot.attrs['aria-labelledby'], text(ot.byTag('h2')[0]), text(ot.byClass('count')[0])], ['overtime', 'overtime-h', 'Over time', 'Every 15 s · 12:00 long']);
    same('the legend gives each seat its color and its shape', ot.byClass('key')[0].byTag('i').map((i) => [i.className, i.attrs['data-c']]), [['mk circle', 'Green'], ['mk square', 'Red'], ['mk triangle', 'Blue'], ['mk diamond', 'Black']]);
    check('nothing is drawn before the width is known', ot.byTag('svg').length === 0);
    draw(ot, 700);
    const charts = ot.byTag('svg');
    same('two charts, the score and the kills, as wide as their boxes', [charts.length, charts.map((c) => c.attrs.width)], [2, ['700', '700']]);
    same('a line and a mark for each seat in each chart', charts.map((c) => [c.byClass('ln').length, c.byClass('mkp').length >= 4]), [[4, true], [4, true]]);
    same('the lines are the seats\'', charts[0].byClass('ln').map((p) => p.attrs['data-c']), ['Green', 'Red', 'Blue', 'Black']);
    check('the chart says what it shows, with the end values', /^Score of every seat over time\. At the end: Gory 140, Dethcon 1,010, Finn \(bot, Hard\) 2,040, Cocoa \(bot, Hard\) 70$/.test(charts[0].attrs['aria-label']), charts[0].attrs['aria-label']);
    check('first blood is marked on both', charts.every((c) => c.byClass('flagt').map(text).join() === 'First blood 0:48'));
    check('the y axis reaches the top in round steps (0, 500 ... 2,500)', charts[0].byClass('tick').map(text).slice(0, 6).join() === '0,500,1,000,1,500,2,000,2,500', charts[0].byClass('tick').map(text).slice(0, 7).join());
    const strip = ot.byClass('leadbar')[0];
    same('who led: a segment for each, as long as it lasted', strip.children.map((c) => [c.attrs['data-c'], c.style.flex]), [['Black', '4600 1 0'], ['Blue', '9805 1 0']]);
    const facts = ot.byClass('facts')[0];
    same('the highlights under the charts', facts.byTag('dt').map(text), ['First blood', 'Last kill', 'Lead', 'Comeback', 'Wiped out']);
    same('... in words', facts.byTag('dd').map(text), ['Finn (bot, Hard) killed an ant of Gory at 0:48', 'Gory killed an ant of Dethcon at 11:54', 'Changed hands 1 time: Cocoa (bot, Hard) until 3:50, then Finn (bot, Hard) to the end.', 'Finn (bot, Hard) was 285 points behind at 3:20 and won.', 'Cocoa (bot, Hard) had no ant left at 10:13; an egg hatched a new one.']);
    // drawn again: the same width keeps the chart, another width makes a new one
    const first = ot.byTag('svg')[0];
    draw(ot, 700);
    check('the same width draws nothing again', ot.byTag('svg')[0] === first);
    draw(ot, 360);
    same('a narrower box draws its chart again, for its width', [ot.byTag('svg').length, ot.byTag('svg')[0].attrs.width, ot.byTag('svg')[0] === first], [2, '360', false]);
    draw(ot, 100);
    check('a box is never drawn narrower than 280', ot.byTag('svg')[0].attrs.width === '280');

    const ac = b.activity;
    same('activity: id, title, the orders in all', [ac.id, text(ac.byTag('h2')[0]), text(ac.byClass('count')[0])], ['activity', 'Activity', '1,028 orders in all']);
    const rows = ac.byClass('hrow');
    same('the heat strip is a header and a row for each seat, a cell for each minute', [rows.length, rows.map((r) => r.byClass('hc').length), ac.byClass('heat')[0].style['--n']], [5, [0, 4, 4, 4, 4], 4]);
    same('... the busiest minute of each seat is ringed', rows.slice(1).map((r) => r.byClass('top').map(text)), [['70'], ['101'], ['125'], ['79']]);
    const cells = rows[3].byClass('hc');
    check('... every cell with orders is painted, the darkest with the dark ink', cells.every((c) => c.style.background) && cells[1].style.color === '#06120d', JSON.stringify(cells.map((c) => c.style.color)));
    check('... the quietest is dark with the light ink', rows[4].byClass('hc')[3].style.color === '#f4ece2');
    same('orders by kind: a column for each kind that the page names', ac.byTag('th').map(text), ['Player', 'Orders', 'Moves', 'Attacks', 'Bombs', 'Fires', 'Bridges', 'Raids']);
    same('... and a row for each seat with its numbers', ac.byTag('tr').slice(1).map((r) => r.byTag('td').map((d) => text(d.byTag('b')[0] || d))), [['Gory', '239', '284', '307', '2', '0', '0', '7'], ['Dethcon', '332', '387', '503', '0', '0', '0', '25'], ['Finn (bot, Hard)', '258', '234', '100', '15', '0', '0', '24'], ['Cocoa (bot, Hard)', '199', '163', '43', '48', '0', '0', '6']]);
    same('... a card for each seat on a phone', ac.byClass('sc-card').length, 4);
    check('the footnote of the orders says that an order with no ant of the seat is counted too', ac.byClass('lgd').map(text).some((t) => t.endsWith('Orders that name no ant of the seat are counted too.')), ac.byClass('lgd').map(text).join(' | '));
    same('who was busiest', ac.byClass('facts')[0].byTag('dd').map(text), ['Dethcon: 332 orders, 28 a minute', 'Minute 2 (1:00 to 2:00): 301 orders from all seats', 'Finn (bot, Hard) in minute 2: 125 orders']);
}

{
    const small = JSON.parse(JSON.stringify(DETAILS));
    small.highlights.comeback = { seat: 2, behind: 99, t: 4000 };
    small.activity.per_minute[0][3] = 0;
    const rec = record(small), b = build(rec);
    same('a comeback of less than 100 points is no chip (but it is a fact)', [MS.chipsOf(MS.contextOf(rec)).map((c) => c.label), b.overtime.byClass('facts')[0].byTag('dt').map(text).includes('Comeback')], [['First blood', 'Lead', 'Busiest'], true]);
    const zero = b.activity.byClass('hrow')[1].byClass('hc')[3];
    same('a minute with no order is a dot, not painted, not ringed', [text(zero), zero.style.background, zero.className], ['·', undefined, 'hc z']);
}

// ---- a match with no fights, an alliance on top, mode 187, a match that was not played by the rules of the page ----
{
    const quiet = JSON.parse(JSON.stringify(DETAILS));
    quiet.over_time.kills = [[0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0]];
    quiet.combat = { kills: [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]], dealt: [0, 0, 0, 0] };
    delete quiet.highlights.first_blood; delete quiet.highlights.last_kill; delete quiet.highlights.comeback;
    quiet.highlights.lead = [[500, 1, 2]];
    const rec = record(quiet), b = build(rec);
    same('no kill: the chip says so, the lead names both allies, the chart of kills is a sentence', [MS.chipsOf(MS.contextOf(rec)).map((c) => c.label), text(b.summary.byTag('a')[0]), text(b.summary.byTag('a')[1])], [['Fights', 'Lead', 'Busiest'], 'FightsNo ant was lost in this match', 'LeadDethcon & Finn (bot, Hard) led from 0:25 to the end']);
    draw(b.overtime, 600);
    same('... one chart and "No ant was killed"', [b.overtime.byTag('svg').length, text(b.overtime.byClass('quiet')[0])], [1, 'No ant was killed in this match.']);
    same('... and a strip of two tones', b.overtime.byClass('leadbar')[0].children.map((c) => [c.className, c.attrs['data-c'], c.attrs['data-c2']]), [['two', 'Red', 'Blue']]);

    // the pass attributes a death to the team of the last damage: an ant that died with no killer is in no kill and no damage, but the scoreboard has it as lost
    const lostSome = record(quiet, { seats: [seat(0, 'Gory', '', { lost: 2 }), seat(1, 'Dethcon', '', { lost: 5 }), seat(2, 'Finn', 'Hard', { lost: 6 }), seat(3, 'Cocoa', 'Hard', { lost: 4 })] });
    same('no kill or damage, but ants lost on the scoreboard: the band does not say that no ant was lost', MS.chipsOf(MS.contextOf(lostSome)).map((c) => c.label), ['Lead', 'Busiest']);
    const withEvent = JSON.parse(JSON.stringify(quiet));
    withEvent.combat.ev = [[100, -1, 1, 0, 0]];
    same('... nor when the list of kills has one (an ant that died with no team to blame)', MS.chipsOf(MS.contextOf(record(withEvent))).map((c) => c.label), ['Lead', 'Busiest']);
    const lostUnknown = record(quiet, { seats: [seat(0, 'Gory', '', { lost: undefined }), seat(1, 'Dethcon', ''), seat(2, 'Finn', 'Hard'), seat(3, 'Cocoa', 'Hard')] });
    same('... nor when the scoreboard does not say how many were lost', MS.chipsOf(MS.contextOf(lostUnknown)).map((c) => c.label), ['Lead', 'Busiest']);
    same('... and one seat that lost one ant is enough', MS.chipsOf(MS.contextOf(record(quiet, { seats: [seat(0, 'Gory', ''), seat(1, 'Dethcon', ''), seat(2, 'Finn', 'Hard', { lost: 1 }), seat(3, 'Cocoa', 'Hard')] }))).map((c) => c.label), ['Lead', 'Busiest']);

    const k187 = record(JSON.parse(JSON.stringify(DETAILS)), { mode: '187' });
    delete k187.details.over_time.score;
    const b187 = build(k187);
    draw(b187.overtime, 600);
    same('mode 187 has no score: one chart of kills, titled so', [b187.overtime.byTag('svg').length, b187.overtime.byTag('h3').map(text), /^Kills of every seat/.test(b187.overtime.byTag('svg')[0].attrs['aria-label'])], [1, ['Kills so far'], true]);
}

// ---- special ants: a card for each type that somebody had, a row for each seat in the scoreboard's order, the numbers from the seats' rows and from details ----
{
    const SEATS4 = [seat(0, 'Hazel', '', { winner: true, fires_lit: 9, bombs_planted: 4, bombs_defused: 0 }), seat(1, 'Clover', '', { winner: true, fires_lit: 14, bombs_planted: 0, bombs_defused: 0 }),
        seat(2, 'Fern', '', { fires_lit: 5, bombs_planted: 7, bombs_defused: 6 }), seat(3, 'Pip', 'Hard', { fires_lit: 0, bombs_planted: 12, bombs_defused: 1 })];
    const ROWS = [{ seats: [0, 1], score: 3050, killed: 14, lost: 9, hatched: 0, winner: true }, { seats: [2, 3], score: 2010, killed: 5, lost: 10, hatched: 1, winner: false }];
    const SP = {
        economy: { food: [[22, 550], [17, 425], [19, 475], [12, 300]], banked: [0, 0, 0, 0], hatch_cost: [0, 0, 0, 0], bombs_off: [3, 0, 2, 4],
            taken: [[1, 1, 2, 2, 2], [0, 2, 1, 1, 0], [2, 1, 1, 0, 1], [3, 0, 0, 3, 1]] },
        combat: { kills: [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]], dealt: [41, 19, 30, 12], taken: [19, 41, 12, 30] },
        special: { built: [6, 0, 3, 2], undug: [1, 0, 4, 0], out: [2, 0, 7, 0], raids: [[8, 5, 250], [3, 3, 130], [6, 2, 100], [0, 0, 0]] },
    };
    const withSpecial = (details, extra) => record(Object.assign({ v: 1, turns: 14405, step: 300 }, details), Object.assign({ seats: SEATS4, rows: ROWS }, extra || {}));
    const rec = withSpecial(SP), sec = S.special(rec), model = MS.specialOf(MS.contextOf(rec));
    same('the section: its id, its title and what it says it shows', [sec.id, sec.attrs['aria-labelledby'], text(sec.byTag('h2')[0]), text(sec.byClass('count')[0])], ['special', 'special-h', 'Special ants', 'What each type of ant did, seat by seat']);
    same('the seats are in the scoreboard\'s order (the winning side first), not in the order of the colors', [model.order, MS.specialOf(MS.contextOf(withSpecial(SP, { rows: [{ seats: [3], winner: true }, { seats: [0, 1] }, { seats: [2] }] }))).order], [[0, 1, 2, 3], [3, 0, 1, 2]]);
    same('a card for each type that somebody had, and the card of food and fighting last and wide', [sec.byClass('abcard').map((c) => text(c.byTag('h3')[0])), sec.byClass('abcard').map((c) => c.className)],
        [['Swimmer', 'Fire Ant', 'Bomber', 'Thief', 'Food and fighting'], ['abcard', 'abcard', 'abcard', 'abcard', 'abcard wide']]);
    same('the columns of each card: the ants of the type first, then the numbers of the type', sec.byClass('abt').map((t) => t.byTag('th').map(text)),
        [['Player', 'Ants', 'Built', 'Taken down'], ['Player', 'Ants', 'Lit', 'Put out'], ['Player', 'Ants', 'Set', 'Defused', 'Went off'], ['Player', 'Ants', 'Raids', 'Loot'], ['Player', 'Food trips', 'Combat Ants', 'Damage dealt', 'Damage taken']]);
    const rowsOf = (card) => card.byTag('tbody')[0].byTag('tr').map((r) => r.byTag('td').map((d) => text(d.byTag('b')[0] || d)));
    same('Swimmer: the ants that took the power-up, the bridges built and the bridges dug away', rowsOf(sec.byClass('abt')[0]), [['Hazel', '2', '6', '1'], ['Clover', '0', '0', '0'], ['Fern', '1', '3', '4'], ['Pip (bot, Hard)', '1', '2', '0']]);
    same('Fire Ant: the fires lit are the seat\'s row, the fires put out are the pass\'s', rowsOf(sec.byClass('abt')[1]), [['Hazel', '1', '9', '2'], ['Clover', '2', '14', '0'], ['Fern', '1', '5', '7'], ['Pip (bot, Hard)', '0', '0', '0']]);
    same('Bomber: bombs set and defused are the seat\'s row, the bombs that went off are the pass\'s', rowsOf(sec.byClass('abt')[2]), [['Hazel', '1', '4', '0', '3'], ['Clover', '0', '0', '0', '0'], ['Fern', '2', '7', '6', '2'], ['Pip (bot, Hard)', '3', '12', '1', '4']]);
    same('Thief: the raids that brought loot home, and the loot', rowsOf(sec.byClass('abt')[3]), [['Hazel', '2', '5', '250'], ['Clover', '1', '3', '130'], ['Fern', '1', '2', '100'], ['Pip (bot, Hard)', '0', '0', '0']]);
    same('Food and fighting', rowsOf(sec.byClass('abt')[4]), [['Hazel', '22', '2', '41', '19'], ['Clover', '17', '1', '19', '41'], ['Fern', '19', '0', '30', '12'], ['Pip (bot, Hard)', '12', '3', '12', '30']]);
    const raidCells = sec.byClass('abt')[3].byTag('tbody')[0].byTag('tr').map((r) => r.byTag('td')[2]);
    same('raids are "5 of 8": the number home, and how many were tried, and the bar is the share that got home', [raidCells.map((c) => text(c.byClass('of')[0])), raidCells.map((c) => c.byClass('mb')[0].children[0].style.width)], [['of 8', 'of 3', 'of 6', ''], ['63%', '100%', '33%', '0%']]);
    same('the other bars are against the best seat of the column', sec.byClass('abt')[0].byTag('tbody')[0].byTag('tr').map((r) => r.byTag('td')[2].byClass('mb')[0].children[0].style.width), ['100%', '0%', '50%', '33%']);
    same('a seat that had none of the type is a dim row; the others are not', sec.byClass('abt').slice(0, 4).map((t) => t.byTag('tbody')[0].byTag('tr').map((r) => r.className)), [['', 'none', '', ''], ['', '', '', 'none'], ['', 'none', '', ''], ['', '', '', 'none']]);
    same('every bar is the color of its seat', sec.byClass('abt')[0].byTag('tbody')[0].byTag('tr').map((r) => r.byTag('i').filter((i) => i.attrs['data-c'] !== undefined).map((i) => i.attrs['data-c']).join()), ['Green,Green,Green,Green', 'Red,Red,Red,Red', 'Blue,Blue,Blue,Blue', 'Black,Black,Black,Black']);
    same('the key lines say what each number means', sec.byClass('lgd').map(text).slice(0, 4), [
        'Built: water squares bridged all the way. Taken down: finished bridges its Swimmers dug away. A bridge that falls by itself after 3 minutes is not counted.',
        'Lit: fire walls started. Put out: fires its Fire Ants put out, its own or anyone\u2019s. A fire that burns out by itself after 3 minutes is not counted.',
        'Set: bombs put down. Defused: enemy bombs taken away. Went off: its bombs that an ant stepped on.',
        'Raids: \u201c5 of 8\u201d is 8 times a Thief went into an enemy hill, 5 times the loot reached its own hill. Loot: the points brought home.']);
    same('every type had somebody: no line about a type that nobody had, and the line of Ants closes the section', [sec.byClass('abnote').length, text(sec.byClass('lgd')[sec.byClass('lgd').length - 1])], [0, 'Ants: how many of the seat\u2019s ants took the power-up and became this type.']);
    check('the section comes from the record: nothing is a node of its own', sec.all((n) => n.tag === 'img' || n.tag === 'script').length === 0);

    // a type that nobody had is left out, with one line
    const some = JSON.parse(JSON.stringify(SP));
    some.economy.taken = [[1, 1, 0, 2, 0], [0, 2, 0, 1, 0], [2, 1, 0, 0, 0], [3, 0, 0, 3, 0]];
    some.special = { built: [0, 0, 0, 0], undug: [0, 0, 0, 0], out: [2, 0, 7, 0], raids: [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]] };
    const partial = S.special(withSpecial(some));
    same('only the types that were used have a card; a line says which were not', [partial.byClass('abcard').map((c) => text(c.byTag('h3')[0])), partial.byClass('abnote').map(text)], [['Fire Ant', 'Bomber', 'Food and fighting'], ['Nobody had a Swimmer or Thief in this match.']]);
    some.economy.taken = [[1, 0, 0, 2, 0], [0, 0, 0, 1, 0], [2, 0, 0, 0, 0], [3, 0, 0, 3, 0]]; some.special.out = [0, 0, 0, 0];
    same('a type is used when its numbers say so (the fires lit are in the seats\' rows), whatever the count of its power-ups', S.special(withSpecial(some)).byClass('abcard').map((c) => text(c.byTag('h3')[0])), ['Fire Ant', 'Bomber', 'Food and fighting']);
    same('three types that nobody had are listed with commas and an or', S.special(withSpecial(some, { seats: SEATS4.map((x) => Object.assign({}, x, { fires_lit: 0 })) })).byClass('abnote').map(text), ['Nobody had a Swimmer, Fire Ant or Thief in this match.']);

    // a Thief that raided and brought nothing home, on a level whose ants are Thieves from the start (no power-up was taken): the raids say that the type was used
    const raided = JSON.parse(JSON.stringify(SP));
    raided.economy.taken = [[0, 0, 0, 2, 0], [0, 0, 0, 1, 0], [0, 0, 0, 0, 0], [0, 0, 0, 3, 0]];
    raided.economy.bombs_off = [0, 0, 0, 0];
    raided.special = { built: [0, 0, 0, 0], undug: [0, 0, 0, 0], out: [0, 0, 0, 0], raids: [[3, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]] };
    const lone = S.special(withSpecial(raided, { seats: SEATS4.map((x) => Object.assign({}, x, { fires_lit: 0, bombs_planted: 0, bombs_defused: 0 })) }));
    same('raids that were tried make the type used, and "0 of 3" is not dimmed', [lone.byClass('abcard').map((c) => text(c.byTag('h3')[0])), lone.byClass('abt')[0].byTag('tbody')[0].byTag('tr').map((r) => r.className), text(lone.byClass('abt')[0].byTag('tbody')[0].byTag('tr')[0].byTag('td')[2])],
        [['Thief', 'Food and fighting'], ['', 'none', 'none', 'none'], '0of 3']);

    // nobody took a power-up: one line before the card of food and fighting
    const bare = { economy: { food: [[19, 475], [12, 300], [0, 0], [0, 0]], bombs_off: [0, 0, 0, 0], taken: [[0, 0, 0, 2, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]] },
        combat: { kills: [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]], dealt: [30, 12, 0, 0], taken: [12, 30, 0, 0] }, special: { built: [0, 0, 0, 0], undug: [0, 0, 0, 0], out: [0, 0, 0, 0], raids: [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]] } };
    const quietSeats = SEATS4.map((x) => Object.assign({}, x, { fires_lit: 0, bombs_planted: 0, bombs_defused: 0 }));
    const nobody = S.special(withSpecial(bare, { seats: quietSeats }));
    same('no power-up at all: the line, then the card of food and fighting, and no line of Ants', [nobody.children.map((c) => c.className || c.tag), nobody.byClass('abnote').map(text), nobody.byClass('abcard').map((c) => text(c.byTag('h3')[0]))],
        [['head', 'abnote', 'abgrid'], ['No ant took a power-up in this match.'], ['Food and fighting']]);
}

// ---- special ants in a record that lacks some of the numbers: it shows what it has and hides what it lacks ----
{
    const rowsOnly = [seat(0, 'Hazel', '', { fires_lit: 9, bombs_planted: 4, bombs_defused: 2 }), seat(1, 'Clover', '', { fires_lit: 0, bombs_planted: 0, bombs_defused: 0 })];
    const base = { v: 1, turns: 14405, step: 300 };
    const plain = (details) => record(details, { seats: rowsOnly, rows: [{ seats: [0] }, { seats: [1] }] });
    const oldDetails = { economy: { food: [[5, 125], [3, 75], [0, 0], [0, 0]], bombs_off: [1, 0, 0, 0], taken: [[1, 1, 0, 0, 0], [0, 0, 1, 0, 2], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]] },
        combat: { kills: [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]], dealt: [1, 2, 0, 0], taken: [2, 1, 0, 0], raids: [[300, 0, 1, 50], [400, 0, 1, 50], [900, 1, 0, 50]] } };
    const old = S.special(plain(Object.assign({}, base, oldDetails)));
    const cols = (sec) => sec.byClass('abt').map((t) => t.byTag('th').map(text));
    same('a record counted before the counts existed: no bridges, no fires put out, no loot; the raids that were begun are counted from the list', cols(old),
        [['Player', 'Ants'], ['Player', 'Ants', 'Lit'], ['Player', 'Ants', 'Set', 'Defused', 'Went off'], ['Player', 'Ants', 'Raids'], ['Player', 'Food trips', 'Combat Ants', 'Damage dealt', 'Damage taken']]);
    same('... a Swimmer card has only the ants, and the raids are "tried" (the list names the thief of each)', [old.byClass('abcard').map((c) => text(c.byTag('h3')[0])), old.byClass('abt')[3].byTag('tbody')[0].byTag('tr').map((r) => text(r.byTag('td')[2]))],
        [['Swimmer', 'Fire Ant', 'Bomber', 'Thief', 'Food and fighting'], ['2tried', '1tried']]);
    same('... when the list of raids is cut (raids_n) or left out, the raids are not known and the column is not shown', [cols(S.special(plain(Object.assign({}, base, { economy: oldDetails.economy, combat: Object.assign({}, oldDetails.combat, { raids_n: 40 }) })))).map((c) => c.includes('Raids')),
        cols(S.special(plain(Object.assign({}, base, { economy: oldDetails.economy, combat: { dealt: [1, 2, 0, 0], taken: [2, 1, 0, 0] } })))).map((c) => c.includes('Raids'))], [[false, false, false, false, false], [false, false, false, false, false]]);
    same('... and the key line of a card says only what its columns say (none for a card of ants alone)', old.byClass('abcard').map((c) => text(c.byClass('lgd')[0])),
        ['', 'Lit: fire walls started.', 'Set: bombs put down. Defused: enemy bombs taken away. Went off: its bombs that an ant stepped on.', 'Raids: times a Thief went into an enemy hill.',
         'Food trips: times an ant picked food up. Combat Ants: ants that took the Combat power-up. Damage: hit points taken from enemy ants, and lost to them.']);
    // no details at all: the seats' rows are what there is
    const bare = S.special(plain(undefined));
    same('a record with no details: the numbers of the rows (fires lit, bombs set and defused) and nothing else, no line about types nobody had, no food and fighting', [cols(bare), bare.byClass('abnote').length, bare.byClass('lgd').map(text).some((t) => t.startsWith('Ants:'))], [[['Player', 'Lit'], ['Player', 'Set', 'Defused']], 0, false]);
    same('... the Bomber and Fire Ant rows say what the seats did', bare.byClass('abt').map((t) => t.byTag('tbody')[0].byTag('tr').map((r) => r.byTag('td').map((d) => text(d.byTag('b')[0] || d)))), [[['Hazel', '9'], ['Clover', '0']], [['Hazel', '4', '2'], ['Clover', '0', '0']]]);
    same('a record whose rows say nothing was done and that has no details has no section', S.special(record(undefined, { seats: [seat(0, 'Hazel', ''), seat(1, 'Clover', '')], rows: [{ seats: [0] }, { seats: [1] }] })), null);
    same('a record that holds no number for a seat does not show that column (a text, a negative, nothing)', cols(S.special(record(undefined, { seats: [seat(0, 'Hazel', '', { fires_lit: 3, bombs_planted: 'x', bombs_defused: -2 }), seat(1, 'Clover', '', { fires_lit: 1, bombs_planted: 1, bombs_defused: 1 })], rows: [{ seats: [0] }, { seats: [1] }] }))), [['Player', 'Lit']]);
    // the marker of a pass that failed: only the rows
    same('the marker of a match that could not be counted is a record with no details, for this section', cols(S.special(plain({ v: 1, n: 1 }))), [['Player', 'Lit'], ['Player', 'Set', 'Defused']]);
    // mode 187 has no food
    const k187 = S.special(plain(Object.assign({}, base, oldDetails, { }))), mode = record(Object.assign({}, base, oldDetails), { mode: '187', seats: rowsOnly, rows: [{ seats: [0] }, { seats: [1] }] });
    same('mode 187 has no food to carry: no column of food trips', [cols(k187).pop(), cols(S.special(mode)).pop()], [['Player', 'Food trips', 'Combat Ants', 'Damage dealt', 'Damage taken'], ['Player', 'Combat Ants', 'Damage dealt', 'Damage taken']]);
}

// ---- special ants: a debt that came home, and details that are damaged ----
{
    const seats = [seat(0, 'Hazel', '', { fires_lit: 1 }), seat(1, 'Clover', '')];
    const debt = S.special(record({ economy: { taken: [[0, 0, 1, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]] }, special: { raids: [[1, 1, -30], [0, 0, 0], [0, 0, 0], [0, 0, 0]] } }, { seats, rows: [{ seats: [0] }, { seats: [1] }] }));
    const thief = debt.byClass('abt')[1].byTag('tbody')[0].byTag('tr')[0].byTag('td');
    same('a loot below zero is shown with its sign, and its bar is empty', [text(thief[3].byTag('b')[0]), thief[3].byClass('mb')[0].children[0].style.width], ['\u221230', '0%']);
    const hostile = { economy: { taken: [['a', null, 1e999, { x: 1 }, -5], 'text', [1e12, -1e12, NaN, 3.7, 2], null], food: 5, bombs_off: 'x', banked: 1 }, combat: { dealt: [1e30, 'x'], taken: 7, raids: 'no' },
        special: { built: [2e9, -2e9, 'z', null], undug: 'x', out: { a: 1 }, raids: [[1e20, 0, 0], 'x', [1, 1], null] } };
    let threw = null, sec = null;
    try { sec = S.special(record(hostile, { seats: [seat(0, '<img src=x onerror=alert(1)>', ''), seat(1, 'Clover', '', { bombs_planted: 1e999 }), seat(2, 'Fern', '', { fires_lit: 2 })], rows: [{ seats: [0, 1, 2, 9, -1, 1.5] }] })); } catch (e) { threw = e; }
    check('damaged numbers throw nothing', threw === null, threw && threw.stack);
    check('every number on the page is plain, short and bounded', !sec || sec.all((n) => n.tag === 'b').every((b) => /^[\u2212]?[0-9,]{1,15}$/.test(text(b))), sec && sec.all((n) => n.tag === 'b').map(text).join(' | '));
    check('a name that looks like markup is only text', !sec || (sec.byClass('who').map(text).some((t) => t.includes('<img src=x onerror=alert(1)>')) && sec.all((n) => n.tag === 'img' || n.tag === 'script').length === 0));
    check('a seat out of range is not a row', !sec || sec.byClass('abt').every((t) => t.byTag('tbody')[0].byTag('tr').length <= 3));
    const odd = S.special(record({ economy: { taken: [[0, 1, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]] } }, { seats: [seat(0, 'Hazel', '', { fires_lit: 2 }), Object.assign(seat(3, 'Pip', '', { fires_lit: 1 }), { seat: 9 })], rows: [{ seats: [0, 9] }] }));
    same('a seat that says it is number 9 is not a row', odd.byClass('abt')[0].byTag('tbody')[0].byTag('tr').map((r) => text(r.byTag('td')[0])), ['Hazel']);
}

// ---- no stats: nothing is built, and a line says why ----
{
    const none = (rec) => { const b = build(rec); return [b.summary, b.overtime, b.activity, b.special]; };
    same('a record with no details builds nothing (no special ant did anything in the seats\' rows either)', none(record(undefined)), [null, null, null, null]);
    same('... nor does one whose details are not an object', [none(record('text')), none(record([1, 2])), none(record(7)), none(record(null))], [[null, null, null, null], [null, null, null, null], [null, null, null, null], [null, null, null, null]]);
    same('... nor the marker of a match that could not be counted', none(record({ v: 1, n: 1 })), [null, null, null, null]);
    same('whether a record has stats', [MS.hasStats(record(DETAILS)), MS.hasStats(record(undefined)), MS.hasStats(record({ v: 1, n: 1 })), MS.hasStats(record({ over_time: { kills: [] } }))], [true, false, false, true]);
    same('a match with stats has no note', MS.noStatsNote(record(DETAILS)), '');
    same('an older version says whose it was and what is kept', MS.noStatsNote(record(undefined, { recording: 'old', game: 'v0.12.0' })), 'Over time and activity are counted from a recording that this version can play. This match was recorded by an older version (v0.12.0), so only the totals above were kept.');
    same('a recording that is gone before the stats were counted', MS.noStatsNote(record(undefined, { recording: 'removed', file: undefined })), 'Over time and activity are counted from the recording. This one was removed before they were, so only the totals above were kept.');
    same('a match that is not counted yet', MS.noStatsNote(record(undefined)), 'Over time and activity are counted from the recording a few minutes after a match ends. They are not ready for this match yet.');
    same('a match that could not be counted', MS.noStatsNote(record({ v: 1, n: 1 })), 'Over time and activity are counted from the recording, and this one could not be counted. Only the totals above were kept.');
    same('an older version with no name for the game', MS.noStatsNote(record(undefined, { recording: 'old', game: 5 })).includes('(unknown)'), true);
}

// ---- damaged details: no section throws, a name is text, a seat out of range is left out ----
{
    const hostile = {
        v: 'x', turns: 'many', step: -3,
        over_time: { score: [['a', null, { x: 1 }, 5], 'text', [1e999, -1, NaN], []], kills: 7 },
        highlights: { first_blood: { t: 'x', by: 9, of: -1 }, last_kill: { t: 5, by: 4.5, of: 2 }, lead: [[1, 7, 99], 'x', [2, 1, 1.5], null], comeback: { seat: 'z', behind: 1e9, t: {} }, wiped_out: [[9, 1, 1], 'x', [1, 'q', 'r']], out: 5, burst: { seat: 17, kills: 99 } },
        activity: { per_minute: [[1, 'x', null, 7], [new Array(500).fill(1)], 4, [3]], by_kind: { move: 'no', attack: [1, 2] }, ants: 'x' },
        combat: 5,
    };
    const names = [seat(0, '<img src=x onerror=alert(1)>', ''), seat(1, 'Dethcon', ''), seat(2, 'Finn', 'Hard'), seat(3, '"><script>x</script>', 'Easy')];
    let threw = null, b = null;
    try { b = build(record(hostile, { seats: names })); if (b.overtime) draw(b.overtime, 500); } catch (e) { threw = e; }
    check('damaged details throw nothing', threw === null, threw && threw.stack);
    check('no chart is drawn with a number that is not one', !b || !b.overtime || b.overtime.all((n) => n.attrs && Object.keys(n.attrs).some((k) => /NaN|Infinity/.test(n.attrs[k]))).length === 0);
    check('the minutes of a damaged strip are bounded', !b || !b.activity || b.activity.byClass('hrow')[0].byClass('hm').length <= 120);
    check('a seat out of range is not named', !b || [b.summary, b.overtime, b.activity].every((n) => !n || !n.byTag('i').some((i) => i.attrs['data-c'] === undefined && i.className === 'sq')));
    const bn = build(record(DETAILS, { seats: names }));
    check('a name that looks like markup is only text', text(bn.summary).includes('Finn') && bn.overtime.byClass('key')[0].textContent.includes('<img src=x onerror=alert(1)>') && bn.overtime.byClass('key')[0].textContent.includes('"><script>x</script> (bot, Easy)'));
    same('none of it makes a node of its own', bn.overtime.all((n) => n.tag === 'img' || n.tag === 'script').length, 0);
    const empty = record({ over_time: { score: [[], [], [], []], kills: [[], [], [], []] }, activity: { per_minute: [[], [], [], []] } });
    same('series with no point and a strip with no minute build nothing', none2(empty), [null, null]);
    function none2(rec) { return [S.overtime(rec), S.activity(rec)]; }
}

// ---- a corrupt record must not hang the page: a length of 30 million turns, a step of 1e300, lists of tens of thousands (the page cuts them; the sizes are chosen so that a page that does not is refused at once, not after a hang) ----
{
    const big = JSON.parse(JSON.stringify(DETAILS));
    big.turns = 3e7; big.step = 1e300;
    big.highlights.lead = Array.from({ length: 20000 }, (_, i) => [i * 10, i % 4, -1]);
    big.highlights.wiped_out = Array.from({ length: 5000 }, () => [1, 2, 1]);
    const t0 = Date.now();
    let threw = null, b = null;
    try { b = build(record(big)); draw(b.overtime, 700); } catch (e) { threw = e; }
    const took = Date.now() - t0;
    check('huge turns, a step of 1e300 and lists of tens of thousands: no throw, and done in a second or two', threw === null && took < 2000, threw ? threw.stack : took + ' ms');
    if (b && b.overtime) {
        const paths = b.overtime.byTag('path').map((n) => n.attrs.d).filter(Boolean);
        check('every path is plain numbers (no exponent, no Infinity)', paths.length > 0 && paths.every((d) => /^[MLHVhvz0-9. \-]+$/.test(d)), paths.filter((d) => !/^[MLHVhvz0-9. \-]+$/.test(d)).map((d) => d.slice(0, 80)).join(' | '));
        check('no attribute of the chart is an exponent or not a number', b.overtime.all((n) => n.attrs && Object.keys(n.attrs).some((k) => k !== 'd' && /^-?[0-9.]+e[+-]?[0-9]+$|NaN|Infinity/.test(n.attrs[k]))).length === 0);
        check('the strip of who led is bounded', b.overtime.byClass('leadbar')[0].children.length <= 200, b.overtime.byClass('leadbar')[0].children.length);
        check('the lines of the minutes are bounded', b.overtime.byTag('svg')[0].byClass('vgrid').length <= 61, b.overtime.byTag('svg')[0].byClass('vgrid').length);
        check('the facts under the charts are bounded', b.overtime.byClass('facts')[0].byTag('dd').length <= 12, b.overtime.byClass('facts')[0].byTag('dd').length);
    }
}

console.log((failures ? 'FAILED: ' + failures + ' of ' : 'ok: ') + checks + ' checks');
process.exit(failures ? 1 : 0);

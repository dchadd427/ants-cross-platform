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

function seat(i, name, bot, extra) { return Object.assign({ seat: i, colour: ['Green', 'Red', 'Blue', 'Black'][i], name, bot, score: 0, killed: 0, lost: 0, hatched: 0, bombs_planted: 0, bombs_defused: 0, fires_lit: 0, left: false, winner: false }, extra || {}); }
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
function build(rec) { const out = { summary: S.summary(rec), overtime: S.overtime(rec), activity: S.activity(rec) }; return out; }
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

// ---- no stats: nothing is built, and a line says why ----
{
    const none = (rec) => { const b = build(rec); return [b.summary, b.overtime, b.activity]; };
    same('a record with no details builds nothing', none(record(undefined)), [null, null, null]);
    same('... nor does one whose details are not an object', [none(record('text')), none(record([1, 2])), none(record(7)), none(record(null))], [[null, null, null], [null, null, null], [null, null, null], [null, null, null]]);
    same('... nor the marker of a match that could not be counted', none(record({ v: 1, n: 1 })), [null, null, null]);
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

// The Analytics section of the admin page (web/admin_analytics.js, docs/ANALYTICS.md) RUN in node with the DOM fake of the Map Maker's checks and a fake hub: the one question that it asks and with what, the
// range (7, 30 or 90 days, two dates, what a bad range says), the three views drawn from an answer (the tiles and their changes, the chart of visitors per day as bars with a hatched cap, as a table, and its card;
// the countries with their hatched share, the new and the returning, the referrers, the entry and exit pages, the time on the site with its middle, the devices, widths and languages; the two timing charts
// with their gaps and their marks for a new version, the errors grouped, the counter itself), the first day when nothing is counted, the files of the export (CSV and a zip that is read back: names, sizes,
// checksums), and that every text that came from a visitor's browser is put in as text and marked as data.
// usage: node web_admin_analytics_check.js ../../web/admin_analytics.js      (exit 0: every check holds; 1: one fails)
'use strict';
const path = require('path');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');
const A = require(path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'admin_analytics.js')));

let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function settle() { for (let i = 0; i < 10; i++) await tick(); }
const NOW = Date.UTC(2026, 11, 14, 19, 20, 0);                 // 2026-12-14 11:20 Pacific
const TODAY = '2026-12-14';

// ---- the pure parts ---------------------------------------------------------------------------------------------------
same('the Pacific day of a moment: before and after the midnight of the 14th', [A.pacificDay(Date.UTC(2026, 11, 14, 7, 59, 0)), A.pacificDay(Date.UTC(2026, 11, 14, 8, 0, 0))], ['2026-12-13', '2026-12-14']);
same('the Pacific day in summer time', [A.pacificDay(Date.UTC(2026, 6, 1, 6, 59, 0)), A.pacificDay(Date.UTC(2026, 6, 1, 7, 0, 0))], ['2026-06-30', '2026-07-01']);
same('days are added across months and years', [A.addDays('2026-12-14', -29), A.addDays('2026-12-31', 1), A.addDays('2026-03-01', -1), A.addDays('2024-03-01', -1)], ['2026-11-15', '2027-01-01', '2026-02-28', '2024-02-29']);
same('the presets: 7, 30 and 90 days end today', [A.rangeOf('7', TODAY), A.rangeOf('30', TODAY), A.rangeOf('90', TODAY)], [{ from: '2026-12-08', to: TODAY }, { from: '2026-11-15', to: TODAY }, { from: '2026-09-16', to: TODAY }]);
same('a custom range is the one that was typed', A.rangeOf('custom', TODAY, { from: '2026-11-28', to: '2026-12-14' }), { from: '2026-11-28', to: '2026-12-14' });
same('the words of a range', [A.rangeWords('2026-11-15', TODAY), A.rangeWords('2025-12-20', TODAY), A.rangeWords('2026-12-14', '2026-12-14')], ['15 Nov – 14 Dec 2026', '20 Dec 2025 – 14 Dec 2026', '14 Dec – 14 Dec 2026']);
same('what is wrong with a custom range', [['', TODAY], ['2026-12-14', '2026-12-14'], ['2026-12-14', '2026-12-15'], ['2026-12-02', '2026-12-01'], ['2025-11-01', TODAY], ['2025-11-10', TODAY]].map((r) => A.customProblem(r[0], r[1], TODAY)),
    ['Pick two days.', '', 'The last day cannot be after today.', 'The first day must not be after the last one.', 'The range can be up to 400 days.', '']);
same('a day that is not a day', [A.customProblem('2026-02-30', TODAY, TODAY), A.customProblem('x', 'y', TODAY)], ['Pick two days.', 'Pick two days.']);
same('weekends and names of days', [A.isWeekend('2026-12-12'), A.isWeekend('2026-12-13'), A.isWeekend('2026-12-14'), A.longDay('2026-12-14'), A.shortDay('2026-12-14')], [true, true, false, 'Mon 14 Dec', '14 Dec']);
same('thousands, percents and durations', [A.thousands(2257), A.thousands(1234567), A.thousands(0), A.percent(264, 3097), A.percent(1270, 3097), A.percent(1, 3), A.percent(0, 0), A.percent(1, 1000), A.duration(79), A.duration(340), A.duration(7200), A.seconds(900), A.seconds(15000)],
    ['2,257', '1,234,567', '0', '8.5%', '41%', '33%', '0%', '0.1%', '1 min 19 s', '5 min 40 s', '2 h 0 min', '0.9 s', '15 s']);
same('the change against the days before', [A.change(2257, 1930, 30), A.change(80, 100, 7), A.change(100, 100, 7), A.change(5, 0, 30), A.change(0, 0, 30)],
    [{ dir: 'up', text: '▲ 17% more than the 30 days before' }, { dir: 'down', text: '▼ 20% less than the 7 days before' }, { dir: 'same', text: 'About the same as the 7 days before' }, null, null]);
same('a step for the ticks of a chart', [A.niceStep(254, 3), A.niceStep(40, 3), A.niceStep(1, 3), A.niceStep(3400 / 1000, 4)], [100, 20, 0.5, 1]);
same('the weighted middle skips the days with too few timings', A.weightedMiddle([{ perf: { load: [100, 900, 1500, 3000] } }, { perf: { load: [5, 99999, 99999, 99999] } }, {}, { perf: { load: [100, 1100, 1700, 3400] } }], 'load', 1), 900);
same('and nothing is nothing', A.weightedMiddle([{}, { perf: { load: [3, 1, 1, 1] } }], 'load', 1), null);

// the files: CSV with its quotes and a guard against a formula; the zip is read back with a parser of its own
same('a CSV file: quotes, commas, new lines and the guard', A.csv(['a', 'b'], [['x,y', 'say "hi"'], ['=1+1', -5], [null, 'ok\nline'], ['+x', '@y'], [3, 4.5]]), 'a,b\r\n"x,y","say ""hi"""\r\n\'=1+1,-5\r\n,"ok\nline"\r\n\'+x,\'@y\r\n3,4.5\r\n');
function crcRef(bytes) { let c = 0xffffffff; for (const b of bytes) { c ^= b; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); } return (c ^ 0xffffffff) >>> 0; }
same('the checksum is the standard one', [A.crc32(Buffer.from('123456789')), A.crc32([])], [0xcbf43926, 0]);
function readZip(bytes) {
    const b = Buffer.from(bytes), out = [];
    const eocd = b.length - 22;
    if (b.readUInt32LE(eocd) !== 0x06054b50) return null;
    const count = b.readUInt16LE(eocd + 10), size = b.readUInt32LE(eocd + 12), start = b.readUInt32LE(eocd + 16);
    if (start + size !== eocd) return null;
    let at = start;
    for (let i = 0; i < count; i++) {
        if (b.readUInt32LE(at) !== 0x02014b50) return null;
        const crc = b.readUInt32LE(at + 16), csize = b.readUInt32LE(at + 20), usize = b.readUInt32LE(at + 24), nlen = b.readUInt16LE(at + 28), offset = b.readUInt32LE(at + 42);
        const name = b.slice(at + 46, at + 46 + nlen).toString('utf8');
        if (b.readUInt32LE(offset) !== 0x04034b50 || b.readUInt16LE(offset + 8) !== 0 || csize !== usize) return null;
        const data = b.slice(offset + 30 + b.readUInt16LE(offset + 26), offset + 30 + b.readUInt16LE(offset + 26) + usize);
        out.push({ name, text: data.toString('utf8'), crcOk: crc === crcRef(data) && crc === b.readUInt32LE(offset + 14) });
        at += 46 + nlen;
    }
    return out;
}
const zipped = readZip(A.zip([{ name: 'one.csv', text: 'a,b\r\n1,2\r\n' }, { name: 'two é.csv', text: 'café 你 😀\r\n' }, { name: 'empty.csv', text: '' }]));
same('a zip holds each file once, stored, with its checksum', zipped && zipped.map((f) => [f.name, f.text, f.crcOk]), [['one.csv', 'a,b\r\n1,2\r\n', true], ['two é.csv', 'café 你 😀\r\n', true], ['empty.csv', '', true]]);

// ---- the fake page, and a hub that answers --------------------------------------------------------------------------------------------
function report(o) {
    o = o || {};
    const days = [];
    for (let i = 0; i < (o.n || 30); i++) {
        const day = A.addDays(TODAY, i - ((o.n || 30) - 1)), v = 50 + (i % 7) * 5 + (i === 13 ? 200 : 0), nt = Math.round(v / 11);
        days.push({ day, visitors: v, visits: v + 10, nt, signups: i % 5 === 0 ? 3 : 0, errors: i === 29 ? 2 : 1, perf: { load: [60 + i, 900 + i, 1500, 3400], game: i < 3 ? [10, 4000, 7000, 15000] : [60, 4200, 7000, 15000] }, vers: i === 0 ? { '0.22.0': 5 } : i === 12 ? { '0.22.0': 3, '0.22.1': 4 } : {} });
    }
    return Object.assign({
        from: days[0].day, to: TODAY, today: TODAY, days, before: { visitors: 1930, visits: 2600, nt: 200, signups: 40, errors: 85 },
        lists: {
            visitors: 2257, 'new': 1648, returning: 609, visits: 2833, nt: 264, signups: 49, dur_sum: 200000, dur_mid: 79, errors_other: 0,
            refs: { '': 1388, 'discord.com': 595, 'google.com': 340, other: 99 }, entry: { '/': 1643, '/matches': 397, '/map': 100, other: 113 }, exit: { '/': 1020, '/play': 680 },
            browsers: { Chrome: 1174, Safari: 429, Firefox: 248 }, systems: { Windows: 1038, macOS: 384, iOS: 316 }, devices: { Desktop: 1535, Phone: 654, Tablet: 68 }, langs: { en: 1602, de: 181, fr: 113 },
            countries: { US: [1270, 100], DE: [341, 20], GB: [248, 10], '': [77, 0], FR: [155, 5], CA: [186, 10], NL: [124, 5], BR: [108, 5], AU: [93, 4], PL: [77, 3], SE: [62, 2], ES: [10, 1], IT: [9, 1], JP: [8, 1] },
            widths: [316, 338, 271, 700, 632], dur: [100, 200, 300, 150, 100, 100, 300, 200, 100, 100, 100, 50, 25, 10, 5]
        },
        errors: [{ kind: 'Game', msg: 'RuntimeError: memory access out of bounds', src: 'index.js:1', n: 25, visitors: 16, first: TODAY, last: NOW / 1000 - 600 },
                 { kind: 'Page', msg: 'TypeError: Cannot read properties of null (reading \'classList\')', src: 'front/lobby_net.js:412', n: 20, visitors: 14, first: '2026-12-13', last: NOW / 1000 - 7200 },
                 { kind: 'Page', msg: 'Error: <img src=x onerror=alert(1)>', src: '', n: 3, visitors: 2, first: '2026-11-20', last: NOW / 1000 - 90000 }],
        counter: { now: NOW / 1000, since: 1, first_day: '2026-09-01', last_at: NOW / 1000 - 60, accepted_today: 1184, refused_today: 3, crawlers_today: 7, raw_days: 30, oldest_raw: '2026-11-14', geo: { name: 'DB-IP Lite', month: '2026-12' } },
        untrusted: true, untrusted_fields: ['lists.refs', 'errors']
    }, o.over || {});
}

function page(o) {
    o = Object.assign({ answers: [], hubToken: { csrf: 'hub-token-1' } }, o || {});
    const doc = makeDocument();
    doc.createElementNS = (ns, tag) => doc.createElement(tag);
    const app = doc.createElement('div');
    doc.body.appendChild(app);
    const calls = [], downloads = [], toasts = [];
    let cleanup = null, refusedCalls = 0;
    const ctx = { doc, app, calls, downloads, toasts, answers: o.answers, report: null };
    const el = (tag, cls, text) => { const n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined && text !== null) n.textContent = text; return n; };
    const h = {
        doc, el, add: function (parent) { for (let i = 1; i < arguments.length; i++) if (arguments[i]) parent.appendChild(arguments[i]); return parent; },
        button: (label, cls, onclick) => { const b = el('button', 'btn ' + cls, label); if (onclick) b.addEventListener('click', onclick); return b; },
        panel: (title, hint) => { const p = el('section', 'panel'); p.appendChild(el('h2', null, title)); if (hint) p.appendChild(el('p', 'ph', hint)); return p; },
        message: (text, cls) => el('div', 'msg ' + (cls || 'bad'), text),
        typed: (tag, cls, text) => { const n = el(tag, cls, text); n.setAttribute('data-untrusted', 'typed-text'); return n; },
        hubToken: () => Promise.resolve(o.hubToken),
        call: (method, url, body) => {
            calls.push({ method, url, body });
            const answer = o.answers.length ? o.answers.shift() : { status: 200, json: ctx.report || report() };
            return typeof answer === 'function' ? answer() : Promise.resolve(answer);
        },
        refused: (r) => { if (r.status === 401 || r.status === 403) { refusedCalls++; return true; } return false; },
        toast: (t) => toasts.push(t),
        setCleanup: (fn) => { cleanup = fn; },
        env: { now: () => NOW, download: (name, mime, content) => downloads.push({ name, mime, content }) }
    };
    ctx.refused = () => refusedCalls;
    ctx.leave = () => { if (cleanup) cleanup(); cleanup = null; };
    ctx.open = async () => { const s = A.make(h); const p = s.render(app); await settle(); return p; };
    return ctx;
}
const all = (app, sel) => app.querySelectorAll(sel);
const texts = (app, sel) => all(app, sel).map((n) => n.textContent);
const press = async (app, label, nth) => { const b = all(app, 'button').filter((x) => x.textContent === label)[nth || 0]; if (!b) throw new Error('no button ' + label); b.click(); await settle(); return b; };
const view = async (p, name) => { await press(p.app, name); };

(async function main() {
    // ---- the question ----
    {
        const p = page();
        await p.open();
        same('it asks once for the last 30 days, with a POST', p.calls, [{ method: 'POST', url: '/hub/admin/api/analytics', body: { from: '2026-11-15', to: TODAY } }]);
        same('the three views and the range bar', [texts(p.app, '.anav .chip'), texts(p.app, '.arange .rlabel'), texts(p.app, '.arange .chip'), texts(p.app, '.rdates'), texts(p.app, '.rsub')],
            [['Overview', 'Visitors', 'Health'], ['Range'], ['7 days', '30 days', '90 days', 'Custom'], ['15 Nov – 14 Dec 2026'], ['30 days, last day is today so far']]);
        same('the 30 days is the chosen one', texts(p.app, '.arange .chip.on'), ['30 days']);
        for (const [label, from] of [['7 days', '2026-12-08'], ['90 days', '2026-09-16'], ['30 days', '2026-11-15']]) {
            await press(p.app, label);
            same('the ' + label + ' ask again for their own days', p.calls[p.calls.length - 1].body, { from, to: TODAY });
        }
        check('a first view needs no token call of its own (the page has the hub token)', p.calls.every((c) => c.method === 'POST'));
    }
    // ---- a token that cannot be had ----
    {
        const p = page({ hubToken: null });
        await p.open();
        same('without the hub\'s token nothing is asked and the page says so', [p.calls.length, texts(p.app, '.abody .msg')], [0, ['The numbers could not be loaded.']]);
    }
    // ---- trouble ----
    {
        const p = page({ answers: [{ status: 503, json: { error: 'analytics_off' } }] });
        await p.open();
        same('a hub with the counter off says the setting', texts(p.app, '.abody .msg'), ['The counter is switched off on this hub (ANTS_HUB_ANALYTICS is off).']);
        await press(p.app, 'Try again');
        same('Try again asks again and draws the numbers', [p.calls.length, texts(p.app, '.tile .tl')], [2, ['Visitors', 'Visits', 'Sign-ups', 'Errors']]);
        const q = page({ answers: [{ status: 400, json: { error: 'range' } }, { status: 500, json: {} }, () => Promise.reject(new Error('network'))] });
        await q.open();
        same('a range that the hub refuses', texts(q.app, '.abody .msg'), ['That range is not one the counter can show.']);
        await press(q.app, 'Try again');
        same('any other answer', texts(q.app, '.abody .msg'), ['The numbers could not be loaded.']);
        await press(q.app, 'Try again');
        same('no answer at all', texts(q.app, '.abody .msg'), ['The server did not answer.']);
        const r = page({ answers: [{ status: 401, json: { error: 'login' } }] });
        await r.open();
        same('a login that ended is the page\'s business: nothing is drawn', [r.refused(), all(r.app, '.tile').length, all(r.app, '.msg').length], [1, 0, 0]);
    }
    // ---- a late answer of an earlier question is not drawn ----
    {
        let release;
        const p = page({ answers: [() => new Promise((resolve) => { release = resolve; })] });
        const opened = p.open();
        await settle();
        await press(p.app, '7 days');
        release({ status: 200, json: report({ over: { days: [{ day: TODAY, visitors: 9999, visits: 0, nt: 0, signups: 0, errors: 0, perf: {}, vers: {} }] } }) });
        await opened;
        await settle();
        check('the answer to the first question is dropped', texts(p.app, '.tile .tb')[0] !== '9,999');
    }
    // ---- the custom range ----
    {
        const p = page();
        await p.open();
        await press(p.app, 'Custom');
        const inputs = all(p.app, 'input');
        same('the custom form: two dates that start at the shown range, up to 400 days back', [inputs.length, inputs.map((i) => i.type), inputs.map((i) => i.value), inputs[0].getAttribute('min'), inputs[0].getAttribute('max')], [2, ['date', 'date'], ['2026-11-15', '2026-12-14'], '2025-11-10', TODAY]);
        check('the note under it', texts(p.app, '.rnote')[0].indexOf('Up to 400 days back') === 0);
        inputs[0].value = '2026-12-14'; inputs[1].value = '2026-12-01';
        await press(p.app, 'Show these days');
        same('a range the wrong way round says so and asks nothing', [texts(p.app, '.rcustom .msg'), p.calls.length], [['The first day must not be after the last one.'], 1]);
        all(p.app, 'input')[0].value = '2026-11-28'; all(p.app, 'input')[1].value = '2026-12-14';
        await press(p.app, 'Show these days');
        same('two good days are asked for, and the Custom chip is the chosen one', [p.calls[1].body, texts(p.app, '.arange .chip.on'), texts(p.app, '.rdates')], [{ from: '2026-11-28', to: TODAY }, ['Custom'], ['28 Nov – 14 Dec 2026']]);
        await press(p.app, '7 days');
        same('a preset closes the form', [all(p.app, 'input').length, texts(p.app, '.arange .chip.on')], [0, ['7 days']]);
    }
    // ---- Overview ----
    {
        const p = page();
        await p.open();
        same('the four tiles', [texts(p.app, '.tile .tl'), texts(p.app, '.tile .tb')], [['Visitors', 'Visits', 'Sign-ups', 'Errors'], [A.thousands(report().days.reduce((a, d) => a + d.visitors, 0)), A.thousands(report().days.reduce((a, d) => a + d.visits + d.nt, 0)), '18', '31']]);
        const sums = report().days.reduce((a, d) => ({ v: a.v + d.visitors, n: a.n + d.nt, e: a.e + d.errors }), { v: 0, n: 0, e: 0 });
        same('the changes against the days before (more errors are the gold one)', texts(p.app, '.tile .td'), [A.change(sums.v, 1930, 30).text, A.change(report().days.reduce((a, d) => a + d.visits + d.nt, 0), 2800, 30).text, A.change(18, 40, 30).text, A.change(31, 85, 30).text]);
        same('a rise is good and a fall of the errors is good too', all(p.app, '.tile .td').map((n) => n.className.split(' ').pop()), ['good', all(p.app, '.tile .td')[1].className.split(' ').pop(), 'warn', 'good']);
        same('the small lines', texts(p.app, '.tile .tt').map((t) => t.replace(/[0-9][0-9,.]*/g, 'N')), ['about N a day', 'N not tracked (N%)', 'N% of visitors', 'N kinds, N new in N days']);
        check('the note says that a day stands alone and how many visits come from browsers that asked not to be tracked', texts(p.app, '.anote')[0].indexOf('a person who comes on 3 days is 3 visitors') > 0 && /Visits include the [0-9,]+ from browsers that asked not to be tracked/.test(texts(p.app, '.anote')[0]));
        const svg = all(p.app, 'svg')[0];
        same('the chart: a bar for every day, a hatched cap for the days with visits that sent only a country, a shade for each weekend day, a hit area for every day',
            [all(svg, 'rect').filter((r) => r.getAttribute('class') === 'bar').length, all(svg, 'rect').filter((r) => r.getAttribute('class') === 'barnt').length, all(svg, 'rect').filter((r) => r.getAttribute('class') === 'weekend').length, all(svg, 'rect').filter((r) => r.getAttribute('class') === 'hit').length],
            [30, 30, report().days.filter((d) => A.isWeekend(d.day)).length, 30]);
        check('the hatched part is on top of the visitors', (() => { const bars = all(svg, 'rect').filter((r) => r.getAttribute('class') === 'bar')[0], cap = all(svg, 'rect').filter((r) => r.getAttribute('class') === 'barnt')[0]; return Math.abs(+cap.getAttribute('y') + +cap.getAttribute('height') - +bars.getAttribute('y')) < 0.01; })());
        check('the tallest bar is the biggest day (13th)', (() => { const hs = all(svg, 'rect').filter((r) => r.getAttribute('class') === 'bar').map((r) => +r.getAttribute('height')); return hs.indexOf(Math.max.apply(null, hs)) === 13; })());
        check('the hatch is a pattern', all(svg, 'pattern').length === 1 && all(svg, 'rect').filter((r) => r.getAttribute('class') === 'barnt').every((r) => r.getAttribute('fill') === 'url(#hatch)'));
        same('the labels of the days', texts(svg, 'text').filter((t) => /[A-Z][a-z]{2}$/.test(t)), ['15 Nov', '25 Nov', '4 Dec', '14 Dec']);
        const hit = all(svg, 'rect').filter((r) => r.getAttribute('class') === 'hit')[13];
        hit.fire('mouseenter', {});
        const tip = all(p.app, '.tip')[0];
        same('the card of a bar: its day and its three numbers', [tip.hidden, texts(tip, 'b')[0], texts(tip, '.tk')], [false, A.longDay(report().days[13].day), ['Visitors' + report().days[13].visitors, 'Not tracked' + report().days[13].nt + ' visits', 'Visits in all' + (report().days[13].visits + report().days[13].nt)]]);
        all(p.app, '.chartbox')[0].fire('mouseleave', {});
        check('and it goes when the pointer leaves', all(p.app, '.tip')[0].hidden === true);
        await press(p.app, 'Show as a table');
        same('as a table: newest day first, the last day marked as so far', [texts(p.app, '.atable th'), all(p.app, '.atable tbody tr').slice(0, 2).map((r) => r.children[0].textContent), all(p.app, '.atable tbody tr').length, all(p.app, 'svg').length], [['Day', 'Visitors', 'Not tracked', 'Visits'], ['Mon 14 Dec (so far)', 'Sun 13 Dec'], 30, 0]);
        await press(p.app, 'Show the chart');
        check('and back to the chart', all(p.app, 'svg').length === 1 && all(p.app, '.atable').length === 0);
    }
    // ---- the first day ----
    {
        const empty = report({ n: 1, over: { days: [{ day: TODAY, visitors: 0, visits: 0, nt: 0, signups: 0, errors: 0, perf: {}, vers: {} }], before: { visitors: 0, visits: 0, nt: 0, signups: 0, errors: 0 }, errors: [],
                                                counter: { now: NOW / 1000, since: NOW / 1000 - 7000, first_day: null, last_at: null, accepted_today: 0, refused_today: 0, crawlers_today: 0, raw_days: 30, oldest_raw: null, geo: null },
                                                lists: { visitors: 0, 'new': 0, returning: 0, visits: 0, nt: 0, signups: 0, dur_sum: 0, dur_mid: 0, errors_other: 0, refs: {}, entry: {}, exit: {}, browsers: {}, systems: {}, devices: {}, langs: {}, countries: {}, widths: [0, 0, 0, 0, 0], dur: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] } } });
        const p = page();
        p.report = empty;
        p.report = empty;
        await p.open();
        const q = p;
        same('before the first event: zero tiles that say nothing is counted, and one card that says so', [texts(q.app, '.tile .tb'), texts(q.app, '.tile .tt')[0], texts(q.app, '.aempty h2'), texts(q.app, '.achecks li'), all(q.app, 'svg').length], [['0', '0', '0', '0'], 'nothing counted yet', ['Nothing counted yet'], ['The counter is on, since today at 09:23', 'No event received yet'], 0]);
        check('no change is told against nothing', texts(q.app, '.tile .td').every((t) => t === ''));
        await view(q, 'Visitors');
        same('the other views say it too', texts(q.app, '.abody .aload'), ['Nothing counted yet.']);
        await view(q, 'Health');
        same('Health still shows the counter itself', [texts(q.app, '.kv .k')[0], texts(q.app, '.kv .v')[0], texts(q.app, '.kv .v').slice(-1)[0]], ['Last event received', 'none yet', 'none every country is unknown']);
    }
    // ---- Visitors ----
    {
        const p = page();
        await p.open();
        await view(p, 'Visitors');
        const panels = texts(p.app, '.panel h2').map((t) => t.replace(/\s+/g, ' '));
        same('the panels of the left column and the right column', [panels.slice(0, 5), panels.slice(5)], [['Countries', 'Browsers', 'Systems', 'Devices and screen size', 'Languages'], ['New and returning', 'Where visitors came from', 'Entry pages', 'Exit pages', 'Time on the site']]);
        check('the note on not-tracked visits (the share of all visits)', /^Not-tracked visits \(8\.5%\) send a country and nothing else\./.test(texts(p.app, '.anote')[0]), texts(p.app, '.anote')[0]);
        const countries = all(p.app, '.agrid .panel')[0];
        const rows = all(countries, '.brow');
        same('the countries: the ten biggest by name, the rest in one row, unknown last', [rows.length, texts(rows[0], '.bl'), texts(rows[0], '.bn'), texts(rows[0], '.bp')], [12, ['United States'], ['1,370'], ['44%']]);
        same('names of countries in order', rows.slice(0, 10).map((r) => texts(r, '.bl')[0]), ['United States', 'Germany', 'United Kingdom', 'Canada', 'France', 'Netherlands', 'Brazil', 'Australia', 'Poland', 'Sweden']);
        same('the last rows: the others with their number, then the unknown', [texts(rows[10], '.bl'), texts(rows[10], '.bn'), texts(rows[11], '.bl'), texts(rows[11], '.bn'), rows[10].className.indexOf('dim') > 0 && rows[11].className.indexOf('dim') > 0], [['Other (3 countries)'], ['30'], ['Unknown'], ['77'], true]);
        check('a hatched part in every row that has not tracked visits, and none in the row that has none', all(rows[0], '.bhat').length === 1 && parseFloat(all(rows[0], '.bhat')[0].style.width) > 0 && all(rows[11], '.bhat').length === 0);
        check('the biggest row is the full width', all(rows[0], '.bfill')[0].style.width === '92.7%' || parseFloat(all(rows[0], '.bfill')[0].style.width) + parseFloat(all(rows[0], '.bhat')[0].style.width) > 99.9, all(rows[0], '.bfill')[0].style.width + ' ' + all(rows[0], '.bhat')[0].style.width);
        same('new and returning: both numbers with their share', [texts(p.app, '.nrv'), all(p.app, '.nrnew')[0].style.width, all(p.app, '.nrret')[0].style.width], [['1,648 73%', '609 27%'], '73%', '27%']);
        const refs = all(p.app, '.agrid .panel')[6];
        same('where visitors came from: direct first by size, then the sites, the others last', texts(refs, '.brow .bl'), ['(direct)', 'discord.com', 'google.com', 'Other sites']);
        check('a site that sent a visitor is text from a browser: marked as data', all(refs, '.brow .bl').filter((n) => n.getAttribute('data-untrusted') === 'typed-text').length === 3);
        same('the entry pages: /map is /map/…, other is Other pages', texts(all(p.app, '.agrid .panel')[7], '.brow .bl'), ['/', '/matches', '/map/…', 'Other pages']);
        same('the exit pages', texts(all(p.app, '.agrid .panel')[8], '.brow .bl'), ['/', '/play']);
        const time = all(p.app, '.agrid .panel')[9];
        same('the time on the site: six columns, the gold one holds the middle', [all(time, '.hcol').length, texts(time, '.hl'), all(time, '.hcol.mid').length, texts(time, '.hcol.mid .hl')], [6, A.DUR_GROUPS.map((g) => g[0]), 1, ['1 to 3 min']]);
        check('the middle and the average in words', /^Middle: 1 min 19 s\s+Average: \d+ min \d+ s$/.test(texts(time, '.hsum')[0]), texts(time, '.hsum')[0]);
        const devices = all(p.app, '.agrid .panel')[3];
        same('devices, then the width of the screen in the order of the sizes', [texts(devices, '.brow .bl').slice(0, 3), texts(devices, 'h3'), texts(devices, '.brow .bl').slice(3)], [['Desktop', 'Phone', 'Tablet'], ['Width of the screen'], A.WIDTHS]);
        same('languages by name', texts(all(p.app, '.agrid .panel')[4], '.brow .bl'), ['English', 'German', 'French']);
        same('browsers and systems count visitors', [texts(all(p.app, '.agrid .panel')[1], '.brow .bl'), texts(all(p.app, '.agrid .panel')[2], '.brow .bl')], [['Chrome', 'Safari', 'Firefox'], ['Windows', 'macOS', 'iOS']]);
    }
    // ---- Health ----
    {
        const p = page();
        await p.open();
        await view(p, 'Health');
        const panels = texts(p.app, '.panel h2').map((t) => t.replace(/\s+/g, ' '));
        same('the panels', panels, ['Page load', 'Game download and start', 'Errors, grouped 48 errors, 3 kinds', 'The counter itself']);
        const charts = all(p.app, 'svg');
        same('two line charts, each with three lines (p50, p75, p95) and their names at the end', [charts.length, charts.map((c) => all(c, 'path').length), charts.map((c) => texts(c, 'text').filter((t) => /^p\d\d$/.test(t)))], [2, [3, 3], [['p50', 'p75', 'p95'], ['p50', 'p75', 'p95']]]);
        const gamePaths = all(charts[1], 'path').map((n) => n.getAttribute('d'));
        check('the game chart has a gap for the first three days (too few timings): its lines start with a second part or start late', gamePaths.every((d) => d.indexOf('M') === 0) && gamePaths[0].split('M').length === 2);
        check('a day with too few timings has no point: the game line has 27 points, the load line 30', all(charts[0], 'path')[0].getAttribute('d').split(/[ML]/).filter(Boolean).length === 30 && gamePaths[0].split(/[ML]/).filter(Boolean).length === 27);
        same('the new version\'s day is marked once in each chart', charts.map((c) => all(c, 'line').filter((l) => l.getAttribute('class') === 'newver').map((l) => l.getAttribute('data-day'))), [[report().days[12].day], [report().days[12].day]]);
        check('and the note says how many', /new version went live \(1 in this range\)/.test(texts(p.app, '.anote')[0]), texts(p.app, '.anote')[0]);
        same('the words above a chart', texts(p.app, '.hsum').slice(0, 2).map((t) => t.replace(/\s+/g, ' ')), ['Half under 0.9 s slowest 1 in 20: 3.4 s', 'Half under 4.2 s slowest 1 in 20: 15 s']);
        const rows = all(p.app, '.erow').slice(1);
        same('the errors: where, the message, times, visitors, first seen, last seen', [rows.length, rows.map((r) => texts(r, '.tg')[0]), rows.map((r) => texts(r, '.etext')[0]), rows.map((r) => texts(r, '.esrc')[0]), rows.map((r) => texts(r, '.nc').join('/'))],
            [3, ['Game', 'Page', 'Page'], ['RuntimeError: memory access out of bounds', 'TypeError: Cannot read properties of null (reading \'classList\')', 'Error: <img src=x onerror=alert(1)>'], ['index.js:1', 'front/lobby_net.js:412', 'file not named'], ['25/16', '20/14', '3/2']]);
        same('first and last seen in words', rows.map((r) => r.children.slice(4).map((c) => c.textContent)), [['14 Dec', 'today, 11:10'], ['13 Dec', 'today, 09:20'], ['20 Nov', 'yesterday']]);
        same('a new kind is tagged New (the one that came today and yesterday, not the old ones)', rows.map((r) => all(r, '.tg.warn').length), [1, 1, 0]);
        check('what came from a browser is text with the mark of data, and no element is made of it', rows.every((r) => all(r, '[data-untrusted]').length >= 1) && all(p.app, 'img').length === 0 && texts(p.app, '.etext').indexOf('Error: <img src=x onerror=alert(1)>') >= 0);
        same('the counter itself', all(p.app, '.kv').map((r) => [texts(r, '.k')[0], texts(r, '.v')[0]]), [['Last event received', 'today, 11:19'], ['Events today', '1,184 since midnight'], ['Events refused', '3 today too big or the wrong shape'], ['Crawlers left out', '7 today not counted'], ['Raw events kept', '30 days back to 14 Nov'], ['Daily totals kept', 'since 1 Sep 2026'], ['Country list', 'DB-IP Lite updated 2026-12']]);
        check('the foot says since when, the days and where the Legal page is', /^Counted since 1 Sep 2026 · Days run from midnight to midnight, Pacific time · Updated today, 11:20 · No cookies; no address or visitor id is kept \(see the Legal page\)\.$/.test(texts(p.app, '.afoot')[0]) && all(p.app, '.afoot a')[0].getAttribute('href') === '/legal', texts(p.app, '.afoot')[0]);
    }
    {
        const many = []; for (let i = 0; i < 60; i++) many.push({ kind: 'Page', msg: 'Error: number ' + i, src: 'a.js:' + i, n: 100 - i, visitors: 1, first: '2026-11-20', last: NOW / 1000 - 5000 });
        const p = page();
        p.report = report({ over: { errors: many } });
        await p.open();
        await view(p, 'Health');
        same('only the first 50 kinds with a button for the rest', [all(p.app, '.erow').length - 1, texts(p.app, '.efoot span')[0].indexOf('the first 50 of 60 kinds') > 0, texts(p.app, '.efoot button')], [50, true, ['Show all']]);
        await press(p.app, 'Show all');
        same('Show all shows them all', [all(p.app, '.erow').length - 1, texts(p.app, '.efoot button')], [60, []]);
        const q = page();
        q.report = report({ over: { errors: [], days: report().days.map((d) => Object.assign({}, d, { perf: {}, errors: 0 })) } });
        await q.open();
        await view(q, 'Health');
        same('no timing and no error', [texts(q.app, '.panel .none'), all(q.app, 'svg').length], [['Not enough timings yet (a day needs 20).', 'Not enough timings yet (a day needs 20).', 'No error in this range.'], 0]);
    }
    {
        const p = page();
        p.report = report({ over: { counter: Object.assign({}, report().counter, { dropped_today: 7 }) } });
        await p.open();
        await view(p, 'Health');
        same('events over the day\'s limit are told when there are some (and only then)', [all(p.app, '.kv').map((r) => texts(r, '.k')[0]).filter((k) => /limit/.test(k)), texts(all(p.app, '.kv')[4], '.v')], [['Over the day\u2019s limit'], ['7 today dropped: a flood?']]);
    }
    // ---- the export ----
    {
        const p = page();
        await p.open();
        await press(p.app, 'Export CSV ▾');
        same('the menu: five lists and the zip, then the note', [texts(p.app, '.amenu2 .mi b'), texts(p.app, '.amenu2 .mfoot')], [['Daily totals', 'Countries, browsers, systems, devices', 'Pages and referrers', 'Timings', 'Errors', 'Everything, as one .zip'], ['Numbers only: a file has no names, addresses or visitor ids, because none are kept.']]);
        same('and what it is for', texts(p.app, '.amenu2 .mhead small'), ['15 Nov – 14 Dec 2026, one row for each day or each line, Pacific days.']);
        await press(p.app, 'Export CSV \u25be');
        const names = [];
        for (const label of ['Daily totals', 'Countries, browsers, systems, devices', 'Pages and referrers', 'Timings', 'Errors']) {
            await press(p.app, 'Export CSV ▾');
            const item = all(p.app, '.amenu2 .mi').filter((b) => texts(b, 'b')[0] === label)[0];
            item.click();
            await settle();
            const d = p.downloads[p.downloads.length - 1];
            names.push(d.name);
            check(label + ': a CSV file with a header and one row for each line', d.mime === 'text/csv;charset=utf-8' && /\r\n$/.test(d.content) && d.content.split('\r\n').length > 2, d.content.slice(0, 80));
        }
        same('the file names say the range', names, ['daily_totals_2026-11-15_2026-12-14.csv', 'countries_browsers_systems_devices_2026-11-15_2026-12-14.csv', 'pages_and_referrers_2026-11-15_2026-12-14.csv', 'timings_2026-11-15_2026-12-14.csv', 'errors_2026-11-15_2026-12-14.csv']);
        const daily = p.downloads[0].content.split('\r\n');
        same('daily totals: the header and a row for the first and the last day (visits include the not tracked)', [daily[0], daily[1], daily.length - 2], ['day,visitors,visits,not_tracked_visits,sign_ups,errors', '2026-11-15,50,65,5,3,1', 30]);
        check('the country file has the not tracked column, the unknown country and every list of the page', /country,United States|country,US,1270,100/.test(p.downloads[1].content) && /country,unknown,77,0/.test(p.downloads[1].content) && /screen_width,Under 400 px,316,0/.test(p.downloads[1].content) && /new_or_returning,returning,609,0/.test(p.downloads[1].content) && /language,en,1602,0/.test(p.downloads[1].content));
        check('the pages file has the referrer, with direct as a word', /referrer_site,direct,1388/.test(p.downloads[2].content) && /time_on_site,1 to 3 min,/.test(p.downloads[2].content) && /entry_page,\/,1643/.test(p.downloads[2].content));
        check('the timings file has a row for each day and kind', p.downloads[3].content.split('\r\n').length === 1 + 60 + 1 && /2026-12-14,load,89,929,1500,3400/.test(p.downloads[3].content));
        check('the errors file: where, message, file, times, visitors, first and last day; a message that starts like a formula is guarded', /Game,RuntimeError: memory access out of bounds,index\.js:1,25,16,2026-12-14,2026-12-14/.test(p.downloads[4].content) && p.downloads[4].content.split('\r\n').length === 5);
        await press(p.app, 'Export CSV ▾');
        all(p.app, '.amenu2 .mi').filter((b) => texts(b, 'b')[0] === 'Everything, as one .zip')[0].click();
        await settle();
        const z = p.downloads[p.downloads.length - 1], files = readZip(z.content);
        same('the zip: one file of each list, each whole, with its checksum', [z.name, z.mime, files && files.map((f) => f.name), files && files.every((f) => f.crcOk), files && files.map((f) => f.text)], ['analytics_2026-11-15_2026-12-14.zip', 'application/zip',
            ['daily_totals.csv', 'countries_browsers_systems_devices.csv', 'pages_and_referrers.csv', 'timings.csv', 'errors.csv'], true, p.downloads.slice(0, 5).map((d) => d.content)]);
        same('a menu that was used is closed', all(p.app, '.amenu2').length, 0);
        await press(p.app, 'Export CSV ▾');
        p.doc.key('Escape');
        await settle();
        same('Escape closes the menu', [all(p.app, '.amenu2').length, (p.doc.listeners.keydown || []).length, (p.doc.listeners.click || []).length], [0, 0, 0]);
        await press(p.app, 'Export CSV ▾');
        p.doc.listeners.click.forEach((f) => f({ target: p.app }));
        await settle();
        same('a press outside it closes it too', all(p.app, '.amenu2').length, 0);
        await press(p.app, 'Export CSV ▾');
        p.leave();
        same('another section takes the page: the menu\'s listeners go', [(p.doc.listeners.keydown || []).length, (p.doc.listeners.click || []).length], [0, 0]);
        const failed = page({ answers: [{ status: 500, json: {} }] });
        await failed.open();
        check('the export button is not there before the numbers are', all(failed.app, '.amw').length === 0);
    }
    // ---- no markup is ever made from a text ----
    {
        const p = page();
        p.report = report({ over: { lists: Object.assign({}, report().lists, { refs: { '<b>x</b>.example': 5 }, entry: { '<i>': 1 } }) } });
        await p.open();
        await view(p, 'Visitors');
        check('a site named like markup is text', all(p.app, 'b').length === 0 && all(p.app, 'x').length === 0 && texts(p.app, '.brow .bl').indexOf('<b>x</b>.example') >= 0 && texts(p.app, '.brow .bl').indexOf('<i>') >= 0);
    }
    console.log(failures ? failures + ' of ' + checks + ' checks failed' : 'all ' + checks + ' checks hold');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check itself: ' + (e && e.stack || e)); process.exit(1); });

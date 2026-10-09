// Runs the front page's OWN code for the line of numbers in its footer (the owner: "Game stats would be cool on the page. How many games played / in progress etc."): the block STATS_BEGIN ..
// STATS_END of web/lobby.html, on tables and with a fake clock, a fake page visibility and fake answers of the site:
//   * statsOf: the answer of /stats ({"now":{"matches","players"},"online":{"day","total"},"local":{"day","total"},"since"}): the live numbers and the sums (games played = online.total + local.total, today =
//     online.day + local.day), and every other shape (not an object, a missing part, a negative or fractional or non-numeric number, a text that is no date as the only thing wrong) as null or as it is allowed;
//   * busyOf: the answer of /busy ({"matches","players"}): the live part only;
//   * the words of the line: the plurals (1 match, 1 player, 1 game), the thousands separators, the green dot only while a match is being played, the title, the live part alone;
//   * the poller: /stats first and nothing else when it answers; /busy when it does not answer, answers with another shape or fails; no line (null) when neither does; a look every 30 s while the page is in
//     front; nothing asked while it is hidden, a look at once when it is shown, an answer that comes late (hidden, or after a newer look) dropped; the line comes back when the site does.
// tests/scripts/test_web_stats.py runs this with node (the quick tier). usage: node web_stats_check.js web/lobby.html     (exit 0: every check holds; failures are printed)
'use strict';
const fs = require('fs');

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_stats_check.js lobby.html'); process.exit(2); }

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
function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

const lobbyText = fs.readFileSync(lobbyPath, 'utf8');
const block = between(lobbyText, 'STATS_BEGIN', 'STATS_END', lobbyPath);
const S = new Function(block + '\nreturn { STATS_EVERY: STATS_EVERY, statsOf: statsOf, busyOf: busyOf, groupDigits: groupDigits, statsWords: statsWords, makeStatsPoller: makeStatsPoller };')();

(async () => {
    const GOOD = { now: { matches: 3, players: 7 }, online: { day: 5, total: 900 }, local: { day: 16, total: 384 }, since: '2026-10-04' };
    const clone = (patch) => { const o = JSON.parse(JSON.stringify(GOOD)); patch(o); return o; };

    // ---- /stats
    same('statsOf: the live numbers, the sums of online and local, the date', S.statsOf(GOOD), { matches: 3, players: 7, played: 1284, today: 21, since: '2026-10-04' });
    same('statsOf: zeros are numbers', S.statsOf({ now: { matches: 0, players: 0 }, online: { day: 0, total: 0 }, local: { day: 0, total: 0 }, since: '2026-10-04' }), { matches: 0, players: 0, played: 0, today: 0, since: '2026-10-04' });
    same('statsOf: more fields than the shape does not matter', S.statsOf(clone((o) => { o.extra = 1; o.now.rooms = 9; o.online.week = 4; })), { matches: 3, players: 7, played: 1284, today: 21, since: '2026-10-04' });
    same('statsOf: the date may be missing or no date: the numbers are still good, only the title goes', [S.statsOf(clone((o) => { delete o.since; })).since, S.statsOf(clone((o) => { o.since = 'last week'; })).since, S.statsOf(clone((o) => { o.since = 5; })).since, S.statsOf(clone((o) => { o.since = '2026-10-04T08:00:00Z'; })).since], ['', '', '', '2026-10-04']);
    const BAD = [null, undefined, 0, 1, 'x', '', '{}', true, [], [GOOD], () => GOOD, {}, { now: GOOD.now }, { now: GOOD.now, online: GOOD.online }, { online: GOOD.online, local: GOOD.local },
                 { now: GOOD.now, online: GOOD.online, local: 5 }, { now: 'x', online: GOOD.online, local: GOOD.local }, { now: null, online: GOOD.online, local: GOOD.local }];
    for (const bad of BAD) check('statsOf: ' + JSON.stringify(bad) + ' is no /stats answer', S.statsOf(bad) === null);
    for (const [where, field] of [['now', 'matches'], ['now', 'players'], ['online', 'day'], ['online', 'total'], ['local', 'day'], ['local', 'total']]) {
        check('statsOf: ' + where + '.' + field + ' missing is no /stats answer', S.statsOf(clone((o) => { delete o[where][field]; })) === null);
        for (const bad of [-1, 1.5, '3', null, NaN, Infinity, -Infinity, true, [3], {}, 1e13]) check('statsOf: ' + where + '.' + field + ' = ' + String(JSON.stringify(bad)) + ' is no /stats answer', S.statsOf(clone((o) => { o[where][field] = bad; })) === null);
    }
    check('statsOf: an answer that is the HTML of the game page (the site has no /stats yet) is not one', S.statsOf('<!DOCTYPE html><html>...') === null);

    // ---- /busy
    same('busyOf: the live part only', S.busyOf({ matches: 2, players: 5 }), { matches: 2, players: 5, played: null, today: null, since: '' });
    same('busyOf: zeros', S.busyOf({ matches: 0, players: 0 }), { matches: 0, players: 0, played: null, today: null, since: '' });
    for (const bad of [null, undefined, 'x', 5, [], {}, { matches: 1 }, { players: 1 }, { matches: -1, players: 0 }, { matches: 1.5, players: 0 }, { matches: '1', players: '0' }, { matches: 1, players: NaN }, [{ matches: 1, players: 1 }]]) check('busyOf: ' + JSON.stringify(bad) + ' is no /busy answer', S.busyOf(bad) === null);

    // ---- the words
    same('digits: thousands separators', [0, 7, 21, 999, 1000, 1284, 12345, 123456, 1234567, 999999999999].map(S.groupDigits), ['0', '7', '21', '999', '1,000', '1,284', '12,345', '123,456', '1,234,567', '999,999,999,999']);
    const words = (m, p, t, d) => S.statsWords({ matches: m, players: p, played: t, today: d, since: '2026-10-04' });
    same('words: the line of the owner\'s example', words(3, 7, 1284, 21), { on: true, live: '3 matches being played · 7 players online', played: '1,284 games played (21 today)', title: 'Counted since 2026-10-04. Today means the last 24 hours.' });
    same('words: singular where the number is 1: match, player, game', [words(1, 1, 1, 1).live, words(1, 1, 1, 1).played], ['1 match being played · 1 player online', '1 game played (1 today)']);
    same('words: plural at 0 and at 2', [words(0, 0, 0, 0).live, words(0, 0, 0, 0).played, words(2, 2, 2, 2).live, words(2, 2, 2, 2).played], ['0 matches being played · 0 players online', '0 games played (0 today)', '2 matches being played · 2 players online', '2 games played (2 today)']);
    same('words: separators in every number', words(1000, 12345, 1234567, 1000).live + ' | ' + words(1000, 12345, 1234567, 1000).played, '1,000 matches being played · 12,345 players online | 1,234,567 games played (1,000 today)');
    same('words: the dot is on while a match is played and off at none', [words(1, 0, 0, 0).on, words(0, 9, 9, 9).on, words(3, 7, 1, 1).on], [true, false, true]);
    same('words: the live part alone (what /busy gives): no played part, no title', S.statsWords(S.busyOf({ matches: 4, players: 1 })), { on: true, live: '4 matches being played · 1 player online', played: '', title: '' });
    check('words: nothing of the answer is markup (they are plain strings of digits, letters and signs)', /^[0-9A-Za-z ,()·.:-]*$/.test(words(3, 7, 1284, 21).live + words(3, 7, 1284, 21).played + words(3, 7, 1284, 21).title));

    // ---- the poller
    const settle = async () => { for (let i = 0; i < 40; i++) await Promise.resolve(); };
    const ok = (v) => () => Promise.resolve(v);
    const fails = () => Promise.reject(new Error('the site does not answer'));
    function fakePage(answers) {
        const io = {
            calls: [], shown: [], timers: [], cancelled: 0, hiddenNow: false, answers,
            get(url) { io.calls.push(url); return io.answers[url](); },
            show(stats) { io.shown.push(stats); },
            later(fn, ms) { const h = { fn, ms, live: true }; io.timers.push(h); return h; },
            cancel(h) { h.live = false; io.cancelled++; },
            hidden() { return io.hiddenNow; },
            pending() { return io.timers.filter((t) => t.live); },
        };
        return io;
    }
    same('the look is every 30 seconds', S.STATS_EVERY, 30000);
    {   // /stats answers: one request, the full line, the next look 30 s later
        const io = fakePage({ '/stats': ok(GOOD), '/busy': fails });
        const poller = S.makeStatsPoller(io);
        poller.start();
        await settle();
        same('/stats answers: only /stats is asked, the line has the whole of it', [io.calls, io.shown], [['/stats'], [{ matches: 3, players: 7, played: 1284, today: 21, since: '2026-10-04' }]]);
        same('... and the next look is 30 s away (one timer)', io.pending().map((t) => t.ms), [30000]);
        io.pending()[0].fn();
        await settle();
        same('... when it comes the site is asked again, /stats first', [io.calls, io.shown.length, io.pending().length], [['/stats', '/stats'], 2, 1]);
    }
    {   // /stats is something else (the site has no such route and answers with its page: the page's JSON fails), /busy answers: the live part
        const io = fakePage({ '/stats': fails, '/busy': ok({ matches: 2, players: 4 }) });
        S.makeStatsPoller(io).start();
        await settle();
        same('/stats fails, /busy answers: the live part only', [io.calls, io.shown], [['/stats', '/busy'], [{ matches: 2, players: 4, played: null, today: null, since: '' }]]);
        io.pending()[0].fn();
        await settle();
        same('... the next look begins with /stats again (the fall back is not for good)', io.calls, ['/stats', '/busy', '/stats', '/busy']);
    }
    for (const [label, stats] of [['a /stats answer of another shape', { now: { matches: 1 } }], ['a /stats answer that is a text', '<html>the game page</html>'], ['a /stats answer that is null', null]]) {
        const io = fakePage({ '/stats': ok(stats), '/busy': ok({ matches: 0, players: 0 }) });
        S.makeStatsPoller(io).start();
        await settle();
        same(label + ': /busy is asked and gives the live part', [io.calls, io.shown], [['/stats', '/busy'], [{ matches: 0, players: 0, played: null, today: null, since: '' }]]);
    }
    {   // neither answers: no line at all, and the look goes on
        const io = fakePage({ '/stats': fails, '/busy': fails });
        S.makeStatsPoller(io).start();
        await settle();
        same('neither answers: the line is hidden (null) and the next look is planned', [io.calls, io.shown, io.pending().length], [['/stats', '/busy'], [null], 1]);
        io.answers['/stats'] = ok(GOOD);
        io.pending()[0].fn();
        await settle();
        same('... the line comes back when the site does', [io.shown.length, io.shown[1] && io.shown[1].played], [2, 1284]);
    }
    {   // both answer with something that is no good
        const io = fakePage({ '/stats': ok({}), '/busy': ok({ matches: 'many' }) });
        S.makeStatsPoller(io).start();
        await settle();
        same('both answer with a wrong shape: no line', io.shown, [null]);
    }
    {   // a request that throws on the spot is a failed look, not an exception
        const io = fakePage({ '/stats': () => { throw new Error('no fetch'); }, '/busy': () => { throw new Error('no fetch'); } });
        S.makeStatsPoller(io).start();
        await settle();
        same('get() that throws at once: no line, no exception', io.shown, [null]);
    }
    {   // a hidden page asks nothing; shown, it asks at once; hidden again it stops
        const io = fakePage({ '/stats': ok(GOOD), '/busy': fails });
        io.hiddenNow = true;
        const poller = S.makeStatsPoller(io);
        poller.start();
        await settle();
        same('a page that starts hidden asks nothing and plans nothing', [io.calls, io.shown.length, io.pending().length], [[], 0, 0]);
        io.hiddenNow = false;
        poller.visibility();
        await settle();
        same('... shown, it looks at once and plans the next look', [io.calls, io.shown.length, io.pending().length], [['/stats'], 1, 1]);
        io.hiddenNow = true;
        poller.visibility();
        same('... hidden again, the planned look is cancelled', io.pending().length, 0);
        await settle();
        same('... and nothing more is asked', io.calls, ['/stats']);
    }
    {   // an answer that comes while the page is hidden is dropped
        let release;
        const io = fakePage({ '/stats': () => new Promise((resolve) => { release = resolve; }), '/busy': fails });
        const poller = S.makeStatsPoller(io);
        poller.start();
        io.hiddenNow = true;
        poller.visibility();
        release(GOOD);
        await settle();
        same('an answer that arrives after the page was hidden is not shown, and nothing is planned', [io.shown.length, io.pending().length], [0, 0]);
    }
    {   // two looks in flight (hidden and shown again before the first answer): the older answer is dropped
        const releases = [];
        const io = fakePage({ '/stats': () => new Promise((resolve) => { releases.push(resolve); }), '/busy': fails });
        const poller = S.makeStatsPoller(io);
        poller.start();
        io.hiddenNow = true;
        poller.visibility();
        io.hiddenNow = false;
        poller.visibility();
        releases[0](clone((o) => { o.now.matches = 1; }));
        await settle();
        same('the older look\'s answer is dropped', io.shown.length, 0);
        releases[1](clone((o) => { o.now.matches = 9; }));
        await settle();
        same('... the newer look\'s answer is shown, and one look is planned', [io.shown.map((s) => s.matches), io.pending().length], [[9], 1]);
    }
    {   // visibility while already shown and nothing is wrong: it just looks again (the timer is replaced, not doubled)
        const io = fakePage({ '/stats': ok(GOOD), '/busy': fails });
        const poller = S.makeStatsPoller(io);
        poller.start();
        await settle();
        poller.visibility();
        await settle();
        same('a second look (the page was shown again): still one timer', [io.calls.length, io.pending().length], [2, 1]);
    }
    check('the block makes no markup and reads no storage (it is numbers in, words out)', !/innerHTML|insertAdjacentHTML|document\.|localStorage/.test(block));
    check('the poller asks /stats first and /busy second, addresses of the site itself (no host in them)', /resolve\(io\.get\('\/stats'\)\)/.test(block) && /resolve\(io\.get\('\/busy'\)\)/.test(block) && block.indexOf("'/stats'") < block.indexOf("'/busy'") && !/https?:|\/\//.test(block.replace(/\/\/ .*$/gm, '').replace(/\/\^[^\n]*\//g, '')));
})().then(() => {
    console.log('web stats check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
}, (e) => {
    console.log('FAIL ' + e.message + '\n' + e.stack);
    process.exit(1);
});

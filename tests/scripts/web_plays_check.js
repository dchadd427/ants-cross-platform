// Runs the OWN code of the play counter (web/replay_plays.js) on a page of fakes (a clock, a window, a storage, a fetch) and the page's own call of it (the block of web/shell.html between PLAYS_BEGIN and PLAYS_END):
// 10 seconds of playing is one play and 9.9 are not; a pause, a jump, the end, a game that is not ready, a hidden tab and a computer that slept add nothing; once a day for a recording in this browser (two page views
// share the memory, a day later it counts again, another recording counts, the memory is written BEFORE the play is sent, at most 200 recordings and none a day old are kept); a browser that refuses its storage
// counts once for each page view and never throws; what is sent (POST /counts/play/<id>, no body, no cookies), a play that does not arrive is not sent again, and nothing is sent for a match that is being
// played, for a file of your own or for an address that is no recording. tests/scripts/test_web_plays.py runs it with node (the quick tier) and again on changed copies, which it must refuse.
// usage: node web_plays_check.js web/replay_plays.js web/shell.html web/history_page.js include/ants_app/replay_view.hpp          (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const [modulePath, shellPath, historyPath, headerPath] = process.argv.slice(2).map((p) => path.resolve(p));
if (!headerPath) { console.log('usage: web_plays_check.js replay_plays.js shell.html history_page.js replay_view.hpp'); process.exit(2); }
const P = require(modulePath);
const H = require(historyPath);

let checks = 0, failures = 0;
function expect(label, got, want) {
    checks++;
    const a = JSON.stringify(got), b = JSON.stringify(want);
    if (a !== b) { failures++; console.log('FAIL ' + label + ': got ' + a + ', wanted ' + b); }
}
process.on('unhandledRejection', (e) => { failures++; process.exitCode = 1; console.log('FAIL a promise was rejected and nobody caught it: ' + e); });

const SEC = 1000, DAY = 24 * 60 * 60 * 1000, T0 = 1800000000000;
const ID = 'ants-TREASURE-20261124-014546Z', ID2 = 'ants-SMALL-20261124-010046Z';
const KEY = 'ants.plays';

function fakeStore(initial) {
    const m = new Map(Object.entries(initial || {}));
    return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => { m.set(k, String(v)); }, m };
}
// One page view: its own window (document, fetch, timers, storage), a clock that may be shared with another page view, and a game that is playing or not
function rig(o) {
    o = o || {};
    const r = { clock: o.clock || { now: T0 }, posts: [], timers: [], later: [], visible: true, playing: true, store: 'store' in o ? o.store : fakeStore() };
    r.win = {
        Date: { now: () => r.clock.now },
        document: { get visibilityState() { return r.visible ? 'visible' : 'hidden'; } },
        fetch(url, init) { r.posts.push({ url, init }); return o.fetch ? o.fetch(url, init, r) : Promise.resolve({ ok: true, status: 204 }); },
        setTimeout(fn, ms) { r.later.push({ fn, ms }); return r.later.length; },
        setInterval(fn, ms) { r.timers.push({ fn, ms, on: true }); return r.timers.length; },
        clearInterval(id) { r.timers[id - 1].on = false; }
    };
    Object.defineProperty(r.win, 'localStorage', { get() { if (o.storage === 'throws') throw new Error('SecurityError'); return o.storage === 'null' ? null : r.store; } });
    r.start = (id) => P.start(r.win, id === undefined ? ID : id, () => { if (o.throws) throw new Error('the game does not answer'); return r.playing; });
    r.tick = () => { r.timers.filter((t) => t.on).forEach((t) => t.fn()); };
    // time passes in steps of `step` ms, the page looking at the game at the end of each
    r.run = (ms, step) => { while (ms > 0) { const d = Math.min(step || 100, ms); r.clock.now += d; ms -= d; r.tick(); } };
    return r;
}
const stored = (r) => JSON.parse(r.store.getItem(KEY));

const settle = () => new Promise((resolve) => setImmediate(resolve));

(async () => {
    // ---- what counts: 10 seconds of playing ----
    {
        const r = rig(); r.start();
        expect('the page looks at the game several times a second and not too often', [r.timers.length, r.timers[0].ms >= 100 && r.timers[0].ms <= 500], [1, true]);
        r.run(9900);
        expect('9.9 seconds of playing are not a play', r.posts.length, 0);
        r.run(100);
        expect('10 seconds of playing are one play', r.posts.length, 1);
        expect('... sent to the door for this recording, a POST with no body, no header and no cookies', [r.posts[0].url, r.posts[0].init.method, r.posts[0].init.credentials, 'body' in r.posts[0].init, 'headers' in r.posts[0].init], ['/counts/play/' + ID, 'POST', 'omit', false, false]);
        r.run(120 * SEC);
        expect('... and only once, however long it goes on', r.posts.length, 1);
        expect('... the timer has stopped itself', r.timers.map((t) => t.on), [false]);
    }
    {
        const r = rig(); r.start();
        r.run(60 * SEC, 250);
        expect('looks every 250 ms count the same', r.posts.length, 1);
        const q = rig(); q.start();
        q.run(9750, 250);
        expect('... 9.75 seconds are not a play', q.posts.length, 0);
        q.run(250, 250);
        expect('... and 10 are', q.posts.length, 1);
    }
    {
        const r = rig(); r.start();
        r.run(6 * SEC); r.playing = false; r.run(30 * SEC); r.playing = true;
        r.run(100);                                   // (the first look that finds it playing again is where the counting goes on)
        r.run(3900);
        expect('the time of a pause does not count: 6 + 3.9 seconds of playing are not a play', r.posts.length, 0);
        r.run(100);
        expect('... and 6 + 4 are, whatever the pause between', r.posts.length, 1);
    }
    {
        const r = rig(); r.start();
        r.playing = false; r.run(120 * SEC);
        expect('a recording that is paused for ever is no play', r.posts.length, 0);
        r.playing = true; r.run(100); r.run(9900);
        expect('... the first look that finds it playing is where the counting starts: 9.9 seconds from there are not a play', r.posts.length, 0);
        r.run(100);
        expect('... and then 10 seconds are one play', r.posts.length, 1);
    }
    {
        const r = rig(); r.start();
        r.run(6 * SEC); r.visible = false; r.run(30 * SEC); r.visible = true; r.run(100); r.run(3900);
        expect('a hidden tab does not count (the game may go on in it): 6 + 3.9 seconds shown are not a play', r.posts.length, 0);
        r.run(100);
        expect('... 6 + 4 are', r.posts.length, 1);
        const q = rig(); q.start();
        q.run(6 * SEC); q.visible = false; q.run(10 * 60 * SEC, 60 * SEC); q.visible = true; q.run(100); q.run(3900);
        expect('a hidden tab that the browser looks at once a minute does not count either', q.posts.length, 0);
        q.run(100);
        expect('... 6 + 4 shown are a play', q.posts.length, 1);
        const w = rig(); w.start();
        w.visible = false; w.run(120 * SEC);
        expect('a tab that is never shown is no play', w.posts.length, 0);
    }
    {
        const r = rig(); r.start();
        r.run(5 * SEC); r.clock.now += 3600 * SEC; r.tick();
        expect('a computer that slept for an hour adds at most one second (not an hour)', r.posts.length, 0);
        r.run(3900);
        expect('... 5 + 1 + 3.9 seconds are not a play', r.posts.length, 0);
        r.run(100);
        expect('... 5 + 1 + 4 are', r.posts.length, 1);
        const q = rig(); q.start();
        q.run(5 * SEC); q.clock.now -= 600 * SEC; q.tick(); q.run(4900);
        expect('a clock that goes back adds nothing', q.posts.length, 0);
        q.run(100);
        expect('... and 10 seconds of the rest are a play', q.posts.length, 1);
    }
    {
        const r = rig(); r.start();
        r.run(30 * SEC, 1000);
        expect('a look every second counts the whole second', r.posts.length, 1);
        const q = rig(); q.start();
        q.run(90 * SEC, 10 * SEC);
        expect('looks every ten seconds count one second each (the gap is capped): 9 looks are not a play', q.posts.length, 0);
        q.run(10 * SEC, 10 * SEC);
        expect('... ten looks are', q.posts.length, 1);
    }
    {
        const r = rig({ throws: true }); const c = r.start();
        let threw = null;
        try { r.run(30 * SEC); } catch (e) { threw = String(e); }
        expect('a game that does not answer is not playing, and nothing throws', [threw, r.posts.length, c.done()], [null, 0, false]);
    }
    expect('a page that has no timer, no window or an id that is no id gets no counter', [P.start(null, ID, () => true), P.start({}, ID, () => true), P.start(rig().win, '../x', () => true), P.start(rig().win, '', () => true), P.start(rig().win, 5, () => true)], [null, null, null, null, null]);

    // ---- once a day for a recording in this browser ----
    {
        const store = fakeStore(), clock = { now: T0 };
        const a = rig({ clock, store }); a.start(); a.run(10 * SEC);
        expect('the first page view counts', a.posts.length, 1);
        expect('... and writes it down in this browser: { id: ms }', stored(a), { [ID]: clock.now });
        const b = rig({ clock, store }); b.start(); b.run(60 * SEC);
        expect('the same recording again within the day is not counted: two page views share the memory', b.posts.length, 0);
        expect('... nothing is sent and nothing changes in the memory', stored(b), { [ID]: T0 + 10 * SEC });
        const counted = T0 + 10 * SEC;
        clock.now = counted + DAY - 11 * SEC;                                      // (a page view that has 10 seconds to go ends a day less a second after the play)
        const c = rig({ clock, store }); c.start(); c.run(10 * SEC);
        expect('... still a day less a second after it was counted', c.posts.length, 0);
        clock.now = counted + DAY - 10 * SEC;
        const d = rig({ clock, store }); d.start(); d.run(10 * SEC);
        expect('... a day after it is counted again (the memory of the day is over)', d.posts.length, 1);
        expect('... and the memory has one entry of the new time', Object.keys(stored(d)).length, 1);
        const e = rig({ clock, store }); e.start(ID2); e.run(30 * SEC);
        expect('another recording is counted', [e.posts.length, e.posts[0].url], [1, '/counts/play/' + ID2]);
        expect('... and both are remembered', Object.keys(stored(e)).sort(), [ID, ID2].sort());
    }
    {
        // two tabs reach their 10 seconds at the same moment: the one that sends has written the memory first, so the other sees it
        const clock = { now: T0 }, store = fakeStore();
        let seen = null;
        const b = rig({ clock, store });
        const a = rig({ clock, store, fetch: () => { seen = store.getItem(KEY); b.tick(); return Promise.resolve({ ok: true }); } });
        a.start(); b.start();
        a.run(9900); b.clock.now = a.clock.now;
        a.clock.now += 100;
        a.tick();
        expect('the memory is written before the play is sent', seen && JSON.parse(seen)[ID] === clock.now, true);
        expect('two tabs that reach 10 seconds together send one play', a.posts.length + b.posts.length, 1);
    }
    {
        // what the memory holds: nothing is trusted, a day is the limit, 200 are the most
        const store = fakeStore();
        const seeds = {};
        for (let i = 0; i < 250; i++) seeds['seed-' + i] = T0 - (i + 1) * 1000;
        seeds['stale-1'] = T0 - DAY - 1; seeds['stale-2'] = T0 - 3 * DAY; seeds['future'] = T0 + 60 * SEC; seeds['text'] = 'x'; seeds['nan'] = null; seeds['neg'] = -5;
        store.setItem(KEY, JSON.stringify(seeds));
        const r = rig({ store }); r.start(); r.run(10 * SEC);
        const kept = stored(r), names = Object.keys(kept);
        expect('a play is counted', r.posts.length, 1);
        expect('at most 200 recordings are kept', names.length, 200);
        expect('... the new one and the newest of the others', [names.includes(ID), names.includes('seed-0'), names.includes('seed-198'), names.includes('seed-199'), names.includes('seed-249')], [true, true, true, false, false]);
        expect('... none that is a day old, from the future, or not a number', ['stale-1', 'stale-2', 'future', 'text', 'nan', 'neg'].map((k) => k in kept), [false, false, false, false, false, false]);
        expect('... every entry is a time in milliseconds', names.every((k) => typeof kept[k] === 'number' && kept[k] <= r.clock.now && r.clock.now - kept[k] < DAY), true);
    }
    for (const [what, raw] of [['a text that is not JSON', 'not json {'], ['a list', '[1,2,3]'], ['a number', '5'], ['null', 'null'], ['a text', '"x"']]) {
        const r = rig({ store: fakeStore({ [KEY]: raw }) }); r.start(); r.run(10 * SEC);
        expect('a memory that is ' + what + ' is an empty one: the play is counted and the memory is made new', [r.posts.length, stored(r)], [1, { [ID]: r.clock.now }]);
    }
    {
        const raw = '{"__proto__":{"x":1},"' + ID + '":' + (T0 - 1000) + '}';
        const r = rig({ store: fakeStore({ [KEY]: raw }) }); r.start(); r.run(30 * SEC);
        expect('a memory with a key that names the prototype does no harm, and an id in it is remembered', [r.posts.length, ({}).x], [0, undefined]);
    }

    // ---- a browser that refuses its storage ----
    for (const how of ['throws', 'null']) {
        const first = rig({ storage: how });
        let threw = null;
        try { first.start(); first.run(60 * SEC); } catch (e) { threw = String(e); }
        expect('storage that ' + (how === 'throws' ? 'throws when it is asked for' : 'is not there') + ': one play for the page view, and nothing throws', [threw, first.posts.length], [null, 1]);
        const second = rig({ storage: how }); second.start(); second.run(60 * SEC);
        expect('... the next page view counts again (nothing is remembered)', second.posts.length, 1);
    }
    for (const [how, store] of [['getItem throws', { getItem() { throw new Error('denied'); }, setItem() {} }], ['setItem throws (full)', { getItem: () => null, setItem() { throw new Error('QuotaExceededError'); } }]]) {
        const r = rig({ store });
        let threw = null;
        try { r.start(); r.run(60 * SEC); } catch (e) { threw = String(e); }
        expect('storage where ' + how + ': one play, nothing throws', [threw, r.posts.length], [null, 1]);
    }

    // ---- a play that does not arrive ----
    for (const [what, fetch] of [['is refused (rejected)', () => Promise.reject(new Error('down'))], ['throws at once', () => { throw new Error('no network'); }], ['answers 404', () => Promise.resolve({ ok: false, status: 404 })]]) {
        const r = rig({ fetch }); r.start();
        let threw = null;
        try { r.run(10 * SEC); r.run(60 * SEC); } catch (e) { threw = String(e); }
        await settle();                                 // (what a rejected play does happens after this turn)
        expect('a play that ' + what + ' is ignored and sent once, never again', [threw, r.posts.length], [null, 1]);
    }
    {
        const r = rig({ fetch: () => Promise.resolve({ ok: false, status: 429 }) }); r.start(); r.run(10 * SEC); await settle();
        expect('a play that the door answers with 429 is tried once more later: one timer, a minute or more but under two', [r.posts.length, r.later.length, r.later[0] && r.later[0].ms >= 60000 && r.later[0].ms < 90000], [1, 1, true]);
        const real = Math.random;
        try {
            Math.random = () => 0; const low = rig({ fetch: () => Promise.resolve({ ok: false, status: 429 }) }); low.start(); low.run(10 * SEC); await settle();
            Math.random = () => 0.999; const high = rig({ fetch: () => Promise.resolve({ ok: false, status: 429 }) }); high.start(); high.run(10 * SEC); await settle();
            expect('... the wait is a minute and a chance of up to half a minute more, so that the pages that were turned away together do not come back together', [low.later[0].ms, high.later[0].ms > 89000 && high.later[0].ms < 90000], [60000, true]);
        } finally { Math.random = real; }
        r.later[0].fn(); await settle();
        expect('... the second try is a POST to the same address, and a second 429 is not tried again', [r.posts.length, r.posts[1].url, r.posts[1].init.method, r.later.length], [2, r.posts[0].url, 'POST', 1]);
        const ok = rig({ fetch: () => Promise.resolve({ ok: true, status: 204 }) }); ok.start(); ok.run(10 * SEC); await settle();
        expect('a play that is counted is not tried again', [ok.posts.length, ok.later.length], [1, 0]);
        const away = rig({ fetch: () => Promise.resolve({ ok: false, status: 404 }) }); away.start(); away.run(10 * SEC); await settle();
        expect('... nor one that the door does not know (404)', [away.posts.length, away.later.length], [1, 0]);
        const none = rig({ fetch: () => Promise.reject(new Error('down')) }); none.start(); none.run(10 * SEC); await settle();
        expect('... nor one that did not arrive at all', [none.posts.length, none.later.length], [1, 0]);
    }
    {
        const r = rig({ fetch: () => Promise.reject(new Error('down')) }); r.start(); r.run(10 * SEC); await settle();
        const again = rig({ store: r.store, clock: r.clock }); again.start(); again.run(60 * SEC);
        expect('a play that failed is not sent on the next page view either: it was counted for the day', again.posts.length, 0);
    }

    // ---- the page's own call (web/shell.html) ----
    {
        const text = fs.readFileSync(shellPath, 'utf8');
        const begin = text.indexOf('// PLAYS_BEGIN'), end = text.indexOf('// PLAYS_END');
        expect('the page has the block that this check runs', [begin > 0, end > begin], [true, true]);
        const block = text.slice(begin, end);
        // the numbers of the page's own tables are the game's (include/ants_app/replay_view.hpp)
        const header = fs.readFileSync(headerPath, 'utf8');
        const enumOf = (name) => { const m = new RegExp('enum class ' + name + '[^{]*\\{([^}]*)\\}').exec(header); const out = {}; m[1].replace(/\/\/.*$/gm, '').split(',').forEach((e) => { const x = /(\w+)\s*=\s*(\d+)/.exec(e); if (x) out[x[1]] = +x[2]; }); return out; };
        const states = enumOf('ReplayState'), values = enumOf('ReplayValue');
        const STATE = vm.runInNewContext('(' + /var STATE = (\{[^}]*\});/.exec(text)[1] + ')'), VALUE = vm.runInNewContext('(' + /var VALUE = (\{[^}]*\});/.exec(text)[1] + ')');
        expect('the page\'s numbers for the replay state and for the state\'s place are the game\'s', [STATE.playing, STATE.paused, STATE.jumping, STATE.ended, STATE.cut, STATE.failed, VALUE.state], [states.Playing, states.Paused, states.Jumping, states.Ended, states.CutShort, states.Failed, values.State]);

        function page(o) {
            const r = rig({ clock: o.clock, store: o.store });
            const scripts = [];
            const game = { state: -1 };
            const context = {
                ANTS_LIVE_MODE: !!o.live, ANTS_OWN_MODE: !!o.own, FILE: o.file === undefined ? ID + '.antsrep' : o.file, ANTS_BUILD_VER: '123', STATE, VALUE,
                get: (what) => (what === VALUE.state ? game.state : 2),           // (anything but the state is 2, the number of "playing")
                window: Object.assign(r.win, { AntsHistory: H, AntsPlays: P }),
                document: { createElement: (tag) => ({ tag }), head: { appendChild: (n) => { scripts.push(n); } } }
            };
            vm.runInNewContext(block, context);
            r.scripts = scripts; r.game = game;
            r.load = () => { if (scripts[0] && scripts[0].onload) scripts[0].onload(); };
            return r;
        }
        const kept = page({});
        expect('a recording of the store fetches the counter once (as the build\'s own version of the script) and starts it when it is loaded', [kept.scripts.length, kept.scripts[0].tag, /^replay_plays\.js\?v=123$/.test(kept.scripts[0].src), kept.timers.length], [1, 'script', true, 0]);
        kept.game.state = states.Playing;
        kept.load();
        expect('... one timer, for this recording', kept.timers.length, 1);
        kept.run(10 * SEC);
        expect('... a game that says "playing" for 10 seconds makes one play of this recording', [kept.posts.length, kept.posts[0].url], [1, '/counts/play/' + ID]);
        for (const [word, state] of [['paused', states.Paused], ['jumping', states.Jumping], ['ended', states.Ended], ['cut short', states.CutShort], ['failed', states.Failed], ['not ready (-1)', -1]]) {
            const p = page({}); p.load(); p.game.state = state; p.run(120 * SEC);
            expect('a game that is ' + word + ' is no play, however long', p.posts.length, 0);
        }
        const jump = page({}); jump.game.state = states.Playing; jump.load(); jump.run(6 * SEC); jump.game.state = states.Jumping; jump.run(60 * SEC); jump.game.state = states.Playing; jump.run(100); jump.run(3900);
        expect('time spent jumping does not count', jump.posts.length, 0);
        jump.run(100);
        expect('... the rest does', jump.posts.length, 1);
        expect('a match that is being played counts nothing and fetches nothing (even when it has a file)', page({ live: true }).scripts.length, 0);
        expect('a file of your own counts nothing and fetches nothing', page({ own: true, file: null }).scripts.length + page({ own: true }).scripts.length, 0);
        expect('an address that has no recording in it counts nothing and fetches nothing', [null, '', '../etc/passwd', 'x.antsrep', ID, ID + '.antsrep/x'].map((f) => page({ file: f }).scripts.length), [0, 0, 0, 0, 0, 0]);
    }
    await settle();
    console.log('plays: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check stopped: ' + (e && e.stack || e)); process.exit(1); });

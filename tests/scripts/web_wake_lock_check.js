// Runs the page's OWN code for the screen that stays on (web/wake_lock.js, AntsWakeLock.forPage, which web/shell.html calls), without a browser, with fakes for the browser's Screen Wake Lock, the document, the
// window and the game, and checks:
//   * a browser without navigator.wakeLock: nothing is made (no timer, no listener, no call); with it, one poll every 500 ms and three listeners (visibilitychange, pagehide, pageshow);
//   * a match that is played (ants_match_running 1: from the countdown card to the results): the lock is asked for once, never twice (not while a request is pending either), let go at the results, asked for
//     again for the next match; a game that is not ready, or says -1 (no game), wants nothing;
//   * a recording (the replay states Playing 2 and Jumping 4): asked for on play, let go on pause, at the end and when it is cut short, asked for again on play and on "Watch again", kept through a jump; a file
//     that cannot be shown (7), no replay (0 and -1) and any other number want nothing; a recording never reads the match's answer and a match never reads the recording's;
//   * the page hidden: the browser drops the lock (flag, event, or either alone), the page asks again at once when it is visible if it is still wanted, and asks for nothing while it is hidden;
//   * a request that is refused or that throws (or that answers with no promise): dropped quietly, not asked again for 15 s, asked again when the page becomes visible; a release that is refused or throws;
//     no rejection is left unhandled; no error reaches the page's timer;
//   * a lock that arrives when it is not wanted any more (the match ended, the page went) is let go at once;
//   * pagehide lets the lock go and stops the poll; a page that comes back from the browser's cache (pageshow, persisted) asks again, one that does not does not;
//   * a game that does not answer (the call throws) wants nothing;
//   * the page's own call (the block of web/shell.html between WAKE_LOCK_BEGIN and WAKE_LOCK_END, run on a page of fakes): the right answers of the game for a match and for a recording, a game with no
//     exports, no Module at all, a page without the script and a browser without the API.
// tests/scripts/test_web_wake_lock.py runs this with node (the quick tier). usage: node web_wake_lock_check.js web/wake_lock.js [web/shell.html]     (exit 0: every check holds; every failure is printed)
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const modulePath = process.argv[2];
if (!modulePath) { console.log('usage: web_wake_lock_check.js wake_lock.js [shell.html]'); process.exit(2); }
const W = require(path.resolve(modulePath));
const shellPath = process.argv[3] || path.join(path.dirname(path.resolve(modulePath)), 'shell.html');

let unhandled = 0;
process.on('unhandledRejection', () => { unhandled++; });
const flush = () => new Promise((resolve) => setImmediate(resolve));       // (the promises of the fakes settle, and an unhandled rejection is reported, within this)

// A sentinel as the browser makes it: released is a flag, release() sets it and fires the 'release' event; the browser itself lets go with drop(how)
function sentinel(opts) {
    const s = { released: false, listeners: [], releaseCalls: 0 };
    s.addEventListener = (type, fn) => { if (type === 'release') s.listeners.push(fn); };
    s.release = () => {
        s.releaseCalls++;
        if (opts.release === 'throw') throw new Error('InvalidStateError');
        if (!s.released) { s.released = true; s.listeners.forEach((fn) => fn({ type: 'release' })); }
        return opts.release === 'reject' ? Promise.reject(new Error('refused')) : Promise.resolve();
    };
    s.drop = (how) => {                                                    // the browser lets go: both (the flag, then the event), the flag alone, or the event alone
        if (how !== 'event') s.released = true;
        if (how !== 'flag') s.listeners.forEach((fn) => fn({ type: 'release' }));
    };
    return s;
}

function lock() {
    const api = { calls: [], sentinels: [], mode: 'grant', release: 'ok', pending: [] };
    api.request = (type) => {
        api.calls.push(type);
        if (api.mode === 'throw') throw new Error('NotAllowedError');
        if (api.mode === 'reject') return Promise.reject(new Error('NotAllowedError'));
        if (api.mode === 'plain') return undefined;
        const s = sentinel(api);
        api.sentinels.push(s);
        if (api.mode === 'pending') return new Promise((resolve) => { api.pending.push(() => resolve(s)); });
        return Promise.resolve(s);
    };
    return api;
}

// The page: a document and a window that keep their listeners, a timer that is run by hand, a clock that is moved by hand, and a game that says what the test says
function rig(opts) {
    opts = opts || {};
    const game = { ready: true, match: 0, state: 0, other: 2 };
    const api = lock();
    const docL = {}, winL = {}, timers = [];
    let now = 1000000;
    const doc = { visibilityState: 'visible', addEventListener(t, f) { (docL[t] = docL[t] || []).push(f); } };
    const win = { addEventListener(t, f) { (winL[t] = winL[t] || []).push(f); } };
    const page = {
        win, doc, nav: opts.nav || (opts.noApi ? {} : { wakeLock: api }), replay: !!opts.replay,
        ready: () => game.ready, matchRunning: () => game.match, replayState: () => game.state,
        every: (fn, ms) => { timers.push({ fn, ms, on: true }); return timers.length; },
        clear: (id) => { timers[id - 1].on = false; },
        now: () => now,
    };
    const r = {
        game, api, doc, win, timers, page,
        advance(ms) { now += ms; },
        poll() { timers.forEach((t) => { if (t.on) t.fn(); }); },
        fireDoc(type, ev) { (docL[type] || []).forEach((f) => f(ev || {})); },
        fireWin(type, ev) { (winL[type] || []).forEach((f) => f(ev || {})); },
        listeners: () => Object.keys(docL).map((k) => 'doc:' + k).concat(Object.keys(winL).map((k) => 'win:' + k)).sort(),
        last: () => api.sentinels[api.sentinels.length - 1],
    };
    r.lock = opts.bare ? null : W.forPage(page);                         // (bare: the page's own call makes the lock)
    return r;
}
async function look(r) { r.poll(); await flush(); }               // one look of the poll, and the promises that it started

(async function main() {
    try {

        // ---- what is registered, and the browsers that have no lock
        {
            const none = rig({ noApi: true });
            expect('no navigator.wakeLock: nothing is made', [none.lock, none.timers.length, none.listeners(), none.api.calls], [null, 0, [], []]);
            expect('a navigator.wakeLock without request is no lock either', [W.forPage({ nav: { wakeLock: {} }, win: {}, doc: {}, ready: () => true }), W.forPage({ win: {}, doc: {}, ready: () => true })], [null, null]);
            const r = rig();
            expect('one poll, every 500 ms', r.timers.map((t) => t.ms), [500]);
            expect('three listeners: the page becomes visible, is left, comes back from the cache', r.listeners(), ['doc:visibilitychange', 'win:pageshow', 'win:pagehide'].sort());
            expect('nothing is asked for before the game wants it', r.api.calls, []);
            expect('the pure function reads the same', [W.wantedOf({ ready: () => true, replay: false, matchRunning: () => 1, replayState: () => 3 }), W.wantedOf({ ready: () => false, replay: false, matchRunning: () => 1, replayState: () => 2 })], [true, false]);
        }

        // ---- a match that is played
        {
            const r = rig();
            r.game.ready = false; r.game.match = 1;
            await look(r); await look(r);
            expect('a game that is not ready wants nothing', r.api.calls, []);
            r.game.ready = true; r.game.match = -1;
            await look(r);
            expect('a game that says -1 (no game) wants nothing', r.api.calls, []);
            r.game.match = 0;
            await look(r);
            expect('a game with no match running (the menu, the results) wants nothing', r.api.calls, []);
            r.game.match = 1;
            await look(r);
            expect('a match: the screen lock is asked for', r.api.calls, ['screen']);
            expect('and is held', r.lock.state().held, true);
            for (let i = 0; i < 6; i++) await look(r);
            expect('looking again asks for nothing more', [r.api.calls.length, r.last().releaseCalls], [1, 0]);
            r.game.match = 0;
            await look(r);
            expect('the results: the lock is let go', [r.last().released, r.last().releaseCalls, r.lock.state().held], [true, 1, false]);
            await look(r); await look(r);
            expect('and nothing more is done', [r.api.calls.length, r.last().releaseCalls], [1, 1]);
            r.game.match = 1;
            await look(r);
            expect('the next match asks again', [r.api.calls.length, r.lock.state().held], [2, true]);
            r.game.ready = false;
            await look(r);
            expect('a game that is not ready any more lets it go', [r.last().released, r.lock.state().held], [true, false]);
            expect('no rejection is left unhandled', unhandled, 0);
        }
        {
            const r = rig();
            r.game.match = 1;
            r.api.mode = 'pending';
            r.poll(); r.poll(); r.poll(); await flush();
            expect('a request that is not answered yet is not made again', [r.api.calls.length, r.lock.state().asking, r.lock.state().held], [1, true, false]);
            r.api.pending[0](); await flush();
            expect('and the lock is held when it comes', [r.lock.state().asking, r.lock.state().held, r.api.calls.length], [false, true, 1]);
        }

        // ---- a recording
        {
            const r = rig({ replay: true });
            r.game.match = 1;                                                       // (a recording never reads the match's answer)
            r.game.state = 3;
            await look(r);
            expect('a recording that is paused wants nothing, whatever the match says', r.api.calls, []);
            for (const bad of [-1, 0, 1, 5, 6, 7, 8, 99]) { r.game.state = bad; await look(r); }
            expect('no replay (-1, 0), the ended (5), the cut short (6), the failed (7) and the numbers that are no state want nothing', r.api.calls, []);
            r.game.state = 2;
            await look(r);
            expect('a recording that plays: the lock is asked for', [r.api.calls, r.lock.state().held], [['screen'], true]);
            const first = r.last();
            r.game.state = 3;
            await look(r);
            expect('paused: let go', [first.released, first.releaseCalls, r.lock.state().held], [true, 1, false]);
            r.game.state = 2;
            await look(r);
            expect('play: asked for again', [r.api.calls.length, r.lock.state().held], [2, true]);
            const second = r.last();
            r.game.state = 4;
            await look(r); await look(r);
            expect('a jump (playing to a turn without a picture) keeps the lock', [r.api.calls.length, second.released, second.releaseCalls], [2, false, 0]);
            r.game.state = 2;
            await look(r);
            expect('and playing on after it asks for nothing', [r.api.calls.length, second.released], [2, false]);
            r.game.state = 5;
            await look(r);
            expect('the end of the match (the results are up): let go', [second.released, r.lock.state().held], [true, false]);
            r.game.state = 2;
            await look(r);
            expect('"Watch again": asked for again', [r.api.calls.length, r.lock.state().held], [3, true]);
            const third = r.last();
            r.game.state = 6;
            await look(r);
            expect('a recording that was cut short: let go', [third.released, r.lock.state().held], [true, false]);
            expect('no rejection is left unhandled', unhandled, 0);
        }
        {
            const r = rig({ replay: false });
            r.game.state = 2; r.game.match = 0;                                       // (a match never reads the recording's answer)
            await look(r);
            expect('a match that is not running wants nothing, whatever the recording state is', r.api.calls, []);
        }

        // ---- the page hidden
        {
            const r = rig();
            r.game.match = 1;
            await look(r);
            const held = r.last();
            r.doc.visibilityState = 'hidden';
            held.drop('both');
            r.fireDoc('visibilitychange');
            expect('hidden: the browser dropped the lock, the page knows', [r.lock.state().held, r.api.calls.length], [false, 1]);
            await look(r); await look(r);
            expect('hidden: nothing is asked for', r.api.calls.length, 1);
            r.doc.visibilityState = 'visible';
            r.fireDoc('visibilitychange');
            await flush();
            expect('visible again: asked for at once, without waiting for the poll', [r.api.calls.length, r.lock.state().held], [2, true]);
            r.doc.visibilityState = 'hidden';
            r.last().drop('both');
            r.game.match = 0;
            r.fireDoc('visibilitychange');
            r.doc.visibilityState = 'visible';
            r.fireDoc('visibilitychange');
            await flush();
            expect('visible again with the match over: nothing is asked for', r.api.calls.length, 2);
        }
        {
            const r = rig();
            r.game.match = 1;
            r.doc.visibilityState = 'hidden';
            await look(r);
            r.fireDoc('visibilitychange');
            await flush();
            expect('a page that is hidden when the match starts asks for nothing', r.api.calls.length, 0);
            r.doc.visibilityState = 'visible';
            r.fireDoc('visibilitychange');
            await flush();
            expect('and asks when it is visible', r.api.calls.length, 1);
        }
        for (const how of ['flag', 'event']) {
            const r = rig();
            r.game.match = 1;
            await look(r);
            r.last().drop(how);
            await look(r);
            expect('the browser lets go (' + how + ' alone says so): the next look asks again', [r.api.calls.length, r.lock.state().held], [2, true]);
        }

        // ---- a request that is refused, one that throws, one that answers with no promise
        for (const mode of ['reject', 'throw', 'plain']) {
            const r = rig();
            r.game.match = 1;
            r.api.mode = mode;
            let thrown = null;
            try { await look(r); } catch (e) { thrown = String(e); }
            expect('a request that ' + mode + 's is dropped quietly: nothing reaches the timer, no hold, one call', [thrown, r.api.calls.length, r.lock.state().held, r.lock.state().asking], [null, 1, false, false]);
            await look(r); await look(r);
            expect(mode + ': not asked again while it waits', r.api.calls.length, 1);
            r.advance(14999);
            await look(r);
            expect(mode + ': not asked again at 14.999 s', r.api.calls.length, 1);
            r.advance(1);
            r.api.mode = 'grant';
            await look(r);
            expect(mode + ': asked again after 15 s, and held when it is granted', [r.api.calls.length, r.lock.state().held], [2, true]);
            const v = rig();
            v.game.match = 1;
            v.api.mode = mode;
            await look(v);
            v.api.mode = 'grant';
            v.fireDoc('visibilitychange');
            await flush();
            expect(mode + ': a page that becomes visible asks at once, without the wait', [v.api.calls.length, v.lock.state().held], [2, true]);
        }
        {
            for (const release of ['reject', 'throw']) {
                const r = rig();
                r.api.release = release;
                r.game.match = 1;
                await look(r);
                r.game.match = 0;
                let thrown = null;
                try { await look(r); } catch (e) { thrown = String(e); }
                expect('a release that ' + release + 's is dropped quietly', [thrown, r.last().releaseCalls, r.lock.state().held], [null, 1, false]);
                r.game.match = 1;
                await look(r);
                expect('and the next match asks as ever', [r.api.calls.length, r.lock.state().held], [2, true]);
            }
            expect('no rejection is left unhandled', unhandled, 0);
        }

        // ---- a lock that comes when it is not wanted any more
        {
            const r = rig();
            r.game.match = 1;
            r.api.mode = 'pending';
            r.poll(); await flush();
            r.game.match = 0;
            r.api.pending[0](); await flush();
            expect('a lock that arrives after the match: let go at once, not held', [r.api.sentinels[0].released, r.api.sentinels[0].releaseCalls, r.lock.state().held], [true, 1, false]);
            const p = rig();
            p.game.match = 1;
            p.api.mode = 'pending';
            p.poll(); await flush();
            p.fireWin('pagehide');
            p.api.pending[0](); await flush();
            expect('and one that arrives after the page was left', [p.api.sentinels[0].released, p.lock.state().held], [true, false]);
        }

        // ---- the page is left, and comes back from the cache
        {
            const r = rig();
            r.game.match = 1;
            await look(r);
            const held = r.last();
            r.fireWin('pagehide');
            expect('pagehide: the lock is let go and the poll stops', [held.released, r.lock.state().held, r.timers[0].on, r.lock.state().polling, r.lock.state().away], [true, false, false, false, true]);
            r.lock.sync(); await flush();
            expect('a page that has gone asks for nothing', r.api.calls.length, 1);
            r.fireWin('pageshow', { persisted: false });
            await flush();
            expect('a page that is shown again but was not kept (not persisted) does nothing', [r.api.calls.length, r.lock.state().away], [1, true]);
            r.fireWin('pageshow', { persisted: true });
            await flush();
            expect('a page that comes back from the browser\'s cache asks again, and polls again', [r.api.calls.length, r.lock.state().held, r.lock.state().polling, r.lock.state().away, r.timers.length], [2, true, true, false, 2]);
            const fresh = rig();
            fresh.fireWin('pageshow', { persisted: true });
            expect('a first show (persisted, never left) makes no second poll', fresh.timers.length, 1);
        }

        // ---- a game that does not answer
        {
            const r = rig();
            r.game.match = 1;
            r.page.ready = () => { throw new Error('Module is not defined'); };
            let thrown = null;
            try { await look(r); } catch (e) { thrown = String(e); }
            expect('a game whose answer throws wants nothing and breaks nothing', [thrown, r.api.calls.length], [null, 0]);
            r.page.ready = () => true;
            r.page.matchRunning = () => { throw new Error('abort'); };
            try { await look(r); } catch (e) { thrown = String(e); }
            expect('nor does one whose match probe throws', [thrown, r.api.calls.length], [null, 0]);
        }

        // ---- the page's own call (web/shell.html)
        {
            const text = fs.readFileSync(shellPath, 'utf8');
            const begin = text.indexOf('// WAKE_LOCK_BEGIN'), end = text.indexOf('// WAKE_LOCK_END');
            expect('the page has the block that this check runs', [begin > 0, end > begin], [true, true]);
            const block = text.slice(begin, end);
            const pageOf = (opts) => {
                const rr = rig({ replay: opts.replay, bare: true });
                const winOf = { AntsWakeLock: opts.noScript ? undefined : W, addEventListener: rr.win.addEventListener, setInterval: (fn, ms) => { rr.timers.push({ fn, ms, on: true }); return rr.timers.length; }, clearInterval: (id) => { rr.timers[id - 1].on = false; } };
                const module = opts.noExports ? {} : {
                    _ants_match_running: () => rr.game.match,
                    _ants_replay_get: (what) => (what === 0 ? rr.game.state : 2),         // (anything but the state is 2, a number that would hold the lock)
                };
                const context = { window: winOf, document: rr.doc, navigator: opts.noApi ? {} : { wakeLock: rr.api }, ANTS_REPLAY_MODE: !!opts.replay, isReadyToPlay: true };
                if (!opts.noModule) context.Module = module;
                vm.runInNewContext(block, context);
                rr.context = context;
                return rr;
            };
            const m = pageOf({ replay: false });
            m.game.match = 1; m.game.state = 3;
            await look(m);
            expect('a match page asks for the lock while the game says that a match runs, whatever it says of a recording', [m.api.calls, m.timers.map((t) => t.ms)], [['screen'], [500]]);
            m.game.match = 0;
            await look(m);
            expect('and lets it go at the results', m.last().released, true);
            m.context.isReadyToPlay = false; m.game.match = 1;
            await look(m);
            expect('a game that is not ready is not asked about', m.api.calls.length, 1);
            const p = pageOf({ replay: true });
            p.game.match = 1; p.game.state = 3;
            await look(p);
            expect('a recording page reads the state (value 0), not the match and not another value: paused wants nothing', p.api.calls, []);
            p.game.state = 2;
            await look(p);
            expect('and asks while it plays', p.api.calls, ['screen']);
            p.game.state = 3;
            await look(p);
            expect('and lets go when it is paused', p.last().released, true);
            const e = pageOf({ replay: false, noExports: true });
            e.game.match = 1;
            await look(e);
            expect('a game with no exports wants nothing', e.api.calls.length, 0);
            const n = pageOf({ replay: false, noModule: true });
            n.game.match = 1;
            let thrown = null;
            try { await look(n); } catch (err) { thrown = String(err); }
            expect('a page with no Module at all wants nothing and throws nothing', [thrown, n.api.calls.length], [null, 0]);
            let missing = null, s = null;
            try { s = pageOf({ replay: false, noScript: true }); } catch (err) { missing = String(err); }
            expect('a page that did not get the script makes nothing and throws nothing', [missing, s && s.timers.length, s && s.api.calls.length], [null, 0, 0]);
            const a = pageOf({ replay: false, noApi: true });
            expect('a browser without the API gets no timer from the page\'s call', a.timers.length, 0);
        }
        expect('no rejection was left unhandled', unhandled, 0);
    } catch (err) {
        console.log('FAIL the check itself broke down: ' + (err && err.stack || err));
        failed++;
    }
    console.log('wake_lock: ' + checks + ' checks, ' + failed + ' failed');
    process.exit(failed ? 1 : 0);
})();

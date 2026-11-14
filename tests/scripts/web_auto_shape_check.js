'use strict';
// Automatic picture shape, run on the pages' OWN code (web/shapes.js, and web/shell.html's blocks between ANTS_PAGE_BEGIN and ANTS_SELECTOR_END) with fake phones and a fake clock:
//   * two phones: an Android-like one whose screen numbers swap when it is turned, and an iPhone-like one whose screen stays 390 x 844 while screen.orientation (or only window.orientation, or only the
//     media query) says the way; both give 20:9 held sideways and the same held upright;
//   * every event that restarts the 300 ms wait (resize, orientationchange, screen.orientation change, fullscreenchange, webkitfullscreenchange, visualViewport resize) and nothing else; a burst gives one
//     switch at its end; a change of the window's height alone gives none;
//   * a hand pick (stored, or ?aspect= in the address) survives every event; a stored pick holds only for the way it was made in; the picks of the earlier key are forgotten;
//   * the list's Automatic entry clears the pick (and the address) and picks at once; Automatic's own switches write neither the key nor the address; a spectator follows the same rule.
// usage: web_auto_shape_check.js shell.html
const fs = require('fs');
const nodePath = require('path');
globalThis.AntsShapes = require(nodePath.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const KEY = 'ants.aspect.v3';
const P = (shape, way) => JSON.stringify({ [way || 'sideways']: shape });
let failed = 0;
function expect(label, got, want) {
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}
function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

// an event target: listeners by type; fire(type) calls them
function target() {
    const on = {};
    return {
        on,
        addEventListener(type, fn) { (on[type] = on[type] || []).push(fn); },
        removeEventListener(type, fn) { on[type] = (on[type] || []).filter((f) => f !== fn); },
        fire(type) { (on[type] || []).slice().forEach((fn) => fn({ type })); },
        count(type) { return (on[type] || []).length; },
    };
}

// The phones. `kind` android: the screen's numbers swap on a turn; iphone: they stay. `report`: what the phone answers to "which way": 'type' (screen.orientation), 'legacy' (window.orientation only),
// 'media' (only the media query). Held upright a phone's window is `[w, h - bars]`, sideways `[h, w - bars]`.
const PHONES = {
    android: { w: 412, h: 915, swaps: true },
    iphone: { w: 390, h: 844, swaps: false },
};
function make(spec) {
    const phone = spec.phone ? PHONES[spec.phone] : null;
    const clock = { now: 0, next: 1, timers: {} };
    const win = target();
    const screenOrientation = target();
    const doc = target();
    const visual = target();
    const storage = { data: Object.assign({}, spec.stored || {}), writes: [], removed: [] };
    const store = {
        getItem(key) { return Object.prototype.hasOwnProperty.call(storage.data, key) ? storage.data[key] : null; },
        setItem(key, value) { storage.writes.push([key, String(value)]); storage.data[key] = String(value); },
        removeItem(key) { storage.removed.push(key); delete storage.data[key]; },
    };
    const addressed = [];
    Object.assign(win, {
        AntsReplay: { fileOf() { return 'ants-TEST-20260101-000000Z.antsrep'; }, liveIdOf() { return 'LIVE-1'; } },
        document: doc,
        visualViewport: visual,
        localStorage: store,
        location: { search: spec.search || '', protocol: 'https:', host: 'example.test', href: 'https://example.test/' + (spec.search || ''), pathname: '/', hash: '' },
        history: { state: null, replaceState(state, title, url) { addressed.push(url); const q = url.indexOf('?'); win.location.search = q < 0 ? '' : url.slice(q); win.location.href = 'https://example.test' + url; } },
        screen: { width: 1920, height: 1080 },
        innerWidth: 1920, innerHeight: 970,
        confirm() { return true; },
        setTimeout(fn, ms) { const id = clock.next++; clock.timers[id] = { at: clock.now + ms, fn }; return id; },
        clearTimeout(id) { delete clock.timers[id]; },
    });
    doc.fullscreenElement = null;
    // the way the phone is held: a function of `held` ('sideways' or 'upright') and its kind
    const phoneState = { held: spec.held || 'sideways', report: spec.report || 'type' };
    function settle() {
        const h = phoneState.held, side = h === 'sideways';
        if (phone) {
            const swap = phone.swaps && side;
            win.screen.width = swap ? phone.h : phone.w;
            win.screen.height = swap ? phone.w : phone.h;
            win.innerWidth = side ? phone.h : phone.w;
            win.innerHeight = side ? phone.w - 60 : phone.h - 180;                    // (bars: what is left is smaller)
        }
        delete win.screen.orientation;
        delete win.orientation;
        delete win.matchMedia;
        if (phoneState.report === 'type') { win.screen.orientation = screenOrientation; screenOrientation.type = side ? 'landscape-primary' : 'portrait-primary'; }
        else if (phoneState.report === 'legacy') win.orientation = side ? 90 : 0;
        else if (phoneState.report === 'media') win.matchMedia = (q) => ({ matches: /landscape/.test(q) ? side : !side });
    }
    if (spec.screen) win.screen = Object.assign({}, spec.screen);
    if (spec.inner) { win.innerWidth = spec.inner[0]; win.innerHeight = spec.inner[1]; }
    if (phone || spec.report || spec.held) settle();
    else if (spec.orientationType) { win.screen.orientation = screenOrientation; screenOrientation.type = spec.orientationType; }

    const text = fs.readFileSync(spec.shell, 'utf8');
    const page = between(text, 'ANTS_PAGE_BEGIN', 'ANTS_PAGE_END', spec.shell);
    const selector = between(text, 'ANTS_SELECTOR_BEGIN', 'ANTS_SELECTOR_END', spec.shell);
    const stage = { attributes: { 'data-aspect': '16:9' }, getAttribute(n) { return this.attributes[n] === undefined ? null : this.attributes[n]; }, setAttribute(n, v) { this.attributes[n] = String(v); } };
    const body = { style: { setProperty() {} } };
    const hosts = [{}];
    const pickers = [];
    const notes = [{ hidden: true }];
    const fakeDoc = {
        body,
        getElementById(id) { if (id === 'game-stage') return stage; if (id === 'pic-pick') return hosts[0]; throw new Error('the page asked for #' + id); },
        createElement() { return {}; },
        querySelectorAll(q) { if (q === '.fit-note') return notes; throw new Error('the page asked for ' + q); },
    };
    const makers = { create(shapes, opts) { const picker = { opts, shown: opts.value, auto: !!opts.auto, set(name, auto) { picker.shown = name; picker.auto = !!auto; } }; pickers.push(picker); return picker; } };
    const events = [];
    const gameCalls = [];
    const fakeModule = { _ants_set_aspect(n) { gameCalls.push(n); return 1; } };
    // (the page is run with `document` being the target that the page's watch listens to: the fake of the page's elements is handed in as `elements`)
    win.document = Object.assign(doc, { body, getElementById: fakeDoc.getElementById, createElement: fakeDoc.createElement, querySelectorAll: fakeDoc.querySelectorAll });
    const code = 'var isReadyToPlay = true; var Module = fakeModule; var holdsThisSeat; var stageElement = fakeStage; var progressContainer = { style: {} }; function hideLoadingScreen() {} function antsLoadGameMenu() {}\n'
        + 'function relayout(force) { events.push("relayout"); }\n' + page + '\n' + selector
        + '\nreturn { aspect: function () { return ANTS_ASPECT; }, source: function () { return ANTS_ASPECT_SOURCE; }, args: ANTS_ARGS, pick: antsPickShape, auto: antsAutomatic, own: function () { return ANTS_ADDRESS_OWN; }, onShape: antsOnShape };';
    const run = new Function('window', 'document', 'fakeModule', 'fakeStage', 'events', 'AntsShapePicker', code)(win, win.document, fakeModule, stage, events, makers);
    const env = {
        win, doc, screenOrientation, visual, storage, addressed, run, stage, pickers, gameCalls, events, phoneState,
        // time passes: the timers that are due run, in order
        advance(ms) {
            const until = clock.now + ms;
            for (;;) {
                const due = Object.keys(clock.timers).map((id) => [id, clock.timers[id]]).filter(([, t]) => t.at <= until).sort((a, b) => a[1].at - b[1].at)[0];
                if (!due) break;
                clock.now = due[1].at; delete clock.timers[due[0]]; due[1].fn();
            }
            clock.now = until;
        },
        pending() { return Object.keys(clock.timers).length; },
        // turn the phone and let the events come the way a browser sends them (`only`: just these)
        turn(held, only) {
            phoneState.held = held;
            settle();
            const all = only || ['screen', 'orientationchange', 'resize', 'visual'];
            all.forEach((e) => { if (e === 'screen') screenOrientation.fire('change'); else if (e === 'orientationchange') win.fire('orientationchange'); else if (e === 'resize') win.fire('resize'); else if (e === 'visual') visual.fire('resize'); });
        },
        shape() { return run.aspect(); },
        get now() { return run.aspect() + ' ' + run.source(); },
    };
    return env;
}

const shellPath = process.argv[2];
if (!shellPath) { console.log('usage: web_auto_shape_check.js shell.html'); process.exit(2); }
const SHELL = (spec) => make(Object.assign({ shell: shellPath }, spec));

try {
    // ---- the rule over the two phones: the same answer whatever the screen's numbers do ----
    for (const phone of ['android', 'iphone']) {
        for (const report of ['type', 'legacy', 'media']) {
            const what = phone + ' reporting by ' + report + ', ';
            const side = SHELL({ phone, report, held: 'sideways' });
            const up = SHELL({ phone, report, held: 'upright' });
            expect(what + 'sideways at load: 20:9 by Automatic', side.now, '20:9 default');
            expect(what + 'upright at load: 16:9 (the tall shapes are not here yet)', up.now, '16:9 default');
            expect(what + 'the screen numbers are ' + (phone === 'android' ? 'swapped' : 'kept') + ' on a turn', side.win.screen.width > side.win.screen.height, phone === 'android');
            // a turn: upright to sideways and back, through the events a browser sends
            up.turn('sideways');
            expect(what + 'the turn alone changes nothing before the wait has gone by', up.now, '16:9 default');
            up.advance(299);
            expect(what + '299 ms after the turn: still nothing', up.now, '16:9 default');
            up.advance(1);
            expect(what + '300 ms after the turn: 20:9', up.now, '20:9 default');
            expect(what + 'the game was told once, by the shape\'s place (20:9 is 4)', JSON.stringify(up.gameCalls), '[4]');
            up.turn('upright');
            up.advance(300);
            expect(what + 'turned back: 16:9', up.now + ' ' + JSON.stringify(up.gameCalls), '16:9 default [4,3]');
            expect(what + 'Automatic wrote neither the key nor the address', JSON.stringify(up.storage.writes) + JSON.stringify(up.storage.removed) + JSON.stringify(up.addressed), '[][][]');
        }
    }
    // a sideways Android phone and a sideways iPhone, each at load, give one and the same shape from numbers that differ
    expect('the two sideways phones give one shape from different screen numbers', [SHELL({ phone: 'android', held: 'sideways' }).shape(), SHELL({ phone: 'iphone', held: 'sideways' }).shape(), SHELL({ phone: 'iphone', held: 'sideways' }).win.screen.width], ['20:9', '20:9', 390]);

    // ---- the events: each one starts the wait, nothing else does ----
    for (const [name, fire] of [
        ['resize', (e) => e.win.fire('resize')],
        ['orientationchange', (e) => e.win.fire('orientationchange')],
        ['screen.orientation change', (e) => e.screenOrientation.fire('change')],
        ['fullscreenchange', (e) => e.doc.fire('fullscreenchange')],
        ['webkitfullscreenchange', (e) => e.doc.fire('webkitfullscreenchange')],
        ['visualViewport resize', (e) => e.visual.fire('resize')],
    ]) {
        const e = SHELL({ phone: 'iphone', held: 'upright' });
        e.phoneState.held = 'sideways';
        // the phone is turned without a word from its other events: only this one comes
        const settleNow = () => { e.phoneState.held = 'sideways'; };
        settleNow();
        e.win.screen.orientation = e.screenOrientation; e.screenOrientation.type = 'landscape-primary'; e.win.innerWidth = 844; e.win.innerHeight = 330;
        fire(e);
        expect('the event ' + name + ' starts a wait', e.pending(), 1);
        expect('the event ' + name + ': nothing at once', e.now, '16:9 default');
        e.advance(300);
        expect('the event ' + name + ': the picture is picked 300 ms later', e.now, '20:9 default');
    }
    {   // a state that is not a change (the same width, the way and fullscreen) picks nothing, whatever event comes
        const e = SHELL({ phone: 'iphone', held: 'sideways' });
        for (const type of ['resize', 'orientationchange']) e.win.fire(type);
        e.visual.fire('resize'); e.screenOrientation.fire('change'); e.doc.fire('fullscreenchange');
        e.advance(300);
        expect('events with nothing changed: nothing is switched', JSON.stringify(e.gameCalls) + JSON.stringify(e.events), '[][]');
    }
    {   // each part of the state counts alone. The screen under the page is changed too (a window moved to a monitor), so that Automatic would give another shape, 16:9 for 1920 x 1080
        const under = (e) => { e.win.screen.width = 1920; e.win.screen.height = 1080; };
        const fs = SHELL({ phone: 'iphone', held: 'sideways' });
        under(fs); fs.doc.fullscreenElement = {}; fs.doc.fire('fullscreenchange'); fs.advance(300);
        expect('fullscreen entered, the width and the way the same: the picture is picked again', fs.now, '16:9 default');
        const wk = SHELL({ phone: 'iphone', held: 'sideways' });
        under(wk); wk.doc.webkitFullscreenElement = {}; wk.doc.fire('webkitfullscreenchange'); wk.advance(300);
        expect('...the webkit fullscreen too', wk.now, '16:9 default');
        const way = SHELL({ phone: 'iphone', held: 'sideways' });
        way.screenOrientation.type = 'portrait-primary'; way.screenOrientation.fire('change'); way.advance(300);
        expect('the way alone (the width and fullscreen the same): the picture is picked again', way.now, '16:9 default');
        const wide = SHELL({ phone: 'iphone', held: 'sideways' });
        under(wide); wide.win.innerWidth = 900; wide.win.fire('resize'); wide.advance(300);
        expect('the width alone: the picture is picked again', wide.now, '16:9 default');
        const none = SHELL({ phone: 'iphone', held: 'sideways' });
        under(none); none.win.fire('resize'); none.advance(300);
        expect('none of the three changed: not picked, whatever the screen says now', none.now, '20:9 default');
    }
    {   // a burst of resizes inside 300 ms gives one switch, at its end
        const e = SHELL({ phone: 'iphone', held: 'upright' });
        e.phoneState.held = 'sideways'; e.win.screen.orientation = e.screenOrientation; e.screenOrientation.type = 'landscape-primary';
        for (let i = 0; i < 12; i++) { e.win.innerWidth = 700 + i * 10; e.win.fire('resize'); e.advance(100); }
        expect('a burst of twelve resizes, 100 ms apart: nothing yet while they go on', e.now + ' ' + JSON.stringify(e.gameCalls), '16:9 default []');
        e.advance(199);
        expect('... 199 ms after the last: still nothing', JSON.stringify(e.gameCalls), '[]');
        e.advance(1);
        expect('... 300 ms after the last: one switch', e.now + ' ' + JSON.stringify(e.gameCalls) + ' ' + e.events.filter((x) => x === 'relayout').length, '20:9 default [4] 1');
        e.advance(5000);
        expect('... and no other', JSON.stringify(e.gameCalls), '[4]');
    }
    {   // a change of the height alone (the address bar) does not count: no switch, even where a pick would give another shape
        const e = SHELL({ phone: 'android', held: 'sideways' });
        e.advance(10);
        e.win.innerHeight = 330; e.win.fire('resize'); e.visual.fire('resize');
        e.advance(300);
        expect('a change of height alone: nothing is switched', e.now + ' ' + JSON.stringify(e.gameCalls) + ' ' + e.events.length, '20:9 default [] 0');
        // ... not even when Automatic would now give another shape (a screen that changed under it): the pick is made only for a change of the width, the way or fullscreen
        e.win.screen.width = 1920; e.win.screen.height = 1080;
        e.win.innerHeight = 500; e.win.fire('resize');
        e.advance(300);
        expect('a change of height alone, the screen changed too: still no new pick', JSON.stringify(e.gameCalls), '[]');
        e.win.innerWidth = 1000; e.win.fire('resize');
        e.advance(300);
        expect('... and a change of the width picks again', e.now + ' ' + JSON.stringify(e.gameCalls), '16:9 default [3]');
    }
    {   // the listeners and the wait belong to the page: one of each, none for events that are not in the list
        const e = SHELL({ phone: 'android', held: 'sideways' });
        expect('the page listens to resize and orientationchange of the window', [e.win.count('resize'), e.win.count('orientationchange')], [1, 1]);
        expect('... to the change of screen.orientation, the two fullscreen events and the visual viewport\'s resize', [e.screenOrientation.count('change'), e.doc.count('fullscreenchange'), e.doc.count('webkitfullscreenchange'), e.visual.count('resize')], [1, 1, 1, 1]);
        expect('the wait is 300 ms', AntsShapes.WAIT_MS, 300);
    }
    {   // a desktop (a landscape screen) with a narrow window stays where it is
        const e = SHELL({ screen: { width: 1920, height: 1080 }, orientationType: 'landscape-primary', inner: [1000, 800] });
        expect('a landscape desktop: 16:9', e.now, '16:9 default');
        e.win.innerWidth = 420; e.win.innerHeight = 900; e.win.fire('resize'); e.advance(300);
        expect('... made narrow and tall: the same', e.now + ' ' + JSON.stringify(e.gameCalls), '16:9 default []');
        const ultra = SHELL({ screen: { width: 3440, height: 1440 }, orientationType: 'landscape-primary', inner: [800, 700] });
        expect('a landscape ultrawide keeps its 21:9 whatever the window', ultra.now, '21:9 default');
    }

    // ---- a hand pick wins, for the way it was made in ----
    for (const phone of ['android', 'iphone']) {
        const what = phone + ', a pick of 4:3 made sideways: ';
        const e = SHELL({ phone, held: 'sideways', stored: { [KEY]: P('4:3', 'sideways') } });
        expect(what + 'at load', e.now, '4:3 remembered');
        e.turn('sideways'); e.advance(300);
        expect(what + 'every event leaves it', e.now + ' ' + JSON.stringify(e.gameCalls), '4:3 remembered []');
        e.win.innerWidth = 600; e.win.fire('resize'); e.doc.fire('fullscreenchange'); e.advance(300);
        expect(what + 'a resize and fullscreen leave it', e.now, '4:3 remembered');
        e.turn('upright'); e.advance(300);
        expect(what + 'held upright it does not hold: Automatic (16:9 until the tall shapes)', e.now, '16:9 default');
        e.turn('sideways'); e.advance(300);
        expect(what + 'held sideways again it is back, from the storage', e.now, '4:3 remembered');
        expect(what + 'nothing was written or removed', JSON.stringify(e.storage.writes) + JSON.stringify(e.storage.removed) + JSON.stringify(e.addressed), '[][][]');
        const up = SHELL({ phone, held: 'upright', stored: { [KEY]: P('3:2', 'upright') } });
        expect(what + 'an upright pick of 3:2 holds upright', up.now, '3:2 remembered');
        up.turn('sideways'); up.advance(300);
        expect(what + '... and not sideways: Automatic', up.now, '20:9 default');
    }
    {   // both picks at once
        const e = SHELL({ phone: 'iphone', held: 'upright', stored: { [KEY]: JSON.stringify({ sideways: '21:9', upright: '3:2' }) } });
        expect('a pick for each way: upright', e.now, '3:2 remembered');
        e.turn('sideways'); e.advance(300);
        expect('... sideways', e.now, '21:9 remembered');
    }
    {   // a pick made in the page is stored for the way it was made in, and the address has it; the other way goes back to Automatic and the address loses it
        const e = SHELL({ phone: 'iphone', held: 'sideways' });
        e.run.pick('4:3');
        expect('a pick sideways is stored under its way', JSON.stringify(e.storage.writes), JSON.stringify([[KEY, P('4:3', 'sideways')]]));
        expect('... shown, and in the address (a pick is a pin)', e.now + ' ' + e.win.location.search + ' ' + !!e.run.own(), '4:3 remembered ?aspect=4:3 true');
        e.turn('upright'); e.advance(300);
        expect('turned upright the pick does not hold: Automatic, and the address loses what the page wrote', e.now + ' [' + e.win.location.search + '] ' + !!e.run.own(), '16:9 default [] false');
        e.run.pick('3:2');
        expect('a pick upright is added to the sideways one', e.storage.data[KEY], JSON.stringify({ sideways: '4:3', upright: '3:2' }));
        e.turn('sideways'); e.advance(300);
        expect('turned sideways: the sideways pick is back', e.now + ' [' + e.win.location.search + ']', '4:3 remembered []');
        expect('only picks wrote the key', e.storage.writes.length, 2);
    }
    {   // picking the shape that Automatic shows pins it (it is a choice now): stored, in the address, and no event changes it
        const e = SHELL({ phone: 'android', held: 'sideways' });
        e.run.pick('20:9');
        expect('picking the shape that is shown pins it', e.now + ' ' + JSON.stringify(e.storage.writes) + ' ' + e.win.location.search, '20:9 remembered ' + JSON.stringify([[KEY, P('20:9')]]) + ' ?aspect=20:9');
        e.turn('sideways'); e.advance(300);
        e.win.screen.width = 1920; e.win.screen.height = 1080; e.win.innerWidth = 900; e.win.fire('resize'); e.advance(300);
        expect('... and a change of the screen does not move it', e.now, '20:9 remembered');
    }
    {   // ?aspect= wins for the tab, for both ways, and is never stored; an address that is no shape is ignored
        const e = SHELL({ phone: 'iphone', held: 'sideways', search: '?aspect=32:9' });
        expect('the address names 32:9', e.now, '32:9 address');
        e.turn('upright'); e.advance(300);
        e.turn('sideways'); e.advance(300);
        e.win.innerWidth = 500; e.win.fire('resize'); e.advance(300);
        expect('every event leaves the address\'s shape', e.now + ' ' + JSON.stringify(e.gameCalls), '32:9 address []');
        expect('... and nothing was written', JSON.stringify(e.storage.writes) + JSON.stringify(e.addressed), '[][]');
        const over = SHELL({ phone: 'iphone', held: 'sideways', search: '?aspect=32:9', stored: { [KEY]: P('4:3') } });
        expect('the address beats a stored pick', over.now, '32:9 address');
        const own = SHELL({ phone: 'iphone', held: 'sideways', search: '?aspect=21:9', stored: { [KEY]: P('21:9') } });
        expect('an address that names the pick of this way (START of the front page, a reload after a pick) is that pick', own.now, '21:9 remembered');
        own.turn('upright'); own.advance(300);
        expect('... so a turn follows the rule of the picks: Automatic upright, and the address loses the shape', own.now + ' [' + own.win.location.search + ']', '16:9 default []');
        const junk = SHELL({ phone: 'iphone', held: 'sideways', search: '?aspect=wide' });
        expect('an address that is no shape: Automatic, and the events work', junk.now, '20:9 default');
    }

    // ---- the picks of the earlier key are forgotten once ----
    {
        const old = SHELL({ phone: 'iphone', held: 'sideways', stored: { 'ants.aspect.v2': '4:3', 'ants.aspect': '3:2' } });
        expect('picks under the earlier keys are not read: Automatic', old.now, '20:9 default');
        expect('... and nothing is written about them', JSON.stringify(old.storage.writes) + JSON.stringify(old.storage.removed), '[][]');
        const plain = SHELL({ phone: 'iphone', held: 'sideways', stored: { [KEY]: '4:3' } });
        expect('a bare shape under the new key is no pick (a pick names its way)', plain.now, '20:9 default');
        const bad = SHELL({ phone: 'iphone', held: 'sideways', stored: { [KEY]: JSON.stringify({ sideways: '5:4', upright: 'x' }) } });
        expect('a pick that names no shape of the table is no pick', bad.now, '20:9 default');
    }

    // ---- the Automatic entry ----
    {
        const e = SHELL({ phone: 'iphone', held: 'sideways' });
        const picker = e.pickers[0];
        expect('the Picture window\'s list has Automatic, which says what it gives now', [typeof picker.opts.automatic, typeof picker.opts.automatic === 'function' ? picker.opts.automatic() : null, picker.opts.auto, picker.shown], ['function', '20:9', true, '20:9']);
        e.run.pick('16:10');
        expect('a pick leaves Automatic: the list shows the shape and not Automatic', [picker.shown, picker.auto], ['16:10', false]);
        e.run.pick(AntsShapes.AUTO);
        expect('choosing Automatic: picks at once', e.now + ' ' + JSON.stringify(e.gameCalls), '20:9 default [2,4]');
        expect('... the list shows Automatic again', [picker.shown, picker.auto], ['20:9', true]);
        expect('... the stored pick and the address are gone', JSON.stringify(e.storage.data) + ' [' + e.win.location.search + '] ' + !!e.run.own(), '{} [] false');
        e.turn('upright'); e.advance(300);
        expect('... and a turn is Automatic\'s again', e.now + ' ' + picker.shown + ' ' + picker.auto, '16:9 default 16:9 true');
    }
    {   // Automatic clears the pick of the way held now; the other way's pick stays
        const e = SHELL({ phone: 'iphone', held: 'sideways', stored: { [KEY]: JSON.stringify({ sideways: '21:9', upright: '3:2' }) } });
        e.run.pick(AntsShapes.AUTO);
        expect('Automatic sideways forgets the sideways pick only', e.storage.data[KEY], JSON.stringify({ upright: '3:2' }));
        e.turn('upright'); e.advance(300);
        expect('... the upright pick still holds', e.now, '3:2 remembered');
    }
    {   // whoever watches the shape (the settings menu's picker) is told at each settling, also when the shape stays and only its source changes, and no more once it let go
        const e = SHELL({ phone: 'iphone', held: 'sideways', stored: { [KEY]: P('20:9') } });
        const told = [];
        const let_go = e.run.onShape(() => told.push(e.run.aspect() + ' ' + e.run.source()));
        e.run.pick(AntsShapes.AUTO);
        expect('Automatic chosen over a pick that has the same shape: the watcher is told, with the new source', told, ['20:9 default']);
        e.turn('upright'); e.advance(300);
        expect('a turn: the watcher is told the new shape', told, ['20:9 default', '16:9 default']);
        let_go();
        e.turn('sideways'); e.advance(300);
        expect('a watcher that let go hears nothing more', told.length, 2);
    }
    {   // ... and held upright it clears the upright pick only
        const e = SHELL({ phone: 'iphone', held: 'upright', stored: { [KEY]: JSON.stringify({ sideways: '21:9', upright: '3:2' }) } });
        e.run.pick(AntsShapes.AUTO);
        expect('Automatic upright forgets the upright pick only', e.storage.data[KEY], JSON.stringify({ sideways: '21:9' }));
    }
    {   // Automatic over a shape that a link named: the address's shape is dropped and the events work again
        const e = SHELL({ phone: 'iphone', held: 'sideways', search: '?aspect=32:9&room=abc' });
        e.run.pick(AntsShapes.AUTO);
        expect('Automatic over a link\'s shape: Automatic, and the rest of the address stays', e.now + ' ' + e.win.location.search, '20:9 default ?room=abc');
        e.turn('upright'); e.advance(300);
        expect('... and the events switch again', e.now, '16:9 default');
        expect('Automatic wrote no key', JSON.stringify(e.storage.writes), '[]');
    }
    {   // a browser that refuses its storage still has Automatic
        const e = SHELL({ phone: 'iphone', held: 'sideways' });
        e.win.localStorage = { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); }, removeItem() { throw new Error('blocked'); } };
        e.turn('upright'); e.advance(300);
        expect('without storage the turn is Automatic\'s', e.now, '16:9 default');
        e.run.pick(AntsShapes.AUTO);
        expect('... and so is the Automatic entry', e.now, '16:9 default');
    }

    // ---- spectators follow the same rule ----
    for (const search of ['?replay=a', '?live=a']) {
        const e = SHELL({ phone: 'iphone', held: 'upright', search });
        expect(search + ': Automatic at load', e.now, '16:9 default');
        e.turn('sideways'); e.advance(300);
        expect(search + ': a turn is Automatic\'s', e.now, '20:9 default');
        const hand = SHELL({ phone: 'iphone', held: 'sideways', search, stored: { [KEY]: P('21:9') } });
        e.run.pick('4:3');
        expect(search + ': a hand pick holds', [hand.now, e.now], ['21:9 remembered', '4:3 remembered']);
        const pic = e.pickers[0];
        expect(search + ': the Picture window\'s list has the Automatic entry', typeof pic.opts.automatic, 'function');
    }

    // ---- AntsShapes on its own: the way, the shape and the picks ----
    {
        const S = AntsShapes;
        const w = (extra) => Object.assign({ screen: { width: 390, height: 844 } }, extra);
        expect('the way: screen.orientation first', [S.wayOf(w({ screen: { width: 390, height: 844, orientation: { type: 'landscape-secondary' } }, orientation: 0 })), S.wayOf(w({ screen: { width: 844, height: 390, orientation: { type: 'portrait-secondary' } }, orientation: 90 }))], ['sideways', 'upright']);
        expect('the way: window.orientation, then the media query, then sideways', [S.wayOf(w({ orientation: 90 })), S.wayOf(w({ orientation: -90 })), S.wayOf(w({ orientation: 0 })), S.wayOf(w({ orientation: 180 })), S.wayOf(w({ matchMedia: () => ({ matches: false }) })), S.wayOf(w({ matchMedia: () => ({ matches: true }) })), S.wayOf(w()), S.wayOf(null)], ['sideways', 'sideways', 'upright', 'upright', 'upright', 'sideways', 'sideways', 'sideways']);
        expect('the way: an orientation that is no word is passed over', S.wayOf(w({ screen: { width: 390, height: 844, orientation: { type: 'sideways' } }, orientation: 0 })), 'upright');
        expect('the way never reads the screen\'s numbers', [S.wayOf({ screen: { width: 100, height: 900, orientation: { type: 'landscape-primary' } } }), S.wayOf({ screen: { width: 900, height: 100, orientation: { type: 'portrait-primary' } } })], ['sideways', 'upright']);
        const L = { screen: { width: 844, height: 390, orientation: { type: 'landscape-primary' } } };
        const L2 = { screen: { width: 390, height: 844, orientation: { type: 'landscape-primary' } } };
        expect('the screen\'s sides are sorted: 844 x 390 and 390 x 844 give the same', [S.automatic(L), S.automatic(L2)], ['20:9', '20:9']);
        expect('Classic 4:3 is a row like the others', S.automatic({ screen: { width: 1024, height: 768, orientation: { type: 'landscape-primary' } } }), '4:3');
        expect('a tie goes to the narrower (the middle of 16:9 and 20:9 is 1.9876)', [S.automatic({ screen: { width: 1987, height: 1000, orientation: { type: 'landscape-primary' } } }), S.automatic({ screen: { width: 1988, height: 1000, orientation: { type: 'landscape-primary' } } })], ['16:9', '20:9']);
        expect('no screen, or a size that is none: 16:9', [S.automatic({}), S.automatic(null), S.automatic({ screen: { width: 0, height: 0 } }), S.automatic({ screen: { width: NaN, height: 5 } })], ['16:9', '16:9', '16:9', '16:9']);
        expect('the picks: read, written, removed', [S.pickFor(P('4:3'), 'sideways'), S.pickFor(P('4:3'), 'upright'), S.pickFor('4:3', 'sideways'), S.pickFor(null, 'sideways'), S.pickFor('{', 'sideways'), S.withPick('', 'upright', '3:2'), S.withPick(P('4:3'), 'upright', '3:2'), S.withPick(P('4:3'), 'sideways', null), S.withPick(null, 'sideways', null)],
               ['4:3', null, null, null, null, '{"upright":"3:2"}', '{"sideways":"4:3","upright":"3:2"}', '', '']);
        expect('a pick that is no shape is dropped, and a name of the object\'s own is no shape', [S.pickFor(JSON.stringify({ sideways: 'toString' }), 'sideways'), S.pickFor(JSON.stringify({ sideways: '4:3 ' }), 'sideways')], [null, null]);
        // watch on its own: stop() takes every listener off and the timer away
        const t = { win: target(), so: target(), doc: target(), vv: target(), timers: 0 };
        const fake = Object.assign(t.win, { document: t.doc, visualViewport: t.vv, screen: { orientation: t.so, width: 1, height: 1 }, innerWidth: 500, setTimeout() { t.timers++; return 7; }, clearTimeout() { t.timers--; } });
        let calls = 0;
        const handle = S.watch(fake, () => { calls++; });
        fake.fire('resize');
        handle.stop();
        expect('stop() takes the listeners and the timer away', [fake.count('resize'), fake.count('orientationchange'), t.so.count('change'), t.doc.count('fullscreenchange'), t.doc.count('webkitfullscreenchange'), t.vv.count('resize'), t.timers, calls], [0, 0, 0, 0, 0, 0, 0, 0]);
        const bare = S.watch({ innerWidth: 1 }, () => {});
        expect('a window with nothing to listen to is watched without breakage', typeof bare.stop, 'function');
        bare.stop();
    }
} catch (e) {
    console.log('FAIL ' + (e && e.stack || e));
    failed++;
}
if (failed === 0) console.log('web auto shape: two phones x three ways of telling the way, six events, the wait, a burst, a change of height alone, the picks of each way, the earlier keys, the Automatic entry and spectators, 0 failures');
process.exit(failed === 0 ? 0 : 1);

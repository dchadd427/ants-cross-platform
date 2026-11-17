// Runs the page's OWN code for the hotkey buttons (web/touch_keys.js, AntsTouchKeys.forPage, which web/shell.html calls), without a browser, with fakes for the page and a model of the game's
// answers (Module._ants_match_running, _ants_menu_get 100 / 101, _ants_probe 5 / 50 / 60), and checks:
//   * the buttons: All ants, Stop, Home, Next, None, Shift, More in that order, real buttons with the page's look and a name for a screen reader; More opens a list (Previous ant, Health marks, Quick help,
//     Quick chat 1 - 4) that closes after a choice, on a tap elsewhere and when the buttons hide;
//   * a press is exactly the key events of a keyboard on the game's canvas, in a keyboard's order (a chord: Control down, the letter down and up, Control up; a single key: down, up), never a repeat,
//     and nothing else; one click is one key (nothing listens to a finger that rests);
//   * the buttons show only in a match that this player plays on a touch screen, and are hidden, with nothing sent by a press, in every other case (the setting off, a mouse, the game not ready, a
//     recording or a live match, no match, the room or the results, a dialog or a vote, the countdown card, the settings panel, the chat's phone keyboard); a press asks again first;
//   * None is dimmed (aria-disabled, no key) while the game says that nothing is picked, and live when the game does not say;
//   * the remembered setting (ants.touchkeys: only "off" is off; a browser that cannot store still works) and the row's functions;
//   * where they stand: classes on the stage (upright or sideways, in the page or fullscreen, in the page's free margin or one column of five inside the stage, a narrow stage), the room kept for the
//     whole match (only the buttons hide for a dialog, the settings panel, the chat field: the classes and sizes stay), and the page's layout is asked for only when it changes;
//   * a button never sends a key that is physically down (Control held: the letter alone; the key itself held: nothing), and the tap that closes the list is the list's alone;
//   * the mouse events that follow a touch stop at the buttons, and the touch events are not listened to (the window's capture listeners hear the fingers of the picture, for Shift);
//   * Shift: a tap lights it, a second tap puts it out, a press of 450 ms locks it, with no key of its own; with a latch or a lock a finger on the picture puts a keyboard's Shift down before the game sees it and up
//     120 ms after the last finger left; a tap and a drag of one finger use a latch up (a hold, a lingering finger and two fingers do not: the game's own tap, hold and slop), a lock stays; a finger within the
//     120 ms keeps the key down; a used latch lets go before the next finger; Shift is let go at once by a blur, a hidden page, a touchcancel, the buttons hiding and the setting off; a keyboard's own Shift is
//     never pressed again; a chord sent while Shift is down says so (shiftKey);
// tests/scripts/test_web_touch_keys.py runs this with node (the quick tier). usage: node web_touch_keys_check.js web/touch_keys.js     (exit 0: every check holds; every failure is printed)
'use strict';

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
if (!modulePath) { console.log('usage: web_touch_keys_check.js touch_keys.js'); process.exit(2); }
const T = require(require('path').resolve(modulePath));

// ---- fakes
let barHeightFn = null;          // (the bar's height: a rail inside the safe areas is as high as the stage minus them)
function element(tag, id) {
    const el = {
        tag, id: id || '', className: '', children: [], parent: null, attrs: {}, listeners: [], hidden: false, textContent: '', type: '', style: { props: {}, setProperty(k, v) { el.style.props[k] = v; } },
        stopped: [], blurred: 0, dispatched: [], clientHeight: 300, clientWidth: 400,
        get offsetHeight() { return el.id === 'touch-keys' && barHeightFn ? barHeightFn() : 48; },
        getBoundingClientRect() { return { left: el.left === undefined ? 200 : el.left, top: el.top === undefined ? 60 : el.top }; },
        setAttribute(k, v) { el.attrs[k] = String(v); }, getAttribute(k) { return k in el.attrs ? el.attrs[k] : null; },
        appendChild(c) { if (c.parent) c.parent.children.splice(c.parent.children.indexOf(c), 1); c.parent = el; el.children.push(c); return c; },
        insertBefore(c, ref) { if (c.parent) c.parent.children.splice(c.parent.children.indexOf(c), 1); c.parent = el; const at = ref ? el.children.indexOf(ref) : -1; if (at < 0) el.children.push(c); else el.children.splice(at, 0, c); return c; },
        addEventListener(type, fn, cap) { el.listeners.push({ type, fn, cap: !!cap }); },
        contains(t) { for (let n = t; n; n = n.parent) if (n === el) return true; return false; },
        blur() { el.blurred++; },
        dispatchEvent(e) { el.dispatched.push(e); return true; },
        fire(type, extra) { const e = Object.assign({ type, target: el, stopPropagation() { el.stopped.push(type); }, preventDefault() { e.prevented = true; } }, extra || {}); el.listeners.forEach((l) => { if (l.type === type) l.fn(e); }); return e; },
        click() { return el.fire('click'); },
    };
    const names = new Set();
    el.classList = { toggle(n, on) { if (on === undefined) on = !names.has(n); if (on) names.add(n); else names.delete(n); return on; }, contains: (n) => names.has(n), list: () => Array.from(names).sort() };
    return el;
}

function page(opts) {
    opts = opts || {};
    const g = { running: 1, where: 0, needs: 0, countdown: -1, modal: 0, picked: 0, probes: true };
    const stage = element('div', 'game-stage'), canvas = element('canvas', 'canvas'), body = element('body');
    const byId = {};
    const calls = { relayout: 0, intervals: [] };
    const doc = {
        createElement: (tag) => element(tag), body, activeElement: body, listeners: [],
        getElementById: (id) => byId[id] || null,
        addEventListener(type, fn, cap) { doc.listeners.push({ type, fn, cap: !!cap }); },
    };
    class KE {
        constructor(type, init) { this.type = type; Object.assign(this, init); if (opts.noLegacy) { delete this.keyCode; delete this.which; } }
    }
    const insets = { left: 0, top: 0, bottom: 0 };
    const win = { innerWidth: 390, innerHeight: 844, pageYOffset: 0, KeyboardEvent: KE, listeners: [], addEventListener(type, fn, cap) { const o = cap && typeof cap === 'object' ? cap : { capture: !!cap }; win.listeners.push({ type, fn, cap: !!o.capture, passive: !!o.passive }); }, getComputedStyle: () => ({ paddingLeft: insets.left + 'px', paddingBottom: insets.bottom + 'px' }) };
    barHeightFn = () => (stage.classList.contains('tk-side') && stage.classList.contains('tk-full') ? stage.clientHeight - insets.top - insets.bottom : 48);
    const state = { setting: opts.setting === undefined ? null : opts.setting, writes: [], touch: opts.touch !== false, ready: opts.ready !== false, watching: !!opts.watching, fullscreen: false };
    const store = opts.noStore ? () => { throw new Error('blocked'); } : () => ({ getItem: () => state.setting, setItem: (k, v) => { state.writes.push([k, v]); state.setting = v; } });
    const clock = { t: 1000, timers: [], next: 1 };
    const p = { g, stage, canvas, doc, win, state, calls, byId, body, insets, clock };
    p.start = () => {
        p.keys = T.forPage({
            doc, win, stage, canvas, store,
            touch: () => state.touch, ready: () => state.ready, watching: () => state.watching, running: () => g.running === 1,
            ask: (n) => { if (!g.probes) return -1; return n === 5 ? g.modal : n === 50 ? g.picked : n === 60 ? g.countdown : -1; },
            menu: (n) => (n === 100 ? g.where : n === 101 ? g.needs : -1),
            isFullscreen: () => state.fullscreen, relayout: () => { calls.relayout++; },
            every: (fn, ms) => { calls.intervals.push({ fn, ms }); return calls.intervals.length; },
            now: () => clock.t, after: (fn, ms) => { const h = clock.next++; clock.timers.push({ h, at: clock.t + ms, fn }); return h; }, cancel: (h) => { clock.timers = clock.timers.filter((x) => x.h !== h); },
        });
        p.bar = p.keys.bar;
        p.btn = (id) => p.bar.children.concat(p.bar.children[p.bar.children.length - 1].children).filter((c) => c.attrs['data-key'] === id)[0];
        p.moreBtn = p.bar.children.filter((c) => c.className.indexOf('tk-morebtn') >= 0)[0];
        p.more = p.bar.children[p.bar.children.length - 1];
        return p;
    };
    p.poll = () => calls.intervals.forEach((i) => i.fn());
    // a key of the real keyboard (trusted) reaching the window's capture listeners; a page-made one is not
    p.physical = (type, code, key, trusted) => { const e = { type, code, key: key || code, isTrusted: trusted !== false }; win.listeners.forEach((l) => { if (l.type === type) l.fn(e); }); return e; };
    // a real key's path through the phases: the window's capture listeners, the document's capture listeners (the settings panel's guard may stop the event there), and only when it was not stopped the window's bubble listeners
    p.path = (type, code, key) => {
        const e = { type, code, key: key || code, isTrusted: true, stopped: false, stopPropagation() { e.stopped = true; }, preventDefault() { /* nothing */ } };
        win.listeners.forEach((l) => { if (l.type === type && l.cap) l.fn(e); });
        doc.listeners.forEach((l) => { if (l.type === type && l.cap) l.fn(e); });
        if (!e.stopped) win.listeners.forEach((l) => { if (l.type === type && !l.cap) l.fn(e); });
        return e;
    };
    p.blurWindow = () => win.listeners.forEach((l) => { if (l.type === 'blur') l.fn({ type: 'blur' }); });
    p.pageHide = () => win.listeners.forEach((l) => { if (l.type === 'pagehide') l.fn({ type: 'pagehide' }); });
    p.visibility = (hidden) => { doc.hidden = hidden; doc.listeners.forEach((l) => { if (l.type === 'visibilitychange') l.fn({ type: 'visibilitychange' }); }); };
    // time passes: the timers that fall due run, in order, at their own moments
    p.advance = (ms) => {
        const end = clock.t + ms;
        for (;;) {
            const due = clock.timers.filter((x) => x.at <= end).sort((a, b) => a.at - b.at)[0];
            if (!due) break;
            clock.timers = clock.timers.filter((x) => x !== due);
            clock.t = due.at;
            due.fn();
        }
        clock.t = end;
    };
    // what SDL's key state says about Shift now (a model: the state changes when the key event arrives; the game reads it when it handles the finger's lift, a frame after the lift)
    p.sdlShift = () => { const k = canvas.dispatched.filter((e) => e.code === 'ShiftLeft'); return k.length > 0 && k[k.length - 1].type === 'keydown'; };
    // the fingers: the browser's list of touches, the window's capture listeners first (the page), then the game's own handler on the canvas (SDL), which notes what its key state says
    p.live = [];
    p.seen = [];
    p.touchEvent = (type, changed) => {
        const e = { type, changedTouches: changed, touches: p.live.slice(), isTrusted: true };
        win.listeners.forEach((l) => { if (l.type === type && l.cap) l.fn(e); });
        if (type === 'touchstart' || type === 'touchend') p.seen.push({ type, shift: p.sdlShift() });
        return e;
    };
    p.down = (id, x, y, target) => { const t = { identifier: id, target: target || canvas, clientX: x, clientY: y }; p.live.push(t); return p.touchEvent('touchstart', [t]); };
    p.move = (id, x, y) => { const t = p.live.filter((f) => f.identifier === id)[0]; t.clientX = x; t.clientY = y; return p.touchEvent('touchmove', [t]); };
    p.up = (id) => { const t = p.live.filter((f) => f.identifier === id)[0]; p.live = p.live.filter((f) => f !== t); return p.touchEvent('touchend', [t]); };
    p.cancelTouch = (id) => { const t = p.live.filter((f) => f.identifier === id)[0]; p.live = p.live.filter((f) => f !== t); return p.touchEvent('touchcancel', [t]); };
    // the Shift button's own pointer events and click
    p.shiftBtn = () => p.btn('shift');
    p.pointer = (type) => p.shiftBtn().fire(type);
    p.shiftSent = () => canvas.dispatched.filter((e) => e.code === 'ShiftLeft').map((e) => [e.type, e.key, e.keyCode, e.which, e.location, e.shiftKey, e.ctrlKey, e.repeat]);
    p.sent = () => canvas.dispatched.map((e) => [e.type, e.key, e.code, e.keyCode, e.which, e.ctrlKey, e.repeat]);
    p.clear = () => { canvas.dispatched.length = 0; };
    return p;
}
const CTRL = ['Control', 'ControlLeft', 17, 17];
const chord = (key, code, vk) => [['keydown'].concat(CTRL, [true, false]), ['keydown', key, code, vk, vk, true, false], ['keyup', key, code, vk, vk, true, false], ['keyup'].concat(CTRL, [false, false])];
const single = (key, code, vk) => [['keydown', key, code, vk, vk, false, false], ['keyup', key, code, vk, vk, false, false]];
const EXPECT = {
    all: chord('a', 'KeyA', 65), stop: chord('s', 'KeyS', 83), home: chord('h', 'KeyH', 72), next: chord('n', 'KeyN', 78), none: single('Escape', 'Escape', 27),
    prev: chord('p', 'KeyP', 80), health: chord('l', 'KeyL', 76), help: single('F1', 'F1', 112), chat1: single('F9', 'F9', 120), chat2: single('F10', 'F10', 121), chat3: single('F11', 'F11', 122), chat4: single('F12', 'F12', 123),
};
function shown(p) { return p.poll(), !p.bar.hidden && p.bar.style.display !== 'none' && !p.bar.classList.contains('tk-gone'); }
function pressAll(p) { ['all', 'stop', 'home', 'next', 'none'].forEach((id) => p.btn(id).click()); p.moreBtn.click(); ['prev', 'health', 'help', 'chat1', 'chat2', 'chat3', 'chat4'].forEach((id) => p.btn(id).click()); }

try {

    // ---- what is made
    {
        const p = page().start();
        expect('one group in the stage, named for a screen reader (beside the layer under its list and a probe of the safe area)', [p.stage.children.map((c) => c.className.split(' ')[0]), p.bar.id, p.bar.getAttribute('role'), p.bar.getAttribute('aria-label')], [['tk-shield', 'tk-probe', 'tk'], 'touch-keys', 'group', 'Game keys']);
        const main = p.bar.children.filter((c) => c.tag === 'button');
        expect('the buttons in order: All ants, Stop, Home, Next, None, Shift, More', main.map((b) => b.textContent), ['All ants', 'Stop', 'Home', 'Next', 'None', 'Shift', 'More']);
        expect('the list: Previous ant, Health marks, Quick help, Quick chat 1 - 4', p.more.children.map((b) => b.textContent), ['Previous ant', 'Health marks', 'Quick help', 'Quick chat 1', 'Quick chat 2', 'Quick chat 3', 'Quick chat 4']);
        const all = main.concat(p.more.children);
        expect('every one is a real button of the page\'s look with a name for a screen reader', all.every((b) => b.tag === 'button' && b.type === 'button' && /(^| )btn( |$)/.test(b.className) && b.getAttribute('aria-label')), true);
        expect('the names say what the key does', ['all', 'none', 'stop'].map((k) => p.btn(k).getAttribute('aria-label')), ['Select all ants', 'Select none', 'Stop the selected ants']);
        expect('a key button listens to its click and nothing else (a finger that rests sends nothing: one click, one key)', all.filter((b) => b.attrs['data-key'] !== 'shift').map((b) => b.listeners.map((l) => l.type).join()).filter((t) => t !== 'click').length, 0);
        expect('Shift sends no key by itself: it listens to its click and to the press that locks it (the pointer), and to no touch', Array.from(new Set(all.filter((b) => b.attrs['data-key'] === 'shift')[0].listeners.map((l) => l.type))).sort(), ['click', 'pointercancel', 'pointerdown', 'pointerleave', 'pointerup']);
        expect('the window hears the fingers of the picture in the capture phase and never stops them (a listener that cannot cancel)', p.win.listeners.filter((l) => /^touch/.test(l.type)).map((l) => l.type + (l.cap ? ':capture' : ':bubble') + (l.passive ? ':passive' : ':active')).sort(), ['touchcancel:capture:passive', 'touchend:capture:passive', 'touchmove:capture:passive', 'touchstart:capture:passive']);
        expect('the bar listens to the mouse events that follow a touch (to stop them), the contextmenu and Esc, and never to a touch event',
            Array.from(new Set(p.bar.listeners.map((l) => l.type))).sort(), ['contextmenu', 'dblclick', 'keydown', 'mousedown', 'mousemove', 'mouseout', 'mouseover', 'mouseup']);
        expect('the poll is 250 ms', p.calls.intervals.map((i) => i.ms), [250]);
        const loading = page({ ready: false }).start();
        expect('the list is hidden, and the bar too until the game runs', [p.more.hidden, loading.bar.hidden], [true, true]);
    }

    // ---- shown in a match of this player's on a touch screen, hidden in every other case, and a press sends nothing then
    {
        const p = page().start();
        expect('a match on a touch screen: shown', shown(p), true);
        const cases = [
            ['the setting is off', () => p.keys.set(false), () => p.keys.set(true)],
            ['a mouse is the primary pointer', () => { p.state.touch = false; }, () => { p.state.touch = true; }],
            ['the game is not ready', () => { p.state.ready = false; }, () => { p.state.ready = true; }],
            ['a recording or a live match is watched', () => { p.state.watching = true; }, () => { p.state.watching = false; }],
            ['no match is running', () => { p.g.running = 0; }, () => { p.g.running = 1; }],
            ['the room or the results (the game has no match screen)', () => { p.g.where = -1; }, () => { p.g.where = 0; }],
            ['a recording\'s screen', () => { p.g.where = 1; }, () => { p.g.where = 0; }],
            ['a dialog, a vote or a lost link waits for its answer', () => { p.g.needs = 1; }, () => { p.g.needs = 0; }],
            ['a dialog of the match is open', () => { p.g.modal = 1; }, () => { p.g.modal = 0; }],
            ['the countdown card is up', () => { p.g.countdown = 20; }, () => { p.g.countdown = -1; }],
            ['the settings panel is open', () => { p.byId['game-menu'] = element('div', 'game-menu'); }, () => { delete p.byId['game-menu']; }],
            ['the chat\'s phone keyboard has the focus', () => { p.doc.activeElement = element('input', 'chat-keys'); }, () => { p.doc.activeElement = p.body; }],
            ['the game does not answer (an old game)', () => { p.g.probes = false; }, () => { p.g.probes = true; }],
        ];
        cases.forEach((c) => {
            c[1]();
            expect('hidden: ' + c[0], shown(p), false);
            p.clear();
            pressAll(p);
            expect('...and a press of any button sends no key: ' + c[0], p.sent(), []);
            c[2]();
            expect('...and shown again when it is over: ' + c[0], shown(p), true);
        });
        // the game answers 'no screen' (-1) from the menu probe too, and a page without probes at all hides the buttons
        const q = page().start();
        q.g.where = -1;
        q.poll();
        expect('the menu probe says no match screen: hidden', q.bar.hidden, true);
    }

    // ---- a press asks again: the state changed since the last poll
    {
        const p = page().start();
        p.poll();
        p.byId['game-menu'] = element('div', 'game-menu');          // (the panel opened since the poll)
        p.btn('all').click();
        expect('the panel opened since the last poll: the press sends nothing and the bar hides', [p.sent(), p.bar.classList.contains('tk-gone')], [[], true]);
        delete p.byId['game-menu'];
        p.poll();
        p.g.needs = 1;                                                // (a dialog since the poll)
        p.btn('stop').click();
        expect('a dialog since the last poll: the press sends nothing', p.sent(), []);
        p.g.needs = 0;
        p.poll();
        p.g.where = -1;                                               // (the results since the poll)
        p.moreBtn.click();
        p.btn('help').click();
        expect('the results since the last poll: neither More nor a key of its list sends anything', [p.sent(), p.keys.isOpen()], [[], false]);
    }

    // ---- every button sends exactly its key, in a keyboard's order, on the canvas
    {
        Object.keys(EXPECT).forEach((id) => {
            const p = page().start();
            p.poll();
            if (EXPECT[id] !== EXPECT.all && ['prev', 'health', 'help', 'chat1', 'chat2', 'chat3', 'chat4'].indexOf(id) >= 0) p.moreBtn.click();
            if (id === 'none') p.g.picked = 2;
            p.poll();
            p.btn(id).click();
            expect(id + ': the events of a keyboard, in order, and nothing else', p.sent(), EXPECT[id]);
            expect(id + ': they bubble and can be cancelled (the settings panel\'s key guard on the document sees them, as a keyboard\'s)', p.canvas.dispatched.every((e) => e.bubbles === true && e.cancelable === true), true);
        });
        const p = page().start();
        p.poll();
        p.btn('all').click();
        p.btn('all').click();
        expect('a second press is a second key (eight events), never a repeat', [p.sent().length, p.sent().every((e) => e[6] === false)], [8, true]);
        expect('the press blurs the button (a hardware keyboard\'s Enter must not press it again)', p.btn('all').blurred >= 2, true);
        const w = page({ noLegacy: true }).start();
        w.poll();
        w.btn('home').click();
        expect('an event that the browser makes without keyCode and which still has them (the game\'s window system reads them)', w.sent().map((e) => [e[3], e[4]]), [[17, 17], [72, 72], [72, 72], [17, 17]]);
    }

    // ---- More
    {
        const p = page().start();
        p.poll();
        expect('the list is closed at first', [p.keys.isOpen(), p.more.hidden, p.moreBtn.getAttribute('aria-expanded')], [false, true, 'false']);
        p.moreBtn.click();
        expect('More opens the list and sends no key', [p.keys.isOpen(), p.more.hidden, p.moreBtn.getAttribute('aria-expanded'), p.sent()], [true, false, 'true', []]);
        p.moreBtn.click();
        expect('More again closes it', p.keys.isOpen(), false);
        p.moreBtn.click();
        p.btn('health').click();
        expect('a choice sends its key and closes the list', [p.sent(), p.keys.isOpen()], [EXPECT.health, false]);
        p.clear();
        p.moreBtn.click();
        expect('an open list has a layer under it that takes the taps elsewhere (hidden when it is closed)', [p.keys.shield.hidden, (p.keys.toggle(false), p.keys.shield.hidden), (p.keys.toggle(true), p.keys.shield.hidden)], [false, true, false]);
        p.keys.shield.fire('click');
        expect('a tap elsewhere closes it, and is the list\'s alone (the click stops at the layer, which stops the mouse events of the touch too, so the game and the page get nothing)', [p.keys.isOpen(), p.keys.shield.stopped.indexOf('click') >= 0, p.sent()], [false, true, []]);
        ['mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'dblclick'].forEach((t) => p.keys.shield.fire(t));
        expect('...the layer takes the mouse events that follow a touch', p.keys.shield.stopped.filter((t) => t !== 'click'), ['mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'dblclick']);
        expect('the layer\'s long press is no context menu', p.keys.shield.fire('contextmenu').prevented, true);
        p.moreBtn.click();
        expect('a tap inside the list (a button of it) does not close it before its own click', p.keys.isOpen(), true);
        p.g.running = 0;
        p.poll();
        expect('the list closes when the buttons hide', [p.keys.isOpen(), p.more.hidden], [false, true]);
        p.g.running = 1;
        p.poll();
        expect('...and it is closed when they come back', p.keys.isOpen(), false);
        p.moreBtn.click();
        const esc = p.bar.fire('keydown', { key: 'Escape' });
        expect('Esc on the buttons closes the list and is not the game\'s Esc: the page-made Esc is cancelled and stopped, so it reaches neither the page\'s other listeners nor the game', [p.keys.isOpen(), esc.prevented, p.bar.stopped.indexOf('keydown') >= 0, p.sent()], [false, true, true, []]);
        expect('...and more opened while the buttons are not usable opens nothing (the list does not show over a hidden bar)', (() => { p.g.where = -1; p.moreBtn.click(); const r = [p.keys.isOpen(), p.more.hidden]; p.g.where = 0; return r; })(), [false, true]);
    }

    // ---- None
    {
        const p = page().start();
        p.poll();
        expect('nothing picked: None is dimmed (aria-disabled)', [p.btn('none').getAttribute('aria-disabled'), p.btn('none').classList.contains('tk-dim')], ['true', true]);
        p.btn('none').click();
        expect('...and sends no key (an Esc would open the settings menu)', p.sent(), []);
        p.g.picked = 100003;
        p.poll();
        expect('something picked: live', [p.btn('none').getAttribute('aria-disabled'), p.btn('none').classList.contains('tk-dim')], ['false', false]);
        p.btn('none').click();
        expect('...and sends Esc', p.sent(), EXPECT.none);
        p.clear();
        p.g.picked = 0;
        p.poll();
        const r = page().start();
        r.g.picked = -1;
        r.poll();
        expect('a probe that says -1 (no answer) leaves None live', r.btn('none').getAttribute('aria-disabled'), 'false');
        r.btn('none').click();
        expect('...and it sends Esc', r.sent(), EXPECT.none);
        // a press asks the game again: the picking changed since the last poll (the buttons' look is the poll's, the key is the press's)
        const q = page().start();
        q.g.picked = 3;
        q.poll();
        q.g.picked = 0;                                           // (the ants died, or were deselected, after the poll: None still looks live)
        expect('None looks live from the last poll', q.btn('none').getAttribute('aria-disabled'), 'false');
        q.btn('none').click();
        expect('...but the press asks the game: nothing is picked now, no Esc (it would open the settings menu)', q.sent(), []);
        q.poll();
        q.g.picked = 2;                                           // (an ant was picked after the poll: None still looks dimmed)
        expect('None looks dimmed from the last poll', q.btn('none').getAttribute('aria-disabled'), 'true');
        q.btn('none').click();
        expect('...but the press asks the game: something is picked now, Esc', q.sent(), EXPECT.none);
    }

    // ---- the remembered setting
    {
        let p = page({ setting: 'off' }).start();
        expect('a stored off: hidden, and the row says off', [shown(p), p.keys.get()], [false, false]);
        p = page({ setting: 'on' }).start();
        expect('a stored on: shown', [shown(p), p.keys.get()], [true, true]);
        p = page({ setting: 'garbage' }).start();
        expect('anything but off is on', [shown(p), p.keys.get()], [true, true]);
        p = page().start();
        expect('nothing stored is on', [shown(p), p.keys.get()], [true, true]);
        p.keys.set(false);
        expect('turned off: stored as off under the key, hidden at once', [p.state.writes, p.bar.hidden, p.keys.get()], [[['ants.touchkeys', 'off']], true, false]);
        p.keys.set(true);
        expect('turned on: stored as on, shown at once', [p.state.writes[1], p.bar.hidden], [['ants.touchkeys', 'on'], false]);
        p = page({ noStore: true }).start();
        expect('a browser that cannot store: on, and a change does not throw', [shown(p), (() => { try { p.keys.set(false); return p.keys.get(); } catch (e) { return 'threw'; } })()], [true, false]);
        p = page().start();
        expect('the row is offered on a touch screen only', [p.keys.available(), (p.state.touch = false, p.keys.available())], [true, false]);
        expect('the key is ants.touchkeys', T.KEY, 'ants.touchkeys');
    }

    // ---- where they stand
    function sideways(p, left, top) { p.win.innerWidth = 844; p.win.innerHeight = 390; p.state.fullscreen = false; p.stage.clientHeight = 280; p.stage.left = left; p.stage.top = top; p.poll(); }
    {
        const cls = (p) => p.stage.classList.list();
        let p = page().start();
        p.poll();
        expect('upright in the page: a row (tk-up) under the picture', cls(p), ['tk-on', 'tk-page', 'tk-up']);
        expect('...the row takes room under the picture (its height and a gap) and the rail var is set', [p.stage.style.props['--tk-row'], p.stage.style.props['--tk-rail']], ['56px', '72px']);
        p.state.fullscreen = true;
        p.poll();
        expect('upright in fullscreen: a row in the stage', cls(p), ['tk-full', 'tk-on', 'tk-up']);
        p.win.innerWidth = 844; p.win.innerHeight = 390; p.stage.clientHeight = 390;
        p.poll();
        expect('sideways in fullscreen: a rail (tk-side) inside the stage, one column on a stage that is tall enough', [cls(p), p.stage.style.props['--tk-rail']], [['tk-full', 'tk-on', 'tk-side'], '72px']);
        p.stage.clientHeight = 280;
        p.poll();
        expect('sideways in fullscreen on a short stage: two columns', [cls(p), p.stage.style.props['--tk-rail']], [['tk-full', 'tk-on', 'tk-side', 'tk-two'], '138px']);
        p.stage.clientHeight = 390; p.insets.top = 30; p.insets.bottom = 60;
        p.poll();
        expect('...the rail stands inside the safe areas, so they count: seven targets do not fit in the 300 px that are left of 390 (two columns)', cls(p).indexOf('tk-two') >= 0, true);
        p.insets.top = 0; p.insets.bottom = 35;
        p.poll();
        expect('...355 px left is not enough for seven in one column (44 px each, six gaps of 6 and the rail\'s padding of 12: 356)', cls(p).indexOf('tk-two') >= 0, true);
        p.insets.bottom = 34;
        p.poll();
        expect('...and 356 px left is', cls(p).indexOf('tk-two') < 0, true);
        p.insets.bottom = 0;

        // sideways in the page: the rail in the free margin left of the stage (outside it: the picture keeps its size), one column
        sideways(p, 111, 60);
        expect('sideways in the page with 111 px of free margin: the rail stands in the margin (tk-rim), the stage keeps its size', [cls(p), p.stage.style.props['--tk-rail-h']], [['tk-on', 'tk-page', 'tk-rim', 'tk-side'], '330px']);
        expect('...330 px is room for six (not seven): six buttons in the bar, in order, with Next first in the list under More', [p.bar.children.filter((c) => c.tag === 'button').map((c) => c.attrs['data-key'] || 'more'), p.more.children[0].attrs['data-key']], [['all', 'stop', 'home', 'none', 'shift', 'more'], 'next']);
        sideways(p, 76, 60);
        expect('76 px of margin is enough (a rail of 60 px, 6 px of air and the page\'s 10 px frame)', cls(p).indexOf('tk-rim') >= 0, true);
        sideways(p, 75, 60);
        expect('75 px is not: the rail would lie under the frame (one column inside the stage)', cls(p).indexOf('tk-rim') < 0, true);
        sideways(p, 65, 60);
        expect('65 px: one column of six inside the stage (no tk-rim, never tk-two)', [cls(p), p.stage.style.props['--tk-rail']], [['tk-on', 'tk-page', 'tk-side'], '72px']);
        expect('...Next stands first in the list under More, and the bar has six buttons (Shift is one of them)', [p.more.children[0].attrs['data-key'], p.bar.children.filter((c) => c.tag === 'button').map((c) => c.attrs['data-key'] || 'more')], ['next', ['all', 'stop', 'home', 'none', 'shift', 'more']]);
        p.insets.left = 10;
        sideways(p, 86, 60);
        expect('the phone\'s left safe area counts in the margin (86 - 10 is 76: enough)', cls(p).indexOf('tk-rim') >= 0, true);
        sideways(p, 85, 60);
        expect('...85 - 10 is not', cls(p).indexOf('tk-rim') < 0, true);
        p.insets.left = 0;
        sideways(p, 111, 100);
        expect('in the margin but the window leaves only 290 px below the stage\'s top (under 294: six targets do not fit): five, Home and Next under More (Home first), still one column', [cls(p).indexOf('tk-rim') >= 0, cls(p).indexOf('tk-two') < 0, p.more.children.slice(0, 2).map((c) => c.attrs['data-key']), p.stage.style.props['--tk-rail-h']], [true, true, ['home', 'next'], '290px']);
        p.moreBtn.click();
        p.btn('next').click();
        expect('Next from the list: its key (Ctrl+N) and the list closes', [p.sent(), p.keys.isOpen()], [chord('n', 'KeyN', 78), false]);
        // the rail uses its real height (the rim rail has no padding): seven targets are 344 px, six are 294, five are 244
        const win = (w, h, left, top, stageH) => { sideways(p, left, top); p.win.innerWidth = w; p.win.innerHeight = h; p.stage.clientHeight = stageH; p.poll(); };
        const barCount = () => p.bar.children.filter((c) => c.tag === 'button').length;
        const listHead = () => p.more.children.slice(0, 2).map((c) => c.attrs['data-key']);
        const prop = (n) => p.stage.style.props[n];
        win(844, 400, 111, 56, 250);
        expect('344 px of room: all seven in the margin, nothing under More but its own list', [cls(p).indexOf('tk-rim') >= 0, barCount(), p.more.children[0].attrs['data-key'], prop('--tk-rail-h'), prop('--tk-btn-h')], [true, 7, 'prev', '344px', '44px']);
        win(844, 400, 111, 49, 250);
        expect('351 px: seven', [barCount(), prop('--tk-rail-h')], [7, '351px']);
        win(844, 400, 111, 57, 250);
        expect('343 px: six, Next under More, the targets still 44 px', [barCount(), listHead()[0], prop('--tk-btn-h')], [6, 'next', '44px']);
        win(740, 360, 111, 66, 250);
        expect('294 px of room (740 x 360): six in the margin, Next under More', [cls(p).indexOf('tk-rim') >= 0, barCount(), listHead()[0], prop('--tk-rail-h'), prop('--tk-btn-h')], [true, 6, 'next', '294px', '44px']);
        win(667, 375, 111, 74, 250);
        expect('301 px (667 x 375): six', [barCount(), prop('--tk-rail-h')], [6, '301px']);
        win(740, 360, 111, 67, 250);
        expect('293 px: five, Home and Next under More', [barCount(), listHead(), prop('--tk-btn-h')], [5, ['home', 'next'], '44px']);
        win(740, 360, 173, 67, 250);
        expect('293 px with 173 px of margin: two columns instead (seven buttons)', [cls(p).indexOf('tk-two') >= 0, barCount()], [true, 7]);
        p.insets.bottom = 20;
        win(740, 360, 111, 66, 250);
        expect('the phone\'s bottom edge is not room: 294 - 20 is five', [barCount(), prop('--tk-rail-h')], [5, '274px']);
        p.insets.bottom = 0;
        // a short window: More never stands below it
        win(800, 304, 173, 66, 194);
        expect('800 x 304 with 173 px of margin: two columns in the margin (seven buttons, All ants across both), as high as the window leaves', [cls(p).indexOf('tk-two') >= 0, cls(p).indexOf('tk-rim') >= 0, barCount(), prop('--tk-rail-h')], [true, true, 7, '238px']);
        win(732, 320, 163, 102, 174);
        expect('732 x 320 (the header wraps): 218 px: two columns', [cls(p).indexOf('tk-two') >= 0, barCount(), prop('--tk-rail-h')], [true, 7, '218px']);
        win(800, 304, 142, 66, 194);
        expect('142 px of margin is enough for two columns (126 px, 6 of air, the 10 px frame)', cls(p).indexOf('tk-two') >= 0, true);
        win(800, 304, 141, 66, 194);
        expect('141 px is not: five in one column, the targets 42 px so they fit the 238', [cls(p).indexOf('tk-two') < 0, cls(p).indexOf('tk-rim') >= 0, barCount(), prop('--tk-btn-h')], [true, true, 5, '42px']);
        win(640, 300, 101, 74, 190);
        expect('640 x 300 with 101 px of margin: five targets of 40 px in the 226', [cls(p).indexOf('tk-two') < 0, prop('--tk-btn-h'), prop('--tk-rail-h')], [true, '40px', '226px']);
        win(640, 260, 101, 74, 150);
        expect('the targets are never under 40 px', prop('--tk-btn-h'), '40px');
        win(568, 320, 40, 74, 217);
        expect('568 x 320 in a wide shape (no margin): the column of five (Home and Next under More) stands in the window, 246 px, targets of 42', [cls(p).indexOf('tk-rim') < 0, barCount(), prop('--tk-rail-h'), prop('--tk-btn-h')], [true, 5, '246px', '42px']);
        win(568, 320, 40, 74, 290);
        expect('...a stage that is high enough keeps its own height and the full targets', [prop('--tk-rail-h'), prop('--tk-btn-h')], ['290px', '44px']);
        win(700, 420, 40, 74, 300);
        expect('700 x 420 in a wide shape (no margin): 306 px is room for a column of six (Next under More)', [cls(p).indexOf('tk-rim') < 0, barCount(), listHead()[0], prop('--tk-rail-h'), prop('--tk-btn-h')], [true, 6, 'next', '306px', '44px']);
        // a page's column that folds gets Home and Next back in the bar in fullscreen (they stood under More)
        win(568, 320, 40, 74, 217);
        expect('Home and Next stand under More in the page\'s column of five', listHead(), ['home', 'next']);
        p.state.fullscreen = true; p.win.innerWidth = 844; p.win.innerHeight = 390; p.stage.clientHeight = 390;
        p.poll();
        expect('...and in fullscreen they are in the bar again (seven buttons, seven in the list)', [barCount(), p.more.children.length, p.btn('next').className.indexOf('tk-item') < 0, p.btn('home').className.indexOf('tk-item') < 0], [7, 7, true, true]);
        p.state.fullscreen = false;
        p.win.innerWidth = 390; p.win.innerHeight = 844;
        p.poll();
        expect('back upright: Home and Next are in the row again, in their places', [p.bar.children.filter((c) => c.tag === 'button').map((c) => c.attrs['data-key'] || 'more'), p.more.children.length, p.btn('next').className.indexOf('tk-item') < 0], [['all', 'stop', 'home', 'next', 'none', 'shift', 'more'], 7, true]);
        p.stage.clientWidth = 280;
        p.poll();
        expect('a narrow upright stage: All ants has a row of its own (tk-wrap), and under 294 the other six stand three to a row (tk-narrow)', [cls(p).indexOf('tk-wrap') >= 0, cls(p).indexOf('tk-narrow') >= 0], [true, true]);
        p.stage.clientWidth = 350;
        p.poll();
        expect('350 (a phone 390 wide): All ants still has a row of its own, the other six fit in one', [cls(p).indexOf('tk-wrap') >= 0, cls(p).indexOf('tk-narrow') >= 0], [true, false]);
        p.stage.clientWidth = 294;
        p.poll();
        expect('294 is the narrowest for six in a row (six targets of 44 px and five gaps)', cls(p).indexOf('tk-narrow') < 0, true);
        p.stage.clientWidth = 293;
        p.poll();
        expect('293 is not', cls(p).indexOf('tk-narrow') >= 0, true);
        p.stage.clientWidth = 363;
        p.poll();
        expect('363: one short of the width for All ants at twice a target and six more (364): its own row', cls(p).indexOf('tk-wrap') >= 0, true);
        p.stage.clientWidth = 364;
        p.poll();
        expect('364: all seven in one row', cls(p).indexOf('tk-wrap') < 0, true);
        p.stage.clientWidth = 400;
        p.g.running = 0;
        p.poll();
        expect('no match: no class on the stage (the room is given back)', cls(p), []);
        p = page().start();
        p.poll();
        const before = p.calls.relayout;
        p.poll();
        p.poll();
        expect('the page\'s layout is asked for when the placement changes and not on every poll', p.calls.relayout, before);
        p.win.innerWidth = 844; p.win.innerHeight = 390;
        p.poll();
        expect('...and is asked when it does', p.calls.relayout, before + 1);
        const n = p.calls.relayout;
        p.win.innerWidth = 390; p.win.innerHeight = 844;
        p.keys.refresh(true);
        expect('...but not by a refresh that the page\'s own layout made', p.calls.relayout, n);
    }

    // ---- the room is kept for the whole match: only the buttons hide for a while, so the picture never moves or changes size
    {
        const cls = (p) => p.stage.classList.list();
        const layouts = {
            'upright in the page': (p) => { p.win.innerWidth = 390; p.win.innerHeight = 844; p.state.fullscreen = false; p.poll(); },
            'upright in fullscreen': (p) => { p.win.innerWidth = 390; p.win.innerHeight = 844; p.state.fullscreen = true; p.poll(); },
            'sideways in the page (margin)': (p) => sideways(p, 111, 60),
            'sideways in the page (inside the stage)': (p) => sideways(p, 40, 60),
            'sideways in fullscreen': (p) => { p.win.innerWidth = 844; p.win.innerHeight = 390; p.state.fullscreen = true; p.stage.clientHeight = 390; p.poll(); },
        };
        const passing = [
            ['a dialog or a lost link waits for its answer', (p) => { p.g.needs = 1; }, (p) => { p.g.needs = 0; }],
            ['a dialog of the match is open', (p) => { p.g.modal = 1; }, (p) => { p.g.modal = 0; }],
            ['the countdown card is up', (p) => { p.g.countdown = 20; }, (p) => { p.g.countdown = -1; }],
            ['the settings panel is open', (p) => { p.byId['game-menu'] = element('div', 'game-menu'); }, (p) => { delete p.byId['game-menu']; }],
            ['the chat field has the focus', (p) => { p.doc.activeElement = element('input', 'chat-keys'); }, (p) => { p.doc.activeElement = p.body; }],
        ];
        const leaving = [
            ['the setting is off', (p) => p.keys.set(false), (p) => p.keys.set(true)],
            ['a mouse is the primary pointer', (p) => { p.state.touch = false; }, (p) => { p.state.touch = true; }],
            ['the game is not ready', (p) => { p.state.ready = false; }, (p) => { p.state.ready = true; }],
            ['a recording is watched', (p) => { p.state.watching = true; }, (p) => { p.state.watching = false; }],
            ['no match is running', (p) => { p.g.running = 0; }, (p) => { p.g.running = 1; }],
            ['the room or the results', (p) => { p.g.where = -1; }, (p) => { p.g.where = 0; }],
        ];
        Object.keys(layouts).forEach((name) => {
            const p = page().start();
            layouts[name](p);
            const snap = () => JSON.stringify([cls(p), p.stage.style.props]);
            const base = snap(), calls = p.calls.relayout;
            expect(name + ': the buttons are shown', shown(p), true);
            passing.forEach((c) => {
                c[1](p);
                p.poll();
                expect(name + ' / ' + c[0] + ': only the buttons hide (gone), the stage\'s classes and sizes do not change', [p.bar.hidden, p.bar.classList.contains('tk-gone'), snap() === base, p.calls.relayout], [false, true, true, calls]);
                expect(name + ' / ' + c[0] + ': ...the room is the stage\'s (the bar is still in the layout: it is not display: none)', p.bar.style.display, '');
                c[2](p);
                expect(name + ' / ' + c[0] + ': ...and they are back, nothing moved', [shown(p), snap() === base, p.calls.relayout], [true, true, calls]);
            });
            leaving.forEach((c) => {
                c[1](p);
                p.poll();
                expect(name + ' / ' + c[0] + ': the room is given back', [cls(p), p.bar.hidden], [[], true]);
                c[2](p);
                p.poll();
                expect(name + ' / ' + c[0] + ': ...and taken again when it is over', snap() === base, true);
            });
        });
        // the row's height is measured while the buttons are hidden for a while too (a window that changes then still gets the right room)
        const q = page().start();
        q.poll();
        q.g.needs = 1;
        q.poll();
        q.stage.clientWidth = 280;
        q.poll();
        expect('a narrow window while a dialog hides the buttons: the row\'s wrap is still counted', q.stage.classList.contains('tk-wrap'), true);
    }

    // ---- a button never sends a key that is physically down
    {
        const p = page().start();
        p.poll();
        p.physical('keydown', 'ControlLeft', 'Control');
        p.btn('all').click();
        expect('Control held on the keyboard: All ants is the letter alone, with ctrlKey and no Control of ours (down or up)', p.sent(), [['keydown', 'a', 'KeyA', 65, 65, true, false], ['keyup', 'a', 'KeyA', 65, 65, true, false]]);
        p.clear();
        p.physical('keydown', 'KeyS', 's');
        p.btn('stop').click();
        expect('...and Stop with the S key held sends nothing (a keyboard cannot press a key that is down)', p.sent(), []);
        p.physical('keyup', 'KeyS', 's');
        p.btn('stop').click();
        expect('...once S is up the button works again (a chord of the letter alone, Control still held)', p.sent().map((e) => e[1] + ':' + e[0]), ['s:keydown', 's:keyup']);
        p.clear();
        p.physical('keyup', 'ControlLeft', 'Control');
        p.btn('home').click();
        expect('Control up: a full chord again (Control down, the letter, Control up)', p.sent(), EXPECT.home);
        p.clear();
        p.physical('keydown', 'ControlRight', 'Control');
        p.btn('next').click();
        expect('the right Control counts too', p.sent().map((e) => e[1]), ['n', 'n']);
        p.clear();
        p.blurWindow();
        p.btn('next').click();
        expect('the window loses the focus: no key is held any more (a lift may never come)', p.sent(), EXPECT.next);
        p.clear();
        // none and the other single keys
        p.g.picked = 3;
        p.poll();
        p.physical('keydown', 'Escape', 'Escape');
        p.btn('none').click();
        expect('Esc held on the keyboard: None sends no Esc', p.sent(), []);
        p.physical('keyup', 'Escape', 'Escape');
        p.btn('none').click();
        expect('...and after the lift it does', p.sent(), EXPECT.none);
        p.clear();
        p.moreBtn.click();
        p.physical('keydown', 'F1', 'F1');
        p.btn('help').click();
        expect('F1 held: Quick help sends nothing, and the list closes as after any choice', [p.sent(), p.keys.isOpen()], [[], false]);
        p.physical('keyup', 'F1', 'F1');
        // our own events (not trusted) are no held key
        p.btn('all').click();
        const own = p.canvas.dispatched.filter((e) => e.type === 'keydown');
        own.forEach((e) => p.physical(e.type, e.code, e.key, false));
        p.clear();
        p.btn('all').click();
        expect('a key that the page made (not trusted: the events of an earlier press that bubbled to the window) is not a held key: the next press is whole', p.sent(), EXPECT.all);
        p.clear();
        // an Apple keyboard: a letter let go while Cmd is down sends no lift
        p.physical('keydown', 'MetaLeft', 'Meta');
        p.physical('keydown', 'KeyA', 'a');
        p.physical('keyup', 'MetaLeft', 'Meta');
        p.btn('all').click();
        expect('Cmd lifted with a letter whose lift never came: the letter is forgotten, All ants works', p.sent(), EXPECT.all);
        p.clear();
        p.physical('keydown', 'ControlLeft', 'Control');
        p.physical('keydown', 'MetaRight', 'Meta');
        p.physical('keyup', 'MetaRight', 'Meta');
        p.btn('stop').click();
        expect('...and a Control that is still held stays held (the letter alone)', p.sent().length, 2);
        p.physical('keyup', 'ControlLeft', 'Control');
        p.clear();
        p.physical('keydown', 'KeyS', 's');
        p.physical('keydown', 'MetaLeft', 'Meta');
        p.btn('stop').click();
        expect('a letter held while Cmd is held stays held (nothing sent)', p.sent(), []);
        p.physical('keyup', 'KeyS', 's');
        p.physical('keyup', 'MetaLeft', 'Meta');
        p.clear();
        // a lift of a key that was never down is no state
        p.physical('keyup', 'KeyA', 'a');
        p.btn('all').click();
        expect('a lift without a press changes nothing', p.sent(), EXPECT.all);
    }

    // ---- the panel's key guard and a button's Esc (a model of the rule of web/game_menu.js: a key that the panel kept stays its own until its lift, a fresh press of it ends that)
    {
        const p = page().start();
        p.g.picked = 100003;
        p.poll();
        const guard = { kept: false, ended: 0, reachedGame: 0 };
        p.doc.addEventListener('keydown', function (e) { if (e.code === 'Escape' && guard.kept && !e.repeat) { guard.kept = false; guard.ended++; } }, true);
        p.doc.addEventListener('keyup', function (e) { if (e.code === 'Escape') { e.stopPropagation(); } }, true);          // (the panel takes the lift of the key it kept: a listener in the bubble phase never hears it)
        // the settings panel was up, a physical Esc closed it and stays down: the panel keeps the key
        p.byId['game-menu'] = element('div', 'game-menu');
        p.physical('keydown', 'Escape', 'Escape');
        guard.kept = true;
        delete p.byId['game-menu'];
        p.poll();
        p.btn('none').click();
        // (the model: what a page-made keydown does on the document, as the real one bubbles from the canvas through it)
        p.canvas.dispatched.forEach((e) => p.doc.listeners.forEach((l) => { if (l.type === e.type) l.fn(e); }));
        expect('the #407 case: Esc held past the panel\'s close, then None: nothing is sent, so the panel keeps its hold and the held Esc\'s repeats do not open it again', [p.sent(), guard.ended, guard.kept], [[], 0, true]);
        p.path('keyup', 'Escape', 'Escape');
        p.btn('none').click();
        expect('...after the real lift (which the panel stops at the document\'s capture phase, so only a capture listener on the window hears it) None is an Esc again', p.sent(), EXPECT.none);
    }

    // ---- Shift: a tap lights it, a second tap puts it out, a press of 450 ms locks it; the key goes with the finger on the picture (down before the game sees the finger, up 120 ms after the last one left)
    {
        const KEYDOWN = ['keydown', 'Shift', 16, 16, 1, true, false, false];
        const KEYUP = ['keyup', 'Shift', 16, 16, 1, false, false, false];
        const fresh = () => { const p = page().start(); p.poll(); return p; };
        const tap = (p, ms) => { p.down(1, 100, 100); p.advance(ms === undefined ? 80 : ms); p.up(1); };
        {
            const p = fresh(), b = p.shiftBtn();
            expect('Shift: a real button named Shift, off', [b.tag, b.type, b.getAttribute('aria-label'), b.textContent, b.getAttribute('aria-pressed'), b.getAttribute('data-state'), p.keys.shift()], ['button', 'button', 'Shift', 'Shift', 'false', 'off', 'off']);
            b.click();
            expect('a tap lights it (latched, pressed) and sends no key', [p.keys.shift(), b.getAttribute('aria-pressed'), b.getAttribute('data-state'), p.sent()], ['latched', 'true', 'latched', []]);
            b.click();
            expect('a second tap puts it out', [p.keys.shift(), b.getAttribute('aria-pressed'), b.getAttribute('data-state'), p.sent()], ['off', 'false', 'off', []]);
            expect('Shift is no key of the exposed press (it goes with a finger)', [p.keys.press('shift'), p.sent()], [false, []]);
            // the press that locks
            p.pointer('pointerdown'); p.advance(449);
            expect('a press of 449 ms has not locked it', p.keys.shift(), 'off');
            p.advance(1);
            expect('450 ms locks it (lit differently: data-state locked, still pressed)', [p.keys.shift(), b.getAttribute('data-state'), b.getAttribute('aria-pressed')], ['locked', 'locked', 'true']);
            p.pointer('pointerup'); b.click();
            expect('the click that follows the lock is not a tap: it stays locked', p.keys.shift(), 'locked');
            b.click();
            expect('a tap on a locked Shift puts it out', p.keys.shift(), 'off');
            p.pointer('pointerdown'); p.advance(300); p.pointer('pointerup'); p.advance(300); b.click();
            expect('a press that ends before 450 ms never locks: its click latches', p.keys.shift(), 'latched');
            p.pointer('pointerdown'); p.advance(450); p.pointer('pointerup'); b.click();
            expect('a press of 450 ms on a latched Shift locks it', p.keys.shift(), 'locked');
            b.click();
            p.pointer('pointerdown'); p.pointer('pointercancel'); p.advance(600);
            expect('a press that the browser takes (pointercancel) never locks', p.keys.shift(), 'off');
            p.pointer('pointerdown'); p.pointer('pointerleave'); p.advance(600);
            expect('...nor one that leaves the button', p.keys.shift(), 'off');
            p.pointer('pointerdown'); p.advance(450); p.pointer('pointerup');                     // (locked; the browser made no click)
            p.pointer('pointerdown'); p.pointer('pointerup'); b.click();
            expect('a lock whose click never came does not swallow the next tap: that tap puts it out', p.keys.shift(), 'off');
            p.pointer('pointerdown'); p.g.needs = 1; p.advance(450);
            expect('the buttons are not usable when the time is up (a dialog): nothing is locked', p.keys.shift(), 'off');
            p.g.needs = 0;
            expect('a tap asks again too: with a dialog up it lights nothing', (p.g.needs = 1, b.click(), p.g.needs = 0, p.keys.shift()), 'off');
            expect('...and the whole of it sent nothing', p.sent(), []);
        }
        // a latched Shift and one finger that taps
        {
            const p = fresh();
            p.shiftBtn().click();
            p.down(1, 100, 100);
            expect('latched, a finger lands on the picture: Shift goes down first, as a keyboard\'s, before the game sees the finger', [p.shiftSent(), p.seen[0].shift, p.keys.shiftDown()], [[KEYDOWN], true, true]);
            p.advance(80);
            p.up(1);
            expect('the finger lifts: the latch is used up at once (the button is dark again) and Shift is still down', [p.keys.shift(), p.sdlShift(), p.shiftSent().length], ['off', true, 1]);
            p.advance(16);
            expect('a frame later, when the game handles the lift: Shift is down', p.sdlShift(), true);
            p.advance(103);
            expect('119 ms after the lift: still down', p.sdlShift(), true);
            p.advance(1);
            expect('120 ms after the lift: Shift goes up, once', [p.shiftSent(), p.sdlShift(), p.keys.shiftDown()], [[KEYDOWN, KEYUP], false, false]);
            p.advance(5000);
            expect('...and nothing comes later', p.shiftSent().length, 2);
            expect('the events are a keyboard\'s: they bubble, can be cancelled and are never repeats', p.canvas.dispatched.every((e) => e.bubbles === true && e.cancelable === true && e.composed === true && e.repeat === false), true);
            tap(p);
            p.advance(200);
            expect('a tap with Shift off sends no Shift', p.shiftSent().length, 2);
        }
        // which one-finger sequences use a latch up: a tap and a drag; not a hold, a lingering finger, or two fingers
        {
            const used = (label, play, want) => { const p = fresh(); p.shiftBtn().click(); play(p); expect(label, p.keys.shift(), want ? 'off' : 'latched'); return p; };
            used('a tap (up at 399 ms) uses it up', (p) => tap(p, 399), true);
            used('up at 400 ms is not the game\'s tap: it does not', (p) => tap(p, 400), false);
            used('a finger that lingers (420 ms, between the tap and the hold) does not', (p) => tap(p, 420), false);
            used('a hold (a right click: Shift plays no part) does not', (p) => tap(p, 600), false);
            used('a wobble of 5 px is no drag: a finger that wobbles and lingers (up at 420 ms) uses nothing', (p) => { p.down(1, 100, 100); p.advance(100); p.move(1, 105, 100); p.advance(320); p.up(1); }, false);
            used('a wobble inside the slop (8 px) is still a tap', (p) => { p.down(1, 100, 100); p.advance(40); p.move(1, 108, 100); p.advance(40); p.up(1); }, true);
            used('a drag (out of the slop at 9 px) uses it up, however long it goes on', (p) => { p.down(1, 100, 100); p.advance(40); p.move(1, 109, 100); p.advance(3000); p.move(1, 300, 300); p.up(1); }, true);
            used('a finger that leaves the slop at 449 ms is a drag', (p) => { p.down(1, 100, 100); p.advance(449); p.move(1, 150, 100); p.advance(200); p.up(1); }, true);
            used('...and at 450 ms the hold has fired: it is not', (p) => { p.down(1, 100, 100); p.advance(450); p.move(1, 150, 100); p.advance(200); p.up(1); }, false);
            used('a finger on the picture that the browser takes (touchcancel) does not', (p) => { p.down(1, 100, 100); p.advance(50); p.cancelTouch(1); }, false);
            used('two fingers (pan, zoom) do not, the first lifting first', (p) => { p.down(1, 100, 100); p.advance(30); p.down(2, 200, 100); p.advance(50); p.move(1, 150, 100); p.up(1); p.advance(50); p.up(2); }, false);
            used('...nor the second lifting first', (p) => { p.down(1, 100, 100); p.advance(30); p.down(2, 200, 100); p.advance(50); p.up(2); p.advance(20); p.up(1); }, false);
            used('...nor a pinch whose second finger lands after a drag began', (p) => { p.down(1, 100, 100); p.advance(30); p.move(1, 150, 100); p.down(2, 200, 100); p.advance(50); p.up(1); p.up(2); }, false);
            const p = fresh();
            p.shiftBtn().click();
            p.down(1, 100, 100); p.advance(30); p.down(2, 200, 100); p.advance(50); p.up(1); p.advance(50);
            expect('two fingers: Shift went down once, and stays down while a finger is on the picture', [p.shiftSent().length, p.sdlShift()], [1, true]);
            p.up(2); p.advance(119);
            expect('...it goes up 120 ms after the LAST finger left, not the first', [p.shiftSent().length, p.sdlShift()], [1, true]);
            p.advance(1);
            expect('...(a keyup, once)', [p.shiftSent(), p.keys.shift()], [[KEYDOWN, KEYUP], 'latched']);
            tap(p);
            expect('the latch was kept for the next tap: it is used up there, with Shift down again', [p.keys.shift(), p.shiftSent().length, p.sdlShift()], ['off', 3, true]);
        }
        // a hold leaves it latched until the next tap or drag (the maintainer's rule)
        {
            const p = fresh();
            p.shiftBtn().click();
            tap(p, 700); p.advance(120);
            tap(p, 700); p.advance(120);
            expect('two holds in a row: still latched, Shift down and up around each', [p.keys.shift(), p.shiftSent().map((e) => e[0])], ['latched', ['keydown', 'keyup', 'keydown', 'keyup']]);
            tap(p, 60);
            expect('...and the next tap uses it', p.keys.shift(), 'off');
        }
        // locked: every sequence is a Shift-click, and a finger within the 120 ms of the last keeps the same key down
        {
            const p = fresh();
            p.pointer('pointerdown'); p.advance(450); p.pointer('pointerup'); p.shiftBtn().click();
            tap(p, 60); p.advance(60);
            expect('locked: a tap, and 60 ms later: Shift is still down', [p.keys.shift(), p.sdlShift(), p.shiftSent().length], ['locked', true, 1]);
            tap(p, 60);
            expect('...a finger within the 120 ms keeps the same key down (no second keydown)', [p.shiftSent().length, p.sdlShift()], [1, true]);
            p.advance(119);
            expect('...and the 120 ms count from this lift', p.sdlShift(), true);
            p.advance(1);
            expect('...then it goes up, once', [p.shiftSent(), p.keys.shift()], [[KEYDOWN, KEYUP], 'locked']);
            tap(p, 300); p.advance(120);
            expect('a later tap: down and up again (locked stays locked, for a tap, a drag or a hold alike)', [p.shiftSent().map((e) => e[0]), p.keys.shift()], [['keydown', 'keyup', 'keydown', 'keyup'], 'locked']);
            p.shiftBtn().click();
            expect('a tap on Shift puts a lock out', p.keys.shift(), 'off');
        }
        // a latch that was used up lets go before the next finger lands (a quick second tap is not a Shift-click)
        {
            const p = fresh();
            p.shiftBtn().click();
            tap(p, 60); p.advance(50);
            p.down(1, 100, 100);
            expect('a finger lands 50 ms after a Shift tap: Shift goes up before it, and the game sees it without Shift', [p.shiftSent(), p.seen[p.seen.length - 1].shift], [[KEYDOWN, KEYUP], false]);
            p.advance(60); p.up(1); p.advance(1000);
            expect('...and nothing more comes', p.shiftSent().length, 2);
        }
        // nothing happens with Shift off, and a finger that is not the picture's is nobody's
        {
            const p = fresh();
            tap(p); p.down(1, 5, 5); p.move(1, 90, 90); p.up(1); p.advance(1000);
            expect('Shift off: fingers on the picture send nothing', [p.sent(), p.shiftSent()], [[], []]);
            p.shiftBtn().click();
            const other = element('div');
            p.down(7, 50, 50, other); p.advance(100); p.up(7); p.advance(1000);
            expect('latched, a finger that is not on the picture (the page, a button) sends nothing and keeps the latch', [p.sent(), p.keys.shift()], [[], 'latched']);
            p.down(1, 100, 100);
            p.down(9, 50, 50, p.btn('stop')); p.up(9);
            p.advance(100);
            expect('a finger of the page ends nobody\'s sequence: Shift is still down with the picture\'s finger', [p.sdlShift(), p.shiftSent().length], [true, 1]);
            p.up(1);
            expect('...and the picture\'s own lift ends it as a tap', p.keys.shift(), 'off');
            p.advance(120);
            expect('...Shift goes up', p.sdlShift(), false);
            // a touchend that the browser never delivered: its list says that this finger is alone
            const q = fresh();
            q.shiftBtn().click();
            q.down(1, 100, 100);
            q.live = [];
            q.down(2, 100, 100); q.advance(60); q.up(2);
            expect('a lift that never came does not make the next finger a second one: it is a tap and uses the latch up', q.keys.shift(), 'off');
        }
        // Shift is never left down
        {
            const causes = [
                ['the window loses the focus', (p) => p.blurWindow(), true],
                ['the page is hidden', (p) => p.visibility(true), true],
                ['the page is left', (p) => p.pageHide(), true],
                ['the browser takes the finger (touchcancel)', (p) => p.cancelTouch(1), false],
                ['the match ends (the buttons hide)', (p) => { p.g.running = 0; p.poll(); }, true],
                ['a dialog opens (the buttons hide)', (p) => { p.g.needs = 1; p.poll(); }, true],
                ['the setting is turned off', (p) => p.keys.set(false), true],
            ];
            causes.forEach((c) => {
                const p = fresh();
                p.shiftBtn().click();
                p.down(1, 100, 100);
                expect(c[0] + ': Shift is down with the finger', p.sdlShift(), true);
                c[1](p);
                expect(c[0] + ': Shift goes up at once', [p.sdlShift(), p.shiftSent().map((e) => e[0])], [false, ['keydown', 'keyup']]);
                expect(c[0] + ': ' + (c[2] ? 'the latch is let go too' : 'the latch stays (no click was made)'), p.keys.shift(), c[2] ? 'off' : 'latched');
                p.live = [];
                p.advance(5000);
                expect(c[0] + ': and no second keyup comes later', p.shiftSent().length, 2);
                if (!c[2]) return;                                   // (a finger that the browser takes is not a lifted finger)
                const q = fresh();
                q.shiftBtn().click();
                tap(q, 60); q.advance(10);
                expect(c[0] + ': Shift waits to go up (the finger already lifted)', q.sdlShift(), true);
                c[1](q);
                expect(c[0] + ': ...and goes at once', [q.sdlShift()], [false]);
            });
            const p = fresh();
            p.pointer('pointerdown'); p.advance(450); p.pointer('pointerup'); p.shiftBtn().click();
            p.down(1, 100, 100);
            p.blurWindow();
            expect('a lock is let go when the window loses the focus, with its key', [p.keys.shift(), p.sdlShift()], ['off', false]);
        }
        // the buttons hiding or a dialog between the polls
        {
            const p = fresh();
            p.shiftBtn().click();
            p.g.needs = 1;                                           // (a dialog opened since the last poll)
            p.down(1, 100, 100); p.advance(60); p.up(1); p.advance(200);
            expect('a finger while a dialog is up (between polls) is not a Shift-click: no key, and it uses nothing', [p.shiftSent(), p.keys.shift()], [[], 'latched']);
            p.poll();
            expect('the poll that hides the buttons lets the latch go', p.keys.shift(), 'off');
            p.g.needs = 0;
            p.poll();
            expect('...and it is off when they come back', p.keys.shift(), 'off');
        }
        // a keyboard's Shift that is down: nothing of ours (a keyboard cannot press a key that is down)
        {
            ['ShiftLeft', 'ShiftRight'].forEach((code) => {
                const p = fresh();
                p.physical('keydown', code, 'Shift');
                p.shiftBtn().click();
                tap(p, 60); p.advance(1000);
                expect('the keyboard\'s ' + code + ' is down: a Shift tap sends no Shift of ours (down or up), and uses the latch up', [p.shiftSent(), p.keys.shift()], [[], 'off']);
            });
            const p = fresh();
            p.physical('keydown', 'ShiftLeft', 'Shift');
            p.btn('all').click();
            expect('...and the chord of a button carries shiftKey, as a keyboard\'s would', p.canvas.dispatched.every((e) => e.shiftKey === true), true);
        }
        // a chord while Shift is down carries it
        {
            const p = fresh();
            p.shiftBtn().click();
            p.down(1, 100, 100);
            p.btn('all').click();
            expect('a chord while our Shift is down carries shiftKey on its four events', p.canvas.dispatched.filter((e) => e.code !== 'ShiftLeft').map((e) => e.shiftKey), [true, true, true, true]);
            p.up(1); p.advance(120); p.clear();
            p.btn('all').click();
            expect('...and once Shift is up it does not', p.canvas.dispatched.map((e) => e.shiftKey), [false, false, false, false]);
        }
    }

    // ---- a finger on a button reaches the game as nothing
    {
        const p = page().start();
        p.poll();
        ['mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'dblclick'].forEach((t) => p.bar.fire(t));
        expect('the mouse events that follow a touch stop at the buttons', p.bar.stopped, ['mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'dblclick']);
        expect('the long press is no context menu', p.bar.fire('contextmenu').prevented, true);
        expect('nothing was sent to the game by any of it', p.sent(), []);
    }

} catch (err) {
    console.log('FAIL the check threw: ' + (err && err.stack ? err.stack : err));
    failed++;
}

console.log(checks + ' checks, ' + failed + ' failures');
process.exit(failed ? 1 : 0);

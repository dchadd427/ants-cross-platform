// Runs the page's OWN code for the hotkey buttons (web/touch_keys.js, AntsTouchKeys.forPage, which web/shell.html calls), without a browser, with fakes for the page and a model of the game's
// answers (Module._ants_match_running, _ants_menu_get 100 / 101, _ants_probe 5 / 50 / 60), and checks:
//   * the buttons: All ants, Stop, Home, Next, None, More in that order, real buttons with the page's look and a name for a screen reader; More opens a list (Previous ant, Health marks, Quick help,
//     Quick chat 1 - 4) that closes after a choice, on a tap elsewhere and when the buttons hide;
//   * a press is exactly the key events of a keyboard on the game's canvas, in a keyboard's order (a chord: Control down, the letter down and up, Control up; a single key: down, up), never a repeat,
//     and nothing else; one click is one key (nothing listens to a finger that rests);
//   * the buttons show only in a match that this player plays on a touch screen, and are hidden, with nothing sent by a press, in every other case (the setting off, a mouse, the game not ready, a
//     recording or a live match, no match, the room or the results, a dialog or a vote, the countdown card, the settings panel, the chat's phone keyboard); a press asks again first;
//   * None is dimmed (aria-disabled, no key) while the game says that nothing is picked, and live when the game does not say;
//   * the remembered setting (ants.touchkeys: only "off" is off; a browser that cannot store still works) and the row's functions;
//   * where they stand: one class on the stage (upright or sideways, in the page or fullscreen, a short stage, a narrow one), and the page's layout is asked for only when it changes;
//   * the mouse events that follow a touch stop at the buttons, and the touch events are not listened to.
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
function element(tag, id) {
    const el = {
        tag, id: id || '', className: '', children: [], parent: null, attrs: {}, listeners: [], hidden: false, textContent: '', type: '', style: { props: {}, setProperty(k, v) { el.style.props[k] = v; } },
        stopped: [], blurred: 0, dispatched: [], clientHeight: 300, clientWidth: 400, offsetHeight: 48,
        setAttribute(k, v) { el.attrs[k] = String(v); }, getAttribute(k) { return k in el.attrs ? el.attrs[k] : null; },
        appendChild(c) { c.parent = el; el.children.push(c); return c; },
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
    const win = { innerWidth: 390, innerHeight: 844, KeyboardEvent: KE, listeners: [], addEventListener(type, fn) { win.listeners.push({ type, fn }); } };
    const state = { setting: opts.setting === undefined ? null : opts.setting, writes: [], touch: opts.touch !== false, ready: opts.ready !== false, watching: !!opts.watching, fullscreen: false };
    const store = opts.noStore ? () => { throw new Error('blocked'); } : () => ({ getItem: () => state.setting, setItem: (k, v) => { state.writes.push([k, v]); state.setting = v; } });
    const p = { g, stage, canvas, doc, win, state, calls, byId, body };
    p.start = () => {
        p.keys = T.forPage({
            doc, win, stage, canvas, store,
            touch: () => state.touch, ready: () => state.ready, watching: () => state.watching, running: () => g.running === 1,
            ask: (n) => { if (!g.probes) return -1; return n === 5 ? g.modal : n === 50 ? g.picked : n === 60 ? g.countdown : -1; },
            menu: (n) => (n === 100 ? g.where : n === 101 ? g.needs : -1),
            isFullscreen: () => state.fullscreen, relayout: () => { calls.relayout++; },
            every: (fn, ms) => { calls.intervals.push({ fn, ms }); return calls.intervals.length; },
        });
        p.bar = p.keys.bar;
        p.btn = (id) => p.bar.children.concat(p.bar.children[p.bar.children.length - 1].children).filter((c) => c.attrs['data-key'] === id)[0];
        p.moreBtn = p.bar.children.filter((c) => c.className.indexOf('tk-morebtn') >= 0)[0];
        p.more = p.bar.children[p.bar.children.length - 1];
        return p;
    };
    p.poll = () => calls.intervals.forEach((i) => i.fn());
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
function shown(p) { return p.poll(), !p.bar.hidden && p.bar.style.display !== 'none'; }
function pressAll(p) { ['all', 'stop', 'home', 'next', 'none'].forEach((id) => p.btn(id).click()); p.moreBtn.click(); ['prev', 'health', 'help', 'chat1', 'chat2', 'chat3', 'chat4'].forEach((id) => p.btn(id).click()); }

try {

    // ---- what is made
    {
        const p = page().start();
        expect('one group in the stage, named for a screen reader', [p.stage.children.length, p.bar.id, p.bar.getAttribute('role'), p.bar.getAttribute('aria-label')], [1, 'touch-keys', 'group', 'Game keys']);
        const main = p.bar.children.filter((c) => c.tag === 'button');
        expect('the buttons in order: All ants, Stop, Home, Next, None, More', main.map((b) => b.textContent), ['All ants', 'Stop', 'Home', 'Next', 'None', 'More']);
        expect('the list: Previous ant, Health marks, Quick help, Quick chat 1 - 4', p.more.children.map((b) => b.textContent), ['Previous ant', 'Health marks', 'Quick help', 'Quick chat 1', 'Quick chat 2', 'Quick chat 3', 'Quick chat 4']);
        const all = main.concat(p.more.children);
        expect('every one is a real button of the page\'s look with a name for a screen reader', all.every((b) => b.tag === 'button' && b.type === 'button' && /(^| )btn( |$)/.test(b.className) && b.getAttribute('aria-label')), true);
        expect('the names say what the key does', ['all', 'none', 'stop'].map((k) => p.btn(k).getAttribute('aria-label')), ['Select all ants', 'Select none', 'Stop the selected ants']);
        expect('a button listens to its click and nothing else (a finger that rests sends nothing: one click, one key)', all.filter((b) => b.id !== 'x').map((b) => b.listeners.map((l) => l.type).join()).filter((t) => t !== 'click').length, 0);
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
        expect('the panel opened since the last poll: the press sends nothing and the bar hides', [p.sent(), p.bar.hidden], [[], true]);
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
        const outside = element('div');
        p.doc.listeners.filter((l) => l.type === 'pointerdown').forEach((l) => l.fn({ target: outside }));
        expect('a tap elsewhere closes it', p.keys.isOpen(), false);
        p.moreBtn.click();
        p.doc.listeners.filter((l) => l.type === 'pointerdown').forEach((l) => l.fn({ target: p.btn('help') }));
        expect('a tap inside the list does not', p.keys.isOpen(), true);
        p.g.running = 0;
        p.poll();
        expect('the list closes when the buttons hide', [p.keys.isOpen(), p.more.hidden], [false, true]);
        p.g.running = 1;
        p.poll();
        expect('...and it is closed when they come back', p.keys.isOpen(), false);
        p.moreBtn.click();
        const esc = p.bar.fire('keydown', { key: 'Escape' });
        expect('Esc on the buttons closes the list and is not the game\'s Esc', [p.keys.isOpen(), esc.prevented, p.sent()], [false, true, []]);
        expect('the listener on the document is for the tap elsewhere and is the capture one', p.doc.listeners.map((l) => [l.type, l.cap]), [['pointerdown', true]]);
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
        expect('sideways in fullscreen: a rail (tk-side)', cls(p), ['tk-full', 'tk-on', 'tk-side']);
        expect('...one column on a stage that is tall enough', p.stage.style.props['--tk-rail'], '72px');
        p.state.fullscreen = false;
        p.stage.clientHeight = 280;
        p.poll();
        expect('sideways in the page on a short stage: two columns', [cls(p), p.stage.style.props['--tk-rail']], [['tk-on', 'tk-page', 'tk-side', 'tk-two'], '138px']);
        p.win.innerWidth = 390; p.win.innerHeight = 844;
        p.stage.clientWidth = 280;
        p.poll();
        expect('a narrow upright stage: All ants has a row of its own (tk-wrap)', cls(p).indexOf('tk-wrap') >= 0, true);
        p.stage.clientWidth = 350;
        p.poll();
        expect('a wide one does not', cls(p).indexOf('tk-wrap') < 0, true);
        p.g.running = 0;
        p.poll();
        expect('hidden: no class on the stage', cls(p), []);
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

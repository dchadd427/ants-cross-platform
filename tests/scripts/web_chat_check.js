// Runs the game page's OWN code for a phone's keyboard (web/shell.html, everything between ANTS_CHAT_BEGIN and ANTS_CHAT_END), without a browser, with fakes for the page and a model of the
// game's chat box and options window (Module._ants_chat_probe / _ants_chat_do / _ants_probe), and checks:
//   * a TAP on the chat box (a finger that stays within 10 px and is up within 400 ms) focuses the field inside the touchend handler, covers the box with it, and starts it with the game's line;
//     a drag, a long press, two fingers, a tap beside the box, a tap while the game takes no text, and a page whose game is not ready do nothing;
//   * what is typed is mirrored into the game's line (clear, then the printable ASCII characters, at most 100), Backspace and a replaced word included; what the game refuses is taken out of
//     the field; a composition (an IME's) is not rewritten while it is going on;
//   * Enter (the key, or a line break from the keyboard) sends: the line is mirrored, the game gets Enter, the field is emptied and the keyboard closes; Esc closes it without sending;
//   * a finger on the game outside the box closes it, a game that stops taking text closes it (the poll), a line that the game changed shows in the field;
//   * the options window (the game says so: probe 6) has four fields instead of the chat box: a tap on one focuses the field, covers it, starts it with the field's text and gives the game the
//     focus on it; typing is mirrored into that field only and written to the profile at the end of every edit; Enter (and a line break) closes the keyboard and does NOT send the game an Enter (it
//     would close the window); a tap on another field moves the keyboard to it, a tap on nothing, the window closing and the chat box coming back close it; the chat box is no box there;
//   * the field's keys never reach the game's own handler (stopPropagation), and the page adds no touchmove listener, never cancels a touch and listens passively.
// tests/scripts/test_web_chat.py runs this with node (the quick tier). usage: node web_chat_check.js web/shell.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');

function between(text, begin, end, path) {
    const at = text.indexOf(begin);
    if (at < 0) throw new Error(path + ': the marker ' + begin + ' is missing');
    const from = text.indexOf('\n', at) + 1;
    const stop = text.indexOf(end, from);
    if (stop < 0) throw new Error(path + ': the marker ' + end + ' is missing');
    return text.slice(from, text.lastIndexOf('\n', stop - 1) + 1);
}

let failed = 0;
let checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
        failed++;
    }
}

const shellPath = process.argv[2];
if (!shellPath) { console.log('usage: web_chat_check.js shell.html'); process.exit(2); }

function target(name) {
    const t = {
        name,
        listeners: [],
        addEventListener(type, fn, options) {
            const o = typeof options === 'object' && options !== null ? options : { capture: !!options };
            t.listeners.push({ type, fn, capture: !!o.capture, passive: o.passive });
        },
        of(type) { return t.listeners.filter((l) => l.type === type); },
    };
    return t;
}

function event(type, fields) {
    const e = Object.assign({ type, prevented: false, stopped: false }, fields || {});
    e.preventDefault = () => { e.prevented = true; };
    e.stopPropagation = () => { e.stopped = true; };
    return e;
}

function fire(who, type, e) {
    for (const l of who.of(type)) l.fn(e);
    return e;
}

// The page: the canvas (960 x 540 game pixels shown at 480 x 270 CSS pixels at (10, 20)), the field, the document, the box, and a game with a chat line
function page(opts) {
    opts = opts || {};
    const doc = { activeElement: null };
    const calls = { focus: 0, blur: 0, gameFocus: 0, intervals: [], cleared: 0 };
    const game = { line: '', takes: true, enters: 0, box: [799, 299, 143, 198], history: [], mode: 0, focus: -1, commits: 0,
                   fields: [[300, 200, 141, 15], [300, 232, 141, 15], [510, 200, 141, 15], [510, 232, 141, 15]], texts: ['Now you are in for it!', 'Let me be!', 'Attack!', 'Do you want to ally?'], saved: [] };
    const activeLine = () => (game.mode === 1 ? (game.focus >= 0 ? game.texts[game.focus] : '') : game.line);
    const input = target('input');
    input.value = '';
    input.style = {};
    input.focus = () => { calls.focus++; doc.activeElement = input; };
    input.blur = () => { calls.blur++; if (doc.activeElement === input) { doc.activeElement = null; fire(input, 'blur', event('blur')); } };
    const canvas = target('canvas');
    canvas.getBoundingClientRect = () => ({ left: 10, top: 20, width: 480, height: 270 });
    const outer = { getBoundingClientRect: () => ({ left: 10, top: 20, width: 480, height: 270 }) };
    const Module = opts.noGame ? {} : {
        _ants_probe(what) { return what === 40 ? 960 : what === 41 ? 540 : -1; },
        _ants_chat_probe(what) {
            if (what === 0) return game.takes ? 1 : 0;
            if (what >= 1 && what <= 4) return game.box[what - 1];
            if (what === 5) return activeLine().length;
            if (what === 6) return game.mode;
            if (what >= 10 && what < 26) return game.mode === 1 ? game.fields[(what - 10) >> 2][(what - 10) & 3] : -1;
            if (what >= 100) return what - 100 < activeLine().length ? activeLine().charCodeAt(what - 100) : -1;
            return -1;
        },
        _ants_chat_do(what, value) {
            game.history.push([what, value]);
            if (!game.takes) return 0;
            if (game.mode === 1) {                                           // the options window: the field with the focus
                if (what === 3) { game.enters++; return 1; }
                if (what === 4) { game.focus = value; return 1; }
                if (game.focus < 0) return 0;
                if (what === 0) game.texts[game.focus] = '';
                else if (what === 1) { if (game.texts[game.focus].length < 100) game.texts[game.focus] += String.fromCharCode(value); }
                else if (what === 2) game.texts[game.focus] = game.texts[game.focus].slice(0, -1);
                else if (what === 5) { game.commits++; game.saved[game.focus] = game.texts[game.focus]; }
                else return 0;
                return 1;
            }
            if (what === 0) game.line = '';
            else if (what === 1) { if (game.line.length < 100) game.line += String.fromCharCode(value); }
            else if (what === 2) game.line = game.line.slice(0, -1);
            else if (what === 3) { game.enters++; }
            return 1;
        },
    };
    const doc2 = Object.assign(doc, { getElementById: (id) => (id === 'chat-keys' ? input : null) });
    return { input, canvas, outer, doc: doc2, Module, game, calls, ready: opts.ready !== false };
}

function load(text, p) {
    const block = between(text, 'ANTS_CHAT_BEGIN', 'ANTS_CHAT_END', shellPath);
    const setInterval = (fn, ms) => { p.calls.intervals.push({ fn, ms }); return p.calls.intervals.length; };
    const clearInterval = () => { p.calls.cleared++; };
    const focusGame = () => { p.calls.gameFocus++; };
    const make = new Function('document', 'canvas', 'boxElement', 'Module', 'isReadyToPlay', 'setInterval', 'clearInterval', 'focusGame', block);
    make(p.doc, p.canvas, p.outer, p.Module, p.ready, setInterval, clearInterval, focusGame);
}

function touch(id, x, y) { return { identifier: id, clientX: x, clientY: y }; }

// A tap at client (x, y): down, up `ms` later, `dx` and `dy` away
function tap(p, x, y, opts) {
    opts = opts || {};
    const down = touch(1, x, y), up = touch(1, x + (opts.dx || 0), y + (opts.dy || 0));
    fire(p.canvas, 'touchstart', event('touchstart', { changedTouches: [down], touches: opts.two ? [down, touch(2, 5, 5)] : [down], timeStamp: 1000 }));
    return fire(p.canvas, 'touchend', event('touchend', { changedTouches: [up], touches: [], timeStamp: 1000 + (opts.ms === undefined ? 80 : opts.ms) }));
}

// The chat box is at game pixels (799, 299) .. (942, 497): at 0.5 CSS pixel per game pixel and the canvas at (10, 20) its middle is at client (10 + 870 / 2, 20 + 398 / 2) = (445, 219)
const INSIDE = [445, 219];
const BESIDE = [100, 100];
// The options window's fields are at game pixels (300, 200), (300, 232), (510, 200), (510, 232), each 141 x 15: their middles in client pixels
const FIELD = [0, 1, 2, 3].map((k) => { const f = [[300, 200], [300, 232], [510, 200], [510, 232]][k]; return [10 + (f[0] + 70.5) / 2, 20 + (f[1] + 7.5) / 2]; });

function typeText(p, text) {
    p.input.value = text;
    fire(p.input, 'input', event('input', { inputType: 'insertText', isComposing: false }));
}

try {
    const text = fs.readFileSync(shellPath, 'utf8');

    // ---- what is registered
    {
        const p = page();
        load(text, p);
        expect('touchstart, touchend and touchcancel on the canvas, all passive', ['touchstart', 'touchend', 'touchcancel'].map((t) => p.canvas.of(t).map((l) => l.passive)), [[true], [true], [true]]);
        expect('no touchmove listener', p.canvas.of('touchmove').length, 0);
        expect('the field stops the keys of its own', ['keydown', 'keypress', 'keyup'].map((t) => p.input.of(t).length), [1, 1, 1]);
    }

    // ---- a tap on the box opens the keyboard
    {
        const p = page();
        p.game.line = 'hel';
        load(text, p);
        const end = tap(p, ...INSIDE);
        expect('a tap on the box focuses the field (inside the touchend handler)', [p.calls.focus, p.doc.activeElement === p.input], [1, true]);
        expect('the touch itself is not cancelled', end.prevented, false);
        expect('the field starts with the game\'s line', p.input.value, 'hel');
        expect('the field covers the box (CSS px inside the game box: 799 * 0.5, 299 * 0.5, 143 * 0.5, 198 * 0.5)', [p.input.style.left, p.input.style.top, p.input.style.width, p.input.style.height], ['399.5px', '149.5px', '71.5px', '99px']);
        expect('the poll starts', p.calls.intervals.length, 1);
    }

    // ---- the slop's edge: 10 px is still a tap, in both directions
    for (const o of [{ dx: 10 }, { dy: 10 }, { dx: -10 }, { dy: -10 }]) {
        const p = page();
        load(text, p);
        tap(p, ...INSIDE, o);
        expect('a finger that moves ' + JSON.stringify(o) + ' px is still a tap', p.calls.focus, 1);
    }

    // ---- what is not a tap on the box
    {
        const cases = [
            ['a tap beside the box', () => { const p = page(); load(text, p); tap(p, ...BESIDE); return p; }],
            ['a drag (the finger travels 30 px)', () => { const p = page(); load(text, p); tap(p, ...INSIDE, { dx: 30 }); return p; }],
            ['a vertical drag (the finger travels 30 px down)', () => { const p = page(); load(text, p); tap(p, ...INSIDE, { dy: 30 }); return p; }],
            ['a finger 11 px off sideways', () => { const p = page(); load(text, p); tap(p, ...INSIDE, { dx: 11 }); return p; }],
            ['a finger 11 px off vertically', () => { const p = page(); load(text, p); tap(p, ...INSIDE, { dy: 11 }); return p; }],
            ['a long press (500 ms)', () => { const p = page(); load(text, p); tap(p, ...INSIDE, { ms: 500 }); return p; }],
            ['two fingers', () => { const p = page(); load(text, p); tap(p, ...INSIDE, { two: true }); return p; }],
            ['a game that takes no text (a dialog is open, chat is off, no match)', () => { const p = page(); p.game.takes = false; load(text, p); tap(p, ...INSIDE); return p; }],
            ['a game that is not ready', () => { const p = page({ ready: false }); load(text, p); tap(p, ...INSIDE); return p; }],
            ['a page without the game\'s exports', () => { const p = page({ noGame: true }); load(text, p); tap(p, ...INSIDE); return p; }],
        ];
        for (const [label, run] of cases) {
            let threw = false, p = null;
            try { p = run(); } catch (err) { threw = true; }
            expect(label + ': nothing throws', threw, false);
            expect(label + ': the keyboard does not open', p ? p.calls.focus : -1, 0);
        }
    }

    // ---- typing is mirrored into the game's line
    {
        const p = page();
        load(text, p);
        tap(p, ...INSIDE);
        typeText(p, 'Rush');
        expect('the line is cleared and typed again', p.game.line, 'Rush');
        typeText(p, 'Rus');
        expect('Backspace is the field with a letter fewer', p.game.line, 'Rus');
        typeText(p, 'Rus the base!');
        expect('more text', p.game.line, 'Rus the base!');
        typeText(p, 'café \u{1F600}ok');
        expect('what the game does not take (accents, emoji) is taken out of the field and the line', [p.input.value, p.game.line], ['caf ok', 'caf ok']);
        typeText(p, 'x'.repeat(130));
        expect('at most 100 characters', [p.input.value.length, p.game.line.length], [100, 100]);
        typeText(p, '');
        expect('an emptied field empties the line', p.game.line, '');
    }

    // ---- a composition is not rewritten while it goes on
    {
        const p = page();
        load(text, p);
        tap(p, ...INSIDE);
        fire(p.input, 'compositionstart', event('compositionstart'));
        p.input.value = 'café';
        fire(p.input, 'input', event('input', { inputType: 'insertCompositionText', isComposing: true }));
        expect('the field keeps the composition as it is', p.input.value, 'café');
        expect('and the game has what it takes of it', p.game.line, 'caf');
        fire(p.input, 'compositionend', event('compositionend'));
        expect('at its end the field is what the game has', [p.input.value, p.game.line], ['caf', 'caf']);
    }

    // ---- Enter sends, Esc does not
    {
        const p = page();
        load(text, p);
        tap(p, ...INSIDE);
        typeText(p, 'hello');
        const key = fire(p.input, 'keydown', event('keydown', { key: 'Enter' }));
        expect('Enter: the game got the line and the key', [p.game.line, p.game.enters], ['hello', 1]);
        expect('Enter is the page\'s, not typed into the field', key.prevented, true);
        expect('the field is empty and the keyboard is closed', [p.input.value, p.doc.activeElement, p.calls.cleared], ['', null, 1]);
        expect('the game gets its keyboard focus back', p.calls.gameFocus >= 1, true);
        const q = page();
        load(text, q);
        tap(q, ...INSIDE);
        typeText(q, 'draft');
        fire(q.input, 'keydown', event('keydown', { key: 'Escape' }));
        expect('Esc closes the keyboard and sends nothing', [q.doc.activeElement, q.game.enters, q.input.value], [null, 0, '']);
        const r = page();
        load(text, r);
        tap(r, ...INSIDE);
        typeText(r, 'line break');
        const before = fire(r.input, 'beforeinput', event('beforeinput', { inputType: 'insertLineBreak' }));
        expect('a line break from the keyboard sends too, and is not typed', [before.prevented, r.game.enters, r.doc.activeElement], [true, 1, null]);
        const u = page();
        load(text, u);
        tap(u, ...INSIDE);
        typeText(u, 'up only');
        fire(u.input, 'keyup', event('keyup', { key: 'Enter' }));
        fire(u.input, 'keypress', event('keypress', { key: 'Enter' }));
        expect('Enter on keyup or keypress alone sends nothing (only the keydown does)', [u.game.enters, u.doc.activeElement === u.input], [0, true]);
        const s = page();
        load(text, s);
        tap(s, ...INSIDE);
        fire(s.input, 'keydown', event('keydown', { key: 'Enter' }));
        expect('Enter on an empty field goes to the game as Enter (the game sends nothing for an empty line)', [s.game.enters, s.game.line], [1, '']);
    }

    // ---- the keys of the field never reach the game's handler
    {
        const p = page();
        load(text, p);
        tap(p, ...INSIDE);
        const all = ['keydown', 'keypress', 'keyup'].map((t) => fire(p.input, t, event(t, { key: 'a' })).stopped);
        expect('keydown, keypress and keyup stop at the field (SDL would type the letter a second time)', all, [true, true, true]);
    }

    // ---- other ways to close
    {
        const p = page();
        load(text, p);
        tap(p, ...INSIDE);
        const down = touch(3, ...BESIDE);
        fire(p.canvas, 'touchstart', event('touchstart', { changedTouches: [down], touches: [down], timeStamp: 5000 }));
        expect('a finger on the game outside the box closes the keyboard', p.doc.activeElement, null);
        const q = page();
        load(text, q);
        tap(q, ...INSIDE);
        const again = touch(4, ...INSIDE);
        fire(q.canvas, 'touchstart', event('touchstart', { changedTouches: [again], touches: [again], timeStamp: 5000 }));
        expect('a finger on the box keeps it', q.doc.activeElement, q.input);
        const r = page();
        load(text, r);
        tap(r, ...INSIDE);
        typeText(r, 'abc');
        r.game.line = 'quick chat';
        r.calls.intervals[0].fn();
        expect('a line that the game changed shows in the field', r.input.value, 'quick chat');
        r.game.takes = false;
        r.calls.intervals[0].fn();
        expect('a game that stops taking text (a dialog opens, the match ends) closes the keyboard', [r.doc.activeElement, r.input.value], [null, '']);
    }

    // ---- the options window: four fields instead of the chat box
    {
        const open = () => { const p = page(); p.game.mode = 1; load(text, p); return p; };
        const p = open();
        const end = tap(p, ...FIELD[1]);
        expect('a tap on a field focuses the keyboard\'s field (inside the touchend handler), the touch is not cancelled', [p.calls.focus, p.doc.activeElement === p.input, end.prevented], [1, true, false]);
        expect('the game is told which field (4, index) before the line is read', [p.game.history[0], p.game.focus], [[4, 1], 1]);
        expect('the field starts with that field\'s text, and the keyboard\'s key says Done (nothing is sent)', [p.input.value, p.input.enterKeyHint], ['Let me be!', 'done']);
        expect('it covers the field (CSS px inside the game box: 300 * 0.5, 232 * 0.5, 141 * 0.5, 15 * 0.5)', [p.input.style.left, p.input.style.top, p.input.style.width, p.input.style.height], ['150px', '116px', '70.5px', '7.5px']);
        typeText(p, 'Hold the line');
        expect('typing goes into that field only, and the edit is written once at its end (the commit)', [p.game.texts, p.game.commits, p.game.saved[1]], [['Now you are in for it!', 'Hold the line', 'Attack!', 'Do you want to ally?'], 1, 'Hold the line']);
        expect('the chat line is untouched', p.game.line, '');
        typeText(p, 'caf\u00e9 ok');
        expect('what the game does not take is taken out of the field and the text', [p.input.value, p.game.texts[1]], ['caf ok', 'caf ok']);
        typeText(p, 'x'.repeat(130));
        expect('at most 100 characters', [p.input.value.length, p.game.texts[1].length], [100, 100]);
        const enters = p.game.enters;
        const key = fire(p.input, 'keydown', event('keydown', { key: 'Enter' }));
        expect('Enter closes the keyboard and does not send the game an Enter (it would close the window)', [key.prevented, p.game.enters - enters, p.doc.activeElement, p.input.value], [true, 0, null, '']);
        expect('what was typed stays in the field', p.game.texts[1].length, 100);
        // another field while it is open: the keyboard moves
        const q = open();
        tap(q, ...FIELD[0]);
        const down = touch(7, ...FIELD[3]);
        fire(q.canvas, 'touchstart', event('touchstart', { changedTouches: [down], touches: [down], timeStamp: 3000 }));
        expect('a finger on another field keeps the keyboard', q.doc.activeElement, q.input);
        fire(q.canvas, 'touchend', event('touchend', { changedTouches: [touch(7, ...FIELD[3])], touches: [], timeStamp: 3050 }));
        expect('and at its end the keyboard is on that field (the game is told, the field is moved and starts with its text)', [q.game.focus, q.input.value, q.input.style.left, q.input.style.top], [3, 'Do you want to ally?', '255px', '116px']);
        // a finger on nothing, and a line break from the keyboard
        const r = open();
        tap(r, ...FIELD[2]);
        const away = touch(8, ...BESIDE);
        fire(r.canvas, 'touchstart', event('touchstart', { changedTouches: [away], touches: [away], timeStamp: 4000 }));
        expect('a finger on the game outside the fields closes the keyboard', r.doc.activeElement, null);
        const u = open();
        tap(u, ...FIELD[2]);
        typeText(u, 'wait');
        const lb = fire(u.input, 'beforeinput', event('beforeinput', { inputType: 'insertLineBreak' }));
        expect('a line break from the keyboard only closes it too', [lb.prevented, u.game.enters, u.doc.activeElement, u.game.texts[2]], [true, 0, null, 'wait']);
        // the chat box is no box while the window is up; the fields are no boxes when it is not
        const v = open();
        tap(v, ...INSIDE);
        expect('a tap where the chat box is does nothing while the options window is up', v.calls.focus, 0);
        const w = page();
        load(text, w);
        tap(w, ...FIELD[1]);
        expect('a tap where a field would be does nothing when the window is not up', w.calls.focus, 0);
        // the window closing, or the chat box coming back, under the keyboard
        const x = open();
        tap(x, ...FIELD[0]);
        x.game.takes = false;
        x.calls.intervals[0].fn();
        expect('the window closing (the game takes no text) closes the keyboard', [x.doc.activeElement, x.input.value], [null, '']);
        const y = open();
        tap(y, ...FIELD[0]);
        y.game.mode = 0;
        y.calls.intervals[0].fn();
        expect('the options window closing with chat open behind it closes the keyboard (the boxes are other ones now)', y.doc.activeElement, null);
        const z = page();
        load(text, z);
        tap(z, ...INSIDE);
        z.game.mode = 1;
        z.calls.intervals[0].fn();
        expect('the options window opening under the chat box\'s keyboard closes it', z.doc.activeElement, null);
        const t = open();
        tap(t, ...FIELD[0]);
        t.game.texts[0] = 'changed by the game';
        t.calls.intervals[0].fn();
        expect('a text that the game changed shows in the field', t.input.value, 'changed by the game');
        // the chat box, once the window is gone, is a chat box again: Enter sends
        const c = page();
        load(text, c);
        tap(c, ...INSIDE);
        expect('the chat box keyboard says Send', c.input.enterKeyHint, 'send');
    }

    // ---- the touch of a finger that went down on the box and was cancelled is no tap
    {
        const p = page();
        load(text, p);
        const down = touch(1, ...INSIDE);
        fire(p.canvas, 'touchstart', event('touchstart', { changedTouches: [down], touches: [down], timeStamp: 1000 }));
        fire(p.canvas, 'touchcancel', event('touchcancel', {}));
        fire(p.canvas, 'touchend', event('touchend', { changedTouches: [touch(1, ...INSIDE)], touches: [], timeStamp: 1050 }));
        expect('a cancelled touch opens nothing', p.calls.focus, 0);
    }

    console.log('web chat: ' + checks + ' checks, ' + failed + ' failures');
    process.exit(failed === 0 ? 0 : 1);
} catch (err) {
    console.log('ERROR ' + (err && err.stack ? err.stack : err));
    process.exit(1);
}

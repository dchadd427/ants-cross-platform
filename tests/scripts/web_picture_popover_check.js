// Runs the game page's OWN code of the replay bar's Picture button (web/shell.html: everything between PICTURE_POPOVER_BEGIN and PICTURE_POPOVER_END) on a page of fakes: the button opens and closes the
// small window with the dropdown of the shapes, a click elsewhere, Esc and the focus leaving it close it, Esc gives the focus back to the button, aria-expanded follows, a click inside does not close it, the
// bar is woken (a fullscreen bar stays while the window is open), and Esc is the window's while it is open and nobody's otherwise.
// tests/scripts/test_web_replay.py runs this with node (the quick tier). usage: node web_picture_popover_check.js web/shell.html     (exit 0: every row holds; every differing row is printed)
'use strict';
const fs = require('fs');

const path = process.argv[2];
if (!path) { console.log('usage: web_picture_popover_check.js shell.html'); process.exit(2); }
const text = fs.readFileSync(path, 'utf8');
const at = text.indexOf('PICTURE_POPOVER_BEGIN');
const stop = text.indexOf('PICTURE_POPOVER_END');
if (at < 0 || stop < 0) { console.log('FAIL the markers of the Picture button\'s block are missing'); process.exit(1); }
const block = text.slice(text.indexOf('\n', at) + 1, text.lastIndexOf('\n', stop - 1) + 1);

let failed = 0;
function expect(label, got, want) {
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}

function element(name) {
    const el = { name, attributes: {}, listeners: {}, children: [], hidden: false, focused: 0,
        setAttribute(k, v) { el.attributes[k] = String(v); }, getAttribute(k) { return k in el.attributes ? el.attributes[k] : null; },
        addEventListener(type, fn) { (el.listeners[type] = el.listeners[type] || []).push(fn); },
        focus() { el.focused++; },
        contains(other) { return other === el || el.children.some((c) => c === other || c.contains(other)); } };
    return el;
}
function setup() {
    const wrap = element('wrap'), button = element('btn'), pop = element('pop'), dropdown = element('dropdown'), outside = element('outside');
    button.tagName = 'BUTTON'; button.pressed = 0; button.click = () => { button.pressed++; };
    dropdown.tagName = 'SELECT'; dropdown.pressed = 0; dropdown.click = () => { dropdown.pressed++; };
    wrap.children = [button, pop]; pop.children = [dropdown];
    pop.hidden = true;
    const byId = { picwrap: wrap, picbtn: button, picpop: pop };
    const calls = { woke: 0 };
    const winListeners = {}, docListeners = {};
    const win = { addEventListener(type, fn, capture) { winListeners[type] = { fn, capture }; } };
    const doc = { addEventListener(type, fn) { docListeners[type] = fn; } };
    new Function('$', 'wake', 'window', 'document', block)((id) => byId[id], () => { calls.woke++; }, win, doc);
    const key = (name) => { const ev = { key: name, prevented: 0, stopped: 0, preventDefault() { ev.prevented++; }, stopImmediatePropagation() { ev.stopped++; } }; winListeners.keydown.fn(ev); return ev; };
    return { wrap, button, pop, radio: dropdown, dropdown, outside, calls, winListeners, key,
        click(target) { if (target === button) button.listeners.click.forEach((f) => f({ target })); docListeners.click({ target }); },
        press(name, target, repeat) {         // a key on a button inside the wrapper: what the page did with the key, and the presses it made
            const ev = { key: name, target, repeat: !!repeat, prevented: 0, preventDefault() { ev.prevented++; } };
            target.pressed = 0;
            wrap.listeners.keydown.forEach((f) => f(ev));
            return { prevented: ev.prevented, pressed: target.pressed };
        },
        release(name) { const ev = { key: name, prevented: 0, preventDefault() { ev.prevented++; } }; wrap.listeners.keyup.forEach((f) => f(ev)); return ev.prevented; },
        focusout(related) { wrap.listeners.focusout.forEach((f) => f({ relatedTarget: related })); },
        state() { return (p.pop.hidden ? 'closed' : 'open') + ' ' + p.button.attributes['aria-expanded']; } };
}
let p;

p = setup();
expect('shut at the start (the markup says so); the block does not open it', p.pop.hidden, true);
expect('the Esc listener is on the window, in the capture phase (ahead of the game and of the bar\'s own keys)', [typeof p.winListeners.keydown.fn, p.winListeners.keydown.capture], ['function', true]);

p = setup();
p.click(p.button);
expect('a press of the button opens the window and says so', p.state(), 'open true');
p.click(p.button);
expect('a second press closes it', p.state(), 'closed false');

p = setup();
p.click(p.button);
p.click(p.radio);
expect('a click inside the window (the dropdown) leaves it open', p.state(), 'open true');
p.click(p.outside);
expect('a click elsewhere closes it', p.state(), 'closed false');
expect('a click elsewhere does not take the focus', p.button.focused, 0);
p.click(p.outside);
expect('a click elsewhere with the window shut does nothing', p.state(), 'closed false');

p = setup();
p.click(p.button);
const esc = p.key('Escape');
expect('Esc closes the window', p.state(), 'closed false');
expect('Esc gives the focus back to the button', p.button.focused, 1);
expect('Esc is the window\'s while it is open: the key is not passed on', [esc.prevented, esc.stopped], [1, 1]);
const again = p.key('Escape');
expect('Esc with the window shut is nobody\'s here: the key is passed on and the focus stays', [again.prevented, again.stopped, p.button.focused], [0, 0, 1]);
p.click(p.button);
const other = p.key('a');
expect('another key leaves the window open and is passed on', [p.state(), other.prevented, other.stopped], ['open true', 0, 0]);

p = setup();
p.click(p.button);
p.focusout(p.radio);
expect('the focus moving inside the window keeps it open', p.state(), 'open true');
p.focusout(null);
expect('a focus that goes to nowhere (the window loses focus) keeps it open', p.state(), 'open true');
p.focusout(p.outside);
expect('the focus leaving for another control closes it', p.state(), 'closed false');

p = setup();
const before = p.calls.woke;
p.click(p.button);
expect('opening wakes the bar (a fullscreen bar that is away comes back and stays while the window is open)', p.calls.woke > before, true);

p = setup();
expect('Enter on a button presses it once and keeps the browser from pressing it again', p.press('Enter', p.button), { prevented: 1, pressed: 1 });
expect('Space on the Picture button presses it once', p.press(' ', p.button), { prevented: 1, pressed: 1 });
expect('Enter and Space on the dropdown are the browser\'s own (it opens its list): the page neither presses nor prevents', [p.press('Enter', p.dropdown), p.press(' ', p.dropdown)], [{ prevented: 0, pressed: 0 }, { prevented: 0, pressed: 0 }]);
expect('a key that is held presses once, not again', p.press('Enter', p.button, true), { prevented: 1, pressed: 0 });
expect('another key is left alone', p.press('a', p.button), { prevented: 0, pressed: 0 });
expect('the Space key\'s release is kept from pressing a second time', [p.release(' '), p.release('Enter')], [1, 0]);

p = setup();
expect('the arrows are the dropdown\'s own: the window has no key handler for them (the window\'s keydown listeners are the wrapper\'s Enter and Space only)', Object.keys(p.pop.listeners).join(), '');

console.log(failed === 0 ? 'web picture popover: every row holds' : 'web picture popover: ' + failed + ' failures');
process.exit(failed === 0 ? 0 : 1);

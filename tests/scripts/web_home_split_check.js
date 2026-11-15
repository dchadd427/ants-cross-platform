// Runs the drag handles of Home (web/home/split.js, docs/HOME_SCREEN.md "The splitters") in node, in a small fake page of this file: the pure parts (the limits, the keys, the drag, the stored text), then the handles
// with a fake mouse and keyboard, a storage that throws, a window that changes and a phone. Also: what home.js does to load them, and the source of the script. usage: node web_home_split_check.js (exit 0: every check holds)
'use strict';
const fs = require('fs');
const path = require('path');
const HOME = path.resolve(__dirname, '..', '..', 'web', 'home');
const S = require(path.join(HOME, 'split.js'));
const source = fs.readFileSync(path.join(HOME, 'split.js'), 'utf8');
const homeSource = fs.readFileSync(path.join(HOME, 'home.js'), 'utf8');
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

// ---- 1. the pure parts ----
same('the least sizes', [S.SIDE_MIN, S.CHAT_MIN, S.STEP, S.BIG], [240, 120, 16, 64]);
same('the players: both lists keep 240 (1280 wide: 24+24 padding, 14 gap)', S.sideLimits(1280), { min: 240, max: 1280 - 62 - 240 });
same('the players: a window too narrow keeps the minimum', S.sideLimits(500), { min: 240, max: 240 });
same('the chat: 120 to 70% of the body', [S.chatLimits(600), S.chatLimits(100)], [{ min: 120, max: 420 }, { min: 120, max: 120 }]);
const L = { min: 240, max: 400 };
same('keys (players): Left wider by 16, Shift 64, Right narrower, Home, End', [S.keyed('x', 'ArrowLeft', false, 300, L), S.keyed('x', 'ArrowLeft', true, 300, L), S.keyed('x', 'ArrowRight', false, 300, L), S.keyed('x', 'Home', false, 300, L), S.keyed('x', 'End', false, 300, L)], [316, 364, 284, 240, 400]);
same('keys (chat): Up taller, Down shorter; the other arrows are not ours', [S.keyed('y', 'ArrowUp', false, 300, L), S.keyed('y', 'ArrowDown', true, 300, L), S.keyed('y', 'ArrowLeft', false, 300, L), S.keyed('x', 'ArrowUp', false, 300, L), S.keyed('y', 'a', false, 300, L)], [316, 240, null, null, null]);
same('keys stay inside the limits', [S.keyed('x', 'ArrowLeft', true, 380, L), S.keyed('x', 'ArrowRight', true, 250, L)], [400, 240]);
same('a drag: left or up grows, and it stops at the limits', [S.dragged(280, -50, L), S.dragged(280, 50, L), S.dragged(280, -900, L), S.dragged(280, 900, L), S.dragged(280.4, 0.4, L)], [330, 240, 400, 240, 280]);
same('the stored text: good, partial, wrong, not text', [S.parse('{"p":300,"c":200}'), S.parse('{"p":300}'), S.parse('{"p":"a","c":-4}'), S.parse('nope'), S.parse(null)], [{ p: 300, c: 200 }, { p: 300, c: null }, { p: null, c: null }, { p: null, c: null }, { p: null, c: null }]);
same('the stored text: absurd numbers are not taken', S.parse('{"p":1e9,"c":0}'), { p: null, c: null });

// ---- 2. the handles in a fake page ----
class Node {
    constructor(tag, id) { this.tag = tag; this.id = id || ''; this.className = ''; this.children = []; this.attrs = {}; this.on = {}; this.styleMap = {}; this.parent = null; this.box = null; this.w = 0; this.h = 0; this.focused = false; this.captured = null; this.classes = new Set();
        const self = this;
        this.style = { setProperty(k, v) { self.styleMap[k] = v; }, removeProperty(k) { delete self.styleMap[k]; } };
        this.classList = { add(c) { self.classes.add(c); }, remove(c) { self.classes.delete(c); }, contains(c) { return self.classes.has(c); } }; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
    removeAttribute(k) { delete this.attrs[k]; }
    appendChild(c) { c.parent = this; this.children.push(c); return c; }
    addEventListener(t, fn) { (this.on[t] = this.on[t] || []).push(fn); }
    fire(t, ev) { (this.on[t] || []).forEach((f) => f(ev)); }
    focus() { this.focused = true; this.fire('focus', {}); }
    setPointerCapture(id) { this.captured = id; }
    releasePointerCapture() { this.captured = null; }
    get clientWidth() { return this.w; }
    get clientHeight() { return this.h; }
    get offsetHeight() { return this.bar || 0; }
    getBoundingClientRect() { return this.box || { width: 0, height: 0 }; }
    querySelector(sel) { const want = sel.slice(1); const walk = (n) => { for (const c of n.children) { if (c.className.split(' ').indexOf(want) !== -1) return c; const d = walk(c); if (d) return d; } return null; }; return walk(this); }
}
function rig(o) {
    o = o || {};
    const host = new Node('div', 'home'), main = new Node('main', 'hmain'), bar = new Node('header'), pl = new Node('div'), chat = new Node('section', 'gchat');
    bar.className = 'hbar'; bar.bar = 80; pl.className = 'pl'; host.appendChild(bar); host.appendChild(main); main.appendChild(pl); host.appendChild(chat);
    host.h = 800; main.w = 1280; pl.box = { width: 280, height: 500 }; chat.box = { width: 1232, height: 224 };
    const store = { data: {}, throwing: !!o.throws };
    const win = { innerWidth: o.width || 1280, wide: o.width === undefined || o.width >= 701, listeners: {}, matchMedia() { return { get matches() { return win.wide; }, addEventListener(t, f) { win.mq = f; } }; },
        addEventListener(t, f) { (win.listeners[t] = win.listeners[t] || []).push(f); },
        get localStorage() { if (store.throwing === 'access') throw new Error('blocked'); return { getItem(k) { if (store.throwing) throw new Error('full'); return k in store.data ? store.data[k] : null; }, setItem(k, v) { if (store.throwing) throw new Error('full'); store.data[k] = v; }, removeItem(k) { if (store.throwing) throw new Error('full'); delete store.data[k]; } }; } };
    if (o.stored) store.data[S.KEY] = o.stored;
    const root = new Node('html');
    const doc = { documentElement: root, getElementById(id) { return id === 'gchat' ? chat : null; }, createElement(t) { return new Node(t); } };
    const m = S.mount({ doc, win, host, main });
    return { host, main, pl, chat, bar, win, store, doc, root, m, x: main.children.find((c) => c.className.indexOf('sp-x') !== -1), y: chat.children.find((c) => c.className.indexOf('sp-y') !== -1),
        resize(w, h) { win.innerWidth = w; win.wide = w >= 701; main.w = w; host.h = h; win.listeners.resize.forEach((f) => f()); } };
}
const ptr = (x, y, id) => ({ button: 0, isPrimary: true, pointerId: id === undefined ? 1 : id, clientX: x, clientY: y, preventDefault() {} });
{
    const r = rig();
    check('both handles are there, as separators', !!r.x && !!r.y && r.x.attrs.role === 'separator' && r.y.attrs.role === 'separator');
    same('orientations and the panels they control', [r.x.attrs['aria-orientation'], r.y.attrs['aria-orientation'], r.x.attrs['aria-controls'], r.y.attrs['aria-controls']], ['vertical', 'horizontal', 'hmain', 'gchat']);
    check('both can be reached with Tab and are named', r.x.attrs.tabindex === '0' && r.y.attrs.tabindex === '0' && /Resize/.test(r.x.attrs['aria-label']) && /Resize/.test(r.y.attrs['aria-label']));
    same('the values: now, least, most', [r.x.attrs['aria-valuenow'], r.x.attrs['aria-valuemin'], r.x.attrs['aria-valuemax']], ['280', '240', String(1280 - 62 - 240)]);
    same('the values of the chat: 70% of 720', [r.y.attrs['aria-valuenow'], r.y.attrs['aria-valuemin'], r.y.attrs['aria-valuemax']], ['224', '120', '503']);
    check('nothing is set on Home before anybody drags', !('--pl-w' in r.host.styleMap) && !('--gc-h' in r.host.styleMap) && r.store.data[S.KEY] === undefined);
    // drag the players wider: the pointer goes left by 100
    r.x.fire('pointerdown', ptr(1000, 300)); check('the pointer is captured and no text selects', r.x.captured === 1 && r.root.classes.has('sp-drag-x'));
    r.x.fire('pointermove', ptr(900, 300)); same('moving left 100 makes the players 380', r.host.styleMap['--pl-w'], '380px');
    check('data-split-w is on', r.host.attrs['data-split-w'] === '1');
    r.x.fire('pointermove', ptr(0, 300)); same('and it stops at the most', r.host.styleMap['--pl-w'], (1280 - 62 - 240) + 'px');
    r.x.fire('pointermove', ptr(5000, 300)); same('and at the least', r.host.styleMap['--pl-w'], '240px');
    r.x.fire('pointermove', ptr(900, 300, 7)); same('another pointer is not heard', r.host.styleMap['--pl-w'], '240px');
    r.x.fire('pointerup', ptr(900, 300)); check('let go: no capture, no drag class, kept', r.x.captured === null && !r.root.classes.has('sp-drag-x') && JSON.parse(r.store.data[S.KEY]).p === 240);
    // a right button does nothing
    r.x.fire('pointerdown', Object.assign(ptr(1000, 300), { button: 2 })); check('a right button does not drag', r.x.captured === null);
    // chat taller by 100
    r.chat.box = { width: 1232, height: 224 };
    r.y.fire('pointerdown', ptr(600, 500)); r.y.fire('pointermove', ptr(600, 400)); same('moving up 100 makes the chat 324', r.host.styleMap['--gc-h'], '324px');
    r.y.fire('pointermove', ptr(600, -900)); same('the chat stops at 70% of the body', r.host.styleMap['--gc-h'], '503px');
    r.y.fire('pointermove', ptr(600, 9000)); same('and at 120', r.host.styleMap['--gc-h'], '120px');
    r.y.fire('lostpointercapture', {}); check('a lost capture ends the drag', !r.root.classes.has('sp-drag-y'));
    same('both sizes are kept', JSON.parse(r.store.data[S.KEY]), { p: 240, c: 120 });
    // the keyboard
    r.pl.box = { width: 300, height: 500 };
    const key = (h, k, shift) => { let stopped = false; h.fire('keydown', { key: k, shiftKey: !!shift, preventDefault() { stopped = true; } }); return stopped; };
    r.m.reset(); r.pl.box = { width: 300, height: 500 };
    check('ArrowLeft is taken (no scroll)', key(r.x, 'ArrowLeft') === true && r.host.styleMap['--pl-w'] === '316px');
    key(r.x, 'ArrowLeft', true); same('Shift moves 64', r.host.styleMap['--pl-w'], '364px');
    key(r.x, 'Home'); same('Home', r.host.styleMap['--pl-w'], '240px'); key(r.x, 'End'); same('End', r.host.styleMap['--pl-w'], (1280 - 62 - 240) + 'px');
    check('Tab is not taken', key(r.x, 'Tab') === false);
    key(r.x, 'Enter'); check('Enter resets: no property, no attribute, nothing kept for it', !('--pl-w' in r.host.styleMap) && !r.host.attrs['data-split-w']);
    r.chat.box = { width: 1232, height: 224 }; key(r.y, 'ArrowUp'); same('chat ArrowUp', r.host.styleMap['--gc-h'], '240px');
    r.y.fire('dblclick', {}); check('a double click resets the chat', !('--gc-h' in r.host.styleMap) && !r.host.attrs['data-split-h']);
    check('after both resets nothing is stored', r.store.data[S.KEY] === undefined);
}
{   // kept sizes are on Home from the first moment, and a window that is smaller clamps them
    const r = rig({ stored: '{"p":400,"c":300}' });
    same('stored sizes are put on Home at once', [r.host.styleMap['--pl-w'], r.host.styleMap['--gc-h']], ['400px', '300px']);
    r.resize(760, 560);
    same('a narrow window clamps the players (760 - 62 - 240 = 458 holds, 400 stays)', r.host.styleMap['--pl-w'], '400px');
    r.resize(700 + 1 + 0, 560); r.resize(701, 560);
    same('and the chat to 70% of 480', r.host.styleMap['--gc-h'], '300px');
    r.resize(701, 300);
    same('a low window clamps the chat (70% of 220)', r.host.styleMap['--gc-h'], '154px');
    r.resize(1280, 800);
    same('a window that grows again gives the size that was asked for back', [r.host.styleMap['--pl-w'], r.host.styleMap['--gc-h']], ['400px', '300px']);
    r.resize(390, 844);
    check('a phone: no size on Home, nothing kept', !('--pl-w' in r.host.styleMap) && !('--gc-h' in r.host.styleMap) && r.store.data[S.KEY] === undefined);
    r.resize(1280, 800); same('and back wide: the default again', r.host.styleMap['--pl-w'], undefined);
}
{   // a phone at the start takes no kept size
    const r = rig({ stored: '{"p":400,"c":300}', width: 390 });
    check('opened on a phone: kept sizes are not applied', !('--pl-w' in r.host.styleMap) && !('--gc-h' in r.host.styleMap));
}
for (const why of [true, 'access']) {   // storage that throws
    const r = rig({ throws: why });
    r.x.fire('pointerdown', ptr(1000, 300)); r.x.fire('pointermove', ptr(950, 300)); r.x.fire('pointerup', ptr(950, 300));
    same('storage that throws (' + why + '): the drag still works', r.host.styleMap['--pl-w'], '330px');
    r.y.fire('keydown', { key: 'ArrowUp', preventDefault() {} }); same('...and the keys', r.host.styleMap['--gc-h'], '240px');
    r.m.reset(); check('...and the reset', !('--pl-w' in r.host.styleMap));
}
{   // a stored text that is nonsense, a dock that comes late
    const r = rig({ stored: '{"p":"x"}' });
    check('nonsense is the default', !('--pl-w' in r.host.styleMap));
    const before = r.chat.children.length; r.m.attach(); check('attach twice makes one handle', r.chat.children.length === before);
}

// ---- 3. the loader in home.js and the source ----
check('home.js loads home/split once, after the first paint, with the page\'s lazy loader', /wantSplit\(\);/.test(homeSource) && /lazy\('home\/split', function \(\) \{ if \(win\.AntsHomeSplit\) split = win\.AntsHomeSplit\.mount\(\{ doc: doc, win: win, host: host, main: main \}\); \}\);/.test(homeSource));
check('the handles watch Home for the dock (a failure stays with them)', /new win\.MutationObserver\(attach\)\.observe\(host, \{ childList: true \}\)/.test(source));
const code = source.replace(/\/\/.*/g, '');
check('split.js is ASCII, ES5, no markup, no network', /^[\x00-\x7f]*$/.test(source) && !/\b(const|let|class|import)\b|=>|`|innerHTML|insertAdjacentHTML|XMLHttpRequest|fetch\(|WebSocket/.test(code));
check('the only storage is the one key, with try', /ants\.home\.split\.v1/.test(source) && (code.match(/localStorage/g) || []).length === 1);
console.log('splitters: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);

// The front page's height (web/home/home.js: fitHeight and heightGuard, docs/HOME_SCREEN.md "The page's height") and the screen numbers of "/?vp" (web/home/vp.js), run in node against a small fake window:
//   1. the rule: the smallest reading wins; a reading of a pinch-zoomed visual viewport, a field with the focus and a drop of more than a third are not a page that is too tall;
//   2. the guard in a fake window whose Home is as tall as the units say: it sets the limit while the box is taller than the window, takes it away when the box fits (a turn of the phone), does not
//      argue with its own limit, does nothing while a field has the focus and looks again when the field lets go, and leaves the Room alone;
//   3. "/?vp" loads the numbers' script and a plain address does not; the box has every number as text, and the script asks for nothing (no request, nothing stored).
// tests/scripts/test_web_home_height.py runs this with node. A real browser run (the page, the imitated fault, the pictures) is tests/scripts/web_home_height_browser.js.
// usage: node web_home_height_check.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const Home = require(path.resolve(__dirname, '..', '..', 'web', 'home', 'home.js'));
const Vp = require(path.resolve(__dirname, '..', '..', 'web', 'home', 'vp.js'));

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const tick = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// ---- 1. the rule ----
const base = { box: 998, inner: 998, client: 998, vv: 998, scale: 1, typing: false };
const fit = (over) => Home.fitHeight(Object.assign({}, base, over));
check('a window that shows what the units say: no limit', fit({}) === null);
check('inner height 60 less: that height', fit({ inner: 938 }) === 938);
check('client height 60 less: that height', fit({ client: 938 }) === 938);
check('visual viewport 60 less: that height', fit({ vv: 938 }) === 938);
check('the smallest of the three wins, whichever it is', fit({ inner: 960, client: 940, vv: 950 }) === 940 && fit({ inner: 940, client: 960, vv: 950 }) === 940 && fit({ inner: 960, client: 950, vv: 940 }) === 940);
check('a missing or zero reading is not a window of no height', fit({ inner: 0, client: undefined, vv: 0 }) === null && fit({ inner: 0, client: 938, vv: 0 }) === 938);
check('1 px of difference is rounding, 2 px is a limit', fit({ inner: 997 }) === null && fit({ inner: 996 }) === 996);
check('a fraction is cut down, so the box is never a hair too tall', fit({ inner: 937.6 }) === 937);
check('a pinch-zoomed visual viewport is not a short window (its height counts at scale 1 only)', fit({ vv: 500, scale: 2 }) === null && fit({ vv: 938, scale: 1.005 }) === 938);
check('a field with the focus changes nothing, however short the window', fit({ inner: 938, typing: true }) === 'keep' && fit({ inner: 500, typing: true }) === 'keep');
check('a drop of more than a third is the keyboard or a split window: no limit', fit({ inner: 600 }) === null && fit({ inner: 665 }) === null && fit({ inner: 666 }) === 666);
check('the box shorter than the window: no limit', fit({ box: 800 }) === null);

// ---- 2. the guard ----
// A window and a page that are just enough: Home's box is the natural height, or the limit when there is one; the window shows `shown` px (all three readings, or what a test says).
function world(natural, shown, onHome) {
    const listeners = {};
    const props = {};
    const w = { innerHeight: shown, listeners, timers: [] };
    const vv = { height: shown, scale: 1, listeners: {}, addEventListener(n, f) { this.listeners[n] = f; } };
    w.visualViewport = vv;
    w.addEventListener = (n, f) => { listeners[n] = f; };
    const d = { activeElement: null, listeners: {}, addEventListener(n, f) { this.listeners[n] = f; } };
    d.documentElement = { clientHeight: shown, style: { setProperty(k, v) { props[k] = v; }, removeProperty(k) { delete props[k]; }, getPropertyValue(k) { return props[k] || ''; } } };
    d.body = { getBoundingClientRect() { const p = parseFloat(props['--home-h']); const h = p > 0 ? Math.min(natural, p) : natural; return { top: 0, bottom: h, height: h }; } };
    const state = { home: onHome !== false };
    const guard = Home.heightGuard(w, d, () => state.home);
    const set = (n) => { w.innerHeight = n; vv.height = n; d.documentElement.clientHeight = n; };
    return { w, d, vv, props, guard, state, set, fire: (n) => listeners[n] && listeners[n]({}), box: () => d.body.getBoundingClientRect().height };
}
{
    const t = world(998, 938);
    check('on load: Home is 60 px too tall, the limit is set and the box fits the window', t.props['--home-h'] === '938px' && t.box() === 938, JSON.stringify(t.props));
    t.guard.fit(); t.guard.fit();
    check('the limit does not argue with itself (a second look finds the same limit)', t.props['--home-h'] === '938px' && t.box() === 938);
    t.set(998); t.fire('resize');
    check('the window gets as tall as the box (a turn, a split window ending): the limit goes', !('--home-h' in t.props) && t.box() === 998);
    t.set(448); t.fire('resize');
    check('a window of less than two thirds is not taken for a short page (no limit)', !('--home-h' in t.props));
    t.set(938); t.vv.listeners.resize && t.vv.listeners.resize({});
    check('the visual viewport\'s resize looks again', t.props['--home-h'] === '938px');
    t.set(998); t.fire('orientationchange');
    check('a turn of the phone looks at once', !('--home-h' in t.props));
}
{
    const t = world(998, 998);
    check('a window that shows all of Home: no limit is ever set', !('--home-h' in t.props));
    t.d.activeElement = { tagName: 'INPUT' };
    t.set(938); t.fire('resize');
    check('a text field has the focus and the window gets shorter: the page is not squeezed', !('--home-h' in t.props) && t.box() === 998);
    t.d.activeElement = { tagName: 'TEXTAREA' }; t.fire('resize');
    check('the same with a text area', !('--home-h' in t.props));
    t.d.activeElement = { tagName: 'DIV', isContentEditable: true }; t.fire('resize');
    check('the same with an editable box', !('--home-h' in t.props));
    t.d.activeElement = { tagName: 'BUTTON' }; t.fire('resize');
    check('a button with the focus is not a field: the page fits the window', t.props['--home-h'] === '938px');
    t.d.activeElement = { tagName: 'INPUT' }; t.set(998); t.fire('resize');
    check('a limit that was set stays while the field has the focus (nothing changes then)', t.props['--home-h'] === '938px');
    t.d.activeElement = null; t.d.listeners.focusout && t.d.listeners.focusout({});
    tick(500).then(() => {
        check('the field lets go: the page looks again after the keyboard went, and the limit goes', !('--home-h' in t.props));
        done2();
    });
}
function done2() {
    const t = world(998, 938);
    t.vv.scale = 2; t.vv.height = 400; t.fire('resize');
    check('a pinch-zoomed page with the window as before keeps the limit of the other readings', t.props['--home-h'] === '938px');
    const u = world(998, 998);
    u.vv.scale = 2; u.vv.height = 400; u.fire('resize');
    check('a pinch alone never makes a limit', !('--home-h' in u.props));
    const r = world(998, 938, false);
    check('the Room gets no limit', !('--home-h' in r.props));
    r.state.home = true; r.guard.fit();
    check('Home shown after the Room gets its limit', r.props['--home-h'] === '938px');
    r.state.home = false; r.guard.fit();
    check('the Room shown after Home takes the limit away', !('--home-h' in r.props));
    const bad = world(998, 938);
    bad.d.body.getBoundingClientRect = () => { throw new Error('no box'); };
    let threw = false; try { bad.guard.fit(); } catch (e) { threw = true; }
    check('a page that cannot be measured keeps its own height and says nothing', !threw);
    part3();
}

// ---- 3. the readout ----
function part3() {
    const UNITS = { '': 900, d: 897, s: 880, l: 910 };       // what the four units measure in the fake window (they differ, as they do on a phone)
    function page(search, extra) {
        const appended = [];
        const requests = [];
        const stored = [];
        const mk = () => { const e = { style: '', attrs: {}, listeners: {}, hidden: false, textContent: '', children: [], setAttribute(k, v) { this.attrs[k] = v; }, addEventListener(n, f) { this.listeners[n] = f; }, getBoundingClientRect() { const m = /height:100(l|s|d)?vh/.exec(this.attrs.style || ''); return { height: m ? UNITS[m[1] || ''] : 0, top: 0, bottom: 0 }; } }; return e; };
        const d = { createElement: mk, head: { appendChild(e) { appended.push(e); } }, documentElement: { clientHeight: 820, style: { getPropertyValue: () => '' } }, body: { appendChild(e) { appended.push(e); }, getBoundingClientRect: () => ({ top: 3, bottom: 897, height: 894 }) } };
        const w = Object.assign({
            location: { search }, innerWidth: 448, innerHeight: 900, devicePixelRatio: 2.5, screen: { width: 448, height: 998, availWidth: 448, availHeight: 960 },
            visualViewport: { width: 448, height: 897, offsetTop: 0, scale: 1 }, navigator: { userAgent: 'Mozilla/5.0 (Linux; Android 15; Pixel 9) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.7390.122 Mobile Safari/537.36' },
            matchMedia: (q) => ({ matches: q === '(display-mode: standalone)' }), getComputedStyle: () => ({ paddingTop: '24px' }), addEventListener() {},
            fetch() { requests.push('fetch'); }, XMLHttpRequest: function () { requests.push('xhr'); }, WebSocket: function () { requests.push('ws'); }, localStorage: { setItem() { stored.push('l'); } }, sessionStorage: { setItem() { stored.push('s'); } },
        }, extra || {});
        return { d, w, appended, requests, stored };
    }
    const plain = page('');
    check('a plain address loads nothing for the readout', Home.readout(plain.w, plain.d) === false && plain.appended.length === 0);
    for (const s of ['?room=abcd', '?vpx', '?x=vp1', '?vpn=1']) { const p = page(s); check('the address ' + s + ' is not the readout\'s', Home.readout(p.w, p.d) === false && p.appended.length === 0); }
    for (const s of ['?vp', '?vp=1', '?a=1&vp', '?vp&a=1']) {
        const p = page(s);
        check('the address ' + s + ' loads the numbers\' script (one script, from this site)', Home.readout(p.w, p.d) === true && p.appended.length === 1 && p.appended[0].src === 'home/vp.js');
    }
    const p = page('?vp');
    p.w.AntsHomeVp = Vp;
    Home.readout(p.w, p.d);
    p.appended[0].onload();
    const box = p.appended.filter((e) => e.id === 'vp-readout')[0] || {};
    check('the script mounts a box over the page that nothing else is in', p.appended.length >= 2 && box.id === 'vp-readout' && p.appended.filter((e) => e.id === 'vp-readout').length === 1);
    const text = box.textContent;
    const want = ['display-mode standalone', 'inner 448 x 900', 'client height 820', 'visual viewport 448 x 897, top 0, scale 1', 'screen 448 x 998, avail 448 x 960', 'pixel ratio 2.5', 'vh 900, dvh 897, svh 880, lvh 910',
        'safe top 24px, right 24px, bottom 24px, left 24px', 'home top 3, bottom 897, limit none', 'Chrome 141.0.7390.122, Android 15'];
    for (const x of want) check('the box says "' + x + '"', text.indexOf(x) !== -1, text);
    check('the box is text (no markup, no link)', !/<|href/.test(text) && box.innerHTML === undefined);
    check('the readout asks for nothing and keeps nothing', p.requests.length === 0 && p.stored.length === 0);
    box.listeners.click();
    check('a tap puts the box away', box.hidden === true);
    const old = page('?vp', { navigator: { userAgent: 'x' }, visualViewport: undefined, matchMedia: () => ({ matches: false }) });
    const lines = Vp.lines(old.w, old.d, { vh: 1, dvh: 2, svh: 3, lvh: 4, t: '0px', r: '0px', b: '0px', l: '0px' });
    check('a browser with no visual viewport and no version says so in words, not by failing', lines.some((l) => l === 'visual viewport none') && lines.some((l) => l === 'display-mode browser') && lines.some((l) => l === 'Chrome -, Android -'), lines.join(' | '));
    console.log('home height: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
}

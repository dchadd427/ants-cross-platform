// The counter's script (web/analytics.js, docs/ANALYTICS.md) RUN in node on a small fake page: what it sends to the hub, when, and what it never does.
//   sends:    a view with the path, the host that sent the visitor (never this site's own), the width of the window and the language; a timing of the load after the load event (not for a page that came
//             back from the browser's memory); a leave when the page is hidden and one when it is left, none while it is shown; at most five errors, each once, with the class, the message and the site's own
//             file and line (never the address of another site's script), as a Game error for a RuntimeError or the game's own file and as a Page error otherwise; nothing for "Script error." of another site;
//             the game's start once (AntsAnalytics.game), when it is a whole number of milliseconds in the range;
//   asked not to be tracked (Do Not Track as '1' or 'yes' on the navigator or on the window, the old IE word, or Global Privacy Control): one `visit` with nothing in it, no listener, no timing, no error;
//   nothing:  in a frame, on the admin page and on any /hub path, in a browser with no sendBeacon, no Blob or no JSON; a beacon that fails or a Blob that cannot be made is silent;
//   never:    a cookie, a local or a session storage (the fake throws when one is touched), a fetch, an image, a form.
// usage: node web_analytics_check.js ../../web/analytics.js      (exit 0: every check holds; 1: one fails)
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SCRIPT = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'analytics.js'));
const SOURCE = fs.readFileSync(SCRIPT, 'utf8');
let checks = 0, failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const same = (label, got, want) => check(label, JSON.stringify(got) === JSON.stringify(want), JSON.stringify(got) + ', wanted ' + JSON.stringify(want));

function page(o) {
    o = Object.assign({ path: '/matches', referrer: '', hostname: 'beta.example.org', origin: 'https://beta.example.org', width: 1300, language: 'en-US', top: true, beacon: true, blob: true, json: true,
                        ready: 'loading', version: '0.22.0', footer: null, navType: 'navigate', loadEnd: 850, visibility: 'visible', dnt: undefined, windowDnt: undefined, msDnt: undefined, gpc: undefined, fails: false }, o);
    const sent = [], listeners = {}, docListeners = {}, timers = [];
    const touched = [];
    const bar = o.version === null ? null : { getAttribute: (k) => (k === 'data-version' ? o.version : null) };
    const doc = {
        referrer: o.referrer, readyState: o.ready, visibilityState: o.visibility,
        querySelector: (s) => (s === 'header.sb[data-version]' ? bar : null),
        getElementById: (id) => (id === 'game-version' && o.footer !== null ? { textContent: o.footer } : null),
        addEventListener: (name, fn) => { (docListeners[name] = docListeners[name] || []).push(fn); },
    };
    Object.defineProperty(doc, 'cookie', { get() { touched.push('cookie'); throw new Error('cookie'); }, set() { touched.push('cookie'); throw new Error('cookie'); } });
    const nav = { language: o.language };
    if (o.beacon) {
        nav.sendBeacon = (url, blob) => {
            if (o.fails) throw new Error('no');
            sent.push({ url, type: blob.type, event: JSON.parse(blob.text) });
            return true;
        };
    }
    if (o.dnt !== undefined) nav.doNotTrack = o.dnt;
    if (o.msDnt !== undefined) nav.msDoNotTrack = o.msDnt;
    if (o.gpc !== undefined) nav.globalPrivacyControl = o.gpc;
    const win = {
        navigator: nav, document: doc, JSON, location: { pathname: o.path, hostname: o.hostname, origin: o.origin }, innerWidth: o.width,
        addEventListener: (name, fn) => { (listeners[name] = listeners[name] || []).push(fn); },
        setTimeout: (fn) => { timers.push(fn); return timers.length; },
        performance: { getEntriesByType: (t) => (t === 'navigation' ? [{ type: o.navType, loadEventEnd: o.loadEnd }] : []) },
    };
    if (o.blob) win.Blob = class { constructor(parts, opts) { this.text = parts.join(''); this.type = opts && opts.type; } };
    if (!o.json) win.JSON = undefined;
    win.self = win;
    win.top = o.top ? win : {};
    if (o.windowDnt !== undefined) win.doNotTrack = o.windowDnt;
    for (const name of ['localStorage', 'sessionStorage']) Object.defineProperty(win, name, { get() { touched.push(name); throw new Error(name); } });
    win.fetch = () => { touched.push('fetch'); throw new Error('fetch'); };
    win.Image = function () { touched.push('Image'); throw new Error('Image'); };
    vm.runInNewContext(SOURCE, Object.assign(win, { window: win, navigator: nav, document: doc }), { filename: SCRIPT });
    const fire = (name, event) => (listeners[name] || []).forEach((fn) => fn(event));
    const fireDoc = (name, event) => (docListeners[name] || []).forEach((fn) => fn(event));
    const runTimers = () => { while (timers.length) timers.shift()(); };
    return { win, doc, sent, touched, fire, fireDoc, runTimers, listeners, docListeners, kinds: () => sent.map((s) => s.event.t) };
}

// ---- the view ---------------------------------------------------------------------------------------------------------
{
    const p = page({ referrer: 'https://www.Discord.com/channels/1/2?x=3', language: 'de-CH' });
    same('one view at once', p.sent.map((s) => [s.url, s.type, s.event]), [['/hub/a', 'text/plain;charset=UTF-8', { t: 'view', p: '/matches', r: 'www.discord.com', w: 1300, l: 'de-CH' }]]);
}
check('this site is no referrer', page({ referrer: 'https://beta.example.org/play' }).sent[0].event.r === '');
check('this site in other letters is no referrer', page({ referrer: 'https://BETA.example.org/' }).sent[0].event.r === '');
check('no referrer is empty', page({ referrer: '' }).sent[0].event.r === '');
check('a referrer with no host is empty', page({ referrer: 'android-app://com.example/' }).sent[0].event.r === '');
check('a port is cut', page({ referrer: 'http://localhost:8080/x' }).sent[0].event.r === 'localhost');
check('the language may be missing', page({ language: undefined }).sent[0].event.l === '');
same('the path of the front page', page({ path: '/' }).sent[0].event.p, '/');

// ---- the load ---------------------------------------------------------------------------------------------------------
{
    const p = page({});
    same('no timing before the page has loaded', p.kinds(), ['view']);
    p.fire('load');
    same('no timing before the timer', p.kinds(), ['view']);
    p.runTimers();
    same('the timing is the end of the load event and the page\'s own version', p.sent[1].event, { t: 'perf', n: 'load', ms: 850, v: '0.22.0' });
}
{
    const p = page({ ready: 'complete' });
    p.runTimers();
    same('a page that was loaded already is timed', p.kinds(), ['view', 'perf']);
}
{
    const p = page({ navType: 'back_forward' });
    p.fire('load'); p.runTimers();
    same('a page from the browser\'s memory is not timed', p.kinds(), ['view']);
}
for (const [label, loadEnd] of [['zero', 0], ['negative', -5], ['past ten minutes', 600001], ['not a number', NaN]]) {
    const p = page({ loadEnd });
    p.fire('load'); p.runTimers();
    check('a timing that is ' + label + ' is not sent', p.kinds().length === 1);
}
check('the version of a page that has none is empty', (() => { const p = page({ version: null }); p.fire('load'); p.runTimers(); return p.sent[1].event.v === ''; })());
check('the version is read from the footer of the front page', (() => { const p = page({ version: null, footer: 'v0.22.1 ' }); p.fire('load'); p.runTimers(); return p.sent[1].event.v === '0.22.1'; })());
check('a stamp that is no version is empty', (() => { const p = page({ version: '@@GAME_VERSION@@' }); p.fire('load'); p.runTimers(); return p.sent[1].event.v === ''; })());

// ---- leaving ----------------------------------------------------------------------------------------------------------
{
    const p = page({});
    p.fireDoc('visibilitychange');
    same('shown: nothing', p.kinds(), ['view']);
    p.doc.visibilityState = 'hidden';
    p.fireDoc('visibilitychange');
    same('hidden: a leave', p.sent[1].event, { t: 'leave', p: '/matches' });
    p.fire('pagehide');
    same('left: another one', p.kinds(), ['view', 'leave', 'leave']);
}

// ---- errors -----------------------------------------------------------------------------------------------------------
{
    const p = page({});
    p.fire('error', { message: 'Uncaught TypeError: Cannot read properties of null', error: { name: 'TypeError' }, filename: 'https://beta.example.org/front/lobby_net.js?v=3', lineno: 412 });
    same('a page error: the class, the message without Uncaught, the own file and the line', p.sent[1].event, { t: 'error', k: 'Page', n: 'TypeError', m: 'Cannot read properties of null', s: 'front/lobby_net.js:412' });
    p.fire('error', { message: 'Cannot read properties of null', error: { name: 'TypeError' }, filename: 'https://beta.example.org/front/lobby_net.js?v=3', lineno: 412 });
    check('the same error is told once', p.kinds().length === 2);
    p.fire('error', { message: 'memory access out of bounds', error: { name: 'RuntimeError' }, filename: '', lineno: 0 });
    same('a RuntimeError is a Game error', p.sent[2].event, { t: 'error', k: 'Game', n: 'RuntimeError', m: 'memory access out of bounds', s: '' });
    p.fire('error', { message: 'x', error: { name: 'Error' }, filename: 'https://beta.example.org/index.js', lineno: 7 });
    check('an error in the game\'s own file is a Game error', p.sent[3].event.k === 'Game' && p.sent[3].event.s === 'index.js:7');
    p.fire('error', { message: 'y', error: { name: 'Error' }, filename: 'https://evil.example/x.js', lineno: 9 });
    same('another site\'s script is not named', [p.sent[4].event.k, p.sent[4].event.s], ['Page', '']);
    p.fire('error', { message: 'Script error.', filename: '', lineno: 0 });
    check('"Script error." of another site is nothing', p.kinds().length === 5);
    p.fire('unhandledrejection', { reason: new (class AbortError extends Error { constructor() { super('The user aborted a request.'); this.name = 'AbortError'; } })() });
    same('a refused promise: its class and message, and no file', p.sent[5].event, { t: 'error', k: 'Page', n: 'AbortError', m: 'The user aborted a request.', s: '' });
}
{
    const p = page({});
    p.fire('unhandledrejection', { reason: 'a plain text' });
    check('a refusal with a text is an Error with that text', p.sent[1].event.n === 'Error' && p.sent[1].event.m === 'a plain text');
}
{
    const p = page({});
    for (let line = 1; line <= 9; line++) p.fire('error', { message: 'message ' + line, error: { name: 'Error' }, filename: '', lineno: line });
    same('at most five errors for a page', p.kinds().filter((k) => k === 'error').length, 5);
}
check('a long message is cut', (() => { const p = page({}); p.fire('error', { message: 'x'.repeat(1000), error: { name: 'Error' }, filename: 'https://beta.example.org/a.js', lineno: 1 }); return p.sent[1].event.m.length === 300; })());

// ---- the game's start --------------------------------------------------------------------------------------------------
{
    const p = page({ path: '/play' });
    p.win.AntsAnalytics.game(4210.4);
    same('the start of the game', p.sent[1].event, { t: 'perf', n: 'game', ms: 4210, v: '0.22.0' });
    p.win.AntsAnalytics.game(5000);
    check('once', p.kinds().length === 2);
}
check('a start that is not in range is not sent', (() => { const p = page({}); p.win.AntsAnalytics.game(0); p.win.AntsAnalytics.game(NaN); p.win.AntsAnalytics.game(700000); return p.kinds().length === 1; })());

// ---- a browser that asked not to be tracked ------------------------------------------------------------------------------
for (const [label, o] of [["doNotTrack '1'", { dnt: '1' }], ["doNotTrack 'yes'", { dnt: 'yes' }], ["window.doNotTrack '1'", { windowDnt: '1' }], ["msDoNotTrack '1'", { msDnt: '1' }], ['Global Privacy Control', { gpc: true }]]) {
    const p = page(o);
    same(label + ': one visit and nothing in it', p.sent.map((s) => s.event), [{ t: 'visit' }]);
    p.fire('load'); p.runTimers(); p.doc.visibilityState = 'hidden'; p.fireDoc('visibilitychange'); p.fire('pagehide');
    p.fire('error', { message: 'x', error: { name: 'Error' }, filename: '', lineno: 1 });
    same(label + ': and nothing later', p.kinds(), ['visit']);
    check(label + ': no listener', Object.keys(p.listeners).length === 0 && Object.keys(p.docListeners).length === 0);
    check(label + ': no game timing', p.win.AntsAnalytics === undefined);
}
for (const [label, o] of [["doNotTrack '0'", { dnt: '0' }], ["doNotTrack 'unspecified'", { dnt: 'unspecified' }], ['Global Privacy Control false', { gpc: false }], ['doNotTrack null', { dnt: null }]]) {
    check(label + ' does not ask', page(o).kinds()[0] === 'view');
}

// ---- nothing -----------------------------------------------------------------------------------------------------------
for (const [label, o] of [['a frame', { top: false }], ['the admin page', { path: '/admin' }], ['a path under the admin page', { path: '/admin/x' }], ['the hub', { path: '/hub/ws' }],
                          ['no sendBeacon', { beacon: false }], ['no Blob', { blob: false }], ['no JSON', { json: false }]]) {
    const p = page(o);
    check(label + ': nothing is sent and nothing is listened to', p.sent.length === 0 && Object.keys(p.listeners).length === 0 && p.win.AntsAnalytics === undefined);
}
check('a path that only starts like the admin page is counted', page({ path: '/administrators' }).sent.length === 1);
{
    const p = page({ fails: true });
    p.fire('load'); p.runTimers(); p.fire('pagehide');
    check('a beacon that fails is silent', p.sent.length === 0);
}
{
    const all = ['/', '/play', '/matches'].map((x) => page({ path: x }));
    check('no cookie, no storage, no fetch and no image were touched', all.every((p) => p.touched.length === 0), JSON.stringify(all.map((p) => p.touched)));
    const busy = page({});
    busy.fire('load'); busy.runTimers(); busy.fire('pagehide'); busy.fire('error', { message: 'x', error: { name: 'Error' }, filename: '', lineno: 1 }); busy.win.AntsAnalytics.game(100);
    same('nothing touched in a whole visit', busy.touched, []);
}
check('only this one address is used', (() => { const p = page({}); p.fire('load'); p.runTimers(); p.fire('pagehide'); p.win.AntsAnalytics.game(100); return p.sent.every((s) => s.url === '/hub/a'); })());
check('the file is plain ES5 (no arrow, let, const, class, template text or async)', !/=>|\blet\b|\bconst\b|\bclass\b|`|\basync\b|\bawait\b/.test(SOURCE.replace(/\/\*[\s\S]*?\*\//, '').replace(/\/\/.*$/gm, '')));

console.log(failures ? failures + ' of ' + checks + ' checks failed' : 'all ' + checks + ' checks hold');
process.exit(failures ? 1 : 0);

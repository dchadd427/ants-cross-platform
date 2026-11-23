// Runs the alerts that need no permission (web/alert_core.js, web/home/alerts.js, the block of web/shell.html between ALERTS_BEGIN and ALERTS_END; docs/HOME_SCREEN.md "Alerts") in node:
//   1. the switches: on until turned off, kept in this browser, a storage that throws or holds rubbish;
//   2. the sounds: none is assigned, so none plays and no switch is drawn; with a file written in the table the sound plays once in two seconds and obeys its switch;
//   3. the tab alert: it starts only while the page is in the background, blinks once a second, and stops (title and icon put back) when the page is shown, has the focus or goes away;
//   4. the game page: the countdown card of a match comes up while the page is hidden: the alert starts, says that the match has started when the card goes, stops when the page is looked at; the page's own block
//      does it on the beat of the page's Worker timer, and not in a recording;
//   5. the front page as a whole (the page's own script, home.js and alerts.js, in the fake browser of tests/scripts/web_page_harness.js): a person who joins the room shows the alert on Home, the Room keeps the page's own
//      toast, a computer player makes none, the switch turns it off, the tab blinks in the background; the Menu's row opens the sheet (the same for a guest and for a person who is logged in), each switch turns its
//      alert off and is kept, no row for a sound, Escape and a press on the dimmed page close it.
// usage: node web_home_alerts_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const path = require('path');
globalThis.AntsShapes = require(path.resolve(__dirname, '..', '..', 'web', 'shapes.js'));

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_alerts_check.js lobby.html'); process.exit(2); }
const web = path.dirname(path.resolve(lobbyPath));
const H = require('./web_page_harness.js')(lobbyPath);
const { NET0, KEY, E, C, runLobby, THROWS } = H;
const BOT = NET0.SLOT.Bot;
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }

// ---- a fake clock, a fake window and a fake document for the core ----
function clockPage(o) {
    o = o || {};
    const timers = [];
    const page = { now: 1700000000000, timers };
    page.setTimeout = (fn, ms) => { const t = { id: timers.length + 1, at: page.now + ms, fn }; timers.push(t); return t.id; };
    page.clearTimeout = (id) => { const i = timers.findIndex((t) => t.id === id); if (i !== -1) timers.splice(i, 1); };
    page.advance = (ms) => {
        const until = page.now + ms;
        for (;;) {
            const due = timers.filter((t) => t.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            timers.splice(timers.indexOf(due), 1); page.now = Math.max(page.now, due.at); due.fn();
        }
        page.now = until;
    };
    page.core = H.loadFile('alert_core.js', { WebSocket: null, setTimeout: page.setTimeout, clearTimeout: page.clearTimeout, Date: { now: () => page.now } }).AntsAlertCore;
    const heard = { doc: {}, win: {} };
    const icon = o.noIcon ? null : { href: '/favicon.png?v=1' };
    const store = { data: {}, mode: o.storage || 'ok' };
    page.icon = icon; page.heard = heard; page.store = store;
    page.doc = {
        title: 'Ants (1998)', hidden: !!o.hidden, focused: o.unfocused ? false : true,
        hasFocus() { return !page.doc.hidden && page.doc.focused; },
        addEventListener(t, f) { (heard.doc[t] = heard.doc[t] || []).push(f); },
        querySelector() { return icon; },
        createElement() { return { width: 0, height: 0, getContext() { return { drawImage() {}, beginPath() {}, arc() {}, fill() {} }; }, toDataURL() { return 'data:image/png;base64,DOT'; } }; },
    };
    page.win = {
        addEventListener(t, f) { (heard.win[t] = heard.win[t] || []).push(f); },
        Image: o.noImage ? undefined : function () { const img = {}; Object.defineProperty(img, 'src', { set() { if (img.onload) img.onload(); } }); return img; },
        get localStorage() {
            if (store.mode === 'blocked') throw new Error('blocked');
            return { getItem(k) { if (store.mode === 'throws') throw new Error('full'); return Object.prototype.hasOwnProperty.call(store.data, k) ? store.data[k] : null; }, setItem(k, v) { if (store.mode === 'throws') throw new Error('full'); store.data[k] = String(v); } };
        },
    };
    page.fire = (where, type) => (heard[where][type] || []).slice().forEach((f) => f({}));
    page.show = () => { page.doc.hidden = false; page.doc.focused = true; page.fire('doc', 'visibilitychange'); };
    page.hide = () => { page.doc.hidden = true; page.fire('doc', 'visibilitychange'); };
    return page;
}

// ---- 1. the switches ----
{
    const p = clockPage();
    const s = p.core.settings(p.win);
    same('every switch is on until it is turned off', p.core.NAMES.map((n) => s.on(n)), [true, true, true, true]);
    check('a name that is no switch is off and cannot be set', s.on('nonsense') === false && s.set('nonsense', true) === false);
    check('turning a switch off is kept in this browser', s.set('join', false) === true && p.core.settings(p.win).on('join') === false && p.core.settings(p.win).on('start') === true);
    check('and on again', (s.set('join', true), p.core.settings(p.win).on('join') === true));
    p.store.data[p.core.KEY] = '{"join":false}'; check('what the storage holds is what counts: a change made in another tab is seen at once', s.on('join') === false);
    s.set('join', true);
    p.store.data[p.core.KEY] = 'not json'; same('rubbish in the storage is the defaults', p.core.settings(p.win).on('start'), true);
    p.store.data[p.core.KEY] = '{"join":"no","start":false,"extra":1}'; same('only a true or a false counts: a word is the default, the rest is ignored', [p.core.settings(p.win).on('join'), p.core.settings(p.win).on('start')], [true, false]);
    const t = clockPage({ storage: 'throws' }), ts = t.core.settings(t.win);
    check('a storage that throws: the switch still turns for the visit and nothing throws', ts.set('start', false) === false && ts.on('start') === false);
    const b = clockPage({ storage: 'blocked' });
    check('a browser that refuses the storage at all: on, and nothing throws', b.core.settings(b.win).on('join') === true && b.core.settings(b.win).set('join', false) === false);
}

// ---- 2. the sounds ----
{
    const p = clockPage(), set = p.core.settings(p.win);
    const made = [];
    p.win.Audio = function (src) { made.push(this); this.src = src; this.plays = 0; this.play = () => { this.plays++; return this.rejects ? Promise.reject(new Error('no gesture')) : Promise.resolve(); }; };
    const none = p.core.sounds({ win: p.win, settings: set });
    same('no sound is assigned: the table is empty', [p.core.SOUNDS.join, p.core.SOUNDS.start], ['', '']);
    check('so none plays and no audio is made', none.play('join') === false && none.play('start') === false && made.length === 0);
    same('and no switch for a sound is wired, and the sheet is "Alerts"', [p.core.wired(), p.core.title()], [[], 'Alerts']);
    const table = { join: '/home/alert_join.mp3', start: '' }, snd = p.core.sounds({ win: p.win, settings: set, table });
    check('a file in the table: the sound plays, quietly', snd.play('join', 10000) === true && made.length === 1 && made[0].src === '/home/alert_join.mp3' && made[0].plays === 1 && made[0].volume > 0 && made[0].volume <= 0.6);
    check('never twice within two seconds, again after them', snd.play('join', 10500) === false && snd.play('join', 11999) === false && snd.play('join', 12000) === true && made.length === 2);
    check('an event with no file stays silent', snd.play('start', 99000) === false);
    same('with a file the sheet says so and names its switch', [p.core.wired(table), p.core.title(table)], [['join'], 'Alerts and sounds']);
    set.set('soundJoin', false);
    check('its switch off: no sound', snd.play('join', 50000) === false && made.length === 2);
    set.set('soundJoin', true);
    p.win.Audio = function () { throw new Error('no audio here'); };
    check('a browser that cannot play: no throw', (() => { try { return snd.play('join', 90000) === true; } catch (e) { return false; } })());
}

// ---- 3. the tab alert ----
{
    const p = clockPage({ hidden: false }), f = p.core.flasher({ win: p.win, doc: p.doc });
    check('a page that is in front: nothing starts and the title is left alone', f.start('X') === false && p.doc.title === 'Ants (1998)' && !f.active() && p.timers.length === 0);
    p.hide();
    check('hidden: it starts at once, with the line, and a dot on the icon', f.start('Sam joined your room') === true && p.doc.title === 'Sam joined your room' && f.active() && p.icon.href === 'data:image/png;base64,DOT');
    p.advance(1000); check('after a second the title is the page\'s own again, and the icon', p.doc.title === 'Ants (1998)' && p.icon.href === '/favicon.png?v=1');
    p.advance(1000); check('after another it is the line again: it blinks once a second', p.doc.title === 'Sam joined your room' && p.icon.href === 'data:image/png;base64,DOT');
    f.start('A second line');
    check('a second start while it blinks changes the line', p.doc.title === 'A second line');
    p.advance(1000); f.start('A third line'); p.advance(1000);
    check('also one that comes while the page\'s own title shows: the next half of the blink says it', p.doc.title === 'A third line');
    p.show();
    check('shown again: it stops, the title and the icon are the page\'s own, and the timer is gone', !f.active() && p.doc.title === 'Ants (1998)' && p.icon.href === '/favicon.png?v=1' && p.timers.length === 0);
    p.advance(5000); check('and the title stays', p.doc.title === 'Ants (1998)');
    const t = clockPage({ hidden: true }), tf = t.core.flasher({ win: t.win, doc: t.doc });
    tf.start('Line'); t.advance(1000); t.doc.title = 'Ants - room abc'; t.advance(1000); t.advance(1000);
    t.doc.title = 'Ants - room xyz'; t.show();
    check('a title that the page changed while the alert ran is the one that comes back', t.doc.title === 'Ants - room xyz' && !tf.active());
    const u = clockPage({ hidden: true }), uf = u.core.flasher({ win: u.win, doc: u.doc });
    uf.start('Line'); u.advance(1000); u.doc.title = 'Ants - room new'; u.advance(1000); uf.stop();
    check('and the same when the alert is stopped by the page itself', u.doc.title === 'Ants - room new');
    const q = clockPage({ unfocused: true }), g = q.core.flasher({ win: q.win, doc: q.doc });
    check('a page that is visible but not the window with the focus is in the background too', g.start('Y') === true && q.doc.title === 'Y');
    q.advance(1000); q.doc.focused = true; q.fire('win', 'focus');
    check('the focus comes back: it stops', !g.active() && q.doc.title === 'Ants (1998)');
    const m = clockPage({ unfocused: true }), mf = m.core.flasher({ win: m.win, doc: m.doc });
    mf.start('Y'); m.fire('doc', 'pointerdown');
    check('a press on a page that is shown stops it, whatever the focus says', !mf.active() && m.doc.title === 'Ants (1998)');
    const w = clockPage({ unfocused: true }); let left2 = 30;
    const ww = w.core.watchMatch({ win: w.win, doc: w.doc, ask: () => left2, ready: () => true });
    w.advance(600); check('a visible page that does not have the focus counts as away', ww.flash.active());
    w.fire('doc', 'pointerdown'); w.advance(3000);
    check('a press on it says that somebody is there: the alert stops and the next look does not start it again', !ww.flash.active() && w.doc.title === 'Ants (1998)');
    w.fire('win', 'blur'); w.advance(600);
    check('the window loses the focus: it counts as away again', ww.flash.active());
    w.fire('doc', 'pointerdown'); w.hide(); w.show(); w.doc.focused = false; w.advance(600);
    check('and after the page was hidden once, an unreported focus counts again', ww.flash.active());
    const kk = clockPage({ hidden: true }), kf = kk.core.flasher({ win: kk.win, doc: kk.doc });
    kf.start('Y'); kk.fire('doc', 'keydown');
    check('a key on a page that is hidden changes nothing', kf.active());
    const r = clockPage({ hidden: true }), h = r.core.flasher({ win: r.win, doc: r.doc });
    h.start('Z'); r.fire('win', 'pagehide');
    check('a page that goes away puts the title back', !h.active() && r.doc.title === 'Ants (1998)');
    const n = clockPage({ hidden: true, noIcon: true, noImage: true }), k = n.core.flasher({ win: n.win, doc: n.doc });
    check('no icon and no way to draw one: the title alone blinks', k.start('W') === true && n.doc.title === 'W' && (n.advance(1000), n.doc.title === 'Ants (1998)'));
}

// ---- 4. the game page: my match starts ----
{
    const p = clockPage({ hidden: true });
    let left = -1, ready = false;
    const w = p.core.watchMatch({ win: p.win, doc: p.doc, ask: (what) => (what === 60 ? left : -1), ready: () => ready });
    p.advance(1000); check('the game is not ready: nothing', p.doc.title === 'Ants (1998)');
    ready = true; p.advance(1000); check('no card: nothing', p.doc.title === 'Ants (1998)');
    left = 30; p.advance(600);
    check('the card comes up while the page is hidden: the title says that the match is starting', p.doc.title === p.core.TEXT.starting && w.flash.active());
    left = -1; p.advance(1000);
    const said = [];
    for (let i = 0; i < 3; i++) { said.push(p.doc.title); p.advance(1000); }
    check('when the card goes it says that the match has started (and still blinks)', said.indexOf(p.core.TEXT.started) !== -1 && said.indexOf(p.core.TEXT.starting) === -1, JSON.stringify(said));
    p.show();
    check('the page is looked at: it stops, the title is back', !w.flash.active() && p.doc.title === 'Ants (1998)');
    left = 20; p.advance(600);
    check('the card is still up and the page is in front: no alert', !w.flash.active() && p.doc.title === 'Ants (1998)');
    p.hide(); p.advance(600);
    check('the person leaves the page during the countdown: the alert starts then', w.flash.active());
    p.show(); left = -1; p.advance(1000);

    const v = clockPage({ hidden: false });
    let up = 30;
    const wv = v.core.watchMatch({ win: v.win, doc: v.doc, ask: () => up, ready: () => true });
    v.advance(3000); check('a card that comes up while the page is in front: no alert, whatever its length', !wv.flash.active() && v.doc.title === 'Ants (1998)');

    const off = clockPage({ hidden: true });
    off.core.settings(off.win).set('start', false);
    const wo = off.core.watchMatch({ win: off.win, doc: off.doc, ask: () => 30, ready: () => true });
    off.advance(3000); check('the switch for the match is off: no alert', !wo.flash.active() && off.doc.title === 'Ants (1998)');

    const sound = clockPage({ hidden: true }), plays = [];
    sound.win.Audio = function (src) { this.play = () => { plays.push(src); }; };
    sound.core.SOUNDS.start = '/home/alert_start.mp3';
    sound.core.watchMatch({ win: sound.win, doc: sound.doc, ask: () => 30, ready: () => true });
    sound.advance(3000); check('with a sound written in the table, it plays once when the alert starts, and never while the page is in front', plays.length === 1 && plays[0] === '/home/alert_start.mp3');
    sound.core.SOUNDS.start = '';

    // the page's own block of web/shell.html, on a page of fakes
    const shell = fs.readFileSync(path.join(web, 'shell.html'), 'utf8');
    const block = shell.slice(shell.indexOf('// ALERTS_BEGIN'), shell.indexOf('// ALERTS_END'));
    check('web/shell.html has the block', block.length > 100);
    function run(o) {
        const page = clockPage({ hidden: true }), beats = [];
        const worker = o.worker === false ? null : { addEventListener(t, f) { if (t === 'message') beats.push(f); } };
        const win = Object.assign({}, page.win, { AntsAlertCore: page.core, antsBackgroundWorker: worker });
        const Module = { _ants_probe: (what) => (what === 60 ? 25 : -1) };
        new Function('window', 'document', 'Module', 'isReadyToPlay', 'ANTS_REPLAY_MODE', 'setTimeout', 'clearTimeout', 'Date', block)(win, page.doc, Module, o.ready !== false, !!o.replay, page.setTimeout, page.clearTimeout, { now: () => page.now });
        return { page, beats };
    }
    const a = run({});
    check('the block listens to the beat of the page\'s Worker timer', a.beats.length === 1 && a.page.timers.length === 0);
    a.beats[0](); check('a beat while the card is up and the page is hidden starts the alert', a.page.doc.title === p.core.TEXT.starting);
    const b = run({ worker: false }); check('a page whose Worker did not start looks on the page\'s own timer', b.page.doc.title === p.core.TEXT.starting);
    const c = run({ replay: true }); check('a recording is not watched', c.beats.length === 0 && c.page.timers.length === 0);
    const d = run({ ready: false }); d.beats[0](); check('the game is not ready: no alert', d.page.doc.title === 'Ants (1998)');
}

// ---- 5. the front page ----
function homePage(stored, options) {
    const env = runLobby('', Object.assign({ 'ants.name': 'Maya' }, stored || {}), Object.assign({ lazy: true, fakeClock: true, stayHome: true }, options || {}));
    env.advance(0);                                                                       // (the first paint: the page loads its parts)
    return env;
}
const toastOf = (env) => env.body.findAll((e) => /(^|\s)al-toast(\s|$)/.test(e.className) && !e.hidden)[0] || null;
const toastNode = (env) => env.body.findAll((e) => /(^|\s)al-toast(\s|$)/.test(e.className))[0] || null;
const shadeOf = (env) => env.body.findAll((e) => /(^|\s)al-shade(\s|$)/.test(e.className))[0] || null;
const rows = (env) => shadeOf(env).findAll((e) => e.tagName === 'button' && e.hasAttribute('data-switch'));
const joinRoom = (env, who) => env.last().receive(H.roomMessage([[C, 'Maya'], [who[0], who[1]], [E, ''], [E, '']], { you: 0, leader: 0 }));
function inRoom(env) {                                                                   // Maya holds a room (made by the page), then goes to Home
    env.makeRoom();
    env.arrive({ seat: 0, key: KEY, seats: [[C, 'Maya'], [E, ''], [E, ''], [E, '']], room: { you: 0, leader: 0 } });
}
{   // a person joins while Home shows
    const env = homePage();
    check('the part is loaded after the first paint', !!env.win.AntsHomeAlerts && env.lazyLoads.indexOf('home/alerts.js') !== -1 && env.lazyErrors.length === 0, env.lazyErrors.join());
    inRoom(env); env.toggleRoom();
    joinRoom(env, [C, 'Sam']);
    const t = toastOf(env);
    check('Home shows the alert for a person who joined: who and where, with Open', !!t && !t.hidden && /Sam joined as Red\./.test(t.textContent) && t.textContent.indexOf(env.win.AntsHomeAlerts.TEXT.open) !== -1);
    check('the page\'s own toast does not say it a second time on Home', env.$('toast').hidden === true);
    check('Open shows the Room', (t.findAll((e) => e.tagName === 'button')[0].click(), env.screen() === 'room') && t.hidden === true);
}
{   // the toast goes by itself
    const env = homePage(); inRoom(env); env.toggleRoom(); joinRoom(env, [C, 'Sam']);
    const t = toastOf(env); check('it is up', !t.hidden); env.advance(5900); check('still up before six seconds', !t.hidden); env.advance(200); check('and gone after them', t.hidden === true);
    joinRoom(env, [C, 'Sam']); joinRoom(env, [E, '']); joinRoom(env, [C, 'Eve']);
    check('a later join is the same one toast with the new words', toastOf(env) === t && toastNode(env) === t && !t.hidden && /Eve joined as Red\./.test(t.textContent));
    const told = env.body.findAll((e) => /(^|\s)al-sr(\s|$)/.test(e.className))[0];
    check('a polite status line, which is in the page for good, says it too (the toast itself is not a live region: it would be said twice)', !!told && told.getAttribute('role') === 'status' && told.getAttribute('aria-live') === 'polite' && /Eve joined as Red\./.test(told.textContent) && !t.getAttribute('role'));
}
{   // the Room screen keeps the page's own toast
    const env = homePage(); inRoom(env);
    check('the Room is showing', env.screen() === 'room');
    joinRoom(env, [C, 'Sam']);
    check('a person joins while the Room shows: the page\'s own toast says it, and Home\'s alert stays away', env.$('toast').hidden === false && /Sam joined as Red\./.test(env.$('toast').textContent) && !toastOf(env));
}
{   // a computer player, and the room the page sees first
    const env = homePage(); inRoom(env); env.toggleRoom();
    joinRoom(env, [BOT, 'Pip (bot, Easy)']);
    check('a computer player sits down: no alert, no toast, no blinking', !toastOf(env) && env.$('toast').hidden === true && env.doc.title === 'Ants (1998)');
    env.hide(true); joinRoom(env, [BOT, 'Rex (bot, Hard)']);
    check('not even in the background', env.doc.title === 'Ants (1998)');
}
{   // the background
    const env = homePage(); inRoom(env); env.toggleRoom(); env.hide(true);
    joinRoom(env, [C, 'Sam']);
    check('a person joins while the page is in the background: the title says it', env.doc.title === '● Sam joined your room');
    env.advance(1000); check('and blinks', env.doc.title === 'Ants (1998)');
    env.hide(false);
    check('the page is shown: it stops and the title is the page\'s own', env.doc.title === 'Ants (1998)' && (env.advance(5000), env.doc.title === 'Ants (1998)'));
}
{   // the switch for the join turns its alert off
    const env = homePage({ 'ants.alerts.v1': '{"join":false}' }); inRoom(env); env.toggleRoom();
    joinRoom(env, [C, 'Sam']);
    check('the switch off: no toast on Home, no second toast of the page, no blinking', !toastOf(env) && env.$('toast').hidden === true);
    env.hide(true); joinRoom(env, [E, '']); joinRoom(env, [C, 'Eve']);
    check('and none in the background', env.doc.title === 'Ants (1998)');
}
{   // the Menu's row and the sheet
    function open(env) { env.home('hmenu').click(); const box = env.body.findAll((e) => /(^|\s)hmenubox(\s|$)/.test(e.className))[0]; const item = box.findAll((e) => /(^|\s)al-menu(\s|$)/.test(e.className))[0]; item.click(); return { box, item }; }
    const env = homePage();
    const { box, item } = open(env);
    check('the Menu has a row for the alerts, after the main links and before the small ones', !!item && item.textContent.indexOf(env.win.AntsAlertCore.TEXT.sheet) !== -1 && item.textContent.indexOf(env.win.AntsAlertCore.TEXT.sheetSounds) === -1 && box.children.indexOf(item) < box.children.findIndex((c) => /msmall/.test(c.className)));
    check('it opens the sheet and the Menu goes', !!shadeOf(env) && box.hidden === true);
    check('the sheet has the two rows that work today, both on, and no row for a sound', rows(env).map((r) => r.getAttribute('data-switch')).join() === 'join,start' && rows(env).every((r) => r.getAttribute('aria-checked') === 'true'));
    const T = env.win.AntsHomeAlerts.TEXT, sheetText = shadeOf(env).textContent;
    check('the sheet says the part, its lead and the two rows, and nothing about notifications', sheetText.indexOf(T.group) !== -1 && sheetText.indexOf(T.lead) !== -1 && sheetText.indexOf(T.join[0]) !== -1 && sheetText.indexOf(T.start[0]) !== -1 && !/notification|bell|Turn on|closed/i.test(sheetText));
    const dialog = shadeOf(env).children[0];
    check('it is a dialog that holds the focus, and each row is a switch with its state', dialog.getAttribute('role') === 'dialog' && dialog.getAttribute('aria-modal') === 'true' && rows(env).every((r) => r.getAttribute('role') === 'switch'));
    const controls = shadeOf(env).findAll((e) => e.tagName === 'button');
    env.doc.activeElement = env.body; env.doc.listeners.keydown.forEach((f) => f({ key: 'Tab', preventDefault() {} }));
    check('Tab with the focus nowhere in the sheet (a press on its text) goes to its first control', env.doc.activeElement === controls[0]);
    controls[controls.length - 1].focus(); env.doc.listeners.keydown.forEach((f) => f({ key: 'Tab', preventDefault() {} }));
    check('Tab from the last control goes to the first', env.doc.activeElement === controls[0]);
    env.doc.listeners.keydown.forEach((f) => f({ key: 'Tab', shiftKey: true, preventDefault() {} }));
    check('Shift and Tab from the first goes to the last', env.doc.activeElement === controls[controls.length - 1]);
    check('the page wrote no markup of its own for the sheet and the toast', env.innerHTMLWrites.length === 0);
    rows(env)[0].click();
    check('a press turns the switch off, shows it, and keeps it in this browser', rows(env)[0].getAttribute('aria-checked') === 'false' && JSON.parse(env.storage.data['ants.alerts.v1']).join === false && JSON.parse(env.storage.data['ants.alerts.v1']).start === undefined);
    rows(env)[1].click();
    check('the other switch is its own', rows(env)[1].getAttribute('aria-checked') === 'false' && JSON.parse(env.storage.data['ants.alerts.v1']).start === false);
    rows(env)[1].click(); check('and turns back on', rows(env)[1].getAttribute('aria-checked') === 'true');
    env.body.fire('keydown', { key: 'Escape' }); env.doc.listeners.keydown.forEach((f) => f({ key: 'Escape', preventDefault() {} }));
    check('Escape closes the sheet and gives the focus back to the button that had it', !shadeOf(env) && env.doc.activeElement === env.home('hmenu'));
    open(env); const shade = shadeOf(env);
    shade.fire('pointerdown', { target: shade }); shade.fire('pointerup', { target: shade }); shade.fire('click', { target: shade });
    check('a press on the dimmed page closes it', !shadeOf(env));
    open(env); const card = shadeOf(env).children[0], sh = shadeOf(env);
    sh.fire('pointerdown', { target: card }); sh.fire('pointerup', { target: sh }); sh.fire('click', { target: sh });
    check('a press that began in the card and ended on the dimmed page does not', !!shadeOf(env));
    shadeOf(env).findAll((e) => e.tagName === 'button' && e.getAttribute('aria-label') === 'Close')[0].click();
    check('the Close button closes it', !shadeOf(env));
    check('the switch kept in the sheet is what the join alert reads', (inRoom(env), env.toggleRoom(), joinRoom(env, [C, 'Sam']), !toastOf(env) && env.$('toast').hidden === true));
}
{   // a guest and a person who is logged in see the same sheet
    for (const me of ['guest', { login: true, you: { name: 'Maya', mod: false } }, undefined]) {
        const env = homePage({}, { me });
        env.home('hmenu').click();
        const item = env.body.findAll((e) => /(^|\s)al-menu(\s|$)/.test(e.className))[0]; item.click();
        const text = shadeOf(env).textContent;
        check('the sheet for ' + (me === 'guest' ? 'a guest' : me ? 'a person who is logged in' : 'a page with no hub') + ': the top part of the picture and nothing about a login', /On this page/.test(text) && rows(env).length === 2 && !/Log in|Discord|When Ants is closed/.test(text));
    }
}
{   // a sound that is written in the table gets its row and plays with the alert; its switch obeys
    const env = homePage();
    const plays = [];
    env.win.Audio = function (src) { this.play = () => { plays.push(src); }; };
    env.win.AntsAlertCore.SOUNDS.join = '/home/alert_join.mp3';
    env.home('hmenu').click();
    env.body.findAll((e) => /(^|\s)al-menu(\s|$)/.test(e.className))[0].click();
    check('a sound in the table: its row is there, and the sheet is "Alerts and sounds"', rows(env).map((r) => r.getAttribute('data-switch')).join() === 'join,start,soundJoin' && shadeOf(env).textContent.indexOf(env.win.AntsAlertCore.TEXT.sheetSounds) !== -1);
    shadeOf(env).findAll((e) => e.tagName === 'button' && e.getAttribute('aria-label') === 'Close')[0].click();
    inRoom(env); env.toggleRoom(); joinRoom(env, [C, 'Sam']);
    check('the sound plays with the alert, once', plays.length === 1 && plays[0] === '/home/alert_join.mp3');
    env.advance(2100); joinRoom(env, [E, '']); joinRoom(env, [C, 'Eve']);
    check('and again after two seconds', plays.length === 2);
    env.win.AntsAlertCore.SOUNDS.join = '';
}

// ---- 6. the bell ----
{
    const env = homePage();
    const A = env.win.AntsHomeAlerts, core = env.win.AntsAlertCore;
    const IPHONE = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1';
    function bellOf(o) {                                                                  // a mount of its own on a bar of its own: o = { permission (none: the browser has no notifications), signedIn, ua, platform, touch, standalone (display-mode), iosStandalone }
        const calls = { asked: 0, constructed: 0, queries: 0 };
        const bar = new H.El(env, 'div', ''), menu = new H.El(env, 'button', ''), other = new H.El(env, 'button', ''); menu.className = 'btn hmenu'; other.className = 'btn hlogin'; bar.appendChild(other); bar.appendChild(menu);          // (the bar of Home's right end: the account button, then the Menu's button)
        const state = { signedIn: o.signedIn !== false, permission: o.permission, dock: null };
        const watchers = [];
        const win = Object.assign({}, env.win, {
            innerHeight: 800, addEventListener(t, f) { (watchers[t] = watchers[t] || []).push(f); },
            navigator: { userAgent: o.ua || 'Mozilla/5.0 (X11; Linux x86_64)', platform: o.platform || 'Linux x86_64', maxTouchPoints: o.touch || 0, standalone: o.iosStandalone === true ? true : undefined,
                permissions: { query() { calls.queries++; return { then(ok) { const st = {}; watchers.status = st; ok(st); } }; } } },
            matchMedia: (q) => ({ matches: /standalone/.test(q) && o.standalone === true }),
        });
        if (o.permission !== undefined) win.Notification = { get permission() { return state.permission; }, requestPermission() { calls.asked++; return { then() {} }; } };
        else delete win.Notification;
        if (o.noStorage) win.localStorage = { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); } };         // a browser that refuses its storage
        const m = A.mount({ doc: env.doc, win, core, ctl: { screen: () => 'home', show() {} }, bar, menuBtn: menu, signedIn: () => state.signedIn, dock: () => state.dock });
        const bell = bar.children.filter((e) => /(^|\s)al-bell(\s|$)/.test(e.className))[0];
        return { m, bar, bell, menu, chip: other, state, calls, watchers, win, marks: () => bell.findAll((e) => /al-mark/.test(e.className)) };
    }
    const named = (b) => ({ state: b.bell.getAttribute('data-state'), label: b.bell.getAttribute('aria-label'), hidden: b.bell.hidden });
    let b = bellOf({ permission: 'default' });
    same('logged in, notifications not set up: the bell with a dot', [named(b), b.marks().length, /dot/.test(b.marks()[0] ? b.marks()[0].className : '')], [{ state: 'off', label: 'Alerts: not set up on this device', hidden: false }, 1, true]);
    const at = (x) => b.bar.children.indexOf(x);
    check('the bell is the Menu\'s button\'s neighbor on its left, behind the account button, whoever is in the bar, and the Menu\'s button is left as it was (no dot of its own)', at(b.bell) === at(b.menu) - 1 && at(b.chip) < at(b.bell) && b.menu.className === 'btn hmenu');
    check('the bar holds the account button, the bell and the Menu\'s button, in that order, and nothing else', b.bar.children.length === 3 && b.bar.children[0] === b.chip && b.bar.children[1] === b.bell && b.bar.children[2] === b.menu);
    b = bellOf({ permission: 'granted' });
    same('notifications on: the plain bell, no dot', [named(b), b.marks().length], [{ state: 'on', label: 'Alerts: on for this device', hidden: false }, 0]);
    b = bellOf({ permission: 'denied' });
    same('the browser blocks them: the struck bell, no dot', [named(b), b.marks().length], [{ state: 'blocked', label: 'Alerts: blocked in this browser', hidden: false }, 0]);
    for (const p of ['default', 'granted', 'denied']) { b = bellOf({ permission: p, signedIn: false }); same('not logged in (' + p + '): the plain bell of the sheet, no dot, whatever the permission says', [named(b), b.marks().length], [{ state: 'guest', label: core.TEXT.sheet, hidden: false }, 0]); }
    b = bellOf({ ua: IPHONE, platform: 'iPhone' });
    same('an iPhone that has not installed the app (it has no notifications yet): the bell that needs the app, with its arrow', [named(b), b.marks().length], [{ state: 'needs', label: 'Alerts: need the app on the Home Screen', hidden: false }, 1]);
    b = bellOf({ ua: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)', platform: 'MacIntel', touch: 5 });
    check('an iPad that says it is a Mac, with a touch screen, is the same', named(b).state === 'needs');
    b = bellOf({ ua: IPHONE, platform: 'iPhone', permission: 'default', iosStandalone: true });
    check('the installed app on an iPhone (navigator.standalone) reads the permission like any browser', named(b).state === 'off');
    b = bellOf({ ua: IPHONE, platform: 'iPhone', permission: 'granted', standalone: true });
    check('and so does the display mode "standalone"', named(b).state === 'on');
    b = bellOf({ ua: IPHONE, platform: 'iPhone', iosStandalone: true });
    check('the installed app of an iOS that has no notifications: no bell', named(b).hidden === true && b.marks().length === 0);
    for (const signedIn of [true, false]) { b = bellOf({ signedIn }); check('a browser with no notifications at all (' + (signedIn ? 'logged in' : 'not logged in') + '): no bell', named(b).hidden === true && b.marks().length === 0); }
    // the state is read again
    b = bellOf({ permission: 'default', signedIn: false });
    b.state.signedIn = true; b.m.refresh();
    check('the visitor logs in: the state is read again', named(b).state === 'off' && b.marks().length === 1);
    b.state.permission = 'granted'; b.m.refresh();
    check('the permission changes: the bell and the dot follow', named(b).state === 'on' && b.marks().length === 0 && b.bell.getAttribute('title') === b.bell.getAttribute('aria-label'));
    b.state.permission = 'denied'; (b.watchers.status.onchange)();
    check('the browser says that the permission changed: the bell follows', named(b).state === 'blocked');
    b.state.permission = 'default'; (b.watchers.focus || []).forEach((f) => f({}));
    check('the window gets the focus again: the bell is read again', named(b).state === 'off');
    // the chat dock comes after the bell: the bell asks it to tell who the visitor is, and reads again when it does
    b = bellOf({ permission: 'default', signedIn: false });
    check('the dock is not there yet: the plain bell', named(b).state === 'guest');
    b.state.signedIn = true; b.state.dock = { tap(fn) { b.state.tapped = fn; } }; env.advance(600);
    check('the dock comes: the bell asks it to tell, and reads again', typeof b.state.tapped === 'function' && named(b).state === 'off');
    b.state.signedIn = false; b.state.tapped('who'); check('a list of players is not news of the visitor', named(b).state === 'off');
    b.state.tapped('lost'); check('the link was lost and the visitor is not known: the plain bell', named(b).state === 'guest');
    b.state.signedIn = true; b.state.tapped('live'); check('the welcome says who the visitor is: the dot', named(b).state === 'off');
    b.state.signedIn = false; b.state.tapped('you'); check('the visitor logs out: the plain bell', named(b).state === 'guest');
    // a press opens the sheet, and the browser is never asked
    b = bellOf({ permission: 'default' });
    (b.watchers.focus || []).forEach((f) => f({})); (b.win.document || env.doc).listeners.visibilitychange && env.doc.listeners.visibilitychange.forEach((f) => f({}));
    check('reading the state asks the browser for nothing', b.calls.asked === 0 && b.calls.constructed === 0);
    b.bell.click();
    check('a press opens the sheet of the alerts', !!shadeOf(env) && /Alerts/.test(shadeOf(env).textContent));
    check('and the browser is still not asked', b.calls.asked === 0 && b.win.Notification.permission === 'default');
    env.doc.listeners.keydown.forEach((f) => f({ key: 'Escape', preventDefault() {} }));
    check('Escape closes it and the focus is on the bell', !shadeOf(env) && env.doc.activeElement === b.bell);
    for (const k of ['granted', 'denied']) { b = bellOf({ permission: k }); b.bell.click(); check('a press while ' + k + ' is the same: the sheet, no question', !!shadeOf(env) && b.calls.asked === 0); env.doc.listeners.keydown.forEach((f) => f({ key: 'Escape', preventDefault() {} })); }
    b = bellOf({ ua: IPHONE, platform: 'iPhone' }); b.bell.click();
    check('a press on the iPhone bell opens the sheet too (the explanation of the app comes with the install button)', !!shadeOf(env));
    env.doc.listeners.keydown.forEach((f) => f({ key: 'Escape', preventDefault() {} }));
    // in step 1 the dot means "not looked yet": it goes for good, in this browser, once the Alerts sheet has been opened
    const escape = () => env.doc.listeners.keydown.forEach((f) => f({ key: 'Escape', preventDefault() {} }));
    env.storage.removeItem(core.LOOKED_KEY);
    b = bellOf({ permission: 'default' });
    check('a browser that has not looked at the alerts: the dot on the bell', b.marks().length === 1);
    (b.watchers.focus || []).forEach((f) => f({})); b.m.refresh();
    check('reading the state again is not looking at the sheet: the dot stays', b.marks().length === 1 && !shadeOf(env));
    b.bell.click();
    same('the sheet is opened with the bell: the dot is gone from the bell, and the bell is there, named as before', [named(b), b.marks().length], [{ state: 'off', label: 'Alerts: not set up on this device', hidden: false }, 0]);
    escape();
    (b.watchers.focus || []).forEach((f) => f({})); b.m.refresh();
    check('and it stays gone when the bell is read again', b.marks().length === 0 && named(b).state === 'off');
    b = bellOf({ permission: 'default' });
    check('a later visit of this browser: no dot on the bell', b.marks().length === 0 && named(b).state === 'off' && b.bell.hidden === false);
    check('what is kept is that the sheet was looked at, and nothing else (no switch is written by it)', env.storage.getItem(core.LOOKED_KEY) === '1' && env.storage.getItem(core.KEY) === null);
    b.state.permission = 'granted'; b.m.refresh(); b.state.permission = 'default'; b.m.refresh();
    check('a permission that comes and goes does not bring the dot back', b.marks().length === 0);
    env.storage.removeItem(core.LOOKED_KEY);
    b = bellOf({ permission: 'default' });
    b.m.row(() => null).click();
    check('the Menu\'s row opens the sheet, and that is looking at it too: the dot is gone', !!shadeOf(env) && b.marks().length === 0);
    escape();
    env.storage.removeItem(core.LOOKED_KEY);
    b = bellOf({ permission: 'default' }); b.m.open(null); escape();
    check('the sheet opened any other way is the same', b.marks().length === 0);
    env.storage.removeItem(core.LOOKED_KEY);
    b = bellOf({ ua: IPHONE, platform: 'iPhone' }); b.bell.click(); escape();
    check('the arrow of an iPhone that has not installed the app is not that dot: it stays', b.marks().length === 1 && named(b).state === 'needs');
    env.storage.removeItem(core.LOOKED_KEY);
    b = bellOf({ permission: 'default', noStorage: true });
    check('a browser that refuses its storage: the dot at first', b.marks().length === 1);
    b.bell.click(); escape(); b.m.refresh();
    check('and it goes for this visit', b.marks().length === 0);
    env.storage.removeItem(core.LOOKED_KEY);
}
{   // the page's own run: Home builds the bell in front of its account button, and the Menu's button gets the dot
    const asked = [];
    const env = runLobby('', { 'ants.name': 'Maya' }, { lazy: true, fakeClock: true, stayHome: true, win: { Notification: { permission: 'default', requestPermission() { asked.push(1); } }, navigator: { userAgent: 'Mozilla/5.0 (X11; Linux x86_64)', platform: 'Linux x86_64', maxTouchPoints: 0 } } });
    env.advance(0);
    const right = env.home('hright'), first = right.children[1];
    check('on the page, the bell is between the account button and the Menu\'s button, and the visitor is not logged in (the chat dock is not here): the plain bell', right.children.length === 3 && /hlogin/.test(right.children[0].className) && /al-bell/.test(first.className) && /hmenu/.test(right.children[2].className) && first.getAttribute('data-state') === 'guest');
    first.click();
    check('its press opens the sheet and asks nothing of the browser', !!shadeOf(env) && asked.length === 0);
}

// ---- what the scripts never do ----
{
    const files = ['alert_core.js', path.join('home', 'alerts.js')].map((f) => fs.readFileSync(path.join(web, f), 'utf8').replace(/\/\/.*/g, ''));
    check('the scripts read the notification permission and never ask for it, and use no worker, no network and no markup', files.every((t) => !/requestPermission|new\s+(window\.)?Notification|showNotification|serviceWorker|pushManager|fetch\(|XMLHttpRequest|WebSocket|sendBeacon|innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b/.test(t)));
}

console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);

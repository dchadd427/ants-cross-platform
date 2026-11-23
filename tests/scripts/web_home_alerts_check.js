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
    same('every switch is on until it is turned off', p.core.NAMES.map((n) => s.on(n)), [true, true, true, true, true, true]);
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
    env.win.AntsHomeChat = H.loadFile('home/chat.js', env.scope).AntsHomeChat;             // (the chat dock's script, which the alerts read their text rules from, is on the page a moment later; the dock itself is not mounted here)
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
const bellOfPage = (env) => env.home('hright').children.filter((e) => /(^|\s)al-bell(\s|$)/.test(e.className))[0];
const escapeKey = (env) => env.doc.listeners.keydown.forEach((f) => f({ key: 'Escape', preventDefault() {} }));
const panelOf = (env) => (shadeOf(env) ? shadeOf(env).findAll((e) => /(^|\s)al-panel(\s|$)/.test(e.className))[0] : null);
const buttonsOf = (root, label) => root.findAll((e) => e.tagName === 'button' && e.getAttribute('aria-label') === label);
{   // the bell opens the list, the gear in it the settings (the switches are the same for a visitor)
    const env = homePage();
    const bell = bellOfPage(env), T = env.win.AntsHomeAlerts.TEXT;
    check('the bar has the bell between its other buttons and the Menu\'s, and the Menu has no row of alerts any more', !!bell && env.home('hright').children.indexOf(bell) === env.home('hright').children.length - 2 && (env.home('hmenu').click(), !env.body.findAll((e) => /(^|\s)al-menu(\s|$)/.test(e.className)).length));
    env.home('hmenu').click();
    bell.click();
    const list = shadeOf(env);
    check('a press opens the panel with the list: nothing yet, one line of where it is kept, and a line that says to log in', !!list && /No alerts yet/.test(list.textContent) && list.textContent.indexOf(T.keptBrowser) !== -1 && list.textContent.indexOf(T.loginNote) !== -1 && rows(env).length === 0);
    const dialog = panelOf(env);
    check('it is a dialog that holds the focus, with a gear and a close button', dialog.getAttribute('role') === 'dialog' && dialog.getAttribute('aria-modal') === 'true' && buttonsOf(dialog, T.gear).length === 1 && buttonsOf(dialog, T.close).length === 1);
    buttonsOf(dialog, T.gear)[0].click();
    check('the gear shows the settings in the same panel: the two switches that a visitor has, both on, and no row for a sound or for an invitation', rows(env).map((r) => r.getAttribute('data-switch')).join() === 'join,start' && rows(env).every((r) => r.getAttribute('aria-checked') === 'true'));
    const sheetText = shadeOf(env).textContent;
    check('the settings say the part, its lead, the rows, where the list is kept and the button that empties it, and nothing about notifications', sheetText.indexOf(T.settings) !== -1 && sheetText.indexOf(T.group) !== -1 && sheetText.indexOf(T.lead) !== -1 && sheetText.indexOf(T.join[0]) !== -1 && sheetText.indexOf(T.start[0]) !== -1 && sheetText.indexOf(T.clearBrowser) !== -1 && !/notification|permission/i.test(sheetText.replace(T.lead, '')));
    check('each row is a switch with its state', rows(env).every((r) => r.getAttribute('role') === 'switch'));
    const controls = shadeOf(env).findAll((e) => e.tagName === 'button');
    env.doc.activeElement = env.body; env.doc.listeners.keydown.forEach((f) => f({ key: 'Tab', preventDefault() {} }));
    check('Tab with the focus nowhere in the panel goes to its first control', env.doc.activeElement === controls[0]);
    controls[controls.length - 1].focus(); env.doc.listeners.keydown.forEach((f) => f({ key: 'Tab', preventDefault() {} }));
    check('Tab from the last control goes to the first', env.doc.activeElement === controls[0]);
    env.doc.listeners.keydown.forEach((f) => f({ key: 'Tab', shiftKey: true, preventDefault() {} }));
    check('Shift and Tab from the first goes to the last', env.doc.activeElement === controls[controls.length - 1]);
    check('the page wrote no markup of its own for the panel and the toast', env.innerHTMLWrites.length === 0);
    rows(env)[0].click();
    check('a press turns the switch off, shows it, and keeps it in this browser', rows(env)[0].getAttribute('aria-checked') === 'false' && JSON.parse(env.storage.data['ants.alerts.v1']).join === false && JSON.parse(env.storage.data['ants.alerts.v1']).start === undefined);
    rows(env)[1].click();
    check('the other switch is its own', rows(env)[1].getAttribute('aria-checked') === 'false' && JSON.parse(env.storage.data['ants.alerts.v1']).start === false);
    rows(env)[1].click(); check('and turns back on', rows(env)[1].getAttribute('aria-checked') === 'true');
    escapeKey(env);
    check('Escape goes back from the settings to the list', !!shadeOf(env) && rows(env).length === 0 && /No alerts yet/.test(shadeOf(env).textContent));
    escapeKey(env);
    check('Escape closes the list and gives the focus back to the bell', !shadeOf(env) && env.doc.activeElement === bell);
    bell.click(); const shade = shadeOf(env);
    shade.fire('pointerdown', { target: shade }); shade.fire('pointerup', { target: shade }); shade.fire('click', { target: shade });
    check('a press on the page outside the panel closes it', !shadeOf(env));
    bell.click(); const card = panelOf(env), sh = shadeOf(env);
    sh.fire('pointerdown', { target: card }); sh.fire('pointerup', { target: sh }); sh.fire('click', { target: sh });
    check('a press that began in the panel and ended outside it does not', !!shadeOf(env));
    buttonsOf(shadeOf(env), T.close)[0].click();
    check('the Close button closes it', !shadeOf(env));
    bell.click(); bell.click();
    check('a second press on the bell closes it', !shadeOf(env));
    check('the switch kept in the settings is what the join alert reads', (inRoom(env), env.toggleRoom(), env.doc.title = 'Ants (1998)', env.advance(7000), env.storage.data['ants.alerts.v1'] = '{"join":false}', joinRoom(env, [C, 'Sam']), !toastOf(env) && env.$('toast').hidden === true));
    env.storage.data['ants.alerts.v1'] = '{}';
}
{   // a person who joins the room is an alert of the visitor's own list, kept in this browser
    const env = homePage(); inRoom(env); env.toggleRoom();
    const bell = bellOfPage(env), marks = () => bell.findAll((e) => /al-mark/.test(e.className)).map((e) => e.textContent);
    joinRoom(env, [C, 'Sam']);
    same('a person joined: the bell has a 1, and the list is kept in this browser', [marks(), JSON.parse(env.storage.data['ants.alerts.list.v1']).map((r) => [r.k, r.name, r.seat, r.u])], [['1'], [['join', 'Sam', 'Red', 1]]]);
    bell.click();
    const item = shadeOf(env).findAll((e) => /(^|\s)al-item(\s|$)/.test(e.className))[0];
    check('the list says who joined and where, in gold while it is unread, with Open', /Sam joined your room/.test(item.textContent) && /as Red/.test(item.textContent) && /unread/.test(item.className) && /Open/.test(item.textContent));
    check('the number is still on the bell while the list is open', marks().join() === '1');
    escapeKey(env);
    check('the list is closed: the alert is read and the number goes, in the list and in the storage', marks().length === 0 && JSON.parse(env.storage.data['ants.alerts.list.v1'])[0].u === 0);
    bell.click(); const again = shadeOf(env).findAll((e) => /(^|\s)al-item(\s|$)/.test(e.className))[0];
    check('and the next time the entry is plain', !/unread/.test(again.className));
    again.findAll((e) => e.tagName === 'button')[0].click();
    check('Open shows the Room and closes the list', env.screen() === 'room' && !shadeOf(env));
    joinRoom(env, [E, '']); joinRoom(env, [C, 'Eve']);
    bell.click(); buttonsOf(panelOf(env), env.win.AntsHomeAlerts.TEXT.gear)[0].click();
    shadeOf(env).findAll((e) => e.hasAttribute('data-clear'))[0].click();
    same('the settings\' button that empties the list empties a visitor\'s own list too: the bell, the list and what this browser keeps', [marks(), JSON.parse(env.storage.data['ants.alerts.list.v1']), /No alerts yet/.test(shadeOf(env).textContent)], [[], [], true]);
}
{   // what the hub's list does for a visitor who is not logged in is never kept: a reload has the same list, a 7-day limit and 30 rows
    const stored = [];
    for (let n = 1; n <= 35; n++) stored.push({ id: n, at: 1700000000 - (n === 1 ? 8 * 86400 : 0) + n, k: 'join', u: 0, name: 'P' + n, seat: 'Blue' });
    stored.push({ id: 99, at: 1700000000, k: 'mention', u: 1, from: 'x', text: 'y' }, { id: 98, at: 1700000000, k: 'join', u: 1, name: '<b>', seat: 'Purple' });
    const env = homePage({ 'ants.alerts.list.v1': JSON.stringify(stored) }, {});
    const bell = bellOfPage(env);
    bell.click();
    const items = shadeOf(env).findAll((e) => /(^|\s)al-item(\s|$)/.test(e.className));
    check('a list in this browser: only the alerts of a visitor (a person joined), the newest 30 of the last 7 days, newest first', items.length === 30 && /P35 joined/.test(items[0].textContent) && !/P1 joined/.test(shadeOf(env).textContent) && !/mentioned/.test(shadeOf(env).textContent));
    escapeKey(env);
    const broken = homePage({ 'ants.alerts.list.v1': 'not json' }, {});
    check('a list that is rubbish is an empty one', bellOfPage(broken).findAll((e) => /al-mark/.test(e.className)).length === 0);
}

// ---- 6. the list of a person who is logged in: the hub's rows, the bell, the toasts, what a press does ----
{
    const env = homePage();
    const A = env.win.AntsHomeAlerts, core = env.win.AntsAlertCore, T = A.TEXT;
    const Chat = H.loadFile('home/chat.js', { WebSocket: null, setTimeout() { return 0; }, clearTimeout() {} }).AntsHomeChat;
    const NOW = 1700000000;
    // a mount of its own on a bar of its own, with a fake dock: o = { signedIn (default true), chat (the chat is on the screen), screen, noBlock }
    function mountOf(o) {
        o = o || {};
        const bar = new H.El(env, 'div', ''), menu = new H.El(env, 'button', ''); menu.className = 'btn hmenu'; bar.appendChild(menu);
        const calls = [], sent = [], blocks = [], state = { signedIn: o.signedIn !== false, chat: !!o.chat, taps: [], dockUp: !o.late };
        const dock = { tap(fn) { state.taps.push(fn); }, send: o.noSend ? undefined : (obj) => { sent.push(obj); return true; }, onScreen: () => state.chat, note() {}, you: () => (state.signedIn ? { name: 'Maya' } : null),
            block: o.noBlock ? undefined : (draw, cls) => { const b = { draw, cls, redraws: 0 }; blocks.push(b); return { redraw() { b.redraws++; } }; } };
        const win = Object.assign({}, env.win, { AntsHomeChat: Chat, innerHeight: 800, navigator: { userAgent: 'x' } });
        const m = A.mount({ doc: env.doc, win, core, ctl: { screen: () => o.screen || 'home', show: (n, how) => calls.push(['show', n, how]), join: (row, back) => { calls.push(['join', row.code, row.from]); return true; }, chat: () => calls.push(['chat']) },
            bar, menuBtn: menu, signedIn: () => state.signedIn, dock: () => (state.dockUp ? dock : null), now: () => NOW });
        const bell = bar.children.filter((e) => /(^|\s)al-bell(\s|$)/.test(e.className))[0];
        const tap = (kind, m1) => state.taps.forEach((f) => f(kind, m1));
        const marks = () => bell.findAll((e) => /al-mark/.test(e.className)).map((e) => e.textContent);
        return { m, bar, bell, menu, calls, sent, blocks, state, tap, marks, dock };
    }
    const INVITE = (id, o) => Object.assign({ id, at: NOW - 60, k: 'invite', u: 1, from: 'Clover', map: 'TREASURE.LVL', n: 3, of: 4, o: 1, code: 'abc234' }, o || {});
    const MENTION = (id, o) => Object.assign({ id, at: NOW - 1920, k: 'mention', u: 1, from: 'tess', text: 'which map are we on?' }, o || {});
    const JOIN = (id, o) => Object.assign({ id, at: NOW - 840, k: 'join', u: 0, name: 'juniper', seat: 'Red' }, o || {});
    const items = (b) => (shadeOf(env) ? shadeOf(env).findAll((e) => /(^|\s)al-item(\s|$)/.test(e.className)) : []);
    const escape = () => escapeKey(env);
    const toast = () => { const all = env.body.findAll((e) => /(^|\s)al-toast(\s|$)/.test(e.className)), last = all[all.length - 1]; return last && !last.hidden ? last : null; };       // (the toast of the newest mount: every mount of this section makes its own)
    let b = mountOf();
    same('no alerts: the plain bell with no number, named for a screen reader', [b.marks(), b.bell.getAttribute('aria-label'), b.bell.getAttribute('aria-haspopup')], [[], 'Alerts', 'dialog']);
    b.tap('alerts', { rows: [MENTION(3), INVITE(4), JOIN(2, { u: 1 }), INVITE(1, { u: 0, o: undefined, code: undefined })] });
    same('the list from the hub: three unread, the number on the bell, named with it', [b.marks(), b.bell.getAttribute('aria-label')], [['3'], 'Alerts, 3 unread']);
    b.bell.click();
    same('the list is as the hub sent it, newest first, each with its words and its button', items().map((i) => [i.getAttribute('data-k'), i.getAttribute('data-id')]), [['mention', '3'], ['invite', '4'], ['join', '2'], ['invite', '1']]);
    const words = items().map((i) => i.textContent);
    check('an invitation says who, the map, how many are in and how long ago, with Join', /Clover invited you/.test(words[1]) && /Treasure/.test(words[1]) && /3 of 4 in/.test(words[1]) && /1 min ago/.test(words[1]) && /Join/.test(words[1]));
    check('a mention says who, the start of the line and how long ago, with Open', /tess mentioned you/.test(words[0]) && /which map are we on\?/.test(words[0]) && /32 min ago/.test(words[0]) && /Open/.test(words[0]));
    check('a person who joined says who and as which color, with Open', /juniper joined your room/.test(words[2]) && /as Red/.test(words[2]) && /14 min ago/.test(words[2]));
    check('an invitation whose room is gone is dim, with no button and the words of the picture', /dim/.test(items()[3].className) && /The room has closed\./.test(words[3]) && items()[3].findAll((e) => e.tagName === 'button').length === 0);
    check('the unread ones are marked, the read ones are plain', items().map((i) => /unread/.test(i.className)).join() === 'true,true,true,false' && shadeOf(env).textContent.indexOf(T.keptAccount) !== -1 && shadeOf(env).textContent.indexOf(T.loginNote) === -1);
    check('the number is on the bell while the list is open', b.marks().join() === '3');
    escape();
    same('the list is closed: the hub is told that everything up to the newest is read, and the number goes', [b.sent, b.marks()], [[{ t: 'seen', upto: 4 }], []]);
    b.bell.click(); b.sent.length = 0; escape();
    check('nothing unread: nothing is told again', b.sent.length === 0);
    // a press on a button
    b = mountOf(); b.tap('alerts', { rows: [MENTION(3), INVITE(4), JOIN(2, { u: 1 })] });
    b.bell.click(); items()[1].findAll((e) => e.tagName === 'button')[0].click();
    same('Join on an invitation closes the list, counts as read and asks Home to join that room', [!!shadeOf(env), b.calls, b.sent, b.marks()], [false, [['join', 'abc234', 'Clover']], [{ t: 'seen', upto: 4 }], []]);
    b.bell.click(); items()[0].findAll((e) => e.tagName === 'button')[0].click();
    check('Open on a mention shows the chat', b.calls.pop().join() === 'chat' && !shadeOf(env));
    b.bell.click(); items()[2].findAll((e) => e.tagName === 'button')[0].click();
    check('Open on a person who joined shows the Room', b.calls.pop().join() === 'show,room,push');
    // the number
    b = mountOf(); b.tap('alerts', { rows: Array.from({ length: 12 }, (_, i) => JOIN(i + 1, { u: 1 })) });
    check('more than nine unread: "9+"', b.marks().join() === '9+' && /12 unread/.test(b.bell.getAttribute('aria-label')));
    // a row that cannot be (more people in the room than it has seats), and a list that is not for a visitor
    b = mountOf(); b.tap('alerts', { rows: [INVITE(5, { n: 4, of: 2 }), MENTION(6)] });
    same('a row whose room holds more people than it has seats is dropped, the others are kept', b.marks(), ['1']);
    b = mountOf({ signedIn: false }); b.tap('alerts', { rows: [MENTION(6)] });
    b.state.signedIn = true; b.tap('you');
    same('a list that comes while the page does not know the visitor is not kept: when the visitor is known, the bell waits for the hub\'s own list', b.marks(), []);
    // the dock comes after the bell: the bell asks it to tell, a while later
    b = mountOf({ late: true });
    same('the dock is not there yet: no tap is made', b.state.taps.length, 0);
    b.state.dockUp = true; env.advance(600);
    same('the dock comes a little later: the bell asks it, and takes its news', [b.state.taps.length, (b.tap('alerts', { rows: [MENTION(3)] }), b.marks())], [1, ['1']]);
    // the news that the hub sends back of a person who joined the room is not news again (the page that holds the room said it)
    b = mountOf(); env.doc.hidden = false;
    b.tap('alert', { row: JOIN(8, { u: 1 }) });
    check('a join that the hub sends back is in the list and on the bell, and says nothing again: no toast, no blinking', b.marks().join() === '1' && !toast() && env.doc.title === 'Ants (1998)');
    // a member's own list is the account's: the join is told to the hub, and nothing of it is kept in this browser
    delete env.storage.data['ants.alerts.list.v1'];
    b = mountOf({ noSend: true }); b.m.alert('join', { name: 'Sam', text: 'Sam joined as Red.', seat: 'Red' });
    check('a member whose dock cannot send yet keeps nothing of the account\'s list in this browser', !env.storage.data['ants.alerts.list.v1'] && b.marks().length === 0);
    b = mountOf(); b.m.alert('join', { name: 'Sam', text: 'Sam joined as Red.', seat: 'Red' });
    same('a member: the join goes to the hub as a note, and not into this browser', [b.sent, env.storage.data['ants.alerts.list.v1']], [[{ t: 'note', k: 'join', name: 'Sam', seat: 'Red' }], undefined]);
    // a new alert from the hub
    b = mountOf();
    b.tap('alert', { row: INVITE(7) });
    const t = toast();
    check('an invitation while the chat is not on the screen: a toast that says who and where, with Join', !!t && /Clover invited you to Treasure \(3 of 4\)\./.test(t.textContent) && /Join/.test(t.textContent) && b.marks().join() === '1');
    env.advance(9900); check('it stays for ten seconds', !t.hidden); env.advance(200); check('and goes after them', t.hidden === true);
    b.tap('alert', { row: INVITE(8) }); toast().findAll((e) => e.tagName === 'button')[0].click();
    same('Join on the toast asks Home to join the room, and the toast goes', [b.calls.pop(), toast()], [['join', 'abc234', 'Clover'], null]);
    b.tap('alert', { row: INVITE(8) });
    check('the same alert twice is one entry', b.marks().join() === '2');
    b.tap('alert', { row: MENTION(9) });
    check('a mention while the chat is not on the screen: a toast with Open (6 seconds)', /tess mentioned you in the chat\./.test(toast().textContent) && /Open/.test(toast().textContent) && (env.advance(5900), !toast().hidden) && (env.advance(200), toast() === null));
    b.tap('alert', { row: MENTION(10) }); toast().findAll((e) => e.tagName === 'button')[0].click();
    check('Open on that toast shows the chat', b.calls.pop().join() === 'chat');
    // the chat is on the screen: one place at a time
    b = mountOf({ chat: true });
    b.tap('alert', { row: INVITE(7) });
    same('the chat is on the screen: the invitation is a line of the chat (the dock\'s own block), no toast, the bell has it', [b.blocks.length, toast(), b.marks()], [1, null, ['1']]);
    const line = new H.El(env, 'div', ''); b.blocks[0].draw(line);
    check('the line says who invited, where, how many are in, with Join', /Clover/.test(line.textContent) && /invited you to their room/.test(line.textContent) && /Treasure/.test(line.textContent) && /3 of 4 in/.test(line.textContent) && line.findAll((e) => e.tagName === 'button').map((e) => e.textContent).join() === 'Join');
    line.findAll((e) => e.tagName === 'button')[0].click();
    check('its Join does what the toast\'s does', b.calls.pop().join() === 'join,abc234,Clover');
    b.tap('alerts', { rows: [INVITE(7, { o: undefined, code: undefined })] });
    const dim = new H.El(env, 'div', ''); b.blocks[0].draw(dim);
    check('the hub says the room is gone: the line is drawn again, dim, with no button and the words of the picture', b.blocks[0].redraws === 1 && /The room has closed\./.test(dim.textContent) && dim.findAll((e) => e.tagName === 'button').length === 0 && /dim/.test(dim.className));
    b.tap('alert', { row: MENTION(9) });
    same('a mention with the chat on the screen is the line\'s own mark: no toast, no second block, the bell has it', [toast(), b.blocks.length, b.marks()], [null, 1, ['2']]);
    // the switches: the entry is kept, nothing is shown
    env.storage.data['ants.alerts.v1'] = '{"invite":false,"mention":false}';
    b = mountOf(); env.doc.hidden = false;
    b.tap('alert', { row: INVITE(7) }); b.tap('alert', { row: MENTION(9) });
    same('the switches off: both are in the list and on the bell, and nothing is shown, in the chat or over it', [b.marks(), toast(), b.blocks.length], [['2'], null, 0]);
    b = mountOf({ chat: true }); b.tap('alert', { row: INVITE(7) });
    check('and not as a line of the chat either', b.blocks.length === 0);
    env.storage.data['ants.alerts.v1'] = '{}';
    // the background
    env.hide(true); b = mountOf();
    b.tap('alert', { row: INVITE(7) });
    check('an invitation while the page is in the background: the tab\'s title says who', env.doc.title === '● Clover invited you');
    env.hide(false); env.doc.title = 'Ants (1998)';
    env.hide(true); b.tap('alert', { row: MENTION(9) });
    check('a mention in the background says so too', env.doc.title === '● tess mentioned you');
    env.hide(false); env.doc.title = 'Ants (1998)';
    // the settings of a person who is logged in
    b = mountOf(); b.bell.click();
    buttonsOf(shadeOf(env), T.gear)[0].click();
    same('a person who is logged in has the two new switches before the other two', rows(env).map((r) => r.getAttribute('data-switch')).join(), 'invite,mention,join,start');
    check('they say what they are', shadeOf(env).textContent.indexOf(T.invite[0]) !== -1 && shadeOf(env).textContent.indexOf(T.mention[0]) !== -1 && shadeOf(env).textContent.indexOf(T.clearAccount) !== -1);
    rows(env)[0].click();
    check('the switch is kept in this browser', JSON.parse(env.storage.data['ants.alerts.v1']).invite === false);
    env.storage.data['ants.alerts.v1'] = '{}';
    b.tap('alerts', { rows: [MENTION(3)] });
    shadeOf(env).findAll((e) => e.getAttribute('data-clear') === 'yes')[0].click();
    same('Clear my alerts tells the hub, empties the list and shows it', [b.sent.pop(), b.marks(), /No alerts yet/.test(shadeOf(env).textContent)], [{ t: 'clearalerts' }, [], true]);
    escape();
    // a person joins the room of a person who is logged in: the hub keeps it, this page keeps none
    b = mountOf(); b.sent.length = 0;
    check('a person joined: told to the hub as a note, kept in no list of this browser', b.m.alert('join', { name: 'Sam', text: 'Sam joined as Red.', seat: 'Red' }) === true && b.sent.length === 1 && JSON.stringify(b.sent[0]) === JSON.stringify({ t: 'note', k: 'join', name: 'Sam', seat: 'Red' }) && !env.storage.data['ants.alerts.list.v1']);
    b.sent.length = 0; b.m.alert('join', { name: 'Sam', text: 'x', seat: 'Purple' });
    check('a seat that is no color is told to nobody', b.sent.length === 0);
    b.tap('alert', { row: JOIN(5) });
    same('and the row that the hub sends back is in the list without a second toast or blink', [b.marks(), env.doc.title], [[], 'Ants (1998)']);
    // a visitor and a hub that stops
    b = mountOf({ signedIn: false });
    b.tap('alerts', { rows: [MENTION(3)] });
    check('a list from a hub is not a visitor\'s: the visitor\'s own is in this browser', b.marks().length === 0);
    b = mountOf(); b.tap('alerts', { rows: [MENTION(3)] }); b.tap('live', {});
    check('a new welcome waits for the hub\'s list again', b.marks().length === 0);
    b.tap('alerts', { rows: 'nonsense' }); b.tap('alerts', { rows: [{ id: 'x' }] }); b.tap('alert', { row: { id: 1, k: 'pm' } }); b.tap('alert', null);
    check('a frame that is not a list is ignored', b.marks().length === 0);
    // say(): the page's own words
    b = mountOf(); check('say(words) is a toast; the gold one is a refusal', b.m.say('Invitation sent to juniper.') === true && /Invitation sent to juniper\./.test(toast().textContent) && !/warn/.test(toast().className) && (b.m.say('Too many invitations.', 'warn'), /warn/.test(toast().className)));
    check('a toast with no action has no button', toast().findAll((e) => e.tagName === 'button').length === 0);
    // the words of a row, and the rows themselves
    const rules = env.win.AntsLobbyRules;
    same('a row that is not one is dropped, a field of strangers is cleaned, a map that is not a file name is none', [
        A.rowOf({ id: 1, at: NOW, k: 'invite', from: 'A', n: 5, of: 4 }, Chat, rules), A.rowOf({ id: 1, at: NOW, k: 'pm', from: 'A' }, Chat, rules), A.rowOf({ id: 1, at: 'now', k: 'mention', from: 'A' }, Chat, rules), A.rowOf(null, Chat, rules),
        A.rowOf({ id: 2, at: NOW, k: 'invite', from: 'Bo‏b', n: 1, of: 4, map: '../x', o: 1, code: 'Not A Code' }, Chat, rules)], [null, null, null, null, { id: 2, at: NOW, k: 'invite', u: false, from: 'Bob', n: 1, of: 4, map: '', code: '', open: false }]);
    same('a mention keeps its words as text, cut to eighty and the dots', [A.rowOf(MENTION(1, { text: '<b>x</b> ' + 'w'.repeat(200) }), Chat, rules).text.length, A.rowOf(MENTION(1, { text: '<b>x</b>' }), Chat, rules).text], [81, '<b>x</b>']);
    same('the unread are counted, and nine is the most that the bell says', [A.countOf([{ u: true }, { u: false }, { u: true }]), A.countText(9), A.countText(10)], [2, '9', '9+']);
    same('a visitor\'s list: the last seven days, the newest thirty, newest first', A.trim([{ id: 1, at: NOW - 8 * 86400 }, { id: 2, at: NOW }, { id: 3, at: NOW }], NOW).map((r) => r.id), [3, 2]);
    check('the list is never a place for the browser to be asked for anything, and nothing is written as markup', env.innerHTMLWrites.length === 0);
}
{   // a sound that is written in the table gets its row and plays with the alert; its switch obeys
    const env = homePage();
    const plays = [];
    env.win.Audio = function (src) { this.play = () => { plays.push(src); }; };
    env.win.AntsAlertCore.SOUNDS.join = '/home/alert_join.mp3';
    bellOfPage(env).click();
    check('a sound in the table: the list is "Alerts and sounds"', shadeOf(env).textContent.indexOf(env.win.AntsAlertCore.TEXT.sheetSounds) !== -1);
    buttonsOf(shadeOf(env), env.win.AntsHomeAlerts.TEXT.gear)[0].click();
    check('and its row is in the settings', rows(env).map((r) => r.getAttribute('data-switch')).join() === 'join,start,soundJoin');
    escapeKey(env); escapeKey(env);
    inRoom(env); env.toggleRoom(); joinRoom(env, [C, 'Sam']);
    check('the sound plays with the alert, once', plays.length === 1 && plays[0] === '/home/alert_join.mp3');
    env.advance(2100); joinRoom(env, [E, '']); joinRoom(env, [C, 'Eve']);
    check('and again after two seconds', plays.length === 2);
    env.win.AntsAlertCore.SOUNDS.join = '';
}

// ---- what the scripts never do ----
{
    const files = ['alert_core.js', path.join('home', 'alerts.js')].map((f) => fs.readFileSync(path.join(web, f), 'utf8').replace(/\/\/.*/g, ''));
    check('the scripts read the notification permission and never ask for it, and use no worker, no network and no markup', files.every((t) => !/requestPermission|new\s+(window\.)?Notification|showNotification|serviceWorker|pushManager|fetch\(|XMLHttpRequest|WebSocket|sendBeacon|innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b/.test(t)));
}

console.log(checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);

// Runs the sheet "Your name", the guest's chip and the welcome card (web/home/names.js, docs/HOME_CHAT.md "Your name") in the fake browser of tests/scripts/web_page_harness.js (its DOM), with a fake dock (the
// hub's frames in and out), a fake clock and a fake person (front/player_name.js: the guest's name that this browser keeps). What it checks:
//   1. the pure parts: the day and the month of a time, the words for a wait, and the sentence under a field for every word that the hub says, in each of the three kinds (a nickname, a tag, a guest's
//      name): a refusal is red, a wait is a lock with its dates, a name that is taken names the name that was asked and nobody else, a word that the page does not know is "could not check";
//   2. the guest's chip: only when the dock is live, nobody is logged in and the Log in button shows; its letter and its name; and the welcome card, over an inert page, with the focus in it: it shows to a
//      guest who has no name and has not answered, only where Home is calm, and never again in this browser (also not after a reload) once a name is set, "Set a display name" or the sheet was opened, "Not now"
//      or Escape was pressed, or the person logged in; "Sign in with Discord to chat" presses the page's Log in button and answers nothing; Tab stays in the card and the focus goes back to the chip; a browser
//      that keeps nothing shows the card again with the next page, and the page still works;
//   3. a guest's sheet: the name is asked of the hub a moment after the last key (one frame, `guest: true`, no tag), the answer is said under the field, Save waits for a good answer, a good name is kept
//      by the person and the chip changes, a name that the person refuses leaves the sheet open, an answer that is not the last question is not heard;
//   4. a logged-in person's sheet: opened by the dock's tap, with the nickname and the tag that the account has; the check frame carries both fields and `save: false`, Save sends `save: true` and the sheet
//      closes with the hub's `saved`; the preview shows "[TAG] Nickname"; a name that is the person's own now is "your name now" and cannot be saved again; a nickname that changed lately is locked with its
//      dates; a refusal leaves Save off; no connection and a rate answer say so; a person who logs out closes the sheet;
//   5. nothing is markup: a name that is HTML stays text, the page writes no innerHTML.
// tests/scripts/test_web_home_names.py runs this with node (the quick tier). usage: node web_home_names_check.js web/lobby.html     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');

const lobbyPath = process.argv[2];
if (!lobbyPath) { console.log('usage: web_home_names_check.js lobby.html'); process.exit(2); }
const lobbyDir = path.dirname(path.resolve(lobbyPath));
const Names = require(path.join(lobbyDir, 'home', 'names.js'));
const Letters = require(path.join(lobbyDir, 'front', 'name_letters.js'));
const H = require('./web_page_harness.js')(lobbyPath);
const { El } = H;

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}
const cls = (e) => String(e.className).split(/\s+/);
const byClass = (root, c) => root.findAll((e) => cls(e).indexOf(c) !== -1);
const one = (root, c) => byClass(root, c)[0] || null;
const helps = (root) => byClass(root, 'nm-h').filter((h) => !h.hidden).map((h) => h.textContent);
const button = (root, text) => root.findAll((e) => e.tagName === 'button' && e.textContent === text)[0] || null;

// 1 June 2026, 12:00 UTC (a Monday) is the fake clock's start; the page's own time zone is the machine's, so the checks that name a day use the machine's day of the same instant
const T0 = Date.UTC(2026, 5, 1, 12, 0, 0);
const DAY = 86400;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fakes
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function rig(o) {
    o = o || {};
    const env = { elements: {}, innerHTMLWrites: [] };
    let now = T0;
    const timers = [];
    let nextTimer = 1;
    const setTimeout_ = (fn, ms) => { const id = nextTimer++; timers.push({ id, at: now + (ms || 0), fn }); return id; };
    const clearTimeout_ = (id) => { const at = timers.findIndex((t) => t.id === id); if (at !== -1) timers.splice(at, 1); };
    const advance = (ms) => {
        const until = now + ms;
        for (;;) {
            const due = timers.filter((t) => t.at <= until).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) break;
            timers.splice(timers.indexOf(due), 1);
            now = Math.max(now, due.at);
            due.fn();
        }
        now = until;
    };
    class FakeDate extends Date { static now() { return now; } }
    const store = Object.assign({}, o.store || {});
    const storage = o.noStorage ? { getItem() { throw new Error('no storage'); }, setItem() { throw new Error('no storage'); } } : { getItem: (k) => (Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } };
    const doc = {
        hidden: false, listeners: {}, capturing: {}, activeElement: null, head: new El(env, 'head', ''), body: new El(env, 'body', ''), documentElement: { clientWidth: 1280 },
        createElement: (t) => { const e = new El(env, t); e.style = { setProperty(k, v) { this[k] = v; } }; return e; }, createTextNode: (t) => ({ isText: true, textContent: String(t), parent: null }), addEventListener() {}, removeEventListener() {}
    };
    env.doc = doc;
    const win = { innerWidth: 1280, innerHeight: 800, Date: FakeDate, localStorage: storage, setTimeout: setTimeout_, clearTimeout: clearTimeout_, addEventListener() {}, AntsNameLetters: Letters };
    const bar = new El(env, 'div', 'bar');
    const login = new El(env, 'button', 'login');
    bar.appendChild(login);
    doc.body.appendChild(bar);
    const sent = [];
    let taps = [];
    const state = { live: o.live !== false, you: o.you === undefined ? null : o.you, sendFails: !!o.sendFails, calm: o.calm !== false };
    const dock = {
        state: () => (state.live ? 'live' : 'down'), you: () => state.you,
        send: (m) => { if (state.sendFails) return false; sent.push(m); return true; },
        tap: (fn) => { taps.push(fn); }
    };
    const typed = { name: o.typed || '', refuse: false };
    const person = {
        typed: () => typed.name,
        guestShown: () => (typed.name ? typed.name + ' (Guest)' : 'Guest 550000'),
        setTyped: (n) => { if (typed.refuse) return false; typed.name = n; return true; }
    };
    const changes = [];
    let clicks = 0;
    login.click = () => { clicks++; };
    const api = Names.mount({ doc, win, dock, bar, login, person, letters: Letters, calm: () => state.calm, changed: () => changes.push(1) });
    const r = { env, doc, win, bar, login, dock, api, sent, typed, person, store, changes, state, advance, now: () => now, clicks: () => clicks };
    r.tell = (kind, m) => taps.forEach((fn) => fn(kind, m));
    if (state.live) r.tell('live', { login: !o.noLogin });       // (the welcome of a hub: it says whether the hub has a login)
    r.chip = () => one(bar, 'nm-gchip');
    r.card = () => one(doc.body, 'nm-welcome');
    r.shade = () => one(doc.body, 'nm-wshade');
    r.key = (key, o2) => { let prevented = false; r.shade().fire('keydown', Object.assign({ key, stopPropagation() {}, preventDefault() { prevented = true; } }, o2 || {})); return prevented; };
    r.buttons = () => ({ sign: one(r.card(), 'nm-wdc'), set: one(r.card(), 'nm-wset'), not: one(r.card(), 'nm-wnot') });
    r.sheet = () => one(doc.body, 'nm-sheet');
    r.nick = () => doc.body.findAll((e) => e.id === 'nm-nick')[0] || null;
    r.tag = () => doc.body.findAll((e) => e.id === 'nm-tag')[0] || null;
    r.save = () => one(doc.body, 'nm-save');
    r.type = (input, text) => { input.value = text; input.fire('input', {}); };
    r.msgs = () => byClass(doc.body, 'nm-msg').filter((m) => !m.hidden).map((m) => m.textContent);
    r.preview = () => { const p = one(doc.body, 'nm-pn'); return p ? p.textContent : null; };
    r.last = () => sent[sent.length - 1];
    r.live = () => { state.live = true; r.tell('live', { login: !o.noLogin }); };
    return r;
}
const ME = (o) => Object.assign({ name: 'maple.leaf', nick: '', tag: '', nickat: 0, nickfree: 0, tagfree: 0 }, o);
const OK = { ok: true };

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 1. The pure parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const at = (s) => { const d = new Date(s * 1000); return d.getDate() + ' ' + ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][d.getMonth()]; };
    const t = T0 / 1000;
    same('dayOf: the day and the short month of a time in seconds', [Names.dayOf(t, null), Names.dayOf(t + 40 * DAY, null)], [at(t), at(t + 40 * DAY)]);
    same('inWords: minutes under an hour (at least one), hours above, one in the singular', [Names.inWords(t + 30, t), Names.inWords(t + 40 * 60, t), Names.inWords(t + 3600, t), Names.inWords(t + 5 * 3600 + 1, t), Names.inWords(t - 100, t)],
        ['in 1 minute', 'in 40 minutes', 'in 1 hour', 'in 6 hours', 'in 1 minute']);
    const time = { date: (s) => 'D' + s, words: (s) => 'W' + s };
    const say = (k, f, asked, cur) => Names.say(k, f, asked, time, cur);
    same('say: nothing said is nothing shown', [say('nick', null, 'Zoe'), say('nick', 5, 'Zoe'), say('tag', undefined, 'ANT')], [{ text: '', tone: '' }, { text: '', tone: '' }, { text: '', tone: '' }]);
    same('say: a good nickname is "free" with the name, a good guest name is "fine", a good tag says nothing, the person\'s own name is "your name now"',
        [say('nick', OK, 'Zoe'), say('guest', OK, 'Wren'), say('tag', OK, 'ANT'), say('nick', OK, 'Zoe', true)],
        [{ text: 'Zoe is free.', tone: 'ok' }, { text: 'That name is fine.', tone: 'ok' }, { text: '', tone: '' }, { text: 'That is your name now.', tone: 'ok' }]);
    same('say: taken names the name that was asked and nobody else', [say('nick', { ok: false, why: 'taken' }, 'Zoe'), say('guest', { ok: false, why: 'taken' }, 'Zoe')], [{ text: 'Zoe is taken. Pick another one.', tone: 'bad' }, { text: 'Zoe is taken. Pick another one.', tone: 'bad' }]);
    const lookalike = say('nick', { ok: false, why: 'lookalike' }, 'Zoe');
    check('say: a lookalike is red and names nobody (not the name asked, not another)', lookalike.tone === 'bad' && lookalike.text.indexOf('Zoe') === -1 && /another name/.test(lookalike.text), lookalike.text);
    same('say: a nickname that changed lately says when it changed and when it can again (30 days between); a tag says how long until it can change', [say('nick', { ok: false, why: 'soon', until: 1000000 }, 'Zoe'), say('tag', { ok: false, why: 'soon', until: 5000 }, 'ANT')],
        [{ text: 'You changed your nickname on D' + (1000000 - 30 * DAY) + '. You can change it again on D1000000.', tone: 'lock' }, { text: 'You changed your tag today. You can change it again W5000.', tone: 'lock' }]);
    const words = ['short', 'long', 'chars', 'script', 'alphabets', 'reserved', 'rude', 'lookalike', 'busy', 'rate'];
    const missing = words.filter((w) => !Names.WHY.nick[w] || !Names.WHY.guest[w]);
    same('WHY: every word of a nickname and of a guest name that the hub can say has a sentence', missing, []);
    same('WHY: every word of a tag that the hub can say has a sentence', ['short', 'long', 'chars', 'script', 'alphabets', 'reserved', 'staff', 'rude', 'bot', 'nick', 'busy', 'rate'].filter((w) => !Names.WHY.tag[w]), []);
    same('say: a word that the page does not know is "could not check" (red), never the word itself', [say('nick', { ok: false, why: 'zzz' }, 'Zoe'), say('nick', { ok: false }, 'Zoe'), say('nick', { ok: false, why: '__proto__' }, 'Zoe')],
        [{ text: Names.TEXT.offline, tone: 'bad' }, { text: Names.TEXT.offline, tone: 'bad' }, { text: Names.TEXT.offline, tone: 'bad' }]);
    const sentences = [].concat(...['nick', 'tag', 'guest'].map((k) => Object.keys(Names.WHY[k]).filter((w) => w !== 'empty').map((w) => Names.WHY[k][w])));       // (an empty field is not asked: its word says nothing)
    check('WHY: no sentence is a word of the hub or contains markup', sentences.every((s) => typeof s === 'string' && s.length > 10 && !/[<>]/.test(s)));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 2. The guest's chip and the welcome card
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const KEY = 'ants.guestwelcome';
    let r = rig({});
    check('chip: a guest on a live dock sees the chip, with the first letter and the shown name', !!r.chip() && !r.chip().hidden && one(r.chip(), 'gc-av').textContent === 'G' && one(r.chip(), 'gc-cn').textContent === 'Guest 550000', r.chip() && r.chip().textContent);
    check('chip: it stands before the Log in button', r.bar.children.indexOf(r.chip()) === r.bar.children.indexOf(r.login) - 1);
    check('chip: its label says what to do', /Set a name/.test(r.chip().getAttribute('aria-label')), r.chip().getAttribute('aria-label'));

    // the card of a first visit
    const box = r.card();
    check('welcome: a guest with no name and no answer sees the card on the first visit', !!box && !!r.shade());
    check('welcome: it is a dialog named by its title and described by its line', box.getAttribute('role') === 'dialog' && box.getAttribute('aria-modal') === 'true'
        && (r.doc.body.findAll((e) => e.id === box.getAttribute('aria-labelledby'))[0] || {}).textContent === 'Welcome to Ants & Uncles'
        && (r.doc.body.findAll((e) => e.id === box.getAttribute('aria-describedby'))[0] || {}).textContent === 'Chatting needs a Discord sign-in. You can play and watch without one.');
    same('welcome: the words of the buttons and their names for a screen reader', [r.buttons().sign.textContent, r.buttons().set.textContent, r.buttons().set.getAttribute('aria-label'), r.buttons().not.textContent],
        ['Sign in with Discord to chat', 'Set a display nameif you don\u2019t want to chat', 'Set a display name, if you don\u2019t want to chat', 'Not now']);
    check('welcome: the focus is in the card itself, so that a reader begins with the title and the line', r.doc.activeElement === box);
    check('welcome: the page behind it is inert (a key, a tap and a reader), the card is not', r.bar.hasAttribute('inert') && !r.shade().hasAttribute('inert') && !box.hasAttribute('inert'));
    check('welcome: the chip is there behind it with its dashed badge', !r.chip().hidden);
    check('welcome: nothing is written to the browser by showing it', r.store[KEY] === undefined);
    {
        const tags = r.doc.body.findAll(() => true).map((e) => e.tagName);
        check('welcome: it makes only the elements of a card, and no markup', tags.every((t) => ['body', 'div', 'button', 'h2', 'p', 'i', 'span', 'small'].indexOf(t) !== -1) && r.env.innerHTMLWrites.length === 0, JSON.stringify(Array.from(new Set(tags))));
    }

    // Tab stays in the card
    const wrapped = (key, shift, from) => { from.focus(); const prevented = r.key(key, { shiftKey: shift }); return { prevented, at: r.doc.activeElement }; };
    const b = r.buttons();
    same('welcome: Tab on the last button goes to the first, Shift+Tab on the first goes to the last, on the card itself to the last', [wrapped('Tab', false, b.not).at === b.sign, wrapped('Tab', true, b.sign).at === b.not, wrapped('Tab', true, box).at === b.not], [true, true, true]);
    same('welcome: Tab and Shift+Tab inside the card are left to the browser (no cancel, the focus stays)', [wrapped('Tab', false, b.sign).prevented, wrapped('Tab', false, b.set).prevented, wrapped('Tab', true, b.not).prevented, wrapped('Tab', false, box).prevented], [false, false, false, false]);
    check('welcome: a press beside the card gives the focus back to it', (r.doc.activeElement = null, r.shade().fire('click', { target: r.shade() }), r.doc.activeElement === box));
    check('welcome: a press inside it does not', (b.set.focus(), r.shade().fire('click', { target: b.set }), r.doc.activeElement === b.set));

    // Not now
    b.not.click();
    check('welcome: "Not now" closes it, wakes the page, puts the focus on the chip, keeps the chip and writes the one key', r.card() === null && r.shade() === null && !r.bar.hasAttribute('inert') && r.doc.activeElement === r.chip() && !r.chip().hidden && r.store[KEY] === '1' && r.sheet() === null);
    r.tell('shown'); r.tell('live', { login: true }); r.tell('you', {});
    check('welcome: it does not come back in this page', r.card() === null);
    r = rig({ store: { [KEY]: '1' } });
    check('welcome: a browser that answered once never sees it again (a new page, the same browser)', r.card() === null && !!r.chip());

    // Escape
    r = rig({});
    check('welcome: Escape is "Not now": the same close, the focus on the chip, the key (and the key goes no further)', (() => { let stopped = false; r.shade().fire('keydown', { key: 'Escape', stopPropagation() { stopped = true; } }); return stopped && r.card() === null && r.doc.activeElement === r.chip() && r.store[KEY] === '1' && !r.bar.hasAttribute('inert'); })());
    r = rig({});
    r.key('Enter');
    check('welcome: another key does nothing', !!r.card() && r.store[KEY] === undefined);

    // Set a display name
    r = rig({});
    r.buttons().set.click();
    check('welcome: "Set a display name" closes it, opens the guest\'s sheet with the field in focus, and answers the welcome', r.card() === null && !!r.sheet() && r.doc.activeElement === r.nick() && r.store[KEY] === '1' && !r.bar.hasAttribute('inert'));
    byClass(r.sheet(), 'rs-x')[0].click();
    check('welcome: closing the sheet without a name does not bring the card back (the sheet is one tap away on the chip)', r.card() === null && r.sheet() === null && !!r.chip() && !r.chip().hidden);
    r.tell('shown');
    check('welcome: ...not even when the page is painted again', r.card() === null);
    r = rig({ calm: false });
    r.chip().click();
    byClass(r.sheet(), 'rs-x')[0].click();
    r.state.calm = true; r.tell('shown');
    check('welcome: a sheet that was opened from the chip answers the welcome too', r.card() === null && r.store[KEY] === '1');

    // Sign in with Discord to chat
    r = rig({});
    r.buttons().sign.click();
    check('welcome: "Sign in with Discord to chat" presses the page\'s own Log in button, closes the card and puts the focus on the chip', r.clicks() === 1 && r.card() === null && r.doc.activeElement === r.chip() && !r.bar.hasAttribute('inert'));
    check('welcome: ...and answers nothing for good (a person who comes back as a guest from Discord is asked once more)', r.store[KEY] === undefined);
    r.tell('shown');
    check('welcome: ...but the same page asks no second time', r.card() === null && r.clicks() === 1);
    r.state.you = ME({}); r.tell('you', {});
    check('welcome: a person who logs in has answered it for good', r.store[KEY] === '1');
    r.state.you = null; r.tell('you', {});
    check('welcome: ...and as a guest again has no card', r.card() === null);

    // who sees it, and when
    r = rig({});
    r.state.you = ME({}); r.tell('you', {});
    check('welcome: logging in while the card shows takes it away and wakes the page', r.card() === null && r.chip().hidden === true && !r.bar.hasAttribute('inert') && r.store[KEY] === '1');
    r = rig({ you: ME({}) });
    check('welcome: a person who is logged in sees none, has no chip, and has answered it for good', r.card() === null && r.chip() === null && r.store[KEY] === '1');
    r.state.you = null; r.tell('you', {});
    check('welcome: ...and logging out brings the chip back without the card', r.chip().hidden === false && r.card() === null);
    r = rig({ noLogin: true });
    check('welcome: a site with no login has no card (there is nothing to sign in to)', r.card() === null && r.chip() === null && r.store[KEY] === undefined);
    r = rig({ typed: 'Wren' });
    check('welcome: a guest who has a name sees none, and the chip has the name and its own label', r.card() === null && one(r.chip(), 'gc-cn').textContent === 'Wren' && one(r.chip(), 'gc-av').textContent === 'W' && /Change it/.test(r.chip().getAttribute('aria-label')));
    r = rig({ live: false });
    check('chip: no chip and no card while the dock is not live (the page does not know yet who this is)', r.chip() === null && r.card() === null);
    r.live();
    check('chip: the chip comes with the dock, and so does the card', !!r.chip() && !r.chip().hidden && !!r.card());
    r = rig({ live: false });
    r.login.hidden = true;
    r.live();
    check('chip: the welcome says that the hub has a login while the Log in button is not shown yet (the dock shows it a moment later): the chip and the card are there all the same', !!r.chip() && !r.chip().hidden && !!r.card());
    r = rig({ calm: false });
    check('welcome: not while Home is not calm (the Room shows, a dialog or a hand-off holds the page), and the chip is there', r.card() === null && !!r.chip() && !r.chip().hidden && r.store[KEY] === undefined);
    r.state.calm = true; r.tell('shown');
    check('welcome: it comes when Home is calm again and the page is painted again', !!r.card());
    r.state.calm = false; r.tell('shown');
    check('welcome: ...and goes, without an answer, when Home is not calm again', r.card() === null && r.store[KEY] === undefined && !r.bar.hasAttribute('inert'));
    r = rig({ noStorage: true });
    check('welcome: a browser that keeps nothing shows the card, and the page works', !!r.card() && !!r.chip());
    r.buttons().not.click();
    r.tell('shown');
    check('welcome: ...and "Not now" closes it for the page', r.card() === null && !!r.chip());
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. A guest's sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({});
    r.chip().click();
    check('guest sheet: the chip opens it, the welcome card goes, there is one field (no tag) with a counter', !!r.sheet() && r.card() === null && !!r.nick() && r.tag() === null && one(r.sheet(), 'nm-cnt').textContent === '0 / 16');
    check('guest sheet: Save is off and nothing was asked yet', r.save().disabled === true && r.sent.length === 0);
    same('guest sheet: the empty field says "Pick a name" and, under it, which letters a name may hold', [r.nick().placeholder, helps(r.sheet())], ['Pick a name', [Names.TEXT.nickHelp]]);
    check('guest sheet: it says what the others see now (the number is the name until one is set) and has the rules for guests and the login button', /Guest 550000/.test(r.preview()) && /until you set one/.test(one(r.sheet(), 'nm-ps').textContent) && byClass(r.sheet(), 'nm-rules')[0].children.length === 3 && !!button(r.sheet(), 'Log in with Discord'));
    r.type(r.nick(), 'Wr');
    r.advance(100);
    r.type(r.nick(), 'Wren');
    r.advance(300);
    check('guest sheet: nothing is asked until the typing stops (350 ms after the last key)', r.sent.length === 0);
    r.advance(100);
    same('guest sheet: then one frame goes, a guest check with the name and no tag', r.sent, [{ t: 'name', r: 1, nick: 'Wren', guest: true }]);
    check('guest sheet: the counter counts characters, not bytes', (r.type(r.nick(), '\u00c4ngel\u00e4'), one(r.sheet(), 'nm-cnt').textContent === '6 / 16'));
    r.advance(400);
    const q = r.last().r;
    r.tell('name', { t: 'name', r: q - 1, ok: true, nick: OK, tag: null, shown: 'Old (Guest)', saved: false });
    check('guest sheet: an answer to an earlier question is not heard', r.msgs().length === 0 && r.save().disabled === true);
    r.tell('name', { t: 'name', r: q, ok: false, nick: { ok: false, why: 'taken' }, tag: null, shown: '', saved: false });
    same('guest sheet: a refusal is said under the field with the name that was asked', r.msgs(), ['\u00c4ngel\u00e4 is taken. Pick another one.']);
    check('guest sheet: while the hub has something to say the line of letters is gone', helps(r.sheet()).length === 0);
    check('guest sheet: the field is red and Save stays off', cls(one(r.sheet(), 'nm-in')).indexOf('err') !== -1 && r.save().disabled === true);
    r.type(r.nick(), 'Wren');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, ok: true, nick: OK, tag: null, shown: 'Wren (Guest)', saved: false });
    same('guest sheet: a good name is "fine", the preview shows "Name (Guest)", Save is on', [r.msgs(), r.preview(), r.save().disabled], [['That name is fine.'], 'Wren (Guest)', false]);
    r.typed.refuse = true;
    r.save().click();
    check('guest sheet: a name that the person refuses leaves the sheet open and the chip as it was', !!r.sheet() && r.changes.length === 0 && one(r.chip(), 'gc-cn').textContent === 'Guest 550000');
    r.typed.refuse = false;
    r.save().click();
    check('guest sheet: Save keeps the name in the person, closes the sheet, changes the chip and tells the page', r.sheet() === null && r.typed.name === 'Wren' && one(r.chip(), 'gc-cn').textContent === 'Wren' && r.changes.length === 1);
    check('guest sheet: the welcome is never shown again (one key), and Save sent nothing more to the hub', r.store['ants.guestwelcome'] === '1' && r.card() === null && r.sent.every((m) => m.guest === true));
    r.chip().click();
    check('guest sheet: it opens again with the name that was kept', r.nick().value === 'Wren');
    r.type(r.nick(), '   ');
    r.advance(1000);
    check('guest sheet: a field of blanks asks nothing and says nothing', r.sent.length === 3 && r.msgs().length === 0 && r.save().disabled === true);
    byClass(r.sheet(), 'rs-x')[0].click();
    check('guest sheet: the cross closes it', r.sheet() === null);
    r.chip().click();
    r.sheet().fire('keydown', { key: 'Escape', stopPropagation() {} });       // (a key of a field reaches the sheet: it bubbles)
    check('guest sheet: Escape closes it', r.sheet() === null);
    r.chip().click();
    button(r.sheet(), 'Log in with Discord').click();
    check('guest sheet: "Log in with Discord" closes the sheet and presses the page\'s own Log in button', r.sheet() === null && r.clicks() === 1);
    r.chip().click();
    r.type(r.nick(), 'Zed');
    r.advance(400);
    r.state.live = false; r.tell('lost');
    r.state.sendFails = true;
    r.type(r.nick(), 'Zeds');
    r.advance(400);
    same('guest sheet: when the hub cannot be asked it says so and Save is off', [r.msgs(), r.save().disabled], [[Names.TEXT.offline], true]);
    r.tell('name', { t: 'name', r: 99, ok: true, nick: OK, shown: 'x', saved: false });
    check('guest sheet: an answer that was never asked for changes nothing', r.save().disabled === true);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4. A logged-in person's sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    let r = rig({ you: ME({}) });
    r.tell('names');
    check('sheet: the dock\'s tap opens it with a nickname field and a tag field, both empty, and the rules of a nickname', !!r.sheet() && !!r.nick() && !!r.tag() && r.nick().value === '' && r.tag().value === '' && byClass(r.sheet(), 'nm-rules')[0].children.length === 4);
    same('sheet: the empty fields say "Pick a nickname" and "ABC", and under them which letters a nickname may hold and what a tag is', [r.nick().placeholder, r.tag().placeholder, helps(r.sheet())], ['Pick a nickname', 'ABC', [Names.TEXT.nickHelp, Names.TEXT.tagHelp]]);
    check('sheet: a login has no guest\'s box and no login button', one(r.sheet(), 'nm-gbox') === null && button(r.sheet(), 'Log in with Discord') === null);
    check('sheet: it says first what the person shows as now (the login\'s name)', r.preview() === 'maple.leaf' && /chat/.test(one(r.sheet(), 'nm-ps').textContent));
    r.type(r.nick(), 'Zo\u00eb');
    r.type(r.tag(), '\u00c4NT');
    r.advance(349);
    check('sheet: a check waits for the last key of either field', r.sent.length === 0);
    r.advance(1);
    same('sheet: one frame asks about both fields and does not save', r.sent, [{ t: 'name', r: 1, nick: 'Zo\u00eb', tag: '\u00c4NT', save: false }]);
    r.tell('name', { t: 'name', r: 1, ok: true, nick: OK, tag: OK, shown: '[\u00c4NT] Zo\u00eb', saved: false });
    same('sheet: a good pair says the nickname is free, the tag nothing, shows the preview as "[TAG] Nickname" and turns Save on', [r.msgs(), r.preview(), r.save().disabled], [['Zo\u00eb is free.'], '[\u00c4NT] Zo\u00eb', false]);
    const tagNode = one(one(r.sheet(), 'nm-pn'), 'nm-tag');
    check('sheet: the preview draws the tag as its own dimmer node (front/name_letters.js paint)', !!tagNode && tagNode.textContent === '[\u00c4NT]');
    r.save().click();
    same('sheet: Save sends the same two fields with save: true and waits for the hub', [r.last(), r.save().disabled, !!r.sheet()], [{ t: 'name', r: 2, nick: 'Zo\u00eb', tag: '\u00c4NT', save: true }, true, true]);
    r.tell('name', { t: 'name', r: 2, ok: true, nick: OK, tag: OK, shown: '[\u00c4NT] Zo\u00eb', saved: true });
    check('sheet: the hub\'s "saved" closes it', r.sheet() === null);
    r.state.you = ME({ nick: 'Zo\u00eb', tag: '\u00c4NT', nickat: T0 / 1000 - 2 * DAY, nickfree: T0 / 1000 + 28 * DAY, tagfree: T0 / 1000 + 3600 });
    r.tell('you', {});
    r.tell('names');
    check('sheet: opened for a person who has a nickname, it has the nickname and the tag in its fields', r.nick().value === 'Zo\u00eb' && r.tag().value === '\u00c4NT');
    check('sheet: its preview is "[TAG] Nickname", and it says since when it has been the nickname and when it can change again',
        r.preview() === '[\u00c4NT] Zo\u00eb' && /^Your nickname since \d+ \w+\. You can change it again on \d+ \w+\.$/.test(one(r.sheet(), 'nm-ps').textContent), r.preview() + ' | ' + one(r.sheet(), 'nm-ps').textContent);
    r.advance(10);
    r.tell('name', { t: 'name', r: r.last().r, ok: true, nick: OK, tag: OK, shown: '[\u00c4NT] Zo\u00eb', saved: false });
    same('sheet: the person\'s own name is "your name now" and cannot be saved again', [r.msgs()[0], r.save().disabled], ['That is your name now.', true]);
    check('... and the note under the preview is still the one of the nickname that the person has (since when, when it can change), not the first-time one', /^Your nickname since \d+ \w+\. You can change it again on \d+ \w+\.$/.test(one(r.sheet(), 'nm-ps').textContent), one(r.sheet(), 'nm-ps').textContent);
    r.type(r.tag(), 'ZZ');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, ok: false, nick: OK, tag: { ok: false, why: 'soon', until: T0 / 1000 + 3600 }, shown: '', saved: false });
    check('sheet: a tag that changed today is locked: the line says when, the field is read only and Save is off', /You changed your tag today\. You can change it again in 1 hour\./.test(r.msgs().join(' | ')) && r.tag().readOnly === true && r.save().disabled === true, r.msgs().join(' | '));
    r.type(r.nick(), 'Fern');
    r.type(r.tag(), '');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, ok: false, nick: { ok: false, why: 'soon', until: T0 / 1000 + 28 * DAY }, tag: null, shown: '', saved: false });
    check('sheet: a nickname that changed lately says when it changed and when it can again, and is locked', /You changed your nickname on \d+ \w+\. You can change it again on \d+ \w+\./.test(r.msgs().join(' | ')) && r.nick().readOnly === true && r.save().disabled === true, r.msgs().join(' | '));
    r.type(r.nick(), 'Fern');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, ok: false, nick: { ok: false, why: 'lookalike' }, tag: null, shown: '', saved: false });
    check('sheet: a lookalike names nobody', r.msgs().length === 1 && /too much like another name/.test(r.msgs()[0]) && r.msgs()[0].indexOf('Fern') === -1 && r.save().disabled === true, r.msgs().join(' | '));
    r.type(r.nick(), 'Fern');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, ok: false, nick: OK, tag: { ok: false, why: 'staff' }, shown: '', saved: false });
    same('sheet: a tag that looks like a staff badge is refused in its own line, the nickname stays fine and Save stays off', [r.msgs(), r.save().disabled], [['Fern is free.', 'That tag looks like a staff badge. Pick another one.'], true]);
    r.type(r.nick(), 'Fern');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, why: 'rate', ok: false });
    check('sheet: "rate" says to wait a moment', r.msgs().length >= 1 && /Not so fast/.test(r.msgs()[0]) && r.save().disabled === true, r.msgs().join(' | '));
    {
        const nobody = rig({ noLogin: true });
        nobody.tell('names');
        check('sheet: a guest sheet opens only where a guest is (a site with no login has no such page: a tap that comes there opens nothing)', nobody.sheet() === null);
    }
    r.state.you = null; r.tell('you', {});
    check('sheet: a person who logs out while it is open loses it (a login\'s sheet is not a guest\'s)', r.sheet() === null);

    r = rig({ you: ME({ nick: 'Zo\u00eb' }) });
    r.tell('names');
    r.state.sendFails = true;
    r.type(r.nick(), 'Zo\u00eb2');
    r.advance(400);
    check('sheet: with no connection the person is told, Save stays off, nothing was sent', r.msgs()[0] === Names.TEXT.offline && r.save().disabled === true && r.sent.length === 0);
    r = rig({ you: ME({ nick: 'Zo\u00eb' }) });
    r.tell('names');
    check('sheet: opened twice it stays one sheet', (r.tell('names'), byClass(r.doc.body, 'nm-sheet').length === 1));
    r.sheet().fire('keydown', { key: 'Escape', stopPropagation() {} });       // (a key of a field reaches the sheet: it bubbles)
    check('sheet: Escape closes it', r.sheet() === null);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 5. Nothing is markup
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({ you: ME({ nick: '<b>x</b>' }) });
    r.tell('names');
    r.type(r.nick(), '<img src=x onerror=alert(1)>');
    r.advance(400);
    r.tell('name', { t: 'name', r: r.last().r, ok: false, nick: { ok: false, why: 'taken' }, tag: null, shown: '', saved: false });
    check('markup: a name that is HTML is shown as text (the field, the line under it)', r.msgs()[0] === '<img src=x onerror=alert(1)> is taken. Pick another one.' && r.env.innerHTMLWrites.length === 0, JSON.stringify(r.env.innerHTMLWrites));
    const tags = r.doc.body.findAll(() => true).map((e) => e.tagName);
    check('markup: the sheet makes only the elements of a form', tags.every((t) => ['body', 'div', 'span', 'button', 'h3', 'label', 'small', 'input', 'p', 'ul', 'li', 'i', 'bdi', 'b'].indexOf(t) !== -1), JSON.stringify(Array.from(new Set(tags))));
}

console.log('home names: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);

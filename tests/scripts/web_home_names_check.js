// Runs the sheet "Your name", the guest's chip and its note (web/home/names.js, docs/HUB_NICKNAMES.md "The page") in the fake browser of tests/scripts/web_page_harness.js (its DOM), with a fake dock (the
// hub's frames in and out), a fake clock and a fake person (front/player_name.js: the guest's name that this browser keeps). What it checks:
//   1. the pure parts: the day and the month of a time, the words for a wait, and the sentence under a field for every word that the hub says, in each of the three kinds (a nickname, a tag, a guest's
//      name): a refusal is red, a wait is a lock with its dates, a name that is taken names the name that was asked and nobody else, a word that the page does not know is "could not check";
//   2. the guest's chip: only when the dock is live, nobody is logged in and the Log in button shows; its letter and its name; "Please set a name" until a name is set or the note is closed, and then never
//      again in this browser (also not after a reload); a browser that keeps nothing shows the note again, and the page still works;
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
    const store = {};
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
    const state = { live: o.live !== false, you: o.you === undefined ? null : o.you, sendFails: !!o.sendFails };
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
    const api = Names.mount({ doc, win, dock, bar, login, person, letters: Letters, changed: () => changes.push(1) });
    const r = { env, doc, win, bar, login, dock, api, sent, typed, person, store, changes, state, advance, now: () => now, clicks: () => clicks };
    r.tell = (kind, m) => taps.forEach((fn) => fn(kind, m));
    if (state.live) r.tell('live', { login: !o.noLogin });       // (the welcome of a hub: it says whether the hub has a login)
    r.chip = () => one(bar, 'nm-gchip');
    r.pop = () => one(doc.body, 'nm-pop');
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
// 2. The guest's chip and its note
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    let r = rig({});
    check('chip: a guest on a live dock sees the chip, with the first letter and the shown name', !!r.chip() && !r.chip().hidden && one(r.chip(), 'gc-av').textContent === 'G' && one(r.chip(), 'gc-cn').textContent === 'Guest 550000', r.chip() && r.chip().textContent);
    check('chip: it stands before the Log in button', r.bar.children.indexOf(r.chip()) === r.bar.children.indexOf(r.login) - 1);
    check('chip: its label says what to do', /Set a name/.test(r.chip().getAttribute('aria-label')), r.chip().getAttribute('aria-label'));
    check('note: "Please set a name" shows under it, with the name that the others see now, and closes with its button', !!r.pop() && /Please set a name/.test(r.pop().textContent) && /Guest 550000/.test(r.pop().textContent));
    byClass(r.pop(), 'rs-x')[0].click();
    check('note: closing it removes it and keeps one key in this browser', r.pop() === null && r.store['ants.guestnote'] === '1');
    r.tell('shown'); r.tell('live', {});
    check('note: it does not come back in this page', r.pop() === null);
    r = rig({});
    r.store['ants.guestnote'] = '1';
    r.tell('shown');
    check('note: a browser that closed it once never sees it again (a new page, the same browser)', r.pop() === null);
    r = rig({ noStorage: true });
    check('note: a browser that keeps nothing shows the note, and the page works', !!r.pop() && !!r.chip());
    byClass(r.pop(), 'rs-x')[0].click();
    check('note: ...and closes it for the page', r.pop() === null);

    r = rig({ live: false });
    check('chip: no chip while the dock is not live (the page does not know yet who this is)', r.chip() === null && r.pop() === null);
    r.live();
    check('chip: it comes with the dock', !!r.chip() && !r.chip().hidden);
    r = rig({ live: false });
    r.login.hidden = true;
    r.live();
    check('chip: the welcome says that the hub has a login while the Log in button is not shown yet (the dock shows it a moment later): the chip is there all the same', !!r.chip() && !r.chip().hidden);
    r = rig({ you: ME({}) });
    check('chip: a person who is logged in has none', r.chip() === null && r.pop() === null);
    r = rig({ noLogin: true });
    check('chip: a site with no login has none (nothing to log in to: a guest name is only for a site that has accounts)', r.chip() === null);
    r = rig({});
    r.state.you = ME({}); r.tell('you', {});
    check('chip: logging in hides it and its note', r.chip().hidden === true && r.pop() === null);
    r.state.you = null; r.tell('you', {});
    check('chip: logging out brings it back', r.chip().hidden === false);
    r = rig({ typed: 'Wren' });
    check('chip: a guest who set a name shows it with its own label, and no note', one(r.chip(), 'gc-cn').textContent === 'Wren' && one(r.chip(), 'gc-av').textContent === 'W' && /Change it/.test(r.chip().getAttribute('aria-label')) && r.pop() === null);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 3. A guest's sheet
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const r = rig({});
    r.chip().click();
    check('guest sheet: the chip opens it, the note goes, there is one field (no tag) with a counter', !!r.sheet() && r.pop() === null && !!r.nick() && r.tag() === null && one(r.sheet(), 'nm-cnt').textContent === '0 / 16');
    check('guest sheet: Save is off and nothing was asked yet', r.save().disabled === true && r.sent.length === 0);
    check('guest sheet: it says what the others see now and has the rules for guests and the login button', /Guest 550000/.test(r.preview()) && byClass(r.sheet(), 'nm-rules')[0].children.length === 3 && !!button(r.sheet(), 'Log in with Discord'));
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
    check('guest sheet: the note is never shown again (one key), and Save sent nothing more to the hub', r.store['ants.guestnote'] === '1' && r.pop() === null && r.sent.every((m) => m.guest === true));
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

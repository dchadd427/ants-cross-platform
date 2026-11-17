// Runs the admin page's OWN code (web/admin_page.js) with a small fake of the DOM and a fake server (the game server's /admin/api, the hub's /hub/admin/api, /hub/me, /hub/logout and /busy): who is let in and what they
// see (signed out, a player, a moderator, an admin, the owner), the shell (the chip and its menu, the four sections as tabs and as a select), the Maps section (the lists, the questions before Approve and Take down, the
// empty states), the Chat section (the form that mutes or bans and its question, the lists, the pictures), the Log (one list, three filters), the Status (all well, trouble, a part that does not answer), what every
// button sends and with which token, and that nothing a person typed becomes markup and no Discord id is shown or sent. tests/scripts/test_web_admin.py runs this with node (the quick tier).
// usage: node web_admin_check.js web/admin_page.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');
const A = require(path.resolve(process.argv[2]));

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function settle() { for (let i = 0; i < 14; i++) await tick(); }


// ---- the time as the pictures write it (Pacific) -----------------------------------------------------------------------------------------------------------------------------------------------------
const NOW = Date.UTC(2026, 9, 18, 20, 0, 0);            // 2026-10-18 13:00 Pacific
const at = (iso) => Date.parse(iso) / 1000;
same('today with the time', A.when(at('2026-10-18T16:41:00Z'), NOW), 'today, 09:41');
same('yesterday has no time', A.when(at('2026-10-17T16:41:00Z'), NOW), 'yesterday');
same('an older day is the day and the month', A.when(at('2026-10-12T16:41:00Z'), NOW), '12 Oct');
same('the day is the Pacific day: 02:00 UTC is the evening before', A.when(at('2026-10-18T02:00:00Z'), NOW), 'yesterday');
same('after midnight in Pacific time it is today', A.when(at('2026-10-18T07:30:00Z'), NOW), 'today, 00:30');
same('the winter hour is another offset', A.when(at('2026-12-05T18:05:00Z'), Date.UTC(2026, 11, 5, 20, 0, 0)), 'today, 10:05');
same('a year ago is the day and the month too', A.when(at('2025-10-18T16:00:00Z'), NOW), '18 Oct');
same('a time that is not a number is no words, and throws nothing', [A.when('x', NOW), A.when(undefined, NOW), A.when(NaN, NOW), A.untilWords(null, NOW), A.leftWords('7', NOW)], ['', '', '', '', '']);
same('the end of a mute: today, tomorrow, another day (Pacific)', [A.untilWords(at('2026-10-18T22:51:00Z'), NOW), A.untilWords(at('2026-10-19T16:00:00Z'), NOW), A.untilWords(at('2026-10-25T18:40:00Z'), NOW)], ['today, 15:51', 'tomorrow, 09:00', '25 Oct, 11:40']);
same('the time that is left of a mute', [A.leftWords(at('2026-10-18T20:00:30Z'), NOW), A.leftWords(at('2026-10-18T20:45:00Z'), NOW), A.leftWords(at('2026-10-18T21:31:00Z'), NOW), A.leftWords(at('2026-10-19T04:00:00Z'), NOW), A.leftWords(at('2026-10-19T20:00:00Z'), NOW), A.leftWords(at('2026-10-25T20:00:00Z'), NOW)],
    ['1 min left', '45 min left', '1 h 31 min left', '8 h left', '1 day left', '7 days left']);
// a username box: a name, a name of digits (written with an @), and the refusals (nothing, spaces, a number as long as an id)
same('a username is taken as it is, without the blanks around it', [A.accountOf('  clover.4782 ').account, A.accountOf('  clover.4782 ').shown], ['clover.4782', 'clover.4782']);
same('a username of digits is written with an @ for the hub', [A.accountOf('12345').account, A.accountOf('12345').shown, A.accountOf('@abc').account], ['@12345', '12345', '@abc']);
same('a reference is a valid account (16 letters and digits)', A.accountOf('7c2e91d0a4b83f56').account, '7c2e91d0a4b83f56');
for (const bad of ['', '   ', 'two words', 'x'.repeat(41), 'caf\u00e9', '\t']) check('a username that is not one is refused: ' + JSON.stringify(bad), A.accountOf(bad).why !== undefined && A.accountOf(bad).account === undefined);
for (const id of ['123456789012345', '1234567890123456789', '12345678901234567890']) check('a number as long as an id is refused, whatever its length (15 to 20 digits): ' + id.length, A.accountOf(id).why !== undefined && A.accountOf(id).account === undefined);
check('the refusal of a number does not repeat it', !/[0-9]{5}/.test(A.accountOf('1234567890123456789').why));

// ---- the page, a fake server ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

function map(id, extra) {
    return Object.assign({ id, state: 'waiting', kind: 'classic', name: 'Treasure', maker: 'Mia', note: '', width: 34, height: 34, sent_at: at('2026-10-18T16:41:00Z'), name_taken: false, has_picture: true, findings: [] }, extra || {});
}
function who(name, role, csrf) {                        // the answer of GET /admin/api/me for a person who is let in
    return { status: 200, json: { name, ref: '0123456789abcdef', role: role || 'admin', csrf: csrf || 'csrf-token-1', untrusted: true, untrusted_fields: ['name'] } };
}
function world(overrides) {
    const w = Object.assign({
        me: who('Dave'),
        hubMe: { status: 200, json: { name: 'Dave', role: 'admin', csrf: 'hub-token-1', untrusted: true, untrusted_fields: ['name'] } },
        hubYou: { status: 200, json: { login: true, you: { name: 'Dave', mod: false, role: 'none', k: 'abc' } } },       // GET /hub/me: what the chat knows of the visitor
        counts: { waiting: 2, approved: 1, declined: 1, taken_down: 1 },
        lists: {
            waiting: [map('a'.repeat(32), { name: 'Treasure', note: 'my first map', name_taken: true }), map('b'.repeat(32), { kind: 'extended', name: 'Big Lake', maker: 'Sam', width: 300, height: 400, sent_at: at('2026-10-17T16:00:00Z') })],
            approved: [map('c'.repeat(32), { state: 'approved', approved_name: 'Treasure 2', name: 'Treasure', decided_at: at('2026-10-18T21:20:00Z') - 3600 * 5 })],
            declined: [map('d'.repeat(32), { state: 'declined', name: 'Dry Canyon', maker: 'Lee', decided_at: at('2026-10-16T16:00:00Z'), message: 'Too plain for the list' })],
            taken_down: [map('e'.repeat(32), { state: 'taken_down', approved_name: 'Old Hill', name: 'Old Hill', decided_at: at('2026-10-18T16:02:00Z') })]
        },
        bans: [{ ref: '11a1111111111111', name: 'clover.4782', at: at('2026-10-18T16:12:00Z'), by: '22b2222222222222', by_name: 'basil' },
            { ref: '33c3333333333333', name: 'spam.king', at: at('2026-10-14T18:00:00Z'), by: '44d4444444444444', by_name: 'tess' },
            { ref: '55e5555555555555', name: '', at: at('2026-10-09T18:00:00Z'), by: 'command line', by_name: '' }],
        mutes: [{ ref: '66f6666666666666', name: 'hazel_b', until: at('2026-10-18T21:31:00Z'), by: '77a7777777777777', by_name: 'sage' },
            { ref: '88b8888888888888', name: 'pebble_9', until: at('2026-10-25T18:40:00Z'), by: '22b2222222222222', by_name: 'basil' }],
        hidden: [{ ref: '99c9999999999999', name: 'marlow.old' }, { ref: 'aaaaaaaaaaaaaaaa', name: '' }],
        modlog: [{ at: at('2026-10-18T19:02:00Z'), action: 'mute', actor: '22b2222222222222', actor_name: 'basil', target: '66f6666666666666', target_name: 'hazel_b', detail: '60' },
            { at: at('2026-10-18T18:47:00Z'), action: 'del', actor: '77a7777777777777', actor_name: 'sage', target: '11a1111111111111', target_name: 'clover.4782', detail: '148' },
            { at: at('2026-10-18T16:12:00Z'), action: 'ban', actor: '22b2222222222222', actor_name: 'basil', target: '11a1111111111111', target_name: 'clover.4782', detail: '' },
            { at: at('2026-10-17T18:00:00Z'), action: 'nopic', actor: '44d4444444444444', actor_name: 'tess', target: '99c9999999999999', target_name: 'marlow.old', detail: '' },
            { at: at('2026-10-13T18:00:00Z'), action: 'del', actor: 'terminal', actor_name: '', target: '', target_name: '', detail: '97' },
            { at: at('2026-10-12T18:00:00Z'), action: 'mute', actor: 'bbbbbbbbbbbbbbbb', actor_name: '', target: 'cccccccccccccccc', target_name: '', detail: '10' }],
        audit: [{ at: at('2026-10-18T18:30:00Z'), who: 'basil', ref: '22b2222222222222', action: 'map_approved', target: 'Mossy Ridge', detail: '' },
            { at: at('2026-10-18T17:02:00Z'), who: 'tess', ref: '44d4444444444444', action: 'map_declined', target: 'Test 1', detail: 'Same as an existing map.' },
            { at: at('2026-10-17T18:00:00Z'), who: 'earlier admin', ref: '', action: 'map_taken_down', target: 'Old Bridge', detail: '' }],
        busy: { matches: 2, players: 5 },
        status: {
            store: { bytes: 4404019, limit: 67108864, log: 314572, free: 0, lines: 187, oldest: at('2026-10-09T18:00:00Z'), pictures: 41, pictures_max: 1000, picture_bytes: 1153434, picture_bytes_max: 25165824, off: 6, blocked: 1 },
            health: { roles: 'ok', roles_known: 14, kind: null, status: null, retry: null }, last_picture: { kind: 'ok', age: 180 }, version: { code: '9f3c1a2e', started: at('2026-10-16T16:30:00Z') }
        },
        calls: [],
        replies: {}
    }, overrides || {});
    w.fetch = async (url, init) => {
        init = init || {};
        w.calls.push({ method: init.method || 'GET', url, headers: init.headers || {}, body: init.body === undefined ? undefined : JSON.parse(init.body), credentials: init.credentials });
        const reply = (status, json) => ({ status, json: async () => json });
        const method = init.method || 'GET';
        const key = method + ' ' + url;
        for (const pattern of Object.keys(w.replies)) {
            if (key.startsWith(pattern)) { const r = w.replies[pattern]; return typeof r === 'function' ? r() : reply(r.status, r.json); }
        }
        if (url === '/admin/api/me') return reply(w.me.status, w.me.json);
        if (url === '/hub/admin/api/me') return reply(w.hubMe.status, w.hubMe.json);
        if (url === '/hub/me') return reply(w.hubYou.status, w.hubYou.json);
        if (url === '/busy') return reply(200, w.busy);
        if (url === '/admin/api/audit') return reply(200, { entries: w.audit, untrusted: true, untrusted_fields: ['who', 'target', 'detail'] });
        if (url === '/hub/admin/api/bans') return reply(200, { bans: w.bans, untrusted: true, untrusted_fields: ['name', 'by_name'] });
        if (url === '/hub/admin/api/mutes') return reply(200, { mutes: w.mutes, untrusted: true, untrusted_fields: ['name', 'by_name'] });
        if (url === '/hub/admin/api/hidden') return reply(200, { hidden: w.hidden, untrusted: true, untrusted_fields: ['name'] });
        if (url === '/hub/admin/api/modlog') return reply(200, { modlog: w.modlog, untrusted: true, untrusted_fields: ['actor_name', 'target_name'] });
        if (url === '/hub/admin/api/status') return reply(200, w.status);
        if (method === 'POST' && url === '/hub/logout') return reply(200, { ok: true });
        if (method === 'POST' && /^\/hub\/admin\/api\/(ban|unban|mute|unmute|nopic|pic|del)$/.test(url)) {
            const body = JSON.parse(init.body), name = url.split('/').pop();
            if (name === 'unban') w.bans = w.bans.filter((x) => x.ref !== body.account);
            if (name === 'unmute') w.mutes = w.mutes.filter((x) => x.ref !== body.account);
            if (name === 'pic') w.hidden = w.hidden.filter((x) => x.ref !== body.account);
            return reply(200, name === 'mute' ? { ok: true, until: at('2026-10-18T21:00:00Z') } : { ok: true });
        }
        const m = /^\/admin\/api\/maps\?state=(\w+)$/.exec(url);
        if (m) return reply(200, { state: m[1], counts: w.counts, maps: w.lists[m[1]] || [] });
        const n = /^\/admin\/api\/next-name\?name=(.*)$/.exec(url);
        if (n) return reply(200, { name: decodeURIComponent(n[1]).replace(/ \d+$/, '') + ' 3' });
        if (method === 'POST') return reply(200, { map: {}, counts_changed: true });
        return reply(404, { error: 'not_found' });
    };
    return w;
}
// an answer whose body is not JSON (the web server's own answer to too many requests)
const notJson = (status) => () => ({ status, json: async () => { throw new SyntaxError('Unexpected token'); } });
const guard = { t: 0, step: 1000 };                 // the clock that only goes forward: each reading is one step later (a question that is answered one step after it opened; 1000 ms, or 100 for a double click)
const STAMPED = { 'data-version': 'v0.16.11', 'data-build': '9d41c2e', 'data-built': '2026-11-16 09:30 PT' };
async function open(w, stamps) {
    const doc = makeDocument();
    const bar = doc.createElement('header');                                       // the bar at the top, which the page's own file carries (the Status reads its stamps)
    bar.className = 'sb';
    for (const k of Object.keys(stamps === undefined ? STAMPED : stamps || {})) bar.setAttribute(k, (stamps === undefined ? STAMPED : stamps)[k]);
    doc.body.appendChild(bar);
    const app = doc.createElement('div');
    app.id = 'app';
    doc.body.appendChild(app);
    doc.getElementById = (id) => (id === 'app' ? app : null);
    const timers = [];
    const page = A.create({ doc, fetch: w.fetch, now: () => NOW, tick: () => (guard.t += guard.step), setTimeout: (f) => { timers.push(f); return timers.length; }, clearTimeout: () => {} });
    await page.start();
    await settle();
    return { doc, app, page, timers, bar };
}
const all = (app, sel) => Array.from(app.querySelectorAll(sel));
const texts = (app, sel) => all(app, sel).map((n) => n.textContent);
const button = (app, label) => all(app, 'button').filter((b) => b.textContent === label);
async function press(app, label, which) {
    const list = button(app, label);
    if (!list.length) return false;
    list[which || 0].click();
    await settle();
    return true;
}
// a pointer press on the page's dimmed area (the .scrim): from and to are the nodes where it starts and ends
async function backdrop(app, from, to) {
    const scrim = app.querySelector('.scrim');
    scrim.fire('pointerdown', { target: from === 'scrim' ? scrim : app.querySelector('.dlg') });
    scrim.fire('pointerup', { target: to === 'scrim' ? scrim : app.querySelector('.dlg') });
    scrim.fire('click', { target: scrim });
    await settle();
}
async function open_maps(label) {                  // the page on the Maps section (Approved for Take down), for a question
    const w = world();
    const o = await open(w);
    if (label === 'Take down') await press(o.app, 'Approved (1)');
    return Object.assign({ w }, o);
}
// the sections, by their tabs
async function section(app, title) {
    const tab = all(app, '.atab').filter((b) => b.textContent.indexOf(title) === 0)[0];
    tab.click();
    await settle();
}
const type = (app, id, value) => { const i = all(app, 'input').filter((n) => n.id === id)[0]; i.value = value; return i; };

const HEAD = 'This page is for the site\u2019s admins.';
const LOGIN_LINE = 'Log in with the Discord account that has the admin role.';
const UNAVAILABLE = 'The admin page is not available right now.';
// the lines that an admin is given when the page can tell which part is off (names of settings, never a value)
const OFF_DOOR = 'The admin page\u2019s door is shut: the chat hub has no good ANTS_ADMIN_GATE_SECRET.';
const OFF_SUBMISSIONS = 'The map submissions door is off or not running on the game server (it is on when ANTS_SUBMIT_PORT is 4030).';
const OFF_KEY = 'The game server did not accept the admin page\u2019s pass: its ANTS_ADMIN_GATE_SECRET is probably missing or is not the same as the chat hub\u2019s.';
function noMarkup(label, app) {
    const allowed = new Set(['div', 'span', 'img', 'button', 'a', 'label', 'input', 'textarea', 'h1', 'h2', '#text', 'b', 'small', 'p', 'section', 'select', 'option', 'code', 'em', 'hr', 'i']);
    for (const t of new Set(app.all().map((n) => n.tagName))) check(label + ': only the page\'s own kinds of node (' + t + ')', allowed.has(t));
    check(label + ': innerHTML was never set', app.all().every((n) => n.innerHTML === undefined));
}
// nothing of the retired password door is on the page: no password box, no form, no menu (while it is closed), and no word that offers one
function noPasswordDoor(label, app) {
    check(label + ': no password box, no form, no menu, no eye', all(app, 'input').every((i) => i.type !== 'password') && all(app, 'form').length === 0 && all(app, '.amenu').length === 0 && all(app, '.eye').length === 0);
    check(label + ': no word offers Users, Sign out, Change password or a sign-in', !/users|sign out|sign in|sign-in|change password|password/i.test(app.textContent));
}
// what the page shows: a section of the shell or one of the cards
function shown(app) {
    if (all(app, '.atabs').length) return 'maps';
    if (all(app, '.lcard .msg').length) return 'line';
    if (all(app, '.lcard a').filter((a) => a.getAttribute('href') === '/hub/login?next=admin').length) return 'login';
    if (all(app, '.lcard h1').length) return 'heading';
    return 'nothing';
}
// a Discord id is like a password: none is on the page, in a call or in a body (these are the ones that the fake server and the checks plant)
const IDS = ['9100000000000001', '9100000000000002', '123456789012345678'];
function noId(label, app, calls) {
    const blob = app.textContent + JSON.stringify(calls.map((c) => [c.url, c.body, c.headers]));
    check(label + ': no Discord id on the page or in a call', IDS.every((id) => blob.indexOf(id) < 0) && !/[0-9]{15,}/.test(blob.replace(/[0-9a-f]{32}/g, '')));
}

// an error that the page or this check throws and nothing catches is a failure, said in words (not only a stack and an exit code)
process.on('unhandledRejection', (e) => { console.log('FAIL an error was thrown and nothing caught it (' + (e && e.message) + ')'); process.exit(1); });
(async () => {

    // ---- GET /admin/api/me answers 200: the shell, a chip with the name and the role, and the Maps section
    for (const [role, roleWords] of [['admin', 'admin'], ['owner', 'owner']]) {
        const w = world({ me: who('Dave', role) });
        const { app, page } = await open(w);
        same(role + ': the question who is here is asked once, with the cookie, then the list of the Maps section and nothing else', [w.calls.map((c) => c.url), w.calls[0].credentials, w.calls[0].method], [['/admin/api/me', '/admin/api/maps?state=waiting'], 'same-origin', 'GET']);
        same(role + ': the title, the line under it and the four sections as tabs', [texts(app, 'h1'), texts(app, '.pt p'), all(app, '.atab').map((b) => b.id), page.sections()], [['Admin'], ['Maps that players sent, bans and mutes in the chat, the logs, and how the site is doing.'], ['tab-maps', 'tab-chat', 'tab-log', 'tab-status'], ['maps', 'chat', 'log', 'status']]);
        same(role + ': the chip shows the name, the role and the first letter, once', [texts(app, '.acct .wn b'), texts(app, '.acct .wn small'), texts(app, '.acct .av'), all(app, '.acct').length], [['Dave'], [roleWords], ['D'], 1]);
        same(role + ': the chip is a button that opens a menu, and the menu is closed', [all(app, '.acct')[0].tagName, all(app, '.acct')[0].getAttribute('aria-haspopup'), all(app, '.acct')[0].getAttribute('aria-expanded'), all(app, '.amenu').length, all(app, 'a').length], ['button', 'menu', 'false', 0, 0]);
        same(role + ': only the owner\'s role is in gold', all(app, '.acct .wn small')[0].className, role === 'owner' ? 'own' : '');
        same(role + ': the name is typed text and is marked as data', all(app, '.acct [data-untrusted]').map((n) => n.getAttribute('data-untrusted')), ['typed-text']);
        same(role + ': everybody who is let in has all of the page: three buttons on a waiting map', Array.from(all(app, '.arow')[0].querySelectorAll('button')).map((b) => b.textContent), ['Approve', 'Rename and approve', 'Decline']);
        noPasswordDoor(role, app);
        noMarkup(role, app);
        noId(role, app, w.calls);
    }
    {   // a name with markup characters is text, and so is every odd name
        const evil = '<img src=x onerror=alert(1)>';
        const w = world({ me: who(evil) });
        const { app } = await open(w);
        same('a name with markup characters is shown as the text it is', [texts(app, '.acct .wn b'), texts(app, '.acct .av')], [[evil], ['<']]);
        check('and it is in one marked element that holds text only', all(app, '.acct [data-untrusted="typed-text"]').length === 1 && all(app, '.acct [data-untrusted="typed-text"]')[0].children.length === 0);
        noMarkup('a name with markup characters', app);
        const o = await open(world({ me: who('"><script>alert(2)</script>') }));
        same('a name that tries to close its own element is text too', texts(o.app, '.acct .wn b'), ['"><script>alert(2)</script>']);
        noMarkup('a name that tries to close its own element', o.app);
        const b = await open(world({ me: who('\u202ebid\u2066i\u200fx') }));
        same('a bidirectional control is dropped from the name, and the letter is not one', [texts(b.app, '.acct .wn b'), texts(b.app, '.acct .av')], [['bidix'], ['b']]);
        const e = await open(world({ me: who('') }));
        same('a name that is empty has a mark in its circle and no markup', [texts(e.app, '.acct .wn b'), texts(e.app, '.acct .av')], [[''], ['?']]);
        const r = await open(world({ me: who('Dave', '<b>role</b>') }));
        same('a role that is not known is shown as text', texts(r.app, '.acct .wn small'), ['<b>role</b>']);
        noMarkup('an unknown role', r.app);
    }
    {   // the chip's menu: the name and the role, Front page and Log out; it closes with Escape and with a press outside; Log out ends the login and shows the card again
        const w = world({ me: who('Dave', 'owner') });
        const { app, doc } = await open(w);
        const chip = all(app, '.acct')[0];
        chip.click();
        await settle();
        same('the menu opens: the name, the role, a link to the front page and the button Log out', [texts(app, '.amenu .mtop b'), texts(app, '.amenu .mtop small'), texts(app, '.amenu a'), all(app, '.amenu a')[0].getAttribute('href'), texts(app, '.amenu button'), chip.getAttribute('aria-expanded'), chip.className.indexOf('open') > 0],
            [['Dave'], ['owner'], ['Front page'], '/', ['Log out'], 'true', true]);
        same('the link is a plain link of this tab, and the name in the menu is typed text', [all(app, '.amenu a')[0].getAttribute('target'), all(app, '.amenu [data-untrusted]').map((n) => n.getAttribute('data-untrusted'))], [null, ['typed-text']]);
        doc.key('Escape');
        await settle();
        same('Escape closes it and the focus is back on the chip', [all(app, '.amenu').length, chip.getAttribute('aria-expanded'), doc.activeElement === chip, (doc.listeners.keydown || []).length, (doc.listeners.click || []).length], [0, 'false', true, 0, 0]);
        chip.click();
        await settle();
        doc.listeners.click[0]({ target: app.querySelector('h1') });
        await settle();
        same('a press elsewhere closes it, and a press inside it does not', [all(app, '.amenu').length], [0]);
        chip.click();
        await settle();
        doc.listeners.click[0]({ target: all(app, '.amenu a')[0] });
        same('a press inside the menu leaves it open', all(app, '.amenu').length, 1);
        chip.click();
        await settle();
        same('the chip closes it too', [all(app, '.amenu').length, (doc.listeners.keydown || []).length], [0, 0]);
        chip.click();
        await settle();
        w.calls.length = 0;
        w.me = { status: 401, json: { error: 'not_signed_in' } };
        await press(app, 'Log out');
        same('Log out: a POST to the hub\'s logout with the cookie and no body, then the question who is here is asked again', [w.calls.map((c) => c.method + ' ' + c.url), w.calls[0].credentials, w.calls[0].body], [['POST /hub/logout', 'GET /admin/api/me'], 'same-origin', undefined]);
        same('Log out: the card for nobody logged in is shown, nothing of the page is left and no key is listened to', [shown(app), all(app, '.acct').length, all(app, '.atabs').length, (doc.listeners.keydown || []).length, (doc.listeners.click || []).length], ['login', 0, 0, 0, 0]);
        const f = world({ replies: { 'POST /hub/logout': { status: 403, json: {} } } });
        const o = await open(f);
        all(o.app, '.acct')[0].click();
        await press(o.app, 'Log out');
        same('a logout that is refused is said and the page stays', [texts(o.app, '.toast'), shown(o.app)], [['That did not work. Reload the page.'], 'maps']);
        const g = world({ replies: { 'POST /hub/logout': () => { throw new Error('network'); } } });
        const p = await open(g);
        all(p.app, '.acct')[0].click();
        await press(p.app, 'Log out');
        same('and so is one that does not arrive', [texts(p.app, '.toast'), shown(p.app)], [['The server did not answer. Try again.'], 'maps']);
    }
    {   // no password box and no text box anywhere but in the two boxes of the Maps page
        const w = world();
        const { app } = await open(w);
        check('the Maps page itself has no input, no text area and no form', all(app, 'input').length === 0 && all(app, 'textarea').length === 0 && all(app, 'form').length === 0);
        noPasswordDoor('waiting', app);
        await press(app, 'Rename and approve', 1);
        same('the rename box has one text box and nothing else that is typed into', [all(app, 'input').map((i) => i.type), all(app, 'textarea').length, all(app, 'input').map((i) => i.id)], [['text'], 0, ['rename-' + 'b'.repeat(32)]]);
        noPasswordDoor('the rename box', app);
        await press(app, 'Cancel');
        await press(app, 'Decline', 0);
        same('the decline box has one text area and no text box', [all(app, 'input').length, all(app, 'textarea').length], [0, 1]);
        noPasswordDoor('the decline box', app);
        await press(app, 'Cancel');
        await press(app, 'Approve', 0);
        same('the question before Approve has nothing typed into', [all(app, 'input').length, all(app, 'textarea').length], [0, 0]);
        noPasswordDoor('the Approve question', app);
        await press(app, 'Cancel');
        for (const tab of ['Approved (1)', 'Declined (1)', 'Taken down (1)']) {
            await press(app, tab);
            noPasswordDoor(tab, app);
            check(tab + ': no input anywhere', all(app, 'input').length === 0 && all(app, 'textarea').length === 0);
        }
        check('the page never asked for a sign-in, a password, a user or a log', w.calls.every((c) => /^\/admin\/api\/(me|maps[/?]|next-name\?)/.test(c.url)));
    }
    {   // a refusal that comes while the menu is open: the card is shown, and the menu and what it listens to are gone
        const w = world();
        const { app, doc } = await open(w);
        all(app, '.acct')[0].click();
        w.replies['GET /admin/api/maps?state=approved'] = { status: 401, json: { error: 'not_signed_in' } };
        await press(app, 'Approved (1)');
        same('the card for nobody is shown, with no menu and no listener of the document', [shown(app), all(app, '.amenu').length, (doc.listeners.keydown || []).length, (doc.listeners.click || []).length], ['login', 0, 0, 0]);
    }
    // ---- the sections: four tabs and a select, one section at a time, the Maps tab says how many maps wait
    {
        const w = world();
        const { app, page } = await open(w);
        same('the tabs: a tab list with its name, the Maps one chosen, the number of maps that wait on it', [all(app, '.atabs')[0].getAttribute('role'), all(app, '.atabs')[0].getAttribute('aria-label'), texts(app, '.atab'), all(app, '.atab').map((b) => b.getAttribute('aria-selected')), all(app, '.atab').map((b) => b.getAttribute('role')), texts(app, '.atab .cnt')],
            ['tablist', 'Sections', ['Maps2', 'Chat', 'Log', 'Status'], ['true', 'false', 'false', 'false'], ['tab', 'tab', 'tab', 'tab'], ['2']]);
        same('the select of a phone: a label, the four sections, the Maps one chosen with its count in words', [texts(app, '.asel label'), all(app, '.asel label')[0].getAttribute('for'), all(app, 'select')[0].id, all(app, 'option').map((o) => o.value), all(app, 'option').map((o) => o.textContent), all(app, 'select')[0].value],
            [['Section'], 'asec', 'asec', ['maps', 'chat', 'log', 'status'], ['Maps (2 waiting)', 'Chat', 'Log', 'Status'], 'maps']);
        same('the section is a panel that the tabs control', [all(app, '#secpanel')[0].getAttribute('role'), all(app, '.atab').map((b) => b.getAttribute('aria-controls'))], ['tabpanel', ['secpanel', 'secpanel', 'secpanel', 'secpanel']]);
        w.calls.length = 0;
        await section(app, 'Chat');
        same('a tab shows its section: the chosen tab, the select and the content change, and Maps is gone', [all(app, '.atab').map((b) => b.getAttribute('aria-selected')), all(app, 'select')[0].value, all(app, '.cgrid').length, all(app, '.chip').length > 0 && all(app, '.arow').length > 0], [['false', 'true', 'false', 'false'], 'chat', 1, false]);
        same('the badge stays on the Maps tab while another section is shown', texts(app, '.atab .cnt'), ['2']);
        all(app, 'select')[0].value = 'log';
        all(app, 'select')[0].fire('change', {});
        await settle();
        same('the select changes the section too', [all(app, '.atab').map((b) => b.getAttribute('aria-selected')), all(app, '.alist2').length, all(app, '.cgrid').length], [['false', 'false', 'true', 'false'], 1, 0]);
        const tab = all(app, '.atab')[2];
        const ev = { key: 'ArrowRight', prevented: false, preventDefault() { this.prevented = true; } };
        tab.fire('keydown', ev);
        await settle();
        same('the arrow keys move between the tabs: the next one is shown and has the focus, the last one has no next', [ev.prevented, all(app, '.atab').map((b) => b.getAttribute('aria-selected')), all(app, '.sgrid').length], [true, ['false', 'false', 'false', 'true'], 1]);
        const last = { key: 'ArrowRight', prevented: false, preventDefault() { this.prevented = true; } };
        all(app, '.atab')[3].fire('keydown', last);
        same('past the last tab nothing happens', [last.prevented, all(app, '.atab')[3].getAttribute('aria-selected')], [false, 'true']);
        all(app, '.atab')[3].fire('keydown', { key: 'ArrowLeft', preventDefault() {} });
        await settle();
        same('and back', all(app, '.atab').map((b) => b.getAttribute('aria-selected')), ['false', 'false', 'true', 'false']);
        await section(app, 'Maps');
        same('Maps again: its list is asked for again, from the first tab', [all(app, '.arow').length, all(app, '.chip.on').map((c) => c.textContent)], [2, ['Waiting (2)']]);
        same('every call of all these changes was a read', w.calls.filter((c) => c.method !== 'GET').length, 0);
    }
    // ---- who is not let in: the card of each of them (the role matrix: nobody, a player, a moderator, a person whose name is not known, a closed door)
    {   // GET /admin/api/me answers 401: one card with a link to the Discord login
        const w = world({ me: { status: 401, json: { error: 'not_signed_in', message: 'The words of the server, which the page does not show.' } } });
        const { app } = await open(w);
        same('401: only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
        same('401: the heading, the line and the link, in these words and no others', [texts(app, 'h1'), texts(app, '.l2'), texts(app, 'a'), app.textContent], [[HEAD], [LOGIN_LINE], ['Log in with Discord'], HEAD + LOGIN_LINE + 'Log in with Discord']);
        const link = all(app, 'a')[0];
        same('401: one plain same-tab link to the login of the chat hub, and nothing that runs when it is pressed', [all(app, 'a').length, link.getAttribute('href'), link.getAttribute('target'), (link.listeners.click || []).length], [1, '/hub/login?next=admin', null, 0]);
        check('401: the link looks like the page\'s button', link.className.split(' ').includes('btn'));
        check('401: nothing else: no title, no sections, no chip, no box to type in, no other button, no form', all(app, '.pt').length === 0 && all(app, '.atabs').length === 0 && all(app, '.acct').length === 0 && all(app, 'input').length === 0 && all(app, 'select').length === 0 && all(app, 'button').length === 0 && all(app, 'form').length === 0);
        same('401: the card is what is shown', shown(app), 'login');
        noPasswordDoor('401', app);
        noMarkup('401', app);
    }
    for (const [label, you, want, name] of [
        ['a player with no role', { name: 'pebble', mod: false, role: 'none', k: 'x' }, 'You are logged in as pebble.', 'pebble'],
        ['a moderator', { name: 'sage', mod: true, role: 'mod', k: 'x' }, 'You are logged in as sage, a moderator. The tools for moderators are on the front page.', 'sage'],
        ['an admin whom the door refused for now (the role is not fresh)', { name: 'basil', mod: true, role: 'admin', k: 'x' }, 'You are logged in as basil.', 'basil']]) {
        const w = world({ me: { status: 403, json: { error: 'not_allowed', message: 'The words of the server, which the page does not show.' } }, hubYou: { status: 200, json: { login: true, you } } });
        const { app } = await open(w);
        same('403 not_allowed, ' + label + ': the question who is here, then who the chat says this is (the hub\'s own call, no body)', [w.calls.map((c) => c.method + ' ' + c.url), w.calls[1].credentials], [['GET /admin/api/me', 'GET /hub/me'], 'same-origin']);
        same('403 not_allowed, ' + label + ': the heading, who it is, Front page and Log out', [texts(app, 'h1'), texts(app, '.l2'), texts(app, '.btnrow a'), texts(app, '.btnrow button'), all(app, '.btnrow a')[0].getAttribute('href')], [[HEAD], [want], ['Front page'], ['Log out'], '/']);
        same('403 not_allowed, ' + label + ': the name is typed text', all(app, '.l2 [data-untrusted]').map((n) => [n.getAttribute('data-untrusted'), n.textContent]), [['typed-text', name]]);
        same('403 not_allowed, ' + label + ': the line about a role that was just given is for the person with no role and the admin, not the moderator', texts(app, '.small'), you.role === 'mod' ? [] : ['Made an admin just now? Wait a minute and reload this page.']);
        check('403 not_allowed, ' + label + ': no link to log in, no title, no sections, no chip', all(app, 'a').length === 1 && all(app, '.atabs').length === 0 && all(app, '.acct').length === 0 && all(app, '.pt').length === 0);
        same('403 not_allowed, ' + label + ': the card is what is shown', shown(app), 'heading');
        noMarkup('403 not_allowed, ' + label, app);
        const o = await open(w);
        w.calls.length = 0;
        w.me = { status: 401, json: { error: 'not_signed_in' } };
        await press(o.app, 'Log out');
        same('403 not_allowed, ' + label + ': Log out posts to the logout of the hub and shows the card for nobody', [w.calls.filter((c) => c.method === 'POST').map((c) => c.url), shown(o.app)], [['/hub/logout'], 'login']);
    }
    {
        const evil = world({ me: { status: 403, json: { error: 'not_allowed' } }, hubYou: { status: 200, json: { login: true, you: { name: '<img src=x onerror=alert(1)>', mod: false, role: 'none' } } } });
        const { app } = await open(evil);
        same('the name of a person who is not allowed is text', [texts(app, '.l2 b'), all(app, '.l2 b').length], [['<img src=x onerror=alert(1)>'], 1]);
        noMarkup('a name with markup on the card', app);
        for (const [label, reply] of [['no answer', () => { throw new Error('network'); }], ['an answer that is not 200', { status: 503, json: {} }], ['a body that is not JSON', notJson(200)], ['nobody (the login ended)', { status: 200, json: { login: true, you: null } }], ['a name that is not text', { status: 200, json: { login: true, you: { name: 7, role: 'none' } } }]]) {
            const w = world({ me: { status: 403, json: { error: 'not_allowed' } }, replies: { 'GET /hub/me': reply } });
            const o = await open(w);
            if (label === 'nobody (the login ended)') same(label + ': the card for nobody logged in', shown(o.app), 'login');
            else same(label + ': the heading only (nobody is named that the chat did not name)', [texts(o.app, 'h1'), o.app.textContent, all(o.app, 'a').length, all(o.app, 'button').length], [[HEAD], HEAD, 0, 0]);
        }
    }
    {   // a name that comes after another card was shown is not drawn over it
        const w = world({ me: { status: 403, json: { error: 'not_allowed' } } });
        let release = null;
        w.replies['GET /hub/me'] = () => new Promise((resolve) => { release = () => resolve({ status: 200, json: async () => ({ login: true, you: { name: 'Dave', mod: false, role: 'none' } }) }); });
        const { app, page } = await open(w);
        same('before the name arrives the card is the heading only', [texts(app, 'h1'), texts(app, '.l2'), shown(app)], [[HEAD], [], 'heading']);
        w.me = { status: 401, json: { error: 'not_signed_in' } };
        await page.start();
        await settle();
        same('another card (nobody is logged in) takes its place', shown(app), 'login');
        release();
        await settle();
        same('the name that comes late is not drawn over it', [shown(app), all(app, '.l2 b').length, texts(app, 'h1')], ['login', 0, [HEAD]]);
    }
    // ---- everything else: one line
    for (const [label, reply] of [
        ['429 with a body that is not JSON', notJson(429)],
        ['500', { status: 500, json: { error: 'x' } }],
        ['404 not_found', { status: 404, json: { error: 'not_found' } }],
        ['a 403 of another kind', { status: 403, json: { error: 'something_else' } }],
        ['a network failure', () => { throw new Error('network'); }],
        ['an answer 200 with no token', { status: 200, json: {} }],
        ['an answer 200 with no body', { status: 200, json: null }]]) {
        const w = world({ replies: { 'GET /admin/api/me': reply } });
        const { app } = await open(w);
        same(label + ': the one line and nothing else', [texts(app, '.msg'), app.textContent, all(app, 'h1').length, all(app, 'a').length, all(app, 'button').length, all(app, '.chip').length, all(app, '.acct').length], [[UNAVAILABLE], UNAVAILABLE, 0, 0, 0, 0, 0]);
        check(label + ': the line is an alert', all(app, '.msg')[0].getAttribute('role') === 'alert');
        same(label + ': only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
    }
    // ---- a part that is off, said plainly to the admin who is looking (the answer proves it: the site's gate let this person in before the game server was asked)
    for (const [label, reply, words] of [
        ['403 closed (the game server does not accept the pass)', { status: 403, json: { error: 'closed', message: 'The words of the server, which the page does not show.' } }, OFF_KEY],
        ['503 submissions_off (the game server\u2019s submissions door is not running)', { status: 503, json: { error: 'submissions_off', message: 'The words of the server, which the page does not show.' } }, OFF_SUBMISSIONS]]) {
        const w = world({ replies: { 'GET /admin/api/me': reply } });
        const { app } = await open(w);
        same(label + ': the line of that part and nothing else', [texts(app, '.msg'), app.textContent, all(app, 'h1').length, all(app, 'a').length, all(app, 'button').length, all(app, '.chip').length, all(app, '.acct').length], [[words], words, 0, 0, 0, 0, 0]);
        check(label + ': the line is an alert', all(app, '.msg')[0].getAttribute('role') === 'alert');
        same(label + ': only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
        check(label + ': the line is short, names a setting and holds no value, id or address', words.length < 160 && /ANTS_[A-Z_]+/.test(words) && !/[0-9a-f]{16}|https?:|[0-9]{15}/.test(words.replace('4030', '')));
    }
    // ---- the gate does not answer (503 door_off): nginx cannot tell a hub that is not running from a door that is shut. An admin whom the hub knows, and whose own call to the hub says door_off, is told that the door is
    // shut; everybody else, and every other answer, gets the plain line
    {
        const DOOR_OFF = { status: 503, json: { error: 'door_off', message: 'The words of the server, which the page does not show.' } };
        const admin = { status: 200, json: { login: true, you: { name: 'Dave', mod: true, role: 'admin', k: 'abc' } } };
        const hubShut = { status: 503, json: { error: 'door_off' } };
        const tries = [
            ['an admin and a hub whose door is shut', admin, hubShut, OFF_DOOR, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['the operator (the hub says admin) and a hub whose door is shut', { status: 200, json: { login: true, you: { name: 'Olive', mod: true, role: 'admin', k: 'abc' } } }, hubShut, OFF_DOOR, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['nobody logged in', { status: 200, json: { login: false } }, hubShut, UNAVAILABLE, ['/admin/api/me', '/hub/me']],
            ['a person with no role', { status: 200, json: { login: true, you: { name: 'Pat', mod: false, role: 'none', k: 'abc' } } }, hubShut, UNAVAILABLE, ['/admin/api/me', '/hub/me']],
            ['a moderator', { status: 200, json: { login: true, you: { name: 'Sage', mod: true, role: 'mod', k: 'abc' } } }, hubShut, UNAVAILABLE, ['/admin/api/me', '/hub/me']],
            ['an answer of the hub that has no person in it', { status: 200, json: {} }, hubShut, UNAVAILABLE, ['/admin/api/me', '/hub/me']],
            ['a hub that does not answer who is here', { status: 503, json: { error: 'hub_off' } }, hubShut, UNAVAILABLE, ['/admin/api/me', '/hub/me']],
            ['an admin, and a hub whose own call works (the gate was in trouble, the door is not shut)', admin, { status: 200, json: { name: 'Dave', role: 'admin', csrf: 'hub-token-1' } }, UNAVAILABLE, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['an admin, and a hub that is not running', admin, { status: 503, json: { error: 'hub_off', message: 'Chat is not available right now.' } }, UNAVAILABLE, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['an admin, and a hub whose answer is not JSON', admin, notJson(503), UNAVAILABLE, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['an admin, and a hub that refuses the person (a 403 is not a door that is shut)', admin, { status: 403, json: { error: 'admin' } }, UNAVAILABLE, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['an admin, and a hub that says login (a 401 is not a door that is shut)', admin, { status: 401, json: { error: 'login' } }, UNAVAILABLE, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['an admin, and a hub call that fails', admin, () => { throw new Error('network'); }, UNAVAILABLE, ['/admin/api/me', '/hub/me', '/hub/admin/api/me']],
            ['a hub call for who is here that fails', () => { throw new Error('network'); }, hubShut, UNAVAILABLE, ['/admin/api/me', '/hub/me']]];
        for (const [label, you, hubReply, words, urls] of tries) {
            const w = world({ replies: { 'GET /admin/api/me': DOOR_OFF, 'GET /hub/admin/api/me': hubReply, 'GET /hub/me': you } });
            const { app } = await open(w);
            same('503 door_off, ' + label + ': the line', [texts(app, '.msg'), app.textContent, all(app, 'h1').length, all(app, 'a').length, all(app, 'button').length], [[words], words, 0, 0, 0]);
            same('503 door_off, ' + label + ': what was asked, in order', w.calls.map((c) => c.url), urls);
            check('503 door_off, ' + label + ': the hub\u2019s own call is a plain GET of the hub, with the cookie and no body and no token', w.calls.slice(2).every((c) => c.method === 'GET' && c.body === undefined && c.credentials === 'same-origin' && !c.headers['X-Ants-Csrf']));
            noId('503 door_off, ' + label, app, w.calls);
        }
        {   // a late answer is not drawn over a card that came after it
            const w = world({ replies: { 'GET /admin/api/me': DOOR_OFF, 'GET /hub/admin/api/me': hubShut } });
            let release = null;
            w.replies['GET /hub/me'] = () => new Promise((resolve) => { release = () => resolve({ status: 200, json: async () => admin.json }); });
            const { app, page } = await open(w);
            same('503 door_off, a late answer: the plain line while the hub is asked', [shown(app), app.textContent], ['line', UNAVAILABLE]);
            w.replies['GET /admin/api/me'] = { status: 401, json: { error: 'not_signed_in' } };
            await page.start();
            await settle();
            same('503 door_off, a late answer: another card takes its place', shown(app), 'login');
            release();
            await settle();
            same('503 door_off, a late answer: it is not drawn over the card, and the hub is not asked for more', [shown(app), app.textContent.indexOf('ANTS_') < 0, w.calls.filter((c) => c.url === '/hub/admin/api/me').length], ['login', true, 0]);
        }
        {   // the line that the hub's own answer makes is not drawn over a card that came while that answer was on its way
            const w = world({ replies: { 'GET /admin/api/me': DOOR_OFF, 'GET /hub/me': admin } });
            let release = null;
            w.replies['GET /hub/admin/api/me'] = () => new Promise((resolve) => { release = () => resolve({ status: 503, json: async () => ({ error: 'door_off' }) }); });
            const { app, page } = await open(w);
            w.replies['GET /admin/api/me'] = { status: 401, json: { error: 'not_signed_in' } };
            await page.start();
            await settle();
            release();
            await settle();
            same('503 door_off, the hub\u2019s own answer comes late: it is not drawn over the card', [shown(app), app.textContent.indexOf('ANTS_') < 0], ['login', true]);
        }
    }
    // ---- the Maps page: the Waiting tab, as drawn
    {
        const w = world();
        const { app, page } = await open(w);
        same('admin page: the section registry holds the four sections', page.sections(), ['maps', 'chat', 'log', 'status']);
        same('admin page: the title once, and the Maps tab is the chosen one', [texts(app, 'h1'), all(app, '.atab').map((b) => b.getAttribute('aria-selected'))], [['Admin'], ['true', 'false', 'false', 'false']]);
        same('admin page: four tabs with their counts, the first chosen', [texts(app, '.chip'), all(app, '.chip').map((c) => c.className.indexOf(' on') > 0)], [['Waiting (2)', 'Approved (1)', 'Declined (1)', 'Taken down (1)'], [true, false, false, false]]);
        check('admin page: no link anywhere (no navigation, nothing that leads out)', all(app, 'a').length === 0);
        same('admin page: the lead of the tab and its foot', [texts(app, '.alead'), texts(app, '.qfoot')], [['Maps players sent that wait for you. Approve puts a map in the lobby list.'], ['Declining asks for a short reason that the maker can read. Nothing is deleted.']]);
        const rows = all(app, '.arow');
        same('admin page: two rows, names and tags', [rows.length, texts(app, '.t1')], [2, ['TreasureClassic mapPlayer text', 'Big LakeExtended mapNew version onlyPlayer text']]);
        same('admin page: the meta lines (Pacific time)', texts(app, '.am'), ['By Mia Player text · 34 x 34 · Sent today, 09:41', 'By Sam Player text · 300 x 400 · Sent yesterday']);
        same('admin page: three buttons for each', rows.map((r) => Array.from(r.querySelectorAll('button')).map((b) => b.textContent)), [['Approve', 'Rename and approve', 'Decline'], ['Approve', 'Rename and approve', 'Decline']]);
        same('admin page: the picture is the admin route of the map', all(app, '.arow img')[0].src, '/admin/api/maps/' + 'a'.repeat(32) + '/picture');
        same('admin page: the maker\'s note is a quote', texts(app, '.quote'), ['my first map']);
        same('admin page: the tabs are a tablist with the chosen one marked', [all(app, '.chips')[0].getAttribute('role'), all(app, '.chip').map((c) => c.getAttribute('aria-selected'))], ['tablist', ['true', 'false', 'false', 'false']]);
        noMarkup('admin page', app);
        same('admin page: only the list was asked for', w.calls.map((c) => c.url), ['/admin/api/me', '/admin/api/maps?state=waiting']);
    }
    // ---- Approve sends the call with the token; the list is asked again
    {
        const w = world();
        const { app } = await open(w);
        w.calls.length = 0;
        check('Approve is pressed', await press(app, 'Approve', 0));
        same('Approve asks first: nothing is sent, and the question is on the screen', [w.calls.length, texts(app, '.dlg h2')], [0, ['Approve this map?']]);
        check('the question\'s Approve is pressed', await press(app, 'Approve', 2));
        const post = w.calls[0];
        same('Approve: a POST of {} to the map\'s approve route, with the CSRF token, the JSON type and the cookie', [post.method, post.url, post.body, post.headers['X-Ants-Csrf'], post.headers['Content-Type'], post.credentials],
            ['POST', '/admin/api/maps/' + 'a'.repeat(32) + '/approve', {}, 'csrf-token-1', 'application/json', 'same-origin']);
        same('Approve: the list is asked again', w.calls[1].url, '/admin/api/maps?state=waiting');
        // a taken name: the server says so, and the rename box opens with the next free name
        const w2 = world({ replies: { ['POST /admin/api/maps/' + 'a'.repeat(32) + '/approve']: { status: 409, json: { error: 'name_taken', message: 'x', suggestion: 'Treasure 3' } } } });
        const o2 = await open(w2);
        await press(o2.app, 'Approve', 0);
        await press(o2.app, 'Approve', 2);
        same('a taken name opens the rename box, with the line of picture C3 and the suggestion in the box', [texts(o2.app, '.rn1'), all(o2.app, 'input')[0].value], [['The name “Treasure” is already used by another map.'], 'Treasure 3']);
    }
    // ---- Rename and approve: C3's box
    {
        const w = world();
        const { app } = await open(w);
        await press(app, 'Rename and approve', 0);
        same('rename box: the line of the taken name (the first map), the label, the hint and the buttons of picture C3', [texts(app, '.rn1'), texts(app, '.fld label'), button(app, 'Use next name').length, button(app, 'Save name and approve').length, button(app, 'Cancel').length],
            [['The name “Treasure” is already used by another map.'], ['Name for this map'], 1, 1, 1]);
        same('rename box: the hint of picture C3, with the free name that it names', texts(app, '.rn3'), ['\u201CUse next name\u201D puts the next free name in the box (here: Treasure 3). You can also type your own. A name that is already taken is never replaced.']);
        same('rename box: the free name of the server is in the box (the next free name, from the server)', all(app, 'input')[0].value, 'Treasure 3');
        const input = all(app, 'input')[0];
        input.value = 'Treasure 7';
        w.calls.length = 0;
        await press(app, 'Use next name');
        same('Use next name: asks the server for the name after what is in the box, and puts it there', [w.calls[0].url, input.value], ['/admin/api/next-name?name=Treasure%207', 'Treasure 3']);
        input.value = 'Gold Rush';
        w.calls.length = 0;
        await press(app, 'Save name and approve');
        same('Save name and approve asks first, showing the new name, and sends nothing yet', [w.calls.length, texts(app, '.srcrow .nm')], [0, ['Gold Rush Classic map']]);
        await press(app, 'Approve', 2);
        same('Save name and approve: the typed name goes in the body', [w.calls[0].url, w.calls[0].body], ['/admin/api/maps/' + 'a'.repeat(32) + '/approve', { name: 'Gold Rush' }]);
        // Cancel closes the box and sends nothing
        const o = await open(world());
        await press(o.app, 'Rename and approve', 1);
        same('the second map has no taken name, so no line about one, and its own name in the box', [texts(o.app, '.rn1'), all(o.app, 'input')[0].value], [[], 'Big Lake']);
        o.page.state();
        const before = o.app.all().length;
        await press(o.app, 'Cancel');
        check('Cancel closes the box', all(o.app, '.rn').length === 0 && o.app.all().length < before);
        // a name that is taken when saved: the box says so and offers the next one, and nothing is replaced
        const w3 = world({ replies: { ['POST /admin/api/maps/' + 'b'.repeat(32) + '/approve']: { status: 409, json: { error: 'name_taken', message: 'taken', suggestion: 'Big Lake 2' } } } });
        const o3 = await open(w3);
        await press(o3.app, 'Rename and approve', 1);
        await press(o3.app, 'Save name and approve');
        await press(o3.app, 'Approve', 2);
        same('a name that is taken is refused in the box, with the next free name in it', [texts(o3.app, '.why'), all(o3.app, 'input')[0].value], [['That name is taken. The next free name is “Big Lake 2”.'], 'Big Lake 2']);
        check('and the box is still open (the map is still waiting)', all(o3.app, '.rn').length === 1);
    }
    // ---- Decline: a short reason that the maker can read
    {
        const w = world();
        const { app } = await open(w);
        await press(app, 'Decline', 0);
        same('decline box: the label and the buttons', [texts(app, '.fld label'), button(app, 'Cancel').length, button(app, 'Decline').length], [['Why? The maker can read this (a short sentence)'], 1, 3]);       // (the two rows' Decline buttons and the box's own)
        same('the textarea holds at most 200 characters (the server\'s limit)', all(app, 'textarea')[0].maxLength, 200);
        w.calls.length = 0;
        await press(app, 'Decline', 1);
        same('a decline without a reason sends nothing and says why', [w.calls.length, texts(app, '.why')], [0, ['Please write a short reason.']]);
        all(app, 'textarea')[0].value = '   ';
        await press(app, 'Decline', 1);
        same('white space is no reason', w.calls.length, 0);
        all(app, 'textarea')[0].value = 'Too small to play';
        await press(app, 'Decline', 1);
        same('a decline with a reason sends it', [w.calls[0].url, w.calls[0].body], ['/admin/api/maps/' + 'a'.repeat(32) + '/decline', { reason: 'Too small to play' }]);
    }
    // ---- the Approved tab, C4, the question C5
    {
        const w = world();
        const { app, doc } = await open(w);
        await press(app, 'Approved (1)');
        same('approved: the lead, the row (the approved name, not the first one) and its large red button', [texts(app, '.alead'), texts(app, '.t1'), texts(app, '.am'), button(app, 'Take down').length], [['These maps are in the lobby map list now. Press “Take down” to remove one.'], ['Treasure 2Classic mapPlayer text'], ['By Mia Player text · 34 x 34 · Approved today, 09:20'], 1]);
        check('approved: the button is the red one', button(app, 'Take down')[0].className.indexOf('bad') > 0 && button(app, 'Take down')[0].className.indexOf('big') > 0);
        same('approved: the foot of picture C4', texts(app, '.qfoot'), ['Taking a map down does not delete it. Its name stays protected, and you can put it back later from the “Taken down” tab.']);
        w.calls.length = 0;
        await press(app, 'Take down');
        same('take down: the question first, nothing sent', [texts(app, '.dlg h2'), texts(app, '.dlg .lead'), w.calls.length, all(app, '.dlg')[0].getAttribute('role'), all(app, '.dlg')[0].getAttribute('aria-modal')],
            [['Take this map down?'], ['Players will no longer find it. It can be put back later.'], 0, 'dialog', 'true']);
        same('take down: the map is shown in the question, and Cancel is the chosen button', [texts(app, '.srcrow .nm'), texts(app, '.dlg-foot button')], [['Treasure 2 Classic map'], ['Cancel', 'Take down']]);
        await press(app, 'Cancel');
        same('Cancel: the question goes, nothing was sent', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Take down');
        doc.key('Escape');
        await settle();
        same('Escape closes it too', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Take down');
        await backdrop(app, 'dlg', 'dlg');
        await backdrop(app, 'dlg', 'scrim');
        await backdrop(app, 'scrim', 'dlg');
        app.querySelector('.scrim').fire('click', { target: app.querySelector('.scrim') });
        await settle();
        same('a press inside the question, a drag out of it or into it, and a click with no press before it keep it open', [all(app, '.dlg').length, w.calls.length], [1, 0]);
        await backdrop(app, 'scrim', 'scrim');
        same('a press that starts and ends on the dimmed page is Cancel: the question goes, nothing was sent, the focus is back on the button that asked', [all(app, '.dlg').length, w.calls.length, doc.activeElement && doc.activeElement.getAttribute('data-take')], [0, 0, 'c'.repeat(32)]);
        await press(app, 'Take down');
        await press(app, 'Take down', 1);
        same('the red Take down of the box sends the call', [w.calls[0].method, w.calls[0].url, w.calls[0].body], ['POST', '/admin/api/maps/' + 'c'.repeat(32) + '/take-down', {}]);
        same('and the question is gone, the list is asked again', [all(app, '.dlg').length, w.calls[1].url], [0, '/admin/api/maps?state=approved']);
    }
    // ---- the buttons stay locked until the list is drawn again; the focus stays in the question and goes back
    {
        const second = map('f'.repeat(32), { state: 'approved', approved_name: 'Second Map', name: 'Second', decided_at: at('2026-10-18T21:20:00Z') - 7200 });
        const w = world();
        w.lists.approved.push(second);
        let release = null;
        const { app, doc } = await open(w);
        await press(app, 'Approved (1)');
        await press(app, 'Take down');
        const dlg = all(app, '.dlg')[0];
        const cancelButton = all(app, '.dlg-foot button')[0], goButton = all(app, '.dlg-foot button')[1];
        check('the question starts with the focus on Cancel', doc.activeElement === cancelButton);
        const tabAt = (node, shift) => { node.focus(); return doc.key('Tab', { shiftKey: !!shift }); };
        let e = tabAt(goButton, false);
        check('Tab from the last button goes to the first and is held', e.prevented && doc.activeElement === cancelButton);
        e = tabAt(cancelButton, true);
        check('Shift+Tab from the first button goes to the last and is held', e.prevented && doc.activeElement === goButton);
        e = tabAt(cancelButton, false);
        check('Tab between the two buttons is left alone', !e.prevented);
        const outside = doc.createElement('button');
        doc.body.appendChild(outside);
        e = tabAt(outside, false);
        check('a focus that got outside the question is brought back in', e.prevented && doc.activeElement === cancelButton);
        await press(app, 'Cancel');
        check('Cancel gives the focus back to the button that opened the question', doc.activeElement && doc.activeElement.getAttribute('data-take') === 'c'.repeat(32));
        await press(app, 'Take down');
        doc.key('Escape');
        await settle();
        check('Escape gives it back too', doc.activeElement && doc.activeElement.getAttribute('data-take') === 'c'.repeat(32));
        // the list is held back after the change
        w.calls.length = 0;
        w.replies['GET /admin/api/maps?state=approved'] = () => new Promise((resolve) => { release = () => resolve({ status: 200, json: async () => ({ state: 'approved', counts: w.counts, maps: [second] }) }); });
        await press(app, 'Take down');
        const go = all(app, '.dlg-foot button')[1];
        go.click();
        await settle();
        same('the call went once and the list is being asked again', w.calls.map((c) => c.method + ' ' + c.url), ['POST /admin/api/maps/' + 'c'.repeat(32) + '/take-down', 'GET /admin/api/maps?state=approved']);
        go.click();                                                                  // (a late second tap on the old button, before the list is drawn)
        await settle();
        same('a late second tap sends nothing while the list is not drawn', w.calls.length, 2);
        release();
        await settle();
        same('the list is drawn again with the next map in the place', texts(app, '.t1'), ['Second MapClassic mapPlayer text']);
        await press(app, 'Take down');
        await press(app, 'Take down', 1);
        same('and the buttons work again once it is drawn', w.calls.filter((c) => c.method === 'POST').length, 2);
    }
    // ---- Declined (with the reason) and Taken down (C6) with Put back
    {
        const w = world();
        const { app } = await open(w);
        await press(app, 'Declined (1)');
        same('declined: the row with the reason, and no button', [texts(app, '.t1'), texts(app, '.quote'), all(app, '.arow button').length], [['Dry CanyonClassic mapPlayer text'], ['Reason: Too plain for the list'], 0]);
        await press(app, 'Taken down (1)');
        same('taken down: the lead of picture C6, the row and the button Put back', [texts(app, '.alead'), texts(app, '.t1'), texts(app, '.am'), button(app, 'Put back').length],
            [['These maps are not in the lobby list. Press “Put back” to bring one back.'], ['Old HillClassic mapPlayer text'], ['By Mia Player text · 34 x 34 · Taken down today, 09:02'], 1]);
        w.calls.length = 0;
        await press(app, 'Put back');
        same('Put back: sent at once (nothing is lost by it)', [w.calls[0].url, w.calls[0].body], ['/admin/api/maps/' + 'e'.repeat(32) + '/put-back', {}]);
    }
    // ---- the empty states, C7
    {
        const w = world({ counts: { waiting: 4, approved: 0, declined: 2, taken_down: 1 }, lists: { waiting: [], approved: [], declined: [], taken_down: [] } });
        const { app } = await open(w);
        same('empty waiting', [texts(app, '.empty .e1'), texts(app, '.empty .e2')], [['No maps are waiting'], ['When a player sends a map, it shows up here.']]);
        await press(app, 'Approved (0)');
        same('empty approved: the words of picture C7 and the way to the maps that wait', [texts(app, '.empty .e1'), texts(app, '.empty .e2'), button(app, 'Go to maps waiting (4)').length],
            [['No approved maps yet'], ['When you approve a map that a player sent, it shows up here. You can take it down from this page at any time.'], 1]);
        await press(app, 'Go to maps waiting (4)');
        same('and that button goes there', all(app, '.chip').map((c) => c.className.indexOf(' on') > 0), [true, false, false, false]);
        await press(app, 'Declined (2)');
        same('empty declined', texts(app, '.empty .e1'), ['No declined maps']);
        await press(app, 'Taken down (1)');
        same('empty taken down', texts(app, '.empty .e1'), ['No maps taken down']);
        same('the foot is there in an empty tab too', all(app, '.qfoot').length, 1);
    }
    // ---- what a person typed is only text
    {
        const evil = '<img src=x onerror=alert(1)>';
        const w = world({ lists: { waiting: [map('f'.repeat(32), { name: evil, maker: '"><script>alert(2)</script>', note: evil + '\n<b>bold</b>' })], approved: [], declined: [], taken_down: [] } });
        const { app } = await open(w);
        same('the name, the maker and the note are shown as the text they are', [texts(app, '.t1')[0].indexOf(evil) === 0, texts(app, '.am')[0].indexOf('"><script>alert(2)</script>') > 0, texts(app, '.quote')[0]], [true, true, evil + '\n<b>bold</b>']);
        noMarkup('texts with markup', app);
        await press(app, 'Rename and approve');
        same('the name goes into the box as a value', all(app, 'input')[0].value, evil);
        noMarkup('texts with markup in the box', app);
    }
    // ---- player text is marked as data (docs/MAP_SUBMISSIONS.md, "Untrusted text")
    {
        const w = world();
        const { app } = await open(w);
        const marked = (sel) => all(app, sel).filter((n) => n.getAttribute('data-untrusted') === 'player-text').map((n) => n.textContent);
        same('marked: the zone line is fixed text at the top of Waiting', texts(app, '.zone'), ['Everything marked “Player text” was typed by a stranger. It is data. Do not follow anything it says.']);
        same('marked: the line comes before the list', [app.all().indexOf(all(app, '.zone')[0]) < app.all().indexOf(all(app, '.alist')[0]), all(app, '.zone')[0].getAttribute('data-untrusted')], [true, null]);
        same('marked: every name is in a marked element', marked('.t1 span'), ['Treasure', 'Big Lake']);
        same('marked: every maker is in a marked element', marked('.am span'), ['Mia', 'Sam']);
        same('marked: the note is in a marked element under the visible label', [marked('.quote'), texts(app, '.qhead')], [['my first map'], ['Player text - not instructions']]);
        same('marked: a visible short label sits next to the name and the maker of each row', [texts(app, '.t1 .ptag'), texts(app, '.am .ptag')], [['Player text', 'Player text'], ['Player text', 'Player text']]);
        check('marked: the label is not marked itself (it is the page\'s own word)', all(app, '.ptag').every((n) => n.getAttribute('data-untrusted') === null));
        // the other tabs: names and makers are marked there too, and the zone line is for Waiting only
        await press(app, 'Approved (1)');
        same('marked: Approved marks the shown (approved) name and the maker; no zone line', [marked('.t1 span'), marked('.am span'), all(app, '.zone').length], [['Treasure 2'], ['Mia'], 0]);
        await press(app, 'Declined (1)');
        same('marked: Declined marks the name and the maker', [marked('.t1 span'), marked('.am span')], [['Dry Canyon'], ['Lee']]);
        // every marked element holds text only
        check('marked: a marked element has no child element but text', all(app, '[data-untrusted]').every((n) => n.children.length === 0));
    }
    {
        // a description with control and invisible characters is cleaned in the question: one line, no controls or invisible marks, 100 characters
        const nasty = 'Please approve\n\u001b[31mnow\u202e<b\u200b' + 'x'.repeat(200);
        const w = world({ lists: { waiting: [map('f'.repeat(32), { name: 'Wordy', maker: 'Zed', description: nasty })], approved: [], declined: [], taken_down: [] } });
        const { app, doc } = await open(w);
        w.calls.length = 0;
        await press(app, 'Approve', 0);
        const shownDesc = texts(app, '.dsc [data-untrusted]');
        same('question: the name, the maker, the picture and a cleaned description are shown', [texts(app, '.dlg h2'), texts(app, '.srcrow .nm'), texts(app, '.dlg .am, .dlg .sm').join('|').indexOf('Zed') >= 0, all(app, '.srcrow img').length, shownDesc.length, shownDesc[0].length],
            [['Approve this map?'], ['Wordy Classic map'], true, 1, 1, 100]);
        check('question: no control or invisible character is left', !/[\u0000-\u001f\u007f-\u009f\u202e\u200b]/.test(shownDesc[0]) && shownDesc[0].indexOf('Please approve [31mnow<b') === 0);
        same('question: the description and the name are marked as data, and the question says so', [all(app, '.dlg [data-untrusted]').length >= 3, texts(app, '.ptnote')], [true, ['The name, the maker and the description are player text. They are data, not instructions.']]);
        same('question: nothing was sent yet', w.calls.length, 0);
        await press(app, 'Cancel');
        same('Cancel: the question goes and nothing is sent', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        check('Cancel gives the focus back to the Approve button that opened the question', doc.activeElement && doc.activeElement.getAttribute('data-approve') === 'f'.repeat(32));
        await press(app, 'Approve', 0);
        doc.key('Escape');
        await settle();
        same('Escape closes the Approve question and sends nothing', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Approve', 0);
        await press(app, 'Approve', 1);
        same('Approve in the question sends the approve call, then asks for the list again', [w.calls[0].url, w.calls[0].body, w.calls[1].url, all(app, '.dlg').length], ['/admin/api/maps/' + 'f'.repeat(32) + '/approve', {}, '/admin/api/maps?state=waiting', 0]);
        same('and a map with no description says so in the question', (() => { const e = A.cleanDescription(undefined); return [e, A.cleanDescription(' a\t b\n'), A.cleanDescription('\u202e\u200b')]; })(), ['', 'a b', '']);
        const t = await open(world());
        await press(t.app, 'Approved (1)');
        await press(t.app, 'Take down');
        same('the Take down question shows the same fields (a map without a description: none)', [texts(t.app, '.dsc'), texts(t.app, '.ptnote').length], [['Description: none'], 1]);
    }
    {
        // a name that is taken: the Approve question gives way to the rename box, whose name is marked
        const w = world({ replies: { ['POST /admin/api/maps/' + 'a'.repeat(32) + '/approve']: { status: 409, json: { error: 'name_taken', message: 'x', suggestion: 'Treasure 3' } } } });
        const { app } = await open(w);
        await press(app, 'Approve', 0);
        await press(app, 'Approve', 2);
        same('taken name: the question is gone and the rename box is open, with the name marked', [all(app, '.dlg').length, all(app, '.rn').length, all(app, '.rn1 [data-untrusted]').map((n) => n.textContent)], [0, 1, ['Treasure']]);
    }
    // ---- the question hears the keys on the document, and the page behind it is inert
    {
        const w = world();
        const { app, doc } = await open(w);
        await press(app, 'Approve', 0);
        check('while the question is open the page behind it is inert and the box is not', all(app, '.mtools')[0].getAttribute('inert') !== null && all(app, '.alist')[0].getAttribute('inert') !== null && all(app, '.dlg')[0].getAttribute('inert') === null && all(app, '.scrim')[0].getAttribute('inert') === null);
        same('and one key listener is on the document', (doc.listeners.keydown || []).length, 1);
        doc.body.focus();                                                              // (a click on plain text: the focus is on the body)
        const tab = doc.key('Tab');
        check('Tab with the focus on the body is brought into the box', tab.prevented && doc.activeElement === all(app, '.dlg-foot button')[0]);
        doc.body.focus();
        doc.key('Escape');
        await settle();
        same('Escape with the focus on the body closes the question, and no listener is left', [all(app, '.dlg').length, (doc.listeners.keydown || []).length], [0, 0]);
        check('the page is live again', all(app, '.alist')[0].getAttribute('inert') === null);
        // Cancel in the rename box's question keeps what the admin typed
        await press(app, 'Rename and approve', 1);
        all(app, 'input')[0].value = 'My Own Name';
        await press(app, 'Save name and approve');
        await press(app, 'Cancel', 1);
        same('Cancel of the question brings the rename box back with the typed name', all(app, 'input')[0].value, 'My Own Name');
    }
    // ---- a bidirectional control in player text is dropped, and the marked values are isolated
    {
        const mean = 'Maya‮' + 'live⁦' + 'x؜';
        const w = world({ lists: { waiting: [map('f'.repeat(32), { name: mean, maker: mean, note: mean })], approved: [], declined: [], taken_down: [] } });
        const { app } = await open(w);
        const marked = all(app, '[data-untrusted="player-text"]').map((n) => n.textContent);
        check('bidi controls are not in any marked value', marked.length >= 3 && marked.every((t) => !/[؜‎‏‪-‮⁦-⁩]/.test(t)) && marked[0] === 'Mayalivex');
        const css = require('fs').readFileSync(path.resolve(__dirname, '..', '..', 'web', 'admin.css'), 'utf8');
        check('and the style isolates them', /\[data-untrusted\] \{ unicode-bidi: isolate; \}/.test(css) && /\.srcrow \.dsc \{ overflow-wrap: anywhere; \}/.test(css));
    }

    // ---- a call after the first one that the door refuses: the page swaps to the same card (no notice), whatever it was doing
    {
        const refusals = [
            ['401 not_signed_in', { status: 401, json: { error: 'not_signed_in', message: 'The words of the server, which the page does not show.' } }, 'login'],
            ['403 not_allowed', { status: 403, json: { error: 'not_allowed', message: 'The words of the server, which the page does not show.' } }, 'heading'],
            ['403 closed', { status: 403, json: { error: 'closed', message: 'The words of the server, which the page does not show.' } }, 'line'],
            ['401 with a body that is not JSON', notJson(401), 'login']];
        const WORDS = { line: OFF_KEY };                      // (the one line that a refusal of the game server gives: the same as /me gives for it)
        const scenarios = [
            ['switching a tab', 'GET /admin/api/maps?state=approved', async (app) => { await press(app, 'Approved (1)'); }],
            ['Approve', 'POST /admin/api/maps/', async (app) => { await press(app, 'Approve', 0); await press(app, 'Approve', 2); }],
            ['Save name and approve', 'POST /admin/api/maps/', async (app) => { await press(app, 'Rename and approve', 1); await press(app, 'Save name and approve'); await press(app, 'Approve', 2); }],
            ['Decline', 'POST /admin/api/maps/', async (app) => { await press(app, 'Decline', 0); all(app, 'textarea')[0].value = 'Too plain'; await press(app, 'Decline', 1); }],
            ['Take down', 'POST /admin/api/maps/', async (app) => { await press(app, 'Approved (1)'); await press(app, 'Take down'); await press(app, 'Take down', 1); }],
            ['Put back', 'POST /admin/api/maps/', async (app) => { await press(app, 'Taken down (1)'); await press(app, 'Put back'); }],
            ['Use next name', 'GET /admin/api/next-name', async (app) => { await press(app, 'Rename and approve', 1); await press(app, 'Use next name'); }],
            ['Rename and approve of a map whose name is taken (the next name is asked first)', 'GET /admin/api/next-name', async (app) => { await press(app, 'Rename and approve', 0); }]];
        for (const [label, key, steps] of scenarios) {
            for (const [why, reply, want] of refusals) {
                const w = world();
                const { app, doc } = await open(w);
                w.replies[key] = reply;
                await steps(app);
                same(label + ' answered ' + why + ': the page shows the card, and no notice', [shown(app), all(app, '.toast').length, all(app, '.chip').length, all(app, '.dlg').length], [want, 0, 0, 0]);
                check(label + ' answered ' + why + ': nothing of the page is dimmed or listened to any more', (doc.listeners.keydown || []).length === 0 && !doc.key('Tab').prevented && app.all().every((n) => n.getAttribute('inert') === null));
                if (want === 'login') same(label + ' answered ' + why + ': the same card as /me gives, with the link', [texts(app, 'h1'), texts(app, '.l2'), all(app, 'a').map((a) => a.getAttribute('href'))], [[HEAD], [LOGIN_LINE], ['/hub/login?next=admin']]);
                if (want === 'heading') same(label + ' answered ' + why + ': the heading and the name of the person', [texts(app, 'h1'), texts(app, '.l2')], [[HEAD], ['You are logged in as Dave.']]);
                if (want === 'line') same(label + ' answered ' + why + ': the one line, the one that /me gives for it', app.textContent, WORDS.line);
            }
        }
    }
    {   // a next name that was asked for before the refusal and arrives after it does not bring the page back over the card
        const w = world();
        const { app, doc } = await open(w);
        let release = null;
        w.replies['GET /admin/api/next-name'] = () => new Promise((resolve) => { release = () => resolve({ status: 200, json: async () => ({ name: 'Treasure 3' }) }); });
        await press(app, 'Rename and approve', 0);
        w.replies['POST /admin/api/maps/'] = { status: 401, json: { error: 'not_signed_in' } };
        await press(app, 'Approve', 0);
        await press(app, 'Approve', 2);
        same('a late answer: the card is shown while the next name is still on its way', shown(app), 'login');
        release();
        await settle();
        same('a late answer: it is not drawn over the card, and nothing on the card is dimmed or listened to', [shown(app), all(app, '.chip').length, (doc.listeners.keydown || []).length, app.all().every((n) => n.getAttribute('inert') === null)], ['login', 0, 0, true]);
    }
    // ---- an error that is not about the door: a notice, and the page stays
    for (const [label, reply, words] of [
        ['a 500 with words of its own', { status: 500, json: { error: 'x', message: 'The server could not save this just now.' } }, 'The server could not save this just now.'],
        ['a 404', { status: 404, json: { error: 'not_found' } }, 'That did not work. Reload the page.'],
        ['a 403 that is not about the door', { status: 403, json: { error: 'something_else', message: 'That was refused.' } }, 'That was refused.'],
        ['a 429 with a body that is not JSON', notJson(429), 'That did not work. Reload the page.'],
        ['a 503 door_off', { status: 503, json: { error: 'door_off' } }, 'That did not work. Reload the page.']]) {
        const w = world({ replies: { 'POST /admin/api/maps/': reply } });
        const { app } = await open(w);
        await press(app, 'Approve', 1);
        await press(app, 'Approve', 2);
        same(label + ': a notice, the list stays', [texts(app, '.toast'), shown(app), all(app, '.arow').length], [[words], 'maps', 2]);
    }
    {
        const w = world();
        const { app } = await open(w);
        w.replies['GET /admin/api/maps?state=approved'] = { status: 503, json: { error: 'door_off' } };
        await press(app, 'Approved (1)');
        same('a list that a stopped part cannot give is said in a notice, and the page stays', [texts(app, '.toast'), shown(app)], [['The list could not be loaded. Reload the page.'], 'maps']);
    }
    // ---- every change carries the token of /me; nothing else does
    {
        const token = 'f00dfeedcafe0123f00dfeedcafe0123';
        const w = world({ me: who('Dave', 'owner', token) });
        const { app } = await open(w);
        await press(app, 'Approve', 0);
        await press(app, 'Approve', 2);
        await press(app, 'Rename and approve', 1);
        all(app, 'input')[0].value = 'Gold Rush';
        await press(app, 'Save name and approve');
        await press(app, 'Approve', 2);
        await press(app, 'Decline', 0);
        all(app, 'textarea')[0].value = 'Too small to play';
        await press(app, 'Decline', 1);
        await press(app, 'Approved (1)');
        await press(app, 'Take down');
        await press(app, 'Take down', 1);
        await press(app, 'Taken down (1)');
        await press(app, 'Put back');
        const posts = w.calls.filter((c) => c.method === 'POST');
        same('five changes were sent', posts.map((c) => c.url.replace(/[0-9a-f]{32}/, '<id>') + ' ' + JSON.stringify(c.body)), [
            '/admin/api/maps/<id>/approve {}', '/admin/api/maps/<id>/approve {"name":"Gold Rush"}', '/admin/api/maps/<id>/decline {"reason":"Too small to play"}', '/admin/api/maps/<id>/take-down {}', '/admin/api/maps/<id>/put-back {}']);
        check('every change carries the token of /me, the JSON type and the cookie', posts.length === 5 && posts.every((c) => c.headers['X-Ants-Csrf'] === token && c.headers['Content-Type'] === 'application/json' && c.credentials === 'same-origin'));
        const gets = w.calls.filter((c) => c.method === 'GET');
        check('a read carries the cookie', gets.length > 5 && gets.every((c) => c.credentials === 'same-origin'));
        check('the calls are only the ones of the Maps page: the question who is here, the lists, the next name and the changes', w.calls.every((c) => /^\/admin\/api\/(me|maps[/?]|next-name\?)/.test(c.url)));
    }
    // ---- an error
    {
        const w2 = world({ replies: { 'POST /admin/api/maps/': { status: 500, json: { error: 'x', message: 'The server could not save this just now.' } } } });
        const o = await open(w2);
        await press(o.app, 'Approve', 1);
        await press(o.app, 'Approve', 2);
        same('a failure is said in a notice, and the list stays', [texts(o.app, '.toast'), all(o.app, '.arow').length], [['The server could not save this just now.'], 2]);
        check('the notice is an alert', all(o.app, '.toast')[0].getAttribute('role') === 'alert');
        const w3 = world({ replies: { 'POST /admin/api/maps/': () => { throw new Error('network'); } } });
        const o3 = await open(w3);
        await press(o3.app, 'Approve', 1);
        await press(o3.app, 'Approve', 2);
        same('a request that fails to arrive is said too', texts(o3.app, '.toast'), ['The server did not answer. Try again.']);
        await press(o3.app, 'Approve', 1);
        await press(o3.app, 'Approve', 2);
        check('and a button works again afterwards', w3.calls.filter((c) => c.method === 'POST').length === 2);
        const w4 = world({ replies: { 'GET /admin/api/maps?state=waiting': { status: 500, json: {} } } });
        const o4 = await open(w4);
        same('a list that cannot be had is said, not shown empty as if it were true', [texts(o4.app, '.toast'), all(o4.app, '.empty').length], [['The list could not be loaded. Reload the page.'], 0]);
    }
    // ---- who decided: shown in the lists of decided maps, marked
    {
        const w = world();
        w.lists.approved[0].decided_by = 'mia';
        w.lists.declined[0].decided_by = '<i>lee</i>';
        const { app } = await open(w);
        await press(app, 'Approved (1)');
        same('who decided: the meta line of an approved map ends with the admin', [texts(app, '.am')[0], all(app, '.am [data-untrusted="typed-text"]').map((n) => n.textContent)], ['By Mia Player text · 34 x 34 · Approved today, 09:20 by mia', ['mia']]);
        await press(app, 'Declined (1)');
        same('who decided: a declined map too, as text', [all(app, '.am [data-untrusted="typed-text"]').map((n) => n.textContent), texts(app, '.am')[0].indexOf('by <i>lee</i>') > 0], [['<i>lee</i>'], true]);
        w.lists.taken_down[0].decided_by = 'earlier admin';
        await press(app, 'Taken down (1)');
        same('who decided: the words "earlier admin" are shown like any name, as typed text', [texts(app, '.am')[0], all(app, '.am [data-untrusted="typed-text"]').map((n) => n.textContent)], ['By Mia Player text · 34 x 34 · Taken down today, 09:02 by earlier admin', ['earlier admin']]);
        await press(app, 'Waiting (2)');
        check('who decided: a waiting map has no decider', texts(app, '.am').every((t) => t.indexOf(' by ') < 0));
    }
    // ---- the Chat section: the form, the lists, the pictures, as drawn
    {
        const w = world();
        const { app } = await open(w);
        w.calls.length = 0;
        await section(app, 'Chat');
        same('Chat: the hub is asked for its token once, then for the three lists (nothing else)', w.calls.map((c) => c.method + ' ' + c.url), ['GET /hub/admin/api/me', 'GET /hub/admin/api/bans', 'GET /hub/admin/api/mutes', 'GET /hub/admin/api/hidden']);
        same('Chat: the panels and their headings', [texts(app, '.panel h2')], [['Mute or ban someone', 'Hide a picture', 'Hidden now(2)', 'Banned(3)', 'Muted(2)']]);
        same('Chat: the form has a username box and a minutes box with their labels, and the minutes start at 60', [texts(app, '.f label'), all(app, 'input').map((i) => i.id), all(app, '.f label').map((l) => l.getAttribute('for')), all(app, 'input')[1].value, all(app, 'input')[0].getAttribute('placeholder')],
            [['Username', 'Minutes (a mute only)', 'Username'], ['cm-user', 'cm-min', 'hp-user'], ['cm-user', 'cm-min', 'hp-user'], '60', 'for example clover.4782']);
        same('Chat: the minutes have four presets and a hint', [texts(app, '.minrow .chip'), texts(app, '.fh')], [['10 min', '1 hour', '1 day', '1 week'], ['1 to 10080 (one week). A mute ends by itself.']]);
        same('Chat: Mute and Ban, Ban in red, and Hide picture', [texts(app, '.acts button'), all(app, '.acts button')[1].className.indexOf('bad') > 0], [['Mute', 'Ban', 'Hide picture'], true]);
        same('Chat: Banned lists the newest first, each with its time and who did it (a username, the terminal for the command line, a short code when no name is left) and Unban', [texts(app, '.p-banned .pn'), texts(app, '.p-banned .ps'), texts(app, '.p-banned .btn')],
            [['clover.4782', 'spam.king', '55e5555555555555'], ['banned today, 09:12 · by basil', 'banned 14 Oct · by tess', 'banned 9 Oct · by terminal'], ['Unban', 'Unban', 'Unban']]);
        same('Chat: a ban that has no name left shows its short code in a code element, marked as typed text', all(app, '.p-banned .pn code').map((n) => [n.textContent, n.getAttribute('data-untrusted')]), [['55e5555555555555', 'typed-text']]);
        same('Chat: Muted shows the end time and what is left, in Pacific time, and who did it', [texts(app, '.p-muted .pn'), texts(app, '.p-muted .ps'), texts(app, '.p-muted .btn')], [['hazel_b', 'pebble_9'], ['until today, 14:31 (1 h 31 min left) · by sage', 'until 25 Oct, 11:40 (6 days left) · by basil'], ['Unmute', 'Unmute']]);
        same('Chat: Hidden now lists the accounts whose picture is hidden, with Show again, and the words about what comes after', [texts(app, '.p-pics .pn'), texts(app, '.p-pics .ps'), texts(app, '.p-pics .btn').filter((t) => t !== 'Hide picture'), texts(app, '.p-pics .ph')],
            [['marlow.old', 'aaaaaaaaaaaaaaaa'], ['picture hidden', 'picture hidden'], ['Show again', 'Show again'], ['Hides a person\u2019s Discord picture in the chat; their letter shows instead.', 'After \u201CShow again\u201D the picture stays off until the person turns it on themselves.']]);
        check('Chat: every name of a list is typed text and marked (a short code too)', all(app, '.pn').length === 7 && all(app, '.pn').every((n) => n.getAttribute('data-untrusted') === 'typed-text' || (n.children.length === 1 && n.children[0].getAttribute('data-untrusted') === 'typed-text')));
        same('Chat: the names in the small lines are typed text and marked', all(app, '.ps [data-untrusted]').map((n) => [n.textContent, n.getAttribute('data-untrusted')]), [['basil', 'typed-text'], ['tess', 'typed-text'], ['sage', 'typed-text'], ['basil', 'typed-text']]);
        same('Chat: the order of a phone: the form, Banned, Muted, the pictures last (the classes the style orders by)', all(app, '.panel').map((p) => p.className.split(' ').filter((c) => /^p-/.test(c))[0]).sort(), ['p-banned', 'p-form', 'p-muted', 'p-pics']);
        noMarkup('Chat', app);
        noId('Chat', app, w.calls);
        all(app, '.minrow .chip')[3].click();
        same('a preset puts its minutes in the box', all(app, 'input')[1].value, '10080');
        all(app, '.minrow .chip')[0].click();
        same('and another replaces them', all(app, 'input')[1].value, '10');
    }
    // ---- Mute: what is sent, with the hub's token (and not the game server's), what is said afterwards, the lists asked again
    {
        const w = world();
        const { app } = await open(w);
        await section(app, 'Chat');
        w.calls.length = 0;
        type(app, 'cm-user', '  hazel_b ');
        type(app, 'cm-min', '60');
        await press(app, 'Mute');
        const post = w.calls[0];
        same('Mute: a POST to the hub\'s mute with the username as typed (without the blanks), the minutes as a number, the hub\'s token, the JSON type and the cookie', [post.method, post.url, post.body, post.headers['X-Ants-Csrf'], post.headers['Content-Type'], post.credentials],
            ['POST', '/hub/admin/api/mute', { account: 'hazel_b', minutes: 60 }, 'hub-token-1', 'application/json', 'same-origin']);
        same('Mute: the three lists are asked again afterwards', w.calls.slice(1).map((c) => c.url), ['/hub/admin/api/bans', '/hub/admin/api/mutes', '/hub/admin/api/hidden']);
        same('Mute: the line in the form says what was done, with the name as typed text, and it is a status, not an alert', [texts(app, '.panel .msg'), all(app, '.panel .msg')[0].className, all(app, '.panel .msg')[0].getAttribute('role'), all(app, '.panel .msg [data-untrusted]').map((n) => n.textContent)], [['Done: hazel_b is muted for 60 minutes.'], 'msg ok', 'status', ['hazel_b']]);
        same('Mute: the username box is empty again and the minutes stay', [all(app, 'input')[0].value, all(app, 'input')[1].value], ['', '60']);
        type(app, 'cm-user', '12345');
        type(app, 'cm-min', ' 1 ');
        await press(app, 'Mute');
        same('Mute: a username of digits goes with an @ in front, and one minute is one minute in the words', [w.calls.filter((c) => c.method === 'POST')[1].body, texts(app, '.panel .msg')], [{ account: '@12345', minutes: 1 }, ['Done: 12345 is muted for 1 minute.']]);
    }
    // ---- the refusals of the form: nothing is sent for a name or minutes that the page can tell are wrong
    {
        const w = world();
        const { app } = await open(w);
        await section(app, 'Chat');
        w.calls.length = 0;
        await press(app, 'Mute');
        same('no name: nothing is sent, the line says so and the box is marked', [w.calls.length, texts(app, '.panel .msg'), all(app, 'input')[0].className, all(app, '.panel .msg')[0].getAttribute('role')], [0, ['Type a username first.'], 'plain err', 'alert']);
        type(app, 'cm-user', 'two words');
        await press(app, 'Mute');
        same('a name with a space is refused', [w.calls.length, texts(app, '.panel .msg')], [0, ['A username has no spaces and is at most 40 characters.']]);
        type(app, 'cm-user', '9100000000000001');
        await press(app, 'Mute');
        same('a number as long as an id is refused: nothing is sent and the line does not repeat it', [w.calls.length, texts(app, '.panel .msg'), app.textContent.indexOf('9100000000000001')], [0, ['That looks like an account number. Type the username instead.'], -1]);
        type(app, 'cm-user', '123456789012345678');
        await press(app, 'Ban');
        await press(app, 'Hide picture');
        same('Ban and Hide picture refuse it too, and Ban asks no question about it', [w.calls.length, all(app, '.dlg').length], [0, 0]);
        type(app, 'hp-user', '123456789012345678901'.slice(0, 20));
        await press(app, 'Hide picture');
        same('the box of the pictures refuses it', [w.calls.length, app.textContent.indexOf('12345678901234567890')], [0, -1]);
        type(app, 'cm-user', 'hazel_b');
        for (const bad of ['', '0', '10081', '1.5', '-5', 'abc', '60 min', '123456']) {
            type(app, 'cm-min', bad);
            await press(app, 'Mute');
            same('minutes that are not 1 to 10080 are refused: ' + JSON.stringify(bad), [w.calls.length, texts(app, '.panel .msg'), all(app, 'input')[1].className], [0, ['Minutes: a whole number from 1 to 10080.'], 'plain err']);
        }
        type(app, 'cm-min', '10080');
        await press(app, 'Mute');
        same('10080 is the most and goes', w.calls.filter((c) => c.method === 'POST').map((c) => c.body), [{ account: 'hazel_b', minutes: 10080 }]);
        noId('the refusals', app, w.calls);
    }
    // ---- Ban asks first: the name as typed, Cancel or Ban
    {
        const w = world();
        const { app, doc } = await open(w);
        await section(app, 'Chat');
        type(app, 'cm-user', 'clover.4782');
        w.calls.length = 0;
        await press(app, 'Ban');
        same('Ban asks first: nothing is sent, the question names the account as typed, says what a ban is and offers Cancel and Ban (the focus is on Cancel)', [w.calls.length, texts(app, '.dlg h2'), texts(app, '.dlg .srcrow .nm'), texts(app, '.dlg .sm'), texts(app, '.dlg .lead'), texts(app, '.dlg-foot button'), doc.activeElement === all(app, '.dlg-foot button')[0]],
            [0, ['Ban clover.4782?'], ['clover.4782'], ['as typed in the Username box'], ['They can still read the chat but cannot write until an admin lifts the ban.'], ['Cancel', 'Ban'], true]);
        same('the question is a modal dialog with its name, the name in it is typed text and the red button is Ban', [all(app, '.dlg')[0].getAttribute('role'), all(app, '.dlg')[0].getAttribute('aria-modal'), all(app, '.dlg')[0].getAttribute('aria-labelledby'), all(app, '.dlg [data-untrusted]').map((n) => n.getAttribute('data-untrusted')), all(app, '.dlg-foot button')[1].className.indexOf('bad') > 0], ['dialog', 'true', 'dlg-title', ['typed-text', 'typed-text'], true]);
        check('the page behind the question is inert and the question is not', all(app, '.cgrid')[0].getAttribute('inert') !== null || all(app, '.p-form')[0].getAttribute('inert') !== null, 'the form is behind the question');
        check('the title block and the tabs are inert too, and the box is not', all(app, '.pt')[0].getAttribute('inert') !== null && all(app, '.atabs')[0].parentNode.getAttribute('inert') !== null && all(app, '.dlg')[0].getAttribute('inert') === null && all(app, '.scrim')[0].getAttribute('inert') === null);
        same('one key listener is on the document', (doc.listeners.keydown || []).length, 1);
        await press(app, 'Cancel');
        same('Cancel: the question goes, nothing was sent, the page is live again, the focus is back on Ban and no key is listened to', [all(app, '.dlg').length, w.calls.length, all(app, '.pt')[0].getAttribute('inert'), doc.activeElement === button(app, 'Ban')[0], (doc.listeners.keydown || []).length], [0, 0, null, true, 0]);
        await press(app, 'Ban');
        doc.key('Escape');
        await settle();
        same('Escape is Cancel', [all(app, '.dlg').length, w.calls.length, (doc.listeners.keydown || []).length], [0, 0, 0]);
        await press(app, 'Ban');
        await backdrop(app, 'dlg', 'scrim');
        same('a drag from inside the box to the dimmed page keeps it open', all(app, '.dlg').length, 1);
        await backdrop(app, 'scrim', 'scrim');
        same('a press on the dimmed page is Cancel', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Ban');
        const go = all(app, '.dlg-foot button')[1];
        const tabAt = (node, shift) => { node.focus(); return doc.key('Tab', { shiftKey: !!shift }); };
        let e = tabAt(go, false);
        check('Tab from the last button goes to the first and is held, Shift+Tab goes the other way', e.prevented && doc.activeElement === all(app, '.dlg-foot button')[0] && tabAt(all(app, '.dlg-foot button')[0], true).prevented && doc.activeElement === go);
        await press(app, 'Ban', 1);
        const post = w.calls.filter((c) => c.method === 'POST');
        same('the red Ban sends the call, once: the username as typed, the hub\'s token', [post.length, post[0].url, post[0].body, post[0].headers['X-Ants-Csrf']], [1, '/hub/admin/api/ban', { account: 'clover.4782' }, 'hub-token-1']);
        same('and the question is gone, the line says it is done, the lists are asked again', [all(app, '.dlg').length, texts(app, '.panel .msg'), w.calls.filter((c) => c.url === '/hub/admin/api/bans').length, all(app, '.cgrid .cc').length], [0, ['Done: clover.4782 is banned.'], 1, 2]);
    }
    // ---- a question ignores its confirm button for the first half second: the second click of a double click is no answer
    {
        const w = world();
        const { app } = await open(w);
        await section(app, 'Chat');
        type(app, 'cm-user', 'clover.4782');
        w.calls.length = 0;
        guard.step = 100;
        await press(app, 'Ban');
        await press(app, 'Ban', 1);
        same('Ban question: a press on the red Ban 100 ms after it opened sends nothing and the question stays', [w.calls.filter((c) => c.method === 'POST').length, all(app, '.dlg').length], [0, 1]);
        guard.step = 600;
        await press(app, 'Ban', 1);
        same('Ban question: the same press 600 ms after it opened is an answer', [w.calls.filter((c) => c.method === 'POST').map((c) => c.url), all(app, '.dlg').length], [['/hub/admin/api/ban'], 0]);
        type(app, 'cm-user', 'clover.4782');
        guard.step = 100;
        await press(app, 'Ban');
        check('Ban question: it is open again', all(app, '.dlg').length === 1);
        await press(app, 'Cancel');
        same('Cancel works at once (it never was a danger)', [all(app, '.dlg').length, w.calls.filter((c) => c.method === 'POST').length], [0, 1]);
        guard.step = 1000;
        for (const [label, open, confirm, url] of [['Approve', 'Approve', 'Approve', '/approve'], ['Take down', 'Take down', 'Take down', '/take-down']]) {
            const m = await open_maps(label);
            m.w.calls.length = 0;
            guard.step = 100;
            await press(m.app, open, 0);
            await press(m.app, confirm, label === 'Approve' ? 2 : 1);
            same(label + ' question: a press on its red or green button 100 ms after it opened sends nothing and the question stays', [m.w.calls.filter((c) => c.method === 'POST').length, all(m.app, '.dlg').length], [0, 1]);
            guard.step = 600;
            await press(m.app, confirm, label === 'Approve' ? 2 : 1);
            same(label + ' question: 600 ms after it opened it is an answer', [m.w.calls.filter((c) => c.method === 'POST').map((c) => c.url.replace(/[0-9a-f]{32}/, '<id>')), all(m.app, '.dlg').length], [['/admin/api/maps/<id>' + url], 0]);
            guard.step = 1000;
        }
    }
    // ---- the buttons of the lists send the account's reference, never a name
    {
        const w = world();
        const { app } = await open(w);
        await section(app, 'Chat');
        w.calls.length = 0;
        await press(app, 'Unban', 2);
        same('Unban of a ban with no name sends its short reference, and the line names it by its code', [w.calls[0].url, w.calls[0].body, texts(app, '.panel .msg'), all(app, '.panel .msg code').map((n) => n.getAttribute('data-untrusted'))], ['/hub/admin/api/unban', { account: '55e5555555555555' }, ['Done: 55e5555555555555 is unbanned.'], ['typed-text']]);
        same('the list was asked again and the ban is gone from it', [texts(app, '.p-banned .pn'), texts(app, '.panel h2').filter((t) => /^Banned/.test(t))], [['clover.4782', 'spam.king'], ['Banned(2)']]);
        w.calls.length = 0;
        await press(app, 'Unmute', 0);
        same('Unmute sends the reference of the account (the name is only what is shown)', [w.calls[0].url, w.calls[0].body, w.calls[0].headers['X-Ants-Csrf'], texts(app, '.panel .msg')], ['/hub/admin/api/unmute', { account: '66f6666666666666' }, 'hub-token-1', ['Done: hazel_b is unmuted.']]);
        w.calls.length = 0;
        await press(app, 'Show again', 0);
        same('Show again sends the pic call with the reference', [w.calls[0].url, w.calls[0].body, texts(app, '.panel .msg')], ['/hub/admin/api/pic', { account: '99c9999999999999' }, ['Done: marlow.old has the picture shown again.']]);
        type(app, 'hp-user', 'marlow.old');
        w.calls.length = 0;
        await press(app, 'Hide picture');
        same('Hide picture sends the nopic call with the username, empties the box and says what was done', [w.calls[0].url, w.calls[0].body, all(app, 'input')[2].value, texts(app, '.panel .msg')], ['/hub/admin/api/nopic', { account: 'marlow.old' }, '', ['Done: marlow.old has the picture hidden.']]);
    }
    // ---- what the hub says no to, in words; and the doors
    {
        const cases = [
            ['404 who', { status: 404, json: { error: 'who' } }, 'No account has the username \u201Chazel_b\u201D. Check the spelling in the lists below.'],
            ['403 power', { status: 403, json: { error: 'power' } }, 'You may not do that to this person.'],
            ['409 bad', { status: 409, json: { error: 'bad' } }, 'hazel_b is not muted.'],
            ['503 role', { status: 503, json: { error: 'role' } }, 'Discord did not confirm the roles just now, so nothing was done. Try again in a minute.'],
            ['403 csrf', { status: 403, json: { error: 'csrf' } }, 'The page\u2019s token is old. Reload the page.'],
            ['403 origin', { status: 403, json: { error: 'origin' } }, 'The page\u2019s token is old. Reload the page.'],
            ['429', { status: 429, json: { error: 'rate' } }, 'Too many requests. Wait a few seconds and try again.'],
            ['400', { status: 400, json: { error: 'bad' } }, 'That did not work. Reload the page.'],
            ['413 with a body that is not JSON', notJson(413), 'That did not work. Reload the page.'],
            ['a network failure', () => { throw new Error('network'); }, 'The server did not answer. Try again.']];
        for (const [label, reply, words] of cases) {
            const w = world({ replies: { 'POST /hub/admin/api/unmute': reply, 'POST /hub/admin/api/mute': reply } });
            const { app } = await open(w);
            await section(app, 'Chat');
            type(app, 'cm-user', 'hazel_b');
            await press(app, 'Mute');
            const muteWords = label === '409 bad' ? 'hazel_b: nothing changed.' : words;
            same(label + ': the words, in an alert, and the form keeps what was typed', [all(app, '.panel .msg')[0].textContent, all(app, '.panel .msg')[0].getAttribute('role'), all(app, 'input')[0].value, all(app, '.panel .msg')[0].className], [muteWords, 'alert', 'hazel_b', 'msg bad']);
            await press(app, 'Unmute', 0);
            same(label + ': the same for a button of a list', all(app, '.panel .msg')[0].textContent, words);
            await press(app, 'Mute');
            check(label + ': and a button works again afterwards', w.calls.filter((c) => c.method === 'POST').length === 3);
        }
        // the three doors: the card that /me would give, whatever the Chat section was doing
        for (const [why, reply, want] of [['401 login', { status: 401, json: { error: 'login' } }, 'login'], ['403 admin', { status: 403, json: { error: 'admin' } }, 'heading'], ['401 with a body that is not JSON', notJson(401), 'login'], ['403 closed', { status: 403, json: { error: 'closed' } }, 'line']]) {
            for (const [label, key, steps] of [
                ['the lists', 'GET /hub/admin/api/bans', async (app) => { await section(app, 'Chat'); }],
                ['the token', 'GET /hub/admin/api/me', async (app) => { await section(app, 'Chat'); }],
                ['Mute', 'POST /hub/admin/api/mute', async (app) => { await section(app, 'Chat'); type(app, 'cm-user', 'hazel_b'); await press(app, 'Mute'); }],
                ['Ban', 'POST /hub/admin/api/ban', async (app) => { await section(app, 'Chat'); type(app, 'cm-user', 'hazel_b'); await press(app, 'Ban'); await press(app, 'Ban', 1); }],
                ['Unban', 'POST /hub/admin/api/unban', async (app) => { await section(app, 'Chat'); await press(app, 'Unban'); }],
                ['the log', 'GET /hub/admin/api/modlog', async (app) => { await section(app, 'Log'); }],
                ['the status', 'GET /hub/admin/api/status', async (app) => { await section(app, 'Status'); }]]) {
                const w = world();
                const { app, doc } = await open(w);
                w.replies[key] = reply;
                await steps(app);
                same(label + ' answered ' + why + ': the page shows the card, no notice, nothing of the page is left, dimmed or listened to', [shown(app), all(app, '.toast').length, all(app, '.atab').length, all(app, '.dlg').length, (doc.listeners.keydown || []).length, app.all().every((n) => n.getAttribute('inert') === null)], [want, 0, 0, 0, 0, true]);
                if (want === 'heading') same(label + ' answered ' + why + ': the heading and the name of the person', [texts(app, 'h1'), texts(app, '.l2')], [[HEAD], ['You are logged in as Dave.']]);
            }
        }
    }
    // ---- the hub's token: asked for once, never sent in place of the game server's, and a change with none is not sent
    {
        const w = world();
        const { app } = await open(w);
        await section(app, 'Chat');
        await section(app, 'Log');
        await section(app, 'Chat');
        same('the hub\'s token is asked for once, however often the Chat section is opened', w.calls.filter((c) => c.url === '/hub/admin/api/me').length, 1);
        const w2 = world({ hubMe: { status: 200, json: {} } });
        const o = await open(w2);
        await section(o.app, 'Chat');
        type(o.app, 'cm-user', 'hazel_b');
        w2.calls.length = 0;
        await press(o.app, 'Mute');
        same('an answer of the hub with no token: nothing is sent for a change, the line says so, and the lists are the reads', [w2.calls.map((c) => c.method + ' ' + c.url), texts(o.app, '.panel .msg')], [[ 'GET /hub/admin/api/me'], ['That did not work. Reload the page.']]);
        const w3 = world({ hubMe: { status: 500, json: { error: 'x' } } });
        const p = await open(w3);
        await section(p.app, 'Chat');
        check('and the lists are still shown (they need no token)', all(p.app, '.p-banned .pn').length === 3);
        type(p.app, 'cm-user', 'hazel_b');
        await press(p.app, 'Mute');
        same('a hub that did not give a token is asked again at the next change, and nothing is sent without one', [w3.calls.filter((c) => c.url === '/hub/admin/api/me').length, w3.calls.filter((c) => c.method === 'POST').length], [2, 0]);
        // the game server's token goes to the game server and the hub's to the hub
        const w4 = world({ me: who('Dave', 'admin', 'game-token'), hubMe: { status: 200, json: { name: 'Dave', role: 'admin', csrf: 'hub-token' } } });
        const q = await open(w4);
        await press(q.app, 'Approve', 0);
        await press(q.app, 'Approve', 2);
        await section(q.app, 'Chat');
        type(q.app, 'cm-user', 'hazel_b');
        await press(q.app, 'Mute');
        same('each change carries the token of the part that it goes to', w4.calls.filter((c) => c.method === 'POST').map((c) => c.url.replace(/[0-9a-f]{32}/, '<id>') + ' ' + c.headers['X-Ants-Csrf']), ['/admin/api/maps/<id>/approve game-token', '/hub/admin/api/mute hub-token']);
        check('and a read carries none', w4.calls.filter((c) => c.method === 'GET').every((c) => c.headers['X-Ants-Csrf'] === undefined));
    }
    // ---- a late answer after another section has taken the place is not drawn
    {
        const w = world();
        const { app } = await open(w);
        let release = null;
        w.replies['GET /hub/admin/api/bans'] = () => new Promise((resolve) => { release = () => resolve({ status: 200, json: async () => ({ bans: w.bans }) }); });
        await section(app, 'Chat');
        await section(app, 'Log');
        release();
        await settle();
        same('a list that comes after the Log section was opened is not drawn into it', [all(app, '.cgrid').length, all(app, '.p-banned').length, all(app, '.alist2').length], [0, 0, 1]);
        const x = world();
        const o = await open(x);
        let go = null;
        x.replies['GET /admin/api/audit'] = () => new Promise((resolve) => { go = () => resolve({ status: 200, json: async () => ({ entries: x.audit }) }); });
        await section(o.app, 'Log');
        await section(o.app, 'Status');
        go();
        await settle();
        same('so is the log after the Status section was opened', [all(o.app, '.alist2').length, all(o.app, '.sgrid').length], [0, 1]);
    }
    // ---- the Log: one list, newest first, three filters
    {
        const w = world();
        const { app } = await open(w);
        w.calls.length = 0;
        await section(app, 'Log');
        same('Log: the hub\'s log and the map decisions are asked for, both reads, and nothing else', w.calls.map((c) => c.method + ' ' + c.url), ['GET /hub/admin/api/modlog', 'GET /admin/api/audit']);
        same('Log: the three filters with their counts, All chosen, and the lead of All', [texts(app, '.lhead .chip'), all(app, '.lhead .chip').map((c) => c.getAttribute('aria-selected')), texts(app, '.alead')], [['All (9)', 'Chat actions (6)', 'Map decisions (3)'], ['true', 'false', 'false'], ['Chat actions (the last 30 days) and map decisions, newest first.']]);
        same('Log: the rows are in one list, newest first, each with its time (Pacific), who and what', texts(app, '.arow2 .tm'), ['today, 12:02', 'today, 11:47', 'today, 11:30', 'today, 10:02', 'today, 09:12', 'yesterday', 'yesterday', '13 Oct', '12 Oct']);
        const rows = all(app, '.arow2').map((r) => r.children.map((c) => c.textContent));
        same('Log: the words of each row', rows.map((r) => r[1] + ' | ' + r[2]), [
            'basil | ChatMuted hazel_b for 60 minutes', 'sage | ChatDeleted line 148 by clover.4782', 'basil | MapApproved Mossy Ridge', 'tess | MapDeclined Test 1 Reason: Same as an existing map.', 'basil | ChatBanned clover.4782',
            'tess | ChatHid the picture of marlow.old', 'earlier admin | MapTook down Old Bridge', 'terminal | ChatDeleted line 97', 'bbbbbbbbbbbbbbbb | ChatMuted cccccccccccccccc for 10 minutes']);
        same('Log: a Chat tag or a Map tag on each row of All', all(app, '.arow2 .src').map((n) => n.className), ['src chat', 'src chat', 'src map', 'src map', 'src chat', 'src chat', 'src map', 'src chat', 'src chat']);
        same('Log: the column heads and the foot', [texts(app, '.ahd span'), texts(app, '.logfoot')], [['When', 'Who', 'What'], ['People are shown by their Discord username. A person with no profile left is shown by the short code that the hub keeps for them.']]);
        same('Log: every name, map name, reason and short code is typed text and marked, and the words of the page and "terminal" are not', [all(app, '.arow2 [data-untrusted]').every((n) => n.getAttribute('data-untrusted') === 'typed-text'), all(app, '.arow2 [data-untrusted]').map((n) => n.textContent).sort()],
        [true, ['Mossy Ridge', 'Old Bridge', 'Same as an existing map.', 'Test 1', 'basil', 'basil', 'basil', 'bbbbbbbbbbbbbbbb', 'cccccccccccccccc', 'clover.4782', 'clover.4782', 'earlier admin', 'hazel_b', 'marlow.old', 'sage', 'tess', 'tess']]);
        same('Log: the map names are in an em and the reason of a decline in a span of its own', [texts(app, '.arow2 em'), texts(app, '.arow2 .why')], [['Mossy Ridge', 'Test 1', 'Old Bridge'], ['Reason: Same as an existing map.']]);
        same('Log: a person that has no name left is shown by the short code, in a code element', all(app, '.arow2 code').map((n) => n.textContent), ['bbbbbbbbbbbbbbbb', 'cccccccccccccccc']);
        await press(app, 'Chat actions (6)');
        same('Chat actions: only the hub\'s rows, no tag, the lead says 30 days', [all(app, '.arow2').length, all(app, '.arow2 .src').length, texts(app, '.alead'), all(app, '.lhead .chip').map((c) => c.getAttribute('aria-selected'))], [6, 0, ['What moderators and admins did in the chat, the last 30 days.'], ['false', 'true', 'false']]);
        await press(app, 'Map decisions (3)');
        same('Map decisions: only the maps, with the decider as a username (the words earlier admin for the old accounts)', [all(app, '.arow2').length, all(app, '.arow2 .wh').map((n) => n.textContent), texts(app, '.alead')], [3, ['basil', 'tess', 'earlier admin'], ['What was decided about the maps that players sent.']]);
        same('and nothing was asked again for a filter', w.calls.length, 2);
        noMarkup('Log', app);
        noId('Log', app, w.calls);
        // every action word, and a word that this page does not know
        const actions = ['mute', 'unmute', 'ban', 'unban', 'kick', 'nopic', 'pic', 'del', 'something_new'];
        const odd = world({ modlog: actions.map((action, i) => ({ at: at('2026-10-18T19:00:00Z') - i, action, actor: 'terminal', actor_name: '', target: '66f6666666666666', target_name: 'hazel_b', detail: action === 'mute' ? '1' : action === 'del' ? 'purge' : '' })), audit: [] });
        const x = await open(odd);
        await section(x.app, 'Log');
        same('every action of the hub has its words, an unknown one is shown as the word it is, a purge is not a line number', texts(x.app, '.arow2 .wt'), ['ChatMuted hazel_b for 1 minute', 'ChatUnmuted hazel_b', 'ChatBanned hazel_b', 'ChatUnbanned hazel_b', 'ChatKicked hazel_b', 'ChatHid the picture of hazel_b', 'ChatShowed the picture of hazel_b', 'ChatDeleted the recent lines of hazel_b', 'Chatsomething_new hazel_b']);
        same('and with nothing in either list the page says so', (await (async () => { const e = await open(world({ modlog: [], audit: [] })); await section(e.app, 'Log'); return [texts(e.app, '.none'), all(e.app, '.alist2').length]; })()), [['Nothing was done in this time.'], 0]);
        // one of the two lists that cannot be had: the other is shown, and the page says which is missing
        for (const [label, key, said, left] of [['the hub\'s log', 'GET /hub/admin/api/modlog', 'The chat\u2019s log could not be loaded.', 3], ['the map decisions', 'GET /admin/api/audit', 'The map decisions could not be loaded.', 6]]) {
            const f = world({ replies: { [key]: { status: 500, json: { error: 'x' } } } });
            const o = await open(f);
            await section(o.app, 'Log');
            same(label + ' cannot be had: the line says so and the other rows are shown', [texts(o.app, '.msg'), all(o.app, '.arow2').length], [[said], left]);
        }
        const none = world({ replies: { 'GET /hub/admin/api/modlog': () => { throw new Error('network'); } } });
        const n = await open(none);
        await section(n.app, 'Log');
        same('no answer at all is said too, and the page does not call it empty', [texts(n.app, '.msg'), texts(n.app, '.none')], [['The chat\u2019s log could not be loaded.', 'The map decisions could not be loaded.'], []]);
        // names that carry markup
        const evil = '<img src=x onerror=alert(1)>';
        const bad = world({ modlog: [{ at: at('2026-10-18T19:00:00Z'), action: 'ban', actor: '22b2222222222222', actor_name: evil, target: '66f6666666666666', target_name: '"><script>alert(2)</script>', detail: '' }], audit: [{ at: at('2026-10-18T18:00:00Z'), who: evil, ref: 'x', action: 'map_declined', target: evil, detail: evil }] });
        const b = await open(bad);
        await section(b.app, 'Log');
        same('a name, a map name and a reason with markup are text', [texts(b.app, '.arow2 .wh'), texts(b.app, '.arow2 em'), texts(b.app, '.arow2 .why')], [[evil, evil], [evil], ['Reason: ' + evil]]);
        noMarkup('the Log with markup in it', b.app);
    }
    {   // a row with no time, in each list: it is shown, with no time, and the others are
        const w = world();
        w.modlog[0].at = 'x';
        w.audit[0].at = null;
        w.mutes[0].until = 'soon';
        w.bans[0].at = undefined;
        const { app } = await open(w);
        await section(app, 'Log');
        same('the Log with a row that has no time: every row is there', [all(app, '.arow2').length, texts(app, '.arow2 .tm').filter((x) => x === '').length], [9, 2]);
        await section(app, 'Chat');
        same('the Chat lists with rows that have no time are drawn', [all(app, '.prow').length, texts(app, '.p-banned .ps')[0], texts(app, '.p-muted .ps')[0]], [7, 'banned  · by basil', 'until  () · by sage']);
    }
    // ---- the Status: all well, as drawn
    {
        const w = world();
        const { app } = await open(w);
        w.calls.length = 0;
        await section(app, 'Status');
        same('Status: the game server\'s counts and the hub\'s status are asked for, reads only', w.calls.map((c) => c.method + ' ' + c.url).sort(), ['GET /busy', 'GET /hub/admin/api/status']);
        same('Status: the game server\'s counts are asked for without the cookie (they are public) and the hub\'s status with it', [w.calls.filter((c) => c.url === '/busy')[0].credentials, w.calls.filter((c) => c.url === '/hub/admin/api/status')[0].credentials], ['omit', 'same-origin']);
        same('Status: when it was checked, in Pacific time, and the Refresh button', [texts(app, '.upd'), button(app, 'Refresh').length], [['Checked today, 13:00 (Pacific time)'], 1]);
        same('Status: the panels', texts(app, '.panel h2'), ['Right now', 'Role checks', 'Pictures from Discord', 'Chat store', 'Versions']);
        same('Status: matches running and players', all(app, '.kv').slice(0, 2).map((r) => r.textContent), ['Matches running2', 'Players on the game server5 in matches and lobbies']);
        same('Status: the role checks work, and say how many accounts are known', [texts(app, '.state').slice(0, 1), all(app, '.tg')[0].className, texts(app, '.note')[0]], [['Working' + 'Roles known for 14 accounts'], 'tg active big', 'If this says Refused or Not answering, the page stays shut for everyone but the owner\u2019s account.']);
        same('Status: the last picture was saved three minutes ago', [texts(app, '.state')[1], all(app, '.tg')[1].className], ['okSaved · 3 minutes ago', 'tg active big']);
        same('Status: the store, from numbers: the database of its limit and the percent, the meter, the log file, the lines (the oldest from a day), the pictures, the switches', [all(app, '.kv').slice(2).map((r) => r.textContent), all(app, '.meter i')[0].style.width, all(app, '.meter i')[0].className],
            [['Database4.2 of 64 MB 6%', 'Log file0.3 MB', 'Lines kept187 the oldest from 9 Oct', 'Pictures kept41 1.1 of 24 MB', 'Switched off / blocked6 / 1', 'Sitev0.16.11 build 9d41c2e · built 2026-11-16 09:30 PT', 'Chat hubcode 9f3c1a2e running since 16 Oct'], '6%', '']);
        noMarkup('Status', app);
        noId('Status', app, w.calls);
        w.calls.length = 0;
        w.busy = { matches: 0, players: 0 };
        w.status.health.roles_known = 15;
        await press(app, 'Refresh');
        same('Refresh asks for both again and draws the new numbers', [w.calls.map((c) => c.url).sort(), all(app, '.kv')[0].textContent, texts(app, '.state')[0]], [['/busy', '/hub/admin/api/status'], 'Matches running0', 'WorkingRoles known for 15 accounts']);
    }
    // ---- the Status in trouble: Discord refuses, Discord does not answer, the roles are off, the picture fetch is down, the store is full
    {
        const trouble = (health, last, store) => world({ status: { store: Object.assign({}, world().status.store, store || {}), health, last_picture: last, version: { code: '9f3c1a2e', started: at('2026-10-16T16:30:00Z') } } });
        const w = trouble({ roles: 'refused', roles_known: 0, kind: 'token', status: 401, retry: 540 }, { kind: 'down', age: 120 });
        const { app } = await open(w);
        await section(app, 'Status');
        same('Refused: the tag, the kind and the status of what Discord said, what it means for the page and when the hub tries again', [texts(app, '.state')[0], all(app, '.tg')[0].className, texts(app, '.note')[0], all(app, '.note')[0].className], ['RefusedDiscord said no (token, status 401)', 'tg bad big', 'Nobody holds a role while Discord refuses. This page and the moderators\u2019 tools stay shut, except for the owner\u2019s account. The hub tries again in about 9 minutes.', 'note bad']);
        same('a picture fetch that is down is said in words, with its age', [texts(app, '.state')[1], all(app, '.tg')[1].className], ['downDiscord did not answer · 2 minutes ago', 'tg warn big']);
        for (const [label, health, want, cls] of [
            ['about a minute left', { roles: 'refused', roles_known: 0, kind: 'guild', status: 404, retry: 20 }, 'RefusedDiscord said no (guild, status 404)', 'tg bad big'],
            ['Discord does not answer', { roles: 'down', roles_known: 3, kind: null, status: null, retry: null }, 'Not answeringDiscord did not answer the last check', 'tg warn big'],
            ['the roles are off', { roles: 'off', roles_known: 0, kind: null, status: null, retry: null }, 'OffNo role is asked of Discord', 'tg warn big']]) {
            const o = await open(trouble(health, null));
            await section(o.app, 'Status');
            same(label + ': the tag and the line', [texts(o.app, '.state')[0], all(o.app, '.tg')[0].className], [want, cls]);
            if (health.retry === 20) check(label + ': a few seconds are one minute, in the singular', /tries again in about 1 minute\.$/.test(texts(o.app, '.note')[0]));
        }
        const none = await open(trouble({ roles: 'ok', roles_known: 1, kind: null, status: null, retry: null }, null));
        await section(none.app, 'Status');
        same('no picture fetched since the hub started: said so', texts(none.app, '.state')[1], 'none yetNo picture was fetched since the hub started');
        for (const [kind, words, cls] of [['none', 'Discord has no picture to keep', 'active'], ['type', 'Not a picture that the hub accepts', 'warn'], ['big', 'The picture was too big', 'warn'], ['shape', 'The picture had a shape that the hub refuses', 'warn'], ['busy', 'Too many at once: some were dropped', 'warn'], ['bug', 'An error in the hub (its log says which kind)', 'bad'], ['brand_new', 'Something that this page does not know', 'warn']]) {
            const word = kind === 'brand_new' ? 'unknown' : kind;
            const o = await open(trouble({ roles: 'ok', roles_known: 1, kind: null, status: null, retry: null }, { kind, age: 7200 }));
            await section(o.app, 'Status');
            same('the kind of fetch ' + kind + ': its words, its look and its age', [texts(o.app, '.state')[1], all(o.app, '.tg')[1].className], [word + words + ' · 2 hours ago', 'tg ' + cls + ' big']);
        }
        const near = await open(trouble({ roles: 'ok', roles_known: 1, kind: null, status: null, retry: null }, null, { bytes: 50000000 }));
        await section(near.app, 'Status');
        same('the meter turns amber from 70% and red from 90%', [all(near.app, '.meter i')[0].className, all(near.app, '.meter i')[0].style.width], ['w', '74%']);
        const full = await open(trouble({ roles: 'ok', roles_known: 1, kind: null, status: null, retry: null }, null, { bytes: 67108864 }));
        await section(full.app, 'Status');
        same('and a full store is 100% and red', [all(full.app, '.meter i')[0].className, all(full.app, '.meter i')[0].style.width, texts(full.app, '.kv')[2].slice(0, 32)], ['b', '100%', 'Database64.0 of 64 MB 100%']);
    }
    // ---- the Status when a part does not answer; the site's version only when the page was stamped
    {
        const w = world({ replies: { 'GET /busy': { status: 503, json: {} } } });
        const { app } = await open(w);
        await section(app, 'Status');
        same('the game server does not answer: its panel says so, the rest is there', [texts(app, '.panel .msg'), texts(app, '.panel h2').length], [['The game server did not answer.'], 5]);
        const x = world({ replies: { 'GET /hub/admin/api/status': { status: 503, json: { error: 'busy' } } } });
        const o = await open(x);
        await section(o.app, 'Status');
        same('the hub does not answer: its panel says so, the counts are there', [texts(o.app, '.panel .msg'), texts(o.app, '.panel h2'), all(o.app, '.kv')[0].textContent], [['The chat hub did not answer.'], ['Right now', 'The chat hub'], 'Matches running2']);
        const n = world({ replies: { 'GET /busy': () => { throw new Error('network'); }, 'GET /hub/admin/api/status': () => { throw new Error('network'); } } });
        const p = await open(n);
        await section(p.app, 'Status');
        same('neither answers: both lines, and Refresh works again', [texts(p.app, '.panel .msg'), p.app.querySelector('.stop button').disabled], [['The game server did not answer.', 'The chat hub did not answer.'], false]);
        const odd = world({ replies: { 'GET /busy': { status: 200, json: { matches: 'x' } }, 'GET /hub/admin/api/status': { status: 200, json: { store: {}, health: null } } } });
        const q = await open(odd);
        await section(q.app, 'Status');
        same('answers of another shape are not trusted', texts(q.app, '.panel .msg'), ['The game server did not answer.', 'The chat hub did not answer.']);
        const bare = await open(world(), {});
        await section(bare.app, 'Status');
        same('a page that nobody stamped has no row for the site and no placeholder', [texts(bare.app, '.panel h2').includes('Versions'), texts(bare.app, '.kv').filter((t) => /^Site/.test(t)).length, bare.app.textContent.indexOf('@@')], [true, 0, -1]);
        const half = await open(world(), { 'data-version': 'v0.16.11', 'data-build': '@@BUILD_ID@@' });
        await section(half.app, 'Status');
        same('a stamp with a placeholder left in it is left out', texts(half.app, '.kv').filter((t) => /^Site/.test(t)), ['Sitev0.16.11']);
    }
    // ---- the other sections do not read their lists until they are opened, and nothing of the page ever asks for an id
    {
        const w = world();
        const { app } = await open(w);
        same('opening the page asks for the question who is here and the maps, and for nothing of the other sections', w.calls.map((c) => c.url), ['/admin/api/me', '/admin/api/maps?state=waiting']);
        await section(app, 'Chat');
        await section(app, 'Log');
        await section(app, 'Status');
        await section(app, 'Maps');
        const urls = w.calls.map((c) => c.url);
        same('every address that the page asked for is one of the known ones, and none holds a query but the maps\'', urls.filter((u) => !/^(\/admin\/api\/(me|audit|maps\?state=\w+)|\/hub\/admin\/api\/(me|bans|mutes|hidden|modlog|status)|\/busy)$/.test(u)), []);
        noId('the whole visit', app, w.calls);
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})();

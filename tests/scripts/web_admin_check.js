// Runs the owner's page's OWN code (web/admin_page.js) with a small fake of the DOM and a fake server: what the page shows to nobody (the sign-in box and nothing else), to an account that still has its starting
// password (the choice of a new one, nothing else), to an Admin and to a Moderator (the first design's pictures: the account menu, the maps, the users, the log), what each button sends (with the CSRF token), the
// questions before a take-down and a removal, the one-time password box, the empty states, a session that ends, and that nothing a person typed becomes markup and no password is ever kept or shown twice.
// tests/scripts/test_web_admin.py runs this with node (the quick tier). usage: node web_admin_check.js web/admin_page.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');
const { makeDocument } = require('./web_mapmaker_fake_dom.js');
const A = require(path.resolve(process.argv[2]));

let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }
function same(label, got, want) { const a = JSON.stringify(got), b = JSON.stringify(want); check(label, a === b, 'got ' + a + ', wanted ' + b); }
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function settle() { for (let i = 0; i < 12; i++) await tick(); }

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

// ---- the page, a fake server ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function map(id, extra) {
    return Object.assign({ id, state: 'waiting', kind: 'classic', name: 'Treasure', maker: 'Mia', note: '', width: 34, height: 34, sent_at: at('2026-10-18T16:41:00Z'), name_taken: false, has_picture: true, findings: [] }, extra || {});
}
function user(id, name, role, extra) {
    return Object.assign({ id, name, role, disabled: false, has_not_chosen_password: false, created_at: at('2026-10-01T16:00:00Z'), last_login_at: 0, created_by: 'console', you: false }, extra || {});
}
function world(overrides) {
    const w = Object.assign({
        me: { status: 200, json: { signed_in: true, name: 'Dave', role: 'admin', must_change: false, csrf: 'csrf-token-1' } },
        users: [user(1, 'Dave', 'admin', { you: true, last_login_at: at('2026-10-18T16:12:00Z') }), user(2, 'mia', 'moderator', { last_login_at: at('2026-10-17T16:40:00Z') }), user(3, 'sam', 'moderator', { has_not_chosen_password: true }), user(4, 'lee', 'moderator', { disabled: true, last_login_at: at('2026-10-03T19:20:00Z') })],
        entries: [{ at: at('2026-10-18T16:12:00Z'), who: 'Dave', action: 'sign_in', target: '', detail: '' }, { at: at('2026-10-18T15:52:00Z'), who: 'mia', action: 'map_approved', target: 'Treasure 2', detail: '' }],
        counts: { waiting: 2, approved: 1, declined: 1, taken_down: 1 },
        lists: {
            waiting: [map('a'.repeat(32), { name: 'Treasure', note: 'my first map', name_taken: true }), map('b'.repeat(32), { kind: 'extended', name: 'Big Lake', maker: 'Sam', width: 300, height: 400, sent_at: at('2026-10-17T16:00:00Z') })],
            approved: [map('c'.repeat(32), { state: 'approved', approved_name: 'Treasure 2', name: 'Treasure', decided_at: at('2026-10-18T21:20:00Z') - 3600 * 5 })],
            declined: [map('d'.repeat(32), { state: 'declined', name: 'Dry Canyon', maker: 'Lee', decided_at: at('2026-10-16T16:00:00Z'), message: 'Too plain for the list' })],
            taken_down: [map('e'.repeat(32), { state: 'taken_down', approved_name: 'Old Hill', name: 'Old Hill', decided_at: at('2026-10-18T16:02:00Z') })]
        },
        calls: [],
        replies: {}
    }, overrides || {});
    w.fetch = async (url, init) => {
        init = init || {};
        w.calls.push({ method: init.method || 'GET', url, headers: init.headers || {}, body: init.body === undefined ? undefined : JSON.parse(init.body), credentials: init.credentials });
        const reply = (status, json) => ({ status, json: async () => json });
        const key = (init.method || 'GET') + ' ' + url;
        for (const pattern of Object.keys(w.replies)) {
            if (key.startsWith(pattern)) { const r = w.replies[pattern]; return typeof r === 'function' ? r() : reply(r.status, r.json); }
        }
        if (url === '/admin/api/me') return reply(w.me.status, w.me.json);
        if (url === '/admin/api/users' && (init.method || 'GET') === 'GET') return reply(200, { users: w.users, untrusted: true });
        if (url === '/admin/api/audit') return reply(200, { entries: w.entries, untrusted: true });
        if (url === '/admin/api/logout') return reply(200, { signed_in: false });
        const m = /^\/admin\/api\/maps\?state=(\w+)$/.exec(url);
        if (m) return reply(200, { state: m[1], counts: w.counts, maps: w.lists[m[1]] || [] });
        const n = /^\/admin\/api\/next-name\?name=(.*)$/.exec(url);
        if (n) return reply(200, { name: decodeURIComponent(n[1]).replace(/ \d+$/, '') + ' 3' });
        if ((init.method || 'GET') === 'POST') return reply(200, { map: {}, counts_changed: true });
        return reply(404, { error: 'not_found' });
    };
    return w;
}
async function open(w) {
    const doc = makeDocument();
    const app = doc.createElement('div');
    app.id = 'app';
    doc.body.appendChild(app);
    doc.getElementById = (id) => (id === 'app' ? app : null);
    const timers = [];
    const page = A.create({ doc, fetch: w.fetch, now: () => NOW, setTimeout: (f) => { timers.push(f); return timers.length; }, clearTimeout: () => {} });
    await page.start();
    await settle();
    return { doc, app, page, timers };
}
const all = (app, sel) => app.querySelectorAll(sel);
const texts = (app, sel) => all(app, sel).map((n) => n.textContent);
const button = (app, label) => all(app, 'button').filter((b) => b.textContent === label);
async function press(app, label, which) {
    const list = button(app, label);
    if (!list.length) return false;
    list[which || 0].click();
    await settle();
    return true;
}
async function type(app, selector, value) {
    const input = app.querySelector(selector);
    input.value = value;
    input.fire('input', {});
    await settle();
}
async function submit(app, which) {
    all(app, 'form')[which || 0].fire('submit', { preventDefault() {} });
    await settle();
}
async function toggleMenu(app) {
    all(app, '.acct')[0].click();
    await settle();
}
const ACCOUNT = 'DDaveAdmin\u25BE';

function noMarkup(label, app) {
    const tags = new Set(all(app, '*'.length ? 'div' : 'div').length >= 0 ? app.all().map((n) => n.tagName) : []);
    const allowed = new Set(['div', 'span', 'img', 'button', 'form', 'label', 'input', 'textarea', 'h1', 'h2', '#text', 'b', 'em', 'hr', 'code', 'small', 'i']);
    for (const t of tags) check(label + ': only the page\'s own kinds of node (' + t + ')', allowed.has(t));
    check(label + ': innerHTML was never set', app.all().every((n) => n.innerHTML === undefined));
}

(async () => {
    // ---- nobody: the sign-in box and nothing else
    {
        const w = world({ me: { status: 200, json: { signed_in: false } } });
        const { app, doc } = await open(w);
        same('nobody: only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
        same('nobody: the heading, and nothing else of words but the labels and the button', [texts(app, 'h1'), texts(app, 'label'), texts(app, 'button').filter((t) => t !== '◉')], [['Sign in'], ['Username', 'Password'], ['Sign in']]);
        check('nobody: no link, no tab, no list, no banner, no menu, nothing about maps', all(app, 'a').length === 0 && all(app, '.chip').length === 0 && all(app, '.arow').length === 0 && all(app, '.banner').length === 0 && all(app, '.acct').length === 0 && app.textContent.toLowerCase().indexOf('map') < 0);
        same('nobody: the call carries the cookie by itself', w.calls[0].credentials, 'same-origin');
        const user = app.querySelector('#login-user'), pass = app.querySelector('#login-pass');
        same('the boxes tell a password manager what they are', [user.type, user.getAttribute('autocomplete'), pass.type, pass.getAttribute('autocomplete'), pass.getAttribute('autocapitalize'), pass.getAttribute('spellcheck')], ['text', 'username', 'password', 'current-password', 'off', 'false']);
        same('the labels belong to the boxes', all(app, 'label').map((l) => l.getAttribute('for')), ['login-user', 'login-pass']);
        check('the focus is in the username box', doc.activeElement === user);
        const eye = all(app, '.eye')[0];
        same('the eye shows and hides the password, and says so', [eye.getAttribute('aria-label'), eye.getAttribute('aria-pressed')], ['Show password', 'false']);
        eye.click();
        same('pressed: the text is shown', [pass.type, eye.getAttribute('aria-label'), eye.getAttribute('aria-pressed')], ['text', 'Hide password', 'true']);
        eye.click();
        same('pressed again: hidden', pass.type, 'password');
        same('the form posts nowhere by itself', [all(app, 'form')[0].getAttribute('action'), all(app, 'form')[0].getAttribute('method')], [null, null]);
    }
    // ---- a sign-in: the call, what happens after, and what is shown for each answer
    {
        const w = world({ me: { status: 200, json: { signed_in: false } } });
        const { app } = await open(w);
        w.calls.length = 0;
        await type(app, '#login-user', 'Dave');
        await type(app, '#login-pass', 'a secret that stays out of the page');
        w.me = { status: 200, json: { signed_in: true, name: 'Dave', role: 'admin', must_change: false, csrf: 'csrf-after-login' } };
        w.replies['POST /admin/api/login'] = { status: 200, json: { signed_in: true } };
        await submit(app);
        same('sign in: one POST of the two values to the login route, and the question who is here again', [w.calls[0].method, w.calls[0].url, w.calls[0].body, w.calls[0].credentials, w.calls[1].url], ['POST', '/admin/api/login', { username: 'Dave', password: 'a secret that stays out of the page' }, 'same-origin', '/admin/api/me']);
        check('sign in: no password is in any address, header or text of the page', w.calls.every((c) => c.url.indexOf('secret') < 0 && JSON.stringify(c.headers).indexOf('secret') < 0) && app.textContent.indexOf('a secret') < 0);
        same('sign in: the owner\'s page is shown, with the new token', [texts(app, '.banner'), all(app, '.acct').length], [['Maps players sent'], 1]);
    }
    for (const [status, json, words, locked] of [[401, { error: 'bad_login', message: 'Wrong username or password.' }, 'Wrong username or password', false], [429, { error: 'locked', message: 'Too many tries. Wait a few minutes.' }, 'Too many tries. Wait a few minutes.', true],
        [503, { error: 'busy' }, 'The server is busy. Please try again in a minute.', false], [500, { error: 'x' }, 'Signing in did not work. Try again in a moment.', false]]) {
        const w = world({ me: { status: 200, json: { signed_in: false } }, replies: { 'POST /admin/api/login': { status, json } } });
        const { app, doc } = await open(w);
        await type(app, '#login-user', 'Dave');
        await type(app, '#login-pass', 'wrong guess here');
        await submit(app);
        same('answer ' + status + ': the words, the name kept, the password gone, the focus in the password box', [texts(app, '.msg'), app.querySelector('#login-user').value, app.querySelector('#login-pass').value, doc.activeElement === app.querySelector('#login-pass')], [[words], 'Dave', '', true]);
        same('answer ' + status + ': the message is an alert, and the Sign in button is ' + (locked ? 'dim' : 'live'), [all(app, '.msg')[0].getAttribute('role'), all(app, 'form button').filter((b) => b.textContent === 'Sign in')[0].disabled], ['alert', locked]);
        if (status === 401) check('a wrong answer marks both boxes the same way (no hint which one was wrong)', app.querySelector('#login-user').className.indexOf('err') >= 0 && app.querySelector('#login-pass').className.indexOf('err') >= 0);
        if (locked) {
            const before = w.calls.length;
            await submit(app);
            same('a dim button sends nothing', w.calls.length, before);
            await type(app, '#login-user', 'Mia');
            check('another name may try: the button is live again', !all(app, 'form button').filter((b) => b.textContent === 'Sign in')[0].disabled);
        }
    }
    {
        const w = world({ me: { status: 200, json: { signed_in: false } }, replies: { 'POST /admin/api/login': () => { throw new Error('network'); } } });
        const { app } = await open(w);
        await type(app, '#login-user', 'Dave');
        await type(app, '#login-pass', 'whatever it is');
        await submit(app);
        same('no answer: said in words, nothing hidden', texts(app, '.msg'), ['The server did not answer. Try again in a moment.']);
    }
    // ---- the owner's page is off on this server (404): the page says only that it is for the owner
    {
        const w = world({ replies: { 'GET /admin/api/me': { status: 404, json: { error: 'not_found' } } } });
        const { app } = await open(w);
        same('off: only "This page is for the owner only", nothing to press', [texts(app, 'h1'), all(app, 'a').length, all(app, 'form').length, all(app, 'button').length], [['This page is for the owner only'], 0, 0, 0]);
    }
    // ---- an account with a starting password: the choice of its own, and nothing else (the server holds that too)
    {
        const w = world({ me: { status: 200, json: { signed_in: true, name: 'sam', role: 'moderator', must_change: true, csrf: 'csrf-tok' } } });
        const { app, doc } = await open(w);
        same('choose: the heading, the line and the only way out', [texts(app, 'h1'), texts(app, '.l2'), texts(app, 'button').filter((t) => t !== '◉'), all(app, 'a').length], [['Choose your own password'], ['You signed in with a starting password. Choose a password that only you know. You cannot go on until you do.'], ['Save and continue', 'Sign out'], 0]);
        same('choose: no map, no menu, no section, and no call but the one question', [w.calls.map((c) => c.url), all(app, '.acct').length, all(app, '.chip').length, all(app, '.banner').length], [['/admin/api/me'], 0, 0, 0]);
        const a = app.querySelector('#choose-new'), b = app.querySelector('#choose-again');
        same('choose: the two boxes are for a new password', [a.getAttribute('autocomplete'), b.getAttribute('autocomplete'), a.type, b.type, doc.activeElement === a], ['new-password', 'new-password', 'password', 'password', true]);
        const save = button(app, 'Save and continue')[0];
        check('choose: Save is dim and refuses to send while the boxes are empty', save.disabled && save.className.indexOf('dis') >= 0);
        same('choose: the rule in plain words', texts(app, '.rules'), ['At least 12 characters. That is the only rule. Four or five random words, like “blue ladder tomato river”, make a good password.']);
        await type(app, '#choose-new', 'seven77');
        same('choose: too short says so, with the count', [texts(app, '.str')[0], all(app, '.bars i.on').length, all(app, '.bars i.on.bad').length], ['Strength: Too short7 of 12 characters', 1, 1]);
        await type(app, '#choose-new', 'elevenchars');
        await type(app, '#choose-again', 'elevenchars');
        same('choose: 11 characters are not enough, even when both are the same', [texts(app, '.str')[0], save.disabled], ['Strength: Too short11 of 12 characters', true]);
        await type(app, '#choose-new', '\u00e9'.repeat(11));
        await type(app, '#choose-again', '\u00e9'.repeat(11));
        same('choose: characters are counted, not bytes (11 accented letters are 22 bytes)', [texts(app, '.str')[0], save.disabled], ['Strength: Too short11 of 12 characters', true]);
        await type(app, '#choose-again', '');
        await type(app, '#choose-new', 'twelve chars');
        same('choose: 12 characters is Okay', [texts(app, '.str')[0], all(app, '.bars i.on.warn').length], ['Strength: Okay', 2]);
        await type(app, '#choose-new', 'sixteen chars ok!');
        same('choose: 17 is Good', texts(app, '.str')[0], 'Strength: Good');
        await type(app, '#choose-new', 'twenty characters long');
        same('choose: 22 is Strong, four bars', [texts(app, '.str')[0], all(app, '.bars i.on.ok').length], ['Strength: Strong', 4]);
        await type(app, '#choose-again', 'twenty characters lon');
        same('choose: the two are not the same: said, Save stays dim', [texts(app, '.fh'), save.disabled], [['The two passwords are not the same.'], true]);
        w.calls.length = 0;
        await submit(app);
        same('choose: a dim Save sends nothing', w.calls.length, 0);
        await type(app, '#choose-again', 'twenty characters long');
        check('choose: the same twice and long enough: Save is live and the warning is gone', !save.disabled && all(app, '.fh').every((n) => n.hidden));
        w.me = { status: 200, json: { signed_in: true, name: 'sam', role: 'moderator', must_change: false, csrf: 'csrf-new' } };
        await submit(app);
        same('choose: Save sends the new password once and nothing of the starting one (the server already knows who is here)', [w.calls[0].method, w.calls[0].url, w.calls[0].body, w.calls[0].headers['X-Ants-Csrf']], ['POST', '/admin/api/password', { current: '', new: 'twenty characters long' }, 'csrf-tok']);
        same('choose: then the page asks who is here and shows the maps', [w.calls[1].url, texts(app, '.banner')], ['/admin/api/me', ['Maps players sent']]);
        check('choose: the password is nowhere in the page', app.textContent.indexOf('twenty characters long') < 0);
        // the refusals of the server
        for (const [status, json, words] of [[400, { error: 'bad_new_password', reason: 'same_as_old' }, 'Choose a password that is not the old one.'], [400, { error: 'bad_new_password', reason: 'same_as_name' }, 'The new password cannot be your username.'],
            [400, { error: 'bad_new_password', reason: 'too_short' }, 'The new password needs at least 12 characters.'], [429, { error: 'locked' }, 'Too many tries. Wait a few minutes.'], [500, { error: 'storage_error' }, 'The password could not be changed. Nothing was changed. Try again in a moment.']]) {
            const x = world({ me: { status: 200, json: { signed_in: true, name: 'sam', role: 'moderator', must_change: true, csrf: 'c' } }, replies: { 'POST /admin/api/password': { status, json } } });
            const o = await open(x);
            await type(o.app, '#choose-new', 'twenty characters long');
            await type(o.app, '#choose-again', 'twenty characters long');
            await submit(o.app);
            same('choose: the server says ' + (json.reason || json.error) + ': ' + words, texts(o.app, '.msg'), [words]);
        }
        const gone = world({ me: { status: 200, json: { signed_in: true, name: 'sam', role: 'moderator', must_change: true, csrf: 'c' } }, replies: { 'POST /admin/api/password': { status: 404, json: { error: 'not_found' } } } });
        const og = await open(gone);
        await type(og.app, '#choose-new', 'twenty characters long');
        await type(og.app, '#choose-again', 'twenty characters long');
        gone.replies['GET /admin/api/me'] = { status: 200, json: { signed_in: false } };
        await submit(og.app);
        same('choose: a session that ended: the sign-in box', texts(og.app, 'h1'), ['Sign in']);
        // Sign out
        const x2 = world({ me: { status: 200, json: { signed_in: true, name: 'sam', role: 'moderator', must_change: true, csrf: 'c' } } });
        const o2 = await open(x2);
        x2.calls.length = 0;
        await press(o2.app, 'Sign out');
        same('choose: Sign out is a POST with the token, then the sign-in box', [x2.calls[0].method, x2.calls[0].url, x2.calls[0].headers['X-Ants-Csrf'], texts(o2.app, 'h1')], ['POST', '/admin/api/logout', 'c', ['Sign in']]);
    }
    // ---- the owner: C3 the Waiting tab, exactly as drawn
    {
        const w = world();
        const { app, page } = await open(w);
        same('owner: the section registry holds the maps and the users', page.sections(), ['maps', 'users']);
        same('owner: the banner of the section, once', texts(app, '.banner'), ['Maps players sent']);
        same('owner: four tabs with their counts, the first chosen', [texts(app, '.chip'), all(app, '.chip').map((c) => c.className.indexOf(' on') > 0)], [['Waiting (2)', 'Approved (1)', 'Declined (1)', 'Taken down (1)'], [true, false, false, false]]);
        same('owner: the account button is the way to Sign out; no line of its own', [texts(app, '.acct .wn b'), all(app, '.signed').length], [['Dave'], 0]);
        check('owner: no link anywhere (no navigation, nothing that leads out)', all(app, 'a').length === 0);
        same('owner: the lead of the tab and its foot', [texts(app, '.alead'), texts(app, '.qfoot')], [['Maps players sent that wait for you. Approve puts a map in the lobby list.'], ['Declining asks for a short reason that the maker can read. Nothing is deleted.']]);
        const rows = all(app, '.arow');
        same('owner: two rows, names and tags', [rows.length, texts(app, '.t1')], [2, ['TreasureClassic mapPlayer text', 'Big LakeExtended mapNew version onlyPlayer text']]);
        same('owner: the meta lines (Pacific time)', texts(app, '.am'), ['By Mia Player text · 34 x 34 · Sent today, 09:41', 'By Sam Player text · 300 x 400 · Sent yesterday']);
        same('owner: three buttons for each', rows.map((r) => r.querySelectorAll('button').map((b) => b.textContent)), [['Approve', 'Rename and approve', 'Decline'], ['Approve', 'Rename and approve', 'Decline']]);
        same('owner: the picture is the owner-only route of the map', all(app, '.arow img')[0].src, '/admin/api/maps/' + 'a'.repeat(32) + '/picture');
        same('owner: the maker\'s note is a quote', texts(app, '.quote'), ['my first map']);
        same('owner: the tabs are a tablist with the chosen one marked', [all(app, '.chips')[0].getAttribute('role'), all(app, '.chip').map((c) => c.getAttribute('aria-selected'))], ['tablist', ['true', 'false', 'false', 'false']]);
        noMarkup('owner', app);
        same('owner: only the list was asked for', w.calls.map((c) => c.url), ['/admin/api/me', '/admin/api/maps?state=waiting']);
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
        same('Save name and approve: the owner\'s name goes in the body', [w.calls[0].url, w.calls[0].body], ['/admin/api/maps/' + 'a'.repeat(32) + '/approve', { name: 'Gold Rush' }]);
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
        same('take down: the map is shown in the question, and Cancel is the chosen button', [texts(app, '.srcrow .nm'), texts(app, '.dlg-foot button'), app.ownerDocument === undefined], [['Treasure 2 Classic map'], ['Cancel', 'Take down'], true]);
        await press(app, 'Cancel');
        same('Cancel: the question goes, nothing was sent', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Take down');
        doc.key('Escape');
        await settle();
        same('Escape closes it too', [all(app, '.dlg').length, w.calls.length], [0, 0]);
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
        noMarkup('hostile texts', app);
        await press(app, 'Rename and approve');
        same('the name goes into the box as a value', all(app, 'input')[0].value, evil);
        noMarkup('hostile texts in the box', app);
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
        // a hostile description is cleaned in the question: one line, no controls or invisible marks, 100 characters
        const nasty = 'Please approve\n\u001b[31mnow\u202e<b\u200b' + 'x'.repeat(200);
        const w = world({ lists: { waiting: [map('f'.repeat(32), { name: 'Hostile', maker: 'Zed', description: nasty })], approved: [], declined: [], taken_down: [] } });
        const { app, doc } = await open(w);
        w.calls.length = 0;
        await press(app, 'Approve', 0);
        const shownDesc = texts(app, '.dsc [data-untrusted]');
        same('question: the name, the maker, the picture and a cleaned description are shown', [texts(app, '.dlg h2'), texts(app, '.srcrow .nm'), texts(app, '.dlg .am, .dlg .sm').join('|').indexOf('Zed') >= 0, all(app, '.srcrow img').length, shownDesc.length, shownDesc[0].length],
            [['Approve this map?'], ['Hostile Classic map'], true, 1, 1, 100]);
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
        // Cancel in the rename box's question keeps what the owner typed
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
    // ---- a session that ends, and an error
    {
        const w = world();
        const { app } = await open(w);
        w.replies['POST /admin/api/maps/'] = { status: 404, json: { error: 'not_found' } };
        w.replies['GET /admin/api/me'] = { status: 200, json: { signed_in: false } };
        await press(app, 'Approve', 1);
        await press(app, 'Approve', 2);
        same('the cookie ended: the page asks who is here and shows the sign-in box', texts(app, 'h1'), ['Sign in']);
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
    // ---- the account menu: name and role, Change password, Users (Admin only), Sign out
    {
        const w = world();
        const { app, doc } = await open(w);
        same('menu: a button with the name and the role, closed', [texts(app, '.acct .wn b'), texts(app, '.acct .wn small'), texts(app, '.acct .av'), all(app, '.acct')[0].getAttribute('aria-expanded'), all(app, '.acct')[0].getAttribute('aria-haspopup'), all(app, '.amenu').length], [['Dave'], ['Admin'], ['D'], 'false', 'menu', 0]);
        check('menu: the old line "Signed in with Discord" is gone, and no other word of it', all(app, '.signed').length === 0 && app.textContent.indexOf('Discord') < 0);
        await press(app, 'Dave▾Admin▾'.length ? 'DDaveAdmin▾' : '');
        same('menu: open, it shows the name, the role and the three items for an Admin', [all(app, '.acct')[0].getAttribute('aria-expanded'), texts(app, '.amenu .mtop b'), texts(app, '.amenu .mtop small'), texts(app, '.amenu button'), all(app, '.amenu')[0].getAttribute('role'), all(app, '.amenu button').map((b) => b.getAttribute('role'))],
            ['true', ['Dave'], ['Admin'], ['Change password', 'UsersAdmin only', 'Sign out'], 'menu', ['menuitem', 'menuitem', 'menuitem']]);
        doc.key('Escape');
        same('menu: Escape closes it', [all(app, '.amenu').length, all(app, '.acct')[0].getAttribute('aria-expanded')], [0, 'false']);
        await press(app, 'DDaveAdmin▾');
        await press(app, 'DDaveAdmin▾');
        same('menu: the button again closes it', all(app, '.amenu').length, 0);
        // a Moderator has no Users item
        const m = world({ me: { status: 200, json: { signed_in: true, name: 'mia', role: 'moderator', must_change: false, csrf: 'c' } } });
        const o = await open(m);
        await press(o.app, 'mmiaModerator▾');
        same('menu: a Moderator sees Change password and Sign out only, and the role says Moderator', [texts(o.app, '.amenu button'), texts(o.app, '.amenu .mtop small')], [['Change password', 'Sign out'], ['Moderator']]);
        check('menu: a Moderator\'s page asks nothing of the users', m.calls.every((c) => c.url.indexOf('users') < 0 && c.url.indexOf('audit') < 0));
        same('menu: the sections that exist for the page', o.page.sections(), ['maps', 'users']);
        // a very long name is shortened by style only: the text is the whole name
        const long = world({ me: { status: 200, json: { signed_in: true, name: 'map.reviewer.from.the.north.team', role: 'admin', must_change: false, csrf: 'c' } } });
        const l = await open(long);
        same('menu: a long name is in the bar and in the menu in full', texts(l.app, '.acct .wn b'), ['map.reviewer.from.the.north.team']);
        // Sign out
        w.calls.length = 0;
        await press(app, 'DDaveAdmin▾');
        await press(app, 'Sign out');
        same('menu: Sign out is a POST with the token, then the sign-in box and no menu', [w.calls[0].method, w.calls[0].url, w.calls[0].headers['X-Ants-Csrf'], texts(app, 'h1'), all(app, '.acct').length], ['POST', '/admin/api/logout', 'csrf-token-1', ['Sign in'], 0]);
    }
    // ---- Change password: the box of the picture, each answer of the server
    {
        const w = world();
        const { app, doc } = await open(w);
        await press(app, 'DDaveAdmin▾');
        await press(app, 'Change password');
        same('change: the box, its fields and their kind', [texts(app, '.dlg h2'), texts(app, '.dlg label'), all(app, '.dlg input').map((i) => i.getAttribute('autocomplete')), all(app, '.dlg input').map((i) => i.type), texts(app, '.dlg .rules')],
            [['Change password'], ['Current password', 'New password', 'New password again'], ['current-password', 'new-password', 'new-password'], ['password', 'password', 'password'], ['At least 12 characters. That is the only rule.']]);
        check('change: the focus is in the current password and the page behind is inert', doc.activeElement === app.querySelector('#change-current') && all(app, '.mhead')[0].getAttribute('inert') !== null);
        const save = button(app, 'Save')[0];
        check('change: Save is dim until all three are right', save.disabled);
        await type(app, '#change-current', 'the old one here');
        await type(app, '#change-new', 'my new long password');
        await type(app, '#change-again', 'my new long password');
        check('change: Save is live', !save.disabled);
        w.replies['POST /admin/api/password'] = { status: 403, json: { error: 'bad_password' } };
        w.calls.length = 0;
        await press(app, 'Save');
        same('change: the call carries the old and the new, with the token', [w.calls[0].url, w.calls[0].body, w.calls[0].headers['X-Ants-Csrf']], ['/admin/api/password', { current: 'the old one here', new: 'my new long password' }, 'csrf-token-1']);
        same('change: a wrong current password: the words of the picture, the box marked, nothing cleared', [texts(app, '.dlg .msg'), app.querySelector('#change-current').className.indexOf('err') >= 0, app.querySelector('#change-new').value], [['The current password is not right. Nothing was changed.'], true, 'my new long password']);
        w.replies['POST /admin/api/password'] = { status: 429, json: { error: 'locked' } };
        await press(app, 'Save');
        same('change: locked says the sign-in words', texts(app, '.dlg .msg'), ['Too many tries. Wait a few minutes.']);
        w.replies['POST /admin/api/password'] = { status: 200, json: { ok: true, must_change: false, csrf: 'csrf-renewed' } };
        w.calls.length = 0;
        await press(app, 'Save');
        same('change: success: the words of the picture and a Close button, the old values are gone from the page', [texts(app, '.dlg .msg'), texts(app, '.dlg-foot button'), app.textContent.indexOf('my new long password'), app.textContent.indexOf('the old one here')], [['Your password was changed. Use the new one the next time you sign in.'], ['Close'], -1, -1]);
        await press(app, 'Close');
        w.calls.length = 0;
        await press(app, 'Approve', 0);
        await press(app, 'Approve', 2);
        same('change: the page goes on with the renewed token', w.calls[0].headers['X-Ants-Csrf'], 'csrf-renewed');
        // Cancel and Escape send nothing and leave no password in the page
        await press(app, 'DDaveAdmin▾');
        await press(app, 'Change password');
        await type(app, '#change-current', 'typed then abandoned');
        w.calls.length = 0;
        await press(app, 'Cancel');
        same('change: Cancel closes it and sends nothing; the page behind is live', [all(app, '.dlg').length, w.calls.length, all(app, '.mhead')[0].getAttribute('inert')], [0, 0, null]);
        check('change: nothing typed is left in the page', app.textContent.indexOf('typed then abandoned') < 0);
        // a session that ends in the middle of it
        const g = world();
        const o = await open(g);
        await press(o.app, 'DDaveAdmin▾');
        await press(o.app, 'Change password');
        await type(o.app, '#change-current', 'x'.repeat(14));
        await type(o.app, '#change-new', 'my new long password');
        await type(o.app, '#change-again', 'my new long password');
        g.replies['POST /admin/api/password'] = { status: 404, json: { error: 'not_found' } };
        g.replies['GET /admin/api/me'] = { status: 200, json: { signed_in: false } };
        await press(o.app, 'Save');
        same('change: a session that ended: the sign-in box, no box open', [texts(o.app, 'h1'), all(o.app, '.dlg').length], [['Sign in'], 0]);
    }
    // ---- the Users page: the list as drawn, the roles, the states
    {
        const w = world();
        const { app } = await open(w);
        await press(app, 'DDaveAdmin▾');
        w.calls.length = 0;
        await press(app, 'UsersAdmin only');
        same('users: asks for the list', w.calls.map((c) => c.method + ' ' + c.url), ['GET /admin/api/users']);
        same('users: the banner, the tabs and the first line', [texts(app, '.banner'), texts(app, '.chip'), texts(app, '.alead')], [['Users'], ['Users (4)', 'Sign-ins and actions'], ['Everyone who can sign in to this page. Only an Admin sees this list.']]);
        same('users: the column heads', texts(app, '.uhd span'), ['Username', 'Role', 'Last sign-in', 'State', '']);
        const rows = all(app, '.urow');
        same('users: four rows with the names, "You" and the sentence for a person who has not chosen yet', [rows.length, rows.map((r) => r.querySelector('.un').textContent), texts(app, '.unsub')], [4, ['DaveYou', 'mia', 'sam', 'lee'], ['Has not chosen a password yet']]);
        same('users: roles and states as tags', [rows.map((r) => r.querySelectorAll('.tg').map((t) => t.textContent).filter((t) => t !== 'You').join('/'))], [['Admin/Active', 'Moderator/Active', 'Moderator/Active', 'Moderator/Disabled']]);
        same('users: the tags have the colors of their words', [all(app, '.tg').filter((t) => t.textContent === 'Admin')[0].className, all(app, '.tg').filter((t) => t.textContent === 'Moderator')[0].className], ['tg admin', 'tg moderator']);
        same('users: last sign-in in Pacific time, and Never', rows.map((r) => r.querySelector('.ul').textContent), ['Last sign-in: today, 09:12', 'Last sign-in: yesterday', 'Last sign-in: Never', 'Last sign-in: 3 Oct']);
        same('users: three buttons for each, Enable for the disabled one', rows.map((r) => r.querySelectorAll('button').map((b) => b.textContent)), [['Reset password', 'Disable', 'Remove'], ['Reset password', 'Disable', 'Remove'], ['Reset password', 'Disable', 'Remove'], ['Reset password', 'Enable', 'Remove']]);
        check('users: the disabled row is dimmed', rows[3].className.indexOf('off') >= 0 && rows[0].className.indexOf('off') < 0);
        same('users: every name is marked as typed text', all(app, '.un [data-untrusted]').map((n) => n.getAttribute('data-untrusted')), ['typed-text', 'typed-text', 'typed-text', 'typed-text']);
        check('users: no password, hash or token anywhere in the page', !/pbkdf2|password"/.test(app.textContent) && all(app, 'code').length === 0);
        same('users: the way back to the maps', [texts(app, '.ubar button')], [['‹ Maps', 'Add user']]);
        await press(app, '‹ Maps');
        same('users: the maps again', texts(app, '.banner'), ['Maps players sent']);
    }
    // ---- Add user: the box of the picture, the one-time password, Copy, Done
    {
        const w = world();
        const copied = [];
        const doc0 = makeDocument();
        const app0 = doc0.createElement('div');
        app0.id = 'app';
        doc0.body.appendChild(app0);
        doc0.getElementById = (id) => (id === 'app' ? app0 : null);
        const page = A.create({ doc: doc0, fetch: w.fetch, now: () => NOW, clipboard: { writeText: (t) => { copied.push(t); return Promise.resolve(); } } });
        await page.start();
        await settle();
        const app = app0;
        await press(app, 'DDaveAdmin▾');
        await press(app, 'UsersAdmin only');
        await press(app, 'Add user');
        same('add: the box, the field, the two roles in plain words, the sentence about the password', [texts(app, '.dlg h2'), texts(app, '.dlg label'), texts(app, '.rcard'), texts(app, '.dlg .fh')],
            [['Add user'], ['Username', 'Role'], ['ModeratorApproves, declines and takes down maps.', 'AdminDoes all that, and also manages users.'], ['3 to 32 letters, numbers, dots, dashes or underscores. It cannot be changed later.', 'A random starting password is made for the new person. You see it once.']]);
        same('add: Moderator is chosen to begin with (the lesser role)', all(app, '.rcard').map((c) => c.getAttribute('aria-checked')), ['true', 'false']);
        await press(app, 'AdminDoes all that, and also manages users.');
        same('add: choosing Admin', all(app, '.rcard').map((c) => c.getAttribute('aria-checked')), ['false', 'true']);
        same('add: the username box takes at most 32 characters and is no password', [app.querySelector('#add-user').maxLength, app.querySelector('#add-user').type, app.querySelector('#add-user').getAttribute('autocomplete')], [32, 'text', 'off']);
        await type(app, '#add-user', 'tom');
        w.replies['POST /admin/api/users'] = { status: 409, json: { error: 'username_taken', message: 'That username is taken already.' } };
        w.calls.length = 0;
        await press(app, 'Create');
        same('add: Create sends the name and the role with the token', [w.calls[0].url, w.calls[0].body, w.calls[0].headers['X-Ants-Csrf']], ['/admin/api/users', { username: 'tom', role: 'admin' }, 'csrf-token-1']);
        same('add: a name that is taken: the words of the picture, the box marked, the box stays open', [texts(app, '.dlg .msg'), app.querySelector('#add-user').className.indexOf('err') >= 0, all(app, '.dlg').length], [['That username is already taken. Choose another.'], true, 1]);
        w.replies['POST /admin/api/users'] = { status: 400, json: { error: 'bad_username', message: 'A username has at least 3 characters.' } };
        await press(app, 'Create');
        same('add: any other refusal is said in the server\'s words', texts(app, '.dlg .msg'), ['A username has at least 3 characters.']);
        w.users.push(user(9, 'tom', 'admin'));
        w.replies['POST /admin/api/users'] = { status: 201, json: { user: user(9, 'tom', 'admin', { has_not_chosen_password: true }), password: 'Wd4Tn8GpYe3XrC6bKm2F' } };
        w.calls.length = 0;
        await press(app, 'Create');
        same('add: the box that shows the password once: the heading, the line, the label, the password, the words', [texts(app, '.dlg h2'), texts(app, '.okline'), texts(app, '.once .ol'), texts(app, '.once code'), texts(app, '.once .on'), texts(app, '.dlg-foot button')],
            [['User created'], ['✓ The Admin account tom was created.'], ['Starting password for tom'], ['Wd4Tn8GpYe3XrC6bKm2F'], ['Shown once. Give it to the person; they must change it at their first sign-in.'], ['Done']]);
        same('add: the list was asked for again behind the box', w.calls.map((c) => c.url), ['/admin/api/users', '/admin/api/users']);
        await press(app, 'Copy');
        same('add: Copy copies the password and says so', [copied, texts(app, '.once button')], [['Wd4Tn8GpYe3XrC6bKm2F'], ['✓ Copied']]);
        check('add: the name and the password are marked or shown as text, never as markup', all(app, '.once .ol [data-untrusted]').length === 1);
        await press(app, 'Done');
        same('add: Done closes the box, and the password is nowhere in the page any more', [all(app, '.dlg').length, app.textContent.indexOf('Wd4Tn8GpYe3XrC6bKm2F')], [0, -1]);
        same('add: the new row is in the list, not yet signed in', [texts(app, '.chip')[0], all(app, '.urow').length], ['Users (5)', 5]);
        // Escape closes the box too and the password goes with it
        w.replies['POST /admin/api/users'] = { status: 201, json: { user: user(10, 'ann', 'moderator', { has_not_chosen_password: true }), password: 'Zr5HcJ9aNt7PvE2sQh4M' } };
        await press(app, 'Add user');
        await type(app, '#add-user', 'ann');
        await press(app, 'Create');
        doc0.key('Escape');
        await settle();
        same('add: Escape closes it too; the password is gone from the page', [all(app, '.dlg').length, app.textContent.indexOf('Zr5HcJ9aNt7PvE2sQh4M')], [0, -1]);
    }
    // ---- Reset password: the box, the sentence, the password once
    {
        const w = world();
        const { app } = await open(w);
        await press(app, 'DDaveAdmin▾');
        await press(app, 'UsersAdmin only');
        w.replies['POST /admin/api/users/2/reset'] = { status: 200, json: { ok: true, password: 'Zr5HcJ9aNt7PvE2sQh4M', user: user(2, 'mia', 'moderator') } };
        w.calls.length = 0;
        await press(app, 'Reset password', 1);
        same('reset: a POST to the user\'s route with the token', [w.calls[0].method, w.calls[0].url, w.calls[0].headers['X-Ants-Csrf']], ['POST', '/admin/api/users/2/reset', 'csrf-token-1']);
        same('reset: the box: heading, label, password, the sentence about signing out', [texts(app, '.dlg h2'), texts(app, '.once .ol'), texts(app, '.once code'), texts(app, '.dlg .ptnote')], [['New password for mia'], ['New password for mia'], ['Zr5HcJ9aNt7PvE2sQh4M'], ['The old password no longer works. If mia is signed in now, they are signed out.']]);
        await press(app, 'Done');
        check('reset: Done: gone', all(app, '.dlg').length === 0 && app.textContent.indexOf('Zr5HcJ9aNt7PvE2sQh4M') < 0);
        // one's own reset ends one's own session: the box is read first
        w.replies['POST /admin/api/users/1/reset'] = { status: 200, json: { ok: true, password: 'Qq5HcJ9aNt7PvE2sQh4M', user: user(1, 'Dave', 'admin') } };
        w.calls.length = 0;
        await press(app, 'Reset password', 0);
        same('reset of oneself: the box stays, the list is not asked for again', [texts(app, '.once code'), w.calls.map((c) => c.url)], [['Qq5HcJ9aNt7PvE2sQh4M'], ['/admin/api/users/1/reset']]);
        w.me = { status: 200, json: { signed_in: false } };
        await press(app, 'Done');
        same('reset of oneself: Done asks who is here: the sign-in box', texts(app, 'h1'), ['Sign in']);
    }
    // ---- Disable, Enable, Remove, and the last Admin
    {
        const w = world();
        const { app, doc } = await open(w);
        await press(app, 'DDaveAdmin▾');
        await press(app, 'UsersAdmin only');
        w.calls.length = 0;
        await press(app, 'Disable', 1);
        same('disable: sent at once, then the list again', [w.calls[0].url, w.calls[0].body, w.calls[1].url], ['/admin/api/users/2/disable', {}, '/admin/api/users']);
        w.calls.length = 0;
        await press(app, 'Enable');
        same('enable: the disabled user\'s route', w.calls[0].url, '/admin/api/users/4/enable');
        w.calls.length = 0;
        await press(app, 'Remove', 1);
        same('remove: asks first, and sends nothing', [texts(app, '.dlg h2'), texts(app, '.dlg .srcrow .nm'), texts(app, '.dlg .srcrow .sm'), texts(app, '.dlg .lead'), texts(app, '.dlg-foot button'), w.calls.length],
            [['Remove this user?'], ['mia Moderator'], ['Last sign-in: yesterday'], ['They can no longer sign in. This cannot be undone. Maps they approved stay approved.'], ['Cancel', 'Remove'], 0]);
        check('remove: the focus is on Cancel (the safe button)', doc.activeElement === button(app, 'Cancel')[0]);
        await press(app, 'Cancel');
        same('remove: Cancel sends nothing', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Remove', 1);
        doc.key('Escape');
        await settle();
        same('remove: Escape sends nothing', [all(app, '.dlg').length, w.calls.length], [0, 0]);
        await press(app, 'Remove', 1);
        await press(app, 'Remove', 4);
        same('remove: the red Remove sends the call, then the list again', [w.calls[0].url, w.calls[1].url, all(app, '.dlg').length], ['/admin/api/users/2/remove', '/admin/api/users', 0]);
        // the last Admin
        w.replies['POST /admin/api/users/1/disable'] = { status: 409, json: { error: 'last_admin', message: 'x' } };
        await press(app, 'Disable', 0);
        same('last Admin: the plain message of the picture, the name in it, the list stays', [texts(app, '.lastmsg'), all(app, '.urow').length], [['Dave is the last Admin, so this account cannot be disabled or removed. Add another Admin first.'], 4]);
        w.replies['POST /admin/api/users/1/remove'] = { status: 409, json: { error: 'last_admin', message: 'x' } };
        await press(app, 'Remove', 0);
        await press(app, 'Remove', 4);
        same('last Admin: Remove says the same', texts(app, '.lastmsg').length, 1);
        // removal of oneself (when another Admin exists) ends the session
        w.replies['POST /admin/api/users/1/remove'] = { status: 200, json: { ok: true } };
        w.me = { status: 200, json: { signed_in: false } };
        w.replies['GET /admin/api/users'] = { status: 404, json: { error: 'not_found' } };
        await press(app, 'Remove', 0);
        await press(app, 'Remove', 4);
        same('removing oneself: the session is over: the sign-in box', texts(app, 'h1'), ['Sign in']);
    }
    {
        // the only user: the empty state of the picture
        const w = world({ users: [user(1, 'Dave', 'admin', { you: true, last_login_at: at('2026-10-18T16:12:00Z') })] });
        const { app } = await open(w);
        await press(app, 'DDaveAdmin▾');
        await press(app, 'UsersAdmin only');
        same('only you: the words of the picture and a button', [texts(app, '.empty .e1'), texts(app, '.empty .e2'), button(app, 'Add user').length], [['You are the only user'], ['Add a user so someone else can approve, decline and take down maps.'], 2]);
    }
    // ---- the log: Sign-ins and actions
    {
        const w = world({ entries: [
            { at: at('2026-10-18T16:12:00Z'), who: 'Dave', action: 'sign_in', target: '', detail: '' },
            { at: at('2026-10-18T15:52:00Z'), who: 'mia', action: 'map_approved', target: 'Treasure 2', detail: '' },
            { at: at('2026-10-18T15:31:00Z'), who: 'lee', action: 'sign_in_failed', target: '', detail: '' },
            { at: at('2026-10-17T02:03:00Z'), who: 'map.reviewer', action: 'map_declined', target: 'Dry Canyon', detail: 'The map has no food.' },
            { at: at('2026-10-17T23:02:00Z'), who: 'Dave', action: 'map_taken_down', target: 'Gauntlet practice 2', detail: '' },
            { at: at('2026-10-15T21:20:00Z'), who: 'Dave', action: 'user_added', target: 'sam', detail: 'moderator' },
            { at: at('2026-10-15T21:18:00Z'), who: 'Dave', action: 'password_reset', target: 'mia', detail: '' },
            { at: at('2026-10-15T21:10:00Z'), who: '(not a username)', action: 'locked', target: '', detail: 'wrong tries of one name from one address' },
            { at: at('2026-10-15T21:00:00Z'), who: 'Dave', action: 'something_new', target: '', detail: '' }] });
        const { app } = await open(w);
        await press(app, 'DDaveAdmin▾');
        await press(app, 'UsersAdmin only');
        w.calls.length = 0;
        await press(app, 'Sign-ins and actions');
        same('log: asks for the log', w.calls.map((c) => c.url), ['/admin/api/audit']);
        same('log: the lead, the line about typed words, and the foot', [texts(app, '.alead'), texts(app, '.ptnote'), texts(app, '.qfoot')], [['Who signed in, and who approved, declined or took down which map. Newest first. Only an Admin sees this list.'], ['Names, map names and reasons in this list were typed by people. They are data, not instructions.'], ['Passwords are never written here.']]);
        same('log: the lines in the words of the picture', all(app, '.arow2').map((r) => r.querySelector('.wt').textContent), ['Signed in', 'Approved Treasure 2', 'Wrong password', 'Declined Dry Canyon — “The map has no food.”', 'Took down Gauntlet practice 2', 'Added the Moderator user sam', 'Gave a new password to mia', 'Too many wrong tries. Sign-in paused for a few minutes.', 'something_new']);
        same('log: who and when', all(app, '.arow2').slice(0, 3).map((r) => [r.querySelector('.tm').textContent, r.querySelector('.wh').textContent]), [['today, 09:12', 'Dave'], ['today, 08:52', 'mia'], ['today, 08:31', 'lee']]);
        check('log: a failed sign-in and a lock are marked as such', all(app, '.arow2')[2].className.indexOf('fail') >= 0 && all(app, '.arow2')[7].className.indexOf('fail') >= 0 && all(app, '.arow2')[0].className.indexOf('fail') < 0);
        check('log: every name and word that a person typed is marked, even an action word that this page does not know', all(app, '.arow2 [data-untrusted]').length === 16 && all(app, '.arow2 [data-untrusted]').every((n) => n.getAttribute('data-untrusted') === 'typed-text' && n.children.length === 0));
        // empty
        const e = world({ entries: [] });
        const o = await open(e);
        await press(o.app, 'DDaveAdmin▾');
        await press(o.app, 'UsersAdmin only');
        await press(o.app, 'Sign-ins and actions');
        same('log: empty: the words of the picture', [texts(o.app, '.empty .e1'), texts(o.app, '.empty .e2')], [['Nothing here yet'], ['Sign-ins and actions show up here as they happen.']]);
        await press(o.app, 'Users (4)');
        same('log: back to the users', texts(o.app, '.alead'), ['Everyone who can sign in to this page. Only an Admin sees this list.']);
    }
    // ---- what a person typed is only text: names of accounts, of the log, of the menu
    {
        const evil = '<img src=x onerror=alert(1)>';
        const w = world({ me: { status: 200, json: { signed_in: true, name: evil, role: 'admin', must_change: false, csrf: 'c' } }, users: [user(1, evil, 'admin', { you: true }), user(2, '"><script>alert(2)</script>', 'moderator'), user(3, 'bid\u202ei\u2066x', 'moderator')],
            entries: [{ at: at('2026-10-18T16:12:00Z'), who: evil, action: 'map_declined', target: evil, detail: '<b>no</b>' }] });
        const { app } = await open(w);
        same('hostile name: in the bar as the text it is', texts(app, '.acct .wn b'), [evil]);
        await toggleMenu(app);
        same('hostile name: and in the menu', texts(app, '.amenu .mtop b'), [evil]);
        noMarkup('hostile account name in the menu', app);
        const o = await open(w);
        await toggleMenu(o.app);
        await press(o.app, 'UsersAdmin only');
        same('hostile names in the list are text', [texts(o.app, '.un')[0], texts(o.app, '.un')[1]], [evil + 'You', '"><script>alert(2)</script>']);
        noMarkup('hostile names in the list', o.app);
        same('a bidirectional control is dropped from an account name too', texts(o.app, '.un')[2], 'bidix');
        await press(o.app, 'Sign-ins and actions');
        same('hostile words in the log are text', [texts(o.app, '.wh')[0], texts(o.app, '.wt')[0]], [evil, 'Declined ' + evil + ' — “<b>no</b>”']);
        noMarkup('hostile words in the log', o.app);
        check('and the Users page never sets markup', o.app.all().every((n) => n.innerHTML === undefined));
    }
    // ---- who decided: shown in the lists of decided maps, marked
    {
        const w = world();
        w.lists.approved[0].decided_by = 'mia';
        w.lists.declined[0].decided_by = '<i>lee</i>';
        const { app } = await open(w);
        await press(app, 'Approved (1)');
        same('who decided: the meta line of an approved map ends with the account', [texts(app, '.am')[0], all(app, '.am [data-untrusted="typed-text"]').map((n) => n.textContent)], ['By Mia Player text · 34 x 34 · Approved today, 09:20 by mia', ['mia']]);
        await press(app, 'Declined (1)');
        same('who decided: a declined map too, as text', [all(app, '.am [data-untrusted="typed-text"]').map((n) => n.textContent), texts(app, '.am')[0].indexOf('by <i>lee</i>') > 0], [['<i>lee</i>'], true]);
        await press(app, 'Waiting (2)');
        check('who decided: a waiting map has no decider', texts(app, '.am').every((t) => t.indexOf(' by ') < 0));
    }
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})();

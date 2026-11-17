// Runs the admin page's OWN code (web/admin_page.js) with a small fake of the DOM and a fake server: what the page shows when the door answers GET /admin/api/me with 200 (the Maps page and the chip with the
// name and the role), 401 (one card with a link to the Discord login), 403 not_allowed (the card's heading) and anything else (one line); that a later call that the door refuses swaps the page to the same card;
// that no password box, sign-in form or menu exists; what each button of the Maps page sends (with the CSRF token of /me); the questions before Approve and Take down; the empty states; and that nothing a person
// typed becomes markup. tests/scripts/test_web_admin.py runs this with node (the quick tier). usage: node web_admin_check.js web/admin_page.js     (exit 0: every check holds; every failure is printed)
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
        const m = /^\/admin\/api\/maps\?state=(\w+)$/.exec(url);
        if (m) return reply(200, { state: m[1], counts: w.counts, maps: w.lists[m[1]] || [] });
        const n = /^\/admin\/api\/next-name\?name=(.*)$/.exec(url);
        if (n) return reply(200, { name: decodeURIComponent(n[1]).replace(/ \d+$/, '') + ' 3' });
        if ((init.method || 'GET') === 'POST') return reply(200, { map: {}, counts_changed: true });
        return reply(404, { error: 'not_found' });
    };
    return w;
}
// an answer whose body is not JSON (the web server's own answer to too many requests)
const notJson = (status) => () => ({ status, json: async () => { throw new SyntaxError('Unexpected token'); } });
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

const HEAD = 'This page is for the site\u2019s admins.';
const LOGIN_LINE = 'Log in with the Discord account that has the admin role.';
const UNAVAILABLE = 'The admin page is not available right now.';
function noMarkup(label, app) {
    const allowed = new Set(['div', 'span', 'img', 'button', 'a', 'label', 'input', 'textarea', 'h1', 'h2', '#text', 'b', 'small']);
    for (const t of new Set(app.all().map((n) => n.tagName))) check(label + ': only the page\'s own kinds of node (' + t + ')', allowed.has(t));
    check(label + ': innerHTML was never set', app.all().every((n) => n.innerHTML === undefined));
}
// nothing of the retired password door is on the page: no password box, no form, no menu, and no word that offers one
function noPasswordDoor(label, app) {
    check(label + ': no password box, no form, no menu, no eye', all(app, 'input').every((i) => i.type !== 'password') && all(app, 'form').length === 0 && all(app, '.amenu').length === 0 && all(app, '.eye').length === 0);
    check(label + ': no word offers Users, Sign out, Change password, a sign-in or a Moderator', !/users|sign out|sign in|sign-in|change password|password|moderator/i.test(app.textContent));
}
// what the page shows: the Maps page or one of the three cards
function shown(app) {
    if (all(app, '.chip').length) return 'maps';
    if (all(app, 'a').length) return 'login';
    if (all(app, 'h1').length) return 'heading';
    if (all(app, '.msg').length) return 'line';
    return 'nothing';
}

// an error that the page or this check throws and nothing catches is a failure, said in words (not only a stack and an exit code)
process.on('unhandledRejection', (e) => { console.log('FAIL an error was thrown and nothing caught it (' + (e && e.message) + ')'); process.exit(1); });
(async () => {

    // ---- GET /admin/api/me answers 200: the Maps page, and a chip with the name and the role
    for (const [role, roleWords] of [['admin', 'Admin'], ['owner', 'Owner']]) {
        const w = world({ me: who('Dave', role) });
        const { app } = await open(w);
        same(role + ': the question who is here is asked once, with the cookie, then the list', [w.calls.map((c) => c.url), w.calls[0].credentials, w.calls[0].method], [['/admin/api/me', '/admin/api/maps?state=waiting'], 'same-origin', 'GET']);
        same(role + ': the Maps section: its banner and its four tabs', [texts(app, '.banner'), texts(app, '.chip')], [['Maps players sent'], ['Waiting (2)', 'Approved (1)', 'Declined (1)', 'Taken down (1)']]);
        same(role + ': the chip shows the name, the role and the first letter, once', [texts(app, '.acct .wn b'), texts(app, '.acct .wn small'), texts(app, '.acct .av'), all(app, '.acct').length], [['Dave'], [roleWords], ['D'], 1]);
        check(role + ': the chip is nothing to press (no button, no link, no menu)', all(app, '.acct')[0].tagName === 'div' && all(app, '.acct button, .acct a').length === 0 && all(app, 'a').length === 0);
        same(role + ': the name is typed text and is marked as data', all(app, '.acct [data-untrusted]').map((n) => n.getAttribute('data-untrusted')), ['typed-text']);
        same(role + ': everybody who is let in has all of the page: three buttons on a waiting map', Array.from(all(app, '.arow')[0].querySelectorAll('button')).map((b) => b.textContent), ['Approve', 'Rename and approve', 'Decline']);
        noPasswordDoor(role, app);
        noMarkup(role, app);
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
    // ---- GET /admin/api/me answers 401: one card with a link to the Discord login
    {
        const w = world({ me: { status: 401, json: { error: 'not_signed_in', message: 'The words of the server, which the page does not show.' } } });
        const { app } = await open(w);
        same('401: only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
        same('401: the heading, the line and the link, in these words and no others', [texts(app, 'h1'), texts(app, '.l2'), texts(app, 'a'), app.textContent], [[HEAD], [LOGIN_LINE], ['Log in with Discord'], HEAD + LOGIN_LINE + 'Log in with Discord']);
        const link = all(app, 'a')[0];
        same('401: one plain same-tab link to the login of the chat hub, and nothing that runs when it is pressed', [all(app, 'a').length, link.getAttribute('href'), link.getAttribute('target'), (link.listeners.click || []).length], [1, '/hub/login?next=admin', null, 0]);
        check('401: the link looks like the page\'s button', link.className.split(' ').includes('btn'));
        check('401: nothing else: no Maps section, no chip, no box to type in, no other button, no form', all(app, '.chip').length === 0 && all(app, '.acct').length === 0 && all(app, '.banner').length === 0 && all(app, 'input').length === 0 && all(app, 'button').length === 0 && all(app, 'form').length === 0);
        same('401: the card is what is shown', shown(app), 'login');
        noPasswordDoor('401', app);
        noMarkup('401', app);
    }
    // ---- 403 not_allowed: logged in, not an admin: the heading only
    {
        const w = world({ me: { status: 403, json: { error: 'not_allowed', message: 'The words of the server, which the page does not show.' } } });
        const { app } = await open(w);
        same('403 not_allowed: the heading and nothing else (no line, no link, no button, no Maps section)', [texts(app, 'h1'), app.textContent, all(app, '.l2').length, all(app, 'a').length, all(app, 'button').length, all(app, '.msg').length, all(app, '.chip').length], [[HEAD], HEAD, 0, 0, 0, 0, 0]);
        same('403 not_allowed: only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
        same('403 not_allowed: the card is what is shown', shown(app), 'heading');
        noPasswordDoor('403 not_allowed', app);
    }
    // ---- everything else: one line
    for (const [label, reply] of [
        ['403 closed', { status: 403, json: { error: 'closed', message: 'The admin page is closed.' } }],
        ['503 door_off', { status: 503, json: { error: 'door_off', message: 'The words of the server.' } }],
        ['503 submissions_off', { status: 503, json: { error: 'submissions_off', message: 'The words of the server.' } }],
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
    // ---- the Maps page: the Waiting tab, as drawn
    {
        const w = world();
        const { app, page } = await open(w);
        same('admin page: the section registry holds the maps', page.sections(), ['maps']);
        same('admin page: the banner of the section, once', texts(app, '.banner'), ['Maps players sent']);
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
                if (want === 'heading') same(label + ' answered ' + why + ': the heading only', app.textContent, HEAD);
                if (want === 'line') same(label + ' answered ' + why + ': the one line', app.textContent, UNAVAILABLE);
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
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})();

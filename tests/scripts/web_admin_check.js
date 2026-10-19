// Runs the owner's page's OWN code (web/admin_page.js) with a small fake of the DOM and a fake server: what the page shows to nobody, to a stranger and to the owner (exactly the approved pictures: the logon, "This page
// is for the owner only", the four tabs), what each button sends (with the CSRF token), the questions before a take-down, the empty states, a session that ends, and that nothing a person typed becomes markup.
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
function world(overrides) {
    const w = Object.assign({
        me: { status: 200, json: { signed_in: true, name: 'Dave', owner: true, csrf: 'csrf-token-1' } },
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
function noMarkup(label, app) {
    const tags = new Set(all(app, '*'.length ? 'div' : 'div').length >= 0 ? app.all().map((n) => n.tagName) : []);
    const allowed = new Set(['div', 'span', 'img', 'button', 'a', 'form', 'label', 'input', 'textarea', 'h1', 'h2', '#text', 'b']);
    for (const t of tags) check(label + ': only the page\'s own kinds of node (' + t + ')', allowed.has(t));
    check(label + ': innerHTML was never set', app.all().every((n) => n.innerHTML === undefined));
}

(async () => {
    // ---- nobody: the logon of the picture C1
    {
        const w = world({ me: { status: 200, json: { signed_in: false } } });
        const { app } = await open(w);
        same('nobody: only the question who is here is asked', w.calls.map((c) => c.url), ['/admin/api/me']);
        same('nobody: the heading and the line of picture C1', [texts(app, 'h1'), texts(app, '.l2')], [['Owner logon'], ['Only the owner can open this page']]);
        const links = all(app, 'a');
        same('nobody: one way on, the Discord button, to the helper', [links.length, links[0].getAttribute('href') || links[0].href, links[0].textContent.trim()], [1, '/admin/login', '● Sign in with Discord']);
        check('nobody: no tab, no list, no banner', all(app, '.chip').length === 0 && all(app, '.arow').length === 0 && all(app, '.banner').length === 0);
        same('nobody: the call carries the cookie by itself', w.calls[0].credentials, 'same-origin');
    }
    // ---- the owner's page is off on this server (404): the page says only that it is for the owner
    {
        const w = world({ replies: { 'GET /admin/api/me': { status: 404, json: { error: 'not_found' } } } });
        const { app } = await open(w);
        same('off: only "This page is for the owner only"', [texts(app, 'h1'), all(app, '.l2').map((n) => n.textContent), all(app, 'a').length, all(app, 'form').length], [['This page is for the owner only'], [''], 0, 0]);
    }
    // ---- a stranger: C2
    {
        const w = world({ me: { status: 200, json: { signed_in: true, name: 'Mia', owner: false } } });
        const { app } = await open(w);
        same('stranger: the sentence, who they are and Sign out', [texts(app, 'h1'), texts(app, '.l2')], [['This page is for the owner only'], ['You are signed in with Discord as Mia.']]);
        const form = all(app, 'form')[0];
        same('stranger: Sign out is a POST to the helper', [form.method, form.action, form.querySelector('button').textContent], ['post', '/admin/logout', 'Sign out']);
        check('stranger: nothing of the maps is asked or shown', w.calls.length === 1 && all(app, '.arow').length === 0 && all(app, '.chip').length === 0);
        same('stranger: the name is text, never markup', (() => { const wx = world({ me: { status: 200, json: { signed_in: true, name: '<img src=x onerror=alert(1)>', owner: false } } }); return wx.me.json.name; })(), '<img src=x onerror=alert(1)>');
    }
    {
        const w = world({ me: { status: 200, json: { signed_in: true, name: '<img src=x onerror=alert(1)>', owner: false } } });
        const { app } = await open(w);
        check('stranger with a hostile name: shown as text', texts(app, '.l2')[0].indexOf('<img src=x onerror=alert(1)>') > 0);
        noMarkup('stranger with a hostile name', app);
    }
    // ---- the owner: C3 the Waiting tab, exactly as drawn
    {
        const w = world();
        const { app, page } = await open(w);
        same('owner: the section registry holds the one section', page.sections(), ['maps']);
        same('owner: the banner of the section, once', texts(app, '.banner'), ['Maps players sent']);
        same('owner: four tabs with their counts, the first chosen', [texts(app, '.chip'), all(app, '.chip').map((c) => c.className.indexOf(' on') > 0)], [['Waiting (2)', 'Approved (1)', 'Declined (1)', 'Taken down (1)'], [true, false, false, false]]);
        same('owner: signed in as, with Sign out', [texts(app, '.signed b'), texts(app, '.signed .linkbtn')], [['Dave'], ['Sign out']]);
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
    // ---- the sign-out stays on a phone: its own element, apart from the words that CSS hides
    {
        const w = world();
        const { app } = await open(w);
        same('the sign-out is in the signed-in line, apart from the words', [all(app, '.signed .who').length, all(app, '.signed form button').map((b) => b.textContent)], [1, ['Sign out']]);
        check('the words are the part that can be hidden', all(app, '.signed .who')[0].textContent.indexOf('Dave') >= 0 && all(app, '.signed form')[0].parentNode.className === 'signed');
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
        const marked = all(app, '[data-untrusted]').map((n) => n.textContent);
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
        same('the cookie ended: the page asks who is here and shows the logon', texts(app, 'h1'), ['Owner logon']);
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
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})();

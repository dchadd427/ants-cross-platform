// OPT-IN, not part of ./run_tests.sh: the admin page (web/admin.html, admin_page.js, admin.css; docs/MAP_SUBMISSIONS.md "The admin page") in a REAL browser (Chromium through Playwright), on a small static
// server of the repository's own files under the page's real content security policy (read from docker/nginx.conf), with made-up answers for the game server's /admin/api, the hub's /hub/admin/api, /hub/me,
// /hub/logout and /busy. It draws the scenes of the approved pictures (signed out, not allowed as a player and as a moderator, Maps waiting and approved, Chat, the Ban question, a refusal and a done line, the
// Log with its four filters, the section Bot matches (Start now, Running, Schedules: filled in, done, refused, the questions), the Status well and in trouble, the chip's menu, the owner's Chat) at 390 wide (a phone), 1280 wide (a desktop) and 320 wide (checks only), and checks on each:
//   1. the page does not scroll sideways, and nothing is wider than the window;
//   2. every button, link, box and select that is shown is 44 px tall (and wide) or more, and inside the window;
//   3. no error of the script and no refusal of the content security policy;
//   4. the sections are a row of tabs from 601 px and a select under that, never both;
//   5. the names are text: a name that is markup is shown as it is, and no tag of it is in the page;
//   6. a real double click on the form's Ban of a short phone, where the Ban of the question that it opens comes to stand under the second click, bans nobody (the question ignores a press in its first half second).
//   --shots DIR takes a picture of every scene (NAME_phone.png, NAME_desktop.png; the phone's is the whole page, the window for the question and the menu; a scene of the section Bot matches is one subject cut out of the page:
//   a panel, the tabs or the question, a desktop at 1x (a panel is about 600 px wide) and a phone at 2x, so that no picture is wider than 1000 px); --only PREFIX draws the scenes that start with it.
//   Made-up names only (basil and tess are admins, sage a moderator, pebble a player, clover.4782 the spammer). ADMIN_SHOT_THUMBS=DIR names a folder with mapthumb_a.png to mapthumb_d.png for the pictures of the
//   maps (else small drawings are made).
//
//   node tests/scripts/web_admin_browser.js [--shots DIR] [--only PREFIX]      (CHROME=/path/to/chromium names a browser, PLAYWRIGHT_MODULE=/path/to/playwright-core a module; as root it runs with --no-sandbox)
//
// Needs `playwright` (npm) and a Chromium that it can start; without them it says so and exits 0 (nothing was checked). Exit status: 0 every check passed (or none could be run), 1 a check failed.
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');

let playwright = null;
for (const where of ['playwright', 'playwright-core', process.env.PLAYWRIGHT_MODULE || '']) {
    if (!where) continue;
    try { playwright = require(where); break; } catch (e) { /* not installed there */ }
}
if (!playwright) { console.log('SKIP: playwright is not installed (npm install playwright): the admin page was NOT run in a browser'); process.exit(0); }

const arg = (name) => (process.argv.indexOf(name) !== -1 ? process.argv[process.argv.indexOf(name) + 1] : null);
const ROOT = path.resolve(__dirname, '..', '..');
const SHOTS = arg('--shots') ? path.resolve(arg('--shots')) : null;
const ONLY = arg('--only') || '';
const NOW_MS = Date.UTC(2026, 10, 16, 21, 20, 0);            // Mon 16 Nov 2026, 13:20 Pacific
const at = (iso) => Math.floor(Date.parse(iso) / 1000);
const SIZES = { phone: { width: 390, height: 844, dsf: 2, mobile: true }, desktop: { width: 1280, height: 800, dsf: 2, mobile: false }, narrow: { width: 320, height: 640, dsf: 1, mobile: true } };
let checks = 0;
let failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }

// ---- the made-up world -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const STAMPS = { '@@GAME_VERSION@@': 'v0.16.11', '@@BUILD_ID@@': '9d41c2e', '@@BUILD_DATE@@': '2026-11-16 09:30 PT' };
const conf = fs.readFileSync(path.join(ROOT, 'docker', 'nginx.conf'), 'utf8');
const CSP = /add_header Content-Security-Policy "([^"]+)" always;/.exec(conf.slice(conf.indexOf('location = /admin {')))[1];          // (the first policy after the page's own block)
function page() {
    let text = fs.readFileSync(path.join(ROOT, 'web', 'admin.html'), 'utf8');
    for (const k of Object.keys(STAMPS)) text = text.split(k).join(STAMPS[k]);
    return text;
}
function mapRow(id, extra) { return Object.assign({ id, state: 'waiting', kind: 'classic', name: 'x', maker: 'x', note: '', width: 31, height: 31, sent_at: at('2026-11-16T16:41:00Z'), name_taken: false, has_picture: true, findings: [] }, extra); }
const MAPS = {
    waiting: [mapRow('a'.repeat(32), { name: 'Dry Canyon', maker: 'Lee', note: 'Made by hand.', thumb: 'a' }), mapRow('b'.repeat(32), { name: 'Gold Rush', maker: 'Sam', note: 'Four lanes and a gold pile in the middle.', thumb: 'b', sent_at: at('2026-11-16T15:12:00Z') }),
        mapRow('c'.repeat(32), { name: 'Big Meadow', maker: 'Mia', kind: 'extended', width: 200, height: 160, thumb: 'c', sent_at: at('2026-11-15T22:30:00Z') })],
    approved: [mapRow('d'.repeat(32), { state: 'approved', name: 'Mossy Ridge', approved_name: 'Mossy Ridge', maker: 'Fern', decided_at: at('2026-11-16T19:30:00Z'), decided_by: 'basil', thumb: 'd' }),
        mapRow('e'.repeat(32), { state: 'approved', name: 'Cave Crossing', approved_name: 'Cave Crossing', maker: 'Lee', decided_at: at('2026-11-14T20:05:00Z'), decided_by: 'tess', thumb: 'a' }),
        mapRow('f'.repeat(32), { state: 'approved', name: 'Old Bridge', approved_name: 'Old Bridge', maker: 'Sam', kind: 'extended', width: 128, height: 128, decided_at: at('2026-11-12T18:00:00Z'), decided_by: 'tess', thumb: 'b' })]
};
const REF = { basil: '1b2a3c4d5e6f7a8b', tess: '2c3d4e5f6a7b8c9d', sage: '3d4e5f6a7b8c9d0e', clover: '4e5f6a7b8c9d0e1f', spam: '5f6a7b8c9d0e1f2a', hazel: '6a7b8c9d0e1f2a3b', juniper: '7b8c9d0e1f2a3b4c', pebble: '8c9d0e1f2a3b4c5d', marlow: '9d0e1f2a3b4c5d6e', fern: 'ae1f2a3b4c5d6e7f' };
function world() {
    return {
        counts: { waiting: 3, approved: 12, declined: 2, taken_down: 1 },
        bans: [{ ref: REF.clover, name: 'clover.4782', at: at('2026-11-16T17:12:00Z'), by: REF.basil, by_name: 'basil' }, { ref: REF.spam, name: 'spam.king', at: at('2026-11-14T20:00:00Z'), by: REF.tess, by_name: 'tess' },
            { ref: '7c2e91d0a4b83f56', name: '', at: at('2026-11-09T20:00:00Z'), by: 'command line', by_name: '' }],
        mutes: [{ ref: REF.hazel, name: 'hazel_b', until: at('2026-11-16T22:51:00Z'), by: REF.sage, by_name: 'sage' }, { ref: REF.juniper, name: 'juniper.berry', until: at('2026-11-17T05:20:00Z'), by: REF.tess, by_name: 'tess' },
            { ref: REF.pebble, name: 'pebble_9', until: at('2026-11-23T19:40:00Z'), by: REF.basil, by_name: 'basil' }],
        hidden: [{ ref: REF.clover, name: 'clover.4782' }, { ref: REF.marlow, name: 'marlow.old' }],
        modlog: [
            { at: at('2026-11-16T21:02:00Z'), action: 'mute', actor: REF.basil, actor_name: 'basil', target: REF.hazel, target_name: 'hazel_b', detail: '60' },
            { at: at('2026-11-16T19:47:00Z'), action: 'del', actor: REF.sage, actor_name: 'sage', target: REF.clover, target_name: 'clover.4782', detail: '148' },
            { at: at('2026-11-16T17:12:00Z'), action: 'ban', actor: REF.basil, actor_name: 'basil', target: REF.clover, target_name: 'clover.4782', detail: '' },
            { at: at('2026-11-16T17:11:00Z'), action: 'kick', actor: REF.sage, actor_name: 'sage', target: REF.clover, target_name: 'clover.4782', detail: '' },
            { at: at('2026-11-15T20:00:00Z'), action: 'nopic', actor: REF.tess, actor_name: 'tess', target: REF.marlow, target_name: 'marlow.old', detail: '' },
            { at: at('2026-11-15T18:00:00Z'), action: 'unmute', actor: REF.tess, actor_name: 'tess', target: REF.pebble, target_name: 'pebble_9', detail: '' },
            { at: at('2026-11-14T20:00:00Z'), action: 'ban', actor: REF.tess, actor_name: 'tess', target: REF.spam, target_name: 'spam.king', detail: '' },
            { at: at('2026-11-13T20:00:00Z'), action: 'del', actor: 'terminal', actor_name: '', target: '', target_name: '', detail: '97' },
            { at: at('2026-11-12T20:00:00Z'), action: 'mute', actor: '3f9a52c07de18b44', actor_name: '', target: REF.fern, target_name: 'fern.leaf', detail: '10' }],
        audit: [{ at: at('2026-11-16T19:30:00Z'), who: 'basil', ref: REF.basil, action: 'map_approved', target: 'Mossy Ridge', detail: '' }, { at: at('2026-11-16T18:02:00Z'), who: 'tess', ref: REF.tess, action: 'map_declined', target: 'Test 1', detail: 'Same as an existing map.' },
            { at: at('2026-11-15T20:00:00Z'), who: 'basil', ref: REF.basil, action: 'map_taken_down', target: 'Old Bridge', detail: '' }, { at: at('2026-11-12T20:00:00Z'), who: 'tess', ref: REF.tess, action: 'map_put_back', target: 'Old Bridge', detail: '' }],
        busy: { matches: 2, players: 5 }, posts: [], bots: botsWorld(),
        status: { store: { bytes: 4404019, limit: 67108864, log: 314572, free: 0, lines: 187, oldest: at('2026-11-09T20:00:00Z'), pictures: 41, pictures_max: 1000, picture_bytes: 1153434, picture_bytes_max: 25165824, off: 6, blocked: 1 },
            health: { roles: 'ok', roles_known: 14, kind: null, status: null, retry: null }, last_picture: { kind: 'ok', age: 180 }, version: { code: '9f3c1a2e', started: at('2026-11-16T17:30:00Z') } }
    };
}

// ---- the matches of computer players of the pictures: four that go, the maps that may be named, the schedules; the calls of the section answer as the game server does (docs/MAP_SUBMISSIONS.md "Bot matches on the admin page")
const BOT_MAPS = [
    { file: 'TINY.LVL', name: 'TINY', group: 'original', width: 32, height: 32, places: 1 }, { file: 'SMALL.LVL', name: 'SMALL', group: 'original', width: 48, height: 48, places: 1 }, { file: 'MEDIUM.LVL', name: 'MEDIUM', group: 'original', width: 64, height: 64, places: 1 },
    { file: 'GAUNTLET.LVL', name: 'GAUNTLET', group: 'original', width: 64, height: 64, places: 1 }, { file: 'TREASURE.LVL', name: 'TREASURE', group: 'original', width: 96, height: 96, places: 1 }, { file: 'ISLANDS.LVL', name: 'ISLANDS', group: 'original', width: 96, height: 96, places: 1 },
    { file: 'POPcOrN.lvl', name: 'POPcOrN', group: 'library', width: 64, height: 64, places: 1 }, { file: 'HELLRAISER.lvl', name: 'HELLRAISER', group: 'library', width: 64, height: 64, places: 1 }, { file: 'Bombz Away.lvl', name: 'Bombz Away', group: 'library', width: 64, height: 64, places: 1 },
    { file: 'Cave Crossing.lvl', name: 'Cave Crossing', group: 'players', width: 31, height: 31, places: 1 }, { file: 'Fight Island.lvl', name: 'Fight Island', group: 'players', width: 150, height: 150, places: 3 },
    { file: 'Mossy Ridge.lvl', name: 'Mossy Ridge', group: 'players', width: 31, height: 31, places: 1 }, { file: 'Old Bridge.lvl', name: 'Old Bridge', group: 'players', width: 128, height: 128, places: 2 }
];
const BOT_ROTATION = { count: 1, map: 'random:rotation', rules: 'random', bots: 'random', levels: ['rmh', 'rmh', 'rmh', 'rmh'], teams: 'random', split: '' };
function botMatch(id, name, rules, seats, teams, startedIso, extra) { return Object.assign({ id, name, rules, seats, teams, started: at(startedIso), live: id + '-live' }, extra); }
function botsWorld() {
    return {
        people: 14, total: 48, refuse: null, nextId: 40, nextSched: 4,
        matches: [
            botMatch('m1', 'MEDIUM', 'highest-score', [{ seat: 0, level: 'hard' }, { seat: 2, level: 'medium' }, { seat: 3, level: 'hard' }], 'ffa', '2026-11-16T21:14:00Z', { schedule: 'Hourly rotation', live: 'MEDIUM-20261116-211400Z' }),
            botMatch('m2', 'TREASURE', '187', [{ seat: 0, level: 'hard' }, { seat: 1, level: 'hard' }, { seat: 2, level: 'medium' }, { seat: 3, level: 'easy' }], '03-12', '2026-11-16T21:16:00Z', { by: 'basil', live: 'TREASURE-20261116-211600Z' }),
            botMatch('m3', 'Fight Island', '187', [{ seat: 1, level: 'hard' }, { seat: 3, level: 'hard' }], 'ffa', '2026-11-16T21:16:00Z', { by: 'basil', live: 'Fight Island-20261116-211600Z' }),
            botMatch('m4', 'TINY', 'highest-score', [{ seat: 0, level: 'medium' }, { seat: 1, level: 'medium' }, { seat: 2, level: 'hard' }, { seat: 3, level: 'hard' }], '02-13', '2026-11-16T21:19:00Z', { by: 'tess', live: null })
        ],
        schedules: [
            { id: 1, name: 'Hourly rotation', default: true, on: true, when: { kind: 'every', minutes: 60 }, what: BOT_ROTATION, last: at('2026-11-16T21:14:00Z'), next: at('2026-11-16T22:14:00Z') },
            { id: 2, name: 'Evening swarm', default: false, on: true, when: { kind: 'times', times: ['18:00', '20:00', '22:00'] }, what: { count: 4, map: 'random:rotation', rules: 'highest-score', bots: '4', levels: ['hard', 'hard', 'medium', 'medium'], teams: 'random', split: '' }, last: at('2026-11-16T04:00:00Z'), next: at('2026-11-17T02:00:00Z') },
            { id: 3, name: 'ISLANDS practice', default: false, on: false, when: { kind: 'every', minutes: 30 }, what: { count: 2, map: 'ISLANDS.LVL', rules: 'highest-score', bots: 'random', levels: ['easy', 'easy', 'easy', 'easy'], teams: 'ffa', split: '' }, last: at('2026-11-15T20:30:00Z'), next: null }
        ],
        audit: [
            { at: at('2026-11-16T21:16:00Z'), who: 'basil', ref: REF.basil, action: 'bot_started', target: '2 matches', detail: 'TREASURE, Fight Island · 187 · 4 and 2 bots' },
            { at: at('2026-11-16T20:42:00Z'), who: 'tess', ref: REF.tess, action: 'bot_stopped', target: 'GAUNTLET', detail: '' },
            { at: at('2026-11-16T18:10:00Z'), who: 'basil', ref: REF.basil, action: 'bot_schedule_added', target: 'Evening swarm', detail: 'at 18:00, 20:00, 22:00 (Pacific) · 4 matches' },
            { at: at('2026-11-16T18:05:00Z'), who: 'basil', ref: REF.basil, action: 'bot_schedule_off', target: 'ISLANDS practice', detail: '' },
            { at: at('2026-11-15T22:00:00Z'), who: 'tess', ref: REF.tess, action: 'bot_stopped_all', target: '5 matches', detail: '' },
            { at: at('2026-11-14T20:00:00Z'), who: 'tess', ref: REF.tess, action: 'bot_schedule_changed', target: 'Hourly rotation', detail: 'every 60 minutes' }
        ]
    };
}
function fullHouse() {
    const seats = [{ seat: 0, level: 'hard' }, { seat: 1, level: 'medium' }];
    W.bots.matches = ['TINY', 'SMALL', 'MEDIUM', 'GAUNTLET', 'TREASURE', 'POPcOrN', 'HELLRAISER', 'Fight Island'].map((n, i) => botMatch('f' + i, n, i % 3 === 0 ? '187' : 'highest-score', seats, 'ffa', '2026-11-16T21:1' + (i % 10) + ':00Z', i % 2 ? { by: 'basil' } : { schedule: 'Evening swarm' }));
}
function botApi(req, p, json) {
    const B = W.bots;
    if (p === '/admin/api/bot-matches' && req.method === 'GET') { json(200, { matches: B.matches, cap: 8, places: { people: B.people, bots: B.matches.length, total: B.total, bots_max: 16 }, replays: true, untrusted: true, untrusted_fields: ['name', 'by', 'schedule'] }); return true; }
    if (p === '/admin/api/bot-options') { json(200, { maps: BOT_MAPS, rotation: { 'highest-score': 9, '187': 6 }, untrusted: true, untrusted_fields: ['name'] }); return true; }
    if (p === '/admin/api/bot-schedules' && req.method === 'GET') { json(200, { schedules: B.schedules, max: 20, untrusted: true, untrusted_fields: ['name'] }); return true; }
    if (p.indexOf('/admin/api/bot-') !== 0 || req.method !== 'POST') return false;
    let raw = '';
    req.on('data', (c) => { raw += c; });
    req.on('end', () => {
        let body = {};
        try { body = JSON.parse(raw || '{}'); } catch (e) { /* a body that is not JSON is an empty one */ }
        let m;
        W.posts.push(p);
        if (p === '/admin/api/bot-matches') {
            if (B.refuse === 'busy') return json(409, { error: 'busy', people: B.people, total: B.total });
            const free = 8 - B.matches.length;
            if (body.count > free) return json(409, { error: 'cap', running: B.matches.length, asked: body.count, free });
            if (body.map === 'ISLANDS.LVL' && body.rules === '187') return json(409, { error: 'map_187' });
            const names = ['SMALL', 'GAUNTLET', 'TREASURE', 'POPcOrN', 'MEDIUM', 'TINY'];
            const file = BOT_MAPS.filter((x) => x.file === body.map)[0];
            for (let i = 0; i < body.count; i++) B.matches.push(botMatch('n' + (B.nextId++), file ? file.name : names[i % names.length], body.rules === '187' ? '187' : 'highest-score', [{ seat: 0, level: 'hard' }, { seat: 2, level: 'hard' }], 'ffa', '2026-11-16T21:20:00Z', { by: 'basil', live: null }));
            return json(200, { started: body.count, asked: body.count });
        }
        if (p === '/admin/api/bot-matches/stop-all') { const n = B.matches.length; B.matches = []; return json(200, { stopped: n }); }
        if ((m = /^\/admin\/api\/bot-matches\/([A-Za-z0-9_-]+)\/stop$/.exec(p))) { B.matches = B.matches.filter((x) => x.id !== m[1]); return json(200, { stopped: 1 }); }
        if (p === '/admin/api/bot-schedules' || (m = /^\/admin\/api\/bot-schedules\/(\d+)$/.exec(p))) {
            const cur = m ? B.schedules.filter((x) => x.id === +m[1])[0] : null;
            if (m && !cur) return json(404, { error: 'gone' });
            if (body.name !== undefined && (typeof body.name !== 'string' || body.name.length < 1 || body.name.length > 40)) return json(400, { error: 'bad_name' });
            if (body.when) {
                if (body.when.kind === 'every' && !(Number.isInteger(+body.when.minutes) && +body.when.minutes >= 5 && +body.when.minutes <= 1440)) return json(400, { error: 'bad_every' });
                if (body.when.kind === 'times' && !body.when.times.length) return json(400, { error: 'no_times' });
            }
            if (cur) Object.assign(cur, body);
            else B.schedules.push(Object.assign({ id: B.nextSched++, default: false, on: true, last: 0, next: at('2026-11-16T22:30:00Z') }, body));
            return json(200, { ok: true });
        }
        if ((m = /^\/admin\/api\/bot-schedules\/(\d+)\/delete$/.exec(p))) { B.schedules = B.schedules.filter((x) => x.id !== +m[1]); return json(200, { ok: true }); }
        return json(404, { error: 'not_found' });
    });
    return true;
}
// who is here: signed out, a player with no role, a moderator, an admin, the owner
const WHO = {
    out: { me: [401, { error: 'not_signed_in' }], you: [200, { login: true, you: null }] },
    player: { me: [403, { error: 'not_allowed' }], you: [200, { login: true, you: { name: 'pebble', mod: false, role: 'none', k: 'x' } }] },
    mod: { me: [403, { error: 'not_allowed' }], you: [200, { login: true, you: { name: 'sage', mod: true, role: 'mod', k: 'x' } }] },
    admin: { me: [200, { name: 'basil', ref: REF.basil, role: 'admin', csrf: 'c'.repeat(32), untrusted: true }], you: [200, { login: true, you: { name: 'basil', mod: true, role: 'admin', k: 'x' } }] },
    owner: { me: [200, { name: 'basil', ref: REF.basil, role: 'owner', csrf: 'c'.repeat(32), untrusted: true }], you: [200, { login: true, you: { name: 'basil', mod: true, role: 'admin', k: 'x' } }] }
};
let W = world();
let who = WHO.out;
let hubRefuses = false;
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon', '.svg': 'image/svg+xml' };
function thumb(res, key) {
    const file = process.env.ADMIN_SHOT_THUMBS ? path.join(process.env.ADMIN_SHOT_THUMBS, 'mapthumb_' + key + '.png') : '';
    if (file && fs.existsSync(file)) { res.writeHead(200, { 'Content-Type': 'image/png' }); res.end(fs.readFileSync(file)); return; }
    const tone = { a: '#7a5b2b', b: '#2f6f5a', c: '#5a3f7a', d: '#7a2f2f' }[key] || '#555';
    res.writeHead(200, { 'Content-Type': 'image/svg+xml' });
    res.end('<svg xmlns="http://www.w3.org/2000/svg" width="62" height="62"><rect width="62" height="62" fill="' + tone + '"/><rect x="8" y="8" width="20" height="14" fill="#c9b27c"/><rect x="34" y="30" width="20" height="22" fill="#222"/></svg>');
}
const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    const p = url.pathname;
    const json = (status, o) => { res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(o)); };
    let m;
    if (p === '/admin') { res.writeHead(200, { 'Content-Type': TYPES['.html'], 'Cache-Control': 'no-store', 'Content-Security-Policy': CSP }); res.end(page()); return; }
    if (p === '/admin/api/me') return json(who.me[0], who.me[1]);
    if (p === '/hub/me') return json(who.you[0], who.you[1]);
    if (p === '/live') return json(200, { live: [] });
    if (p === '/busy') return json(200, W.busy);
    if (p === '/admin/api/audit') return json(200, { entries: W.audit.concat(W.bots.audit).sort((a, b) => b.at - a.at), untrusted: true });
    if (p.indexOf('/admin/api/bot-') === 0 && botApi(req, p, json)) return undefined;
    if (p === '/admin/api/maps') { const st = url.searchParams.get('state') || 'waiting'; return json(200, { state: st, counts: W.counts, maps: MAPS[st] || [] }); }
    if ((m = /^\/admin\/api\/maps\/([a-f]{32})\/picture$/.exec(p))) { const row = [].concat(MAPS.waiting, MAPS.approved).filter((x) => x.id === m[1])[0]; return thumb(res, row ? row.thumb : 'a'); }
    if (p.indexOf('/hub/admin/api/') === 0) {
        if (hubRefuses) return json(403, { error: 'admin' });
        const name = p.slice('/hub/admin/api/'.length);
        if (req.method === 'GET') {
            if (name === 'me') return json(200, { name: 'basil', role: 'admin', csrf: 'd'.repeat(64), untrusted: true });
            if (name === 'bans') return json(200, { bans: W.bans, untrusted: true });
            if (name === 'mutes') return json(200, { mutes: W.mutes, untrusted: true });
            if (name === 'hidden') return json(200, { hidden: W.hidden, untrusted: true });
            if (name === 'modlog') return json(200, { modlog: W.modlog, untrusted: true });
            if (name === 'status') return json(200, W.status);
        } else {
            let body = '';
            req.on('data', (c) => { body += c; });
            req.on('end', () => {
                const names = ['clover.4782', 'hazel_b', 'juniper.berry', 'pebble_9', 'marlow.old', 'spam.king'];
                W.posts.push(name);
                const account = JSON.parse(body || '{}').account;
                if (account && account.length < 20 && names.indexOf(account) < 0 && !/^[0-9a-f]{16}$/.test(account)) return json(404, { error: 'who' });
                return json(200, name === 'mute' ? { ok: true, until: at('2026-11-16T22:20:00Z') } : { ok: true });
            });
            return undefined;
        }
    }
    if (p === '/hub/logout') return json(200, { ok: true });
    const full = path.resolve(ROOT, 'web', decodeURIComponent(p).replace(/^\/+/, ''));
    if (full.indexOf(path.join(ROOT, 'web') + path.sep) !== 0 || !fs.existsSync(full) || !fs.statSync(full).isFile()) { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not here'); return undefined; }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(full)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(fs.readFileSync(full));
    return undefined;
});

// ---- the scenes: a name, who is here, what to do after the page has opened, and whether the picture is the window alone -----------------------------------------------------------------------------------------------
async function openSection(pg, id) {                               // by the value of the select (a phone) or the id of the tab (a desktop)
    if (await pg.locator('.atabs').isVisible()) await pg.click('#tab-' + id);
    else await pg.selectOption('#asec', id);
}
const SCENES = [
    ['1_signed_out', { who: 'out' }, async (pg) => { await pg.waitForSelector('.lcard a'); }],
    ['1b_not_allowed_player', { who: 'player' }, async (pg) => { await pg.waitForSelector('.lcard .l2'); }],
    ['1c_not_allowed_moderator', { who: 'mod' }, async (pg) => { await pg.waitForSelector('.lcard .l2'); }],
    ['2_maps_waiting', { who: 'admin' }, async (pg) => { await pg.waitForSelector('.alist .arow'); }],
    ['2b_maps_approved_by_username', { who: 'admin' }, async (pg) => { await pg.waitForSelector('.alist .arow'); await pg.click('.chip:has-text("Approved")'); await pg.waitForFunction(() => /Mossy/.test(document.querySelector('.alist .arow .t1').textContent)); }],
    ['3_chat', { who: 'admin' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); }],
    ['3b_chat_ban_confirm', { who: 'admin', window: true }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.fill('#cm-user', 'clover.4782'); await pg.click('.acts .btn.bad'); await pg.waitForSelector('.dlg'); }],
    ['3c_chat_refused', { who: 'admin' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.fill('#cm-user', 'clove'); await pg.click('.acts .btn:has-text("Mute")'); await pg.waitForSelector('.panel .msg'); }],
    ['3d_chat_done', { who: 'admin' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.fill('#cm-user', 'hazel_b'); await pg.click('.acts .btn:has-text("Mute")'); await pg.waitForSelector('.panel .msg.ok'); }],
    ['4_log_all', { who: 'admin' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.arow2'); }],
    ['4b_log_chat_actions', { who: 'admin' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.arow2'); await pg.click('.lhead .chip:has-text("Chat actions")'); }],
    ['4c_log_map_decisions', { who: 'admin' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.arow2'); await pg.click('.lhead .chip:has-text("Map decisions")'); }],
    ['5_status_ok', { who: 'admin' }, async (pg) => { await openSection(pg, 'status'); await pg.waitForSelector('.kv'); }],
    ['5b_status_trouble', { who: 'owner', trouble: true }, async (pg) => { await openSection(pg, 'status'); await pg.waitForSelector('.kv'); }],
    ['6_chip_menu_open', { who: 'admin', window: true }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); await pg.click('.acct'); await pg.waitForSelector('.amenu'); }],
    ['9_owner_chat', { who: 'owner' }, async (pg) => { await openSection(pg, 'chat'); await pg.waitForSelector('.p-banned .prow'); }]
];

// ---- the section Bot matches: the scenes of the approved pictures (one subject each: `crop` is the selector of what is cut out of the page, `dialog` the question over it; `setup` changes the made-up world first)
async function openBots(pg) {
    await openSection(pg, 'bots');
    await pg.waitForSelector('.b-now .step');
    await pg.waitForSelector('.b-run .meter');
    await pg.waitForSelector('.b-sched .blist, .b-sched .none');
}
const chipOf = (pg, group, text) => pg.locator('[aria-labelledby="' + group + '-l"] button', { hasText: new RegExp('^' + text + '$') }).first().click();
async function fillStart(pg) {                         // three matches, Fight Island, 187, four bots Hard Hard Medium Easy, a split of 1 and 2 against 3 and 4
    const plus = pg.locator('.b-now .step button').nth(1);
    await plus.click(); await plus.click();
    await pg.selectOption('#now-map', 'Fight Island.lvl');
    await chipOf(pg, 'now-rules', '187');
    await chipOf(pg, 'now-bots', '4');
    await pg.selectOption('#now-lv0', 'hard'); await pg.selectOption('#now-lv1', 'hard'); await pg.selectOption('#now-lv2', 'medium'); await pg.selectOption('#now-lv3', 'easy');
    await chipOf(pg, 'now-teams', 'A split');
    await pg.selectOption('#now-split', '12-34');
}
const BOT_SCENES = [
    ['0_tabs', { who: 'admin', clip: ['.pt.apt', '.atabs', '.asel'] }, async (pg) => { await pg.waitForSelector('.atab .cnt', { state: 'attached' }); await pg.waitForFunction(() => document.querySelectorAll('.atab .cnt').length === 2); }],
    ['1_start_now', { who: 'admin', crop: '.b-now' }, openBots],
    ['1b_start_now_filled_in', { who: 'admin', crop: '.b-now' }, async (pg) => { await openBots(pg); await fillStart(pg); }],
    ['1c_start_now_done', { who: 'admin', crop: '.b-now' }, async (pg) => { await openBots(pg); await pg.locator('.b-now .step button').nth(1).click(); await pg.click('.b-now .startrow .btn'); await pg.waitForSelector('.b-now .msg.ok'); await pg.waitForTimeout(300); }],
    ['1d_start_now_refused_people_busy', { who: 'admin', crop: '.b-now', setup: () => { W.bots.refuse = 'busy'; W.bots.people = 40; } }, async (pg) => { await openBots(pg); await pg.click('.b-now .startrow .btn'); await pg.waitForSelector('.b-now .msg.bad'); }],
    ['1e_start_now_refused_over_the_limit', { who: 'admin', crop: '.b-now' }, async (pg) => { await openBots(pg); const plus = pg.locator('.b-now .step button').nth(1); for (let i = 0; i < 5; i++) await plus.click(); await pg.click('.b-now .startrow .btn'); await pg.waitForSelector('.b-now .msg.bad'); }],
    ['1f_start_now_all_places_taken', { who: 'admin', crop: '.b-now', setup: fullHouse }, async (pg) => { await openBots(pg); await pg.waitForSelector('.b-now .msg.bad'); }],
    ['1g_start_now_islands_in_187', { who: 'admin', crop: '.b-now' }, async (pg) => { await openBots(pg); await pg.selectOption('#now-map', 'ISLANDS.LVL'); await chipOf(pg, 'now-rules', '187'); }],
    ['2_running', { who: 'admin', crop: '.b-run' }, openBots],
    ['2b_running_none', { who: 'admin', crop: '.b-run', setup: () => { W.bots.matches = []; } }, openBots],
    ['2c_running_all_places_taken', { who: 'admin', crop: '.b-run', setup: fullHouse }, openBots],
    ['2d_stop_all_question', { who: 'admin', dialog: true }, async (pg) => { await openBots(pg); await pg.click('.b-run .stopall .btn'); await pg.waitForSelector('.dlg'); await pg.waitForTimeout(150); }],
    ['3_schedules', { who: 'admin', crop: '.b-sched' }, openBots],
    ['3b_schedule_edit_every', { who: 'admin', crop: '.b-sched' }, async (pg) => { await openBots(pg); await pg.locator('.b-sched .brow .bb button', { hasText: 'Edit' }).first().click(); await pg.waitForSelector('.sform'); }],
    ['3c_schedule_new_at_times', { who: 'admin', crop: '.b-sched' }, async (pg) => {
        await openBots(pg);
        await pg.click('.b-sched .sfoot .btn'); await pg.waitForSelector('.sform');
        await pg.fill('#sc-name', 'Weekend swarm');
        await chipOf(pg, 'sc-when', 'At times of the day');
        await pg.fill('#sc-time', '10:00'); await pg.locator('.addtime .btn').click();
        await pg.fill('#sc-time', '14:30'); await pg.locator('.addtime .btn').click();
        await pg.locator('.tm2 button[aria-label="Remove 18:00"]').click();
        const plus = pg.locator('.sform .step button').nth(1);
        await plus.click(); await plus.click(); await plus.click();
        await pg.selectOption('#sc-map', 'random:players');
        await chipOf(pg, 'sc-rules', '187');
        await chipOf(pg, 'sc-bots', '3');
        await pg.selectOption('#sc-lv0', 'hard'); await pg.selectOption('#sc-lv1', 'hard'); await pg.selectOption('#sc-lv2', 'medium');
        await chipOf(pg, 'sc-teams', 'A split');
        await pg.selectOption('#sc-split', '3-12');
    }],
    ['3d_schedule_refused_by_the_form', { who: 'admin', crop: '.b-sched' }, async (pg) => { await openBots(pg); await pg.click('.b-sched .sfoot .btn'); await pg.waitForSelector('.sform'); await pg.fill('#sc-min', '3'); await pg.locator('.sform .sfoot .btn', { hasText: 'Save' }).click(); await pg.waitForSelector('.sform .msg.bad'); }],
    ['3e_schedule_delete_question', { who: 'admin', dialog: true }, async (pg) => { await openBots(pg); await pg.locator('.b-sched .brow .bb button', { hasText: 'Delete' }).first().click(); await pg.waitForSelector('.dlg'); await pg.waitForTimeout(150); }],
    ['4_log_bot_matches', { who: 'admin', crop: '#secpanel' }, async (pg) => { await openSection(pg, 'log'); await pg.waitForSelector('.alist2 .arow2'); await pg.locator('.lhead .chip', { hasText: 'Bot matches' }).click(); await pg.waitForTimeout(150); }]
];
// one subject cut out of the page: a desktop at 1x and a phone at 2x (never wider than 1000 px); the question is the window of a phone and its own box on a desktop
async function shoot(pg, spec, file, sizeName) {
    const phone = sizeName === 'phone';
    if (spec.dialog) {
        if (phone) { await pg.screenshot({ path: file }); return; }
        const r = await pg.evaluate(() => { const b = document.querySelector('.dlg').getBoundingClientRect(); return { x: b.left, y: b.top, w: b.width, h: b.height }; });
        await pg.screenshot({ path: file, clip: { x: Math.max(0, r.x - 40), y: Math.max(0, r.y - 40), width: r.w + 80, height: r.h + 80 } });
        return;
    }
    const r = await pg.evaluate(({ sels, vw }) => {
        let x0 = 1e9, y0 = 1e9, x1 = 0, y1 = 0;
        sels.forEach((s) => { const n = document.querySelector(s); if (!n) return; const b = n.getBoundingClientRect(); if (b.width === 0) return; x0 = Math.min(x0, b.left + scrollX); y0 = Math.min(y0, b.top + scrollY); x1 = Math.max(x1, b.right + scrollX); y1 = Math.max(y1, b.bottom + scrollY); });
        return { x0, y0, x1, y1, vw };
    }, { sels: spec.clip || [spec.crop], vw: await pg.evaluate(() => innerWidth) });
    const x0 = phone ? 0 : Math.max(0, r.x0 - 14), x1 = phone ? r.vw : Math.min(r.vw, r.x1 + 18);
    await pg.screenshot({ path: file, fullPage: true, clip: { x: x0, y: Math.max(0, r.y0 - 12), width: x1 - x0, height: r.y1 - r.y0 + 30 } });
}

async function checks_of(pg, label, size) {
    const out = await pg.evaluate(({ w }) => {
        const found = [];
        const de = document.documentElement;
        if (de.scrollWidth > w + 1) found.push('sideways scroll: ' + de.scrollWidth + ' > ' + w);
        const bad = [];
        document.querySelectorAll('button, a.btn, a[href], input, select').forEach((n) => {
            const r = n.getBoundingClientRect();
            if (r.width === 0 || r.height === 0) return;
            const cs = getComputedStyle(n);
            if (cs.visibility === 'hidden' || cs.display === 'none') return;
            if (n.closest('[hidden], template, .sbm')) return;
            if (r.height < 43.5 || r.width < 43.5) bad.push((n.textContent || n.id || n.tagName).trim().slice(0, 24) + ' ' + Math.round(r.width) + 'x' + Math.round(r.height));
            if (r.right > w + 1 || r.left < -1) bad.push('outside: ' + (n.textContent || n.id).trim().slice(0, 24));
        });
        if (bad.length) found.push('controls: ' + bad.join('; '));
        const row = document.querySelector('.atabs'), sel = document.querySelector('.asel');
        if (row && sel) {
            const rowShown = getComputedStyle(row).display !== 'none', selShown = getComputedStyle(sel).display !== 'none';
            if (rowShown === selShown) found.push('the tabs and the select are shown ' + (rowShown ? 'both' : 'neither'));
            if (rowShown !== (w > 600)) found.push('the row of tabs is ' + (rowShown ? 'shown' : 'hidden') + ' at ' + w);
        }
        return found;
    }, { w: size.width });
    check(label, out.length === 0, out.join(' | '));
    return out.length;
}

(async () => {
    if (SHOTS) fs.mkdirSync(SHOTS, { recursive: true });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const base = 'http://127.0.0.1:' + server.address().port;
    const launch = { executablePath: process.env.CHROME || undefined };
    if (process.getuid && process.getuid() === 0) launch.args = ['--no-sandbox'];
    let browser;
    try { browser = await playwright.chromium.launch(launch); } catch (e) { console.log('SKIP: no browser could be started (' + String(e.message).split('\n')[0] + '): the admin page was NOT run in a browser'); server.close(); process.exit(0); }
    for (const [name, spec, steps] of SCENES.concat(BOT_SCENES)) {
        if (ONLY && name.indexOf(ONLY) !== 0) continue;
        for (const sizeName of SHOTS ? ['phone', 'desktop'] : ['narrow', 'phone', 'desktop']) {
            const size = spec.crop || spec.clip || spec.dialog ? Object.assign({}, SIZES[sizeName], sizeName === 'desktop' ? { dsf: 1 } : {}) : SIZES[sizeName];       // (a subject cut out of the page: a desktop at 1x)
            W = world();
            who = WHO[spec.who];
            hubRefuses = false;
            if (spec.setup) spec.setup();
            if (spec.trouble) { W.status.health = { roles: 'refused', roles_known: 0, kind: 'token', status: 401, retry: 600 }; W.status.last_picture = { kind: 'down', age: 120 }; }
            const ctx = await browser.newContext({ viewport: { width: size.width, height: size.height }, deviceScaleFactor: size.dsf, isMobile: size.mobile, hasTouch: size.mobile, colorScheme: 'dark' });
            await ctx.addInitScript((now) => { Date.now = () => now; }, NOW_MS);
            const pg = await ctx.newPage();
            const errors = [];
            pg.on('pageerror', (e) => errors.push(String(e)));
            pg.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) errors.push(m.text()); });
            await pg.goto(base + '/admin', { waitUntil: 'load' });
            try { await steps(pg); } catch (e) { check(name + '_' + sizeName + ': the scene could not be drawn', false, String(e.message).split('\n')[0]); await ctx.close(); continue; }
            await pg.evaluate(() => document.fonts.ready);
            await pg.waitForTimeout(250);
            await checks_of(pg, name + '_' + sizeName, size);
            check(name + '_' + sizeName + ': no error of the script and no refusal of the policy', errors.length === 0, errors.join(' | '));
            if (SHOTS) {
                if (spec.crop || spec.clip || spec.dialog) await shoot(pg, spec, path.join(SHOTS, name + '_' + sizeName + '.png'), sizeName);
                else await pg.screenshot({ path: path.join(SHOTS, name + '_' + sizeName + '.png'), fullPage: !spec.window });
            }
            await ctx.close();
        }
    }
    {   // a real double click on the form's Ban: the second click lands on the question's own Ban, and the question must not take it for an answer
        W = world();
        who = WHO.admin;
        const ctx = await browser.newContext({ viewport: { width: 390, height: 600 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true, colorScheme: 'dark' });
        const pg = await ctx.newPage();
        const errors = [];
        pg.on('pageerror', (e) => errors.push(String(e)));
        await pg.goto(base + '/admin', { waitUntil: 'load' });
        await pg.waitForSelector('.alist .arow');
        await openSection(pg, 'chat');
        await pg.waitForSelector('.p-banned .prow');
        await pg.fill('#cm-user', 'clover.4782');
        await pg.click('.acts .btn.bad');                                                      // (once, to see where the question's Ban stands)
        await pg.waitForSelector('.dlg');
        const sheet = await pg.locator('.dlg-foot .btn.bad').boundingBox();
        await pg.keyboard.press('Escape');
        await pg.waitForSelector('.dlg', { state: 'detached' });
        const form = await pg.locator('.acts .btn.bad').boundingBox();
        await pg.evaluate((dy) => window.scrollBy(0, dy), form.y + form.height / 2 - (sheet.y + sheet.height / 2));      // (the form's Ban at the height of the question's)
        const at = await pg.locator('.acts .btn.bad').boundingBox();
        const x = Math.round(at.x + at.width / 2), y = Math.round(at.y + at.height / 2);
        await pg.mouse.dblclick(x, y);
        await pg.waitForTimeout(100);
        const over = await pg.evaluate(([px, py]) => { const e = document.elementFromPoint(px, py); return e && e.closest('.dlg-foot') ? e.textContent : null; }, [x, y]);
        check('the second click of a double click lands on the Ban of the question (else this check shows nothing)', over === 'Ban', String(over));
        check('and it banned nobody: the question is open and nothing was sent', (await pg.locator('.dlg').count()) === 1 && W.posts.length === 0, JSON.stringify(W.posts));
        if ((await pg.locator('.dlg').count()) === 1) {
            await pg.waitForTimeout(700);
            await pg.locator('.dlg-foot .btn.bad').click();
            await pg.waitForTimeout(200);
            check('a press on it a moment later is the answer: the ban is sent, once', W.posts.length === 1 && W.posts[0] === 'ban', JSON.stringify(W.posts));
        }
        check('no error of the script', errors.length === 0, errors.join(' | '));
        await ctx.close();
    }
    {   // a name that is markup is text on every section: no tag of it is in the page
        const evil = '<img src=x onerror=alert(1)>';
        W = world();
        W.bans[0].name = evil;
        W.modlog[0].actor_name = evil;
        W.audit[0].target = evil;
        who = WHO.admin;
        const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
        const pg = await ctx.newPage();
        const errors = [];
        pg.on('pageerror', (e) => errors.push(String(e)));
        await pg.goto(base + '/admin', { waitUntil: 'load' });
        await pg.waitForSelector('.alist .arow');
        for (const id of ['chat', 'log', 'status']) { await openSection(pg, id); await pg.waitForTimeout(300); }
        await openSection(pg, 'chat');
        await pg.waitForSelector('.p-banned .prow');
        check('a name that is markup is shown as text in the Chat list', (await pg.locator('.p-banned .pn').first().textContent()) === evil);
        check('and no node of it is in the page', (await pg.locator('#app img').count()) === (await pg.locator('.arow img').count()));
        await openSection(pg, 'log');
        await pg.waitForSelector('.arow2');
        check('a name that is markup is shown as text in the Log', (await pg.locator('.arow2 .wh').allTextContents()).indexOf(evil) >= 0);
        check('no script ran', errors.length === 0, errors.join(' | '));
        await ctx.close();
    }
    await browser.close();
    server.close();
    console.log(checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });

// Hands a lobby over to real games, as the lobby page will: two pages (web/front/lobby_net.js, LobbyClient, over a WebSocket) in a lobby room of a REAL ants_server, the leader's plan for the two of them and START;
// the START waits for the games (network protocol 16), the pages go, and a native game of each name takes its seat over with the key that its page was given (the game finds the key in the file of its rejoin
// store, as the game page finds it in the browser's local storage); when both are games the match loads, starts and runs. The page goes the way a browser lets it go in either order:
//   gone       the page is gone before the game comes (a tab that was closed or a page that was left): its seat is held, and the game's Hello with the key takes it back;
//   superseded the page is still connected when the game comes: the game's Hello supersedes it, and the page is told so (its client stops and does not ask for the seat again).
// usage: ANTS_E2E_SECRET=... [ANTS_HANDOFF_WAIT_MS=60000] node web_lobby_handoff_check.js web/front/lobby_net.js ws://127.0.0.1:WSPORT/ws http://127.0.0.1:CTLPORT 127.0.0.1:GAMEPORT build/src/ants_app/ants REPO_ROOT WORK_FOLDER
//        (the server: --demo-rooms N --demo-map TINY.LVL --demo-lobbies M and the control interface on CTLPORT with that secret)      exit 0: every check holds; the failures are printed
'use strict';
const path = require('path');
const fs = require('fs');
const http = require('http');
const { spawn } = require('child_process');
const MiniWebSocket = require('./mini_websocket.js');

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
function until(label, fn, ms) {
    return new Promise((resolve, reject) => {
        const t0 = Date.now();
        const tick = () => {
            let v;
            try { v = fn(); } catch (e) { reject(e); return; }
            if (v) { resolve(v); return; }
            if (Date.now() - t0 > (ms || 10000)) { reject(new Error('timeout: ' + label)); return; }
            setTimeout(tick, 20);
        };
        tick();
    });
}

// ---- the pages: a process of their own, so that a page can go the way a page goes (killed, with no word to the server) --------------------------------------------------------------------------------
async function pages(modulePath, wsUrl, code) {
    const N = require(path.resolve(modulePath));
    const make = (name) => new N.LobbyClient({ url: wsUrl, code, map: 'TINY.LVL', name, WebSocket: MiniWebSocket });
    const A = make('Ada');
    const B = make('Bea');
    for (const [who, c] of [['A', A], ['B', B]]) c.on('status', (s) => console.log(who + ' status ' + s));
    A.connect();
    await until('Ada online', () => A.status === 'online' && A.room !== null);
    B.connect();
    await until('Bea online', () => B.status === 'online' && B.room !== null && A.room.slots[1].state === N.SLOT.Client);
    A.setPlan({ map: '', kinds: [N.PLAN.Open, N.PLAN.Open, N.PLAN.Nobody, N.PLAN.Nobody], teamA: 255, teamB: 255 });      // (two persons and nobody else: no bot to wait for)
    await until('the plan', () => B.room.plan[2] === N.PLAN.Nobody && B.room.plan[3] === N.PLAN.Nobody);
    if (!A.start()) throw new Error('the leader could not ask for START');
    await until('START waits', () => A.room.starting && B.room.starting);
    console.log('READY ' + JSON.stringify({ a: { seat: A.seat, key: N.hexOf(A.key) }, b: { seat: B.seat, key: N.hexOf(B.key) } }));
    await sleep(600000);
}

if (process.argv[2] === '--pages') {
    pages(process.argv[3], process.argv[4], process.argv[5]).catch((e) => { console.log('PAGES FAILED ' + (e && e.message)); process.exit(3); });
} else {
    main().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });
}

// ---- the check ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
async function main() {
    const [modulePath, wsUrl, ctlBase, gameAddress, gameBinary, root, work] = process.argv.slice(2);
    const secret = process.env.ANTS_E2E_SECRET || '';
    const waitMs = Number(process.env.ANTS_HANDOFF_WAIT_MS) || 60000;                       // (how long a match may take to begin: a sanitized build is slower)
    if (!modulePath || !wsUrl || !ctlBase || !gameAddress || !gameBinary || !root || !work) {
        console.log('usage: ANTS_E2E_SECRET=... web_lobby_handoff_check.js lobby_net.js ws://host:port/ws http://host:ctlport host:gameport game-binary repo-root work-folder');
        process.exit(2);
    }
    let checks = 0;
    let failures = 0;
    const check = (label, ok, detail) => {
        checks++;
        if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
    };
    const room = (code) => new Promise((resolve) => {
        const req = http.get(ctlBase + '/rooms/' + code, { headers: { Authorization: 'Bearer ' + secret }, timeout: 2000 }, (res) => {
            let body = '';
            res.setEncoding('utf8');
            res.on('data', (d) => { body += d; });
            res.on('end', () => { try { resolve(JSON.parse(body)); } catch (e) { resolve(null); } });
        });
        req.on('error', () => resolve(null));
        req.on('timeout', () => { req.destroy(); resolve(null); });
    });
    const spawned = [];
    const stopAll = () => { for (const p of spawned) { try { p.kill('SIGKILL'); } catch (e) { /* gone already */ } } };
    process.on('exit', stopAll);
    for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => { stopAll(); process.exit(1); });
    setTimeout(() => { console.log('FAIL the check ran out of time'); stopAll(); process.exit(1); }, Math.max(240000, 6 * waitMs)).unref();      // (no game and no page outlives a check that hangs)

    const RUN = Math.random().toString(36).slice(2, 4).padEnd(2, '0');
    async function scenario(title, pagesGoFirst) {
        const code = 'ho' + RUN + title.slice(0, 2);
        const folder = path.join(work, 'handoff_' + title);
        fs.mkdirSync(folder, { recursive: true });
        // the pages
        const pagesLog = [];
        const page = spawn(process.execPath, [__filename, '--pages', modulePath, wsUrl, code], { stdio: ['ignore', 'pipe', 'inherit'] });
        spawned.push(page);
        let keys = null;
        let carry = '';
        page.stdout.setEncoding('utf8');
        page.stdout.on('data', (d) => {
            carry += d;
            let at;
            while ((at = carry.indexOf('\n')) >= 0) {
                const line = carry.slice(0, at).trim();
                carry = carry.slice(at + 1);
                if (line.startsWith('READY ')) keys = JSON.parse(line.slice(6));
                else if (line) pagesLog.push(line);
            }
        });
        const pageExit = new Promise((resolve) => page.on('exit', () => resolve()));
        try { await until(title + ': the pages are in the lobby and START waits', () => keys !== null || pagesLog.some((l) => l.startsWith('PAGES FAILED')), 20000); } catch (e) { check(title + ': the pages are in the lobby and START waits', false, e.message); }
        if (keys === null) { check(title + ': the pages are in the lobby and START waits', false, pagesLog.join(' | ')); page.kill('SIGKILL'); return; }
        check(title + ': the pages are in the lobby and START waits (the leader asked, both pages see it)', true);
        const waiting = await room(code);
        check(title + ': the server says the lobby is waiting with START asked for and two people in it', waiting !== null && waiting.state === 'waiting' && waiting.starting === true && waiting.joined === 2 && waiting.lobby === true, JSON.stringify(waiting));
        if (pagesGoFirst) {
            page.kill('SIGKILL');
            await pageExit;
        }
        // the games, each with the key of its page in a rejoin file beside the settings file that it is given (a headless run keeps no file of its own)
        const games = [];
        for (const who of ['a', 'b']) {
            const dir = path.join(folder, who);
            fs.mkdirSync(dir, { recursive: true });
            fs.writeFileSync(path.join(dir, 'rejoin.txt'), [gameAddress, code, String(keys[who].seat), keys[who].key, String(Math.floor(Date.now() / 1000))].join('\t') + '\n', { mode: 0o600 });
            const log = fs.openSync(path.join(dir, 'game.log'), 'w');
            const name = who === 'a' ? 'Ada' : 'Bea';
            const g = spawn(gameBinary, ['--headless', '--no-lan', '--settings', path.join(dir, 'settings.cfg'), '--name', name, '--join', gameAddress, '--room', code, '--seat', String(keys[who].seat),
                '--screenshot', path.join(dir, 'end.png'), '--frames', '4000000'], { cwd: root, stdio: ['ignore', log, log] });
            spawned.push(g);
            const entry = { name, dir, process: g, exited: false };
            g.on('exit', () => { entry.exited = true; });
            games.push(entry);
        }
        let running = null;
        try {
            running = await new Promise((resolve, reject) => {
                const t0 = Date.now();
                const poll = async () => {
                    const r = await room(code);
                    if (r !== null && r.state === 'running') { resolve(r); return; }
                    if (Date.now() - t0 > waitMs) { reject(new Error('the room never ran: ' + JSON.stringify(r))); return; }
                    setTimeout(poll, 150);
                };
                poll();
            });
        } catch (e) { check(title + ': both games took their seats over and the match started', false, e.message); }
        if (running !== null) {
            check(title + ': both games took their seats over and the match started (state running, START no longer waits)', running.starting === false && running.joined === 2);
            check(title + ': the two people are Ada and Bea, in the seats that their pages had', JSON.stringify(running.players.map((p) => [p.seat, p.name])) === JSON.stringify([[keys.a.seat, 'Ada'], [keys.b.seat, 'Bea']]), JSON.stringify(running.players));
            let ticks = 0;
            try { ticks = await new Promise((resolve, reject) => { const t0 = Date.now(); const poll = async () => { const r = await room(code); if (r !== null && r.ticks > 0) { resolve(r.ticks); return; } if (Date.now() - t0 > waitMs) { reject(new Error('no tick')); return; } setTimeout(poll, 150); }; poll(); }); } catch (e) { /* ticks stays 0 */ }
            check(title + ': the referee\'s clock runs (both games loaded the map and report their state: ticks ' + ticks + ')', ticks > 0);
            await sleep(1500);                                                      // (a desync of the two games would be reported within a second or two)
            const later = await room(code);
            check(title + ': the match goes on with both games in it', later !== null && later.state === 'running' && later.ticks > ticks && games.every((g) => !g.exited), JSON.stringify(later && { state: later.state, ticks: later.ticks, reason: later.reason }));
        }
        if (!pagesGoFirst) {
            try { await until(title + ': the pages are told that another window has the seat', () => pagesLog.filter((l) => l === 'A status superseded' || l === 'B status superseded').length === 2, 10000); check(title + ': both pages are told that the seat was taken by another window (superseded)', true); }
            catch (e) { check(title + ': both pages are told that the seat was taken by another window (superseded)', false, pagesLog.join(' | ')); }
            check(title + ': a superseded page does not ask for the seat again (no second connecting after it)', !pagesLog.some((l, i) => /status superseded$/.test(l) && pagesLog.slice(i + 1).some((m) => /status (connecting|online)$/.test(m))), pagesLog.join(' | '));
            page.kill('SIGKILL');
            await pageExit;
        }
        for (const g of games) g.process.kill('SIGTERM');
        await Promise.race([Promise.all(games.map((g) => new Promise((resolve) => (g.exited ? resolve() : g.process.on('exit', () => resolve()))))), sleep(8000)]);
        for (const g of games) g.process.kill('SIGKILL');
        const logs = games.map((g) => fs.readFileSync(path.join(g.dir, 'game.log'), 'utf8')).join('\n');
        check(title + ': neither game reported an error', !/out of sync|failed|error|rejected|could not/i.test(logs), logs.split('\n').filter((l) => /out of sync|failed|error|rejected|could not/i.test(l)).slice(0, 3).join(' | '));
    }

    await scenario('gone', true);
    await scenario('superseded', false);
    console.log('handoff to real games: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
}

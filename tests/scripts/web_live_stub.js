// A stand-in for the game's program (index.js) in tests/scripts/web_live_check.py: the page's glue only knows the game through Module._ants_replay_get / _ants_replay_do (the values and the
// controls of src/ants_app/application.cpp: ReplayValue Live=7, Limit=8, Complete=9; ReplayControl Extend=5, LiveOver=6), the file system that it writes the recording to, and the report that the
// game makes when it has read the file. The file here is "ANTSFAKE <turns> <complete> <id>\n...": the stub plays turns at 20 a second to the Limit (the whole file, or 40 turns short of it
// while it follows a file that grows) and keeps what the glue did in window.__stub for the check to read.
(function () {
    'use strict';
    var M = window.Module;
    var files = {};
    M.FS = { writeFile: function (p, b) { files[p] = new Uint8Array(b); }, readFile: function (p) { return files[p]; } };
    var STATE = { playing: 2, paused: 3, jumping: 4, ended: 5, cut: 6, failed: 7 };
    var g = { state: STATE.playing, turn: 0, total: 0, speed: 100, jumpPct: 0, target: 0, live: 0, liveOver: 0, complete: 0, id: '', args: [], extends: 0, ignored: 0, seeks: [], jumpUntil: 0, prior: STATE.playing };
    window.__stub = g;
    function parse(bytes) {
        if (!bytes) return null;
        var m = /^ANTSFAKE (\d+) (\d)\s+(\S+)/.exec(new TextDecoder().decode(bytes.subarray(0, 80)));
        return m ? { turns: +m[1], complete: +m[2], id: m[3] } : null;
    }
    function limit() { return g.live ? Math.max(0, g.total - 40) : g.total; }
    M._ants_replay_get = function (what) {
        switch (what) {
            case 0: return g.state;
            case 1: return Math.floor(g.turn);
            case 2: return g.total;
            case 3: return g.speed;
            case 4: return g.jumpPct;
            case 5: return 0;
            case 6: return g.target;
            case 7: return g.live;
            case 8: return limit();
            case 9: return g.complete;
        }
        return -1;
    };
    M._ants_replay_do = function (what, value) {
        switch (what) {
            case 0: if (g.state === STATE.playing) g.state = STATE.paused; else if (g.state === STATE.paused) g.state = STATE.playing; break;
            case 1: g.state = value ? STATE.paused : STATE.playing; break;
            case 2: g.speed = value; break;
            case 3:                                                                                            // a seek past the Limit is the game's to clamp
                g.seeks.push(value);
                g.target = Math.max(0, Math.min(value, limit()));
                g.prior = g.state === STATE.jumping ? g.prior : g.state;
                g.state = STATE.jumping; g.jumpPct = 0; g.jumpUntil = performance.now() + 400;
                break;
            case 4: g.turn = 0; g.state = STATE.playing; break;
            case 5: {                                                                                          // Extend: the file in the file system, if it is the same match with more turns
                g.extends++;
                var f = parse(files['/replay.antsrep']);
                if (f && f.id === g.id && f.turns >= g.total) {
                    g.total = f.turns;
                    if (f.complete) { g.complete = 1; g.live = 0; }
                } else g.ignored++;
                break;
            }
            case 6: g.liveOver = 1; g.live = 0; break;
        }
    };
    var last = performance.now();
    var canvas = document.getElementById('canvas');
    function draw() {
        var ctx = canvas.getContext('2d');
        if (!ctx) return;
        canvas.width = 960; canvas.height = 540;
        ctx.fillStyle = g.state === STATE.ended ? '#223' : '#3b5a3b';
        ctx.fillRect(0, 0, 960, 540);
    }
    function frame() {
        var now = performance.now(), dt = (now - last) / 1000;
        last = now;
        if (g.state === STATE.jumping) {
            g.jumpPct = Math.min(100, (1 - (g.jumpUntil - now) / 400) * 100);
            if (now >= g.jumpUntil) { g.turn = g.target; g.state = g.prior === STATE.paused ? STATE.paused : STATE.playing; g.jumpPct = 0; }
        } else if (g.state === STATE.playing) {
            g.turn = Math.min(limit(), g.turn + dt * 20 * (g.speed / 100));
            if (!g.live && g.turn >= g.total) g.state = g.complete ? STATE.ended : STATE.cut;                  // the end of a file that cannot grow
        }
        draw();
        requestAnimationFrame(frame);
    }
    (M.preRun || []).forEach(function (f) { f(); });                                                           // (the page writes the recording in its preRun)
    g.args = (M.arguments || []).slice();
    var f = parse(files['/replay.antsrep']);
    if (!f) g.state = STATE.failed;
    else { g.total = f.turns; g.complete = f.complete; g.id = f.id; g.live = (g.args.indexOf('--replay-live') !== -1 && !f.complete) ? 1 : 0; }
    setTimeout(function () {
        (M.postRun || []).forEach(function (f) { f(); });
        if (typeof window.antsReplayReport === 'function') {
            window.antsReplayReport({ map: 'TREASURE.LVL', roster: 15, names: ['Mika', 'Bot (Medium)', 'Bot (Medium)', 'Bot (Medium)'], turns: g.total, over: !!g.complete, version: '0.11.0', game: 'v0.11.0', sim_rules: 1 });
        }
        requestAnimationFrame(frame);
    }, 200);
})();

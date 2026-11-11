// Runs the state machine that hides the replay bar of a fullscreen page (web/replay_page.js barZone; docs/REPLAYS.md "Hiding the bar") on a fake clock:
//   open (as ever), hidden (the viewer's choice: no move, click, key or wheel brings the bar back), peek (the pointer rests in the band along the picture's bottom for 150 ms; it goes back when the
//   pointer has been away from the band and the bar for 700 ms, a button down or a busy bar counts as being on it).
// usage: node web_replay_bar_check.js web/replay_page.js    (exit 0: every check holds; every failure is printed)
'use strict';
const path = process.argv[2];
if (!path) { console.log('usage: web_replay_bar_check.js replay_page.js'); process.exit(2); }
const R = require(require('path').resolve(path));
let checks = 0, failures = 0;
function check(label, ok, detail) { checks++; if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); } }

function rig(opts) {
    opts = opts || {};
    const t = { now: 0, timers: [], changes: [], busy: false, bar: { left: 200, right: 1080, top: 640, bottom: 784 }, zone: { left: 0, right: 1280, bottom: 800 } };
    const env = {
        setTimeout: (fn, ms) => { const id = t.timers.length + 1; t.timers.push({ id, at: t.now + ms, fn, live: true }); return id; },
        clearTimeout: (id) => { const x = t.timers.find((q) => q.id === id); if (x) x.live = false; },
        zone: () => t.zone, bar: () => t.bar, busy: () => t.busy, change: (m) => t.changes.push(m),
    };
    t.z = R.barZone(env);
    t.wait = (ms) => {
        const end = t.now + ms;
        for (;;) {
            const due = t.timers.filter((q) => q.live && q.at <= end).sort((a, b) => a.at - b.at)[0];
            if (!due) break;
            t.now = due.at; due.live = false; due.fn();
        }
        t.now = end;
    };
    return t;
}

check('the numbers: a 48 px band, 150 ms to rest, 700 ms to leave', R.ZONE_PX === 48 && R.ZONE_DWELL_MS === 150 && R.ZONE_LEAVE_MS === 700);
{
    const t = rig();
    check('it starts open', t.z.mode() === 'open');
    t.z.pointer(640, 790, 0); t.wait(1000);
    check('an open bar is not changed by a pointer at the bottom', t.z.mode() === 'open' && t.changes.length === 0);
    t.z.hide();
    check('hide: hidden, and told once', t.z.mode() === 'hidden' && t.changes.join() === 'hidden');
    t.z.hide();
    check('hide twice tells nothing more', t.changes.length === 1);
}
{
    const t = rig(); t.z.hide();
    for (const [x, y] of [[640, 400], [10, 300], [1279, 300], [640, 1], [640, 751], [5000, 790], [-5, 790]]) { t.z.pointer(x, y, 0); t.wait(2000); }
    check('moves on the map, at the other edges, above the band (y 751) and outside the picture do not open it', t.z.mode() === 'hidden');
    t.z.pointer(640, 752, 0); t.wait(149);
    check('inside the band for 149 ms: not yet', t.z.mode() === 'hidden');
    t.wait(2);
    check('150 ms: peek', t.z.mode() === 'peek');
}
{
    const t = rig(); t.z.hide();
    t.z.pointer(640, 790, 0); t.wait(100); t.z.pointer(640, 300, 0); t.wait(1000);
    check('a pass shorter than 150 ms opens nothing', t.z.mode() === 'hidden');
    t.z.pointer(640, 790, 0); t.wait(100); t.z.pointer(700, 795, 0); t.wait(100); t.z.pointer(760, 790, 0); t.wait(100);
    check('moving inside the band keeps the 150 ms running (it does not start again)', t.z.mode() === 'peek');
}
{
    const t = rig(); t.z.hide();
    t.z.pointer(400, 500, 1); t.z.pointer(400, 795, 1); t.wait(2000); t.z.pointer(900, 795, 1); t.wait(2000);
    check('a drag along the bottom edge (a button down) opens nothing', t.z.mode() === 'hidden');
    t.z.pointer(900, 795, 0); t.wait(160);
    check('the button let go and the pointer at rest in the band: it opens', t.z.mode() === 'peek');
    const u = rig(); u.z.hide(); u.z.pointer(400, 795, 0); u.wait(100); u.z.pointer(400, 795, 1); u.wait(200);
    check('a button pressed during the 150 ms cancels it', u.z.mode() === 'hidden');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(200);
    t.z.pointer(640, 300, 0); t.wait(699);
    check('peek: the pointer away for 699 ms: still there', t.z.mode() === 'peek');
    t.wait(2);
    check('peek: 700 ms away: hidden again', t.z.mode() === 'hidden' && t.changes.join() === 'hidden,peek,hidden');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(200);
    t.z.pointer(640, 300, 0); t.wait(500); t.z.pointer(640, 700, 0); t.wait(2000);
    check('peek: back on the bar within 700 ms (the bar is above the band): it stays', t.z.mode() === 'peek');
    t.z.pointer(640, 400, 0); t.wait(600); t.z.pointer(300, 790, 0); t.wait(2000);
    check('peek: back in the band within 700 ms: it stays', t.z.mode() === 'peek');
    t.z.pointer(640, 400, 1); t.wait(2000);
    check('peek: a button down (a drag of the timeline that left the bar) keeps it', t.z.mode() === 'peek');
    t.z.pointer(640, 400, 0); t.wait(701);
    check('peek: the button let go away from the bar: it goes after 700 ms', t.z.mode() === 'hidden');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(200); t.busy = true;
    t.z.pointer(640, 300, 0); t.wait(3000);
    check('peek: a busy bar (a drag, the Picture window) stays', t.z.mode() === 'peek');
    t.busy = false; t.wait(701);
    check('peek: and goes 700 ms after it is not busy (the check on the timer)', t.z.mode() === 'hidden');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(200);
    t.z.gone(); t.wait(701);
    check('peek: the pointer leaving the page counts as away', t.z.mode() === 'hidden');
    const u = rig(); u.z.hide(); u.z.pointer(640, 790, 0); u.wait(100); u.z.gone(); u.wait(1000);
    check('hidden: the pointer leaving the page cancels the 150 ms', u.z.mode() === 'hidden');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(200);
    t.z.open();
    check('open from a peek: pinned open', t.z.mode() === 'open');
    t.z.pointer(640, 300, 0); t.wait(5000);
    check('open: no timer takes it back', t.z.mode() === 'open');
    t.z.hide(); t.z.open();
    check('open from hidden', t.z.mode() === 'open' && t.changes.slice(-2).join() === 'hidden,open');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(200); t.z.settle();
    check('settle: a peek goes back to hidden (fullscreen ended)', t.z.mode() === 'hidden');
    t.wait(5000);
    check('settle: and no timer is left to open it', t.z.mode() === 'hidden');
    const u = rig(); u.z.settle(); check('settle: an open bar stays open', u.z.mode() === 'open');
    const v = rig(); v.z.hide(); v.z.settle(); check('settle: a hidden bar stays hidden', v.z.mode() === 'hidden');
}
{
    const t = rig(); t.z.hide(); t.z.pointer(640, 790, 0); t.wait(100); t.z.open(); t.wait(2000);
    check('a press of the handle during the 150 ms: open stays open (the timer is cancelled)', t.z.mode() === 'open');
    t.z.hide(); t.z.pointer(640, 790, 0); t.wait(100); t.z.hide(); t.wait(2000);
    check('hide during the 150 ms: hidden stays hidden', t.z.mode() === 'hidden');
    const u = rig(); u.z.hide(); u.zone = u.zone; u.zone.bottom = 400; u.z.pointer(640, 360, 0); u.wait(200);
    check('the band follows the picture\'s bottom (a picture that ends at 400)', u.z.mode() === 'peek');
    const w = rig(); w.z.hide(); w.zone.left = 100; w.zone.right = 900; w.z.pointer(50, 790, 0); w.wait(500);
    check('beside the picture: nothing', w.z.mode() === 'hidden');
}
console.log(checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);

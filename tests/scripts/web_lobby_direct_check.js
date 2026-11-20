// Runs the Room's "Direct connection" switch (web/front/lobby_direct.js, protocol 26, docs/P2P_HINTS.md) in node with a small fake DOM and a fake storage, and checks:
//   * off by default: a fresh browser has the switch off and writes nothing; the control says in plain words what it is and what the risk is (the IP address), and uses none of the network's words;
//   * the first time it is turned on the explanation comes and the switch does NOT change until the answer: Keep it off (or Escape) leaves it off and writes nothing; Turn it on keeps the choice;
//     once it was explained it is one press each way, and turning it off never asks;
//   * the choice is what the game page reads (`ants.direct`, '1' or '0'), read back after it is written; a browser that will not keep it has a switch that cannot be pressed and says that the relay stays;
//   * a browser with no WebRTC has no switch at all.
// tests/scripts/test_web_lobby_direct.py runs this with node (the quick tier). usage: node web_lobby_direct_check.js web/front/lobby_direct.js        (exit 0: every check passed)
'use strict';
const path = require('path');
const SCRIPT = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'front', 'lobby_direct.js'));
const D = require(SCRIPT);

let failed = 0, checks = 0;
function expect(label, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { console.log('FAIL ' + label + ': ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
}

class El {
    constructor(tag) { this.tag = tag; this.children = []; this.attrs = {}; this.listeners = {}; this.className = ''; this._text = ''; this.hidden = false; this.disabled = false; this.id = ''; this.focused = false; this.type = ''; }
    set textContent(t) { this._text = String(t); this.children = []; }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    appendChild(c) { this.children.push(c); return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
    focus() { this.focused = true; }
    fire(t, e) { (this.listeners[t] || []).forEach((f) => f(e || {})); }
    find(id) { if (this.id === id) return this; for (const c of this.children) { const r = c.find(id); if (r) return r; } return null; }
}
const doc = { createElement: (t) => new El(t) };
function store(initial, broken) {
    const m = Object.assign({}, initial || {});
    return { m, getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { if (broken === true) throw new Error('blocked'); if (broken === 'silent') return; m[k] = String(v); } };
}
function make(initial, o) {
    const storage = o && o.noStorage ? null : store(initial, o && o.broken);
    const host = new El('div');
    const api = D.create({ doc, host, storage, rtc: !(o && o.noRtc) });
    return { api, host, storage, sw: host.find('dr-switch'), yes: host.find('dr-yes'), no: host.find('dr-no'), note: host.find('dr-note'), help: host.find('dr-help') };
}

expect('the setting has the name that the game page reads', [D.KEY, D.SEEN], ['ants.direct', 'ants.direct.seen']);
{
    const t = make();
    expect('off by default: the switch says so, nothing is written, the answer is no', [t.sw.attrs['aria-checked'], t.api.on(), JSON.stringify(t.storage.m), t.note.hidden], ['false', false, '{}', true]);
    expect('the switch is a switch with a label and a help line', [t.sw.attrs.role, t.sw.attrs['aria-labelledby'], t.sw.attrs['aria-describedby']], ['switch', 'dr-label', 'dr-help dr-note']);
    const words = JSON.stringify(D.WORDS);
    expect('the words say the true risk: that players can see the IP address', [/IP address/.test(D.WORDS.help), /IP address/.test(D.WORDS.ask)], [true, true]);
    expect('the words use none of the network\'s words', /STUN|TURN|ICE|WebRTC|NAT|peer|candidate/.test(words), false);
    expect('the help line is shown', t.help.textContent, D.WORDS.help);
}
{   // the first time: the explanation, and the switch waits for the answer
    const t = make();
    t.sw.fire('click');
    expect('the first press asks: the explanation shows, the switch is still off, nothing is written', [t.note.hidden, t.sw.attrs['aria-checked'], t.api.on(), JSON.stringify(t.storage.m), t.yes.focused], [false, 'false', false, '{}', true]);
    t.no.fire('click');
    expect('Keep it off closes it and leaves everything as it was', [t.note.hidden, t.api.on(), JSON.stringify(t.storage.m), t.sw.focused], [true, false, '{}', true]);
    t.sw.fire('click');
    t.note.fire('keydown', { key: 'Escape' });
    expect('Escape is Keep it off', [t.note.hidden, t.api.on(), JSON.stringify(t.storage.m)], [true, false, '{}']);
    t.sw.fire('click');
    t.yes.fire('click');
    expect('Turn it on keeps the choice and that it was explained, and the switch shows it', [t.api.on(), t.storage.m['ants.direct'], t.storage.m['ants.direct.seen'], t.sw.attrs['aria-checked'], /\bis-on\b/.test(t.host.className), t.note.hidden], [true, '1', '1', 'true', true, true]);
    t.sw.fire('click');
    expect('turning it off is one press and asks nothing', [t.api.on(), t.storage.m['ants.direct'], t.note.hidden, t.sw.attrs['aria-checked'], /\bis-on\b/.test(t.host.className)], [false, '0', true, 'false', false]);
    t.sw.fire('click');
    expect('once explained, turning it on again is one press', [t.api.on(), t.note.hidden, t.storage.m['ants.direct']], [true, true, '1']);
}
{   // a browser that has the choice from an earlier visit
    const on = make({ 'ants.direct': '1', 'ants.direct.seen': '1' });
    expect('a kept choice shows', [on.api.on(), on.sw.attrs['aria-checked']], [true, 'true']);
    const odd = make({ 'ants.direct': 'yes' });
    expect('anything but 1 is off', [odd.api.on(), odd.sw.attrs['aria-checked']], [false, 'false']);
}
{   // a browser that will not keep the choice
    const t = make({}, { broken: true });
    t.sw.fire('click');
    t.yes.fire('click');
    expect('a browser that refuses the choice: the switch is off, cannot be pressed, and says that the relay stays', [t.api.on(), t.sw.disabled, t.help.textContent], [false, true, D.WORDS.stuck]);
    const quiet = make({}, { broken: 'silent' });                        // (a storage that takes the write and keeps nothing)
    quiet.sw.fire('click');
    quiet.yes.fire('click');
    expect('a storage that keeps nothing is told by reading the choice back: off, and the relay stays', [quiet.api.on(), quiet.sw.disabled, quiet.help.textContent], [false, true, D.WORDS.stuck]);
    const none = make({}, { noStorage: true });
    expect('no storage at all is the same', [none.api.on(), none.sw.disabled, none.help.textContent], [false, true, D.WORDS.stuck]);
}
{   // no WebRTC: no switch
    const t = make({ 'ants.direct': '1' }, { noRtc: true });
    expect('a browser with no WebRTC has no switch', [t.host.hidden, t.sw, t.api.on(), t.api.set(true)], [true, null, false, false]);
}
console.log((failed ? 'FAILED ' : 'ok ') + checks + ' checks, ' + failed + ' failed');
process.exit(failed ? 1 : 0);

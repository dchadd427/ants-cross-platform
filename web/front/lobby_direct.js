// The "Direct connection" switch of the front page's Room (web/lobby.html; protocol 25, docs/P2P_HINTS.md): one switch, for this player only, off by default, remembered by this browser
// (localStorage `ants.direct`, which the game page reads when it opens the match: web/p2p.js). Off, every connection to the other players goes through the operator's relay and no player's address
// reaches another. On, a connection to a player who has turned it on too may be direct, which can lower the delay with players who are near, and those players can see this player's IP address.
//   * The first time a person turns it on, a short explanation says so in plain words and asks (Turn it on / Keep it off); the switch changes only on the answer. Turning it off again is one press, and
//     asks nothing. The explanation is remembered (`ants.direct.seen`) and not shown again.
//   * A browser that has no WebRTC, or that will not keep the choice, shows no switch that could do anything: the first has none, the second says that the relay stays.
//   * ES5 and no module system: a page gets the global AntsLobbyDirect, node gets module.exports (the DOM comes in as `doc`). Every text is set with textContent.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsLobbyDirect = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';
    var KEY = 'ants.direct', SEEN = 'ants.direct.seen';
    // the words of the control (the wording the maintainer chose; "relay" is the only technical word, and it is explained by what it does)
    var WORDS = {
        label: 'Direct connection',
        help: 'Can lower delay with players near you. Players you connect to directly can see your IP address.',
        askTitle: 'Turn on a direct connection?',
        ask: 'A direct connection links your browser to the browsers of other players in a match, with nothing in between. It can lower the delay with players who are near you. Those players can see your IP address, which tells roughly where you are. With the switch off, everything goes through our relay and nobody sees it. You can turn it off again at any time.',
        yes: 'Turn it on',
        no: 'Keep it off',
        stuck: 'This browser cannot keep the choice, so everything goes through our relay.'
    };

    function read(storage, key) { try { return storage.getItem(key); } catch (e) { return null; } }
    function write(storage, key, value) { try { storage.setItem(key, value); return read(storage, key) === value; } catch (e) { return false; } }
    function isOn(storage) { return !!storage && read(storage, KEY) === '1'; }

    // o: { doc, host (the element that holds the control), storage (localStorage, or null), rtc (true when the browser has WebRTC) }. Returns { on(), set(value), ask(), field, host }.
    function create(o) {
        var doc = o.doc, host = o.host, storage = o.storage || null, ready = o.rtc !== false;
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        host.className = (host.className ? host.className + ' ' : '') + 'direct';
        if (!ready) { host.hidden = true; return { on: function () { return false; }, set: function () { return false; }, ask: function () {}, field: null, host: host }; }
        var row = el('div', 'dr-row'), label = el('span', 'dr-label', WORDS.label); label.id = 'dr-label';
        var sw = el('button', 'dr-switch'); sw.type = 'button'; sw.id = 'dr-switch';
        sw.setAttribute('role', 'switch'); sw.setAttribute('aria-labelledby', 'dr-label'); sw.setAttribute('aria-describedby', 'dr-help dr-note');
        var knob = el('i', 'dr-knob'); knob.setAttribute('aria-hidden', 'true'); sw.appendChild(knob);
        row.appendChild(label); row.appendChild(sw);
        var help = el('p', 'dr-help', WORDS.help); help.id = 'dr-help';
        var note = el('div', 'dr-note'); note.id = 'dr-note'; note.setAttribute('role', 'group'); note.hidden = true;
        var title = el('p', 'dr-title', WORDS.askTitle), text = el('p', 'dr-text', WORDS.ask), buttons = el('div', 'dr-buttons');
        var yes = el('button', 'btn sm', WORDS.yes), no = el('button', 'btn sm', WORDS.no);
        yes.type = 'button'; no.type = 'button'; yes.id = 'dr-yes'; no.id = 'dr-no';
        buttons.appendChild(yes); buttons.appendChild(no);
        note.appendChild(title); note.appendChild(text); note.appendChild(buttons);
        note.setAttribute('aria-label', WORDS.askTitle);
        host.appendChild(row); host.appendChild(help); host.appendChild(note);
        var asking = false, stuck = !storage;
        function paint() {
            var on = isOn(storage);
            sw.setAttribute('aria-checked', on ? 'true' : 'false');
            host.className = host.className.replace(/\s*\bis-on\b/, '') + (on ? ' is-on' : '');
            sw.disabled = stuck;
            help.textContent = stuck ? WORDS.stuck : WORDS.help;
            note.hidden = !asking;
        }
        function set(on) {                                   // the answer: the choice is kept, and read back (a browser that refuses is told so)
            asking = false;
            if (!write(storage, KEY, on ? '1' : '0')) stuck = true;
            else stuck = false;
            paint();
            return isOn(storage) === !!on;
        }
        function press() {
            if (stuck) return;
            if (isOn(storage)) return set(false);
            if (read(storage, SEEN) === '1') return set(true);       // (it was explained once: one press)
            asking = true;
            paint();
            yes.focus();
        }
        sw.addEventListener('click', press);
        yes.addEventListener('click', function () { write(storage, SEEN, '1'); set(true); sw.focus(); });
        no.addEventListener('click', function () { asking = false; paint(); sw.focus(); });
        note.addEventListener('keydown', function (e) { if (e.key === 'Escape') { asking = false; paint(); sw.focus(); } });
        paint();
        return { on: function () { return isOn(storage); }, set: set, ask: press, field: sw, host: host };
    }
    return { create: create, isOn: isOn, KEY: KEY, SEEN: SEEN, WORDS: WORDS };
});

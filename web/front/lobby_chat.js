// The chat box of the front page's lobby (web/lobby.html, docs/PLAY_IN_BROWSER.md "Chat in the lobby"): the people in a room talk before START, in the box under the colors. The room's own chat is the game's
// (docs/NETWORK_PORT.md "Chat in the waiting room": a line of 100 printable ASCII characters, relayed by the room to everybody in it, the sender too, with the seat of the connection stamped); a lobby page
// already hears those lines (web/front/lobby_net.js, the 'chat' event) and now says them (LobbyClient.say, at the server's pace). Nothing of the server or of the protocol changed.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsLobbyChat, node gets module.exports (tests/scripts/web_lobby_chat_check.js runs it with a small fake of the DOM).
//   * Every name and every line is set as TEXT (textContent), never as markup: a name may be any printable ASCII, "<b>" included.
//   * The pace of the lines is the client's (BUDGETS in lobby_net.js: 4 at once, then one every 1.25 s: a little under the room's own, so that a line that is handled early is not dropped), so that no line is
//     dropped by the server and nobody is thrown out for a flood; this script only says so
//     when the client refuses a line.
//   * The lines live in the page: nothing is stored, a reload starts an empty box (the room does not replay, as it does not for a game that takes a seat over).
//   * A line is the room's when the room says it back (the sender hears its own lines): until then it is pending. The client's say() is true as soon as the socket took the bytes, and a stalled uplink
//     or a link that drops with the line in it loses the line without a word; no echo in PENDING_MS, or the link dropping first, is a hint that the line may not have arrived and the text back in the field.
//   * The toast is for a stranger's text: it is cut to TOAST_CHARS and there is at most one chat toast in TOAST_GAP (the log has every line).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsLobbyChat = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MAX_LINE = 100;                          // the match's chat entry (the server's kMaxChatChars, lobby_net.js MAX_CHAT)
    var MAX_LINES = 200;                         // the room's own log keeps the last 200 (ChatLog::kMaxLines)
    var HOST = 1, CLIENT = 2;                    // lobby_net.js SLOT.Host and SLOT.Client: the states of a color that a person holds
    var FOLLOW = 40;                             // a box within this many pixels of its end follows the newest line
    var STICKY = 140;                            // the page's START bar (sticky at the bottom of a phone's screen) covers this much of the window: a box under it is off screen
    var TOAST_CHARS = 60;                        // a line is cut to this many characters in the toast (a stranger's line of 100 must not fill a phone's screen)
    var TOAST_GAP = 4000;                        // ms: at most one chat toast in this time (a talker must not keep the toast up; the log has every line)
    var PENDING_MS = 3000;                       // ms: a line that the room has not said back by then may not have arrived

    // The words. ASCII and the ellipsis, as the other words of the lobby (tests/scripts/web_lobby_chat_check.js holds them to it).
    var TEXT = {
        title: 'Chat',
        note: 'Only the people in this room can read it.',
        label: 'Messages in this room',
        field: 'Chat message',
        send: 'Send',
        placeholder: 'Say something to the room',
        down: 'Chat is back when you are in the room again.',
        opening: 'Opening the game…',
        alone: 'You are the only one here. Send the link above, then say hello.',
        empty: 'Nothing said yet. Say hello: everyone in this room will see it.',
        tooFast: 'A little slower, please. Your line is still here.',
        accent: 'Letters, digits and signs without accents only: the game cannot show the rest.',
        lostBack: 'Your last line may not have arrived. It is back in the field.',
        lostSaid: 'Your last line may not have arrived:'
    };

    // ---- what may be typed and what is sent ------------------------------------------------------------------------------------------------------------------------------------------------------
    // The game's own chat line takes printable ASCII (0x20 - 0x7e) and at most 100 characters, and drops the rest as it is typed; so does this box. { text: what is left, dropped: something was left out }.
    // (A line that is too long is cut, which is no character left out: the field has a maxlength of 100, a paste is cut by the browser.)
    function cleanLine(text) {
        if (typeof text !== 'string') return { text: '', dropped: false };
        var kept = text.replace(/[^\x20-\x7e]/g, '');
        return { text: kept.slice(0, MAX_LINE), dropped: kept.length !== text.length };
    }
    // What a line sends: the characters that can be shown, at most 100, the blanks at both ends cut off; '' when nothing is left (a line of blanks says nothing)
    function sendable(text) {
        return cleanLine(text).text.replace(/^ +/, '').replace(/ +$/, '');
    }

    // ---- what the box shows in each state of the room ----------------------------------------------------------------------------------------------------------------------------------------------
    // How many people are in a Room message (a computer player reads nothing, an empty color is nobody)
    function peopleIn(room) {
        var n = 0;
        if (room && room.slots) for (var s = 0; s < room.slots.length; s++) if (room.slots[s].state === HOST || room.slots[s].state === CLIENT) n++;
        return n;
    }
    // { shown: the box is there (a lobby room that welcomed this page), live: a line can be said now, placeholder: what the field says, empty: what the log says while it holds no line }
    function stateOf(status, room) {
        var shown = !!room && room.lobby === true;
        var live = shown && status === 'online' && !room.starting;
        return {
            shown: shown,
            live: live,
            placeholder: live ? TEXT.placeholder : (shown && status === 'online' && room.starting ? TEXT.opening : TEXT.down),
            empty: peopleIn(room) < 2 ? TEXT.alone : TEXT.empty
        };
    }
    // The name that the room shows for a seat ("Seat N" for a seat with no name yet, as the game's own chat box says)
    function nameOf(room, seat) {
        return room && room.slots && room.slots[seat] && room.slots[seat].name ? room.slots[seat].name : 'Seat ' + (seat + 1);
    }
    // The small lines of the system: the words of the page's toast for a person who comes ("Sam joined as Red."), and one for a person who goes
    function joinedText(name, seat, colors) { return name + ' joined as ' + colors[seat].name + '.'; }
    function leftText(name) { return name + ' left.'; }
    function toastText(who, text) { return who + ': ' + (text.length > TOAST_CHARS ? text.slice(0, TOAST_CHARS - 1) + '\u2026' : text); }

    // ---- the box ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // mount({ doc, host (the section of the page), colors (the lobby's four colors by seat: { id, name }), say (text -> true when the client took the line), toast (text), onScreen (optional: is the box in view),
    //         now / setTimeout / clearTimeout (optional: the clock and the timers, which the checks drive; the page's own by default) })
    // builds the box into `host` and returns { attach(client), clear(), lines() }. attach(client) is for every client that the page makes (a page that joins another room, takes its room back, or is made again
    // after a removal makes a new one): the box follows the last one, keeps its lines while the room is the same, and lets them go when the room is another or was made again.
    function mount(opts) {
        var doc = opts.doc, host = opts.host, colors = opts.colors;
        var current = null;
        var now = opts.now || function () { return Date.now(); };
        var setT = opts.setTimeout || function (fn, ms) { return setTimeout(fn, ms); };
        var clearT = opts.clearTimeout || function (id) { clearTimeout(id); };
        var pending = [];                        // { text, at }: the lines that the client took and that the room has not said back yet, the oldest first
        var timer = null;                        // waits for the oldest of them
        var lostText = null, lostHint = '';      // the last line that may have been lost, and the hint that said so
        var lastToast = null;                    // when the last chat toast was shown
        var lines = [];                          // { kind: 'chat' | 'sys', seat, who, text }, the last MAX_LINES
        var rows = [];                           // the list items of the lines, the same order
        var hintShown = true;                    // the log holds the line of the empty state (and nothing else)

        function el(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        host.setAttribute('aria-labelledby', 'chat-h');         // (the heading is made here: the page's markup is the empty section)
        var head = el('div', 'head');
        var title = el('h2', '', TEXT.title);
        title.setAttribute('id', 'chat-h');
        head.appendChild(title);
        head.appendChild(el('span', 'note', TEXT.note));
        var log = el('div', 'chat-log');
        log.setAttribute('id', 'chat-log');
        log.setAttribute('role', 'log');
        log.setAttribute('aria-live', 'polite');
        log.setAttribute('aria-relevant', 'additions');
        log.setAttribute('aria-label', TEXT.label);
        log.setAttribute('tabindex', '0');
        var empty = el('div', 'empty');
        log.appendChild(empty);
        var form = el('form', 'chat-form');
        form.setAttribute('autocomplete', 'off');
        var input = el('input');
        input.setAttribute('id', 'chat-input');
        input.setAttribute('type', 'text');
        input.setAttribute('maxlength', String(MAX_LINE));
        input.setAttribute('aria-label', TEXT.field);
        input.setAttribute('aria-describedby', 'chat-hint');
        input.setAttribute('autocomplete', 'off');
        input.setAttribute('autocapitalize', 'off');
        input.setAttribute('autocorrect', 'off');
        input.setAttribute('spellcheck', 'false');
        input.setAttribute('enterkeyhint', 'send');
        input.setAttribute('inputmode', 'text');
        var send = el('button', 'btn sm', TEXT.send);
        send.setAttribute('type', 'submit');
        send.setAttribute('id', 'chat-send');
        form.appendChild(input);
        form.appendChild(send);
        var hint = el('p', 'hint');
        hint.setAttribute('id', 'chat-hint');
        hint.setAttribute('role', 'status');
        host.appendChild(head);
        host.appendChild(log);
        host.appendChild(form);
        host.appendChild(hint);

        function state() { return stateOf(current ? current.status : 'idle', current ? current.room : null); }
        function setHint(text) { if (hint.textContent !== text) hint.textContent = text; }
        function paint() {
            var st = state();
            host.hidden = !st.shown;
            input.readOnly = !st.live;           // (not disabled: a disabled field loses the focus, and a phone's keyboard closes with it)
            send.disabled = !st.live;
            input.placeholder = st.placeholder;
            empty.textContent = st.empty;
            if (lines.length === 0 && !hintShown) { log.appendChild(empty); hintShown = true; }
        }
        function nearBottom() { return log.scrollHeight - log.scrollTop - log.clientHeight < FOLLOW; }
        // A line of the log: { kind, seat, who, text }. A line of one's own always shows; the others follow the end only while the box is at its end (somebody who scrolled up to read is left alone).
        function add(kind, seat, who, text, own) {
            var follow = own || nearBottom();
            var li = el('div', kind === 'sys' ? 'sys' : '');
            if (kind === 'chat') {
                if (colors[seat]) li.setAttribute('data-c', colors[seat].id);
                li.appendChild(el('span', 'who', who + ': '));          // (the blank is in the text, so that a copy of the log reads "Sam: hello")
                li.appendChild(el('span', 'say', text));
            } else li.textContent = text;
            if (hintShown) { log.removeChild(empty); hintShown = false; }
            log.appendChild(li);
            lines.push({ kind: kind, seat: seat, who: who, text: text });
            rows.push(li);
            while (lines.length > MAX_LINES) { lines.shift(); log.removeChild(rows.shift()); }
            if (follow) log.scrollTop = log.scrollHeight;
        }
        function clear() {
            while (rows.length > 0) log.removeChild(rows.pop());
            lines = [];
            forget();
            paint();
        }
        function inView() {
            if (typeof opts.onScreen === 'function') return opts.onScreen();
            var r = host.getBoundingClientRect();
            var h = doc.defaultView && doc.defaultView.innerHeight ? doc.defaultView.innerHeight : 0;
            return r.bottom > 0 && r.top < h - STICKY;
        }
        function hear(c, e) {
            var who = nameOf(c.room, e.sender);
            add('chat', e.sender, who, e.text, e.sender === c.seat);
            if (e.sender === c.seat) echoed(e.text);
            else if (!inView() && opts.toast && (lastToast === null || now() - lastToast >= TOAST_GAP)) {
                lastToast = now();
                opts.toast(toastText(who, e.text));
            }
        }

        // ---- a line that the room has not said back: pending, then lost (the hint and the text back in the field) ----------------------------------------------------------------------------------
        function stopTimer() { if (timer !== null) { clearT(timer); timer = null; } }
        function watch() {                       // one timer, for the oldest pending line
            stopTimer();
            if (pending.length === 0) return;
            var wait = pending[0].at + PENDING_MS - now();
            timer = setT(expire, wait > 0 ? wait : 0);
        }
        function expire() {
            timer = null;
            var t = now(), old = [];
            while (pending.length > 0 && t - pending[0].at >= PENDING_MS) old.push(pending.shift());
            if (old.length > 0) lose(old);
            watch();
        }
        function lose(list) {                    // the newest of the lines comes back to the field when it is empty, else the hint holds it
            lostText = list[list.length - 1].text;
            if (input.value === '') { input.value = lostText; lostHint = TEXT.lostBack; } else lostHint = TEXT.lostSaid + ' ' + lostText;
            setHint(lostHint);
        }
        function loseAll() {                     // the link dropped: no echo can come for a line that was on its way
            stopTimer();
            var all = pending;
            pending = [];
            if (all.length > 0) lose(all);
        }
        function forget() {                      // another room, or the same room made again: the lines of the old one are nobody's business
            stopTimer();
            pending = [];
            lostText = null;
        }
        function echoed(text) {                  // the room said a line of this seat back: the one that it was, if it was pending, else the late word of a line that was taken for lost
            for (var i = 0; i < pending.length; i++) {
                if (pending[i].text === text) { pending.splice(i, 1); watch(); return; }
            }
            if (lostText !== null && text === lostText) {
                if (hint.textContent === lostHint) setHint('');
                if (input.value === lostText) input.value = '';
                lostText = null;
            }
        }

        // sending: the line goes to the client; one that it does not take (the pace) stays in the field, and the hint says why
        function submit() {
            var line = sendable(input.value);
            if (line === '' || !state().live) return false;
            if (opts.say(line)) {
                input.value = '';
                setHint('');
                lostText = null;
                pending.push({ text: line, at: now() });
                if (timer === null) watch();
                input.focus();                    // (a phone's keyboard stays up for the next line)
                return true;
            }
            setHint(TEXT.tooFast);
            return false;
        }
        function typed(ev) {
            if (ev && ev.isComposing) return;     // (a predictive keyboard's or an IME's composition is not rewritten while it goes on)
            var c = cleanLine(input.value);
            if (c.dropped) { input.value = c.text; setHint(TEXT.accent); } else setHint('');
        }
        form.addEventListener('submit', function (ev) { if (ev && ev.preventDefault) ev.preventDefault(); submit(); });
        input.addEventListener('input', typed);
        input.addEventListener('compositionend', function () { typed(null); });

        function attach(c) {
            if (current !== c) {
                if (!current || current.code !== c.code) clear();
                current = c;
            }
            c.on('welcome', function () { if (c === current) paint(); });
            c.on('room', function () { if (c === current) paint(); });
            c.on('status', function () { if (c === current) { paint(); if (c.status !== 'online') loseAll(); } });
            c.on('gone', function () { if (c === current) { loseAll(); clear(); } });        // (the room was lost and made again: the lines were a dead room's, and a line on its way is lost with it)
            c.on('chat', function (e) { if (c === current) hear(c, e); });
            c.on('joined', function (e) { if (c === current && e.seat !== c.seat) add('sys', -1, '', joinedText(e.name, e.seat, colors), false); });
            c.on('left', function (e) { if (c === current) add('sys', -1, '', leftText(e.name), false); });
            paint();
        }
        paint();
        return { attach: attach, clear: clear, lines: function () { return lines.slice(); } };
    }

    return { MAX_LINE: MAX_LINE, MAX_LINES: MAX_LINES, PENDING_MS: PENDING_MS, TOAST_CHARS: TOAST_CHARS, TOAST_GAP: TOAST_GAP, TEXT: TEXT, cleanLine: cleanLine, sendable: sendable, peopleIn: peopleIn, stateOf: stateOf, nameOf: nameOf, joinedText: joinedText, leftText: leftText, toastText: toastText, mount: mount };
});

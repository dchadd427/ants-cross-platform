// Runs the lobby page's chat (web/front/lobby_chat.js) without a browser: what may be typed and sent (printable ASCII, 100 characters, no empty line), what the box shows in each state of the room
// (nothing before a room, the hint when alone, the hint when nobody has spoken, "Chat is back when you are in the room again." while the link is down, "Opening the game" while START waits), the
// words of the lines of the system, and the whole box built and driven with a small fake of the DOM and of the page's client: a line of a player and a line of the room's guests are set as TEXT only
// (the fake throws when innerHTML is assigned), a colour for each seat, the last 200 lines, the box follows the newest line unless it was scrolled up, a line that arrives while the box is off
// screen is also a toast, what a line that the room did not take does (the typed text stays, and the hint says why), what happens to the box when the page changes its client.
//   - every exported function of lobby_chat.js must be called by this check (a function that no table reaches is a failure);
//   - the pace of the lines is the client's (web/front/lobby_net.js, tests/scripts/web_lobby_client_check.js), not this script's.
// usage: node web_lobby_chat_check.js web/front/lobby_chat.js web/front/lobby_rules.js web/front/lobby_net.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');

const chatPath = process.argv[2];
const rulesPath = process.argv[3];
const netPath = process.argv[4];
if (!chatPath || !rulesPath || !netPath) { console.log('usage: web_lobby_chat_check.js lobby_chat.js lobby_rules.js lobby_net.js'); process.exit(2); }
process.on('uncaughtException', (e) => { console.log('FAIL the check stopped on an error: ' + (e && e.stack || e)); process.exit(1); });

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', want ' + b);
}

const RAW = require(path.resolve(chatPath));
const Rules = require(path.resolve(rulesPath));
const Net = require(path.resolve(netPath));
const called = Object.create(null);
const C = {};
Object.keys(RAW).forEach((k) => {
    C[k] = typeof RAW[k] === 'function' ? function () { called[k] = (called[k] || 0) + 1; return RAW[k].apply(this, arguments); } : RAW[k];
});

// ---- 0. what the library offers -------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('the library exports exactly these names (a new one needs a table here)', Object.keys(RAW).sort(), ['MAX_LINE', 'MAX_LINES', 'TEXT', 'cleanLine', 'joinedText', 'leftText', 'mount', 'nameOf', 'peopleIn', 'sendable', 'stateOf', 'toastText']);
check('a line is at most 100 characters: the match\'s chat entry (the server\'s kMaxChatChars, the client\'s MAX_CHAT)', C.MAX_LINE === 100 && C.MAX_LINE === Net.MAX_CHAT);
check('the box keeps the last 200 lines, as the room\'s own log does (ChatLog::kMaxLines)', C.MAX_LINES === 200);
check('the person states that the box counts are the protocol\'s Host and Client', Net.SLOT.Host === 1 && Net.SLOT.Client === 2 && Net.SLOT.Bot === 3 && Net.SLOT.Empty === 0);

// ---- 1. the words: plain text, printable ASCII and the ellipsis, no markup, no stray blank, "player" not "friend" -----------------------------------------------------------------------------------------
{
    const words = Object.keys(C.TEXT).map((k) => C.TEXT[k]);
    check('every word of the box is a string', words.every((w) => typeof w === 'string' && w.length > 0));
    check('the words are ASCII and the ellipsis only (the font of the page, as the other words of the lobby)', words.every((w) => /^[\x20-\x7e…]*$/.test(w)), words.filter((w) => !/^[\x20-\x7e…]*$/.test(w)).join(' | '));
    check('no word has markup in it', words.every((w) => !/[<>&]/.test(w)));
    check('no word has a stray blank (at the ends, doubled)', words.every((w) => !/^\s|\s$|\s\s/.test(w)));
    check('no word says "friend" (a project rule: player)', words.every((w) => !/friend/i.test(w)));
    same('the heading, the note under it and the button', [C.TEXT.title, C.TEXT.note, C.TEXT.send], ['Chat', 'Only the people in this room can read it.', 'Send']);
    same('the two empty states (alone, and with people who have not spoken)', [C.TEXT.alone, C.TEXT.empty], ['You are the only one here. Send the link above, then say hello.', 'Nothing said yet. Say hello: everyone in this room will see it.']);
    same('the placeholder, and the two that replace it', [C.TEXT.placeholder, C.TEXT.down, C.TEXT.opening], ['Say something to the room', 'Chat is back when you are in the room again.', 'Opening the game…']);
    same('the two hints under the field', [C.TEXT.tooFast, C.TEXT.accent], ['A little slower, please. Your line is still here.', 'Letters, digits and signs without accents only: the game cannot show the rest.']);
}

// ---- 2. what may be typed and what is sent ------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const hundred = 'x'.repeat(100);
    const rows = [
        ['hello', 'hello', false], ['', '', false], ['  two  spaces  ', '  two  spaces  ', false],
        ['café', 'caf', true], ['☺ ok', ' ok', true], ['你好', '', true], ['tab\there', 'tabhere', true], ['line\nbreak', 'linebreak', true], ['del\x7fete', 'delete', true],
        ['\x1fcontrol', 'control', true], [' ~ ', ' ~ ', false], ['😀', '', true],
        [hundred, hundred, false], [hundred + 'y', hundred, false], ['a'.repeat(300), 'a'.repeat(100), false],
        ['<b>x</b> & "q"', '<b>x</b> & "q"', false],                      // (markup is not what is left out: it is shown as text)
        [null, '', false], [undefined, '', false], [42, '', false]
    ];
    for (const [typed, text, dropped] of rows) same('cleanLine(' + JSON.stringify(typed) + ')', C.cleanLine(typed), { text, dropped });
    check('the printable range is 0x20 to 0x7e: both ends stay, the byte before and after go', C.cleanLine(' ').dropped === false && C.cleanLine('~').dropped === false && C.cleanLine('\x1f').dropped && C.cleanLine('\x7f').dropped);
    const sends = [
        ['hello', 'hello'], ['  hi  ', 'hi'], ['', ''], ['   ', ''], ['é', ''], ['aéb', 'ab'], ['☺', ''], ['x'.repeat(120), hundred],
        ['x'.repeat(99) + ' y', 'x'.repeat(99)], [' ' + hundred, hundred.slice(1)], [null, ''], [{}, '']
    ];
    for (const [typed, text] of sends) same('sendable(' + JSON.stringify(typed).slice(0, 40) + ')', C.sendable(typed), text);
    check('what is sendable is something that the client takes (validChatLine) and nothing else is', sends.every(([typed]) => C.sendable(typed) === '' || Net.validChatLine(C.sendable(typed))));
}

// ---- 3. what the box shows in each state of the room ---------------------------------------------------------------------------------------------------------------------------------------------
const E = Net.SLOT.Empty, H = Net.SLOT.Host, P = Net.SLOT.Client, B = Net.SLOT.Bot;
function roomOf(states, o) {
    o = o || {};
    return { lobby: o.lobby === undefined ? true : o.lobby, starting: !!o.starting, you: o.you === undefined ? 0 : o.you, leader: 0, slots: states.map((s, i) => ({ state: s, name: s === E ? '' : (o.names && o.names[i] !== undefined ? o.names[i] : ['Maple', 'Sam', 'Juniper', 'Pip'][i]) })) };
}
{
    same('peopleIn counts persons: a host or a client; a computer player and an empty colour are none', [C.peopleIn(roomOf([P, P, B, E])), C.peopleIn(roomOf([H, E, E, E])), C.peopleIn(roomOf([E, E, B, B])), C.peopleIn(roomOf([P, P, P, P])), C.peopleIn(null)], [2, 1, 0, 4, 0]);
    const alone = roomOf([P, E, E, E]), two = roomOf([P, P, E, E]), vsBot = roomOf([P, B, E, E]);
    same('no room yet: the box is not there', C.stateOf('connecting', null), { shown: false, live: false, placeholder: C.TEXT.down, empty: C.TEXT.alone });
    same('a room that no lobby made (a game made it): no box', C.stateOf('online', roomOf([P, P, E, E], { lobby: false })).shown, false);
    same('alone: the box is open, with the hint to send the link', C.stateOf('online', alone), { shown: true, live: true, placeholder: C.TEXT.placeholder, empty: C.TEXT.alone });
    same('with another person: the box is open, with the hint to say hello', C.stateOf('online', two), { shown: true, live: true, placeholder: C.TEXT.placeholder, empty: C.TEXT.empty });
    same('with a computer player only: still alone (a computer player reads nothing)', C.stateOf('online', vsBot).empty, C.TEXT.alone);
    for (const status of ['connecting', 'offline', 'refused', 'removed', 'superseded', 'closed', 'idle']) same('while the link is "' + status + '": the box stays, the field is shut and says why', C.stateOf(status, two), { shown: true, live: false, placeholder: C.TEXT.down, empty: C.TEXT.empty });
    same('START pressed: the field is shut, "Opening the game"', C.stateOf('online', roomOf([P, P, E, E], { starting: true })), { shown: true, live: false, placeholder: C.TEXT.opening, empty: C.TEXT.empty });
}

// ---- 4. names, colours and the words of the lines of the system ---------------------------------------------------------------------------------------------------------------------------------------
{
    const room = roomOf([P, P, E, P], { names: ['Maple', 'Sam', '', ''] });
    same('a line is under the name that the room shows for the seat', [C.nameOf(room, 0), C.nameOf(room, 1)], ['Maple', 'Sam']);
    same('a seat with no name is "Seat N" (the game\'s own chat box says the same)', [C.nameOf(room, 2), C.nameOf(room, 3), C.nameOf(null, 1), C.nameOf(room, 9)], ['Seat 3', 'Seat 4', 'Seat 2', 'Seat 10']);
    same('a joiner is announced as the page\'s toast says it: name, "joined as", the colour', C.joinedText('Sam', 1, Rules.COLOURS), 'Sam joined as Red.');
    same('... in each colour', [0, 1, 2, 3].map((s) => C.joinedText('X', s, Rules.COLOURS)), ['X joined as Green.', 'X joined as Red.', 'X joined as Blue.', 'X joined as Black.']);
    same('a person who goes', C.leftText('Sam'), 'Sam left.');
    same('a name is put in as it is (it is text; nothing is cut or escaped here)', [C.leftText('<b>'), C.joinedText('<i>&', 3, Rules.COLOURS)], ['<b> left.', '<i>& joined as Black.']);
    same('the toast of a line is "name: line"', C.toastText('Sam', 'hello there'), 'Sam: hello there');
}

// ---- 5. the box, built and driven: a small fake of the DOM and of the page's client -----------------------------------------------------------------------------------------------------------------------
function node(tag) {
    const n = { tag, children: [], attrs: {}, listeners: {}, className: '', textValue: '', hidden: false, disabled: false, value: '', placeholder: '', parentNode: null, scrollTop: 0, clientHeight: 100, focused: 0, rect: { top: 0, bottom: 200 } };
    for (const bad of ['innerHTML', 'outerHTML']) Object.defineProperty(n, bad, { get() { return ''; }, set() { throw new Error(bad + ' was assigned: a name or a line must be text only'); } });
    n.insertAdjacentHTML = () => { throw new Error('insertAdjacentHTML was called'); };
    Object.defineProperty(n, 'textContent', {
        get() { return n.children.length ? n.children.map((c) => c.textContent).join('') : n.textValue; },
        set(v) { n.children.forEach((c) => { c.parentNode = null; }); n.children = []; n.textValue = String(v); }
    });
    Object.defineProperty(n, 'scrollHeight', { get() { return n.children.length * 20; } });
    n.appendChild = (c) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; n.children.push(c); return c; };
    n.removeChild = (c) => { const i = n.children.indexOf(c); if (i >= 0) { n.children.splice(i, 1); c.parentNode = null; } return c; };
    Object.defineProperty(n, 'firstChild', { get() { return n.children[0] || null; } });
    n.setAttribute = (k, v) => { n.attrs[k] = String(v); };
    n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null);
    n.addEventListener = (type, fn) => { (n.listeners[type] = n.listeners[type] || []).push(fn); };
    n.fire = (type, ev) => { const e = Object.assign({ type, preventDefault() { this.prevented = true; } }, ev || {}); (n.listeners[type] || []).forEach((fn) => fn(e)); return e; };
    n.focus = () => { n.focused++; };
    n.getBoundingClientRect = () => n.rect;
    return n;
}
function world() {
    const doc = { defaultView: { innerHeight: 800 }, createElement: node };
    const host = node('section');
    host.hidden = true;
    const toasts = [];
    const state = { current: null, sayResult: true, said: [], onScreen: true };
    const chat = C.mount({ doc, host, colours: Rules.COLOURS, say: (text) => { state.said.push(text); return state.sayResult; }, toast: (text) => toasts.push(text), onScreen: () => state.onScreen });
    return { doc, host, toasts, state, chat };
}
function fakeClient(code, seat, room, status) {
    const c = { code, seat, status: status || 'online', room: room || null, handlers: {} };
    c.on = (ev, fn) => { (c.handlers[ev] = c.handlers[ev] || []).push(fn); return c; };
    c.emit = (ev, data) => (c.handlers[ev] || []).forEach((fn) => fn(data));
    return c;
}
const find = (n, pred) => { if (pred(n)) return n; for (const c of n.children) { const r = find(c, pred); if (r) return r; } return null; };
const byTag = (n, tag) => find(n, (x) => x.tag === tag);
const byId = (n, id) => find(n, (x) => x.attrs.id === id);
const logOf = (w) => byId(w.host, 'chat-log');
const lineTexts = (w) => logOf(w).children.map((li) => li.textContent);

{   // the pieces: what the box is made of, and what each piece says to a screen reader and to a phone's keyboard
    const w = world();
    check('before any client the box is hidden', w.host.hidden === true);
    const heading = byId(w.host, 'chat-h');
    check('a heading "Chat" that the section is named by', !!heading && heading.tag === 'h2' && heading.textContent === 'Chat' && w.host.getAttribute('aria-labelledby') === 'chat-h');
    check('the note says who reads it', !!find(w.host, (x) => x.className === 'note' && x.textContent === 'Only the people in this room can read it.'));
    const log = logOf(w);
    check('the log is a list that is a live log for a screen reader, in reading order, that a keyboard can scroll', log.tag === 'ul' && log.getAttribute('role') === 'log' && log.getAttribute('aria-live') === 'polite' && log.getAttribute('aria-relevant') === 'additions' && log.getAttribute('tabindex') === '0' && !!log.getAttribute('aria-label'));
    const input = byId(w.host, 'chat-input');
    same('the field: a text field for one line of 100 that no keyboard corrects, with the key that says Send',
        [input.tag, input.getAttribute('type'), input.getAttribute('maxlength'), input.getAttribute('autocomplete'), input.getAttribute('autocapitalize'), input.getAttribute('autocorrect'), input.getAttribute('spellcheck'), input.getAttribute('enterkeyhint'), input.getAttribute('inputmode')],
        ['input', 'text', '100', 'off', 'off', 'off', 'false', 'send', 'text']);
    check('the field has a name for a screen reader and is described by the hint line', !!input.getAttribute('aria-label') && input.getAttribute('aria-describedby') === 'chat-hint');
    const button = byTag(w.host, 'button');
    check('the button is a submit button that says Send', button.getAttribute('type') === 'submit' && button.textContent === 'Send' && /\bbtn\b/.test(button.className));
    const hint = byId(w.host, 'chat-hint');
    check('the hint line is a status that is empty at first', hint.getAttribute('role') === 'status' && hint.textContent === '');
    check('the form is the field and the button, and it does not let a browser fill it in', byTag(w.host, 'form').getAttribute('autocomplete') === 'off' && byTag(w.host, 'form').children.length === 2);
}

{   // a room of one: the box opens with the hint; a second person changes the hint; the lines of players are text with a colour
    const w = world();
    const room = roomOf([P, E, E, E]);
    const c = fakeClient('k7m2xq', 0, room);
    w.chat.attach(c);
    check('a client in a lobby room: the box is shown, the field is open', w.host.hidden === false && byId(w.host, 'chat-input').disabled === false && byTag(w.host, 'button').disabled === false);
    same('alone: the log holds the hint to send the link', lineTexts(w), [C.TEXT.alone]);
    check('the field says what it is for', byId(w.host, 'chat-input').placeholder === C.TEXT.placeholder);
    c.room = roomOf([P, P, E, E]);
    c.emit('room', c.room);
    same('a second person: the hint says to say hello', lineTexts(w), [C.TEXT.empty]);
    c.emit('chat', { sender: 1, team: false, text: 'hey! is this the room link?' });
    same('a line of a player: "Name:" and the line, and the hint is gone', lineTexts(w), ['Sam:hey! is this the room link?']);
    const li = logOf(w).children[0];
    check('the line carries the seat\'s colour (Red is seat 1) and no other class that styles it as the system', li.getAttribute('data-c') === 'red' && li.className === '');
    check('the name and the line are two spans (the name is in the colour)', li.children.length === 2 && li.children[0].className === 'who' && li.children[0].textContent === 'Sam:' && li.children[1].className === 'say');
    for (const [seat, id] of [[0, 'green'], [1, 'red'], [2, 'blue'], [3, 'black']]) {
        c.room = roomOf([P, P, P, P]);
        c.emit('chat', { sender: seat, team: false, text: 'x' });
        check('seat ' + seat + ' is ' + id, logOf(w).children[logOf(w).children.length - 1].getAttribute('data-c') === id);
    }
    c.emit('chat', { sender: 1, team: true, text: 'a team line (there is no team before the match; the server clears the flag)' });
    check('a line is a line whatever its flag says', lineTexts(w)[lineTexts(w).length - 1].indexOf('Sam:a team line') === 0);
    // the lines of the system
    const before = lineTexts(w).length;
    c.emit('joined', { type: 'joined', seat: 3, name: 'Pip' });
    c.emit('left', { type: 'left', seat: 3, name: 'Pip' });
    same('a person who comes and goes is a small line of the system, in no colour', lineTexts(w).slice(before), ['Pip joined as Black.', 'Pip left.']);
    const sys = logOf(w).children.slice(before);
    check('(the system lines have the class "sys" and no seat colour)', sys.every((l) => l.className === 'sys' && l.getAttribute('data-c') === null && l.children.length === 0));
    c.emit('joined', { type: 'joined', seat: 0, name: 'Maple' });
    check('the page\'s own seat is not announced to itself', lineTexts(w).length === before + 2);
}

{   // names and lines are TEXT: the fake throws when markup is assigned; a name that is markup shows as it is
    const w = world();
    const c = fakeClient('k7m2xq', 0, roomOf([P, P, E, E], { names: ['Maple', '<img src=x onerror=alert(1)>', '', ''] }));
    w.chat.attach(c);
    c.emit('chat', { sender: 1, team: false, text: '<script>alert(2)</script> & "quotes"' });
    same('the name and the line are shown as the characters that they are', lineTexts(w), ['<img src=x onerror=alert(1)>:<script>alert(2)</script> & "quotes"']);
    c.emit('joined', { type: 'joined', seat: 2, name: '<b>Eve</b>' });
    check('so is the name in a line of the system', lineTexts(w)[1] === '<b>Eve</b> joined as Blue.');
    check('the fake DOM never saw markup assigned (it would have thrown)', true);
}

{   // the last 200 lines
    const w = world();
    const c = fakeClient('k7m2xq', 0, roomOf([P, P, E, E]));
    w.chat.attach(c);
    for (let i = 1; i <= 205; i++) c.emit('chat', { sender: 1, team: false, text: 'line ' + i });
    check('205 lines: the last 200 are kept, the oldest five are gone', logOf(w).children.length === 200 && lineTexts(w)[0] === 'Sam:line 6' && lineTexts(w)[199] === 'Sam:line 205' && w.chat.lines().length === 200);
}

{   // the box follows the newest line unless it was scrolled up; a line of one's own always shows
    const w = world();
    const c = fakeClient('k7m2xq', 0, roomOf([P, P, E, E]));
    w.chat.attach(c);
    const log = logOf(w);
    for (let i = 0; i < 12; i++) c.emit('chat', { sender: 1, team: false, text: 'a' + i });
    check('at the bottom: a new line scrolls to the end', log.scrollTop === log.scrollHeight);
    log.scrollTop = 0;
    c.emit('chat', { sender: 1, team: false, text: 'b' });
    check('scrolled up to read: a line of somebody else does not take the place away', log.scrollTop === 0);
    c.emit('chat', { sender: 0, team: false, text: 'mine' });
    check('a line of one\'s own always scrolls to the end', log.scrollTop === log.scrollHeight);
}

{   // a line that arrives while the box is off screen is a toast too; not one of one's own, not one of the system, not one that is on screen
    const w = world();
    const c = fakeClient('k7m2xq', 0, roomOf([P, P, E, E]));
    w.chat.attach(c);
    w.state.onScreen = false;
    c.emit('chat', { sender: 1, team: false, text: 'hi' });
    c.emit('chat', { sender: 0, team: false, text: 'mine' });
    c.emit('joined', { type: 'joined', seat: 2, name: 'Eve' });
    same('only the line of somebody else, as "name: line"', w.toasts, ['Sam: hi']);
    w.state.onScreen = true;
    c.emit('chat', { sender: 1, team: false, text: 'again' });
    check('on screen: no toast', w.toasts.length === 1);
}

{   // the default of "on screen": the box's place in the window (the page's sticky START bar covers the bottom 140 px)
    const doc = { defaultView: { innerHeight: 800 }, createElement: node };
    const host = node('section');
    const toasts = [];
    const chat = C.mount({ doc, host, colours: Rules.COLOURS, say: () => true, toast: (t) => toasts.push(t) });
    const c = fakeClient('k7m2xq', 0, roomOf([P, P, E, E]));
    chat.attach(c);
    host.rect = { top: 100, bottom: 500 };
    c.emit('chat', { sender: 1, team: false, text: '1' });
    host.rect = { top: 900, bottom: 1300 };
    c.emit('chat', { sender: 1, team: false, text: '2' });
    host.rect = { top: 700, bottom: 1100 };
    c.emit('chat', { sender: 1, team: false, text: '3' });
    host.rect = { top: -500, bottom: -10 };
    c.emit('chat', { sender: 1, team: false, text: '4' });
    host.rect = { top: -200, bottom: 20 };
    c.emit('chat', { sender: 1, team: false, text: '5' });
    same('in the window: none; below it, under the START bar, above it: toasts; a corner of it above: none', toasts, ['Sam: 2', 'Sam: 3', 'Sam: 4']);
}

{   // sending: the typed line goes to the client, the field is emptied and keeps the focus; a line that the client does not take stays, with the hint; what cannot be shown is left out as it is typed
    const w = world();
    const c = fakeClient('k7m2xq', 0, roomOf([P, P, E, E]));
    w.chat.attach(c);
    const input = byId(w.host, 'chat-input'), hint = byId(w.host, 'chat-hint'), form = byTag(w.host, 'form');
    input.value = '  hello there  ';
    const e1 = form.fire('submit');
    same('a line is sent trimmed, once', w.state.said, ['hello there']);
    check('the page keeps the browser from sending the form (preventDefault) and the field is empty with its focus', e1.prevented === true && input.value === '' && input.focused >= 1 && hint.textContent === '');
    input.value = '   ';
    form.fire('submit');
    check('blanks are no line: nothing is sent', w.state.said.length === 1);
    input.value = '';
    form.fire('submit');
    check('an empty field sends nothing', w.state.said.length === 1);
    w.state.sayResult = false;
    input.value = 'too fast';
    form.fire('submit');
    check('a line that the client does not take (the pace) stays in the field, and the hint asks for a little slower', input.value === 'too fast' && hint.textContent === C.TEXT.tooFast && w.state.said.length === 2);
    input.fire('input');
    check('typing on clears the hint', hint.textContent === '');
    w.state.sayResult = true;
    form.fire('submit');
    check('the same line goes when the pace allows, and the hint stays empty', input.value === '' && hint.textContent === '' && w.state.said[2] === 'too fast');
    input.value = 'café ☺ ok';
    input.fire('input');
    check('a character that the game cannot show is left out as it is typed, and the hint says why', input.value === 'caf  ok' && hint.textContent === C.TEXT.accent);
    input.value = 'caf  ok!';
    input.fire('input');
    check('typing on with nothing left out clears the hint again', hint.textContent === '');
    input.value = 'é';
    input.fire('input');
    form.fire('submit');
    check('a field that held only characters that are left out sends nothing', input.value === '' && w.state.said.length === 3);
    input.value = 'draft';
    input.fire('input', { isComposing: true });
    input.value = 'drafté';
    input.fire('input', { isComposing: true });
    check('while a composition goes on (a predictive keyboard, an IME) the field is not rewritten', input.value === 'drafté');
    input.fire('compositionend');
    check('when it ends, what the game cannot show is left out', input.value === 'draft' && hint.textContent === C.TEXT.accent);
}

{   // the field is shut while the link is down and while START waits: no line is sent, nothing is typed; it opens again with the link
    const w = world();
    const room = roomOf([P, P, E, E]);
    const c = fakeClient('k7m2xq', 0, room);
    w.chat.attach(c);
    const input = byId(w.host, 'chat-input'), button = byTag(w.host, 'button'), form = byTag(w.host, 'form');
    c.status = 'offline';
    c.emit('status', 'offline');
    check('the link is down: the box stays, the field and the button are shut, the field says when it is back', w.host.hidden === false && input.disabled && button.disabled && input.placeholder === C.TEXT.down);
    input.value = 'typed while down';
    form.fire('submit');
    check('a submit while shut sends nothing (a browser may submit a field that a script shut)', w.state.said.length === 0);
    input.value = '';
    c.status = 'online';
    c.emit('welcome', { seat: 0, created: false, rejoin: true });
    check('the link is back: open again', !input.disabled && !button.disabled && input.placeholder === C.TEXT.placeholder);
    c.room = roomOf([P, P, E, E], { starting: true });
    c.emit('room', c.room);
    check('START pressed: shut, "Opening the game"', input.disabled && button.disabled && input.placeholder === C.TEXT.opening);
    c.room = roomOf([P, P, E, E]);
    c.emit('room', c.room);
    check('the start is given up: open again', !input.disabled && input.placeholder === C.TEXT.placeholder);
}

{   // the page changes its client: another room clears the lines, the same room keeps them, a room that was made again clears them, the old client is not heard
    const w = world();
    const a = fakeClient('aaaaaa', 0, roomOf([P, P, E, E]));
    w.chat.attach(a);
    a.emit('chat', { sender: 1, team: false, text: 'in room a' });
    const again = fakeClient('aaaaaa', 0, roomOf([P, P, E, E]));
    w.chat.attach(again);
    check('a new client for the same room (the window took the room back) keeps the lines', lineTexts(w).join('|') === 'Sam:in room a');
    a.emit('chat', { sender: 1, team: false, text: 'from the old client' });
    again.emit('chat', { sender: 1, team: false, text: 'from the new one' });
    check('the client that was let go is not heard any more', lineTexts(w).join('|') === 'Sam:in room a|Sam:from the new one');
    const b = fakeClient('bbbbbb', 0, roomOf([P, E, E, E]));
    w.chat.attach(b);
    same('a client of another room: the lines of the first are gone, the hint to send the link is back', lineTexts(w), [C.TEXT.alone]);
    b.room = roomOf([P, P, E, E]);
    b.emit('chat', { sender: 1, team: false, text: 'in room b' });
    b.emit('gone', {});
    same('a room that was lost and made again (gone): the lines of the dead room are gone', lineTexts(w).length, 1);
    check('(and the hint is the room\'s again)', lineTexts(w)[0] === C.TEXT.empty || lineTexts(w)[0] === C.TEXT.alone);
    const noRoom = fakeClient('cccccc', -1, null, 'connecting');
    w.chat.attach(noRoom);
    check('a client with no room yet (a link that was just made): the box is not there', w.host.hidden === true);
    noRoom.room = roomOf([P, P, E, E]);
    noRoom.status = 'online';
    noRoom.emit('welcome', { seat: 0, created: true, rejoin: false });
    check('the Welcome and the room open it', w.host.hidden === false);
    w.chat.clear();
    check('clear() empties the log (the hint comes back)', lineTexts(w).length === 1 && w.chat.lines().length === 0);
}

// ---- END OF SECTIONS ----
for (const name of Object.keys(RAW)) if (typeof RAW[name] === 'function') check('the check called ' + name + '()', called[name] > 0);
console.log('LobbyChat: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);

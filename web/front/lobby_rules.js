// The rules and the words of the front page's lobby (web/lobby.html): everything that decides what the page shows and sends, with no browser in it, so that node can run it
// (tests/scripts/web_lobby_rules_check.js). The page reads a Room message of network protocol 16 (web/front/lobby_net.js) through modelOf(), asks viewOf() what each colour card
// and the map side show, and asks planWith(), moveOf() and the team functions what to send. The pictures that these words and rules belong to are the lobby's (the owner's cards).
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsLobbyRules, node gets module.exports.
//   * Seats are the server's: Green 0, Red 1, Blue 2, Black 3. The cards lie Black, Green, Red, Blue (GRID: the four hills of the maps, top left to bottom right) and are built in that
//     reading order, so that Tab and a screen reader follow what is on screen.
//   * Nothing here puts a name into markup: a name is text, and the page sets it with textContent (or esc() where it builds a string).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsLobbyRules = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var COLOURS = [{ id: 'green', name: 'Green' }, { id: 'red', name: 'Red' }, { id: 'blue', name: 'Blue' }, { id: 'black', name: 'Black' }];
    var GRID = [3, 0, 1, 2];
    var PLAYERS = 4;
    var NO_TEAM = 255;
    var SLOT = { Empty: 0, Host: 1, Client: 2, Bot: 3 };          // lobby_net.js SLOT
    var KINDS = ['open', 'easy', 'medium', 'hard', 'nobody'];     // the plan's values 0 - 4 (PLAN in lobby_net.js)

    // The maps of the original game as the server names them (--demo-maps) and the Map Info line of each, as the game's setup screen shows it (tests/scripts/test_web_front.py reads them from Original-Ants/Maps)
    var MAPS = [
        { key: 'tiny', name: 'Tiny', file: 'TINY.LVL', info: 'Tiny map with no PowerUps (6 min)' },
        { key: 'small', name: 'Small', file: 'SMALL.LVL', info: 'Small map for fast game (8 min)' },
        { key: 'medium', name: 'Medium', file: 'MEDIUM.LVL', info: 'Intermediate map (10 min)' },
        { key: 'gauntlet', name: 'Gauntlet', file: 'GAUNTLET.LVL', info: 'Race for your life! (10 min)' },
        { key: 'treasure', name: 'Treasure', file: 'TREASURE.LVL', info: "One person's trash... (12 min)" },
        { key: 'islands', name: 'Islands', file: 'ISLANDS.LVL', info: 'Island hopping, expert map (12 min)' }
    ];
    // Treasure is the map that is played most: it is the default of everything (the owner's request)
    var DEFAULT_MAP_KEY = 'treasure';
    var NAMES = ['Maple', 'Clover', 'Pebble', 'Sorrel', 'Fern', 'Bramble', 'Juniper', 'Flint', 'Willow', 'Cedar', 'Moss', 'Thistle'];

    function mapByKey(key) {
        var k = typeof key === 'string' ? key.toLowerCase() : '';
        for (var i = 0; i < MAPS.length; i++) if (MAPS[i].key === k) return MAPS[i];
        return null;
    }
    // The map of a Room message's file name ("TREASURE.LVL", any case), or null for one that the page does not list (the room's own: shown as it is said)
    function mapOfFile(file) {
        var f = typeof file === 'string' ? file.toUpperCase() : '';
        for (var i = 0; i < MAPS.length; i++) if (MAPS[i].file === f) return MAPS[i];
        return null;
    }

    // ---- names ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The rules of a name, the same as the game page's own name step (the NAME block of web/shell.html and web/lobby.html, which tests/scripts/web_lobby_rules_check.js compares with this): printable ASCII only,
    // the blanks at both ends cut off, at most 32 characters, nothing that starts with "Bot (" (blanks and case ignored). Empty is fine: the page picks one. { ok: true, name } or { ok: false, why }.
    function nameCheck(text) {
        var raw = typeof text === 'string' ? text.replace(/[\t\r\n]/g, '') : '';
        if (/[^\x20-\x7e]/.test(raw)) return { ok: false, why: 'A name can hold printable ASCII characters only: letters, digits and signs without accents.' };
        var name = raw.replace(/^ +/, '').replace(/ +$/, '');
        if (name.length > 32) return { ok: false, why: 'A name has at most 32 characters.' };
        if (name.replace(/ /g, '').toLowerCase().indexOf('bot(') === 0) return { ok: false, why: 'A name that starts with "Bot (" is for computer players. Please choose another name.' };
        return { ok: true, name: name };
    }
    // A name for a visitor who typed none: one of the NAMES, `random` being a number from 0 up to (not including) 1
    function pickName(random) {
        var r = typeof random === 'number' && random >= 0 && random < 1 ? random : 0;
        return NAMES[Math.floor(r * NAMES.length)];
    }

    // ---- room codes -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // The code of a room that this page makes: six letters and numbers from an alphabet without look-alikes (31 symbols: no i, l, o, 0 or 1), from `values` (six whole numbers from the browser's random generator).
    // It is a name and nothing more. Screens show it in two groups of three (codeText); links, fields and the server keep the plain code.
    var CODE_CHARS = 'abcdefghjkmnpqrstuvwxyz23456789';
    function makeCode(values) {
        var out = '';
        var n = CODE_CHARS.length;
        for (var i = 0; i < 6; i++) {
            var v = Math.floor(Number(values[i]));
            out += CODE_CHARS.charAt(isFinite(v) ? ((v % n) + n) % n : 0);        // (a number that is negative, or no number, still gives a symbol)
        }
        return out;
    }
    function codeText(code) {
        var text = typeof code === 'string' ? code : '';
        return text.length === 6 ? text.slice(0, 3) + ' ' + text.slice(3) : text;
    }
    // A code that was typed, pasted or linked: blanks and capital letters do not matter ("K7M 2xq" is k7m2xq), and what is left must be 1 to 32 letters, digits, - and _ (a code that the server
    // takes: the older codes were up to eight characters and keep working). { ok: true, code } or { ok: false, why } (why is the line under the field).
    function typedCode(text) {
        var code = typeof text === 'string' ? text.replace(/\s+/g, '').toLowerCase() : '';
        if (code === '') return { ok: false, why: 'Type or paste the code first.' };
        if (!/^[a-z0-9_-]{1,32}$/.test(code)) return { ok: false, why: 'A room code has letters and numbers only, like k7m 2xq.' };
        return { ok: true, code: code };
    }

    // ---- what a Room message shows --------------------------------------------------------------------------------------------------------------------------------------------------------------
    function levelOfPlan(p) { return p === 1 ? 'easy' : p === 2 ? 'medium' : p === 3 ? 'hard' : ''; }
    function levelWord(level) { return level === 'easy' ? 'Easy' : level === 'medium' ? 'Medium' : 'Hard'; }
    function article(level) { return level === 'easy' ? 'an ' : 'a '; }                       // an Easy bot, a Medium bot, a Hard bot
    function listOf(a) { return a.length < 2 ? a.join('') : a.slice(0, -1).join(', ') + ' and ' + a[a.length - 1]; }
    // The two seats of a team pair as the switches: both on Team 1 (the room holds the pair only; a switch that is pressed alone is the leader's page's own until a second one joins it)
    function sidesOfRoom(room) {
        var out = [0, 0, 0, 0];
        if (room.teamA !== NO_TEAM && room.teamB !== NO_TEAM && room.teamA < PLAYERS && room.teamB < PLAYERS) { out[room.teamA] = 1; out[room.teamB] = 1; }
        return out;
    }
    // The room as the page shows it: { you, leader (-1: none), guest, starting, map (key or ''), mapFile, slots: [{ kind: person | open | bot | nobody, ... }], sides }.
    // A colour that a person holds shows the person; any other shows what the leader's plan says (the plan of a colour that a person holds is what comes back when the person leaves).
    function modelOf(room) {
        var leader = room.leader === 255 ? -1 : room.leader, slots = [];
        for (var s = 0; s < PLAYERS; s++) {
            var st = room.slots[s].state, plan = room.plan[s];
            if (st === SLOT.Host || st === SLOT.Client) {
                slots.push({ kind: 'person', name: room.slots[s].name, leader: s === leader, me: s === room.you, ingame: s !== room.you && ((room.inGame >> s) & 1) === 1 });
            } else if (st === SLOT.Bot || (plan >= 1 && plan <= 3)) {
                slots.push({ kind: 'bot', level: levelOfPlan(plan) || 'medium' });
            } else slots.push({ kind: plan === 4 ? 'nobody' : 'open' });
        }
        var m = mapOfFile(room.map);
        return { you: room.you, leader: leader, guest: room.you !== leader, starting: !!room.starting, map: m ? m.key : '', mapFile: room.map, slots: slots, sides: sidesOfRoom(room) };
    }
    function playing(model) {
        var out = [];
        for (var s = 0; s < PLAYERS; s++) if (model.slots[s].kind === 'person' || model.slots[s].kind === 'bot') out.push(s);
        return out;
    }
    function hostName(model) { return model.leader >= 0 && model.slots[model.leader].kind === 'person' ? model.slots[model.leader].name : 'The host'; }
    function nameOf(model, i) {
        var s = model.slots[i];
        return s.kind === 'person' ? s.name : s.kind === 'bot' ? levelWord(s.level) + ' bot' : s.kind === 'open' ? 'the open ' + COLOURS[i].name : 'the unused ' + COLOURS[i].name;
    }
    // What a card says of a person who sits in it (the toast of a move)
    function nameAt(model, i, you) {
        var s = model.slots[i];
        return s.kind === 'person' ? (you ? 'You' : s.name) : s.kind === 'bot' ? 'The ' + levelWord(s.level) + ' bot' : 'The ' + (s.kind === 'open' ? 'open ' : 'unused ') + COLOURS[i].name;
    }

    // ---- Team 1 and Team 2 (the rules of the live match card, PR #27, which this page replaced) ---------------------------------------------------------------------------------------
    // Every colour held by a person or a computer player has the two buttons when three or four play. A team is two colours on the same button; with four playing the other two colours are the other
    // team and show their button lit although nobody pressed it; with three the third plays alone. The team goes with the player: a move or a swap carries it along (the room does that).
    // `sides` is four numbers (0 for neither, 1 or 2), `play` the colours that play.
    function sidesFix(sides, play) {
        var out = [0, 0, 0, 0];
        if (play.length < 3 || !Array.isArray(sides)) return out;
        play.forEach(function (s) { out[s] = sides[s] === 1 || sides[s] === 2 ? sides[s] : 0; });
        var crowded = [1, 2].some(function (k) { return out.filter(function (v) { return v === k; }).length > 2; });
        return crowded ? [0, 0, 0, 0] : out;
    }
    function pairOf(sides, play) {
        for (var k = 1; k <= 2; k++) {
            var on = play.filter(function (s) { return sides[s] === k; });
            if (on.length === 2) return on;
        }
        return null;
    }
    // What the buttons show: each colour's own, and with four playing and two on one button the other two on the other one
    function shownOf(sides, play) {
        var shown = sides.slice(), pair = play.length === 4 ? pairOf(sides, play) : null;
        if (pair) play.forEach(function (s) { if (pair.indexOf(s) === -1) shown[s] = sides[pair[0]] === 1 ? 2 : 1; });
        return shown;
    }
    // May `seat` have button `side` on? It may when that button is lit for it already, or when fewer than two other colours have it lit
    function sideOpen(sides, play, seat, side) {
        if (play.length < 3 || play.indexOf(seat) === -1 || (side !== 1 && side !== 2)) return false;
        var shown = shownOf(sides, play);
        return shown[seat] === side || play.filter(function (s) { return s !== seat && shown[s] === side; }).length < 2;
    }
    // A press of button `side` of `seat`: one that is off goes on if the colour may have it, else the press is refused and `refusal` says why; one that is lit goes off, unless the others keep it lit
    // (the other team of a pair, or a press that a pair has made useless): then the teams go away. What is lit is what a person sees, so a press that is accepted always shows.
    function pressSide(sides, play, seat, side) {
        var fixed = sidesFix(sides, play), shown = shownOf(fixed, play);
        if (!sideOpen(fixed, play, seat, side)) {
            var pressed = play.filter(function (s) { return fixed[s] === side; }).length;
            return { sides: fixed, refusal: play.length < 3 || play.indexOf(seat) === -1 ? '' : pressed === 2 ? 'Team ' + side + ' has two colours already. Press one of them to take it off first.' : 'Team ' + side + ' is the other two colours already. Press a lit button to clear the teams first.' };
        }
        var out = fixed.slice();
        if (shown[seat] !== side) out[seat] = side;
        else { out[seat] = 0; if (shownOf(sidesFix(out, play), play)[seat] === side) out = [0, 0, 0, 0]; }      // a lit button that the others keep lit: the teams go away
        return { sides: sidesFix(out, play), refusal: '' };
    }
    // The two colours of the team that the room is told of, lower colour first: { teamA, teamB }, 255 twice for none
    function teamBytes(sides, play) {
        var pair = pairOf(sidesFix(sides, play), play);
        return pair ? { teamA: Math.min(pair[0], pair[1]), teamB: Math.max(pair[0], pair[1]) } : { teamA: NO_TEAM, teamB: NO_TEAM };
    }
    // The line under the colours: what the match will be
    function teamsText(sides, play, guest) {
        if (play.length < 3) return guest || play.length < 2 ? '' : 'Teams need three or four players.';
        var pair = pairOf(sides, play);
        if (!pair) return guest ? '' : 'Free for all. For a team, put two colours on the same team.';
        if (play.length === 4 && pair[0] !== 0) pair = play.filter(function (s) { return pair.indexOf(s) === -1; });
        var rest = play.filter(function (s) { return pair.indexOf(s) === -1; });
        function nm(s) { return COLOURS[s].name; }
        return 'Teams: ' + pair.map(nm).join(' + ') + ' against ' + rest.map(nm).join(' + ') + '.';
    }
    // The leader's own switches (`local`) and what the room says: a pair that the room holds is the truth; without one, the buttons that were pressed alone stay (the room cannot hold them)
    function reconcileSides(local, model) {
        var play = playing(model), server = sidesFix(model.sides, play);
        if (pairOf(server, play)) return server;
        var mine = sidesFix(local, play);
        return pairOf(mine, play) ? [0, 0, 0, 0] : mine;
    }

    // ---- what the leader asks the room (PlanMsg, SeatMove) --------------------------------------------------------------------------------------------------------------------------
    // The plan as the leader wants it: { map: file name or '' (the room's own stays), kinds: four plan values, teamA, teamB }. `change` can hold { map: key, seat + kind: a plan word, teams: sides }.
    function planWith(room, model, change) {
        var kinds = room.plan.slice(), teamA = room.teamA, teamB = room.teamB, map = '';
        if (change && change.map && mapByKey(change.map)) map = mapByKey(change.map).file;
        if (change && typeof change.seat === 'number' && KINDS.indexOf(change.kind) !== -1) kinds[change.seat] = KINDS.indexOf(change.kind);
        if (change && change.sides) {
            var t = teamBytes(change.sides, playingAfter(model, kinds));
            teamA = t.teamA; teamB = t.teamB;
        } else {
            // a colour that stops playing takes its team with it: the room's pair must be two colours that play
            var t2 = teamBytes(sidesOfPair(teamA, teamB), playingAfter(model, kinds));
            teamA = t2.teamA; teamB = t2.teamB;
        }
        return { map: map, kinds: kinds, teamA: teamA, teamB: teamB };
    }
    function sidesOfPair(a, b) { var out = [0, 0, 0, 0]; if (a !== NO_TEAM && b !== NO_TEAM && a < PLAYERS && b < PLAYERS) { out[a] = 1; out[b] = 1; } return out; }
    // The colours that would play if the plan were `kinds`: every colour that a person holds, and every other colour whose plan is a bot
    function playingAfter(model, kinds) {
        var out = [];
        for (var s = 0; s < PLAYERS; s++) if (model.slots[s].kind === 'person' || (kinds[s] >= 1 && kinds[s] <= 3)) out.push(s);
        return out;
    }
    // Does the room's team need a correction (it names a colour that does not play, or the colours are fewer than three)? The leader's page then sends the plan again with the teams cleared
    function teamNeedsFix(room, model) {
        if (room.teamA === NO_TEAM && room.teamB === NO_TEAM) return false;
        var play = playing(model);
        return play.length < 3 || play.indexOf(room.teamA) === -1 || play.indexOf(room.teamB) === -1;
    }
    // A colour dragged (or tapped) onto another colour. The room exchanges two colours completely when a person is in the move (SeatMove: the person, the plan and the team follow); two colours that no person
    // holds exchange their plans (the page sends the plan with the two exchanged, the team pair following). Returns { op: 'move', from, to } (from is the person), { op: 'plan', plan } or null.
    function moveOf(room, model, from, to) {
        if (from === to || from < 0 || to < 0 || from >= PLAYERS || to >= PLAYERS) return null;
        var a = model.slots[from], b = model.slots[to];
        if (a.kind === 'person') return { op: 'move', from: from, to: to };
        if (b.kind === 'person') return { op: 'move', from: to, to: from };
        var kinds = room.plan.slice();
        kinds[from] = room.plan[to]; kinds[to] = room.plan[from];
        var moved = function (seat) { return seat === from ? to : seat === to ? from : seat; };
        var x = room.teamA === NO_TEAM ? NO_TEAM : moved(room.teamA), y = room.teamB === NO_TEAM ? NO_TEAM : moved(room.teamB);
        return { op: 'plan', plan: { map: '', kinds: kinds, teamA: x === NO_TEAM ? NO_TEAM : Math.min(x, y), teamB: x === NO_TEAM ? NO_TEAM : Math.max(x, y) } };
    }
    // Does START play on this computer? With nobody else in the match (no other person, no computer player) the match is a game for one: a room of the game server needs two to start, and the game page
    // plays it alone (the original's single player).
    function startsAlone(model) { return playing(model).length === 1; }

    // ---- what each card and the side show -------------------------------------------------------------------------------------------------------------------------------------------
    // ctx: { sides (the leader's own switches), picked (the colour that was tapped to be moved, -1), removing (the colour of the open question, -1), offline (false, 'lost', 'none' or 'join'), refusal }.
    // The cards come back in seat order; the page builds them in GRID order. A card: { seat, colour, kind, who: [{ text, cls }], mode (the plan word of a colour that the leader can set, else ''), stat, statCls,
    // pencil, remove (a Remove button), ask (the question is open), grip, tab (read-only team tab), teams: [{ side, pressed, disabled }] or null, label, drop (what a tap on it does while a colour is picked) }.
    function viewOf(model, ctx) {
        var g = model.guest, busy = model.starting, off = ctx.offline || false, ro = g || busy || !!off;
        var play = playing(model), sides = sidesFix(g ? model.sides : ctx.sides, play), shown = shownOf(sides, play), can = play.length >= 3;
        var picked = ctx.picked === undefined ? -1 : ctx.picked, removing = ctx.removing === undefined ? -1 : ctx.removing;
        if (removing >= 0 && (ro || !model.slots[removing] || model.slots[removing].kind !== 'person' || model.slots[removing].me)) removing = -1;
        var cards = [];
        for (var i = 0; i < PLAYERS; i++) {
            var s = model.slots[i], c = COLOURS[i], human = s.kind === 'person';
            var card = { seat: i, colour: c, kind: s.kind, who: [], mode: '', stat: '', statCls: 'pstat', pencil: false, remove: false, ask: false, grip: !ro, tab: '', teams: null, label: '', drop: '' };
            if (human) {
                card.pencil = !!s.me && !busy && !off;
                card.who.push({ text: s.name, cls: 'nm' });
                if (s.leader && s.me) { card.who.push({ text: 'You', cls: 'you' }); card.stat = 'Host. You start the match.'; }
                else if (s.leader) { card.who.push({ text: 'Host', cls: 'you' }); card.stat = 'Starts the match.'; }
                else if (s.me) { card.who.push({ text: 'You', cls: 'you' }); card.stat = 'Waiting for the host to start.'; }
                else card.stat = 'In the room';
                if (!ro && !s.me && !s.leader) { card.remove = removing !== i; card.ask = removing === i; }
                if (busy) { card.stat = s.ingame ? 'In the game' : 'Opening the game…'; card.statCls += s.ingame ? ' in' : ' wait'; }      // START is pressed: whose game is open already
            } else {
                card.stat = s.kind === 'bot' ? 'Computer player' : s.kind === 'open' ? 'Takes the next player who joins.' : 'Stays out of the match.';
                var word = s.kind === 'bot' ? s.level : s.kind;
                if (ro) card.who.push({ text: s.kind === 'bot' ? levelWord(s.level) + ' bot' : s.kind === 'open' ? 'Open' : 'Nobody', cls: '' });
                else card.mode = word;
            }
            if (can && play.indexOf(i) !== -1) {
                if (ro) { if (shown[i]) card.tab = 'Team ' + shown[i]; }
                else card.teams = [1, 2].map(function (k) { return { side: k, pressed: shown[i] === k, disabled: shown[i] !== k && !sideOpen(sides, play, i, k) }; });
            }
            card.label = c.name + ': ' + (human ? s.name + (s.leader && s.me ? ', you, host' : s.leader ? ', host' : s.me ? ', you' : '') : (card.who.length ? card.who[0].text : s.kind));
            card.drop = picked >= 0 ? 'Tap to put ' + nameOf(model, picked) + ' here' : '';
            cards.push(card);
        }
        var others = model.slots.some(function (x) { return x.kind === 'person' && !x.me; });
        var notes = {
            invite: busy ? 'New players cannot join while the game opens.' : 'Players who open this link take the next free colour.',
            slots: busy ? 'The match begins as soon as all of you are in the game.' : off === 'lost' ? 'Your colour is kept for a minute while the page gets you back in.' : off ? ''
                : g ? hostName(model) + ' arranges the colours and starts the match.'
                : picked >= 0 ? 'Now tap the colour where ' + nameOf(model, picked) + ' goes (tap the same player again to let go).'
                : others ? 'Drag a player onto another colour to move them. A colour that is taken swaps places.' : 'Waiting for players. Drag a player onto another colour to move them.',
            teams: ctx.refusal || teamsText(sides, play, g),
            teamsWarn: !!ctx.refusal
        };
        return { guest: g, busy: busy, ro: ro, offline: off, heading: off === 'join' ? 'Joining a room' : g ? hostName(model) + '’s room' : 'Your room', cards: cards, notes: notes, removing: removing, sides: sides, play: play, side: sideView(model, ctx, play) };
    }
    // The map's side: the map, START and the line under it (who plays, and what an open colour does)
    function sideView(model, ctx, play) {
        var g = model.guest, busy = model.starting, away = ctx.offline || false;
        var m = mapByKey(model.map) || mapByKey(DEFAULT_MAP_KEY);
        var names = [], open = [], short = [];
        model.slots.forEach(function (s, i) {
            if (s.kind === 'person') {
                var me = s.me;
                names.push(me ? 'you' : s.name); short.push(me ? 'You' : s.name);
            } else if (s.kind === 'bot') { names.push(article(s.level) + levelWord(s.level) + ' bot'); short.push(levelWord(s.level) + ' bot'); }
            else if (s.kind === 'open') open.push(COLOURS[i].name);
        });
        var list = listOf(names); list = list.charAt(0).toUpperCase() + list.slice(1);
        var text = names.length === 1 ? 'A game for one' + (open.length ? '.' : ': only your colony is on the map.') : list + ' play on ' + m.name + '.';
        if (open.length) text += ' ' + listOf(open) + (open.length > 1 ? ' are open and stay empty' : ' is open and stays empty') + ' unless a player joins first.';
        var host = hostName(model);
        var label = busy ? 'Getting ready…' : away === 'lost' ? 'Reconnecting…' : away ? 'START!' : g ? 'Waiting for ' + host : 'START!';
        var plan = busy ? (g ? host + ' pressed START. ' : '') + 'Opening the game for everybody. The match begins as soon as all of you are in.'
            : away === 'lost' ? 'START comes back as soon as you are in.' : away === 'join' ? '' : away ? 'There is no room yet.'
            : g ? host + ' starts the match when everybody is in. ' + (names.length > 1 ? list + ' play on ' + m.name + '.' : '') : text;
        var shortText = busy ? (g ? host + ' pressed START' : 'Opening the game for everybody') : away === 'lost' ? 'Getting you back in' : away === 'join' ? '' : away ? 'No room yet'
            : g ? host + ' starts the match' : listOf(short) + ' · ' + m.name + (open.length ? ' · ' + open.length + ' open' : '');
        return { mapKey: m.key, mapName: m.name, mapInfo: m.info, mapDisabled: g || busy || !!away, startDisabled: g || busy || !!away, startBusy: busy, startLabel: label, plan: plan, planShort: shortText };
    }

    // ---- the strip at the top of the page: every message --------------------------------------------------------------------------------------------------------------------------------------
    // tone: '' green (the page changed something for you), 'warn' gold (something did not work); btn: the button's words ('' none). The sentences that start with the server's own words (`a.server`: the late
    // game, no place, the match that cannot start, the map) print what the server sent, then the page's own.
    var TEXT = {
        over: function (a) { return 'Welcome back. Your match on ' + a.map + ' is over. Everything is as you left it: change what you like and press START for another.'; },
        old: function (a) { return 'That room is gone, because everybody left, so this one is yours now' + (a && a.sameMap === false ? '' : ', with the same map') + '. Send the link on if you like.'; },
        host: function (a) { return a.guest ? a.leaver + ' left. ' + a.host + ' is the host now.' : a.leaver + ' left, so you are the host now. The map, the colours and START are yours.'; },
        late: function (a) { return a.server + ' Everybody is back in the room, as you left it. Press START to try again.'; },
        lateplayer: function (a) { return 'The match did not start, so everybody is back in the room. ' + a.host + ' can press START to try again.'; },
        noplace: function (a) { return a.server + ' Everybody is back in the room.'; },
        startsfailed: function (a) { return a.server + ' Everybody is back in the room.'; },
        maplost: function (a) { return a.server; },
        mapcolours: function (a) { return a.server; },
        removed: function () { return 'The host removed you from the room, so this one is yours now. Send the link on if you like.'; },
        full: function () { return 'That room is full, so this one is yours now. Send the link on if you like.'; },
        running: function () { return 'The match in that room has already started, so this room is yours instead. Send the link on if you like.'; },
        busy: function () { return 'The server is full right now, so there is no room for you yet. Try again in a minute.'; },
        lost: function () { return 'Connection lost. Getting you back in…'; },
        unreachable: function () { return 'Cannot reach the game server. Check your connection; trying again…'; },
        away: function () { return 'This room was opened in another window, so it is closed here.'; },
        version: function () { return 'A newer version of Ants is out. Reload the page to update.'; },
        noroom: function () { return 'There is no room with that code. Check it, or ask the host to send the link again.'; },
        notice: function (a) { return a.server; }
    };
    // The lines under the "Have a code?" field when the code that was typed does not lead into a room (the first two are the page's own room and one that it is in already)
    var HINT = {
        own: 'That is your own room.',
        same: 'You are in that room already.',
        full: 'That room is full.',
        running: 'The match in that room has already started.'
    };
    var META = {
        host: { tone: '', btn: 'OK' }, old: { tone: '', btn: 'OK' }, over: { tone: '', btn: 'OK' }, late: { tone: 'warn', btn: 'OK' }, lateplayer: { tone: 'warn', btn: 'OK' }, noplace: { tone: 'warn', btn: 'OK' },
        startsfailed: { tone: 'warn', btn: 'OK' }, maplost: { tone: 'warn', btn: 'OK' }, mapcolours: { tone: 'warn', btn: 'OK' }, full: { tone: '', btn: 'OK' }, running: { tone: '', btn: 'OK' },
        removed: { tone: '', btn: 'OK' }, lost: { tone: 'warn', btn: '' }, unreachable: { tone: 'warn', btn: '' }, busy: { tone: 'warn', btn: 'Try again' }, away: { tone: 'warn', btn: 'Use this window' },
        version: { tone: 'warn', btn: 'Reload' }, notice: { tone: 'warn', btn: 'OK' }
    };
    // What a banner's button does, in words (the page does it; a test reads the words)
    var ACTS = { busy: 'This asks the server again.', away: 'This window takes the room back, and the other window closes.', version: 'This reloads the page.' };
    // What a line that the room itself said (a Chat from sender 255) means: { kind, server } for the sentences of the START (the words of include/ants_net/protocol.hpp, to the leader), 'over' (with the map's name) for the line that a person who comes back to the room after its match is sent once, 'moved' for the line that a moved player
    // is sent (the page shows no strip for it), else the line as a notice
    function noticeKind(text) {
        var t = typeof text === 'string' ? text : '';
        if (/^These games did not come in time: /.test(t) || /'s game did not come in time\.$/.test(t)) return { kind: 'late', server: t };
        if (t.indexOf('The server has no place for another match right now') === 0) return { kind: 'noplace', server: t };
        if (t.indexOf('The match could not start a few times in a row') === 0) return { kind: 'startsfailed', server: t };
        if (t === 'This map could not be loaded: choose another map.') return { kind: 'maplost', server: t };
        if (t === 'This map cannot be played with these colours.') return { kind: 'mapcolours', server: t };
        var over = /^Your match on (.+) is over\.$/.exec(t);                                                                                   // the room that is back after a match (the file of the map, as the server says it)
        if (over) { var played = mapOfFile(over[1]); return { kind: 'over', server: t, map: played ? played.name : over[1].replace(/\.lvl$/i, '') }; }
        if (/^.+ moved you to [A-Za-z]+\.$/.test(t)) return { kind: 'moved', server: t };                                   // the leader moved this player: the colour and the toast say so already, there is no strip
        return { kind: 'notice', server: t };
    }
    // The banner for a kind: { kind, text, tone, btn }
    function banner(kind, args) {
        var meta = META[kind] || META.notice;
        return { kind: kind, text: TEXT[kind](args || {}), tone: meta.tone, btn: meta.btn };
    }

    return {
        COLOURS: COLOURS, GRID: GRID, MAPS: MAPS, DEFAULT_MAP_KEY: DEFAULT_MAP_KEY, NAMES: NAMES, KINDS: KINDS, CODE_CHARS: CODE_CHARS, TEXT: TEXT, HINT: HINT, META: META, ACTS: ACTS,
        mapByKey: mapByKey, mapOfFile: mapOfFile, nameCheck: nameCheck, pickName: pickName, makeCode: makeCode, codeText: codeText, typedCode: typedCode,
        levelWord: levelWord, article: article, listOf: listOf, modelOf: modelOf, playing: playing, hostName: hostName, nameOf: nameOf, nameAt: nameAt,
        sidesFix: sidesFix, pairOf: pairOf, shownOf: shownOf, sideOpen: sideOpen, pressSide: pressSide, teamBytes: teamBytes, teamsText: teamsText, reconcileSides: reconcileSides,
        planWith: planWith, teamNeedsFix: teamNeedsFix, moveOf: moveOf, startsAlone: startsAlone, viewOf: viewOf, sideView: sideView, noticeKind: noticeKind, banner: banner
    };
});

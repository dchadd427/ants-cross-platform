// "Play this map" of the map maker (docs/MAP_EDITOR.md, "Play this map"): a test game on the map that is open, against bots, in a tab of its own (the game page's side is front/test_game.js). This script is
// loaded the first time the button is pressed, so a player who never tests a map downloads nothing of it. Nothing here judges a map: the verdict is read from the answers of the map core's own check
// (its kind, its findings by their codes and teams) and the first reading of the file (the teams' start markers and hill cells); the bytes that the game gets are the open file's bytes as they are, copied,
// never written again by a script of the page (the page that plays them reads them back byte for byte). Names of maps and every sentence of the check are put in with textContent. A plain ES5 script that a page
// and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapPlay = factory();
})(this, function () {
    'use strict';

    var MAX_ROWS = 90;                                                                  // a Classic map plays up to 90 rows (where the original's pathfinder wall stands)
    var MAX_EXTENDED_SIDE = 1024;                                                       // an Extended map plays up to 1,024 a side and 262,144 cells (512 x 512)
    var MAX_EXTENDED_CELLS = 262144;
    var SEATS = [{ name: 'Green', dot: '#3aa655' }, { name: 'Red', dot: '#c8402f' }, { name: 'Blue', dot: '#4a63d8' }, { name: 'Black', dot: '#171717' }];       // by the game's seat numbers (the core's team numbers)
    var ORDER = [3, 2, 0, 1];                                                           // as the dialog lists them: Black, Blue, Green, Red
    var CHOICES = [['y', 'You'], ['e', 'Bot Easy'], ['m', 'Bot Medium'], ['h', 'Bot Hard'], ['n', 'Empty']];
    var TEAM_CODES = { hill_missing: 1, hill_without_entrance: 1, hills_several: 1, starting_ants_missing: 1 };      // the findings that are about one color's start (their `team` says which)
    var KEPT = 3;                                                                       // the hand-overs that this tab keeps (a game tab that is reloaded asks for its own again)
    var TOAST_MS = 7000;
    var HEAVY_START_ANTS = 2400;                                                        // above this many start ants (all colors together) a map is heavy: the server's OnlineLimits::kHeavyStartAnts (include/ants_server/online_limits.hpp), which a test keeps equal to this

    var MAX_ONLINE_ANTS = 8000;                                                         // a room plays no map above this many start ants: the server's OnlineLimits::kMaxStartAnts (a test keeps them equal)

    function commas(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    // The one warning for a heavy map, in both tabs: a warning only, nothing is refused and no button is turned off
    function heavyText(ants) { return 'This map starts with ' + commas(ants) + ' ants, so a game may run slowly, especially on phones and older computers.'; }
    // The refusal of a map that no online room plays, in the With other players tab (shown first, so that nothing is sent)
    function onlineText(ants) { return 'This map starts ' + commas(ants) + ' ants. An online room plays at most ' + commas(MAX_ONLINE_ANTS) + ', so it cannot host this map. You can still play it against bots on this device.'; }
    function capital(text) { text = String(text); return text.charAt(0).toUpperCase() + text.slice(1); }

    // What the check and the first reading say of the open map, as the dialog needs it: { ok, title, text, note, list, convert, seats, ants, heavy }
    //   seats[seat] = { seat, name, dot, ants, hills, usable, why: [the check's own sentences about this color] }
    //   ants = the start markers of the four colors added up, as the server counts them; heavy = more than HEAVY_START_ANTS (the dialog warns; the map is played as any other)
    // A map is refused for the game when: the check could not judge it, a Classic map has more than 90 rows, an Extended map is bigger than 512 x 512 (262,144 cells, 1,024 a side), a finding that is about no color stops it, or fewer than
    // two colors have a hill and starting ants (one seat for you and one for a bot)
    function verdict(check, read) {
        var out = { ok: false, title: 'This map cannot be played yet', text: '', note: '', list: [], convert: false, seats: [] };
        var rows = read && typeof read.rows === 'number' ? read.rows : 0, columns = read && typeof read.columns === 'number' ? read.columns : 0;
        var extended = check && check.kind === 'extended', findings = (check && check.findings) || [];
        if (!check || check.judged === false || !read || read.loaded === false) {
            out.title = 'This map cannot be checked';
            out.text = 'The check could not read this map, so it cannot be played.';
            return out;
        }
        if (!extended && rows > MAX_ROWS) {
            out.text = 'This map is bigger than the game can play yet. Test games work on Classic maps of up to ' + MAX_ROWS + ' rows for now.';
            out.note = 'What you can do: make the map ' + MAX_ROWS + ' rows or fewer, or start an Extended map from this one, then try again. Your map was not changed.';
            out.convert = false;
            return out;
        }
        if (extended && (rows > MAX_EXTENDED_SIDE || columns > MAX_EXTENDED_SIDE || rows * columns > MAX_EXTENDED_CELLS)) {
            out.text = 'This map is bigger than the game can play yet. Test games work on Extended maps of up to 512 x 512 cells (' + MAX_EXTENDED_SIDE + ' a side) for now.';
            out.note = 'What you can do: make a Classic copy that fits (Make a Classic copy...), or resize the map to 512 x 512 or smaller. Your map was not changed.';
            out.convert = true;
            return out;
        }
        var general = findings.filter(function (f) { return f.severity === 'fatal' && !(typeof f.team === 'number' && f.team >= 0); });
        if (general.length) {
            out.title = 'This map cannot be played';
            out.text = 'The check found a problem that stops the game from loading this map:';
            out.list = general.map(function (f) { return capital(f.message); });
            out.note = 'Fix it in the map maker (Check map shows where), then try again. Your map was not changed.';
            return out;
        }
        var teams = read.teams || [];
        for (var s = 0; s < 4; s++) {
            var t = teams[s] || {}, why = [], hills = t.hill_cells > 0 ? 1 : 0;
            findings.forEach(function (f) {
                if (f.team !== s) return;
                if (f.severity === 'fatal' || TEAM_CODES[f.code]) why.push(capital(f.message));
                if (f.code === 'hills_several' && f.count > 1) hills = f.count;
            });
            if (!(t.start_markers > 0) && !why.length) why.push(SEATS[s].name + ' has no starting ants');
            if (!(t.hill_cells > 0) && !why.length) why.push(SEATS[s].name + ' has no hill');
            out.seats.push({ seat: s, name: SEATS[s].name, dot: SEATS[s].dot, ants: t.start_markers || 0, hills: hills, usable: !why.length, why: why });
        }
        if (out.seats.filter(function (x) { return x.usable; }).length < 2) {
            out.title = 'This map cannot be played yet';
            out.text = 'A test game needs two colors that each have a hill and starting ants: one for you and one for a bot.';
            out.list = [].concat.apply([], out.seats.map(function (x) { return x.why; }));
            out.note = 'Put the missing hills and ants on the map (Hills and Ants tools), then try again. Your map was not changed.';
            return out;
        }
        out.ants = 0;
        for (var c = 0; c < 4; c++) out.ants += (teams[c] && teams[c].start_markers > 0) ? teams[c].start_markers : 0;
        out.heavy = out.ants > HEAVY_START_ANTS;
        out.online = out.ants <= MAX_ONLINE_ANTS;                                      // (false: only a room is refused)
        out.ok = true;
        return out;
    }

    // The four letters for the game's address, in the game's order (Green, Red, Blue, Black): y, e, m, h or n
    function code(choice) { return [0, 1, 2, 3].map(function (s) { return choice[s]; }).join(''); }

    // What the dialog shows at first: you at the first color that can play, one Medium bot at the next
    function firstChoice(seats) {
        var usable = seats.filter(function (x) { return x.usable; }), choice = ['n', 'n', 'n', 'n'];
        choice[usable[0].seat] = 'y';
        choice[usable[1].seat] = 'm';
        return choice;
    }

    // Can the choice be started? { ok, text }: exactly one seat is yours and at least one is a bot, on colors that can play
    function startable(choice, seats) {
        var you = 0, bots = 0, bad = false;
        for (var s = 0; s < 4; s++) {
            var c = choice[s];
            if (c === 'y') you++; else if (c !== 'n') bots++;
            if (c !== 'n' && !seats[s].usable) bad = true;
        }
        if (bad) return { ok: false, text: 'A color without a hill and starting ants cannot be used.' };
        if (you !== 1) return { ok: false, text: 'Choose one color for yourself.' };
        if (!bots) return { ok: false, text: 'Add at least one bot.' };
        return { ok: true, text: 'You and ' + bots + (bots === 1 ? ' bot.' : ' bots.') };
    }

    // "1 bot", "2 bots", "3 bots": You stay where you are, the bots take the next colors that can play (Medium); null when there are not so many
    function quick(choice, seats, count) {
        var next = ['n', 'n', 'n', 'n'], you = choice.indexOf('y'), left = count;
        if (you < 0) return null;
        next[you] = 'y';
        for (var i = 1; i < 4 && left; i++) {
            var s = (you + i) % 4;
            if (seats[s].usable) { next[s] = 'm'; left--; }
        }
        return left ? null : next;
    }

    function randomId(win) {
        var letters = 'abcdefghijklmnopqrstuvwxyz0123456789', out = '', bytes = null;
        try { bytes = win.crypto.getRandomValues(new win.Uint8Array(12)); } catch (e) { bytes = null; }
        for (var i = 0; i < 12; i++) out += letters.charAt((bytes ? bytes[i] : Math.floor(Math.random() * 256)) % 36);
        return out;
    }

    // The words on a row of a color that cannot play: plain, without the color (the row has it) and without the check's tile numbers; the check's own sentence only for any other reason
    function why(info) {
        var parts = [];
        if (!info.ants) parts.push('no starting ants');
        if (!info.hills) parts.push('no hill');
        return capital(parts.length ? parts.join(', ') : info.why[0] || 'cannot be used');
    }
    function hillText(n) { return n === 0 ? 'no hill' : n === 1 ? '1 hill' : n + ' hills'; }
    function antText(n) { return n === 1 ? '1 ant' : n + ' ants'; }
    function kbText(bytes) { return Math.max(1, Math.round((bytes || 0) / 1024)) + ' KB'; }

    // The hand-overs of this tab (a record for each started game): the game page asks for its own through window.antsTestGameHandoff(id), and tells the way back through window.antsTestGameBack(id)
    var kept = [];

    // env: { doc, win, dialogs, core (the map core), map() -> { name, bytes }, convert() }
    function open(env) {
        var doc = env.doc, win = env.win, dialogs = env.dialogs;
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function named(parent, before, name, after) { parent.appendChild(doc.createTextNode(before)); parent.appendChild(el('b', '', name)); parent.appendChild(doc.createTextNode(after)); return parent; }

        if (!win.antsTestGameHandoff) {
            win.antsTestGameHandoff = function (id) {
                for (var i = 0; i < kept.length; i++) if (kept[i].id === id) return { name: kept[i].name, bytes: kept[i].bytes };
                return null;
            };
            win.antsTestGameBack = function (id) {
                for (var i = 0; i < kept.length; i++) if (kept[i].id === id) return toast('Back from the test game. Your map is as you left it.');
            };
        }
        var toastTimer = 0;
        function toast(text) {
            var old = doc.getElementById('play-toast');
            if (old) old.parentNode.removeChild(old);
            var t = el('div', 'play-toast', text);
            t.id = 'play-toast';
            t.setAttribute('role', 'status');
            doc.body.appendChild(t);
            win.clearTimeout(toastTimer);
            toastTimer = win.setTimeout(function () { if (t.parentNode) t.parentNode.removeChild(t); }, TOAST_MS);
        }

        var m = env.map(), checked = env.core.checkMap(m.bytes, 'extended'), read = env.core.readReport(m.bytes, 'extended');
        if (checked.status !== 0 || read.status !== 0) return dialogs.message('The check did not run', capital(checked.status !== 0 ? checked.error : read.error) + '.');
        var v = verdict(checked.json, read.json), shape = read.json.rows + ' x ' + read.json.columns;

        if (!v.ok) {
            return dialogs.open({
                title: v.title, cls: 'narrow',
                build: function (body) {
                    body.appendChild(el('p', 'fatalmsg', v.text));
                    if (v.list.length) { var ul = el('ul', 'play-why'); v.list.forEach(function (t) { ul.appendChild(el('li', '', t)); }); body.appendChild(ul); }
                    if (v.note) body.appendChild(el('div', 'note', v.note));
                },
                buttons: (v.convert && env.convert ? [{ id: 'convert', text: 'Make a Classic copy...', ghost: true, onClick: function () { win.setTimeout(env.convert, 0); } }] : []).concat([{ id: 'ok', text: 'OK' }])
            });
        }

        var mode = 'bots', choice = firstChoice(v.seats), groups = [], quicks = [], tabs = [], botsPanel, friendsPanel, startButton, status;
        function refresh() {
            var go = startable(choice, v.seats);
            groups.forEach(function (g) {
                g.inputs.forEach(function (i) {
                    i.checked = choice[g.seat] === i.value;
                    i.disabled = !v.seats[g.seat].usable && i.value !== 'n';
                    i.parentNode.className = 'pc' + (i.checked ? ' on' : '') + (i.disabled ? ' dis' : '');          // (the style does not rely on :has)
                });
            });
            quicks.forEach(function (q) { q.button.disabled = !quick(choice, v.seats, q.count); });
            status.textContent = go.text;
            status.className = 'play-status' + (go.ok ? '' : ' no');
            if (tabs.length) {
                tabs[0].className = 'play-tab' + (mode === 'bots' ? ' on' : '');
                tabs[0].setAttribute('aria-selected', mode === 'bots' ? 'true' : 'false');
                tabs[1].className = 'play-tab' + (mode === 'friends' ? ' on' : '');
                tabs[1].setAttribute('aria-selected', mode === 'friends' ? 'true' : 'false');
                botsPanel.hidden = mode !== 'bots';
                friendsPanel.hidden = mode !== 'friends';
            }
            if (startButton) startButton.disabled = !go.ok;
            if (startButton && mode === 'friends') { startButton.disabled = !v.online; startButton.textContent = '\u25b6 Open a room with this map'; }
            else if (startButton) startButton.textContent = '\u25b6 Start the game';
        }
        function set(seat, value) {
            if (value === 'y') for (var s = 0; s < 4; s++) if (choice[s] === 'y') choice[s] = 'n';
            choice[seat] = value;
            refresh();
        }
        function setMode(nextMode) {
            mode = nextMode;
            refresh();
        }

        var started = false;
        function start(ctl) {
            var go = mode === 'friends' ? { ok: v.online } : startable(choice, v.seats);
            if (!go.ok || started) return;
            started = true;                                                          // (a second call opens no second tab)
            var rec = { id: randomId(win), name: m.name, bytes: m.bytes.slice() };
            var tab = win.open(mode === 'friends' ? '/lobby.html?testroom=' + rec.id : '/play.html?testmap=' + rec.id + '&seats=' + code(choice), '_blank');
            if (!tab) {
                ctl.close();
                return dialogs.message('The test game could not open', 'Your browser did not open a new tab for it. Allow pop-ups for this site and try again. Your map was not changed.');
            }
            kept.push(rec);                                                          // (the new tab asks for it when its page has loaded: this task is over by then)
            while (kept.length > KEPT) kept.shift();
            ctl.close();
        }

        function warning() { var w = el('p', 'play-status no', heavyText(v.ants)); w.setAttribute('role', 'status'); return w; }
        var ctl = dialogs.open({
            title: 'Play this map', cls: 'play',
            build: function (body) {
                var tablist = el('div', 'play-tabs');
                tablist.setAttribute('role', 'tablist');
                tablist.setAttribute('aria-label', 'Play mode');
                [['bots', 'Against bots', '1 player on this device'], ['friends', 'With other players', 'Host an online room with a share link']].forEach(function (t) {
                    var tb = el('button', 'play-tab');
                    tb.type = 'button';
                    tb.setAttribute('role', 'tab');
                    tb.setAttribute('data-mode', t[0]);
                    tb.appendChild(el('span', 'play-tab-t', t[1]));
                    tb.appendChild(el('span', 'play-tab-s', t[2]));
                    tb.addEventListener('click', function () { setMode(t[0]); });
                    tabs.push(tb);
                    tablist.appendChild(tb);
                });
                body.appendChild(tablist);
                botsPanel = el('div', 'play-panel-bots');
                botsPanel.appendChild(named(el('p', 'play-lead'), 'A test game on ', m.name, ' (' + shape + ') as it is now. Nothing is saved and no score is kept.'));
                if (v.heavy) botsPanel.appendChild(warning());              // (under the intro, above the seats, so that a phone shows it without scrolling)
                var qrow = el('div', 'play-quick');
                qrow.appendChild(el('span', 'play-ql', 'Quick:'));
                [1, 2, 3].forEach(function (n) {
                    var b = el('button', 'btn sm', n + (n === 1 ? ' bot' : ' bots'));
                    b.type = 'button';
                    b.addEventListener('click', function () { var next = quick(choice, v.seats, n); if (next) { choice = next; refresh(); } });
                    quicks.push({ button: b, count: n });
                    qrow.appendChild(b);
                });
                botsPanel.appendChild(qrow);
                var list = el('div', 'play-seats');
                ORDER.forEach(function (seat) {
                    var info = v.seats[seat], row = el('div', 'pseat' + (info.usable ? '' : ' off')), who = el('div', 'pwho'), dot = el('span', 'pdot');
                    dot.style.background = info.dot;
                    dot.setAttribute('aria-hidden', 'true');
                    var label = el('div', 'pname');
                    label.appendChild(el('span', 'pn', info.name));
                    label.appendChild(el('span', 'pf', info.usable ? antText(info.ants) + ', ' + hillText(info.hills) : why(info)));
                    who.appendChild(dot); who.appendChild(label);
                    var set1 = el('div', 'pchoices'), g = { seat: seat, inputs: [] };
                    set1.setAttribute('role', 'radiogroup');
                    set1.setAttribute('aria-label', info.name);
                    CHOICES.forEach(function (c) {
                        var lab = el('label', 'pc'), input = el('input');
                        input.type = 'radio'; input.name = 'pseat-' + seat; input.value = c[0];
                        input.addEventListener('change', function () { if (input.checked) set(seat, c[0]); });
                        lab.appendChild(input);
                        lab.appendChild(el('span', '', c[1]));
                        set1.appendChild(lab);
                        g.inputs.push(input);
                    });
                    groups.push(g);
                    row.appendChild(who); row.appendChild(set1); list.appendChild(row);
                });
                botsPanel.appendChild(list);
                botsPanel.appendChild(el('p', 'play-help', 'You play one color. The other colors are bots or empty. A color needs ants and a hill on the map to be used.'));
                status = el('p', 'play-status');
                status.setAttribute('role', 'status');
                botsPanel.appendChild(status);
                body.appendChild(botsPanel);

                friendsPanel = el('div', 'play-panel-friends');
                friendsPanel.hidden = true;
                var fcard = el('div', 'play-fcard'), fhead = el('div', 'play-fcard-head');
                fhead.appendChild(el('b', 'play-fname', m.name));
                fhead.appendChild(el('span', 'play-fbadge', checked.json.kind === 'extended' ? 'Extended map' : 'Classic map'));
                fhead.appendChild(el('span', 'play-fbadge gold', 'Unlisted test map'));
                fcard.appendChild(fhead);
                fcard.appendChild(el('p', 'play-ftext', 'Opens a room in a new tab with the map as it is right now (' + kbText(m.bytes.length) + '). Anyone with your room link or 6-character room code receives the map automatically when they join.'));
                fcard.appendChild(el('p', 'play-fnote', 'Not added to the public map list and not submitted for approval. Your editor tab stays open as you left it.'));
                friendsPanel.appendChild(fcard);
                if (!v.online) {                                             // (the refusal takes the place of the slow-game warning)
                    var no = el('p', 'play-status no', onlineText(v.ants));
                    no.setAttribute('role', 'status');
                    friendsPanel.appendChild(no);
                } else if (v.heavy) friendsPanel.appendChild(warning());
                var usableCount = v.seats.filter(function (x) { return x.usable; }).length;
                friendsPanel.appendChild(el('p', 'play-fhead', 'Colors available on this map (' + usableCount + ' of 4):'));
                var flist = el('div', 'play-fseats');
                ORDER.forEach(function (seat) {
                    var info = v.seats[seat], frow = el('div', 'pfseat' + (info.usable ? '' : ' off')), dot = el('span', 'pdot'), flabel = el('div', 'pname');
                    dot.style.background = info.dot;
                    dot.setAttribute('aria-hidden', 'true');
                    flabel.appendChild(el('span', 'pfn', info.name));
                    flabel.appendChild(el('span', 'pff', info.usable ? antText(info.ants) + ', ' + hillText(info.hills) + ' \u00b7 Ready' : why(info)));
                    frow.appendChild(dot); frow.appendChild(flabel);
                    flist.appendChild(frow);
                });
                friendsPanel.appendChild(flist);
                body.appendChild(friendsPanel);
                refresh();
            },
            buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'start', text: '\u25b6 Start the game', keepOpen: true, onClick: start }]
        });
        startButton = ctl.buttons.start;
        startButton.className += ' gold';
        refresh();
        return ctl;
    }

    return { verdict: verdict, startable: startable, quick: quick, firstChoice: firstChoice, code: code, open: open, heavyText: heavyText, HEAVY_START_ANTS: HEAVY_START_ANTS, MAX_ONLINE_ANTS: MAX_ONLINE_ANTS, onlineText: onlineText, MAX_ROWS: MAX_ROWS, MAX_EXTENDED_SIDE: MAX_EXTENDED_SIDE, KEPT: KEPT, kept: function () { return kept; } };
});

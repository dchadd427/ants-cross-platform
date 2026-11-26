// The sheet "Your name", the guest's name chip and its note (docs/HUB_NICKNAMES.md; docs/HOME_CHAT.md, "Your name"). A person who is logged in chooses a nickname and an optional tag there (the chat menu's button opens it); a guest
// types the name that the games show as "Name (Guest)", which this browser keeps (front/player_name.js). The hub checks every name (the `name` frame: hub/nicknames.py) and the page only says what the hub said: each field has the word of
// its refusal, and the page owns the sentences. Nothing here is markup: every text is set as text, and a name is drawn by AntsNameLetters.paint.
//   * ES5, no module system: a page gets AntsHomeNames, node gets module.exports (tests/scripts/web_home_names_check.js runs the model parts and the sheet against a fake page).
//   * mount({ doc, win, dock, bar (the node that holds the login chip), login (the chip), person (front/player_name.js), letters (AntsNameLetters), later (setTimeout) }) puts the guest's chip before the login chip, opens the sheet
//     when the chat menu's button (the dock's tap `names`) or the chip is pressed, and returns { open, close, state }.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeNames = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var NOTE_KEY = 'ants.guestnote';                 // localStorage: '1' once the guest has set a name or closed the note: it never shows again in this browser
    var CHECK_MS = 350;                              // the wait after the last key before the hub is asked
    var NICK_MIN = 3, NICK_MAX = 16, TAG_MIN = 2, TAG_MAX = 5;
    var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var TEXT = {
        title: 'Your name', close: 'Close', previewLabel: 'How others see you', save: 'Save',
        nickLabel: 'Nickname', nickHint: '3 to 16 characters', nickHelp: 'Letters and digits of one alphabet, spaces and . _ - \u2019',
        tagLabel: 'Tag', tagHint: 'optional, 2 to 5', tagHelp: 'Letters or digits. Shown as [TAG] before your name.',
        guestLabel: 'Name', guestHelp: '',
        firstNote: 'In the chat, the players list, rooms and match results.', guestNote: 'In rooms and in the game.',
        sinceNow: function (date) { return 'Your nickname since ' + date + '. You can change it now; after that you wait 30 days.'; }, sinceLater: function (date, free) { return 'Your nickname since ' + date + '. You can change it again on ' + free + '.'; },
        rules: ['Your nickname is yours alone: nobody else can use it.', 'You can change it once every 30 days. The old one stays yours for 30 days.', 'A tag can change once a day. Anyone can use any tag.', 'Names and tags are public and are kept with match results.'],
        guestRules: ['You cannot use the name of a player who is logged in.', 'Your name stays in this browser. Another browser starts over.', 'Names are public and are kept with match results.'],
        guestBox: 'Guests cannot chat in the lobby or get messages.', loginWith: 'Log in with Discord',
        menuFirst: 'You show as ', menuFirstEnd: ', your Discord name.', menuChoose: 'Choose a nickname', menuChange: 'Nickname and tag', menuNick: 'Nickname ', menuTag: ' \u00b7 Tag ', menuNoTag: ' \u00b7 No tag',
        popTitle: 'Please set a name', popOthers: function (name) { return 'Others see ' + name + ' now.'; },
        chipLabel: function (shown) { return 'You show as ' + shown + '. Set a name.'; }, chipLabelSet: function (name) { return 'Your name: ' + name + ' (Guest). Change it.'; },
        free: function (name) { return name + ' is free.'; }, fine: 'That name is fine.', same: 'That is your name now.', checking: 'Checking...',
        offline: 'Could not check just now. Try again in a moment.'
    };
    // The sentence for the word that the hub gave a field, or '' for none. kind: 'nick', 'tag' or 'guest'; `until` is seconds since 1970; `name` the text that was asked
    var WHY = {
        nick: {
            short: 'A nickname has at least ' + NICK_MIN + ' characters.', long: 'A nickname has at most ' + NICK_MAX + ' characters.', chars: 'Use letters and digits of one alphabet, spaces and . _ - \u2019 only. A sign cannot start or end it.',
            script: 'The game cannot draw these letters yet. Use Latin or Cyrillic letters.', alphabets: 'Use one alphabet in a nickname. This one mixes Latin and Cyrillic letters.', reserved: 'That name is reserved.',
            rude: 'That name is not allowed here.', lookalike: 'That looks too much like another name. Pick one that is clearly different.', busy: 'The hub could not check that just now. Try again in a moment.', rate: 'Not so fast. Try again in a moment.', empty: ''
        },
        tag: {
            short: 'A tag has at least ' + TAG_MIN + ' characters.', long: 'A tag has at most ' + TAG_MAX + ' characters.', chars: 'A tag is letters and digits only.', script: 'The game cannot draw these letters yet. Use Latin or Cyrillic letters.',
            alphabets: 'Use one alphabet in a tag. This one mixes Latin and Cyrillic letters.', reserved: 'That tag is reserved.', staff: 'That tag looks like a staff badge. Pick another one.', rude: 'That tag is not allowed here.',
            bot: 'That tag is reserved.', nick: 'Choose a nickname first: a tag stands before it.', busy: 'The hub could not check that just now. Try again in a moment.', rate: 'Not so fast. Try again in a moment.'
        },
        guest: {
            short: 'A name has at least ' + NICK_MIN + ' characters.', long: 'A name has at most ' + NICK_MAX + ' characters.', chars: 'Use letters and digits of one alphabet, spaces and . _ - \u2019 only. A sign cannot start or end it.',
            script: 'The game cannot draw these letters yet. Use Latin or Cyrillic letters.', alphabets: 'Use one alphabet in a name. This one mixes Latin and Cyrillic letters.', reserved: 'That name is reserved.',
            rude: 'That name is not allowed here.', lookalike: 'That looks too much like another name. Pick one that is clearly different.', busy: 'The hub could not check that just now. Try again in a moment.', rate: 'Not so fast. Try again in a moment.', empty: ''
        }
    };

    // ---- the pure parts ----
    // "4 Dec": the day and the month of a time in seconds since 1970 (the browser's own time zone)
    function dayOf(seconds, win) {
        var d = new (win && win.Date ? win.Date : Date)(seconds * 1000);
        return d.getDate() + ' ' + MONTHS[d.getMonth()];
    }
    // "in 6 hours" / "in 1 hour" / "in 40 minutes" / "tomorrow": how long until a time, in words, for a wait of up to a day
    function inWords(seconds, nowSeconds) {
        var left = Math.max(60, seconds - nowSeconds), hours = Math.ceil(left / 3600);
        if (left < 3600) return 'in ' + Math.ceil(left / 60) + (Math.ceil(left / 60) === 1 ? ' minute' : ' minutes');
        return 'in ' + hours + (hours === 1 ? ' hour' : ' hours');
    }
    // The line under a field for what the hub said of it: { text, tone } with tone 'bad' (a refusal), 'lock' (a wait: the field is greyed), 'ok' or '' (nothing). `field` is the hub's { ok, why?, until? }; `asked` is the text
    // that was asked (the name that is free is named); `time` is { date(seconds) -> "4 Dec", words(seconds) -> "in 6 hours" }; `current` is true when the field says what the account has now.
    function say(kind, field, asked, time, current) {
        if (!field || typeof field !== 'object') return { text: '', tone: '' };
        if (field.ok === true) {
            if (current) return { text: TEXT.same, tone: 'ok' };
            return { text: kind === 'guest' ? TEXT.fine : kind === 'nick' ? TEXT.free(asked) : '', tone: kind === 'tag' ? '' : 'ok' };
        }
        var why = typeof field.why === 'string' ? field.why : '';
        if (why === 'soon' && typeof field.until === 'number') {
            if (kind === 'tag') return { text: 'You changed your tag today. You can change it again ' + time.words(field.until) + '.', tone: 'lock' };
            return { text: 'You changed your nickname on ' + time.date(field.until - 30 * 86400) + '. You can change it again on ' + time.date(field.until) + '.', tone: 'lock' };       // (a nickname changes once in 30 days: the change was 30 days before the end of the wait)
        }
        if (why === 'taken') return { text: asked + ' is taken. Pick another one.', tone: 'bad' };
        var table = WHY[kind] || WHY.nick;
        return { text: Object.prototype.hasOwnProperty.call(table, why) ? table[why] : TEXT.offline, tone: 'bad' };
    }
    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, bar = env.bar, login = env.login, person = env.person, letters = env.letters || win.AntsNameLetters;
        var later = env.later || function (fn, ms) { return win.setTimeout(fn, ms); }, cancel = env.cancel || function (h) { win.clearTimeout(h); };
        var seq = 0, pending = 0, timer = null, sheet = null, chip = null, chipAv = null, chipName = null, pop = null, shownNote = false, loginOn = false;
        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined && text !== null) e.textContent = text; return e; }
        function put(node, kids) { kids.forEach(function (k) { if (k) node.appendChild(k); }); return node; }
        function storage() { try { return win.localStorage; } catch (e) { return null; } }
        function noteClosed() { try { var s = storage(); return !!s && s.getItem(NOTE_KEY) === '1'; } catch (e) { return false; } }
        function closeNote() { try { var s = storage(); if (s) s.setItem(NOTE_KEY, '1'); } catch (e) { /* a browser that keeps nothing: the note comes back with the page */ } shownNote = false; if (pop) { pop.remove ? pop.remove() : pop.parentNode && pop.parentNode.removeChild(pop); pop = null; } }
        function you() { return dock && dock.you ? dock.you() : null; }
        function now() { return Math.floor((win.Date ? win.Date.now() : Date.now()) / 1000); }
        var time = { date: function (s) { return dayOf(s, win); }, words: function (s) { return inWords(s, now()); } };
        function draw(node, name) { if (letters && letters.paint) letters.paint(doc, node, name); else node.textContent = name; return node; }
        function initial(name) { return letters && letters.initial ? letters.initial(name) : String(name).charAt(0).toUpperCase(); }

        // ---- the chip of a guest ----
        function live() { return !!dock && dock.state && dock.state() === 'live'; }
        function guestHere() { return live() && !you() && loginOn && !!login; }       // (the hub's welcome says that it has a login: the Log in button may not be shown yet when the welcome comes)
        function shownName() { return person.guestShown(); }
        function paintChip() {
            if (!guestHere()) { if (chip) chip.hidden = true; if (pop) closePopFor(); return; }
            if (!chip) {
                chip = el('button', 'btn nm-gchip'); chip.type = 'button';
                chipAv = el('span', 'gc-av'); chipName = el('span', 'gc-cn'); chip.appendChild(chipAv); chip.appendChild(chipName);
                chip.addEventListener('click', function () { open(); });
                if (login.parentNode) login.parentNode.insertBefore(chip, login); else bar.appendChild(chip);
            }
            var typed = person.typed(), label = typed || shownName();
            chip.hidden = false;
            chipAv.textContent = initial(label);
            chipName.textContent = label;
            chip.setAttribute('aria-label', typed ? TEXT.chipLabelSet(typed) : TEXT.chipLabel(shownName()));
            if (!typed && !noteClosed() && !sheet) showNote(); else if (pop) closePopFor();
        }
        function closePopFor() { if (pop) { if (pop.parentNode) pop.parentNode.removeChild(pop); pop = null; } }
        // The note under the chip: "Please set a name" until a name is saved or it is closed
        function showNote() {
            if (pop || !chip) return;
            pop = el('div', 'nm-pop'); pop.setAttribute('role', 'status');
            put(pop, [el('span', 'nm-pop-t', TEXT.popTitle), el('span', 'nm-pop-s', TEXT.popOthers(shownName()))]);
            var x = el('button', 'rs-x'); x.type = 'button'; x.setAttribute('aria-label', TEXT.close);
            x.addEventListener('click', function (e) { if (e && e.stopPropagation) e.stopPropagation(); closeNote(); });
            pop.appendChild(x);
            pop.addEventListener('click', function () { open(); });
            doc.body.appendChild(pop);
            placePop();
        }
        function placePop() {
            if (!pop || !chip || !chip.getBoundingClientRect) return;
            var r = chip.getBoundingClientRect(), w = pop.getBoundingClientRect().width || 268, vw = (doc.documentElement && doc.documentElement.clientWidth) || win.innerWidth || 1000;
            var left = Math.max(10, Math.min(r.left + r.width / 2 - 36, vw - 10 - w));
            pop.style.left = left + 'px'; pop.style.top = Math.round(r.bottom + 10) + 'px';
            pop.style.setProperty('--ax', Math.round(r.left + r.width / 2 - left) + 'px');
        }

        // ---- the sheet ----
        function open() {
            if (sheet) return;
            var me = you(), mode = me ? 'login' : 'guest';
            if (mode === 'guest' && !guestHere()) return;
            closePopFor();
            var s = sheet = { mode: mode, nick: null, tag: null, answer: null, typed: { nick: '', tag: '' }, node: null, saving: false };
            var back = el('div', 'rs-back nm-back'), box = el('div', 'rs nm-sheet'); box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-labelledby', 'nm-title');
            var top = el('div', 'rs-top'), h = el('h3', '', TEXT.title); h.id = 'nm-title'; top.appendChild(h);
            var x = el('button', 'rs-x'); x.type = 'button'; x.setAttribute('aria-label', TEXT.close); x.addEventListener('click', close); top.appendChild(x); box.appendChild(top);
            // the preview
            var prev = el('div', 'nm-prev'), av = el('span', 'nm-av' + (mode === 'guest' ? ' nm-gav' : '')), pn = el('span', 'nm-pn'), ps = el('span', 'nm-ps');
            put(prev, [el('span', 'nm-pl', TEXT.previewLabel), av, pn, ps]); box.appendChild(prev);
            s.av = av; s.pn = pn; s.ps = ps;
            // the fields
            function field(label, hint, id, max, help) {
                var f = el('div', 'nm-f'), lab = el('label'); lab.setAttribute('for', id); lab.appendChild(doc.createTextNode(label)); lab.appendChild(el('small', '', hint)); f.appendChild(lab);
                var wrap = el('div', 'nm-in'), input = el('input'); input.id = id; input.type = 'text'; input.setAttribute('autocomplete', 'off'); input.setAttribute('autocapitalize', 'off'); input.setAttribute('spellcheck', 'false'); input.setAttribute('maxlength', String(max * 2));
                var count = el('span', 'nm-cnt', '0 / ' + max);
                put(wrap, [input, count]); f.appendChild(wrap);
                var msg = el('p', 'nm-msg'); msg.hidden = true; var helper = el('p', 'nm-h', help); helper.hidden = !help;
                put(f, [msg, helper]);
                return { box: f, wrap: wrap, input: input, count: count, msg: msg, help: helper, max: max };
            }
            if (mode === 'login') {
                s.nick = field(TEXT.nickLabel, TEXT.nickHint, 'nm-nick', NICK_MAX, TEXT.nickHelp);
                s.tag = field(TEXT.tagLabel, TEXT.tagHint, 'nm-tag', TAG_MAX, TEXT.tagHelp);
                s.nick.input.value = me.nick || ''; s.tag.input.value = me.tag || '';
                put(box, [s.nick.box, s.tag.box]);
            } else {
                s.nick = field(TEXT.guestLabel, TEXT.nickHint, 'nm-nick', NICK_MAX, TEXT.guestHelp);
                s.nick.input.value = person.typed();
                box.appendChild(s.nick.box);
            }
            var rules = el('ul', 'nm-rules');
            (mode === 'guest' ? TEXT.guestRules : TEXT.rules).forEach(function (t) { rules.appendChild(el('li', '', t)); });
            box.appendChild(rules);
            if (mode === 'guest') {
                var gb = el('div', 'nm-gbox'), dc = el('button', 'btn'); dc.type = 'button';
                dc.appendChild(el('i', 'gc-dc')); dc.appendChild(doc.createTextNode(TEXT.loginWith));
                dc.addEventListener('click', function () { close(); if (login && login.click) login.click(); });
                put(gb, [el('p', '', TEXT.guestBox), dc]); box.appendChild(gb);
            }
            var foot = el('div', 'nm-foot'), save = el('button', 'btn nm-save', TEXT.save); save.type = 'button'; save.disabled = true; save.addEventListener('click', function () { saveNow(); });
            foot.appendChild(save); box.appendChild(foot);
            s.save = save; s.node = back;
            back.appendChild(box); doc.body.appendChild(back);
            back.addEventListener('click', function (e) { if (e && e.target === back) close(); });
            box.addEventListener('keydown', function (e) { if (e && e.key === 'Escape') { if (e.stopPropagation) e.stopPropagation(); close(); } else if (e && e.key === 'Enter' && e.target && e.target.tagName === 'INPUT') { if (e.preventDefault) e.preventDefault(); if (!save.disabled) saveNow(); } });
            [s.nick, s.tag].forEach(function (f) { if (f) f.input.addEventListener('input', function () { typedChanged(); }); });
            if (win.visualViewport && win.visualViewport.addEventListener) { s.vv = function () { var small = (win.innerHeight || 0) - win.visualViewport.height > 150; box.className = 'rs nm-sheet' + (small ? ' nm-kbd' : ''); }; win.visualViewport.addEventListener('resize', s.vv); }
            paintPreview(); typedChanged(true);
            try { s.nick.input.focus(); } catch (e) { /* nothing to focus */ }
        }
        function close() {
            var s = sheet;
            if (!s) return;
            sheet = null;
            if (timer) { cancel(timer); timer = null; }
            pending = 0;
            if (s.vv && win.visualViewport && win.visualViewport.removeEventListener) win.visualViewport.removeEventListener('resize', s.vv);
            if (s.node && s.node.parentNode) s.node.parentNode.removeChild(s.node);
            paintChip();
        }
        // The preview: how the others see the person. With a name that the hub has just found good it is that name; else what the account has now (a guest: the typed name, or the number)
        function paintPreview(shown) {
            var s = sheet; if (!s) return;
            var me = you(), name, note;
            if (s.mode === 'guest') {
                var typed = person.typed();
                name = shown || (typed ? typed + ' (Guest)' : shownName());
                note = shown || typed ? TEXT.guestNote : TEXT.popOthers(shownName());
            } else {
                var current = me && me.nick ? (me.tag ? '[' + me.tag + '] ' : '') + me.nick : '';       // (how the person shows now, when they have a nickname)
                name = shown || current || (me ? me.name : '');
                note = !current || (shown && shown !== current) ? TEXT.firstNote : me.nickfree ? TEXT.sinceLater(time.date(me.nickat), time.date(me.nickfree)) : TEXT.sinceNow(time.date(me.nickat));
            }
            draw(s.pn, name); s.av.textContent = initial(name); s.ps.textContent = note;
        }
        function count(text) { var p = letters && letters.codePoints ? letters.codePoints(text) : null; return p ? p.length : String(text).length; }
        function typedChanged(first) {
            var s = sheet; if (!s) return;
            s.typed = { nick: s.nick.input.value, tag: s.tag ? s.tag.input.value : '' };
            s.nick.count.textContent = count(s.typed.nick) + ' / ' + NICK_MAX;
            if (s.tag) s.tag.count.textContent = count(s.typed.tag) + ' / ' + TAG_MAX;
            s.save.disabled = true; s.answer = null;
            if (timer) { cancel(timer); timer = null; }
            if (s.typed.nick.replace(/ /g, '') === '') { pending = 0; paintMessages(null); paintPreview(); return; }
            timer = later(function () { timer = null; ask(false); }, first ? 0 : CHECK_MS);       // (the hub is asked a moment after the last key; what was said before stays until the answer comes)
        }
        function frame(save) {
            var s = sheet;
            return s.mode === 'guest' ? { t: 'name', r: ++seq, nick: s.typed.nick, guest: true } : { t: 'name', r: ++seq, nick: s.typed.nick, tag: s.typed.tag, save: !!save };
        }
        function ask(save) {
            var s = sheet; if (!s) return;
            var f = frame(save);
            pending = f.r;
            if (!dock || !dock.send || dock.send(f) === false) { pending = 0; paintMessages({ offline: true }); }
        }
        // the hub's answer to a frame of this sheet: a check, or a save
        function heard(m) {
            var s = sheet;
            if (!s || !m || typeof m !== 'object' || m.r !== pending) return;
            pending = 0;
            if (m.why === 'rate') { paintMessages({ offline: true, rate: true }); return; }
            s.answer = m;
            if (m.saved === true) { saved(m); return; }
            paintMessages(m);
        }
        function paintMessages(m) {
            var s = sheet; if (!s) return;
            var me = you();
            function one(f, kind, field, asked, current) {
                var out = field ? say(kind, field, asked, time, current) : { text: '', tone: '' };
                if (m && m.offline) out = { text: m.rate ? WHY.nick.rate : TEXT.offline, tone: 'bad' };
                f.msg.hidden = out.text === ''; f.msg.textContent = out.text; f.msg.className = 'nm-msg ' + out.tone;
                f.help.hidden = !f.help.textContent || out.text !== '';
                f.wrap.className = 'nm-in' + (out.tone === 'bad' ? ' err' : out.tone === 'ok' ? ' ok' : out.tone === 'lock' ? ' off' : '');
                f.input.readOnly = out.tone === 'lock';
            }
            if (!m) { [s.nick, s.tag].forEach(function (f) { if (f) { f.msg.hidden = true; f.help.hidden = !f.help.textContent; f.wrap.className = 'nm-in'; f.input.readOnly = false; } }); s.save.disabled = true; return; }
            var asked = s.typed.nick.replace(/^ +| +$/g, ''), parts = m.ok === true && typeof m.shown === 'string' && letters && letters.split ? letters.split(m.shown) : null;
            var current = !!me && s.mode === 'login' && !!parts && me.nick === parts.name;
            one(s.nick, s.mode === 'guest' ? 'guest' : 'nick', m.nick, parts && s.mode === 'login' ? parts.name : asked, current);
            if (s.tag) one(s.tag, 'tag', m.tag, s.typed.tag, false);
            paintPreview(m.ok === true && !m.offline ? m.shown : undefined);
            var unchanged = !!me && s.mode === 'login' && !!parts && me.nick === parts.name && (me.tag || '') === (parts.tag ? parts.tag.slice(1, -1) : '');
            s.save.disabled = !(m.ok === true && !m.offline) || unchanged;
        }
        function saveNow() {
            var s = sheet; if (!s || s.save.disabled) return;
            if (s.mode === 'guest') {
                var name = s.typed.nick.replace(/^ +| +$/g, '');
                if (!person.setTyped(name)) return;
                closeNote(); close(); paintChip(); notify();
                return;
            }
            s.saving = true; s.save.disabled = true;
            ask(true);
        }
        function saved() { close(); }
        function notify() { try { if (env.changed) env.changed(); } catch (e) { /* the page goes on */ } }

        // ---- the dock ----
        if (dock && dock.tap) dock.tap(function (kind, m) {
            if (kind === 'names') open();
            else if (kind === 'name') heard(m);
            else if (kind === 'live' || kind === 'you' || kind === 'lost') { if (kind === 'live') loginOn = !!m && m.login === true; paintChip(); if (sheet && sheet.mode === 'login' && !you()) close(); }
            else if (kind === 'shown') paintChip();
        });
        if (win.addEventListener) win.addEventListener('resize', function () { placePop(); });
        paintChip();
        return { open: open, close: close, state: function () { return { sheet: !!sheet, chip: !!chip && !chip.hidden, note: !!pop }; }, heard: heard };
    }

    return { NOTE_KEY: NOTE_KEY, TEXT: TEXT, WHY: WHY, dayOf: dayOf, inWords: inWords, say: say, mount: mount };
});

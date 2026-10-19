// Send a map for approval, and My submissions (docs/MAP_SUBMISSIONS.md has the server's API, docs/MAP_EDITOR.md this page): the dialog, the call, the answers of the owner. Nothing is published by the
// player: a map only goes to the owner's queue, nothing can be replaced, and the button works only for a map that the checker says works. Everything shown is put in with textContent. A plain ES5 script.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_checks.js'), require('./mapmaker_store.js'));
    else root.MapSubmit = factory(root.MapChecks, root.MapStore);
})(this, function (Checks, Store) {
    'use strict';

    var CAP = { classic: 512 * 1024, extended: 4 * 1024 * 1024 };               // the most a map file of each kind may be (the server's own limits)
    var TOKEN = /^[A-Za-z0-9_-]{43}$/, MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var OFF = 'Sending maps is not switched on right now. Everything else works, and your map stays here. Please try again later.';
    var LINES = {                                                                  // the limits and the server's own trouble, in plain words (the server's sentence is used for the rest)
        address_limit: 'You have already sent 3 maps today. Please try again tomorrow.', day_limit: 'The most maps for one day have been sent. Please try again tomorrow.',
        queue_full: 'The owner has many maps waiting right now. Please try again later.', too_many_requests: 'There were too many tries in the last hour. Please wait a while.',
        busy: 'The server is busy checking other maps. Please try again in a minute.', storage_full: 'The server has no room for a map right now. Please try again later.'
    };
    var STATES = { waiting: ['warn', 'Waiting for approval'], approved: ['ok', 'Approved'], taken_down: ['wait', 'Approved, but no longer in the map list'], declined: ['bad', 'Declined'] };

    function guard(fn) { try { return fn(); } catch (e) { return undefined; } }
    // The door is off when the server says so, or when there is no door (a 404 that is not the server's own JSON: a real 404 of the server, such as a withdraw of an unknown id, has an error code)
    function isOff(res) { return (res.status === 503 && !!res.json && res.json.error === 'submissions_off') || (res.status === 404 && !(res.json && res.json.error)); }
    var MAYBE = ' The map may have been received: look at My submissions before you send it again.';
    var LOST = ' If this was the first map you sent, this browser cannot find it again, and sending it again may give the owner two.';
    function chars(t) { return Array.from(t).length; }
    function enc(t) { return encodeURIComponent(t); }                              // (a space is %20 and a plus stays a plus: never a form encoder)

    // The map's name as the server wants it (docs/MAP_SUBMISSIONS.md, "The text"): '' when it is fine, else a sentence. The server judges it again.
    function nameProblem(name) {
        var n = chars(name);
        if (n < 3 || n > 60) return 'The name needs 3 to 60 characters.';
        if (!/^[A-Za-z0-9][A-Za-z0-9 '\-_.!&(),]*$/.test(name) || (name.match(/[A-Za-z0-9]/g) || []).length < 2 || /\.\.|\.$/.test(name)) return 'A name may hold letters, digits, spaces and these marks: \' - _ . ! & ( ) ,';
        return '';
    }
    function textProblem(text, most, what) {
        if (chars(text) > most) return 'The ' + what + ' can have ' + most + ' characters at most.';
        return /[<>]/.test(text) ? 'The ' + what + ' cannot have the marks < and >.' : /:\/\/|www\./i.test(text) || text.indexOf('```') >= 0 ? 'The ' + what + ' cannot have a web address or three backticks.' : '';
    }
    // The maker's name as the server judges it again (map_names.cpp): look-alike letters, accents, "l" as "i" and digits as letters are folded; LOOK and ACCENTS are pairs (from, to)
    var LOOK = 'АAВBЕEКKМMНHОOРPСCТTУYХXаaеeкkоoрpсcуyхxΑAΒBΕEΖZΗHΙIΚKΜMΝNΟOΡPΤTΥYΧXαaιiκkνvοoρpυuχxωw', ACCENTS = 'ıiøołlđdðdħhŧt';
    function fold(t) { return t.replace(/[^\x00-\x7f]/g, function (c) { var i = LOOK.indexOf(c); return i < 0 ? c : LOOK.charAt(i + 1); }).toLowerCase(); }
    function skeletons(t) {
        var f = fold(t).normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[ıøłđðħŧ]/g, function (c) { return ACCENTS.charAt(ACCENTS.indexOf(c) + 1); }).replace(/l/g, 'i');
        return [f, f.replace(/[0-5]/g, function (c) { return 'oi eas'.charAt(c); })].map(function (x) { return x.replace(/[^a-z\u0080-\uffff]/g, ''); });
    }
    function makerProblem(t) {
        return skeletons(t).some(function (k) { return /^(the)?(owner|admin|administrator|system|moderator|mod|staff|assistant|server|ants|root)$/.test(k); }) || /^bot\(|\(bot(,[^()]+)?\)$/.test(fold(t).replace(/\s/g, '')) ?
            'That player name cannot be used. Please choose another.' : '';
    }
    function when(sec, now) {
        var d = new Date(sec * 1000), t = new Date(now);
        if (d.toDateString() === t.toDateString()) return 'today, ' + d.getHours() + ':' + ('0' + d.getMinutes()).slice(-2);
        return d.getDate() + ' ' + MONTHS[d.getMonth()] + (d.getFullYear() === t.getFullYear() ? '' : ' ' + d.getFullYear());
    }
    // A status link carries the token in the fragment (never sent to a server): '#sent=<token>,<token>' -> the tokens that are shaped like one
    function tokensOf(hash) {
        var m = /^#sent=([A-Za-z0-9_,-]+)$/.exec(hash || '');
        return m ? m[1].split(',').filter(function (t) { return TOKEN.test(t); }) : [];
    }
    function statusOf(rec) {
        var s = STATES[rec.state] || ['wait', String(rec.state)], text = s[1];
        if (rec.state === 'approved' && rec.renamed_by_owner && rec.approved_name) text = 'Approved as “' + rec.approved_name + '” (renamed by the owner)';
        return [s[0], text];
    }
    // Why a call did not work, in a sentence: the status first (the answers of nginx are HTML, not JSON), the server's sentence when there is one
    function why(res) {
        var j = res.json || {};
        if (res.net) return 'The server did not answer. Check your connection and try again.';
        if (res.timeout) return 'The server did not answer in time.';
        if (isOff(res)) return OFF;
        if (LINES[j.error]) return LINES[j.error];
        if (typeof j.message === 'string' && j.message) return j.message;
        if (res.status === 413) return 'This map file is too big to send.';
        if (res.status === 429 || res.status === 503) return 'There are too many tries or the server is busy. Please wait a minute and try again.';
        return 'The server could not take this (error ' + res.status + '). Nothing was sent.';
    }

    // The calls: done({status, json}), done({net: true}) or, after 30 seconds, done({timeout: true}) (the request is aborted), once. env: {fetch, later(fn, ms), Abort (AbortController or null)}
    function api(env) {
        var fetchFn = env.fetch;
        function call(method, path, token, body, done) {
            var over = false, headers = {};
            function fin(r) { if (!over) { over = true; done(r); } }
            if (token) headers.Authorization = 'Bearer ' + token;
            if (body) headers['Content-Type'] = 'application/octet-stream';
            var stop = env.Abort && guard(function () { return new env.Abort(); }), init = { method: method, headers: headers, body: body || undefined, cache: 'no-store' };
            if (stop) init.signal = stop.signal;
            var p = fetchFn && guard(function () { return fetchFn(path, init); });
            if (!p) return fin({ net: true });
            env.later(function () { if (!over) { fin({ timeout: true }); if (stop) guard(function () { stop.abort(); }); } }, 30000);
            p.then(function (r) {
                return r.text().then(function (t) { var j = guard(function () { return JSON.parse(t); }); fin({ status: r.status, json: j && typeof j === 'object' ? j : null }); });
            }).then(null, function () { fin({ net: true }); });
        }
        return { call: call };
    }

    // env: {doc, dialogs, store (the maps' store: inLibrary, libraryState, onLibrary), sent (Store.createSent), fetch, later(fn, ms), parse(bytes) -> file | null, thumb(file) -> canvas | null, tags(host, kind),
    //       Abort (AbortController or null),
    //       current() -> {name, bytes, file, report} | {error}, showFinding(f), onOpenCopy(rec), onSee(), clipboard, link() -> the page's address without a fragment, now()}
    function create(env) {
        var doc = env.doc, dialogs = env.dialogs, calls = api(env).call, pending = false, recs = Object.create(null), tokens = [], linked = [], maker = '', token = 0, loaded = false;
        function h(tag, cls, kids, attrs) {
            var n = doc.createElement(tag), k;
            if (cls) n.className = cls;
            [].concat(kids === undefined ? [] : kids).forEach(function (c) { n.appendChild(typeof c === 'string' ? doc.createTextNode(c) : c); });
            for (k in attrs || {}) n.setAttribute(k, attrs[k]);
            return n;
        }
        function button(cls, text, onClick, label) { var b = h('button', cls, text, { type: 'button' }); if (label) b.setAttribute('aria-label', label); b.addEventListener('click', onClick); return b; }
        function note(cls, mark, kids) { return h('div', 'note ' + cls, [h('span', 'ic', mark, { 'aria-hidden': 'true' }), h('div', '', kids)]); }
        function say(title, text) { return dialogs.message(title, text); }
        function visible() { return Object.keys(recs).map(function (k) { return recs[k]; }).filter(function (r) { return r.state !== 'withdrawn'; }).sort(function (a, b) { return (b.sent_at || 0) - (a.sent_at || 0); }); }
        function keep(rec) { env.sent.put(rec, function () {}); }
        function own() { return tokens.filter(function (t) { return linked.indexOf(t) < 0; }); }                  // (the tokens this browser made itself: a send never uses one that came from a link)
        function keepMeta() { env.sent.put({ id: '#tokens', list: tokens, linked: linked }, function () {}); env.sent.put({ id: '#maker', name: maker }, function () {}); }
        function addTokens(list) { var n = tokens.length; list.forEach(function (t) { if (TOKEN.test(t) && tokens.indexOf(t) < 0) tokens.push(t); }); if (tokens.length !== n) keepMeta(); return tokens.length !== n; }

        // What this browser kept (merged into what is in memory, so a late answer never takes anything away)
        function load(done) {
            env.sent.open(function (status) {
                if (status !== 'ready') return done();
                env.sent.all(function (error, list) {
                    (list || []).forEach(function (r) {
                        if (r.id === '#tokens') { (Array.isArray(r.linked) ? r.linked : []).forEach(function (t) { if (TOKEN.test(t) && linked.indexOf(t) < 0) linked.push(t); }); addTokens(r.list || []); }          // (what came from a link is known before anything is written back)
                        else if (r.id === '#maker') maker = maker || (typeof r.name === 'string' ? r.name : '');
                        else if (typeof r.id === 'string' && !recs[r.id]) recs[r.id] = r;
                    });
                    loaded = true;
                    done();
                });
            });
        }

        // a map that is waiting already, with these very bytes
        function waitingTwin(bytes) {
            return visible().filter(function (r) {
                if (r.state !== 'waiting' || !r.bytes || r.bytes.length !== bytes.length) return false;
                for (var i = 0; i < bytes.length; i++) if (r.bytes[i] !== bytes[i]) return false;
                return true;
            })[0];
        }

        // ---- Submit for approval
        function open() {
            if (pending) return say('Still sending', 'A map is still being sent. Please wait for the answer, then look at My submissions.');
            var cur = env.current();
            if (cur.error) return say('The map cannot be sent', cur.error);
            var kind = cur.file.kind, report = cur.report, bytes = cur.bytes, works = !!report.works, sending = false, door = 'checking', ctl, shown = false;
            var reasons = (report.findings || []).filter(function (f) { return f.severity === 'fatal' || f.severity === 'warning'; });                    // (what stops a map: the checker's own sentences)
            var twin = waitingTwin(bytes), big = bytes.length > CAP[kind], name = Store.clean(cur.name), editing = !maker;
            var nameIn = h('input', '', [], { type: 'text', id: 'sub-name', maxlength: 60, autocomplete: 'off', spellcheck: 'false' }), nameBox = h('div', '', [], { 'aria-live': 'polite' });
            var noteIn = h('textarea', '', [], { id: 'sub-note', maxlength: 280, rows: 3, placeholder: 'For example: what is special about this map' }), noteBox = h('div', '');
            var makerIn = h('input', '', [], { type: 'text', id: 'sub-maker', maxlength: 24, autocomplete: 'off' }), makerBox = h('div', 'subwho'), problems = h('div', '', [], { 'aria-live': 'polite' });
            var errBox = h('div', '', [], { 'aria-live': 'polite', id: 'sub-err' }), details = h('div', ''), doorBox = h('div', '', [], { 'aria-live': 'polite' });
            nameIn.value = name.slice(0, 60); makerIn.value = maker;

            function values() { return { name: Store.clean(nameIn.value), maker: Store.clean(makerIn.value), note: noteIn.value.replace(/\r\n?/g, '\n').replace(/\n{3,}/g, '\n\n').replace(/^\s+|\s+$/g, '') }; }
            function bad() {
                var v = values();
                return nameProblem(v.name) || (chars(v.maker) < 1 ? 'Please type the name that players will see as the maker.' : textProblem(v.maker, 24, 'player name') || makerProblem(v.maker)) || textProblem(v.note, 280, 'note');
            }
            function paint() {
                var v = values(), nm = nameProblem(v.name), lib = env.store.libraryState();
                nameBox.textContent = '';
                if (nm) nameBox.appendChild(note('bad', '!', nm));
                else if (env.store.inLibrary(v.name)) nameBox.appendChild(note('warn', 'i', 'The name “' + v.name + '” is already in the game’s map list. You can still send it. The owner may change the name or add a number (for example “' + v.name + ' 2”). You will see the final name in My submissions.'));
                else if (lib === 'ready') nameBox.appendChild(h('div', 'cfree', [h('i', 'dot ok'), 'No map in the list has this name.']));
                makerBox.textContent = '';
                if (editing) makerBox.appendChild(h('label', '', ['Player name shown as the maker', makerIn]));
                else makerBox.appendChild(h('span', '', ['Maker shown to players ', h('b', '', maker), ' ', button('link', 'Change my player name', function () { editing = true; paint(); makerIn.focus(); })]));
                doorBox.textContent = '';
                if (door === 'off') doorBox.appendChild(note('warn', 'i', OFF));
                else if (twin) doorBox.appendChild(note('warn', 'i', ['This very map is already waiting for approval (sent ' + when(twin.sent_at || 0, env.now()) + ' as \u201c' + twin.name + '\u201d). Look at My submissions instead of sending it again. To send a changed map, change it first.', h('div', '', button('btn sm', 'See my submissions', function () { ctl.close(); env.onSee(); }))]));
                else if (big) doorBox.appendChild(note('bad', '!', 'This map file is too big to send (at most ' + Math.round(CAP[kind] / 1024) + ' KB).'));
                var ready = works && !twin && !big && door !== 'off' && door !== 'checking' && !bad() && !sending;
                if (ctl) { ctl.setDisabled('send', !ready); ctl.setDisabled('cancel', false); }
                problems.textContent = '';
                var line = nm ? '' : bad();
                if (line) problems.appendChild(h('p', 'cnever', line));
            }

            function verdict() {
                var box = h('div', 'subchk'), head = h('div', 'subhd'), toggle = button('link', 'Show details', function () {
                    shown = !shown; details.hidden = !shown; toggle.textContent = shown ? 'Hide details' : 'Show details'; toggle.setAttribute('aria-expanded', String(shown));
                });
                toggle.setAttribute('aria-expanded', 'false');
                head.appendChild(h('span', works ? 'st-ok' : 'st-no', [h('i', 'dot ' + (works ? 'ok' : 'bad')), ' ' + (report.headline || (works ? 'Works' : 'Does not work'))]));
                head.appendChild(toggle);
                box.appendChild(head);
                if (!works && reasons.length) box.appendChild(h('ul', 'subfin', reasons.map(function (f) { return h('li', '', Checks.capital(f.message)); })));
                details.hidden = true;
                details.appendChild(Checks.render(doc, report, {}));
                box.appendChild(details);
                if (!works) {
                    var first = reasons.filter(function (f) { return typeof f.row === 'number' && f.row >= 0; })[0] || reasons[0];
                    box.appendChild(note('bad', '!', ['A map that does not work cannot be submitted. Fix ' + (reasons.length === 1 ? 'this thing' : 'these ' + (reasons.length === 2 ? 'two' : reasons.length || '') + ' things') + ' in the editor, then come back.', h('div', '', first ? button('btn sm', 'Show me', function () { ctl.close(); env.showFinding(first); }) : '')]));
                } else if (kind === 'extended') box.appendChild(note('info', 'i', 'This is an Extended map. If it is approved, only players on the newest version will see it in the map list.'));
                return box;
            }

            function build(body, c) {
                ctl = c;
                var tags = h('span', 'tags'), pic = env.thumb(cur.file);
                env.tags(tags, kind);
                body.appendChild(h('p', 'lead', 'Send your map to the owner of the game. If the owner approves it, it joins the map list for everyone.'));
                body.appendChild(h('div', 'srccard', [pic ? h('span', 'cthumb', pic) : '', h('div', 'st', [h('div', 'sh', [tags]), h('div', 'sl', cur.file.rows + ' x ' + cur.file.columns + (env.sent.status() === 'ready' ? ' · saved in this browser' : ''))])]));
                body.appendChild(h('div', 'cname', [h('label', '', 'Name of the map', { 'for': 'sub-name' }), nameIn, nameBox]));
                body.appendChild(verdict());
                body.appendChild(doorBox);
                body.appendChild(h('div', 'pfield', [h('label', '', 'Note for the owner (optional)', { 'for': 'sub-note' }), noteIn, noteBox]));
                body.appendChild(makerBox);
                body.appendChild(problems);
                body.appendChild(h('div', 'note', [h('div', '', [h('b', '', 'Nothing is published until it is approved. '), 'You keep your own copy.'])]));
                body.appendChild(errBox);
                [nameIn, noteIn, makerIn].forEach(function (i) { i.addEventListener('input', paint); });
                env.store.onLibrary(function () { if (!ctl.closed) paint(); });
                paint();
            }

            function fail(res) {
                var box = errBox, list = res.json && Array.isArray(res.json.findings) ? res.json.findings : [];
                box.textContent = '';
                var lost = res.net || res.timeout || (res.status === 201 && !(res.json && res.json.submission));
                box.appendChild(note('bad', '!', [why(res) + (lost ? MAYBE + (own().length ? '' : LOST) : '')].concat(lost ? [h('div', '', button('btn sm', 'See my submissions', function () { ctl.close(); env.onSee(); }))] : []).concat(list.length ? [h('ul', 'subfin', list.map(function (f) { return h('li', '', Checks.capital(String(f.message))); }))] : [])));
                if (isOff(res)) door = 'off';
                paint();
            }

            function send() {
                var v = values(), q = '?kind=' + kind + '&name=' + enc(v.name) + '&maker=' + enc(v.maker) + (v.note ? '&note=' + enc(v.note) : '');
                if (sending || bad() || !works) return;
                sending = pending = true; paint();
                errBox.textContent = '';
                calls('POST', '/submissions' + q, own()[0], bytes, function (res) {
                    sending = pending = false;
                    var j = res.json || {};
                    if (res.status !== 201 || !j.submission || typeof j.submission.id !== 'string') return attempt.closed ? undefined : fail(res);
                    var t = TOKEN.test(j.token) ? j.token : own()[0], rec = { tok: t, bytes: bytes, rows: cur.file.rows, columns: cur.file.columns }, k;
                    for (k in j.submission) rec[k] = j.submission[k];
                    if (t) tokens = [t].concat(tokens.filter(function (x) { return x !== t; }));
                    maker = v.maker; keepMeta();
                    recs[rec.id] = rec; keep(rec);
                    if (!attempt.closed || !dialogs.isOpen()) sentDialog(rec, !!j.name_taken);          // (a late answer after the dialog was closed is kept, and shown unless another dialog is open)
                });
            }

            var attempt = dialogs.open({
                title: 'Submit for approval', cls: 'subdlg', build: build, focus: '#sub-name',
                buttons: [{ id: 'cancel', text: 'Cancel', ghost: true }, { id: 'send', text: 'Submit for approval', disabled: true, keepOpen: true, onClick: send }]
            });
            calls('GET', '/submissions', null, null, function (res) { door = isOff(res) ? 'off' : 'on'; if (!attempt.closed) paint(); });       // (no token: 401 says the door is on, 503 that it is off)
            return attempt;
        }

        function sentDialog(rec, taken) {
            dialogs.open({
                title: 'Submitted', cls: 'narrow',
                build: function (body) {
                    body.appendChild(h('div', 'subok', [h('span', 'tick', '✓', { 'aria-hidden': 'true' }), h('div', '', [h('div', 'big', 'Sent. Waiting for approval.'), h('div', 'sl', '“' + rec.name + '” is with the owner now.')])]));
                    body.appendChild(h('ul', 'subul', [
                        h('li', '', 'The owner looks at it and decides. This can take a while.'),
                        h('li', '', 'Nothing is published until it is approved. You keep your own copy, and you can go on editing it.'),
                        h('li', '', env.sent.status() === 'ready' ? 'You can see the answer in My submissions. The page remembers your submissions in this browser.' : 'You can see the answer in My submissions. This browser cannot remember them, so copy your status link there to find them again.')]));
                    if (taken) body.appendChild(note('warn', 'i', 'The name “' + rec.name + '” is already in the game’s map list. The owner may change it or add a number. You will see the final name in My submissions.'));
                },
                buttons: [{ id: 'back', text: 'Back to the editor', ghost: true }, { id: 'see', text: 'See my submissions', onClick: function () { env.onSee(); } }]
            });
        }

        // ---- My submissions (inside the My maps screen). ui: {list, note, say, link (the button)}
        function merge(view, t) {
            var r = recs[view.id] || (recs[view.id] = { id: view.id }), before = [r.state, r.message, r.approved_name, r.decided_at, r.name].join('|'), k;
            for (k in view) r[k] = view[k];
            r.tok = t;
            if (linked.indexOf(t) >= 0) r.linked = true;
            if (before !== [r.state, r.message, r.approved_name, r.decided_at, r.name].join('|')) keep(r);
        }
        function show(ui) {
            var mine = ++token, problem = '', looking = !!tokens.length;
            ui.note.textContent = '';
            ui.note.appendChild(doc.createTextNode('An approved map is never replaced. To change a map, open your own copy, change it and send the changed copy as a '));
            ui.note.appendChild(h('b', '', 'new'));
            ui.note.appendChild(doc.createTextNode(' submission.'));
            ui.link.hidden = false;
            if (!ui.link.wired) { ui.link.wired = true; ui.link.addEventListener('click', function () { copyLink(ui); }); }
            function paint() {
                var rows = visible();
                ui.list.textContent = '';
                if (problem) ui.list.appendChild(note('warn', 'i', problem + ' Showing what this browser last knew.'));
                if (!rows.length) ui.list.appendChild(note('', 'i', looking ? 'Looking for your submissions…' : 'You have not sent a map yet. Open a map and choose “Submit for approval...”.'));
                rows.forEach(function (r) { ui.list.appendChild(row(r, ui, paint)); });
                ui.say(rows.length === 1 ? '1 submission shown.' : rows.length + ' submissions shown.');
                if (ui.count) ui.count(rows.length);
            }
            paint();
            if (!tokens.length) return;
            var left = tokens.length;
            tokens.slice().forEach(function (t) {
                calls('GET', '/submissions', t, null, function (res) {
                    if (mine !== token) return;
                    if (res.status === 200 && res.json && Array.isArray(res.json.submissions)) res.json.submissions.forEach(function (v) { if (v && typeof v.id === 'string') merge(v, t); });
                    else problem = problem || (isOff(res) ? 'Sending maps is not switched on right now, so the owner’s answers cannot be looked up.' : 'The owner’s answers could not be looked up (' + why(res).replace(/\.$/, '') + ').');
                    if (--left === 0) { looking = false; paint(); }
                });
            });
        }
        function row(rec, ui, paint) {
            var st = statusOf(rec), file = rec.bytes ? env.parse(new Uint8Array(rec.bytes)) : null, node = h('div', 'mrow'), tags = h('span', 'tags'), acts = h('div', 'macts'), mid = h('div', 'mmid'), name = String(rec.name || '');
            var rows = rec.height || rec.rows, cols = rec.width || rec.columns, pic = file && env.thumb(file);
            env.tags(tags, rec.kind === 'extended' ? 'extended' : 'classic');
            mid.appendChild(h('div', 't1', [name + ' ', tags]));
            mid.appendChild(h('div', 't2', 'Sent ' + when(rec.sent_at || 0, env.now()) + (rows && cols ? ' · ' + rows + ' x ' + cols : '')));
            mid.appendChild(h('div', 't3', [h('i', 'dot ' + st[0]), h('span', 'st-' + st[0], st[1])]));
            if (rec.linked) mid.appendChild(h('div', 't2', 'Added from a link'));
            if (rec.message) mid.appendChild(h('div', 'owrote', [h('span', '', 'The owner wrote: '), String(rec.message)]));
            node.appendChild(h('span', 'mthumb', pic || '', { 'aria-hidden': 'true' }));
            node.appendChild(mid);
            if (rec.state === 'waiting') acts.appendChild(button('btn sm', 'Withdraw', function () { withdraw(rec, paint); }, 'Withdraw ' + name));
            if (file && rec.state !== 'waiting') acts.appendChild(button('btn sm', 'Open my copy', function () { env.onOpenCopy(rec); }, 'Open my copy of ' + name));
            if (file && rec.state === 'declined') acts.appendChild(button('btn sm', 'Change and send again', function () { env.onOpenCopy(rec, true); }, 'Change ' + name + ' and send it again'));
            node.appendChild(acts);
            return node;
        }
        function withdraw(rec, paint) {
            dialogs.open({
                title: 'Withdraw this map?', cls: 'narrow',
                build: function (body) { body.appendChild(h('p', 'fatalmsg', '“' + rec.name + '” will not go to the owner. You keep your own copy, and you can send it again later.')); },
                buttons: [{ id: 'keep', text: 'Keep it waiting', ghost: true }, { id: 'go', text: 'Withdraw it', onClick: function () {
                    calls('POST', '/submissions/' + enc(rec.id) + '/withdraw', rec.tok, null, function (res) {
                        if (res.status === 200) { delete recs[rec.id]; env.sent.remove(rec.id, function () {}); return paint(); }
                        say(res.status === 409 ? 'The owner has already answered' : 'The map could not be withdrawn', res.status === 409 ? 'Open My submissions again to see the answer.' : why(res));
                    });
                } }]
            });
        }
        function copyLink(ui) {
            var url = tokens.length ? env.link() + '#sent=' + tokens.join(',') : '';
            if (!url) return say('No status link yet', 'You have not sent a map from this browser yet. The link comes with your first submission.');
            function manual() {
                dialogs.open({
                    title: 'Your status link', cls: 'narrow', focus: 'input',
                    build: function (body) {
                        body.appendChild(h('p', 'fatalmsg', 'Copy this link and open it in a browser on another device to see your submissions there. Anyone who has it can see them and withdraw them, so keep it to yourself.'));
                        var i = h('input', '', [], { type: 'text', readonly: 'readonly', 'aria-label': 'Your status link' });
                        i.value = url; i.addEventListener('focus', function () { i.select(); });
                        body.appendChild(i);
                    }, buttons: [{ text: 'Close' }]
                });
            }
            var p = env.clipboard && guard(function () { return env.clipboard.writeText(url); });
            if (!p) return manual();
            p.then(function () { ui.link.textContent = 'Link copied'; env.later(function () { ui.link.textContent = 'Copy my status link'; }, 2500); }, manual);
        }

        // A status link opened in this browser: the address loses its fragment at once, then the page asks before it adds anything (a link can carry a stranger's token). added() runs once they are added
        function fromAddress(hash, clear, added) {
            var list = tokensOf(hash).slice(0, 10);                                          // (a link adds at most 10)
            if (!list.length) return false;
            clear();
            var fresh = list.filter(function (t) { return tokens.indexOf(t) < 0; });
            if (!fresh.length) { if (added) added(); return true; }
            dialogs.open({
                title: 'Add submissions from a link?', cls: 'narrow',
                build: function (body) { body.appendChild(h('p', 'fatalmsg', 'This link adds submissions made by someone else to this browser. Add them?')); },
                buttons: [{ id: 'no', text: 'Do not add', ghost: true }, { id: 'add', text: 'Add them', onClick: function () {
                    fresh.forEach(function (t) { if (tokens.indexOf(t) < 0) { tokens.push(t); linked.push(t); } });
                    keepMeta();
                    if (added) added();
                } }]
            });
            return true;
        }

        return { load: load, open: open, show: show, hide: function () { token++; }, count: function () { return visible().length; }, fromAddress: fromAddress, tokens: function () { return tokens.slice(); } };
    }

    return { create: create, api: api, nameProblem: nameProblem, textProblem: textProblem, makerProblem: makerProblem, skeletons: skeletons, tokensOf: tokensOf, statusOf: statusOf, why: why, when: when, isOff: isOff };
});

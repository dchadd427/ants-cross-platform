// Connect an AI, the page's side (docs/MAP_AI.md): the sheet where a person makes, sees and revokes the keys of their own AI and finds out how to connect it, the switch "Let an AI work in this tab",
// the socket of the armed tab (wss://<site>/hub/ai/ws, which exists only while the switch is on: closing it, with Stop or Turn off or by closing the tab, disarms at once) and the strip that
// shows what the AI does and has Stop and Undo AI changes. The work itself is mapmaker_ai_tools.js. Loaded the first time Connect an AI is pressed (with its style), so a person who never connects
// an AI never downloads it. Text from outside (the name of a key, the name of the AI app, a note that the AI sends) is put in with textContent only, and a key is held in memory while it is shown
// and nowhere else: not in storage, not in an address, not in a log. A plain ES5 script that a page and node can load (the sheet needs a document).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./mapmaker_ai_tools.js'));
    else root.MapAi = factory(root.MapAiTools);
})(this, function (AiTools) {
    'use strict';

    var DAYS = [7, 30, 90], MAX_NAME = 40, ACTIVITY = 4, KINDS = [['edit', 'Look and edit', 'Read the map, change it (undo always works) and save a draft.'], ['read', 'Look only', 'Read the map and run the checks. It cannot change anything.']];
    var STATES_OFF = { ai_off: 'Connecting an AI is not switched on here yet.', login: 'Log in with Discord to connect an AI.', banned: 'This account cannot make keys.', rate: 'Too many tries: wait a moment and try again.' };
    var BYE = { replaced: 'Another tab took the AI\u2019s place, so this one is off.', idle: 'No call for an hour, so this tab was turned off.', 'logged out': 'You were logged out, so this tab was turned off.', 'the login ran out': 'Your login ran out, so this tab was turned off.', silent: 'The connection went quiet, so this tab was turned off.', 'the hub restarts': 'The site restarted, so this tab was turned off.' };
    var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

    function dateText(seconds) { var d = new Date(seconds * 1000); return MONTHS[d.getMonth()] + ' ' + d.getDate(); }
    function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }
    // "4 minutes ago", "6 days ago": how long since `then`, for a key's last use
    function ago(seconds) {
        if (seconds < 90) return 'just now';
        if (seconds < 5400) return plural(Math.round(seconds / 60), 'minute', 'minutes') + ' ago';
        if (seconds < 129600) return plural(Math.round(seconds / 3600), 'hour', 'hours') + ' ago';
        return plural(Math.round(seconds / 86400), 'day', 'days') + ' ago';
    }
    function short(seconds) { return seconds < 2 ? 'now' : seconds < 90 ? Math.round(seconds) + ' s ago' : Math.round(seconds / 60) + ' min ago'; }
    function clean(text) { return Array.from(String(text || '')).filter(function (c) { return c >= ' ' && c !== '\u007f'; }).slice(0, 60).join(''); }
    function metaOf(key, now) { return 'Made ' + dateText(key.made) + ' \u00b7 stops working ' + dateText(key.ends) + ' \u00b7 ' + (key.calls ? 'used ' + ago(Math.max(0, now - key.used)) : 'not used yet'); }

    // The lines that tell a person how to put a key into an AI app: one for each kind of app (the key is in the text only while the sheet shows it)
    function commandFor(kind, origin, key) {
        var k = key || '<your key>';
        if (kind === 'code') return 'claude mcp add --transport http ants-map-maker \\\n  ' + origin + '/hub/ai/mcp \\\n  --header "Authorization: Bearer ' + k + '"';
        if (kind === 'file') return '{\n  "mcpServers": {\n    "ants-map-maker": {\n      "url": "' + origin + '/hub/ai/mcp",\n      "headers": { "Authorization": "Bearer ' + k + '" }\n    }\n  }\n}';
        return 'curl -X POST ' + origin + '/hub/ai/v1/call \\\n  -H "Authorization: Bearer ' + k + '" \\\n  -H "Content-Type: application/json" \\\n  -d \'{"tool": "map_info"}\'';
    }

    // env: doc, win, dialogs (MapDialog.create), fetch, host (the node that the strip is put after), entry (the button of the name row, or null), tools (an env for mapmaker_ai_tools.js: create(env)),
    // frame(rect, label) (draws a dashed frame over the map for a moment), announce(text)
    function create(env) {
        var doc = env.doc, win = env.win, dialogs = env.dialogs, origin = win.location.origin;
        var tenv = env.tools, tools, sock = null, state = 'off', csrf = '', keys = [], max = 3, on = true, loginNeeded = false, loadFailed = '', edits = 0, who = { key: '' }, gen = 0, cancelled = {};
        var activity = [], strip = null, ticker = 0, shownKey = '', pending = null, ended = '', current = null;

        tenv.note = function (text) { activityAdd('The AI says: \u201c' + clean(text) + '\u201d', true); showStrip(); };
        tenv.onAct = function (text) { activityAdd(text, false); };
        tenv.fetchText = function (url, done) {                                    // (a guide: a file of the site's skills, asked for by a fixed name: mapmaker_ai_tools.js)
            if (!env.fetch) return done('failed');
            env.fetch(url, { credentials: 'same-origin' }).then(function (r) { return r.ok ? r.text() : Promise.reject(new Error('no')); }).then(function (t) { done('', t); }, function () { done('failed'); });
        };
        tools = AiTools.create(tenv);

        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        function button(text, cls, onClick) { var b = el('button', 'btn' + (cls ? ' ' + cls : ''), text); b.type = 'button'; if (onClick) b.addEventListener('click', onClick); return b; }
        function now() { return Math.floor(Date.now() / 1000); }

        // ---- the calls that the page makes to the hub for keys ----
        function api(method, path, body, done) {
            var opts = { method: method, credentials: 'same-origin', headers: {} };
            if (method === 'POST') { opts.headers['Content-Type'] = 'application/json'; opts.headers['X-Ants-Csrf'] = csrf; opts.body = JSON.stringify(body); }
            if (!env.fetch) return done(0, null);
            env.fetch(path, opts).then(function (r) { return r.json().then(function (j) { done(r.status, j); }, function () { done(r.status, null); }); }, function () { done(0, null); });
        }
        function load(done) {
            api('GET', '/hub/ai/keys', null, function (status, j) {
                loginNeeded = on = false; loadFailed = '';
                if (status === 200 && j && j.ok) { keys = j.keys || []; max = j.max || 3; csrf = j.csrf || ''; on = true; }
                else if (status === 503 && j && j.error === 'ai_off') loadFailed = STATES_OFF.ai_off;
                else if (status === 401) { loginNeeded = true; loadFailed = STATES_OFF.login; }
                else if (status === 403 && j && j.error === 'banned') loadFailed = STATES_OFF.banned;
                else loadFailed = 'The site did not answer. Check your connection and try again.';
                done();
            });
        }

        // ---- the dashed frame over the part of the map that the AI just touched (it goes by itself) ----
        var frameNode = null, frameTimer = 0;
        function frameOver(rect, label) {
            if (!env.view || !env.wrap || !env.canvasNode) return;
            var st = env.view(), a = env.toScreen(st.view, st.width, st.height, rect.row, rect.column), b = env.toScreen(st.view, st.width, st.height, rect.row + rect.rows, rect.column + rect.columns);
            var cr = env.canvasNode.getBoundingClientRect(), wr = env.wrap.getBoundingClientRect(), kx = cr.width / st.width, ky = cr.height / st.height;
            var x0 = Math.max(0, a.x), y0 = Math.max(0, a.y), x1 = Math.min(st.width, b.x), y1 = Math.min(st.height, b.y);
            if (x1 <= x0 || y1 <= y0) return;
            if (!frameNode) { frameNode = el('div', 'aik-frame'); frameNode.appendChild(el('span')); frameNode.setAttribute('aria-hidden', 'true'); env.wrap.appendChild(frameNode); }
            frameNode.style.left = (cr.left - wr.left + x0 * kx) + 'px'; frameNode.style.top = (cr.top - wr.top + y0 * ky) + 'px';
            frameNode.style.width = ((x1 - x0) * kx) + 'px'; frameNode.style.height = ((y1 - y0) * ky) + 'px';
            frameNode.firstChild.textContent = label; frameNode.hidden = false;
            win.clearTimeout(frameTimer);
            frameTimer = win.setTimeout(function () { if (frameNode) frameNode.hidden = true; }, 3000);
        }

        // ---- the strip ----
        function activityAdd(text, says) { activity.unshift({ at: Date.now(), text: text, says: !!says }); activity.length = Math.min(activity.length, ACTIVITY); }
        function buildStrip() {
            if (strip) return strip;
            strip = el('div', 'aik-bar'); strip.setAttribute('role', 'status'); strip.hidden = true;
            env.host.parentNode.insertBefore(strip, env.host.nextSibling);
            return strip;
        }
        function showStrip() {
            var bar = buildStrip(), act = Date.now();
            if (env.entry) { env.entry.classList.toggle('on', state === 'armed' || state === 'working'); var label = env.entry.querySelector('.lbl'); if (label) label.textContent = state === 'armed' ? 'AI is on' : state === 'working' ? 'AI is working' : 'Connect an AI...'; }
            bar.textContent = '';
            bar.hidden = state === 'off' && !ended;
            if (bar.hidden) return;
            bar.className = 'aik-bar' + (state === 'off' ? ' ended' : '');
            var head = el('div', 'who'), dot = el('span', 'dt'), title = el('b');
            dot.setAttribute('aria-hidden', 'true');
            title.textContent = state === 'off' ? 'The AI is off' : state === 'armed' ? 'This tab is open to an AI' : 'An AI is working on this map';
            head.appendChild(dot); head.appendChild(title); bar.appendChild(head);
            var meta = el('div', 'meta');
            if (state === 'off') meta.textContent = ended;
            else if (state === 'armed') meta.textContent = 'Waiting for your AI to connect. Nothing changes until it does, and you will see each change here. This turns off by itself when you close or reload the tab.';
            else meta.textContent = 'The AI with key \u201c' + who.key + '\u201d \u00b7 ' + plural(edits, 'change', 'changes') + ' so far \u00b7 you can keep editing';
            bar.appendChild(meta);
            var btns = el('div', 'btns');
            if (state === 'off') btns.appendChild(button('OK', 'ghost', function () { ended = ''; showStrip(); }));
            else if (state === 'armed') btns.appendChild(button('Turn off', 'stop', disarm));
            else {
                btns.appendChild(button('Stop', 'stop', disarm));
                btns.appendChild(button('Undo AI changes', 'ghost', undoAi));
            }
            bar.appendChild(btns);
            if (state === 'working' && activity.length) {
                var list = el('ul', 'aik-act'); list.setAttribute('aria-label', 'What the AI just did');
                activity.forEach(function (a) {
                    var li = el('li');
                    li.appendChild(el('span', 'ago', short((act - a.at) / 1000)));
                    li.appendChild(el('span', a.says ? 'say' : '', a.text));
                    list.appendChild(li);
                });
                bar.appendChild(list);
            }
        }
        function setState(next) {
            state = next;
            win.clearInterval(ticker);
            if (state === 'working') ticker = win.setInterval(showStrip, 1000);
            showStrip();
        }
        function undoAi() { var n = tools.undoAll(); activityAdd(n ? 'You undid ' + plural(n, 'step', 'steps') + ' of the AI\u2019s' : 'There was nothing of the AI\u2019s to undo', false); showStrip(); env.announce(n ? 'Undid ' + plural(n, 'step', 'steps') + ' of the AI.' : 'Nothing of the AI\u2019s to undo.'); }

        // ---- the armed tab ----
        function arm() {
            if (sock) return;
            var url = (win.location.protocol === 'https:' ? 'wss://' : 'ws://') + win.location.host + '/hub/ai/ws';
            ended = ''; edits = 0; activity = []; who = { key: '' }; gen++; cancelled = {};
            try { sock = new win.WebSocket(url); } catch (e) { sock = null; ended = 'The connection could not be opened.'; return setState('off'); }
            var mine = sock;
            setState('connecting');
            mine.onmessage = function (e) { if (sock === mine) onFrame(e.data); };
            mine.onclose = function () { if (sock !== mine) return; sock = null; gen++; if (!ended) ended = state === 'connecting' ? 'The site did not let this tab be opened to an AI. Log in with Discord and try again.' : 'The connection to the site ended, so this tab is turned off for AIs.'; setState('off'); };
            mine.onerror = function () {};
        }
        function disarm() {
            var s = sock;
            sock = null;
            gen++;                                                                  // (a call that is still waiting for the person's stroke is dropped: it asks alive() before it changes anything)
            ended = '';
            if (s) { try { s.close(1000, 'stopped'); } catch (e) {} }
            setState('off');
            env.announce('The AI is stopped and this tab is turned off for AIs.');
        }
        function send(value) { if (sock && sock.readyState === 1) sock.send(JSON.stringify(value)); }
        function onFrame(text) {
            var m;
            try { m = JSON.parse(text); } catch (e) { return; }
            if (!m || typeof m !== 'object') return;
            if (m.t === 'ready') { tools.arm(); setState('armed'); }
            else if (m.t === 'bye') { ended = BYE[m.why] || 'The AI was turned off.'; }
            else if (m.t === 'cancel' && typeof m.id === 'number') { Object.keys(cancelled).forEach(function (k) { if (Number(k) < m.id - 100) delete cancelled[k]; }); cancelled[m.id] = true; }
            else if (m.t === 'call') onCall(m);
        }
        var SAYS = {
            map_info: 'Looked at the map', get_region: 'Looked at part of the map', get_picture: 'Looked at a picture of the map', check_map: 'Ran the checks', list_tiles: 'Looked up the tiles', read_guide: 'Read a guide',
            list_library_maps: 'Looked at the library\u2019s maps', view_library_map: 'Looked at a library map', save_draft: 'Saved a draft', undo: 'Undid a step', redo: 'Redid a step'
        };
        function onCall(m) {
            if (typeof m.id !== 'number' || typeof m.tool !== 'string' || !m.args || typeof m.args !== 'object') return;
            who = { key: clean(m.key) };
            if (state !== 'working') setState('working');
            var mine = sock, id = m.id, mygen = gen;
            function alive() { return sock === mine && gen === mygen && !cancelled[id]; }          // (this arming, and the call not cancelled by the hub)
            tools.run(m.tool, m.args, function (frame) {
                if (!alive()) return;                                              // (stopped or cancelled meanwhile: the hub has answered the call itself)
                if (frame.ok) {
                    if (SAYS[m.tool] && !frame.cells && frame.result && !frame.result.changed) activityAdd(SAYS[m.tool], false);
                    if (frame.cells || (frame.result && frame.result.changed)) edits++;
                    if (frame.result && frame.result.rect) frameOver(frame.result.rect, 'AI: ' + plural(frame.cells || 1, 'cell', 'cells'));
                    send({ t: 'result', id: id, ok: true, result: frame.result, cells: frame.cells || 0 });
                } else send({ t: 'result', id: id, ok: false, error: frame.error, message: frame.message });
                showStrip();
            }, alive);
        }

        // ---- the sheet ----
        function open(spec) { return dialogs.open(spec); }
        function note(cls, mark, text) { var n = el('div', 'note' + (cls ? ' ' + cls : '')); n.appendChild(el('span', 'ic', mark)); n.appendChild(el('div', '', text)); return n; }
        function badge(scope) { return el('span', 'aik-badge' + (scope === 'read' ? ' look' : ''), scope === 'read' ? 'Look only' : 'Look and edit'); }

        function keyRow(key, list, ctl) {
            var row = el('div', 'aik-key'), name = el('div', 'nm'), asking = false;
            name.appendChild(el('span', '', key.name)); name.appendChild(badge(key.scope));
            row.appendChild(name);
            var revoke = button('Revoke', 'sm ghost', function () { if (asking) return; asking = true; render(); });
            revoke.setAttribute('aria-label', 'Revoke ' + key.name);
            function render() {
                row.textContent = '';
                row.className = 'aik-key' + (asking ? ' ask' : '');
                row.appendChild(name);
                if (!asking) row.appendChild(revoke);
                row.appendChild(el('div', 'meta', metaOf(key, now())));
                if (asking) {
                    row.appendChild(el('div', 'q', 'Revoke \u201c' + key.name + '\u201d? An AI that uses it is cut off at once. You cannot undo this; you can make a new key.'));
                    var two = el('div', 'two');
                    two.appendChild(button('Revoke this key', 'sm stop', function () {
                        api('POST', '/hub/ai/keys/revoke', { id: key.id }, function (status) { if (status === 200 || status === 404) showKeys(); else { asking = false; render(); env.announce('The key could not be revoked: try again.'); } });
                    }));
                    two.appendChild(button('Keep it', 'sm ghost', function () { asking = false; render(); }));
                    row.appendChild(two);
                }
            }
            render();
            return row;
        }

        function showKeys() {
            load(function () {
                var ctl = open({
                    title: 'Connect an AI', cls: 'aikey',
                    build: function (body) {
                        body.appendChild(el('p', 'lead', 'An AI assistant can help you build the map you have open. It works in this tab while you watch, and you can stop it at any moment. It cannot send your map in for approval, publish it or delete anything, and it cannot see anyone else\u2019s maps.'));
                        if (!on) {
                            body.appendChild(note('', 'i', loadFailed));
                            if (loginNeeded) { var a = el('a', 'btn gold', 'Log in with Discord'); a.setAttribute('href', '/hub/login?next=mapmaker'); body.appendChild(a); }
                            return;
                        }
                        var arm0 = el('div', 'aik-arm'), sw = el('button', 'eb tog'), tk = el('span', 'tk');
                        sw.type = 'button'; sw.setAttribute('role', 'switch'); sw.setAttribute('aria-checked', String(state !== 'off'));
                        tk.setAttribute('aria-hidden', 'true'); sw.appendChild(tk); sw.appendChild(el('span', '', 'Let an AI work in this tab: ' + (state === 'off' ? 'Off' : 'On')));
                        sw.addEventListener('click', function () { if (state === 'off') arm(); else disarm(); ctl.close(); });
                        arm0.appendChild(sw);
                        arm0.appendChild(el('span', 'st', state === 'off' ? 'Turn it on when your AI is ready' : 'Your AI can work in this tab now'));
                        arm0.appendChild(el('div', 'help', 'Nothing can change your map until you turn this on. It turns itself off when you close or reload this tab.'));
                        body.appendChild(arm0);
                        var head = el('h3'), list = el('div', 'aik-keys');
                        head.appendChild(el('span', '', 'Your keys')); head.appendChild(el('span', 'cnt', keys.length + ' of ' + max));
                        body.appendChild(head);
                        if (!keys.length) list.appendChild(el('div', 'aik-empty', 'You have no keys yet. Make one to connect an AI.'));
                        keys.forEach(function (k) { list.appendChild(keyRow(k, list, ctl)); });
                        body.appendChild(list);
                        var how = el('button', 'link', 'How to connect an AI app'); how.type = 'button';
                        how.addEventListener('click', function () { showConnect(''); });
                        body.appendChild(how);
                    },
                    buttons: [{ id: 'close', text: 'Close', ghost: true }].concat(on ? [{ id: 'make', text: 'Make a key...', keepOpen: true, onClick: function () { if (keys.length >= max) { env.announce('You have ' + max + ' keys: revoke one first.'); return; } showMake(); } }] : [])
                });
                if (ctl.buttons.make && keys.length >= max) { ctl.buttons.make.disabled = true; ctl.buttons.make.title = 'You have ' + max + ' keys: revoke one first'; }
            });
        }

        function showMake() {
            var name = el('input'), scope = 'edit', days = 30, warn = el('div'), when = el('div', 'help');
            name.type = 'text'; name.id = 'aik-name'; name.maxLength = MAX_NAME; name.value = 'My AI'; name.setAttribute('autocomplete', 'off'); name.setAttribute('spellcheck', 'false');
            function ends() { var t = new Date(Date.now() + days * 86400000); when.textContent = 'This key will stop working by itself on ' + MONTHS[t.getMonth()] + ' ' + t.getDate() + '. There is no key that never ends.'; }
            open({
                title: 'Make a key', cls: 'aikey', focus: '#aik-name',
                build: function (body) {
                    body.appendChild(el('p', 'lead', 'A key lets one AI app talk to this Map Maker as you. You will see it once; keep it like a password.'));
                    var f = el('div', 'pfield'), l = el('label', '', 'Name (only you see it)');
                    l.htmlFor = 'aik-name'; f.appendChild(l); f.appendChild(name); f.appendChild(el('div', 'help', 'For example the computer or the app it will live on.')); body.appendChild(f);
                    var lg = el('div', 'pfield'), kinds = el('div', 'kinds stacked');
                    lg.appendChild(el('div', 'lg', 'What it may do'));
                    KINDS.forEach(function (k) {
                        var lab = el('label', 'kind' + (k[0] === scope ? ' on' : '')), r = el('input'), rb = el('span', 'rb');
                        r.type = 'radio'; r.name = 'aik-can'; r.checked = k[0] === scope; rb.setAttribute('aria-hidden', 'true');
                        r.addEventListener('change', function () { scope = k[0]; Array.prototype.forEach.call(kinds.children, function (c) { c.classList.toggle('on', c === lab); }); });
                        lab.appendChild(r); lab.appendChild(rb); lab.appendChild(el('span', 'kt', k[1])); lab.appendChild(el('span', 'kl', k[2])); kinds.appendChild(lab);
                    });
                    lg.appendChild(kinds); body.appendChild(lg);
                    var ex = el('div', 'pfield'), row = el('div', 'aik-days');
                    ex.appendChild(el('div', 'lg', 'Stops working after'));
                    DAYS.forEach(function (d) {
                        var lab = el('label', 'pc' + (d === days ? ' on' : '')), r = el('input');
                        r.type = 'radio'; r.name = 'aik-days'; r.checked = d === days;
                        r.addEventListener('change', function () { days = d; Array.prototype.forEach.call(row.children, function (c) { c.classList.toggle('on', c === lab); }); ends(); });
                        lab.appendChild(r); lab.appendChild(doc.createTextNode(d + ' days')); row.appendChild(lab);
                    });
                    ex.appendChild(row); ex.appendChild(when); body.appendChild(ex);
                    ends();
                    body.appendChild(warn);
                    body.appendChild(note('', 'i', 'You can have ' + max + ' keys at a time. You can revoke any of them whenever you like.'));
                },
                buttons: [{ id: 'back', text: 'Back', ghost: true, keepOpen: true, onClick: function () { showKeys(); } },
                          { id: 'make', text: 'Make the key', keepOpen: true, onClick: function (ctl) {
                              var text = name.value.replace(/^\s+|\s+$/g, '');
                              if (!text) { warn.textContent = ''; warn.appendChild(note('bad', '!', 'Give the key a name.')); return; }
                              ctl.setDisabled('make', true);
                              api('POST', '/hub/ai/keys', { name: text, scope: scope, days: days }, function (status, j) {
                                  if (status === 200 && j && j.key) return showShown(j);
                                  ctl.setDisabled('make', false);
                                  var why = status === 409 ? 'You have ' + max + ' keys: revoke one first.' : status === 429 ? 'You made as many keys today as one account may. Try again tomorrow.' : status === 400 ? 'That name or choice did not work. Change it and try again.' : status === 403 ? 'The page was not let in. Reload the page and try again.' : 'The site did not answer. Check your connection and try again.';
                                  warn.textContent = ''; warn.appendChild(note('bad', '!', why));
                              });
                          } }]
            });
        }

        function copy(text, done) {
            var c = win.navigator && win.navigator.clipboard;
            if (c && c.writeText) return c.writeText(text).then(function () { done(true); }, function () { done(false); });
            done(false);
        }
        function showShown(made) {
            shownKey = made.key;
            var codeEl = el('code', '', made.key), said = el('span', 'small');
            open({
                title: 'Your key', cls: 'aikey', onClose: function () { shownKey = ''; codeEl.textContent = ''; },
                build: function (body) {
                    var ok = el('div', 'aik-ok'), tick = el('span', 'tick', '\u2713');
                    tick.setAttribute('aria-hidden', 'true'); ok.appendChild(tick); ok.appendChild(el('span', '', 'The key \u201c' + made.name + '\u201d is ready.')); body.appendChild(ok);
                    var s = el('div', 'aik-secret'), cp = button('Copy key', 'gold', function () { copy(made.key, function (good) { said.textContent = good ? 'Copied.' : 'Select the key and copy it yourself.'; }); });
                    s.appendChild(codeEl); s.appendChild(cp); body.appendChild(s); body.appendChild(said);
                    var w = el('div', 'note aik-warn'), d = el('div'), strong = el('b', '', 'This is the only time you will see the whole key. ');
                    w.appendChild(el('span', 'ic', '!')); d.appendChild(strong);
                    d.appendChild(doc.createTextNode('We keep only a scrambled copy, so we cannot show it again. Treat it like a password: with it, an AI can edit the map in your tab while you have \u201cLet an AI work in this tab\u201d on. If it leaks, revoke it here.'));
                    w.appendChild(d); body.appendChild(w);
                    body.appendChild(el('p', 'aik-how', 'Next you put the key into your AI app. It takes a minute.'));
                },
                buttons: [{ id: 'later', text: 'Not now', ghost: true, keepOpen: true, onClick: function (ctl) { ctl.close(); showKeys(); } }, { id: 'next', text: 'Next: connect your AI', keepOpen: true, onClick: function (ctl) { var k = shownKey; showConnect(k); } }]
            });
        }

        function showConnect(key) {
            var kind = 'code', pre = el('pre', 'aik-code'), copyNote = el('span', 'small'), lead = el('p', 'lead'), tabs = el('div', 'aik-tabs'), tabButtons = [];
            var TABS = [['code', 'Claude Code', 'one command', 'Run this once in a terminal. It adds the Map Maker to Claude Code with your key.'],
                        ['file', 'Desktop apps', 'a small file', 'For apps that read an MCP settings file and take a web address with headers (Cursor, VS Code, Windsurf and others), add this to the file. An app that can only start programs on your computer cannot use it yet.'],
                        ['web', 'Other AIs', 'plain web calls', 'A program or an AI tool that makes web calls: send this. The OpenAPI file at ' + origin + '/hub/ai/v1/openapi.json lists every tool.']];
            function show() {
                var t = TABS.filter(function (x) { return x[0] === kind; })[0];
                lead.textContent = t[3];
                pre.textContent = commandFor(kind, origin, key);
                tabButtons.forEach(function (b, i) { b.className = 'play-tab aik-tab' + (TABS[i][0] === kind ? ' on' : ''); b.setAttribute('aria-selected', String(TABS[i][0] === kind)); });
                copyNote.textContent = key ? 'The key is inside the text. Do not paste it in a chat or a public place. Running or saving it keeps the key on this computer (the terminal\u2019s history, the AI app\u2019s settings): if the computer is shared, revoke the key when you are done.' : 'Put your key where it says <your key>. A key is shown only once, when it is made.';
            }
            open({
                title: 'Connect your AI', cls: 'aikey', onClose: function () { key = ''; pre.textContent = ''; },
                build: function (body) {
                    tabs.setAttribute('role', 'tablist');
                    TABS.forEach(function (t) {
                        var b = el('button', 'play-tab aik-tab'); b.type = 'button'; b.setAttribute('role', 'tab');
                        b.appendChild(el('span', 'play-tab-t', t[1])); b.appendChild(el('span', 'play-tab-s', t[2]));
                        b.addEventListener('click', function () { kind = t[0]; show(); });
                        tabs.appendChild(b); tabButtons.push(b);
                    });
                    body.appendChild(tabs); body.appendChild(lead); body.appendChild(pre);
                    var cp = el('div', 'aik-cp'), c = button('Copy', 'sm', function () { copy(pre.textContent, function (good) { copyNote.textContent = good ? 'Copied.' : 'Select the text and copy it yourself.'; }); });
                    cp.appendChild(c); cp.appendChild(copyNote); body.appendChild(cp);
                    var sk = el('div', 'aik-skills');
                    sk.appendChild(el('div', 'hd', 'Teach it to make good maps'));
                    sk.appendChild(el('p', '', 'Skills are short guides your AI reads when you ask for an Ants map: the tiles and rules, fair layouts, how to check and fix a map, and how to use these tools. Get them once.'));
                    var row = el('div', 'row'), get = el('a', 'btn gold', 'Get the skills (.zip)'), more = el('button', 'link', 'What is in them, and how to install them');
                    get.setAttribute('href', '/ai/skills/all.zip'); get.setAttribute('download', 'ants-map-skills.zip'); more.type = 'button';
                    more.addEventListener('click', showSkills);
                    row.appendChild(get); row.appendChild(more); sk.appendChild(row); body.appendChild(sk);
                    show();
                },
                buttons: [{ id: 'back', text: 'Back', ghost: true, keepOpen: true, onClick: function () { showKeys(); } }, { id: 'done', text: 'Done' }]
            });
        }

        function showSkills() {
            var list = el('div', 'aik-skillist'), all = el('div', 'aik-cp');
            if (env.fetch) env.fetch('/ai/skills/index.json', { credentials: 'same-origin' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (data) { fill(data); }, function () { fill(null); });
            else fill(null);
            function fill(data) {
                list.textContent = '';
                if (!data || !data.skills) { list.appendChild(el('p', 'aik-how', 'The list of skills could not be loaded just now. Try again in a moment.')); return; }
                var total = data.all ? Math.round(data.all.bytes / 1024) : 0, top = el('a', 'btn gold', 'Download all ' + data.skills.length + ' (.zip)');
                top.setAttribute('href', '/ai/skills/all.zip'); top.setAttribute('download', 'ants-map-skills.zip');
                all.textContent = ''; all.appendChild(top); if (total) all.appendChild(el('span', 'small', 'About ' + total + ' KB in all. They are updated together with the Map Maker.'));
                data.skills.forEach(function (s) {
                    var r = el('div', 'aik-skill'), get = el('a', 'link', 'Get .zip');
                    get.setAttribute('href', '/ai/skills/' + encodeURIComponent(s.name) + '.zip'); get.setAttribute('download', s.name + '.zip'); get.setAttribute('aria-label', 'Get ' + s.name + ' as a zip');
                    r.appendChild(el('span', 'nm', s.name)); r.appendChild(el('span', 'd', s.description)); r.appendChild(get); list.appendChild(r);
                });
            }
            open({
                title: 'Skills for your AI', cls: 'aikey',
                build: function (body) {
                    body.appendChild(el('p', 'lead', 'Short guides. Each is a folder with a SKILL.md that Claude and other agent apps load by themselves when you ask for an Ants map.'));
                    body.appendChild(all); body.appendChild(list);
                    var how = el('p', 'aik-how');
                    [['Claude apps: ', 'Settings, Capabilities, Skills, upload one skill zip at a time. '], ['Claude Code: ', 'unzip the folders into ~/.claude/skills/. '], ['Other AIs: ', 'the same text is available to them through the connection itself (the tool \u201cread_guide\u201d), so nothing to install.']].forEach(function (p) { how.appendChild(el('b', '', p[0])); how.appendChild(doc.createTextNode(p[1])); });
                    body.appendChild(how);
                },
                buttons: [{ id: 'back', text: 'Back', ghost: true, keepOpen: true, onClick: function () { showKeys(); } }, { id: 'done', text: 'Done' }]
            });
        }

        buildStrip();
        return {
            open: showKeys,
            touch: function () { tools.touch(); },
            armed: function () { return state !== 'off' && state !== 'connecting'; },
            state: function () { return state; },
            stop: function () { if (sock) disarm(); },
            tools: tools,
            // for tests: the pure pieces
            commandFor: commandFor
        };
    }

    return { create: create, commandFor: commandFor, ago: ago, metaOf: metaOf, dateText: dateText };
});

// The front page's map picker (docs/PLAY_IN_BROWSER.md "Choosing a map"): the list of the maps that the game server offers (GET /maps: the six originals and the players' maps, with a preview picture and how
// often each was played), shown as a Detail list or a Thumbnails grid, with a search over name and description and four sorts. The page opens it from the map selector; choosing a map hands the entry to the page,
// which sets the room's map as the selector does.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsMapPicker, node gets module.exports (tests/scripts/web_map_picker_check.js runs it, the pure functions on tables and the
//     whole picker with a small fake of the browser's DOM).
//   * Nothing a server says is ever written as markup: names, descriptions and addresses are set as text or as an attribute of an element made here.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsMapPicker = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var VIEW_KEY = 'ants.mapview';                 // localStorage: 'detail' (the default) or 'thumbs'
    var SORTS = [
        { id: 'default', label: 'Originals, then plays' },
        { id: 'plays', label: 'Most played' },
        { id: 'name', label: 'Name' },
        { id: 'size', label: 'Size' }
    ];

    function whole(n) { return typeof n === 'number' && isFinite(n) && Math.floor(n) === n && n >= 0; }
    function text(s, max) { return typeof s === 'string' ? s.replace(/[\u0000-\u001f\u007f]/g, ' ').slice(0, max) : ''; }
    // The name that a map is shown under: an original by its word ("TREASURE.LVL" is Treasure), a map of the players by its file name without the ending
    function displayName(file, original) {
        var base = String(file).replace(/\.lvl$/i, '');
        return original ? base.charAt(0).toUpperCase() + base.slice(1).toLowerCase() : base;
    }
    // The game's rule for a map's file name (net::valid_map_name, protocol.cpp): 5 - 64 printable ASCII characters, none of / \\ : * ? " < > |, no leading dot, ends in exactly .lvl or .LVL
    function validMapFile(name) {
        return typeof name === 'string' && name.length >= 5 && name.length <= 64 && /^[\x20-\x7e]+$/.test(name) && !/[\/\\:*?"<>|]/.test(name) && name.charAt(0) !== '.' && /\.(lvl|LVL)$/.test(name);
    }
    // A picture's address is the site's own and exactly one of the shapes the server writes: /maps/previews/<percent-encoded name>.webp (the library's), /maps/previews/<percent-encoded name>.png (a map that a player sent and the owner approved, docs/MAP_SUBMISSIONS.md) or /front/preview_<word>.png. A name holds unreserved characters and
    // %XX escapes (never an escaped slash, backslash or NUL), no query, no leading dot
    var NAME_PART = "(?:[A-Za-z0-9_~-]|%(?!2[fF]|5[cC]|00)[0-9A-Fa-f]{2})(?:[A-Za-z0-9._~-]|%(?!2[fF]|5[cC]|00)[0-9A-Fa-f]{2})*";
    var PICTURE = new RegExp('^(?:/maps/previews/' + NAME_PART + '\\.(?:webp|png)|/front/preview_[a-z]+\\.png)$');
    function ownPath(url) { return typeof url === 'string' && PICTURE.test(url) ? url : ''; }
    // A character string as an address part, as the server writes it (map_store.cpp percent_encode): everything but the unreserved characters is %XX (encodeURIComponent leaves ! ' ( ) * as they are, which the server never does)
    function percentEncode(s) {
        var out = '';
        for (var i = 0; i < s.length; i++) {
            var c = s.charAt(i);
            out += /[A-Za-z0-9\-._~]/.test(c) ? c : '%' + ('0' + s.charCodeAt(i).toString(16).toUpperCase()).slice(-2);
        }
        return out;
    }
    var ORIGINALS = /^(?:tiny|small|medium|gauntlet|treasure|islands)\.lvl$/i;       // the six that the front page has its own pictures of (/front/preview_<word>.png)
    // The picture address of a map of the players from its name alone, as a match record has it ("POPcOrN.lvl", or "POPcOrN"): /maps/previews/<file name without .lvl>.webp, or '' for a name that is not a map's file name (the game's
    // rule above), one of the six originals, or anything outside the two shapes the page loads
    function previewPath(map) {
        if (typeof map !== 'string') return '';
        var base = map.replace(/\.lvl$/i, ''), file = base + '.lvl';
        if (!validMapFile(file) || ORIGINALS.test(file)) return '';
        return ownPath('/maps/previews/' + percentEncode(base) + '.webp');
    }
    // The picture of the map that a match record names, from the list that parseList made (null while it is not known): the list's own entry says what the server serves (the record may spell the name in another
    // case, and the server may have no picture for it: then none); a map the list does not hold has the address that its name makes
    function thumbFor(map, list) {
        if (typeof map !== 'string') return '';
        var base = map.replace(/\.lvl$/i, ''), file = base + '.lvl';
        if (!validMapFile(file) || ORIGINALS.test(file)) return '';
        if (list) {
            var low = null;
            for (var i = 0; i < list.length; i++) {
                if (list[i].file.replace(/\.lvl$/i, '') === base) return list[i].original ? '' : list[i].thumb;
                if (!low && list[i].file.toLowerCase() === file.toLowerCase()) low = list[i];
            }
            if (low) return low.original ? '' : low.thumb;
        }
        return previewPath(map);
    }

    // The list as the server sends it ({"format":1|2,"maps":[...]}) as entries the picker uses, in the server's order; an entry that is no map (a bad name, no number) is left out. null when it is no list.
    function parseList(data) {
        if (!data || (data.format !== 1 && data.format !== 2) || !Array.isArray(data.maps)) return null;
        var out = [];
        data.maps.forEach(function (m, i) {
            if (!m || !validMapFile(m.file)) return;
            var original = m.original === true;
            var kind = m.kind === 'extended' ? 'extended' : 'classic';
            out.push({
                file: m.file, name: displayName(m.file, original), original: original, kind: kind, order: i,
                width: whole(m.width) ? m.width : 0, height: whole(m.height) ? m.height : 0, minutes: whole(m.minutes) ? m.minutes : 0, players: whole(m.players) ? m.players : 0,
                description: text(m.description, 200), plays: whole(m.plays) ? m.plays : 0, thumb: ownPath(m.thumb),
                online: m.online !== false, ants: whole(m.ants) ? m.ants : 0       // (online false: a room refuses the map, too big or too many start ants; ants says how many in the second case)
            });
        });
        return out;
    }
    function byName(a, b) {
        var x = a.name.toLowerCase(), y = b.name.toLowerCase();
        return x < y ? -1 : x > y ? 1 : a.file < b.file ? -1 : a.file > b.file ? 1 : 0;
    }
    // The order of a list: 'default' the originals first (the server's order), then the others by plays, most first; 'plays' every map by plays; 'name'; 'size' by the map's area, smallest first. Ties go by name.
    function sortMaps(list, mode) {
        var out = list.slice();
        out.sort(function (a, b) {
            if (mode === 'name') return byName(a, b);
            if (mode === 'size') return (a.width * a.height) - (b.width * b.height) || byName(a, b);
            if (mode === 'plays') return (b.plays - a.plays) || byName(a, b);
            if (a.original !== b.original) return a.original ? -1 : 1;
            return a.original ? a.order - b.order : (b.plays - a.plays) || byName(a, b);
        });
        return out;
    }
    // The maps whose name or description holds every word of the search (case ignored)
    function filterMaps(list, query) {
        var words = String(query || '').toLowerCase().split(/\s+/).filter(function (w) { return w; });
        if (!words.length) return list.slice();
        return list.filter(function (m) {
            var hay = (m.name + ' ' + m.description).toLowerCase();
            return words.every(function (w) { return hay.indexOf(w) !== -1; });
        });
    }
    // Why a room does not play a map that the server marked "online": false (the server's own sentences are in chat when the leader chooses it anyway)
    function offlineText(m) { return m.ants > 0 ? 'Too many ants for an online game' : 'Too big for an online game'; }
    function playsText(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    function metaText(m) { return m.original ? 'Shipped with the game' : m.players + ' players · ' + m.minutes + ' min · ' + m.width + '×' + m.height; }
    // The Map Info line of a map of the players, as the selector shows it under the picture
    function infoText(m) { return (m.description ? m.description + ' ' : '') + '(' + m.minutes + ' min)'; }

    function recall(storage) {
        try { var v = storage && storage.getItem(VIEW_KEY); return v === 'thumbs' ? 'thumbs' : 'detail'; } catch (e) { return 'detail'; }
    }
    function remember(storage, v) { try { if (storage) storage.setItem(VIEW_KEY, v); } catch (e) { /* the choice is for this visit only */ } }

    // opts: { host (the dialog's panel; its parent is the overlay that is shown and hidden), doc, fetch, storage, setTimeout, clearTimeout, AbortController, now, matchMedia,
    //         canOpen() and canChoose() (the host of the room, START not pressed: asked when the list opens and again when a map is chosen), onChoose(entry), onList(), onOpen(), onClose() }
    function create(opts) {
        var doc = opts.doc, host = opts.host;
        var later = opts.setTimeout || function (f, ms) { return setTimeout(f, ms); }, unlater = opts.clearTimeout || function (t) { clearTimeout(t); };
        var clock = opts.now || function () { return Date.now(); };
        var list = null, status = 'idle', view = recall(opts.storage), sort = 'default', query = '', picked = '', shown = [], opened = false, byFile = {}, byLower = {};
        var parts = {}, typing = 0, failedAt = 0, attempt = 0;

        function el(tag, cls, txt) { var e = doc.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; }
        function button(cls, label) { var b = el('button', cls, label); b.setAttribute('type', 'button'); return b; }
        // a file name as the list has it (the room may say it in another case)
        function canon(file) { return byFile[file] ? file : byLower[String(file || '').toLowerCase()] ? byLower[String(file).toLowerCase()].file : file; }
        function find(file) { return byFile[file] || byLower[String(file || '').toLowerCase()] || null; }

        function build() {
            host.textContent = '';
            host.setAttribute('aria-labelledby', 'picker-h'); host.setAttribute('tabindex', '-1');
            var head = el('div', 'head');
            var h = el('h2', '', 'Choose a map'); h.id = 'picker-h';
            parts.close = button('btn sm', 'Close'); parts.close.setAttribute('aria-label', 'Close the map list');
            head.appendChild(h); head.appendChild(parts.close);
            var bar = el('div', 'pk-bar');
            parts.q = el('input', 'pk-q'); parts.q.setAttribute('type', 'search'); parts.q.setAttribute('aria-label', 'Search the maps by name or description'); parts.q.setAttribute('autocomplete', 'off'); parts.q.setAttribute('spellcheck', 'false'); parts.q.setAttribute('placeholder', 'Search the maps');
            var seg = el('span', 'pk-seg'); seg.setAttribute('role', 'group'); seg.setAttribute('aria-label', 'View');
            parts.vD = button('pk-chip', 'Detail'); parts.vT = button('pk-chip', 'Thumbnails');
            seg.appendChild(parts.vD); seg.appendChild(parts.vT);
            bar.appendChild(parts.q); bar.appendChild(seg);
            var sorts = el('div', 'pk-sorts'); sorts.setAttribute('role', 'group'); sorts.setAttribute('aria-label', 'Sort by');
            sorts.appendChild(el('span', '', 'Sort by'));
            parts.sorts = {};
            SORTS.forEach(function (s) { var b = button('pk-chip', s.label); b.setAttribute('data-sort', s.id); parts.sorts[s.id] = b; sorts.appendChild(b); });
            parts.out = el('div', 'pk-out');
            parts.fine = el('p', 'pk-fine', 'Plays count only matches with people in them that lasted at least 30 seconds.');
            var foot = el('div', 'pk-foot');
            parts.count = el('span', 'pk-count'); parts.count.setAttribute('role', 'status'); parts.count.setAttribute('aria-live', 'polite');
            parts.go = button('btn', 'Choose map');
            foot.appendChild(parts.count); foot.appendChild(parts.go);
            [head, bar, sorts, parts.out, parts.fine, foot].forEach(function (n) { host.appendChild(n); });

            parts.close.addEventListener('click', close);
            parts.q.addEventListener('input', function () { unlater(typing); typing = later(function () { typing = 0; query = parts.q.value; paint('top'); }, 120); });       // (a search is made when the typing pauses)
            parts.q.addEventListener('keydown', function (e) { if (e.key === 'ArrowDown') { e.preventDefault(); flush(); focusItem(pickedIndex() >= 0 ? pickedIndex() : 0); } });
            parts.vD.addEventListener('click', function () { setView('detail'); });
            parts.vT.addEventListener('click', function () { setView('thumbs'); });
            SORTS.forEach(function (s) { parts.sorts[s.id].addEventListener('click', function () { flush(); sort = s.id; paint('picked'); }); });
            parts.go.addEventListener('click', choose);
            parts.out.addEventListener('click', function (e) {
                var node = e.target && e.target.closest ? e.target.closest('[data-file]') : null;
                if (node) { picked = node.getAttribute('data-file'); markPicked(); }
                else if (e.target && e.target.getAttribute && e.target.getAttribute('data-retry')) load(true);
            });
            parts.out.addEventListener('dblclick', function (e) {
                var node = e.target && e.target.closest ? e.target.closest('[data-file]') : null;
                if (node) { picked = node.getAttribute('data-file'); choose(); }
            });
            parts.out.addEventListener('keydown', onListKey);
            host.addEventListener('keydown', onHostKey);
        }
        // a search that is waiting for the typing to pause is made now
        function flush() { if (typing) { unlater(typing); typing = 0; query = parts.q.value; paint('top'); } }

        function pickedIndex() { for (var i = 0; i < shown.length; i++) if (shown[i].file === picked) return i; return -1; }
        function items() { return parts.out.querySelectorAll ? parts.out.querySelectorAll('[data-file]') : []; }
        function focusItem(i) {
            var nodes = items();
            if (!nodes.length) return;
            i = Math.max(0, Math.min(nodes.length - 1, i));
            picked = nodes[i].getAttribute('data-file');
            markPicked();
            nodes[i].focus();
            if (nodes[i].scrollIntoView) nodes[i].scrollIntoView({ block: 'nearest' });
        }
        // how many items stand in one row of the grid (the list has one): the items that share the first one's top
        function perRow() {
            if (view !== 'thumbs') return 1;
            var nodes = items(), n = 0;
            if (!nodes.length) return 1;
            var top = nodes[0].offsetTop;
            for (var i = 0; i < nodes.length && nodes[i].offsetTop === top; i++) n++;
            return Math.max(1, n);
        }
        function onListKey(e) {
            var i = pickedIndex(), step = perRow(), k = e.key;
            if (k === 'ArrowDown') i += step; else if (k === 'ArrowUp') i -= step;
            else if (k === 'ArrowRight' && view === 'thumbs') i += 1; else if (k === 'ArrowLeft' && view === 'thumbs') i -= 1;
            else if (k === 'Home') i = 0; else if (k === 'End') i = shown.length - 1;
            else if (k === 'PageDown') i += 8 * step; else if (k === 'PageUp') i -= 8 * step;
            else if (k === 'Enter') { e.preventDefault(); if (i >= 0) choose(); return; }
            else if (k === ' ') { e.preventDefault(); return; }
            else return;
            e.preventDefault();
            focusItem(i < 0 && k === 'ArrowUp' ? 0 : i);
        }
        // Esc closes; Tab stays inside the dialog
        function onHostKey(e) {
            if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); close(); return; }
            if (e.key !== 'Tab') return;
            var stops = [parts.close, parts.q, parts.vD, parts.vT];
            SORTS.forEach(function (s) { stops.push(parts.sorts[s.id]); });
            var cur = parts.out.querySelector ? parts.out.querySelector('[data-file][tabindex="0"]') : null;
            if (cur) stops.push(cur);
            var retry = parts.out.querySelector ? parts.out.querySelector('[data-retry]') : null;
            if (retry) stops.push(retry);
            stops.push(parts.go);
            var at = stops.indexOf(doc.activeElement);
            var next = at === -1 ? 0 : (at + (e.shiftKey ? -1 : 1) + stops.length) % stops.length;
            if (at === -1 && doc.activeElement && parts.out.contains && parts.out.contains(doc.activeElement)) next = stops.indexOf(cur) + (e.shiftKey ? -1 : 1);
            e.preventDefault();
            stops[(next + stops.length) % stops.length].focus();
        }
        // Esc closes the list even when the focus is on the page's body (a click on the dialog's own text, a tap on a phone)
        function onDocKey(e) { if (opened && e.key === 'Escape') { e.preventDefault(); close(); } }

        function setView(v) { flush(); view = v; remember(opts.storage, v); paint('picked'); }

        function item(m) {
            var thumbs = view === 'thumbs', px = thumbs ? 130 : 64;
            var d = el('div', thumbs ? 'pk-card' : 'pk-row');
            d.setAttribute('role', 'option'); d.setAttribute('data-file', m.file); d.setAttribute('tabindex', m.file === picked ? '0' : '-1'); d.setAttribute('aria-selected', m.file === picked ? 'true' : 'false');
            if (m.file === picked) d.className += ' sel';
            if (m.thumb) {
                var img = el('img'); img.setAttribute('src', m.thumb); img.setAttribute('alt', ''); img.setAttribute('width', String(px)); img.setAttribute('height', String(px)); img.setAttribute('loading', 'lazy'); img.setAttribute('decoding', 'async');
                d.appendChild(img);
            } else d.appendChild(el('span', 'pk-nopic'));
            var body = el('div', 'pk-body');
            var nm = el('div', 'pk-nm', m.name);
            if (m.original) nm.appendChild(el('span', 'pk-badge', 'Original'));
            else if (m.kind === 'extended') nm.appendChild(el('span', 'pk-badge pk-ext', 'Extended'));
            if (!m.online) nm.appendChild(el('span', 'pk-badge pk-off', offlineText(m)));
            body.appendChild(nm);
            if (!thumbs) {
                body.appendChild(el('div', 'pk-meta', metaText(m)));
                var pl = el('div', 'pk-pl'); pl.appendChild(el('b', '', playsText(m.plays))); pl.appendChild(doc.createTextNode(m.plays === 1 ? 'play' : 'plays'));
                d.appendChild(body); d.appendChild(pl);
            } else {
                body.appendChild(el('div', 'pk-meta', playsText(m.plays) + (m.plays === 1 ? ' play' : ' plays')));
                d.appendChild(body);
            }
            return d;
        }
        function markPicked() {
            var nodes = items();
            for (var i = 0; i < nodes.length; i++) {
                var on = nodes[i].getAttribute('data-file') === picked;
                nodes[i].setAttribute('aria-selected', on ? 'true' : 'false'); nodes[i].setAttribute('tabindex', on ? '0' : '-1');
                nodes[i].className = nodes[i].className.replace(/ sel\b/, '') + (on ? ' sel' : '');
            }
            parts.go.disabled = pickedIndex() < 0;   // (a pick that a search hides is not chosen)
        }

        // where the list stands afterwards: 'top' a search (a new list), 'picked' a sort or a view (the chosen row is brought into view, else the list stays where it was), '' nothing is moved
        function paint(where) {
            var was = parts.out.scrollTop || 0;
            parts.vD.setAttribute('aria-pressed', String(view === 'detail')); parts.vT.setAttribute('aria-pressed', String(view === 'thumbs'));
            SORTS.forEach(function (s) { parts.sorts[s.id].setAttribute('aria-pressed', String(sort === s.id)); });
            parts.out.textContent = '';
            parts.out.removeAttribute('role'); parts.out.removeAttribute('aria-label');
            parts.out.className = 'pk-out';
            parts.go.disabled = true;
            if (status === 'loading' || status === 'idle') { parts.out.appendChild(el('p', 'pk-note', 'Loading the maps…')); parts.count.textContent = ''; return; }
            if (status === 'error') {
                var p = el('div', 'pk-note pk-err');
                p.appendChild(el('p', '', 'The list of maps is not available right now. The six original maps can still be chosen with the arrows beside the picture.'));
                var r = button('btn sm', 'Try again'); r.setAttribute('data-retry', '1'); p.appendChild(r);
                parts.out.appendChild(p); parts.count.textContent = ''; return;
            }
            parts.q.setAttribute('placeholder', 'Search ' + list.length + ' maps');
            shown = sortMaps(filterMaps(list, query), sort);
            parts.count.textContent = shown.length + ' of ' + list.length + ' maps shown';
            if (!shown.length) { var e = el('div', 'pk-note pk-empty'); e.appendChild(el('b', '', 'No map is called "' + query.trim() + '".')); e.appendChild(el('span', '', 'Try fewer letters.')); parts.out.appendChild(e); return; }
            if (!byFile[picked]) picked = '';
            parts.out.className = 'pk-out ' + (view === 'thumbs' ? 'pk-grid' : 'pk-list');
            parts.out.setAttribute('role', 'listbox'); parts.out.setAttribute('aria-label', 'Maps');
            var frag = doc.createDocumentFragment ? doc.createDocumentFragment() : parts.out;
            var firstStop = pickedIndex() < 0;
            shown.forEach(function (m, i) { var n = item(m); if (firstStop && i === 0) n.setAttribute('tabindex', '0'); frag.appendChild(n); });
            if (frag !== parts.out) parts.out.appendChild(frag);
            parts.go.disabled = pickedIndex() < 0;   // (a pick that a search hides is not chosen)
            if (where === 'top') parts.out.scrollTop = 0;
            else if (where === 'picked') {
                var cur = parts.out.querySelector ? parts.out.querySelector('[data-file].sel') : null;
                if (cur && cur.scrollIntoView) cur.scrollIntoView({ block: 'center' }); else parts.out.scrollTop = was;
            }
        }

        // The list: asked for once for the visit. A try ends in 12 s at the latest (a stuck connection is a failure with Try again, never a list that loads for ever); `again` is a person's retry, a soft ask
        // (the page needing the list for a map in the room) waits 15 s after a failure
        function load(again) {
            if (status === 'ready' && !again) return;
            if (status === 'loading' && !again) return;
            if (!again && status === 'error' && clock() - failedAt < 15000) return;
            var mine = ++attempt, timer = 0, ctl = null;
            status = 'loading';
            if (opened) paint('');
            function failed() { if (mine !== attempt) return; attempt++; unlater(timer); if (ctl) { try { ctl.abort(); } catch (e) { /* nothing to stop */ } } status = 'error'; failedAt = clock(); if (opened) paint(''); }
            var Ctl = opts.AbortController;
            var init = { credentials: 'same-origin' };
            if (Ctl) { ctl = new Ctl(); init.signal = ctl.signal; }
            timer = later(failed, 12000);
            var request;
            try { request = opts.fetch('/maps/all', init); } catch (e) { failed(); return; }
            request.then(function (r) { if (!r.ok) throw new Error('status'); return r.json(); }).then(function (data) {
                if (mine !== attempt) return;
                var parsed = parseList(data);
                if (!parsed || !parsed.length) { failed(); return; }
                attempt++; unlater(timer);
                list = parsed; byFile = {}; byLower = {}; list.forEach(function (m) { byFile[m.file] = m; if (!byLower[m.file.toLowerCase()]) byLower[m.file.toLowerCase()] = m; });
                status = 'ready';
                if (opened) { picked = canon(picked); paint('picked'); }
                if (opts.onList) opts.onList();
            }).then(undefined, failed);
        }

        // Opens the list with `file` (the room's map now) picked; nothing opens for a page that is not the room's host
        function open(file) {
            if (opts.canOpen && !opts.canOpen()) return;
            if (!parts.out) build();
            opened = true; picked = canon(file || ''); query = ''; parts.q.value = '';
            host.parentNode.hidden = false;
            if (doc.addEventListener) doc.addEventListener('keydown', onDocKey);
            if (opts.onOpen) opts.onOpen();
            if (status === 'error' || status === 'idle') load(true); else if (status === 'loading') paint('');
            paint('picked');
            // a phone's keyboard would cover the sheet: the search box takes the focus on a screen with a mouse only
            var coarse = false;
            try { coarse = !!(opts.matchMedia && opts.matchMedia('(pointer: coarse)').matches); } catch (e) { coarse = false; }
            (coarse ? host : parts.q).focus();
        }
        function close() {
            if (!opened) return;
            opened = false;
            unlater(typing); typing = 0;
            host.parentNode.hidden = true;
            if (doc.removeEventListener) doc.removeEventListener('keydown', onDocKey);
            if (opts.onClose) opts.onClose();
        }
        function choose() {
            flush();
            var m = byFile[picked];
            if (!m) return;
            var allowed = !opts.canChoose || opts.canChoose();                     // (the room may have changed hands, or START been pressed, while the list was open)
            close();
            if (allowed && opts.onChoose) opts.onChoose(m);
        }
        return {
            open: open, close: close, load: function () { load(false); }, isOpen: function () { return opened; },
            find: find, status: function () { return status; }, view: function () { return view; }, entries: function () { return list; }
        };
    }

    return { VIEW_KEY: VIEW_KEY, SORTS: SORTS, validMapFile: validMapFile, previewPath: previewPath, thumbFor: thumbFor, percentEncode: percentEncode, displayName: displayName, parseList: parseList, sortMaps: sortMaps, filterMaps: filterMaps, playsText: playsText, metaText: metaText, infoText: infoText, offlineText: offlineText, create: create };
});

// The front page's map picker (docs/PLAY_IN_BROWSER.md "Choosing a map"): the maps that the game server offers (GET /maps/all), as a Detail list or a Thumbnails grid, with a search and four sorts, under two
// headings by default, the room's map marked In this room, and on a wide screen a preview of the map that is picked. The map card's Change map button opens it; choosing a map hands the entry to the page. Until
// the list is there, and when it cannot be had, the six originals are shown from the page's own table (opts.originals).
//   * ES5 and no module system: a page gets the global AntsMapPicker, node gets module.exports (tests/scripts/web_map_picker_check.js).
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
    // The picture of the map that a match record names, from the list that parseList made (null while not known): the list's own entry says what the server serves; a map not in the list has the address its name makes
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
                heavy: m.heavy === true,      // (the server's flag: more start ants than the game runs comfortably everywhere; a warning on the row)
                tooManyAnts: m.online === false && m.online_why === 'ants'       // (the server's flag: more start ants than a room plays)
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
    // The words of the warning chip of a heavy map (a game on it may run slowly; the map can be chosen as any other)
    var HEAVY_TEXT = 'Many ants: may run slowly';
    // The chip of a map that starts more ants than a room plays (it can still be played on this device)
    var TOO_MANY_TEXT = 'Too many ants to play online';
    function playsText(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
    function metaText(m) { return m.original ? 'Shipped with the game' : m.players + ' players · ' + m.minutes + ' min · ' + m.width + '×' + m.height; }
    // The Map Info line of a map of the players
    function infoText(m) { return (m.description ? m.description + ' ' : '') + '(' + m.minutes + ' min)'; }
    // The chips of the map card and the facts of the preview: the kind, then the players, minutes and size that the list knows (no entry: only the kind)
    function factsOf(m, community) {
        if (!m) return [community ? "Players' map" : 'Original'];
        var out = [m.original ? 'Original' : m.kind === 'extended' ? 'Extended' : "Players' map"];
        if (m.players) out.push(m.players + (m.players === 1 ? ' player' : ' players'));
        if (m.minutes) out.push(m.minutes + ' min');
        if (m.width && m.height) out.push(m.width + '\u00d7' + m.height);
        return out;
    }

    function recall(storage) {
        try { var v = storage && storage.getItem(VIEW_KEY); return v === 'thumbs' ? 'thumbs' : 'detail'; } catch (e) { return 'detail'; }
    }
    function remember(storage, v) { try { if (storage) storage.setItem(VIEW_KEY, v); } catch (e) { /* the choice is for this visit only */ } }

    // opts: { host (the dialog's panel; its parent is the overlay), doc, fetch, storage, setTimeout, clearTimeout, AbortController, now, matchMedia, canOpen() and canChoose() (the host, START not pressed:
    //         asked when the list opens and when a map is chosen), onChoose(entry), onList(), onOpen(), onClose(), room() (the room's map file: marked In this room), originals (the page's six: { key, name, file, info }) }
    function create(opts) {
        var doc = opts.doc, host = opts.host;
        var later = opts.setTimeout || function (f, ms) { return setTimeout(f, ms); }, unlater = opts.clearTimeout || function (t) { clearTimeout(t); };
        var clock = opts.now || function () { return Date.now(); };
        var list = null, status = 'idle', view = recall(opts.storage), sort = 'default', query = '', picked = '', shown = [], opened = false, byFile = {}, byLower = {};
        var parts = {}, typing = 0, failedAt = 0, attempt = 0, fb = null, fbList = [], sections = 0;

        function el(tag, cls, txt) { var e = doc.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; }
        function button(cls, label) { var b = el('button', cls, label); b.setAttribute('type', 'button'); return b; }
        // a file name as the list has it (the room may say it in another case)
        function canon(file) { return byFile[file] ? file : byLower[String(file || '').toLowerCase()] ? byLower[String(file).toLowerCase()].file : file; }
        function find(file) { return byFile[file] || byLower[String(file || '').toLowerCase()] || null; }
        // the six originals as entries, from the page's own table (no play counts: the page has none)
        function fallback() {
            if (!fb) {
                fb = {};
                (opts.originals || []).forEach(function (o, i) {
                    var e = { file: o.file, name: o.name, original: true, kind: 'classic', order: i, width: 0, height: 0, minutes: 0, players: 0, description: text(o.info, 200), plays: 0, thumb: ownPath('/front/preview_' + o.key + '.png'), heavy: false, tooManyAnts: false, bare: true };
                    fb[o.file] = e; fbList.push(e);
                });
            }
            return fbList;
        }
        function lookup(file) { return byFile[file] || (status !== 'ready' && fb && fb[file]) || null; }

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
            parts.pane = el('div', 'pk-pane'); parts.pane.setAttribute('role', 'group'); parts.pane.setAttribute('aria-label', 'The map picked');       // (a wide screen shows it beside the list; a phone's sheet has none)
            parts.pimg = el('img', 'pk-pic'); parts.pimg.setAttribute('alt', ''); parts.pname = el('p', 'pk-pn'); parts.pmeta = el('p', 'pk-meta'); parts.pdesc = el('p', 'pk-pd');
            [parts.pimg, parts.pname, parts.pmeta, parts.pdesc].forEach(function (n) { parts.pane.appendChild(n); });
            parts.fine = el('p', 'pk-fine', 'Plays count only matches with people in them that lasted at least 30 seconds.');
            var foot = el('div', 'pk-foot');
            parts.count = el('span', 'pk-count'); parts.count.setAttribute('role', 'status'); parts.count.setAttribute('aria-live', 'polite');
            parts.go = button('btn', 'Choose map');
            foot.appendChild(parts.count); foot.appendChild(parts.go);
            [head, bar, sorts, parts.out, parts.pane, parts.fine, foot].forEach(function (n) { host.appendChild(n); });

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
        // Up and Down in the grid: the item of the next row whose left edge is nearest (the two headings' rows do not line up)
        function across(i, d) {
            var n = items(), c = n[i], b = -1, row = -1;
            for (var j = i + d; c && j >= 0 && j < n.length; j += d) {
                var t = n[j].offsetTop;
                if (t === c.offsetTop) continue;
                if (row < 0) row = t; else if (t !== row) break;
                if (b < 0 || Math.abs(n[j].offsetLeft - c.offsetLeft) < Math.abs(n[b].offsetLeft - c.offsetLeft)) b = j;
            }
            return b < 0 ? (c && d > 0 ? n.length - 1 : 0) : b;
        }
        function onListKey(e) {
            var i = pickedIndex(), step = perRow(), k = e.key, grid = view === 'thumbs';
            if (k === 'ArrowDown') i = grid ? across(i, 1) : i + 1; else if (k === 'ArrowUp') i = grid ? across(i, -1) : i - 1;
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
            var here = opts.room ? canon(opts.room() || '') : '';
            if (m.thumb) {
                var img = el('img'); img.setAttribute('src', m.thumb); img.setAttribute('alt', ''); img.setAttribute('width', String(px)); img.setAttribute('height', String(px)); img.setAttribute('loading', 'lazy'); img.setAttribute('decoding', 'async');
                d.appendChild(img);
            } else d.appendChild(el('span', 'pk-nopic'));
            var body = el('div', 'pk-body');
            var nm = el('div', 'pk-nm', m.name);
            function tag(cls, t) { nm.appendChild(doc.createTextNode(' ')); nm.appendChild(el('span', 'pk-badge' + cls, t)); }      // (a space before each, so a screen reader says "Treasure Original")
            if (m.original) tag('', 'Original'); else if (m.kind === 'extended') tag(' pk-ext', 'Extended');
            if (m.tooManyAnts) tag(' pk-heavy', TOO_MANY_TEXT); else if (m.heavy) tag(' pk-heavy', HEAVY_TEXT);
            if (here && m.file === here) { tag(' pk-here', 'In this room'); d.className += ' here'; }
            body.appendChild(nm);
            if (!thumbs) {
                body.appendChild(el('div', 'pk-meta', metaText(m)));
                d.appendChild(body);
                if (!m.bare) { var pl = el('div', 'pk-pl'); pl.appendChild(el('b', '', playsText(m.plays))); pl.appendChild(doc.createTextNode(m.plays === 1 ? 'play' : 'plays')); d.appendChild(pl); }
            } else {
                if (!m.bare) body.appendChild(el('div', 'pk-meta', playsText(m.plays) + (m.plays === 1 ? ' play' : ' plays')));
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
            paintPane();
        }
        // the preview beside the list: the map that is picked (none: empty), its picture, name, facts and description
        function paintPane() {
            var m = pickedIndex() >= 0 ? lookup(picked) : null;
            parts.pane.className = 'pk-pane' + (m ? '' : ' pk-none');
            parts.pname.textContent = m ? m.name : ''; parts.pmeta.textContent = ''; parts.pdesc.textContent = m ? m.description : '';
            if (m && m.thumb) parts.pimg.setAttribute('src', m.thumb); else parts.pimg.removeAttribute('src');
            if (!m) return;
            if (m.original) parts.pname.appendChild(el('span', 'pk-badge', 'Original')); else if (m.kind === 'extended') parts.pname.appendChild(el('span', 'pk-badge pk-ext', 'Extended'));
            var facts = factsOf(m).slice(1); if (m.original) facts.unshift('Shipped with the game'); if (!m.bare) facts.push(playsText(m.plays) + (m.plays === 1 ? ' play' : ' plays'));
            parts.pmeta.textContent = facts.join(' \u00b7 ');
        }
        // the items under their headings (a flat list when `groups` has one group with no title); the headings are the groups' names for a screen reader
        function sectionsOf(groups, role) {
            var frag = doc.createDocumentFragment ? doc.createDocumentFragment() : parts.out, first = pickedIndex() < 0, n = 0;
            groups.forEach(function (g) {
                if (!g.items.length) return;
                var sec = el('div', 'pk-sec ' + (view === 'thumbs' ? 'pk-grid' : 'pk-list'));
                if (g.title) {
                    var h = el('h3', 'pk-h', g.title); h.id = 'pk-h-' + (++sections);
                    if (g.note) { h.appendChild(doc.createTextNode(' ')); h.appendChild(el('span', '', g.note)); }
                    sec.setAttribute('role', 'group'); sec.setAttribute('aria-labelledby', h.id); sec.appendChild(h);
                } else if (role) { sec.setAttribute('role', 'listbox'); sec.setAttribute('aria-label', role); }
                g.items.forEach(function (m) { var node = item(m); if (first && n === 0) node.setAttribute('tabindex', '0'); n++; sec.appendChild(node); });
                frag.appendChild(sec);
            });
            if (frag !== parts.out) parts.out.appendChild(frag);
        }

        // afterwards: 'top' a search, 'picked' a sort or a view (the chosen row is brought into view), '' nothing is moved
        function paint(where) {
            var was = parts.out.scrollTop || 0;
            parts.vD.setAttribute('aria-pressed', String(view === 'detail')); parts.vT.setAttribute('aria-pressed', String(view === 'thumbs'));
            SORTS.forEach(function (s) { parts.sorts[s.id].setAttribute('aria-pressed', String(sort === s.id)); });
            parts.out.textContent = '';
            parts.out.removeAttribute('role'); parts.out.removeAttribute('aria-label');
            parts.out.className = 'pk-out';
            parts.go.disabled = true;
            if (status !== 'ready') {      // (no list yet, or none to be had: the six originals are there at once, under the note)
                var p = el('div', status === 'error' ? 'pk-note pk-err' : 'pk-note');
                if (status === 'error') {
                    p.appendChild(el('p', '', 'The full list of maps is not available right now. The six original maps can still be chosen here.'));
                    var r = button('btn sm', 'Try again'); r.setAttribute('data-retry', '1'); p.appendChild(r);
                } else p.appendChild(el('p', '', 'Loading the maps\u2026'));
                parts.out.appendChild(p); parts.count.textContent = '';
                shown = fallback().slice(); if (status === 'error' && !fb[picked]) picked = '';
                sectionsOf([{ title: '', items: shown }], 'The six originals');
                parts.go.disabled = pickedIndex() < 0; paintPane(); return;
            }
            parts.q.setAttribute('placeholder', 'Search ' + list.length + ' maps');
            shown = sortMaps(filterMaps(list, query), sort);
            parts.count.textContent = shown.length + ' of ' + list.length + ' maps shown';
            if (!shown.length) { var e = el('div', 'pk-note pk-empty'); e.appendChild(el('b', '', 'No map is called "' + query.trim() + '".')); e.appendChild(el('span', '', 'Try fewer letters.')); parts.out.appendChild(e); paintPane(); return; }
            if (!byFile[picked]) picked = '';
            parts.out.setAttribute('role', 'listbox'); parts.out.setAttribute('aria-label', 'Maps');
            if (sort === 'default') {      // (the originals come first in this order: two headings, the second says how many players' maps there are)
                var own = shown.filter(function (m) { return m.original; }), rest = shown.filter(function (m) { return !m.original; });
                sectionsOf([{ title: 'The six originals', items: own }, { title: "Players' maps", note: rest.length + (rest.length === 1 ? ' map, most played first' : ' maps, most played first'), items: rest }]);
            } else sectionsOf([{ title: '', items: shown }]);
            parts.go.disabled = pickedIndex() < 0;   // (a pick that a search hides is not chosen)
            paintPane();
            if (where === 'top') parts.out.scrollTop = 0;
            else if (where === 'picked') {
                var cur = parts.out.querySelector ? parts.out.querySelector('[data-file].sel') : null;
                if (cur && cur.scrollIntoView) cur.scrollIntoView({ block: 'center' }); else parts.out.scrollTop = was;
            }
        }

        // The list: asked for once for the visit; a try ends in 12 s at the latest (Try again); `again` is a person's retry, a soft ask waits 15 s after a failure
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
            var m = lookup(picked);
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

    return { VIEW_KEY: VIEW_KEY, SORTS: SORTS, validMapFile: validMapFile, previewPath: previewPath, thumbFor: thumbFor, percentEncode: percentEncode, displayName: displayName, parseList: parseList, sortMaps: sortMaps, filterMaps: filterMaps, playsText: playsText, metaText: metaText, infoText: infoText, factsOf: factsOf, HEAVY_TEXT: HEAVY_TEXT, TOO_MANY_TEXT: TOO_MANY_TEXT, create: create };
});

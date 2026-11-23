/* The search and the filters of the map index (/map-library; docs/MAP_PAGES.md). The page is complete without this file: its list is one page of the library and its pager is plain links
   (what a search engine and a visitor without script get). With the list that the page loads before this file (window.ANTS_MAP_LIBRARY, made by tools/map_pages.py) this script shows the
   search and the filters, and while one is in use it draws the matching maps itself, 48 to a page; with all of them cleared it puts the page back as it was. Everything that came with the list
   (a map's name, its line) is put in as text, never as markup. "Most played" asks the site's list of maps (GET /maps) once, when it is first chosen. Plain ES5, no globals. */
(function () {
    'use strict';
    var d = document, data = window.ANTS_MAP_LIBRARY;
    var tools = d.getElementById('tools'), list = d.getElementById('list'), count = d.getElementById('count'), pager = d.getElementById('pager');
    if (!data || !data.maps || !tools || !list || !count || !pager) return;
    function table(list) { var out = {}; (list || []).forEach(function (e) { out[e[0]] = e[1] === null ? Infinity : e[1]; }); return out; }
    var SIZES = table(data.sizes), LENGTHS = table(data.lengths);                // (the tops of the sizes and of the game lengths come with the list: tools/map_pages.py is their one home)
    var maps = data.maps.map(function (r, i) {
        return { slug: r[0], name: r[1], say: r[2], width: r[3], height: r[4], minutes: r[5], pic: r[6], original: r[7] === 1, file: r[8], players: r[9], at: i, text: (r[1] + ' ' + r[2]).toLowerCase(), key: r[1].toLowerCase().replace(/^[^a-z0-9]+/, '') };
    });
    var PAGE = data.page || 48, still = { list: list.innerHTML, count: count.textContent, pager: pager.innerHTML };
    var state = { q: '', kind: '', size: '', length: '', players: '', order: '', page: 1 }, plays = null, asked = false;
    var search = d.getElementById('q'), kinds = tools.querySelectorAll('[data-kind]');
    var pickers = { size: d.getElementById('f-size'), length: d.getElementById('f-length'), players: d.getElementById('f-players'), order: d.getElementById('f-order') };

    function make(tag, cls, text) { var n = d.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
    function longest(m) { return Math.max(m.width, m.height); }
    function within(value, table, key) { var keys = Object.keys(table), at = keys.indexOf(key); return at < 0 || (value <= table[key] && (at === 0 || value > table[keys[at - 1]])); }
    function plain() { return !state.q && !state.kind && !state.size && !state.length && !state.players && !state.order; }
    function wanted(m) {
        var words = state.q.toLowerCase().split(/\s+/).filter(Boolean), i;
        for (i = 0; i < words.length; i++) if (m.text.indexOf(words[i]) < 0) return false;
        if (state.kind === 'original' && !m.original) return false;
        if (state.kind === 'player' && m.original) return false;
        if (state.players && String(m.players) !== state.players) return false;
        return within(longest(m), SIZES, state.size) && within(m.minutes, LENGTHS, state.length);
    }
    function played(m) { return plays && plays[m.file.toLowerCase()] || 0; }
    function ordered(found) {
        var by = { '': function (a, b) { return a.at - b.at; }, name: function (a, b) { return a.key < b.key ? -1 : a.key > b.key ? 1 : a.at - b.at; },
                   plays: function (a, b) { return played(b) - played(a) || a.at - b.at; } };
        return found.slice().sort(by[state.order] || by['']);
    }
    function card(m) {
        var li = make('li', 'm'), fig = make('figure', 'tv'), img = make('img'), info = make('div', 'info'), h3 = make('h3'), a = make('a', '', m.name), when = '', n = played(m);
        img.src = m.pic; img.alt = ''; img.width = 96; img.height = 96; img.loading = 'lazy';
        fig.appendChild(img);
        a.href = '/map/' + m.slug;
        h3.appendChild(a);
        if (m.original) h3.appendChild(make('span', 'mtag o', 'Original'));
        info.appendChild(h3);
        if (m.say) info.appendChild(make('p', 'say', m.say));
        when = m.width + ' x ' + m.height + ' · ' + m.players + ' players · ' + m.minutes + ' min';
        info.appendChild(make('span', 'when', when + (n ? ' · ' + n + (n === 1 ? ' play' : ' plays') : '')));
        li.appendChild(fig);
        li.appendChild(info);
        return li;
    }
    function numbers(page, pages) {                       // (the same pager as the page's own: tools/map_pages.py pager)
        var keep = {}, out = [], last = 0, n;
        [1, pages, page - 1, page, page + 1].forEach(function (k) { keep[k] = true; });
        if (page === 1) { keep[2] = keep[3] = true; }
        if (page === pages) { keep[pages - 1] = keep[pages - 2] = true; }
        for (n = 1; n <= pages; n++) if (keep[n]) { if (n - last > 1) out.push(0); out.push(n); last = n; }
        return out;
    }
    function button(text, cls, to, current) {
        var b = make('button', cls, text);
        b.type = 'button';
        if (current) b.setAttribute('aria-current', 'page');
        if (to) b.addEventListener('click', function () { state.page = to; draw(true); });
        else b.disabled = true;
        return b;
    }
    function drawPager(page, pages) {
        pager.textContent = '';
        if (pages < 2) return;
        pager.appendChild(button('‹ Back', 'pg wide' + (page > 1 ? '' : ' off'), page > 1 ? page - 1 : 0));
        numbers(page, pages).forEach(function (n) { pager.appendChild(n ? button(String(n), 'pg num', n === page ? 0 : n, n === page) : make('span', 'gap', '…')); });
        pager.appendChild(make('span', 'of', 'of ' + pages));
        pager.appendChild(button('Next ›', 'pg wide' + (page < pages ? '' : ' off'), page < pages ? page + 1 : 0));
    }
    function draw(scroll) {
        if (plain()) {
            list.innerHTML = still.list; count.textContent = still.count; pager.innerHTML = still.pager;
            return;
        }
        var found = ordered(maps.filter(wanted)), pages = Math.max(1, Math.ceil(found.length / PAGE)), page = Math.min(state.page, pages), first = (page - 1) * PAGE;
        list.textContent = '';
        found.slice(first, first + PAGE).forEach(function (m) { list.appendChild(card(m)); });
        if (!found.length) list.appendChild(make('li', 'empty', 'No map has that in its name or its words.'));
        count.textContent = found.length + (found.length === 1 ? ' map' : ' maps') + (found.length > PAGE ? ' · ' + (first + 1) + ' to ' + Math.min(first + PAGE, found.length) : '');
        drawPager(page, pages);
        if (scroll && d.getElementById('list-h') && d.getElementById('list-h').scrollIntoView) d.getElementById('list-h').scrollIntoView();
    }
    function askPlays() {
        if (asked || typeof fetch !== 'function') return;
        asked = true;
        fetch('/maps', { credentials: 'omit' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (answer) {
            if (!answer || !Array.isArray(answer.maps)) return;
            plays = {};
            answer.maps.forEach(function (m) { if (m && typeof m.file === 'string' && typeof m.plays === 'number') plays[m.file.toLowerCase()] = m.plays; });
            draw(false);
        }).catch(function () { /* the order stays as it was */ });
    }
    function change(key, value) { state[key] = value; state.page = 1; if (key === 'order' && value === 'plays') askPlays(); draw(false); }

    if (search) search.addEventListener('input', function () { change('q', search.value); });
    [].forEach.call(kinds, function (b) {
        b.addEventListener('click', function () {
            [].forEach.call(kinds, function (o) { o.setAttribute('aria-pressed', o === b ? 'true' : 'false'); });
            change('kind', b.getAttribute('data-kind'));
        });
    });
    Object.keys(pickers).forEach(function (key) {
        if (pickers[key]) pickers[key].addEventListener('change', function () { pickers[key].parentNode.classList.toggle('on', !!pickers[key].value && key !== 'order'); change(key, pickers[key].value); });
    });
    var more = d.getElementById('filters');
    if (more) more.addEventListener('click', function () { tools.classList.add('open'); });
    tools.hidden = false;
}());

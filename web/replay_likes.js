/* The plays and the likes of a replay on the pages of the match history (docs/REPLAYS.md "The match pages"; the door, the numbers and the privacy note are in docs/REPLAY_COUNTS.md): the count of plays and the heart with
   its count on a card of the list (web/matches.html) and on the match page (web/match.html). The counts come from the record's `plays` and `likes` (a whole number from 0 up; anything else is 0).
   * The heart: a button, lit when this visitor likes the replay. One look at the door tells who is logged in and which of the shown replays they like (GET /counts/mine?ids=..., once for the cards of a page, once
     on the match page; the door says 503 when likes are off, and the hearts are plain counts). A tap changes the heart at once and asks the door (POST /counts/like/<id> or /counts/unlike/<id>, with the token that the look
     gave in the header X-Ants-Csrf); the answer's `likes` is the count shown. When the call fails the heart goes back and one plain line says why. A token that the door refuses (403) is asked for once more and the
     call is made once more. One call at a time for a replay. Nobody sees who liked: the page only holds its own answers.
   * Not logged in: a tap shows a small card, "Log in to like a replay." with the way in, and asks the door for nothing.
   * A replay whose recording is gone or was made by an older version keeps its numbers; its heart is a plain count (a span, not a button).
   * Nothing a server says is put in as markup: counts go through numberText, the words are this file's own.
   The two pages call it with `create`; `card` and `hero` put the parts into the card and the result of the page. ES5, no module system: a page gets AntsLikes, node gets module.exports
   (tests/scripts/web_likes_check.js runs it on a strict fake of the DOM). */
(function (root, factory) {
    'use strict';
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsLikes = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var NS = 'http://www.w3.org/2000/svg';
    var CSRF = /^[0-9a-f]{32}$/;                     // the token of the door: 32 lower-case hex characters
    var MAX_IDS = 50;                                // the door's limit for one look
    var NOTE_MS = 4000;                              // how long the plain line of a failed call stays
    var CARD_W = 232;                                // the width of the small card (matches.css .lkask)
    var HEART = 'M10 16.6 C4.5 12.4 1.5 9.4 1.5 6 C1.5 3.4 3.5 1.5 5.9 1.5 C7.6 1.5 9.1 2.4 10 3.9 C10.9 2.4 12.4 1.5 14.1 1.5 C16.5 1.5 18.5 3.4 18.5 6 C18.5 9.4 15.5 12.4 10 16.6 Z';
    var TRIANGLE = 'M0 0 L9 5 L0 10 Z';
    var SAY = {
        login: 'Log in to like a replay.', small: 'Only the count shows. Nobody sees who liked.', way: 'Log in with Discord',
        slow: 'Slow down a little.', limit: 'You have liked as many replays as can be kept.', off: "Liking isn't available right now.", net: "Couldn't reach the server.",
        gone: "This replay can't be liked any more.", other: 'That did not work. Try again in a moment.'
    };

    // A count of the record: a whole number from 0 up to a billion; anything else is 0
    function countOf(n) { return typeof n === 'number' && isFinite(n) && n >= 0 && n <= 1e9 && n === Math.floor(n) ? n : 0; }
    // The plain line for a call that failed, from its status (0: nothing came)
    function lineOf(status) {
        return status === 429 ? SAY.slow : status === 409 ? SAY.limit : status === 503 ? SAY.off : status === 404 ? SAY.gone : status === 0 ? SAY.net : SAY.other;
    }

    // create({ document, win, fetch, numberText, login }): the one object of a page. `login(id)` is the address that "Log in with Discord" goes to; `win` is the window (the timer and the place of the small card).
    function create(env) {
        var doc = env.document, win = env.win, fetcher = env.fetch, numberText = env.numberText, loginHref = env.login;
        var state = { known: false, off: false, signed: false, liked: Object.create(null), csrf: '' };
        var rows = [], waiting = null, popup = null, popTimer = 0, stamp = 0, touched = Object.create(null);

        function el(tag, className, text) { var n = doc.createElement(tag); if (className) n.className = className; if (text !== undefined) n.textContent = text; return n; }
        function icon(box, d) {
            var s = doc.createElementNS(NS, 'svg'), p = doc.createElementNS(NS, 'path');
            s.setAttribute('viewBox', box); s.setAttribute('aria-hidden', 'true'); p.setAttribute('d', d); s.appendChild(p);
            return s;
        }
        function plural(n, one, many) { return numberText(n) + ' ' + (n === 1 ? one : many); }

        // ---- what the page shows ----
        function playsNode(r, words) {
            var n = r.plays, s = el('span', 'pv' + (n ? '' : ' zero') + (r.key === 'plays' && !words ? ' key' : ''));
            s.appendChild(icon('0 0 9 10', TRIANGLE));
            s.appendChild(doc.createTextNode(words ? plural(n, 'play', 'plays') : numberText(n)));
            s.title = plural(n, 'play', 'plays') + ' of the replay';
            return s;
        }
        function paint(h, r) {
            var n = countOf(r.likes), on = r.liked && !h.plain;
            h.node.className = 'lk' + (on ? ' on' : '') + (n ? '' : ' zero') + (h.plain ? ' flat' : '') + (r.key === 'likes' ? ' key' : '');
            h.num.textContent = numberText(n);
            h.node.title = h.plain ? plural(n, 'like', 'likes') : on ? 'You like this replay' : 'Like this replay';
            if (!h.plain) { h.node.setAttribute('aria-pressed', String(on)); h.node.setAttribute('aria-label', 'Like this replay: ' + plural(n, 'like', 'likes')); }
        }
        function build(h, r) {
            h.node = el(h.plain ? 'span' : 'button');
            if (h.id) h.node.id = h.id;
            if (!h.plain) { h.node.type = 'button'; h.node.addEventListener('click', function () { tap(r, h); }); }
            h.node.appendChild(icon('0 0 20 18', HEART));
            h.num = el('span', 'n'); h.node.appendChild(h.num);
            paint(h, r);
            return h.node;
        }
        function heartOf(r, id) { var h = { plain: r.plain, id: id }; r.hearts.push(h); return build(h, r); }
        function show(r) { r.hearts.forEach(function (h) { paint(h, r); }); }
        // What the look of the door says is shown on every heart that is not at work; a heart becomes a plain count when likes are off
        function sync() {
            rows.forEach(function (r) {
                if (!r.busy) r.liked = state.signed && state.liked[r.id] === true;
                r.hearts.forEach(function (h) {
                    if (state.off && !h.plain && h.node.parentNode) { var old = h.node; h.plain = true; h.node.parentNode.replaceChild(build(h, r), old); }
                    paint(h, r);
                });
            });
        }

        // ---- the small card under a heart: the way in, or one plain line ----
        function close() {
            if (popup && popup.parentNode) popup.parentNode.removeChild(popup);
            popup = null;
            if (popTimer) { win.clearTimeout(popTimer); popTimer = 0; }
        }
        function open(box, anchor) {
            close();
            doc.body.appendChild(box);
            popup = box;
            var q = anchor.getBoundingClientRect(), wide = win.innerWidth || 0, left = Math.max(8, Math.min(wide - CARD_W - 12, q.left + q.width / 2 - 40));
            box.style.left = (left + (win.scrollX || 0)) + 'px'; box.style.top = (q.bottom + (win.scrollY || 0) + 12) + 'px';
            box.style.setProperty('--tip', Math.round(q.left + q.width / 2 - left - 8) + 'px');
        }
        function wayIn(h, r) {
            var box = el('div', 'lkask'), p = el('p', '', SAY.login), a = el('a', 'btn sm', SAY.way);
            box.setAttribute('role', 'dialog'); box.setAttribute('aria-label', SAY.login);
            p.appendChild(el('small', '', SAY.small)); box.appendChild(p);
            a.href = loginHref(r.id); box.appendChild(a);
            open(box, h.node);
            try { a.focus({ preventScroll: true }); } catch (e) { /* the link is there to press */ }
        }
        function say(h, text) {
            var box = el('div', 'lkask line');
            box.setAttribute('role', 'alert'); box.appendChild(el('p', '', text));
            open(box, h.node);
            popTimer = win.setTimeout(close, NOTE_MS);
        }
        function inside(node) { for (var n = node; n; n = n.parentNode) if (n === popup) return true; return false; }
        doc.addEventListener('pointerdown', function (ev) { if (popup && !inside(ev.target)) close(); });
        doc.addEventListener('keydown', function (ev) { if (popup && ev.key === 'Escape') close(); });

        // ---- the door ----
        function json(response) { return response.json().then(function (body) { return { status: response.status, body: body }; }, function () { return { status: response.status, body: null }; }); }
        // The look at who is logged in and which of `ids` they like. Resolves to true when the door answered in a way that the page understands (also "nobody" and "likes are off"), else to false; never rejects.
        function ask(ids) {
            var from = ++stamp;
            return new Promise(function (resolve) {
                var request;
                try { request = fetcher('/counts/mine?ids=' + ids.map(encodeURIComponent).join(','), { credentials: 'same-origin', cache: 'no-store' }); } catch (e) { resolve(false); return; }
                request.then(json).then(function (a) {
                    var ok = false;
                    try { ok = take(a.status, a.body, ids, from); } catch (e) { ok = false; }
                    resolve(ok);
                }, function () { resolve(false); });
            });
        }
        function take(status, a, ids, from) {
            if (status === 503 && a && (a.error === 'likes_off' || a.error === 'counts_off')) { state.known = true; state.off = true; state.signed = false; state.liked = Object.create(null); sync(); return true; }      // (only the two words that say that the likes are off: a 503 that says busy is a moment, and the hearts stay as they are)
            if (status !== 200 || !a || typeof a.signed_in !== 'boolean') return false;
            if (a.signed_in && (typeof a.csrf !== 'string' || !CSRF.test(a.csrf) || !Array.isArray(a.liked))) return false;
            state.off = false; state.known = true; state.signed = a.signed_in;
            if (!a.signed_in) { state.liked = Object.create(null); state.csrf = ''; } else {
                state.csrf = a.csrf;
                ids.forEach(function (id) { if (!(touched[id] > from)) state.liked[id] = a.liked.indexOf(id) !== -1; });      // (a heart that was tapped while the look was out is not changed by it)
            }
            sync();
            return true;
        }
        function call(what, id) {
            return new Promise(function (resolve) {
                var request;
                try { request = fetcher('/counts/' + what + '/' + id, { method: 'POST', credentials: 'same-origin', headers: { 'X-Ants-Csrf': state.csrf } }); } catch (e) { resolve({ status: 0, body: null }); return; }
                request.then(json).then(resolve, function () { resolve({ status: 0, body: null }); });
            });
        }

        // ---- a tap ----
        function tap(r, h) {
            if (r.busy) return;
            r.busy = true;
            function go() {
                if (!state.known) { r.busy = false; say(h, SAY.net); return; }
                if (state.off) { r.busy = false; sync(); say(h, SAY.off); return; }
                if (!state.signed) { r.busy = false; wayIn(h, r); return; }
                var was = { liked: r.liked, likes: r.likes }, want = !r.liked;
                r.liked = want; r.likes = Math.max(0, r.likes + (want ? 1 : -1)); show(r);                  // (at once: the answer comes later)
                attempt(r, h, want, was, false);
            }
            if (state.known && !waiting) go(); else (waiting || ask([r.id])).then(go);          // (a page that has not heard from the door yet, or whose look is still out, waits for it)
        }
        function back(r, was) { r.liked = was.liked; r.likes = was.likes; r.busy = false; show(r); }
        function attempt(r, h, want, was, again) {
            call(want ? 'like' : 'unlike', r.id).then(function (a) {
                var b = a.body && typeof a.body === 'object' ? a.body : {};
                if (a.status === 200) {
                    r.liked = typeof b.liked === 'boolean' ? b.liked : want;
                    if (typeof b.likes === 'number') r.likes = countOf(b.likes);
                    state.liked[r.id] = r.liked; touched[r.id] = ++stamp; r.busy = false; show(r);
                    return;
                }
                if (a.status === 403 && !again) {                                  // (the token is out of date: one more look, one more call)
                    ask([r.id]).then(function (ok) {
                        if (!ok) { back(r, was); say(h, SAY.net); } else if (state.off) { back(r, was); sync(); say(h, SAY.off); } else if (!state.signed) { back(r, was); wayIn(h, r); } else attempt(r, h, want, was, true);
                    });
                    return;
                }
                back(r, was);
                if (a.status === 401) { state.signed = false; state.liked = Object.create(null); state.csrf = ''; sync(); wayIn(h, r); } else say(h, lineOf(a.status));
            });
        }

        // ---- what the pages call ----
        function rowOf(rec, o) {
            var r = { id: rec.id, plays: countOf(rec.plays), likes: countOf(rec.likes), liked: false, plain: !!(o && o.plain), key: o && o.key || '', hearts: [], busy: false };
            rows.push(r);
            return r;
        }
        function group(r) { var g = el('span', 'pl'); g.appendChild(playsNode(r, false)); g.appendChild(heartOf(r)); return g; }
        return {
            // a new page of the list: the rows of the old one are forgotten
            begin: function () { rows = []; close(); },
            // the plays and the heart of one card of the list (two groups: the card of a computer shows one, a phone's the other). o = { plain: true for a recording that is not to be liked, key: 'plays' | 'likes' for the sort }
            card: function (li, rec, o) {
                var r = rowOf(rec, o), act = li.querySelector('.act'), info = li.querySelector('.info');
                if (act) act.insertBefore(group(r), act.firstChild);
                if (info) info.appendChild(group(r));
                return r;
            },
            // the plays (at the end of the line under the title) and the heart (beside Show replay) of the match page's result
            hero: function (section, rec, o) {
                var r = rowOf(rec, o), meta = section.querySelector('.meta'), acts = section.querySelector('.acts'), shown = section.querySelector('#show'), node = heartOf(r, 'like');
                if (meta) { meta.appendChild(el('span', 'pvdot', ' · ')); meta.appendChild(playsNode(r, true)); }
                if (acts) acts.insertBefore(node, shown ? shown.nextSibling : acts.firstChild);
                return r;
            },
            // one look at the door for the rows that can be liked (after the rows are put on the page)
            look: function () {
                var ids = [];
                rows.forEach(function (r) { if (!r.plain && ids.indexOf(r.id) === -1) ids.push(r.id); });
                if (!ids.length) return Promise.resolve(false);
                var mine = waiting = ask(ids.slice(0, MAX_IDS));
                mine.then(function () { if (waiting === mine) waiting = null; });
                return mine;
            },
            state: state
        };
    }

    return { create: create, countOf: countOf, lineOf: lineOf, SAY: SAY, MAX_IDS: MAX_IDS };
});

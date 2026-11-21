// The room's chat in the chat box of Home (web/home/roomchat.js, docs/HOME_CHAT.md "The Room tab"): the box has two tabs while the page holds a room, the front page's own chat ("Ants & Uncles") and the room's ("Room":
// only the people in the room can read it). The room's chat is the page's (web/front/lobby_chat.js); this part only gives it a place in the dock, one tap from the other, and a dot on the tab that has news.
//   * Loaded once, after the dock, by web/home/home.js (a page whose part does not come keeps the room's chat inside the room's panel, under the notice). Text and properties only, never markup. ES5.
//   * No tab until the page holds a room (the room's chat is shown by lobby_chat.js); when the room goes the box is the front page's again.
//   * A line of the room that comes while its tab is not the one that shows (the other tab is open, the box is folded, a phone is on another view) is a dot on the Room tab; a line of the front page's chat that comes while the room's tab shows is a dot on
//     the other. Opening the tab clears its dot.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeRoomChat = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var TEXT = { global: 'Ants & Uncles', room: 'Room', note: 'Only the people in this room can read it.', tabs: 'Chats', news: 'new messages' };
    var NAMES = ['global', 'room'];

    // mount({ doc, win, dock (the chat dock of chat.js: root, away(on)), chat (the page's room chat section, hidden until there is a room), news (optional: fn(count), the room's unread lines, for the phone's strip) })
    // returns { sync, seen, news, select, tab }
    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, chat = env.chat, box = dock && dock.root;
        if (!doc || !dock || !box || !chat || !box.querySelector) return null;
        var head = box.querySelector('.gc-head'), globalBody = box.querySelector('.gc-body'), here = box.querySelector('.gc-online'), pill = box.querySelector('.gc-unread');
        if (!head || !globalBody) return null;
        var current = 'global', unread = 0, exists = false, tabs = {}, dots = {};

        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        var strip = el('div', 'rc-tabs'), note = el('span', 'rc-note', TEXT.note), body = el('div', 'rc-body');
        strip.setAttribute('role', 'tablist'); strip.setAttribute('aria-label', TEXT.tabs); strip.hidden = true; note.hidden = true; body.hidden = true;
        NAMES.forEach(function (name) {
            var b = el('button', 'btn rc-tab', name === 'global' ? TEXT.global : TEXT.room), dot = el('span', 'rc-dot');
            b.type = 'button'; b.setAttribute('role', 'tab'); b.setAttribute('data-rc', name); b.setAttribute('aria-selected', name === current ? 'true' : 'false');
            b.setAttribute('aria-controls', name === 'global' ? globalBody.id || 'gchat-body' : 'chat');
            dot.hidden = true; dot.setAttribute('aria-hidden', 'true'); b.appendChild(dot);
            b.addEventListener('click', function () { select(name, true); });
            b.addEventListener('keydown', function (e) {
                var k = e && e.key;
                if (k !== 'ArrowLeft' && k !== 'ArrowRight' && k !== 'Home' && k !== 'End') return;
                if (e.preventDefault) e.preventDefault();
                select(k === 'ArrowLeft' || k === 'Home' ? 'global' : 'room', true);
            });
            strip.appendChild(b); tabs[name] = b; dots[name] = dot;
        });
        head.insertBefore(strip, head.firstChild);
        if (here && here.parentNode === head) head.insertBefore(note, here); else head.appendChild(note);
        box.appendChild(body);
        // the dock is the place of the room's chat from now on (the section keeps its own hidden: lobby_chat.js shows it while there is a room); a move is a move, nothing is copied
        body.appendChild(chat);

        function open() { return box.getAttribute('data-open') === 'true'; }
        function seen() { return exists && current === 'room' && open() && !doc.hidden; }
        function paint() {
            box.setAttribute('data-rc', exists ? current : 'none');
            strip.hidden = !exists; note.hidden = !(exists && current === 'room'); body.hidden = !(exists && current === 'room');
            NAMES.forEach(function (name) { tabs[name].setAttribute('aria-selected', name === current ? 'true' : 'false'); tabs[name].setAttribute('tabindex', name === current ? '0' : '-1'); });
            try { dock.away(exists && current === 'room'); } catch (e) { /* the dock is its own */ }
            dots.room.hidden = !(exists && unread > 0 && !seen());
            dots.global.hidden = !(exists && current === 'room' && pill && !pill.hidden);
            if (dots.room.hidden === false) tabs.room.setAttribute('aria-label', TEXT.room + ', ' + TEXT.news); else tabs.room.removeAttribute('aria-label');
            if (dots.global.hidden === false) tabs.global.setAttribute('aria-label', TEXT.global + ', ' + TEXT.news); else tabs.global.removeAttribute('aria-label');
            if (env.news) { try { env.news(exists && !seen() ? unread : 0); } catch (e) { /* the strip has no count */ } }
        }
        function select(name, focus) {
            if (name !== 'global' && name !== 'room') return;
            if (name === 'room' && !exists) return;
            current = name;
            if (seen()) unread = 0;
            paint();
            if (focus && tabs[name].focus) tabs[name].focus();
        }
        // The page says whether it holds a room (the room's chat is shown): called with every render of the page
        function sync() {
            var now = !chat.hidden;
            if (now === exists) { if (exists && seen() && unread) { unread = 0; paint(); } return; }
            exists = now;
            if (!exists) { current = 'global'; unread = 0; }
            paint();
        }
        // A line of the room that came while it was not in view: a dot. Returns true (the page does not show its own pop-up for it)
        function news() {
            if (!exists) return false;
            if (seen()) return true;
            unread++;
            paint();
            return true;
        }
        // the box was opened or folded, or the global chat's own count moved: the dots follow
        try {
            if (win.MutationObserver) {
                var watcher = new win.MutationObserver(function () { sync(); paint(); });
                watcher.observe(box, { attributes: true, attributeFilter: ['data-open'] });
                if (pill) watcher.observe(pill, { attributes: true, attributeFilter: ['hidden'] });
            }
        } catch (e) { /* the dots follow the page's own renders */ }
        sync();
        return { sync: sync, seen: seen, news: news, select: function (name) { select(name, false); }, tab: function () { return current; }, exists: function () { return exists; } };
    }

    return { TEXT: TEXT, mount: mount };
});

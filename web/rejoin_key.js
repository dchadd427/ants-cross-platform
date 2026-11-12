// The keys of the seats that this browser holds, read in one place: the front page's Rejoin strip and the game page's name step both ask it (web/lobby.html, web/shell.html).
// ES5, no module system: a page gets the global AntsRejoinKey, node gets module.exports (tests/scripts/web_rejoin_block_check.js runs the block below, as it stands).
// { PREFIX, MAX_AGE_MS, FUTURE_MS, parse(name, text), offer(storage, now, server, room, seat) }. THE KEY IS A SECRET: nothing here returns it, writes it, logs it or puts it in an address.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsRejoinKey = api;
})(typeof self !== 'undefined' ? self : this, function () {
    // REJOINKEY_BEGIN (tests/scripts/web_rejoin_block_check.js and web_lobby_entry_check.js run this block; the two pages load this file)
    // THE KEYS OF THE SEATS that this browser holds. The game page keeps the key of every seat that it holds in a game server's room in the browser's local storage, ants.rejoin.<room>.<seat> =
    // {"k": 32 hex digits, "s": the server's address, "t": epoch ms} (src/ants_app/rejoin_store.cpp), lets go of it when the match ends, the seat is dropped or the player leaves, and removes an
    // entry that three hours have passed over when it reads it. This block only LOOKS (the front page's Rejoin button and the game page's name step ask it): THE KEY IS A SECRET, it is read to check an
    // entry and is in nothing that comes out of here; the game finds it in the storage by itself.
    var REJOIN_PREFIX = 'ants.rejoin.';
    var REJOIN_MAX_AGE_MS = 3 * 60 * 60 * 1000;          // the game's own rule: an entry that is older is of no use (a match of the site's server is over well within three hours)
    var REJOIN_FUTURE_MS = 60 * 1000;                    // a time further ahead than this is a clock that was wrong, not a match to go back to
    // One entry of the storage: { room, seat, server, t } when `name` and `text` are exactly what the game page writes (a room code by the page's own rule, a seat 0 - 3, a JSON object with these three
    // members and no others, a key of 32 hex digits that is not zero, a printable server of 1 - 255 characters, a time that is a whole number of milliseconds), else null. The key is not returned.
    function rejoinParse(name, text) {
        var m = /^ants\.rejoin\.([A-Za-z0-9_-]{1,32})\.([0-3])$/.exec(typeof name === 'string' ? name : '');
        if (!m || typeof text !== 'string') return null;
        var v;
        try { v = JSON.parse(text); } catch (e) { return null; }
        if (!v || typeof v !== 'object' || Array.isArray(v)) return null;
        var members = Object.keys(v).sort().join(',');
        if (members !== 'k,s,t') return null;
        if (typeof v.k !== 'string' || !/^[0-9A-Fa-f]{32}$/.test(v.k) || /^0+$/.test(v.k)) return null;
        if (typeof v.s !== 'string' || !/^[\x20-\x7e]{1,255}$/.test(v.s)) return null;
        if (typeof v.t !== 'number' || !isFinite(v.t) || Math.floor(v.t) !== v.t || v.t < 0 || v.t > 999999999999999) return null;
        return { room: m[1], seat: Number(m[2]), server: v.s, t: v.t };
    }
    // The seat to offer, or null: the newest (by t; on a tie the lower seat, then the room) entry of `server` that is not older than three hours and not more than a minute ahead of `now` (ms), of the
    // room `room` when one is given ('' or nothing: any) and of the seat `seat` when it is a number from 0 (nothing: any). `storage` is the browser's (length, key(i), getItem, removeItem; any call
    // may throw). An entry that three hours have passed over is removed, as the game does when it reads (whatever its server: a key that old is of no use to anybody); an entry of another server or room,
    // one from the future and anything that is no entry of the game are left as they are. { room, seat, t }: no key.
    function rejoinOffer(storage, now, server, room, seat) {
        var names = [];
        try {
            for (var i = 0; i < storage.length; i++) {
                var name = storage.key(i);
                if (typeof name === 'string' && name.indexOf(REJOIN_PREFIX) === 0) names.push(name);
            }
        } catch (e) { return null; }
        var best = null;
        names.forEach(function (name) {
            var text = null;
            try { text = storage.getItem(name); } catch (e) { return; }
            var entry = rejoinParse(name, text);
            if (!entry) return;
            if (now - entry.t > REJOIN_MAX_AGE_MS) { try { storage.removeItem(name); } catch (e) { /* it stays, and is not offered */ } return; }
            if (entry.t - now > REJOIN_FUTURE_MS || entry.server !== server) return;
            if ((room && entry.room !== room) || (typeof seat === 'number' && seat >= 0 && entry.seat !== seat)) return;
            if (!best || entry.t > best.t || (entry.t === best.t && (entry.seat < best.seat || (entry.seat === best.seat && entry.room < best.room)))) best = entry;
        });
        return best ? { room: best.room, seat: best.seat, t: best.t } : null;
    }
    // REJOINKEY_END
    return { PREFIX: REJOIN_PREFIX, MAX_AGE_MS: REJOIN_MAX_AGE_MS, FUTURE_MS: REJOIN_FUTURE_MS, parse: rejoinParse, offer: rejoinOffer };
});

// The players' maps in the browser (docs/PLAY_IN_BROWSER.md "Players' maps"): the game's virtual file system holds the six originals only, so a map of the community is fetched from the site (/maps/files/<name>),
// checked against the hash that the room's Start (or the recording's head) names, and written where the game looks for maps (the game's --maps-dir, Original-Ants/Maps) before the match loads.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsMapFiles, node gets module.exports (tests/scripts/web_map_files_check.js runs it with fakes).
//   * The game (src/ants_app/map_fetch.cpp) asks through request / status / message and polls: the network lock-step never waits on a promise inside the wasm.
//   * A file is fetched once per page (kept in memory, as bytes that were checked; the browser's own HTTP cache serves a reload); a failure is never kept, so asking again is a retry.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsMapFiles = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var MAX_BYTES = 1024 * 1024;               // the biggest community map is 230 kB; anything above a megabyte is no map
    var TIMEOUT_MS = 25000;                    // one try (the server's load barrier gives a whole room 60 s)
    var TRIES = 2;                             // a connection error or a 5xx is tried once more
    var RETRY_MS = 600;
    var PENDING = 0, READY = 1, FAILED = 2;    // what status() says (the game reads the number)

    // The address of a map file: everything but the unreserved characters is %XX, as the server writes the thumbnails' addresses (src/ants_server/map_store.cpp percent_encode)
    function fileUrl(name) {
        var out = '';
        for (var i = 0; i < name.length; i++) {
            var c = name.charAt(i);
            out += /[A-Za-z0-9\-._~]/.test(c) ? c : '%' + ('0' + name.charCodeAt(i).toString(16).toUpperCase()).slice(-2);
        }
        return '/maps/files/' + out;
    }

    // What the game calls a map name (valid_map_name): printable ASCII, 5 - 64 characters, no path or Windows-forbidden character, no leading dot, ends in .lvl / .LVL
    function validName(name) {
        if (typeof name !== 'string' || name.length < 5 || name.length > 64) return false;
        for (var i = 0; i < name.length; i++) { var c = name.charCodeAt(i); if (c < 0x20 || c > 0x7E) return false; }
        if (/[\/\\:*?"<>|]/.test(name) || name.charAt(0) === '.') return false;
        var tail = name.slice(-4);
        return tail === '.LVL' || tail === '.lvl';
    }

    var ORIGINALS = /^(TINY|SMALL|MEDIUM|GAUNTLET|TREASURE|ISLANDS)\.LVL$/i;       // the six maps that the game's data holds (lobby_rules.js MAPS)

    function hex8(n) { return ('00000000' + (n >>> 0).toString(16)).slice(-8); }

    // FNV-1a 64 of the bytes (net::hash_file), as 16 hex digits, high word first. The multiplier is 2^40 + 0x1b3: the product mod 2^64 is lo * 0x1b3 (up to 41 bits, exact in a double) plus lo << 40 in the high word.
    function hash64(bytes) {
        var hi = 0xcbf29ce4, lo = 0x84222325;
        for (var i = 0; i < bytes.length; i++) {
            lo = (lo ^ bytes[i]) >>> 0;
            var low = lo * 0x1b3;                                       // < 2^41
            var carry = Math.floor(low / 4294967296);
            var newLo = low % 4294967296;
            var newHi = (hi * 0x1b3 + carry + (lo << 8)) >>> 0;        // (lo << 8): bits 32 - 39 of lo << 40; a 32-bit shift drops what is above, which is above bit 63 here
            hi = newHi;
            lo = newLo >>> 0;
        }
        return hex8(hi) + hex8(lo);
    }

    // The map that a recording (.antsrep) was played on: { name, hash } from its HEAD chunk (src/ants_replay/replay.cpp: the magic, then chunks tag(4) length(u32) payload crc(u32); the head is u16 format,
    // u16 rules and fields varint id, varint length, bytes; field 3 the map's file name, field 4 its FNV-1a 64, little endian), or null when the file is no recording, has no such fields or plays one of the six originals.
    function mapOfReplay(bytes) {
        var MAGIC = [0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a];
        if (!bytes || bytes.length < 16 + 4) return null;
        for (var m = 0; m < MAGIC.length; m++) if (bytes[m] !== MAGIC[m]) return null;
        if (String.fromCharCode(bytes[8], bytes[9], bytes[10], bytes[11]) !== 'HEAD') return null;
        var length = (bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | (bytes[15] << 24)) >>> 0;
        var pos = 16 + 4, end = 16 + length;
        if (end > bytes.length) return null;
        function varint() {
            var v = 0, shift = 0;
            while (pos < end && shift < 35) { var b = bytes[pos++]; v += (b & 0x7f) * Math.pow(2, shift); if (!(b & 0x80)) return v; shift += 7; }
            return -1;
        }
        var name = '', hash = '';
        while (pos < end) {
            var id = varint(), len = varint();
            if (id < 0 || len < 0 || pos + len > end) return null;
            if (id === 3) { for (var i = 0; i < len; i++) name += String.fromCharCode(bytes[pos + i]); }
            else if (id === 4 && len === 8) { hash = ''; for (var k = 7; k >= 0; k--) hash += ('0' + bytes[pos + k].toString(16)).slice(-2); }
            pos += len;
        }
        return name && hash && !ORIGINALS.test(name) ? { name: name, hash: hash } : null;
    }

    // env: { fetch, getFS() (the game's file system, or null before the game runs), setTimeout, clearTimeout, now, timeoutMs, tries, retryMs, AbortController, onChange(name, state) }
    function create(env) {
        var timeoutMs = env.timeoutMs || TIMEOUT_MS, tries = env.tries || TRIES, retryMs = env.retryMs === undefined ? RETRY_MS : env.retryMs;
        var entries = {};                                               // name -> { state, bytes, hash (of the bytes), expect (the hash that the room named, '' when none yet), text, run (a number), folder }

        function entryOf(name) { return entries[name] || (entries[name] = { state: PENDING, bytes: null, hash: '', expect: '', text: '', run: 0, folder: '' }); }

        // One try: the bytes, or why not. The words are for a person (the game shows them as the reason the match did not start).
        function once(name, fresh) {
            return new Promise(function (resolve) {
                var done = false, timer = null, ctl = env.AbortController ? new env.AbortController() : null;
                function finish(result) { if (done) return; done = true; if (timer !== null) env.clearTimeout(timer); resolve(result); }
                timer = env.setTimeout(function () { if (ctl) { try { ctl.abort(); } catch (e) { /* nothing to stop */ } } finish({ ok: false, retry: false, text: 'Downloading the map ' + name + ' took too long. Check your connection and try again.' }); }, timeoutMs);
                var init = { credentials: 'same-origin' };
                if (ctl) init.signal = ctl.signal;
                if (fresh) init.cache = 'reload';
                var request;
                try { request = env.fetch(fileUrl(name), init); } catch (e) { finish({ ok: false, retry: false, text: 'This browser cannot download the map ' + name + '.' }); return; }
                request.then(function (response) {
                    if (response.status === 404 || response.status === 410) { finish({ ok: false, retry: false, text: 'The map ' + name + ' is not on the server (it may have been taken off).' }); return; }
                    if (!response.ok) { finish({ ok: false, retry: response.status >= 500, text: 'The server answered ' + response.status + ' for the map ' + name + '. Try again in a moment.' }); return; }
                    var type = response.headers && response.headers.get ? response.headers.get('content-type') || '' : '';
                    if (/^\s*text\/html/i.test(type)) { finish({ ok: false, retry: false, text: 'The map ' + name + ' came back as a web page, not a map. Check your connection and try again.' }); return; }
                    var announced = response.headers && response.headers.get ? parseInt(response.headers.get('content-length') || '', 10) : NaN;
                    if (announced > MAX_BYTES) { finish({ ok: false, retry: false, text: 'The map file ' + name + ' from the server is not a map (' + announced + ' bytes).' }); return; }
                    return response.arrayBuffer().then(function (buffer) {
                        if (!buffer.byteLength || buffer.byteLength > MAX_BYTES) { finish({ ok: false, retry: false, text: 'The map file ' + name + ' from the server is not a map (' + buffer.byteLength + ' bytes).' }); return; }
                        finish({ ok: true, bytes: new Uint8Array(buffer) });
                    });
                }).then(undefined, function () {
                    finish({ ok: false, retry: true, text: 'Could not download the map ' + name + '. Check your connection and try again.' });
                });
            });
        }

        function download(name, fresh, left) {
            return once(name, fresh).then(function (result) {
                if (result.ok || !result.retry || left <= 1) return result;
                return new Promise(function (resolve) { env.setTimeout(resolve, retryMs); }).then(function () { return download(name, fresh, left - 1); });
            });
        }

        function install(folder, name, bytes) {
            var FS = env.getFS ? env.getFS() : null;
            if (!FS) return 'The game is not ready to take the map ' + name + '.';
            try { FS.writeFile(folder + '/' + name, bytes); } catch (e) { return 'The game could not store the map ' + name + '.'; }
            return '';
        }

        // The bytes of the map, fetched and checked: hashed against the room's (`expect()` is read when the bytes are here, as the Start may come while they travel; '' means none yet), with one more fetch that
        // skips the HTTP cache when the hash is wrong, in case the cache holds the map of before an update of the site.
        function verified(name, expect, fresh) {
            return download(name, fresh, tries).then(function (result) {
                if (!result.ok) return result;
                var h = hash64(result.bytes), want = expect();
                if (want && h !== want) {
                    if (!fresh) return verified(name, expect, true);
                    return { ok: false, mismatch: true, text: 'The map ' + name + ' on this site is not the host\'s copy of it (the files differ). The host may need to choose the map again.' };
                }
                return { ok: true, bytes: result.bytes, hash: h };
            });
        }

        // A copy that was written before the Start named its hash and turned out not to be the host's is taken out again: the game must not find a file there that it was told it cannot have
        function discard(e, name) {
            var FS = env.getFS ? env.getFS() : null;
            if (e.bytes && FS && FS.unlink) { try { FS.unlink(e.folder + '/' + name); } catch (err) { /* it is gone already */ } }
            e.bytes = null; e.hash = '';
        }

        function run(name, fresh) {
            var e = entryOf(name), mine = ++e.run;
            e.state = PENDING; e.text = '';
            return verified(name, function () { return e.expect; }, fresh).then(function (result) {
                if (mine !== e.run) return;                                  // (a newer ask took over)
                if (!result.ok) { if (result.mismatch) discard(e, name); e.state = FAILED; e.text = result.text; changed(name); return; }
                var why = install(e.folder, name, result.bytes);
                if (why) { e.state = FAILED; e.text = why; changed(name); return; }
                e.bytes = result.bytes; e.hash = result.hash; e.state = READY;
                changed(name);
            });
        }
        function changed(name) { if (env.onChange) { try { env.onChange(name, entries[name].state, entries[name].text); } catch (err) { /* the page's strip is not the game's business */ } } }

        // The game asks for a map. hash: 16 hex digits (the room's Start), or '' (a prefetch: the room named the map, the Start is not here yet). Asking again is harmless; after a failure it is a retry.
        function request(folder, name, hash) {
            if (!validName(name)) { var bad = entryOf(name); bad.state = FAILED; bad.text = 'The map name ' + name + ' is not one that the game takes.'; return; }
            var e = entryOf(name);
            e.folder = folder;
            e.expect = hash || '';                                           // (a prefetch names no hash: one that an earlier Start named does not judge this file)
            if (e.state === PENDING && e.run > 0) return;                    // on its way (the hash is checked when it comes)
            if (e.state === READY) {
                if (!e.expect || e.hash === e.expect) {                      // kept and right: the file system must still hold it (a new game page, a cleared folder)
                    var why = install(folder, name, e.bytes);
                    if (why) { e.state = FAILED; e.text = why; }
                    return;
                }
                run(name, true);                                             // kept, but not the host's: the site may have a newer one
                return;
            }
            run(name, false);                                                // new, or failed before: (re)try
        }

        // 0 on its way (or never asked), 1 in the file system and the hash is the room's, 2 failed (message() says why)
        function status(name) {
            var e = entries[name];
            if (!e) return PENDING;
            if (e.state === READY && e.expect && e.hash !== e.expect) return PENDING;
            return e.state;
        }
        function message(name) { return entries[name] ? entries[name].text : ''; }

        // For a page that has the bytes before the game runs (a recording: the file system is not there yet): the checked bytes, to be written when it is. { ok, bytes } or { ok: false, text }
        function fetchOnly(name, hash) {
            if (!validName(name)) return Promise.resolve({ ok: false, text: 'The map name ' + name + ' is not one that the game takes.' });
            return verified(name, function () { return hash || ''; }, false);
        }

        return { request: request, status: status, message: message, fetchOnly: fetchOnly, fileUrl: fileUrl, entries: entries };
    }

    // The object of a page (web/shell.html): the game's file system is Module.FS once the game runs; a map that cannot be had is said in the page's toast for a while
    function forPage(win, getModule, toast) {
        if (!win.fetch) return null;
        var timer = 0;
        return create({
            fetch: function (url, init) { return win.fetch(url, init); }, getFS: function () { var m = getModule(); return m ? m.FS : null; },
            setTimeout: function (f, ms) { return win.setTimeout(f, ms); }, clearTimeout: function (t) { win.clearTimeout(t); }, AbortController: win.AbortController,
            onChange: function (name, state, text) {
                if (state !== FAILED || !toast) return;
                toast.textContent = text; toast.hidden = false;
                win.clearTimeout(timer); timer = win.setTimeout(function () { toast.hidden = true; }, 15000);
            }
        });
    }

    // The page that is not a game (the front page) only warms the browser's HTTP cache with a map it is about to need: the game page, a moment later, finds the file there. It is held to the real fetch's
    // limits: a try ends in TIMEOUT_MS at the latest, and a body that announces more than MAX_BYTES (or an answer that is no file) is cancelled unread.
    function warm(win, name) {
        if (!win.fetch || !validName(name)) return;
        var ctl = null, timer = null;
        function stop() { if (timer !== null && win.clearTimeout) win.clearTimeout(timer); timer = null; }
        function abort() { if (ctl) { try { ctl.abort(); } catch (e) { /* nothing to stop */ } } }
        try {
            var init = { credentials: 'same-origin' };
            if (win.AbortController) { ctl = new win.AbortController(); init.signal = ctl.signal; }
            if (win.setTimeout) timer = win.setTimeout(abort, TIMEOUT_MS);
            win.fetch(fileUrl(name), init).then(function (r) {
                var announced = r.ok && r.headers && r.headers.get ? parseInt(r.headers.get('content-length') || '', 10) : NaN;
                if (!r.ok || announced > MAX_BYTES) { abort(); return null; }
                return r.arrayBuffer();
            }).then(stop, function () { stop(); /* the game page asks again */ });
        } catch (e) { stop(); /* nothing is lost */ }
    }

    // A game that has not started has no file system yet: its preRun function writes the checked bytes where the game looks (the maps folder; its parents are made when the data package has not yet)
    function writer(FSOf, name, bytes) {
        return function () { var FS = FSOf(); try { FS.mkdirTree('Original-Ants/Maps'); } catch (e) { /* it is there */ } FS.writeFile('Original-Ants/Maps/' + name, bytes); };
    }

    // A game for one on a map of the players (the game page's ?mapfile=): the file name when the game takes it and it is none of the six originals' files, else ''
    function communityFile(name) { return validName(name) && !ORIGINALS.test(name) ? name : ''; }

    // The game page's start of a game on this computer: with no map of the players (file '') the game starts at once (`go`); else the file is fetched from the site (`files`: window.antsMapFiles; no hash to check,
    // nobody else plays it), written into the game's maps folder when its file system is there (the game's Module.preRun) and then the game starts. What cannot be had is `fail`'s text.
    function beforeLocalGame(files, file, getModule, fail, go) {
        if (!file) { go(); return; }
        if (!files || typeof Promise === 'undefined') { fail('This browser cannot download the map ' + file + '.'); return; }
        files.fetchOnly(file, '').then(function (map) {
            if (!map.ok) { fail(map.text); return; }
            getModule().preRun.push(writer(function () { return getModule().FS; }, file, map.bytes));
            go();
        });
    }

    return { warm: warm, forPage: forPage, writer: writer, communityFile: communityFile, beforeLocalGame: beforeLocalGame, PENDING: PENDING, READY: READY, FAILED: FAILED, MAX_BYTES: MAX_BYTES, fileUrl: fileUrl, validName: validName, hash64: hash64, mapOfReplay: mapOfReplay, create: create };
});

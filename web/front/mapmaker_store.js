// What the map maker keeps in the browser (docs/MAP_EDITOR.md, "Save as a new map"): saved maps (drafts), the one slot of unsaved work, and the names of the site's maps. A draft is added, never
// replaced. Every call is guarded: a browser that keeps nothing leaves the status 'unavailable' and the page works without it. A plain ES5 script (callbacks).
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapStore = factory();
})(this, function () {
    'use strict';

    var ORIGINALS = ['tiny', 'small', 'medium', 'gauntlet', 'treasure', 'islands'];     // the six maps of the game's data (the site's list holds them too)

    // A name as people compare it: blanks round it and between words count once, capitals do not count
    function clean(name) { return String(name).replace(/\s+/g, ' ').replace(/^ | $/g, ''); }
    function key(name) { return clean(name).toLowerCase(); }

    // The names in the list that GET /maps sends ({"format":1,"maps":[{file}]}): the file names without .lvl, or null when it is no list
    function libraryNames(data) {
        if (!data || data.format !== 1 || !Array.isArray(data.maps)) return null;
        var out = {};
        data.maps.forEach(function (m) { if (m && typeof m.file === 'string') out[key(m.file.replace(/\.lvl$/i, ''))] = true; });
        return out;
    }

    function guard(fn) { try { return fn(); } catch (e) { return undefined; } }

    // env: {idb (the browser's indexedDB or null), fetch (or null)}
    function create(env) {
        var db = null, status = 'pending', drafts = {}, library = {}, libraryState = env.fetch ? 'pending' : 'failed', asked = false;
        ORIGINALS.forEach(function (n) { library[n] = true; });

        // Opens the database and reads the names of the drafts (keys only); done(status) says 'ready' or 'unavailable'
        function open(done) {
            var req = env.idb && guard(function () { return env.idb.open('ants-mapmaker', 1); }), listed, over = false;
            function end(ok) { if (!over) { over = true; status = ok ? 'ready' : 'unavailable'; done(status); } }
            if (!req) return end(false);
            req.onupgradeneeded = function () { guard(function () { req.result.createObjectStore('drafts', { keyPath: 'key' }); req.result.createObjectStore('work'); }); };
            req.onerror = req.onblocked = function () { end(false); };
            req.onsuccess = function () {
                db = req.result;
                listed = guard(function () { return db.transaction('drafts', 'readonly').objectStore('drafts').getAllKeys(); });
                if (!listed) return end(false);
                listed.onerror = function () { end(false); };
                listed.onsuccess = function () { (listed.result || []).forEach(function (k) { drafts[k] = true; }); end(true); };
            };
        }

        // One request on one store; done(error, result): error is '' when it worked, 'exists' for a key that was there already (add), else 'failed'
        function run(name, mode, make, done) {
            var tx, req, result = null, answered = false;
            function end(error) { if (!answered) { answered = true; done(error, result); } }
            if (status !== 'ready') return end('failed');
            guard(function () {
                tx = db.transaction(name, mode);
                req = make(tx.objectStore(name));
                req.onsuccess = function () { result = req.result; };
                req.onerror = function (e) { if (e && e.preventDefault) e.preventDefault(); end(req.error && req.error.name === 'ConstraintError' ? 'exists' : 'failed'); };
                tx.oncomplete = function () { end(''); };
                tx.onabort = tx.onerror = function () { end('failed'); };
            });
            if (!req) end('failed');
        }

        return {
            open: open,
            status: function () { return status; },
            // A name that a map has: a draft, one of the six originals and, once the site's list has come, any library map
            has: function (name) { var k = key(name); return !!(drafts[k] || library[k]); },
            // The site's list of maps, asked once (the page works when it does not come, and says so): 'pending', 'ready' or 'failed'
            loadLibrary: function () {
                var bad = function () { libraryState = 'failed'; };
                if (asked || !env.fetch) return;
                asked = true;
                if (guard(function () {
                    env.fetch('/maps').then(function (r) { return r.ok ? r.json() : null; }).then(function (data) {
                        var names = libraryNames(data);
                        if (!names) return bad();
                        for (var k in names) library[k] = true;
                        libraryState = 'ready';
                    }).then(null, bad);
                    return true;
                }) !== true) bad();
            },
            libraryState: function () { return libraryState; },
            // A new draft under a name that no draft has ('exists' when it is there: it is never written over)
            add: function (name, kind, bytes, done) {
                var k = key(name);
                run('drafts', 'readwrite', function (s) { return s.add({ key: k, name: clean(name), kind: kind, bytes: bytes, saved: Date.now() }); }, function (error) {
                    if (!error || error === 'exists') drafts[k] = true;
                    done(error);
                });
            },
            // The one slot of unsaved work: not a map (never listed, never a name), replaced as the person draws
            keepWork: function (name, kind, bytes, done) { run('work', 'readwrite', function (s) { return s.put({ name: clean(name), kind: kind, bytes: bytes, saved: Date.now() }, 'current'); }, done || function () {}); },
            clearWork: function (done) { run('work', 'readwrite', function (s) { return s.delete('current'); }, done || function () {}); },
            readWork: function (done) { run('work', 'readonly', function (s) { return s.get('current'); }, function (error, result) { done(error ? null : result || null); }); }
        };
    }

    return { create: create, clean: clean, key: key, libraryNames: libraryNames, ORIGINALS: ORIGINALS };
});

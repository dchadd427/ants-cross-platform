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
    // (a no-break space counts as a blank, zero-width characters count for nothing, and a letter written in two ways is one: NFC)
    function clean(name) { return String(name).normalize('NFC').replace(/[\u00AD\u034F\u061C\u115F\u1160\u17B4\u17B5\u180B-\u180E\u200B-\u200F\u202A-\u202E\u2060-\u2064\u2066-\u206F\u3164\uFE00-\uFE0F\uFEFF\uFFA0]|\uDB40[\uDC00-\uDFFF]/g, '').replace(/\s+/g, ' ').replace(/^ | $/g, ''); }
    function key(name) { return clean(name).toLowerCase(); }

    // The names in the list that GET /maps sends ({"format":1,"maps":[{file}]}): the file names without .lvl, or null when it is no list
    function libraryNames(data) {
        if (!data || data.format !== 1 || !Array.isArray(data.maps)) return null;
        var out = Object.create(null);
        data.maps.forEach(function (m) { if (m && typeof m.file === 'string') out[key(m.file.replace(/\.lvl$/i, ''))] = true; });
        return out;
    }

    function guard(fn) { try { return fn(); } catch (e) { return undefined; } }

    // One request on one store of a database (false for no database); done(error, result): '' when it worked, 'exists' for a key that was there already (add), else 'failed'
    function request(db, name, mode, make, done) {
        var tx, req, result = null, answered = false;
        function end(error) { if (!answered) { answered = true; done(error, result); } }
        if (!db) return end('failed');
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

    // What the player sent for approval (mapmaker_submit.js), in a database of its own so that the drafts' database is never touched: one store, a record for each submission (its id is the server's)
    // and the records '#tokens' and '#maker'. Nothing in it is a map of My maps. env: {idb}
    function createSent(env) {
        var db = null, status = 'pending';
        function go(mode, make, done) { request(status === 'ready' && db, 'sent', mode, make, done); }
        return {
            status: function () { return status; },
            open: function (done) {
                var req = env.idb && guard(function () { return env.idb.open('ants-mapmaker-sent', 1); }), over = false;
                function finish(ok) { if (over) return; over = true; status = ok ? 'ready' : 'unavailable'; done(status); }
                if (!req) return finish(false);
                req.onupgradeneeded = function () { guard(function () { req.result.createObjectStore('sent', { keyPath: 'id' }); }); };
                req.onerror = req.onblocked = function () { finish(false); };
                req.onsuccess = function () { db = req.result; finish(true); };
            },
            all: function (done) { go('readonly', function (s) { return s.getAll(); }, function (error, result) { done(error ? 'failed' : '', result || []); }); },
            put: function (rec, done) { go('readwrite', function (s) { return s.put(rec); }, function (error) { done(error ? 'failed' : ''); }); },
            remove: function (id, done) { go('readwrite', function (s) { return s.delete(id); }, function (error) { done(error ? 'failed' : ''); }); }
        };
    }

    // env: {idb (the browser's indexedDB or null), fetch (or null)}
    function create(env) {
        var db = null, status = 'pending', drafts = Object.create(null), library = Object.create(null), libraryState = env.fetch ? 'pending' : 'failed', asked = false, waiting = [];
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
        function run(name, mode, make, done) { request(status === 'ready' && db, name, mode, make, done); }

        return {
            open: open,
            status: function () { return status; },
            // A name that a map has: a draft, one of the six originals and, once the site's list has come, any library map
            has: function (name) { var k = key(name); return !!(drafts[k] || library[k]); },
            // A name that the game's own list has (the six originals and the site's list), not a draft: what Submit for approval tells the player about
            inLibrary: function (name) { return !!library[key(name)]; },
            // The site's list of maps, asked once (the page works when it does not come, and says so): 'pending', 'ready' or 'failed'
            loadLibrary: function () {
                var bad = function () { done('failed'); };
                function done(state) { if (libraryState !== 'pending') return; libraryState = state; waiting.splice(0).forEach(function (f) { f(); }); }
                if (asked || !env.fetch) return;
                asked = true;
                if (env.later) env.later(bad, 8000);                  // (a list that never answers is a list that did not come)
                if (guard(function () {
                    env.fetch('/maps').then(function (r) { return r.ok ? r.json() : null; }).then(function (data) {
                        var names = libraryNames(data);
                        if (!names) return bad();
                        for (var k in names) library[k] = true;
                        done('ready');
                    }).then(null, bad);
                    return true;
                }) !== true) bad();
            },
            libraryState: function () { return libraryState; },
            // fn is called once when the answer has come (at once when it has)
            onLibrary: function (fn) { if (libraryState === 'pending') waiting.push(fn); else fn(); },
            // A new draft under a name that no draft has ('exists' when it is there: it is never written over)
            add: function (name, kind, bytes, done) {
                var k = key(name);
                run('drafts', 'readwrite', function (s) { return s.add({ key: k, name: clean(name), kind: kind, bytes: bytes, saved: Date.now() }); }, function (error) {
                    if (!error || error === 'exists') drafts[k] = true;
                    done(error);
                });
            },
            // The saved maps for My maps, newest first ('failed' when the browser will not say)
            list: function (done) {
                run('drafts', 'readonly', function (s) { return s.getAll(); }, function (error, result) {
                    done(error ? 'failed' : '', (result || []).sort(function (a, b) { return (b.saved || 0) - (a.saved || 0); }));
                });
            },
            // Deletes one saved map from the drafts store (the player's own copy; nothing else is ever deleted), by the key the record itself has: its name is free again
            remove: function (k, done) {
                run('drafts', 'readwrite', function (s) { return s.delete(k); }, function (error) { if (!error) delete drafts[k]; done(error ? 'failed' : ''); });
            },
            // The one slot of unsaved work: not a map (never listed, never a name), replaced as the person draws
            keepWork: function (name, kind, bytes, done) { run('work', 'readwrite', function (s) { return s.put({ name: clean(name), kind: kind, bytes: bytes, saved: Date.now() }, 'current'); }, done || function () {}); },
            clearWork: function (done) { run('work', 'readwrite', function (s) { return s.delete('current'); }, done || function () {}); },
            readWork: function (done) { run('work', 'readonly', function (s) { return s.get('current'); }, function (error, result) { done(error ? null : result || null); }); }
        };
    }

    return { create: create, createSent: createSent, clean: clean, key: key, libraryNames: libraryNames, ORIGINALS: ORIGINALS };
});

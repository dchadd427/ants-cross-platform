// The map core in the page: a thin wrapper around lvl_core.js / lvl_core.wasm (src/ants_assets/lvl_core_c.cpp; docs/MAP_FORMAT.md, "In the browser"). It copies the bytes in,
// calls the module, copies the answer out and gives every buffer back, so that a caller never touches the module's memory. A plain ES5 script that a page and node can load.
//
//   <script src="lvl_core.js"></script><script src="lvl_core_api.js"></script>
//   createLvlCore().then(function (module) { var core = LvlCore.wrap(module); var r = core.readReport(bytes); if (r.status === 0) console.log(r.json.findings); });
//
// Every call answers {status, text, bytes, json, error}: status 0 is the answer (`json` is the parsed report for readReport, checkOriginal and sameLevel, `bytes` is the file for
// rewrite and blank, `text` is the JSON text as the core wrote it), 1 is a refusal and 2 a fault or no memory left (`error` is one sentence). Nothing is thrown by a bad input.
// wrap() throws when the module is not the interface version the wrapper needs; after a call made the module throw (a trap) `isDead()` is true and every later call answers
// status 2 until the module is created again. A sentence can hold text of the file: show it with textContent, never innerHTML.
// `profile`: "new_map" (the default: the limits of a new Classic map, at most 10,000 cells and 130 a side, 512 KB read), "game" (the game's own limits) or "extended" (a place that also takes
// Extended maps, version 9: at most 1,024 a side and 262,144 cells, 4 MiB read; a Classic map keeps its 256 a side there).
// The interface version (LvlCore.EXPECTED_INTERFACE is the one this script needs; LvlCore.moduleVersion(module) is the one a module has, 0 for a module without the call): a page that
// wants to refuse an older module with its own words asks before wrap(), which also throws.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.LvlCore = factory();
})(this, function () {
    'use strict';

    var PROFILES = { new_map: 0, game: 1, extended: 2 };
    var EXPECTED_INTERFACE = 2;     // lvl_interface_version() of the module this wrapper was written for

    function profileOf(profile) {
        if (profile === undefined || profile === null) return 0;
        return Object.prototype.hasOwnProperty.call(PROFILES, profile) ? PROFILES[profile] : -1;
    }

    function moduleVersion(module) {
        return module && typeof module._lvl_interface_version === 'function' ? module._lvl_interface_version() : 0;
    }

    function wrap(module) {
        var found = moduleVersion(module);
        if (found !== EXPECTED_INTERFACE) {
            throw new Error('lvl_core.js is interface version ' + found + ' and lvl_core_api.js needs ' + EXPECTED_INTERFACE + ' (an old copy of one of them is cached: reload the page)');
        }
        var decoder = new TextDecoder('utf-8');
        var dead = false;      // set when the module threw (a wasm trap): its memory is in an unknown state, so every later call fails until the module is created again

        function fault(status, message) { return { status: status, error: message, text: message }; }
        function stopped() { return fault(2, 'the map core stopped after an error: create the module again'); }
        function trapped(e) {
            dead = true;
            return fault(2, 'the map core failed: ' + (e && e.message ? e.message : String(e)));
        }

        // Gives `bytes` to the module as a buffer of its own, calls fn(pointer, size), and always frees the buffer
        function withBuffers(list, fn) {
            if (dead) return stopped();
            var pointers = [];
            try {
                for (var i = 0; i < list.length; i++) {
                    var bytes = list[i];
                    if (!(bytes instanceof Uint8Array)) return fault(1, 'the file must be a Uint8Array');
                    var pointer = module._lvl_alloc(bytes.length);
                    if (!pointer) return fault(2, 'out of memory');
                    pointers.push(pointer);
                    module.HEAPU8.set(bytes, pointer);          // (read HEAPU8 afresh: the memory may have grown, which replaces the view)
                }
                return fn(pointers);
            } catch (e) {
                return trapped(e);
            } finally {
                if (!dead) for (var j = 0; j < pointers.length; j++) module._lvl_free(pointers[j]);
            }
        }

        // Copies a result block out and frees it; `kind` is 'json' or 'bytes'
        function take(block, kind) {
            if (!block) return fault(2, 'out of memory');
            try {
                var status = module._lvl_result_status(block);
                var size = module._lvl_result_size(block);
                var data = module._lvl_result_data(block);
                var bytes = module.HEAPU8.slice(data, data + size);
                if (status !== 0) {
                    var sentence = bytes.length === 0 && status === 2 ? 'out of memory' : decoder.decode(bytes);
                    return { status: status, error: sentence, text: sentence };
                }
                if (kind === 'bytes') return { status: 0, bytes: bytes };
                var text = decoder.decode(bytes);
                return { status: 0, text: text, json: JSON.parse(text) };
            } finally {
                module._lvl_free(block);
            }
        }

        function fileCall(fn, kind) {
            return function (bytes, profile) {
                var p = profileOf(profile);
                if (p < 0) return fault(1, 'unknown profile');
                return withBuffers([bytes], function (ptr) { return take(fn(ptr[0], bytes.length, p), kind); });
            };
        }

        return {
            interfaceVersion: found,
            isDead: function () { return dead; },
            readReport: fileCall(function (ptr, size, p) { return module._lvl_read_report(ptr, size, p); }, 'json'),
            checkOriginal: fileCall(function (ptr, size, p) { return module._lvl_check_original(ptr, size, p); }, 'json'),
            rewrite: fileCall(function (ptr, size, p) { return module._lvl_rewrite(ptr, size, p); }, 'bytes'),
            // rows, columns: the grid; ground: the tile id of the ground (485 is the plain grass of the original maps); eggs for each team; minutes
            blank: function (rows, columns, ground, eggs, minutes, profile) {
                var p = profileOf(profile);
                if (p < 0) return fault(1, 'unknown profile');
                var numbers = [rows, columns, ground, eggs, minutes];
                for (var i = 0; i < numbers.length; i++) {
                    if (typeof numbers[i] !== 'number' || !isFinite(numbers[i]) || numbers[i] < 0 || numbers[i] > 4294967295 || Math.floor(numbers[i]) !== numbers[i]) return fault(1, 'the sizes, the tile, the eggs and the minutes are whole numbers');
                }
                if (dead) return stopped();
                try {
                    return take(module._lvl_blank(rows, columns, ground, eggs, minutes, p), 'bytes');
                } catch (e) {
                    return trapped(e);
                }
            },
            // A new Extended map (version 9): rows, columns (at most 1,024 a side and 262,144 cells), ground, eggs for each team, minutes
            blankExtended: function (rows, columns, ground, eggs, minutes) {
                var numbers = [rows, columns, ground, eggs, minutes];
                for (var i = 0; i < numbers.length; i++) {
                    if (typeof numbers[i] !== 'number' || !isFinite(numbers[i]) || numbers[i] < 0 || numbers[i] > 4294967295 || Math.floor(numbers[i]) !== numbers[i]) return fault(1, 'the sizes, the tile, the eggs and the minutes are whole numbers');
                }
                if (dead) return stopped();
                try {
                    return take(module._lvl_blank_extended(rows, columns, ground, eggs, minutes), 'bytes');
                } catch (e) {
                    return trapped(e);
                }
            },
            // The limits of a profile as the code applies them: json.classic and json.extended (rows, columns, cells), json.read_bytes, json.write_bytes (null: no limit)
            limits: function (profile) {
                var p = profileOf(profile);
                if (p < 0) return fault(1, 'unknown profile');
                if (dead) return stopped();
                try {
                    return take(module._lvl_limits(p), 'json');
                } catch (e) {
                    return trapped(e);
                }
            },
            sameLevel: function (a, b, profile) {
                var p = profileOf(profile);
                if (p < 0) return fault(1, 'unknown profile');
                return withBuffers([a, b], function (ptr) { return take(module._lvl_same_level(ptr[0], a.length, ptr[1], b.length, p), 'json'); });
            }
        };
    }

    return { wrap: wrap, moduleVersion: moduleVersion, EXPECTED_INTERFACE: EXPECTED_INTERFACE };
});

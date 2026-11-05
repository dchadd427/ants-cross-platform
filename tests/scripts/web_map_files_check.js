// Runs the page's OWN code for the players' maps (web/map_files.js) with fakes for the network, the timers and the game's file system: the address and the hash of a file, what the game is told for
// every way a download can end (done, 404, a connection that fails, a server that errs, a page that is not a map, a file of another hash, a slow one), that a failure is never kept, that the file is
// written where the game looks, and the map that a recording names. tests/scripts/test_web_map_files.py runs this with node (the quick tier).
// usage: node web_map_files_check.js web/map_files.js tests/data/replay_head.hex REAL_MAP_FILE REAL_MAP_HASH [maps.json] [tests/data/replay_tmap.hex]    (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');

const [, , modulePath, headPath, realMap, realHash, mapsJson, tmapPath] = process.argv;
if (!modulePath || !headPath || !realMap || !realHash) { console.log('usage: web_map_files_check.js map_files.js replay_head.hex map.lvl hash'); process.exit(2); }
const M = require(require('path').resolve(modulePath));

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
function same(label, got, want) {
    const a = JSON.stringify(got);
    const b = JSON.stringify(want);
    check(label, a === b, 'got ' + a + ', wanted ' + b);
}
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function settle() { for (let i = 0; i < 12; i++) await tick(); }

// ---- the file's hash is the C++ one (net::hash_file, FNV-1a 64) --------------------------------------------------------------------------------------------------------------------------------------
same('hash of nothing', M.hash64(new Uint8Array(0)), 'cbf29ce484222325');
same('hash of a', M.hash64(Buffer.from('a')), 'af63dc4c8601ec8c');
same('hash of foobar', M.hash64(Buffer.from('foobar')), '85944171f73967e8');
same('hash of a real map', M.hash64(fs.readFileSync(realMap)), realHash);
same('hash of every byte value', M.hash64(Uint8Array.from({ length: 256 }, (_, i) => i)), (() => {            // (the same, in BigInt)
    let h = 0xcbf29ce484222325n;
    for (let i = 0; i < 256; i++) { h ^= BigInt(i); h = (h * 0x100000001b3n) & 0xffffffffffffffffn; }
    return h.toString(16).padStart(16, '0');
})());
{
    let seed = 12345, agree = true;                                                                              // a random run against BigInt
    for (let round = 0; round < 200 && agree; round++) {
        const len = round % 7 === 0 ? 0 : 1 + (round * 37) % 300;
        const bytes = new Uint8Array(len);
        for (let i = 0; i < len; i++) { seed = (seed * 1103515245 + 12345) >>> 0; bytes[i] = seed >>> 24; }
        let h = 0xcbf29ce484222325n;
        for (const b of bytes) { h ^= BigInt(b); h = (h * 0x100000001b3n) & 0xffffffffffffffffn; }
        agree = M.hash64(bytes) === h.toString(16).padStart(16, '0');
    }
    check('hash agrees with BigInt on 200 random files', agree);
}

// every map of the community: the hash that Community-Maps/maps.json holds for the file (computed by tools/community_maps.py with the C++ rule) is the one the page computes
if (mapsJson) {
    const folder = require('path').dirname(mapsJson);
    const list = JSON.parse(fs.readFileSync(mapsJson, 'utf8')).maps;
    const wrong = list.filter((m) => M.hash64(fs.readFileSync(require('path').join(folder, m.file))) !== m.hash).map((m) => m.file);
    check('the page hashes all ' + list.length + ' maps of Community-Maps as maps.json says', list.length > 500 && wrong.length === 0, wrong.slice(0, 3).join(', '));
    const odd = list.filter((m) => !M.validName(m.file)).map((m) => m.file);
    check('every file name of Community-Maps is a name that the game takes', odd.length === 0, odd.slice(0, 3).join(', '));
}

// ---- the address and the name ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
same('address of a plain name', M.fileUrl('Gauntlet2.lvl'), '/maps/files/Gauntlet2.lvl');
same('address with spaces, brackets and marks', M.fileUrl("Test Players Map (1)!'#$&~.lvl"), '/maps/files/Test%20Players%20Map%20%281%29%21%27%23%24%26~.lvl');
same('address of a name with a percent sign', M.fileUrl('100%.lvl'), '/maps/files/100%25.lvl');
for (const [name, ok] of [['.lvl', false], ['a.lvl', true], ['abcde.lvl', true], ['ABCDE.LVL', true], ['abcde.Lvl', false], ['.hidden.lvl', false], ['a/b/cd.lvl', false], ['a\\bcde.lvl', false], ['a:bcde.lvl', false], ['a*bcde.lvl', false],
    ['a?bcde.lvl', false], ['a"bcde.lvl', false], ['a<bcde.lvl', false], ['a>bcde.lvl', false], ['a|bcde.lvl', false], ['aébcde.lvl', false], ['a\tbcde.lvl', false], ['x'.repeat(60) + '.lvl', true], ['x'.repeat(61) + '.lvl', false], ['', false]]) {
    check('name ' + JSON.stringify(name) + (ok ? ' is a map name' : ' is not'), M.validName(name) === ok);
}
check('a number is no name', M.validName(5) === false);

// ---- the recording's head --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
    const lines = fs.readFileSync(headPath, 'utf8').split('\n').filter((l) => l && l[0] !== '#');
    const bytes = Uint8Array.from(Buffer.from(lines[lines.length - 1], 'hex'));
    same('the map of a recording written by the C++ encoder', M.mapOfReplay(bytes), { name: 'Test Players Map (1).lvl', hash: '0123456789abcdef' });
    check('a recording cut inside its head names no map', M.mapOfReplay(bytes.slice(0, 40)) === null);
    check('bytes that are no recording name no map', M.mapOfReplay(Uint8Array.from({ length: 80 }, (_, i) => i)) === null);
    check('nothing names no map', M.mapOfReplay(null) === null && M.mapOfReplay(new Uint8Array(0)) === null);
    // a head made by hand (the checksum is not read): any name, any hash; the six originals are in the game's data and name nothing to fetch
    const head = (name, hash) => {
        const field = (id, body) => [id, body.length, ...body];
        const payload = [1, 0, 21, 0, ...field(3, [...Buffer.from(name)]), ...field(4, [...Buffer.from(hash, 'hex').reverse()])];
        return Uint8Array.from([0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a, ...Buffer.from('HEAD'), payload.length, 0, 0, 0, ...payload, 0, 0, 0, 0]);
    };
    same('a hand-made head', M.mapOfReplay(head('Some Map.lvl', 'aabbccddeeff0011')), { name: 'Some Map.lvl', hash: 'aabbccddeeff0011' });
    for (const original of ['TREASURE.LVL', 'tiny.lvl', 'Islands.Lvl'.replace('Lvl', 'LVL'), 'GAUNTLET.LVL', 'SMALL.LVL', 'MEDIUM.LVL']) check(original + ' names nothing to fetch', M.mapOfReplay(head(original, '0011223344556677')) === null);
    check('a head with a name and no hash names no map', M.mapOfReplay(Uint8Array.from([...head('Some Map.lvl', 'aabbccddeeff0011').slice(0, 8 + 4 + 4), 2, 0, 0, 0, 1, 0, 0, 0, 0, 0])) === null);
    const damaged = bytes.slice(); damaged[8] = 0x58;
    check('a first chunk that is not HEAD names no map', M.mapOfReplay(damaged) === null);
    // what a head says of the match, whatever the map is (the page of a file of the visitor's own shows it before the game has read the file, and when the game never will; the six originals too)
    same('the head of a recording written by the C++ encoder says its map', M.infoOfReplay(bytes).map, 'Test Players Map (1).lvl');
    same('the head of an original map says it too, and the seats and their names when it has them', M.infoOfReplay(head('TREASURE.LVL', '0011223344556677')), { map: 'TREASURE.LVL', names: ['', '', '', ''] });
    {
        const field = (id, body) => [id, body.length, ...body];
        const payload = [1, 0, 21, 0, ...field(3, [...Buffer.from('Dune.lvl')]), ...field(4, [...Buffer.from('0011223344556677', 'hex').reverse()]), ...field(6, [13]), ...field(8, [...Buffer.from('Mika')]), ...field(9, [...Buffer.from('')]),
                         ...field(10, [...Buffer.from('Bot (Medium)')]), ...field(11, [...Buffer.from('x'.repeat(65))])];
        const file = Uint8Array.from([0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a, ...Buffer.from('HEAD'), payload.length, 0, 0, 0, ...payload, 0, 0, 0, 0]);
        same('the roster and the names of the seats', M.infoOfReplay(file), { map: 'Dune.lvl', roster: 13, names: ['Mika', '', 'Bot (Medium)', ''] });         // (a name of 65 bytes is no name: the game refuses it)
        const withCtl = (() => { const p2 = [1, 0, 21, 0, ...field(3, [...Buffer.from('Dune.lvl')]), ...field(4, [...Buffer.from('0011223344556677', 'hex').reverse()]), ...field(8, [0x4d, 0x0a, 0x61]), ...field(9, [0xc3, 0xa9])];
                                   return Uint8Array.from([0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a, ...Buffer.from('HEAD'), p2.length, 0, 0, 0, ...p2, 0, 0, 0, 0]); })();
        same('a name with a control character or bytes above ASCII is no name', M.infoOfReplay(withCtl).names, ['', '', '', '']);
    }
    same('a recording cut inside its head, bytes that are none and nothing say nothing', [M.infoOfReplay(bytes.slice(0, 40)), M.infoOfReplay(Uint8Array.from({ length: 80 }, (_, i) => i)), M.infoOfReplay(null), M.infoOfReplay(new Uint8Array(0))], [null, null, null, null]);
}

// ---- the map that a recording carries (its optional tmap chunk) -------------------------------------------------------------------------------------------------------------------------------------------
{
    // the file as the C++ writer makes it (tests/data/replay_tmap.hex, held by RP9.9): the map is the 40 bytes 3 i + 1
    if (tmapPath) {
        const lines = fs.readFileSync(tmapPath, 'utf8').split('\n').filter((l) => l && l[0] !== '#');
        const file = Uint8Array.from(Buffer.from(lines[lines.length - 1], 'hex'));
        const got = M.embeddedMapOfReplay(file);
        check('a recording written by the C++ encoder carries its map', got !== null && got.name === 'Draft Map ~test-0badf00d.lvl' && got.bytes.length === 40 && got.bytes.every((b, i) => b === i * 3 + 1), JSON.stringify(got));
        check('its hash is the one of the head and of the bytes', got !== null && got.hash === M.hash64(got.bytes), JSON.stringify(got && got.hash));
        same('and mapOfReplay still reads its head the old way', M.mapOfReplay(file), { name: 'Draft Map ~test-0badf00d.lvl', hash: got && got.hash });
    }
    // files made here, chunk by chunk (the checksum is the C++ reader's business: the page checks lengths and the map's own hash)
    const table = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
    const crc = (bytes) => { let c = 0xffffffff; for (const b of bytes) c = table[(c ^ b) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
    const le32 = (n) => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255];
    const chunk = (tag, payload) => { const t = [...Buffer.from(tag)]; return [...t, ...le32(payload.length), ...payload, ...le32(crc([...t, ...payload]))]; };
    const field = (id, body) => [id, body.length, ...body];
    const hexBytes = (hex) => [...Buffer.from(hex, 'hex')].reverse();
    const headChunk = (name, hash) => chunk('HEAD', [1, 0, 21, 0, ...field(3, [...Buffer.from(name)]), ...field(4, hexBytes(hash))]);
    const MAGIC = [0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a];
    const MAP = Uint8Array.from({ length: 300 }, (_, i) => (i * 7 + 3) & 255);
    const GOOD = M.hash64(MAP);
    const make = (chunks) => Uint8Array.from([...MAGIC, ...chunks.flat()]);
    const ENDS = chunk('ENDS', [...le32(20), 0, 1, 2, 3, 4, 5, 6, 7, 8]);
    const NAME = 'Draft Map ~test-0badf00d.lvl';
    const good = make([headChunk(NAME, GOOD), chunk('tmap', [...MAP]), ENDS]);
    const got = M.embeddedMapOfReplay(good);
    check('a recording with its map: the name, the hash and exactly the bytes', got !== null && got.name === NAME && got.hash === GOOD && got.bytes.length === 300 && got.bytes.every((b, i) => b === MAP[i]));
    check('a Buffer is read like a Uint8Array', M.embeddedMapOfReplay(Buffer.from(good)) !== null);
    const none = (label, bytes) => check(label, M.embeddedMapOfReplay(bytes) === null);
    none('a map whose hash is not the head\'s is not taken', make([headChunk(NAME, 'aabbccddeeff0011'), chunk('tmap', [...MAP]), ENDS]));
    const flipped = MAP.slice(); flipped[150] ^= 1;
    none('a map with one flipped byte is not taken', make([headChunk(NAME, GOOD), chunk('tmap', [...flipped]), ENDS]));
    none('a recording with no tmap chunk takes the old way (its map by name)', make([headChunk(NAME, GOOD), chunk('CMDS', [0, 0, 0, 0, 0, 0, 0, 0]), ENDS]));
    none('two tmap chunks are not taken (the game refuses the file)', make([headChunk(NAME, GOOD), chunk('tmap', [...MAP]), chunk('tmap', [...MAP]), ENDS]));
    none('an empty tmap is not taken', make([headChunk(NAME, M.hash64(new Uint8Array(0))), chunk('tmap', []), ENDS]));
    const big = new Uint8Array(1024 * 1024 + 1).fill(7);
    none('a map over 1 MiB is not taken, though its hash is right', make([headChunk(NAME, M.hash64(big)), chunk('tmap', [...big]), ENDS]));
    const claim = Uint8Array.from([...MAGIC, ...headChunk(NAME, GOOD), ...Buffer.from('tmap'), 0xff, 0xff, 0xff, 0xff, 1, 2, 3, 4]);
    none('a chunk that claims four billion bytes is not looked at', claim);
    const cut = good.slice(0, good.length - 12 - 13 - 5);                                                       // (cut inside the map's bytes: the chunk runs past the end)
    none('a file cut inside the map has none', cut.slice(0, MAGIC.length + headChunk(NAME, GOOD).length + 8 + 100));
    {   // a chunk that runs past the end of the file, whose cut bytes are the map that the head names: it is a cut chunk, not a map (the game reads such a file as one with no map of its own)
        const part = MAP.slice(0, 120);
        const lying = Uint8Array.from([...MAGIC, ...headChunk(NAME, M.hash64(part)), ...Buffer.from('tmap'), ...le32(300), ...part]);
        none('a map chunk that is cut is not a map, whatever the hash of what is left', lying);
    }
    const afterMap = good.slice(0, MAGIC.length + headChunk(NAME, GOOD).length + 8 + 300 + 4);
    check('a file cut after the map still has it', M.embeddedMapOfReplay(afterMap) !== null && M.embeddedMapOfReplay(afterMap).bytes.length === 300);
    none('a first chunk that is not HEAD names nothing', make([chunk('tmap', [...MAP]), headChunk(NAME, GOOD), ENDS]));
    none('bytes that are no recording carry nothing', Uint8Array.from({ length: 400 }, (_, i) => i));
    check('nothing carries nothing', M.embeddedMapOfReplay(null) === null && M.embeddedMapOfReplay(new Uint8Array(0)) === null && M.embeddedMapOfReplay(undefined) === null);
    for (const original of ['TINY.LVL', 'tiny.lvl', 'Treasure.LVL', 'ISLANDS.lvl']) none('a map named like one of the six originals is never written (' + original + ')', make([headChunk(original, GOOD), chunk('tmap', [...MAP]), ENDS]));
    for (const bad of ['../x.lvl', 'a/b/cdef.lvl', 'a\\bcdef.lvl', '.hidden.lvl', 'x.txt', '.lvl', 'tab\there.lvl']) none('a name that is no map name is never written (' + JSON.stringify(bad) + ')', make([headChunk(bad, GOOD), chunk('tmap', [...MAP]), ENDS]));
    // the bytes are a view into the file: nothing is copied, and a hostile file is walked once, by its lengths
    check('the bytes are a view of the file, not a copy', got.bytes.buffer === good.buffer);
    const many = [headChunk(NAME, GOOD)];
    for (let i = 0; i < 2000; i++) many.push(chunk('padd', [i & 255]));
    many.push(chunk('tmap', [...MAP]), ENDS);
    check('a map after two thousand other chunks is found', M.embeddedMapOfReplay(make(many)) !== null);
}

// ---- the starting ants of a map (the page's guard on the map that a recording carries; the game's own is kMaxEmbeddedStartAnts, include/ants_replay/replay.hpp) ---------------------------------------------------
{
    const tools = require('./replay_file_tools.js');
    const path = require('path');
    const originals = path.resolve(__dirname, '..', '..', 'Original-Ants', 'Maps');
    const tiny = Uint8Array.from(fs.readFileSync(path.join(originals, 'TINY.LVL')));
    same('the six original maps start the ants that the native tool counts (lvl_tool info: 4 colors of 3, 4, 6, 6, 6 and 8)', ['TINY', 'SMALL', 'MEDIUM', 'GAUNTLET', 'TREASURE', 'ISLANDS'].map((n) => M.startMarkersOfMap(fs.readFileSync(path.join(originals, n + '.LVL')))), [12, 16, 24, 24, 24, 32]);
    const counts = [0, 1, 7999, 8000, 8001, 20000, 65535];
    same('a map with exactly N start markers counts N (the limit is at 8,000, the most that block 1 can hold is 65,535)', counts.map((n) => M.startMarkersOfMap(tools.mapWithAnts(tiny, n))), counts);
    check('the page\'s limit is the game\'s: 8,000 ants (kMaxEmbeddedStartAnts)', M.MAX_EMBEDDED_ANTS === 8000);
    {   // the plants of block 1 (a record that is no start marker) start no ant
        const L = tools.lvlLayout(tiny), at = tools.mapWithAnts(tiny, 3), La = tools.lvlLayout(at);
        const plants = [].concat(...[0, 1, 2, 3, 4].map(() => [404 & 255, 404 >> 8, 1, 0, 1, 0]));
        const withPlants = Uint8Array.from(Array.from(at.subarray(0, La.afterAt)).concat(plants, Array.from(at.subarray(La.afterAt))));
        withPlants[La.block1At] = (La.count + 5) & 255; withPlants[La.block1At + 1] = (La.count + 5) >> 8;
        same('plants are no ants: three markers and five plants start three', [tools.lvlLayout(withPlants).count - La.count, M.startMarkersOfMap(withPlants)], [5, 3]);
        check('(the layout helper reads TINY: 31 x 31 cells and 12 records in block 1)', L.rows === 31 && L.columns === 31 && L.count === 12);
    }
    {   // version 9: the tile is the low 12 bits when the high four are at most 5 (the ant type the Map Maker writes), else it is the whole word; version 8 never splits
        const v9 = tools.mapAsVersion9(tiny, 0), L = tools.lvlLayout(v9.slice(8)), at = L.recordsAt + 8;
        same('a version 9 file is counted as the version 8 file that it was made from', M.startMarkersOfMap(v9), 12);
        const typed = Uint8Array.from(v9), set = (i, raw) => { typed[at + 6 * i] = raw & 255; typed[at + 6 * i + 1] = raw >> 8; };
        set(0, (3 << 12) | 154); set(1, (6 << 12) | 154);
        same('version 9: a start marker with an ant type of 0 to 5 counts, one with a type of 6 does not (its tile is the whole word)', M.startMarkersOfMap(typed), 11);
        const classic = Uint8Array.from(tiny), c = tools.lvlLayout(tiny).recordsAt;
        classic[c] = 154; classic[c + 1] = 0x30; classic[c + 6] = 154; classic[c + 7] = 0x60;
        same('version 8: the same two words are not start markers (a Classic map has no ant types)', M.startMarkersOfMap(classic), 10);
    }
    {   // a file that is not laid out as the loader reads it counts as -1 (the game says what is wrong), whatever it claims
        const L = tools.lvlLayout(tiny), dword = (b, at, v) => { const o = Uint8Array.from(b); tools.le32(v).forEach((x, i) => { o[at + i] = x; }); return o; };
        const bad = {
            'nothing': null, 'an empty file': new Uint8Array(0), 'a file of 43 bytes': tiny.slice(0, 43), 'a version that nobody reads': dword(tiny, 0, 7), 'version 10': dword(tiny, 0, 10),
            'a file cut inside the dictionary': tiny.slice(0, 200), 'a file cut inside the layers': tiny.slice(0, L.layersAt + 500), 'a file cut before block 1': tiny.slice(0, L.block1At + 1),
            'a grid of 65,535 x 65,535 cells in a file of 19 KB': dword(dword(tiny, L.gridAt, 65535), L.gridAt + 4, 65535), 'a count of 65,535 records in a file that holds 12': (() => { const o = Uint8Array.from(tiny); o[L.block1At] = 255; o[L.block1At + 1] = 255; return o; })(),
            'a version 9 file whose description runs past the end': (() => { const o = Uint8Array.from(tools.mapAsVersion9(tiny, 0)); o[16] = 255; o[17] = 255; return o; })(),
        };
        Object.keys(bad).forEach((label) => check(label + ' count as -1', M.startMarkersOfMap(bad[label]) === -1, String(M.startMarkersOfMap(bad[label]))));
        same('a version 8 grid keeps the lower 16 bits of each size, as the original does: a header with more bits is the map that its lower bits name', [M.startMarkersOfMap(dword(tiny, L.gridAt, 31 + 65536)), M.startMarkersOfMap(dword(dword(tiny, L.gridAt, 31 + 131072), L.gridAt + 4, 31 + 65536))], [12, 12]);
        const started = Date.now();
        for (let i = 0; i < 2000; i++) M.startMarkersOfMap(dword(dword(tiny, L.gridAt, 4294967295), L.gridAt + 4, 4294967295));
        check('a header that claims four billion cells costs nothing (2,000 of them in under a second)', Date.now() - started < 1000);
    }
    {   // what a recording hands the page: the count with the map; the verdict that goes to the game or to a card
        const crowd = tools.mapWithAnts(tiny, 8001);
        const file = tools.withMap(Uint8Array.from(fs.readFileSync(path.resolve(__dirname, '..', 'data', 'replay_bots_tiny.antsrep'))), 'Crowd.lvl', crowd, true);
        const got = M.embeddedMapOfReplay(file);
        check('a recording that carries a map hands over its count with it', got !== null && got.ants === 8001 && got.bytes.length === crowd.length, JSON.stringify(got && got.ants));
        same('the verdict on 8,001 ants is a sentence with both numbers (and no bytes)', M.carriedVerdict(got), { ok: false, text: 'the map in the file starts 8,001 ants (the start markers of the four colors), more than the 8,000 that a replay is set up with' });
        const ok = M.embeddedMapOfReplay(tools.withMap(Uint8Array.from(fs.readFileSync(path.resolve(__dirname, '..', 'data', 'replay_bots_tiny.antsrep'))), 'Crowd.lvl', tools.mapWithAnts(tiny, 8000), true));
        check('at the limit it goes to the game, with its bytes', M.carriedVerdict(ok).ok === true && M.carriedVerdict(ok).bytes === ok.bytes);
        check('65,535 ants say so with the comma', M.carriedVerdict({ ants: 65535, bytes: null }).text.indexOf('the map in the file starts 65,535 ants (') === 0);
        check('a map that is not laid out as the loader reads it goes to the game (it says why not)', M.carriedVerdict({ ants: -1, bytes: tiny }).ok === true);
    }
}

// ---- the plants and the waypoints of a map: setting a match up looks through all the waypoints for the one of every plant that drops flowers (SimulationEngine::init), so the work is their product ----------------------
{
    const tools = require('./replay_file_tools.js');
    const path = require('path');
    const root = path.resolve(__dirname, '..', '..');
    const tiny = Uint8Array.from(fs.readFileSync(path.join(root, 'Original-Ants', 'Maps', 'TINY.LVL')));
    same('TINY asks for 12 ants and has no plant and no waypoint', M.factsOfMap(tiny), { ants: 12, plants: 0, waypoints: 0 });
    check('the page\'s limits are the game\'s: 8,000 ants and 1,000,000 pairs (kMaxEmbeddedStartAnts, kMaxEmbeddedPlantWaypointPairs)', M.MAX_EMBEDDED_ANTS === 8000 && M.MAX_EMBEDDED_PAIRS === 1000000);
    same('N plants and M waypoints are counted as N and M (the review\'s two files: 65,400 x 65,535 and 30,000 x 30,000)', [[0, 0], [1, 1], [1000, 1000], [1001, 1000], [30000, 30000], [65400, 65535]].map(([n, m]) => { const f = M.factsOfMap(tools.mapWithPlantsAndWaypoints(tiny, n, m)); return [f.plants, f.waypoints]; }),
         [[0, 0], [1, 1], [1000, 1000], [1001, 1000], [30000, 30000], [65400, 65535]]);
    {   // a plant is a record whose tile has the plant flag: 404 .. 408, 410 .. 416, 420 and 421, and no other (the engine's table, src/ants_sim/movement_tables_data.inc)
        const plants = []; for (let id = 395; id <= 430; id++) if (M.factsOfMap(tools.mapWithRecords(tiny, [id], 0)).plants === 1) plants.push(id);
        same('the tiles that are plants (the clovers and the flowers that drop)', plants, [404, 405, 406, 407, 408, 410, 411, 412, 413, 414, 415, 416, 420, 421]);
        same('a start marker is no plant, and a plant is no ant', [M.factsOfMap(tools.mapWithRecords(tiny, [404, 404, 404], 0)).ants, M.factsOfMap(tiny).plants], [12, 0]);
    }
    {   // the waypoints are the records of block 4 that the loader keeps: with a trigger (52 bytes) or without (8), and the record that a file ends in
        const five = tools.mapWithRecords(tiny, [], 5, 2), L = tools.lvlLayout(five), at = L.block4At + 2;
        same('five waypoints, two of them with a trigger (52 bytes each)', [M.factsOfMap(five).waypoints, five.length - tiny.length], [5, 2 * 52 + 3 * 8 + 0]);
        const cutAt = (length) => M.factsOfMap(five.slice(0, length)).waypoints;
        same('a file that ends before the count of block 4 has no waypoint', [cutAt(L.block4At), cutAt(L.block4At + 1)], [0, 0]);
        same('a file that ends inside the first record: not even a cell keeps nothing, a cell keeps the half record (the loader keeps it, with no trigger)', [cutAt(at + 2), cutAt(at + 3), cutAt(at + 4), cutAt(at + 5), cutAt(at + 8)], [0, 0, 1, 1, 1]);
        same('a file that ends inside the second record, which has a trigger (52 bytes: it ends at 104), keeps that half record; at 104 the next record has no cell, at 108 it has the cell only', [cutAt(at + 55), cutAt(at + 56), cutAt(at + 70), cutAt(at + 103), cutAt(at + 104), cutAt(at + 105), cutAt(at + 108), cutAt(at + 112)], [1, 2, 2, 2, 2, 2, 3, 3]);
        const claim = Uint8Array.from(five); claim[L.block4At] = 255; claim[L.block4At + 1] = 255;
        same('a count of 65,535 that the file does not hold is the records that it does hold', M.factsOfMap(claim).waypoints, 5);
    }
    {   // block 2 (the food piles) is walked as the loader walks it: a version 8 pile without stages ends the block, and block 3 is read from the bytes right behind that pile's stage count; version 9 is no file then
        const L = tools.lvlLayout(tiny), head = Array.from(tiny.subarray(0, L.afterAt));
        const block2 = [2, 0, 3, 0, 4, 0, 0, 0, 0, 0, 0, 0], block3 = [0, 0, 0, 0], block4 = [2, 0, 5, 0, 5, 0, 0, 0, 0, 0, 5, 0, 5, 0, 0, 0, 0, 0], eggs = [6, 0];
        const quirk = Uint8Array.from(head.concat(block2, block3, block4, eggs));
        same('a version 8 pile without stages ends block 2 (the second pile that block 2 announces is never read): block 3 and block 4 follow the first one\'s stage count', M.factsOfMap(quirk), { ants: 12, plants: 0, waypoints: 2 });
        const stages = Uint8Array.from(head.concat([1, 0, 3, 0, 4, 0, 0, 0, 0, 0, 2, 0, 9, 0, 1, 0, 8, 0, 2, 0], block3, block4, eggs));
        same('a pile with two stages is walked: 10 bytes and 4 for each stage', M.factsOfMap(stages).waypoints, 2);
        same('in a version 9 file a pile without stages is no file (the loader refuses it): the waypoints are not known', M.factsOfMap(tools.mapAsVersion9(quirk, 0)).waypoints, -1);
        same('a block 2 that ends inside a pile, inside its stages or before block 3 is no file: not known', [M.factsOfMap(Uint8Array.from(head.concat([1, 0, 3, 0, 4, 0]))).waypoints, M.factsOfMap(Uint8Array.from(head.concat([1, 0, 3, 0, 4, 0, 0, 0, 0, 0, 5, 0, 1, 0]))).waypoints, M.factsOfMap(Uint8Array.from(head.concat([0, 0, 1, 0]))).waypoints], [-1, -1, -1]);
        same('a map that is cut before block 4\'s count has the ants and the plants and none of the waypoints', M.factsOfMap(Uint8Array.from(head.concat([0, 0], block3))), { ants: 12, plants: 0, waypoints: 0 });
    }
    {   // the verdict: the product above 1,000,000 is refused with the numbers; each number alone is not; a map that cannot be read is the game's to judge; the ants come first
        const verdict = (n, m) => M.carriedVerdict(Object.assign({ bytes: null }, M.factsOfMap(tools.mapWithPlantsAndWaypoints(tiny, n, m))));
        same('exactly 1,000,000 pairs go to the game, one plant more do not', [verdict(1000, 1000).ok, verdict(1001, 1000).ok, verdict(1000, 1001).ok], [true, false, false]);
        same('the sentence of the review\'s file: both numbers, the pairs, the limit', verdict(65400, 65535), { ok: false, text: 'the map in the file has 65,400 plants and 65,535 waypoints, so setting it up looks at 4,285,989,000 pairs of them, more than the 1,000,000 that a replay is set up with' });
        same('65,535 waypoints and no plant, or 65,523 plants and no waypoint, are no work for the engine', [verdict(0, 65535).ok, verdict(65523, 0).ok], [true, true]);
        check('the verdict of a map with unknown plants or waypoints is the game\'s: it goes on', M.carriedVerdict({ ants: 12, plants: 5000, waypoints: -1, bytes: tiny }).ok && M.carriedVerdict({ ants: 12, plants: -1, waypoints: 5000, bytes: tiny }).ok);
        const both = M.carriedVerdict({ ants: 9000, plants: 5000, waypoints: 5000, bytes: tiny });
        check('a map with too many ants and too many pairs is refused for its ants first', both.ok === false && /^the map in the file starts 9,000 ants/.test(both.text));
        const file = tools.withMap(Uint8Array.from(fs.readFileSync(path.resolve(__dirname, '..', 'data', 'replay_bots_tiny.antsrep'))), 'Plants.lvl', tools.mapWithPlantsAndWaypoints(tiny, 65400, 65535), true);
        const got = M.embeddedMapOfReplay(file);
        check('a recording that carries a map hands over the plants and the waypoints with it', got !== null && got.plants === 65400 && got.waypoints === 65535 && got.ants === 12 && M.carriedVerdict(got).ok === false, JSON.stringify(got && [got.ants, got.plants, got.waypoints]));
    }
    {   // every map of the library is within both limits: the page never refuses a map that a recording of the library carries (the largest has 248 ants, 112 plants and 114 waypoints: 12,768 pairs)
        const community = path.join(root, 'Community-Maps');
        const files = fs.readdirSync(community).filter((f) => /\.lvl$/i.test(f)).map((f) => path.join(community, f)).concat(['TINY', 'SMALL', 'MEDIUM', 'GAUNTLET', 'TREASURE', 'ISLANDS'].map((n) => path.join(root, 'Original-Ants', 'Maps', n + '.LVL')));
        let ants = 0, pairs = 0, unreadable = 0, refused = 0;
        for (const f of files) {
            const bytes = Uint8Array.from(fs.readFileSync(f)), facts = M.factsOfMap(bytes);
            if (facts.ants < 0 || facts.plants < 0 || facts.waypoints < 0) unreadable++;
            ants = Math.max(ants, facts.ants); pairs = Math.max(pairs, facts.plants * facts.waypoints);
            if (!M.carriedVerdict(Object.assign({ bytes }, facts)).ok) refused++;
        }
        same('the 540 maps of the library: all read, none refused; the most ants, the most pairs', [files.length, unreadable, refused, ants, pairs], [540, 0, 0, 248, 12768]);
    }
}

// ---- the downloads ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const NAME = 'Test Players Map (1).lvl';
const BYTES = Uint8Array.from(fs.readFileSync(realMap));
const HASH = realHash;
const OTHER = Uint8Array.from(Buffer.from('another file'));

function response(status, body, type, length) {
    return { ok: status >= 200 && status < 300, status, headers: { get: (k) => (k.toLowerCase() === 'content-type' ? type || 'application/octet-stream' : k.toLowerCase() === 'content-length' && length !== undefined ? String(length) : null) }, arrayBuffer: () => Promise.resolve(body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength)) };
}

// A page: a fetch that answers from a script (one answer for each call; the last one repeats), timers that the test fires, a file system that records its writes
function rig(answers, options) {
    const calls = [];
    const written = {};
    const timers = [];
    const changes = [];
    let fsLost = false;
    const FS = { writeFile(path, bytes) { if (fsLost) throw new Error('gone'); written[path] = bytes; }, unlink(path) { delete written[path]; } };
    const env = {
        fetch(url, init) {
            calls.push({ url, init });
            const a = answers[Math.min(calls.length - 1, answers.length - 1)];
            return typeof a === 'function' ? a(init) : a;
        },
        getFS: () => (options && options.noFS ? null : FS),
        setTimeout(f, ms) { if (ms === 5) { setImmediate(f); return -1; } timers.push({ f, ms, live: true }); return timers.length - 1; },       // (the pause between two tries runs at once; the timer of a try waits for fire())
        clearTimeout(t) { if (timers[t]) timers[t].live = false; },
        AbortController: function () { this.signal = { aborted: false }; this.abort = () => { this.signal.aborted = true; }; },
        timeoutMs: 1000, retryMs: 5, tries: 2,
        onChange(name, state, text) { changes.push([name, state, text]); }
    };
    const maps = M.create(env);
    return { maps, calls, written, timers, changes, lose() { fsLost = true; }, fire(ms) { for (const t of timers.slice()) if (t.live && t.ms === ms) { t.live = false; t.f(); } } };
}
const rejects = () => Promise.reject(new Error('offline'));

(async () => {
    // 1. a map is fetched, checked against the Start's hash and written where the game looks; the game's call and the answer are plain
    {
        const r = rig([Promise.resolve(response(200, BYTES))]);
        same('nothing asked: pending', r.maps.status(NAME), M.PENDING);
        r.maps.request('Original-Ants/Maps', NAME, HASH);
        same('on its way: pending', r.maps.status(NAME), M.PENDING);
        await settle();
        same('arrived: ready', r.maps.status(NAME), M.READY);
        same('one request', r.calls.length, 1);
        same('the address', r.calls[0].url, '/maps/files/Test%20Players%20Map%20%281%29.lvl');
        same('same origin', r.calls[0].init.credentials, 'same-origin');
        check('through the browser cache (no cache option)', r.calls[0].init.cache === undefined);
        same('written under the folder and name', Object.keys(r.written), ['Original-Ants/Maps/' + NAME]);
        check('written bytes are the file', Buffer.compare(Buffer.from(r.written['Original-Ants/Maps/' + NAME]), Buffer.from(BYTES)) === 0);
        // asked again: kept in memory, no second download; written again when the file system lost it
        r.maps.request('Original-Ants/Maps', NAME, HASH);
        await settle();
        same('asked again: still one download', r.calls.length, 1);
        same('asked again: ready', r.maps.status(NAME), M.READY);
        delete r.written['Original-Ants/Maps/' + NAME];
        r.maps.request('Original-Ants/Maps', NAME, HASH);
        check('the file system is given the kept file again', r.written['Original-Ants/Maps/' + NAME] !== undefined && r.calls.length === 1);
    }
    // 2. a prefetch (no hash) and then the Start's hash: no second download when right, a download that skips the cache when wrong
    {
        const r = rig([Promise.resolve(response(200, BYTES))]);
        r.maps.request('f', NAME, '');
        await settle();
        same('prefetched: ready', r.maps.status(NAME), M.READY);
        r.maps.request('f', NAME, HASH);
        same('the Start names the same hash: ready at once', r.maps.status(NAME), M.READY);
        same('and nothing was fetched again', r.calls.length, 1);
        const q = rig([Promise.resolve(response(200, OTHER)), Promise.resolve(response(200, BYTES))]);
        q.maps.request('f', NAME, '');
        await settle();
        same('prefetched the old file: ready', q.maps.status(NAME), M.READY);
        q.maps.request('f', NAME, HASH);
        same('the Start names another hash: pending again', q.maps.status(NAME), M.PENDING);
        await settle();
        same('then the site\'s newer file: ready', q.maps.status(NAME), M.READY);
        same('fetched again, skipping the cache', [q.calls.length, q.calls[1].init.cache], [2, 'reload']);
    }
    // 3. the hash comes while the bytes travel: it is the one that counts
    {
        let release;
        const r = rig([new Promise((resolve) => { release = () => resolve(response(200, OTHER)); }), Promise.resolve(response(200, OTHER))]);
        r.maps.request('f', NAME, '');
        r.maps.request('f', NAME, HASH);
        release();
        await settle();
        same('a file that is not the Start\'s (even after a second try): failed', r.maps.status(NAME), M.FAILED);
        check('the reason says the files differ', /not the host's copy/.test(r.maps.message(NAME)), r.maps.message(NAME));
        same('tried twice, the second skipping the cache', [r.calls.length, r.calls[1].init.cache], [2, 'reload']);
        same('the wrong file was never written', Object.keys(r.written), []);
        same('the page was told once', r.changes.map((c) => c[1]), [M.FAILED]);
    }
    // 3b. a prefetch wrote the site's file before the Start named its hash, and the site's file is not the host's: it is taken out of the game's file system again
    {
        const r = rig([Promise.resolve(response(200, OTHER))]);
        r.maps.request('f', NAME, '');
        await settle();
        same('prefetched: the file is in the game', Object.keys(r.written), ['f/' + NAME]);
        r.maps.request('f', NAME, HASH);
        await settle();
        same('the Start names another hash and the site has no better: failed', r.maps.status(NAME), M.FAILED);
        same('and the wrong file is gone from the game', Object.keys(r.written), []);
    }
    // 3c. a prefetch after a Start forgets the Start's hash (a later room must not be judged by an earlier one)
    {
        const r = rig([Promise.resolve(response(200, OTHER)), Promise.resolve(response(200, OTHER))]);
        r.maps.request('f', NAME, HASH);
        await settle();
        same('the Start named another hash: failed', r.maps.status(NAME), M.FAILED);
        r.maps.request('f', NAME, '');
        await settle();
        same('a prefetch of the same name afterwards is ready, not a false "not the host\'s copy"', [r.maps.status(NAME), r.maps.message(NAME)], [M.READY, '']);
    }
    // 4. the ways a download fails, and that a failure is never kept
    const ways = [
        ['404', [Promise.resolve(response(404, new Uint8Array(0)))], /not on the server/, 1],
        ['410', [Promise.resolve(response(410, new Uint8Array(0)))], /not on the server/, 1],
        ['503 twice', [Promise.resolve(response(503, new Uint8Array(0)))], /answered 503/, 2],
        ['no connection', [() => rejects()], /Check your connection/, 2],
        ['a web page instead of the map', [Promise.resolve(response(200, Buffer.from('<html>'), 'text/html; charset=utf-8'))], /web page/, 1],
        ['an empty body', [Promise.resolve(response(200, new Uint8Array(0)))], /not a map/, 1],
        ['a Content-Length over a megabyte (the body is never read)', [Promise.resolve(Object.assign(response(200, BYTES, undefined, M.MAX_BYTES + 1), { arrayBuffer: () => { throw new Error('read'); } }))], /not a map/, 1],
        ['a body over a megabyte', [Promise.resolve(response(200, new Uint8Array(M.MAX_BYTES + 1)))], /not a map/, 1]
    ];
    for (const [label, answers, words, tries] of ways) {
        const r = rig(answers);
        r.maps.request('f', NAME, HASH);
        await settle();
        same(label + ': failed', r.maps.status(NAME), M.FAILED);
        check(label + ': the reason', words.test(r.maps.message(NAME)), r.maps.message(NAME));
        same(label + ': tries', r.calls.length, tries);
        same(label + ': nothing written', Object.keys(r.written), []);
        same(label + ': the page was told', r.changes.map((c) => c[1]), [M.FAILED]);
        // asking again is a retry (and the network is back)
        r.calls.length = 0;
        answers.length = 0; answers.push(Promise.resolve(response(200, BYTES)));
        r.maps.request('f', NAME, HASH);
        same(label + ': asked again, pending', r.maps.status(NAME), M.PENDING);
        await settle();
        same(label + ': asked again, ready', r.maps.status(NAME), M.READY);
        same(label + ': asked again, fetched again', r.calls.length, 1);
    }
    // 5. a connection error or a 5xx that is gone at the second try
    {
        const r = rig([() => rejects(), Promise.resolve(response(200, BYTES))]);
        r.maps.request('f', NAME, HASH);
        await settle();
        same('the second try works: ready', [r.maps.status(NAME), r.calls.length], [M.READY, 2]);
        const q = rig([Promise.resolve(response(502, new Uint8Array(0))), Promise.resolve(response(200, BYTES))]);
        q.maps.request('f', NAME, HASH);
        await settle();
        same('a 502 and then the file: ready', [q.maps.status(NAME), q.calls.length], [M.READY, 2]);
    }
    // 6. a server that does not answer: the timer ends the try, the request is stopped
    {
        let signal = null;
        const r = rig([(init) => { signal = init.signal; return new Promise(() => {}); }]);
        r.maps.request('f', NAME, HASH);
        await settle();
        same('slow: pending', r.maps.status(NAME), M.PENDING);
        r.fire(1000);
        await settle();
        same('slow: failed', r.maps.status(NAME), M.FAILED);
        check('slow: the reason says it took too long', /took too long/.test(r.maps.message(NAME)), r.maps.message(NAME));
        check('slow: the request was aborted', signal && signal.aborted === true);
        same('slow: no second try (a minute is the room\'s)', r.calls.length, 1);
    }
    // 7. the game is not there to take the file, a name that is no map name, a fetch that throws
    {
        const r = rig([Promise.resolve(response(200, BYTES))], { noFS: true });
        r.maps.request('f', NAME, HASH);
        await settle();
        same('no file system: failed', r.maps.status(NAME), M.FAILED);
        check('no file system: the reason', /not ready to take the map/.test(r.maps.message(NAME)), r.maps.message(NAME));
        const q = rig([Promise.resolve(response(200, BYTES))]);
        q.maps.request('f', '../x.lvl', HASH);
        same('a path is no map name: failed and nothing fetched', [q.maps.status('../x.lvl'), q.calls.length], [M.FAILED, 0]);
        const t = rig([() => { throw new Error('no fetch'); }]);
        t.maps.request('f', NAME, HASH);
        await settle();
        same('a fetch that throws: failed', t.maps.status(NAME), M.FAILED);
        const w = rig([Promise.resolve(response(200, BYTES))]);
        w.lose();
        w.maps.request('f', NAME, HASH);
        await settle();
        same('a file system that refuses the write: failed', w.maps.status(NAME), M.FAILED);
        check('the reason says the game could not store it', /could not store/.test(w.maps.message(NAME)), w.maps.message(NAME));
    }
    // 8. a recording's map: fetched and checked now, written by the page when the game's file system is there
    {
        const r = rig([Promise.resolve(response(200, BYTES))]);
        const got = await r.maps.fetchOnly(NAME, HASH);
        check('fetchOnly gives the checked bytes', got.ok === true && Buffer.compare(Buffer.from(got.bytes), Buffer.from(BYTES)) === 0);
        same('fetchOnly writes nothing', Object.keys(r.written), []);
        const q = rig([Promise.resolve(response(200, OTHER))]);
        const bad = await q.maps.fetchOnly(NAME, HASH);
        check('fetchOnly refuses another file, in the words of a recording and not those of a room', bad.ok === false && bad.mismatch === true && /not the file that this match was played on \(the files differ\)/.test(bad.text) && !/host/.test(bad.text), JSON.stringify(bad));
        const z = rig([Promise.resolve(response(404, new Uint8Array(0)))]);
        const gone = await z.maps.fetchOnly(NAME, HASH);
        check('fetchOnly says a map that is gone', gone.ok === false && /not on the server/.test(gone.text));
        const n = await rig([]).maps.fetchOnly('bad', HASH);
        check('fetchOnly refuses a name that is no map name', n.ok === false);
    }
    // 9. a throwing page callback never reaches the game
    {
        const r = rig([Promise.resolve(response(404, new Uint8Array(0)))]);
        const maps = M.create({ fetch: () => Promise.resolve(response(404, new Uint8Array(0))), getFS: () => ({ writeFile() {} }), setTimeout: r.timers.push.bind(r.timers), clearTimeout() {}, onChange() { throw new Error('strip'); } });
        maps.request('f', NAME, HASH);
        await settle();
        same('a page that throws in onChange: still failed, not broken', maps.status(NAME), M.FAILED);
    }

    // 10. the page's object: made only where there is a fetch, files go to the game's file system when it runs, a failure is said in the toast for a while, a recording's writer makes the folder first
    {
        check('no fetch in the page: no object', M.forPage({}, () => null, null) === null);
        const toast = { textContent: '', hidden: true };
        const timers = [];
        const written = {};
        const win = { fetch: () => Promise.resolve(response(404, new Uint8Array(0))), setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout() {}, AbortController: undefined };
        let module = null;
        const maps = M.forPage(win, () => module, toast);
        maps.request('Original-Ants/Maps', NAME, HASH);
        await settle();
        same('the game is not running: the toast says the map could not be had', [toast.hidden, /not on the server/.test(toast.textContent)], [false, true]);
        const hide = timers.filter((t) => t.ms === 15000).pop();
        check('and goes in 15 seconds', hide !== undefined);
        hide.f();
        same('the toast is hidden again', toast.hidden, true);
        module = { FS: { writeFile(path, bytes) { written[path] = bytes; } } };
        win.fetch = () => Promise.resolve(response(200, BYTES));
        maps.request('Original-Ants/Maps', NAME, HASH);
        await settle();
        same('the game runs: the file is in its file system', [maps.status(NAME), Object.keys(written)], [M.READY, ['Original-Ants/Maps/' + NAME]]);
        const made = [];
        M.writer(() => ({ mkdirTree: (d) => made.push(d), writeFile: (p) => made.push(p) }), NAME, BYTES)();
        same('a recording\'s map: the folder, then the file', made, ['Original-Ants/Maps', 'Original-Ants/Maps/' + NAME]);
        let wrote = '';
        M.writer(() => ({ mkdirTree: () => { throw new Error('exists'); }, writeFile: (p) => { wrote = p; } }), NAME, BYTES)();
        same('a folder that is there is no error', wrote, 'Original-Ants/Maps/' + NAME);
    }

    // 11. the front page only warms the cache: one request for a good name, none for a bad one or a browser without fetch, never a throw
    {
        const asked = [];
        const win = { fetch: (url, init) => { asked.push([url, init.credentials]); return Promise.resolve(response(200, BYTES)); } };
        M.warm(win, NAME);
        M.warm(win, '../x.lvl');
        M.warm({}, NAME);
        M.warm({ fetch: () => { throw new Error('no'); } }, NAME);
        M.warm({ fetch: () => Promise.reject(new Error('offline')) }, NAME);
        await settle();
        same('warm asks for the map once, from the site, through the browser\'s cache', asked, [['/maps/files/Test%20Players%20Map%20%281%29.lvl', 'same-origin']]);

        // the same limits as the real fetch: a time limit, and a body that announces too much is cancelled unread
        const make = (answer) => {
            const w = { timers: [], cleared: [], reads: 0, signal: null };
            w.AbortController = function () { this.signal = { aborted: false }; this.abort = () => { this.signal.aborted = true; }; w.ctl = this; };
            w.setTimeout = (f, ms) => { w.timers.push({ f, ms }); return w.timers.length; };
            w.clearTimeout = (t) => { w.cleared.push(t); };
            w.fetch = (url, init) => { w.signal = init.signal; return answer(w); };
            return w;
        };
        const stuck = make(() => new Promise(() => {}));
        M.warm(stuck, NAME);
        check('warm gives the request a signal and a time limit of 25 s', !!stuck.signal && stuck.signal.aborted === false && stuck.timers.length === 1 && stuck.timers[0].ms === 25000, JSON.stringify(stuck.timers.map((t) => t.ms)));
        stuck.timers[0].f();
        check('a request that does not answer in time is aborted', stuck.signal.aborted === true);
        const big = make((w) => Promise.resolve(Object.assign(response(200, BYTES, 'application/octet-stream', M.MAX_BYTES + 1), { arrayBuffer: () => { w.reads++; return Promise.resolve(new ArrayBuffer(1)); } })));
        M.warm(big, NAME);
        await settle();
        check('a body that announces more than MAX_BYTES is cancelled and not read', big.signal.aborted === true && big.reads === 0 && big.cleared.length === 1);
        const edge = make((w) => Promise.resolve(Object.assign(response(200, BYTES, 'application/octet-stream', M.MAX_BYTES), { arrayBuffer: () => { w.reads++; return Promise.resolve(new ArrayBuffer(1)); } })));
        M.warm(edge, NAME);
        await settle();
        check('a body of exactly MAX_BYTES is read, and the timer is cleared', edge.signal.aborted === false && edge.reads === 1 && edge.cleared.length === 1);
        const gone = make(() => Promise.resolve(response(404, new Uint8Array(0))));
        M.warm(gone, NAME);
        await settle();
        check('an answer that is no file is cancelled and the timer cleared', gone.signal.aborted === true && gone.cleared.length === 1);
        const down = make(() => Promise.reject(new Error('offline')));
        M.warm(down, NAME);
        await settle();
        check('a connection that fails clears the timer', down.cleared.length === 1);
    }

    // 12. a game for one on a map of the players (the game page's ?mapfile=): which file names count, and the start that fetches the file first and writes it for the game
    {
        same('communityFile: a name the game takes is itself', [M.communityFile(NAME), M.communityFile('Bomb Island.lvl'), M.communityFile('!!!BLOCKADES!!!.lvl')], [NAME, 'Bomb Island.lvl', '!!!BLOCKADES!!!.lvl']);
        same('communityFile: none for a name that is no map name, or one of the six originals\' files (any case)', ['', 'bad', '../x.lvl', 'a/b.lvl', 'TINY.LVL', 'tiny.lvl', 'Islands.LVL', 'Treasure.lvl', undefined, null, 5].map((n) => M.communityFile(n)), ['', '', '', '', '', '', '', '', '', '', '']);
        const run = async (files, file) => {
            const log = [], pre = [];
            const module = { preRun: pre, FS: { mkdirTree() {}, writeFile: (p) => log.push('write ' + p) } };
            M.beforeLocalGame(files, file, () => module, (text, gone) => log.push('fail ' + (gone ? '[gone] ' : '') + text), () => log.push('go'));
            await settle();
            pre.forEach((f) => f());
            return log;
        };
        same('no map of the players: the game starts at once, nothing is fetched', await run(null, ''), ['go']);
        const r = rig([Promise.resolve(response(200, BYTES))]);
        same('a map of the players: fetched without a hash, written into the maps folder by the game\'s preRun, then the game starts', await run(r.maps, NAME), ['go', 'write Original-Ants/Maps/' + NAME]);
        same('... and the fetch asked for that file', r.calls.map((c) => c.url), [M.fileUrl(NAME)]);
        const gone = rig([Promise.resolve(response(404, new Uint8Array(0)))]);
        const lost = await run(gone.maps, NAME);
        check('a map that is gone: the page is told why (and that the server says it is gone) and the game does not start', lost.length === 1 && /^fail \[gone\] .*not on the server/.test(lost[0]), JSON.stringify(lost));
        const e403 = rig([Promise.resolve(response(403, new Uint8Array(0)))]);
        const broken = await run(e403.maps, NAME);
        check('an answer other than 404/410 is not "gone" (the page offers Reload for it)', broken.length === 1 && /^fail The server answered 403/.test(broken[0]), JSON.stringify(broken));
        const gone410 = await run(rig([Promise.resolve(response(410, new Uint8Array(0)))]).maps, NAME);
        check('a 410 is gone too', gone410.length === 1 && /^fail \[gone\] /.test(gone410[0]), JSON.stringify(gone410));
        const rejecting = await run({ fetchOnly: () => Promise.reject(new Error('boom')) }, NAME);
        check('a fetch that rejects: the page is told (not gone), the game does not start, nothing is left unhandled', rejecting.length === 1 && /^fail Could not download the map/.test(rejecting[0]), JSON.stringify(rejecting));
        const none = await run(null, NAME);
        check('a browser that cannot fetch: the page is told, the game does not start', none.length === 1 && /^fail This browser cannot download the map/.test(none[0]), JSON.stringify(none));
    }

    console.log((failures ? 'FAILED: ' + failures + ' of ' : 'ok: ') + checks + ' checks');
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL the check itself broke: ' + (e && e.stack || e)); process.exit(1); });

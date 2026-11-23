// "Open in Map maker" (docs/MAP_PAGES.md): the Map Maker opens the library map that its address names, /mapmaker?mapfile=<file name>. The page's own function (addressMap of web/front/mapmaker_app.js, taken
// out of the file as web_mapmaker_convert_check.js takes withLongOrders: the page needs a real canvas and the map core) runs here with fakes for what it asks of the page: the address, the history, the
// store of maps, the dialogs and the question about unsaved drawing. Run by tests/scripts/test_web_map_library.py (node); prints a line for every group and "N failed" at the end.
'use strict';
const fs = require('fs'), path = require('path');
const APP = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'front', 'mapmaker_app.js'), 'utf8');
let failed = 0;
function check(ok, what) { if (!ok) { failed++; console.log('FAIL: ' + what); } }

const found = /\n    function addressMap\(\) \{\n[\s\S]*?\n    \}\n/.exec(APP);
check(found !== null && found[0].split('\n').length <= 30, 'addressMap is in mapmaker_app.js and is a short function');
const FUNCTION = found ? found[0] : '';
// where the page calls it: once, when the decision about the last visit's unsaved work is made (restoreWork's end)
check(/function end\(\) \{ keepReady = true; keepSoon\(\); addressMap\(\); \}/.test(APP), 'the page asks the address once the last visit\'s work is decided');

function world(search, hash) {
    const log = { replaced: [], messages: [], opened: [], asked: [], left: 0 };
    const store = {
        libraryOn: (fn) => fn(), fetched: [],
        onLibrary(fn) { this.libraryOn(fn); }, libraryList: () => [{ file: 'TREASURE.LVL', name: 'Treasure' }, { file: '!Crossed Lake.lvl', name: '!Crossed Lake' }],
        fetchMap(file, done) { this.fetched.push(file); if (file === 'Gone.lvl') return done('This map is not available from the site right now.', null); done('', new Uint8Array([1, 2, 3])); }
    };
    const win = { location: { search, hash: hash || '', pathname: '/mapmaker' }, history: { replaceState: (a, b, url) => log.replaced.push(url) }, URLSearchParams, Array };
    const env = { win, store, dialogs: { message: (title, text) => log.messages.push([title, text]) }, leave: (then) => { log.left++; if (!world.refuse) then(); }, openBytes: (bytes, name) => log.opened.push([Array.from(bytes).join(','), name]),
                  freeName: (wanted) => wanted + ' 2' };
    const make = new Function('win', 'store', 'dialogs', 'leave', 'openBytes', 'freeName', 'Array', FUNCTION + '\n return addressMap;');
    return { log, store, run: () => make(env.win, env.store, env.dialogs, env.leave, env.openBytes, env.freeName, Array)() };
}

// ---- A: the address names a map
{
    const w = world('?mapfile=TREASURE.LVL');
    w.run();
    check(JSON.stringify(w.store.fetched) === '["TREASURE.LVL"]', 'A the map is fetched by the name in the address: ' + w.store.fetched);
    check(w.log.left === 1 && JSON.stringify(w.log.opened) === '[["1,2,3","Treasure 2"]]', 'A after the question about unsaved drawing it opens under the free name of the library\'s name: ' + JSON.stringify(w.log.opened));
    check(JSON.stringify(w.log.replaced) === '["/mapmaker"]', 'A the address loses the word, so that a reload does not open it again: ' + w.log.replaced);
    const odd = world('?mapfile=%21Crossed%20Lake.lvl&x=1', '#top');
    odd.run();
    check(JSON.stringify(odd.store.fetched) === '["!Crossed Lake.lvl"]' && odd.log.opened[0][1] === '!Crossed Lake 2', 'A a name with spaces and signs, percent-encoded: ' + JSON.stringify(odd.log.opened));
    check(JSON.stringify(odd.log.replaced) === '["/mapmaker?x=1#top"]', 'A the other words of the address and its fragment are kept: ' + odd.log.replaced);
    const own = world('?mapfile=My%20Own%20Map.lvl');
    own.run();
    check(own.log.opened[0][1] === 'My Own Map 2', 'A a name that the library list does not have is the file name without .lvl: ' + JSON.stringify(own.log.opened));
    console.log('RUNNING: A a map named in the address ... done');
}
// ---- B: what is not a map, a refusal, drawing that is not saved
{
    const none = world('');
    none.run();
    check(none.store.fetched.length === 0 && none.log.replaced.length === 0 && none.log.left === 0, 'B no word, no request and the address is left alone');
    const other = world('?room=abc');
    other.run();
    check(other.store.fetched.length === 0 && other.log.replaced.length === 0, 'B another word is not ours');
    const gone = world('?mapfile=Gone.lvl');
    gone.run();
    check(gone.log.opened.length === 0 && gone.log.messages.length === 1 && gone.log.messages[0][1].indexOf('not available') >= 0, 'B a map that the site does not have is said in words and opens nothing: ' + JSON.stringify(gone.log.messages));
    world.refuse = true;
    const keep = world('?mapfile=TREASURE.LVL');
    keep.run();
    check(keep.log.left === 1 && keep.log.opened.length === 0, 'B a person who stays on unsaved drawing keeps it');
    world.refuse = false;
    const bad = world('?mapfile=');
    bad.run();
    check(bad.store.fetched.length === 0, 'B an empty name asks for nothing');
    const lone = world('?mapfile=a');
    lone.run();
    check(lone.log.messages.length === 0 && lone.store.fetched[0] === 'a', 'B whatever name is given goes to the store, which says no to a name that is no map file (fetchMap: validMapFile)');
    console.log('RUNNING: B what is not a map ... done');
}
// ---- C: a browser with no URLSearchParams or no history does not stop the page
{
    const w = world('?mapfile=TREASURE.LVL');
    const make = new Function('win', 'store', 'dialogs', 'leave', 'openBytes', 'freeName', 'Array', FUNCTION + '\n return addressMap;');
    let ok = true;
    try { make({ location: { search: '?mapfile=TREASURE.LVL', hash: '' } }, w.store, {}, () => {}, () => {}, () => '', Array)(); } catch (e) { ok = false; }
    check(ok && w.store.fetched.length === 0, 'C a window with no URLSearchParams is no error and opens nothing');
    console.log('RUNNING: C an old browser ... done');
}
console.log(failed + ' failed');
process.exit(failed ? 1 : 0);

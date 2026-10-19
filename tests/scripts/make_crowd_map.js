// Makes the 100 x 100 Extended map of the proofs of play on a map taller than 90 rows with the map maker's own code and the map core, as a person would: a blank Extended map (rules 1),
// a hill for each team near its corner (Blocks.placeHill), 25 ants of each team mixed in a box in the middle (Ants.planMix / putMix), and the Combat Ant as the default ant type
// (core.setProperties), so that every starting ant fights.
//
//   node tests/scripts/make_crowd_map.js OUT.lvl [--module DIR] [--rows N] [--columns N] [--per N]
//
//   --module DIR   the map core of the web build (lvl_core.js, lvl_core.wasm, lvl_core_api.js; default build_web/lvl_core, or ANTS_LVL_CORE_DIR)
// Exit status 0 when the file was written, 1 when it could not be made, 2 for the command line (or no module: the browser check says so and does not run).
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..'), FRONT = path.join(ROOT, 'web', 'front');
const Edit = require(path.join(FRONT, 'mapmaker_edit.js')), Blocks = require(path.join(FRONT, 'mapmaker_blocks.js')), Ants = require(path.join(FRONT, 'mapmaker_ants.js'));
const File = require(path.join(FRONT, 'mapmaker_file.js'));
const LvlCore = require(path.join(ROOT, 'web', 'lvl_core_api.js'));

const o = { out: '', module: process.env.ANTS_LVL_CORE_DIR || path.join(ROOT, 'build_web', 'lvl_core'), rows: 100, columns: 100, per: 25 };
for (let i = 2; i < process.argv.length; i++) {
    const a = process.argv[i];
    if (a === '--module') o.module = process.argv[++i];
    else if (a === '--rows') o.rows = +process.argv[++i];
    else if (a === '--columns') o.columns = +process.argv[++i];
    else if (a === '--per') o.per = +process.argv[++i];
    else if (!o.out && a[0] !== '-') o.out = a;
    else { console.error('usage: make_crowd_map.js OUT.lvl [--module DIR] [--rows N] [--columns N] [--per N]'); process.exit(2); }
}
if (!o.out || !(o.rows >= 20 && o.columns >= 20 && o.per >= 1)) { console.error('usage: make_crowd_map.js OUT.lvl [--module DIR] [--rows N] [--columns N] [--per N]'); process.exit(2); }
if (!fs.existsSync(path.join(o.module, 'lvl_core.wasm'))) { console.error('make_crowd_map: no map core in ' + o.module); process.exit(2); }

require(path.resolve(path.join(o.module, 'lvl_core.js')))().then((module) => {
    const core = LvlCore.wrap(module);
    const made = core.blankExtended(o.rows, o.columns, 485, 6, 10);
    if (made.status !== 0) throw new Error('blankExtended: ' + made.error);
    const ed = Edit.create(File.parse(made.bytes), { rebase: (b) => File.parse(b), erase: Blocks.eraseAt });
    const spots = [[4, 4], [4, o.columns - 8], [o.rows - 8, 4], [o.rows - 8, o.columns - 8]];         // green, red, blue, black
    for (let t = 0; t < 4; t++) {
        const ch = Blocks.placeHill(ed.file, t, spots[t][0], spots[t][1]);
        if (ch.error || ed.act(ch) === null) throw new Error('hill ' + t + ': ' + ch.error);
    }
    const side = Math.ceil(Math.sqrt(o.per * 4)) + 2, r0 = Math.floor(o.rows / 2 - side / 2), c0 = Math.floor(o.columns / 2 - side / 2);
    const plan = Ants.planMix(ed.file, { r0, c0, r1: r0 + side - 1, c1: c0 + side - 1 }, [0, 1, 2, 3], o.per, 1);
    if (!plan.ok) throw new Error('mix: ' + plan.text);
    const put = Ants.putMix(ed.file, plan);
    if (put.error || ed.act(put) === null) throw new Error('mix: ' + put.error);
    const props = core.setProperties(ed.file.bytes, 'ants in the middle', 10, 6, 62, 'extended');
    if (props.status !== 0 || !props.json.ok) throw new Error('setProperties: ' + (props.error || JSON.stringify(props.json)));
    fs.writeFileSync(o.out, Buffer.from(props.bytes));
    const read = core.readReport(props.bytes, 'extended').json;
    console.log('make_crowd_map: ' + o.out + ', ' + props.bytes.length + ' bytes, ' + read.rows + ' x ' + read.columns + ', starting ants ' + read.teams.map((t) => t.start_markers).join(' ') + ', hills ' + read.teams.map((t) => t.hill_cells).join(' '));
}).catch((e) => { console.error('make_crowd_map: ' + (e && e.message || e)); process.exit(1); });

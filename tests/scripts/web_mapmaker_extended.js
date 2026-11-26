// A version 8 map as a version 9 (Extended) map, made for the checks of the map maker without the map core (the core's own conversion is tests/scripts/web_mapmaker_core_check.js): the header is 18
// bytes and the anchors of the objects are offsets (docs/MAP_FORMAT.md, "Version 9"). `front` is the folder of the map maker's scripts. Used by web_mapmaker_fill_check.js and
// web_mapmaker_ai_check.js.
'use strict';
const path = require('path');

function toExtended(front, bytes) {
    const File = require(path.join(front, 'mapmaker_file.js')), Edit = require(path.join(front, 'mapmaker_edit.js'));
    const f = File.parse(bytes), dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
    let n = 10;
    while (n < 40 && bytes[n] !== 0) n++;
    const desc = bytes.subarray(10, n), head = new Uint8Array(18 + desc.length), hv = new DataView(head.buffer);
    hv.setUint32(0, 9, true); hv.setUint32(4, dv.getUint32(4, true), true); hv.setUint16(8, dv.getUint16(8, true), true); hv.setUint32(10, 0, true); hv.setUint16(14, 0, true); hv.setUint16(16, desc.length, true);
    head.set(desc, 18);
    const rest = bytes.slice(40), out = new Uint8Array(head.length + rest.length);
    out.set(head, 0); out.set(rest, head.length);
    const g = File.parse(out);
    if (!g.ok || g.kind !== 'extended') throw new Error('the conversion for the test does not read');
    for (let i = 0; i < g.cells; i++) {
        const t = Edit.tile(g, 2, i);
        if (t === Edit.EMPTY2 || (t >= 129 && t <= 132)) continue;
        const r = Math.floor(i / g.columns), c = i % g.columns, w = Edit.third(f, 2, i), ar = w & 255, ac = w >> 8;
        const p = g.layer2 + 6 * i + 4, rel = ((ar - r) & 255) | (((ac - c) & 255) << 8);
        out[p] = rel & 255; out[p + 1] = rel >> 8;
    }
    return out;
}

module.exports = { toExtended };

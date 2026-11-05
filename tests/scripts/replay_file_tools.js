// Recordings made by hand, for the checks of the page that opens a replay FILE of your own (tests/scripts/web_replay_open_check.js, web_replay_open_browser.js, web_replay_open_fuzz.js). The format is the
// one of include/ants_replay/replay.hpp: the 8 bytes that open a file, then chunks of a tag (4 letters), a length (u32 little endian), the payload and a checksum (CRC-32 of the tag and the payload). The
// CRC here is the plain definition, bit by bit, on purpose: it is not the page's (web/replay_page.js crc32), which is what the checks hold up against it. The head is two numbers (the format and the rules
// of the network) and fields { varint id, varint length, bytes }: 3 the map's file name, 4 its FNV-1a 64 (8 bytes, little endian), 16 the rules number of the simulation (u16).
'use strict';

const MAGIC = [0x89, 0x41, 0x52, 0x50, 0x4c, 0x0d, 0x0a, 0x1a];

function crc32(bytes, seed) {
    let c = ~(seed || 0) >>> 0;
    for (const b of bytes) {
        c = (c ^ b) >>> 0;
        for (let k = 0; k < 8; k++) c = (c & 1 ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1)) >>> 0;
    }
    return ~c >>> 0;
}
const le32 = (n) => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255];
const read32 = (b, at) => (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;

// a chunk: o.length is the length that is written (else the payload's), o.crc the checksum (else the right one)
function chunk(tag, payload, o) {
    const t = Array.from(tag, (ch) => ch.charCodeAt(0));
    const crc = o && o.crc !== undefined ? o.crc : crc32(Uint8Array.from(payload), crc32(Uint8Array.from(t)));
    return t.concat(le32(o && o.length !== undefined ? o.length : payload.length), Array.from(payload), le32(crc));
}
// every chunk of a file: { tag, at (where its frame starts), length, payload (a copy) }; a file that is cut stops at the cut
function chunks(file) {
    const out = [];
    let pos = MAGIC.length;
    while (file.length - pos >= 12) {
        const length = read32(file, pos + 4);
        if (length > file.length - pos - 12) break;
        out.push({ tag: String.fromCharCode(file[pos], file[pos + 1], file[pos + 2], file[pos + 3]), at: pos, length, payload: Array.from(file.subarray(pos + 8, pos + 8 + length)) });
        pos += 12 + length;
    }
    return out;
}
function putVarint(out, n) { while (n >= 128) { out.push((n & 127) | 128); n = Math.floor(n / 128); } out.push(n); }
// the head's payload as [format, rules, fields], fields as [{ id, bytes }]
function readHead(payload) {
    let pos = 4;
    const varint = () => { let v = 0, shift = 0; for (;;) { const b = payload[pos++]; v += (b & 127) * Math.pow(2, shift); if (!(b & 128)) return v; shift += 7; } };
    const fields = [];
    while (pos < payload.length) { const id = varint(), len = varint(); fields.push({ id, bytes: payload.slice(pos, pos + len) }); pos += len; }
    return { head: payload.slice(0, 4), fields };
}
function writeHead(h) {
    const out = Array.from(h.head);
    for (const f of h.fields) { putVarint(out, f.id); putVarint(out, f.bytes.length); for (const b of f.bytes) out.push(b); }
    return out;
}
// FNV-1a 64 of bytes as 16 hex digits, the high word first (net::hash_bytes, web/map_files.js hash64)
function fnv64(bytes) {
    let h = 0xcbf29ce484222325n;
    for (const b of bytes) { h ^= BigInt(b); h = (h * 0x100000001b3n) & 0xffffffffffffffffn; }
    return h.toString(16).padStart(16, '0');
}
const hashBytes = (hex) => Array.from({ length: 8 }, (_, i) => parseInt(hex.slice(14 - 2 * i, 16 - 2 * i), 16));      // (little endian)

// The same recording with its head changed: edit(fields) may change the array (set, add or delete fields); the head's checksum is made again, every other chunk stays as it was. Returns the bytes.
function withHead(file, edit) {
    const all = chunks(file);
    const head = all[0];
    if (!head || head.tag !== 'HEAD') throw new Error('no HEAD first');
    const h = readHead(head.payload);
    edit(h.fields, h);
    const out = MAGIC.concat(chunk('HEAD', writeHead(h)));
    return Uint8Array.from(out.concat(Array.from(file.subarray(head.at + 12 + head.length))));
}
const setField = (fields, id, bytes) => { const f = fields.find((x) => x.id === id); if (f) f.bytes = bytes; else fields.push({ id, bytes }); };
const text = (s) => Array.from(s, (ch) => ch.charCodeAt(0));
// The recording as if it had been played on the map `name` whose file is `mapBytes`: the head names it and holds its hash; with `embed` the file carries the map as its tmap chunk (right after the head)
function withMap(file, name, mapBytes, embed) {
    const hex = fnv64(mapBytes);
    const out = withHead(file, (fields) => { setField(fields, 3, text(name)); setField(fields, 4, hashBytes(hex)); });
    if (!embed) return out;
    const head = chunks(out)[0];
    const at = head.at + 12 + head.length;
    return Uint8Array.from(Array.from(out.subarray(0, at)).concat(chunk('tmap', Array.from(mapBytes)), Array.from(out.subarray(at))));
}
// The recording with another rules number of the simulation (a file of an older or a newer version of the game: the game decides, whatever the page thinks)
function withRules(file, rules) { return withHead(file, (fields) => setField(fields, 16, [rules & 255, (rules >> 8) & 255])); }

// ---- maps made by hand, from the shipped TINY (a Classic map: version 8; docs/MAP_FORMAT.md "The layout"): bytes of a .lvl file that a recording can carry
// Where the parts of a version 8 map start: the dictionary, the grid size, the two layers, block 1 (the start markers and the plants: a word for the count, then 6 bytes a record) and what follows it
function lvlLayout(map) {
    const u16 = (at) => map[at] | (map[at + 1] << 8);
    const gridAt = 40 + 2 + 11 * (u16(40) + 1);
    const rows = read32(map, gridAt) & 0xffff, columns = read32(map, gridAt + 4) & 0xffff;
    const layersAt = gridAt + 8, block1At = layersAt + rows * columns * 12, count = u16(block1At);
    return { rows, columns, gridAt, layersAt, block1At, count, recordsAt: block1At + 2, afterAt: block1At + 2 + 6 * count };
}
// The map with exactly `ants` start markers (all green, on the cell at row 1, column 1): its plants stay and so does the rest of the file. Block 1 holds 65,535 records at most
function mapWithAnts(map, ants) {
    const L = lvlLayout(map), kept = [];
    for (let i = 0; i < L.count; i++) {
        const record = Array.from(map.subarray(L.recordsAt + 6 * i, L.recordsAt + 6 * i + 6)), tile = record[0] | (record[1] << 8);
        if (tile < 152 || tile > 155) kept.push(record);
    }
    const records = kept.concat(Array.from({ length: ants }, () => [154, 0, 1, 0, 1, 0]));
    if (records.length > 65535) throw new Error('block 1 holds at most 65,535 records');
    return Uint8Array.from(Array.from(map.subarray(0, L.block1At)).concat([records.length & 255, records.length >> 8], [].concat(...records), Array.from(map.subarray(L.afterAt))));
}
// The map with a grid of `rows` x `columns` cells (the first cells of the layers) and its start markers where they were: markers outside the grid, a map that the game's loader reads and its validity check refuses
function mapWithGrid(map, rows, columns) {
    const L = lvlLayout(map);
    return Uint8Array.from(Array.from(map.subarray(0, L.gridAt)).concat(le32(rows), le32(columns), Array.from(map.subarray(L.layersAt, L.layersAt + rows * columns * 12)), Array.from(map.subarray(L.block1At))));
}
// The same map as a version 9 file whose rules word is `rules` (0 and 1 are known: any other the reader refuses before it reads a cell): the header of version 9 (format flags 0, the rules, the description as a length and its 30 bytes), then the rest as it was
function mapAsVersion9(map, rules) {
    return Uint8Array.from([9, 0, 0, 0].concat(Array.from(map.subarray(4, 10)), [0, 0, 0, 0, rules & 255, rules >> 8, 30, 0], Array.from(map.subarray(10))));
}

module.exports = { MAGIC, crc32, le32, read32, chunk, chunks, readHead, writeHead, fnv64, hashBytes, withHead, withMap, withRules, setField, text, lvlLayout, mapWithAnts, mapWithGrid, mapAsVersion9 };

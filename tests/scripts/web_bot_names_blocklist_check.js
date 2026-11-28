// The list of the computer players' names against the blocklist, with the page's OWN code (web/front/bot_names.js): the names that the page holds, whole-name words (`=word`) only as a whole name and
// the other words anywhere inside a name, in any case. tests/scripts/test_bot_names_blocklist.py runs this with node, on the library and on copies of it that hold a name that must be found.
// usage: node web_bot_names_blocklist_check.js web/front/bot_names.js tests/data/bot_names_blocklist.txt          (exit 0: the blocklist is a real list and no name is, or holds, a word of it; FAIL lines say what is)
'use strict';
const fs = require('fs');
const path = require('path');
const B = require(path.resolve(process.argv[2]));
const block = fs.readFileSync(process.argv[3], 'latin1').split('\n').map((l) => l.trim()).filter((l) => l !== '' && l[0] !== '#');
let failures = 0;
function check(name, ok, detail) {
    if (!ok) { failures++; console.log('FAIL ' + name + (detail === undefined ? '' : ' -- ' + detail)); }
}
check('the blocklist is a real list (at least 100 words, whole-name and inside)', block.length >= 100 && block.some((w) => w[0] === '=') && block.some((w) => w[0] !== '='), block.length);
const hits = [];
B.NAMES.forEach((n) => block.forEach((w) => { const l = n.toLowerCase(); if (w[0] === '=' ? l === w.slice(1) : l.indexOf(w) >= 0) hits.push(n + ' ~ ' + w); }));
check('no name is, or holds, a word of the blocklist', hits.length === 0, hits.join('; '));
console.log(failures === 0 ? 'ok: the blocklist holds' : failures + ' failures');
process.exit(failures === 0 ? 0 : 1);

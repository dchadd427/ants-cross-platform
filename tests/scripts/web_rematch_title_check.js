// The title of a draft map that the Map Maker's "With other players" sends (web/front/rematch.js, cleanTitle, through frontStrip().begin()): what the page puts in `name=` of POST /test-maps is the
// name of the draft cleaned of its control characters and white space and cut at 45 characters, and "Test Map" when nothing is left; the kind is read from the version word of the bytes. The server cuts and cleans
// the name again (tests/test_server test_test_maps TM1.6); this holds the page's own cut, which a page that forgot it would hide. A name is only ever set as text.
// usage: node web_rematch_title_check.js web/front/rematch.js     (exit 0: every check holds; every failure is printed)
'use strict';
const path = require('path');

const [rematchPath] = process.argv.slice(2);
if (!rematchPath) { console.log('usage: web_rematch_title_check.js rematch.js'); process.exit(2); }
process.on('uncaughtException', (e) => { console.log('FAIL the check stopped on an error: ' + (e && e.stack || e)); process.exit(1); });
process.on('unhandledRejection', (e) => { console.log('FAIL the check stopped on an error: ' + (e && e.stack || e)); process.exit(1); });

let checks = 0;
let failures = 0;
function check(label, ok, detail) {
    checks++;
    if (!ok) { failures++; console.log('FAIL ' + label + (detail === undefined ? '' : ' (' + detail + ')')); }
}
const flush = () => new Promise((resolve) => setImmediate(resolve));

function node(tag) {
    const n = { tag, children: [], attrs: {}, listeners: {}, className: '', textValue: '', hidden: false, parentNode: null, style: {}, href: '', target: '', rel: '', type: '', id: '' };
    for (const bad of ['innerHTML', 'outerHTML']) Object.defineProperty(n, bad, { get() { return ''; }, set() { throw new Error(bad + ' was assigned: a name must be text only'); } });
    n.insertAdjacentHTML = () => { throw new Error('insertAdjacentHTML was called'); };
    Object.defineProperty(n, 'textContent', {
        get() { return n.children.length ? n.children.map((c) => c.textContent).join('') : n.textValue; },
        set(v) { n.children.forEach((c) => { c.parentNode = null; }); n.children = []; n.textValue = String(v); }
    });
    n.appendChild = (c) => { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = n; n.children.push(c); return c; };
    n.removeChild = (c) => { const i = n.children.indexOf(c); if (i >= 0) { n.children.splice(i, 1); c.parentNode = null; } return c; };
    n.insertBefore = (c, ref) => { if (c.parentNode) c.parentNode.removeChild(c); const i = n.children.indexOf(ref); c.parentNode = n; n.children.splice(i < 0 ? n.children.length : i, 0, c); return c; };
    n.setAttribute = (k, v) => { n.attrs[k] = String(v); };
    n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null);
    n.addEventListener = (type, fn) => { (n.listeners[type] = n.listeners[type] || []).push(fn); };
    return n;
}
const doc = { createElement: node, createTextNode: (text) => { const t = node('#text'); t.textValue = String(text); return t; } };

const R = require(path.resolve(rematchPath));

// The address that begin() uploads to for a draft with this name and first byte
async function uploadFor(name, firstByte) {
    const body = node('body'), screen = node('div');
    body.appendChild(screen);
    const asked = [];
    const strip = R.frontStrip({
        doc,
        win: {
            location: { search: '?testroom=k7m2xq9abc12', pathname: '/lobby.html' },
            history: { replaceState: () => {} },
            opener: { closed: false, antsTestGameHandoff: () => ({ name, bytes: new Uint8Array([firstByte === undefined ? 8 : firstByte, 0, 0, 0, 1, 2]) }) }
        },
        before: screen,
        fetch: (u, i) => { asked.push([u, i && i.method]); return Promise.resolve({ status: 201, json: () => Promise.resolve({ test_map: { file: 'X ~test-a1b2c3d4.lvl', name: 'X', kind: 'classic', width: 31, height: 31, usable: [true, true, true, true] } }) }); },
        later: (fn) => fn()
    });
    strip.begin(() => {});
    await flush(); await flush();
    return asked.length === 1 && asked[0][1] === 'POST' ? asked[0][0] : '(no upload: ' + JSON.stringify(asked) + ')';
}

async function main() {
    const wanted = (stem, kind) => '/test-maps?kind=' + (kind || 'classic') + '&name=' + encodeURIComponent(stem);
    check('a short name goes as it is', await uploadFor('Popcorn Remix') === wanted('Popcorn Remix'));
    check('45 characters stay', await uploadFor('B'.repeat(45)) === wanted('B'.repeat(45)));
    check('46 characters are cut to 45', await uploadFor('B'.repeat(46)) === wanted('B'.repeat(45)));
    check('a name of 300 characters is cut to 45', await uploadFor('A'.repeat(300)) === wanted('A'.repeat(45)));
    check('the cut comes after the white space is folded: "a" + 60 blanks + "b" is "a b"', await uploadFor('a' + ' '.repeat(60) + 'b') === wanted('a b'));
    check('control characters and a line end are removed, the ends\' blanks are cut', await uploadFor('  \u0001Hi\r\nthere\u0085\u007f  ') === wanted('Hithere'));
    check('white space inside is folded to one blank', await uploadFor('Hi \t \u00a0 there') === wanted('Hi there'));
    check('nothing left: "Test Map"', await uploadFor('\u0001\u0002 ') === wanted('Test Map'));
    check('no name at all: "Test Map"', await uploadFor(undefined) === wanted('Test Map'));
    check('a name that is not text: "Test Map"', await uploadFor(12345) === wanted('Test Map'));
    check('the kind is the file\'s: a version 9 file is an Extended map', await uploadFor('Big', 9) === wanted('Big', 'extended'));
    console.log('rematch.js title: ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures === 0 ? 0 : 1);
}
main();

// The play count of a map's page (web/map_plays.js, docs/MAP_PAGES.md) runs on the real script with a small fake of the DOM: the number of this map's file comes from the site's list of maps and is put in as text; with no answer
// of the right shape nothing changes. Run by tests/scripts/test_web_map_library.py (node); prints a line for every group and "N failed" at the end.
'use strict';
const fs = require('fs'), path = require('path');
const SOURCE = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'map_plays.js'), 'utf8');
let failed = 0;
function check(ok, what) { if (!ok) { failed++; console.log('FAIL: ' + what); } }

class El {
    constructor(tag) { this.tagName = tag; this.children = []; this.attrs = {}; this.hidden = false; this._text = ''; }
    appendChild(c) { this.children.push(c); return c; }
    set textContent(v) { this.children = []; this._text = String(v); }
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(''); }
    set innerHTML(v) { throw new Error('a count of the site is text, never markup'); }
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
}
function page(file, matches) {
    const line = new El('p'), fact = new El('div'), cell = new El('dd'), ids = { 'plays-line': line, 'plays-fact': fact, 'plays-count': cell };
    line.attrs['data-file'] = file; line.attrs['data-matches'] = matches === undefined ? '/matches?q=Treasure' : matches;
    line._text = 'Find the matches that were played on it in the match history.';
    fact.hidden = true;
    return { line, fact, cell, doc: { getElementById: (id) => ids[id] || null, createElement: (t) => new El(t), createTextNode: (t) => { const n = new El('#text'); n._text = t; return n; } } };
}
function run(p, answer, asked) {
    const g = { fetch: (url, opts) => { if (asked) asked.push([url, opts]); return typeof answer === 'function' ? answer() : Promise.resolve(answer); } };
    new Function('document', 'fetch', SOURCE)(p.doc, g.fetch);
    return new Promise((r) => setTimeout(r, 5));
}
const list = (maps, status) => ({ ok: status === undefined ? true : status < 400, json: () => Promise.resolve({ format: 1, maps }) });
const STATIC = 'Find the matches that were played on it in the match history.';

(async () => {
    // ---- A: the number of the map's file
    {
        const asked = [], p = page('Treasure 2.lvl');
        await run(p, list([{ file: 'TREASURE.LVL', plays: 655 }, { file: 'treasure 2.LVL', plays: 12 }]), asked);
        check(asked.length === 1 && asked[0][0] === '/maps' && asked[0][1].credentials === 'omit', 'A the page asks the list of maps once, with no credentials: ' + JSON.stringify(asked));
        check(!p.fact.hidden && p.cell.textContent === '12 matches', 'A the fact shows the number of this file (any letter case): ' + p.cell.textContent);
        check(p.line.textContent === 'It has been played 12 times on this site: see its matches.' && p.line.children.length === 2 && p.line.children[0].href === '/matches?q=Treasure' && p.line.children[0].textContent === 'see its matches', 'A the sentence says it with a link to the matches: ' + p.line.textContent);
        const one = page('A.lvl');
        await run(one, list([{ file: 'A.lvl', plays: 1 }]));
        check(one.cell.textContent === '1 match' && one.line.textContent === 'It has been played once on this site: see its matches.', 'A one play: ' + one.cell.textContent + ' | ' + one.line.textContent);
        const none = page('A.lvl');
        await run(none, list([{ file: 'A.lvl', plays: 0 }]));
        check(!none.fact.hidden && none.cell.textContent === 'Not yet' && none.line.textContent === 'It has not been played on this site yet: be the first.' && none.line.children.length === 0, 'A no play: ' + none.cell.textContent + ' | ' + none.line.textContent);
        console.log('RUNNING: A the number of the map ... done');
    }
    // ---- B: no answer of the right shape: nothing is shown and the sentence stays
    {
        const cases = [['no network', () => Promise.reject(new Error('down'))], ['the site says no', list([{ file: 'A.lvl', plays: 3 }], 503)], ['not a list', Promise.resolve({ ok: true, json: () => Promise.resolve({ format: 1 }) })],
                       ['not json', Promise.resolve({ ok: true, json: () => Promise.reject(new Error('not json')) })], ['null', Promise.resolve({ ok: true, json: () => Promise.resolve(null) })],
                       ['no entry for the file', list([{ file: 'B.lvl', plays: 3 }, null, 7, { plays: 3 }])], ['plays as text', list([{ file: 'A.lvl', plays: '3' }])], ['a negative number', list([{ file: 'A.lvl', plays: -1 }])],
                       ['a fraction', list([{ file: 'A.lvl', plays: 1.5 }])], ['a number too big', list([{ file: 'A.lvl', plays: 1e12 }])], ['not a number', list([{ file: 'A.lvl', plays: NaN }])],
                       ['markup as the number', list([{ file: 'A.lvl', plays: '<b>3</b>' }])]];
        for (const [what, answer] of cases) {
            const p = page('A.lvl');
            await run(p, answer);
            check(p.fact.hidden && p.cell.textContent === '' && p.line.textContent === STATIC, 'B ' + what + ': the fact stays hidden and the sentence is the one of the page');
        }
        console.log('RUNNING: B no answer ... done');
    }
    // ---- C: a page that has not the parts, a browser with no fetch
    {
        const bare = { getElementById: () => null };
        new Function('document', 'fetch', SOURCE)(bare, () => { throw new Error('nothing to ask'); });
        const p = page('A.lvl');
        new Function('document', 'fetch', SOURCE)(p.doc, undefined);
        check(p.fact.hidden && p.line.textContent === STATIC, 'C no fetch: the page stays as it came');
        const nameless = page('');
        let asked = 0;
        new Function('document', 'fetch', SOURCE)(nameless.doc, () => { asked++; return Promise.resolve(list([])); });
        check(asked === 0, 'C a sentence that names no file asks for nothing');
        const odd = page('A.lvl', 'https://elsewhere.example/x');
        await run(odd, list([{ file: 'A.lvl', plays: 2 }]));
        check(odd.line.children.every((c) => c.tagName !== 'a') && odd.line.textContent === 'It has been played 2 times on this site: see its matches.', 'C an address that is not the matches page is not made a link: ' + odd.line.textContent);
        console.log('RUNNING: C no parts, no fetch ... done');
    }
    console.log(failed + ' failed');
    process.exit(failed ? 1 : 0);
})();

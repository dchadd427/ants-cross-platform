// Runs the game page's OWN code for counting a game on this computer (web/shell.html, the block ANTS_REPORT_BEGIN .. ANTS_REPORT_END): the function antsReportLocalGame that the game calls when the
// first tick of a single-player game runs. Nothing but a POST with nothing in it goes to /stats/local, once for each call, and no failure of any kind (no fetch, a throw, a rejected or a late
// answer, a thenable without catch) reaches the game or is retried.
// tests/scripts/test_web_stats.py runs this with node (the quick tier). usage: node web_stats_check.js web/shell.html     (exit 0: every check holds; every failure is printed)
'use strict';
const fs = require('fs');
const vm = require('vm');

const shellPath = process.argv[2];
if (!shellPath) { console.log('usage: web_stats_check.js shell.html'); process.exit(2); }

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

const text = fs.readFileSync(shellPath, 'utf8');
const begin = text.indexOf('ANTS_REPORT_BEGIN');
const end = text.indexOf('ANTS_REPORT_END');
if (begin < 0 || end < begin || text.indexOf('ANTS_REPORT_BEGIN', begin + 1) >= 0) { console.log('FAIL the block ANTS_REPORT_BEGIN .. ANTS_REPORT_END is missing or twice in ' + shellPath); process.exit(1); }
const block = text.slice(text.indexOf('\n', begin) + 1, text.lastIndexOf('\n', end));

// A page whose global scope has the given fetch (and nothing else): the block is a script, and its function is a global of the page, as the game's EM_JS code needs it
function page(fetchImpl) {
    const context = vm.createContext({ fetch: fetchImpl });
    vm.runInContext(block, context);
    return context;
}

// The unhandled rejection of a promise that nobody catches is what a page would log as an error: none may come from the function
let unhandled = 0;
process.on('unhandledRejection', () => { unhandled++; });

(async () => {
    const quiet = () => new Promise((resolve) => setTimeout(resolve, 20));

    // 1. the function is a global and, called, makes ONE request: a POST of /stats/local with keepalive and nothing else (no body, no headers, no credentials, no mode)
    {
        const calls = [];
        const context = page(function () { calls.push(Array.prototype.slice.call(arguments)); return Promise.resolve({ ok: true, status: 204 }); });
        check('antsReportLocalGame is a global of the page', typeof context.antsReportLocalGame === 'function');
        const result = context.antsReportLocalGame();
        await quiet();
        check('it returns nothing', result === undefined);
        check('one call makes one request', calls.length === 1, calls.length);
        same('the request is a POST of /stats/local, keepalive, nothing else', calls[0], ['/stats/local', { method: 'POST', keepalive: true }]);
        check('the address is the page\'s own (no host, no query)', calls[0][0] === '/stats/local');
        context.antsReportLocalGame();
        context.antsReportLocalGame();
        await quiet();
        check('it does not remember: each call is a request (the game says when a match began, once for each match)', calls.length === 3, calls.length);
    }

    // 2. every way to fail is swallowed, nothing is retried, nothing is left unhandled
    const failing = [
        ['a rejected request (the network is down)', () => Promise.reject(new TypeError('Failed to fetch'))],
        ['a refusal that is no error (503, the rate limit of nginx)', () => Promise.resolve({ ok: false, status: 503 })],
        ['a throw of fetch itself', () => { throw new TypeError('fetch is not allowed here'); }],
        ['a throw that is no error object', () => { throw 'no'; }],
        ['a fetch that returns nothing', () => undefined],
        ['a fetch that returns null', () => null],
        ['a fetch that returns a number', () => 7],
        ['a thenable that has no catch', () => ({ then() {} })],
        ['a promise that never settles', () => new Promise(() => {})],
        ['a promise that rejects with nothing', () => Promise.reject()],
        ['a promise that rejects late', () => new Promise((_, reject) => setTimeout(() => reject(new Error('late')), 5))],
    ];
    for (const [label, impl] of failing) {
        let calls = 0;
        const context = page(function () { calls++; return impl(); });
        let thrown = null;
        try { context.antsReportLocalGame(); } catch (e) { thrown = e; }
        await quiet();
        await quiet();
        check(label + ': nothing reaches the game', thrown === null, String(thrown));
        check(label + ': one request, no retry', calls === 1, calls);
    }
    {   // a browser without fetch at all: the name is not even defined
        const context = vm.createContext({});
        vm.runInContext(block, context);
        let thrown = null;
        try { context.antsReportLocalGame(); } catch (e) { thrown = e; }
        check('a page without fetch: nothing reaches the game', thrown === null, String(thrown));
    }
    {   // fetch is undefined, or something that is no function
        for (const value of [undefined, null, 5, 'fetch', {}]) {
            const context = page(value);
            let thrown = null;
            try { context.antsReportLocalGame(); } catch (e) { thrown = e; }
            check('fetch is ' + JSON.stringify(value) + ': nothing reaches the game', thrown === null, String(thrown));
        }
    }
    check('no rejection was left unhandled', unhandled === 0, unhandled);

    // 3. what the page's text says: the address appears once in the block, the page has no other place that posts a report, and nothing here reads a body, a name or an address of the player
    check('the block names /stats/local once', (block.match(/\/stats\/local/g) || []).length === 1);
    check('the block sends no body, headers or credentials', !/\b(body|headers|credentials|mode)\b\s*:/.test(block.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '')));
    check('the page has no other reference to /stats/local than this function\'s', (text.match(/\/stats\/local/g) || []).length === (text.slice(begin, end).match(/\/stats\/local/g) || []).length);

    console.log('web_stats_check: ' + checks + ' checks, ' + failures + ' failures');
    process.exit(failures === 0 ? 0 : 1);
})();

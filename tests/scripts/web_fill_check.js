// Runs the page's own code that validates ?fill= on a table of addresses' values (tests/scripts/test_ants_server.sh): web/shell.html's antsFillArg (what may reach the game's arguments
// as --fill-bots) and web/four.html's validFill (what may go into a link of the room). Both functions are cut out of the page between their marker comments and run as they are.
// usage: node web_fill_check.js web/shell.html web/four.html     (exit 0: every row of the table holds; the first differing row is printed)
'use strict';
const fs = require('fs');

function extract(path, begin, end, name) {
    const text = fs.readFileSync(path, 'utf8');
    const at = text.indexOf(begin);
    const from = at < 0 ? -1 : text.indexOf('\n', at) + 1;              // (the code starts on the line after the marker's comment)
    const to = at < 0 ? -1 : text.lastIndexOf('\n', text.indexOf(end, from) - 1) + 1;     // (and ends before the line of the closing marker)
    if (from < 1 || to < from) throw new Error(path + ': the markers ' + begin + ' / ' + end + ' are missing');
    const code = text.slice(from, to);
    return new Function(code + '\nreturn ' + name + ';')();
}

// [value, what comes back]: the three words in any case come back in lower case; everything else is no fill
const table = [
    ['easy', 'easy'], ['medium', 'medium'], ['hard', 'hard'],
    ['EASY', 'easy'], ['Medium', 'medium'], ['HARD', 'hard'], ['mEdIuM', 'medium'],
    ['', ''], [null, ''], [undefined, ''], [42, ''], [{}, ''], [['easy'], ''],
    ['none', ''], ['off', ''], ['bot', ''], ['mediu', ''], ['mediumm', ''], ['easy ', ''], [' easy', ''], ['medium\n', ''], ['\nmedium', ''], ['hard\0', ''],
    ['easy medium', ''], ['easy;--room x', ''], ['hard&seat=1', ''], ['--fill-bots', ''], ['easy\u0000hard', ''], ['İEASY', ''],
    ['MEDIUM', 'medium'], ['ｍｅｄｉｕｍ', ''], ['Keasy', ''], ['hard'.repeat(2000), ''],
];

let failed = 0;
for (const [path, begin, end, name] of [[process.argv[2], 'ANTS_FILL_BEGIN', 'ANTS_FILL_END', 'antsFillArg'], [process.argv[3], 'FILL_BEGIN', 'FILL_END', 'validFill']]) {
    if (!path) { console.log('usage: web_fill_check.js shell.html four.html'); process.exit(2); }
    let fn;
    try { fn = extract(path, begin, end, name); } catch (e) { console.log('FAIL ' + e.message); failed++; continue; }
    for (const [value, want] of table) {
        let got;
        try { got = fn(value); } catch (e) { got = 'threw ' + e.message; }
        if (got !== want) { console.log('FAIL ' + name + '(' + JSON.stringify(value).slice(0, 40) + ') = ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
    }
}
console.log(failed === 0 ? 'ok: ' + table.length + ' values, two functions' : failed + ' rows differ');
process.exit(failed === 0 ? 0 : 1);

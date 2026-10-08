// Runs the page's own code that validates ?fill=, ?teams=, ?start=, ?roommap=, ?roomseats=, ?roomleaderstart= and ?platform= on tables of addresses' values (tests/scripts/test_ants_server.sh): web/shell.html's
// antsFillArg / antsFillPlanArg / antsTeamsArg / antsStartArg / antsRoomMapArg / antsRoomSeatsArg / antsRoomLeaderStartArg / antsPlatformArg (what may reach the game's arguments as --fill-bots, --teams, --start-when,
// --room-map, --room-seats, --room-leader-start and --platform; ?roomteams= is read by antsTeamsArg, like ?teams=) and web/lobby.html's validFill / validFillPlan (what may go into a link of the room). The functions are cut
// out of the pages between their marker comments and run as they are.
// usage: node web_fill_check.js web/shell.html web/lobby.html     (exit 0: every row of the table holds; the first differing row is printed)
'use strict';
const fs = require('fs');

function extract(path, begin, end, names) {
    const text = fs.readFileSync(path, 'utf8');
    const at = text.indexOf(begin);
    const from = at < 0 ? -1 : text.indexOf('\n', at) + 1;              // (the code starts on the line after the marker's comment)
    const to = at < 0 ? -1 : text.lastIndexOf('\n', text.indexOf(end, from) - 1) + 1;     // (and ends before the line of the closing marker)
    if (from < 1 || to < from) throw new Error(path + ': the markers ' + begin + ' / ' + end + ' are missing');
    const code = text.slice(from, to);
    return new Function(code + '\nreturn {' + names.map((n) => n + ': ' + n).join(', ') + '};')();
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

// ... and what a LIST of levels is: four words for the seats 0 - 3 (none for none), or one word (every seat); the tested lower case text comes back, "" for no bots at all and for anything that is not one word
// or four. The one-word rows of the table above are rows of this one too.
const planTable = table.concat([
    ['none,none,easy,hard', 'none,none,easy,hard'], ['none,easy,none,hard', 'none,easy,none,hard'], ['hard,hard,hard,hard', 'hard,hard,hard,hard'], ['easy,easy,easy,easy', 'easy,easy,easy,easy'],
    ['NONE,Easy,None,HARD', 'none,easy,none,hard'], ['None,mEdIuM,medium,MEDIUM', 'none,medium,medium,medium'],
    ['none,none,none,none', ''], ['NONE,none,None,NONE', ''],
    ['easy,hard', ''], ['easy,hard,easy', ''], ['none,none,easy', ''], ['none,none,easy,hard,none', ''], ['none,none,easy,hard,', ''], ['none,none,easy,', ''], [',,,', ''], [',', ''], ['none,none,,hard', ''],
    ['none, none,easy,hard', ''], ['none,none,easy,hard ', ''], [' none,none,easy,hard', ''], ['none,none,easy,hard\n', ''], ['none,none,easy,har', ''], ['none,none,easy,hardd', ''], ['none,none,easy,loud', ''],
    ['none;none;easy;hard', ''], ['none none easy hard', ''], ['none|none|easy|hard', ''], ['none,none,easy,hard&seat=1', ''], ['none,none,easy,--room x', ''], ['none,none,easy,hard;ls', ''], ['--fill-bots,none,easy,hard', ''],
    ['none,none,easy,hard\u0000', ''], ['none,none,ｅａｓｙ,hard', ''], ['none,none,İEASY,hard', ''], ['constructor,none,easy,hard', ''], ['__proto__,none,easy,hard', ''],
    ['hard,'.repeat(2000), ''], [['none', 'none', 'easy', 'hard'], ''], [{ toString() { return 'none,none,easy,hard'; } }, ''],
]);
// how many people the leader's game waits for before it presses START by itself (the front page's card: 1 + its Friend rows): one digit 1 - 4, "" for anything else (no hook)
const startTable = [
    ['1', '1'], ['2', '2'], ['3', '3'], ['4', '4'],
    ['', ''], [null, ''], [undefined, ''], [1, ''], [2, ''], [{}, ''], [['2'], ''], [{ toString() { return '2'; } }, ''],
    ['0', ''], ['5', ''], ['9', ''], ['10', ''], ['12', ''], ['01', ''], ['02', ''], ['-1', ''], ['+2', ''], ['2.0', ''], ['2.5', ''], ['1e1', ''], ['0x2', ''],
    [' 2', ''], ['2 ', ''], ['2\n', ''], ['\n2', ''], ['2\t', ''], ['2\0', ''], ['2,3', ''], ['2;ls', ''], ['2&seat=1', ''], ['2 --room x', ''], ['two', ''], ['\u0662', ''], ['\uff12', ''], ['constructor', ''],
    ['2'.repeat(2000), ''],
];
// the teams: two different seats 0 - 3 as A+B (a "+" that arrived as a blank is read too); "" for anything else, free for all included (nothing to tell the game)
const teamsTable = [
    ['0+1', '0+1'], ['0+2', '0+2'], ['0+3', '0+3'], ['1+2', '1+2'], ['1+3', '1+3'], ['2+3', '2+3'], ['3+0', '3+0'], ['1+0', '1+0'], ['2+1', '2+1'],
    ['0 1', '0+1'], ['2 3', '2+3'], ['3 0', '3+0'],
    ['', ''], [null, ''], [undefined, ''], [42, ''], [{}, ''], [['0+1'], ''],
    ['ffa', ''], ['FFA', ''], ['none', ''], ['0+0', ''], ['1+1', ''], ['2 2', ''], ['3+3', ''], ['0+4', ''], ['4+0', ''], ['0+9', ''], ['4+5', ''], ['9+0', ''],
    ['01', ''], ['0', ''], ['0+', ''], ['+1', ''], ['0++1', ''], ['0+ 1', ''], ['0 +1', ''], ['0  1', ''], [' 0+1', ''], ['0+1 ', ''], ['0+1\n', ''], ['\n0+1', ''], ['0\t1', ''], ['0+1+2', ''], ['0+11', ''], ['00+1', ''],
    ['0+1&teams=ffa', ''], ['0+1;ls', ''], ['0+1\n--name x', ''], ['0+1,2+3', ''], ['a+b', ''], ['0+one', ''], ['0+\u0967', ''], ['0+\uff11', ''], ['-1+2', ''], ['0-1', ''], ['0/1', ''], ['0+1\u0000', ''],
    ['constructor', ''], ['__proto__', ''], ['0+1'.repeat(2000), ''],
];
// the map of a room's create block (protocol 15): one of the six keys in any case, as the tested lower case key; "" for anything else (never a file name, a path or a word of the address)
const roomMapTable = [
    ['tiny', 'tiny'], ['small', 'small'], ['medium', 'medium'], ['gauntlet', 'gauntlet'], ['treasure', 'treasure'], ['islands', 'islands'],
    ['TINY', 'tiny'], ['Small', 'small'], ['MEDIUM', 'medium'], ['Gauntlet', 'gauntlet'], ['tReAsUrE', 'treasure'], ['ISLANDS', 'islands'],
    ['', ''], [null, ''], [undefined, ''], [42, ''], [{}, ''], [['tiny'], ''], [{ toString() { return 'tiny'; } }, ''],
    ['TINY.LVL', ''], ['tiny.lvl', ''], ['tin', ''], ['tinyy', ''], ['tiny ', ''], [' tiny', ''], ['tiny\n', ''], ['\ntiny', ''], ['tiny\0', ''], ['tiny small', ''], ['tiny,small', ''], ['tiny;--room x', ''], ['tiny&roomseats=2', ''], ['--room-map', ''],
    ['../tiny', ''], ['/tiny', ''], ['Original-Ants/Maps/TINY.LVL', ''], ['nowhere', ''], ['any', ''], ['constructor', ''], ['__proto__', ''], ['toString', ''], ['hasOwnProperty', ''],
    ['ｔｉｎｙ', ''], ['tіny', ''], ['İslands', ''], ['treasure'.repeat(2000), ''],
];
// the seats of a room's create block: one digit 2 - 4 (the tested digit comes back)
const roomSeatsTable = [
    ['2', '2'], ['3', '3'], ['4', '4'],
    ['', ''], [null, ''], [undefined, ''], [2, ''], [4, ''], [{}, ''], [['3'], ''], [{ toString() { return '3'; } }, ''],
    ['0', ''], ['1', ''], ['5', ''], ['9', ''], ['10', ''], ['12', ''], ['02', ''], ['03', ''], ['-2', ''], ['+2', ''], ['2.0', ''], ['2.5', ''], ['1e1', ''], ['0x2', ''],
    [' 2', ''], ['2 ', ''], ['2\n', ''], ['\n2', ''], ['2\t', ''], ['2\0', ''], ['2,3', ''], ['2;ls', ''], ['2&seat=1', ''], ['2 --room x', ''], ['two', ''], ['٢', ''], ['２', ''], ['constructor', ''],
    ['2'.repeat(2000), ''],
];
// the flag of a full room that waits for its leader's START (the game's --room-leader-start takes no value): true for exactly "1", false for everything else
const roomLeaderStartTable = [
    ['1', true],
    ['', false], ['0', false], ['2', false], ['true', false], ['yes', false], ['on', false], ['11', false], ['01', false], ['1 ', false], [' 1', false], ['1\n', false], ['\n1', false], ['1\0', false], ['1,1', false], ['one', false], ['١', false], ['１', false],
    ['1&seat=1', false], ['1;ls', false], [null, false], [undefined, false], [1, false], [true, false], [{}, false], [['1'], false], [{ toString() { return '1'; } }, false], ['1'.repeat(2000), false],
];
// what the game tells the room about itself (the game's --platform): [browser-]windows, macos, linux, android, ios or other in any case, as the tested lower case word; "" for anything else
const platformTable = [];
for (const os of ['windows', 'macos', 'linux', 'android', 'ios', 'other']) {
    platformTable.push([os, os], ['browser-' + os, 'browser-' + os], [os.toUpperCase(), os], ['BROWSER-' + os.toUpperCase(), 'browser-' + os], ['Browser-' + os.charAt(0).toUpperCase() + os.slice(1), 'browser-' + os], [os.charAt(0).toUpperCase() + os.slice(1), os]);
    platformTable.push([os + ' ', ''], [' ' + os, ''], [os + '\n', ''], ['\n' + os, ''], [os + '\0', ''], [os + 'x', ''], ['x' + os, ''], ['browser' + os, ''], ['browser-' + os + '-', ''], ['browser--' + os, ''], ['browser_' + os, ''], ['browser ' + os, ''], ['browser-browser-' + os, ''], [os + ',' + os, ''], [os + ';ls', ''], [os + '&x=1', ''], [os + ' --name x', '']);
}
platformTable.push(
    ['', ''], [null, ''], [undefined, ''], [42, ''], [{}, ''], [['linux'], ''], [{ toString() { return 'linux'; } }, ''],
    ['browser', ''], ['browser-', ''], ['win', ''], ['windows10', ''], ['window', ''], ['mac', ''], ['macosx', ''], ['darwin', ''], ['ubuntu', ''], ['chromeos', ''], ['unknown', ''], ['--platform', ''], ['constructor', ''], ['__proto__', ''],
    ['freebsd', ''], ['openbsd', ''], ['plan9', ''], ['win32', ''], ['ipados', ''], ['iphone', ''], ['android-x86', ''], ['browser-chromeos', ''], ['browser-unknown', ''], ['browser-freebsd', ''], ['x11', ''], ['linux2', ''],
    ['İOS', ''], ['ＩＯＳ', ''], ['ıos', ''], ['windows'.repeat(2000), ''],
);

let failed = 0;
const run = (label, fn, rows) => {
    for (const [value, want] of rows) {
        let got;
        try { got = fn(value); } catch (e) { got = 'threw ' + e.message; }
        if (got !== want) { console.log('FAIL ' + label + '(' + JSON.stringify(value).slice(0, 40) + ') = ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want)); failed++; }
    }
};
let functionCount = 0;
for (const [path, begin, end, names] of [[process.argv[2], 'ANTS_FILL_BEGIN', 'ANTS_FILL_END', ['antsFillArg', 'antsFillPlanArg', 'antsTeamsArg', 'antsStartArg', 'antsRoomMapArg', 'antsRoomSeatsArg', 'antsRoomLeaderStartArg', 'antsPlatformArg']], [process.argv[3], 'FILL_BEGIN', 'FILL_END', ['validFill', 'validFillPlan']]]) {
    if (!path) { console.log('usage: web_fill_check.js shell.html lobby.html'); process.exit(2); }
    let fns;
    try { fns = extract(path, begin, end, names); } catch (e) { console.log('FAIL ' + e.message); failed++; continue; }
    functionCount += names.length;
    // (the first of the names is the one level; its table has a few more rows with lists, which are no level)
    run(names[0], fns[names[0]], table.concat([['none,none,easy,hard', ''], ['easy,hard', ''], ['easy,easy,easy,easy', '']]));
    run(names[1], fns[names[1]], planTable);
    if (names[2]) run(names[2], fns[names[2]], teamsTable);
    if (names[3]) run(names[3], fns[names[3]], startTable);
    if (names[4]) run(names[4], fns[names[4]], roomMapTable);
    if (names[5]) run(names[5], fns[names[5]], roomSeatsTable);
    if (names[6]) run(names[6], fns[names[6]], roomLeaderStartTable);
    if (names[7]) run(names[7], fns[names[7]], platformTable);
    // (the room's teams are read by the one function of ?teams=: the same table, whatever the parameter is called)
}
// The meeting of ?fill= and the mouse-wheel zoom in one page (web/shell.html carries both): the page cancels the wheel and Safari's pinch over the game's CANVAS only (nothing on window,
// document or body, so the title, the selector and the guide scroll as usual) and the game's own wheel handler has the page to itself; web/four.html (the games' frames) has no wheel
// handler of its own, the outer page scrolls as usual. The setup screen's chat input is drawn on the canvas by the game: nothing of the page's is added for it.
for (const [path, isShell] of [[process.argv[2], true], [process.argv[3], false]]) {
    const text = fs.readFileSync(path, 'utf8');
    const receivers = [...text.matchAll(/([A-Za-z_$][\w$.]*)\.addEventListener\(\s*['"](wheel|mousewheel|DOMMouseScroll|gesturestart|gesturechange|gestureend)['"]/g)].map((m) => m[1] + ':' + m[2]);
    const onattr = /\bonwheel\b|\bonmousewheel\b/.test(text);
    if (isShell) {
        // (the one other receiver is the fullscreen STAGE's wheel: the black bars around the picture of a screen of another shape; it cancels the wheel only in fullscreen and only
        // outside the picture's box, so the page scrolls as usual otherwise: web_edge_check.py checks it in a real browser)
        const wrong = receivers.filter((r) => !r.startsWith('canvas:') && r !== 'stageElement:wheel');
        const kinds = new Set(receivers.map((r) => r.split(':')[1]));
        if (wrong.length || onattr || !kinds.has('wheel') || !kinds.has('gesturestart') || !kinds.has('gesturechange') || !kinds.has('gestureend')) {
            console.log('FAIL ' + path + ': wheel / pinch listeners must all be on the canvas (and the stage\'s, in fullscreen over a bar), wheel and the three gesture events: ' + JSON.stringify(receivers) + (onattr ? ' (and an on-wheel attribute)' : ''));
            failed++;
        }
        const stageWheel = /stageElement\.addEventListener\('wheel',[^\n]*/.exec(text);
        if (receivers.indexOf('stageElement:wheel') !== -1 && !(stageWheel && /isFullscreen\(\)/.test(stageWheel[0]) && /!boxElement\.contains\(e\.target\)/.test(stageWheel[0]))) {
            console.log('FAIL ' + path + ': the stage\'s wheel listener must act in fullscreen only, and only outside the picture\'s box');
            failed++;
        }
        // the middle button (back to the zoom 1): its press and its click are cancelled on the canvas, and only there, and only for button 1 (an unprevented press over a page that scrolls starts
        // the browser's autoscroll on Windows); web_aspect_check.py --wheel checks it in a real browser
        const auxclicks = [...text.matchAll(/([A-Za-z_$][\w$.]*)\.addEventListener\(\s*['"]auxclick['"][^\n]*button === 1/g)].map((m) => m[1]);
        const anyAux = [...text.matchAll(/([A-Za-z_$][\w$.]*)\.addEventListener\(\s*['"]auxclick['"]/g)].map((m) => m[1]);
        const downs = [...text.matchAll(/([A-Za-z_$][\w$.]*)\.addEventListener\(\s*['"]mousedown['"][^\n]*button === 1/g)].map((m) => m[1]);
        if (auxclicks.join() !== 'canvas' || anyAux.join() !== 'canvas' || downs.join() !== 'canvas') {
            console.log('FAIL ' + path + ': the middle button must be cancelled on the canvas only (button === 1): mousedown ' + JSON.stringify(downs) + ', auxclick ' + JSON.stringify(anyAux));
            failed++;
        }
        const joinsWithFill = /joinArguments/.test(text) && /--fill-bots/.test(text);
        if (!joinsWithFill) { console.log('FAIL ' + path + ': the page does not pass ?fill= on as --fill-bots'); failed++; }
    } else if (receivers.length || onattr || /\bauxclick\b/.test(text)) {
        console.log('FAIL ' + path + ': the games\' page must not handle the wheel or the middle button itself: ' + JSON.stringify(receivers));
        failed++;
    }
}

console.log(failed === 0 ? 'ok: ' + (table.length + planTable.length + teamsTable.length + startTable.length + roomMapTable.length + roomSeatsTable.length + roomLeaderStartTable.length + platformTable.length) + ' values, ' + functionCount + ' functions, the wheel handlers of the pages' : failed + ' rows differ');
process.exit(failed === 0 ? 0 : 1);

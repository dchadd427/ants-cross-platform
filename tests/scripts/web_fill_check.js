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

console.log(failed === 0 ? 'ok: ' + table.length + ' values, two functions, the wheel handlers of the pages' : failed + ' rows differ');
process.exit(failed === 0 ? 0 : 1);

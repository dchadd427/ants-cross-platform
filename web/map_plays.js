/* How often a map was played here, on its page (/map/NAME; docs/MAP_PAGES.md). The page is made when the image is built and cannot know it, so the page asks the site's list of maps (GET /maps, one answer for everybody, kept for five seconds)
   when it opens: the "Played here" fact and the sentence "It has been played N times on this site: see its matches." appear with the number of this map's file. With no answer (no list, a list that is not that, no entry for the file,
   a number that is not a whole number from 0 up) nothing changes: the fact stays hidden and the sentence is the one that the page came with. The number is put in as text. Plain ES5, no globals. */
(function () {
    'use strict';
    var d = document, line = d.getElementById('plays-line'), fact = d.getElementById('plays-fact'), cell = d.getElementById('plays-count');
    if (!line || !fact || !cell || typeof fetch !== 'function') return;
    var file = (line.getAttribute('data-file') || '').toLowerCase(), matches = line.getAttribute('data-matches') || '';
    if (!file) return;
    function show(n) {
        cell.textContent = n ? n + (n === 1 ? ' match' : ' matches') : 'Not yet';
        fact.hidden = false;
        if (!n) { line.textContent = 'It has not been played on this site yet: be the first.'; return; }
        line.textContent = 'It has been played ' + (n === 1 ? 'once' : n + ' times') + ' on this site: ';
        if (matches.indexOf('/matches') === 0) {
            var a = d.createElement('a');
            a.href = matches;
            a.textContent = 'see its matches';
            line.appendChild(a);
            line.appendChild(d.createTextNode('.'));
        } else line.appendChild(d.createTextNode('see its matches.'));
    }
    function whole(n) { return typeof n === 'number' && isFinite(n) && n >= 0 && n === Math.floor(n) && n <= 1e9; }
    fetch('/maps', { credentials: 'omit' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (answer) {
        if (!answer || !Array.isArray(answer.maps)) return;
        for (var i = 0; i < answer.maps.length; i++) {
            var m = answer.maps[i];
            if (m && typeof m.file === 'string' && m.file.toLowerCase() === file) { if (whole(m.plays)) show(m.plays); return; }
        }
    }).catch(function () { /* no answer: the page stays as it came */ });
}());

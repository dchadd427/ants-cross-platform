// The checks card of the map maker (docs/MAP_EDITOR.md): the answer of the map core's `checkMap` for a map, as the approved picture draws it: the kind, the headline, the neutral badge
// "New version only" of an Extended map, and the findings in the checker's own sentences under the three marks (stops the map, warning, good to know). A sentence can hold text of the
// file, so everything is put in with textContent. A plain ES5 script that a page and node can load.
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MapChecks = factory();
})(this, function () {
    'use strict';

    var MARKS = { fatal: ['Stops the map', 'fatal'], warning: ['Warning', 'warning'], note: ['Good to know', 'info'] };
    var NOT_A_FAULT = 'This is not a fault. To use the map in the original game, make a Classic copy if it fits.';
    var ALL_GOOD = 'Every team has a hill and starting ants. Nothing to fix.';

    function capital(text) { return text.charAt(0).toUpperCase() + text.slice(1); }
    function plain(report) { return report && report.judged !== false; }
    function stopsOrWarns(f) { return f.severity === 'fatal' || f.severity === 'warning'; }

    // The words of the link that shows where a finding is ("Show me: row 12, column 40"), or null when the finding is about the whole map
    function showText(f) {
        if (typeof f.row !== 'number' || f.row < 0) return null;
        return 'Show me: row ' + f.row + (typeof f.column === 'number' && f.column >= 0 ? ', column ' + f.column : '');
    }

    // How a map stands, for the small line of the side panel: {works, text}
    function summary(report) { return { works: !!(report && report.works), text: report && report.headline ? report.headline : 'Not checked' }; }

    // The card. `doc` is the document; `handlers.onShow(row, column)` is called by a "Show me" link. Returns the element.
    function render(doc, report, handlers) {
        function el(tag, cls, text) { var n = doc.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; }
        var card = el('div', 'ccard'), kind = report.kind === 'extended' ? 'extended' : 'classic';
        var cap = el('div', 'cap');
        cap.appendChild(el('span', '', 'Checks'));
        cap.appendChild(el('span', 'tag ' + (kind === 'extended' ? 'tag-ext' : 'tag-classic'), kind === 'extended' ? 'Extended map' : 'Classic map'));
        card.appendChild(cap);
        var hl = el('h3', 'hl' + (report.works ? '' : ' no'));
        hl.appendChild(el('span', 'mk', report.works ? '✓' : '!')).setAttribute('aria-hidden', 'true');
        hl.appendChild(doc.createTextNode(report.headline || 'Not checked'));
        card.appendChild(hl);
        if (report.badge) { var badges = el('div', 'badges'); badges.appendChild(el('span', 'badge-nv', report.badge)); card.appendChild(badges); }
        var findings = report.findings || [];
        if (plain(report) && !findings.some(stopsOrWarns)) card.appendChild(el('p', 'clean', ALL_GOOD));
        if (findings.length) {
            var list = el('ul', 'finds');
            findings.forEach(function (f) {
                var mark = MARKS[f.severity] || MARKS.note, li = el('li');
                li.appendChild(el('span', 'ico ' + mark[1])).setAttribute('aria-hidden', 'true');
                var body = el('div');
                body.appendChild(el('div', 'sev ' + mark[1], mark[0]));
                body.appendChild(el('div', 'msg', capital(f.message)));
                var show = showText(f);
                if (show && handlers && handlers.onShow) {
                    var go = el('button', 'go', show);
                    go.type = 'button';
                    go.addEventListener('click', function () { handlers.onShow(f.row, f.column); });
                    body.appendChild(go);
                }
                li.appendChild(body);
                list.appendChild(li);
            });
            card.appendChild(list);
        }
        if (kind === 'extended' && report.works) card.appendChild(el('p', 'foot', NOT_A_FAULT));
        return card;
    }

    return { render: render, summary: summary, showText: showText, capital: capital, MARKS: MARKS, NOT_A_FAULT: NOT_A_FAULT, ALL_GOOD: ALL_GOOD };
});

/* The "Map maker" link of the lobby's header (next to the Matches button). It is added here, and not in web/lobby.html, because that page is at its line limit
   (tools/check_hygiene.py). Tested by tests/scripts/test_web_map_maker_link.py. ES5, no globals. */
(function () {
    'use strict';
    var head = document.querySelector('header.mast'), matches = head && head.querySelector('#watch-top');
    if (!matches || head.querySelector('#mapmaker-top')) return;            // (no Matches button: nothing to sit beside; already there: never twice)
    var link = document.createElement('a');
    link.className = 'btn sm';
    link.id = 'mapmaker-top';
    link.href = '/mapmaker.html';
    link.title = 'Map maker: draw your own map';
    link.textContent = 'Map maker';
    head.insertBefore(link, matches);
    var style = document.createElement('style');
    /* Desktop: the pair sits at the right, 10 px apart (the row's gap is 22 px). Phone (the page's own 720 px rule): the grid gets a column for each button, both on the logo's row, the tagline under them.
       The 44 px target: the button keeps the look of Matches (38 px tall) and a hidden layer around it adds the rest (the layer is measured from the padding box, inside the 2 px border: 34 + 12 = 46 px). Under 390 px the logo, gaps and padding shrink so that both fit at 320 px. */
    style.textContent =
        '.mast #mapmaker-top { margin-left: auto; margin-right: -12px; flex: 0 0 auto; position: relative; }' +
        '.mast #mapmaker-top + #watch-top { margin-left: 0; }' +
        '.mast #discord-top { margin-left: -12px; flex: 0 0 auto; position: relative; display: inline-flex; align-items: center; gap: 7px; }' +
        '.mast #discord-top::before { content: ""; position: absolute; left: -4px; right: -4px; top: -6px; bottom: -6px; }' +
        '.mast #mapmaker-top::before { content: ""; position: absolute; left: -4px; right: -4px; top: -6px; bottom: -6px; }' +
        '.mast #mapmaker-top:focus-visible { outline: 3px solid var(--gold, #f2c14e); outline-offset: 3px; }' +
        '@media (max-width: 720px) {' +
        '.mast { grid-template-columns: auto minmax(0, 1fr) auto auto; grid-template-areas: "logo . map btn" "text text text disc"; column-gap: 10px; }' +
        '.mast #discord-top { grid-area: disc; justify-self: end; align-self: start; margin-left: 0; min-width: 44px; justify-content: center; padding: 6px 11px; }' +
        '.mast #discord-top .dt { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); }' +
        '.mast #mapmaker-top { grid-area: map; justify-self: end; margin-right: 0; }' +
        '}' +
        '@media (max-width: 389px) {' +
        '.mast { column-gap: 6px; } .mast img { width: 88px; }' +
        '.mast #mapmaker-top, .mast #watch-top { padding-left: 9px; padding-right: 9px; font-size: 14px; }' +
        '}';
    document.head.appendChild(style);
}());

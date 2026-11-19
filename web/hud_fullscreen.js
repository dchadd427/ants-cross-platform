// The HUD's Fullscreen button, the small button right of Quit on the match screen (docs/VIEW_AND_HUD.md, "The Fullscreen button"; src/ants_app/hud_windows.cpp).
// The game draws the button and shows its press, but a browser grants fullscreen only inside the handler of a real click, so the page does the fullscreen. It listens for the click on the stage and asks the
// game whether the pointer was on the button; the page's own Fullscreen control and this button then agree because both go through the page's one enter / leave, and the page tells the game whenever the
// page is fullscreen (the browser's own and the page's own alike, by this button, by the bar's control, by Esc), so that the button shows the picture that leaves.
//   * ask(75): 1 while the game has the pointer on the button or holding it; ask(76): how many times the game's button has acted (a tap may be over before the click arrives); both -1 while no click on the
//     button would act (Module._ants_probe of src/ants_app/application.cpp; the page's own `ask` answers -1 for a game that has none). The pointer is the game's own, not the click's coordinates: a
//     fullscreen page holds the pointer lock and its clicks have none.
//   * the game is told by tell(on) (Module._ants_set_page_fullscreen) when the state changes and every poll (250 ms) after the first look, so that a game that starts later is told too.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsHudFullscreen, node gets module.exports (tests/scripts/web_hud_fullscreen_check.js runs it with fakes).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHudFullscreen = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var POLL_MS = 250;

    // page: { stage, ask(what), ready(), isFullscreen(), enter(), leave(), tell(on), every(fn, ms) (setInterval by default) }
    function forPage(page) {
        var told = null;                                             // what the game was last told (null: nothing yet)
        var clicks = null;                                           // the game's count of the button's acts at the last look

        function sync() {
            var on = !!page.isFullscreen();
            if (on !== told) {
                told = on;
                page.tell(on);
            }
            var n = page.ask(76);
            clicks = n >= 0 ? n : null;
        }

        function onClick(event) {
            if (event && event.button !== undefined && event.button !== 0) return;
            if (!page.ready()) return;
            var n = page.ask(76);
            var acted = n >= 0 && clicks !== null && n !== clicks;   // the game has taken the press and the release already
            if (!(page.ask(75) === 1 || acted)) return;              // not the game's button: the click is the game's own
            if (page.isFullscreen()) page.leave(); else page.enter();
            sync();
        }

        page.stage.addEventListener('click', onClick, true);
        (page.every || function (fn, ms) { return setInterval(fn, ms); })(sync, POLL_MS);
        sync();
        return { sync: sync, onClick: onClick };
    }

    return { forPage: forPage, POLL_MS: POLL_MS };
});

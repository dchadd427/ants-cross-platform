// The countdown's Fullscreen button (network protocol 21, docs/PLAY_IN_BROWSER.md "The countdown", docs/VIEW_AND_HUD.md "The countdown card").
// A match of a room with two or more people opens with a countdown card of the game (30 s, the chat open). The card leaves a place for a Fullscreen button, and the page lays a real button over it so that
// the players can go fullscreen before the match starts (a browser grants fullscreen only to the click of a real button; the way here, a page that went to this page, has no click to give).
//   * The game says where it is: ask(60) is the seconds that the countdown has left (-1: no card is up), ask(61) to ask(64) the place of the button in the canvas's pixels, ask(40) and ask(41) the size of
//     the game's picture (Module._ants_probe of src/ants_app/application.cpp; the page's own `ask` answers -1 for a game that has none).
//   * The button is shown while the card is up, the game is ready and the page is not fullscreen already (the browser's own and the page's own alike: both leave by Esc or the x), at least 120 x 44 CSS
//     pixels (a finger), centred on the game's place and kept inside the picture. It is a child of the stage, not of the picture's box: it is not the game's touch, and it goes with the stage into the
//     browser's fullscreen (where it is hidden: the stage is fullscreen already). Its click is the Fullscreen button of the header's: the page's own enterFullscreen.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsCountdown, node gets module.exports (tests/scripts/web_countdown_check.js runs it with fakes).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsCountdown = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var POLL_MS = 250;
    var MIN_W = 120;                                                 // CSS pixels: the least that a finger hits
    var MIN_H = 44;

    // page: { doc, stage, canvas, ask(what), ready(), isFullscreen(), enter(), every(fn, ms) (setInterval by default) }
    function forPage(page) {
        var doc = page.doc;
        var button = doc.createElement('button');
        button.type = 'button';
        button.id = 'countdown-fs';
        button.className = 'btn';
        button.title = 'Go fullscreen before the match starts';
        button.textContent = 'Fullscreen';
        button.hidden = true;
        button.style.display = 'none';
        button.style.position = 'absolute';
        button.style.zIndex = '4';
        button.style.minWidth = MIN_W + 'px';
        button.style.whiteSpace = 'nowrap';
        page.stage.appendChild(button);

        function place() {                                           // over the game's place, in the stage's own coordinates
            var rect = page.canvas.getBoundingClientRect(), outer = page.stage.getBoundingClientRect();
            var logicalW = page.ask(40), logicalH = page.ask(41);
            if (!(rect.width > 0 && rect.height > 0 && logicalW > 0 && logicalH > 0)) return false;
            var fx = rect.width / logicalW, fy = rect.height / logicalH;
            var w = Math.max(MIN_W, page.ask(63) * fx), h = Math.max(MIN_H, page.ask(64) * fy);
            var left = rect.left - outer.left + (page.ask(61) + page.ask(63) / 2) * fx - w / 2;
            var top = rect.top - outer.top + (page.ask(62) + page.ask(64) / 2) * fy - h / 2;
            left = Math.max(rect.left - outer.left, Math.min(left, rect.left - outer.left + rect.width - w));
            top = Math.max(rect.top - outer.top, Math.min(top, rect.top - outer.top + rect.height - h));
            button.style.left = left + 'px';
            button.style.top = top + 'px';
            button.style.width = w + 'px';
            button.style.height = h + 'px';
            return true;
        }

        function refresh() {
            var show = !!page.ready() && page.ask(60) >= 0 && !page.isFullscreen();
            if (show && !place()) show = false;
            button.hidden = !show;
            button.style.display = show ? '' : 'none';
        }

        button.addEventListener('click', function () {
            if (!page.isFullscreen()) page.enter();
            refresh();
        });
        (page.every || function (fn, ms) { return setInterval(fn, ms); })(refresh, POLL_MS);
        return { button: button, refresh: refresh, place: place };
    }

    return { forPage: forPage, POLL_MS: POLL_MS, MIN_W: MIN_W, MIN_H: MIN_H };
});

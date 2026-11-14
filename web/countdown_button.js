// The countdown's Fullscreen button (network protocol 21, docs/PLAY_IN_BROWSER.md "The countdown", docs/VIEW_AND_HUD.md "The countdown card").
// A match of a room with two or more people opens with a countdown card of the game (30 s, the chat open). The card leaves a place for a Fullscreen button, and the page lays a real button over it so that
// the players can go fullscreen before the match starts (a browser grants fullscreen only to the click of a real button; the way here, a page that went to this page, has no click to give).
//   * The game says where it is: ask(60) is the seconds that the countdown has left (-1: no card is up), ask(61) to ask(64) the place of the button in the canvas's pixels, ask(40) and ask(41) the size of
//     the game's picture (Module._ants_probe of src/ants_app/application.cpp; the page's own `ask` answers -1 for a game that has none).
//   * The button is shown while the card is up, the game is ready and the page is not fullscreen already (the browser's own and the page's own alike: both leave by Esc or the x), the size of
//     that place scaled with the picture (150 x 32 game pixels of the card's own button, so it fits the card on a phone held sideways: about 92 x 28 CSS pixels on a card 240 wide), the height never
//     less than 28 CSS pixels (the finger floor on a phone) and the width never less than 80 (the word fits on one line), never wider than the card (400 game pixels); its text 0.55 x the height (12 to
//     20 px) on one line; centred on the game's place, then kept inside the card (its bottom at least 2 px above the card's bottom edge, 8 game pixels under the place; its top not above the card's
//     hint line, 4 game pixels over the place; the card wins when both cannot hold) and inside the picture. It is a child of the stage, not of the picture's box: it is not the game's touch, and it goes with the stage into the
//     browser's fullscreen (where it is hidden: the stage is fullscreen already). Its click is the Fullscreen button of the header's: the page's own enterFullscreen.
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsCountdown, node gets module.exports (tests/scripts/web_countdown_check.js runs it with fakes).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsCountdown = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var POLL_MS = 250;
    var MIN_H = 28;                                                  // CSS pixels: the least height that a finger hits on a phone (32 would reach past the card's bottom edge)
    var MIN_W = 80;                                                  // CSS pixels: the least width that holds "Fullscreen" on one line
    var CARD_BELOW = 8;                                              // game pixels from the place's bottom to the card's bottom edge (card 346 high, place at 306, 32 high)
    var HINT_GAP = 4;                                                // game pixels from the hint line's bottom to the place's top
    var CARD_W = 400;                                                // the card's width in game pixels (include/ants_app/countdown_card.hpp): the button is never wider

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
        button.style.boxSizing = 'border-box';                       // the box below is the whole button: the page's .btn minimums, padding and font size are set aside
        button.style.minWidth = '0px';
        button.style.minHeight = '0px';
        button.style.padding = '0px 4px';
        button.style.gap = '0px';
        button.style.whiteSpace = 'nowrap';
        button.style.overflow = 'hidden';
        button.style.lineHeight = '1';
        page.stage.appendChild(button);

        function place() {                                           // over the game's place, in the stage's own coordinates
            var rect = page.canvas.getBoundingClientRect(), outer = page.stage.getBoundingClientRect();
            var logicalW = page.ask(40), logicalH = page.ask(41);
            if (!(rect.width > 0 && rect.height > 0 && logicalW > 0 && logicalH > 0)) return false;
            var fx = rect.width / logicalW, fy = rect.height / logicalH;
            var slotW = page.ask(63) * fx, slotH = page.ask(64) * fy;
            if (!(slotW > 0 && slotH > 0)) return false;
            var h = Math.max(MIN_H, slotH), w = Math.max(MIN_W, slotW);   // the place's size; a floor on one side does not widen the other
            w = Math.min(w, CARD_W * fx, rect.width);
            h = Math.min(h, rect.height);
            var left = rect.left - outer.left + (page.ask(61) + page.ask(63) / 2) * fx - w / 2;
            var top = rect.top - outer.top + (page.ask(62) + page.ask(64) / 2) * fy - h / 2;
            left = Math.max(rect.left - outer.left, Math.min(left, rect.left - outer.left + rect.width - w));
            var slotTop = rect.top - outer.top + page.ask(62) * fy, slotBottom = slotTop + slotH;
            top = Math.max(top, slotTop - HINT_GAP * fy);                                  // not up into the hint line
            top = Math.min(top, slotBottom + CARD_BELOW * fy - 2 - h);                     // and inside the card's bottom edge, which wins
            top = Math.max(rect.top - outer.top, Math.min(top, rect.top - outer.top + rect.height - h));
            button.style.left = left + 'px';
            button.style.top = top + 'px';
            button.style.width = w + 'px';
            button.style.height = h + 'px';
            button.style.fontSize = Math.max(12, Math.min(20, Math.round(h * 0.55 * 10) / 10)) + 'px';
            var border = h >= 40 ? 2 : 1;                            // the frame, the corners and the shadow shrink with the button (the page's .btn: 2 px, 8 px, 3 x 4 px at 44 px)
            button.style.borderWidth = border + 'px';
            button.style.borderRadius = Math.max(4, Math.min(8, Math.round(h * 0.18))) + 'px';
            button.style.boxShadow = 'var(--bevel), ' + (h >= 40 ? '3px 4px' : '1px 2px') + ' 0 var(--shadow)';
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

    return { forPage: forPage, POLL_MS: POLL_MS, MIN_H: MIN_H, MIN_W: MIN_W, CARD_W: CARD_W };
});

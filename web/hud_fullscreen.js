// The HUD's Fullscreen button, the small button right of Quit on the match screen (docs/VIEW_AND_HUD.md, "The Fullscreen button"; src/ants_app/hud_windows.cpp).
// The game draws the button and shows its press, but a browser grants fullscreen only inside the handler of a real click or tap, so the page does the fullscreen. It listens for the click on the stage and asks the
// game whether the pointer was on the button; the page's own Fullscreen control and this button then agree because both go through the page's one enter / leave, and the page tells the game whenever the
// page is fullscreen (the browser's own and the page's own alike, by this button, by the bar's control, by Esc), so that the button shows the picture that leaves.
//   * ask(75): 1 while the game has the pointer on the button or holding it; ask(76): how many times the game's button has acted (a tap may be over before the click arrives); both -1 while no click on the
//     button would act (Module._ants_probe of src/ants_app/application.cpp; the page's own `ask` answers -1 for a game that has none). The pointer is the game's own, not the click's coordinates: a
//     fullscreen page holds the pointer lock and its clicks have none.
//   * a FINGER sends no click: the game's touch handlers cancel the touch's default action (Emscripten does, for a touch that its callback took), and a cancelled touch has no click. So a tap is read from
//     the touch events: one finger, up within 400 ms and 10 px of where it came down, down and up on the button's place (ask(70) - ask(73), the game's own rectangle in the picture's pixels, with ask(40) and
//     ask(41) for the canvas's size): that is a click on the button, made inside the touchend, which every browser takes as the gesture. A click that a browser sends after a finger has lifted (a tap in a black
//     bar of the fullscreen is outside the canvas, so the game does not take it and the browser does) belongs to the finger and is ignored: the game's pointer rests where the last tap left it, perhaps on the button.
//   * a mouse click acts only when its press began on the button too (the game's pointer on it at the mouse down): a box that is dragged over the button and let go there is not a click on it.
//   * the game is told by tell(on) (Module._ants_set_page_fullscreen) when the state changes and every poll (250 ms) after the first look, so that a game that starts later is told too; until ready() says that
//     the game runs, nothing is asked and nothing is told (a call of the game's exports before the program is up leaves them unusable for the life of the page: every later probe answers -1).
//   * ES5 and no module system, as the site's other scripts: a page gets the global AntsHudFullscreen, node gets module.exports (tests/scripts/web_hud_fullscreen_check.js runs it with fakes).
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHudFullscreen = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var POLL_MS = 250;
    var TAP_MS = 400;                                                // a finger that is up within this, and within TAP_SLOP, is a tap (the game's own: touch_control.hpp)
    var TAP_SLOP = 10;                                               // CSS pixels
    var CLICK_AFTER_TOUCH_MS = 700;                                  // a click that a browser sends after a finger has lifted belongs to that finger, whose tap is judged by its touches alone

    // page: { stage, rect() (the canvas's box in CSS pixels), ask(what), ready(), isFullscreen(), enter(), leave(), tell(on), every(fn, ms) (setInterval by default) }
    function forPage(page) {
        var told = null;                                             // what the game was last told (null: nothing yet)
        var clicks = null;                                           // the game's count of the button's acts at the last look
        var finger = null;                                           // the one finger that is down: { x, y, at }
        var touchAt = null;                                          // when a finger last lifted (the time stamp of its touchend)
        var pressed = null;                                          // whether the game had its pointer on the button when the mouse's button went down (null: no press seen)

        function sync() {
            if (!page.ready()) return;                               // the game's functions are lazy stubs until it runs: the first call of one before that breaks it for good, so nothing is asked or told yet
            var on = !!page.isFullscreen();
            if (on !== told) {
                told = on;
                page.tell(on);
            }
            var n = page.ask(76);
            clicks = n >= 0 ? n : null;
        }

        function toggle() {
            if (page.isFullscreen()) page.leave(); else page.enter();
            sync();
        }

        function onMouseDown(event) {
            if (event && event.button !== undefined && event.button !== 0) return;
            pressed = page.ready() ? page.ask(75) === 1 : null;
        }

        function onClick(event) {
            var down = pressed;
            pressed = null;
            if (event && event.button !== undefined && event.button !== 0) return;
            if (!page.ready()) return;
            // A finger's click (a tap outside the picture, in a bar of the fullscreen, is no touch of the game's, so a browser sends it) finds the game's pointer where the last tap left it, perhaps on the button.
            if (touchAt !== null && event && typeof event.timeStamp === 'number' && event.timeStamp - touchAt >= 0 && event.timeStamp - touchAt < CLICK_AFTER_TOUCH_MS) return;
            var n = page.ask(76);
            var acted = n >= 0 && clicks !== null && n !== clicks;   // the game has taken the press and the release already
            if (!(page.ask(75) === 1 || acted)) return;              // not the game's button: the click is the game's own
            if (down === false && !acted) return;                    // the press began elsewhere (a box that is dragged over the button): the game does not act on it, nor does the page
            toggle();
        }

        // Is a point of the page (CSS pixels) on the button's place? The rectangle is the game's (-1 while no press on it would act); its pixels are the canvas's logical ones.
        function onButton(clientX, clientY) {
            var x = page.ask(70), y = page.ask(71), w = page.ask(72), h = page.ask(73), cw = page.ask(40), ch = page.ask(41);
            if (!(x >= 0 && y >= 0 && w > 0 && h > 0 && cw > 0 && ch > 0)) return false;
            var r = page.rect();
            if (!r || !(r.width > 0 && r.height > 0)) return false;
            var gx = (clientX - r.left) * cw / r.width, gy = (clientY - r.top) * ch / r.height;
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        }

        function onTouchStart(event) {
            finger = null;                                           // (a second finger landing ends the first one's tap: a pinch is no press)
            if (!event.touches || event.touches.length !== 1 || !event.changedTouches || event.changedTouches.length === 0) return;
            var t = event.changedTouches[0];
            finger = { x: t.clientX, y: t.clientY, at: event.timeStamp };
        }

        function onTouchEnd(event) {
            var f = finger;
            finger = null;
            if (typeof event.timeStamp === 'number') touchAt = event.timeStamp;
            if (!f || !page.ready() || !event.changedTouches || event.changedTouches.length === 0) return;
            var t = event.changedTouches[0];
            if (event.timeStamp - f.at > TAP_MS) return;
            if (Math.abs(t.clientX - f.x) > TAP_SLOP || Math.abs(t.clientY - f.y) > TAP_SLOP) return;
            if (!onButton(f.x, f.y) || !onButton(t.clientX, t.clientY)) return;
            toggle();
        }

        function onTouchCancel() { finger = null; }

        page.stage.addEventListener('mousedown', onMouseDown, true);
        page.stage.addEventListener('click', onClick, true);
        page.stage.addEventListener('touchstart', onTouchStart, true);
        page.stage.addEventListener('touchend', onTouchEnd, true);
        page.stage.addEventListener('touchcancel', onTouchCancel, true);
        (page.every || function (fn, ms) { return setInterval(fn, ms); })(sync, POLL_MS);
        sync();
        return { sync: sync, onClick: onClick, onMouseDown: onMouseDown, onTouchStart: onTouchStart, onTouchEnd: onTouchEnd };
    }

    return { forPage: forPage, POLL_MS: POLL_MS, TAP_MS: TAP_MS, TAP_SLOP: TAP_SLOP };
});

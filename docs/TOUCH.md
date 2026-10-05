# Touch controls

A touch screen (a phone, a tablet, a laptop's) plays the whole game. Nothing here is a command that a mouse cannot give: a pure model (`include/ants_app/touch_control.hpp`, no SDL) turns fingers into the
mouse's events, a pan and a zoom; the game runs them through the code that the mouse uses (`src/ants_app/application_touch.cpp`); the simulation, the network and every hash never see any of it. The
guide on the game page (Touch & Mobile) says it to the player. A touch screen on a desktop (Windows, Linux, a laptop's) takes the same path; a mouse, a trackpad and a pen are unchanged.

## The gestures, and why these numbers

| Fingers | Gesture | Is | Constant |
|---|---|---|---|
| 1, on the map view | **Tap** | a left click at the point where the finger WENT DOWN (a shaking finger is never a tiny rubber band) | up before 400 ms (Android's long-press time) |
| 1, on the map view | **Hold** | a right click: pressed where the finger went down when the time is up, released at the lift (the game acts at the release, as for the mouse) | 450 ms; a ring closes around the finger from 150 ms, a pulse and a buzz (where the browser has one) at 450 |
| 1, on the map view | **Drag** | a left drag: the press at the origin first, then the finger (the rubber band) | the finger leaves the slop |
| 1, on the minimap, a button, a dialog, any other screen | **Touch** | the press at once, as a mouse (nothing to wait for); a finger that rests on the minimap turns it into a right click there ("send the selected ants there") | 450 ms |
| 2, on the map view | **Pan and pinch** | the map follows the middle point; the fingers' distance picks a level of the wheel's list, towards the middle point | hysteresis 0.15 of a step, a span under 3 slops counts as 3 (at the start and afterwards: fingers that touch zoom nothing) |

A lift between 400 and 450 ms is neither a click nor a right click (the finger lingered). Two fingers act only where the wheel zooms: the map view of a match, no dialog, no results, not the catch-up
screen of a network match (a finger there is a plain press that the screen swallows: no ring, no buzz), no press held but the first finger's own on the map (the second finger ends that one with no selection and no order). Elsewhere a second finger does nothing and a tap is still a click. A third finger, and the finger that
stays after the other lifts, are ignored until they lift, and a finger that lands meanwhile starts nothing: BY DESIGN, so that taking a pinch up again with one finger is not a tap, a hold or a drag.
A first finger whose press held nothing (the frame, a blank part of the panel, where no control took it) is not one of these: the next finger takes its place at once (that press ends with no act, and the
finger is ignored until it lifts), so a thumb that rests on the panel never blocks the taps on the map. The pair is judged once per frame (a pinch's two moves arrive as two events, and between them the distance is wrong by a whole step).

**A stall cannot make a touch long.** SDL stamps an event when it sees it (the browser's dispatch, the game's read of the queue), so a stall makes the stamp late. An event is judged no later than 100 ms
(`touch::kStallMs`) after the last frame (or the first finger's arrival): a stalled 100 ms tap is still a click, a stalled first move still a drag. A frame knows that the finger is down, so a hold fires
from it at its own time at any frame rate; a lift after the hold's time is a hold only if a frame saw the finger down within 100 ms before that time (every frame does at 10 per second or more; at 4 a
lift just after the time may count as a tap).

**The slop** (how far a finger may wobble and still be a tap or a hold) is a size on the glass: 8 CSS pixels of the game box (Android's own touch slop is 8 dp), never under 6 device pixels, in picture
pixels (`touch::slop_pixels`): in the browser the box's CSS size and the device ratio, on a desktop the window's size in points and the output's size in pixels. It is 18 - 20 picture pixels in a phone held
upright, 8 in a 960 pixel window. The ring (`touch_feedback.hpp`) is as big as the slop says: it closes from 8 to 5 slops of radius, just outside of a fingertip. The two times are named constants to
be tuned from the owner's phone (`touch::kTapMs`, `touch::kHoldMs`, and `touch::kSlopCssPx` for the wobble): a thumb that rests on the map for 0.45 s is a right click, which a phone may show as holds that were not meant.

## The page (`web/shell.html`, the block ANTS_TOUCH_BEGIN ... END)

SDL's Emscripten backend reads the canvas's touches itself, and SDL's own touch-to-mouse emulation is off (`SDL_HINT_TOUCH_MOUSE_EVENTS`, set at SDL's highest priority: an environment variable of the
player's cannot bring it back), so the page only keeps the browser out of the way:

- the canvas has `touch-action: none` and the page `touch-action: manipulation` (panning and pinch stay, double-tap zoom goes), every control outside the canvas too;
- `overscroll-behavior-y: none` on html and body (a pull down past the top of the page does not reload it), without stopping the page's own scrolling; only the vertical axis, so a desktop trackpad's
  horizontal swipe (back, forward) is as it was;
- no callout, selection or tap highlight over the game (`-webkit-touch-callout`, `user-select`), the context menu cancelled over the game's box;
- the browser's pinch (`gesturestart`, `gesturechange`, `gestureend`) cancelled on the canvas and, while a game finger is down, on the document; a trackpad's pinch still zooms the game through the wheel;
- a `touchcancel` tells the game (`Module._ants_touch_cancel`): no finger is tracked any more and a held press ends with no act (on a dialog or a screen it is let go of where no control is, so that
  the quit dialog's Yes, START and Leave never fire from a cancel); a lost focus, a hidden page and a change of the window's or the box's size (a rotation, a fullscreen toggle, the address bar: the same
  spot of the glass is another pixel of the picture then) do the same. The page's cancel waits behind the finger events that SDL already holds (a user event), so a touch that is cancelled in the frame that
  it began in is no tap;
- a touch that lands when the browser's own list (`touches`) says that no other touch of the game is down makes the page's count and the game's fingers start again: a lift that the browser never
  delivered cannot keep a finger "down" and make the game ignore every finger after it (SDL's own finger list could not tell: it is made of the same events; the game's cancel waits behind what SDL
  holds, so what came before the touch is still handled);
- the sound's unlock stays on `touchend` (iOS and Chrome on Android grant their permission at the END of a touch), so a hold that ends in a right click still unlocks it;
- `navigator.vibrate` is feature-detected and never required (Android's Chrome buzzes only after the page has had a first tap; iOS has none);
- the Fullscreen button is as before: the browser's fullscreen where there is the API, the page's own (with a button to leave it) where there is none (Safari on an iPhone).

Never relied on: an edge swipe (the system's back gesture on Android and iOS).

## What is verified, and what is not

Verified by tests: the model (`test_touch_model`, suite 3.25: every rule, a soak of random sessions, the feedback's geometry), the application (`test_touch_app`, suite 3.26: synthetic finger events through
the real event loop against the mouse's click, band and right click, point by point), the page's block run with fakes (`tests/scripts/web_touch_check.js`) and its style, script, guide and exports
(`tests/scripts/test_web_touch.py`), all in the quick tier. In a real browser (opt-in, `tests/scripts/test_web_touch.sh`, a running web image): a Pixel-like phone (touch, mobile metrics, device ratio
2.625, Android Chrome's user agent) and an iPhone-like one (390 x 844, ratio 3, the iPhone's user agent, no Fullscreen API, no vibration) drive the page with the DevTools protocol's multi-touch.

**Not verified, and to be checked on a phone** (headless Chromium is not a phone, and has no WebKit): that SDL's Emscripten backend delivers the touches as assumed (its coordinates, `touchcancel` as a lift);
a thumb that rests on the glass and holds by accident; the system's own gestures (the back swipe, pull to refresh); a finger's real jitter against the slop; the vibration motor; that the browser's `touches`
list and SDL's `SIZE_CHANGED` behave as the stale-finger rule and the size-change cancel assume; how often a stall of 150 - 450 ms happens on a phone (the stall rule's 100 ms); and, for iOS Safari,
every guard that exists for it: the non-passive gesture listeners (the browser's pinch), `touch-action` (honoured from iOS 13), the callout and selection rules, the unlock on `touchend`, the page's own
fullscreen on an iPhone and the missing vibration. They run in Chromium and break nothing there; whether they do what is meant on WebKit is only known on an iPhone or an iPad.

/* Mobile-first touch & pointer orbit controller for side-by-side comparison cards.
 *
 * Why a dedicated touch arbiter is required on mobile browsers (especially iOS Safari):
 * With `touch-action: pan-y` on a scrollable card grid, PointerEvents (`pointermove`)
 * are non-cancelable — any tiny vertical thumb drift fires `pointercancel` and aborts
 * the orbit after a few degrees. By handling TouchEvents with `{ passive: false }` on
 * `touchmove`, a horizontal/diagonal swipe locks immediately into `orbit` mode and calls
 * `preventDefault()` for the entire finger hold, while steep vertical swipes hand off
 * seamlessly to native 120Hz page scrolling. Mouse/pen input uses PointerEvents cleanly.
 */
"use strict";

(() => {
  const AXIS_LOCK_PX = 5;
  // Horizontal-or-diagonal threshold: |dx| >= |dy| * 0.65 (~57 deg cone) locks orbit.
  const ORBIT_SLOPE = 0.65;

  function findTouch(touchList, id) {
    for (let i = 0; i < touchList.length; i++) {
      if (touchList[i].identifier === id) return touchList[i];
    }
    return null;
  }

  function attachCardOrbit(el, opts) {
    if (!el || !opts) return { shouldSuppressClick: () => false };
    const matchFn = opts.matchFn || (() => el);
    const onOrbit = opts.onOrbit || (() => {});
    const onStateChange = opts.onStateChange || (() => {});

    let suppressClickUntil = 0;
    let touchState = null;
    let ptrState = null;

    function finishTouch(cancelSuppress) {
      if (!touchState) return;
      const wasOrbit = touchState.mode === "orbit";
      const target = touchState.target;
      touchState = null;
      if (wasOrbit) {
        if (!cancelSuppress) suppressClickUntil = performance.now() + 340;
        onStateChange(false, target);
      }
    }

    el.addEventListener("touchstart", (e) => {
      if (e.touches.length !== 1) {
        finishTouch(false);
        return;
      }
      const t = e.touches[0];
      const target = matchFn(t.target || e.target);
      if (!target) return;
      touchState = {
        id: t.identifier,
        target,
        startX: t.clientX,
        startY: t.clientY,
        lastX: t.clientX,
        lastY: t.clientY,
        mode: "pending",
      };
    }, { passive: true });

    el.addEventListener("touchmove", (e) => {
      if (!touchState) return;
      if (e.touches.length !== 1) {
        finishTouch(false);
        return;
      }
      const t = findTouch(e.touches, touchState.id);
      if (!t) return;

      if (touchState.mode === "scroll") return;

      const tdx = Math.abs(t.clientX - touchState.startX);
      const tdy = Math.abs(t.clientY - touchState.startY);

      if (touchState.mode === "pending") {
        if (tdx < AXIS_LOCK_PX && tdy < AXIS_LOCK_PX) return;
        if (tdx >= tdy * ORBIT_SLOPE) {
          touchState.mode = "orbit";
          if (e.cancelable) e.preventDefault();
          onStateChange(true, touchState.target);
          const dx = t.clientX - touchState.lastX;
          const dy = t.clientY - touchState.lastY;
          touchState.lastX = t.clientX;
          touchState.lastY = t.clientY;
          onOrbit(dx, dy, touchState.target, true);
        } else {
          touchState.mode = "scroll";
        }
        return;
      }

      // Locked in orbit mode: block native scroll/back-swipe for the whole finger hold.
      if (e.cancelable) e.preventDefault();
      const dx = t.clientX - touchState.lastX;
      const dy = t.clientY - touchState.lastY;
      touchState.lastX = t.clientX;
      touchState.lastY = t.clientY;
      if (dx !== 0 || dy !== 0) {
        onOrbit(dx, dy, touchState.target, true);
      }
    }, { passive: false });

    const onTouchEnd = (e) => {
      if (!touchState) return;
      if (!findTouch(e.touches, touchState.id)) {
        finishTouch(false);
      }
    };
    el.addEventListener("touchend", onTouchEnd, { passive: true });
    el.addEventListener("touchcancel", onTouchEnd, { passive: true });

    // Mouse / Pen drag via PointerEvents (touch excluded so gestures never double-fire).
    el.addEventListener("pointerdown", (e) => {
      if (e.pointerType === "touch") return;
      if (e.button !== 0 && e.pointerType === "mouse") return;
      const target = matchFn(e.target);
      if (!target) return;
      ptrState = {
        id: e.pointerId,
        target,
        startX: e.clientX,
        startY: e.clientY,
        lastX: e.clientX,
        lastY: e.clientY,
        active: false,
      };
    });

    el.addEventListener("pointermove", (e) => {
      if (!ptrState || e.pointerId !== ptrState.id) return;
      const tdx = Math.abs(e.clientX - ptrState.startX);
      const tdy = Math.abs(e.clientY - ptrState.startY);
      if (!ptrState.active) {
        if (tdx <= 3 && tdy <= 3) return;
        ptrState.active = true;
        onStateChange(true, ptrState.target);
        try { el.setPointerCapture(e.pointerId); } catch (_) {}
      }
      const dx = e.clientX - ptrState.lastX;
      const dy = e.clientY - ptrState.lastY;
      ptrState.lastX = e.clientX;
      ptrState.lastY = e.clientY;
      onOrbit(dx, dy, ptrState.target, false);
    });

    const endPtr = (e) => {
      if (!ptrState || e.pointerId !== ptrState.id) return;
      const wasActive = ptrState.active;
      const target = ptrState.target;
      ptrState = null;
      if (wasActive) {
        suppressClickUntil = performance.now() + 260;
        onStateChange(false, target);
      }
    };
    el.addEventListener("pointerup", endPtr);
    el.addEventListener("pointercancel", endPtr);
    el.addEventListener("lostpointercapture", endPtr);

    return {
      shouldSuppressClick: () => performance.now() < suppressClickUntil,
    };
  }

  window.__antTouch = { attachCardOrbit };
})();

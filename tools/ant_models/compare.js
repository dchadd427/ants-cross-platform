/* Ants 1998 Sprite vs. Live 3D Comparison View — single offscreen WebGL2 context blitted to 2D panels. */
(function () {
  "use strict";

  const { clamp, mat4Translate, mat4Multiply, transformPoint, parseGlb } = window.__antGltf;
  const {
    ANT_CLASSES, ANT_LABELS, TEAMS, ANGLES, SHEET_ANGLES, DEG,
    wrapAngleDeg, AntRenderer, installToolbarRovingTabindex,
  } = window.__antRender;

  const ORIGINAL_TEAMS = ["green", "red", "blue", "black"];
  const SPEEDS = [0.5, 1, 1.5];
  const GROUND_FRAC = 0.87;
  const RELOAD_STATE_KEY = "ants_viewer_reload_state_v1";

  function formatSec(sec) {
    return Number.isFinite(sec) && sec > 0 ? sec.toFixed(2) + "s" : "\u2014";
  }

  function pickOrigFrame(entry, simTimeSec) {
    if (!entry || !Array.isArray(entry.steps) || entry.steps.length <= 1 || !entry.total_ms) return 0;
    const total = entry.total_ms;
    const tMs = (((simTimeSec * 1000) % total) + total) % total;
    let acc = 0;
    for (let i = 0; i < entry.steps.length; i++) {
      acc += entry.steps[i].ms;
      if (tMs < acc) return entry.steps[i].f;
    }
    return entry.steps[entry.steps.length - 1].f;
  }

  function initCompare() {
    const toolbarEl = document.getElementById("compare-toolbar");
    const gridEl = document.getElementById("compare-grid");
    const soloEl = document.getElementById("compare-solo");
    const stageMain = document.getElementById("compare-stage");
    const soloOrigCanvas = document.getElementById("solo-orig-canvas");
    const solo3dCanvas = document.getElementById("solo-3d-canvas");
    const soloTitleEl = document.getElementById("solo-title");
    const soloCycleEl = document.getElementById("solo-cycle");
    const soloAnimEl = document.getElementById("solo-anim");
    const soloOrigPill = document.getElementById("solo-orig-pill");
    const solo3dPill = document.getElementById("solo-3d-pill");
    const btnBackAll = document.getElementById("btn-back-all");
    const btnSoloReset = document.getElementById("btn-solo-reset-angle");
    const btnIdle = document.getElementById("btn-idle");
    const btnSyncReset = document.getElementById("btn-sync-reset");
    const studioTab = document.getElementById("tab-studio");
    const statusEl = document.getElementById("status-region");
    const emptyOverlay = document.getElementById("compare-empty");
    const emptyTitle = document.getElementById("compare-empty-title");
    const emptyMessage = document.getElementById("compare-empty-message");
    const badgeAnt = document.getElementById("badge-ant");
    const badgeTeam = document.getElementById("badge-team");
    const badgeAngle = document.getElementById("badge-angle");
    const metaScale = document.getElementById("meta-scale");
    const metaPlay = document.getElementById("meta-play");

    if (!("focusGroup" in HTMLElement.prototype)) installToolbarRovingTabindex(toolbarEl);

    const cardCanvases = new Map();
    const cardCycles = new Map();
    for (const cls of ANT_CLASSES) {
      const cv = gridEl.querySelector(`[data-canvas-ant="${cls}"]`);
      if (cv) cardCanvases.set(cls, { canvas: cv, ctx: cv.getContext("2d") });
      const cy = gridEl.querySelector(`[data-cycle-for="${cls}"]`);
      if (cy) cardCycles.set(cls, cy);
    }
    const soloOrigCtx = soloOrigCanvas.getContext("2d");
    const solo3dCtx = solo3dCanvas.getContext("2d");

    let savedState = null;
    try {
      const raw = window.sessionStorage.getItem(RELOAD_STATE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        if (parsed && (!parsed.page || parsed.page === window.location.pathname)) {
          window.sessionStorage.removeItem(RELOAD_STATE_KEY);
          savedState = parsed;
        }
      }
    } catch (_) {}

    const params = new URLSearchParams(window.location.search);
    const qAnt = (params.get("ant") || "").toLowerCase();
    const qTeam = (params.get("team") || "").toLowerCase();
    const qAngle = (params.get("angle") || "").toLowerCase();
    const qSpeed = Number(params.get("speed"));
    const qIdle = params.get("idle");
    const qTime = params.has("t") ? Number(params.get("t")) : NaN;
    const darkMq = window.matchMedia("(prefers-color-scheme: dark)");

    const initAnt = params.get("lineup") === "1" || qAnt === "all" || qAnt === "lineup"
      ? "all"
      : (ANT_CLASSES.includes(qAnt)
        ? qAnt
        : (savedState && (savedState.ant === "all" || ANT_CLASSES.includes(savedState.ant)) ? savedState.ant : "all"));
    const initTeam = ORIGINAL_TEAMS.includes(qTeam)
      ? qTeam
      : (savedState && ORIGINAL_TEAMS.includes(savedState.team) ? savedState.team : "green");
    const initAngle = SHEET_ANGLES.includes(qAngle)
      ? qAngle
      : (savedState && SHEET_ANGLES.includes(savedState.angle) ? savedState.angle : "301");
    const initIdle = qIdle !== null
      ? qIdle !== "0"
      : (savedState && typeof savedState.idle === "boolean" ? savedState.idle : true);
    const initSpeed = SPEEDS.includes(qSpeed)
      ? qSpeed
      : (savedState && SPEEDS.includes(savedState.speed) ? savedState.speed : 1);

    const state = {
      ant: initAnt,
      team: initTeam,
      angle: initAngle,
      idle: initIdle,
      speed: initSpeed,
      simTime: Number.isFinite(qTime) && qTime >= 0 ? qTime : 0,
      dark: darkMq.matches,
      soloCam: { yaw: ANGLES[initAngle].yaw, pitch: ANGLES[initAngle].pitch, zoom: 1.0, projection: "ortho" },
      soloOrbited: false,
      origBundle: null,
      origMetrics: null,
      origImages: new Map(),
      models: new Map(),
      scenes: new Map(),
      spanCache: new Map(),
      workerSpan301: null,
      max3dRatio: 1.0,
      lastPxScaleLabel: "",
      dirty: true,
      loadedModels: 0,
      error: null,
    };

    const offCanvas = document.createElement("canvas");
    offCanvas.width = 256;
    offCanvas.height = 256;
    let renderer;
    try {
      renderer = new AntRenderer(offCanvas, { premultipliedAlpha: true });
    } catch (err) {
      state.error = err.message;
      emptyTitle.textContent = "WebGL2 is required";
      emptyMessage.textContent = err.message;
      emptyOverlay.hidden = false;
      statusEl.textContent = "WebGL2 unavailable: " + err.message;
      syncDebugState();
      return;
    }

    function announce(msg) {
      statusEl.textContent = msg;
    }

    function markDirty() {
      state.dirty = true;
    }

    function syncStudioLink() {
      if (!studioTab) return;
      const q = new URLSearchParams();
      q.set("ant", state.ant === "all" ? "lineup" : state.ant);
      q.set("team", state.team);
      q.set("angle", state.angle);
      studioTab.setAttribute("href", "viewer.html?" + q.toString());
    }

    function syncDebugState() {
      window.__antCompare = {
        ready: Boolean(state.origBundle) && state.loadedModels === ANT_CLASSES.length && !state.error,
        mode: state.ant === "all" ? "all" : "solo",
        ant: state.ant,
        team: state.team,
        angle: state.angle,
        idle: state.idle,
        speed: state.speed,
        simTime: Number(state.simTime.toFixed(3)),
        soloOrbited: state.soloOrbited,
        loadedModels: state.loadedModels,
        origLoaded: Boolean(state.origBundle),
        scale: state.lastPxScaleLabel,
        error: state.error,
      };
    }

    window.__antViewerGetState = () => ({
      ant: state.ant,
      team: state.team,
      angle: state.angle,
      idle: state.idle,
      speed: state.speed,
    });

    function updateCycleLabels() {
      for (const cls of ANT_CLASSES) {
        const origEntry = state.origBundle && state.origBundle.sprites[cls] && state.origBundle.sprites[cls][state.angle];
        const model = state.models.get(cls);
        const origSec = origEntry ? origEntry.total_ms / 1000 : NaN;
        const modelSec = model && model.idleAnims && model.idleAnims.length ? model.idleAnims[0].duration : NaN;
        const text = `Orig ${formatSec(origSec)} \u00b7 3D ${formatSec(modelSec)}`;
        const cardCy = cardCycles.get(cls);
        if (cardCy) cardCy.textContent = text;
        if (state.ant === cls) {
          soloCycleEl.textContent = text;
          soloAnimEl.textContent = origEntry ? `${origEntry.anim_name} (${origEntry.frame_count}f)` : "\u2014";
        }
      }
    }

    function updateUiState() {
      toolbarEl.querySelectorAll("button[data-ant]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.ant === state.ant));
      });
      toolbarEl.querySelectorAll("button[data-team]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.team === state.team));
      });
      toolbarEl.querySelectorAll("button[data-angle]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.angle === state.angle));
      });
      toolbarEl.querySelectorAll("button[data-speed]").forEach((b) => {
        b.setAttribute("aria-pressed", String(Number(b.dataset.speed) === state.speed));
      });
      btnIdle.setAttribute("aria-pressed", String(state.idle));

      const isSolo = state.ant !== "all";
      gridEl.hidden = isSolo;
      soloEl.hidden = !isSolo;

      const angleInfo = ANGLES[state.angle] || ANGLES["301"];
      badgeAnt.textContent = isSolo ? ANT_LABELS[state.ant] : "All 6";
      badgeTeam.textContent = TEAMS[state.team].label;
      badgeAngle.textContent = angleInfo.label;
      metaPlay.textContent = `${state.idle ? "Playing" : "Paused"} \u00b7 ${state.speed}\u00d7`;

      if (isSolo) {
        soloTitleEl.textContent = ANT_LABELS[state.ant] + " Ant";
        btnSoloReset.disabled = !state.soloOrbited;
        solo3dPill.textContent = state.soloOrbited
          ? `Live 3D \u00b7 Yaw ${Math.round(state.soloCam.yaw)}\u00b0 Pitch ${Math.round(state.soloCam.pitch)}\u00b0`
          : `Live 3D \u00b7 ${angleInfo.label}`;
      }

      updateCycleLabels();
      syncStudioLink();
      syncDebugState();
    }

    function resetSoloCamera() {
      const preset = ANGLES[state.angle] || ANGLES["301"];
      state.soloCam.yaw = preset.yaw;
      state.soloCam.pitch = preset.pitch;
      state.soloCam.zoom = 1.0;
      state.soloOrbited = false;
    }

    function selectAnt(id) {
      const next = ANT_CLASSES.includes(id) ? id : "all";
      const changed = state.ant !== next;
      state.ant = next;
      resetSoloCamera();
      updateUiState();
      markDirty();
      if (changed) {
        announce(next === "all"
          ? "Showing all 6 ants side by side."
          : `Focused on ${ANT_LABELS[next]} 1998 sprite vs. live 3D.`);
      }
    }

    function selectTeam(teamId) {
      if (!ORIGINAL_TEAMS.includes(teamId)) return;
      state.team = teamId;
      updateUiState();
      markDirty();
      announce("Team set to " + TEAMS[teamId].label + ".");
    }

    function selectAngle(angleKey) {
      if (!SHEET_ANGLES.includes(angleKey)) return;
      state.angle = angleKey;
      resetSoloCamera();
      updateUiState();
      markDirty();
      announce("Canonical angle set to " + ANGLES[angleKey].full + ".");
    }

    function toggleIdle() {
      state.idle = !state.idle;
      updateUiState();
      markDirty();
      announce(state.idle ? `Idle playback resumed at ${state.speed}x.` : "Idle playback paused.");
    }

    function selectSpeed(spd) {
      if (!SPEEDS.includes(spd)) return;
      state.speed = spd;
      updateUiState();
      markDirty();
      announce(`Playback speed set to ${spd}x.`);
    }

    function syncReset() {
      state.simTime = 0;
      let idx = 0;
      for (const sc of state.scenes.values()) {
        const inst = sc.instances[0];
        if (inst && inst.blink) {
          inst.blink.active = false;
          inst.blink.elapsed = 0;
          inst.blink.wait = 1.8 + idx * 0.55;
        }
        idx++;
      }
      resetSoloCamera();
      updateUiState();
      markDirty();
      announce("Synchronized 1998 and 3D clocks restarted at 0.00s.");
    }

    function computeSpan(cls, yawDeg, pitchDeg) {
      const key = `${cls}:${yawDeg.toFixed(1)}:${pitchDeg.toFixed(1)}`;
      if (state.spanCache.has(key)) return state.spanCache.get(key);
      const sc = state.scenes.get(cls);
      if (!sc) return { vMin: -0.1, vMax: 1.0, span: 1.1 };
      const inst = sc.instances[0];
      const p = pitchDeg * DEG, y = yawDeg * DEG;
      const ux = -Math.sin(p) * Math.sin(y), uy = Math.cos(p), uz = -Math.sin(p) * Math.cos(y);
      let vMin = Infinity, vMax = -Infinity;
      for (const dc of inst.model.drawCalls) {
        const m = mat4Multiply(inst.offsetMat, dc.worldMat);
        const lmin = dc.prim.localMin, lmax = dc.prim.localMax;
        const cx = (lmin[0] + lmax[0]) * 0.5, cy = (lmin[1] + lmax[1]) * 0.5, cz = (lmin[2] + lmax[2]) * 0.5;
        const hx = (lmax[0] - lmin[0]) * 0.5, hy = (lmax[1] - lmin[1]) * 0.5, hz = (lmax[2] - lmin[2]) * 0.5;
        const wc = transformPoint(m, cx, cy, cz);
        const d0 = (m[0] * ux + m[1] * uy + m[2] * uz) * hx;
        const d1 = (m[4] * ux + m[5] * uy + m[6] * uz) * hy;
        const d2 = (m[8] * ux + m[9] * uy + m[10] * uz) * hz;
        const rad = Math.hypot(d0, d1, d2);
        const vc = wc[0] * ux + wc[1] * uy + wc[2] * uz;
        if (vc - rad < vMin) vMin = vc - rad;
        if (vc + rad > vMax) vMax = vc + rad;
      }
      if (!Number.isFinite(vMin)) { vMin = 0; vMax = 1; }
      const res = { vMin, vMax, span: Math.max(0.25, vMax - vMin) };
      state.spanCache.set(key, res);
      return res;
    }

    function recompute3dMetrics() {
      state.spanCache.clear();
      if (!state.scenes.has("worker")) return;
      const wSpan = computeSpan("worker", ANGLES["301"].yaw, ANGLES["301"].pitch).span;
      state.workerSpan301 = wSpan;
      let maxRatio = 1.0;
      for (const cls of state.scenes.keys()) {
        for (const ang of SHEET_ANGLES) {
          const sp = computeSpan(cls, ANGLES[ang].yaw, ANGLES[ang].pitch).span;
          maxRatio = Math.max(maxRatio, sp / wSpan);
        }
      }
      state.max3dRatio = maxRatio;
    }

    function computeOrigMetrics(bundle) {
      let maxW = 24, maxAbove = 24, maxBelow = 2;
      for (const cls of ANT_CLASSES) {
        const clsMap = bundle.sprites[cls];
        if (!clsMap) continue;
        for (const ang of SHEET_ANGLES) {
          const e = clsMap[ang];
          if (!e) continue;
          maxW = Math.max(maxW, e.fw);
          maxAbove = Math.max(maxAbove, e.foot_y);
          maxBelow = Math.max(maxBelow, Math.max(0, e.fh - e.foot_y));
        }
      }
      const w301 = bundle.sprites.worker && bundle.sprites.worker["301"];
      return { maxW, maxAbove, maxBelow, workerVisH: w301 ? w301.vis_h : 34 };
    }

    function getOrigImage(cls, angle, team) {
      const key = `${cls}:${angle}:${team}`;
      const existing = state.origImages.get(key);
      if (existing) return existing;
      const entry = state.origBundle && state.origBundle.sprites[cls] && state.origBundle.sprites[cls][angle];
      const uri = entry && entry.sheets && entry.sheets[team];
      if (!uri) return null;
      const rec = { img: new Image(), ready: false, failed: false };
      rec.img.decoding = "async";
      rec.img.onload = () => { rec.ready = true; markDirty(); };
      rec.img.onerror = () => { rec.failed = true; };
      rec.img.src = uri;
      state.origImages.set(key, rec);
      return rec;
    }

    function groundFracFor(panelW, panelH) {
      return panelH > panelW * 1.35 ? 0.72 : GROUND_FRAC;
    }

    function computeScaleParams(panelW, panelH, dpr) {
      const gFrac = groundFracFor(panelW, panelH);
      const groundY = Math.round(panelH * gFrac);
      const om = state.origMetrics || { maxW: 52, maxAbove: 52, maxBelow: 4, workerVisH: 34 };
      const topPad = Math.round(24 * dpr);
      const availW = Math.max(24, panelW * 0.94);
      const availAbove = Math.max(24, (groundY - topPad) * 0.95);
      const availBelow = Math.max(8, (panelH - groundY) - Math.round(4 * dpr));
      const maxAbove = Math.max(om.maxAbove, om.workerVisH * state.max3dRatio);
      const rawPx = Math.min(availW / om.maxW, availAbove / maxAbove, availBelow / Math.max(1, om.maxBelow));
      const pxScale = Math.max(1, Math.floor(rawPx));
      const workerPxH = Math.max(12, om.workerVisH * pxScale);
      const workerSpan = state.workerSpan301 || 1.0;
      const fixedHalfH = (workerSpan * panelH) / (2 * workerPxH);
      const cssScale = pxScale / dpr;
      const scaleText = Number.isInteger(cssScale) ? `${cssScale}\u00d7` : `${cssScale.toFixed(1)}\u00d7`;
      return { gFrac, groundY, pxScale, fixedHalfH, scaleText };
    }

    function drawStudioBackdrop(ctx, w, h, groundY, withCenterDivider) {
      const dark = state.dark;
      const grad = ctx.createLinearGradient(0, 0, 0, h);
      if (dark) {
        grad.addColorStop(0, "#121822");
        grad.addColorStop(0.58, "#1d2534");
        grad.addColorStop(1, "#141a24");
      } else {
        grad.addColorStop(0, "#eef2f7");
        grad.addColorStop(0.58, "#dfe6ef");
        grad.addColorStop(1, "#cfd9e6");
      }
      ctx.fillStyle = grad;
      ctx.fillRect(0, 0, w, h);

      // Subtle grounded floor tint below shared ground line
      const floorGrad = ctx.createLinearGradient(0, groundY, 0, h);
      if (dark) {
        floorGrad.addColorStop(0, "rgba(0, 0, 0, 0.22)");
        floorGrad.addColorStop(1, "rgba(0, 0, 0, 0.06)");
      } else {
        floorGrad.addColorStop(0, "rgba(20, 32, 48, 0.08)");
        floorGrad.addColorStop(1, "rgba(20, 32, 48, 0.02)");
      }
      ctx.fillStyle = floorGrad;
      ctx.fillRect(0, groundY, w, h - groundY);

      // Crisp shared horizontal ground line
      ctx.strokeStyle = dark ? "rgba(255, 255, 255, 0.18)" : "rgba(20, 32, 48, 0.20)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, groundY + 0.5);
      ctx.lineTo(w, groundY + 0.5);
      ctx.stroke();

      if (withCenterDivider) {
        const mid = Math.floor(w * 0.5);
        ctx.strokeStyle = dark ? "rgba(255, 255, 255, 0.08)" : "rgba(20, 32, 48, 0.10)";
        ctx.beginPath();
        ctx.moveTo(mid + 0.5, 0);
        ctx.lineTo(mid + 0.5, h);
        ctx.stroke();
      }
    }

    function drawContactShadow(ctx, cx, groundY, rx, ry) {
      ctx.save();
      ctx.translate(cx, groundY);
      ctx.scale(rx, ry);
      const rg = ctx.createRadialGradient(0, 0, 0.05, 0, 0, 1);
      rg.addColorStop(0, state.dark ? "rgba(0, 0, 0, 0.48)" : "rgba(18, 28, 42, 0.26)");
      rg.addColorStop(0.65, state.dark ? "rgba(0, 0, 0, 0.20)" : "rgba(18, 28, 42, 0.10)");
      rg.addColorStop(1, "rgba(0, 0, 0, 0)");
      ctx.fillStyle = rg;
      ctx.beginPath();
      ctx.arc(0, 0, 1, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }

    function drawOrigSprite(ctx, cls, x0, panelW, groundY, pxScale) {
      const entry = state.origBundle && state.origBundle.sprites[cls] && state.origBundle.sprites[cls][state.angle];
      if (!entry) return;
      const rec = getOrigImage(cls, state.angle, state.team);
      if (!rec || !rec.ready) return;
      const frameIdx = pickOrigFrame(entry, state.simTime);
      const dw = entry.fw * pxScale;
      const dh = entry.fh * pxScale;
      const dx = Math.round(x0 + (panelW - dw) * 0.5);
      const dy = Math.round(groundY - entry.foot_y * pxScale);
      const shadowRx = Math.max(12, Math.min(panelW * 0.34, entry.vis_w * pxScale * 0.42));
      drawContactShadow(ctx, x0 + panelW * 0.5, groundY, shadowRx, Math.max(4, shadowRx * 0.22));
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(rec.img, frameIdx * entry.fw, 0, entry.fw, entry.fh, dx, dy, dw, dh);
    }

    function ensureOffscreenSize(w, h) {
      if (offCanvas.width < w || offCanvas.height < h || offCanvas.width > w * 2.2 || offCanvas.height > h * 2.2) {
        offCanvas.width = Math.max(w, offCanvas.width > w * 2.2 ? w : offCanvas.width);
        offCanvas.height = Math.max(h, offCanvas.height > h * 2.2 ? h : offCanvas.height);
      }
    }

    function draw3dAnt(ctx, cls, x0, panelW, panelH, groundY, fixedHalfH, cam, useCanonicalAnchor) {
      const sc = state.scenes.get(cls);
      if (!sc) return;
      ensureOffscreenSize(panelW, panelH);
      const canon = ANGLES[state.angle] || ANGLES["301"];
      const anchorYaw = useCanonicalAnchor ? canon.yaw : cam.yaw;
      const anchorPitch = useCanonicalAnchor ? canon.pitch : cam.pitch;
      const span = computeSpan(cls, anchorYaw, anchorPitch);
      const gFrac = groundY / panelH;
      const targetY = (span.vMin + fixedHalfH * (2 * gFrac - 1)) / Math.cos(anchorPitch * DEG);

      const shadowRx = Math.max(12, Math.min(panelW * 0.34, (panelH / (2 * fixedHalfH)) * sc.instances[0].model.size[0] * 0.38));
      drawContactShadow(ctx, x0 + panelW * 0.5, groundY, shadowRx, Math.max(4, shadowRx * 0.22));

      renderer.drawScene(sc, cam, {
        width: panelW,
        height: panelH,
        transparent: true,
        showPlinth: false,
        dark: state.dark,
        team: state.team,
        blinkMode: "auto",
        idleAnim: true,
        fixedIdleTime: state.simTime,
        overrideProj: "ortho",
        fixedHalfH,
        customTarget: [0, targetY, 0],
      });
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(offCanvas, 0, offCanvas.height - panelH, panelW, panelH, x0, 0, panelW, panelH);
    }

    function syncCanvasBacking(cv, dpr) {
      const w = Math.max(1, Math.round(cv.clientWidth * dpr));
      const h = Math.max(1, Math.round(cv.clientHeight * dpr));
      if (cv.width !== w || cv.height !== h) {
        cv.width = w;
        cv.height = h;
      }
      return [w, h];
    }

    function renderAll() {
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const canon = ANGLES[state.angle] || ANGLES["301"];
      const canonCam = { yaw: canon.yaw, pitch: canon.pitch, zoom: 1.0, projection: "ortho" };

      if (state.ant === "all") {
        let minHalfW = Infinity, minH = Infinity;
        const sizes = new Map();
        for (const cls of ANT_CLASSES) {
          const rec = cardCanvases.get(cls);
          if (!rec) continue;
          const [w, h] = syncCanvasBacking(rec.canvas, dpr);
          const halfW = Math.max(1, Math.floor(w * 0.5));
          sizes.set(cls, { w, h, halfW });
          if (halfW < minHalfW) minHalfW = halfW;
          if (h < minH) minH = h;
        }
        if (!Number.isFinite(minHalfW)) return;
        const scParams = computeScaleParams(minHalfW, minH, dpr);
        if (state.lastPxScaleLabel !== scParams.scaleText) {
          state.lastPxScaleLabel = scParams.scaleText;
          metaScale.textContent = `Worker-locked \u00b7 ${scParams.scaleText} pixel scale`;
        }
        for (const cls of ANT_CLASSES) {
          const rec = cardCanvases.get(cls), sz = sizes.get(cls);
          if (!rec || !sz) continue;
          const groundY = Math.round(sz.h * GROUND_FRAC);
          const rightW = sz.w - sz.halfW;
          const halfHForCard = scParams.fixedHalfH * (sz.h / minH);
          drawStudioBackdrop(rec.ctx, sz.w, sz.h, groundY, true);
          drawOrigSprite(rec.ctx, cls, 0, sz.halfW, groundY, scParams.pxScale);
          draw3dAnt(rec.ctx, cls, sz.halfW, rightW, sz.h, groundY, halfHForCard, canonCam, true);
        }
      } else {
        const [ow, oh] = syncCanvasBacking(soloOrigCanvas, dpr);
        const [rw, rh] = syncCanvasBacking(solo3dCanvas, dpr);
        const panelW = Math.min(ow, rw), panelH = Math.min(oh, rh);
        const scParams = computeScaleParams(panelW, panelH, dpr);
        if (state.lastPxScaleLabel !== scParams.scaleText) {
          state.lastPxScaleLabel = scParams.scaleText;
          metaScale.textContent = `Worker-locked \u00b7 ${scParams.scaleText} pixel scale`;
        }
        soloOrigPill.textContent = `1998 Original \u00b7 ${scParams.scaleText}`;

        const gOrig = Math.round(oh * scParams.gFrac);
        drawStudioBackdrop(soloOrigCtx, ow, oh, gOrig, false);
        drawOrigSprite(soloOrigCtx, state.ant, 0, ow, gOrig, scParams.pxScale);

        const g3d = Math.round(rh * scParams.gFrac);
        drawStudioBackdrop(solo3dCtx, rw, rh, g3d, false);
        draw3dAnt(solo3dCtx, state.ant, 0, rw, rh, g3d, scParams.fixedHalfH * (rh / panelH), state.soloCam, true);
      }
    }

    async function loadAssets() {
      const origTask = fetch("models/orig_sprites.json", { cache: "no-cache" })
        .then((res) => {
          if (!res.ok) throw new Error("HTTP " + res.status + " loading models/orig_sprites.json");
          return res.json();
        })
        .then((bundle) => {
          state.origBundle = bundle;
          state.origMetrics = computeOrigMetrics(bundle);
          updateCycleLabels();
          markDirty();
          syncDebugState();
        });

      const modelTasks = ANT_CLASSES.map(async (cls, idx) => {
        const res = await fetch(`models/${cls}.glb`, { cache: "no-cache" });
        if (!res.ok) throw new Error(`HTTP ${res.status} loading models/${cls}.glb`);
        const buf = await res.arrayBuffer();
        const model = await parseGlb(buf, renderer.gl);
        state.models.set(cls, model);
        const yLift = Math.abs(model.bounds.min[1]) > 0.02 ? -model.bounds.min[1] : 0;
        const offsetMat = mat4Translate(-model.center[0], yLift, -model.center[2]);
        const inst = {
          id: cls,
          model,
          offsetMat,
          blink: { active: false, elapsed: 0, wait: 1.8 + idx * 0.55 },
          idleTime: 0,
        };
        state.scenes.set(cls, {
          instances: [inst],
          target: [0, model.center[1] + yLift, 0],
          fitRadius: 1,
          halfWidth: 1,
          plinthSpanX: 0,
          plinthRadius: 1,
          shadows: [],
        });
        state.loadedModels++;
        recompute3dMetrics();
        updateCycleLabels();
        markDirty();
        syncDebugState();
      });

      const results = await Promise.allSettled([origTask, ...modelTasks]);
      const firstErr = results.find((r) => r.status === "rejected");
      if (firstErr && (!state.origBundle || state.loadedModels === 0)) {
        state.error = firstErr.reason ? String(firstErr.reason.message || firstErr.reason) : "Failed to load assets";
        emptyTitle.textContent = "Could not load comparison assets";
        emptyMessage.textContent = state.error + ". Run python3 -m tools.ant_models.build_all and serve over HTTP.";
        emptyOverlay.hidden = false;
        announce("Asset load failed: " + state.error);
      } else {
        emptyOverlay.hidden = true;
        announce("Loaded all 6 1998 sprites and live 3D ant models.");
      }
      syncDebugState();
    }

    // --- Event Wiring ---
    toolbarEl.addEventListener("click", (e) => {
      const btn = e.target.closest("button");
      if (!btn) return;
      if (btn.dataset.ant) selectAnt(btn.dataset.ant);
      else if (btn.dataset.team) selectTeam(btn.dataset.team);
      else if (btn.dataset.angle) selectAngle(btn.dataset.angle);
      else if (btn.dataset.speed) selectSpeed(Number(btn.dataset.speed));
      else if (btn === btnIdle) toggleIdle();
      else if (btn === btnSyncReset) syncReset();
    });

    gridEl.addEventListener("click", (e) => {
      const focusBtn = e.target.closest("button[data-focus-ant]");
      if (focusBtn) { selectAnt(focusBtn.dataset.focusAnt); return; }
      const stageHit = e.target.closest("[data-stage-ant]");
      if (stageHit) selectAnt(stageHit.dataset.stageAnt);
    });

    btnBackAll.addEventListener("click", () => selectAnt("all"));
    btnSoloReset.addEventListener("click", () => {
      resetSoloCamera();
      updateUiState();
      markDirty();
      announce("3D camera reset to " + ANGLES[state.angle].full + ".");
    });

    darkMq.addEventListener("change", (e) => {
      state.dark = e.matches;
      markDirty();
      announce(state.dark ? "Switched studio backdrop to dark theme." : "Switched studio backdrop to light theme.");
    });

    // Solo 3D Orbit + Pinch Zoom + Keyboard Orbit
    const pointers = new Map();
    let lastPinch = 0;
    const pinchDist = () => {
      const pts = Array.from(pointers.values());
      return pts.length < 2 ? 0 : Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
    };
    solo3dCanvas.addEventListener("pointerdown", (e) => {
      if (e.pointerType === "touch") e.preventDefault();
      try { solo3dCanvas.setPointerCapture(e.pointerId); } catch (_) {}
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pointers.size === 2) lastPinch = pinchDist();
    });
    solo3dCanvas.addEventListener("pointermove", (e) => {
      const prev = pointers.get(e.pointerId);
      if (!prev) return;
      const dx = e.clientX - prev.x, dy = e.clientY - prev.y;
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pointers.size === 1) {
        state.soloCam.yaw = wrapAngleDeg(state.soloCam.yaw - dx * 0.45);
        state.soloCam.pitch = clamp(state.soloCam.pitch + dy * 0.35, -10, 85);
        state.soloOrbited = true;
        updateUiState();
        markDirty();
      } else if (pointers.size === 2) {
        const dist = pinchDist();
        if (lastPinch > 1 && dist > 1) {
          state.soloCam.zoom = clamp(state.soloCam.zoom * (lastPinch / dist), 0.45, 2.5);
          state.soloOrbited = true;
          updateUiState();
          markDirty();
        }
        lastPinch = dist;
      }
    });
    const endPtr = (e) => { pointers.delete(e.pointerId); if (pointers.size < 2) lastPinch = 0; };
    solo3dCanvas.addEventListener("pointerup", endPtr);
    solo3dCanvas.addEventListener("pointercancel", endPtr);
    solo3dCanvas.addEventListener("lostpointercapture", endPtr);
    const preventPinchZoom = (e) => { if (!e.touches || e.touches.length >= 2) e.preventDefault(); };
    solo3dCanvas.addEventListener("touchmove", preventPinchZoom, { passive: false });
    solo3dCanvas.addEventListener("gesturestart", preventPinchZoom, { passive: false });
    solo3dCanvas.addEventListener("gesturechange", preventPinchZoom, { passive: false });
    solo3dCanvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      state.soloCam.zoom = clamp(state.soloCam.zoom * Math.exp(e.deltaY * 0.0012), 0.45, 2.5);
      state.soloOrbited = true;
      updateUiState();
      markDirty();
    }, { passive: false });

    solo3dCanvas.addEventListener("keydown", (e) => {
      const sy = e.shiftKey ? 4 : 12, sp = e.shiftKey ? 3 : 8;
      if (e.key === "ArrowLeft") state.soloCam.yaw = wrapAngleDeg(state.soloCam.yaw + sy);
      else if (e.key === "ArrowRight") state.soloCam.yaw = wrapAngleDeg(state.soloCam.yaw - sy);
      else if (e.key === "ArrowUp") state.soloCam.pitch = clamp(state.soloCam.pitch + sp, -10, 85);
      else if (e.key === "ArrowDown") state.soloCam.pitch = clamp(state.soloCam.pitch - sp, -10, 85);
      else if (e.key === "+" || e.key === "=") state.soloCam.zoom = clamp(state.soloCam.zoom * 0.88, 0.45, 2.5);
      else if (e.key === "-" || e.key === "_") state.soloCam.zoom = clamp(state.soloCam.zoom * 1.14, 0.45, 2.5);
      else if (e.key === "r" || e.key === "R") {
        e.preventDefault();
        resetSoloCamera();
        updateUiState();
        markDirty();
        announce("3D camera reset to " + ANGLES[state.angle].full + ".");
        return;
      } else return;
      e.preventDefault();
      state.soloOrbited = true;
      updateUiState();
      markDirty();
    });

    document.addEventListener("keydown", (e) => {
      if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
      if (e.key === "Escape" && state.ant !== "all") {
        e.preventDefault();
        selectAnt("all");
      } else if (e.key === "i" || e.key === "I") {
        e.preventDefault();
        toggleIdle();
      } else if ((e.key === "r" || e.key === "R") && document.activeElement !== solo3dCanvas) {
        e.preventDefault();
        syncReset();
      } else if (e.key >= "1" && e.key <= "5") {
        e.preventDefault();
        selectAngle(SHEET_ANGLES[Number(e.key) - 1]);
      }
    });

    new ResizeObserver(() => markDirty()).observe(stageMain);

    let prevTime = performance.now();
    function frame(now) {
      const dt = Math.min((now - prevTime) / 1000, 0.1);
      prevTime = now;
      if (state.idle) {
        const sDt = dt * state.speed;
        state.simTime += sDt;
        if (window.__antCompare) window.__antCompare.simTime = Number(state.simTime.toFixed(3));
        for (const sc of state.scenes.values()) {
          const inst = sc.instances[0];
          const bi = inst.model.blinkInfo, bl = inst.blink;
          if (!bi || !bi.hasBlink || !bl) continue;
          const pulseDur = bi.anim ? (bi.endTime - bi.startTime) : 0.22;
          if (bl.active) {
            bl.elapsed += sDt;
            if (bl.elapsed >= pulseDur) {
              bl.active = false;
              bl.elapsed = 0;
              bl.wait = 2.4 + Math.random() * 3.0;
            }
          } else {
            bl.wait -= sDt;
            if (bl.wait <= 0) {
              bl.active = true;
              bl.elapsed = 0;
            }
          }
        }
        state.dirty = true;
      }
      if (state.dirty) {
        state.dirty = false;
        renderAll();
      }
      requestAnimationFrame(frame);
    }

    updateUiState();
    loadAssets();
    requestAnimationFrame(frame);
  }

  document.addEventListener("DOMContentLoaded", initCompare);
})();

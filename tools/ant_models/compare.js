/* Ants 1998 Sprite vs. Live 3D Comparison View — single offscreen WebGL2 context blitted to 2D panels. */
(function () {
  "use strict";

  const { clamp, mat4Translate, mat4Multiply, transformPoint, parseGlb } = window.__antGltf;
  const {
    ANT_CLASSES, ANT_LABELS, TEAMS, ANGLES, SHEET_ANGLES, DEG,
    wrapAngleDeg, selectActionAnims, AntRenderer, installToolbarRovingTabindex,
  } = window.__antRender;

  const ORIGINAL_TEAMS = ["green", "red", "blue", "black"];
  const SPEEDS = [0.5, 1, 1.5];
  const RETRO_STEP_SEC = {
    worker: 0.150,
    combat: 0.125,
    thief: 0.100,
    bomber: 0.100,
    swimmer: 0.150,
    fire: 0.100,
  };
  const RETRO_ATTACK_STEP_SEC = {
    worker: 0.070,
    combat: 0.090,
    thief: 0.060,
    bomber: 0.060,
    swimmer: 0.076,
    fire: 0.060,
  };
  const GROUND_FRAC = 0.87;
  const ATTACK_GROUND_FRAC = 0.78;
  const CANONICAL_PITCH = ANGLES["301"].pitch;
  const RELOAD_STATE_KEY = "ants_viewer_reload_state_v1";

  const COMPASS_DIRS = [
    { id: "301", angle: "301", flip: false, yaw: 0, dir: "S", label: "301 S", full: "301 S (Front)" },
    { id: "201", angle: "201", flip: false, yaw: -45, dir: "SE", label: "201 SE", full: "201 SE (Front-Right)" },
    { id: "901", angle: "901", flip: false, yaw: -90, dir: "E", label: "901 E", full: "901 E (Side Right)" },
    { id: "801", angle: "801", flip: false, yaw: -135, dir: "NE", label: "801 NE", full: "801 NE (Back-Right)" },
    { id: "701", angle: "701", flip: false, yaw: 180, dir: "N", label: "701 N", full: "701 N (Back)" },
    { id: "801_nw", angle: "801", flip: true, yaw: 135, dir: "NW", label: "801 NW \u00b7 flip", full: "801 NW (Back-Left, mirrored)" },
    { id: "901_w", angle: "901", flip: true, yaw: 90, dir: "W", label: "901 W \u00b7 flip", full: "901 W (Side Left, mirrored)" },
    { id: "201_sw", angle: "201", flip: true, yaw: 45, dir: "SW", label: "201 SW \u00b7 flip", full: "201 SW (Front-Left, mirrored)" },
  ];
  const COMPASS_BY_ID = Object.fromEntries(COMPASS_DIRS.map((d) => [d.id, d]));
  const COMPASS_BY_SHORT = Object.fromEntries(COMPASS_DIRS.map((d) => [d.dir.toLowerCase(), d]));

  function compassForYaw(yawDeg) {
    const norm = ((-yawDeg % 360) + 360) % 360;
    return COMPASS_DIRS[Math.round(norm / 45) % 8];
  }

  function resolveCompassQuery(raw) {
    if (!raw) return null;
    const k = raw.toLowerCase();
    return COMPASS_BY_ID[k] || COMPASS_BY_SHORT[k] || null;
  }

  function formatSec(sec) {
    return Number.isFinite(sec) && sec > 0 ? sec.toFixed(2) + "s" : "\u2014";
  }

  function pickOrigStep(entry, simTimeSec) {
    if (!entry || !Array.isArray(entry.steps) || entry.steps.length <= 1 || !entry.total_ms) {
      return { frameIdx: 0, quantizedSec: simTimeSec, stepDurSec: 0.125 };
    }
    const total = entry.total_ms;
    const totalSec = total / 1000;
    const cycles = Math.floor(simTimeSec / totalSec);
    const tMs = (((simTimeSec * 1000) % total) + total) % total;
    let acc = 0;
    for (let i = 0; i < entry.steps.length; i++) {
      const ms = entry.steps[i].ms;
      if (tMs < acc + ms) {
        return {
          frameIdx: entry.steps[i].f,
          quantizedSec: cycles * totalSec + acc / 1000,
          stepDurSec: ms / 1000,
        };
      }
      acc += ms;
    }
    const last = entry.steps[entry.steps.length - 1];
    return {
      frameIdx: last.f,
      quantizedSec: cycles * totalSec + Math.max(0, total - last.ms) / 1000,
      stepDurSec: last.ms / 1000,
    };
  }

  function pickOrigFrame(entry, simTimeSec) {
    return pickOrigStep(entry, simTimeSec).frameIdx;
  }

  function extractRestWorldVerts(model, offsetMat) {
    const raw = model && model.rawBytes;
    if (!raw || raw.byteLength < 28) return null;
    try {
      const dv = new DataView(raw);
      if (dv.getUint32(0, true) !== 0x46546c67) return null;
      const jsonLen = dv.getUint32(12, true);
      const gltf = JSON.parse(new TextDecoder("utf-8").decode(new Uint8Array(raw, 20, jsonLen)));
      const binOff = 20 + jsonLen;
      if (binOff + 8 > raw.byteLength) return null;
      const binLen = dv.getUint32(binOff, true);
      const binStart = binOff + 8;
      const nodes = gltf.nodes || [], meshes = gltf.meshes || [];
      const accessors = gltf.accessors || [], views = gltf.bufferViews || [];
      const chunks = [];
      let totalFloats = 0;
      for (let ni = 0; ni < nodes.length; ni++) {
        const node = nodes[ni];
        if (!node || node.mesh === undefined || !meshes[node.mesh]) continue;
        const baseMat = model.baseWorldMats && model.baseWorldMats[ni];
        if (!baseMat) continue;
        const m = mat4Multiply(offsetMat, baseMat);
        // Skip hidden rest-pose FX meshes scaled near zero (e.g. attack-only water/flame nodes).
        if (Math.hypot(m[0], m[1], m[2]) < 1e-3 || Math.hypot(m[4], m[5], m[6]) < 1e-3) continue;
        for (const prim of meshes[node.mesh].primitives || []) {
          const accIdx = prim.attributes && prim.attributes.POSITION;
          if (accIdx === undefined) continue;
          const acc = accessors[accIdx], bv = acc && views[acc.bufferView];
          if (!acc || !bv || acc.componentType !== 5126 || acc.type !== "VEC3") continue;
          const stride = bv.byteStride || 12;
          const start = binStart + (bv.byteOffset || 0) + (acc.byteOffset || 0);
          if (start + (acc.count - 1) * stride + 12 > binStart + binLen) continue;
          const pts = new Float32Array(acc.count * 3);
          for (let k = 0; k < acc.count; k++) {
            const pOff = start + k * stride;
            const x = dv.getFloat32(pOff, true), y = dv.getFloat32(pOff + 4, true), z = dv.getFloat32(pOff + 8, true);
            const o = k * 3;
            pts[o] = m[0] * x + m[4] * y + m[8] * z + m[12];
            pts[o + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
            pts[o + 2] = m[2] * x + m[6] * y + m[10] * z + m[14];
          }
          chunks.push(pts);
          totalFloats += pts.length;
        }
      }
      if (!totalFloats) return null;
      const merged = new Float32Array(totalFloats);
      let cursor = 0;
      for (const ch of chunks) { merged.set(ch, cursor); cursor += ch.length; }
      return merged;
    } catch (_) {
      return null;
    }
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
    const soloHintEl = soloEl ? soloEl.querySelector(".solo-hint") : null;
    if (soloHintEl) {
      soloHintEl.textContent = "Drag: Synced 360\u00b0 Turn (Pitch locked 45\u00b0) \u00b7 Scroll/Pinch: Zoom \u00b7 Arrows: Turn \u00b7 R: Reset \u00b7 Esc: All 6";
    }
    if (solo3dCanvas) {
      solo3dCanvas.setAttribute(
        "aria-label",
        "Live 3D ant model viewport locked to 45 degree isometric pitch with 1998 sprite. Drag horizontally to turn both ants together, scroll or pinch to zoom. Press Left or Right arrow keys to turn, plus or minus to zoom, R to reset to canonical angle, A to toggle Idle or Attack, I to pause or play, 1 through 8 for compass angles, or Escape to return to All 6."
      );
    }
    const btnBackAll = document.getElementById("btn-back-all");
    const btnSoloReset = document.getElementById("btn-solo-reset-angle");
    const btnPixelMode = document.getElementById("btn-pixel-mode");
    const btnSilhouette = document.getElementById("btn-silhouette");
    const btnRetroFps = document.getElementById("btn-retro-fps");
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
    const metaProj = document.getElementById("meta-proj");
    const metaPlay = document.getElementById("meta-play");

    if (!("focusGroup" in HTMLElement.prototype)) installToolbarRovingTabindex(toolbarEl);

    const cardCanvases = new Map();
    const cardCycles = new Map();
    const cardOrigPills = [];
    const card3dPills = [];
    for (const cls of ANT_CLASSES) {
      const cv = gridEl.querySelector(`[data-canvas-ant="${cls}"]`);
      if (cv) cardCanvases.set(cls, { canvas: cv, ctx: cv.getContext("2d") });
      const cy = gridEl.querySelector(`[data-cycle-for="${cls}"]`);
      if (cy) cardCycles.set(cls, cy);
      const lPill = gridEl.querySelector(`[data-card-ant="${cls}"] .compare-pill-left`);
      if (lPill) cardOrigPills.push(lPill);
      const rPill = gridEl.querySelector(`[data-card-ant="${cls}"] .compare-pill-right`);
      if (rPill) card3dPills.push(rPill);
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
    const qYaw = params.has("yaw") && Number.isFinite(Number(params.get("yaw"))) ? wrapAngleDeg(Number(params.get("yaw"))) : null;
    const qSpeed = Number(params.get("speed"));
    const qIdle = params.get("idle");
    const qAction = (params.get("action") || "").toLowerCase();
    const qPixel = params.get("pixel");
    const qSil = params.get("silhouette");
    const qRetro = params.get("retrofps");
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
    const qCompass = resolveCompassQuery(qAngle);
    const savedCompass = savedState ? resolveCompassQuery(savedState.compassId || savedState.angle) : null;
    const initYaw = qYaw !== null
      ? qYaw
      : (qCompass ? qCompass.yaw
        : (savedState && Number.isFinite(savedState.yaw) ? wrapAngleDeg(savedState.yaw)
          : (savedCompass ? savedCompass.yaw : 0)));
    const initCompass = compassForYaw(initYaw);
    const initIdle = qIdle !== null
      ? qIdle !== "0"
      : (savedState && typeof savedState.idle === "boolean" ? savedState.idle : true);
    const initAction = qAction === "attack" || qAction === "idle"
      ? qAction
      : (savedState && (savedState.action === "attack" || savedState.action === "idle") ? savedState.action : "idle");
    const initPixel = qPixel !== null
      ? (qPixel === "1" || qPixel.toLowerCase() === "true")
      : (savedState && typeof savedState.pixelMode === "boolean" ? savedState.pixelMode : false);
    const initSil = qSil !== null
      ? (qSil === "1" || qSil.toLowerCase() === "true")
      : (savedState && typeof savedState.silhouette === "boolean" ? savedState.silhouette : false);
    const initRetro = qRetro !== null
      ? (qRetro === "1" || qRetro.toLowerCase() === "true")
      : (savedState && typeof savedState.retroFps === "boolean" ? savedState.retroFps : false);
    const initSpeed = SPEEDS.includes(qSpeed)
      ? qSpeed
      : (savedState && SPEEDS.includes(savedState.speed) ? savedState.speed : 1);

    const state = {
      ant: initAnt,
      team: initTeam,
      angle: initCompass.angle,
      compass: initCompass,
      flipped: initCompass.flip,
      yaw: initYaw,
      pitch: CANONICAL_PITCH,
      zoom: 1.0,
      idle: initIdle,
      action: initAction,
      pixelMode: initPixel,
      silhouette: initSil,
      retroFps: initRetro,
      speed: initSpeed,
      simTime: Number.isFinite(qTime) && qTime >= 0 ? qTime : 0,
      dark: darkMq.matches,
      soloOrbited: false,
      origBundle: null,
      origMetrics: null,
      origImages: new Map(),
      models: new Map(),
      scenes: new Map(),
      worldVerts: new Map(),
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
    const silCanvas = document.createElement("canvas");
    silCanvas.width = 64;
    silCanvas.height = 64;
    const silCtx = silCanvas.getContext("2d");
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

    function syncCompassFromYaw() {
      const comp = compassForYaw(state.yaw);
      state.compass = comp;
      state.angle = comp.angle;
      state.flipped = comp.flip;
      const offYaw = Math.abs(wrapAngleDeg(state.yaw - comp.yaw)) >= 1.0;
      const offPitch = Math.abs(state.pitch - CANONICAL_PITCH) >= 0.5;
      const offZoom = Math.abs(state.zoom - 1.0) >= 0.01;
      state.soloOrbited = offYaw || offPitch || offZoom;
    }

    function syncStudioLink() {
      if (!studioTab) return;
      const q = new URLSearchParams();
      q.set("ant", state.ant === "all" ? "lineup" : state.ant);
      q.set("team", state.team);
      const exactCanon = !state.flipped && Math.abs(wrapAngleDeg(state.yaw - state.compass.yaw)) < 1.0;
      if (exactCanon) {
        q.set("angle", state.angle);
      } else {
        q.set("yaw", String(Math.round(state.yaw)));
        q.set("proj", "ortho");
      }
      if (state.action === "attack") q.set("action", "attack");
      if (state.silhouette) q.set("silhouette", "1");
      if (state.retroFps) q.set("retrofps", "1");
      studioTab.setAttribute("href", "viewer.html?" + q.toString());
    }

    function activeClipName() {
      return state.action === "attack" ? "Attack" : "Idle";
    }

    function getOrigEntry(cls, angle) {
      if (!state.origBundle) return null;
      const map = state.action === "attack" && state.origBundle.attack_sprites
        ? state.origBundle.attack_sprites
        : state.origBundle.sprites;
      return (map && map[cls] && map[cls][angle])
        || (state.origBundle.sprites && state.origBundle.sprites[cls] && state.origBundle.sprites[cls][angle])
        || null;
    }

    function syncDebugState() {
      window.__antCompare = {
        ready: Boolean(state.origBundle) && state.loadedModels === ANT_CLASSES.length && !state.error,
        mode: state.ant === "all" ? "all" : "solo",
        ant: state.ant,
        team: state.team,
        angle: state.angle,
        compass: state.compass.id,
        dir: state.compass.dir,
        flipped: state.flipped,
        yaw: Number(state.yaw.toFixed(1)),
        pitch: Number(state.pitch.toFixed(1)),
        idle: state.idle,
        action: state.action,
        pixelMode: state.pixelMode,
        silhouette: state.silhouette,
        retroFps: state.retroFps,
        speed: state.speed,
        simTime: Number(state.simTime.toFixed(3)),
        soloOrbited: state.soloOrbited,
        loadedModels: state.loadedModels,
        origLoaded: Boolean(state.origBundle),
        max3dRatio: Number(state.max3dRatio.toFixed(3)),
        scale: state.lastPxScaleLabel,
        error: state.error,
      };
    }

    window.__antViewerGetState = () => ({
      ant: state.ant,
      team: state.team,
      angle: state.angle,
      compassId: state.compass.id,
      yaw: state.yaw,
      idle: state.idle,
      action: state.action,
      pixelMode: state.pixelMode,
      silhouette: state.silhouette,
      retroFps: state.retroFps,
      speed: state.speed,
    });

    function updateCycleLabels() {
      const clipName = activeClipName();
      for (const cls of ANT_CLASSES) {
        const origEntry = getOrigEntry(cls, state.angle);
        const model = state.models.get(cls);
        const origSec = origEntry ? origEntry.total_ms / 1000 : NaN;
        const sel = selectActionAnims(model, clipName);
        let text;
        if (state.action === "attack" && model && !sel.matched) {
          text = `Orig ${formatSec(origSec)} \u00b7 3D Idle (no Attack yet)`;
        } else {
          const modelSec = sel.anims.length ? sel.anims[0].duration : NaN;
          text = `Orig ${formatSec(origSec)} \u00b7 3D ${formatSec(modelSec)}`;
        }
        const cardCy = cardCycles.get(cls);
        if (cardCy) cardCy.textContent = text;
        if (state.ant === cls) {
          soloCycleEl.textContent = text;
          const flipTag = state.flipped ? " flip" : "";
          const stepCount = origEntry && Array.isArray(origEntry.steps) ? origEntry.steps.length : (origEntry ? origEntry.frame_count : 0);
          soloAnimEl.textContent = origEntry ? `${origEntry.anim_name}${flipTag} (${stepCount}f)` : "\u2014";
        }
      }
    }

    function updateUiState() {
      syncCompassFromYaw();
      const comp = state.compass;
      const offYaw = Math.abs(wrapAngleDeg(state.yaw - comp.yaw)) >= 1.0;
      const offPitch = Math.abs(state.pitch - CANONICAL_PITCH) >= 0.5;
      const yawDisp = Math.round(state.yaw);

      toolbarEl.querySelectorAll("button[data-ant]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.ant === state.ant));
      });
      toolbarEl.querySelectorAll("button[data-team]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.team === state.team));
      });
      toolbarEl.querySelectorAll("button[data-angle]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.angle === comp.id));
      });
      toolbarEl.querySelectorAll("button[data-action]").forEach((b) => {
        b.setAttribute("aria-pressed", String(b.dataset.action === state.action));
      });
      toolbarEl.querySelectorAll("button[data-speed]").forEach((b) => {
        b.setAttribute("aria-pressed", String(Number(b.dataset.speed) === state.speed));
      });
      btnIdle.setAttribute("aria-pressed", String(state.idle));
      if (btnPixelMode) btnPixelMode.setAttribute("aria-pressed", String(state.pixelMode));
      if (btnSilhouette) btnSilhouette.setAttribute("aria-pressed", String(state.silhouette));
      if (btnRetroFps) btnRetroFps.setAttribute("aria-pressed", String(state.retroFps));

      const isSolo = state.ant !== "all";
      gridEl.hidden = isSolo;
      soloEl.hidden = !isSolo;

      badgeAnt.textContent = isSolo ? ANT_LABELS[state.ant] : "All 6";
      badgeTeam.textContent = TEAMS[state.team].label;
      badgeAngle.textContent = offYaw
        ? `${comp.dir} (${comp.angle}${comp.flip ? " flip" : ""}) \u00b7 ${yawDisp}\u00b0`
        : comp.label;
      if (metaProj) {
        const modeBits = [
          `Ortho ${Math.round(CANONICAL_PITCH)}\u00b0 (Locked)`,
          state.pixelMode ? "1\u00d7 Pixel" : "Shared Ground",
        ];
        if (state.silhouette) modeBits.push("Silhouette");
        metaProj.textContent = modeBits.join(" \u00b7 ");
      }
      const actionLabel = state.action === "attack" ? "Attack" : "Idle";
      metaPlay.textContent = `${actionLabel} \u00b7 ${state.idle ? "Playing" : "Paused"} \u00b7 ${state.speed}\u00d7${state.retroFps ? " \u00b7 1998 FPS" : ""}`;

      const origCardLabel = comp.flip ? "1998 Orig \u00b7 flip" : "1998 Original";
      for (const pill of cardOrigPills) pill.textContent = origCardLabel;
      const base3d = state.pixelMode ? "Live 3D \u00b7 1\u00d7" : "Live 3D";
      const card3dLabel = offYaw || comp.flip ? `${base3d} \u00b7 ${yawDisp}\u00b0` : base3d;
      for (const pill of card3dPills) pill.textContent = card3dLabel;

      if (isSolo) {
        soloTitleEl.textContent = ANT_LABELS[state.ant] + " Ant";
        btnSoloReset.disabled = !state.soloOrbited;
        solo3dPill.textContent = offPitch
          ? `${base3d} \u00b7 Yaw ${yawDisp}\u00b0 Pitch ${Math.round(state.pitch)}\u00b0`
          : (offYaw ? `${base3d} \u00b7 ${comp.dir} ${yawDisp}\u00b0` : `${base3d} \u00b7 ${comp.label}`);
      }

      updateCycleLabels();
      syncStudioLink();
      syncDebugState();
    }

    function resetSoloCamera() {
      state.yaw = state.compass.yaw;
      state.pitch = CANONICAL_PITCH;
      state.zoom = 1.0;
      syncCompassFromYaw();
    }

    function selectAnt(id) {
      const next = ANT_CLASSES.includes(id) ? id : "all";
      const changed = state.ant !== next;
      state.ant = next;
      state.pitch = CANONICAL_PITCH;
      state.zoom = 1.0;
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
      const comp = resolveCompassQuery(angleKey);
      if (!comp) return;
      state.yaw = comp.yaw;
      state.pitch = CANONICAL_PITCH;
      state.zoom = 1.0;
      updateUiState();
      markDirty();
      announce("Angle set to " + comp.full + ".");
    }

    function selectAction(next) {
      const target = next === "attack" ? "attack" : "idle";
      if (state.action === target) return;
      state.action = target;
      state.simTime = 0;
      updateUiState();
      markDirty();
      if (target === "attack") {
        const checkIds = state.ant === "all" ? ANT_CLASSES : [state.ant];
        const anyAttack = checkIds.some((id) => selectActionAnims(state.models.get(id), "Attack").matched);
        announce(anyAttack
          ? "Action set to Attack sequence."
          : "Action set to 1998 Attack sequence (3D falls back to Idle until Attack clips are built).");
      } else {
        announce("Action set to Idle stance.");
      }
    }

    function toggleAction() {
      selectAction(state.action === "attack" ? "idle" : "attack");
    }

    function toggleIdle() {
      state.idle = !state.idle;
      updateUiState();
      markDirty();
      const label = state.action === "attack" ? "Attack" : "Idle";
      announce(state.idle ? `${label} playback resumed at ${state.speed}x.` : `${label} playback paused.`);
    }

    function togglePixelMode() {
      state.pixelMode = !state.pixelMode;
      updateUiState();
      markDirty();
      announce(state.pixelMode ? "3D side switched to 1x 1998 pixel resolution." : "3D side switched to HD resolution.");
    }

    function toggleSilhouette() {
      state.silhouette = !state.silhouette;
      updateUiState();
      markDirty();
      announce(state.silhouette ? "Silhouette view enabled on both sides." : "Silhouette view disabled.");
    }

    function toggleRetroFps() {
      state.retroFps = !state.retroFps;
      updateUiState();
      markDirty();
      if (!state.retroFps) {
        announce("1998 FPS off (smooth 60 fps 3D playback).");
        return;
      }
      const step = RETRO_STEP_SEC[state.ant];
      if (step) {
        const fps = (1 / step).toFixed(1).replace(/\.0$/, "");
        announce(`1998 FPS on (${ANT_LABELS[state.ant]} \u00b7 ${fps} fps, ${Math.round(step * 1000)}ms step).`);
      } else {
        announce("1998 FPS on (6.7\u201310 fps, frame-locked to 1998 sprites).");
      }
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
      const ry = Math.round(wrapAngleDeg(yawDeg));
      const rp = Math.round(pitchDeg);
      const key = `${cls}:${ry}:${rp}`;
      if (state.spanCache.has(key)) return state.spanCache.get(key);
      if (state.spanCache.size > 480) state.spanCache.clear();
      const sc = state.scenes.get(cls);
      if (!sc) return { vMin: -0.1, vMax: 1.0, span: 1.1 };
      const p = rp * DEG, y = ry * DEG;
      const ux = -Math.sin(p) * Math.sin(y), uy = Math.cos(p), uz = -Math.sin(p) * Math.cos(y);
      let vMin = Infinity, vMax = -Infinity;
      const pts = state.worldVerts.get(cls);
      if (pts && pts.length >= 3) {
        for (let i = 0; i < pts.length; i += 3) {
          const v = pts[i] * ux + pts[i + 1] * uy + pts[i + 2] * uz;
          if (v < vMin) vMin = v;
          if (v > vMax) vMax = v;
        }
      } else {
        const inst = sc.instances[0];
        for (const dc of inst.model.drawCalls) {
          const m = mat4Multiply(inst.offsetMat, dc.worldMat);
          const lmin = dc.prim.localMin, lmax = dc.prim.localMax;
          const cx = (lmin[0] + lmax[0]) * 0.5, cy = (lmin[1] + lmax[1]) * 0.5, cz = (lmin[2] + lmax[2]) * 0.5;
          const hx = (lmax[0] - lmin[0]) * 0.5, hy = (lmax[1] - lmin[1]) * 0.5, hz = (lmax[2] - lmin[2]) * 0.5;
          const wc = transformPoint(m, cx, cy, cz);
          const d0 = Math.abs((m[0] * ux + m[1] * uy + m[2] * uz) * hx);
          const d1 = Math.abs((m[4] * ux + m[5] * uy + m[6] * uz) * hy);
          const d2 = Math.abs((m[8] * ux + m[9] * uy + m[10] * uz) * hz);
          const rad = d0 + d1 + d2;
          const vc = wc[0] * ux + wc[1] * uy + wc[2] * uz;
          if (vc - rad < vMin) vMin = vc - rad;
          if (vc + rad > vMax) vMax = vc + rad;
        }
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
      const key = `${state.action}:${cls}:${angle}:${team}`;
      const existing = state.origImages.get(key);
      if (existing) return existing;
      const entry = getOrigEntry(cls, angle);
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
      if (panelH > panelW * 1.35) return state.action === "attack" ? 0.68 : 0.72;
      return state.action === "attack" ? ATTACK_GROUND_FRAC : GROUND_FRAC;
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
      const entry = getOrigEntry(cls, state.angle);
      if (!entry) return;
      const rec = getOrigImage(cls, state.angle, state.team);
      if (!rec || !rec.ready) return;
      const frameIdx = pickOrigFrame(entry, state.simTime);
      const dw = entry.fw * pxScale;
      const dh = entry.fh * pxScale;
      let dx = Math.round(x0 + (panelW - dw) * 0.5);
      if (state.action === "attack" && Number.isFinite(entry.anchor_x)) {
        const ax = entry.anchor_x * pxScale;
        const rawX = state.flipped
          ? Math.round(x0 + panelW * 0.5 + ax - dw)
          : Math.round(x0 + panelW * 0.5 - ax);
        dx = dw <= panelW - 4 ? clamp(rawX, x0 + 2, x0 + panelW - dw - 2) : Math.round(x0 + (panelW - dw) * 0.5);
      }
      const dy = Math.round(groundY - entry.foot_y * pxScale);
      const shadowRx = Math.max(12, Math.min(panelW * 0.34, entry.vis_w * pxScale * 0.42));
      drawContactShadow(ctx, x0 + panelW * 0.5, groundY, shadowRx, Math.max(4, shadowRx * 0.22));
      let srcImg = rec.img, sx = frameIdx * entry.fw;
      if (state.silhouette) {
        if (silCanvas.width !== entry.fw || silCanvas.height !== entry.fh) {
          silCanvas.width = entry.fw;
          silCanvas.height = entry.fh;
        }
        silCtx.globalCompositeOperation = "source-over";
        silCtx.clearRect(0, 0, entry.fw, entry.fh);
        silCtx.drawImage(rec.img, sx, 0, entry.fw, entry.fh, 0, 0, entry.fw, entry.fh);
        silCtx.globalCompositeOperation = "source-in";
        silCtx.fillStyle = state.dark ? "#e8eef8" : "#11161f";
        silCtx.fillRect(0, 0, entry.fw, entry.fh);
        srcImg = silCanvas;
        sx = 0;
      }
      ctx.save();
      ctx.beginPath();
      ctx.rect(x0, 0, panelW, ctx.canvas.height);
      ctx.clip();
      ctx.imageSmoothingEnabled = false;
      if (state.flipped) {
        ctx.translate(dx + dw, dy);
        ctx.scale(-1, 1);
        ctx.drawImage(srcImg, sx, 0, entry.fw, entry.fh, 0, 0, dw, dh);
      } else {
        ctx.drawImage(srcImg, sx, 0, entry.fw, entry.fh, dx, dy, dw, dh);
      }
      ctx.restore();
    }

    function ensureOffscreenSize(w, h) {
      if (offCanvas.width < w || offCanvas.height < h || offCanvas.width > w * 2.2 || offCanvas.height > h * 2.2) {
        offCanvas.width = Math.max(w, offCanvas.width > w * 2.2 ? w : offCanvas.width);
        offCanvas.height = Math.max(h, offCanvas.height > h * 2.2 ? h : offCanvas.height);
      }
    }

    function sampled3dTiming(cls) {
      if (!state.retroFps) return { idleTime: state.simTime, stepSec: 0 };
      const origEntry = getOrigEntry(cls, state.angle);
      if (origEntry && Array.isArray(origEntry.steps) && origEntry.steps.length > 1 && origEntry.total_ms) {
        const st = pickOrigStep(origEntry, state.simTime);
        return { idleTime: st.quantizedSec, stepSec: st.stepDurSec };
      }
      const stepTable = state.action === "attack" ? RETRO_ATTACK_STEP_SEC : RETRO_STEP_SEC;
      const step = stepTable[cls] || 0.125;
      return { idleTime: Math.floor(state.simTime / step) * step, stepSec: step };
    }

    function draw3dAnt(ctx, cls, x0, panelW, panelH, groundY, fixedHalfH, pxScale, cam) {
      const sc = state.scenes.get(cls);
      if (!sc) return;
      const anchorYaw = cam.yaw;
      const anchorPitch = CANONICAL_PITCH;
      const span = computeSpan(cls, anchorYaw, anchorPitch);

      const shadowRx = Math.max(12, Math.min(panelW * 0.34, (panelH / (2 * fixedHalfH)) * sc.instances[0].model.size[0] * 0.38));
      drawContactShadow(ctx, x0 + panelW * 0.5, groundY, shadowRx, Math.max(4, shadowRx * 0.22));

      const timing = sampled3dTiming(cls);
      const inst = sc.instances[0];
      const bl = inst && inst.blink;
      let savedBlink = null;
      if (state.retroFps && bl && bl.active) {
        savedBlink = bl.elapsed;
        const bi = inst.model.blinkInfo;
        const pulseDur = bi && bi.anim ? (bi.endTime - bi.startTime) : 0.22;
        const bStep = Math.min(timing.stepSec || 0.125, Math.max(0.04, pulseDur * 0.5));
        bl.elapsed = Math.floor(savedBlink / bStep) * bStep;
      }

      try {
        if (state.pixelMode && pxScale > 1) {
          const lowW = Math.max(16, Math.floor(panelW / pxScale));
          const lowH = Math.max(16, Math.floor(panelH / pxScale));
          const lowGroundY = Math.min(lowH - 1, Math.max(1, Math.round(groundY / pxScale)));
          const lowGFrac = lowGroundY / lowH;
          const lowFixedHalfH = fixedHalfH * ((lowH * pxScale) / panelH);
          const targetY = (span.vMin + lowFixedHalfH * cam.zoom * (2 * lowGFrac - 1)) / Math.cos(anchorPitch * DEG);
          ensureOffscreenSize(lowW, lowH);
          renderer.drawScene(sc, cam, {
            width: lowW,
            height: lowH,
            transparent: true,
            showPlinth: false,
            dark: state.dark,
            team: state.team,
            blinkMode: "auto",
            idleAnim: true,
            clipName: activeClipName(),
            fixedIdleTime: timing.idleTime,
            silhouette: state.silhouette,
            overrideProj: "ortho",
            fixedHalfH: lowFixedHalfH,
            customTarget: [0, targetY, 0],
          });
          const dstW = lowW * pxScale;
          const dstH = lowH * pxScale;
          const dstX = Math.round(x0 + (panelW - dstW) * 0.5);
          const dstY = Math.round(groundY - lowGroundY * pxScale);
          ctx.save();
          ctx.beginPath();
          ctx.rect(x0, 0, panelW, panelH);
          ctx.clip();
          ctx.imageSmoothingEnabled = false;
          ctx.drawImage(offCanvas, 0, offCanvas.height - lowH, lowW, lowH, dstX, dstY, dstW, dstH);
          ctx.restore();
          return;
        }

        ensureOffscreenSize(panelW, panelH);
        const gFrac = groundY / panelH;
        const targetY = (span.vMin + fixedHalfH * cam.zoom * (2 * gFrac - 1)) / Math.cos(anchorPitch * DEG);
        renderer.drawScene(sc, cam, {
          width: panelW,
          height: panelH,
          transparent: true,
          showPlinth: false,
          dark: state.dark,
          team: state.team,
          blinkMode: "auto",
          idleAnim: true,
          clipName: activeClipName(),
          fixedIdleTime: timing.idleTime,
          silhouette: state.silhouette,
          overrideProj: "ortho",
          fixedHalfH,
          customTarget: [0, targetY, 0],
        });
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage(offCanvas, 0, offCanvas.height - panelH, panelW, panelH, x0, 0, panelW, panelH);
      } finally {
        if (savedBlink !== null && bl) bl.elapsed = savedBlink;
      }
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
      const gridCam = { yaw: state.yaw, pitch: CANONICAL_PITCH, zoom: 1.0, projection: "ortho" };
      const soloCam = { yaw: state.yaw, pitch: state.pitch, zoom: state.zoom, projection: "ortho" };

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
          const groundY = Math.round(sz.h * scParams.gFrac);
          const rightW = sz.w - sz.halfW;
          const halfHForCard = scParams.fixedHalfH * (sz.h / minH);
          drawStudioBackdrop(rec.ctx, sz.w, sz.h, groundY, true);
          drawOrigSprite(rec.ctx, cls, 0, sz.halfW, groundY, scParams.pxScale);
          draw3dAnt(rec.ctx, cls, sz.halfW, rightW, sz.h, groundY, halfHForCard, scParams.pxScale, gridCam);
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
        soloOrigPill.textContent = state.flipped
          ? `1998 Original \u00b7 flip \u00b7 ${scParams.scaleText}`
          : `1998 Original \u00b7 ${scParams.scaleText}`;

        const gOrig = Math.round(oh * scParams.gFrac);
        drawStudioBackdrop(soloOrigCtx, ow, oh, gOrig, false);
        drawOrigSprite(soloOrigCtx, state.ant, 0, ow, gOrig, scParams.pxScale);

        const g3d = Math.round(rh * scParams.gFrac);
        drawStudioBackdrop(solo3dCtx, rw, rh, g3d, false);
        draw3dAnt(solo3dCtx, state.ant, 0, rw, rh, g3d, scParams.fixedHalfH * (rh / panelH), scParams.pxScale, soloCam);
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
        const verts = extractRestWorldVerts(model, offsetMat);
        if (verts) state.worldVerts.set(cls, verts);
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
      else if (btn.dataset.action) selectAction(btn.dataset.action);
      else if (btn.dataset.speed) selectSpeed(Number(btn.dataset.speed));
      else if (btn === btnPixelMode) togglePixelMode();
      else if (btn === btnSilhouette) toggleSilhouette();
      else if (btn === btnRetroFps) toggleRetroFps();
      else if (btn === btnIdle) toggleIdle();
      else if (btn === btnSyncReset) syncReset();
    });

    let suppressStageClickUntil = 0;
    function attachHorizontalDrag(el, matchFn, sensitivity) {
      let drag = null;
      el.addEventListener("pointerdown", (e) => {
        if (e.button !== 0 && e.pointerType === "mouse") return;
        const target = matchFn(e.target);
        if (!target) return;
        drag = { id: e.pointerId, target, startX: e.clientX, startY: e.clientY, lastX: e.clientX, active: false };
      });
      el.addEventListener("pointermove", (e) => {
        if (!drag || e.pointerId !== drag.id) return;
        const totalDx = Math.abs(e.clientX - drag.startX);
        const totalDy = Math.abs(e.clientY - drag.startY);
        if (!drag.active) {
          if (e.pointerType === "touch" && totalDy > 10 && totalDy > totalDx * 1.2) { drag = null; return; }
          if (totalDx <= 6) return;
          drag.active = true;
          try { el.setPointerCapture(e.pointerId); } catch (_) {}
        }
        const dx = e.clientX - drag.lastX;
        drag.lastX = e.clientX;
        state.yaw = wrapAngleDeg(state.yaw - dx * sensitivity);
        updateUiState();
        markDirty();
      });
      const finish = (e) => {
        if (!drag || e.pointerId !== drag.id) return;
        if (drag.active) suppressStageClickUntil = performance.now() + 260;
        drag = null;
      };
      el.addEventListener("pointerup", finish);
      el.addEventListener("pointercancel", finish);
      el.addEventListener("lostpointercapture", finish);
    }

    attachHorizontalDrag(gridEl, (t) => t.closest("[data-stage-ant]"), 0.55);
    attachHorizontalDrag(soloOrigCanvas, () => soloOrigCanvas, 0.5);

    gridEl.addEventListener("click", (e) => {
      const focusBtn = e.target.closest("button[data-focus-ant]");
      if (focusBtn) { selectAnt(focusBtn.dataset.focusAnt); return; }
      if (performance.now() < suppressStageClickUntil) return;
      const stageHit = e.target.closest("[data-stage-ant]");
      if (stageHit) selectAnt(stageHit.dataset.stageAnt);
    });

    btnBackAll.addEventListener("click", () => selectAnt("all"));
    btnSoloReset.addEventListener("click", () => {
      resetSoloCamera();
      updateUiState();
      markDirty();
      announce("3D camera reset to " + state.compass.full + ".");
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
      const dx = e.clientX - prev.x;
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pointers.size === 1) {
        state.yaw = wrapAngleDeg(state.yaw - dx * 0.45);
        state.pitch = CANONICAL_PITCH;
        updateUiState();
        markDirty();
      } else if (pointers.size === 2) {
        const dist = pinchDist();
        if (lastPinch > 1 && dist > 1) {
          state.zoom = clamp(state.zoom * (lastPinch / dist), 0.45, 2.5);
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
      state.zoom = clamp(state.zoom * Math.exp(e.deltaY * 0.0012), 0.45, 2.5);
      updateUiState();
      markDirty();
    }, { passive: false });

    solo3dCanvas.addEventListener("keydown", (e) => {
      const sy = e.shiftKey ? 4 : 12;
      if (e.key === "ArrowLeft") state.yaw = wrapAngleDeg(state.yaw + sy);
      else if (e.key === "ArrowRight") state.yaw = wrapAngleDeg(state.yaw - sy);
      else if (e.key === "+" || e.key === "=") state.zoom = clamp(state.zoom * 0.88, 0.45, 2.5);
      else if (e.key === "-" || e.key === "_") state.zoom = clamp(state.zoom * 1.14, 0.45, 2.5);
      else if (e.key === "r" || e.key === "R") {
        e.preventDefault();
        resetSoloCamera();
        updateUiState();
        markDirty();
        announce("3D camera reset to " + state.compass.full + ".");
        return;
      } else return;
      e.preventDefault();
      updateUiState();
      markDirty();
    });

    document.addEventListener("keydown", (e) => {
      if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
      if (e.key === "Escape" && state.ant !== "all") {
        e.preventDefault();
        selectAnt("all");
      } else if (e.key === "a" || e.key === "A") {
        e.preventDefault();
        toggleAction();
      } else if (e.key === "i" || e.key === "I") {
        e.preventDefault();
        toggleIdle();
      } else if (e.key === "p" || e.key === "P") {
        e.preventDefault();
        togglePixelMode();
      } else if (e.key === "s" || e.key === "S") {
        e.preventDefault();
        toggleSilhouette();
      } else if (e.key === "f" || e.key === "F") {
        e.preventDefault();
        toggleRetroFps();
      } else if ((e.key === "r" || e.key === "R") && document.activeElement !== solo3dCanvas) {
        e.preventDefault();
        syncReset();
      } else if (e.key >= "1" && e.key <= "8") {
        e.preventDefault();
        selectAngle(COMPASS_DIRS[Number(e.key) - 1].id);
      } else if ((e.key === "ArrowLeft" || e.key === "ArrowRight")
        && document.activeElement !== solo3dCanvas
        && (!toolbarEl.contains(document.activeElement))) {
        e.preventDefault();
        const step = e.shiftKey ? 5 : 15;
        state.yaw = wrapAngleDeg(state.yaw + (e.key === "ArrowLeft" ? step : -step));
        updateUiState();
        markDirty();
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

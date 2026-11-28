"use strict";

// Live Playable 3D Browser RTS Client for Ants (1998).
// Connects to tools/ants_3d_bridge via binary WebSocket UTF-8 JSON frames (or runs ?mock=1 20 Hz harness).
(() => {
  const { clamp, mat4FromTRS, mat4Identity, parseGlb } = window.__antGltf;
  const { DEG, TEAMS, validLighting, wrapAngleDeg, AntRenderer } = window.__antRender;
  const { buildCarriedLunchboxMat } = window.__demoHeist;
  const { buildBorderGrassMesh, buildBatchedMapBombsMesh } = window.__demoBorder;
  const { buildGroundScatterMesh, buildAtmoParticles, buildColonyAntsForMap, updateColonyAnts } = window.__demoAtmo;
  const { SEAT_TEAMS, SEAT_LABELS, CLASS_META, BOMB_IDS, HILL_IDS, createHud, createFxAssets, pushExplosionFx, createMockMatch } = window.__playHud;

  const $ = (id) => document.getElementById(id);
  const ANT_SCALE = 1.15;
  const SWIMMER_SUBMERGE_Y = -1.05 * ANT_SCALE;
  const BASE_FIT_RADIUS = 11.0;

  // Frozen wire cls_idx order (ants_3d_bridge, board #556): 0=worker, 1=combat, 2=bomber, 3=swimmer, 4=fire, 5=thief.
  let antClassByIdx = ["worker", "combat", "bomber", "swimmer", "fire", "thief"];
  const ALL_ANT_CLASSES = ["worker", "combat", "bomber", "swimmer", "fire", "thief"];
  const DROPPER_FD_IDS = ["FD_BOMB", "FD_COMB", "FD_THIEF", "FD_SWIM", "FD_FIRE"];
  // 8-way compass facing (0=N..7=NW, include/ants_assets/mirroring.hpp) -> 3D +Y yaw (South/+Z = 0).
  const FACING_YAW = [Math.PI, 0.75 * Math.PI, 0.5 * Math.PI, 0.25 * Math.PI, 0, -0.25 * Math.PI, -0.5 * Math.PI, -0.75 * Math.PI];

  const CAM_PRESETS = {
    game:  { label: "Game View", yaw:   0, pitch: 50, zoom: 0.82, proj: "ortho" },
    rts3d: { label: "3D RTS",    yaw: -14, pitch: 42, zoom: 0.76, proj: "perspective" },
    hero:  { label: "Low Hero",  yaw: -22, pitch: 24, zoom: 0.54, proj: "perspective" },
    top:   { label: "Top-Down",  yaw:   0, pitch: 80, zoom: 0.96, proj: "ortho" },
  };
  const CAM_ORDER = ["rts3d", "game", "hero", "top"];

  const LIGHT_SCENARIOS = {
    daylight:  { label: "Daylight",  elev: 58, azim: -36, shadowStr: 0.36, gridAlpha: 0.12, gridCol: [0.05, 0.07, 0.06], glowMode: "none" },
    golden:    { label: "Sunset",    elev: 28, azim: -64, shadowStr: 0.48, gridAlpha: 0.12, gridCol: [0.16, 0.08, 0.05], glowMode: "warm" },
    night:     { label: "Night",     elev: 44, azim:  34, shadowStr: 0.46, gridAlpha: 0.16, gridCol: [0.22, 0.34, 0.58], glowMode: "night" },
    moonlight: { label: "Moonlight", elev: 48, azim: -32, shadowStr: 0.44, gridAlpha: 0.16, gridCol: [0.30, 0.46, 0.74], glowMode: "moon" },
    neon:      { label: "Neon",      elev: 46, azim: -48, shadowStr: 0.42, gridAlpha: 0.36, gridCol: [0.10, 0.92, 0.98], glowMode: "neon" },
    overcast:  { label: "Overcast",  elev: 64, azim: -25, shadowStr: 0.22, gridAlpha: 0.12, gridCol: [0.08, 0.10, 0.12], glowMode: "none" },
    flat:      { label: "1998 Flat", elev: 58, azim: -36, shadowStr: 0.26, gridAlpha: 0.14, gridCol: [0.05, 0.07, 0.06], glowMode: "none" },
  };
  const LIGHT_ORDER = ["daylight", "golden", "night", "moonlight", "neon", "overcast", "flat"];

  // --- Math Helpers (4x4 Matrix Inverse, Raycast, Angles) ---
  function mat4Invert(m) {
    const out = new Float32Array(16);
    const [m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33] = m;
    const b00 = m00 * m11 - m01 * m10, b01 = m00 * m12 - m02 * m10, b02 = m00 * m13 - m03 * m10;
    const b03 = m01 * m12 - m02 * m11, b04 = m01 * m13 - m03 * m11, b05 = m02 * m13 - m03 * m12;
    const b06 = m20 * m31 - m21 * m30, b07 = m20 * m32 - m22 * m30, b08 = m20 * m33 - m23 * m30;
    const b09 = m21 * m32 - m22 * m31, b10 = m21 * m33 - m23 * m31, b11 = m22 * m33 - m23 * m32;
    const det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (Math.abs(det) < 1e-10) return null;
    const inv = 1.0 / det;
    out[0] = (m11 * b11 - m12 * b10 + m13 * b09) * inv; out[1] = (m02 * b10 - m01 * b11 - m03 * b09) * inv;
    out[2] = (m31 * b05 - m32 * b04 + m33 * b03) * inv; out[3] = (m22 * b04 - m21 * b05 - m23 * b03) * inv;
    out[4] = (m12 * b08 - m10 * b11 - m13 * b07) * inv; out[5] = (m00 * b11 - m02 * b08 + m03 * b07) * inv;
    out[6] = (m32 * b02 - m30 * b05 - m33 * b01) * inv; out[7] = (m20 * b05 - m22 * b02 + m23 * b01) * inv;
    out[8] = (m10 * b10 - m11 * b08 + m13 * b06) * inv; out[9] = (m01 * b08 - m00 * b10 - m03 * b06) * inv;
    out[10] = (m30 * b04 - m31 * b02 + m33 * b00) * inv; out[11] = (m21 * b02 - m20 * b04 - m23 * b00) * inv;
    out[12] = (m11 * b07 - m10 * b09 - m12 * b06) * inv; out[13] = (m00 * b09 - m01 * b07 + m02 * b06) * inv;
    out[14] = (m31 * b01 - m30 * b03 - m32 * b00) * inv; out[15] = (m20 * b03 - m21 * b01 + m22 * b00) * inv;
    return out;
  }

  function unprojectNdc(invVP, nx, ny, nz) {
    const x = invVP[0] * nx + invVP[4] * ny + invVP[8] * nz + invVP[12];
    const y = invVP[1] * nx + invVP[5] * ny + invVP[9] * nz + invVP[13];
    const z = invVP[2] * nx + invVP[6] * ny + invVP[10] * nz + invVP[14];
    const w = invVP[3] * nx + invVP[7] * ny + invVP[11] * nz + invVP[15];
    return Math.abs(w) < 1e-8 ? null : [x / w, y / w, z / w];
  }

  function lerpAngleRad(cur, target, t) {
    const diff = ((target - cur + Math.PI) % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI) - Math.PI;
    return cur + diff * t;
  }

  function buildWorldMat(model, x, z, scale, yawRad, yLift = 0) {
    const minY = model && model.bounds && model.bounds.min ? model.bounds.min[1] : 0;
    const h = yawRad * 0.5;
    return mat4FromTRS([x, -minY * scale + yLift, z], [0, Math.sin(h), 0, Math.cos(h)], [scale, scale, scale]);
  }

  function sunVectorFromAngles(elevDeg, azimDeg) {
    const el = elevDeg * DEG, az = azimDeg * DEG, ce = Math.cos(el);
    return [ce * Math.sin(az), Math.sin(el), -ce * Math.cos(az)];
  }

  function resolveAntClip(cls, state, action, swimming, drowning, stunned, burnMs, moving) {
    if (drowning || state === 10 || action === 0x0F) return "Drown";
    if (state === 17) return "Dive";
    if (state === 18) return "Surface";
    if (action === 0x14) return "Hatch";
    if (state === 8 || action === 0x02) return "Enter";
    if (state === 16 || action === 0x0D) return "Steal";
    if (state === 21 || action === 0x08) return "PlantBomb";
    if (state === 22 || action === 0x09) return "DefuseBomb";
    if (state === 23 || action === 0x06) return "StartFire";
    if (state === 24 || action === 0x07) return "Extinguish";
    if (state === 19 || state === 20 || action === 0x10 || action === 0x11) return swimming ? "ShovelWater" : "Shovel";
    if (state === 2 || action === 0x12) return "Attack";
    if (state === 26 || action === 0x05) return "Harvest";
    if (state === 25 || action === 0x0B) return "CantGo";
    if (action === 0x0A || (burnMs > 0 && burnMs < 420)) return "BlownUp";
    if (state === 5 || action === 0x13) return "BlownBack";
    if (state === 4 || action === 0x0E) return "Hit";
    if (stunned || state === 7 || state === 27 || action === 0x03) return "Stunned";
    if (swimming || state === 15) return "Swim";
    if (state === 1 || action === 0x01 || moving) return "Walk";
    return cls === "thief" ? "IdleLookout" : "Idle";
  }

  // --- Core App State & Renderer ---
  const params = new URLSearchParams(window.location.search);
  const canvas = $("play-canvas");
  const renderer = new AntRenderer(canvas);
  const gl = renderer.gl;
  const fxAssets = createFxAssets(gl);

  const initCamKey = CAM_PRESETS[params.get("cam")] ? params.get("cam") : "rts3d";
  const initLightKey = LIGHT_SCENARIOS[params.get("light")] ? params.get("light") : "daylight";
  const cam = {
    yaw: CAM_PRESETS[initCamKey].yaw, pitch: CAM_PRESETS[initCamKey].pitch,
    zoom: CAM_PRESETS[initCamKey].zoom, projection: CAM_PRESETS[initCamKey].proj,
    target: [30.0, 0.3, 30.0],
  };
  let camGoal = null, lastCamera = null, lastInvVP = null, lastCanvasRect = null;

  const uiState = {
    camPreset: initCamKey, lighting: initLightKey,
    showGrid: params.get("grid") === "1", castShadows: params.get("shadows") !== "0",
    hiResTiles: params.get("hires") !== "0", abilityMode: false, boxSelectMode: false,
    simTime: 0, seat: 0, paused: false, fog: false, bots: "medium", seed: 1,
    activeSeats: 0, mode: "connecting",
  };

  // --- Model Cache & Calibrated 1998 Item Metrics ---
  const modelCache = new Map();
  const itemMetrics = new Map();
  let mapsCatalog = null, atlasMaskImg = null, baseAssetsReady = null;

  function loadImage(url) {
    return new Promise((resolve, reject) => {
      const img = new Image(); img.decoding = "async";
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(`Failed to load ${url}`));
      img.src = url;
    });
  }

  function loadModel(key, url) {
    if (modelCache.has(key)) return modelCache.get(key);
    const p = (async () => {
      for (let attempt = 0; attempt < 2; attempt++) {
        try {
          const res = await fetch(url);
          if (!res.ok) throw new Error(`HTTP ${res.status}`);
          const m = await parseGlb(await res.arrayBuffer(), gl);
          p._resolved = m;
          return m;
        } catch (_) {
          if (attempt === 1) { modelCache.delete(key); return null; }
          await new Promise((r) => setTimeout(r, 80));
        }
      }
      return null;
    })();
    modelCache.set(key, p);
    return p;
  }

  function getCachedModel(key, url) {
    const entry = modelCache.get(key);
    if (entry && entry._resolved) return entry._resolved;
    if (!entry) loadModel(key, url);
    return null;
  }

  function registerMetricsFromCatalog(catalog, manifest, origItems) {
    for (const m of (catalog && catalog.maps ? catalog.maps : [])) {
      for (const o of (m.objects || [])) {
        const low = o.id.toLowerCase();
        if (!itemMetrics.has(low)) itemMetrics.set(low, { id: o.id, ox: o.x - o.col, oz: o.z - o.row, scale: o.s });
      }
    }
    const origMap = (origItems && origItems.items) || {};
    for (const it of (manifest && manifest.items ? manifest.items : [])) {
      const low = it.id.toLowerCase();
      if (itemMetrics.has(low)) continue;
      const sx = Math.max(0.08, Number(it.size && it.size[0]) || 1);
      const sy = Math.max(0.08, Number(it.size && it.size[1]) || 1);
      const sz = Math.max(0.08, Number(it.size && it.size[2]) || 1);
      const fr = ((origMap[it.id] || origMap[low]) || {}).frames?.[0];
      const wT = Math.max(0.15, (fr ? fr.w : 32) / 32), hT = Math.max(0.15, (fr ? fr.h : 32) / 32);
      let ox = 0.5, oz = 0.5, scale = 1.0;
      if (low.endsWith("hill")) { scale = 4.0 / sx; ox = 0.88; oz = 1.06; }
      else if (low.endsWith("bomb")) scale = 0.58;
      else if (low === "wallup04") scale = 1.06;
      else if (low === "lunchbox") scale = 0.53;
      else if (low.startsWith("pu_")) scale = Math.min(wT / sx, hT / (sz + 0.78 * sy)) * 1.04;
      else if (low.startsWith("flower") || low.startsWith("clover") || low.startsWith("dflower")) scale = Math.min((wT / sx) * 1.02, (hT / (sz + 0.78 * sy)) * 1.04);
      else { scale = Math.min((wT / sx) * 1.04, (hT / (sz + 0.78 * sy)) * 1.06); oz = 0.5 + 0.39 * sy * scale; }
      itemMetrics.set(low, { id: it.id, ox, oz, scale });
    }
  }

  function getItemPlacement(rawId, col, row) {
    const low = String(rawId || "").toLowerCase(), m = itemMetrics.get(low);
    if (m) return { id: m.id, x: col + m.ox, z: row + m.oz, s: m.scale };
    return { id: rawId, x: col + 0.5, z: row + 0.5, s: low.endsWith("bomb") ? 0.58 : (low === "wallup04" ? 1.06 : 1.0) };
  }

  function buildCanvasFallbackMask(cols, rows, tiles, maskImg) {
    const cellPx = 8, raw = document.createElement("canvas");
    raw.width = cols * cellPx; raw.height = rows * cellPx;
    const rctx = raw.getContext("2d"), aCols = (mapsCatalog && mapsCatalog.atlas_cols) || 16;
    const srcCell = Math.floor(maskImg.width / aCols);
    for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
      const ti = clamp(tiles[r * cols + c] || 54, 0, 238);
      rctx.drawImage(maskImg, (ti % aCols) * srcCell, Math.floor(ti / aCols) * srcCell, srcCell, srcCell, c * cellPx, r * cellPx, cellPx, cellPx);
    }
    const out = document.createElement("canvas"); out.width = raw.width; out.height = raw.height;
    const octx = out.getContext("2d"); octx.filter = "blur(2.2px)"; octx.drawImage(raw, 0, 0);
    return out;
  }

  async function ensureBaseAssets() {
    if (baseAssetsReady) return baseAssetsReady;
    baseAssetsReady = (async () => {
      const [atlasImg, maskImg, cat, man, orig] = await Promise.all([
        loadImage("models/demo/ground.png"),
        loadImage("models/demo/ground_masks.png"),
        fetch("models/demo/maps.json").then((r) => (r.ok ? r.json() : null)).catch(() => null),
        fetch("models/items/manifest.json").then((r) => (r.ok ? r.json() : null)).catch(() => null),
        fetch("models/items/orig_items.json").then((r) => (r.ok ? r.json() : null)).catch(() => null),
      ]);
      renderer.uploadTileAtlas(atlasImg);
      atlasMaskImg = maskImg;
      renderer.uploadMaskAtlas(maskImg);
      mapsCatalog = cat;
      registerMetricsFromCatalog(cat, man, orig);
      const core = [
        ...ALL_ANT_CLASSES.map((c) => [`ant:${c}`, `models/${c}.glb`]),
        ...["lunchbox", "wallup04", "dflower1", ...BOMB_IDS, ...HILL_IDS, ...DROPPER_FD_IDS,
          "pu_mason", "pu_swim", "pu_bomb", "pu_comb", "pu_thief"].map((id) => [`item:${id}`, `models/items/${id}.glb`]),
      ];
      await Promise.all(core.map(([k, u]) => loadModel(k, u)));
    })();
    return baseAssetsReady;
  }

  // --- World State & Wire Message Handling ---
  let activeWorld = null, latestTick = null, fogBits = null, tickReceivedAt = performance.now();
  const antStates = new Map(), selectedIds = new Set(), localMarkers = [];
  let hoverAntId = 0, mapInitSerial = 0;

  function decodeFogHex(hex, total) {
    if (!hex || typeof hex !== "string") return null;
    const bits = new Uint8Array(total);
    if (hex.length >= total * 2) {
      for (let i = 0; i < total; i++) bits[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16) ? 1 : 0;
    } else {
      for (let b = 0, i = 0; i < total && b * 2 + 1 < hex.length; b++) {
        const byte = parseInt(hex.slice(b * 2, b * 2 + 2), 16) || 0;
        for (let bit = 0; bit < 8 && i < total; bit++, i++) bits[i] = (byte >> bit) & 1;
      }
    }
    return bits;
  }

  function isTileRevealed(c, r) {
    if (!uiState.fog || !fogBits || !activeWorld) return true;
    return c >= 0 && r >= 0 && c < activeWorld.cols && r < activeWorld.rows && fogBits[r * activeWorld.cols + c] !== 0;
  }

  function disposeWorld() {
    if (!activeWorld) return;
    const sc = activeWorld.scene;
    if (sc && sc.borderGrass) sc.borderGrass.dispose();
    if (sc && sc.groundScatter) sc.groundScatter.dispose();
    if (sc && sc.atmoParticles) sc.atmoParticles.dispose();
    if (activeWorld.colonyAnts) activeWorld.colonyAnts.dispose();
    if (activeWorld.batchedBombs) activeWorld.batchedBombs.dispose();
    activeWorld = null;
  }

  async function applyInitMessage(msg) {
    const serial = ++mapInitSerial;
    hud.showLoading(true, `Loading ${msg.map.label || msg.map.file}…`, "Generating 3D PBR terrain and streaming map props.");
    await ensureBaseAssets();
    if (serial !== mapInitSerial) return;
    if (Array.isArray(msg.classes) && msg.classes.length >= 6) antClassByIdx = msg.classes.slice();

    const m = msg.map, cols = m.w || m.cols || 60, rows = m.h || m.rows || 60;
    const tiles = m.tiles || new Array(cols * rows).fill(54);
    uiState.seat = Number.isInteger(m.seat) ? m.seat : 0;
    uiState.fog = Boolean(m.fog); uiState.paused = Boolean(m.paused); uiState.bots = m.bots || "medium";
    hud.setFog(uiState.fog); hud.setPaused(uiState.paused); hud.setMapsList(msg.maps, m);

    if (typeof renderer.buildMaskFromAtlas === "function" && atlasMaskImg) {
      renderer.buildMaskFromAtlas(cols, rows, tiles, atlasMaskImg);
    } else {
      renderer.uploadMapGrid(cols, rows, tiles, true);
      let ok = false;
      if (m.id) {
        try {
          const pre = await loadImage(`models/demo/mask_${m.id}.png`);
          if (serial !== mapInitSerial) return;
          renderer.uploadMaskAtlas(pre); ok = true;
        } catch (_) {}
      }
      if (!ok && atlasMaskImg) renderer.uploadMaskAtlas(buildCanvasFallbackMask(cols, rows, tiles, atlasMaskImg));
    }

    const staticSpecs = [], seenIds = new Set(), hillsMap = {}, hillsArr = [];
    let seatMask = 0;
    const pushStatic = (id, col, row) => {
      if (!id) return;
      const pl = getItemPlacement(id, col, row);
      staticSpecs.push({ ...pl, col, row }); seenIds.add(pl.id);
    };
    for (const d of (m.static_decor || [])) pushStatic(d.id, d.col, d.row);
    for (const p of (m.plants || [])) pushStatic(p.id, p.col, p.row);
    for (const h of (m.hills || [])) {
      const hid = h.id || HILL_IDS[h.team] || "GREENHILL", pl = getItemPlacement(hid, h.col, h.row);
      pushStatic(hid, h.col, h.row);
      hillsMap[SEAT_TEAMS[h.team] || "green"] = [pl.x, pl.z, h.col, h.row];
      hillsArr.push({ team: h.team, id: hid, col: h.col, row: h.row, x: pl.x, z: pl.z });
      seatMask |= (1 << h.team);
    }
    uiState.activeSeats = seatMask;

    const uniq = [...seenIds];
    for (let i = 0; i < uniq.length; i += 8) {
      await Promise.all(uniq.slice(i, i + 8).map((id) => loadModel(`item:${id}`, `models/items/${id}.glb`)));
      if (serial !== mapInitSerial) return;
    }

    const staticEntries = [];
    for (let i = 0; i < staticSpecs.length; i++) {
      const sp = staticSpecs[i], model = getCachedModel(`item:${sp.id}`, `models/items/${sp.id}.glb`);
      if (!model) continue;
      staticEntries.push({
        spec: sp, x: sp.x, z: sp.z, radius: 0.65 * Math.max(model.size[0], model.size[2], 0.8) * sp.s,
        inst: { kind: "item", id: sp.id, isGrass: sp.id.toLowerCase().startsWith("grass"), model, clipName: "Idle", idleTime: (i * 0.29) % 1.1, offsetMat: buildWorldMat(model, sp.x, sp.z, sp.s, 0) },
      });
    }

    disposeWorld(); antStates.clear(); selectedIds.clear(); latestTick = null; fogBits = null;
    const mapShim = { cols, rows, tiles, hills: hillsMap }, waterTile = (mapsCatalog && mapsCatalog.water_tile_index) || 45;
    activeWorld = {
      id: m.id || "custom", label: m.label || m.file || "Backyard Match", file: m.file || "",
      cols, rows, tiles, hills: hillsArr, hillsMap, staticEntries,
      colonyAnts: buildColonyAntsForMap(gl, staticEntries), batchedBombs: null, batchedBombSig: "",
      scene: {
        instances: [], borderGrass: buildBorderGrassMesh(gl, mapShim, waterTile),
        groundScatter: buildGroundScatterMesh(gl, mapShim, waterTile), atmoParticles: buildAtmoParticles(gl, mapShim),
        target: [cols * 0.5, 0.3, rows * 0.5], fitRadius: BASE_FIT_RADIUS, halfWidth: BASE_FIT_RADIUS * 1.35,
        plinthSpanX: 0, plinthRadius: 1, shadows: [],
      },
    };
    hud.setMapTerrain(cols, rows, tiles, hillsArr, activeWorld.label);
    const myHill = hillsArr.find((h) => h.team === uiState.seat) || hillsArr[0];
    cam.target = myHill
      ? [clamp(myHill.x + (cols * 0.5 - myHill.x) * 0.22, 6, cols - 6), 0.3, clamp(myHill.z + (rows * 0.5 - myHill.z) * 0.22, 6, rows - 6)]
      : [cols * 0.5, 0.3, rows * 0.5];
    camGoal = null;
    hud.showLoading(false);
    hud.pushNews(`Match started on ${activeWorld.label} — You are ${SEAT_LABELS[uiState.seat]}!`);
  }

  function applyTickMessage(msg) {
    if (!activeWorld) return;
    latestTick = msg; tickReceivedAt = performance.now(); uiState.paused = Boolean(msg.paused);
    fogBits = (uiState.fog && msg.fog_hex) ? decodeFogHex(msg.fog_hex, activeWorld.cols * activeWorld.rows) : null;

    const seen = new Set();
    for (const a of (msg.ants || [])) {
      const id = a[0], team = a[1], cls = typeof a[2] === "string" ? a[2] : (antClassByIdx[a[2]] || "worker");
      const tx = a[3] / 32.0, tz = a[4] / 32.0, targetYaw = FACING_YAW[a[5] & 7];
      const hp = a[6], maxHp = a[7] || 10, state = a[8], action = a[9];
      const holding = Boolean(a[12]), carriedPts = a[13] || 0;
      const swimming = Boolean(a[14]), drowning = Boolean(a[15]), stunned = Boolean(a[16]), frozen = Boolean(a[17]), burnMs = a[18] || 0;
      seen.add(id);

      let ent = antStates.get(id);
      if (!ent) {
        ent = {
          id, team, cls, prevX: tx, prevZ: tz, targetX: tx, targetZ: tz, rx: tx, rz: tz,
          yaw: targetYaw, renderYaw: targetYaw, hp, maxHp, state, action, holding, carriedPts,
          swimming, drowning, stunned, frozen, burnMs, walkDist: 0, phase: (id * 0.37) % 1.0,
          yLift: swimming ? SWIMMER_SUBMERGE_Y : 0, clipName: "Idle",
          inst: {
            kind: "ant", cls, submerged: swimming || drowning, teamColors: TEAMS[SEAT_TEAMS[team]] || TEAMS.green,
            model: getCachedModel(`ant:${cls}`, `models/${cls}.glb`), clipName: "Idle", idleTime: 0,
            blink: { active: false, elapsed: 0, nextIn: 1.5 + ((id * 1.37) % 3.2) }, offsetMat: mat4Identity(),
          },
          lunchboxInst: { kind: "item", id: "lunchbox", model: null, clipName: "Idle", idleTime: 0, offsetMat: mat4Identity() },
        };
        antStates.set(id, ent);
      } else {
        ent.prevX = ent.rx; ent.prevZ = ent.rz;
        if (Math.hypot(tx - ent.rx, tz - ent.rz) > 3.2) { ent.prevX = tx; ent.prevZ = tz; ent.rx = tx; ent.rz = tz; }
        ent.targetX = tx; ent.targetZ = tz; ent.yaw = targetYaw; ent.team = team;
        if (ent.cls !== cls) { ent.cls = cls; ent.inst.cls = cls; ent.inst.model = getCachedModel(`ant:${cls}`, `models/${cls}.glb`); }
        ent.inst.teamColors = TEAMS[SEAT_TEAMS[team]] || TEAMS.green;
        ent.hp = hp; ent.maxHp = maxHp; ent.state = state; ent.action = action; ent.holding = holding; ent.carriedPts = carriedPts;
        ent.swimming = swimming; ent.drowning = drowning; ent.stunned = stunned; ent.frozen = frozen; ent.burnMs = burnMs;
      }
    }
    for (const id of antStates.keys()) if (!seen.has(id)) { antStates.delete(id); selectedIds.delete(id); }
    for (const ev of (msg.sfx || [])) {
      const hasPos = ev.length >= 3 && (ev[1] > 0 || ev[2] > 0);
      hud.sound.play(ev[0], hasPos ? ev[1] / 32 : NaN, hasPos ? ev[2] / 32 : NaN, cam.target);
    }
    hud.updateTick(msg, uiState.seat, uiState.activeSeats);
  }

  // --- Binary WebSocket Transport & ?mock=1 Launcher ---
  const encoder = new TextEncoder(), decoder = new TextDecoder();
  let ws = null, wsRetryTimer = null, wsRetryDelay = 700, mockRunner = null;

  function defaultWsUrl() {
    return params.get("wsUrl") || `ws://${location.hostname || "127.0.0.1"}:${params.get("ws") || "4081"}`;
  }

  function sendCommand(payload) {
    if (uiState.mode === "mock" && mockRunner) { mockRunner.handleCommand(payload, startMockMatch); return; }
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(encoder.encode(JSON.stringify(payload)));
  }

  function stopMock() { if (mockRunner) { mockRunner.stop(); mockRunner = null; } }

  function connectWebSocket() {
    stopMock();
    if (wsRetryTimer) { clearTimeout(wsRetryTimer); wsRetryTimer = null; }
    if (ws) { try { ws.close(); } catch (_) {} ws = null; }
    uiState.mode = "connecting";
    hud.setConnection("connecting", "Connecting…");
    const url = defaultWsUrl();
    hud.showLoading(true, "Connecting to Match Server…", `Opening binary WebSocket to ${url}`);
    let opened = false;
    const failTimer = setTimeout(() => {
      if (!opened && uiState.mode === "connecting") {
        hud.setConnection("offline", "Offline");
        hud.showLoading(true, "Match Server Not Detected", `Could not reach ${url}. Start ants_3d_bridge or jump into Offline Practice!`, true);
      }
    }, 1800);
    try { ws = new WebSocket(url); ws.binaryType = "arraybuffer"; } catch (_) {
      clearTimeout(failTimer); hud.setConnection("offline", "Offline");
      hud.showLoading(true, "WebSocket Unavailable", "Start ants_3d_bridge or play Offline Mock.", true); return;
    }
    ws.onopen = () => { opened = true; clearTimeout(failTimer); wsRetryDelay = 700; uiState.mode = "live"; hud.setConnection("live", "Live · 20Hz"); };
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(typeof ev.data === "string" ? ev.data : decoder.decode(new Uint8Array(ev.data)));
        if (msg.type === "init") applyInitMessage(msg);
        else if (msg.type === "tick") applyTickMessage(msg);
      } catch (_) {}
    };
    ws.onerror = () => {};
    ws.onclose = () => {
      clearTimeout(failTimer);
      if (uiState.mode === "mock") return;
      uiState.mode = "offline"; hud.setConnection("offline", "Reconnecting…");
      wsRetryTimer = setTimeout(() => { wsRetryDelay = Math.min(3500, Math.round(wsRetryDelay * 1.4)); connectWebSocket(); }, wsRetryDelay);
    };
  }

  async function startMockMatch(cfg = {}) {
    if (wsRetryTimer) { clearTimeout(wsRetryTimer); wsRetryTimer = null; }
    if (ws) { try { ws.close(); } catch (_) {} ws = null; }
    stopMock();
    uiState.mode = "mock"; hud.setConnection("mock", "Mock · 20Hz");
    await ensureBaseAssets();
    const maps = (mapsCatalog && mapsCatalog.maps) || [];
    const req = String(cfg.map || params.get("map") || "TREASURE.LVL").toLowerCase();
    const mapObj = maps.find((m) => m.id === req || m.file.toLowerCase() === req) || maps[0];
    if (!mapObj) return;
    mockRunner = createMockMatch(mapObj, maps, cfg, applyInitMessage, applyTickMessage);
    await applyInitMessage(mockRunner.initMsg);
    cam.target = [mockRunner.focus[0], 0.3, mockRunner.focus[1]];
    for (const id of mockRunner.initialSelect) selectedIds.add(id);
    mockRunner.start();
  }

  // --- Camera Projection, Inverse-viewProj Raycast & Orders ---
  const getCamOpts = () => ({ customTarget: cam.target, squareGameView: uiState.camPreset === "game" && cam.projection === "ortho" });

  function updateCameraMatrices() {
    if (!activeWorld) return;
    lastCanvasRect = canvas.getBoundingClientRect();
    lastCamera = renderer.computeCamera(activeWorld.scene, cam, canvas.width, canvas.height, null, getCamOpts());
    lastInvVP = mat4Invert(lastCamera.viewProj);
  }

  function screenToWorldXZ(clientX, clientY, planeY = 0) {
    if (!lastInvVP || !lastCanvasRect) updateCameraMatrices();
    if (!lastInvVP || !lastCanvasRect) return null;
    const nx = ((clientX - lastCanvasRect.left) / Math.max(1, lastCanvasRect.width)) * 2 - 1;
    const ny = 1 - ((clientY - lastCanvasRect.top) / Math.max(1, lastCanvasRect.height)) * 2;
    const p0 = unprojectNdc(lastInvVP, nx, ny, -1), p1 = unprojectNdc(lastInvVP, nx, ny, 1);
    if (!p0 || !p1 || Math.abs(p1[1] - p0[1]) < 1e-6) return null;
    const t = (planeY - p0[1]) / (p1[1] - p0[1]);
    return [p0[0] + (p1[0] - p0[0]) * t, p0[2] + (p1[2] - p0[2]) * t];
  }

  function worldToScreen(wx, wy, wz) {
    if (!lastCamera || !lastCanvasRect) return null;
    const m = lastCamera.viewProj;
    const cx = m[0] * wx + m[4] * wy + m[8] * wz + m[12], cy = m[1] * wx + m[5] * wy + m[9] * wz + m[13];
    const cz = m[2] * wx + m[6] * wy + m[10] * wz + m[14], cw = m[3] * wx + m[7] * wy + m[11] * wz + m[15];
    if (cw <= 1e-5) return null;
    const ndcZ = cz / cw;
    if (ndcZ < -1.1 || ndcZ > 1.1) return null;
    return [lastCanvasRect.left + ((cx / cw) * 0.5 + 0.5) * lastCanvasRect.width, lastCanvasRect.top + (0.5 - (cy / cw) * 0.5) * lastCanvasRect.height];
  }

  function getMinimapCameraQuad() {
    if (!lastCanvasRect || !activeWorld) return null;
    const r = lastCanvasRect;
    const pts = [screenToWorldXZ(r.left, r.top), screenToWorldXZ(r.right, r.top), screenToWorldXZ(r.right, r.bottom), screenToWorldXZ(r.left, r.bottom)];
    return pts.some((p) => !p) ? null : pts.map(([x, z]) => [clamp(x, 0, activeWorld.cols), clamp(z, 0, activeWorld.rows)]);
  }

  function clampTarget() {
    if (!activeWorld) return;
    cam.target[0] = clamp(cam.target[0], 1, activeWorld.cols - 1);
    cam.target[2] = clamp(cam.target[2], 1, activeWorld.rows - 1);
  }

  function panByPixels(dx, dy) {
    camGoal = null;
    const viewH = Math.max(canvas.clientHeight, 300), aspect = Math.max(0.2, canvas.clientWidth / viewH);
    const fitHalfH = Math.max(BASE_FIT_RADIUS, (BASE_FIT_RADIUS * 1.35 / aspect) * 1.06) * cam.zoom;
    const wpp = (2.0 * fitHalfH) / viewH, yr = cam.yaw * DEG;
    const pSin = (uiState.camPreset === "game" && cam.projection === "ortho" && Math.abs(cam.yaw) < 0.01) ? 1.0 : Math.max(0.32, Math.sin(cam.pitch * DEG));
    cam.target[0] -= (Math.cos(yr) * dx + Math.sin(yr) * (dy / pSin)) * wpp;
    cam.target[2] -= (-Math.sin(yr) * dx + Math.cos(yr) * (dy / pSin)) * wpp;
    clampTarget();
  }

  function applyOrbitDelta(dYaw, dPitch) {
    camGoal = null;
    if (uiState.camPreset === "game" && cam.projection === "ortho") { cam.projection = "perspective"; cam.pitch = 46; }
    cam.yaw = wrapAngleDeg(cam.yaw + dYaw); cam.pitch = clamp(cam.pitch + dPitch, 14, 86);
    uiState.camPreset = ""; hud.setCamLabel("Custom");
  }

  function setCamPreset(key) {
    const p = CAM_PRESETS[key]; if (!p) return;
    uiState.camPreset = key; cam.projection = p.proj;
    camGoal = { target: cam.target.slice(), yaw: p.yaw, pitch: p.pitch, zoom: p.zoom };
    hud.setCamLabel(p.label);
  }

  function setLightingPreset(key) {
    const k = LIGHT_SCENARIOS[key] ? key : "daylight";
    uiState.lighting = k; hud.setLightLabel(LIGHT_SCENARIOS[k].label, k);
  }

  function pickAntAt(cx, cy, filter) {
    let best = null, bestPx = 30;
    const wPos = screenToWorldXZ(cx, cy, 0);
    for (const a of antStates.values()) {
      if (filter === "own" && a.team !== uiState.seat) continue;
      if (filter === "enemy" && a.team === uiState.seat) continue;
      if (a.team !== uiState.seat && !isTileRevealed(Math.floor(a.rx), Math.floor(a.rz))) continue;
      const sc = worldToScreen(a.rx, (a.yLift || 0) + 0.48, a.rz);
      if (sc) { const dp = Math.hypot(sc[0] - cx, sc[1] - cy); if (dp < bestPx) { bestPx = dp; best = a; } }
      if (!best && wPos && Math.hypot(a.rx - wPos[0], a.rz - wPos[1]) < 0.72) best = a;
    }
    return best;
  }

  function isOwnHillHit(wx, wz) {
    const h = activeWorld && activeWorld.hills.find((x) => x.team === uiState.seat);
    return Boolean(h && Math.hypot(wx - h.x, wz - h.z) <= 2.15);
  }

  function getSelectedOwnAnts() {
    const out = [];
    for (const id of selectedIds) { const a = antStates.get(id); if (a && a.team === uiState.seat) out.push(a); }
    return out;
  }

  function issueOrderAt(cx, cy, forceSpecial) {
    const sel = getSelectedOwnAnts();
    if (!sel.length || !activeWorld) return;
    const ids = sel.map((a) => a.id), leadCls = sel[0].cls;
    if (forceSpecial || uiState.abilityMode) {
      const w = screenToWorldXZ(cx, cy, 0); if (!w) return;
      const tx = clamp(Math.floor(w[0]), 0, activeWorld.cols - 1), ty = clamp(Math.floor(w[1]), 0, activeWorld.rows - 1);
      sendCommand({ type: "special", ants: ids, tx, ty });
      localMarkers.push({ kind: "special", x: tx + 0.5, z: ty + 0.5, t0: uiState.simTime });
      if (uiState.mode !== "live") hud.sound.playVoice("ability", leadCls);
      uiState.abilityMode = false; return;
    }
    const enemy = pickAntAt(cx, cy, "enemy");
    if (enemy) {
      sendCommand({ type: "attack", ants: ids, target_id: enemy.id });
      localMarkers.push({ kind: "attack", x: enemy.rx, z: enemy.rz, t0: uiState.simTime });
      if (uiState.mode !== "live") hud.sound.playVoice("attack", leadCls);
      return;
    }
    const w = screenToWorldXZ(cx, cy, 0); if (!w) return;
    const tx = clamp(Math.floor(w[0]), 0, activeWorld.cols - 1), ty = clamp(Math.floor(w[1]), 0, activeWorld.rows - 1);
    sendCommand({ type: "move", ants: ids, tx, ty });
    localMarkers.push({ kind: "move", x: tx + 0.5, z: ty + 0.5, t0: uiState.simTime });
    if (uiState.mode !== "live") hud.sound.playVoice("move", leadCls);
  }

  function selectByMarquee(x0, y0, x1, y1, additive) {
    const minX = Math.min(x0, x1), maxX = Math.max(x0, x1), minY = Math.min(y0, y1), maxY = Math.max(y0, y1);
    if (!additive) selectedIds.clear();
    let lead = null;
    for (const a of antStates.values()) {
      if (a.team !== uiState.seat) continue;
      const sc = worldToScreen(a.rx, (a.yLift || 0) + 0.42, a.rz);
      if (sc && sc[0] >= minX && sc[0] <= maxX && sc[1] >= minY && sc[1] <= maxY) { selectedIds.add(a.id); if (!lead) lead = a; }
    }
    if (lead) hud.sound.playVoice("ready", lead.cls);
  }

  const triggerHatch = () => { sendCommand({ type: "hatch" }); hud.sound.play(0, NaN, NaN, null); };
  const triggerStop = () => {
    const sel = getSelectedOwnAnts(); if (!sel.length) return;
    sendCommand({ type: "stop", ants: sel.map((a) => a.id) }); hud.sound.play(61, NaN, NaN, null);
  };
  const toggleAbilityMode = () => {
    uiState.abilityMode = getSelectedOwnAnts().some((a) => (CLASS_META[a.cls] || {}).canSpecial) ? !uiState.abilityMode : false;
    hud.sound.play(0, NaN, NaN, null);
  };

  // --- HUD Callbacks ---
  const hud = createHud({
    onMinimapJump: (wx, wz) => { camGoal = null; cam.target[0] = wx; cam.target[2] = wz; clampTarget(); },
    onCycleCamera: () => setCamPreset(CAM_ORDER[(Math.max(0, CAM_ORDER.indexOf(uiState.camPreset)) + 1) % CAM_ORDER.length]),
    onCycleLighting: () => setLightingPreset(LIGHT_ORDER[(Math.max(0, LIGHT_ORDER.indexOf(uiState.lighting)) + 1) % LIGHT_ORDER.length]),
    onToggleFog: () => {
      uiState.fog = !uiState.fog; hud.setFog(uiState.fog);
      if (activeWorld) sendCommand({ type: "new_match", map: activeWorld.file || "TREASURE.LVL", bots: uiState.bots, fog: uiState.fog, seat: uiState.seat, seed: uiState.seed });
    },
    onTogglePause: () => { uiState.paused = !uiState.paused; sendCommand({ type: "pause", paused: uiState.paused }); hud.setPaused(uiState.paused); },
    onHatch: triggerHatch, onToggleAbility: toggleAbilityMode, onStop: triggerStop,
    onSelectAll: () => {
      selectedIds.clear(); let lead = null;
      for (const a of antStates.values()) if (a.team === uiState.seat) { selectedIds.add(a.id); if (!lead) lead = a; }
      if (lead) hud.sound.playVoice("ready", lead.cls);
    },
    onSelectClass: (cls) => {
      selectedIds.clear(); let lead = null;
      for (const a of antStates.values()) if (a.team === uiState.seat && a.cls === cls) { selectedIds.add(a.id); if (!lead) lead = a; }
      if (lead) { hud.sound.playVoice("ready", lead.cls); camGoal = { target: [lead.rx, 0.3, lead.rz], yaw: cam.yaw, pitch: cam.pitch, zoom: cam.zoom }; }
    },
    onToggleBoxSelect: () => { uiState.boxSelectMode = !uiState.boxSelectMode; },
    onNewMatch: (cfg) => {
      uiState.bots = cfg.bots || "medium"; uiState.fog = Boolean(cfg.fog); uiState.seat = Number(cfg.seat) || 0; uiState.seed = Number(cfg.seed) || 1;
      if (uiState.mode === "live") sendCommand({ type: "new_match", map: cfg.map, bots: uiState.bots, fog: uiState.fog, seat: uiState.seat, seed: uiState.seed });
      else startMockMatch(cfg);
    },
    onStartMock: (cfg) => startMockMatch(cfg), onRetryWs: () => connectWebSocket(),
  });

  // --- Desktop Mouse/Keyboard + Mobile Touch RTS Input ---
  const ptr = { pointers: new Map(), mode: "idle", startX: 0, startY: 0, lastX: 0, lastY: 0, moved: false, shift: false, lastClickTime: 0, lastClickAntId: 0, lastPinch: 0, lastAngle: 0 };
  const keysHeld = new Set();
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  canvas.addEventListener("pointerdown", (e) => {
    hud.sound.unlock(); canvas.focus({ preventScroll: true });
    try { canvas.setPointerCapture(e.pointerId); } catch (_) {}
    ptr.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY, type: e.pointerType });
    camGoal = null;
    if (ptr.pointers.size === 1) {
      ptr.startX = ptr.lastX = e.clientX; ptr.startY = ptr.lastY = e.clientY; ptr.moved = false; ptr.shift = e.shiftKey;
      if (e.pointerType === "touch") ptr.mode = uiState.boxSelectMode ? "marquee" : "touch_pan_or_tap";
      else if (e.button === 1) { ptr.mode = "pan"; $("play-shell").classList.add("mode-panning"); }
      else if (e.button === 2) ptr.mode = "right_click_or_orbit";
      else if (e.button === 0 && e.shiftKey) ptr.mode = "shift_left";
      else ptr.mode = "left_select";
    } else if (ptr.pointers.size === 2) {
      hud.setMarquee(null); ptr.mode = "two_finger";
      const [a, b] = [...ptr.pointers.values()];
      ptr.lastPinch = Math.hypot(b.x - a.x, b.y - a.y); ptr.lastAngle = Math.atan2(b.y - a.y, b.x - a.x);
      ptr.lastX = 0.5 * (a.x + b.x); ptr.lastY = 0.5 * (a.y + b.y);
    }
  });

  canvas.addEventListener("pointermove", (e) => {
    const cur = ptr.pointers.get(e.pointerId);
    if (!cur) { const h = pickAntAt(e.clientX, e.clientY, null); hoverAntId = h ? h.id : 0; return; }
    cur.x = e.clientX; cur.y = e.clientY;
    if (Math.hypot(e.clientX - ptr.startX, e.clientY - ptr.startY) > 6) ptr.moved = true;
    if (ptr.pointers.size === 1) {
      const dx = e.clientX - ptr.lastX, dy = e.clientY - ptr.lastY;
      ptr.lastX = e.clientX; ptr.lastY = e.clientY;
      if (ptr.mode === "pan" || (ptr.mode === "touch_pan_or_tap" && ptr.moved)) panByPixels(dx, dy);
      else if ((ptr.mode === "right_click_or_orbit" || ptr.mode === "shift_left") && ptr.moved) applyOrbitDelta(-dx * 0.36, dy * 0.28);
      else if ((ptr.mode === "left_select" || ptr.mode === "marquee") && ptr.moved && !uiState.abilityMode) {
        hud.setMarquee({ x: Math.min(ptr.startX, e.clientX), y: Math.min(ptr.startY, e.clientY), w: Math.abs(e.clientX - ptr.startX), h: Math.abs(e.clientY - ptr.startY) });
      }
    } else if (ptr.pointers.size === 2) {
      const [a, b] = [...ptr.pointers.values()], cx = 0.5 * (a.x + b.x), cy = 0.5 * (a.y + b.y);
      const dist = Math.hypot(b.x - a.x, b.y - a.y), ang = Math.atan2(b.y - a.y, b.x - a.x);
      if (ptr.lastPinch > 2 && dist > 2) cam.zoom = clamp(cam.zoom * (ptr.lastPinch / dist), 0.28, 2.8);
      let dAng = ang - ptr.lastAngle;
      while (dAng > Math.PI) dAng -= 2 * Math.PI;
      while (dAng < -Math.PI) dAng += 2 * Math.PI;
      if (dist > 36 && Math.abs(dAng) > 0.008) applyOrbitDelta(dAng / DEG, 0);
      panByPixels(cx - ptr.lastX, cy - ptr.lastY);
      ptr.lastPinch = dist; ptr.lastAngle = ang; ptr.lastX = cx; ptr.lastY = cy;
    }
  });

  function handlePointerEnd(e) {
    const had = ptr.pointers.has(e.pointerId); ptr.pointers.delete(e.pointerId);
    $("play-shell").classList.remove("mode-panning");
    if (!had || ptr.pointers.size > 0) return;
    hud.setMarquee(null);
    if (ptr.mode === "left_select" || ptr.mode === "shift_left" || ptr.mode === "marquee") {
      if (ptr.moved && ptr.mode !== "shift_left" && !uiState.abilityMode) selectByMarquee(ptr.startX, ptr.startY, e.clientX, e.clientY, ptr.shift);
      else if (!ptr.moved) {
        if (uiState.abilityMode) issueOrderAt(e.clientX, e.clientY, true);
        else {
          const ant = pickAntAt(e.clientX, e.clientY, "own");
          if (ant) {
            const now = performance.now();
            if (!ptr.shift && ptr.lastClickAntId === ant.id && now - ptr.lastClickTime < 350) {
              selectedIds.clear();
              for (const o of antStates.values()) if (o.team === uiState.seat && o.cls === ant.cls) selectedIds.add(o.id);
            } else if (ptr.shift) { if (selectedIds.has(ant.id)) selectedIds.delete(ant.id); else selectedIds.add(ant.id); }
            else { selectedIds.clear(); selectedIds.add(ant.id); }
            ptr.lastClickAntId = ant.id; ptr.lastClickTime = now; hud.sound.playVoice("ready", ant.cls);
          } else {
            const w = screenToWorldXZ(e.clientX, e.clientY, 0);
            if (w && isOwnHillHit(w[0], w[1])) triggerHatch();
            else if (!ptr.shift) selectedIds.clear();
          }
        }
      }
    } else if (ptr.mode === "right_click_or_orbit" && !ptr.moved) {
      issueOrderAt(e.clientX, e.clientY, false);
    } else if (ptr.mode === "touch_pan_or_tap" && !ptr.moved) {
      if (uiState.abilityMode) issueOrderAt(e.clientX, e.clientY, true);
      else {
        const own = pickAntAt(e.clientX, e.clientY, "own");
        if (own) { selectedIds.clear(); selectedIds.add(own.id); hud.sound.playVoice("ready", own.cls); }
        else if (selectedIds.size > 0) issueOrderAt(e.clientX, e.clientY, false);
        else { const w = screenToWorldXZ(e.clientX, e.clientY, 0); if (w && isOwnHillHit(w[0], w[1])) triggerHatch(); }
      }
    }
    ptr.mode = "idle";
  }
  canvas.addEventListener("pointerup", handlePointerEnd);
  canvas.addEventListener("pointercancel", handlePointerEnd);
  canvas.addEventListener("touchmove", (e) => { if (e.touches && e.touches.length >= 2) e.preventDefault(); }, { passive: false });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault(); camGoal = null;
    if (e.shiftKey || e.altKey) applyOrbitDelta(-clamp(e.deltaX, -120, 120) * 0.28, clamp(e.deltaY, -120, 120) * 0.20);
    else cam.zoom = clamp(cam.zoom * Math.exp(clamp(e.deltaY, -180, 180) * 0.0014), 0.28, 2.8);
  }, { passive: false });

  let gRot0 = 0, gSc0 = 1, gYaw0 = 0, gZoom0 = 1;
  canvas.addEventListener("gesturestart", (e) => { e.preventDefault(); camGoal = null; gRot0 = e.rotation || 0; gSc0 = e.scale || 1; gYaw0 = cam.yaw; gZoom0 = cam.zoom; }, { passive: false });
  canvas.addEventListener("gesturechange", (e) => {
    e.preventDefault();
    if (Math.abs((e.rotation || 0) - gRot0) > 0.5) {
      if (uiState.camPreset === "game" && cam.projection === "ortho") cam.projection = "perspective";
      cam.yaw = wrapAngleDeg(gYaw0 + ((e.rotation || 0) - gRot0)); uiState.camPreset = "";
    }
    if (e.scale && gSc0 > 0) cam.zoom = clamp(gZoom0 / (e.scale / gSc0), 0.28, 2.8);
  }, { passive: false });

  window.addEventListener("keydown", (e) => {
    hud.sound.unlock();
    const tag = document.activeElement && document.activeElement.tagName;
    if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA" || e.metaKey || e.ctrlKey || e.altKey) return;
    const k = e.key.toLowerCase(); keysHeld.add(k);
    if (e.key === "Escape") { if (uiState.abilityMode) uiState.abilityMode = false; else if (!$("modal-match").hidden) hud.openMatchModal(false); else selectedIds.clear(); }
    else if (k === "h") { e.preventDefault(); triggerHatch(); }
    else if (k === "p") { e.preventDefault(); $("btn-pause").click(); }
    else if (k === "m") { e.preventDefault(); $("btn-mute").click(); }
    else if (k === "g") { e.preventDefault(); uiState.showGrid = !uiState.showGrid; }
    else if (k === "l") { e.preventDefault(); $("btn-light-cycle").click(); }
    else if (k === "q") { e.preventDefault(); applyOrbitDelta(-15, 0); }
    else if (k === "e") { e.preventDefault(); applyOrbitDelta(15, 0); }
    else if (k === "b" || (e.key === " " && tag !== "BUTTON")) { e.preventDefault(); toggleAbilityMode(); }
    else if (k === "a" && selectedIds.size > 0 && getSelectedOwnAnts().some((a) => (CLASS_META[a.cls] || {}).canSpecial)) { e.preventDefault(); toggleAbilityMode(); }
    else if ((k === "s" && selectedIds.size > 0) || k === "x") { e.preventDefault(); triggerStop(); }
    else if (e.key === "Tab") { e.preventDefault(); $("cmd-select-all").click(); }
    else if (k === "f" && activeWorld) {
      const sel = getSelectedOwnAnts(), hl = activeWorld.hills.find((h) => h.team === uiState.seat);
      const tgt = sel.length ? [sel[0].rx, 0.3, sel[0].rz] : (hl ? [hl.x, 0.3, hl.z] : null);
      if (tgt) camGoal = { target: tgt, yaw: cam.yaw, pitch: cam.pitch, zoom: cam.zoom };
    } else if (e.key >= "1" && e.key <= "4") { e.preventDefault(); setCamPreset(CAM_ORDER[Number(e.key) - 1]); }
  });
  window.addEventListener("keyup", (e) => keysHeld.delete(e.key.toLowerCase()));

  // --- 60 Hz 3D Scene Assembly & Main Loop ---
  function buildFrameScene(dt) {
    if (!activeWorld) return { ripples: [], dynGlows: [], visibleAnts: [] };
    uiState.simTime += dt;
    const t = uiState.simTime;
    const aspect = Math.max(0.6, (canvas.clientWidth || 1000) / Math.max(1, canvas.clientHeight || 700));
    const visHalfSpan = BASE_FIT_RADIUS * cam.zoom * Math.max(aspect, 1.0 / Math.max(0.42, Math.sin(cam.pitch * DEG))) * 1.2 + 4.5;
    const tx = cam.target[0], tz = cam.target[2], visible = [], ripples = [], dynGlows = [];
    const inView = (x, z, pad = 2.0) => Math.abs(x - tx) <= visHalfSpan + pad && Math.abs(z - tz) <= visHalfSpan + pad;

    for (let i = 0; i < activeWorld.staticEntries.length; i++) {
      const e = activeWorld.staticEntries[i];
      if (!inView(e.x, e.z, e.radius)) continue;
      e.inst.idleTime = t + ((i * 0.29) % 1.1); visible.push(e.inst);
    }
    if (activeWorld.colonyAnts) updateColonyAnts(activeWorld.colonyAnts, t, !uiState.paused, tx, tz, visHalfSpan, visible);

    const tk = latestTick;
    if (tk) {
      for (const f of (tk.food || [])) {
        if (!isTileRevealed(f[0], f[1])) continue;
        const pl = getItemPlacement(f[2], f[0], f[1]); if (!inView(pl.x, pl.z, 2)) continue;
        const m = getCachedModel(`item:${pl.id}`, `models/items/${pl.id}.glb`);
        if (m) visible.push({ kind: "item", id: pl.id, model: m, clipName: "Idle", idleTime: t, offsetMat: buildWorldMat(m, pl.x, pl.z, pl.s, 0) });
      }
      for (const b of (tk.bridges || [])) {
        const bx = b[0] + 0.5, bz = b[1] + 0.5;
        if (inView(bx, bz, 1.5)) visible.push({ kind: "item", id: "bridge_mud", submerged: true, model: fxAssets.bridges[clamp(b[2] || 4, 1, 4) - 1], clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([bx, 0, bz], [0, 0, 0, 1], [1, 1, 1]) });
      }
      const bombs = tk.bombs || [];
      if (bombs.length > 40) {
        const sig = `${bombs.length}:${bombs[0][0]},${bombs[0][1]}`;
        if (activeWorld.batchedBombSig !== sig) {
          if (activeWorld.batchedBombs) activeWorld.batchedBombs.dispose();
          activeWorld.batchedBombs = buildBatchedMapBombsMesh(gl, bombs.map((b) => ({ id: BOMB_IDS[b[2]] || "blackbomb", x: b[0] + 0.5, z: b[1] + 0.5, s: 0.58 })));
          activeWorld.batchedBombSig = sig;
        }
        if (activeWorld.batchedBombs) visible.push(activeWorld.batchedBombs.inst);
      } else {
        for (let i = 0; i < bombs.length; i++) {
          const b = bombs[i]; if (!isTileRevealed(b[0], b[1])) continue;
          const bx = b[0] + 0.5, bz = b[1] + 0.5; if (!inView(bx, bz, 1.5)) continue;
          const bid = BOMB_IDS[b[2]] || "blackbomb", m = getCachedModel(`item:${bid}`, `models/items/${bid}.glb`);
          if (m) visible.push({ kind: "item", id: bid, model: m, clipName: "Idle", idleTime: t + i * 0.27, offsetMat: buildWorldMat(m, bx, bz, 0.58, 0) });
          if (dynGlows.length < 8) dynGlows.push([bx, bz, 1.4, 0.48, 1.9, 0.9, 0.22]);
        }
      }
      const fireModel = getCachedModel("item:wallup04", "models/items/wallup04.glb");
      if (fireModel) {
        for (let i = 0; i < (tk.fires || []).length; i++) {
          const f = tk.fires[i]; if (!isTileRevealed(f[0], f[1])) continue;
          const fx = f[0] + 0.5, fz = f[1] + 0.5; if (!inView(fx, fz, 1.5)) continue;
          visible.push({ kind: "item", id: "wallup04", isGrass: true, model: fireModel, clipName: "Idle", idleTime: t + i * 0.41, offsetMat: buildWorldMat(fireModel, fx, fz, 1.06, 0) });
          if (dynGlows.length < 8) dynGlows.push([fx, fz, 2.3, 0.85 + 0.18 * Math.sin(t * 9 + i), 2.1, 0.72, 0.16]);
        }
      }
      for (const p of (tk.powerups || [])) {
        if (!isTileRevealed(p[0], p[1])) continue;
        const pl = getItemPlacement(p[2], p[0], p[1]); if (!inView(pl.x, pl.z, 1.8)) continue;
        const m = getCachedModel(`item:${pl.id}`, `models/items/${pl.id}.glb`), ph = (p[0] * 0.7 + p[1] * 1.1) % 6.28;
        if (m) visible.push({ kind: "item", id: pl.id, model: m, clipName: "Idle", idleTime: t, offsetMat: buildWorldMat(m, pl.x, pl.z, pl.s, t * 1.2 + ph, 0.08 + 0.055 * Math.sin(t * 2.8 + ph)) });
        const rSc = 0.94 + 0.12 * Math.sin(t * 2.8 + ph);
        visible.push({ kind: "item", id: "pu_ring", isGrass: true, model: fxAssets.puRings[pl.id] || fxAssets.puRingDefault, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([pl.x, 0, pl.z], [0, 0, 0, 1], [rSc, 1, rSc]) });
      }
      const lbM = getCachedModel("item:lunchbox", "models/items/lunchbox.glb");
      if (lbM) for (const lb of (tk.lunchboxes || [])) {
        if (isTileRevealed(lb[0], lb[1]) && inView(lb[0] + 0.5, lb[1] + 0.5, 1.5)) {
          visible.push({ kind: "item", id: "lunchbox", model: lbM, clipName: "Idle", idleTime: 0, offsetMat: buildWorldMat(lbM, lb[0] + 0.5, lb[1] + 0.5, 0.53, 0.4) });
        }
      }
      const dfM = getCachedModel("item:dflower1", "models/items/dflower1.glb");
      for (const dr of (tk.droppers || [])) {
        const dx = dr[0] + 0.5, dz = dr[1] + 0.5; if (!inView(dx, dz, 3.0)) continue;
        if (dfM) visible.push({ kind: "item", id: "dflower1", model: dfM, clipName: "Idle", idleTime: dr[2] ? dr[3] * 0.08 : 0, offsetMat: buildWorldMat(dfM, dx, dz, 2.2, 0) });
        if (dr[2]) {
          const fdId = DROPPER_FD_IDS[clamp((dr[2] || 1) - 1, 0, DROPPER_FD_IDS.length - 1)], fdM = getCachedModel(`item:${fdId}`, `models/items/${fdId}.glb`), u = clamp((dr[3] || 0) / 8, 0, 1);
          if (fdM) visible.push({ kind: "item", id: fdId, model: fdM, clipName: "Idle", idleTime: u * 0.74, offsetMat: buildWorldMat(fdM, dx, dz + 1, 2.2, u * 1.4, (1 - u) * 1.4) });
        }
      }
      let blastSlot = 0;
      for (const fx of (tk.fx || [])) {
        const kind = String(fx[0] || "").toLowerCase(), fxX = fx[1] / 32, fxZ = fx[2] / 32, ageMs = fx[3] || 0;
        if (!inView(fxX, fxZ, 3.5) || !isTileRevealed(Math.floor(fxX), Math.floor(fxZ))) continue;
        if (kind === "bombex" || kind === "dud") pushExplosionFx(fxAssets, fxX, fxZ, ageMs, kind === "dud", visible, dynGlows, blastSlot++);
        else if (kind === "battle") {
          for (let k = 0; k < 4; k++) {
            const a = t * 8.5 + k * 1.57, sc = 0.24 + 0.08 * Math.sin(t * 14 + k);
            visible.push({ kind: "item", id: "fx_b", isGrass: true, model: fxAssets.dustPuff, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([fxX + Math.cos(a) * 0.28, 0.34 + 0.14 * Math.sin(a * 1.7), fxZ + Math.sin(a) * 0.28], [0, 0, 0, 1], [sc, sc, sc]) });
          }
        } else if (kind === "sputter") {
          const u = clamp(ageMs / 830, 0, 1), sc = 0.22 + u * 0.28;
          visible.push({ kind: "item", id: "fx_sp", isGrass: true, model: fxAssets.smokePuff, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([fxX, 0.2 + u * 0.95, fxZ], [0, 0, 0, 1], [sc, sc, sc]) });
        } else if (kind === "dsplash") {
          const u = clamp(ageMs / 460, 0, 1);
          if (ripples.length < 8) ripples.push([fxX, fxZ, u, 0.95]);
          for (let k = 0; k < 4; k++) {
            const a = k * 1.57 + 0.4, sc = Math.max(0.05, 0.18 * (1 - u));
            visible.push({ kind: "item", id: "fx_ds", isGrass: true, model: fxAssets.splashDrop, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([fxX + Math.cos(a) * u * 0.55, Math.sin(u * Math.PI) * 0.55, fxZ + Math.sin(a) * u * 0.55], [0, 0, 0, 1], [sc, sc, sc]) });
          }
        } else if (kind === "getpow") {
          const u = clamp(ageMs / 650, 0, 1), sc = 0.7 + u * 1.1;
          visible.push({ kind: "item", id: "fx_gp", isGrass: true, model: fxAssets.powHalo, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([fxX, 0.08 + u * 0.9, fxZ], [0, 0, 0, 1], [sc, 1, sc]) });
          if (dynGlows.length < 8) dynGlows.push([fxX, fxZ, 2.4, (1 - u) * 1.6, 1.8, 1.4, 0.35]);
        } else if (kind === "xmarks") {
          const sc = 1 - clamp(ageMs / 550, 0, 1) * 0.35;
          visible.push({ kind: "item", id: "fx_xm", isGrass: true, model: fxAssets.xMarkers.move, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([fxX, 0.02, fxZ], [0, 0, 0, 1], [sc, 1, sc]) });
        }
      }
    }

    for (let i = localMarkers.length - 1; i >= 0; i--) {
      const mk = localMarkers[i], age = t - mk.t0;
      if (age > 0.65) { localMarkers.splice(i, 1); continue; }
      const u = age / 0.65, xSc = 0.85 + 0.25 * Math.sin(u * Math.PI), rSc = 1.45 - u * 0.85;
      visible.push({ kind: "item", id: "loc_x", isGrass: true, model: fxAssets.xMarkers[mk.kind] || fxAssets.xMarkers.move, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([mk.x, 0.01, mk.z], [0, 0, 0, 1], [xSc, 1, xSc]) });
      visible.push({ kind: "item", id: "loc_r", isGrass: true, model: fxAssets.orderRings[mk.kind] || fxAssets.orderRings.move, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([mk.x, 0, mk.z], [0, 0, 0, 1], [rSc, 1, rSc]) });
    }

    const interp = clamp((performance.now() - tickReceivedAt) / 50.0, 0, 1);
    const lbModel = getCachedModel("item:lunchbox", "models/items/lunchbox.glb");
    const visibleAnts = [];
    for (const a of antStates.values()) {
      const ox = a.rx, oz = a.rz;
      a.rx = a.prevX + (a.targetX - a.prevX) * interp; a.rz = a.prevZ + (a.targetZ - a.prevZ) * interp;
      const stepD = Math.hypot(a.rx - ox, a.rz - oz);
      a.renderYaw = lerpAngleRad(a.renderYaw, a.yaw, 1 - Math.exp(-dt * 18.0));
      const moving = stepD > 0.002 || a.state === 1 || a.action === 1;
      if (moving && !uiState.paused) a.walkDist += stepD;
      if (a.team !== uiState.seat && !isTileRevealed(Math.floor(a.rx), Math.floor(a.rz))) continue;
      if (!inView(a.rx, a.rz, 2.2)) continue;
      if (!a.inst.model) a.inst.model = getCachedModel(`ant:${a.cls}`, `models/${a.cls}.glb`);
      if (!a.inst.model) continue;

      const clip = resolveAntClip(a.cls, a.state, a.action, a.swimming, a.drowning, a.stunned, a.burnMs, moving);
      a.clipName = clip; a.inst.clipName = clip;
      const tgtSub = a.swimming ? SWIMMER_SUBMERGE_Y : (a.drowning ? -0.34 * ANT_SCALE : 0);
      a.yLift += (tgtSub - a.yLift) * (1 - Math.exp(-dt * 14.0));
      const bob = a.swimming ? Math.sin(t * 4.6 + a.phase * 6.28) * 0.026 : 0;
      const air = (clip === "BlownBack" || clip === "BlownUp") ? Math.abs(Math.sin(t * 9 + a.id)) * 0.52 : 0;
      a.inst.submerged = a.yLift < -0.05 || a.swimming || a.drowning;
      a.inst.offsetMat = buildWorldMat(a.inst.model, a.rx, a.rz, ANT_SCALE, a.renderYaw, a.yLift + bob + air);
      a.inst.idleTime = clip === "Walk" ? (a.phase * 0.6 + a.walkDist * 0.85) : (t * (a.swimming ? 1.25 : 1.0) + a.phase * 1.4);

      if ((a.swimming || a.drowning) && ripples.length < 8) {
        const snX = 0.23 * ANT_SCALE, snZ = 0.30 * ANT_SCALE, cy = Math.cos(a.renderYaw), sy = Math.sin(a.renderYaw);
        ripples.push([a.rx + snX * cy + snZ * sy, a.rz - snX * sy + snZ * cy, a.phase, 0.72]);
      }
      if (!uiState.paused && a.inst.blink) {
        if (a.inst.blink.active) { if ((a.inst.blink.elapsed += dt) >= 0.22) { a.inst.blink.active = false; a.inst.blink.elapsed = 0; a.inst.blink.nextIn = 2 + Math.random() * 3.8; } }
        else if ((a.inst.blink.nextIn -= dt) <= 0) { a.inst.blink.active = true; a.inst.blink.elapsed = 0; }
      }
      if (selectedIds.has(a.id)) {
        const pulse = 1 + 0.045 * Math.sin(t * 5.2 + a.phase * 6.28), h = a.renderYaw * 0.5;
        visible.push({ kind: "item", id: "sel_ring", isGrass: true, model: fxAssets.selRings[a.team] || fxAssets.selRings[0], clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([a.rx, 0, a.rz], [0, Math.sin(h), 0, Math.cos(h)], [pulse, 1, pulse]) });
      } else if (a.id === hoverAntId) {
        visible.push({ kind: "item", id: "hov_ring", isGrass: true, model: a.team === uiState.seat ? fxAssets.hoverRing : fxAssets.enemyHoverRing, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([a.rx, 0, a.rz], [0, 0, 0, 1], [1, 1, 1]) });
      }
      visible.push(a.inst); visibleAnts.push(a);
      if (a.holding && lbModel && !a.swimming) {
        a.lunchboxInst.model = lbModel;
        a.lunchboxInst.offsetMat = buildCarriedLunchboxMat(lbModel, a.rx, Math.max(0, a.yLift + air), a.rz, a.renderYaw, a.inst.idleTime, clip === "Walk", ANT_SCALE, false);
        visible.push(a.lunchboxInst);
      }
    }

    if (uiState.fog && fogBits) {
      for (let r = Math.max(0, Math.floor(tz - visHalfSpan)); r <= Math.min(activeWorld.rows - 1, Math.ceil(tz + visHalfSpan)); r += 2) {
        for (let c = Math.max(0, Math.floor(tx - visHalfSpan)); c <= Math.min(activeWorld.cols - 1, Math.ceil(tx + visHalfSpan)); c += 2) {
          if (!fogBits[r * activeWorld.cols + c]) {
            visible.push({ kind: "item", id: "fog_tile", isGrass: true, model: fxAssets.fogTile, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([c + 1, 0.04, r + 1], [0, 0, 0, 1], [2.02, 1, 2.02]) });
          }
        }
      }
    }
    activeWorld.scene.instances = visible;
    return { ripples, dynGlows, visibleAnts };
  }

  function updateCameraAndKeys(dt) {
    const spd = 560 * dt;
    let kx = 0, ky = 0;
    if (keysHeld.has("arrowleft") || (keysHeld.has("a") && selectedIds.size === 0)) kx += spd;
    if (keysHeld.has("arrowright") || keysHeld.has("d")) kx -= spd;
    if (keysHeld.has("arrowup") || keysHeld.has("w")) ky += spd;
    if (keysHeld.has("arrowdown") || (keysHeld.has("s") && selectedIds.size === 0)) ky -= spd;
    if (kx !== 0 || ky !== 0) panByPixels(kx, ky);
    if (!camGoal) return;
    const al = 1 - Math.exp(-dt * 8.5);
    let dY = camGoal.yaw - cam.yaw;
    while (dY > 180) dY -= 360;
    while (dY < -180) dY += 360;
    cam.yaw = wrapAngleDeg(cam.yaw + dY * al);
    cam.pitch += (camGoal.pitch - cam.pitch) * al; cam.zoom += (camGoal.zoom - cam.zoom) * al;
    for (let i = 0; i < 3; i++) cam.target[i] += (camGoal.target[i] - cam.target[i]) * al;
    if (Math.abs(dY) + Math.abs(camGoal.pitch - cam.pitch) + Math.abs(camGoal.zoom - cam.zoom) * 30 + Math.hypot(camGoal.target[0] - cam.target[0], camGoal.target[2] - cam.target[2]) * 12 < 0.08) {
      cam.yaw = camGoal.yaw; cam.pitch = camGoal.pitch; cam.zoom = camGoal.zoom; cam.target = camGoal.target.slice(); camGoal = null;
    }
  }

  const isCoarse = Boolean(window.matchMedia && window.matchMedia("(pointer: coarse)").matches);
  function resizeCanvas() {
    const dpr = Math.min(window.devicePixelRatio || 1, isCoarse ? 1.5 : 2.0);
    const w = Math.max(120, Math.floor(canvas.clientWidth * dpr)), h = Math.max(80, Math.floor(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    return [w, h];
  }

  let lastTs = performance.now(), fpsAccum = 0, fpsFrames = 0, uiFrame = 0;
  function frame(ts) {
    const dt = Math.min(0.05, Math.max(0.001, (ts - lastTs) * 0.001)); lastTs = ts;
    updateCameraAndKeys(dt);
    if (activeWorld) {
      const [w, h] = resizeCanvas(), { ripples, dynGlows, visibleAnts } = buildFrameScene(dt);
      const ls = LIGHT_SCENARIOS[uiState.lighting] || LIGHT_SCENARIOS.daylight;
      const glows = dynGlows.slice(0, 8);
      if (ls.glowMode === "night" || ls.glowMode === "neon") {
        for (const hl of activeWorld.hills) if (glows.length < 8) glows.push([hl.x, hl.z, 3.8, 1.0, 1.75, 0.72, 0.22]);
      }
      renderer.drawScene(activeWorld.scene, cam, {
        width: w, height: h, lighting: validLighting(uiState.lighting) || "daylight",
        dark: true, team: SEAT_TEAMS[uiState.seat] || "green", idleAnim: !uiState.paused,
        fixedIdleTime: uiState.paused ? uiState.simTime : null, clipName: "Idle",
        showPlinth: false, sunDir: sunVectorFromAngles(ls.elev, ls.azim), shadowMap: uiState.castShadows,
        skipGrassShadows: cam.zoom > 1.15, shadowRadius: clamp(BASE_FIT_RADIUS * cam.zoom * 1.65, 9.5, 36.0), ...getCamOpts(),
        tileGround: {
          cols: activeWorld.cols, rows: activeWorld.rows, atlasCols: (mapsCatalog && mapsCatalog.atlas_cols) || 16,
          atlasRows: (mapsCatalog && mapsCatalog.atlas_rows) || 15, waterTile: (mapsCatalog && mapsCatalog.water_tile_index) || 45,
          gridAlpha: uiState.showGrid ? ls.gridAlpha : 0, gridColor: ls.gridCol, crispPixels: !uiState.hiResTiles,
          pbrFloor: uiState.hiResTiles, shadowStrength: ls.shadowStr, time: uiState.simTime, glows, ripples,
        },
      });
      updateCameraMatrices();
      if ((uiFrame++ & 1) === 0) {
        const own = [];
        for (const a of antStates.values()) if (a.team === uiState.seat) own.push(a);
        hud.updateSelection(getSelectedOwnAnts(), own, uiState.abilityMode, uiState.boxSelectMode);
        const bubbles = (latestTick && latestTick.bubbles ? latestTick.bubbles : []).map((b) => ({ wx: b[0] / 32, wz: b[1] / 32, pts: b[2], team: b[3], ageMs: b[4] }));
        hud.updateFloatingOverlays(visibleAnts, bubbles, selectedIds, worldToScreen);
        hud.updateMinimap(latestTick, uiState.seat, getMinimapCameraQuad(), fogBits);
      }
    }
    fpsAccum += dt; fpsFrames++;
    if (fpsAccum >= 0.5) { hud.setFps(Math.round(fpsFrames / fpsAccum)); fpsAccum = 0; fpsFrames = 0; }
    requestAnimationFrame(frame);
  }

  window.__playClient = {
    get ready() { return Boolean(activeWorld && $("play-loading").hidden); },
    get mode() { return uiState.mode; },
    get selectedIds() { return [...selectedIds]; },
    get antCount() { return antStates.size; },
    get cam() { return { yaw: cam.yaw, pitch: cam.pitch, zoom: cam.zoom, target: cam.target.slice(), projection: cam.projection, preset: uiState.camPreset }; },
    setCamPreset, setLightingPreset, screenToWorldXZ, worldToScreen, triggerHatch, sendCommand,
    setMarquee: (rect) => hud.setMarquee(rect),
    setCam(yaw, pitch, zoom, target, proj) { camGoal = null; cam.yaw = yaw; cam.pitch = pitch; cam.zoom = zoom; if (target) cam.target = target.slice(); if (proj) cam.projection = proj; updateCameraMatrices(); },
    issueMockExplosion(x, z) { if (mockRunner) mockRunner.pushExplosion(x, z); },
    addOrderMarker(kind, x, z) { localMarkers.push({ kind, x, z, t0: uiState.simTime }); },
  };

  hud.setCamLabel(CAM_PRESETS[initCamKey].label);
  hud.setLightLabel(LIGHT_SCENARIOS[initLightKey].label, initLightKey);
  if (params.get("mock") === "1") startMockMatch({ map: params.get("map") || "TREASURE.LVL", fog: params.get("fog") === "1", seat: Number(params.get("seat") || 0) });
  else connectWebSocket();
  requestAnimationFrame(frame);
})();

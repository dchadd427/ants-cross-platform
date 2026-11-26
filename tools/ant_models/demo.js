"use strict";

(() => {
  const { clamp, mat4FromTRS, parseGlb } = window.__antGltf;
  const {
    DEG, TEAMS, LIGHTING_ORDER, validLighting, populateLightingSelect,
    wrapAngleDeg, AntRenderer, installToolbarRovingTabindex,
  } = window.__antRender;

  const $ = (id) => document.getElementById(id);

  // Exact 1:1 scale relative to 1 world unit = 1 original 32x32 game tile:
  // Original worker sprite agst301 is 23x40 px (0.72 x 1.25 tiles); worker.glb at 1.15 projects 1.25 tiles tall at 45 deg.
  const ANT_SCALE = 1.15;
  const BASE_FIT_RADIUS = 11.0;

  const CAM_PRESETS = {
    game:  { label: "Game View", yaw:   0, pitch: 50, zoom: 0.78, proj: "ortho" },
    rts3d: { label: "3D RTS",    yaw: -14, pitch: 42, zoom: 0.80, proj: "perspective" },
    hero:  { label: "Low Hero",  yaw: -22, pitch: 22, zoom: 0.56, proj: "perspective" },
    top:   { label: "Top-Down",  yaw:   0, pitch: 80, zoom: 0.92, proj: "ortho" },
  };

  // Curated regional focus bookmarks for each original 1998 .LVL map.
  const MAP_FOCUSES = {
    treasure: [
      { id: "center",   label: "Center Crossroads", target: [30.5, 0.3, 29.0], zoom: 0.92 },
      { id: "can",      label: "Can & Slingshot",   target: [15.5, 0.3, 14.0], zoom: 0.66 },
      { id: "steak",    label: "Steak Whiz & Bear", target: [38.5, 0.3,  6.5], zoom: 0.78 },
      { id: "slinky",   label: "Slinky & Glasses",  target: [10.0, 0.3, 47.5], zoom: 0.72 },
      { id: "chicken",  label: "Chicken & Pond",    target: [38.5, 0.3, 45.0], zoom: 0.72 },
      { id: "pizza",    label: "Pizza Tube",        target: [ 4.5, 0.3, 28.5], zoom: 0.52 },
      { id: "full",     label: "Full 60×60 Map",    target: [30.0, 0.2, 30.0], zoom: 2.55 },
    ],
    tiny: [
      { id: "center",   label: "Center Arena",      target: [15.5, 0.3, 15.5], zoom: 0.95 },
      { id: "green",    label: "Green Corner",      target: [ 7.0, 0.3,  7.0], zoom: 0.62 },
      { id: "red",      label: "Red Corner",        target: [24.0, 0.3, 24.0], zoom: 0.62 },
      { id: "full",     label: "Full 31×31 Map",    target: [15.5, 0.2, 15.5], zoom: 1.48 },
    ],
    small: [
      { id: "center",   label: "Center Pond",       target: [20.0, 0.3, 20.0], zoom: 0.96 },
      { id: "full",     label: "Full 40×40 Map",    target: [20.0, 0.2, 20.0], zoom: 1.85 },
    ],
    islands: [
      { id: "center",   label: "Center Islands",    target: [30.0, 0.3, 30.0], zoom: 1.05 },
      { id: "full",     label: "Full 60×60 Map",    target: [30.0, 0.2, 30.0], zoom: 2.55 },
    ],
  };

  // Extra active ants placed around Treasure Map's signature outer landmarks so every region feels alive.
  const TREASURE_LANDMARK_ANTS = [
    // Center mud crossroads clash beneath the giant flowers & clovers
    { cls: "combat", team: "black", clip: "Attack",  x: 28.8, z: 27.9, face: [30.4, 29.3], phase: 0.05 },
    { cls: "combat", team: "green", clip: "Attack",  x: 31.6, z: 28.1, face: [29.8, 29.4], phase: 0.32 },
    { cls: "combat", team: "red",   clip: "Attack",  x: 28.9, z: 30.9, face: [30.6, 29.5], phase: 0.58 },
    { cls: "combat", team: "blue",  clip: "Attack",  x: 31.5, z: 30.8, face: [29.7, 29.4], phase: 0.81 },
    // NW Soda Can, Cola Spill & Slingshot raid (Black & Green scouts)
    { cls: "worker", team: "black", clip: "Harvest", x: 16.5, z: 18.2, face: [16.6, 15.9], phase: 0.15 },
    { cls: "worker", team: "black", clip: "Walk",    phase: 0.0, speed: 1.25,
      path: [[21.0, 21.0], [18.5, 18.0], [16.0, 17.5], [19.2, 19.6]] },
    { cls: "thief",  team: "black", clip: "Walk",    phase: 0.4, speed: 1.45,
      path: [[10.5, 16.5], [15.5, 10.5], [18.5, 14.2], [13.2, 17.2]] },
    { cls: "fire",   team: "green", clip: "StartFire", x: 19.2, z: 15.2, face: [16.6, 15.9], phase: 0.5 },
    // North Steak Whiz, Shirt Button & Teddy Bear Body (Green & Black)
    { cls: "worker", team: "green", clip: "Harvest", x: 30.2, z:  6.1, face: [29.5, 5.2], phase: 0.2 },
    { cls: "worker", team: "green", clip: "Walk",    phase: 0.25, speed: 1.30,
      path: [[36.5, 19.2], [35.0, 11.5], [32.8, 6.8], [36.2, 12.2]] },
    { cls: "thief",  team: "green", clip: "Walk",    phase: 0.6, speed: 1.45,
      path: [[42.5,  8.5], [46.5, 11.2], [49.5, 6.5], [44.0, 4.5]] },
    // SW Metal Slinky, Eyeglasses & Stick (Red squad)
    { cls: "combat", team: "red",   clip: "Walk",    phase: 0.1, speed: 1.35,
      path: [[ 9.5, 45.5], [15.5, 47.2], [13.5, 53.0], [ 8.5, 49.5]] },
    { cls: "bomber", team: "red",   clip: "PlantBomb", x: 8.6, z: 45.4, face: [6.0, 44.5], phase: 0.3 },
    { cls: "worker", team: "red",   clip: "Harvest", x: 28.4, z: 54.8, face: [30.0, 55.0], phase: 0.7 },
    // SE Roast Chicken, Mushroom & Potted Meat (Blue squad)
    { cls: "worker", team: "blue",  clip: "Harvest", x: 40.8, z: 46.5, face: [43.0, 47.0], phase: 0.1 },
    { cls: "worker", team: "blue",  clip: "Walk",    phase: 0.2, speed: 1.30,
      path: [[38.8, 38.8], [40.5, 43.5], [41.5, 45.5], [38.2, 41.5]] },
    { cls: "combat", team: "blue",  clip: "Idle",    x: 35.8, z: 44.2, face: [33.0, 44.0], phase: 0.4 },
    { cls: "worker", team: "blue",  clip: "Harvest", x: 54.1, z: 30.2, face: [56.0, 29.5], phase: 0.6 },
    // West Pizza Tube & Slice (Red vs Black contest)
    { cls: "worker", team: "red",   clip: "Harvest", x:  5.8, z: 29.8, face: [4.2, 29.0], phase: 0.25 },
    { cls: "combat", team: "black", clip: "Attack",  x:  6.4, z: 28.1, face: [5.8, 29.8], phase: 0.55 },
  ];

  const MAP_ORDER = ["treasure", "tiny", "small", "islands"];
  const START_CLASS_CYCLE = ["worker", "combat", "worker", "thief", "bomber", "fire"];

  // --- Arc-Length Closed Path Sampler ---
  function preparePath(pts) {
    const segs = [];
    let total = 0;
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], b = pts[(i + 1) % pts.length];
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
      if (len > 1e-4) {
        segs.push({ ax: a[0], az: a[1], bx: b[0], bz: b[1], len, start: total });
        total += len;
      }
    }
    return { segs, total: Math.max(total, 1e-4) };
  }

  function samplePath(pathData, distRaw) {
    const { segs, total } = pathData;
    const d = ((distRaw % total) + total) % total;
    for (let i = 0; i < segs.length; i++) {
      const s = segs[i];
      if (d <= s.start + s.len || i === segs.length - 1) {
        const u = clamp((d - s.start) / s.len, 0, 1);
        const x = s.ax + (s.bx - s.ax) * u;
        const z = s.az + (s.bz - s.az) * u;
        const currAng = Math.atan2(s.bx - s.ax, s.bz - s.az);
        const nextSeg = segs[(i + 1) % segs.length];
        const nextAng = Math.atan2(nextSeg.bx - nextSeg.ax, nextSeg.bz - nextSeg.az);
        let yaw = currAng;
        if (u > 0.80) {
          const blend = (u - 0.80) / 0.20;
          let diff = nextAng - currAng;
          while (diff > Math.PI) diff -= Math.PI * 2;
          while (diff < -Math.PI) diff += Math.PI * 2;
          yaw = currAng + diff * (blend * blend * (3 - 2 * blend));
        }
        return { x, z, yaw };
      }
    }
    return { x: 0, z: 0, yaw: 0 };
  }

  function buildWorldMat(model, x, z, scale, yawRad, yLift = 0) {
    const minY = model && model.min ? model.min[1] : 0;
    const h = yawRad * 0.5;
    return mat4FromTRS([x, -minY * scale + yLift, z], [0, Math.sin(h), 0, Math.cos(h)], [scale, scale, scale]);
  }

  function sunVectorFromAngles(elevDeg, azimDeg) {
    const el = elevDeg * DEG;
    const az = azimDeg * DEG;
    const ce = Math.cos(el);
    return [ce * Math.sin(az), Math.sin(el), -ce * Math.cos(az)];
  }

  // --- App State ---
  const reducedMotionMq = window.matchMedia("(prefers-reduced-motion: reduce)");
  const darkMq = window.matchMedia("(prefers-color-scheme: dark)");
  let isDark = darkMq.matches;
  darkMq.addEventListener("change", (e) => { isDark = e.matches; });

  const params = new URLSearchParams(window.location.search);
  const initMap = MAP_ORDER.includes(params.get("map")) ? params.get("map") : "treasure";
  const initCamKey = CAM_PRESETS[params.get("cam")] ? params.get("cam") : "game";
  const initProj = params.get("proj") === "perspective" ? "perspective"
    : (params.get("proj") === "ortho" ? "ortho" : CAM_PRESETS[initCamKey].proj);

  const state = {
    mapId: initMap,
    camPreset: initCamKey,
    focusId: "center",
    lighting: validLighting(params.get("light")) || "daylight",
    sunElev: clamp(Number(params.get("elev") || 32), 16, 72),
    sunAzim: clamp(Number(params.get("azim") ?? -42), -180, 180),
    playing: params.get("play") !== "0",
    showGrid: params.get("grid") !== "0",
    castShadows: params.get("shadows") !== "0",
    hiResTiles: params.get("hires") !== "0",
    patrols: params.get("patrols") !== "0",
    showHud: params.get("hud") !== "0",
    simTime: Number.isFinite(Number(params.get("t"))) ? Number(params.get("t")) : 0,
    fixedTime: params.has("t") && params.get("play") === "0",
  };

  const cam = {
    yaw: CAM_PRESETS[initCamKey].yaw,
    pitch: CAM_PRESETS[initCamKey].pitch,
    zoom: CAM_PRESETS[initCamKey].zoom,
    projection: initProj,
    target: [30.5, 0.3, 29.0],
  };
  let camGoal = null;

  const canvas = $("demo-canvas");
  const renderer = new AntRenderer(canvas);
  const gl = renderer.gl;
  const modelCache = new Map();
  let mapsCatalog = null;
  let atlasLoaded = false;
  let activeMap = null;
  let loadSerial = 0;

  async function loadModel(key, url) {
    if (modelCache.has(key)) return modelCache.get(key);
    const p = (async () => {
      let lastErr;
      for (let attempt = 0; attempt < 3; attempt++) {
        try {
          const res = await fetch(url);
          if (!res.ok) throw new Error(`HTTP ${res.status} loading ${url}`);
          const ab = await res.arrayBuffer();
          return parseGlb(ab, gl);
        } catch (err) {
          lastErr = err;
          await new Promise((r) => setTimeout(r, 60 * (attempt + 1)));
        }
      }
      modelCache.delete(key);
      throw lastErr;
    })();
    modelCache.set(key, p);
    return p;
  }

  async function runPool(items, limit, fn) {
    let idx = 0, done = 0;
    const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
      while (idx < items.length) {
        const cur = items[idx++];
        await fn(cur, ++done);
      }
    });
    await Promise.all(workers);
  }

  function loadImage(url) {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.decoding = "async";
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(`Failed to load ${url}`));
      img.src = url;
    });
  }

  function setStatus(msg) {
    const el = $("status-region");
    if (el) el.textContent = msg;
  }

  function setCamGoal(target, yaw, pitch, zoom) {
    if (reducedMotionMq.matches) {
      cam.target = target.slice();
      cam.yaw = yaw;
      cam.pitch = pitch;
      cam.zoom = zoom;
      camGoal = null;
    } else {
      camGoal = { target: target.slice(), yaw, pitch, zoom };
    }
  }

  function buildAntSpecsForMap(mapData) {
    const specs = [];
    const teamCounter = { black: 0, green: 0, red: 0, blue: 0 };
    const cx = mapData.cols * 0.5, cz = mapData.rows * 0.5;

    // 1. Every authentic team starting ant from Block 1 of the original .LVL file
    for (const st of mapData.starts) {
      const t = st.team;
      const idx = teamCounter[t] || 0;
      teamCounter[t] = idx + 1;
      const cls = START_CLASS_CYCLE[idx % START_CLASS_CYCLE.length];
      const sx = st.col + 0.5, sz = st.row + 0.5;
      const hill = mapData.hills[t];
      // Give the first worker of each team a live foraging loop between its hill and start tile
      if (cls === "worker" && idx === 0 && hill) {
        const mx = (sx + cx) * 0.5, mz = (sz + cz) * 0.5;
        specs.push({
          cls, team: t, clip: "Walk", phase: (idx * 0.23 + t.length * 0.11) % 1, speed: 1.25,
          path: [[sx, sz], [mx, mz], [sx + (hill[0] - sx) * 0.4, sz + (hill[1] - sz) * 0.4]],
        });
      } else {
        const clip = cls === "thief" ? "IdleLookout" : (cls === "combat" ? "Idle" : "Idle");
        specs.push({ cls, team: t, clip, x: sx, z: sz, face: [cx, cz], phase: (idx * 0.31) % 1 });
      }
    }

    // 2. Swimmer ants actively swimming in every real water pond on the map!
    const pondTeams = ["blue", "green", "red", "black"];
    for (let i = 0; i < (mapData.ponds || []).length; i++) {
      const [px, pz, cells] = mapData.ponds[i];
      const rad = clamp(Math.sqrt(cells) * 0.22, 0.7, 1.35);
      specs.push({
        cls: "swimmer",
        team: pondTeams[i % pondTeams.length],
        clip: "Swim",
        phase: (i * 0.27) % 1,
        speed: 1.05,
        yLift: -0.18,
        path: [
          [px - rad, pz - rad * 0.5],
          [px + rad, pz - rad * 0.5],
          [px + rad, pz + rad * 0.5],
          [px - rad, pz + rad * 0.5],
        ],
      });
    }

    // 3. Curated active landmark battles & harvesters on Treasure Map
    if (mapData.id === "treasure") {
      for (const a of TREASURE_LANDMARK_ANTS) specs.push(a);
    }
    return specs;
  }

  function renderHudAndSidebar(mapData, antCount) {
    const teams = Object.keys(mapData.hills || {});
    $("hud-title").textContent = `${mapData.label} (${mapData.file})`;
    $("hud-desc").textContent =
      `Original ${mapData.cols}×${mapData.rows} tile grid at 1:1 scale — ${mapData.objects.length} placed 3D objects, ${teams.length} anthills, ${mapData.ponds.length} water ponds, and angled directional shadows.`;
    const teamsEl = $("hud-teams");
    teamsEl.textContent = "";
    for (const tid of ["green", "red", "blue", "black"]) {
      if (!teams.includes(tid)) continue;
      const pill = document.createElement("span");
      pill.className = "demo-team-pill";
      const dot = document.createElement("span");
      dot.className = `demo-team-dot team-${tid}`;
      dot.setAttribute("aria-hidden", "true");
      pill.append(dot, document.createTextNode(TEAMS[tid].label));
      teamsEl.appendChild(pill);
    }
    $("badge-scene-stats").textContent = `${mapData.cols}×${mapData.rows} · ${antCount} ants · ${mapData.objects.length} props`;
    $("meta-grid").textContent = `${mapData.cols} × ${mapData.rows} tiles (32px)`;
    $("meta-ants").textContent = `${antCount} (${teams.length} teams, all 6 classes)`;
    $("meta-items").textContent = `${mapData.objects.length} placed 3D models`;

    const focuses = MAP_FOCUSES[mapData.id] || [
      { id: "center", label: "Center", target: [mapData.cols * 0.5, 0.3, mapData.rows * 0.5], zoom: 0.92 },
    ];
    const focusGrid = $("demo-focus-grid");
    focusGrid.textContent = "";
    for (const f of focuses) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "demo-focus-btn";
      btn.dataset.focus = f.id;
      btn.setAttribute("aria-pressed", String(state.focusId === f.id));
      btn.textContent = f.label;
      btn.addEventListener("click", () => {
        state.focusId = f.id;
        setCamGoal(f.target, cam.yaw, cam.pitch, f.zoom);
        syncUi();
        setStatus(`Focused on ${f.label} in ${mapData.label}`);
      });
      focusGrid.appendChild(btn);
    }
  }

  async function ensureCatalog() {
    if (!mapsCatalog) {
      const res = await fetch("models/demo/maps.json");
      if (!res.ok) throw new Error(`HTTP ${res.status} loading models/demo/maps.json`);
      mapsCatalog = await res.json();
    }
    if (!atlasLoaded) {
      const img = await loadImage("models/demo/ground.png");
      renderer.uploadTileAtlas(img);
      atlasLoaded = true;
    }
  }

  async function activateMap(mapId, preserveCam = false) {
    const serial = ++loadSerial;
    state.mapId = mapId;
    state.focusId = "center";
    const loadingEl = $("demo-loading");
    loadingEl.hidden = false;
    $("demo-loading-title").textContent = "Loading Original 1998 Map…";
    $("demo-loading-msg").textContent = "Streaming original terrain tiles and 1:1 3D models.";
    syncUi();

    try {
      await ensureCatalog();
      if (serial !== loadSerial) return;
      const mapData = mapsCatalog.maps.find((m) => m.id === mapId) || mapsCatalog.maps[0];
      state.mapId = mapData.id;
      $("demo-loading-title").textContent = `Loading ${mapData.label} (${mapData.cols}×${mapData.rows})…`;

      const antSpecs = buildAntSpecsForMap(mapData);
      const uniqueAnts = [...new Set(antSpecs.map((a) => a.cls))];
      const uniqueItems = [...new Set(mapData.objects.map((o) => o.id))];
      const queue = [
        ...uniqueAnts.map((cls) => [`ant:${cls}`, `models/${cls}.glb`]),
        ...uniqueItems.map((id) => [`item:${id}`, `models/items/${id}.glb`]),
      ];
      let loadedCount = 0;
      await runPool(queue, 8, async ([key, url]) => {
        await loadModel(key, url);
        loadedCount++;
        if (serial === loadSerial && (loadedCount % 6 === 0 || loadedCount === queue.length)) {
          $("demo-loading-msg").textContent = `Streaming 3D models (${loadedCount} / ${queue.length})…`;
        }
      });
      if (serial !== loadSerial) return;

      renderer.uploadMapGrid(mapData.cols, mapData.rows, mapData.tiles);

      const itemEntries = [];
      for (let i = 0; i < mapData.objects.length; i++) {
        const spec = mapData.objects[i];
        const model = await modelCache.get(`item:${spec.id}`);
        const radius = 0.65 * Math.max(model.size[0], model.size[2], 0.8) * spec.s;
        const inst = {
          kind: "item",
          id: spec.id,
          model,
          clipName: "Idle",
          idleTime: ((i * 0.29) % 1.1),
          offsetMat: buildWorldMat(model, spec.x, spec.z, spec.s, 0),
        };
        itemEntries.push({ inst, spec, x: spec.x, z: spec.z, radius });
      }

      const antEntries = [];
      for (let i = 0; i < antSpecs.length; i++) {
        const spec = antSpecs[i];
        const model = await modelCache.get(`ant:${spec.cls}`);
        const pathData = spec.path ? preparePath(spec.path) : null;
        let x = spec.x || 0, z = spec.z || 0, yawRad = (spec.yaw || 0) * DEG;
        if (pathData) {
          const p0 = samplePath(pathData, (spec.phase || 0) * pathData.total);
          x = p0.x; z = p0.z; yawRad = p0.yaw;
        } else if (spec.face) {
          yawRad = Math.atan2(spec.face[0] - x, spec.face[1] - z);
        }
        const inst = {
          kind: "ant",
          cls: spec.cls,
          teamColors: TEAMS[spec.team] || TEAMS.green,
          model,
          clipName: spec.clip || "Idle",
          idleTime: (spec.phase || 0) * 1.4,
          blink: { active: false, elapsed: 0, nextIn: 1.4 + ((i * 1.73) % 3.6) },
          offsetMat: buildWorldMat(model, x, z, ANT_SCALE, yawRad, spec.yLift || 0),
        };
        antEntries.push({ inst, spec, pathData, x, z, yawRad });
      }

      activeMap = {
        mapData,
        itemEntries,
        antEntries,
        scene: {
          instances: [],
          target: [mapData.cols * 0.5, 0.3, mapData.rows * 0.5],
          fitRadius: BASE_FIT_RADIUS,
          halfWidth: BASE_FIT_RADIUS * 1.35,
          plinthSpanX: 0,
          plinthRadius: 1,
          shadows: [],
        },
      };

      const focuses = MAP_FOCUSES[mapData.id] || [];
      const initFocus = focuses.find((f) => f.id === params.get("focus")) || focuses[0];
      if (!preserveCam || !activeMap._inited) {
        const preset = CAM_PRESETS[state.camPreset] || CAM_PRESETS.game;
        const tgt = initFocus ? initFocus.target : [mapData.cols * 0.5, 0.3, mapData.rows * 0.5];
        const zm = initFocus ? initFocus.zoom : preset.zoom;
        state.focusId = initFocus ? initFocus.id : "center";
        if (preserveCam) {
          cam.target = tgt.slice();
          cam.yaw = preset.yaw;
          cam.pitch = preset.pitch;
          cam.zoom = zm;
          camGoal = null;
        } else {
          setCamGoal(tgt, preset.yaw, preset.pitch, zm);
        }
        activeMap._inited = true;
      }

      renderHudAndSidebar(mapData, antEntries.length);
      loadingEl.hidden = true;
      syncUi();
      setStatus(`Loaded ${mapData.label}: ${mapData.cols}×${mapData.rows} original tiles, ${mapData.objects.length} 3D objects, ${antEntries.length} animated ants.`);
    } catch (err) {
      if (serial !== loadSerial) return;
      $("demo-loading-title").textContent = "Unable to load map";
      $("demo-loading-msg").textContent = String(err && err.message ? err.message : err);
      setStatus(`Load error: ${err && err.message ? err.message : err}`);
    }
  }

  function updateSimulation(dt) {
    if (!activeMap) return;
    const { itemEntries, antEntries, scene } = activeMap;
    if (state.playing && !state.fixedTime) {
      state.simTime += dt;
    }
    const t = state.simTime;

    // View-frustum culling around camera target (+ generous margin for angled shadow casters)
    const visHalfSpan = BASE_FIT_RADIUS * cam.zoom * 1.75 + 9.0;
    const tx = cam.target[0], tz = cam.target[2];
    const visible = [];

    for (let i = 0; i < itemEntries.length; i++) {
      const e = itemEntries[i];
      if (Math.abs(e.x - tx) > visHalfSpan + e.radius || Math.abs(e.z - tz) > visHalfSpan + e.radius) continue;
      e.inst.idleTime = t + ((i * 0.29) % 1.1);
      visible.push(e.inst);
    }

    for (const e of antEntries) {
      const { inst, spec, pathData } = e;
      if (pathData) {
        const travel = state.patrols ? t * (spec.speed || 1.2) : 0;
        const pose = samplePath(pathData, (spec.phase || 0) * pathData.total + travel);
        e.x = pose.x;
        e.z = pose.z;
        e.yawRad = pose.yaw;
        inst.clipName = state.patrols ? (spec.clip || "Walk") : "Idle";
        inst.offsetMat = buildWorldMat(inst.model, e.x, e.z, ANT_SCALE, e.yawRad, spec.yLift || 0);
      }
      if (Math.abs(e.x - tx) > visHalfSpan + 2.0 || Math.abs(e.z - tz) > visHalfSpan + 2.0) continue;
      inst.idleTime = t + (spec.phase || 0) * 1.4;
      if (state.playing && !state.fixedTime && inst.blink) {
        if (inst.blink.active) {
          inst.blink.elapsed += dt;
          if (inst.blink.elapsed >= 0.22) {
            inst.blink.active = false;
            inst.blink.elapsed = 0;
            inst.blink.nextIn = 2.0 + Math.random() * 3.8;
          }
        } else {
          inst.blink.nextIn -= dt;
          if (inst.blink.nextIn <= 0) {
            inst.blink.active = true;
            inst.blink.elapsed = 0;
          }
        }
      }
      visible.push(inst);
    }
    scene.instances = visible;
  }

  function updateCamera(dt) {
    if (!camGoal) return;
    const alpha = 1 - Math.exp(-dt * 8.0);
    let dYaw = camGoal.yaw - cam.yaw;
    while (dYaw > 180) dYaw -= 360;
    while (dYaw < -180) dYaw += 360;
    cam.yaw = wrapAngleDeg(cam.yaw + dYaw * alpha);
    cam.pitch += (camGoal.pitch - cam.pitch) * alpha;
    cam.zoom += (camGoal.zoom - cam.zoom) * alpha;
    for (let i = 0; i < 3; i++) cam.target[i] += (camGoal.target[i] - cam.target[i]) * alpha;
    const err = Math.abs(dYaw) + Math.abs(camGoal.pitch - cam.pitch) + Math.abs(camGoal.zoom - cam.zoom) * 35
      + Math.hypot(camGoal.target[0] - cam.target[0], camGoal.target[2] - cam.target[2]) * 14;
    if (err < 0.08) {
      cam.yaw = camGoal.yaw;
      cam.pitch = camGoal.pitch;
      cam.zoom = camGoal.zoom;
      cam.target = camGoal.target.slice();
      camGoal = null;
    }
  }

  function resizeCanvas() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(120, Math.floor(canvas.clientWidth * dpr));
    const h = Math.max(80, Math.floor(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    return [w, h];
  }

  // --- RTS Map Pan (Left-Drag) & 3D Orbit (Right-Drag / Shift+Drag) + Zoom ---
  const ptr = { pointers: new Map(), mode: "pan", lastX: 0, lastY: 0, lastPinch: 0 };

  function clampTarget() {
    if (!activeMap) return;
    const { cols, rows } = activeMap.mapData;
    cam.target[0] = clamp(cam.target[0], 1, cols - 1);
    cam.target[2] = clamp(cam.target[2], 1, rows - 1);
  }

  function panByPixels(dx, dy) {
    camGoal = null;
    const viewH = Math.max(canvas.clientHeight, 300);
    const worldPerPx = (2.0 * BASE_FIT_RADIUS * cam.zoom) / viewH;
    const yawRad = cam.yaw * DEG;
    const pitchSin = Math.max(0.32, Math.sin(cam.pitch * DEG));
    const rightX = Math.cos(yawRad), rightZ = -Math.sin(yawRad);
    const fwdX = -Math.sin(yawRad), fwdZ = -Math.cos(yawRad);
    cam.target[0] -= (rightX * dx - fwdX * (dy / pitchSin)) * worldPerPx;
    cam.target[2] -= (rightZ * dx - fwdZ * (dy / pitchSin)) * worldPerPx;
    clampTarget();
    state.focusId = "";
    syncFocusButtons();
  }

  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  canvas.addEventListener("pointerdown", (e) => {
    canvas.setPointerCapture(e.pointerId);
    ptr.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    camGoal = null;
    if (ptr.pointers.size === 1) {
      ptr.mode = (e.button === 2 || e.button === 1 || e.shiftKey) ? "orbit" : "pan";
      ptr.lastX = e.clientX;
      ptr.lastY = e.clientY;
    } else if (ptr.pointers.size === 2) {
      const [a, b] = [...ptr.pointers.values()];
      ptr.lastPinch = Math.hypot(b.x - a.x, b.y - a.y);
      ptr.lastX = 0.5 * (a.x + b.x);
      ptr.lastY = 0.5 * (a.y + b.y);
    }
  });

  canvas.addEventListener("pointermove", (e) => {
    if (!ptr.pointers.has(e.pointerId)) return;
    ptr.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (ptr.pointers.size === 1) {
      const dx = e.clientX - ptr.lastX;
      const dy = e.clientY - ptr.lastY;
      ptr.lastX = e.clientX;
      ptr.lastY = e.clientY;
      if (ptr.mode === "pan") {
        panByPixels(dx, dy);
      } else {
        cam.yaw = wrapAngleDeg(cam.yaw - dx * 0.34);
        cam.pitch = clamp(cam.pitch + dy * 0.28, 12, 86);
        state.camPreset = "";
        syncCamButtons();
      }
    } else if (ptr.pointers.size === 2) {
      const [a, b] = [...ptr.pointers.values()];
      const cx = 0.5 * (a.x + b.x), cy = 0.5 * (a.y + b.y);
      const dist = Math.hypot(b.x - a.x, b.y - a.y);
      if (ptr.lastPinch > 1 && dist > 1) {
        cam.zoom = clamp(cam.zoom * (ptr.lastPinch / dist), 0.24, 3.0);
      }
      panByPixels(cx - ptr.lastX, cy - ptr.lastY);
      ptr.lastPinch = dist;
      ptr.lastX = cx;
      ptr.lastY = cy;
    }
  });

  const endPtr = (e) => {
    ptr.pointers.delete(e.pointerId);
    if (ptr.pointers.size === 1) {
      const [rem] = [...ptr.pointers.values()];
      ptr.lastX = rem.x;
      ptr.lastY = rem.y;
      ptr.mode = "pan";
    }
  };
  canvas.addEventListener("pointerup", endPtr);
  canvas.addEventListener("pointercancel", endPtr);
  canvas.addEventListener("lostpointercapture", endPtr);

  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    camGoal = null;
    const factor = Math.exp(clamp(e.deltaY, -180, 180) * 0.0014);
    cam.zoom = clamp(cam.zoom * factor, 0.24, 3.0);
  }, { passive: false });

  // --- UI Synchronization ---
  function syncCamButtons() {
    document.querySelectorAll("#demo-dock [data-cam]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.cam === state.camPreset));
    });
  }

  function syncFocusButtons() {
    document.querySelectorAll("#demo-focus-grid [data-focus]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.focus === state.focusId));
    });
    const focuses = MAP_FOCUSES[state.mapId] || [];
    const activeFocus = focuses.find((f) => f.id === state.focusId);
    $("focus-readout").textContent = activeFocus ? activeFocus.label : "Custom";
  }

  function syncUi() {
    document.querySelectorAll("#map-group [data-map]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.map === state.mapId));
    });
    document.querySelectorAll("#demo-dock [data-proj]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.proj === cam.projection));
    });
    $("btn-play").setAttribute("aria-pressed", String(state.playing));
    $("btn-grid").setAttribute("aria-pressed", String(state.showGrid));
    $("btn-shadows").setAttribute("aria-pressed", String(state.castShadows));
    $("btn-hires-tiles").setAttribute("aria-pressed", String(state.hiResTiles));
    $("btn-patrols").setAttribute("aria-pressed", String(state.patrols));
    $("btn-hud").setAttribute("aria-pressed", String(state.showHud));
    $("demo-hud-card").hidden = !state.showHud;
    $("range-sun-elev").value = String(Math.round(state.sunElev));
    $("out-sun-elev").textContent = `${Math.round(state.sunElev)}°`;
    $("range-sun-azim").value = String(Math.round(state.sunAzim));
    $("out-sun-azim").textContent = `${Math.round(state.sunAzim)}°`;
    populateLightingSelect($("select-lighting"), state.lighting);
    syncCamButtons();
    syncFocusButtons();
  }

  // --- Control Bindings ---
  document.querySelectorAll("#map-group [data-map]").forEach((btn) => {
    btn.addEventListener("click", () => {
      if (btn.dataset.map !== state.mapId) activateMap(btn.dataset.map, false);
    });
  });

  document.querySelectorAll("#demo-dock [data-cam]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.dataset.cam;
      const p = CAM_PRESETS[key];
      if (!p) return;
      state.camPreset = key;
      cam.projection = p.proj;
      setCamGoal(cam.target, p.yaw, p.pitch, Math.min(cam.zoom, key === "hero" ? 0.56 : 1.0));
      syncUi();
      setStatus(`Camera view: ${p.label}`);
    });
  });

  document.querySelectorAll("#demo-dock [data-proj]").forEach((btn) => {
    btn.addEventListener("click", () => {
      cam.projection = btn.dataset.proj === "ortho" ? "ortho" : "perspective";
      syncUi();
    });
  });

  $("btn-play").addEventListener("click", () => {
    state.playing = !state.playing;
    state.fixedTime = false;
    syncUi();
    setStatus(state.playing ? "Animations playing" : "Animations paused");
  });

  $("btn-grid").addEventListener("click", () => {
    state.showGrid = !state.showGrid;
    syncUi();
    setStatus(state.showGrid ? "32px tile grid overlay on" : "Tile grid overlay hidden");
  });

  $("btn-reset").addEventListener("click", () => {
    const p = CAM_PRESETS.game;
    const focuses = MAP_FOCUSES[state.mapId] || [];
    const f0 = focuses[0];
    state.camPreset = "game";
    state.focusId = f0 ? f0.id : "center";
    cam.projection = p.proj;
    setCamGoal(f0 ? f0.target : cam.target, p.yaw, p.pitch, f0 ? f0.zoom : p.zoom);
    syncUi();
    setStatus("Camera reset to Game View");
  });

  $("btn-shadows").addEventListener("click", () => { state.castShadows = !state.castShadows; syncUi(); });
  $("btn-hires-tiles").addEventListener("click", () => { state.hiResTiles = !state.hiResTiles; syncUi(); });
  $("btn-patrols").addEventListener("click", () => { state.patrols = !state.patrols; syncUi(); });
  $("btn-hud").addEventListener("click", () => { state.showHud = !state.showHud; syncUi(); });

  $("range-sun-elev").addEventListener("input", (e) => { state.sunElev = Number(e.target.value); syncUi(); });
  $("range-sun-azim").addEventListener("input", (e) => { state.sunAzim = Number(e.target.value); syncUi(); });

  const selectLighting = $("select-lighting");
  selectLighting.addEventListener("change", () => {
    state.lighting = validLighting(selectLighting.value) || "daylight";
    syncUi();
  });
  const stepLighting = (dir) => {
    const idx = Math.max(0, LIGHTING_ORDER.indexOf(state.lighting));
    state.lighting = LIGHTING_ORDER[(idx + dir + LIGHTING_ORDER.length) % LIGHTING_ORDER.length];
    syncUi();
  };
  $("btn-light-prev").addEventListener("click", () => stepLighting(-1));
  $("btn-light-next").addEventListener("click", () => stepLighting(1));

  // Responsive controls drawer
  const panelEl = $("inspector-panel");
  const backdropEl = $("inspector-backdrop");
  const toggleBtn = $("btn-toggle-panel");
  let panelOpen = window.innerWidth > 960;
  function applyPanelState() {
    const narrow = window.innerWidth <= 960;
    panelEl.classList.toggle("is-collapsed", !panelOpen);
    panelEl.classList.toggle("is-open", narrow && panelOpen);
    backdropEl.hidden = !(narrow && panelOpen);
    toggleBtn.setAttribute("aria-expanded", String(panelOpen));
  }
  toggleBtn.addEventListener("click", () => { panelOpen = !panelOpen; applyPanelState(); });
  $("btn-close-panel").addEventListener("click", () => { panelOpen = false; applyPanelState(); });
  backdropEl.addEventListener("click", () => { panelOpen = false; applyPanelState(); });
  window.addEventListener("resize", applyPanelState);
  applyPanelState();

  // Keyboard shortcuts
  window.addEventListener("keydown", (e) => {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const tag = document.activeElement && document.activeElement.tagName;
    if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
    const k = e.key.toLowerCase();
    if (e.key >= "1" && e.key <= "4") {
      const id = MAP_ORDER[Number(e.key) - 1];
      if (id) { e.preventDefault(); activateMap(id, false); }
    } else if (e.key === " " || k === "i") {
      if (e.key === " " && tag === "BUTTON") return;
      e.preventDefault();
      $("btn-play").click();
    } else if (k === "g") {
      e.preventDefault();
      $("btn-grid").click();
    } else if (k === "r") {
      e.preventDefault();
      $("btn-reset").click();
    } else if (k === "l") {
      e.preventDefault();
      stepLighting(1);
    } else if (k === "w" || e.key === "ArrowUp")    { e.preventDefault(); panByPixels(0, 48); }
    else if (k === "s" || e.key === "ArrowDown")     { e.preventDefault(); panByPixels(0, -48); }
    else if (k === "a" || e.key === "ArrowLeft")     { e.preventDefault(); panByPixels(48, 0); }
    else if (k === "d" || e.key === "ArrowRight")    { e.preventDefault(); panByPixels(-48, 0); }
    else if (e.key === "+" || e.key === "=") { e.preventDefault(); camGoal = null; cam.zoom = clamp(cam.zoom * 0.86, 0.24, 3.0); }
    else if (e.key === "-" || e.key === "_") { e.preventDefault(); camGoal = null; cam.zoom = clamp(cam.zoom * 1.16, 0.24, 3.0); }
  });

  if (!("focusGroup" in HTMLElement.prototype)) {
    installToolbarRovingTabindex($("demo-toolbar"));
    installToolbarRovingTabindex($("demo-dock"));
  }

  // --- Main Render Loop ---
  let lastTs = performance.now();
  let fpsAccum = 0, fpsFrames = 0;
  function frame(ts) {
    const dt = Math.min(0.05, Math.max(0.001, (ts - lastTs) * 0.001));
    lastTs = ts;
    updateCamera(dt);
    updateSimulation(dt);

    if (activeMap) {
      const [w, h] = resizeCanvas();
      const { mapData, scene } = activeMap;
      const sunDir = sunVectorFromAngles(state.sunElev, state.sunAzim);
      const shadowRadius = clamp(BASE_FIT_RADIUS * cam.zoom * 1.65, 9.5, 36.0);
      renderer.drawScene(scene, cam, {
        width: w,
        height: h,
        lighting: state.lighting,
        dark: isDark,
        team: "green",
        idleAnim: state.playing,
        fixedIdleTime: state.playing ? null : state.simTime,
        clipName: "Idle",
        showPlinth: false,
        sunDir,
        shadowMap: state.castShadows,
        shadowRadius,
        customTarget: cam.target,
        tileGround: {
          cols: mapData.cols,
          rows: mapData.rows,
          atlasCols: mapsCatalog.atlas_cols,
          atlasRows: mapsCatalog.atlas_rows,
          waterTile: mapsCatalog.water_tile_index,
          gridAlpha: state.showGrid ? 0.24 : 0.0,
          crispPixels: !state.hiResTiles,
          shadowStrength: 0.68,
          time: state.simTime,
          glows: [],
        },
      });
    }

    fpsAccum += dt;
    fpsFrames++;
    if (fpsAccum >= 0.6) {
      const fps = Math.round(fpsFrames / fpsAccum);
      $("meta-fps").textContent = `${fps} fps`;
      $("meta-cam").textContent = `(${cam.target[0].toFixed(1)}, ${cam.target[2].toFixed(1)}) · ${Math.round(cam.pitch)}° · ${cam.zoom.toFixed(2)}×`;
      fpsAccum = 0;
      fpsFrames = 0;
    }
    requestAnimationFrame(frame);
  }

  window.__demoState = {
    get ready() { return Boolean(activeMap && $("demo-loading").hidden); },
    activateMap,
    setFocus(id) {
      const focuses = MAP_FOCUSES[state.mapId] || [];
      const f = focuses.find((x) => x.id === id);
      if (f) {
        state.focusId = f.id;
        cam.target = f.target.slice();
        cam.zoom = f.zoom;
        camGoal = null;
        syncUi();
      }
    },
    setPreset(key) {
      const p = CAM_PRESETS[key];
      if (p) {
        state.camPreset = key;
        cam.yaw = p.yaw; cam.pitch = p.pitch; cam.projection = p.proj;
        camGoal = null;
        syncUi();
      }
    },
    setSun(elev, azim) { state.sunElev = elev; state.sunAzim = azim; syncUi(); },
    setGrid(on) { state.showGrid = Boolean(on); syncUi(); },
    setCam(yaw, pitch, zoom, target, proj) {
      camGoal = null;
      cam.yaw = yaw; cam.pitch = pitch; cam.zoom = zoom;
      if (target) cam.target = target.slice();
      if (proj) cam.projection = proj;
      syncUi();
    },
  };

  syncUi();
  activateMap(state.mapId, true);
  requestAnimationFrame(frame);
})();

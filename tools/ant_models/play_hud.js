"use strict";

// Modern translucent 3D RTS HUD, interactive Minimap Radar, Audio A/B engine,
// procedural 3D FX/bridge geometry builders, and built-in ?mock=1 20 Hz harness.
(() => {
  const $ = (id) => document.getElementById(id);

  // Seat numbering matches ants_sim (src/ants_sim/start_teams.cpp): 0=Green, 1=Red, 2=Blue, 3=Black.
  const SEAT_TEAMS = ["green", "red", "blue", "black"];
  const SEAT_LABELS = ["Green", "Red", "Blue", "Black"];
  const SEAT_HEX = ["#3be07c", "#f24c5b", "#4e8eff", "#9ea8bd"];
  const SEAT_DOT_HEX = ["#2fd36e", "#f04656", "#4f8cff", "#c8d0e0"];
  const BOMB_IDS = ["greenbomb", "redbomb", "bluebomb", "blackbomb"];
  const HILL_IDS = ["GREENHILL", "REDHILL", "BLUEHILL", "BLACKHILL"];

  const CLASS_META = {
    worker:  { name: "Worker Ant",  short: "Wrk", badge: "W", ability: "Harvest",   canSpecial: false },
    combat:  { name: "Combat Ant",  short: "Cmb", badge: "C", ability: "Charge",    canSpecial: false },
    bomber:  { name: "Bomber Ant",  short: "Bmb", badge: "B", ability: "Bomb",      canSpecial: true  },
    swimmer: { name: "Swimmer Ant", short: "Swm", badge: "S", ability: "Bridge",    canSpecial: true  },
    fire:    { name: "Fire Ant",    short: "Fir", badge: "F", ability: "Fire Wall", canSpecial: true  },
    thief:   { name: "Thief Ant",   short: "Thf", badge: "T", ability: "Raid Hill", canSpecial: true  },
  };
  const CLASS_ORDER = ["worker", "combat", "bomber", "swimmer", "fire", "thief"];

  const POWERUP_RING_COLORS = {
    pu_mason: [1.35, 0.86, 0.14], pu_swim: [0.16, 1.05, 1.48], pu_bomb: [1.45, 0.42, 0.16],
    pu_comb: [1.42, 1.12, 0.22], pu_thief: [0.95, 0.44, 1.48],
  };

  // Authentic 1998 sound IDs (asset_catalog/sounds/sound_NNN_*.wav) from include/ants_sim/sim_engine.hpp.
  const SOUND_FILES = {
    0: "sound_000_buttonclick.wav", 1: "sound_001_powerupc.wav", 2: "sound_002_powerupc2.wav",
    3: "sound_003_combatnetfairy.wav", 4: "sound_004_bombexp.wav", 5: "sound_005_fireburnout.wav",
    13: "sound_013_gantorders.wav", 14: "sound_014_gantrdy.wav", 15: "sound_015_gantcommand.wav",
    16: "sound_016_gantattack.wav", 17: "sound_017_gantgo.wav", 18: "sound_018_theifrdy.wav",
    19: "sound_019_theifgo.wav", 20: "sound_020_theifattack.wav", 21: "sound_021_theifdo.wav",
    22: "sound_022_firerdy.wav", 23: "sound_023_firego.wav", 24: "sound_024_fireattack.wav",
    25: "sound_025_firedo.wav", 26: "sound_026_combrdy2.wav", 27: "sound_027_combrdy1.wav",
    28: "sound_028_combgo1.wav", 29: "sound_029_combgo2.wav", 30: "sound_030_combdo2.wav",
    31: "sound_031_combdo1.wav", 32: "sound_032_brdgrdy.wav", 33: "sound_033_brdggo.wav",
    34: "sound_034_brdgat.wav", 35: "sound_035_brdgdo.wav", 36: "sound_036_bombrdy.wav",
    37: "sound_037_bombgo.wav", 38: "sound_038_bombattack.wav", 39: "sound_039_bombdo.wav",
    40: "sound_040_powerupd.wav", 41: "sound_041_playerout.wav", 42: "sound_042_losers.wav",
    43: "sound_043_exithill.wav", 44: "sound_044_countdwn.wav", 45: "sound_045_chatsnda.wav",
    46: "sound_046_chatsnd.wav", 47: "sound_047_bump.wav", 48: "sound_048_anthill.wav",
    49: "sound_049_allyoff.wav", 50: "sound_050_allyon.wav", 51: "sound_051_allypro.wav",
    52: "sound_052_allynot.wav", 53: "sound_053_allyyes.wav", 54: "sound_054_30sec.wav",
    55: "sound_055_1min.wav", 56: "sound_056_winner.wav", 57: "sound_057_attack.wav",
    58: "sound_058_underattack.wav", 59: "sound_059_combat1.wav", 60: "sound_060_combat2.wav",
    61: "sound_061_antstop.wav", 62: "sound_062_powerdrip.wav", 63: "sound_063_cantgo.wav",
    64: "sound_064_flythumpa.wav", 65: "sound_065_flythumpb.wav", 66: "sound_066_harvest.wav",
    67: "sound_067_firestarta.wav", 68: "sound_068_firestartb.wav", 69: "sound_069_fireextinguish.wav",
    70: "sound_070_stun.wav", 71: "sound_071_splash.wav", 72: "sound_072_antdrown.wav",
    73: "sound_073_bombdrop.wav", 74: "sound_074_bombmuffle.wav", 75: "sound_075_attack_alt.wav",
    76: "sound_076_FlyThumpB.wav", 77: "sound_077_harvest_alt.wav", 78: "sound_078_attack2.wav",
    79: "sound_079_waterattack.wav", 80: "sound_080_wateraction.wav", 81: "sound_081_shovelgravel.wav",
    82: "sound_082_shovelwater.wav", 83: "sound_083_theifwhip.wav", 84: "sound_084_steala.wav",
    85: "sound_085_stealb.wav", 86: "sound_086_stealc.wav", 87: "sound_087_scoreup.wav",
    88: "sound_088_scoredn.wav", 89: "sound_089_navbuttonclick.wav", 90: "sound_090_bombpick.wav",
  };

  const AUDIO_MODES = ["remastered", "classic", "muted"];
  const AUDIO_LABELS = { remastered: "Remastered", classic: "1998 SFX", muted: "Muted" };

  // Delegates to window.__playAudio (play_audio.js) when loaded, with built-in Web Audio fallback.
  function createSoundPlayer() {
    let ctx = null, mode = "remastered", extInited = false, unavailable = false, missCount = 0, variant = 0;
    const buffers = new Map(), pending = new Map(), lastPlayed = new Map();

    function ext() { return window.__playAudio && typeof window.__playAudio.play === "function" ? window.__playAudio : null; }

    function ensureExt() {
      const a = ext();
      if (a && !extInited) {
        extInited = true;
        if (typeof a.init === "function") a.init("models/audio/manifest.json");
        if (typeof a.setMode === "function") a.setMode(mode);
      }
      return a;
    }

    function unlock() {
      const a = ensureExt();
      if (a && typeof a.unlock === "function") { a.unlock(); return; }
      if (mode === "muted" || unavailable) return;
      if (!ctx) {
        const Ctor = window.AudioContext || window.webkitAudioContext;
        if (!Ctor) { unavailable = true; return; }
        try { ctx = new Ctor(); } catch (_) { unavailable = true; return; }
      }
      if (ctx.state === "suspended") ctx.resume().catch(() => {});
    }

    function setMode(next) {
      mode = AUDIO_MODES.includes(next) ? next : "remastered";
      const a = ensureExt();
      if (a && typeof a.setMode === "function") a.setMode(mode);
      return mode;
    }

    async function loadSound(id) {
      if (buffers.has(id)) return buffers.get(id);
      if (pending.has(id)) return pending.get(id);
      const file = SOUND_FILES[id];
      if (!file || !ctx || unavailable) return null;
      const p = (async () => {
        try {
          const res = await fetch(`../../asset_catalog/sounds/${file}`);
          if (!res.ok) { if (++missCount >= 4) unavailable = true; return null; }
          const buf = await ctx.decodeAudioData(await res.arrayBuffer());
          buffers.set(id, buf);
          return buf;
        } catch (_) {
          if (++missCount >= 4) unavailable = true;
          return null;
        } finally { pending.delete(id); }
      })();
      pending.set(id, p);
      return p;
    }

    function play(soundId, wx, wz, camTarget) {
      if (mode === "muted") return;
      const a = ensureExt();
      if (a) {
        const cx = camTarget ? camTarget[0] : (Number.isFinite(wx) ? wx : 0);
        const cz = camTarget ? camTarget[2] : (Number.isFinite(wz) ? wz : 0);
        a.play(soundId, Number.isFinite(wx) ? wx : cx, Number.isFinite(wz) ? wz : cz, cx, cz);
        return;
      }
      if (unavailable || !SOUND_FILES[soundId] || !ctx || ctx.state !== "running") return;
      const now = performance.now();
      if (now - (lastPlayed.get(soundId) || 0) < 95) return;
      lastPlayed.set(soundId, now);
      let vol = 0.42;
      if (Number.isFinite(wx) && Number.isFinite(wz) && camTarget) {
        const dist = Math.hypot(wx - camTarget[0], wz - camTarget[2]);
        if (dist > 34) return;
        vol *= Math.max(0.14, 1.0 - dist / 38.0);
      }
      const fire = (buf) => {
        if (!buf || mode === "muted" || !ctx) return;
        try {
          const src = ctx.createBufferSource(), gain = ctx.createGain();
          src.buffer = buf; gain.gain.value = vol;
          src.connect(gain); gain.connect(ctx.destination); src.start();
        } catch (_) {}
      };
      const cached = buffers.get(soundId);
      if (cached) fire(cached); else loadSound(soundId).then(fire);
    }

    function playVoice(kind, cls) {
      const v = (variant++) & 7;
      const table = {
        ready:   { worker: v % 3 === 0 ? 13 : 14, swimmer: 32, fire: 22, combat: v % 2 ? 26 : 27, bomber: 36, thief: 18 },
        move:    { worker: v % 2 === 0 ? 17 : 15, swimmer: 33, fire: 23, combat: v % 2 ? 28 : 29, bomber: 37, thief: 19 },
        attack:  { worker: 16, swimmer: 34, fire: 24, combat: v % 2 ? 60 : 59, bomber: 38, thief: 20 },
        ability: { swimmer: 35, fire: 25, bomber: 39, thief: 21 },
      };
      const id = table[kind] && table[kind][cls];
      if (Number.isInteger(id)) play(id, NaN, NaN, null);
    }

    return { unlock, play, playVoice, getMode: () => mode, setMode };
  }

  // --- Interactive Live Minimap Radar ---
  function tileMinimapRgb(ti) {
    if (ti >= 45 && ti <= 53) return [36, 108, 178];
    if ((ti >= 0 && ti <= 12) || (ti >= 229 && ti <= 238)) return [78, 72, 66];
    if (ti >= 56 && ti <= 59) return [196, 184, 162];
    if (ti >= 140 && ti <= 148) return [134, 136, 152];
    if ((ti >= 13 && ti <= 44) || (ti >= 76 && ti <= 91)) return [102, 88, 78];
    if (ti >= 60 && ti <= 107) return [164, 146, 128];
    if (ti >= 149 && ti <= 228) return [132, 126, 128];
    return [136, 110, 88];
  }

  function createMinimap(canvas, onJump) {
    const ctx = canvas.getContext("2d");
    const terrainCanvas = document.createElement("canvas");
    const terrainCtx = terrainCanvas.getContext("2d");
    let mapCols = 60, mapRows = 60, hillsList = [], dragging = false;

    function buildTerrain(cols, rows, tiles, hills) {
      mapCols = Math.max(1, cols); mapRows = Math.max(1, rows); hillsList = hills || [];
      terrainCanvas.width = mapCols; terrainCanvas.height = mapRows;
      const img = terrainCtx.createImageData(mapCols, mapRows), d = img.data;
      for (let i = 0; i < mapCols * mapRows; i++) {
        const rgb = tileMinimapRgb(tiles ? tiles[i] : 54), o = i * 4;
        d[o] = rgb[0]; d[o + 1] = rgb[1]; d[o + 2] = rgb[2]; d[o + 3] = 255;
      }
      terrainCtx.putImageData(img, 0, 0);
    }

    function clientToWorld(cx, cy) {
      const r = canvas.getBoundingClientRect();
      return [
        Math.max(0, Math.min(1, (cx - r.left) / Math.max(1, r.width))) * mapCols,
        Math.max(0, Math.min(1, (cy - r.top) / Math.max(1, r.height))) * mapRows,
      ];
    }
    canvas.addEventListener("pointerdown", (e) => {
      e.stopPropagation(); dragging = true;
      try { canvas.setPointerCapture(e.pointerId); } catch (_) {}
      const [wx, wz] = clientToWorld(e.clientX, e.clientY); onJump(wx, wz);
    });
    canvas.addEventListener("pointermove", (e) => {
      if (!dragging) return;
      e.stopPropagation();
      const [wx, wz] = clientToWorld(e.clientX, e.clientY); onJump(wx, wz);
    });
    canvas.addEventListener("pointerup", () => { dragging = false; });
    canvas.addEventListener("pointercancel", () => { dragging = false; });

    function draw(tk, seat, camQuad, fogBits) {
      const w = canvas.width, h = canvas.height, sx = w / mapCols, sy = h / mapRows;
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(terrainCanvas, 0, 0, w, h);
      ctx.fillStyle = "rgba(10, 12, 10, 0.14)";
      ctx.fillRect(0, 0, w, h);
      const isRev = (c, r) => !fogBits || (r * mapCols + c >= 0 && fogBits[r * mapCols + c] !== 0);

      if (tk) {
        ctx.fillStyle = "#5c4d42";
        for (const b of (tk.bridges || [])) ctx.fillRect(b[0] * sx, b[1] * sy, Math.max(2, sx), Math.max(2, sy));
        ctx.fillStyle = "#f2c84b";
        for (const f of (tk.food || [])) if (isRev(f[0], f[1])) ctx.fillRect(f[0] * sx - 1, f[1] * sy - 1, Math.max(3, sx * 1.1), Math.max(3, sy * 1.1));
        ctx.fillStyle = "#5ef0ff";
        for (const p of (tk.powerups || [])) if (isRev(p[0], p[1])) { ctx.beginPath(); ctx.arc((p[0] + 0.5) * sx, (p[1] + 0.5) * sy, 2.5, 0, Math.PI * 2); ctx.fill(); }
        ctx.fillStyle = "#ff7828";
        for (const f of (tk.fires || [])) if (isRev(f[0], f[1])) ctx.fillRect(f[0] * sx - 0.5, f[1] * sy - 0.5, Math.max(2.5, sx), Math.max(2.5, sy));
      }
      for (const hl of hillsList) {
        const hx = (hl.col + 1) * sx, hy = (hl.row + 1) * sy;
        ctx.fillStyle = "#191512"; ctx.fillRect(hx - 5, hy - 5, 10, 10);
        ctx.fillStyle = SEAT_DOT_HEX[hl.team] || "#fff"; ctx.fillRect(hx - 3.5, hy - 3.5, 7, 7);
      }
      if (fogBits) {
        ctx.fillStyle = "rgba(8, 11, 10, 0.72)";
        const st = mapCols > 40 ? 2 : 1;
        for (let r = 0; r < mapRows; r += st) for (let c = 0; c < mapCols; c += st) {
          if (!fogBits[r * mapCols + c]) ctx.fillRect(c * sx, r * sy, st * sx + 0.5, st * sy + 0.5);
        }
      }
      if (tk && tk.ants) {
        for (const a of tk.ants) {
          const team = a[1], ax = a[3] / 32, az = a[4] / 32;
          if (team !== seat && !isRev(Math.floor(ax), Math.floor(az))) continue;
          ctx.fillStyle = SEAT_DOT_HEX[team] || "#fff";
          ctx.beginPath(); ctx.arc(ax * sx, az * sy, team === seat ? 2.6 : 2.1, 0, Math.PI * 2); ctx.fill();
        }
      }
      if (camQuad && camQuad.length === 4) {
        ctx.strokeStyle = "rgba(255, 255, 255, 0.92)"; ctx.lineWidth = 1.5;
        ctx.beginPath();
        for (let i = 0; i < 4; i++) {
          const px = camQuad[i][0] * sx, py = camQuad[i][1] * sy;
          if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
        }
        ctx.closePath(); ctx.stroke();
      }
    }
    return { buildTerrain, draw };
  }

  // --- Procedural 3D Gameplay Geometry (Selection Rings, X-Markers, Mud Bridges, Live FX) ---
  function makeProceduralModel(gl, pos, nrm, uv, col, idx, mat, rad) {
    const { mat4Identity } = window.__antGltf;
    const vao = gl.createVertexArray(), bufs = [];
    gl.bindVertexArray(vao);
    const up = (target, data, loc, sz) => {
      const b = gl.createBuffer(); gl.bindBuffer(target, b); gl.bufferData(target, data, gl.STATIC_DRAW);
      if (loc !== undefined) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, sz, gl.FLOAT, false, 0, 0); }
      bufs.push(b);
    };
    up(gl.ARRAY_BUFFER, new Float32Array(pos), 0, 3);
    up(gl.ARRAY_BUFFER, new Float32Array(nrm), 1, 3);
    up(gl.ARRAY_BUFFER, new Float32Array(uv), 2, 2);
    up(gl.ARRAY_BUFFER, new Float32Array(col), 3, 3);
    up(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx));
    gl.bindVertexArray(null);
    const idMat = mat4Identity();
    return {
      animations: [], idleAnims: [], blinkInfo: { hasBlink: false }, baseWorldMats: [idMat],
      bounds: { min: [-rad, 0, -rad], max: [rad, 0.2, rad] }, size: [rad * 2, 0.2, rad * 2],
      drawCalls: [{
        nodeIndex: 0, worldMat: idMat,
        prim: { vao, indexed: true, mode: gl.TRIANGLES, drawCount: idx.length, indexType: gl.UNSIGNED_SHORT, hasUv: false, localMin: [-rad, 0, -rad], localMax: [rad, 0.2, rad], material: mat },
      }],
      dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
  }

  function buildRing(gl, rgb, alpha, rIn, rOut, pip) {
    const pos = [], nrm = [], uv = [], col = [], idx = [], segs = 32;
    for (let s = 0; s <= segs; s++) {
      const a = (s / segs) * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
      pos.push(ca * rIn, 0.032, sa * rIn, ca * rOut, 0.032, sa * rOut);
      nrm.push(0, 1, 0, 0, 1, 0); uv.push(s / segs, 0, s / segs, 1); col.push(1, 1, 1, 1, 1, 1);
    }
    for (let s = 0; s < segs; s++) { const a = s * 2; idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3); }
    if (pip) {
      const b0 = pos.length / 3, z0 = rOut - 0.02, z1 = rOut + 0.12, hw = 0.13;
      pos.push(-hw, 0.036, z0, hw, 0.036, z0, hw, 0.036, z1, -hw, 0.036, z1);
      for (let k = 0; k < 4; k++) { nrm.push(0, 1, 0); uv.push(0.5, 0.5); col.push(1, 1, 1); }
      idx.push(b0, b0 + 2, b0 + 1, b0, b0 + 3, b0 + 2);
    }
    return makeProceduralModel(gl, pos, nrm, uv, col, idx, {
      name: "play_ring", baseColor: [rgb[0] * 0.35, rgb[1] * 0.35, rgb[2] * 0.35, alpha],
      metallic: 0, roughness: 0.6, normalScale: 0, normalTex: null, baseTex: null, mrTex: null,
      emissive: rgb, alphaMode: "BLEND", isTranslucent: true, doubleSided: true,
    }, rOut + 0.15);
  }

  function buildXMarker(gl, rgb) {
    const pos = [], nrm = [], uv = [], col = [], idx = [], arm = 0.42, hw = 0.075;
    for (const ang of [Math.PI * 0.25, -Math.PI * 0.25]) {
      const ca = Math.cos(ang), sa = Math.sin(ang), b0 = pos.length / 3;
      for (const [lx, lz] of [[-arm, -hw], [arm, -hw], [arm, hw], [-arm, hw]]) {
        pos.push(lx * ca - lz * sa, 0.042, lx * sa + lz * ca);
        nrm.push(0, 1, 0); uv.push(0.5, 0.5); col.push(1, 1, 1);
      }
      idx.push(b0, b0 + 2, b0 + 1, b0, b0 + 3, b0 + 2);
    }
    return makeProceduralModel(gl, pos, nrm, uv, col, idx, {
      name: "play_xmark", baseColor: [rgb[0] * 0.45, rgb[1] * 0.45, rgb[2] * 0.45, 0.94],
      metallic: 0, roughness: 0.45, normalScale: 0, normalTex: null, baseTex: null, mrTex: null,
      emissive: [rgb[0] * 1.15, rgb[1] * 1.15, rgb[2] * 1.15], alphaMode: "BLEND", isTranslucent: true, doubleSided: true,
    }, 0.55);
  }

  function buildSphereFx(gl, rgb, emissive, alpha, rough, spiky) {
    const pos = [], nrm = [], uv = [], col = [], idx = [], uS = 14, vS = 8;
    for (let v = 0; v <= vS; v++) {
      const phi = (v / vS) * Math.PI, sp = Math.sin(phi), cp = Math.cos(phi);
      for (let u = 0; u <= uS; u++) {
        const th = (u / uS) * Math.PI * 2;
        const r = spiky ? (0.58 + 0.42 * Math.pow(Math.abs(Math.sin(th * 3 + phi * 2) * Math.cos(phi * 2.5 - th * 2)), 0.7))
          : (0.86 + 0.14 * Math.sin(th * 3 + phi * 2));
        const nx = sp * Math.cos(th), ny = cp, nz = sp * Math.sin(th), sh = 0.65 + 0.35 * (0.5 + 0.5 * ny);
        pos.push(nx * r, ny * r * 0.85, nz * r); nrm.push(nx, ny, nz); uv.push(u / uS, v / vS);
        col.push(rgb[0] * sh, rgb[1] * sh, rgb[2] * sh);
      }
    }
    const st = uS + 1;
    for (let v = 0; v < vS; v++) for (let u = 0; u < uS; u++) {
      const a = v * st + u; idx.push(a, a + 1, a + st, a + 1, a + st + 1, a + st);
    }
    return makeProceduralModel(gl, pos, nrm, uv, col, idx, {
      name: "play_fx", baseColor: [1, 1, 1, alpha], metallic: 0, roughness: rough,
      normalScale: 0, normalTex: null, baseTex: null, mrTex: null,
      emissive, alphaMode: alpha < 0.99 ? "BLEND" : "OPAQUE", isTranslucent: alpha < 0.99, doubleSided: true,
    }, 1.0);
  }

  function buildMudBridge(gl, stage) {
    const pos = [], nrm = [], uv = [], col = [], idx = [], segs = 22;
    const rx = [0.27, 0.34, 0.45, 0.50][Math.max(0, Math.min(3, stage - 1))];
    const hTop = [0.024, 0.042, 0.056, 0.066][Math.max(0, Math.min(3, stage - 1))];
    const rings = [
      { y: -0.24, sc: 1.04, c: [0.018, 0.019, 0.028], ny: 0.40 },
      { y: 0.004, sc: 1.00, c: [0.026, 0.023, 0.022], ny: 0.58 },
      { y: hTop * 0.65, sc: 0.85, c: [0.054, 0.047, 0.043], ny: 0.86 },
      { y: hTop * 0.95, sc: 0.46, c: [0.078, 0.068, 0.060], ny: 0.98 },
    ];
    for (const rg of rings) {
      for (let s = 0; s < segs; s++) {
        const a = (s / segs) * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        const sq = stage >= 3 ? (1 / Math.pow(Math.pow(Math.abs(ca), 3) + Math.pow(Math.abs(sa), 3), 1 / 3)) : 1;
        let shp = 1 - 0.08 * Math.cos(a * 4) + 0.06 * Math.sin(a * 3 + stage);
        if (stage < 4) {
          let da = a - 0.85;
          while (da > Math.PI) da -= Math.PI * 2;
          while (da < -Math.PI) da += Math.PI * 2;
          shp *= (1 - 0.45 * Math.exp(-(da * da) / 0.36));
        }
        const x = ca * sq * rx * rg.sc * shp, z = sa * sq * rx * rg.sc * shp;
        pos.push(x, rg.y, z); nrm.push(ca * (1 - rg.ny * 0.6), rg.ny, sa * (1 - rg.ny * 0.6));
        uv.push(x + 0.5, z + 0.5); col.push(rg.c[0], rg.c[1], rg.c[2]);
      }
    }
    const cIdx = pos.length / 3;
    pos.push(0, hTop, 0); nrm.push(0, 1, 0); uv.push(0.5, 0.5); col.push(0.08, 0.07, 0.062);
    for (let r = 0; r < rings.length - 1; r++) for (let s = 0; s < segs; s++) {
      const ns = (s + 1) % segs, a = r * segs + s, b = r * segs + ns, c = (r + 1) * segs + s, d = (r + 1) * segs + ns;
      idx.push(a, d, b, a, c, d);
    }
    const topR = (rings.length - 1) * segs;
    for (let s = 0; s < segs; s++) idx.push(topR + s, cIdx, topR + ((s + 1) % segs));
    return makeProceduralModel(gl, pos, nrm, uv, col, idx, {
      name: `play_bridge_${stage}`, baseColor: [1, 1, 1, 1], metallic: 0, roughness: 0.72,
      normalScale: 0, normalTex: null, baseTex: null, mrTex: null,
      emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: true,
    }, 0.62);
  }

  function createFxAssets(gl) {
    const fogTile = makeProceduralModel(gl,
      [-0.55, 0.06, -0.55, 0.55, 0.06, -0.55, 0.55, 0.06, 0.55, -0.55, 0.06, 0.55],
      [0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0], [0, 0, 1, 0, 1, 1, 0, 1],
      [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1], [0, 2, 1, 0, 3, 2],
      { name: "play_fog", baseColor: [0.02, 0.03, 0.03, 0.68], metallic: 0, roughness: 1, normalScale: 0, normalTex: null, baseTex: null, mrTex: null, emissive: [0, 0, 0], alphaMode: "BLEND", isTranslucent: true, doubleSided: true },
      0.6);
    return {
      selRings: [
        buildRing(gl, [0.02, 0.92, 0.14], 0.94, 0.44, 0.56, true),
        buildRing(gl, [1.05, 0.04, 0.06], 0.94, 0.44, 0.56, true),
        buildRing(gl, [0.04, 0.34, 1.15], 0.94, 0.44, 0.56, true),
        buildRing(gl, [0.46, 0.52, 0.64], 0.94, 0.44, 0.56, true),
      ],
      hoverRing: buildRing(gl, [0.85, 0.82, 0.45], 0.55, 0.42, 0.50, false),
      enemyHoverRing: buildRing(gl, [1.15, 0.05, 0.05], 0.82, 0.42, 0.52, false),
      xMarkers: { move: buildXMarker(gl, [0.02, 0.98, 0.18]), attack: buildXMarker(gl, [1.18, 0.05, 0.05]), special: buildXMarker(gl, [1.22, 0.68, 0.02]) },
      orderRings: { move: buildRing(gl, [0.02, 0.92, 0.16], 0.82, 0.38, 0.47, false), attack: buildRing(gl, [1.15, 0.05, 0.05], 0.82, 0.38, 0.47, false), special: buildRing(gl, [1.18, 0.64, 0.02], 0.82, 0.38, 0.47, false) },
      puRings: Object.fromEntries(Object.entries(POWERUP_RING_COLORS).map(([k, c]) => [k, buildRing(gl, c, 0.80, 0.36, 0.47, false)])),
      puRingDefault: buildRing(gl, [1.35, 0.95, 0.22], 0.80, 0.36, 0.47, false),
      bridges: [1, 2, 3, 4].map((st) => buildMudBridge(gl, st)),
      fogTile,
      blastFlash: buildSphereFx(gl, [1.0, 0.88, 0.35], [2.8, 1.9, 0.55], 1.0, 0.24, true),
      blastRing: buildRing(gl, [2.4, 1.1, 0.18], 0.88, 0.58, 0.96, false),
      smokePuff: buildSphereFx(gl, [0.64, 0.58, 0.52], [0.05, 0.04, 0.03], 0.88, 0.94, false),
      dudSmoke: buildSphereFx(gl, [0.30, 0.28, 0.26], [0.02, 0.02, 0.02], 0.84, 0.96, false),
      dustPuff: buildSphereFx(gl, [0.88, 0.78, 0.62], [0.16, 0.12, 0.08], 0.86, 0.92, false),
      splashDrop: buildSphereFx(gl, [0.42, 0.88, 1.0], [0.22, 0.62, 0.95], 0.84, 0.18, false),
      powHalo: buildRing(gl, [1.8, 1.45, 0.32], 0.90, 0.26, 0.52, false),
      extExplosion: (window.__demoExplosion && typeof window.__demoExplosion.buildBombExplosionModels === "function")
        ? window.__demoExplosion.buildBombExplosionModels(gl) : null,
      extRigs: [],
    };
  }

  function pushExplosionFx(fxAssets, wx, wz, ageMs, isDud, visible, dynGlows, slotIdx = 0) {
    const { clamp, mat4FromTRS } = window.__antGltf;
    if (fxAssets.extExplosion && window.__demoExplosion && typeof window.__demoExplosion.updateBombExplosion === "function") {
      while (fxAssets.extRigs.length <= slotIdx) {
        fxAssets.extRigs.push(window.__demoExplosion.createBombExplosionRig(fxAssets.extExplosion));
      }
      window.__demoExplosion.updateBombExplosion(fxAssets.extRigs[slotIdx], wx, wz, ageMs / 1000.0, isDud, visible, dynGlows);
      return;
    }
    const u = clamp(ageMs / 680.0, 0, 1);
    if (!isDud && u < 0.45) {
      const fSc = 0.28 + Math.sin((u / 0.45) * Math.PI) * 0.92;
      visible.push({ kind: "item", id: "fx_flash", isGrass: true, model: fxAssets.blastFlash, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([wx, 0.42, wz], [0, 0, 0, 1], [fSc, fSc, fSc]) });
    }
    if (!isDud && u > 0.08 && u < 0.72) {
      const rSc = 0.45 + ((u - 0.08) / 0.64) * 1.95;
      visible.push({ kind: "item", id: "fx_ring", isGrass: true, model: fxAssets.blastRing, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([wx, 0.14, wz], [0, 0, 0, 1], [rSc, 1, rSc]) });
      dynGlows.push([wx, wz, 4.2, (1 - u) * 2.1, 2.2, 0.95, 0.22]);
    }
    const pm = isDud ? fxAssets.dudSmoke : fxAssets.smokePuff, n = isDud ? 3 : 6;
    for (let k = 0; k < n; k++) {
      const ang = (k / n) * Math.PI * 2 + u * 0.7, rad = (isDud ? 0.18 : 0.35) + u * (isDud ? 0.45 : 1.35);
      const py = 0.22 + u * (isDud ? 0.85 : 1.05) + (k % 2) * 0.18, sc = Math.max(0.06, (1 - u * 0.75) * (isDud ? 0.36 : 0.48));
      visible.push({ kind: "item", id: "fx_smoke", isGrass: true, model: pm, clipName: "Idle", idleTime: 0, offsetMat: mat4FromTRS([wx + Math.cos(ang) * rad, py, wz + Math.sin(ang) * rad], [0, 0, 0, 1], [sc, sc, sc]) });
    }
  }

  // --- Built-in Playable 20 Hz Offline Mock Harness (?mock=1) ---
  function createMockMatch(mapObj, allMaps, cfg, onInit, onTick) {
    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
    const seat = Number.isInteger(cfg.seat) ? cfg.seat : 0;
    const cols = mapObj.cols, rows = mapObj.rows, tiles = mapObj.tiles;
    const isWater = (c, r) => c >= 0 && r >= 0 && c < cols && r < rows && tiles[r * cols + c] >= 45 && tiles[r * cols + c] <= 53;

    const staticDecor = [], plants = [], hills = [], food = [], powerups = [], bombs = [], fires = [], bridges = [], lunchboxes = [], droppers = [];
    for (const o of mapObj.objects) {
      const low = o.id.toLowerCase();
      if (low.endsWith("hill")) hills.push({ team: Math.max(0, HILL_IDS.indexOf(o.id)), id: o.id, col: o.col, row: o.row });
      else if (low.startsWith("fd") && !low.startsWith("fd_")) food.push([o.col, o.row, o.id, 25]);
      else if (low.startsWith("pu_")) powerups.push([o.col, o.row, o.id]);
      else if (low.endsWith("bomb")) bombs.push({ c: o.col, r: o.row, team: Math.max(0, BOMB_IDS.indexOf(low)), ttl: 9999 });
      else if (low === "wallup04") fires.push({ c: o.col, r: o.row, ttl: 9999 });
      else if (o.canopy || low.startsWith("flower") || low.startsWith("clover")) plants.push({ id: o.id, col: o.col, row: o.row });
      else staticDecor.push({ id: o.id, col: o.col, row: o.row });
    }

    let nextId = 1;
    const ants = [];
    const spawn = (team, clsIdx, c, r, opts = {}) => ants.push({
      id: nextId++, team, clsIdx, px: c * 32 + 16, py: r * 32 + 16, facing: opts.facing ?? 4,
      hp: opts.hp ?? 10, maxHp: 10, state: opts.state ?? 0, action: opts.action ?? 0,
      holding: opts.holding ? 1 : 0, carriedPts: opts.holding ? 25 : 0,
      swimming: opts.swimming ? 1 : 0, drowning: 0, stunned: 0, frozen: 0, burnMs: 0,
      goalX: null, goalZ: null, attackId: null, special: null, patrol: opts.patrol || null, patrolIdx: 0, animTimer: 0,
    });

    // Wire cls_idx spec (#556): 0=worker, 1=combat, 2=bomber, 3=swimmer, 4=fire, 5=thief
    const gHill = hills.find((h) => h.team === seat) || { col: 36, row: 20 };
    const cx = clamp(gHill.col - 3, 5, cols - 6), cz = clamp(gHill.row + 4, 5, rows - 6);
    spawn(seat, 2, cx, cz, { facing: 3 });                        // Bomber
    spawn(seat, 1, cx + 2, cz - 1, { facing: 5, hp: 8 });         // Combat
    spawn(seat, 0, cx - 2, cz + 1, { facing: 2, holding: true }); // Worker with lunchbox
    spawn(seat, 4, cx + 1, cz + 2, { facing: 4 });                // Fire Ant
    spawn(seat, 5, cx - 1, cz - 2, { facing: 3 });                // Thief
    if (mapObj.ponds && mapObj.ponds.length) {
      const pc = Math.floor(mapObj.ponds[0][0]), pr = Math.floor(mapObj.ponds[0][1]);
      spawn(seat, 3, pc, pr, { facing: 2, swimming: 1, state: 15, patrol: [[pc, pr], [clamp(pc + 2, 1, cols - 2), pr]] });
      bridges.push([clamp(pc - 1, 1, cols - 2), pr, 4], [pc, pr, 3]);
    } else spawn(seat, 3, cx + 3, cz + 1, { facing: 6 });

    for (const h of hills) {
      if (h.team === seat) continue;
      const hx = clamp(h.col + (cols * 0.5 > h.col ? 3 : -3), 4, cols - 5), hz = clamp(h.row + (rows * 0.5 > h.row ? 3 : -3), 4, rows - 5);
      spawn(h.team, 0, hx, hz, { facing: 3, holding: true, patrol: [[hx, hz], [clamp(hx + 3, 3, cols - 4), clamp(hz + 2, 3, rows - 4)]] });
      spawn(h.team, 1, hx + 1, hz + 1, { facing: 5, patrol: [[hx + 1, hz + 1], [cx + 4, cz + 3]] });
      spawn(h.team, 2, hx - 1, hz + 2, { facing: 2 });
    }
    spawn((seat + 1) % 4, 1, clamp(cx + 5, 3, cols - 4), clamp(cz + 2, 3, rows - 4), { facing: 6, hp: 7, patrol: [[clamp(cx + 5, 3, cols - 4), clamp(cz + 2, 3, rows - 4)], [clamp(cx + 5, 3, cols - 4), clamp(cz - 1, 3, rows - 4)]] });
    spawn((seat + 2) % 4, 0, clamp(cx - 4, 3, cols - 4), clamp(cz + 3, 3, rows - 4), { facing: 2, holding: true, patrol: [[clamp(cx - 4, 3, cols - 4), clamp(cz + 3, 3, rows - 4)], [clamp(cx - 1, 3, cols - 4), clamp(cz + 4, 3, rows - 4)]] });
    bombs.push({ c: clamp(cx - 2, 3, cols - 4), r: clamp(cz - 3, 3, rows - 4), team: seat, ttl: 260 });
    fires.push({ c: clamp(cx + 3, 3, cols - 4), r: clamp(cz + 2, 3, rows - 4), ttl: 360 });
    if (food.length < 4) food.push([cx - 3, cz - 1, "fdpizz1", 25], [cx + 2, cz + 4, "fdpretz1", 25]);
    if (powerups.length < 2) powerups.push([cx + 1, cz - 3, "pu_bomb"], [cx - 3, cz + 2, "pu_mason"]);

    const st = {
      tick: 0, remMs: 600000, paused: false, over: false, winner: -1, fog: Boolean(cfg.fog),
      scores: [2450, 1820, 1340, 980], eggs: [5, 5, 5, 5], hatching: [0, 0, 0, 0],
      ants, food, powerups, bombs, fires, bridges, lunchboxes, droppers,
      fx: [], bubbles: [[gHill.col * 32 + 24, gHill.row * 32 + 16, 25, seat, 100]], news: [], sfx: [], timer: null,
      focus: [cx + 0.8, cz + 0.4], initialSelect: [ants[0].id, ants[1].id],
    };

    const emitTick = () => {
      let fogHex = "";
      if (st.fog) {
        const rev = new Uint8Array(cols * rows);
        const disk = (x0, z0, rad) => {
          for (let r = Math.max(0, Math.floor(z0 - rad)); r <= Math.min(rows - 1, Math.ceil(z0 + rad)); r++)
            for (let c = Math.max(0, Math.floor(x0 - rad)); c <= Math.min(cols - 1, Math.ceil(x0 + rad)); c++)
              if ((c - x0) * (c - x0) + (r - z0) * (r - z0) <= rad * rad) rev[r * cols + c] = 1;
        };
        disk(gHill.col + 1, gHill.row + 1, 7.5);
        for (const a of st.ants) if (a.team === seat) disk(a.px / 32, a.py / 32, 6.2);
        const hc = [];
        for (let i = 0; i < rev.length; i += 4) hc.push(((rev[i] || 0) | ((rev[i + 1] || 0) << 1) | ((rev[i + 2] || 0) << 2) | ((rev[i + 3] || 0) << 3)).toString(16));
        fogHex = hc.join("");
      }
      onTick({
        type: "tick", tick: st.tick, rem_ms: st.remMs, over: st.over, winner: st.winner, paused: st.paused,
        scores: st.scores.slice(), eggs: st.eggs.slice(), hatching: st.hatching.slice(),
        ants: st.ants.map((a) => [a.id, a.team, a.clsIdx, Math.round(a.px), Math.round(a.py), a.facing, a.hp, a.maxHp, a.state, a.action, 0, 0, a.holding, a.carriedPts, a.swimming, a.drowning, a.stunned, a.frozen, a.burnMs]),
        food: st.food, bombs: st.bombs.map((b) => [b.c, b.r, b.team]), fires: st.fires.map((f) => [f.c, f.r]),
        bridges: st.bridges, powerups: st.powerups, lunchboxes: st.lunchboxes, droppers: st.droppers,
        fx: st.fx.map((f) => [f.kind, f.px, f.py, f.ageMs, f.team]), bubbles: st.bubbles.map((b) => b.slice()),
        sfx: st.sfx.splice(0), news: st.news.splice(0), fog_hex: fogHex,
      });
    };

    const step = () => {
      if (!st.paused && !st.over) {
        st.tick++; st.remMs = Math.max(0, st.remMs - 50);
        if (st.remMs === 0) { st.over = true; st.winner = 0; }
        if (st.tick % 45 === 15) { st.scores[seat] += 25; st.bubbles.push([gHill.col * 32 + 24, gHill.row * 32 + 20, 25, seat, 0]); }
        for (let t = 0; t < 4; t++) if (st.hatching[t] > 0 && --st.hatching[t] === 0) {
          const h = hills.find((x) => x.team === t) || gHill;
          spawn(t, 0, clamp(h.col, 2, cols - 3), clamp(h.row + 2, 2, rows - 3), { facing: 4, state: 8, action: 0x14 });
          st.sfx.push([43, h.col * 32, h.row * 32]); st.news.push(`${SEAT_LABELS[t]} hatched a Worker Ant`);
        }
        for (let i = st.bombs.length - 1; i >= 0; i--) {
          if (st.bombs[i].ttl < 9000 && --st.bombs[i].ttl <= 0) {
            const b = st.bombs.splice(i, 1)[0];
            st.fx.push({ kind: "bombex", px: b.c * 32 + 16, py: b.r * 32 + 16, ageMs: 0, maxMs: 700, team: b.team });
            st.sfx.push([4, b.c * 32 + 16, b.r * 32 + 16]); st.news.push(`${SEAT_LABELS[b.team]} bomb detonated!`);
          }
        }
        for (let i = st.fires.length - 1; i >= 0; i--) if (st.fires[i].ttl < 9000 && --st.fires[i].ttl <= 0) {
          const f = st.fires.splice(i, 1)[0];
          st.fx.push({ kind: "sputter", px: f.c * 32 + 16, py: f.r * 32 + 16, ageMs: 0, maxMs: 820, team: 0 });
        }
        for (let i = st.fx.length - 1; i >= 0; i--) { st.fx[i].ageMs += 50; if (st.fx[i].ageMs >= st.fx[i].maxMs) st.fx.splice(i, 1); }
        for (let i = st.bubbles.length - 1; i >= 0; i--) { st.bubbles[i][4] += 50; if (st.bubbles[i][4] >= 1400) st.bubbles.splice(i, 1); }
        for (const a of st.ants) {
          if (a.animTimer > 0) { if (--a.animTimer === 0) { a.state = 0; a.action = 0; } continue; }
          if (a.attackId != null) {
            const tgt = st.ants.find((x) => x.id === a.attackId);
            if (!tgt) a.attackId = null;
            else if (Math.hypot(tgt.px - a.px, tgt.py - a.py) < 44) {
              a.state = 2; a.action = 0x12; a.animTimer = 10;
              a.facing = Math.round(((Math.atan2(tgt.px - a.px, -(tgt.py - a.py)) / (Math.PI / 4)) + 8) % 8);
              tgt.hp = Math.max(1, tgt.hp - (a.clsIdx === 1 ? 2 : 1)); tgt.state = a.clsIdx === 1 ? 5 : 4; tgt.animTimer = 8;
              st.fx.push({ kind: "battle", px: (a.px + tgt.px) * 0.5, py: (a.py + tgt.py) * 0.5, ageMs: 0, maxMs: 650, team: a.team });
              st.sfx.push([a.clsIdx === 1 ? 78 : 57, a.px, a.py]); continue;
            } else { a.goalX = tgt.px / 32; a.goalZ = tgt.py / 32; }
          }
          if (a.goalX === null && a.patrol && a.patrol.length >= 2) {
            const wp = a.patrol[a.patrolIdx % a.patrol.length]; a.goalX = wp[0] + 0.5; a.goalZ = wp[1] + 0.5;
          }
          if (a.goalX !== null) {
            const gx = a.goalX * 32, gz = a.goalZ * 32, dx = gx - a.px, dz = gz - a.py, d = Math.hypot(dx, dz);
            const spd = a.clsIdx === 5 ? 4.8 : (a.swimming ? 3.2 : 4.0);
            if (d <= spd + 2) {
              a.px = gx; a.py = gz; a.goalX = null; a.goalZ = null;
              if (a.patrol) a.patrolIdx = (a.patrolIdx + 1) % a.patrol.length;
              if (a.special) {
                const { tx, ty } = a.special; a.special = null;
                if (a.clsIdx === 2) { a.state = 21; a.action = 8; a.animTimer = 14; st.bombs.push({ c: tx, r: ty, team: a.team, ttl: 40 }); st.news.push(`${SEAT_LABELS[a.team]} Bomber planted a bomb!`); }
                else if (a.clsIdx === 4) { a.state = 23; a.action = 6; a.animTimer = 16; st.fires.push({ c: tx, r: ty, ttl: 260 }); st.sfx.push([68, tx * 32, ty * 32]); }
                else if (a.clsIdx === 3) { a.state = 19; a.action = 0x10; a.animTimer = 14; st.bridges.push([tx, ty, 4]); st.sfx.push([81, tx * 32, ty * 32]); }
                else if (a.clsIdx === 5) { a.state = 16; a.action = 0x0D; a.animTimer = 14; st.scores[a.team] += 50; st.bubbles.push([a.px, a.py, 50, a.team, 0]); }
              } else { a.state = a.swimming ? 15 : 0; a.action = 0; }
            } else {
              a.px += (dx / d) * spd; a.py += (dz / d) * spd;
              a.facing = Math.round(((Math.atan2(dx, -dz) / (Math.PI / 4)) + 8) % 8);
              a.swimming = (a.clsIdx === 3 && isWater(Math.floor(a.px / 32), Math.floor(a.py / 32))) ? 1 : 0;
              a.state = a.swimming ? 15 : 1; a.action = 1;
            }
          }
        }
      }
      emitTick();
    };

    const initMsg = {
      type: "init", maps: allMaps.map((m) => ({ id: m.id, label: m.label, file: m.file })),
      map: { id: mapObj.id, label: mapObj.label, file: mapObj.file, w: cols, h: rows, tiles, static_decor: staticDecor, plants, hills, seat, bots: cfg.bots || "medium", fog: st.fog, paused: false },
    };

    return {
      initMsg,
      focus: st.focus,
      initialSelect: st.initialSelect,
      start() { emitTick(); st.timer = setInterval(step, 50); },
      stop() { if (st.timer) { clearInterval(st.timer); st.timer = null; } },
      pushExplosion(x, z) { st.fx.push({ kind: "bombex", px: x * 32, py: z * 32, ageMs: 140, maxMs: 700, team: seat }); emitTick(); },
      handleCommand(cmd, onRestart) {
        if (!cmd || !cmd.type) return;
        if (cmd.type === "pause") { st.paused = Boolean(cmd.paused); return; }
        if (cmd.type === "new_match") { onRestart(cmd); return; }
        if (cmd.type === "hatch" && st.eggs[seat] > 0 && st.hatching[seat] <= 0) {
          st.eggs[seat]--; st.hatching[seat] = 16;
          st.news.push(`${SEAT_LABELS[seat]} is hatching a new Worker Ant!`);
          st.sfx.push([48, gHill.col * 32, gHill.row * 32]); return;
        }
        const ids = new Set(cmd.ants || []);
        for (const a of st.ants) {
          if (!ids.has(a.id) || a.team !== seat) continue;
          if (cmd.type === "stop") { a.goalX = null; a.goalZ = null; a.attackId = null; a.special = null; a.state = 0; a.action = 0; }
          else if (cmd.type === "move") { a.goalX = clamp(cmd.tx, 1, cols - 2) + 0.5; a.goalZ = clamp(cmd.ty, 1, rows - 2) + 0.5; a.attackId = null; a.special = null; }
          else if (cmd.type === "attack") { a.attackId = cmd.target_id; a.special = null; }
          else if (cmd.type === "special") { a.goalX = clamp(cmd.tx, 1, cols - 2) + 0.5; a.goalZ = clamp(cmd.ty, 1, rows - 2) + 0.5; a.attackId = null; a.special = { tx: clamp(cmd.tx, 1, cols - 2), ty: clamp(cmd.ty, 1, rows - 2) }; }
        }
      },
    };
  }

  // --- HUD Controller ---
  function formatTimer(ms) {
    const sec = Math.max(0, Math.ceil((ms || 0) / 1000));
    return `${String(Math.floor(sec / 60)).padStart(2, "0")}:${String(sec % 60).padStart(2, "0")}`;
  }

  function createHud(cb) {
    const scoresEl = $("hud-scores"), timerEl = $("hud-timer"), connBtn = $("hud-conn"), connLabel = $("hud-conn-label");
    const mapLabelEl = $("hud-map-label"), newsStack = $("hud-news-stack"), pauseBanner = $("hud-pause-banner");
    const colonyStrip = $("hud-colony-strip"), hpLayer = $("play-hp-layer"), bubblesLayer = $("play-bubbles-layer");
    const marqueeEl = $("play-marquee"), shellEl = $("play-shell");
    const minimap = createMinimap($("hud-minimap"), (wx, wz) => cb.onMinimapJump(wx, wz));
    const sound = createSoundPlayer();
    let victoryShown = false, lastScoreSig = "", lastColonySig = "";
    const seenNews = new Set();

    const scorePills = [];
    for (let t = 0; t < 4; t++) {
      const pill = document.createElement("div"); pill.className = "team-score-pill"; pill.dataset.team = String(t);
      const dot = document.createElement("span"); dot.className = "team-emblem";
      const val = document.createElement("span"); val.className = "team-score-val"; val.textContent = "0";
      const egg = document.createElement("span"); egg.className = "team-egg-chip"; egg.title = `${SEAT_LABELS[t]} eggs remaining`; egg.textContent = "5";
      pill.append(dot, val, egg); scoresEl.appendChild(pill);
      scorePills.push({ pill, val, egg });
    }

    const colonyChips = {};
    for (const cls of CLASS_ORDER) {
      const btn = document.createElement("button"); btn.type = "button"; btn.className = "colony-chip zero"; btn.title = `Select all ${CLASS_META[cls].name}s`;
      const lbl = document.createElement("span"); lbl.className = "colony-chip-lbl"; lbl.textContent = CLASS_META[cls].short;
      const num = document.createElement("span"); num.className = "colony-chip-num"; num.textContent = "0";
      btn.append(lbl, num); btn.addEventListener("click", () => cb.onSelectClass(cls));
      colonyStrip.appendChild(btn); colonyChips[cls] = { btn, num };
    }

    function syncAudioBtn() {
      const m = sound.getMode();
      const b = $("btn-mute");
      b.dataset.audio = m;
      b.setAttribute("aria-pressed", String(m === "muted"));
      $("lbl-audio-mode").textContent = AUDIO_LABELS[m] || "Remastered";
    }
    syncAudioBtn();

    $("btn-new-match").addEventListener("click", () => openMatchModal(true));
    $("btn-close-match").addEventListener("click", () => openMatchModal(false));
    $("modal-match").addEventListener("click", (e) => { if (e.target === $("modal-match")) openMatchModal(false); });
    connBtn.addEventListener("click", () => openMatchModal(true));
    $("btn-cam-cycle").addEventListener("click", () => cb.onCycleCamera());
    $("btn-light-cycle").addEventListener("click", () => cb.onCycleLighting());
    $("btn-fog-toggle").addEventListener("click", () => cb.onToggleFog());
    $("btn-pause").addEventListener("click", () => cb.onTogglePause());
    $("btn-resume-inline").addEventListener("click", () => cb.onTogglePause());
    $("btn-mute").addEventListener("click", () => {
      const next = AUDIO_MODES[(AUDIO_MODES.indexOf(sound.getMode()) + 1) % AUDIO_MODES.length];
      sound.setMode(next);
      syncAudioBtn();
      if (next !== "muted") { sound.unlock(); sound.play(0, NaN, NaN, null); }
      pushNews(`Audio: ${AUDIO_LABELS[next]}`);
    });
    $("cmd-hatch").addEventListener("click", () => cb.onHatch());
    $("cmd-ability").addEventListener("click", () => cb.onToggleAbility());
    $("cmd-stop").addEventListener("click", () => cb.onStop());
    $("cmd-select-all").addEventListener("click", () => cb.onSelectAll());
    $("cmd-box-select").addEventListener("click", () => cb.onToggleBoxSelect());

    const readModalCfg = () => ({
      map: $("sel-map").value || "TREASURE.LVL", bots: $("sel-bots").value || "medium",
      seat: Number($("sel-seat").value) || 0, fog: Boolean($("chk-fog").checked),
      seed: Math.max(1, Number($("inp-seed").value) || 1),
    });
    $("btn-launch-match").addEventListener("click", () => { openMatchModal(false); $("modal-victory").hidden = true; victoryShown = false; cb.onNewMatch(readModalCfg()); });
    $("btn-start-mock").addEventListener("click", () => { openMatchModal(false); $("modal-victory").hidden = true; victoryShown = false; cb.onStartMock(readModalCfg()); });
    $("btn-play-again").addEventListener("click", () => {
      $("modal-victory").hidden = true; victoryShown = false;
      $("inp-seed").value = String((Number($("inp-seed").value) || 1) + 1);
      cb.onNewMatch(readModalCfg());
    });
    $("btn-victory-maps").addEventListener("click", () => { $("modal-victory").hidden = true; openMatchModal(true); });
    $("btn-retry-ws").addEventListener("click", () => cb.onRetryWs());
    $("btn-fallback-mock").addEventListener("click", () => cb.onStartMock({}));

    function openMatchModal(show) { $("modal-match").hidden = !show; }

    function pushNews(text) {
      if (!text || typeof text !== "string" || !text.trim()) return;
      const el = document.createElement("div"); el.className = "news-toast"; el.textContent = text.trim();
      newsStack.appendChild(el);
      while (newsStack.children.length > 3) newsStack.removeChild(newsStack.firstChild);
      setTimeout(() => el.classList.add("fading"), 4200);
      setTimeout(() => { if (el.parentNode) el.parentNode.removeChild(el); }, 4650);
    }

    function setMapsList(maps, curMap) {
      const sel = $("sel-map"); sel.textContent = "";
      const list = (maps && maps.length) ? maps : [
        { id: "treasure", label: "Treasure Map", file: "TREASURE.LVL" }, { id: "islands", label: "Islands", file: "ISLANDS.LVL" },
        { id: "gauntlet", label: "Gauntlet", file: "GAUNTLET.LVL" }, { id: "medium", label: "Medium", file: "MEDIUM.LVL" },
        { id: "small", label: "Small Map", file: "SMALL.LVL" }, { id: "tiny", label: "Tiny Arena", file: "TINY.LVL" },
      ];
      for (const m of list) {
        const opt = document.createElement("option");
        opt.value = m.file || m.id; opt.textContent = `${m.label || m.file} (${m.file || m.id})`;
        sel.appendChild(opt);
      }
      if (curMap) {
        if (curMap.file) sel.value = curMap.file;
        if (curMap.bots) $("sel-bots").value = curMap.bots;
        if (Number.isFinite(curMap.seat)) $("sel-seat").value = String(curMap.seat);
        $("chk-fog").checked = Boolean(curMap.fog);
      }
    }

    function setConnection(st, label) {
      connBtn.dataset.state = st; connLabel.textContent = label;
      const h = $("modal-ws-hint");
      if (h) h.textContent = st === "live" ? "Connected via binary WebSocket to live C++ 20 Hz engine."
        : (st === "mock" ? "Running local 20 Hz offline mock match (connect bridge on port 4081 for C++ AI)." : "Waiting for C++ 3D bridge WebSocket (or click Practice Offline).");
    }

    function updateTick(tk, seat, activeSeats) {
      if (!tk) return;
      const scores = tk.scores || [0, 0, 0, 0], eggs = tk.eggs || [0, 0, 0, 0], hatching = tk.hatching || [0, 0, 0, 0];
      const sig = `${scores.join(",")}|${eggs.join(",")}|${hatching.join(",")}|${seat}|${activeSeats}`;
      if (sig !== lastScoreSig) {
        lastScoreSig = sig;
        for (let t = 0; t < 4; t++) {
          const p = scorePills[t];
          p.val.textContent = Number(scores[t] || 0).toLocaleString();
          p.egg.textContent = String(eggs[t] || 0);
          p.egg.classList.toggle("hatching", Boolean(hatching[t]));
          p.pill.classList.toggle("is-self", t === seat);
          p.pill.classList.toggle("is-absent", activeSeats > 0 && ((activeSeats >> t) & 1) === 0);
        }
        $("cmd-eggs-badge").textContent = String(eggs[seat] || 0);
        $("cmd-hatch").disabled = (eggs[seat] || 0) <= 0;
      }
      const rem = tk.rem_ms ?? 600000;
      timerEl.textContent = formatTimer(rem);
      timerEl.classList.toggle("warn", rem <= 60000 && rem > 30000);
      timerEl.classList.toggle("urgent", rem <= 30000 && rem > 0);
      setPaused(Boolean(tk.paused));
      for (const line of (tk.news || [])) {
        const key = `${tk.tick}:${line}`;
        if (!seenNews.has(key)) { seenNews.add(key); if (seenNews.size > 60) seenNews.clear(); pushNews(line); }
      }
      if (tk.over && !victoryShown) {
        victoryShown = true;
        const winSeat = Number.isInteger(tk.winner) ? tk.winner : -1;
        $("victory-title").textContent = winSeat === seat ? "Victory!" : (winSeat >= 0 ? `${SEAT_LABELS[winSeat]} Wins!` : "Match Over");
        $("victory-sub").textContent = winSeat === seat ? `${SEAT_LABELS[seat]} Colony conquered the backyard!` : "Final standings across all colonies.";
        const tbl = $("victory-table"); tbl.textContent = "";
        [0, 1, 2, 3].filter((t) => activeSeats === 0 || ((activeSeats >> t) & 1) !== 0)
          .sort((a, b) => (scores[b] || 0) - (scores[a] || 0)).forEach((t, idx) => {
            const row = document.createElement("div"); row.className = "victory-row" + (t === winSeat || (winSeat < 0 && idx === 0) ? " winner" : "");
            const l = document.createElement("span"); l.textContent = `#${idx + 1} ${SEAT_LABELS[t]}${t === seat ? " (You)" : ""}`; l.style.color = SEAT_HEX[t];
            const r = document.createElement("span"); r.textContent = `${Number(scores[t] || 0).toLocaleString()} pts`;
            row.append(l, r); tbl.appendChild(row);
          });
        $("modal-victory").hidden = false;
      }
    }

    function updateSelection(sel, own, abilityMode, boxMode) {
      const n = sel.length, hpFill = $("hud-sel-hp-fill"), abBtn = $("cmd-ability");
      if (n === 0) {
        $("hud-sel-avatar").textContent = String(own.length);
        $("hud-sel-name").textContent = "Colony Ready";
        $("hud-sel-sub").textContent = `${own.length} ants active · Click or drag to command`;
        $("hud-sel-loot").hidden = true; hpFill.style.width = "100%";
        abBtn.disabled = true; $("cmd-ability-lbl").textContent = "Ability"; $("cmd-stop").disabled = true;
      } else {
        const first = sel[0], meta = CLASS_META[first.cls] || CLASS_META.worker;
        const hpSum = sel.reduce((s, a) => s + a.hp, 0), maxSum = Math.max(1, sel.reduce((s, a) => s + a.maxHp, 0));
        const pct = Math.max(0, Math.min(100, Math.round((hpSum / maxSum) * 100)));
        const loot = sel.reduce((s, a) => s + (a.holding ? (a.carriedPts || 25) : 0), 0);
        const spec = sel.find((a) => (CLASS_META[a.cls] || {}).canSpecial);
        $("hud-sel-avatar").textContent = n === 1 ? meta.badge : String(n);
        $("hud-sel-name").textContent = n === 1 ? meta.name : `${n} Ants Selected`;
        $("hud-sel-sub").textContent = n === 1 ? `HP ${first.hp}/${first.maxHp} · ${first.clipName || "Idle"}` : `Avg HP ${pct}% · Right-click to order`;
        $("hud-sel-loot").hidden = loot <= 0; if (loot > 0) $("hud-sel-loot").textContent = `+${loot}`;
        hpFill.style.width = `${pct}%`; abBtn.disabled = !spec;
        $("cmd-ability-lbl").textContent = spec ? CLASS_META[spec.cls].ability : "Ability";
        $("cmd-stop").disabled = false;
      }
      abBtn.setAttribute("aria-pressed", String(Boolean(abilityMode)));
      $("cmd-box-select").setAttribute("aria-pressed", String(Boolean(boxMode)));
      shellEl.classList.toggle("mode-ability", Boolean(abilityMode));

      const cnt = { worker: 0, combat: 0, bomber: 0, swimmer: 0, fire: 0, thief: 0 };
      for (const a of own) if (cnt[a.cls] !== undefined) cnt[a.cls]++;
      const cSig = CLASS_ORDER.map((c) => cnt[c]).join(",");
      if (cSig !== lastColonySig) {
        lastColonySig = cSig;
        for (const c of CLASS_ORDER) { colonyChips[c].num.textContent = String(cnt[c]); colonyChips[c].btn.classList.toggle("zero", cnt[c] === 0); }
      }
    }

    function updateFloatingOverlays(ants, bubbles, selectedIds, worldToScreen) {
      hpLayer.textContent = "";
      for (const a of ants) {
        if (!selectedIds.has(a.id) && a.hp >= a.maxHp) continue;
        const p = worldToScreen(a.rx, (a.yLift || 0) + 1.18, a.rz);
        if (!p || p[0] < -30 || p[1] < -30 || p[0] > window.innerWidth + 30 || p[1] > window.innerHeight + 30) continue;
        const bar = document.createElement("div"); bar.className = "hp-micro";
        bar.style.left = `${p[0].toFixed(1)}px`; bar.style.top = `${p[1].toFixed(1)}px`;
        const fill = document.createElement("div"), ratio = Math.max(0, Math.min(1, a.hp / Math.max(1, a.maxHp)));
        fill.className = "hp-micro-fill" + (ratio < 0.35 ? " low" : (ratio < 0.65 ? " mid" : ""));
        fill.style.width = `${Math.round(ratio * 100)}%`; bar.appendChild(fill); hpLayer.appendChild(bar);
      }
      bubblesLayer.textContent = "";
      for (const b of bubbles) {
        const u = Math.max(0, Math.min(1, (b.ageMs || 0) / 1400)), p = worldToScreen(b.wx, 0.95 + u * 1.4, b.wz);
        if (!p) continue;
        const el = document.createElement("div"); el.className = `score-bubble ${b.pts >= 0 ? "gain" : "loss"}`;
        el.textContent = `${b.pts >= 0 ? "+" : ""}${b.pts}`;
        el.style.left = `${p[0].toFixed(1)}px`; el.style.top = `${p[1].toFixed(1)}px`; el.style.opacity = String(Math.max(0, 1 - u * u));
        bubblesLayer.appendChild(el);
      }
    }

    function setMarquee(rect) {
      marqueeEl.hidden = !rect;
      if (rect) { marqueeEl.style.left = `${rect.x}px`; marqueeEl.style.top = `${rect.y}px`; marqueeEl.style.width = `${rect.w}px`; marqueeEl.style.height = `${rect.h}px`; }
    }
    function setPaused(p) { pauseBanner.hidden = !p; $("btn-pause").setAttribute("aria-pressed", String(Boolean(p))); }
    function setFog(on) { $("btn-fog-toggle").setAttribute("aria-pressed", String(Boolean(on))); }
    function setMapTerrain(cols, rows, tiles, hills, label) {
      minimap.buildTerrain(cols, rows, tiles, hills);
      if (label) mapLabelEl.textContent = label;
      victoryShown = false; $("modal-victory").hidden = true;
    }
    function showLoading(show, title, msg, actions = false) {
      $("play-loading").hidden = !show;
      if (title) $("play-loading-title").textContent = title;
      if (msg) $("play-loading-msg").textContent = msg;
      $("play-loading-actions").hidden = !actions;
    }

    return {
      sound, setConnection, setMapsList, setMapTerrain, updateTick, updateSelection,
      updateMinimap: minimap.draw, updateFloatingOverlays, pushNews, setMarquee, setPaused, setFog,
      openMatchModal, showLoading,
      setCamLabel: (t) => { $("lbl-cam-preset").textContent = t; },
      setLightLabel: (t, id) => { $("lbl-light-preset").textContent = t; $("play-optics").dataset.lighting = id || "daylight"; },
      setFps: (fps) => { $("hud-fps").textContent = `${fps} fps`; },
    };
  }

  window.__playHud = {
    SEAT_TEAMS, SEAT_LABELS, SEAT_HEX, CLASS_META, BOMB_IDS, HILL_IDS,
    createHud, createFxAssets, pushExplosionFx, createMockMatch,
  };
})();

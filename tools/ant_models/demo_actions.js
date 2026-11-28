"use strict";

// Synchronized 1998 Ants Live Action Vignettes:
// 1. Swimmer digging a 3-tile dirt bridge across water (`Shovel` + `ShovelWater` + rising 3D dirt causeway)
// 2. Bomber placing a sizzling team bomb -> sprinting clear -> detonation launching a victim (`PlantBomb` + `BlownUp` + `Stunned`)
// 3. Combat Ant power-hitting an enemy -> dramatic airborne `BlownBack` + bounce + `Stunned` + `Hit` recovery
// 4. Fire Ant igniting a 3-tile roaring Fire Wall (`StartFire` magnifying glass) and fearlessly walking right across the flames
// 5. Regular Worker Ants brawling 1v1 with mutual strikes & authentic `Hit` knockback-roll recoveries!
(() => {
  const { mat4FromTRS, mat4Identity } = window.__antGltf;
  const { TEAMS } = window.__antRender;
  const { buildBombExplosionModels, createBombExplosionRig, updateBombExplosion } = window.__demoExplosion;

  const ACTION_ANT_CLASSES = ["worker", "combat", "bomber", "swimmer", "fire"];
  const DROPPER_FD_IDS = ["FD_BOMB", "FD_FIRE", "FD_SWIM", "FD_COMB", "FD_THIEF"];
  const ACTION_ITEM_IDS = ["wallup04", "redbomb", "bluebomb", "greenbomb", "blackbomb", "lunchbox", "dflower1", "pu_mason", "pu_swim", ...DROPPER_FD_IDS];
  const BOMB_SCALE = 0.58; // Matches `bomber_held_bomb` inside `PlantBomb` for a seamless 1:1 ground handoff!
  const FIRE_SCALE = 1.06;
  const FD_DROP_SCALE = 2.45;
  const HILL_ID_BY_TEAM = { red: "REDHILL", blue: "BLUEHILL", green: "GREENHILL", black: "BLACKHILL" };

  function lerp(a, b, u) { return a + (b - a) * u; }
  function clamp01(u) { return Math.max(0, Math.min(1, u)); }
  function smooth(u) { const c = clamp01(u); return c * c * (3 - 2 * c); }

  function quatY(rad) {
    const h = rad * 0.5;
    return [0, Math.sin(h), 0, Math.cos(h)];
  }

  function buildAntMat(model, x, y, z, scale, yawRad) {
    const minY = model && model.bounds && model.bounds.min ? model.bounds.min[1] : 0;
    return mat4FromTRS([x, -minY * scale + y, z], quatY(yawRad), [scale, scale, scale]);
  }

  function buildPropMat(model, x, y, z, sx, sy, sz, yawRad = 0) {
    const minY = model && model.bounds && model.bounds.min ? model.bounds.min[1] : 0;
    return mat4FromTRS([x, -minY * sy + y, z], quatY(yawRad), [sx, sy, sz]);
  }

  // Authentic 1998 4-stage wet dark-mud bridge tiles (`bridge1`..`bridge4`, sprites 112..115).
  // Exact dark charcoal-brown pond mud palette matching `viewer_fx.js` `colMud` & sprite `bridge4` (`sRGB ~ 62,60,58`),
  // low scalloped 4-lobed hand-shoveled mud silhouette, damp specular roughness `0.48`, and submerged mud skirt!
  function buildMudBridgeStage(gl, stage) {
    const pos = [], nrm = [], uv = [], col = [], idx = [];

    function pushMudLobe(cx, cz, rx, rz, hTop, hBase, seed, isScallop, notchAng, cSub, cDark, cMid, cTop) {
      const baseV = pos.length / 3;
      const segs = 28;
      const rings = [
        { y: hBase, sc: 1.05, c: cSub, ny: 0.40 },
        { y: 0.004, sc: 1.00, c: cDark, ny: 0.56 },
        { y: hTop * 0.62, sc: 0.87, c: cMid, ny: 0.84 },
        { y: hTop * 0.93, sc: 0.56, c: cTop, ny: 0.96 },
        { y: hTop * 0.97, sc: 0.24, c: cTop, ny: 0.99 },
      ];
      for (let r = 0; r < rings.length; r++) {
        const rg = rings[r];
        for (let s = 0; s < segs; s++) {
          const a = (s / segs) * Math.PI * 2;
          const ca = Math.cos(a), sa = Math.sin(a);
          // Classic 1998 `bridge4` silhouette: soft pillow-square pinched gently at N/S/E/W waists with 4 organic corner lobes
          const sq = isScallop
            ? 1.0 / Math.pow(Math.pow(Math.abs(ca), 3.1) + Math.pow(Math.abs(sa), 3.1), 1.0 / 3.1)
            : 1.0;
          let shape = isScallop
            ? (1.0 - 0.095 * Math.cos(a * 4.0) + 0.055 * Math.sin(a * 3.0 + seed) + 0.036 * Math.cos(a * 5.0 - seed * 1.4))
            : (1.0 + 0.14 * Math.sin(a * 3.0 + seed) + 0.09 * Math.cos(a * 5.0 + seed * 1.7));
          if (notchAng !== null) {
            // Distinctive organic water-filled bite/notch matching 1998 `bridge1`..`bridge3` incomplete stages!
            let da = a - notchAng;
            while (da > Math.PI) da -= Math.PI * 2;
            while (da < -Math.PI) da += Math.PI * 2;
            shape *= (1.0 - 0.56 * Math.exp(-(da * da) / 0.36));
          }
          const x = cx + ca * sq * rx * rg.sc * shape;
          const z = cz + sa * sq * rz * rg.sc * shape;
          const lumpY = r >= 2
            ? (0.011 * Math.sin(x * 11.5 + z * 8.7 + seed) * Math.cos(z * 10.4 - x * 6.5) + 0.005 * Math.sin(x * 23.0 - z * 19.0))
            : 0;
          const y = rg.y + lumpY;
          const nx = ca * (1.0 - rg.ny * 0.65) + (r >= 2 ? 0.22 * Math.cos(x * 14.0 + z * 9.0 + seed) : 0);
          const nz = sa * (1.0 - rg.ny * 0.65) + (r >= 2 ? 0.22 * Math.sin(z * 14.0 - x * 9.0 - seed) : 0);
          const nL = Math.hypot(nx, rg.ny, nz) || 1;
          const mottle = 0.78 + 0.34 * (0.5 + 0.5 * Math.sin(x * 11.3 + z * 9.7 + seed * 2.1) * Math.cos(z * 12.1 - x * 7.9));
          pos.push(x, y, z);
          nrm.push(nx / nL, rg.ny / nL, nz / nL);
          uv.push(x + 0.5, z + 0.5);
          col.push(rg.c[0] * mottle, rg.c[1] * mottle, rg.c[2] * mottle);
        }
      }
      const centerIdx = pos.length / 3;
      pos.push(cx, hTop, cz);
      nrm.push(0, 1, 0);
      uv.push(cx + 0.5, cz + 0.5);
      col.push(cTop[0] * 1.04, cTop[1] * 1.04, cTop[2] * 1.04);

      // Outward CCW winding (verified against +Y normal when angle advances CW in XZ)
      for (let r = 0; r < rings.length - 1; r++) {
        for (let s = 0; s < segs; s++) {
          const ns = (s + 1) % segs;
          const a = baseV + r * segs + s, b = baseV + r * segs + ns;
          const c = baseV + (r + 1) * segs + s, d = baseV + (r + 1) * segs + ns;
          idx.push(a, d, b, a, c, d);
        }
      }
      const topRing = baseV + (rings.length - 1) * segs;
      for (let s = 0; s < segs; s++) {
        idx.push(topRing + s, centerIdx, topRing + ((s + 1) % segs));
      }
    }

    // Authentic 1998 dark wet pond mud linear RGB (matches `colMud` in `viewer_fx.js` & `bridge4` sprite 115!)
    const subCol = [0.017, 0.018, 0.028];
    const rimDark = [0.025, 0.022, 0.022];
    const mudMid = [0.052, 0.046, 0.042];
    const mudTop = [0.075, 0.066, 0.059];
    const siltBlue = [0.026, 0.030, 0.068];

    if (stage === 1) {
      // `bridge1` (112): dark churned indigo-silt swirl + tiny first mud clod barely breaking water
      pushMudLobe(0.0, 0.0, 0.27, 0.24, 0.020, -0.22, 1.1, false, 0.65, subCol, siltBlue, [0.032, 0.034, 0.062], [0.044, 0.039, 0.038]);
    } else if (stage === 2) {
      // `bridge2` (113): two overlapping wet dark-mud clods emerging from dark stirred water
      pushMudLobe(-0.06, -0.04, 0.32, 0.28, 0.038, -0.24, 2.3, false, 0.9, subCol, rimDark, [0.045, 0.040, 0.039], [0.064, 0.057, 0.052]);
      pushMudLobe(0.11, 0.09, 0.20, 0.18, 0.044, -0.18, 4.1, false, null, subCol, rimDark, mudMid, mudTop);
    } else if (stage === 3) {
      // `bridge3` (114): 3/4-built lumpy mud pad with distinctive open water notch on SE side
      pushMudLobe(-0.03, -0.02, 0.45, 0.42, 0.056, -0.26, 3.2, true, 0.85, subCol, rimDark, mudMid, mudTop);
      pushMudLobe(-0.14, 0.10, 0.23, 0.20, 0.062, -0.16, 5.4, false, null, subCol, rimDark, mudMid, [0.082, 0.072, 0.064]);
    } else {
      // `bridge4` / `bridge4b` (115/116): iconic low flat-crowned scalloped dark-mud pad + hand-shoveled mud clods!
      pushMudLobe(0.0, 0.0, 0.50, 0.50, 0.064, -0.28, 0.8, true, null, subCol, rimDark, mudMid, mudTop);
      pushMudLobe(-0.15, -0.13, 0.22, 0.19, 0.073, -0.10, 2.7, false, null, rimDark, rimDark, mudMid, [0.084, 0.074, 0.066]);
      pushMudLobe(0.16, 0.12, 0.21, 0.20, 0.071, -0.10, 6.1, false, null, rimDark, rimDark, mudMid, [0.080, 0.070, 0.062]);
    }

    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const bufs = [];
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
      animations: [], idleAnims: [], blinkInfo: { hasBlink: false },
      baseWorldMats: [idMat],
      bounds: { min: [-0.60, -0.28, -0.60], max: [0.60, 0.08, 0.60] },
      size: [1.20, 0.36, 1.20],
      drawCalls: [{
        nodeIndex: 0, worldMat: idMat,
        prim: {
          vao, indexed: true, mode: gl.TRIANGLES, drawCount: idx.length, indexType: gl.UNSIGNED_SHORT,
          hasUv: false, localMin: [-0.60, -0.28, -0.60], localMax: [0.60, 0.08, 0.60],
          material: {
            name: `item_mud_bridge_${stage}`, baseColor: [1, 1, 1, 1], metallic: 0.0, roughness: 0.72,
            normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
            emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: true,
          },
        },
      }],
      dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
  }

  function makeAntInst(cls, team, model, phase, submerged = false) {
    return {
      kind: "ant", cls, submerged, teamColors: TEAMS[team] || TEAMS.green,
      model, clipName: "Idle", idleTime: 0, animTime: undefined,
      blink: { active: false, elapsed: 0, nextIn: 1.8 + phase },
      offsetMat: mat4Identity(),
    };
  }

  function makePropInst(id, model, submerged = false) {
    return { kind: "item", id, submerged, model, clipName: "Idle", idleTime: 0, offsetMat: mat4Identity() };
  }

  function findBridgeShore(mapData, waterTile, prefX, prefZ) {
    const cols = mapData.cols, rows = mapData.rows, tiles = mapData.tiles || [];
    let best = null, bestD = 1e9;
    for (let r = 3; r < rows - 3; r++) {
      for (let c = 3; c < cols - 5; c++) {
        if (tiles[r * cols + c] !== waterTile && tiles[r * cols + c + 1] === waterTile
          && tiles[r * cols + c + 2] === waterTile && tiles[r * cols + c + 3] === waterTile) {
          const d = Math.hypot(c - prefX, r - prefZ);
          if (d < bestD) { bestD = d; best = [c + 0.45, r + 0.5]; }
        }
      }
    }
    return best;
  }

  function findWaterDigSpan(mapData, waterTile, prefX, prefZ) {
    const cols = mapData.cols, rows = mapData.rows, tiles = mapData.tiles || [];
    let best = null, bestD = 1e9;
    for (let r = 3; r < rows - 3; r++) {
      for (let c = 3; c < cols - 5; c++) {
        if (tiles[r * cols + c] === waterTile && tiles[r * cols + c + 1] === waterTile
          && tiles[r * cols + c + 2] === waterTile && tiles[r * cols + c + 3] === waterTile) {
          const d = Math.hypot(c + 1.5 - prefX, r + 0.5 - prefZ);
          if (d < bestD) { bestD = d; best = [c + 0.5, r + 0.5]; }
        }
      }
    }
    return best;
  }

  function buildActionVignettes(gl, mapData, models, waterTile, antScale, itemEntries) {
    const bridgeStages = [1, 2, 3, 4].map((st) => buildMudBridgeStage(gl, st));
    const blastModels = buildBombExplosionModels(gl);
    const bridgeModel = { dispose() { bridgeStages.forEach((m) => m.dispose()); blastModels.dispose(); } };
    const v = { bridgeModel, bridgeStages, blastModels, bridges: [], waterBridges: [], swimmers: [], morphs: [], bombers: [], combats: [], fires: [], brawls: [], deposits: [], droppers: [] };
    const mid = mapData.id;

    // 1A. Land-Digging Swimmers (`asbbl` / `Shovel`): build 3-tile dark-mud bridges (`bridge1`..`bridge4`) from shore!
    const bridgeSpots = mid === "treasure"
      ? [[13.45, 28.5, "blue", 0.0], [27.45, 42.5, "green", 4.5]]
      : (mid === "gauntlet"
        ? [[51.45, 40.5, "blue", 0.3]]
        : (mid === "medium"
          ? [[26.45, 41.5, "blue", 0.4]]
          : (mid === "islands"
            ? [[27.45, 29.5, "blue", 0.5]]
            : (mid === "small" ? [[15.45, 24.5, "blue", 0.2]] : []))));
    for (const [px, pz, team, ph] of bridgeSpots) {
      const sh = findBridgeShore(mapData, waterTile, px, pz);
      if (!sh || !models.swimmer) continue;
      v.bridges.push({
        sx: sh[0], sz: sh[1], phase: ph, antScale,
        swimmer: makeAntInst("swimmer", team, models.swimmer, ph, false),
        tiles: [0, 1, 2].map(() => makePropInst("bridge_mud", bridgeStages[3], true)),
      });
    }

    // 1B. Underwater-Digging Swimmers (`asbbw` / `ShovelWater` + `Swim`): swim in pond & dig mud bridges RIGHT FROM UNDERWATER!
    const waterDigSpots = mid === "treasure"
      ? [[16.0, 30.5, "green", 1.1, -1], [30.5, 42.5, "blue", 4.2, +1]]
      : (mid === "gauntlet"
        ? [[54.5, 42.5, "green", 1.0, -1]]
        : (mid === "medium"
          ? [[30.5, 41.5, "green", 0.9, -1]]
          : (mid === "islands"
            ? [[30.5, 30.5, "green", 0.9, +1]]
            : (mid === "small" ? [[20.0, 16.5, "green", 0.7, +1]] : []))));
    for (const [px, pz, team, ph, dir] of waterDigSpots) {
      const sp = findWaterDigSpan(mapData, waterTile, px, pz);
      if (!sp || !models.swimmer) continue;
      v.waterBridges.push({
        x0: sp[0], sz: sp[1], dir, phase: ph, antScale,
        swimmer: makeAntInst("swimmer", team, models.swimmer, ph, true),
        tiles: [0, 1].map(() => makePropInst("bridge_mud_uw", bridgeStages[3], true)),
      });
    }

    // 1c. Authentic 1998 Amphibious Swimmer Transitions: Walk on mud -> Dive into pond -> Swim underwater -> Surface back onto land!
    const swimTransSpots = mid === "treasure"
      ? [[27.45, 16.5, "blue", 0.4], [28.45, 44.5, "green", 3.1]]
      : (mid === "small"
        ? [[14.45, 18.5, "blue", 0.3]]
        : (mid === "islands"
          ? [[28.45, 25.5, "red", 0.5]]
          : (mid === "medium"
            ? [[22.45, 17.5, "blue", 0.4]]
            : (mid === "gauntlet" ? [[47.45, 6.5, "blue", 0.4]] : []))));
    for (const [sx, sz, team, ph] of swimTransSpots) {
      const sh = findBridgeShore(mapData, waterTile, sx, sz);
      if (!sh || !models.swimmer) continue;
      v.swimmers.push({
        landX: sh[0] - 0.95, shoreX: sh[0] + 0.12, waterX: sh[0] + 2.35,
        z: sh[1], phase: ph, antScale,
        swimmer: makeAntInst("swimmer", team, models.swimmer, ph, false),
      });
    }

    // 1d. Live Worker -> Specialty Class Power-Up Morph Vignette (`worker` picks up `pu_mason` or `pu_swim` -> transforms into Fire/Swimmer Ant!)
    const morphSpotMap = {
      treasure: [[26.5, 29.0, "green", "fire", "pu_mason", 0.6], [33.5, 29.0, "blue", "swimmer", "pu_swim", 3.4]],
      gauntlet: [[29.5, 32.0, "green", "fire", "pu_mason", 0.8]],
      medium: [[30.0, 29.5, "red", "fire", "pu_mason", 0.5]],
      islands: [[29.5, 29.5, "green", "swimmer", "pu_swim", 0.7]],
      small: [[20.5, 29.5, "green", "fire", "pu_mason", 0.4]],
    };
    for (const [mx, mz, team, targetCls, puId, ph] of (morphSpotMap[mid] || [])) {
      if (!models.worker || !models[targetCls] || !models[puId]) continue;
      v.morphs.push({
        x: mx, z: mz, phase: ph, antScale, targetCls,
        worker: makeAntInst("worker", team, models.worker, ph),
        morphed: makeAntInst(targetCls, team, models[targetCls], ph),
        powerup: makePropInst(puId, models[puId]),
        ring: makePropInst("morph_ring", blastModels.ring),
      });
    }

    // 2. Bomber seamlessly planting Red Bomb -> 1998 `bombex` explosion + `BlownBack` flight (alternating with Dud `BlownUp`!)
    const bomberSpotMap = {
      treasure: [38.6, 29.0], gauntlet: [39.5, 19.5], medium: [39.5, 19.5],
      small: [29.0, 16.5], popcorn: [15.5, 12.2], bombz_away: [15.5, 15.5], hellraiser: [15.5, 8.5],
    };
    if (models.bomber && models.worker && models.redbomb && bomberSpotMap[mid]) {
      const [bx, bz] = bomberSpotMap[mid];
      v.bombers.push({
        x: bx, z: bz, phase: 1.0, antScale, drownEast: mid === "treasure",
        bomber: makeAntInst("bomber", "red", models.bomber, 0.3),
        victim: makeAntInst("worker", "blue", models.worker, 0.7),
        bomb: makePropInst("redbomb", models.redbomb),
        explosion: createBombExplosionRig(blastModels),
      });
    }

    // 3. Combat Ant power-hitting Worker -> huge BlownBack launch + Stunned + Hit roll recovery
    const combatSpotMap = {
      treasure: [30.2, 22.2], gauntlet: [22.5, 22.5], medium: [21.0, 21.0],
      islands: [23.0, 20.0], small: [11.5, 15.0], bombz_away: [15.5, 9.5], hellraiser: [8.5, 15.5],
    };
    if (models.combat && models.worker) {
      const [cx, cz] = combatSpotMap[mid] || [mapData.cols * 0.5 - 1.2, mapData.rows * 0.5 - 2.2];
      v.combats.push({
        x: cx, z: cz, phase: 0.4, antScale,
        attacker: makeAntInst("combat", "green", models.combat, 0.1),
        victim: makeAntInst("worker", "black", models.worker, 0.6),
      });
    }

    // 4. Fire Ant seamlessly igniting 3-tile Fire Wall right under magnifying glass & walking right through the flames!
    const fireSpotMap = {
      treasure: [30.2, 36.0], gauntlet: [36.5, 36.5], medium: [39.0, 39.0],
      islands: [37.2, 33.0], small: [28.0, 32.5], bombz_away: [15.5, 21.5], hellraiser: [15.5, 22.5],
    };
    if (models.fire && models.worker && models.wallup04) {
      const [fx, fz] = fireSpotMap[mid] || [mapData.cols * 0.5, mapData.rows * 0.5 + 3.0];
      v.fires.push({
        x: fx, z: fz, phase: 2.2, antScale,
        fireAnt: makeAntInst("fire", "red", models.fire, 0.2),
        bystander: makeAntInst("worker", "green", models.worker, 0.9),
        flames: [makePropInst("wallup04", models.wallup04), makePropInst("wallup04", models.wallup04), makePropInst("wallup04", models.wallup04)],
      });
    }

    // 5. Regular Worker Ants brawling 1v1 with mutual strikes & Hit knockback-roll recoveries
    const brawlSpotMap = {
      treasure: [21.4, 29.0], gauntlet: [17.5, 32.5], medium: [20.5, 38.0],
      islands: [37.0, 21.5], small: [11.0, 25.5], bombz_away: [9.5, 15.5], hellraiser: [22.5, 15.5],
    };
    if (models.worker) {
      const [rx, rz] = brawlSpotMap[mid] || [mapData.cols * 0.5 + 2.8, mapData.rows * 0.5];
      v.brawls.push({
        x: rx, z: rz, phase: 0.8, antScale,
        a: makeAntInst("worker", "blue", models.worker, 0.2),
        b: makeAntInst("worker", "red", models.worker, 0.5),
      });
    }

    // 6. Food-Depositing Worker Ants: carry swinging lunchbox -> climb anthill -> Enter crater hole -> Hatch out empty-handed!
    if (models.worker && models.lunchbox && Array.isArray(itemEntries)) {
      const depSpecs = (mid === "treasure" || mid === "gauntlet" || mid === "medium")
        ? [["red", 0.0, -0.34], ["red", 4.8, +0.34], ["blue", 2.4, 0.0]]
        : (mid === "small"
          ? [["red", 0.2, -0.34], ["red", 5.0, +0.34], ["blue", 2.6, 0.0]]
          : [["red", 0.4, -0.32], ["red", 5.2, +0.32]]);
      for (const [team, ph, angOff] of depSpecs) {
        const hid = HILL_ID_BY_TEAM[team];
        const hill = itemEntries.find((e) => e.spec.id === hid);
        if (!hill) continue;
        const m = hill.inst.model, s = hill.spec.s;
        let rx = 0, rz = 0;
        if (m && Array.isArray(m.nodes)) {
          const rn = m.nodes.find((n) => n && n.name === "anthill");
          if (rn && rn.translation) { rx = rn.translation[0]; rz = rn.translation[2]; }
        }
        const minY = m && m.bounds && m.bounds.min ? m.bounds.min[1] : 0;
        const hx = hill.x + rx * s, hz = hill.z + rz * s;
        const rimY = (0.436 - minY) * s;
        const baseAng = Math.atan2(mapData.rows * 0.5 - hz, mapData.cols * 0.5 - hx) + angOff;
        const ux = Math.cos(baseAng), uz = Math.sin(baseAng);
        v.deposits.push({
          team, phase: ph, antScale, hx, hz, rimY, moundR: 0.56 * s, ux, uz,
          worker: makeAntInst("worker", team, models.worker, ph),
          lunchbox: makePropInst("lunchbox", models.lunchbox),
        });
      }
    }

    // 7. Authentic 1998 Groucho Daisy Power-Up Droppers on Small, Islands, Gauntlet & Medium (`dflower1` + 820 ms `FD_*` drops!)
    if ((mid === "small" || mid === "islands" || mid === "gauntlet" || mid === "medium") && Array.isArray(itemEntries)) {
      const fdPool = DROPPER_FD_IDS.filter((id) => Boolean(models[id]));
      const flowers = itemEntries.filter((e) => e.spec.id === "flower1");
      if (fdPool.length && flowers.length) {
        const orders = mid === "gauntlet"
          ? [["FD_COMB", "FD_FIRE", "FD_BOMB", "FD_SWIM", "FD_THIEF"]]
          : [
              ["FD_BOMB", "FD_FIRE", "FD_SWIM", "FD_COMB", "FD_THIEF"],
              ["FD_SWIM", "FD_THIEF", "FD_BOMB", "FD_FIRE", "FD_COMB"],
            ];
        flowers.forEach((fl, fi) => {
          fl.managedAnim = true;
          const fdInsts = {};
          for (const fid of fdPool) fdInsts[fid] = makePropInst(fid, models[fid]);
          const order = (orders[fi % orders.length]).filter((id) => Boolean(fdInsts[id]));
          v.droppers.push({
            fx: fl.x, fz: fl.z, landX: fl.x, landZ: fl.z + 1.0, phase: fi * 1.9,
            flowerInst: fl.inst,
            order: order.length ? order : fdPool,
            fdInsts,
          });
        });
      }
    }

    return v;
  }

  function updateActionVignettes(v, t, patrolsOn, tx, tz, visHalfSpan, visible, ripples, dynGlows) {
    if (!v) return;
    const inView = (x, z) => Math.abs(x - tx) <= visHalfSpan + 4.0 && Math.abs(z - tz) <= visHalfSpan + 4.0;

    // Helper to render one dark-mud bridge tile at its exact 1998 construction stage (`0` hidden, `0.01..1.0` -> `bridge1..bridge4`)
    function pushMudBridgeTile(inst, stages, prog, bx, bz, rotIdx) {
      if (prog <= 0.02) return;
      const stIdx = prog < 0.28 ? 0 : (prog < 0.54 ? 1 : (prog < 0.80 ? 2 : 3));
      const subU = stIdx < 3 ? clamp01((prog - stIdx * 0.26) / 0.26) : clamp01((prog - 0.78) / 0.22);
      inst.model = stages[stIdx];
      const sc = stIdx === 3 ? lerp(0.94, 1.02, smooth(subU)) : lerp(0.86, 1.02, smooth(subU));
      const rY = lerp(-0.04, 0.0, smooth(subU));
      inst.offsetMat = mat4FromTRS([bx, rY, bz], quatY(rotIdx * 1.5708), [sc, 1.0, sc]);
      visible.push(inst);
    }

    // --- 1A. Land-Digging Swimmers (`Shovel` / `asbbl`): Seamless 1:1 Shovel-Toss onto Growing Mud Bridge ---
    // Measured `swimmer_dirt_toss` in `swimmer.glb`: clods land at `+0.74 * antScale` (`~0.85` tiles ahead) at `t = 0.36 s`.
    const mudH = 0.064;
    for (const b of v.bridges) {
      if (!inView(b.sx + 1.5, b.sz)) continue;
      const dur = 11.4;
      const lt = patrolsOn ? ((t + b.phase) % dur + dur) % dur : 4.85;
      const reach = 0.72 * b.antScale; // Exact shovel-toss landing offset (~0.83 tiles) so mud lands dead-center on target tile!
      const tX = [b.sx + 1.0, b.sx + 2.0, b.sx + 3.0];
      const digX = [tX[0] - reach, tX[1] - reach, tX[2] - reach];
      let ax = digX[0], ay = 0, az = b.sz, yaw = Math.PI * 0.5;
      const prog = [0, 0, 0];
      if (lt < 1.0) {
        const u = smooth(lt / 1.0);
        ax = lerp(b.sx - 0.85, digX[0], u);
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 2.92) {
        // 4 crisp `Shovel` scoops (4 x 0.48 s = 1.92 s) tossing mud directly onto tile #0 (`bridge1..bridge4`)!
        const dtS = lt - 1.0;
        ax = digX[0];
        b.swimmer.clipName = "Shovel"; b.swimmer.animTime = dtS;
        prog[0] = clamp01((dtS - 0.24) / 1.60);
        if (ripples.length < 8) ripples.push([tX[0], b.sz, b.phase, 0.85]);
      } else if (lt < 3.62) {
        const u = smooth((lt - 2.92) / 0.70);
        ax = lerp(digX[0], digX[1], u); ay = lerp(0.0, mudH, smooth(clamp01((u - 0.2) / 0.8))); prog[0] = 1;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 5.54) {
        const dtS = lt - 3.62;
        ax = digX[1]; ay = mudH; prog[0] = 1; prog[1] = clamp01((dtS - 0.24) / 1.60);
        b.swimmer.clipName = "Shovel"; b.swimmer.animTime = dtS;
        if (ripples.length < 8) ripples.push([tX[1], b.sz, b.phase + 0.3, 0.85]);
      } else if (lt < 6.24) {
        const u = smooth((lt - 5.54) / 0.70);
        ax = lerp(digX[1], digX[2], u); ay = mudH; prog[0] = 1; prog[1] = 1;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 8.16) {
        const dtS = lt - 6.24;
        ax = digX[2]; ay = mudH; prog[0] = 1; prog[1] = 1; prog[2] = clamp01((dtS - 0.24) / 1.60);
        b.swimmer.clipName = "Shovel"; b.swimmer.animTime = dtS;
        if (ripples.length < 8) ripples.push([tX[2], b.sz, b.phase + 0.6, 0.85]);
      } else if (lt < 10.1) {
        const u = smooth((lt - 8.16) / 1.40);
        ax = lerp(digX[2], tX[2], u); ay = mudH; prog[0] = 1; prog[1] = 1; prog[2] = 1;
        b.swimmer.clipName = u < 0.95 ? "Walk" : "Idle"; b.swimmer.animTime = lt * 1.4;
      } else {
        const u = smooth((lt - 10.1) / 1.30);
        ax = lerp(tX[2], b.sx - 0.85, u); yaw = -Math.PI * 0.5;
        ay = lerp(mudH, 0.0, clamp01(u * 1.4));
        const fade = 1 - smooth(clamp01((u - 0.35) / 0.65));
        prog[0] = fade; prog[1] = fade; prog[2] = fade;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.8;
      }
      for (let i = 0; i < 3; i++) pushMudBridgeTile(b.tiles[i], v.bridgeStages, prog[i], tX[i], b.sz, i);
      b.swimmer.offsetMat = buildAntMat(b.swimmer.model, ax, ay, az, b.antScale, yaw);
      visible.push(b.swimmer);
    }

    // --- 1B. Underwater-Digging Swimmers (`Swim` + `ShovelWater` / `asbbw`): Seamless Underwater Shovel-Toss! ---
    for (const uwb of v.waterBridges) {
      if (!inView(uwb.x0 + 1.5, uwb.sz)) continue;
      const dur = 10.2;
      const lt = patrolsOn ? ((t + uwb.phase) % dur + dur) % dur : 3.25;
      const d = uwb.dir;
      const t0x = d > 0 ? (uwb.x0 + 2.0) : (uwb.x0 + 1.0);
      const t1x = d > 0 ? (uwb.x0 + 1.0) : (uwb.x0 + 2.0);
      const reach = 0.76 * uwb.antScale; // Exact `swimmer_water_toss` spray/mud landing offset (~0.87 tiles ahead)
      const dig0x = t0x - d * reach;
      const dig1x = t1x - d * reach;
      const startX = dig0x - d * 0.75;
      const subSwimY = -1.05 * uwb.antScale;
      const subDigY = -0.46 * uwb.antScale;
      let ax = dig0x, ay = subSwimY, yaw = d > 0 ? Math.PI * 0.5 : -Math.PI * 0.5;
      const prog = [0, 0];

      if (lt < 1.35) {
        const u = smooth(lt / 1.35);
        ax = lerp(startX, dig0x, u);
        ay = lerp(subSwimY, subDigY, smooth(clamp01((u - 0.65) / 0.35))) + Math.sin(lt * 5.0) * 0.025;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.4;
      } else if (lt < 3.35) {
        const dtS = lt - 1.35;
        ax = dig0x; ay = subDigY + Math.sin(lt * 6.5) * 0.02;
        uwb.swimmer.clipName = "ShovelWater"; uwb.swimmer.animTime = dtS;
        prog[0] = clamp01((dtS - 0.25) / 1.65);
        if (ripples.length < 8) ripples.push([t0x, uwb.sz, uwb.phase + 0.1, 0.92]);
      } else if (lt < 4.65) {
        const u = smooth((lt - 3.35) / 1.30);
        ax = lerp(dig0x, dig1x, u);
        ay = lerp(subDigY, subSwimY, Math.sin(u * Math.PI)) + Math.sin(lt * 5.0) * 0.025;
        yaw = d > 0 ? -Math.PI * 0.5 : Math.PI * 0.5;
        prog[0] = 1;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.4;
      } else if (lt < 6.65) {
        const dtS = lt - 4.65;
        ax = dig1x; ay = subDigY + Math.sin(lt * 6.5) * 0.02;
        uwb.swimmer.clipName = "ShovelWater"; uwb.swimmer.animTime = dtS;
        prog[0] = 1; prog[1] = clamp01((dtS - 0.25) / 1.65);
        if (ripples.length < 8) ripples.push([t1x, uwb.sz, uwb.phase + 0.4, 0.92]);
      } else if (lt < 8.70) {
        const u = smooth((lt - 6.65) / 2.05);
        ax = lerp(dig1x, startX, u);
        ay = lerp(subDigY, subSwimY, clamp01(u * 2.2)) + Math.sin(lt * 5.0) * 0.028;
        prog[0] = 1; prog[1] = 1;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.3;
      } else {
        const u = smooth((lt - 8.70) / 1.50);
        ax = startX; ay = subSwimY + Math.sin(lt * 4.8) * 0.028;
        const fade = 1 - smooth(u);
        prog[0] = fade; prog[1] = fade;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.2;
      }

      if (ripples.length < 8) ripples.push([ax, uwb.sz, uwb.phase + 0.7, 0.78]);
      pushMudBridgeTile(uwb.tiles[0], v.bridgeStages, prog[0], t0x, uwb.sz, 1);
      pushMudBridgeTile(uwb.tiles[1], v.bridgeStages, prog[1], t1x, uwb.sz, 2);
      uwb.swimmer.offsetMat = buildAntMat(uwb.swimmer.model, ax, ay, uwb.sz, uwb.antScale, yaw);
      visible.push(uwb.swimmer);
    }

    // --- 1c. Authentic 1998 Amphibious Swimmer Land <-> Water Transitions (`Walk` -> `Dive` -> `Swim` -> `Surface` -> `Walk`) ---
    for (const sw of v.swimmers) {
      if (!inView(sw.shoreX, sw.z)) continue;
      const dur = 8.4;
      const lt = patrolsOn ? ((t + sw.phase) % dur + dur) % dur : 1.72;
      const swimDepth = -0.34 * sw.antScale;
      let ax = sw.landX, ay = 0.0, yaw = Math.PI * 0.5;
      sw.swimmer.submerged = false;

      if (lt < 1.15) {
        // Walk from mud bank up to the shoreline edge
        const u = smooth(lt / 1.15);
        ax = lerp(sw.landX, sw.shoreX, u);
        sw.swimmer.clipName = "Walk"; sw.swimmer.animTime = lt * 1.45;
      } else if (lt < 2.19) {
        // Authentic `Dive` (`asdi`, 1.04s): leaps head-first off the bank & splashes under the water surface!
        const dtD = lt - 1.15;
        const u = clamp01(dtD / 1.04);
        ax = lerp(sw.shoreX, sw.shoreX + 0.85, smooth(u));
        ay = u < 0.42 ? Math.sin((u / 0.42) * Math.PI) * 0.20 : lerp(0.0, swimDepth, smooth((u - 0.42) / 0.58));
        sw.swimmer.submerged = u > 0.46;
        sw.swimmer.clipName = "Dive"; sw.swimmer.animTime = Math.min(1.03, dtD);
        if (u > 0.36 && ripples.length < 8) ripples.push([ax, sw.z, (dtD * 1.3) % 1.0, 0.95]);
      } else if (lt < 4.55) {
        // Swims gracefully out into deep sapphire water & turns back toward shore!
        const u = (lt - 2.19) / 2.36;
        const out = u < 0.50;
        const p = smooth(out ? (u / 0.50) : ((u - 0.50) / 0.50));
        ax = out ? lerp(sw.shoreX + 0.85, sw.waterX, p) : lerp(sw.waterX, sw.shoreX + 0.55, p);
        ay = swimDepth + Math.sin(lt * 5.2) * 0.025;
        yaw = out ? Math.PI * 0.5 : -Math.PI * 0.5;
        sw.swimmer.submerged = true;
        sw.swimmer.clipName = "Swim"; sw.swimmer.animTime = lt * 1.35;
        if (ripples.length < 8) ripples.push([ax, sw.z, sw.phase + 0.25, 0.72]);
      } else if (lt < 5.35) {
        // Authentic `Surface` (`asgo`, 0.42s x2): pops back out of the pond & scrambles onto dry mud!
        const dtS = lt - 4.55;
        const u = clamp01(dtS / 0.80);
        ax = lerp(sw.shoreX + 0.55, sw.shoreX - 0.10, smooth(u));
        ay = lerp(swimDepth, 0.0, smooth(u)) + Math.sin(u * Math.PI) * 0.08;
        yaw = -Math.PI * 0.5;
        sw.swimmer.submerged = u < 0.45;
        sw.swimmer.clipName = "Surface"; sw.swimmer.animTime = Math.min(0.41, u * 0.41);
        if (u < 0.75 && ripples.length < 8) ripples.push([sw.shoreX + 0.35, sw.z, (dtS * 1.4) % 1.0, 0.88]);
      } else if (lt < 6.85) {
        // Walks proudly back onto land shaking off water
        const u = smooth((lt - 5.35) / 1.50);
        ax = lerp(sw.shoreX - 0.10, sw.landX, u);
        yaw = -Math.PI * 0.5;
        sw.swimmer.clipName = "Walk"; sw.swimmer.animTime = lt * 1.45;
      } else {
        // Turns back toward pond ready for next dive
        const u = smooth((lt - 6.85) / 1.55);
        ax = sw.landX;
        yaw = lerp(-Math.PI * 0.5, Math.PI * 0.5, smooth(clamp01(u * 2.2)));
        sw.swimmer.clipName = "Idle"; sw.swimmer.animTime = lt;
      }
      sw.swimmer.offsetMat = buildAntMat(sw.swimmer.model, ax, ay, sw.z, sw.antScale, yaw);
      visible.push(sw.swimmer);
    }

    // --- 1d. Live Worker -> Specialty Class Power-Up Morph (`worker` collects `pu_mason` / `pu_swim` -> Fire / Swimmer Ant!) ---
    for (const mp of v.morphs) {
      if (!inView(mp.x, mp.z)) continue;
      const dur = 7.6;
      const lt = patrolsOn ? ((t + mp.phase) % dur + dur) % dur : 2.55;
      const startX = mp.x - 1.85, endX = mp.x + 1.85;
      const glowCol = mp.targetCls === "fire" ? [2.1, 0.95, 0.25] : [0.35, 1.45, 2.2];

      if (lt < 2.20) {
        // Regular Worker approaches the spinning specialty power-up emblem
        const u = smooth(lt / 2.20);
        const wx = lerp(startX, mp.x - 0.12, u);
        mp.worker.clipName = "Walk"; mp.worker.animTime = lt * 1.5;
        mp.worker.offsetMat = buildAntMat(mp.worker.model, wx, 0, mp.z, mp.antScale, Math.PI * 0.5);
        const bob = 0.08 + Math.sin(t * 3.8 + mp.phase) * 0.05;
        mp.powerup.offsetMat = buildPropMat(mp.powerup.model, mp.x, bob, mp.z, 0.72, 0.72, 0.72, t * 1.6);
        visible.push(mp.worker, mp.powerup);
        dynGlows.push([mp.x, mp.z, 2.1, 0.72 + 0.18 * Math.sin(t * 4.2), ...glowCol]);
      } else if (lt < 3.05) {
        // Power-Up pickup morph burst: sleek rising golden aura ring as Worker transforms cleanly into Fire / Swimmer Ant!
        const u = clamp01((lt - 2.20) / 0.85);
        const burst = Math.sin(u * Math.PI);
        const active = u < 0.44 ? mp.worker : mp.morphed;
        const popSc = mp.antScale * (1.0 + 0.16 * burst);
        const spinYaw = Math.PI * 0.5 + (u < 0.44 ? u * 2.4 : (1.0 - u) * -2.4);
        active.clipName = "Idle"; active.animTime = lt;
        active.offsetMat = buildAntMat(active.model, mp.x, burst * 0.14, mp.z, popSc, spinYaw);
        visible.push(active);
        const rRad = lerp(0.50, 0.92, u);
        const rH = Math.max(0.08, burst * 0.55);
        mp.ring.offsetMat = mat4FromTRS([mp.x, lerp(0.12, 0.88, u), mp.z], quatY(lt * 5.0), [rRad, rH, rRad]);
        visible.push(mp.ring);
        dynGlows.push([mp.x, mp.z, 3.2, 2.0 * burst, ...glowCol]);
      } else if (lt < 6.45) {
        // Newly morphed Fire Ant / Swimmer Ant struts forward & shows off its new specialty class!
        const u = clamp01((lt - 3.05) / 3.40);
        const mx = u < 0.55 ? lerp(mp.x, endX, smooth(u / 0.55)) : endX;
        mp.morphed.clipName = u < 0.55 ? "Walk" : (mp.targetCls === "fire" ? "StartFire" : "Shovel");
        mp.morphed.animTime = u < 0.55 ? (lt * 1.45) : (lt - 4.92);
        mp.morphed.offsetMat = buildAntMat(mp.morphed.model, mx, 0, mp.z, mp.antScale, Math.PI * 0.5);
        visible.push(mp.morphed);
      } else {
        // Smooth reset: power-up respawns softly for the next Worker coming up the path
        const u = smooth((lt - 6.45) / 1.15);
        const puSc = 0.72 * u;
        const wx = lerp(startX - 0.5, startX, u);
        mp.worker.clipName = "Walk"; mp.worker.animTime = lt * 1.4;
        mp.worker.offsetMat = buildAntMat(mp.worker.model, wx, 0, mp.z, mp.antScale, Math.PI * 0.5);
        if (puSc > 0.05) {
          mp.powerup.offsetMat = buildPropMat(mp.powerup.model, mp.x, 0.08, mp.z, puSc, puSc, puSc, t * 1.6);
          visible.push(mp.powerup);
        }
        visible.push(mp.worker);
      }
    }

    // --- 2. Seamless Bomber (`PlantBomb` 1:1 ground handoff) -> 1998 `bombex` Explosion + `BlownBack` Flight (vs Dud `BlownUp`!) ---
    // Measured `bomber_held_bomb` in `bomber.glb`: touches down at exact local `+0.348 * antScale` (`t = 1.08..1.32 s`), hides at `1.36 s`.
    for (const m of v.bombers) {
      if (!inView(m.x, m.z)) continue;
      const roundDur = 9.4;
      const fullDur = roundDur * 2.0; // Round 0: Live Bomb (`bombex` + `BlownBack` flight); Round 1: Dud Bomb (fizzle + `BlownUp` straight up!)
      const ft = patrolsOn ? ((t + m.phase) % fullDur + fullDur) % fullDur : 5.20;
      const isDud = ft >= roundDur;
      const lt = ft % roundDur;
      const plantX = m.x - 0.348 * m.antScale; // EXACT 1:1 coordinate so `bomber_held_bomb` touches down squarely on `(m.x, 0, m.z)`!
      let bx = plantX, by = 0, byaw = Math.PI * 0.5;
      let vx = m.x + 2.8, vy = 0, vz = m.z, vyaw = -Math.PI * 0.5;
      let showBomb = false, bombScale = BOMB_SCALE;
      const blastT = lt >= 4.90 ? (lt - 4.90) : -1;

      if (lt < 1.30) {
        const u = smooth(lt / 1.30);
        bx = lerp(m.x - 2.6, plantX, u);
        m.bomber.clipName = "Walk"; m.bomber.animTime = lt * 1.5;
      } else if (lt < 2.63) {
        // `PlantBomb` plays cleanly (`0.00 -> 1.33 s`): ONLY `bomber_held_bomb` is shown as he sets it on `(m.x, 0, m.z)`!
        bx = plantX;
        m.bomber.clipName = "PlantBomb"; m.bomber.animTime = lt - 1.30;
        showBomb = false;
      } else if (lt < 4.90) {
        // Exact frame `lt = 2.63`: `redbomb` takes over AT THE EXACT SAME `(m.x, 0, m.z)` where his hands set it down!
        const u = smooth((lt - 2.63) / 1.55);
        bx = lerp(plantX, m.x - 2.9, u); byaw = u < 0.94 ? -Math.PI * 0.5 : Math.PI * 0.5;
        m.bomber.clipName = u < 0.94 ? "Walk" : "Idle"; m.bomber.animTime = lt * 1.8;
        showBomb = true;
        const settle = smooth(clamp01((lt - 2.63) / 0.14));
        const pulse = lt > 4.25 ? (1.0 + 0.14 * Math.sin((lt - 4.25) * 34.0)) : 1.0;
        bombScale = lerp(0.44, BOMB_SCALE, settle) * pulse;
        dynGlows.push([m.x, m.z, 2.2, lt > 4.25 ? 1.45 : 0.72, 1.8, 0.72, 0.16]);
      } else {
        bx = m.x - 2.9; byaw = Math.PI * 0.5;
        m.bomber.clipName = "Idle"; m.bomber.animTime = lt;
      }

      // Victim Blue Worker walks directly ONTO the bomb `(m.x, m.z)`!
      let showVictim = true;
      m.victim.submerged = false;
      if (lt < 2.85) {
        vx = m.x + 2.8; m.victim.clipName = "Idle"; m.victim.animTime = lt;
      } else if (lt < 4.90) {
        const u = smooth((lt - 2.85) / 2.05);
        vx = lerp(m.x + 2.8, m.x + 0.12, u);
        m.victim.clipName = "Walk"; m.victim.animTime = lt * 1.5;
      } else if (!isDud && m.drownEast && lt < 5.88) {
        // ROUND 1 (Treasure East Pond): Live `bombex` explosion blasts Blue Worker straight into East Pond (`x=44.4`)!
        const u = clamp01((lt - 4.90) / 0.98);
        vx = lerp(m.x + 0.12, m.x + 5.75, Math.pow(u, 0.78));
        vy = Math.sin(u * Math.PI) * 0.72;
        m.victim.clipName = "BlownBack"; m.victim.animTime = Math.min(0.45, u * 0.45);
      } else if (!isDud && m.drownEast && lt < 8.25) {
        // SPLASH & DROWN: Flails frantically and sinks beneath the deep blue pond with expanding water ripples (`Drown`, 2.37s)!
        const drT = lt - 5.88;
        const drU = clamp01(drT / 2.37);
        vx = m.x + 5.75;
        vy = lerp(-0.04, -0.68, Math.pow(drU, 1.45));
        vyaw = -Math.PI * 0.5 + Math.sin(drT * 9.0) * 0.35;
        m.victim.submerged = true;
        m.victim.clipName = "Drown"; m.victim.animTime = Math.min(2.36, drT);
        ripples.push([vx, m.z, (drT * 0.9) % 1.0, lerp(1.05, 0.45, drU)]);
      } else if (!isDud && m.drownEast) {
        // Brief pause after sinking before resetting cleanly for next round
        showVictim = false;
      } else if (!isDud && lt < 5.95) {
        // ROUND 1 — LIVE BOMB: 1998 `bombex` explosion + single clean ballistic `BlownBack` arc!
        const u = clamp01((lt - 4.90) / 1.05);
        vx = lerp(m.x + 0.12, m.x + 3.85, Math.pow(clamp01(u / 0.68), 0.72));
        vy = u < 0.46 ? (Math.sin((u / 0.46) * Math.PI) * 0.42) : 0.0;
        m.victim.clipName = "BlownBack"; m.victim.animTime = Math.min(0.89, u * 0.89);
      } else if (isDud && lt < 6.08) {
        // ROUND 2 — DUD BOMB: Smoky dud fizzle + single vertical `BlownUp` pop toward camera!
        const u = clamp01((lt - 4.90) / 1.15);
        vx = m.x + 0.12;
        vz = m.z + (u < 0.60 ? Math.sin((u / 0.60) * Math.PI) * 0.62 : 0.0);
        vy = u < 0.56 ? (Math.sin((u / 0.56) * Math.PI) * 0.32) : 0.0;
        vyaw = lerp(-Math.PI * 0.5, 0.0, smooth(clamp01(u * 2.5)));
        m.victim.clipName = "BlownUp"; m.victim.animTime = Math.min(1.14, u * 1.14);
      } else if (lt < 8.15) {
        vx = isDud ? (m.x + 0.12) : (m.x + 3.85);
        m.victim.clipName = "Stunned"; m.victim.animTime = lt - (isDud ? 6.08 : 5.95);
      } else {
        const u = smooth((lt - 8.15) / 1.25);
        vx = lerp(isDud ? (m.x + 0.12) : (m.x + 3.85), m.x + 2.8, u);
        vyaw = isDud ? Math.PI * 0.5 : -Math.PI * 0.5;
        m.victim.clipName = "Walk"; m.victim.animTime = lt * 1.4;
      }

      if (showBomb) {
        m.bomb.idleTime = t;
        m.bomb.offsetMat = buildPropMat(m.bomb.model, m.x, 0, m.z, bombScale, bombScale, bombScale, 0.35);
        visible.push(m.bomb);
      }

      // Multi-stage 3D `bombex` Explosion (fireball core, serrated shockwave, sparks/debris, layered smoke & scorch)
      if (blastT >= 0) {
        updateBombExplosion(m.explosion, m.x, m.z, blastT, isDud, visible, dynGlows);
      }

      m.bomber.offsetMat = buildAntMat(m.bomber.model, bx, by, m.z, m.antScale, byaw);
      visible.push(m.bomber);
      if (showVictim) {
        m.victim.offsetMat = buildAntMat(m.victim.model, vx, vy, vz, m.antScale, vyaw);
        visible.push(m.victim);
      }
    }

    // --- 3. Combat Ant Power-Hit -> Single Clean BlownBack Flight + Stunned + Recovery ---
    for (const c of v.combats) {
      if (!inView(c.x, c.z)) continue;
      const dur = 5.6;
      const lt = patrolsOn ? ((t + c.phase) % dur + dur) % dur : 1.62;
      let ax = c.x - 0.7, vx = c.x + 0.55, vy = 0;
      const ayaw = Math.PI * 0.5, vyaw = -Math.PI * 0.5;

      if (lt < 1.0) {
        const u = smooth(lt / 1.0);
        ax = lerp(c.x - 1.5, c.x - 0.58, u);
        vx = lerp(c.x + 1.5, c.x + 0.52, u);
        c.attacker.clipName = "Walk"; c.attacker.animTime = lt * 1.5;
        c.victim.clipName = "Walk"; c.victim.animTime = lt * 1.5;
      } else if (lt < 1.54) {
        ax = c.x - 0.58; vx = c.x + 0.52;
        c.attacker.clipName = "Attack"; c.attacker.animTime = lt - 1.0;
        if (lt < 1.26) { c.victim.clipName = "Idle"; c.victim.animTime = lt; }
        else {
          const u = clamp01((lt - 1.26) / 0.90);
          vx = lerp(c.x + 0.52, c.x + 3.15, Math.pow(clamp01(u / 0.65), 0.72));
          vy = u < 0.46 ? (Math.sin((u / 0.46) * Math.PI) * 0.55) : 0.0;
          c.victim.clipName = "BlownBack"; c.victim.animTime = u * 0.89;
        }
      } else if (lt < 2.16) {
        // Victim finishes single ballistic flight & kips up cleanly (no double bounce!)
        const u = clamp01((lt - 1.26) / 0.90);
        ax = c.x - 0.58;
        vx = lerp(c.x + 0.52, c.x + 3.15, Math.pow(clamp01(u / 0.65), 0.72));
        vy = u < 0.46 ? (Math.sin((u / 0.46) * Math.PI) * 0.55) : 0.0;
        c.attacker.clipName = "Idle"; c.attacker.animTime = lt;
        c.victim.clipName = "BlownBack"; c.victim.animTime = Math.min(0.89, u * 0.89);
      } else if (lt < 4.10) {
        ax = c.x - 0.58; vx = c.x + 3.15;
        c.attacker.clipName = "Idle"; c.attacker.animTime = lt;
        c.victim.clipName = "Stunned"; c.victim.animTime = lt - 2.16;
      } else {
        const u = smooth((lt - 4.10) / 1.50);
        ax = lerp(c.x - 0.58, c.x - 1.5, u);
        vx = lerp(c.x + 3.15, c.x + 1.5, u);
        c.attacker.clipName = "Walk"; c.attacker.animTime = lt * 1.4;
        c.victim.clipName = "Walk"; c.victim.animTime = lt * 1.4;
      }

      c.attacker.offsetMat = buildAntMat(c.attacker.model, ax, 0, c.z, c.antScale, ayaw);
      c.victim.offsetMat = buildAntMat(c.victim.model, vx, vy, c.z, c.antScale, vyaw);
      visible.push(c.attacker, c.victim);
    }

    // --- 4. Seamless Fire Ant (`StartFire` 1:1 focal alignment under magnifying glass) + Walking RIGHT THROUGH the Flames! ---
    // Measured `fire_sunbeam` & `fire_spark` in `fire.glb`: focused at exact local `+0.546 * antScale` (`t_spark = 1.30..1.70 s`).
    for (const f of v.fires) {
      if (!inView(f.x, f.z)) continue;
      const dur = 13.8;
      const lt = patrolsOn ? ((t + f.phase) % dur + dur) % dur : 8.6;
      const focusOff = 0.546 * f.antScale; // Exact sunbeam focal point directly ahead of the Fire Ant!
      const flX = [f.x - 0.95, f.x, f.x + 0.95];
      const igX = [flX[0] - focusOff, flX[1] - focusOff, flX[2] - focusOff];
      let fx = igX[0] - 0.9, fyaw = Math.PI * 0.5;
      const fSc = [0, 0, 0];
      let bx = f.x + 2.8, by = 0, byaw = -Math.PI * 0.5;

      if (lt < 0.95) {
        const u = smooth(lt / 0.95);
        fx = lerp(igX[0] - 0.9, igX[0], u);
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.5;
      } else if (lt < 2.71) {
        // Ignites Flame #1 RIGHT ON THE EXACT SUNBEAM FOCAL POINT `flX[0]`!
        const dtF = lt - 0.95;
        fx = igX[0];
        f.fireAnt.clipName = "StartFire"; f.fireAnt.animTime = dtF;
        if (dtF > 1.30) fSc[0] = smooth((dtF - 1.30) / 0.36);
      } else if (lt < 3.36) {
        const u = smooth((lt - 2.71) / 0.65);
        fx = lerp(igX[0], igX[1], u); fSc[0] = 1;
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.5;
      } else if (lt < 5.12) {
        // Ignites Flame #2 RIGHT ON THE EXACT SUNBEAM FOCAL POINT `flX[1]`!
        const dtF = lt - 3.36;
        fx = igX[1]; fSc[0] = 1;
        f.fireAnt.clipName = "StartFire"; f.fireAnt.animTime = dtF;
        if (dtF > 1.30) fSc[1] = smooth((dtF - 1.30) / 0.36);
      } else if (lt < 5.77) {
        const u = smooth((lt - 5.12) / 0.65);
        fx = lerp(igX[1], igX[2], u); fSc[0] = 1; fSc[1] = 1;
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.5;
      } else if (lt < 7.53) {
        // Ignites Flame #3 RIGHT ON THE EXACT SUNBEAM FOCAL POINT `flX[2]`!
        const dtF = lt - 5.77;
        fx = igX[2]; fSc[0] = 1; fSc[1] = 1;
        f.fireAnt.clipName = "StartFire"; f.fireAnt.animTime = dtF;
        if (dtF > 1.30) fSc[2] = smooth((dtF - 1.30) / 0.36);
      } else if (lt < 11.55) {
        // FIRE-WALKING: Fire Ant struts fearlessly back and forth RIGHT ON TOP OF / THROUGH the roaring flames!
        fSc[0] = 1; fSc[1] = 1; fSc[2] = 1;
        const pU = (lt - 7.53) / 4.02;
        if (pU < 0.45) {
          fx = lerp(igX[2], flX[2] + 0.55, smooth(pU / 0.45)); fyaw = Math.PI * 0.5;
        } else {
          fx = lerp(flX[2] + 0.55, igX[0], smooth((pU - 0.45) / 0.55)); fyaw = -Math.PI * 0.5;
        }
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.65;
      } else if (lt < 12.75) {
        // Extinguishes flames cleanly (`Extinguish` 1.20 s)
        fx = igX[0]; fyaw = Math.PI * 0.5;
        f.fireAnt.clipName = "Extinguish"; f.fireAnt.animTime = lt - 11.55;
        const rem = 1.0 - smooth((lt - 11.75) / 0.85);
        fSc[0] = rem; fSc[1] = rem; fSc[2] = rem;
      } else {
        const u = smooth((lt - 12.75) / 1.05);
        fx = lerp(igX[0], igX[0] - 0.9, u); fyaw = -Math.PI * 0.5;
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.4;
      }

      // Bystander Green Worker wanders near the blaze, gets singed (`Hit` jump back!), and retreats!
      if (lt > 7.8 && lt < 9.1) {
        const u = smooth((lt - 7.8) / 1.3);
        bx = lerp(f.x + 3.2, f.x + 1.85, u);
        f.bystander.clipName = "Walk"; f.bystander.animTime = lt * 1.4;
      } else if (lt >= 9.1 && lt < 9.9) {
        const u = (lt - 9.1) / 0.80;
        bx = lerp(f.x + 1.85, f.x + 3.15, Math.pow(u, 0.68));
        by = u < 0.58 ? (Math.sin((u / 0.58) * Math.PI) * 0.42) : 0;
        f.bystander.clipName = "Hit"; f.bystander.animTime = lt - 9.1;
      } else {
        bx = f.x + 3.2; f.bystander.clipName = "Idle"; f.bystander.animTime = lt;
      }

      let fireGlow = 0;
      for (let i = 0; i < 3; i++) {
        if (fSc[i] > 0.03) {
          const sc = FIRE_SCALE * fSc[i];
          f.flames[i].idleTime = t + i * 0.43;
          f.flames[i].offsetMat = buildPropMat(f.flames[i].model, flX[i], 0, f.z, sc, sc, sc, 0);
          visible.push(f.flames[i]);
          fireGlow = Math.max(fireGlow, fSc[i]);
        }
      }
      if (fireGlow > 0.1) dynGlows.push([f.x, f.z, 4.6, fireGlow * 1.45, 1.9, 0.70, 0.16]);
      f.fireAnt.offsetMat = buildAntMat(f.fireAnt.model, fx, 0, f.z, f.antScale, fyaw);
      f.bystander.offsetMat = buildAntMat(f.bystander.model, bx, by, f.z, f.antScale, byaw);
      visible.push(f.fireAnt, f.bystander);
    }

    // --- 5. Regular Worker Ants Brawling 1v1 with Mutual Knockbacks (`Attack` + `Hit`) ---
    for (const w of v.brawls) {
      if (!inView(w.x, w.z)) continue;
      const dur = 7.2;
      const lt = patrolsOn ? ((t + w.phase) % dur + dur) % dur : 1.55;
      let ax = w.x - 0.52, ay = 0, bx = w.x + 0.52, by = 0;

      if (lt < 0.95) {
        const u = smooth(lt / 0.95);
        ax = lerp(w.x - 1.35, w.x - 0.46, u); bx = lerp(w.x + 1.35, w.x + 0.46, u);
        w.a.clipName = "Walk"; w.a.animTime = lt * 1.6; w.b.clipName = "Walk"; w.b.animTime = lt * 1.6;
      } else if (lt < 2.05) {
        // Blue Worker strikes -> Red Worker gets knocked back (`Hit` roll)!
        ax = w.x - 0.46;
        w.a.clipName = lt < 1.37 ? "Attack" : "Idle"; w.a.animTime = lt < 1.37 ? (lt - 0.95) : lt;
        if (lt < 1.18) { bx = w.x + 0.46; w.b.clipName = "Idle"; w.b.animTime = lt; }
        else {
          const u = clamp01((lt - 1.18) / 0.80);
          bx = lerp(w.x + 0.46, w.x + 1.55, Math.pow(u, 0.66));
          by = u < 0.55 ? (Math.sin((u / 0.55) * Math.PI) * 0.34) : 0;
          w.b.clipName = "Hit"; w.b.animTime = Math.min(0.79, lt - 1.18);
        }
      } else if (lt < 2.95) {
        // Red Worker charges back in!
        const u = smooth((lt - 2.05) / 0.90);
        ax = w.x - 0.46; bx = lerp(w.x + 1.55, w.x + 0.46, u);
        w.a.clipName = "Idle"; w.a.animTime = lt; w.b.clipName = "Walk"; w.b.animTime = lt * 1.6;
      } else if (lt < 4.05) {
        // Red Worker counter-strikes -> Blue Worker knocked back (`Hit` roll)!
        bx = w.x + 0.46;
        w.b.clipName = lt < 3.37 ? "Attack" : "Idle"; w.b.animTime = lt < 3.37 ? (lt - 2.95) : lt;
        if (lt < 3.18) { ax = w.x - 0.46; w.a.clipName = "Idle"; w.a.animTime = lt; }
        else {
          const u = clamp01((lt - 3.18) / 0.80);
          ax = lerp(w.x - 0.46, w.x - 1.55, Math.pow(u, 0.66));
          ay = u < 0.55 ? (Math.sin((u / 0.55) * Math.PI) * 0.34) : 0;
          w.a.clipName = "Hit"; w.a.animTime = Math.min(0.79, lt - 3.18);
        }
      } else if (lt < 5.05) {
        // Both charge in for a double clash!
        const u = smooth((lt - 4.05) / 1.00);
        ax = lerp(w.x - 1.55, w.x - 0.45, u); bx = w.x + 0.45;
        w.a.clipName = "Walk"; w.a.animTime = lt * 1.6; w.b.clipName = "Idle"; w.b.animTime = lt;
      } else if (lt < 6.20) {
        // Mutual clash -> BOTH bounce back simultaneously!
        if (lt < 5.32) {
          ax = w.x - 0.45; bx = w.x + 0.45;
          w.a.clipName = "Attack"; w.a.animTime = lt - 5.05;
          w.b.clipName = "Attack"; w.b.animTime = lt - 5.05;
        } else {
          const u = clamp01((lt - 5.32) / 0.80);
          ax = lerp(w.x - 0.45, w.x - 1.35, Math.pow(u, 0.68));
          bx = lerp(w.x + 0.45, w.x + 1.35, Math.pow(u, 0.68));
          const hop = u < 0.55 ? (Math.sin((u / 0.55) * Math.PI) * 0.30) : 0;
          ay = hop; by = hop;
          w.a.clipName = "Hit"; w.a.animTime = Math.min(0.79, lt - 5.32);
          w.b.clipName = "Hit"; w.b.animTime = Math.min(0.79, lt - 5.32);
        }
      } else {
        ax = w.x - 1.35; bx = w.x + 1.35;
        w.a.clipName = "Idle"; w.a.animTime = lt; w.b.clipName = "Idle"; w.b.animTime = lt;
      }

      w.a.offsetMat = buildAntMat(w.a.model, ax, ay, w.z, w.antScale, Math.PI * 0.5);
      w.b.offsetMat = buildAntMat(w.b.model, bx, by, w.z, w.antScale, -Math.PI * 0.5);
      visible.push(w.a, w.b);
    }

    // --- 6. Food-Depositing Worker Ants (Carry Lunchbox -> Climb Mound -> Enter Hole -> Emerge Empty-Handed!) ---
    const buildLunchMat = window.__demoHeist && window.__demoHeist.buildCarriedLunchboxMat;
    for (const d of v.deposits) {
      if (!inView(d.hx, d.hz)) continue;
      const dur = 9.6;
      const lt = patrolsOn ? ((t + d.phase) % dur + dur) % dur : ((3.38 + d.phase) % dur);
      const yawIn = Math.atan2(-d.ux, -d.uz);
      const yawOut = Math.atan2(d.ux, d.uz);
      const farR = d.moundR + 2.55;
      const baseR = d.moundR;
      let r = farR, y = 0, yaw = yawIn, showAnt = true, showBox = false, walking = false, animT = lt * 1.5;
      let boxDropY = 0, boxScaleMul = 1.0;

      if (lt < 1.80) {
        // March toward anthill carrying yellow lunchbox!
        const u = smooth(lt / 1.80);
        r = lerp(farR, baseR, u); y = 0; yaw = yawIn; showBox = true; walking = true;
        d.worker.clipName = "Walk"; d.worker.animTime = animT;
      } else if (lt < 3.05) {
        // Climb smoothly up the earthen anthill slope right to the crater rim still holding the lunchbox!
        const u = smooth((lt - 1.80) / 1.25);
        r = lerp(baseR, 0.16, u);
        y = Math.sin(u * Math.PI * 0.5) * d.rimY;
        yaw = yawIn; showBox = true; walking = true;
        d.worker.clipName = "Walk"; d.worker.animTime = animT;
      } else if (lt < 3.90) {
        // Authentic 1998 `Enter` (dive-in half 0.00 -> 0.50 s): tips head-first & plunges INTO the dark crater hole!
        const u = (lt - 3.05) / 0.85;
        r = lerp(0.16, 0.0, smooth(Math.min(1, u * 1.45)));
        y = d.rimY; // `Enter` clip natively sinks full-size ant below rimY down into the mound!
        yaw = yawIn; walking = false;
        d.worker.clipName = "Enter"; d.worker.animTime = lerp(0.0, 0.50, u);
        if (u < 0.42) {
          showBox = true;
          const bu = smooth(u / 0.42);
          boxDropY = -bu * 0.36;
          boxScaleMul = lerp(1.0, 0.35, bu);
        }
      } else if (lt < 4.85) {
        // Safely inside the anthill depositing food!
        showAnt = false; showBox = false;
      } else if (lt < 5.65) {
        // Authentic 1998 `Hatch` (0.00 -> 0.52 s): rises energetically OUT of the dark crater hole with NO lunchbox!
        const u = (lt - 4.85) / 0.80;
        r = lerp(0.0, 0.18, smooth(Math.max(0, (u - 0.35) / 0.65)));
        y = d.rimY; // `Hatch` clip natively rises from inside burrow up to planted feet on rim!
        yaw = yawOut; showBox = false; walking = false;
        d.worker.clipName = "Hatch"; d.worker.animTime = Math.min(0.519, u * 0.52);
      } else if (lt < 6.90) {
        // Climb briskly back DOWN the anthill slope EMPTY-HANDED!
        const u = smooth((lt - 5.65) / 1.25);
        r = lerp(0.18, baseR, u);
        y = Math.cos(u * Math.PI * 0.5) * d.rimY;
        yaw = yawOut; showBox = false; walking = true;
        d.worker.clipName = "Walk"; d.worker.animTime = animT;
      } else {
        // Trot happily back out across the dirt EMPTY-HANDED (no lunchbox!) to gather more food!
        const u = smooth((lt - 6.90) / 2.70);
        r = lerp(baseR, farR, u); y = 0; yaw = yawOut; showBox = false; walking = true;
        d.worker.clipName = "Walk"; d.worker.animTime = animT;
      }

      if (showAnt) {
        const ax = d.hx + d.ux * r, az = d.hz + d.uz * r;
        d.worker.offsetMat = buildAntMat(d.worker.model, ax, y, az, d.antScale, yaw);
        visible.push(d.worker);
        if (showBox && buildLunchMat) {
          d.lunchbox.offsetMat = buildLunchMat(d.lunchbox.model, ax, y + boxDropY, az, yaw, animT, walking, d.antScale * boxScaleMul, false);
          visible.push(d.lunchbox);
        }
      }
    }

    // --- 7. Authentic 1998 Groucho Daisy Power-Up Droppers on Small & Islands (`dflower1` + 820 ms `FD_*` drops!) ---
    for (const dr of v.droppers) {
      if (!inView(dr.fx, dr.fz)) continue;
      const slotDur = 4.2;
      const totalT = patrolsOn ? Math.max(0, t + dr.phase) : (1.12 + dr.phase);
      const slotIdx = Math.floor(totalT / slotDur);
      const slotT = totalT % slotDur;
      const fdId = dr.order[slotIdx % dr.order.length];
      const inst = dr.fdInsts[fdId];
      if (!inst) continue;

      if (slotT < 0.70) {
        // Groucho Disguise Daisy glances suspiciously left & right with mustache twitch right before dropping!
        if (dr.flowerInst) { dr.flowerInst.clipName = "Idle"; dr.flowerInst.animTime = slotT; }
      } else {
        if (dr.flowerInst) { dr.flowerInst.clipName = "Idle"; dr.flowerInst.animTime = 0.0; }
        let fdAnimT = 0.74, py = 0.0, sc = FD_DROP_SCALE, yaw = 0.0, glow = 0.9;
        if (slotT < 1.52) {
          // Authentic 820 ms 1998 golden nectar drip -> stretch -> fall -> splat -> 3D power-up gear reveal!
          const fdT = slotT - 0.70;
          fdAnimT = Math.min(0.76, fdT);
          py = lerp(1.65, 0.0, Math.pow(clamp01(fdT / 0.52), 1.35));
          glow = fdT < 0.52 ? lerp(0.45, 1.35, fdT / 0.52) : lerp(1.55, 0.95, (fdT - 0.52) / 0.30);
        } else if (slotT < 3.65) {
          // Landed random power-up sits on the ground tile (x, z+1) gently spinning & pulsing!
          const holdT = slotT - 1.52;
          fdAnimT = 0.74;
          py = 0.02 + Math.sin(holdT * 4.2) * 0.025;
          yaw = holdT * 1.35;
          sc = FD_DROP_SCALE * (1.0 + 0.03 * Math.sin(holdT * 4.8));
        } else {
          // Quick golden vanish before the daisy glances around and drops the next random type!
          const vu = smooth((slotT - 3.65) / 0.55);
          fdAnimT = 0.74;
          py = vu * 0.35;
          yaw = (slotT - 1.52) * 1.8;
          sc = FD_DROP_SCALE * (1.0 - vu * 0.94);
          glow = 0.9 * (1.0 - vu);
        }
        inst.clipName = "Idle";
        inst.animTime = fdAnimT;
        inst.offsetMat = buildPropMat(inst.model, dr.landX, py, dr.landZ, sc, sc, sc, yaw);
        visible.push(inst);
        dynGlows.push([dr.landX, dr.landZ, 2.3, glow, 1.75, 1.30, 0.30]);
      }
    }
  }

  window.__demoActions = {
    ACTION_ANT_CLASSES,
    ACTION_ITEM_IDS,
    buildActionVignettes,
    updateActionVignettes,
  };
})();

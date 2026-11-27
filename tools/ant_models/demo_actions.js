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

  const ACTION_ANT_CLASSES = ["worker", "combat", "bomber", "swimmer", "fire"];
  const DROPPER_FD_IDS = ["FD_BOMB", "FD_FIRE", "FD_SWIM", "FD_COMB", "FD_THIEF"];
  const ACTION_ITEM_IDS = ["wallup04", "redbomb", "bluebomb", "greenbomb", "blackbomb", "lunchbox", "dflower1", ...DROPPER_FD_IDS];
  const BOMB_SCALE = 0.72;
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
    const bridgeModel = { dispose() { bridgeStages.forEach((m) => m.dispose()); } };
    const v = { bridgeModel, bridgeStages, bridges: [], waterBridges: [], bombers: [], combats: [], fires: [], brawls: [], deposits: [], droppers: [] };
    const mid = mapData.id;

    // 1A. Land-Digging Swimmers (`asbbl` / `Shovel`): build 3-tile dark-mud bridges (`bridge1`..`bridge4`) from shore!
    const bridgeSpots = mid === "treasure"
      ? [[13.45, 28.5, "blue", 0.0], [27.45, 42.5, "green", 4.5]]
      : (mid === "islands"
        ? [[27.45, 29.5, "blue", 0.5]]
        : (mid === "small" ? [[15.45, 24.5, "blue", 0.2]] : []));
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
      : (mid === "islands"
        ? [[30.5, 30.5, "green", 0.9, +1]]
        : (mid === "small" ? [[20.0, 16.5, "green", 0.7, +1]] : []));
    for (const [px, pz, team, ph, dir] of waterDigSpots) {
      const sp = findWaterDigSpan(mapData, waterTile, px, pz);
      if (!sp || !models.swimmer) continue;
      // 4 contiguous water tiles: [x0, x0+1, x0+2, x0+3]
      v.waterBridges.push({
        x0: sp[0], sz: sp[1], dir, phase: ph, antScale,
        swimmer: makeAntInst("swimmer", team, models.swimmer, ph, true),
        tiles: [0, 1].map(() => makePropInst("bridge_mud_uw", bridgeStages[3], true)),
      });
    }

    // 2. Bomber placing sizzling Red Bomb -> running clear -> BOOM + BlownUp knockback
    if (models.bomber && models.worker && models.redbomb && models.wallup04 && (mid === "treasure" || mid === "popcorn" || mid === "small")) {
      const bx = mid === "treasure" ? 38.6 : (mid === "popcorn" ? 15.5 : 29.0);
      const bz = mid === "treasure" ? 29.0 : (mid === "popcorn" ? 12.2 : 16.5);
      v.bombers.push({
        x: bx, z: bz, phase: 1.0, antScale,
        bomber: makeAntInst("bomber", "red", models.bomber, 0.3),
        victim: makeAntInst("worker", "blue", models.worker, 0.7),
        bomb: makePropInst("redbomb", models.redbomb),
        blast: makePropInst("wallup04", models.wallup04),
      });
    }

    // 3. Combat Ant power-hitting Worker -> huge BlownBack launch + Stunned + Hit roll recovery
    if (models.combat && models.worker) {
      const cx = mid === "treasure" ? 30.2 : (mid === "islands" ? 23.0 : (mid === "small" ? 11.5 : (mapData.cols * 0.5 - 1.2)));
      const cz = mid === "treasure" ? 22.2 : (mid === "islands" ? 20.0 : (mid === "small" ? 15.0 : (mapData.rows * 0.5 - 2.2)));
      v.combats.push({
        x: cx, z: cz, phase: 0.4, antScale,
        attacker: makeAntInst("combat", "green", models.combat, 0.1),
        victim: makeAntInst("worker", "black", models.worker, 0.6),
      });
    }

    // 4. Fire Ant igniting a 3-tile Fire Wall with magnifying glass & walking right through the fire!
    if (models.fire && models.worker && models.wallup04) {
      const fx = mid === "treasure" ? 30.2 : (mid === "islands" ? 37.2 : (mid === "small" ? 28.0 : (mapData.cols * 0.5)));
      const fz = mid === "treasure" ? 36.0 : (mid === "islands" ? 33.0 : (mid === "small" ? 32.5 : (mapData.rows * 0.5 + 3.0)));
      v.fires.push({
        x: fx, z: fz, phase: 2.2, antScale,
        fireAnt: makeAntInst("fire", "red", models.fire, 0.2),
        bystander: makeAntInst("worker", "green", models.worker, 0.9),
        flames: [makePropInst("wallup04", models.wallup04), makePropInst("wallup04", models.wallup04), makePropInst("wallup04", models.wallup04)],
      });
    }

    // 5. Regular Worker Ants brawling 1v1 with mutual strikes & Hit knockback-roll recoveries
    if (models.worker) {
      const rx = mid === "treasure" ? 21.4 : (mid === "islands" ? 37.0 : (mid === "small" ? 11.0 : (mapData.cols * 0.5 + 2.8)));
      const rz = mid === "treasure" ? 29.0 : (mid === "islands" ? 21.5 : (mid === "small" ? 25.5 : (mapData.rows * 0.5)));
      v.brawls.push({
        x: rx, z: rz, phase: 0.8, antScale,
        a: makeAntInst("worker", "blue", models.worker, 0.2),
        b: makeAntInst("worker", "red", models.worker, 0.5),
      });
    }

    // 6. Food-Depositing Worker Ants: carry swinging lunchbox -> climb anthill -> Enter crater hole -> Hatch out empty-handed!
    if (models.worker && models.lunchbox && Array.isArray(itemEntries)) {
      const depSpecs = mid === "treasure"
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
        // Approach from the open front/inward side toward the map center (rotated slightly per paired worker)
        const baseAng = Math.atan2(mapData.rows * 0.5 - hz, mapData.cols * 0.5 - hx) + angOff;
        const ux = Math.cos(baseAng), uz = Math.sin(baseAng);
        v.deposits.push({
          team, phase: ph, antScale, hx, hz, rimY, moundR: 0.56 * s, ux, uz,
          worker: makeAntInst("worker", team, models.worker, ph),
          lunchbox: makePropInst("lunchbox", models.lunchbox),
        });
      }
    }

    // 7. Authentic 1998 Groucho Daisy Power-Up Droppers on Small & Islands (`dflower1` + 820 ms `FD_*` golden nectar drops!)
    if ((mid === "small" || mid === "islands") && Array.isArray(itemEntries)) {
      const fdPool = DROPPER_FD_IDS.filter((id) => Boolean(models[id]));
      const flowers = itemEntries.filter((e) => e.spec.id === "flower1");
      if (fdPool.length && flowers.length) {
        const orders = [
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

    // --- 1A. Land-Digging Swimmers (`Shovel` / `asbbl`): 4-Stage Dark-Mud Bridges Built from Shore ---
    const mudH = 0.064;
    for (const b of v.bridges) {
      if (!inView(b.sx + 1.5, b.sz)) continue;
      const dur = 11.4;
      const lt = patrolsOn ? ((t + b.phase) % dur + dur) % dur : 4.85;
      let ax = b.sx, ay = 0, az = b.sz, yaw = Math.PI * 0.5;
      const prog = [0, 0, 0];
      if (lt < 1.0) {
        const u = smooth(lt / 1.0);
        ax = lerp(b.sx - 0.85, b.sx, u);
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 3.0) {
        const u = (lt - 1.0) / 2.0;
        b.swimmer.clipName = "Shovel"; b.swimmer.animTime = lt;
        prog[0] = clamp01((u - 0.06) / 0.88);
        if (ripples.length < 8) ripples.push([b.sx + 1.0, b.sz, b.phase, 0.85]);
      } else if (lt < 3.7) {
        const u = smooth((lt - 3.0) / 0.7);
        ax = lerp(b.sx, b.sx + 1.0, u); ay = lerp(0.0, mudH, u); prog[0] = 1;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 5.7) {
        const u = (lt - 3.7) / 2.0;
        ax = b.sx + 1.0; ay = mudH; prog[0] = 1; prog[1] = clamp01((u - 0.06) / 0.88);
        b.swimmer.clipName = "Shovel"; b.swimmer.animTime = lt;
        if (ripples.length < 8) ripples.push([b.sx + 2.0, b.sz, b.phase + 0.3, 0.85]);
      } else if (lt < 6.4) {
        const u = smooth((lt - 5.7) / 0.7);
        ax = lerp(b.sx + 1.0, b.sx + 2.0, u); ay = mudH; prog[0] = 1; prog[1] = 1;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 8.4) {
        const u = (lt - 6.4) / 2.0;
        ax = b.sx + 2.0; ay = mudH; prog[0] = 1; prog[1] = 1; prog[2] = clamp01((u - 0.06) / 0.88);
        b.swimmer.clipName = "Shovel"; b.swimmer.animTime = lt;
        if (ripples.length < 8) ripples.push([b.sx + 3.0, b.sz, b.phase + 0.6, 0.85]);
      } else if (lt < 10.2) {
        const u = smooth((lt - 8.4) / 1.4);
        ax = lerp(b.sx + 2.0, b.sx + 3.0, u); ay = mudH; prog[0] = 1; prog[1] = 1; prog[2] = 1;
        b.swimmer.clipName = u < 0.95 ? "Walk" : "Idle"; b.swimmer.animTime = lt * 1.4;
      } else {
        const u = smooth((lt - 10.2) / 1.2);
        ax = lerp(b.sx + 3.0, b.sx - 0.85, u); yaw = -Math.PI * 0.5;
        ay = lerp(mudH, 0.0, clamp01(u * 1.4));
        const fade = 1 - smooth(clamp01((u - 0.35) / 0.65));
        prog[0] = fade; prog[1] = fade; prog[2] = fade;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.8;
      }
      for (let i = 0; i < 3; i++) pushMudBridgeTile(b.tiles[i], v.bridgeStages, prog[i], b.sx + 1.0 + i, b.sz, i);
      b.swimmer.offsetMat = buildAntMat(b.swimmer.model, ax, ay, az, b.antScale, yaw);
      visible.push(b.swimmer);
    }

    // --- 1B. Underwater-Digging Swimmers (`Swim` + `ShovelWater` / `asbbw`): Digging Dark-Mud Bridges from INSIDE the Water! ---
    for (const uwb of v.waterBridges) {
      if (!inView(uwb.x0 + 1.5, uwb.sz)) continue;
      const dur = 10.2;
      const lt = patrolsOn ? ((t + uwb.phase) % dur + dur) % dur : 3.25;
      const d = uwb.dir; // +1 faces East, -1 faces West
      // 4 water cells along x0..x0+3: dig tile 0 & tile 1 while submerged in adjacent water tile!
      const t0x = d > 0 ? (uwb.x0 + 2.0) : (uwb.x0 + 1.0);
      const t1x = d > 0 ? (uwb.x0 + 1.0) : (uwb.x0 + 2.0);
      const dig0x = t0x - d * 1.12; // submerged dig stance beside tile 0 (clear of mud rim!)
      const dig1x = t1x - d * 1.12; // submerged dig stance beside tile 1 (still in pure water!)
      const startX = dig0x - d * 0.72;
      const subSwimY = -1.05 * uwb.antScale;
      const subDigY = -0.46 * uwb.antScale; // chest-deep underwater: goggles, yellow snorkel, swinging yellow shovel & aqua spray burst out!
      let ax = dig0x, ay = subSwimY, yaw = d > 0 ? Math.PI * 0.5 : -Math.PI * 0.5;
      const prog = [0, 0];

      if (lt < 1.35) {
        // Swim smoothly underwater toward bridge spot 1 (yellow snorkel + water ripples!)
        const u = smooth(lt / 1.35);
        ax = lerp(startX, dig0x, u);
        ay = lerp(subSwimY, subDigY, smooth(clamp01((u - 0.65) / 0.35))) + Math.sin(lt * 5.0) * 0.025;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.4;
      } else if (lt < 3.55) {
        // Dig mud bridge tile #1 right from UNDERWATER (`ShovelWater` / `asbbw` + splashing water spray!)
        const u = (lt - 1.35) / 2.20;
        ax = dig0x; ay = subDigY + Math.sin(lt * 6.5) * 0.02;
        uwb.swimmer.clipName = "ShovelWater"; uwb.swimmer.animTime = lt;
        prog[0] = clamp01((u - 0.06) / 0.88);
        if (ripples.length < 8) ripples.push([t0x, uwb.sz, uwb.phase + 0.1, 0.92]);
      } else if (lt < 4.80) {
        // Swim back 1 water tile underwater (`Swim`) to extend the bridge from the water!
        const u = smooth((lt - 3.55) / 1.25);
        ax = lerp(dig0x, dig1x, u);
        ay = lerp(subDigY, subSwimY, Math.sin(u * Math.PI)) + Math.sin(lt * 5.0) * 0.025;
        yaw = d > 0 ? -Math.PI * 0.5 : Math.PI * 0.5;
        prog[0] = 1;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.4;
      } else if (lt < 7.00) {
        // Dig mud bridge tile #2 right from UNDERWATER (`ShovelWater`!)
        const u = (lt - 4.80) / 2.20;
        ax = dig1x; ay = subDigY + Math.sin(lt * 6.5) * 0.02;
        uwb.swimmer.clipName = "ShovelWater"; uwb.swimmer.animTime = lt;
        prog[0] = 1; prog[1] = clamp01((u - 0.06) / 0.88);
        if (ripples.length < 8) ripples.push([t1x, uwb.sz, uwb.phase + 0.4, 0.92]);
      } else if (lt < 8.80) {
        // Tread / swim playfully underwater beside his finished 2-tile dark-mud bridge!
        const u = smooth((lt - 7.00) / 1.80);
        ax = lerp(dig1x, startX, u);
        ay = lerp(subDigY, subSwimY, clamp01(u * 2.2)) + Math.sin(lt * 5.0) * 0.028;
        prog[0] = 1; prog[1] = 1;
        uwb.swimmer.clipName = "Swim"; uwb.swimmer.animTime = lt * 1.3;
      } else {
        const u = smooth((lt - 8.80) / 1.40);
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

    // --- 2. Bomber Planting Sizzling Bomb -> Detonation & BlownUp Knockback ---
    for (const m of v.bombers) {
      if (!inView(m.x, m.z)) continue;
      const dur = 9.4;
      const lt = patrolsOn ? ((t + m.phase) % dur + dur) % dur : 5.45;
      let bx = m.x - 1.1, by = 0, byaw = Math.PI * 0.5;
      let vx = m.x + 2.6, vy = 0, vyaw = -Math.PI * 0.5;
      let showBomb = false, bombScale = BOMB_SCALE, showBlast = false, blastScale = 1.0;

      if (lt < 1.3) {
        const u = smooth(lt / 1.3);
        bx = lerp(m.x - 2.9, m.x - 0.95, u);
        m.bomber.clipName = "Walk"; m.bomber.animTime = lt * 1.5;
      } else if (lt < 2.66) {
        bx = m.x - 0.95;
        m.bomber.clipName = "PlantBomb"; m.bomber.animTime = lt - 1.3;
        if (lt > 2.05) { showBomb = true; bombScale = BOMB_SCALE * smooth((lt - 2.05) / 0.35); }
      } else if (lt < 4.95) {
        const u = smooth((lt - 2.66) / 1.65);
        bx = lerp(m.x - 0.95, m.x - 3.1, u); byaw = u < 0.95 ? -Math.PI * 0.5 : Math.PI * 0.5;
        m.bomber.clipName = u < 0.95 ? "Walk" : "Idle"; m.bomber.animTime = lt * 1.8;
        showBomb = true;
        // Rapid tension pulse right before detonation!
        const pulse = lt > 4.25 ? (1.0 + 0.16 * Math.sin((lt - 4.25) * 34.0)) : 1.0;
        bombScale = BOMB_SCALE * pulse;
        dynGlows.push([m.x, m.z, 2.2, lt > 4.25 ? 1.5 : 0.75, 1.8, 0.72, 0.16]);
      } else {
        bx = m.x - 3.1; byaw = Math.PI * 0.5;
        m.bomber.clipName = "Idle"; m.bomber.animTime = lt;
      }

      // Victim Blue Worker approaches bomb -> gets launched sky-high by explosion -> Stunned!
      if (lt < 2.8) {
        vx = m.x + 2.8; m.victim.clipName = "Idle"; m.victim.animTime = lt;
      } else if (lt < 4.95) {
        const u = smooth((lt - 2.8) / 1.75);
        vx = lerp(m.x + 2.8, m.x + 0.78, u);
        m.victim.clipName = u < 0.94 ? "Walk" : "Idle"; m.victim.animTime = lt * 1.4;
      } else if (lt < 6.10) {
        // EXPLOSION + DRAMATIC BLOWNUP PARABOLIC LAUNCH!
        const u = (lt - 4.95) / 1.15;
        vx = lerp(m.x + 0.78, m.x + 3.45, Math.pow(u, 0.72));
        vy = u < 0.76 ? (Math.sin((u / 0.76) * Math.PI) * 1.55) : (Math.sin(((u - 0.76) / 0.24) * Math.PI) * 0.26);
        m.victim.clipName = "BlownUp"; m.victim.animTime = lt - 4.95;
        if (u < 0.72) {
          showBlast = true;
          blastScale = FIRE_SCALE * (u < 0.18 ? lerp(0.5, 1.55, u / 0.18) : lerp(1.55, 0.15, (u - 0.18) / 0.54));
          dynGlows.push([m.x, m.z, 5.2, (1.0 - u / 0.72) * 3.0, 2.2, 0.86, 0.22]);
        }
      } else if (lt < 8.15) {
        vx = m.x + 3.45;
        m.victim.clipName = "Stunned"; m.victim.animTime = lt - 6.10;
      } else {
        const u = smooth((lt - 8.15) / 1.25);
        vx = lerp(m.x + 3.45, m.x + 2.8, u);
        m.victim.clipName = "Walk"; m.victim.animTime = lt * 1.4;
      }

      if (showBomb) {
        m.bomb.idleTime = t;
        m.bomb.offsetMat = buildPropMat(m.bomb.model, m.x, 0, m.z, bombScale, bombScale, bombScale, 0.35);
        visible.push(m.bomb);
      }
      if (showBlast) {
        m.blast.idleTime = t * 1.8;
        m.blast.offsetMat = buildPropMat(m.blast.model, m.x, 0, m.z, blastScale, blastScale * 1.25, blastScale, 0);
        visible.push(m.blast);
      }
      m.bomber.offsetMat = buildAntMat(m.bomber.model, bx, by, m.z, m.antScale, byaw);
      m.victim.offsetMat = buildAntMat(m.victim.model, vx, vy, m.z, m.antScale, vyaw);
      visible.push(m.bomber, m.victim);
    }

    // --- 3. Combat Ant Hitting Worker -> Huge BlownBack Launch + Stunned + Hit Roll ---
    for (const c of v.combats) {
      if (!inView(c.x, c.z)) continue;
      const dur = 7.4;
      const lt = patrolsOn ? ((t + c.phase) % dur + dur) % dur : 1.72;
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
          const u = (lt - 1.26) / 0.90;
          vx = lerp(c.x + 0.52, c.x + 3.15, Math.pow(u, 0.68));
          vy = Math.sin(clamp01(u / 0.72) * Math.PI) * 0.95;
          c.victim.clipName = "BlownBack"; c.victim.animTime = lt - 1.26;
        }
      } else if (lt < 2.16) {
        // Victim finishes flying backward & bounces onto dirt!
        const u = (lt - 1.26) / 0.90;
        ax = c.x - 0.58;
        vx = lerp(c.x + 0.52, c.x + 3.15, Math.pow(clamp01(u), 0.68));
        vy = u < 0.72 ? (Math.sin((u / 0.72) * Math.PI) * 0.95) : (Math.sin(((u - 0.72) / 0.28) * Math.PI) * 0.22);
        c.attacker.clipName = "Idle"; c.attacker.animTime = lt;
        c.victim.clipName = "BlownBack"; c.victim.animTime = Math.min(0.89, lt - 1.26);
      } else if (lt < 3.65) {
        ax = c.x - 0.58; vx = c.x + 3.15;
        c.attacker.clipName = "Idle"; c.attacker.animTime = lt;
        c.victim.clipName = "Stunned"; c.victim.animTime = lt - 2.16;
      } else if (lt < 4.95) {
        // Worker charges angrily back in for round 2!
        const u = smooth((lt - 3.65) / 1.30);
        ax = c.x - 0.58; vx = lerp(c.x + 3.15, c.x + 0.52, u);
        c.attacker.clipName = "Idle"; c.attacker.animTime = lt;
        c.victim.clipName = "Walk"; c.victim.animTime = lt * 1.7;
      } else if (lt < 5.50) {
        ax = c.x - 0.58; vx = c.x + 0.52;
        c.attacker.clipName = "Attack"; c.attacker.animTime = lt - 4.95;
        if (lt < 5.20) { c.victim.clipName = "Attack"; c.victim.animTime = lt - 4.95; }
        else {
          const u = (lt - 5.20) / 0.80;
          vx = lerp(c.x + 0.52, c.x + 1.85, Math.pow(clamp01(u), 0.65));
          vy = Math.sin(clamp01(u / 0.55) * Math.PI) * 0.36;
          c.victim.clipName = "Hit"; c.victim.animTime = lt - 5.20;
        }
      } else if (lt < 6.00) {
        // Classic 1998 `Hit` knockback-onto-back + snappy kip-up roll recovery!
        const u = (lt - 5.20) / 0.80;
        ax = c.x - 0.58;
        vx = lerp(c.x + 0.52, c.x + 1.85, Math.pow(clamp01(u), 0.65));
        vy = u < 0.55 ? (Math.sin((u / 0.55) * Math.PI) * 0.36) : 0;
        c.attacker.clipName = "Idle"; c.attacker.animTime = lt;
        c.victim.clipName = "Hit"; c.victim.animTime = Math.min(0.79, lt - 5.20);
      } else {
        const u = smooth((lt - 6.00) / 1.40);
        ax = lerp(c.x - 0.58, c.x - 1.5, u);
        vx = lerp(c.x + 1.85, c.x + 1.5, u);
        c.attacker.clipName = "Walk"; c.attacker.animTime = lt * 1.4;
        c.victim.clipName = "Walk"; c.victim.animTime = lt * 1.4;
      }

      c.attacker.offsetMat = buildAntMat(c.attacker.model, ax, 0, c.z, c.antScale, ayaw);
      c.victim.offsetMat = buildAntMat(c.victim.model, vx, vy, c.z, c.antScale, vyaw);
      visible.push(c.attacker, c.victim);
    }

    // --- 4. Fire Ant Igniting a 3-Tile Fire Wall & Fearlessly Walking RIGHT THROUGH the Flames! ---
    for (const f of v.fires) {
      if (!inView(f.x, f.z)) continue;
      const dur = 11.6;
      const lt = patrolsOn ? ((t + f.phase) % dur + dur) % dur : 7.4;
      let fx = f.x - 1.8, fyaw = Math.PI * 0.5;
      const fSc = [0, 0, 0];
      let bx = f.x + 2.8, by = 0, byaw = -Math.PI * 0.5;

      if (lt < 1.0) {
        const u = smooth(lt / 1.0);
        fx = lerp(f.x - 2.6, f.x - 1.75, u);
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.5;
      } else if (lt < 2.76) {
        // Pulls out brass magnifying glass (`StartFire`) to ignite Flame #1!
        fx = f.x - 1.75;
        f.fireAnt.clipName = "StartFire"; f.fireAnt.animTime = lt - 1.0;
        if (lt > 2.05) fSc[0] = smooth((lt - 2.05) / 0.55);
      } else if (lt < 3.45) {
        const u = smooth((lt - 2.76) / 0.69);
        fx = lerp(f.x - 1.75, f.x - 0.75, u); fSc[0] = 1;
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.5;
      } else if (lt < 5.21) {
        // Ignites Flame #2 & Flame #3 into a blazing 3-tile fire line!
        fx = f.x - 0.75; fSc[0] = 1;
        f.fireAnt.clipName = "StartFire"; f.fireAnt.animTime = lt - 3.45;
        if (lt > 4.20) fSc[1] = smooth((lt - 4.20) / 0.55);
        if (lt > 4.55) fSc[2] = smooth((lt - 4.55) / 0.55);
      } else if (lt < 9.40) {
        // FIRE-WALKING: Fire Ant struts back and forth RIGHT ON TOP OF / THROUGH the roaring flames!
        fSc[0] = 1; fSc[1] = 1; fSc[2] = 1;
        const pU = (lt - 5.21) / 4.19;
        if (pU < 0.50) {
          fx = lerp(f.x - 1.05, f.x + 1.55, smooth(pU / 0.50)); fyaw = Math.PI * 0.5;
        } else {
          fx = lerp(f.x + 1.55, f.x - 1.75, smooth((pU - 0.50) / 0.50)); fyaw = -Math.PI * 0.5;
        }
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.65;
      } else if (lt < 10.60) {
        // Extinguishes flames smoothly (`Extinguish` 1.20 s)
        fx = f.x - 1.75; fyaw = Math.PI * 0.5;
        f.fireAnt.clipName = "Extinguish"; f.fireAnt.animTime = lt - 9.40;
        const rem = 1.0 - smooth((lt - 9.60) / 0.85);
        fSc[0] = rem; fSc[1] = rem; fSc[2] = rem;
      } else {
        const u = smooth((lt - 10.60) / 1.0);
        fx = lerp(f.x - 1.75, f.x - 2.6, u); fyaw = -Math.PI * 0.5;
        f.fireAnt.clipName = "Walk"; f.fireAnt.animTime = lt * 1.4;
      }

      // Bystander Green Worker wanders near the blaze, gets singed (`Hit` jump back!), and retreats!
      if (lt > 5.8 && lt < 7.1) {
        const u = smooth((lt - 5.8) / 1.3);
        bx = lerp(f.x + 3.2, f.x + 1.85, u);
        f.bystander.clipName = "Walk"; f.bystander.animTime = lt * 1.4;
      } else if (lt >= 7.1 && lt < 7.9) {
        const u = (lt - 7.1) / 0.80;
        bx = lerp(f.x + 1.85, f.x + 3.15, Math.pow(u, 0.68));
        by = u < 0.58 ? (Math.sin((u / 0.58) * Math.PI) * 0.42) : 0;
        f.bystander.clipName = "Hit"; f.bystander.animTime = lt - 7.1;
      } else {
        bx = f.x + 3.2; f.bystander.clipName = "Idle"; f.bystander.animTime = lt;
      }

      let fireGlow = 0;
      for (let i = 0; i < 3; i++) {
        if (fSc[i] > 0.03) {
          const sc = FIRE_SCALE * fSc[i];
          f.flames[i].idleTime = t + i * 0.43;
          f.flames[i].offsetMat = buildPropMat(f.flames[i].model, f.x - 0.95 + i * 0.95, 0, f.z, sc, sc, sc, 0);
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
        const bob = walking ? Math.abs(Math.sin(animT * 5.2)) * 0.022 : 0;
        d.worker.offsetMat = buildAntMat(d.worker.model, ax, y + bob, az, d.antScale, yaw);
        visible.push(d.worker);
        if (showBox && buildLunchMat) {
          d.lunchbox.offsetMat = buildLunchMat(d.lunchbox.model, ax, y + bob + boxDropY, az, yaw, animT, walking, d.antScale * boxScaleMul, false);
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

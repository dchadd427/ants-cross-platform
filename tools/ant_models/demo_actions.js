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
  const ACTION_ITEM_IDS = ["wallup04", "redbomb", "bluebomb", "greenbomb", "blackbomb"];
  const BOMB_SCALE = 0.72;
  const FIRE_SCALE = 1.06;

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

  // Procedural 3D raised packed-earth dirt bridge tile (warm backyard clay top + damp dark mud underwater skirt).
  // Renders cleanly through PBR_VS/PBR_FS (`submerged: true` so skirt extends below Y=0 while top rises solidly above water!).
  function buildDirtBridgeModel(gl) {
    const pos = [], nrm = [], uv = [], col = [], idx = [];
    const rings = [
      { y: -0.45, rx: 0.64, rz: 0.64, c: [0.06, 0.05, 0.05] }, // submerged muddy base
      { y: +0.01, rx: 0.59, rz: 0.59, c: [0.11, 0.09, 0.08] }, // wet waterline mud rim
      { y: +0.13, rx: 0.51, rz: 0.51, c: [0.24, 0.17, 0.14] }, // packed backyard dirt slope
      { y: +0.17, rx: 0.26, rz: 0.26, c: [0.30, 0.21, 0.17] }, // crowned warm backyard clay top
    ];
    const segs = 16;
    for (let r = 0; r < rings.length; r++) {
      const rg = rings[r];
      for (let s = 0; s < segs; s++) {
        const a = (s / segs) * Math.PI * 2;
        const ca = Math.cos(a), sa = Math.sin(a);
        const sq = 1.0 / Math.pow(Math.pow(Math.abs(ca), 3.6) + Math.pow(Math.abs(sa), 3.6), 1.0 / 3.6);
        const lump = 1.0 + 0.06 * Math.sin(a * 5.0 + r * 1.4) + 0.04 * Math.cos(a * 7.0);
        const x = ca * sq * rg.rx * lump;
        const z = sa * sq * rg.rz * lump;
        const y = rg.y + (r >= 2 ? 0.014 * Math.sin(a * 3.0 + r) : 0);
        const nx = (r <= 1 ? ca * 0.85 : ca * 0.32), ny = (r <= 1 ? 0.52 : 0.95), nz = (r <= 1 ? sa * 0.85 : sa * 0.32);
        const nL = Math.hypot(nx, ny, nz) || 1;
        const shade = 0.90 + 0.16 * Math.sin(a * 4.0 + r * 2.1);
        pos.push(x, y, z);
        nrm.push(nx / nL, ny / nL, nz / nL);
        uv.push(x + 0.5, z + 0.5);
        col.push(rg.c[0] * shade, rg.c[1] * shade, rg.c[2] * shade);
      }
    }
    const centerIdx = rings.length * segs;
    pos.push(0, 0.18, 0); nrm.push(0, 1, 0); uv.push(0.5, 0.5); col.push(0.32, 0.23, 0.18);

    // Outward CCW winding (verified against +Y normal when angle advances CW in XZ!)
    for (let r = 0; r < rings.length - 1; r++) {
      for (let s = 0; s < segs; s++) {
        const ns = (s + 1) % segs;
        const a = r * segs + s, b = r * segs + ns, c = (r + 1) * segs + s, d = (r + 1) * segs + ns;
        idx.push(a, d, b, a, c, d);
      }
    }
    const topRing = (rings.length - 1) * segs;
    for (let s = 0; s < segs; s++) {
      idx.push(topRing + s, centerIdx, topRing + ((s + 1) % segs));
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
      bounds: { min: [-0.66, -0.45, -0.66], max: [0.66, 0.19, 0.66] },
      size: [1.32, 0.64, 1.32],
      drawCalls: [{
        nodeIndex: 0, worldMat: idMat,
        prim: {
          vao, indexed: true, mode: gl.TRIANGLES, drawCount: idx.length, indexType: gl.UNSIGNED_SHORT,
          hasUv: false, localMin: [-0.66, -0.45, -0.66], localMax: [0.66, 0.19, 0.66],
          material: {
            name: "item_dirt_bridge", baseColor: [1, 1, 1, 1], metallic: 0.0, roughness: 0.90,
            normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
            emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: true,
          },
        },
      }],
      dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
  }

  function makeAntInst(cls, team, model, phase) {
    return {
      kind: "ant", cls, submerged: false, teamColors: TEAMS[team] || TEAMS.green,
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

  function buildActionVignettes(gl, mapData, models, waterTile, antScale) {
    const bridgeModel = buildDirtBridgeModel(gl);
    const v = { bridgeModel, bridges: [], bombers: [], combats: [], fires: [], brawls: [] };
    const mid = mapData.id;

    // 1. Swimmers digging 3-tile dirt bridges into ponds / channels
    const bridgeSpots = mid === "treasure"
      ? [[13.45, 29.5, "blue", 0.0], [27.45, 42.5, "green", 4.5]]
      : (mid === "islands"
        ? [[27.45, 29.5, "blue", 0.5], [27.45, 19.5, "green", 5.2]]
        : []);
    for (const [px, pz, team, ph] of bridgeSpots) {
      const sh = findBridgeShore(mapData, waterTile, px, pz);
      if (!sh || !models.swimmer) continue;
      v.bridges.push({
        sx: sh[0], sz: sh[1], phase: ph, antScale,
        swimmer: makeAntInst("swimmer", team, models.swimmer, ph),
        tiles: [makePropInst("bridge_dirt", bridgeModel, true), makePropInst("bridge_dirt", bridgeModel, true), makePropInst("bridge_dirt", bridgeModel, true)],
      });
    }

    // 2. Bomber placing sizzling Red Bomb -> running clear -> BOOM + BlownUp knockback
    if (models.bomber && models.worker && models.redbomb && models.wallup04 && (mid === "treasure" || mid === "popcorn" || mid === "small")) {
      const bx = mid === "treasure" ? 38.6 : (mid === "popcorn" ? 15.5 : 22.5);
      const bz = mid === "treasure" ? 29.0 : (mid === "popcorn" ? 12.2 : 17.5);
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
      const cx = mid === "treasure" ? 30.2 : (mid === "islands" ? 23.0 : (mapData.cols * 0.5 - 1.2));
      const cz = mid === "treasure" ? 22.2 : (mid === "islands" ? 20.0 : (mapData.rows * 0.5 - 2.2));
      v.combats.push({
        x: cx, z: cz, phase: 0.4, antScale,
        attacker: makeAntInst("combat", "green", models.combat, 0.1),
        victim: makeAntInst("worker", "black", models.worker, 0.6),
      });
    }

    // 4. Fire Ant igniting a 3-tile Fire Wall with magnifying glass & walking right through the fire!
    if (models.fire && models.worker && models.wallup04) {
      const fx = mid === "treasure" ? 30.2 : (mid === "islands" ? 37.2 : (mapData.cols * 0.5));
      const fz = mid === "treasure" ? 36.0 : (mid === "islands" ? 33.0 : (mapData.rows * 0.5 + 3.0));
      v.fires.push({
        x: fx, z: fz, phase: 2.2, antScale,
        fireAnt: makeAntInst("fire", "red", models.fire, 0.2),
        bystander: makeAntInst("worker", "green", models.worker, 0.9),
        flames: [makePropInst("wallup04", models.wallup04), makePropInst("wallup04", models.wallup04), makePropInst("wallup04", models.wallup04)],
      });
    }

    // 5. Regular Worker Ants brawling 1v1 with mutual strikes & Hit knockback-roll recoveries
    if (models.worker) {
      const rx = mid === "treasure" ? 21.4 : (mid === "islands" ? 37.0 : (mapData.cols * 0.5 + 2.8));
      const rz = mid === "treasure" ? 29.0 : (mid === "islands" ? 21.5 : (mapData.rows * 0.5));
      v.brawls.push({
        x: rx, z: rz, phase: 0.8, antScale,
        a: makeAntInst("worker", "blue", models.worker, 0.2),
        b: makeAntInst("worker", "red", models.worker, 0.5),
      });
    }

    return v;
  }

  function updateActionVignettes(v, t, patrolsOn, tx, tz, visHalfSpan, visible, ripples, dynGlows) {
    if (!v) return;
    const inView = (x, z) => Math.abs(x - tx) <= visHalfSpan + 4.0 && Math.abs(z - tz) <= visHalfSpan + 4.0;

    // --- 1. Swimmers Digging 3-Tile Dirt Bridges ---
    for (const b of v.bridges) {
      if (!inView(b.sx + 1.5, b.sz)) continue;
      const dur = 11.4;
      const lt = patrolsOn ? ((t + b.phase) % dur + dur) % dur : 4.8;
      let ax = b.sx, ay = 0, az = b.sz, yaw = Math.PI * 0.5;
      const rise = [0, 0, 0];
      if (lt < 1.0) {
        const u = smooth(lt / 1.0);
        ax = lerp(b.sx - 0.9, b.sx, u);
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 3.0) {
        const u = (lt - 1.0) / 2.0;
        b.swimmer.clipName = u < 0.45 ? "Shovel" : "ShovelWater"; b.swimmer.animTime = lt;
        rise[0] = smooth(clamp01((u - 0.12) / 0.82));
        if (ripples.length < 8) ripples.push([b.sx + 1.0, b.sz, b.phase, 0.85]);
      } else if (lt < 3.7) {
        const u = smooth((lt - 3.0) / 0.7);
        ax = lerp(b.sx, b.sx + 1.0, u); ay = lerp(0.0, 0.16, u); rise[0] = 1;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 5.7) {
        const u = (lt - 3.7) / 2.0;
        ax = b.sx + 1.0; ay = 0.16; rise[0] = 1; rise[1] = smooth(clamp01((u - 0.12) / 0.82));
        b.swimmer.clipName = u < 0.45 ? "Shovel" : "ShovelWater"; b.swimmer.animTime = lt;
        if (ripples.length < 8) ripples.push([b.sx + 2.0, b.sz, b.phase + 0.3, 0.85]);
      } else if (lt < 6.4) {
        const u = smooth((lt - 5.7) / 0.7);
        ax = lerp(b.sx + 1.0, b.sx + 2.0, u); ay = 0.16; rise[0] = 1; rise[1] = 1;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.4;
      } else if (lt < 8.4) {
        const u = (lt - 6.4) / 2.0;
        ax = b.sx + 2.0; ay = 0.16; rise[0] = 1; rise[1] = 1; rise[2] = smooth(clamp01((u - 0.12) / 0.82));
        b.swimmer.clipName = u < 0.45 ? "Shovel" : "ShovelWater"; b.swimmer.animTime = lt;
        if (ripples.length < 8) ripples.push([b.sx + 3.0, b.sz, b.phase + 0.6, 0.85]);
      } else if (lt < 10.2) {
        const u = smooth((lt - 8.4) / 1.4);
        ax = lerp(b.sx + 2.0, b.sx + 3.0, u); ay = 0.16; rise[0] = 1; rise[1] = 1; rise[2] = 1;
        b.swimmer.clipName = u < 0.95 ? "Walk" : "Idle"; b.swimmer.animTime = lt * 1.4;
      } else {
        const u = smooth((lt - 10.2) / 1.2);
        ax = lerp(b.sx + 3.0, b.sx - 0.9, u); yaw = -Math.PI * 0.5;
        ay = lerp(0.16, 0.0, clamp01(u * 1.4));
        const fade = 1 - smooth(clamp01((u - 0.35) / 0.65));
        rise[0] = fade; rise[1] = fade; rise[2] = fade;
        b.swimmer.clipName = "Walk"; b.swimmer.animTime = lt * 1.8;
      }
      for (let i = 0; i < 3; i++) {
        if (rise[i] > 0.02) {
          const rY = lerp(-0.32, 0.0, rise[i]);
          const sc = lerp(0.55, 1.02, rise[i]);
          b.tiles[i].offsetMat = mat4FromTRS([b.sx + 1.0 + i, rY, b.sz], quatY(i * 0.9), [sc, 1.0, sc]);
          visible.push(b.tiles[i]);
        }
      }
      b.swimmer.offsetMat = buildAntMat(b.swimmer.model, ax, ay, az, b.antScale, yaw);
      visible.push(b.swimmer);
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
  }

  window.__demoActions = {
    ACTION_ANT_CLASSES,
    ACTION_ITEM_IDS,
    buildActionVignettes,
    updateActionVignettes,
  };
})();

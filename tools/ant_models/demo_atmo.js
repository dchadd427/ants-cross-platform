"use strict";

// Single-draw-call backyard 3D ground micro-clutter (pebbles, fallen dried leaf chips,
// twig splinters, tiny grass sprouts, border meadow wildflowers) + GPU floating atmosphere
// particles (sunlit golden pollen / dandelion fluff in Daylight/Sunset, blinking fireflies at Night).
(() => {
  function hash01(seed) {
    let s = (seed >>> 0) + 0x6D2B79F5;
    s = Math.imul(s ^ (s >>> 15), 1 | s);
    s = (s + Math.imul(s ^ (s >>> 7), 61 | s)) ^ s;
    return ((s ^ (s >>> 14)) >>> 0) / 4294967296;
  }

  function makeVao(gl, arrays, indices, useUint32) {
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const bufs = [];
    for (const [loc, size, data] of arrays) {
      const b = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, b);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(loc);
      gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0);
      bufs.push(b);
    }
    const ib = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, useUint32 ? new Uint32Array(indices) : new Uint16Array(indices), gl.STATIC_DRAW);
    bufs.push(ib);
    gl.bindVertexArray(null);
    return { vao, count: indices.length, dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); } };
  }

  // Build 1 static GPU VAO of lived-in ant-scale backyard ground micro-detail (~8k-13k tris, 1 draw call).
  // Uses BORDER_GRASS_VS/FS (`a_pos`, `a_nrm`, `a_col(r,g,b,sway)`): pebbles/twigs/leaves have sway=0, sprouts/blossoms sway gently.
  function buildGroundScatterMesh(gl, mapData, waterTile) {
    const cols = mapData.cols, rows = mapData.rows;
    const tiles = mapData.tiles || [];
    const hills = Object.values(mapData.hills || {});
    let seed = (cols * 2654435761) ^ (rows * 2246822519) ^ 0xA7C31B;
    const nextR = () => hash01(++seed);

    const pos = [], nrm = [], col = [], idx = [];
    let vCount = 0;

    function isSafeLand(x, z) {
      if (x < 0.35 || z < 0.35 || x > cols - 0.35 || z > rows - 0.35) return false;
      const c = Math.floor(x), r = Math.floor(z);
      for (let dr = -1; dr <= 1; dr++) {
        for (let dc = -1; dc <= 1; dc++) {
          const nc = Math.max(0, Math.min(cols - 1, c + dc));
          const nr = Math.max(0, Math.min(rows - 1, r + dr));
          if (tiles[nr * cols + nc] === waterTile) return false;
        }
      }
      for (let i = 0; i < hills.length; i++) {
        if (Math.hypot(x - hills[i][0], z - hills[i][1]) < 2.8) return false;
      }
      return true;
    }

    // Tiny 3-blade stray grass sprout poking naturally near outer grassy backyard edges (6 tris)
    function addSprout(cx, cz, hScale) {
      for (let k = 0; k < 3; k++) {
        const yaw = (k / 3) * Math.PI * 2 + nextR() * 1.1;
        const cy = Math.cos(yaw), sy = Math.sin(yaw);
        const h = (0.14 + nextR() * 0.12) * hScale;
        const w = 0.036 + nextR() * 0.020;
        const lean = 0.34 + nextR() * 0.30;
        const sh = 0.78 + nextR() * 0.30;
        const baseV = vCount;
        // Root pair -> mid pair -> tip = 3 tris
        pos.push(cx - cy * w * 0.5, 0.0, cz + sy * w * 0.5); nrm.push(sy * 0.5, 0.85, cy * 0.5); col.push(0.035 * sh, 0.19 * sh, 0.012 * sh, 0.0);
        pos.push(cx + cy * w * 0.5, 0.0, cz - sy * w * 0.5); nrm.push(sy * 0.5, 0.85, cy * 0.5); col.push(0.035 * sh, 0.19 * sh, 0.012 * sh, 0.0);
        const mx = cx + sy * lean * h * 0.45, mz = cz + cy * lean * h * 0.45;
        pos.push(mx - cy * w * 0.34, h * 0.55, mz + sy * w * 0.34); nrm.push(sy * 0.4, 0.90, cy * 0.4); col.push(0.08 * sh, 0.38 * sh, 0.02 * sh, 0.55);
        pos.push(mx + cy * w * 0.34, h * 0.55, mz - sy * w * 0.34); nrm.push(sy * 0.4, 0.90, cy * 0.4); col.push(0.08 * sh, 0.38 * sh, 0.02 * sh, 0.55);
        pos.push(cx + sy * lean * h, h * 0.92, cz + cy * lean * h); nrm.push(0, 1, 0); col.push(0.14 * sh, 0.52 * sh, 0.03 * sh, 1.0);
        idx.push(baseV, baseV + 1, baseV + 3, baseV, baseV + 3, baseV + 2, baseV + 2, baseV + 3, baseV + 4);
        vCount += 5;
      }
    }

    // Tiny wildflower / clover blossom speck nestled in the outer grass border meadow (2 crossed diamond petals = 4 tris)
    function addBlossom(cx, cz, yH, rad, r, g, b) {
      const baseV = vCount;
      const yaw = nextR() * Math.PI;
      const cy = Math.cos(yaw), sy = Math.sin(yaw);
      const pts = [[0, 0, -rad], [rad, 0, 0], [0, 0, rad], [-rad, 0, 0]];
      for (let i = 0; i < 4; i++) {
        const lx = pts[i][0], lz = pts[i][2];
        pos.push(cx + lx * cy - lz * sy, yH, cz + lx * sy + lz * cy);
        nrm.push(0, 1, 0);
        col.push(r, g, b, 0.72);
        vCount++;
      }
      pos.push(cx, yH + rad * 0.28, cz);
      nrm.push(0, 1, 0);
      col.push(0.86, 0.62, 0.06, 0.75); // warm golden pollen dot center
      vCount++;
      idx.push(
        baseV + 0, baseV + 1, baseV + 4,
        baseV + 1, baseV + 2, baseV + 4,
        baseV + 2, baseV + 3, baseV + 4,
        baseV + 3, baseV + 0, baseV + 4,
      );
    }

    // Only scatter subtle pebbles on plain dirt tiles (`g01*` = 54..58) so white sand & mud stay spotless!
    function isPlainDirt(x, z) {
      if (!isSafeLand(x, z)) return false;
      const ti = tiles[Math.floor(z) * cols + Math.floor(x)];
      return ti >= 54 && ti <= 58;
    }

    const area = cols * rows;
    // 1. Delicate stray grass sprouts hugging only the outer grassy clearing edges (keeps dirt & sand 100% clean — zero dark chips!)
    const sproutCount = Math.min(120, Math.round(area * 0.036));
    for (let i = 0; i < sproutCount; i++) {
      const x = nextR() * cols, z = nextR() * rows;
      if (!isPlainDirt(x, z)) continue;
      const edgeD = Math.min(Math.min(x, cols - x), Math.min(z, rows - z));
      if (edgeD > 2.6) continue;
      addSprout(x, z, 0.85 + nextR() * 0.35);
    }

    // 4. Tiny white clover & buttercup-yellow wildflowers dotting the outer grass border meadow
    const flowerCount = Math.min(240, Math.round((cols + rows) * 2.0));
    for (let i = 0; i < flowerCount; i++) {
      const side = Math.floor(nextR() * 4);
      const off = 1.1 + nextR() * 7.2;
      let x, z;
      if (side === 0) { x = -3 + nextR() * (cols + 6); z = -off; }
      else if (side === 1) { x = -3 + nextR() * (cols + 6); z = rows + off + 0.4; }
      else if (side === 2) { x = -off; z = -2 + nextR() * (rows + 4); }
      else { x = cols + off; z = -2 + nextR() * (rows + 4); }
      const isYellow = nextR() < 0.38;
      const rad = 0.075 + nextR() * 0.065;
      const yH = 0.55 + nextR() * 0.52;
      addBlossom(x, z, yH, rad, isYellow ? 0.92 : 0.94, isYellow ? 0.74 : 0.93, isYellow ? 0.10 : 0.84);
    }

    return makeVao(gl, [[0, 3, new Float32Array(pos)], [1, 3, new Float32Array(nrm)], [2, 4, new Float32Array(col)]], idx, true);
  }

  // Build 1 single GPU VAO of ~260 floating 3D backyard atmosphere particles (pollen motes / dandelion fluff / night fireflies).
  function buildAtmoParticles(gl, mapData) {
    const cols = mapData.cols, rows = mapData.rows;
    let seed = (cols * 1597334677) ^ (rows * 3812015801) ^ 0xF1EA42;
    const nextR = () => hash01(++seed);
    const count = 260;
    const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
    const cArr = [], oArr = [], sArr = [], idx = [];

    for (let i = 0; i < count; i++) {
      const ox = -5.0 + nextR() * (cols + 10.0);
      const oy = 0.38 + Math.pow(nextR(), 1.25) * 3.8;
      const oz = -5.0 + nextR() * (rows + 10.0);
      const sz = 0.055 + nextR() * 0.095;
      const phase = nextR() * Math.PI * 2;
      const speed = 0.35 + nextR() * 0.65;
      const driftR = 0.45 + nextR() * 1.15;
      const kind = nextR(); // 0..1 distinguishes soft pollen vs bright firefly / dandelion seed
      const baseV = i * 4;
      for (let k = 0; k < 4; k++) {
        cArr.push(corners[k][0], corners[k][1]);
        oArr.push(ox, oy, oz, sz);
        sArr.push(phase, speed, driftR, kind);
      }
      idx.push(baseV, baseV + 1, baseV + 2, baseV, baseV + 2, baseV + 3);
    }
    return makeVao(gl, [[0, 2, new Float32Array(cArr)], [1, 4, new Float32Array(oArr)], [2, 4, new Float32Array(sArr)]], idx, false);
  }

  // Build realistic miniature backyard colony ant model (head + pinched thorax + teardrop gaster + 6 legs + antennae, optional golden crumb).
  function buildTinyColonyAntModel(gl, withCrumb) {
    const { mat4Identity } = window.__antGltf;
    const pos = [], nrm = [], uv = [], col = [], idx = [];
    function pushEllipsoid(cx, cy, cz, rx, ry, rz, rgb) {
      const base = pos.length / 3;
      const uS = 8, vS = 6;
      for (let iv = 0; iv <= vS; iv++) {
        const phi = (iv / vS) * Math.PI;
        const sp = Math.sin(phi), cp = Math.cos(phi);
        for (let iu = 0; iu <= uS; iu++) {
          const th = (iu / uS) * Math.PI * 2;
          const nx = sp * Math.cos(th), ny = cp, nz = sp * Math.sin(th);
          pos.push(cx + nx * rx, cy + ny * ry, cz + nz * rz);
          nrm.push(nx, ny, nz);
          uv.push(iu / uS, iv / vS);
          col.push(rgb[0], rgb[1], rgb[2]);
        }
      }
      const stride = uS + 1;
      for (let iv = 0; iv < vS; iv++) {
        for (let iu = 0; iu < uS; iu++) {
          const a = base + iv * stride + iu, b = a + 1, c = a + stride, d = c + 1;
          idx.push(a, c, b, b, c, d);
        }
      }
    }
    function pushStick(p0, p1, rad, rgb) {
      const base = pos.length / 3;
      const dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
      const len = Math.hypot(dx, dy, dz) || 1;
      const tx = dx / len, ty = dy / len, tz = dz / len;
      let ux = -tz, uy = 0, uz = tx;
      const uL = Math.hypot(ux, uy, uz);
      if (uL < 0.01) { ux = 1; uy = 0; uz = 0; } else { ux /= uL; uz /= uL; }
      const vx = ty * uz - tz * uy, vy = tz * ux - tx * uz, vz = tx * uy - ty * ux;
      const segs = 4;
      for (let s = 0; s < segs; s++) {
        const a = (s / segs) * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        const nx = ux * ca + vx * sa, ny = uy * ca + vy * sa, nz = uz * ca + vz * sa;
        pos.push(p0[0] + nx * rad, p0[1] + ny * rad, p0[2] + nz * rad);
        pos.push(p1[0] + nx * rad * 0.72, p1[1] + ny * rad * 0.72, p1[2] + nz * rad * 0.72);
        nrm.push(nx, ny, nz, nx, ny, nz);
        uv.push(0, 0, 1, 1);
        col.push(rgb[0], rgb[1], rgb[2], rgb[0], rgb[1], rgb[2]);
      }
      for (let s = 0; s < segs; s++) {
        const ns = (s + 1) % segs;
        const a = base + s * 2, b = a + 1, c = base + ns * 2, d = c + 1;
        idx.push(a, c, b, b, c, d);
      }
    }
    const bodyCol = [0.032, 0.019, 0.015];
    const darkLeg = [0.022, 0.014, 0.011];
    // Facing +Z: head (+0.068), slender thorax (0.0), plump gaster (-0.078)
    pushEllipsoid(0.0, 0.032, +0.066, 0.024, 0.020, 0.028, bodyCol);
    pushEllipsoid(0.0, 0.028, +0.004, 0.016, 0.015, 0.034, bodyCol);
    pushEllipsoid(0.0, 0.036, -0.072, 0.030, 0.026, 0.044, bodyCol);
    // 2 elbowed antennae + 6 real ant legs splayed gracefully to ground (y=0)
    for (const side of [-1, 1]) {
      pushStick([side * 0.012, 0.038, +0.084], [side * 0.030, 0.052, +0.118], 0.0045, darkLeg);
      pushStick([side * 0.014, 0.026, +0.026], [side * 0.058, 0.002, +0.062], 0.0050, darkLeg);
      pushStick([side * 0.014, 0.026, +0.000], [side * 0.066, 0.002, -0.004], 0.0050, darkLeg);
      pushStick([side * 0.014, 0.026, -0.026], [side * 0.058, 0.002, -0.068], 0.0050, darkLeg);
    }
    if (withCrumb) {
      pushEllipsoid(0.0, 0.036, +0.104, 0.022, 0.018, 0.020, [0.92, 0.68, 0.14]);
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
      bounds: { min: [-0.07, 0, -0.12], max: [0.07, 0.065, 0.13] },
      size: [0.14, 0.065, 0.25],
      drawCalls: [{
        nodeIndex: 0, worldMat: idMat,
        prim: {
          vao, indexed: true, mode: gl.TRIANGLES, drawCount: idx.length, indexType: gl.UNSIGNED_SHORT,
          hasUv: false, localMin: [-0.07, 0, -0.12], localMax: [0.07, 0.065, 0.13],
          material: {
            name: withCrumb ? "colony_ant_crumb" : "colony_ant", baseColor: [1, 1, 1, 1],
            metallic: 0.08, roughness: 0.42, normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
            emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: false,
          },
        },
      }],
      dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
  }

  // Exact normalized radial profile matching `_build_anthill_base` (items_structures.py) so tiny colony ants hug every mound slope!
  function hillWobble(ang) {
    const lobe = 0.165 * Math.exp(-Math.pow((ang - 0.31) / 0.55, 2));
    const neTrim = -0.085 * Math.exp(-Math.pow((ang + 0.78) / 0.60, 2));
    return 1.0 + lobe + neTrim;
  }

  function hillHeightNorm(rNorm, ang = 0) {
    const rEff = rNorm / hillWobble(ang);
    const stations = [
      [0.04, 0.14], [0.118, 0.42], [0.150, 0.436], [0.255, 0.370],
      [0.345, 0.245], [0.435, 0.125], [0.515, 0.036], [0.565, 0.0],
    ];
    if (rEff <= stations[0][0]) return stations[0][1];
    for (let i = 0; i < stations.length - 1; i++) {
      const [r0, y0] = stations[i], [r1, y1] = stations[i + 1];
      if (rEff <= r1) return y0 + (y1 - y0) * ((rEff - r0) / (r1 - r0));
    }
    return 0.0;
  }

  function quatFromYawPitch(yaw, pitch) {
    const cy = Math.cos(yaw * 0.5), sy = Math.sin(yaw * 0.5);
    const cp = Math.cos(pitch * 0.5), sp = Math.sin(pitch * 0.5);
    return [cy * sp, sy * cp, -sy * sp, cy * cp];
  }

  function buildColonyAntsForMap(gl, itemEntries) {
    const HILL_IDS = new Set(["GREENHILL", "REDHILL", "BLUEHILL", "BLACKHILL"]);
    const hills = itemEntries.filter((e) => HILL_IDS.has(e.spec.id));
    if (!hills.length) return null;
    const modelEmpty = buildTinyColonyAntModel(gl, false);
    const modelCrumb = buildTinyColonyAntModel(gl, true);
    const ants = [];
    for (let hIdx = 0; hIdx < hills.length; hIdx++) {
      const h = hills[hIdx];
      const hs = h.spec.s || 3.88;
      const m = h.inst && h.inst.model;
      let rx = 0, rz = 0;
      if (m && Array.isArray(m.nodes)) {
        const rootN = m.nodes.find((n) => n && n.name === "anthill");
        if (rootN && rootN.translation) { rx = rootN.translation[0]; rz = rootN.translation[2]; }
      }
      const cx = h.x + rx * hs, cz = h.z + rz * hs;
      // 7 tiny bustling colony ants per anthill (4 carrying golden crumbs into the hole, 3 emerging out to forage!)
      for (let k = 0; k < 7; k++) {
        const inbound = k < 4;
        const baseAng = (k / 7) * Math.PI * 2 + hIdx * 0.92 + (inbound ? 0.18 : 0.62);
        const dur = 5.4 + ((k * 1.17 + hIdx * 0.83) % 3.2);
        const phase = ((k * 0.31 + hIdx * 0.23) % 1.0) * dur;
        const bend = ((k % 2 === 0) ? 1 : -1) * (0.28 + (k % 3) * 0.14);
        const sc = 0.92 + (k % 3) * 0.12;
        ants.push({
          hx: cx, hz: cz, hs, inbound, baseAng, dur, phase, bend, sc,
          inst: {
            kind: "item", id: `colony_ant_${hIdx}_${k}`, isGrass: true,
            model: inbound ? modelCrumb : modelEmpty,
            clipName: "Idle", idleTime: 0, offsetMat: null,
          },
        });
      }
    }
    return {
      ants,
      dispose() { modelEmpty.dispose(); modelCrumb.dispose(); },
    };
  }

  function updateColonyAnts(colony, t, patrolsOn, tx, tz, visHalfSpan, visible) {
    if (!colony) return;
    const { mat4FromTRS } = window.__antGltf;
    for (let i = 0; i < colony.ants.length; i++) {
      const a = colony.ants[i];
      if (Math.abs(a.hx - tx) > visHalfSpan + 2.5 || Math.abs(a.hz - tz) > visHalfSpan + 2.5) continue;
      const lt = patrolsOn ? ((t + a.phase) % a.dur + a.dur) % a.dur : (a.phase % a.dur);
      const u = lt / a.dur;
      // Hide briefly while down inside the underground burrow tunnel!
      if (a.inbound ? (u > 0.84) : (u < 0.16)) continue;
      const frac = a.inbound ? (u / 0.84) : ((u - 0.16) / 0.84);
      // Radial crawl from outer skirt (r=0.58) up mound slope -> over crater lip -> down into central hole (r=0.05)
      const r0 = a.inbound ? 0.58 : 0.055;
      const r1 = a.inbound ? 0.055 : 0.58;
      const rN = r0 + (r1 - r0) * frac;
      const rNext = r0 + (r1 - r0) * Math.min(1.0, frac + 0.025);
      const ang = a.baseAng + Math.sin(frac * Math.PI) * a.bend + Math.sin(t * 9.0 + i * 2.1) * 0.018;
      const angNext = a.baseAng + Math.sin(Math.min(1.0, frac + 0.025) * Math.PI) * a.bend;
      const px = a.hx + Math.cos(ang) * rN * a.hs;
      const pz = a.hz + Math.sin(ang) * rN * a.hs;
      const py = hillHeightNorm(rN, ang) * a.hs + 0.018;
      const nx = a.hx + Math.cos(angNext) * rNext * a.hs;
      const nz = a.hz + Math.sin(angNext) * rNext * a.hs;
      const ny = hillHeightNorm(rNext, angNext) * a.hs + 0.018;
      const dx = nx - px, dz = nz - pz, dy = ny - py;
      const horiz = Math.hypot(dx, dz) || 1e-4;
      const yaw = Math.atan2(dx, dz);
      const pitch = -Math.atan2(dy, horiz);
      // Soft scale fade right at outer skirt & deep inside burrow throat so ants emerge/enter naturally!
      const edgeFade = Math.min(1.0, Math.min(frac / 0.10, (1.0 - frac) / 0.10));
      const sc = a.sc * edgeFade;
      if (sc < 0.08) continue;
      a.inst.offsetMat = mat4FromTRS([px, py, pz], quatFromYawPitch(yaw, pitch), [sc, sc, sc]);
      visible.push(a.inst);
    }
  }

  // Crisp 2-layer pulsing ground glow halo ring for map power-ups (`pu_*`):
  // Layer 1: subtle dark grounding shadow ring (contrast on bright grass & sand)
  // Layer 2: smooth luminous pulsing color-coded energy ring + faint inner glow pool (pops cleanly without clutter!)
  const POWERUP_RING_COLORS = {
    pu_mason: [1.35, 0.86, 0.14], // Fire Ant: warm golden-amber
    pu_swim:  [0.16, 1.05, 1.48], // Swimmer: bright aqua-cyan
    pu_bomb:  [1.45, 0.42, 0.16], // Bomber: vivid ember-orange
    pu_comb:  [1.42, 1.12, 0.22], // Combat: royal gold
    pu_thief: [0.95, 0.44, 1.48], // Thief: crisp electric violet
  };

  function buildPowerupRingModel(gl, id, rgb) {
    const { mat4Identity } = window.__antGltf;
    const idMat = mat4Identity();
    const bufs = [];
    function buildAnnulus(rIn, rOut, yH, baseCol, emissive, alpha, name) {
      const pos = [], nrm = [], uv = [], col = [], idx = [];
      const segs = 32;
      for (let s = 0; s <= segs; s++) {
        const a = (s / segs) * Math.PI * 2;
        const ca = Math.cos(a), sa = Math.sin(a);
        pos.push(ca * rIn, yH, sa * rIn, ca * rOut, yH, sa * rOut);
        nrm.push(0, 1, 0, 0, 1, 0);
        uv.push(s / segs, 0, s / segs, 1);
        col.push(1, 1, 1, 1, 1, 1);
      }
      for (let s = 0; s < segs; s++) {
        const a = s * 2, b = a + 1, c = a + 2, d = a + 3;
        idx.push(a, c, b, b, c, d);
      }
      const vao = gl.createVertexArray();
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
      return {
        vao,
        dc: {
          nodeIndex: 0, worldMat: idMat,
          prim: {
            vao, indexed: true, mode: gl.TRIANGLES, drawCount: idx.length, indexType: gl.UNSIGNED_SHORT,
            hasUv: false, localMin: [-rOut, yH, -rOut], localMax: [rOut, yH + 0.01, rOut],
            material: {
              name, baseColor: [...baseCol, alpha], metallic: 0.0, roughness: 0.96,
              normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
              emissive, alphaMode: alpha < 0.99 ? "BLEND" : "OPAQUE", doubleSided: true,
            },
          },
        },
      };
    }
    const baseRim = buildAnnulus(0.02, 0.41, 0.014, [0.03, 0.03, 0.04], [0, 0, 0], 0.22, `pu_ring_base_${id}`);
    const glowBand = buildAnnulus(0.36, 0.45, 0.028, [rgb[0] * 0.58, rgb[1] * 0.58, rgb[2] * 0.58], rgb, 0.80, `pu_ring_glow_${id}`);
    const outerFeather = buildAnnulus(0.45, 0.55, 0.022, [rgb[0] * 0.42, rgb[1] * 0.42, rgb[2] * 0.42], [rgb[0] * 0.65, rgb[1] * 0.65, rgb[2] * 0.65], 0.22, `pu_ring_halo_${id}`);
    return {
      animations: [], idleAnims: [], blinkInfo: { hasBlink: false },
      baseWorldMats: [idMat],
      bounds: { min: [-0.56, 0.01, -0.56], max: [0.56, 0.04, 0.56] },
      size: [1.12, 0.03, 1.12],
      drawCalls: [baseRim.dc, outerFeather.dc, glowBand.dc],
      dispose() {
        gl.deleteVertexArray(baseRim.vao); gl.deleteVertexArray(glowBand.vao); gl.deleteVertexArray(outerFeather.vao);
        bufs.forEach((b) => gl.deleteBuffer(b));
      },
    };
  }

  function buildPowerupGlowRingsForMap(gl, itemEntries) {
    const models = new Map();
    const rings = [];
    for (let i = 0; i < itemEntries.length; i++) {
      const e = itemEntries[i];
      if (!e.spec.id.startsWith("pu_")) continue;
      const id = e.spec.id;
      if (!models.has(id)) {
        models.set(id, buildPowerupRingModel(gl, id, POWERUP_RING_COLORS[id] || [1.35, 0.92, 0.20]));
      }
      rings.push({
        x: e.x, z: e.z, phase: (i * 0.71) % 6.283,
        inst: {
          kind: "item", id: `pu_ring_${i}`, isGrass: true,
          model: models.get(id), clipName: "Idle", idleTime: 0, offsetMat: null,
        },
      });
    }
    if (!rings.length) return null;
    return {
      rings,
      dispose() { models.forEach((m) => m.dispose()); },
    };
  }

  function updatePowerupGlowRings(puRings, t, patrolsOn, tx, tz, visHalfSpan, visible) {
    if (!puRings) return;
    const { mat4FromTRS } = window.__antGltf;
    for (let i = 0; i < puRings.rings.length; i++) {
      const r = puRings.rings[i];
      if (Math.abs(r.x - tx) > visHalfSpan + 1.5 || Math.abs(r.z - tz) > visHalfSpan + 1.5) continue;
      const pulse = patrolsOn ? Math.sin(t * 2.8 + r.phase) : 0.35;
      const sc = 0.94 + 0.12 * pulse;
      const yaw = patrolsOn ? (-t * 0.65 + r.phase) : r.phase;
      const h = yaw * 0.5;
      r.inst.offsetMat = mat4FromTRS([r.x, 0.0, r.z], [0, Math.sin(h), 0, Math.cos(h)], [sc, 1.0, sc]);
      visible.push(r.inst);
    }
  }

  window.__demoAtmo = {
    buildGroundScatterMesh,
    buildAtmoParticles,
    buildColonyAntsForMap,
    updateColonyAnts,
    buildPowerupGlowRingsForMap,
    updatePowerupGlowRings,
  };
})();

"use strict";

// Single-draw-call batched 3D backyard grass perimeter VAO wrapping all 4 outer edges & corners.
// ~115x fewer triangles and 1 draw call (vs ~560 draw calls / ~3.9M tris), zero shadow pass, GPU wind sway!
(() => {
  function hash01(seed) {
    let s = (seed >>> 0) + 0x6D2B79F5;
    s = Math.imul(s ^ (s >>> 15), 1 | s);
    s = (s + Math.imul(s ^ (s >>> 7), 61 | s)) ^ s;
    return ((s ^ (s >>> 14)) >>> 0) / 4294967296;
  }

  function isEdgeWater(mapData, waterTile, x, z) {
    if (!mapData || !mapData.tiles || waterTile === undefined) return false;
    const c = Math.max(0, Math.min(mapData.cols - 1, Math.floor(x)));
    const r = Math.max(0, Math.min(mapData.rows - 1, Math.floor(z)));
    return mapData.tiles[r * mapData.cols + c] === waterTile;
  }

  // Build one batched GPU mesh of ~4,000-6,800 curved, tapered 3D grass blades (5 tris/blade) around [0..cols, 0..rows].
  function buildBorderGrassMesh(gl, mapData, waterTile) {
    const cols = mapData.cols, rows = mapData.rows;
    let seed = (cols * 73856093) ^ (rows * 19349663) ^ 0x51A7F3;
    const nextR = () => hash01(++seed);

    const pos = [], nrm = [], col = [], idx = [];
    let vCount = 0;

    // Exact linear chlorophyll palette matched to `item_grass_blade` (baseColorFactor linear + root-to-tip tint)
    const baseR = 0.106, baseG = 0.610, baseB = 0.020;

    function pushBlade(bx, bz, height, width, yaw, lean, shade, hueG) {
      const cy = Math.cos(yaw), sy = Math.sin(yaw);
      const nx0 = sy, nz0 = cy;
      // 3 height rings + pointed apex tip = 7 verts, 5 triangles!
      const levels = [
        [0.00, 1.00, 0.00],
        [0.36, 0.80, 0.20],
        [0.72, 0.44, 0.56],
      ];
      const baseV = vCount;
      for (let i = 0; i < 3; i++) {
        const [u, wF, lF] = levels[i];
        const horiz = lean * height * lF;
        const vy = height * u * (1.0 - 0.16 * lF * lF);
        const cx = bx + sy * horiz, cz = bz + cy * horiz;
        const hw = width * 0.5 * wF;
        // Root-to-tip linear chlorophyll gradient (deep shaded forest root -> rich emerald -> sunlit chartreuse tip)
        const rc = baseR * (0.26 + 0.72 * u) * shade;
        const gc = baseG * (0.34 + 0.66 * Math.pow(u, 0.78)) * shade * hueG;
        const bc = baseB * (0.28 + 0.62 * u) * shade;
        // Subtle normal tilt along blade arch so directional sunlight models each tuft cleanly
        const ny = 0.55 + 0.35 * u;
        const nLen = Math.hypot(nx0 * 0.65, ny, nz0 * 0.65) || 1;
        const nx = (nx0 * 0.65) / nLen, nyn = ny / nLen, nz = (nz0 * 0.65) / nLen;
        for (const side of [-1, 1]) {
          pos.push(cx + cy * (side * hw), vy, cz - sy * (side * hw));
          nrm.push(nx, nyn, nz);
          col.push(rc, gc, bc, u);
          vCount++;
        }
      }
      // Pointed blade tip vertex
      const tipH = lean * height;
      const tipY = height * 0.86;
      const tr = baseR * 1.06 * shade, tg = baseG * 1.04 * shade * hueG, tb = baseB * 0.96 * shade;
      pos.push(bx + sy * tipH, tipY, bz + cy * tipH);
      nrm.push(sy * 0.35, 0.88, cy * 0.35);
      col.push(tr, tg, tb, 1.0);
      vCount++;

      // 2 quads (4 tris) + tip triangle (1 tri) = 5 tris total
      idx.push(
        baseV + 0, baseV + 1, baseV + 3,  baseV + 0, baseV + 3, baseV + 2,
        baseV + 2, baseV + 3, baseV + 5,  baseV + 2, baseV + 5, baseV + 4,
        baseV + 4, baseV + 5, baseV + 6,
      );
    }

    function addClump(cx, cz, nBlades, radX, radZ, hMin, hMax, wScale, leanOutX, leanOutZ) {
      const clumpShade = 0.86 + nextR() * 0.24;
      const clumpHue = 0.90 + nextR() * 0.22;
      for (let k = 0; k < nBlades; k++) {
        const ang = nextR() * Math.PI * 2;
        const r = Math.sqrt(nextR());
        const bx = cx + Math.cos(ang) * radX * r;
        const bz = cz + Math.sin(ang) * radZ * r;
        // Fan blades slightly outward from clump center + slight bias away from playable clearing edge
        const outX = Math.cos(ang) * 0.72 + leanOutX * 0.28;
        const outZ = Math.sin(ang) * 0.72 + leanOutZ * 0.28;
        const yaw = Math.atan2(outX, outZ) + (nextR() - 0.5) * 0.55;
        const h = hMin + nextR() * (hMax - hMin);
        const w = (0.14 + nextR() * 0.11) * wScale;
        const lean = 0.28 + nextR() * 0.36;
        const shade = clumpShade * (0.82 + nextR() * 0.30);
        pushBlade(bx, bz, h, w, yaw, lean, shade, clumpHue);
      }
    }

    // 3 concentric staggered organic bands outside all 4 edges + corners:
    // Band 0 hugs the clearing tightly (zero bare strip; South slightly lower & further out so row `rows-1` stays clear!)
    const bands = [
      { offN: 0.55, offS: 1.15, offW: 0.58, offE: 0.58, step: 1.05, jA: 0.34, jO: 0.28, blades: 10, rad: 0.72, hMin: 0.80, hMax: 1.48, wS: 1.08, pad: 1.0 },
      { offN: 2.75, offS: 3.30, offW: 2.85, offE: 2.85, step: 2.25, jA: 0.65, jO: 0.60, blades: 10, rad: 1.42, hMin: 0.92, hMax: 1.72, wS: 1.28, pad: 3.6 },
      { offN: 6.75, offS: 7.30, offW: 6.95, offE: 6.95, step: 3.70, jA: 1.05, jO: 1.05, blades: 9,  rad: 2.10, hMin: 1.05, hMax: 1.92, wS: 1.48, pad: 8.0 },
    ];

    for (let b = 0; b < bands.length; b++) {
      const cfg = bands[b];
      // North & South edges (wrapping all 4 corners)
      for (let t = -cfg.pad; t <= cols + cfg.pad; t += cfg.step) {
        for (const isSouth of [false, true]) {
          const x = t + (nextR() - 0.5) * 2 * cfg.jA;
          const waterPush = (b === 0 && isEdgeWater(mapData, waterTile, x, isSouth ? rows - 1 : 0)) ? 0.50 : 0.0;
          const z = isSouth
            ? (rows + cfg.offS + (nextR() - 0.5) * 2 * cfg.jO + waterPush)
            : (-cfg.offN + (nextR() - 0.5) * 2 * cfg.jO - waterPush);
          const hMul = (b === 0 && isSouth) ? 0.80 : 1.0;
          addClump(x, z, cfg.blades, cfg.rad * 1.15, cfg.rad * 0.82, cfg.hMin * hMul, cfg.hMax * hMul, cfg.wS, 0, isSouth ? 1 : -1);
        }
      }
      // West & East edges
      for (let t = 0.5; t <= rows - 0.5; t += cfg.step) {
        for (const isEast of [false, true]) {
          const z = t + (nextR() - 0.5) * 2 * cfg.jA;
          const waterPush = (b === 0 && isEdgeWater(mapData, waterTile, isEast ? cols - 1 : 0, z)) ? 0.50 : 0.0;
          const x = isEast
            ? (cols + cfg.offE + (nextR() - 0.5) * 2 * cfg.jO + waterPush)
            : (-cfg.offW + (nextR() - 0.5) * 2 * cfg.jO - waterPush);
          addClump(x, z, cfg.blades, cfg.rad * 0.82, cfg.rad * 1.15, cfg.hMin, cfg.hMax, cfg.wS, isEast ? 1 : -1, 0);
        }
      }
    }

    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const makeBuf = (target, data, loc, size) => {
      const buf = gl.createBuffer();
      gl.bindBuffer(target, buf);
      gl.bufferData(target, data, gl.STATIC_DRAW);
      if (loc !== undefined) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0); }
      return buf;
    };
    const bufs = [
      makeBuf(gl.ARRAY_BUFFER, new Float32Array(pos), 0, 3),
      makeBuf(gl.ARRAY_BUFFER, new Float32Array(nrm), 1, 3),
      makeBuf(gl.ARRAY_BUFFER, new Float32Array(col), 2, 4),
      makeBuf(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(idx)),
    ];
    gl.bindVertexArray(null);
    return {
      vao,
      count: idx.length,
      cols,
      rows,
      dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
  }

  // Authentic 1998 `129..132` tapered ordnance canister batch for dense minefield maps like `Bombz Away` (896 bombs in 2 draw calls!).
  // Matches `items_structures.py::_build_team_bomb` (`blackbomb.glb`..`bluebomb.glb`): crimson foot, flared team canister,
  // crimson warning shoulder band, signature overhanging magenta-purple blast cap, brass bushing, curved cream fuse & blazing sparkler!
  function buildBatchedMapBombsMesh(gl, bombSpecs) {
    const { mat4Identity } = window.__antGltf;
    const body = { pos: [], nrm: [], uv: [], col: [], idx: [] };
    const spark = { pos: [], nrm: [], uv: [], col: [], idx: [] };
    const teamRgb = {
      blackbomb: [0.036, 0.046, 0.068],
      redbomb:   [0.520, 0.022, 0.024],
      bluebomb:  [0.032, 0.118, 0.560],
      greenbomb: [0.042, 0.340, 0.052],
    };
    const crimson = [0.214, 0.004, 0.006];
    const purple = [0.538, 0.012, 0.342];
    const purpleTop = [0.640, 0.022, 0.420];
    const brass = [0.711, 0.367, 0.027];
    const fuseCol = [0.869, 0.787, 0.604];

    function pushLathe(dst, cx, cz, sc, rings, segs) {
      const base = dst.pos.length / 3;
      const nR = rings.length;
      for (let r = 0; r < nR; r++) {
        const [rad, y, rgb] = rings[r];
        const prev = rings[Math.max(0, r - 1)], next = rings[Math.min(nR - 1, r + 1)];
        const dr = next[0] - prev[0], dy = next[1] - prev[1];
        const len = Math.hypot(dy, -dr) || 1;
        const nr = dy / len, ny = -dr / len;
        for (let s = 0; s < segs; s++) {
          const a = (s / segs) * Math.PI * 2;
          const ca = Math.cos(a), sa = Math.sin(a);
          dst.pos.push(cx + ca * rad * sc, y * sc, cz + sa * rad * sc);
          dst.nrm.push(ca * nr, ny, sa * nr);
          dst.uv.push(s / segs, r / (nR - 1));
          dst.col.push(rgb[0], rgb[1], rgb[2]);
        }
      }
      for (let r = 0; r < nR - 1; r++) {
        for (let s = 0; s < segs; s++) {
          const ns = (s + 1) % segs;
          const a = base + r * segs + s, b = base + r * segs + ns;
          const c = base + (r + 1) * segs + s, d = base + (r + 1) * segs + ns;
          dst.idx.push(a, d, b, a, c, d);
        }
      }
      // Top cap center closing the purple blast lid cleanly
      const topC = dst.pos.length / 3;
      const topR = rings[nR - 1];
      dst.pos.push(cx, topR[1] * sc, cz);
      dst.nrm.push(0, 1, 0);
      dst.uv.push(0.5, 0.5);
      dst.col.push(topR[2][0], topR[2][1], topR[2][2]);
      const lastRing = base + (nR - 1) * segs;
      for (let s = 0; s < segs; s++) {
        dst.idx.push(lastRing + s, topC, lastRing + ((s + 1) % segs));
      }
    }

    function pushTubeSeg(dst, p0, p1, r0, r1, segs, rgb) {
      const base = dst.pos.length / 3;
      const dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
      const len = Math.hypot(dx, dy, dz) || 1;
      const tx = dx / len, ty = dy / len, tz = dz / len;
      // Orthonormal frame (u, v) perpendicular to segment tangent
      let ux = -tz, uy = 0, uz = tx;
      const uL = Math.hypot(ux, uy, uz);
      if (uL < 0.01) { ux = 1; uy = 0; uz = 0; } else { ux /= uL; uz /= uL; }
      const vx = ty * uz - tz * uy, vy = tz * ux - tx * uz, vz = tx * uy - ty * ux;
      for (let s = 0; s < segs; s++) {
        const a = (s / segs) * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        const nx = ux * ca + vx * sa, ny = uy * ca + vy * sa, nz = uz * ca + vz * sa;
        dst.pos.push(p0[0] + nx * r0, p0[1] + ny * r0, p0[2] + nz * r0);
        dst.pos.push(p1[0] + nx * r1, p1[1] + ny * r1, p1[2] + nz * r1);
        dst.nrm.push(nx, ny, nz, nx, ny, nz);
        dst.uv.push(0, 0, 1, 1);
        dst.col.push(rgb[0], rgb[1], rgb[2], rgb[0], rgb[1], rgb[2]);
      }
      for (let s = 0; s < segs; s++) {
        const ns = (s + 1) % segs;
        const a = base + s * 2, b = a + 1, c = base + ns * 2, d = c + 1;
        dst.idx.push(a, c, b, b, c, d);
      }
    }

    function pushSparkStar(dst, cx, cy, cz, r, rgb) {
      const base = dst.pos.length / 3;
      const pts = [[0, r, 0], [r, 0, 0], [0, 0, r], [-r, 0, 0], [0, 0, -r], [0, -r, 0]];
      for (const [px, py, pz] of pts) {
        dst.pos.push(cx + px, cy + py, cz + pz);
        const l = Math.hypot(px, py, pz) || 1;
        dst.nrm.push(px / l, py / l, pz / l);
        dst.uv.push(0.5, 0.5);
        dst.col.push(rgb[0], rgb[1], rgb[2]);
      }
      const f = [0,1,2, 0,2,3, 0,3,4, 0,4,1, 5,2,1, 5,3,2, 5,4,3, 5,1,4];
      for (const k of f) dst.idx.push(base + k);
    }

    for (let i = 0; i < bombSpecs.length; i++) {
      const sp = bombSpecs[i];
      const s = sp.s || 0.715;
      const bodyRgb = teamRgb[sp.id] || teamRgb.blackbomb;
      // Exact 1:1 normalized profile of `blackbomb.glb` (`items_structures.py::_build_team_bomb`, y = 0.00..0.855):
      const rings = [
        [0.132, 0.000, crimson],   // riveted crimson foot base
        [0.172, 0.068, crimson],   // crimson foot belly
        [0.165, 0.095, bodyRgb],   // lower team canister
        [0.211, 0.345, bodyRgb],   // flared team enamel body
        [0.234, 0.435, crimson],   // deep crimson warning shoulder band
        [0.222, 0.460, purple],    // underside of overhanging purple blast cap
        [0.267, 0.510, purple],    // signature wide magenta-purple blast lid rim!
        [0.236, 0.576, purpleTop], // domed magenta-purple blast lid crown
        [0.052, 0.594, brass],     // brass hex fuse bushing
      ];
      pushLathe(body, sp.x, sp.z, s, rings, 10);
      // Curved cream-white cotton fuse sweeping up-left then arcing up-right to `(0.104, 0.855, 0.011)`!
      const p0 = [sp.x, 0.605 * s, sp.z];
      const p1 = [sp.x - 0.036 * s, 0.715 * s, sp.z - 0.006 * s];
      const p2 = [sp.x + 0.032 * s, 0.835 * s, sp.z + 0.012 * s];
      const p3 = [sp.x + 0.102 * s, 0.848 * s, sp.z + 0.010 * s];
      pushTubeSeg(body, p0, p1, 0.022 * s, 0.020 * s, 5, fuseCol);
      pushTubeSeg(body, p1, p2, 0.020 * s, 0.018 * s, 5, fuseCol);
      pushTubeSeg(body, p2, p3, 0.018 * s, 0.015 * s, 5, fuseCol);
      pushSparkStar(spark, sp.x + 0.108 * s, 0.855 * s, sp.z + 0.010 * s, 0.072 * s, [1.0, 0.82, 0.24]);
    }

    const bufs = [];
    const vaos = [];
    function uploadPrim(dst, mat) {
      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      const up = (target, data, loc, sz) => {
        const b = gl.createBuffer(); gl.bindBuffer(target, b); gl.bufferData(target, data, gl.STATIC_DRAW);
        if (loc !== undefined) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, sz, gl.FLOAT, false, 0, 0); }
        bufs.push(b);
      };
      up(gl.ARRAY_BUFFER, new Float32Array(dst.pos), 0, 3);
      up(gl.ARRAY_BUFFER, new Float32Array(dst.nrm), 1, 3);
      up(gl.ARRAY_BUFFER, new Float32Array(dst.uv), 2, 2);
      up(gl.ARRAY_BUFFER, new Float32Array(dst.col), 3, 3);
      up(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(dst.idx));
      gl.bindVertexArray(null);
      vaos.push(vao);
      return {
        vao, indexed: true, mode: gl.TRIANGLES, drawCount: dst.idx.length, indexType: gl.UNSIGNED_INT,
        hasUv: false, localMin: [0, 0, 0], localMax: [60, 2, 60], material: mat,
      };
    }

    const idMat = mat4Identity();
    const model = {
      animations: [], idleAnims: [], blinkInfo: { hasBlink: false },
      baseWorldMats: [idMat],
      bounds: { min: [0, 0, 0], max: [60, 2, 60] },
      size: [60, 2, 60],
      drawCalls: [
        {
          nodeIndex: 0, worldMat: idMat,
          prim: uploadPrim(body, {
            name: "batched_map_bombs", baseColor: [1, 1, 1, 1], metallic: 0.28, roughness: 0.36,
            normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
            emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: true,
          }),
        },
        {
          nodeIndex: 0, worldMat: idMat,
          prim: uploadPrim(spark, {
            name: "batched_map_bomb_sparks", baseColor: [1, 0.88, 0.38, 1], metallic: 0.0, roughness: 0.20,
            normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
            emissive: [2.8, 1.75, 0.32], alphaMode: "OPAQUE", doubleSided: true,
          }),
        },
      ],
      dispose() { vaos.forEach((v) => gl.deleteVertexArray(v)); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
    return {
      inst: { kind: "item", id: "batched_bombs", isGrass: true, model, clipName: "Idle", idleTime: 0, offsetMat: idMat },
      dispose() { model.dispose(); },
    };
  }

  window.__demoBorder = {
    buildBorderGrassMesh,
    buildBatchedMapBombsMesh,
  };
})();

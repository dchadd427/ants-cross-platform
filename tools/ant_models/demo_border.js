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

  window.__demoBorder = {
    buildBorderGrassMesh,
  };
})();

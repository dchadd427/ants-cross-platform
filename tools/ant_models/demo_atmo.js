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

    // Low-poly 5-vertex faceted backyard pebble / earth clod nestled into dirt (4 tris)
    function addPebble(cx, cz, rx, ry, rz, yaw, r, g, b) {
      const cy = Math.cos(yaw), sy = Math.sin(yaw);
      const baseV = vCount;
      const ring = [[-rx, 0, -rz * 0.4], [rx * 0.85, 0, -rz * 0.7], [rx, 0, rz * 0.65], [-rx * 0.75, 0, rz * 0.85]];
      for (let i = 0; i < 4; i++) {
        const lx = ring[i][0], lz = ring[i][2];
        const wx = cx + lx * cy - lz * sy, wz = cz + lx * sy + lz * cy;
        const nL = Math.hypot(lx, ry * 1.4, lz) || 1;
        pos.push(wx, 0.002, wz);
        nrm.push((lx * cy - lz * sy) / nL, (ry * 1.4) / nL, (lx * sy + lz * cy) / nL);
        col.push(r * 0.74, g * 0.74, b * 0.74, 0.0);
        vCount++;
      }
      pos.push(cx + (nextR() - 0.5) * rx * 0.3, ry, cz + (nextR() - 0.5) * rz * 0.3);
      nrm.push(0, 1, 0);
      col.push(r * 1.10, g * 1.10, b * 1.10, 0.0);
      vCount++;
      idx.push(
        baseV + 0, baseV + 1, baseV + 4,
        baseV + 1, baseV + 2, baseV + 4,
        baseV + 2, baseV + 3, baseV + 4,
        baseV + 3, baseV + 0, baseV + 4,
      );
    }

    // Tiny curled fallen backyard leaf chip / bark splinter resting flat on the soil (2 tris)
    function addLeafChip(cx, cz, len, wid, yaw, r, g, b) {
      const cy = Math.cos(yaw), sy = Math.sin(yaw);
      const baseV = vCount;
      const pts = [
        [0, 0.018, -len * 0.52],
        [wid * 0.5, 0.006, 0],
        [0, 0.022, len * 0.52],
        [-wid * 0.5, 0.008, 0],
      ];
      for (let i = 0; i < 4; i++) {
        const lx = pts[i][0], ly = pts[i][1], lz = pts[i][2];
        const s = (i === 0 || i === 2) ? 1.06 : 0.86;
        pos.push(cx + lx * cy - lz * sy, ly, cz + lx * sy + lz * cy);
        nrm.push((i === 1 ? 0.25 : i === 3 ? -0.25 : 0) * cy, 0.96, (i === 0 ? -0.18 : 0.18) * sy);
        col.push(r * s, g * s, b * s, 0.0);
        vCount++;
      }
      idx.push(baseV, baseV + 1, baseV + 2, baseV, baseV + 2, baseV + 3);
    }

    // Tiny 3-blade stray grass sprout poking naturally through backyard dirt crevices (6 tris)
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

    const area = cols * rows;
    // 1. Scattered backyard pebbles & small gravel clods (clustered naturally in organic pockets)
    const pebbleCount = Math.min(620, Math.round(area * 0.17));
    const pebblePalette = [
      [0.36, 0.30, 0.25], [0.46, 0.40, 0.34], [0.24, 0.21, 0.19], [0.30, 0.31, 0.38], [0.54, 0.46, 0.36],
    ];
    for (let i = 0; i < pebbleCount; i++) {
      const x = nextR() * cols, z = nextR() * rows;
      if (!isSafeLand(x, z)) continue;
      const pal = pebblePalette[Math.floor(nextR() * pebblePalette.length)];
      const sz = 0.048 + Math.pow(nextR(), 1.8) * 0.085;
      addPebble(x, z, sz * (0.85 + nextR() * 0.45), sz * (0.42 + nextR() * 0.32), sz * (0.75 + nextR() * 0.45), nextR() * Math.PI * 2, pal[0], pal[1], pal[2]);
    }

    // 2. Fallen tiny dried backyard leaf flecks & twig splinters (breaks up sterile clean earth!)
    const leafCount = Math.min(460, Math.round(area * 0.13));
    const leafPalette = [
      [0.34, 0.18, 0.06], [0.44, 0.26, 0.07], [0.22, 0.24, 0.08], [0.28, 0.15, 0.08], [0.48, 0.34, 0.10],
    ];
    for (let i = 0; i < leafCount; i++) {
      const x = nextR() * cols, z = nextR() * rows;
      if (!isSafeLand(x, z)) continue;
      const pal = leafPalette[Math.floor(nextR() * leafPalette.length)];
      const isTwig = nextR() < 0.32;
      const len = isTwig ? (0.14 + nextR() * 0.18) : (0.09 + nextR() * 0.11);
      const wid = isTwig ? (0.022 + nextR() * 0.014) : (0.06 + nextR() * 0.07);
      addLeafChip(x, z, len, wid, nextR() * Math.PI * 2, pal[0], pal[1], pal[2]);
    }

    // 3. Delicate stray backyard grass sprouts (concentrated near edges & sparsely across soil)
    const sproutCount = Math.min(340, Math.round(area * 0.10));
    for (let i = 0; i < sproutCount; i++) {
      const x = nextR() * cols, z = nextR() * rows;
      if (!isSafeLand(x, z)) continue;
      const edgeD = Math.min(Math.min(x, cols - x), Math.min(z, rows - z));
      if (edgeD > 4.5 && nextR() > 0.36) continue;
      addSprout(x, z, edgeD < 3.0 ? 1.15 : 0.85);
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

  window.__demoAtmo = {
    buildGroundScatterMesh,
    buildAtmoParticles,
  };
})();

"use strict";

// High-detail multi-stage 3D Bomb Explosion FX (`133:bombex` 1998 Ants supercharged in 3D):
// 1. Blinding detonation flash, radial supersonic flare spikes & multi-lobe turbulent fireball plumes
// 2. Crisp fiery terrain-hugging ground shockwave, broken 1998 fire arcs, dust veil & organic scorch crater
// 3. Velocity-aligned ballistic glowing spark shower, red bomb shrapnel & tumbling dirt debris chunks
// 4. Seamless metaball volumetric smoke plumes (expanding hollow ring, rising crown, trailing tufts) + comical Dud sputter
(() => {
  const { mat4FromTRS, mat4Identity, quatMul } = window.__antGltf;

  function lerp(a, b, u) { return a + (b - a) * u; }
  function clamp01(u) { return Math.max(0, Math.min(1, u)); }
  function smooth(u) { const c = clamp01(u); return c * c * (3 - 2 * c); }

  function hash01(seed) {
    let s = (seed >>> 0) + 0x6D2B79F5;
    s = Math.imul(s ^ (s >>> 15), 1 | s);
    s = (s + Math.imul(s ^ (s >>> 7), 61 | s)) ^ s;
    return ((s ^ (s >>> 14)) >>> 0) / 4294967296;
  }

  function hash2i(ix, iy, seed) {
    return hash01(((ix * 374761393) ^ (iy * 668265263) ^ (seed * 2246822519)) >>> 0);
  }

  function tileNoise2(x, y, period, seed) {
    const px = ((x % period) + period) % period;
    const py = ((y % period) + period) % period;
    const x0 = Math.floor(px), y0 = Math.floor(py);
    const x1 = (x0 + 1) % period, y1 = (y0 + 1) % period;
    const fx = px - x0, fy = py - y0;
    const ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    return lerp(
      lerp(hash2i(x0, y0, seed), hash2i(x1, y0, seed), ux),
      lerp(hash2i(x0, y1, seed), hash2i(x1, y1, seed), ux),
      uy,
    );
  }

  function tileFbm2(u, v, basePeriod, seed, octaves = 4) {
    let sum = 0, amp = 0.52, norm = 0, p = basePeriod;
    for (let o = 0; o < octaves; o++) {
      sum += amp * tileNoise2(u * p, v * p, p, seed + o * 17);
      norm += amp; amp *= 0.5; p *= 2;
    }
    return sum / norm;
  }

  function quatY(rad) { const h = rad * 0.5; return [0, Math.sin(h), 0, Math.cos(h)]; }

  function quatFromAxisAngle(ax, ay, az, rad) {
    const l = Math.hypot(ax, ay, az) || 1, h = rad * 0.5, s = Math.sin(h) / l;
    return [ax * s, ay * s, az * s, Math.cos(h)];
  }

  function quatFromDirY(dx, dy, dz) {
    const l = Math.hypot(dx, dy, dz) || 1;
    const nx = dx / l, ny = dy / l, nz = dz / l;
    if (ny > 0.9995) return [0, 0, 0, 1];
    if (ny < -0.9995) return [1, 0, 0, 0];
    const qx = nz, qz = -nx, qw = 1 + ny;
    const ql = Math.hypot(qx, qz, qw) || 1;
    return [qx / ql, 0, qz / ql, qw / ql];
  }

  function recomputeSmoothNormals(pos, idx) {
    const nrm = new Float32Array(pos.length);
    for (let i = 0; i + 2 < idx.length; i += 3) {
      const i0 = idx[i] * 3, i1 = idx[i + 1] * 3, i2 = idx[i + 2] * 3;
      const ax = pos[i1] - pos[i0], ay = pos[i1 + 1] - pos[i0 + 1], az = pos[i1 + 2] - pos[i0 + 2];
      const bx = pos[i2] - pos[i0], by = pos[i2 + 1] - pos[i0 + 1], bz = pos[i2 + 2] - pos[i0 + 2];
      const nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
      nrm[i0] += nx; nrm[i0 + 1] += ny; nrm[i0 + 2] += nz;
      nrm[i1] += nx; nrm[i1 + 1] += ny; nrm[i1 + 2] += nz;
      nrm[i2] += nx; nrm[i2 + 1] += ny; nrm[i2 + 2] += nz;
    }
    for (let i = 0; i < nrm.length; i += 3) {
      const l = Math.hypot(nrm[i], nrm[i + 1], nrm[i + 2]);
      if (l > 1e-6) { nrm[i] /= l; nrm[i + 1] /= l; nrm[i + 2] /= l; }
      else { nrm[i] = 0; nrm[i + 1] = 1; nrm[i + 2] = 0; }
    }
    return nrm;
  }

  // --- Procedural WebGL2 Textures ---
  function uploadRgbaTex(gl, size, bytes, repeat) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, size, size, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
    const wrap = repeat ? gl.REPEAT : gl.CLAMP_TO_EDGE;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrap);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, wrap);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.bindTexture(gl.TEXTURE_2D, null);
    return tex;
  }

  function buildProceduralTextures(gl) {
    const S = 128;

    // 1. High-contrast volcanic fire texture: white-gold plasma veins amidst vivid orange & deep crimson folds
    const fireBytes = new Uint8Array(S * S * 4);
    for (let y = 0; y < S; y++) {
      const v = y / S;
      for (let x = 0; x < S; x++) {
        const u = x / S;
        const w1 = tileFbm2(u, v, 4, 11, 3);
        const w2 = tileFbm2(u + 0.31, v + 0.17, 4, 29, 3);
        const n = tileFbm2(u + (w1 - 0.5) * 0.30, v + (w2 - 0.5) * 0.30, 5, 47, 4);
        const ridge = 1.0 - Math.abs(n * 2.0 - 1.0);
        const hot = clamp01(Math.pow(ridge, 1.65) * 0.72 + n * 0.40 - 0.20);
        let r, g, b;
        if (hot < 0.40) {
          const k = hot / 0.40;
          r = lerp(0.12, 0.72, k); g = lerp(0.005, 0.040, k); b = 0.002;
        } else if (hot < 0.76) {
          const k = (hot - 0.40) / 0.36;
          r = lerp(0.72, 1.00, k); g = lerp(0.040, 0.40, k); b = lerp(0.002, 0.012, k);
        } else {
          const k = (hot - 0.76) / 0.24;
          r = 1.00; g = lerp(0.40, 0.90, k); b = lerp(0.012, 0.32, k);
        }
        const o = (y * S + x) * 4;
        fireBytes[o] = Math.round(r * 255); fireBytes[o + 1] = Math.round(g * 255);
        fireBytes[o + 2] = Math.round(b * 255); fireBytes[o + 3] = 255;
      }
    }

    // 2. Silky soft billowing cloud albedo + gentle low-frequency normal map (zero rocky grit!)
    const smokeH = new Float32Array(S * S);
    const smokeAlbBytes = new Uint8Array(S * S * 4);
    const smokeNrmBytes = new Uint8Array(S * S * 4);
    for (let y = 0; y < S; y++) {
      const v = y / S;
      for (let x = 0; x < S; x++) {
        const u = x / S;
        const nA = tileFbm2(u, v, 3, 73, 3);
        const nB = tileFbm2(u + 0.23, v + 0.41, 6, 97, 2);
        const puff = nA * 0.75 + nB * 0.25;
        smokeH[y * S + x] = puff;
        const tone = lerp(0.86, 1.00, smooth((puff - 0.24) / 0.58));
        const o = (y * S + x) * 4;
        smokeAlbBytes[o] = Math.round(tone * 255); smokeAlbBytes[o + 1] = Math.round(tone * 0.992 * 255);
        smokeAlbBytes[o + 2] = Math.round(tone * 0.985 * 255); smokeAlbBytes[o + 3] = 255;
      }
    }
    for (let y = 0; y < S; y++) {
      const ym = (y + S - 1) % S, yp = (y + 1) % S;
      for (let x = 0; x < S; x++) {
        const xm = (x + S - 1) % S, xp = (x + 1) % S;
        const dx = (smokeH[y * S + xp] - smokeH[y * S + xm]) * 0.95;
        const dy = (smokeH[yp * S + x] - smokeH[ym * S + x]) * 0.95;
        const l = Math.hypot(-dx, -dy, 1.0) || 1;
        const o = (y * S + x) * 4;
        smokeNrmBytes[o] = Math.round(((-dx / l) * 0.5 + 0.5) * 255);
        smokeNrmBytes[o + 1] = Math.round(((-dy / l) * 0.5 + 0.5) * 255);
        smokeNrmBytes[o + 2] = Math.round(((1.0 / l) * 0.5 + 0.5) * 255);
        smokeNrmBytes[o + 3] = 255;
      }
    }

    // 3. Organic irregular charred blast crater (asymmetric soot splatters + soft blast smudges, ZERO uniform spider-legs!)
    const scorchBytes = new Uint8Array(S * S * 4);
    for (let y = 0; y < S; y++) {
      const ny = (y / (S - 1)) * 2.0 - 1.0;
      for (let x = 0; x < S; x++) {
        const nx = (x / (S - 1)) * 2.0 - 1.0;
        const r = Math.hypot(nx, ny);
        const ang = (Math.atan2(ny, nx) / (Math.PI * 2) + 1.0) % 1.0;
        // Multi-frequency organic angular smudges & broken streaks (never regular periodic spikes!)
        const broad = tileFbm2(ang, r * 0.25, 5, 131, 3);
        const streak = tileFbm2(ang + broad * 0.12, r * 0.16, 11, 179, 3);
        const grit = tileFbm2(nx * 0.5 + 0.5, ny * 0.5 + 0.5, 7, 163, 4);
        const warp = (broad - 0.5) * 0.34 + (streak - 0.5) * 0.24 + (grit - 0.5) * 0.18;
        const rr = r + warp;
        const core = 1.0 - smooth((rr - 0.18) / 0.34);
        const halo = (1.0 - smooth((rr - 0.34) / 0.44)) * (0.42 + 0.58 * streak);
        const rimDirt = Math.exp(-Math.pow((rr - 0.56) / 0.14, 2)) * 0.38 * grit;
        const alpha = clamp01(Math.max(core * 0.90, halo * 0.68 + rimDirt)) * (1.0 - smooth((r - 0.72) / 0.26));
        const soot = lerp(0.068, 0.016, core) + rimDirt * 0.055;
        const o = (y * S + x) * 4;
        scorchBytes[o] = Math.round(clamp01(soot * 1.08) * 255);
        scorchBytes[o + 1] = Math.round(clamp01(soot * 0.88) * 255);
        scorchBytes[o + 2] = Math.round(clamp01(soot * 0.74) * 255);
        scorchBytes[o + 3] = Math.round(alpha * 255);
      }
    }

    // 4. Razor-sharp fiery orange-crimson supersonic ground shockwave band
    const shockBytes = new Uint8Array(S * S * 4);
    for (let y = 0; y < S; y++) {
      const v = y / (S - 1);
      for (let x = 0; x < S; x++) {
        const u = x / S;
        const jag = tileFbm2(u, v * 0.35, 16, 211, 3);
        const streaks = tileFbm2(u, v * 0.20, 22, 239, 3);
        const edgeV = 0.82 + (jag - 0.5) * 0.12;
        const front = Math.exp(-Math.pow((v - edgeV) / 0.10, 2));
        const flameBand = smooth((v - 0.16) / 0.32) * (1.0 - smooth((v - (edgeV + 0.04)) / 0.10));
        const heat = clamp01(front * 0.96 + flameBand * (0.28 + 0.54 * streaks));
        const r = clamp01(lerp(0.72, 1.00, heat));
        const g = clamp01(lerp(0.03, 0.52, Math.pow(heat, 1.85)));
        const b = clamp01(lerp(0.00, 0.12, Math.pow(heat, 3.8)));
        const alpha = clamp01((front * 0.96 + flameBand * (0.34 + 0.52 * streaks)) * (1.0 - smooth((v - 0.94) / 0.06)));
        const o = (y * S + x) * 4;
        shockBytes[o] = Math.round(r * 255); shockBytes[o + 1] = Math.round(g * 255);
        shockBytes[o + 2] = Math.round(b * 255); shockBytes[o + 3] = Math.round(alpha * 255);
      }
    }

    // 5. Soft circular ground dust-kick veil
    const dustBytes = new Uint8Array(S * S * 4);
    for (let y = 0; y < S; y++) {
      const v = y / (S - 1);
      for (let x = 0; x < S; x++) {
        const u = x / S;
        const w = tileFbm2(u, v * 0.45, 10, 271, 3);
        const alpha = clamp01(Math.sin(v * Math.PI) * (0.45 + 0.55 * w) * 0.65) * smooth(v / 0.18) * (1.0 - smooth((v - 0.76) / 0.24));
        const c = lerp(0.28, 0.46, w);
        const o = (y * S + x) * 4;
        dustBytes[o] = Math.round(c * 255); dustBytes[o + 1] = Math.round(c * 0.88 * 255);
        dustBytes[o + 2] = Math.round(c * 0.76 * 255); dustBytes[o + 3] = Math.round(alpha * 255);
      }
    }

    const fireTex = uploadRgbaTex(gl, S, fireBytes, true);
    const smokeAlbedoTex = uploadRgbaTex(gl, S, smokeAlbBytes, true);
    const smokeNormalTex = uploadRgbaTex(gl, S, smokeNrmBytes, true);
    const scorchTex = uploadRgbaTex(gl, S, scorchBytes, false);
    const shockTex = uploadRgbaTex(gl, S, shockBytes, true);
    const dustTex = uploadRgbaTex(gl, S, dustBytes, true);
    return {
      fireTex, smokeAlbedoTex, smokeNormalTex, scorchTex, shockTex, dustTex,
      dispose() {
        gl.deleteTexture(fireTex); gl.deleteTexture(smokeAlbedoTex); gl.deleteTexture(smokeNormalTex);
        gl.deleteTexture(scorchTex); gl.deleteTexture(shockTex); gl.deleteTexture(dustTex);
      },
    };
  }

  // --- Shared Mesh Upload & Material Cloner ---
  function makeMeshModel(gl, pos, nrm, uv, col, idx, mat, rad) {
    const idMat = mat4Identity();
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const bufs = [];
    const up = (target, data, loc, sz) => {
      const b = gl.createBuffer();
      gl.bindBuffer(target, b);
      gl.bufferData(target, data, gl.STATIC_DRAW);
      if (loc !== undefined) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, sz, gl.FLOAT, false, 0, 0); }
      bufs.push(b);
    };
    up(gl.ARRAY_BUFFER, pos instanceof Float32Array ? pos : new Float32Array(pos), 0, 3);
    up(gl.ARRAY_BUFFER, nrm instanceof Float32Array ? nrm : new Float32Array(nrm), 1, 3);
    up(gl.ARRAY_BUFFER, uv instanceof Float32Array ? uv : new Float32Array(uv), 2, 2);
    up(gl.ARRAY_BUFFER, col instanceof Float32Array ? col : new Float32Array(col), 3, 3);
    up(gl.ELEMENT_ARRAY_BUFFER, idx instanceof Uint16Array ? idx : new Uint16Array(idx));
    gl.bindVertexArray(null);
    return {
      animations: [], idleAnims: [], blinkInfo: { hasBlink: false },
      baseWorldMats: [idMat],
      bounds: { min: [-rad, -rad, -rad], max: [rad, rad, rad] },
      size: [rad * 2, rad * 2, rad * 2],
      drawCalls: [{
        nodeIndex: 0, worldMat: idMat,
        prim: {
          vao, indexed: true, mode: gl.TRIANGLES, drawCount: idx.length, indexType: gl.UNSIGNED_SHORT,
          hasUv: true, localMin: [-rad, -rad, -rad], localMax: [rad, rad, rad], material: mat,
        },
      }],
      dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); },
    };
  }

  function makeFxInst(id, sharedModel, matOverrides = null) {
    let model = sharedModel;
    let matRef = sharedModel.drawCalls[0].prim.material;
    if (matOverrides) {
      const srcDc = sharedModel.drawCalls[0], srcMat = srcDc.prim.material;
      const clonedMat = {
        ...srcMat, ...matOverrides,
        baseColor: (matOverrides.baseColor || srcMat.baseColor).slice(),
        emissive: (matOverrides.emissive || srcMat.emissive).slice(),
      };
      matRef = clonedMat;
      model = {
        ...sharedModel, _hasBlend: undefined,
        drawCalls: [{ nodeIndex: srcDc.nodeIndex, worldMat: srcDc.worldMat, prim: { ...srcDc.prim, material: clonedMat } }],
      };
    }
    return {
      kind: "item", id, submerged: false, model, mat: matRef,
      clipName: "Idle", idleTime: 0, offsetMat: mat4Identity(),
    };
  }

  // Seamless pole-free subdivided icosphere (subdiv=2 -> 162 verts, 320 equilateral tris, ZERO north-pole pinch!)
  function buildUnitIcosphere(subdiv = 2) {
    const t = (1 + Math.sqrt(5)) * 0.5;
    let verts = [
      [-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0],
      [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
      [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1],
    ].map(([x, y, z]) => { const l = Math.hypot(x, y, z); return [x / l, y / l, z / l]; });
    let tris = [
      [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
      [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
      [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
      [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
    ];
    for (let s = 0; s < subdiv; s++) {
      const midCache = new Map(), next = [];
      const mid = (a, b) => {
        const key = a < b ? `${a}_${b}` : `${b}_${a}`;
        if (midCache.has(key)) return midCache.get(key);
        const va = verts[a], vb = verts[b];
        const mx = va[0] + vb[0], my = va[1] + vb[1], mz = va[2] + vb[2];
        const ml = Math.hypot(mx, my, mz) || 1;
        const id = verts.length;
        verts.push([mx / ml, my / ml, mz / ml]);
        midCache.set(key, id);
        return id;
      };
      for (const [a, b, c] of tris) {
        const ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
        next.push([a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]);
      }
      tris = next;
    }
    return { verts, tris };
  }

  const ICO_2 = buildUnitIcosphere(2);
  const ICO_1 = buildUnitIcosphere(1);

  // Append one smooth pole-free icosphere puff lobe into (pos, uv, col, idx)
  function appendIcoCloudBulb(ico, pos, uv, col, idx, cx, cy, cz, rx, ry, rz, seed, wobAmp, cBot, cTop) {
    const baseV = pos.length / 3;
    for (let i = 0; i < ico.verts.length; i++) {
      const [dx, dy, dz] = ico.verts[i];
      const w = 1.0 + wobAmp * (
        0.60 * Math.sin(dx * 3.8 + dy * 3.2 - dz * 3.5 + seed)
        + 0.40 * Math.cos(dy * 4.4 - dx * 3.1 + dz * 4.0 + seed * 1.9)
      );
      const px = cx + dx * rx * w, py = cy + dy * ry * w, pz = cz + dz * rz * w;
      pos.push(px, py, pz);
      uv.push(px * 0.72 + py * 0.25 + seed * 0.11, pz * 0.72 - py * 0.22 + seed * 0.17);
      const up = clamp01(0.46 + 0.54 * dy);
      col.push(lerp(cBot[0], cTop[0], up), lerp(cBot[1], cTop[1], up), lerp(cBot[2], cTop[2], up));
    }
    for (const [a, b, c] of ico.tris) idx.push(baseV + a, baseV + b, baseV + c);
  }

  // Seamless single-surface directional-Gaussian metaball on pole-free icosphere (ZERO UV poles!)
  function buildMetaballSurface(ico, baseR, bumps, turbAmp, seed, yScale, cBot, cTop, colorByHeight) {
    const pos = [], uv = [], col = [], idx = [];
    const normBumps = bumps.map(([bx, by, bz, amp, width]) => {
      const l = Math.hypot(bx, by, bz) || 1;
      return [bx / l, by / l, bz / l, amp, width];
    });
    for (let i = 0; i < ico.verts.length; i++) {
      const [dx, dy, dz] = ico.verts[i];
      let r = baseR, bumpSum = 0;
      for (const [bx, by, bz, amp, w] of normBumps) {
        const b = amp * Math.exp(-(1.0 - (dx * bx + dy * by + dz * bz)) / w);
        r += b; bumpSum += b;
      }
      if (turbAmp > 0) {
        r += turbAmp * (
          0.55 * Math.sin(dx * 5.2 + dy * 4.6 - dz * 5.0 + seed)
          + 0.45 * Math.cos(dz * 6.0 - dy * 4.8 + dx * 5.4 + seed * 2.1)
        );
      }
      const px = dx * r, py = dy * r * yScale, pz = dz * r;
      pos.push(px, py, pz);
      uv.push(px * 0.72 + py * 0.28 + seed * 0.11, pz * 0.72 - py * 0.24 + seed * 0.17);
      const shade = colorByHeight
        ? clamp01(0.44 + 0.52 * dy + bumpSum * 0.30 - 0.10)
        : clamp01((r - baseR) / 0.55);
      col.push(lerp(cBot[0], cTop[0], shade), lerp(cBot[1], cTop[1], shade), lerp(cBot[2], cTop[2], shade));
    }
    for (const [a, b, c] of ico.tris) idx.push(a, b, c);
    return { pos, nrm: recomputeSmoothNormals(pos, idx), uv, col, idx };
  }

  // --- Sculpted High-Detail 3D Explosion Meshes ---
  function buildBombExplosionModels(gl) {
    const tex = buildProceduralTextures(gl);

    // 1. Compact blinding incandescent white-yellow starburst core (`0.00..0.20s`)
    function buildFlashCore() {
      const m = buildMetaballSurface(ICO_2, 0.56, [
        [1, 0.2, 0.2, 0.34, 0.26], [-0.8, 0.4, 0.5, 0.32, 0.26], [0.2, 0.9, -0.3, 0.36, 0.28],
        [-0.3, -0.1, -0.9, 0.34, 0.26], [0.5, -0.2, 0.8, 0.30, 0.25], [-0.7, 0.3, -0.6, 0.30, 0.26],
      ], 0.10, 1.3, 0.92, [1.0, 0.98, 0.84], [1.0, 0.46, 0.03], false);
      return makeMeshModel(gl, m.pos, m.nrm, m.uv, m.col, m.idx, {
        name: "bombex_flash_core", baseColor: [1.8, 1.45, 0.78, 1.0], metallic: 0.0, roughness: 0.35,
        normalScale: 0.0, normalTex: null, baseTex: tex.fireTex, mrTex: null,
        emissive: [5.4, 3.6, 1.25], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 1.05);
    }

    // 2. 16 razor-sharp supersonic detonation flame darts shooting radially outward
    function buildFlareSpikes() {
      const pos = [], uv = [], col = [], idx = [];
      const specs = [
        [0.08, 0.20, 1.66, 0.16], [0.48, 0.38, 1.36, 0.13], [0.92, 0.16, 1.74, 0.17], [1.34, 0.50, 1.28, 0.12],
        [1.76, 0.22, 1.60, 0.15], [2.18, 0.34, 1.40, 0.14], [2.62, 0.18, 1.70, 0.16], [3.06, 0.46, 1.34, 0.13],
        [3.48, 0.20, 1.62, 0.16], [3.92, 0.38, 1.38, 0.13], [4.38, 0.15, 1.72, 0.17], [4.84, 0.48, 1.26, 0.12],
        [5.32, 0.22, 1.58, 0.15], [5.78, 0.36, 1.42, 0.14], [1.10, 0.96, 1.30, 0.14], [4.10, 1.02, 1.34, 0.15],
      ];
      for (const [az, el, len, halfW] of specs) {
        const ce = Math.cos(el), se = Math.sin(el);
        const dx = Math.cos(az) * ce, dy = se, dz = Math.sin(az) * ce;
        let ux = -dz, uz = dx, ul = Math.hypot(ux, uz);
        if (ul < 1e-4) { ux = 1; uz = 0; ul = 1; }
        ux /= ul; uz /= ul;
        const vx = dy * uz, vy = dz * ux - dx * uz, vz = -dy * ux;
        const baseV = pos.length / 3, midL = len * 0.34;
        pos.push(dx * 0.10, dy * 0.10, dz * 0.10); uv.push(0.5, 0.1); col.push(1.0, 0.96, 0.65);
        const rDirs = [[ux, 0, uz], [vx, vy, vz], [-ux, 0, -uz], [-vx, -vy, -vz]];
        for (let k = 0; k < 4; k++) {
          pos.push(dx * midL + rDirs[k][0] * halfW, dy * midL + rDirs[k][1] * halfW, dz * midL + rDirs[k][2] * halfW);
          uv.push(k * 0.25, 0.45); col.push(1.0, 0.50, 0.04);
        }
        pos.push(dx * len, dy * len, dz * len); uv.push(0.5, 0.96); col.push(0.95, 0.08, 0.01);
        for (let k = 0; k < 4; k++) {
          const nk = (k + 1) % 4;
          idx.push(baseV, baseV + 1 + nk, baseV + 1 + k, baseV + 1 + k, baseV + 1 + nk, baseV + 5);
        }
      }
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name: "bombex_flare_spikes", baseColor: [2.4, 0.78, 0.05, 1.0], metallic: 0.0, roughness: 0.45,
        normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
        emissive: [3.2, 0.88, 0.03], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 1.80);
    }

    // 3. Seamless multi-lobe turbulent metaball fireball plumes (pole-free icosphere magma-billow surface!)
    function buildFirePlume(seed, bumps, name) {
      const m = buildMetaballSurface(ICO_2, 0.44, bumps, 0.13, seed, 1.08, [1.0, 0.88, 0.25], [0.96, 0.12, 0.01], false);
      return makeMeshModel(gl, m.pos, m.nrm, m.uv, m.col, m.idx, {
        name, baseColor: [2.8, 0.88, 0.04, 1.0], metallic: 0.0, roughness: 0.72,
        normalScale: 0.14, normalTex: tex.smokeNormalTex, baseTex: tex.fireTex, mrTex: null,
        emissive: [1.75, 0.36, 0.01], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: false,
      }, 1.25);
    }

    // 4. Jagged curved crimson-orange outer flame-tongue crown (9 curled 3D flame petals!)
    function buildFlameCrown(seed, name) {
      const pos = [], uv = [], col = [], idx = [];
      const petals = 9;
      for (let p = 0; p < petals; p++) {
        const a0 = (p / petals) * Math.PI * 2 + (hash01(seed + p * 11) - 0.5) * 0.34;
        const ca = Math.cos(a0), sa = Math.sin(a0), tx = -sa, tz = ca;
        const len = 0.96 + hash01(seed + p * 23) * 0.62;
        const baseW = 0.22 + hash01(seed + p * 37) * 0.10;
        const lift = 0.38 + hash01(seed + p * 53) * 0.54;
        const curl = (hash01(seed + p * 71) - 0.5) * 0.38;
        const segs = 5, baseV = pos.length / 3;
        for (let s = 0; s <= segs; s++) {
          const u = s / segs, r = 0.18 + u * len;
          const y = Math.pow(u, 1.2) * lift + Math.sin(u * Math.PI) * 0.12;
          const sway = Math.sin(u * Math.PI) * curl;
          const w = baseW * Math.sin(clamp01(0.18 + u * 0.82) * Math.PI) * (1.0 - u * 0.70);
          const thk = w * 0.44;
          const cx = ca * r + tx * sway, cz = sa * r + tz * sway;
          const cG = lerp(0.82, 0.05, Math.pow(u, 0.80));
          const cB = lerp(0.20, 0.004, Math.pow(u, 0.65));
          pos.push(cx - tx * w, y - thk * 0.2, cz - tz * w); uv.push(0.1, u); col.push(1.0, cG * 0.52, cB * 0.3);
          pos.push(cx, y + thk, cz); uv.push(0.5, u); col.push(1.0, cG, cB);
          pos.push(cx + tx * w, y - thk * 0.2, cz + tz * w); uv.push(0.9, u); col.push(1.0, cG * 0.52, cB * 0.3);
        }
        for (let s = 0; s < segs; s++) {
          const r0 = baseV + s * 3, r1 = r0 + 3;
          idx.push(r0, r0 + 1, r1 + 1, r0, r1 + 1, r1, r0 + 1, r0 + 2, r1 + 2, r0 + 1, r1 + 2, r1 + 1);
        }
      }
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name, baseColor: [2.6, 0.36, 0.015, 1.0], metallic: 0.0, roughness: 0.58,
        normalScale: 0.0, normalTex: null, baseTex: tex.fireTex, mrTex: null,
        emissive: [2.2, 0.22, 0.005], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 1.65);
    }

    // 5. Broken 1998-signature jagged fiery blast arcs (`sprite_0309..0311`: plump sculpted flame ribbons!)
    function buildBrokenFireArcs() {
      const pos = [], uv = [], col = [], idx = [];
      const arcs = [
        [0.08, 0.82, 0.96, 0.28], [1.18, 1.90, 1.06, 0.25], [2.28, 3.02, 0.96, 0.30],
        [3.38, 4.08, 1.04, 0.27], [4.46, 5.14, 0.98, 0.28], [5.46, 6.06, 1.02, 0.24],
      ];
      for (let a = 0; a < arcs.length; a++) {
        const [aStart, aEnd, radBase, peakH] = arcs[a];
        const segs = 10, prof = 5, baseV = pos.length / 3;
        for (let s = 0; s <= segs; s++) {
          const u = s / segs, ang = lerp(aStart, aEnd, u), ca = Math.cos(ang), sa = Math.sin(ang);
          const env = Math.sin(u * Math.PI);
          const tooth = 0.65 + 0.45 * Math.pow(Math.abs(Math.sin(u * Math.PI * 3.5 + a * 1.4)), 0.75);
          const tubeR = peakH * 0.55 * env * tooth;
          const cy = 0.14 + tubeR * 0.65;
          for (let j = 0; j < prof; j++) {
            const b = (j / (prof - 1)) * Math.PI * 2;
            const cb = Math.cos(b), sb = Math.sin(b);
            const rr = radBase + cb * tubeR;
            const py = Math.max(0.03, cy + sb * tubeR * (sb > 0 ? 1.35 : 0.65));
            pos.push(ca * rr, py, sa * rr);
            uv.push(u * 2.0, j / (prof - 1));
            const top = 0.5 + 0.5 * sb;
            col.push(1.0, lerp(0.86, 0.12, top), lerp(0.28, 0.01, top));
          }
        }
        for (let s = 0; s < segs; s++) for (let j = 0; j < prof - 1; j++) {
          const p0 = baseV + s * prof + j, p1 = p0 + 1, p2 = p0 + prof, p3 = p2 + 1;
          idx.push(p0, p2, p1, p1, p2, p3);
        }
      }
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name: "bombex_fire_arcs", baseColor: [2.8, 0.78, 0.04, 1.0], metallic: 0.0, roughness: 0.60,
        normalScale: 0.0, normalTex: null, baseTex: tex.fireTex, mrTex: null,
        emissive: [2.3, 0.44, 0.01], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 1.35);
    }

    // 6. Crisp high-res fiery orange-gold terrain-hugging ground shockwave ring (64 segs)
    function buildShockRing() {
      const pos = [], uv = [], col = [], idx = [];
      const segs = 64;
      const prof = [
        { r: 0.54, y: 0.020, v: 0.02, c: [0.95, 0.10, 0.01] },
        { r: 0.74, y: 0.085, v: 0.36, c: [1.00, 0.36, 0.02] },
        { r: 0.89, y: 0.145, v: 0.74, c: [1.00, 0.72, 0.14] },
        { r: 0.96, y: 0.092, v: 0.88, c: [1.00, 0.88, 0.36] },
        { r: 1.02, y: 0.022, v: 0.99, c: [1.00, 0.36, 0.02] },
      ];
      for (let i = 0; i <= segs; i++) {
        const u = i / segs, a = u * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        const serr = 1.0 + 0.028 * Math.sin(a * 17.0 + 0.4) + 0.020 * Math.cos(a * 25.0 - 0.8);
        const hMod = 0.80 + 0.34 * Math.pow(0.5 + 0.5 * Math.sin(a * 19.0 + 0.9), 1.4);
        for (const pr of prof) {
          pos.push(ca * pr.r * serr, pr.y * hMod, sa * pr.r * serr);
          uv.push(u * 3.0, pr.v);
          col.push(pr.c[0], pr.c[1], pr.c[2]);
        }
      }
      const st = prof.length;
      for (let i = 0; i < segs; i++) for (let j = 0; j < st - 1; j++) {
        const a = i * st + j, b = a + 1, c = a + st, d = c + 1;
        idx.push(a, c, b, b, c, d);
      }
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name: "bombex_shock_ring", baseColor: [2.8, 0.62, 0.03, 0.92], metallic: 0.0, roughness: 0.98,
        normalScale: 0.0, normalTex: null, baseTex: tex.shockTex, mrTex: null,
        emissive: [3.0, 0.64, 0.02], alphaMode: "BLEND", isTranslucent: true, doubleSided: true,
      }, 1.25);
    }

    // 7. Subtle smooth low ground dust-kick ring (64 segs)
    function buildDustWave() {
      const pos = [], nrm = [], uv = [], col = [], idx = [];
      const segs = 64;
      const prof = [{ r: 0.68, y: 0.018, v: 0.02 }, { r: 0.84, y: 0.065, v: 0.50 }, { r: 1.02, y: 0.018, v: 0.98 }];
      for (let i = 0; i <= segs; i++) {
        const u = i / segs, a = u * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        for (const pr of prof) {
          pos.push(ca * pr.r, pr.y, sa * pr.r); nrm.push(0, 1, 0); uv.push(u * 4.0, pr.v); col.push(0.42, 0.36, 0.30);
        }
      }
      const st = prof.length;
      for (let i = 0; i < segs; i++) for (let j = 0; j < st - 1; j++) {
        const a = i * st + j, b = a + 1, c = a + st, d = c + 1;
        idx.push(a, c, b, b, c, d);
      }
      return makeMeshModel(gl, pos, nrm, uv, col, idx, {
        name: "bombex_dust_wave", baseColor: [0.64, 0.56, 0.48, 0.30], metallic: 0.0, roughness: 0.98,
        normalScale: 0.0, normalTex: null, baseTex: tex.dustTex, mrTex: null,
        emissive: [0, 0, 0], alphaMode: "BLEND", isTranslucent: true, doubleSided: true,
      }, 1.20);
    }

    // 8. Organic irregular charred ground scorch crater disc (`y = 0.018`)
    function buildScorchDisc() {
      const pos = [], nrm = [], uv = [], col = [], idx = [];
      const segs = 40, rings = 4;
      pos.push(0, 0.019, 0); nrm.push(0, 1, 0); uv.push(0.5, 0.5); col.push(0.14, 0.11, 0.095);
      for (let r = 1; r <= rings; r++) {
        const frac = r / rings;
        for (let s = 0; s < segs; s++) {
          const a = (s / segs) * Math.PI * 2;
          const warp = 1.0 + 0.08 * Math.sin(a * 5.0 + 0.4) + 0.06 * Math.cos(a * 8.0 - 1.1);
          const rad = frac * lerp(1.0, warp, frac);
          const x = Math.cos(a) * rad, z = Math.sin(a) * rad;
          pos.push(x, 0.019 - frac * 0.002, z); nrm.push(0, 1, 0);
          uv.push(0.5 + x * 0.44, 0.5 + z * 0.44);
          col.push(lerp(0.12, 0.30, frac), lerp(0.095, 0.24, frac), lerp(0.085, 0.20, frac));
        }
      }
      for (let s = 0; s < segs; s++) idx.push(0, 1 + ((s + 1) % segs), 1 + s);
      for (let r = 0; r < rings - 1; r++) {
        const r0 = 1 + r * segs, r1 = r0 + segs;
        for (let s = 0; s < segs; s++) {
          const ns = (s + 1) % segs;
          idx.push(r0 + s, r0 + ns, r1 + ns, r0 + s, r1 + ns, r1 + s);
        }
      }
      return makeMeshModel(gl, pos, nrm, uv, col, idx, {
        name: "bombex_scorch", baseColor: [0.24, 0.20, 0.17, 0.90], metallic: 0.0, roughness: 0.98,
        normalScale: 0.0, normalTex: null, baseTex: tex.scorchTex, mrTex: null,
        emissive: [0, 0, 0], alphaMode: "BLEND", isTranslucent: true, doubleSided: true,
      }, 1.25);
    }

    // 9. Glowing molten crater ember core inside ground zero
    function buildCraterEmber() {
      const pos = [], nrm = [], uv = [], col = [], idx = [];
      const segs = 28, rings = 3;
      pos.push(0, 0.025, 0); nrm.push(0, 1, 0); uv.push(0.5, 0.5); col.push(1.0, 0.86, 0.28);
      for (let r = 1; r <= rings; r++) {
        const frac = r / rings;
        for (let s = 0; s < segs; s++) {
          const a = (s / segs) * Math.PI * 2;
          const jag = 1.0 + 0.14 * Math.sin(a * 6.0 + 0.7) + 0.09 * Math.cos(a * 9.0);
          const x = Math.cos(a) * frac * jag, z = Math.sin(a) * frac * jag;
          pos.push(x, 0.025 - frac * 0.004, z); nrm.push(0, 1, 0);
          uv.push(0.5 + x * 0.42, 0.5 + z * 0.42);
          col.push(lerp(1.0, 0.46, frac), lerp(0.68, 0.04, frac), lerp(0.10, 0.004, frac));
        }
      }
      for (let s = 0; s < segs; s++) idx.push(0, 1 + ((s + 1) % segs), 1 + s);
      for (let r = 0; r < rings - 1; r++) {
        const r0 = 1 + r * segs, r1 = r0 + segs;
        for (let s = 0; s < segs; s++) {
          const ns = (s + 1) % segs;
          idx.push(r0 + s, r0 + ns, r1 + ns, r0 + s, r1 + ns, r1 + s);
        }
      }
      return makeMeshModel(gl, pos, nrm, uv, col, idx, {
        name: "bombex_crater_ember", baseColor: [2.6, 0.68, 0.05, 0.88], metallic: 0.0, roughness: 0.98,
        normalScale: 0.0, normalTex: null, baseTex: tex.fireTex, mrTex: null,
        emissive: [2.2, 0.42, 0.02], alphaMode: "BLEND", isTranslucent: true, doubleSided: true,
      }, 1.0);
    }

    // 10. Crisp tapered velocity-aligned ballistic spark streak needle
    function buildSparkStreak() {
      const pos = [0, 0.56, 0, 0.11, 0.14, 0, 0, 0.14, 0.11, -0.11, 0.14, 0, 0, 0.14, -0.11, 0, -0.95, 0];
      const uv = [0.5, 1, 0, 0.5, 0.25, 0.5, 0.5, 0.5, 0.75, 0.5, 0.5, 0];
      const col = [1, 0.98, 0.76, 1, 0.74, 0.14, 1, 0.74, 0.14, 1, 0.74, 0.14, 1, 0.74, 0.14, 1, 0.14, 0.01];
      const idx = [0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 1, 4, 5, 1, 2, 5, 2, 3, 5, 3, 4, 5, 4, 1];
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name: "bombex_spark", baseColor: [2.2, 1.30, 0.30, 1.0], metallic: 0.0, roughness: 0.25,
        normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
        emissive: [4.8, 2.4, 0.30], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 1.0);
    }

    // 11. Jagged curved red bomb casing shrapnel fragment (`sprite_0306..0307`)
    function buildShrapnelShard() {
      const pos = [
        0.00, 0.19, 0.06, -0.15, 0.02, 0.04, -0.08, -0.16, 0.05, 0.12, -0.11, 0.04, 0.16, 0.06, 0.03,
        0.00, 0.17, -0.04, -0.13, 0.01, -0.04, -0.07, -0.14, -0.03, 0.10, -0.09, -0.04, 0.14, 0.05, -0.04,
      ];
      const uv = [0.5, 1, 0, 0.6, 0.2, 0, 0.8, 0.1, 1, 0.7, 0.5, 1, 0, 0.6, 0.2, 0, 0.8, 0.1, 1, 0.7];
      const col = [
        0.82, 0.05, 0.04, 0.76, 0.04, 0.03, 0.64, 0.04, 0.03, 0.78, 0.06, 0.04, 0.84, 0.07, 0.04,
        0.14, 0.04, 0.02, 0.11, 0.03, 0.02, 0.12, 0.03, 0.02, 0.13, 0.04, 0.02, 0.14, 0.04, 0.02,
      ];
      const idx = [
        0, 1, 2, 0, 2, 3, 0, 3, 4, 5, 7, 6, 5, 8, 7, 5, 9, 8,
        0, 5, 1, 1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 8, 3, 8, 4, 4, 8, 9, 4, 9, 0, 0, 9, 5,
      ];
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name: "bombex_shrapnel", baseColor: [1.0, 1.0, 1.0, 1.0], metallic: 0.35, roughness: 0.32,
        normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
        emissive: [0.32, 0.04, 0.01], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 0.24);
    }

    // 12. Sculpted dark earth / dirt debris clods (pole-free icosphere)
    function buildDirtClod(seed, name) {
      const m = buildMetaballSurface(ICO_1, 0.10, [[1, 0.3, 0, 0.05, 0.4], [-0.5, 0.6, 0.6, 0.05, 0.4], [0, -0.5, -0.8, 0.04, 0.4]], 0.02, seed, 0.85, [0.045, 0.032, 0.022], [0.15, 0.10, 0.065], true);
      return makeMeshModel(gl, m.pos, m.nrm, m.uv, m.col, m.idx, {
        name, baseColor: [1.0, 1.0, 1.0, 1.0], metallic: 0.0, roughness: 0.94,
        normalScale: 0.22, normalTex: tex.smokeNormalTex, baseTex: null, mrTex: null,
        emissive: [0, 0, 0], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: false,
      }, 0.20);
    }

    // 13. Plush OPAQUE 4-bulb pole-free icosphere cauliflower smoke cloud (velvety round billows, ZERO poles, ZERO glass bubbles!)
    function buildSmokeCloud(seed, bulbs, botRgb, topRgb, name) {
      const pos = [], uv = [], col = [], idx = [];
      for (let i = 0; i < bulbs.length; i++) {
        const [cx, cy, cz, rx, ry, rz] = bulbs[i];
        appendIcoCloudBulb(ICO_2, pos, uv, col, idx, cx, cy, cz, rx, ry, rz, seed + i * 2.3, 0.04, botRgb, topRgb);
      }
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name, baseColor: [1.0, 1.0, 1.0, 1.0], metallic: 0.0, roughness: 0.98,
        normalScale: 0.08, normalTex: tex.smokeNormalTex, baseTex: tex.smokeAlbedoTex, mrTex: null,
        emissive: [0, 0, 0], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: false,
      }, 1.30);
    }

    // 14. Clean glowing golden ring preserved for Power-Up Morph vignette (`v.morphs`)
    function buildMorphRing() {
      const pos = [], uv = [], col = [], idx = [];
      const maj = 24, minS = 10, R = 0.78, rTube = 0.20;
      for (let i = 0; i <= maj; i++) {
        const a = (i / maj) * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a), jag = 1.0 + 0.14 * Math.sin(a * 6.0);
        for (let j = 0; j <= minS; j++) {
          const b = (j / minS) * Math.PI * 2, cb = Math.cos(b), sb = Math.sin(b), rr = R * jag + cb * rTube;
          pos.push(ca * rr, sb * rTube * 0.85, sa * rr); uv.push(i / maj, j / minS);
          const top = 0.5 + 0.5 * sb;
          col.push(1.0, lerp(0.45, 0.95, top), lerp(0.10, 0.42, top));
        }
      }
      const st = minS + 1;
      for (let i = 0; i < maj; i++) for (let j = 0; j < minS; j++) {
        const a = i * st + j, b = a + 1, c = a + st, d = c + 1;
        idx.push(a, c, b, b, c, d);
      }
      return makeMeshModel(gl, pos, recomputeSmoothNormals(pos, idx), uv, col, idx, {
        name: "morph_aura_ring", baseColor: [1, 1, 1, 1], metallic: 0.0, roughness: 0.30,
        normalScale: 0.0, normalTex: null, baseTex: null, mrTex: null,
        emissive: [2.2, 1.15, 0.22], alphaMode: "OPAQUE", isTranslucent: false, doubleSided: true,
      }, 1.15);
    }

    const flashCore = buildFlashCore();
    const flareSpikes = buildFlareSpikes();
    const fireBumpSets = [
      [[0.7, 0.5, 0.4, 0.38, 0.34], [-0.7, 0.4, -0.5, 0.34, 0.34], [0.1, 0.9, -0.3, 0.40, 0.36], [-0.4, -0.2, 0.8, 0.30, 0.32], [0.5, -0.3, -0.7, 0.28, 0.32]],
      [[-0.8, 0.5, 0.3, 0.38, 0.34], [0.6, 0.3, -0.6, 0.36, 0.34], [-0.2, 0.9, 0.4, 0.38, 0.36], [0.5, -0.2, 0.7, 0.32, 0.32], [-0.5, -0.3, -0.7, 0.28, 0.32]],
      [[0.4, 0.8, -0.5, 0.40, 0.36], [-0.6, 0.6, 0.5, 0.36, 0.34], [0.8, 0.1, 0.5, 0.34, 0.34], [-0.7, -0.2, -0.6, 0.30, 0.32], [0.1, -0.4, 0.8, 0.28, 0.32]],
    ];
    const firePlumes = fireBumpSets.map((bs, i) => buildFirePlume(1.5 + i * 3.7, bs, `bombex_fire_${i}`));
    const flameCrowns = [buildFlameCrown(19, "bombex_crown_a"), buildFlameCrown(53, "bombex_crown_b")];
    const fireArcs = buildBrokenFireArcs();
    const shockRing = buildShockRing();
    const dustWave = buildDustWave();
    const scorchDisc = buildScorchDisc();
    const craterEmber = buildCraterEmber();
    const sparkStreak = buildSparkStreak();
    const shrapnelShard = buildShrapnelShard();
    const dirtClods = [buildDirtClod(2.3, "bombex_dirt_a"), buildDirtClod(6.9, "bombex_dirt_b")];

    // 3 plush 4-bulb overlapping icosphere cauliflower cloud clusters (butter-smooth round billows, warm 1998 cream-taupe!)
    const cloudBulbSets = [
      [[0, 0.04, 0, 0.50, 0.46, 0.50], [0.34, 0.18, 0.16, 0.36, 0.34, 0.36], [-0.32, 0.14, -0.18, 0.36, 0.33, 0.38], [0.06, 0.30, -0.22, 0.32, 0.32, 0.32]],
      [[0, 0.02, 0, 0.48, 0.46, 0.50], [-0.32, 0.18, 0.20, 0.36, 0.36, 0.34], [0.30, 0.12, -0.22, 0.36, 0.32, 0.36], [-0.08, 0.32, 0.16, 0.32, 0.32, 0.30]],
      [[0, 0.06, 0, 0.50, 0.44, 0.48], [0.26, 0.24, -0.20, 0.36, 0.36, 0.34], [-0.28, 0.10, 0.24, 0.34, 0.32, 0.36], [0.12, -0.06, 0.26, 0.32, 0.28, 0.32]],
    ];
    const smokeClusters = cloudBulbSets.map((bs, i) => buildSmokeCloud(12 + i * 7, bs, [0.22, 0.20, 0.21], [0.84, 0.78, 0.70], `bombex_smoke_${i}`));
    const smokeTufts = cloudBulbSets.map((bs, i) => buildSmokeCloud(37 + i * 11, bs, [0.32, 0.29, 0.28], [0.88, 0.82, 0.75], `bombex_tuft_${i}`));
    const dudClusters = [
      buildSmokeCloud(61, cloudBulbSets[0], [0.055, 0.052, 0.062], [0.30, 0.28, 0.30], "bombex_dud_a"),
      buildSmokeCloud(79, cloudBulbSets[1], [0.075, 0.072, 0.082], [0.38, 0.36, 0.38], "bombex_dud_b"),
    ];
    const ring = buildMorphRing();

    const allMeshes = [
      flashCore, flareSpikes, ...firePlumes, ...flameCrowns, fireArcs, shockRing, dustWave,
      scorchDisc, craterEmber, sparkStreak, shrapnelShard, ...dirtClods,
      ...smokeClusters, ...smokeTufts, ...dudClusters, ring,
    ];
    return {
      flashCore, flareSpikes, firePlumes, flameCrowns, fireArcs, shockRing, dustWave,
      scorchDisc, craterEmber, sparkStreak, shrapnelShard, dirtClods,
      smokeClusters, smokeTufts, dudClusters, ring,
      dispose() { for (const m of allMeshes) m.dispose(); tex.dispose(); },
    };
  }

  // --- Per-Bomber Pre-Allocated Explosion Rig ---
  function createBombExplosionRig(assets) {
    // 9 multi-lobe turbulent fireball plumes (3 inner rising gold + 6 radial outer volcanic orange-crimson)
    const firePlumeSpecs = [
      { ang: 0.0,  r0: 0.02, r1: 0.16, y0: 0.26, y1: 1.54, scPeak: 0.80, delay: 0.00, dur: 0.42, spin:  2.0, inner: true,  v: 0 },
      { ang: 2.1,  r0: 0.08, r1: 0.42, y0: 0.22, y1: 1.24, scPeak: 0.70, delay: 0.02, dur: 0.40, spin: -2.2, inner: true,  v: 2 },
      { ang: 4.3,  r0: 0.08, r1: 0.40, y0: 0.24, y1: 1.18, scPeak: 0.68, delay: 0.03, dur: 0.39, spin:  2.4, inner: true,  v: 1 },
      { ang: 0.30, r0: 0.22, r1: 1.22, y0: 0.22, y1: 0.94, scPeak: 0.64, delay: 0.01, dur: 0.38, spin:  2.6, inner: false, v: 1 },
      { ang: 1.35, r0: 0.20, r1: 1.28, y0: 0.24, y1: 0.86, scPeak: 0.62, delay: 0.02, dur: 0.37, spin: -2.5, inner: false, v: 0 },
      { ang: 2.40, r0: 0.24, r1: 1.18, y0: 0.20, y1: 1.00, scPeak: 0.66, delay: 0.01, dur: 0.39, spin:  2.4, inner: false, v: 2 },
      { ang: 3.45, r0: 0.22, r1: 1.26, y0: 0.22, y1: 0.84, scPeak: 0.62, delay: 0.03, dur: 0.36, spin: -2.8, inner: false, v: 1 },
      { ang: 4.50, r0: 0.20, r1: 1.20, y0: 0.24, y1: 0.96, scPeak: 0.64, delay: 0.02, dur: 0.38, spin:  2.6, inner: false, v: 0 },
      { ang: 5.55, r0: 0.22, r1: 1.24, y0: 0.22, y1: 0.90, scPeak: 0.60, delay: 0.03, dur: 0.37, spin: -2.4, inner: false, v: 2 },
    ];

    // 26 high-velocity ballistic glowing spark streaks fanning well past the smoke ring
    const sparks = [];
    for (let i = 0; i < 26; i++) {
      const ang = (i / 26) * Math.PI * 2 + (hash01(101 + i * 17) - 0.5) * 0.22;
      const hot = i % 2 === 0;
      sparks.push({
        ca: Math.cos(ang), sa: Math.sin(ang),
        spdH: 3.4 + hash01(203 + i * 23) * 4.6,
        spdY: 2.8 + hash01(307 + i * 31) * 3.8,
        drag: 0.95 + hash01(409 + i * 13) * 0.80,
        life: 0.48 + hash01(503 + i * 29) * 0.34,
        delay: hash01(601 + i * 19) * 0.035,
        size: (hot ? 0.25 : 0.20) + hash01(701 + i * 11) * 0.12,
        hot,
        inst: makeFxInst(`bombex_spk_${i}`, assets.sparkStreak, {
          baseColor: hot ? [2.4, 1.55, 0.30, 1.0] : [2.2, 0.68, 0.05, 1.0],
          emissive: hot ? [5.0, 2.9, 0.38] : [3.8, 0.98, 0.03],
        }),
      });
    }

    // 8 spinning red bomb casing shrapnel fragments
    const shrapnel = [];
    for (let i = 0; i < 8; i++) {
      const ang = (i / 8) * Math.PI * 2 + 0.25 + (hash01(811 + i * 19) - 0.5) * 0.30;
      shrapnel.push({
        ca: Math.cos(ang), sa: Math.sin(ang),
        spdH: 2.4 + hash01(907 + i * 23) * 2.6,
        spdY: 3.4 + hash01(1009 + i * 29) * 2.4,
        ax: hash01(1103 + i * 7) - 0.5, ay: hash01(1151 + i * 11) - 0.5, az: hash01(1201 + i * 13) - 0.5,
        spinRate: 11.0 + hash01(1301 + i * 17) * 9.0,
        scale: 0.76 + hash01(1409 + i * 19) * 0.36,
        inst: makeFxInst(`bombex_shrap_${i}`, assets.shrapnelShard),
      });
    }

    // 14 tumbling dark earth / dirt debris clods
    const dirt = [];
    for (let i = 0; i < 14; i++) {
      const ang = (i / 14) * Math.PI * 2 + (hash01(1511 + i * 23) - 0.5) * 0.28;
      dirt.push({
        ca: Math.cos(ang), sa: Math.sin(ang),
        spdH: 2.0 + hash01(1601 + i * 29) * 3.0,
        spdY: 2.8 + hash01(1709 + i * 31) * 3.2,
        ax: hash01(1801 + i * 7) - 0.5, ay: hash01(1847 + i * 11) - 0.5, az: hash01(1901 + i * 13) - 0.5,
        spinRate: 8.5 + hash01(2003 + i * 17) * 10.0,
        scale: 0.68 + hash01(2111 + i * 19) * 0.55,
        life: 0.66 + hash01(2203 + i * 11) * 0.20,
        inst: makeFxInst(`bombex_dirt_${i}`, assets.dirtClods[i % assets.dirtClods.length]),
      });
    }

    // Tier 1: 12 plump overlapping cauliflower smoke clouds forming the iconic 1998 hollow ring (`0.06..1.08s`)
    const smokeRing = [];
    for (let i = 0; i < 12; i++) {
      const ang = (i / 12) * Math.PI * 2 + (hash01(2309 + i * 17) - 0.5) * 0.20;
      smokeRing.push({
        ang, ca: Math.cos(ang), sa: Math.sin(ang),
        r0: 0.46 + hash01(2411 + i * 13) * 0.14,
        r1: 1.98 + hash01(2503 + i * 19) * 0.44,
        y0: 0.18 + hash01(2609 + i * 23) * 0.10,
        y1: 0.92 + hash01(2707 + i * 29) * 0.38,
        scPeak: 0.72 + hash01(2801 + i * 31) * 0.22,
        spin: (i % 2 === 0 ? 1 : -1) * (0.9 + hash01(2903 + i * 11) * 0.7),
        delay: 0.06 + hash01(3001 + i * 13) * 0.06,
        dur: 0.90 + hash01(3109 + i * 17) * 0.16,
        inst: makeFxInst(`bombex_smk_r_${i}`, assets.smokeClusters[i % assets.smokeClusters.length], {}),
      });
    }

    // Tier 2: 5 towering rising smoke crown billows lifting cleanly above the scorch crater (`0.14..1.18s`)
    const smokeCrown = [];
    for (let i = 0; i < 5; i++) {
      const ang = (i / 5) * Math.PI * 2 + 0.55;
      smokeCrown.push({
        ang, ca: Math.cos(ang), sa: Math.sin(ang),
        r0: 0.36,
        r1: 1.24 + hash01(3203 + i * 17) * 0.36,
        y0: 0.56,
        y1: 2.28 + hash01(3301 + i * 23) * 0.45,
        scPeak: 0.76 + hash01(3407 + i * 19) * 0.18,
        spin: (i % 2 === 0 ? 0.75 : -0.85),
        delay: 0.14 + i * 0.03,
        dur: 0.98 + hash01(3511 + i * 13) * 0.08,
        inst: makeFxInst(`bombex_smk_c_${i}`, assets.smokeClusters[(i + 1) % assets.smokeClusters.length], {}),
      });
    }

    // Tier 3: 10 lighter OPAQUE outer drifting smoke tufts breaking away smoothly (`0.30..1.20s`, ZERO soap-bubble rims!)
    const smokeTufts = [];
    for (let i = 0; i < 10; i++) {
      const ang = (i / 10) * Math.PI * 2 + 0.31 + (hash01(3607 + i * 19) - 0.5) * 0.24;
      smokeTufts.push({
        ang, ca: Math.cos(ang), sa: Math.sin(ang),
        r0: 1.32 + hash01(3701 + i * 13) * 0.26,
        r1: 2.70 + hash01(3803 + i * 17) * 0.52,
        y0: 0.36 + hash01(3907 + i * 23) * 0.24,
        y1: 1.56 + hash01(4001 + i * 29) * 0.55,
        scPeak: 0.42 + hash01(4111 + i * 31) * 0.16,
        delay: 0.28 + hash01(4201 + i * 11) * 0.10,
        dur: 0.78 + hash01(4327 + i * 13) * 0.14,
        inst: makeFxInst(`bombex_tuft_${i}`, assets.smokeTufts[i % assets.smokeTufts.length], {}),
      });
    }

    // Comical sputtering Dud Bomb chimney chugs (3 tall dark charcoal cough bursts + 9 sizzling misfire sparks)
    const dudPuffs = [];
    const chugDelays = [0.02, 0.05, 0.16, 0.22, 0.34, 0.40, 0.50, 0.58, 0.66];
    for (let i = 0; i < chugDelays.length; i++) {
      const ang = i * 2.15 + 0.35;
      dudPuffs.push({
        ca: Math.cos(ang), sa: Math.sin(ang),
        delay: chugDelays[i],
        dur: 0.64 + (i % 3) * 0.07,
        r1: 0.46 + (i % 4) * 0.18,
        y1: 1.65 + i * 0.14,
        scPeak: 0.48 + (i % 3) * 0.10,
        inst: makeFxInst(`bombex_dud_p_${i}`, assets.dudClusters[i % 2], {}),
      });
    }
    const dudSparks = [];
    for (let i = 0; i < 9; i++) {
      const ang = (i / 9) * Math.PI * 2 + 0.4;
      dudSparks.push({
        ca: Math.cos(ang), sa: Math.sin(ang),
        delay: (i % 3) * 0.12,
        spdH: 1.45 + hash01(4507 + i * 13) * 1.10,
        spdY: 2.0 + hash01(4603 + i * 17) * 1.25,
        inst: makeFxInst(`bombex_dud_s_${i}`, assets.sparkStreak, {
          baseColor: [2.4, 1.05, 0.16, 1.0],
          emissive: [3.8, 1.25, 0.06],
        }),
      });
    }

    return {
      flashCore: makeFxInst("bombex_flash_core", assets.flashCore, {}),
      flareSpikes: makeFxInst("bombex_flare_spikes", assets.flareSpikes, {}),
      firePlumes: firePlumeSpecs.map((sp, i) => ({
        ...sp,
        inst: makeFxInst(`bombex_plume_${i}`, assets.firePlumes[sp.v], {}),
      })),
      flameCrowns: [
        makeFxInst("bombex_crown_0", assets.flameCrowns[0], {}),
        makeFxInst("bombex_crown_1", assets.flameCrowns[1], {}),
      ],
      fireArcs: makeFxInst("bombex_fire_arcs", assets.fireArcs, {}),
      shockRing: makeFxInst("bombex_shock_ring", assets.shockRing, {}),
      dustWave: makeFxInst("bombex_dust_wave", assets.dustWave, {}),
      scorch: makeFxInst("bombex_scorch", assets.scorchDisc, {}),
      craterEmber: makeFxInst("bombex_crater_ember", assets.craterEmber, {}),
      sparks, shrapnel, dirt, smokeRing, smokeCrown, smokeTufts,
      dudPuffs, dudSparks,
    };
  }

  // --- Multi-Stage Per-Frame Update ---
  function updateBombExplosion(rig, x, z, blastT, isDud, visible, dynGlows) {
    if (!rig || blastT < 0) return;

    if (isDud) {
      if (blastT >= 1.50) return;
      if (blastT > 0.02 && blastT < 1.48) {
        const scA = blastT < 0.10 ? smooth(blastT / 0.10) : (1.0 - smooth((blastT - 0.85) / 0.63));
        if (scA > 0.03) {
          rig.scorch.mat.baseColor[3] = scA * 0.72;
          const sr = lerp(0.32, 0.82, smooth(clamp01(blastT / 0.24)));
          rig.scorch.offsetMat = mat4FromTRS([x, 0.0, z], quatY(0.4), [sr, 1.0, sr]);
          visible.push(rig.scorch);
        }
      }
      if (blastT < 0.22) {
        const fu = blastT / 0.22;
        const fs = lerp(0.18, 0.48, Math.sin(fu * Math.PI));
        rig.flashCore.mat.emissive[0] = lerp(3.4, 0.6, fu);
        rig.flashCore.mat.emissive[1] = lerp(1.3, 0.12, fu);
        rig.flashCore.mat.emissive[2] = lerp(0.15, 0.01, fu);
        rig.flashCore.offsetMat = mat4FromTRS([x, 0.24, z], quatY(blastT * 7.0), [fs, fs * 0.75, fs]);
        visible.push(rig.flashCore);
      }
      for (const sp of rig.dudSparks) {
        const dt = blastT - sp.delay;
        if (dt <= 0 || dt >= 0.52) continue;
        const u = dt / 0.52, g = 10.5;
        const px = x + sp.ca * sp.spdH * dt * 0.80;
        const pz = z + sp.sa * sp.spdH * dt * 0.80;
        const py = Math.max(0.06, 0.28 + sp.spdY * dt - 0.5 * g * dt * dt);
        const vy = sp.spdY - g * dt;
        const sc = (1.0 - u) * 0.21;
        sp.inst.offsetMat = mat4FromTRS([px, py, pz], quatFromDirY(sp.ca * sp.spdH, vy, sp.sa * sp.spdH), [sc, sc * 1.9, sc]);
        visible.push(sp.inst);
      }
      for (const dp of rig.dudPuffs) {
        const dt = blastT - dp.delay;
        if (dt <= 0 || dt >= dp.dur) continue;
        const u = dt / dp.dur;
        const env = u < 0.24 ? smooth(u / 0.24) : Math.pow(1.0 - smooth((u - 0.34) / 0.66), 1.35);
        const sc = dp.scPeak * (0.20 + 0.90 * env);
        if (sc < 0.04) continue;
        const r = lerp(0.12, dp.r1, Math.pow(u, 0.66));
        const py = 0.36 + dp.y1 * Math.pow(u, 0.78);
        const sway = Math.sin(u * 5.0 + dp.delay * 9.0) * 0.14;
        dp.inst.offsetMat = mat4FromTRS([x + dp.ca * r + sway, py, z + dp.sa * r], quatY(u * 2.0 + dp.delay * 5.0), [sc, sc * 0.92, sc]);
        visible.push(dp.inst);
      }
      if (blastT < 0.58) dynGlows.push([x, z, 2.5, (1.0 - blastT / 0.58) * 1.25, 2.0, 0.76, 0.14]);
      return;
    }

    // ===================== LIVE BOMB DETONATION (0.00 -> 2.60s) =====================
    if (blastT >= 2.60) return;

    // --- STAGE 2D: Organic Charred Scorch Crater (0.03 -> 2.60s) + Molten Crater Embers (0.02 -> 0.68s) ---
    if (blastT > 0.02) {
      const scIn = smooth(clamp01((blastT - 0.02) / 0.14));
      const scOut = 1.0 - smooth(clamp01((blastT - 1.55) / 1.05));
      const scAlpha = scIn * scOut;
      if (scAlpha > 0.025) {
        rig.scorch.mat.baseColor[3] = scAlpha * 0.90;
        const scRad = lerp(0.76, 1.78, smooth(clamp01((blastT - 0.02) / 0.24))) * lerp(0.92, 1.0, scOut);
        rig.scorch.offsetMat = mat4FromTRS([x, 0.0, z], quatY(0.35), [scRad, 1.0, scRad]);
        visible.push(rig.scorch);
      }
    }
    if (blastT > 0.02 && blastT < 0.68) {
      const cu = (blastT - 0.02) / 0.66;
      const cFade = smooth(clamp01(cu / 0.08)) * Math.pow(1.0 - smooth(clamp01((cu - 0.16) / 0.84)), 1.3);
      if (cFade > 0.03) {
        rig.craterEmber.mat.baseColor[3] = cFade * 0.88;
        rig.craterEmber.mat.emissive[0] = lerp(3.0, 0.55, cu) * cFade;
        rig.craterEmber.mat.emissive[1] = lerp(0.98, 0.05, Math.pow(cu, 0.72)) * cFade;
        rig.craterEmber.mat.emissive[2] = lerp(0.06, 0.00, cu) * cFade;
        const cr = lerp(0.46, 1.04, Math.pow(cu, 0.45));
        rig.craterEmber.offsetMat = mat4FromTRS([x, 0.0, z], quatY(-0.5 + cu * 0.22), [cr, 1.0, cr]);
        visible.push(rig.craterEmber);
      }
    }

    // --- STAGE 1: Detonation Flash, Supersonic Flare Spikes & Volcanic Multi-Lobe Fireball (0.00 -> 0.44s) ---
    if (blastT < 0.22) {
      const fu = blastT / 0.22;
      const fs = fu < 0.26 ? lerp(0.32, 0.86, smooth(fu / 0.26)) : lerp(0.86, 0.08, Math.pow((fu - 0.26) / 0.74, 1.2));
      const hot = 1.0 - fu;
      rig.flashCore.mat.emissive[0] = lerp(2.2, 5.8, hot);
      rig.flashCore.mat.emissive[1] = lerp(0.42, 4.0, Math.pow(hot, 1.4));
      rig.flashCore.mat.emissive[2] = lerp(0.01, 1.4, Math.pow(hot, 2.2));
      rig.flashCore.offsetMat = mat4FromTRS([x, 0.30 + fu * 0.34, z], quatY(blastT * 5.2), [fs, fs * 0.95, fs]);
      visible.push(rig.flashCore);
    }
    if (blastT < 0.24) {
      const su = blastT / 0.24;
      const spSc = su < 0.26 ? lerp(0.36, 1.56, Math.pow(su / 0.26, 0.62)) : lerp(1.56, 0.10, (su - 0.26) / 0.74);
      const hot = 1.0 - su;
      rig.flareSpikes.mat.emissive[0] = lerp(1.8, 4.4, hot);
      rig.flareSpikes.mat.emissive[1] = lerp(0.20, 1.55, Math.pow(hot, 1.3));
      rig.flareSpikes.mat.emissive[2] = lerp(0.01, 0.10, Math.pow(hot, 2.0));
      rig.flareSpikes.offsetMat = mat4FromTRS([x, 0.28 + su * 0.24, z], quatY(0.2 + blastT * 2.8), [spSc, spSc, spSc]);
      visible.push(rig.flareSpikes);
    }

    for (const pl of rig.firePlumes) {
      const dt = blastT - pl.delay;
      if (dt < 0 || dt >= pl.dur) continue;
      const u = dt / pl.dur;
      const grow = u < 0.32 ? smooth(u / 0.32) : (1.0 - Math.pow((u - 0.32) / 0.68, 1.30));
      const sc = pl.scPeak * (0.20 + 0.92 * grow);
      if (sc < 0.04) continue;
      const rad = lerp(pl.r0, pl.r1, Math.pow(u, 0.62));
      const py = lerp(pl.y0, pl.y1, Math.pow(u, 0.72));
      const cool = clamp01(u * 1.10);
      const m = pl.inst.mat;
      if (pl.inner) {
        m.baseColor[0] = lerp(3.0, 2.2, cool);
        m.baseColor[1] = lerp(1.30, 0.34, cool);
        m.baseColor[2] = lerp(0.12, 0.01, cool);
        m.emissive[0] = lerp(3.2, 1.30, cool);
        m.emissive[1] = lerp(1.25, 0.14, Math.pow(cool, 0.85));
        m.emissive[2] = lerp(0.08, 0.00, cool);
      } else {
        m.baseColor[0] = lerp(2.8, 1.9, cool);
        m.baseColor[1] = lerp(0.46, 0.10, cool);
        m.baseColor[2] = 0.005;
        m.emissive[0] = lerp(2.3, 0.85, cool);
        m.emissive[1] = lerp(0.32, 0.04, Math.pow(cool, 0.82));
        m.emissive[2] = 0.00;
      }
      const q = quatMul(quatY(pl.ang + u * pl.spin), quatFromAxisAngle(1, 0, 0, (pl.inner ? 0.14 : 0.36) * u));
      pl.inst.offsetMat = mat4FromTRS([x + Math.cos(pl.ang) * rad, py, z + Math.sin(pl.ang) * rad], q, [sc, sc * (pl.inner ? 1.22 : 0.96), sc]);
      visible.push(pl.inst);
    }

    if (blastT > 0.01 && blastT < 0.39) {
      const cu = (blastT - 0.01) / 0.38;
      const env = cu < 0.30 ? smooth(cu / 0.30) : (1.0 - Math.pow((cu - 0.30) / 0.70, 1.25));
      for (let k = 0; k < 2; k++) {
        const cr = rig.flameCrowns[k];
        const scXZ = lerp(0.44, 1.52 + k * 0.14, Math.pow(cu, 0.58)) * (0.35 + 0.74 * env);
        const scY = lerp(0.58, 1.28, Math.pow(cu, 0.55)) * (0.25 + 0.86 * env);
        if (scY < 0.06) continue;
        cr.mat.emissive[0] = lerp(2.5, 0.82, cu);
        cr.mat.emissive[1] = lerp(0.28, 0.03, Math.pow(cu, 0.78));
        cr.mat.emissive[2] = 0.00;
        cr.offsetMat = mat4FromTRS([x, 0.14 + k * 0.15 + cu * 0.28, z], quatY(k * 0.38 + (k === 0 ? 1 : -1) * cu * 1.15), [scXZ, scY, scXZ]);
        visible.push(cr);
      }
    }

    // --- STAGE 2: Crisp Fiery Ground Shockwave, Broken 1998 Fire Arcs & Subtle Dust Veil (0.03 -> 0.66s) ---
    if (blastT > 0.03 && blastT < 0.58) {
      const ru = (blastT - 0.03) / 0.55;
      const rRad = lerp(0.48, 2.60, Math.pow(ru, 0.56));
      const rH = lerp(1.25, 0.24, Math.pow(ru, 0.75));
      const alpha = smooth(clamp01(ru / 0.08)) * Math.pow(1.0 - smooth(clamp01((ru - 0.22) / 0.78)), 1.25);
      if (alpha > 0.03) {
        const m = rig.shockRing.mat;
        m.baseColor[3] = alpha * 0.94;
        m.emissive[0] = lerp(3.8, 0.62, ru) * alpha;
        m.emissive[1] = lerp(0.58, 0.04, Math.pow(ru, 0.80)) * alpha;
        m.emissive[2] = lerp(0.02, 0.00, ru) * alpha;
        rig.shockRing.offsetMat = mat4FromTRS([x, 0.0, z], quatY(-blastT * 1.1), [rRad, rH, rRad]);
        visible.push(rig.shockRing);
      }
    }

    if (blastT > 0.12 && blastT < 0.54) {
      const au = (blastT - 0.12) / 0.42;
      const aRad = lerp(0.70, 1.82, Math.pow(au, 0.62));
      const aEnv = au < 0.28 ? smooth(au / 0.28) : (1.0 - Math.pow((au - 0.28) / 0.72, 1.15));
      if (aEnv > 0.05) {
        const m = rig.fireArcs.mat;
        m.emissive[0] = lerp(2.8, 0.90, au);
        m.emissive[1] = lerp(0.68, 0.07, Math.pow(au, 0.78));
        m.emissive[2] = 0.005;
        rig.fireArcs.offsetMat = mat4FromTRS([x, 0.06 + au * 0.28, z], quatY(au * 0.60), [aRad, 0.35 + 1.15 * aEnv, aRad]);
        visible.push(rig.fireArcs);
      }
    }

    if (blastT > 0.12 && blastT < 0.66) {
      const du = (blastT - 0.12) / 0.54;
      const dRad = lerp(0.85, 2.85, Math.pow(du, 0.60));
      const dAlpha = smooth(clamp01(du / 0.20)) * (1.0 - smooth(clamp01((du - 0.34) / 0.66)));
      if (dAlpha > 0.03) {
        rig.dustWave.mat.baseColor[3] = dAlpha * 0.28;
        rig.dustWave.offsetMat = mat4FromTRS([x, 0.0, z], quatY(0.5 + du * 0.3), [dRad, lerp(1.0, 0.35, du), dRad]);
        visible.push(rig.dustWave);
      }
    }

    // --- STAGE 3: High-Velocity Golden Spark Shower, Crimson Shrapnel & Tumbling Dirt Clods (0.01 -> 0.86s) ---
    const gSpark = 11.0;
    for (const sp of rig.sparks) {
      const dt = blastT - sp.delay;
      if (dt <= 0 || dt >= sp.life) continue;
      const u = dt / sp.life;
      const dragT = (1.0 - Math.exp(-dt * sp.drag)) / sp.drag;
      const rH = 0.16 + sp.spdH * dragT;
      let py = 0.28 + sp.spdY * dt - 0.5 * gSpark * dt * dt;
      let vy = sp.spdY - gSpark * dt;
      if (py < 0.04) { py = 0.04 + Math.abs(py - 0.04) * 0.38; vy = Math.abs(vy) * 0.38; }
      const vH = sp.spdH * Math.exp(-dt * sp.drag);
      const speed = Math.hypot(vH, vy);
      const fade = 1.0 - Math.pow(u, 1.5);
      const wSc = sp.size * (0.48 + 0.52 * fade);
      const lSc = sp.size * clamp01(0.50 + speed * 0.26) * 2.6 * (0.42 + 0.58 * fade);
      const m = sp.inst.mat;
      m.emissive[0] = (sp.hot ? 5.2 : 4.0) * (0.38 + 0.62 * fade);
      m.emissive[1] = (sp.hot ? 3.0 : 1.05) * Math.pow(fade, 1.3);
      m.emissive[2] = (sp.hot ? 0.42 : 0.03) * Math.pow(fade, 2.2);
      sp.inst.offsetMat = mat4FromTRS([x + sp.ca * rH, py, z + sp.sa * rH], quatFromDirY(sp.ca * vH, vy, sp.sa * vH), [wSc, lSc, wSc]);
      visible.push(sp.inst);
    }

    const gDebris = 12.2;
    if (blastT > 0.01 && blastT < 0.80) {
      const dt = blastT - 0.01;
      const fade = 1.0 - smooth(clamp01((dt - 0.56) / 0.23));
      for (const sh of rig.shrapnel) {
        const dragT = (1.0 - Math.exp(-dt * 0.72)) / 0.72;
        const rH = 0.18 + sh.spdH * dragT;
        let py = 0.26 + sh.spdY * dt - 0.5 * gDebris * dt * dt;
        if (py < 0.05) py = 0.05 + Math.abs(py - 0.05) * 0.25;
        const sc = sh.scale * fade;
        if (sc < 0.06) continue;
        sh.inst.offsetMat = mat4FromTRS([x + sh.ca * rH, py, z + sh.sa * rH], quatFromAxisAngle(sh.ax, sh.ay, sh.az, dt * sh.spinRate), [sc, sc, sc]);
        visible.push(sh.inst);
      }
    }

    for (const dc of rig.dirt) {
      if (blastT <= 0.02 || blastT >= dc.life) continue;
      const dt = blastT - 0.02, u = dt / (dc.life - 0.02);
      const dragT = (1.0 - Math.exp(-dt * 0.65)) / 0.65;
      const rH = 0.20 + dc.spdH * dragT;
      let py = 0.14 + dc.spdY * dt - 0.5 * gDebris * dt * dt;
      if (py < 0.04) py = 0.04 + Math.abs(py - 0.04) * 0.22;
      const sc = dc.scale * (1.0 - smooth(clamp01((u - 0.72) / 0.28)));
      if (sc < 0.05) continue;
      dc.inst.offsetMat = mat4FromTRS([x + dc.ca * rH, py, z + dc.sa * rH], quatFromAxisAngle(dc.ax, dc.ay, dc.az, dt * dc.spinRate), [sc, sc, sc]);
      visible.push(dc.inst);
    }

    // --- STAGE 4: Seamless Volumetric Cauliflower Smoke Plumes (0.06 -> 1.20s, ZERO glass-bubble rims!) ---
    for (const sm of rig.smokeRing) {
      const dt = blastT - sm.delay;
      if (dt <= 0 || dt >= sm.dur) continue;
      const u = dt / sm.dur;
      const grow = u < 0.28 ? smooth(u / 0.28) : Math.pow(1.0 - smooth((u - 0.28) / 0.72), 1.25);
      const sc = sm.scPeak * (0.14 + 0.94 * grow);
      if (sc < 0.04) continue;
      const rad = lerp(sm.r0, sm.r1, Math.pow(u, 0.56));
      const py = lerp(sm.y0, sm.y1, Math.pow(u, 0.84));
      const fireLit = Math.pow(1.0 - smooth(clamp01(u / 0.34)), 1.6);
      const m = sm.inst.mat;
      m.emissive[0] = fireLit * 0.62;
      m.emissive[1] = fireLit * 0.14;
      m.emissive[2] = 0.0;
      const lift = smooth(clamp01((u - 0.10) / 0.42));
      m.baseColor[0] = lerp(0.84, 1.06, lift);
      m.baseColor[1] = lerp(0.82, 1.03, lift);
      m.baseColor[2] = lerp(0.84, 1.00, lift);
      sm.inst.offsetMat = mat4FromTRS([x + sm.ca * rad, py, z + sm.sa * rad], quatY(sm.ang + u * sm.spin), [sc, sc * 0.88, sc]);
      visible.push(sm.inst);
    }

    for (const cr of rig.smokeCrown) {
      const dt = blastT - cr.delay;
      if (dt <= 0 || dt >= cr.dur) continue;
      const u = dt / cr.dur;
      const grow = u < 0.32 ? smooth(u / 0.32) : Math.pow(1.0 - smooth((u - 0.32) / 0.68), 1.35);
      const sc = cr.scPeak * (0.14 + 0.94 * grow);
      if (sc < 0.04) continue;
      const rad = lerp(cr.r0, cr.r1, Math.pow(u, 0.62));
      const py = lerp(cr.y0, cr.y1, Math.pow(u, 0.72));
      const fireLit = Math.pow(1.0 - smooth(clamp01(u / 0.32)), 1.6);
      const m = cr.inst.mat;
      m.emissive[0] = fireLit * 0.72;
      m.emissive[1] = fireLit * 0.17;
      m.emissive[2] = 0.0;
      const lift = smooth(clamp01((u - 0.10) / 0.42));
      m.baseColor[0] = lerp(0.76, 1.04, lift);
      m.baseColor[1] = lerp(0.75, 1.01, lift);
      m.baseColor[2] = lerp(0.78, 0.98, lift);
      cr.inst.offsetMat = mat4FromTRS([x + cr.ca * rad, py, z + cr.sa * rad], quatY(cr.ang + u * cr.spin), [sc, sc * 0.94, sc]);
      visible.push(cr.inst);
    }

    for (const tf of rig.smokeTufts) {
      const dt = blastT - tf.delay;
      if (dt <= 0 || dt >= tf.dur) continue;
      const u = dt / tf.dur;
      const env = u < 0.24 ? smooth(u / 0.24) : Math.pow(1.0 - smooth((u - 0.24) / 0.76), 1.45);
      const sc = tf.scPeak * env;
      if (sc < 0.04) continue;
      const rad = lerp(tf.r0, tf.r1, Math.pow(u, 0.64));
      const py = lerp(tf.y0, tf.y1, Math.pow(u, 0.78));
      tf.inst.offsetMat = mat4FromTRS([x + tf.ca * rad, py, z + tf.sa * rad], quatY(tf.ang + u * 0.95), [sc, sc * 0.85, sc]);
      visible.push(tf.inst);
    }

    // --- Warm Blast Light Bloom on Terrain (`dynGlows`) ---
    if (blastT < 0.88) {
      const gu = blastT / 0.88;
      const flash = Math.exp(-blastT * 8.0);
      const rad = lerp(7.6, 3.4, gu);
      const strength = (1.0 - gu) * 2.5 + flash * 2.8;
      const gr = lerp(2.7, 2.0, gu) + flash * 0.7;
      const gg = lerp(1.30, 0.42, Math.pow(gu, 0.7)) + flash * 0.60;
      const gb = lerp(0.28, 0.04, Math.pow(gu, 0.6)) + flash * 0.35;
      dynGlows.push([x, z, rad, strength, gr, gg, gb]);
    }
  }

  window.__demoExplosion = {
    buildBombExplosionModels,
    createBombExplosionRig,
    updateBombExplosion,
  };
})();

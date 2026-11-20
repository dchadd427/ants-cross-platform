"use strict";

(() => {
  const ANT_CLASSES = ["worker", "combat", "thief", "bomber", "swimmer", "fire"];
  const ANT_LABELS = {
    worker: "Worker", combat: "Combat", thief: "Thief",
    bomber: "Bomber", swimmer: "Swimmer", fire: "Fire", lineup: "All 6 Lineup",
  };
  const srgbToLinear = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
  const rgbLinear = (rgb) => rgb.map(srgbToLinear);
  const TEAMS = {
    green: { label: "Green", team: rgbLinear([0.24, 0.44, 0.27]), dark: rgbLinear([0.16, 0.31, 0.19]) },
    red: { label: "Red", team: rgbLinear([0.52, 0.16, 0.18]), dark: rgbLinear([0.36, 0.10, 0.12]) },
    blue: { label: "Blue", team: rgbLinear([0.20, 0.32, 0.54]), dark: rgbLinear([0.13, 0.22, 0.39]) },
    black: { label: "Black", team: rgbLinear([0.35, 0.37, 0.42]), dark: rgbLinear([0.23, 0.25, 0.29]) },
  };
  const ANGLES = {
    "301": { label: "301 S", full: "301 S (Front)", yaw: 0, pitch: 50 },
    "201": { label: "201 SE", full: "201 SE (3/4)", yaw: -45, pitch: 50 },
    "901": { label: "901 E", full: "901 E (Side)", yaw: -90, pitch: 50 },
    "801": { label: "801 NE", full: "801 NE (Back 3/4)", yaw: -135, pitch: 50 },
    "701": { label: "701 N", full: "701 N (Back)", yaw: 180, pitch: 50 },
    hero: { label: "Hero", full: "Hero Showcase", yaw: -28, pitch: 22 },
  };
  const SHEET_ANGLES = ["301", "201", "901", "801", "701"];
  const DEG = Math.PI / 180;
  const STUDIO_PALETTE = {
    light: {
      bgTop: [0.855, 0.880, 0.918], bgMid: [0.958, 0.966, 0.978], bgBot: [0.780, 0.816, 0.862],
      plinthTop: [0.68, 0.71, 0.76], plinthSide: [0.42, 0.46, 0.52], plinthRing: [0.45, 0.49, 0.56],
      sky: [0.60, 0.67, 0.78], ground: [0.16, 0.17, 0.20],
    },
    dark: {
      bgTop: [0.075, 0.092, 0.128], bgMid: [0.138, 0.165, 0.215], bgBot: [0.055, 0.068, 0.095],
      plinthTop: [0.095, 0.112, 0.145], plinthSide: [0.048, 0.060, 0.082], plinthRing: [0.19, 0.24, 0.31],
      sky: [0.42, 0.50, 0.64], ground: [0.07, 0.08, 0.10],
    },
  };

  // --- Column-Major 3D Math & Quaternion Helpers ---
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const wrapAngleDeg = (deg) => { const a = ((deg + 180) % 360 + 360) % 360 - 180; return a === -180 ? 180 : a; };
  const mat4Identity = () => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
  const mat4Translate = (x, y, z) => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]);

  function mat4Multiply(a, b) {
    const out = new Float32Array(16);
    for (let c = 0; c < 4; c++) {
      const o = c * 4, b0 = b[o], b1 = b[o + 1], b2 = b[o + 2], b3 = b[o + 3];
      out[o] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
      out[o + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
      out[o + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
      out[o + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
    }
    return out;
  }

  function mat4FromTRS(t, q, s) {
    const [x, y, z, w] = q, [sx, sy, sz] = s;
    const xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    return new Float32Array([
      (1 - 2 * (yy + zz)) * sx, 2 * (xy + wz) * sx, 2 * (xz - wy) * sx, 0,
      2 * (xy - wz) * sy, (1 - 2 * (xx + zz)) * sy, 2 * (yz + wx) * sy, 0,
      2 * (xz + wy) * sz, 2 * (yz - wx) * sz, (1 - 2 * (xx + yy)) * sz, 0,
      t[0], t[1], t[2], 1,
    ]);
  }

  function mat4LookAt(eye, target, up) {
    let zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2], l = Math.hypot(zx, zy, zz) || 1;
    zx /= l; zy /= l; zz /= l;
    let xx = up[1] * zz - up[2] * zy, xy = up[2] * zx - up[0] * zz, xz = up[0] * zy - up[1] * zx;
    l = Math.hypot(xx, xy, xz) || 1; xx /= l; xy /= l; xz /= l;
    const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    return new Float32Array([
      xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0,
      -(xx * eye[0] + xy * eye[1] + xz * eye[2]), -(yx * eye[0] + yy * eye[1] + yz * eye[2]), -(zx * eye[0] + zy * eye[1] + zz * eye[2]), 1,
    ]);
  }

  function mat4Perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy * 0.5), nf = 1 / (near - far);
    return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0]);
  }

  function mat4Ortho(l, r, b, t, n, f) {
    const rl = 1 / (r - l), tb = 1 / (t - b), fn = 1 / (f - n);
    return new Float32Array([2 * rl, 0, 0, 0, 0, 2 * tb, 0, 0, 0, 0, -2 * fn, 0, -(r + l) * rl, -(t + b) * tb, -(f + n) * fn, 1]);
  }

  const mat3DetFromMat4 = (m) => m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) + m[8] * (m[1] * m[6] - m[2] * m[5]);
  const transformPoint = (m, x, y, z) => [m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]];

  function mat3NormalFromMat4(m) {
    const a00 = m[0], a01 = m[1], a02 = m[2], a10 = m[4], a11 = m[5], a12 = m[6], a20 = m[8], a21 = m[9], a22 = m[10];
    const b01 = a22 * a11 - a12 * a21, b11 = -a22 * a10 + a12 * a20, b21 = a21 * a10 - a11 * a20;
    const det = a00 * b01 + a01 * b11 + a02 * b21;
    if (Math.abs(det) < 1e-8) return new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1]);
    const inv = 1 / det;
    return new Float32Array([
      b01 * inv, (-a22 * a01 + a02 * a21) * inv, (a12 * a01 - a02 * a11) * inv,
      b11 * inv, (a22 * a00 - a02 * a20) * inv, (-a12 * a00 + a02 * a10) * inv,
      b21 * inv, (-a21 * a00 + a01 * a20) * inv, (a11 * a00 - a01 * a10) * inv,
    ]);
  }

  function quatSlerp(a, b, t) {
    let [ax, ay, az, aw] = a, [bx, by, bz, bw] = b;
    let dot = ax * bx + ay * by + az * bz + aw * bw;
    if (dot < 0) { dot = -dot; bx = -bx; by = -by; bz = -bz; bw = -bw; }
    let s0 = 1 - t, s1 = t;
    if (1 - dot > 1e-4) {
      const om = Math.acos(clamp(dot, -1, 1)), inv = 1 / Math.sin(om);
      s0 = Math.sin((1 - t) * om) * inv; s1 = Math.sin(t * om) * inv;
    }
    const rx = s0 * ax + s1 * bx, ry = s0 * ay + s1 * by, rz = s0 * az + s1 * bz, rw = s0 * aw + s1 * bw, l = Math.hypot(rx, ry, rz, rw) || 1;
    return [rx / l, ry / l, rz / l, rw / l];
  }

  const quatMul = (a, b) => [
    a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
  ];
  const quatAngle = (a, b) => 2 * Math.acos(clamp(Math.abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]), 0, 1));

  function sampleChannel(ch, t) {
    const times = ch.times, vals = ch.values, n = times.length;
    if (!n) return null;
    const dim = ch.path === "rotation" ? 4 : 3, cubic = ch.interpolation === "CUBICSPLINE";
    const at = (i) => {
      const off = (cubic ? i * 3 + 1 : i) * dim;
      return dim === 4 ? [vals[off], vals[off + 1], vals[off + 2], vals[off + 3]] : [vals[off], vals[off + 1], vals[off + 2]];
    };
    if (n === 1 || t <= times[0]) return at(0);
    if (t >= times[n - 1]) return at(n - 1);
    let idx = 0;
    while (idx + 1 < n - 1 && times[idx + 1] <= t) idx++;
    if (ch.interpolation === "STEP") return at(idx);
    const span = times[idx + 1] - times[idx], u = span > 1e-6 ? clamp((t - times[idx]) / span, 0, 1) : 0;
    const v0 = at(idx), v1 = at(idx + 1);
    return dim === 4 ? quatSlerp(v0, v1, u) : [v0[0] + (v1[0] - v0[0]) * u, v0[1] + (v1[1] - v0[1]) * u, v0[2] + (v1[2] - v0[2]) * u];
  }

  // --- glTF 2.0 Binary (.glb) Parser ---
  const TYPE_COMPS = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
  const COMP_SIZES = { 5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4 };
  const COMP_READERS = {
    5126: (dv, o) => dv.getFloat32(o, true),
    5125: (dv, o) => dv.getUint32(o, true),
    5123: (dv, o, n) => dv.getUint16(o, true) / (n ? 65535 : 1),
    5122: (dv, o, n) => (n ? Math.max(dv.getInt16(o, true) / 32767, -1) : dv.getInt16(o, true)),
    5121: (dv, o, n) => dv.getUint8(o) / (n ? 255 : 1),
    5120: (dv, o, n) => (n ? Math.max(dv.getInt8(o) / 127, -1) : dv.getInt8(o)),
  };

  function decodeDataUri(uri) {
    const idx = uri.indexOf("base64,");
    if (idx < 0) return null;
    const raw = atob(uri.slice(idx + 7)), bytes = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
    return bytes.buffer;
  }

  function readAccessor(gltf, buffers, accIdx) {
    const acc = gltf.accessors[accIdx], numComp = TYPE_COMPS[acc.type], compSize = COMP_SIZES[acc.componentType], count = acc.count;
    if (acc.bufferView === undefined) return new Float32Array(count * numComp);
    const bv = gltf.bufferViews[acc.bufferView], dv = new DataView(buffers[bv.buffer || 0]);
    const baseOff = (bv.byteOffset || 0) + (acc.byteOffset || 0), stride = bv.byteStride || numComp * compSize, norm = Boolean(acc.normalized);
    const out = !norm && acc.componentType === 5125 ? new Uint32Array(count * numComp)
      : (!norm && (acc.componentType === 5123 || acc.componentType === 5121) ? new Uint16Array(count * numComp) : new Float32Array(count * numComp));
    const read = COMP_READERS[acc.componentType];
    for (let i = 0; i < count; i++) {
      const row = baseOff + i * stride;
      for (let c = 0; c < numComp; c++) out[i * numComp + c] = read ? read(dv, row + c * compSize, norm) : 0;
    }
    return out;
  }

  function expandBounds(min, max, x, y, z) {
    if (x < min[0]) min[0] = x; if (x > max[0]) max[0] = x;
    if (y < min[1]) min[1] = y; if (y > max[1]) max[1] = y;
    if (z < min[2]) min[2] = z; if (z > max[2]) max[2] = z;
  }

  function computeFallbackNormals(pos, indices) {
    const norm = new Float32Array(pos.length), count = indices ? indices.length : pos.length / 3;
    for (let i = 0; i + 2 < count; i += 3) {
      const i0 = (indices ? indices[i] : i) * 3, i1 = (indices ? indices[i + 1] : i + 1) * 3, i2 = (indices ? indices[i + 2] : i + 2) * 3;
      const ax = pos[i1] - pos[i0], ay = pos[i1 + 1] - pos[i0 + 1], az = pos[i1 + 2] - pos[i0 + 2];
      const bx = pos[i2] - pos[i0], by = pos[i2 + 1] - pos[i0 + 1], bz = pos[i2 + 2] - pos[i0 + 2];
      const nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
      norm[i0] += nx; norm[i0 + 1] += ny; norm[i0 + 2] += nz;
      norm[i1] += nx; norm[i1 + 1] += ny; norm[i1 + 2] += nz;
      norm[i2] += nx; norm[i2 + 1] += ny; norm[i2 + 2] += nz;
    }
    for (let i = 0; i < norm.length; i += 3) {
      const l = Math.hypot(norm[i], norm[i + 1], norm[i + 2]) || 1;
      norm[i] /= l; norm[i + 1] /= l; norm[i + 2] /= l;
    }
    return norm;
  }

  function uploadBuffer(gl, target, data, attrLoc) {
    const buf = gl.createBuffer();
    gl.bindBuffer(target, buf); gl.bufferData(target, data, gl.STATIC_DRAW);
    if (attrLoc !== undefined) { gl.enableVertexAttribArray(attrLoc); gl.vertexAttribPointer(attrLoc, 3, gl.FLOAT, false, 0, 0); }
    return buf;
  }

  function parseGlb(arrayBuffer, gl) {
    if (arrayBuffer.byteLength < 20) throw new Error("File is too small to be a valid .glb");
    const dv = new DataView(arrayBuffer);
    const magic = dv.getUint32(0, true), version = dv.getUint32(4, true), totalLen = dv.getUint32(8, true);
    if (magic !== 0x46546c67) throw new Error("Invalid .glb header magic");
    if (version !== 2) throw new Error("Unsupported glTF version " + version);
    if (totalLen > arrayBuffer.byteLength) throw new Error("Truncated .glb buffer");

    let gltf = null, binSlice = null;
    for (let offset = 12; offset + 8 <= totalLen;) {
      const chunkLen = dv.getUint32(offset, true), chunkType = dv.getUint32(offset + 4, true), start = offset + 8;
      if (start + chunkLen > totalLen) throw new Error("Truncated .glb chunk");
      if (chunkType === 0x4e4f534a) gltf = JSON.parse(new TextDecoder("utf-8").decode(new Uint8Array(arrayBuffer, start, chunkLen)));
      else if (chunkType === 0x004e4942) binSlice = arrayBuffer.slice(start, start + chunkLen);
      offset = start + chunkLen;
    }
    if (!gltf) throw new Error("Missing JSON chunk in .glb");

    const buffers = (gltf.buffers || []).map((b, i) => (!b.uri && i === 0 && binSlice ? binSlice : (b.uri && b.uri.startsWith("data:") ? decodeDataUri(b.uri) : binSlice || new ArrayBuffer(0))));
    if (!buffers.length && binSlice) buffers.push(binSlice);

    const defaultMat = { name: "default", baseColor: [0.75, 0.78, 0.82, 1], metallic: 0.05, roughness: 0.4, emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: false, isTranslucent: false };
    const materials = (gltf.materials || []).map((m, i) => {
      const pbr = m.pbrMetallicRoughness || {}, bc = Array.isArray(pbr.baseColorFactor) ? pbr.baseColorFactor : [1, 1, 1, 1];
      const name = m.name || ("material_" + i);
      let alpha = bc.length > 3 ? bc[3] : 1, alphaMode = m.alphaMode || "OPAQUE", doubleSided = Boolean(m.doubleSided);
      if (alphaMode === "OPAQUE" && alpha >= 0.99) {
        if (name === "glass_tinted") { alpha = 0.30; alphaMode = "BLEND"; doubleSided = true; }
        else if (name === "glass_clear") { alpha = 0.22; alphaMode = "BLEND"; doubleSided = true; }
      }
      return {
        name, baseColor: [bc[0], bc[1], bc[2], alpha],
        metallic: typeof pbr.metallicFactor === "number" ? pbr.metallicFactor : 0.05,
        roughness: typeof pbr.roughnessFactor === "number" ? pbr.roughnessFactor : 0.45,
        emissive: Array.isArray(m.emissiveFactor) ? m.emissiveFactor : [0, 0, 0],
        alphaMode, doubleSided, isTranslucent: alphaMode === "BLEND" || alpha < 0.99,
      };
    });

    const uploadedMeshes = (gltf.meshes || []).map((mesh) => (mesh.primitives || []).map((prim) => {
      if (prim.attributes.POSITION === undefined) return null;
      const pos = readAccessor(gltf, buffers, prim.attributes.POSITION);
      const indices = prim.indices !== undefined ? readAccessor(gltf, buffers, prim.indices) : null;
      const norm = prim.attributes.NORMAL !== undefined ? readAccessor(gltf, buffers, prim.attributes.NORMAL) : computeFallbackNormals(pos, indices);
      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      const glBufs = [uploadBuffer(gl, gl.ARRAY_BUFFER, pos, 0), uploadBuffer(gl, gl.ARRAY_BUFFER, norm, 1)];
      let indexType = 0, drawCount = pos.length / 3;
      if (indices) {
        glBufs.push(uploadBuffer(gl, gl.ELEMENT_ARRAY_BUFFER, indices));
        indexType = indices instanceof Uint32Array ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT; drawCount = indices.length;
      }
      gl.bindVertexArray(null);
      const lmin = [Infinity, Infinity, Infinity], lmax = [-Infinity, -Infinity, -Infinity];
      for (let i = 0; i < pos.length; i += 3) expandBounds(lmin, lmax, pos[i], pos[i + 1], pos[i + 2]);
      const mode = prim.mode !== undefined ? prim.mode : gl.TRIANGLES;
      return {
        vao, buffers: glBufs, indexed: Boolean(indices), indexType, drawCount, mode,
        triCount: mode === gl.TRIANGLES ? Math.floor(drawCount / 3) : 0,
        material: prim.material !== undefined ? materials[prim.material] || defaultMat : defaultMat,
        localMin: lmin, localMax: lmax,
      };
    }).filter(Boolean));

    const nodes = gltf.nodes || [], scene = gltf.scenes && gltf.scenes[gltf.scene || 0];
    const rootIndices = scene && Array.isArray(scene.nodes) ? scene.nodes : nodes.map((_, i) => i);
    const drawCalls = [], min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    const nodeDefs = new Array(nodes.length), baseWorldMats = new Array(nodes.length), nodeOrder = [], eyelidNodes = [];
    let nodeCount = 0, triCount = 0;

    function walkNode(nodeIdx, parentIdx, parentMat) {
      const node = nodes[nodeIdx];
      if (!node) return;
      nodeCount++;
      const name = node.name || "";
      if (/eyelid/i.test(name)) eyelidNodes.push(nodeIdx);
      const hasMat = Array.isArray(node.matrix) && node.matrix.length === 16;
      const t = node.translation || [0, 0, 0], r = node.rotation || [0, 0, 0, 1], s = node.scale || [1, 1, 1];
      const localMat = hasMat ? new Float32Array(node.matrix) : mat4FromTRS(t, r, s);
      const worldMat = mat4Multiply(parentMat, localMat);
      nodeDefs[nodeIdx] = { name, parent: parentIdx, matrix: hasMat ? localMat : null, t, r, s };
      baseWorldMats[nodeIdx] = worldMat; nodeOrder.push(nodeIdx);
      if (node.mesh !== undefined && uploadedMeshes[node.mesh]) {
        for (const prim of uploadedMeshes[node.mesh]) {
          triCount += prim.triCount; drawCalls.push({ prim, nodeIndex: nodeIdx, worldMat });
          for (let c = 0; c < 8; c++) {
            const p = transformPoint(worldMat, (c & 1) ? prim.localMax[0] : prim.localMin[0], (c & 2) ? prim.localMax[1] : prim.localMin[1], (c & 4) ? prim.localMax[2] : prim.localMin[2]);
            expandBounds(min, max, p[0], p[1], p[2]);
          }
        }
      }
      if (Array.isArray(node.children)) node.children.forEach((child) => walkNode(child, nodeIdx, worldMat));
    }
    rootIndices.forEach((root) => walkNode(root, -1, mat4Identity()));
    if (!Number.isFinite(min[0])) { min.splice(0, 3, -0.5, 0, -0.5); max.splice(0, 3, 0.5, 1, 0.5); }

    const animations = (gltf.animations || []).map((anim) => {
      let duration = 0;
      const channels = (anim.channels || []).map((ch) => {
        const tgt = ch.target, smp = (anim.samplers || [])[ch.sampler];
        if (!tgt || tgt.node === undefined || !tgt.path || !smp) return null;
        const times = readAccessor(gltf, buffers, smp.input), values = readAccessor(gltf, buffers, smp.output);
        if (times.length) duration = Math.max(duration, times[times.length - 1]);
        return { nodeIndex: tgt.node, path: tgt.path, times, values, interpolation: smp.interpolation || "LINEAR" };
      }).filter(Boolean);
      return { name: anim.name || "", duration, channels };
    });

    const eyelidSet = new Set(eyelidNodes);
    const blinkAnim = animations.find((a) => /blink/i.test(a.name))
      || animations.find((a) => a.channels.some((c) => eyelidSet.has(c.nodeIndex))) || null;
    const blinkInfo = { anim: blinkAnim, eyelidNodes, closedTime: 0, startTime: 0, endTime: 0.24, hasBlink: Boolean(blinkAnim || eyelidNodes.length) };
    if (blinkAnim && blinkAnim.channels.length) {
      const refCh = blinkAnim.channels.find((c) => c.path === "rotation") || blinkAnim.channels[0];
      const restQ = (nodeDefs[refCh.nodeIndex] && nodeDefs[refCh.nodeIndex].r) || [0, 0, 0, 1], times = refCh.times;
      let maxAng = -1, pFirst = 0, pLast = 0, sIdx = 0, eIdx = times.length - 1;
      const angs = Array.from(times, (tm) => { const q = refCh.path === "rotation" ? sampleChannel(refCh, tm) : null; return q ? quatAngle(q, restQ) : 0; });
      angs.forEach((a, i) => { if (a > maxAng + 1e-4) { maxAng = a; pFirst = pLast = i; } else if (Math.abs(a - maxAng) <= 1e-4) pLast = i; });
      for (let i = pFirst; i >= 0; i--) if (angs[i] < 1e-3) { sIdx = i; break; }
      for (let i = pLast; i < times.length; i++) if (angs[i] < 1e-3) { eIdx = i; break; }
      blinkInfo.closedTime = (times[pFirst] + times[pLast]) * 0.5;
      blinkInfo.startTime = times[sIdx] || 0;
      blinkInfo.endTime = Math.max(blinkInfo.startTime + 0.08, times[eIdx] || blinkAnim.duration || 0.24);
    }

    const isBlend = (dc) => (dc.prim.material.alphaMode === "BLEND" || dc.prim.material.baseColor[3] < 0.99 ? 1 : 0);
    drawCalls.sort((a, b) => isBlend(a) - isBlend(b));
    return {
      drawCalls, nodeDefs, nodeOrder, baseWorldMats, animations, blinkInfo, bounds: { min, max },
      size: [max[0] - min[0], max[1] - min[1], max[2] - min[2]],
      center: [(min[0] + max[0]) * 0.5, (min[1] + max[1]) * 0.5, (min[2] + max[2]) * 0.5],
      triCount, meshCount: uploadedMeshes.length, nodeCount, rawBytes: arrayBuffer,
      dispose() { for (const prims of uploadedMeshes) for (const p of prims) { gl.deleteVertexArray(p.vao); p.buffers.forEach((b) => gl.deleteBuffer(b)); } },
    };
  }

  // --- WebGL2 Shaders (Studio Backdrop, Plinth + Contact Shadows, Sculpted Studio PBR) ---
  const BACKDROP_VS = `#version 300 es
  out vec2 v_uv;
  void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.9999, 1.0); }`;

  const BACKDROP_FS = `#version 300 es
  precision highp float;
  in vec2 v_uv;
  uniform vec3 u_topColor, u_midColor, u_botColor;
  out vec4 fragColor;
  void main() {
    float rad = clamp(length((v_uv - vec2(0.5, 0.48)) * vec2(1.15, 1.0)) * 1.15, 0.0, 1.0);
    fragColor = vec4(mix(u_midColor, mix(u_botColor, u_topColor, smoothstep(0.0, 1.0, v_uv.y)), smoothstep(0.05, 0.95, rad)), 1.0);
  }`;

  const PBR_VS = `#version 300 es
  layout(location = 0) in vec3 a_position;
  layout(location = 1) in vec3 a_normal;
  uniform mat4 u_model, u_viewProj;
  uniform mat3 u_normalMat;
  uniform vec3 u_instOffset;
  out vec3 v_worldPos, v_worldNormal, v_samplePos;
  void main() {
    vec4 wp = u_model * vec4(a_position, 1.0);
    v_worldPos = wp.xyz; v_samplePos = wp.xyz - u_instOffset;
    v_worldNormal = normalize(u_normalMat * a_normal);
    gl_Position = u_viewProj * wp;
  }`;

  const PBR_FS = `#version 300 es
  precision highp float;
  const float PI = 3.14159265359;
  in vec3 v_worldPos, v_worldNormal, v_samplePos;
  uniform vec3 u_cameraPos, u_viewDir, u_baseColor, u_emissive, u_keyDir, u_fillDir, u_rimDir, u_skyColor, u_groundColor;
  uniform float u_alpha, u_metallic, u_roughness, u_noiseSeed;
  uniform int u_isOrtho, u_surfaceKind;
  out vec4 fragColor;

  float hash31(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
  }
  float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
      mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), f.x), mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x), f.y),
      mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x), mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), f.x), f.y), f.z);
  }
  vec3 bumpNormal(vec3 N, vec3 p, float strength) {
    float c = vnoise(p), e = 0.08;
    vec3 g = vec3(vnoise(p + vec3(e, 0.0, 0.0)), vnoise(p + vec3(0.0, e, 0.0)), vnoise(p + vec3(0.0, 0.0, e))) - c;
    g -= N * dot(g, N);
    return normalize(N - g * strength);
  }
  float D_GGX(float NoH, float a) { float a2 = a * a, d = (NoH * NoH) * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-6); }
  float G_Smith(float NoV, float NoL, float r) {
    float k = ((r + 1.0) * (r + 1.0)) / 8.0;
    return (NoV / (NoV * (1.0 - k) + k + 1e-6)) * (NoL / (NoL * (1.0 - k) + k + 1e-6));
  }
  vec3 F_Schlick(float c, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0); }
  vec3 studioEnv(vec3 R, float rough) {
    vec3 env = mix(u_groundColor * 0.55, u_skyColor * 1.05, smoothstep(0.18, 0.92, clamp(R.y * 0.5 + 0.5, 0.0, 1.0)));
    float sharp = mix(1.0, 0.14, rough);
    return env + vec3(1.0, 0.97, 0.92) * smoothstep(0.68, 0.97, dot(R, u_keyDir)) * 1.25 * sharp
               + vec3(0.82, 0.90, 1.0) * smoothstep(0.76, 0.98, dot(R, u_rimDir)) * 0.52 * sharp;
  }
  vec3 evalLight(vec3 L, vec3 lCol, vec3 N, vec3 V, vec3 albedo, float met, float rough, vec3 F0, float coatW, inout vec3 specAcc) {
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4), NoH = max(dot(N, H), 0.0), HoV = max(dot(H, V), 0.0);
    vec3 F = F_Schlick(HoV, F0);
    vec3 spec = (D_GGX(NoH, max(rough * rough, 0.045)) * G_Smith(NoV, NoL, rough) * F) / (4.0 * NoV * NoL + 1e-4);
    vec3 diff = (vec3(1.0) - F) * (1.0 - met) * albedo / PI;
    float coat = coatW * (D_GGX(NoH, 0.0484) * G_Smith(NoV, NoL, 0.22) * (0.04 + 0.96 * pow(1.0 - HoV, 5.0))) / (4.0 * NoV * NoL + 1e-4);
    vec3 specTerm = (spec + vec3(coat)) * lCol * NoL;
    specAcc += specTerm;
    return diff * lCol * NoL + specTerm;
  }
  vec3 acesFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
  vec3 linearToSrgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }

  void main() {
    vec3 N = normalize(v_worldNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = u_isOrtho == 1 ? normalize(u_viewDir) : normalize(u_cameraPos - v_worldPos);
    float rough = clamp(u_roughness, 0.06, 0.98), met = clamp(u_metallic, 0.0, 1.0);
    vec3 albedo = max(u_baseColor, 0.0), tp = v_samplePos + vec3(u_noiseSeed, 0.0, u_noiseSeed * 0.61);
    float coatW = 0.04;
    if (u_surfaceKind == 0) {
      coatW = 0.18 * (1.0 - smoothstep(0.22, 0.55, rough));
    } else if (u_surfaceKind == 1) {
      float m1 = vnoise(tp * 6.5), m2 = vnoise(tp * 16.0 + 7.1), m3 = vnoise(tp * 40.0 + 19.3);
      float mottle = m1 * 0.52 + m2 * 0.33 + m3 * 0.15;
      albedo *= mix(vec3(0.72, 0.74, 0.68), vec3(1.18, 1.20, 1.08), smoothstep(0.26, 0.76, mottle));
      float dust = smoothstep(0.50, 0.88, m1 * 0.68 + m2 * 0.32) * max(N.y, 0.0);
      albedo = mix(albedo, albedo * 1.28 + vec3(0.025, 0.028, 0.020), dust * 0.32);
      rough = clamp(rough + (m2 - 0.5) * 0.16 + (m3 - 0.5) * 0.08, 0.28, 0.94);
      N = bumpNormal(N, tp * 30.0, 0.38);
      coatW = 0.10 * (1.0 - smoothstep(0.52, 0.88, rough));
    } else if (u_surfaceKind == 2) {
      float weave = vnoise(tp * 52.0) * 0.62 + vnoise(tp * 14.0 + 3.7) * 0.38;
      albedo *= mix(0.84, 1.10, weave);
      rough = clamp(rough + (weave - 0.5) * 0.12, 0.32, 0.96);
      N = bumpNormal(N, tp * 48.0, 0.26);
      coatW = 0.03 * (1.0 - smoothstep(0.42, 0.72, rough));
    } else {
      float brush = vnoise(tp * vec3(24.0, 56.0, 24.0)) * 0.65 + vnoise(tp * 10.0) * 0.35;
      albedo *= mix(0.86, 1.08, brush);
      rough = clamp(rough + (brush - 0.5) * 0.14, 0.14, 0.78);
    }
    vec3 F0 = mix(vec3(0.04), albedo, met), specAcc = vec3(0.0);
    vec3 Lo = evalLight(u_keyDir, vec3(1.0, 0.97, 0.92) * 3.45, N, V, albedo, met, rough, F0, coatW, specAcc)
            + evalLight(u_fillDir, vec3(0.70, 0.82, 0.98) * 0.54, N, V, albedo, met, rough, F0, coatW, specAcc)
            + evalLight(u_rimDir, vec3(0.88, 0.94, 1.0) * 1.22, N, V, albedo, met, rough, F0, coatW, specAcc);
    float ao = mix(0.64, 1.0, smoothstep(0.0, 0.32, v_worldPos.y)) * mix(0.56, 1.0, smoothstep(-0.35, 0.55, N.y));
    vec3 ambDiff = mix(u_groundColor, u_skyColor, N.y * 0.5 + 0.5) * albedo * (1.0 - met) * 0.30;
    vec3 R = reflect(-V, N); float NoV = max(dot(N, V), 0.0);
    vec3 F_env = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NoV, 5.0);
    vec3 envSpec = studioEnv(R, rough) * F_env * mix(0.88, 0.14, rough);
    vec3 envCoat = studioEnv(R, 0.22) * ((0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coatW) * 0.26;
    vec3 outCol = linearToSrgb(acesFilm((Lo + (ambDiff + envSpec + envCoat) * ao + u_emissive) * 1.03));
    float outAlpha = u_alpha;
    if (u_alpha < 0.99) {
      float specLuma = dot(acesFilm((specAcc + (envSpec + envCoat) * ao) * 1.03), vec3(0.2126, 0.7152, 0.0722));
      outAlpha = clamp(u_alpha + pow(1.0 - NoV, 3.0) * 0.42 + specLuma * 0.55, 0.0, 0.90);
    }
    fragColor = vec4(outCol, outAlpha);
  }`;

  const PLINTH_VS = `#version 300 es
  layout(location = 0) in vec3 a_position;
  layout(location = 1) in vec3 a_normal;
  uniform mat4 u_viewProj;
  out vec3 v_worldPos, v_worldNormal;
  void main() { v_worldPos = a_position; v_worldNormal = a_normal; gl_Position = u_viewProj * vec4(a_position, 1.0); }`;

  const PLINTH_FS = `#version 300 es
  precision highp float;
  in vec3 v_worldPos, v_worldNormal;
  uniform vec3 u_topBase, u_sideBase, u_ringColor, u_keyDir;
  uniform vec4 u_shadows[6];
  uniform int u_shadowCount;
  uniform float u_halfSpanX, u_radius;
  out vec4 fragColor;
  void main() {
    vec3 N = normalize(v_worldNormal);
    float isTop = smoothstep(0.65, 0.95, N.y);
    vec3 base = mix(u_sideBase, u_topBase, isTop) * (max(dot(N, u_keyDir), 0.0) * 0.48 + 0.58);
    if (isTop > 0.5) {
      float rNorm = length(vec2(max(abs(v_worldPos.x) - u_halfSpanX, 0.0), v_worldPos.z)) / max(u_radius, 0.01);
      float rings = (1.0 - smoothstep(0.008, 0.022, abs(rNorm - 0.88))) * 0.45 + (1.0 - smoothstep(0.006, 0.018, abs(rNorm - 0.58))) * 0.25;
      base = mix(base, u_ringColor, rings * isTop);
      float shadow = 0.0;
      for (int i = 0; i < 6; i++) {
        if (i >= u_shadowCount) break;
        vec2 q = (v_worldPos.xz - u_shadows[i].xy) / max(u_shadows[i].zw, vec2(0.05));
        float d2 = dot(q, q);
        shadow = max(shadow, exp(-d2 * 2.5) * 0.52 + exp(-d2 * 0.85) * 0.24);
      }
      base *= (1.0 - clamp(shadow, 0.0, 0.66));
    }
    fragColor = vec4(pow(clamp(base, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
  }`;

  function createProgram(gl, vsSrc, fsSrc) {
    const mk = (type, src) => {
      const sh = gl.createShader(type); gl.shaderSource(sh, src); gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error("Shader compile error: " + gl.getShaderInfoLog(sh));
      return sh;
    };
    const vs = mk(gl.VERTEX_SHADER, vsSrc), fs = mk(gl.FRAGMENT_SHADER, fsSrc), prog = gl.createProgram();
    gl.attachShader(prog, vs); gl.attachShader(prog, fs); gl.linkProgram(prog);
    gl.deleteShader(vs); gl.deleteShader(fs);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error("Program link error: " + gl.getProgramInfoLog(prog));
    return prog;
  }

  function getUniforms(gl, prog, names) {
    const out = {};
    for (const name of names) out[name] = gl.getUniformLocation(prog, name);
    return out;
  }

  function buildPlinthMesh(gl, halfSpanX, radius) {
    const halfSegs = 24, perim = [];
    for (const [cx, startA] of [[halfSpanX, -Math.PI * 0.5], [-halfSpanX, Math.PI * 0.5]]) {
      for (let i = 0; i <= halfSegs; i++) {
        const a = startA + (i / halfSegs) * Math.PI;
        perim.push([cx + Math.cos(a) * radius, Math.sin(a) * radius]);
      }
    }
    const n = perim.length, pos = [0, -0.002, 0], norm = [0, 1, 0], idx = [], scaleTop = (radius - 0.014) / radius;
    for (let i = 0; i < n; i++) {
      const [px, pz] = perim[i], cx = clamp(px, -halfSpanX, halfSpanX);
      pos.push(cx + (px - cx) * scaleTop, -0.002, pz * scaleTop); norm.push(0, 1, 0); idx.push(0, 1 + ((i + 1) % n), 1 + i);
    }
    const ringStart = pos.length / 3;
    for (const [ry, inset, ny] of [[-0.002, 0.014, 0.65], [-0.014, 0, 0.45], [-0.014, 0, 0], [-0.068, -0.012, 0]]) {
      for (const [px, pz] of perim) {
        const cx = clamp(px, -halfSpanX, halfSpanX), rx = px - cx, rl = Math.hypot(rx, pz) || 1;
        const ux = rx / rl, uz = pz / rl, rad = radius - inset, nl = Math.hypot(ux, ny, uz) || 1;
        pos.push(cx + ux * rad, ry, uz * rad); norm.push(ux / nl, ny / nl, uz / nl);
      }
    }
    for (const [a, b] of [[0, 1], [2, 3]]) {
      const r0 = ringStart + a * n, r1 = ringStart + b * n;
      for (let i = 0; i < n; i++) { const nx = (i + 1) % n; idx.push(r0 + i, r0 + nx, r1 + nx, r0 + i, r1 + nx, r1 + i); }
    }
    const vao = gl.createVertexArray(); gl.bindVertexArray(vao);
    const bufs = [
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(pos), 0),
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(norm), 1),
      uploadBuffer(gl, gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx)),
    ];
    gl.bindVertexArray(null);
    return { vao, count: idx.length, halfSpanX, radius, dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); } };
  }

  function surfaceKindFor(mat) {
    const n = mat.name || "";
    if (n === "chitin_team" || n === "chitin_dark") return 1;
    if (n.startsWith("eye_") || n.startsWith("glass_") || (mat.metallic < 0.35 && mat.roughness < 0.22)) return 0;
    if (mat.metallic >= 0.40 || n === "gear_steel" || n === "gear_gold_metal" || n === "gear_brass_trim") return 3;
    return 2;
  }

  function evaluateWorldMats(inst, blinkMode) {
    const m = inst.model, bi = m.blinkInfo;
    if (!bi || !bi.hasBlink) return m.baseWorldMats;
    const isClosed = blinkMode === "closed", isPulsing = !isClosed && inst.blink && inst.blink.active;
    if (!isClosed && !isPulsing) return m.baseWorldMats;

    const overrides = new Map();
    if (bi.anim && bi.anim.channels.length) {
      const t = isClosed ? bi.closedTime : Math.min(bi.endTime, bi.startTime + inst.blink.elapsed);
      for (const ch of bi.anim.channels) {
        const val = sampleChannel(ch, t);
        if (!val) continue;
        let ov = overrides.get(ch.nodeIndex);
        if (!ov) { ov = {}; overrides.set(ch.nodeIndex, ov); }
        ov[ch.path] = val;
      }
    } else if (bi.eyelidNodes.length) {
      const u = inst.blink ? inst.blink.elapsed / 0.22 : 1;
      const amt = isClosed ? 1 : (u < 0.35 ? u / 0.35 : (u < 0.60 ? 1 : Math.max(0, (1 - u) / 0.40)));
      const qx = [Math.sin(0.575 * amt), 0, 0, Math.cos(0.575 * amt)];
      for (const ni of bi.eyelidNodes) {
        const nd = m.nodeDefs[ni];
        overrides.set(ni, { rotation: quatMul(nd ? nd.r : [0, 0, 0, 1], qx) });
      }
    }
    if (!overrides.size) return m.baseWorldMats;
    const out = m.baseWorldMats.slice(), dirty = new Set();
    for (const ni of m.nodeOrder) {
      const nd = m.nodeDefs[ni], ov = overrides.get(ni);
      if (!nd || (!ov && !dirty.has(nd.parent))) continue;
      dirty.add(ni);
      const local = ov ? mat4FromTRS(ov.translation || nd.t, ov.rotation || nd.r, ov.scale || nd.s) : (nd.matrix || mat4FromTRS(nd.t, nd.r, nd.s));
      out[ni] = nd.parent >= 0 ? mat4Multiply(out[nd.parent], local) : local;
    }
    return out;
  }

  // --- Studio WebGL2 Renderer ---
  class AntRenderer {
    constructor(canvas) {
      this.canvas = canvas;
      const gl = canvas.getContext("webgl2", { antialias: true, alpha: true, premultipliedAlpha: false, preserveDrawingBuffer: true });
      if (!gl) throw new Error("WebGL2 is not available in this browser");
      this.gl = gl;
      this.backdropProg = createProgram(gl, BACKDROP_VS, BACKDROP_FS);
      this.pbrProg = createProgram(gl, PBR_VS, PBR_FS);
      this.plinthProg = createProgram(gl, PLINTH_VS, PLINTH_FS);
      this.emptyVao = gl.createVertexArray();
      this.plinth = null; this.plinthKey = "";
      this.uBack = getUniforms(gl, this.backdropProg, ["u_topColor", "u_midColor", "u_botColor"]);
      this.uPbr = getUniforms(gl, this.pbrProg, [
        "u_model", "u_normalMat", "u_viewProj", "u_instOffset", "u_cameraPos", "u_viewDir", "u_isOrtho",
        "u_baseColor", "u_alpha", "u_metallic", "u_roughness", "u_emissive", "u_noiseSeed", "u_surfaceKind",
        "u_keyDir", "u_fillDir", "u_rimDir", "u_skyColor", "u_groundColor",
      ]);
      this.uPlinth = getUniforms(gl, this.plinthProg, [
        "u_viewProj", "u_topBase", "u_sideBase", "u_ringColor", "u_keyDir",
        "u_shadows[0]", "u_shadowCount", "u_halfSpanX", "u_radius",
      ]);
    }

    ensurePlinth(halfSpanX, radius) {
      const key = halfSpanX.toFixed(2) + ":" + radius.toFixed(2);
      if (this.plinthKey === key && this.plinth) return this.plinth;
      if (this.plinth) this.plinth.dispose();
      this.plinth = buildPlinthMesh(this.gl, halfSpanX, radius); this.plinthKey = key;
      return this.plinth;
    }

    resize() {
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.max(1, Math.floor(this.canvas.clientWidth * dpr)), h = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
      if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
      return [w, h];
    }

    computeCamera(scene, cam, w, h, overrideProj) {
      const aspect = Math.max(0.2, w / Math.max(1, h)), yaw = cam.yaw * DEG, pitch = cam.pitch * DEG, t = scene.target;
      const isOrtho = overrideProj ? overrideProj === "ortho" : cam.projection === "ortho", fovy = 32 * DEG;
      const fitHalfH = Math.max(scene.fitRadius, (scene.halfWidth / aspect) * 1.06) * cam.zoom;
      const dist = isOrtho ? Math.max(6.0, fitHalfH * 4.5) : (fitHalfH / Math.tan(fovy * 0.5)) * 1.16;
      const cp = Math.cos(pitch), sp = Math.sin(pitch), sy = Math.sin(yaw), cy = Math.cos(yaw);
      const eye = [t[0] + dist * cp * sy, t[1] + dist * sp, t[2] + dist * cp * cy];
      const proj = isOrtho
        ? mat4Ortho(-fitHalfH * aspect, fitHalfH * aspect, -fitHalfH, fitHalfH, 0.05, dist * 3.0)
        : mat4Perspective(fovy, aspect, 0.05, dist * 4.0);
      const dx = eye[0] - t[0], dy = eye[1] - t[1], dz = eye[2] - t[2], l = Math.hypot(dx, dy, dz) || 1;
      return { eye, viewProj: mat4Multiply(proj, mat4LookAt(eye, t, [0, 1, 0])), viewDir: [dx / l, dy / l, dz / l], isOrtho, yaw };
    }

    rotateLight(lx, ly, lz, yaw) {
      const c = Math.cos(yaw), s = Math.sin(yaw), rx = lx * c + lz * s, rz = -lx * s + lz * c, l = Math.hypot(rx, ly, rz) || 1;
      return [rx / l, ly / l, rz / l];
    }

    drawScene(scene, cam, opts) {
      const gl = this.gl, pal = opts.dark ? STUDIO_PALETTE.dark : STUDIO_PALETTE.light, team = TEAMS[opts.team] || TEAMS.green;
      gl.viewport(0, 0, opts.width, opts.height); gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      if (!opts.transparent) {
        gl.disable(gl.DEPTH_TEST); gl.depthMask(false); gl.disable(gl.BLEND); gl.disable(gl.CULL_FACE);
        gl.useProgram(this.backdropProg);
        gl.uniform3fv(this.uBack.u_topColor, pal.bgTop); gl.uniform3fv(this.uBack.u_midColor, pal.bgMid); gl.uniform3fv(this.uBack.u_botColor, pal.bgBot);
        gl.bindVertexArray(this.emptyVao); gl.drawArrays(gl.TRIANGLES, 0, 3); gl.bindVertexArray(null);
      }
      gl.enable(gl.DEPTH_TEST); gl.depthMask(true);
      const camera = this.computeCamera(scene, cam, opts.width, opts.height, opts.overrideProj);
      const keyDir = this.rotateLight(-0.58, 0.86, 0.56, camera.yaw);
      const fillDir = this.rotateLight(0.75, 0.40, 0.35, camera.yaw);
      const rimDir = this.rotateLight(0.38, 0.56, -0.82, camera.yaw);

      if (opts.showPlinth !== false && !opts.transparent) {
        const plinth = this.ensurePlinth(scene.plinthSpanX, scene.plinthRadius);
        gl.disable(gl.BLEND); gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); gl.frontFace(gl.CCW);
        gl.useProgram(this.plinthProg);
        gl.uniformMatrix4fv(this.uPlinth.u_viewProj, false, camera.viewProj);
        gl.uniform3fv(this.uPlinth.u_topBase, pal.plinthTop); gl.uniform3fv(this.uPlinth.u_sideBase, pal.plinthSide);
        gl.uniform3fv(this.uPlinth.u_ringColor, pal.plinthRing); gl.uniform3fv(this.uPlinth.u_keyDir, keyDir);
        const sData = new Float32Array(24), sCount = Math.min(scene.shadows.length, 6);
        for (let i = 0; i < sCount; i++) sData.set(scene.shadows[i], i * 4);
        gl.uniform4fv(this.uPlinth["u_shadows[0]"], sData); gl.uniform1i(this.uPlinth.u_shadowCount, sCount);
        gl.uniform1f(this.uPlinth.u_halfSpanX, plinth.halfSpanX); gl.uniform1f(this.uPlinth.u_radius, plinth.radius);
        gl.bindVertexArray(plinth.vao); gl.drawElements(gl.TRIANGLES, plinth.count, gl.UNSIGNED_SHORT, 0); gl.bindVertexArray(null);
      }

      if (!scene.instances.length) return;
      gl.useProgram(this.pbrProg);
      gl.uniformMatrix4fv(this.uPbr.u_viewProj, false, camera.viewProj);
      gl.uniform3fv(this.uPbr.u_cameraPos, camera.eye); gl.uniform3fv(this.uPbr.u_viewDir, camera.viewDir);
      gl.uniform1i(this.uPbr.u_isOrtho, camera.isOrtho ? 1 : 0);
      gl.uniform3fv(this.uPbr.u_keyDir, keyDir); gl.uniform3fv(this.uPbr.u_fillDir, fillDir); gl.uniform3fv(this.uPbr.u_rimDir, rimDir);
      gl.uniform3fv(this.uPbr.u_skyColor, pal.sky); gl.uniform3fv(this.uPbr.u_groundColor, pal.ground);

      // 2-pass rendering across all instances: Pass 0 opaque, Pass 1 translucent glass lenses
      const instMats = scene.instances.map((inst) => evaluateWorldMats(inst, opts.blinkMode));
      for (let pass = 0; pass < 2; pass++) {
        const wantBlend = pass === 1;
        if (wantBlend) {
          gl.enable(gl.BLEND); gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false);
        } else { gl.disable(gl.BLEND); gl.depthMask(true); }
        scene.instances.forEach((inst, instIdx) => {
          const worldMats = instMats[instIdx];
          gl.uniform3f(this.uPbr.u_instOffset, inst.offsetMat ? inst.offsetMat[12] : 0, inst.offsetMat ? inst.offsetMat[13] : 0, inst.offsetMat ? inst.offsetMat[14] : 0);
          gl.uniform1f(this.uPbr.u_noiseSeed, instIdx * 17.3);
          for (const item of inst.model.drawCalls) {
            const prim = item.prim, mat = prim.material;
            if (Boolean(mat.isTranslucent) !== wantBlend) continue;
            let bc = mat.baseColor;
            if (mat.name === "chitin_team") bc = [team.team[0], team.team[1], team.team[2], bc[3]];
            else if (mat.name === "chitin_dark") bc = [team.dark[0], team.dark[1], team.dark[2], bc[3]];
            if (mat.doubleSided) gl.disable(gl.CULL_FACE); else { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); }
            const nodeMat = worldMats[item.nodeIndex] || item.worldMat;
            const modelMat = inst.offsetMat ? mat4Multiply(inst.offsetMat, nodeMat) : nodeMat;
            gl.frontFace(mat3DetFromMat4(modelMat) < 0 ? gl.CW : gl.CCW);
            gl.uniformMatrix4fv(this.uPbr.u_model, false, modelMat); gl.uniformMatrix3fv(this.uPbr.u_normalMat, false, mat3NormalFromMat4(modelMat));
            gl.uniform3f(this.uPbr.u_baseColor, bc[0], bc[1], bc[2]); gl.uniform1f(this.uPbr.u_alpha, bc[3]);
            gl.uniform1f(this.uPbr.u_metallic, mat.metallic); gl.uniform1f(this.uPbr.u_roughness, mat.roughness);
            gl.uniform1i(this.uPbr.u_surfaceKind, surfaceKindFor(mat)); gl.uniform3fv(this.uPbr.u_emissive, mat.emissive);
            gl.bindVertexArray(prim.vao);
            if (prim.indexed) gl.drawElements(prim.mode, prim.drawCount, prim.indexType, 0); else gl.drawArrays(prim.mode, 0, prim.drawCount);
          }
        });
      }
      gl.bindVertexArray(null); gl.frontFace(gl.CCW); gl.depthMask(true); gl.disable(gl.BLEND);
    }

    renderOffscreenPixels(w, h, scene, cam, opts) {
      const gl = this.gl, samples = Math.min(4, gl.getParameter(gl.MAX_SAMPLES) || 0);
      const makeRb = (fmt) => {
        const rb = gl.createRenderbuffer(); gl.bindRenderbuffer(gl.RENDERBUFFER, rb);
        if (samples > 1) gl.renderbufferStorageMultisample(gl.RENDERBUFFER, samples, fmt, w, h); else gl.renderbufferStorage(gl.RENDERBUFFER, fmt, w, h);
        return rb;
      };
      const msaaFbo = gl.createFramebuffer(), colorRb = makeRb(gl.RGBA8), depthRb = makeRb(gl.DEPTH_COMPONENT24);
      gl.bindFramebuffer(gl.FRAMEBUFFER, msaaFbo);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, colorRb);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depthRb);
      this.drawScene(scene, cam, { ...opts, width: w, height: h });
      let readFbo = msaaFbo, resolveFbo = null, resolveTex = null;
      if (samples > 1) {
        resolveFbo = gl.createFramebuffer(); resolveTex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, resolveTex); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.bindFramebuffer(gl.FRAMEBUFFER, resolveFbo); gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, resolveTex, 0);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, msaaFbo); gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, resolveFbo);
        gl.blitFramebuffer(0, 0, w, h, 0, 0, w, h, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        readFbo = resolveFbo;
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, readFbo);
      const raw = new Uint8Array(w * h * 4); gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, raw);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.deleteFramebuffer(msaaFbo); gl.deleteRenderbuffer(colorRb); gl.deleteRenderbuffer(depthRb);
      if (resolveFbo) { gl.deleteFramebuffer(resolveFbo); gl.deleteTexture(resolveTex); }
      const flipped = new Uint8ClampedArray(w * h * 4), rowBytes = w * 4;
      for (let y = 0; y < h; y++) {
        const sr = (h - 1 - y) * rowBytes, dr = y * rowBytes;
        for (let x = 0; x < rowBytes; x += 4) {
          const a = raw[sr + x + 3], inv = a > 0 && a < 255 ? 255 / a : 1;
          flipped[dr + x] = Math.min(255, Math.round(raw[sr + x] * inv));
          flipped[dr + x + 1] = Math.min(255, Math.round(raw[sr + x + 1] * inv));
          flipped[dr + x + 2] = Math.min(255, Math.round(raw[sr + x + 2] * inv));
          flipped[dr + x + 3] = a;
        }
      }
      return new ImageData(flipped, w, h);
    }
  }

  // --- Toolbar Roving-Tabindex Fallback (ONLY when native focusGroup is unsupported) ---
  function installToolbarRovingTabindex(toolbarEl) {
    const getButtons = () => Array.from(toolbarEl.querySelectorAll("button:not(:disabled)"));
    const init = getButtons();
    if (!init.length) return;
    init.forEach((b, i) => b.setAttribute("tabindex", i === 0 ? "0" : "-1"));
    const activate = (target, doFocus) => {
      for (const b of getButtons()) b.setAttribute("tabindex", b === target ? "0" : "-1");
      if (doFocus) target.focus();
    };
    toolbarEl.addEventListener("focusin", (e) => { const btn = e.target.closest("button"); if (btn && toolbarEl.contains(btn)) activate(btn, false); });
    toolbarEl.addEventListener("keydown", (e) => {
      const list = getButtons(), idx = list.indexOf(document.activeElement);
      if (idx < 0) return;
      const next = e.key === "ArrowRight" ? list[(idx + 1) % list.length]
        : e.key === "ArrowLeft" ? list[(idx - 1 + list.length) % list.length]
        : e.key === "Home" ? list[0] : e.key === "End" ? list[list.length - 1] : null;
      if (next) { e.preventDefault(); activate(next, true); }
    });
  }

  const emptyScene = () => ({
    instances: [], target: [0, 0.55, 0], fitRadius: 0.9, halfWidth: 0.7,
    plinthSpanX: 0, plinthRadius: 0.72, shadows: [], boundsSize: [0, 0, 0], triCount: 0, meshCount: 0, nodeCount: 0,
  });

  // --- Application Controller ---
  function initViewer() {
    const $ = (id) => document.getElementById(id);
    const stageCanvas = $("stage"), stageWrap = $("stage-wrap"), toolbarEl = $("viewer-toolbar");
    const statusEl = $("status-region"), spriteInset = $("sprite-inset");
    const spriteCanvas = $("sprite-canvas"), spriteCtx = spriteCanvas.getContext("2d");
    const emptyOverlay = $("empty-overlay"), fileInput = $("glb-file-input");
    if (!("focusGroup" in HTMLElement.prototype)) installToolbarRovingTabindex(toolbarEl);

    const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");
    const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
    const params = new URLSearchParams(window.location.search), getP = (k) => (params.get(k) || "").toLowerCase();
    const pLineup = getP("lineup") === "1" || getP("lineup") === "true";
    const pAnt = getP("ant"), pTeam = getP("team"), pAngle = getP("angle"), pProj = getP("proj"), pTurn = getP("turntable"), pBlink = getP("blink");
    const initAnt = pLineup ? "lineup" : (ANT_CLASSES.includes(pAnt) || pAnt === "lineup" ? pAnt : "worker");
    const initTeam = TEAMS[pTeam] ? pTeam : "green", initAngle = ANGLES[pAngle] ? pAngle : "hero";
    const initProj = pProj === "ortho" || pProj === "persp" ? pProj : (ANGLES[pAngle] && pAngle !== "hero" ? "ortho" : "persp");
    const initTurn = pTurn === "0" || pTurn === "false" ? false : (pTurn === "1" || pTurn === "true" ? true : (!motionQuery.matches && !pAngle && !pLineup));
    const initBlink = pBlink === "closed" ? "closed" : (pBlink === "open" ? "open" : (motionQuery.matches ? "open" : "auto"));

    const preset = ANGLES[initAngle];
    const state = {
      ant: initAnt, customLabel: "", team: initTeam, angle: initAngle, blinkMode: initBlink,
      dark: darkQuery.matches, turntable: initTurn, spritePreview: !(getP("sprite") === "0" || getP("sprite") === "false"),
      cam: { yaw: preset.yaw, pitch: preset.pitch, zoom: 1.0, projection: initProj },
      camTarget: null, dirty: true, spriteDirty: true, lastSpriteTime: 0, loadSeq: 0, ready: false, error: "",
      modelCache: new Map(), customModel: null, scene: emptyScene(),
    };

    const announce = (msg) => { statusEl.textContent = msg; };
    const showEmpty = (title, msg) => { $("empty-title").textContent = title; $("empty-message").textContent = msg; emptyOverlay.hidden = false; };
    let renderer;
    try { renderer = new AntRenderer(stageCanvas); } catch (err) {
      showEmpty("WebGL2 unavailable", err.message); announce("WebGL2 failed to initialize: " + err.message); return;
    }

    const markDirty = (includeSprite = true) => { state.dirty = true; if (includeSprite) state.spriteDirty = true; };
    const onThemeChange = () => { state.dark = darkQuery.matches; markDirty(false); };
    if (typeof darkQuery.addEventListener === "function") darkQuery.addEventListener("change", onThemeChange);
    else if (typeof darkQuery.addListener === "function") darkQuery.addListener(onThemeChange);
    stageCanvas.addEventListener("webglcontextlost", (e) => { e.preventDefault(); announce("WebGL context lost."); });

    function buildScene(entries) {
      if (!entries.length) { state.scene = emptyScene(); return; }
      const multi = entries.length > 1, gap = 0.24, widths = entries.map((e) => Math.max(0.55, e.model.size[0]));
      const totalW = multi ? widths.reduce((a, b) => a + b, 0) + gap * (entries.length - 1) : entries[0].model.size[0];
      let cursor = multi ? -totalW * 0.5 : 0, maxH = 0, maxD = 0, triCount = 0, meshCount = 0, nodeCount = 0;
      const instances = [], shadows = [];
      entries.forEach((e, i) => {
        const m = e.model, slotX = multi ? cursor + widths[i] * 0.5 : 0;
        if (multi) cursor += widths[i] + gap;
        const yLift = Math.abs(m.bounds.min[1]) > 0.02 ? -m.bounds.min[1] : 0;
        instances.push({
          id: e.id, model: m, offsetMat: mat4Translate(slotX - m.center[0], yLift, -m.center[2]),
          blink: { active: false, elapsed: 0, wait: 1.3 + i * 0.65 + Math.random() * 2.1 },
        });
        shadows.push([slotX, 0, Math.max(0.30, m.size[0] * 0.52), Math.max(0.30, m.size[2] * 0.52)]);
        maxH = Math.max(maxH, m.size[1]); maxD = Math.max(maxD, m.size[2]);
        triCount += m.triCount; meshCount += m.meshCount; nodeCount += m.nodeCount;
      });
      if (!multi) {
        const m = entries[0].model, yLift = Math.abs(m.bounds.min[1]) > 0.02 ? -m.bounds.min[1] : 0;
        const rx = m.size[0] * 0.5, ry = m.size[1] * 0.5, rz = m.size[2] * 0.5;
        state.scene = {
          instances, target: [0, m.center[1] + yLift, 0], fitRadius: Math.max(0.68, Math.hypot(rx, ry, rz) * 1.12),
          halfWidth: Math.max(0.58, Math.max(rx, rz) * 1.18), plinthSpanX: 0, plinthRadius: Math.max(0.62, Math.max(rx, rz) * 1.38),
          shadows, boundsSize: m.size, triCount, meshCount, nodeCount,
        };
      } else {
        const plinthRadius = Math.max(0.66, maxD * 0.72);
        state.scene = {
          instances, target: [0, maxH * 0.46, 0], fitRadius: Math.max(1.02, maxH * 0.92), halfWidth: totalW * 0.65,
          plinthSpanX: Math.max(0.4, totalW * 0.5 - plinthRadius * 0.35), plinthRadius,
          shadows, boundsSize: [totalW, maxH, maxD], triCount, meshCount, nodeCount,
        };
      }
    }

    function syncDebugState() {
      window.__antViewer = {
        ready: state.ready, ant: state.ant, team: state.team, angle: state.angle, blink: state.blinkMode,
        hasBlinkAnim: state.scene.instances.some((inst) => inst.model.blinkInfo && inst.model.blinkInfo.hasBlink),
        projection: state.cam.projection, yaw: Math.round(wrapAngleDeg(state.cam.yaw)), pitch: Math.round(state.cam.pitch),
        lineup: state.ant === "lineup", loadedCount: state.scene.instances.length, triCount: state.scene.triCount, error: state.error,
      };
    }
    function publishState(ready, error = "") { state.ready = ready; state.error = error; syncDebugState(); }
    function updateCameraReadout() {
      $("meta-camera").textContent = "Yaw " + Math.round(wrapAngleDeg(state.cam.yaw)) + "° · Pitch " + Math.round(state.cam.pitch) + "° · " + (state.cam.projection === "ortho" ? "Ortho" : "Persp");
      syncDebugState();
    }

    function updateUiState() {
      for (const [attr, val] of [["ant", state.ant], ["team", state.team], ["angle", state.angle], ["projection", state.cam.projection]]) {
        toolbarEl.querySelectorAll("button[data-" + attr + "]").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset[attr] === val)));
      }
      $("btn-turntable").setAttribute("aria-pressed", String(state.turntable));
      $("btn-sprite-scale").setAttribute("aria-pressed", String(state.spritePreview));
      $("btn-blink-hold").setAttribute("aria-pressed", String(state.blinkMode === "closed"));
      spriteInset.hidden = !state.spritePreview;
      const antLabel = state.ant === "custom" ? (state.customLabel || "Custom .GLB") : (ANT_LABELS[state.ant] || state.ant);
      const teamLabel = (TEAMS[state.team] || TEAMS.green).label, angleLabel = ANGLES[state.angle] ? ANGLES[state.angle].label : "Custom";
      $("badge-ant").textContent = antLabel; $("badge-team").textContent = teamLabel;
      $("badge-angle").textContent = angleLabel; $("sprite-angle-tag").textContent = angleLabel;
      $("meta-active").textContent = antLabel + " · " + teamLabel + " · " + angleLabel;
      const s = state.scene, has = s.instances.length > 0;
      $("meta-tris").textContent = has ? s.triCount.toLocaleString() : "—";
      $("meta-nodes").textContent = has ? (s.meshCount + " / " + s.nodeCount) : "—";
      $("meta-bounds").textContent = has ? (s.boundsSize.map((v) => v.toFixed(2)).join(" × ") + " u") : "—";
      updateCameraReadout();
    }

    async function fetchModel(id) {
      if (state.modelCache.has(id)) return state.modelCache.get(id);
      const resp = await fetch("models/" + encodeURIComponent(id) + ".glb");
      if (!resp.ok) throw new Error("HTTP " + resp.status + " loading models/" + id + ".glb");
      const parsed = parseGlb(await resp.arrayBuffer(), renderer.gl);
      if (state.modelCache.has(id)) { parsed.dispose(); return state.modelCache.get(id); }
      state.modelCache.set(id, parsed);
      return parsed;
    }

    async function loadManifest() {
      try {
        const resp = await fetch("models/manifest.json");
        if (!resp.ok) return;
        const data = await resp.json();
        if (data && Array.isArray(data.models)) {
          data.models.forEach((m) => { if (m && m.id && m.label) ANT_LABELS[m.id] = m.label; });
          updateUiState();
        }
      } catch (_) {}
    }

    async function selectAnt(antId) {
      const seq = ++state.loadSeq;
      state.ant = antId; state.customLabel = ""; emptyOverlay.hidden = true; publishState(false);
      if (antId === "lineup") {
        announce("Loading all 6 ant models for lineup…");
        const res = await Promise.allSettled(ANT_CLASSES.map((id) => fetchModel(id).then((model) => ({ id, model }))));
        if (seq !== state.loadSeq) return;
        const loaded = res.filter((r) => r.status === "fulfilled").map((r) => r.value);
        buildScene(loaded);
        if (!loaded.length) {
          showEmpty("Could not load lineup models", "Serve this folder over HTTP or drop .glb files onto the viewport.");
          announce("No models available for lineup. Use Load .GLB or serve via HTTP.");
          updateUiState(); markDirty(); publishState(true, "No models loaded"); return;
        }
        updateUiState(); markDirty(); announce("Showing All 6 Lineup (" + loaded.length + " of 6 models loaded)."); publishState(true); return;
      }
      try {
        announce("Loading " + (ANT_LABELS[antId] || antId) + " model…");
        const model = await fetchModel(antId);
        if (seq !== state.loadSeq) return;
        buildScene([{ id: antId, model }]); updateUiState(); markDirty();
        announce("Loaded " + (ANT_LABELS[antId] || antId) + " (" + model.triCount.toLocaleString() + " triangles)."); publishState(true);
      } catch (err) {
        if (seq !== state.loadSeq) return;
        buildScene([]);
        showEmpty("Could not load models/" + antId + ".glb", window.location.protocol === "file:"
          ? "Browsers block fetch() on file:// pages. Click Load .GLB, drag & drop .glb files here, or run a local HTTP server."
          : (err.message + ". Click Load .GLB or drag & drop a .glb file here."));
        updateUiState(); markDirty(); announce("Could not load " + antId + ".glb: " + err.message); publishState(true, err.message);
      }
    }

    function setAnglePreset(angleKey, instant = false) {
      const info = ANGLES[angleKey];
      if (!info) return;
      state.angle = angleKey; state.turntable = false;
      if (instant || motionQuery.matches) { state.cam.yaw = info.yaw; state.cam.pitch = info.pitch; state.cam.zoom = 1; state.camTarget = null; }
      else state.camTarget = { yaw: info.yaw, pitch: info.pitch, zoom: 1 };
      updateUiState(); markDirty();
    }

    async function loadLocalFiles(fileList) {
      const files = Array.from(fileList || []).filter((f) => f.name.toLowerCase().endsWith(".glb"));
      if (!files.length) { announce("Please select or drop a .glb file."); return; }
      const seq = ++state.loadSeq, loadedNames = [];
      let lastId = null;
      for (const file of files) {
        try {
          const buf = await file.arrayBuffer();
          if (seq !== state.loadSeq) return;
          const parsed = parseGlb(buf, renderer.gl), stem = file.name.replace(/\.glb$/i, "").toLowerCase();
          if (ANT_CLASSES.includes(stem)) {
            if (state.modelCache.has(stem)) state.modelCache.get(stem).dispose();
            state.modelCache.set(stem, parsed); lastId = stem;
          } else {
            if (state.customModel) state.customModel.dispose();
            state.customModel = parsed; state.customLabel = file.name; lastId = "custom";
          }
          loadedNames.push(file.name);
        } catch (err) { if (seq !== state.loadSeq) return; announce("Failed to parse " + file.name + ": " + err.message); }
      }
      if (seq !== state.loadSeq || !loadedNames.length) return;
      emptyOverlay.hidden = true;
      const avail = ANT_CLASSES.filter((id) => state.modelCache.has(id)).map((id) => ({ id, model: state.modelCache.get(id) }));
      if (files.length > 1 && avail.length) { state.ant = avail.length > 1 ? "lineup" : avail[0].id; buildScene(avail); }
      else if (lastId === "custom") { state.ant = "custom"; buildScene([{ id: "custom", model: state.customModel }]); }
      else if (lastId) { state.ant = lastId; buildScene([{ id: lastId, model: state.modelCache.get(lastId) }]); }
      updateUiState(); markDirty(); announce("Loaded local file(s): " + loadedNames.join(", ") + "."); publishState(true);
    }

    function triggerDownload(blob, filename) {
      const url = URL.createObjectURL(blob), a = document.createElement("a");
      a.href = url; a.download = filename; document.body.appendChild(a); a.click(); a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    }

    function exportFiveAngleSheet() {
      if (!state.scene.instances.length) { announce("Cannot export: no model is currently loaded."); return; }
      const size = 256, out = document.createElement("canvas"), tmp = document.createElement("canvas");
      out.width = size * SHEET_ANGLES.length; out.height = size + 44; tmp.width = size; tmp.height = size;
      const ctx = out.getContext("2d"), tCtx = tmp.getContext("2d");
      SHEET_ANGLES.forEach((key, idx) => {
        const info = ANGLES[key], x = idx * size;
        const img = renderer.renderOffscreenPixels(size, size, state.scene, { yaw: info.yaw, pitch: info.pitch, zoom: 1, projection: "ortho" }, {
          transparent: true, showPlinth: false, dark: false, team: state.team, blinkMode: state.blinkMode, overrideProj: "ortho",
        });
        tCtx.putImageData(img, 0, 0); ctx.drawImage(tmp, x, 0);
        ctx.fillStyle = "rgba(20, 24, 31, 0.86)"; ctx.beginPath();
        if (typeof ctx.roundRect === "function") ctx.roundRect(x + 12, size + 9, size - 24, 26, 6); else ctx.rect(x + 12, size + 9, size - 24, 26);
        ctx.fill();
        ctx.fillStyle = "#ffffff"; ctx.font = "600 13px system-ui, -apple-system, sans-serif";
        ctx.textAlign = "center"; ctx.textBaseline = "middle"; ctx.fillText(info.full, x + size * 0.5, size + 22.5);
      });
      const slug = state.ant === "custom" ? (state.customLabel || "custom").replace(/\.glb$/i, "").replace(/[^a-z0-9_-]+/gi, "_") : state.ant;
      const fileName = "ants-" + slug + "-" + state.team + "-5angle.png";
      out.toBlob((blob) => {
        if (!blob) { announce("PNG export failed."); return; }
        triggerDownload(blob, fileName); announce("Exported 5-angle PNG sprite sheet (" + fileName + ").");
      }, "image/png");
    }

    function downloadActiveGlb() {
      if (state.ant === "lineup") { announce("Select a single ant class to download its .glb file."); return; }
      const model = state.ant === "custom" ? state.customModel : state.modelCache.get(state.ant);
      if (!model || !model.rawBytes) { announce("No .glb file is loaded to download."); return; }
      const name = state.ant === "custom" ? (state.customLabel || "model.glb") : (state.ant + ".glb");
      triggerDownload(new Blob([model.rawBytes], { type: "model/gltf-binary" }), name);
      announce("Downloaded " + name + ".");
    }

    function triggerBlink() {
      const capable = state.scene.instances.filter((inst) => inst.model.blinkInfo && inst.model.blinkInfo.hasBlink);
      if (!capable.length) { announce("Current model has no blink eyelids."); return; }
      if (state.blinkMode === "closed") state.blinkMode = motionQuery.matches ? "open" : "auto";
      capable.forEach((inst) => { inst.blink.active = true; inst.blink.elapsed = 0; });
      updateUiState(); markDirty(); announce("Triggered eye blink.");
    }

    function toggleBlinkHold() {
      state.blinkMode = state.blinkMode === "closed" ? (motionQuery.matches ? "open" : "auto") : "closed";
      state.scene.instances.forEach((inst) => { if (inst.blink) inst.blink.active = false; });
      updateUiState(); markDirty(); announce(state.blinkMode === "closed" ? "Eyelids held closed." : "Eyelids released.");
    }

    toolbarEl.addEventListener("click", (e) => {
      const btn = e.target.closest("button");
      if (!btn) return;
      if (btn.dataset.ant) selectAnt(btn.dataset.ant);
      else if (btn.dataset.team && TEAMS[btn.dataset.team]) { state.team = btn.dataset.team; updateUiState(); markDirty(); announce("Team color set to " + TEAMS[state.team].label + "."); }
      else if (btn.dataset.angle) { setAnglePreset(btn.dataset.angle); announce("Camera set to " + ANGLES[btn.dataset.angle].full + "."); }
      else if (btn.dataset.projection) { state.cam.projection = btn.dataset.projection === "ortho" ? "ortho" : "persp"; updateUiState(); markDirty(); announce("Projection set to " + (state.cam.projection === "ortho" ? "Orthographic." : "Perspective.")); }
      else if (btn.id === "btn-turntable") { state.turntable = !state.turntable; state.camTarget = null; updateUiState(); markDirty(false); announce("Turntable spin " + (state.turntable ? "enabled." : "paused.")); }
      else if (btn.id === "btn-sprite-scale") { state.spritePreview = !state.spritePreview; updateUiState(); markDirty(true); announce("48px sprite-scale preview " + (state.spritePreview ? "shown." : "hidden.")); }
      else if (btn.id === "btn-blink-hold") toggleBlinkHold();
      else if (btn.id === "btn-blink") triggerBlink();
      else if (btn.id === "btn-export-sheet") exportFiveAngleSheet();
      else if (btn.id === "btn-download-glb") downloadActiveGlb();
      else if (btn.id === "btn-reset-view") { const key = ANGLES[state.angle] ? state.angle : "hero"; setAnglePreset(key); announce("View reset to " + ANGLES[key].full + "."); }
      else if (btn.id === "btn-load-glb") fileInput.click();
    });

    fileInput.addEventListener("change", () => { if (fileInput.files && fileInput.files.length) { loadLocalFiles(fileInput.files); fileInput.value = ""; } });

    let dragDepth = 0;
    stageWrap.addEventListener("dragenter", (e) => { e.preventDefault(); dragDepth++; stageWrap.classList.add("is-dragging"); });
    stageWrap.addEventListener("dragover", (e) => e.preventDefault());
    stageWrap.addEventListener("dragleave", () => { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) stageWrap.classList.remove("is-dragging"); });
    stageWrap.addEventListener("drop", (e) => { e.preventDefault(); dragDepth = 0; stageWrap.classList.remove("is-dragging"); if (e.dataTransfer && e.dataTransfer.files) loadLocalFiles(e.dataTransfer.files); });

    const pointers = new Map();
    let lastPinch = 0;
    const pinchDist = () => { const [a, b] = Array.from(pointers.values()); return Math.hypot(b.x - a.x, b.y - a.y); };
    stageCanvas.addEventListener("pointerdown", (e) => {
      stageCanvas.setPointerCapture(e.pointerId); pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pointers.size === 2) lastPinch = pinchDist();
      state.camTarget = null; if (state.turntable) { state.turntable = false; updateUiState(); }
    });
    stageCanvas.addEventListener("pointermove", (e) => {
      const prev = pointers.get(e.pointerId);
      if (!prev) return;
      const dx = e.clientX - prev.x, dy = e.clientY - prev.y;
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pointers.size === 1) {
        state.cam.yaw = wrapAngleDeg(state.cam.yaw - dx * 0.45); state.cam.pitch = clamp(state.cam.pitch + dy * 0.35, -10, 85);
        state.angle = "custom"; updateUiState(); markDirty();
      } else if (pointers.size === 2) {
        const dist = pinchDist();
        if (lastPinch > 1 && dist > 1) { state.cam.zoom = clamp(state.cam.zoom * (lastPinch / dist), 0.35, 3.2); markDirty(); }
        lastPinch = dist;
      }
    });
    const endPtr = (e) => { pointers.delete(e.pointerId); if (pointers.size < 2) lastPinch = 0; };
    stageCanvas.addEventListener("pointerup", endPtr); stageCanvas.addEventListener("pointercancel", endPtr);
    stageCanvas.addEventListener("wheel", (e) => { e.preventDefault(); state.camTarget = null; state.cam.zoom = clamp(state.cam.zoom * Math.exp(e.deltaY * 0.0012), 0.35, 3.2); markDirty(); }, { passive: false });

    stageCanvas.addEventListener("keydown", (e) => {
      const sy = e.shiftKey ? 4 : 12, sp = e.shiftKey ? 3 : 8;
      let manual = true;
      if (e.key === "ArrowLeft") { state.cam.yaw = wrapAngleDeg(state.cam.yaw + sy); state.angle = "custom"; }
      else if (e.key === "ArrowRight") { state.cam.yaw = wrapAngleDeg(state.cam.yaw - sy); state.angle = "custom"; }
      else if (e.key === "ArrowUp") { state.cam.pitch = clamp(state.cam.pitch + sp, -10, 85); state.angle = "custom"; }
      else if (e.key === "ArrowDown") { state.cam.pitch = clamp(state.cam.pitch - sp, -10, 85); state.angle = "custom"; }
      else if (e.key === "+" || e.key === "=") state.cam.zoom = clamp(state.cam.zoom * 0.88, 0.35, 3.2);
      else if (e.key === "-" || e.key === "_") state.cam.zoom = clamp(state.cam.zoom * 1.14, 0.35, 3.2);
      else if (e.key === "b" || e.key === "B") { e.preventDefault(); if (e.shiftKey) toggleBlinkHold(); else triggerBlink(); return; }
      else {
        manual = false;
        const key = (e.key === "r" || e.key === "R") ? (ANGLES[state.angle] ? state.angle : "hero")
          : (e.key >= "1" && e.key <= "5") ? SHEET_ANGLES[Number(e.key) - 1]
          : (e.key === "6" || e.key === "0") ? "hero" : null;
        if (!key) return;
        setAnglePreset(key); announce((e.key === "r" || e.key === "R" ? "View reset to " : "Camera set to ") + ANGLES[key].full + ".");
      }
      e.preventDefault();
      if (manual) { state.camTarget = null; if (state.turntable) state.turntable = false; updateUiState(); markDirty(); }
    });

    new ResizeObserver(() => markDirty(false)).observe(stageCanvas);

    let prevTime = performance.now();
    function frame(now) {
      const dt = Math.min((now - prevTime) / 1000, 0.1); prevTime = now;
      if (state.camTarget) {
        const t = Math.min(1, dt * 11), dyaw = wrapAngleDeg(state.camTarget.yaw - state.cam.yaw);
        state.cam.yaw = wrapAngleDeg(state.cam.yaw + dyaw * t);
        state.cam.pitch += (state.camTarget.pitch - state.cam.pitch) * t;
        state.cam.zoom += (state.camTarget.zoom - state.cam.zoom) * t;
        if (Math.abs(dyaw) < 0.15 && Math.abs(state.camTarget.pitch - state.cam.pitch) < 0.15 && Math.abs(state.camTarget.zoom - state.cam.zoom) < 0.005) {
          Object.assign(state.cam, state.camTarget); state.camTarget = null;
        }
        updateCameraReadout(); state.dirty = true; state.spriteDirty = true;
      } else if (state.turntable && state.scene.instances.length) {
        state.cam.yaw = wrapAngleDeg(state.cam.yaw - dt * 22); updateCameraReadout(); state.dirty = true;
        if (now - state.lastSpriteTime > 80) state.spriteDirty = true;
      }
      if (state.blinkMode !== "closed") {
        for (const inst of state.scene.instances) {
          const bi = inst.model.blinkInfo, bl = inst.blink;
          if (!bi || !bi.hasBlink || !bl) continue;
          const pulseDur = bi.anim ? (bi.endTime - bi.startTime) : 0.22;
          if (bl.active) {
            bl.elapsed += dt;
            if (bl.elapsed >= pulseDur) { bl.active = false; bl.elapsed = 0; bl.wait = 2.4 + Math.random() * 3.2; }
            state.dirty = true; state.spriteDirty = true;
          } else if (state.blinkMode === "auto") {
            bl.wait -= dt;
            if (bl.wait <= 0) { bl.active = true; bl.elapsed = 0; state.dirty = true; state.spriteDirty = true; }
          }
        }
      }
      if (state.dirty) {
        const [w, h] = renderer.resize();
        renderer.drawScene(state.scene, state.cam, { width: w, height: h, transparent: false, showPlinth: true, dark: state.dark, team: state.team, blinkMode: state.blinkMode });
        state.dirty = false;
      }
      if (state.spriteDirty && state.spritePreview) {
        state.spriteDirty = false; state.lastSpriteTime = now;
        spriteCtx.clearRect(0, 0, spriteCanvas.width, spriteCanvas.height);
        if (state.scene.instances.length) {
          const spriteCam = { yaw: state.cam.yaw, pitch: state.cam.pitch, zoom: 1.0, projection: "ortho" };
          spriteCtx.putImageData(renderer.renderOffscreenPixels(spriteCanvas.width, spriteCanvas.height, state.scene, spriteCam, {
            transparent: true, showPlinth: false, dark: false, team: state.team, blinkMode: state.blinkMode, overrideProj: "ortho",
          }), 0, 0);
        }
      }
      requestAnimationFrame(frame);
    }

    updateUiState(); loadManifest(); selectAnt(state.ant); requestAnimationFrame(frame);
  }

  document.addEventListener("DOMContentLoaded", initViewer);
})();

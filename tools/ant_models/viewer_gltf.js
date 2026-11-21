"use strict";

(() => {
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
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

  // --- glTF 2.0 Accessors, Embedded Images & WebGL2 Texture Upload ---
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
  const ANISO_EXTS = ["EXT_texture_filter_anisotropic", "WEBKIT_EXT_texture_filter_anisotropic", "MOZ_EXT_texture_filter_anisotropic"];

  function decodeDataUri(uri) {
    const idx = uri.indexOf("base64,");
    if (idx < 0) return null;
    const raw = atob(uri.slice(idx + 7)), bytes = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
    return bytes.buffer;
  }

  function readAccessor(gltf, buffers, accIdx, forceNorm = false) {
    const acc = gltf.accessors[accIdx], numComp = TYPE_COMPS[acc.type], compSize = COMP_SIZES[acc.componentType], count = acc.count;
    if (acc.bufferView === undefined) return new Float32Array(count * numComp);
    const bv = gltf.bufferViews[acc.bufferView], dv = new DataView(buffers[bv.buffer || 0]);
    const baseOff = (bv.byteOffset || 0) + (acc.byteOffset || 0), stride = bv.byteStride || numComp * compSize;
    const norm = Boolean(acc.normalized) || (forceNorm && acc.componentType !== 5126);
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

  function uploadBuffer(gl, target, data, attrLoc, compSize = 3) {
    const buf = gl.createBuffer();
    gl.bindBuffer(target, buf);
    gl.bufferData(target, data, gl.STATIC_DRAW);
    if (attrLoc !== undefined) {
      gl.enableVertexAttribArray(attrLoc);
      gl.vertexAttribPointer(attrLoc, compSize, gl.FLOAT, false, 0, 0);
    }
    return buf;
  }

  function getAnisoExt(gl) {
    for (const name of ANISO_EXTS) {
      const ext = gl.getExtension(name);
      if (ext) return ext;
    }
    return null;
  }

  function createFallbackTextures(gl) {
    const mk = (rgba) => {
      const tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(rgba));
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      gl.bindTexture(gl.TEXTURE_2D, null);
      return tex;
    };
    return { white: mk([255, 255, 255, 255]), flatNormal: mk([128, 128, 255, 255]) };
  }

  async function decodeImageBlob(blob) {
    if (typeof createImageBitmap === "function") {
      try {
        const bitmap = await createImageBitmap(blob, { colorSpaceConversion: "none", premultiplyAlpha: "none" });
        return { source: bitmap, release: () => { if (typeof bitmap.close === "function") bitmap.close(); } };
      } catch (_) {}
    }
    const url = URL.createObjectURL(blob);
    try {
      const img = new Image();
      img.decoding = "async";
      img.src = url;
      await new Promise((resolve, reject) => {
        img.onload = resolve;
        img.onerror = () => reject(new Error("Failed to decode embedded texture image"));
      });
      return { source: img, release: () => {} };
    } finally {
      URL.revokeObjectURL(url);
    }
  }

  async function decodeGltfImages(gltf, buffers) {
    const defs = gltf.images || [];
    return Promise.all(defs.map(async (imgDef) => {
      if (!imgDef) return null;
      let blob = null;
      if (imgDef.bufferView !== undefined) {
        const bv = (gltf.bufferViews || [])[imgDef.bufferView];
        if (!bv) return null;
        const buf = buffers[bv.buffer || 0], off = bv.byteOffset || 0, len = bv.byteLength || 0;
        if (!buf || off + len > buf.byteLength) return null;
        blob = new Blob([new Uint8Array(buf, off, len)], { type: imgDef.mimeType || "image/png" });
      } else if (typeof imgDef.uri === "string" && imgDef.uri.startsWith("data:")) {
        const raw = decodeDataUri(imgDef.uri);
        if (!raw) return null;
        const m = /^data:([^;,]+)/i.exec(imgDef.uri);
        blob = new Blob([raw], { type: (m && m[1]) || imgDef.mimeType || "image/png" });
      }
      if (!blob) return null;
      try { return await decodeImageBlob(blob); } catch (_) { return null; }
    }));
  }

  function uploadGltfTexture(gl, source, sampler, isSrgb, anisoExt, maxAniso) {
    const validWrap = new Set([gl.CLAMP_TO_EDGE, gl.MIRRORED_REPEAT, gl.REPEAT]);
    const validMag = new Set([gl.NEAREST, gl.LINEAR]);
    const validMin = new Set([
      gl.NEAREST, gl.LINEAR, gl.NEAREST_MIPMAP_NEAREST,
      gl.LINEAR_MIPMAP_NEAREST, gl.NEAREST_MIPMAP_LINEAR, gl.LINEAR_MIPMAP_LINEAR,
    ]);
    const wrapS = sampler && validWrap.has(sampler.wrapS) ? sampler.wrapS : gl.REPEAT;
    const wrapT = sampler && validWrap.has(sampler.wrapT) ? sampler.wrapT : gl.REPEAT;
    const magFilter = sampler && validMag.has(sampler.magFilter) ? sampler.magFilter : gl.LINEAR;
    const minFilter = sampler && validMin.has(sampler.minFilter) ? sampler.minFilter : gl.LINEAR_MIPMAP_LINEAR;

    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    gl.texImage2D(gl.TEXTURE_2D, 0, isSrgb ? gl.SRGB8_ALPHA8 : gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, source);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrapS);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, wrapT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, magFilter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, minFilter);
    const wantsMip = minFilter !== gl.NEAREST && minFilter !== gl.LINEAR;
    if (wantsMip) gl.generateMipmap(gl.TEXTURE_2D);
    if (anisoExt && maxAniso > 1) {
      gl.texParameterf(gl.TEXTURE_2D, anisoExt.TEXTURE_MAX_ANISOTROPY_EXT, Math.min(8, maxAniso));
    }
    gl.bindTexture(gl.TEXTURE_2D, null);
    return tex;
  }

  async function parseGlb(arrayBuffer, gl) {
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

    const decodedImages = await decodeGltfImages(gltf, buffers);
    const anisoExt = getAnisoExt(gl);
    const maxAniso = anisoExt ? gl.getParameter(anisoExt.MAX_TEXTURE_MAX_ANISOTROPY_EXT) : 1;
    const uploadedTexCache = new Map(), ownedTextures = [];
    const resolveTexture = (texInfo, isSrgb) => {
      if (!texInfo || typeof texInfo.index !== "number") return null;
      const key = texInfo.index + ":" + (isSrgb ? "s" : "l");
      if (uploadedTexCache.has(key)) return uploadedTexCache.get(key);
      const texDef = (gltf.textures || [])[texInfo.index];
      if (!texDef || typeof texDef.source !== "number") return null;
      const decoded = decodedImages[texDef.source];
      if (!decoded) return null;
      const sampler = typeof texDef.sampler === "number" ? (gltf.samplers || [])[texDef.sampler] : null;
      const glTex = uploadGltfTexture(gl, decoded.source, sampler, isSrgb, anisoExt, maxAniso);
      uploadedTexCache.set(key, glTex);
      ownedTextures.push(glTex);
      return glTex;
    };

    const defaultMat = {
      name: "default", baseColor: [0.75, 0.78, 0.82, 1], metallic: 0.05, roughness: 0.45,
      emissive: [0, 0, 0], alphaMode: "OPAQUE", doubleSided: false, isTranslucent: false,
      baseTex: null, normalTex: null, normalScale: 1.0, mrTex: null,
    };
    let materials;
    try {
      materials = (gltf.materials || []).map((m, i) => {
        const pbr = m.pbrMetallicRoughness || {}, bc = Array.isArray(pbr.baseColorFactor) ? pbr.baseColorFactor : [1, 1, 1, 1];
        const name = m.name || ("material_" + i);
        let alpha = bc.length > 3 ? bc[3] : 1, alphaMode = m.alphaMode || "OPAQUE", doubleSided = Boolean(m.doubleSided);
        if (alphaMode === "OPAQUE" && alpha >= 0.99) {
          if (name === "glass_tinted") { alpha = 0.30; alphaMode = "BLEND"; doubleSided = true; }
          else if (name === "glass_clear") { alpha = 0.22; alphaMode = "BLEND"; doubleSided = true; }
        }
        const baseTex = resolveTexture(pbr.baseColorTexture, true);
        const normalTex = resolveTexture(m.normalTexture, false);
        const mrTex = resolveTexture(pbr.metallicRoughnessTexture, false);
        const hasMr = Boolean(mrTex);
        return {
          name, baseColor: [bc[0], bc[1], bc[2], alpha],
          metallic: typeof pbr.metallicFactor === "number" ? pbr.metallicFactor : (hasMr ? 1.0 : 0.05),
          roughness: typeof pbr.roughnessFactor === "number" ? pbr.roughnessFactor : (hasMr ? 1.0 : 0.45),
          emissive: Array.isArray(m.emissiveFactor) ? m.emissiveFactor : [0, 0, 0],
          alphaMode, doubleSided, isTranslucent: alphaMode === "BLEND" || alpha < 0.99,
          baseTex, normalTex, mrTex,
          normalScale: m.normalTexture && typeof m.normalTexture.scale === "number" ? m.normalTexture.scale : 1.0,
        };
      });
    } finally {
      for (const img of decodedImages) if (img) img.release();
    }

    let hasVertexColors = false;
    const uploadedMeshes = (gltf.meshes || []).map((mesh) => (mesh.primitives || []).map((prim) => {
      if (prim.attributes.POSITION === undefined) return null;
      const pos = readAccessor(gltf, buffers, prim.attributes.POSITION);
      const indices = prim.indices !== undefined ? readAccessor(gltf, buffers, prim.indices) : null;
      const norm = prim.attributes.NORMAL !== undefined ? readAccessor(gltf, buffers, prim.attributes.NORMAL) : computeFallbackNormals(pos, indices);
      const hasUv = prim.attributes.TEXCOORD_0 !== undefined;
      const uv = hasUv ? readAccessor(gltf, buffers, prim.attributes.TEXCOORD_0, true) : null;
      let col = null;
      if (prim.attributes.COLOR_0 !== undefined) {
        const colAcc = gltf.accessors[prim.attributes.COLOR_0];
        const rawCol = readAccessor(gltf, buffers, prim.attributes.COLOR_0, true);
        if (colAcc && colAcc.type === "VEC4") {
          const n = colAcc.count;
          col = new Float32Array(n * 3);
          for (let i = 0; i < n; i++) { col[i * 3] = rawCol[i * 4]; col[i * 3 + 1] = rawCol[i * 4 + 1]; col[i * 3 + 2] = rawCol[i * 4 + 2]; }
        } else col = rawCol;
        hasVertexColors = true;
      }
      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      const glBufs = [uploadBuffer(gl, gl.ARRAY_BUFFER, pos, 0, 3), uploadBuffer(gl, gl.ARRAY_BUFFER, norm, 1, 3)];
      if (uv) glBufs.push(uploadBuffer(gl, gl.ARRAY_BUFFER, uv, 2, 2)); else gl.disableVertexAttribArray(2);
      if (col) glBufs.push(uploadBuffer(gl, gl.ARRAY_BUFFER, col, 3, 3)); else gl.disableVertexAttribArray(3);
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
        vao, buffers: glBufs, indexed: Boolean(indices), indexType, drawCount, mode, hasUv,
        triCount: mode === gl.TRIANGLES ? Math.floor(drawCount / 3) : 0,
        material: prim.material !== undefined ? materials[prim.material] || defaultMat : defaultMat,
        localMin: lmin, localMax: lmax,
      };
    }).filter(Boolean));

    const nodes = gltf.nodes || [], scene = gltf.scenes && gltf.scenes[gltf.scene || 0];
    const childIndices = new Set();
    nodes.forEach((n) => { if (n && Array.isArray(n.children)) n.children.forEach((c) => childIndices.add(c)); });
    const rootIndices = scene && Array.isArray(scene.nodes)
      ? scene.nodes
      : nodes.map((_, i) => i).filter((i) => !childIndices.has(i));
    const drawCalls = [], min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    const nodeDefs = new Array(nodes.length), baseWorldMats = new Array(nodes.length), nodeOrder = [], eyelidNodes = [];
    const visited = new Set();
    let nodeCount = 0, triCount = 0;

    function walkNode(nodeIdx, parentIdx, parentMat) {
      const node = nodes[nodeIdx];
      if (!node || visited.has(nodeIdx)) return;
      visited.add(nodeIdx);
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

    const animPaths = new Set(["translation", "rotation", "scale"]);
    const animations = (gltf.animations || []).map((anim) => {
      let duration = 0;
      const channels = (anim.channels || []).map((ch) => {
        const tgt = ch.target, smp = (anim.samplers || [])[ch.sampler];
        if (!tgt || tgt.node === undefined || !animPaths.has(tgt.path) || !smp) return null;
        const times = readAccessor(gltf, buffers, smp.input), values = readAccessor(gltf, buffers, smp.output);
        if (times.length) duration = Math.max(duration, times[times.length - 1]);
        return { nodeIndex: tgt.node, path: tgt.path, times, values, interpolation: smp.interpolation || "LINEAR" };
      }).filter(Boolean);
      return { name: anim.name || "", duration, channels };
    });

    const eyelidSet = new Set(eyelidNodes);
    const blinkAnim = animations.find((a) => /blink/i.test(a.name))
      || animations.find((a) => a.channels.some((c) => eyelidSet.has(c.nodeIndex))) || null;
    const nonBlinkAnims = animations.filter((a) => a !== blinkAnim && a.duration > 0 && a.channels.length > 0);
    const attackAnims = nonBlinkAnims.filter((a) => /attack/i.test(a.name));
    const idleOnly = nonBlinkAnims.filter((a) => !attackAnims.includes(a));
    const idleAnims = idleOnly.length ? idleOnly : nonBlinkAnims;
    const blinkInfo = { anim: blinkAnim, eyelidNodes, closedTime: 0, startTime: 0, endTime: 0.24, hasBlink: Boolean(blinkAnim || eyelidNodes.length) };
    if (blinkAnim && blinkAnim.channels.length) {
      const refCh = blinkAnim.channels.find((c) => c.path === "rotation") || blinkAnim.channels[0], times = refCh.times;
      const restQ = (refCh.path === "rotation" && times.length ? sampleChannel(refCh, times[0]) : null)
        || (nodeDefs[refCh.nodeIndex] && nodeDefs[refCh.nodeIndex].r) || [0, 0, 0, 1];
      let maxAng = -1, pFirst = 0, pLast = 0, sIdx = 0, eIdx = times.length - 1;
      const angs = Array.from(times, (tm) => { const q = refCh.path === "rotation" ? sampleChannel(refCh, tm) : null; return q ? quatAngle(q, restQ) : 0; });
      angs.forEach((a, i) => { if (a > maxAng + 1e-4) { maxAng = a; pFirst = pLast = i; } else if (Math.abs(a - maxAng) <= 1e-4) pLast = i; });
      for (let i = pFirst; i >= 0; i--) if (angs[i] < 1e-3) { sIdx = i; break; }
      for (let i = pLast; i < times.length; i++) if (angs[i] < 1e-3) { eIdx = i; break; }
      blinkInfo.closedTime = (times[pFirst] + times[pLast]) * 0.5;
      blinkInfo.startTime = times[sIdx] || 0;
      blinkInfo.endTime = Math.max(blinkInfo.startTime + 0.08, times[eIdx] || blinkAnim.duration || 0.24);
    }

    const isBlend = (dc) => (dc.prim.material.isTranslucent ? 1 : 0);
    drawCalls.sort((a, b) => isBlend(a) - isBlend(b));
    return {
      drawCalls, nodeDefs, nodeOrder, baseWorldMats, animations, idleAnims, attackAnims, blinkInfo, bounds: { min, max },
      size: [max[0] - min[0], max[1] - min[1], max[2] - min[2]],
      center: [(min[0] + max[0]) * 0.5, (min[1] + max[1]) * 0.5, (min[2] + max[2]) * 0.5],
      triCount, meshCount: uploadedMeshes.length, nodeCount, textureCount: ownedTextures.length, hasVertexColors,
      rawBytes: arrayBuffer,
      dispose() {
        for (const prims of uploadedMeshes) for (const p of prims) { gl.deleteVertexArray(p.vao); p.buffers.forEach((b) => gl.deleteBuffer(b)); }
        ownedTextures.forEach((t) => gl.deleteTexture(t));
      },
    };
  }

  window.__antGltf = {
    clamp, mat4Identity, mat4Translate, mat4Multiply, mat4FromTRS, mat4LookAt, mat4Perspective, mat4Ortho,
    mat3DetFromMat4, mat3NormalFromMat4, transformPoint, quatSlerp, quatMul, quatAngle, sampleChannel,
    uploadBuffer, createFallbackTextures, parseGlb,
  };
})();

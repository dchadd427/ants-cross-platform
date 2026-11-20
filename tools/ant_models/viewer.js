"use strict";

(() => {
  const {
    clamp, mat4Translate, mat4Multiply, mat4FromTRS, mat4LookAt, mat4Perspective, mat4Ortho,
    mat3DetFromMat4, mat3NormalFromMat4, quatMul, sampleChannel,
    uploadBuffer, createFallbackTextures, parseGlb,
  } = window.__antGltf;

  const ANT_CLASSES = ["worker", "combat", "thief", "bomber", "swimmer", "fire"];
  const ANT_LABELS = {
    worker: "Worker", combat: "Combat", thief: "Thief",
    bomber: "Bomber", swimmer: "Swimmer", fire: "Fire", lineup: "All 6 Lineup",
  };
  const srgbToLinear = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
  const rgbLinear = (rgb) => rgb.map(srgbToLinear);
  const DEFAULT_CUSTOM_HEX = "#7c3aed";
  const TEAMS = {
    green: { label: "Green", team: rgbLinear([0.24, 0.45, 0.27]), dark: rgbLinear([0.10, 0.22, 0.13]) },
    red: { label: "Red", team: rgbLinear([0.54, 0.16, 0.18]), dark: rgbLinear([0.24, 0.06, 0.07]) },
    blue: { label: "Blue", team: rgbLinear([0.20, 0.33, 0.56]), dark: rgbLinear([0.08, 0.14, 0.28]) },
    black: { label: "Black", team: rgbLinear([0.36, 0.38, 0.43]), dark: rgbLinear([0.14, 0.15, 0.18]) },
  };

  function normalizeHex(raw) {
    if (typeof raw !== "string") return null;
    const s = raw.trim().replace(/^#/, "");
    if (/^[0-9a-f]{6}$/i.test(s)) return "#" + s.toLowerCase();
    if (/^[0-9a-f]{3}$/i.test(s)) return "#" + (s[0] + s[0] + s[1] + s[1] + s[2] + s[2]).toLowerCase();
    return null;
  }

  function deriveCustomTeam(hex) {
    const clean = normalizeHex(hex) || DEFAULT_CUSTOM_HEX;
    const r = parseInt(clean.slice(1, 3), 16) / 255;
    const g = parseInt(clean.slice(3, 5), 16) / 255;
    const b = parseInt(clean.slice(5, 7), 16) / 255;
    const lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    let dr, dg, db;
    if (lum < 0.09) {
      const lift = 0.12;
      dr = clamp(r * 0.65 + lift, 0.09, 0.24);
      dg = clamp(g * 0.65 + lift, 0.09, 0.24);
      db = clamp(b * 0.65 + lift * 1.08, 0.10, 0.26);
    } else {
      const scale = lum > 0.82 ? 0.38 : 0.44, sat = 1.18;
      dr = clamp((lum + (r - lum) * sat) * scale, 0.04, 0.48);
      dg = clamp((lum + (g - lum) * sat) * scale, 0.04, 0.48);
      db = clamp((lum + (b - lum) * sat) * scale, 0.04, 0.48);
    }
    return {
      hex: clean, label: "Custom " + clean.toUpperCase(), slug: "custom-" + clean.slice(1),
      team: rgbLinear([r, g, b]), dark: rgbLinear([dr, dg, db]),
    };
  }

  const resolveTeam = (id, customHex) => (id === "custom" ? deriveCustomTeam(customHex) : (TEAMS[id] || TEAMS.green));
  const ANGLES = {
    "301": { label: "301 S", full: "301 S (Front)", yaw: 0, pitch: 33 },
    "201": { label: "201 SE", full: "201 SE (3/4)", yaw: -45, pitch: 33 },
    "901": { label: "901 E", full: "901 E (Side)", yaw: -90, pitch: 33 },
    "801": { label: "801 NE", full: "801 NE (Back 3/4)", yaw: -135, pitch: 33 },
    "701": { label: "701 N", full: "701 N (Back)", yaw: 180, pitch: 33 },
    hero: { label: "Hero", full: "Hero Showcase", yaw: -28, pitch: 22 },
  };
  const SHEET_ANGLES = ["301", "201", "901", "801", "701"];
  const DEG = Math.PI / 180;
  const wrapAngleDeg = (deg) => { const a = ((deg + 180) % 360 + 360) % 360 - 180; return a === -180 ? 180 : a; };
  const STUDIO_PALETTE = {
    light: {
      bgTop: [0.855, 0.880, 0.918], bgMid: [0.958, 0.966, 0.978], bgBot: [0.780, 0.816, 0.862],
      plinthTop: [0.68, 0.71, 0.76], plinthSide: [0.42, 0.46, 0.52], plinthRing: [0.45, 0.49, 0.56],
      sky: [0.56, 0.70, 0.82], ground: [0.18, 0.17, 0.18], rimBoost: 0.62,
    },
    dark: {
      bgTop: [0.072, 0.092, 0.130], bgMid: [0.136, 0.168, 0.220], bgBot: [0.052, 0.068, 0.098],
      plinthTop: [0.095, 0.114, 0.148], plinthSide: [0.048, 0.060, 0.082], plinthRing: [0.19, 0.25, 0.32],
      sky: [0.38, 0.56, 0.68], ground: [0.08, 0.08, 0.09], rimBoost: 1.0,
    },
  };

  // --- WebGL2 Shaders (Studio Backdrop, Plinth + Contact Shadows, Normal-Mapped Satin PBR) ---
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
  layout(location = 2) in vec2 a_uv;
  layout(location = 3) in vec3 a_color;
  uniform mat4 u_model, u_viewProj;
  uniform mat3 u_normalMat;
  out vec3 v_worldPos, v_worldNormal, v_color;
  out vec2 v_uv;
  void main() {
    vec4 wp = u_model * vec4(a_position, 1.0);
    v_worldPos = wp.xyz;
    v_worldNormal = normalize(u_normalMat * a_normal);
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_viewProj * wp;
  }`;

  const PBR_FS = `#version 300 es
  precision highp float;
  const float PI = 3.14159265359;
  in vec3 v_worldPos, v_worldNormal, v_color;
  in vec2 v_uv;
  uniform vec3 u_cameraPos, u_viewDir, u_baseColor, u_emissive, u_keyDir, u_fillDir, u_rimDir, u_skyColor, u_groundColor;
  uniform float u_alpha, u_metallic, u_roughness, u_normalScale, u_rimBoost;
  uniform int u_isOrtho, u_matKind, u_hasNormalTex;
  uniform sampler2D u_baseTex, u_normalTex, u_mrTex;
  out vec4 fragColor;

  vec3 applyNormalMap(vec3 N, vec3 pos, vec2 uv, out float slope) {
    vec3 mapN = texture(u_normalTex, uv).xyz * 2.0 - 1.0;
    vec2 scaledXY = mapN.xy * u_normalScale;
    slope = length(scaledXY);
    mapN = normalize(vec3(scaledXY, max(mapN.z, 1e-4)));
    vec3 dp1 = dFdx(pos), dp2 = dFdy(pos);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    float dpMax2 = max(dot(dp1, dp1), dot(dp2, dp2));
    float uvMax2 = max(dot(duv1, duv1), dot(duv2, duv2));
    if (dpMax2 < 1e-30 || uvMax2 < 1e-30) return N;
    float dpScale = inversesqrt(dpMax2), uvScale = inversesqrt(uvMax2);
    dp1 *= dpScale; dp2 *= dpScale;
    duv1 *= uvScale; duv2 *= uvScale;
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float tLen2 = dot(T, T), bLen2 = dot(B, B);
    if (tLen2 < 1e-12 && bLen2 < 1e-12) return N;
    vec3 Tn = tLen2 >= 1e-12 ? T * inversesqrt(tLen2) : normalize(cross(N, B));
    vec3 Bn = bLen2 >= 1e-12 ? B * inversesqrt(bLen2) : normalize(cross(T, N));
    return normalize(mat3(Tn, Bn, N) * mapN);
  }
  float D_GGX(float NoH, float a) { float a2 = a * a, d = (NoH * NoH) * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-6); }
  float G_Smith(float NoV, float NoL, float r) {
    float k = ((r + 1.0) * (r + 1.0)) / 8.0;
    return (NoV / (NoV * (1.0 - k) + k + 1e-6)) * (NoL / (NoL * (1.0 - k) + k + 1e-6));
  }
  vec3 F_Schlick(float c, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0); }
  vec3 studioEnv(vec3 R, float rough) {
    vec3 env = mix(u_groundColor * 0.58, u_skyColor * 1.14, smoothstep(0.14, 0.90, clamp(R.y * 0.5 + 0.5, 0.0, 1.0)));
    float sharp = (1.0 - rough) * (1.0 - rough);
    float glintExp = mix(22.0, 220.0, sharp);
    float keySoft = smoothstep(0.66, 0.96, dot(R, u_keyDir)) * 1.38 + pow(max(dot(R, u_keyDir), 0.0), glintExp) * 1.85 * sharp;
    float fillStrip = smoothstep(0.76, 0.98, dot(R, u_fillDir)) * 0.52;
    float rimSoft = smoothstep(0.70, 0.97, dot(R, u_rimDir)) * 0.88 + pow(max(dot(R, u_rimDir), 0.0), glintExp * 0.7) * 0.95 * sharp;
    return env + vec3(1.0, 0.95, 0.87) * keySoft * sharp + vec3(0.64, 0.88, 1.0) * fillStrip * sharp + vec3(0.84, 0.96, 1.0) * rimSoft * sharp;
  }
  vec3 evalLight(vec3 L, vec3 lCol, vec3 N, vec3 V, vec3 albedo, float met, float rough, vec3 F0, float coatW, inout vec3 specAcc) {
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4), NoH = max(dot(N, H), 0.0), HoV = max(dot(H, V), 0.0);
    vec3 F = F_Schlick(HoV, F0);
    vec3 spec = (D_GGX(NoH, max(rough * rough, 0.032)) * G_Smith(NoV, NoL, rough) * F) / (4.0 * NoV * NoL + 1e-4);
    vec3 diff = (vec3(1.0) - F) * (1.0 - met) * albedo / PI;
    float coat = coatW * (D_GGX(NoH, 0.024) * G_Smith(NoV, NoL, 0.16) * (0.04 + 0.96 * pow(1.0 - HoV, 5.0))) / (4.0 * NoV * NoL + 1e-4);
    vec3 specTerm = (spec + vec3(coat)) * lCol * NoL;
    specAcc += specTerm;
    return diff * lCol * NoL + specTerm;
  }
  vec3 acesFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
  vec3 linearToSrgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }

  void main() {
    vec3 N = normalize(v_worldNormal);
    if (!gl_FrontFacing) N = -N;
    float normSlope = 0.0;
    if (u_hasNormalTex == 1 && u_normalScale > 0.0) N = applyNormalMap(N, v_worldPos, v_uv, normSlope);
    vec3 V = u_isOrtho == 1 ? normalize(u_viewDir) : normalize(u_cameraPos - v_worldPos);

    vec4 baseSample = texture(u_baseTex, v_uv);
    vec4 mrSample = texture(u_mrTex, v_uv);
    float rough = clamp(u_roughness * mrSample.g, 0.04, 0.98);
    float met = clamp(u_metallic * mrSample.b, 0.0, 1.0);
    float microCav = 1.0;
    if (u_hasNormalTex == 1) {
      float pitDepth = smoothstep(0.62, 0.98, mrSample.g);
      microCav = clamp(1.0 - (pitDepth * 0.60 + clamp(normSlope, 0.0, 1.0) * 0.45) * mix(0.03, 0.015, met), 0.94, 1.0);
    }
    vec3 albedo = max(u_baseColor * baseSample.rgb * microCav, 0.0);

    // Optical specular glints on eyes & glass; warm key + cool-teal fill + crisp rim on character forms
    float coatW = u_matKind == 1 ? 0.34 * (1.0 - smoothstep(0.18, 0.42, rough)) : 0.0;
    vec3 F0 = mix(vec3(0.045), albedo, met), specAcc = vec3(0.0);
    vec3 Lo = evalLight(u_keyDir, vec3(1.0, 0.94, 0.85) * 3.30, N, V, albedo, met, rough, F0, coatW, specAcc)
            + evalLight(u_fillDir, vec3(0.58, 0.84, 0.96) * 0.94, N, V, albedo, met, rough, F0, coatW, specAcc)
            + evalLight(u_rimDir, vec3(0.84, 0.96, 1.0) * 1.90, N, V, albedo, met, rough, F0, coatW, specAcc);

    vec3 bakedAo = clamp(v_color, 0.0, 1.0);
    vec3 aoRich = pow(bakedAo, vec3(1.28));
    float aoScalar = dot(aoRich, vec3(0.2126, 0.7152, 0.0722)) * microCav;
    float geoAo = mix(0.75, 1.0, smoothstep(0.0, 0.26, v_worldPos.y)) * mix(0.72, 1.0, smoothstep(-0.38, 0.52, N.y));
    vec3 directAo = mix(vec3(1.0), aoRich, u_alpha < 0.99 ? 0.18 : 0.62);

    vec3 R = reflect(-V, N); float NoV = max(dot(N, V), 0.0);
    float specOccl = clamp(pow(NoV + aoScalar, exp2(-16.0 * rough - 1.0)) - 1.0 + aoScalar, 0.0, 1.0);
    vec3 ambDiff = mix(u_groundColor, u_skyColor, N.y * 0.5 + 0.5) * albedo * (1.0 - met) * 0.35 * aoRich * geoAo;
    float envAtten = (1.0 - rough) * (1.0 - rough);
    vec3 F_env = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NoV, 5.0);
    vec3 envSpec = studioEnv(R, rough) * F_env * mix(0.18, 1.05, envAtten) * mix(envAtten, 1.0, met) * specOccl * geoAo;
    vec3 envCoat = studioEnv(R, 0.14) * ((0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coatW) * 0.42 * specOccl;

    // Warm organic subsurface sheen across chitin terminators + crisp Fresnel contour rim
    vec3 sssBounce = vec3(0.0);
    if (u_matKind == 2) {
      float wrapKey = clamp((dot(N, u_keyDir) + 0.28) / 1.28, 0.0, 1.0);
      float termBand = wrapKey * (1.0 - max(dot(N, u_keyDir), 0.0));
      sssBounce = (albedo * vec3(1.18, 1.08, 0.88) + vec3(0.012, 0.010, 0.005)) * termBand * 0.44 * aoRich * geoAo;
    }
    float fresnelRim = pow(1.0 - NoV, 3.2) * smoothstep(-0.25, 0.72, N.y);
    float rimMask = mix(0.45, 1.0, clamp(dot(N, u_rimDir) * 0.55 + 0.55, 0.0, 1.0));
    vec3 rimTint = mix(vec3(0.76, 0.92, 1.0), albedo * 1.45 + vec3(0.28, 0.36, 0.42), u_matKind == 2 ? 0.42 : 0.18);
    vec3 rimGlow = rimTint * fresnelRim * rimMask * (u_matKind == 1 ? 0.40 : (u_matKind == 2 ? 0.34 : 0.24)) * u_rimBoost * aoRich * geoAo;

    vec3 outCol = linearToSrgb(acesFilm((Lo * directAo + ambDiff + envSpec + envCoat + sssBounce + rimGlow + u_emissive) * 1.03));
    float outAlpha = 1.0;
    if (u_alpha < 0.99) {
      float baseA = clamp(u_alpha * baseSample.a, 0.0, 1.0);
      float specLuma = dot(acesFilm((specAcc * directAo + envSpec + envCoat + rimGlow) * 1.03), vec3(0.2126, 0.7152, 0.0722));
      outAlpha = clamp(baseA + pow(1.0 - NoV, 3.0) * 0.44 + specLuma * 0.58, 0.0, 0.90);
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
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(pos), 0, 3),
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(norm), 1, 3),
      uploadBuffer(gl, gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx)),
    ];
    gl.bindVertexArray(null);
    return { vao, count: idx.length, halfSpanX, radius, dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); } };
  }

  function matKindFor(mat) {
    const n = mat.name || "";
    if (n.startsWith("eye_") || n.startsWith("glass_") || (mat.metallic < 0.35 && mat.roughness < 0.22 && !mat.normalTex)) return 1;
    if (n.startsWith("chitin_")) return 2;
    return 0;
  }

  function applyAnimChannels(overrides, channels, t) {
    for (const ch of channels) {
      const val = sampleChannel(ch, t);
      if (!val) continue;
      let ov = overrides.get(ch.nodeIndex);
      if (!ov) { ov = {}; overrides.set(ch.nodeIndex, ov); }
      ov[ch.path] = val;
    }
  }

  function evaluateWorldMats(inst, opts) {
    const m = inst.model, bi = m.blinkInfo;
    const isClosed = opts.blinkMode === "closed", isPulsing = !isClosed && inst.blink && inst.blink.active;
    const hasIdle = m.idleAnims && m.idleAnims.length > 0 && (opts.idleAnim || opts.fixedIdleTime !== null);
    if (!hasIdle && (!bi || !bi.hasBlink || (!isClosed && !isPulsing))) return m.baseWorldMats;

    const overrides = new Map();
    if (hasIdle) {
      for (const anim of m.idleAnims) {
        const rawT = opts.fixedIdleTime !== null ? opts.fixedIdleTime : (inst.idleTime || 0);
        applyAnimChannels(overrides, anim.channels, ((rawT % anim.duration) + anim.duration) % anim.duration);
      }
    }
    if (bi && bi.hasBlink && (isClosed || isPulsing)) {
      if (bi.anim && bi.anim.channels.length) {
        applyAnimChannels(overrides, bi.anim.channels, isClosed ? bi.closedTime : Math.min(bi.endTime, bi.startTime + inst.blink.elapsed));
      } else if (bi.eyelidNodes.length) {
        const u = inst.blink ? inst.blink.elapsed / 0.22 : 1;
        const amt = isClosed ? 1 : (u < 0.35 ? u / 0.35 : (u < 0.60 ? 1 : Math.max(0, (1 - u) / 0.40)));
        const qx = [Math.sin(0.575 * amt), 0, 0, Math.cos(0.575 * amt)];
        for (const ni of bi.eyelidNodes) {
          const nd = m.nodeDefs[ni];
          let ov = overrides.get(ni);
          if (!ov) { ov = {}; overrides.set(ni, ov); }
          ov.rotation = quatMul(ov.rotation || (nd ? nd.r : [0, 0, 0, 1]), qx);
        }
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
      this.fallbacks = createFallbackTextures(gl);
      this.plinth = null; this.plinthKey = "";
      this.uBack = getUniforms(gl, this.backdropProg, ["u_topColor", "u_midColor", "u_botColor"]);
      this.uPbr = getUniforms(gl, this.pbrProg, [
        "u_model", "u_normalMat", "u_viewProj", "u_cameraPos", "u_viewDir", "u_isOrtho",
        "u_baseColor", "u_alpha", "u_metallic", "u_roughness", "u_emissive", "u_normalScale",
        "u_matKind", "u_hasNormalTex", "u_baseTex", "u_normalTex", "u_mrTex",
        "u_keyDir", "u_fillDir", "u_rimDir", "u_skyColor", "u_groundColor", "u_rimBoost",
      ]);
      this.uPlinth = getUniforms(gl, this.plinthProg, [
        "u_viewProj", "u_topBase", "u_sideBase", "u_ringColor", "u_keyDir",
        "u_shadows[0]", "u_shadowCount", "u_halfSpanX", "u_radius",
      ]);
      gl.useProgram(this.pbrProg);
      gl.uniform1i(this.uPbr.u_baseTex, 0);
      gl.uniform1i(this.uPbr.u_normalTex, 1);
      gl.uniform1i(this.uPbr.u_mrTex, 2);
      gl.useProgram(null);
      gl.vertexAttrib2f(2, 0, 0);
      gl.vertexAttrib3f(3, 1, 1, 1);
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
      const gl = this.gl, pal = opts.dark ? STUDIO_PALETTE.dark : STUDIO_PALETTE.light, team = opts.teamColors || resolveTeam(opts.team, opts.customColor);
      gl.viewport(0, 0, opts.width, opts.height); gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      if (!opts.transparent) {
        gl.disable(gl.DEPTH_TEST); gl.depthMask(false); gl.disable(gl.BLEND); gl.disable(gl.CULL_FACE);
        gl.useProgram(this.backdropProg);
        gl.uniform3fv(this.uBack.u_topColor, pal.bgTop); gl.uniform3fv(this.uBack.u_midColor, pal.bgMid); gl.uniform3fv(this.uBack.u_botColor, pal.bgBot);
        gl.bindVertexArray(this.emptyVao); gl.drawArrays(gl.TRIANGLES, 0, 3); gl.bindVertexArray(null);
      }
      gl.enable(gl.DEPTH_TEST); gl.depthMask(true);
      const camera = this.computeCamera(scene, cam, opts.width, opts.height, opts.overrideProj);
      const keyDir = this.rotateLight(-0.74, 0.62, 0.30, camera.yaw);
      const fillDir = this.rotateLight(0.84, 0.28, 0.22, camera.yaw);
      const rimDir = this.rotateLight(0.38, 0.54, -0.82, camera.yaw);

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
      gl.vertexAttrib2f(2, 0, 0);
      gl.vertexAttrib3f(3, 1, 1, 1);
      gl.uniformMatrix4fv(this.uPbr.u_viewProj, false, camera.viewProj);
      gl.uniform3fv(this.uPbr.u_cameraPos, camera.eye); gl.uniform3fv(this.uPbr.u_viewDir, camera.viewDir);
      gl.uniform1i(this.uPbr.u_isOrtho, camera.isOrtho ? 1 : 0);
      gl.uniform3fv(this.uPbr.u_keyDir, keyDir); gl.uniform3fv(this.uPbr.u_fillDir, fillDir); gl.uniform3fv(this.uPbr.u_rimDir, rimDir);
      gl.uniform3fv(this.uPbr.u_skyColor, pal.sky); gl.uniform3fv(this.uPbr.u_groundColor, pal.ground);
      gl.uniform1f(this.uPbr.u_rimBoost, opts.transparent ? 0.85 : pal.rimBoost);

      // 2-pass rendering across all instances: Pass 0 opaque, Pass 1 translucent glass lenses
      const instMats = scene.instances.map((inst) => evaluateWorldMats(inst, opts));
      for (let pass = 0; pass < 2; pass++) {
        const wantBlend = pass === 1;
        if (wantBlend) {
          gl.enable(gl.BLEND); gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false);
        } else { gl.disable(gl.BLEND); gl.depthMask(true); }
        scene.instances.forEach((inst, instIdx) => {
          const worldMats = instMats[instIdx];
          for (const item of inst.model.drawCalls) {
            const prim = item.prim, mat = prim.material;
            if (Boolean(mat.isTranslucent) !== wantBlend) continue;
            let bc = mat.baseColor;
            if (mat.name === "chitin_team") bc = [team.team[0], team.team[1], team.team[2], bc[3]];
            else if (mat.name === "chitin_dark") bc = [team.dark[0], team.dark[1], team.dark[2], bc[3]];
            else if (mat.name === "chitin_highlight") {
              bc = [
                team.team[0] * 0.66 + 0.28,
                team.team[1] * 0.66 + 0.31,
                team.team[2] * 0.66 + 0.24,
                bc[3],
              ];
            }
            if (mat.doubleSided) gl.disable(gl.CULL_FACE); else { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); }
            const nodeMat = worldMats[item.nodeIndex] || item.worldMat;
            const modelMat = inst.offsetMat ? mat4Multiply(inst.offsetMat, nodeMat) : nodeMat;
            gl.frontFace(mat3DetFromMat4(modelMat) < 0 ? gl.CW : gl.CCW);
            gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, mat.baseTex || this.fallbacks.white);
            gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, mat.normalTex || this.fallbacks.flatNormal);
            gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, mat.mrTex || this.fallbacks.white);
            gl.uniformMatrix4fv(this.uPbr.u_model, false, modelMat); gl.uniformMatrix3fv(this.uPbr.u_normalMat, false, mat3NormalFromMat4(modelMat));
            gl.uniform3f(this.uPbr.u_baseColor, bc[0], bc[1], bc[2]); gl.uniform1f(this.uPbr.u_alpha, bc[3]);
            gl.uniform1f(this.uPbr.u_metallic, mat.metallic); gl.uniform1f(this.uPbr.u_roughness, mat.roughness);
            gl.uniform1f(this.uPbr.u_normalScale, mat.normalScale);
            gl.uniform1i(this.uPbr.u_hasNormalTex, mat.normalTex && prim.hasUv ? 1 : 0);
            gl.uniform1i(this.uPbr.u_matKind, matKindFor(mat));
            gl.uniform3fv(this.uPbr.u_emissive, mat.emissive);
            gl.bindVertexArray(prim.vao);
            if (prim.indexed) gl.drawElements(prim.mode, prim.drawCount, prim.indexType, 0); else gl.drawArrays(prim.mode, 0, prim.drawCount);
          }
        });
      }
      gl.activeTexture(gl.TEXTURE0);
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
    const sel = "button:not(:disabled), input.team-color-input:not(:disabled)";
    const getItems = () => Array.from(toolbarEl.querySelectorAll(sel));
    const init = getItems();
    if (!init.length) return;
    init.forEach((b, i) => b.setAttribute("tabindex", i === 0 ? "0" : "-1"));
    const activate = (target, doFocus) => {
      for (const b of getItems()) b.setAttribute("tabindex", b === target ? "0" : "-1");
      if (doFocus) target.focus();
    };
    toolbarEl.addEventListener("focusin", (e) => { const item = e.target.closest(sel); if (item && toolbarEl.contains(item)) activate(item, false); });
    toolbarEl.addEventListener("keydown", (e) => {
      const list = getItems(), idx = list.indexOf(document.activeElement);
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
    const colorInput = $("input-team-color"), customSwatch = $("swatch-custom");
    if (!("focusGroup" in HTMLElement.prototype)) installToolbarRovingTabindex(toolbarEl);

    const RELOAD_STORAGE_KEY = "ants_viewer_reload_state_v1";
    let savedView = null;
    try {
      const raw = window.sessionStorage.getItem(RELOAD_STORAGE_KEY);
      if (raw) {
        window.sessionStorage.removeItem(RELOAD_STORAGE_KEY);
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed === "object") savedView = parsed;
      }
    } catch (_) {}

    const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");
    const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
    const params = new URLSearchParams(window.location.search), getP = (k) => (params.get(k) || "").toLowerCase();
    const pLineup = getP("lineup") === "1" || getP("lineup") === "true";
    const pAnt = getP("ant"), pTeam = getP("team"), pColor = normalizeHex(params.get("color")), pAngle = getP("angle"), pProj = getP("proj"), pTurn = getP("turntable"), pBlink = getP("blink"), pIdle = getP("idle"), pSprite = getP("sprite");
    const pIdleTimeRaw = params.get("idleTime"), fixedIdleTime = pIdleTimeRaw !== null && Number.isFinite(Number(pIdleTimeRaw)) ? Number(pIdleTimeRaw) : null;
    const savedAnt = !params.has("lineup") && !params.has("ant") && savedView && (ANT_CLASSES.includes(savedView.ant) || savedView.ant === "lineup") ? savedView.ant : null;
    const initAnt = pLineup ? "lineup" : (ANT_CLASSES.includes(pAnt) || pAnt === "lineup" ? pAnt : (savedAnt || "worker"));
    const savedColor = normalizeHex(savedView && savedView.customColor);
    const initCustomColor = pColor || savedColor || DEFAULT_CUSTOM_HEX;
    const savedTeam = !params.has("team") && savedView && (savedView.team === "custom" || TEAMS[savedView.team]) ? savedView.team : null;
    const initTeam = (pTeam === "custom" || TEAMS[pTeam]) ? pTeam : (pColor ? "custom" : (savedTeam || "green"));
    const savedAngle = !params.has("angle") && savedView && (ANGLES[savedView.angle] || savedView.angle === "custom") ? savedView.angle : null;
    const initAngle = ANGLES[pAngle] ? pAngle : (savedAngle || "hero");
    const initProj = pProj === "ortho" || pProj === "persp" ? pProj
      : (!params.has("proj") && savedView && (savedView.proj === "ortho" || savedView.proj === "persp") ? savedView.proj : (ANGLES[initAngle] && initAngle !== "hero" ? "ortho" : "persp"));
    const initTurn = pTurn === "0" || pTurn === "false" ? false : (pTurn === "1" || pTurn === "true" ? true
      : (!params.has("turntable") && savedView && typeof savedView.turntable === "boolean" ? savedView.turntable : (!motionQuery.matches && !pAngle && !pLineup)));
    const initSprite = pSprite === "0" || pSprite === "false" ? false : (pSprite === "1" || pSprite === "true" ? true
      : (!params.has("sprite") && savedView && typeof savedView.sprite === "boolean" ? savedView.sprite : true));
    const initBlink = pBlink === "closed" ? "closed" : (pBlink === "open" ? "open" : (motionQuery.matches ? "open" : "auto"));
    const initIdle = fixedIdleTime !== null ? false : (pIdle === "0" || pIdle === "false" ? false : (pIdle === "1" || pIdle === "true" ? true
      : (!params.has("idle") && savedView && typeof savedView.idle === "boolean" ? savedView.idle : !motionQuery.matches)));

    const preset = ANGLES[initAngle] || ANGLES.hero;
    const useSavedCam = !params.has("angle") && savedView && Number.isFinite(savedView.yaw) && Number.isFinite(savedView.pitch) && Number.isFinite(savedView.zoom);
    let pendingUpdatedVer = savedView && typeof savedView.updatedVersion === "string" ? savedView.updatedVersion : "";
    const state = {
      ant: initAnt, customLabel: "", team: initTeam, customColor: initCustomColor, angle: initAngle, blinkMode: initBlink, idleAnim: initIdle, fixedIdleTime,
      dark: darkQuery.matches, turntable: initTurn, spritePreview: initSprite,
      cam: {
        yaw: useSavedCam ? wrapAngleDeg(savedView.yaw) : preset.yaw,
        pitch: useSavedCam ? clamp(savedView.pitch, -10, 85) : preset.pitch,
        zoom: useSavedCam ? clamp(savedView.zoom, 0.35, 3.2) : 1.0,
        projection: initProj,
      },
      camTarget: null, dirty: true, spriteDirty: true, lastSpriteTime: 0, loadSeq: 0, ready: false, error: "",
      modelCache: new Map(), customModel: null, scene: emptyScene(),
    };
    const activeTeam = () => resolveTeam(state.team, state.customColor);
    window.__antViewerGetState = () => ({
      ant: state.ant === "custom" ? "" : state.ant,
      team: state.team, customColor: state.customColor, angle: state.angle, proj: state.cam.projection,
      turntable: state.turntable, idle: state.idleAnim, sprite: state.spritePreview,
      yaw: state.cam.yaw, pitch: state.cam.pitch, zoom: state.cam.zoom,
    });

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
      const phaseStagger = [0.0, 1.13, 0.47, 1.89, 0.78, 2.31];
      entries.forEach((e, i) => {
        const m = e.model, slotX = multi ? cursor + widths[i] * 0.5 : 0;
        if (multi) cursor += widths[i] + gap;
        const yLift = Math.abs(m.bounds.min[1]) > 0.02 ? -m.bounds.min[1] : 0;
        instances.push({
          id: e.id, model: m, offsetMat: mat4Translate(slotX - m.center[0], yLift, -m.center[2]),
          blink: { active: false, elapsed: 0, wait: 1.3 + i * 0.65 + Math.random() * 2.1 },
          idleTime: multi ? phaseStagger[i % phaseStagger.length] : 0,
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
        ready: state.ready, ant: state.ant, team: state.team, customColor: state.customColor, angle: state.angle, blink: state.blinkMode, idle: state.idleAnim,
        hasBlinkAnim: state.scene.instances.some((inst) => inst.model.blinkInfo && inst.model.blinkInfo.hasBlink),
        idleAnims: state.scene.instances.flatMap((inst) => (inst.model.idleAnims || []).map((a) => a.name)),
        textureCount: state.scene.instances.reduce((sum, inst) => sum + (inst.model.textureCount || 0), 0),
        hasVertexColors: state.scene.instances.some((inst) => Boolean(inst.model.hasVertexColors)),
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
      if (colorInput && colorInput.value.toLowerCase() !== state.customColor) colorInput.value = state.customColor;
      if (customSwatch) customSwatch.style.backgroundColor = state.customColor;
      $("btn-turntable").setAttribute("aria-pressed", String(state.turntable));
      $("btn-idle").setAttribute("aria-pressed", String(state.idleAnim));
      $("btn-sprite-scale").setAttribute("aria-pressed", String(state.spritePreview));
      $("btn-blink-hold").setAttribute("aria-pressed", String(state.blinkMode === "closed"));
      spriteInset.hidden = !state.spritePreview;
      const antLabel = state.ant === "custom" ? (state.customLabel || "Custom .GLB") : (ANT_LABELS[state.ant] || state.ant);
      const teamLabel = activeTeam().label, angleLabel = ANGLES[state.angle] ? ANGLES[state.angle].label : "Custom";
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
      const resp = await fetch("models/" + encodeURIComponent(id) + ".glb", { cache: "no-cache" });
      if (!resp.ok) throw new Error("HTTP " + resp.status + " loading models/" + id + ".glb");
      const parsed = await parseGlb(await resp.arrayBuffer(), renderer.gl);
      if (state.modelCache.has(id)) { parsed.dispose(); return state.modelCache.get(id); }
      state.modelCache.set(id, parsed);
      return parsed;
    }

    async function loadManifest() {
      try {
        const resp = await fetch("models/manifest.json", { cache: "no-cache" });
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
        const upd = pendingUpdatedVer ? ("Updated to " + pendingUpdatedVer + ". ") : "";
        pendingUpdatedVer = "";
        updateUiState(); markDirty(); announce(upd + "Showing All 6 Lineup (" + loaded.length + " of 6 models loaded)."); publishState(true); return;
      }
      try {
        announce("Loading " + (ANT_LABELS[antId] || antId) + " model…");
        const model = await fetchModel(antId);
        if (seq !== state.loadSeq) return;
        const upd = pendingUpdatedVer ? ("Updated to " + pendingUpdatedVer + ". ") : "";
        pendingUpdatedVer = "";
        buildScene([{ id: antId, model }]); updateUiState(); markDirty();
        announce(upd + "Loaded " + (ANT_LABELS[antId] || antId) + " (" + model.triCount.toLocaleString() + " triangles)."); publishState(true);
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
          const parsed = await parseGlb(buf, renderer.gl);
          if (seq !== state.loadSeq) { parsed.dispose(); return; }
          const stem = file.name.replace(/\.glb$/i, "").toLowerCase();
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
      const teamObj = activeTeam();
      SHEET_ANGLES.forEach((key, idx) => {
        const info = ANGLES[key], x = idx * size;
        const img = renderer.renderOffscreenPixels(size, size, state.scene, { yaw: info.yaw, pitch: info.pitch, zoom: 1, projection: "ortho" }, {
          transparent: true, showPlinth: false, dark: false, team: state.team, teamColors: teamObj, blinkMode: state.blinkMode,
          idleAnim: state.idleAnim, fixedIdleTime: state.fixedIdleTime, overrideProj: "ortho",
        });
        tCtx.putImageData(img, 0, 0); ctx.drawImage(tmp, x, 0);
        ctx.fillStyle = "rgba(20, 24, 31, 0.86)"; ctx.beginPath();
        if (typeof ctx.roundRect === "function") ctx.roundRect(x + 12, size + 9, size - 24, 26, 6); else ctx.rect(x + 12, size + 9, size - 24, 26);
        ctx.fill();
        ctx.fillStyle = "#ffffff"; ctx.font = "600 13px system-ui, -apple-system, sans-serif";
        ctx.textAlign = "center"; ctx.textBaseline = "middle"; ctx.fillText(info.full, x + size * 0.5, size + 22.5);
      });
      const slug = state.ant === "custom" ? (state.customLabel || "custom").replace(/\.glb$/i, "").replace(/[^a-z0-9_-]+/gi, "_") : state.ant;
      const fileName = "ants-" + slug + "-" + (teamObj.slug || state.team) + "-5angle.png";
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

    function toggleIdle() {
      state.idleAnim = !state.idleAnim;
      state.fixedIdleTime = null;
      updateUiState(); markDirty(); announce("Idle animation " + (state.idleAnim ? "enabled." : "paused."));
    }

    toolbarEl.addEventListener("click", (e) => {
      const btn = e.target.closest("button");
      if (!btn) return;
      if (btn.dataset.ant) selectAnt(btn.dataset.ant);
      else if (btn.dataset.team === "custom") {
        if (state.team === "custom" && colorInput) colorInput.click();
        else { state.team = "custom"; updateUiState(); markDirty(); announce("Team color set to " + activeTeam().label + "."); }
      }
      else if (btn.dataset.team && TEAMS[btn.dataset.team]) { state.team = btn.dataset.team; updateUiState(); markDirty(); announce("Team color set to " + TEAMS[state.team].label + "."); }
      else if (btn.dataset.angle) { setAnglePreset(btn.dataset.angle); announce("Camera set to " + ANGLES[btn.dataset.angle].full + "."); }
      else if (btn.dataset.projection) { state.cam.projection = btn.dataset.projection === "ortho" ? "ortho" : "persp"; updateUiState(); markDirty(); announce("Projection set to " + (state.cam.projection === "ortho" ? "Orthographic." : "Perspective.")); }
      else if (btn.id === "btn-turntable") { state.turntable = !state.turntable; state.camTarget = null; updateUiState(); markDirty(false); announce("Turntable spin " + (state.turntable ? "enabled." : "paused.")); }
      else if (btn.id === "btn-idle") toggleIdle();
      else if (btn.id === "btn-sprite-scale") { state.spritePreview = !state.spritePreview; updateUiState(); markDirty(true); announce("48px sprite-scale preview " + (state.spritePreview ? "shown." : "hidden.")); }
      else if (btn.id === "btn-blink-hold") toggleBlinkHold();
      else if (btn.id === "btn-blink") triggerBlink();
      else if (btn.id === "btn-export-sheet") exportFiveAngleSheet();
      else if (btn.id === "btn-download-glb") downloadActiveGlb();
      else if (btn.id === "btn-reset-view") { const key = ANGLES[state.angle] ? state.angle : "hero"; setAnglePreset(key); announce("View reset to " + ANGLES[key].full + "."); }
      else if (btn.id === "btn-load-glb") fileInput.click();
    });

    if (colorInput) {
      const applyPickedColor = () => {
        const hex = normalizeHex(colorInput.value);
        if (!hex) return;
        state.customColor = hex; state.team = "custom"; updateUiState(); markDirty();
        announce("Team color set to " + activeTeam().label + ".");
      };
      colorInput.addEventListener("click", () => { if (state.team !== "custom") { state.team = "custom"; updateUiState(); markDirty(); announce("Team color set to " + activeTeam().label + "."); } });
      colorInput.addEventListener("input", applyPickedColor);
      colorInput.addEventListener("change", applyPickedColor);
    }

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
      if (e.pointerType === "touch") e.preventDefault();
      try { stageCanvas.setPointerCapture(e.pointerId); } catch (_) {}
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
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
    stageCanvas.addEventListener("lostpointercapture", endPtr);
    const preventPinchZoom = (e) => { if (!e.touches || e.touches.length >= 2) e.preventDefault(); };
    stageCanvas.addEventListener("touchmove", preventPinchZoom, { passive: false });
    stageCanvas.addEventListener("gesturestart", preventPinchZoom, { passive: false });
    stageCanvas.addEventListener("gesturechange", preventPinchZoom, { passive: false });
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
      else if (e.key === "i" || e.key === "I") { e.preventDefault(); toggleIdle(); return; }
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
      if (state.idleAnim) {
        for (const inst of state.scene.instances) {
          if (inst.model.idleAnims && inst.model.idleAnims.length) {
            inst.idleTime = (inst.idleTime || 0) + dt;
            state.dirty = true;
            if (now - state.lastSpriteTime > 65) state.spriteDirty = true;
          }
        }
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
        renderer.drawScene(state.scene, state.cam, {
          width: w, height: h, transparent: false, showPlinth: true, dark: state.dark, team: state.team, teamColors: activeTeam(),
          blinkMode: state.blinkMode, idleAnim: state.idleAnim, fixedIdleTime: state.fixedIdleTime,
        });
        state.dirty = false;
      }
      if (state.spriteDirty && state.spritePreview) {
        state.spriteDirty = false; state.lastSpriteTime = now;
        spriteCtx.clearRect(0, 0, spriteCanvas.width, spriteCanvas.height);
        if (state.scene.instances.length) {
          const spriteCam = { yaw: state.cam.yaw, pitch: state.cam.pitch, zoom: 1.0, projection: "ortho" };
          spriteCtx.putImageData(renderer.renderOffscreenPixels(spriteCanvas.width, spriteCanvas.height, state.scene, spriteCam, {
            transparent: true, showPlinth: false, dark: false, team: state.team, teamColors: activeTeam(),
            blinkMode: state.blinkMode, idleAnim: state.idleAnim, fixedIdleTime: state.fixedIdleTime, overrideProj: "ortho",
          }), 0, 0);
        }
      }
      requestAnimationFrame(frame);
    }

    updateUiState(); loadManifest(); selectAnt(state.ant); requestAnimationFrame(frame);
  }

  document.addEventListener("DOMContentLoaded", initViewer);
})();

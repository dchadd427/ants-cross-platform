"use strict";

(() => {
  const {
    clamp, mat4Multiply, mat4FromTRS, mat4LookAt, mat4Perspective, mat4Ortho,
    mat3DetFromMat4, mat3NormalFromMat4, quatMul, sampleChannel,
    uploadBuffer, createFallbackTextures,
  } = window.__antGltf;
  const {
    FIRE_VS, FIRE_FS, buildFireParticles,
    SPARK_VS, SPARK_FS, buildSparkParticles,
    FUR_PROFILES, FUR_VS, FUR_FS,
    evalJelloWobble,
  } = window.__antFx;

  const ANT_CLASSES = ["worker", "combat", "thief", "bomber", "swimmer", "fire"];
  const ANT_LABELS = {
    worker: "Worker", combat: "Combat", thief: "Thief",
    bomber: "Bomber", swimmer: "Swimmer", fire: "Fire", lineup: "All 6 Lineup",
  };
  const srgbToLinear = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
  const rgbLinear = (rgb) => rgb.map(srgbToLinear);
  const DEFAULT_CUSTOM_HEX = "#7c3aed";
  const TEAMS = {
    green: { label: "Green", team: rgbLinear([0.25, 0.51, 0.40]), dark: rgbLinear([0.13, 0.28, 0.25]) },
    red: { label: "Red", team: rgbLinear([0.52, 0.16, 0.20]), dark: rgbLinear([0.24, 0.07, 0.10]) },
    blue: { label: "Blue", team: rgbLinear([0.28, 0.36, 0.60]), dark: rgbLinear([0.12, 0.17, 0.33]) },
    black: { label: "Black", team: rgbLinear([0.39, 0.40, 0.45]), dark: rgbLinear([0.17, 0.18, 0.22]) },
  };
  const ANGLES = {
    "301": { label: "301 S", full: "301 S (Front)", yaw: 0, pitch: 45 },
    "201": { label: "201 SE", full: "201 SE (3/4)", yaw: -45, pitch: 45 },
    "901": { label: "901 E", full: "901 E (Side)", yaw: -90, pitch: 45 },
    "801": { label: "801 NE", full: "801 NE (Back 3/4)", yaw: -135, pitch: 45 },
    "701": { label: "701 N", full: "701 N (Back)", yaw: 180, pitch: 45 },
    "801_nw": { label: "801 NW", full: "801 NW (Back-Left)", yaw: 135, pitch: 45 },
    "901_w": { label: "901 W", full: "901 W (Side Left)", yaw: 90, pitch: 45 },
    "201_sw": { label: "201 SW", full: "201 SW (Front-Left)", yaw: 45, pitch: 45 },
    hero: { label: "Hero", full: "Hero Showcase", yaw: -28, pitch: 22 },
  };
  const SHEET_ANGLES = ["301", "201", "901", "801", "701"];
  const DEG = Math.PI / 180;
  const wrapAngleDeg = (deg) => { const a = ((deg + 180) % 360 + 360) % 360 - 180; return a === -180 ? 180 : a; };

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

  const STUDIO_PALETTE = {
    light: {
      bgTop: [0.855, 0.880, 0.918], bgMid: [0.958, 0.966, 0.978], bgBot: [0.780, 0.816, 0.862],
      plinthTop: [0.68, 0.71, 0.76], plinthSide: [0.42, 0.46, 0.52], plinthRing: [0.45, 0.49, 0.56],
      sky: [0.62, 0.72, 0.82], ground: [0.34, 0.36, 0.40], rimBoost: 0.54,
    },
    dark: {
      bgTop: [0.072, 0.092, 0.130], bgMid: [0.136, 0.168, 0.220], bgBot: [0.052, 0.068, 0.098],
      plinthTop: [0.095, 0.114, 0.148], plinthSide: [0.048, 0.060, 0.082], plinthRing: [0.19, 0.25, 0.32],
      sky: [0.46, 0.58, 0.72], ground: [0.22, 0.26, 0.32], rimBoost: 0.80,
    },
  };

  const LIGHTING_ORDER = ["studio", "daylight", "golden", "moonlight", "spotlight", "dramatic", "neon", "flat"];
  const LIGHTING_PRESETS = {
    studio: {
      id: "studio", label: "Studio",
      keyBasis: [0.64, 0.56, -0.52], fillBasis: [0.42, 0.14, 0.89], rimBasis: [-0.54, 0.64, 0.54],
      keyColor: [2.75, 2.64, 2.46], fillColor: [0.36, 0.44, 0.56], rimColor: [0.68, 0.76, 0.86],
      envKeyTint: [1.0, 0.96, 0.88], envFillTint: [0.62, 0.82, 1.0], envRimTint: [0.84, 0.94, 1.0],
      spotParams: [0.42, 1.16, 1.15], lightMix: [0.96, 1.12, 1.04, 0.0], pal: null,
    },
    daylight: {
      id: "daylight", label: "Sunlit Meadow",
      keyBasis: [0.46, 0.82, -0.34], fillBasis: [0.35, 0.28, 0.89], rimBasis: [-0.62, 0.54, 0.56],
      keyColor: [2.85, 2.68, 2.34], fillColor: [0.52, 0.66, 0.86], rimColor: [0.58, 0.68, 0.52],
      envKeyTint: [1.0, 0.96, 0.84], envFillTint: [0.66, 0.86, 1.0], envRimTint: [0.84, 0.96, 0.78],
      spotParams: [0.86, 1.08, 0.75], lightMix: [1.45, 1.05, 1.04, 0.0],
      pal: {
        bgTop: [0.54, 0.76, 0.95], bgMid: [0.85, 0.92, 0.96], bgBot: [0.64, 0.72, 0.54],
        plinthTop: [0.64, 0.68, 0.56], plinthSide: [0.40, 0.44, 0.35], plinthRing: [0.46, 0.52, 0.38],
        sky: [0.66, 0.84, 1.00], ground: [0.42, 0.48, 0.28], rimBoost: 0.62, darkPanel: false,
      },
    },
    golden: {
      id: "golden", label: "Golden Hour",
      keyBasis: [0.34, 0.26, -0.90], fillBasis: [0.54, 0.36, 0.76], rimBasis: [-0.46, 0.52, -0.72],
      keyColor: [2.95, 1.88, 0.92], fillColor: [0.30, 0.42, 0.76], rimColor: [1.35, 0.78, 0.38],
      envKeyTint: [1.0, 0.72, 0.38], envFillTint: [0.52, 0.64, 0.96], envRimTint: [1.0, 0.68, 0.34],
      spotParams: [0.55, 1.18, 1.10], lightMix: [0.95, 1.15, 1.04, 0.0],
      pal: {
        bgTop: [0.16, 0.14, 0.26], bgMid: [0.46, 0.28, 0.22], bgBot: [0.14, 0.09, 0.12],
        plinthTop: [0.30, 0.22, 0.18], plinthSide: [0.14, 0.10, 0.09], plinthRing: [0.62, 0.40, 0.22],
        sky: [0.52, 0.44, 0.68], ground: [0.38, 0.22, 0.12], rimBoost: 0.96, darkPanel: true,
      },
    },
    moonlight: {
      id: "moonlight", label: "Moonlight",
      keyBasis: [0.34, 0.76, -0.55], fillBasis: [0.48, -0.10, 0.87], rimBasis: [-0.52, 0.72, 0.46],
      keyColor: [1.45, 1.85, 2.48], fillColor: [0.14, 0.20, 0.36], rimColor: [0.95, 1.24, 1.65],
      envKeyTint: [0.68, 0.86, 1.0], envFillTint: [0.34, 0.48, 0.80], envRimTint: [0.74, 0.92, 1.0],
      spotParams: [0.40, 1.22, 1.35], lightMix: [0.75, 1.18, 1.00, 0.0],
      pal: {
        bgTop: [0.032, 0.052, 0.108], bgMid: [0.078, 0.122, 0.215], bgBot: [0.018, 0.028, 0.062],
        plinthTop: [0.075, 0.105, 0.165], plinthSide: [0.032, 0.045, 0.078], plinthRing: [0.24, 0.38, 0.58],
        sky: [0.20, 0.32, 0.58], ground: [0.06, 0.08, 0.15], rimBoost: 1.18, darkPanel: true,
      },
    },
    spotlight: {
      id: "spotlight", label: "Spotlight",
      keyBasis: [0.58, 0.76, -0.28], fillBasis: [0.40, -0.35, 0.84], rimBasis: [-0.62, 0.62, 0.48],
      keyColor: [3.35, 3.22, 2.95], fillColor: [0.07, 0.08, 0.11], rimColor: [0.52, 0.56, 0.66],
      envKeyTint: [1.0, 0.96, 0.88], envFillTint: [0.28, 0.32, 0.40], envRimTint: [0.78, 0.84, 0.94],
      spotParams: [0.06, 1.38, 2.80], lightMix: [0.28, 1.25, 1.02, 0.0],
      pal: {
        bgTop: [0.020, 0.022, 0.030], bgMid: [0.095, 0.102, 0.122], bgBot: [0.014, 0.016, 0.022],
        plinthTop: [0.145, 0.152, 0.172], plinthSide: [0.035, 0.038, 0.046], plinthRing: [0.42, 0.44, 0.50],
        sky: [0.16, 0.18, 0.24], ground: [0.05, 0.05, 0.07], rimBoost: 0.72, darkPanel: true,
      },
    },
    dramatic: {
      id: "dramatic", label: "Dramatic",
      keyBasis: [0.24, 0.22, -0.94], fillBasis: [0.55, -0.22, 0.80], rimBasis: [-0.46, 0.56, 0.69],
      keyColor: [3.25, 2.86, 2.40], fillColor: [0.08, 0.13, 0.20], rimColor: [0.55, 1.32, 1.72],
      envKeyTint: [1.0, 0.88, 0.72], envFillTint: [0.24, 0.36, 0.52], envRimTint: [0.42, 0.88, 1.0],
      spotParams: [0.22, 1.28, 1.85], lightMix: [0.34, 1.30, 1.04, 0.0],
      pal: {
        bgTop: [0.032, 0.038, 0.052], bgMid: [0.076, 0.094, 0.126], bgBot: [0.018, 0.022, 0.030],
        plinthTop: [0.082, 0.096, 0.118], plinthSide: [0.028, 0.034, 0.044], plinthRing: [0.22, 0.42, 0.54],
        sky: [0.18, 0.28, 0.40], ground: [0.06, 0.07, 0.09], rimBoost: 1.28, darkPanel: true,
      },
    },
    neon: {
      id: "neon", label: "Neon Arcade",
      keyBasis: [0.42, 0.32, -0.85], fillBasis: [0.44, 0.18, 0.88], rimBasis: [-0.52, 0.66, 0.54],
      keyColor: [2.75, 0.66, 2.05], fillColor: [0.26, 1.68, 1.98], rimColor: [0.42, 1.65, 2.00],
      envKeyTint: [1.0, 0.32, 0.78], envFillTint: [0.18, 0.90, 1.0], envRimTint: [0.24, 0.92, 1.0],
      spotParams: [0.42, 1.20, 1.25], lightMix: [0.52, 1.35, 1.02, 0.0],
      pal: {
        bgTop: [0.075, 0.028, 0.138], bgMid: [0.145, 0.058, 0.220], bgBot: [0.024, 0.058, 0.118],
        plinthTop: [0.105, 0.068, 0.165], plinthSide: [0.038, 0.026, 0.072], plinthRing: [0.24, 0.72, 0.86],
        sky: [0.34, 0.18, 0.52], ground: [0.08, 0.22, 0.32], rimBoost: 1.30, darkPanel: true,
      },
    },
    flat: {
      id: "flat", label: "Flat / 1998",
      keyBasis: [0.88, 0.38, -0.28], fillBasis: [0.56, -0.18, 0.80], rimBasis: [-0.58, 0.68, 0.44],
      keyColor: [1.0, 1.0, 1.0], fillColor: [0.0, 0.0, 0.0], rimColor: [0.0, 0.0, 0.0],
      envKeyTint: [1.0, 1.0, 1.0], envFillTint: [1.0, 1.0, 1.0], envRimTint: [1.0, 1.0, 1.0],
      spotParams: [1.0, 1.0, 1.0], lightMix: [1.0, 0.0, 1.0, 1.0], pal: null,
    },
  };

  function validLighting(raw) {
    if (typeof raw !== "string") return null;
    const k = raw.trim().toLowerCase();
    return Object.prototype.hasOwnProperty.call(LIGHTING_PRESETS, k) ? k : null;
  }
  function getLightingPreset(id) { return LIGHTING_PRESETS[validLighting(id) || "studio"]; }
  function populateLightingSelect(selectEl, activeId) {
    if (!selectEl) return;
    if (!selectEl.options.length) {
      for (const id of LIGHTING_ORDER) {
        const opt = document.createElement("option");
        opt.value = id; opt.textContent = LIGHTING_PRESETS[id].label;
        selectEl.appendChild(opt);
      }
    }
    selectEl.value = validLighting(activeId) || "studio";
    selectEl.dataset.extended = String(selectEl.value !== "studio");
  }
  const rgbCss = (c) => `rgb(${Math.round(clamp(c[0], 0, 1) * 255)}, ${Math.round(clamp(c[1], 0, 1) * 255)}, ${Math.round(clamp(c[2], 0, 1) * 255)})`;
  function paintPresetBackdrop2d(ctx, x0, w, h, groundY, lightingId) {
    const pal = getLightingPreset(lightingId).pal;
    if (!pal) return false;
    const g = ctx.createLinearGradient(x0, 0, x0, h);
    g.addColorStop(0, rgbCss(pal.bgTop)); g.addColorStop(0.55, rgbCss(pal.bgMid)); g.addColorStop(1, rgbCss(pal.bgBot));
    ctx.fillStyle = g; ctx.fillRect(x0, 0, w, h);
    const fg = ctx.createLinearGradient(x0, groundY, x0, h);
    fg.addColorStop(0, pal.darkPanel ? "rgba(0,0,0,0.34)" : "rgba(20,32,48,0.12)");
    fg.addColorStop(1, "rgba(0,0,0,0.06)");
    ctx.fillStyle = fg; ctx.fillRect(x0, groundY, w, h - groundY);
    ctx.strokeStyle = pal.darkPanel ? "rgba(255,255,255,0.20)" : "rgba(20,32,48,0.20)";
    ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(x0, groundY + 0.5); ctx.lineTo(x0 + w, groundY + 0.5); ctx.stroke();
    return true;
  }

  // --- WebGL2 Shaders (Studio Backdrop, Plinth + Contact Shadows, Pass 7 Cinematic PBR) ---
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
  uniform vec4 u_jelloWobble;
  out vec3 v_worldPos, v_worldNormal, v_color;
  out vec2 v_uv;
  void main() {
    vec3 lp = a_position, ln = a_normal;
    if (u_jelloWobble.w > 0.01) {
      float h = clamp(lp.y / u_jelloWobble.w, 0.0, 1.0);
      float sway = h * h, midW = sin(h * 3.14159265);
      lp.xz *= 1.0 + u_jelloWobble.z * midW;
      lp.x += u_jelloWobble.x * sway;
      lp.z += u_jelloWobble.y * sway;
      float dScale = 2.0 * h / u_jelloWobble.w;
      ln.x -= dScale * u_jelloWobble.x * ln.y;
      ln.z -= dScale * u_jelloWobble.y * ln.y;
    }
    vec4 wp = u_model * vec4(lp, 1.0);
    v_worldPos = wp.xyz;
    v_worldNormal = normalize(u_normalMat * normalize(ln));
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_viewProj * wp;
  }`;

  const PBR_FS = `#version 300 es
  precision highp float;
  const float PI = 3.14159265359;
  in vec3 v_worldPos, v_worldNormal, v_color;
  in vec2 v_uv;
  uniform vec3 u_cameraPos, u_viewDir, u_baseColor, u_emissive, u_keyDir, u_fillDir, u_rimDir, u_skyColor, u_groundColor, u_silhouetteColor;
  uniform vec3 u_keyColor, u_fillColor, u_rimColor, u_envKeyTint, u_envFillTint, u_envRimTint, u_spotParams;
  uniform vec4 u_lightMix;
  uniform float u_alpha, u_metallic, u_roughness, u_normalScale, u_rimBoost;
  uniform int u_isOrtho, u_matKind, u_hasNormalTex, u_silhouette;
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
    float upT = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 env = mix(u_groundColor * 0.62, u_skyColor * 1.18, smoothstep(0.12, 0.88, upT));
    // Warm studio cyclorama floor bounce band near horizon + crisp overhead softbox canopy
    float horizon = exp(-abs(R.y + 0.06) * 5.5);
    env += mix(u_groundColor, u_envKeyTint * 0.42, 0.45) * horizon * 0.22;
    float sharp = (1.0 - rough) * (1.0 - rough);
    float glintExp = mix(20.0, 240.0, sharp);
    float topSoftbox = smoothstep(0.56, 0.94, R.y) * (0.32 + 0.38 * sharp);
    float keySoft = smoothstep(0.68, 0.96, dot(R, u_keyDir)) * 1.05 + pow(max(dot(R, u_keyDir), 0.0), glintExp) * 1.35 * sharp;
    float fillStrip = smoothstep(0.74, 0.97, dot(R, u_fillDir)) * 0.30;
    float rimSoft = smoothstep(0.70, 0.96, dot(R, u_rimDir)) * 0.44 + pow(max(dot(R, u_rimDir), 0.0), glintExp * 0.7) * 0.52 * sharp;
    return env + u_envKeyTint * (keySoft * sharp + topSoftbox * 0.55) + u_envFillTint * (fillStrip * sharp + topSoftbox * 0.28) + u_envRimTint * rimSoft * sharp;
  }
  vec3 evalLight(vec3 L, vec3 lCol, vec3 N, vec3 V, vec3 albedo, float met, float rough, vec3 F0, float coatW, float coatRough, vec3 coatTint, inout vec3 specAcc) {
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4), NoH = max(dot(N, H), 0.0), HoV = max(dot(H, V), 0.0);
    vec3 F = F_Schlick(HoV, F0);
    vec3 spec = (D_GGX(NoH, max(rough * rough, 0.032)) * G_Smith(NoV, NoL, rough) * F) / (4.0 * NoV * NoL + 1e-4);
    vec3 diff = (vec3(1.0) - F) * (1.0 - met) * albedo / PI;
    float coatA = max(coatRough * coatRough, 0.018);
    float coat = coatW * (D_GGX(NoH, coatA) * G_Smith(NoV, NoL, coatRough) * (0.04 + 0.96 * pow(1.0 - HoV, 5.0))) / (4.0 * NoV * NoL + 1e-4);
    vec3 specTerm = (spec + coatTint * coat) * lCol * NoL;
    specAcc += specTerm;
    return diff * lCol * NoL + specTerm;
  }
  vec3 acesFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
  vec3 linearToSrgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }
  // Lightweight mobile-friendly 3D world-space value noise for seamless stone mineral grain, quartz sparkle & organic food micro-relief
  float hash31(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
  float noise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n00 = mix(hash31(i), hash31(i + vec3(1.0, 0.0, 0.0)), f.x);
    float n10 = mix(hash31(i + vec3(0.0, 1.0, 0.0)), hash31(i + vec3(1.0, 1.0, 0.0)), f.x);
    float n01 = mix(hash31(i + vec3(0.0, 0.0, 1.0)), hash31(i + vec3(1.0, 0.0, 1.0)), f.x);
    float n11 = mix(hash31(i + vec3(0.0, 1.0, 1.0)), hash31(i + vec3(1.0, 1.0, 1.0)), f.x);
    return mix(mix(n00, n10, f.y), mix(n01, n11, f.y), f.z);
  }
  float fbm3(vec3 p) { return noise3(p) * 0.55 + noise3(p * 2.13 + vec3(13.7, 7.1, 4.3)) * 0.30 + noise3(p * 4.41 + vec3(5.2, 19.4, 11.8)) * 0.15; }

  void main() {
    if (v_worldPos.y < -0.006) discard;
    if (u_silhouette != 0) {
      fragColor = vec4(u_silhouetteColor, 1.0);
      return;
    }
    vec3 N = normalize(v_worldNormal);
    if (!gl_FrontFacing) N = -N;
    float normSlope = 0.0;
    if (u_hasNormalTex == 1 && u_normalScale > 0.0) N = applyNormalMap(N, v_worldPos, v_uv, normSlope);
    // Subtle 3D world-space micro-perturbation for geological stone (5) & wet organic food (6) — zero UV seams!
    float stoneGrain = 0.5, stoneFine = 0.5;
    if (u_matKind == 5) {
      stoneGrain = fbm3(v_worldPos * 16.0);
      stoneFine = noise3(v_worldPos * 58.0);
      float gx = fbm3(v_worldPos * 22.0 + vec3(0.012, 0.0, 0.0)) - fbm3(v_worldPos * 22.0 - vec3(0.012, 0.0, 0.0));
      float gz = fbm3(v_worldPos * 22.0 + vec3(0.0, 0.0, 0.012)) - fbm3(v_worldPos * 22.0 - vec3(0.0, 0.0, 0.012));
      float pert = mix(0.24, 0.10, 1.0 - smoothstep(0.38, 0.72, u_roughness));
      N = normalize(N + vec3(-gx, 0.0, -gz) * pert);
    } else if (u_matKind == 6) {
      float ripX = noise3(v_worldPos * 24.0 + vec3(0.015, 0.0, 0.0)) - noise3(v_worldPos * 24.0 - vec3(0.015, 0.0, 0.0));
      float ripZ = noise3(v_worldPos * 24.0 + vec3(0.0, 0.0, 0.015)) - noise3(v_worldPos * 24.0 - vec3(0.0, 0.0, 0.015));
      N = normalize(N + vec3(-ripX, 0.0, -ripZ) * 0.055);
    }
    vec3 V = u_isOrtho == 1 ? normalize(u_viewDir) : normalize(u_cameraPos - v_worldPos);

    vec4 baseSample = texture(u_baseTex, v_uv);
    vec4 mrSample = texture(u_mrTex, v_uv);
    float rough = clamp(u_roughness * mrSample.g, 0.04, 0.98);
    float met = clamp(u_metallic * mrSample.b, 0.0, 1.0);
    if (u_matKind == 5) {
      rough = clamp(rough + (stoneGrain - 0.5) * 0.14 + (stoneFine - 0.5) * 0.08, 0.46, 0.96);
    } else if (u_matKind == 4) {
      float fMicro = noise3(v_worldPos * 42.0);
      rough = clamp(rough + (fMicro - 0.5) * 0.10, 0.12, 0.96);
    }
    float microCav = 1.0;
    if (u_hasNormalTex == 1) {
      float pitDepth = smoothstep(0.56, 0.96, mrSample.g);
      microCav = clamp(1.0 - (pitDepth * 0.58 + clamp(normSlope, 0.0, 1.0) * 0.56) * mix(0.22, 0.05, met), 0.74, 1.0);
    }
    if (u_matKind == 5) {
      microCav *= mix(0.86, 1.08, stoneGrain) * mix(0.93, 1.06, stoneFine);
    } else if (u_matKind == 4) {
      microCav *= mix(0.92, 1.05, noise3(v_worldPos * 32.0));
    }
    // Preserve pure grayscale AO on ants while honoring true dark/saturated sculptor vertex tints on map items!
    vec3 bakedCol = clamp(v_color, 0.0, 1.0);
    float vMax = max(max(bakedCol.r, bakedCol.g), bakedCol.b);
    float vMin = min(min(bakedCol.r, bakedCol.g), bakedCol.b);
    float isGrayAo = smoothstep(0.014, 0.003, vMax - vMin) * smoothstep(0.40, 0.46, vMax);
    vec3 vChroma = vMax > 1e-4 ? (bakedCol / vMax) : vec3(1.0);
    vec3 vAlbedoMul = mix(bakedCol, vChroma, isGrayAo);
    vec3 albedo = max(u_baseColor * baseSample.rgb * vAlbedoMul * microCav, 0.0);

    // Clearcoat for glossy glass/candy (1), chitin (2), foliage (3), and wet fried-egg/liquid glaze (6)
    float coatW = u_matKind == 1 ? 0.46 * (1.0 - smoothstep(0.16, 0.46, rough))
                : (u_matKind == 2 ? 0.38 : (u_matKind == 3 ? 0.14 : (u_matKind == 6 ? 0.68 : 0.0)));
    float coatR = u_matKind == 2 ? 0.26 : (u_matKind == 3 ? 0.36 : (u_matKind == 6 ? 0.085 : 0.13));
    float keyCoat = u_matKind == 2 ? 0.50 : coatW;
    vec3 coatTint = u_matKind == 2 ? mix(vec3(1.0), clamp(albedo * 1.9 + vec3(0.24), 0.0, 1.0), 0.38) : vec3(1.0);
    float keyDot = max(dot(N, u_keyDir), 0.0);
    float spotFocus = mix(u_spotParams.x, u_spotParams.y, pow(keyDot, u_spotParams.z));
    vec3 F0 = mix(vec3(0.045), albedo, met), specAcc = vec3(0.0);
    vec3 Lo = evalLight(u_keyDir, u_keyColor * spotFocus, N, V, albedo, met, rough, F0, keyCoat, coatR, coatTint, specAcc)
            + evalLight(u_fillDir, u_fillColor, N, V, albedo, met, rough, F0, coatW * 0.22, coatR, coatTint, specAcc)
            + evalLight(u_rimDir, u_rimColor, N, V, albedo, met, rough, F0, coatW * 0.28, coatR, coatTint, specAcc);
    if (u_matKind == 5) {
      // Subtle real quartz / mica crystal micro-glints catching key light on weathered granite & river stone!
      vec3 Hk = normalize(V + u_keyDir);
      float glintMask = smoothstep(0.82, 0.96, noise3(v_worldPos * 105.0));
      float glint = pow(max(dot(N, Hk), 0.0), 112.0) * glintMask * 0.28;
      Lo += u_keyColor * glint;
    }

    vec3 bakedAo = vec3(vMax);
    vec3 aoSoft = mix(vec3(1.0), bakedAo, 0.78);
    vec3 aoEff = mix(vec3(0.92), aoSoft, isGrayAo);
    float aoScalar = dot(aoEff, vec3(0.2126, 0.7152, 0.0722)) * min(microCav, 1.0);
    float geoAo = mix(0.80, 1.0, smoothstep(0.0, 0.26, v_worldPos.y)) * mix(0.78, 1.0, smoothstep(-0.46, 0.50, N.y));
    vec3 directAo = mix(vec3(1.0), aoSoft, isGrayAo * (u_alpha < 0.99 ? 0.16 : 0.56));

    vec3 R = reflect(-V, N); float NoV = max(dot(N, V), 0.0);
    float specOccl = clamp(pow(NoV + aoScalar, exp2(-16.0 * rough - 1.0)) - 1.0 + aoScalar, 0.0, 1.0);
    vec3 ambDiff = mix(u_groundColor * 0.96, u_skyColor * 1.05, N.y * 0.5 + 0.5) * albedo * (1.0 - met) * 0.22 * u_lightMix.x * aoEff * geoAo;
    float envAtten = (1.0 - rough) * (1.0 - rough);
    vec3 F_env = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NoV, 5.0);
    vec3 envSpec = studioEnv(R, rough) * F_env * mix(0.10, 0.84, envAtten) * mix(envAtten, 1.0, met) * specOccl * geoAo * u_lightMix.y;
    vec3 envCoat = studioEnv(R, coatR) * coatTint * ((0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coatW) * (u_matKind == 2 ? 0.09 : (u_matKind == 6 ? 0.34 : 0.24)) * specOccl * u_lightMix.y;

    // Physically-motivated Subsurface Scattering (SSS) & soft micro-velvet sheen per organic material class!
    vec3 sssBounce = vec3(0.0);
    float rawKeyN = dot(N, u_keyDir);
    if (u_matKind == 2) {
      float wrapKey = clamp((rawKeyN + 0.24) / 1.24, 0.0, 1.0);
      float termBand = wrapKey * (1.0 - keyDot);
      sssBounce = (albedo * vec3(1.16, 1.08, 0.92) + vec3(0.008, 0.008, 0.006)) * termBand * 0.16 * aoEff * geoAo * min(u_lightMix.x, 1.15);
    } else if (u_matKind == 3) {
      float wrapLeaf = clamp((rawKeyN + 0.38) / 1.38, 0.0, 1.0);
      float termLeaf = wrapLeaf * (1.0 - keyDot * 0.85);
      vec3 backH = normalize(u_keyDir + N * 0.42);
      float backTrans = pow(clamp(dot(V, -backH), 0.0, 1.0), 2.8) * 0.45 + pow(clamp(dot(V, -u_rimDir), 0.0, 1.0), 3.0) * 0.28;
      vec3 leafTint = albedo * vec3(1.22, 1.18, 0.72) + vec3(0.012, 0.018, 0.004);
      sssBounce = leafTint * (termLeaf * 0.22 + backTrans * 0.34) * u_keyColor * 0.28 * aoEff * geoAo * min(u_lightMix.x, 1.2);
    } else if (u_matKind == 4) {
      // Soft food, bakery crust, spreads & skin: warm amber-rose subsurface wrap + gentle micro-velvet sheen!
      float wrapSoft = clamp((rawKeyN + 0.36) / 1.36, 0.0, 1.0);
      float termSoft = wrapSoft * (1.0 - keyDot * 0.86);
      float velvet = pow(1.0 - NoV, 3.4) * rough * 0.15;
      vec3 warmSss = albedo * vec3(1.28, 1.06, 0.80) + vec3(0.014, 0.007, 0.003);
      sssBounce = (warmSss * termSoft * 0.24 * u_keyColor * 0.28 + (albedo * 0.72 + vec3(0.16)) * velvet) * aoEff * geoAo * min(u_lightMix.x, 1.2);
    } else if (u_matKind == 6) {
      // Translucent wet food & liquids (fried egg yolks/whites, cola, honey nectar, sauces): luminous internal SSS glow!
      float wrapWet = clamp((rawKeyN + 0.44) / 1.44, 0.0, 1.0);
      float rimTrans = pow(1.0 - NoV, 2.2) * 0.38;
      vec3 deepGlow = albedo * vec3(1.36, 1.08, 0.62) + albedo * albedo * 0.35;
      sssBounce = deepGlow * (wrapWet * 0.28 + rimTrans * 0.32) * u_keyColor * 0.26 * aoEff * geoAo * min(u_lightMix.x, 1.2);
    }
    float fresnelRim = pow(1.0 - NoV, 3.0) * smoothstep(-0.28, 0.72, N.y);
    float rimMask = mix(0.45, 1.0, clamp(dot(N, u_rimDir) * 0.55 + 0.55, 0.0, 1.0));
    vec3 rimTint = mix(u_envRimTint, albedo * 1.45 + u_envRimTint * 0.30, (u_matKind == 2 || u_matKind == 3 || u_matKind == 6) ? 0.58 : 0.22);
    float rimStr = u_matKind == 6 ? 0.34 : (u_matKind == 1 ? 0.28 : (u_matKind == 3 ? 0.22 : (u_matKind == 5 ? 0.12 : 0.17)));
    vec3 rimGlow = rimTint * fresnelRim * rimMask * rimStr * u_rimBoost * aoEff * geoAo;

    vec3 outCol = linearToSrgb(acesFilm((Lo * directAo + ambDiff + envSpec + envCoat + sssBounce + rimGlow + u_emissive) * u_lightMix.z));
    if (u_lightMix.w > 0.001) {
      float softN = mix(0.78, 1.06, clamp(dot(N, u_keyDir) * 0.5 + 0.5, 0.0, 1.0));
      vec3 flatCol = linearToSrgb(acesFilm((albedo * mix(vec3(1.0), bakedAo, 0.34) * softN + u_emissive) * 1.15));
      outCol = mix(outCol, flatCol, u_lightMix.w);
    }
    float outAlpha = 1.0;
    if (u_alpha < 0.99) {
      float baseA = clamp(u_alpha * baseSample.a, 0.0, 1.0);
      float specLuma = dot(acesFilm((specAcc * directAo + envSpec + envCoat + rimGlow) * 1.02), vec3(0.2126, 0.7152, 0.0722));
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
    if (
      n.startsWith("item_egg_") || n === "item_cola_spill" || n === "item_milk_spill"
      || n === "item_nectar_gold" || n.startsWith("item_jello") || n === "item_pizza_sauce"
      || n === "item_water_puddle"
    ) return 6;
    if (n === "item_stone_granite" || n === "item_stone_quartz") return 5;
    if (
      n.startsWith("eye_") || n.startsWith("glass_") || n.startsWith("item_tomato")
      || n.startsWith("item_candy") || n === "item_mustard"
      || n === "item_pickle" || n.startsWith("item_skit_")
      || n === "item_bowl_yellow"
      || (mat.metallic < 0.35 && mat.roughness < 0.24 && !mat.normalTex)
    ) return 1;
    if (n.startsWith("chitin_")) return 2;
    if (
      n.startsWith("item_leaf") || n.startsWith("item_grass") || n.startsWith("item_petal")
      || n === "item_lettuce" || n === "item_stem_green" || n === "item_onion"
    ) return 3;
    if (
      n.startsWith("item_bun") || n.startsWith("item_chicken") || n.startsWith("item_corndog")
      || n.startsWith("item_beef") || n.startsWith("item_hotdog") || n.startsWith("item_mush")
      || n.startsWith("item_bear") || n.startsWith("item_flag") || n.startsWith("cloth_")
      || n.startsWith("fur_") || n === "item_banana_slice" || n === "item_groucho_nose"
      || n === "item_tongue_pink" || n === "item_cotton_fluff" || n === "item_cheese"
      || n === "item_doll_skin" || n === "item_frog_green" || n.startsWith("item_wax_")
      || n.startsWith("item_twinkie") || n.startsWith("item_cracker") || n === "item_cream_white"
      || n === "item_pb_swirl" || n === "item_steak_paste" || n === "item_fish_green"
      || n === "item_potted_fill" || n === "item_pizza_dough" || n === "item_chocolate"
      || n === "item_pretzel_crust" || n.startsWith("item_pez_")
    ) return 4;
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

  function selectActionAnims(model, clipName) {
    if (!model) return { anims: [], matched: false };
    const key = typeof clipName === "string" && clipName.trim() ? clipName.trim().toLowerCase() : "idle";
    if (key !== "idle") {
      const exact = model.animsByName && model.animsByName.get(key);
      if (exact && exact.length) return { anims: exact, matched: true };
      if (key === "attack" && model.attackAnims && model.attackAnims.length) return { anims: model.attackAnims, matched: true };
      if (key === "hit" && model.hitAnims && model.hitAnims.length) return { anims: model.hitAnims, matched: true };
      return { anims: model.idleAnims || [], matched: false };
    }
    const idle = model.idleAnims || [];
    return { anims: idle, matched: idle.length > 0 };
  }

  function evaluateWorldMats(inst, opts) {
    const m = inst.model, bi = m.blinkInfo;
    const isClosed = opts.blinkMode === "closed", isPulsing = !isClosed && inst.blink && inst.blink.active;
    const actionAnims = selectActionAnims(m, opts.clipName).anims;
    const hasAction = actionAnims.length > 0 && (opts.idleAnim || opts.fixedIdleTime !== null);
    if (!hasAction && (!bi || !bi.hasBlink || (!isClosed && !isPulsing))) return m.baseWorldMats;

    const overrides = new Map();
    if (hasAction) {
      for (const anim of actionAnims) {
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

  class AntRenderer {
    constructor(canvas, ctxOpts = {}) {
      this.canvas = canvas;
      const gl = canvas.getContext("webgl2", {
        antialias: true,
        alpha: true,
        premultipliedAlpha: Boolean(ctxOpts.premultipliedAlpha),
        preserveDrawingBuffer: true,
      });
      if (!gl) throw new Error("WebGL2 is not available in this browser");
      this.gl = gl;
      this.backdropProg = createProgram(gl, BACKDROP_VS, BACKDROP_FS);
      this.pbrProg = createProgram(gl, PBR_VS, PBR_FS);
      this.plinthProg = createProgram(gl, PLINTH_VS, PLINTH_FS);
      this.fireProg = createProgram(gl, FIRE_VS, FIRE_FS);
      this.sparkProg = createProgram(gl, SPARK_VS, SPARK_FS);
      this.furProg = createProgram(gl, FUR_VS, FUR_FS);
      this.fireParticles = buildFireParticles(gl);
      this.sparkParticles = buildSparkParticles(gl);
      this.emptyVao = gl.createVertexArray();
      this.fallbacks = createFallbackTextures(gl);
      this.plinth = null; this.plinthKey = "";
      this.uBack = getUniforms(gl, this.backdropProg, ["u_topColor", "u_midColor", "u_botColor"]);
      this.uPbr = getUniforms(gl, this.pbrProg, [
        "u_model", "u_normalMat", "u_viewProj", "u_jelloWobble", "u_cameraPos", "u_viewDir", "u_isOrtho",
        "u_baseColor", "u_alpha", "u_metallic", "u_roughness", "u_emissive", "u_normalScale",
        "u_matKind", "u_hasNormalTex", "u_baseTex", "u_normalTex", "u_mrTex",
        "u_keyDir", "u_fillDir", "u_rimDir", "u_skyColor", "u_groundColor", "u_rimBoost",
        "u_keyColor", "u_fillColor", "u_rimColor", "u_envKeyTint", "u_envFillTint", "u_envRimTint", "u_spotParams", "u_lightMix",
        "u_silhouette", "u_silhouetteColor",
      ]);
      this.uPlinth = getUniforms(gl, this.plinthProg, [
        "u_viewProj", "u_topBase", "u_sideBase", "u_ringColor", "u_keyDir",
        "u_shadows[0]", "u_shadowCount", "u_halfSpanX", "u_radius",
      ]);
      this.uFire = getUniforms(gl, this.fireProg, [
        "u_model", "u_viewProj", "u_camRight", "u_camUp", "u_emitterMin", "u_emitterMax",
        "u_time", "u_transparent", "u_silhouette", "u_silhouetteColor",
      ]);
      this.uSpark = getUniforms(gl, this.sparkProg, [
        "u_model", "u_viewProj", "u_camRight", "u_camUp", "u_emitterCenter",
        "u_emitterRadius", "u_time", "u_silhouette", "u_silhouetteColor",
      ]);
      this.uFur = getUniforms(gl, this.furProg, [
        "u_model", "u_normalMat", "u_viewProj", "u_cameraPos", "u_viewDir", "u_isOrtho",
        "u_baseColor", "u_keyDir", "u_fillDir", "u_rimDir", "u_skyColor", "u_groundColor",
        "u_keyColor", "u_fillColor", "u_rimColor", "u_envRimTint", "u_lightMix", "u_rimBoost",
        "u_shellFrac", "u_furLength", "u_furDroop", "u_furDensity", "u_isCotton",
        "u_silhouette", "u_silhouetteColor",
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

    computeCamera(scene, cam, w, h, overrideProj, opts = {}) {
      const aspect = Math.max(0.2, w / Math.max(1, h)), yaw = cam.yaw * DEG, pitch = cam.pitch * DEG;
      const t = opts.customTarget || scene.target;
      const isOrtho = overrideProj ? overrideProj === "ortho" : cam.projection === "ortho", fovy = 32 * DEG;
      const fitHalfH = Number.isFinite(opts.fixedHalfH)
        ? opts.fixedHalfH * cam.zoom
        : Math.max(scene.fitRadius, (scene.halfWidth / aspect) * 1.06) * cam.zoom;
      const dist = isOrtho ? Math.max(6.0, fitHalfH * 4.5) : (fitHalfH / Math.tan(fovy * 0.5)) * 1.16;
      const cp = Math.cos(pitch), sp = Math.sin(pitch), sy = Math.sin(yaw), cy = Math.cos(yaw);
      const eye = [t[0] + dist * cp * sy, t[1] + dist * sp, t[2] + dist * cp * cy];
      const proj = isOrtho
        ? mat4Ortho(-fitHalfH * aspect, fitHalfH * aspect, -fitHalfH, fitHalfH, 0.05, dist * 3.0)
        : mat4Perspective(fovy, aspect, 0.05, dist * 4.0);
      const dx = eye[0] - t[0], dy = eye[1] - t[1], dz = eye[2] - t[2], l = Math.hypot(dx, dy, dz) || 1;
      return { eye, viewProj: mat4Multiply(proj, mat4LookAt(eye, t, [0, 1, 0])), viewDir: [dx / l, dy / l, dz / l], isOrtho, yaw };
    }

    cameraLights(camera, preset) {
      const vd = camera.viewDir;
      const rLen = Math.hypot(vd[2], -vd[0]) || 1;
      const cr = [vd[2] / rLen, 0, -vd[0] / rLen];
      const cu = [
        vd[1] * cr[2] - vd[2] * cr[1],
        vd[2] * cr[0] - vd[0] * cr[2],
        vd[0] * cr[1] - vd[1] * cr[0],
      ];
      const basis = (v) => {
        const x = vd[0] * v[0] + cu[0] * v[1] + cr[0] * v[2];
        const y = vd[1] * v[0] + cu[1] * v[1] + cr[1] * v[2];
        const z = vd[2] * v[0] + cu[2] * v[1] + cr[2] * v[2];
        const l = Math.hypot(x, y, z) || 1;
        return [x / l, y / l, z / l];
      };
      const p = preset || LIGHTING_PRESETS.studio;
      return {
        keyDir: basis(p.keyBasis),
        fillDir: basis(p.fillBasis),
        rimDir: basis(p.rimBasis),
      };
    }

    drawScene(scene, cam, opts) {
      const gl = this.gl, lp = getLightingPreset(opts.lighting);
      const pal = lp.pal || (opts.dark ? STUDIO_PALETTE.dark : STUDIO_PALETTE.light);
      const team = opts.teamColors || resolveTeam(opts.team, opts.customColor);
      gl.viewport(0, 0, opts.width, opts.height); gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      if (!opts.transparent) {
        gl.disable(gl.DEPTH_TEST); gl.depthMask(false); gl.disable(gl.BLEND); gl.disable(gl.CULL_FACE);
        gl.useProgram(this.backdropProg);
        gl.uniform3fv(this.uBack.u_topColor, pal.bgTop); gl.uniform3fv(this.uBack.u_midColor, pal.bgMid); gl.uniform3fv(this.uBack.u_botColor, pal.bgBot);
        gl.bindVertexArray(this.emptyVao); gl.drawArrays(gl.TRIANGLES, 0, 3); gl.bindVertexArray(null);
      }
      gl.enable(gl.DEPTH_TEST); gl.depthMask(true);
      const camera = this.computeCamera(scene, cam, opts.width, opts.height, opts.overrideProj, opts);
      const { keyDir, fillDir, rimDir } = this.cameraLights(camera, lp);

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
      gl.uniform3fv(this.uPbr.u_keyColor, lp.keyColor); gl.uniform3fv(this.uPbr.u_fillColor, lp.fillColor); gl.uniform3fv(this.uPbr.u_rimColor, lp.rimColor);
      gl.uniform3fv(this.uPbr.u_envKeyTint, lp.envKeyTint); gl.uniform3fv(this.uPbr.u_envFillTint, lp.envFillTint); gl.uniform3fv(this.uPbr.u_envRimTint, lp.envRimTint);
      gl.uniform3fv(this.uPbr.u_spotParams, lp.spotParams); gl.uniform4fv(this.uPbr.u_lightMix, lp.lightMix);
      gl.uniform3fv(this.uPbr.u_skyColor, pal.sky); gl.uniform3fv(this.uPbr.u_groundColor, pal.ground);
      gl.uniform1f(this.uPbr.u_rimBoost, opts.transparent && !lp.pal ? (opts.dark ? 0.95 : 0.65) : pal.rimBoost);
      const isSil = Boolean(opts.silhouette);
      gl.uniform1i(this.uPbr.u_silhouette, isSil ? 1 : 0);
      gl.uniform3fv(this.uPbr.u_silhouetteColor, opts.dark ? [0.910, 0.933, 0.973] : [0.067, 0.086, 0.122]);

      const instMats = scene.instances.map((inst) => evaluateWorldMats(inst, opts));
      const fireEmitters = [];
      const sparkEmitters = [];
      const furCalls = [];
      for (let pass = 0; pass < 2; pass++) {
        const wantBlend = pass === 1;
        if (wantBlend) {
          gl.enable(gl.BLEND); gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false);
        } else { gl.disable(gl.BLEND); gl.depthMask(true); }
        scene.instances.forEach((inst, instIdx) => {
          const worldMats = instMats[instIdx];
          if (inst.model._isJello === undefined) {
            inst.model._isJello = inst.model.drawCalls.some((dc) => dc.prim.material.name === "item_jello_ruby");
          }
          const fxTime = Number.isFinite(opts.fixedIdleTime) ? opts.fixedIdleTime : (inst.idleTime || 0);
          const jw = inst.model._isJello && (opts.idleAnim || opts.fixedIdleTime !== null)
            ? evalJelloWobble(fxTime, inst.model.size ? inst.model.size[1] : 0.64)
            : [0, 0, 0, 0];
          gl.uniform4f(this.uPbr.u_jelloWobble, jw[0], jw[1], jw[2], jw[3]);
          for (const item of inst.model.drawCalls) {
            const prim = item.prim, mat = prim.material;
            if (mat.name === "item_fire_volume" || mat.name === "item_spark_volume") {
              if (!wantBlend) {
                const nodeMat = worldMats[item.nodeIndex] || item.worldMat;
                const modelMat = inst.offsetMat ? mat4Multiply(inst.offsetMat, nodeMat) : nodeMat;
                if (mat.name === "item_fire_volume") {
                  fireEmitters.push({ modelMat, localMin: prim.localMin, localMax: prim.localMax, time: fxTime });
                } else {
                  const ctr = [
                    0.5 * (prim.localMin[0] + prim.localMax[0]),
                    0.5 * (prim.localMin[1] + prim.localMax[1]),
                    0.5 * (prim.localMin[2] + prim.localMax[2]),
                  ];
                  const rad = 0.5 * Math.max(
                    prim.localMax[0] - prim.localMin[0],
                    prim.localMax[1] - prim.localMin[1],
                    prim.localMax[2] - prim.localMin[2],
                  );
                  sparkEmitters.push({ modelMat, center: ctr, radius: rad, time: fxTime });
                }
              }
              continue;
            }
            if (!wantBlend && FUR_PROFILES[mat.name]) {
              const nodeMat = worldMats[item.nodeIndex] || item.worldMat;
              const modelMat = inst.offsetMat ? mat4Multiply(inst.offsetMat, nodeMat) : nodeMat;
              furCalls.push({ prim, mat, modelMat, profile: FUR_PROFILES[mat.name] });
            }
            if (Boolean(mat.isTranslucent) !== wantBlend) continue;
            let bc = mat.baseColor;
            if (mat.name === "chitin_team") bc = [team.team[0], team.team[1], team.team[2], bc[3]];
            else if (mat.name === "chitin_dark") bc = [team.dark[0], team.dark[1], team.dark[2], bc[3]];
            else if (mat.name === "chitin_highlight") {
              bc = [team.team[0] * 0.66 + 0.28, team.team[1] * 0.66 + 0.31, team.team[2] * 0.66 + 0.24, bc[3]];
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
      if (furCalls.length) {
        gl.useProgram(this.furProg);
        gl.enable(gl.DEPTH_TEST);
        gl.depthMask(false);
        gl.enable(gl.BLEND);
        gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        gl.disable(gl.CULL_FACE);
        gl.uniformMatrix4fv(this.uFur.u_viewProj, false, camera.viewProj);
        gl.uniform3fv(this.uFur.u_cameraPos, camera.eye);
        gl.uniform3fv(this.uFur.u_viewDir, camera.viewDir);
        gl.uniform1i(this.uFur.u_isOrtho, camera.isOrtho ? 1 : 0);
        gl.uniform3fv(this.uFur.u_keyDir, keyDir);
        gl.uniform3fv(this.uFur.u_fillDir, fillDir);
        gl.uniform3fv(this.uFur.u_rimDir, rimDir);
        gl.uniform3fv(this.uFur.u_keyColor, lp.keyColor);
        gl.uniform3fv(this.uFur.u_fillColor, lp.fillColor);
        gl.uniform3fv(this.uFur.u_rimColor, lp.rimColor);
        gl.uniform3fv(this.uFur.u_envRimTint, lp.envRimTint);
        gl.uniform4fv(this.uFur.u_lightMix, lp.lightMix);
        gl.uniform3fv(this.uFur.u_skyColor, pal.sky);
        gl.uniform3fv(this.uFur.u_groundColor, pal.ground);
        gl.uniform1f(this.uFur.u_rimBoost, opts.transparent && !lp.pal ? (opts.dark ? 0.95 : 0.65) : pal.rimBoost);
        gl.uniform1i(this.uFur.u_silhouette, isSil ? 1 : 0);
        gl.uniform3fv(this.uFur.u_silhouetteColor, opts.dark ? [0.910, 0.933, 0.973] : [0.067, 0.086, 0.122]);

        for (const fc of furCalls) {
          const { prim, mat, modelMat, profile } = fc;
          gl.uniformMatrix4fv(this.uFur.u_model, false, modelMat);
          gl.uniformMatrix3fv(this.uFur.u_normalMat, false, mat3NormalFromMat4(modelMat));
          gl.uniform3f(this.uFur.u_baseColor, mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]);
          gl.uniform1f(this.uFur.u_furLength, profile.length);
          gl.uniform1f(this.uFur.u_furDroop, profile.droop);
          gl.uniform1f(this.uFur.u_furDensity, profile.density);
          gl.uniform1i(this.uFur.u_isCotton, profile.isCotton);
          gl.bindVertexArray(prim.vao);
          for (let sh = 1; sh <= profile.shells; sh++) {
            gl.uniform1f(this.uFur.u_shellFrac, sh / profile.shells);
            if (prim.indexed) gl.drawElements(prim.mode, prim.drawCount, prim.indexType, 0);
            else gl.drawArrays(prim.mode, 0, prim.drawCount);
          }
        }
      }
      if (fireEmitters.length) {
        const vd = camera.viewDir;
        const rLen = Math.hypot(vd[2], -vd[0]) || 1;
        const camRight = [vd[2] / rLen, 0, -vd[0] / rLen];
        const camUp = [
          vd[1] * camRight[2] - vd[2] * camRight[1],
          vd[2] * camRight[0] - vd[0] * camRight[2],
          vd[0] * camRight[1] - vd[1] * camRight[0],
        ];
        gl.useProgram(this.fireProg);
        gl.disable(gl.CULL_FACE);
        gl.enable(gl.DEPTH_TEST);
        gl.depthMask(false);
        gl.enable(gl.BLEND);
        gl.uniformMatrix4fv(this.uFire.u_viewProj, false, camera.viewProj);
        gl.uniform3fv(this.uFire.u_camRight, camRight);
        gl.uniform3fv(this.uFire.u_camUp, camUp);
        gl.uniform1i(this.uFire.u_transparent, opts.transparent ? 1 : 0);
        gl.uniform1i(this.uFire.u_silhouette, isSil ? 1 : 0);
        gl.uniform3fv(this.uFire.u_silhouetteColor, opts.dark ? [0.910, 0.933, 0.973] : [0.067, 0.086, 0.122]);
        gl.bindVertexArray(this.fireParticles.vao);

        for (const em of fireEmitters) {
          gl.uniformMatrix4fv(this.uFire.u_model, false, em.modelMat);
          gl.uniform3fv(this.uFire.u_emitterMin, em.localMin);
          gl.uniform3fv(this.uFire.u_emitterMax, em.localMax);
          gl.uniform1f(this.uFire.u_time, em.time);

          // 1. Subtle rising dark wispy smoke behind/above the blaze
          gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
          gl.drawElements(gl.TRIANGLES, this.fireParticles.smokeRange.count, gl.UNSIGNED_SHORT, this.fireParticles.smokeRange.offset);

          // 2. Main turbulent rising volumetric fire plumes (alpha-blended for sharp licking fiery detail, zero blowout!)
          gl.drawElements(gl.TRIANGLES, this.fireParticles.mainRange.count, gl.UNSIGNED_SHORT, this.fireParticles.mainRange.offset);

          // 3. Incandescent inner plasma core bloom (controlled additive)
          if (!isSil) {
            gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
          }
          gl.drawElements(gl.TRIANGLES, this.fireParticles.coreRange.count, gl.UNSIGNED_SHORT, this.fireParticles.coreRange.offset);

          // 4. Crisp glowing rising sparks & ember flecks
          gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
          gl.drawElements(gl.TRIANGLES, this.fireParticles.sparkRange.count, gl.UNSIGNED_SHORT, this.fireParticles.sparkRange.offset);
        }
      }
      if (sparkEmitters.length) {
        const vd = camera.viewDir;
        const rLen = Math.hypot(vd[2], -vd[0]) || 1;
        const camRight = [vd[2] / rLen, 0, -vd[0] / rLen];
        const camUp = [
          vd[1] * camRight[2] - vd[2] * camRight[1],
          vd[2] * camRight[0] - vd[0] * camRight[2],
          vd[0] * camRight[1] - vd[1] * camRight[0],
        ];
        gl.useProgram(this.sparkProg);
        gl.disable(gl.CULL_FACE); gl.enable(gl.DEPTH_TEST); gl.depthMask(false); gl.enable(gl.BLEND);
        gl.uniformMatrix4fv(this.uSpark.u_viewProj, false, camera.viewProj);
        gl.uniform3fv(this.uSpark.u_camRight, camRight);
        gl.uniform3fv(this.uSpark.u_camUp, camUp);
        gl.uniform1i(this.uSpark.u_silhouette, isSil ? 1 : 0);
        gl.uniform3fv(this.uSpark.u_silhouetteColor, opts.dark ? [0.910, 0.933, 0.973] : [0.067, 0.086, 0.122]);
        gl.bindVertexArray(this.sparkParticles.vao);

        for (const em of sparkEmitters) {
          gl.uniformMatrix4fv(this.uSpark.u_model, false, em.modelMat);
          gl.uniform3fv(this.uSpark.u_emitterCenter, em.center);
          gl.uniform1f(this.uSpark.u_emitterRadius, em.radius);
          gl.uniform1f(this.uSpark.u_time, em.time);

          gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
          gl.drawElements(gl.TRIANGLES, this.sparkParticles.smokeRange.count, gl.UNSIGNED_SHORT, this.sparkParticles.smokeRange.offset);
          gl.drawElements(gl.TRIANGLES, this.sparkParticles.streakRange.count, gl.UNSIGNED_SHORT, this.sparkParticles.streakRange.offset);
          gl.drawElements(gl.TRIANGLES, this.sparkParticles.burstRange.count, gl.UNSIGNED_SHORT, this.sparkParticles.burstRange.offset);
          if (!isSil) gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
          gl.drawElements(gl.TRIANGLES, this.sparkParticles.coreRange.count, gl.UNSIGNED_SHORT, this.sparkParticles.coreRange.offset);
        }
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
    if (!toolbarEl) return;
    const sel = "button:not(:disabled), input.team-color-input:not(:disabled), select.action-select:not(:disabled)";
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
      const rtl = getComputedStyle(toolbarEl).direction === "rtl";
      const step = e.key === "ArrowRight" ? (rtl ? -1 : 1) : (e.key === "ArrowLeft" ? (rtl ? 1 : -1) : 0);
      const next = step ? list[(idx + step + list.length) % list.length]
        : e.key === "Home" ? list[0] : e.key === "End" ? list[list.length - 1] : null;
      if (next) { e.preventDefault(); activate(next, true); }
    });
  }

  window.__antRender = {
    ANT_CLASSES, ANT_LABELS, DEFAULT_CUSTOM_HEX, TEAMS, ANGLES, SHEET_ANGLES, DEG, STUDIO_PALETTE,
    LIGHTING_ORDER, LIGHTING_PRESETS, validLighting, getLightingPreset, populateLightingSelect, paintPresetBackdrop2d,
    wrapAngleDeg, normalizeHex, deriveCustomTeam, resolveTeam, selectActionAnims, evaluateWorldMats, AntRenderer,
    installToolbarRovingTabindex,
  };
})();

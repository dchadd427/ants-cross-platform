(() => {
  "use strict";

  function uploadBuffer(gl, target, arr, loc, size) {
    const buf = gl.createBuffer();
    gl.bindBuffer(target, buf);
    gl.bufferData(target, arr, gl.STATIC_DRAW);
    if (loc !== undefined) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0); }
    return buf;
  }

  // --- Real-Time Volumetric GPU Fire, Dissipating Smoke & Rising Spark Particle Shaders ---
  const FIRE_VS = `#version 300 es
  layout(location = 0) in vec2 a_corner;
  layout(location = 1) in vec4 a_seed;
  layout(location = 2) in vec4 a_meta;
  uniform mat4 u_model, u_viewProj;
  uniform vec3 u_camRight, u_camUp, u_emitterMin, u_emitterMax;
  uniform float u_time;
  out vec2 v_uv;
  out vec4 v_info;
  out float v_worldY;
  void main() {
    float kind = a_meta.x, speed = a_meta.y, baseSz = a_meta.z, riseMax = a_meta.w;
    float age = fract(u_time * speed + a_seed.x);
    vec3 ext = max(u_emitterMax - u_emitterMin, vec3(0.08));
    vec2 ctr = 0.5 * (u_emitterMin.xz + u_emitterMax.xz), halfXZ = 0.5 * ext.xz;
    float hSpan = ext.y, rnd = a_seed.w, ang0 = rnd * 6.2831853;
    vec3 lp = vec3(ctr.x, u_emitterMin.y, ctr.y);
    float radX = baseSz, radY = baseSz, yLiftBias = 0.0;
    vec2 uv = a_corner;

    if (kind < 0.5) {
      float yFrac = 0.48 + pow(age, 0.86) * riseMax;
      float drift = (0.10 + 0.36 * age) * halfXZ.x;
      lp.x += a_seed.y * halfXZ.x * (0.55 + 0.45 * age) + sin(u_time * 1.2 + ang0 + age * 3.0) * drift;
      lp.z += a_seed.z * halfXZ.y * (0.55 + 0.45 * age) + cos(u_time * 1.0 + ang0 * 1.3 + age * 2.6) * drift;
      lp.y += yFrac * hSpan;
      float grow = mix(0.65, 2.05, age);
      radX = baseSz * grow; radY = baseSz * grow * 1.15;
    } else if (kind < 1.5) {
      float yFrac = pow(age, 1.12) * riseMax;
      float pinch = mix(1.02, 0.72, smoothstep(0.10, 0.92, age));
      float curlAmt = (0.05 + 0.22 * pow(age, 1.20)) * halfXZ.x;
      float swX = sin(age * 5.2 + ang0 + u_time * 2.1) * curlAmt + sin(age * 9.0 - u_time * 2.9 + rnd * 19.0) * curlAmt * 0.32;
      float swZ = cos(age * 4.8 + ang0 * 1.4 + u_time * 1.8) * curlAmt;
      lp.x += a_seed.y * halfXZ.x * pinch + swX;
      lp.z += a_seed.z * halfXZ.y * pinch + swZ;
      lp.y += yFrac * hSpan;
      float szEnv = smoothstep(0.0, 0.14, age) * mix(1.12, 0.48, smoothstep(0.22, 1.0, age));
      radX = baseSz * szEnv * mix(1.06, 0.74, age);
      radY = baseSz * szEnv * mix(1.15, 1.65, age);
      yLiftBias = 0.58;
    } else if (kind < 2.5) {
      float yFrac = pow(age, 1.06) * riseMax;
      float pinch = mix(0.84, 0.50, age);
      float curlAmt = (0.03 + 0.12 * age) * halfXZ.x;
      lp.x += a_seed.y * halfXZ.x * 0.68 * pinch + sin(age * 6.0 + ang0 + u_time * 2.6) * curlAmt;
      lp.z += a_seed.z * halfXZ.y * 0.68 * pinch + cos(age * 5.4 + ang0 + u_time * 2.2) * curlAmt;
      lp.y += yFrac * hSpan;
      float szEnv = smoothstep(0.0, 0.12, age) * mix(1.05, 0.36, smoothstep(0.20, 1.0, age));
      radX = baseSz * szEnv * 0.85; radY = baseSz * szEnv * 1.45; yLiftBias = 0.62;
    } else {
      float yFrac = 0.12 + pow(age, 0.90) * riseMax;
      float spiralR = (0.28 + 0.88 * pow(age, 0.85)) * halfXZ.x;
      float theta = ang0 + age * (4.2 + 3.4 * fract(rnd * 7.3)) * (rnd > 0.5 ? 1.0 : -1.0);
      lp.x += a_seed.y * halfXZ.x * 0.65 + cos(theta) * spiralR * 0.45 + sin(u_time * 2.5 + ang0) * 0.05;
      lp.z += a_seed.z * halfXZ.y * 0.65 + sin(theta) * spiralR * 0.45;
      lp.y += yFrac * hSpan;
      float szEnv = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.50, 1.0, age) * 0.75);
      radX = baseSz * szEnv; radY = baseSz * szEnv * 1.85;
    }

    vec3 wCenter = (u_model * vec4(lp, 1.0)).xyz;
    float mScale = length(u_model[0].xyz);
    float spin = (kind < 2.5) ? (rnd - 0.5) * 0.36 + sin(u_time * 1.6 + ang0) * 0.12 : sin(age * 6.0 + ang0) * 0.40;
    float cs = cos(spin), sn = sin(spin);
    vec2 rotC = vec2(a_corner.x * cs - a_corner.y * sn, a_corner.x * sn + a_corner.y * cs);
    vec3 wPos = wCenter + (u_camRight * (rotC.x * radX) + u_camUp * ((rotC.y + yLiftBias) * radY)) * mScale;
    gl_Position = u_viewProj * vec4(wPos, 1.0);
    v_uv = uv; v_info = vec4(age, kind, rnd, u_time); v_worldY = wPos.y;
  }`;

  const FIRE_FS = `#version 300 es
  precision highp float;
  in vec2 v_uv;
  in vec4 v_info;
  in float v_worldY;
  uniform int u_transparent, u_silhouette;
  uniform vec3 u_silhouetteColor;
  out vec4 fragColor;

  float hash21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
  float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(hash21(i), hash21(i + vec2(1,0)), u.x), mix(hash21(i + vec2(0,1)), hash21(i + vec2(1,1)), u.x), u.y);
  }
  float fbm(vec2 p) {
    float v = 0.0, amp = 0.52;
    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);
    for (int i = 0; i < 4; i++) { v += amp * vnoise(p); p = rot * p * 2.04 + vec2(1.7, 9.2); amp *= 0.50; }
    return v;
  }
  vec3 blackbodyFire(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.72, 0.05, 0.005), c1 = vec3(0.98, 0.22, 0.01), c2 = vec3(1.00, 0.54, 0.02);
    vec3 c3 = vec3(1.00, 0.88, 0.16), c4 = vec3(1.00, 0.99, 0.88);
    if (t < 0.28) return mix(c0, c1, t / 0.28);
    if (t < 0.58) return mix(c1, c2, (t - 0.28) / 0.30);
    if (t < 0.84) return mix(c2, c3, (t - 0.58) / 0.26);
    return mix(c3, c4, (t - 0.84) / 0.16);
  }

  void main() {
    float age = v_info.x, kind = v_info.y, rnd = v_info.z, t = v_info.w;
    float groundMask = smoothstep(0.012, 0.072, v_worldY);
    if (groundMask < 0.005) discard;
    if (kind < 0.5) {
      if (u_silhouette != 0) discard;
      float r = length(v_uv);
      if (r > 1.0) discard;
      float n = fbm(v_uv * 2.1 + vec2(rnd * 13.1, -age * 2.6 - t * 0.45));
      float mask = smoothstep(0.98, 0.16, r + (n - 0.5) * 0.52);
      float fade = smoothstep(0.0, 0.24, age) * pow(1.0 - smoothstep(0.32, 1.0, age), 1.55);
      float a = mask * fade * 0.17 * groundMask;
      if (a < 0.004) discard;
      fragColor = vec4(mix(vec3(0.24, 0.13, 0.08), vec3(0.10, 0.10, 0.12), smoothstep(-0.5, 0.7, v_uv.y)), a);
      return;
    }
    if (kind > 2.5) {
      vec2 sv = v_uv * vec2(1.25, 0.90);
      float r2 = dot(sv, sv);
      if (r2 > 1.0) discard;
      float core = exp(-r2 * 8.5), halo = exp(-r2 * 2.2) * 0.42;
      float flicker = 0.62 + 0.38 * sin(t * 24.0 + rnd * 53.0);
      float fade = smoothstep(0.0, 0.08, age) * pow(1.0 - smoothstep(0.45, 1.0, age), 1.45);
      float a = clamp((core + halo) * fade * flicker * groundMask, 0.0, 0.96);
      if (a < 0.01) discard;
      if (u_silhouette != 0) { fragColor = vec4(u_silhouetteColor, step(0.18, a)); return; }
      fragColor = vec4(blackbodyFire(clamp((1.0 - age * 0.75) * 0.72 + core * 0.36, 0.0, 1.0)), a);
      return;
    }
    vec2 q = v_uv;
    float pinchTop = mix(0.84, 1.60, smoothstep(-0.65, 0.92, q.y));
    float r = length(vec2(q.x * pinchTop, q.y * 0.94 + 0.08));
    if (r > 1.10) discard;
    vec2 nCoord = vec2(q.x * 2.5 + rnd * 19.7, q.y * 2.2 - age * 4.8 - t * 1.75);
    float n1 = fbm(nCoord);
    float n2 = fbm(nCoord * 1.55 + vec2((n1 - 0.5) * 1.45, -age * 2.4));
    float rawD = (1.0 - smoothstep(0.05, 1.02, r)) + (n2 - 0.47) * mix(0.46, 0.96, age);
    float erodeLo = mix(0.18, 0.62, pow(age, 1.10));
    float density = smoothstep(erodeLo, erodeLo + mix(0.38, 0.24, age), rawD);
    float lifeFade = smoothstep(0.0, 0.10, age) * pow(1.0 - smoothstep(0.38, 1.0, age), 1.40);
    if (u_silhouette != 0) { if (density * lifeFade * groundMask < 0.24) discard; fragColor = vec4(u_silhouetteColor, 1.0); return; }
    if (step(1.5, kind) < 0.5) {
      float a = clamp(density * lifeFade * groundMask * 0.62, 0.0, 0.88);
      if (a < 0.008) discard;
      float temp = clamp((1.0 - clamp(age * 0.92 + max(0.0, q.y) * 0.26, 0.0, 1.0)) * 0.72 + density * 0.34 - r * 0.26, 0.0, 0.86);
      fragColor = vec4(blackbodyFire(temp), a);
    } else {
      float a = clamp(density * lifeFade * groundMask * (u_transparent != 0 ? 0.22 : 0.26), 0.0, 0.52);
      if (a < 0.006) discard;
      fragColor = vec4(blackbodyFire(clamp((1.0 - age * 0.75) * 0.65 + density * 0.45 - r * 0.20, 0.35, 1.0)), a);
    }
  }`;

  function buildFireParticles(gl) {
    let s = 0x1998F12E;
    const rand = () => {
      s = (s + 0x6D2B79F5) >>> 0;
      let t = Math.imul(s ^ (s >>> 15), 1 | s);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
    const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
    const cArr = [], seedArr = [], metaArr = [], idxArr = [];
    let pCount = 0;
    function addGroup(n, kind, speedLo, speedHi, szLo, szHi, riseLo, riseHi, spreadPow) {
      const startIndex = idxArr.length;
      for (let i = 0; i < n; i++) {
        const phase = (i + rand() * 0.85) / n, rad = Math.pow(rand(), spreadPow), ang = rand() * Math.PI * 2;
        const bx = Math.cos(ang) * rad, bz = Math.sin(ang) * rad, rnd = rand();
        const speed = speedLo + rand() * (speedHi - speedLo), sz = szLo + rand() * (szHi - szLo), rise = riseLo + rand() * (riseHi - riseLo);
        const baseV = pCount * 4;
        for (let k = 0; k < 4; k++) { cArr.push(corners[k][0], corners[k][1]); seedArr.push(phase, bx, bz, rnd); metaArr.push(kind, speed, sz, rise); }
        idxArr.push(baseV, baseV + 1, baseV + 2, baseV, baseV + 2, baseV + 3);
        pCount++;
      }
      return { offset: startIndex * 2, count: idxArr.length - startIndex };
    }
    const smokeRange = addGroup(36, 0.0, 0.34, 0.54, 0.14, 0.22, 0.62, 0.86, 0.60);
    const mainRange = addGroup(124, 1.0, 0.68, 1.15, 0.14, 0.23, 0.66, 0.98, 0.56);
    const coreRange = addGroup(36, 2.0, 0.94, 1.48, 0.09, 0.16, 0.34, 0.58, 0.78);
    const sparkRange = addGroup(74, 3.0, 0.56, 1.08, 0.014, 0.025, 0.78, 1.22, 0.52);
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const bufs = [
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(cArr), 0, 2),
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(seedArr), 1, 4),
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(metaArr), 2, 4),
      uploadBuffer(gl, gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idxArr)),
    ];
    gl.bindVertexArray(null);
    return { vao, smokeRange, mainRange, coreRange, sparkRange, dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); } };
  }

  // --- Real-Time Volumetric GPU Bomb Fuse Sparkler Shaders (Blazing Core, Crackling Ballistic Sparks & Powder Smoke) ---
  const SPARK_VS = `#version 300 es
  layout(location = 0) in vec2 a_corner;
  layout(location = 1) in vec4 a_dir;   // xyz = unit ejection dir, w = phase
  layout(location = 2) in vec4 a_meta;  // x = kind (0=smoke, 1=core/halo, 2=ballistic streak, 3=crackle burst), y = speed, z = size, w = rnd
  uniform mat4 u_model, u_viewProj;
  uniform vec3 u_camRight, u_camUp, u_emitterCenter;
  uniform float u_emitterRadius, u_time;
  out vec2 v_uv;
  out vec4 v_info; // x = age, y = kind, z = rnd, w = time
  void main() {
    float kind = a_meta.x, speed = a_meta.y, baseSz = a_meta.z, rnd = a_meta.w;
    float age = fract(u_time * speed + a_dir.w);
    float mScale = length(u_model[0].xyz);
    vec3 wOrigin = (u_model * vec4(u_emitterCenter, 1.0)).xyz;
    float R = max(u_emitterRadius, 0.028) * mScale;
    vec3 wPos = wOrigin;

    if (kind < 0.5) {
      // Thin curly blue-grey gunpowder smoke drifting gently up off the burning fuse tip
      float drift = age * R * 4.4;
      vec3 off = vec3(
        a_dir.x * R * 0.7 * age + sin(u_time * 2.2 + rnd * 19.0 + age * 4.0) * R * 0.9 * age,
        R * 0.35 + drift,
        a_dir.z * R * 0.7 * age + cos(u_time * 1.9 + rnd * 15.0 + age * 3.5) * R * 0.9 * age
      );
      float rad = baseSz * R * mix(0.55, 2.2, age);
      wPos = wOrigin + off + (u_camRight * a_corner.x + u_camUp * a_corner.y) * rad;
    } else if (kind < 1.5) {
      // Compact sizzling incandescent fuse ember core + gentle golden corona
      float pulse = 0.88 + 0.18 * sin(u_time * 26.0 + rnd * 47.0) + 0.08 * cos(u_time * 41.0 + rnd * 13.0);
      vec3 jitter = a_dir.xyz * (R * 0.12 * sin(u_time * 31.0 + rnd * 29.0));
      float rad = baseSz * R * pulse;
      float spin = u_time * (rnd > 0.5 ? 4.5 : -4.0) + rnd * 6.2831853;
      float cs = cos(spin), sn = sin(spin);
      vec2 rc = vec2(a_corner.x * cs - a_corner.y * sn, a_corner.x * sn + a_corner.y * cs);
      wPos = wOrigin + jitter + (u_camRight * rc.x + u_camUp * rc.y) * rad;
    } else if (kind < 2.5) {
      // Delicate velocity-aligned crackling golden spark streaks arcing naturally with gravity
      float burstId = floor(u_time * speed + a_dir.w);
      float jAng = fract(sin(burstId * 91.7 + rnd * 311.7) * 43758.5453) * 6.2831853;
      vec3 ejDir = normalize(a_dir.xyz + vec3(cos(jAng) * 0.30, 0.24 + 0.18 * sin(jAng * 2.3), sin(jAng) * 0.30));
      float v0 = R * (2.2 + 2.3 * fract(rnd * 13.7));
      float grav = R * 4.6;
      float tDrag = (1.0 - exp(-age * 2.4)) / 2.4;
      vec3 disp = ejDir * (v0 * tDrag) + vec3(0.0, -0.5 * grav * age * age, 0.0);
      vec3 vel = ejDir * (v0 * exp(-age * 2.4)) + vec3(0.0, -grav * age, 0.0);
      vec2 v2 = vec2(dot(vel, u_camRight), dot(vel, u_camUp));
      float vLen = length(v2);
      vec2 dir2 = vLen > 1e-4 ? (v2 / vLen) : vec2(0.0, 1.0);
      vec2 perp2 = vec2(-dir2.y, dir2.x);
      float life = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.48, 1.0, age));
      float streakLen = baseSz * R * mix(0.85, 2.25, clamp(vLen / max(R * 3.5, 0.01), 0.0, 1.0)) * life;
      float streakW = baseSz * R * 0.24 * mix(1.0, 0.45, age);
      vec2 local2 = dir2 * (a_corner.y * streakLen) + perp2 * (a_corner.x * streakW);
      wPos = wOrigin + disp + u_camRight * local2.x + u_camUp * local2.y;
    } else {
      // Subtle occasional micro-crackle star pops off outer spark tips
      float burstId = floor(u_time * speed + a_dir.w);
      float jAng = fract(sin(burstId * 67.3 + rnd * 197.1) * 43758.5453) * 6.2831853;
      vec3 ejDir = normalize(a_dir.xyz + vec3(cos(jAng) * 0.35, 0.24, sin(jAng) * 0.35));
      float dist = R * (0.85 + 1.45 * fract(rnd * 7.1)) * pow(age, 0.72);
      vec3 disp = ejDir * dist + vec3(0.0, -R * 1.1 * age * age, 0.0);
      float pop = smoothstep(0.0, 0.14, age) * (1.0 - smoothstep(0.26, 0.78, age));
      float rad = baseSz * R * pop * (0.75 + 0.40 * sin(u_time * 36.0 + rnd * 61.0));
      float spin = rnd * 6.2831853 + age * 2.2;
      float cs = cos(spin), sn = sin(spin);
      vec2 rc = vec2(a_corner.x * cs - a_corner.y * sn, a_corner.x * sn + a_corner.y * cs);
      wPos = wOrigin + disp + (u_camRight * rc.x + u_camUp * rc.y) * rad;
    }

    gl_Position = u_viewProj * vec4(wPos, 1.0);
    v_uv = a_corner;
    v_info = vec4(age, kind, rnd, u_time);
  }`;

  const SPARK_FS = `#version 300 es
  precision highp float;
  in vec2 v_uv;
  in vec4 v_info;
  uniform int u_silhouette;
  uniform vec3 u_silhouetteColor;
  out vec4 fragColor;

  vec3 sparkColor(float heat) {
    heat = clamp(heat, 0.0, 1.0);
    vec3 c0 = vec3(0.95, 0.18, 0.01);
    vec3 c1 = vec3(1.00, 0.56, 0.04);
    vec3 c2 = vec3(1.00, 0.90, 0.28);
    vec3 c3 = vec3(1.00, 0.99, 0.92);
    if (heat < 0.33) return mix(c0, c1, heat / 0.33);
    if (heat < 0.70) return mix(c1, c2, (heat - 0.33) / 0.37);
    return mix(c2, c3, (heat - 0.70) / 0.30);
  }

  void main() {
    float age = v_info.x, kind = v_info.y, rnd = v_info.z, t = v_info.w;
    if (kind < 0.5) {
      if (u_silhouette != 0) discard;
      float r = length(v_uv);
      if (r > 1.0) discard;
      float puff = smoothstep(1.0, 0.14, r);
      float fade = smoothstep(0.0, 0.22, age) * pow(1.0 - smoothstep(0.30, 1.0, age), 1.5);
      float a = puff * fade * 0.14;
      if (a < 0.005) discard;
      fragColor = vec4(mix(vec3(0.42, 0.36, 0.30), vec3(0.20, 0.22, 0.26), age), a);
      return;
    }
    if (kind < 1.5) {
      // Compact glowing molten ember core with subtle delicate shimmer rays
      float r = length(v_uv);
      if (r > 1.0) discard;
      float ang = atan(v_uv.y, v_uv.x);
      float rays = pow(abs(cos(ang * 3.0 + rnd * 6.283)), 12.0) * 0.26 + pow(abs(cos(ang * 5.0 - t * 5.0)), 16.0) * 0.18;
      float core = exp(-r * r * 14.0);
      float corona = exp(-r * r * 4.2) * (0.44 + rays);
      float intensity = clamp(core * 1.05 + corona, 0.0, 1.0);
      if (intensity < 0.02) discard;
      if (u_silhouette != 0) { if (intensity < 0.35) discard; fragColor = vec4(u_silhouetteColor, 1.0); return; }
      float heat = clamp(core * 0.90 + corona * 0.45, 0.0, 1.0);
      fragColor = vec4(sparkColor(heat), clamp(intensity * 0.66, 0.0, 0.78));
      return;
    }
    if (kind < 2.5) {
      // Crisp tapered golden spark streak (subtle & refined, never blinding!)
      float headDist = length(vec2(v_uv.x * 1.20, (v_uv.y - 0.55) * 1.8));
      float tailProfile = smoothstep(-1.0, 0.65, v_uv.y) * smoothstep(1.0, 0.55, v_uv.y);
      float widthMask = exp(-v_uv.x * v_uv.x * mix(8.5, 3.8, clamp(v_uv.y * 0.5 + 0.5, 0.0, 1.0)));
      float body = widthMask * tailProfile;
      float head = exp(-headDist * headDist * 6.5);
      float flicker = 0.58 + 0.42 * sin(t * 32.0 + rnd * 89.0);
      float life = smoothstep(0.0, 0.07, age) * pow(1.0 - smoothstep(0.36, 1.0, age), 1.45);
      float a = clamp((head * 0.92 + body * 0.68) * life * flicker, 0.0, 0.86);
      if (a < 0.015) discard;
      if (u_silhouette != 0) { if (a < 0.25) discard; fragColor = vec4(u_silhouetteColor, 1.0); return; }
      float heat = clamp((1.0 - age * 0.78) * 0.58 + head * 0.42 + max(0.0, v_uv.y) * 0.14, 0.0, 1.0);
      fragColor = vec4(sparkColor(heat), a);
      return;
    }
    // Subtle starburst crackle flash
    float r = length(v_uv);
    if (r > 1.0) discard;
    float star = max(exp(-abs(v_uv.x) * 10.0) * exp(-abs(v_uv.y) * 2.6), exp(-abs(v_uv.y) * 10.0) * exp(-abs(v_uv.x) * 2.6));
    float dotC = exp(-r * r * 14.0);
    float fade = smoothstep(0.0, 0.12, age) * (1.0 - smoothstep(0.24, 0.80, age));
    float a = clamp((dotC + star * 0.68) * fade * 0.80, 0.0, 0.82);
    if (a < 0.02) discard;
    if (u_silhouette != 0) { if (a < 0.25) discard; fragColor = vec4(u_silhouetteColor, 1.0); return; }
    fragColor = vec4(sparkColor(clamp(0.55 + dotC * 0.45 - age * 0.35, 0.0, 1.0)), a);
  }`;

  function buildSparkParticles(gl) {
    let s = 0x1998B0AB;
    const rand = () => {
      s = (s + 0x6D2B79F5) >>> 0;
      let t = Math.imul(s ^ (s >>> 15), 1 | s);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
    const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
    const cArr = [], dirArr = [], metaArr = [], idxArr = [];
    let pCount = 0;
    function addGroup(n, kind, speedLo, speedHi, szLo, szHi, upBias) {
      const startIndex = idxArr.length;
      for (let i = 0; i < n; i++) {
        const phase = (i + rand() * 0.85) / n;
        const theta = rand() * Math.PI * 2;
        const z = (rand() * 2 - 1) * (1 - upBias * 0.45) + upBias;
        const xy = Math.sqrt(Math.max(0.01, 1 - Math.min(0.98, z * z)));
        const dx = Math.cos(theta) * xy, dy = z, dz = Math.sin(theta) * xy;
        const inv = 1 / (Math.hypot(dx, dy, dz) || 1);
        const speed = speedLo + rand() * (speedHi - speedLo);
        const sz = szLo + rand() * (szHi - szLo);
        const rnd = rand();
        const baseV = pCount * 4;
        for (let k = 0; k < 4; k++) {
          cArr.push(corners[k][0], corners[k][1]);
          dirArr.push(dx * inv, dy * inv, dz * inv, phase);
          metaArr.push(kind, speed, sz, rnd);
        }
        idxArr.push(baseV, baseV + 1, baseV + 2, baseV, baseV + 2, baseV + 3);
        pCount++;
      }
      return { offset: startIndex * 2, count: idxArr.length - startIndex };
    }
    const smokeRange = addGroup(14, 0.0, 0.52, 0.86, 0.58, 0.98, 0.65);
    const coreRange = addGroup(6, 1.0, 1.30, 2.00, 0.44, 0.76, 0.15);
    const streakRange = addGroup(34, 2.0, 1.25, 2.35, 0.24, 0.44, 0.35);
    const burstRange = addGroup(10, 3.0, 1.50, 2.60, 0.28, 0.50, 0.30);
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const bufs = [
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(cArr), 0, 2),
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(dirArr), 1, 4),
      uploadBuffer(gl, gl.ARRAY_BUFFER, new Float32Array(metaArr), 2, 4),
      uploadBuffer(gl, gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idxArr)),
    ];
    gl.bindVertexArray(null);
    return { vao, smokeRange, coreRange, streakRange, burstRange, dispose() { gl.deleteVertexArray(vao); bufs.forEach((b) => gl.deleteBuffer(b)); } };
  }

  // --- Real-Time Volumetric 3D Shell Fur & Pulled Cotton Batting Shaders ---
  const FUR_PROFILES = {
    item_bear_fur:     { length: 0.032, density: 108.0, droop: 0.22, isCotton: 0, shells: 16 },
    item_bear_muzzle:  { length: 0.021, density: 122.0, droop: 0.16, isCotton: 0, shells: 14 },
    item_bear_belly:   { length: 0.025, density: 114.0, droop: 0.16, isCotton: 0, shells: 14 },
    item_cotton_fluff: { length: 0.044, density: 66.0,  droop: 0.10, isCotton: 1, shells: 16 },
  };

  const FUR_VS = `#version 300 es
  layout(location = 0) in vec3 a_position;
  layout(location = 1) in vec3 a_normal;
  layout(location = 3) in vec3 a_color;
  uniform mat4 u_model, u_viewProj;
  uniform mat3 u_normalMat;
  uniform float u_shellFrac, u_furLength, u_furDroop;
  out vec3 v_worldPos, v_worldNormal, v_localPos, v_color;
  void main() {
    vec3 wN = normalize(u_normalMat * normalize(a_normal));
    vec4 wp = u_model * vec4(a_position, 1.0);
    float mScale = length(u_model[0].xyz), h = u_shellFrac;
    vec3 comb = vec3(sin(a_position.y * 19.0 + a_position.z * 14.0) * 0.16, -u_furDroop * h, cos(a_position.x * 19.0 - a_position.y * 14.0) * 0.16);
    wp.xyz += (wN + comb * h) * (h * u_furLength * mScale);
    v_worldPos = wp.xyz; v_worldNormal = wN; v_localPos = a_position; v_color = a_color;
    gl_Position = u_viewProj * wp;
  }`;

  const FUR_FS = `#version 300 es
  precision highp float;
  in vec3 v_worldPos, v_worldNormal, v_localPos, v_color;
  uniform vec3 u_cameraPos, u_viewDir, u_baseColor, u_keyDir, u_fillDir, u_rimDir, u_skyColor, u_groundColor, u_silhouetteColor;
  uniform vec3 u_keyColor, u_fillColor, u_rimColor, u_envRimTint;
  uniform vec4 u_lightMix;
  uniform float u_shellFrac, u_furDensity, u_rimBoost;
  uniform int u_isOrtho, u_isCotton, u_silhouette;
  out vec4 fragColor;

  vec3 hash33(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.xxy + p.yxx) * p.zyx); }
  float noise3(vec3 p) {
    vec3 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
    float n000 = hash33(i).x, n100 = hash33(i + vec3(1,0,0)).x, n010 = hash33(i + vec3(0,1,0)).x, n110 = hash33(i + vec3(1,1,0)).x;
    float n001 = hash33(i + vec3(0,0,1)).x, n101 = hash33(i + vec3(1,0,1)).x, n011 = hash33(i + vec3(0,1,1)).x, n111 = hash33(i + vec3(1,1,1)).x;
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
  }
  vec3 acesFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
  vec3 linearToSrgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }

  void main() {
    if (v_worldPos.y < -0.004) discard;
    float h = u_shellFrac;
    vec3 N = normalize(v_worldNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = u_isOrtho == 1 ? normalize(u_viewDir) : normalize(u_cameraPos - v_worldPos);
    float alpha = 1.0, strandVar = 0.0;
    if (u_isCotton != 0) {
      vec3 cp = v_localPos * (u_furDensity * 0.45) + vec3(0.0, h * 1.4, 0.0);
      float nA = noise3(cp), nB = noise3(cp * 2.35 + vec3(nA * 1.4, -nA * 1.1, nA * 0.8));
      float fibers = noise3(v_localPos * (u_furDensity * 1.65) + vec3(nB * 1.8));
      float puff = nA * 0.50 + nB * 0.32 + fibers * 0.18;
      float cut = mix(0.28, 0.72, pow(h, 0.90));
      alpha = smoothstep(cut - 0.08, cut + 0.14, puff) * (1.0 - pow(h, 1.8) * 0.65);
      if (alpha < 0.08) discard;
      strandVar = (fibers - 0.5) * 0.10;
    } else {
      vec3 clumpWarp = vec3(noise3(v_localPos * 18.0) - 0.5, noise3(v_localPos * 18.0 + 7.1) - 0.5, noise3(v_localPos * 18.0 + 13.7) - 0.5) * (0.65 * h);
      vec3 p = v_localPos * u_furDensity + clumpWarp, ip = floor(p), fp = fract(p);
      float bestD = 9.0; vec3 bestR = vec3(0.5);
      for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        vec3 off = vec3(float(dx), float(dy), float(dz)), rnd = hash33(ip + off), diff = off + (0.18 + 0.64 * rnd) - fp;
        float d = dot(diff, diff);
        if (d < bestD) { bestD = d; bestR = rnd; }
      }
      float strandLen = 0.34 + 0.66 * bestR.x;
      if (h > strandLen) discard;
      float tipFrac = h / strandLen, rLim = mix(0.58, 0.14, pow(tipFrac, 0.85)), dist = sqrt(bestD);
      if (dist > rLim) discard;
      alpha = smoothstep(rLim, rLim * 0.45, dist) * mix(1.0, 0.72, tipFrac);
      strandVar = (bestR.y - 0.5) * 0.20;
    }
    if (u_silhouette != 0) { if (alpha < 0.35) discard; fragColor = vec4(u_silhouetteColor, 1.0); return; }
    vec3 bakedCol = clamp(v_color, 0.0, 1.0);
    float vMax = max(max(bakedCol.r, bakedCol.g), bakedCol.b);
    vec3 vChroma = vMax > 1e-4 ? (bakedCol / vMax) : vec3(1.0);
    float rootAo = u_isCotton != 0 ? mix(0.58, 1.10, pow(h, 0.62)) : mix(0.48, 1.12, pow(h, 0.68));
    vec3 tipWarm = u_isCotton != 0
      ? mix(vec3(0.84, 0.87, 0.93), vec3(1.02, 1.01, 1.00), pow(h, 0.65))
      : mix(vec3(0.92, 0.84, 0.78), vec3(1.12, 1.04, 0.92), h);
    vec3 albedo = max(u_baseColor * vChroma * (1.0 + strandVar) * tipWarm, 0.0);
    float wrapKey = clamp((dot(N, u_keyDir) + 0.42) / 1.42, 0.0, 1.0);
    float wrapFill = clamp((dot(N, u_fillDir) + 0.48) / 1.48, 0.0, 1.0);
    float wrapRim = clamp((dot(N, u_rimDir) + 0.35) / 1.35, 0.0, 1.0);
    float velvetSheen = pow(1.0 - max(dot(N, V), 0.0), 2.3) * (0.28 + 0.48 * h);
    vec3 diff = albedo / 3.14159265 * (u_keyColor * wrapKey * 0.92 + u_fillColor * wrapFill * 0.55 + u_rimColor * wrapRim * 0.40);
    vec3 amb = mix(u_groundColor * 0.95, u_skyColor, N.y * 0.5 + 0.5) * albedo * 0.32 * u_lightMix.x;
    vec3 sheenCol = mix(albedo * 1.45 + vec3(0.06), u_envRimTint * 0.6 + albedo * 0.9, 0.35) * velvetSheen * (u_isCotton != 0 ? 0.36 : 0.24) * u_rimBoost;
    vec3 outCol = linearToSrgb(acesFilm((diff + amb + sheenCol) * (mix(1.0, vMax, 0.55) * rootAo) * u_lightMix.z));
    if (u_lightMix.w > 0.001) outCol = mix(outCol, linearToSrgb(acesFilm(albedo * rootAo * 1.10)), u_lightMix.w);
    fragColor = vec4(outCol, alpha);
  }`;

  // --- Exact 1.050s (7 x 150ms) Grounded Elastic Jell-O Wave & Crown Shear (`366:fdjelo1`) ---
  // Keyframes matched to 1998 sprites 529..535: [shearX, shearZ, bellyBulge] with base anchored at y=0.
  const JELLO_KEYS = [
    [ 0.000,  0.000,  0.000], // F0 (0.00s): neutral tall dome
    [-0.015,  0.010,  0.036], // F1 (0.15s): begins settling down
    [-0.025,  0.016,  0.082], // F2 (0.30s): drops deeper & swells at waist
    [-0.010,  0.012,  0.115], // F3 (0.45s): deepest squash & widest belly
    [ 0.028, -0.015, -0.078], // F4 (0.60s): snappy elastic upward stretch & waist pinch
    [ 0.058, -0.022, -0.038], // F5 (0.75s): peak tall overshoot + rightward crown wiggle
    [ 0.022,  0.015,  0.080], // F6 (0.90s): secondary settle bounce & flare
    [ 0.000,  0.000,  0.000], // F7 (1.05s): loop into F0
  ];
  function evalJelloWobble(timeSec, topH) {
    const dur = 1.05;
    const u = (((timeSec % dur) + dur) % dur) / dur;
    const f = u * 7.0;
    const i0 = Math.min(6, Math.floor(f));
    const t = f - i0;
    const s = t * t * (3.0 - 2.0 * t);
    const a = JELLO_KEYS[i0], b = JELLO_KEYS[i0 + 1];
    return [
      a[0] + (b[0] - a[0]) * s,
      a[1] + (b[1] - a[1]) * s,
      a[2] + (b[2] - a[2]) * s,
      Math.max(0.12, topH || 0.64),
    ];
  }

  window.__antFx = {
    FIRE_VS, FIRE_FS, buildFireParticles,
    SPARK_VS, SPARK_FS, buildSparkParticles,
    FUR_PROFILES, FUR_VS, FUR_FS,
    evalJelloWobble,
  };
})();

// Web Audio engine for Ants (1998) — Remastered 44.1 kHz & Classic 1998 live A/B playback.
// Exposes `window.__playAudio` (and module.exports for Node unit tests).
// API: init(manifestUrl), unlock(), setMode("remastered"|"classic"|"muted"), getMode(),
//      cycleMode(), play(soundId, wx, wy, camTargetX, camTargetZ, opts),
//      setMusic(on), isMusicEnabled(), setSfxVolume(v), setMusicVolume(v),
//      stopAll(), getManifest(), getStats(), onModeChange(fn).
(function (root) {
  "use strict";

  const VALID_MODES = ["remastered", "classic", "muted"];
  const MAX_VOICES = 12;
  const SAME_SOUND_COOLDOWN_MS = 32;
  const DEFAULT_MANIFEST_URL = "models/audio/manifest.json";
  const PREWARM_IDS = [0, 1, 2, 4, 14, 17, 43, 57, 66, 71, 87, 89];
  const TILE_PAN_SPAN = 15.0;
  const TILE_HEAR_RADIUS = 78.0;
  const PIXEL_PAN_SPAN = 480.0;
  const PIXEL_HEAR_RADIUS = 2500.0;
  const MAX_PAN = 0.78;

  const state = {
    ctx: null,
    compressor: null,
    masterGain: null,
    mode: "remastered",
    sfxVolume: 0.92,
    musicVolume: 0.22,
    musicEnabled: false,
    musicAudio: null,
    musicIndex: 0,
    musicTracks: [],
    manifest: null,
    manifestBaseUrl: "models/audio/",
    byId: new Map(),
    byName: new Map(),
    buffers: { remastered: new Map(), classic: new Map() },
    pending: { remastered: new Map(), classic: new Map() },
    activeVoices: [],
    lastPlayedAt: new Map(),
    listeners: new Set(),
    gestureHooked: false,
    initPromise: null,
    prewarmed: false,
    stats: { played: 0, stolen: 0, dropped: 0, decoded: 0, errors: 0 },
  };

  const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
  const nowMs = () => (typeof performance !== "undefined" && performance.now ? performance.now() : Date.now());

  function normalizeMode(raw) {
    if (raw == null) return state.mode;
    const s = String(raw).trim().toLowerCase();
    if (s === "remastered" || s === "new" || s === "hd" || s === "on") return "remastered";
    if (s === "classic" || s === "original" || s === "1998" || s === "retro") return "classic";
    if (s === "muted" || s === "mute" || s === "off" || s === "silent") return "muted";
    return state.mode;
  }

  function resolveBaseUrl(manifestUrl) {
    const clean = String(manifestUrl || DEFAULT_MANIFEST_URL).split("?")[0].split("#")[0];
    const slash = clean.lastIndexOf("/");
    return slash >= 0 ? clean.slice(0, slash + 1) : "";
  }

  function joinUrl(base, rel) {
    if (!rel) return "";
    return /^(https?:|data:|blob:|\/)/i.test(rel) ? rel : base + rel;
  }

  function ensureContext() {
    if (state.ctx) return state.ctx;
    const Ctor = typeof root !== "undefined" ? root.AudioContext || root.webkitAudioContext : null;
    if (!Ctor) return null;
    try {
      const ctx = new Ctor();
      const comp = typeof ctx.createDynamicsCompressor === "function" ? ctx.createDynamicsCompressor() : null;
      const master = ctx.createGain();
      master.gain.value = state.mode === "muted" ? 0.0 : state.sfxVolume;
      if (comp) {
        if (comp.threshold) comp.threshold.value = -14;
        if (comp.knee) comp.knee.value = 18;
        if (comp.ratio) comp.ratio.value = 4;
        if (comp.attack) comp.attack.value = 0.004;
        if (comp.release) comp.release.value = 0.14;
        comp.connect(master);
      }
      master.connect(ctx.destination);
      state.ctx = ctx;
      state.compressor = comp;
      state.masterGain = master;
      return ctx;
    } catch (_err) {
      state.stats.errors++;
      return null;
    }
  }

  function updateMasterGain() {
    if (!state.ctx || !state.masterGain) return;
    const target = state.mode === "muted" ? 0.0 : state.sfxVolume;
    const param = state.masterGain.gain;
    const t = state.ctx.currentTime || 0;
    if (param.cancelScheduledValues && param.setTargetAtTime) {
      param.cancelScheduledValues(t);
      param.setTargetAtTime(target, t, 0.008);
    } else {
      param.value = target;
    }
  }

  function hookUnlockGestures() {
    if (state.gestureHooked || typeof document === "undefined" || !document.addEventListener) return;
    state.gestureHooked = true;
    const onGesture = () => { unlock(); };
    ["pointerdown", "touchstart", "mousedown", "keydown"].forEach((ev) => {
      document.addEventListener(ev, onGesture, { once: true, passive: true, capture: true });
    });
  }

  function indexManifest(manifest) {
    state.manifest = manifest;
    state.byId.clear();
    state.byName.clear();
    const list = Array.isArray(manifest && manifest.sounds) ? manifest.sounds : [];
    for (let i = 0; i < list.length; i++) {
      const entry = list[i];
      if (!entry || typeof entry.id !== "number") continue;
      state.byId.set(entry.id, entry);
      if (entry.name) state.byName.set(String(entry.name).toLowerCase(), entry);
    }
    state.musicTracks = Array.isArray(manifest && manifest.music)
      ? manifest.music.map((m) => (typeof m === "string" ? m : m && m.url ? m.url : "")).filter(Boolean)
      : [];
  }

  function resolveEntry(soundId) {
    if (typeof soundId === "number") return state.byId.get(soundId) || null;
    if (typeof soundId === "string") {
      const s = soundId.trim();
      return /^\d+$/.test(s) ? state.byId.get(parseInt(s, 10)) || null : state.byName.get(s.toLowerCase()) || null;
    }
    return null;
  }

  function candidateUrlsFor(entry, mode) {
    if (!entry) return [];
    const base = state.manifestBaseUrl;
    const urls = [];
    if (mode === "classic") {
      if (entry.classic_url) urls.push(joinUrl(base, entry.classic_url));
      if (entry.classic_fallback_url) urls.push(entry.classic_fallback_url);
    } else {
      if (entry.remastered_url) urls.push(joinUrl(base, entry.remastered_url));
      if (entry.classic_url) urls.push(joinUrl(base, entry.classic_url));
    }
    return urls;
  }

  async function fetchAndDecode(entry, mode) {
    const bucket = mode === "classic" ? "classic" : "remastered";
    const cached = state.buffers[bucket].get(entry.id);
    if (cached) return cached;
    const inflight = state.pending[bucket].get(entry.id);
    if (inflight) return inflight;
    const ctx = ensureContext();
    if (!ctx || typeof fetch !== "function") return null;

    const task = (async () => {
      const urls = candidateUrlsFor(entry, bucket);
      for (let i = 0; i < urls.length; i++) {
        try {
          const resp = await fetch(urls[i], { cache: "no-cache" });
          if (!resp || !resp.ok) continue;
          const buf = await ctx.decodeAudioData(await resp.arrayBuffer());
          if (buf) {
            state.buffers[bucket].set(entry.id, buf);
            state.stats.decoded++;
            return buf;
          }
        } catch (_err) {}
      }
      state.stats.errors++;
      return null;
    })();

    state.pending[bucket].set(entry.id, task);
    try { return await task; } finally { state.pending[bucket].delete(entry.id); }
  }

  function prewarmCommon() {
    if (state.prewarmed || !state.manifest || state.mode === "muted") return;
    if (!state.ctx || state.ctx.state !== "running") return;
    state.prewarmed = true;
    const mode = state.mode === "classic" ? "classic" : "remastered";
    for (let i = 0; i < PREWARM_IDS.length; i++) {
      const entry = state.byId.get(PREWARM_IDS[i]);
      if (entry) fetchAndDecode(entry, mode).catch(() => {});
    }
  }

  function computeSpatial(entry, wx, wy, camTargetX, camTargetZ, opts) {
    const forceSpatial = opts && opts.spatial === true;
    const forceCenter = opts && opts.nonSpatial === true;
    const metaNonSpatial = Boolean(entry && entry.playback && entry.playback.non_spatial);
    if (
      forceCenter || (!forceSpatial && metaNonSpatial) ||
      typeof wx !== "number" || typeof wy !== "number" ||
      typeof camTargetX !== "number" || typeof camTargetZ !== "number" ||
      !Number.isFinite(wx) || !Number.isFinite(wy) || !Number.isFinite(camTargetX) || !Number.isFinite(camTargetZ) ||
      (wx === 0 && wy === 0 && !forceSpatial)
    ) {
      return { pan: 0, gain: 1.0, culled: false };
    }

    const dx = wx - camTargetX;
    const dz = wy - camTargetZ;
    const isPx = opts && opts.spatialScale === "pixels" ? true
      : opts && opts.spatialScale === "tiles" ? false
      : Math.max(Math.abs(wx), Math.abs(wy), Math.abs(camTargetX), Math.abs(camTargetZ)) > 120;
    const panSpan = isPx ? PIXEL_PAN_SPAN : TILE_PAN_SPAN;
    const hearRadius = isPx ? PIXEL_HEAR_RADIUS : TILE_HEAR_RADIUS;
    const pan = clamp(dx / panSpan, -MAX_PAN, MAX_PAN);
    const dist = Math.hypot(dx, dz);
    if (dist > hearRadius * 1.15) return { pan, gain: 0, culled: true };
    const inner = hearRadius * 0.14;
    if (dist <= inner) return { pan, gain: 1.0, culled: false };
    const u = clamp((dist - inner) / (hearRadius - inner), 0, 1);
    return { pan, gain: 0.12 + 0.88 * (0.5 * (1.0 + Math.cos(Math.PI * u))), culled: false };
  }

  function removeVoice(voice) {
    const idx = state.activeVoices.indexOf(voice);
    if (idx >= 0) state.activeVoices.splice(idx, 1);
  }

  function stopVoiceGracefully(voice) {
    removeVoice(voice);
    try {
      const t = state.ctx ? state.ctx.currentTime || 0 : 0;
      if (voice.gain && voice.gain.gain && voice.gain.gain.setTargetAtTime) {
        voice.gain.gain.setTargetAtTime(0.0, t, 0.004);
      }
      if (voice.src && voice.src.stop) voice.src.stop(t + 0.015);
    } catch (_err) {}
  }

  function acquireVoiceSlot(priority) {
    if (state.activeVoices.length < MAX_VOICES) return true;
    let victimIdx = -1;
    let lowestPrio = Infinity;
    let oldestTime = Infinity;
    for (let i = 0; i < state.activeVoices.length; i++) {
      const v = state.activeVoices[i];
      if (v.priority < lowestPrio || (v.priority === lowestPrio && v.startTime < oldestTime)) {
        lowestPrio = v.priority;
        oldestTime = v.startTime;
        victimIdx = i;
      }
    }
    if (victimIdx >= 0 && priority >= lowestPrio) {
      state.stats.stolen++;
      stopVoiceGracefully(state.activeVoices[victimIdx]);
      return true;
    }
    state.stats.dropped++;
    return false;
  }

  function startBufferPlayback(entry, buffer, spatial, opts) {
    const ctx = ensureContext();
    const bus = state.compressor || state.masterGain;
    if (!ctx || !bus || !buffer) return false;
    const priority = entry && entry.playback && typeof entry.playback.priority === "number" ? entry.playback.priority : 2;
    if (!acquireVoiceSlot(priority)) return false;

    const src = ctx.createBufferSource();
    src.buffer = buffer;
    const defaultJitter = entry && entry.playback && typeof entry.playback.pitch_jitter === "number" ? entry.playback.pitch_jitter : 0.0;
    const jitter = opts && typeof opts.pitchJitter === "number" ? opts.pitchJitter : defaultJitter;
    if (jitter > 0 && src.playbackRate) {
      src.playbackRate.value = 1.0 + (Math.random() * 2.0 - 1.0) * jitter;
    }

    const gainNode = ctx.createGain();
    const volMul = opts && typeof opts.volume === "number" ? clamp(opts.volume, 0, 1.5) : 1.0;
    gainNode.gain.value = clamp(spatial.gain * volMul, 0.0, 1.5);

    if (typeof ctx.createStereoPanner === "function" && Math.abs(spatial.pan) > 0.005) {
      const panner = ctx.createStereoPanner();
      panner.pan.value = spatial.pan;
      src.connect(gainNode);
      gainNode.connect(panner);
      panner.connect(bus);
    } else {
      src.connect(gainNode);
      gainNode.connect(bus);
    }

    const voice = { src, gain: gainNode, soundId: entry.id, priority, startTime: nowMs() };
    state.activeVoices.push(voice);
    src.onended = () => removeVoice(voice);
    try {
      src.start(0);
      state.stats.played++;
      return true;
    } catch (_err) {
      removeVoice(voice);
      state.stats.errors++;
      return false;
    }
  }

  function stopMusicElement() {
    if (!state.musicAudio) return;
    try { state.musicAudio.pause(); state.musicAudio.currentTime = 0; } catch (_err) {}
    state.musicAudio = null;
  }

  function startNextMusicTrack() {
    if (!state.musicEnabled || state.mode === "muted" || !state.musicTracks.length || typeof Audio === "undefined") return;
    stopMusicElement();
    const url = joinUrl(state.manifestBaseUrl, state.musicTracks[state.musicIndex % state.musicTracks.length]);
    try {
      const el = new Audio(url);
      el.volume = clamp(state.musicVolume, 0, 1);
      el.preload = "auto";
      el.addEventListener("ended", () => {
        state.musicIndex = (state.musicIndex + 1) % state.musicTracks.length;
        if (state.musicEnabled && state.mode !== "muted") startNextMusicTrack();
      });
      el.addEventListener("error", () => stopMusicElement());
      state.musicAudio = el;
      const p = el.play();
      if (p && p.catch) p.catch(() => {});
    } catch (_err) {}
  }

  function syncMusicPlayback() {
    if (!state.musicEnabled || state.mode === "muted") {
      if (state.musicAudio) { try { state.musicAudio.pause(); } catch (_err) {} }
      return;
    }
    if (state.musicAudio) {
      state.musicAudio.volume = clamp(state.musicVolume, 0, 1);
      const p = state.musicAudio.play();
      if (p && p.catch) p.catch(() => {});
    } else {
      startNextMusicTrack();
    }
  }

  async function init(manifestUrl) {
    hookUnlockGestures();
    const url = manifestUrl || DEFAULT_MANIFEST_URL;
    state.manifestBaseUrl = resolveBaseUrl(url);
    if (state.initPromise && state.manifest) return state.manifest;
    if (typeof fetch !== "function") return null;
    state.initPromise = (async () => {
      try {
        const resp = await fetch(url, { cache: "no-cache" });
        if (!resp || !resp.ok) return null;
        const data = await resp.json();
        indexManifest(data);
        if (state.musicEnabled) syncMusicPlayback();
        prewarmCommon();
        return data;
      } catch (_err) {
        state.stats.errors++;
        return null;
      }
    })();
    return state.initPromise;
  }

  async function unlock() {
    const ctx = ensureContext();
    if (!ctx) return false;
    try {
      if (ctx.state === "suspended" && ctx.resume) await ctx.resume();
      updateMasterGain();
      prewarmCommon();
      if (state.musicEnabled && state.mode !== "muted") syncMusicPlayback();
      return ctx.state === "running";
    } catch (_err) {
      return false;
    }
  }

  function setMode(nextMode) {
    const normalized = normalizeMode(nextMode);
    const changed = normalized !== state.mode;
    state.mode = normalized;
    updateMasterGain();
    if (normalized === "muted") stopAll();
    syncMusicPlayback();
    if (changed) state.listeners.forEach((fn) => { try { fn(normalized); } catch (_err) {} });
    return state.mode;
  }

  function getMode() { return state.mode; }

  function cycleMode() {
    return setMode(VALID_MODES[(VALID_MODES.indexOf(state.mode) + 1) % VALID_MODES.length]);
  }

  function play(soundId, wx, wy, camTargetX, camTargetZ, opts) {
    const effectiveMode = opts && opts.mode ? normalizeMode(opts.mode) : state.mode;
    if (effectiveMode === "muted") return false;
    const entry = resolveEntry(soundId);
    if (!entry) return false;

    const tNow = nowMs();
    const key = effectiveMode + ":" + entry.id;
    const prevT = state.lastPlayedAt.get(key);
    if (!(opts && opts.force) && prevT !== undefined && tNow - prevT < SAME_SOUND_COOLDOWN_MS) return false;
    state.lastPlayedAt.set(key, tNow);

    const spatial = computeSpatial(entry, wx, wy, camTargetX, camTargetZ, opts);
    if (spatial.culled) return false;

    const ctx = ensureContext();
    if (!ctx) return false;
    if (ctx.state === "suspended" && ctx.resume) ctx.resume().catch(() => {});

    const bucket = effectiveMode === "classic" ? "classic" : "remastered";
    const cached = state.buffers[bucket].get(entry.id);
    if (cached) return startBufferPlayback(entry, cached, spatial, opts);

    fetchAndDecode(entry, bucket).then((buf) => {
      if (!buf || (!(opts && opts.mode) && state.mode === "muted")) return;
      startBufferPlayback(entry, buf, spatial, opts);
    });
    return true;
  }

  function setMusic(on) { state.musicEnabled = Boolean(on); syncMusicPlayback(); return state.musicEnabled; }
  function isMusicEnabled() { return state.musicEnabled; }
  function setMusicVolume(v) { state.musicVolume = clamp(Number(v) || 0, 0, 1); if (state.musicAudio) state.musicAudio.volume = state.musicVolume; return state.musicVolume; }
  function setSfxVolume(v) { state.sfxVolume = clamp(Number(v) || 0, 0, 1); updateMasterGain(); return state.sfxVolume; }
  function stopAll() { state.activeVoices.slice().forEach(stopVoiceGracefully); state.activeVoices.length = 0; }
  function getManifest() { return state.manifest; }
  function getStats() {
    return {
      mode: state.mode, activeVoices: state.activeVoices.length, maxVoices: MAX_VOICES,
      cachedRemastered: state.buffers.remastered.size, cachedClassic: state.buffers.classic.size,
      musicEnabled: state.musicEnabled, musicTracks: state.musicTracks.length, ...state.stats,
    };
  }
  function onModeChange(fn) {
    if (typeof fn !== "function") return () => {};
    state.listeners.add(fn);
    return () => state.listeners.delete(fn);
  }
  function _loadManifestObject(manifest, baseUrl) {
    state.manifestBaseUrl = baseUrl || "models/audio/";
    indexManifest(manifest);
  }

  const api = {
    init, unlock, setMode, getMode, cycleMode, play,
    setMusic, isMusicEnabled, setMusicVolume, setSfxVolume,
    stopAll, getManifest, getStats, onModeChange,
    _loadManifestObject, MAX_VOICES, VALID_MODES,
  };
  if (typeof root !== "undefined") root.__playAudio = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);

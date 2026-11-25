/* Side-by-side 1998 original sprite vs live 3D map items viewer. */
"use strict";

(() => {
  const { clamp, mat4Translate, parseGlb } = window.__antGltf;
  const {
    DEG, LIGHTING_ORDER, validLighting, getLightingPreset, populateLightingSelect,
    paintPresetBackdrop2d, wrapAngleDeg, AntRenderer, installToolbarRovingTabindex,
  } = window.__antRender;

  const RELOAD_KEY = "ants_items_reload_state_v1";
  const CAT_LABELS = {
    all: "All",
    food: "Food",
    plants: "Plants & Grass",
    toys: "Toys & Props",
    rocks: "Rocks",
    structures: "Hills & Gear",
  };
  const CAM_PRESETS = {
    orig: { label: "1998 Angle" },
    hero: { label: "Hero 3D", yaw: -32, pitch: 24 },
    front: { label: "Front", yaw: 0, pitch: 18 },
    top: { label: "Top-Down", yaw: 0, pitch: 72 },
  };

  function isLocalDevHost() {
    const h = (window.location.hostname || "").toLowerCase();
    if (!h || h === "localhost" || h === "127.0.0.1" || h === "::1" || h === "[::1]" || h.endsWith(".local")) {
      return true;
    }
    return /^(?:10\.|192\.168\.|172\.(?:1[6-9]|2\d|3[01])\.|100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.)/.test(h);
  }

  function main() {
    const stageMain = document.getElementById("items-stage");
    const gridEl = document.getElementById("items-grid");
    const soloEl = document.getElementById("items-solo");
    const emptyOverlay = document.getElementById("items-empty");
    const emptyTitle = document.getElementById("items-empty-title");
    const emptyMessage = document.getElementById("items-empty-message");

    const catGroup = document.getElementById("cat-group");
    const badgeProgress = document.getElementById("badge-progress");
    const badgeVersion = document.getElementById("badge-version");
    const badgeCount = document.getElementById("badge-count");
    const badgeDetailsSummary = document.getElementById("badge-details-summary");
    const metaSprite = document.getElementById("meta-sprite");
    const metaMesh = document.getElementById("meta-mesh");
    const metaSize = document.getElementById("meta-size");
    const metaCatalog = document.getElementById("meta-catalog");
    const statusRegion = document.getElementById("status-region");
    const updateToast = document.getElementById("update-toast");

    const btnTogglePanel = document.getElementById("btn-toggle-panel");
    const btnClosePanel = document.getElementById("btn-close-panel");
    const inspectorPanel = document.getElementById("inspector-panel");
    const inspectorBackdrop = document.getElementById("inspector-backdrop");

    const soloTitle = document.getElementById("solo-title");
    const soloMeta = document.getElementById("solo-meta");
    const soloFamilyPills = document.getElementById("solo-family-pills");
    const btnBackGrid = document.getElementById("btn-back-grid");
    const btnSoloPrev = document.getElementById("btn-solo-prev");
    const btnSoloNext = document.getElementById("btn-solo-next");
    const btnSoloReset = document.getElementById("btn-solo-reset");
    const soloOrigCanvas = document.getElementById("solo-orig-canvas");
    const solo3dCanvas = document.getElementById("solo-3d-canvas");
    const soloOrigPill = document.getElementById("solo-orig-pill");
    const solo3dPill = document.getElementById("solo-3d-pill");

    const btnPlay = document.getElementById("btn-play");
    const btnSpin = document.getElementById("btn-spin");
    const btnResetAll = document.getElementById("btn-reset-all");
    const btnLightPrev = document.getElementById("btn-light-prev");
    const btnLightNext = document.getElementById("btn-light-next");
    const selectLighting = document.getElementById("select-lighting");
    const inputSearch = document.getElementById("input-search");
    const btnPixelMode = document.getElementById("btn-pixel-mode");
    const btnSilhouette = document.getElementById("btn-silhouette");
    const btnPlinth = document.getElementById("btn-plinth");

    const soloOrigCtx = soloOrigCanvas.getContext("2d");
    const solo3dCtx = solo3dCanvas.getContext("2d");

    const offCanvas = document.createElement("canvas");
    offCanvas.width = 512;
    offCanvas.height = 512;
    let renderer = null;
    try {
      renderer = new AntRenderer(offCanvas, { alpha: true, preserveDrawingBuffer: true });
    } catch (err) {
      emptyTitle.textContent = "WebGL2 unavailable";
      emptyMessage.textContent = String(err && err.message ? err.message : err);
      emptyOverlay.hidden = false;
      return;
    }

    const silCanvas = document.createElement("canvas");
    const silCtx = silCanvas.getContext("2d");

    const darkMq = window.matchMedia("(prefers-color-scheme: dark)");
    const reduceMotionMq = window.matchMedia("(prefers-reduced-motion: reduce)");
    const mobileMq = window.matchMedia("(max-width: 900px)");

    populateLightingSelect(selectLighting);

    // Restore session state if coming back from auto-reload
    let saved = null;
    try {
      const raw = window.sessionStorage.getItem(RELOAD_KEY);
      if (raw) {
        window.sessionStorage.removeItem(RELOAD_KEY);
        const parsed = JSON.parse(raw);
        if (parsed && parsed.page === window.location.pathname) saved = parsed;
      }
    } catch (_) {}

    const params = new URLSearchParams(window.location.search);
    const paramOr = (k, fb) => (params.has(k) ? params.get(k) : (saved && saved[k] != null ? saved[k] : fb));

    const state = {
      manifest: null,
      items: [],
      byId: new Map(),
      families: new Map(),
      origItems: new Map(),
      origImages: new Map(),
      scenes: new Map(),
      loadingIds: new Set(),
      failedIds: new Set(),
      cards: new Map(),
      visibleCardIds: new Set(),

      category: String(paramOr("cat", "all")).toLowerCase(),
      search: String(paramOr("q", "")),
      activeId: paramOr("item", null) || null,
      camPreset: String(paramOr("cam", "orig")).toLowerCase(),
      projection: String(paramOr("proj", "perspective")).toLowerCase() === "ortho" ? "ortho" : "perspective",
      lighting: validLighting(paramOr("light", "studio")) || "studio",
      playing: !reduceMotionMq.matches,
      spin: false,
      pixelMode: paramOr("pixel", "0") === "1",
      silhouette: paramOr("sil", "0") === "1",
      showPlinth: paramOr("plinth", "0") === "1",
      dark: darkMq.matches,

      gridYawOffset: 0,
      soloYaw: null,
      soloPitch: null,
      soloZoom: 1.0,
      soloPanX: 0.0,
      soloPanY: 0.0,
      spinAngle: 0,
      simTime: 0,
      dirty: true,
      ready: false,
      error: null,
    };

    if (!CAM_PRESETS[state.camPreset]) state.camPreset = "orig";
    if (params.has("yaw") && Number.isFinite(Number(params.get("yaw")))) {
      state.soloYaw = wrapAngleDeg(Number(params.get("yaw")));
    }
    if (params.has("pitch") && Number.isFinite(Number(params.get("pitch")))) {
      state.soloPitch = clamp(Number(params.get("pitch")), -10, 85);
    }
    if (params.has("zoom") && Number.isFinite(Number(params.get("zoom")))) {
      state.soloZoom = clamp(Number(params.get("zoom")), 0.45, 2.6);
    }
    if (params.get("spin") === "1") state.spin = true;
    if (params.get("play") === "0") state.playing = false;

    function announce(msg) {
      if (statusRegion) statusRegion.textContent = msg;
    }

    function markDirty() {
      state.dirty = true;
    }

    function getFilteredItems() {
      const q = state.search.trim().toLowerCase();
      return state.items.filter((item) => {
        if (state.category !== "all" && item.category !== state.category) return false;
        if (!q) return true;
        return (
          item.id.toLowerCase().includes(q)
          || item.label.toLowerCase().includes(q)
          || (item.family || "").toLowerCase().includes(q)
          || String(item.chd_id).includes(q)
        );
      });
    }

    function getBaseAngles(item) {
      if (state.camPreset === "orig") {
        return {
          yaw: Number.isFinite(item.default_yaw) ? item.default_yaw : 0,
          pitch: Number.isFinite(item.default_pitch) ? item.default_pitch : 36,
        };
      }
      const preset = CAM_PRESETS[state.camPreset] || CAM_PRESETS.hero;
      return { yaw: preset.yaw, pitch: preset.pitch };
    }

    function getCardCamera(item) {
      const base = getBaseAngles(item);
      return {
        yaw: wrapAngleDeg(base.yaw + state.gridYawOffset + (state.spin ? state.spinAngle : 0)),
        pitch: base.pitch,
        zoom: 1.0,
        panX: 0.0,
        panY: 0.0,
        projection: state.projection,
      };
    }

    function getSoloCamera(item) {
      const base = getBaseAngles(item);
      const yawBase = state.soloYaw !== null ? state.soloYaw : base.yaw;
      const pitchBase = state.soloPitch !== null ? state.soloPitch : base.pitch;
      return {
        yaw: wrapAngleDeg(yawBase + (state.spin ? state.spinAngle : 0)),
        pitch: pitchBase,
        zoom: state.soloZoom,
        panX: state.soloPanX,
        panY: state.soloPanY,
        projection: state.projection,
      };
    }

    function computeFitHalfH(model, yawDeg, pitchDeg, aspect, isOrtho) {
      const w = Math.max(0.08, model.size[0]);
      const h = Math.max(0.08, model.size[1]);
      const d = Math.max(0.08, model.size[2]);
      const y = Math.abs(yawDeg * DEG);
      const p = Math.abs(pitchDeg * DEG);
      const cy = Math.abs(Math.cos(y));
      const sy = Math.abs(Math.sin(y));
      const cp = Math.cos(p);
      const sp = Math.sin(p);
      const horizSpan = w * cy + d * sy;
      const depthSpan = w * sy + d * cy;
      const vertSpan = h * cp + depthSpan * sp;
      const pad = isOrtho ? 0.62 : 0.66;
      const halfV = Math.max(0.16, vertSpan * pad);
      const halfH = Math.max(0.16, horizSpan * pad);
      return Math.max(halfV, halfH / Math.max(0.32, aspect));
    }

    function ensureOffscreenSize(w, h) {
      if (offCanvas.width < w || offCanvas.height < h || offCanvas.width > w * 2.2 || offCanvas.height > h * 2.2) {
        offCanvas.width = Math.max(w, offCanvas.width > w * 2.2 ? w : offCanvas.width);
        offCanvas.height = Math.max(h, offCanvas.height > h * 2.2 ? h : offCanvas.height);
      }
    }

    function syncCanvasBacking(cv, dpr) {
      const w = Math.max(1, Math.round(cv.clientWidth * dpr));
      const h = Math.max(1, Math.round(cv.clientHeight * dpr));
      if (cv.width !== w || cv.height !== h) {
        cv.width = w;
        cv.height = h;
      }
      return [w, h];
    }

    function drawStudioBackdrop(ctx, w, h, groundY, splitDivider) {
      const dark = state.dark;
      const g = ctx.createLinearGradient(0, 0, 0, h);
      if (dark) {
        g.addColorStop(0, "#121722");
        g.addColorStop(0.62, "#1e2634");
        g.addColorStop(1, "#0d1119");
      } else {
        g.addColorStop(0, "#d9e1eb");
        g.addColorStop(0.62, "#f1f4f8");
        g.addColorStop(1, "#c7d1de");
      }
      ctx.fillStyle = g;
      ctx.fillRect(0, 0, w, h);

      ctx.fillStyle = dark ? "rgba(8, 12, 18, 0.42)" : "rgba(75, 92, 114, 0.14)";
      ctx.fillRect(0, groundY, w, Math.max(0, h - groundY));

      ctx.strokeStyle = dark ? "rgba(180, 200, 228, 0.14)" : "rgba(40, 56, 78, 0.15)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, groundY + 0.5);
      ctx.lineTo(w, groundY + 0.5);
      ctx.stroke();

      if (splitDivider) {
        const mid = Math.floor(w * 0.5) + 0.5;
        ctx.strokeStyle = dark ? "rgba(255, 255, 255, 0.10)" : "rgba(18, 28, 42, 0.12)";
        ctx.beginPath();
        ctx.moveTo(mid, 0);
        ctx.lineTo(mid, h);
        ctx.stroke();
      }
    }

    function drawContactShadow(ctx, cx, cy, rx, ry) {
      ctx.save();
      ctx.translate(cx, cy);
      ctx.scale(1, Math.max(0.15, ry / Math.max(1, rx)));
      const rg = ctx.createRadialGradient(0, 0, 0, 0, 0, Math.max(4, rx));
      const a = state.dark ? 0.44 : 0.25;
      rg.addColorStop(0, `rgba(0, 0, 0, ${a})`);
      rg.addColorStop(0.62, `rgba(0, 0, 0, ${a * 0.45})`);
      rg.addColorStop(1, "rgba(0, 0, 0, 0)");
      ctx.fillStyle = rg;
      ctx.beginPath();
      ctx.arc(0, 0, Math.max(4, rx), 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }

    function getOrigFrameImage(itemId) {
      const entry = state.origItems.get(itemId);
      if (!entry || !Array.isArray(entry.frames) || !entry.frames.length) return null;
      let frameIdx = 0;
      if (entry.frames.length > 1 && entry.total_ms > 0) {
        const ms = ((state.simTime * 1000) % entry.total_ms + entry.total_ms) % entry.total_ms;
        let acc = 0;
        for (let i = 0; i < entry.frames.length; i++) {
          acc += Math.max(16, entry.frames[i].duration_ms || 120);
          if (ms < acc) {
            frameIdx = i;
            break;
          }
        }
      }
      const frame = entry.frames[frameIdx];
      const key = `${itemId}:${frameIdx}`;
      let rec = state.origImages.get(key);
      if (!rec) {
        const img = new Image();
        rec = { img, loaded: false, w: frame.w, h: frame.h };
        img.onload = () => {
          rec.loaded = true;
          markDirty();
        };
        img.src = frame.data_uri;
        state.origImages.set(key, rec);
      }
      return { rec, frame, frameIdx, totalFrames: entry.frames.length };
    }

    function drawOrigPanel(ctx, itemId, x0, panelW, panelH, groundY) {
      const info = getOrigFrameImage(itemId);
      if (!info || !info.rec.loaded) {
        return { pxScale: 2, w: 64, h: 64, frameIdx: 0, totalFrames: 1 };
      }
      const fw = Math.max(1, info.frame.w || info.rec.img.naturalWidth || 1);
      const fh = Math.max(1, info.frame.h || info.rec.img.naturalHeight || 1);
      const topMargin = Math.max(24, panelH * 0.14);
      const availW = Math.max(16, panelW * 0.80);
      const availH = Math.max(16, (groundY - topMargin) * 0.94);
      const rawScale = Math.min(availW / fw, availH / fh);
      const pxScale = rawScale >= 2 ? Math.floor(rawScale * 2) / 2 : rawScale;
      const dw = Math.max(1, Math.round(fw * pxScale));
      const dh = Math.max(1, Math.round(fh * pxScale));
      const dx = Math.round(x0 + (panelW - dw) * 0.5);
      const dy = Math.round(groundY - dh + Math.min(6, dh * 0.04));

      const shadowRx = clamp(dw * 0.38, 12, panelW * 0.36);
      drawContactShadow(ctx, x0 + panelW * 0.5, groundY, shadowRx, Math.max(4, shadowRx * 0.22));

      let srcImg = info.rec.img;
      if (state.silhouette) {
        if (silCanvas.width !== fw || silCanvas.height !== fh) {
          silCanvas.width = fw;
          silCanvas.height = fh;
        }
        silCtx.globalCompositeOperation = "source-over";
        silCtx.clearRect(0, 0, fw, fh);
        silCtx.drawImage(info.rec.img, 0, 0, fw, fh);
        silCtx.globalCompositeOperation = "source-in";
        silCtx.fillStyle = state.dark ? "#e8eef8" : "#11161f";
        silCtx.fillRect(0, 0, fw, fh);
        srcImg = silCanvas;
      }

      ctx.save();
      ctx.beginPath();
      ctx.rect(x0, 0, panelW, panelH);
      ctx.clip();
      ctx.imageSmoothingEnabled = pxScale < 1;
      ctx.drawImage(srcImg, 0, 0, fw, fh, dx, dy, dw, dh);
      ctx.restore();

      return { pxScale, w: fw, h: fh, frameIdx: info.frameIdx, totalFrames: info.totalFrames };
    }

    function draw3dPanel(ctx, item, x0, panelW, panelH, groundY, cam, pxScale, allowPlinth) {
      const sc = state.scenes.get(item.id);
      if (!sc) {
        return;
      }
      const model = sc.instances[0].model;
      const aspect = Math.max(0.25, panelW / Math.max(1, panelH));
      const isOrtho = cam.projection === "ortho";
      const fitHalfH = computeFitHalfH(model, cam.yaw, cam.pitch, aspect, isOrtho);
      const showPlinth = Boolean(allowPlinth && state.showPlinth && !state.pixelMode && !state.silhouette);

      if (!state.silhouette) {
        paintPresetBackdrop2d(ctx, x0, panelW, panelH, groundY, state.lighting);
      }

      if (!showPlinth) {
        const shadowRx = clamp(panelW * 0.28 / Math.max(0.5, cam.zoom), 14, panelW * 0.36);
        drawContactShadow(ctx, x0 + panelW * 0.5, groundY, shadowRx, Math.max(4, shadowRx * 0.22));
      }

      // Shift vertical camera target slightly so model sits cleanly above the ground line
      const gFrac = groundY / Math.max(1, panelH);
      const centerShiftFrac = (0.5 - (gFrac - 0.36));
      const targetY = clamp(model.size[1] * 0.46 + fitHalfH * centerShiftFrac * 0.22, model.size[1] * 0.25, model.size[1] * 0.72);
      const yRad = cam.yaw * DEG, pRad = cam.pitch * DEG;
      const px = cam.panX || 0, py = cam.panY || 0;
      const panWorldX = px * Math.cos(yRad) - py * Math.sin(pRad) * Math.sin(yRad);
      const panWorldY = py * Math.cos(pRad);
      const panWorldZ = -px * Math.sin(yRad) - py * Math.sin(pRad) * Math.cos(yRad);
      const customTarget = [panWorldX, targetY + panWorldY, panWorldZ];
      const hasAnim = model.idleAnims && model.idleAnims.length > 0;

      if (state.pixelMode && pxScale > 1.25) {
        const lowW = Math.max(20, Math.floor(panelW / pxScale));
        const lowH = Math.max(20, Math.floor(panelH / pxScale));
        ensureOffscreenSize(lowW, lowH);
        renderer.drawScene(sc, cam, {
          width: lowW,
          height: lowH,
          transparent: true,
          showPlinth: false,
          dark: state.dark,
          team: "green",
          lighting: state.lighting,
          blinkMode: "off",
          idleAnim: hasAnim,
          clipName: "idle",
          fixedIdleTime: hasAnim ? state.simTime : null,
          silhouette: state.silhouette,
          overrideProj: cam.projection,
          fixedHalfH: fitHalfH,
          customTarget,
        });
        const dstW = Math.round(lowW * pxScale);
        const dstH = Math.round(lowH * pxScale);
        const dstX = Math.round(x0 + (panelW - dstW) * 0.5);
        const dstY = Math.round((panelH - dstH) * 0.5);
        ctx.save();
        ctx.beginPath();
        ctx.rect(x0, 0, panelW, panelH);
        ctx.clip();
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(offCanvas, 0, offCanvas.height - lowH, lowW, lowH, dstX, dstY, dstW, dstH);
        ctx.restore();
        return;
      }

      ensureOffscreenSize(panelW, panelH);
      renderer.drawScene(sc, cam, {
        width: panelW,
        height: panelH,
        transparent: !showPlinth,
        showPlinth,
        dark: state.dark,
        team: "green",
        lighting: state.lighting,
        blinkMode: "off",
        idleAnim: hasAnim,
        clipName: "idle",
        fixedIdleTime: hasAnim ? state.simTime : null,
        silhouette: state.silhouette,
        overrideProj: cam.projection,
        fixedHalfH: fitHalfH,
        customTarget,
      });
      ctx.save();
      ctx.beginPath();
      ctx.rect(x0, 0, panelW, panelH);
      ctx.clip();
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(offCanvas, 0, offCanvas.height - panelH, panelW, panelH, x0, 0, panelW, panelH);
      ctx.restore();
    }

    async function ensureModelLoaded(item) {
      if (!item || state.scenes.has(item.id) || state.loadingIds.has(item.id) || state.failedIds.has(item.id)) {
        return;
      }
      state.loadingIds.add(item.id);
      try {
        const res = await fetch(`models/items/${item.file}`, { cache: "no-cache" });
        if (!res.ok) throw new Error(`HTTP ${res.status} loading ${item.file}`);
        const buf = await res.arrayBuffer();
        const model = await parseGlb(buf, renderer.gl);
        const yLift = Math.abs(model.bounds.min[1]) > 0.004 ? -model.bounds.min[1] : 0;
        const offsetMat = mat4Translate(-model.center[0], yLift, -model.center[2]);
        const rad = Math.max(0.35, Math.hypot(model.size[0], model.size[2]) * 0.55);
        const inst = { id: item.id, model, offsetMat, blink: null, idleTime: 0 };
        state.scenes.set(item.id, {
          instances: [inst],
          target: [0, model.size[1] * 0.46, 0],
          fitRadius: Math.max(model.size[1] * 0.6, rad),
          halfWidth: rad * 1.15,
          plinthSpanX: 0,
          plinthRadius: Math.max(0.55, rad * 1.15),
          shadows: [[0, 0, Math.max(0.22, model.size[0] * 0.44), Math.max(0.22, model.size[2] * 0.44)]],
        });
        const card = state.cards.get(item.id);
        if (card && card.loadingEl) card.loadingEl.hidden = true;
        markDirty();
        syncDebugState();
      } catch (err) {
        state.failedIds.add(item.id);
        const card = state.cards.get(item.id);
        if (card && card.loadingEl) {
          card.loadingEl.textContent = "Load failed";
          card.loadingEl.hidden = false;
        }
        console.error("Failed to load map item GLB:", item.id, err);
      } finally {
        state.loadingIds.delete(item.id);
      }
    }

    const loadQueue = [];
    let activeLoads = 0;
    const MAX_CONCURRENT_LOADS = 4;

    function queueLoad(item, highPriority = false) {
      if (!item || state.scenes.has(item.id) || state.loadingIds.has(item.id) || state.failedIds.has(item.id)) return;
      const idx = loadQueue.indexOf(item);
      if (idx >= 0) {
        if (highPriority) {
          loadQueue.splice(idx, 1);
          loadQueue.unshift(item);
        }
        return;
      }
      if (highPriority) loadQueue.unshift(item);
      else loadQueue.push(item);
      pumpLoadQueue();
    }

    function pumpLoadQueue() {
      while (activeLoads < MAX_CONCURRENT_LOADS && loadQueue.length > 0) {
        const next = loadQueue.shift();
        if (!next || state.scenes.has(next.id) || state.loadingIds.has(next.id) || state.failedIds.has(next.id)) continue;
        activeLoads++;
        ensureModelLoaded(next).finally(() => {
          activeLoads--;
          pumpLoadQueue();
        });
      }
    }

    const cardObserver = new IntersectionObserver(
      (entries) => {
        for (const ent of entries) {
          const id = ent.target.dataset.cardItem;
          if (!id) continue;
          if (ent.isIntersecting) {
            state.visibleCardIds.add(id);
            const item = state.byId.get(id);
            if (item) queueLoad(item, true);
          } else {
            state.visibleCardIds.delete(id);
          }
        }
        markDirty();
      },
      { root: stageMain, rootMargin: "240px 0px" }
    );

    function rebuildGridCards() {
      cardObserver.disconnect();
      state.cards.clear();
      state.visibleCardIds.clear();
      gridEl.textContent = "";

      const filtered = getFilteredItems();
      badgeCount.textContent = `${filtered.length} shown`;

      if (!filtered.length) {
        const msg = document.createElement("div");
        msg.className = "empty-card";
        msg.innerHTML = "<h2>No matching map items</h2><p>Try clearing the search box or choosing another category.</p>";
        gridEl.appendChild(msg);
        return;
      }

      const frag = document.createDocumentFragment();
      for (const item of filtered) {
        const card = document.createElement("article");
        card.className = "item-card";
        card.dataset.cardItem = item.id;

        const head = document.createElement("header");
        head.className = "item-card-head";

        const titleWrap = document.createElement("div");
        titleWrap.className = "item-card-title-wrap";
        const h2 = document.createElement("h2");
        h2.className = "item-card-title";
        h2.textContent = item.label;
        const sub = document.createElement("span");
        sub.className = "item-card-sub";
        sub.textContent = `${item.chd_name} \u00b7 #${item.chd_id}`;
        titleWrap.append(h2, sub);

        const actions = document.createElement("div");
        actions.className = "item-card-actions";
        const catTag = document.createElement("span");
        catTag.className = "item-cat-tag";
        catTag.textContent = CAT_LABELS[item.category] || item.category;
        const focusBtn = document.createElement("button");
        focusBtn.type = "button";
        focusBtn.className = "card-focus-btn";
        focusBtn.dataset.focusItem = item.id;
        focusBtn.setAttribute("aria-label", `Focus ${item.label} side-by-side comparison`);
        focusBtn.textContent = "Focus";
        actions.append(catTag, focusBtn);
        head.append(titleWrap, actions);

        const stage = document.createElement("div");
        stage.className = "item-card-stage";
        stage.dataset.stageItem = item.id;

        const canvas = document.createElement("canvas");
        canvas.className = "item-card-canvas";
        canvas.setAttribute("aria-label", `${item.label} 1998 original sprite beside live 3D model`);

        const pillL = document.createElement("span");
        pillL.className = "compare-pill compare-pill-left";
        pillL.setAttribute("aria-hidden", "true");
        pillL.textContent = "1998 Original";

        const pillR = document.createElement("span");
        pillR.className = "compare-pill compare-pill-right";
        pillR.setAttribute("aria-hidden", "true");
        pillR.textContent = "Live 3D";

        const loadingEl = document.createElement("span");
        loadingEl.className = "item-card-loading";
        loadingEl.textContent = "Loading 3D\u2026";
        loadingEl.hidden = state.scenes.has(item.id);

        stage.append(canvas, pillL, pillR, loadingEl);
        card.append(head, stage);
        frag.appendChild(card);

        state.cards.set(item.id, {
          item,
          el: card,
          canvas,
          ctx: canvas.getContext("2d"),
          loadingEl,
        });
        cardObserver.observe(card);
        state.visibleCardIds.add(item.id);
      }
      gridEl.appendChild(frag);
      for (const item of filtered) queueLoad(item, false);
      markDirty();
    }

    function updateDetailsPanel() {
      const item = state.activeId ? state.byId.get(state.activeId) : null;
      if (state.manifest) {
        const built = state.manifest.built_count || state.items.length;
        const total = state.manifest.total_catalog_sprites || 211;
        badgeProgress.textContent = `${built} / ${total} items`;
        metaCatalog.textContent = `${built} / ${total} built`;
      }
      if (!item) {
        badgeDetailsSummary.textContent = CAT_LABELS[state.category] || "All";
        metaSprite.textContent = `${getFilteredItems().length} items in view`;
        const totalTris = getFilteredItems().reduce((acc, it) => acc + (it.triangles || 0), 0);
        metaMesh.textContent = `${totalTris.toLocaleString()} total tris`;
        metaSize.textContent = "Select any card to inspect";
        return;
      }
      const orig = state.origItems.get(item.id);
      const fw = orig && orig.frames && orig.frames[0] ? orig.frames[0].w : 0;
      const fh = orig && orig.frames && orig.frames[0] ? orig.frames[0].h : 0;
      const nf = orig ? orig.frame_count : 1;
      badgeDetailsSummary.textContent = item.chd_name;
      metaSprite.textContent = `#${item.chd_id} \u00b7 ${fw}\u00d7${fh}px` + (nf > 1 ? ` (${nf}f)` : "");
      metaMesh.textContent = `${(item.triangles || 0).toLocaleString()} tris \u00b7 ${Math.round((item.bytes || 0) / 1024)} KB`;
      const sz = item.size || [0, 0, 0];
      metaSize.textContent = `${sz[0].toFixed(2)} \u00d7 ${sz[1].toFixed(2)} \u00d7 ${sz[2].toFixed(2)}`;
    }

    function rebuildSoloFamilyPills(item) {
      soloFamilyPills.textContent = "";
      const peers = (item && item.family && state.families.get(item.family)) || [];
      if (peers.length <= 1) {
        soloFamilyPills.hidden = true;
        return;
      }
      soloFamilyPills.hidden = false;
      for (const peer of peers) {
        const b = document.createElement("button");
        b.type = "button";
        b.dataset.focusItem = peer.id;
        b.setAttribute("aria-pressed", peer.id === item.id ? "true" : "false");
        b.textContent = peer.stage_label || peer.label;
        soloFamilyPills.appendChild(b);
      }
    }

    function updateUiState() {
      // Category buttons
      for (const btn of catGroup.querySelectorAll("button[data-cat]")) {
        btn.setAttribute("aria-pressed", btn.dataset.cat === state.category ? "true" : "false");
      }
      // Camera preset buttons
      for (const btn of document.querySelectorAll("button[data-cam]")) {
        btn.setAttribute("aria-pressed", btn.dataset.cam === state.camPreset ? "true" : "false");
      }
      // Projection buttons
      for (const btn of document.querySelectorAll("button[data-proj]")) {
        btn.setAttribute("aria-pressed", btn.dataset.proj === state.projection ? "true" : "false");
      }
      btnPlay.setAttribute("aria-pressed", state.playing ? "true" : "false");
      btnSpin.setAttribute("aria-pressed", state.spin ? "true" : "false");
      btnPixelMode.setAttribute("aria-pressed", state.pixelMode ? "true" : "false");
      btnSilhouette.setAttribute("aria-pressed", state.silhouette ? "true" : "false");
      btnPlinth.setAttribute("aria-pressed", state.showPlinth ? "true" : "false");
      selectLighting.value = state.lighting;

      const activeItem = state.activeId ? state.byId.get(state.activeId) : null;
      const inSolo = Boolean(activeItem);
      gridEl.hidden = inSolo;
      soloEl.hidden = !inSolo;
      if (inSolo) {
        soloTitle.textContent = activeItem.label;
        soloMeta.textContent = `${activeItem.chd_name} \u00b7 #${activeItem.chd_id} \u00b7 ${CAT_LABELS[activeItem.category] || activeItem.category}`;
        rebuildSoloFamilyPills(activeItem);
        const cam = getSoloCamera(activeItem);
        solo3dPill.textContent = `Live 3D \u00b7 ${Math.round(cam.yaw)}\u00b0 / ${Math.round(cam.pitch)}\u00b0`;
        queueLoad(activeItem, true);
      }
      updateDetailsPanel();
      syncDebugState();
    }

    function selectCategory(cat) {
      const clean = CAT_LABELS[cat] ? cat : "all";
      state.category = clean;
      if (state.activeId) {
        const cur = state.byId.get(state.activeId);
        if (!cur || (clean !== "all" && cur.category !== clean)) {
          state.activeId = null;
        }
      }
      rebuildGridCards();
      updateUiState();
      announce(`Showing ${CAT_LABELS[clean]} map items.`);
    }

    function selectItem(id) {
      if (!id || !state.byId.has(id)) {
        state.activeId = null;
        updateUiState();
        markDirty();
        announce("Returned to map items grid.");
        return;
      }
      state.activeId = id;
      state.soloYaw = null;
      state.soloPitch = null;
      state.soloZoom = 1.0;
      state.soloPanX = 0.0;
      state.soloPanY = 0.0;
      const item = state.byId.get(id);
      queueLoad(item, true);
      updateUiState();
      markDirty();
      announce(`Focused ${item.label} side-by-side comparison.`);
    }

    function stepSoloItem(delta) {
      const filtered = getFilteredItems();
      if (!filtered.length) return;
      const idx = filtered.findIndex((it) => it.id === state.activeId);
      const nextIdx = idx < 0 ? 0 : (idx + delta + filtered.length) % filtered.length;
      selectItem(filtered[nextIdx].id);
    }

    function selectCamPreset(presetId) {
      if (!CAM_PRESETS[presetId]) return;
      state.camPreset = presetId;
      state.gridYawOffset = 0;
      state.soloYaw = null;
      state.soloPitch = null;
      state.soloZoom = 1.0;
      updateUiState();
      markDirty();
      announce(`Camera preset set to ${CAM_PRESETS[presetId].label}.`);
    }

    function selectProjection(proj) {
      state.projection = proj === "ortho" ? "ortho" : "perspective";
      updateUiState();
      markDirty();
      announce(`Projection set to ${state.projection}.`);
    }

    function selectLightingPreset(id) {
      state.lighting = validLighting(id) || "studio";
      updateUiState();
      markDirty();
      announce(`Lighting set to ${getLightingPreset(state.lighting).label}.`);
    }

    function resetCamera() {
      state.gridYawOffset = 0;
      state.soloYaw = null;
      state.soloPitch = null;
      state.soloZoom = 1.0;
      state.soloPanX = 0.0;
      state.soloPanY = 0.0;
      state.spinAngle = 0;
      updateUiState();
      markDirty();
      announce("Camera angles reset.");
    }

    function renderAll() {
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      if (state.activeId && state.byId.has(state.activeId)) {
        const item = state.byId.get(state.activeId);
        const [ow, oh] = syncCanvasBacking(soloOrigCanvas, dpr);
        const [rw, rh] = syncCanvasBacking(solo3dCanvas, dpr);
        const gOrig = Math.round(oh * 0.84);
        drawStudioBackdrop(soloOrigCtx, ow, oh, gOrig, false);
        const origInfo = drawOrigPanel(soloOrigCtx, item.id, 0, ow, oh, gOrig);
        soloOrigPill.textContent = origInfo.totalFrames > 1
          ? `1998 Original \u00b7 ${origInfo.w}\u00d7${origInfo.h} (${origInfo.frameIdx + 1}/${origInfo.totalFrames})`
          : `1998 Original \u00b7 ${origInfo.w}\u00d7${origInfo.h}`;

        const g3d = Math.round(rh * 0.84);
        drawStudioBackdrop(solo3dCtx, rw, rh, g3d, false);
        const cam = getSoloCamera(item);
        draw3dPanel(solo3dCtx, item, 0, rw, rh, g3d, cam, origInfo.pxScale, true);
        solo3dPill.textContent = `Live 3D \u00b7 ${Math.round(cam.yaw)}\u00b0 / ${Math.round(cam.pitch)}\u00b0`;
        return;
      }

      for (const id of state.visibleCardIds) {
        const rec = state.cards.get(id);
        if (!rec) continue;
        const [w, h] = syncCanvasBacking(rec.canvas, dpr);
        if (w < 4 || h < 4) continue;
        const halfW = Math.floor(w * 0.5);
        const rightW = w - halfW;
        const groundY = Math.round(h * 0.84);
        drawStudioBackdrop(rec.ctx, w, h, groundY, true);
        const origInfo = drawOrigPanel(rec.ctx, id, 0, halfW, h, groundY);
        draw3dPanel(rec.ctx, rec.item, halfW, rightW, h, groundY, getCardCamera(rec.item), origInfo.pxScale, false);
      }
    }

    function syncDebugState() {
      const filtered = getFilteredItems();
      window.__antItems = {
        ready: state.ready,
        loadedCount: state.scenes.size,
        totalCount: state.items.length,
        filteredCount: filtered.length,
        view: state.activeId ? "solo" : "grid",
        activeItem: state.activeId,
        category: state.category,
        camPreset: state.camPreset,
        projection: state.projection,
        lighting: state.lighting,
        spin: state.spin,
        error: state.error,
        getItemIds: () => state.items.map((it) => it.id),
        selectItem: (id) => selectItem(id),
        selectCategory: (cat) => selectCategory(cat),
        setCameraPreset: (p) => selectCamPreset(p),
        setCamera: (yaw, pitch, zoom) => {
          if (Number.isFinite(yaw)) state.soloYaw = wrapAngleDeg(yaw);
          if (Number.isFinite(pitch)) state.soloPitch = clamp(pitch, -10, 85);
          if (Number.isFinite(zoom)) state.soloZoom = clamp(zoom, 0.45, 2.6);
          updateUiState();
          markDirty();
        },
        setLighting: (id) => selectLightingPreset(id),
        setProjection: (p) => selectProjection(p),
        setSpin: (on) => { state.spin = Boolean(on); updateUiState(); markDirty(); },
        renderNow: () => { renderAll(); state.dirty = false; },
      };
    }

    // Panel Drawer Toggle
    let panelOpen = !mobileMq.matches;
    function syncPanel() {
      const mobile = mobileMq.matches;
      document.body.classList.toggle("panel-collapsed", !panelOpen);
      btnTogglePanel.setAttribute("aria-expanded", panelOpen ? "true" : "false");
      inspectorBackdrop.hidden = !(mobile && panelOpen);
      markDirty();
    }
    btnTogglePanel.addEventListener("click", () => { panelOpen = !panelOpen; syncPanel(); });
    btnClosePanel.addEventListener("click", () => { panelOpen = false; syncPanel(); });
    inspectorBackdrop.addEventListener("click", () => { panelOpen = false; syncPanel(); });
    mobileMq.addEventListener("change", () => { panelOpen = !mobileMq.matches; syncPanel(); });
    syncPanel();

    // Toolbar roving tabindex fallback ONLY when native focusGroup is unsupported
    if (!("focusGroup" in HTMLElement.prototype)) {
      installToolbarRovingTabindex(document.getElementById("items-toolbar"));
      installToolbarRovingTabindex(document.getElementById("items-dock"));
    }

    // Event Wiring
    catGroup.addEventListener("click", (e) => {
      const btn = e.target.closest("button[data-cat]");
      if (btn) selectCategory(btn.dataset.cat);
    });

    document.getElementById("items-dock").addEventListener("click", (e) => {
      const camBtn = e.target.closest("button[data-cam]");
      if (camBtn) { selectCamPreset(camBtn.dataset.cam); return; }
      const projBtn = e.target.closest("button[data-proj]");
      if (projBtn) { selectProjection(projBtn.dataset.proj); return; }
    });

    btnPlay.addEventListener("click", () => {
      state.playing = !state.playing;
      updateUiState();
      markDirty();
      announce(state.playing ? "Item animations playing." : "Item animations paused.");
    });

    btnSpin.addEventListener("click", () => {
      state.spin = !state.spin;
      updateUiState();
      markDirty();
      announce(state.spin ? "360 degree turntable spin enabled." : "Turntable spin stopped.");
    });

    btnResetAll.addEventListener("click", resetCamera);
    btnSoloReset.addEventListener("click", resetCamera);
    btnBackGrid.addEventListener("click", () => selectItem(null));
    btnSoloPrev.addEventListener("click", () => stepSoloItem(-1));
    btnSoloNext.addEventListener("click", () => stepSoloItem(1));

    soloFamilyPills.addEventListener("click", (e) => {
      const btn = e.target.closest("button[data-focus-item]");
      if (btn) selectItem(btn.dataset.focusItem);
    });

    btnLightPrev.addEventListener("click", () => {
      const idx = LIGHTING_ORDER.indexOf(state.lighting);
      selectLightingPreset(LIGHTING_ORDER[(idx - 1 + LIGHTING_ORDER.length) % LIGHTING_ORDER.length]);
    });
    btnLightNext.addEventListener("click", () => {
      const idx = LIGHTING_ORDER.indexOf(state.lighting);
      selectLightingPreset(LIGHTING_ORDER[(idx + 1) % LIGHTING_ORDER.length]);
    });
    selectLighting.addEventListener("change", () => selectLightingPreset(selectLighting.value));

    inputSearch.addEventListener("input", () => {
      state.search = inputSearch.value;
      rebuildGridCards();
      updateDetailsPanel();
    });

    btnPixelMode.addEventListener("click", () => {
      state.pixelMode = !state.pixelMode;
      updateUiState();
      markDirty();
      announce(state.pixelMode ? "1x pixel scale enabled." : "Full HD 3D rendering enabled.");
    });

    btnSilhouette.addEventListener("click", () => {
      state.silhouette = !state.silhouette;
      updateUiState();
      markDirty();
      announce(state.silhouette ? "Silhouette comparison enabled." : "Silhouette comparison disabled.");
    });

    btnPlinth.addEventListener("click", () => {
      state.showPlinth = !state.showPlinth;
      updateUiState();
      markDirty();
      announce(state.showPlinth ? "Focus plinth enabled." : "Focus plinth hidden.");
    });

    // Horizontal drag on grid cards to turn all cards; click to focus
    let suppressCardClickUntil = 0;
    let cardDrag = null;
    gridEl.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 && e.pointerType === "mouse") return;
      const stageHit = e.target.closest("[data-stage-item]");
      if (!stageHit) return;
      cardDrag = { id: e.pointerId, startX: e.clientX, startY: e.clientY, lastX: e.clientX, active: false };
    });
    gridEl.addEventListener("pointermove", (e) => {
      if (!cardDrag || e.pointerId !== cardDrag.id) return;
      const tdx = Math.abs(e.clientX - cardDrag.startX);
      const tdy = Math.abs(e.clientY - cardDrag.startY);
      if (!cardDrag.active) {
        if (e.pointerType === "touch" && tdy > 16 && tdy > tdx * 1.6) { cardDrag = null; return; }
        if (tdx <= 4) return;
        cardDrag.active = true;
        try { gridEl.setPointerCapture(e.pointerId); } catch (_) {}
      }
      const dx = e.clientX - cardDrag.lastX;
      cardDrag.lastX = e.clientX;
      const cardEl = gridEl.querySelector(".item-card-stage");
      const halfW = Math.max(120, (cardEl ? cardEl.clientWidth : 280) * 0.5);
      const sens = clamp(260 / halfW, 0.75, 1.80);
      state.gridYawOffset = wrapAngleDeg(state.gridYawOffset - dx * sens);
      markDirty();
    });
    const endCardDrag = (e) => {
      if (!cardDrag || e.pointerId !== cardDrag.id) return;
      if (cardDrag.active) suppressCardClickUntil = performance.now() + 260;
      cardDrag = null;
    };
    gridEl.addEventListener("pointerup", endCardDrag);
    gridEl.addEventListener("pointercancel", endCardDrag);
    gridEl.addEventListener("lostpointercapture", endCardDrag);

    gridEl.addEventListener("click", (e) => {
      const focusBtn = e.target.closest("button[data-focus-item]");
      if (focusBtn) { selectItem(focusBtn.dataset.focusItem); return; }
      if (performance.now() < suppressCardClickUntil) return;
      const stageHit = e.target.closest("[data-stage-item]");
      if (stageHit) selectItem(stageHit.dataset.stageItem);
    });

    // Full 360 orbit + pitch + 2-finger/right-drag pan + pinch/wheel zoom on Focus 3D Canvas
    const pointers = new Map();
    let lastPinch = 0;
    let lastMid = null;
    let mousePanMode = false;
    const pinchDist = () => {
      const pts = Array.from(pointers.values());
      return pts.length < 2 ? 0 : Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
    };
    const pinchMid = () => {
      const pts = Array.from(pointers.values());
      return pts.length < 2 ? null : { x: 0.5 * (pts[0].x + pts[1].x), y: 0.5 * (pts[0].y + pts[1].y) };
    };
    const applySoloPan = (dx, dy) => {
      const hCss = Math.max(140, solo3dCanvas.clientHeight || 300);
      const worldPerPx = (1.6 * state.soloZoom) / hCss;
      state.soloPanX = clamp(state.soloPanX - dx * worldPerPx, -1.4, 1.4);
      state.soloPanY = clamp(state.soloPanY + dy * worldPerPx, -1.2, 1.2);
    };
    solo3dCanvas.addEventListener("contextmenu", (e) => e.preventDefault());
    solo3dCanvas.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      try { solo3dCanvas.setPointerCapture(e.pointerId); } catch (_) {}
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      mousePanMode = e.pointerType === "mouse" && (e.button === 2 || e.button === 1 || e.shiftKey);
      if (pointers.size === 2) {
        lastPinch = pinchDist();
        lastMid = pinchMid();
      }
    });
    solo3dCanvas.addEventListener("pointermove", (e) => {
      const prev = pointers.get(e.pointerId);
      if (!prev) return;
      const dx = e.clientX - prev.x;
      const dy = e.clientY - prev.y;
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      const item = state.activeId ? state.byId.get(state.activeId) : null;
      if (!item) return;
      const base = getBaseAngles(item);
      if (pointers.size === 1) {
        if (mousePanMode || e.shiftKey) {
          applySoloPan(dx, dy);
        } else {
          const curYaw = state.soloYaw !== null ? state.soloYaw : base.yaw;
          const curPitch = state.soloPitch !== null ? state.soloPitch : base.pitch;
          const wCss = Math.max(120, solo3dCanvas.clientWidth || 240);
          const hCss = Math.max(160, solo3dCanvas.clientHeight || 300);
          const yawPerPx = clamp(280 / wCss, 0.70, 1.90);
          const pitchPerPx = clamp(180 / hCss, 0.55, 1.25);
          state.soloYaw = wrapAngleDeg(curYaw - dx * yawPerPx);
          state.soloPitch = clamp(curPitch + dy * pitchPerPx, -12, 85);
        }
        markDirty();
      } else if (pointers.size === 2) {
        const dist = pinchDist();
        const mid = pinchMid();
        if (lastPinch > 1 && dist > 1) {
          state.soloZoom = clamp(state.soloZoom * (lastPinch / dist), 0.35, 2.8);
        }
        if (lastMid && mid) {
          applySoloPan(mid.x - lastMid.x, mid.y - lastMid.y);
        }
        lastPinch = dist;
        lastMid = mid;
        markDirty();
      }
    });
    const endSoloPtr = (e) => {
      pointers.delete(e.pointerId);
      if (pointers.size < 2) { lastPinch = 0; lastMid = null; }
      if (pointers.size === 0) mousePanMode = false;
    };
    solo3dCanvas.addEventListener("pointerup", endSoloPtr);
    solo3dCanvas.addEventListener("pointercancel", endSoloPtr);
    solo3dCanvas.addEventListener("lostpointercapture", endSoloPtr);
    const preventSoloTouch = (e) => { e.preventDefault(); };
    solo3dCanvas.addEventListener("touchstart", preventSoloTouch, { passive: false });
    solo3dCanvas.addEventListener("touchmove", preventSoloTouch, { passive: false });
    solo3dCanvas.addEventListener("gesturestart", preventSoloTouch, { passive: false });
    solo3dCanvas.addEventListener("gesturechange", preventSoloTouch, { passive: false });
    solo3dCanvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      state.soloZoom = clamp(state.soloZoom * Math.exp(e.deltaY * 0.0012), 0.35, 2.8);
      markDirty();
    }, { passive: false });

    solo3dCanvas.addEventListener("keydown", (e) => {
      const item = state.activeId ? state.byId.get(state.activeId) : null;
      if (!item) return;
      const base = getBaseAngles(item);
      const curYaw = state.soloYaw !== null ? state.soloYaw : base.yaw;
      const curPitch = state.soloPitch !== null ? state.soloPitch : base.pitch;
      const step = e.shiftKey ? 4 : 12;
      if (e.key === "ArrowLeft") state.soloYaw = wrapAngleDeg(curYaw + step);
      else if (e.key === "ArrowRight") state.soloYaw = wrapAngleDeg(curYaw - step);
      else if (e.key === "ArrowUp") state.soloPitch = clamp(curPitch + step * 0.7, -10, 85);
      else if (e.key === "ArrowDown") state.soloPitch = clamp(curPitch - step * 0.7, -10, 85);
      else if (e.key === "+" || e.key === "=") state.soloZoom = clamp(state.soloZoom * 0.88, 0.45, 2.6);
      else if (e.key === "-" || e.key === "_") state.soloZoom = clamp(state.soloZoom * 1.14, 0.45, 2.6);
      else return;
      e.preventDefault();
      markDirty();
    });

    document.addEventListener("keydown", (e) => {
      if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
      if (e.key === "Escape" && state.activeId) { e.preventDefault(); selectItem(null); }
      else if (e.key === "[" && state.activeId) { e.preventDefault(); stepSoloItem(-1); }
      else if (e.key === "]" && state.activeId) { e.preventDefault(); stepSoloItem(1); }
      else if (e.key === "t" || e.key === "T") { e.preventDefault(); btnSpin.click(); }
      else if (e.key === "i" || e.key === "I") { e.preventDefault(); btnPlay.click(); }
      else if (e.key === "p" || e.key === "P") { e.preventDefault(); btnPixelMode.click(); }
      else if (e.key === "s" || e.key === "S") { e.preventDefault(); btnSilhouette.click(); }
      else if (e.key === "r" || e.key === "R") { e.preventDefault(); resetCamera(); }
      else if (e.key === "l" || e.key === "L") { e.preventDefault(); btnLightNext.click(); }
      else if (e.key === "1") { e.preventDefault(); selectCamPreset("orig"); }
      else if (e.key === "2") { e.preventDefault(); selectCamPreset("hero"); }
      else if (e.key === "3") { e.preventDefault(); selectCamPreset("front"); }
      else if (e.key === "4") { e.preventDefault(); selectCamPreset("top"); }
    });

    darkMq.addEventListener("change", (e) => {
      state.dark = e.matches;
      markDirty();
    });

    new ResizeObserver(() => markDirty()).observe(stageMain);

    // Load Manifest & 1998 Sprite Bundle
    async function loadCatalog() {
      try {
        const [manRes, origRes] = await Promise.all([
          fetch("models/items/manifest.json", { cache: "no-cache" }),
          fetch("models/items/orig_items.json", { cache: "no-cache" }),
        ]);
        if (!manRes.ok) throw new Error(`HTTP ${manRes.status} loading models/items/manifest.json`);
        if (!origRes.ok) throw new Error(`HTTP ${origRes.status} loading models/items/orig_items.json`);
        const [manifest, origBundle] = await Promise.all([manRes.json(), origRes.json()]);

        state.manifest = manifest;
        state.items = Array.isArray(manifest.items) ? manifest.items : [];
        state.byId.clear();
        state.families.clear();
        for (const item of state.items) {
          state.byId.set(item.id, item);
          if (item.family) {
            if (!state.families.has(item.family)) state.families.set(item.family, []);
            state.families.get(item.family).push(item);
          }
        }
        state.origItems.clear();
        if (origBundle && origBundle.items) {
          for (const [id, entry] of Object.entries(origBundle.items)) {
            state.origItems.set(id, entry);
          }
        }

        if (manifest.build_id && badgeVersion) {
          badgeVersion.textContent = "v-" + manifest.build_id;
          badgeVersion.hidden = false;
        }

        // Update category button counts
        const counts = { all: state.items.length };
        for (const it of state.items) counts[it.category] = (counts[it.category] || 0) + 1;
        for (const btn of catGroup.querySelectorAll("button[data-cat]")) {
          const cid = btn.dataset.cat;
          const label = CAT_LABELS[cid] || cid;
          const n = counts[cid] || 0;
          btn.textContent = n > 0 ? `${label} (${n})` : label;
        }

        if (state.search) inputSearch.value = state.search;
        if (state.activeId && !state.byId.has(state.activeId)) state.activeId = null;

        rebuildGridCards();
        updateUiState();
        emptyOverlay.hidden = true;
        state.ready = true;
        announce(`Loaded ${state.items.length} map item 3D models and 1998 sprites.`);
        syncDebugState();

        // Start dev live-reload poller on localhost/LAN
        startBuildWatcher(manifest.build_id || "");
      } catch (err) {
        state.error = String(err && err.message ? err.message : err);
        emptyTitle.textContent = "Could not load map item models";
        emptyMessage.textContent = state.error;
        emptyOverlay.hidden = false;
        announce("Map items load failed: " + state.error);
        syncDebugState();
      }
    }

    function startBuildWatcher(initialBuildId) {
      if (window.location.protocol !== "http:" && window.location.protocol !== "https:") return;
      const pollParam = (params.get("poll") || "").toLowerCase();
      const forcePoll = pollParam === "1" || pollParam === "true";
      if (!(isLocalDevHost() || forcePoll) || pollParam === "0" || pollParam === "false") return;
      if (!initialBuildId) return;

      let reloading = false;
      window.setInterval(async () => {
        if (reloading || document.visibilityState === "hidden") return;
        try {
          const r = await fetch("models/items/manifest.json?t=" + Date.now(), { cache: "no-store" });
          if (!r.ok) return;
          const data = await r.json();
          if (data && data.build_id && data.build_id !== initialBuildId) {
            reloading = true;
            try {
              window.sessionStorage.setItem(RELOAD_KEY, JSON.stringify({
                page: window.location.pathname,
                cat: state.category,
                q: state.search,
                item: state.activeId,
                cam: state.camPreset,
                proj: state.projection,
                light: state.lighting,
                pixel: state.pixelMode ? "1" : "0",
                sil: state.silhouette ? "1" : "0",
                plinth: state.showPlinth ? "1" : "0",
              }));
            } catch (_) {}
            if (updateToast) {
              updateToast.textContent = "Updating map items to v-" + data.build_id + "\u2026";
              updateToast.hidden = false;
            }
            window.setTimeout(() => window.location.reload(), 300);
          }
        } catch (_) {}
      }, 3000);
    }

    // Main Render Loop
    let prevTime = performance.now();
    function frame(now) {
      const dt = Math.min((now - prevTime) / 1000, 0.1);
      prevTime = now;
      if (state.playing) {
        state.simTime += dt;
        // Mark dirty only if an animated sprite or animated 3D model is visible
        let anyAnim = false;
        const checkIds = state.activeId ? [state.activeId] : Array.from(state.visibleCardIds);
        for (const id of checkIds) {
          const orig = state.origItems.get(id);
          if (orig && orig.frame_count > 1) { anyAnim = true; break; }
          const sc = state.scenes.get(id);
          if (sc && sc.instances[0].model.idleAnims && sc.instances[0].model.idleAnims.length > 0) {
            anyAnim = true;
            break;
          }
        }
        if (anyAnim) state.dirty = true;
      }
      if (state.spin) {
        state.spinAngle = wrapAngleDeg(state.spinAngle + dt * 36);
        state.dirty = true;
      }
      if (state.dirty) {
        renderAll();
        state.dirty = false;
      }
      requestAnimationFrame(frame);
    }

    loadCatalog();
    requestAnimationFrame(frame);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", main, { once: true });
  } else {
    main();
  }
})();

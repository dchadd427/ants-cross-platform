"use strict";

(() => {
  const { clamp, mat4Translate, parseGlb } = window.__antGltf;
  const {
    ANT_CLASSES, ANT_LABELS, DEFAULT_CUSTOM_HEX, TEAMS, ANGLES, SHEET_ANGLES,
    LIGHTING_ORDER, validLighting, getLightingPreset, populateLightingSelect,
    wrapAngleDeg, normalizeHex, resolveTeam, selectActionAnims, AntRenderer, installToolbarRovingTabindex,
  } = window.__antRender;
  const {
    QUICK_ACTIONS, validAction, getAction, actionSupportsClass, stepSecFor, populateActionSelect,
  } = window.__antActions;

  const emptyScene = () => ({
    instances: [], target: [0, 0.55, 0], fitRadius: 0.9, halfWidth: 0.7,
    plinthSpanX: 0, plinthRadius: 0.72, shadows: [], boundsSize: [0, 0, 0], triCount: 0, meshCount: 0, nodeCount: 0,
  });

  function initViewer() {
    const $ = (id) => document.getElementById(id);
    const stageCanvas = $("stage"), stageWrap = $("stage-wrap"), toolbarEl = $("viewer-toolbar");
    const statusEl = $("status-region"), spriteInset = $("sprite-inset");
    const spriteCanvas = $("sprite-canvas"), spriteCtx = spriteCanvas.getContext("2d");
    const emptyOverlay = $("empty-overlay"), fileInput = $("glb-file-input");
    const colorInput = $("input-team-color"), customSwatch = $("swatch-custom");
    const actionSelect = $("select-action");
    const lightSelect = $("select-lighting");
    const compareTab = $("tab-compare");
    if (!("focusGroup" in HTMLElement.prototype)) installToolbarRovingTabindex(toolbarEl);

    const RELOAD_STORAGE_KEY = "ants_viewer_reload_state_v1";
    let savedView = null;
    try {
      const raw = window.sessionStorage.getItem(RELOAD_STORAGE_KEY);
      if (raw) {
        window.sessionStorage.removeItem(RELOAD_STORAGE_KEY);
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed === "object" && (!parsed.page || parsed.page === window.location.pathname)) savedView = parsed;
      }
    } catch (_) {}

    const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");
    const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
    const params = new URLSearchParams(window.location.search), getP = (k) => (params.get(k) || "").toLowerCase();
    const pLineup = getP("lineup") === "1" || getP("lineup") === "true";
    const pAnt = getP("ant"), pTeam = getP("team"), pColor = normalizeHex(params.get("color")), pAngle = getP("angle"), pProj = getP("proj"), pTurn = getP("turntable"), pBlink = getP("blink"), pIdle = getP("idle"), pAction = getP("action"), pSprite = getP("sprite"), pSil = getP("silhouette"), pRetro = getP("retrofps");
    const pIdleTimeRaw = params.get("idleTime"), fixedIdleTime = pIdleTimeRaw !== null && Number.isFinite(Number(pIdleTimeRaw)) ? Number(pIdleTimeRaw) : null;
    const pYaw = params.has("yaw") && Number.isFinite(Number(params.get("yaw"))) ? wrapAngleDeg(Number(params.get("yaw"))) : null;
    const pPitch = params.has("pitch") && Number.isFinite(Number(params.get("pitch"))) ? clamp(Number(params.get("pitch")), -10, 85) : null;
    const savedAnt = !params.has("lineup") && !params.has("ant") && savedView && (ANT_CLASSES.includes(savedView.ant) || savedView.ant === "lineup") ? savedView.ant : null;
    const initAnt = pLineup ? "lineup" : (ANT_CLASSES.includes(pAnt) || pAnt === "lineup" ? pAnt : (savedAnt || "lineup"));
    const savedColor = normalizeHex(savedView && savedView.customColor);
    const initCustomColor = pColor || savedColor || DEFAULT_CUSTOM_HEX;
    const savedTeam = !params.has("team") && savedView && (savedView.team === "custom" || TEAMS[savedView.team]) ? savedView.team : null;
    const initTeam = (pTeam === "custom" || TEAMS[pTeam]) ? pTeam : (pColor ? "custom" : (savedTeam || "green"));
    const savedAngle = !params.has("angle") && pYaw === null && savedView && (ANGLES[savedView.angle] || savedView.angle === "custom") ? savedView.angle : null;
    const initAngle = ANGLES[pAngle] ? pAngle : (pYaw !== null ? "custom" : (savedAngle || "301"));
    const initProj = pProj === "ortho" || pProj === "persp" ? pProj
      : (!params.has("proj") && pYaw === null && savedView && (savedView.proj === "ortho" || savedView.proj === "persp") ? savedView.proj : (initAngle === "hero" ? "persp" : "ortho"));
    const initTurn = pTurn === "0" || pTurn === "false" ? false : (pTurn === "1" || pTurn === "true" ? true
      : (!params.has("turntable") && savedView && typeof savedView.turntable === "boolean" ? savedView.turntable : false));
    const initSprite = pSprite === "0" || pSprite === "false" ? false : (pSprite === "1" || pSprite === "true" ? true
      : (!params.has("sprite") && savedView && typeof savedView.sprite === "boolean" ? savedView.sprite : true));
    const initSil = pSil === "1" || pSil === "true" ? true : (pSil === "0" || pSil === "false" ? false
      : (!params.has("silhouette") && savedView && typeof savedView.silhouette === "boolean" ? savedView.silhouette : false));
    const initRetro = pRetro === "1" || pRetro === "true" ? true : (pRetro === "0" || pRetro === "false" ? false
      : (!params.has("retrofps") && savedView && typeof savedView.retroFps === "boolean" ? savedView.retroFps : false));
    const initBlink = pBlink === "closed" ? "closed" : (pBlink === "open" ? "open" : (motionQuery.matches ? "open" : "auto"));
    const initIdle = fixedIdleTime !== null ? false : (pIdle === "0" || pIdle === "false" ? false : (pIdle === "1" || pIdle === "true" ? true
      : (!params.has("idle") && savedView && typeof savedView.idle === "boolean" ? savedView.idle : !motionQuery.matches)));
    const initAction = validAction(pAction) || (!params.has("action") && validAction(savedView && savedView.action)) || "idle";
    const initLight = validLighting(getP("light")) || (!params.has("light") && validLighting(savedView && savedView.lighting)) || "studio";

    const preset = ANGLES[initAngle] || ANGLES["301"];
    const useSavedCam = !params.has("angle") && pYaw === null && savedView && Number.isFinite(savedView.yaw) && Number.isFinite(savedView.pitch) && Number.isFinite(savedView.zoom);
    let pendingUpdatedVer = savedView && typeof savedView.updatedVersion === "string" ? savedView.updatedVersion : "";
    const state = {
      ant: initAnt, customLabel: "", team: initTeam, customColor: initCustomColor, angle: initAngle, blinkMode: initBlink, idleAnim: initIdle, action: initAction, lighting: initLight, fixedIdleTime,
      dark: darkQuery.matches, turntable: initTurn, spritePreview: initSprite, silhouette: initSil, retroFps: initRetro,
      cam: {
        yaw: pYaw !== null ? pYaw : (useSavedCam ? wrapAngleDeg(savedView.yaw) : preset.yaw),
        pitch: pPitch !== null ? pPitch : (useSavedCam ? clamp(savedView.pitch, -10, 85) : preset.pitch),
        zoom: useSavedCam ? clamp(savedView.zoom, 0.35, 3.2) : 1.0,
        projection: initProj,
      },
      camTarget: null, dirty: true, spriteDirty: true, lastSpriteTime: 0, loadSeq: 0, ready: false, error: "",
      modelCache: new Map(), customModel: null, scene: emptyScene(),
    };
    const activeTeam = () => resolveTeam(state.team, state.customColor);
    const actionMeta = () => getAction(state.action);
    const activeClipName = () => actionMeta().clip;
    window.__antViewerGetState = () => ({
      ant: state.ant === "custom" ? "" : state.ant,
      team: state.team, customColor: state.customColor, angle: state.angle, proj: state.cam.projection,
      turntable: state.turntable, idle: state.idleAnim, action: state.action, lighting: state.lighting, sprite: state.spritePreview, silhouette: state.silhouette, retroFps: state.retroFps,
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

    function syncTabLinks() {
      if (!compareTab) return;
      const q = new URLSearchParams();
      if (ANT_CLASSES.includes(state.ant)) q.set("ant", state.ant);
      if (TEAMS[state.team]) q.set("team", state.team);
      if (SHEET_ANGLES.includes(state.angle)) q.set("angle", state.angle);
      else if (state.angle === "custom") q.set("yaw", String(Math.round(state.cam.yaw)));
      if (state.action !== "idle") q.set("action", state.action);
      if (state.lighting !== "studio") q.set("light", state.lighting);
      if (state.silhouette) q.set("silhouette", "1");
      if (state.retroFps) q.set("retrofps", "1");
      const qs = q.toString();
      compareTab.setAttribute("href", qs ? ("compare.html?" + qs) : "compare.html");
    }

    function syncDebugState() {
      window.__antViewer = {
        ready: state.ready, ant: state.ant, team: state.team, customColor: state.customColor, angle: state.angle, blink: state.blinkMode, idle: state.idleAnim, action: state.action, lighting: state.lighting, silhouette: state.silhouette, retroFps: state.retroFps,
        hasBlinkAnim: state.scene.instances.some((inst) => inst.model.blinkInfo && inst.model.blinkInfo.hasBlink),
        idleAnims: state.scene.instances.flatMap((inst) => (inst.model.idleAnims || []).map((a) => a.name)),
        attackAnims: state.scene.instances.flatMap((inst) => (inst.model.attackAnims || []).map((a) => a.name)),
        hitAnims: state.scene.instances.flatMap((inst) => (inst.model.hitAnims || []).map((a) => a.name)),
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
      for (const [attr, val] of [["ant", state.ant], ["team", state.team], ["angle", state.angle], ["action", state.action], ["projection", state.cam.projection]]) {
        toolbarEl.querySelectorAll("button[data-" + attr + "]").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset[attr] === val)));
      }
      if (actionSelect) {
        populateActionSelect(actionSelect, {
          activeAction: state.action,
          activeAnt: state.ant,
          allowUnavailable: false,
          hasClipFn: (act) => {
            if (state.scene.instances.some((inst) => selectActionAnims(inst.model, act.clip).matched)) return true;
            if (act.group === "class" && act.classes && act.classes.length === 1) {
              const cached = state.modelCache.get(act.classes[0]);
              return cached ? selectActionAnims(cached, act.clip).matched : false;
            }
            return false;
          },
        });
        actionSelect.dataset.extended = String(!QUICK_ACTIONS.includes(state.action));
      }
      populateLightingSelect(lightSelect, state.lighting);
      if (colorInput && colorInput.value.toLowerCase() !== state.customColor) colorInput.value = state.customColor;
      if (customSwatch) customSwatch.style.backgroundColor = state.customColor;
      $("btn-turntable").setAttribute("aria-pressed", String(state.turntable));
      $("btn-idle").setAttribute("aria-pressed", String(state.idleAnim));
      $("btn-sprite-scale").setAttribute("aria-pressed", String(state.spritePreview));
      if ($("btn-silhouette")) $("btn-silhouette").setAttribute("aria-pressed", String(state.silhouette));
      if ($("btn-retro-fps")) $("btn-retro-fps").setAttribute("aria-pressed", String(state.retroFps));
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
      syncTabLinks();
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
          transparent: true, showPlinth: false, dark: false, team: state.team, teamColors: teamObj, lighting: state.lighting, blinkMode: state.blinkMode,
          idleAnim: state.idleAnim, clipName: activeClipName(), fixedIdleTime: state.fixedIdleTime, overrideProj: "ortho",
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

    function selectAction(next) {
      const target = validAction(next) || "idle";
      const meta = getAction(target);
      if (meta.group === "class" && meta.classes && meta.classes.length === 1 && ANT_CLASSES.includes(state.ant) && !actionSupportsClass(target, state.ant)) {
        state.action = target;
        selectAnt(meta.classes[0]);
        return;
      }
      if (state.action === target) return;
      state.action = target;
      const multi = state.scene.instances.length > 1;
      const phaseStagger = [0.0, 0.14, 0.28, 0.09, 0.21, 0.34];
      state.scene.instances.forEach((inst, i) => { inst.idleTime = multi ? phaseStagger[i % phaseStagger.length] : 0; });
      updateUiState(); markDirty();
      if (target !== "idle") {
        const anyMatched = state.scene.instances.some((inst) => selectActionAnims(inst.model, meta.clip).matched);
        announce(anyMatched ? `Action set to ${meta.label}.` : `Action set to ${meta.label} (falling back to Idle — 3D ${meta.label} clip coming soon).`);
      } else {
        announce("Action set to Idle.");
      }
    }

    function toggleAction(kind = "attack") { selectAction(state.action === kind ? "idle" : kind); }

    function toggleIdle() {
      state.idleAnim = !state.idleAnim;
      state.fixedIdleTime = null;
      updateUiState(); markDirty();
      announce(actionMeta().label + " animation " + (state.idleAnim ? "playing." : "paused."));
    }

    function toggleSilhouette() {
      state.silhouette = !state.silhouette;
      updateUiState(); markDirty(); announce("Silhouette view " + (state.silhouette ? "enabled." : "disabled."));
    }

    function toggleRetroFps() {
      state.retroFps = !state.retroFps;
      updateUiState(); markDirty();
      if (!state.retroFps) { announce("1998 FPS off (smooth 60 fps playback)."); return; }
      const step = ANT_CLASSES.includes(state.ant) ? stepSecFor(state.action, state.ant) : 0;
      if (step) {
        const fps = (1 / step).toFixed(1).replace(/\.0$/, "");
        announce(`1998 FPS on (${ANT_LABELS[state.ant]} · ${fps} fps, ${Math.round(step * 1000)}ms step).`);
      } else {
        announce(`1998 FPS on (${actionMeta().label} 1998 cadence).`);
      }
    }

    function withQuantizedTiming(fn) {
      if (!state.retroFps || state.fixedIdleTime !== null) return fn();
      const saved = state.scene.instances.map((inst) => {
        const step = stepSecFor(state.action, inst.id);
        const origIdle = inst.idleTime || 0;
        inst.idleTime = Math.floor(origIdle / step) * step;
        let origBlink = null;
        const bl = inst.blink, bi = inst.model.blinkInfo;
        if (bl && bl.active) {
          origBlink = bl.elapsed;
          const pulseDur = bi && bi.anim ? (bi.endTime - bi.startTime) : 0.22;
          const bStep = Math.min(step, Math.max(0.04, pulseDur * 0.5));
          bl.elapsed = Math.floor(origBlink / bStep) * bStep;
        }
        return [inst, origIdle, origBlink];
      });
      try { return fn(); } finally {
        for (const [inst, origIdle, origBlink] of saved) {
          inst.idleTime = origIdle;
          if (origBlink !== null && inst.blink) inst.blink.elapsed = origBlink;
        }
      }
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
      else if (btn.dataset.action) selectAction(btn.dataset.action);
      else if (btn.dataset.projection) { state.cam.projection = btn.dataset.projection === "ortho" ? "ortho" : "persp"; updateUiState(); markDirty(); announce("Projection set to " + (state.cam.projection === "ortho" ? "Orthographic." : "Perspective.")); }
      else if (btn.id === "btn-turntable") { state.turntable = !state.turntable; state.camTarget = null; updateUiState(); markDirty(false); announce("Turntable spin " + (state.turntable ? "enabled." : "paused.")); }
      else if (btn.id === "btn-idle") toggleIdle();
      else if (btn.id === "btn-sprite-scale") { state.spritePreview = !state.spritePreview; updateUiState(); markDirty(true); announce("48px sprite-scale preview " + (state.spritePreview ? "shown." : "hidden.")); }
      else if (btn.id === "btn-silhouette") toggleSilhouette();
      else if (btn.id === "btn-retro-fps") toggleRetroFps();
      else if (btn.id === "btn-blink-hold") toggleBlinkHold();
      else if (btn.id === "btn-blink") triggerBlink();
      else if (btn.id === "btn-export-sheet") exportFiveAngleSheet();
      else if (btn.id === "btn-download-glb") downloadActiveGlb();
      else if (btn.id === "btn-reset-view") { const key = ANGLES[state.angle] ? state.angle : "301"; setAnglePreset(key); announce("View reset to " + ANGLES[key].full + "."); }
      else if (btn.id === "btn-load-glb") fileInput.click();
    });

    if (actionSelect) {
      actionSelect.addEventListener("change", () => selectAction(actionSelect.value));
    }
    const selectLighting = (id) => {
      const next = validLighting(id) || "studio";
      state.lighting = next;
      updateUiState(); markDirty();
      announce("Lighting set to " + getLightingPreset(next).label + ".");
    };
    const cycleLighting = () => selectLighting(LIGHTING_ORDER[(LIGHTING_ORDER.indexOf(state.lighting) + 1) % LIGHTING_ORDER.length]);
    if (lightSelect) {
      lightSelect.addEventListener("change", () => selectLighting(lightSelect.value));
    }

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
      else if (e.key === "w" || e.key === "W") { e.preventDefault(); toggleAction("walk"); return; }
      else if (e.key === "a" || e.key === "A") { e.preventDefault(); toggleAction("attack"); return; }
      else if (e.key === "h" || e.key === "H") { e.preventDefault(); toggleAction("hit"); return; }
      else if (e.key === "b" || e.key === "B") { e.preventDefault(); if (e.shiftKey) toggleBlinkHold(); else triggerBlink(); return; }
      else if (e.key === "i" || e.key === "I") { e.preventDefault(); toggleIdle(); return; }
      else if (e.key === "s" || e.key === "S") { e.preventDefault(); toggleSilhouette(); return; }
      else if (e.key === "f" || e.key === "F") { e.preventDefault(); toggleRetroFps(); return; }
      else if (e.key === "l" || e.key === "L") { e.preventDefault(); cycleLighting(); return; }
      else {
        manual = false;
        const key = (e.key === "r" || e.key === "R") ? (ANGLES[state.angle] ? state.angle : "301")
          : (e.key >= "1" && e.key <= "5") ? SHEET_ANGLES[Number(e.key) - 1]
          : (e.key === "6" || e.key === "0") ? "hero" : null;
        if (!key) return;
        setAnglePreset(key); announce((e.key === "r" || e.key === "R" ? "View reset to " : "Camera set to ") + ANGLES[key].full + ".");
      }
      e.preventDefault();
      if (manual) { state.camTarget = null; if (state.turntable) state.turntable = false; updateUiState(); markDirty(); }
    });

    document.addEventListener("keydown", (e) => {
      if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
      if (document.activeElement === stageCanvas) return;
      if (e.key === "w" || e.key === "W") { e.preventDefault(); toggleAction("walk"); }
      else if (e.key === "a" || e.key === "A") { e.preventDefault(); toggleAction("attack"); }
      else if (e.key === "h" || e.key === "H") { e.preventDefault(); toggleAction("hit"); }
      else if (e.key === "s" || e.key === "S") { e.preventDefault(); toggleSilhouette(); }
      else if (e.key === "f" || e.key === "F") { e.preventDefault(); toggleRetroFps(); }
      else if (e.key === "l" || e.key === "L") { e.preventDefault(); cycleLighting(); }
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
        const clipName = activeClipName();
        for (const inst of state.scene.instances) {
          if (selectActionAnims(inst.model, clipName).anims.length) {
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
        withQuantizedTiming(() => renderer.drawScene(state.scene, state.cam, {
          width: w, height: h, transparent: false, showPlinth: true, dark: state.dark, team: state.team, teamColors: activeTeam(), lighting: state.lighting,
          blinkMode: state.blinkMode, idleAnim: state.idleAnim, clipName: activeClipName(), fixedIdleTime: state.fixedIdleTime, silhouette: state.silhouette,
        }));
        state.dirty = false;
      }
      if (state.spriteDirty && state.spritePreview) {
        state.spriteDirty = false; state.lastSpriteTime = now;
        spriteCtx.clearRect(0, 0, spriteCanvas.width, spriteCanvas.height);
        if (state.scene.instances.length) {
          const spriteCam = { yaw: state.cam.yaw, pitch: state.cam.pitch, zoom: 1.0, projection: "ortho" };
          spriteCtx.putImageData(withQuantizedTiming(() => renderer.renderOffscreenPixels(spriteCanvas.width, spriteCanvas.height, state.scene, spriteCam, {
            transparent: true, showPlinth: false, dark: state.dark, team: state.team, teamColors: activeTeam(), lighting: state.lighting,
            blinkMode: state.blinkMode, idleAnim: state.idleAnim, clipName: activeClipName(), fixedIdleTime: state.fixedIdleTime, silhouette: state.silhouette, overrideProj: "ortho",
          })), 0, 0);
        }
      }
      requestAnimationFrame(frame);
    }

    updateUiState(); loadManifest(); selectAnt(state.ant); requestAnimationFrame(frame);
  }

  document.addEventListener("DOMContentLoaded", initViewer);
})();

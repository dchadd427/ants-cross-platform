/* Shared canonical 1998 Ants animation registry (22 shared + class-specific actions). */
"use strict";

(() => {
  const QUICK_ACTIONS = ["idle", "walk", "attack", "hit"];
  const DEFAULT_STEP_SEC = { worker: 0.10, combat: 0.10, thief: 0.10, bomber: 0.10, swimmer: 0.10, fire: 0.10 };
  const CLASS_LABELS = {
    worker: "Worker",
    combat: "Combat",
    thief: "Thief",
    bomber: "Bomber",
    swimmer: "Swimmer",
    fire: "Fire",
  };
  const NON_SWIMMER = ["worker", "combat", "thief", "bomber", "fire"];

  const ACTIONS = [
    {
      id: "idle", clip: "Idle", label: "Idle", group: "shared", coreKey: "sprites",
      groundFrac: 0.87, tallFrac: 0.72,
      stepSec: { worker: 0.15, combat: 0.125, thief: 0.10, bomber: 0.10, swimmer: 0.15, fire: 0.10 },
    },
    {
      id: "walk", clip: "Walk", label: "Walk", group: "shared", extra: true,
      groundFrac: 0.85, tallFrac: 0.71,
      stepSec: { worker: 0.05, combat: 0.06, thief: 0.05, bomber: 0.05, swimmer: 0.06, fire: 0.05 },
    },
    {
      id: "attack", clip: "Attack", label: "Attack", group: "shared", coreKey: "attack_sprites",
      groundFrac: 0.78, tallFrac: 0.68,
      stepSec: { worker: 0.07, combat: 0.09, thief: 0.06, bomber: 0.06, swimmer: 0.076, fire: 0.06 },
    },
    {
      id: "hit", clip: "Hit", label: "Hit", group: "shared", coreKey: "hit_sprites",
      groundFrac: 0.80, tallFrac: 0.69,
      stepSec: { worker: 0.089, combat: 0.098, thief: 0.106, bomber: 0.111, swimmer: 0.117, fire: 0.10 },
    },
    { id: "harvest", clip: "Harvest", label: "Harvest", group: "shared", extra: true, groundFrac: 0.82, tallFrac: 0.70 },
    { id: "cantgo", clip: "CantGo", label: "Can't Go", group: "shared", extra: true, groundFrac: 0.84, tallFrac: 0.71, angleHint: "1998: 301 S only" },
    { id: "blownback", clip: "BlownBack", label: "Blown Back", group: "shared", extra: true, groundFrac: 0.78, tallFrac: 0.68 },
    { id: "blownup", clip: "BlownUp", label: "Blown Up", group: "shared", extra: true, groundFrac: 0.76, tallFrac: 0.67, angleHint: "1998: 301 S only" },
    { id: "stunned", clip: "Stunned", label: "Stunned", group: "shared", extra: true, groundFrac: 0.83, tallFrac: 0.70, angleHint: "1998: 301 S only" },
    { id: "drown", clip: "Drown", label: "Drown", group: "shared", extra: true, classes: NON_SWIMMER, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: 301 S only" },
    { id: "enter", clip: "Enter", label: "Enter", group: "shared", extra: true, groundFrac: 0.82, tallFrac: 0.70, angleHint: "1998: 301 S only" },
    { id: "hatch", clip: "Hatch", label: "Hatch", group: "shared", extra: true, groundFrac: 0.82, tallFrac: 0.70, angleHint: "1998: 301 S only" },
    { id: "steal", clip: "Steal", label: "Steal", group: "class", classes: ["thief"], extra: true, groundFrac: 0.78, tallFrac: 0.68, angleHint: "1998: 501 only" },
    { id: "plantbomb", clip: "PlantBomb", label: "Plant Bomb", group: "class", classes: ["bomber"], extra: true, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: S/E/N only" },
    { id: "defusebomb", clip: "DefuseBomb", label: "Defuse Bomb", group: "class", classes: ["bomber"], extra: true, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: S/E/N only" },
    { id: "swim", clip: "Swim", label: "Swim", group: "class", classes: ["swimmer"], extra: true, groundFrac: 0.82, tallFrac: 0.70 },
    { id: "dive", clip: "Dive", label: "Dive", group: "class", classes: ["swimmer"], extra: true, groundFrac: 0.80, tallFrac: 0.69 },
    { id: "surface", clip: "Surface", label: "Surface", group: "class", classes: ["swimmer"], extra: true, groundFrac: 0.80, tallFrac: 0.69 },
    { id: "shovel", clip: "Shovel", label: "Shovel (Land)", group: "class", classes: ["swimmer"], extra: true, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: S/E/N only" },
    { id: "shovelwater", clip: "ShovelWater", label: "Shovel (Water)", group: "class", classes: ["swimmer"], extra: true, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: S/E/N only" },
    { id: "startfire", clip: "StartFire", label: "Start Fire", group: "class", classes: ["fire"], extra: true, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: S/E/N only" },
    { id: "extinguish", clip: "Extinguish", label: "Extinguish", group: "class", classes: ["fire"], extra: true, groundFrac: 0.80, tallFrac: 0.69, angleHint: "1998: S/E/N only" },
  ];

  const BY_ID = Object.fromEntries(ACTIONS.map((a) => [a.id, a]));
  const ANGLE_RANK = { "301": 0, "201": 1, "901": 2, "801": 3, "701": 4 };

  function validAction(raw) {
    if (typeof raw !== "string") return null;
    const k = raw.trim().toLowerCase();
    return Object.prototype.hasOwnProperty.call(BY_ID, k) ? k : null;
  }

  function getAction(id) {
    return BY_ID[validAction(id) || "idle"] || BY_ID.idle;
  }

  function actionSupportsClass(actionId, cls) {
    const a = getAction(actionId);
    if (!a.classes) return true;
    return a.classes.includes(cls);
  }

  function stepSecFor(actionId, cls) {
    const a = getAction(actionId);
    return (a.stepSec && a.stepSec[cls]) || DEFAULT_STEP_SEC[cls] || 0.10;
  }

  function nearestAvailableAngle(desiredAngle, availableAngles, fallbackHint) {
    if (!Array.isArray(availableAngles) || !availableAngles.length) {
      return { angle: desiredAngle, exact: true, hint: "" };
    }
    if (availableAngles.includes(desiredAngle)) {
      return { angle: desiredAngle, exact: true, hint: "" };
    }
    const want = ANGLE_RANK[desiredAngle] !== undefined ? ANGLE_RANK[desiredAngle] : 0;
    let best = availableAngles[0], bestDist = Infinity;
    for (const cand of availableAngles) {
      const r = ANGLE_RANK[cand] !== undefined ? ANGLE_RANK[cand] : 0;
      const d = Math.abs(r - want);
      if (d < bestDist) { bestDist = d; best = cand; }
    }
    const hint = fallbackHint || (availableAngles.length === 1 ? "1998: 301 S only" : "1998: S/E/N only");
    return { angle: best, exact: false, hint };
  }

  function populateActionSelect(selectEl, opts) {
    if (!selectEl) return;
    const { activeAction, activeAnt, hasClipFn, allowUnavailable } = opts;
    const isSingle = Boolean(activeAnt && CLASS_LABELS[activeAnt]);
    const prevValue = validAction(activeAction) || "idle";

    selectEl.textContent = "";
    const sharedGroup = document.createElement("optgroup");
    sharedGroup.label = "Shared 1998";
    const classGroup = document.createElement("optgroup");
    classGroup.label = "Class Abilities";

    for (const act of ACTIONS) {
      if (isSingle && act.group === "class" && !actionSupportsClass(act.id, activeAnt)) {
        // Still allow switching to another class's ability in single-ant view, labeled clearly with its class.
      }
      const supportedHere = !isSingle || actionSupportsClass(act.id, activeAnt);
      const has3d = typeof hasClipFn === "function" ? hasClipFn(act) : true;
      const opt = document.createElement("option");
      opt.value = act.id;
      let text = act.label;
      if (act.group === "class" && act.classes && act.classes.length === 1 && (!isSingle || !supportedHere)) {
        text += ` (${CLASS_LABELS[act.classes[0]] || act.classes[0]})`;
      } else if (isSingle && !supportedHere) {
        text += " (N/A)";
      }
      if (!has3d && act.id !== "idle") {
        text += allowUnavailable ? " \u2022 1998 preview" : " (coming soon)";
      }
      opt.textContent = text;
      if (!allowUnavailable && !has3d && act.id !== "idle") {
        opt.disabled = true;
      }
      if (act.group === "class") classGroup.appendChild(opt);
      else sharedGroup.appendChild(opt);
    }

    selectEl.appendChild(sharedGroup);
    if (classGroup.children.length) selectEl.appendChild(classGroup);
    selectEl.value = prevValue;
  }

  function initViewerChrome(opts) {
    const { onResize, onLightStep, onControlClick } = opts || {};
    const shell = document.querySelector(".app-shell");
    const panel = document.getElementById("inspector-panel");
    const toggleBtn = document.getElementById("btn-toggle-panel");
    const closeBtn = document.getElementById("btn-close-panel");
    const backdrop = document.getElementById("inspector-backdrop");
    const renderApi = window.__antRender;

    if (!("focusGroup" in HTMLElement.prototype) && renderApi && renderApi.installToolbarRovingTabindex) {
      document.querySelectorAll('[role="toolbar"]').forEach((bar) => renderApi.installToolbarRovingTabindex(bar));
    }

    const mqNarrow = window.matchMedia("(max-width: 960px)");
    const PANEL_KEY = "ants_3d_panel_collapsed_v1";
    let desktopCollapsed = false;
    try { desktopCollapsed = window.localStorage.getItem(PANEL_KEY) === "1"; } catch (_) {}
    let drawerOpen = false;

    function syncPanel() {
      if (!shell || !panel) return;
      const narrow = mqNarrow.matches;
      shell.classList.toggle("panel-collapsed", !narrow && desktopCollapsed);
      shell.classList.toggle("drawer-open", narrow && drawerOpen);
      if (backdrop) backdrop.hidden = !(narrow && drawerOpen);
      const isExpanded = narrow ? drawerOpen : !desktopCollapsed;
      if (toggleBtn) toggleBtn.setAttribute("aria-expanded", String(isExpanded));
      if (typeof onResize === "function") requestAnimationFrame(onResize);
    }

    if (toggleBtn) {
      toggleBtn.addEventListener("click", () => {
        if (mqNarrow.matches) {
          drawerOpen = !drawerOpen;
        } else {
          desktopCollapsed = !desktopCollapsed;
          try { window.localStorage.setItem(PANEL_KEY, desktopCollapsed ? "1" : "0"); } catch (_) {}
        }
        syncPanel();
      });
    }
    const closeDrawer = () => {
      if (!drawerOpen) return;
      drawerOpen = false;
      syncPanel();
      if (toggleBtn) toggleBtn.focus();
    };
    if (closeBtn) closeBtn.addEventListener("click", closeDrawer);
    if (backdrop) backdrop.addEventListener("click", closeDrawer);
    if (typeof mqNarrow.addEventListener === "function") mqNarrow.addEventListener("change", () => { drawerOpen = false; syncPanel(); });
    window.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && mqNarrow.matches && drawerOpen) {
        e.preventDefault();
        closeDrawer();
      }
    });

    const prevLight = document.getElementById("btn-light-prev");
    const nextLight = document.getElementById("btn-light-next");
    if (prevLight && typeof onLightStep === "function") prevLight.addEventListener("click", () => onLightStep(-1));
    if (nextLight && typeof onLightStep === "function") nextLight.addEventListener("click", () => onLightStep(1));

    if (typeof onControlClick === "function") {
      const zones = [
        document.getElementById("compare-toolbar"),
        document.getElementById("viewer-toolbar"),
        document.getElementById("playback-dock"),
        panel,
      ];
      for (const zone of zones) {
        if (!zone) continue;
        zone.addEventListener("click", (e) => {
          const btn = e.target.closest("button");
          if (!btn || !zone.contains(btn)) return;
          if (btn.id === "btn-toggle-panel" || btn.id === "btn-close-panel" || btn.id === "btn-light-prev" || btn.id === "btn-light-next") return;
          onControlClick(btn);
        });
      }
    }

    syncPanel();
  }

  window.__antActions = {
    QUICK_ACTIONS,
    ACTIONS,
    BY_ID,
    validAction,
    getAction,
    actionSupportsClass,
    stepSecFor,
    nearestAvailableAngle,
    populateActionSelect,
    initViewerChrome,
  };
})();

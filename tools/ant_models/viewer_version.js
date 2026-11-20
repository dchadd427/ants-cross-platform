"use strict";

(() => {
  const POLL_MS = 3000;

  function parseManifest(data) {
    if (!data || typeof data !== "object") return null;
    const version = typeof data.version === "string" ? data.version.trim() : "";
    const buildHash = typeof data.build_hash === "string" ? data.build_hash.trim() : "";
    const modelHashes = new Map();
    if (Array.isArray(data.models)) {
      for (const item of data.models) {
        if (item && typeof item.id === "string" && typeof item.sha256 === "string") {
          modelHashes.set(item.id, item.sha256);
        }
      }
    } else if (data.models && typeof data.models === "object") {
      for (const [id, item] of Object.entries(data.models)) {
        if (item && typeof item.sha256 === "string") modelHashes.set(id, item.sha256);
      }
    }
    const sorted = Array.from(modelHashes.entries()).sort((a, b) => a[0].localeCompare(b[0]));
    const sig = [version, buildHash, ...sorted.map(([id, h]) => id + ":" + h)].join("|");
    if (!version && !buildHash && !sorted.length) return null;
    const displayVersion = version || (buildHash ? "v-" + buildHash.slice(0, 8) : "");
    return { version, buildHash, displayVersion, modelHashes, sig };
  }

  function diffUpdatedModels(baseMap, nextMap) {
    const changed = [];
    for (const [id, hash] of nextMap.entries()) {
      if (baseMap.get(id) !== hash) changed.push(id);
    }
    return changed.sort();
  }

  function initVersionWatcher() {
    if (window.location.protocol !== "http:" && window.location.protocol !== "https:") return;

    const badge = document.getElementById("badge-version");
    const dialog = document.getElementById("update-dialog");
    const bodyEl = document.getElementById("update-body");
    const btnRefresh = document.getElementById("btn-refresh-now");
    const btnLater = document.getElementById("btn-refresh-later");
    const statusEl = document.getElementById("status-region");
    if (!dialog || !bodyEl || !btnRefresh || !btnLater) return;

    const params = new URLSearchParams(window.location.search);
    const pollParam = (params.get("poll") || "").toLowerCase();
    const pollingEnabled = pollParam !== "0" && pollParam !== "false";

    let initial = null;
    let shownSig = "";
    let dismissedSig = "";
    let inFlight = false;

    function publishVersionState(latest) {
      window.__antVersion = {
        version: initial ? initial.displayVersion : "",
        buildHash: initial ? initial.buildHash : "",
        latestVersion: latest ? latest.displayVersion : (initial ? initial.displayVersion : ""),
        updateAvailable: Boolean(initial && latest && latest.sig !== initial.sig),
      };
    }

    async function fetchManifest() {
      const resp = await fetch("models/manifest.json?t=" + Date.now(), { cache: "no-store" });
      if (!resp.ok) return null;
      return parseManifest(await resp.json());
    }

    function showUpdatePrompt(next) {
      const changed = diffUpdatedModels(initial.modelHashes, next.modelHashes);
      const verText = next.displayVersion ? ("Build " + next.displayVersion + " is ready.") : "A newer build is ready.";
      const detail = changed.length ? (" Updated: " + changed.join(", ") + ".") : " Viewer or build assets changed.";
      bodyEl.textContent = verText + detail;
      shownSig = next.sig;
      if (!dialog.open) {
        if (typeof dialog.showModal === "function") dialog.showModal();
        else dialog.setAttribute("open", "");
      }
      if (statusEl) {
        statusEl.textContent = "New 3D models available" + (next.displayVersion ? " (" + next.displayVersion + ")." : ".");
      }
    }

    function dismissCurrent() {
      dismissedSig = shownSig;
      if (dialog.open) {
        if (typeof dialog.close === "function") dialog.close();
        else dialog.removeAttribute("open");
      }
    }

    btnRefresh.addEventListener("click", () => window.location.reload());
    btnLater.addEventListener("click", dismissCurrent);
    dialog.addEventListener("cancel", (e) => {
      e.preventDefault();
      dismissCurrent();
    });

    async function checkOnce() {
      if (inFlight) return;
      inFlight = true;
      try {
        const next = await fetchManifest();
        if (!next) return;
        if (!initial) {
          initial = next;
          if (badge && next.displayVersion) {
            badge.textContent = next.displayVersion;
            badge.hidden = false;
          }
          publishVersionState(next);
          return;
        }
        publishVersionState(next);
        if (next.sig !== initial.sig && next.sig !== dismissedSig && next.sig !== shownSig) {
          showUpdatePrompt(next);
        }
      } catch (_) {
        // Ignore transient network errors while polling locally
      } finally {
        inFlight = false;
      }
    }

    checkOnce();
    if (!pollingEnabled) return;

    window.setInterval(() => {
      if (document.visibilityState === "visible") checkOnce();
    }, POLL_MS);

    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "visible") checkOnce();
    });
  }

  document.addEventListener("DOMContentLoaded", initVersionWatcher);
})();

"use strict";

(() => {
  const POLL_MS = 3000;
  const RELOAD_DELAY_MS = 300;
  const STORAGE_KEY = "ants_viewer_reload_state_v1";

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

  function saveReloadState(updatedVersion) {
    try {
      const snap = typeof window.__antViewerGetState === "function" ? window.__antViewerGetState() : {};
      const payload = { ...snap, page: window.location.pathname, updatedVersion: updatedVersion || "" };
      window.sessionStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
    } catch (_) {
      // Ignore storage errors in restricted browsing modes
    }
  }

  function isLocalDevHost() {
    const h = (window.location.hostname || "").toLowerCase();
    if (!h || h === "localhost" || h === "127.0.0.1" || h === "::1" || h === "[::1]" || h.endsWith(".local")) {
      return true;
    }
    return /^(?:10\.|192\.168\.|172\.(?:1[6-9]|2\d|3[01])\.|100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.)/.test(h);
  }

  function initVersionWatcher() {
    if (window.location.protocol !== "http:" && window.location.protocol !== "https:") return;

    const badge = document.getElementById("badge-version");
    const toast = document.getElementById("update-toast");
    const statusEl = document.getElementById("status-region");

    const params = new URLSearchParams(window.location.search);
    const pollParam = (params.get("poll") || "").toLowerCase();
    const forcePoll = pollParam === "1" || pollParam === "true";
    const pollingEnabled = (isLocalDevHost() || forcePoll) && pollParam !== "0" && pollParam !== "false";

    let initial = null;
    let inFlight = false;
    let reloading = false;

    function publishVersionState(latest) {
      window.__antVersion = {
        version: initial ? initial.displayVersion : "",
        buildHash: initial ? initial.buildHash : "",
        latestVersion: latest ? latest.displayVersion : (initial ? initial.displayVersion : ""),
        updateAvailable: Boolean(initial && latest && latest.sig !== initial.sig),
        reloading,
      };
    }

    async function fetchManifest() {
      const url = pollingEnabled ? ("models/manifest.json?t=" + Date.now()) : "models/manifest.json";
      const resp = await fetch(url, { cache: pollingEnabled ? "no-store" : "no-cache" });
      if (!resp.ok) return null;
      return parseManifest(await resp.json());
    }

    function triggerAutoReload(next) {
      if (reloading) return;
      reloading = true;
      publishVersionState(next);
      saveReloadState(next.displayVersion);
      const msg = next.displayVersion ? ("Updating to " + next.displayVersion + "…") : "Updating 3D models…";
      if (toast) {
        toast.textContent = msg;
        toast.hidden = false;
      }
      if (statusEl) statusEl.textContent = msg;
      window.setTimeout(() => window.location.reload(), RELOAD_DELAY_MS);
    }

    async function checkOnce() {
      if (inFlight || reloading) return;
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
        if (next.sig !== initial.sig) triggerAutoReload(next);
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

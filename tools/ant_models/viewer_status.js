/* Live Build Status Pill + Native <dialog> Drawer for 3D Studio & Compare 1998. */
(function () {
  "use strict";

  const POLL_MS = 3000;
  const STATUS_URL = "models/status.json";

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = String(text);
    return node;
  }

  function isLocalDevHost() {
    const h = (window.location.hostname || "").toLowerCase();
    if (!h || h === "localhost" || h === "127.0.0.1" || h === "::1" || h === "[::1]" || h.endsWith(".local")) {
      return true;
    }
    return /^(?:10\.|192\.168\.|172\.(?:1[6-9]|2\d|3[01])\.|100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.)/.test(h);
  }

  function initStatusPanel() {
    const btn = document.getElementById("btn-status");
    const labelEl = document.getElementById("status-btn-label");
    const dialog = document.getElementById("status-dialog");
    if (!btn || !dialog) return;

    const params = new URLSearchParams(window.location.search);
    const pollParam = (params.get("poll") || "").toLowerCase();
    const forcePoll = pollParam === "1" || pollParam === "true";
    const localHost = isLocalDevHost();
    const pollEnabled = (localHost || forcePoll) && pollParam !== "0" && pollParam !== "false" && window.location.protocol !== "file:";
    const canFetch = (localHost || forcePoll) && window.location.protocol !== "file:";

    let timerId = null;
    let lastRaw = "";
    let latestData = null;
    let available = false;
    let loaded = false;
    let missing = !canFetch;
    btn.hidden = true;

    function syncDebug() {
      const inProg = latestData && Array.isArray(latestData.in_progress) ? latestData.in_progress.length : 0;
      const queued = latestData && Array.isArray(latestData.queued) ? latestData.queued.length : 0;
      const recent = latestData && Array.isArray(latestData.recently_live) ? latestData.recently_live.length : 0;
      window.__antStatus = {
        loaded,
        available,
        open: Boolean(dialog.open),
        inProgressCount: inProg,
        queuedCount: queued,
        recentCount: recent,
        updatedAt: (latestData && latestData.updated_at) || "",
        liveVersion: (latestData && latestData.live_version) || "",
      };
    }

    function updateButton() {
      const count = latestData && Array.isArray(latestData.in_progress) ? latestData.in_progress.length : 0;
      const busy = available && count > 0;
      btn.dataset.busy = busy ? "true" : "false";
      if (labelEl) {
        labelEl.textContent = busy ? `Status \u00b7 ${count}` : "Status";
      }
      btn.setAttribute(
        "aria-label",
        busy ? `Build status: ${count} in progress` : "Open live build status"
      );
      syncDebug();
    }

    function buildSection(title, items, emptyText, kind) {
      const section = el("section", "status-section");
      const head = el("div", "status-section-head");
      head.appendChild(el("h3", "status-section-title", title));
      head.appendChild(el("span", "status-count-pill", items.length));
      section.appendChild(head);

      if (!items.length) {
        section.appendChild(el("p", "status-empty", emptyText));
        return section;
      }

      const list = el("ul", "status-list");
      for (const item of items) {
        if (!item || typeof item !== "object") continue;
        const li = el("li", `status-item status-item-${kind}`);
        const row = el("div", "status-item-head");
        if (kind === "recent") {
          row.appendChild(el("span", "status-tag status-tag-ver", item.version || "live"));
        } else {
          row.appendChild(el("span", "status-item-target", item.target || "Task"));
          if (kind === "active" && item.worker) {
            row.appendChild(el("span", "status-tag", item.worker));
          }
        }
        li.appendChild(row);
        if (item.summary) {
          li.appendChild(el("p", "status-item-summary", item.summary));
        }
        list.appendChild(li);
      }
      section.appendChild(list);
      return section;
    }

    function renderDialog() {
      dialog.textContent = "";
      const inner = el("div", "status-dialog-inner");

      const header = el("header", "status-dialog-head");
      const titleWrap = el("div", "status-dialog-title-wrap");
      const h2 = el("h2", "status-dialog-title", "Live Build Status");
      h2.id = "status-dialog-title";
      titleWrap.appendChild(h2);

      const meta = el("div", "status-dialog-meta");
      if (available && latestData) {
        if (latestData.live_version) {
          meta.appendChild(el("span", "status-tag status-tag-ver", latestData.live_version));
        }
        if (latestData.updated_at) {
          meta.appendChild(el("span", "status-updated", "Updated " + latestData.updated_at));
        }
      } else {
        meta.appendChild(el("span", "status-updated", loaded ? "Static snapshot" : "Checking status\u2026"));
      }
      titleWrap.appendChild(meta);
      header.appendChild(titleWrap);

      const closeBtn = el("button", "status-close-btn", "Close");
      closeBtn.type = "button";
      closeBtn.setAttribute("aria-label", "Close build status dialog");
      closeBtn.addEventListener("click", () => { dialog.close(); syncDebug(); });
      header.appendChild(closeBtn);
      inner.appendChild(header);

      const body = el("div", "status-dialog-body");
      if (!available) {
        body.appendChild(el(
          "p",
          "status-empty",
          loaded
            ? "Live build status feed (models/status.json) is not present in this static build."
            : "Loading live build status\u2026"
        ));
      } else {
        const inProgress = Array.isArray(latestData.in_progress) ? latestData.in_progress : [];
        const queued = Array.isArray(latestData.queued) ? latestData.queued : [];
        const recent = Array.isArray(latestData.recently_live) ? latestData.recently_live : [];
        body.appendChild(buildSection("Working On Now", inProgress, "All workers idle \u2014 nothing currently in progress.", "active"));
        body.appendChild(buildSection("Waiting / Queued", queued, "Nothing queued right now.", "queued"));
        body.appendChild(buildSection("Recently Published Live", recent, "No recent live publishes recorded yet.", "recent"));
      }
      inner.appendChild(body);
      dialog.appendChild(inner);
    }

    async function fetchStatus() {
      if (!canFetch || missing) return;
      try {
        const res = await fetch(STATUS_URL, { cache: "no-store" });
        if (res.status === 404) {
          missing = true;
          available = false;
          loaded = true;
          btn.hidden = true;
          stopPolling();
          updateButton();
          if (dialog.open) renderDialog();
          return;
        }
        if (!res.ok) {
          loaded = true;
          syncDebug();
          return;
        }
        const raw = await res.text();
        loaded = true;
        if (raw === lastRaw && available) {
          syncDebug();
          return;
        }
        const parsed = JSON.parse(raw);
        if (!parsed || typeof parsed !== "object") return;
        lastRaw = raw;
        latestData = parsed;
        available = true;
        btn.hidden = false;
        updateButton();
        if (dialog.open) renderDialog();
      } catch (_) {
        loaded = true;
        syncDebug();
      }
    }

    function stopPolling() {
      if (timerId !== null) {
        window.clearInterval(timerId);
        timerId = null;
      }
    }

    function startPolling() {
      if (!pollEnabled || missing || timerId !== null) return;
      timerId = window.setInterval(() => {
        if (document.visibilityState !== "hidden") fetchStatus();
      }, POLL_MS);
    }

    btn.addEventListener("click", () => {
      if (dialog.open) {
        dialog.close();
        return;
      }
      renderDialog();
      if (typeof dialog.showModal === "function") dialog.showModal();
      else dialog.setAttribute("open", "");
      syncDebug();
      if (!missing) fetchStatus();
    });

    if (!("closedBy" in HTMLDialogElement.prototype)) {
      dialog.addEventListener("click", (e) => {
        if (e.target === dialog) { dialog.close(); syncDebug(); }
      });
    }

    dialog.addEventListener("close", () => {
      syncDebug();
    });

    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "visible" && pollEnabled && !missing) fetchStatus();
    });

    updateButton();
    if (canFetch) {
      fetchStatus();
      startPolling();
    } else {
      loaded = true;
      syncDebug();
    }
  }

  document.addEventListener("DOMContentLoaded", initStatusPanel);
})();

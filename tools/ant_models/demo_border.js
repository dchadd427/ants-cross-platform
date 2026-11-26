"use strict";

// Lush 3D backyard grass perimeter wrapping all 4 outer edges & corners of every demo map
// so the boundary blends seamlessly into the surrounding grassy lawn with zero harsh black drop-off.
(() => {
  const BORDER_GRASS_IDS = ["grassbig1", "grassmed1", "grassmed2", "grass3", "grass4"];

  function hash01(seed) {
    let s = (seed >>> 0) + 0x6D2B79F5;
    s = Math.imul(s ^ (s >>> 15), 1 | s);
    s = (s + Math.imul(s ^ (s >>> 7), 61 | s)) ^ s;
    return ((s ^ (s >>> 14)) >>> 0) / 4294967296;
  }

  function isEdgeWater(mapData, waterTile, x, z) {
    if (!mapData || !mapData.tiles || waterTile === undefined) return false;
    const c = Math.max(0, Math.min(mapData.cols - 1, Math.floor(x)));
    const r = Math.max(0, Math.min(mapData.rows - 1, Math.floor(z)));
    return mapData.tiles[r * mapData.cols + c] === waterTile;
  }

  // Build deterministic, organically staggered 3D grass tufts around all 4 edges + outer lawn bands.
  // Keeps inner tufts cleanly outside playable tiles (extra south lip clearance for 39° Game View).
  function appendBorderGrass(itemEntries, mapData, grassModels, waterTile, buildWorldMat) {
    const cols = mapData.cols, rows = mapData.rows;
    const big = grassModels.grassbig1;
    const med1 = grassModels.grassmed1 || big;
    const med2 = grassModels.grassmed2 || big;
    const tuft3 = grassModels.grass3 || big;
    const tuft4 = grassModels.grass4 || tuft3;
    if (!big) return;

    let seed = (cols * 73856093) ^ (rows * 19349663) ^ 0x51A7F3;
    const nextR = () => hash01(++seed);

    function addClump(id, model, x, z, scale, yawRad) {
      if (!model) return;
      const radius = 0.65 * Math.max(model.size[0], model.size[2], 1.0) * scale;
      const inst = {
        kind: "item",
        id,
        model,
        clipName: "Idle",
        idleTime: nextR() * 2.2,
        offsetMat: buildWorldMat(model, x, z, scale, yawRad),
      };
      itemEntries.push({ inst, spec: { id, x, z, s: scale, border: true }, x, z, radius });
    }

    // 3 concentric staggered organic grass bands outside all 4 edges:
    // Band 0: tight lush hedge hugging the edge (zero flat gap); Band 1: full mid-lawn thicket; Band 2: outer meadow clumps
    const bands = [
      { offN: 1.15, offS: 1.75, offW: 1.25, offE: 1.25, step: 2.15, jitterAlong: 0.45, jitterOut: 0.28, sMin: 4.1, sMax: 5.2, cornerPad: 1.6 },
      { offN: 3.70, offS: 4.35, offW: 3.90, offE: 3.90, step: 3.30, jitterAlong: 0.78, jitterOut: 0.58, sMin: 5.1, sMax: 6.6, cornerPad: 4.5 },
      { offN: 7.80, offS: 8.40, offW: 8.10, offE: 8.10, step: 4.80, jitterAlong: 1.10, jitterOut: 0.90, sMin: 6.0, sMax: 7.6, cornerPad: 8.8 },
    ];

    const pickModel = (bandIdx, isHoriz) => {
      const r = nextR();
      if (bandIdx === 0) {
        if (r < 0.42) return ["grassbig1", big, 1.0];
        if (r < 0.72) return isHoriz ? ["grassmed1", med1, 0.96] : ["grassmed2", med2, 0.76];
        return r < 0.86 ? ["grass3", tuft3, 0.66] : ["grass4", tuft4, 0.68];
      }
      if (bandIdx === 1) {
        if (r < 0.68) return ["grassbig1", big, 1.0];
        return isHoriz ? ["grassmed1", med1, 0.92] : ["grassmed2", med2, 0.74];
      }
      return ["grassbig1", big, 1.0];
    };

    for (let b = 0; b < bands.length; b++) {
      const cfg = bands[b];
      const pad = cfg.cornerPad;

      // North & South edges (spanning past corners to wrap cleanly)
      for (let t = -pad; t <= cols + pad; t += cfg.step) {
        for (const isSouth of [false, true]) {
          const jA = (nextR() - 0.5) * 2 * cfg.jitterAlong;
          const jO = (nextR() - 0.5) * 2 * cfg.jitterOut;
          const x = t + jA;
          const waterPush = (b === 0 && isEdgeWater(mapData, waterTile, x, isSouth ? rows - 1 : 0)) ? 0.55 : 0.0;
          const z = isSouth ? (rows + cfg.offS + jO + waterPush) : (-cfg.offN - jO - waterPush);
          const [id, model, sMul] = pickModel(b, true);
          const sc = (cfg.sMin + nextR() * (cfg.sMax - cfg.sMin)) * sMul;
          const yaw = (id === "grassmed1") ? ((nextR() - 0.5) * 0.45 + (nextR() > 0.5 ? Math.PI : 0)) : (nextR() * Math.PI * 2);
          addClump(id, model, x, z, sc, yaw);
        }
      }

      // West & East edges (filling between North & South inner spans so corners aren't over-stacked)
      for (let t = 0.6; t <= rows - 0.6; t += cfg.step) {
        for (const isEast of [false, true]) {
          const jA = (nextR() - 0.5) * 2 * cfg.jitterAlong;
          const jO = (nextR() - 0.5) * 2 * cfg.jitterOut;
          const z = t + jA;
          const waterPush = (b === 0 && isEdgeWater(mapData, waterTile, isEast ? cols - 1 : 0, z)) ? 0.55 : 0.0;
          const x = isEast ? (cols + cfg.offE + jO + waterPush) : (-cfg.offW - jO - waterPush);
          const [id, model, sMul] = pickModel(b, false);
          const sc = (cfg.sMin + nextR() * (cfg.sMax - cfg.sMin)) * sMul;
          const yaw = (id === "grassmed2") ? ((nextR() - 0.5) * 0.45 + (nextR() > 0.5 ? Math.PI : 0)) : (nextR() * Math.PI * 2);
          addClump(id, model, x, z, sc, yaw);
        }
      }
    }
  }

  window.__demoBorder = {
    BORDER_GRASS_IDS,
    appendBorderGrass,
  };
})();

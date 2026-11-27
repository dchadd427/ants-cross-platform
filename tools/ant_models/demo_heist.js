"use strict";

// Synchronized 1998 Thief Base-Heist (bottle-cap hatch flip -> head-first dive -> lunchbox escape)
// and Worker/Thief 3D lunchbox carrying (`356:lunchbox`, `1095:atcr501`, `hgwg301`).
(() => {
  const { mat4FromTRS } = window.__antGltf;
  const { TEAMS } = window.__antRender;

  const HILL_ID_BY_TEAM = {
    green: "GREENHILL",
    red: "REDHILL",
    blue: "BLUEHILL",
    black: "BLACKHILL",
  };
  const RAID_PAIRS = [
    { thiefTeam: "black", victimTeam: "green", phase: 1.4 },
    { thiefTeam: "red",   victimTeam: "blue",  phase: 6.2 },
  ];
  const CYCLE_DUR = 10.2;
  const LUNCHBOX_SCALE = 0.53;

  function quatMul(a, b) {
    return [
      a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
      a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
      a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
      a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ];
  }

  function quatAxis(ax, ay, az, rad) {
    const h = rad * 0.5, s = Math.sin(h);
    return [ax * s, ay * s, az * s, Math.cos(h)];
  }

  function quatYawPitch(yaw, pitch) {
    return quatMul(quatAxis(0, 1, 0, yaw), quatAxis(1, 0, 0, pitch));
  }

  function buildAntPoseMat(model, x, y, z, scale, yawRad, pitchRad = 0) {
    const minY = model && model.bounds && model.bounds.min ? model.bounds.min[1] : 0;
    const q = quatYawPitch(yawRad, pitchRad);
    return mat4FromTRS([x, -minY * scale + y, z], q, [scale, scale, scale]);
  }

  // Authentic 1998 carried lunchbox (`hgwg301`/`htwg301`):
  // - Rotated +114° (canceling -24° bake + 90° turn) so the handle runs front-to-back along the ant's side
  //   with the brass clasp facing outward and the flat broad side parallel to the hip.
  // - Pivoted cleanly around the top handle grip (`0.685 * sc`), locked to the left hand's `Walk` arm swing (0.60 s cycle)
  //   with lively pendulum swing forward & backward!
  function buildCarriedLunchboxMat(lunchboxModel, x, y, z, yawRad, animTime, isWalking, antScale, heldFront = false) {
    const sc = LUNCHBOX_SCALE * (antScale / 1.15);
    const handleGripY = 0.685 * sc;
    const cy = Math.cos(yawRad), sy = Math.sin(yawRad);

    // Exact phase match with `shared_clips_loco.py` Walk clip (0.60 s period): s1 > 0 lifts/swings left arm forward (+Y/+Z).
    const phi = (animTime / 0.60) * Math.PI * 2;
    const s1 = isWalking ? Math.sin(phi) : 0;
    const sLag = isWalking ? Math.sin(phi - 0.25) : 0;

    // Ant-local left paw trajectory measured directly from `worker.glb`/`thief.glb` Walk `forearm_left` tip
    // so the top handle stays firmly gripped in the ant's left hand at every frame of the stride!
    const handX = heldFront ? 0.0 : ((0.136 + 0.014 * s1) * antScale);
    const handY = heldFront ? (0.56 * antScale) : ((0.382 + 0.058 * s1 + 0.008 * s1 * s1) * antScale);
    const handZ = heldFront ? (0.30 * antScale) : ((0.180 + 0.016 * s1) * antScale);

    // Subtle, natural weighted pendulum swing (~±14°) around the gripped handle in sync with the arm!
    const swingRx = heldFront ? -0.14 : (-0.25 * sLag);
    // Positive Rz tilts the bottom gently outward (+X) so the box clears the hip/legs while handle stays in the paw!
    const outRollRz = heldFront ? 0.0 : (0.14 + 0.02 * Math.abs(s1));
    // +114° turns `lunchbox.glb` (baked at -24°) to +90° so handle runs front-to-back parallel to the ant's side!
    const boxYawLocal = heldFront ? (24 * Math.PI / 180) : (114 * Math.PI / 180);

    // Compose: worldYaw * antSagittalPitch * slightOutwardRoll * boxSideYaw
    const qSwing = quatMul(quatAxis(1, 0, 0, swingRx), quatAxis(0, 0, 1, outRollRz));
    const qAntToWorld = quatMul(quatAxis(0, 1, 0, yawRad), qSwing);
    const qTotal = quatMul(qAntToWorld, quatAxis(0, 1, 0, boxYawLocal));

    // Rotate handle vector (0, handleGripY, 0) by qSwing into ant-local space so handle stays glued in the left paw!
    const cR = Math.cos(outRollRz), sR = Math.sin(outRollRz);
    const cP = Math.cos(swingRx), sP = Math.sin(swingRx);
    const gxLoc = -handleGripY * sR;
    const gyLoc = handleGripY * cR * cP;
    const gzLoc = handleGripY * cR * sP;

    const bxLoc = handX - gxLoc;
    const byLoc = handY - gyLoc;
    const bzLoc = handZ - gzLoc;

    const wx = x + bxLoc * cy + bzLoc * sy;
    const wz = z - bxLoc * sy + bzLoc * cy;
    const wy = Math.max(0.015, y + byLoc);
    return mat4FromTRS([wx, wy, wz], qTotal, [sc, sc, sc]);
  }

  function findHillHoleWorld(hillEntry) {
    const m = hillEntry.inst.model;
    const s = hillEntry.spec.s;
    let rx = 0, ry = 0, rz = 0;
    let hx = 0.33, hy = 0.18, hz = 0.08;
    if (m && Array.isArray(m.nodes)) {
      const rootN = m.nodes.find((n) => n && n.name === "anthill");
      const holeN = m.nodes.find((n) => n && typeof n.name === "string" && n.name.endsWith("_hatch_hole"));
      if (rootN && rootN.translation) [rx, ry, rz] = rootN.translation;
      if (holeN && holeN.translation) [hx, hy, hz] = holeN.translation;
    }
    const minY = m && m.bounds && m.bounds.min ? m.bounds.min[1] : 0;
    return [
      hillEntry.x + (rx + hx) * s,
      (ry + hy - minY) * s,
      hillEntry.z + (rz + hz) * s,
    ];
  }

  function snapYaw8(rad) {
    const step = Math.PI * 0.25;
    return Math.round(rad / step) * step;
  }

  function lerp(a, b, u) { return a + (b - a) * u; }
  function smooth(u) { const c = Math.max(0, Math.min(1, u)); return c * c * (3 - 2 * c); }

  function buildHeistsForMap(mapData, itemEntries, thiefModel, lunchboxModel, antScale) {
    if (!thiefModel || !lunchboxModel) return [];
    const heists = [];
    const pairs = mapData.id === "treasure" ? RAID_PAIRS : [RAID_PAIRS[0]];
    for (let i = 0; i < pairs.length; i++) {
      const { thiefTeam, victimTeam, phase } = pairs[i];
      const hid = HILL_ID_BY_TEAM[victimTeam];
      const hillEntry = itemEntries.find((e) => e.spec.id === hid);
      if (!hillEntry) continue;
      const hole = findHillHoleWorld(hillEntry);
      // Stand spot just outside the right-front slope of the bottle cap; escape waypoint 3.2 tiles SE along 8-way leg!
      const stand = [
        Math.min(mapData.cols - 1.5, hole[0] + 0.78),
        Math.min(mapData.rows - 1.5, hole[2] + 0.48),
      ];
      const homeHill = mapData.hills && mapData.hills[thiefTeam];
      const dirX = homeHill && homeHill[0] < hillEntry.x ? -1 : 1;
      const dirZ = homeHill && homeHill[1] > hillEntry.z ? 1 : -1;
      // 8-way getaway staging point cleanly off the hill skirt!
      const away = [
        Math.max(1.5, Math.min(mapData.cols - 1.5, stand[0] + (dirX < 0 ? 1.8 : 2.2))),
        Math.max(1.5, Math.min(mapData.rows - 1.5, stand[1] + dirZ * 2.4)),
      ];
      const stash = [
        Math.max(1.5, Math.min(mapData.cols - 1.5, away[0] + dirX * 2.2)),
        away[1],
      ];
      // Deep interior target inside the anthill mound behind the dark circular tunnel hole!
      const inside = [
        lerp(hole[0], hillEntry.x, 0.45),
        Math.max(-0.25, hole[1] - 0.58),
        lerp(hole[2], hillEntry.z, 0.45),
      ];
      const inst = {
        kind: "ant",
        cls: "thief",
        submerged: false,
        teamColors: TEAMS[thiefTeam] || TEAMS.black,
        model: thiefModel,
        clipName: "Walk",
        idleTime: 0,
        animTime: undefined,
        blink: { active: false, elapsed: 0, nextIn: 2.2 + i },
        offsetMat: buildAntPoseMat(thiefModel, stand[0], 0, stand[1], antScale, 0),
      };
      const lootInst = {
        kind: "item",
        id: "lunchbox",
        model: lunchboxModel,
        clipName: "Idle",
        idleTime: 0,
        offsetMat: buildCarriedLunchboxMat(lunchboxModel, stand[0], 0, stand[1], 0, 0, false, antScale, false),
      };
      heists.push({
        thiefTeam, victimTeam, phase, hillEntry, hole, stand, away, stash, inside,
        inst, lootInst, antScale, lunchboxModel,
      });
    }
    return heists;
  }

  function updateHeists(heists, t, patrolsOn, tx, tz, visHalfSpan, visible) {
    for (let i = 0; i < heists.length; i++) {
      const h = heists[i];
      const { hillEntry, hole, stand, away, stash, inside, inst, lootInst, antScale, lunchboxModel } = h;
      const lt = patrolsOn ? ((t + h.phase) % CYCLE_DUR + CYCLE_DUR) % CYCLE_DUR : 3.92;
      const yawToHole = Math.atan2(hole[0] - stand[0], hole[2] - stand[1]);

      // Reset hill to default Idle unless we are inside the active 3.60s bottle-cap Raid window (1.60 .. 5.20 s)!
      let showAnt = true;
      let showLoot = false;
      let lootFront = false;
      let x = stand[0], y = 0, z = stand[1], yaw = yawToHole, pitch = 0, walkDist = 0, walking = false;

      if (lt < 1.60) {
        // Phase 1: Sneak up to the enemy anthill's bottle cap along an 8-way leg!
        const u = smooth(lt / 1.60);
        x = lerp(away[0], stand[0], u);
        z = lerp(away[1], stand[1], u);
        yaw = snapYaw8(Math.atan2(stand[0] - away[0], stand[1] - away[1]));
        walkDist = u * Math.hypot(stand[0] - away[0], stand[1] - away[1]);
        walking = true;
        y = 0;
        inst.clipName = "Walk";
        inst.animTime = undefined;
        inst.idleTime = walkDist * 0.85;
        hillEntry.inst.clipName = "Idle";
        hillEntry.inst.animTime = undefined;
      } else if (lt < 5.20) {
        // Phases 2..6: Synchronized 3.60s anthill `Raid` bottle-cap hatch open + Thief pry, head-first dive, & lunchbox pop-out!
        const rt = lt - 1.60; // 0.00 .. 3.60 s
        hillEntry.inst.clipName = "Raid";
        hillEntry.inst.animTime = rt;

        if (rt < 1.65) {
          // Lookout & pry bottle cap rim (`Steal` clip synced to cap wiggle at 1.30..1.65s)
          inst.clipName = "Steal";
          inst.animTime = rt * 1.05;
          yaw = yawToHole;
        } else if (rt < 2.02) {
          // Cap swings wide open (126°), Thief hops excitedly ready to dive!
          const u = (rt - 1.65) / 0.37;
          y = Math.sin(u * Math.PI) * 0.24;
          pitch = -0.14 * Math.sin(u * Math.PI);
          inst.clipName = "Steal";
          inst.animTime = 1.98 + u * 0.22;
          yaw = yawToHole;
        } else if (rt < 2.62) {
          // HEAD-FIRST DIVE straight into the open dark circular bottle-cap hole!
          const u = (rt - 2.02) / 0.60;
          const midX = lerp(stand[0], hole[0], 0.55);
          const midY = hole[1] + 0.58;
          const midZ = lerp(stand[1], hole[2], 0.55);
          if (u < 0.45) {
            const v = u / 0.45;
            const o = 1 - v;
            x = o * o * stand[0] + 2 * o * v * midX + v * v * hole[0];
            y = o * o * 0.0 + 2 * o * v * midY + v * v * (hole[1] + 0.08);
            z = o * o * stand[1] + 2 * o * v * midZ + v * v * hole[2];
            pitch = smooth(v) * 1.65; // pitches head-first toward the dark tunnel!
          } else {
            const v = (u - 0.45) / 0.55;
            x = lerp(hole[0], inside[0], v);
            y = lerp(hole[1] + 0.08, inside[1], v);
            z = lerp(hole[2], inside[2], v);
            pitch = lerp(1.65, 2.15, v); // plunges straight down into tunnel, cleanly depth-clipped by the hole!
          }
          yaw = yawToHole;
          inst.clipName = "Swim";
          inst.animTime = undefined;
          inst.idleTime = rt * 3.4;
          if (u > 0.94) showAnt = false;
        } else if (rt < 3.12) {
          // Underground inside enemy base stealing the lunchbox while bottle cap stays propped open!
          showAnt = false;
        } else {
          // Pop back OUT of the dark bottle-cap hole clutching the stolen yellow-lidded 3D lunchbox just before cap slams shut!
          const u = (rt - 3.12) / 0.48;
          const popApexY = hole[1] + 0.50;
          if (u < 0.34) {
            const v = u / 0.34;
            x = lerp(inside[0], hole[0], v);
            y = lerp(inside[1], hole[1] + 0.14, v);
            z = lerp(inside[2], hole[2], v);
            pitch = lerp(-0.50, -0.22, v);
          } else {
            const v = (u - 0.34) / 0.66;
            const o = 1 - v;
            const mx = lerp(hole[0], stand[0], 0.5);
            const mz = lerp(hole[2], stand[1], 0.5);
            x = o * o * hole[0] + 2 * o * v * mx + v * v * stand[0];
            y = o * o * (hole[1] + 0.14) + 2 * o * v * popApexY + v * v * 0.0;
            z = o * o * hole[2] + 2 * o * v * mz + v * v * stand[1];
            pitch = lerp(-0.22, 0.0, smooth(v));
          }
          yaw = yawToHole + Math.PI;
          inst.clipName = "Idle";
          inst.animTime = undefined;
          inst.idleTime = rt * 2.0;
          showLoot = u > 0.15;
          lootFront = true;
        }
      } else {
        // Phase 7: Getaway sprint carrying the stolen yellow-lidded lunchbox back toward home base!
        hillEntry.inst.clipName = "Idle";
        hillEntry.inst.animTime = undefined;
        const gt = lt - 5.20; // 0.00 .. 5.00 s
        if (gt < 1.85) {
          const u = smooth(gt / 1.85);
          x = lerp(stand[0], away[0], u);
          z = lerp(stand[1], away[1], u);
          yaw = snapYaw8(Math.atan2(away[0] - stand[0], away[1] - stand[1]));
          walkDist = u * Math.hypot(away[0] - stand[0], away[1] - stand[1]);
          walking = true;
          showLoot = true;
        } else if (gt < 3.45) {
          const u = smooth((gt - 1.85) / 1.60);
          x = lerp(away[0], stash[0], u);
          z = lerp(away[1], stash[1], u);
          yaw = snapYaw8(Math.atan2(stash[0] - away[0], stash[1] - away[1]));
          walkDist = 2.6 + u * Math.hypot(stash[0] - away[0], stash[1] - away[1]);
          walking = true;
          showLoot = true;
        } else if (gt < 4.05) {
          // Quick victorious pause with lunchbox before stashing & heading back for another raid
          x = stash[0]; z = stash[1];
          yaw = snapYaw8(Math.atan2(away[0] - stash[0], away[1] - stash[1]));
          showLoot = gt < 3.85;
        } else {
          const u = smooth((gt - 4.05) / 0.95);
          x = lerp(stash[0], away[0], u);
          z = lerp(stash[1], away[1], u);
          yaw = snapYaw8(Math.atan2(away[0] - stash[0], away[1] - stash[1]));
          walkDist = u * Math.hypot(away[0] - stash[0], away[1] - stash[1]);
          walking = true;
        }
        y = 0;
        inst.clipName = walking ? "Walk" : "IdleLookout";
        inst.idleTime = walking ? walkDist * 0.90 : t * 1.2;
        inst.animTime = walking ? inst.idleTime : undefined;
      }

      if (Math.abs(x - tx) > visHalfSpan + 3.0 || Math.abs(z - tz) > visHalfSpan + 3.0) continue;
      if (showAnt) {
        inst.offsetMat = buildAntPoseMat(inst.model, x, y, z, antScale, yaw, pitch);
        visible.push(inst);
      }
      if (showLoot) {
        const lootAnimT = Number.isFinite(inst.animTime) ? inst.animTime : (inst.idleTime || 0);
        lootInst.offsetMat = buildCarriedLunchboxMat(lunchboxModel, x, y, z, yaw, lootAnimT, walking, antScale, lootFront);
        visible.push(lootInst);
      }
    }
  }

  window.__demoHeist = {
    buildCarriedLunchboxMat,
    buildHeistsForMap,
    updateHeists,
  };
})();

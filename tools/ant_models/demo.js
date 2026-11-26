"use strict";

(() => {
  const { clamp, mat4FromTRS, parseGlb } = window.__antGltf;
  const {
    DEG, TEAMS, LIGHTING_ORDER, validLighting, populateLightingSelect,
    wrapAngleDeg, AntRenderer, installToolbarRovingTabindex,
  } = window.__antRender;

  const $ = (id) => document.getElementById(id);
  const ANT_SCALE = 0.62;

  const CAM_PRESETS = {
    rts:     { label: "RTS 45°",  yaw: -14, pitch: 45, zoom: 0.96, target: [0.0, 0.35, 0.0] },
    hero:    { label: "Low Hero", yaw: -24, pitch: 20, zoom: 0.72, target: [0.0, 0.45, 0.0] },
    closeup: { label: "Close-Up", yaw: -18, pitch: 28, zoom: 0.42, target: [0.2, 0.40, 0.0] },
    top:     { label: "Top-Down", yaw:   0, pitch: 74, zoom: 1.05, target: [0.0, 0.15, 0.0] },
  };

  // Curated 3D backyard RTS scenarios with authentic 1998 ant-to-item relative scales.
  const SCENARIOS = {
    picnic: {
      id: "picnic",
      title: "Picnic Feast Raid",
      desc: "Green workers forage across a gingham picnic blanket loaded with Cheeseburger, wobbling Jell-O, Cereal, Corn Dog, and Steak Whiz while Red raiders contest a dropping Power Nectar.",
      teams: ["green", "red"],
      defaultLighting: "daylight",
      fitRadius: 4.55,
      halfWidth: 6.2,
      ground: {
        theme: 0,
        zone: [0.5, -0.1, 3.25, 2.5],
        zoneRot: 0.14,
        clearings: [
          [-4.8, 1.5, 2.0, 0.95],
          [-2.8, 1.1, 1.15, 0.55],
          [5.4, -1.8, 1.9, 0.90],
        ],
        glows: [],
      },
      focuses: [
        { id: "overview", label: "Overview", target: [0.1, 0.35, 0.0], yaw: -14, pitch: 45, zoom: 0.96 },
        { id: "burger",   label: "Cheeseburger", target: [0.2, 0.42, -0.5], yaw: -22, pitch: 26, zoom: 0.38 },
        { id: "jello",    label: "Wobbling Jell-O", target: [-1.6, 0.46, -1.4], yaw: 18, pitch: 24, zoom: 0.36 },
        { id: "steak",    label: "Steak Whiz", target: [1.1, 0.32, 1.35], yaw: -32, pitch: 28, zoom: 0.38 },
        { id: "skirmish", label: "Nectar Clash", target: [3.15, 0.42, -0.65], yaw: -28, pitch: 24, zoom: 0.40 },
        { id: "groucho",  label: "Groucho Daisy", target: [-3.5, 0.72, -2.9], yaw: 12, pitch: 22, zoom: 0.38 },
        { id: "hill",     label: "Green Hill", target: [-4.6, 0.55, 1.5], yaw: 24, pitch: 30, zoom: 0.44 },
      ],
      items: [
        { id: "GREENHILL", x: -4.8,  z:  1.5,  s: 2.35, yaw:  25 },
        { id: "REDHILL",   x:  5.4,  z: -1.8,  s: 2.20, yaw: -145 },
        // Picnic blanket feast
        { id: "fdburgr1",  x:  0.15, z: -0.45, s: 1.85, yaw:  18 },
        { id: "fdjelo1",   x: -1.65, z: -1.40, s: 1.65, yaw: -12 },
        { id: "fdcer1",    x:  1.42, z: -1.68, s: 1.88, yaw:  34 },
        { id: "fdcornd1",  x: -1.35, z:  1.10, s: 1.85, yaw: -26 },
        { id: "steakwhiz", x:  1.55, z:  1.18, s: 1.55, yaw: -138 },
        { id: "fdsteak1",  x:  0.42, z:  1.58, s: 0.88, yaw:  24 },
        { id: "fdsteak2",  x:  0.98, z:  1.98, s: 0.80, yaw: -35 },
        { id: "fdpmeat1",  x:  2.65, z:  0.55, s: 1.28, yaw: -22 },
        { id: "fdskit1",   x: -0.35, z:  0.72, s: 0.72, yaw:  15 },
        { id: "fdskit2",   x: -0.88, z:  0.30, s: 0.66, yaw:  80 },
        // Power Nectar & backyard perimeter props
        { id: "FD_COMB",   x:  3.25, z: -0.62, s: 1.48, yaw:   0, phase: 0.56 },
        { id: "dflower1",  x: -3.55, z: -2.90, s: 2.15, yaw:  16 },
        { id: "flower2a",  x:  3.95, z:  2.25, s: 1.75, yaw: -28 },
        { id: "botcap1",   x: -2.65, z:  2.35, s: 0.92, yaw:  15 },
        { id: "bigrock1",  x:  5.25, z:  1.55, s: 1.82, yaw: -25 },
        { id: "grassbig1", x: -5.8,  z: -1.6,  s: 2.30, yaw:  20 },
        { id: "grassbig2", x:  4.8,  z: -4.0,  s: 2.35, yaw: -35 },
        { id: "grassmed1", x: -1.6,  z: -3.7,  s: 1.60, yaw:  10 },
        { id: "grassmed2", x:  1.85, z: -3.95, s: 1.55, yaw: -20 },
        { id: "grassmed3", x: -5.1,  z:  3.7,  s: 1.50, yaw:  40 },
        { id: "clovers",   x: -3.75, z:  2.95, s: 1.15, yaw:  12 },
        { id: "clover2",   x:  3.75, z:  1.55, s: 1.05, yaw: -40 },
      ],
      ants: [
        // Live Green foraging train marching between Green Hill & Picnic Blanket
        { cls: "worker", team: "green", clip: "Walk", phase: 0.00, speed: 0.72,
          path: [[-3.45, 1.35], [-2.55, 0.70], [-2.15, 0.15], [-0.95, -0.08], [-2.15, 0.15], [-2.55, 0.70]] },
        { cls: "worker", team: "green", clip: "Walk", phase: 0.33, speed: 0.72,
          path: [[-3.45, 1.35], [-2.55, 0.70], [-2.15, 0.15], [-0.95, -0.08], [-2.15, 0.15], [-2.55, 0.70]] },
        { cls: "worker", team: "green", clip: "Walk", phase: 0.66, speed: 0.72,
          path: [[-3.45, 1.35], [-2.55, 0.70], [-2.15, 0.15], [-0.95, -0.08], [-2.15, 0.15], [-2.55, 0.70]] },
        // Green harvesters chomping food
        { cls: "worker", team: "green", clip: "Harvest", x: -0.78, z: -0.68, face: [0.15, -0.45], phase: 0.1 },
        { cls: "worker", team: "green", clip: "Harvest", x: -1.78, z: -0.45, face: [-1.65, -1.4], phase: 0.45 },
        { cls: "worker", team: "green", clip: "Harvest", x:  0.02, z:  1.58, face: [0.42, 1.58], phase: 0.75 },
        // Frontline combat clash around Combat Power Nectar (clear of cereal bowl!)
        { cls: "combat", team: "green", clip: "Attack",  x:  2.62, z: -0.42, face: [3.85, -0.65], phase: 0.05 },
        { cls: "combat", team: "red",   clip: "Attack",  x:  3.95, z: -0.62, face: [2.62, -0.42], phase: 0.42 },
        { cls: "combat", team: "green", clip: "Attack",  x:  2.72, z: -1.18, face: [3.82, -1.25], phase: 0.62 },
        { cls: "combat", team: "red",   clip: "Attack",  x:  3.82, z: -1.28, face: [2.72, -1.18], phase: 0.22 },
        // Sneaky Red thief flanking cleanly behind the cereal bowl
        { cls: "thief",  team: "red",   clip: "Walk", phase: 0.15, speed: 0.85,
          path: [[2.95, -2.65], [0.85, -2.75], [0.45, -2.35], [2.45, -2.45]] },
        // Green Fire guard & Red supply worker
        { cls: "fire",   team: "green", clip: "Idle",   x: -2.45, z: -0.45, face: [0.2, -0.5], phase: 0.3 },
        { cls: "worker", team: "red",   clip: "Walk", phase: 0.5, speed: 0.68,
          path: [[4.55, -1.05], [3.65, 0.25], [3.25, 0.55], [4.15, -0.45]] },
      ],
    },

    toys: {
      id: "toys",
      title: "Toybox Backyard Skirmish",
      desc: "Blue and Red squads clash on a flagstone patio across a standing Metal Slinky, Wireless Mouse & HDMI plug, Teddy Bear, Rubber Ducky, Toy Dino, Santa Head, Bottle Cap, and blazing Fire Wall.",
      teams: ["blue", "red"],
      defaultLighting: "daylight",
      fitRadius: 4.65,
      halfWidth: 6.3,
      ground: {
        theme: 1,
        zone: [0.0, 0.0, 3.85, 2.85],
        zoneRot: -0.07,
        clearings: [
          [-5.2, 1.2, 1.95, 0.94],
          [5.2, -1.3, 1.95, 0.94],
        ],
        glows: [
          [0.62, 0.78, 1.85, 0.98],
          [1.42, 1.25, 1.85, 0.95],
          [-1.50, 1.48, 1.15, 0.58],
          [2.15, 0.12, 1.15, 0.58],
        ],
      },
      focuses: [
        { id: "overview", label: "Overview",       target: [0.0, 0.35, 0.0],   yaw: -14, pitch: 45, zoom: 0.96 },
        { id: "slinky",   label: "Metal Slinky",   target: [-0.35, 0.45, -0.55], yaw: -16, pitch: 22, zoom: 0.38 },
        { id: "mouse",    label: "Mouse & HDMI",   target: [-1.85, 0.30, -1.65], yaw: 24,  pitch: 28, zoom: 0.40 },
        { id: "firewall", label: "Fire Wall",      target: [1.05, 0.48, 0.85],  yaw: -28, pitch: 24, zoom: 0.40 },
        { id: "santa",    label: "Santa & Bear",   target: [-1.65, 0.52, 1.85], yaw: 14,  pitch: 24, zoom: 0.42 },
        { id: "cap",      label: "Cap & Button",   target: [-1.30, 0.22, 0.56], yaw: -12, pitch: 38, zoom: 0.34 },
        { id: "ducky",    label: "Ducky & Bomber", target: [2.45, 0.45, 0.85],  yaw: -36, pitch: 25, zoom: 0.40 },
      ],
      items: [
        { id: "BLUEHILL",  x: -5.2,  z:  1.2,  s: 2.30, yaw:  30 },
        { id: "REDHILL",   x:  5.2,  z: -1.3,  s: 2.30, yaw: -150 },
        // Signature patio toys & upgraded props
        { id: "slinky",    x: -0.40, z: -0.95, s: 2.22, yaw:  18 },
        { id: "mouse1",    x: -2.62, z: -1.52, s: 2.02, yaw:  32 },
        { id: "mouse2",    x: -1.18, z: -2.18, s: 1.18, yaw:  58 },
        { id: "can1",      x:  2.45, z: -1.82, s: 2.10, yaw: -28 },
        { id: "bearhed",   x: -3.38, z:  1.38, s: 1.72, yaw:  24 },
        { id: "ducky1",    x:  2.98, z:  1.52, s: 1.55, yaw: -42 },
        { id: "dino",      x:  0.98, z: -2.35, s: 1.85, yaw: -20 },
        { id: "santahed",  x: -0.35, z:  2.38, s: 1.62, yaw:  10 },
        { id: "botcap1",   x: -2.05, z:  0.48, s: 0.95, yaw:   0 },
        { id: "shtbut1",   x: -1.08, z:  0.95, s: 0.88, yaw:  22 },
        // Volumetric Fire Wall, Sizzling Bombs & Power Nectar
        { id: "wallup04",  x:  0.62, z:  0.78, s: 1.18, yaw:  25 },
        { id: "wallup04",  x:  1.42, z:  1.25, s: 1.10, yaw: -18 },
        { id: "bluebomb",  x: -1.55, z:  1.62, s: 0.84, yaw:  10 },
        { id: "redbomb",   x:  2.15, z:  0.12, s: 0.82, yaw: -20 },
        { id: "FD_FIRE",   x:  3.55, z: -0.58, s: 1.48, yaw:   0, phase: 0.45 },
        // Backyard lawn border framing
        { id: "dflower1",  x:  4.35, z:  2.55, s: 2.05, yaw: -22 },
        { id: "grassbig1", x: -4.85, z: -2.55, s: 2.25, yaw:  15 },
        { id: "grassbig2", x:  4.55, z: -3.55, s: 2.30, yaw: -30 },
        { id: "grassmed1", x: -2.05, z: -3.85, s: 1.55, yaw:   5 },
        { id: "clover1",   x: -4.05, z:  3.15, s: 1.15, yaw:  18 },
      ],
      ants: [
        // Bombers & Fire Ant triggering effects
        { cls: "bomber",  team: "blue", clip: "PlantBomb",   x: -2.12, z:  1.68, face: [-1.55, 1.62], phase: 0.1 },
        { cls: "fire",    team: "red",  clip: "StartFire",   x:  1.42, z:  0.22, face: [0.62, 0.78], phase: 0.35 },
        { cls: "bomber",  team: "red",  clip: "PlantBomb",   x:  2.65, z:  0.05, face: [2.15, 0.12], phase: 0.65 },
        // Center duel cleanly in front of the Slinky Arch & Shirt Button
        { cls: "combat",  team: "blue", clip: "Attack",      x: -0.35, z:  0.22, face: [0.55, -0.05], phase: 0.0 },
        { cls: "combat",  team: "red",  clip: "Attack",      x:  0.55, z: -0.08, face: [-0.35, 0.22], phase: 0.38 },
        // Blue Thief sneaking behind the Wireless Mouse & HDMI plug
        { cls: "thief",   team: "blue", clip: "Walk", phase: 0.1, speed: 0.88,
          path: [[-3.65, -0.45], [-3.25, -2.65], [-0.45, -2.95], [-1.85, -0.45]] },
        // Red Thief lookout near soda can
        { cls: "thief",   team: "red",  clip: "IdleLookout", x:  3.25, z: -2.45, face: [0.0, 0.0], phase: 0.25 },
        // Marching workers & reinforcements
        { cls: "worker",  team: "blue", clip: "Walk", phase: 0.2, speed: 0.70,
          path: [[-4.0, 0.65], [-2.55, -0.15], [-3.45, -0.05]] },
        { cls: "worker",  team: "red",  clip: "Walk", phase: 0.7, speed: 0.70,
          path: [[4.05, -0.95], [3.15, -0.35], [3.85, -1.25]] },
        { cls: "combat",  team: "blue", clip: "Idle",        x: -2.08, z:  2.42, face: [0.62, 0.78], phase: 0.5 },
        { cls: "swimmer", team: "blue", clip: "Idle",        x:  0.48, z:  2.28, face: [1.42, 1.25], phase: 0.8 },
        { cls: "combat",  team: "red",  clip: "Walk", phase: 0.4, speed: 0.76,
          path: [[2.95, 0.65], [1.95, 0.95], [2.45, 0.45]] },
      ],
    },

    colony: {
      id: "colony",
      title: "Backyard Colony Base",
      desc: "A thriving Green meadow colony harvesting Roast Chicken, Pizza Tube, Twinkie, Pretzel, Cola Spill, and Fried Eggs surrounded by clovers, mossy rocks, Googly Flower, and Groucho Daisy.",
      teams: ["green", "black"],
      defaultLighting: "daylight",
      fitRadius: 4.7,
      halfWidth: 6.3,
      ground: {
        theme: 2,
        zone: [0, 0, 0, 0],
        zoneRot: 0,
        clearings: [
          [-1.8, -0.3, 2.35, 0.96],
          [1.65, -1.05, 1.45, 0.65],
          [1.45, 1.65, 1.55, 0.68],
          [-1.05, 2.25, 1.30, 0.55],
        ],
        glows: [],
      },
      focuses: [
        { id: "overview", label: "Overview",       target: [0.0, 0.35, 0.0],   yaw: -14, pitch: 45, zoom: 0.96 },
        { id: "hill",     label: "Green Colony",   target: [-1.8, 0.55, -0.3], yaw: -20, pitch: 30, zoom: 0.42 },
        { id: "chicken",  label: "Roast Chicken",  target: [2.15, 0.40, -1.2], yaw: -32, pitch: 26, zoom: 0.38 },
        { id: "pizza",    label: "Pizza Tube",     target: [1.95, 0.32, 1.65], yaw: -26, pitch: 28, zoom: 0.40 },
        { id: "groucho",  label: "Groucho Daisy",  target: [-3.6, 0.72, -2.55], yaw: 18, pitch: 22, zoom: 0.38 },
        { id: "nectar",   label: "Swim Nectar",    target: [3.65, 0.45, 0.25], yaw: -40, pitch: 24, zoom: 0.38 },
      ],
      items: [
        { id: "GREENHILL", x: -1.8,  z: -0.3,  s: 2.45, yaw:  15 },
        // Harvest food ring
        { id: "fdchick1",  x:  2.2,  z: -1.2,  s: 1.80, yaw: -20 },
        { id: "pizzatube", x:  2.55, z:  1.35, s: 1.65, yaw: -140 },
        { id: "fdpizz1",   x:  1.35, z:  1.95, s: 1.12, yaw:  25 },
        { id: "fdtwink1",  x: -0.4,  z:  2.55, s: 1.55, yaw:  12 },
        { id: "fdpretz1",  x: -2.6,  z:  2.35, s: 1.38, yaw: -18 },
        { id: "fdcola1",   x:  0.35, z: -2.68, s: 1.72, yaw:  10 },
        { id: "fdeggs1",   x: -3.95, z:  1.35, s: 1.55, yaw:  28 },
        { id: "fdgumw1",   x: -0.6,  z:  1.25, s: 1.28, yaw:  42 },
        { id: "fdcrakr1",  x:  0.55, z:  0.65, s: 0.96, yaw: -15 },
        // Power Nectars
        { id: "FD_SWIM",   x:  3.85, z:  0.25, s: 1.48, yaw:   0, phase: 0.42 },
        { id: "FD_THIEF",  x: -4.35, z: -1.25, s: 1.48, yaw:   0, phase: 0.62 },
        // Wild meadow flora & mossy rocks
        { id: "dflower1",  x: -3.6,  z: -2.55, s: 2.15, yaw:  22 },
        { id: "flower3c",  x:  3.8,  z: -2.4,  s: 1.95, yaw: -25 },
        { id: "flower2a",  x: -4.85, z:  2.85, s: 1.80, yaw:  35 },
        { id: "mush2a",    x: -1.5,  z: -2.85, s: 1.45, yaw:  10 },
        { id: "bigrock2",  x:  4.55, z: -1.15, s: 2.05, yaw: -30 },
        { id: "bigrock1",  x: -4.95, z:  0.35, s: 1.85, yaw:  20 },
        { id: "medrock3",  x:  4.05, z:  3.10, s: 1.30, yaw: -15 },
        { id: "smlrock1",  x: -2.85, z: -1.65, s: 0.90, yaw:  40 },
        { id: "grassbig1", x: -5.3,  z: -2.15, s: 2.30, yaw:  12 },
        { id: "grassbig2", x:  4.95, z:  1.65, s: 2.25, yaw: -22 },
        { id: "grassmed2", x:  2.05, z: -3.55, s: 1.60, yaw: -10 },
        { id: "clover1",   x: -3.3,  z:  0.20, s: 1.10, yaw:   0 },
        { id: "clover3",   x:  2.4,  z:  2.95, s: 1.15, yaw:  30 },
        { id: "clovers",   x: -1.7,  z:  3.35, s: 1.20, yaw: -20 },
      ],
      ants: [
        // Two busy worker foraging loops
        { cls: "worker",  team: "green", clip: "Walk", phase: 0.00, speed: 0.72,
          path: [[-0.55, -0.45], [0.45, -0.85], [1.35, -1.05], [0.45, -0.35]] },
        { cls: "worker",  team: "green", clip: "Walk", phase: 0.50, speed: 0.72,
          path: [[-0.55, -0.45], [0.45, -0.85], [1.35, -1.05], [0.45, -0.35]] },
        { cls: "worker",  team: "green", clip: "Walk", phase: 0.20, speed: 0.68,
          path: [[-0.85, 0.65], [0.15, 1.45], [0.95, 1.85], [-0.15, 1.75]] },
        { cls: "worker",  team: "green", clip: "Walk", phase: 0.72, speed: 0.68,
          path: [[-0.85, 0.65], [0.15, 1.45], [0.95, 1.85], [-0.15, 1.75]] },
        // Active harvesters
        { cls: "worker",  team: "green", clip: "Harvest",     x:  1.45, z: -1.05, face: [2.2, -1.2], phase: 0.12 },
        { cls: "worker",  team: "green", clip: "Harvest",     x:  0.98, z:  1.72, face: [1.35, 1.95], phase: 0.44 },
        { cls: "worker",  team: "green", clip: "Harvest",     x: -3.22, z:  1.25, face: [-3.95, 1.35], phase: 0.78 },
        // Colony guards & specialists
        { cls: "combat",  team: "green", clip: "Idle",        x:  0.35, z: -1.48, face: [2.2, -1.2], phase: 0.3 },
        { cls: "swimmer", team: "green", clip: "Idle",        x:  3.12, z:  0.30, face: [3.85, 0.25], phase: 0.6 },
        { cls: "fire",    team: "green", clip: "Walk", phase: 0.1, speed: 0.62,
          path: [[-2.55, 1.25], [-1.35, 1.65], [-2.15, 1.75]] },
        { cls: "thief",   team: "green", clip: "IdleLookout", x: -3.62, z: -1.12, face: [-4.35, -1.25], phase: 0.2 },
        // Sneaky Black Thief creeping cleanly in front of the outer rock
        { cls: "thief",   team: "black", clip: "Walk", phase: 0.35, speed: 0.84,
          path: [[3.65, 2.05], [2.85, 2.55], [3.35, 1.65]] },
      ],
    },

    melee: {
      id: "melee",
      title: "Four-Team Backyard Melee",
      desc: "All four teams (Green, Red, Blue, Black) and all six ant classes collide in a sun-baked central arena over a giant Cheeseburger, Metal Slinky, Wireless Mouse, Power Nectars, Bombs, and Fire Wall.",
      teams: ["green", "red", "blue", "black"],
      defaultLighting: "daylight",
      fitRadius: 4.9,
      halfWidth: 6.5,
      ground: {
        theme: 3,
        zone: [0, 0, 0, 0],
        zoneRot: 0,
        clearings: [
          [-4.55, 2.85, 1.9, 0.94],
          [4.55, -2.85, 1.9, 0.94],
          [-4.55, -2.85, 1.9, 0.94],
          [4.55, 2.85, 1.9, 0.94],
        ],
        glows: [
          [-0.15, 1.48, 1.95, 1.0],
          [1.48, -0.55, 1.15, 0.58],
          [-1.48, -0.75, 1.15, 0.58],
        ],
      },
      focuses: [
        { id: "overview", label: "Overview",    target: [0.0, 0.35, 0.0],   yaw: -14, pitch: 45, zoom: 0.98 },
        { id: "center",   label: "Center Clash", target: [0.0, 0.42, -0.15], yaw: -22, pitch: 24, zoom: 0.42 },
        { id: "firewall", label: "Fire Duel",   target: [-0.15, 0.48, 1.45], yaw: 14,  pitch: 22, zoom: 0.38 },
        { id: "slinky",   label: "Slinky Flank", target: [0.0, 0.45, -2.55], yaw: -16, pitch: 24, zoom: 0.40 },
        { id: "mouse",    label: "Mouse South", target: [0.15, 0.32, 3.15],  yaw: 22,  pitch: 28, zoom: 0.38 },
      ],
      items: [
        // 4 quadrant anthills
        { id: "GREENHILL", x: -4.55, z:  2.85, s: 2.15, yaw:  42 },
        { id: "REDHILL",   x:  4.55, z: -2.85, s: 2.15, yaw: -138 },
        { id: "BLUEHILL",  x: -4.55, z: -2.85, s: 2.15, yaw:  138 },
        { id: "BLACKHILL", x:  4.55, z:  2.85, s: 2.15, yaw: -42 },
        // Contested center arena
        { id: "fdburgr1",  x:  0.0,  z: -0.15, s: 1.85, yaw:  10 },
        { id: "slinky",    x:  0.0,  z: -2.65, s: 2.05, yaw:   0 },
        { id: "fdjelo1",   x: -2.85, z:  0.30, s: 1.45, yaw:  15 },
        { id: "steakwhiz", x:  2.48, z:  0.35, s: 1.42, yaw: -135 },
        { id: "wallup04",  x: -0.15, z:  1.48, s: 1.15, yaw:   0 },
        { id: "redbomb",   x:  1.48, z: -0.55, s: 0.82, yaw: -20 },
        { id: "greenbomb", x: -1.55, z: -0.92, s: 0.82, yaw:  25 },
        { id: "FD_BOMB",   x:  1.95, z: -1.92, s: 1.42, yaw:   0, phase: 0.48 },
        { id: "FD_COMB",   x: -1.98, z:  1.88, s: 1.42, yaw:   0, phase: 0.66 },
        { id: "botcap1",   x:  1.58, z:  1.65, s: 0.92, yaw:   0 },
        { id: "shtbut1",   x: -2.08, z: -2.18, s: 0.86, yaw:  15 },
        { id: "mouse1",    x:  0.15, z:  3.15, s: 1.80, yaw: -18 },
        // Perimeter flora & rocks
        { id: "dflower1",  x: -2.45, z: -3.95, s: 1.95, yaw:  18 },
        { id: "bigrock2",  x:  2.65, z: -4.05, s: 1.85, yaw: -35 },
        { id: "grassbig1", x: -5.85, z:  0.0,  s: 2.25, yaw:  20 },
        { id: "grassbig2", x:  5.85, z:  0.0,  s: 2.25, yaw: -20 },
        { id: "flower2a",  x:  2.55, z:  3.95, s: 1.70, yaw: -25 },
        { id: "clovers",   x: -2.45, z:  3.85, s: 1.15, yaw:  10 },
      ],
      ants: [
        // Green squad (SW)
        { cls: "combat",  team: "green", clip: "Attack",      x: -1.08, z:  0.55, face: [0.0, -0.15], phase: 0.05 },
        { cls: "fire",    team: "green", clip: "StartFire",   x: -0.88, z:  1.40, face: [-0.15, 1.48], phase: 0.20 },
        { cls: "bomber",  team: "green", clip: "PlantBomb",   x: -2.15, z: -0.95, face: [-1.55, -0.92], phase: 0.45 },
        { cls: "worker",  team: "green", clip: "Walk", phase: 0.1, speed: 0.72,
          path: [[-3.45, 2.05], [-1.75, 1.05], [-2.55, 1.85]] },
        // Red squad (NE)
        { cls: "combat",  team: "red",   clip: "Attack",      x:  1.08, z: -0.22, face: [0.0, -0.15], phase: 0.38 },
        { cls: "bomber",  team: "red",   clip: "PlantBomb",   x:  1.98, z: -0.68, face: [1.48, -0.55], phase: 0.72 },
        { cls: "thief",   team: "red",   clip: "Walk", phase: 0.3, speed: 0.88,
          path: [[2.95, -1.65], [1.25, -2.05], [2.15, -1.35]] },
        { cls: "worker",  team: "red",   clip: "Walk", phase: 0.6, speed: 0.72,
          path: [[3.45, -1.95], [2.25, -0.95], [3.15, -1.35]] },
        // Blue squad (NW)
        { cls: "combat",  team: "blue",  clip: "Attack",      x: -0.78, z: -1.18, face: [0.0, -0.15], phase: 0.55 },
        { cls: "swimmer", team: "blue",  clip: "Walk", phase: 0.15, speed: 0.75,
          path: [[-3.55, -1.95], [-2.65, -1.55], [-3.05, -2.55]] },
        { cls: "thief",   team: "blue",  clip: "IdleLookout", x: -1.18, z: -1.85, face: [0.0, -0.15], phase: 0.15 },
        { cls: "worker",  team: "blue",  clip: "Harvest",     x: -2.05, z:  0.15, face: [-2.85, 0.30], phase: 0.80 },
        // Black squad (SE)
        { cls: "combat",  team: "black", clip: "Attack",      x:  0.98, z:  0.88, face: [0.0, -0.15], phase: 0.75 },
        { cls: "fire",    team: "black", clip: "StartFire",   x:  0.60, z:  1.58, face: [-0.15, 1.48], phase: 0.52 },
        { cls: "swimmer", team: "black", clip: "Idle",        x:  2.42, z:  1.55, face: [0.0, 0.0], phase: 0.35 },
        { cls: "worker",  team: "black", clip: "Walk", phase: 0.85, speed: 0.72,
          path: [[3.45, 1.95], [2.05, 1.15], [2.95, 1.75]] },
      ],
    },
  };

  const SCENARIO_ORDER = ["picnic", "toys", "colony", "melee"];

  // --- Arc-Length Parameterized Closed Path Evaluator ---
  function preparePath(pts) {
    const segs = [];
    let total = 0;
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], b = pts[(i + 1) % pts.length];
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
      if (len > 1e-4) {
        segs.push({ ax: a[0], az: a[1], bx: b[0], bz: b[1], len, start: total });
        total += len;
      }
    }
    return { segs, total: Math.max(total, 1e-4) };
  }

  function samplePath(pathData, distRaw) {
    const { segs, total } = pathData;
    const d = ((distRaw % total) + total) % total;
    for (let i = 0; i < segs.length; i++) {
      const s = segs[i];
      if (d <= s.start + s.len || i === segs.length - 1) {
        const u = clamp((d - s.start) / s.len, 0, 1);
        const x = s.ax + (s.bx - s.ax) * u;
        const z = s.az + (s.bz - s.az) * u;
        // Blend heading smoothly across corners so ants turn cleanly instead of snapping
        const currAng = Math.atan2(s.bx - s.ax, s.bz - s.az);
        const nextSeg = segs[(i + 1) % segs.length];
        const nextAng = Math.atan2(nextSeg.bx - nextSeg.ax, nextSeg.bz - nextSeg.az);
        let yaw = currAng;
        if (u > 0.82) {
          const blend = (u - 0.82) / 0.18;
          let diff = nextAng - currAng;
          while (diff > Math.PI) diff -= Math.PI * 2;
          while (diff < -Math.PI) diff += Math.PI * 2;
          yaw = currAng + diff * (blend * blend * (3 - 2 * blend));
        }
        return { x, z, yaw };
      }
    }
    return { x: 0, z: 0, yaw: 0 };
  }

  function buildWorldMat(model, x, z, scale, yawRad) {
    const minY = model && model.min ? model.min[1] : 0;
    const h = yawRad * 0.5;
    return mat4FromTRS([x, -minY * scale, z], [0, Math.sin(h), 0, Math.cos(h)], [scale, scale, scale]);
  }

  function itemShadowParams(model, id, scale) {
    const sx = model && model.size ? model.size[0] : 0.8;
    const sz = model && model.size ? model.size[2] : 0.8;
    const baseRad = 0.52 * Math.max(sx, sz) * scale;
    if (/^(grass|flower|dflower|clover)/.test(id)) return [baseRad * 0.72, 0.54];
    if (/^(FD_|wallup)/.test(id)) return [baseRad * 0.85, 0.42];
    if (/HILL$/.test(id)) return [baseRad * 0.96, 0.84];
    return [baseRad * 0.90, 0.78];
  }

  // --- App State ---
  const reducedMotionMq = window.matchMedia("(prefers-reduced-motion: reduce)");
  const darkMq = window.matchMedia("(prefers-color-scheme: dark)");
  let isDark = darkMq.matches;
  darkMq.addEventListener("change", (e) => { isDark = e.matches; });

  const params = new URLSearchParams(window.location.search);
  const initScenario = SCENARIOS[params.get("scenario")] ? params.get("scenario") : "picnic";
  const initCamKey = CAM_PRESETS[params.get("cam")] ? params.get("cam") : "rts";
  const initProj = params.get("proj") === "ortho" ? "ortho" : "perspective";
  const initLight = validLighting(params.get("light")) || SCENARIOS[initScenario].defaultLighting;

  const state = {
    scenarioId: initScenario,
    camPreset: initCamKey,
    focusId: "overview",
    lighting: initLight,
    playing: params.get("play") !== "0",
    orbit: params.get("orbit") === "1" && !reducedMotionMq.matches,
    patrols: params.get("patrols") !== "0",
    pixelMode: params.get("pixel") === "1",
    showHud: params.get("hud") !== "0",
    simTime: Number.isFinite(Number(params.get("t"))) ? Number(params.get("t")) : 0,
    fixedTime: params.has("t") && params.get("play") === "0",
  };

  const cam = {
    yaw: CAM_PRESETS[initCamKey].yaw,
    pitch: CAM_PRESETS[initCamKey].pitch,
    zoom: CAM_PRESETS[initCamKey].zoom,
    projection: initProj,
    target: CAM_PRESETS[initCamKey].target.slice(),
  };
  let camGoal = null;

  const canvas = $("demo-canvas");
  const renderer = new AntRenderer(canvas);
  const gl = renderer.gl;
  const modelCache = new Map();
  let activeScene = null;
  let loadSerial = 0;

  async function loadModel(key, url) {
    if (modelCache.has(key)) return modelCache.get(key);
    const p = fetch(url).then((res) => {
      if (!res.ok) throw new Error(`HTTP ${res.status} loading ${url}`);
      return res.arrayBuffer();
    }).then((ab) => parseGlb(ab, gl));
    modelCache.set(key, p);
    return p;
  }

  function setStatus(msg) {
    const el = $("status-region");
    if (el) el.textContent = msg;
  }

  function setCamGoal(target, yaw, pitch, zoom) {
    if (reducedMotionMq.matches) {
      cam.target = target.slice();
      cam.yaw = yaw;
      cam.pitch = pitch;
      cam.zoom = zoom;
      camGoal = null;
    } else {
      camGoal = { target: target.slice(), yaw, pitch, zoom };
    }
  }

  function renderHudAndSidebar(sc, triCount) {
    $("hud-title").textContent = sc.title;
    $("hud-desc").textContent = sc.desc;
    const teamsEl = $("hud-teams");
    teamsEl.textContent = "";
    for (const tid of sc.teams) {
      const pill = document.createElement("span");
      pill.className = "demo-team-pill";
      const dot = document.createElement("span");
      dot.className = `demo-team-dot team-${tid}`;
      dot.setAttribute("aria-hidden", "true");
      pill.append(dot, document.createTextNode(TEAMS[tid].label));
      teamsEl.appendChild(pill);
    }
    $("badge-scene-stats").textContent = `${sc.ants.length} ants · ${sc.items.length} items`;
    $("meta-ants").textContent = `${sc.ants.length} (${sc.teams.map((t) => TEAMS[t].label).join(" vs ")})`;
    $("meta-items").textContent = `${sc.items.length} 3D props`;
    $("meta-tris").textContent = triCount ? triCount.toLocaleString() : "—";

    const focusGrid = $("demo-focus-grid");
    focusGrid.textContent = "";
    for (const f of sc.focuses) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "demo-focus-btn";
      btn.dataset.focus = f.id;
      btn.setAttribute("aria-pressed", String(state.focusId === f.id));
      btn.textContent = f.label;
      btn.addEventListener("click", () => {
        state.focusId = f.id;
        state.camPreset = f.id === "overview" ? "rts" : "";
        setCamGoal(f.target, f.yaw, f.pitch, f.zoom);
        syncUi();
        setStatus(`Focused on ${f.label} in ${sc.title}`);
      });
      focusGrid.appendChild(btn);
    }
  }

  async function activateScenario(scenarioId, preserveCam = false) {
    const sc = SCENARIOS[scenarioId] || SCENARIOS.picnic;
    state.scenarioId = sc.id;
    state.focusId = "overview";
    const serial = ++loadSerial;
    const loadingEl = $("demo-loading");
    loadingEl.hidden = false;
    $("demo-loading-title").textContent = `Loading ${sc.title}…`;
    $("demo-loading-msg").textContent = "Streaming animated ants and 3D backyard props.";
    syncUi();

    try {
      const uniqueAnts = [...new Set(sc.ants.map((a) => a.cls))];
      const uniqueItems = [...new Set(sc.items.map((it) => it.id))];
      const jobs = [
        ...uniqueAnts.map((cls) => loadModel(`ant:${cls}`, `models/${cls}.glb`)),
        ...uniqueItems.map((id) => loadModel(`item:${id}`, `models/items/${id}.glb`)),
      ];
      await Promise.all(jobs);
      if (serial !== loadSerial) return;

      const instances = [];
      const itemEntries = [];
      const antEntries = [];
      let totalTris = 0;

      for (const spec of sc.items) {
        const model = await modelCache.get(`item:${spec.id}`);
        totalTris += model.triCount || 0;
        const yawRad = (spec.yaw || 0) * DEG;
        const [rad, op] = itemShadowParams(model, spec.id, spec.s);
        const inst = {
          kind: "item",
          id: spec.id,
          model,
          clipName: "Idle",
          idleTime: ((spec.x * 0.37 + spec.z * 0.53) % 1 + 1) % 1,
          offsetMat: buildWorldMat(model, spec.x, spec.z, spec.s, yawRad),
        };
        instances.push(inst);
        itemEntries.push({ inst, spec, shadow: [spec.x, spec.z, rad, op] });
      }

      for (let i = 0; i < sc.ants.length; i++) {
        const spec = sc.ants[i];
        const model = await modelCache.get(`ant:${spec.cls}`);
        totalTris += model.triCount || 0;
        const pathData = spec.path ? preparePath(spec.path) : null;
        let x = spec.x || 0, z = spec.z || 0, yawRad = (spec.yaw || 0) * DEG;
        if (pathData) {
          const p0 = samplePath(pathData, (spec.phase || 0) * pathData.total);
          x = p0.x; z = p0.z; yawRad = p0.yaw;
        } else if (spec.face) {
          yawRad = Math.atan2(spec.face[0] - x, spec.face[1] - z);
        }
        const inst = {
          kind: "ant",
          cls: spec.cls,
          teamColors: TEAMS[spec.team] || TEAMS.green,
          model,
          clipName: spec.clip || "Idle",
          idleTime: (spec.phase || 0) * 1.4,
          blink: { active: false, elapsed: 0, nextIn: 1.4 + ((i * 1.73) % 3.6) },
          offsetMat: buildWorldMat(model, x, z, ANT_SCALE, yawRad),
        };
        instances.push(inst);
        antEntries.push({ inst, spec, pathData, x, z, yawRad });
      }

      activeScene = {
        sc,
        instances,
        itemEntries,
        antEntries,
        target: [0, 0.35, 0],
        fitRadius: sc.fitRadius,
        halfWidth: sc.halfWidth,
        plinthSpanX: 0,
        plinthRadius: 1,
        shadows: [],
      };

      if (!preserveCam) {
        const preset = CAM_PRESETS[state.camPreset] || CAM_PRESETS.rts;
        setCamGoal(preset.target, preset.yaw, preset.pitch, preset.zoom);
      }
      renderHudAndSidebar(sc, totalTris);
      loadingEl.hidden = true;
      setStatus(`Loaded ${sc.title}: ${sc.ants.length} animated ants and ${sc.items.length} 3D map items.`);
    } catch (err) {
      if (serial !== loadSerial) return;
      $("demo-loading-title").textContent = "Unable to load 3D scenario";
      $("demo-loading-msg").textContent = String(err && err.message ? err.message : err);
      setStatus(`Load error: ${err && err.message ? err.message : err}`);
    }
  }

  function updateSimulation(dt) {
    if (!activeScene) return;
    const { sc, itemEntries, antEntries } = activeScene;
    if (state.playing && !state.fixedTime) {
      state.simTime += dt;
    }
    const t = state.simTime;

    for (let i = 0; i < itemEntries.length; i++) {
      const e = itemEntries[i];
      const basePhase = Number.isFinite(e.spec.phase) ? e.spec.phase : ((i * 0.29) % 1.1);
      e.inst.idleTime = t + basePhase;
    }

    const shadows = [];
    for (const e of itemEntries) shadows.push(e.shadow);

    for (const e of antEntries) {
      const { inst, spec, pathData } = e;
      inst.idleTime = t * (spec.animSpeed || 1.0) + (spec.phase || 0) * 1.4;
      if (state.playing && !state.fixedTime && inst.blink) {
        if (inst.blink.active) {
          inst.blink.elapsed += dt;
          if (inst.blink.elapsed >= 0.22) {
            inst.blink.active = false;
            inst.blink.elapsed = 0;
            inst.blink.nextIn = 2.0 + Math.random() * 3.8;
          }
        } else {
          inst.blink.nextIn -= dt;
          if (inst.blink.nextIn <= 0) {
            inst.blink.active = true;
            inst.blink.elapsed = 0;
          }
        }
      }
      if (pathData) {
        const travel = state.patrols ? t * (spec.speed || 0.72) : 0;
        const pose = samplePath(pathData, (spec.phase || 0) * pathData.total + travel);
        e.x = pose.x;
        e.z = pose.z;
        e.yawRad = pose.yaw;
        inst.clipName = state.patrols ? (spec.clip || "Walk") : "Idle";
        inst.offsetMat = buildWorldMat(inst.model, e.x, e.z, ANT_SCALE, e.yawRad);
      }
      shadows.push([e.x, e.z, 0.34, 0.74]);
    }
    activeScene.shadows = shadows;
    activeScene.groundOpts = {
      theme: sc.ground.theme,
      zone: sc.ground.zone,
      zoneRot: sc.ground.zoneRot,
      clearings: sc.ground.clearings,
      glows: sc.ground.glows,
      shadows,
      time: t,
    };
  }

  function updateCamera(dt) {
    if (camGoal) {
      const alpha = 1 - Math.exp(-dt * 7.5);
      let dYaw = camGoal.yaw - cam.yaw;
      while (dYaw > 180) dYaw -= 360;
      while (dYaw < -180) dYaw += 360;
      cam.yaw = wrapAngleDeg(cam.yaw + dYaw * alpha);
      cam.pitch += (camGoal.pitch - cam.pitch) * alpha;
      cam.zoom += (camGoal.zoom - cam.zoom) * alpha;
      for (let i = 0; i < 3; i++) cam.target[i] += (camGoal.target[i] - cam.target[i]) * alpha;
      const err = Math.abs(dYaw) + Math.abs(camGoal.pitch - cam.pitch) + Math.abs(camGoal.zoom - cam.zoom) * 40
        + Math.hypot(camGoal.target[0] - cam.target[0], camGoal.target[2] - cam.target[2]) * 20;
      if (err < 0.08) {
        cam.yaw = camGoal.yaw;
        cam.pitch = camGoal.pitch;
        cam.zoom = camGoal.zoom;
        cam.target = camGoal.target.slice();
        camGoal = null;
      }
    } else if (state.orbit && state.playing && !pointerState.dragging) {
      cam.yaw = wrapAngleDeg(cam.yaw + dt * 9.5);
    }
  }

  function resizeCanvas() {
    const dpr = state.pixelMode ? 0.34 : Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(120, Math.floor(canvas.clientWidth * dpr));
    const h = Math.max(80, Math.floor(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    return [w, h];
  }

  // --- Interactive Orbit, Pan & Pinch-Zoom Pointer Controller ---
  const pointerState = { dragging: false, pointers: new Map(), mode: "orbit", lastX: 0, lastY: 0, lastPinch: 0 };

  function panByPixels(dx, dy) {
    camGoal = null;
    const scale = 0.0085 * cam.zoom * (activeScene ? activeScene.fitRadius * 0.42 : 2.0);
    const yawRad = cam.yaw * DEG;
    const rightX = Math.cos(yawRad), rightZ = -Math.sin(yawRad);
    const fwdX = -Math.sin(yawRad), fwdZ = -Math.cos(yawRad);
    cam.target[0] = clamp(cam.target[0] - (rightX * dx - fwdX * dy) * scale, -7.2, 7.2);
    cam.target[2] = clamp(cam.target[2] - (rightZ * dx - fwdZ * dy) * scale, -6.2, 6.2);
    state.focusId = "";
    syncFocusButtons();
  }

  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  canvas.addEventListener("pointerdown", (e) => {
    canvas.setPointerCapture(e.pointerId);
    pointerState.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    pointerState.dragging = true;
    camGoal = null;
    if (pointerState.pointers.size === 1) {
      pointerState.mode = (e.button === 2 || e.button === 1 || e.shiftKey) ? "pan" : "orbit";
      pointerState.lastX = e.clientX;
      pointerState.lastY = e.clientY;
    } else if (pointerState.pointers.size === 2) {
      const [a, b] = [...pointerState.pointers.values()];
      pointerState.lastPinch = Math.hypot(b.x - a.x, b.y - a.y);
      pointerState.lastX = 0.5 * (a.x + b.x);
      pointerState.lastY = 0.5 * (a.y + b.y);
    }
  });

  canvas.addEventListener("pointermove", (e) => {
    if (!pointerState.pointers.has(e.pointerId)) return;
    pointerState.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointerState.pointers.size === 1) {
      const dx = e.clientX - pointerState.lastX;
      const dy = e.clientY - pointerState.lastY;
      pointerState.lastX = e.clientX;
      pointerState.lastY = e.clientY;
      if (pointerState.mode === "pan") {
        panByPixels(dx, dy);
      } else {
        cam.yaw = wrapAngleDeg(cam.yaw - dx * 0.34);
        cam.pitch = clamp(cam.pitch + dy * 0.28, 8, 84);
        state.camPreset = "";
        syncCamButtons();
      }
    } else if (pointerState.pointers.size === 2) {
      const [a, b] = [...pointerState.pointers.values()];
      const cx = 0.5 * (a.x + b.x), cy = 0.5 * (a.y + b.y);
      const dist = Math.hypot(b.x - a.x, b.y - a.y);
      if (pointerState.lastPinch > 1 && dist > 1) {
        cam.zoom = clamp(cam.zoom * (pointerState.lastPinch / dist), 0.22, 1.95);
      }
      panByPixels(cx - pointerState.lastX, cy - pointerState.lastY);
      pointerState.lastPinch = dist;
      pointerState.lastX = cx;
      pointerState.lastY = cy;
    }
  });

  const endPointer = (e) => {
    pointerState.pointers.delete(e.pointerId);
    if (pointerState.pointers.size === 0) pointerState.dragging = false;
    else if (pointerState.pointers.size === 1) {
      const [rem] = [...pointerState.pointers.values()];
      pointerState.lastX = rem.x;
      pointerState.lastY = rem.y;
      pointerState.mode = "orbit";
    }
  };
  canvas.addEventListener("pointerup", endPointer);
  canvas.addEventListener("pointercancel", endPointer);
  canvas.addEventListener("lostpointercapture", endPointer);

  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    camGoal = null;
    const factor = Math.exp(clamp(e.deltaY, -180, 180) * 0.0014);
    cam.zoom = clamp(cam.zoom * factor, 0.22, 1.95);
  }, { passive: false });

  // --- UI Synchronization ---
  function syncCamButtons() {
    document.querySelectorAll("#demo-dock [data-cam]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.cam === state.camPreset));
    });
  }

  function syncFocusButtons() {
    document.querySelectorAll("#demo-focus-grid [data-focus]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.focus === state.focusId));
    });
    const sc = SCENARIOS[state.scenarioId];
    const activeFocus = sc && sc.focuses.find((f) => f.id === state.focusId);
    $("focus-readout").textContent = activeFocus ? activeFocus.label : "Custom";
  }

  function syncUi() {
    document.querySelectorAll("#scenario-group [data-scenario]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.scenario === state.scenarioId));
    });
    document.querySelectorAll("#demo-dock [data-proj]").forEach((b) => {
      b.setAttribute("aria-pressed", String(b.dataset.proj === cam.projection));
    });
    $("btn-play").setAttribute("aria-pressed", String(state.playing));
    $("btn-orbit").setAttribute("aria-pressed", String(state.orbit));
    $("btn-patrols").setAttribute("aria-pressed", String(state.patrols));
    $("btn-pixel-mode").setAttribute("aria-pressed", String(state.pixelMode));
    $("btn-hud").setAttribute("aria-pressed", String(state.showHud));
    $("demo-hud-card").hidden = !state.showHud;
    canvas.classList.toggle("pixel-mode", state.pixelMode);
    populateLightingSelect($("select-lighting"), state.lighting);
    syncCamButtons();
    syncFocusButtons();
  }

  // --- Bind Controls ---
  document.querySelectorAll("#scenario-group [data-scenario]").forEach((btn) => {
    btn.addEventListener("click", () => {
      if (btn.dataset.scenario !== state.scenarioId) activateScenario(btn.dataset.scenario, false);
    });
  });

  document.querySelectorAll("#demo-dock [data-cam]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.dataset.cam;
      const p = CAM_PRESETS[key];
      if (!p) return;
      state.camPreset = key;
      state.focusId = key === "rts" ? "overview" : "";
      setCamGoal(p.target, p.yaw, p.pitch, p.zoom);
      syncUi();
      setStatus(`Camera set to ${p.label}`);
    });
  });

  document.querySelectorAll("#demo-dock [data-proj]").forEach((btn) => {
    btn.addEventListener("click", () => {
      cam.projection = btn.dataset.proj === "ortho" ? "ortho" : "perspective";
      syncUi();
      setStatus(`Projection set to ${cam.projection === "ortho" ? "Orthographic" : "Perspective"}`);
    });
  });

  $("btn-play").addEventListener("click", () => {
    state.playing = !state.playing;
    state.fixedTime = false;
    syncUi();
    setStatus(state.playing ? "Scene animation playing" : "Scene animation paused");
  });

  $("btn-orbit").addEventListener("click", () => {
    state.orbit = !state.orbit;
    syncUi();
    setStatus(state.orbit ? "Cinematic camera orbit on" : "Cinematic camera orbit off");
  });

  $("btn-reset").addEventListener("click", () => {
    state.camPreset = "rts";
    state.focusId = "overview";
    const p = CAM_PRESETS.rts;
    setCamGoal(p.target, p.yaw, p.pitch, p.zoom);
    syncUi();
    setStatus("Camera reset to RTS 45° overview");
  });

  $("btn-patrols").addEventListener("click", () => {
    state.patrols = !state.patrols;
    syncUi();
    setStatus(state.patrols ? "Live ant patrols enabled" : "Ants holding formation");
  });

  $("btn-pixel-mode").addEventListener("click", () => {
    state.pixelMode = !state.pixelMode;
    syncUi();
    setStatus(state.pixelMode ? "1998 retro pixel mode enabled" : "Crisp HD 3D rendering enabled");
  });

  $("btn-hud").addEventListener("click", () => {
    state.showHud = !state.showHud;
    syncUi();
  });

  const selectLighting = $("select-lighting");
  selectLighting.addEventListener("change", () => {
    state.lighting = validLighting(selectLighting.value) || "daylight";
    syncUi();
  });
  const stepLighting = (dir) => {
    const idx = Math.max(0, LIGHTING_ORDER.indexOf(state.lighting));
    state.lighting = LIGHTING_ORDER[(idx + dir + LIGHTING_ORDER.length) % LIGHTING_ORDER.length];
    syncUi();
  };
  $("btn-light-prev").addEventListener("click", () => stepLighting(-1));
  $("btn-light-next").addEventListener("click", () => stepLighting(1));

  // Responsive controls panel drawer (same interaction pattern as items.html / viewer.html)
  const panelEl = $("inspector-panel");
  const backdropEl = $("inspector-backdrop");
  const toggleBtn = $("btn-toggle-panel");
  let panelOpen = window.innerWidth > 960;
  function applyPanelState() {
    const narrow = window.innerWidth <= 960;
    panelEl.classList.toggle("is-collapsed", !panelOpen);
    panelEl.classList.toggle("is-open", narrow && panelOpen);
    backdropEl.hidden = !(narrow && panelOpen);
    toggleBtn.setAttribute("aria-expanded", String(panelOpen));
  }
  toggleBtn.addEventListener("click", () => { panelOpen = !panelOpen; applyPanelState(); });
  $("btn-close-panel").addEventListener("click", () => { panelOpen = false; applyPanelState(); });
  backdropEl.addEventListener("click", () => { panelOpen = false; applyPanelState(); });
  window.addEventListener("resize", applyPanelState);
  applyPanelState();

  // Keyboard shortcuts
  window.addEventListener("keydown", (e) => {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const tag = document.activeElement && document.activeElement.tagName;
    if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
    if (e.key >= "1" && e.key <= "4") {
      const targetId = SCENARIO_ORDER[Number(e.key) - 1];
      if (targetId) { e.preventDefault(); activateScenario(targetId, false); }
    } else if (e.key === " " || e.key.toLowerCase() === "i") {
      if (e.key === " " && tag === "BUTTON") return;
      e.preventDefault();
      $("btn-play").click();
    } else if (e.key.toLowerCase() === "t") {
      e.preventDefault();
      $("btn-orbit").click();
    } else if (e.key.toLowerCase() === "r") {
      e.preventDefault();
      $("btn-reset").click();
    } else if (e.key.toLowerCase() === "l") {
      e.preventDefault();
      stepLighting(1);
    } else if (e.key.toLowerCase() === "p") {
      e.preventDefault();
      $("btn-pixel-mode").click();
    } else if (document.activeElement === canvas) {
      if (e.key === "ArrowLeft")  { e.preventDefault(); camGoal = null; cam.yaw = wrapAngleDeg(cam.yaw + 8); }
      if (e.key === "ArrowRight") { e.preventDefault(); camGoal = null; cam.yaw = wrapAngleDeg(cam.yaw - 8); }
      if (e.key === "ArrowUp")    { e.preventDefault(); camGoal = null; cam.pitch = clamp(cam.pitch + 5, 8, 84); }
      if (e.key === "ArrowDown")  { e.preventDefault(); camGoal = null; cam.pitch = clamp(cam.pitch - 5, 8, 84); }
      if (e.key === "+" || e.key === "=") { e.preventDefault(); camGoal = null; cam.zoom = clamp(cam.zoom * 0.88, 0.22, 1.95); }
      if (e.key === "-" || e.key === "_") { e.preventDefault(); camGoal = null; cam.zoom = clamp(cam.zoom * 1.14, 0.22, 1.95); }
    }
  });

  // Only install roving-tabindex fallback when native focusgroup is unsupported.
  if (!("focusGroup" in HTMLElement.prototype)) {
    installToolbarRovingTabindex($("demo-toolbar"));
    installToolbarRovingTabindex($("demo-dock"));
  }

  // --- Main Render Loop ---
  let lastTs = performance.now();
  let fpsAccum = 0, fpsFrames = 0;
  function frame(ts) {
    const dt = Math.min(0.05, Math.max(0.001, (ts - lastTs) * 0.001));
    lastTs = ts;
    updateCamera(dt);
    updateSimulation(dt);

    if (activeScene) {
      const [w, h] = resizeCanvas();
      renderer.drawScene(activeScene, cam, {
        width: w,
        height: h,
        lighting: state.lighting,
        dark: isDark,
        team: "green",
        idleAnim: state.playing,
        fixedIdleTime: state.playing ? null : state.simTime,
        clipName: "Idle",
        showPlinth: false,
        worldSun: true,
        customTarget: cam.target,
        ground: activeScene.groundOpts,
      });
    }

    fpsAccum += dt;
    fpsFrames++;
    if (fpsAccum >= 0.6) {
      const fps = Math.round(fpsFrames / fpsAccum);
      $("meta-fps").textContent = `${fps} fps`;
      $("meta-cam").textContent = `${Math.round(cam.yaw)}° / ${Math.round(cam.pitch)}° · ${cam.zoom.toFixed(2)}×`;
      fpsAccum = 0;
      fpsFrames = 0;
    }
    requestAnimationFrame(frame);
  }

  // Expose light inspection hook for automated visual snapshots
  window.__demoState = {
    get ready() { return Boolean(activeScene && $("demo-loading").hidden); },
    activateScenario,
    setFocus(id) {
      const sc = SCENARIOS[state.scenarioId];
      const f = sc && sc.focuses.find((x) => x.id === id);
      if (f) {
        state.focusId = f.id;
        cam.target = f.target.slice();
        cam.yaw = f.yaw; cam.pitch = f.pitch; cam.zoom = f.zoom;
        camGoal = null;
        syncUi();
      }
    },
    setLighting(l) { state.lighting = validLighting(l) || "daylight"; syncUi(); },
    setCam(yaw, pitch, zoom, target) {
      camGoal = null;
      cam.yaw = yaw; cam.pitch = pitch; cam.zoom = zoom;
      if (target) cam.target = target.slice();
    },
  };

  syncUi();
  activateScenario(state.scenarioId, true);
  requestAnimationFrame(frame);
})();

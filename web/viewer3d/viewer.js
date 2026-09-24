// ============================================================================
// Realistic 3D Ants Title Scene - Three.js WebGL Engine
// Authentic stylized realism with PBR materials, multi-point lighting & inspector
// ============================================================================

(function () {
  'use strict';

  // --- Scene, Camera, Renderer ---
  let scene, camera, renderer, controls;
  const container = document.getElementById('canvas-container');
  let antsGroup, backdropGroup, logoGroup, lightsGroup;
  const antInstances = {};
  let isAnimating = true;
  let isTurntable = false;
  let isWireframe = false;
  let clock = new THREE.Clock();

  // Animation interpolation state
  const cameraLerp = {
    active: false,
    startPos: new THREE.Vector3(),
    targetPos: new THREE.Vector3(),
    startLook: new THREE.Vector3(),
    targetLook: new THREE.Vector3(),
    progress: 0,
    duration: 1.2
  };

  // Ant caste database
  const CASTE_DATA = {
    overview: {
      name: "Classic Squad Overview",
      desc: "Faithful 3D recreation of the classic 1998 cover art featuring all 6 specialized ant castes staged in authentic formation.",
      role: "Title Cover Scene",
      stats: { "Castes": "6 Unique Units", "Style": "Stylized Realism", "Lighting": "Studio 5-Point" }
    },
    combat: {
      name: "Combat Ant",
      desc: "Top-left squad leader with broad muscular carapace, heavy brow scowl, and crossed ammo bandolier sash (X-strap) packed with golden cartridge shells.",
      role: "Heavy Melee & Guard",
      stats: { "Health": "35 HP", "Attack": "High Claws", "Trait": "X-Sash Bandolier" }
    },
    fire: {
      name: "Fire Ant",
      desc: "Back-right squad protector wearing a sculpted yellow firefighter helmet with reinforced crown ridge and bold red letter 'A' crest badge.",
      role: "Flame Spray & Fire Extinguish",
      stats: { "Health": "25 HP", "Special": "Fire Stream", "Trait": "Rescue Helmet 'A'" }
    },
    thief: {
      name: "Thief Ant",
      desc: "Bottom-right stealth raider with bandit bandana mask wrapped across forehead and brow, showing sharp mischievous eyes through the cutouts.",
      role: "Infiltration & Larva Steal",
      stats: { "Speed": "Very Fast", "Stealth": "High", "Trait": "Eye-Cutout Bandana" }
    },
    worker: {
      name: "Worker Ant",
      desc: "Dead center backbone of the colony. Features youthful proportions, large curious compound eyes, and glistening unadorned emerald carapace.",
      role: "Resource Gatherer & Digger",
      stats: { "Health": "20 HP", "Capacity": "Full Sugar", "Trait": "Youthful Big Eyes" }
    },
    bomber: {
      name: "Bomber Ant",
      desc: "Front-and-center demolition specialist wearing yellow circular aviator goggles and carrying a utility backpack packed with classic cartoon spherical bombs.",
      role: "Demolition & Area Splash",
      stats: { "Damage": "Devastating", "Gear": "Yellow Goggles", "Trait": "Cartoon Bomb Pack" }
    },
    swimmer: {
      name: "Swimmer Ant",
      desc: "Left-hand aquatic explorer equipped with yellow scuba diving mask, curved snorkel tube, and gripping an excavation shovel in his right hand.",
      role: "Water Traverser & Builder",
      stats: { "Terrain": "Deep Water", "Tool": "Steel Shovel", "Trait": "Diving Mask & Snorkel" }
    }
  };

  // Camera presets
  const CAMERA_PRESETS = {
    cover: { pos: [0.0, 4.2, 9.0], look: [0.0, 0.15, 0.0] },
    combat: { pos: [-1.4, 2.2, 3.8], look: [-1.25, 1.05, -0.75] },
    fire: { pos: [1.3, 2.1, 3.6], look: [1.20, 0.95, -0.70] },
    swimmer: { pos: [-1.6, 1.4, 3.2], look: [-1.45, 0.35, 0.25] },
    worker: { pos: [0.0, 1.4, 3.5], look: [0.0, 0.40, -0.10] },
    thief: { pos: [1.4, 1.2, 3.0], look: [1.25, 0.05, 0.20] },
    bomber: { pos: [-0.2, 1.6, 2.2], look: [-0.20, -0.35, 0.75] }
  };

  // --- Procedural Textures & Materials ---
  function createChitinBumpTexture() {
    const size = 512;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');

    // Subtle organic cellular noise
    ctx.fillStyle = '#808080';
    ctx.fillRect(0, 0, size, size);

    for (let i = 0; i < 6000; i++) {
      const x = Math.random() * size;
      const y = Math.random() * size;
      const r = Math.random() * 2.5 + 0.5;
      const shade = Math.floor(Math.random() * 50 + 105);
      ctx.fillStyle = `rgb(${shade},${shade},${shade})`;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.fill();
    }

    const texture = new THREE.CanvasTexture(canvas);
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;
    texture.repeat.set(6, 6);
    return texture;
  }

  const bumpMap = createChitinBumpTexture();

  // Authentic rich emerald/forest green chitin with waxy clearcoat sheen
  const chitinMat = new THREE.MeshPhysicalMaterial({
    color: 0x274e27,
    emissive: 0x051205,
    roughness: 0.28,
    metalness: 0.12,
    clearcoat: 0.85,
    clearcoatRoughness: 0.15,
    bumpMap: bumpMap,
    bumpScale: 0.015,
    sheen: new THREE.Color(0x55cc44)
  });

  const darkChitinMat = new THREE.MeshPhysicalMaterial({
    color: 0x122412,
    roughness: 0.38,
    metalness: 0.18,
    clearcoat: 0.7,
    clearcoatRoughness: 0.2
  });

  const eyeWhiteMat = new THREE.MeshPhysicalMaterial({
    color: 0xf5f8f5,
    roughness: 0.12,
    metalness: 0.0,
    clearcoat: 1.0,
    clearcoatRoughness: 0.04
  });

  const eyePupilMat = new THREE.MeshPhysicalMaterial({
    color: 0x080c08,
    roughness: 0.05,
    metalness: 0.0,
    clearcoat: 1.0,
    clearcoatRoughness: 0.02
  });

  // --- Procedural Ant Character Assembler ---
  function createBaseAnt(options = {}) {
    const root = new THREE.Group();
    const scale = options.scale || 1.0;
    root.scale.set(scale, scale, scale);

    const bodyGroup = new THREE.Group();
    root.add(bodyGroup);

    // 1. Thorax (Mesosoma) - Segmented muscular insect chest
    const thoraxGroup = new THREE.Group();
    bodyGroup.add(thoraxGroup);

    const protoThoraxGeo = new THREE.SphereGeometry(0.55, 24, 20);
    protoThoraxGeo.scale(0.85, 0.9, 1.25);
    const protoThorax = new THREE.Mesh(protoThoraxGeo, chitinMat);
    protoThorax.castShadow = true;
    protoThorax.receiveShadow = true;
    thoraxGroup.add(protoThorax);

    const mesoThoraxGeo = new THREE.SphereGeometry(0.48, 20, 16);
    mesoThoraxGeo.scale(0.8, 0.8, 1.0);
    const mesoThorax = new THREE.Mesh(mesoThoraxGeo, chitinMat);
    mesoThorax.position.set(0, -0.05, -0.55);
    mesoThorax.castShadow = true;
    thoraxGroup.add(mesoThorax);

    // 2. Petiole (Waist Node)
    const petioleGeo = new THREE.CylinderGeometry(0.12, 0.15, 0.35, 12);
    petioleGeo.rotateX(Math.PI / 3);
    const petiole = new THREE.Mesh(petioleGeo, darkChitinMat);
    petiole.position.set(0, -0.1, -1.0);
    thoraxGroup.add(petiole);

    // 3. Gaster (Abdomen)
    const gasterGroup = new THREE.Group();
    gasterGroup.position.set(0, -0.15, -1.4);
    thoraxGroup.add(gasterGroup);

    const gasterGeo = new THREE.SphereGeometry(0.95, 28, 24);
    gasterGeo.scale(0.85, 0.95, 1.45);
    const gaster = new THREE.Mesh(gasterGeo, chitinMat);
    gaster.position.set(0, 0.15, -0.6);
    gaster.rotation.x = -Math.PI / 10;
    gaster.castShadow = true;
    gaster.receiveShadow = true;
    gasterGroup.add(gaster);

    // Segmented ring grooves on gaster
    for (let r = 0; r < 4; r++) {
      const ringGeo = new THREE.TorusGeometry(0.82 - r * 0.12, 0.035, 10, 32);
      ringGeo.scale(0.86, 0.96, 1.0);
      const ring = new THREE.Mesh(ringGeo, darkChitinMat);
      ring.position.set(0, 0.18 + r * 0.05, -0.3 - r * 0.32);
      ring.rotation.x = -Math.PI / 10;
      gasterGroup.add(ring);
    }

    // 4. Neck & Head
    const neckGeo = new THREE.CylinderGeometry(0.2, 0.25, 0.35, 16);
    neckGeo.rotateX(-Math.PI / 5);
    const neck = new THREE.Mesh(neckGeo, darkChitinMat);
    neck.position.set(0, 0.22, 0.55);
    thoraxGroup.add(neck);

    const headGroup = new THREE.Group();
    headGroup.position.set(0, 0.42, 0.85);
    thoraxGroup.add(headGroup);

    // Expressive stylized ant head
    const headGeo = new THREE.SphereGeometry(0.68, 28, 24);
    headGeo.scale(0.95, 1.15, 1.05);
    const head = new THREE.Mesh(headGeo, chitinMat);
    head.castShadow = true;
    head.receiveShadow = true;
    headGroup.add(head);

    // Mandibles (Jaws)
    const mandibleMat = new THREE.MeshPhysicalMaterial({
      color: 0x142014,
      roughness: 0.25,
      metalness: 0.3,
      clearcoat: 0.9
    });

    const leftMandibleGeo = new THREE.ConeGeometry(0.12, 0.45, 12);
    leftMandibleGeo.rotateX(Math.PI / 2.3);
    leftMandibleGeo.rotateZ(-Math.PI / 6);
    const leftMandible = new THREE.Mesh(leftMandibleGeo, mandibleMat);
    leftMandible.position.set(-0.24, -0.48, 0.6);
    headGroup.add(leftMandible);

    const rightMandibleGeo = new THREE.ConeGeometry(0.12, 0.45, 12);
    rightMandibleGeo.rotateX(Math.PI / 2.3);
    rightMandibleGeo.rotateZ(Math.PI / 6);
    const rightMandible = new THREE.Mesh(rightMandibleGeo, mandibleMat);
    rightMandible.position.set(0.24, -0.48, 0.6);
    headGroup.add(rightMandible);

    // Eyes
    const eyeRadius = options.bigEyes ? 0.32 : 0.26;
    const eyeY = 0.15;
    const eyeZ = 0.45;
    const eyeX = 0.42;

    function buildEye(isRight, scowlAngle = 0, squint = 1.0) {
      const eyeGroup = new THREE.Group();
      const sign = isRight ? 1 : -1;
      eyeGroup.position.set(sign * eyeX, eyeY, eyeZ);

      // Sclera
      const scleraGeo = new THREE.SphereGeometry(eyeRadius, 24, 20);
      scleraGeo.scale(1.0, squint, 1.0);
      const sclera = new THREE.Mesh(scleraGeo, eyeWhiteMat);
      eyeGroup.add(sclera);

      // Pupil
      const pupilGeo = new THREE.SphereGeometry(eyeRadius * 0.65, 16, 16);
      pupilGeo.scale(0.85, squint * 0.9, 0.3);
      const pupil = new THREE.Mesh(pupilGeo, eyePupilMat);
      pupil.position.set(0, 0, eyeRadius * 0.85);
      eyeGroup.add(pupil);

      // Specular highlight glints
      const glintMat = new THREE.MeshBasicMaterial({ color: 0xffffff });
      const glint1 = new THREE.Mesh(new THREE.SphereGeometry(eyeRadius * 0.2, 10, 10), glintMat);
      glint1.position.set(-eyeRadius * 0.25, eyeRadius * 0.3 * squint, eyeRadius * 0.95);
      eyeGroup.add(glint1);

      const glint2 = new THREE.Mesh(new THREE.SphereGeometry(eyeRadius * 0.1, 8, 8), glintMat);
      glint2.position.set(eyeRadius * 0.2, -eyeRadius * 0.2 * squint, eyeRadius * 0.95);
      eyeGroup.add(glint2);

      // Scowl eyelid if applicable
      if (scowlAngle !== 0) {
        const lidMat = new THREE.MeshStandardMaterial({ color: 0x204420, roughness: 0.4 });
        const lidGeo = new THREE.SphereGeometry(eyeRadius * 1.05, 18, 14, 0, Math.PI * 2, 0, Math.PI / 2.5);
        const lid = new THREE.Mesh(lidGeo, lidMat);
        lid.rotation.z = sign * scowlAngle;
        lid.rotation.x = 0.3;
        lid.position.set(0, eyeRadius * 0.2, 0);
        eyeGroup.add(lid);
      }

      return eyeGroup;
    }

    const scowl = options.scowl ? 0.45 : 0.0;
    const squint = options.squint || 1.0;
    const leftEye = buildEye(false, scowl, squint);
    const rightEye = buildEye(true, scowl, squint);
    headGroup.add(leftEye);
    headGroup.add(rightEye);

    // Antennae (Segmented elbowed insect antennae)
    const antennaGroup = new THREE.Group();
    headGroup.add(antennaGroup);

    function createAntenna(isRight) {
      const ant = new THREE.Group();
      const sign = isRight ? 1 : -1;
      ant.position.set(sign * 0.22, 0.65, 0.35);

      // Scape (base segment)
      const scapeGeo = new THREE.CylinderGeometry(0.04, 0.05, 0.6, 8);
      scapeGeo.translate(0, 0.3, 0);
      const scape = new THREE.Mesh(scapeGeo, darkChitinMat);
      scape.rotation.z = sign * -0.45;
      scape.rotation.x = -0.2;
      ant.add(scape);

      // Flagellum (elbowed tip curving gracefully)
      const flagellumGeo = new THREE.CylinderGeometry(0.025, 0.04, 0.85, 8);
      flagellumGeo.translate(0, 0.42, 0);
      const flagellum = new THREE.Mesh(flagellumGeo, chitinMat);
      flagellum.position.set(sign * -0.25, 0.55, 0.1);
      flagellum.rotation.z = sign * 0.55;
      flagellum.rotation.x = -0.35;
      ant.add(flagellum);

      // Antenna tip bulb
      const tipGeo = new THREE.SphereGeometry(0.055, 12, 12);
      const tip = new THREE.Mesh(tipGeo, chitinMat);
      tip.position.set(sign * -0.65, 1.25, 0.35);
      ant.add(tip);

      return ant;
    }

    const leftAntenna = createAntenna(false);
    const rightAntenna = createAntenna(true);
    antennaGroup.add(leftAntenna);
    antennaGroup.add(rightAntenna);

    // 5. Six Articulated Legs (Natural insect posture)
    const legsGroup = new THREE.Group();
    thoraxGroup.add(legsGroup);

    function createLeg(pairIndex, isRight) {
      const leg = new THREE.Group();
      const sign = isRight ? 1 : -1;

      const zOffsets = [0.25, -0.15, -0.6];
      const yOffsets = [-0.15, -0.2, -0.25];
      const zAngle = [0.35, 0.0, -0.45];

      leg.position.set(sign * 0.45, yOffsets[pairIndex], zOffsets[pairIndex]);

      // Femur (thigh)
      const femurLen = 0.95 + pairIndex * 0.15;
      const femurGeo = new THREE.CylinderGeometry(0.06, 0.08, femurLen, 8);
      femurGeo.translate(0, femurLen * 0.5, 0);
      const femur = new THREE.Mesh(femurGeo, darkChitinMat);
      femur.rotation.z = sign * -1.05;
      femur.rotation.y = sign * zAngle[pairIndex];
      femur.castShadow = true;
      leg.add(femur);

      // Tibia (shin)
      const tibiaLen = 1.05 + pairIndex * 0.18;
      const tibiaGeo = new THREE.CylinderGeometry(0.035, 0.055, tibiaLen, 8);
      tibiaGeo.translate(0, -tibiaLen * 0.5, 0);
      const tibia = new THREE.Mesh(tibiaGeo, chitinMat);
      tibia.position.set(sign * 0.78, 0.48, 0);
      tibia.rotation.z = sign * 0.45;
      tibia.castShadow = true;
      leg.add(tibia);

      // Tarsus / Foot claw
      const tarsusGeo = new THREE.ConeGeometry(0.04, 0.28, 8);
      tarsusGeo.rotateX(Math.PI);
      const tarsus = new THREE.Mesh(tarsusGeo, darkChitinMat);
      tarsus.position.set(sign * 1.18, -0.48, 0);
      leg.add(tarsus);

      return leg;
    }

    for (let p = 0; p < 3; p++) {
      legsGroup.add(createLeg(p, false));
      legsGroup.add(createLeg(p, true));
    }

    // Attach reference handles
    root.userData = {
      headGroup,
      thoraxGroup,
      gasterGroup,
      leftAntenna,
      rightAntenna,
      legsGroup
    };

    return root;
  }

  // --- Caste Specific Accessories ---

  // 1. Combat Ant: Heavy brow scowl & Crossed Bandolier X-Sash with Gold Cartridges
  function applyCombatGear(ant) {
    const thorax = ant.userData.thoraxGroup;
    const head = ant.userData.headGroup;

    // Heavy brow ridges over eyes (combative scowl)
    const browMat = new THREE.MeshStandardMaterial({ color: 0x1a361a, roughness: 0.5 });
    const browGeo = new THREE.CylinderGeometry(0.08, 0.1, 0.7, 8);
    browGeo.rotateZ(Math.PI / 2);
    const brow = new THREE.Mesh(browGeo, browMat);
    brow.position.set(0, 0.38, 0.68);
    brow.rotation.x = -0.35;
    head.add(brow);

    // Crossed Bandolier Sash forming an 'X' across torso
    const leatherMat = new THREE.MeshStandardMaterial({
      color: 0x3d2010,
      roughness: 0.75,
      metalness: 0.1
    });

    const brassMat = new THREE.MeshStandardMaterial({
      color: 0xf1c40f,
      roughness: 0.22,
      metalness: 0.9
    });

    function createBandolierStrap(yRot, zRot) {
      const strapGroup = new THREE.Group();
      const strapGeo = new THREE.TorusGeometry(0.68, 0.08, 8, 36);
      strapGeo.scale(0.92, 1.28, 0.95);
      const strap = new THREE.Mesh(strapGeo, leatherMat);
      strap.rotation.y = yRot;
      strap.rotation.z = zRot;
      strapGroup.add(strap);

      // Gold cartridge shells along strap
      for (let i = 0; i < 7; i++) {
        const shellGroup = new THREE.Group();
        const shellGeo = new THREE.CylinderGeometry(0.05, 0.05, 0.28, 10);
        const shell = new THREE.Mesh(shellGeo, brassMat);
        const tipGeo = new THREE.ConeGeometry(0.05, 0.1, 10);
        const tip = new THREE.Mesh(tipGeo, brassMat);
        tip.position.y = 0.18;
        shellGroup.add(shell);
        shellGroup.add(tip);

        const a = (i - 3) * 0.28;
        shellGroup.position.set(Math.sin(a) * 0.7, Math.cos(a) * 0.44, 0.36);
        shellGroup.rotation.z = -a;
        strapGroup.add(shellGroup);
      }
      return strapGroup;
    }

    const sashContainer = new THREE.Group();
    sashContainer.position.set(0, 0.05, 0.1);
    sashContainer.add(createBandolierStrap(Math.PI / 7, Math.PI / 4));
    sashContainer.add(createBandolierStrap(-Math.PI / 7, -Math.PI / 4));
    thorax.add(sashContainer);
  }

  // 2. Fire Ant: Firefighter Chief Helmet with "A" Shield Badge
  function applyFireGear(ant) {
    const head = ant.userData.headGroup;

    const helmetGroup = new THREE.Group();
    helmetGroup.position.set(0, 0.52, 0.08);
    helmetGroup.rotation.x = -0.12;

    // Rescue yellow helmet shell
    const helmetMat = new THREE.MeshPhysicalMaterial({
      color: 0xf5b041,
      roughness: 0.2,
      clearcoat: 0.9,
      clearcoatRoughness: 0.08
    });

    // Flared protective brim (dips lower at back)
    const brimGeo = new THREE.CylinderGeometry(0.96, 1.1, 0.12, 32);
    brimGeo.scale(1.0, 1.0, 1.3);
    const brim = new THREE.Mesh(brimGeo, helmetMat);
    brim.position.set(0, 0.1, -0.12);
    helmetGroup.add(brim);

    // High rounded crown dome
    const crownGeo = new THREE.SphereGeometry(0.72, 24, 20);
    crownGeo.scale(0.92, 0.9, 1.05);
    const crown = new THREE.Mesh(crownGeo, helmetMat);
    crown.position.set(0, 0.48, -0.05);
    crown.castShadow = true;
    helmetGroup.add(crown);

    // Central reinforcement ridge along top
    const ridgeGeo = new THREE.BoxGeometry(0.12, 0.24, 1.05);
    const ridge = new THREE.Mesh(ridgeGeo, helmetMat);
    ridge.position.set(0, 0.9, -0.05);
    helmetGroup.add(ridge);

    // Front Shield Plate (Gold shield badge with maroon letter 'A')
    const shieldPlateMat = new THREE.MeshStandardMaterial({
      color: 0xf39c12,
      roughness: 0.3,
      metalness: 0.6
    });

    const shieldGeo = new THREE.CylinderGeometry(0.3, 0.38, 0.48, 6);
    shieldGeo.scale(1.0, 1.0, 0.2);
    const shield = new THREE.Mesh(shieldGeo, shieldPlateMat);
    shield.position.set(0, 0.7, 0.72);
    shield.rotation.x = 0.2;
    helmetGroup.add(shield);

    // Letter 'A' badge emblem in bold maroon
    const letterMat = new THREE.MeshStandardMaterial({ color: 0x8b0000, roughness: 0.35 });
    const barGeo = new THREE.BoxGeometry(0.06, 0.3, 0.05);

    const leftLeg = new THREE.Mesh(barGeo, letterMat);
    leftLeg.position.set(-0.08, 0.7, 0.8);
    leftLeg.rotation.z = -0.28;
    leftLeg.rotation.x = 0.2;
    helmetGroup.add(leftLeg);

    const rightLeg = new THREE.Mesh(barGeo, letterMat);
    rightLeg.position.set(0.08, 0.7, 0.8);
    rightLeg.rotation.z = 0.28;
    rightLeg.rotation.x = 0.2;
    helmetGroup.add(rightLeg);

    const crossBar = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.05, 0.05), letterMat);
    crossBar.position.set(0, 0.68, 0.8);
    crossBar.rotation.x = 0.2;
    helmetGroup.add(crossBar);

    head.add(helmetGroup);
  }

  // 3. Thief Ant: Cloth Bandana Mask with Eye Openings
  function applyThiefGear(ant) {
    const head = ant.userData.headGroup;

    const bandanaGroup = new THREE.Group();
    bandanaGroup.position.set(0, 0.18, 0.12);

    const bandanaMat = new THREE.MeshStandardMaterial({
      color: 0xc89825,
      roughness: 0.8,
      metalness: 0.05
    });

    // Main band wrapped around forehead and eye level
    const bandGeo = new THREE.CylinderGeometry(0.74, 0.76, 0.5, 28, 1, true);
    bandGeo.scale(0.96, 1.0, 1.05);
    const band = new THREE.Mesh(bandGeo, bandanaMat);
    band.rotation.x = -0.1;
    bandanaGroup.add(band);

    // Knot and flowing streamers at side
    const knot = new THREE.Mesh(new THREE.SphereGeometry(0.18, 12, 12), bandanaMat);
    knot.position.set(0.72, 0.08, -0.6);
    bandanaGroup.add(knot);

    const streamerGeo = new THREE.CylinderGeometry(0.06, 0.18, 0.8, 8);
    streamerGeo.scale(1.0, 1.0, 0.3);
    const streamer1 = new THREE.Mesh(streamerGeo, bandanaMat);
    streamer1.position.set(0.85, -0.18, -0.7);
    streamer1.rotation.z = 0.5;
    streamer1.rotation.x = 0.3;
    bandanaGroup.add(streamer1);

    const streamer2 = new THREE.Mesh(streamerGeo, bandanaMat);
    streamer2.position.set(0.78, -0.38, -0.75);
    streamer2.rotation.z = 0.75;
    bandanaGroup.add(streamer2);

    head.add(bandanaGroup);
  }

  // 4. Bomber Ant: Yellow Aviator Goggles & Bomb Utility Backpack
  function applyBomberGear(ant) {
    const head = ant.userData.headGroup;
    const thorax = ant.userData.thoraxGroup;

    // Aviator Goggles
    const gogglesGroup = new THREE.Group();
    gogglesGroup.position.set(0, 0.16, 0.56);

    const yellowRimMat = new THREE.MeshPhysicalMaterial({
      color: 0xf39c12,
      roughness: 0.28,
      metalness: 0.5,
      clearcoat: 0.7
    });

    const lensMat = new THREE.MeshPhysicalMaterial({
      color: 0x99ddff,
      roughness: 0.05,
      metalness: 0.1,
      transmission: 0.75,
      transparent: true,
      opacity: 0.75,
      clearcoat: 1.0
    });

    const strapMat = new THREE.MeshStandardMaterial({ color: 0x222831, roughness: 0.8 });

    function createGoggleEye(isRight) {
      const g = new THREE.Group();
      const sign = isRight ? 1 : -1;
      g.position.x = sign * 0.38;

      // Rim
      const rimGeo = new THREE.TorusGeometry(0.24, 0.065, 12, 28);
      const rim = new THREE.Mesh(rimGeo, yellowRimMat);
      g.add(rim);

      // Glass lens
      const lensGeo = new THREE.CylinderGeometry(0.22, 0.22, 0.04, 24);
      lensGeo.rotateX(Math.PI / 2);
      const lens = new THREE.Mesh(lensGeo, lensMat);
      g.add(lens);

      return g;
    }

    gogglesGroup.add(createGoggleEye(false));
    gogglesGroup.add(createGoggleEye(true));

    const bridge = new THREE.Mesh(new THREE.BoxGeometry(0.25, 0.05, 0.05), yellowRimMat);
    gogglesGroup.add(bridge);

    const strapGeo = new THREE.TorusGeometry(0.7, 0.045, 8, 32);
    strapGeo.scale(0.96, 0.8, 1.05);
    const goggleStrap = new THREE.Mesh(strapGeo, strapMat);
    goggleStrap.position.set(0, 0, -0.2);
    gogglesGroup.add(goggleStrap);

    head.add(gogglesGroup);

    // Utility Backpack with Cartoon Bombs
    const backpackGroup = new THREE.Group();
    backpackGroup.position.set(0, 0.25, -0.65);

    const packMat = new THREE.MeshStandardMaterial({ color: 0x543d2b, roughness: 0.8 });
    const packBody = new THREE.Mesh(new THREE.BoxGeometry(0.75, 0.65, 0.55), packMat);
    backpackGroup.add(packBody);

    // Spherical cast iron cartoon bombs
    const bombMat = new THREE.MeshPhysicalMaterial({
      color: 0x151515,
      roughness: 0.35,
      metalness: 0.7,
      clearcoat: 0.4
    });

    const capMat = new THREE.MeshStandardMaterial({ color: 0xc0392b, roughness: 0.4 });
    const fuseMat = new THREE.MeshStandardMaterial({ color: 0xe0d8c3, roughness: 0.9 });

    function createBomb(x, y, z, rot) {
      const b = new THREE.Group();
      b.position.set(x, y, z);
      b.rotation.z = rot;

      const sphere = new THREE.Mesh(new THREE.SphereGeometry(0.22, 18, 16), bombMat);
      sphere.castShadow = true;
      b.add(sphere);

      const cap = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.07, 0.08, 12), capMat);
      cap.position.y = 0.22;
      b.add(cap);

      const fuseCurve = new THREE.CatmullRomCurve3([
        new THREE.Vector3(0, 0.26, 0),
        new THREE.Vector3(0.08, 0.35, 0),
        new THREE.Vector3(0.04, 0.42, 0.05)
      ]);
      const fuse = new THREE.Mesh(new THREE.TubeGeometry(fuseCurve, 8, 0.02, 6, false), fuseMat);
      b.add(fuse);

      return b;
    }

    backpackGroup.add(createBomb(-0.2, 0.35, -0.05, -0.2));
    backpackGroup.add(createBomb(0.22, 0.38, 0.05, 0.25));

    thorax.add(backpackGroup);
  }

  // 5. Swimmer Ant: Diving Mask, Snorkel & Excavation Shovel
  function applySwimmerGear(ant) {
    const head = ant.userData.headGroup;

    // Scuba Diving Mask with yellow rubber frame
    const maskGroup = new THREE.Group();
    maskGroup.position.set(0, 0.18, 0.6);

    const maskFrameMat = new THREE.MeshPhysicalMaterial({
      color: 0xf1c40f,
      roughness: 0.3,
      clearcoat: 0.6
    });

    const maskGlassMat = new THREE.MeshPhysicalMaterial({
      color: 0xa8e6cf,
      roughness: 0.05,
      metalness: 0.05,
      transmission: 0.85,
      transparent: true,
      opacity: 0.65,
      clearcoat: 1.0
    });

    const maskFrameGeo = new THREE.TorusGeometry(0.48, 0.07, 12, 32);
    maskFrameGeo.scale(1.35, 0.85, 1.0);
    maskGroup.add(new THREE.Mesh(maskFrameGeo, maskFrameMat));

    const glassGeo = new THREE.CylinderGeometry(0.45, 0.45, 0.03, 32);
    glassGeo.scale(1.3, 0.8, 1.0);
    glassGeo.rotateX(Math.PI / 2);
    maskGroup.add(new THREE.Mesh(glassGeo, maskGlassMat));

    const strapGeo = new THREE.TorusGeometry(0.72, 0.04, 8, 32);
    strapGeo.scale(0.96, 0.8, 1.05);
    const strap = new THREE.Mesh(strapGeo, maskFrameMat);
    strap.position.set(0, 0, -0.25);
    maskGroup.add(strap);

    // Snorkel Tube running up the side
    const snorkelCurve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(0.55, -0.25, 0.3),
      new THREE.Vector3(0.75, -0.1, 0.1),
      new THREE.Vector3(0.85, 0.4, 0.0),
      new THREE.Vector3(0.85, 1.0, -0.1),
      new THREE.Vector3(0.85, 1.15, -0.2)
    ]);
    const snorkel = new THREE.Mesh(new THREE.TubeGeometry(snorkelCurve, 20, 0.065, 12, false), maskFrameMat);
    maskGroup.add(snorkel);

    const purgeCap = new THREE.Mesh(new THREE.CylinderGeometry(0.08, 0.08, 0.1, 12), new THREE.MeshStandardMaterial({ color: 0xe67e22, roughness: 0.4 }));
    purgeCap.position.set(0.85, 1.18, -0.2);
    maskGroup.add(purgeCap);

    head.add(maskGroup);

    // Excavation Shovel held proudly in right hand
    const shovelGroup = new THREE.Group();
    shovelGroup.position.set(-1.05, -0.1, 0.6);
    shovelGroup.rotation.z = -0.3;
    shovelGroup.rotation.x = 0.2;

    const woodMat = new THREE.MeshStandardMaterial({ color: 0x8b5a2b, roughness: 0.7 });
    const steelMat = new THREE.MeshStandardMaterial({
      color: 0xcfd8dc,
      roughness: 0.25,
      metalness: 0.9
    });

    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.045, 2.0, 10), woodMat);
    shovelGroup.add(shaft);

    const dGripGroup = new THREE.Group();
    dGripGroup.position.set(0, 1.0, 0);
    const gripCross = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 0.25, 8), steelMat);
    gripCross.rotation.z = Math.PI / 2;
    dGripGroup.add(gripCross);
    shovelGroup.add(dGripGroup);

    // Curved metallic spade blade on bottom
    const bladeGeo = new THREE.BoxGeometry(0.48, 0.6, 0.05);
    bladeGeo.translate(0, -0.3, 0);
    const blade = new THREE.Mesh(bladeGeo, steelMat);
    blade.position.set(0, -1.0, 0);
    blade.castShadow = true;
    shovelGroup.add(blade);

    ant.add(shovelGroup);
  }

  // --- Environment: Crimson Backdrop Glow, Floor & 3D Title Logo ---
  function setupEnvironment() {
    backdropGroup = new THREE.Group();
    scene.add(backdropGroup);

    // 1. Crimson / Vermilion Spotlight Backdrop Disc
    const canvas = document.createElement('canvas');
    canvas.width = 1024;
    canvas.height = 1024;
    const ctx = canvas.getContext('2d');
    const grad = ctx.createRadialGradient(512, 512, 60, 512, 512, 480);
    grad.addColorStop(0.0, '#ff3b14');
    grad.addColorStop(0.45, '#d61818');
    grad.addColorStop(0.8, '#880000');
    grad.addColorStop(1.0, '#000000');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 1024, 1024);

    const discTexture = new THREE.CanvasTexture(canvas);
    const discGeo = new THREE.PlaneGeometry(16, 13);
    const discMat = new THREE.MeshBasicMaterial({
      map: discTexture,
      transparent: true,
      depthWrite: false
    });
    const backdropDisc = new THREE.Mesh(discGeo, discMat);
    backdropDisc.position.set(0, 0.5, -4.0);
    backdropGroup.add(backdropDisc);

    // 2. Studio Shadow Pedestal Ground (seamlessly blending into the black void)
    const groundGeo = new THREE.PlaneGeometry(50, 50);
    const groundMat = new THREE.MeshStandardMaterial({
      color: 0x07090c,
      roughness: 0.95,
      metalness: 0.05
    });
    const ground = new THREE.Mesh(groundGeo, groundMat);
    ground.rotation.x = -Math.PI / 2;
    ground.position.y = -2.8;
    ground.receiveShadow = true;
    backdropGroup.add(ground);

    // 3. Sculpted 3D "Ants!" Title Foreground Logo Plaque
    logoGroup = new THREE.Group();
    logoGroup.position.set(0, -2.15, 2.8);
    logoGroup.rotation.x = -0.32; // Angled back toward camera
    scene.add(logoGroup);

    // High-res typography canvas
    const logoCanvas = document.createElement('canvas');
    logoCanvas.width = 1024;
    logoCanvas.height = 360;
    const lctx = logoCanvas.getContext('2d');

    // Transparent background
    lctx.clearRect(0, 0, 1024, 360);

    // Gradient text
    const textGrad = lctx.createLinearGradient(120, 0, 900, 0);
    textGrad.addColorStop(0.0, '#4facfe');
    textGrad.addColorStop(0.35, '#00f2fe');
    textGrad.addColorStop(0.7, '#7f00ff');
    textGrad.addColorStop(1.0, '#e100ff');

    // Drop shadow
    lctx.shadowColor = 'rgba(0, 0, 0, 0.9)';
    lctx.shadowBlur = 18;
    lctx.shadowOffsetX = 6;
    lctx.shadowOffsetY = 10;

    // Stylized font rendering
    lctx.font = '900 240px -apple-system, BlinkMacSystemFont, "Arial Black", Impact, sans-serif';
    lctx.textAlign = 'center';
    lctx.textBaseline = 'middle';

    // Stroke outline
    lctx.lineWidth = 14;
    lctx.strokeStyle = '#ffffff';
    lctx.strokeText('ants!', 512, 180);

    // Inner gradient fill
    lctx.fillStyle = textGrad;
    lctx.fillText('ants!', 512, 180);

    const logoTexture = new THREE.CanvasTexture(logoCanvas);
    logoTexture.generateMipmaps = true;

    // 3D Plaque Mesh
    const plaqueGeo = new THREE.PlaneGeometry(4.8, 1.7);
    const plaqueMat = new THREE.MeshBasicMaterial({
      map: logoTexture,
      transparent: true,
      depthWrite: false
    });
    const logoMesh = new THREE.Mesh(plaqueGeo, plaqueMat);
    logoGroup.add(logoMesh);
  }

  // --- Multi-Point Studio Lighting Rig ("I want good lighting too") ---
  function setupLighting() {
    lightsGroup = new THREE.Group();
    scene.add(lightsGroup);

    // Hemisphere Ambient
    const hemiLight = new THREE.HemisphereLight(0x445577, 0x111122, 0.45);
    lightsGroup.add(hemiLight);

    // Key Light (Warm 45-degree sunlight with contact-hardening shadows)
    const keyLight = new THREE.DirectionalLight(0xfff5ea, 1.45);
    keyLight.position.set(5, 8, 8);
    keyLight.castShadow = true;
    keyLight.shadow.mapSize.width = 2048;
    keyLight.shadow.mapSize.height = 2048;
    keyLight.shadow.camera.near = 1.0;
    keyLight.shadow.camera.far = 30;
    keyLight.shadow.camera.left = -8;
    keyLight.shadow.camera.right = 8;
    keyLight.shadow.camera.top = 8;
    keyLight.shadow.camera.bottom = -8;
    keyLight.shadow.bias = -0.0006;
    lightsGroup.add(keyLight);

    // Cool Sky-Fill Light
    const fillLight = new THREE.DirectionalLight(0x70a0d0, 0.55);
    fillLight.position.set(-7, 2, 5);
    lightsGroup.add(fillLight);

    // Golden Rim Light (Back-Left edge highlights)
    const rimGold = new THREE.DirectionalLight(0xffa834, 1.9);
    rimGold.position.set(-8, 6, -6);
    lightsGroup.add(rimGold);

    // Cyan Rim Light (Back-Right edge highlights)
    const rimCyan = new THREE.DirectionalLight(0x33ddff, 1.45);
    rimCyan.position.set(8, 5, -5);
    lightsGroup.add(rimCyan);

    // Backdrop Spotlight: Rich Crimson/Red glow directly onto the disc
    const backdropLight = new THREE.PointLight(0xff2200, 3.2, 18);
    backdropLight.position.set(0, 0.8, -2.5);
    lightsGroup.add(backdropLight);
  }

  // Lighting modes
  function setLightingMode(mode) {
    if (!lightsGroup) return;
    lightsGroup.children.forEach(light => {
      if (light.isDirectionalLight || light.isPointLight) {
        if (mode === 'sunset') {
          if (light.color) light.color.set(0xff7733);
        } else if (mode === 'neon') {
          if (light.color) light.color.set(0x00ffcc);
        } else if (mode === 'studio') {
          if (light.color) light.color.set(0xffffff);
        } else {
          setupLighting();
        }
      }
    });
  }

  // --- Squad Staging (Faithful to Title Cover Art) ---
  let glbScene = null;

  function assembleSquad() {
    antsGroup = new THREE.Group();
    scene.add(antsGroup);

    // Load authentic master Blender GLB scene
    if (typeof THREE.GLTFLoader !== 'undefined') {
      const loader = new THREE.GLTFLoader();
      loader.load('ants_scene.glb', (gltf) => {
        glbScene = gltf.scene;
        glbScene.traverse(child => {
          if (child.isMesh) {
            child.castShadow = true;
            child.receiveShadow = true;
          }
        });
        scene.add(glbScene);
        // Hide procedural fallback group once master GLB is loaded
        antsGroup.visible = false;
        if (backdropGroup) backdropGroup.visible = false;
        if (logoGroup) logoGroup.visible = false;
        console.log("Loaded authentic 3D Blender GLB scene successfully!");
      }, undefined, (err) => {
        console.warn("GLB load notice (using procedural fallback):", err);
      });
    }

    // 1. Combat Ant (Top-Left, elevated, muscular posture, X-sash)
    const combatAnt = createBaseAnt({ scale: 1.22, scowl: true });
    combatAnt.position.set(-2.1, 1.6, -0.6);
    combatAnt.rotation.y = 0.28;
    combatAnt.rotation.x = 0.12;
    applyCombatGear(combatAnt);
    antsGroup.add(combatAnt);
    antInstances.combat = combatAnt;

    // 2. Fire Ant (Back-Right, elevated, firefighter helmet with 'A')
    const fireAnt = createBaseAnt({ scale: 1.12 });
    fireAnt.position.set(2.0, 1.8, -1.0);
    fireAnt.rotation.y = -0.28;
    fireAnt.rotation.x = 0.1;
    applyFireGear(fireAnt);
    antsGroup.add(fireAnt);
    antInstances.fire = fireAnt;

    // 3. Thief Ant (Bottom-Right, bandit bandana mask with squinting eyes)
    const thiefAnt = createBaseAnt({ scale: 0.98, squint: 0.7 });
    thiefAnt.position.set(2.2, -0.5, 0.7);
    thiefAnt.rotation.y = -0.36;
    thiefAnt.rotation.x = -0.05;
    applyThiefGear(thiefAnt);
    antsGroup.add(thiefAnt);
    antInstances.thief = thiefAnt;

    // 4. Worker Ant (Dead Center, youthful proportions, big innocent eyes)
    const workerAnt = createBaseAnt({ scale: 1.0, bigEyes: true });
    workerAnt.position.set(0.0, 0.45, 0.15);
    workerAnt.rotation.y = 0.0;
    antsGroup.add(workerAnt);
    antInstances.worker = workerAnt;

    // 5. Bomber Ant (Front-Center, lowest, yellow goggles, bomb backpack)
    const bomberAnt = createBaseAnt({ scale: 0.94 });
    bomberAnt.position.set(-0.35, -1.35, 1.7);
    bomberAnt.rotation.y = 0.15;
    bomberAnt.rotation.x = -0.24; // Looking up toward camera
    applyBomberGear(bomberAnt);
    antsGroup.add(bomberAnt);
    antInstances.bomber = bomberAnt;

    // 6. Swimmer Ant (Middle-Left, mask, snorkel, holding shovel in right hand)
    const swimmerAnt = createBaseAnt({ scale: 1.04 });
    swimmerAnt.position.set(-2.3, -0.2, 0.6);
    swimmerAnt.rotation.y = 0.38;
    swimmerAnt.rotation.x = 0.05;
    applySwimmerGear(swimmerAnt);
    antsGroup.add(swimmerAnt);
    antInstances.swimmer = swimmerAnt;
  }

  // --- Smooth Camera Lerping ---
  function transitionToCamera(presetKey) {
    const preset = CAMERA_PRESETS[presetKey] || CAMERA_PRESETS.cover;
    cameraLerp.startPos.copy(camera.position);
    cameraLerp.targetPos.set(...preset.pos);
    cameraLerp.startLook.copy(controls.target);
    cameraLerp.targetLook.set(...preset.look);
    cameraLerp.progress = 0;
    cameraLerp.active = true;

    updateInfoCard(presetKey);

    document.querySelectorAll('.caste-btn').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.caste === presetKey);
    });
  }

  function updateInfoCard(casteKey) {
    const data = CASTE_DATA[casteKey] || CASTE_DATA.overview;
    document.getElementById('card-title').textContent = data.name;
    document.getElementById('card-desc').textContent = data.desc;

    const traitsContainer = document.getElementById('card-traits');
    traitsContainer.innerHTML = '';
    for (const [key, val] of Object.entries(data.stats)) {
      const row = document.createElement('div');
      row.className = 'trait-row';
      row.innerHTML = `<span class="trait-label">${key}</span><span class="trait-val">${val}</span>`;
      traitsContainer.appendChild(row);
    }
  }

  // --- Screenshot Exporter ---
  function captureScreenshot() {
    renderer.render(scene, camera);
    const dataURL = renderer.domElement.toDataURL('image/png');
    const link = document.createElement('a');
    link.download = 'realistic_ants_3d_render.png';
    link.href = dataURL;
    link.click();
  }

  // --- UI Event Handlers ---
  function bindUI() {
    document.querySelectorAll('.caste-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const caste = btn.dataset.caste;
        transitionToCamera(caste);
      });
    });

    document.getElementById('btn-reset').addEventListener('click', () => {
      transitionToCamera('cover');
    });

    const modal = document.getElementById('render-modal');
    const btnMasterRender = document.getElementById('btn-master-render');
    const btnCloseModal = document.getElementById('btn-close-modal');
    if (btnMasterRender && modal) {
      btnMasterRender.addEventListener('click', () => modal.classList.remove('hidden'));
    }
    if (btnCloseModal && modal) {
      btnCloseModal.addEventListener('click', () => modal.classList.add('hidden'));
    }
    if (modal) {
      modal.addEventListener('click', (e) => {
        if (e.target === modal) modal.classList.add('hidden');
      });
    }

    document.getElementById('btn-screenshot').addEventListener('click', captureScreenshot);

    document.getElementById('select-lighting').addEventListener('change', (e) => {
      setLightingMode(e.target.value);
    });

    const btnAnimate = document.getElementById('btn-animate');
    btnAnimate.addEventListener('click', () => {
      isAnimating = !isAnimating;
      btnAnimate.classList.toggle('active', isAnimating);
    });

    const btnTurntable = document.getElementById('btn-turntable');
    btnTurntable.addEventListener('click', () => {
      isTurntable = !isTurntable;
      controls.autoRotate = isTurntable;
      btnTurntable.classList.toggle('active', isTurntable);
    });

    const btnWireframe = document.getElementById('btn-wireframe');
    btnWireframe.addEventListener('click', () => {
      isWireframe = !isWireframe;
      btnWireframe.classList.toggle('active', isWireframe);
      scene.traverse(child => {
        if (child.isMesh && child.material) {
          if (Array.isArray(child.material)) {
            child.material.forEach(m => m.wireframe = isWireframe);
          } else {
            child.material.wireframe = isWireframe;
          }
        }
      });
    });

    window.addEventListener('resize', onWindowResize);
  }

  function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  }

  // --- Animation Loop ---
  function animate() {
    requestAnimationFrame(animate);

    const delta = clock.getDelta();
    const time = clock.getElapsedTime();

    if (cameraLerp.active) {
      cameraLerp.progress += delta / cameraLerp.duration;
      if (cameraLerp.progress >= 1.0) {
        cameraLerp.progress = 1.0;
        cameraLerp.active = false;
      }
      const t = 3 * cameraLerp.progress * cameraLerp.progress - 2 * cameraLerp.progress * cameraLerp.progress * cameraLerp.progress;
      camera.position.lerpVectors(cameraLerp.startPos, cameraLerp.targetPos, t);
      controls.target.lerpVectors(cameraLerp.startLook, cameraLerp.targetLook, t);
    }

    controls.update();

    // Subtle organic breathing & antennae micro-movements
    if (isAnimating && antsGroup) {
      Object.entries(antInstances).forEach(([key, ant], index) => {
        const offset = index * 1.1;
        const breathe = Math.sin(time * 2.2 + offset) * 0.02;
        ant.position.y += breathe * delta * 2.0;

        const { leftAntenna, rightAntenna } = ant.userData;
        if (leftAntenna && rightAntenna) {
          leftAntenna.rotation.x = Math.sin(time * 3.5 + offset) * 0.08;
          rightAntenna.rotation.x = Math.cos(time * 3.2 + offset) * 0.08;
        }
      });
    }

    renderer.render(scene, camera);
  }

  // --- Init Engine ---
  function init() {
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0a0c10);
    scene.fog = new THREE.FogExp2(0x0a0c10, 0.025);

    camera = new THREE.PerspectiveCamera(38, window.innerWidth / window.innerHeight, 0.1, 60);
    camera.position.set(...CAMERA_PRESETS.cover.pos);

    renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true, powerPreference: "high-performance" });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.15;
    renderer.outputEncoding = THREE.sRGBEncoding;
    container.appendChild(renderer.domElement);

    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.06;
    controls.target.set(...CAMERA_PRESETS.cover.look);
    controls.maxPolarAngle = Math.PI / 2 + 0.05;
    controls.minDistance = 1.5;
    controls.maxDistance = 22.0;
    controls.autoRotateSpeed = 1.2;

    setupLighting();
    setupEnvironment();
    assembleSquad();

    bindUI();
    updateInfoCard('overview');

    const loader = document.getElementById('loader');
    if (loader) {
      setTimeout(() => {
        loader.style.opacity = '0';
        setTimeout(() => loader.remove(), 500);
      }, 300);
    }

    animate();
  }

  window.addEventListener('DOMContentLoaded', init);
})();

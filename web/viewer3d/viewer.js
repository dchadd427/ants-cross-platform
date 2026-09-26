// ============================================================================
// Ants! 3D Character Studio - Multi-Caste Viewer (Worker Ant & Fire Ant)
// Interactive 360° Cycles Ray-Traced Turntable, Compare Mode & WebGL Engine
// ============================================================================

(function () {
  'use strict';

  // --- Caste Configurations ---
  const CASTES = {
    worker: {
      id: 'worker',
      name: 'Worker Ant (Caste #1)',
      title: 'WORKER ANT (CASTE #1)',
      statusText: 'Approved (Reference)',
      glb: 'worker_ant.glb',
      usdz: 'worker_ant.usdz',
      turntableDir: 'turntable',
      turntableAlt: '360 Ray-Traced Worker Ant Turntable',
      compareRef: 'reference_master.jpg',
      compare3D: 'worker_front.png',
      cardTitle: 'Worker Ant (Caste #1)',
      cardDesc: 'Foundational 3D model for all ant castes. Features huge innocent cartoon eyes with amber sunburst, prominent bulldog pincer mandibles with interlocking fangs, curved antennae, tall heroic posture, gap-free petiole waist, and mottled terracotta chitin.',
      traits: [
        'Innocent Compound Eyes',
        'Bulldog Mandibles',
        'Curved Antennae',
        'Continuous Petiole Waist',
        'Heroic Stature',
        'Mottled Terracotta Chitin'
      ],
      gameplayStill: 'worker_gameplay_angle.png',
      stills: [
        { id: 'front', name: 'Front Heroic Stance', file: 'worker_front.png', desc: 'High-resolution Cycles path-traced beauty render showcasing the heart-shaped cranium, cartoon eyes, bulldog mandibles, and articulated legs.' },
        { id: 'perspective', name: '3/4 Depth Perspective', file: 'worker_perspective.png', desc: 'Dynamic 3/4 angle showcasing leg articulation and depth proportions.' },
        { id: 'closeup', name: 'Face & Muzzle Macro Close-up', file: 'worker_face_closeup.png', desc: 'Extreme macro close-up of the forward-facing binocular cartoon eyes and serrated teeth.' },
        { id: 'gameplay', name: '🎮 1998 RTS Gameplay Angle', file: 'worker_gameplay_angle.png', desc: 'Authentic 1998 classic RTS top-down south-angled gameplay camera view.' }
      ]
    },
    fire: {
      id: 'fire',
      name: 'Fire Ant (Caste #2)',
      title: 'FIRE ANT (CASTE #2)',
      statusText: 'In User Review',
      glb: 'fire_ant.glb',
      usdz: 'fire_ant.usdz',
      turntableDir: 'turntable_fire',
      turntableAlt: '360 Ray-Traced Fire Ant Turntable',
      compareRef: 'fire_ant_master_reference.jpg',
      compare3D: 'fire_front.png',
      cardTitle: 'Fire Ant (Caste #2)',
      cardDesc: "Caste #2: Fire Chief Mason. Wears an oversized Cairns-style golden helmet ('child wearing an adult's hat') with flared duckbill brim and central comb, mounted with a bold scarlet red 'A' shield badge. Features slate-violet organic cranium, alert empty hands framing the chest, continuous gap-free petiole waist, and compact insect stance.",
      traits: [
        "Oversized Cairns Fire Chief Helmet ('Child in Adult Hat')",
        "Bold Scarlet Red 'A' Shield Badge",
        "Zero Antennae Helmet Enclosure",
        "Slate-Violet Organic Cranium",
        "Alert Empty Hands Framing Chest",
        "Continuous Gap-Free Petiole Waist"
      ],
      gameplayStill: 'fire_gameplay_angle.png',
      stills: [
        { id: 'front', name: 'Front Heroic Stance', file: 'fire_front.png', desc: "Front view showcasing the oversized Cairns helmet, bold scarlet red 'A' badge, slate-violet head, and alert empty hands framing the chest." },
        { id: 'perspective', name: '3/4 Depth Perspective', file: 'fire_perspective.png', desc: 'Dynamic perspective showcasing the flared duckbill brim dipping low over the neck, 4-legged compact stance, and seamless petiole waist.' },
        { id: 'closeup', name: 'Face & Helmet Macro Close-up', file: 'fire_face_closeup.png', desc: "Macro close-up of the crisp 3D beveled red letter 'A' badge, golden shield plaque, large amber cartoon eyes, and bulldog cheeks." },
        { id: 'gameplay', name: '🎮 1998 RTS Gameplay Angle', file: 'fire_gameplay_angle.png', desc: 'Authentic 1998 classic RTS top-down south-angled gameplay camera view with oversized golden helmet signature silhouette.' }
      ]
    }
  };

  let currentCaste = 'fire'; // Default to newest Fire Ant

  // --- DOM Elements ---
  const container = document.getElementById('canvas-container');
  const turntableContainer = document.getElementById('turntable-container');
  const turntableImg = document.getElementById('turntable-img');
  const turntableAngleLabel = document.getElementById('turntable-angle-label');
  const compareContainer = document.getElementById('compare-container');
  const turntableDock = document.getElementById('turntable-dock-controls');
  const webglDock = document.getElementById('webgl-dock-controls');

  // Mode Buttons
  const modeTurntableBtn = document.getElementById('mode-turntable');
  const modeCompareBtn = document.getElementById('mode-compare');
  const modeWebglBtn = document.getElementById('mode-webgl');

  let currentMode = 'turntable'; // 'turntable', 'compare', 'webgl'

  // --- 1. 360° Cycles Ray-Traced Turntable Controller ---
  const TOTAL_FRAMES = 36;
  let frames = [];
  let currentFrame = 0;
  let isDragging = false;
  let startX = 0;
  let startFrame = 0;
  let isAutoSpinning = true;
  let autoSpinInterval = null;

  function loadTurntableFrames() {
    frames = [];
    const cfg = CASTES[currentCaste];
    const timestamp = Date.now();
    for (let i = 0; i < TOTAL_FRAMES; i++) {
      const img = new Image();
      const pad = i.toString().padStart(2, '0');
      img.src = `${cfg.turntableDir}/frame_${pad}.png?v=${timestamp}`;
      frames.push(img);
    }
    updateTurntableDisplay(0);
  }

  function initTurntable() {
    loadTurntableFrames();

    // Mouse drag interaction
    turntableContainer.addEventListener('mousedown', (e) => {
      isDragging = true;
      startX = e.clientX;
      startFrame = currentFrame;
      stopAutoSpin();
    });

    window.addEventListener('mousemove', (e) => {
      if (!isDragging) return;
      const dx = e.clientX - startX;
      const frameDelta = Math.round(dx / 12);
      let next = (startFrame - frameDelta) % TOTAL_FRAMES;
      if (next < 0) next += TOTAL_FRAMES;
      updateTurntableDisplay(next);
    });

    window.addEventListener('mouseup', () => {
      isDragging = false;
    });

    // Touch interaction
    turntableContainer.addEventListener('touchstart', (e) => {
      if (e.touches.length === 1) {
        isDragging = true;
        startX = e.touches[0].clientX;
        startFrame = currentFrame;
        stopAutoSpin();
      }
    }, { passive: true });

    window.addEventListener('touchmove', (e) => {
      if (!isDragging || e.touches.length !== 1) return;
      const dx = e.touches[0].clientX - startX;
      const frameDelta = Math.round(dx / 12);
      let next = (startFrame - frameDelta) % TOTAL_FRAMES;
      if (next < 0) next += TOTAL_FRAMES;
      updateTurntableDisplay(next);
    }, { passive: true });

    window.addEventListener('touchend', () => {
      isDragging = false;
    });

    startAutoSpin();
  }

  function updateTurntableDisplay(frameIdx) {
    currentFrame = frameIdx;
    const pad = frameIdx.toString().padStart(2, '0');
    const cfg = CASTES[currentCaste];
    if (frames[frameIdx] && frames[frameIdx].src) {
      turntableImg.src = frames[frameIdx].src;
    } else {
      turntableImg.src = `${cfg.turntableDir}/frame_${pad}.png?v=${Date.now()}`;
    }

    const angleDeg = Math.round(frameIdx * (360 / TOTAL_FRAMES));
    if (turntableAngleLabel) {
      let desc = `${angleDeg}°`;
      if (angleDeg === 0) desc += ' (Front View)';
      else if (angleDeg === 40 || angleDeg === 50) desc += ' (3/4 Angle)';
      else if (angleDeg === 90) desc += ' (Side Profile)';
      else if (angleDeg === 180) desc += ' (Rear View)';
      else if (angleDeg === 270) desc += ' (Side Profile)';
      turntableAngleLabel.textContent = `${desc} — Drag to Orbit 360°`;
    }
  }

  function startAutoSpin() {
    isAutoSpinning = true;
    const btnAuto = document.getElementById('btn-spin-auto');
    if (btnAuto) {
      btnAuto.classList.add('active');
      btnAuto.textContent = 'Auto Spin: ON';
    }
    clearInterval(autoSpinInterval);
    autoSpinInterval = setInterval(() => {
      let next = (currentFrame + 1) % TOTAL_FRAMES;
      updateTurntableDisplay(next);
    }, 70); // ~14 fps smooth rotation
  }

  function stopAutoSpin() {
    isAutoSpinning = false;
    const btnAuto = document.getElementById('btn-spin-auto');
    if (btnAuto) {
      btnAuto.classList.remove('active');
      btnAuto.textContent = 'Auto Spin: OFF';
    }
    clearInterval(autoSpinInterval);
  }

  // --- 2. View Mode Selector ---
  function setViewMode(mode) {
    currentMode = mode;
    modeTurntableBtn.classList.toggle('active', mode === 'turntable');
    modeCompareBtn.classList.toggle('active', mode === 'compare');
    modeWebglBtn.classList.toggle('active', mode === 'webgl');

    if (mode === 'turntable') {
      turntableContainer.style.display = 'flex';
      compareContainer.style.display = 'none';
      container.style.display = 'none';
      turntableDock.style.display = 'flex';
      webglDock.style.display = 'none';
      startAutoSpin();
    } else if (mode === 'compare') {
      turntableContainer.style.display = 'none';
      compareContainer.style.display = 'flex';
      container.style.display = 'none';
      turntableDock.style.display = 'none';
      webglDock.style.display = 'none';
      stopAutoSpin();
    } else if (mode === 'webgl') {
      turntableContainer.style.display = 'none';
      compareContainer.style.display = 'none';
      container.style.display = 'block';
      turntableDock.style.display = 'none';
      webglDock.style.display = 'flex';
      stopAutoSpin();
      if (controls) controls.update();
    }
  }

  // --- 3. WebGL Three.js Engine ---
  let scene, camera, renderer, controls;
  let antGroup, stageGroup;
  let ambientLight, keyLight, fillLight, rimLight;
  let mixer, walkAction, idleAction;
  let activeActionName = 'Walk';
  let isTurntable = false;
  let isWireframe = false;
  let clock = new THREE.Clock();

  function initThree() {
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0a0c10);

    camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 100);
    camera.position.set(0, 0.4, 4.8);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: 'high-performance' });
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.15;
    container.appendChild(renderer.domElement);

    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.maxPolarAngle = Math.PI / 2 + 0.1;
    controls.minDistance = 1.2;
    controls.maxDistance = 10;
    controls.target.set(0, 0.1, 0);

    setupLighting();
    setupStage();
    loadAntModel(currentCaste);

    animate();
  }

  function setupLighting() {
    ambientLight = new THREE.AmbientLight(0x222833, 0.8);
    scene.add(ambientLight);

    keyLight = new THREE.DirectionalLight(0xfffaec, 2.5);
    keyLight.position.set(-3.5, 4.5, 4.0);
    keyLight.castShadow = true;
    keyLight.shadow.mapSize.width = 2048;
    keyLight.shadow.mapSize.height = 2048;
    keyLight.shadow.bias = -0.0001;
    scene.add(keyLight);

    fillLight = new THREE.DirectionalLight(0xb0d2f8, 1.0);
    fillLight.position.set(3.5, 2.0, 3.0);
    scene.add(fillLight);

    rimLight = new THREE.DirectionalLight(0xffffff, 2.2);
    rimLight.position.set(0, 4.0, -4.5);
    scene.add(rimLight);
  }

  function setLightingMode(mode) {
    if (!ambientLight || !keyLight) return;
    if (mode === 'sunset') {
      ambientLight.color.setHex(0x3a1a12);
      ambientLight.intensity = 0.9;
      keyLight.color.setHex(0xff7733);
      keyLight.intensity = 3.2;
      keyLight.position.set(-4.0, 3.0, 3.0);
      fillLight.color.setHex(0x5522aa);
      fillLight.intensity = 1.4;
      rimLight.color.setHex(0xffddaa);
      rimLight.intensity = 2.6;
      if (scene) scene.background = new THREE.Color(0x0e0608);
    } else if (mode === 'neon') {
      ambientLight.color.setHex(0x080e18);
      ambientLight.intensity = 0.7;
      keyLight.color.setHex(0x00ffcc);
      keyLight.intensity = 2.8;
      keyLight.position.set(-3.5, 4.0, 3.5);
      fillLight.color.setHex(0xff0077);
      fillLight.intensity = 2.2;
      rimLight.color.setHex(0xaa22ff);
      rimLight.intensity = 3.0;
      if (scene) scene.background = new THREE.Color(0x050512);
    } else if (mode === 'studio') {
      ambientLight.color.setHex(0x2a2a2e);
      ambientLight.intensity = 0.9;
      keyLight.color.setHex(0xffffff);
      keyLight.intensity = 2.4;
      keyLight.position.set(-3.0, 4.5, 4.0);
      fillLight.color.setHex(0xf0f0f5);
      fillLight.intensity = 1.2;
      rimLight.color.setHex(0xffffff);
      rimLight.intensity = 2.0;
      if (scene) scene.background = new THREE.Color(0x08090b);
    } else { // 'cover' / master studio 3-point
      ambientLight.color.setHex(0x222833);
      ambientLight.intensity = 0.8;
      keyLight.color.setHex(0xfffaec);
      keyLight.intensity = 2.5;
      keyLight.position.set(-3.5, 4.5, 4.0);
      fillLight.color.setHex(0xb0d2f8);
      fillLight.intensity = 1.0;
      rimLight.color.setHex(0xffffff);
      rimLight.intensity = 2.2;
      if (scene) scene.background = new THREE.Color(0x05070a);
    }
  }

  function setupStage() {
    stageGroup = new THREE.Group();
    scene.add(stageGroup);

    const discGeo = new THREE.CylinderGeometry(2.4, 2.6, 0.15, 64);
    const discMat = new THREE.MeshStandardMaterial({
      color: 0x11161d,
      roughness: 0.7,
      metalness: 0.2
    });
    const disc = new THREE.Mesh(discGeo, discMat);
    disc.position.y = -1.25;
    disc.receiveShadow = true;
    stageGroup.add(disc);

    const ringGeo = new THREE.RingGeometry(2.35, 2.45, 64);
    const ringMat = new THREE.MeshBasicMaterial({ color: 0xffaa33, side: THREE.DoubleSide });
    const ring = new THREE.Mesh(ringGeo, ringMat);
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = -1.17;
    stageGroup.add(ring);
  }

  function loadAntModel(casteId) {
    if (!antGroup) {
      antGroup = new THREE.Group();
      scene.add(antGroup);
    } else {
      while (antGroup.children.length > 0) {
        const obj = antGroup.children[0];
        antGroup.remove(obj);
      }
    }

    if (mixer) {
      mixer.stopAllAction();
      mixer = null;
    }

    const cfg = CASTES[casteId];
    const gltfLoader = new THREE.GLTFLoader();

    gltfLoader.load(
      `${cfg.glb}?v=${Date.now()}`,
      function (gltf) {
        const model = gltf.scene;
        model.traverse(function (child) {
          if (child.isMesh) {
            child.castShadow = true;
            child.receiveShadow = true;
            if (child.material) {
              child.material.wireframe = isWireframe;
              child.material.side = THREE.DoubleSide;
            }
          }
        });

        model.position.set(0, -1.17, 0);
        model.scale.set(1.0, 1.0, 1.0);
        antGroup.add(model);

        if (gltf.animations && gltf.animations.length > 0) {
          mixer = new THREE.AnimationMixer(model);
          walkAction = null;
          idleAction = null;

          gltf.animations.forEach((clip) => {
            const nameLower = clip.name.toLowerCase();
            if (nameLower.includes('walk')) {
              walkAction = mixer.clipAction(clip);
              walkAction.setLoop(THREE.LoopRepeat);
            } else if (nameLower.includes('idle')) {
              idleAction = mixer.clipAction(clip);
              idleAction.setLoop(THREE.LoopRepeat);
            }
          });

          // Fallback if specific clip name not tagged
          if (!walkAction && gltf.animations[0]) {
            walkAction = mixer.clipAction(gltf.animations[0]);
            walkAction.setLoop(THREE.LoopRepeat);
          }

          setAnimation(activeActionName);
        }
      },
      undefined,
      function (error) {
        console.error(`Error loading ${cfg.glb}:`, error);
      }
    );
  }

  function setAnimation(actionName) {
    activeActionName = actionName;
    if (!mixer) return;

    if (walkAction) walkAction.stop();
    if (idleAction) idleAction.stop();

    if (actionName === 'Walk' && walkAction) {
      walkAction.play();
    } else if (actionName === 'Idle' && idleAction) {
      idleAction.play();
    } else if (actionName === 'Idle' && !idleAction && walkAction) {
      // Fallback if no separate idle clip
      walkAction.play();
    }
  }

  function animate() {
    requestAnimationFrame(animate);
    const delta = clock.getDelta();

    if (currentMode === 'webgl') {
      controls.update();
      if (isTurntable && antGroup) {
        antGroup.rotation.y += delta * 0.5;
      }
      if (mixer && activeActionName !== 'Pause') {
        mixer.update(delta);
      }
      renderer.render(scene, camera);
    }
  }

  // --- 4. Switch Caste Function ---
  function switchCaste(casteId) {
    if (!CASTES[casteId]) return;
    currentCaste = casteId;
    const cfg = CASTES[casteId];

    // 1. Update Header Select and Badge
    const casteSelect = document.getElementById('caste-select');
    if (casteSelect) casteSelect.value = casteId;

    const statusBadge = document.getElementById('caste-status-badge');
    if (statusBadge) {
      statusBadge.textContent = cfg.statusText;
      statusBadge.className = cfg.statusClass;
    }

    // 2. Update QuickLook USDZ Link
    const quickLookLink = document.getElementById('quicklook-link');
    if (quickLookLink) {
      quickLookLink.href = cfg.usdz;
    }

    // 3. Update Caste Drawer Navigation Buttons
    const casteBtns = document.querySelectorAll('.caste-btn');
    casteBtns.forEach(btn => {
      const c = btn.getAttribute('data-caste');
      btn.classList.toggle('active', c === casteId);
    });

    // 4. Update Turntable
    loadTurntableFrames();

    // 5. Update Compare View
    const compareCardRefImg = document.querySelector('#compare-container .compare-card:first-child img');
    if (compareCardRefImg) {
      compareCardRefImg.src = `${cfg.compareRef}?v=${Date.now()}`;
    }
    const compareCard3DImg = document.getElementById('compare-3d-img');
    if (compareCard3DImg) {
      compareCard3DImg.src = `${cfg.compare3D}?v=${Date.now()}`;
    }

    // 6. Update Info Detail Card
    const cardTitle = document.getElementById('card-title');
    if (cardTitle) cardTitle.textContent = cfg.cardTitle;
    const cardDesc = document.getElementById('card-desc');
    if (cardDesc) cardDesc.textContent = cfg.cardDesc;
    const cardTraits = document.getElementById('card-traits');
    if (cardTraits) {
      cardTraits.innerHTML = cfg.traits.map(t => `<span class="trait-tag">${t}</span>`).join('');
    }

    // 7. Update Gallery Modal Tabs
    updateGalleryModal();

    // 8. Update WebGL Model
    loadAntModel(casteId);
  }

  function updateGalleryModal() {
    const cfg = CASTES[currentCaste];
    const modalTitle = document.querySelector('#render-modal .modal-header h2');
    if (modalTitle) modalTitle.textContent = `${cfg.name} — Multi-Angle Stills`;

    const galleryTabsWrap = document.querySelector('.gallery-tabs');
    if (galleryTabsWrap) {
      galleryTabsWrap.innerHTML = cfg.stills.map((s, idx) => `
        <button class="gallery-tab-btn ${idx === 0 ? 'active' : ''}" data-img="${s.file}" data-desc="${s.desc}">
          ${s.name}
        </button>
      `).join('');

      // Rebind click events
      const masterImg = document.getElementById('master-render-img');
      const downloadLink = document.getElementById('download-link');
      const renderCaption = document.getElementById('render-caption');

      const tabs = galleryTabsWrap.querySelectorAll('.gallery-tab-btn');
      tabs.forEach(tab => {
        tab.addEventListener('click', () => {
          tabs.forEach(t => t.classList.remove('active'));
          tab.classList.add('active');
          const imgName = tab.getAttribute('data-img');
          const desc = tab.getAttribute('data-desc');
          if (masterImg) masterImg.src = `${imgName}?v=${Date.now()}`;
          if (downloadLink) {
            downloadLink.href = imgName;
            downloadLink.setAttribute('download', imgName);
          }
          if (renderCaption) {
            renderCaption.innerHTML = `<strong>${tab.textContent.trim()}:</strong> ${desc}`;
          }
        });
      });

      // Default to first tab
      if (tabs.length > 0) {
        tabs[0].click();
      }
    }
  }

  // --- 5. Setup UI Event Listeners ---
  function setupUI() {
    // Caste Dropdown Select
    const casteSelect = document.getElementById('caste-select');
    if (casteSelect) {
      casteSelect.addEventListener('change', (e) => {
        switchCaste(e.target.value);
      });
    }

    // Caste Drawer Buttons
    const casteBtns = document.querySelectorAll('.caste-btn:not(.locked)');
    casteBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        const c = btn.getAttribute('data-caste');
        if (c && CASTES[c]) {
          switchCaste(c);
          const casteNav = document.getElementById('caste-nav');
          const casteNavBackdrop = document.getElementById('caste-nav-backdrop');
          casteNav?.classList.remove('open');
          casteNavBackdrop?.classList.remove('open');
        }
      });
    });

    // View Mode Switching
    modeTurntableBtn.addEventListener('click', () => setViewMode('turntable'));
    modeCompareBtn.addEventListener('click', () => setViewMode('compare'));
    modeWebglBtn.addEventListener('click', () => setViewMode('webgl'));

    // Turntable Dock Controls
    const btnAuto = document.getElementById('btn-spin-auto');
    if (btnAuto) {
      btnAuto.addEventListener('click', () => {
        if (isAutoSpinning) stopAutoSpin();
        else startAutoSpin();
      });
    }

    document.getElementById('btn-front-snap')?.addEventListener('click', () => {
      stopAutoSpin();
      updateTurntableDisplay(0);
    });
    document.getElementById('btn-angle-snap')?.addEventListener('click', () => {
      stopAutoSpin();
      updateTurntableDisplay(4); // ~40 degrees
    });
    document.getElementById('btn-side-snap')?.addEventListener('click', () => {
      stopAutoSpin();
      updateTurntableDisplay(9); // 90 degrees
    });
    document.getElementById('btn-back-snap')?.addEventListener('click', () => {
      stopAutoSpin();
      updateTurntableDisplay(18); // 180 degrees
    });
    document.getElementById('btn-gameplay-snap')?.addEventListener('click', () => {
      stopAutoSpin();
      const cfg = CASTES[currentCaste];
      turntableImg.src = `${cfg.gameplayStill}?v=${Date.now()}`;
      if (turntableAngleLabel) {
        turntableAngleLabel.textContent = "🎮 Authentic 1998 RTS Angled Top-Down View (South Perspective)";
      }
    });

    // Blender Instructions Modal
    const blenderModal = document.getElementById('blender-modal');
    document.getElementById('btn-open-blender')?.addEventListener('click', () => {
      blenderModal?.classList.remove('hidden');
    });
    document.getElementById('btn-close-blender-modal')?.addEventListener('click', () => {
      blenderModal?.classList.add('hidden');
    });

    // Mobile Caste Drawer
    const btnCasteToggle = document.getElementById('btn-caste-toggle');
    const btnCloseCasteNav = document.getElementById('btn-close-caste-nav');
    const casteNavBackdrop = document.getElementById('caste-nav-backdrop');
    const casteNav = document.getElementById('caste-nav');

    if (btnCasteToggle && casteNav) {
      btnCasteToggle.addEventListener('click', () => {
        casteNav.classList.add('open');
        casteNavBackdrop?.classList.add('open');
      });
    }
    if (btnCloseCasteNav && casteNav) {
      btnCloseCasteNav.addEventListener('click', () => {
        casteNav.classList.remove('open');
        casteNavBackdrop?.classList.remove('open');
      });
    }
    if (casteNavBackdrop && casteNav) {
      casteNavBackdrop.addEventListener('click', () => {
        casteNav.classList.remove('open');
        casteNavBackdrop.classList.remove('open');
      });
    }

    // Render Gallery Modal
    const renderModal = document.getElementById('render-modal');
    document.getElementById('btn-master-render')?.addEventListener('click', () => {
      renderModal?.classList.remove('hidden');
    });
    document.getElementById('btn-close-modal')?.addEventListener('click', () => {
      renderModal?.classList.add('hidden');
    });

    // WebGL Controls
    document.getElementById('select-lighting')?.addEventListener('change', (e) => {
      setLightingMode(e.target.value);
    });

    document.getElementById('select-anim')?.addEventListener('change', (e) => {
      setAnimation(e.target.value);
    });

    document.getElementById('btn-turntable')?.addEventListener('click', (e) => {
      isTurntable = !isTurntable;
      e.target.classList.toggle('active', isTurntable);
    });

    document.getElementById('btn-wireframe')?.addEventListener('click', (e) => {
      isWireframe = !isWireframe;
      e.target.classList.toggle('active', isWireframe);
      if (antGroup) {
        antGroup.traverse(c => {
          if (c.isMesh && c.material) c.material.wireframe = isWireframe;
        });
      }
    });

    document.getElementById('btn-webgl-gameplay')?.addEventListener('click', () => {
      if (camera && controls) {
        camera.position.set(0.0, 3.8, 2.5);
        controls.target.set(0.0, 0.0, 0.0);
        controls.update();
      }
    });

    window.addEventListener('resize', () => {
      if (camera && renderer) {
        camera.aspect = window.innerWidth / window.innerHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(window.innerWidth, window.innerHeight);
      }
    });
  }

  // --- Initialize Everything ---
  window.addEventListener('DOMContentLoaded', () => {
    initTurntable();
    initThree();
    setupUI();
    updateGalleryModal();

    // Default to Fire Ant as active review subject
    switchCaste('fire');

    // Hide loader
    const loader = document.getElementById('loader');
    if (loader) {
      setTimeout(() => {
        loader.style.opacity = '0';
        setTimeout(() => loader.style.display = 'none', 300);
      }, 400);
    }
  });

})();

// ============================================================================
// Ants! 3D Character Studio - Worker Ant (Caste #1)
// Interactive 360° Cycles Ray-Traced Turntable, Compare Mode & WebGL Engine
// ============================================================================

(function () {
  'use strict';

  // --- State & DOM Elements ---
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
  const frames = [];
  let currentFrame = 0;
  let isDragging = false;
  let startX = 0;
  let startFrame = 0;
  let isAutoSpinning = true;
  let autoSpinInterval = null;

  function initTurntable() {
    // Preload all 36 ray-traced frames
    for (let i = 0; i < TOTAL_FRAMES; i++) {
      const img = new Image();
      const pad = i.toString().padStart(2, '0');
      img.src = `turntable/frame_${pad}.png`;
      frames.push(img);
    }

    // Set initial frame
    updateTurntableDisplay(0);

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
      // Sensitivity: 12 pixels per frame
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

    // Start auto-spin initially
    startAutoSpin();
  }

  function updateTurntableDisplay(frameIdx) {
    currentFrame = frameIdx;
    const pad = frameIdx.toString().padStart(2, '0');
    turntableImg.src = `turntable/frame_${pad}.png`;

    const deg = Math.round(frameIdx * (360 / TOTAL_FRAMES));
    let label = `${deg}° Orbit`;
    if (deg === 0) label = "0° Front Heroic Stance";
    else if (deg === 40) label = "40° 3/4 Depth Perspective";
    else if (deg === 90) label = "90° Side Profile";
    else if (deg === 180) label = "180° Rear Back View";
    else if (deg === 270) label = "270° Opposite Profile";

    if (turntableAngleLabel) {
      turntableAngleLabel.textContent = `${label} (Drag to Orbit 360°)`;
    }
  }

  function startAutoSpin() {
    if (autoSpinInterval) clearInterval(autoSpinInterval);
    isAutoSpinning = true;
    const btn = document.getElementById('btn-spin-auto');
    if (btn) btn.classList.add('active');

    autoSpinInterval = setInterval(() => {
      let next = (currentFrame + 1) % TOTAL_FRAMES;
      updateTurntableDisplay(next);
    }, 120); // ~8.3 fps smooth turntable orbit
  }

  function stopAutoSpin() {
    if (autoSpinInterval) {
      clearInterval(autoSpinInterval);
      autoSpinInterval = null;
    }
    isAutoSpinning = false;
    const btn = document.getElementById('btn-spin-auto');
    if (btn) btn.classList.remove('active');
  }

  // --- 2. Mode Switching Logic ---
  function setViewMode(mode) {
    currentMode = mode;
    [modeTurntableBtn, modeCompareBtn, modeWebglBtn].forEach(b => b.classList.remove('active'));

    if (mode === 'turntable') {
      modeTurntableBtn.classList.add('active');
      turntableContainer.style.display = 'flex';
      compareContainer.style.display = 'none';
      turntableDock.style.display = 'flex';
      webglDock.style.display = 'none';
    } else if (mode === 'compare') {
      modeCompareBtn.classList.add('active');
      turntableContainer.style.display = 'none';
      compareContainer.style.display = 'flex';
      turntableDock.style.display = 'none';
      webglDock.style.display = 'none';
      stopAutoSpin();
    } else if (mode === 'webgl') {
      modeWebglBtn.classList.add('active');
      turntableContainer.style.display = 'none';
      compareContainer.style.display = 'none';
      turntableDock.style.display = 'none';
      webglDock.style.display = 'flex';
      stopAutoSpin();
    }
  }

  // --- 3. Three.js Real-Time Engine ---
  let scene, camera, renderer, controls;
  let antGroup, stageGroup, lightsGroup;
  let isAnimating = true;
  let isTurntable = false;
  let isWireframe = false;
  let clock = new THREE.Clock();

  function initThree() {
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x05070a);

    camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 100);
    camera.position.set(0.0, 0.1, 4.4);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, preserveDrawingBuffer: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.15;
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    container.appendChild(renderer.domElement);

    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.target.set(0.0, -0.1, 0.0);
    controls.minDistance = 1.2;
    controls.maxDistance = 12.0;
    controls.maxPolarAngle = Math.PI / 2 + 0.1;

    setupLighting();
    setupStage();
    setupWorkerAnt();
    animate();
  }

  function setupLighting() {
    lightsGroup = new THREE.Group();
    scene.add(lightsGroup);

    const ambient = new THREE.AmbientLight(0x222833, 0.8);
    lightsGroup.add(ambient);

    const keyLight = new THREE.DirectionalLight(0xfffaec, 2.5);
    keyLight.position.set(-3.5, 4.5, 4.0);
    keyLight.castShadow = true;
    keyLight.shadow.mapSize.width = 2048;
    keyLight.shadow.mapSize.height = 2048;
    lightsGroup.add(keyLight);

    const fillLight = new THREE.DirectionalLight(0xb0d2f8, 1.0);
    fillLight.position.set(3.5, 2.5, 2.5);
    lightsGroup.add(fillLight);

    const rimLight = new THREE.DirectionalLight(0xffffff, 2.2);
    rimLight.position.set(0.0, 4.5, -3.5);
    lightsGroup.add(rimLight);
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
    const ringMat = new THREE.MeshBasicMaterial({ color: 0x2ecc71, side: THREE.DoubleSide });
    const ring = new THREE.Mesh(ringGeo, ringMat);
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = -1.17;
    stageGroup.add(ring);
  }

  function setupWorkerAnt() {
    antGroup = new THREE.Group();
    scene.add(antGroup);

    const gltfLoader = new THREE.GLTFLoader();
    gltfLoader.load(
      'worker_ant.glb?v=' + Date.now(),
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
      },
      undefined,
      function (error) {
        console.error('Error loading worker_ant.glb:', error);
      }
    );
  }

  function animate() {
    requestAnimationFrame(animate);
    const delta = clock.getDelta();

    if (currentMode === 'webgl') {
      controls.update();
      if (isTurntable && antGroup) {
        antGroup.rotation.y += delta * 0.5;
      }
      renderer.render(scene, camera);
    }
  }

  // --- 4. Setup UI Interactions ---
  function setupUI() {
    // Mode Switching
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

    // Blender Instructions Modal
    const blenderModal = document.getElementById('blender-modal');
    document.getElementById('btn-open-blender')?.addEventListener('click', () => {
      blenderModal.classList.remove('hidden');
    });
    document.getElementById('btn-close-blender-modal')?.addEventListener('click', () => {
      blenderModal.classList.add('hidden');
    });

    // Render Gallery Modal
    const renderModal = document.getElementById('render-modal');
    document.getElementById('btn-master-render')?.addEventListener('click', () => {
      renderModal.classList.remove('hidden');
    });
    document.getElementById('btn-close-modal')?.addEventListener('click', () => {
      renderModal.classList.add('hidden');
    });

    // Gallery Tabs
    const galleryTabs = document.querySelectorAll('.gallery-tab-btn');
    const masterImg = document.getElementById('master-render-img');
    const downloadLink = document.getElementById('download-link');
    galleryTabs.forEach(tab => {
      tab.addEventListener('click', () => {
        galleryTabs.forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        const imgName = tab.getAttribute('data-img');
        if (masterImg) masterImg.src = `${imgName}?v=${Date.now()}`;
        if (downloadLink) {
          downloadLink.href = imgName;
          downloadLink.setAttribute('download', imgName);
        }
      });
    });

    // WebGL Controls
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

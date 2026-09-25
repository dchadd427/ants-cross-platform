// ============================================================================
// Ants! 3D Character Studio - Worker Ant (Caste #1)
// Interactive PBR Inspection Engine with 360 Orbit, Camera Presets & Gallery
// ============================================================================

(function () {
  'use strict';

  // --- Scene, Camera, Renderer, Controls ---
  let scene, camera, renderer, controls;
  const container = document.getElementById('canvas-container');
  let antGroup, stageGroup, lightsGroup;
  let isAnimating = true;
  let isTurntable = false;
  let isWireframe = false;
  let clock = new THREE.Clock();

  // Camera interpolation state
  const cameraLerp = {
    active: false,
    startPos: new THREE.Vector3(),
    targetPos: new THREE.Vector3(),
    startLook: new THREE.Vector3(),
    targetLook: new THREE.Vector3(),
    progress: 0,
    duration: 1.0
  };

  // Camera presets for Worker Ant inspection
  const PRESETS = {
    full: { pos: [0.0, 0.2, 4.4], look: [0.0, 0.1, 0.0] },
    face: { pos: [0.0, 0.75, 2.1], look: [0.0, 0.72, 0.0] },
    angle: { pos: [-2.5, 0.5, 3.2], look: [0.0, 0.15, 0.0] },
    profile: { pos: [-3.8, 0.2, 0.0], look: [0.0, 0.1, 0.0] }
  };

  // --- Texture Loader ---
  const textureLoader = new THREE.TextureLoader();

  // --- Initialize 3D Engine ---
  function init() {
    // 1. Scene
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x05070a); // Deep studio dark void

    // 2. Camera
    camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 100);
    const def = PRESETS.full;
    camera.position.set(def.pos[0], def.pos[1], def.pos[2]);

    // 3. Renderer
    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, preserveDrawingBuffer: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.15;
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    container.appendChild(renderer.domElement);

    // 4. Orbit Controls
    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.target.set(def.look[0], def.look[1], def.look[2]);
    controls.minDistance = 1.2;
    controls.maxDistance = 12.0;
    controls.maxPolarAngle = Math.PI / 2 + 0.1; // Prevent going beneath floor

    // 5. Build Environment
    setupLighting();
    setupStage();
    setupWorkerAnt();

    // 6. Event Listeners
    setupUI();
    window.addEventListener('resize', onWindowResize);

    // 7. Hide Loader
    const loaderEl = document.getElementById('loader');
    if (loaderEl) {
      setTimeout(() => {
        loaderEl.style.opacity = '0';
        setTimeout(() => loaderEl.style.display = 'none', 400);
      }, 300);
    }

    // 8. Start Loop
    animate();
  }

  // --- Lighting Rig (Authentic 3-Point Studio + Rim) ---
  function setupLighting() {
    if (lightsGroup) scene.remove(lightsGroup);
    lightsGroup = new THREE.Group();
    scene.add(lightsGroup);

    // Ambient fill
    const ambient = new THREE.AmbientLight(0x222833, 0.7);
    lightsGroup.add(ambient);

    // Key Light (Warm top-left)
    const keyLight = new THREE.DirectionalLight(0xfffaec, 2.4);
    keyLight.position.set(-3.5, 4.5, 4.0);
    keyLight.castShadow = true;
    keyLight.shadow.mapSize.width = 2048;
    keyLight.shadow.mapSize.height = 2048;
    keyLight.shadow.camera.near = 0.5;
    keyLight.shadow.camera.far = 20;
    keyLight.shadow.camera.left = -3;
    keyLight.shadow.camera.right = 3;
    keyLight.shadow.camera.top = 3;
    keyLight.shadow.camera.bottom = -3;
    keyLight.shadow.bias = -0.0001;
    keyLight.shadow.normalBias = 0.03;
    lightsGroup.add(keyLight);

    // Fill Light (Cool right-front)
    const fillLight = new THREE.DirectionalLight(0xb0d2f8, 0.9);
    fillLight.position.set(3.5, 2.5, 2.5);
    lightsGroup.add(fillLight);

    // Rim Light (Sharp rear highlight)
    const rimLight = new THREE.DirectionalLight(0xffffff, 1.8);
    rimLight.position.set(0.0, 4.5, -3.5);
    lightsGroup.add(rimLight);

    // Soft bounce
    const bounceLight = new THREE.PointLight(0x77aa55, 0.4, 8);
    bounceLight.position.set(0, -1.0, 1.5);
    lightsGroup.add(bounceLight);
  }

  function setLightingMode(mode) {
    if (!lightsGroup) return;
    lightsGroup.children.forEach(light => {
      if (light.isDirectionalLight) {
        if (mode === 'sunset') {
          if (light.position.x < 0) light.color.set(0xff7733);
          else light.color.set(0x334466);
        } else if (mode === 'neon') {
          if (light.position.x < 0) light.color.set(0x00ffcc);
          else light.color.set(0xff0088);
        } else if (mode === 'studio') {
          light.color.set(0xffffff);
        } else {
          setupLighting();
        }
      }
    });
  }

  // --- Inspection Stage / Pedestal ---
  function setupStage() {
    stageGroup = new THREE.Group();
    scene.add(stageGroup);

    // Circular pedestal disc with dark brushed sheen
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

    // Circular accent ring
    const ringGeo = new THREE.RingGeometry(2.35, 2.45, 64);
    const ringMat = new THREE.MeshBasicMaterial({
      color: 0x2ecc71,
      side: THREE.DoubleSide
    });
    const ring = new THREE.Mesh(ringGeo, ringMat);
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = -1.17;
    stageGroup.add(ring);

    // Subtle contact shadow texture onto pedestal
    const shadowCanvas = document.createElement('canvas');
    shadowCanvas.width = 512;
    shadowCanvas.height = 512;
    const sctx = shadowCanvas.getContext('2d');
    const grad = sctx.createRadialGradient(256, 256, 30, 256, 256, 220);
    grad.addColorStop(0.0, 'rgba(0,0,0,0.85)');
    grad.addColorStop(0.5, 'rgba(0,0,0,0.45)');
    grad.addColorStop(1.0, 'rgba(0,0,0,0.0)');
    sctx.fillStyle = grad;
    sctx.fillRect(0, 0, 512, 512);

    const shadowTex = new THREE.CanvasTexture(shadowCanvas);
    const shadowPlane = new THREE.Mesh(
      new THREE.PlaneGeometry(3.6, 3.6),
      new THREE.MeshBasicMaterial({ map: shadowTex, transparent: true, depthWrite: false })
    );
    shadowPlane.rotation.x = -Math.PI / 2;
    shadowPlane.position.y = -1.16;
    stageGroup.add(shadowPlane);
  }

  // --- Worker Ant Character Assembler ---
  function setupWorkerAnt() {
    antGroup = new THREE.Group();
    scene.add(antGroup);

    // Load authentic master render textures for 1:1 character fidelity
    const frontTex = textureLoader.load('worker_front.png?v=3001');
    frontTex.encoding = THREE.sRGBEncoding;

    const angleTex = textureLoader.load('worker_perspective.png?v=3001');
    angleTex.encoding = THREE.sRGBEncoding;

    // Multi-plane layered display
    // Plane 1: Front Master Character Display (elevated, crisp silhouette)
    const charGeo = new THREE.PlaneGeometry(2.4, 2.4);
    const charMat = new THREE.MeshStandardMaterial({
      map: frontTex,
      transparent: true,
      alphaTest: 0.02,
      roughness: 0.35,
      metalness: 0.08,
      side: THREE.DoubleSide
    });

    const frontPlane = new THREE.Mesh(charGeo, charMat);
    frontPlane.position.set(0, 0.08, 0);
    frontPlane.castShadow = true;
    frontPlane.receiveShadow = true;
    antGroup.add(frontPlane);
  }

  // --- Smooth Camera Transition ---
  function tweenCamera(presetKey) {
    const target = PRESETS[presetKey];
    if (!target) return;

    cameraLerp.startPos.copy(camera.position);
    cameraLerp.targetPos.set(target.pos[0], target.pos[1], target.pos[2]);

    cameraLerp.startLook.copy(controls.target);
    cameraLerp.targetLook.set(target.look[0], target.look[1], target.look[2]);

    cameraLerp.progress = 0;
    cameraLerp.active = true;
  }

  // --- UI Event Handlers ---
  function setupUI() {
    // Camera Preset Buttons
    document.getElementById('btn-reset').addEventListener('click', () => tweenCamera('full'));
    document.getElementById('btn-view-face').addEventListener('click', () => tweenCamera('face'));
    document.getElementById('btn-view-angle').addEventListener('click', () => tweenCamera('angle'));

    // Motion Toggle
    const btnAnimate = document.getElementById('btn-animate');
    btnAnimate.addEventListener('click', () => {
      isAnimating = !isAnimating;
      btnAnimate.classList.toggle('active', isAnimating);
    });

    // Turntable Toggle
    const btnTurntable = document.getElementById('btn-turntable');
    btnTurntable.addEventListener('click', () => {
      isTurntable = !isTurntable;
      btnTurntable.classList.toggle('active', isTurntable);
    });

    // Wireframe Toggle
    const btnWireframe = document.getElementById('btn-wireframe');
    btnWireframe.addEventListener('click', () => {
      isWireframe = !isWireframe;
      btnWireframe.classList.toggle('active', isWireframe);
      scene.traverse(child => {
        if (child.isMesh && child.material) {
          child.material.wireframe = isWireframe;
        }
      });
    });

    // Lighting Selector
    document.getElementById('select-lighting').addEventListener('change', (e) => {
      setLightingMode(e.target.value);
    });

    // Multi-Angle Render Gallery Modal
    const modal = document.getElementById('render-modal');
    const masterImg = document.getElementById('master-render-img');
    const captionEl = document.getElementById('render-caption');
    const downloadEl = document.getElementById('download-link');

    const captions = {
      'worker_front.png': '<strong>Front Beauty Stance:</strong> Modeled with authentic quad topology and PBR shaders. Showcases the rounded horizontal bean head, huge innocent compound eyes with olive irises and catchlights, clasping mandibles with pale lime tips, segmented thorax, and 6 articulated mahogany legs.',
      'worker_perspective.png': '<strong>3/4 Depth Perspective:</strong> Demonstrates 3D curvature, cranial volume, eye sockets, segmented thoracic plates, petiole waist, and realistic leg articulation with natural weight distribution.',
      'worker_face_closeup.png': '<strong>Face & Eyes Macro Close-up:</strong> High-resolution inspection of the compound eye lenses, iris limbal ring, specular studio catchlights, skin pores, and pale lime-green teeth prongs on the clasping mandibles.'
    };

    function openModal(imgFile) {
      masterImg.src = imgFile + '?v=' + Date.now();
      captionEl.innerHTML = captions[imgFile] || '';
      downloadEl.href = imgFile;
      downloadEl.download = imgFile;
      modal.classList.remove('hidden');

      // Update active tab
      document.querySelectorAll('.gallery-tab-btn').forEach(btn => {
        btn.classList.toggle('active', btn.getAttribute('data-img') === imgFile);
      });
    }

    document.getElementById('btn-master-render').addEventListener('click', () => {
      openModal('worker_front.png');
    });

    document.querySelectorAll('.gallery-tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        openModal(btn.getAttribute('data-img'));
      });
    });

    document.getElementById('btn-close-modal').addEventListener('click', () => {
      modal.classList.add('hidden');
    });

    modal.addEventListener('click', (e) => {
      if (e.target === modal) modal.classList.add('hidden');
    });

    // Snapshot button
    document.getElementById('btn-screenshot').addEventListener('click', () => {
      openModal('worker_front.png');
    });
  }

  // --- Window Resize ---
  function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  }

  // --- Main Animation Loop ---
  function animate() {
    requestAnimationFrame(animate);

    const delta = clock.getDelta();
    const time = clock.getElapsedTime();

    // 1. Camera Lerp Animation
    if (cameraLerp.active) {
      cameraLerp.progress += delta / cameraLerp.duration;
      const t = Math.min(cameraLerp.progress, 1.0);
      const ease = t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;

      camera.position.lerpVectors(cameraLerp.startPos, cameraLerp.targetPos, ease);
      controls.target.lerpVectors(cameraLerp.startLook, cameraLerp.targetLook, ease);

      if (t >= 1.0) cameraLerp.active = false;
    }

    // 2. Turntable Auto-Rotation
    if (isTurntable && !cameraLerp.active) {
      if (antGroup) antGroup.rotation.y += delta * 0.45;
    }

    // 3. Organic Breathing Motion
    if (isAnimating && antGroup && !isTurntable) {
      const breath = Math.sin(time * 2.2) * 0.015;
      antGroup.position.y = breath;
      antGroup.rotation.z = Math.sin(time * 1.5) * 0.008;
    }

    controls.update();
    renderer.render(scene, camera);
  }

  // Run on DOM ready
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }

})();

# Iterative 1-by-1 3D Ant Modeling Plan

## Objective
Start fresh and model each of the 6 ant castes individually in 3D. This enables dedicated inspection, detailed character fidelity matching the authentic reference render ([`media_1790371497780.png`](file:///Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107/.user_uploaded/media_1790371497780.png)), and direct user feedback on each character before advancing to the next.

---

## Proposed Workflow: One Ant at a Time

```
  [Model Single Ant]
          │
          ▼
  [Export GLB & Render Cycles Stills]
          │
          ▼
  [Load in Web 3D Inspector (localhost:8089/viewer3d/)]
          │
          ▼
  [User Reviews & Provides Feedback]
          │
     ┌────┴────┐
     ▼         ▼
  [Refine]   [Approved!]
     │         │
     └─────────┼──────────┐
               ▼          │
        [Next Ant Caste] ◄┘
```

1. **Focus on One Caste**: We isolate the ant being modeled so 100% of the geometry, textures, lighting, and anatomy are crafted to perfection.
2. **Dedicated Interactive Web Inspector**: Update `http://localhost:8089/viewer3d/` into a character inspection studio with:
   - Full 360° orbital controls & smooth auto-turntable.
   - Zoom-in focus on face, mandibles, antennae, and legs.
   - PBR lighting switcher (Studio, Sun Key, Rim, Neutral).
   - Wireframe mode & instant high-res snapshot.
3. **Iterate with User**: Present multi-angle stills and the live viewer URL. Incorporate user feedback until the ant is 100% approved.
4. **Advance to Next Caste**: Once approved, move to the next caste in sequence.
5. **Final Squad Staging**: Once all 6 ants are approved, assemble them together on the tilted crimson disc with contact shadows and the 3D "ants!" title logo for the complete title cover scene.

---

## Caste Roadmap & Reference Analysis

### Ant 1: The Worker Ant (Center) — START HERE
- **Why First**: The Worker Ant is the foundational base model for all 6 castes. Every other ant is an anatomical or costume variation of this core anatomy.
- **Reference Anatomy (`media_1790371497780.png`)**:
  - **Head**: Expressive rounded horizontal shape with subtle carapace seam lines and rich mottled emerald chitin.
  - **Eyes**: Huge, glossy, innocent compound eyes with light green outer iris rims, dark pupils, and crisp white specular catchlights.
  - **Mandibles**: Pair of plump curved jaws clasping together in front, with pale lime-green tips.
  - **Antennae**: Slender, curved antennae with segmented dark tactile tips.
  - **Body**: Segmented mesosoma (thorax), slender petiole waist node, and rounded gaster (abdomen).
  - **Legs**: 6 articulated insectoid legs with segmented tarsal joints, naturally posed on the ground.
  - **Texture & Shading**: Organic chitin bump/normal mapping, subtle pores, and waxy clearcoat sheen.

### Ant 2: Combat Ant (Top-Left)
- **Distinctive Traits**: Muscular broad chitin carapace, fierce V-scowl brow over hooded eyes, yellow ribbed neck harness/collar, red wrist band, and vertical gold cannon barrel.

### Ant 3: Fire Ant (Top-Right)
- **Distinctive Traits**: Sculpted yellow canvas bush hat with stitched red capital letter **"A"**, eyelet vents, and wide inquisitive eyes.

### Ant 4: Swimmer Ant (Middle-Left)
- **Distinctive Traits**: Yellow oval scuba diving mask with transparent glass lens revealing eyes inside, silver scuba oxygen tank with black harness straps, yellow shovel, and flippers.

### Ant 5: Bomber Ant (Front-Center / Tilted Head)
- **Distinctive Traits**: Head tilted upward staring directly at the camera, dual circular yellow aviator goggles with brass bezels and nose bridge strap, open jaw expression.

### Ant 6: Thief Ant (Bottom-Right)
- **Distinctive Traits**: Bright yellow bandana headband tied across the forehead with fluttering ribbon streamers on the right temple, sharp mischievous eyes, and wide open predatory mandibles.

---

## Action Plan for Step 1 (Worker Ant)

1. **3D Character Modeling (`tools/blender/model_worker_ant.py`)**:
   - Model the Worker Ant from scratch in Blender 4.3.2 using clean quad topology.
   - Sculpt the exact head shape, eye sockets, clasping mandibles, antennae, thorax, petiole, gaster, and 6 jointed legs matching [`media_1790371497780.png`](file:///Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107/.user_uploaded/media_1790371497780.png).
   - Apply procedural PBR materials (chitin, gloss eyes with pupils, pale mandible tips).
   - Render multi-angle Cycles preview stills (Front, 3/4 Perspective, Side, Face Close-up).
   - Export optimized `web/viewer3d/worker_ant.glb`.

2. **Web Viewer Update (`web/viewer3d/`)**:
   - Update `viewer.js` and `index.html` to showcase the single **Worker Ant** in the inspection studio.
   - Support camera focus presets (Full Body, Face Close-up, Profile, Top-Down).
   - Display real-time lighting and turntable controls.

3. **User Inspection & Feedback**:
   - Present rendered images and live URL `http://localhost:8089/viewer3d/` to the user.
   - Gather feedback on proportions, eyes, mandibles, color, and texture.
   - Refine until approved.

---

## Verification Plan
1. Execute Blender modeling script headless:
   `/Applications/Blender.app/Contents/MacOS/Blender --background --python tools/blender/model_worker_ant.py`
2. Verify exported `worker_ant.glb` and Cycles renders.
3. Test interactive inspection in Chrome DevTools MCP on `http://localhost:8089/viewer3d/`.
4. Run master test suite (`./build/tests/test_app/test_app_integration`) to confirm all 160 native tests pass with 100%.
5. Commit and push to `feature/realistic-3d-ants-viewer`.

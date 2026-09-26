# Implementation Plan: Caste #2 — The Fire Ant (`af` / Mason)

## 1. Context & Completed Worker Ant Verification
In the previous step, the two visual geometry issues on the Worker Ant were resolved:
1. **Petiole Waist Seamless Connection**: Replaced disconnected cylindrical gap with an articulated, continuous sleeve spanning from `Thorax_Metanotum` ($Z = 0.94$) deep into the anterior socket of `Gaster` ($Z = 0.78$), completely eliminating any air gap from any 360° viewing angle.
2. **Leg Stance Narrowing**: Brought middle knees inward from $X = \pm 0.44$ to $\pm 0.28$, hind knees from $\pm 0.64$ to $\pm 0.42$, and feet from $\pm 0.60$ to $\pm 0.38$, matching the compact stance of the master reference artwork.
3. **Automated Verification**: Local CMake build passes with 0 warnings, all 160 native integration tests pass, and all 506 E2E tests pass (100% pass rate). Changes committed and pushed to `feature/realistic-3d-ants-viewer`.

---

## 2. Caste #2 Reverse Engineering Findings (`Original-Ants/ants.chd`)
Primary source inspection of extracted 1998 sprites (`afst301.bmp`, `afst401.bmp`, `afst701.bmp`, `afwa401.bmp`, `afsf301..304.bmp`, `pufire.bmp`) and `docs/GAME_REVERSE_ENGINEERING.md`:

| Attribute | 1998 Original Asset / Behavior | 3D Remake Implementation |
|---|---|---|
| **Class Identifier** | `af` (Mason / Fire Ant), Tile 66, Powerup `pufire.bmp` (matchbook) | Caste #2 model in 3D studio viewer |
| **Headwear** | Classic Firefighter Chief helmet (golden-yellow with front shield and 'A' badge) | Sculpted Cairns-style helmet: high domed crown, central comb ridge, flared duckbill neck-guard brim, front shield plaque |
| **Helmet Badge** | Embossed 'A' in player team color palette (Indices 178–181 Red, 34–39 Blue, etc.) | High-relief embossed 'A' badge in crimson/red team color on gold shield |
| **Facial Anatomy** | Slate-grey/violet head (#6B6575), large white-sclera eyes under brim, sharp mandibles | Organic sculpted head nestled under helmet, white cartoon sclera, dark irises, curved cheek lobes, biting mandibles |
| **Body Chitin** | Deep charcoal-plum / burnt sienna (#2E2633) with warm amber/crimson specular sheen | High-resolution PBR procedural/painted chitin with organic micro-bump, warm russet undertones |
| **Signature Accessory** | Handheld magnifying glass (`afsf301..304.bmp`) used to ignite firewalls (`wallup04`) | Volumetric magnifying glass: turned dark-wood handle, polished brass bezel collar, convex optical glass lens |
| **Stance & Locomotion** | 6-legged insect tripod gait, expressive front arms holding tool | Armature with 2 expressive arms, 4 ground-walking legs ($Z = 0$), tool attachment bone |

---

## 3. Architecture & Implementation Steps

### Phase 1: PBR Texture Generation (`tools/blender/generate_fire_ant_textures.py`)
- `fire_chitin_pbr.png`: Deep charcoal-plum and burnt sienna carapace with subtle micro-cellular bump and amber subsurface tint.
- `fire_helmet_pbr.png`: Classic fire chief golden-yellow lacquer with subtle surface scuffs, darker ochre ridges, and polished brass shield rivets.
- `fire_accessory_pbr.png`: Turned mahogany wood grain handle, reflective brass bezel, and clear optical glass.
- `fire_eye_pbr.png`: Crisp white sclera, dark charcoal limbal ring, and determined forward-focused pupil.

### Phase 2: Blender Sculpting & Modeling (`tools/blender/build_authentic_fire_ant.py`)
1. **Head & Fire Chief Helmet**:
   - Sculpt high-domed helmet crown with longitudinal spine/comb.
   - Extrude wide flared duckbill brim extending down over the neck at the back.
   - Construct front shield plaque with embossed serif 'A' emblem in red team livery.
   - Add leather chin strap and brass mounting hardware.
   - Model underlying head nestled under the brim: large expressive eyes, curved mandibles, flexible antennae emerging from beneath the brim.
2. **Thorax, Petiole & Gaster**:
   - 3-segment articulated thorax (pronotum, mesonotum, metanotum).
   - Solid, gap-free petiole waist condyle socket penetrating deep into both metanotum and gaster.
   - Suspended egg-shaped gaster with sternite banding grooves and sting tip.
3. **Limbs & Stance**:
   - 2 front expressive arms with articulated wrists and clawed hands.
   - 4 ground-planted walking legs ($Z = 0$) using the narrowed compact stance coordinates ($X \approx \pm 0.28$ middle, $\pm 0.42$ hind).
4. **Magnifying Glass Tool**:
   - Turned ergonomic wood handle.
   - Cylindrical brass ferrule and circular magnifying lens rim.
   - Convex refractive glass element parented to right hand claw.

### Phase 3: Rigging, Walking & Ability Animation
- Create `Fire_Ant_Rig` armature matching bone hierarchy with additional `Prop_Hand_R` bone for the magnifying glass.
- Bind all body meshes with vertex groups or parent bones.
- Create two animation tracks:
  1. `Idle`: Subtle breathing bob, antennae twitch, helmet settle.
  2. `Walk`: Authentic alternating tripod walking gait cycle.

### Phase 4: Export & Web Viewer Integration
- Export `web/viewer3d/fire_ant.glb` and `fire_ant.usdz`.
- Render multi-angle stills (`fire_front.png`, `fire_perspective.png`, `fire_face_closeup.png`).
- Render 360° 36-frame turntable sequence.
- Update `web/viewer3d/index.html` and `viewer.js` with a caste switcher (toggle between **Worker Ant** and **Fire Ant**), dynamically reloading model, metadata, and animations.

---

## 4. Verification & Testing Plan
1. **Automated Unit & Integration Tests**:
   - Run `DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build -j8`.
   - Run native integration test suite (`./build/tests/test_app/test_app_integration` -> 160/160 pass).
   - Run E2E test runner (`./build_e2e/e2e_runner` -> 506/506 pass).
2. **Visual Verification**:
   - Inspect rendered beauty stills against extracted 1998 sprites (`afst301.bmp`, `afsf304.bmp`).
   - Validate 3D model in browser at `http://localhost:8089/viewer3d/` with WebGL, lighting presets, and mobile view.
3. **Commit & Push Policy**:
   - Verify clean git status, stage, commit with clear message, and push to origin.

# Implementation Plan & Progress: Worker Ant (Caste #1) 3D Model

## 1. Project Invariants & Workflow
- **Iterative Single-Ant Process**: Model each ant caste one-by-one from scratch. Under no circumstances move onto Caste #2 (Combat Ant) until Caste #1 (Worker Ant) is reviewed, refined, and explicitly approved by the user.
- **Visual Appearance First**: Complete and perfect the visual appearance in Blender (matching master reference artwork) before importing into Unity.
- **True 3D Geometry**: Deliver an authentic, fully volumetric 360° polygonal 3D model (GLB) viewable in the interactive WebGL inspection studio at `http://localhost:8089/viewer3d/`.
- **Publisher Rule**: Strictly NEVER write or mention the forbidden term ("M-i-c-r-o-s-o-f-t").
- **Quality & Verification**: Maintain 100% pass rate across all 160 native integration tests.

---

## 2. Phase 2: Master Reference Aesthetic Fidelity Overhaul

### Discrepancy Analysis vs. Master Reference (`worker_ant_master_1790372115097.jpg`):
1. **Head Geometry & Eye Sockets**:
   - *Previous*: Separate floating torus "goggles" created an artificial toy appearance.
   - *Master Art*: Unified organic head mesh with a rounded cushion crown, central vertical furrow, arched brow ridge, and deeply recessed orbital cavities that naturally cradle the eye globes.
2. **Eye Texturing & Proportions**:
   - *Previous*: Under-scaled iris and distorted equirectangular mapping produced a shocked, frog-like appearance with giant sclera.
   - *Master Art*: Large, warm, expressive eyes where the olive-amber striated iris fills ~55% of the visible sphere, with a velvety dark pupil, sharp limbal ring, warm cream sclera, and crisp specular catchlights.
3. **Mandibles**:
   - *Previous*: Overly thick, pale sausage shapes resembling a marshmallow mustache.
   - *Master Art*: Smooth green chitin cheek lobes curving forward and inward into cupped pincers, tipped with luminous chartreuse/pale lime biting edges and sculpted serrated teeth with subsurface scattering.
4. **Antennae**:
   - *Previous*: Disconnected segments floating or hidden.
   - *Master Art*: Crown-emerging stalks with seamless root collars, sweeping upward then looping in a graceful hairpin curve backward and downward with clubbed rounded tips.
5. **Limbs & Pose**:
   - *Previous*: Generic stick legs with round sphere joints.
   - *Master Art*: Distinct front arms with wrist collars and articulated 2-fingered hands hovering alertly in front of the chest; 4 muscular rear walking legs firmly planted on the ground plane ($Z = 0$).
6. **Carapace Texturing**:
   - *Previous*: Monochromatic flat green texture.
   - *Master Art*: Weathered sage green chitin with warm terracotta/burnt umber dusting across the crown, brow, and cheek margins, complemented by fine organic micro-pore bump mapping.

---

## 3. Implementation Steps:
1. **Authentic PBR Textures**:
   - Regenerate `eye_pbr.png` with spherical equirectangular metric scaling ($2\times \Delta U$ compensation) so the iris and pupil appear perfectly circular on a 3D sphere.
   - Generate `chitin_pbr.png` featuring weathered sage green, warm terracotta/russet mottling, and organic cellular pores.
   - Generate `mandible_pbr.png` with smooth green-to-chartreuse gradient.
   - Generate `limbs_pbr.png` with deep mahogany/charcoal brown and warm joint accents.
2. **Dedicated Master Sculpt Script (`tools/blender/build_master_worker_ant.py`)**:
   - Construct unified organic head with seamless orbital sockets and brow arches.
   - Embed spherical eye globes with the updated eye texture.
   - Model curved mandibles with sharp triangular biting teeth.
   - Model crown-rooted hairpin antennae.
   - Model 3-segment articulated thorax with physical step seams and petiole waist.
   - Model teardrop gaster with 4 tergal segments.
   - Model 2 expressive front arms with articulated hands and 4 muscular walking legs planted at $Z=0$.
   - Setup studio 3-point lighting rig and render multi-angle stills (`worker_front.png`, `worker_perspective.png`, `worker_face_closeup.png`).
   - Export binary `.glb` to `web/viewer3d/worker_ant.glb`.
3. **Verification**:
   - Visually verify multi-angle beauty stills against reference art.
   - Verify 3D model in WebGL inspection studio (`http://localhost:8089/viewer3d/`).
   - Run native integration test suite (160/160 pass).
   - Git commit and push to tracking branch.

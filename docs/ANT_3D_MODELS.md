# 3D Ant Models

Procedural stylized 3D models (`glTF 2.0` `.glb`) for all six ant classes of Ants (1998), built with Python.

Parked: kept as the source for a possible future 3D view; the `/test_3d/` page is gone, and no image builds or serves the models.

## Ant classes and animations

The generator in [`tools/ant_models/`](../tools/ant_models/) builds all six playable unit classes on a shared articulated base rig (`base_ant.py`, `leg_rig.py`, `mesh_core.py`, `materials.py`, `textures.py`, `gltf_writer.py`):

| Class | Module | Signature gear | Animations |
|---|---|---|---|
| Worker | `ant_worker.py` | Bare expressive head, curved antennae, articulated mandibles | 13 clips (Blink, Idle, Attack, Hit, Walk, Harvest, CantGo, Enter, Hatch, BlownBack, BlownUp, Stunned, Drown) |
| Combat | `ant_combat.py` | Olive soldier helmet with star emblem, rifle, ammo bandolier | 13 clips |
| Thief | `ant_thief.py` | Wide-brimmed yellow hat, dark shades, red bandana, lasso whip | 15 clips (including IdleLookout and Steal) |
| Bomber | `ant_bomber.py` | Rounded helmet with yellow visor brow and `A` insignia, lit bomb | 15 clips (including PlantBomb and DefuseBomb) |
| Swimmer | `ant_swimmer.py` | Diving mask, snorkel with valve, swim flippers, spade | 17 clips (including Swim, Dive, Surface, Shovel, ShovelWater; Swimmer does not drown) |
| Fire | `ant_fire.py` | Yellow firefighter helmet with badge, magnifying glass, extinguisher | 15 clips (including StartFire and Extinguish) |

All 88 animation clips match the original 1998 sprite durations from `ants.chd` (decoded from `docs/chd_table4_animations.json`, or from `asset_catalog/catalog_data.js` where `docs/` is absent). Hidden effect props (stun stars, splashes, whip segments, fire sparks, foam) stay scaled to zero at rest so rest bounds remain grounded at `Y = 0`.

## Building locally

Requirements: Python 3 with `numpy` and `Pillow`.

```bash
# Build all six .glb models, manifest.json, and original 1998 sprite bundles
python3 -m tools.ant_models.build_all

# Build a single class into a custom directory
python3 -m tools.ant_models.build_all --only worker --out /tmp/ant_models_out

# Render a headless multi-angle PNG contact sheet
python3 -m tools.ant_models.build_all --render-preview /tmp/ants_sheet.png --team green
```

Generated `.glb` and `.json` files in `tools/ant_models/models/` are gitignored and never committed.

## Verification

- Unit tests: `python3 tests/scripts/test_ant_models.py` (materials, glTF writer/validator, mesh primitives, rig clearance, all 88 clips, pure-stdlib import guards, and catalog fallback parity).
- The removed page: `/test_3d/` answers the site's normal 404 (`tests/scripts/test_nginx_headers.py`, `tests/scripts/test_nginx_clean_urls.py`).

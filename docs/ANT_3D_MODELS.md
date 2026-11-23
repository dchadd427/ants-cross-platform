# 3D Ant Models & Comparison Viewer (`/test_3d/`)

Procedural stylized 3D models (`glTF 2.0` `.glb`) for all six ant classes of Ants (1998), built with Python and paired with a zero-dependency WebGL comparison viewer served at `/test_3d/`.

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

All 88 animation clips match the original 1998 sprite durations from `ants.chd` (decoded from `asset_catalog/catalog_data.js` in container builds or `docs/chd_table4_animations.json` locally). Hidden effect props (stun stars, splashes, whip segments, fire sparks, foam) stay scaled to zero at rest so rest bounds remain grounded at `Y = 0`.

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

Generated `.glb` and `.json` files in `tools/ant_models/models/` are gitignored and never committed; the web Docker image builds them deterministically during image assembly.

To preview locally, serve `tools/ant_models/` over HTTP (for example `python3 -m http.server 8765 -d tools/ant_models`) and open `compare.html` or `viewer.html`.

## Viewer and `/test_3d/` web route

The viewer runs entirely on client-side WebGL with no external CDN scripts or dependencies:

- **Compare 1998 (`/test_3d/`)**: default entry point (`compare.html`), showing the 3D ant model side by side with the authentic palette-shifted 1998 sprite animation across all 22 actions, 8 compass directions, 4 team colors, and 8 lighting presets (Studio, Flat, Daylight, Dramatic, Rim, Spotlight, Moonlight, Sunset).
- **3D Studio (`/test_3d/viewer.html`)**: turntable studio view with free orbit/zoom, orthographic/perspective toggle, wireframe, auto-spin, blink trigger, custom team color picker, and direct `.glb` download.
- **Layout**: slim single-row top bar (mode switch, class selector, team color swatches, controls toggle), collapsible right inspector panel (3x3 camera compass rose, lighting stepper, display switches, and live metadata), and compact bottom playback dock.

### Routing, caching, and search engines

`docker/nginx.conf` serves `/test_3d/` from `/usr/share/nginx/html/test_3d/`:

- `/test_3d` and `/test_3d/compare.html` return `301` permanent redirects to `/test_3d/` preserving any query string (`/test_3d/compare.html` lives in `map $uri $ants_clean_address` so `index compare.html` never loops).
- Missing paths under `/test_3d/` return `404` with the site's `web/404.html` error page.
- All responses under `/test_3d/` carry `Cache-Control: no-cache, must-revalidate`, the site's standard security headers, and `X-Robots-Tag: noindex`. Both `compare.html` and `viewer.html` also include `<meta name="robots" content="noindex">` and are excluded from `sitemap.xml` (`tests/scripts/test_web_seo.py`).

### Docker build stage

`Dockerfile` builds the 3D assets in its own small stage (`ant-models-env` -> `ant-models`, based on a pinned `debian:13-slim` image with `python3`, `python3-numpy`, and `python3-pil`), leaving the Emscripten `emsdk-warm` and `builder` stages completely unchanged. Pre-compressed `.gz` copies of `.glb` and `.json` files are generated in that stage for `gzip_static on`, and `runner` copies `/src/test_3d/` with `COPY --from=ant-models`.

## Verification

- Unit tests: `python3 tests/scripts/test_ant_models.py` (materials, glTF writer/validator, mesh primitives, rig clearance, all 88 clips, pure-stdlib import guards, JS syntax & action registry parity, and catalog fallback parity).
- Web routing, headers, and SEO: `tests/scripts/test_nginx_headers.py`, `tests/scripts/test_nginx_clean_urls.py`, `tests/scripts/test_nginx_routes.py`, `tests/scripts/test_web_seo.py`, and `tests/scripts/test_docker_layers.py`.

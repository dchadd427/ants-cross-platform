#!/usr/bin/env python3
"""Build all available stylized 3D Ants models (.glb), manifest.json, and previews.

Usage:
  python3 tools/ant_models/build_all.py
  python3 tools/ant_models/build_all.py --render-preview /tmp/preview.png
  python3 tools/ant_models/build_all.py --include-base --render-preview /tmp/base.png
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import os
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
if THIS_DIR not in sys.path:
    sys.path.insert(0, THIS_DIR)

try:
    from .gltf_writer import write_glb
    from .materials import get_material, material_linear_color
except ImportError:
    from gltf_writer import write_glb  # type: ignore
    from materials import get_material, material_linear_color  # type: ignore

ANT_CLASSES: Tuple[str, ...] = ("worker", "combat", "thief", "bomber", "swimmer", "fire")
DEFAULT_OUT_DIR = os.path.join(THIS_DIR, "models")

# Canonical studio angles matching original 1998 ~33 deg isometric camera: (label, yaw_deg, pitch_deg).
PREVIEW_ANGLES: Tuple[Tuple[str, float, float], ...] = (
    ("Hero", -28.0, 22.0),
    ("301 S", 0.0, 33.0),
    ("201 SE", -45.0, 33.0),
    ("901 E", -90.0, 33.0),
    ("801 NE", -135.0, 33.0),
    ("701 N", 180.0, 33.0),
)


def _load_ant_builder(ant_id: str):
    """Return builder callable for `ant_id`, or None if module file does not exist."""
    if ant_id == "base":
        try:
            mod = importlib.import_module("tools.ant_models.base_ant")
        except ImportError:
            mod = importlib.import_module("base_ant")
        return mod.build_base_ant

    mod_name = f"ant_{ant_id}"
    mod_file = os.path.join(THIS_DIR, f"{mod_name}.py")
    if not os.path.isfile(mod_file):
        return None

    mod = None
    try:
        mod = importlib.import_module(f"tools.ant_models.{mod_name}")
    except ImportError as exc:
        if exc.name not in ("tools", "tools.ant_models", f"tools.ant_models.{mod_name}"):
            raise
    if mod is None:
        mod = importlib.import_module(mod_name)

    for attr in ("build_ant", f"build_{ant_id}", "build"):
        fn = getattr(mod, attr, None)
        if callable(fn):
            return fn
    raise AttributeError(f"Module {mod_name}.py defines none of build_ant(), build_{ant_id}(), or build()")


def build_scene_for(ant_id: str, *, bake_ao: bool = True):
    """Build and return SceneBuilder for `ant_id` (with baked crevice AO), or None if absent."""
    builder = _load_ant_builder(ant_id)
    if builder is None:
        return None
    result = builder()
    scene = result[0] if isinstance(result, tuple) else result
    if bake_ao and scene is not None:
        try:
            from .mesh_core import bake_scene_ao
        except ImportError:
            from mesh_core import bake_scene_ao  # type: ignore
        bake_scene_ao(scene)
    return scene


def discover_available_classes(include_base: bool = False) -> List[str]:
    found: List[str] = []
    if include_base:
        found.append("base")
    for cls_name in ANT_CLASSES:
        if os.path.isfile(os.path.join(THIS_DIR, f"ant_{cls_name}.py")):
            found.append(cls_name)
    return found


VIEWER_EXTS: Tuple[str, ...] = (".html", ".css", ".js")


def _discover_viewer_files() -> List[str]:
    """Return sorted viewer*.html / viewer*.css / viewer*.js filenames in tools/ant_models/."""
    found: List[str] = []
    if os.path.isdir(THIS_DIR):
        for fname in sorted(os.listdir(THIS_DIR)):
            if fname.startswith("viewer") and fname.endswith(VIEWER_EXTS):
                if os.path.isfile(os.path.join(THIS_DIR, fname)):
                    found.append(fname)
    return found


def _file_sha256_hex(path: str, length: int = 12) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:length]


def _compute_manifest_hashes(entries: Sequence[Dict[str, Any]]) -> Tuple[str, str, str]:
    """Return deterministic (version, build_hash, models_hash) across models + all viewer files."""
    sorted_models = sorted(entries, key=lambda e: str(e.get("id", "")))
    m_hasher = hashlib.sha256()
    for e in sorted_models:
        m_hasher.update(f"{e['id']}:{e.get('sha256', '')}\n".encode("utf-8"))
    models_hash = m_hasher.hexdigest()[:16]

    b_hasher = hashlib.sha256()
    b_hasher.update(f"models:{models_hash}\n".encode("utf-8"))
    for fname in _discover_viewer_files():
        fpath = os.path.join(THIS_DIR, fname)
        b_hasher.update(f"{fname}:{_file_sha256_hex(fpath, length=64)}\n".encode("utf-8"))
    build_hash = b_hasher.hexdigest()[:16]
    version = f"v-{build_hash[:8]}"
    return version, build_hash, models_hash


def _model_sort_key(entry: Dict[str, Any]) -> Tuple[int, str]:
    ant_id = str(entry.get("id", ""))
    try:
        return (ANT_CLASSES.index(ant_id), ant_id)
    except ValueError:
        return (len(ANT_CLASSES), ant_id)


def build_all(
    out_dir: str = DEFAULT_OUT_DIR,
    only: Optional[Sequence[str]] = None,
    *,
    include_base: bool = False,
    quiet: bool = False,
) -> Tuple[Dict[str, Any], List[Tuple[str, Any]]]:
    """Build available models into `out_dir`, write manifest.json, return (manifest, built_scenes)."""
    os.makedirs(out_dir, exist_ok=True)
    targets = list(only) if only else (["base"] + list(ANT_CLASSES) if include_base else list(ANT_CLASSES))
    manifest_path = os.path.join(out_dir, "manifest.json")

    # Preserve untouched model entries when performing a partial `--only` rebuild.
    preserved: Dict[str, Dict[str, Any]] = {}
    if only and os.path.isfile(manifest_path):
        try:
            with open(manifest_path, encoding="utf-8") as f:
                prev = json.load(f)
            for item in prev.get("models", []):
                mid = item.get("id")
                mfile = item.get("file")
                if mid and mfile and os.path.isfile(os.path.join(out_dir, mfile)):
                    entry_copy = dict(item)
                    if not entry_copy.get("sha256"):
                        entry_copy["sha256"] = _file_sha256_hex(os.path.join(out_dir, mfile), 12)
                    preserved[mid] = entry_copy
        except (OSError, ValueError, TypeError):
            preserved = {}

    built_entries: Dict[str, Dict[str, Any]] = dict(preserved) if only else {}
    built_scenes: List[Tuple[str, Any]] = []

    for ant_id in targets:
        scene = build_scene_for(ant_id)
        if scene is None:
            if not quiet:
                print(f"  [skip] {ant_id}: ant_{ant_id}.py not present yet")
            continue
        filename = f"{ant_id}.glb"
        out_path = os.path.join(out_dir, filename)
        stats = write_glb(scene, out_path)
        sha12 = _file_sha256_hex(out_path, 12)
        entry = {
            "id": ant_id,
            "file": filename,
            "sha256": sha12,
            "byte_length": stats["byte_length"],
            "triangles": stats["triangles"],
            "vertices": stats["vertices"],
            "nodes": stats["nodes"],
            "materials": stats["materials"],
            "animations": stats.get("animations", []),
            "textures": stats.get("textures", 0),
            "images": stats.get("images", 0),
            "bbox_min": stats["bbox_min"],
            "bbox_max": stats["bbox_max"],
            "dimensions": stats["dimensions"],
        }
        built_entries[ant_id] = entry
        built_scenes.append((ant_id, scene))
        if not quiet:
            dims = stats["dimensions"]
            print(
                f"  [ok]   {ant_id:8s} -> {filename} "
                f"({stats['triangles']} tris, {stats['byte_length']:,} B, sha={sha12}, "
                f"size {dims[0]:.2f}x{dims[1]:.2f}x{dims[2]:.2f}, min_y={stats['bbox_min'][1]:.4f})"
            )

    manifest_models = sorted(built_entries.values(), key=_model_sort_key)
    version, build_hash, models_hash = _compute_manifest_hashes(manifest_models)
    manifest = {
        "schema_version": 1,
        "version": version,
        "build_hash": build_hash,
        "models_hash": models_hash,
        "coordinate_system": "glTF 2.0 right-handed (+Y up, +Z front, +X ant-left, feet at Y=0)",
        "models": manifest_models,
    }
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    return manifest, built_scenes


# ---------------------------------------------------------------------------
# Pure-Python software studio renderer (numpy + Pillow) for headless previews
# ---------------------------------------------------------------------------

def _aces_srgb(rgb):
    import numpy as np

    x = np.maximum(rgb, 0.0)
    mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
    mapped = np.clip(mapped, 0.0, 1.0)
    srgb = np.where(mapped <= 0.0031308, 12.92 * mapped, 1.055 * (mapped ** (1.0 / 2.4)) - 0.055)
    return np.clip(srgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)


def _shade_vinyl(normals, view_dir, base_rgb, metallic: float, roughness: float, ao=None):
    """Satin organic exoskeleton PBR studio approximation with baked crevice AO."""
    import numpy as np

    n = normals / np.maximum(1e-8, np.linalg.norm(normals, axis=-1, keepdims=True))
    v = view_dir / max(1e-8, float(np.linalg.norm(view_dir)))
    albedo = np.clip(base_rgb, 0.0, 1.0)
    ao_val = np.clip(ao, 0.35, 1.0) if ao is not None else 1.0

    # 3-point studio lights: crisp warm key, cool fill, crisp studio rim backlight.
    lights = (
        (np.array([-0.46, 0.74, 0.49]), np.array([1.70, 1.63, 1.52])),
        (np.array([0.72, 0.32, 0.44]), np.array([0.34, 0.40, 0.52])),
        (np.array([0.24, 0.56, -0.79]), np.array([0.72, 0.80, 0.92])),
    )

    f0 = (1.0 - metallic) * 0.04 + metallic * albedo
    diffuse_albedo = albedo * (1.0 - metallic)
    alpha = max(0.08, roughness * roughness)
    spec_exp = max(8.0, (2.0 / (alpha * alpha)) - 2.0)
    spec_norm = (spec_exp + 2.0) / 14.0

    # Hemisphere ambient bounce scaled by baked crevice AO for crisp socket & seam depth.
    hemi = 0.5 * (n[..., 1:2] + 1.0)
    ambient = (0.06 * (1.0 - hemi) * np.array([0.26, 0.25, 0.24]) + 0.19 * hemi * np.array([0.76, 0.83, 0.94]))
    color = diffuse_albedo * ambient * ao_val

    ndv = np.clip(np.sum(n * v, axis=-1, keepdims=True), 0.0, 1.0)
    fresnel_rim = (1.0 - ndv) ** 3.6
    spec_ao = 0.55 + 0.45 * ao_val

    for l_vec, l_col in lights:
        l_dir = l_vec / float(np.linalg.norm(l_vec))
        ndl = np.clip(np.sum(n * l_dir, axis=-1, keepdims=True), 0.0, 1.0)
        h = l_dir + v
        h /= max(1e-8, float(np.linalg.norm(h)))
        ndh = np.clip(np.sum(n * h, axis=-1, keepdims=True), 0.0, 1.0)

        spec = spec_norm * (ndh ** min(spec_exp, 140.0)) * f0 * ((1.0 - 0.55 * roughness) ** 1.2)
        coat = 0.09 * (ndh ** 68.0) if roughness < 0.25 else 0.0
        color = color + (diffuse_albedo * ndl * (0.45 + 0.55 * ao_val) + (spec + coat) * spec_ao * (ndl > 0.0)) * l_col

    rim_strength = 0.03 + 0.11 * ((1.0 - roughness) ** 1.6)
    color = color + fresnel_rim * rim_strength * spec_ao * np.array([0.68, 0.78, 0.92])
    return color


def render_scene_orthographic(
    scene: Any,
    *,
    width: int = 220,
    height: int = 220,
    yaw_deg: float = -28.0,
    pitch_deg: float = 22.0,
    team: str = "green",
    view_span: Optional[float] = None,
):
    """Render `scene` from an orthographic studio camera to an RGB uint8 numpy image."""
    import numpy as np

    bmin, bmax = scene.bounds()
    center = 0.5 * (bmin + bmax)
    center[1] = max(center[1], 0.48 * float(bmax[1]))
    extent = float(np.linalg.norm(bmax - bmin))
    span = float(view_span) if view_span is not None else max(1.38, extent * 0.98)

    yaw = math.radians(yaw_deg)
    pitch = math.radians(pitch_deg)
    cam_dir = np.array(
        [math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)],
        dtype=np.float64,
    )
    forward = -cam_dir
    world_up = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    right = np.cross(forward, world_up)
    rlen = float(np.linalg.norm(right))
    right = right / rlen if rlen > 1e-8 else np.array([1.0, 0.0, 0.0])
    up = np.cross(right, forward)

    # Studio backdrop gradient + soft turntable ground shadow ellipse on Y = 0.
    yy, xx = np.mgrid[0:height, 0:width]
    bg_t = (yy / max(1, height - 1))[:, :, None]
    color_buf = (1.0 - bg_t) * np.array([0.050, 0.060, 0.082]) + bg_t * np.array([0.024, 0.028, 0.038])
    z_buf = np.full((height, width), -1e9, dtype=np.float64)

    # Project ray from each pixel onto ground plane Y = 0 for a subtle contact shadow.
    if abs(forward[1]) > 0.05:
        scale_px = span / float(min(width, height))
        u_world = (xx - 0.5 * (width - 1)) * scale_px
        v_world = (0.5 * (height - 1) - yy) * scale_px
        ray_orig_y = center[1] + u_world * right[1] + v_world * up[1]
        t_gnd = -ray_orig_y / forward[1]
        gx = center[0] + u_world * right[0] + v_world * up[0] + t_gnd * forward[0]
        gz = center[2] + u_world * right[2] + v_world * up[2] + t_gnd * forward[2]
        rad_x = max(0.32, 0.58 * float(bmax[0] - bmin[0]))
        rad_z = max(0.38, 0.54 * float(bmax[2] - bmin[2]))
        d2 = (gx / rad_x) ** 2 + ((gz - center[2]) / rad_z) ** 2
        plinth = np.exp(-d2 * 0.85) * 0.045
        shadow = np.exp(-d2 * 2.6) * 0.48
        color_buf = color_buf + plinth[:, :, None] * np.array([0.75, 0.82, 0.95])
        color_buf = color_buf * (1.0 - shadow[:, :, None])

    scale = float(min(width, height)) / span
    cx_px = 0.5 * (width - 1)
    cy_px = 0.5 * (height - 1)

    all_world = scene.world_parts() if hasattr(scene, "world_parts") else scene.all_parts()
    opaque_parts = []
    blend_parts = []
    for part in all_world:
        if len(part.indices) == 0:
            continue
        spec = get_material(part.material)
        if spec.resolved_alpha_mode() == "BLEND" or spec.base_color[3] < 0.999:
            blend_parts.append(part)
        else:
            opaque_parts.append(part)

    for is_blend, bucket in ((False, opaque_parts), (True, blend_parts)):
        for part in bucket:
            spec = get_material(part.material)
            rgba = material_linear_color(part.material, team=team)
            base_rgb = np.array(rgba[:3], dtype=np.float64)
            base_alpha = float(rgba[3])
            v_cols = part.colors.astype(np.float64) if getattr(part, "colors", None) is not None else None

            world_pos = part.positions.astype(np.float64)
            pos = world_pos - center[None, :]
            nrm = part.normals.astype(np.float64)
            idx = part.indices.reshape(-1, 3)

            sx = cx_px + (pos @ right) * scale
            sy = cy_px - (pos @ up) * scale
            sz = pos @ cam_dir

            x0, x1, x2 = sx[idx[:, 0]], sx[idx[:, 1]], sx[idx[:, 2]]
            y0, y1, y2 = sy[idx[:, 0]], sy[idx[:, 1]], sy[idx[:, 2]]
            z0, z1, z2 = sz[idx[:, 0]], sz[idx[:, 1]], sz[idx[:, 2]]

            # Screen-space signed area (+Y is down in image coords, so CCW front faces have area < 0).
            area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
            vis = np.nonzero(area < -1e-6)[0]
            if len(vis) == 0:
                continue

            tri_z = (z0[vis] + z1[vis] + z2[vis]) / 3.0
            # Opaque front-to-back for early z-reject; translucent back-to-front for clean blending.
            vis = vis[np.argsort(tri_z if is_blend else -tri_z)]

            for t_i in vis:
                ax, ay, az = x0[t_i], y0[t_i], z0[t_i]
                bx, by, bz = x1[t_i], y1[t_i], z1[t_i]
                cx, cy, cz = x2[t_i], y2[t_i], z2[t_i]

                min_x = max(0, int(math.floor(min(ax, bx, cx))))
                max_x = min(width - 1, int(math.ceil(max(ax, bx, cx))))
                min_y = max(0, int(math.floor(min(ay, by, cy))))
                max_y = min(height - 1, int(math.ceil(max(ay, by, cy))))
                if min_x > max_x or min_y > max_y:
                    continue

                inv_a = 1.0 / area[t_i]
                px = np.arange(min_x, max_x + 1, dtype=np.float64)[None, :] + 0.5
                py = np.arange(min_y, max_y + 1, dtype=np.float64)[:, None] + 0.5

                w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * inv_a
                w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * inv_a
                w2 = 1.0 - w0 - w1
                mask = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
                if not np.any(mask):
                    continue

                z_interp = w0 * az + w1 * bz + w2 * cz
                sub_z = z_buf[min_y : max_y + 1, min_x : max_x + 1]
                hit = mask & (z_interp > sub_z)
                if not np.any(hit):
                    continue

                i0, i1, i2 = idx[t_i]
                n_pix = (
                    w0[hit, None] * nrm[i0]
                    + w1[hit, None] * nrm[i1]
                    + w2[hit, None] * nrm[i2]
                )
                ao_pix = (
                    w0[hit, None] * v_cols[i0]
                    + w1[hit, None] * v_cols[i1]
                    + w2[hit, None] * v_cols[i2]
                ) if v_cols is not None else None
                shaded = _shade_vinyl(n_pix, cam_dir, base_rgb, spec.metallic, spec.roughness, ao=ao_pix)
                sub_col = color_buf[min_y : max_y + 1, min_x : max_x + 1]
                if not is_blend:
                    sub_z[hit] = z_interp[hit]
                    sub_col[hit] = shaded
                else:
                    # Fresnel glass reflection keeps edges/highlights crisp while eyes read clearly behind.
                    n_unit = n_pix / np.maximum(1e-8, np.linalg.norm(n_pix, axis=-1, keepdims=True))
                    ndv = np.clip(np.sum(n_unit * cam_dir, axis=-1, keepdims=True), 0.0, 1.0)
                    spec_luma = np.clip(np.mean(shaded, axis=-1, keepdims=True), 0.0, 1.0)
                    g_alpha = np.clip(base_alpha + ((1.0 - ndv) ** 3.0) * 0.42 + spec_luma * 0.45, 0.0, 0.90)
                    sub_col[hit] = g_alpha * shaded + (1.0 - g_alpha) * sub_col[hit]

    return _aces_srgb(color_buf)


def render_preview_sheet(
    built_scenes: Sequence[Tuple[str, Any]],
    out_path: str,
    *,
    team: str = "green",
    tile_size: int = 220,
    angles: Sequence[Tuple[str, float, float]] = PREVIEW_ANGLES,
) -> str:
    """Render a multi-angle studio contact sheet PNG for all `built_scenes`."""
    from PIL import Image, ImageDraw

    if not built_scenes:
        raise ValueError("No scenes available to render preview contact sheet")

    header_h = 26
    cols = len(angles)
    rows = len(built_scenes)
    sheet = Image.new("RGB", (cols * tile_size, rows * (tile_size + header_h)), (24, 28, 36))
    draw = ImageDraw.Draw(sheet)

    for r_idx, (ant_id, scene) in enumerate(built_scenes):
        y_top = r_idx * (tile_size + header_h)
        for c_idx, (label, yaw, pitch) in enumerate(angles):
            x_left = c_idx * tile_size
            arr = render_scene_orthographic(
                scene,
                width=tile_size,
                height=tile_size,
                yaw_deg=yaw,
                pitch_deg=pitch,
                team=team,
            )
            tile_img = Image.fromarray(arr, mode="RGB")
            sheet.paste(tile_img, (x_left, y_top + header_h))
            draw.rectangle([x_left, y_top, x_left + tile_size - 1, y_top + header_h - 1], fill=(32, 37, 48))
            draw.text((x_left + 8, y_top + 6), f"{ant_id} | {label}", fill=(228, 234, 244))

    parent = os.path.dirname(os.path.abspath(out_path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    sheet.save(out_path)
    return out_path


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Build Ants stylized 3D .glb models and manifest.")
    parser.add_argument("--out-dir", "--out", dest="out_dir", default=DEFAULT_OUT_DIR, help="Output directory for .glb and manifest.json")
    parser.add_argument("--only", nargs="+", help="Build only specific model ids (space- or comma-separated, e.g. worker,fire)")
    parser.add_argument("--include-base", action="store_true", help="Also build base_ant as base.glb")
    parser.add_argument("--render-preview", default="", help="Optional path to save a headless PNG contact sheet")
    parser.add_argument("--team", default="green", choices=("green", "red", "blue", "black"), help="Preview team color")
    parser.add_argument("--tile-size", type=int, default=220, help="Preview tile pixel size (default: 220)")
    parser.add_argument("--quiet", action="store_true", help="Suppress per-model log lines")
    args = parser.parse_args(argv)

    only_ids: Optional[List[str]] = None
    if args.only:
        only_ids = [item.strip() for token in args.only for item in token.split(",") if item.strip()]

    manifest, built = build_all(
        out_dir=args.out_dir,
        only=only_ids,
        include_base=args.include_base,
        quiet=args.quiet,
    )
    if not args.quiet:
        print(f"Built {len(manifest['models'])} model(s) in {args.out_dir}")

    if args.render_preview:
        if not built:
            # Render base_ant if no specialist modules exist yet.
            base_scene = build_scene_for("base")
            built = [("base", base_scene)]
        out_png = render_preview_sheet(built, args.render_preview, team=args.team, tile_size=args.tile_size)
        if not args.quiet:
            print(f"Rendered preview contact sheet -> {out_png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

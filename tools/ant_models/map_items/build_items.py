"""Build 3D map item `.glb` assets, `manifest.json`, and 1998 sprite bundle `orig_items.json`.

Usage:
    python3 -m tools.ant_models.map_items.build_items --out tools/ant_models/models/items
    python3 -m tools.ant_models.map_items.build_items --only bearhed,ducky1,fdburgr1
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

try:
    from .. import gltf_writer
    from . import CATEGORIES, MAP_ITEM_SPECS, SPECS_BY_ID, MapItemSpec, resolve_builder
except ImportError:
    _PARENT = Path(__file__).resolve().parent.parent
    if str(_PARENT) not in sys.path:
        sys.path.insert(0, str(_PARENT))
    import gltf_writer  # type: ignore
    from map_items import CATEGORIES, MAP_ITEM_SPECS, SPECS_BY_ID, MapItemSpec, resolve_builder  # type: ignore


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def build_orig_items_bundle(specs: Sequence[MapItemSpec], out_path: Path) -> Dict[str, Any]:
    """Extract original 1998 sprites & frame metadata from `asset_catalog/` and `chd_table4_animations.json`."""
    root = _repo_root()
    t4_path = root / "docs" / "chd_table4_animations.json"
    sprites_dir = root / "asset_catalog" / "sprites"
    by_idx: Dict[int, Dict[str, Any]] = {}
    if t4_path.exists():
        with t4_path.open("r", encoding="utf-8") as fh:
            for entry in json.load(fh):
                by_idx[int(entry["entry_index"])] = entry

    try:
        import io as _io
        from PIL import Image as _PilImage
    except ImportError:
        _io = None  # type: ignore
        _PilImage = None  # type: ignore

    items_out: Dict[str, Any] = {}
    for spec in specs:
        entry = by_idx.get(spec.chd_id)
        frames_out: List[Dict[str, Any]] = []
        total_ms = 0
        if entry is not None:
            total_ms = int(entry.get("total_duration_ms", 0) or 0)
            subitems = entry.get("subitems", [])
            has_multi_layer = any(len(sub.get("frames", [])) > 1 for sub in subitems)

            if has_multi_layer and _PilImage is not None and _io is not None:
                # Composite all authentic 1998 CHD sprite layers (e.g. hill mound + waving flag, daisy stem + head)
                # onto one shared bounding canvas so animation frames stay rock-steady with zero jitter!
                loaded_subs = []
                min_x, min_y, max_x, max_y = 99999, 99999, -99999, -99999
                for sub in subitems:
                    dur_ms = int(sub.get("duration_ms", 120) or 120)
                    layers = []
                    for fr in sub.get("frames", []):
                        sid = int(fr["sprite_index"])
                        sp_file = sprites_dir / f"sprite_{sid:04d}.png"
                        if not sp_file.exists():
                            continue
                        im = _PilImage.open(sp_file).convert("RGBA")
                        dx, dy = int(fr.get("dx", 0)), int(fr.get("dy", 0))
                        min_x = min(min_x, dx)
                        min_y = min(min_y, dy)
                        max_x = max(max_x, dx + im.width)
                        max_y = max(max_y, dy + im.height)
                        layers.append((sid, dx, dy, im))
                    if layers:
                        loaded_subs.append((dur_ms, layers))
                    if len(loaded_subs) >= 8:
                        break
                if loaded_subs and max_x > min_x and max_y > min_y:
                    cw, ch = max_x - min_x, max_y - min_y
                    for dur_ms, layers in loaded_subs:
                        canvas = _PilImage.new("RGBA", (cw, ch), (0, 0, 0, 0))
                        for _sid, dx, dy, im in reversed(layers):
                            canvas.alpha_composite(im, (dx - min_x, dy - min_y))
                        buf = _io.BytesIO()
                        canvas.save(buf, format="PNG", optimize=True)
                        raw = buf.getvalue()
                        frames_out.append({
                            "sprite_index": layers[0][0],
                            "w": cw,
                            "h": ch,
                            "duration_ms": max(60, dur_ms),
                            "data_uri": "data:image/png;base64," + base64.b64encode(raw).decode("ascii"),
                        })

            if not frames_out:
                seen_sids: List[int] = []
                for sub in subitems:
                    dur_ms = int(sub.get("duration_ms", 120) or 120)
                    sub_frames = sub.get("frames", [])
                    if not sub_frames:
                        continue
                    sid = int(sub_frames[0]["sprite_index"])
                    sp_file = sprites_dir / f"sprite_{sid:04d}.png"
                    if not sp_file.exists():
                        continue
                    if seen_sids and sid == seen_sids[-1]:
                        continue
                    seen_sids.append(sid)
                    raw = sp_file.read_bytes()
                    # Parse PNG IHDR dimensions directly (pure stdlib, no PIL required!).
                    w = int.from_bytes(raw[16:20], "big") if len(raw) >= 24 else 0
                    h = int.from_bytes(raw[20:24], "big") if len(raw) >= 24 else 0
                    frames_out.append({
                        "sprite_index": sid,
                        "w": w,
                        "h": h,
                        "duration_ms": max(60, dur_ms),
                        "data_uri": "data:image/png;base64," + base64.b64encode(raw).decode("ascii"),
                    })
                    if len(frames_out) >= 8:
                        break
        items_out[spec.id] = {
            "id": spec.id,
            "chd_id": spec.chd_id,
            "chd_name": spec.chd_name,
            "total_ms": total_ms,
            "frame_count": len(frames_out),
            "frames": frames_out,
        }

    bundle = {
        "schema_version": 1,
        "item_count": len(items_out),
        "items": items_out,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(bundle, indent=2) + "\n", encoding="utf-8")
    return bundle


def build_all_items(out_dir: Path, only: Optional[Sequence[str]] = None) -> Dict[str, Any]:
    """Build `.glb` files, `manifest.json`, and `orig_items.json` for selected or all registered items."""
    out_dir.mkdir(parents=True, exist_ok=True)
    if only:
        targets = []
        for raw in only:
            for tok in str(raw).split(","):
                k = tok.strip()
                if not k:
                    continue
                if k not in SPECS_BY_ID:
                    raise ValueError(f"Unknown map item {k!r}; expected one of: {', '.join(SPECS_BY_ID)}")
                targets.append(SPECS_BY_ID[k])
    else:
        targets = list(MAP_ITEM_SPECS)

    manifest_path = out_dir / "manifest.json"
    existing_items: Dict[str, Dict[str, Any]] = {}
    if manifest_path.exists():
        try:
            old = json.loads(manifest_path.read_text(encoding="utf-8"))
            for row in old.get("items", []):
                if isinstance(row, dict) and "id" in row:
                    existing_items[row["id"]] = row
        except Exception:
            existing_items = {}

    for spec in targets:
        builder = resolve_builder(spec)
        scene = builder()
        try:
            from ..mesh_core import bake_scene_ao
        except ImportError:
            from mesh_core import bake_scene_ao  # type: ignore
        bake_scene_ao(scene)
        glb_bytes = gltf_writer.scene_to_glb_bytes(scene)
        stats = gltf_writer.validate_glb(glb_bytes)
        glb_path = out_dir / f"{spec.id}.glb"
        glb_path.write_bytes(glb_bytes)
        sha = hashlib.sha256(glb_bytes).hexdigest()[:12]
        bmin, bmax = scene.bounds()
        size = [round(float(bmax[k] - bmin[k]), 4) for k in range(3)]
        existing_items[spec.id] = {
            "id": spec.id,
            "label": spec.label,
            "category": spec.category,
            "family": spec.family,
            "stage_label": spec.stage_label,
            "chd_id": spec.chd_id,
            "chd_name": spec.chd_name,
            "default_yaw": spec.default_yaw,
            "default_pitch": spec.default_pitch,
            "file": f"{spec.id}.glb",
            "bytes": int(len(glb_bytes)),
            "sha256_12": sha,
            "triangles": int(stats["triangles"]),
            "vertices": int(stats["vertices"]),
            "min_y": round(float(bmin[1]), 4),
            "size": size,
            "animations": list(stats.get("animations", [])),
        }
        print(
            f"  [ok] {spec.id:12s} -> {glb_path.name} "
            f"({stats['triangles']} tris, {len(glb_bytes):,} B, sha={sha}, "
            f"size {size[0]:.2f}x{size[1]:.2f}x{size[2]:.2f}, min_y={bmin[1]:.4f})"
        )

    ordered_rows = [existing_items[s.id] for s in MAP_ITEM_SPECS if s.id in existing_items]
    orig_path = out_dir / "orig_items.json"
    orig_bundle = build_orig_items_bundle(MAP_ITEM_SPECS, orig_path)
    print(f"  [ok] orig_items   -> {orig_path.name} ({orig_path.stat().st_size:,} B, {orig_bundle['item_count']} items)")

    # Compute deterministic content hash for cache busting & live reload.
    h = hashlib.sha256()
    for row in ordered_rows:
        h.update(f"{row['id']}:{row['sha256_12']}".encode("ascii"))
    manifest = {
        "schema_version": 1,
        "build_id": h.hexdigest()[:8],
        "categories": [{"id": cid, "label": lbl} for cid, lbl in CATEGORIES],
        "built_count": len(ordered_rows),
        "total_catalog_sprites": 211,
        "items": ordered_rows,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Built {len(targets)} map item(s) in {out_dir} (total {len(ordered_rows)} in manifest)")
    return manifest


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Build 3D map item GLB models and 1998 reference sprites.")
    parser.add_argument(
        "--out",
        "--output-dir",
        dest="out_dir",
        default=str(Path(__file__).resolve().parent.parent / "models" / "items"),
        help="Destination directory for item GLBs, manifest.json, and orig_items.json",
    )
    parser.add_argument("--only", nargs="*", help="Optional comma- or space-separated item IDs to rebuild")
    args = parser.parse_args(argv)
    build_all_items(Path(args.out_dir), only=args.only)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

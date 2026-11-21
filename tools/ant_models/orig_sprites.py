#!/usr/bin/env python3
"""Deterministic 1998 original Ants sprite-sheet bundle generator (`orig_sprites.json`).

Reads `docs/chd_table4_animations.json` and decodes authentic palette-shifted team
sprites from `Original-Ants/ants.chd` via `tools/front_page_art/artlib.py` for all
6 ant classes x 5 canonical isometric angles x 4 original teams.

Safe to import in pure-stdlib environments (Pillow and `artlib` are imported lazily
when generating the bundle).
"""
from __future__ import annotations

import base64
import io
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

THIS_DIR = Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
DEFAULT_ANIM_JSON = REPO_ROOT / "docs" / "chd_table4_animations.json"
DEFAULT_CHD_PATH = REPO_ROOT / "Original-Ants" / "ants.chd"
DEFAULT_OUT_PATH = THIS_DIR / "models" / "orig_sprites.json"

CLASS_PREFIXES: Tuple[Tuple[str, str], ...] = (
    ("worker", "agst"),
    ("combat", "acst"),
    ("thief", "atst"),
    ("bomber", "abst"),
    ("swimmer", "asst"),
    ("fire", "afst"),
)
ATTACK_PREFIXES: Tuple[Tuple[str, str], ...] = (
    ("worker", "agat"),
    ("combat", "acat"),
    ("thief", "atat"),
    ("bomber", "abat"),
    ("swimmer", "asat"),
    ("fire", "afat"),
)
HIT_PREFIXES: Tuple[Tuple[str, str], ...] = (
    ("worker", "aggh"),
    ("combat", "acgh"),
    ("thief", "atgh"),
    ("bomber", "abgh"),
    ("swimmer", "asgh"),
    ("fire", "afgh"),
)
ANT_CLASSES: Tuple[str, ...] = tuple(cls for cls, _ in CLASS_PREFIXES)
CANONICAL_ANGLES: Tuple[str, ...] = ("301", "201", "901", "801", "701")
ORIGINAL_TEAMS: Tuple[str, ...] = ("green", "red", "blue", "black")
ACTIONS: Tuple[str, ...] = ("idle", "attack", "hit")


def can_generate_orig_sprites(
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    chd_path: Path = DEFAULT_CHD_PATH,
) -> bool:
    """Return True if Pillow and the original animation/CHD sources are available."""
    try:
        import PIL.Image  # noqa: F401
    except ImportError:
        return False
    return anim_json.is_file() and chd_path.is_file()


def _load_artlib():
    front_art_dir = str(REPO_ROOT / "tools" / "front_page_art")
    if front_art_dir not in sys.path:
        sys.path.insert(0, front_art_dir)
    import artlib  # type: ignore

    return artlib


def _encode_compact_png_bytes(image: Any) -> bytes:
    """Encode an RGBA Pillow image into a compact deterministic lossless PNG."""
    from PIL import Image

    rgba_img = image.convert("RGBA")
    colors = rgba_img.getcolors(maxcolors=256)
    buf = io.BytesIO()
    if colors is not None and len(colors) > 0:
        # Sort deterministically by (count desc, RGBA tuple asc) so PNG bytes never drift.
        palette_rgba = [c for _, c in sorted(colors, key=lambda item: (-item[0], item[1]))]
        index_map = {c: idx for idx, c in enumerate(palette_rgba)}
        packed = Image.new("P", rgba_img.size)
        raw_pixels = rgba_img.get_flattened_data() if hasattr(rgba_img, "get_flattened_data") else rgba_img.getdata()
        packed.putdata([index_map[px] for px in raw_pixels])
        packed.putpalette([ch for rgba in palette_rgba for ch in rgba[:3]])
        alpha_table = bytes(rgba[3] for rgba in palette_rgba)
        if min(alpha_table) < 255:
            packed.save(buf, format="PNG", optimize=True, compress_level=9, transparency=alpha_table)
        else:
            packed.save(buf, format="PNG", optimize=True, compress_level=9)
        return buf.getvalue()

    rgba_img.save(buf, format="PNG", optimize=True, compress_level=9)
    return buf.getvalue()


def _png_data_uri(image: Any) -> str:
    raw = _encode_compact_png_bytes(image)
    return "data:image/png;base64," + base64.b64encode(raw).decode("ascii")


def _load_animations_by_name(anim_json: Path) -> Dict[str, Dict[str, Any]]:
    raw = json.loads(anim_json.read_text(encoding="utf-8"))
    by_name: Dict[str, Dict[str, Any]] = {}
    items = raw if isinstance(raw, list) else raw.get("entries", raw.get("animations", []))
    for entry in items:
        if isinstance(entry, dict) and isinstance(entry.get("name"), str):
            by_name[entry["name"].lower()] = entry
    return by_name


def _composite_frame(
    layers: Sequence[Tuple[int, int, int]],
    team: str,
    min_x: int,
    min_y: int,
    fw: int,
    fh: int,
    sprite_cache: Dict[Tuple[int, str], Any],
    artlib: Any,
) -> Any:
    from PIL import Image

    canvas = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
    for sid, dx, dy in layers:
        key = (sid, team)
        spr = sprite_cache.get(key)
        if spr is None:
            spr = artlib.team_ant(sid, team)
            sprite_cache[key] = spr
        canvas.alpha_composite(spr, (dx - min_x, dy - min_y))
    return canvas


def _build_action_sprites(
    *,
    anims: Dict[str, Dict[str, Any]],
    anim_json: Path,
    class_prefixes: Sequence[Tuple[str, str]],
    angles: Sequence[str],
    teams: Sequence[str],
    sprite_cache: Dict[Tuple[int, str], Any],
    artlib: Any,
    ref_world_foot_y: Optional[Dict[Tuple[str, str], int]] = None,
    include_attack_aliases: bool = False,
) -> Tuple[Dict[str, Dict[str, Any]], Dict[Tuple[str, str], int]]:
    from PIL import Image

    sprites_out: Dict[str, Dict[str, Any]] = {}
    world_foot_y_out: Dict[Tuple[str, str], int] = {}

    for cls_name, prefix in class_prefixes:
        cls_map: Dict[str, Any] = {}
        for angle in angles:
            anim_name = f"{prefix}{angle}"
            entry = anims.get(anim_name)
            if entry is None:
                raise KeyError(f"Animation {anim_name!r} not found in {anim_json}")

            unique_layers: List[Tuple[Tuple[int, int, int], ...]] = []
            layer_to_idx: Dict[Tuple[Tuple[int, int, int], ...], int] = {}
            steps: List[Dict[str, int]] = []
            for sub in entry["subitems"]:
                layers = tuple(
                    (int(fr["sprite_index"]), int(fr["dx"]), int(fr["dy"]))
                    for fr in sub["frames"]
                )
                f_idx = layer_to_idx.get(layers)
                if f_idx is None:
                    f_idx = len(unique_layers)
                    layer_to_idx[layers] = f_idx
                    unique_layers.append(layers)
                steps.append({"f": f_idx, "ms": int(sub["duration_ms"])})

            # Compute tight union bounds across all unique frames using authentic (dx, dy).
            min_x = min_y = 10**9
            max_x = max_y = -10**9
            for layers in unique_layers:
                for sid, dx, dy in layers:
                    key = (sid, teams[0])
                    spr = sprite_cache.get(key)
                    if spr is None:
                        spr = artlib.team_ant(sid, teams[0])
                        sprite_cache[key] = spr
                    bb = spr.getbbox()
                    if bb is not None:
                        min_x = min(min_x, dx + bb[0])
                        min_y = min(min_y, dy + bb[1])
                        max_x = max(max_x, dx + bb[2])
                        max_y = max(max_y, dy + bb[3])

            if max_x <= min_x or max_y <= min_y:
                raise ValueError(f"Empty opaque bounds for animation {anim_name!r}")

            fw = int(max_x - min_x)
            fh = int(max_y - min_y)
            frame_count = len(unique_layers)

            # Frame 0 opaque metrics provide exact 1x rest height & ground foot row for 3D alignment.
            f0_ref = _composite_frame(unique_layers[0], teams[0], min_x, min_y, fw, fh, sprite_cache, artlib)
            bb0 = f0_ref.getbbox() or (0, 0, fw, fh)
            vis_w = int(bb0[2] - bb0[0])
            vis_h = int(bb0[3] - bb0[1])
            ref_y = ref_world_foot_y.get((cls_name, angle)) if ref_world_foot_y else None
            if ref_y is not None:
                # Lock attack foot_y to the exact CHD world ground row used by idle so Idle <-> Attack never jumps.
                foot_y = max(1, min(fh, int(ref_y - min_y)))
            else:
                foot_y = int(bb0[3])
            world_foot_y_out[(cls_name, angle)] = int(min_y + foot_y)

            sheets: Dict[str, str] = {}
            for team in teams:
                strip = Image.new("RGBA", (fw * frame_count, fh), (0, 0, 0, 0))
                for f_idx, layers in enumerate(unique_layers):
                    frame_img = _composite_frame(layers, team, min_x, min_y, fw, fh, sprite_cache, artlib)
                    strip.paste(frame_img, (f_idx * fw, 0))
                sheets[team] = _png_data_uri(strip)

            durations_ms = [int(s["ms"]) for s in steps]
            total_ms = int(entry.get("total_duration_ms") or sum(durations_ms))
            origin_x = int(-min_x)
            origin_y = int(-min_y)
            spec: Dict[str, Any] = {
                "anim_name": anim_name,
                "total_ms": total_ms,
                "durations_ms": durations_ms,
                "fw": fw,
                "fh": fh,
                "foot_y": foot_y,
                "anchor_x": origin_x,
                "anchor_y": foot_y,
                "vis_w": vis_w,
                "vis_h": vis_h,
                "origin_x": origin_x,
                "origin_y": origin_y,
                "frame_count": frame_count,
                "steps": steps,
                "sheets": sheets,
            }
            if include_attack_aliases:
                spec["frame_w"] = fw
                spec["frame_h"] = fh
                spec["total_duration_ms"] = total_ms
            cls_map[angle] = spec
        sprites_out[cls_name] = cls_map

    return sprites_out, world_foot_y_out


def build_orig_sprites_bundle(
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    classes: Sequence[Tuple[str, str]] = CLASS_PREFIXES,
    attack_classes: Optional[Sequence[Tuple[str, str]]] = None,
    hit_classes: Optional[Sequence[Tuple[str, str]]] = None,
    angles: Sequence[str] = CANONICAL_ANGLES,
    teams: Sequence[str] = ORIGINAL_TEAMS,
) -> Dict[str, Any]:
    """Build and return the deterministic 1998 sprite-sheet bundle dict (idle + attack + hit)."""
    artlib = _load_artlib()
    anims = _load_animations_by_name(anim_json)
    sprite_cache: Dict[Tuple[int, str], Any] = {}

    if attack_classes is None:
        attack_lookup = dict(ATTACK_PREFIXES)
        attack_pairs: Sequence[Tuple[str, str]] = tuple(
            (cls_name, attack_lookup.get(cls_name, prefix[:-2] + "at")) for cls_name, prefix in classes
        )
    else:
        attack_pairs = attack_classes

    if hit_classes is None:
        hit_lookup = dict(HIT_PREFIXES)
        hit_pairs: Sequence[Tuple[str, str]] = tuple(
            (cls_name, hit_lookup.get(cls_name, prefix[:-2] + "gh")) for cls_name, prefix in classes
        )
    else:
        hit_pairs = hit_classes

    idle_sprites, idle_world_foot_y = _build_action_sprites(
        anims=anims,
        anim_json=anim_json,
        class_prefixes=classes,
        angles=angles,
        teams=teams,
        sprite_cache=sprite_cache,
        artlib=artlib,
    )
    attack_sprites, _ = _build_action_sprites(
        anims=anims,
        anim_json=anim_json,
        class_prefixes=attack_pairs,
        angles=angles,
        teams=teams,
        sprite_cache=sprite_cache,
        artlib=artlib,
        ref_world_foot_y=idle_world_foot_y,
        include_attack_aliases=True,
    )
    hit_sprites, _ = _build_action_sprites(
        anims=anims,
        anim_json=anim_json,
        class_prefixes=hit_pairs,
        angles=angles,
        teams=teams,
        sprite_cache=sprite_cache,
        artlib=artlib,
        ref_world_foot_y=idle_world_foot_y,
        include_attack_aliases=True,
    )

    return {
        "schema_version": 1,
        "actions": list(ACTIONS),
        "classes": [cls for cls, _ in classes],
        "angles": list(angles),
        "teams": list(teams),
        "sprites": idle_sprites,
        "attack_sprites": attack_sprites,
        "hit_sprites": hit_sprites,
    }


def write_orig_sprites(
    out_path: os.PathLike[str] | str = DEFAULT_OUT_PATH,
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
) -> Dict[str, Any]:
    """Generate and deterministically write `orig_sprites.json` to `out_path`."""
    bundle = build_orig_sprites_bundle(anim_json=anim_json)
    dest = Path(out_path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(bundle, sort_keys=True, separators=(",", ":")) + "\n"
    dest.write_text(text, encoding="utf-8")
    return bundle


def main(argv: Optional[Sequence[str]] = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Generate deterministic 1998 Ants sprite bundle (orig_sprites.json).")
    parser.add_argument("--out", default=str(DEFAULT_OUT_PATH), help="Output JSON path (default: models/orig_sprites.json)")
    args = parser.parse_args(list(argv) if argv is not None else None)
    write_orig_sprites(args.out)
    size_bytes = os.path.getsize(args.out)
    print(f"  [ok]   orig_sprites -> {os.path.basename(args.out)} ({size_bytes:,} B)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

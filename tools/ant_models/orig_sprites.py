#!/usr/bin/env python3
"""Deterministic 1998 original Ants sprite-sheet bundle generator (`orig_sprites.json`).

Reads `docs/chd_table4_animations.json` (or `asset_catalog/catalog_data.js` when `docs/`
is absent, such as inside Docker builds) and decodes authentic palette-shifted team
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
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

THIS_DIR = Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
DEFAULT_ANIM_JSON = REPO_ROOT / "docs" / "chd_table4_animations.json"
DEFAULT_CATALOG_JS = REPO_ROOT / "asset_catalog" / "catalog_data.js"
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
CLASS_LETTERS: Dict[str, str] = {cls: prefix[1] for cls, prefix in CLASS_PREFIXES}
CANONICAL_ANGLES: Tuple[str, ...] = ("301", "201", "901", "801", "701")
ORIGINAL_TEAMS: Tuple[str, ...] = ("green", "red", "blue", "black")
ACTIONS: Tuple[str, ...] = ("idle", "attack", "hit")

# Extended 1998 animations emitted into per-ant sibling `orig_sprites_extra_<cls>.json` bundles.
# Each entry maps (action_id, prefix_or_suffix, ((output_angle, chd_angle_suffix), ...)).
_FIVE_DIRS: Tuple[Tuple[str, str], ...] = tuple((a, a) for a in CANONICAL_ANGLES)
_THREE_DIRS: Tuple[Tuple[str, str], ...] = (("301", "301"), ("901", "901"), ("701", "701"))
_ONE_DIR_301: Tuple[Tuple[str, str], ...] = (("301", "301"),)

EXTRA_SHARED_SPECS: Tuple[Tuple[str, str, Tuple[Tuple[str, str], ...]], ...] = (
    ("walk", "wg", _FIVE_DIRS),
    ("harvest", "gf", _FIVE_DIRS),
    ("cantgo", "cg", _ONE_DIR_301),
    ("blownback", "gb", _FIVE_DIRS),
    ("blownup", "bu", _ONE_DIR_301),
    ("stunned", "sd", _ONE_DIR_301),
    ("drown", "dr", _ONE_DIR_301),
    ("enter", "en", _ONE_DIR_301),
    ("hatch", "hatch", (("301", ""),)),
)

EXTRA_CLASS_SPECS: Dict[str, Tuple[Tuple[str, str, Tuple[Tuple[str, str], ...]], ...]] = {
    "worker": (),
    "combat": (),
    "thief": (
        ("steal", "atcr", (("301", "501"),)),
    ),
    "bomber": (
        ("plantbomb", "absb", _THREE_DIRS),
        ("defusebomb", "abdb", _THREE_DIRS),
    ),
    "swimmer": (
        ("swim", "assw", _FIVE_DIRS),
        ("dive", "asdi", _FIVE_DIRS),
        ("surface", "asgo", _FIVE_DIRS),
        ("shovel", "asbbl", _THREE_DIRS),
        ("shovelwater", "asbbw", _THREE_DIRS),
    ),
    "fire": (
        ("startfire", "afsf", _THREE_DIRS),
        ("extinguish", "afxf", _THREE_DIRS),
    ),
}


def can_generate_orig_sprites(
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    chd_path: Path = DEFAULT_CHD_PATH,
    catalog_js: Path = DEFAULT_CATALOG_JS,
) -> bool:
    """Return True if Pillow and the original animation/CHD sources are available."""
    try:
        import PIL.Image  # noqa: F401
    except ImportError:
        return False
    has_anims = anim_json.is_file() or (anim_json == DEFAULT_ANIM_JSON and catalog_js.is_file())
    return has_anims and chd_path.is_file()


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


def _load_animations_from_catalog_js(catalog_js: Path) -> Dict[str, Dict[str, Any]]:
    """Parse the public animation table from `asset_catalog/catalog_data.js` (no docs/ needed)."""
    text = catalog_js.read_text(encoding="utf-8")
    start = text.find("animations: [")
    if start < 0:
        raise ValueError(f"No animations table found in {catalog_js}")
    found = list(re.finditer(r'\n\s*\{ id: (\d+), name: "([^"]*)", subitems: \[', text[start:]))
    sub_re = re.compile(
        r'\{ val3: (\d+), sound_id: -?\d+, sound_name: "[^"]*", frames: \[(.*?)\n\s*\] \}',
        re.S,
    )
    frame_re = re.compile(r"\{ dx: (-?\d+), dy: (-?\d+), sprite_id: (\d+),")
    by_name: Dict[str, Dict[str, Any]] = {}
    for k, match in enumerate(found):
        name = match.group(2)
        end_pos = found[k + 1].start() if k + 1 < len(found) else len(text) - start
        block = text[start + match.end() : start + end_pos]
        subitems: List[Dict[str, Any]] = []
        total_ms = 0
        for sm in sub_re.finditer(block):
            dur_ms = int(sm.group(1))
            frames = [
                {"dx": int(fm.group(1)), "dy": int(fm.group(2)), "sprite_index": int(fm.group(3))}
                for fm in frame_re.finditer(sm.group(2))
            ]
            subitems.append({"duration_ms": dur_ms, "frames": frames})
            total_ms += dur_ms
        by_name[name.lower()] = {
            "id": int(match.group(1)),
            "name": name,
            "total_duration_ms": total_ms,
            "subitems": subitems,
        }
    return by_name


def _load_animations_by_name(
    anim_json: Path,
    *,
    catalog_js: Path = DEFAULT_CATALOG_JS,
) -> Dict[str, Dict[str, Any]]:
    if not anim_json.is_file() and anim_json == DEFAULT_ANIM_JSON and catalog_js.is_file():
        return _load_animations_from_catalog_js(catalog_js)
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


def _encode_anim_spec(
    *,
    anim_name: str,
    entry: Dict[str, Any],
    teams: Sequence[str],
    sprite_cache: Dict[Tuple[int, str], Any],
    artlib: Any,
    ref_y: Optional[int] = None,
    include_attack_aliases: bool = False,
) -> Tuple[Dict[str, Any], int]:
    from PIL import Image

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
    if ref_y is not None:
        foot_y = max(1, min(fh, int(ref_y - min_y)))
    else:
        foot_y = int(bb0[3])
    world_foot_y = int(min_y + foot_y)

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
    return spec, world_foot_y


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
    sprites_out: Dict[str, Dict[str, Any]] = {}
    world_foot_y_out: Dict[Tuple[str, str], int] = {}

    for cls_name, prefix in class_prefixes:
        cls_map: Dict[str, Any] = {}
        for angle in angles:
            anim_name = f"{prefix}{angle}"
            entry = anims.get(anim_name)
            if entry is None:
                raise KeyError(f"Animation {anim_name!r} not found in {anim_json}")
            ref_y = ref_world_foot_y.get((cls_name, angle)) if ref_world_foot_y else None
            spec, world_fy = _encode_anim_spec(
                anim_name=anim_name,
                entry=entry,
                teams=teams,
                sprite_cache=sprite_cache,
                artlib=artlib,
                ref_y=ref_y,
                include_attack_aliases=include_attack_aliases,
            )
            world_foot_y_out[(cls_name, angle)] = world_fy
            cls_map[angle] = spec
        sprites_out[cls_name] = cls_map

    return sprites_out, world_foot_y_out


def build_extra_orig_sprites_bundle(
    cls_name: str,
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    teams: Sequence[str] = ORIGINAL_TEAMS,
    anims: Optional[Dict[str, Dict[str, Any]]] = None,
    sprite_cache: Optional[Dict[Tuple[int, str], Any]] = None,
    artlib: Any = None,
    ref_world_foot_y: Optional[Dict[Tuple[str, str], int]] = None,
) -> Dict[str, Any]:
    """Build deterministic extended 1998 sprite bundle for one ant class."""
    if cls_name not in CLASS_LETTERS:
        raise ValueError(f"Unknown ant class: {cls_name!r}")
    if artlib is None:
        artlib = _load_artlib()
    if anims is None:
        anims = _load_animations_by_name(anim_json)
    if sprite_cache is None:
        sprite_cache = {}
    letter = CLASS_LETTERS[cls_name]
    if ref_world_foot_y is None:
        _, ref_world_foot_y = _build_action_sprites(
            anims=anims,
            anim_json=anim_json,
            class_prefixes=((cls_name, f"a{letter}st"),),
            angles=CANONICAL_ANGLES,
            teams=teams[:1],
            sprite_cache=sprite_cache,
            artlib=artlib,
        )

    actions_out: Dict[str, Any] = {}
    specs: List[Tuple[str, str, Tuple[Tuple[str, str], ...]]] = [
        (aid, f"a{letter}{suf}", ang_pairs) for aid, suf, ang_pairs in EXTRA_SHARED_SPECS
    ] + list(EXTRA_CLASS_SPECS.get(cls_name, ()))

    for action_id, prefix, ang_pairs in specs:
        angle_map: Dict[str, Any] = {}
        avail: List[str] = []
        for out_angle, chd_suffix in ang_pairs:
            anim_name = f"{prefix}{chd_suffix}"
            entry = anims.get(anim_name)
            if entry is None:
                # Authentic absence (e.g. Swimmer has no `asdr301` drown animation).
                continue
            ref_y = ref_world_foot_y.get((cls_name, out_angle), ref_world_foot_y.get((cls_name, "301")))
            spec, _ = _encode_anim_spec(
                anim_name=anim_name,
                entry=entry,
                teams=teams,
                sprite_cache=sprite_cache,
                artlib=artlib,
                ref_y=ref_y,
                include_attack_aliases=True,
            )
            angle_map[out_angle] = spec
            avail.append(out_angle)
        if avail:
            actions_out[action_id] = {
                "available_angles": avail,
                "angles": angle_map,
            }

    return {
        "schema_version": 1,
        "class": cls_name,
        "teams": list(teams),
        "actions": actions_out,
    }


def _build_orig_sprites_with_context(
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    classes: Sequence[Tuple[str, str]] = CLASS_PREFIXES,
    attack_classes: Optional[Sequence[Tuple[str, str]]] = None,
    hit_classes: Optional[Sequence[Tuple[str, str]]] = None,
    angles: Sequence[str] = CANONICAL_ANGLES,
    teams: Sequence[str] = ORIGINAL_TEAMS,
) -> Tuple[Dict[str, Any], Dict[str, Dict[str, Any]], Dict[Tuple[int, str], Any], Any, Dict[Tuple[str, str], int]]:
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

    bundle = {
        "schema_version": 1,
        "actions": list(ACTIONS),
        "classes": [cls for cls, _ in classes],
        "angles": list(angles),
        "teams": list(teams),
        "sprites": idle_sprites,
        "attack_sprites": attack_sprites,
        "hit_sprites": hit_sprites,
    }
    return bundle, anims, sprite_cache, artlib, idle_world_foot_y


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
    bundle, _, _, _, _ = _build_orig_sprites_with_context(
        anim_json=anim_json,
        classes=classes,
        attack_classes=attack_classes,
        hit_classes=hit_classes,
        angles=angles,
        teams=teams,
    )
    return bundle


def write_extra_orig_sprites(
    out_dir: os.PathLike[str] | str = DEFAULT_OUT_PATH.parent,
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    classes: Sequence[str] = ANT_CLASSES,
    anims: Optional[Dict[str, Dict[str, Any]]] = None,
    sprite_cache: Optional[Dict[Tuple[int, str], Any]] = None,
    artlib: Any = None,
    ref_world_foot_y: Optional[Dict[Tuple[str, str], int]] = None,
) -> Dict[str, Path]:
    """Write deterministic per-ant `orig_sprites_extra_<cls>.json` bundles into `out_dir`."""
    dest_dir = Path(out_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    written: Dict[str, Path] = {}
    for cls_name in classes:
        extra = build_extra_orig_sprites_bundle(
            cls_name,
            anim_json=anim_json,
            anims=anims,
            sprite_cache=sprite_cache,
            artlib=artlib,
            ref_world_foot_y=ref_world_foot_y,
        )
        p = dest_dir / f"orig_sprites_extra_{cls_name}.json"
        p.write_text(json.dumps(extra, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
        written[cls_name] = p
    return written


def write_orig_sprites(
    out_path: os.PathLike[str] | str = DEFAULT_OUT_PATH,
    *,
    anim_json: Path = DEFAULT_ANIM_JSON,
    write_extras: Optional[bool] = None,
) -> Dict[str, Any]:
    """Generate and deterministically write `orig_sprites.json` (plus sibling per-ant extra bundles)."""
    bundle, anims, sprite_cache, artlib, idle_world_foot_y = _build_orig_sprites_with_context(anim_json=anim_json)
    dest = Path(out_path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(bundle, sort_keys=True, separators=(",", ":")) + "\n"
    dest.write_text(text, encoding="utf-8")
    emit_extras = (dest.name == "orig_sprites.json") if write_extras is None else bool(write_extras)
    if emit_extras:
        write_extra_orig_sprites(
            dest.parent,
            anim_json=anim_json,
            anims=anims,
            sprite_cache=sprite_cache,
            artlib=artlib,
            ref_world_foot_y=idle_world_foot_y,
        )
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

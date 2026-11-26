#!/usr/bin/env python3
"""Export original 1998 Ants ground tile atlas and classic .LVL map layouts for the 3D Map Demo.

Reads ONLY public repository sources (Original-Ants/Maps/*.LVL, asset_catalog/sprites/,
docs/chd_table4_animations.json, tools/map_atlas.py, tools/ant_models/models/items/manifest.json).
Never reads any original executable or decompilation (AGENTS.md rule 4).

Outputs under tools/ant_models/models/demo/:
  ground.png   239 original 32x32 ground tiles (Table-4 ids 431..669), 16 columns x 15 rows
  maps.json    Parsed classic .LVL maps (TREASURE, TINY, SMALL, ISLANDS) with exact tile grids,
               layer-2 anchored 3D items, block-1 plants, anthills, and team starting spawns
"""
import argparse
import json
import math
import os
import struct
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import map_atlas  # noqa: E402

GROUND_FIRST = map_atlas.GROUND_FIRST  # 431
GROUND_LAST = map_atlas.GROUND_LAST    # 669
TILE_PX = 32.0
START_MARKERS = {"BSTART": "black", "GSTART": "green", "USTART": "blue", "RSTART": "red"}
HILL_TEAMS = {"BLACKHILL": "black", "GREENHILL": "green", "BLUEHILL": "blue", "REDHILL": "red"}

MAPS_TO_EXPORT = [
    ("treasure", "Treasure Map", "Original-Ants/Maps/TREASURE.LVL"),
    ("tiny",     "Tiny Arena",   "Original-Ants/Maps/TINY.LVL"),
    ("small",    "Small Map",    "Original-Ants/Maps/SMALL.LVL"),
    ("islands",  "Islands",      "Original-Ants/Maps/ISLANDS.LVL"),
]


def read_png_dims(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


def build_sprite_metrics(t4_entries, items_manifest, sprites_dir):
    """Compute exact 1998 pixel box, anchor center offset (in tiles), and 1:1 3D scale per item."""
    t4_by_name = {e["name"].lower(): e for e in t4_entries}
    metrics = {}
    png_dim_cache = {}

    def get_dims(sprite_idx):
        if sprite_idx not in png_dim_cache:
            p = os.path.join(sprites_dir, f"sprite_{sprite_idx:04d}.png")
            png_dim_cache[sprite_idx] = read_png_dims(p)
        return png_dim_cache[sprite_idx]

    for item in items_manifest["items"]:
        item_id = item["id"]
        low = item_id.lower()
        t4 = t4_by_name.get(low)
        if not t4 or not t4.get("subitems"):
            continue
        sub = t4["subitems"][0]
        frames = sub.get("frames") or []
        if not frames:
            continue
        min_x = min_y = 1e9
        max_x = max_y = -1e9
        for fr in frames:
            w, h = get_dims(fr["sprite_index"])
            min_x = min(min_x, fr["dx"])
            min_y = min(min_y, fr["dy"])
            max_x = max(max_x, fr["dx"] + w)
            max_y = max(max_y, fr["dy"] + h)
        px_w = max(4.0, max_x - min_x)
        px_h = max(4.0, max_y - min_y)
        w_tiles = px_w / TILE_PX
        h_tiles = px_h / TILE_PX

        b = sub.get("bounds") or {"left": 5, "top": 5, "right": 20, "bottom": 20}
        bl, bt, br, bb = float(b["left"]), float(b["top"]), float(b["right"]), float(b["bottom"])
        bw = br - bl
        bh = bb - bt
        # Tall canopy plants (clovers/flowers) anchor their stem at the cell center (bounds 5..20)
        # while their sprite extends upward into negative dy. Ground objects anchor via bounds/sprite center.
        is_canopy = low.startswith(("clover", "flower", "dflower"))
        if is_canopy:
            ox = 0.5
            oz = 0.5
        elif bw >= 24.0 or bh >= 24.0:
            ox = 0.5 * (bl + br) / TILE_PX
            oz = 0.5 * (bt + bb) / TILE_PX
        else:
            ox = 0.5 * (min_x + max_x) / TILE_PX
            oz = 0.5 * (min_y + max_y) / TILE_PX

        sx, sy, sz = [max(0.08, float(v)) for v in item["size"]]
        # Match 1998 projected width (sx) & 45-degree projected height (0.707*(sy+sz)) onto 32px tiles
        scale_x = w_tiles / sx
        scale_h = h_tiles / (0.7071 * (sy + sz))
        if low.startswith(("grass", "ivy", "leaf")) or low in HILL_TEAMS or bw >= 60.0:
            # Multi-tile walls/hills/large objects match exact horizontal tile span cleanly
            scale = 0.72 * scale_x + 0.28 * scale_h
        else:
            scale = math.sqrt(scale_x * scale_h)

        metrics[low] = {
            "id": item_id,
            "ox": round(ox, 3),
            "oz": round(oz, 3),
            "scale": round(scale, 3),
            "w_tiles": round(w_tiles, 2),
            "h_tiles": round(h_tiles, 2),
            "is_plant": is_canopy or low.startswith(("grass", "ivy", "leaf", "plant")),
        }
    return metrics


def parse_lvl(path, t4_entries, metrics):
    """Parse a Classic v8 .LVL file using exact renderer.cpp anchor rules."""
    with open(path, "rb") as f:
        data = f.read()
    pos = 0

    def u16():
        nonlocal pos
        v = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        return v

    def u32():
        nonlocal pos
        v = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        return v

    ver = u32()
    if ver != 8:
        raise ValueError(f"{path}: expected Classic LVL version 8, got {ver}")
    _mode = u32()
    _mins = u16()
    desc = data[pos:pos + 30].split(b"\x00")[0].decode("latin1").strip()
    pos += 30
    tcount = u16()
    dict_names = []
    for _ in range(tcount + 1):
        dict_names.append(data[pos:pos + 11].split(b"\x00")[0].decode("latin1"))
        pos += 11
    rows = u32() & 0xFFFF
    cols = u32() & 0xFFFF

    t4_id_by_name = {e["name"].lower(): e["entry_index"] for e in t4_entries}

    # Resolve map dictionary index -> Table-4 id (matches Renderer::set_level remap)
    def resolve_t4_id(tile_idx):
        if tile_idx >= len(dict_names):
            return -1
        nm = dict_names[tile_idx]
        if tile_idx < len(t4_entries) and t4_entries[tile_idx]["name"].lower() == nm.lower():
            return tile_idx
        return t4_id_by_name.get(nm.lower(), -1)

    # Layer 1 ground terrain tiles -> 0..238 index into ground.png (255 = empty)
    tiles = []
    water_cells = []
    for r in range(rows):
        for c in range(cols):
            tid, _flg, _prop = struct.unpack_from("<HHH", data, pos)
            pos += 6
            t4_id = resolve_t4_id(tid)
            if GROUND_FIRST <= t4_id <= GROUND_LAST:
                g_idx = t4_id - GROUND_FIRST
                tiles.append(g_idx)
                if t4_id == 476:  # w01a water tile
                    water_cells.append([c, r])
            else:
                tiles.append(54)  # fallback g01a (485 - 431 = 54)

    # Layer 2 anchored objects + power-ups + anthills (exact renderer.cpp line 545 anchor flag)
    objects = []
    hills = {}
    for r in range(rows):
        for c in range(cols):
            tid, flg, _prop = struct.unpack_from("<HHH", data, pos)
            pos += 6
            if tid in (0, 0xFFFF, 0x7FFE):
                continue
            if tid >= len(dict_names):
                continue
            nm = dict_names[tid]
            low = nm.lower()
            is_pu = low.startswith("pu_")
            if not is_pu and (flg & 1) == 0:
                continue
            m = metrics.get(low)
            if not m:
                continue
            x = round(c + m["ox"], 2)
            z = round(r + m["oz"], 2)
            objects.append({
                "id": m["id"],
                "col": c,
                "row": r,
                "x": x,
                "z": z,
                "s": m["scale"],
            })
            if nm in HILL_TEAMS:
                hills[HILL_TEAMS[nm]] = [round(x, 2), round(z, 2), c, r]

    # Block 1: team start markers (BSTART/GSTART/USTART/RSTART) and top-layer plants
    b1_count = u16()
    starts = []
    for _ in range(b1_count):
        raw_tid, r, c = struct.unpack_from("<HHH", data, pos)
        pos += 6
        if raw_tid < len(dict_names):
            nm = dict_names[raw_tid]
        elif raw_tid < len(t4_entries):
            nm = t4_entries[raw_tid]["name"]
        else:
            continue
        if nm in START_MARKERS:
            starts.append({"team": START_MARKERS[nm], "col": c, "row": r})
        else:
            # Block 1 plant uses Table-4 id directly (renderer.cpp line 576)
            p_nm = t4_entries[raw_tid]["name"] if raw_tid < len(t4_entries) else nm
            m = metrics.get(p_nm.lower())
            if m:
                objects.append({
                    "id": m["id"],
                    "col": c,
                    "row": r,
                    "x": round(c + m["ox"], 2),
                    "z": round(r + m["oz"], 2),
                    "s": m["scale"],
                    "canopy": True,
                })

    # Find distinct water pond centers (BFS components of w01a cells) for Swimmer ants
    water_set = {(c, r) for c, r in water_cells}
    seen_w = set()
    ponds = []
    for cell in water_cells:
        t = tuple(cell)
        if t in seen_w:
            continue
        stack = [t]
        comp = []
        seen_w.add(t)
        while stack:
            cx, cy = stack.pop()
            comp.append((cx, cy))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nb = (cx + dx, cy + dy)
                if nb in water_set and nb not in seen_w:
                    seen_w.add(nb)
                    stack.append(nb)
        if len(comp) >= 6:
            avg_c = sum(p[0] for p in comp) / len(comp) + 0.5
            avg_r = sum(p[1] for p in comp) / len(comp) + 0.5
            ponds.append([round(avg_c, 2), round(avg_r, 2), len(comp)])

    return {
        "desc": desc,
        "cols": cols,
        "rows": rows,
        "tiles": tiles,
        "objects": objects,
        "hills": hills,
        "starts": starts,
        "ponds": ponds,
    }


def main():
    ap = argparse.ArgumentParser(description="Export original 1998 tile atlas and classic .LVL maps for demo.html")
    ap.add_argument("--out", default=os.path.join(REPO, "tools", "ant_models", "models", "demo"))
    args = ap.parse_args()

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    sprites_dir = os.path.join(REPO, "asset_catalog", "sprites")
    t4_path = os.path.join(REPO, "docs", "chd_table4_animations.json")
    items_manifest_path = os.path.join(REPO, "tools", "ant_models", "models", "items", "manifest.json")

    with open(t4_path, encoding="utf-8") as f:
        t4_entries = json.load(f)
    with open(items_manifest_path, encoding="utf-8") as f:
        items_manifest = json.load(f)

    # 1. Build ground.png (239 real 32x32 ground tiles, 16x15 grid) via map_atlas helpers
    # Reuse map_atlas to generate ground.png directly into a temp dir, keeping ONLY ground.png!
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        old_argv = sys.argv
        try:
            sys.argv = ["map_atlas.py", tmp]
            map_atlas.main()
        finally:
            sys.argv = old_argv
        src_ground = os.path.join(tmp, "ground.png")
        dst_ground = os.path.join(out_dir, "ground.png")
        with open(src_ground, "rb") as sf, open(dst_ground, "wb") as df:
            df.write(sf.read())

    # 2. Export parsed classic .LVL maps
    metrics = build_sprite_metrics(t4_entries, items_manifest, sprites_dir)
    maps_out = []
    for map_id, label, rel_path in MAPS_TO_EXPORT:
        parsed = parse_lvl(os.path.join(REPO, rel_path), t4_entries, metrics)
        parsed["id"] = map_id
        parsed["label"] = label
        parsed["file"] = os.path.basename(rel_path)
        maps_out.append(parsed)
        print(
            f"  {map_id:10s} ({parsed['cols']}x{parsed['rows']}): "
            f"{len(parsed['objects']):3d} 3D objects, {len(parsed['starts']):2d} starts, "
            f"{len(parsed['hills'])} hills, {len(parsed['ponds'])} ponds"
        )

    payload = {
        "schema_version": 1,
        "tile_px": 32,
        "atlas_cols": map_atlas.GROUND_COLUMNS,
        "atlas_rows": (GROUND_LAST - GROUND_FIRST + 1 + map_atlas.GROUND_COLUMNS - 1) // map_atlas.GROUND_COLUMNS,
        "ground_first_id": GROUND_FIRST,
        "water_tile_index": 476 - GROUND_FIRST,
        "maps": maps_out,
    }
    maps_path = os.path.join(out_dir, "maps.json")
    with open(maps_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(payload, f, separators=(",", ":"))
    print(f"Wrote {dst_ground} ({os.path.getsize(dst_ground):,} B) and {maps_path} ({os.path.getsize(maps_path):,} B)")


if __name__ == "__main__":
    main()

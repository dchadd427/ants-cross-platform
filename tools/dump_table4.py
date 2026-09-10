#!/usr/bin/env python3
"""
tools/dump_table4.py
Extracts and analyzes all 1,344 Table 4 animation & physics bytecode sequences
from Original-Ants/ants.chd, correlating sound triggers with Table 2 audio.
Generates:
  - docs/chd_table4_animations.json
  - docs/TABLE4_ANIMATION_REFERENCE.md
"""

import os
import sys
import struct
import json

CHD_PATH = "Original-Ants/ants.chd"
JSON_OUT = "docs/chd_table4_animations.json"
MD_OUT = "docs/TABLE4_ANIMATION_REFERENCE.md"

def load_sounds(data, t2_off):
    count = struct.unpack_from("<I", data, t2_off)[0]
    offsets = struct.unpack_from(f"<{count}I", data, t2_off + 4)
    sounds = {}
    canonical_names = {
        6: "mslogo.wav",
        17: "cantgo.wav",
        40: "powerupd.wav",
        42: "losers.wav",
        43: "exithill.wav",
        45: "chatsnda.wav",
        46: "chatsnd.wav",
        48: "anthill.wav",
        54: "30sec.wav",
        55: "1min.wav",
        59: "combat1.wav",
        60: "combat2.wav",
        75: "attack_alt.wav",
        77: "harvest_alt.wav",
        80: "wateraction.wav",
    }
    for i in range(count):
        off = offsets[i]
        fmt_len = struct.unpack_from("<I", data, off)[0]
        pos = off + 4 + fmt_len
        pcm_len = struct.unpack_from("<I", data, pos)[0]
        pos += 4 + pcm_len
        fn_len = struct.unpack_from("<I", data, pos)[0]
        name = data[pos+4:pos+4+fn_len].decode("latin-1").rstrip("\x00")
        if not name:
            name = canonical_names.get(i, f"sound_{i}.wav")
        sounds[i] = name
    return sounds

def parse_table4(data, t4_off, sound_map):
    count = struct.unpack_from("<I", data, t4_off)[0]
    offsets = struct.unpack_from(f"<{count}I", data, t4_off + 4)
    entries = []

    for i in range(count):
        off = offsets[i]
        name_len = struct.unpack_from("<I", data, off)[0]
        name = data[off+4:off+4+name_len].decode("latin-1").rstrip("\x00")
        pos = off + 4 + name_len
        flag1, flag2, flag3, sub_cnt = struct.unpack_from("<4I", data, pos)
        pos += 16

        subitems = []
        total_duration_ms = 0
        sound_triggers = []

        for s in range(sub_cnt):
            val1, val2, val3, bl, bt, br, bb, flags, default_sp, frame_cnt = struct.unpack_from("<3I4i3I", data, pos)
            pos += 40
            sval1 = struct.unpack("<i", struct.pack("<I", val1))[0]
            sval2 = struct.unpack("<i", struct.pack("<I", val2))[0]
            
            frames = []
            for f_idx in range(frame_cnt):
                dx, dy, sp_idx = struct.unpack_from("<2iI", data, pos)
                pos += 12
                frames.append({
                    "frame_index": f_idx,
                    "dx": dx,
                    "dy": dy,
                    "sprite_index": sp_idx
                })

            subitem_dict = {
                "subitem_index": s,
                "dx_per_tick": sval1,
                "dy_per_tick": sval2,
                "duration_ms": val3,
                "bounds": {"left": bl, "top": bt, "right": br, "bottom": bb},
                "flags": flags,
                "sound_id": default_sp if default_sp < 91 else None,
                "sound_name": sound_map.get(default_sp) if default_sp < 91 else None,
                "frame_count": frame_cnt,
                "frames": frames
            }
            subitems.append(subitem_dict)
            total_duration_ms += val3
            if default_sp < 91:
                sound_triggers.append({
                    "subitem": s,
                    "sound_id": default_sp,
                    "sound_name": sound_map.get(default_sp, f"sound_{default_sp}.wav")
                })

        entries.append({
            "entry_index": i,
            "name": name,
            "flags": [flag1, flag2, flag3],
            "subitem_count": sub_cnt,
            "total_duration_ms": total_duration_ms,
            "sound_triggers": sound_triggers,
            "subitems": subitems
        })

    return entries

def generate_markdown_reference(entries, sound_map):
    md = []
    md.append("# Table 4 Animation & Physics Reference (`ants.chd`)")
    md.append("")
    md.append("This document is generated directly from ground-truth inspection of `ants.chd` Table 4.")
    md.append(f"Total animation sequences: **{len(entries)}**.")
    md.append("")
    md.append("## Action Prefixes & Meanings")
    md.append("- **Prefix Class**: `ag` (Worker), `ab` (Bomber), `af` (Fire), `ac` (Combat), `as` (Swimmer), `at` (Thief)")
    md.append("- **Action Code**:")
    md.append("  - `wg` / `ws`: Walking on Ground / Shoreline")
    md.append("  - `st`: Standing / Idle ready")
    md.append("  - `at`: Melee Attack strike")
    md.append("  - `gh`: Get Hit (combat flinch reaction slide)")
    md.append("  - `gb`: Ground Bounce (tumbling collision bounce flight)")
    md.append("  - `gf`: Grab Food (harvesting food bite)")
    md.append("  - `fa`: Food Action / Eat")
    md.append("  - `sb`: Set Bomb (plant bomb sequence)")
    md.append("  - `db`: Defuse Bomb / Demolish Bridge")
    md.append("  - `sf`: Set Fire (plant firewall)")
    md.append("  - `xf`: Extinguish Fire")
    md.append("  - `bb`: Build Bridge")
    md.append("  - `sw`: Swimming in water")
    md.append("  - `di`: Diving into water")
    md.append("  - `go`: Emerging / exiting water onto land")
    md.append("  - `dr`: Drowning sequence")
    md.append("  - `bu`: Bomb dud / smoke burn stagger")
    md.append("  - `cg`: Can't Go / blocked path reaction")
    md.append("")
    md.append("---")
    md.append("")
    md.append("## Core Action Timings, Subitems & Audio Triggers")
    md.append("")
    md.append("| Entry | Action Name | Subitems | Total Duration | Audio Triggers (Subitem: Sound) | Motion (dx, dy) |")
    md.append("|:-----:|:------------|:--------:|:--------------:|:--------------------------------|:----------------|")

    tracked_prefixes = ["ag", "ab", "af", "ac", "as", "at"]
    tracked_actions = ["at", "gh", "gb", "gf", "sb", "db", "sf", "xf", "bb", "cg", "dr", "bu", "st", "wg"]
    
    seen = set()
    for e in entries:
        name = e["name"]
        key = None
        for p in tracked_prefixes:
            for a in tracked_actions:
                if name.startswith(p + a) and (name.endswith("301") or name.endswith("201")):
                    key = p + a
                    break
            if key: break
        if not key:
            if name in ["battle", "ears", "bombex", "hgen301", "pudrop"]:
                key = name

        if key and key not in seen:
            seen.add(key)
            snd_str = ", ".join([f"Sub {st['subitem']}: `{st['sound_name']}` ({st['sound_id']})" for st in e["sound_triggers"]])
            if not snd_str: snd_str = "None"
            
            dx_dy_list = []
            for s in e["subitems"][:4]:
                if s["dx_per_tick"] != 0 or s["dy_per_tick"] != 0:
                    dx_dy_list.append(f"s{s['subitem_index']}:({s['dx_per_tick']},{s['dy_per_tick']})")
            motion_str = ", ".join(dx_dy_list) if dx_dy_list else "Stationary"

            md.append(f"| {e['entry_index']:4d} | `{name}` | {e['subitem_count']} | {e['total_duration_ms']} ms | {snd_str} | {motion_str} |")

    md.append("")
    md.append("---")
    md.append("")
    md.append("## Full Table 4 Data Access")
    md.append("The complete dataset with all frames, sprite indices, bounding boxes, and per-subitem durations is serialized in `docs/chd_table4_animations.json`.")
    return "\n".join(md)

def main():
    if not os.path.exists(CHD_PATH):
        print(f"Error: {CHD_PATH} not found", file=sys.stderr)
        return 1

    with open(CHD_PATH, "rb") as f:
        data = f.read()

    ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack_from("<7I", data, 0)
    print(f"Loading CHD: ver={ver}, tables: T1={t1_off}, T2={t2_off}, T3={t3_off}, T4={t4_off}")

    sound_map = load_sounds(data, t2_off)
    print(f"Loaded {len(sound_map)} sound entries from Table 2.")

    entries = parse_table4(data, t4_off, sound_map)
    print(f"Parsed {len(entries)} animation sequences from Table 4.")

    os.makedirs("docs", exist_ok=True)
    with open(JSON_OUT, "w") as f:
        json.dump(entries, f, indent=2)
    print(f"Exported JSON to {JSON_OUT} ({os.path.getsize(JSON_OUT):,} bytes).")

    md_content = generate_markdown_reference(entries, sound_map)
    with open(MD_OUT, "w") as f:
        f.write(md_content)
    print(f"Exported Markdown reference to {MD_OUT} ({os.path.getsize(MD_OUT):,} bytes).")

    return 0

if __name__ == "__main__":
    sys.exit(main())

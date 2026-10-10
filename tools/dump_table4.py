#!/usr/bin/env python3
"""
tools/dump_table4.py
Extracts and analyzes all 1,344 Table 4 animation & physics bytecode sequences
from Original-Ants/ants.chd, correlating sound triggers with Table 2 audio.
Generates docs/chd_table4_animations.json.
"""

import os
import sys
import struct
import json

CHD_PATH = "Original-Ants/ants.chd"
JSON_OUT = "docs/chd_table4_animations.json"

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

    return 0

if __name__ == "__main__":
    sys.exit(main())

import struct

chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd"
with open(chd_path, "rb") as f:
    hdr = f.read(28)
ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack("<IIIIIII", hdr)

with open(chd_path, "rb") as f:
    f.seek(t4_off)
    a_count, = struct.unpack("<I", f.read(4))
    a_offsets = struct.unpack(f"<{a_count}I", f.read(4 * a_count))
    anims = []
    for i, off in enumerate(a_offsets):
        f.seek(off)
        nlen, = struct.unpack("<I", f.read(4))
        name = f.read(nlen).split(b"\x00")[0].decode("latin1")
        f1, f2, f3, sub_cnt = struct.unpack("<IIII", f.read(16))
        subitems = []
        for s in range(sub_cnt):
            v1, v2, v3, bl, bt, br, bb, v8, def_sp, f_cnt = struct.unpack("<IIIIIIIIII", f.read(40))
            frames = []
            for fr in range(f_cnt):
                dx, dy, s_idx = struct.unpack("<iiI", f.read(12))
                frames.append((dx, dy, s_idx))
            subitems.append((bl, bt, br, bb, def_sp, frames))
        anims.append((i, name, sub_cnt, subitems))

# Look for worker ant walking animations, e.g. "agwg", "agwt", "agw*"
worker_walks = [a for a in anims if a[1].startswith("agw") or a[1].startswith("ag")]
print(f"Worker animations count: {len(worker_walks)}")
for a in worker_walks[:20]:
    print(f"  Anim {a[0]}: {a[1]}, subitems={a[2]}")

# Check any anims with "lb" in name or carrying
carrier_anims = [a for a in anims if "lb" in a[1].lower() or "car" in a[1].lower()]
print(f"\nCarrier anims: {[a[1] for a in carrier_anims]}")

# Check anims with 2 subitems (where subitem 0 might be ant and subitem 1 might be lunchbox!)
multi_sub = [a for a in anims if a[2] > 1 and a[1].startswith("ag")]
print(f"\nWorker multi-subitem anims: {[(a[0], a[1], a[2]) for a in multi_sub]}")

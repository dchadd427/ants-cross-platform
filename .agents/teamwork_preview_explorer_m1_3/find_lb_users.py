import struct

chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd"
with open(chd_path, "rb") as f:
    hdr = f.read(28)
ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack("<IIIIIII", hdr)

with open(chd_path, "rb") as f:
    # Table 1
    f.seek(t1_off)
    s_count, = struct.unpack("<I", f.read(4))
    s_offsets = struct.unpack(f"<{s_count}I", f.read(4 * s_count))
    sprites = []
    for i, off in enumerate(s_offsets):
        f.seek(off)
        pitch, width, height, fn_len = struct.unpack("<IIII", f.read(16))
        fn = f.read(fn_len).split(b"\x00")[0].decode("latin1")
        sprites.append(fn)

    # Table 4
    f.seek(t4_off)
    a_count, = struct.unpack("<I", f.read(4))
    a_offsets = struct.unpack(f"<{a_count}I", f.read(4 * a_count))
    
    anims_using_lb = {}
    for i, off in enumerate(a_offsets):
        f.seek(off)
        nlen, = struct.unpack("<I", f.read(4))
        name = f.read(nlen).split(b"\x00")[0].decode("latin1")
        f1, f2, f3, sub_cnt = struct.unpack("<IIII", f.read(16))
        for s in range(sub_cnt):
            v1, v2, v3, bl, bt, br, bb, v8, def_sp, f_cnt = struct.unpack("<IIIIIIIIII", f.read(40))
            for fr in range(f_cnt):
                dx, dy, s_idx = struct.unpack("<iiI", f.read(12))
                sname = sprites[s_idx]
                if "lb" in sname.lower() and sname.lower() != "foodlb.bmp":
                    if name not in anims_using_lb:
                        anims_using_lb[name] = []
                    anims_using_lb[name].append((s, fr, dx, dy, s_idx, sname))

print(f"Total animations using lunchbox sprites: {len(anims_using_lb)}")
for aname, usage in sorted(anims_using_lb.items()):
    print(f"  {aname}: {len(usage)} references, sample: {usage[0]}")

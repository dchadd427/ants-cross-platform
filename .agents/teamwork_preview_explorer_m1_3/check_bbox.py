import struct

chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd"
with open(chd_path, "rb") as f:
    hdr = f.read(28)
ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack("<IIIIIII", hdr)

with open(chd_path, "rb") as f:
    f.seek(t4_off)
    a_count, = struct.unpack("<I", f.read(4))
    a_offsets = struct.unpack(f"<{a_count}I", f.read(4 * a_count))
    
    samples = []
    for i in [811, 816, 821, 941, 1095, 1261]:
        off = a_offsets[i]
        f.seek(off)
        nlen, = struct.unpack("<I", f.read(4))
        name = f.read(nlen).split(b"\x00")[0].decode("latin1")
        f1, f2, f3, sub_cnt = struct.unpack("<IIII", f.read(16))
        for s in range(sub_cnt):
            raw = f.read(40)
            v1, v2, v3, bl, bt, br, bb, v8, def_sp, f_cnt = struct.unpack("<iii iiii I I I", raw)
            f.seek(f_cnt * 12, 1)
            samples.append((name, s, bl, bt, br, bb, def_sp))

print("Bounding boxes (signed int32):")
for s in samples[:15]:
    print(f"  {s[0]} sub {s[1]}: L={s[2]}, T={s[3]}, R={s[4]}, B={s[5]}, sound={s[6]}")

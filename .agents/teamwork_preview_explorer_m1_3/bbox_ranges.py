import struct

chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd"
with open(chd_path, "rb") as f:
    hdr = f.read(28)
ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack("<IIIIIII", hdr)

min_l = min_t = min_r = min_b = 999999
max_l = max_t = max_r = max_b = -999999

with open(chd_path, "rb") as f:
    f.seek(t4_off)
    a_count, = struct.unpack("<I", f.read(4))
    a_offsets = struct.unpack(f"<{a_count}I", f.read(4 * a_count))
    for off in a_offsets:
        f.seek(off)
        nlen, = struct.unpack("<I", f.read(4))
        name = f.read(nlen)
        f1, f2, f3, sub_cnt = struct.unpack("<IIII", f.read(16))
        for s in range(sub_cnt):
            raw = f.read(40)
            v1, v2, v3, bl, bt, br, bb, v8, def_sp, f_cnt = struct.unpack("<iii iiii I I I", raw)
            f.seek(f_cnt * 12, 1)
            min_l = min(min_l, bl)
            max_l = max(max_l, bl)
            min_t = min(min_t, bt)
            max_t = max(max_t, bt)
            min_r = min(min_r, br)
            max_r = max(max_r, br)
            min_b = min(min_b, bb)
            max_b = max(max_b, bb)

print(f"Bounding box ranges (signed):")
print(f"  Left: [{min_l}, {max_l}]")
print(f"  Top: [{min_t}, {max_t}]")
print(f"  Right: [{min_r}, {max_r}]")
print(f"  Bottom: [{min_b}, {max_b}]")

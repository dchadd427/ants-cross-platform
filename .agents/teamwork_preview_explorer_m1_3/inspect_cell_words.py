import struct, glob, os

maps_dir = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps"
lvl_files = sorted(glob.glob(os.path.join(maps_dir, "*.LVL")))

for lvl_path in lvl_files:
    name = os.path.basename(lvl_path)
    with open(lvl_path, "rb") as fp:
        data = fp.read()
    
    tcount, = struct.unpack_from("<H", data, 40)
    pos = 42 + (tcount + 1) * 11
    w, h = struct.unpack_from("<II", data, pos)
    pos += 8
    
    # L1
    l1_w1_vals, l1_w2_vals, l1_w3_vals = set(), set(), set()
    for _ in range(w * h):
        w1, w2, w3 = struct.unpack_from("<HHH", data, pos)
        l1_w1_vals.add(w1)
        l1_w2_vals.add(w2)
        l1_w3_vals.add(w3)
        pos += 6
        
    # L2
    l2_w1_vals, l2_w2_vals, l2_w3_vals = set(), set(), set()
    for _ in range(w * h):
        w1, w2, w3 = struct.unpack_from("<HHH", data, pos)
        l2_w1_vals.add(w1)
        l2_w2_vals.add(w2)
        l2_w3_vals.add(w3)
        pos += 6
        
    print(f"{name}:")
    print(f"  L1: w1 in [{min(l1_w1_vals)}, {max(l1_w1_vals)}], w2 in [{min(l1_w2_vals)}, {max(l1_w2_vals)}], w3 in [{min(l1_w3_vals)}, {max(l1_w3_vals)}]")
    has_sentinel = 0x7FFE in l2_w1_vals
    l2_non_sentinel = [v for v in l2_w1_vals if v != 0x7FFE]
    min_non_sent = min(l2_non_sentinel) if l2_non_sentinel else "N/A"
    max_non_sent = max(l2_non_sentinel) if l2_non_sentinel else "N/A"
    print(f"  L2: sentinel 0x7FFE present? {has_sentinel}, non-sentinel w1 in [{min_non_sent}, {max_non_sent}], w2 in [{min(l2_w2_vals)}, {max(l2_w2_vals)}], w3 in [{min(l2_w3_vals)}, {max(l2_w3_vals)}]")

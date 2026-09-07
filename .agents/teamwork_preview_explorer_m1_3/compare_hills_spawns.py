import struct

for m in ["TINY", "SMALL", "MEDIUM"]:
    path = f"/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/{m}.LVL"
    with open(path, "rb") as f:
        data = f.read()
    tcount, = struct.unpack_from("<H", data, 40)
    pos = 42 + (tcount + 1) * 11
    w, h = struct.unpack_from("<II", data, pos)
    pos += 8
    
    # skip L1
    pos += w * h * 6
    
    # read L2
    l2_hills = []
    for idx in range(w * h):
        w1, w2, w3 = struct.unpack_from("<HHH", data, pos)
        pos += 6
        if w1 in [245, 246, 247, 248]: # BLACKHILL, BLUEHILL, REDHILL, GREENHILL
            x = idx % w
            y = idx // w
            l2_hills.append((w1, x, y))
            
    # read Block 1
    b1_cnt, = struct.unpack_from("<H", data, pos)
    pos += 2
    b1_records = []
    for _ in range(b1_cnt):
        tid, c1, c2 = struct.unpack_from("<HHH", data, pos)
        pos += 6
        if tid in [152, 153, 154, 155]: # BSTART, USTART, GSTART, RSTART
            b1_records.append((tid, c1, c2))
            
    print(f"\n{m}:")
    print(f"  L2 Anthills: {l2_hills}")
    print(f"  B1 Spawns (tid, c1, c2):")
    for r in b1_records:
        print(f"    {r}")

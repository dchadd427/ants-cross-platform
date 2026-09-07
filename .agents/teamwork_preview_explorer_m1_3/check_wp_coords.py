import struct

for m in ["SMALL", "MEDIUM", "GAUNTLET", "ISLANDS", "TREASURE"]:
    path = f"/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/{m}.LVL"
    with open(path, "rb") as f:
        data = f.read()
    tcount, = struct.unpack_from("<H", data, 40)
    pos = 42 + (tcount + 1) * 11
    w, h = struct.unpack_from("<II", data, pos)
    pos += 8 + w * h * 12 # skip L1 and L2
    
    b1_cnt, = struct.unpack_from("<H", data, pos)
    pos += 2 + b1_cnt * 6
    
    b2_cnt, = struct.unpack_from("<H", data, pos)
    pos += 2
    for _ in range(b2_cnt):
        pos += 10
        _, _, _, _, icnt = struct.unpack_from("<HHHHH", data, pos - 10)
        pos += icnt * 4
        
    pos += 4 # Block 3
    
    b4_cnt, = struct.unpack_from("<H", data, pos)
    pos += 2
    print(f"\n{m} Block 4 Waypoints:")
    for i in range(min(5, b4_cnt)):
        c1, c2, flag = struct.unpack_from("<HHI", data, pos)
        pos += 8
        pts = []
        param = 0
        if flag != 0:
            param, = struct.unpack_from("<I", data, pos)
            pos += 4
            for _ in range(5):
                px, py = struct.unpack_from("<II", data, pos)
                pos += 8
                pts.append((px, py))
        print(f"  wp {i}: c1={c1}, c2={c2}, flag={flag}, param={param}, pts={pts}")

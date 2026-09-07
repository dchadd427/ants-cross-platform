import struct

for m in ["TINY", "SMALL", "MEDIUM"]:
    path = f"/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/{m}.LVL"
    with open(path, "rb") as f:
        data = f.read()
    tcount, = struct.unpack_from("<H", data, 40)
    pos = 42 + (tcount + 1) * 11
    w, h = struct.unpack_from("<II", data, pos)
    pos += 8 + w * h * 6 # skip L1
    
    # Read L2 tiles into 2D array
    l2 = [[0]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            w1, w2, w3 = struct.unpack_from("<HHH", data, pos)
            pos += 6
            l2[y][x] = w1
            
    b1_cnt, = struct.unpack_from("<H", data, pos)
    pos += 2 + b1_cnt * 6
    
    b2_cnt, = struct.unpack_from("<H", data, pos)
    pos += 2
    print(f"\n{m} Block 2 food coordinates check:")
    for _ in range(min(5, b2_cnt)):
        c1, c2, idel, rint, icnt = struct.unpack_from("<HHHHH", data, pos)
        pos += 10
        vars = []
        for _ in range(icnt):
            wt, tid = struct.unpack_from("<HH", data, pos)
            pos += 4
            vars.append((wt, tid))
        # Check l2[c2][c1] vs l2[c1][c2]
        l2_c1_c2 = l2[c2][c1] if c2 < h and c1 < w else "OOB"
        l2_c2_c1 = l2[c1][c2] if c1 < h and c2 < w else "OOB"
        print(f"  c1={c1}, c2={c2}, idel={idel}, rint={rint}, items={vars}")
        print(f"    if c1=x, c2=y: L2[y][x] = {l2_c1_c2} (tile {vars[0][1]}? {l2_c1_c2 == vars[0][1]})")
        print(f"    if c1=y, c2=x: L2[x][y] = {l2_c2_c1} (tile {vars[0][1]}? {l2_c2_c1 == vars[0][1]})")

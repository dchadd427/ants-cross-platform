import struct

# Deep inspect Maps
maps = ["TINY", "SMALL", "MEDIUM", "GAUNTLET", "ISLANDS", "TREASURE"]
map_stats = {}

for m in maps:
    path = f"/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/{m}.LVL"
    with open(path, "rb") as f:
        data = f.read()
    ver, mode, mins = struct.unpack_from("<IIH", data, 0)
    desc = data[10:40].split(b"\x00")[0].decode("latin1")
    tcount, = struct.unpack_from("<H", data, 40)
    dict_entries = []
    pos = 42
    for i in range(tcount + 1):
        name = data[pos:pos+11].split(b"\x00")[0].decode("latin1")
        dict_entries.append(name)
        pos += 11
    w, h = struct.unpack_from("<II", data, pos)
    pos += 8
    
    # Layer 1
    l1_cells = []
    for i in range(w * h):
        w1, w2, w3 = struct.unpack_from("<HHH", data, pos)
        l1_cells.append((w1, w2, w3))
        pos += 6
        
    # Layer 2
    l2_cells = []
    l2_non_empty = []
    for i in range(w * h):
        w1, w2, w3 = struct.unpack_from("<HHH", data, pos)
        l2_cells.append((w1, w2, w3))
        if w1 != 0x7FFE:
            tname = dict_entries[w1] if w1 < len(dict_entries) else f"0x{w1:04X}"
            l2_non_empty.append((i % w, i // w, w1, tname, w2, w3))
        pos += 6
        
    # Block 1: Spawns
    b1_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    b1_items = []
    for i in range(b1_count):
        tid, y, x = struct.unpack_from("<HHH", data, pos)
        pos += 6
        tname = dict_entries[tid] if tid < len(dict_entries) else f"0x{tid:04X}"
        b1_items.append((x, y, tid, tname))
        
    # Block 2: Food
    b2_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    b2_items = []
    for i in range(b2_count):
        x, y, idel, rint, icount = struct.unpack_from("<HHHHH", data, pos)
        pos += 10
        variants = []
        for j in range(icount):
            weight, tid = struct.unpack_from("<HH", data, pos)
            pos += 4
            tname = dict_entries[tid] if tid < len(dict_entries) else f"0x{tid:04X}"
            variants.append((weight, tid, tname))
        b2_items.append((x, y, idel, rint, icount, variants))
        
    # Block 3: Ambient
    b3_f1, b3_tid = struct.unpack_from("<HH", data, pos)
    pos += 4
    b3_name = dict_entries[b3_tid] if b3_tid < len(dict_entries) else f"0x{b3_tid:04X}"
    
    # Block 4: Waypoints
    b4_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    b4_items = []
    for i in range(b4_count):
        x, y, flag = struct.unpack_from("<HHI", data, pos)
        pos += 8
        param = None
        pts = []
        if flag != 0:
            param, = struct.unpack_from("<I", data, pos)
            pos += 4
            for k in range(5):
                px, py = struct.unpack_from("<II", data, pos)
                pos += 8
                pts.append((px, py))
        b4_items.append((x, y, flag, param, pts))
        
    f_last, = struct.unpack_from("<H", data, pos)
    pos += 2
    
    print(f"=== {m}.LVL ===")
    print(f"  Size: {len(data)}, Dims: {w}x{h}, Dict: {tcount}+1 = {len(dict_entries)}, f_last: {f_last}")
    print(f"  Layer 2 non-empty count: {len(l2_non_empty)} / {w*h}")
    # summarize layer 2 object names
    l2_names = set(item[3] for item in l2_non_empty)
    print(f"  Layer 2 distinct objects ({len(l2_names)}): {sorted(list(l2_names))[:15]}...")
    print(f"  Block 1 Spawns count: {b1_count}, types: {set(item[3] for item in b1_items)}")
    # Find base spawns
    base_spawns = [it for it in b1_items if 'START' in it[3] or 'HILL' in it[3]]
    print(f"  Base spawns: {base_spawns}")
    print(f"  Block 2 Food count: {b2_count}")
    print(f"  Block 3 Ambient: f1={b3_f1}, tid={b3_tid} ({b3_name})")
    print(f"  Block 4 Waypoints: count={b4_count}, non-zero flag count={sum(1 for it in b4_items if it[2] != 0)}")
    print(f"  Parsed bytes: {pos} / {len(data)} (rem = {len(data) - pos})")

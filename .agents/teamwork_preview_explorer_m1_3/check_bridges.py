import glob, os, struct

maps_dir = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps"
lvl_files = sorted(glob.glob(os.path.join(maps_dir, "*.LVL")))

for lvl_path in lvl_files:
    name = os.path.basename(lvl_path)
    with open(lvl_path, "rb") as fp:
        data = fp.read()
    tc, = struct.unpack_from("<H", data, 40)
    pos = 42
    d = {}
    for i in range(tc + 1):
        s = data[pos:pos+11].split(b"\x00")[0].decode("latin1")
        d[i] = s
        pos += 11
    
    b_tiles = {k: d[k] for k in [34, 35, 36, 37, 134] if k in d}
    print(f"{name}: {b_tiles}")

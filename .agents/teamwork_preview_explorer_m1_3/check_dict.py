import struct
import glob
import os

maps_dir = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps"
lvl_files = sorted(glob.glob(os.path.join(maps_dir, "*.LVL")))

for lvl_path in lvl_files:
    name = os.path.basename(lvl_path)
    with open(lvl_path, "rb") as fp:
        data = fp.read()
    
    tile_count, = struct.unpack_from("<H", data, 40)
    pos = 42
    entries = [data[pos+i*11:pos+(i+1)*11] for i in range(tile_count + 1)]
    print(f"\n{name} (tile_count = {tile_count}):")
    print(f"  Entry 0: {entries[0]}")
    print(f"  Entry 1: {entries[1]}")
    print(f"  Entry 2: {entries[2]}")
    print(f"  Entry {tile_count-1}: {entries[tile_count-1]}")
    print(f"  Entry {tile_count}: {entries[tile_count]}")

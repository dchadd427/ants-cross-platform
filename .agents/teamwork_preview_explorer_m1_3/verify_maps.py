import struct
import glob
import os

maps_dir = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps"
lvl_files = sorted(glob.glob(os.path.join(maps_dir, "*.LVL")))

for lvl_path in lvl_files:
    name = os.path.basename(lvl_path)
    with open(lvl_path, "rb") as fp:
        data = fp.read()
    
    pos = 0
    version, game_mode, default_minutes = struct.unpack_from("<IIH", data, pos)
    pos += 10
    desc_bytes = data[pos:pos+30]
    desc = desc_bytes.split(b"\x00")[0].decode("latin1", errors="replace")
    pos += 30
    
    tile_type_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    
    dict_count = tile_type_count + 1
    dict_entries = []
    for i in range(dict_count):
        raw_name = data[pos:pos+11]
        name_str = raw_name.split(b"\x00")[0].decode("latin1", errors="replace")
        dict_entries.append((raw_name, name_str))
        pos += 11
        
    width, height = struct.unpack_from("<II", data, pos)
    pos += 8
    
    print(f"\n========================================================")
    print(f"[{name}] File size: {len(data)} bytes")
    print(f"  Header: ver={version}, mode={game_mode}, min={default_minutes}, desc='{desc}'")
    print(f"  Tile count in hdr: {tile_type_count} (+1 = {dict_count} entries of 11 bytes)")
    print(f"  First 3 dict entries: {[e[1] for e in dict_entries[:3]]}")
    print(f"  Last 3 dict entries: {[e[1] for e in dict_entries[-3:]]}")
    print(f"  Dimensions: width={width}, height={height}")
    
    # Layer 1: width * height * 6 bytes
    l1_size = width * height * 6
    l1_data = data[pos:pos+l1_size]
    pos += l1_size
    
    # Layer 2: width * height * 6 bytes
    l2_size = width * height * 6
    l2_data = data[pos:pos+l2_size]
    pos += l2_size
    
    print(f"  Layer 1 size: {l1_size} bytes (offset {pos - l1_size - l2_size} to {pos - l2_size})")
    print(f"  Layer 2 size: {l2_size} bytes (offset {pos - l2_size} to {pos})")
    
    # Trailing blocks:
    # Block 1: Spawns
    b1_start = pos
    b1_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    b1_records = []
    for i in range(b1_count):
        tile_id, y, x = struct.unpack_from("<HHH", data, pos)
        pos += 6
        tile_name = dict_entries[tile_id][1] if tile_id < len(dict_entries) else f"UNKNOWN({tile_id})"
        b1_records.append((tile_id, tile_name, x, y))
    print(f"  Block 1 (Spawns): count={b1_count}, consumed {pos - b1_start} bytes")
    print(f"    Spawns sample: {b1_records[:4]} ... total {len(b1_records)}")
    
    # Block 2: Food schedules
    b2_start = pos
    b2_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    b2_records = []
    for i in range(b2_count):
        x, y, init_delay, respawn_int, item_count = struct.unpack_from("<HHHHH", data, pos)
        pos += 10
        variants = []
        for j in range(item_count):
            weight, tile_id = struct.unpack_from("<HH", data, pos)
            pos += 4
            tname = dict_entries[tile_id][1] if tile_id < len(dict_entries) else f"0x{tile_id:04X}"
            variants.append((weight, tile_id, tname))
        b2_records.append((x, y, init_delay, respawn_int, item_count, variants))
    print(f"  Block 2 (Food): count={b2_count}, consumed {pos - b2_start} bytes")
    print(f"    Food sample: {b2_records[:2]}")
    
    # Block 3: Ambient
    b3_start = pos
    b3_flag1, b3_tile_sound = struct.unpack_from("<HH", data, pos)
    pos += 4
    b3_tname = dict_entries[b3_tile_sound][1] if b3_tile_sound < len(dict_entries) else f"0x{b3_tile_sound:04X}"
    print(f"  Block 3 (Ambient): flag1={b3_flag1}, tile_or_sound={b3_tile_sound} ({b3_tname}), consumed {pos - b3_start} bytes")
    
    # Block 4: Waypoints
    b4_start = pos
    b4_count, = struct.unpack_from("<H", data, pos)
    pos += 2
    b4_records = []
    for i in range(b4_count):
        x, y, flag = struct.unpack_from("<HHI", data, pos)
        pos += 8
        param = None
        points = []
        if flag != 0:
            param, = struct.unpack_from("<I", data, pos)
            pos += 4
            for p_idx in range(5):
                px, py = struct.unpack_from("<II", data, pos)
                pos += 8
                points.append((px, py))
        b4_records.append((x, y, flag, param, points))
    print(f"  Block 4 (Waypoints): count={b4_count}, consumed {pos - b4_start} bytes")
    if b4_count > 0:
        print(f"    Waypoints sample: count={b4_count}, first: {b4_records[0]}")
    
    # Final field
    f_last, = struct.unpack_from("<H", data, pos)
    pos += 2
    
    rem = len(data) - pos
    print(f"  f_last: {f_last}")
    print(f"  FINAL VERIFICATION: Total parsed = {pos}, File size = {len(data)}, REMAINING = {rem} bytes")
    assert rem == 0, f"FAILED: {name} rem={rem} != 0"
    print(f"  >>> SUCCESS: {name} rem == 0 verified! <<<")

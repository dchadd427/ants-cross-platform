import struct

chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd"
with open(chd_path, "rb") as f:
    hdr = f.read(28)
ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack("<IIIIIII", hdr)

with open(chd_path, "rb") as f:
    f.seek(t1_off)
    s_count, = struct.unpack("<I", f.read(4))
    s_offsets = struct.unpack(f"<{s_count}I", f.read(4 * s_count))
    total_pixels = 0
    for off in s_offsets:
        f.seek(off)
        pitch, width, height, fn_len = struct.unpack("<IIII", f.read(16))
        total_pixels += pitch * height

print(f"Total pixel bytes across all 2794 sprites: {total_pixels} bytes ({total_pixels / (1024*1024):.2f} MB)")

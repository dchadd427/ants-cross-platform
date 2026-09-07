import struct

chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd"
with open(chd_path, "rb") as f:
    hdr = f.read(28)
ver, ts, t1_off, t2_off, t3_off, t4_off, pal_bytes = struct.unpack("<IIIIIII", hdr)
print(f"CHD Header: ver={ver}, t1={t1_off}, t2={t2_off}, t3={t3_off}, t4={t4_off}, pal_bytes={pal_bytes}")

with open(chd_path, "rb") as f:
    # Table 1
    f.seek(t1_off)
    s_count, = struct.unpack("<I", f.read(4))
    s_offsets = struct.unpack(f"<{s_count}I", f.read(4 * s_count))
    sprites = []
    for i, off in enumerate(s_offsets):
        f.seek(off)
        pitch, width, height, fn_len = struct.unpack("<IIII", f.read(16))
        fn = f.read(fn_len).split(b"\x00")[0].decode("latin1")
        sprites.append((i, fn, width, height, pitch))
    
    # Table 4
    f.seek(t4_off)
    a_count, = struct.unpack("<I", f.read(4))
    a_offsets = struct.unpack(f"<{a_count}I", f.read(4 * a_count))
    anims = []
    for i, off in enumerate(a_offsets):
        f.seek(off)
        nlen, = struct.unpack("<I", f.read(4))
        name = f.read(nlen).split(b"\x00")[0].decode("latin1")
        f1, f2, f3, sub_cnt = struct.unpack("<IIII", f.read(16))
        subitems = []
        for s in range(sub_cnt):
            v1, v2, v3, bl, bt, br, bb, v8, def_sp, f_cnt = struct.unpack("<IIIIIIIIII", f.read(40))
            frames = []
            for fr in range(f_cnt):
                dx, dy, s_idx = struct.unpack("<iiI", f.read(12))
                frames.append((dx, dy, s_idx))
            subitems.append((bl, bt, br, bb, def_sp, frames))
        anims.append((i, name, sub_cnt, subitems))

print(f"Loaded {len(sprites)} sprites, {len(anims)} animations.")

# Find lunchbox sprites
lb_sprites = [s for s in sprites if "lb" in s[1].lower()]
print(f"\nFound {len(lb_sprites)} lunchbox sprites:")
for s in lb_sprites:
    print(f"  Sprite {s[0]}: {s[1]}, size={s[2]}x{s[3]}, pitch={s[4]}")

# Find lunchbox animations
lb_anims = [a for a in anims if "lb" in a[1].lower()]
print(f"\nFound {len(lb_anims)} lunchbox animations:")
for a in lb_anims:
    print(f"  Anim {a[0]}: {a[1]}, subitems={a[2]}")
    for si_idx, si in enumerate(a[3]):
        print(f"    Subitem {si_idx}: bbox=({si[0]},{si[1]},{si[2]},{si[3]}), frames={si[5]}")

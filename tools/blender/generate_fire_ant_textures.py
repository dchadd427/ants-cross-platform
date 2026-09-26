"""
Generate ultra-fidelity PBR textures for Fire Ant (Caste #2: Mason / af)
Calibrated 1:1 to 2D master concept (fire_ant_master_reference.jpg)
"""

import math
import os
import random
from PIL import Image, ImageDraw, ImageFont, ImageFilter

random.seed(42)

OUT_DIR = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/textures_fire"
os.makedirs(OUT_DIR, exist_ok=True)

# -----------------------------------------------------------------------------
# 1. Fire Ant Eye Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_eye_texture():
    size = 1024
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cx, cy = size * 0.50, size * 0.50
    iris_cx, iris_cy = size * 0.512, size * 0.50

    r_outer = size * 0.48
    r_iris = size * 0.22   # Hazel/amber iris framed by large bright white sclera
    r_pupil = size * 0.11  # Expressive dark pupil

    for y in range(size):
        ny = (y - cy) / (size * 0.5)
        for x in range(size):
            nx = (x - cx) / (size * 0.5)
            d_sclera = math.sqrt(nx**2 + (ny / 1.05)**2)
            
            # Bright warm ivory-white sclera
            t_edge = max(0.0, min(1.0, (d_sclera - 0.70) / 0.28))
            r = int(252 - 14 * t_edge)
            g = int(253 - 12 * t_edge)
            b = int(247 - 18 * t_edge)
            
            # Check iris
            d_iris = math.sqrt(((x - iris_cx) / r_iris)**2 + ((y - iris_cy) / (r_iris * 1.02))**2)
            d_pupil = math.sqrt(((x - iris_cx) / r_pupil)**2 + ((y - iris_cy) / r_pupil)**2)

            if d_iris <= 1.0:
                angle = math.atan2(y - iris_cy, x - iris_cx)
                t_iris = (d_iris - (r_pupil / r_iris)) / (1.0 - (r_pupil / r_iris))
                t_iris = max(0.0, min(1.0, t_iris))

                fibers = math.sin(angle * 64.0) * 0.08 + math.cos(angle * 128.0 + 0.8) * 0.05
                # Warm fiery amber sunburst in iris
                amber_ring = math.exp(-((t_iris - 0.42)**2) / 0.06) * 0.70
                
                base_r = int(145 + amber_ring * 75 + fibers * 25)
                base_g = int(110 + amber_ring * 45 + fibers * 18)
                base_b = int(40  + amber_ring * 15 + fibers * 10)
                
                # Dark limbal ring
                if t_iris > 0.82:
                    t_limb = (t_iris - 0.82) / 0.18
                    base_r = int(base_r * (1.0 - 0.70 * t_limb) + 38 * t_limb)
                    base_g = int(base_g * (1.0 - 0.70 * t_limb) + 28 * t_limb)
                    base_b = int(base_b * (1.0 - 0.70 * t_limb) + 16 * t_limb)
                
                r, g, b = base_r, base_g, base_b

            if d_pupil <= 1.0:
                t_p = min(1.0, max(0.0, (1.0 - d_pupil) * 16.0))
                r = int(r * (1.0 - t_p) + 12 * t_p)
                g = int(g * (1.0 - t_p) + 10 * t_p)
                b = int(b * (1.0 - t_p) + 14 * t_p)

            img.putpixel((x, y), (min(255, max(0, r)),
                                  min(255, max(0, g)),
                                  min(255, max(0, b)),
                                  255))

    # Catchlights
    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d_over = ImageDraw.Draw(overlay)

    # Primary keylight softbox at 10:30
    hl_cx = int(iris_cx - r_iris * 0.40)
    hl_cy = int(iris_cy - r_iris * 0.45)
    hl_r = int(r_pupil * 0.55)
    d_over.ellipse([hl_cx - hl_r, hl_cy - hl_r, hl_cx + hl_r, hl_cy + hl_r],
                   fill=(255, 255, 255, 248))
    
    # Secondary subtle reflection at 4:30
    s_cx = int(iris_cx + r_iris * 0.45)
    s_cy = int(iris_cy + r_iris * 0.40)
    s_r = int(r_pupil * 0.28)
    d_over.ellipse([s_cx - s_r, s_cy - s_r, s_cx + s_r, s_cy + s_r],
                   fill=(255, 245, 235, 80))

    overlay = overlay.filter(ImageFilter.GaussianBlur(radius=2.5))
    img = Image.alpha_composite(img, overlay)

    out_path = f"{OUT_DIR}/fire_eye_pbr.png"
    img.save(out_path)
    print(f"Generated Fire Ant eye texture: {out_path}")

# -----------------------------------------------------------------------------
# 2. Fire Ant Chitin Texture: Charcoal-Plum & Burnt Sienna (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_chitin_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            
            n1 = math.sin(fx * 14.0) * math.cos(fy * 14.0)
            n2 = math.sin(fx * 32.0 + fy * 16.0) * 0.5
            n3 = math.sin(fx * 64.0 - fy * 32.0) * 0.25
            val = (n1 + n2 + n3) / 1.75
            
            # Subtle gradient along carapace
            gradient = max(0.0, math.sin(fy * math.pi)) * 0.35 + val * 0.25
            t_amber = max(0.0, min(1.0, 0.35 + gradient))
            
            # Charcoal-plum base (#342B3A = 52, 43, 58) blending into rich burnt sienna (#823824 = 130, 56, 36)
            r = int(52 * (1 - t_amber) + 130 * t_amber + val * 10)
            g = int(43 * (1 - t_amber) + 56 * t_amber + val * 6)
            b = int(58 * (1 - t_amber) + 36 * t_amber + val * 4)
            
            pore = (random.random() - 0.5) * 10
            r = int(min(255, max(0, r + pore)))
            g = int(min(255, max(0, g + pore)))
            b = int(min(255, max(0, b + pore)))
            
            pixels[x, y] = (r, g, b, 255)

    out_path = f"{OUT_DIR}/fire_chitin_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant chitin texture: {out_path}")

# -----------------------------------------------------------------------------
# 3. Fire Chief Helmet Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_helmet_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            
            n1 = math.sin(fx * 10.0) * math.cos(fy * 10.0) * 0.5
            n2 = (random.random() - 0.5) * 6.0
            
            # Rich golden yellow (#E5B824 = 229, 184, 36) to bright marigold (#F2C438 = 242, 196, 56)
            val = (n1 + n2) / 10.0
            r = int(min(255, max(0, 235 + val * 15)))
            g = int(min(255, max(0, 188 + val * 12)))
            b = int(min(255, max(0, 38 + val * 8)))
            
            pixels[x, y] = (r, g, b, 255)

    out_path = f"{OUT_DIR}/fire_helmet_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Chief helmet texture: {out_path}")

# -----------------------------------------------------------------------------
# 4. Fire Chief Front Shield Plaque with Bold Red 'A' (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_shield_texture():
    size = 1024
    img = Image.new("RGBA", (size, size), (242, 196, 56, 255)) # Gold shield background
    draw = ImageDraw.Draw(img)

    # Darker gold border / bevel around plaque
    draw.rectangle([16, 16, size - 17, size - 17], outline=(190, 140, 25, 255), width=24)
    draw.rectangle([40, 40, size - 41, size - 41], outline=(255, 225, 90, 255), width=12)

    # Render bold red letter 'A'
    # Try using system font or draw geometric polygonal letter 'A'
    # Geometric high-precision serif letter 'A'
    cx = size // 2
    top_y = int(size * 0.18)
    bot_y = int(size * 0.82)
    outer_w = int(size * 0.34)
    inner_w = int(size * 0.14)
    bar_y1 = int(size * 0.52)
    bar_y2 = int(size * 0.64)

    # Shadow for 3D embossed look
    shadow_offset = 12
    draw.polygon([
        (cx + shadow_offset, top_y + shadow_offset),
        (cx + outer_w + shadow_offset, bot_y + shadow_offset),
        (cx + outer_w - 90 + shadow_offset, bot_y + shadow_offset),
        (cx + 45 + shadow_offset, int(size * 0.44) + shadow_offset),
        (cx - 45 + shadow_offset, int(size * 0.44) + shadow_offset),
        (cx - outer_w + 90 + shadow_offset, bot_y + shadow_offset),
        (cx - outer_w + shadow_offset, bot_y + shadow_offset)
    ], fill=(130, 20, 20, 255))

    # Main bold scarlet/crimson Red 'A' (#D32F2F = 211, 47, 47)
    draw.polygon([
        (cx, top_y),
        (cx + outer_w, bot_y),
        (cx + outer_w - 90, bot_y),
        (cx + 45, int(size * 0.44)),
        (cx - 45, int(size * 0.44)),
        (cx - outer_w + 90, bot_y),
        (cx - outer_w, bot_y)
    ], fill=(215, 35, 35, 255))

    # Crossbar
    draw.rectangle([cx - int(outer_w * 0.65), bar_y1, cx + int(outer_w * 0.65), bar_y2], fill=(215, 35, 35, 255))

    # Inner cutout triangle
    draw.polygon([
        (cx, int(size * 0.28)),
        (cx + inner_w, int(size * 0.50)),
        (cx - inner_w, int(size * 0.50))
    ], fill=(242, 196, 56, 255))

    # Subtle bevel highlights on letter 'A'
    draw.line([(cx, top_y), (cx - outer_w, bot_y)], fill=(255, 110, 110, 255), width=8)
    draw.line([(cx - int(outer_w * 0.65), bar_y1), (cx + int(outer_w * 0.65), bar_y1)], fill=(255, 110, 110, 255), width=6)

    # Soften slightly with blur
    img = img.filter(ImageFilter.GaussianBlur(radius=1.2))

    out_path = f"{OUT_DIR}/fire_shield_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Chief shield plaque texture: {out_path}")

# -----------------------------------------------------------------------------
# 5. Fire Ant Mandible Texture (512x512)
# -----------------------------------------------------------------------------
def generate_fire_mandible_texture():
    size = 512
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        t = y / size
        for x in range(size):
            noise = (random.random() - 0.5) * 6
            if t < 0.40:
                # Charcoal-plum base
                r = int(58 + noise)
                g = int(46 + noise)
                b = int(62 + noise)
            elif t < 0.75:
                # Burnt sienna transition
                blend = (t - 0.40) / 0.35
                r = int(58 * (1 - blend) + 140 * blend + noise)
                g = int(46 * (1 - blend) + 70 * blend + noise)
                b = int(62 * (1 - blend) + 45 * blend + noise)
            else:
                # Pale sharp ivory/amber biting tip
                blend = (t - 0.75) / 0.25
                r = int(140 * (1 - blend) + 210 * blend + noise)
                g = int(70 * (1 - blend) + 170 * blend + noise)
                b = int(45 * (1 - blend) + 110 * blend + noise)
                
            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/fire_mandible_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant mandible texture: {out_path}")

# -----------------------------------------------------------------------------
# 6. Fire Ant Limbs Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_limbs_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            ridge = math.sin(fx * 36.0 * math.pi) * 0.5 + math.sin(fx * 72.0 * math.pi) * 0.25
            pore = (random.random() - 0.5) * 8.0

            mottle = math.sin(fx * 10.0 + fy * 14.0) * 0.5 + math.cos(fx * 20.0 - fy * 16.0) * 0.3
            joint_t = max(0.0, math.sin(fy * math.pi * 4.0)) * 18.0

            blend_t = max(0.0, min(1.0, 0.40 + 0.35 * ridge + 0.25 * mottle))
            # Deep charcoal-plum (#2C2430) with burnt-amber joint collars (#944222)
            r = int(44 * (1.0 - blend_t) + 128 * blend_t + joint_t * 0.9 + pore)
            g = int(32 * (1.0 - blend_t) + 56 * blend_t + joint_t * 0.5 + pore * 0.5)
            b = int(40 * (1.0 - blend_t) + 38 * blend_t + joint_t * 0.3 + pore * 0.3)

            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/fire_limbs_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant limbs texture: {out_path}")

if __name__ == "__main__":
    generate_fire_eye_texture()
    generate_fire_chitin_texture()
    generate_fire_helmet_texture()
    generate_fire_shield_texture()
    generate_fire_mandible_texture()
    generate_fire_limbs_texture()
    print("All Fire Ant PBR textures generated successfully!")

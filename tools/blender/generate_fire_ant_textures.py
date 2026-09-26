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
# 2. Fire Ant Chitin Texture: Canonical Moss-Green (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_chitin_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            
            n1 = math.sin(fx * 16.0) * math.cos(fy * 16.0)
            n2 = math.sin(fx * 36.0 + fy * 20.0) * 0.5
            n3 = math.sin(fx * 72.0 - fy * 36.0) * 0.25
            val = (n1 + n2 + n3) / 1.75
            
            gradient = max(0.0, math.sin(fy * math.pi)) * 0.35 + val * 0.25
            t_blend = max(0.0, min(1.0, 0.40 + gradient))
            
            # Canonical moss-green ant chitin palette (matching Worker):
            # Base dark moss-green (#3B592D = 59, 89, 45) to rich olive-amber (#6B8E4E = 107, 142, 78)
            r = int(59 * (1.0 - t_blend) + 107 * t_blend + val * 8)
            g = int(89 * (1.0 - t_blend) + 142 * t_blend + val * 10)
            b = int(45 * (1.0 - t_blend) + 78  * t_blend + val * 6)
            
            pore = (random.random() - 0.5) * 6
            r = int(min(255, max(0, r + pore)))
            g = int(min(255, max(0, g + pore)))
            b = int(min(255, max(0, b + pore)))
            
            pixels[x, y] = (r, g, b, 255)

    out_path = f"{OUT_DIR}/fire_chitin_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant canonical moss-green chitin texture: {out_path}")


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
    img = Image.new("RGBA", (size, size), (242, 196, 56, 255))
    draw = ImageDraw.Draw(img)

    draw.rectangle([16, 16, size - 17, size - 17], outline=(180, 130, 20, 255), width=24)
    draw.rectangle([40, 40, size - 41, size - 41], outline=(255, 230, 95, 255), width=12)

    cx = size // 2
    top_y = int(size * 0.18)
    bot_y = int(size * 0.82)
    outer_w = int(size * 0.34)
    inner_w = int(size * 0.14)
    bar_y1 = int(size * 0.52)
    bar_y2 = int(size * 0.64)

    shadow_offset = 12
    draw.polygon([
        (cx + shadow_offset, top_y + shadow_offset),
        (cx + outer_w + shadow_offset, bot_y + shadow_offset),
        (cx + outer_w - 90 + shadow_offset, bot_y + shadow_offset),
        (cx + 45 + shadow_offset, int(size * 0.44) + shadow_offset),
        (cx - 45 + shadow_offset, int(size * 0.44) + shadow_offset),
        (cx - outer_w + 90 + shadow_offset, bot_y + shadow_offset),
        (cx - outer_w + shadow_offset, bot_y + shadow_offset)
    ], fill=(110, 15, 15, 255))

    draw.polygon([
        (cx, top_y),
        (cx + outer_w, bot_y),
        (cx + outer_w - 90, bot_y),
        (cx + 45, int(size * 0.44)),
        (cx - 45, int(size * 0.44)),
        (cx - outer_w + 90, bot_y),
        (cx - outer_w, bot_y)
    ], fill=(225, 25, 25, 255))

    draw.rectangle([cx - int(outer_w * 0.65), bar_y1, cx + int(outer_w * 0.65), bar_y2], fill=(225, 25, 25, 255))

    draw.polygon([
        (cx, int(size * 0.28)),
        (cx + inner_w, int(size * 0.50)),
        (cx - inner_w, int(size * 0.50))
    ], fill=(242, 196, 56, 255))

    img = img.filter(ImageFilter.GaussianBlur(radius=1.2))
    out_path = f"{OUT_DIR}/fire_shield_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Chief shield texture: {out_path}")

# -----------------------------------------------------------------------------
# 5. Fire Ant Pincer Claw Texture: Moss-Green with Luminous Chartreuse Tips (512x512)
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
                # Deep moss-green basal hinge (#3B592D)
                r = int(59 + noise)
                g = int(89 + noise)
                b = int(45 + noise)
            elif t < 0.72:
                # Luminous chartreuse muscle blade (#A8CC44)
                blend = (t - 0.40) / 0.32
                r = int(59 * (1 - blend) + 168 * blend + noise)
                g = int(89 * (1 - blend) + 204 * blend + noise)
                b = int(45 * (1 - blend) + 68  * blend + noise)
            else:
                # Bright bone-ivory biting edge / tip (#E8F0C8)
                blend = (t - 0.72) / 0.28
                r = int(168 * (1 - blend) + 232 * blend + noise)
                g = int(204 * (1 - blend) + 242 * blend + noise)
                b = int(68  * (1 - blend) + 195 * blend + noise)
                
            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/fire_mandible_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant mandible texture: {out_path}")


# -----------------------------------------------------------------------------
# 6. Fire Ant Limbs Texture: Charcoal-Plum & Slate-Violet (1024x1024)
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
            pore = (random.random() - 0.5) * 6.0

            mottle = math.sin(fx * 10.0 + fy * 14.0) * 0.5 + math.cos(fx * 20.0 - fy * 16.0) * 0.3
            joint_t = max(0.0, math.sin(fy * math.pi * 4.0)) * 12.0

            blend_t = max(0.0, min(1.0, 0.40 + 0.35 * ridge + 0.25 * mottle))
            # Deep charcoal-plum (#271733) with slate-violet striations (#473353) and dark joints
            r = int(35 * (1.0 - blend_t) + 72 * blend_t + joint_t * 0.6 + pore)
            g = int(22 * (1.0 - blend_t) + 52 * blend_t + joint_t * 0.4 + pore * 0.5)
            b = int(46 * (1.0 - blend_t) + 84 * blend_t + joint_t * 0.7 + pore * 0.5)

            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/fire_limbs_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant limbs texture: {out_path}")

# -----------------------------------------------------------------------------
# 7. Fire Ant Head Texture: Authentic Deep Slate-Violet & Charcoal (1024x1024)
# -----------------------------------------------------------------------------
def generate_fire_head_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            
            # Subtle organic chitin flow
            n1 = math.sin(fx * 12.0) * math.cos(fy * 14.0) * 0.5
            n2 = math.sin(fx * 28.0 + fy * 18.0) * 0.3
            n3 = math.sin(fx * 56.0 - fy * 32.0) * 0.2
            val = (n1 + n2 + n3)
            
            # Face shading gradient: darker at bottom (snout/mandible base) and top (under helmet)
            face_contour = max(0.0, math.sin(fy * math.pi)) * 0.4 + val * 0.2
            t_blend = max(0.0, min(1.0, 0.35 + face_contour))
            
            # Authentic 1998 afst301 palette:
            # Base deep charcoal-plum (#271733 = 39, 23, 51) to rich slate-violet (#473353 = 71, 51, 83 / #57475B = 87, 71, 91)
            pore = (random.random() - 0.5) * 6
            r = int(37 * (1.0 - t_blend) + 82 * t_blend + pore)
            g = int(22 * (1.0 - t_blend) + 66 * t_blend + pore)
            b = int(49 * (1.0 - t_blend) + 88 * t_blend + pore)
            
            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/fire_head_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated Fire Ant head texture: {out_path}")

if __name__ == "__main__":
    generate_fire_eye_texture()
    generate_fire_chitin_texture()
    generate_fire_helmet_texture()
    generate_fire_shield_texture()
    generate_fire_mandible_texture()
    generate_fire_limbs_texture()
    generate_fire_head_texture()
    print("All Fire Ant PBR textures generated successfully!")

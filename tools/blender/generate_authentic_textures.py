"""
Generate ultra-fidelity PBR textures for Worker Ant (Caste #1)
Calibrated 1:1 to master reference artwork (worker_ant_master_1790372115097.jpg)
"""

import math
from PIL import Image, ImageDraw, ImageFilter
import random

random.seed(42)

OUT_DIR = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# -----------------------------------------------------------------------------
# 1. High-Fidelity Innocent Cartoon Eye Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_eye_texture():
    size = 1024
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cx, cy = size * 0.50, size * 0.50
    # Centered innocent gaze (slight 1.5% medial shift for natural binocular focus)
    iris_cx, iris_cy = size * 0.512, size * 0.50

    r_outer = size * 0.48
    # Authentic Pixar/reference proportions: Iris is ~22% of eye, leaving ~56% for wide bright white sclera!
    r_iris = size * 0.22   # Hazel iris framed by large bright white sclera
    r_pupil = size * 0.11  # Expressive dark pupil

    for y in range(size):
        ny = (y - cy) / (size * 0.5)
        for x in range(size):
            nx = (x - cx) / (size * 0.5)
            d_sclera = math.sqrt(nx**2 + (ny / 1.05)**2)
            
            # Bright warm ivory-white sclera (#FCFDF8 center, soft warm cream at perimeter)
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

                # Fine radial striation fibers
                fibers = math.sin(angle * 64.0) * 0.08 + math.cos(angle * 128.0 + 0.8) * 0.05
                # Warm golden-amber sunburst in middle iris zone
                amber_ring = math.exp(-((t_iris - 0.42)**2) / 0.06) * 0.65
                
                base_r = int(122 + amber_ring * 65 + fibers * 22)
                base_g = int(145 + amber_ring * 42 + fibers * 18)
                base_b = int(52  + amber_ring * 18 + fibers * 12)
                
                # Limbal ring: dark olive-brown margin framing the iris against the white sclera
                if t_iris > 0.82:
                    t_limb = (t_iris - 0.82) / 0.18
                    base_r = int(base_r * (1.0 - 0.65 * t_limb) + 32 * t_limb)
                    base_g = int(base_g * (1.0 - 0.60 * t_limb) + 38 * t_limb)
                    base_b = int(base_b * (1.0 - 0.60 * t_limb) + 16 * t_limb)
                
                r, g, b = base_r, base_g, base_b

            if d_pupil <= 1.0:
                t_p = min(1.0, max(0.0, (1.0 - d_pupil) * 16.0))
                r = int(r * (1.0 - t_p) + 10 * t_p)
                g = int(g * (1.0 - t_p) + 10 * t_p)
                b = int(b * (1.0 - t_p) + 12 * t_p)

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
                   fill=(245, 248, 255, 80))

    overlay = overlay.filter(ImageFilter.GaussianBlur(radius=2.5))
    img = Image.alpha_composite(img, overlay)

    out_path = f"{OUT_DIR}/eye_pbr.png"
    img.save(out_path)
    print(f"Generated authentic eye texture: {out_path}")

# -----------------------------------------------------------------------------
# 2. Weathered Sage Chitin Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_chitin_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            
            n1 = math.sin(fx * 12.0) * math.cos(fy * 12.0)
            n2 = math.sin(fx * 28.0 + fy * 14.0) * 0.5
            n3 = math.sin(fx * 64.0 - fy * 32.0) * 0.25
            val = (n1 + n2 + n3) / 1.75
            
            crown_mask = max(0.0, math.sin((fy - 0.28) * math.pi / 0.72)) if fy >= 0.28 else 0.0
            warm_weight = min(1.0, max(0.0, crown_mask * 0.58 + val * 0.22))
            
            r = int(88 * (1 - warm_weight) + 142 * warm_weight + val * 12)
            g = int(122 * (1 - warm_weight) + 104 * warm_weight + val * 8)
            b = int(86 * (1 - warm_weight) + 78 * warm_weight + val * 6)
            
            pore = (random.random() - 0.5) * 12
            r = int(min(255, max(0, r + pore)))
            g = int(min(255, max(0, g + pore)))
            b = int(min(255, max(0, b + pore)))
            
            pixels[x, y] = (r, g, b, 255)

    out_path = f"{OUT_DIR}/chitin_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated authentic chitin texture: {out_path}")

# -----------------------------------------------------------------------------
# 3. Mandible PBR Texture (512x512)
# -----------------------------------------------------------------------------
def generate_mandible_texture():
    size = 512
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        t = y / size
        for x in range(size):
            noise = (random.random() - 0.5) * 6
            if t < 0.35:
                r = int(92 + noise)
                g = int(126 + noise)
                b = int(88 + noise)
            elif t < 0.68:
                blend = (t - 0.35) / 0.33
                r = int(92 * (1 - blend) + 168 * blend + noise)
                g = int(126 * (1 - blend) + 204 * blend + noise)
                b = int(88 * (1 - blend) + 100 * blend + noise)
            else:
                blend = (t - 0.68) / 0.32
                r = int(168 * (1 - blend) + 220 * blend + noise)
                g = int(204 * (1 - blend) + 244 * blend + noise)
                b = int(100 * (1 - blend) + 156 * blend + noise)
                
            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/mandible_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated authentic mandible texture: {out_path}")

# -----------------------------------------------------------------------------
# 4. Limbs / Antennae PBR Texture (512x512)
# -----------------------------------------------------------------------------
def generate_limbs_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            # Longitudinal chitin striation ridges running along the limb axis
            ridge = math.sin(fx * 36.0 * math.pi) * 0.5 + math.sin(fx * 72.0 * math.pi) * 0.25
            pore = (random.random() - 0.5) * 8.0

            # Organic mottling: deep burgundy mahogany (#341510) and warm chestnut terracotta (#7A3822)
            mottle = math.sin(fx * 10.0 + fy * 14.0) * 0.5 + math.cos(fx * 20.0 - fy * 16.0) * 0.3
            joint_t = max(0.0, math.sin(fy * math.pi * 4.0)) * 18.0

            blend_t = max(0.0, min(1.0, 0.40 + 0.35 * ridge + 0.25 * mottle))
            r = int(54 * (1.0 - blend_t) + 122 * blend_t + joint_t * 0.9 + pore)
            g = int(22 * (1.0 - blend_t) + 58 * blend_t + joint_t * 0.6 + pore * 0.6)
            b = int(18 * (1.0 - blend_t) + 42 * blend_t + joint_t * 0.4 + pore * 0.4)

            # Olive lichen/moss undertone patches tying limbs authentically to the body chitin
            olive_fac = max(0.0, math.sin(fx * 7.0 - fy * 9.0) * 0.55)
            r = int(r * (1.0 - 0.35 * olive_fac) + 54 * olive_fac)
            g = int(g * (1.0 - 0.35 * olive_fac) + 72 * olive_fac)
            b = int(b * (1.0 - 0.35 * olive_fac) + 46 * olive_fac)

            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/limbs_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated authentic limbs texture: {out_path}")

if __name__ == "__main__":
    generate_eye_texture()
    generate_chitin_texture()
    generate_mandible_texture()
    generate_limbs_texture()
    print("All authentic textures updated successfully!")

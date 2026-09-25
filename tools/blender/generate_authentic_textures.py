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
    iris_cx, iris_cy = size * 0.515, size * 0.50

    r_outer = size * 0.48
    r_iris = size * 0.285   # Large cute cartoon iris
    r_pupil = size * 0.138  # Expressive dark pupil

    for y in range(size):
        ny = (y - cy) / (size * 0.5)
        for x in range(size):
            nx = (x - cx) / (size * 0.5)
            d_sclera = math.sqrt(nx**2 + (ny / 1.06)**2)
            
            if d_sclera <= 0.97:
                # Warm eggshell ivory sclera (#FAFBF2 in center, fading to #D8DFCA at rim)
                t_edge = max(0.0, (d_sclera - 0.60) / 0.37)
                r = int(250 - 40 * t_edge)
                g = int(252 - 34 * t_edge)
                b = int(242 - 46 * t_edge)
                
                # Check iris
                d_iris = math.sqrt(((x - iris_cx) / r_iris)**2 + ((y - iris_cy) / (r_iris * 1.02))**2)
                d_pupil = math.sqrt(((x - iris_cx) / r_pupil)**2 + ((y - iris_cy) / r_pupil)**2)

                if d_iris <= 1.0:
                    angle = math.atan2(y - iris_cy, x - iris_cx)
                    t_iris = (d_iris - (r_pupil / r_iris)) / (1.0 - (r_pupil / r_iris))
                    t_iris = max(0.0, min(1.0, t_iris))

                    # Fine radial striation fibers
                    fibers = math.sin(angle * 72.0) * 0.08 + math.cos(angle * 144.0 + 0.8) * 0.05
                    # Golden sunburst in middle iris zone
                    amber_ring = math.exp(-((t_iris - 0.42)**2) / 0.045) * 0.55
                    
                    base_r = int(142 + amber_ring * 65 + fibers * 25)
                    base_g = int(162 + amber_ring * 45 + fibers * 20)
                    base_b = int(58  + amber_ring * 20 + fibers * 15)
                    
                    # Limbal ring: dark olive margin
                    if t_iris > 0.82:
                        t_limb = (t_iris - 0.82) / 0.18
                        base_r = int(base_r * (1.0 - 0.65 * t_limb) + 40 * t_limb)
                        base_g = int(base_g * (1.0 - 0.60 * t_limb) + 50 * t_limb)
                        base_b = int(base_b * (1.0 - 0.60 * t_limb) + 22 * t_limb)
                    
                    r, g, b = base_r, base_g, base_b

                if d_pupil <= 1.0:
                    t_p = min(1.0, max(0.0, (1.0 - d_pupil) * 16.0))
                    r = int(r * (1.0 - t_p) + 12 * t_p)
                    g = int(g * (1.0 - t_p) + 12 * t_p)
                    b = int(b * (1.0 - t_p) + 14 * t_p)

                alpha = 255
                if d_sclera > 0.90:
                    alpha = int(255 * (0.97 - d_sclera) / 0.07)
                img.putpixel((x, y), (min(255, max(0, r)),
                                      min(255, max(0, g)),
                                      min(255, max(0, b)),
                                      alpha))

    # Catchlights
    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d_over = ImageDraw.Draw(overlay)

    # Primary keylight softbox at 10:30
    hl_cx = int(iris_cx - r_iris * 0.42)
    hl_cy = int(iris_cy - r_iris * 0.46)
    hl_r = int(r_pupil * 0.52)
    d_over.ellipse([hl_cx - hl_r, hl_cy - hl_r, hl_cx + hl_r, hl_cy + hl_r],
                   fill=(255, 255, 255, 245))
    
    # Secondary subtle reflection at 4:30
    s_cx = int(iris_cx + r_iris * 0.48)
    s_cy = int(iris_cy + r_iris * 0.42)
    s_r = int(r_pupil * 0.26)
    d_over.ellipse([s_cx - s_r, s_cy - s_r, s_cx + s_r, s_cy + s_r],
                   fill=(240, 245, 255, 75))

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
    size = 512
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            grain = math.sin(fx * 48.0) * 8 + math.cos(fy * 24.0) * 6
            pore = (random.random() - 0.5) * 10
            
            green_mottle = max(0.0, math.sin(fx * 16.0 + fy * 18.0) * 0.5 + math.sin(fx * 32.0 - fy * 12.0) * 0.25)
            joint_warm = max(0.0, math.sin(fy * math.pi * 4.0)) * 22
            
            base_r = 92 + joint_warm * 0.8 + grain + pore
            base_g = 62 + joint_warm * 0.5 + grain * 0.5 + pore
            base_b = 50 + joint_warm * 0.3 + grain * 0.3 + pore
            
            r = int(base_r * (1 - green_mottle) + 72 * green_mottle)
            g = int(base_g * (1 - green_mottle) + 94 * green_mottle)
            b = int(base_b * (1 - green_mottle) + 64 * green_mottle)
            
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

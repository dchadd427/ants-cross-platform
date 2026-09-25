"""
Generate ultra-fidelity PBR textures for Worker Ant (Caste #1)
Matches reference artwork:
- eye_pbr.png: Large expressive hazel-olive iris, dark pupil, warm cream sclera, crisp catchlights
- chitin_pbr.png: Weathered sage green with warm russet/terracotta brow dusting and organic pebble pores
- mandible_pbr.png: Gradient from sage green base to chartreuse/pale lime teeth
- limbs_pbr.png: Deep mahogany/charcoal brown with warm terracotta joint accents
"""

import math
from PIL import Image, ImageDraw, ImageFilter
import random

random.seed(42)

OUT_DIR = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# -----------------------------------------------------------------------------
# 1. High-Fidelity Expressive Eye Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_eye_texture():
    size = 1024
    img = Image.new("RGBA", (size, size), (232, 236, 218, 255)) # Warm cream sclera
    draw = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2

    # Spherical aspect factor: horizontal compression in equirectangular space
    # so that on a 3D sphere, the iris and pupil appear circular!
    # In equirectangular mapping, delta_u needs to be scaled by ~1.85 to appear circular.
    u_scale = 1.85

    # Radii in vertical pixels
    r_limbal = 310   # Outer iris ring (limbal ring)
    r_iris = 295     # Main iris body
    r_inner_iris = 165 # Inner iris zone
    r_pupil = 135    # Dark pupil
    
    # 1. Sclera shading: soft warm shadow towards edges
    for y in range(size):
        for x in range(size):
            dx = (x - cx) / u_scale
            dy = (y - cy)
            dist = math.sqrt(dx*dx + dy*dy)
            if dist > r_limbal:
                # Soft ambient occlusion towards the perimeter
                edge_factor = min(1.0, (dist - r_limbal) / 220.0)
                r = int(232 - 45 * edge_factor)
                g = int(236 - 40 * edge_factor)
                b = int(218 - 50 * edge_factor)
                img.putpixel((x, y), (r, g, b, 255))

    # 2. Limbal Ring (sharp dark olive-brown outer border)
    x_min = max(0, int(cx - r_limbal * u_scale - 15))
    x_max = min(size - 1, int(cx + r_limbal * u_scale + 15))
    y_min = max(0, cy - r_limbal - 10)
    y_max = min(size - 1, cy + r_limbal + 10)
    for y in range(y_min, y_max + 1):
        for x in range(x_min, x_max + 1):
            dx = (x - cx) / u_scale
            dy = (y - cy)
            dist = math.sqrt(dx*dx + dy*dy)
            if r_iris < dist <= r_limbal:
                t = (dist - r_iris) / (r_limbal - r_iris)
                # Blend from outer iris (green) to limbal (dark olive #3a481c)
                lr = int(58 * (1-t) + 45 * t)
                lg = int(72 * (1-t) + 55 * t)
                lb = int(28 * (1-t) + 20 * t)
                img.putpixel((x, y), (lr, lg, lb, 255))

    # 3. Main Iris: Olive-green with radial striations & golden-amber stipples
    x_iris_min = max(0, int(cx - r_iris * u_scale))
    x_iris_max = min(size - 1, int(cx + r_iris * u_scale))
    y_iris_min = max(0, cy - r_iris)
    y_iris_max = min(size - 1, cy + r_iris)
    for y in range(y_iris_min, y_iris_max + 1):
        for x in range(x_iris_min, x_iris_max + 1):
            dx = (x - cx) / u_scale
            dy = (y - cy)
            dist = math.sqrt(dx*dx + dy*dy)
            if r_pupil < dist <= r_iris:
                angle = math.atan2(dy, dx)
                t_rad = (dist - r_pupil) / (r_iris - r_pupil)
                
                # Radial striation pattern
                striation = math.sin(angle * 72.0) * 0.15 + math.sin(angle * 144.0) * 0.08
                # Golden-amber ring in middle
                amber_ring = math.exp(-((t_rad - 0.45) ** 2) / 0.04) * 0.35
                
                base_r = 104 + striation * 25 + amber_ring * 55
                base_g = 125 + striation * 20 + amber_ring * 45
                base_b = 50 + striation * 15 + amber_ring * 25
                
                # Darken slightly near pupil margin
                if t_rad < 0.15:
                    darken = t_rad / 0.15
                    base_r *= (0.6 + 0.4 * darken)
                    base_g *= (0.6 + 0.4 * darken)
                    base_b *= (0.6 + 0.4 * darken)
                    
                img.putpixel((x, y), (int(min(255, max(0, base_r))),
                                      int(min(255, max(0, base_g))),
                                      int(min(255, max(0, base_b))),
                                      255))

    # 4. Pupil: Deep velvety dark circle (#151815)
    x_pupil_min = max(0, int(cx - r_pupil * u_scale))
    x_pupil_max = min(size - 1, int(cx + r_pupil * u_scale))
    y_pupil_min = max(0, cy - r_pupil)
    y_pupil_max = min(size - 1, cy + r_pupil)
    for y in range(y_pupil_min, y_pupil_max + 1):
        for x in range(x_pupil_min, x_pupil_max + 1):
            dx = (x - cx) / u_scale
            dy = (y - cy)
            dist = math.sqrt(dx*dx + dy*dy)
            if dist <= r_pupil:
                # Soft anti-aliased edge at pupil boundary
                if dist > r_pupil - 2.5:
                    aa = (r_pupil - dist) / 2.5
                    pr = int(21 * (1 - aa) + 55 * aa)
                    pg = int(24 * (1 - aa) + 65 * aa)
                    pb = int(21 * (1 - aa) + 25 * aa)
                    img.putpixel((x, y), (pr, pg, pb, 255))
                else:
                    img.putpixel((x, y), (21, 24, 21, 255))

    # 5. Crisp Specular Catchlights (Studio Lightbox reflection at 10 o'clock)
    # Primary highlight: curved soft pill reflection
    hl1_x = int(cx - 55 * u_scale)
    hl1_y = cy - 55
    draw.ellipse([hl1_x - int(24 * u_scale), hl1_y - 28,
                  hl1_x + int(24 * u_scale), hl1_y + 28],
                 fill=(255, 255, 255, 250))
                 
    # Secondary subtle catchlight dot at 8 o'clock
    hl2_x = int(cx - 40 * u_scale)
    hl2_y = cy - 10
    draw.ellipse([hl2_x - int(7 * u_scale), hl2_y - 7,
                  hl2_x + int(7 * u_scale), hl2_y + 7],
                 fill=(245, 250, 240, 210))

    # Gentle blur on highlights for natural optics
    out_path = f"{OUT_DIR}/eye_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated authentic eye texture: {out_path}")

# -----------------------------------------------------------------------------
# 2. Weathered Sage Chitin Texture (1024x1024)
# -----------------------------------------------------------------------------
def generate_chitin_texture():
    size = 1024
    img = Image.new("RGBA", (size, size))
    pixels = img.load()

    # Perlin-like cellular noise synthesis
    # Base: Sage green (#5c7b5a)
    # Warm patches: Terracotta/burnt umber (#7c6448)
    # Light accents: Pale sage highlight (#7e9e7a)
    for y in range(size):
        fy = y / size
        for x in range(size):
            fx = x / size
            
            # Multi-octave organic pattern
            n1 = math.sin(fx * 14.0) * math.cos(fy * 14.0)
            n2 = math.sin(fx * 32.0 + fy * 16.0) * 0.5
            n3 = math.sin(fx * 72.0 - fy * 36.0) * 0.25
            val = (n1 + n2 + n3) / 1.75 # roughly [-1, 1]
            
            # Terracotta warm gradient: prominent on upper forehead/crown (fy in [0.55, 0.95])
            # and around brow margins
            crown_mask = max(0.0, math.sin((fy - 0.35) * math.pi / 0.65)) if fy >= 0.35 else 0.0
            warm_weight = crown_mask * 0.55 + max(0.0, val * 0.25)
            warm_weight = min(1.0, max(0.0, warm_weight))
            
            # Base sage green: (92, 126, 92)
            # Warm russet: (138, 108, 76)
            # Light highlight: (130, 162, 126)
            r = int(92 * (1 - warm_weight) + 138 * warm_weight + val * 10)
            g = int(126 * (1 - warm_weight) + 108 * warm_weight + val * 8)
            b = int(92 * (1 - warm_weight) + 76 * warm_weight + val * 6)
            
            # Micro-pore stippling
            noise_pore = (random.random() - 0.5) * 14
            r = int(min(255, max(0, r + noise_pore)))
            g = int(min(255, max(0, g + noise_pore)))
            b = int(min(255, max(0, b + noise_pore)))
            
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

    # Vertical gradient:
    # Top (y < 200): Sage green cheek chitin (#668260)
    # Middle (200-360): Vibrant chartreuse transition (#a6cc68)
    # Bottom / tooth edge (y > 360): Translucent pale lime-cream (#d8f0a0)
    for y in range(size):
        t = y / size
        for x in range(size):
            noise = (random.random() - 0.5) * 8
            if t < 0.40:
                # Green chitin
                r = int(102 + noise)
                g = int(130 + noise)
                b = int(96 + noise)
            elif t < 0.70:
                # Transition zone
                blend = (t - 0.40) / 0.30
                r = int(102 * (1 - blend) + 175 * blend + noise)
                g = int(130 * (1 - blend) + 208 * blend + noise)
                b = int(96 * (1 - blend) + 110 * blend + noise)
            else:
                # Pale tooth enamel
                blend = (t - 0.70) / 0.30
                r = int(175 * (1 - blend) + 218 * blend + noise)
                g = int(208 * (1 - blend) + 242 * blend + noise)
                b = int(110 * (1 - blend) + 165 * blend + noise)
                
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

    # Deep mahogany / charcoal brown with warm terracotta joint undertones
    for y in range(size):
        fy = y / size
        for x in range(size):
            # Longitudinal chitin grain
            grain = math.sin((x / size) * 80.0) * 8
            pore = (random.random() - 0.5) * 10
            
            # Subtle joint warmth
            joint_warm = max(0.0, math.sin(fy * math.pi * 3.0)) * 25
            
            r = int(54 + joint_warm * 0.7 + grain + pore)
            g = int(40 + joint_warm * 0.4 + grain + pore)
            b = int(34 + joint_warm * 0.2 + grain + pore)
            
            pixels[x, y] = (min(255, max(0, r)), min(255, max(0, g)), min(255, max(0, b)), 255)

    out_path = f"{OUT_DIR}/limbs_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated authentic limbs texture: {out_path}")

if __name__ == "__main__":
    generate_eye_texture()
    generate_chitin_texture()
    generate_mandible_texture()
    generate_limbs_texture()
    print("All authentic Worker Ant textures generated successfully!")

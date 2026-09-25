"""
Accurate spherical UV eye texture generator for Worker Ant (Caste #1)
Calculates exact equirectangular projection metrics so the iris and pupil
appear perfectly circular on a 3D UV sphere.
"""

import math
from PIL import Image, ImageDraw, ImageFilter
import random

random.seed(42)

OUT_DIR = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

def generate_eye_texture():
    size = 1024
    img = Image.new("RGBA", (size, size), (234, 238, 222, 255)) # Cream sclera
    draw = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2

    # Equirectangular math on sphere equator:
    # 1 unit V = 180 degrees latitude.
    # 1 unit U = 360 degrees longitude.
    # Therefore, delta_u = delta_v / 2 for equal angular radius!
    # In pixel space: dy = 2.0 * dx for circular appearance on sphere.
    
    # Angular radii in degrees:
    # Limbal ring: 48 deg -> dy = 48/180 * 1024 = 273 px, dx = 136 px
    # Iris: 44 deg -> dy = 44/180 * 1024 = 250 px, dx = 125 px
    # Pupil: 22 deg -> dy = 22/180 * 1024 = 125 px, dx = 62 px
    
    rad_limbal_y = 273
    rad_iris_y = 250
    rad_pupil_y = 125

    # 1. Sclera shading (gentle darkening towards periphery)
    for y in range(size):
        dy = y - cy
        for x in range(size):
            dx = x - cx
            # Metric distance on sphere in vertical pixel units:
            dist_sq = (dx * 2.0)**2 + dy**2
            dist = math.sqrt(dist_sq)
            if dist > rad_limbal_y:
                # Soft vignette at outer edge
                t = min(1.0, (dist - rad_limbal_y) / 200.0)
                r = int(234 - 38 * t)
                g = int(238 - 34 * t)
                b = int(222 - 42 * t)
                img.putpixel((x, y), (r, g, b, 255))

    # 2. Limbal ring & 3. Iris
    for y in range(max(0, cy - rad_limbal_y - 5), min(size, cy + rad_limbal_y + 5)):
        dy = y - cy
        for x in range(max(0, int(cx - (rad_limbal_y / 2.0) - 5)), min(size, int(cx + (rad_limbal_y / 2.0) + 5))):
            dx = x - cx
            dist = math.sqrt((dx * 2.0)**2 + dy**2)
            
            # Limbal ring
            if rad_iris_y < dist <= rad_limbal_y:
                t = (dist - rad_iris_y) / (rad_limbal_y - rad_iris_y)
                lr = int(58 * (1 - t) + 42 * t)
                lg = int(72 * (1 - t) + 52 * t)
                lb = int(28 * (1 - t) + 20 * t)
                img.putpixel((x, y), (lr, lg, lb, 255))
                
            # Iris
            elif rad_pupil_y < dist <= rad_iris_y:
                angle = math.atan2(dy, dx * 2.0)
                t_rad = (dist - rad_pupil_y) / (rad_iris_y - rad_pupil_y)
                
                striation = math.sin(angle * 64.0) * 0.16 + math.sin(angle * 128.0) * 0.08
                amber = math.exp(-((t_rad - 0.50)**2) / 0.05) * 0.35
                
                ir_r = 105 + striation * 22 + amber * 55
                ir_g = 126 + striation * 18 + amber * 45
                ir_b = 52 + striation * 14 + amber * 22
                
                # Darken slightly near pupil
                if t_rad < 0.18:
                    dark = t_rad / 0.18
                    ir_r *= (0.65 + 0.35 * dark)
                    ir_g *= (0.65 + 0.35 * dark)
                    ir_b *= (0.65 + 0.35 * dark)
                    
                img.putpixel((x, y), (int(min(255, max(0, ir_r))),
                                      int(min(255, max(0, ir_g))),
                                      int(min(255, max(0, ir_b))),
                                      255))
                                      
            # Pupil
            elif dist <= rad_pupil_y:
                if dist > rad_pupil_y - 3.0:
                    aa = (rad_pupil_y - dist) / 3.0
                    pr = int(20 * (1 - aa) + 55 * aa)
                    pg = int(24 * (1 - aa) + 65 * aa)
                    pb = int(20 * (1 - aa) + 25 * aa)
                    img.putpixel((x, y), (pr, pg, pb, 255))
                else:
                    img.putpixel((x, y), (20, 24, 20, 255))

    # Catchlights (reflecting at 10 o'clock on sphere)
    # Highlight 1: Primary pill highlight
    hl1_x = int(cx - 24)
    hl1_y = int(cy - 48)
    draw.ellipse([hl1_x - 12, hl1_y - 24, hl1_x + 12, hl1_y + 24], fill=(255, 255, 255, 250))
    
    # Highlight 2: Secondary dot
    hl2_x = int(cx - 16)
    hl2_y = int(cy - 12)
    draw.ellipse([hl2_x - 4, hl2_y - 8, hl2_x + 4, hl2_y + 8], fill=(240, 245, 235, 210))

    out_path = f"{OUT_DIR}/eye_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated geometrically corrected eye texture: {out_path}")

if __name__ == "__main__":
    generate_eye_texture()

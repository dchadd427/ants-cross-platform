"""
Ultra-realistic eye texture generator for Worker Ant (Caste #1)
Matches ref_eye_crop.png:
- Warm eggshell cream sclera with soft ambient occlusion rim
- Dark thin limbal border ring
- Multi-octave organic hazel-olive iris with golden-amber sunburst (no bicycle-wheel spokes)
- Deep velvety charcoal pupil with soft anti-aliased margin
- Crisp curved studio softbox catchlight at 10:30 + secondary dot at 8:00
- Spherical aspect factor (dy = 2.0 * dx) for perfect circular projection on 3D UV sphere
"""

import math
from PIL import Image, ImageDraw, ImageFilter
import random

random.seed(1337)

OUT_DIR = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

def generate_eye():
    size = 1024
    img = Image.new("RGBA", (size, size), (236, 239, 226, 255)) # Warm cream sclera
    draw = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2

    # Equirectangular spherical metrics
    rad_limbal = 270 # Vertical radius in pixels
    rad_iris = 250
    rad_pupil = 130

    # 1. Sclera shading with soft darkening towards periphery
    for y in range(size):
        dy = y - cy
        for x in range(size):
            dx = x - cx
            dist = math.sqrt((dx * 2.0)**2 + dy**2)
            if dist > rad_limbal:
                t = min(1.0, (dist - rad_limbal) / 220.0)
                # Soft vignette at outer edge
                r = int(236 - 36 * t)
                g = int(239 - 32 * t)
                b = int(226 - 40 * t)
                img.putpixel((x, y), (r, g, b, 255))

    # 2. Limbal ring & 3. Iris
    x_min = max(0, int(cx - (rad_limbal / 2.0) - 10))
    x_max = min(size - 1, int(cx + (rad_limbal / 2.0) + 10))
    y_min = max(0, cy - rad_limbal - 10)
    y_max = min(size - 1, cy + rad_limbal + 10)

    for y in range(y_min, y_max + 1):
        dy = y - cy
        for x in range(x_min, x_max + 1):
            dx = x - cx
            dist = math.sqrt((dx * 2.0)**2 + dy**2)
            
            # Limbal ring: dark olive-brown rim
            if rad_iris < dist <= rad_limbal:
                t = (dist - rad_iris) / (rad_limbal - rad_iris)
                lr = int(58 * (1 - t) + 40 * t)
                lg = int(72 * (1 - t) + 50 * t)
                lb = int(28 * (1 - t) + 20 * t)
                img.putpixel((x, y), (lr, lg, lb, 255))
                
            # Iris: Organic hazel-olive with golden-amber sunburst
            elif rad_pupil < dist <= rad_iris:
                angle = math.atan2(dy, dx * 2.0)
                t_rad = (dist - rad_pupil) / (rad_iris - rad_pupil)
                
                # Organic multi-frequency radial noise (fine natural fibers)
                fiber1 = math.sin(angle * 96.0) * 0.08
                fiber2 = math.sin(angle * 192.0 + 1.2) * 0.04
                fiber3 = math.cos(angle * 48.0 - 0.5) * 0.06
                fiber = fiber1 + fiber2 + fiber3
                
                # Golden-amber sunburst ring in mid-inner iris
                amber_bell = math.exp(-((t_rad - 0.40)**2) / 0.06) * 0.45
                
                # Base olive green (#6c8234)
                # Golden amber (#a6aa44)
                ir_r = 108 + fiber * 25 + amber_bell * 60
                ir_g = 130 + fiber * 20 + amber_bell * 45
                ir_b = 52 + fiber * 15 + amber_bell * 18
                
                # Subtle organic stippling
                stipple = (random.random() - 0.5) * 12
                ir_r += stipple
                ir_g += stipple
                ir_b += stipple
                
                # Soft darkening right next to pupil margin
                if t_rad < 0.14:
                    dark = t_rad / 0.14
                    ir_r *= (0.60 + 0.40 * dark)
                    ir_g *= (0.60 + 0.40 * dark)
                    ir_b *= (0.60 + 0.40 * dark)
                    
                img.putpixel((x, y), (int(min(255, max(0, ir_r))),
                                      int(min(255, max(0, ir_g))),
                                      int(min(255, max(0, ir_b))),
                                      255))
                                      
            # Pupil: Deep velvety dark circle (#141614)
            elif dist <= rad_pupil:
                if dist > rad_pupil - 3.0:
                    aa = (rad_pupil - dist) / 3.0
                    pr = int(20 * (1 - aa) + 55 * aa)
                    pg = int(22 * (1 - aa) + 65 * aa)
                    pb = int(20 * (1 - aa) + 25 * aa)
                    img.putpixel((x, y), (pr, pg, pb, 255))
                else:
                    img.putpixel((x, y), (20, 22, 20, 255))

    # 4. Catchlights: Curved studio softbox reflection at 10:30 position
    hl1_x = int(cx - 24)
    hl1_y = int(cy - 48)
    draw.ellipse([hl1_x - 14, hl1_y - 28, hl1_x + 14, hl1_y + 28], fill=(255, 255, 255, 252))
    
    # Secondary subtle reflection dot at 8:00 position
    hl2_x = int(cx - 16)
    hl2_y = int(cy - 10)
    draw.ellipse([hl2_x - 5, hl2_y - 10, hl2_x + 5, hl2_y + 10], fill=(242, 248, 238, 215))

    out_path = f"{OUT_DIR}/eye_pbr.png"
    img.save(out_path, "PNG")
    print(f"Generated ultra-realistic eye texture: {out_path}")

if __name__ == "__main__":
    generate_eye()

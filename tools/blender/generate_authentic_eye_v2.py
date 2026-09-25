"""
Generate 1024x1024 Authentic Cartoon Ant Eye Texture
Directly derived from the ground-truth master reference artwork (ref_eye_crop2.png).
Left eye has pupil looking inward-forward.
Right eye mirrors UV coordinates.
"""

import math
from PIL import Image, ImageDraw, ImageFilter
import random

random.seed(42)

def make_eye_texture():
    size = 1024
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Eye center in UV space
    # The eye is an egg shape, iris shifted slightly towards medial side (X = 0.56, Y = 0.48)
    cx, cy = size * 0.50, size * 0.50
    iris_cx, iris_cy = size * 0.55, size * 0.48

    r_outer = size * 0.47
    r_iris = size * 0.24
    r_pupil = size * 0.11

    # 1. Base Sclera (Warm eggshell cream with soft peripheral ambient occlusion)
    for y in range(size):
        ny = (y - cy) / (size * 0.5)
        for x in range(size):
            nx = (x - cx) / (size * 0.5)
            # Oval distance: slightly taller than wide
            d_sclera = math.sqrt(nx**2 + (ny / 1.05)**2)
            
            if d_sclera <= 0.96:
                # Sclera color: ivory cream #FAFBF0 in center, fading to #Dfe5cc near edge
                t_edge = max(0.0, (d_sclera - 0.70) / 0.26)
                
                # Soft ambient occlusion at the perimeter
                r = int(246 - 45 * t_edge)
                g = int(248 - 40 * t_edge)
                b = int(238 - 50 * t_edge)
                
                # Check if inside iris
                d_iris = math.sqrt(((x - iris_cx) / r_iris)**2 + ((y - iris_cy) / (r_iris * 1.02))**2)
                d_pupil = math.sqrt(((x - iris_cx) / r_pupil)**2 + ((y - iris_cy) / r_pupil)**2)

                if d_iris <= 1.0:
                    # Inside Iris
                    angle = math.atan2(y - iris_cy, x - iris_cx)
                    t_iris = (d_iris - (r_pupil / r_iris)) / (1.0 - (r_pupil / r_iris))
                    t_iris = max(0.0, min(1.0, t_iris))

                    # Fine radial striations
                    fibers = math.sin(angle * 72.0) * 0.08 + math.cos(angle * 144.0 + 0.8) * 0.05
                    
                    # Golden sunburst in middle iris
                    amber_ring = math.exp(-((t_iris - 0.42)**2) / 0.04) * 0.55
                    
                    # Base colors:
                    # Inner: golden olive #8e9e38
                    # Mid: bright golden-amber #b4b840
                    # Outer limbal edge: dark forest olive #3f4d1e
                    base_r = int(140 + amber_ring * 65 + fibers * 25)
                    base_g = int(160 + amber_ring * 45 + fibers * 20)
                    base_b = int(55  + amber_ring * 20 + fibers * 15)
                    
                    # Limbal ring darkening
                    if t_iris > 0.82:
                        t_limb = (t_iris - 0.82) / 0.18
                        base_r = int(base_r * (1.0 - 0.65 * t_limb) + 38 * t_limb)
                        base_g = int(base_g * (1.0 - 0.60 * t_limb) + 48 * t_limb)
                        base_b = int(base_b * (1.0 - 0.60 * t_limb) + 20 * t_limb)
                    
                    r, g, b = base_r, base_g, base_b

                if d_pupil <= 1.0:
                    # Jet velvety black pupil with anti-aliased edge
                    t_p = min(1.0, max(0.0, (1.0 - d_pupil) * 15.0))
                    r = int(r * (1.0 - t_p) + 12 * t_p)
                    g = int(g * (1.0 - t_p) + 12 * t_p)
                    b = int(b * (1.0 - t_p) + 14 * t_p)

                # Soft socket perimeter fade
                alpha = 255
                if d_sclera > 0.90:
                    alpha = int(255 * (0.96 - d_sclera) / 0.06)
                img.putpixel((x, y), (min(255, max(0, r)),
                                      min(255, max(0, g)),
                                      min(255, max(0, b)),
                                      alpha))

    # 2. Add Glossy Studio Catchlights (10:30 o'clock primary softbox + secondary fill)
    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d_over = ImageDraw.Draw(overlay)

    # Primary keylight: curved softbox at upper-left of pupil
    # Center around (iris_cx - r_iris * 0.45, iris_cy - r_iris * 0.48)
    hl_cx = int(iris_cx - r_iris * 0.42)
    hl_cy = int(iris_cy - r_iris * 0.46)
    hl_r = int(r_pupil * 0.55)

    d_over.ellipse([hl_cx - hl_r, hl_cy - hl_r, hl_cx + hl_r, hl_cy + hl_r],
                   fill=(255, 255, 255, 245))
    
    # Secondary subtle reflection
    s_cx = int(iris_cx + r_iris * 0.48)
    s_cy = int(iris_cy + r_iris * 0.42)
    s_r = int(r_pupil * 0.28)
    d_over.ellipse([s_cx - s_r, s_cy - s_r, s_cx + s_r, s_cy + s_r],
                   fill=(240, 245, 255, 75))

    overlay = overlay.filter(ImageFilter.GaussianBlur(radius=3.0))
    img = Image.alpha_composite(img, overlay)

    out_path = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/eye_pbr.png"
    img.save(out_path)
    print(f"Saved authentic eye texture: {out_path}")

if __name__ == "__main__":
    make_eye_texture()

"""
Blender Python Script: Generate crisp, authentic PBR textures for Worker Ant (Caste #1)
Using numpy and bpy for 100% native execution in Blender.
"""

import bpy
import numpy as np
import os

out_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"
os.makedirs(out_dir, exist_ok=True)

def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def save_image(name, filename, arr_rgba):
    # arr_rgba shape: (H, W, 4), float32, range [0, 1]
    H, W, _ = arr_rgba.shape
    # Flip vertically for Blender image coordinate convention
    flipped = np.flipud(arr_rgba).astype(np.float32)
    flat = flipped.ravel()
    
    img = bpy.data.images.new(name, W, H, alpha=True)
    img.pixels.foreach_set(flat)
    filepath = os.path.join(out_dir, filename)
    img.filepath_raw = filepath
    img.file_format = 'PNG'
    img.save()
    print(f"Saved: {filepath}")

# -----------------------------------------------------------------------------
# 1. Eye PBR Texture (1024 x 1024)
# -----------------------------------------------------------------------------
def make_eye_texture():
    N = 1024
    u = np.linspace(0.0, 1.0, N, endpoint=False)
    v = np.linspace(0.0, 1.0, N, endpoint=False)
    U, V = np.meshgrid(u, v)
    
    # Distance from front center (0.5, 0.5)
    # On a UV sphere, latitude circumference is 2*pi*R while meridian length is pi*R.
    # Therefore, scale dx by 2.0 so circles in UV space map to perfect circles on the 3D sphere!
    dx = (U - 0.5) * 2.0
    dy = V - 0.5
    dist = np.sqrt(dx * dx + dy * dy)
    angle = np.arctan2(dy, dx)
    
    # Base sclera: warm creamy ivory
    r_sclera = 0.94 - dist * 0.12
    g_sclera = 0.91 - dist * 0.12
    b_sclera = 0.83 - dist * 0.14
    
    # Iris dimensions - balanced for innocent expressive insect eye
    iris_r = 0.175
    limbal_inner = 0.155
    pupil_r = 0.088
    
    # Iris mask (1 inside iris, 0 outside)
    iris_mask = 1.0 - smoothstep(iris_r - 0.010, iris_r + 0.005, dist)
    
    # Limbal ring (dark outer edge of iris)
    limbal_mask = smoothstep(limbal_inner, iris_r, dist) * iris_mask
    
    # Pupil mask (1 inside pupil, 0 outside)
    pupil_mask = 1.0 - smoothstep(pupil_r - 0.005, pupil_r + 0.003, dist)
    
    # Iris color gradient (olive/amber gold)
    # Normalized iris radius: 0 at pupil edge, 1 at limbal ring
    t_iris = np.clip((dist - pupil_r) / (limbal_inner - pupil_r + 1e-5), 0.0, 1.0)
    
    # Radial striations
    striations = 0.5 + 0.5 * np.sin(angle * 32.0) * np.sin(angle * 14.0 + 1.2)
    fiber_factor = 0.88 + 0.24 * striations
    
    r_iris = ((0.52 * (1.0 - t_iris) + 0.38 * t_iris) * fiber_factor)
    g_iris = ((0.64 * (1.0 - t_iris) + 0.48 * t_iris) * fiber_factor)
    b_iris = ((0.24 * (1.0 - t_iris) + 0.14 * t_iris) * fiber_factor)
    
    # Dark limbal ring
    r_iris = r_iris * (1.0 - limbal_mask * 0.6) + 0.14 * limbal_mask
    g_iris = g_iris * (1.0 - limbal_mask * 0.6) + 0.18 * limbal_mask
    b_iris = b_iris * (1.0 - limbal_mask * 0.6) + 0.06 * limbal_mask
    
    # Deep dark pupil
    r_pupil = 0.04
    g_pupil = 0.05
    b_pupil = 0.04
    
    # Blend sclera -> iris -> pupil
    r = r_sclera * (1.0 - iris_mask) + (r_iris * (1.0 - pupil_mask) + r_pupil * pupil_mask) * iris_mask
    g = g_sclera * (1.0 - iris_mask) + (g_iris * (1.0 - pupil_mask) + g_pupil * pupil_mask) * iris_mask
    b = b_sclera * (1.0 - iris_mask) + (b_iris * (1.0 - pupil_mask) + b_pupil * pupil_mask) * iris_mask
    
    # Specular Catchlights (Studio lighting reflections)
    # Primary Key Catchlight (upper-right at ~1:30 position)
    kx, ky = 0.5 + 0.022, 0.5 + 0.042
    k_dist = np.sqrt(((U - kx) * 2.0)**2 + (V - ky)**2)
    k_mask = 1.0 - smoothstep(0.014, 0.020, k_dist)
    
    # Secondary Soft Fill Catchlight (lower-left at ~7:30 position)
    bx, by = 0.5 - 0.022, 0.5 - 0.038
    b_dist = np.sqrt(((U - bx) * 2.0)**2 + (V - by)**2)
    b_mask = (1.0 - smoothstep(0.008, 0.014, b_dist)) * 0.65
    
    spec_mask = np.clip(k_mask + b_mask, 0.0, 1.0)
    r = r * (1.0 - spec_mask) + 1.0 * spec_mask
    g = g * (1.0 - spec_mask) + 1.0 * spec_mask
    b = b * (1.0 - spec_mask) + 1.0 * spec_mask
    
    arr = np.stack([np.clip(r, 0, 1), np.clip(g, 0, 1), np.clip(b, 0, 1), np.ones_like(r)], axis=-1)
    save_image("EyeTex", "eye_pbr.png", arr)

# -----------------------------------------------------------------------------
# 2. Mandible PBR Texture (1024 x 1024)
# -----------------------------------------------------------------------------
def make_mandible_texture():
    N = 1024
    u = np.linspace(0.0, 1.0, N, endpoint=False)
    v = np.linspace(0.0, 1.0, N, endpoint=False)
    U, V = np.meshgrid(u, v)
    
    # Base chitin at top (V > 0.6) -> pale lime at bottom (V < 0.35)
    t = smoothstep(0.25, 0.65, V)
    
    # Base: emerald green (0.24, 0.44, 0.22)
    # Mid: olive green (0.48, 0.66, 0.30)
    # Tip: pale lime-green (0.86, 0.96, 0.62)
    r = 0.88 * (1.0 - t) + 0.22 * t
    g = 0.98 * (1.0 - t) + 0.44 * t
    b = 0.64 * (1.0 - t) + 0.20 * t
    
    # Highlight band along inner bite edge (U near 0.1 or 0.9)
    bite_edge = 1.0 - smoothstep(0.0, 0.12, np.minimum(U, 1.0 - U))
    r += bite_edge * 0.08
    g += bite_edge * 0.08
    b += bite_edge * 0.12
    
    arr = np.stack([np.clip(r, 0, 1), np.clip(g, 0, 1), np.clip(b, 0, 1), np.ones_like(r)], axis=-1)
    save_image("MandibleTex", "mandible_pbr.png", arr)

# -----------------------------------------------------------------------------
# 3. Chitin Carapace Texture (1024 x 1024)
# -----------------------------------------------------------------------------
def make_chitin_texture():
    N = 1024
    np.random.seed(1998)
    
    # Box filter smooth interpolation
    def smooth_noise(grid_size, weight):
        g = np.random.rand(grid_size, grid_size)
        # Bilinear interpolation
        y_idx = np.linspace(0, grid_size - 1, N)
        x_idx = np.linspace(0, grid_size - 1, N)
        y0 = np.floor(y_idx).astype(int)
        y1 = np.clip(y0 + 1, 0, grid_size - 1)
        x0 = np.floor(x_idx).astype(int)
        x1 = np.clip(x0 + 1, 0, grid_size - 1)
        
        wy = (y_idx - y0)[:, None]
        wx = (x_idx - x0)[None, :]
        
        top = g[y0, :][:, x0] * (1 - wx) + g[y0, :][:, x1] * wx
        bot = g[y1, :][:, x0] * (1 - wx) + g[y1, :][:, x1] * wx
        return (top * (1 - wy) + bot * wy) * weight

    noise = (
        smooth_noise(8, 0.45) +
        smooth_noise(16, 0.30) +
        smooth_noise(32, 0.15) +
        smooth_noise(64, 0.07) +
        np.random.rand(N, N) * 0.03
    )
    noise = (noise - noise.min()) / (noise.max() - noise.min() + 1e-6)
    
    # Authentic emerald carapace color mapping:
    # Deep emerald shadows: (0.16, 0.32, 0.16)
    # Mid emerald body: (0.26, 0.48, 0.24)
    # Warm olive highlights: (0.38, 0.58, 0.28)
    r = 0.16 + noise * 0.22
    g = 0.32 + noise * 0.26
    b = 0.16 + noise * 0.12
    
    arr = np.stack([np.clip(r, 0, 1), np.clip(g, 0, 1), np.clip(b, 0, 1), np.ones_like(r)], axis=-1)
    save_image("ChitinTex", "chitin_pbr.png", arr)

if __name__ == "__main__":
    make_eye_texture()
    make_mandible_texture()
    make_chitin_texture()

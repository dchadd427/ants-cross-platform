"""Quiet seamless textures for the plates (periodic by construction: filtered noise on a torus; tiles at 3 device pixels per canvas pixel).
veins: soft leaf-vein EMBOSSING at low contrast (ridges blurred over about 3 canvas pixels, lit from the top left); no thin bright lines.
speck: very fine mottle."""
import os
import numpy as np
from PIL import Image
import common

K = 3

def _fft_noise(w, h, seed, theta_deg, f_across, s_along, s_across):
    rng = np.random.default_rng(seed)
    n = rng.standard_normal((h, w))
    F = np.fft.fft2(n)
    fy = np.fft.fftfreq(h)[:, None] * K
    fx = np.fft.fftfreq(w)[None, :] * K
    t = np.deg2rad(theta_deg)
    a = fx * np.cos(t) + fy * np.sin(t)
    c = -fx * np.sin(t) + fy * np.cos(t)
    G = np.exp(-(a / s_along) ** 2) * np.exp(-((np.abs(c) - f_across) / s_across) ** 2)
    out = np.real(np.fft.ifft2(F * G))
    return out / out.std()

def _blur(a, sigma):
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.fftfreq(w)[None, :]
    G = np.exp(-2 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(a) * G))

def veins(tw=192, th=128, seed=11, theta=-58, spacing=9.0, amp=34, blur=1.5):
    """soft embossed veins: returns a file URL of an RGBA tile (light where the surface faces the top left, dark where it faces away), alpha at most `amp` / 255"""
    w, h = tw * K, th * K
    name = 'tex2_veins_%d_%d_%s_%d_%s.png' % (seed, theta, str(spacing).replace('.', '_'), amp, str(blur).replace('.', '_'))
    if not os.path.exists(common.scratch() + name):
        L = np.zeros((h, w))
        for i, (dth, sd, wt) in enumerate(((0, seed, 1.0), (34, seed + 5, .55), (-34, seed + 9, .55))):
            n = _fft_noise(w, h, sd, theta + dth, 1.0 / (2 * spacing * (1 + .3 * i)), .022, .05)
            L = np.maximum(L, np.exp(-(n / .22) ** 2) * wt)
        Hh = _blur(L, blur * K)
        gy, gx = np.gradient(Hh)
        gx = (np.roll(Hh, -1, 1) - np.roll(Hh, 1, 1)) / 2; gy = (np.roll(Hh, -1, 0) - np.roll(Hh, 1, 0)) / 2
        s = -(gx + gy) / np.sqrt(2)                       # light from the top left: the slope facing the light is bright
        s = s / np.abs(s).max()
        rgba = np.zeros((h, w, 4), np.uint8)
        pos = s > 0
        rgba[..., 0] = np.where(pos, 190, 4); rgba[..., 1] = np.where(pos, 245, 36); rgba[..., 2] = np.where(pos, 220, 24)
        rgba[..., 3] = np.clip(np.abs(s) * amp * 2.2, 0, amp).astype(np.uint8)
        Image.fromarray(rgba, 'RGBA').save(common.scratch() + name)
    return 'file://' + common.scratch() + name

def speck(tw=64, th=64, seed=5, amp=12):
    w, h = tw * K, th * K
    name = 'tex2_speck_%d_%d.png' % (seed, amp)
    if not os.path.exists(common.scratch() + name):
        rng = np.random.default_rng(seed)
        n = _blur(rng.standard_normal((h, w)), 1.6)
        n /= n.std()
        rgba = np.zeros((h, w, 4), np.uint8)
        rgba[..., :3] = np.where(n[..., None] > 0, (210, 255, 235), (3, 30, 18))
        rgba[..., 3] = np.clip(np.abs(n) * amp * .5, 0, amp).astype(np.uint8)
        Image.fromarray(rgba, 'RGBA').save(common.scratch() + name)
    return 'file://' + common.scratch() + name

#!/usr/bin/env python3
"""Deterministic 44.1 kHz 16-bit DSP audio remasterer for Ants (1998).

Remasters all 91 original 8-bit (11,025 / 22,050 Hz) WAV sound effects from
`asset_catalog/sounds/` into clean 44,100 Hz 16-bit PCM WAVs inside the
gitignored `tools/ant_models/models/audio/remastered/` folder. Preserves the
unmistakable timing, cadence, and personality of the 1998 originals while
upgrading sonic clarity, warmth, and giving each ant class a distinct fresh
vocal-tract timbre via WSOLA pitch shift + cepstral formant warping.

Also stages the classic originals under `models/audio/classic/` and optional
backyard MP3 tracks from `Original-Ants/` under `models/audio/music/`, writing
`tools/ant_models/models/audio/manifest.json` for live A/B comparison.

Pure Python + NumPy (no SciPy required). Metadata and classification helpers
import cleanly without NumPy for CI environments.

Usage:
  python3 tools/ant_models/remaster_audio.py
  python3 tools/ant_models/remaster_audio.py --report
  python3 tools/ant_models/remaster_audio.py --only 0,1,4,14
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import math
import os
import re
import shutil
import sys
import wave
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import numpy as np

    HAS_NUMPY = True
except ImportError:
    np = None  # type: ignore
    HAS_NUMPY = False

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
DEFAULT_SRC_DIR = os.path.join(REPO_ROOT, "asset_catalog", "sounds")
DEFAULT_MUSIC_DIR = os.path.join(REPO_ROOT, "Original-Ants")
DEFAULT_OUT_DIR = os.path.join(THIS_DIR, "models", "audio")

TARGET_SAMPLE_RATE = 44100
TARGET_BITS = 16
EXPECTED_SOUND_COUNT = 91
MAX_SAFE_PEAK = 0.94

MUSIC_TRACK_NAMES: Tuple[str, ...] = ("ANTS2A.mp3", "ANTS2B.mp3", "ANTSFUN3.mp3")
SOUND_FILE_RE = re.compile(r"^sound_(\d{3})_(.+)\.wav$", re.IGNORECASE)

CATEGORY_LABELS: Dict[str, str] = {
    "voice_ant": "Ant Voices",
    "voice_announcer": "Announcer & Alerts",
    "explosion": "Explosions & Bombs",
    "fire": "Fire & Flame",
    "water_earth": "Water & Digging",
    "combat_action": "Combat & Actions",
    "chime_pickup": "Pickups, Chimes & Fanfares",
    "ui": "UI & Chat",
}

# Explicit sonic class mapping covering all 91 original SoundIDs (0..90).
_CATEGORY_GROUPS: Dict[str, Tuple[int, ...]] = {
    "ui": (0, 45, 46, 89),
    "chime_pickup": (1, 2, 6, 40, 42, 43, 44, 48, 54, 55, 56, 61, 63, 87, 88),
    "explosion": (4, 73, 74),
    "fire": (5, 67, 68, 69),
    "voice_ant": (*range(7, 40), 58, 59, 60),
    "voice_announcer": (41, 49, 50, 51, 52, 53),
    "water_earth": (62, 71, 72, 79, 80, 81, 82),
    "combat_action": (3, 47, 57, 64, 65, 66, 70, 75, 76, 77, 78, 83, 84, 85, 86, 90),
}
SOUND_CATEGORIES: Dict[int, str] = {
    sid: cat for cat, ids in _CATEGORY_GROUPS.items() for sid in ids
}

# Playback rules: +/-3% pitch micro-variation ONLY on repeatable Foley/combat/water/fire.
PITCH_VARIATION_CATEGORIES = frozenset({"combat_action", "water_earth", "fire"})
PITCH_VARIATION_EXTRA_IDS = frozenset({73, 74})
PITCH_VARIATION_EXCLUDED_IDS = frozenset({67, 70})
NON_SPATIAL_CATEGORIES = frozenset({"ui", "voice_announcer"})
NON_SPATIAL_IDS = frozenset({6, 42, 44, 48, 54, 55, 56, 58, 61, 63, 87, 88})
HIGH_PRIORITY_IDS = frozenset({4, 42, 48, 56, 58})

# Per-sound voice re-characterization profiles: (role, semitones, target_formant, note).
VOICE_PROFILES: Dict[int, Tuple[str, float, float, str]] = {
    # Random colony chatter (distinct takes per clip; separates 1998 rndm1/rndm6 pair)
    7: ("rndm", -1.35, 0.95, "Warm deeper chatter"),
    8: ("rndm", +1.35, 1.05, "Bright peppy chatter"),
    9: ("rndm", -0.65, 0.97, "Mellow casual chatter"),
    10: ("rndm", +0.80, 1.03, "Nimble bright chatter"),
    11: ("rndm", -1.05, 0.96, "Playful low-mid chatter"),
    12: ("rndm", +1.10, 1.04, "Cheerful colony chatter"),
    # Worker (gant*): brighter / peppier (+1.15..+1.40 st, bright formant lift)
    13: ("worker", +1.20, 1.05, "Brighter peppy worker"),
    14: ("worker", +1.35, 1.06, "Brighter peppy worker"),
    15: ("worker", +1.15, 1.05, "Brighter peppy worker"),
    16: ("worker", +1.40, 1.06, "Brighter peppy worker"),
    17: ("worker", +1.25, 1.05, "Brighter peppy worker"),
    # Thief (theif*): slinkier / sneakier (-1.0 st, tight intimate whisper presence)
    18: ("thief", -1.00, 0.98, "Slinky intimate whisper"),
    19: ("thief", -0.95, 0.98, "Slinky intimate whisper"),
    20: ("thief", -1.05, 0.97, "Slinky intimate whisper"),
    21: ("thief", -1.00, 0.98, "Slinky intimate whisper"),
    # Fire (fire*): slightly brisker / warmer (+0.8 st, subtle warm analog grit)
    22: ("fire", +0.80, 1.03, "Brisk warm analog grit"),
    23: ("fire", +0.75, 1.03, "Brisk warm analog grit"),
    24: ("fire", +0.85, 1.04, "Brisk warm analog grit"),
    25: ("fire", +0.80, 1.03, "Brisk warm analog grit"),
    # Combat (comb*, combat1, combat2): deeper / gruffer (-1.65..-2.05 st, chest resonance)
    26: ("combat", -1.85, 0.93, "Deeper gruff chest tone"),
    27: ("combat", -1.75, 0.93, "Deeper gruff chest tone"),
    28: ("combat", -1.95, 0.92, "Deeper gruff chest tone"),
    29: ("combat", -1.65, 0.94, "Deeper gruff chest tone"),
    30: ("combat", -2.00, 0.92, "Deeper gruff chest tone"),
    31: ("combat", -1.80, 0.93, "Deeper gruff chest tone"),
    59: ("combat", -1.90, 0.93, "Deeper gruff battle cry"),
    60: ("combat", -2.05, 0.92, "Deeper gruff battle cry"),
    # Swimmer / Bridge (brdg*): subtle dive-mask / snorkel acoustic warmth (-0.5 st)
    32: ("swimmer", -0.45, 0.97, "Snorkel mask warmth"),
    33: ("swimmer", -0.65, 0.96, "Snorkel mask warmth (alt)"),
    34: ("swimmer", -0.50, 0.97, "Snorkel mask warmth"),
    35: ("swimmer", -0.55, 0.97, "Snorkel mask warmth"),
    # Bomber (bomb*): quirkier / higher energy (+1.5 st, crisp upper-mid edge)
    36: ("bomber", +1.50, 1.07, "Quirky crisp high energy"),
    37: ("bomber", +1.45, 1.06, "Quirky crisp high energy"),
    38: ("bomber", +1.60, 1.07, "Quirky crisp high energy"),
    39: ("bomber", +1.50, 1.07, "Quirky crisp high energy"),
    # Under-attack vocal alert
    58: ("worker", +0.65, 1.02, "Urgent colony warning"),
}


def voice_profile_for(sound_id: int) -> Optional[Dict[str, Any]]:
    """Return structured voice re-characterization spec for ant voices, or None."""
    spec = VOICE_PROFILES.get(sound_id)
    if spec is None:
        return None
    role, semitones, formant, note = spec
    return {"role": role, "semitones": semitones, "formant": formant, "note": note}


def parse_sound_filename(filename: str) -> Optional[Tuple[int, str]]:
    """Return `(sound_id, short_name)` from `sound_NNN_name.wav`, or None."""
    m = SOUND_FILE_RE.match(os.path.basename(filename))
    return (int(m.group(1)), m.group(2)) if m else None


def classify_sound(sound_id: int, name: str = "") -> str:
    """Return the canonical sonic class for `sound_id`."""
    if sound_id in SOUND_CATEGORIES:
        return SOUND_CATEGORIES[sound_id]
    low = name.lower()
    if low in ("bombexp", "bombdrop", "bombmuffle"):
        return "explosion"
    if low in ("fireburnout", "firestarta", "firestartb", "fireextinguish"):
        return "fire"
    if any(low.startswith(p) for p in ("gant", "theif", "fire", "comb", "brdg", "bomb", "rndm")):
        return "combat_action" if low in ("theifwhip", "bombpick") else "voice_ant"
    if any(k in low for k in ("splash", "water", "drown", "shovel")):
        return "water_earth"
    if "click" in low or "chat" in low:
        return "ui"
    if any(k in low for k in ("powerup", "score", "winner")):
        return "chime_pickup"
    return "combat_action"


def playback_metadata(sound_id: int, category: str) -> Dict[str, Any]:
    """Return runtime Web Audio playback hints for a sound."""
    allow_pitch = (
        (category in PITCH_VARIATION_CATEGORIES or sound_id in PITCH_VARIATION_EXTRA_IDS)
        and sound_id not in PITCH_VARIATION_EXCLUDED_IDS
    )
    non_spatial = category in NON_SPATIAL_CATEGORIES or sound_id in NON_SPATIAL_IDS
    if sound_id in HIGH_PRIORITY_IDS:
        priority = 4
    elif category in ("voice_ant", "voice_announcer", "chime_pickup", "explosion"):
        priority = 3
    elif category in ("fire", "ui"):
        priority = 2
    else:
        priority = 1
    return {"pitch_jitter": 0.03 if allow_pitch else 0.0, "non_spatial": non_spatial, "priority": priority}


def discover_source_wavs(src_dir: str = DEFAULT_SRC_DIR) -> List[Tuple[int, str, str]]:
    """Return sorted list of `(sound_id, name, abs_path)` for all source WAVs."""
    found: List[Tuple[int, str, str]] = []
    for path in sorted(glob.glob(os.path.join(src_dir, "sound_*.wav"))):
        parsed = parse_sound_filename(path)
        if parsed is not None:
            found.append((parsed[0], parsed[1], os.path.abspath(path)))
    found.sort(key=lambda item: item[0])
    return found


def _require_numpy() -> None:
    if not HAS_NUMPY:
        raise RuntimeError("NumPy is required to run DSP audio remastering (pip install numpy)")


# ---------------------------------------------------------------------------
# WAV I/O
# ---------------------------------------------------------------------------

def read_wav_float(path: str) -> Tuple[Any, int, int]:
    """Read PCM WAV into float64 array shaped `(channels, frames)` in [-1, 1]."""
    _require_numpy()
    with wave.open(path, "rb") as w:
        sr, n_ch, sw, n_frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n_frames)
    if sw == 1:
        data = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif sw == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"Unsupported WAV sample width {sw} bytes in {path}")
    data = data.reshape(-1, n_ch).T.copy() if n_ch > 1 else data.reshape(1, -1).copy()
    return data, sr, sw * 8


def write_wav_i16(path: str, audio_2d: Any, sample_rate: int, seed: int) -> Dict[str, float]:
    """Write `(channels, frames)` float64 array to 16-bit signed PCM WAV with TPDF dither."""
    _require_numpy()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    x = np.asarray(audio_2d, dtype=np.float64)
    if x.ndim == 1:
        x = x.reshape(1, -1)
    n_ch, n_frames = x.shape

    x = np.clip(x, -MAX_SAFE_PEAK, MAX_SAFE_PEAK)
    rng = np.random.default_rng(seed)
    dither = (rng.random(x.shape) - rng.random(x.shape)) / 32768.0
    x_dith = np.clip(np.where(np.abs(x) > (1.0 / 65536.0), x + dither, 0.0), -0.955, 0.955)
    pcm = np.rint(x_dith * 32767.0).astype("<i2")
    interleaved = pcm.T.reshape(-1) if n_ch > 1 else pcm.reshape(-1)

    with wave.open(path, "wb") as w:
        w.setnchannels(n_ch)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(interleaved.tobytes())

    mono = x.mean(axis=0)
    return {
        "peak": float(np.max(np.abs(x))) if n_frames else 0.0,
        "rms": float(np.sqrt(np.mean(mono * mono))) if n_frames else 0.0,
        "dc": float(np.mean(mono)) if n_frames else 0.0,
        "duration_ms": 1000.0 * n_frames / float(sample_rate),
    }


def _file_sha256_short(path: str, length: int = 12) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:length]


# ---------------------------------------------------------------------------
# Pure-NumPy DSP primitives
# ---------------------------------------------------------------------------

def _next_pow2(n: int) -> int:
    return 1 << (max(1, n) - 1).bit_length()


def _db_to_lin(db: float) -> float:
    return 10.0 ** (db / 20.0)


def _hp(freqs: Any, fc: float, order: int = 2) -> Any:
    resp = 1.0 / np.sqrt(1.0 + (fc / np.maximum(freqs, 1e-6)) ** (2 * order))
    resp[0] = 0.0
    return resp


def _lp(freqs: Any, fc: float, order: int = 2) -> Any:
    return 1.0 / np.sqrt(1.0 + (np.maximum(freqs, 1e-6) / fc) ** (2 * order))


def _bell(freqs: Any, fc: float, gain_db: float, bw_oct: float = 1.0) -> Any:
    if abs(gain_db) < 1e-4:
        return np.ones_like(freqs)
    d = np.log2(np.maximum(freqs, 1.0) / fc) / max(0.15, bw_oct)
    return 10.0 ** ((gain_db / 20.0) * np.exp(-0.5 * d * d))


def _shelf(freqs: Any, fc: float, gain_db: float, high: bool = True, slope: float = 0.75) -> Any:
    if abs(gain_db) < 1e-4:
        return np.ones_like(freqs)
    z = np.log2(np.maximum(freqs, 1.0) / fc) / max(0.15, slope)
    w = 1.0 / (1.0 + np.exp(-np.clip(z if high else -z, -20.0, 20.0)))
    return 10.0 ** ((gain_db / 20.0) * w)


def _zero_phase_filter(x: Any, sr: int, curve_fn: Callable[[Any], Any]) -> Any:
    """Linear-padded zero-phase frequency-domain filter (no phase distortion)."""
    n = len(x)
    if n == 0:
        return x.copy()
    pad = min(2048, max(256, n // 2))
    n_fft = _next_pow2(n + 2 * pad)
    buf = np.zeros(n_fft, dtype=np.float64)
    buf[pad : pad + n] = x
    freqs = np.fft.rfftfreq(n_fft, d=1.0 / sr)
    return np.fft.irfft(np.fft.rfft(buf) * curve_fn(freqs), n=n_fft)[pad : pad + n].copy()


def _fft_resample_length(x: Any, target_len: int) -> Any:
    """Zero-phase band-limited FFT resampling with cosine Nyquist anti-ringing taper."""
    n_in = len(x)
    if n_in == 0 or target_len <= 0:
        return np.zeros(max(0, target_len), dtype=np.float64)
    if n_in == target_len:
        return x.copy()

    pad = min(max(32, n_in // 6), 384, max(1, n_in - 1))
    if pad > 1 and n_in > 2:
        left = x[pad:0:-1] * np.linspace(0.0, 1.0, pad)
        right = x[-2 : -(pad + 2) : -1] * np.linspace(1.0, 0.0, pad)
        xp = np.concatenate([left, x, right])
    else:
        pad = 0
        xp = x

    n_pad = len(xp)
    scale = target_len / float(n_in)
    n_pad_out = max(2, int(round(n_pad * scale)))
    pad_out = int(round(pad * scale))

    spec_in = np.fft.rfft(xp)
    copy_bins = min(len(spec_in), n_pad_out // 2 + 1)
    taper = np.ones(copy_bins, dtype=np.float64)
    t_start = int(0.93 * copy_bins)
    if copy_bins - t_start >= 2:
        taper[t_start:] = 0.5 * (1.0 + np.cos(np.pi * np.linspace(0.0, 1.0, copy_bins - t_start)))

    spec_out = np.zeros(n_pad_out // 2 + 1, dtype=np.complex128)
    spec_out[:copy_bins] = spec_in[:copy_bins] * taper
    y_pad = np.fft.irfft(spec_out, n=n_pad_out) * (n_pad_out / float(n_pad))
    y = y_pad[pad_out : pad_out + target_len]
    return np.pad(y, (0, max(0, target_len - len(y))))[:target_len].copy()


def _bandlimited_upsample(x: Any, orig_sr: int, target_sr: int = TARGET_SAMPLE_RATE) -> Any:
    x0 = x - float(np.mean(x)) if len(x) else x.copy()
    if orig_sr == target_sr or len(x0) == 0:
        return x0
    return _fft_resample_length(x0, int(round(len(x0) * target_sr / float(orig_sr))))


def _stft_denoise(
    x: Any, sr: int, orig_sr: int, strength: float = 1.10, floor_gain: float = 0.22, cap_mult: float = 2.6
) -> Any:
    """Musical-noise-free STFT Wiener de-hisser capped at theoretical 8-bit quantization floor."""
    n = len(x)
    if n < 256 or strength <= 0.0:
        return x.copy()

    win_len, hop = 1024, 256
    win = np.hanning(win_len)
    pad_l, pad_r = win_len, win_len + hop
    xp = np.pad(x, (pad_l, pad_r), mode="constant")
    n_frames = 1 + (len(xp) - win_len) // hop
    idx = np.arange(win_len)[None, :] + (np.arange(n_frames) * hop)[:, None]
    spec = np.fft.rfft(xp[idx] * win[None, :], axis=1)
    power = np.abs(spec) ** 2

    freqs = np.fft.rfftfreq(win_len, d=1.0 / sr)
    active_bins = max(16.0, float(np.sum(freqs <= 0.5 * orig_sr)))
    q_rms = (1.0 / 128.0) / math.sqrt(12.0)
    q_bin_p = (q_rms * q_rms) * float(np.sum(win * win)) * (win_len / (2.0 * active_bins))

    f0 = max(0, (pad_l - win_len // 2) // hop)
    f1 = min(n_frames, (pad_l + n + win_len // 2) // hop)
    est = np.percentile(power[f0:f1] if f1 > f0 + 2 else power, 15, axis=0)
    band_w = np.where(freqs > 1800.0, 1.35, np.where(freqs < 250.0, 0.45, 0.85))
    noise_p = np.minimum(est, q_bin_p * (cap_mult * cap_mult) * band_w)
    noise_p = np.maximum(noise_p, np.minimum(est, q_bin_p * 0.45 * np.where(freqs > 2400.0, 1.0, 0.25)))

    gain = np.clip((power - strength * noise_p[None, :]) / (power + 1e-12), floor_gain, 1.0)
    if gain.shape[0] >= 3:
        g_t = gain.copy()
        g_t[1:-1] = 0.25 * gain[:-2] + 0.50 * gain[1:-1] + 0.25 * gain[2:]
        gain = g_t
    if gain.shape[1] >= 3:
        g_f = gain.copy()
        g_f[:, 1:-1] = 0.22 * gain[:, :-2] + 0.56 * gain[:, 1:-1] + 0.22 * gain[:, 2:]
        gain = g_f

    clean = np.fft.irfft(spec * gain, n=win_len, axis=1) * win[None, :]
    out = np.zeros(len(xp), dtype=np.float64)
    norm = np.zeros(len(xp), dtype=np.float64)
    win_sq = win * win
    for i in range(n_frames):
        s = i * hop
        out[s : s + win_len] += clean[i]
        norm[s : s + win_len] += win_sq
    return (out / np.maximum(norm, 1e-9))[pad_l : pad_l + n].copy()


def _smooth_envelope(x: Any, sr: int, attack_ms: float = 5.0, release_ms: float = 60.0) -> Any:
    """Vectorized 2 ms-block attack/release RMS envelope follower."""
    n = len(x)
    if n == 0:
        return x.copy()
    hop = max(1, int(round(sr * 0.002)))
    n_blk = max(1, (n + hop - 1) // hop)
    blk_rms = np.sqrt(np.mean(np.pad(x * x, (0, n_blk * hop - n)).reshape(n_blk, hop), axis=1) + 1e-15)
    dt = hop / float(sr)
    a_att = 1.0 - math.exp(-dt / max(0.0005, attack_ms * 0.001))
    a_rel = 1.0 - math.exp(-dt / max(0.001, release_ms * 0.001))
    env = np.empty(n_blk, dtype=np.float64)
    cur = float(blk_rms[0])
    for i in range(n_blk):
        v = float(blk_rms[i])
        cur += (a_att if v > cur else a_rel) * (v - cur)
        env[i] = cur
    pos = (np.arange(n, dtype=np.float64) + 0.5) / float(hop) - 0.5
    return np.interp(pos, np.arange(n_blk, dtype=np.float64), env)


def _pink_band_noise(n: int, sr: int, lo_hz: float, hi_hz: float, rng: Any) -> Any:
    """Unit-RMS pink-sloped (-3 dB/oct) band-limited texture noise (warm, never harsh white)."""
    raw = rng.standard_normal(n)
    y = _zero_phase_filter(
        raw,
        sr,
        lambda f: _hp(f, lo_hz, 2) * _lp(f, hi_hz, 2) / np.sqrt(np.maximum(f / max(lo_hz, 100.0), 0.5)),
    )
    return y / (float(np.sqrt(np.mean(y * y))) + 1e-9)


def _harmonic_exciter(
    x: Any, sr: int, src_lo: float, src_hi: float, out_lo: float, out_hi: float,
    mix: float, even_ratio: float = 0.55, drive: float = 2.2,
) -> Any:
    """Envelope-gated phase-coherent harmonic sheen (never excites noise floor)."""
    if mix <= 0.0 or len(x) < 64:
        return np.zeros_like(x)
    band = _zero_phase_filter(x, sr, lambda f: _hp(f, src_lo, 2) * _lp(f, src_hi, 2))
    b_rms = float(np.sqrt(np.mean(band * band)))
    if b_rms < 1e-5:
        return np.zeros_like(x)
    nb = band / (b_rms * 2.5 + 1e-9)
    even = nb * nb - float(np.mean(nb * nb))
    odd = np.tanh(drive * nb) - nb
    harm = _zero_phase_filter(even_ratio * even + (1.0 - even_ratio) * odd, sr, lambda f: _hp(f, out_lo, 2) * _lp(f, out_hi, 2))
    h_rms = float(np.sqrt(np.mean(harm * harm)))
    if h_rms < 1e-9:
        return np.zeros_like(x)
    env = _smooth_envelope(band, sr, attack_ms=3.0, release_ms=35.0)
    gate = np.clip((env / (float(np.max(env)) + 1e-9) - 0.06) / 0.18, 0.0, 1.0)
    return harm * ((b_rms / h_rms) * mix * gate)


def _soft_compress(
    x: Any, sr: int, thresh_db: float = -16.0, ratio: float = 2.2,
    knee_db: float = 6.0, attack_ms: float = 6.0, release_ms: float = 65.0,
) -> Any:
    env_db = 20.0 * np.log10(np.maximum(_smooth_envelope(x, sr, attack_ms, release_ms), 1e-6))
    over = env_db - thresh_db
    half_k = 0.5 * knee_db
    gr_db = np.zeros_like(env_db)
    above = over > half_k
    in_k = (over >= -half_k) & (~above)
    gr_db[above] = (1.0 / ratio - 1.0) * over[above]
    if np.any(in_k):
        w = over[in_k] + half_k
        gr_db[in_k] = (1.0 / ratio - 1.0) * (w * w) / (2.0 * knee_db)
    return x * (10.0 ** (gr_db / 20.0))


def _soft_limit(x: Any, knee: float = 0.82, ceiling: float = 0.92) -> Any:
    ax = np.abs(x)
    head = max(0.02, ceiling - knee)
    return np.where(ax <= knee, x, np.sign(x) * (knee + head * np.tanh((ax - knee) / head)))


def _apply_fades(x: Any, sr: int, fade_in_ms: float = 3.0, fade_out_ms: float = 6.0) -> Any:
    n = len(x)
    if n < 8:
        return x.copy()
    y = x.copy()
    fi = min(int(round(sr * fade_in_ms * 0.001)), n // 6)
    fo = min(int(round(sr * fade_out_ms * 0.001)), n // 4)
    if fi >= 2:
        y[:fi] *= 0.5 * (1.0 - np.cos(np.pi * np.linspace(0.0, 1.0, fi)))
    if fo >= 2:
        y[-fo:] *= 0.5 * (1.0 + np.cos(np.pi * np.linspace(0.0, 1.0, fo)))
    y[0] = 0.0
    y[-1] = 0.0
    return y


def _gentle_silence_gate(x: Any, sr: int, rel_thresh: float = 0.045, floor_gain: float = 0.14) -> Any:
    env = _smooth_envelope(x, sr, attack_ms=4.0, release_ms=70.0)
    pk = float(np.max(env))
    if pk < 1e-6:
        return x.copy()
    lo = 0.35 * rel_thresh
    u = np.clip((env / pk - lo) / max(1e-6, rel_thresh - lo), 0.0, 1.0)
    return x * (floor_gain + (1.0 - floor_gain) * u * u * (3.0 - 2.0 * u))


def _level_and_limit(
    x: Any, target_rms: float, max_gain_db: float = 9.0, min_gain_db: float = -8.0,
    knee: float = 0.82, ceiling: float = 0.92,
) -> Any:
    rms = float(np.sqrt(np.mean(x * x)))
    if rms > 1e-6:
        x = x * max(_db_to_lin(min_gain_db), min(_db_to_lin(max_gain_db), target_rms / rms))
    return _soft_limit(x, knee=knee, ceiling=ceiling)


def _early_gloss_tap(x: Any, sr: int, delay_ms: float = 16.0, mix: float = 0.08) -> Any:
    d = int(round(sr * delay_ms * 0.001))
    if d <= 0 or d >= len(x) or mix <= 0.0:
        return x
    bright = _zero_phase_filter(x, sr, lambda f: _hp(f, 850.0, 2) * _lp(f, 9500.0, 2))
    out = x.copy()
    out[d:] += mix * bright[:-d]
    return out


# ---------------------------------------------------------------------------
# Voice re-characterization: WSOLA pitch shift + cepstral formant warping
# ---------------------------------------------------------------------------

def _wsola_time_stretch(x: Any, sr: int, stretch_ratio: float) -> Any:
    """Waveform-Similarity Overlap-Add time stretch (crisp natural speech, zero metallic phasiness)."""
    n = len(x)
    n_out = max(1, int(round(n * stretch_ratio)))
    if n < 1024 or abs(stretch_ratio - 1.0) < 1e-4:
        return x.copy()

    win_len = 960  # ~21.8 ms periodic Hann (exact 50% COLA)
    hs = win_len // 2
    delta = int(round(0.008 * sr))
    win = 0.5 * (1.0 - np.cos(2.0 * np.pi * np.arange(win_len, dtype=np.float64) / float(win_len)))

    xp = np.pad(x, (0, win_len + delta + hs + 4), mode="constant")
    max_start = max(0, n - win_len)
    n_frames = max(1, int(math.ceil((n_out + win_len) / float(hs))))
    out = np.zeros(n_frames * hs + win_len, dtype=np.float64)
    norm = np.zeros_like(out)
    csum_sq = np.concatenate([[0.0], np.cumsum(xp * xp)])
    prev_pos = 0

    for k in range(n_frames):
        a_k = int(round(k * hs / stretch_ratio))
        if k == 0:
            best = 0
        else:
            nat = min(prev_pos + hs, max(0, len(xp) - hs - 1))
            ref = xp[nat : nat + hs]
            ref_e = float(np.sqrt(np.dot(ref, ref)))
            lo, hi = max(0, a_k - delta), min(max_start, a_k + delta)
            if ref_e < 1e-5 or hi <= lo:
                best = max(0, min(max_start, a_k))
            else:
                corr = np.correlate(xp[lo : hi + hs], ref, mode="valid")
                idx_lo = lo + np.arange(len(corr))
                seg_sq = np.maximum(csum_sq[idx_lo + hs] - csum_sq[idx_lo], 1e-12)
                best = lo + int(np.argmax(corr / (np.sqrt(seg_sq) * ref_e + 1e-12)))
        s = k * hs
        out[s : s + win_len] += xp[best : best + win_len] * win
        norm[s : s + win_len] += win
        prev_pos = best
    return (out / np.maximum(norm, 1e-6))[:n_out].copy()


def _wsola_pitch_shift(x: Any, sr: int, semitones: float) -> Any:
    if abs(semitones) < 1e-3 or len(x) < 1024:
        return x.copy()
    return _fft_resample_length(_wsola_time_stretch(x, sr, 2.0 ** (semitones / 12.0)), len(x))


def _cepstral_formant_warp(x: Any, sr: int, extra_ratio: float) -> Any:
    """Zero-phase cepstral spectral-envelope formant warp (reshapes vocal tract without pitch smear)."""
    n = len(x)
    if n < 512 or abs(extra_ratio - 1.0) < 0.012:
        return x.copy()
    extra_ratio = max(0.86, min(1.16, extra_ratio))

    win_len, hop = 1024, 256
    win = np.hanning(win_len)
    pad_l, pad_r = win_len, win_len + hop
    xp = np.pad(x, (pad_l, pad_r), mode="constant")
    n_frames = 1 + (len(xp) - win_len) // hop
    idx = np.arange(win_len)[None, :] + (np.arange(n_frames) * hop)[:, None]
    spec = np.fft.rfft(xp[idx] * win[None, :], axis=1)

    lifter_cut = 44  # ~1.0 ms quefrency separates smooth vocal-tract formants from pitch
    q = np.arange(win_len)
    q_sym = np.minimum(q, win_len - q)
    lifter = np.where(q_sym <= lifter_cut, 0.5 * (1.0 + np.cos(np.pi * q_sym / float(lifter_cut))), 0.0)
    env_log = np.fft.rfft(np.fft.irfft(np.log(np.abs(spec) + 1e-5), n=win_len, axis=1) * lifter[None, :], axis=1).real

    freqs = np.fft.rfftfreq(win_len, d=1.0 / sr)
    src_f = freqs / extra_ratio
    blend = np.clip((freqs - 110.0) / 140.0, 0.0, 1.0) * np.clip((8400.0 - freqs) / 1400.0, 0.0, 1.0)

    gain = np.ones_like(env_log)
    for i in range(n_frames):
        warped = np.interp(src_f, freqs, env_log[i], left=env_log[i, 0], right=env_log[i, -1])
        gain[i] = np.exp(np.clip((warped - env_log[i]) * blend, -0.85, 0.85))
    if n_frames >= 3:
        g_sm = gain.copy()
        g_sm[1:-1] = 0.25 * gain[:-2] + 0.50 * gain[1:-1] + 0.25 * gain[2:]
        gain = g_sm

    clean = np.fft.irfft(spec * gain, n=win_len, axis=1) * win[None, :]
    out = np.zeros(len(xp), dtype=np.float64)
    norm = np.zeros(len(xp), dtype=np.float64)
    win_sq = win * win
    for i in range(n_frames):
        s = i * hop
        out[s : s + win_len] += clean[i]
        norm[s : s + win_len] += win_sq
    return (out / np.maximum(norm, 1e-9))[pad_l : pad_l + n].copy()


def _subtle_analog_warmth(x: Any, sr: int, drive: float = 1.9, mix: float = 0.16) -> Any:
    band = _zero_phase_filter(x, sr, lambda f: _hp(f, 200.0, 2) * _lp(f, 3800.0, 2))
    rms = float(np.sqrt(np.mean(band * band)))
    if mix <= 0.0 or rms < 1e-5:
        return x
    nb = band / (rms * 2.4 + 1e-9)
    sat = (np.tanh(drive * (nb + 0.12)) - np.tanh(drive * 0.12)) - nb
    sat = _zero_phase_filter(sat, sr, lambda f: _hp(f, 180.0, 2) * _lp(f, 6500.0, 2))
    s_rms = float(np.sqrt(np.mean(sat * sat)))
    return x + (sat * (rms / s_rms)) * mix if s_rms > 1e-9 else x


def _snorkel_cavity_tap(x: Any, sr: int, delay_ms: float = 1.35, mix: float = 0.13) -> Any:
    d = int(round(sr * delay_ms * 0.001))
    if d <= 0 or d >= len(x) or mix <= 0.0:
        return x
    mid = _zero_phase_filter(x, sr, lambda f: _hp(f, 380.0, 2) * _lp(f, 2600.0, 2))
    out = x.copy()
    out[d:] += mix * mid[:-d]
    return out


# ---------------------------------------------------------------------------
# Category-specific remastering chains
# ---------------------------------------------------------------------------

def _voice_eq_curve(role: str) -> Callable[[Any], Any]:
    if role == "combat":
        return lambda f: _hp(f, 78.0) * _bell(f, 190.0, +3.2, 1.05) * _bell(f, 480.0, +1.4) * _bell(f, 2900.0, +2.1, 1.05) * _lp(f, 11000.0)
    if role == "bomber":
        return lambda f: _hp(f, 135.0) * _bell(f, 2800.0, +3.0, 0.95) * _bell(f, 4700.0, +2.0) * _lp(f, 12000.0)
    if role == "swimmer":
        return lambda f: _hp(f, 100.0) * _bell(f, 680.0, +2.6, 0.9) * _bell(f, 1350.0, +1.8, 0.85) * _bell(f, 2700.0, -1.8, 0.95) * _bell(f, 4000.0, +1.4, 0.9) * _lp(f, 7200.0)
    if role == "fire":
        return lambda f: _hp(f, 105.0) * _bell(f, 320.0, +1.8, 1.05) * _bell(f, 3500.0, +2.6) * _lp(f, 11600.0)
    if role == "thief":
        return lambda f: _hp(f, 125.0) * _bell(f, 260.0, -1.6) * _bell(f, 3900.0, +3.3, 0.95) * _shelf(f, 5200.0, +1.8) * _lp(f, 12200.0)
    if role == "worker":
        return lambda f: _hp(f, 115.0) * _bell(f, 360.0, +0.7, 1.05) * _bell(f, 3400.0, +2.8) * _shelf(f, 5200.0, +1.2, True, 0.8) * _lp(f, 11800.0)
    return lambda f: _hp(f, 110.0) * _bell(f, 330.0, +1.0, 1.1) * _bell(f, 3250.0, +2.5) * _lp(f, 11500.0)


def _remaster_voice_ant(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    y = _stft_denoise(x, sr, orig_sr, strength=1.18, floor_gain=0.18, cap_mult=2.8)
    y = _zero_phase_filter(y, sr, lambda f: _hp(f, 75.0) * _lp(f, 11800.0))

    prof = VOICE_PROFILES.get(sound_id)
    role = "worker"
    if prof is not None:
        role, semitones, target_formant, _ = prof
        y = _wsola_pitch_shift(y, sr, semitones)
        y = _cepstral_formant_warp(y, sr, target_formant / (2.0 ** (semitones / 12.0)))

    y = _zero_phase_filter(y, sr, _voice_eq_curve(role))
    if role == "combat":
        y = _subtle_analog_warmth(y, sr, drive=1.9, mix=0.14)
    elif role == "fire":
        y = _subtle_analog_warmth(y, sr, drive=2.1, mix=0.18)
    elif role == "swimmer":
        y = _snorkel_cavity_tap(y, sr, delay_ms=1.35, mix=0.13)

    if orig_sr <= 11025:
        y = y + _harmonic_exciter(y, sr, 1800.0, 4900.0, 5100.0, 9600.0, mix=0.09, even_ratio=0.6)
    elif role == "thief":
        y = y + _harmonic_exciter(y, sr, 2400.0, 5800.0, 6000.0, 11400.0, mix=0.075, even_ratio=0.62)
    elif role != "swimmer":
        y = y + _harmonic_exciter(y, sr, 2400.0, 5800.0, 6200.0, 11200.0, mix=0.045, even_ratio=0.6)

    y = _gentle_silence_gate(y, sr, rel_thresh=0.055, floor_gain=0.12)
    y = _soft_compress(y, sr, thresh_db=-18.0, ratio=2.6 if role == "thief" else 2.2, attack_ms=6.0, release_ms=65.0)
    y = _level_and_limit(y, target_rms=0.145, max_gain_db=10.5, ceiling=0.91)
    return _apply_fades(y, sr, fade_in_ms=3.0, fade_out_ms=6.0)


def _remaster_voice_announcer(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    y = _stft_denoise(x, sr, orig_sr, strength=1.15, floor_gain=0.20, cap_mult=2.8)
    y = _zero_phase_filter(y, sr, lambda f: _hp(f, 95.0) * _bell(f, 280.0, +1.0, 1.1) * _bell(f, 3000.0, +2.3, 1.05) * _lp(f, 11000.0))
    if orig_sr <= 11025:
        y = y + _harmonic_exciter(y, sr, 1700.0, 4800.0, 5100.0, 9500.0, mix=0.085, even_ratio=0.6)
    y = _gentle_silence_gate(y, sr, rel_thresh=0.045, floor_gain=0.15)
    y = _soft_compress(y, sr, thresh_db=-17.0, ratio=2.1, attack_ms=7.0, release_ms=75.0)
    y = _level_and_limit(y, target_rms=0.155, max_gain_db=9.0, ceiling=0.91)
    return _apply_fades(y, sr, fade_in_ms=3.5, fade_out_ms=8.0)


def _remaster_bombexp(x: Any, sr: int, orig_sr: int, seed: int) -> Any:
    """Hero remaster for bombexp: warm sub-bass punch + tight crack + airy decay, 1998 blast intact."""
    n = len(x)
    rng = np.random.default_rng(seed)
    y = _stft_denoise(x, sr, orig_sr, strength=0.85, floor_gain=0.28, cap_mult=2.4)
    y = _zero_phase_filter(
        y, sr,
        lambda f: _hp(f, 28.0) * _bell(f, 65.0, +2.4, 0.95) * _bell(f, 250.0, +2.2, 1.1) * _bell(f, 1800.0, +1.5, 1.2) * _lp(f, 11500.0),
    )

    env_body = _smooth_envelope(x, sr, attack_ms=4.0, release_ms=95.0)
    env_pk = float(np.max(env_body)) + 1e-9
    env_norm = env_body / env_pk
    onset_idx = int(np.argmax(np.abs(x) >= 0.65 * float(np.max(np.abs(x)))))
    t = np.arange(n, dtype=np.float64) / float(sr)
    t_onset = onset_idx / float(sr)

    # Warm sub-bass thump (84 -> 42 Hz sweep locked to the blast envelope)
    t_rel = np.maximum(0.0, t - max(0.0, t_onset - 0.004))
    phase = 2.0 * np.pi * np.cumsum(42.0 + 42.0 * np.exp(-t_rel / 0.11)) / float(sr)
    sub_env = (env_norm ** 1.15) * np.exp(-t_rel / 0.38) * np.clip((t - max(0.0, t_onset - 0.006)) / 0.008, 0.0, 1.0)
    sub_thump = np.sin(phase) * sub_env * (0.20 * env_pk)

    # Tight initial crack transient synced to original onset (pink-sloped, never hissy)
    crack_bp = _pink_band_noise(n, sr, 950.0, 4800.0, rng)
    dt_c = t - t_onset
    crack_env = np.where(dt_c >= -0.002, np.clip((dt_c + 0.002) / 0.004, 0.0, 1.0) * np.exp(-np.maximum(0.0, dt_c) / 0.022), 0.0)
    crack_layer = crack_bp * crack_env * (0.14 * env_pk)

    # Warm low-mid crunch harmonics (translates powerfully on laptop/phone speakers)
    lm = _zero_phase_filter(y, sr, lambda f: _hp(f, 55.0) * _lp(f, 480.0))
    lm_rms = float(np.sqrt(np.mean(lm * lm))) + 1e-9
    crunch = _zero_phase_filter(np.tanh(2.1 * (lm / (lm_rms * 2.0))) * lm_rms, sr, lambda f: _hp(f, 130.0) * _lp(f, 1600.0))

    # Gentle natural airy tail decay riding the blast envelope
    air_bp = _pink_band_noise(n, sr, 420.0, 3800.0, rng)
    air_env = _smooth_envelope(x, sr, attack_ms=10.0, release_ms=175.0) / env_pk
    air_layer = air_bp * (air_env ** 1.35) * (0.026 * env_pk)

    mixed = _soft_compress(y + sub_thump + crack_layer + 0.14 * crunch + air_layer, sr, thresh_db=-12.0, ratio=2.4, attack_ms=8.0, release_ms=110.0)
    mixed = _level_and_limit(mixed, target_rms=0.255, max_gain_db=4.0, min_gain_db=-4.0, knee=0.80, ceiling=0.93)
    return _apply_fades(mixed, sr, fade_in_ms=2.5, fade_out_ms=32.0)


def _remaster_explosion(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    if sound_id == 4:
        return _remaster_bombexp(x, sr, orig_sr, seed=19980000 + sound_id)
    y = _stft_denoise(x, sr, orig_sr, strength=0.95, floor_gain=0.25, cap_mult=2.5)
    y = _zero_phase_filter(y, sr, lambda f: _hp(f, 35.0) * _bell(f, 95.0, +2.6) * _bell(f, 2400.0, +1.8, 1.1) * _lp(f, 11500.0))
    y = y + _harmonic_exciter(y, sr, 900.0, 4800.0, 5000.0, 9600.0, mix=0.08, even_ratio=0.5)
    y = _soft_compress(y, sr, thresh_db=-14.0, ratio=2.2, attack_ms=5.0, release_ms=65.0)
    y = _level_and_limit(y, target_rms=0.165 if sound_id == 73 else 0.135, max_gain_db=7.5, ceiling=0.92)
    return _apply_fades(y, sr, fade_in_ms=2.5, fade_out_ms=8.0)


def _remaster_fire(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    rng = np.random.default_rng(19980100 + sound_id)
    if sound_id == 67:  # firestarta: tonal magnifying-glass beam
        y = _stft_denoise(x, sr, orig_sr, strength=1.10, floor_gain=0.24, cap_mult=2.2)
        y = _zero_phase_filter(y, sr, lambda f: _hp(f, 65.0) * _bell(f, 260.0, +1.2) * _bell(f, 2400.0, +2.0, 1.1) * _lp(f, 10500.0))
        y = y + _harmonic_exciter(y, sr, 180.0, 1800.0, 1600.0, 7200.0, mix=0.11, even_ratio=0.65)
        y = _level_and_limit(y, target_rms=0.18, max_gain_db=5.0, ceiling=0.90)
        return _apply_fades(y, sr, fade_in_ms=4.0, fade_out_ms=14.0)

    y = _stft_denoise(x, sr, orig_sr, strength=0.80, floor_gain=0.30, cap_mult=2.2)
    body_g = +2.4 if sound_id in (5, 68) else +0.8
    y = _zero_phase_filter(y, sr, lambda f: _hp(f, 55.0) * _bell(f, 240.0, body_g, 1.15) * _bell(f, 3000.0, +1.8, 1.15) * _lp(f, 11800.0))
    env = _smooth_envelope(y, sr, attack_ms=5.0, release_ms=45.0)
    env_pk = float(np.max(env)) + 1e-9
    air = _pink_band_noise(len(y), sr, 3600.0 if sound_id != 69 else 4600.0, 10500.0, rng)
    air_amt = 0.042 if sound_id == 69 else 0.025
    y = y + air * (env / env_pk) * (air_amt * env_pk)
    y = y + _harmonic_exciter(y, sr, 1200.0, 4800.0, 5000.0, 9800.0, mix=0.075, even_ratio=0.48)
    y = _soft_compress(y, sr, thresh_db=-16.0, ratio=2.1, attack_ms=5.0, release_ms=65.0)
    target = 0.105 if sound_id == 5 else (0.185 if sound_id == 68 else 0.135)
    y = _level_and_limit(y, target_rms=target, max_gain_db=9.5, ceiling=0.91)
    return _apply_fades(y, sr, fade_in_ms=5.0 if sound_id == 68 else 3.0, fade_out_ms=10.0)


def _remaster_water_earth(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    rng = np.random.default_rng(19980200 + sound_id)
    y = _stft_denoise(x, sr, orig_sr, strength=1.05, floor_gain=0.24, cap_mult=2.5)
    is_earth, is_bubble = sound_id in (81, 82), sound_id in (62, 72)
    y = _zero_phase_filter(
        y, sr,
        lambda f: _hp(f, 75.0) * _bell(f, 300.0, +1.8 if is_bubble else +1.2, 1.1) * _bell(f, 2900.0, +2.2 if is_earth else +1.8, 1.15) * _lp(f, 11800.0),
    )
    y = y + _harmonic_exciter(y, sr, 1200.0, 5000.0, 5100.0, 10500.0, mix=0.10 if not is_bubble else 0.075, even_ratio=0.52)
    if not is_bubble:
        env_fast = _smooth_envelope(y, sr, attack_ms=2.0, release_ms=24.0)
        e_pk = float(np.max(env_fast)) + 1e-9
        tex = _pink_band_noise(len(y), sr, 4200.0 if is_earth else 4800.0, 10800.0, rng)
        y = y + tex * ((env_fast / e_pk) ** 1.4) * (0.030 * e_pk)
    if sound_id in (71, 80):
        y = _gentle_silence_gate(y, sr, rel_thresh=0.04, floor_gain=0.14)
    y = _soft_compress(y, sr, thresh_db=-16.0, ratio=2.1, attack_ms=4.0, release_ms=55.0)
    tmap = {62: 0.21, 71: 0.11, 72: 0.19, 79: 0.15, 80: 0.13, 81: 0.14, 82: 0.11}
    y = _level_and_limit(y, target_rms=tmap.get(sound_id, 0.14), max_gain_db=9.0, ceiling=0.92)
    return _apply_fades(y, sr, fade_in_ms=3.0, fade_out_ms=12.0 if sound_id in (72, 79) else 8.0)


def _remaster_combat_action(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    y = _stft_denoise(x, sr, orig_sr, strength=1.05, floor_gain=0.22, cap_mult=2.6)
    heavy = sound_id in (65, 76, 78, 47)
    bright = sound_id in (70, 66, 77, 83, 84, 85, 86, 57, 75)
    y = _zero_phase_filter(
        y, sr,
        lambda f: _hp(f, 45.0 if heavy else 80.0) * _bell(f, 180.0 if heavy else 240.0, +2.6 if heavy else +1.6, 1.05) * _bell(f, 3100.0, +2.1 if bright else +1.6, 1.1) * _lp(f, 11800.0),
    )
    ex_mix = 0.10 if sound_id in (57, 75, 83, 3, 66, 77) else 0.075
    y = y + _harmonic_exciter(y, sr, 1100.0, 5000.0, 5100.0, 10400.0, mix=ex_mix, even_ratio=0.48)
    y = _gentle_silence_gate(y, sr, rel_thresh=0.04, floor_gain=0.16)
    y = _soft_compress(y, sr, thresh_db=-15.0, ratio=2.3, attack_ms=4.0, release_ms=55.0)
    tmap = {3: 0.135, 47: 0.16, 57: 0.165, 64: 0.14, 65: 0.165, 66: 0.115, 70: 0.125, 75: 0.14, 76: 0.165, 77: 0.15, 78: 0.21, 83: 0.16, 84: 0.135, 85: 0.155, 86: 0.14, 90: 0.125}
    y = _level_and_limit(y, target_rms=tmap.get(sound_id, 0.15), max_gain_db=8.5, ceiling=0.92)
    return _apply_fades(y, sr, fade_in_ms=2.5, fade_out_ms=10.0 if sound_id in (64, 70) else 7.0)


def _remaster_chime_pickup(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    y = _stft_denoise(x, sr, orig_sr, strength=1.12, floor_gain=0.22, cap_mult=2.3)
    sparkle, warm = sound_id in (1, 2, 6, 43, 56, 87), sound_id in (40, 42, 48, 88)
    y = _zero_phase_filter(
        y, sr,
        lambda f: _hp(f, 55.0 if warm else 95.0) * _bell(f, 260.0, +1.6 if warm else +0.8) * _bell(f, 3400.0, +2.3 if sparkle else +1.6, 1.15) * _shelf(f, 4800.0, +1.6 if sparkle else +0.8, True, 0.8) * _lp(f, 12800.0),
    )
    y = y + _harmonic_exciter(y, sr, 420.0, 4400.0, 3400.0, 11000.0, mix=0.13 if sparkle else 0.075, even_ratio=0.72, drive=2.0)
    if sparkle:
        y = _early_gloss_tap(y, sr, delay_ms=16.0, mix=0.075)
    y = _soft_compress(y, sr, thresh_db=-14.0, ratio=1.9, attack_ms=6.0, release_ms=75.0)
    tmap = {1: 0.185, 2: 0.185, 6: 0.155, 40: 0.19, 42: 0.175, 43: 0.23, 44: 0.16, 48: 0.24, 54: 0.20, 55: 0.195, 56: 0.165, 61: 0.16, 63: 0.175, 87: 0.21, 88: 0.22}
    y = _level_and_limit(y, target_rms=tmap.get(sound_id, 0.18), max_gain_db=7.5, min_gain_db=-6.0, ceiling=0.90)
    fi = 1.5 if sound_id == 61 else 3.0
    fo = 12.0 if sound_id in (2, 42, 43, 48, 56, 63, 87, 88) else (5.0 if sound_id == 61 else 7.0)
    return _apply_fades(y, sr, fade_in_ms=fi, fade_out_ms=fo)


def _remaster_ui(x: Any, sr: int, orig_sr: int, sound_id: int) -> Any:
    y = _stft_denoise(x, sr, orig_sr, strength=1.15, floor_gain=0.20, cap_mult=2.4)
    y = _zero_phase_filter(y, sr, lambda f: _hp(f, 120.0) * _bell(f, 3400.0, +2.2, 1.05) * _lp(f, 12200.0))
    y = y + _harmonic_exciter(y, sr, 900.0, 4800.0, 4800.0, 10600.0, mix=0.10, even_ratio=0.65)
    if sound_id in (45, 46):
        y = _early_gloss_tap(y, sr, delay_ms=14.0, mix=0.06)
    y = _level_and_limit(y, target_rms=0.085 if sound_id in (0, 89) else 0.155, max_gain_db=9.0, ceiling=0.86)
    return _apply_fades(y, sr, fade_in_ms=2.0, fade_out_ms=7.0)


CATEGORY_PROCESSORS: Dict[str, Callable[[Any, int, int, int], Any]] = {
    "voice_ant": _remaster_voice_ant,
    "voice_announcer": _remaster_voice_announcer,
    "explosion": _remaster_explosion,
    "fire": _remaster_fire,
    "water_earth": _remaster_water_earth,
    "combat_action": _remaster_combat_action,
    "chime_pickup": _remaster_chime_pickup,
    "ui": _remaster_ui,
}


def remaster_signal(
    audio_2d: Any, orig_sr: int, sound_id: int, name: str = "", target_sr: int = TARGET_SAMPLE_RATE
) -> Tuple[Any, str, float]:
    """Remaster a `(channels, frames)` signal and return `(remastered_2d, category, env_corr)`."""
    _require_numpy()
    x = np.asarray(audio_2d, dtype=np.float64)
    if x.ndim == 1:
        x = x.reshape(1, -1)
    category = classify_sound(sound_id, name)
    proc = CATEGORY_PROCESSORS[category]

    upsampled_chs = [_bandlimited_upsample(ch, orig_sr, target_sr) for ch in x]
    out_chs = [proc(up, target_sr, orig_sr, sound_id) for up in upsampled_chs]
    remastered = np.stack(out_chs, axis=0)
    upsampled = np.stack(upsampled_chs, axis=0)

    # Envelope fidelity against DC-cleaned (>30 Hz) resampled original
    orig_ref = _zero_phase_filter(upsampled.mean(axis=0), target_sr, lambda f: _hp(f, 30.0))
    e0 = _smooth_envelope(orig_ref, target_sr, attack_ms=8.0, release_ms=25.0)
    e1 = _smooth_envelope(remastered.mean(axis=0), target_sr, attack_ms=8.0, release_ms=25.0)
    denom = float(np.sqrt(np.sum(e0 * e0) * np.sum(e1 * e1)))
    env_corr = float(np.sum(e0 * e1) / denom) if denom > 1e-12 else 1.0
    return remastered, category, env_corr


def remaster_file(
    src_path: str, dst_path: str, sound_id: int, name: str, target_sr: int = TARGET_SAMPLE_RATE
) -> Dict[str, Any]:
    """Remaster one WAV file and return structured before/after metrics."""
    _require_numpy()
    orig_2d, orig_sr, orig_bits = read_wav_float(src_path)
    orig_mono = orig_2d.mean(axis=0)
    orig_dc = float(np.mean(orig_mono))
    orig_c = orig_mono - orig_dc
    orig_stats = {
        "sample_rate": orig_sr,
        "channels": int(orig_2d.shape[0]),
        "bits": orig_bits,
        "frames": int(orig_2d.shape[1]),
        "duration_ms": round(1000.0 * orig_2d.shape[1] / float(orig_sr), 2),
        "peak": round(float(np.max(np.abs(orig_c))) if orig_2d.shape[1] else 0.0, 4),
        "rms": round(float(np.sqrt(np.mean(orig_c * orig_c))) if orig_2d.shape[1] else 0.0, 4),
        "dc": round(orig_dc, 5),
    }

    rem_2d, category, env_corr = remaster_signal(orig_2d, orig_sr, sound_id, name, target_sr)
    out_m = write_wav_i16(dst_path, rem_2d, target_sr, seed=19985000 + sound_id)
    out_stats = {
        "sample_rate": target_sr,
        "channels": int(rem_2d.shape[0]),
        "bits": TARGET_BITS,
        "frames": int(rem_2d.shape[1]),
        "duration_ms": round(out_m["duration_ms"], 2),
        "peak": round(out_m["peak"], 4),
        "rms": round(out_m["rms"], 4),
        "dc": round(out_m["dc"], 5),
        "env_corr": round(env_corr, 4),
        "sha256": _file_sha256_short(dst_path),
    }

    res: Dict[str, Any] = {
        "id": sound_id,
        "name": name,
        "category": category,
        "category_label": CATEGORY_LABELS.get(category, category),
        "original": orig_stats,
        "remastered": out_stats,
        "playback": playback_metadata(sound_id, category),
    }
    vp = voice_profile_for(sound_id)
    if vp is not None:
        res["voice_profile"] = vp
    return res


def _stage_music_tracks(music_src_dir: str, out_dir: str) -> List[Dict[str, str]]:
    staged: List[Dict[str, str]] = []
    if not os.path.isdir(music_src_dir):
        return staged
    music_out = os.path.join(out_dir, "music")
    for fname in MUSIC_TRACK_NAMES:
        src = os.path.join(music_src_dir, fname)
        if os.path.isfile(src):
            os.makedirs(music_out, exist_ok=True)
            dst = os.path.join(music_out, fname)
            if (not os.path.isfile(dst)) or os.path.getsize(dst) != os.path.getsize(src):
                shutil.copy2(src, dst)
            staged.append({"id": os.path.splitext(fname)[0].lower(), "title": os.path.splitext(fname)[0], "url": f"music/{fname}"})
    return staged


def build_audio_bank(
    src_dir: str = DEFAULT_SRC_DIR,
    out_dir: str = DEFAULT_OUT_DIR,
    music_src_dir: str = DEFAULT_MUSIC_DIR,
    only_ids: Optional[Iterable[int]] = None,
    copy_classic: bool = True,
    copy_music: bool = True,
) -> Dict[str, Any]:
    """Remaster all discovered WAVs and write `manifest.json` under `out_dir`."""
    _require_numpy()
    sources = discover_source_wavs(src_dir)
    if only_ids is not None:
        wanted = set(only_ids)
        sources = [s for s in sources if s[0] in wanted]

    rem_dir = os.path.join(out_dir, "remastered")
    classic_dir = os.path.join(out_dir, "classic")
    os.makedirs(rem_dir, exist_ok=True)
    if copy_classic:
        os.makedirs(classic_dir, exist_ok=True)

    entries: List[Dict[str, Any]] = []
    cat_counts: Dict[str, int] = {}
    hasher = hashlib.sha256()

    for sound_id, name, src_path in sources:
        out_name = f"sound_{sound_id:03d}_{name.lower()}.wav"
        info = remaster_file(src_path, os.path.join(rem_dir, out_name), sound_id, name, TARGET_SAMPLE_RATE)
        orig_base = os.path.basename(src_path)
        if copy_classic:
            classic_path = os.path.join(classic_dir, out_name)
            if (not os.path.isfile(classic_path)) or os.path.getsize(classic_path) != os.path.getsize(src_path):
                shutil.copy2(src_path, classic_path)
            info["classic_url"] = f"classic/{out_name}"
        else:
            info["classic_url"] = f"../../../../asset_catalog/sounds/{orig_base}"
        info["remastered_url"] = f"remastered/{out_name}"
        info["classic_fallback_url"] = f"../../asset_catalog/sounds/{orig_base}"
        entries.append(info)
        cat = info["category"]
        cat_counts[cat] = cat_counts.get(cat, 0) + 1
        hasher.update(f"{sound_id}:{info['remastered']['sha256']}\n".encode("utf-8"))

    music_tracks = _stage_music_tracks(music_src_dir, out_dir) if copy_music else []
    audio_hash = hasher.hexdigest()[:16]
    manifest: Dict[str, Any] = {
        "schema": 1,
        "version": f"audio-{audio_hash[:8]}",
        "audio_hash": audio_hash,
        "sample_rate": TARGET_SAMPLE_RATE,
        "bits": TARGET_BITS,
        "count": len(entries),
        "categories": [{"id": cid, "label": label, "count": cat_counts.get(cid, 0)} for cid, label in CATEGORY_LABELS.items()],
        "music": music_tracks,
        "sounds": entries,
    }
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    return manifest


def format_report(manifest: Dict[str, Any]) -> str:
    """Format a compact summary table comparing original vs remastered stats."""
    lines = [
        f"Ants Audio Remaster: {manifest['count']} sounds -> {manifest['sample_rate']} Hz {manifest['bits']}-bit PCM ({manifest['version']})",
        f"{'ID':>3} {'Name':<18} {'Category':<15} {'OrigSR':>6} {'DurDiff':>7} {'OrigRMS':>7} {'RemRMS':>7} {'RemPk':>6} {'Corr':>6}",
    ]
    max_pk, min_corr, max_dd = 0.0, 1.0, 0.0
    for s in manifest["sounds"]:
        o, r = s["original"], s["remastered"]
        dd = r["duration_ms"] - o["duration_ms"]
        max_pk, min_corr, max_dd = max(max_pk, r["peak"]), min(min_corr, r["env_corr"]), max(max_dd, abs(dd))
        lines.append(
            f"{s['id']:3d} {s['name']:<18} {s['category']:<15} {o['sample_rate']:6d} "
            f"{dd:+6.2f}m {o['rms']:7.4f} {r['rms']:7.4f} {r['peak']:6.3f} {r['env_corr']:6.3f}"
        )
    lines.append(
        f"Summary: max_peak={max_pk:.4f} (limit {MAX_SAFE_PEAK:.2f}), "
        f"max_dur_diff={max_dd:.2f}ms, min_env_corr={min_corr:.4f}, music_tracks={len(manifest.get('music', []))}"
    )
    return "\n".join(lines)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Remaster all 91 Ants (1998) WAV sounds to 44.1 kHz 16-bit PCM.")
    parser.add_argument("--src", default=DEFAULT_SRC_DIR, help="Source directory containing sound_*.wav")
    parser.add_argument("--out", default=DEFAULT_OUT_DIR, help="Output directory (default: tools/ant_models/models/audio)")
    parser.add_argument("--music-src", default=DEFAULT_MUSIC_DIR, help="Directory containing optional Original-Ants MP3s")
    parser.add_argument("--only", default="", help="Comma-separated sound IDs to process (e.g. 0,1,4,14)")
    parser.add_argument("--no-copy-classic", action="store_true", help="Do not copy classic WAVs into out/classic/")
    parser.add_argument("--no-copy-music", action="store_true", help="Do not copy MP3 music tracks into out/music/")
    parser.add_argument("--report", action="store_true", help="Print detailed per-file remaster report")
    args = parser.parse_args(list(argv) if argv is not None else None)

    only_ids = [int(tok.strip()) for tok in args.only.split(",") if tok.strip()] if args.only.strip() else None
    manifest = build_audio_bank(
        src_dir=args.src,
        out_dir=args.out,
        music_src_dir=args.music_src,
        only_ids=only_ids,
        copy_classic=not args.no_copy_classic,
        copy_music=not args.no_copy_music,
    )
    if args.report:
        print(format_report(manifest))
    else:
        peaks = [s["remastered"]["peak"] for s in manifest["sounds"]]
        rms_vals = [s["remastered"]["rms"] for s in manifest["sounds"]]
        corrs = [s["remastered"]["env_corr"] for s in manifest["sounds"]]
        print(
            f"Remastered {manifest['count']} WAVs -> {args.out}/remastered/ "
            f"({manifest['sample_rate']} Hz {manifest['bits']}-bit, {manifest['version']}; "
            f"peak max={max(peaks) if peaks else 0:.4f}, "
            f"rms={min(rms_vals) if rms_vals else 0:.3f}..{max(rms_vals) if rms_vals else 0:.3f}, "
            f"min_corr={min(corrs) if corrs else 1:.3f}, music={len(manifest.get('music', []))})"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Canonical PBR materials, texture family bindings, and team palettes for Ants 3D models.

Material names are part of the shared contract with viewer.js so team shells
(chitin_team and chitin_dark) can be swapped live at render time.
All material colors are authored in sRGB and converted to linear light via the
IEC 61966-2-1 EOTF when serialized to glTF 2.0 baseColorFactor (alpha linear).
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


def srgb_to_linear(c: float) -> float:
    """Convert a single sRGB channel in [0, 1] to linear light via IEC 61966-2-1 EOTF."""
    v = max(0.0, min(1.0, float(c)))
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def srgb_rgba_to_linear(rgba: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    """Convert sRGB RGB channels to glTF 2.0 linear RGB while keeping alpha linear."""
    r, g, b, a = rgba
    return (srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), max(0.0, min(1.0, float(a))))


@dataclass(frozen=True)
class MaterialSpec:
    """Metallic-roughness PBR material matching glTF 2.0 pbrMetallicRoughness.

    `base_color` stores sRGB-authored RGBA factors; `to_gltf_dict()` converts
    RGB channels to linear space per glTF 2.0 section 5.22.1 (alpha stays linear).
    `texture_family` selects the embedded PBR texture set ("chitin", "cloth",
    "metal", "rubber", or None for untextured optical eyes and glass).
    """

    name: str
    base_color: Tuple[float, float, float, float]
    metallic: float
    roughness: float
    double_sided: bool = False
    alpha_mode: str = "OPAQUE"
    texture_family: Optional[str] = None
    normal_scale: float = 0.50
    emissive: Tuple[float, float, float] = (0.0, 0.0, 0.0)

    def resolved_alpha_mode(self) -> str:
        """Return effective glTF 2.0 alphaMode ('BLEND' when alpha < 1.0)."""
        if self.alpha_mode != "OPAQUE":
            return self.alpha_mode
        return "BLEND" if self.base_color[3] < 0.9999 else "OPAQUE"

    def linear_base_color(self) -> Tuple[float, float, float, float]:
        """Return linear-space RGBA baseColorFactor per glTF 2.0 spec."""
        return srgb_rgba_to_linear(self.base_color)

    def linear_emissive(self) -> Tuple[float, float, float]:
        """Return linear-space RGB emissiveFactor per glTF 2.0 spec."""
        r, g, b = self.emissive
        return (srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b))

    def to_gltf_dict(
        self,
        *,
        base_tex_index: Optional[int] = None,
        normal_tex_index: Optional[int] = None,
        mr_tex_index: Optional[int] = None,
    ) -> Dict[str, object]:
        """Serialize to a glTF 2.0 material object with IEC-linear baseColorFactor."""
        mode = self.resolved_alpha_mode()
        pbr: Dict[str, object] = {
            "baseColorFactor": [round(float(c), 6) for c in self.linear_base_color()],
            "metallicFactor": round(float(self.metallic), 6),
            "roughnessFactor": round(float(self.roughness), 6),
        }
        if base_tex_index is not None:
            pbr["baseColorTexture"] = {"index": int(base_tex_index)}
        if mr_tex_index is not None:
            pbr["metallicRoughnessTexture"] = {"index": int(mr_tex_index)}

        out: Dict[str, object] = {
            "name": self.name,
            "pbrMetallicRoughness": pbr,
            "alphaMode": mode,
            "doubleSided": bool(self.double_sided or mode == "BLEND"),
        }
        if any(c > 1e-6 for c in self.emissive):
            out["emissiveFactor"] = [round(float(c), 6) for c in self.linear_emissive()]
        if normal_tex_index is not None:
            out["normalTexture"] = {
                "index": int(normal_tex_index),
                "scale": round(float(self.normal_scale), 4),
            }
        return out


_SPECS: Tuple[MaterialSpec, ...] = (
    MaterialSpec(
        name="chitin_team",
        base_color=(0.24, 0.45, 0.27, 1.0),
        metallic=0.06,
        roughness=0.50,
        texture_family="chitin",
        normal_scale=0.30,
    ),
    MaterialSpec(
        name="chitin_dark",
        base_color=(0.10, 0.22, 0.13, 1.0),
        metallic=0.06,
        roughness=0.54,
        texture_family="chitin",
        normal_scale=0.30,
    ),
    MaterialSpec(
        name="eye_white",
        base_color=(0.95, 0.94, 0.87, 1.0),
        metallic=0.0,
        roughness=0.12,
    ),
    MaterialSpec(
        name="eye_pupil",
        base_color=(0.04, 0.05, 0.06, 1.0),
        metallic=0.04,
        roughness=0.07,
    ),
    MaterialSpec(
        name="gear_gold",
        base_color=(0.86, 0.68, 0.20, 1.0),
        metallic=0.04,
        roughness=0.64,
        texture_family="cloth",
        normal_scale=0.40,
    ),
    MaterialSpec(
        name="gear_gold_metal",
        base_color=(0.88, 0.68, 0.18, 1.0),
        metallic=0.82,
        roughness=0.28,
        texture_family="metal",
        normal_scale=0.34,
    ),
    MaterialSpec(
        name="gear_accent",
        base_color=(0.74, 0.15, 0.20, 1.0),
        metallic=0.04,
        roughness=0.60,
        texture_family="cloth",
        normal_scale=0.38,
    ),
    MaterialSpec(
        name="gear_dark",
        base_color=(0.12, 0.12, 0.14, 1.0),
        metallic=0.08,
        roughness=0.66,
        texture_family="rubber",
        normal_scale=0.34,
    ),
    MaterialSpec(
        name="glass_tinted",
        base_color=(0.12, 0.24, 0.32, 0.30),
        metallic=0.10,
        roughness=0.06,
        double_sided=True,
        alpha_mode="BLEND",
    ),
    MaterialSpec(
        name="glass_clear",
        base_color=(0.85, 0.94, 0.99, 0.22),
        metallic=0.08,
        roughness=0.05,
        double_sided=True,
        alpha_mode="BLEND",
    ),
    MaterialSpec(
        name="fuse_white",
        base_color=(0.88, 0.84, 0.75, 1.0),
        metallic=0.0,
        roughness=0.76,
        texture_family="cloth",
        normal_scale=0.38,
    ),
    MaterialSpec(
        name="prop_brown",
        base_color=(0.42, 0.27, 0.16, 1.0),
        metallic=0.02,
        roughness=0.70,
        texture_family="cloth",
        normal_scale=0.36,
    ),
    MaterialSpec(
        name="eye_iris",
        base_color=(0.64, 0.62, 0.24, 1.0),
        metallic=0.06,
        roughness=0.14,
    ),
    MaterialSpec(
        name="eye_limbal",
        base_color=(0.14, 0.16, 0.09, 1.0),
        metallic=0.04,
        roughness=0.10,
    ),
    MaterialSpec(
        name="eye_catchlight",
        base_color=(1.0, 1.0, 1.0, 1.0),
        metallic=0.0,
        roughness=0.04,
        emissive=(0.68, 0.68, 0.65),
    ),
    MaterialSpec(
        name="gear_steel",
        base_color=(0.64, 0.66, 0.69, 1.0),
        metallic=0.78,
        roughness=0.34,
        texture_family="metal",
        normal_scale=0.34,
    ),
    MaterialSpec(
        name="gear_brass_trim",
        base_color=(0.74, 0.38, 0.19, 1.0),
        metallic=0.56,
        roughness=0.32,
        texture_family="metal",
        normal_scale=0.34,
    ),
    MaterialSpec(
        name="bomb_charge",
        base_color=(0.84, 0.08, 0.55, 1.0),
        metallic=0.04,
        roughness=0.60,
        texture_family="cloth",
        normal_scale=0.34,
    ),
    MaterialSpec(
        name="bomb_cup_trim",
        base_color=(0.24, 0.06, 0.09, 1.0),
        metallic=0.14,
        roughness=0.54,
        texture_family="rubber",
        normal_scale=0.32,
    ),
)

MATERIAL_ORDER: Tuple[str, ...] = tuple(spec.name for spec in _SPECS)
MATERIALS: Dict[str, MaterialSpec] = {spec.name: spec for spec in _SPECS}

TEAM_COLORS: Dict[str, Dict[str, List[float]]] = {
    "green": {
        "chitin_team": [0.24, 0.45, 0.27, 1.0],
        "chitin_dark": [0.10, 0.22, 0.13, 1.0],
    },
    "red": {
        "chitin_team": [0.54, 0.16, 0.18, 1.0],
        "chitin_dark": [0.24, 0.06, 0.07, 1.0],
    },
    "blue": {
        "chitin_team": [0.20, 0.33, 0.56, 1.0],
        "chitin_dark": [0.08, 0.14, 0.28, 1.0],
    },
    "black": {
        "chitin_team": [0.36, 0.38, 0.43, 1.0],
        "chitin_dark": [0.14, 0.15, 0.18, 1.0],
    },
}


def get_material(name: str) -> MaterialSpec:
    """Return the MaterialSpec for `name`, raising KeyError if unknown."""
    if name not in MATERIALS:
        known = ", ".join(MATERIAL_ORDER)
        raise KeyError(f"Unknown material {name!r}; expected one of: {known}")
    return MATERIALS[name]


def material_color(name: str, team: str = "green") -> Tuple[float, float, float, float]:
    """Return the sRGB-authored RGBA color for `name` under `team`."""
    spec = get_material(name)
    palette = TEAM_COLORS.get(team)
    if palette and name in palette:
        c = palette[name]
        return (float(c[0]), float(c[1]), float(c[2]), float(c[3]))
    return spec.base_color


def material_linear_color(name: str, team: str = "green") -> Tuple[float, float, float, float]:
    """Return the glTF 2.0 linear-RGB + linear-alpha color for `name` under `team`."""
    return srgb_rgba_to_linear(material_color(name, team=team))

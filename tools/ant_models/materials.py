"""Canonical PBR materials and team color palettes for Ants 3D models.

Material names are part of the shared contract with viewer.js so team shells
(chitin_team and chitin_dark) can be swapped live at render time.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Tuple


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
    """

    name: str
    base_color: Tuple[float, float, float, float]
    metallic: float
    roughness: float
    double_sided: bool = False

    def linear_base_color(self) -> Tuple[float, float, float, float]:
        """Return linear-space RGBA baseColorFactor per glTF 2.0 spec."""
        return srgb_rgba_to_linear(self.base_color)

    def to_gltf_dict(self) -> Dict[str, object]:
        """Serialize to a glTF 2.0 material object with IEC-linear baseColorFactor."""
        return {
            "name": self.name,
            "pbrMetallicRoughness": {
                "baseColorFactor": [round(float(c), 6) for c in self.linear_base_color()],
                "metallicFactor": round(float(self.metallic), 6),
                "roughnessFactor": round(float(self.roughness), 6),
            },
            "doubleSided": bool(self.double_sided),
        }


_SPECS: Tuple[MaterialSpec, ...] = (
    MaterialSpec(
        name="chitin_team",
        base_color=(0.18, 0.56, 0.34, 1.0),
        metallic=0.05,
        roughness=0.30,
    ),
    MaterialSpec(
        name="chitin_dark",
        base_color=(0.08, 0.19, 0.13, 1.0),
        metallic=0.06,
        roughness=0.46,
    ),
    MaterialSpec(
        name="eye_white",
        base_color=(0.97, 0.98, 0.99, 1.0),
        metallic=0.0,
        roughness=0.14,
    ),
    MaterialSpec(
        name="eye_pupil",
        base_color=(0.07, 0.08, 0.11, 1.0),
        metallic=0.05,
        roughness=0.08,
    ),
    MaterialSpec(
        name="gear_gold",
        base_color=(0.95, 0.74, 0.18, 1.0),
        metallic=0.14,
        roughness=0.28,
    ),
    MaterialSpec(
        name="gear_gold_metal",
        base_color=(0.92, 0.69, 0.15, 1.0),
        metallic=0.78,
        roughness=0.22,
    ),
    MaterialSpec(
        name="gear_accent",
        base_color=(0.86, 0.17, 0.29, 1.0),
        metallic=0.05,
        roughness=0.32,
    ),
    MaterialSpec(
        name="gear_dark",
        base_color=(0.15, 0.17, 0.21, 1.0),
        metallic=0.08,
        roughness=0.52,
    ),
    MaterialSpec(
        name="glass_tinted",
        base_color=(0.10, 0.22, 0.30, 1.0),
        metallic=0.22,
        roughness=0.08,
    ),
    MaterialSpec(
        name="glass_clear",
        base_color=(0.80, 0.93, 0.98, 1.0),
        metallic=0.12,
        roughness=0.06,
    ),
    MaterialSpec(
        name="fuse_white",
        base_color=(0.95, 0.92, 0.85, 1.0),
        metallic=0.0,
        roughness=0.72,
    ),
    MaterialSpec(
        name="prop_brown",
        base_color=(0.48, 0.31, 0.19, 1.0),
        metallic=0.0,
        roughness=0.68,
    ),
)

MATERIAL_ORDER: Tuple[str, ...] = tuple(spec.name for spec in _SPECS)
MATERIALS: Dict[str, MaterialSpec] = {spec.name: spec for spec in _SPECS}

TEAM_COLORS: Dict[str, Dict[str, List[float]]] = {
    "green": {
        "chitin_team": [0.18, 0.56, 0.34, 1.0],
        "chitin_dark": [0.08, 0.19, 0.13, 1.0],
    },
    "red": {
        "chitin_team": [0.68, 0.18, 0.22, 1.0],
        "chitin_dark": [0.24, 0.07, 0.09, 1.0],
    },
    "blue": {
        "chitin_team": [0.22, 0.38, 0.72, 1.0],
        "chitin_dark": [0.09, 0.14, 0.28, 1.0],
    },
    "black": {
        "chitin_team": [0.46, 0.48, 0.54, 1.0],
        "chitin_dark": [0.13, 0.14, 0.17, 1.0],
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

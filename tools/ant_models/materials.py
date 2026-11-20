"""Canonical PBR materials and team color palettes for Ants 3D models.

Material names are part of the shared contract with viewer.js so team shells
(chitin_team and chitin_dark) can be swapped live at render time.
All material colors are authored in sRGB and converted to linear light via the
IEC 61966-2-1 EOTF when serialized to glTF 2.0 baseColorFactor (alpha linear).
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
    alpha_mode: str = "OPAQUE"

    def resolved_alpha_mode(self) -> str:
        """Return effective glTF 2.0 alphaMode ('BLEND' when alpha < 1.0)."""
        if self.alpha_mode != "OPAQUE":
            return self.alpha_mode
        return "BLEND" if self.base_color[3] < 0.9999 else "OPAQUE"

    def linear_base_color(self) -> Tuple[float, float, float, float]:
        """Return linear-space RGBA baseColorFactor per glTF 2.0 spec."""
        return srgb_rgba_to_linear(self.base_color)

    def to_gltf_dict(self) -> Dict[str, object]:
        """Serialize to a glTF 2.0 material object with IEC-linear baseColorFactor."""
        mode = self.resolved_alpha_mode()
        return {
            "name": self.name,
            "pbrMetallicRoughness": {
                "baseColorFactor": [round(float(c), 6) for c in self.linear_base_color()],
                "metallicFactor": round(float(self.metallic), 6),
                "roughnessFactor": round(float(self.roughness), 6),
            },
            "alphaMode": mode,
            "doubleSided": bool(self.double_sided or mode == "BLEND"),
        }


_SPECS: Tuple[MaterialSpec, ...] = (
    MaterialSpec(
        name="chitin_team",
        base_color=(0.24, 0.44, 0.27, 1.0),
        metallic=0.06,
        roughness=0.50,
    ),
    MaterialSpec(
        name="chitin_dark",
        base_color=(0.16, 0.31, 0.19, 1.0),
        metallic=0.06,
        roughness=0.52,
    ),
    MaterialSpec(
        name="eye_white",
        base_color=(0.93, 0.93, 0.87, 1.0),
        metallic=0.0,
        roughness=0.18,
    ),
    MaterialSpec(
        name="eye_pupil",
        base_color=(0.04, 0.05, 0.06, 1.0),
        metallic=0.04,
        roughness=0.08,
    ),
    MaterialSpec(
        name="gear_gold",
        base_color=(0.84, 0.67, 0.22, 1.0),
        metallic=0.06,
        roughness=0.52,
    ),
    MaterialSpec(
        name="gear_gold_metal",
        base_color=(0.85, 0.65, 0.18, 1.0),
        metallic=0.78,
        roughness=0.28,
    ),
    MaterialSpec(
        name="gear_accent",
        base_color=(0.70, 0.14, 0.18, 1.0),
        metallic=0.05,
        roughness=0.48,
    ),
    MaterialSpec(
        name="gear_dark",
        base_color=(0.11, 0.11, 0.12, 1.0),
        metallic=0.10,
        roughness=0.58,
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
        roughness=0.78,
    ),
    MaterialSpec(
        name="prop_brown",
        base_color=(0.40, 0.26, 0.16, 1.0),
        metallic=0.0,
        roughness=0.72,
    ),
    MaterialSpec(
        name="eye_iris",
        base_color=(0.58, 0.56, 0.24, 1.0),
        metallic=0.05,
        roughness=0.18,
    ),
    MaterialSpec(
        name="gear_steel",
        base_color=(0.62, 0.64, 0.66, 1.0),
        metallic=0.72,
        roughness=0.34,
    ),
    MaterialSpec(
        name="gear_brass_trim",
        base_color=(0.72, 0.36, 0.18, 1.0),
        metallic=0.45,
        roughness=0.32,
    ),
)

MATERIAL_ORDER: Tuple[str, ...] = tuple(spec.name for spec in _SPECS)
MATERIALS: Dict[str, MaterialSpec] = {spec.name: spec for spec in _SPECS}

TEAM_COLORS: Dict[str, Dict[str, List[float]]] = {
    "green": {
        "chitin_team": [0.24, 0.44, 0.27, 1.0],
        "chitin_dark": [0.16, 0.31, 0.19, 1.0],
    },
    "red": {
        "chitin_team": [0.52, 0.16, 0.18, 1.0],
        "chitin_dark": [0.36, 0.10, 0.12, 1.0],
    },
    "blue": {
        "chitin_team": [0.20, 0.32, 0.54, 1.0],
        "chitin_dark": [0.13, 0.22, 0.39, 1.0],
    },
    "black": {
        "chitin_team": [0.35, 0.37, 0.42, 1.0],
        "chitin_dark": [0.23, 0.25, 0.29, 1.0],
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

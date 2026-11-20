"""Procedural 3D sculpting primitives, transforms, and scene builder.

Coordinate system (glTF 2.0 right-handed):
  +Y = up, +Z = front (South / camera 301), +X = ant's left.
All primitives emit finite float32 positions, unit outward float32 normals,
and counter-clockwise uint32 triangle indices.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Tuple, Union

import numpy as np

try:
    from .materials import get_material
except ImportError:
    from materials import get_material  # type: ignore

Vec3Like = Union[Sequence[float], np.ndarray]


def _as_vec3(v: Vec3Like, dy: Optional[float] = None, dz: Optional[float] = None) -> np.ndarray:
    if dy is not None and dz is not None:
        return np.array([float(v), float(dy), float(dz)], dtype=np.float64)
    arr = np.asarray(v, dtype=np.float64).reshape(-1)
    if arr.size == 1:
        return np.full(3, float(arr[0]), dtype=np.float64)
    if arr.size != 3:
        raise ValueError(f"Expected 3 components, got shape {arr.shape}")
    return arr


def _normalize_rows(vecs: np.ndarray, fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> np.ndarray:
    norms = np.linalg.norm(vecs, axis=1, keepdims=True)
    out = np.empty_like(vecs, dtype=np.float64)
    good = norms[:, 0] > 1e-10
    out[good] = vecs[good] / norms[good]
    if not np.all(good):
        fb = np.array(fallback, dtype=np.float64)
        out[~good] = fb / np.linalg.norm(fb)
    return out


def _quad_ring_tris(b0: int, b1: int, n: int) -> List[Tuple[int, int, int]]:
    tris: List[Tuple[int, int, int]] = []
    for i in range(n):
        ni = (i + 1) % n
        tris.append((b0 + i, b0 + ni, b1 + ni))
        tris.append((b0 + i, b1 + ni, b1 + i))
    return tris


def recompute_smooth_normals(positions: np.ndarray, indices: np.ndarray) -> np.ndarray:
    """Compute area-weighted smooth unit vertex normals from triangle indices."""
    pos = np.asarray(positions, dtype=np.float64)
    idx = np.asarray(indices, dtype=np.int64).reshape(-1, 3)
    normals = np.zeros_like(pos, dtype=np.float64)
    if len(idx) > 0:
        face_n = np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]])
        np.add.at(normals, idx[:, 0], face_n)
        np.add.at(normals, idx[:, 1], face_n)
        np.add.at(normals, idx[:, 2], face_n)
    bad = np.linalg.norm(normals, axis=1) <= 1e-10
    if np.any(bad) and len(pos) > 0:
        normals[bad] = _normalize_rows(pos[bad] - pos.mean(axis=0))
    return _normalize_rows(normals).astype(np.float32)


def signed_volume(positions: np.ndarray, indices: np.ndarray) -> float:
    """Return signed volume of a closed triangle mesh (positive = outward CCW)."""
    pos = np.asarray(positions, dtype=np.float64)
    idx = np.asarray(indices, dtype=np.int64).reshape(-1, 3)
    if len(idx) == 0:
        return 0.0
    c = pos.mean(axis=0)
    v0, v1, v2 = pos[idx[:, 0]] - c, pos[idx[:, 1]] - c, pos[idx[:, 2]] - c
    return float(np.sum(np.einsum("ij,ij->i", v0, np.cross(v1, v2))) / 6.0)


def ensure_outward_winding(
    positions: np.ndarray,
    indices: np.ndarray,
    reference_center: Optional[Vec3Like] = None,
) -> np.ndarray:
    """Ensure CCW outward triangle winding; flips winding if inverted."""
    pos = np.asarray(positions, dtype=np.float64)
    idx = np.asarray(indices, dtype=np.uint32).reshape(-1, 3).copy()
    if len(idx) == 0:
        return idx.reshape(-1)
    if reference_center is None:
        vol = signed_volume(pos, idx)
        if abs(vol) > 1e-10:
            if vol < 0.0:
                idx = idx[:, [0, 2, 1]]
            return idx.reshape(-1).astype(np.uint32)
        ref = 0.5 * (pos.min(axis=0) + pos.max(axis=0))
    else:
        ref = _as_vec3(reference_center)
    v0, v1, v2 = pos[idx[:, 0]], pos[idx[:, 1]], pos[idx[:, 2]]
    face_n = np.cross(v1 - v0, v2 - v0)
    face_c = (v0 + v1 + v2) / 3.0 - ref
    if float(np.sum(np.einsum("ij,ij->i", face_n, face_c))) < 0.0:
        idx = idx[:, [0, 2, 1]]
    return idx.reshape(-1).astype(np.uint32)


def _rotation_matrix_xyz(rx: float, ry: float, rz: float) -> np.ndarray:
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    mx = np.array([[1.0, 0.0, 0.0], [0.0, cx, -sx], [0.0, sx, cx]], dtype=np.float64)
    my = np.array([[cy, 0.0, sy], [0.0, 1.0, 0.0], [-sy, 0.0, cy]], dtype=np.float64)
    mz = np.array([[cz, -sz, 0.0], [sz, cz, 0.0], [0.0, 0.0, 1.0]], dtype=np.float64)
    return mz @ my @ mx


def _rotation_matrix_axis(axis: Vec3Like, angle: float) -> np.ndarray:
    a = _as_vec3(axis)
    n = np.linalg.norm(a)
    if n <= 1e-12:
        return np.eye(3, dtype=np.float64)
    x, y, z = a / n
    c, s, t = math.cos(angle), math.sin(angle), 1.0 - math.cos(angle)
    return np.array(
        [
            [t * x * x + c, t * x * y - s * z, t * x * z + s * y],
            [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
            [t * x * z - s * y, t * y * z + s * x, t * z * z + c],
        ],
        dtype=np.float64,
    )


def quat_from_axis_angle(axis: Vec3Like, angle: float, *, degrees: bool = False) -> Tuple[float, float, float, float]:
    """Return unit glTF quaternion (qx, qy, qz, qw) for rotation around `axis`."""
    ang = math.radians(angle) if degrees else float(angle)
    a = _as_vec3(axis)
    n = float(np.linalg.norm(a))
    if n <= 1e-12 or abs(ang) <= 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    s, c = math.sin(0.5 * ang), math.cos(0.5 * ang)
    ax, ay, az = a / n
    return (float(ax * s), float(ay * s), float(az * s), float(c))


def quat_mul(q1: Tuple[float, float, float, float], q2: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return (
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    )


def quat_from_euler_xyz(rx: float = 0.0, ry: float = 0.0, rz: float = 0.0, *, degrees: bool = False) -> Tuple[float, float, float, float]:
    """Return unit glTF quaternion matching `rotate_xyz(rx, ry, rz)` (apply X, then Y, then Z)."""
    q = quat_mul(
        quat_from_axis_angle((0.0, 0.0, 1.0), rz, degrees=degrees),
        quat_mul(quat_from_axis_angle((0.0, 1.0, 0.0), ry, degrees=degrees), quat_from_axis_angle((1.0, 0.0, 0.0), rx, degrees=degrees)),
    )
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q) if n > 1e-12 else (0.0, 0.0, 0.0, 1.0)


@dataclass
class NodeTransform:
    """glTF 2.0 node TRS transform (applied as T * R * S)."""

    translation: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    rotation: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 1.0)
    scale: Tuple[float, float, float] = (1.0, 1.0, 1.0)

    def to_matrix(self) -> np.ndarray:
        qx, qy, qz, qw = self.rotation
        n = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
        if n > 1e-12:
            qx, qy, qz, qw = qx / n, qy / n, qz / n, qw / n
        r = np.array([
            [1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
            [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
            [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)],
        ], dtype=np.float64)
        m = np.eye(4, dtype=np.float64)
        m[:3, :3], m[:3, 3] = r * np.asarray(self.scale, dtype=np.float64)[None, :], np.asarray(self.translation, dtype=np.float64)
        return m


@dataclass
class AnimationChannel:
    """Keyframe track targeting a named SceneBuilder node's TRS property."""
    target_node: str
    path: str
    times: List[float]
    values: List[Tuple[float, ...]]
    interpolation: str = "LINEAR"


@dataclass
class AnimationClip:
    """Named glTF 2.0 animation clip containing one or more keyframe channels."""
    name: str
    channels: List[AnimationChannel] = field(default_factory=list)


DEFAULT_UV_SCALE = 2.6


def _mirrored_theta_u(thetas: np.ndarray, circum: float, uv_scale: float = DEFAULT_UV_SCALE) -> np.ndarray:
    """Seam-free periodic mirrored U in [0, repeats] with zero wrap-back jump at theta=0/2pi."""
    repeats = max(1, int(round(max(0.05, float(circum)) * uv_scale * 0.5)))
    return (repeats * (1.0 - np.abs(thetas / math.pi - 1.0))).astype(np.float32)


def _default_continuous_uvs(positions: np.ndarray, uv_scale: float = DEFAULT_UV_SCALE) -> np.ndarray:
    """Fallback seam-free cylindrical UVs for hand-built meshes without explicit UVs."""
    pos = np.asarray(positions, dtype=np.float64)
    if len(pos) == 0:
        return np.zeros((0, 2), dtype=np.float32)
    ctr = 0.5 * (pos.min(axis=0) + pos.max(axis=0))
    span = pos.max(axis=0) - pos.min(axis=0)
    dx, dz = pos[:, 0] - ctr[0], pos[:, 2] - ctr[2]
    ang = np.mod(np.arctan2(dx, dz), 2.0 * math.pi)
    circum = math.pi * max(0.08, float(0.5 * (span[0] + span[2])))
    u = _mirrored_theta_u(ang, circum, uv_scale=uv_scale)
    v = ((pos[:, 1] - pos[:, 1].min()) * uv_scale).astype(np.float32)
    return np.column_stack([u, v]).astype(np.float32)


@dataclass
class MeshPart:
    """Immutable-style triangle mesh chunk with one named PBR material, UVs, and vertex AO."""

    name: str
    material: str
    positions: np.ndarray
    normals: np.ndarray
    indices: np.ndarray
    uvs: Optional[np.ndarray] = None
    colors: Optional[np.ndarray] = None

    def __post_init__(self) -> None:
        get_material(self.material)
        self.positions = np.ascontiguousarray(self.positions, dtype=np.float32).reshape(-1, 3)
        self.normals = np.ascontiguousarray(self.normals, dtype=np.float32).reshape(-1, 3)
        self.indices = np.ascontiguousarray(self.indices, dtype=np.uint32).reshape(-1)
        n = len(self.positions)
        if self.uvs is None:
            self.uvs = _default_continuous_uvs(self.positions)
        else:
            self.uvs = np.ascontiguousarray(self.uvs, dtype=np.float32).reshape(-1, 2)
        if self.colors is None:
            self.colors = np.ones((n, 3), dtype=np.float32)
        else:
            self.colors = np.ascontiguousarray(self.colors, dtype=np.float32).reshape(-1, 3)
        if self.positions.shape != self.normals.shape:
            raise ValueError(f"positions {self.positions.shape} != normals {self.normals.shape}")
        if self.uvs.shape != (n, 2):
            raise ValueError(f"uvs {self.uvs.shape} != ({n}, 2)")
        if self.colors.shape != (n, 3):
            raise ValueError(f"colors {self.colors.shape} != ({n}, 3)")
        if len(self.indices) % 3 != 0:
            raise ValueError(f"indices length {len(self.indices)} is not a multiple of 3")
        if not (np.all(np.isfinite(self.positions)) and np.all(np.isfinite(self.normals)) and np.all(np.isfinite(self.uvs)) and np.all(np.isfinite(self.colors))):
            raise ValueError(f"MeshPart {self.name!r} has non-finite vertex attributes")

    def copy(self, name: Optional[str] = None, material: Optional[str] = None) -> "MeshPart":
        return MeshPart(
            self.name if name is None else name,
            self.material if material is None else material,
            self.positions.copy(),
            self.normals.copy(),
            self.indices.copy(),
            self.uvs.copy(),
            self.colors.copy(),
        )

    def translate(self, dx: Union[float, Vec3Like], dy: Optional[float] = None, dz: Optional[float] = None) -> "MeshPart":
        offset = _as_vec3(dx, dy, dz).astype(np.float32)
        return MeshPart(self.name, self.material, self.positions + offset, self.normals.copy(), self.indices.copy(), self.uvs.copy(), self.colors.copy())

    def scale(self, sx: Union[float, Vec3Like], sy: Optional[float] = None, sz: Optional[float] = None) -> "MeshPart":
        s = _as_vec3(sx, sy, sz)
        if np.any(np.abs(s) <= 1e-10):
            raise ValueError(f"Zero scale factor not allowed: {s}")
        m = np.eye(4, dtype=np.float64)
        m[0, 0], m[1, 1], m[2, 2] = s[0], s[1], s[2]
        return self.transform(m)

    def rotate_xyz(self, rx: float = 0.0, ry: float = 0.0, rz: float = 0.0, *, degrees: bool = False) -> "MeshPart":
        if degrees:
            rx, ry, rz = math.radians(rx), math.radians(ry), math.radians(rz)
        m = np.eye(4, dtype=np.float64)
        m[:3, :3] = _rotation_matrix_xyz(rx, ry, rz)
        return self.transform(m)

    def rotate_axis(self, axis: Vec3Like, angle: float, *, degrees: bool = False) -> "MeshPart":
        m = np.eye(4, dtype=np.float64)
        m[:3, :3] = _rotation_matrix_axis(axis, math.radians(angle) if degrees else angle)
        return self.transform(m)

    def transform(self, matrix_4x4: np.ndarray) -> "MeshPart":
        mat = np.asarray(matrix_4x4, dtype=np.float64)
        if mat.shape != (4, 4):
            raise ValueError(f"Expected 4x4 transform matrix, got {mat.shape}")
        lin = mat[:3, :3]
        pos = self.positions.astype(np.float64) @ lin.T + mat[:3, 3]
        norms = _normalize_rows(self.normals.astype(np.float64) @ np.linalg.inv(lin))
        idx = self.indices.copy()
        if np.linalg.det(lin) < 0.0:
            idx = idx.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)
        return MeshPart(self.name, self.material, pos.astype(np.float32), norms.astype(np.float32), idx, self.uvs.copy(), self.colors.copy())

    def mirror_x(self, name: Optional[str] = None) -> "MeshPart":
        """Mirror across the YZ plane (X -> -X) while preserving outward winding."""
        pos, norms = self.positions.copy(), self.normals.copy()
        pos[:, 0], norms[:, 0] = -pos[:, 0], -norms[:, 0]
        idx = self.indices.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1).copy()
        return MeshPart(name if name is not None else f"{self.name}_mirror", self.material, pos, norms, idx, self.uvs.copy(), self.colors.copy())

    def deform(self, fn: Callable[[np.ndarray], np.ndarray], *, recompute_normals: bool = True) -> "MeshPart":
        """Apply a vertex deformation callback `fn(positions_Nx3) -> new_positions_Nx3`."""
        new_pos = np.asarray(fn(self.positions.astype(np.float64).copy()), dtype=np.float32).reshape(-1, 3)
        if new_pos.shape != self.positions.shape:
            raise ValueError(f"deform changed shape from {self.positions.shape} to {new_pos.shape}")
        new_norms = recompute_smooth_normals(new_pos, self.indices) if recompute_normals else self.normals.copy()
        return MeshPart(self.name, self.material, new_pos, new_norms, self.indices.copy(), self.uvs.copy(), self.colors.copy())

    def recompute_normals(self) -> "MeshPart":
        return MeshPart(
            self.name, self.material, self.positions.copy(),
            recompute_smooth_normals(self.positions, self.indices), self.indices.copy(), self.uvs.copy(), self.colors.copy(),
        )

    def ensure_outward(self, reference_center: Optional[Vec3Like] = None) -> "MeshPart":
        idx = ensure_outward_winding(self.positions, self.indices, reference_center=reference_center)
        return MeshPart(self.name, self.material, self.positions.copy(), recompute_smooth_normals(self.positions, idx), idx, self.uvs.copy(), self.colors.copy())

    def bounds(self) -> Tuple[np.ndarray, np.ndarray]:
        if len(self.positions) == 0:
            z = np.zeros(3, dtype=np.float32)
            return z, z
        return self.positions.min(axis=0), self.positions.max(axis=0)

    def triangle_count(self) -> int:
        return int(len(self.indices) // 3)


def merge_parts(parts: Sequence[MeshPart], name: str = "merged", material: Optional[str] = None) -> MeshPart:
    """Merge multiple MeshParts sharing a material into a single MeshPart."""
    if not parts:
        raise ValueError("Cannot merge an empty sequence of MeshParts")
    mat = parts[0].material if material is None else material
    pos_l, nrm_l, idx_l, uv_l, col_l, offset = [], [], [], [], [], 0
    for part in parts:
        pos_l.append(part.positions)
        nrm_l.append(part.normals)
        idx_l.append(part.indices + np.uint32(offset))
        uv_l.append(part.uvs)
        col_l.append(part.colors)
        offset += len(part.positions)
    return MeshPart(name, mat, np.concatenate(pos_l), np.concatenate(nrm_l), np.concatenate(idx_l), np.concatenate(uv_l), np.concatenate(col_l))


@dataclass
class SceneBuilder:
    """Organizes MeshParts into named logical nodes with optional local TRS pivots and animations."""

    name: str = "ant"
    nodes: Dict[str, List[MeshPart]] = field(default_factory=dict)
    node_transforms: Dict[str, NodeTransform] = field(default_factory=dict)
    animations: List[AnimationClip] = field(default_factory=list)

    def add(self, node_name: str, part: Union[MeshPart, Iterable[MeshPart]]) -> None:
        bucket = self.nodes.setdefault(node_name, [])
        if isinstance(part, MeshPart):
            bucket.append(part)
        else:
            for item in part:
                if not isinstance(item, MeshPart):
                    raise TypeError(f"Expected MeshPart, got {type(item).__name__}")
                bucket.append(item)

    def add_mirrored(
        self, node_name: str, part: MeshPart, *, left_suffix: str = "_left", right_suffix: str = "_right"
    ) -> Tuple[MeshPart, MeshPart]:
        """Add `part` (on +X ant-left) and its X-mirrored twin (on -X ant-right)."""
        left = part.copy(name=f"{part.name}{left_suffix}")
        right = part.mirror_x(name=f"{part.name}{right_suffix}")
        self.add(node_name, left)
        self.add(node_name, right)
        return left, right

    def set_node_transform(
        self, node_name: str, *, translation: Optional[Vec3Like] = None,
        rotation: Optional[Sequence[float]] = None, scale: Optional[Vec3Like] = None,
    ) -> NodeTransform:
        cur = self.node_transforms.get(node_name, NodeTransform())
        t = tuple(float(c) for c in _as_vec3(translation)) if translation is not None else cur.translation
        r = tuple(float(c) for c in rotation) if rotation is not None else cur.rotation
        if len(r) != 4:
            raise ValueError(f"rotation must be a 4-tuple (qx, qy, qz, qw), got {r}")
        s = tuple(float(c) for c in _as_vec3(scale)) if scale is not None else cur.scale
        xf = NodeTransform(translation=t, rotation=r, scale=s)
        self.node_transforms[node_name] = xf
        return xf

    def get_node_transform(self, node_name: str) -> Optional[NodeTransform]:
        return self.node_transforms.get(node_name)

    def add_animation(self, name: str, channels: Sequence[AnimationChannel]) -> AnimationClip:
        clip = AnimationClip(name=name, channels=list(channels))
        self.animations.append(clip)
        return clip

    def add_blink_animation(
        self, eyelid_nodes: Sequence[str] = ("eyelid_left", "eyelid_right"), closed_angle: float = 1.48, *, name: str = "Blink"
    ) -> AnimationClip:
        """Add a glTF 2.0 Blink clip keyframing eyelid rotation around local +X."""
        times = [0.0, 2.40, 2.48, 2.56, 2.68, 3.00]
        q_open, q_closed = (0.0, 0.0, 0.0, 1.0), quat_from_axis_angle((1.0, 0.0, 0.0), closed_angle)
        vals = [q_open, q_open, q_closed, q_closed, q_open, q_open]
        return self.add_animation(name, [AnimationChannel(n, "rotation", list(times), list(vals)) for n in eyelid_nodes if self.nodes.get(n)])

    def get_node(self, node_name: str) -> List[MeshPart]:
        return list(self.nodes.get(node_name, ()))

    def node_names(self) -> List[str]:
        return [k for k, v in self.nodes.items() if v]

    def all_parts(self) -> List[MeshPart]:
        return [p for parts in self.nodes.values() for p in parts]

    def world_parts(self) -> List[MeshPart]:
        """Return all MeshParts transformed into world space (applying node TRS pivots)."""
        out: List[MeshPart] = []
        for k, parts in self.nodes.items():
            xf = self.node_transforms.get(k)
            mat = xf.to_matrix() if xf is not None else None
            out.extend(p.transform(mat) if mat is not None else p for p in parts)
        return out

    def bounds(self) -> Tuple[np.ndarray, np.ndarray]:
        non_empty = [p.bounds() for p in self.world_parts() if len(p.positions) > 0]
        if not non_empty:
            z = np.zeros(3, dtype=np.float32)
            return z, z
        return np.min([b[0] for b in non_empty], axis=0), np.max([b[1] for b in non_empty], axis=0)

    def triangle_count(self) -> int:
        return sum(p.triangle_count() for p in self.all_parts())

    def translate_all(self, dx: Union[float, Vec3Like], dy: Optional[float] = None, dz: Optional[float] = None) -> "SceneBuilder":
        off = _as_vec3(dx, dy, dz)
        for k, parts in self.nodes.items():
            xf = self.node_transforms.get(k)
            if xf is not None:
                self.node_transforms[k] = NodeTransform(
                    (xf.translation[0] + float(off[0]), xf.translation[1] + float(off[1]), xf.translation[2] + float(off[2])),
                    xf.rotation, xf.scale,
                )
            else:
                self.nodes[k] = [p.translate(off) for p in parts]
        return self

    def scale_all(self, sx: Union[float, Vec3Like], sy: Optional[float] = None, sz: Optional[float] = None) -> "SceneBuilder":
        s = _as_vec3(sx, sy, sz)
        for k, parts in self.nodes.items():
            xf = self.node_transforms.get(k)
            if xf is not None:
                self.node_transforms[k] = NodeTransform(
                    (xf.translation[0] * float(s[0]), xf.translation[1] * float(s[1]), xf.translation[2] * float(s[2])),
                    xf.rotation, xf.scale,
                )
            self.nodes[k] = [p.scale(s) for p in parts]
        return self

    def rotate_all_xyz(self, rx: float = 0.0, ry: float = 0.0, rz: float = 0.0, *, degrees: bool = False) -> "SceneBuilder":
        rot = _rotation_matrix_xyz(math.radians(rx) if degrees else rx, math.radians(ry) if degrees else ry, math.radians(rz) if degrees else rz)
        q_rot = quat_from_euler_xyz(rx, ry, rz, degrees=degrees)
        for k, parts in self.nodes.items():
            xf = self.node_transforms.get(k)
            if xf is not None:
                t_new = rot @ np.asarray(xf.translation, dtype=np.float64)
                self.node_transforms[k] = NodeTransform((float(t_new[0]), float(t_new[1]), float(t_new[2])), quat_mul(q_rot, xf.rotation), xf.scale)
            else:
                self.nodes[k] = [p.rotate_xyz(rx, ry, rz, degrees=degrees) for p in parts]
        return self

    def ground_to_y(self, target_y: float = 0.0) -> float:
        """Shift all geometry vertically so `min_y == target_y`. Returns shift dy."""
        dy = float(target_y - self.bounds()[0][1])
        if abs(dy) > 1e-7:
            self.translate_all(0.0, dy, 0.0)
        return dy

    def merge_from(self, other: "SceneBuilder", prefix: str = "") -> "SceneBuilder":
        for k, parts in other.nodes.items():
            target = f"{prefix}{k}" if prefix else k
            for p in parts:
                self.add(target, p.copy())
            if k in other.node_transforms:
                xf = other.node_transforms[k]
                self.node_transforms[target] = NodeTransform(xf.translation, xf.rotation, xf.scale)
        for clip in other.animations:
            self.animations.append(AnimationClip(
                clip.name,
                [AnimationChannel(f"{prefix}{ch.target_node}" if prefix else ch.target_node, ch.path, list(ch.times), list(ch.values), ch.interpolation) for ch in clip.channels],
            ))
        return self


# ---------------------------------------------------------------------------
# Spline and frame helpers
# ---------------------------------------------------------------------------

def _dedupe_points(pts: np.ndarray, extra: Optional[np.ndarray] = None, eps: float = 1e-6):
    keep = [0]
    for i in range(1, len(pts)):
        if np.linalg.norm(pts[i] - pts[keep[-1]]) > eps:
            keep.append(i)
    idx = np.array(keep, dtype=np.int64)
    return pts[idx], (None if extra is None else extra[idx])


def catmull_rom_spline(
    points: Sequence[Vec3Like], samples_per_seg: int = 8, *, closed: bool = False, alpha: float = 0.5
) -> np.ndarray:
    """Evaluate a centripetal Catmull-Rom spline through 3D control points."""
    pts = np.asarray(points, dtype=np.float64).reshape(-1, 3)
    if len(pts) < 2:
        raise ValueError("catmull_rom_spline requires at least 2 points")
    if len(pts) == 2 and not closed:
        t = np.linspace(0.0, 1.0, max(2, samples_per_seg + 1))[:, None]
        return (1.0 - t) * pts[0] + t * pts[1]
    if closed:
        ctrl, seg_count = np.vstack([pts[-1:], pts, pts[:2]]), len(pts)
    else:
        ctrl = np.vstack([(2.0 * pts[0] - pts[1])[None, :], pts, (2.0 * pts[-1] - pts[-2])[None, :]])
        seg_count = len(pts) - 1

    out: List[np.ndarray] = []
    for i in range(seg_count):
        p0, p1, p2, p3 = ctrl[i], ctrl[i + 1], ctrl[i + 2], ctrl[i + 3]
        t0 = 0.0
        t1 = t0 + max(1e-6, float(np.linalg.norm(p1 - p0))) ** alpha
        t2 = t1 + max(1e-6, float(np.linalg.norm(p2 - p1))) ** alpha
        t3 = t2 + max(1e-6, float(np.linalg.norm(p3 - p2))) ** alpha
        ts = np.linspace(t1, t2, max(2, samples_per_seg), endpoint=(not closed and i == seg_count - 1))
        for t in ts:
            a1 = ((t1 - t) * p0 + (t - t0) * p1) / (t1 - t0)
            a2 = ((t2 - t) * p1 + (t - t1) * p2) / (t2 - t1)
            a3 = ((t3 - t) * p2 + (t - t2) * p3) / (t3 - t2)
            b1 = ((t2 - t) * a1 + (t - t0) * a2) / (t2 - t0)
            b2 = ((t3 - t) * a2 + (t - t1) * a3) / (t3 - t1)
            out.append(((t2 - t) * b1 + (t - t1) * b2) / (t2 - t1))
    return np.asarray(out, dtype=np.float64)


def _resample_radii(radii_2d: np.ndarray, seg_count: int, samples_per_seg: int, closed: bool) -> np.ndarray:
    out: List[np.ndarray] = []
    n = len(radii_2d)
    for i in range(seg_count):
        r1, r2 = radii_2d[i % n], radii_2d[(i + 1) % n]
        ts = np.linspace(0.0, 1.0, max(2, samples_per_seg), endpoint=(not closed and i == seg_count - 1))
        for u in ts:
            s = u * u * (3.0 - 2.0 * u)
            out.append((1.0 - s) * r1 + s * r2)
    return np.asarray(out, dtype=np.float64)


def _parse_radii(radii: Union[float, Sequence[float], Sequence[Tuple[float, float]]], count: int) -> np.ndarray:
    if isinstance(radii, (int, float)):
        return np.full((count, 2), float(radii), dtype=np.float64)
    arr = np.asarray(radii, dtype=np.float64)
    if arr.ndim == 1:
        if len(arr) == 2 and count != 2:
            return np.tile(arr[None, :], (count, 1))
        if len(arr) != count:
            raise ValueError(f"radii length {len(arr)} does not match points length {count}")
        return np.column_stack([arr, arr])
    if arr.ndim == 2 and arr.shape == (count, 2):
        return arr.copy()
    raise ValueError(f"Invalid radii shape {arr.shape} for {count} points")


def _parallel_transport_frames(pts: np.ndarray, closed: bool) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    n = len(pts)
    tangents = np.zeros((n, 3), dtype=np.float64)
    for i in range(n):
        if closed:
            d = pts[(i + 1) % n] - pts[(i - 1) % n]
        elif i == 0:
            d = pts[1] - pts[0]
        elif i == n - 1:
            d = pts[-1] - pts[-2]
        else:
            d = pts[i + 1] - pts[i - 1]
        norm = np.linalg.norm(d)
        tangents[i] = d / norm if norm > 1e-10 else np.array([0.0, 1.0, 0.0])

    normals = np.zeros((n, 3), dtype=np.float64)
    binormals = np.zeros((n, 3), dtype=np.float64)
    ref = np.array([0.0, 1.0, 0.0]) if abs(tangents[0, 1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    n0 = ref - np.dot(ref, tangents[0]) * tangents[0]
    normals[0] = n0 / np.linalg.norm(n0)
    binormals[0] = np.cross(tangents[0], normals[0])

    for i in range(1, n):
        axis = np.cross(tangents[i - 1], tangents[i])
        s, c = float(np.linalg.norm(axis)), float(np.clip(np.dot(tangents[i - 1], tangents[i]), -1.0, 1.0))
        ni = (_rotation_matrix_axis(axis / s, math.atan2(s, c)) @ normals[i - 1]) if s > 1e-8 else normals[i - 1].copy()
        ni = ni - np.dot(ni, tangents[i]) * tangents[i]
        nlen = np.linalg.norm(ni)
        normals[i] = ni / nlen if nlen > 1e-10 else normals[i - 1]
        binormals[i] = np.cross(tangents[i], normals[i])

    if closed and n > 2:
        axis = np.cross(tangents[-1], tangents[0])
        s, c = float(np.linalg.norm(axis)), float(np.clip(np.dot(tangents[-1], tangents[0]), -1.0, 1.0))
        n_end = (_rotation_matrix_axis(axis / s, math.atan2(s, c)) @ normals[-1]) if s > 1e-8 else normals[-1]
        twist = math.atan2(float(np.dot(n_end, binormals[0])), float(np.dot(n_end, normals[0])))
        if abs(twist) > 1e-6:
            for i in range(1, n):
                ang = -twist * (i / n)
                ca, sa = math.cos(ang), math.sin(ang)
                normals[i], binormals[i] = ca * normals[i] + sa * binormals[i], -sa * normals[i] + ca * binormals[i]
    return tangents, normals, binormals


# ---------------------------------------------------------------------------
# Procedural sculpting primitives
# ---------------------------------------------------------------------------

def _spow(x: np.ndarray, exp: float) -> np.ndarray:
    return np.sign(x) * (np.abs(x) ** exp)


def superellipsoid(
    rx: float, ry: float, rz: float, e1: float = 1.0, e2: float = 1.0,
    u_segs: int = 28, v_segs: int = 20, *, name: str = "superellipsoid", material: str = "chitin_team",
) -> MeshPart:
    """Build a smooth closed superellipsoid centered at origin with seam-free UVs."""
    u_segs, v_segs = max(6, int(u_segs)), max(4, int(v_segs))
    e1, e2 = max(0.08, float(e1)), max(0.08, float(e2))
    phis = np.linspace(-0.5 * math.pi, 0.5 * math.pi, v_segs + 1)[1:-1]
    thetas = np.linspace(0.0, 2.0 * math.pi, u_segs, endpoint=False)
    cos_t, sin_t = _spow(np.cos(thetas), e2), _spow(np.sin(thetas), e2)

    u_ring = _mirrored_theta_u(thetas, math.pi * (rx + rz))
    v_scale = math.pi * 0.5 * (ry + 0.5 * (rx + rz)) * DEFAULT_UV_SCALE
    verts: List[np.ndarray] = [np.array([[0.0, -ry, 0.0]], dtype=np.float64)]
    uvs_l: List[np.ndarray] = [np.array([[float(u_ring.mean()), 0.0]], dtype=np.float32)]
    for phi in phis:
        cp = float(_spow(np.array(math.cos(phi)), e1))
        sp = float(_spow(np.array(math.sin(phi)), e1))
        verts.append(np.column_stack([rx * cp * sin_t, np.full(u_segs, ry * sp), rz * cp * cos_t]))
        v_val = float((phi + 0.5 * math.pi) / math.pi * v_scale)
        uvs_l.append(np.column_stack([u_ring, np.full(u_segs, v_val, dtype=np.float32)]))
    verts.append(np.array([[0.0, +ry, 0.0]], dtype=np.float64))
    uvs_l.append(np.array([[float(u_ring.mean()), float(v_scale)]], dtype=np.float32))

    positions = np.vstack(verts).astype(np.float32)
    uvs = np.vstack(uvs_l).astype(np.float32)
    n_rings, north_idx = len(phis), len(positions) - 1
    tris: List[Tuple[int, int, int]] = [(0, 1 + (i + 1) % u_segs, 1 + i) for i in range(u_segs)]
    for r in range(n_rings - 1):
        tris.extend(_quad_ring_tris(1 + r * u_segs, 1 + (r + 1) * u_segs, u_segs))
    top_b = 1 + (n_rings - 1) * u_segs
    tris.extend((north_idx, top_b + i, top_b + (i + 1) % u_segs) for i in range(u_segs))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices, uvs=uvs)


def sphere(radius: float, u_segs: int = 28, v_segs: int = 20, *, name: str = "sphere", material: str = "chitin_team") -> MeshPart:
    return superellipsoid(radius, radius, radius, 1.0, 1.0, u_segs, v_segs, name=name, material=material)


def ellipsoid(
    rx: float, ry: float, rz: float, u_segs: int = 28, v_segs: int = 20, *, name: str = "ellipsoid", material: str = "chitin_team"
) -> MeshPart:
    return superellipsoid(rx, ry, rz, 1.0, 1.0, u_segs, v_segs, name=name, material=material)


def swept_tube(
    points: Sequence[Vec3Like],
    radii: Union[float, Sequence[float], Sequence[Tuple[float, float]]],
    radial_segs: int = 14,
    cap_start: bool = True,
    cap_end: bool = True,
    closed: bool = False,
    *,
    smooth_subdiv: int = 0,
    cap_rings: int = 4,
    name: str = "tube",
    material: str = "chitin_dark",
) -> MeshPart:
    """Sweep a smooth circular/elliptical tube along a 3D curve with Bishop frames & round caps."""
    raw_pts = np.asarray(points, dtype=np.float64).reshape(-1, 3)
    if len(raw_pts) < 2:
        raise ValueError("swept_tube requires at least 2 points")
    raw_rad = _parse_radii(radii, len(raw_pts))
    raw_pts, raw_rad = _dedupe_points(raw_pts, raw_rad)
    if closed and len(raw_pts) > 2 and np.linalg.norm(raw_pts[0] - raw_pts[-1]) <= 1e-6:
        raw_pts, raw_rad = raw_pts[:-1], raw_rad[:-1]
    if len(raw_pts) < 2:
        raise ValueError("swept_tube points collapsed to fewer than 2 distinct points")

    if smooth_subdiv > 0:
        pts = catmull_rom_spline(raw_pts, samples_per_seg=smooth_subdiv, closed=closed)
        rad = _resample_radii(raw_rad, len(raw_pts) if closed else len(raw_pts) - 1, smooth_subdiv, closed)
    else:
        pts, rad = raw_pts, raw_rad

    radial_segs, cap_rings = max(5, int(radial_segs)), max(2, int(cap_rings))
    tangents, f_n, f_b = _parallel_transport_frames(pts, closed)
    angles = np.linspace(0.0, 2.0 * math.pi, radial_segs, endpoint=False)
    ca, sa = np.cos(angles)[:, None], np.sin(angles)[:, None]
    u_ring = _mirrored_theta_u(angles, 2.0 * math.pi * float(rad.mean()))

    seg_lens = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    arc_s = np.concatenate([[0.0], np.cumsum(seg_lens)])

    rings: List[np.ndarray] = []
    ring_vs: List[float] = []
    start_apex: Optional[np.ndarray] = None
    end_apex: Optional[np.ndarray] = None
    start_v = 0.0

    if not closed and cap_start:
        r0, r_ax = rad[0], float(0.5 * (rad[0, 0] + rad[0, 1]))
        start_apex = pts[0] - r_ax * tangents[0]
        start_v = -0.5 * math.pi * r_ax * DEFAULT_UV_SCALE
        for k in range(cap_rings - 1, 0, -1):
            phi = (k / cap_rings) * (0.5 * math.pi)
            c_phi, s_phi = math.cos(phi), math.sin(phi)
            ctr = pts[0] - (r_ax * s_phi) * tangents[0]
            rings.append(ctr[None, :] + (r0[0] * c_phi) * ca * f_n[0][None, :] + (r0[1] * c_phi) * sa * f_b[0][None, :])
            ring_vs.append(-phi * r_ax * DEFAULT_UV_SCALE)

    for i in range(len(pts)):
        rings.append(pts[i][None, :] + rad[i, 0] * ca * f_n[i][None, :] + rad[i, 1] * sa * f_b[i][None, :])
        ring_vs.append(float(arc_s[i] * DEFAULT_UV_SCALE))

    end_v = float(arc_s[-1] * DEFAULT_UV_SCALE)
    if not closed and cap_end:
        rn, r_ax = rad[-1], float(0.5 * (rad[-1, 0] + rad[-1, 1]))
        for k in range(1, cap_rings):
            phi = (k / cap_rings) * (0.5 * math.pi)
            c_phi, s_phi = math.cos(phi), math.sin(phi)
            ctr = pts[-1] + (r_ax * s_phi) * tangents[-1]
            rings.append(ctr[None, :] + (rn[0] * c_phi) * ca * f_n[-1][None, :] + (rn[1] * c_phi) * sa * f_b[-1][None, :])
            ring_vs.append(float((arc_s[-1] + phi * r_ax) * DEFAULT_UV_SCALE))
        end_apex = pts[-1] + r_ax * tangents[-1]
        end_v = float((arc_s[-1] + 0.5 * math.pi * r_ax) * DEFAULT_UV_SCALE)

    u_mid = float(u_ring.mean())
    blocks: List[np.ndarray] = ([] if start_apex is None else [start_apex[None, :]]) + rings
    uv_blocks: List[np.ndarray] = ([] if start_apex is None else [np.array([[u_mid, start_v]], dtype=np.float32)]) + [
        np.column_stack([u_ring, np.full(radial_segs, v_val, dtype=np.float32)]) for v_val in ring_vs
    ]
    if end_apex is not None:
        blocks.append(end_apex[None, :])
        uv_blocks.append(np.array([[u_mid, end_v]], dtype=np.float32))
    positions = np.vstack(blocks).astype(np.float32)
    uvs = np.vstack(uv_blocks).astype(np.float32)

    ring_off = 1 if start_apex is not None else 0
    n_rings = len(rings)
    tris: List[Tuple[int, int, int]] = []
    if start_apex is not None:
        tris.extend((0, ring_off + (i + 1) % radial_segs, ring_off + i) for i in range(radial_segs))
    for r in range(n_rings if closed else n_rings - 1):
        tris.extend(_quad_ring_tris(ring_off + r * radial_segs, ring_off + ((r + 1) % n_rings) * radial_segs, radial_segs))
    if end_apex is not None:
        end_idx = ring_off + n_rings * radial_segs
        b_last = ring_off + (n_rings - 1) * radial_segs
        tris.extend((end_idx, b_last + i, b_last + (i + 1) % radial_segs) for i in range(radial_segs))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices, uvs=uvs)


def curved_tube(
    control_points: Sequence[Vec3Like],
    radii: Union[float, Sequence[float], Sequence[Tuple[float, float]]],
    samples_per_seg: int = 6,
    radial_segs: int = 14,
    cap_start: bool = True,
    cap_end: bool = True,
    closed: bool = False,
    *,
    cap_rings: int = 4,
    name: str = "curved_tube",
    material: str = "chitin_dark",
) -> MeshPart:
    """Convenience wrapper around `swept_tube` with Catmull-Rom smoothing enabled."""
    return swept_tube(
        control_points, radii, radial_segs=radial_segs, cap_start=cap_start, cap_end=cap_end,
        closed=closed, smooth_subdiv=samples_per_seg, cap_rings=cap_rings, name=name, material=material,
    )


def lathe(
    profile_rz: Sequence[Tuple[float, float]],
    radial_segs: int = 28,
    cap_bottom: bool = True,
    cap_top: bool = True,
    *,
    axis: str = "y",
    name: str = "lathe",
    material: str = "gear_gold",
) -> MeshPart:
    """Build a smooth body of revolution from `(radius, height)` pairs around `axis`."""
    prof = [(max(0.0, float(r)), float(h)) for r, h in profile_rz]
    if len(prof) < 2:
        raise ValueError("lathe requires at least 2 profile points")
    radial_segs = max(6, int(radial_segs))
    thetas = np.linspace(0.0, 2.0 * math.pi, radial_segs, endpoint=False)
    sin_t, cos_t = np.sin(thetas), np.cos(thetas)
    u_ring = _mirrored_theta_u(thetas, 2.0 * math.pi * max([r for r, _ in prof] or [0.05]))
    u_mid = float(u_ring.mean())

    verts: List[np.ndarray] = []
    uvs_l: List[np.ndarray] = []
    ring_meta: List[Tuple[str, int]] = []
    v_count, arc_v = 0, 0.0
    for j, (r, h) in enumerate(prof):
        if j > 0:
            arc_v += math.hypot(r - prof[j - 1][0], h - prof[j - 1][1]) * DEFAULT_UV_SCALE
        if r <= 1e-6:
            verts.append(np.array([[0.0, h, 0.0]], dtype=np.float64))
            uvs_l.append(np.array([[u_mid, arc_v]], dtype=np.float32))
            ring_meta.append(("pole", v_count))
            v_count += 1
        else:
            verts.append(np.column_stack([r * sin_t, np.full(radial_segs, h), r * cos_t]))
            uvs_l.append(np.column_stack([u_ring, np.full(radial_segs, arc_v, dtype=np.float32)]))
            ring_meta.append(("ring", v_count))
            v_count += radial_segs

    tris: List[Tuple[int, int, int]] = []
    for j in range(len(prof) - 1):
        (k0, i0), (k1, i1) = ring_meta[j], ring_meta[j + 1]
        if k0 == "pole" and k1 == "ring":
            tris.extend((i0, i1 + (i + 1) % radial_segs, i1 + i) for i in range(radial_segs))
        elif k0 == "ring" and k1 == "pole":
            tris.extend((i1, i0 + i, i0 + (i + 1) % radial_segs) for i in range(radial_segs))
        elif k0 == "ring" and k1 == "ring":
            tris.extend(_quad_ring_tris(i0, i1, radial_segs))

    wall_pos = np.vstack(verts).astype(np.float32)
    wall_uv = np.vstack(uvs_l).astype(np.float32)
    wall_idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    parts = [MeshPart(name, material, wall_pos, recompute_smooth_normals(wall_pos, wall_idx), wall_idx, uvs=wall_uv)]

    for want_cap, (r, h), ny, rev in (
        (cap_bottom, prof[0], -1.0, True),
        (cap_top, prof[-1], +1.0, False),
    ):
        if want_cap and r > 1e-6:
            cp = np.vstack([np.array([[0.0, h, 0.0]]), np.column_stack([r * sin_t, np.full(radial_segs, h), r * cos_t])])
            cn = np.tile(np.array([[0.0, ny, 0.0]]), (radial_segs + 1, 1))
            cuv = np.column_stack([cp[:, 0] * DEFAULT_UV_SCALE, cp[:, 2] * DEFAULT_UV_SCALE]).astype(np.float32)
            ci = [(0, 1 + (i + 1) % radial_segs, 1 + i) if rev else (0, 1 + i, 1 + (i + 1) % radial_segs) for i in range(radial_segs)]
            parts.append(MeshPart(f"{name}_cap", material, cp.astype(np.float32), cn.astype(np.float32), np.asarray(ci, dtype=np.uint32).reshape(-1), uvs=cuv))

    mesh = merge_parts(parts, name=name, material=material)
    is_closed = (cap_bottom or prof[0][0] <= 1e-6) and (cap_top or prof[-1][0] <= 1e-6)
    if is_closed and signed_volume(mesh.positions, mesh.indices) < 0.0:
        mesh = MeshPart(mesh.name, mesh.material, mesh.positions, -mesh.normals, mesh.indices.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1), uvs=mesh.uvs, colors=mesh.colors)

    if axis == "z":
        return mesh.rotate_xyz(rx=0.5 * math.pi)
    if axis == "x":
        return mesh.rotate_xyz(rz=-0.5 * math.pi)
    if axis != "y":
        raise ValueError(f"axis must be 'x', 'y', or 'z', got {axis!r}")
    return mesh


def cylinder(
    radius_bottom: float, radius_top: float, height: float, radial_segs: int = 24, *,
    cap_bottom: bool = True, cap_top: bool = True, bevel: float = 0.0, axis: str = "y",
    name: str = "cylinder", material: str = "gear_gold",
) -> MeshPart:
    """Build a centered cylinder or frustum with optional end chamfers."""
    h0, h1 = -0.5 * height, 0.5 * height
    b = max(0.0, min(float(bevel), 0.35 * height, 0.35 * min(radius_bottom, radius_top)))
    prof = (
        [(max(0.0, radius_bottom - b), h0), (radius_bottom, h0 + b), (radius_top, h1 - b), (max(0.0, radius_top - b), h1)]
        if b > 1e-6
        else [(radius_bottom, h0), (radius_top, h1)]
    )
    return lathe(prof, radial_segs=radial_segs, cap_bottom=cap_bottom, cap_top=cap_top, axis=axis, name=name, material=material)


def cone(
    radius: float, height: float, radial_segs: int = 24, *,
    cap_bottom: bool = True, axis: str = "y", name: str = "cone", material: str = "gear_gold",
) -> MeshPart:
    return lathe([(radius, -0.5 * height), (0.0, 0.5 * height)], radial_segs=radial_segs, cap_bottom=cap_bottom, cap_top=False, axis=axis, name=name, material=material)


def capsule(
    radius: float, length: float, radial_segs: int = 20, cap_rings: int = 6, *,
    axis: str = "y", name: str = "capsule", material: str = "chitin_dark",
) -> MeshPart:
    """Build a smooth rounded capsule centered at origin (cylindrical span = `length`)."""
    half = 0.5 * max(0.0, float(length))
    prof = [(radius * math.cos(-0.5 * math.pi + k / cap_rings * 0.5 * math.pi), -half + radius * math.sin(-0.5 * math.pi + k / cap_rings * 0.5 * math.pi)) for k in range(cap_rings + 1)]
    prof += [(radius * math.cos(k / cap_rings * 0.5 * math.pi), +half + radius * math.sin(k / cap_rings * 0.5 * math.pi)) for k in range(cap_rings + 1)]
    return lathe(prof, radial_segs=radial_segs, cap_bottom=False, cap_top=False, axis=axis, name=name, material=material)


def torus_arc(
    major_r: float, minor_r: float, start_ang: float = 0.0, end_ang: float = 2.0 * math.pi,
    u_segs: int = 32, v_segs: int = 14, *, cap_ends: bool = True, plane: str = "xy",
    name: str = "torus_arc", material: str = "gear_gold",
) -> MeshPart:
    """Build a full torus or capped toroidal arc in the XY (default), XZ, or YZ plane."""
    u_segs, v_segs = max(6, int(u_segs)), max(6, int(v_segs))
    span = float(end_ang - start_ang)
    if abs(span) <= 1e-6:
        raise ValueError("torus_arc requires a non-zero angular span")
    if span < 0.0:
        start_ang, end_ang, span = end_ang, start_ang, -span

    if abs(span - 2.0 * math.pi) >= 1e-4:
        us = np.linspace(start_ang, end_ang, u_segs + 1)
        pts = np.column_stack([major_r * np.cos(us), major_r * np.sin(us), np.zeros_like(us)])
        mesh = swept_tube(pts, minor_r, radial_segs=v_segs, cap_start=cap_ends, cap_end=cap_ends, closed=False, name=name, material=material)
    else:
        us = np.linspace(0.0, 2.0 * math.pi, u_segs, endpoint=False)
        vs = np.linspace(0.0, 2.0 * math.pi, v_segs, endpoint=False)
        rad = major_r + minor_r * np.cos(vs)[None, :]
        positions = np.column_stack([
            (rad * np.cos(us)[:, None]).reshape(-1),
            (rad * np.sin(us)[:, None]).reshape(-1),
            np.broadcast_to(minor_r * np.sin(vs)[None, :], (u_segs, v_segs)).reshape(-1),
        ]).astype(np.float32)
        uu = _mirrored_theta_u(us, 2.0 * math.pi * major_r)[:, None]
        vv = _mirrored_theta_u(vs, 2.0 * math.pi * minor_r)[None, :]
        uvs = np.column_stack([
            np.broadcast_to(uu, (u_segs, v_segs)).reshape(-1),
            np.broadcast_to(vv, (u_segs, v_segs)).reshape(-1),
        ]).astype(np.float32)
        tris: List[Tuple[int, int, int]] = []
        for i in range(u_segs):
            b0, b1 = i * v_segs, ((i + 1) % u_segs) * v_segs
            for j in range(v_segs):
                nj = (j + 1) % v_segs
                tris.append((b0 + j, b1 + j, b1 + nj))
                tris.append((b0 + j, b1 + nj, b0 + nj))
        indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
        mesh = MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices, uvs=uvs)

    if plane == "xz":
        return mesh.rotate_xyz(rx=0.5 * math.pi)
    if plane == "yz":
        return mesh.rotate_xyz(ry=0.5 * math.pi)
    if plane != "xy":
        raise ValueError(f"plane must be 'xy', 'xz', or 'yz', got {plane!r}")
    return mesh


def rounded_box(
    sx: float, sy: float, sz: float, bevel: float = 0.15, segs: int = 3, *,
    name: str = "rounded_box", material: str = "gear_gold",
) -> MeshPart:
    """Build a watertight beveled box centered at origin with exact size (sx, sy, sz)."""
    hx, hy, hz = 0.5 * float(sx), 0.5 * float(sy), 0.5 * float(sz)
    b = max(0.0, min(float(bevel), 0.49 * min(sx, sy, sz)))
    segs = max(1, int(segs))

    if b <= 1e-6:
        faces = [
            ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
            ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]),
            ((1, 0, 0), [(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)]),
            ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)]),
            ((0, 1, 0), [(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)]),
            ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]),
        ]
        pos_l, nrm_l, uv_l, idx_l = [], [], [], []
        face_dims = ((sx, sy), (sx, sy), (sz, sy), (sz, sy), (sx, sz), (sx, sz))
        for fi, (nrm, corners) in enumerate(faces):
            base = fi * 4
            fu, fv = face_dims[fi][0] * DEFAULT_UV_SCALE, face_dims[fi][1] * DEFAULT_UV_SCALE
            pos_l.extend(corners)
            nrm_l.extend([nrm] * 4)
            uv_l.extend([(0.0, 0.0), (fu, 0.0), (fu, fv), (0.0, fv)])
            idx_l.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        return MeshPart(name, material, np.asarray(pos_l, dtype=np.float32), np.asarray(nrm_l, dtype=np.float32), np.asarray(idx_l, dtype=np.uint32), uvs=np.asarray(uv_l, dtype=np.float32))

    # Exact Minkowski sum of inner box (hx-b, hy-b, hz-b) and sphere of radius b.
    ix, iy, iz = hx - b, hy - b, hz - b
    n_grid = 2 * segs + 1
    core_vals = [(-1.0 if k <= segs else 1.0) for k in range(n_grid + 1)]
    dir_vals = [
        (-math.tan(((segs - k) / segs) * 0.25 * math.pi) if k <= segs else math.tan(((k - segs - 1) / segs) * 0.25 * math.pi))
        for k in range(n_grid + 1)
    ]

    # Per-face vertex table keeps analytic smooth corner normals while giving distortion-free planar UVs.
    face_maps = (
        (lambda u, v: (u, v, n_grid), lambda x, y, z: (x, y)),
        (lambda u, v: (n_grid - u, v, 0), lambda x, y, z: (-x, y)),
        (lambda u, v: (n_grid, v, n_grid - u), lambda x, y, z: (-z, y)),
        (lambda u, v: (0, v, u), lambda x, y, z: (z, y)),
        (lambda u, v: (u, n_grid, n_grid - v), lambda x, y, z: (x, -z)),
        (lambda u, v: (u, 0, v), lambda x, y, z: (x, z)),
    )
    pos_l, nrm_l, uv_l, tris = [], [], [], []
    for fi, (fmap, uv_proj) in enumerate(face_maps):
        f_base = len(pos_l)
        for v in range(n_grid + 1):
            for u in range(n_grid + 1):
                gx, gy, gz = fmap(u, v)
                dx, dy, dz = dir_vals[gx], dir_vals[gy], dir_vals[gz]
                dlen = math.sqrt(dx * dx + dy * dy + dz * dz)
                nx, ny, nz = dx / dlen, dy / dlen, dz / dlen
                px, py, pz = core_vals[gx] * ix + b * nx, core_vals[gy] * iy + b * ny, core_vals[gz] * iz + b * nz
                pos_l.append((px, py, pz))
                nrm_l.append((nx, ny, nz))
                pu, pv = uv_proj(px, py, pz)
                uv_l.append((pu * DEFAULT_UV_SCALE, pv * DEFAULT_UV_SCALE))
        stride = n_grid + 1
        for v in range(n_grid):
            for u in range(n_grid):
                v00 = f_base + v * stride + u
                v10, v11, v01 = v00 + 1, v00 + stride + 1, v00 + stride
                tris.append((v00, v10, v11))
                tris.append((v00, v11, v01))
    return MeshPart(name, material, np.asarray(pos_l, dtype=np.float32), np.asarray(nrm_l, dtype=np.float32), np.asarray(tris, dtype=np.uint32).reshape(-1), uvs=np.asarray(uv_l, dtype=np.float32))


def hemisphere_shell(
    rx: float, ry: float, rz: float, thickness: float = 0.04, start_phi: float = 0.0,
    u_segs: int = 32, v_segs: int = 14, *, e1: float = 1.0, e2: float = 1.0,
    deform_fn: Optional[Callable[[np.ndarray], np.ndarray]] = None,
    name: str = "shell", material: str = "gear_gold",
) -> MeshPart:
    """Build a watertight thickened dome/helmet shell with a beveled bottom rim."""
    u_segs, v_segs = max(8, int(u_segs)), max(4, int(v_segs))
    thickness, e1, e2 = max(1e-3, float(thickness)), max(0.1, float(e1)), max(0.1, float(e2))
    phis = np.linspace(float(start_phi), 0.5 * math.pi, v_segs + 1)[:-1]
    thetas = np.linspace(0.0, 2.0 * math.pi, u_segs, endpoint=False)
    cos_t, sin_t = _spow(np.cos(thetas), e2), _spow(np.sin(thetas), e2)

    outer_rings = [
        np.column_stack([rx * float(_spow(np.array(math.cos(phi)), e1)) * sin_t, np.full(u_segs, ry * float(_spow(np.array(math.sin(phi)), e1))), rz * float(_spow(np.array(math.cos(phi)), e1)) * cos_t])
        for phi in phis
    ]
    n_rings = len(outer_rings)
    p_out = np.vstack(outer_rings + [np.array([[0.0, ry, 0.0]], dtype=np.float64)])
    if deform_fn is not None:
        p_out = np.asarray(deform_fn(p_out.copy()), dtype=np.float64).reshape(-1, 3)

    outer_tris: List[Tuple[int, int, int]] = []
    for r in range(n_rings - 1):
        outer_tris.extend(_quad_ring_tris(r * u_segs, (r + 1) * u_segs, u_segs))
    apex_out, top_b = n_rings * u_segs, (n_rings - 1) * u_segs
    outer_tris.extend((apex_out, top_b + i, top_b + (i + 1) % u_segs) for i in range(u_segs))

    n_out = recompute_smooth_normals(p_out.astype(np.float32), np.asarray(outer_tris, dtype=np.uint32)).astype(np.float64)
    p_in = p_out - thickness * n_out
    rim_down = _normalize_rows(p_out[:u_segs] - p_out[u_segs:2 * u_segs], fallback=(0.0, -1.0, 0.0))
    p_lip = 0.5 * (p_out[:u_segs] + p_in[:u_segs]) + (0.35 * thickness) * rim_down

    in_off, lip_off = len(p_out), 2 * len(p_out)
    positions = np.vstack([p_out, p_in, p_lip]).astype(np.float32)
    all_tris = list(outer_tris) + [(in_off + a, in_off + c, in_off + b) for a, b, c in outer_tris]
    all_tris.extend(_quad_ring_tris(lip_off, 0, u_segs))
    all_tris.extend(_quad_ring_tris(in_off, lip_off, u_segs))

    u_ring = _mirrored_theta_u(thetas, math.pi * (rx + rz))
    v_scale = math.pi * 0.5 * (ry + 0.5 * (rx + rz)) * DEFAULT_UV_SCALE
    uv_out = np.vstack([
        np.column_stack([u_ring, np.full(u_segs, float((phi - start_phi) / max(0.2, 0.5 * math.pi - start_phi) * v_scale * 0.5), dtype=np.float32)])
        for phi in phis
    ] + [np.array([[float(u_ring.mean()), float(v_scale * 0.5)]], dtype=np.float32)])
    uv_lip = np.column_stack([u_ring, np.full(u_segs, -0.04, dtype=np.float32)])
    uvs = np.vstack([uv_out, uv_out, uv_lip]).astype(np.float32)

    indices = ensure_outward_winding(positions, np.asarray(all_tris, dtype=np.uint32).reshape(-1))
    return MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices, uvs=uvs)


def _ear_clip_2d(pts: List[Tuple[float, float]]) -> List[Tuple[int, int, int]]:
    def cross(o, u, v):
        return (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0])

    rem, tris, guard = list(range(len(pts))), [], 0
    while len(rem) > 3 and guard < len(pts) * len(pts) + 10:
        guard += 1
        clipped = False
        m = len(rem)
        for k in range(m):
            ia, ib, ic = rem[(k - 1) % m], rem[k], rem[(k + 1) % m]
            a, b, c = pts[ia], pts[ib], pts[ic]
            if cross(a, b, c) <= 1e-10:
                continue
            if any(cross(a, b, pts[j]) >= -1e-9 and cross(b, c, pts[j]) >= -1e-9 and cross(c, a, pts[j]) >= -1e-9 for j in rem if j not in (ia, ib, ic)):
                continue
            tris.append((ia, ib, ic))
            rem.pop(k)
            clipped = True
            break
        if not clipped:
            break
    if len(rem) == 3:
        tris.append((rem[0], rem[1], rem[2]))
    return tris


def extrude_polygon(
    points_2d: Sequence[Tuple[float, float]], depth: float, *, name: str = "extrude", material: str = "gear_accent"
) -> MeshPart:
    """Extrude a simple 2D polygon along Z in [-depth/2, +depth/2] with crisp normals."""
    pts = [(float(x), float(y)) for x, y in points_2d]
    if len(pts) < 3:
        raise ValueError("extrude_polygon requires at least 3 2D points")
    area2 = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    if area2 < 0.0:
        pts.reverse()
    cap_tris = _ear_clip_2d(pts)
    hz, n = 0.5 * float(depth), len(pts)

    pos_l = [(x, y, +hz) for x, y in pts] + [(x, y, -hz) for x, y in pts]
    nrm_l = [(0.0, 0.0, 1.0)] * n + [(0.0, 0.0, -1.0)] * n
    uv_l = [(x * DEFAULT_UV_SCALE, y * DEFAULT_UV_SCALE) for x, y in pts] * 2
    idx_l: List[int] = []
    for a, b, c in cap_tris:
        idx_l.extend([a, b, c, n + a, n + c, n + b])

    perim = 0.0
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        elen = math.hypot(x1 - x0, y1 - y0)
        if elen <= 1e-10:
            continue
        nx, ny = (y1 - y0) / elen, -(x1 - x0) / elen
        sb = len(pos_l)
        pos_l.extend([(x0, y0, -hz), (x1, y1, -hz), (x1, y1, +hz), (x0, y0, +hz)])
        nrm_l.extend([(nx, ny, 0.0)] * 4)
        u0, u1 = perim * DEFAULT_UV_SCALE, (perim + elen) * DEFAULT_UV_SCALE
        uv_l.extend([(u0, 0.0), (u1, 0.0), (u1, depth * DEFAULT_UV_SCALE), (u0, depth * DEFAULT_UV_SCALE)])
        perim += elen
        idx_l.extend([sb, sb + 1, sb + 2, sb, sb + 2, sb + 3])
    return MeshPart(name, material, np.asarray(pos_l, dtype=np.float32), np.asarray(nrm_l, dtype=np.float32), np.asarray(idx_l, dtype=np.uint32), uvs=np.asarray(uv_l, dtype=np.float32))


# ---------------------------------------------------------------------------
# Fast vectorized baked crevice and contact Ambient Occlusion (COLOR_0)
# ---------------------------------------------------------------------------

def _box_blur_3d(grid: np.ndarray, passes: int) -> np.ndarray:
    g = grid.astype(np.float32, copy=True)
    for _ in range(passes):
        for axis in (0, 1, 2):
            p = np.pad(g, [(1, 1) if a == axis else (0, 0) for a in range(3)], mode="edge")
            sl0 = [slice(0, -2) if a == axis else slice(None) for a in range(3)]
            sl1 = [slice(1, -1) if a == axis else slice(None) for a in range(3)]
            sl2 = [slice(2, None) if a == axis else slice(None) for a in range(3)]
            g = (p[tuple(sl0)] + 2.0 * p[tuple(sl1)] + p[tuple(sl2)]) * 0.25
    return g


def _sample_grid_trilinear(grid: np.ndarray, coords: np.ndarray) -> np.ndarray:
    res = grid.shape[0]
    c = np.clip(coords, 0.0, float(res - 1.001))
    i0 = np.floor(c).astype(np.int32)
    f = (c - i0).astype(np.float32)
    i1 = np.minimum(i0 + 1, res - 1)
    x0, y0, z0 = i0[:, 0], i0[:, 1], i0[:, 2]
    x1, y1, z1 = i1[:, 0], i1[:, 1], i1[:, 2]
    fx, fy, fz = f[:, 0], f[:, 1], f[:, 2]
    c00 = grid[x0, y0, z0] * (1.0 - fx) + grid[x1, y0, z0] * fx
    c10 = grid[x0, y1, z0] * (1.0 - fx) + grid[x1, y1, z0] * fx
    c01 = grid[x0, y0, z1] * (1.0 - fx) + grid[x1, y0, z1] * fx
    c11 = grid[x0, y1, z1] * (1.0 - fx) + grid[x1, y1, z1] * fx
    return (c00 * (1.0 - fy) + c10 * fy) * (1.0 - fz) + (c01 * (1.0 - fy) + c11 * fy) * fz


def _local_concavity(positions: np.ndarray, normals: np.ndarray, indices: np.ndarray) -> np.ndarray:
    """Measure local mesh concavity in [0, 1] for crisp groove/seam shading."""
    n = len(positions)
    if n == 0 or len(indices) == 0:
        return np.zeros(n, dtype=np.float32)
    pos = positions.astype(np.float64)
    idx = indices.reshape(-1, 3)
    acc = np.zeros_like(pos)
    cnt = np.zeros(n, dtype=np.float64)
    for a, b in ((0, 1), (1, 2), (2, 0)):
        ia, ib = idx[:, a], idx[:, b]
        np.add.at(acc, ia, pos[ib])
        np.add.at(acc, ib, pos[ia])
        np.add.at(cnt, ia, 1.0)
        np.add.at(cnt, ib, 1.0)
    avg = acc / np.maximum(1.0, cnt[:, None])
    conc = np.sum((avg - pos) * normals.astype(np.float64), axis=1)
    return np.clip(conc * 38.0, 0.0, 1.0).astype(np.float32)


def bake_scene_ao(scene: SceneBuilder, *, grid_res: int = 56) -> SceneBuilder:
    """Bake inter-part and crevice Ambient Occlusion into every MeshPart.colors (COLOR_0).

    Excludes translucent BLEND glass materials as occluders so eyes behind scuba
    masks and goggles stay crisp and bright.
    """
    bmin, bmax = scene.bounds()
    pad = 0.14
    origin = bmin.astype(np.float64) - pad
    extent = np.maximum(0.25, (bmax - bmin).astype(np.float64) + 2.0 * pad)
    step = float(extent.max()) / float(grid_res - 1)
    inv_step = 1.0 / step

    # Collect opaque world-space sample points (vertices + triangle centroids).
    occ_pts: List[np.ndarray] = []
    for k, parts in scene.nodes.items():
        xf = scene.node_transforms.get(k)
        mat = xf.to_matrix() if xf is not None else None
        for p in parts:
            spec = get_material(p.material)
            if spec.resolved_alpha_mode() == "BLEND":
                continue
            wp = p.transform(mat) if mat is not None else p
            occ_pts.append(wp.positions.astype(np.float64))
            idx = wp.indices.reshape(-1, 3)
            if len(idx) > 0:
                tri_c = (wp.positions[idx[:, 0]] + wp.positions[idx[:, 1]] + wp.positions[idx[:, 2]]) / 3.0
                occ_pts.append(tri_c.astype(np.float64))

    if not occ_pts:
        return scene

    all_occ = np.vstack(occ_pts)
    grid = np.zeros((grid_res, grid_res, grid_res), dtype=np.float32)
    vox = np.clip(np.round((all_occ - origin[None, :]) * inv_step).astype(np.int32), 0, grid_res - 1)
    np.add.at(grid, (vox[:, 0], vox[:, 1], vox[:, 2]), 0.45)
    grid = np.clip(grid, 0.0, 1.0)
    occ_fine = _box_blur_3d(grid, 1)
    occ_broad = _box_blur_3d(grid, 3)

    for k, parts in scene.nodes.items():
        xf = scene.node_transforms.get(k)
        mat = xf.to_matrix() if xf is not None else None
        for p in parts:
            spec = get_material(p.material)
            if spec.resolved_alpha_mode() == "BLEND":
                p.colors = np.ones((len(p.positions), 3), dtype=np.float32)
                continue
            wp = p.transform(mat) if mat is not None else p
            pos = wp.positions.astype(np.float64)
            nrm = wp.normals.astype(np.float64)

            # Build per-vertex tangent frame for tilted hemisphere probes.
            ref = np.where(np.abs(nrm[:, 1:2]) < 0.85, np.array([[0.0, 1.0, 0.0]]), np.array([[1.0, 0.0, 0.0]]))
            t1 = _normalize_rows(np.cross(nrm, ref))
            t2 = np.cross(nrm, t1)

            occ = np.zeros(len(pos), dtype=np.float32)
            # Direct outward probes + 4 tilted hemisphere probes catch brims, sockets, seams, joints.
            for r_off, w, g_src in ((0.032, 0.24, occ_fine), (0.068, 0.24, occ_broad), (0.120, 0.16, occ_broad)):
                occ += w * _sample_grid_trilinear(g_src, (pos + r_off * nrm - origin[None, :]) * inv_step)
            for d_vec in (nrm + 0.72 * t1, nrm - 0.72 * t1, nrm + 0.72 * t2, nrm - 0.72 * t2):
                d_u = _normalize_rows(d_vec)
                occ += 0.09 * _sample_grid_trilinear(occ_broad, (pos + 0.064 * d_u - origin[None, :]) * inv_step)

            conc = _local_concavity(wp.positions, wp.normals, wp.indices)
            gnd = np.clip(pos[:, 1] / 0.10, 0.0, 1.0).astype(np.float32)
            gnd_ao = 0.82 + 0.18 * gnd

            ao = (1.0 - np.clip(occ * 0.72 + conc * 0.24, 0.0, 0.58)) * gnd_ao
            floor_ao = 0.78 if p.material in ("eye_white", "eye_iris", "eye_pupil") else 0.44
            ao = np.clip(ao, floor_ao, 1.0).astype(np.float32)
            p.colors = np.column_stack([ao, ao, ao]).astype(np.float32)
    return scene

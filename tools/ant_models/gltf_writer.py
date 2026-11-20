"""Strictly valid glTF 2.0 binary (.glb) writer and pure-stdlib validator.

This module uses only the Python standard library so CI environments without
numpy or Pillow can still run structural .glb validation unconditionally.
"""
from __future__ import annotations

import json
import math
import os
import struct
from typing import Any, Dict, List, Sequence, Tuple, Union

try:
    from .materials import MATERIAL_ORDER, get_material
except ImportError:
    from materials import MATERIAL_ORDER, get_material  # type: ignore

GLB_MAGIC = 0x46546C67  # b"glTF"
GLB_VERSION = 2
CHUNK_JSON = 0x4E4F534A  # b"JSON"
CHUNK_BIN = 0x004E4942  # b"BIN\0"

ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

COMPONENT_UNSIGNED_SHORT = 5123
COMPONENT_UNSIGNED_INT = 5125
COMPONENT_FLOAT = 5126


def _pad4(data: bytes, pad_byte: bytes) -> bytes:
    rem = len(data) % 4
    return data if rem == 0 else data + pad_byte * (4 - rem)


def _pack_vec3_f32(arr: Any) -> Tuple[bytes, int, List[float], List[float]]:
    """Serialize (N, 3) float32 array/sequence to little-endian bytes + bounds."""
    if hasattr(arr, "astype") and hasattr(arr, "tobytes"):
        a = arr.astype("<f4", copy=False).reshape(-1, 3)
        count = int(a.shape[0])
        if count == 0:
            return b"", 0, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
        raw = a.tobytes()
        vmin = [float(x) for x in a.min(axis=0)]
        vmax = [float(x) for x in a.max(axis=0)]
        for c in vmin + vmax:
            if not math.isfinite(c):
                raise ValueError("Non-finite vertex coordinate encountered during GLB export")
        return raw, count, vmin, vmax

    rows = list(arr)
    count = len(rows)
    if count == 0:
        return b"", 0, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
    buf = bytearray()
    vmin = [math.inf, math.inf, math.inf]
    vmax = [-math.inf, -math.inf, -math.inf]
    for x, y, z in rows:
        fx, fy, fz = float(x), float(y), float(z)
        if not (math.isfinite(fx) and math.isfinite(fy) and math.isfinite(fz)):
            raise ValueError("Non-finite vertex coordinate encountered during GLB export")
        buf.extend(struct.pack("<3f", fx, fy, fz))
        if fx < vmin[0]:
            vmin[0] = fx
        if fy < vmin[1]:
            vmin[1] = fy
        if fz < vmin[2]:
            vmin[2] = fz
        if fx > vmax[0]:
            vmax[0] = fx
        if fy > vmax[1]:
            vmax[1] = fy
        if fz > vmax[2]:
            vmax[2] = fz
    return bytes(buf), count, vmin, vmax


def _pack_u32(arr: Any, offset: int = 0) -> Tuple[bytes, int, int, int]:
    """Serialize uint32 indices with optional vertex index offset."""
    if hasattr(arr, "astype") and hasattr(arr, "tobytes"):
        a = arr.astype("<u4", copy=False).reshape(-1)
        if offset:
            a = (a + offset).astype("<u4", copy=False)
        count = int(a.size)
        if count == 0:
            return b"", 0, 0, 0
        return a.tobytes(), count, int(a.min()), int(a.max())

    vals = [int(v) + offset for v in arr]
    if not vals:
        return b"", 0, 0, 0
    raw = struct.pack(f"<{len(vals)}I", *vals)
    return raw, len(vals), min(vals), max(vals)


def _merge_primitive_streams(parts: Sequence[Any]) -> Tuple[bytes, bytes, bytes, int, int, List[float], List[float], int, int]:
    """Concatenate positions, normals, and offset indices for same-material parts."""
    pos_buf = bytearray()
    nrm_buf = bytearray()
    idx_buf = bytearray()
    v_total = 0
    i_total = 0
    pmin = [math.inf, math.inf, math.inf]
    pmax = [-math.inf, -math.inf, -math.inf]
    imin = math.inf
    imax = -math.inf

    for part in parts:
        p_raw, v_cnt, mn, mx = _pack_vec3_f32(part.positions)
        n_raw, n_cnt, _, _ = _pack_vec3_f32(part.normals)
        if v_cnt != n_cnt:
            raise ValueError(f"Part {part.name!r}: position count {v_cnt} != normal count {n_cnt}")
        if v_cnt == 0:
            continue
        i_raw, i_cnt, lo, hi = _pack_u32(part.indices, offset=v_total)
        if i_cnt == 0:
            continue
        pos_buf.extend(p_raw)
        nrm_buf.extend(n_raw)
        idx_buf.extend(i_raw)
        for k in range(3):
            if mn[k] < pmin[k]:
                pmin[k] = mn[k]
            if mx[k] > pmax[k]:
                pmax[k] = mx[k]
        if lo < imin:
            imin = lo
        if hi > imax:
            imax = hi
        v_total += v_cnt
        i_total += i_cnt

    if v_total == 0 or i_total == 0:
        return b"", b"", b"", 0, 0, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], 0, 0
    return bytes(pos_buf), bytes(nrm_buf), bytes(idx_buf), v_total, i_total, pmin, pmax, int(imin), int(imax)


def _pack_f32_scalars(vals: Sequence[float]) -> Tuple[bytes, int, float, float]:
    nums = [float(v) for v in vals]
    if not nums:
        return b"", 0, 0.0, 0.0
    for v in nums:
        if not math.isfinite(v):
            raise ValueError("Non-finite animation timestamp encountered")
    return struct.pack(f"<{len(nums)}f", *nums), len(nums), min(nums), max(nums)


def _pack_f32_tuples(vals: Sequence[Sequence[float]], width: int) -> Tuple[bytes, int]:
    buf = bytearray()
    for row in vals:
        if len(row) != width:
            raise ValueError(f"Expected {width}-tuple in animation track, got {len(row)}")
        f_row = [float(c) for c in row]
        if not all(math.isfinite(c) for c in f_row):
            raise ValueError("Non-finite animation value encountered")
        buf.extend(struct.pack(f"<{width}f", *f_row))
    return bytes(buf), len(vals)


def scene_to_glb_bytes(scene: Any) -> bytes:
    """Serialize a SceneBuilder into strictly valid glTF 2.0 binary (.glb) bytes."""
    used_materials_set = set()
    active_nodes: List[Tuple[str, List[Any]]] = []
    for node_name in scene.node_names():
        parts = [p for p in scene.get_node(node_name) if len(p.indices) > 0]
        if parts:
            active_nodes.append((node_name, parts))
            for p in parts:
                used_materials_set.add(p.material)

    if not active_nodes:
        raise ValueError(f"Scene {scene.name!r} contains no triangles to export")

    ordered_materials = [m for m in MATERIAL_ORDER if m in used_materials_set]
    for m in sorted(used_materials_set):
        if m not in ordered_materials:
            ordered_materials.append(m)
    mat_index = {name: i for i, name in enumerate(ordered_materials)}
    gltf_materials = [get_material(name).to_gltf_dict() for name in ordered_materials]

    bin_blob = bytearray()
    buffer_views: List[Dict[str, Any]] = []
    accessors: List[Dict[str, Any]] = []
    meshes: List[Dict[str, Any]] = []
    nodes: List[Dict[str, Any]] = [{"name": scene.name, "children": []}]
    node_name_to_idx: Dict[str, int] = {}

    def append_view(raw: bytes, target: int = 0, byte_stride: int = 0) -> int:
        while len(bin_blob) % 4 != 0:
            bin_blob.append(0)
        offset = len(bin_blob)
        bin_blob.extend(raw)
        view: Dict[str, Any] = {
            "buffer": 0,
            "byteOffset": offset,
            "byteLength": len(raw),
        }
        if target:
            view["target"] = target
        if byte_stride:
            view["byteStride"] = byte_stride
        idx = len(buffer_views)
        buffer_views.append(view)
        return idx

    get_xf = getattr(scene, "get_node_transform", None)

    for node_name, parts in active_nodes:
        # Group parts within this logical node by material (preserving order).
        grouped: Dict[str, List[Any]] = {}
        for p in parts:
            grouped.setdefault(p.material, []).append(p)

        primitives: List[Dict[str, Any]] = []
        for mat_name, mat_parts in grouped.items():
            p_raw, n_raw, i_raw, v_cnt, i_cnt, pmin, pmax, imin, imax = _merge_primitive_streams(mat_parts)
            if v_cnt == 0 or i_cnt == 0:
                continue

            pos_bv = append_view(p_raw, ARRAY_BUFFER, byte_stride=12)
            pos_acc = len(accessors)
            accessors.append(
                {
                    "bufferView": pos_bv,
                    "byteOffset": 0,
                    "componentType": COMPONENT_FLOAT,
                    "count": v_cnt,
                    "type": "VEC3",
                    "min": pmin,
                    "max": pmax,
                }
            )

            nrm_bv = append_view(n_raw, ARRAY_BUFFER, byte_stride=12)
            nrm_acc = len(accessors)
            accessors.append(
                {
                    "bufferView": nrm_bv,
                    "byteOffset": 0,
                    "componentType": COMPONENT_FLOAT,
                    "count": v_cnt,
                    "type": "VEC3",
                }
            )

            idx_bv = append_view(i_raw, ELEMENT_ARRAY_BUFFER)
            idx_acc = len(accessors)
            accessors.append(
                {
                    "bufferView": idx_bv,
                    "byteOffset": 0,
                    "componentType": COMPONENT_UNSIGNED_INT,
                    "count": i_cnt,
                    "type": "SCALAR",
                    "min": [imin],
                    "max": [imax],
                }
            )

            primitives.append(
                {
                    "attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc},
                    "indices": idx_acc,
                    "material": mat_index[mat_name],
                    "mode": 4,
                }
            )

        if primitives:
            mesh_idx = len(meshes)
            meshes.append({"name": node_name, "primitives": primitives})
            node_idx = len(nodes)
            node_obj: Dict[str, Any] = {"name": node_name, "mesh": mesh_idx}
            xf = get_xf(node_name) if callable(get_xf) else None
            if xf is not None:
                if any(abs(c) > 1e-7 for c in xf.translation):
                    node_obj["translation"] = [round(float(c), 6) for c in xf.translation]
                # Emit explicit rotation on pivoted nodes (including identity so animated tracks have an explicit baseline).
                node_obj["rotation"] = [round(float(c), 6) for c in xf.rotation]
                if any(abs(c - 1.0) > 1e-7 for c in xf.scale):
                    node_obj["scale"] = [round(float(c), 6) for c in xf.scale]
            nodes.append(node_obj)
            nodes[0]["children"].append(node_idx)
            node_name_to_idx[node_name] = node_idx

    gltf_animations: List[Dict[str, Any]] = []
    for clip in getattr(scene, "animations", ()) or ():
        samplers: List[Dict[str, Any]] = []
        channels: List[Dict[str, Any]] = []
        for ch in clip.channels:
            if ch.target_node not in node_name_to_idx:
                continue
            if ch.path not in ("rotation", "translation", "scale"):
                raise ValueError(f"Unsupported animation path {ch.path!r}")
            t_raw, t_cnt, t_min, t_max = _pack_f32_scalars(ch.times)
            width = 4 if ch.path == "rotation" else 3
            v_raw, v_cnt = _pack_f32_tuples(ch.values, width)
            if t_cnt == 0 or t_cnt != v_cnt:
                raise ValueError(f"Animation channel {ch.target_node}.{ch.path}: time count {t_cnt} != value count {v_cnt}")

            # glTF 2.0 spec: animation bufferViews omit `target`.
            t_bv = append_view(t_raw)
            t_acc = len(accessors)
            accessors.append(
                {
                    "bufferView": t_bv,
                    "byteOffset": 0,
                    "componentType": COMPONENT_FLOAT,
                    "count": t_cnt,
                    "type": "SCALAR",
                    "min": [t_min],
                    "max": [t_max],
                }
            )
            v_bv = append_view(v_raw)
            v_acc = len(accessors)
            accessors.append(
                {
                    "bufferView": v_bv,
                    "byteOffset": 0,
                    "componentType": COMPONENT_FLOAT,
                    "count": v_cnt,
                    "type": "VEC4" if width == 4 else "VEC3",
                }
            )
            s_idx = len(samplers)
            samplers.append({"input": t_acc, "interpolation": ch.interpolation, "output": v_acc})
            channels.append(
                {
                    "sampler": s_idx,
                    "target": {"node": node_name_to_idx[ch.target_node], "path": ch.path},
                }
            )
        if channels:
            gltf_animations.append({"name": clip.name, "samplers": samplers, "channels": channels})

    bin_bytes = _pad4(bytes(bin_blob), b"\x00")
    gltf_doc: Dict[str, Any] = {
        "asset": {
            "version": "2.0",
            "generator": "Ants Procedural 3D Model Builder",
        },
        "scene": 0,
        "scenes": [{"name": scene.name, "nodes": [0]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": gltf_materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(bin_bytes)}],
    }
    if gltf_animations:
        gltf_doc["animations"] = gltf_animations

    json_raw = json.dumps(gltf_doc, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    json_bytes = _pad4(json_raw, b" ")

    total_length = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    header = struct.pack("<III", GLB_MAGIC, GLB_VERSION, total_length)
    json_chunk = struct.pack("<II", len(json_bytes), CHUNK_JSON) + json_bytes
    bin_chunk = struct.pack("<II", len(bin_bytes), CHUNK_BIN) + bin_bytes
    return header + json_chunk + bin_chunk


def write_glb(scene: Any, path: Union[str, os.PathLike]) -> Dict[str, Any]:
    """Write `scene` to a .glb file, validate it, and return validation stats."""
    out_path = os.fspath(path)
    parent = os.path.dirname(out_path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    glb_bytes = scene_to_glb_bytes(scene)
    stats = validate_glb(glb_bytes)
    with open(out_path, "wb") as f:
        f.write(glb_bytes)
    return stats


def _read_accessor_vec3(doc: Dict[str, Any], bin_data: bytes, acc_idx: int) -> List[Tuple[float, float, float]]:
    accessors = doc["accessors"]
    views = doc["bufferViews"]
    if not (0 <= acc_idx < len(accessors)):
        raise ValueError(f"Accessor index {acc_idx} out of range")
    acc = accessors[acc_idx]
    if acc.get("componentType") != COMPONENT_FLOAT or acc.get("type") != "VEC3":
        raise ValueError(f"Accessor {acc_idx}: expected FLOAT VEC3")
    bv_idx = acc["bufferView"]
    if not (0 <= bv_idx < len(views)):
        raise ValueError(f"Accessor {acc_idx}: bufferView {bv_idx} out of range")
    bv = views[bv_idx]
    count = int(acc["count"])
    stride = int(bv.get("byteStride", 12))
    if stride < 12:
        raise ValueError(f"Accessor {acc_idx}: invalid byteStride {stride}")
    base = int(bv.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
    end = base + (count - 1) * stride + 12 if count > 0 else base
    if base < 0 or end > len(bin_data):
        raise ValueError(f"Accessor {acc_idx}: data range [{base}, {end}) exceeds BIN buffer {len(bin_data)}")

    out: List[Tuple[float, float, float]] = []
    if stride == 12:
        for i in range(count):
            out.append(struct.unpack_from("<3f", bin_data, base + i * 12))
    else:
        for i in range(count):
            out.append(struct.unpack_from("<3f", bin_data, base + i * stride))
    return out


def _read_accessor_indices(doc: Dict[str, Any], bin_data: bytes, acc_idx: int) -> List[int]:
    accessors = doc["accessors"]
    views = doc["bufferViews"]
    if not (0 <= acc_idx < len(accessors)):
        raise ValueError(f"Index accessor {acc_idx} out of range")
    acc = accessors[acc_idx]
    ctype = acc.get("componentType")
    if acc.get("type") != "SCALAR" or ctype not in (COMPONENT_UNSIGNED_SHORT, COMPONENT_UNSIGNED_INT):
        raise ValueError(f"Index accessor {acc_idx}: expected UNSIGNED_SHORT or UNSIGNED_INT SCALAR")
    bv = views[acc["bufferView"]]
    count = int(acc["count"])
    elem_size = 2 if ctype == COMPONENT_UNSIGNED_SHORT else 4
    fmt_char = "H" if ctype == COMPONENT_UNSIGNED_SHORT else "I"
    base = int(bv.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
    end = base + count * elem_size
    if base < 0 or end > len(bin_data):
        raise ValueError(f"Index accessor {acc_idx}: range [{base}, {end}) exceeds BIN buffer {len(bin_data)}")
    return list(struct.unpack_from(f"<{count}{fmt_char}", bin_data, base))


def parse_glb(path_or_bytes: Union[str, os.PathLike, bytes, bytearray, memoryview]) -> Tuple[Dict[str, Any], bytes]:
    """Parse GLB header and return (gltf_json_dict, bin_bytes)."""
    if isinstance(path_or_bytes, (bytes, bytearray, memoryview)):
        data = bytes(path_or_bytes)
    else:
        with open(path_or_bytes, "rb") as f:
            data = f.read()

    if len(data) < 20:
        raise ValueError(f"GLB too short ({len(data)} bytes)")
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError(f"Invalid GLB magic 0x{magic:08X}")
    if version != GLB_VERSION:
        raise ValueError(f"Unsupported GLB version {version}")
    if length != len(data):
        raise ValueError(f"GLB header length {length} != actual {len(data)}")
    if length % 4 != 0:
        raise ValueError(f"GLB length {length} is not 4-byte aligned")

    offset = 12
    json_chunk: bytes = b""
    bin_chunk: bytes = b""
    chunk_idx = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("Truncated GLB chunk header")
        c_len, c_type = struct.unpack_from("<II", data, offset)
        offset += 8
        if c_len % 4 != 0:
            raise ValueError(f"GLB chunk {chunk_idx} length {c_len} not 4-byte aligned")
        if offset + c_len > len(data):
            raise ValueError(f"GLB chunk {chunk_idx} overruns file")
        payload = data[offset : offset + c_len]
        offset += c_len
        if chunk_idx == 0:
            if c_type != CHUNK_JSON:
                raise ValueError(f"First GLB chunk must be JSON, got 0x{c_type:08X}")
            json_chunk = payload
        elif c_type == CHUNK_BIN and not bin_chunk:
            bin_chunk = payload
        chunk_idx += 1

    if not json_chunk:
        raise ValueError("Missing JSON chunk in GLB")
    doc = json.loads(json_chunk.decode("utf-8"))
    return doc, bin_chunk


def _read_accessor_floats(doc: Dict[str, Any], bin_data: bytes, acc_idx: int, expected_type: str) -> List[Tuple[float, ...]]:
    type_widths = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    width = type_widths[expected_type]
    accessors = doc["accessors"]
    views = doc["bufferViews"]
    if not (0 <= acc_idx < len(accessors)):
        raise ValueError(f"Accessor index {acc_idx} out of range")
    acc = accessors[acc_idx]
    if acc.get("componentType") != COMPONENT_FLOAT or acc.get("type") != expected_type:
        raise ValueError(f"Accessor {acc_idx}: expected FLOAT {expected_type}")
    bv_idx = acc["bufferView"]
    if not (0 <= bv_idx < len(views)):
        raise ValueError(f"Accessor {acc_idx}: bufferView {bv_idx} out of range")
    bv = views[bv_idx]
    count = int(acc["count"])
    stride = int(bv.get("byteStride", width * 4))
    if stride < width * 4:
        raise ValueError(f"Accessor {acc_idx}: invalid byteStride {stride}")
    base = int(bv.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
    end = base + (count - 1) * stride + width * 4 if count > 0 else base
    if base < 0 or end > len(bin_data):
        raise ValueError(f"Accessor {acc_idx}: range [{base}, {end}) exceeds BIN buffer {len(bin_data)}")
    fmt = f"<{width}f"
    return [struct.unpack_from(fmt, bin_data, base + i * stride) for i in range(count)]


def _mat4_mul(a: List[float], b: List[float]) -> List[float]:
    # Column-major 4x4 matrices (glTF convention): out[col*4 + row] = sum_k a[k*4 + row] * b[col*4 + k]
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
    return out


def _node_local_matrix(node: Dict[str, Any]) -> List[float]:
    if "matrix" in node:
        return [float(x) for x in node["matrix"]]
    tx, ty, tz = [float(x) for x in node.get("translation", (0.0, 0.0, 0.0))]
    qx, qy, qz, qw = [float(x) for x in node.get("rotation", (0.0, 0.0, 0.0, 1.0))]
    sx, sy, sz = [float(x) for x in node.get("scale", (1.0, 1.0, 1.0))]
    n = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    if n > 1e-12:
        qx, qy, qz, qw = qx / n, qy / n, qz / n, qw / n
    r00 = 1.0 - 2.0 * (qy * qy + qz * qz)
    r01 = 2.0 * (qx * qy - qz * qw)
    r02 = 2.0 * (qx * qz + qy * qw)
    r10 = 2.0 * (qx * qy + qz * qw)
    r11 = 1.0 - 2.0 * (qx * qx + qz * qz)
    r12 = 2.0 * (qy * qz - qx * qw)
    r20 = 2.0 * (qx * qz - qy * qw)
    r21 = 2.0 * (qy * qz + qx * qw)
    r22 = 1.0 - 2.0 * (qx * qx + qy * qy)
    return [
        r00 * sx, r10 * sx, r20 * sx, 0.0,
        r01 * sy, r11 * sy, r21 * sy, 0.0,
        r02 * sz, r12 * sz, r22 * sz, 0.0,
        tx, ty, tz, 1.0,
    ]


def _compute_world_matrices(doc: Dict[str, Any]) -> Dict[int, List[float]]:
    ident = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    nodes = doc.get("nodes", [])
    world: Dict[int, List[float]] = {}

    def walk(idx: int, parent_m: List[float]) -> None:
        m = _mat4_mul(parent_m, _node_local_matrix(nodes[idx]))
        world[idx] = m
        for c in nodes[idx].get("children", ()):
            walk(int(c), m)

    scenes = doc.get("scenes", [])
    if scenes:
        for root in scenes[doc.get("scene", 0)].get("nodes", ()):
            walk(int(root), ident)
    for i in range(len(nodes)):
        if i not in world:
            world[i] = _node_local_matrix(nodes[i])
    return world


def _transform_point(m: List[float], x: float, y: float, z: float) -> Tuple[float, float, float]:
    return (
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    )


def validate_glb(path_or_bytes: Union[str, os.PathLike, bytes, bytearray, memoryview]) -> Dict[str, Any]:
    """Validate GLB structure, schema, geometry, unit normals, indices, and animations.

    Pure Python stdlib (no numpy required). Raises ValueError on any error.
    """
    if isinstance(path_or_bytes, (bytes, bytearray, memoryview)):
        byte_length = len(path_or_bytes)
    else:
        byte_length = os.path.getsize(path_or_bytes)

    doc, bin_data = parse_glb(path_or_bytes)
    asset = doc.get("asset", {})
    if asset.get("version") != "2.0":
        raise ValueError(f"Expected glTF asset.version '2.0', got {asset.get('version')!r}")

    for key in ("scenes", "nodes", "meshes", "materials", "accessors", "bufferViews", "buffers"):
        if key not in doc or not isinstance(doc[key], list):
            raise ValueError(f"Missing or invalid glTF top-level list {key!r}")

    if not doc["buffers"]:
        raise ValueError("glTF has no buffers")
    declared_bin = int(doc["buffers"][0]["byteLength"])
    if declared_bin > len(bin_data) or len(bin_data) - declared_bin > 3:
        raise ValueError(f"BIN chunk size {len(bin_data)} mismatches buffer.byteLength {declared_bin}")

    for bv_i, bv in enumerate(doc["bufferViews"]):
        bo = int(bv.get("byteOffset", 0))
        bl = int(bv["byteLength"])
        if bo % 4 != 0:
            raise ValueError(f"bufferView {bv_i}: byteOffset {bo} not 4-byte aligned")
        if bo < 0 or bo + bl > declared_bin:
            raise ValueError(f"bufferView {bv_i}: [{bo}, {bo + bl}) exceeds buffer length {declared_bin}")

    mat_names = [str(m.get("name", f"material_{i}")) for i, m in enumerate(doc["materials"])]
    node_names = [str(n.get("name", f"node_{i}")) for i, n in enumerate(doc["nodes"])]
    world_mats = _compute_world_matrices(doc)
    mesh_nodes: Dict[int, List[int]] = {}
    for n_i, n in enumerate(doc["nodes"]):
        if "mesh" in n:
            mesh_nodes.setdefault(int(n["mesh"]), []).append(n_i)

    total_vertices = 0
    total_triangles = 0
    primitive_count = 0
    gmin = [math.inf, math.inf, math.inf]
    gmax = [-math.inf, -math.inf, -math.inf]

    for m_i, mesh in enumerate(doc["meshes"]):
        prims = mesh.get("primitives", [])
        if not prims:
            raise ValueError(f"Mesh {m_i} has no primitives")
        mats_for_mesh = [world_mats[n_i] for n_i in mesh_nodes.get(m_i, ())] or [
            [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
        ]
        for p_i, prim in enumerate(prims):
            primitive_count += 1
            attrs = prim.get("attributes", {})
            if "POSITION" not in attrs or "NORMAL" not in attrs:
                raise ValueError(f"Mesh {m_i} primitive {p_i} missing POSITION or NORMAL")
            if "indices" not in prim:
                raise ValueError(f"Mesh {m_i} primitive {p_i} missing indices")
            if "material" in prim and not (0 <= prim["material"] < len(doc["materials"])):
                raise ValueError(f"Mesh {m_i} primitive {p_i}: invalid material index {prim['material']}")

            pos_idx = int(attrs["POSITION"])
            nrm_idx = int(attrs["NORMAL"])
            idx_idx = int(prim["indices"])

            positions = _read_accessor_vec3(doc, bin_data, pos_idx)
            normals = _read_accessor_vec3(doc, bin_data, nrm_idx)
            indices = _read_accessor_indices(doc, bin_data, idx_idx)

            if len(positions) != len(normals):
                raise ValueError(f"Mesh {m_i} prim {p_i}: position/normal length mismatch")
            if len(positions) == 0 or len(indices) == 0:
                raise ValueError(f"Mesh {m_i} prim {p_i}: empty geometry")
            if len(indices) % 3 != 0:
                raise ValueError(f"Mesh {m_i} prim {p_i}: index count {len(indices)} not multiple of 3")

            pmin = [math.inf, math.inf, math.inf]
            pmax = [-math.inf, -math.inf, -math.inf]
            for v_i, (x, y, z) in enumerate(positions):
                if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
                    raise ValueError(f"Mesh {m_i} prim {p_i} vertex {v_i}: non-finite POSITION")
                if x < pmin[0]:
                    pmin[0] = x
                if y < pmin[1]:
                    pmin[1] = y
                if z < pmin[2]:
                    pmin[2] = z
                if x > pmax[0]:
                    pmax[0] = x
                if y > pmax[1]:
                    pmax[1] = y
                if z > pmax[2]:
                    pmax[2] = z
                for wm in mats_for_mesh:
                    wx, wy, wz = _transform_point(wm, x, y, z)
                    if wx < gmin[0]:
                        gmin[0] = wx
                    if wy < gmin[1]:
                        gmin[1] = wy
                    if wz < gmin[2]:
                        gmin[2] = wz
                    if wx > gmax[0]:
                        gmax[0] = wx
                    if wy > gmax[1]:
                        gmax[1] = wy
                    if wz > gmax[2]:
                        gmax[2] = wz

            pos_acc = doc["accessors"][pos_idx]
            if "min" not in pos_acc or "max" not in pos_acc:
                raise ValueError(f"POSITION accessor {pos_idx} missing required min/max bounds")
            for k in range(3):
                if abs(float(pos_acc["min"][k]) - pmin[k]) > 1e-4 or abs(float(pos_acc["max"][k]) - pmax[k]) > 1e-4:
                    raise ValueError(f"POSITION accessor {pos_idx} min/max bounds do not match vertex data")

            for n_i, (nx, ny, nz) in enumerate(normals):
                if not (math.isfinite(nx) and math.isfinite(ny) and math.isfinite(nz)):
                    raise ValueError(f"Mesh {m_i} prim {p_i} normal {n_i}: non-finite NORMAL")
                nlen = math.sqrt(nx * nx + ny * ny + nz * nz)
                if abs(nlen - 1.0) > 1e-3:
                    raise ValueError(f"Mesh {m_i} prim {p_i} normal {n_i}: non-unit length {nlen:.6f}")

            v_count = len(positions)
            for t in range(0, len(indices), 3):
                a, b, c = indices[t], indices[t + 1], indices[t + 2]
                if a >= v_count or b >= v_count or c >= v_count:
                    raise ValueError(f"Mesh {m_i} prim {p_i}: index out of range ({a}, {b}, {c}) >= {v_count}")
                if a == b or b == c or a == c:
                    raise ValueError(f"Mesh {m_i} prim {p_i}: degenerate triangle ({a}, {b}, {c})")

            total_vertices += v_count
            total_triangles += len(indices) // 3

    if total_triangles == 0:
        raise ValueError("GLB contains zero triangles")

    anim_names: List[str] = []
    for a_i, anim in enumerate(doc.get("animations", [])):
        anim_names.append(str(anim.get("name", f"animation_{a_i}")))
        samplers = anim.get("samplers", [])
        channels = anim.get("channels", [])
        if not samplers or not channels:
            raise ValueError(f"Animation {a_i} missing samplers or channels")
        for c_i, ch in enumerate(channels):
            s_idx = int(ch["sampler"])
            if not (0 <= s_idx < len(samplers)):
                raise ValueError(f"Animation {a_i} channel {c_i}: sampler {s_idx} out of range")
            target = ch.get("target", {})
            n_idx = int(target.get("node", -1))
            path = target.get("path")
            if not (0 <= n_idx < len(doc["nodes"])):
                raise ValueError(f"Animation {a_i} channel {c_i}: target node {n_idx} out of range")
            if path not in ("rotation", "translation", "scale"):
                raise ValueError(f"Animation {a_i} channel {c_i}: unsupported target path {path!r}")
            sampler = samplers[s_idx]
            in_idx = int(sampler["input"])
            out_idx = int(sampler["output"])
            times = [row[0] for row in _read_accessor_floats(doc, bin_data, in_idx, "SCALAR")]
            if not times or any(not math.isfinite(t) for t in times):
                raise ValueError(f"Animation {a_i} sampler {s_idx}: invalid timestamps")
            for k in range(1, len(times)):
                if times[k] <= times[k - 1]:
                    raise ValueError(f"Animation {a_i} sampler {s_idx}: timestamps must be strictly increasing")
            in_acc = doc["accessors"][in_idx]
            if "min" not in in_acc or "max" not in in_acc:
                raise ValueError(f"Animation {a_i} input accessor {in_idx} missing required min/max")
            if abs(float(in_acc["min"][0]) - times[0]) > 1e-4 or abs(float(in_acc["max"][0]) - times[-1]) > 1e-4:
                raise ValueError(f"Animation {a_i} input accessor {in_idx} min/max mismatch")
            out_type = "VEC4" if path == "rotation" else "VEC3"
            values = _read_accessor_floats(doc, bin_data, out_idx, out_type)
            if len(values) != len(times):
                raise ValueError(f"Animation {a_i} sampler {s_idx}: input/output count mismatch")
            for v_i, vec in enumerate(values):
                if not all(math.isfinite(c) for c in vec):
                    raise ValueError(f"Animation {a_i} sampler {s_idx} key {v_i}: non-finite value")
                if path == "rotation":
                    qlen = math.sqrt(sum(c * c for c in vec))
                    if abs(qlen - 1.0) > 1e-3:
                        raise ValueError(f"Animation {a_i} sampler {s_idx} key {v_i}: non-unit quaternion {qlen:.6f}")

    dims = [round(gmax[k] - gmin[k], 5) for k in range(3)]
    return {
        "byte_length": byte_length,
        "nodes": node_names,
        "node_count": len(node_names),
        "mesh_count": len(doc["meshes"]),
        "primitive_count": primitive_count,
        "vertices": total_vertices,
        "triangles": total_triangles,
        "materials": mat_names,
        "animations": anim_names,
        "bbox_min": [round(v, 5) for v in gmin],
        "bbox_max": [round(v, 5) for v in gmax],
        "dimensions": dims,
    }

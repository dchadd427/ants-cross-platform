"""
Test script for authentic sculpted mandibles with interlocking teeth.
"""
import bpy
import bmesh
import math
from mathutils import Vector, Euler, Matrix

def build_sculpted_mandible(bm, is_left=True):
    sign = -1.0 if is_left else 1.0
    
    # 5 Cross-section stations along the curved claw trajectory:
    # Station 0: Base / cheek root
    # Station 1: Expanding shoulder / bulb
    # Station 2: Anterior apex (furthest forward)
    # Station 3: Inward hook
    # Station 4: Sharp claw tip
    
    # Each station has: Center, Normal (tangent direction), Width (along local X), Height (along local Z)
    stations = [
        (Vector((sign * 0.16, -0.22, 1.48)), Vector((sign * 0.4, -0.9, -0.1)).normalized(), 0.080, 0.085),
        (Vector((sign * 0.24, -0.32, 1.45)), Vector((sign * 0.2, -0.9, -0.3)).normalized(), 0.105, 0.095),
        (Vector((sign * 0.22, -0.44, 1.40)), Vector((sign * -0.5, -0.7, -0.4)).normalized(), 0.095, 0.085),
        (Vector((sign * 0.12, -0.45, 1.37)), Vector((sign * -0.9, -0.2, -0.3)).normalized(), 0.075, 0.070),
        (Vector((sign * 0.04, -0.42, 1.36)), Vector((sign * -1.0, 0.0, 0.0)).normalized(), 0.020, 0.025)
    ]
    
    # 8 points per ring (octagonal cross-section)
    num_pts = 8
    rings = []
    
    for i, (center, normal, rx, rz) in enumerate(stations):
        # Construct orthonormal basis: Normal (tangent), Right, Up
        up_ref = Vector((0, 0, 1))
        right = normal.cross(up_ref).normalized()
        up = right.cross(normal).normalized()
        
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            # Bulbous outward curve on lateral side, flatter on medial side
            cos_t = math.cos(th)
            sin_t = math.sin(th)
            
            # Asymmetry: outer lateral side (cos_t * sign > 0) is more puffed
            rx_eff = rx * (1.20 if (cos_t * sign > 0) else 0.85)
            # Flatten bottom slightly
            rz_eff = rz * (0.90 if sin_t < 0 else 1.05)
            
            p_local = (right * (cos_t * rx_eff)) + (up * (sin_t * rz_eff))
            world_p = center + p_local
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)
        
    # Bridge rings with quad faces
    for i in range(len(stations) - 1):
        r0 = rings[i]
        r1 = rings[i + 1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
            
    # Cap root and tip
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    
    # Now extrude teeth from the inner medial edge of station 2 and 3!
    # Inner edge points are facing medial (towards midline X=0)
    # Let's add 2 sculpted sharp teeth:
    # Tooth 1 (Upper primary fang):
    t1_base1 = rings[2][num_pts // 2]
    t1_base2 = rings[2][(num_pts // 2 + 1) % num_pts]
    t1_tip_pos = Vector((sign * 0.05, -0.41, 1.40 + (0.015 if is_left else -0.015)))
    v_t1 = bm.verts.new(t1_tip_pos)
    
    t1_b3 = rings[3][num_pts // 2]
    t1_b4 = rings[3][(num_pts // 2 + 1) % num_pts]
    
    # Create quad/tri tooth geometry welded to the body
    bm.faces.new([t1_base1, v_t1, t1_b3])
    bm.faces.new([t1_base2, t1_b4, v_t1])
    bm.faces.new([t1_base1, t1_base2, v_t1])
    bm.faces.new([t1_b3, v_t1, t1_b4])
    
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

if __name__ == "__main__":
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bm = bmesh.new()
    build_sculpted_mandible(bm, True)
    build_sculpted_mandible(bm, False)
    mesh = bpy.data.meshes.new("Mandibles")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("Mandibles", mesh)
    bpy.context.scene.collection.objects.link(obj)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    for p in mesh.polygons:
        p.use_smooth = True
    print(f"Mandibles created! Verts: {len(mesh.vertices)}, Faces: {len(mesh.polygons)}")

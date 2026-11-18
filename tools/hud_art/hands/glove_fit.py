"""The two hands of the cursors (the pointing hand c_select and the open hand c_scroll), modeled and rendered in Blender. Not run by the build: make_hands.py runs it (Blender, headless) and writes the sprites
that are committed here, hands/<sprite>_<scale>x.png; make_hud_art.py reads those.  Run by hand: Blender -b --python glove_fit.py -- <outdir>   (writes <outdir>/<sprite>_12x.png)

The glove is built to the original sprites' proportions and pose. Every part is given in the sprite's own pixel coordinates (x right, y down, 17 x 21), measured on the original at 1x: where each fingertip
and knuckle sits, each finger's width (every finger is one straight segment of constant width with a round tip), the thumb's root, direction and length, the palm's edges, the wrist's width and where it
leaves the box. Parts are balls, tapering limbs and rounded blocks; a part's silhouette is its shape plus the outline (INK pixels on every side), so the model's sizes are the measured ones less the
outline. The look: a smooth white glove, a pale gold shaded side, a dark outline (inverted hull), light from the upper left, no cuff. The camera sees exactly the sprite's box (1 unit = 1 canvas pixel) at 12
samples for each canvas pixel, so nothing is fitted or scaled afterwards but the average down to 1x, 2x and 3x."""
import bpy, sys, math, os
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
OUTDIR = argv[0]
BOX = (17, 21); PX = 12; INK = 0.80
D = math.radians

# limb: (x0, y0, r0, x1, y1, r1, depth) centre line from (x0, y0) to (x1, y1) with the silhouette's radii (outline included); depth > 0 is nearer the viewer
# block: (x0, y0, x1, y1, corner radius, depth, thickness) the silhouette's rectangle (outline included)
THUMB = [('limb', 1.95, 13.05, 1.95, 4.6, 14.6, 2.75, 0.5), ('limb', 4.6, 14.4, 2.6, 7.8, 16.2, 2.2, 0.3)]          # the thumb: its tip at the left edge, a level upper edge, a slanting lower edge running into the wrist
PALM = [('block', 5.0, 8.6, 17.0, 18.6, 2.6, 0.0, 5.2), ('block', 6.0, 14.0, 15.0, 24.0, 1.6, -0.2, 4.4)]            # the palm, and the wrist leaving the box between x 6 and 15
HANDS = {
    # the pointing hand (c_select): the index straight up, three folded fingers as knuckles stepping down to the right
    'c_select': PALM + THUMB + [
        ('limb', 7.0, 2.0, 2.0, 7.0, 11.0, 2.0, 0.9),                                  # the index: x 5 .. 9, its tip at y 0
        ('limb', 10.2, 9.3, 2.25, 10.2, 12.5, 2.25, 1.5),                              # folded finger 2: top at y 7
        ('limb', 12.9, 10.2, 2.2, 12.9, 13.0, 2.2, 1.2),                               # folded finger 3: top at y 8
        ('limb', 15.0, 12.5, 1.65, 15.0, 14.0, 1.65, 0.9),                              # folded finger 4: top at y 10
    ],
    # the open hand (c_scroll): four fingers up, the first leaning a little left at its tip, the fourth short
    'c_scroll': PALM + THUMB + [
        ('limb', 5.1, 2.0, 2.0, 6.2, 10.0, 2.0, 0.6),          # finger 1: one straight segment of constant width, leaning a little left toward its tip at y 0
        ('limb', 10.0, 3.0, 2.0, 10.0, 11.0, 2.0, 1.2),                                # finger 2: tip at y 1, x 8 .. 12
        ('limb', 13.0, 5.0, 2.0, 13.0, 11.5, 2.0, 0.9),                                # finger 3: tip at y 3, x 11 .. 15
        ('limb', 15.35, 7.55, 1.55, 15.35, 11.5, 1.55, 0.5),          # finger 4: one straight segment, tip at y 6, x 14 .. 17
    ],
}

def W(x, y, depth=0.0): return Vector((x - BOX[0] / 2.0, -depth, BOX[1] / 2.0 - y))

def material(name, color, shade):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial'); add = nt.nodes.new('ShaderNodeAddShader')
    sh = nt.nodes.new('ShaderNodeBsdfToon'); sh.component = 'DIFFUSE'
    sh.inputs['Color'].default_value = tuple(max(c - d, 0.0) for c, d in zip(color, shade)) + (1,); sh.inputs['Size'].default_value = 0.58; sh.inputs['Smooth'].default_value = 0.10
    em = nt.nodes.new('ShaderNodeEmission'); em.inputs['Color'].default_value = tuple(shade) + (1,); em.inputs['Strength'].default_value = 1.0
    nt.links.new(sh.outputs[0], add.inputs[0]); nt.links.new(em.outputs[0], add.inputs[1]); nt.links.new(add.outputs[0], out.inputs['Surface'])
    return m

def ink_material():
    m = bpy.data.materials.new('ink'); m.use_nodes = True
    nt = m.node_tree; nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial'); mix = nt.nodes.new('ShaderNodeMixShader'); geo = nt.nodes.new('ShaderNodeNewGeometry')
    tr = nt.nodes.new('ShaderNodeBsdfTransparent'); em = nt.nodes.new('ShaderNodeEmission'); em.inputs['Color'].default_value = (0.010, 0.006, 0.002, 1)
    nt.links.new(geo.outputs['Backfacing'], mix.inputs['Fac']); nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2]); nt.links.new(mix.outputs[0], out.inputs['Surface'])
    return m

def build(parts, white):
    objs = []
    def done():
        ob = bpy.context.active_object; bpy.ops.object.shade_smooth(); ob.data.materials.append(white); objs.append(ob); return ob
    for p in parts:
        if p[0] == 'limb':
            _, x0, y0, r0, x1, y1, r1, dep = p
            r0, r1 = r0 - INK, r1 - INK; a, b = W(x0, y0, dep), W(x1, y1, dep)
            bpy.ops.mesh.primitive_uv_sphere_add(radius=r0, location=a, segments=32, ring_count=16); done()
            bpy.ops.mesh.primitive_uv_sphere_add(radius=r1, location=b, segments=32, ring_count=16); done()
            d = b - a
            if d.length > 1e-4:
                bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=r0, radius2=r1, depth=d.length, location=(a + b) / 2, end_fill_type='NOTHING')
                ob = done(); ob.rotation_mode = 'QUATERNION'; ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
        else:
            _, x0, y0, x1, y1, r, dep, th = p
            x0, y0, x1, y1, r = x0 + INK, y0 + INK, x1 - INK, y1 - INK, max(r - INK, 0.3)
            bpy.ops.mesh.primitive_cube_add(size=1, location=W((x0 + x1) / 2.0, (y0 + y1) / 2.0, dep))
            ob = bpy.context.active_object; ob.scale = (x1 - x0, th, y1 - y0); bpy.ops.object.transform_apply(scale=True)
            bv = ob.modifiers.new('round', 'BEVEL'); bv.width = r; bv.segments = 10; bv.limit_method = 'NONE'
            done()
    ink = ink_material()
    for ob in list(objs):
        h = ob.copy(); h.data = ob.data.copy(); bpy.context.scene.collection.objects.link(h)
        h.data.materials.clear(); h.data.materials.append(ink)
        md = h.modifiers.new('out', 'DISPLACE'); md.strength = INK; md.mid_level = 0.0; md.direction = 'NORMAL'
        h.visible_shadow = False

for name, parts in HANDS.items():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 96; sc.cycles.use_denoising = False
    sc.render.film_transparent = True; sc.cycles.transparent_max_bounces = 64; sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
    sc.view_settings.view_transform = 'Standard'; sc.view_settings.look = 'None'
    sc.render.threads_mode = 'FIXED'; sc.render.threads = 2
    w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True; w.node_tree.nodes['Background'].inputs[1].default_value = 0.0
    sun = bpy.data.lights.new('sun', 'SUN'); sun.energy = 3.0; sun.angle = D(8)
    so = bpy.data.objects.new('sun', sun); sc.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = Vector((0, 0, -1)).rotation_difference(Vector((0.62, 0.72, -0.52)).normalized())
    cam = bpy.data.cameras.new('cam'); cam.type = 'ORTHO'; cam.ortho_scale = BOX[1]
    co = bpy.data.objects.new('cam', cam); sc.collection.objects.link(co); sc.camera = co
    co.location = (0, -40, 0); co.rotation_euler = (D(90), 0, 0)
    build(parts, material('glove', (1.0, 1.0, 1.0), (0.80, 0.66, 0.36)))
    sc.render.resolution_x, sc.render.resolution_y = BOX[0] * PX, BOX[1] * PX; sc.render.resolution_percentage = 100
    sc.render.filepath = os.path.join(OUTDIR, '%s_12x.png' % name)
    bpy.ops.render.render(write_still=True)
    print('FIT', name, flush=True)

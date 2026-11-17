"""The places of the parts of the original's animations, from the repository's decoded catalog (asset_catalog/catalog_data.js): ANIMS[name] = for each frame, [(dx, dy, sprite name, width, height)]."""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'asset_catalog')) + '/'
with open(ROOT + 'catalog_data.js') as _f:
    _txt = _f.read()
ANIMS = {}
for m in re.finditer(r'\{ id: (\d+), name: "([^"]+)", subitems: \[(.*?)\n    \] \},', _txt, re.S):
    frames = []
    for sub in re.finditer(r'frames: \[(.*?)\] \}', m.group(3), re.S):
        fr = []
        for f in re.finditer(r'\{ dx: (-?\d+), dy: (-?\d+), sprite_id: (\d+), sprite_name: "([^"]+)", w: (\d+), h: (\d+) \}', sub.group(1)):
            fr.append((int(f.group(1)), int(f.group(2)), f.group(4), int(f.group(5)), int(f.group(6))))
        frames.append(fr)
    ANIMS[m.group(2)] = frames

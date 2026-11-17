"""The original's HUD animations: the frame times (docs/chd_table4_animations.json) and the places of the parts (asset_catalog/, through cat_orig.py). Numbers only: no picture of the original is read."""
import json
import cat_orig as cat
from common import ROOT

with open(ROOT + 'docs/chd_table4_animations.json') as _f:
    TABLE = {e['name']: e for e in json.load(_f)}

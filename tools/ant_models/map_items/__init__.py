"""Registry of 3D map item models (food, toys, plants, grass, rocks, anthills, bridges, gear) for Ants (1998).

Pure-stdlib safe on import so CI without numpy can inspect metadata & catalog specs.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Dict, Tuple


@dataclass(frozen=True)
class MapItemSpec:
    """Metadata and builder binding for one 1998 map item 3D model."""

    id: str
    label: str
    category: str
    chd_id: int
    chd_name: str
    family: str
    stage_label: str = ""
    default_yaw: float = 0.0
    default_pitch: float = 44.0
    builder_module: str = ""
    builder_fn: str = ""


CATEGORIES: Tuple[Tuple[str, str], ...] = (
    ("all", "All"),
    ("food", "Food"),
    ("plants", "Plants & Grass"),
    ("toys", "Toys & Props"),
    ("rocks", "Rocks"),
    ("structures", "Hills & Gear"),
)

# Waves 1–2 realistic 3D showcase:
# - Plants & Grass: 35 / 35 complete (100%)
# - Hills & Gameplay Gear: 19 / 19 complete (100%)
# - Food: 7 Wave 1 items, Toys & Props: 6 Wave 1 items, Rocks: 1 Wave 1 item (total 68 / 216).
MAP_ITEM_SPECS: Tuple[MapItemSpec, ...] = (
    # --- Toys & Props (Wave 1) ---
    MapItemSpec("bearhed", "Teddy Bear Head", "toys", 345, "bearhed", "bear", "Head", default_yaw=-8.0, default_pitch=46.0, builder_module="items_toys", builder_fn="build_bearhed"),
    MapItemSpec("bearbod", "Teddy Bear Body", "toys", 346, "bearbod", "bear", "Body", default_yaw=0.0, default_pitch=58.0, builder_module="items_toys", builder_fn="build_bearbod"),
    MapItemSpec("ducky1", "Rubber Ducky (3/4)", "toys", 348, "ducky1", "ducky", "Pose 1", default_yaw=0.0, default_pitch=34.0, builder_module="items_toys", builder_fn="build_ducky1"),
    MapItemSpec("ducky2", "Rubber Ducky (Profile)", "toys", 349, "ducky2", "ducky", "Pose 2", default_yaw=0.0, default_pitch=32.0, builder_module="items_toys", builder_fn="build_ducky2"),
    MapItemSpec("dino", "Toy Stegosaurus", "toys", 351, "dino", "dino", "", default_yaw=0.0, default_pitch=38.0, builder_module="items_toys", builder_fn="build_dino"),
    MapItemSpec("can1", "Crushed Critter Cola", "toys", 238, "can1", "soda_can", "Can 1", default_yaw=0.0, default_pitch=44.0, builder_module="items_toys", builder_fn="build_can1"),
    # --- Food (Wave 1) ---
    MapItemSpec("fdburgr1", "Cheeseburger (Full)", "food", 253, "fdburgr1", "burger", "Full", default_yaw=0.0, default_pitch=40.0, builder_module="items_food", builder_fn="build_fdburgr1"),
    MapItemSpec("fdburgr2", "Cheeseburger (Bitten)", "food", 254, "fdburgr2", "burger", "Half-Eaten", default_yaw=0.0, default_pitch=40.0, builder_module="items_food", builder_fn="build_fdburgr2"),
    MapItemSpec("fdburgr3", "Cheeseburger (Remnants)", "food", 255, "fdburgr3", "burger", "Scraps", default_yaw=0.0, default_pitch=48.0, builder_module="items_food", builder_fn="build_fdburgr3"),
    MapItemSpec("fdcornd1", "Corn Dog & Mustard", "food", 361, "fdcornd1", "corndog", "Full", default_yaw=0.0, default_pitch=44.0, builder_module="items_food", builder_fn="build_fdcornd1"),
    MapItemSpec("fdjelo1", "Ruby Jell-O & Banana", "food", 366, "fdjelo1", "jello", "Full", default_yaw=0.0, default_pitch=43.0, builder_module="items_food", builder_fn="build_fdjelo1"),
    MapItemSpec("fdchick1", "Fried Chicken Drumstick", "food", 399, "fdchick1", "chicken", "Full", default_yaw=0.0, default_pitch=46.0, builder_module="items_food", builder_fn="build_fdchick1"),
    MapItemSpec("fdsuckr1", "Peppermint Lollipop", "food", 256, "fdsuckr1", "lollipop", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food", builder_fn="build_fdsuckr1"),
    # --- Plants & Grass (ALL 35 / 35 COMPLETE!) ---
    MapItemSpec("grass1", "Grass Tuft (Small)", "plants", 1320, "grass1", "grass", "Tuft 1", default_yaw=0.0, default_pitch=28.0, builder_module="items_plants", builder_fn="build_grass1"),
    MapItemSpec("grass2", "Grass Tuft (Swayed)", "plants", 1321, "grass2", "grass", "Tuft 2", default_yaw=0.0, default_pitch=28.0, builder_module="items_plants", builder_fn="build_grass2"),
    MapItemSpec("grass3", "Grass Clump (Medium)", "plants", 1322, "grass3", "grass", "Clump 3", default_yaw=0.0, default_pitch=28.0, builder_module="items_plants", builder_fn="build_grass3"),
    MapItemSpec("grass4", "Grass Fountain (Open)", "plants", 1323, "grass4", "grass", "Clump 4", default_yaw=0.0, default_pitch=28.0, builder_module="items_plants", builder_fn="build_grass4"),
    MapItemSpec("grassmed1", "Grass Hedge (Horizontal)", "plants", 1324, "grassmed1", "grass", "Hedge H", default_yaw=0.0, default_pitch=32.0, builder_module="items_plants", builder_fn="build_grassmed1"),
    MapItemSpec("grassbig1", "Tall Grass Clump", "plants", 1325, "grassbig1", "grass", "Tall 1", default_yaw=0.0, default_pitch=30.0, builder_module="items_plants", builder_fn="build_grassbig1"),
    MapItemSpec("grassmed2", "Grass Hedge (Vertical)", "plants", 1326, "grassmed2", "grass", "Hedge V", default_yaw=0.0, default_pitch=46.0, builder_module="items_plants", builder_fn="build_grassmed2"),
    MapItemSpec("grassmed3", "Grass Hedge (SW-NE)", "plants", 1327, "grassmed3", "grass", "Hedge SW", default_yaw=0.0, default_pitch=44.0, builder_module="items_plants", builder_fn="build_grassmed3"),
    MapItemSpec("grassbig2", "Tall Grass Hedge (Curved)", "plants", 1328, "grassbig2", "grass", "Tall 2", default_yaw=0.0, default_pitch=42.0, builder_module="items_plants", builder_fn="build_grassbig2"),
    MapItemSpec("grassmed4", "Grass Hedge (NW-SE)", "plants", 1329, "grassmed4", "grass", "Hedge NW", default_yaw=0.0, default_pitch=44.0, builder_module="items_plants", builder_fn="build_grassmed4"),
    MapItemSpec("clover1", "Four-Leaf Clover (Right)", "plants", 404, "clover1", "clover", "Clover 1", default_yaw=0.0, default_pitch=48.0, builder_module="items_plants", builder_fn="build_clover1"),
    MapItemSpec("clover2", "Four-Leaf Clover (Upright)", "plants", 405, "clover2", "clover", "Clover 2", default_yaw=0.0, default_pitch=50.0, builder_module="items_plants", builder_fn="build_clover2"),
    MapItemSpec("clover3", "Four-Leaf Clover (Curved)", "plants", 406, "clover3", "clover", "Clover 3", default_yaw=0.0, default_pitch=48.0, builder_module="items_plants", builder_fn="build_clover3"),
    MapItemSpec("clover4", "Four-Leaf Clover (Lush)", "plants", 407, "clover4", "clover", "Clover 4", default_yaw=0.0, default_pitch=48.0, builder_module="items_plants", builder_fn="build_clover4"),
    MapItemSpec("clovers", "Clover Patch Bouquet", "plants", 408, "clovers", "clover", "Bouquet", default_yaw=0.0, default_pitch=34.0, builder_module="items_plants", builder_fn="build_clovers"),
    MapItemSpec("plant1", "Mint Leaf Rosette", "plants", 409, "plant1", "plant", "Herb", default_yaw=0.0, default_pitch=52.0, builder_module="items_foliage", builder_fn="build_plant1"),
    MapItemSpec("leaf1", "Maple Leaf (Upright)", "plants", 323, "leaf1", "leaf", "Leaf 1", default_yaw=0.0, default_pitch=58.0, builder_module="items_foliage", builder_fn="build_leaf1"),
    MapItemSpec("leaf2", "Maple Leaf (Diagonal)", "plants", 324, "leaf2", "leaf", "Leaf 2", default_yaw=0.0, default_pitch=58.0, builder_module="items_foliage", builder_fn="build_leaf2"),
    MapItemSpec("leaf3", "Oak Leaf (Diagonal)", "plants", 325, "leaf3", "leaf", "Leaf 3", default_yaw=0.0, default_pitch=58.0, builder_module="items_foliage", builder_fn="build_leaf3"),
    MapItemSpec("leaf4", "Oak Leaf (Upright)", "plants", 326, "leaf4", "leaf", "Leaf 4", default_yaw=0.0, default_pitch=58.0, builder_module="items_foliage", builder_fn="build_leaf4"),
    MapItemSpec("stick1", "Crooked Woodland Twig", "plants", 327, "stick1", "stick", "Twig", default_yaw=0.0, default_pitch=58.0, builder_module="items_foliage", builder_fn="build_stick1"),
    MapItemSpec("stick2a", "Leafy Branch Twig", "plants", 328, "stick2a", "stick", "Leafy", default_yaw=0.0, default_pitch=56.0, builder_module="items_foliage", builder_fn="build_stick2a"),
    MapItemSpec("ivyrt", "Backyard Ivy (Right)", "plants", 417, "ivyrt", "ivy", "Right", default_yaw=0.0, default_pitch=56.0, builder_module="items_foliage", builder_fn="build_ivyrt"),
    MapItemSpec("ivylft", "Backyard Ivy (Left)", "plants", 418, "ivylft", "ivy", "Left", default_yaw=0.0, default_pitch=56.0, builder_module="items_foliage", builder_fn="build_ivylft"),
    MapItemSpec("ivyfront", "Backyard Ivy (Spread)", "plants", 419, "ivyfront", "ivy", "Spread", default_yaw=0.0, default_pitch=56.0, builder_module="items_foliage", builder_fn="build_ivyfront"),
    MapItemSpec("flower1a", "Oxeye Daisy (Front)", "plants", 410, "flower1a", "daisy", "Front", default_yaw=0.0, default_pitch=30.0, builder_module="items_plants", builder_fn="build_flower1a"),
    MapItemSpec("flower1b", "Oxeye Daisy (Side)", "plants", 411, "flower1b", "daisy", "Profile", default_yaw=0.0, default_pitch=28.0, builder_module="items_foliage", builder_fn="build_flower1b"),
    MapItemSpec("flower2a", "Hot-Pink Cosmos", "plants", 412, "flower2a", "cosmos", "Pink", default_yaw=0.0, default_pitch=28.0, builder_module="items_foliage", builder_fn="build_flower2a"),
    MapItemSpec("flower2b", "Rose Starflower", "plants", 413, "flower2b", "cosmos", "Rose", default_yaw=0.0, default_pitch=28.0, builder_module="items_foliage", builder_fn="build_flower2b"),
    MapItemSpec("flower3a", "Spotted Lilac Lily", "plants", 414, "flower3a", "lily", "Lilac", default_yaw=0.0, default_pitch=28.0, builder_module="items_foliage", builder_fn="build_flower3a"),
    MapItemSpec("flower3b", "Spotted Violet Lily", "plants", 415, "flower3b", "lily", "Violet", default_yaw=0.0, default_pitch=28.0, builder_module="items_foliage", builder_fn="build_flower3b"),
    MapItemSpec("flower3c", "Googly-Eyed Flower", "plants", 416, "flower3c", "lily", "Googly", default_yaw=0.0, default_pitch=28.0, builder_module="items_foliage", builder_fn="build_flower3c"),
    MapItemSpec("dflower1", "Groucho Disguise Daisy", "plants", 420, "dflower1", "dflower", "Animated", default_yaw=0.0, default_pitch=30.0, builder_module="items_plants", builder_fn="build_dflower1"),
    MapItemSpec("flower1", "Groucho Daisy (Still)", "plants", 421, "flower1", "dflower", "Still", default_yaw=0.0, default_pitch=30.0, builder_module="items_foliage", builder_fn="build_flower1"),
    MapItemSpec("mush2a", "Spotted Mushroom", "plants", 329, "mush2a", "mushroom", "", default_yaw=0.0, default_pitch=34.0, builder_module="items_plants", builder_fn="build_mush2a"),
    # --- Rocks (Wave 1) ---
    MapItemSpec("bigrock2", "Mossy Granite Boulder", "rocks", 290, "bigrock2", "rock", "Big Mossy", default_yaw=0.0, default_pitch=44.0, builder_module="items_world", builder_fn="build_bigrock2"),
    # --- Hills & Gameplay Gear (ALL 19 / 19 COMPLETE!) ---
    MapItemSpec("GREENHILL", "Green Team Anthill", "structures", 248, "GREENHILL", "anthill", "Green", default_yaw=0.0, default_pitch=46.0, builder_module="items_world", builder_fn="build_greenhill"),
    MapItemSpec("REDHILL", "Red Team Anthill", "structures", 247, "REDHILL", "anthill", "Red", default_yaw=0.0, default_pitch=46.0, builder_module="items_structures", builder_fn="build_redhill"),
    MapItemSpec("BLUEHILL", "Blue Team Anthill", "structures", 246, "BLUEHILL", "anthill", "Blue", default_yaw=0.0, default_pitch=46.0, builder_module="items_structures", builder_fn="build_bluehill"),
    MapItemSpec("BLACKHILL", "Black Team Anthill", "structures", 245, "BLACKHILL", "anthill", "Black", default_yaw=0.0, default_pitch=46.0, builder_module="items_structures", builder_fn="build_blackhill"),
    MapItemSpec("bridge1", "Mud Bridge (Stage 1)", "structures", 34, "bridge1", "bridge", "Stage 1", default_yaw=0.0, default_pitch=52.0, builder_module="items_structures", builder_fn="build_bridge1"),
    MapItemSpec("bridge2", "Mud Bridge (Stage 2)", "structures", 35, "bridge2", "bridge", "Stage 2", default_yaw=0.0, default_pitch=52.0, builder_module="items_structures", builder_fn="build_bridge2"),
    MapItemSpec("bridge3", "Mud Bridge (Stage 3)", "structures", 36, "bridge3", "bridge", "Stage 3", default_yaw=0.0, default_pitch=52.0, builder_module="items_structures", builder_fn="build_bridge3"),
    MapItemSpec("bridge4", "Mud Bridge (Complete)", "structures", 37, "bridge4", "bridge", "Full A", default_yaw=0.0, default_pitch=52.0, builder_module="items_structures", builder_fn="build_bridge4"),
    MapItemSpec("bridge4b", "Mud Bridge (Variant)", "structures", 38, "bridge4b", "bridge", "Full B", default_yaw=0.0, default_pitch=52.0, builder_module="items_structures", builder_fn="build_bridge4b"),
    MapItemSpec("pu_comb", "Power-Up: Combat Belt", "structures", 62, "pu_comb", "powerup", "Combat", default_yaw=0.0, default_pitch=44.0, builder_module="items_powerups", builder_fn="build_pu_comb"),
    MapItemSpec("pu_thief", "Power-Up: Thief Mask", "structures", 63, "pu_thief", "powerup", "Thief", default_yaw=0.0, default_pitch=36.0, builder_module="items_powerups", builder_fn="build_pu_thief"),
    MapItemSpec("pu_bomb", "Power-Up: Bomber Kit", "structures", 64, "pu_bomb", "powerup", "Bomber", default_yaw=0.0, default_pitch=32.0, builder_module="items_powerups", builder_fn="build_pu_bomb"),
    MapItemSpec("pu_swim", "Power-Up: Swimmer Mask", "structures", 65, "pu_swim", "powerup", "Swimmer", default_yaw=0.0, default_pitch=34.0, builder_module="items_powerups", builder_fn="build_pu_swim"),
    MapItemSpec("pu_mason", "Power-Up: Fire Helmet", "structures", 66, "pu_mason", "powerup", "Fire/Builder", default_yaw=0.0, default_pitch=36.0, builder_module="items_powerups", builder_fn="build_pu_mason"),
    MapItemSpec("redbomb", "Planted Bomb (Red)", "structures", 129, "redbomb", "bomb", "Red", default_yaw=0.0, default_pitch=28.0, builder_module="items_structures", builder_fn="build_redbomb"),
    MapItemSpec("greenbomb", "Planted Bomb (Green)", "structures", 130, "greenbomb", "bomb", "Green", default_yaw=0.0, default_pitch=28.0, builder_module="items_structures", builder_fn="build_greenbomb"),
    MapItemSpec("blackbomb", "Planted Bomb (Black)", "structures", 131, "blackbomb", "bomb", "Black", default_yaw=0.0, default_pitch=28.0, builder_module="items_structures", builder_fn="build_blackbomb"),
    MapItemSpec("bluebomb", "Planted Bomb (Blue)", "structures", 132, "bluebomb", "bomb", "Blue", default_yaw=0.0, default_pitch=28.0, builder_module="items_structures", builder_fn="build_bluebomb"),
    MapItemSpec("wallup04", "Fire Wall Flame", "structures", 134, "wallup04", "flame", "", default_yaw=0.0, default_pitch=22.0, builder_module="items_structures", builder_fn="build_wallup04"),
)

SPECS_BY_ID: Dict[str, MapItemSpec] = {s.id: s for s in MAP_ITEM_SPECS}


def resolve_builder(spec: MapItemSpec) -> Callable[[], object]:
    """Import and return the sculptor function for `spec` (requires numpy)."""
    import importlib

    pkg = __package__ or "map_items"
    mod = importlib.import_module(f".{spec.builder_module}", package=pkg)
    return getattr(mod, spec.builder_fn)


__all__ = ["CATEGORIES", "MAP_ITEM_SPECS", "MapItemSpec", "SPECS_BY_ID", "resolve_builder"]

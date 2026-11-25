"""Registry of 3D map item models (food, toys, plants, grass, rocks, anthills) for Ants (1998).

Pure-stdlib safe on import so CI without numpy can inspect metadata & catalog specs.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Tuple


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

# Wave 1 realistic 3D showcase covering every requested category (food, bear, duck, plants, grass, rock, hill).
MAP_ITEM_SPECS: Tuple[MapItemSpec, ...] = (
    # --- Toys & Props ---
    MapItemSpec("bearhed", "Teddy Bear Head", "toys", 345, "bearhed", "bear", "Head", default_yaw=-8.0, default_pitch=46.0, builder_module="items_toys", builder_fn="build_bearhed"),
    MapItemSpec("bearbod", "Teddy Bear Body", "toys", 346, "bearbod", "bear", "Body", default_yaw=0.0, default_pitch=58.0, builder_module="items_toys", builder_fn="build_bearbod"),
    MapItemSpec("ducky1", "Rubber Ducky (3/4)", "toys", 348, "ducky1", "ducky", "Pose 1", default_yaw=0.0, default_pitch=34.0, builder_module="items_toys", builder_fn="build_ducky1"),
    MapItemSpec("ducky2", "Rubber Ducky (Profile)", "toys", 349, "ducky2", "ducky", "Pose 2", default_yaw=0.0, default_pitch=32.0, builder_module="items_toys", builder_fn="build_ducky2"),
    MapItemSpec("dino", "Toy Stegosaurus", "toys", 351, "dino", "dino", "", default_yaw=0.0, default_pitch=38.0, builder_module="items_toys", builder_fn="build_dino"),
    MapItemSpec("can1", "Crushed Critter Cola", "toys", 238, "can1", "soda_can", "Can 1", default_yaw=0.0, default_pitch=44.0, builder_module="items_toys", builder_fn="build_can1"),
    # --- Food ---
    MapItemSpec("fdburgr1", "Cheeseburger (Full)", "food", 253, "fdburgr1", "burger", "Full", default_yaw=0.0, default_pitch=40.0, builder_module="items_food", builder_fn="build_fdburgr1"),
    MapItemSpec("fdburgr2", "Cheeseburger (Bitten)", "food", 254, "fdburgr2", "burger", "Half-Eaten", default_yaw=0.0, default_pitch=40.0, builder_module="items_food", builder_fn="build_fdburgr2"),
    MapItemSpec("fdburgr3", "Cheeseburger (Remnants)", "food", 255, "fdburgr3", "burger", "Scraps", default_yaw=0.0, default_pitch=48.0, builder_module="items_food", builder_fn="build_fdburgr3"),
    MapItemSpec("fdcornd1", "Corn Dog & Mustard", "food", 361, "fdcornd1", "corndog", "Full", default_yaw=0.0, default_pitch=44.0, builder_module="items_food", builder_fn="build_fdcornd1"),
    MapItemSpec("fdjelo1", "Ruby Jell-O & Banana", "food", 366, "fdjelo1", "jello", "Full", default_yaw=0.0, default_pitch=36.0, builder_module="items_food", builder_fn="build_fdjelo1"),
    MapItemSpec("fdchick1", "Fried Chicken Drumstick", "food", 399, "fdchick1", "chicken", "Full", default_yaw=0.0, default_pitch=46.0, builder_module="items_food", builder_fn="build_fdchick1"),
    MapItemSpec("fdsuckr1", "Peppermint Lollipop", "food", 256, "fdsuckr1", "lollipop", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food", builder_fn="build_fdsuckr1"),
    # --- Plants & Grass ---
    MapItemSpec("grass1", "Grass Tuft (Small)", "plants", 1320, "grass1", "grass", "Small", default_yaw=0.0, default_pitch=28.0, builder_module="items_plants", builder_fn="build_grass1"),
    MapItemSpec("grass3", "Grass Clump (Medium)", "plants", 1322, "grass3", "grass", "Medium", default_yaw=0.0, default_pitch=28.0, builder_module="items_plants", builder_fn="build_grass3"),
    MapItemSpec("grassbig1", "Tall Grass Clump", "plants", 1325, "grassbig1", "grass", "Tall", default_yaw=0.0, default_pitch=30.0, builder_module="items_plants", builder_fn="build_grassbig1"),
    MapItemSpec("clover1", "Four-Leaf Clover", "plants", 404, "clover1", "clover", "Clover 1", default_yaw=0.0, default_pitch=48.0, builder_module="items_plants", builder_fn="build_clover1"),
    MapItemSpec("flower1a", "Oxeye Daisy", "plants", 410, "flower1a", "daisy", "Daisy", default_yaw=0.0, default_pitch=30.0, builder_module="items_plants", builder_fn="build_flower1a"),
    MapItemSpec("dflower1", "Groucho Disguise Daisy", "plants", 420, "dflower1", "dflower", "Disguise", default_yaw=0.0, default_pitch=30.0, builder_module="items_plants", builder_fn="build_dflower1"),
    MapItemSpec("mush2a", "Spotted Mushroom", "plants", 329, "mush2a", "mushroom", "", default_yaw=0.0, default_pitch=34.0, builder_module="items_plants", builder_fn="build_mush2a"),
    # --- Rocks & Structures ---
    MapItemSpec("bigrock2", "Mossy Granite Boulder", "rocks", 290, "bigrock2", "rock", "Big Mossy", default_yaw=0.0, default_pitch=44.0, builder_module="items_world", builder_fn="build_bigrock2"),
    MapItemSpec("GREENHILL", "Green Team Anthill", "structures", 248, "GREENHILL", "anthill", "Green", default_yaw=0.0, default_pitch=46.0, builder_module="items_world", builder_fn="build_greenhill"),
)

SPECS_BY_ID: Dict[str, MapItemSpec] = {s.id: s for s in MAP_ITEM_SPECS}


def resolve_builder(spec: MapItemSpec) -> Callable[[], object]:
    """Import and return the sculptor function for `spec` (requires numpy)."""
    import importlib

    pkg = __package__ or "map_items"
    mod = importlib.import_module(f".{spec.builder_module}", package=pkg)
    return getattr(mod, spec.builder_fn)


__all__ = ["CATEGORIES", "MAP_ITEM_SPECS", "MapItemSpec", "SPECS_BY_ID", "resolve_builder"]

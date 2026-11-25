"""Registry of 3D map item models (food, toys, plants, grass, rocks, anthills, gear) for Ants (1998).

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

# Waves 1–3 realistic 3D showcase:
# - Plants & Grass: 35 / 35 complete (100%)
# - Hills & Gameplay Gear: 14 / 14 complete (100%; mud bridges excluded as ground tiles)
# - Toys, Props, Hardware, Doll & Characters: 51 / 51 complete (100%)
# - Food: 7 Wave 1 items, Rocks: 1 Wave 1 item (total 108 / 211).
MAP_ITEM_SPECS: Tuple[MapItemSpec, ...] = (
    # --- Toys & Props (Wave 1) ---
    MapItemSpec("bearhed", "Teddy Bear Head", "toys", 345, "bearhed", "bear", "Head", default_yaw=-8.0, default_pitch=46.0, builder_module="items_toys", builder_fn="build_bearhed"),
    MapItemSpec("bearbod", "Teddy Bear Body", "toys", 346, "bearbod", "bear", "Body", default_yaw=0.0, default_pitch=58.0, builder_module="items_toys", builder_fn="build_bearbod"),
    MapItemSpec("ducky1", "Rubber Ducky (3/4)", "toys", 348, "ducky1", "ducky", "Pose 1", default_yaw=0.0, default_pitch=34.0, builder_module="items_toys", builder_fn="build_ducky1"),
    MapItemSpec("ducky2", "Rubber Ducky (Profile)", "toys", 349, "ducky2", "ducky", "Pose 2", default_yaw=0.0, default_pitch=32.0, builder_module="items_toys", builder_fn="build_ducky2"),
    MapItemSpec("dino", "Toy Stegosaurus", "toys", 351, "dino", "dino", "", default_yaw=0.0, default_pitch=38.0, builder_module="items_toys", builder_fn="build_dino"),
    MapItemSpec("can1", "Crushed Critter Cola", "toys", 238, "can1", "soda_can", "Can 1", default_yaw=0.0, default_pitch=44.0, builder_module="items_toys", builder_fn="build_can1"),
    MapItemSpec("can2", "Crushed Critter Cola (Side)", "toys", 239, "can2", "soda_can", "Can 2", default_yaw=0.0, default_pitch=44.0, builder_module="items_props_toys", builder_fn="build_can2"),
    MapItemSpec("glasses", "Cat-Eye Eyeglasses", "toys", 236, "glasses", "glasses", "", default_yaw=0.0, default_pitch=42.0, builder_module="items_props_toys", builder_fn="build_glasses"),
    MapItemSpec("bobber", "Fishing Bobber", "toys", 237, "bobber", "bobber", "", default_yaw=0.0, default_pitch=44.0, builder_module="items_props_toys", builder_fn="build_bobber"),
    MapItemSpec("yoyo", "Vintage Yo-Yo", "toys", 240, "yoyo", "yoyo", "", default_yaw=0.0, default_pitch=52.0, builder_module="items_props_toys", builder_fn="build_yoyo"),
    MapItemSpec("brush", "Retro Squirt Pistol", "toys", 241, "brush", "brush", "", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_toys", builder_fn="build_brush"),
    MapItemSpec("slinky", "Metal Slinky Spring", "toys", 242, "slinky", "slinky", "", default_yaw=0.0, default_pitch=44.0, builder_module="items_props_toys", builder_fn="build_slinky"),
    MapItemSpec("mouse1", "Desktop Mouse & Cord", "toys", 243, "mouse1", "mouse", "Mouse", default_yaw=0.0, default_pitch=52.0, builder_module="items_props_toys", builder_fn="build_mouse1"),
    MapItemSpec("mouse2", "Serial Plug (DB9)", "toys", 244, "mouse2", "mouse", "Plug", default_yaw=0.0, default_pitch=50.0, builder_module="items_props_toys", builder_fn="build_mouse2"),
    MapItemSpec("buglass", "Bug-Eye Spring Glasses", "toys", 335, "buglass", "buglass", "", default_yaw=0.0, default_pitch=48.0, builder_module="items_props_toys", builder_fn="build_buglass"),
    MapItemSpec("jacks1", "Metal Jack (Pose 1)", "toys", 336, "jacks1", "jacks", "Jack 1", default_yaw=0.0, default_pitch=46.0, builder_module="items_props_toys", builder_fn="build_jacks1"),
    MapItemSpec("jacks2", "Metal Jack (Pose 2)", "toys", 337, "jacks2", "jacks", "Jack 2", default_yaw=0.0, default_pitch=46.0, builder_module="items_props_toys", builder_fn="build_jacks2"),
    MapItemSpec("jacks3", "Metal Jack (Pose 3)", "toys", 338, "jacks3", "jacks", "Jack 3", default_yaw=0.0, default_pitch=46.0, builder_module="items_props_toys", builder_fn="build_jacks3"),
    MapItemSpec("jacks4", "Metal Jack (Pose 4)", "toys", 339, "jacks4", "jacks", "Jack 4", default_yaw=0.0, default_pitch=46.0, builder_module="items_props_toys", builder_fn="build_jacks4"),
    MapItemSpec("ball", "Red Rubber Bounce Ball", "toys", 344, "ball", "ball", "", default_yaw=0.0, default_pitch=40.0, builder_module="items_props_toys", builder_fn="build_ball"),
    MapItemSpec("chicken", "Yellow Rubber Chicken", "toys", 347, "chicken", "rubber_chicken", "", default_yaw=0.0, default_pitch=52.0, builder_module="items_props_toys", builder_fn="build_chicken"),
    MapItemSpec("sling", "Wooden Slingshot", "toys", 350, "sling", "sling", "", default_yaw=0.0, default_pitch=54.0, builder_module="items_props_toys", builder_fn="build_sling"),
    MapItemSpec("pinkcray", "Pink Krayawn Crayon", "toys", 352, "pinkcray", "crayon", "Pink", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_toys", builder_fn="build_pinkcray"),
    MapItemSpec("grncray", "Broken Green Crayon", "toys", 353, "grncray", "crayon", "Green", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_toys", builder_fn="build_grncray"),
    MapItemSpec("yelcray", "Yellow Krayawn Crayon", "toys", 354, "yelcray", "crayon", "Yellow", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_toys", builder_fn="build_yelcray"),
    MapItemSpec("blucray", "Blue Krayawn Crayon", "toys", 355, "blucray", "crayon", "Blue", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_toys", builder_fn="build_blucray"),
    MapItemSpec("lunchbox", "Mini Metal Lunchbox", "toys", 356, "lunchbox", "lunchbox", "", default_yaw=0.0, default_pitch=40.0, builder_module="items_props_toys", builder_fn="build_lunchbox"),
    # --- Backyard Hardware & Debris (15 items) ---
    MapItemSpec("papclp1", "Paperclip (Vertical)", "toys", 311, "papclp1", "paperclip", "Vertical", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_hardware", builder_fn="build_papclp1"),
    MapItemSpec("papclp2", "Paperclip (Diagonal)", "toys", 312, "papclp2", "paperclip", "Diagonal", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_hardware", builder_fn="build_papclp2"),
    MapItemSpec("pin1", "Blue Push-Pin", "toys", 313, "pin1", "pushpin", "Blue", default_yaw=0.0, default_pitch=34.0, builder_module="items_props_hardware", builder_fn="build_pin1"),
    MapItemSpec("pin2", "Yellow Bent Push-Pin", "toys", 314, "pin2", "pushpin", "Yellow", default_yaw=0.0, default_pitch=30.0, builder_module="items_props_hardware", builder_fn="build_pin2"),
    MapItemSpec("pin3", "Green Push-Pin", "toys", 315, "pin3", "pushpin", "Green", default_yaw=0.0, default_pitch=30.0, builder_module="items_props_hardware", builder_fn="build_pin3"),
    MapItemSpec("botcap1", "YOU WIN Bottle Cap", "toys", 316, "botcap1", "botcap", "", default_yaw=0.0, default_pitch=50.0, builder_module="items_props_hardware", builder_fn="build_botcap1"),
    MapItemSpec("bolt1", "Hex Steel Nut (Flat)", "toys", 317, "bolt1", "hexnut", "Flat", default_yaw=0.0, default_pitch=46.0, builder_module="items_props_hardware", builder_fn="build_bolt1"),
    MapItemSpec("bolt2", "Hex Steel Nut (Tilted)", "toys", 318, "bolt2", "hexnut", "Tilted", default_yaw=0.0, default_pitch=40.0, builder_module="items_props_hardware", builder_fn="build_bolt2"),
    MapItemSpec("screw1", "Flat-Head Wood Screw", "toys", 319, "screw1", "screw", "Wood", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_hardware", builder_fn="build_screw1"),
    MapItemSpec("screw2", "Phillips Machine Screw", "toys", 320, "screw2", "screw", "Phillips", default_yaw=0.0, default_pitch=38.0, builder_module="items_props_hardware", builder_fn="build_screw2"),
    MapItemSpec("pencal1", "Hot-Pink Pen Cap", "toys", 321, "pencal1", "pencap", "", default_yaw=0.0, default_pitch=52.0, builder_module="items_props_hardware", builder_fn="build_pencal1"),
    MapItemSpec("shtbut1", "Teal Shirt Button", "toys", 322, "shtbut1", "button", "", default_yaw=0.0, default_pitch=54.0, builder_module="items_props_hardware", builder_fn="build_shtbut1"),
    MapItemSpec("broken1", "Pink High-Heel (Side)", "toys", 287, "broken1", "heels", "Side", default_yaw=0.0, default_pitch=52.0, builder_module="items_props_hardware", builder_fn="build_broken1"),
    MapItemSpec("broken2", "Pink High-Heel (Upright)", "toys", 288, "broken2", "heels", "Upright", default_yaw=0.0, default_pitch=52.0, builder_module="items_props_hardware", builder_fn="build_broken2"),
    MapItemSpec("broken3", "Silver Spoon Handle", "toys", 1334, "broken3", "spoon", "", default_yaw=0.0, default_pitch=54.0, builder_module="items_props_hardware", builder_fn="build_broken3"),
    # --- Barbie Doll Parts & Easter-Egg Characters (9 items) ---
    MapItemSpec("barbie", "Doll Torso (Pink Dress)", "toys", 330, "barbie", "doll", "Torso", default_yaw=0.0, default_pitch=28.0, builder_module="items_props_characters", builder_fn="build_barbie"),
    MapItemSpec("leg1", "Doll Leg (Vertical)", "toys", 331, "leg1", "doll", "Leg 1", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_characters", builder_fn="build_leg1"),
    MapItemSpec("leg2", "Doll Leg (Diagonal)", "toys", 332, "leg2", "doll", "Leg 2", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_characters", builder_fn="build_leg2"),
    MapItemSpec("arm1", "Doll Arm (Straight)", "toys", 333, "arm1", "doll", "Arm 1", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_characters", builder_fn="build_arm1"),
    MapItemSpec("arm2", "Doll Arm (Bent)", "toys", 334, "arm2", "doll", "Arm 2", default_yaw=0.0, default_pitch=56.0, builder_module="items_props_characters", builder_fn="build_arm2"),
    MapItemSpec("tad", "Tad the Space Frog (Back)", "toys", 427, "tad", "tad", "Lying", default_yaw=0.0, default_pitch=54.0, builder_module="items_props_characters", builder_fn="build_tad"),
    MapItemSpec("tad2", "Tad the Space Frog (Puddle)", "toys", 428, "tad2", "tad", "Sitting", default_yaw=0.0, default_pitch=36.0, builder_module="items_props_characters", builder_fn="build_tad2"),
    MapItemSpec("santahed", "Santa Claus Head", "toys", 429, "santahed", "santahed", "", default_yaw=0.0, default_pitch=26.0, builder_module="items_props_characters", builder_fn="build_santahed"),
    MapItemSpec("elvished", "Elvis Head", "toys", 430, "elvished", "elvished", "", default_yaw=0.0, default_pitch=26.0, builder_module="items_props_characters", builder_fn="build_elvished"),
    # --- Food (Wave 1 + Eaten Stages) ---
    MapItemSpec("fdburgr1", "Cheeseburger (Full)", "food", 253, "fdburgr1", "burger", "Full", default_yaw=0.0, default_pitch=40.0, builder_module="items_food", builder_fn="build_fdburgr1"),
    MapItemSpec("fdburgr2", "Cheeseburger (Bitten)", "food", 254, "fdburgr2", "burger", "Half-Eaten", default_yaw=0.0, default_pitch=40.0, builder_module="items_food", builder_fn="build_fdburgr2"),
    MapItemSpec("fdburgr3", "Cheeseburger (Remnants)", "food", 255, "fdburgr3", "burger", "Scraps", default_yaw=0.0, default_pitch=48.0, builder_module="items_food", builder_fn="build_fdburgr3"),
    MapItemSpec("fdcornd1", "Corn Dog & Mustard", "food", 361, "fdcornd1", "corndog", "Full", default_yaw=0.0, default_pitch=44.0, builder_module="items_food", builder_fn="build_fdcornd1"),
    MapItemSpec("fdcornd2", "Corn Dog (Bitten)", "food", 362, "fdcornd2", "corndog", "Bitten", default_yaw=0.0, default_pitch=44.0, builder_module="items_food_stages", builder_fn="build_fdcornd2"),
    MapItemSpec("fdcornd3", "Corn Dog (Half-Eaten)", "food", 363, "fdcornd3", "corndog", "Half", default_yaw=0.0, default_pitch=44.0, builder_module="items_food_stages", builder_fn="build_fdcornd3"),
    MapItemSpec("fdcornd4", "Corn Dog (Last Bite)", "food", 364, "fdcornd4", "corndog", "Stub", default_yaw=0.0, default_pitch=44.0, builder_module="items_food_stages", builder_fn="build_fdcornd4"),
    MapItemSpec("fdcornd5", "Corn Dog Stick", "food", 365, "fdcornd5", "corndog", "Stick", default_yaw=0.0, default_pitch=48.0, builder_module="items_food_stages", builder_fn="build_fdcornd5"),
    MapItemSpec("fdjelo1", "Ruby Jell-O & Banana", "food", 366, "fdjelo1", "jello", "Full", default_yaw=0.0, default_pitch=43.0, builder_module="items_food", builder_fn="build_fdjelo1"),
    MapItemSpec("fdjelo2", "Ruby Jell-O (Half-Eaten)", "food", 367, "fdjelo2", "jello", "Half", default_yaw=0.0, default_pitch=48.0, builder_module="items_food_stages", builder_fn="build_fdjelo2"),
    MapItemSpec("fdjelo3", "Ruby Jell-O (Scraps)", "food", 368, "fdjelo3", "jello", "Scraps", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_stages", builder_fn="build_fdjelo3"),
    MapItemSpec("fdchick1", "Fried Chicken Drumstick", "food", 399, "fdchick1", "chicken", "Full", default_yaw=0.0, default_pitch=46.0, builder_module="items_food", builder_fn="build_fdchick1"),
    MapItemSpec("fdchick2", "Fried Chicken (Bitten)", "food", 400, "fdchick2", "chicken", "Bitten", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_stages", builder_fn="build_fdchick2"),
    MapItemSpec("fdchick3", "Fried Chicken (Half-Eaten)", "food", 401, "fdchick3", "chicken", "Half", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_stages", builder_fn="build_fdchick3"),
    MapItemSpec("fdchick4", "Fried Chicken (Almost Gone)", "food", 402, "fdchick4", "chicken", "Nearly", default_yaw=0.0, default_pitch=48.0, builder_module="items_food_stages", builder_fn="build_fdchick4"),
    MapItemSpec("fdchick5", "Chicken Drumstick Bone", "food", 403, "fdchick5", "chicken", "Bone", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_stages", builder_fn="build_fdchick5"),
    MapItemSpec("fdsuckr1", "Peppermint Lollipop", "food", 256, "fdsuckr1", "lollipop", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food", builder_fn="build_fdsuckr1"),
    MapItemSpec("fdsuckr2", "Lollipop (Bitten)", "food", 257, "fdsuckr2", "lollipop", "Bitten", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_stages", builder_fn="build_fdsuckr2"),
    MapItemSpec("fdsuckr3", "Lollipop (Sliver)", "food", 258, "fdsuckr3", "lollipop", "Sliver", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_stages", builder_fn="build_fdsuckr3"),
    MapItemSpec("fdsuckr4", "Lollipop Stick", "food", 259, "fdsuckr4", "lollipop", "Stick", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_stages", builder_fn="build_fdsuckr4"),
    # --- Food Wave 4B: Savory Entrees & Parody Condiments (19 items) ---
    MapItemSpec("pizzatube", "Pizza! In A Tube", "food", 389, "pizzatube", "pizza", "Tube", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_pizzatube"),
    MapItemSpec("fdpizz1", "Tube Pizza (Full)", "food", 390, "fdpizz1", "pizza", "Full", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_savory", builder_fn="build_fdpizz1"),
    MapItemSpec("fdpizz2", "Tube Pizza (Bitten)", "food", 391, "fdpizz2", "pizza", "Bitten", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_savory", builder_fn="build_fdpizz2"),
    MapItemSpec("fdpizz3", "Tube Pizza (Half-Eaten)", "food", 392, "fdpizz3", "pizza", "Half", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_savory", builder_fn="build_fdpizz3"),
    MapItemSpec("fdpizz4", "Tube Pizza (Crumbs)", "food", 393, "fdpizz4", "pizza", "Crumbs", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_savory", builder_fn="build_fdpizz4"),
    MapItemSpec("steakwhiz", "Steak Whiz Spray Can", "food", 377, "steakwhiz", "steak", "Can", default_yaw=0.0, default_pitch=34.0, builder_module="items_food_savory", builder_fn="build_steakwhiz"),
    MapItemSpec("fdsteak1", "Steak Whiz Paste (Full)", "food", 378, "fdsteak1", "steak", "Full", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_savory", builder_fn="build_fdsteak1"),
    MapItemSpec("fdsteak2", "Steak Whiz Paste (Bitten)", "food", 379, "fdsteak2", "steak", "Bitten", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_savory", builder_fn="build_fdsteak2"),
    MapItemSpec("fdsteak3", "Steak Whiz Paste (Half)", "food", 380, "fdsteak3", "steak", "Half", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_savory", builder_fn="build_fdsteak3"),
    MapItemSpec("fdsteak4", "Steak Whiz Paste (Last Bit)", "food", 381, "fdsteak4", "steak", "Scraps", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_savory", builder_fn="build_fdsteak4"),
    MapItemSpec("fdeggs1", "Fried Eggs (Full)", "food", 385, "fdeggs1", "eggs", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_fdeggs1"),
    MapItemSpec("fdeggs2", "Fried Eggs (Bitten)", "food", 386, "fdeggs2", "eggs", "Bitten", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_fdeggs2"),
    MapItemSpec("fdeggs3", "Fried Eggs (Torn White)", "food", 387, "fdeggs3", "eggs", "Torn", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_fdeggs3"),
    MapItemSpec("fdeggs4", "Fried Eggs (Twin Yolks)", "food", 388, "fdeggs4", "eggs", "Yolks", default_yaw=0.0, default_pitch=48.0, builder_module="items_food_savory", builder_fn="build_fdeggs4"),
    MapItemSpec("fdfish1", "Choco-Fishy Snax (Full)", "food", 394, "fdfish1", "fish", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_fdfish1"),
    MapItemSpec("fdfish2", "Choco-Fishy Snax (Bitten)", "food", 395, "fdfish2", "fish", "Bitten", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_fdfish2"),
    MapItemSpec("fdfish3", "Choco-Fishy Snax (Chewed)", "food", 396, "fdfish3", "fish", "Chewed", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_savory", builder_fn="build_fdfish3"),
    MapItemSpec("fdfish4", "Choco-Fishy Wrapper (Messy)", "food", 397, "fdfish4", "fish", "Messy", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_savory", builder_fn="build_fdfish4"),
    MapItemSpec("fdfish5", "Choco-Fishy Wrapper (Clean)", "food", 398, "fdfish5", "fish", "Wrapper", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_savory", builder_fn="build_fdfish5"),
    # --- Food Wave 4C: Snacks, Bakery & Crackers (22 items) ---
    MapItemSpec("fdtwink1", "Golden Twinkie (Full)", "food", 382, "fdtwink1", "twinkie", "Full", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_snacks", builder_fn="build_fdtwink1"),
    MapItemSpec("fdtwink2", "Golden Twinkie (Bitten)", "food", 383, "fdtwink2", "twinkie", "Bitten", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_snacks", builder_fn="build_fdtwink2"),
    MapItemSpec("fdtwink3", "Golden Twinkie (Half)", "food", 384, "fdtwink3", "twinkie", "Half", default_yaw=0.0, default_pitch=46.0, builder_module="items_food_snacks", builder_fn="build_fdtwink3"),
    MapItemSpec("fdpmeat1", "Potted Meat Tin (Full)", "food", 357, "fdpmeat1", "pmeat", "Full", default_yaw=0.0, default_pitch=36.0, builder_module="items_food_snacks", builder_fn="build_fdpmeat1"),
    MapItemSpec("fdpmeat2", "Potted Meat Tin (Scooped)", "food", 358, "fdpmeat2", "pmeat", "Scooped", default_yaw=0.0, default_pitch=36.0, builder_module="items_food_snacks", builder_fn="build_fdpmeat2"),
    MapItemSpec("fdpmeat3", "Potted Meat Tin (Nearly Empty)", "food", 359, "fdpmeat3", "pmeat", "Low", default_yaw=0.0, default_pitch=36.0, builder_module="items_food_snacks", builder_fn="build_fdpmeat3"),
    MapItemSpec("fdpmeat4", "Potted Meat Tin (Empty)", "food", 360, "fdpmeat4", "pmeat", "Empty", default_yaw=0.0, default_pitch=36.0, builder_module="items_food_snacks", builder_fn="build_fdpmeat4"),
    MapItemSpec("fdchoc1", "Ultimate Chocolate Bar (Full)", "food", 260, "fdchoc1", "chocolate", "Full", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdchoc1"),
    MapItemSpec("fdchoc2", "Ultimate Chocolate Bar (Bitten)", "food", 261, "fdchoc2", "chocolate", "Bitten", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdchoc2"),
    MapItemSpec("fdchoc3", "Ultimate Chocolate Bar (Stub)", "food", 262, "fdchoc3", "chocolate", "Stub", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdchoc3"),
    MapItemSpec("fdchoc4", "Chocolate Foil Wrapper", "food", 263, "fdchoc4", "chocolate", "Wrapper", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdchoc4"),
    MapItemSpec("fdpretz1", "Salted Pretzel (Full)", "food", 264, "fdpretz1", "pretzel", "Full", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_snacks", builder_fn="build_fdpretz1"),
    MapItemSpec("fdpretz2", "Salted Pretzel (Broken)", "food", 265, "fdpretz2", "pretzel", "Broken", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_snacks", builder_fn="build_fdpretz2"),
    MapItemSpec("fdpretz3", "Salted Pretzel (Crumbs)", "food", 266, "fdpretz3", "pretzel", "Crumbs", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_snacks", builder_fn="build_fdpretz3"),
    MapItemSpec("fdcrakr1", "Saltine Cracker (Full)", "food", 369, "fdcrakr1", "saltine", "Full", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdcrakr1"),
    MapItemSpec("fdcrakr2", "Saltine Cracker (Nibbled)", "food", 370, "fdcrakr2", "saltine", "Nibbled", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdcrakr2"),
    MapItemSpec("fdcrakr3", "Saltine Cracker (Half)", "food", 371, "fdcrakr3", "saltine", "Half", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdcrakr3"),
    MapItemSpec("fdcrakr4", "Saltine Cracker (Crumbs)", "food", 372, "fdcrakr4", "saltine", "Crumbs", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_snacks", builder_fn="build_fdcrakr4"),
    MapItemSpec("fdcraks1", "Swirled Cracker (Full)", "food", 373, "fdcraks1", "ritz", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_snacks", builder_fn="build_fdcraks1"),
    MapItemSpec("fdcraks2", "Swirled Cracker (Nibbled)", "food", 374, "fdcraks2", "ritz", "Nibbled", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_snacks", builder_fn="build_fdcraks2"),
    MapItemSpec("fdcraks3", "Swirled Cracker (Half)", "food", 375, "fdcraks3", "ritz", "Half", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_snacks", builder_fn="build_fdcraks3"),
    MapItemSpec("fdcraks4", "Swirled Cracker (Crumbs)", "food", 376, "fdcraks4", "ritz", "Crumbs", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_snacks", builder_fn="build_fdcraks4"),
    # --- Food Wave 4D: Candy, Gummy Worms, Fizzy Cola & Fruit Loops (20 items) ---
    MapItemSpec("fdskit1", "ANTS Candies (Pile)", "food", 267, "fdskit1", "skittles", "Pile", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdskit1"),
    MapItemSpec("fdskit2", "ANTS Candies (Cluster)", "food", 268, "fdskit2", "skittles", "Cluster", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdskit2"),
    MapItemSpec("fdskit3", "ANTS Candies (Handful)", "food", 269, "fdskit3", "skittles", "Handful", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdskit3"),
    MapItemSpec("fdskit4", "ANTS Candies (A-N-T-S)", "food", 270, "fdskit4", "skittles", "A-N-T-S", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdskit4"),
    MapItemSpec("fdskit5", "ANTS Candy (Red A)", "food", 271, "fdskit5", "skittles", "Single A", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_candy", builder_fn="build_fdskit5"),
    MapItemSpec("fdgumw1", "Rainbow Gummy Worm (Full)", "food", 272, "fdgumw1", "gummy", "Full", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_candy", builder_fn="build_fdgumw1"),
    MapItemSpec("fdgumw2", "Rainbow Gummy Worm (Bitten)", "food", 273, "fdgumw2", "gummy", "Bitten", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_candy", builder_fn="build_fdgumw2"),
    MapItemSpec("fdgumw3", "Rainbow Gummy Worm (Pieces)", "food", 274, "fdgumw3", "gummy", "Pieces", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_candy", builder_fn="build_fdgumw3"),
    MapItemSpec("fdgumw4", "Rainbow Gummy Worm (Bits)", "food", 275, "fdgumw4", "gummy", "Bits", default_yaw=0.0, default_pitch=50.0, builder_module="items_food_candy", builder_fn="build_fdgumw4"),
    MapItemSpec("fdcola1", "Fizzy Cola Spill (Large)", "food", 276, "fdcola1", "cola_spill", "Large", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_candy", builder_fn="build_fdcola1"),
    MapItemSpec("fdcola2", "Fizzy Cola Spill (Medium)", "food", 277, "fdcola2", "cola_spill", "Medium", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_candy", builder_fn="build_fdcola2"),
    MapItemSpec("fdcola3", "Fizzy Cola Spill (Small)", "food", 278, "fdcola3", "cola_spill", "Small", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_candy", builder_fn="build_fdcola3"),
    MapItemSpec("fdcola4", "Cola Spill Droplets", "food", 279, "fdcola4", "cola_spill", "Drops", default_yaw=0.0, default_pitch=52.0, builder_module="items_food_candy", builder_fn="build_fdcola4"),
    MapItemSpec("fdfrl1", "Fruit Loop (Pink)", "food", 280, "fdfrl1", "frootloop", "Pink", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl1"),
    MapItemSpec("fdfrl2", "Fruit Loop (Purple)", "food", 281, "fdfrl2", "frootloop", "Purple", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl2"),
    MapItemSpec("fdfrl3", "Fruit Loop (Yellow)", "food", 282, "fdfrl3", "frootloop", "Yellow", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl3"),
    MapItemSpec("fdfrl4", "Fruit Loop (Half Yellow)", "food", 283, "fdfrl4", "frootloop", "Half", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl4"),
    MapItemSpec("fdfrl5", "Fruit Loop (Green)", "food", 284, "fdfrl5", "frootloop", "Green", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl5"),
    MapItemSpec("fdfrl6", "Fruit Loop (Broken Purple)", "food", 285, "fdfrl6", "frootloop", "Broken", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl6"),
    MapItemSpec("fdfrl7", "Fruit Loop (Lime Oval)", "food", 286, "fdfrl7", "frootloop", "Lime", default_yaw=0.0, default_pitch=54.0, builder_module="items_food_candy", builder_fn="build_fdfrl7"),
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
    # --- Hills & Gameplay Gear (ALL 14 / 14 COMPLETE!) ---
    MapItemSpec("GREENHILL", "Green Team Anthill", "structures", 248, "GREENHILL", "anthill", "Green", default_yaw=0.0, default_pitch=46.0, builder_module="items_world", builder_fn="build_greenhill"),
    MapItemSpec("REDHILL", "Red Team Anthill", "structures", 247, "REDHILL", "anthill", "Red", default_yaw=0.0, default_pitch=46.0, builder_module="items_structures", builder_fn="build_redhill"),
    MapItemSpec("BLUEHILL", "Blue Team Anthill", "structures", 246, "BLUEHILL", "anthill", "Blue", default_yaw=0.0, default_pitch=46.0, builder_module="items_structures", builder_fn="build_bluehill"),
    MapItemSpec("BLACKHILL", "Black Team Anthill", "structures", 245, "BLACKHILL", "anthill", "Black", default_yaw=0.0, default_pitch=46.0, builder_module="items_structures", builder_fn="build_blackhill"),
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

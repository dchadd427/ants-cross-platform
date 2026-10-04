#!/usr/bin/env python3
"""The default map (run by ./run_tests.sh --fast and by the CI): Treasure is the map that is played most, so it is the default of everything that chooses a map when the person
did not (the owner's request). The C++ suites test the setup screen's highlight (test_app_integration 8.9), the start menu's Host panel (test_start_menu M4.4,
test_start_menu_app A4.6) and a LAN host's room (test_network_app N5.3b). What no C++ test reaches is read here, from the files themselves:

  - docker-compose.stack.yml: the map of a demo room whose code names none (`--demo-map ${ANTS_DEMO_MAP:-TREASURE.LVL}`), the six maps that a code may choose, and the comments that
    say what the default is; the commented example of docker-compose.server.yml. (A real server started with these options makes a Treasure room for a code that names no map:
    tests/scripts/test_ants_server.sh.) An ANTS_DEMO_MAP set in the environment of a stack replaces the default; the file's own default is what is read here.
  - web/lobby.html (the front page): the New match form is preselected on Treasure until a choice is remembered, the order of its list is unchanged (it does not choose the default),
    a remembered choice and ?map= still win, and every place that falls back to a map falls back to the default.
  - the defaults of the program and of the page name the same map.
"""
import json
import os
import re
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stack_command  # noqa: E402  (the parser that tests/scripts/test_ants_server.sh uses too)

SIX_MAPS = ["TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"]
PAGE_ORDER = ["tiny", "small", "medium", "gauntlet", "treasure", "islands"]


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class StackFile(unittest.TestCase):
    def setUp(self):
        self.text = read("docker-compose.stack.yml")
        self.args = stack_command.server_command(self.text)
        self.options = stack_command.demo_options(self.args)

    def test_a_code_that_names_no_map_is_made_on_treasure(self):
        self.assertEqual(self.options.get("--demo-map"), "TREASURE.LVL")
        self.assertIn('"${ANTS_DEMO_MAP:-TREASURE.LVL}"', self.text)         # the variable still overrides it: the default is the part after :-

    def test_the_six_maps_of_the_page_may_still_be_chosen_and_the_default_is_one_of_them(self):
        maps = self.options["--demo-maps"].split(",")
        self.assertEqual(sorted(maps), sorted(SIX_MAPS))
        self.assertIn(self.options["--demo-map"], maps)
        for name in maps:                                                     # (the maps folder of the image holds them: the server stops at its start when one is missing)
            self.assertTrue(os.path.isfile(os.path.join(REPO, "Original-Ants", "Maps", name)), name)

    def test_the_other_demo_options_are_as_they_were(self):
        self.assertEqual(self.options.get("--demo-rooms"), "12")

    def test_the_comments_name_the_same_default(self):
        self.assertIn("ANTS_DEMO_ROOMS=12 ANTS_DEMO_MAP=TREASURE.LVL", self.text)
        self.assertIn("ANTS_DEMO_MAP (default TREASURE.LVL)", self.text)
        for old in ("ANTS_DEMO_MAP=TINY.LVL", "ANTS_DEMO_MAP (default TINY.LVL)", "(default TINY.LVL)"):
            self.assertNotIn(old, self.text)

    def test_the_example_of_the_server_file_names_it_too(self):
        text = read("docker-compose.server.yml")
        match = re.search(r"^\s*#\s*command:\s*(\[[^\n]*--demo-rooms[^\n]*\])\s*$", text, re.MULTILINE)
        self.assertIsNotNone(match, "the commented demo example is gone from docker-compose.server.yml")
        args = json.loads(match.group(1))
        self.assertEqual(args[args.index("--demo-map") + 1], "TREASURE.LVL")
        self.assertEqual(sorted(args[args.index("--demo-maps") + 1].split(",")), sorted(SIX_MAPS))


class PlayOnlinePage(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")

    def maps_of_the_list(self):
        block = re.search(r"var MAPS = \[(.*?)\];", self.page, re.DOTALL)
        self.assertIsNotNone(block)
        return re.findall(r"\{ key: '([a-z]+)', name: '[A-Za-z]+' \}", block.group(1))

    def test_the_list_keeps_its_order_and_the_default_is_treasure(self):
        self.assertEqual(self.maps_of_the_list(), PAGE_ORDER)                # by size: the order of the list is not what chooses the default
        match = re.search(r"var DEFAULT_MAP_KEY = '([a-z]+)';", self.page)
        self.assertIsNotNone(match, "the page names no default map")
        self.assertEqual(match.group(1), "treasure")

    def test_the_form_opens_on_treasure_and_a_remembered_choice_wins(self):
        preselect = self.page.index("mapSelect.value = DEFAULT_MAP_KEY;")
        recalled = self.page.index("recall('ants-four-map')")
        remembered = self.page.index("mapSelect.value = last;")
        self.assertTrue(preselect < recalled < remembered, "the default must be set before the remembered choice is applied")
        self.assertIn("if (last && mapByKey(last)) mapSelect.value = last;", self.page)

    def test_an_address_still_chooses_the_map(self):
        self.assertIn("var wantedMap = params.get('map');", self.page)
        self.assertIn("else if (wantedMap && mapByKey(wantedMap.toLowerCase())) {", self.page)
        self.assertIn("create(wantedMap.toLowerCase(),", self.page)

    def test_every_fallback_to_a_map_is_the_default(self):
        self.assertNotIn("MAPS[0]", self.page)                                # (the first of the list is Tiny)
        self.assertIn("var m = mapByKey(mapKey) || mapByKey(DEFAULT_MAP_KEY);", self.page)


class OneDefaultEverywhere(unittest.TestCase):
    """The program's own choices and the page's name the same map as the stack."""

    def test_the_page_the_stack_the_setup_screen_and_the_host_panel_agree(self):
        stack = stack_command.demo_options(stack_command.server_command(read("docker-compose.stack.yml")))["--demo-map"]
        page = re.search(r"var DEFAULT_MAP_KEY = '([a-z]+)';", read("web", "lobby.html")).group(1)
        screen = re.search(r'DEFAULT_MAP_FILE = "([A-Za-z0-9_.]+)";', read("include", "ants_app", "map_select.hpp")).group(1)
        index = int(re.search(r"kDefaultMenuMap = (\d+);", read("include", "ants_app", "start_menu.hpp")).group(1))
        keys = re.findall(r'\{"([a-z]+)", "[A-Za-z]+"\}', re.search(r"constexpr MenuMap kMaps\[kMenuMapCount\] = \{(.*?)\};", read("src", "ants_app", "start_menu.cpp"), re.DOTALL).group(1))
        self.assertEqual(keys, PAGE_ORDER)                                    # the start menu's list is the page's list
        self.assertEqual(stack, "TREASURE.LVL")
        self.assertEqual(page.upper() + ".LVL", stack)
        self.assertEqual(screen.upper(), stack)
        self.assertEqual(keys[index].upper() + ".LVL", stack)


if __name__ == "__main__":
    unittest.main()

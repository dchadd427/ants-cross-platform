#!/usr/bin/env python3
"""The skills that teach a person's own AI to make maps (ai_skills/, tools/make_ai_skills.py; docs/MAP_AI.md, "The skills"; run by ./run_tests.sh --fast and by the CI).

  TheSkills         six folders and no other, each a SKILL.md whose frontmatter names it and says when to use it, short enough to be read whole; every numeral that a skill states as a limit is one that
                    its sources say; no word of the private kind; a tool, an error or a finding that a skill names is a real one, and every tool is described; every call that a skill shows is a call that the hub
                    takes (hub/aitools.py checks its arguments)
  TheGeneratedBlocks the numbers and names inside the skills are the sources' (a block made again from the sources is what the file holds), a stale block is found by the check, a source that no longer fits the
                    generator (a name, a team, a topic) stops it with a sentence
  TheServedFiles    what the image serves at /ai/skills/: one zip for each skill, all.zip, the folders and index.json; reproducible (the same bytes twice), a zip holds exactly the folder (sorted, a fixed time),
                    index.json says what the page and `read_guide` need, the build needs nothing but ai_skills/ and the tool
  TheWiring         the Dockerfile and build_web.sh make and carry the files, after the compile layers; nothing of the generator's sources is needed in the image

The sources are read where they are (the palette, the checker's table, the hub's tool table, the original maps): a block that a developer forgets to make again fails here.
"""
import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools"))
import check_hygiene  # noqa: E402
import make_ai_skills as tool  # noqa: E402
from hub import aitools  # noqa: E402

TOPICS = tool.TOPICS
SKILLS = os.path.join(REPO, "ai_skills")
CALL = re.compile(r"```json\n(.*?)\n```", re.S)
FENCE = re.compile(r"```.*?```", re.S)
GENERATED = re.compile(r"<!-- generated:([a-z0-9-]+) begin.*?<!-- generated:\1 end -->\n", re.S)
WORD = re.compile(r"`([a-z]+(?:_[a-z0-9]+)+)`")
OTHER_NAMES = {"place_limit", "untrusted_fields", "keep_row", "keep_column", "default_ant", "max_px", "auto_edges"}          # (words with an underscore that are neither a tool, nor an error, nor a finding: arguments are added below)
FORBIDDEN = re.compile(r"\b(owner|audit|attack|exploit|hostile|vulnerab\w*|claude|anthropic|sonnet|opus|haiku|gpt)\b", re.I)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def skill_text(name):
    return read("ai_skills", name, "SKILL.md")


def prose(text):
    """A file's text without its generated blocks and its code blocks: what a person (or an AI) reads as words."""
    return FENCE.sub("", GENERATED.sub("", text))


def numbers(text):
    return {int(n.replace(",", "")) for n in re.findall(r"\d[\d,]*", text) if n.replace(",", "").isdigit()}


def schema_numbers():
    out = set()

    def walk(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key in ("minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems") and isinstance(value, int):
                    out.add(value)
                walk(value)
        elif isinstance(node, list):
            for item in node:
                walk(item)
    for tool_ in aitools.TOOLS.values():
        walk(tool_.schema)
    return out


def calls_of(text):
    """The calls that a text shows: the JSON objects of its ```json blocks that have a `tool`."""
    return [json.loads(body) for body in CALL.findall(text) if '"tool"' in body]


class TheSkills(unittest.TestCase):
    def test_there_are_six_skills_and_no_other_folder(self):
        folders = sorted(n for n in os.listdir(SKILLS) if os.path.isdir(os.path.join(SKILLS, n)))
        self.assertEqual(folders, sorted(TOPICS.values()))
        self.assertEqual(list(TOPICS), list(aitools.GUIDE_TOPICS), "the topics of read_guide are the skills, in the hub's order")
        self.assertEqual([n for n in os.listdir(SKILLS) if not os.path.isdir(os.path.join(SKILLS, n))], [], "no loose file beside the folders")

    def test_each_skill_names_itself_says_when_to_use_it_and_is_short(self):
        for name in TOPICS.values():
            text = skill_text(name)
            meta = tool.frontmatter(text)
            self.assertEqual(sorted(meta), ["description", "name"], name)
            self.assertEqual(meta["name"], name)
            self.assertRegex(meta["name"], tool.NAME)
            self.assertLessEqual(len(meta["name"]), 64)
            self.assertTrue(40 <= len(meta["description"]) <= 1024, "%s: a description that says what the skill is and when to use it (1,024 characters at the most)" % name)
            self.assertLess(text.count("\n"), 500, "%s: every skill is read whole" % name)
            self.assertTrue(text.startswith("---\n") and "\n---\n" in text)
            for base, _, files in os.walk(os.path.join(SKILLS, name)):
                for f in files:
                    self.assertTrue(f == "SKILL.md" or base.endswith("reference") and f.endswith(".md"), os.path.join(base, f))

    def test_a_reference_file_is_named_in_its_skill(self):
        for name in TOPICS.values():
            folder = os.path.join(SKILLS, name, "reference")
            for f in os.listdir(folder) if os.path.isdir(folder) else []:
                self.assertIn("reference/" + f, skill_text(name), "%s/reference/%s is read only when its skill points at it" % (name, f))
                self.assertLess(read("ai_skills", name, "reference", f).count("\n"), 500)

    def test_the_words_are_plain_and_none_of_the_private_kind(self):
        for path, text in tool.skill_files().items():
            self.assertIsNone(FORBIDDEN.search(text), "%s: %s" % (path, FORBIDDEN.search(text) and FORBIDDEN.search(text).group(0)))
            self.assertNotIn("\r", text)
            self.assertNotIn("\t", text)
        files = ["ai_skills/" + p for p in tool.skill_files()] + ["tools/make_ai_skills.py"]
        self.assertEqual(check_hygiene.check_spelling(REPO, files), [], "American English")
        self.assertEqual(check_hygiene.check_forbidden(REPO, files), [])

    def test_every_numeral_that_a_skill_states_as_a_limit_is_one_of_the_sources(self):
        allowed = schema_numbers()
        for text in tool.skill_files().values():
            for block in GENERATED.finditer(text):
                allowed |= numbers(block.group(0))
        for path, text in tool.skill_files().items():
            for m in re.finditer(r"\b(?:at most|up to|no more than)\s+(\d[\d,]*)", prose(text)):
                self.assertIn(int(m.group(1).replace(",", "")), allowed, "%s: %s is not a limit that the sources say" % (path, m.group(0)))

    def test_a_name_in_backticks_is_a_tool_an_argument_an_error_or_a_finding_and_no_tool_is_made_up(self):
        known = set(aitools.TOOLS) | set(aitools.ERRORS) | {code for code, _ in tool.findings()} | OTHER_NAMES
        for t in aitools.TOOLS.values():
            known |= set(t.schema["properties"])
        for path, text in tool.skill_files().items():
            for word in WORD.findall(prose(text)):
                self.assertIn(word, known, "%s names `%s`, which the hub does not have" % (path, word))
        for banned in ("test_play", "request_test_play", "submit_map", "publish_map", "download_map"):
            for path, text in tool.skill_files().items():
                self.assertNotIn(banned, text, path)

    def test_every_tool_is_described_in_the_tools_skill_and_the_guide_names_the_loop(self):
        text = skill_text(TOPICS["tools"])
        for name in aitools.TOOLS:
            self.assertIn("`%s`" % name, text, name)
        guide = skill_text(TOPICS["guide"])
        for name in ("map_info", "put_hill", "put_ants", "put_food", "check_map", "save_draft", "read_guide"):
            self.assertIn("`%s`" % name, guide)
        marked = aitools.mark("map_info", {})
        for key in marked:
            self.assertIn(key, text, "the tools skill says what the hub marks as typed text")

    def test_every_call_that_a_skill_shows_is_a_call_that_the_hub_takes(self):
        shown = 0
        for path, text in tool.skill_files().items():
            for call in calls_of(text):
                self.assertEqual(sorted(call), ["args", "tool"], path)
                self.assertIsNone(aitools.arguments(call["tool"], call["args"]), "%s: %r" % (path, call))
                shown += 1
        self.assertGreaterEqual(shown, 40)

    def test_every_tool_has_an_example_and_the_recipe_uses_the_hills_entrance_rule(self):
        seen = {c["tool"] for c in calls_of(read("ai_skills", TOPICS["tools"], "reference", "examples.md"))}
        self.assertEqual(seen, set(aitools.TOOLS))
        recipe = calls_of(read("ai_skills", TOPICS["layout"], "reference", "recipes.md"))
        hills = [c["args"] for c in recipe if c["tool"] == "put_hill"]
        self.assertEqual(sorted(h["team"] for h in hills), sorted(aitools.TEAMS), "a hill for every color")
        rows = {h["row"] for h in hills}
        self.assertEqual(sorted(rows), [8, 50], "the entrance of the exact mirror: size - 2 - 8")
        for hill in hills:                                     # the hill's cells (one before the entrance, two after) must be on the map
            self.assertTrue(1 <= hill["row"] <= 60 - 3 and 1 <= hill["column"] <= 60 - 3)
        ants = {c["args"]["team"]: c["args"]["count"] for c in recipe if c["tool"] == "put_ants"}
        self.assertEqual(sorted(ants), sorted(aitools.TEAMS))
        self.assertEqual(len(set(ants.values())), 1, "every color has the same number of ants")


class TheGeneratedBlocks(unittest.TestCase):
    def test_the_blocks_of_the_files_are_what_the_sources_say_now(self):
        self.assertEqual(tool.stale(), [])
        names = {m.group(1) for text in tool.skill_files().values() for m in tool.BLOCK.finditer(text)}
        self.assertEqual(names, set(tool.GENERATORS), "every generator is used, and no block is left without one")

    def test_a_block_that_is_changed_by_hand_is_found_and_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = os.path.join(tmp, "ai_skills")
            shutil.copytree(SKILLS, copy)
            path = os.path.join(copy, "ants-map-vocabulary", "SKILL.md")
            with open(path, encoding="utf-8") as f:
                text = f.read()
            with open(path, "w", encoding="utf-8") as f:
                f.write(text.replace("| 485 |", "| 486 |", 1))
            tool.SKILLS, was = copy, tool.SKILLS
            try:
                self.assertEqual(tool.stale(), [("ants-map-vocabulary/SKILL.md", "ground")])
                with contextlib.redirect_stderr(io.StringIO()) as said:
                    self.assertEqual(tool.main(["--check"]), 1)
                self.assertIn("ants-map-vocabulary/SKILL.md", said.getvalue())
                self.assertEqual(tool.write_all(), ["ants-map-vocabulary/SKILL.md"])
                self.assertEqual(tool.stale(), [])
            finally:
                tool.SKILLS = was

    def test_a_generated_block_is_made_again_exactly_and_the_unknown_one_is_refused(self):
        text = skill_text(TOPICS["vocabulary"])
        self.assertEqual(tool.refresh(text, {})[0], text, "making a block again changes nothing")
        with self.assertRaises(ValueError):
            tool.refresh("<!-- generated:nothing begin -->\n<!-- generated:nothing end -->\n", {})

    def test_the_numbers_are_the_sources_numbers(self):
        limits = tool.GENERATORS["limits"]()
        core, online = tool.core_limits(), tool.online_limits()
        for n in (core["wall"], core["classic_columns"], core["classic_cells"], core["ext_side"], core["ext_cells"], core["ext_description"], core["classic_description"], online["kMaxCells"], online["kMaxStartAnts"], online["kHeavyStartAnts"]):
            self.assertIn(format(n, ","), limits)
        self.assertEqual((core["wall"], core["classic_columns"], core["classic_cells"]), (90, 130, 10000), "the limits of a new Classic map (docs/MAP_FORMAT.md)")
        self.assertEqual(core["dictionary"], 1344)
        teams = tool.GENERATORS["teams"]()
        for team in tool.blocks_source()["teams"]:
            self.assertIn("| `%s` | %d | %d |" % (team["key"], team["hill"], team["marker"]), teams)
        self.assertEqual(sorted(t["key"] for t in tool.blocks_source()["teams"]), sorted(aitools.TEAMS))
        self.assertEqual(len(tool.tile_names()), 1344)

    def test_the_names_of_the_palette_are_the_games_and_a_wrong_one_stops_the_tool(self):
        names = tool.tile_names()
        for group, rows in tool.palette().items():
            for row in rows:
                self.assertEqual(names[row[0]], row[1], "%s: tile %d" % (group, row[0]))
        real = tool.tile_names
        try:
            tool.tile_names = lambda: ["x"] * 1344
            with self.assertRaises(ValueError):
                tool.block_objects()
            with self.assertRaises(ValueError):
                tool.block_bigger()
        finally:
            tool.tile_names = real

    def test_the_original_maps_are_read_as_the_documents_say(self):
        """GAMEPLAY.md lists what the six maps hold; the reader of the tool gives the same."""
        expected = {"TINY": (31, 6, 3, 3, 14, 4800, 0, 0), "SMALL": (40, 8, 2, 4, 5, 4000, 2, 2), "MEDIUM": (60, 10, 6, 6, 4, 4900, 0, 1), "GAUNTLET": (60, 10, 6, 6, 2, 1800, 10, 1),
                    "ISLANDS": (60, 12, 4, 8, 10, 5600, 40, 2), "TREASURE": (60, 12, 9, 6, 18, 10950, 20, 0)}
        for name, want in expected.items():
            m = tool.read_map(os.path.join(REPO, "Original-Ants", "Maps", name + ".LVL"))
            self.assertEqual((m["rows"], m["minutes"], m["eggs"], m["ants"], m["piles"], m["points"], m["powerups"], m["droppers"]), (want[0], want[1], want[2], [want[3]], want[4], want[5], want[6], want[7]), name)
        self.assertEqual(tool.read_map(os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL"))["symmetry"], 100)

    def test_the_findings_are_the_documents_table_and_every_one_is_in_the_skill(self):
        codes = tool.findings()
        self.assertGreaterEqual(len(codes), 25)
        self.assertEqual({s for _, s in codes}, {"fatal", "warning", "note"})
        text = skill_text(TOPICS["check-and-fix"])
        for code, _ in codes:
            self.assertIn("`%s`" % code, text, code)

    def test_a_team_an_ant_type_or_a_topic_that_differs_from_the_hubs_stops_the_tool(self):
        real, teams, types = tool.TOPICS, tool.blocks_source, tool.props_source
        try:
            tool.TOPICS = type(real)(list(real.items())[:-1])
            with self.assertRaises(ValueError):
                tool.block_topics()
            tool.TOPICS = real
            tool.blocks_source = lambda: dict(teams(), teams=teams()["teams"][:-1])
            with self.assertRaises(ValueError):
                tool.block_teams()
            tool.blocks_source = teams
            tool.props_source = lambda: dict(types(), types=types()["types"][:-1])
            with self.assertRaises(ValueError):
                tool.block_teams()
        finally:
            tool.TOPICS, tool.blocks_source, tool.props_source = real, teams, types


class TheServedFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.one, cls.two = tempfile.mkdtemp(prefix="ants_skills_"), tempfile.mkdtemp(prefix="ants_skills_")
        cls.index = tool.build(cls.one)
        tool.build(cls.two)
        cls.root = os.path.join(cls.one, "ai", "skills")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.one, ignore_errors=True)
        shutil.rmtree(cls.two, ignore_errors=True)

    def listing(self, root):
        return sorted(os.path.relpath(os.path.join(b, f), root) for b, _, fs in os.walk(root) for f in fs)

    def test_the_tree_is_a_zip_for_each_skill_all_zip_the_folders_and_the_index_and_nothing_else(self):
        want = ["all.zip", "index.json"] + ["%s.zip" % n for n in TOPICS.values()]
        for name in TOPICS.values():
            want += ["%s/%s" % (name, f) for f in sorted(p[len(name) + 1:] for p in tool.skill_files() if p.startswith(name + "/"))]
        self.assertEqual(self.listing(self.root), sorted(want))
        self.assertEqual(sorted(os.listdir(os.path.join(self.one, "ai"))), ["skills"])

    def test_the_same_skills_are_the_same_bytes(self):
        for rel in self.listing(self.root):
            with open(os.path.join(self.root, rel), "rb") as a, open(os.path.join(self.two, "ai", "skills", rel), "rb") as b:
                self.assertEqual(a.read(), b.read(), rel)

    def test_a_zip_holds_exactly_the_folder_sorted_with_a_fixed_time_and_no_marker(self):
        files = {p: tool.served(t) for p, t in tool.skill_files().items()}
        for name in TOPICS.values():
            with zipfile.ZipFile(os.path.join(self.root, name + ".zip")) as z:
                self.assertIsNone(z.testzip())
                mine = {p: t for p, t in files.items() if p.startswith(name + "/")}
                self.assertEqual(z.namelist(), sorted(mine), name)
                self.assertIn(name + "/SKILL.md", z.namelist(), "SKILL.md is at <name>/SKILL.md inside")
                for info in z.infolist():
                    self.assertEqual((info.date_time, info.create_system, info.external_attr >> 16), (tool.ZIP_TIME, 3, 0o644))
                    self.assertEqual(z.read(info).decode("utf-8"), mine[info.filename])
        with zipfile.ZipFile(os.path.join(self.root, "all.zip")) as z:
            self.assertEqual(z.namelist(), sorted(files))
        for text in files.values():
            self.assertNotIn("generated:", text, "the marker lines are not served")
            self.assertNotIn("\n\n\n", text)

    def test_the_folders_are_the_served_text(self):
        for path, text in tool.skill_files().items():
            with open(os.path.join(self.root, path), encoding="utf-8") as f:
                self.assertEqual(f.read(), tool.served(text), path)

    def test_the_index_says_what_the_page_and_read_guide_need(self):
        with open(os.path.join(self.root, "index.json"), encoding="utf-8") as f:
            index = json.load(f)
        self.assertEqual(index, self.index)
        self.assertEqual(sorted(index), ["all", "skills"])
        self.assertEqual([s["name"] for s in index["skills"]], list(TOPICS.values()))
        self.assertEqual([s["topic"] for s in index["skills"]], list(aitools.GUIDE_TOPICS))
        for entry in index["skills"]:
            self.assertEqual(sorted(entry), ["bytes", "description", "files", "name", "topic"])
            self.assertEqual(entry["bytes"], os.path.getsize(os.path.join(self.root, entry["name"] + ".zip")))
            self.assertEqual(entry["description"], tool.frontmatter(skill_text(entry["name"]))["description"])
            self.assertIn("SKILL.md", entry["files"])
            for rel in entry["files"]:
                self.assertTrue(os.path.isfile(os.path.join(self.root, entry["name"], rel)), rel)
            self.assertEqual(TOPICS[entry["topic"]], entry["name"])
        self.assertEqual(index["all"], {"zip": "all.zip", "bytes": os.path.getsize(os.path.join(self.root, "all.zip"))})
        self.assertLess(index["all"]["bytes"], 120 * 1024, "small enough for a phone and for a skill upload")

    def test_the_build_needs_only_the_skills_and_the_tool(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "tools"))
            shutil.copy(os.path.join(REPO, "tools", "make_ai_skills.py"), os.path.join(tmp, "tools"))
            shutil.copytree(SKILLS, os.path.join(tmp, "ai_skills"))
            done = subprocess.run([sys.executable, "-I", os.path.join("tools", "make_ai_skills.py"), "--build", "site"], cwd=tmp, capture_output=True, text=True, timeout=120)
            self.assertEqual(done.returncode, 0, done.stderr)
            with open(os.path.join(tmp, "site", "ai", "skills", "index.json"), "rb") as built, open(os.path.join(self.root, "index.json"), "rb") as mine:
                self.assertEqual(built.read(), mine.read(), "the image makes the same files as a checkout")
            self.assertEqual(subprocess.run([sys.executable, "-I", os.path.join("tools", "make_ai_skills.py")], cwd=tmp, capture_output=True, text=True).returncode, 2, "it says what to do")

    def test_a_skill_without_its_file_or_its_name_stops_the_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = os.path.join(tmp, "ai_skills")
            shutil.copytree(SKILLS, copy)
            tool.SKILLS, was = copy, tool.SKILLS
            try:
                path = os.path.join(copy, "ants-map-terrain", "SKILL.md")
                with open(path, encoding="utf-8") as f:
                    text = f.read()
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace("name: ants-map-terrain", "name: other", 1))
                with self.assertRaises(ValueError):
                    tool.build(os.path.join(tmp, "out"))
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
                os.remove(path)
                with self.assertRaises(ValueError):
                    tool.build(os.path.join(tmp, "out"))
            finally:
                tool.SKILLS = was


class TheWiring(unittest.TestCase):
    def test_the_image_makes_the_files_after_the_compile_and_carries_them_to_the_site(self):
        text = read("Dockerfile")
        for line in ("COPY ai_skills/ /src/ai_skills/", "COPY tools/make_ai_skills.py /src/tools/make_ai_skills.py", "COPY --from=builder /src/ai_site/ai/ /usr/share/nginx/html/ai/"):
            self.assertIn(line + "\n", text, line)
        run = text[text.index("RUN python3 tools/make_ai_skills.py"):].split("\n", 1)[0]
        self.assertIn("--build ai_site", run)
        for output in ("ai_site/ai/skills/index.json", "ai_site/ai/skills/all.zip"):
            self.assertIn("test -s " + output, run, "an empty build stops the image")
        self.assertLess(text.index("RUN python3 tools/map_atlas.py"), text.index("RUN python3 tools/make_ai_skills.py"))
        self.assertLess(text.index("RUN python3 tools/make_ai_skills.py"), text.index("AS runner"), "in the builder stage")
        self.assertLess(text.index("emcmake cmake -B build_web"), text.index("COPY ai_skills/"), "after the compile: the skills do not rebuild the game")
        self.assertNotIn("hub/", run)
        self.assertNotIn("docs/", run)

    def test_the_local_web_build_makes_them_too_and_the_context_does_not_hide_them(self):
        self.assertIn('python3 "${SCRIPT_DIR}/tools/make_ai_skills.py" --build "${DIST_DIR}"', read("build_web.sh"))
        for line in read(".dockerignore").splitlines():
            line = line.strip()
            self.assertFalse(line.startswith("ai_skills") or line in ("tools", "tools/*") or line.startswith("tools/make_ai"), ".dockerignore hides " + line)

    def test_the_tool_reads_nothing_but_the_skills_when_it_builds(self):
        source = read("tools", "make_ai_skills.py")
        build = source[source.index("def build(out_dir):"):source.index("def main(argv=None):")]
        for word in ("read(", "read_bytes(", "aitools(", "REPO"):
            self.assertNotIn(word, build, "the build packs the committed files: " + word)


if __name__ == "__main__":
    unittest.main()

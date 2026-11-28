#!/usr/bin/env python3
"""Lists the PUBLIC text files (export/allowlist.txt, minus export/private.txt) that name something private: the check of the words that must not read in the public tree.

    export/check_export.py [--repo DIR] [--list] [--tsv FILE] [--strict]

REPORT ONLY: it prints a summary and exits 0, however many files it finds, until the changes that cut the crossings have brought the count to zero (export/README.md, "The scan of
words"); then --strict, which exits 1 when anything is found, becomes the gate of the export.
For every tracked text file that classify.py puts on the public list it counts the mentions of private headers and targets, private paths, private documents, private web files,
private tools, the program ants_server, nginx and the stack, and private test targets (KINDS). --list prints one row for every file (area, file, hits, kinds, examples); --tsv writes the
same rows to a file. Extend KINDS when a new private name appears.
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classify as C  # noqa: E402

TEXT = ('.cpp', '.hpp', '.h', '.inc', '.in', '.txt', '.md', '.py', '.js', '.sh', '.bat', '.html', '.css', '.yml', '.yaml', '.json', '.cmake', '.tsv', '.csv', '.webmanifest', '')
SKIP_PREFIX = ('Community-Maps/', 'asset_catalog/sprites', 'asset_catalog/sounds', 'third_party/mbedtls', 'tests/data/lvl_core')
KINDS = collections.OrderedDict([
    ('include-private-header', re.compile(r'#\s*include\s*[<"]ants_(server|ctl|db|history)/')),
    ('link-private-target', re.compile(r'\b(ants_server_core|ants_history_core|ants_thumb|ants_ctl|ants_db|ants_sqlite|ants_history)\b(?!/)')),
    ('server-source-path', re.compile(r'\b(src|include)/ants_(server|ctl|db|history|thumb)\b')),
    ('hub', re.compile(r'(?<![A-Za-z])hub/[a-z_]+\.py|(?<![A-Za-z./])hub/(?:store/)?|python3? -m hub|\bhub\.(?:admin|server|chat|config)\b')),
    ('dockerfile', re.compile(r'\bDockerfile(?:\.[a-z]+)?\b|docker-compose[A-Za-z.-]*\.yml|docker/(?:nginx\.conf|trusted-proxies|turn)')),
    ('private-doc', re.compile(r'\b(?:SERVER|SERVER_REPLAYS|HISTORY_SERVICE|MAP_SUBMISSIONS|MAP_AI|MAP_PAGES|REPLAY_COUNTS|ADDRESS_LIMITS|ANT_3D_MODELS|SITE_NAV|SITE_STATS|HOME_SCREEN|HOME_CHAT|HOME_GAMES|ANALYTICS|HUB(?:_[A-Z]+)?)\.md\b')),
    ('site-page', re.compile(r'(?<![A-Za-z_])(?:lobby\.html|matches?\.html|admin(?:_[a-z_]+)?\.(?:html|js|css)|legal\.html|site_nav\.(?:js|css)|analytics\.js|history_page\.js|hub_here\.js|replay_(?:plays|likes|activity)\.(?:js|css)|map_(?:plays|library)\.js|match_stats\.(?:js|css)|death_map\.(?:js|css)|lobby_(?:rules|chat|direct)\.(?:js|css)|map_picker\.(?:js|css)|mapmaker_(?:ai|ai_tools|submit)\.(?:js|css)|room_sheet|seat_menu|game_modes\.js|game_type\.(?:js|css)|web/home/)')),
    ('private-tool', re.compile(r'tools/(?:web_without_docker|changelog_to_html|seo_files|map_pages|make_ai_skills|image_files_diff|image_manifest|link_inputs|relink_outputs|front_page_art|ant_models)')),
    ('ants_server', re.compile(r'\bants_server\b(?!_core)')),
    ('nginx/stack', re.compile(r'\bnginx\b|portainer|docker compose|docker-compose', re.I)),
    ('server::', re.compile(r'\bserver::|ants::server\b')),
    ('private-test', re.compile(r'\b(?:test_server|test_db|test_ctl|test_history_service|test_wss_match|test_rejoin\b|test_network_app|test_start_menu_app|test_rejoin_app|test_prediction_link)\b')),
    ('private-path', re.compile(r'tests/(?:test_server|test_db|test_ctl)|tests/scripts/(?:test_hub|test_nginx|web_home|web_lobby|test_web_home|test_web_lobby)|third_party/sqlite|imgconv/|ai_skills/')),
])


def area(f):
    if f.startswith('docs/'):
        return 'docs'
    if f.startswith('tests/scripts/'):
        return 'tests/scripts'
    if f.startswith('tests/data/'):
        return 'tests/data'
    if f.startswith(('tests/test_', 'tests/common', 'tests/e2e')):
        return 'tests/C++'
    if f.startswith(('src/', 'include/')):
        return 'src+include'
    if f.startswith(('web/', 'asset_catalog')):
        return 'web'
    if f.startswith('tools/'):
        return 'tools'
    return 'root/build/ci'


def scan(repo, allow, private):
    """(public text file count, rows): a row for every public text file that names something private: (area, file, hits, 'kind=n;kind=n', examples)."""
    pub = C.classify(C.tracked_files(repo), allow, private)[0]
    rows = []
    scanned = 0
    for f in pub:
        base = os.path.basename(f)
        if f.startswith(SKIP_PREFIX) or not (os.path.splitext(f)[1].lower() in TEXT or base in ('LICENSE', 'VERSION', 'CMakeLists.txt', 'CODEOWNERS')):
            continue
        try:
            with open(os.path.join(repo, f), encoding='utf-8', errors='replace') as fh:
                text = fh.read()
        except OSError:
            continue
        scanned += 1
        found = collections.OrderedDict()
        examples = []
        for k, rx in KINDS.items():
            ms = list(rx.finditer(text))
            if ms:
                found[k] = len(ms)
                examples += sorted({m.group(0) for m in ms})[:2]
        if found:
            rows.append((area(f), f, sum(found.values()), ';'.join('%s=%d' % kv for kv in found.items()), ' | '.join(dict.fromkeys(examples))[:140]))
    rows.sort()
    return scanned, rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', default=os.path.dirname(HERE))
    ap.add_argument('--allow', default=os.path.join(HERE, 'allowlist.txt'))
    ap.add_argument('--private', default=os.path.join(HERE, 'private.txt'))
    ap.add_argument('--list', action='store_true', help='print one row for every file that names something private')
    ap.add_argument('--tsv', help='write the rows to this file')
    ap.add_argument('--strict', action='store_true', help='exit 1 when any public text file names something private (the gate of the export; off until the count is zero)')
    args = ap.parse_args(argv)
    allow, private = C.read_list(args.allow), C.read_list(args.private)
    scanned, rows = scan(args.repo, allow, private)
    by_kind = collections.Counter()
    files_of_kind = collections.Counter()
    for _area, _f, _n, kinds, _ex in rows:
        for kv in kinds.split(';'):
            k, n = kv.rsplit('=', 1)
            by_kind[k] += int(n)
            files_of_kind[k] += 1
    by_area = collections.Counter(r[0] for r in rows)
    mode = 'strict' if args.strict else 'report only'
    print('words that must not read in the public tree (%s): %d of %d public text files name something private (%d hits)' % (mode, len(rows), scanned, sum(by_kind.values())))
    if rows:
        print('  by kind: ' + ', '.join('%s %d files' % (k, files_of_kind[k]) for k in KINDS if files_of_kind[k]))
        print('  by area: ' + ', '.join('%s %d' % kv for kv in sorted(by_area.items())))
    if args.list:
        for r in rows:
            print('%-14s %-62s %4d  %s  %s' % r)
    if args.tsv:
        with open(args.tsv, 'w', encoding='utf-8') as o:
            o.write('area\tfile\thits\tkinds\texamples\n')
            for r in rows:
                o.write('\t'.join(map(str, r)) + '\n')
    return 1 if args.strict and rows else 0


if __name__ == '__main__':
    sys.exit(main())

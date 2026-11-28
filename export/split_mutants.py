#!/usr/bin/env python3
"""Splits a mutant spec (tests/data/NAME_mutants.json) into a public half and a private half, so that the public repository never holds a mutant that names something private.

    export/split_mutants.py [--repo DIR] [--allow FILE] [--private FILE] [--write] [--public-note NAME=TEXT ...] [--notes FILE] [--why] [NAME ...]

A mutant is PUBLIC when everything it touches is on the public list (export/classify.py): the file it changes, every path in its test command (a test program, a script, a page or a
folder that the command is given), and every CMake target in its `build`. Any other mutant is PRIVATE. A spec whose mutants are all public is left alone; a spec with none stays whole and
private (its line on export/private.txt); a spec with both is split:

  tests/data/NAME_mutants.json          the public mutants, and the note (reworded: --public-note or --notes), the defaults and the baseline that the public mutants need
  tests/data/NAME_server_mutants.json   the private mutants (private by the pattern tests/data/*_server_mutants.json), with the original note

tools/mutate.py and tests/scripts/mutant_specs.py read NAME_mutants.json together with NAME_server_mutants.json when it is there. The text of every mutant is kept byte for byte: only the
mutants that move are taken out of one file and put into the other, and the `note`, `defaults` and `baseline` are written again for each half.
Without --write nothing is changed: the table says what would happen, and which public file names would have to move between the two lists.
--notes FILE is a JSON object {"NAME": "the public half's note", ...}. A public note that still names something private (the scan of export/check_export.py) is reported.
Exit status: 1 when a file would be split or move between the lists and --write was not given, 2 when a spec cannot be judged (a path that is on neither list) or its second half
is there already and the list has private mutants again; 0 when every list is whole and on the right list (a tree that is already split gives 0).
"""
import argparse
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classify as C  # noqa: E402
import check_export as X  # noqa: E402

DATA = 'tests/data'
SUFFIX = '_mutants.json'
HALF = '_server_mutants.json'
PATH_RX = re.compile(r'(?<![\w./-])(?:\./)?((?:build_web|build)/[\w./+-]+|(?:tests|web|tools|src|include|docs|Original-Ants|Community-Maps|asset_catalog|hub|docker|cmake|third_party|imgconv|export)/[\w./+-]*[\w+])')
MODULE_RX = re.compile(r'\btests\.scripts\.(\w+)')
TARGET_RX = re.compile(r'\b(?:add_executable|add_library)\(\s*(\w+)')


class Sides:
    """Which list a path is on, by the two lists of the export."""

    def __init__(self, repo, allow=None, private=None):
        self.repo = repo
        self.allow = C.read_list(allow or os.path.join(HERE, 'allowlist.txt'))
        self.private = C.read_list(private or os.path.join(HERE, 'private.txt'))
        self.files = set(C.tracked_files(repo))
        self.dirs = {os.path.dirname(f) for f in self.files}
        for d in list(self.dirs):
            while d:
                d = os.path.dirname(d)
                self.dirs.add(d)

    def side(self, path):
        """'public', 'private', 'both' (a mistake in the lists) or None (on neither)"""
        a, p = C.in_list(self.allow, path), C.in_list(self.private, path)
        return 'both' if a and p else 'public' if a else 'private' if p else None


def spec_names(repo):
    """the names (NAME of tests/data/NAME_mutants.json) of the specs that are not private halves"""
    names = []
    for f in sorted(os.listdir(os.path.join(repo, DATA))):
        if f.endswith(SUFFIX) and not f.endswith(HALF):
            names.append(f[:-len(SUFFIX)])
    return names


def private_targets(sides):
    """the CMake targets that a private folder defines (add_executable and add_library in the CMakeLists.txt and .cmake files that are not public)"""
    found = set()
    for f in sorted(sides.files):
        if (f.endswith('CMakeLists.txt') or f.endswith('.cmake')) and sides.side(f) == 'private':
            try:
                with open(os.path.join(sides.repo, f), encoding='utf-8') as fh:
                    found.update(TARGET_RX.findall(fh.read()))
            except OSError:
                pass
    return found


def command_paths(command):
    """the repository paths that a test command names: a program or script, a page, a folder; a built test program stands for its source"""
    out = []
    for m in PATH_RX.finditer(command):
        t = m.group(1)
        if t.startswith('build_web/'):
            continue                                                  # built output of the web build
        if t.startswith('build/'):
            rest = t[len('build/'):]
            if rest.startswith(('tests/', 'src/')):
                t = rest + ('.cpp' if rest.startswith('tests/') else '')
            else:
                t = 'tools/' + rest.split('/')[0] + '.cpp'            # build/lvl_tool, build/bot_arena
        out.append(t)
    out.extend('tests/scripts/%s.py' % m.group(1) for m in MODULE_RX.finditer(command))
    return out


def side_of_paths(paths, sides):
    """('public', None), ('private', the first path that is private) or raises when a path is on neither list"""
    for p in paths:
        if p in sides.dirs and p not in sides.files:                  # a folder the command is given: the folder's files decide nothing here
            continue
        s = sides.side(p)
        if s is None:
            raise ValueError('%s is on neither list of the export' % p)
        if s != 'public':
            return 'private', p
    return 'public', None


def effective(spec, raw):
    m = dict(spec.get('defaults', {}))
    m.update(raw)
    return m


def mutant_side(m, sides, targets):
    """('public', None) or ('private', why) for one mutant (its keys after the defaults)"""
    s, why = side_of_paths([m['file']], sides)
    if s != 'public':
        return 'private', 'changes %s' % why
    s, why = side_of_paths(command_paths(m.get('test', '')), sides)
    if s != 'public':
        return 'private', 'its test names %s' % why
    build = m.get('build') or []
    build = build.split() if isinstance(build, str) else build
    for t in build:
        if t in targets:
            return 'private', 'it builds %s' % t
    return 'public', None


# ---- the text of a spec: where each mutant is, so that moving one changes nothing else ------------------------------------------------------------

def skip_ws(text, i):
    while i < len(text) and text[i] in ' \t\r\n':
        i += 1
    return i


def scan(text):
    """The top-level object of a spec as spans of the text: {'members': [(key, start, end, value_start, value_end)], 'elements': [(start, end)], ...}"""
    dec = json.JSONDecoder()
    i = skip_ws(text, 0)
    if text[i] != '{':
        raise ValueError('the spec is not a JSON object')
    first = i + 1
    i = first
    members, elements = [], []
    while True:
        i = skip_ws(text, i)
        if text[i] == '}':
            break
        start = i
        key, i = dec.raw_decode(text, i)
        i = skip_ws(text, i)
        if text[i] != ':':
            raise ValueError('a ":" is missing after "%s"' % key)
        i = skip_ws(text, i + 1)
        vstart = i
        if key == 'mutants':
            if text[i] != '[':
                raise ValueError('"mutants" is not a list')
            i += 1
            while True:
                i = skip_ws(text, i)
                if text[i] == ']':
                    break
                _, e = dec.raw_decode(text, i)
                elements.append((i, e))
                i = skip_ws(text, e)
                if text[i] == ',':
                    i += 1
            vend = i + 1
        else:
            _, vend = dec.raw_decode(text, i)
        members.append((key, start, vend, vstart, vend))
        i = skip_ws(text, vend)
        if text[i] == ',':
            i += 1
    return {'members': members, 'elements': elements, 'open': first, 'close': i}


def dumps_like(value, original_text):
    """JSON text of `value` in the style of the text it replaces: the indent of its own lines and the escapes"""
    ascii_only = '\\u' in original_text
    lines = original_text.split('\n')
    indent = 0
    if len(lines) > 1:
        m = re.match(r' *', lines[1])
        close = re.match(r' *', lines[-1])
        step = (len(m.group(0)) - len(close.group(0))) if m else 1
        base = len(close.group(0)) if close else 0
        indent = max(step, 1)
    else:
        base = 0
    body = json.dumps(value, indent=indent if len(lines) > 1 else None, ensure_ascii=ascii_only)
    if len(lines) > 1:
        body = body.replace('\n', '\n' + ' ' * base)
    return body


def segments(command):
    """the parts of a shell command that are joined by && (not inside quotes)"""
    out, cur, quote, i = [], [], None, 0
    while i < len(command):
        c = command[i]
        if quote:
            cur.append(c)
            if c == '\\' and quote == '"' and i + 1 < len(command):
                cur.append(command[i + 1])
                i += 1
            elif c == quote:
                quote = None
        elif c in '\'"':
            quote = c
            cur.append(c)
        elif command.startswith('&&', i):
            out.append(''.join(cur).strip())
            cur = []
            i += 1
        else:
            cur.append(c)
        i += 1
    out.append(''.join(cur).strip())
    return [s for s in out if s]


def baseline_half(baseline, want, sides, targets):
    """the entries of a declared baseline that belong to one half ('public' or 'private'): the parts of each chain that are of that side, and the targets of that side"""
    entries = baseline if isinstance(baseline, list) else [baseline]
    kept = []
    for e in entries:
        parts = []
        for seg in segments(e.get('test', '')):
            paths = command_paths(seg)
            side = side_of_paths(paths, sides)[0] if paths else 'both'
            if side == 'both' or side == want:
                parts.append(seg)
        build = e.get('build') or []
        build = build.split() if isinstance(build, str) else list(build)
        build = [t for t in build if (t in targets) == (want == 'private')]
        if parts:
            item = dict(e)
            item['test'] = ' && '.join(parts)
            if build:
                item['build'] = build
            else:
                item.pop('build', None)
            kept.append(item)
    if not kept:
        return None
    return kept if isinstance(baseline, list) else kept[0]


def needed_defaults(defaults, mutants):
    """the default keys that at least one of the mutants does not set itself"""
    return {k: v for k, v in defaults.items() if any(k not in m for m in mutants)}


def member_text(text, spans, key):
    for k, s, e, vs, ve in spans['members']:
        if k == key:
            return text[vs:ve]
    return None


def build_half(text, spans, elements, replace, drop=()):
    """The text of a half: the original with only `elements` (indexes) kept, and some members replaced ({key: json text}) or left out. Blank members are never written."""
    members = spans['members']
    sep = ',\n'
    if len(members) > 1:
        sep = text[members[0][2]:members[1][1]]                       # between two members, as the file has it
    out = [text[:members[0][1]]]
    first = True
    for key, s, e, vs, ve in members:
        if key in drop:
            continue
        if not first:
            out.append(sep)
        first = False
        if key == 'mutants':
            elems = spans['elements']
            esep = ',\n'
            if len(elems) > 1:
                esep = text[elems[0][1]:elems[1][0]]
            body = esep.join(text[elems[i][0]:elems[i][1]] for i in elements)
            head = text[vs:elems[0][0]]
            tail = text[elems[-1][1]:ve]
            out.append(text[s:vs] + head + body + tail)
        elif key in replace:
            out.append(text[s:vs] + replace[key])
        else:
            out.append(text[s:e])
    out.append(text[members[-1][2]:])
    return ''.join(out)


PRIVATE_NOTE = 'The private half of tests/data/%s%s (tools/mutate.py runs it together with the public half): the mutants of files that are not in the public repository, and the ones that only a test that is not there catches. '


def split(name, repo, sides, targets, public_note=None):
    """(plan) for one spec: {'name', 'public', 'private': lists of ids, 'why': {id: why}, 'public_text', 'private_text'}"""
    path = os.path.join(repo, DATA, name + SUFFIX)
    with open(path, encoding='utf-8') as f:
        text = f.read()
    spec = json.loads(text)
    spans = scan(text)
    mutants = spec['mutants']
    pub, priv, why = [], [], {}
    for n, raw in enumerate(mutants):
        m = effective(spec, raw)
        s, w = mutant_side(m, sides, targets)
        (pub if s == 'public' else priv).append(n)
        if w:
            why[raw['id']] = w
    plan = {'name': name, 'total': len(mutants), 'public': pub, 'private': priv, 'why': why, 'public_text': None, 'private_text': None}
    if not pub or not priv:
        return plan
    defaults = spec.get('defaults', {})
    baseline = spec.get('baseline')
    note = spec.get('note', '')
    for half, ids in (('public', pub), ('private', priv)):
        replace, drop = {}, set()
        kept = [mutants[i] for i in ids]
        if 'defaults' in spec:
            d = needed_defaults(defaults, kept)
            if d:
                replace['defaults'] = dumps_like(d, member_text(text, spans, 'defaults'))
            else:
                drop.add('defaults')
        if baseline is not None:
            b = baseline_half(baseline, half, sides, targets)
            if b is not None:
                replace['baseline'] = dumps_like(b, member_text(text, spans, 'baseline'))
            else:
                drop.add('baseline')
        if 'note' in spec:
            n = (public_note if public_note is not None else note) if half == 'public' else PRIVATE_NOTE % (name, SUFFIX) + note
            replace['note'] = dumps_like(n, member_text(text, spans, 'note'))
        plan[half + '_text'] = build_half(text, spans, ids, replace, drop)
    return plan


def public_note_hits(note):
    return sorted({k for k, rx in X.KINDS.items() if rx.search(note)})


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help='the specs to look at (NAME of NAME_mutants.json; default: every spec)')
    ap.add_argument('--repo', default=os.path.dirname(HERE))
    ap.add_argument('--allow', default=os.path.join(HERE, 'allowlist.txt'))
    ap.add_argument('--private', default=os.path.join(HERE, 'private.txt'))
    ap.add_argument('--write', action='store_true', help='write the halves (the lists are not touched: the table says which names move)')
    ap.add_argument('--public-note', action='append', default=[], metavar='NAME=TEXT')
    ap.add_argument('--notes', help='a JSON file {NAME: public note}')
    ap.add_argument('--why', action='store_true', help='print why each private mutant is private')
    args = ap.parse_args(argv)
    sides = Sides(args.repo, args.allow, args.private)
    targets = private_targets(sides)
    notes = {}
    if args.notes:
        with open(args.notes, encoding='utf-8') as f:
            notes.update(json.load(f))
    for item in args.public_note:
        k, _, v = item.partition('=')
        notes[k] = v
    todo = False
    failed = False
    totals = collections.Counter()
    for name in args.names or spec_names(args.repo):
        try:
            plan = split(name, args.repo, sides, targets, notes.get(name))
        except ValueError as e:
            print('%s: %s' % (name, e))
            failed = True
            continue
        fname = '%s/%s%s' % (DATA, name, SUFFIX)
        half = os.path.join(args.repo, DATA, name + HALF)
        listed = sides.side(fname)
        pub, priv = len(plan['public']), len(plan['private'])
        totals['public'] += pub
        totals['private'] += priv
        if os.path.isfile(half):
            with open(half, encoding='utf-8') as f:
                totals['private'] += len(json.load(f)['mutants'])
        want = 'public' if pub else 'private'
        action = []
        if pub and priv:
            action.append('split')
            if os.path.isfile(half):
                print('%s: %s already exists and the list has private mutants again: move them into it by hand' % (name, os.path.basename(half)))
                failed = True
                continue
        if listed != want:
            action.append('list: %s -> %s' % (listed, want))
        if action:
            todo = True
            print('%-44s %4d public %4d private  %s' % (name, pub, priv, ', '.join(action)))
            if args.why:
                for i, w in sorted(plan['why'].items()):
                    print('      %s: %s' % (i, w))
        if plan['public_text'] is not None:
            hits = public_note_hits(json.loads(plan['public_text']).get('note', ''))
            if hits:
                print('    the public note still names: %s' % ', '.join(hits))
            if args.write:
                base = os.path.join(args.repo, DATA)
                with open(os.path.join(base, name + SUFFIX), 'w', encoding='utf-8', newline='\n') as f:
                    f.write(plan['public_text'])
                with open(os.path.join(base, name + HALF), 'w', encoding='utf-8', newline='\n') as f:
                    f.write(plan['private_text'])
    print('mutants: %d public, %d private' % (totals['public'], totals['private']))
    if failed:
        return 2
    return 1 if todo and not args.write else 0


if __name__ == '__main__':
    sys.exit(main())

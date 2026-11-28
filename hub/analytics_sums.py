"""The sums of a day, made from the day's raw events, and the sums of a range of days (docs/ANALYTICS.md, "The numbers"). Pure functions: no file, no clock.

The raw events hold no moment to the second (only the hour), so a visit is told by two small marks that the counter put on the events when it took them (hub/analytics.py `take`): `vs`, 1 on the page view
that starts a visit (the visitor's first page of the day, or one that came more than GAP seconds after its last event), and `lb`, the bin of DURATION_EDGES that the seconds from the start of the visit to
this event fall in. The time of a visit is the bin of its last event; the sums keep the bins, and the average is made from the middle of each bin.

A DAY (`sums_of_day`) is a dictionary of plain numbers and short lists that holds nothing that names a person: no visitor code, no time of an event, no address.
  visitors, new, returning   visitors who sent a page view that day (tracked browsers only); `returning` were seen on one of the 6 days before
  visits, nt                 visits (a run of pages of one visitor with no gap of 30 minutes) and, apart, the visits of browsers that asked not to be tracked
  countries                  {country: [tracked visits, not tracked visits]}
  refs, entry, exit          {site or "": visits} by the first page's referrer, the first page and the last page of the visit; a site that fewer than REF_MIN_VISITORS visitors came from that day is counted under "other"
  browsers, systems, devices, widths, langs     {name: visitors} (widths: a list of 5 buckets)
  dur                        [visits in each of the time bins of DURATION_EDGES + one more], dur_sum the seconds of all visits
  perf                       {"load" or "game": [number of timings, p50, p75, p95] in milliseconds}
  errors                     [[kind, message, source, times, visitors, last day]] (at most ERRORS_MAX; more are counted in `errors_other`)
  vers                       {version of the site: timings} (the days a new version went live)
"""
import collections
import itertools

from . import analytics_events as events

GAP = 1800                                         # seconds: a visitor's pages that are further apart than this are two visits
DURATION_EDGES = (5, 10, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1800, 3600)       # seconds: the bins of a visit's time on the site (the page groups them in six)
ERRORS_MAX = 200
ERROR_GROUPS_MAX = 1000                            # different errors that a day keeps count of while it is read (a page that makes every message new cannot fill the memory): the rest are counted in `errors_other`
REF_MIN_VISITORS = 3                               # a site that fewer visitors than this came from in a day is not kept by name in the sums (they are for good): such a site can name one person (a personal address)
LIST_MAX = 60                                      # entries of a list that a day keeps (the rest are one "other" entry: referrers, entry and exit pages, languages)
PERF_MIN = 20                                      # the page shows a dash for fewer timings than this in a day: the day keeps the numbers

Row = collections.namedtuple("Row", "vis ret kind vs lb country browser system device wb lang path ref ms name msg src ver")


def length_bin(seconds):
    """The bin of DURATION_EDGES that a time in seconds falls in: 0 for under the first edge, len(DURATION_EDGES) for the last edge and more."""
    return sum(1 for edge in DURATION_EDGES if seconds >= edge)


def bin_seconds(index):
    """The middle of a bin in seconds (the last bin is open: it is taken as twice its first edge, as middle() does)."""
    edges = (0,) + DURATION_EDGES
    index = max(0, min(index, len(DURATION_EDGES)))
    high = edges[index + 1] if index + 1 < len(edges) else edges[index] * 2
    return (edges[index] + high) // 2


def percentile(values, p):
    """The value below which `p` percent of the sorted `values` lie (nearest rank)."""
    return values[max(0, -(-len(values) * p // 100) - 1)]


def top(counter, keep=LIST_MAX):
    """{key: count} of the biggest entries; the rest are added under the key `other`."""
    items = sorted(counter.items(), key=lambda item: (-item[1], item[0]))
    out = dict(items[:keep])
    rest = sum(n for _, n in items[keep:])
    if rest:
        out["other"] = out.get("other", 0) + rest
    return out


def visits_of(rows):
    """[[first view, rows of the visit]] for the tracked views and leaves of ONE visitor in the order they came: a view marked `vs` starts a visit (so does the first view, whatever it is marked)."""
    visits = []
    for row in rows:
        if row.kind not in ("view", "leave"):
            continue
        if row.kind == "view" and (row.vs or not visits):
            visits.append([])
        elif not visits:
            continue                                # (a page that was left and whose view was on another day)
        visits[-1].append(row)
    return visits


def sums_of_day(day, rows, signups=0):
    """The sums of the day `day` from its raw events: an iterable of `Row`, each visitor's events together and in the order they came (Store.rows_of), read once."""
    nt = collections.Counter()
    perf = collections.defaultdict(list)
    versions = collections.Counter()
    errors = {}
    errors_over = 0
    countries = collections.defaultdict(lambda: [0, 0])
    refs, ref_visitors, entry, exit_, browsers, systems, devices, langs = (collections.Counter() for _ in range(8))
    widths = [0] * 5
    bins = [0] * (len(DURATION_EDGES) + 1)
    seconds = visits = new = returning = visitors = 0
    for vis, group in itertools.groupby(rows, key=lambda r: r.vis):
        pages = []
        for row in group:
            if row.kind == "visit":
                nt[row.country] += 1
            elif row.kind == "perf":
                perf[row.name].append(row.ms)
                if row.ver:
                    versions[row.ver] += 1
            elif row.kind == "error":
                key = (row.name, row.msg, row.src)
                if key not in errors and len(errors) >= ERROR_GROUPS_MAX:
                    errors_over += 1
                else:
                    found = errors.setdefault(key, [0, set()])
                    found[0] += 1
                    found[1].add(row.vis)
            elif vis and row.kind in ("view", "leave"):
                pages.append(row)
        trips = visits_of(pages)
        if not trips:
            continue
        first = trips[0][0]
        visitors += 1
        if first.ret:
            returning += 1
        else:
            new += 1
        browsers[first.browser or "Other"] += 1
        systems[first.system or "Other"] += 1
        devices[first.device or "Desktop"] += 1
        if first.wb >= 0:
            widths[first.wb] += 1
        if first.lang:
            langs[first.lang] += 1
        came_from = set()
        for trip in trips:
            start = trip[0]
            visits += 1
            countries[start.country][0] += 1
            refs[start.ref] += 1
            if start.ref not in came_from:
                came_from.add(start.ref)
                ref_visitors[start.ref] += 1
            entry[start.path] += 1
            exit_[[r for r in trip if r.kind == "view"][-1].path] += 1
            length = min(trip[-1].lb, len(DURATION_EDGES))
            bins[length] += 1
            seconds += bin_seconds(length)
    for country, n in nt.items():
        countries[country][1] += n
    for ref in [r for r in refs if r and ref_visitors[r] < REF_MIN_VISITORS]:
        refs["other"] += refs.pop(ref)
    perf_sums = {}
    for name, values in perf.items():
        values.sort()
        perf_sums[name] = [len(values), percentile(values, 50), percentile(values, 75), percentile(values, 95)]
    grouped = sorted(([k, m, s, n, len(v), day] for (k, m, s), (n, v) in errors.items()), key=lambda e: (-e[3], e[0], e[1], e[2]))
    return {"visitors": visitors, "new": new, "returning": returning, "visits": visits, "nt": sum(nt.values()),
            "countries": {country: counts for country, counts in sorted(countries.items())},
            "refs": top(refs), "entry": top(entry), "exit": top(exit_), "browsers": dict(browsers), "systems": dict(systems), "devices": dict(devices), "widths": widths, "langs": top(langs),
            "dur": bins, "dur_sum": seconds, "perf": perf_sums, "errors": grouped[:ERRORS_MAX], "errors_other": sum(e[3] for e in grouped[ERRORS_MAX:]) + errors_over, "vers": dict(versions), "signups": signups}


def without_benign(day):
    """The sums of a day without the errors that are a browser's harmless notes (events.is_benign: ResizeObserver's loop messages). The sums of a day that was kept before the counter
    dropped such events still hold them, and a report must not show them or count them; the kept sums are not changed. (`errors_other` is a count with no message: it stays as it is.)"""
    errors = day.get("errors") or ()
    kept = [e for e in errors if not events.is_benign(e[1])]
    return day if len(kept) == len(errors) else dict(day, errors=kept)


# ---- a range of days ---------------------------------------------------------------------------------------------------------------------------

def merged(days):
    """The lists of a range from the sums of its days (a list of dictionaries of `sums_of_day`; a day with nothing is {}): the lists added up, the errors grouped, the middle of the visits' time."""
    out = {"visitors": 0, "new": 0, "returning": 0, "visits": 0, "nt": 0, "signups": 0, "dur_sum": 0, "errors_other": 0}
    counters = {name: collections.Counter() for name in ("refs", "entry", "exit", "browsers", "systems", "devices", "langs")}
    countries = collections.defaultdict(lambda: [0, 0])
    widths = [0] * 5
    bins = [0] * (len(DURATION_EDGES) + 1)
    errors = {}
    for day in days:
        for key in ("visitors", "new", "returning", "visits", "nt", "signups", "dur_sum", "errors_other"):
            out[key] += day.get(key, 0)
        for name, counter in counters.items():
            counter.update(day.get(name, {}))
        for country, (tracked, untracked) in day.get("countries", {}).items():
            countries[country][0] += tracked
            countries[country][1] += untracked
        for i, n in enumerate(day.get("widths", ())):
            widths[i] += n
        for i, n in enumerate(day.get("dur", ())):
            bins[i] += n
        for kind, message, source, n, visitors, last in day.get("errors", ()):
            entry = errors.setdefault((kind, message, source), [0, 0, ""])
            entry[0] += n
            entry[1] += visitors                    # (the visitors of different days are different codes: they are added, as the page says)
            entry[2] = max(entry[2], last)
    out.update({name: top(counter) for name, counter in counters.items()}, countries={c: v for c, v in sorted(countries.items())}, widths=widths, dur=bins,
                errors=sorted(([k, m, s, n, v, last] for (k, m, s), (n, v, last) in errors.items()), key=lambda e: (-e[3], e[0], e[1], e[2])))
    out["dur_mid"] = middle(bins)
    return out


def middle(bins):
    """The middle of the visits' times in seconds, found inside the bin that holds it (the bins are DURATION_EDGES and one more); 0 for no visit."""
    total = sum(bins)
    if not total:
        return 0
    edges = (0,) + DURATION_EDGES
    half, seen = total / 2.0, 0
    for i, n in enumerate(bins):
        if seen + n >= half and n:
            low = edges[i]
            high = edges[i + 1] if i + 1 < len(edges) else low * 2
            return int(low + (high - low) * (half - seen) / n)
        seen += n
    return 0

"""The sums of a day, made from the day's raw events, and the sums of a range of days (docs/ANALYTICS.md, "The numbers"). Pure functions: no file, no clock.

A DAY (`sums_of_day`) is a dictionary of plain numbers and short lists that holds nothing that names a person: no visitor code, no time of an event, no address.
  visitors, new, returning   visitors who sent a page view that day (tracked browsers only); `returning` were seen on one of the 6 days before
  visits, nt                 visits (a run of pages of one visitor with no gap of 30 minutes) and, apart, the visits of browsers that asked not to be tracked
  countries                  {country: [tracked visits, not tracked visits]}
  refs, entry, exit          {site or "": visits} by the first page's referrer, the first page and the last page of the visit
  browsers, systems, devices, widths, langs     {name: visitors} (widths: a list of 5 buckets)
  dur                        [visits in each of the time bins of DURATION_EDGES + one more], dur_sum the seconds of all visits
  perf                       {"load" or "game": [number of timings, p50, p75, p95] in milliseconds}
  errors                     [[kind, message, source, times, visitors, last time]] (at most ERRORS_MAX; more are counted in `errors_other`)
  vers                       {version of the site: timings} (the days a new version went live)
"""
import collections

GAP = 1800                                         # seconds: a visitor's pages that are further apart than this are two visits
DURATION_EDGES = (5, 10, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1800, 3600)       # seconds: the bins of a visit's time on the site (the page groups them in six)
ERRORS_MAX = 200
LIST_MAX = 60                                      # entries of a list that a day keeps (the rest are one "other" entry: referrers, entry and exit pages, languages)
PERF_MIN = 20                                      # the page shows a dash for fewer timings than this in a day: the day keeps the numbers

Row = collections.namedtuple("Row", "at kind vis ret country browser system device wb lang path ref ms name msg src ver")


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
    """[[first view, rows of the visit]] for the tracked views and leaves of ONE visitor in time order."""
    visits = []
    last = None
    for row in rows:
        if row.kind not in ("view", "leave"):
            continue
        if row.kind == "leave" and not visits:
            continue                                # (a page that was left and whose view was on another day)
        if row.kind == "view" and (last is None or row.at - last > GAP):
            visits.append([])
        visits[-1].append(row)
        last = row.at
    return visits


def sums_of_day(rows, signups=0):
    """The sums of one day from its raw events, a list of `Row` in time order."""
    by_visitor = collections.defaultdict(list)
    nt = collections.Counter()
    perf = collections.defaultdict(list)
    versions = collections.Counter()
    errors = {}
    for row in rows:
        if row.kind == "visit":
            nt[row.country] += 1
        elif row.kind == "perf":
            perf[row.name].append(row.ms)
            if row.ver:
                versions[row.ver] += 1
        elif row.kind == "error":
            entry = errors.setdefault((row.name, row.msg, row.src), [0, set(), 0])
            entry[0] += 1
            entry[1].add(row.vis)
            entry[2] = max(entry[2], row.at)
        if row.vis and row.kind in ("view", "leave"):
            by_visitor[row.vis].append(row)
    countries = collections.defaultdict(lambda: [0, 0])
    refs, entry, exit_, browsers, systems, devices, langs = (collections.Counter() for _ in range(7))
    widths = [0] * 5
    bins = [0] * (len(DURATION_EDGES) + 1)
    seconds = 0
    visits = new = returning = 0
    visitors = 0
    for vis, events in by_visitor.items():
        trips = visits_of(events)
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
        for trip in trips:
            start, end = trip[0], trip[-1]
            visits += 1
            countries[start.country][0] += 1
            refs[start.ref] += 1
            entry[start.path] += 1
            exit_[max((r for r in trip if r.kind == "view"), key=lambda r: r.at).path] += 1
            length = max(0, end.at - start.at)
            seconds += length
            bins[sum(1 for edge in DURATION_EDGES if length >= edge)] += 1
    for country, n in nt.items():
        countries[country][1] += n
    perf_sums = {}
    for name, values in perf.items():
        values.sort()
        perf_sums[name] = [len(values), percentile(values, 50), percentile(values, 75), percentile(values, 95)]
    grouped = sorted(([k, m, s, n, len(v), last] for (k, m, s), (n, v, last) in errors.items()), key=lambda e: (-e[3], e[0], e[1], e[2]))
    return {"visitors": visitors, "new": new, "returning": returning, "visits": visits, "nt": sum(nt.values()),
            "countries": {country: counts for country, counts in sorted(countries.items())},
            "refs": top(refs), "entry": top(entry), "exit": top(exit_), "browsers": dict(browsers), "systems": dict(systems), "devices": dict(devices), "widths": widths, "langs": top(langs),
            "dur": bins, "dur_sum": seconds, "perf": perf_sums, "errors": grouped[:ERRORS_MAX], "errors_other": sum(e[3] for e in grouped[ERRORS_MAX:]), "vers": dict(versions), "signups": signups}


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
            entry = errors.setdefault((kind, message, source), [0, 0, 0])
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

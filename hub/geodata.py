"""The country of an address, from a local file (docs/ANALYTICS.md, "The country list"). The data is DB-IP Lite, "IP to Country Lite", which db-ip.com publishes under CC BY 4.0 (the Legal page says so).

THE FILE is what `build` makes of the published CSV (lines `first address,last address,country`): a small binary table that is read into memory once, with the month of the data in its head. Nothing is
looked up on the network while the hub runs: the image gets the file when it is BUILT (Dockerfile.hub, `python -m hub.geodata build OUT [MONTH]`, which tries the month that is asked for, else this
month, else the one before; a build that cannot reach the site goes on without a file, and the counter then counts every country as unknown and says so on the admin page).

  header   8 bytes `ANTSGEO1`, 8 bytes of the month (`2026-11` and a zero), the number of IPv4 ranges and of IPv6 ranges (4 bytes each, little-endian)
  IPv4     starts (4 bytes each), ends (4 bytes each), countries (2 letters each), all little-endian and sorted
  IPv6     the same with the TOP 64 BITS of an address (8 bytes each): the published ranges are never finer than a /48

`Geo.country(ip)` is the two letters of a country, or "" when the address is in no range, is private or is not an address. The address is a text in the caller's hands for as long as the call takes.
"""
import array
import bisect
import csv
import gzip
import io
import ipaddress
import os
import re
import struct
import sys
import tempfile
import urllib.request
from datetime import datetime, timezone

MAGIC = b"ANTSGEO1"
HEADER = struct.Struct("<8s8sII")
CODE = re.compile(r"[A-Z]{2}\Z")
MONTH = re.compile(r"[0-9]{4}-(?:0[1-9]|1[0-2])\Z")
URL = "https://download.db-ip.com/free/dbip-country-lite-%s.csv.gz"
READ_MAX = 96 * 1024 * 1024                     # what is read of the download (the published file is about 4 MB packed and 30 MB open)
CONNECT_SECONDS = 30


class GeoError(Exception):
    pass


def little(values, kind):
    """The array of `kind` as little-endian bytes, whatever the machine is."""
    values = array.array(kind, values)
    if sys.byteorder == "big":
        values.byteswap()
    return values.tobytes()


def from_little(data, kind):
    values = array.array(kind)
    values.frombytes(data)
    if sys.byteorder == "big":
        values.byteswap()
    return values


def parse_rows(lines):
    """The ranges of a CSV as ([(first, last, country)] for IPv4, the same for the top 64 bits of IPv6), sorted, with neighbors of one country joined. A row that is not a range of two addresses of one
    kind and a country of two capital letters (`ZZ` is "unknown": not a country) is left out."""
    found = {4: [], 6: []}
    for row in csv.reader(lines):
        if len(row) < 3:
            continue
        code = row[2].strip().upper()
        if not CODE.match(code) or code == "ZZ":
            continue
        try:
            first, last = ipaddress.ip_address(row[0].strip()), ipaddress.ip_address(row[1].strip())
        except ValueError:
            continue
        if first.version != last.version or int(last) < int(first):
            continue
        shift = 64 if first.version == 6 else 0
        found[first.version].append((int(first) >> shift, int(last) >> shift, code))
    out = []
    for version in (4, 6):
        merged = []
        for first, last, code in sorted(found[version]):
            if merged and merged[-1][2] == code and first <= merged[-1][1] + 1:
                merged[-1] = (merged[-1][0], max(merged[-1][1], last), code)
            else:
                merged.append((first, last, code))
        out.append(merged)
    return out[0], out[1]


def build(lines, month):
    """The bytes of the file for the lines of a CSV and its month (`2026-11`)."""
    if not MONTH.match(month):
        raise GeoError("the month is not like 2026-11")
    v4, v6 = parse_rows(lines)
    if not v4:
        raise GeoError("no range")
    body = [little([r[0] for r in v4], "I"), little([r[1] for r in v4], "I"), "".join(r[2] for r in v4).encode("ascii"),
            little([r[0] for r in v6], "Q"), little([r[1] for r in v6], "Q"), "".join(r[2] for r in v6).encode("ascii")]
    return HEADER.pack(MAGIC, month.encode("ascii"), len(v4), len(v6)) + b"".join(body)


class Geo:
    """The table in memory. `month` is the month of its data ('' for no table), `country(ip)` the two letters of an address."""

    def __init__(self, data=None):
        self.month = ""
        self.starts4 = self.ends4 = self.codes4 = self.starts6 = self.ends6 = self.codes6 = ()
        if data:
            self.load(data)

    @classmethod
    def open(cls, path):
        """The table of a file, or an empty one for a file that is not there or is not whole (the counter then says that it has no list)."""
        try:
            with open(path, "rb") as f:
                return cls(f.read(READ_MAX))
        except (OSError, GeoError):
            return cls()

    def load(self, data):
        if len(data) < HEADER.size:
            raise GeoError("too short")
        magic, month, n4, n6 = HEADER.unpack_from(data)
        text = month.rstrip(b"\0").decode("ascii", "replace")
        if magic != MAGIC or not MONTH.match(text) or len(data) != HEADER.size + n4 * 10 + n6 * 18:
            raise GeoError("not a country list")
        at = HEADER.size

        def take(size):
            nonlocal at
            chunk = data[at:at + size]
            at += size
            return chunk
        self.starts4, self.ends4 = from_little(take(n4 * 4), "I"), from_little(take(n4 * 4), "I")
        self.codes4 = take(n4 * 2).decode("ascii")
        self.starts6, self.ends6 = from_little(take(n6 * 8), "Q"), from_little(take(n6 * 8), "Q")
        self.codes6 = take(n6 * 2).decode("ascii")
        self.month = text

    def country(self, ip):
        if not self.month:
            return ""
        try:
            address = ipaddress.ip_address(ip)
        except ValueError:
            return ""
        if address.version == 6 and address.ipv4_mapped is not None:
            address = address.ipv4_mapped
        if address.version == 4:
            starts, ends, codes, value = self.starts4, self.ends4, self.codes4, int(address)
        else:
            starts, ends, codes, value = self.starts6, self.ends6, self.codes6, int(address) >> 64
        i = bisect.bisect_right(starts, value) - 1
        return codes[i * 2:i * 2 + 2] if i >= 0 and value <= ends[i] else ""


# ---- the build step of the image -------------------------------------------------------------------------------------------------------------

def months(start=None):
    """The month that is asked for (else this month, then the one before), as texts like 2026-11."""
    if start:
        return [start]
    now = datetime.now(timezone.utc)
    previous = (now.year - 1, 12) if now.month == 1 else (now.year, now.month - 1)
    return ["%04d-%02d" % (now.year, now.month), "%04d-%02d" % previous]


def fetch(month, opener=urllib.request.urlopen):
    """The unpacked CSV lines of a month from db-ip.com (at the build, never at run time), or a GeoError."""
    if not MONTH.match(month):
        raise GeoError("the month is not like 2026-11")
    try:
        with opener(URL % month, timeout=CONNECT_SECONDS) as answer:
            packed = answer.read(READ_MAX + 1)
        if len(packed) > READ_MAX:
            raise GeoError("too big")
        data = gzip.decompress(packed)
    except (OSError, EOFError, ValueError) as e:
        raise GeoError(type(e).__name__)
    if len(data) > 8 * READ_MAX:
        raise GeoError("too big")
    return io.StringIO(data.decode("utf-8", "replace"))


def write_atomically(path, data):
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    handle, temp = tempfile.mkstemp(dir=folder)
    try:
        with os.fdopen(handle, "wb") as f:
            f.write(data)
        os.chmod(temp, 0o644)                       # (the hub runs as a user of its own and reads it: a temporary file is made private)
        os.replace(temp, path)
    except BaseException:
        if os.path.exists(temp):
            os.unlink(temp)
        raise


def main(argv, opener=urllib.request.urlopen):
    """`build OUT [MONTH]`: 0 when the file was written, 1 when no month could be fetched (no file is left)."""
    if len(argv) < 2 or argv[0] != "build" or len(argv) > 3:
        sys.stderr.write("usage: python -m hub.geodata build OUT [MONTH]\n")
        return 2
    for month in months(argv[2] if len(argv) == 3 and argv[2] else None):
        try:
            write_atomically(argv[1], build(fetch(month, opener), month))
        except GeoError as e:
            sys.stderr.write("hub.geodata: %s: %s\n" % (month, e))
            continue
        sys.stdout.write("hub.geodata: country list of %s written\n" % month)
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

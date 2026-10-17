#!/usr/bin/env python3
"""Generates the TZif fixtures embedded in the `std/time/zone` tests.

Run it on a machine with tzdata installed (Python 3.9 or later):

    python3 tools/gen_tzif_fixtures.py > fixtures.txt

It prints one Gamma constant per fixture, `NAME :: "<hex of the TZif file>"`, ready to be
pasted into a test. Real zones are read from the system tzdata; the "slim" ones keep the
transitions up to a cut-off year and rely on the POSIX TZ footer after it, as `zic -b slim`
does, and the script checks that each of them gives the same offsets as the complete file
between 1880 and 2100 (using Python's own TZif reader). Synthetic fixtures are built from
a handful of transitions to exercise corners that real zones rarely have.
"""

import datetime
import io
import os
import struct
import sys
import zoneinfo

ZONEINFO = "/usr/share/zoneinfo"


def read_v2(data):
    """Returns (times, idx, types, abbr_bytes, footer) of the 64-bit block of a TZif file."""
    assert data[:4] == b"TZif"
    isut, isstd, leap, timecnt, typecnt, charcnt = struct.unpack(">6l", data[20:44])
    pos = 44 + timecnt * 5 + typecnt * 6 + charcnt + leap * 8 + isstd + isut
    isut, isstd, leap, timecnt, typecnt, charcnt = struct.unpack(">6l", data[pos + 20 : pos + 44])
    pos += 44
    times = list(struct.unpack(">%dq" % timecnt, data[pos : pos + 8 * timecnt]))
    pos += 8 * timecnt
    idx = list(data[pos : pos + timecnt])
    pos += timecnt
    types = []
    for i in range(typecnt):
        off, dst, ai = struct.unpack(">lBB", data[pos + 6 * i : pos + 6 * i + 6])
        types.append((off, dst, ai))
    pos += 6 * typecnt
    chars = data[pos : pos + charcnt]
    pos += charcnt + leap * 12 + isstd + isut
    footer = data[pos:].strip(b"\n")
    return times, idx, types, chars, footer


def build(version, times, idx, types, chars, footer, wide=True, leaps=(), indicators=False, v1=True):
    """Builds a TZif file. `types` are (offset, is_dst, abbreviation index)."""
    def block(w):
        tfmt = ">%d" % len(times) + ("q" if w else "l")
        n = len(types)
        out = struct.pack(tfmt, *times) + bytes(idx)
        for off, dst, ai in types:
            out += struct.pack(">lBB", off, dst, ai)
        out += chars
        for t, corr in leaps:
            out += struct.pack(">q" if w else ">l", t) + struct.pack(">l", corr)
        if indicators:
            out += bytes([i % 2 for i in range(n)]) + bytes([(i + 1) % 2 for i in range(n)])
        return out

    def header(timecnt, typecnt, charcnt, leapcnt, ind):
        ver = b"\0" if version == 0 else str(version).encode()
        return b"TZif" + ver + b"\0" * 15 + struct.pack(
            ">6l", typecnt if ind else 0, typecnt if ind else 0, leapcnt, timecnt, typecnt, charcnt
        )

    if version == 0:
        return header(len(times), len(types), len(chars), len(leaps), indicators) + block(False)
    out = b""
    if v1:
        # A minimal first block, as slim files have.
        out += header(0, 1, 1, 0, False) + struct.pack(">lBB", 0, 0, 0) + b"\0"
    out += header(len(times), len(types), len(chars), len(leaps), indicators) + block(True)
    return out + b"\n" + footer + b"\n"


def real(name):
    with open(os.path.join(ZONEINFO, name), "rb") as f:
        return f.read()


def slim(name, version, cutoff_year):
    """The zone `name` with transitions before `cutoff_year` only, checked against the full file."""
    full = real(name)
    times, idx, types, chars, footer = read_v2(full)
    cutoff = int(datetime.datetime(cutoff_year, 1, 1, tzinfo=datetime.timezone.utc).timestamp())
    keep = [i for i, t in enumerate(times) if t < cutoff]
    out = build(version, [times[i] for i in keep], [idx[i] for i in keep], types, chars, footer)
    a = zoneinfo.ZoneInfo(name)
    b = zoneinfo.ZoneInfo.from_file(io.BytesIO(out))
    t = datetime.datetime(1880, 1, 1, tzinfo=datetime.timezone.utc)
    end = datetime.datetime(2100, 1, 1, tzinfo=datetime.timezone.utc)
    while t < end:
        la, lb = t.astimezone(a), t.astimezone(b)
        assert la.utcoffset() == lb.utcoffset() and la.tzname() == lb.tzname(), (name, t)
        t += datetime.timedelta(hours=7)
    return out


def emit(name, data):
    print('%s :: "%s"' % (name, data.hex()))


def main():
    # Real zones.
    emit("PARIS", slim("Europe/Paris", 3, 2000))
    emit("NEW_YORK", slim("America/New_York", 2, 2010))
    emit("LORD_HOWE", slim("Australia/Lord_Howe", 2, 2010))
    emit("KOLKATA", real("Asia/Kolkata"))
    emit("APIA", real("Pacific/Apia"))
    emit("SAO_PAULO", real("America/Sao_Paulo"))

    # Synthetic fixtures. Instants: 2000-01-01T00:00:00Z = 946684800.
    chars = b"AAA\0BBB\0CCC\0"            # AAA at 0, BBB at 4, CCC at 8
    types = [(0, 0, 0), (3600, 0, 4), (7200, 1, 8)]
    # Version 1 only (32-bit times, leap second records, indicators): AAA until
    # 2000-03-01T00:00Z, BBB until 2000-06-01T00:00Z, then CCC (DST) until 2000-09-01, then BBB.
    t = [951868800, 959817600, 967766400]
    emit("V1_ONLY", build(0, t, [1, 2, 1], types, chars, b"", leaps=[(78796800, 1), (94694401, 2)], indicators=True))
    # The same with 64-bit data and no footer: the last type stays in force.
    emit("NO_FOOTER", build(2, t, [1, 2, 1], types, chars, b"", leaps=[(78796800, 1)], indicators=True))
    # Transitions plus an extended-hours footer (version 3): from the last transition on, DST
    # starts on the first Sunday of April at 25:00 and ends on the last Sunday of October at -1:00.
    emit("EXTENDED", build(3, [t[0]], [1], types, chars, b"BBB-1CCC,M4.1.0/25,M10.5.0/-1"))
    # A single type and a quoted footer, no transitions.
    emit("NO_TRANSITIONS", build(2, [], [], [(10800, 0, 0)], b"+03\0", b"<+03>-3"))
    # A single type with a footer in the southern hemisphere, 'big bang' first transition (v4).
    emit("BIG_BANG", build(4, [-(1 << 59), 946684800], [1, 2], types, chars, b"XST-1XDT,M10.1.0,M4.1.0/3"))


if __name__ == "__main__":
    sys.exit(main())

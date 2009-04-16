#!/usr/bin/python

import sys
import struct
import time
from pprint import pprint

f = open(sys.argv[1])

header_magic, \
        tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt, tzh_timecnt, \
        tzh_typecnt, tzh_charcnt = \
        struct.unpack(">4s16x6l", f.read(44))

if header_magic != "TZif": raise "Bad header magic"

for expr in [ "tzh_ttisgmtcnt", "tzh_ttisstdcnt", "tzh_leapcnt", "tzh_timecnt", "tzh_typecnt", "tzh_charcnt" ]:
    print "% 15s: %s" % (expr, eval(expr))

transitions = zip(
        struct.unpack(">%dl" % tzh_timecnt, f.read(4 * tzh_timecnt)),
        struct.unpack(">%dB" % tzh_timecnt, f.read(tzh_timecnt))
        )

print "Transitions:"
pprint([ "%s -> type %d" % (time.ctime(x[0]), x[1]) for x in transitions ])

types = []

for i in range(tzh_typecnt):
    types.append(struct.unpack(">lbB", f.read(4 + 1 + 1)))

print "Types:"
pprint(types)

abbreviations = f.read(tzh_charcnt)

print "Abbreviations:", repr(abbreviations)

leaps = []

for i in range(tzh_leapcnt):
    leaps.append(struct.unpack(">ll", f.read(8)))

print "Leap seconds:"
pprint([ "%d seconds at %s" % (x[1], time.ctime(x[0])) for x in leaps ])

standard_wall_indicators = struct.unpack(">%dB" % tzh_ttisstdcnt, \
        f.read(tzh_ttisstdcnt))

print "standard/wall indicators:"
pprint(standard_wall_indicators)

utc_local_indicators = struct.unpack(">%dB" % tzh_ttisgmtcnt, \
        f.read(tzh_ttisgmtcnt))

print "utc/local indicators:"
pprint(utc_local_indicators)

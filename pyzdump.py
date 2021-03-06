#!/usr/bin/python

"""Dump zoneinfo files

This was written based on the tzfile.5 manpage, from man-pages 3.01.
"""

__author__ = 'Ohad Lutzky <ohad@lutzky.net>'

import sys
import struct
import time
from pprint import pprint

class TZType:
    """This class represents a timezone type.

    * offset: Offset in seconds from GMT
    * is_dst: Is this a DST time?
    * abbr: Abbreviation"""

    def __init__(self, offset, is_dst, abbr):
        self.offset, self.is_dst, self.abbr = offset, is_dst, abbr
    def __repr__(self):
        return "<TZType %s: UTC%+d dst=%s>" % \
                (self.abbr, self.offset, self.is_dst)

class TZFile:
    def __init__(self, filename):
        self.cached_types = None

        f = open(sys.argv[1])

        header_magic, \
                self.ttisgmtcnt, self.ttisstdcnt, self.leapcnt, \
                self.timecnt, self.typecnt, self.charcnt = \
                struct.unpack(">4s16x6l", f.read(44))

        if header_magic != "TZif": raise "Bad header magic"

        self.transitions = zip(
                struct.unpack(">%dl" % self.timecnt, f.read(4 * self.timecnt)),
                struct.unpack(">%dB" % self.timecnt, f.read(self.timecnt))
                )

        self.types = []

        for i in range(self.typecnt):
            self.types.append(struct.unpack(">lbB", f.read(4 + 1 + 1)))

        self.abbreviations = f.read(self.charcnt)

        self.leaps = []

        for i in range(self.leapcnt):
            self.leaps.append(struct.unpack(">ll", f.read(8)))

        self.standard_wall_indicators = struct.unpack(">%dB" % self.ttisstdcnt,\
                f.read(self.ttisstdcnt))

        self.utc_local_indicators = struct.unpack(">%dB" % self.ttisgmtcnt, \
                f.read(self.ttisgmtcnt))

        f.close()

    def get_abbr(self, abbrind):
        """Return the '\\0'-terminated abbreviation at index abbrind within
        the abbreviations string"""

        return self.abbreviations[abbrind:].split("\x00")[0]

    def get_types(self):
        """tz.get_types() -> list of TZTypes

        Get the local timezone types as a TZType list"""
        if not self.cached_types:
            self.cached_types = [
                    TZType(type[0], [False, True][type[1]],
                        self.get_abbr(type[2]))
                    for type in self.types ]
        return self.cached_types

    def get_transitions(self):
        """tz.get_transitions() -> (timestamp, TZType) list

        Get the list of timezone transitions."""

        # TODO: timestamp may be relative to UTC, local standard, or local wall
        # time. It's UTC in modern systems, but the POSIX standard
        # should support all three. Use standard_wall_indicators and
        # utc_local_indicators.
        return [ ( transition[0], self.get_types()[transition[1]] )
                 for transition in self.transitions ]

    def formatted_transitions(self):
        """tz.formatted_transitions() -> string list

        Get a human-readable list of timezone transitions"""

        return [ "At %s, switch to %s" %
                (time.ctime(transition[0]), transition[1].abbr)
                for transition in self.get_transitions() ]

if __name__ == '__main__':
        my_tzfile = TZFile(sys.argv[1])

        print "Transitions:"
        pprint(my_tzfile.formatted_transitions())
        print "Types:"
        pprint(my_tzfile.get_types())
